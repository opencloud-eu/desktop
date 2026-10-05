#!/bin/bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
sources="$repo_root/shell_integration/MacOSX/OpenCloudFinderExtension/FileProviderExt"
xcrun swiftc -parse-as-library \
  "$sources"/WebDAV/*.swift \
  "$sources/Database/ItemMetadata.swift" "$sources/Database/ItemDatabase.swift" \
  "$sources/FileProviderItem.swift" "$sources/FileProviderActions.swift" \
  "$repo_root/test/macos/test_fileprovider_actions.swift" \
  -o "$test_dir/test-fileprovider-actions"
"$test_dir/test-fileprovider-actions" "$sources/Info.plist"
