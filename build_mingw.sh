#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e
# Treat unset variables as an error.
set -u
# Ensure pipeline failures are reported
set -o pipefail

# --- Configuration ---
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
BUILD_DIR="$SCRIPT_DIR/build"
TOOLCHAIN_FILE="$SCRIPT_DIR/mingw-w64-x86_64.cmake"
BUILD_TYPE="Release" # Or "Debug" if needed

# --- Check for Deployment Path Argument ---
if [ -z "${1:-}" ]; then
  echo "Usage: $0 /path/to/deployment/directory"
  echo "Error: Deployment directory argument is required."
  exit 1
fi
DEPLOY_PATH="$1"

# --- Sanity Checks ---
if [ ! -f "$TOOLCHAIN_FILE" ]; then
    echo "Error: Toolchain file not found at $TOOLCHAIN_FILE"
    exit 1
fi
DEPLOY_PARENT_DIR=$(dirname "$DEPLOY_PATH")
if [ ! -d "$DEPLOY_PARENT_DIR" ]; then
    echo "Warning: Parent directory of deployment path does not exist: $DEPLOY_PARENT_DIR"
fi

# --- Build Steps ---
echo "--- Starting MinGW Cross-Compile Build ---"

echo "[1/5] Cleaning previous build directory..."
rm -rf "$BUILD_DIR"

echo "[2/5] Creating build directory..."
mkdir -p "$BUILD_DIR"

echo "[3/5] Configuring CMake..."
cd "$BUILD_DIR"
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DDEPLOY_DIRECTORY="$DEPLOY_PATH" \
    -G "Unix Makefiles"

echo "[4/5] Building target (make)..."
make -j$(nproc)

echo "[5/5] Installing target (make install)..."
# Check if install target exists and needs running
if ! make -q install > /dev/null 2>&1; then
    make install # No sudo needed
else
    echo "[5/5] Skipping installation (install target is up-to-date or DEPLOY_DIRECTORY was invalid)."
fi

cd "$SCRIPT_DIR"

echo "--- Build and Installation Complete ---"
# *** UPDATED FILENAME HERE ***
FINAL_DLL_PATH="$DEPLOY_PATH/game_x64.dll"
if [ -f "$FINAL_DLL_PATH" ]; then
    echo "Output DLL successfully installed to: $FINAL_DLL_PATH"
else
    echo "Warning: Expected DLL not found at $FINAL_DLL_PATH after installation."
    echo "Check CMake output and permissions for directory: $DEPLOY_PATH"
fi

exit 0