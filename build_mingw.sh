#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e
# Treat unset variables as an error.
set -u
# Ensure pipeline failures are reported
set -o pipefail

# --- Configuration ---
# Get the directory where the script resides (project root)
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
BUILD_DIR="$SCRIPT_DIR/build"
TOOLCHAIN_FILE="$SCRIPT_DIR/mingw-w64-x86_64.cmake"
BUILD_TYPE="Release" # Or "Debug" if needed

# --- Check for Deployment Path Argument ---
# The script expects the desired deployment path as the first argument
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
# Check if the deployment directory *parent* exists (safer than checking the dir itself, as CMake might create it)
DEPLOY_PARENT_DIR=$(dirname "$DEPLOY_PATH")
if [ ! -d "$DEPLOY_PARENT_DIR" ]; then
    echo "Warning: Parent directory of deployment path does not exist: $DEPLOY_PARENT_DIR"
    # Decide if you want to exit or continue
    # exit 1
fi


# --- Build Steps ---
echo "--- Starting MinGW Cross-Compile Build ---"

echo "[1/5] Cleaning previous build directory..."
rm -rf "$BUILD_DIR"

echo "[2/5] Creating build directory..."
mkdir -p "$BUILD_DIR"

echo "[3/5] Configuring CMake..."
# Change directory into the build dir to run cmake
cd "$BUILD_DIR"
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DDEPLOY_DIRECTORY="$DEPLOY_PATH" \
    -G "Unix Makefiles"

echo "[4/5] Building target (make)..."
# Use nproc to get the number of CPU cores for parallel build
make -j$(nproc)

echo "[5/5] Installing target (make install)..."
make install

# Go back to the original directory
cd "$SCRIPT_DIR"

echo "--- Build and Installation Complete ---"
echo "Output DLL: $DEPLOY_PATH/game_x86.dll"

exit 0
