#pragma once

#include <cstdint>

// Configuration for entity limits
struct EntityLimitsConfig
{
	int32_t max_sentries = 3;
	int32_t max_lasers = 6;
	int32_t max_teslas = 12;
	int32_t max_barrels = 10;
	int32_t max_prox = 20;
	int32_t max_summons = 8;
};

// Base weapon configurations
struct BlasterConfig
{
	int damage = 15;
	int speed = 1300;
};

struct HyperBlasterConfig
{
	int damage = 15;
	int speed = 1700;
};

struct ShotgunConfig
{
	int damage_min = 3;
	int damage_max = 5;
	int damage_energy_min = 7;
	int damage_energy_max = 11;
	int kick = 8;
};

struct SuperShotgunConfig
{
	int damage_min = 4;
	int damage_max = 6;
	int kick = 12;
};

struct MachinegunConfig
{
	int damage = 8;
	int kick = 2;
};

struct ChaingunConfig
{
	int damage = 6;
	int kick = 2;
};

struct GrenadeConfig
{
	int damage = 125;
	float radius_offset = 40.0f;  // radius = damage + radius_offset
};

struct GrenadeLauncherConfig
{
	int damage_min = 100;
	int damage_max = 120;
	float radius = 165.0f;
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
};

// Xatrix expansion weapon configurations
struct IonRipperConfig
{
	int damage = 50;
};

struct PhalanxConfig
{
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
	float pull_radius = 400.0f;
	float pull_speed_monster = 210.0f;
	float pull_speed_player = 290.0f;
	int duration_sec = 80;
	int health = 125;
	int explosion_damage = 300;
	int explosion_radius = 100;
};

struct DopplegangerConfig
{
	int time_to_live_sec = 30;
	int health_base = 100;
	int explosion_damage = 160;
	int explosion_radius = 140;
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
	DopplegangerConfig doppleganger;

	// Ammo regeneration
	AmmoRegenConfig ammo_regen;
};

// Global config instance
extern GameConfig g_config;

// Config management functions
void Config_Load(const char* basedir);
void Config_Reload();
void Config_SetDefaults();
