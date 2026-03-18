// FileProviderXPCService -- XPC communication bridge between the File Provider
// extension process and the main OpenCloud application process.
#pragma once

#import <CoreGraphics/CoreGraphics.h>
#import <FileProvider/FileProvider.h>
#import <Foundation/Foundation.h>
#import <os/log.h>

NS_ASSUME_NONNULL_BEGIN

/// Stable XPC service name shared between the extension and the main app.
/// Both sides must agree on this identifier.
static NSString *const kOpenCloudXPCServiceName = @"eu.opencloud.desktop.fileprovider.xpc";

/// App Group identifier used to share data between the main app and extension.
static NSString *const kOpenCloudAppGroupIdentifier = @"P4D766R5ZA.eu.opencloud.desktop";

/// Filename for the XPC listener endpoint stored in the App Group shared container.
/// The main app writes this file; the extension reads it to establish the XPC connection.
static NSString *const kOpenCloudXPCEndpointFilename = @"xpc_listener_endpoint.data";

#pragma mark - XPC Protocol

/// Protocol defining the messages the File Provider Extension sends to the main
/// OpenCloud application via XPC. The main app must vend an object conforming
/// to this protocol on its NSXPCListener.
///
/// All methods are asynchronous and use completion handlers to return results
/// back to the extension process.
@protocol OpenCloudXPCServiceProtocol <NSObject>

/// Request the main app to hydrate (download) a file's contents.
/// @param fileId  The server-side file identifier.
/// @param url     The local URL where the content should be written.
/// @param handler Called when hydration completes; error is nil on success.
- (void)requestHydration:(NSString *)fileId targetURL:(NSURL *)url completionHandler:(void (^)(NSError *_Nullable error))handler;

/// Schedule an upload of a locally-created or modified file to the server.
/// @param localURL         The local file URL containing the content to upload.
/// @param parentId         The server-side identifier of the parent folder.
/// @param handler          Called with the server-assigned file ID on success, or error.
- (void)scheduleUpload:(NSURL *)localURL
      parentIdentifier:(NSString *)parentId
     completionHandler:(void (^)(NSString *_Nullable serverFileId, NSError *_Nullable error))handler;

/// Query the current pin state for a file.
/// @param fileId  The server-side file identifier.
/// @param handler Called with the pin state (as NSInteger) or error.
- (void)requestPinState:(NSString *)fileId completionHandler:(void (^)(NSInteger pinState, NSError *_Nullable error))handler;

/// Set the pin state for a file (e.g., always keep downloaded, or free space).
/// @param pinState The desired pin state (as NSInteger).
/// @param fileId   The server-side file identifier.
/// @param handler  Called when the operation completes; error is nil on success.
- (void)setPinState:(NSInteger)pinState forFileId:(NSString *)fileId completionHandler:(void (^)(NSError *_Nullable error))handler;

/// Connectivity check. Returns YES if the main app is alive and responding.
/// @param handler Called with the liveness status.
- (void)ping:(void (^)(BOOL alive))handler;

/// Enumerate child items of a container (folder) from the sync journal.
/// @param containerId  The file ID of the parent container, or root identifier.
/// @param cursor       Opaque pagination cursor (empty string for first page).
/// @param handler      Called with an array of item dictionaries, an optional next cursor
///                     (nil if no more pages), or an error.
- (void)enumerateItems:(NSString *)containerId
                cursor:(NSString *)cursor
     completionHandler:(void (^)(NSArray<NSDictionary *> *_Nullable items, NSString *_Nullable nextCursor, NSError *_Nullable error))handler;

/// Fetch metadata for a single item by its server-side file identifier.
/// @param identifier  The file ID to look up.
/// @param handler     Called with item metadata dictionary or error.
- (void)itemForIdentifier:(NSString *)identifier completionHandler:(void (^)(NSDictionary *_Nullable itemDict, NSError *_Nullable error))handler;

/// Create a directory on the server.
/// @param name        The directory name.
/// @param parentId    The server-side identifier of the parent folder.
/// @param handler     Called with metadata dictionary of the created directory, or error.
- (void)createDirectory:(NSString *)name
       parentIdentifier:(NSString *)parentId
      completionHandler:(void (^)(NSDictionary *_Nullable itemDict, NSError *_Nullable error))handler;

/// Rename an item on the server.
/// @param fileId      The server-side file identifier.
/// @param newName     The new filename.
/// @param handler     Called with updated metadata dictionary, or error.
- (void)renameItem:(NSString *)fileId
              newName:(NSString *)newName
    completionHandler:(void (^)(NSDictionary *_Nullable itemDict, NSError *_Nullable error))handler;

/// Move an item to a different parent folder on the server.
/// @param fileId      The server-side file identifier.
/// @param newParentId The server-side identifier of the new parent folder.
/// @param handler     Called with updated metadata dictionary, or error.
- (void)moveItem:(NSString *)fileId
            newParent:(NSString *)newParentId
    completionHandler:(void (^)(NSDictionary *_Nullable itemDict, NSError *_Nullable error))handler;

/// Delete an item from the server.
/// @param fileId      The server-side file identifier.
/// @param handler     Called with nil on success, or error.
- (void)deleteItem:(NSString *)fileId completionHandler:(void (^)(NSError *_Nullable error))handler;

/// Fetch a thumbnail image for a file.
/// @param fileId  The server-side file identifier.
/// @param size    The requested thumbnail dimensions.
/// @param handler Called with PNG image data, or nil if no thumbnail is available.
- (void)fetchThumbnail:(NSString *)fileId size:(CGSize)size completionHandler:(void (^)(NSData *_Nullable imageData, NSError *_Nullable error))handler;

@end

#pragma mark - XPC Service Source

/// Implements NSFileProviderServiceSource to provide XPC connectivity between
/// the File Provider extension and the main OpenCloud app.
///
/// The extension registers this service source so the system can broker
/// connections. The main app's NSXPCListener vends an object conforming to
/// OpenCloudXPCServiceProtocol.
API_AVAILABLE(macos(12.0))
@interface FileProviderXPCService : NSObject <NSFileProviderServiceSource>

/// The stable service name used to identify this XPC service.
@property (nonatomic, readonly, copy) NSFileProviderServiceName serviceName;

/// Returns a proxy object conforming to OpenCloudXPCServiceProtocol for
/// sending messages to the main application. May return nil if the
/// connection has not been established.
@property (nonatomic, readonly, nullable) id<OpenCloudXPCServiceProtocol> remoteObjectProxy;

/// Designated initializer.
- (instancetype)init;

/// Explicitly invalidate the XPC connection. Called during extension teardown.
- (void)invalidate;

@end

NS_ASSUME_NONNULL_END
