### Build Command
```bash
python3 /home/perrobjorn/Documents/Repo/Q2Remaster-Horde-Mod/build_mingw.py '/home/perrobjorn/Games/Heroic/Quake II Enhanced/baseq2' Release
```

### Debug Command (Automated Debugging)
One-terminal workflow: builds mod and launches winedbg with auto-configured GDB
```bash
# Auto-build and debug (requires: pip install pexpect)
python3 /home/perrobjorn/Documents/Repo/Q2Remaster-Horde-Mod/debug.py

# Debug with UndefinedBehaviorSanitizer
python3 /home/perrobjorn/Documents/Repo/Q2Remaster-Horde-Mod/debug.py --sanitizer ubsan

# Skip build, just launch debugger
python3 /home/perrobjorn/Documents/Repo/Q2Remaster-Horde-Mod/debug.py --skip-build

# Manual mode (no pexpect needed, prints GDB commands to enter manually)
python3 /home/perrobjorn/Documents/Repo/Q2Remaster-Horde-Mod/debug.py --no-auto
```
When game crashes, type `bt` at Wine-gdb> prompt for backtrace.

### Function Macros
Forward declarations are plain C++, implementations use macros:
```cpp
void my_function(edict_t* self);  // declaration
THINK(my_function)(edict_t* self) -> void { ... }  // implementation
```
**Macros:** `THINK`, `TOUCH`, `USE`, `MONSTERINFO_ATTACK`, `DIE`

### Print Functions
Use `{}` placeholders, NOT `%s`/`%d`:
```cpp
gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Player {}\n", name);  // CORRECT
gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Player %s\n", name);  // WRONG
```
- for debug we dont use locclient, we use gi.Com_PrintFmt {}
