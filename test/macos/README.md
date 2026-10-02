# macOS FileProvider tests

These checks require Xcode 26 and the macOS 26 SDK. Run the native provider tests
on macOS 26. Configure the desktop client with its normal CMake/Craft dependencies
and `BUILD_TESTING=ON`, then build the client and test targets.

From a configured build directory:

```bash
ctest --output-on-failure -R '^(testfolderman|testsyncproviderselection|testfileproviderlifecycle|testmacsocketapi|testfileprovider_.*)$'
```

For a Craft build, set `DYLD_LIBRARY_PATH` to the build's `bin` directory so the
C++ tests load the newly built libraries. The interactive credential-manager test
is separate from this selection.

The provider suites can also run from the repository root:

```bash
bash tools/test-fileprovider-database.sh
bash tools/test-fileprovider-webdav.sh
bash tools/test-fileprovider-sockets.sh
bash tools/test-fileprovider-search.sh
bash tools/test-fileprovider-status.sh
bash tools/test-fileprovider-trash.sh
bash tools/test-fileprovider-actions.sh
```

These cover persistent identity and enumeration, conditional DAV operations,
socket framing, search and thumbnails, pending work and error status, server
Trash, and private-link actions. The Qt lifecycle tests inject account/domain
failures without configuring real accounts. Swift transport tests use controlled
responses; they do not certify production-server compatibility.

Build both extension architectures without signing:

```bash
xcodebuild \
  -project shell_integration/MacOSX/OpenCloudFinderExtension/OpenCloudFinderExtension.xcodeproj \
  -target FileProviderExt -target FinderSyncExt -configuration Debug \
  ARCHS='arm64 x86_64' ONLY_ACTIVE_ARCH=NO CODE_SIGNING_ALLOWED=NO
```

## Signed Finder exercise

The separate fixture uses a disposable host, isolated credentials and a loopback
DAV server. It requires a built FileProviderExt.appex, a valid signing identity
and matching Team ID, and an interactive macOS session.

```bash
python3 tools/test-fileprovider-signed.py \
  --extension "/absolute/path/to/FileProviderExt.appex" \
  --sign-id "Developer ID Application: YOUR NAME (TEAMID)" \
  --team-id "TEAMID" \
  --interactive \
  --bundle-id eu.opencloud.desktop.review.local
```

When prompted, enable **OpenCloud Isolated Test** under **System Settings >
General > Login Items & Extensions > File Providers**. Reuse the isolated bundle
ID to avoid creating a new provider preference for each run.

The exercise checks signed XPC caller authorization, Keychain isolation/recovery,
Finder enumeration and hydration, upload/rename/delete, package directories,
Trash/restore/permanent deletion, status snapshots, offline reads, eviction,
credential revocation, disconnect/reconnect, and cleanup. It removes its domains
and unregisters the temporary bundle when finished.

Omit `--interactive` to exercise native services without Finder I/O. Use `--help`
for other options. This fixture is separate from unattended CTest because native
provider activation can require user interaction. Passing it does not establish
clean-machine installation, notarization, older macOS compatibility, or the
behavior of the shipped Settings UI.

See [Finder behavior and limits](../../shell_integration/MacOSX/README.md).
