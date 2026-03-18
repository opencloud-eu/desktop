// NsfpXpcHandler implementation -- XPC listener in the main app that handles
// hydration, enumeration, and pin-state requests from the File Provider extension.

#include "nsfpxpchandler.h"

#include "common/syncjournaldb.h"
#include "common/syncjournalfilerecord.h"
#include "libsync/vfs/hydrationjob.h"
#include "libsync/vfs/vfs.h"

#include <QFile>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QPointer>

#import <Foundation/Foundation.h>

// Import the shared XPC protocol definition from the extension sources.
// The protocol header is self-contained (no ObjC++ / Qt dependencies).
#import "FileProviderXPCService.h"

Q_LOGGING_CATEGORY(lcNsfpXpc, "sync.vfs.nsfp.xpc", QtInfoMsg)

static const int ENUMERATE_PAGE_SIZE = 500;

using namespace OCC;

// ---------------------------------------------------------------------------
// Objective-C delegate that conforms to OpenCloudXPCServiceProtocol.
// All heavy lifting is forwarded to the Qt event loop via QPointer + invokeMethod.
// ---------------------------------------------------------------------------

@interface NsfpXpcDelegate : NSObject <OpenCloudXPCServiceProtocol, NSXPCListenerDelegate>
- (instancetype)initWithVfs:(QPointer<Vfs>)vfs handler:(QPointer<NsfpXpcHandler>)handler;
@end

@implementation NsfpXpcDelegate {
    QPointer<Vfs> _vfs;
    QPointer<NsfpXpcHandler> _handler;

    /// Guards against duplicate hydration requests for the same fileId.
    /// Key: fileId (NSString*). Value: array of pending completion handlers.
    NSMutableDictionary<NSString *, NSMutableArray *> *_inflightHydrations;
}

- (instancetype)initWithVfs:(QPointer<Vfs>)vfs handler:(QPointer<NsfpXpcHandler>)handler {
    self = [super init];
    if (self) {
        _vfs = vfs;
        _handler = handler;
        _inflightHydrations = [NSMutableDictionary dictionary];
    }
    return self;
}

#pragma mark - NSXPCListenerDelegate

- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)newConnection {
    Q_UNUSED(listener)

    qCInfo(lcNsfpXpc) << "Accepting new XPC connection from extension";

    newConnection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(OpenCloudXPCServiceProtocol)];
    newConnection.exportedObject = self;

    __weak NSXPCConnection *weakConn = newConnection;
    newConnection.invalidationHandler = ^{
        qCInfo(lcNsfpXpc) << "XPC connection invalidated";
        Q_UNUSED(weakConn)
    };
    newConnection.interruptionHandler = ^{
        qCInfo(lcNsfpXpc) << "XPC connection interrupted";
    };

    [newConnection resume];
    return YES;
}

#pragma mark - OpenCloudXPCServiceProtocol

- (void)requestHydration:(NSString *)fileId
               targetURL:(NSURL *)url
       completionHandler:(void (^)(NSError * _Nullable))completionHandler {

    NSString *fileIdCopy = [fileId copy];
    NSURL *urlCopy = [url copy];
    auto handler = [completionHandler copy];

    qCInfo(lcNsfpXpc) << "requestHydration fileId:" << QString::fromNSString(fileIdCopy)
                      << "target:" << QString::fromNSString(urlCopy.path);

    // Coalesce: if a hydration for the same fileId is already in flight, queue the callback.
    @synchronized (_inflightHydrations) {
        NSMutableArray *pending = _inflightHydrations[fileIdCopy];
        if (pending) {
            qCInfo(lcNsfpXpc) << "Coalescing hydration request for fileId:" << QString::fromNSString(fileIdCopy);
            [pending addObject:handler];
            return;
        }
        _inflightHydrations[fileIdCopy] = [NSMutableArray arrayWithObject:handler];
    }

    QPointer<Vfs> vfs = _vfs;
    __weak __typeof__(self) weakSelf = self;

    QMetaObject::invokeMethod(vfs, [vfs, fileIdCopy, urlCopy, weakSelf]() {
        if (!vfs) {
            qCWarning(lcNsfpXpc) << "Vfs gone during hydration request";
            [weakSelf completeHydration:fileIdCopy
                              withError:[NSError errorWithDomain:NSFileProviderErrorDomain
                                                            code:NSFileProviderErrorServerUnreachable
                                                        userInfo:@{NSLocalizedDescriptionKey: @"Sync engine unavailable"}]];
            return;
        }

        const auto qFileId = QString::fromNSString(fileIdCopy).toUtf8();
        const auto targetPath = QString::fromNSString(urlCopy.path);

        // Open a QFile as the output device for HydrationJob.
        auto device = std::make_unique<QFile>(targetPath);

        auto *job = new HydrationJob(vfs, qFileId, std::move(device), vfs);
        job->setTargetFile(targetPath);

        QObject::connect(job, &HydrationJob::finished, vfs, [weakSelf, fileIdCopy, job]() {
            qCInfo(lcNsfpXpc) << "Hydration finished successfully for fileId:" << QString::fromNSString(fileIdCopy);
            [weakSelf completeHydration:fileIdCopy withError:nil];
            job->deleteLater();
        });
        QObject::connect(job, &HydrationJob::error, vfs, [weakSelf, fileIdCopy, job](const QString &errorMsg) {
            qCWarning(lcNsfpXpc) << "Hydration error for fileId:" << QString::fromNSString(fileIdCopy) << errorMsg;
            NSError *nsError = [NSError errorWithDomain:NSFileProviderErrorDomain
                                                   code:NSFileProviderErrorServerUnreachable
                                               userInfo:@{NSLocalizedDescriptionKey: errorMsg.toNSString()}];
            [weakSelf completeHydration:fileIdCopy withError:nsError];
            job->deleteLater();
        });

        job->start();
    }, Qt::QueuedConnection);
}

/// Internal helper: resolve all queued completion handlers for a hydration request.
- (void)completeHydration:(NSString *)fileId withError:(NSError * _Nullable)error {
    NSArray *handlers = nil;
    @synchronized (_inflightHydrations) {
        handlers = [_inflightHydrations[fileId] copy];
        [_inflightHydrations removeObjectForKey:fileId];
    }
    for (void (^h)(NSError *) in handlers) {
        h(error);
    }
}

- (void)scheduleUpload:(NSURL *)localURL
      parentIdentifier:(NSString *)parentId
     completionHandler:(void (^)(NSString * _Nullable, NSError * _Nullable))completionHandler {

    qCInfo(lcNsfpXpc) << "scheduleUpload — stub, not yet implemented";

    NSError *error = [NSError errorWithDomain:NSFileProviderErrorDomain
                                         code:NSFileProviderErrorServerUnreachable
                                     userInfo:@{NSLocalizedDescriptionKey: @"Upload scheduling not yet implemented"}];
    completionHandler(nil, error);
}

- (void)requestPinState:(NSString *)fileId
      completionHandler:(void (^)(NSInteger, NSError * _Nullable))completionHandler {

    NSString *fileIdCopy = [fileId copy];
    QPointer<Vfs> vfs = _vfs;

    QMetaObject::invokeMethod(vfs, [vfs, fileIdCopy, completionHandler]() {
        if (!vfs) {
            completionHandler(0, [NSError errorWithDomain:NSFileProviderErrorDomain
                                                     code:NSFileProviderErrorServerUnreachable
                                                 userInfo:@{NSLocalizedDescriptionKey: @"Sync engine unavailable"}]);
            return;
        }

        // Look up the record by fileId to get its relative path, then query pinState.
        auto *journal = vfs->params().journal;
        if (!journal) {
            completionHandler(0, [NSError errorWithDomain:NSFileProviderErrorDomain
                                                     code:NSFileProviderErrorServerUnreachable
                                                 userInfo:@{NSLocalizedDescriptionKey: @"No journal available"}]);
            return;
        }

        QString relPath;
        const auto qFileId = QString::fromNSString(fileIdCopy).toUtf8();
        journal->getFileRecordsByFileId(qFileId, [&relPath](const SyncJournalFileRecord &record) {
            if (record.isValid()) {
                relPath = record.path();
            }
        });

        if (relPath.isEmpty()) {
            completionHandler(0, [NSError errorWithDomain:NSFileProviderErrorDomain
                                                     code:NSFileProviderErrorNoSuchItem
                                                 userInfo:@{NSLocalizedDescriptionKey: @"Item not found in journal"}]);
            return;
        }

        auto state = vfs->pinState(relPath);
        if (state) {
            completionHandler(static_cast<NSInteger>(*state), nil);
        } else {
            // No explicit pin state -- return Inherited (0)
            completionHandler(static_cast<NSInteger>(PinState::Inherited), nil);
        }
    }, Qt::QueuedConnection);
}

- (void)setPinState:(NSInteger)pinState
          forFileId:(NSString *)fileId
  completionHandler:(void (^)(NSError * _Nullable))completionHandler {

    NSString *fileIdCopy = [fileId copy];
    QPointer<Vfs> vfs = _vfs;

    QMetaObject::invokeMethod(vfs, [vfs, fileIdCopy, pinState, completionHandler]() {
        if (!vfs) {
            completionHandler([NSError errorWithDomain:NSFileProviderErrorDomain
                                                  code:NSFileProviderErrorServerUnreachable
                                              userInfo:@{NSLocalizedDescriptionKey: @"Sync engine unavailable"}]);
            return;
        }

        auto *journal = vfs->params().journal;
        if (!journal) {
            completionHandler([NSError errorWithDomain:NSFileProviderErrorDomain
                                                  code:NSFileProviderErrorServerUnreachable
                                              userInfo:@{NSLocalizedDescriptionKey: @"No journal available"}]);
            return;
        }

        QString relPath;
        const auto qFileId = QString::fromNSString(fileIdCopy).toUtf8();
        journal->getFileRecordsByFileId(qFileId, [&relPath](const SyncJournalFileRecord &record) {
            if (record.isValid()) {
                relPath = record.path();
            }
        });

        if (relPath.isEmpty()) {
            completionHandler([NSError errorWithDomain:NSFileProviderErrorDomain
                                                  code:NSFileProviderErrorNoSuchItem
                                              userInfo:@{NSLocalizedDescriptionKey: @"Item not found in journal"}]);
            return;
        }

        const auto state = static_cast<PinState>(pinState);
        const bool ok = vfs->setPinState(relPath, state);
        if (ok) {
            completionHandler(nil);
        } else {
            completionHandler([NSError errorWithDomain:NSFileProviderErrorDomain
                                                  code:NSFileProviderErrorServerUnreachable
                                              userInfo:@{NSLocalizedDescriptionKey: @"Failed to set pin state"}]);
        }
    }, Qt::QueuedConnection);
}

- (void)ping:(void (^)(BOOL))handler {
    qCDebug(lcNsfpXpc) << "ping received";
    handler(YES);
}

- (void)enumerateItems:(NSString *)containerId
                cursor:(NSString *)cursor
     completionHandler:(void (^)(NSArray<NSDictionary *> * _Nullable,
                                  NSString * _Nullable,
                                  NSError * _Nullable))completionHandler {

    NSString *containerIdCopy = [containerId copy];
    NSString *cursorCopy = [cursor copy];
    QPointer<Vfs> vfs = _vfs;

    QMetaObject::invokeMethod(vfs, [vfs, containerIdCopy, cursorCopy, completionHandler]() {
        if (!vfs) {
            completionHandler(nil, nil,
                [NSError errorWithDomain:NSFileProviderErrorDomain
                                    code:NSFileProviderErrorServerUnreachable
                                userInfo:@{NSLocalizedDescriptionKey: @"Sync engine unavailable"}]);
            return;
        }

        auto *journal = vfs->params().journal;
        if (!journal) {
            completionHandler(nil, nil,
                [NSError errorWithDomain:NSFileProviderErrorDomain
                                    code:NSFileProviderErrorServerUnreachable
                                userInfo:@{NSLocalizedDescriptionKey: @"No journal available"}]);
            return;
        }

        const auto qContainerId = QString::fromNSString(containerIdCopy);
        const int offset = QString::fromNSString(cursorCopy).toInt(); // empty -> 0

        // Determine the parent path. Root container => enumerate top-level items.
        QString parentPath;
        if (!qContainerId.isEmpty()) {
            // Look up the path for this fileId
            const auto qFileIdBytes = qContainerId.toUtf8();
            journal->getFileRecordsByFileId(qFileIdBytes, [&parentPath](const SyncJournalFileRecord &record) {
                if (record.isValid()) {
                    parentPath = record.path();
                }
            });
            if (parentPath.isEmpty()) {
                completionHandler(nil, nil,
                    [NSError errorWithDomain:NSFileProviderErrorDomain
                                        code:NSFileProviderErrorNoSuchItem
                                    userInfo:@{NSLocalizedDescriptionKey: @"Container not found in journal"}]);
                return;
            }
        }
        // parentPath empty means root

        // Collect children from the journal
        QVector<SyncJournalFileRecord> children;
        journal->listFilesInPath(parentPath, [&children](const SyncJournalFileRecord &record) {
            children.append(record);
        });

        // Apply pagination
        const int total = children.size();
        const int start = qMin(offset, total);
        const int end = qMin(start + ENUMERATE_PAGE_SIZE, total);

        NSMutableArray<NSDictionary *> *items = [NSMutableArray arrayWithCapacity:end - start];
        for (int i = start; i < end; ++i) {
            const auto &rec = children[i];
            NSDictionary *dict = @{
                @"fileId" : QString::fromUtf8(rec.fileId()).toNSString(),
                @"path" : rec.path().toNSString(),
                @"name" : rec.name().toNSString(),
                @"isDirectory" : @(rec.isDirectory()),
                @"size" : @(rec.size()),
                @"modtime" : @(rec.modtime()),
                @"etag" : rec.etag().toNSString(),
                @"isVirtualFile" : @(rec.isVirtualFile()),
            };
            [items addObject:dict];
        }

        NSString *nextCursor = nil;
        if (end < total) {
            nextCursor = [NSString stringWithFormat:@"%d", end];
        }

        completionHandler(items, nextCursor, nil);
    }, Qt::QueuedConnection);
}

- (void)itemForIdentifier:(NSString *)identifier
        completionHandler:(void (^)(NSDictionary * _Nullable,
                                     NSError * _Nullable))completionHandler {

    NSString *identifierCopy = [identifier copy];
    QPointer<Vfs> vfs = _vfs;

    QMetaObject::invokeMethod(vfs, [vfs, identifierCopy, completionHandler]() {
        if (!vfs) {
            completionHandler(nil,
                [NSError errorWithDomain:NSFileProviderErrorDomain
                                    code:NSFileProviderErrorServerUnreachable
                                userInfo:@{NSLocalizedDescriptionKey: @"Sync engine unavailable"}]);
            return;
        }

        auto *journal = vfs->params().journal;
        if (!journal) {
            completionHandler(nil,
                [NSError errorWithDomain:NSFileProviderErrorDomain
                                    code:NSFileProviderErrorServerUnreachable
                                userInfo:@{NSLocalizedDescriptionKey: @"No journal available"}]);
            return;
        }

        const auto qFileId = QString::fromNSString(identifierCopy).toUtf8();
        SyncJournalFileRecord found;
        journal->getFileRecordsByFileId(qFileId, [&found](const SyncJournalFileRecord &record) {
            if (record.isValid() && !found.isValid()) {
                found = record;
            }
        });

        if (!found.isValid()) {
            completionHandler(nil,
                [NSError errorWithDomain:NSFileProviderErrorDomain
                                    code:NSFileProviderErrorNoSuchItem
                                userInfo:@{NSLocalizedDescriptionKey: @"Item not found in journal"}]);
            return;
        }

        NSDictionary *dict = @{
            @"fileId" : QString::fromUtf8(found.fileId()).toNSString(),
            @"path" : found.path().toNSString(),
            @"name" : found.name().toNSString(),
            @"isDirectory" : @(found.isDirectory()),
            @"size" : @(found.size()),
            @"modtime" : @(found.modtime()),
            @"etag" : found.etag().toNSString(),
            @"isVirtualFile" : @(found.isVirtualFile()),
        };

        completionHandler(dict, nil);
    }, Qt::QueuedConnection);
}

@end

// ---------------------------------------------------------------------------
// C++ Private implementation (PIMPL)
// ---------------------------------------------------------------------------

namespace OCC {

struct NsfpXpcHandler::Private
{
    NSXPCListener *listener = nil;
    NsfpXpcDelegate *delegate = nil;
};

NsfpXpcHandler::NsfpXpcHandler(Vfs *vfs, QObject *parent)
    : QObject(parent)
    , _p(std::make_unique<Private>())
    , _vfs(vfs)
{
}

NsfpXpcHandler::~NsfpXpcHandler()
{
    stopListener();
}

void NsfpXpcHandler::startListener()
{
    if (_p->listener) {
        qCDebug(lcNsfpXpc) << "Listener already started";
        return;
    }

    qCInfo(lcNsfpXpc) << "Starting anonymous NSXPCListener (endpoint shared via App Group container)";

    _p->delegate = [[NsfpXpcDelegate alloc] initWithVfs:QPointer<Vfs>(_vfs)
                                                handler:QPointer<NsfpXpcHandler>(this)];

    // Use an anonymous listener instead of initWithMachServiceName: because
    // unsandboxed apps cannot register Mach services with launchd.
    _p->listener = [NSXPCListener anonymousListener];
    _p->listener.delegate = _p->delegate;
    [_p->listener resume];

    // Write the listener endpoint to the App Group container so the extension
    // can establish an XPC connection. NSXPCListenerEndpoint conforms to
    // NSSecureCoding; the serialized form carries a Mach send right that the
    // kernel transfers to whichever process unarchives the data.  The endpoint
    // becomes invalid when this process exits, but that is expected — the
    // extension will retry on the next launch.
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (containerURL) {
        NSXPCListenerEndpoint *endpoint = _p->listener.endpoint;
        if (endpoint) {
            NSError *archiveError = nil;
            NSData *data = [NSKeyedArchiver archivedDataWithRootObject:endpoint
                                                requiringSecureCoding:YES
                                                                error:&archiveError];
            if (data && !archiveError) {
                NSURL *endpointURL = [containerURL URLByAppendingPathComponent:kOpenCloudXPCEndpointFilename];
                [data writeToURL:endpointURL atomically:YES];
                qCInfo(lcNsfpXpc) << "XPC endpoint written to App Group container";
            } else {
                qCWarning(lcNsfpXpc) << "Failed to archive XPC endpoint:"
                                     << QString::fromNSString(archiveError.localizedDescription);
            }
        }
    }

    qCInfo(lcNsfpXpc) << "NSXPCListener started";
}

void NsfpXpcHandler::stopListener()
{
    if (!_p->listener) {
        return;
    }

    qCInfo(lcNsfpXpc) << "Stopping NSXPCListener";
    [_p->listener invalidate];
    _p->listener = nil;
    _p->delegate = nil;

    // Remove the endpoint file so the extension does not try to connect
    // to a dead listener.
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (containerURL) {
        NSURL *endpointURL = [containerURL URLByAppendingPathComponent:kOpenCloudXPCEndpointFilename];
        [[NSFileManager defaultManager] removeItemAtURL:endpointURL error:nil];
    }
}

} // namespace OCC
