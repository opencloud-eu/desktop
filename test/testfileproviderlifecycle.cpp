/* This software is in the public domain, furnished "as is", without warranty. */

#include "gui/accountmanager.h"
#include "gui/folderman.h"
#include "gui/macOS/fileprovider.h"
#include "gui/macOS/fileproviderdomainhistory.h"
#include "gui/macOS/fileproviderdomainidentity.h"
#include "gui/macOS/fileproviderdomainmanager.h"
#include "gui/macOS/fileproviderxpc.h"
#include "libsync/accessmanager.h"
#include "libsync/account.h"
#include "libsync/creds/httpcredentials.h"
#include "libsync/graphapi/spacesmanager.h"
#include <QNetworkReply>
#include <QTimer>

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

#import "ClientCommunicationProtocol.h"
#import <FileProvider/FileProvider.h>
#import <objc/runtime.h>

using namespace OCC;
using namespace OCC::Mac;

namespace {
struct Scenario
{
    NSMutableArray<NSFileProviderDomain *> *domains = [NSMutableArray new];
    NSMutableArray *clearReplies = [NSMutableArray new];
    NSMutableArray *configurationRequests = [NSMutableArray new];
    NSMutableArray *addReplies = [NSMutableArray new];
    bool holdAdd = false;
    NSString *suite = nil;
    bool holdConfiguration = false;
    void (^discoveryReply)(NSArray<NSFileProviderDomain *> *, NSError *);
    NSError *discoveryError = nil;
    NSError *removeError = nil;
    NSError *disconnectError = nil;
    NSError *addError = nil;
    NSError *clearError = nil;
    NSString *reportedIdentifier = nil;
    bool holdDiscovery = false;
    bool holdClear = false;
    bool holdStatus = false;
    NSDictionary *statusOverrides = nil;
    NSMutableArray *statusReplies = [NSMutableArray new];
    int additions = 0;
    int removals = 0;
    int disconnections = 0;
    int reconnections = 0;
    int connections = 0;
    NSMutableDictionary *connectionsByDomain = [NSMutableDictionary new];
    QHash<QString, QString> configuredPaths;
    QHash<QString, QString> configuredUsers;
};
Scenario scenario;

NSError *failure()
{
    return [NSError errorWithDomain:@"FileProviderLifecycleTest" code:1 userInfo:@{NSLocalizedDescriptionKey : @"Injected native failure"}];
}
}

@interface TestProviderProxy : NSObject <ClientCommunicationProtocol>
@property NSString *domainId;
@end
@implementation TestProviderProxy
- (void)getSyncStatusWithCompletionHandler:(void (^)(NSDictionary<NSString *, id> *, NSError *))reply
{
    NSMutableDictionary *status = [@{
        @"schemaVersion" : @1,
        @"domainIdentifier" : self.domainId,
        @"activeUploads" : @0,
        @"activeDownloads" : @0,
        @"activeMetadata" : @0,
        @"pendingItems" : @0,
        @"errorCount" : @0,
        @"uploadedBytes" : @0,
        @"uploadTotalBytes" : @0,
        @"downloadedBytes" : @0,
        @"downloadTotalBytes" : @0,
        @"isAuthenticated" : @YES,
        @"isSynced" : @YES,
        @"pendingKnown" : @YES,
        @"pendingTruncated" : @NO,
        @"sampledAt" : @(NSDate.date.timeIntervalSince1970),
        @"lastCheckedAt" : @(NSDate.date.timeIntervalSince1970),
        @"lastSyncedAt" : @(NSDate.date.timeIntervalSince1970),
        @"errorDescription" : @""
    } mutableCopy];
    if (scenario.statusOverrides) {
        [status addEntriesFromDictionary:scenario.statusOverrides];
    }
    if (scenario.holdStatus) {
        [scenario.statusReplies addObject:[^{ reply(status, nil); } copy]];
    } else {
        reply(status, nil);
    }
}
- (void)getFileProviderDomainIdentifierWithCompletionHandler:(void (^)(NSString *, NSError *))reply
{
    reply(scenario.reportedIdentifier ? scenario.reportedIdentifier : self.domainId, nil);
}
- (void)configureAccountWithUser:(NSString *)user
                          userId:(NSString *)userId
                       serverUrl:(NSString *)serverUrl
                        password:(NSString *)password
                         davPath:(NSString *)davPath
{
    [self configureAccountWithUser:user userId:userId serverUrl:serverUrl password:password davPath:davPath authType:@"bearer"];
}
- (void)configureAccountWithUser:(NSString *)user
                          userId:(NSString *)userId
                       serverUrl:(NSString *)serverUrl
                        password:(NSString *)password
                         davPath:(NSString *)davPath
                        authType:(NSString *)authType
{
    scenario.configuredPaths.insert(QString::fromNSString(self.domainId), QString::fromNSString(serverUrl) + QString::fromNSString(davPath));
    scenario.configuredUsers.insert(QString::fromNSString(self.domainId), QString::fromNSString(userId));
}
- (void)configureAccountWithUser:(NSString *)user
                          userId:(NSString *)userId
                       serverUrl:(NSString *)serverUrl
                        password:(NSString *)password
                         davPath:(NSString *)davPath
                        authType:(NSString *)authType
                      generation:(NSString *)generation
               completionHandler:(void (^)(NSError *))reply
{
    void (^configure)(void) = ^{
        NSUserDefaults *defaults = [[NSUserDefaults alloc] initWithSuiteName:scenario.suite];
        NSString *expected = [defaults stringForKey:[@"fp_config_generation_" stringByAppendingString:self.domainId]];
        if (![generation isEqualToString:expected] || [defaults boolForKey:[@"fp_removed_domain_" stringByAppendingString:self.domainId]]) {
            reply(failure());
            return;
        }
        [self configureAccountWithUser:user userId:userId serverUrl:serverUrl password:password davPath:davPath authType:authType];
        reply(nil);
    };
    if (scenario.holdConfiguration) {
        [scenario.configurationRequests addObject:[configure copy]];
    } else {
        configure();
    }
}
- (void)removeAccountConfig
{
}
- (void)removeAccountConfigWithCompletionHandler:(void (^)(NSError *))reply
{
    [self removeAccountConfigWithGeneration:@"" completionHandler:reply];
}
- (void)removeAccountConfigWithGeneration:(NSString *)generation completionHandler:(void (^)(NSError *))reply
{
    if (scenario.holdClear) {
        [scenario.clearReplies addObject:[reply copy]];
    } else {
        if (!scenario.clearError) {
            NSUserDefaults *defaults = [[NSUserDefaults alloc] initWithSuiteName:scenario.suite];
            [defaults setObject:NSUUID.UUID.UUIDString forKey:[@"fp_config_generation_" stringByAppendingString:self.domainId]];
            [defaults removeObjectForKey:[@"fp_removed_domain_" stringByAppendingString:self.domainId]];
            scenario.configuredPaths.remove(QString::fromNSString(self.domainId));
        }
        reply(scenario.clearError);
    }
}
@end

@interface TestProviderConnection : NSObject
@property NSXPCInterface *remoteObjectInterface;
@property (copy) void (^invalidationHandler)(void);
@property (copy) void (^interruptionHandler)(void);
@property TestProviderProxy *proxy;
@end
@implementation TestProviderConnection
- (void)resume
{
}
- (void)invalidate
{
}
- (id)remoteObjectProxyWithErrorHandler:(void (^)(NSError *))handler
{
    return self.proxy;
}
@end

@interface TestProviderService : NSObject
@property NSString *domainId;
@end
@implementation TestProviderService
- (void)getFileProviderConnectionWithCompletionHandler:(void (^)(NSXPCConnection *, NSError *))reply
{
    ++scenario.connections;
    TestProviderConnection *connection = [TestProviderConnection new];
    connection.proxy = [TestProviderProxy new];
    connection.proxy.domainId = self.domainId;
    scenario.connectionsByDomain[self.domainId] = connection;
    reply((NSXPCConnection *)connection, nil);
}
@end

@interface TestProviderManager : NSObject
@property NSString *domainId;
@end
@implementation TestProviderManager
- (void)reconnectWithCompletionHandler:(void (^)(NSError *))reply
{
    ++scenario.reconnections;
    reply(nil);
}
- (void)disconnectWithReason:(NSString *)reason options:(NSFileProviderManagerDisconnectionOptions)options completionHandler:(void (^)(NSError *))reply
{
    ++scenario.disconnections;
    reply(scenario.disconnectError);
}
- (void)getServiceWithName:(NSString *)name
            itemIdentifier:(NSFileProviderItemIdentifier)identifier
         completionHandler:(void (^)(NSFileProviderService *, NSError *))reply
{
    TestProviderService *service = [TestProviderService new];
    service.domainId = self.domainId;
    reply((NSFileProviderService *)service, nil);
}
@end

namespace {
void discover(id, SEL, void (^reply)(NSArray<NSFileProviderDomain *> *, NSError *))
{
    if (scenario.holdDiscovery) {
        scenario.discoveryReply = [reply copy];
    } else {
        reply([scenario.domains copy], scenario.discoveryError);
    }
}
id manager(id, SEL, NSFileProviderDomain *domain)
{
    TestProviderManager *result = [TestProviderManager new];
    result.domainId = domain.identifier;
    return result;
}
void addDomain(id, SEL, NSFileProviderDomain *domain, void (^reply)(NSError *))
{
    ++scenario.additions;
    void (^complete)(void) = ^{
        if (!scenario.addError) {
            for (NSFileProviderDomain *existing in [scenario.domains copy]) {
                if ([existing.identifier isEqualToString:domain.identifier]) {
                    [scenario.domains removeObject:existing];
                }
            }
            [scenario.domains addObject:domain];
        }
        reply(scenario.addError);
    };
    if (scenario.holdAdd) {
        [scenario.addReplies addObject:[complete copy]];
    } else {
        complete();
    }
}
void removeDomain(id, SEL, NSFileProviderDomain *domain, void (^reply)(NSError *))
{
    ++scenario.removals;
    if (!scenario.removeError) {
        [scenario.domains removeObject:domain];
    }
    reply(scenario.removeError);
}

class TestReply : public QNetworkReply
{
public:
    TestReply(const QNetworkRequest &request, QNetworkAccessManager::Operation operation, QByteArray body, QObject *parent)
        : QNetworkReply(parent)
        , _body(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        setHeader(QNetworkRequest::ContentLengthHeader, _body.size());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            Q_EMIT readyRead();
            Q_EMIT finished();
        });
    }
    void abort() override { }
    qint64 bytesAvailable() const override { return _body.size() - _offset + QNetworkReply::bytesAvailable(); }
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto count = std::min<qint64>(maximum, _body.size() - _offset);
        if (!count) {
            return -1;
        }
        memcpy(data, _body.constData() + _offset, count);
        _offset += count;
        return count;
    }

private:
    QByteArray _body;
    qint64 _offset = 0;
};

class TestAccessManager : public OCC::AccessManager
{
public:
    TestAccessManager(const QByteArray *spaces, QObject *parent)
        : OCC::AccessManager(parent)
        , _spaces(spaces)
    {
    }
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *) override
    {
        return new TestReply(request, operation, request.url().path() == QLatin1String("/graph/v1.0/me/drives") ? *_spaces : QByteArray("{}"), this);
    }

private:
    const QByteArray *_spaces;
};

class TestCredentials : public HttpCredentials
{
public:
    explicit TestCredentials(TestAccessManager *am)
        : HttpCredentials(QStringLiteral("test-token"))
        , _am(am)
    {
    }
    OCC::AccessManager *createAM() const override { return _am; }
    void restartOauth() override { }
    void persist() override { }
    void forgetSensitiveData() override
    {
        _accessToken.clear();
        _ready = false;
    }

private:
    TestAccessManager *_am;
};
}

class TestFileProviderLifecycle : public QObject
{
    Q_OBJECT
    std::unique_ptr<FolderMan> _folders;
    FileProvider *_provider = nullptr;
    QByteArray _spaces;
    QString _suite;
    QList<std::pair<Method, IMP>> _methods;

    AccountStatePtr createAccount(bool multipleSpaces = false, const QString &davRoot = QStringLiteral("https://dav.example.org/dav/spaces/"))
    {
        QJsonArray values;
        auto add = [&values, &davRoot](const QString &id, const QString &type) {
            values.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), id}, {QStringLiteral("driveType"), type},
                {QStringLiteral("root"), QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("webDavUrl"), QString(davRoot + id)}}}});
        };
        add(QStringLiteral("personal-space"), QStringLiteral("personal"));
        if (multipleSpaces) {
            add(QStringLiteral("project-space"), QStringLiteral("project"));
            add(QStringLiteral("shares-space"), QStringLiteral("virtual"));
        }
        _spaces = QJsonDocument(QJsonObject{{QStringLiteral("value"), values}}).toJson();
        auto account = Account::create(QUuid::createUuid());
        account->setUrl(QUrl(QStringLiteral("https://login.example.org")));
        auto *am = new TestAccessManager(&_spaces, account.data());
        account->setCredentials(new TestCredentials(am));
        auto state = AccountManager::instance()->addAccount(account);
        // Tests drive only the Graph spaces request, not account connectivity or keychain fetching.
        QObject::disconnect(account.data(), &Account::credentialsFetched, state.data(), nullptr);
        account->spacesManager()->checkReady();
        return state;
    }

    void addExisting(const QString &id)
    {
        NSFileProviderDomain *domain = [[NSFileProviderDomain alloc] initWithIdentifier:id.toNSString() displayName:@"Isolated test domain"];
        if (@available(macOS 26.0, *)) {
            domain.supportsStringSearchRequest = YES;
        }
        if (@available(macOS 13.0, *)) {
            domain.supportsSyncingTrash = YES;
        }
        [scenario.domains addObject:domain];
    }

private Q_SLOTS:
    void initTestCase()
    {
        auto replace = [this](SEL selector, IMP implementation) {
            Method method = class_getClassMethod(NSFileProviderManager.class, selector);
            QVERIFY(method);
            _methods.append({method, method_setImplementation(method, implementation)});
        };
        replace(@selector(getDomainsWithCompletionHandler:), (IMP)discover);
        replace(@selector(managerForDomain:), (IMP)manager);
        replace(@selector(addDomain:completionHandler:), (IMP)addDomain);
        replace(@selector(removeDomain:completionHandler:), (IMP)removeDomain);
    }

    void cleanupTestCase()
    {
        for (const auto &[method, implementation] : _methods) {
            method_setImplementation(method, implementation);
        }
    }

    void init()
    {
        scenario = Scenario{};
        ConfigFile::makeQSettings().clear();
        _suite = QStringLiteral("eu.opencloud.desktop.test.") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        scenario.suite = _suite.toNSString();
        _folders = FolderMan::createInstance(true);
    }

    void cleanup()
    {
        delete _provider;
        _provider = nullptr;
        AccountManager::instance()->shutdown();
        _folders.reset();
        NSUserDefaults *defaults = [[NSUserDefaults alloc] initWithSuiteName:_suite.toNSString()];
        [defaults removePersistentDomainForName:_suite.toNSString()];
        [defaults synchronize];
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void testCredentialRevocationPersistence()
    {
        const auto domain = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(FileProviderXPC::recordAccountRemoval(domain, _suite));
        const auto suite = (__bridge CFStringRef)_suite.toNSString();
        QVERIFY(CFPreferencesSynchronize(suite, kCFPreferencesCurrentUser, kCFPreferencesAnyHost));
        auto generation = CFPreferencesCopyValue((__bridge CFStringRef)[@"fp_config_generation_" stringByAppendingString:domain.toNSString()], suite,
            kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
        QVERIFY(generation);
        const QString firstGeneration = QString::fromNSString((__bridge NSString *)generation);
        CFRelease(generation);
        QVERIFY(!QUuid(firstGeneration).isNull());
        auto removed = CFPreferencesCopyValue(
            (__bridge CFStringRef)[@"fp_removed_domain_" stringByAppendingString:domain.toNSString()], suite, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
        QVERIFY(removed && CFEqual(removed, kCFBooleanTrue));
        CFRelease(removed);
        QVERIFY(FileProviderXPC::recordAccountRemoval(domain, _suite));
        NSUserDefaults *defaults = [[NSUserDefaults alloc] initWithSuiteName:_suite.toNSString()];
        QVERIFY(QString::fromNSString([defaults stringForKey:[@"fp_config_generation_" stringByAppendingString:domain.toNSString()]]) != firstGeneration);
    }

    void testDomainIdentityAndIsolation()
    {
        const auto first = QUuid::createUuid();
        const auto second = QUuid::createUuid();
        const auto personal = fileProviderDomainIdentifier(first, QStringLiteral("personal"), true);
        QCOMPARE(personal, first.toString(QUuid::WithoutBraces));
        const auto project = fileProviderDomainIdentifier(first, QString::fromUtf8("Team:Résumé/☁"), false);
        const auto parsed = fileProviderDomainIdentity(project);
        QVERIFY(parsed);
        QCOMPARE(parsed->account, first);
        QCOMPARE(parsed->spaceId, QString::fromUtf8("Team:Résumé/☁"));
        QVERIFY(!project.contains(QLatin1Char(':')));
        QVERIFY(!project.contains(QLatin1Char('/')));
        QVERIFY(project != fileProviderDomainIdentifier(second, parsed->spaceId, false));
        const auto realisticSpace = QStringLiteral("a0ca6a90-a365-4782-871e-d44447bbc668$a0ca6a90-a365-4782-871e-d44447bbc668");
        const auto realisticDomain = fileProviderDomainIdentifier(first, realisticSpace, false);
        QCOMPARE(fileProviderDomainIdentity(realisticDomain)->spaceId, realisticSpace);
        QCOMPARE(fileProviderDomainIdentity(QString(realisticDomain).replace(QStringLiteral(".space."), QStringLiteral(":space:")))->spaceId, realisticSpace);
        QVERIFY(!fileProviderDomainIdentity(personal + QStringLiteral(":space:!!!")));
        QVERIFY(!fileProviderDomainIdentity(personal + QStringLiteral(":space:")));
        QVERIFY(!fileProviderDomainIdentity(personal + QStringLiteral(".space.")));
        QVERIFY(!fileProviderDomainIdentity(personal + QStringLiteral(".space.Zg==")));
        QVERIFY(!fileProviderDomainIdentity(personal + QStringLiteral(".space.Zg:trailing")));
        QVERIFY(!fileProviderDomainIdentity(QStringLiteral("icloud")));
    }

    void testPersonalMigrationAndProjectSpaces()
    {
        const auto account = createAccount(true);
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        const auto personal = account->account()->uuid().toString(QUuid::WithoutBraces);
        addExisting(personal);
        FileProviderXPC xpc(nullptr, _suite);
        FileProviderDomainManager domains(nullptr, &xpc);
        connect(&domains, &FileProviderDomainManager::domainSetupComplete, &xpc, &FileProviderXPC::connectToFileProviderDomains);
        domains.start();
        QTRY_COMPARE(scenario.configuredPaths.size(), 3);
        QCOMPARE(scenario.additions, 2);
        QCOMPARE(scenario.configuredPaths.value(personal), QStringLiteral("https://dav.example.org/dav/spaces/personal-space"));
        for (const auto &space : {QStringLiteral("project-space"), QStringLiteral("shares-space")}) {
            const auto id = fileProviderDomainIdentifier(account->account()->uuid(), space, false);
            QCOMPARE(scenario.configuredPaths.value(id), QStringLiteral("https://dav.example.org/dav/spaces/") + space);
            QCOMPARE(scenario.configuredUsers.value(id), personal);
        }
    }

    void testVirtualSharesKeepsDomainWithoutUnsupportedCapabilities()
    {
        const auto account = createAccount(true);
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        const auto personal = account->account()->uuid().toString(QUuid::WithoutBraces);
        const auto project = fileProviderDomainIdentifier(account->account()->uuid(), QStringLiteral("project-space"), false);
        const auto shares = fileProviderDomainIdentifier(account->account()->uuid(), QStringLiteral("shares-space"), false);
        for (const auto &id : {personal, project, shares}) {
            addExisting(id);
        }
        FileProviderXPC xpc(nullptr, _suite);
        FileProviderDomainManager domains(nullptr, &xpc);
        connect(&domains, &FileProviderDomainManager::domainSetupComplete, &xpc, &FileProviderXPC::connectToFileProviderDomains);
        domains.start();
        QTRY_COMPARE(scenario.configuredPaths.size(), 3);
        QTRY_COMPARE(scenario.additions, 1);
        QCOMPARE(scenario.removals, 0);
        QCOMPARE(scenario.domains.count, 3UL);
        for (NSFileProviderDomain *domain in scenario.domains) {
            const bool physical = QString::fromNSString(domain.identifier) != shares;
            if (@available(macOS 26.0, *)) {
                QCOMPARE(bool(domain.supportsStringSearchRequest), physical);
            }
            if (@available(macOS 13.0, *)) {
                QCOMPARE(bool(domain.supportsSyncingTrash), physical);
            }
        }
    }

    void testSpaceRevokedDuringRegistrationIsNotLeftMounted()
    {
        const auto account = createAccount();
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        FileProviderXPC xpc(nullptr, _suite);
        FileProviderDomainManager domains(nullptr, &xpc);
        connect(&domains, &FileProviderDomainManager::domainSetupComplete, &xpc, &FileProviderXPC::connectToFileProviderDomains);
        scenario.holdAdd = true;
        domains.start();
        QTRY_COMPARE(scenario.addReplies.count, 1UL);
        _spaces = QByteArray("{\"value\":[]}");
        Q_EMIT account->account()->credentialsFetched();
        QTRY_VERIFY(account->account()->spacesManager()->spaces().isEmpty());
        void (^registered)(void) = scenario.addReplies.firstObject;
        registered();
        QTRY_COMPARE(scenario.removals, 1);
        QVERIFY(scenario.configuredPaths.isEmpty());
        QCOMPARE(scenario.domains.count, 0UL);
    }

    void testSpaceReappearingDuringCleanupKeepsItsDomain()
    {
        const auto account = createAccount();
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        const auto id = account->account()->uuid().toString(QUuid::WithoutBraces);
        addExisting(id);
        FileProviderXPC xpc(nullptr, _suite);
        FileProviderDomainManager domains(nullptr, &xpc);
        connect(&domains, &FileProviderDomainManager::domainSetupComplete, &xpc, &FileProviderXPC::connectToFileProviderDomains);
        domains.start();
        QTRY_COMPARE(scenario.configuredPaths.size(), 1);
        scenario.holdClear = true;
        const auto original = _spaces;
        _spaces = QByteArray("{\"value\":[]}");
        Q_EMIT account->account()->credentialsFetched();
        QTRY_COMPARE(scenario.clearReplies.count, 1UL);
        _spaces = original;
        Q_EMIT account->account()->credentialsFetched();
        QTRY_COMPARE(account->account()->spacesManager()->spaces().size(), 1);
        NSUserDefaults *defaults = [[NSUserDefaults alloc] initWithSuiteName:_suite.toNSString()];
        [defaults removeObjectForKey:[@"fp_removed_domain_" stringByAppendingString:id.toNSString()]];
        void (^cleared)(NSError *) = scenario.clearReplies.firstObject;
        cleared(nil);
        QTest::qWait(20);
        QCOMPARE(scenario.removals, 0);
        QCOMPARE(scenario.domains.count, 1UL);
    }

    void testNativeRegistrationErrorsAreReported()
    {
        const auto account = createAccount();
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        scenario.addError = failure();
        _provider = new FileProvider(nullptr, _suite);
        QTRY_VERIFY(!_provider->error().isEmpty());
        QVERIFY(_provider->error().contains(QStringLiteral("Injected native failure")));
        QVERIFY(!_provider->ready());
    }

    void testSyncStatusRequiresObservedIdleAndRejectsStaleSnapshots()
    {
        const auto account = createAccount();
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        scenario.holdStatus = true;
        _provider = new FileProvider(nullptr, _suite);
        QTRY_VERIFY(scenario.statusReplies.count > 0);
        QVERIFY(!_provider->ready()); // Credentials alone cannot prove synchronization.
        void (^reply)(void) = scenario.statusReplies.firstObject;
        reply();
        QTRY_VERIFY(_provider->ready());
        QVERIFY(_provider->syncStatusText().contains(QStringLiteral("Last synced:")));
        scenario.holdStatus = false;
        scenario.statusOverrides = @{@"activeUploads" : @1, @"uploadedBytes" : @256, @"uploadTotalBytes" : @1024};
        _provider->xpc()->refreshSyncStatus();
        QTRY_VERIFY(!_provider->ready());
        QVERIFY(_provider->syncStatusText().contains(QStringLiteral("Uploading: 1")));
        scenario.statusOverrides = @{@"errorCount" : @1, @"errorDescription" : @"Upload denied"};
        _provider->xpc()->refreshSyncStatus();
        QTRY_VERIFY(_provider->error().contains(QStringLiteral("Upload denied")));
        scenario.statusOverrides = @{@"pendingKnown" : @NO};
        _provider->xpc()->refreshSyncStatus();
        QTRY_VERIFY(_provider->error().isEmpty());
        QVERIFY(!_provider->ready());
        scenario.statusOverrides = @{@"sampledAt" : @1};
        _provider->xpc()->refreshSyncStatus();
        QTRY_VERIFY(!_provider->error().isEmpty());
        QVERIFY(!_provider->ready());
    }

    void testRoutineInvalidationOnlyRechecksAffectedDomain()
    {
        const auto account = createAccount(true);
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        _provider = new FileProvider(nullptr, _suite);
        QTRY_VERIFY(_provider->ready());
        QCOMPARE(scenario.connections, 3);
        const auto personal = account->account()->uuid().toString(QUuid::WithoutBraces);
        const auto project = fileProviderDomainIdentifier(account->account()->uuid(), QStringLiteral("project-space"), false);
        TestProviderConnection *connection = scenario.connectionsByDomain[personal.toNSString()];
        TestProviderConnection *healthy = scenario.connectionsByDomain[project.toNSString()];
        void (^staleInvalidation)(void) = connection.invalidationHandler;
        staleInvalidation();
        QTRY_VERIFY(!_provider->ready());
        QVERIFY(_provider->error().isEmpty());
        QVERIFY(_provider->syncStatusText().contains(QStringLiteral("Checking on-demand sync status")));
        QVERIFY(healthy.invalidationHandler != nil);
        QTRY_VERIFY(_provider->ready());
        QCOMPARE(scenario.connections, 4);
        QVERIFY(scenario.connectionsByDomain[project.toNSString()] == healthy);
        staleInvalidation();
        QTest::qWait(20);
        QVERIFY(_provider->ready());
        QCOMPARE(scenario.connections, 4);

        Q_EMIT _provider->xpc()->domainStatusChanged(personal, QStringLiteral("Real configuration failure"));
        connection = scenario.connectionsByDomain[personal.toNSString()];
        connection.invalidationHandler();
        QTRY_VERIFY(connection.invalidationHandler == nil);
        QVERIFY(_provider->error().contains(QStringLiteral("Real configuration failure")));
        scenario.reportedIdentifier = @"unexpected-domain";
        QTRY_VERIFY(_provider->error().contains(QStringLiteral("unexpected domain identity")));
        QVERIFY(!_provider->ready());
    }

    void testLateSyncStatusCannotReviveSignedOutDomain()
    {
        const auto account = createAccount();
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        scenario.holdStatus = true;
        _provider = new FileProvider(nullptr, _suite);
        QTRY_VERIFY(scenario.statusReplies.count > 0);
        void (^reply)(void) = scenario.statusReplies.firstObject;
        const auto domainId = account->account()->uuid().toString(QUuid::WithoutBraces);
        _provider->xpc()->clearAccountConfiguration(domainId, {});
        reply();
        QTest::qWait(20);
        QVERIFY(!_provider->ready());
        QVERIFY(!_provider->syncStatusText().contains(QStringLiteral("Last synced:")));
    }

    void testSyncStatusSnapshotValidation()
    {
        const auto now = QDateTime::currentDateTimeUtc();
        QJsonObject status{{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("domainIdentifier"), QStringLiteral("domain")},
            {QStringLiteral("isAuthenticated"), true}, {QStringLiteral("isSynced"), true}, {QStringLiteral("pendingKnown"), true},
            {QStringLiteral("pendingTruncated"), false}};
        for (const auto &key : {"activeUploads", "activeDownloads", "activeMetadata", "pendingItems", "errorCount", "uploadedBytes", "uploadTotalBytes",
                 "downloadedBytes", "downloadTotalBytes", "lastCheckedAt", "lastSyncedAt"}) {
            status.insert(QLatin1String(key), 0);
        }
        status.insert(QStringLiteral("sampledAt"), now.toSecsSinceEpoch());
        const auto parsed = FileProviderSyncStatus::parse(QStringLiteral("domain"), status, now);
        QVERIFY(parsed);
        QVERIFY(!parsed->idle(now));
        QVERIFY(!parsed->fresh(now.addSecs(11)));
        QVERIFY(!FileProviderSyncStatus::parse(QStringLiteral("other"), status, now));
        status.insert(QStringLiteral("pendingItems"), -1);
        QVERIFY(!FileProviderSyncStatus::parse(QStringLiteral("domain"), status, now));
    }

    void testExistingDomainReceivesSearchCapabilities()
    {
        if (@available(macOS 26.0, *)) {
            const auto account = createAccount();
            QTRY_VERIFY(account->account()->spacesManager()->isReady());
            addExisting(account->account()->uuid().toString(QUuid::WithoutBraces));
            scenario.domains.firstObject.supportsStringSearchRequest = NO;
            FileProviderXPC xpc(nullptr, _suite);
            FileProviderDomainManager domains(nullptr, &xpc);
            domains.start();
            QTRY_COMPARE(scenario.additions, 1);
            QTRY_COMPARE(scenario.domains.count, 1UL);
            QVERIFY(scenario.domains.firstObject.supportsStringSearchRequest);
            QVERIFY(scenario.domains.firstObject.supportsSyncingTrash);
        } else {
            QSKIP("Finder string search requires macOS 26");
        }
    }

    void testSearchCapabilityMatchesSpaceDAVEndpoint_data()
    {
        QTest::addColumn<QString>("davRoot");
        QTest::addColumn<bool>("supportsSearch");
        QTest::newRow("space") << QStringLiteral("https://dav.example.org/dav/spaces/") << true;
        QTest::newRow("deployment-prefix") << QStringLiteral("https://dav.example.org/cloud/dav/spaces/") << true;
        QTest::newRow("generic-webdav") << QStringLiteral("https://dav.example.org/remote.php/webdav/") << false;
        QTest::newRow("subfolder") << QStringLiteral("https://dav.example.org/dav/spaces/drive/subfolder/") << false;
    }

    void testSearchCapabilityMatchesSpaceDAVEndpoint()
    {
        if (@available(macOS 26.0, *)) {
            QFETCH(QString, davRoot);
            QFETCH(bool, supportsSearch);
            const auto account = createAccount(false, davRoot);
            QTRY_VERIFY(account->account()->spacesManager()->isReady());
            FileProviderXPC xpc(nullptr, _suite);
            FileProviderDomainManager domains(nullptr, &xpc);
            domains.start();
            QTRY_COMPARE(scenario.domains.count, 1UL);
            QCOMPARE(bool(scenario.domains.firstObject.supportsStringSearchRequest), supportsSearch);
            QCOMPARE(bool(scenario.domains.firstObject.supportsSyncingTrash), supportsSearch);
        } else {
            QSKIP("Finder string search requires macOS 26");
        }
    }

    void testDeletionWaitsForCredentialAcknowledgment()
    {
        const auto account = createAccount();
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        const auto id = account->account()->uuid().toString(QUuid::WithoutBraces);
        addExisting(id);
        FileProviderXPC xpc(nullptr, _suite);
        FileProviderDomainManager domains(nullptr, &xpc);
        connect(&domains, &FileProviderDomainManager::domainSetupComplete, &xpc, &FileProviderXPC::connectToFileProviderDomains);
        domains.start();
        QTRY_COMPARE(scenario.configuredPaths.size(), 1);
        scenario.holdClear = true;
        AccountManager::instance()->deleteAccount(account);
        QCOMPARE(scenario.removals, 0);
        QCOMPARE(scenario.clearReplies.count, 1UL);
        NSUserDefaults *defaults = [[NSUserDefaults alloc] initWithSuiteName:_suite.toNSString()];
        QVERIFY([defaults boolForKey:[@"fp_removed_domain_" stringByAppendingString:id.toNSString()]]);
        void (^reply)(NSError *) = scenario.clearReplies.firstObject;
        reply(nil);
        QTRY_COMPARE(scenario.removals, 1);
    }

    void testCleanupFailurePreservesDisabledDomain()
    {
        const auto account = createAccount();
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        addExisting(account->account()->uuid().toString(QUuid::WithoutBraces));
        FileProviderXPC xpc(nullptr, _suite);
        FileProviderDomainManager domains(nullptr, &xpc);
        connect(&domains, &FileProviderDomainManager::domainSetupComplete, &xpc, &FileProviderXPC::connectToFileProviderDomains);
        domains.start();
        QTRY_COMPARE(scenario.configuredPaths.size(), 1);
        scenario.clearError = failure();
        AccountManager::instance()->deleteAccount(account);
        QTRY_COMPARE(scenario.disconnections, 1);
        QCOMPARE(scenario.removals, 0);
        QCOMPARE(scenario.domains.count, 1UL);
    }

    void testLateCleanupReplyCannotCompleteNextRequest()
    {
        const auto account = createAccount();
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        const auto id = account->account()->uuid().toString(QUuid::WithoutBraces);
        addExisting(id);
        FileProviderXPC xpc(nullptr, _suite);
        xpc.connectToFileProviderDomains();
        QTRY_COMPARE(scenario.configuredPaths.size(), 1);
        scenario.holdClear = true;
        int first = 0, second = 0;
        xpc.clearAccountConfiguration(id, [&](bool success) { first = success ? 1 : -1; });
        QTRY_COMPARE_WITH_TIMEOUT(first, -1, 4000);
        xpc.clearAccountConfiguration(id, [&](bool success) { second = success ? 1 : -1; });
        QCOMPARE(scenario.clearReplies.count, 2UL);
        void (^oldReply)(NSError *) = scenario.clearReplies[0];
        oldReply(nil);
        QTest::qWait(20);
        QCOMPARE(second, 0);
        void (^newReply)(NSError *) = scenario.clearReplies[1];
        newReply(nil);
        QTRY_COMPARE(second, 1);
    }

    void testDelayedConfigurationCannotRestoreSignedOutCredentials()
    {
        const auto account = createAccount();
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        const auto id = account->account()->uuid().toString(QUuid::WithoutBraces);
        addExisting(id);
        FileProviderXPC xpc(nullptr, _suite);
        FileProviderDomainManager domains(nullptr, &xpc);
        connect(&domains, &FileProviderDomainManager::domainSetupComplete, &xpc, &FileProviderXPC::connectToFileProviderDomains);
        domains.start();
        QTRY_COMPARE(scenario.configuredPaths.size(), 1);
        scenario.holdConfiguration = true;
        xpc.authenticateFileProviderDomain(id);
        QCOMPARE(scenario.configurationRequests.count, 1UL);
        account->signOutByUi();
        QTRY_COMPARE(scenario.disconnections, 1);
        QVERIFY(scenario.configuredPaths.isEmpty());
        void (^delayed)(void) = scenario.configurationRequests.firstObject;
        delayed();
        QTest::qWait(20);
        QVERIFY(scenario.configuredPaths.isEmpty());
    }

    void testDestroyedOwnerIgnoresDiscoveryReply()
    {
        scenario.holdDiscovery = true;
        {
            FileProviderXPC xpc(nullptr, _suite);
            xpc.connectToFileProviderDomains();
        }
        addExisting(QUuid::createUuid().toString(QUuid::WithoutBraces));
        scenario.discoveryReply(scenario.domains, nil);
        QTest::qWait(20);
        QCOMPARE(scenario.connections, 0);
        {
            FileProviderDomainManager domains;
            domains.start();
        }
        scenario.discoveryReply(scenario.domains, nil);
        QTest::qWait(20);
        QCOMPARE(scenario.disconnections, 0);
    }

    void testMismatchedServiceCannotReceiveCredentials()
    {
        const auto account = createAccount();
        QTRY_VERIFY(account->account()->spacesManager()->isReady());
        addExisting(account->account()->uuid().toString(QUuid::WithoutBraces));
        scenario.reportedIdentifier = QUuid::createUuid().toString(QUuid::WithoutBraces).toNSString();
        FileProviderXPC xpc(nullptr, _suite);
        QSignalSpy connected(&xpc, &FileProviderXPC::domainConnected);
        xpc.connectToFileProviderDomains();
        QTRY_COMPARE(scenario.connections, 1);
        QTest::qWait(20);
        QCOMPARE(connected.count(), 0);
        QVERIFY(scenario.configuredPaths.isEmpty());
    }

    void testFallbackDisconnectsDomainsWithoutBundledExtension()
    {
        QVERIFY(!FileProvider::fileProviderAvailable());
        addExisting(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(FileProvider::prepareForFolderSync());
        QCOMPARE(scenario.disconnections, 1);
        scenario.disconnectError = failure();
        const auto result = FileProvider::prepareForFolderSync();
        QVERIFY(!result);
        QVERIFY(result.error().contains(QStringLiteral("Injected native failure")));
    }

    void testMissingExtensionFallbackUsesDomainHistory()
    {
        if (@available(macOS 14.1, *)) {
            scenario.discoveryError =
                [NSError errorWithDomain:NSFileProviderErrorDomain
                                    code:NSFileProviderErrorProviderNotFound
                                userInfo:@{
                                    NSUnderlyingErrorKey :
                                        [NSError errorWithDomain:NSFileProviderErrorDomain code:NSFileProviderErrorApplicationExtensionNotFound userInfo:nil]
                                }];
        } else {
            scenario.discoveryError = [NSError errorWithDomain:NSFileProviderErrorDomain code:NSFileProviderErrorProviderNotFound userInfo:nil];
        }
        QVERIFY(FileProvider::prepareForFolderSync());
        rememberActiveFileProviderDomain(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(!FileProvider::prepareForFolderSync());
        rememberFileProviderDomains({});
        QVERIFY(FileProvider::prepareForFolderSync());
        ConfigFile::makeQSettings().clear();
        ConfigFile::makeQSettings().setValue(QStringLiteral("Accounts/size"), 1);
        QVERIFY(!FileProvider::prepareForFolderSync());
    }

    void testCleanupReportsNativeFailures()
    {
        FileProviderXPC xpc(nullptr, _suite);
        FileProviderDomainManager domains(nullptr, &xpc);
        scenario.discoveryError = failure();
        QVERIFY(!domains.removeAllDomains());
        scenario.discoveryError = nil;
        addExisting(fileProviderDomainIdentifier(QUuid::createUuid(), QStringLiteral("project"), false));
        scenario.removeError = failure();
        QVERIFY(!domains.removeAllDomains());
        QCOMPARE(scenario.removals, 1);
        scenario.removeError = nil;
        QVERIFY(domains.removeAllDomains());
        QCOMPARE(scenario.domains.count, 0UL);
    }
};

QTEST_MAIN(TestFileProviderLifecycle)
#include "testfileproviderlifecycle.moc"
