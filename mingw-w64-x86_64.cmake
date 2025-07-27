# Toolchain file for LLVM-MinGW - Direct Invocation
# This calls the real compiler executable and sets the target manually.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_ROOT "/home/perrobjorn/tools/llvm-mingw-20250709-ucrt-ubuntu-22.04-x86_64")

# Set the target triple for cross-compilation
set(TARGET_TRIPLE "x86_64-w64-windows-gnu")

# Point to the REAL compiler executable (clang-20) and pass the target flag.
# We put the flag in CMAKE_..._FLAGS_INIT to ensure it's used correctly.
set(CMAKE_C_COMPILER   "${TOOLCHAIN_ROOT}/bin/clang-20")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/clang-20")
set(CMAKE_C_FLAGS_INIT "--target=${TARGET_TRIPLE}")

# --- THIS IS THE FIX ---
# Add the flag to link the C++ standard library
set(CMAKE_CXX_FLAGS_INIT "--target=${TARGET_TRIPLE} -lc++")

# The resource compiler is a direct executable, not a script.
set(CMAKE_RC_COMPILER  "${TOOLCHAIN_ROOT}/bin/x86_64-w64-mingw32-windres")

# Set the sysroot to help the compiler find its own libraries
set(CMAKE_SYSROOT "${TOOLCHAIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)