// NsfpDomainManager -- manages NSFileProviderDomain lifecycle for the macOS VFS plugin.
#pragma once

#include <QString>

#include <functional>
#include <memory>

#ifdef __OBJC__
#import <FileProvider/FileProvider.h>
#import <Foundation/Foundation.h>
#endif

namespace OCC {

/// Callback type for async domain operations.
/// On success, errorMessage is empty. On failure, it contains a description.
using NsfpDomainCompletionHandler = std::function<void(const QString &errorMessage)>;

/// Manages the lifecycle of NSFileProviderDomain objects.
///
/// This is a pure Objective-C++ class (not a QObject) since it interfaces
/// directly with NSFileProvider APIs. All NSFileProvider calls are dispatched
/// on a dedicated serial queue. Results are bridged back to Qt via the
/// completion handler, which callers are expected to invoke on their own
/// thread (e.g. via QMetaObject::invokeMethod with Qt::QueuedConnection).
///
/// Domain registration is idempotent: if a domain with the given identifier
/// already exists, the manager reconnects to it instead of creating a duplicate.
class NsfpDomainManager
{
public:
    NsfpDomainManager();
    ~NsfpDomainManager();

    // Non-copyable, non-movable
    NsfpDomainManager(const NsfpDomainManager &) = delete;
    NsfpDomainManager &operator=(const NsfpDomainManager &) = delete;

    /// Register or reconnect to an NSFileProviderDomain.
    /// The identifier must be stable across restarts (account UUID + space ID).
    /// The displayName is shown in Finder sidebar.
    /// Idempotent: if the domain already exists, reconnects without creating a duplicate.
    void addDomain(const QString &identifier, const QString &displayName, NsfpDomainCompletionHandler completionHandler);

    /// Fully remove an NSFileProviderDomain and delete its replica store.
    void removeDomain(const QString &identifier, NsfpDomainCompletionHandler completionHandler);

    /// Invalidate the manager for the given domain without removing it.
    /// Used during app shutdown so files persist on disk.
    void invalidateManager(const QString &identifier);

    /// Signal the File Provider framework to re-enumerate items in the given container.
    /// This causes Finder to refresh its view of that directory.
    /// @param identifier   The domain identifier.
    /// @param containerId  The container (folder) whose contents changed.
    ///                     Use NSFileProviderRootContainerItemIdentifier for root.
    void signalEnumerator(const QString &identifier, const QString &containerId);

    /// Evict (dehydrate) a single item, freeing its local storage.
    /// The item must have allowsEviction capability set in its FileProviderItem.
    /// @param identifier  The domain identifier.
    /// @param fileId      The NSFileProviderItemIdentifier of the item to evict.
    /// @param completionHandler  Called with empty string on success, error description on failure.
    void evictItem(const QString &identifier, const QString &fileId, NsfpDomainCompletionHandler completionHandler);

    /// Signal the system to perform storage-pressure eviction.
    /// The framework will decide which items to evict based on their
    /// allowsEviction capability and last-access timestamps.
    /// @param identifier  The domain identifier.
    void requestSystemEviction(const QString &identifier);

#ifdef __OBJC__
    /// Return a cached NSFileProviderManager for the given domain identifier,
    /// creating one via +[NSFileProviderManager managerForDomain:] if needed.
    /// Returns nil if the domain has not been registered.
    NSFileProviderManager *managerForIdentifier(const QString &identifier);
#endif

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace OCC
