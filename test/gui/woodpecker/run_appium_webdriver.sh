#!/bin/bash

set -e

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/../ && pwd)"

# shellcheck disable=SC1091
if [ -f "$TEST_DIR/.woodpecker.env" ]; then
    . "$TEST_DIR/.woodpecker.env"
fi

WEBDRIVER_HOST="${WEBDRIVER_HOST:-127.0.0.1}"
WEBDRIVER_PORT="${WEBDRIVER_PORT:-4723}"

mkdir -p "$TEST_DIR/reports"

echo "[INFO] Starting Appium server on $WEBDRIVER_HOST:$WEBDRIVER_PORT..."

exec appium \
    --address "$WEBDRIVER_HOST" \
    --port "$WEBDRIVER_PORT" \
    --log-level warn \
    --log "$TEST_DIR/reports/appium.log"