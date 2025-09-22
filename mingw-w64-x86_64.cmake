# mingw-w64-x86_64.cmake
# Enhanced MinGW cross-compilation toolchain with better Windows compatibility

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Point to the standard system cross-compilers.
set(CMAKE_C_COMPILER   /usr/bin/x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  /usr/bin/x86_64-w64-mingw32-windres)

# Enhanced compiler flags for better Windows compatibility
set(CMAKE_C_FLAGS_INIT "-mthreads -DWINVER=0x0601 -D_WIN32_WINNT=0x0601")
set(CMAKE_CXX_FLAGS_INIT "-mthreads -fexceptions -DWINVER=0x0601 -D_WIN32_WINNT=0x0601")

# Enhanced linker flags for modern Windows features
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")

# Set find root path modes for cross-compilation
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)