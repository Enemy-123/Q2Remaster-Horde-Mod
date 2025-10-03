#pragma once

#include <cstdint>
#include <unordered_map>

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
	int damage = 15;
	int speed = 1300;
	int bounces = 5;
};

struct HyperBlasterConfig
{
	int damage_min = 16;
	int damage_max = 18;
	int speed = 1700;
	int bounces = 3;
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
};

struct ChaingunConfig
{
	int damage_min = 7;
	int damage_max = 11;
	int kick = 2;
	int tracer_damage = 20;
	int tracer_cooldown_ms = 300;
};

struct GrenadeConfig
{
	int damage = 125;
	float radius_offset = 40.0f;  // radius = damage + radius_offset
};

struct GrenadeLauncherConfig
{
	int damage_normal = 115;
	int damage_napalm = 95;
	float radius_normal = 155.0f;
	float radius_napalm = 135.0f;
	int speed = 1200;
};

struct RocketLauncherConfig
{
	int damage_min = 100;
	int damage_max = 120;
	int speed = 1230;
	int radius = 125;
};

struct RailgunConfig
{
	int damage = 150;
	int damage_horde = 225;
	int kick = 285;
};

struct BFGConfig
{
	int damage = 700;
	float radius = 1000.0f;
	int speed = 600;
	int ammo_normal = 50;
	int ammo_slide = 25;
};

// Xatrix expansion weapon configurations
struct IonRipperConfig
{
	int damage = 50;
};

struct PhalanxConfig
{
	int damage_min = 80;
	int damage_max = 95;
	int radius_damage = 120;
	int damage_radius = 120;
};

// Rogue expansion weapon configurations
struct PlasmaBeamConfig
{
	int damage = 145;
	int damage_singleplayer = 135;
	int kick = 3;
};

struct ETFRifleConfig
{
	int kick_normal = 3;
	int kick_homing = 75;
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
	int speed_min = 500;
	int speed_max = 900;
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

// Monster weapon damage configuration
struct MonsterWeaponDamage
{
	int blaster = 0;
	int shotgun = 0;
	int machinegun = 0;
	int grenade = 0;
	int rocket = 0;
	int railgun = 0;
	int bfg = 0;
	int ionripper = 0;
	int hyperblaster = 0;
	int tracker = 0;
	int plasma = 0;
	int dabeam = 0;
	int heatbeam = 0;
	int melee = 0;
	int slam = 0;
	int lightning = 0;
	int flechette = 0;
};

// Monster stats configuration
struct MonsterStatsConfig
{
	int health = 100;
	int power_armor_power = 0;
	int32_t power_armor_type = 0; // item_id_t (IT_NULL, IT_ITEM_POWER_SHIELD, IT_ITEM_POWER_SCREEN)
	int armor_power = 0;
	int32_t armor_type = 0; // item_id_t (IT_NULL, IT_ARMOR_BODY, IT_ARMOR_COMBAT, etc.)
	MonsterWeaponDamage weapon_damage;
	// float wave_damage_multiplier = 0.0f; // Future: per-wave damage scaling
};

// Monsters configuration - maps MonsterTypeID to stats
struct MonstersConfig
{
	std::unordered_map<uint8_t, MonsterStatsConfig> monsters;
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
	BFGConfig bfg;

	// Xatrix weapons
	IonRipperConfig ionripper;
	PhalanxConfig phalanx;

	// Rogue weapons
	PlasmaBeamConfig plasmabeam;
	ETFRifleConfig etfrifle;

	// Deployables
	ProxMineConfig prox_mine;
	LaserConfig laser;
	TrapConfig trap;
	TeslaConfig tesla;
	DopplegangerConfig doppleganger;

	// Special abilities
	BombSpellConfig bomb_spell;

	// Hook and Grapple
	HookConfig hook;
	GrappleConfig grapple;

	// Ammo regeneration
	AmmoRegenConfig ammo_regen;

	// Monsters
	MonstersConfig monsters;
};

// Global config instance
extern GameConfig g_config;

// Config management functions
void Config_Load(const char* basedir);
void Config_Reload();
void Config_SetDefaults();

// Monster config helper functions
const MonsterStatsConfig* GetMonsterConfig(uint8_t monster_type_id);
int GetMonsterWeaponDamage(uint8_t monster_type_id, const char* weapon_name);
