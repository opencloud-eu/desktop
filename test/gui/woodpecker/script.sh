#!/bin/bash

touch .woodpecker.env

PY_REQUIREMENTS_PATH="test/gui/pyproject.toml"

# get playwright version from pyproject.toml
get_playwright_version() {
    if [[ ! -f "$PY_REQUIREMENTS_PATH" ]]; then
        echo "Error: file not found: $PY_REQUIREMENTS_PATH"
        exit 1
    fi

    playwright_version=$(grep 'playwright==' "$PY_REQUIREMENTS_PATH" | cut -d'=' -f3 | cut -d'.' -f1-2)
    playwright_version=${playwright_version//[^0-9.]/}
    if [[ -z "$playwright_version" ]]; then
        echo "Error: Playwright package not found in requirements.txt" >&2
        exit 78
    fi

    echo "$playwright_version"
}

# Function to check if the cache exists for the given commit ID
check_browsers_cache() {
    playwright_version=$(get_playwright_version)

    playwright_cache=$(mc find s3/$CACHE_BUCKET/desktop/browsers-cache/$playwright_version/playwright-browsers.tar.gz 2>&1 | grep 'Object does not exist')

    if [[ "$playwright_cache" != "" ]]
    then
        echo "Browsers cache for playwright v$playwright_version not found in cache."
        ENV="BROWSER_CACHE_FOUND=false\n"
    else
      echo "Browsers cache for playwright v$playwright_version found in cache."
      ENV="BROWSER_CACHE_FOUND=true\n"
    fi
}

get_requirementstxt_hash() {
    # Hash both pyproject.toml and uv.lock for more accurate cache key
    requirements_sha=$(cat test/gui/pyproject.toml test/gui/uv.lock | sha1sum | cut -d" " -f1)
    echo "$requirements_sha"
}

check_python_cache() {
    requirements_sha=$(get_requirementstxt_hash)
    python_cache=$(mc find s3/$CACHE_BUCKET/desktop/python-cache/$requirements_sha/python-cache.tar.gz 2>&1 | grep 'Object does not exist')

    if [[ "$python_cache" != "" ]]
    then
        echo "Python cache for '$requirements_sha' hash not found in cache."
        ENV="PYTHON_CACHE_FOUND=false\n"
    else
      echo "Python cache for '$requirements_sha' hash found in cache."
      ENV="PYTHON_CACHE_FOUND=true\n"
    fi
}

if [[ "$1" == "" ]]; then
    echo "Usage: $0 [COMMAND]"
    echo "Commands:"
    echo -e "  get_playwright_version \t get the playwright version from pyproject.toml"
    echo -e "  get_requirementstxt_hash \t get the hash of the current pyproject.toml and uv.lock"
    echo -e "  check_browsers_cache \t check if the browsers cache exists for the given playwright version"
    echo -e "  check_python_cache \t check if a cache for the current dependencies exists"
    exit 1
fi

$1

echo -e $ENV >> .woodpecker.env
