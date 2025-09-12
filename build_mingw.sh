#!/bin/bash
set -e
set -u
set -o pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
BUILD_DIR="$SCRIPT_DIR/build"

# --- TOOLCHAIN SETUP ---
MINGW_TOOLCHAIN_FILE="$SCRIPT_DIR/mingw-w64-x86_64.cmake"
VCPKG_TOOLCHAIN_FILE="$SCRIPT_DIR/vcpkg/scripts/buildsystems/vcpkg.cmake"

# --- FAKE POWERSHELL WORKAROUND --- ( is this needed ? )
# Create a fake bin directory to hold our fake powershell
mkdir -p "$SCRIPT_DIR/fake_bin"
# Create a fake powershell.exe that does nothing and exits successfully
echo '#!/bin/bash' > "$SCRIPT_DIR/fake_bin/powershell.exe"
echo 'exit 0' >> "$SCRIPT_DIR/fake_bin/powershell.exe"
# Make it executable
chmod +x "$SCRIPT_DIR/fake_bin/powershell.exe"
# Add our fake bin directory to the front of the PATH
# This makes the build system find our fake one first.
export PATH="$SCRIPT_DIR/fake_bin:$PATH"
# --- END WORKAROUND ---

DEPLOY_PATH="$1"
BUILD_TYPE="${2:-Release}"
echo "--- Build Type set to: $BUILD_TYPE ---"

if [ ! -f "$MINGW_TOOLCHAIN_FILE" ]; then
    echo "Error: MinGW Toolchain file not found at $MINGW_TOOLCHAIN_FILE"
    exit 1
fi
if [ ! -f "$VCPKG_TOOLCHAIN_FILE" ]; then
    echo "Error: vcpkg Toolchain file not found at $VCPKG_TOOLCHAIN_FILE"
    exit 1
fi
if [ ! -d "$DEPLOY_PATH" ]; then
    echo "Warning: Deployment directory does not exist: $DEPLOY_PATH"
    echo "Creating it..."
    mkdir -p "$DEPLOY_PATH"
fi

echo "--- Starting GCC MinGW Cross-Compile Build ---"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"
cmake .. \
    -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DDEPLOY_DIRECTORY="$DEPLOY_PATH" \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN_FILE" \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$MINGW_TOOLCHAIN_FILE" \
    -DVCPKG_TARGET_TRIPLET=x64-mingw-static

cmake --build . -- -j$(nproc)
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

# --- GCC RUNTIME DEPENDENCIES ---
echo ""
echo "--- Handling GCC Runtime Dependencies ---"
GAME_ROOT_DIR=$(dirname "$DEPLOY_PATH")
REQUIRED_DLLS=(
    "libgcc_s_seh-1.dll"
    "libstdc++-6.dll"
)

GCC_LIB_DIR=$(dirname $(find /usr/lib/gcc/x86_64-w64-mingw32 -name "libgcc_s_seh-1.dll" | head -n 1))

if [ -z "$GCC_LIB_DIR" ] || [ ! -d "$GCC_LIB_DIR" ]; then
    echo "!!! CRITICAL ERROR: Could not find the GCC MinGW runtime library directory."
    exit 1
fi

for DLL_NAME in "${REQUIRED_DLLS[@]}"; do
    DLL_PATH="$GCC_LIB_DIR/$DLL_NAME"
    if [ -f "$DLL_PATH" ]; then
        echo "Found $DLL_NAME, copying to game root directory..."
        cp "$DLL_PATH" "$GAME_ROOT_DIR/"
    else
        echo "!!! CRITICAL WARNING: Could not find required runtime DLL: $DLL_NAME"
        echo "Searched in: $GCC_LIB_DIR"
        exit 1
    fi
done

echo "All required runtime DLLs copied successfully."
exit 0