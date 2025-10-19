# clang-w64-x86_64.cmake
# Clang cross-compilation toolchain for Windows x86_64 using MinGW

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Target Windows x86_64 using MinGW
set(MINGW_TARGET x86_64-w64-mingw32)

# Use clang/clang++ with MinGW target
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_RC_COMPILER /usr/bin/${MINGW_TARGET}-windres)

# Critical: Set the target for all compiler invocations
set(CMAKE_C_COMPILER_TARGET ${MINGW_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${MINGW_TARGET})

# Find GCC library directory and include paths for MinGW
execute_process(
    COMMAND ${MINGW_TARGET}-gcc -print-libgcc-file-name
    OUTPUT_VARIABLE LIBGCC_FILE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
get_filename_component(GCC_LIB_DIR ${LIBGCC_FILE} DIRECTORY)

# Get GCC version for include paths
execute_process(
    COMMAND ${MINGW_TARGET}-gcc -dumpversion
    OUTPUT_VARIABLE GCC_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Set up C++ include paths
set(MINGW_INCLUDE_PATH "/usr/${MINGW_TARGET}/include")
set(MINGW_CXX_INCLUDE_PATH "/usr/${MINGW_TARGET}/include/c++/${GCC_VERSION}")
set(MINGW_CXX_INCLUDE_BACKWARD_PATH "${MINGW_CXX_INCLUDE_PATH}/backward")
set(MINGW_CXX_INCLUDE_TARGET_PATH "${MINGW_CXX_INCLUDE_PATH}/${MINGW_TARGET}")

# Compiler flags for Windows compatibility - use MinGW's libstdc++
set(CMAKE_C_FLAGS_INIT "-DWINVER=0x0601 -D_WIN32_WINNT=0x0601")
set(CMAKE_CXX_FLAGS_INIT "-fexceptions -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 -isystem ${MINGW_CXX_INCLUDE_PATH} -isystem ${MINGW_CXX_INCLUDE_TARGET_PATH} -isystem ${MINGW_CXX_INCLUDE_BACKWARD_PATH} -isystem ${MINGW_INCLUDE_PATH}")

# Linker flags - use lld and MinGW runtime libraries
# Use -fuse-ld=lld to use LLVM's linker instead of GNU ld
# Use --rtlib=compiler-rt and -lunwind or use GCC's runtime with proper paths
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -Wl,-Bstatic,-lstdc++,-Bdynamic -L${GCC_LIB_DIR} -lpthread")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld -Wl,-Bstatic,-lstdc++,-Bdynamic -L${GCC_LIB_DIR} -lpthread")

# Set sysroot and paths for MinGW libraries
set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_TARGET})
set(CMAKE_SYSROOT /usr/${MINGW_TARGET})

# Set find root path modes for cross-compilation
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
