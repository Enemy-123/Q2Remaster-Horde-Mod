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
TOOLCHAIN_ROOT="/home/perrobjorn/tools/llvm-mingw-20250709-ucrt-ubuntu-22.04-x86_64"

# --- Check for Arguments ---
if [ -z "${1:-}" ]; then
  echo "Usage: $0 /path/to/deployment/directory [BuildType]"
  echo "  BuildType can be 'Debug', 'Release', 'RelWithDebInfo', or 'DebugASan'."
  echo "Error: Deployment directory argument is required."
  exit 1
fi
DEPLOY_PATH="$1"
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
    echo "--- Handling ASan and LLVM Runtime Dependencies ---"

    # --- THIS IS THE FIX ---
    # Find the directory where Quake II.exe lives, which is one level above the mod path.
    GAME_ROOT_DIR=$(dirname "$DEPLOY_PATH")
    echo "Game root directory identified as: $GAME_ROOT_DIR"

    LLVM_TARGET_BIN_DIR="$TOOLCHAIN_ROOT/x86_64-w64-mingw32/bin"
    REQUIRED_DLLS=(
        "libclang_rt.asan_dynamic-x86_64.dll"
        "libc++.dll"
        "libunwind.dll"
    )

    for DLL_NAME in "${REQUIRED_DLLS[@]}"; do
        DLL_PATH="$LLVM_TARGET_BIN_DIR/$DLL_NAME"
        if [ -f "$DLL_PATH" ]; then
            echo "Copying $DLL_NAME to game root directory..."
            cp "$DLL_PATH" "$GAME_ROOT_DIR/"
        else
            echo "!!! CRITICAL WARNING: Could not find required runtime DLL: $DLL_NAME"
            exit 1
        fi
    done
    echo "All required runtime DLLs copied successfully."
fi

exit 0