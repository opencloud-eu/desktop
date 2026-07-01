# Running GUI Tests Locally (Linux)

This guide explains how to build the OpenCloud desktop client and run the GUI test suite locally on Linux.

## Prerequisites

Before you begin, ensure the following are installed:

- Linux (Ubuntu or another Debian-based distribution)
- Git
- Python 3
- Docker and Docker Compose
- CMake
- Ninja

## 1. Install System Dependencies

Install the required system packages:

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

## 2. Build the Desktop Client

### Clone OpenBuild

Clone the OpenBuild repository:

```bash
git clone https://github.com/opencloud-eu/openbuild.git
cd openbuild
```

### Build the client

```bash
python3 ./openbuild.py \
    --branch build/build-qtbase-with-a11y \
    --target linux-gcc-x86_64 \
    -- \
    --install-deps opencloud-desktop
```

> [!IMPORTANT]
> Verify that Qt was built with accessibility support enabled:
>
> ```bash
> strings ~/CraftRoot/lib/libQt6Gui.so | grep -i QSpiAccessibleBridge
> ```
>
> The command should print:
>
> ```text
> QSpiAccessibleBridge
> ```
>
> If no output is produced, rebuild the client using the `build/build-qtbase-with-a11y` branch.

### Configure the project

```bash
cmake \
    -DCMAKE_PREFIX_PATH=<path-to-openbuild>/build/build-qtbase-with-a11y/linux-gcc-x86_64 \
    -GNinja \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Debug \
    -DVIRTUAL_FILE_SYSTEM_PLUGINS=off \
    -S ..
```

Build the project:

```bash
ninja
```

## 3. Clone the AT-SPI WebDriver

```bash
git clone https://invent.kde.org/sdk/selenium-webdriver-at-spi.git
```

## 4. Set Up the Python Environment

Navigate to the GUI test directory:

```bash
cd test/gui
```

Create a virtual environment:

```bash
python3 -m venv venv --system-site-packages
```

Activate it:

```bash
source venv/bin/activate
```

Install the required Python packages:

```bash
pip install -r requirements.txt
```

## 5. Start the OpenCloud Server

From the desktop repository root, start the server:

```bash
docker compose up
```

> [!NOTE]
> Keep this terminal running while executing the GUI tests.

## 6. Start the AT-SPI WebDriver

Open a new terminal and activate the virtual environment:

```bash
cd test/gui
source venv/bin/activate
```

Start the WebDriver:

```bash
FLASK_APP=<path-to-selenium-webdriver-at-spi>/selenium-webdriver-at-spi.py \
FLASK_ENV=production \
flask run \
    --port 4723 \
    --no-reload
```

> [!NOTE]
> Leave this terminal running while the tests execute.

## 7. Configure the Test Environment

Edit `test/gui/config.ini`:

```ini
[DEFAULT]
APP_PATH=<full-path-to>/opencloud
BACKEND_HOST=https://localhost:9200
```

Where:

- `APP_PATH` is the path to the built OpenCloud desktop client.
- `BACKEND_HOST` is the URL of the running OpenCloud server.

## 8. Run the GUI Tests

Open another terminal:

```bash
cd test/gui
source venv/bin/activate
```

Run a smoke test:

```bash
behave --tags=smoke features/add-account/account.feature
```

Or run the complete test suite:

```bash
behave
```

## Troubleshooting

> [!TIP]
> If you encounter an error similar to:
>
> ```text
> pnpm: command not found
> ```
>
> create a system-wide symbolic link:
>
> ```bash
> sudo ln -s $(which pnpm) /usr/bin/pnpm
> ```

> [!TIP]
> If you see:
>
> ```text
> Error in getClipboardText() invocation:
> The Froglogic test extension missing on the display
> ```
>
> log out and select an **Xorg** session from the login screen before running the tests again.

> [!TIP]
> If the desktop client does not launch, verify that `APP_PATH` in `test/gui/config.ini` points to the correct executable.

> [!TIP]
> If the tests cannot connect to the backend, ensure the OpenCloud server is running:
>
> ```bash
> docker compose up
> ```
>
> and verify that `BACKEND_HOST` matches the server URL.

> [!TIP]
> If the AT-SPI WebDriver cannot be reached, make sure it is still running on port **4723**.

> [!TIP]
> If the accessibility check does not print `QSpiAccessibleBridge`, rebuild the client using the accessibility-enabled Qt branch before running the tests.