#!/bin/bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
sources="$repo_root/shell_integration/MacOSX/OpenCloudFinderExtension/FileProviderExt"
xcrun swiftc -parse-as-library \
  "$sources/WebDAV/WebDAVItem.swift" \
  "$sources/WebDAV/TransferProgressDelegate.swift" \
  "$sources/WebDAV/WebDAVClient.swift" \
  "$sources/WebDAV/WebDAVXMLParser.swift" \
  "$sources/WebDAV/WebDAVTrash.swift" \
  "$sources/Database/ItemMetadata.swift" \
  "$sources/Database/ItemDatabase.swift" \
  "$sources/FileProviderTrash.swift" \
  "$repo_root/test/macos/test_fileprovider_trash.swift" \
  -o "$test_dir/test-fileprovider-trash"
"$test_dir/test-fileprovider-trash"
