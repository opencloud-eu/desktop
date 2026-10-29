#!/bin/bash

set -e

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/../ && pwd)"
DRIVER_FILE="$TEST_DIR/atspi-selenium-driver/atspi-selenium-driver.py"

if [ -z "$WEBDRIVER_HOST" ]; then
    WEBDRIVER_HOST="0.0.0.0"
fi
if [ -z "$WEBDRIVER_PORT" ]; then
    WEBDRIVER_PORT="4723"
fi

# run webdriver server
export FLASK_ENV=production
export FLASK_APP="$DRIVER_FILE"
uv run flask run --host="$WEBDRIVER_HOST" --port="$WEBDRIVER_PORT" --no-reload