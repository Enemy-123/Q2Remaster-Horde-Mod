#pragma once

#include <cstdint>
#include <unordered_map>
#include <string>
#include <array>

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
	int32_t max_teslas = 12;
	int32_t max_barrels = 10;
	int32_t max_prox = 20;
	int32_t max_traps = 8;
	int32_t max_summons = 8;
};

// Base weapon configurations
struct BlasterConfig
{
	int damage_min = 16;
	int damage_max = 18;
	int speed = 1300;
	int bounces = 5;
	int speed_addon = 40;  // Speed added per upgrade level
};

struct HyperBlasterConfig
{
	int damage_min = 16;
	int damage_max = 18;
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
	int pellet_count_normal = 20;
};

struct SuperShotgunConfig
{
	int damage_min = 7;
	int damage_max = 10;
	int damage_energy_min = 14;
	int damage_energy_max = 16;
	int kick = 17;
	int pellet_count = 20;
};

struct MachinegunConfig
{
	int damage_min = 7;
	int damage_max = 10;
	int kick = 2;
	int tracer_damage = 40;
	int tracer_cooldown_ms = 500;
	int tracer_damage_per_level = 4;  // Damage added per tracer upgrade level (max 40 at level 10)
};;

struct ChaingunConfig
{
	int damage_min = 7;
	int damage_max = 11;
	int kick = 2;
	int tracer_damage = 20;
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
	int damage_normal = 115;
	int damage_napalm = 95;
	float radius_normal = 155.0f;
	float radius_napalm = 135.0f;
	int speed = 1200;
	int speed_addon = 30;  // Speed added per upgrade level
};

struct RocketLauncherConfig
{
	int damage_min = 100;
	int damage_max = 120;
	int speed = 1230;
	int radius = 125;
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
	int damage = 35;
	int kick = 35;
	int range = 650;
	int recoil_force = 500;
	int range_addon = 30;  // Range added per upgrade level
};

struct BFGConfig
{
	int damage = 700;
	float radius = 1000.0f;
	int speed = 650;  // Initial speed (was hardcoded as BFG10K_INITIAL_SPEED)
	int ammo_normal = 50;
	int ammo_slide = 25;
	int damage_addon = 2;   // Damage added per upgrade level
	int speed_addon = 35;   // Speed added per upgrade level
};;

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
	int damage = 145;
	int damage_singleplayer = 135;
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
	int kick_normal = 3;
	int kick_homing = 75;
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
	int health_base = 150;
	int health_addon_per_wave = 120;
	int damage_initial = 1;
	int damage_addon_per_wave = 4;
};

struct TrapConfig
{
	float minspeed = 500.0f;   // Min throw speed
	float maxspeed = 900.0f;   // Max throw speed
	float speed_addon = 30.0f; // Speed added per upgrade level
	int timer_sec = 5;
	float pull_radius = 400.0f;
	float pull_speed_monster = 210.0f;
	float pull_speed_player = 290.0f;
	int duration_sec = 80;
	int health = 125;
	int explosion_damage = 300;
	int explosion_radius = 100;
};

struct TeslaConfig
{
	int damage = 4;
	int damage_radius = 200;
	int health = 50;
	int time_to_live_sec = 30;
	int activate_time_ms = 1200;
	int explosion_damage_multiplier = 50;
	int explosion_radius = 200;
	int knockback = 8;
	int damage_addon = 0;  // Damage added per upgrade level (currently uses multiplier from skill system)
	float minspeed = 600.0f;   // Min throw speed
	float maxspeed = 900.0f;   // Max throw speed
	float speed_addon = 30.0f; // Speed added per upgrade level
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
	int max_height = 256;
	int step_size = 96;
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
struct AmmoRegenRateConfig
{
	int quantity = 0;
	int interval_ms = 0;
};

struct AmmoRegenConfig
{
	bool enabled = true;
	AmmoRegenRateConfig bullets{10, 3000};
	AmmoRegenRateConfig shells{5, 3000};
	AmmoRegenRateConfig grenades{3, 4000};
	AmmoRegenRateConfig rockets{2, 5000};
	AmmoRegenRateConfig cells{10, 3000};
	AmmoRegenRateConfig slugs{5, 4000};
	AmmoRegenRateConfig magslug{3, 5000};
	AmmoRegenRateConfig prox{1, 6000};
	AmmoRegenRateConfig trap{1, 6000};
	AmmoRegenRateConfig tesla{2, 5000};
};

// Global weapon damage configuration (base values for all monsters)
struct GlobalWeaponDamage
{
	int melee = 10;
	int blaster = 15;
	int blaster2 = 20;
	int blaster_bolt = 18;
	int blueblaster = 20;
	int shotgun = 4;
	int machinegun = 8;
	int grenade = 50;
	int rocket = 100;
	int heat = 15;
	int railgun = 150;
	int bfg = 500;
	int ionripper = 50;
	int hyperblaster = 15;
	int bolt = 20;
	int tracker = 30;
	int plasma = 40;
	int dabeam = 30;
	int heatbeam = 30;
	int slam = 25;
	int lightning = 12;
	int flechette = 12;
	int fireball = 40;
	int proboscis = 20;
};

// Global weapon speed configuration (projectile velocity)
struct GlobalWeaponSpeed
{
	int blaster = 1000;
	int blaster2 = 1100;
	int blaster_bolt = 1000;
	int blueblaster = 1100;
	int shotgun = 0; // instant hit
	int machinegun = 0; // instant hit
	int grenade = 600;
	int rocket = 650;
	int heat = 1000;
	int railgun = 0; // instant hit
	int bfg = 400;
	int ionripper = 500;
	int hyperblaster = 1000;
	int bolt = 800;
	int tracker = 500;
	int plasma = 1200;
	int dabeam = 0; // beam weapon
	int heatbeam = 0; // beam weapon
	int melee = 0; // melee
	int slam = 0; // melee
	int lightning = 0; // instant hit
	int flechette = 1150;
	int fireball = 400;
	int proboscis = 0; // melee
};

// Global weapon damage radius configuration (explosion/splash radius)
struct GlobalWeaponRadius
{
	float grenade = 150.0f;
	float rocket = 140.0f;
	float bfg = 1000.0f;
	float tracker = 120.0f;
	float plasma = 100.0f;
	float fireball = 125.0f;
	// Most weapons don't have radius (0 by default)
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


//// =======================================================================
// ADD THIS NEW BLOCK OF CODE
// =======================================================================
// Load weapon damage overrides

	std::unordered_map<std::string, int> weapon_damage_overrides; 
// =======================================================================
// END OF NEW CODE
// =======================================================================

};

// Monsters configuration - maps MonsterTypeID to stats
struct MonstersConfig
{
	std::unordered_map<uint8_t, MonsterStatsConfig> monsters;
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
	DopplegangerConfig doppleganger;

	// Special abilities
	BombSpellConfig bomb_spell;
	FireballConfig fireball;

	// Hook and Grapple
	HookConfig hook;
	GrappleConfig grapple;

	// Ammo regeneration
	AmmoRegenConfig ammo_regen;

	// Global monster weapons (base values)
	GlobalWeaponDamage global_weapon_damage;
	GlobalWeaponSpeed global_weapon_speed;
	GlobalWeaponRadius global_weapon_radius;

	// Monsters
	MonstersConfig monsters;

	// Maps
	MapsConfig maps;

	// Scaling system
	bool use_sigmoid_scaling = false;
	bool use_sigmoid_scaling_bosses_only = false;
	bool use_sigmoid_scaling_except_bosses = false;
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
int GetMonsterWeaponDamage(uint8_t monster_type_id, const char* weapon_name);
int GetMonsterWeaponSpeed(uint8_t monster_type_id, const char* weapon_name);
int GetMonsterWeaponRadius(uint8_t monster_type_id, const char* weapon_name);

// Global weapon helpers
int GetGlobalWeaponDamage(const char* weapon_name);
int GetGlobalWeaponSpeed(const char* weapon_name);
float GetGlobalWeaponRadius(const char* weapon_name);

// Scaling helpers
int GetScaledHealth(int base_health, float health_scale, int wave_level, bool is_boss);
int GetScaledArmor(int base_armor, float armor_scale, int wave_level, bool is_boss);
int GetScaledPowerArmor(int base_power_armor, float power_armor_scale, int wave_level, bool is_boss);

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
