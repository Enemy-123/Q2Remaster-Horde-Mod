#pragma once

#include <cstdint>
#include <boost/container/flat_map.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <string>
#include <array>
#include "horde/weapon_id.h"  // Need full definition for WeaponID::UNKNOWN in set() methods

// Forward declarations
namespace horde {
	struct MapSize;
	enum class MapID : uint16_t;
}

// Configuration for entity limits
struct EntityLimitsConfig
{
	int32_t max_sentries = 3;
	int32_t max_lasers = 6;
	int32_t max_teslas = 11;
	int32_t max_barrels = 4;
	int32_t max_prox = 12;
	int32_t max_traps = 8;
	int32_t max_summons = 3;
};

// Base weapon configurations
struct BlasterConfig
{
	int damage_min = 12;
	int damage_max = 16;
	int speed = 1200;
	int bounces = 5;
	int speed_addon = 40;  // Speed added per upgrade level
};

struct HyperBlasterConfig
{
	int damage_min = 12;
	int damage_max = 14;
	int speed = 1700;
	int bounces = 3;
	int speed_addon = 40;  // Speed added per upgrade level
};

struct ShotgunConfig
{
	int damage_min = 3;
	int damage_max = 5;
	int damage_energy_min = 7;
	int damage_energy_max = 11;
	int kick = 8;
	int pellet_count_deathmatch = 12;
	int pellet_count_normal = 18;
};

struct SuperShotgunConfig
{
	int damage_min = 5;
	int damage_max = 9;
	int damage_energy_min = 14;
	int damage_energy_max = 16;
	int kick = 17;
	int pellet_count = 20;
};

struct MachinegunConfig
{
	int damage_min = 4;
	int damage_max = 8;
	int kick = 2;
	int tracer_damage = 12;
	int tracer_cooldown_ms = 500;
	int tracer_damage_per_level = 4;  // Damage added per tracer upgrade level (max 40 at level 10)
};;

struct ChaingunConfig
{
	int damage_min = 6;
	int damage_max = 9;
	int kick = 3;
	int tracer_damage = 10;
	int tracer_cooldown_ms = 300;
	int tracer_damage_per_level = 2;  // Damage added per tracer upgrade level (max 20 at level 10)
};;

struct GrenadeConfig
{
	int damage = 125;
	float radius_offset = 40.0f;  // radius = damage + radius_offset
	float minspeed = 600.0f;  // Min throw speed for hand grenade
	float maxspeed = 900.0f;  // Max throw speed for hand grenade
	float speed_addon = 30.0f;  // Speed added per upgrade level
};

struct GrenadeLauncherConfig
{
	int damage_normal = 100;
	int damage_napalm = 95;
	float radius_normal = 135.0f;
	float radius_napalm = 115.0f;
	int speed = 1200;
	int speed_addon = 30;  // Speed added per upgrade level
};

struct RocketLauncherConfig
{
	int damage_min = 100;
	int damage_max = 125;
	int speed = 1230;
	int radius = 115;
	int damage_addon = 3;      // Damage added per upgrade level (was 3.5 hardcoded)
	int radius_addon = 3;      // Radius damage added per upgrade level (was 3.5 hardcoded)
	int speed_addon = 28;      // Speed added per upgrade level
};

struct RailgunConfig
{
	int damage = 150;
	int damage_horde = 225;
	int kick = 285;
	int damage_addon = 8;  // Damage added per upgrade level
};;

struct Cannon20mmConfig
{
	int damage = 22;
	int kick = 35;
	int range = 650;
	int recoil_force = 250;
	int range_addon = 30;  // Range added per upgrade level
};

struct BFGConfig
{
	int damage = 700;
	float radius = 1000.0f;
	int speed = 600;  // Initial speed (was hardcoded as BFG10K_INITIAL_SPEED)
	int damage_addon = 2;   // Damage added per upgrade level
	int speed_addon = 35;   // Speed added per upgrade level
};

// Xatrix expansion weapon configurations
struct IonRipperConfig
{
	int damage = 50;
	int damage_addon = 2;  // Damage added per upgrade level (was 2.5 hardcoded)
	int init_speed = 900;  // Initial projectile speed
	int speed_addon = 40;  // Speed added per upgrade level
};

struct PhalanxConfig
{
	int damage_min = 80;
	int damage_max = 95;
	int radius_damage = 120;
	int damage_radius = 120;
	int damage_addon = 3;      // Damage added per upgrade level (similar to rockets)
	int radius_addon = 3;      // Radius damage added per upgrade level
	int speed_addon = 25;      // Speed added per upgrade level
};

// Rogue expansion weapon configurations
struct PlasmaBeamConfig
{
	int damage = 15;
	int damage_singleplayer = 15;
	int kick = 3;
	int kick_singleplayer = 3;
	int damage_addon = 1;  // Damage added per upgrade level (was hardcoded +1)
};

struct TrackerConfig
{
	int damage = 140;
	int speed = 1000;
	int damage_addon = 1;      // Damage added per upgrade level (tick damage, smaller increment)
	int speed_addon = 30;      // Speed added per upgrade level
	float duration_addon = 0.2f; // Duration added per upgrade level (for tracker momentum)
};

struct ETFRifleConfig
{
	int damage_min = 9;   // Base per-shot damage roll minimum
	int damage_max = 13;  // Base per-shot damage roll maximum
	int kick_normal = 3;
	int damage_addon = 1;  // Damage added per upgrade level (was 1.0 hardcoded, comment said 1.25)
	int init_speed = 1450;  // Initial projectile speed
	int speed_addon = 40;   // Speed added per upgrade level
};

// Deployable configurations
struct ProxMineConfig
{
	int damage = 95;
	int damage_radius = 220;
	int health = 30;
	int time_to_live_sec = 45;
	int time_delay_ms = 350;
	float damage_open_multiplier = 1.5f;
	float bound_size = 96.0f;
	int damage_addon = 0;  // Damage added per upgrade level (currently no player skill addon)
};

struct LaserConfig
{
	int initial_health = 0;
	int addon_health = 150;
	int initial_damage = 1;
	int addon_damage = 2;
	float nonclient_mod = 0.5f;  // Damage multiplier for non-monster entities
	int cost = 25;  // Power cube cost to deploy
};

struct TrapConfig
{
	float minspeed = 500.0f;   // Min throw speed
	float maxspeed = 900.0f;   // Max throw speed
	float speed_addon = 30.0f; // Speed added per upgrade level
	float pull_radius = 350.0f;
	float pull_speed_monster = 210.0f;
	float pull_speed_player = 290.0f;
	int duration_sec = 80;
	int health = 125;
	int explosion_damage = 300;
	int explosion_radius = 100;
};

struct TeslaConfig
{
	// Only throw speed is configurable; damage/health/etc. come from the skill system
	float minspeed = 600.0f;   // Min throw speed
	float maxspeed = 900.0f;   // Max throw speed
	float speed_addon = 30.0f; // Speed added per upgrade level
};

struct SentryGunConfig
{
	int initial_health = 50;
	int addon_health = 15;
	int initial_armor = 50;
	int addon_armor = 30;
	int max_health = 200;
	int max_armor = 350;
	// Weapon damage scaling
	int initial_bullet = 6;       // Machinegun base damage
	int addon_bullet = 1;         // Machinegun damage per level
	int initial_heatbeam = 3;     // Heatbeam base damage
	int addon_heatbeam = 1;       // Heatbeam damage per level
	int initial_flechette = 6;    // Flechette base damage
	int addon_flechette = 1;      // Flechette damage per level
	int initial_rocket = 50;      // Rocket base damage
	int addon_rocket = 15;        // Rocket damage per level
	int initial_plasma = 50;      // Plasma base damage
	int addon_plasma = 15;        // Plasma damage per level
	int initial_grenade = 50;     // Grenade base damage
	int addon_grenade = 15;       // Grenade damage per level
	int cost = 50;                // Power cube cost to deploy
};

struct DopplegangerConfig
{
	int time_to_live_sec = 30;
	int health_base = 100;
	int explosion_damage = 160;
	int explosion_radius = 140;
};

// Bombspell configuration
struct BombSpellConfig
{
	int initial_damage = 75;
	int addon_damage = 10;
	int damage_radius = 150;
	int duration_sec = 5;
	int forward_cooldown_ms = 1500;
	int area_cooldown_ms = 10000;
	int step_size = 128;  // Vortex CARPETBOMB_STEP_SIZE
	int carpet_width = 200;
};

// Fireball configuration
struct FireballConfig
{
	int initial_damage = 50;
	int addon_damage = 25;
	int initial_radius = 100;
	float addon_radius = 2.5f;
	int initial_speed = 650;
	int addon_speed = 35;
	int cost = 15;  // Power cube cost per fireball
};

// Exploding Barrel configuration
struct ExplodingBarrelConfig
{
	int initial_health = 30;
	int addon_health = 0;      // No health scaling per level
	int initial_damage = 100;
	int addon_damage = 40;     // +40 damage per level
	int cost = 20;             // Power cube cost per spawn
	int max_count = 4;         // Max active barrels
};

// Monster Summon configuration
struct SummonConfig
{
	int spawn_cost = 25;            // Power cube cost to spawn a monster
	int upkeep_per_monster = 1;     // PC regen consumed per alive monster (reduces effective regen level)
	int initial_health = 100;       // Base health at level 1
	int addon_health = 50;          // Health added per upgrade level
	int initial_armor = 0;          // Base armor at level 1
	int addon_armor = 25;           // Armor added per upgrade level
	float damage_scale = 1.0f;      // Damage multiplier for summoned monsters
	float speed_scale = 1.0f;       // Speed multiplier for summoned monsters
};

// Hook configuration (offhand hook system from hook.cpp)
struct HookConfig
{
	int speed = 900;
	int pull_speed = 700;
	int damage = 20;
	int init_damage = 10;
	int max_damage = 20;
	int max_time_sec = 5;
	float delay_sec = 0.2f;
	int bot_chain_speed = 800;
	int bot_throw_speed = 1800;
	bool allow_sky_attach = false;
};

// Grapple configuration (weapon-slot grapple from CTF)
struct GrappleConfig
{
	int fly_speed = 650;
	int pull_speed = 650;
	int damage = 10;
};

// Ammo regeneration configuration
// Global weapon damage configuration (base values for all monsters)
// OPTIMIZED: Array-based O(1) lookups instead of O(log n) string lookups
struct GlobalWeaponDamage
{
	std::array<int, 24> values{}; // Indexed by horde::WeaponID enum

	// Constructor with default values matching original named fields
	GlobalWeaponDamage() {
		values[0]  = 7;   // MELEE
		values[1]  = 5;   // BLASTER
		values[2]  = 7;   // BLASTER2
		values[3]  = 8;   // BLASTER_BOLT
		values[4]  = 5;   // BLUEBLASTER
		values[5]  = 1;   // SHOTGUN
		values[6]  = 3;   // MACHINEGUN
		values[7]  = 18;  // GRENADE
		values[8]  = 22;  // ROCKET
		values[9]  = 20;  // HEAT
		values[10] = 25;  // RAILGUN
		values[11] = 25;  // BFG
		values[12] = 6;   // IONRIPPER
		values[13] = 6;   // HYPERBLASTER
		values[14] = 14;  // BOLT
		values[15] = 7;   // TRACKER
		values[16] = 12;  // PLASMA
		values[17] = 5;   // DABEAM
		values[18] = 5;   // HEATBEAM
		values[19] = 30;  // SLAM
		values[20] = 5;   // LIGHTNING
		values[21] = 4;   // FLECHETTE
		values[22] = 12;  // FIREBALL
		values[23] = 7;   // PROBOSCIS
	}

	// Helper for JSON loading
	void set(horde::WeaponID id, int val) {
		if (id != horde::WeaponID::UNKNOWN) {
			values[static_cast<size_t>(id)] = val;
		}
	}
};

// Global weapon speed configuration (projectile velocity)
// OPTIMIZED: Array-based O(1) lookups instead of O(log n) string lookups
struct GlobalWeaponSpeed
{
	std::array<int, 24> values{}; // Indexed by horde::WeaponID enum

	// Constructor with default values matching original named fields
	GlobalWeaponSpeed() {
		values[0]  = 0;    // MELEE - melee
		values[1]  = 1000; // BLASTER
		values[2]  = 1000; // BLASTER2
		values[3]  = 1000; // BLASTER_BOLT
		values[4]  = 1000; // BLUEBLASTER
		values[5]  = 0;    // SHOTGUN - instant hit
		values[6]  = 0;    // MACHINEGUN - instant hit
		values[7]  = 600;  // GRENADE
		values[8]  = 650;  // ROCKET
		values[9]  = 650;  // HEAT
		values[10] = 0;    // RAILGUN - instant hit
		values[11] = 400;  // BFG
		values[12] = 1000; // IONRIPPER
		values[13] = 1000; // HYPERBLASTER
		values[14] = 600;  // BOLT
		values[15] = 500;  // TRACKER
		values[16] = 700;  // PLASMA
		values[17] = 0;    // DABEAM - beam weapon
		values[18] = 0;    // HEATBEAM - beam weapon
		values[19] = 0;    // SLAM - melee
		values[20] = 0;    // LIGHTNING - instant hit
		values[21] = 1150; // FLECHETTE
		values[22] = 600;  // FIREBALL
		values[23] = 0;    // PROBOSCIS - melee
	}

	// Helper for JSON loading
	void set(horde::WeaponID id, int val) {
		if (id != horde::WeaponID::UNKNOWN) {
			values[static_cast<size_t>(id)] = val;
		}
	}
};

// Global weapon damage radius configuration (explosion/splash radius)
// OPTIMIZED: Array-based O(1) lookups instead of O(log n) string lookups
struct GlobalWeaponRadius
{
	std::array<float, 24> values{}; // Indexed by horde::WeaponID enum

	// Constructor with default values (most weapons have 0 radius)
	GlobalWeaponRadius() {
		// All values default to 0.0f, only set non-zero ones
		values[7]  = 140.0f;  // GRENADE
		values[8]  = 120.0f;  // ROCKET
		values[9]  = 140.0f;  // HEAT
		values[11] = 450.0f;  // BFG
		values[15] = 140.0f;  // TRACKER
		values[16] = 120.0f;  // PLASMA
		values[22] = 120.0f;  // FIREBALL
	}

	// Helper for JSON loading
	void set(horde::WeaponID id, float val) {
		if (id != horde::WeaponID::UNKNOWN) {
			values[static_cast<size_t>(id)] = val;
		}
	}
};

// Monster stats configuration (per-monster multipliers and base stats)
struct MonsterStatsConfig
{
	// Base stats
	int health = 100;
	int power_armor_power = 0;
	int32_t power_armor_type = 0; // item_id_t (IT_NULL, IT_ITEM_POWER_SHIELD, IT_ITEM_POWER_SCREEN)
	int armor_power = 0;
	int32_t armor_type = 0; // item_id_t (IT_NULL, IT_ARMOR_BODY, IT_ARMOR_COMBAT, etc.)

	// Scaling multipliers (applied to base values, then wave scaling)
	float health_scale = 1.0f;         // Boss: 1.5-2.0x
	float damage_scale = 1.0f;         // Boss: 1.3-1.5x, applies to ALL weapons this monster uses
	float speed_scale = 1.0f;          // Projectile speed adjustment
	float armor_scale = 1.0f;          // Boss: 1.5x
	float power_armor_scale = 1.0f;    // Boss: 1.5x

	// Weapon damage overrides - OPTIMIZED using array-based lookup (O(1) instead of O(log n))
	// Index using horde::WeaponID enum. Value of 0 means "use global damage"
	std::array<int, 24> weapon_damage_overrides{}; // 24 = horde::WeaponID::MAX_WEAPONS
	std::array<int, 24> weapon_damage_max{};       // Original Remaster cap, 0 means no clamp

	// Boss-only weapon damage overrides. When the monster instance is IS_BOSS and the entry
	// is > 0, this exact value is used as the final damage (no damage_scale, no max clamp) -
	// the whole point is to let bosses exceed the normal Remaster caps. 0 = no boss override.
	std::array<int, 24> boss_weapon_damage_overrides{}; // 24 = horde::WeaponID::MAX_WEAPONS
	std::array<int, 24> weapon_addon_damage{};     // Reserved for future level scaling
	
	// Weapon speed overrides - OPTIMIZED using array-based lookup (O(1) instead of O(log n))
	// Index using horde::WeaponID enum. Value of 0 means "use global speed with speed_scale"
	std::array<int, 24> weapon_speed_overrides{}; // 24 = horde::WeaponID::MAX_WEAPONS
};;

// Monster level scaling configuration (level-based progression)
// Base stats come from MonsterStatsConfig; addon_* values are the active progression knobs.
// initial_* fields are kept for backward compatibility and data migration support.
struct MonsterLevelScaling
{
	int initial_health = 100;
	int addon_health = 10;
	int initial_armor = 0;
	int addon_armor = 0;
	int initial_power_armor = 0;
	int addon_power_armor = 0;
};

// Monsters configuration - maps MonsterTypeID to stats
struct MonstersConfig
{
	boost::container::flat_map<uint8_t, MonsterStatsConfig> monsters;  // Small integer keys (0-255) for frequent lookups
	boost::unordered::unordered_flat_map<std::string, MonsterLevelScaling> level_scaling;  // Cache-friendly hash map
};

// Map-specific override configuration
struct MapOverrideConfig
{
	int32_t monster_cap = -1;  // -1 means use default based on map size
	bool enable_grid = true;
	bool has_grid_override = false;  // Whether enable_grid was explicitly set
	bool enable_loadent = true;  // Whether to enable g_loadent for this map
	bool has_loadent_override = false;  // Whether enable_loadent was explicitly set
	// Map size override (stored as separate bools to avoid circular dependency with horde::MapSize)
	bool size_override_is_small = false;
	bool size_override_is_big = false;
	bool size_override_is_medium = true;
	bool has_size_override = false;  // Whether map_size was explicitly set
	// Boss size override - decoupled from map_size so a map can keep, e.g., medium
	// monster spawning while drawing bosses from a different (smaller) pool.
	bool boss_size_override_is_small = false;
	bool boss_size_override_is_big = false;
	bool boss_size_override_is_medium = false;
	bool has_boss_size_override = false;  // Whether boss_size was explicitly set
};

// Maps configuration - default caps and per-map overrides
struct MapsConfig
{
	// Default monster caps by map size
	int32_t big_map_cap = 26;
	int32_t medium_map_cap = 14;
	int32_t small_map_cap = 12;
	int32_t custom_map_cap = 20;

	// Default grid setting
	bool default_enable_grid = false;

	// Per-map overrides indexed by MapID (use MapID::UNKNOWN for unset entries)
	std::array<MapOverrideConfig, 64> map_overrides;  // 64 = horde::MapID::MAX_MAPS
};

// Power cubes currency configuration
struct PowerCubesConfig
{
	int cubes_per_ammopack = 25;      // Power cubes gained per ammopack pickup
	int cubes_per_shard = 5;      // Power cubes gained per armor shard pickup
	bool use_bullets_max = true;  // Use bullets max ammo as part of capacity calculation
	bool use_cells_max = true;    // Use cells max ammo as part of capacity calculation
};


// Power cubes regeneration configuration
struct PowerCubesRegenConfig
{
	float base_regen_time = 5.0f; // Base time (in seconds) between regenerations (at level 1)
	int cubes_per_regen = 5;      // Power cubes gained per regeneration tick
};

// Master configuration structure
struct GameConfig
{
	// Entity limits
	EntityLimitsConfig entity_limits;

	// Base weapons
	BlasterConfig blaster;
	HyperBlasterConfig hyperblaster;
	ShotgunConfig shotgun;
	SuperShotgunConfig supershotgun;
	MachinegunConfig machinegun;
	ChaingunConfig chaingun;
	GrenadeConfig grenade;
	GrenadeLauncherConfig grenadelauncher;
	RocketLauncherConfig rocket;
	RailgunConfig railgun;
	Cannon20mmConfig cannon20mm;
	BFGConfig bfg;

	// Xatrix weapons
	IonRipperConfig ionripper;
	PhalanxConfig phalanx;

	// Rogue weapons
	PlasmaBeamConfig plasmabeam;
	TrackerConfig tracker;
	ETFRifleConfig etfrifle;

	// Deployables
	ProxMineConfig prox_mine;
	LaserConfig laser;
	TrapConfig trap;
	TeslaConfig tesla;
	SentryGunConfig sentrygun;
	DopplegangerConfig doppleganger;

	// Special abilities
	BombSpellConfig bomb_spell;
	FireballConfig fireball;
	ExplodingBarrelConfig exploding_barrel;
	SummonConfig summon;

	// Hook and Grapple
	HookConfig hook;
	GrappleConfig grapple;

	// Power cubes currency
	PowerCubesConfig power_cubes;
	PowerCubesRegenConfig power_cubes_regen;

	// Global monster weapons (base values)
	GlobalWeaponDamage global_weapon_damage;
	GlobalWeaponSpeed global_weapon_speed;
	GlobalWeaponRadius global_weapon_radius;

	// Monsters
	MonstersConfig monsters;

	// Maps
	MapsConfig maps;

	// Squad respawn timers (seconds; 0 = unset, use compiled defaults)
	float respawn_damage_time_sec = 0.f;
	float respawn_bad_area_time_sec = 0.f;
};

// Global config instance
extern GameConfig g_config;

// Config management functions
void Config_Load(const char* basedir);
void Config_Reload();
void Config_SetDefaults();
void Config_LoadMaps(const char* basedir);

// Monster config helper functions
const MonsterStatsConfig* GetMonsterConfig(uint8_t monster_type_id);
int GetMonsterWeaponDamage(uint8_t monster_type_id, horde::WeaponID weapon_id, bool is_boss = false);
int GetMonsterWeaponSpeed(uint8_t monster_type_id, horde::WeaponID weapon_id);
int GetMonsterWeaponRadius(uint8_t monster_type_id, horde::WeaponID weapon_id);

// Scaling helpers
int GetScaledHealth(int base_health, float health_scale, int wave_level, bool is_boss);
int GetScaledArmor(int base_armor, float armor_scale, int wave_level, bool is_boss);
int GetScaledPowerArmor(int base_power_armor, float power_armor_scale, int wave_level, bool is_boss);
bool ShouldApplyMonsterWaveScaling(uint8_t monster_type_id);

// Monster health/armor helpers (for macros)
int GetMonsterBaseHealth(uint8_t monster_type_id);
int GetMonsterScaledHealth(uint8_t monster_type_id, int wave_level, bool is_boss);
int GetMonsterBaseArmor(uint8_t monster_type_id);
int GetMonsterScaledArmor(uint8_t monster_type_id, int wave_level, bool is_boss);
int GetMonsterBasePowerArmor(uint8_t monster_type_id);
int GetMonsterScaledPowerArmor(uint8_t monster_type_id, int wave_level, bool is_boss);
int32_t GetMonsterArmorType(uint8_t monster_type_id);
int32_t GetMonsterPowerArmorType(uint8_t monster_type_id);

// Map config helper functions
int32_t GetMonsterCapForMap(horde::MapID mapId, const struct horde::MapSize& mapSize);
int32_t GetMonsterCapForMap(const char* mapname, const struct horde::MapSize& mapSize);  // Convenience overload
bool GetGridEnabledForMap(horde::MapID mapId);
bool GetGridEnabledForMap(const char* mapname);  // Convenience overload
bool GetLoadentEnabledForMap(horde::MapID mapId);
bool GetLoadentEnabledForMap(const char* mapname);  // Convenience overload

// Monster level scaling helpers
const MonsterLevelScaling* GetMonsterLevelScaling(const char* monster_name);
void GetMonsterLevelScaledStats(const char* monster_name, int32_t pvm_level, int& out_health, int& out_armor);

// Global variables for player levels (updated periodically in Horde_RunFrame)
extern int32_t g_lowest_player_level;
extern int32_t g_highest_player_level;
