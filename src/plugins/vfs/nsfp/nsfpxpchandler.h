// NsfpXpcHandler -- XPC listener in the main app that handles hydration,
// enumeration, and pin-state requests from the File Provider extension.
#pragma once

#include <QMap>
#include <QObject>

#include <functional>
#include <memory>

#ifdef __OBJC__
#import <Foundation/Foundation.h>
#endif

namespace OCC {

class Vfs;

/// Handles incoming XPC calls from the NSFileProvider extension process.
///
/// The extension connects to the main app via a Mach-service-based
/// NSXPCListener. This class vends an Objective-C object that conforms to
/// OpenCloudXPCServiceProtocol and forwards requests into the Qt event loop.
///
/// Thread safety: all SyncJournalDb and HydrationJob work is dispatched to
/// the Qt main thread via QMetaObject::invokeMethod. The XPC listener and
/// its delegate live on a GCD serial queue.
class NsfpXpcHandler : public QObject
{
    Q_OBJECT

public:
    explicit NsfpXpcHandler(Vfs *vfs, QObject *parent = nullptr);
    ~NsfpXpcHandler() override;

    // Non-copyable, non-movable
    NsfpXpcHandler(const NsfpXpcHandler &) = delete;
    NsfpXpcHandler &operator=(const NsfpXpcHandler &) = delete;

    /// Start the NSXPCListener. Must be called after VfsSetupParams are available.
    void startListener();

    /// Stop the listener and abort any in-flight hydration jobs.
    void stopListener();

private:
    struct Private;
    std::unique_ptr<Private> _p;

    Vfs *_vfs = nullptr;
};

} // namespace OCC
