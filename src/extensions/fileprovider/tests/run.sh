#!/bin/bash
# Standalone unit tests for the File Provider extension's pure helpers.
# These compile without the full app build (Foundation only).
#
#   ./run.sh
#
set -euo pipefail
cd "$(dirname "$0")"

CXX="clang++ -fobjc-arc -framework Foundation -I.."
fail=0

build_run() {
    local name="$1"; shift
    local bin="/tmp/fp_test_${name}"
    echo "=== ${name} ==="
    # shellcheck disable=SC2086
    $CXX "$@" -o "$bin"
    if "$bin"; then :; else fail=1; fi
    echo
}

build_run webdav      test_fileprovider_webdav.mm           ../FileProviderWebDAV.mm
build_run item_cache  test_fileprovider_item_cache.mm       ../FileProviderItemCache.mm
build_run ws_delta    test_fileprovider_workingset_delta.mm ../FileProviderWorkingSetDelta.mm

if [ "$fail" -eq 0 ]; then
    echo "ALL TESTS PASSED"
else
    echo "SOME TESTS FAILED"
    exit 1
fi