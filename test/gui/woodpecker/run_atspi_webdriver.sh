#!/bin/bash

set -e

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/../ && pwd)"
WEBDRIVER_DIR="$TEST_DIR/__webdriver"

mkdir -p "$WEBDRIVER_DIR"

DRIVER_FILE="atspi-webdriver.py"
DRIVER_URL="https://raw.githubusercontent.com/KDE/selenium-webdriver-at-spi"

# shellcheck disable=SC1091
. "$TEST_DIR/.woodpecker.env"

if [ -z "$ATSPI_WEBDRIVER_VERSION" ]; then
    ATSPI_WEBDRIVER_VERSION="master"
fi

if [ ! -f "$WEBDRIVER_DIR/$DRIVER_FILE" ]; then
    curl -sSL --fail "$DRIVER_URL/$ATSPI_WEBDRIVER_VERSION/selenium-webdriver-at-spi.py" -o "$WEBDRIVER_DIR/$DRIVER_FILE"
fi

if [ ! -f "$WEBDRIVER_DIR/app_roles.py" ]; then
    curl -sSL --fail "$DRIVER_URL/$ATSPI_WEBDRIVER_VERSION/app_roles.py" -o "$WEBDRIVER_DIR/app_roles.py"
fi

if [ -z "$WEBDRIVER_HOST" ]; then
    WEBDRIVER_HOST="0.0.0.0"
fi
if [ -z "$WEBDRIVER_PORT" ]; then
    WEBDRIVER_PORT="4723"
fi


# cd "$TEST_DIR"
# curl -LsSf https://astral.sh/uv/install.sh | sh
# export PATH="$HOME/.local/bin:$PATH"
uv venv --python /usr/bin/python3 --system-site-packages --clear
source .venv/bin/activate
uv sync --frozen

# run webdriver server
export FLASK_ENV=production
export FLASK_APP="$WEBDRIVER_DIR/$DRIVER_FILE"
flask run --host="$WEBDRIVER_HOST" --port="$WEBDRIVER_PORT" --no-reload