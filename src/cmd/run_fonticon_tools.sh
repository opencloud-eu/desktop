#!/bin/bash

# FontIcon Collector Tools Runner
# This script builds and runs the FontIcon usage analysis tools

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "======================================"
echo "FontIcon Usage Collector Tools"
echo "======================================"
echo "Root directory: $ROOT_DIR"
echo

# Function to build console version
build_console() {
    echo "Building console version..."
    mkdir -p "$SCRIPT_DIR/build_console"
    cd "$SCRIPT_DIR/build_console"
    cp "$SCRIPT_DIR/CMakeLists_console.txt" ./CMakeLists.txt
    cp "$SCRIPT_DIR/fonticon_collector_console.cpp" .
    cmake . > /dev/null
    make > /dev/null
    echo "Console version built successfully."
}

# Function to build GUI version
build_gui() {
    echo "Building GUI version..."
    mkdir -p "$SCRIPT_DIR/build_simple"
    cd "$SCRIPT_DIR/build_simple"
    cp "$SCRIPT_DIR/CMakeLists_simple.txt" ./CMakeLists.txt
    cp "$SCRIPT_DIR/fonticon_collector_simple.cpp" .
    cmake . > /dev/null
    make > /dev/null
    echo "GUI version built successfully."
}

# Function to run console version
run_console() {
    echo
    echo "======================================"
    echo "Running Console Analysis Tool"
    echo "======================================"
    cd "$ROOT_DIR"
    "$SCRIPT_DIR/build_console/fonticon_collector_console"
}

# Function to run GUI version
run_gui() {
    echo
    echo "======================================"
    echo "Running GUI Analysis Tool"
    echo "======================================"
    echo "Note: GUI requires X11 display. Use DISPLAY=:0 if running with X forwarding."
    cd "$ROOT_DIR"
    if command -v xvfb-run > /dev/null; then
        echo "Starting GUI with virtual framebuffer..."
        DISPLAY=:99 xvfb-run -a -s "-screen 0 1400x1000x24" "$SCRIPT_DIR/build_simple/fonticon_collector" &
        GUI_PID=$!
        echo "GUI started with PID $GUI_PID"
        echo "To stop: kill $GUI_PID"
        wait $GUI_PID 2>/dev/null || true
    else
        echo "Starting GUI (requires X11 display)..."
        "$SCRIPT_DIR/build_simple/fonticon_collector"
    fi
}

# Main execution
case "${1:-both}" in
    "console")
        build_console
        run_console
        ;;
    "gui")
        build_gui
        run_gui
        ;;
    "both"|"")
        build_console
        build_gui
        run_console
        echo
        echo "Press Enter to start GUI version (Ctrl+C to skip)..."
        read -r
        run_gui
        ;;
    "build")
        build_console
        build_gui
        echo "Both versions built successfully."
        ;;
    *)
        echo "Usage: $0 [console|gui|both|build]"
        echo "  console - Build and run console version only"
        echo "  gui     - Build and run GUI version only"  
        echo "  both    - Build and run both versions (default)"
        echo "  build   - Build both versions without running"
        exit 1
        ;;
esac