# Desktop Client GUI Testing

The OpenCloud desktop GUI tests use the following tools:

- **Behave** – Executes the GUI test scenarios.
- **Appium** – Drives the test automation by sending WebDriver commands.
- **PyAutoGUI** – Performs mouse and keyboard interactions that are not exposed through the accessibility API.
- **selenium-webdriver-at-spi** – A WebDriver implementation for Appium that uses the Linux AT-SPI accessibility API to automate desktop applications.

# Running GUI Tests

## Linux (Ubuntu 24.04)

This guide explains how to build the OpenCloud desktop client and run the GUI test suite on Linux.

### Prerequisites

Install the following tools before continuing:

- Git
- Python 3
- Docker and Docker Compose
- CMake
- Ninja

**Install system dependencies**

```bash
sudo apt update

sudo apt install \
    xclip \
    python3-pyatspi \
    libatspi2.0-dev \
    libdbus-1-dev \
    libfuse3-dev \
    libfuse-dev
```

### 1. Install `uv` (Python environment and dependency manager)

Install [uv](https://astral.sh/uv) using the official installer:

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

Verify the installation:

```bash
uv --version
```

> [!NOTE]
> If `uv` is not found after installation, ensure that `~/.local/bin` is in your `PATH`.


### 2. Build the Desktop Client

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
mkdir build
cd build

cmake \
    -DCMAKE_PREFIX_PATH=<path-to-openbuild>/main/linux-gcc-x86_64 \
    -GNinja \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Debug \
    -DVIRTUAL_FILE_SYSTEM_PLUGINS=off \
    -S ..

ninja
```

### 3. Install the Test Dependencies

```bash
cd <desktop-repo-root>/test/gui
make install
```

### 4. Start the OpenCloud Server

Create a `compose.yaml` file with the following configuration:

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

Start the server:

```bash
docker compose up
```

### 5. Start the AT-SPI WebDriver

Run the following commands to start the webdriver server:

```bash
cd <desktop-repo-root>/test/gui
source .venv/bin/activate
bash woodpecker/run_atspi_webdriver.sh
```

### 6. Configure the test environment

Copy `config.sample.ini` to `config.ini`, then update the required values.

```ini
[DEFAULT]
APP_PATH=<full-path-to>/opencloud # desktop app path
BACKEND_HOST=<opencloud-server-url>
```

### 7. Run the GUI Tests

> [!NOTE]
> - Close any running OpenCloud desktop client before running the GUI tests.

Run a test scenario:

```bash
cd <desktop-repo-root>/test/gui
uv run behave features/add-account/account.feature
```

### Test Reports

Following test reports are generated in `test/gui/reports/` path:

- **`report.log`** – Contains the detailed execution log.
- **`report.html`** – An HTML report summarizing the test results.
- **`screenshots/`** – Screenshots captured when a test fails, showing the application state at the point of failure (when enabled).
- **`recordings/`** – Screen recordings of failed test runs (when enabled).

Screenshot and screen recording of the test execution can be enabled using the following:

```ini
# test/gui/config.ini
CI=true
RECORD_VIDEO_ON_FAILURE=true
```

Or while running the test:

```bash
CI=true \
RECORD_VIDEO_ON_FAILURE=true \
uv run behave features/add-account/account.feature
```