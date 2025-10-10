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

## Adding Skills/Abilities to Upgrade Menu

When adding a new skill/ability with progress (multi-level), follow this pattern:

### 1. Add Configuration (g_config.h + g_config.cpp)
```cpp
// In g_config.h - Add config struct
struct FireballConfig {
    int initial_damage = 50;
    int addon_damage = 25;
    int initial_radius = 100;
    float addon_radius = 2.5f;  // Use float for decimal values
    int initial_speed = 650;
    int addon_speed = 35;
};

// Add to GameConfig struct
struct GameConfig {
    // ... other configs
    FireballConfig fireball;
};
```

```cpp
// In g_config.cpp - Add JSON loading under special_abilities
if (abilities.isMember("fireball") && abilities["fireball"].isObject()) {
    const Json::Value& f = abilities["fireball"];
    g_config.fireball.initial_damage = GetJsonInt(f, "initial_damage", 50);
    g_config.fireball.addon_damage = GetJsonInt(f, "addon_damage", 25);
    g_config.fireball.initial_radius = GetJsonInt(f, "initial_radius", 100);
    g_config.fireball.addon_radius = GetJsonFloat(f, "addon_radius", 2.5f);
    g_config.fireball.initial_speed = GetJsonInt(f, "initial_speed", 650);
    g_config.fireball.addon_speed = GetJsonInt(f, "addon_speed", 35);
}
```

### 2. Add Player Skill Field (g_local.h)
```cpp
// In player_skills_t struct
struct player_skills_t {
    // ... other skills
    int8_t fireball = 0;  // 0-10: Single field for multi-property skill
};
```

### 3. Add Upgrade Definition (horde/g_upgrades.cpp)
```cpp
// In UPGRADE_DEFS array
{
    "fireball",                    // Unique ID
    "Fireball",                    // Display name
    "Throws a fireball\n"          // Multi-line description
    ""
    ""
    ""
    "",
    10, 1,                         // max_level, cost_per_level
    UpgradeCategory::ABILITY,      // Category
    nullptr, 0                     // No prerequisites
}
```

### 4. Update Skill Management Functions (horde/g_upgrades.cpp)
```cpp
// GetSkillLevel - Add case
else if (strcmp(upgrade_id, "fireball") == 0)
    return player->client->pers.skills.fireball;

// UpgradeSkill - Add case
else if (strcmp(upgrade_id, "fireball") == 0) {
    player->client->pers.skills.fireball++;
}

// ResetAllSkills - Add to total_points and reset logic
total_points += player->client->pers.skills.fireball;
// ... later in reset section
player->client->pers.skills.fireball = 0;
```

### 5. Implement Command/Function Using Config Values (g_cmds.cpp)
```cpp
void Cmd_Fireball_f(edict_t* ent) {
	// Get player's fireball skill level
	int8_t fireball_level = ent->client->pers.skills.fireball;

	// Check if player has the fireball skill
	if (fireball_level == 0)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "You need to upgrade the Fireball skill first!\n");
		return;
	}

    // Calculate values using config + skill level
    int damage = g_config.fireball.initial_damage +
                 (fireball_level * g_config.fireball.addon_damage);

    float damage_radius = static_cast<float>(
        g_config.fireball.initial_radius +
        (fireball_level * g_config.fireball.addon_radius));

    int speed = g_config.fireball.initial_speed +
                (fireball_level * g_config.fireball.addon_speed);

    // Use calculated values
    fire_fireball(ent, start, aimdir, damage, damage_radius, speed, flames, flame_damage);
}

// Register command in ClientCommand()
else if (Q_strcasecmp(cmd, "fireball") == 0)
    Cmd_Fireball_f(ent);
```

//then dont forget to edit character.cpp to add fireball saving into characters

### Menu Display
**MenuFormatItemWithProgress** automatically formats multi-level abilities with `[X/MAX]` progress indicator when the menu reads from `GetUpgradeDefinitions()`. No extra menu code needed - the system handles it automatically in `horde/horde_menu.cpp::CreateAbilitiesMenu()`.

### Key Points
- Use **single skill field** for abilities that upgrade multiple properties together
- Always use **g_config values** with skill level scaling: `initial + (level * addon)`
- Use `GetJsonFloat()` for decimal config values, `GetJsonInt()` for integers
- Description should explain base values and per-level bonuses
- Command functions must read player's skill level and apply scaling

