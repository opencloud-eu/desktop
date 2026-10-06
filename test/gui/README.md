<!-- add TOC -->

# Table of Contents

- [Desktop Client GUI Testing](#desktop-client-gui-testing)
  - [Linux vs Windows](#linux-vs-windows)
- [OpenCloud Server](#opencloud-server)
- [Running GUI Tests](#running-gui-tests)
  - [Linux (Ubuntu 24.04)](#linux-ubuntu-2404)
    - [Install System Dependencies](#install-system-dependencies)
    - [Build Desktop Client](#build-desktop-client)
    - [Run GUI Tests](#run-gui-tests)
  - [Windows](#windows)
    - [Set up Test Environment](#set-up-test-environment)
    - [Run GUI Tests](#run-gui-tests-1)
  - [Test Reports](#test-reports)
- [Writing GUI Test](#writing-gui-test)
  - [Code Formatting and Linting](#code-formatting-and-linting)

# Desktop Client GUI Testing

The OpenCloud desktop GUI tests use the following tools:

- [Behave](https://behave.readthedocs.io/en/stable/) – Executes the GUI test scenarios.
- [Appium](https://appium.io/docs/en/latest/) – Drives the test automation by sending WebDriver commands.
- [PyAutoGUI](https://pyautogui.readthedocs.io/en/latest/) – Performs mouse and keyboard interactions that are not exposed through the accessibility API.
- [selenium-webdriver-at-spi](https://invent.kde.org/sdk/selenium-webdriver-at-spi) – A WebDriver implementation for Appium that uses the Linux AT-SPI accessibility API to automate desktop applications.
- [NovaWindows](https://github.com/AutomateThePlanet/appium-novawindows-driver) – An Appium driver for Windows desktop application automation using Windows UI Automation.

## Linux vs Windows

| Linux | Windows |
|---|---|
| Appium Python Client | Appium Python Client |
| PyAutoGUI | PyAutoGUI |
| AT-SPI WebDriver | NovaWindows |
| AT-SPI | Windows UI Automation |

# OpenCloud Server

1. Install [Docker and Docker Compose plugin](https://docs.docker.com/engine/install/) if not already installed on your system. *(On Windows, ensure Docker Desktop is running).*

2. Create a `compose.yaml` file with the following configuration:
   ```yaml
   services:
   opencloud:
     image: opencloudeu/opencloud:latest
     entrypoint: /bin/sh
     command: ['-c', 'opencloud init || true && opencloud server']
     environment:
       OC_LOG_LEVEL: 'error'
       OC_INSECURE: true
       OC_URL: 'https://localhost:9200'
       IDM_ADMIN_PASSWORD: 'admin'
       PROXY_ENABLE_BASIC_AUTH: true
     ports:
       - 9200:9200
   ```

3. Start the server:

   ```bash
   docker compose up
   ```

# Running GUI Tests

## Linux (Ubuntu 24.04)

This guide explains how to build the OpenCloud desktop client and run the GUI test suite on Linux.

### Install System Dependencies

1. Install build dependencies:

   ```bash
   sudo apt update
   sudo apt install \
      build-essential \
      libgl1-mesa-dev \
      libglu1-mesa-dev \
      cmake \
      ninja-build \
      libfuse3-dev
   ```

2. Install test dependencies:

   ```bash
   sudo apt update
   sudo apt install \
      python3-nautilus \
      python3-pyatspi \
      libatspi2.0-dev \
      libdbus-1-dev \
      libgirepository1.0-dev \
      libcairo2-dev \
      python3-dev
   ```

### Build Desktop Client

1. Install the required build dependencies using [openbuild](https://github.com/opencloud-eu/openbuild)

   ```bash
   python3 ./openbuild.py --branch main --target linux-gcc-x86_64 -- --install-deps opencloud-desktop
   ```

> [!IMPORTANT]
> Verify that Qt was built with accessibility support enabled:
>
> ```bash
> strings main/linux-gcc-x86_64/lib/libQt6Gui.so | grep -i QSpiAccessibleBridge
> ```
>
> The output should contain:
>
> ```text
> QSpiAccessibleBridge
> ```

2. Build the desktop client

   Navigate to the desktop repository and build the client:

   ```bash
   mkdir build && cd build

   cmake \
      -DCMAKE_PREFIX_PATH=<path-to-openbuild>/main/linux-gcc-x86_64 \
      -GNinja \
      -DBUILD_TESTING=OFF \
      -DCMAKE_BUILD_TYPE=Debug \
      -DVIRTUAL_FILE_SYSTEM_PLUGINS=off \
      -S ..

   ninja
   ```

> [!NOTE]
> The desktop client binary will be located at `./bin/opencloud`.

### Run GUI Tests

1. Start the OpenCloud server (see [OpenCloud Server](#opencloud-server))

2. Install python test dependencies

   ```bash
   cd <desktop-repo-root>/test/gui
   make install
   ```

3. Configure the test environment

   Copy `config.sample.ini` to `config.ini`, then update the required values.

   ```ini
   [DEFAULT]
   APP_PATH=<full-path-to>/opencloud # desktop app binary: <desktop-repo-root>/build/bin/opencloud
   BACKEND_HOST=<opencloud-server-url> # https://localhost:9200
   ```

4. Start the AT-SPI WebDriver

   ```bash
   bash woodpecker/run_atspi_webdriver.sh
   ```

   > [!NOTE]
   > If you encounter `ModuleNotFoundError: No module named 'pyatspi'`:
   > Recreate the virtual environment using the system Python and allow access to system packages:
   >
   > ```bash
   > uv venv --python /usr/bin/python3 --system-site-packages
   > ```

5. Run the GUI Tests

   Run a test scenario:

   ```bash
   cd <desktop-repo-root>/test/gui
   uv run behave features/add-account/account.feature
   ```

> [!IMPORTANT]
>
> Close any running OpenCloud desktop client before running the GUI tests.

## Windows

For Windows, we use Appium with the NovaWindows driver to automate the OpenCloud desktop client through Windows UI Automation.

### Set up Test Environment

1. Install the necessary system dependencies:
   - [Python 3](https://www.python.org/downloads/windows/)
   - [uv](https://docs.astral.sh/uv/)
   - [Node.js](https://nodejs.org/)
   - [GNU Make](https://gnuwin32.sourceforge.net/packages/make.htm)

2. Install [Appium 2 or latest](https://appium.io/docs/en/latest/quickstart/install/)

   ```powershell
   npm install -g appium
   ```

3. Install the [NovaWindows driver](https://github.com/AutomateThePlanet/appium-novawindows-driver):

   ```powershell
   appium driver install --source=npm appium-novawindows-driver
   ```

   > [!NOTE]
   > To verify that the NovaWindows driver is installed, run:
   >
   > ```powershell
   > appium driver list
   > ```
   >
   > Ensure `appium-novawindows-driver` appears in the list of installed drivers.


### Run GUI Tests

1. Start the OpenCloud server (see [OpenCloud Server](#opencloud-server))

2. Configure test config:

   Copy `test\gui\config.sample.ini` to `test\gui\config.ini`, then update the required values. Ensure the OpenCloud desktop client is built or installed on Windows.

   ```ini
   [DEFAULT]
   APP_PATH=<full-path-to>\opencloud.exe # path to the desktop app executable
   BACKEND_HOST=<opencloud-server-url> # https://localhost:9200
   ```
3. Install python test dependencies

   ```powershell
   cd <desktop-repo-root>\test\gui
   make install
   ```

4. Start NovaWindows driver:

   ```powershell
   appium
   ```

5. Run the GUI Tests

   ```powershell
   uv run behave .\features\add-account\account.feature
   ```
   
> [!IMPORTANT]
>
> Close any running OpenCloud desktop client before running the GUI tests.

## Test Reports

Following test reports are generated in `test/gui/reports/` path:

- `report.log` – Contains the detailed execution log.
- `report.html` – An HTML report summarizing the test results.
- `screenshots/` – Screenshots captured when a test fails, showing the application state at the point of failure (when enabled).
- `recordings/` – Screen recordings of failed test runs (when enabled).

Screenshot and screen recording of the test execution can be enabled using the following:

```ini
# test/gui/config.ini
CI=true
RECORD_VIDEO_ON_FAILURE=true
```

Alternatively, enable them when running a test.

**Linux:**

```bash
CI=true \
RECORD_VIDEO_ON_FAILURE=true \
uv run behave features/add-account/account.feature
```

**Windows (PowerShell):**

```powershell
$env:CI="true"
$env:RECORD_VIDEO_ON_FAILURE="true"
uv run behave .\features\add-account\account.feature
```

# Writing GUI Test

## Code Formatting and Linting

The GUI tests use [Ruff](https://docs.astral.sh/ruff) for code formatting and linting.

1. Check Formatting and Linting

```bash
make python-lint
```

2. Apply Formatting and Fix Supported Issues

```bash
make python-lint-fix
```

> [!NOTE]
> Run the following command to check specific rule violation:
>
> ```bash
> uv run ruff check --select <RULE>
> ```
>
> See the [available Ruff rules](https://docs.astral.sh/ruff/rules/).