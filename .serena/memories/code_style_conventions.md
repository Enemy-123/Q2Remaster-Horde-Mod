# Code Style and Conventions

## Naming Conventions
- **Variables and Functions**: `snake_case`
  - Example: `g_horde_state`, `CalculateRemainingMonsters()`
- **Types and Classes**: `PascalCase`
  - Example: `MonsterTypeInfo`, `PlayerStats`, `HordeState`
- **Enums**: `PascalCase` with `SCREAMING_CASE` values
  - Example: `enum class MonsterWaveType { None, Flying, Ground }`
- **Constants**: `SCREAMING_CASE` or `snake_case` with `constexpr`
  - Example: `MAX_SPLIT_PLAYERS`, `g_num_spawn_points`
- **Global Variables**: Prefix with `g_`
  - Example: `g_horde`, `g_spawn_point_list`

## Type Declarations
- **Prefer `using` over `typedef`**:
  ```cpp
  using gvec3_t = float[3];  // Good
  typedef float gvec3_t[3];  // Avoid
  ```

## Function Macros (Game Callbacks)
Forward declarations are plain C++, but implementations use special macros:

```cpp
// Declaration (in header)
void my_function(edict_t* self);

// Implementation (in source)
THINK(my_function)(edict_t* self) -> void {
    // Implementation
}
```

**Available Macros:**
- `THINK` - Think callbacks for entity AI
- `TOUCH` - Touch/collision callbacks
- `USE` - Use/activation callbacks
- `MONSTERINFO_ATTACK` - Monster attack callbacks
- `DIE` - Death callbacks

## Formatting & Printing

### String Formatting
**ALWAYS use `{}` placeholders, NEVER use `%s`/`%d` format specifiers:**

```cpp
// CORRECT - Use {} placeholders
gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Player {}\n", name);
gi.Com_PrintFmt("Wave {}, Monsters: {}\n", wave, count);

// WRONG - Don't use printf-style
gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Player %s\n", name);  // ❌
```

### Debug Printing
For debug output, use `gi.Com_PrintFmt` with `{}` placeholders:
```cpp
gi.Com_PrintFmt("Debug: value = {}\n", value);
```

## Modern C++ Features

### Use Modern Containers
- `std::vector` for dynamic arrays
- `std::array` for fixed-size arrays
- `boost::container::flat_map` for sorted maps (better cache locality)
- `boost::container::flat_set` for sorted sets
- `std::unordered_map` / `std::unordered_set` for hash-based containers

### Use Modern Language Features
- `constexpr` for compile-time constants
- `auto` for type deduction
- Range-based for loops
- `std::span` for non-owning array views
- Structured bindings
- Concepts (where applicable)

## File Organization

### Include Order
1. Related header file (if .cpp)
2. Project headers (relative paths)
3. Standard library headers
4. Third-party library headers

Example:
```cpp
#include "../shared.h"
#include "g_horde.h"
#include <vector>
#include <boost/container/flat_set.hpp>
```

### Header Guards
Use `#pragma once` (preferred over include guards in this codebase)

## Comments
- Prefer English for comments (some Spanish comments exist but English is standard)
- Use `//` for single-line comments
- Use `/* */` for multi-line comments
- Document complex algorithms and non-obvious behavior

## Memory Management
- Prefer RAII and smart pointers where applicable
- Be mindful of game engine memory constraints
- Use `memory_safety.h` utilities for bounds checking

## Error Handling
- Return error codes or use exceptions sparingly (game engine constraints)
- Validate inputs, especially from game engine callbacks
- Check pointers before dereferencing
