# Codebase Structure

## Directory Layout

```
Q2Remaster-Horde-Mod/
├── .claude/              # Claude Code configuration
├── .git/                 # Git repository
├── .serena/              # Serena MCP server data
│   ├── cache/            # Language server cache
│   └── memories/         # Project memories
├── .vs/                  # Visual Studio cache
├── .vscode/              # VS Code configuration
├── bots/                 # Bot AI implementation
├── build-windows/        # Windows build output (not used)
├── build_mingw/          # MinGW build output (not used)
├── build/                # Visual Studio build output
│   └── debug/baseq2/     # Debug builds
├── ctf/                  # Capture The Flag mode
├── deploy/               # Deployment files
│   ├── bots/             # Bot configuration files
│   ├── config/           # Game configuration
│   │   └── weapon_and_bonus.json
│   ├── ents/             # Entity configurations
│   ├── horde.cfg         # Horde mode config
│   └── horde_config.json # Horde settings
├── game/                 # Game DLL source (alternative structure, unused)
├── horde/                # **Horde mode implementation (core mod code)**
├── out/                  # Build artifacts
├── rogue/                # Rogue mission pack
├── test/                 # Test files
├── vcpkg_installed/      # vcpkg dependencies
├── xatrix/               # Xatrix mission pack
└── [root source files]   # Core game source (g_*, m_*, p_*)
```

## Core Directories

### Root Directory
Contains base game implementation files:
- **g_*.cpp/h**: Game logic (spawn, combat, AI, items, etc.)
- **m_*.cpp/h**: Monster implementations (each monster type)
- **p_*.cpp/h**: Player code (client, movement, weapons, view)
- **cg_*.cpp/h**: Client-side game code
- **bg_*.h**: Background/shared definitions

### horde/
**The main horde mode implementation:**
- `g_horde.cpp/h` - Core horde mode logic, state management
- `g_horde_scaling.cpp/h` - Difficulty and damage scaling
- `g_horde_benefits.cpp/h` - Player upgrades and benefits
- `g_horde_phys.cpp/h` - Horde-specific physics
- `horde_spawning.cpp/h` - Monster spawning system
- `horde_boss.cpp/h` - Boss battle mechanics
- `horde_monster_data.cpp/h` - Monster statistics and configuration
- `horde_ids.cpp/h` - Type identifiers for monsters/items
- `horde_menu.cpp` - In-game menu system
- `horde_constants.h` - Game constants
- `g_upgrades.cpp/h` - Player upgrade system
- `g_pvm.cpp/h` - Player vs Monster mode
- `g_pvm_menu.cpp/h` - PvM menu interface
- `g_character.cpp/h` - Character system
- `weapon_id.cpp/h` - Weapon identification
- Entity utilities: `g_barrel.cpp`, `g_bombspell.cpp`, `g_fire.cpp`, `g_laser.cpp/h`, `g_strogg_summoner.cpp`, `g_teleport.cpp`, `g_tesla.cpp`, `g_traps.cpp`
- Morph abilities: `p_brain_morph.cpp/h`, `p_flyer_morph.cpp/h`
- Special items: `g_doppleganger.cpp`, `g_idview.cpp`
- Helpers: `g_entity_properties.cpp/h`, `menu_helpers.h`, `horde_performance.h`

### bots/
Bot AI implementation for the game

### ctf/
Capture The Flag game mode

### rogue/ and xatrix/
Mission pack expansions (Ground Zero and The Reckoning)

### deploy/
Files that get deployed alongside the DLL:
- Bot configurations
- Entity definitions
- Weapon and item configurations
- Horde mode settings

## Key Files

### Build Configuration
- `Q2HordeRemaster.sln` - Visual Studio solution
- `game.vcxproj` - Visual Studio project file
- `CMakeLists.txt` - CMake build (alternative, not used)
- `vcpkg.json` - Package dependencies

### Python Build Scripts (Alternative System)
- `build_mingw.py` - MinGW cross-compilation (Linux)
- `build_clang.py` - Clang build script
- `debug.py` - Automated debugging setup

### Core Headers
- `game.h` - Main game API and types
- `g_local.h` - Local game definitions
- `shared.h` - Shared code between game and engine
- `monster_constants.h` - Monster-related constants
- `memory_safety.h` - Memory safety utilities
- `profiler.h/cpp` - Performance profiling

### Utility Files
- `q_std.cpp/h` - Standard utility functions
- `q_vec3.h` - Vector mathematics
- `hook.cpp` - Hook system
- `g_utils.cpp` - General utilities

### Documentation
- `CLAUDE.md` - Development instructions for Claude Code
- `monsters_attack_analysis.md` - Monster weapon analysis
- `oldupgrade.md` - Legacy upgrade documentation
- `LICENSE.txt` - GNU GPL 2.0 license
- `fire_results.txt` - Fire mechanism test results

## Monster Files (m_*.cpp/h)
Each monster type has its own implementation file:
- `m_soldier.cpp/h` - Soldier monsters
- `m_tank.cpp/h` - Tank monsters
- `m_gladiator.cpp/h` - Gladiator
- `m_gunner.cpp/h` - Gunner
- `m_berserk.cpp/h` - Berserk
- `m_brain.cpp/h` - Brain
- `m_chick.cpp/h` - Iron Maiden
- `m_flyer.cpp/h` - Flyer
- `m_hover.cpp/h` - Hover
- `m_medic.cpp/h` - Medic
- `m_parasite.cpp/h` - Parasite
- `m_mutant.cpp/h` - Mutant
- `m_arachnid.cpp/h` - Arachnid
- `m_boss2/3/31/32.cpp/h` - Various bosses
- `m_guardian.cpp/h` - Guardian
- `m_shambler.cpp/h` - Shambler
- And many more...

## Code Organization Patterns

### Monster Implementation
Monsters follow a standard pattern:
1. AI think functions
2. Attack functions
3. Pain/death functions
4. Animation sequences
5. Spawn function

### Horde Mode Flow
1. **Init** (`Horde_PreInit`, `Horde_Init`) - Setup
2. **Warmup** - Preparation phase
3. **Spawning** - Monster spawning phase
4. **Active Wave** - Combat phase
5. **Cleanup** - Remove corpses
6. **Rest** - Brief pause before next wave

### Naming Patterns
- Functions that start with `G_` are global game functions
- Functions that start with `Horde_` are horde-specific
- Monster functions typically start with monster name (e.g., `soldier_`, `tank_`)
- Callbacks use macros: `THINK()`, `TOUCH()`, `USE()`, `DIE()`
