import Foundation

final class AccessReply: @unchecked Sendable {
    private let lock = NSLock()
    private var continuation: CheckedContinuation<Bool, Never>?
    init(_ continuation: CheckedContinuation<Bool, Never>) { self.continuation = continuation }
    func finish(_ allowed: Bool) {
        lock.lock()
        let pending = continuation
        continuation = nil
        lock.unlock()
        pending?.resume(returning: allowed)
    }
}

@main struct UntrustedProviderTest {
    static func main() async {
        guard CommandLine.arguments.count == 2 else { exit(2) }
        do {
            let url = URL(fileURLWithPath: CommandLine.arguments[1])
            let services = try await FileManager.default.fileProviderServicesForItem(at: url)
            guard let service = services[NSFileProviderServiceName("eu.opencloud.desktop.ClientCommunicationService")] else {
                print("FAIL: no endpoint to test"); exit(1)
            }
            let connection = try await service.fileProviderConnection()
            connection.remoteObjectInterface = NSXPCInterface(with: ClientCommunicationProtocol.self)
            connection.resume()
            let allowed: Bool = await withCheckedContinuation { continuation in
                let reply = AccessReply(continuation)
                let proxy = connection.remoteObjectProxyWithErrorHandler { _ in reply.finish(false) } as! ClientCommunicationProtocol
                proxy.getFileProviderDomainIdentifier { _, error in reply.finish(error == nil) }
            }
            connection.invalidate()
            if allowed { print("FAIL: foreign caller reached configuration endpoint"); exit(1) }
            print("PASS foreign signed caller rejected by XPC")
        } catch {
            let failure = error as NSError
            if failure.domain == NSCocoaErrorDomain && failure.code == NSFileReadNoPermissionError {
                print("PASS foreign signed caller denied access to the provider endpoint")
                return
            }
            print("FAIL: could not reach endpoint to test caller rejection: \(error)")
            exit(1)
        }
    }
}
