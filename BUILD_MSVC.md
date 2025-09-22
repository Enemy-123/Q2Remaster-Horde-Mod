# MSVC-Compatible Build Setup

This document explains how to build the Q2 Horde Mod with MSVC ABI compatibility instead of MinGW, which should resolve Windows-specific bugs.

## Problem with MinGW

MinGW can produce DLLs with subtle ABI incompatibilities when used with Steam Proton, leading to:
- Memory alignment issues
- Exception handling problems
- Standard library incompatibilities
- Runtime crashes that don't occur with native MSVC builds

## Solution: Clang with MSVC ABI

We can use Clang's MSVC target to produce Windows-compatible binaries while cross-compiling on Linux.

## Prerequisites

Install required packages on Arch Linux:

```bash
sudo pacman -S clang lld llvm
```

## Usage

### Automatic Compiler Selection (Recommended)

```bash
./build_universal.py /path/to/game/baseq2 Release
```

This will automatically:
1. Try Clang with MSVC ABI first (recommended)
2. Fall back to MinGW if Clang is not available
3. Show which compiler is being used

### Force Specific Compiler

```bash
# Force MSVC-compatible build
./build_universal.py /path/to/game/baseq2 Release --compiler clang-msvc

# Force MinGW build (original method)
./build_universal.py /path/to/game/baseq2 Release --compiler mingw
```

### Build Types

- `Release` - Optimized build for production
- `Debug` - Debug build with symbols
- `RelWithDebInfo` - Optimized with debug symbols

## Files Created

### New Build Scripts
- `build_universal.py` - Universal build script with compiler auto-detection
- `build_msvc.py` - MSVC-only build script
- `clang-msvc-x86_64.cmake` - Clang MSVC toolchain file
- `CMakeLists_msvc.txt` - MSVC-optimized CMake configuration

### Original Files (unchanged)
- `build.py` - Original MinGW build script
- `mingw-w64-x86_64.cmake` - MinGW toolchain file
- `CMakeLists.txt` - Original CMake configuration

## Technical Details

### Clang MSVC Target
- Uses `x86_64-pc-windows-msvc` target triple
- Employs `lld-link` for MSVC-compatible linking
- Applies MSVC-style command line options via clang-cl
- Links against Windows static libraries from vcpkg

### Benefits Over MinGW
- **Better ABI compatibility** with Windows/Steam Proton
- **Proper exception handling** using Windows SEH
- **Native Windows calling conventions**
- **Compatible standard library** implementation
- **Reduced runtime dependencies** (no libwinpthread-1.dll needed)

### Compatibility
- Should work identically to native MSVC builds
- Better integration with Windows debugging tools
- Proper stack unwinding and crash reporting
- Compatible with Windows profilers and analyzers

## Troubleshooting

### Missing Tools Error
```
Error: Clang with MSVC target not available!
Install with: sudo pacman -S clang lld llvm
```

**Solution:** Install the required packages as shown.

### vcpkg Issues
The build system includes a PowerShell workaround for vcpkg on Linux. If you encounter vcpkg-related errors, ensure the `vcpkg` directory exists and is properly initialized.

### Build Failures
If the MSVC-compatible build fails, you can fall back to MinGW:
```bash
./build_universal.py /path/to/game/baseq2 Release --compiler mingw
```

## Testing

After building with the MSVC-compatible toolchain:

1. **Test basic functionality** - Ensure the mod loads and runs
2. **Test problematic features** - Check areas that had MinGW-specific bugs
3. **Monitor stability** - Look for reduced crashes compared to MinGW builds
4. **Check performance** - MSVC builds may have different performance characteristics

## Migration from MinGW

To switch from MinGW to MSVC-compatible builds:

1. Use the new `build_universal.py` script instead of `build.py`
2. The generated DLL should be drop-in compatible
3. Remove `libwinpthread-1.dll` if present (not needed for MSVC builds)
4. Test thoroughly for improved stability

## Future Improvements

- Consider setting up Wine + native MSVC for 100% compatibility
- Add support for Visual Studio Build Tools via Wine
- Implement automated testing to compare MinGW vs MSVC builds