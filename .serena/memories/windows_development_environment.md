# Windows Development Environment

## Development Platform
- **OS**: Windows
- **IDE**: Microsoft Visual Studio Community 2022
- **Platform Toolset**: v143
- **Target**: Windows SDK 10.0

## Directory Paths

### Project Location
```
C:\Users\pipev\Documents\Repos\Enemy-123\Q2Remaster-Horde-Mod\
```

### Game Installation
```
C:\Program Files (x86)\GOG Galaxy\Games\Quake II Enhanced\
```

### Build Output
- **Debug**: `build\debug\baseq2\game_x64.dll`
- **Release**: Directly to game installation `baseq2\game_x64.dll`

## Windows-Specific Considerations

### File Paths
- Use backslashes `\` or forward slashes `/` (both work in most contexts)
- Paths with spaces must be quoted in command line
- Case-insensitive file system (but maintain consistent casing)

### Line Endings
- **Git Setting**: `core.autocrlf = true` ✓ (already configured)
- This converts LF to CRLF on checkout, CRLF to LF on commit
- Prevents massive diffs due to line ending changes

### Command Line Tools

#### PowerShell (Recommended)
```powershell
# List files
Get-ChildItem
dir

# Search files
Get-ChildItem -Recurse -Filter "*.cpp"

# Search in files
Select-String -Path "*.cpp" -Pattern "pattern"

# Change directory
cd path\to\directory

# Copy files
Copy-Item source destination

# Remove files
Remove-Item path -Recurse -Force
```

#### Command Prompt (cmd.exe)
```batch
dir         # List files
cd          # Change directory
copy        # Copy files
del         # Delete files
type        # Display file contents
find        # Search in files (limited)
```

### Equivalent Commands (Linux → Windows)

| Linux | Windows PowerShell | Windows CMD |
|-------|-------------------|-------------|
| ls | Get-ChildItem, dir | dir |
| cd | Set-Location, cd | cd |
| pwd | Get-Location, pwd | cd |
| cat | Get-Content, type | type |
| grep | Select-String | find, findstr |
| cp | Copy-Item, copy | copy |
| mv | Move-Item, move | move |
| rm | Remove-Item, del | del |
| mkdir | New-Item -Type Directory, mkdir | mkdir |
| rm -rf | Remove-Item -Recurse -Force | rmdir /s /q |

## Visual Studio Integration

### vcpkg Integration
vcpkg is integrated with Visual Studio automatically:
- Dependencies defined in `vcpkg.json`
- Manifest mode enabled
- Static linking configured (`VcpkgUseStatic=true`)

### Post-Build Events
Automatically deploys files after successful build:
- Copies `deploy/bots/` to game directory
- Copies `deploy/ents/` to game directory
- Copies `deploy/horde_config.json` to game directory
- Uses `robocopy` with `/XO` flag (only newer files)

### Build Configurations

#### Debug
- Optimization: Disabled
- Runtime: MultiThreadedDebug (static)
- Symbols: Full debug info
- Output: Local build directory
- ASAN: Disabled (can be enabled for address sanitizer)

#### Release
- Optimization: Full (`/O2`)
- Runtime: MultiThreaded (static)
- Symbols: Limited
- Output: Game installation directory
- Whole Program Optimization: Enabled

## File System Considerations

### Case Sensitivity
- Windows file system is case-insensitive
- But maintain consistent casing for cross-platform compatibility
- Example: `g_horde.h` not `G_HORDE.h`

### Reserved Names
Avoid these reserved filenames on Windows:
- CON, PRN, AUX, NUL
- COM1-COM9, LPT1-LPT9

### Path Length
- Traditional limit: 260 characters
- Long path support may be needed for deep hierarchies
- Keep paths reasonably short

## Debugging on Windows

### Visual Studio Debugger
- Integrated debugger is the primary debugging tool
- Set breakpoints with F9
- Start debugging with F5
- Step over: F10, Step into: F11

### Debug Output
- Use `gi.Com_PrintFmt` for debug logging
- Output visible in Quake II console
- Can also use `OutputDebugString` for IDE output window

### Crash Debugging
- Visual Studio can attach to crashes automatically
- Configure "Just-In-Time Debugging"
- Minidumps can be generated for post-mortem analysis

## Performance Profiling

### Visual Studio Profiler
- Available in VS 2022
- Performance Profiler: Debug → Performance Profiler
- CPU Usage, Memory Usage, etc.

### Custom Profiler
The project includes `profiler.h/cpp` for in-game profiling

## Environment Variables

### Useful for Development
```powershell
# Add to PATH if needed
$env:PATH += ";C:\path\to\tools"

# Set vcpkg root (usually automatic)
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
```

## Windows Security & Permissions

### UAC (User Account Control)
- Game installation in `Program Files (x86)` requires admin rights
- Visual Studio may need to run as administrator for Release builds
- Consider using a non-protected directory for development builds

### Antivirus
- Exclude project directory from real-time scanning for faster builds
- Game mods may trigger false positives

## Notes

### Why Not Using MinGW Build Currently
- Visual Studio provides better debugging experience on Windows
- Integrated IDE features
- Native Windows toolchain
- Better integration with Windows APIs

The MinGW build system (build_mingw.py) is still available for:
- Cross-compilation from Linux
- Alternative build system if needed
- CI/CD pipelines
