#!/bin/bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
xcrun swiftc -parse-as-library \
  "$repo_root/shell_integration/MacOSX/OpenCloudFinderExtension/FileProviderExt/FileProviderSyncStatus.swift" \
  "$repo_root/test/macos/test_fileprovider_status.swift" -o "$test_dir/tests"
"$test_dir/tests"
