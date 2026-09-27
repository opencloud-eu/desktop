# macOS Finder integration

The macOS on-demand provider uses Apple's public FileProvider APIs. Its scope is the OpenCloud locations in Finder. It does not move or back up Desktop and Documents.

## Architecture and build requirements

The desktop client registers a FileProvider domain for each account space. It
passes credentials through an authenticated XPC service; the extension stores
its domain credentials in Keychain and performs WebDAV requests independently.
SQLite stores item identities, change history, and local Finder metadata. The
host receives provider status over XPC and combines it for the tray and Settings.
Traditional Finder overlays use a Unix socket in the shared App Group container.

The FileProvider extension currently requires macOS 26, and building it requires
Xcode 26 with the macOS 26 SDK. FinderSync retains its macOS 10.15 deployment
target. The host checks the bundled provider's minimum OS version before choosing
on-demand mode; otherwise it uses traditional folder sync. Older macOS runtime
support for FileProvider would require availability guards and separate testing.

CMake builds both extensions and copies them into the app's `Contents/PlugIns`.
Use `APPLE_DEVELOPMENT_TEAM_ID` and `SOCKETAPI_TEAM_IDENTIFIER_PREFIX` for your
signing team; the latter includes the trailing dot. The host and both extensions
must have matching signing identities and App Group entitlements. Standalone
Xcode builds accept `DEVELOPMENT_TEAM` and `OC_SOCKETAPI_TEAM_IDENTIFIER_PREFIX`.
Keep the standalone Xcode `MARKETING_VERSION` aligned with `VERSION.cmake`.

## Feature matrix

| Feature | Behavior and limits |
| --- | --- |
| On-demand files | Finder lists placeholders and downloads contents when opened. Hydrated contents remain available offline. |
| Download Now, Remove Download, Keep Downloaded | macOS owns these controls and user pinning. The provider supplies a lazy root policy, inherited child policies, and cancellable transfers. |
| Sync status | The tray and account Settings show active uploads/downloads, available byte progress, pending work, errors, last checked time, and observed sync completion. Authentication alone never means synced. A provider reconnect temporarily returns that location to checking, preserves real errors, and leaves other locations connected. Unavailable, stale, or truncated pending information cannot produce a synced state. |
| Finder transfer presentation | Transfers publish native Progress objects. macOS decides the icons, menus, and progress presentation. There is no public setter for iCloud's exact sidebar checkmark and last-sync popover. |
| File operations | Create, edit, rename, move, and delete respect DAV permissions and conditional requests. Conflicts preserve local contents without overwriting unrelated server contents. |
| Trash, restore, permanent deletion | Standard OpenCloud space endpoints use the server recycle bin. Restore rejects destination collisions. Permanently deleting an item already in Trash uses the recycle API. Cloud-only trashed contents must be restored before opening because the recycle API provides no file download operation. |
| Finder tags and metadata | Tags, last-used time, creation date overrides, file flags, and extended attributes survive refresh, restart, and metadata-cache recovery. They are stored per account on this Mac; this does not replicate these fields between devices. Uploads send supported modification timestamps with X-OC-Mtime. |
| Favorites | Finder owns macOS sidebar favorites and user pinning. The extension does not call the iOS-only favorite-rank API. |
| Cloud search | On macOS 26, supported OpenCloud space domains advertise native string search. Results use server search scoped to that space, with bounded snapshots and Finder-sized pages. This does not promise full-content indexing or unlimited search results. |
| Thumbnails and Quick Look | The provider requests server previews without downloading the full document. Unsupported previews return no thumbnail. Finder and installed Quick Look plugins handle previews that need actual file contents. |
| Browser, sharing, version history | Native context actions open the existing OpenCloud private link. Sharing requires reshare permission. Sharing and versions use the web interface; the actions never create a public share automatically. A separate action copies the private link. |
| Authentication and recovery | Signed XPC and domain-scoped Keychain records supply credentials. Authentication, connectivity, permission, quota, and conflict failures remain distinct. Recovery signals eligible native work for retry. |
| Packages and symlinks | Directory creation preserves directory semantics. The provider rejects unsupported serialized package payloads and symlinks before writing remote contents. It does not silently upload an empty file or invent a DAV link format. |
| Traditional sync | On-demand is the default when the extension is available. Enabling traditional folder sync in General Settings requires a restart and disconnects on-demand domains before folder sync starts. Returning to on-demand stops traditional sync. |
| Exclude from sync | Not advertised. Apple's exclusion contract removes the item from the provider after hydration, which would delete its remote counterpart under the current DAV contract. Remove Download remains the safe way to release local storage while retaining server contents. |

This change does not add an app-level Pause button. Finder owns cancellation and retry controls, and macOS can disable the provider. A persistent app pause needs a separate lifecycle state so reconciliation does not reconnect a deliberately disconnected domain.

Search, remote previews, and recoverable Trash require the OpenCloud `/dav/spaces/<space>` API. Generic DAV roots do not advertise those space-specific capabilities. The virtual Shares location remains browsable and supports on-demand contents, but does not advertise a recycle bin or scoped cloud search because those operations require a real storage-space root. Server configuration, permissions, macOS version, and installed preview plugins can further restrict individual features.

Items already known before trashing retain their provider identities through restore, including directory descendants. The recycle API does not expose the live IDs of previously unseen descendants. Such descendants may receive their live server identities when the restored directory is scanned. The provider does not guess identity from a matching filename.

An upload error for a newly created item can outlive that item if it is discarded locally before receiving a server ID. Public callbacks do not guarantee notification of that deletion. The provider keeps the error until it observes recovery or resets its status; an unrelated refresh cannot safely prove that the upload succeeded. Confirmed server deletions clear errors for the removed item and its descendants, while preserving errors for children moved elsewhere.

## Public API references

- [FileProvider global progress](https://developer.apple.com/documentation/fileprovider/nsfileprovidermanager/globalprogress(for:))
- [Pending-set enumeration](https://developer.apple.com/documentation/fileprovider/nsfileproviderpendingsetenumerator)
- [Content policies](https://developer.apple.com/documentation/fileprovider/nsfileprovideritemprotocol/contentpolicy)
- [Native string search](https://developer.apple.com/documentation/fileprovider/nsfileprovidersearching)
- [Replicated FileProvider behavior and packages](https://developer.apple.com/videos/play/wwdc2021/10182/)

## Development signing

Use the same Developer ID identity and a stable local installation path, such as `~/Applications/OpenCloud Development.app`, for successive manual builds. Verify the signed bundle before replacing the local installation and quit the previous app first. Use the installed copy for routine testing instead of launching temporary staging copies.

An existing credential may still require one approval when moving from an Apple Development signature to Developer ID. After checking the requesting app, use the normal macOS **Always Allow** option. This workflow preserves account credentials and does not broaden their Keychain access to every application. See [Apple's Keychain access instructions](https://support.apple.com/guide/keychain-access/kyca1243/mac).

See [FileProvider test instructions](../../test/macos/README.md) for automated and signed runtime checks. A passing isolated fixture does not establish production-server compatibility, clean-machine installation, or notarization readiness.
