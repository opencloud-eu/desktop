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
    : _p(std::make_shared<Private>())
{
}

NsfpDomainManager::~NsfpDomainManager()
{
    // Drain the serial queue so all pending blocks finish before this object is
    // freed. In-flight NSFileProviderManager completion handlers capture their
    // own shared_ptr<Private> copy (see below), so they remain safe even after
    // the manager is destroyed.
    dispatch_sync(_p->dispatchQueue, ^{});

    QMutexLocker lock(&_p->cacheMutex);
    _p->domainCache.clear();
    _p->managerCache.clear();
}

void NsfpDomainManager::addDomain(const QString &identifier, const QString &displayName,
                                  NsfpDomainCompletionHandler completionHandler, bool forceRecreate)
{
    qCInfo(lcNsfpDomainManager) << "addDomain requested:" << identifier << "displayName:" << displayName;

    // Copy parameters by value — they are used in asynchronous blocks
    // that outlive this function call.
    QString identifierCopy = identifier;
    NSString *nsIdentifier = identifier.toNSString();
    NSString *nsDisplayName = displayName.toNSString();

    // Capture a shared_ptr copy so Private survives async completions that may
    // fire after this manager is destroyed.
    auto p = _p;

    // Capture completion handler by value for the block
    auto handler = std::move(completionHandler);

    dispatch_async(p->dispatchQueue, ^{
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
                    }
                    // NOTE: Do NOT remove other "opencloud*" domains here. Every space
                    // is its own domain sharing the "opencloud-<account>-<space>" prefix,
                    // so treating siblings as "stale" deletes all previously-registered
                    // spaces whenever a new space is added (only the last one survives).
                    // Orphaned-domain cleanup happens explicitly via removeDomain() when a
                    // space is unsynced, not as a side effect of adding a different space.
                }
            }
            dispatch_semaphore_signal(listSemaphore);
        }];
        dispatch_semaphore_wait(listSemaphore, DISPATCH_TIME_FOREVER);

        if (listError) {
            qCWarning(lcNsfpDomainManager) << "Failed to list existing domains:" << QString::fromNSString(listError.localizedDescription);
        }

        // One-time corruption recovery: if asked to force-recreate and the domain
        // exists, remove it (discarding the corrupted replica/FPFS) so the create
        // path below registers it fresh. Gated by a per-domain marker in the caller
        // so this happens at most once per update.
        if (existingDomain && forceRecreate) {
            qCWarning(lcNsfpDomainManager) << "forceRecreate: removing existing domain to clear corrupted state:" << identifierCopy;
            dispatch_semaphore_t recreateSem = dispatch_semaphore_create(0);
            [NSFileProviderManager removeDomain:existingDomain completionHandler:^(NSError *rmErr) {
                if (rmErr) {
                    qCWarning(lcNsfpDomainManager) << "forceRecreate remove failed:"
                                                   << QString::fromNSString(rmErr.localizedDescription);
                } else {
                    qCInfo(lcNsfpDomainManager) << "forceRecreate: domain removed, will re-create fresh:" << identifierCopy;
                }
                dispatch_semaphore_signal(recreateSem);
            }];
            dispatch_semaphore_wait(recreateSem, DISPATCH_TIME_FOREVER);
            existingDomain = nil; // fall through to the create path below
        }

        // If the domain already exists, reuse it rather than removing and re-adding.
        // Unconditional remove-then-readd on every launch causes Finder to briefly drop
        // the Locations entry; if addDomain then fails for any transient reason the
        // entry is gone permanently until the next launch.
        if (existingDomain) {
            qCInfo(lcNsfpDomainManager) << "Domain already registered, attempting reuse:" << identifierCopy
                                        << "userEnabled:" << existingDomain.userEnabled;

            NSFileProviderManager *existingManager = [NSFileProviderManager managerForDomain:existingDomain];
            if (existingManager) {
                // Domain is healthy — wake the extension by forcing a full re-enumeration.
                [existingManager reimportItemsBelowItemWithIdentifier:NSFileProviderRootContainerItemIdentifier
                                                   completionHandler:^(NSError *reimportErr) {
                    if (reimportErr) {
                        qCWarning(lcNsfpDomainManager) << "Reimport (domain reuse) failed:"
                                                       << QString::fromNSString(reimportErr.localizedDescription);
                    } else {
                        qCInfo(lcNsfpDomainManager) << "Reimport (domain reuse) succeeded";
                    }
                }];

                {
                    QMutexLocker lock(&p->cacheMutex);
                    p->domainCache[identifierCopy] = existingDomain;
                    p->managerCache[identifierCopy] = existingManager;
                }
                if (handler) {
                    handler(QString()); // success — domain reused
                }
                return;
            }

            // No manager available — domain is in a broken state. Remove and re-add
            // as recovery so fileproviderd picks up a fresh extension registration.
            qCWarning(lcNsfpDomainManager) << "Domain exists but manager unavailable, removing for re-add:" << identifierCopy;
            dispatch_semaphore_t removeSem = dispatch_semaphore_create(0);
            [NSFileProviderManager removeDomain:existingDomain completionHandler:^(NSError *removeErr) {
                if (removeErr) {
                    qCWarning(lcNsfpDomainManager) << "Failed to remove broken domain for re-add:"
                                                   << QString::fromNSString(removeErr.localizedDescription);
                } else {
                    qCInfo(lcNsfpDomainManager) << "Broken domain removed — will re-add fresh:" << identifierCopy;
                }
                dispatch_semaphore_signal(removeSem);
            }];
            dispatch_semaphore_wait(removeSem, DISPATCH_TIME_FOREVER);
            // Fall through to the addDomain path below.
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
                        QMutexLocker lock(&p->cacheMutex);
                        p->domainCache[identifierCopy] = domain;
                        p->managerCache[identifierCopy] = fallbackManager;
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
                QMutexLocker lock(&p->cacheMutex);
                p->domainCache[identifierCopy] = domain;
                p->managerCache[identifierCopy] = manager;
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
    auto p = _p;
    auto handler = std::move(completionHandler);

    dispatch_async(p->dispatchQueue, ^{
        NSFileProviderDomain *domain = nil;
        {
            QMutexLocker lock(&p->cacheMutex);
            domain = p->domainCache.value(identifierCopy, nil);
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
                QMutexLocker lock(&p->cacheMutex);
                p->domainCache.remove(identifierCopy);
                p->managerCache.remove(identifierCopy);
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
    auto p = _p;

    dispatch_async(p->dispatchQueue, ^{
        NSFileProviderManager *manager = nil;
        {
            QMutexLocker lock(&p->cacheMutex);
            manager = p->managerCache.value(identifierCopy, nil);
        }

        if (manager) {

            qCInfo(lcNsfpDomainManager) << "Manager invalidated for domain:" << identifierCopy;
        } else {
            qCDebug(lcNsfpDomainManager) << "invalidateManager: no manager cached for:" << identifierCopy;
        }

        {
            QMutexLocker lock(&p->cacheMutex);
            p->managerCache.remove(identifierCopy);
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
    auto p = _p;

    dispatch_async(p->dispatchQueue, ^{
        NSFileProviderManager *manager = nil;
        {
            QMutexLocker lock(&p->cacheMutex);
            manager = p->managerCache.value(identifierCopy, nil);
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
    auto p = _p;
    dispatch_async(p->dispatchQueue, ^{
        NSFileProviderManager *manager = nil;
        {
            QMutexLocker lock(&p->cacheMutex);
            manager = p->managerCache.value(identifierCopy, nil);
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
    auto p = _p;
    auto handler = std::move(completionHandler);

    dispatch_async(p->dispatchQueue, ^{
        NSFileProviderManager *manager = nil;
        {
            QMutexLocker lock(&p->cacheMutex);
            manager = p->managerCache.value(identifierCopy, nil);
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
    auto p = _p;

    dispatch_async(p->dispatchQueue, ^{
        NSFileProviderManager *manager = nil;
        {
            QMutexLocker lock(&p->cacheMutex);
            manager = p->managerCache.value(identifierCopy, nil);
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
