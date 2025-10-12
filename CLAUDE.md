### Build Command
```bash
python3 /home/perrobjorn/Documents/Repo/Q2Remaster-Horde-Mod/build_mingw.py '/home/perrobjorn/Games/Heroic/Quake II Enhanced/baseq2' Release
```

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
