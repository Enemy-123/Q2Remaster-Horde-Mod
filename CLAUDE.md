### Build Command
```bash
python3 /home/perrobjorn/Documents/Repo/Q2Remaster-Horde-Mod/build_mingw.py '/home/perrobjorn/.steam/steam/steamapps/common/Quake 2/rerelease/baseq2/' Release
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

Small, Iterative Changes

Work in small, testable increments - implement, test with human in the loop, then continue
Make the smallest reasonable changes to achieve the desired outcome
Break down work into small, iterable, testable chunks
Always discuss plans before implementation unless explicitly told otherwise


Best Practices

Help me become a better coder by explaining the "why" behind your implementation choices
Use idiomatic coding patterns for each language - always confirm you're following language-specific best practices
Use tldr tool when you are trying to figure out the syntax of a 3rd party tool