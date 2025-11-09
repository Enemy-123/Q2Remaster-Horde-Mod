# Technology Stack

## Language
- **C++23**: Modern C++ with latest features (std::span, ranges, concepts, etc.)

## Build System
- **Primary**: Microsoft Visual Studio Community 2022 (v143 toolset)
- **Alternative**: CMake with MinGW (documented but not currently in use)
- **Package Manager**: vcpkg for dependency management

## Dependencies (via vcpkg)
- **fmt**: Modern C++ formatting library (header-only mode)
- **JsonCpp**: JSON parsing and serialization
- **Boost**: Container libraries (flat_map, flat_set)
  - Used for performance-critical data structures

## Compilation Settings
- **Standard**: C++23 (`/std:c++23`)
- **Runtime Library**: 
  - Debug: MultiThreadedDebug (static)
  - Release: MultiThreaded (static)
- **Character Set**: Unicode
- **Preprocessor Defines**:
  - `FMT_HEADER_ONLY`
  - `KEX_Q2_GAME`
  - `KEX_Q2GAME_EXPORTS`
  - `KEX_Q2GAME_DYNAMIC`
  - `_CRT_SECURE_NO_WARNINGS`
  - `NOMINMAX` (prevents min/max macro conflicts)
  - `WIN32_LEAN_AND_MEAN` (reduces Windows header bloat)

## Build Configurations
- **Debug**: Builds to `build\debug\baseq2\game_x64.dll`
- **Release**: Builds directly to `C:\Program Files (x86)\GOG Galaxy\Games\Quake II Enhanced\baseq2\game_x64.dll`

## Version Control
- **Git**: Repository with .gitignore for build artifacts
- **Line Endings**: `core.autocrlf = true` (Windows compatibility)

## Python Scripts
- **build_mingw.py**: Alternative MinGW build system (not currently used)
- **debug.py**: Automated debugging with winedbg/GDB (Linux-focused, not used on Windows)
- **check_damage_fallbacks.py**: Code analysis tool
- **fire.py**: Testing/analysis script
- **scriptutf.py**: UTF-8 script utilities
