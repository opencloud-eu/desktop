#!/bin/bash

touch .woodpecker.env

# get playwright version from package.json
get_playwright_version() {
    PACKAGE_JSON_PATH="test/gui/webUI/package.json"
    if [[ ! -f "$PACKAGE_JSON_PATH" ]]; then
        echo "Error: package.json file not found."
    fi

    playwright_version=$(grep '"@playwright/test":' "$PACKAGE_JSON_PATH" | cut -d':' -f2 | tr -d '", ')
    if [[ -z "$playwright_version" ]]; then
        echo "Error: Playwright package not found in package.json." >&2
        exit 78
    fi

    echo "$playwright_version"
}

# Function to check if the cache exists for the given commit ID
check_browsers_cache() {
    get_playwright_version

    playwright_cache=$(mc find s3/$CACHE_BUCKET/web/browsers-cache/$playwright_version/playwright-browsers.tar.gz 2>&1 | grep 'Object does not exist')

    if [[ "$playwright_cache" != "" ]]
    then
        echo "Playwright v$playwright_version supported browsers doesn't exist in cache."
        ENV="BROWSER_CACHE_FOUND=false\n"
    else
      echo "Playwright v$playwright_version supported browsers found in cache."
      ENV="BROWSER_CACHE_FOUND=true\n"
    fi
}

check_python_cache() {
    requirements_sha=$(sha1sum test/gui/requirements.txt | cut -d" " -f1)
    python_cache=$(mc find s3/$CACHE_BUCKET/desktop/python-cache/python-cache-$requirements_sha.tar.gz 2>&1 | grep 'Object does not exist')

    if [[ "$python_cache" != "" ]]
    then
        echo "Python cache of requirements with hash $requirements_sha doesn't exist in cache."
        ENV="PYTHON_CACHE_FOUND=false\n"
    else
      echo "Python cache of requirements with hash $requirements_sha found in cache."
      ENV="PYTHON_CACHE_FOUND=true\n"
    fi
}

if [[ "$1" == "" ]]; then
    echo "Usage: $0 [COMMAND]"
    echo "Commands:"
    echo -e "  get_playwright_version \t get the playwright version from package.json"
    echo -e "  check_browsers_cache \t check if the browsers cache exists for the given playwright version"
    echo -e "  check_python_cache \t check if a cache for the current requirements.txt exists"
    exit 1
fi

$1

echo -e $ENV >> .woodpecker.env
