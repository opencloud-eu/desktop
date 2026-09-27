#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/fileprovider-webdav-tests.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT
sources="$root/shell_integration/MacOSX/OpenCloudFinderExtension/FileProviderExt/WebDAV"
xcrun swiftc -o "$build_dir/tests" \
    "$sources/TransferProgressDelegate.swift" "$sources/WebDAVItem.swift" "$sources/WebDAVXMLParser.swift" "$sources/WebDAVClient.swift" \
    "$sources/../Database/ItemMetadata.swift" "$sources/../FileProviderItem.swift" \
    "$root/test/macos/test_fileprovider_webdav.swift"
"$build_dir/tests"
