#!/bin/bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
sources="$repo_root/shell_integration/MacOSX/OpenCloudFinderExtension/FileProviderExt"
xcrun swiftc -parse-as-library \
  "$sources/WebDAV/WebDAVItem.swift" \
  "$sources/Database/ItemMetadata.swift" \
  "$sources/Database/ItemDatabase.swift" \
  "$sources/WebDAV/TransferProgressDelegate.swift" \
  "$sources/WebDAV/WebDAVClient.swift" \
  "$sources/WebDAV/WebDAVXMLParser.swift" \
  "$repo_root/test/macos/test_fileprovider_database.swift" \
  -o "$test_dir/test-fileprovider-database"
"$test_dir/test-fileprovider-database"

xcrun swiftc -parse-as-library \
  "$sources/WebDAV/WebDAVItem.swift" \
  "$sources/Database/ItemMetadata.swift" \
  "$sources/Database/ItemDatabase.swift" \
  "$sources/FileProviderSyncStatus.swift" \
  "$sources/FileProviderStatusRecovery.swift" \
  "$sources/FileProviderItem.swift" \
  "$sources/FileProviderEnumerator.swift" \
  "$repo_root/test/macos/test_fileprovider_enumerator.swift" \
  -o "$test_dir/test-fileprovider-enumerator"
"$test_dir/test-fileprovider-enumerator"
