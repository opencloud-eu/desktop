// NsfpDomainManager implementation -- manages NSFileProviderDomain lifecycle.

#include "nsfpdomainmanager.h"

#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>

#import <FileProvider/FileProvider.h>
#import <Foundation/Foundation.h>

Q_LOGGING_CATEGORY(lcNsfpDomainManager, "sync.vfs.nsfp.domain", QtInfoMsg)

namespace OCC {

struct NsfpDomainManager::Private
{
    /// Serial dispatch queue for all NSFileProvider calls.
    dispatch_queue_t dispatchQueue = dispatch_queue_create("eu.opencloud.vfs.nsfp.domain", DISPATCH_QUEUE_SERIAL);

    /// Thread-safe cache of domain identifier -> NSFileProviderDomain.
    QMutex cacheMutex;
    QMap<QString, NSFileProviderDomain *> domainCache;
    QMap<QString, NSFileProviderManager *> managerCache;
};

NsfpDomainManager::NsfpDomainManager()
    : _p(std::make_unique<Private>())
{
}

NsfpDomainManager::~NsfpDomainManager()
{
    // Drain the serial queue so all pending blocks finish before _p is freed.
    // Without this, in-flight dispatch_async blocks (e.g. from invalidateManager)
    // can access _p->cacheMutex after it has been destroyed → use-after-free.
    dispatch_sync(_p->dispatchQueue, ^{});

    QMutexLocker lock(&_p->cacheMutex);
    _p->domainCache.clear();
    _p->managerCache.clear();
}

void NsfpDomainManager::addDomain(const QString &identifier, const QString &displayName,
                                  NsfpDomainCompletionHandler completionHandler)
{
    qCInfo(lcNsfpDomainManager) << "addDomain requested:" << identifier << "displayName:" << displayName;

    // Copy parameters by value — they are used in asynchronous blocks
    // that outlive this function call.
    QString identifierCopy = identifier;
    NSString *nsIdentifier = identifier.toNSString();
    NSString *nsDisplayName = displayName.toNSString();

    // Capture completion handler by value for the block
    auto handler = std::move(completionHandler);

    dispatch_async(_p->dispatchQueue, ^{
        // First, check if our domain already exists and is enabled
        dispatch_semaphore_t listSemaphore = dispatch_semaphore_create(0);
        __block NSError *listError = nil;
        __block NSFileProviderDomain *existingDomain = nil;
        __block NSMutableArray<NSFileProviderDomain *> *staleDomainsToRemove = [NSMutableArray array];

        [NSFileProviderManager getDomainsWithCompletionHandler:^(NSArray<NSFileProviderDomain *> *domains, NSError *error) {
            if (error) {
                listError = error;
            } else {
                for (NSFileProviderDomain *domain in domains) {
                    if ([domain.identifier isEqualToString:nsIdentifier]) {
                        existingDomain = domain;
                    } else if ([domain.identifier hasPrefix:@"opencloud"]) {
                        // Remove stale opencloud domains with different identifiers
                        [staleDomainsToRemove addObject:domain];
                    }
                }
            }
            dispatch_semaphore_signal(listSemaphore);
        }];
        dispatch_semaphore_wait(listSemaphore, DISPATCH_TIME_FOREVER);

        if (listError) {
            qCWarning(lcNsfpDomainManager) << "Failed to list existing domains:" << QString::fromNSString(listError.localizedDescription);
        }

        // If the domain already exists, force-remove it so fileproviderd re-resolves
        // the extension UUID from pluginkit on the subsequent addDomain call.
        // Reusing the existing domain would leave fileproviderd bound to the old
        // extension UUID (e.g. after re-signing the appex), causing ETIMEDOUT on fetch.
        if (existingDomain) {
            qCInfo(lcNsfpDomainManager) << "Domain already exists, removing for clean re-add:" << identifierCopy
                                        << "userEnabled:" << existingDomain.userEnabled;
            dispatch_semaphore_t removeSem = dispatch_semaphore_create(0);
            [NSFileProviderManager removeDomain:existingDomain completionHandler:^(NSError *removeErr) {
                if (removeErr) {
                    qCWarning(lcNsfpDomainManager) << "Failed to remove existing domain for re-add:"
                                                   << QString::fromNSString(removeErr.localizedDescription);
                } else {
                    qCInfo(lcNsfpDomainManager) << "Existing domain removed — will re-add fresh:" << identifierCopy;
                }
                dispatch_semaphore_signal(removeSem);
            }];
            dispatch_semaphore_wait(removeSem, DISPATCH_TIME_FOREVER);
            // Fall through to the addDomain path below so fileproviderd picks up the
            // current pluginkit extension UUID.
        }

        // Remove stale domains before creating a new one
        for (NSFileProviderDomain *staleDomain in staleDomainsToRemove) {
            qCInfo(lcNsfpDomainManager) << "Removing stale domain:"
                                        << QString::fromNSString(staleDomain.identifier)
                                        << "userEnabled:" << staleDomain.userEnabled;

            dispatch_semaphore_t removeSem = dispatch_semaphore_create(0);
            [NSFileProviderManager removeDomain:staleDomain completionHandler:^(NSError *removeErr) {
                if (removeErr) {
                    qCWarning(lcNsfpDomainManager) << "Failed to remove stale domain:"
                                                   << QString::fromNSString(removeErr.localizedDescription);
                } else {
                    qCInfo(lcNsfpDomainManager) << "Stale domain removed successfully:"
                                                << QString::fromNSString(staleDomain.identifier);
                }
                dispatch_semaphore_signal(removeSem);
            }];
            dispatch_semaphore_wait(removeSem, DISPATCH_TIME_FOREVER);
        }

        // Create a new domain (only when no existing domain was found)
        NSFileProviderDomain *domain = [[NSFileProviderDomain alloc] initWithIdentifier:nsIdentifier
                                                                            displayName:nsDisplayName];

        [NSFileProviderManager addDomain:domain completionHandler:^(NSError *error) {
            if (error) {
                QString errorMsg = QString::fromNSString(error.localizedDescription);
                qCWarning(lcNsfpDomainManager) << "Failed to add domain:" << identifierCopy << "error:" << errorMsg;

                // The domain may already be registered in fileproviderd (e.g. addDomain failed
                // because getDomainsWithCompletionHandler also failed with -2001 during init,
                // so we fell through to the create path even though the domain exists).
                // Try to obtain a manager anyway — if the domain is registered, this succeeds
                // and we can still call reimportItemsBelowItemWithIdentifier to wake the extension.
                NSFileProviderManager *fallbackManager = [NSFileProviderManager managerForDomain:domain];
                if (fallbackManager) {
                    qCInfo(lcNsfpDomainManager) << "addDomain failed but domain is registered; attempting fallback reimport for:" << identifierCopy;
                    [fallbackManager reimportItemsBelowItemWithIdentifier:NSFileProviderRootContainerItemIdentifier
                                                       completionHandler:^(NSError *reimportErr) {
                        if (reimportErr) {
                            qCWarning(lcNsfpDomainManager) << "Fallback reimport failed:"
                                                           << QString::fromNSString(reimportErr.localizedDescription);
                        } else {
                            qCInfo(lcNsfpDomainManager) << "Fallback reimport succeeded — extension should wake";
                        }
                    }];
                    {
                        QMutexLocker lock(&_p->cacheMutex);
                        _p->domainCache[identifierCopy] = domain;
                        _p->managerCache[identifierCopy] = fallbackManager;
                    }
                    if (handler) {
                        handler(QString()); // treat as success — we have a live manager
                    }
                    return;
                }

                if (handler) {
                    handler(errorMsg);
                }
                return;
            }

            qCInfo(lcNsfpDomainManager) << "Domain added successfully:" << identifierCopy;

            NSFileProviderManager *manager = [NSFileProviderManager managerForDomain:domain];

            // Force re-enumeration to clear any backoff state from previous sessions.
            [manager reimportItemsBelowItemWithIdentifier:NSFileProviderRootContainerItemIdentifier
                                       completionHandler:^(NSError *reimportErr) {
                if (reimportErr) {
                    qCWarning(lcNsfpDomainManager) << "reimportItems (new domain) failed:"
                                                   << QString::fromNSString(reimportErr.localizedDescription);
                } else {
                    qCInfo(lcNsfpDomainManager) << "reimportItems (new domain) succeeded — fileproviderd will re-enumerate";
                }
            }];

            // Check userEnabled status of the newly added domain
            qCInfo(lcNsfpDomainManager) << "New domain userEnabled:" << domain.userEnabled;

            if (!domain.userEnabled) {
                // Try reconnect on the new domain too
                qCInfo(lcNsfpDomainManager) << "New domain is user-disabled, attempting reconnect...";
                [manager reconnectWithCompletionHandler:^(NSError *reconnectError) {
                    if (reconnectError) {
                        qCWarning(lcNsfpDomainManager) << "Reconnect on new domain failed:"
                                                       << QString::fromNSString(reconnectError.localizedDescription);
                    } else {
                        qCInfo(lcNsfpDomainManager) << "Reconnect on new domain succeeded!";
                    }
                }];
            }

            {
                QMutexLocker lock(&_p->cacheMutex);
                _p->domainCache[identifierCopy] = domain;
                _p->managerCache[identifierCopy] = manager;
            }

            if (handler) {
                handler(QString());
            }
        }];
    });
}

void NsfpDomainManager::removeDomain(const QString &identifier,
                                     NsfpDomainCompletionHandler completionHandler)
{
    qCInfo(lcNsfpDomainManager) << "removeDomain requested:" << identifier;

    QString identifierCopy = identifier;
    auto handler = std::move(completionHandler);

    dispatch_async(_p->dispatchQueue, ^{
        NSFileProviderDomain *domain = nil;
        {
            QMutexLocker lock(&_p->cacheMutex);
            domain = _p->domainCache.value(identifierCopy, nil);
        }

        if (!domain) {
            qCWarning(lcNsfpDomainManager) << "removeDomain: domain not found in cache:" << identifierCopy;
            if (handler) {
                handler(QStringLiteral("Domain not found: %1").arg(identifierCopy));
            }
            return;
        }

        [NSFileProviderManager removeDomain:domain completionHandler:^(NSError *error) {
            if (error) {
                QString errorMsg = QString::fromNSString(error.localizedDescription);
                qCWarning(lcNsfpDomainManager) << "Failed to remove domain:" << identifierCopy << "error:" << errorMsg;

                if (handler) {
                    handler(errorMsg);
                }
                return;
            }

            qCInfo(lcNsfpDomainManager) << "Domain removed successfully:" << identifierCopy;

            {
                QMutexLocker lock(&_p->cacheMutex);
                _p->domainCache.remove(identifierCopy);
                _p->managerCache.remove(identifierCopy);
            }

            if (handler) {
                handler(QString());
            }
        }];
    });
}

void NsfpDomainManager::invalidateManager(const QString &identifier)
{
    qCInfo(lcNsfpDomainManager) << "invalidateManager requested:" << identifier;

    QString identifierCopy = identifier;

    dispatch_async(_p->dispatchQueue, ^{
        NSFileProviderManager *manager = nil;
        {
            QMutexLocker lock(&_p->cacheMutex);
            manager = _p->managerCache.value(identifierCopy, nil);
        }

        if (manager) {

            qCInfo(lcNsfpDomainManager) << "Manager invalidated for domain:" << identifierCopy;
        } else {
            qCDebug(lcNsfpDomainManager) << "invalidateManager: no manager cached for:" << identifierCopy;
        }

        {
            QMutexLocker lock(&_p->cacheMutex);
            _p->managerCache.remove(identifierCopy);
            // Keep the domain in cache so it can be reconnected later
        }
    });
}

NSFileProviderManager *NsfpDomainManager::managerForIdentifier(const QString &identifier)
{
    QMutexLocker lock(&_p->cacheMutex);

    // Return cached manager if available
    auto managerIt = _p->managerCache.find(identifier);
    if (managerIt != _p->managerCache.end()) {
        return managerIt.value();
    }

    // Try to create from cached domain
    auto domainIt = _p->domainCache.find(identifier);
    if (domainIt != _p->domainCache.end()) {
        NSFileProviderManager *manager = [NSFileProviderManager managerForDomain:domainIt.value()];
        _p->managerCache[identifier] = manager;
        return manager;
    }

    qCDebug(lcNsfpDomainManager) << "managerForIdentifier: no domain registered for:" << identifier;
    return nil;
}

void NsfpDomainManager::signalEnumerator(const QString &identifier, const QString &containerId)
{
    qCInfo(lcNsfpDomainManager) << "signalEnumerator requested for domain:" << identifier
                                << "container:" << containerId;

    // Copy parameters by value — they must survive past this function's return
    // since they are used in asynchronous blocks.
    QString identifierCopy = identifier;
    QString containerIdCopy = containerId;
    NSString *nsContainerId = containerId.toNSString();

    dispatch_async(_p->dispatchQueue, ^{
        NSFileProviderManager *manager = nil;
        {
            QMutexLocker lock(&_p->cacheMutex);
            manager = _p->managerCache.value(identifierCopy, nil);
        }

        if (!manager) {
            qCWarning(lcNsfpDomainManager) << "signalEnumerator: no manager for domain:" << identifierCopy;
            return;
        }

        NSFileProviderItemIdentifier itemId = nsContainerId;
        if (containerIdCopy.isEmpty()) {
            itemId = NSFileProviderRootContainerItemIdentifier;
        }

        [manager signalEnumeratorForContainerItemIdentifier:itemId
                                          completionHandler:^(NSError *error) {
            if (error) {
                qCWarning(lcNsfpDomainManager) << "signalEnumerator failed:"
                                               << QString::fromNSString(error.localizedDescription);
            } else {
                qCDebug(lcNsfpDomainManager) << "signalEnumerator succeeded for container:" << containerIdCopy;
            }
        }];
    });
}

void NsfpDomainManager::signalWorkingSet(const QString &identifier)
{
    qCInfo(lcNsfpDomainManager) << "signalWorkingSet requested for domain:" << identifier;

    QString identifierCopy = identifier;
    dispatch_async(_p->dispatchQueue, ^{
        NSFileProviderManager *manager = nil;
        {
            QMutexLocker lock(&_p->cacheMutex);
            manager = _p->managerCache.value(identifierCopy, nil);
        }

        if (!manager) {
            qCWarning(lcNsfpDomainManager) << "signalWorkingSet: no manager for domain:" << identifierCopy;
            return;
        }

        [manager signalEnumeratorForContainerItemIdentifier:NSFileProviderWorkingSetContainerItemIdentifier
                                          completionHandler:^(NSError *error) {
            if (error) {
                qCWarning(lcNsfpDomainManager) << "signalWorkingSet failed:"
                                               << QString::fromNSString(error.localizedDescription);
            } else {
                qCDebug(lcNsfpDomainManager) << "signalWorkingSet succeeded for domain:" << identifierCopy;
            }
        }];
    });
}

void NsfpDomainManager::evictItem(const QString &identifier, const QString &fileId,
                                  NsfpDomainCompletionHandler completionHandler)
{
    qCInfo(lcNsfpDomainManager) << "evictItem requested for domain:" << identifier
                                << "fileId:" << fileId;

    QString identifierCopy = identifier;
    QString fileIdCopy = fileId;
    NSString *nsFileId = fileId.toNSString();
    auto handler = std::move(completionHandler);

    dispatch_async(_p->dispatchQueue, ^{
        NSFileProviderManager *manager = nil;
        {
            QMutexLocker lock(&_p->cacheMutex);
            manager = _p->managerCache.value(identifierCopy, nil);
        }

        if (!manager) {
            qCWarning(lcNsfpDomainManager) << "evictItem: no manager for domain:" << identifierCopy;
            if (handler) {
                handler(QStringLiteral("No manager for domain: %1").arg(identifierCopy));
            }
            return;
        }

        [manager evictItemWithIdentifier:nsFileId
                       completionHandler:^(NSError *error) {
            if (error) {
                const auto errorMsg = QString::fromNSString(error.localizedDescription);
                qCWarning(lcNsfpDomainManager) << "evictItem failed for fileId:" << fileIdCopy
                                               << "error:" << errorMsg;
                if (handler) {
                    handler(errorMsg);
                }
            } else {
                qCInfo(lcNsfpDomainManager) << "evictItem succeeded for fileId:" << fileIdCopy;
                if (handler) {
                    handler(QString());
                }
            }
        }];
    });
}

void NsfpDomainManager::requestSystemEviction(const QString &identifier)
{
    qCInfo(lcNsfpDomainManager) << "requestSystemEviction for domain:" << identifier;

    QString identifierCopy = identifier;

    dispatch_async(_p->dispatchQueue, ^{
        NSFileProviderManager *manager = nil;
        {
            QMutexLocker lock(&_p->cacheMutex);
            manager = _p->managerCache.value(identifierCopy, nil);
        }

        if (!manager) {
            qCWarning(lcNsfpDomainManager) << "requestSystemEviction: no manager for domain:" << identifierCopy;
            return;
        }

        // Signal the working set enumerator to let the system decide what to evict
        // based on allowsEviction capability and last-access timestamps.
        [manager signalEnumeratorForContainerItemIdentifier:NSFileProviderWorkingSetContainerItemIdentifier
                                          completionHandler:^(NSError *error) {
            if (error) {
                qCWarning(lcNsfpDomainManager) << "requestSystemEviction signal failed:"
                                               << QString::fromNSString(error.localizedDescription);
            } else {
                qCInfo(lcNsfpDomainManager) << "requestSystemEviction signal sent successfully";
            }
        }];
    });
}

} // namespace OCC
