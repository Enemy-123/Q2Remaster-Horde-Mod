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

# --- Check for Arguments ---
if [ -z "${1:-}" ]; then
  echo "Usage: $0 /path/to/deployment/directory [BuildType]"
  echo "  BuildType can be 'Debug', 'Release', 'RelWithDebInfo', or 'DebugASan' (defaults to Release)."
  echo "Error: Deployment directory argument is required."
  exit 1
fi
DEPLOY_PATH="$1"
# Set BUILD_TYPE to the second argument, or "Release" if it's not provided.
BUILD_TYPE="${2:-Release}"
echo "--- Build Type set to: $BUILD_TYPE ---"


# --- Sanity Checks ---
if [ ! -f "$TOOLCHAIN_FILE" ]; then
    echo "Error: Toolchain file not found at $TOOLCHAIN_FILE"
    exit 1
fi
if [ ! -d "$DEPLOY_PATH" ]; then
    echo "Warning: Deployment directory does not exist: $DEPLOY_PATH"
    echo "The installation step might fail. Creating it..."
    mkdir -p "$DEPLOY_PATH"
fi

# --- Build Steps ---
echo "--- Starting MinGW Cross-Compile Build ---"

echo "[1/4] Cleaning and creating build directory..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "[2/4] Configuring CMake..."
cd "$BUILD_DIR"
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DDEPLOY_DIRECTORY="$DEPLOY_PATH" \
    -G "Unix Makefiles"

echo "[3/4] Building target (make)..."
# Use cmake --build for portability, but falls back to make here.
cmake --build . -- -j$(nproc)

echo "[4/4] Installing target..."
cmake --build . --target install

cd "$SCRIPT_DIR"

echo "--- Build and Installation Complete ---"
FINAL_DLL_PATH="$DEPLOY_PATH/game_x64.dll"
if [ -f "$FINAL_DLL_PATH" ]; then
    echo "Output DLL successfully installed to: $FINAL_DLL_PATH"
else
    echo "Error: Expected DLL not found at $FINAL_DLL_PATH after installation."
    exit 1
fi

# --- Post-Build Action for AddressSanitizer ---
if [ "$BUILD_TYPE" == "DebugASan" ]; then
    echo ""
    echo "!!! ACTION REQUIRED FOR AddressSanitizer !!!"
    ASAN_DLL_PATH=$(find /usr/lib/gcc/x86_64-w64-mingw32 -name "libasan*.dll" | head -n 1)
    if [ -n "$ASAN_DLL_PATH" ]; then
        echo "Your mod was built with ASan. You MUST copy the ASan runtime to the game directory."
        echo "Run this command:"
        echo "cp \"$ASAN_DLL_PATH\" \"$DEPLOY_PATH/\""
        # Optional: uncomment the line below to copy it automatically
        # cp "$ASAN_DLL_PATH" "$DEPLOY_PATH/" && echo "ASan runtime copied automatically."
    else
        echo "Warning: Could not automatically find the ASan runtime (libasan*.dll)."
        echo "You must find it in your MinGW installation and copy it to the game directory manually."
    fi
fi

exit 0