import Foundation
import FileProvider
import CryptoKit

func require(_ condition: Bool, _ message: String) throws {
    if !condition { throw NSError(domain: "SignedProviderTest", code: 1, userInfo: [NSLocalizedDescriptionKey: message]) }
}

final class Reply<Value>: @unchecked Sendable {
    private let lock = NSLock()
    private var continuation: CheckedContinuation<Value, Error>?
    init(_ continuation: CheckedContinuation<Value, Error>) { self.continuation = continuation }
    func finish(_ result: Result<Value, Error>) {
        lock.lock()
        let pending = continuation
        continuation = nil
        lock.unlock()
        pending?.resume(with: result)
    }
}

func call(_ connection: NSXPCConnection, operation: (ClientCommunicationProtocol, @escaping @Sendable (Error?) -> Void) -> Void) async throws {
    try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
        let reply = Reply(continuation)
        let proxy = connection.remoteObjectProxyWithErrorHandler { reply.finish(.failure($0)) } as! ClientCommunicationProtocol
        operation(proxy) { error in
            if let error { reply.finish(.failure(error)) }
            else { reply.finish(.success(())) }
        }
    }
}

func syncStatus(_ connection: NSXPCConnection, domain: String) async throws -> [String: Any] {
    let snapshot = try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<[String: Any], Error>) in
        let reply = Reply(continuation)
        let proxy = connection.remoteObjectProxyWithErrorHandler { reply.finish(.failure($0)) } as! ClientCommunicationProtocol
        proxy.getSyncStatus { value, error in
            if let error { reply.finish(.failure(error)) }
            else if let value { reply.finish(.success(value)) }
            else { reply.finish(.failure(NSError(domain: "SignedProviderTest", code: 5))) }
        }
        DispatchQueue.global().asyncAfter(deadline: .now() + 10) {
            reply.finish(.failure(NSError(domain: "SignedProviderTest", code: 6,
                                         userInfo: [NSLocalizedDescriptionKey: "Sync status reply timed out"])))
        }
    }
    try require(snapshot["domainIdentifier"] as? String == domain, "Sync status belongs to another domain")
    try require((snapshot["schemaVersion"] as? NSNumber)?.intValue == 1, "Unexpected sync status schema")
    try require(snapshot["isAuthenticated"] as? Bool == true, "Configured domain status is not authenticated")
    for key in ["activeUploads", "activeDownloads", "activeMetadata", "pendingItems", "errorCount",
                "uploadedBytes", "uploadTotalBytes", "downloadedBytes", "downloadTotalBytes",
                "lastCheckedAt", "lastSyncedAt", "sampledAt"] {
        try require((snapshot[key] as? NSNumber)?.doubleValue ?? -1 >= 0, "Invalid sync status field " + key)
    }
    if snapshot["isSynced"] as? Bool == true {
        try require(snapshot["pendingKnown"] as? Bool == true && snapshot["pendingTruncated"] as? Bool == false,
                    "Synced status lacks a complete pending sample")
        for key in ["activeUploads", "activeDownloads", "activeMetadata", "pendingItems", "errorCount"] {
            try require((snapshot[key] as? NSNumber)?.intValue == 0, "Synced status has outstanding " + key)
        }
    }
    return snapshot
}

func persistGeneration(_ generation: String, key: String, markRemoval: Bool = false) throws {
    let group = Bundle.main.object(forInfoDictionaryKey: "AppGroupIdentifier") as! String
    var values: [String: Any] = [key: generation]
    let removalKey = "fp_removed_domain_" + key.dropFirst("fp_config_generation_".count)
    if markRemoval { values[removalKey] = true }
    CFPreferencesSetMultiple(values as CFDictionary, nil, group as CFString,
                             kCFPreferencesCurrentUser, kCFPreferencesAnyHost)
    try require(CFPreferencesSynchronize(group as CFString, kCFPreferencesCurrentUser, kCFPreferencesAnyHost),
                "Exact-scope credential generation synchronization failed")
    try require(CFPreferencesCopyValue(key as CFString, group as CFString,
                                      kCFPreferencesCurrentUser, kCFPreferencesAnyHost) as? String == generation,
                "Credential generation readback failed")
    if markRemoval {
        try require(CFPreferencesCopyValue(removalKey as CFString, group as CFString,
                                          kCFPreferencesCurrentUser, kCFPreferencesAnyHost) as? Bool == true,
                    "Credential removal tombstone readback failed")
    }
}

func clearConfiguration(_ connection: NSXPCConnection, defaults: UserDefaults, key: String) async throws {
    let generation = UUID().uuidString
    try persistGeneration(generation, key: key, markRemoval: true)
    try await call(connection) { proxy, completion in
        proxy.removeAccountConfig(withGeneration: generation, completionHandler: completion)
    }
}

func assertRemote(_ server: String, domain: String, filename: String, content: Data?) async throws {
    var request = URLRequest(url: URL(string: server)!.appendingPathComponent("dav/spaces").appendingPathComponent(domain).appendingPathComponent(filename))
    request.setValue("Basic " + Data(("review:test-" + domain).utf8).base64EncodedString(), forHTTPHeaderField: "Authorization")
    for _ in 0..<40 {
        let (bytes, response) = try await URLSession.shared.data(for: request)
        let status = (response as! HTTPURLResponse).statusCode
        if let content {
            if status == 200 && bytes == content { return }
        } else if status == 404 { return }
        try await Task.sleep(nanoseconds: 500_000_000)
    }
    throw NSError(domain: "SignedProviderTest", code: 4, userInfo: [NSLocalizedDescriptionKey: "Remote mutation did not arrive for " + filename])
}

func assertTrash(_ server: String, domain: String, filename: String, present: Bool) async throws {
    var request = URLRequest(url: URL(string: server)!.appendingPathComponent("dav/spaces/trash-bin").appendingPathComponent(domain))
    request.httpMethod = "PROPFIND"
    request.setValue("1", forHTTPHeaderField: "Depth")
    request.setValue("Basic " + Data(("review:test-" + domain).utf8).base64EncodedString(), forHTTPHeaderField: "Authorization")
    for _ in 0..<40 {
        let (data, response) = try await URLSession.shared.data(for: request)
        let listed = String(decoding: data, as: UTF8.self).contains(">" + filename + "<")
        if (response as? HTTPURLResponse)?.statusCode == 207 && listed == present { return }
        try await Task.sleep(nanoseconds: 500_000_000)
    }
    throw NSError(domain: "SignedProviderTest", code: 9, userInfo: [NSLocalizedDescriptionKey: "Recycle state did not arrive for " + filename])
}

func assertNativeCapabilities(_ domain: NSFileProviderDomain, enabled: Bool) async throws {
    let registered = try await NSFileProviderManager.domains()
    guard let stored = registered.first(where: { $0.identifier == domain.identifier }) else {
        throw NSError(domain: "SignedProviderTest", code: 10,
                      userInfo: [NSLocalizedDescriptionKey: "Capability probe domain disappeared"])
    }
    print("CAPABILITIES expected=\(enabled) trash=\(stored.supportsSyncingTrash) search=\(stored.supportsStringSearchRequest)")
    try require(stored.supportsSyncingTrash == enabled, "Native trash capability did not persist")
    try require(stored.supportsStringSearchRequest == enabled, "Native search capability did not persist")
}

@main struct SignedProviderTest {
    static func main() async {
        setbuf(stdout, nil)
        let args = CommandLine.arguments
        guard args.count >= 3, args[2].hasPrefix("review-") else { exit(2) }
        let domain = NSFileProviderDomain(identifier: NSFileProviderDomainIdentifier(args[2]), displayName: "OpenCloud isolated review")
        domain.supportsSyncingTrash = true
        domain.supportsStringSearchRequest = true
        do {
            if args[1] == "cleanup-project" {
                guard args.count == 4, args[3].hasPrefix(String(args[2].dropFirst("review-".count)) + ".space.") else { exit(2) }
                let project = NSFileProviderDomain(identifier: NSFileProviderDomainIdentifier(args[3]), displayName: "OpenCloud isolated project identifier")
                try await NSFileProviderManager.remove(project)
                return
            }
            if args[1] == "domains" {
                do {
                    let domains = try await NSFileProviderManager.domains()
                    print("QUERY domains without extension: \(domains.map { $0.identifier.rawValue })")
                } catch { print("QUERY domains without extension error: \(error as NSError)") }
                return
            }
            if args[1] == "cleanup" {
                for attempt in 0..<30 {
                    do { try await NSFileProviderManager.remove(domain); return }
                    catch {
                        if attempt == 29 { throw error }
                        try await Task.sleep(nanoseconds: 500_000_000)
                    }
                }
            }
            // A failed earlier run can leave only this isolated host's test domains.
            if let oldDomains = try? await NSFileProviderManager.domains() {
                for old in oldDomains where old.identifier.rawValue.hasPrefix("review-") && old.identifier != domain.identifier {
                    try await NSFileProviderManager.remove(old)
                }
            }
            // LaunchServices and pluginkit registration are asynchronous.
            for attempt in 0..<20 {
                do { try await NSFileProviderManager.add(domain); break }
                catch {
                    if attempt == 19 { throw error }
                    try await Task.sleep(nanoseconds: 500_000_000)
                }
            }
            print("PASS signed domain registration")
            if args[1] == "register-only" { return }
            try await assertNativeCapabilities(domain, enabled: true)
            domain.supportsSyncingTrash = false
            domain.supportsStringSearchRequest = false
            try await NSFileProviderManager.add(domain)
            try await assertNativeCapabilities(domain, enabled: false)
            domain.supportsSyncingTrash = true
            domain.supportsStringSearchRequest = true
            try await NSFileProviderManager.add(domain)
            try await assertNativeCapabilities(domain, enabled: true)
            print("PASS native capability readback and existing-domain updates")
            // Keep this vector aligned with fileproviderdomainidentity.h. Real
            // project/shared IDs contain two UUIDs separated by '$'. Mock-only
            // lifecycle tests cannot enforce the native identifier restrictions.
            let accountID = String(args[2].dropFirst("review-".count))
            let spaceID = "a0ca6a90-a365-4782-871e-d44447bbc668$a0ca6a90-a365-4782-871e-d44447bbc668"
            let encodedSpace = Data(spaceID.utf8).base64EncodedString()
                .replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_")
                .replacingOccurrences(of: "=", with: "")
            let projectDomain = NSFileProviderDomain(identifier: NSFileProviderDomainIdentifier(accountID + ".space." + encodedSpace),
                displayName: "OpenCloud isolated project identifier")
            try await NSFileProviderManager.add(projectDomain)
            try await NSFileProviderManager.remove(projectDomain)
            let rejectedDomain = NSFileProviderDomain(identifier: NSFileProviderDomainIdentifier(accountID + ":space:" + encodedSpace),
                displayName: "OpenCloud isolated invalid identifier")
            var rejected = false
            do {
                try await NSFileProviderManager.add(rejectedDomain)
            } catch { rejected = true }
            if !rejected { try await NSFileProviderManager.remove(rejectedDomain) }
            try require(rejected, "Native FileProvider must reject the prerelease colon identifier")
            print("PASS native project/shared space identifier registration and invalid-colon regression")
            guard let manager = NSFileProviderManager(for: domain) else { throw NSError(domain: "SignedProviderTest", code: 2) }
            guard let service = try await manager.service(named: NSFileProviderServiceName("eu.opencloud.desktop.ClientCommunicationService"), for: .rootContainer) else { throw NSError(domain: "SignedProviderTest", code: 3) }
            var connection = try await service.fileProviderConnection()
            connection.remoteObjectInterface = NSXPCInterface(with: ClientCommunicationProtocol.self)
            connection.resume()
            defer { connection.invalidate() }
            let identifier: String = try await withCheckedThrowingContinuation { continuation in
                let reply = Reply(continuation)
                let proxy = connection.remoteObjectProxyWithErrorHandler { reply.finish(.failure($0)) } as! ClientCommunicationProtocol
                proxy.getFileProviderDomainIdentifier { identifier, error in
                    if let error { reply.finish(.failure(error)) }
                    else { reply.finish(.success(identifier ?? "")) }
                }
            }
            try require(identifier == domain.identifier.rawValue, "XPC returned another domain")
            print("PASS signed host XPC identity")
            let visibleRoot = try await manager.getUserVisibleURL(for: .rootContainer)
            let foreign = Process()
            foreign.executableURL = URL(fileURLWithPath: args[4])
            foreign.arguments = [visibleRoot.path]
            try foreign.run()
            foreign.waitUntilExit()
            try require(foreign.terminationStatus == 0, "Foreign caller trust test failed")
            let group = Bundle.main.object(forInfoDictionaryKey: "AppGroupIdentifier") as! String
            let defaults = UserDefaults(suiteName: group)!
            let generationKey = "fp_config_generation_" + args[2]
            let initialGeneration = UUID().uuidString
            print("Diagnostic suite synchronize result: \(defaults.synchronize())")
            try persistGeneration(initialGeneration, key: generationKey)
            print("PASS exact-scope generation persistence and readback")
            try await call(connection) { proxy, completion in
                proxy.configureAccount(withUser: "review", userId: "review", serverUrl: args[3],
                    password: "test-" + args[2], davPath: "/dav/spaces/" + args[2] + "/", authType: "basic", generation: initialGeneration, completionHandler: completion)
            }
            print("PASS acknowledged Keychain configuration")
            let replacementGeneration = UUID().uuidString
            try persistGeneration(replacementGeneration, key: generationKey)
            var staleGenerationRejected = false
            do {
                try await call(connection) { proxy, completion in
                    proxy.configureAccount(withUser: "review", userId: "review", serverUrl: args[3],
                        password: "test-" + args[2], davPath: "/dav/spaces/" + args[2] + "/", authType: "basic",
                        generation: initialGeneration, completionHandler: completion)
                }
            } catch { staleGenerationRejected = true }
            try require(staleGenerationRejected, "Warmed extension accepted a generation revoked by the host")
            try await call(connection) { proxy, completion in
                proxy.configureAccount(withUser: "review", userId: "review", serverUrl: args[3],
                    password: "test-" + args[2], davPath: "/dav/spaces/" + args[2] + "/", authType: "basic",
                    generation: replacementGeneration, completionHandler: completion)
            }
            print("PASS cross-process generation replacement invalidates warmed extension state")
            _ = try await syncStatus(connection, domain: args[2])
            print("PASS authenticated domain sync-status schema")
            let secondID = args[2] + "-second"
            let secondDomain = NSFileProviderDomain(identifier: NSFileProviderDomainIdentifier(secondID), displayName: "OpenCloud isolated second account")
            try await NSFileProviderManager.add(secondDomain)
            let secondManager = NSFileProviderManager(for: secondDomain)!
            let secondService = try await secondManager.service(named: NSFileProviderServiceName("eu.opencloud.desktop.ClientCommunicationService"), for: .rootContainer)!
            var secondConnection = try await secondService.fileProviderConnection()
            secondConnection.remoteObjectInterface = NSXPCInterface(with: ClientCommunicationProtocol.self)
            secondConnection.resume()
            defer { secondConnection.invalidate() }
            let secondKey = "fp_config_generation_" + secondID
            let secondGeneration = UUID().uuidString
            try persistGeneration(secondGeneration, key: secondKey)
            try await call(secondConnection) { proxy, completion in
                proxy.configureAccount(withUser: "review", userId: "second", serverUrl: args[3], password: "test-" + secondID,
                    davPath: "/dav/spaces/" + secondID + "/", authType: "basic", generation: secondGeneration, completionHandler: completion)
            }
            if args[1] == "interactive" {
                print("ACTION: Enable OpenCloud Isolated Test under System Settings > General > Login Items & Extensions > File Providers")
                var enabled = false
                for _ in 0..<300 {
                    enabled = try await NSFileProviderManager.domains().first { $0.identifier == domain.identifier }?.userEnabled ?? false
                    if enabled { break }
                    try await Task.sleep(nanoseconds: 1_000_000_000)
                }
                try require(enabled, "Isolated extension was not enabled by the user")
            let rootURL = try await manager.getUserVisibleURL(for: .rootContainer)
            let scope = rootURL.startAccessingSecurityScopedResource()
            defer { if scope { rootURL.stopAccessingSecurityScopedResource() } }
            let coordinator = NSFileCoordinator(filePresenter: nil)
            var found = false
            for _ in 0..<20 {
                var coordinationError: NSError?
                coordinator.coordinate(readingItemAt: rootURL, options: [], error: &coordinationError) { url in
                    found = ((try? FileManager.default.contentsOfDirectory(atPath: url.path)) ?? []).contains("hello.txt")
                }
                if found { break }
                if let coordinationError { print("Enumeration coordination: \(coordinationError)") }
                try await Task.sleep(nanoseconds: 500_000_000)
            }
            try require(found, "Finder root did not enumerate hello.txt")
            var downloaded: Data?
            var readError: NSError?
            coordinator.coordinate(readingItemAt: rootURL.appendingPathComponent("hello.txt"), options: [], error: &readError) { url in
                downloaded = try? Data(contentsOf: url)
            }
            if let readError { throw readError }
            try require(downloaded == Data(("hello " + args[2]).utf8), "Hydration returned wrong account contents")
            print("PASS Finder enumeration and on-demand hydration")
            var observedStatus = false
            for _ in 0..<40 {
                let status = try await syncStatus(connection, domain: args[2])
                if status["pendingKnown"] as? Bool == true,
                   (status["activeUploads"] as? NSNumber)?.intValue == 0,
                   (status["activeDownloads"] as? NSNumber)?.intValue == 0 {
                    observedStatus = true
                    break
                }
                try await Task.sleep(nanoseconds: 500_000_000)
            }
            try require(observedStatus, "Native transfer status never settled after hydration")
            print("PASS signed sync-status snapshot, native pending observation, and settled transfer counts")
            let secondRoot = try await secondManager.getUserVisibleURL(for: .rootContainer)
            let secondScope = secondRoot.startAccessingSecurityScopedResource()
            defer { if secondScope { secondRoot.stopAccessingSecurityScopedResource() } }
            var secondData: Data?
            for _ in 0..<20 {
                coordinator.coordinate(readingItemAt: secondRoot, options: [], error: &readError) { url in
                    _ = try? FileManager.default.contentsOfDirectory(atPath: url.path)
                }
                coordinator.coordinate(readingItemAt: secondRoot.appendingPathComponent("hello.txt"), options: [], error: &readError) { url in
                    secondData = try? Data(contentsOf: url)
                }
                if secondData != nil { break }
                try await Task.sleep(nanoseconds: 500_000_000)
            }
            try require(secondData == Data(("hello " + secondID).utf8), "Second domain used another account's credentials or data")
            print("PASS two active account domains remain isolated")
            let newURL = rootURL.appendingPathComponent("new.txt")
            let renamedURL = rootURL.appendingPathComponent("renamed.txt")
            let localContents = Data("local signed test upload".utf8)
            var writeError: NSError?
            var mutationError: Error?
            coordinator.coordinate(writingItemAt: newURL, options: [], error: &writeError) { url in
                do { try localContents.write(to: url) } catch { mutationError = error }
            }
            if let writeError { throw writeError }
            if let mutationError { throw mutationError }
            try await assertRemote(args[3], domain: args[2], filename: "new.txt", content: localContents)
            coordinator.coordinate(writingItemAt: newURL, options: .forMoving, error: &writeError) { url in
                do { try FileManager.default.moveItem(at: url, to: renamedURL) } catch { mutationError = error }
            }
            if let writeError { throw writeError }
            if let mutationError { throw mutationError }
            try await assertRemote(args[3], domain: args[2], filename: "renamed.txt", content: localContents)
            coordinator.coordinate(writingItemAt: renamedURL, options: .forDeleting, error: &writeError) { url in
                do { try FileManager.default.removeItem(at: url) } catch { mutationError = error }
            }
            if let writeError { throw writeError }
            if let mutationError { throw mutationError }
            try await assertRemote(args[3], domain: args[2], filename: "renamed.txt", content: nil)
            print("PASS Finder upload, rename, and delete")
            let packageURL = rootURL.appendingPathComponent("Review.bundle", isDirectory: true)
            coordinator.coordinate(writingItemAt: packageURL, options: [], error: &writeError) { url in
                do {
                    try FileManager.default.createDirectory(at: url, withIntermediateDirectories: false)
                    try localContents.write(to: url.appendingPathComponent("contents.txt"))
                } catch { mutationError = error }
            }
            if let writeError { throw writeError }
            if let mutationError { throw mutationError }
            try await assertRemote(args[3], domain: args[2], filename: "Review.bundle/contents.txt", content: localContents)
            print("PASS native package directory preserves child contents")
            let trashSource = rootURL.appendingPathComponent("trash-demo.txt")
            let trashRestored = rootURL.appendingPathComponent("trash-restored.txt")
            try localContents.write(to: trashSource)
            try await assertRemote(args[3], domain: args[2], filename: "trash-demo.txt", content: localContents)
            var trashedURL: NSURL?
            try FileManager.default.trashItem(at: trashSource, resultingItemURL: &trashedURL)
            try await assertRemote(args[3], domain: args[2], filename: "trash-demo.txt", content: nil)
            try await assertTrash(args[3], domain: args[2], filename: "trash-demo.txt", present: true)
            guard let trashedURL = trashedURL as URL? else { throw NSError(domain: "SignedProviderTest", code: 8) }
            coordinator.coordinate(writingItemAt: trashedURL, options: .forMoving, error: &writeError) { url in
                do { try FileManager.default.moveItem(at: url, to: trashRestored) } catch { mutationError = error }
            }
            if let writeError { throw writeError }
            if let mutationError { throw mutationError }
            try await assertRemote(args[3], domain: args[2], filename: "trash-restored.txt", content: localContents)
            var trashAgainURL: NSURL?
            try FileManager.default.trashItem(at: trashRestored, resultingItemURL: &trashAgainURL)
            try await assertRemote(args[3], domain: args[2], filename: "trash-restored.txt", content: nil)
            if let trashAgainURL = trashAgainURL as URL? { try FileManager.default.removeItem(at: trashAgainURL) }
            try await assertTrash(args[3], domain: args[2], filename: "trash-restored.txt", present: false)
            print("PASS native Finder trash, restore, and permanent delete")
            var control = URLRequest(url: URL(string: args[3])!.appendingPathComponent("dav/spaces").appendingPathComponent(args[2]).appendingPathComponent(".test-control"))
            control.httpMethod = "POST"
            control.setValue("Basic " + Data(("review:test-" + args[2]).utf8).base64EncodedString(), forHTTPHeaderField: "Authorization")
            control.httpBody = Data("{\"offline\":true}".utf8)
            _ = try await URLSession.shared.data(for: control)
            var offlineData: Data?
            coordinator.coordinate(readingItemAt: rootURL.appendingPathComponent("hello.txt"), options: [], error: &readError) { url in
                offlineData = try? Data(contentsOf: url)
            }
            control.httpBody = Data("{\"offline\":false}".utf8)
            _ = try await URLSession.shared.data(for: control)
            try require(offlineData == downloaded, "Hydrated contents were unavailable offline")
            print("PASS offline access to hydrated contents")

            let remoteFile = "/dav/spaces/" + args[2] + "/hello.txt"
            let fileID = SHA256.hash(data: Data(remoteFile.utf8)).map { String(format: "%02x", $0) }.joined()
            try await manager.evictItem(identifier: NSFileProviderItemIdentifier(fileID))
            let extensionPath = Bundle.main.bundleURL.appendingPathComponent("Contents/PlugIns/FileProviderExt.appex/Contents/MacOS/FileProviderExt").resolvingSymlinksInPath().path
            let processList = Process()
            processList.executableURL = URL(fileURLWithPath: "/bin/ps")
            processList.arguments = ["-ww", "-axo", "pid=,comm="]
            let output = Pipe()
            processList.standardOutput = output
            try processList.run()
            let listing = String(data: output.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
            processList.waitUntilExit()
            var killed = false
            for line in listing.split(separator: "\n") {
                let fields = line.split(maxSplits: 1, whereSeparator: { $0.isWhitespace })
                if fields.count == 2, URL(fileURLWithPath: String(fields[1])).resolvingSymlinksInPath().path == extensionPath, let pid = Int32(fields[0]) {
                    try require(kill(pid, SIGTERM) == 0, "Could not stop isolated extension")
                    killed = true
                }
            }
            try require(killed, "Did not find the isolated extension process")
            try await Task.sleep(nanoseconds: 1_000_000_000)
            connection.invalidate()
            secondConnection.invalidate()
            let restoredService = try await manager.service(named: NSFileProviderServiceName("eu.opencloud.desktop.ClientCommunicationService"), for: .rootContainer)!
            connection = try await restoredService.fileProviderConnection()
            connection.remoteObjectInterface = NSXPCInterface(with: ClientCommunicationProtocol.self)
            connection.resume()
            let restoredSecondService = try await secondManager.service(named: NSFileProviderServiceName("eu.opencloud.desktop.ClientCommunicationService"), for: .rootContainer)!
            secondConnection = try await restoredSecondService.fileProviderConnection()
            secondConnection.remoteObjectInterface = NSXPCInterface(with: ClientCommunicationProtocol.self)
            secondConnection.resume()
            var restoredData: Data?
            coordinator.coordinate(readingItemAt: rootURL.appendingPathComponent("hello.txt"), options: [], error: &readError) { url in
                restoredData = try? Data(contentsOf: url)
            }
            if let readError { throw readError }
            try require(restoredData == downloaded, "Extension restart did not restore credentials and hydrate contents")
            print("PASS eviction and extension relaunch restore from Keychain")
            } else {
                print("SKIP Finder I/O: requires enabling the isolated provider in System Settings; rerun with --interactive")
            }
            try await clearConfiguration(secondConnection, defaults: defaults, key: secondKey)
            try await NSFileProviderManager.remove(secondDomain)
            try await clearConfiguration(connection, defaults: defaults, key: generationKey)
            print("PASS acknowledged Keychain and database cleanup")
            var staleRejected = false
            do {
                try await call(connection) { proxy, completion in
                    proxy.configureAccount(withUser: "review", userId: "review", serverUrl: args[3],
                        password: "test-" + args[2], davPath: "/dav/spaces/" + args[2] + "/", authType: "basic", generation: initialGeneration, completionHandler: completion)
                }
            } catch { staleRejected = true }
            try require(staleRejected, "Pre-sign-out configuration revived removed credentials")
            let freshGeneration = UUID().uuidString
            try persistGeneration(freshGeneration, key: generationKey)
            try await call(connection) { proxy, completion in
                proxy.configureAccount(withUser: "review", userId: "review", serverUrl: args[3],
                    password: "test-" + args[2], davPath: "/dav/spaces/" + args[2] + "/", authType: "basic", generation: freshGeneration, completionHandler: completion)
            }
            try await clearConfiguration(connection, defaults: defaults, key: generationKey)
            print("PASS revoked generation rejected and fresh sign-in accepted")
            try await manager.disconnect(reason: "Isolated provider mode switch test", options: [])
            try await manager.reconnect()
            print("PASS persistent disconnect and reconnect")
            try await NSFileProviderManager.remove(domain)
            print("PASS signed domain removal")
        } catch {
            print("FAIL signed domain test: \(error as NSError)")
            try? await NSFileProviderManager.remove(domain)
            exit(1)
        }
    }
}
