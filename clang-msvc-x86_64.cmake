# clang-msvc-x86_64.cmake
# Cross-compilation toolchain file using Clang with MSVC ABI compatibility
# This provides better Windows compatibility than MinGW while building on Linux

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Use clang/clang++ with Windows MSVC target
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Set target triple for Windows MSVC ABI
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# Set compiler flags for MSVC compatibility
set(CMAKE_C_FLAGS_INIT "-target x86_64-pc-windows-msvc -fms-extensions -fms-compatibility")
set(CMAKE_CXX_FLAGS_INIT "-target x86_64-pc-windows-msvc -fms-extensions -fms-compatibility")

# Use LLD as the linker for Windows PE format
set(CMAKE_LINKER "ld.lld")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")

# Set the resource compiler for Windows
find_program(CMAKE_RC_COMPILER NAMES llvm-rc windres)

# Configure for shared library (DLL) output
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")
set(CMAKE_IMPORT_LIBRARY_SUFFIX ".lib")

# Set find root path modes for cross-compilation
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Ensure CMAKE_MAKE_PROGRAM is set
find_program(CMAKE_MAKE_PROGRAM NAMES make gmake nmake REQUIRED)