#!/bin/bash
set -euo pipefail
repo_dir=$(cd "$(dirname "$0")/.." && pwd)
build_dir=$(mktemp -d /tmp/opencloud-socket-tests.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT
for extension in FinderSyncExt FileProviderExt; do
    source_dir="$repo_dir/shell_integration/MacOSX/OpenCloudFinderExtension/$extension"
    extra_sources=(-DTEST_FILEPROVIDER)
    if [[ "$extension" == FinderSyncExt ]]; then
        extra_sources=(-DTEST_FINDER "$source_dir/FinderSyncSocketLineProcessor.m")
    fi
    xcrun clang -fobjc-arc -fmodules -framework Foundation -I "$source_dir" \
        "$repo_dir/test/macos/test_fileprovider_sockets.m" "$source_dir/LocalSocketClient.m" \
        "${extra_sources[@]}" -o "$build_dir/$extension"
    "$build_dir/$extension"
done
