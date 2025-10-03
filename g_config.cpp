#include "g_config.h"
#include "g_local.h"
#include "horde/horde_ids.h"
#include <json/json.h>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <cstring>

// Global config instance
GameConfig g_config;

// Helper function to safely get int from JSON
static int GetJsonInt(const Json::Value& json, const char* key, int defaultValue)
{
	if (json.isMember(key) && json[key].isInt())
		return json[key].asInt();
	return defaultValue;
}

// Helper function to safely get float from JSON
static float GetJsonFloat(const Json::Value& json, const char* key, float defaultValue)
{
	if (json.isMember(key) && json[key].isNumeric())
		return json[key].asFloat();
	return defaultValue;
}

// Note: GetMonsterTypeIDFromString removed - use horde::MonsterTypeRegistry::GetTypeID() instead

// Helper function to convert armor type string to item_id_t
static int32_t GetArmorTypeFromString(const std::string& type)
{
	if (type == "shield") return IT_ITEM_POWER_SHIELD;
	if (type == "screen") return IT_ITEM_POWER_SCREEN;
	if (type == "body") return IT_ARMOR_BODY;
	if (type == "combat") return IT_ARMOR_COMBAT;
	if (type == "jacket") return IT_ARMOR_JACKET;
	return IT_NULL;
}

void Config_SetDefaults()
{
	// Reset to default values (already set in struct definitions)
	g_config = GameConfig();
}

void Config_LoadMonsters(const char* basedir)
{
	// Build config file path
	std::string config_path = std::string(basedir) + "config/monsters.json";

	// Try to open config file
	std::ifstream config_file(config_path, std::ifstream::binary);
	if (!config_file.is_open())
	{
		gi.Com_PrintFmt("Config: config/monsters.json not found, using default monster values\n");
		gi.Com_PrintFmt("Config: You can create {} to customize monster settings\n", config_path);
		return;
	}

	// Parse JSON
	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errs;

	if (!Json::parseFromStream(builder, config_file, &root, &errs))
	{
		gi.Com_PrintFmt("Config: Failed to parse config/monsters.json: {}\n", errs);
		gi.Com_PrintFmt("Config: Using default monster values\n");
		return;
	}

	config_file.close();

	// Load monsters
	if (root.isMember("monsters") && root["monsters"].isObject())
	{
		const Json::Value& monsters = root["monsters"];
		int loaded_count = 0;

		for (const auto& monster_name : monsters.getMemberNames())
		{
			horde::MonsterTypeID monster_type = horde::MonsterTypeRegistry::GetTypeID(monster_name.c_str());
			if (monster_type == horde::MonsterTypeID::UNKNOWN)
			{
				gi.Com_PrintFmt("Config: Unknown monster type: {}\n", monster_name);
				continue;
			}
			uint8_t monster_id = static_cast<uint8_t>(monster_type);

			const Json::Value& monster_data = monsters[monster_name];
			MonsterStatsConfig config;

			// Load basic stats
			config.health = GetJsonInt(monster_data, "health", 100);
			config.power_armor_power = GetJsonInt(monster_data, "power_armor_power", 0);
			config.armor_power = GetJsonInt(monster_data, "armor_power", 0);

			// Load armor types
			if (monster_data.isMember("power_armor_type") && monster_data["power_armor_type"].isString())
			{
				config.power_armor_type = GetArmorTypeFromString(monster_data["power_armor_type"].asString());
			}
			if (monster_data.isMember("armor_type") && monster_data["armor_type"].isString())
			{
				config.armor_type = GetArmorTypeFromString(monster_data["armor_type"].asString());
			}

			// Load weapon damage
			if (monster_data.isMember("weapon_damage") && monster_data["weapon_damage"].isObject())
			{
				const Json::Value& weapons = monster_data["weapon_damage"];
				config.weapon_damage.blaster = GetJsonInt(weapons, "blaster", 0);
				config.weapon_damage.shotgun = GetJsonInt(weapons, "shotgun", 0);
				config.weapon_damage.machinegun = GetJsonInt(weapons, "machinegun", 0);
				config.weapon_damage.grenade = GetJsonInt(weapons, "grenade", 0);
				config.weapon_damage.rocket = GetJsonInt(weapons, "rocket", 0);
				config.weapon_damage.railgun = GetJsonInt(weapons, "railgun", 0);
				config.weapon_damage.bfg = GetJsonInt(weapons, "bfg", 0);
				config.weapon_damage.ionripper = GetJsonInt(weapons, "ionripper", 0);
				config.weapon_damage.hyperblaster = GetJsonInt(weapons, "hyperblaster", 0);
			}

			g_config.monsters.monsters[monster_id] = config;
			loaded_count++;
		}

		gi.Com_PrintFmt("Config: Loaded {} monster configurations from config/monsters.json\n", loaded_count);
	}
}

void Config_Load(const char* basedir)
{
	// Set defaults first
	Config_SetDefaults();

	// Build config file path
	std::string config_path = std::string(basedir) + "config/player_config.json";

	// Try to open config file
	std::ifstream config_file(config_path, std::ifstream::binary);
	if (!config_file.is_open())
	{
		gi.Com_PrintFmt("Config: config/player_config.json not found, using default values\n");
		gi.Com_PrintFmt("Config: You can create {} to customize settings\n", config_path);
		return;
	}

	// Parse JSON
	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errs;

	if (!Json::parseFromStream(builder, config_file, &root, &errs))
	{
		gi.Com_PrintFmt("Config: Failed to parse config/player_config.json: {}\n", errs);
		gi.Com_PrintFmt("Config: Using default values\n");
		return;
	}

	config_file.close();

	// Load entity limits
	if (root.isMember("entity_limits") && root["entity_limits"].isObject())
	{
		const Json::Value& limits = root["entity_limits"];
		g_config.entity_limits.max_sentries = GetJsonInt(limits, "max_sentries", 3);
		g_config.entity_limits.max_lasers = GetJsonInt(limits, "max_lasers", 6);
		g_config.entity_limits.max_teslas = GetJsonInt(limits, "max_teslas", 12);
		g_config.entity_limits.max_barrels = GetJsonInt(limits, "max_barrels", 10);
		g_config.entity_limits.max_prox = GetJsonInt(limits, "max_prox", 50);
		g_config.entity_limits.max_traps = GetJsonInt(limits, "max_traps", 8);
		g_config.entity_limits.max_summons = GetJsonInt(limits, "max_summons", 8);
	}

	// Load weapon configs
	if (root.isMember("weapons") && root["weapons"].isObject())
	{
		const Json::Value& weapons = root["weapons"];

		// Blaster
		if (weapons.isMember("blaster") && weapons["blaster"].isObject())
		{
			const Json::Value& w = weapons["blaster"];
			g_config.blaster.damage = GetJsonInt(w, "damage", 15);
			g_config.blaster.speed = GetJsonInt(w, "speed", 1300);
			g_config.blaster.bounces = GetJsonInt(w, "bounces", 5);
		}

		// Hyperblaster
		if (weapons.isMember("hyperblaster") && weapons["hyperblaster"].isObject())
		{
			const Json::Value& w = weapons["hyperblaster"];
			g_config.hyperblaster.damage_min = GetJsonInt(w, "damage_min", 16);
			g_config.hyperblaster.damage_max = GetJsonInt(w, "damage_max", 18);
			g_config.hyperblaster.speed = GetJsonInt(w, "speed", 1700);
			g_config.hyperblaster.bounces = GetJsonInt(w, "bounces", 3);
		}

		// Shotgun
		if (weapons.isMember("shotgun") && weapons["shotgun"].isObject())
		{
			const Json::Value& w = weapons["shotgun"];
			g_config.shotgun.damage_min = GetJsonInt(w, "damage_min", 3);
			g_config.shotgun.damage_max = GetJsonInt(w, "damage_max", 5);
			g_config.shotgun.damage_energy_min = GetJsonInt(w, "damage_energy_min", 7);
			g_config.shotgun.damage_energy_max = GetJsonInt(w, "damage_energy_max", 11);
			g_config.shotgun.kick = GetJsonInt(w, "kick", 8);
			g_config.shotgun.pellet_count_deathmatch = GetJsonInt(w, "pellet_count_deathmatch", 12);
			g_config.shotgun.pellet_count_normal = GetJsonInt(w, "pellet_count_normal", 20);
		}

		// Super Shotgun
		if (weapons.isMember("supershotgun") && weapons["supershotgun"].isObject())
		{
			const Json::Value& w = weapons["supershotgun"];
			g_config.supershotgun.damage_min = GetJsonInt(w, "damage_min", 7);
			g_config.supershotgun.damage_max = GetJsonInt(w, "damage_max", 10);
			g_config.supershotgun.damage_energy_min = GetJsonInt(w, "damage_energy_min", 14);
			g_config.supershotgun.damage_energy_max = GetJsonInt(w, "damage_energy_max", 16);
			g_config.supershotgun.kick = GetJsonInt(w, "kick", 17);
			g_config.supershotgun.pellet_count = GetJsonInt(w, "pellet_count", 20);
		}

		// Machinegun
		if (weapons.isMember("machinegun") && weapons["machinegun"].isObject())
		{
			const Json::Value& w = weapons["machinegun"];
			g_config.machinegun.damage_min = GetJsonInt(w, "damage_min", 7);
			g_config.machinegun.damage_max = GetJsonInt(w, "damage_max", 10);
			g_config.machinegun.kick = GetJsonInt(w, "kick", 2);
			g_config.machinegun.tracer_damage = GetJsonInt(w, "tracer_damage", 40);
			g_config.machinegun.tracer_cooldown_ms = GetJsonInt(w, "tracer_cooldown_ms", 500);
		}

		// Chaingun
		if (weapons.isMember("chaingun") && weapons["chaingun"].isObject())
		{
			const Json::Value& w = weapons["chaingun"];
			g_config.chaingun.damage_min = GetJsonInt(w, "damage_min", 7);
			g_config.chaingun.damage_max = GetJsonInt(w, "damage_max", 11);
			g_config.chaingun.kick = GetJsonInt(w, "kick", 2);
			g_config.chaingun.tracer_damage = GetJsonInt(w, "tracer_damage", 20);
			g_config.chaingun.tracer_cooldown_ms = GetJsonInt(w, "tracer_cooldown_ms", 300);
		}

		// Grenade
		if (weapons.isMember("grenade") && weapons["grenade"].isObject())
		{
			const Json::Value& w = weapons["grenade"];
			g_config.grenade.damage = GetJsonInt(w, "damage", 125);
			g_config.grenade.radius_offset = GetJsonFloat(w, "radius_offset", 40.0f);
		}

		// Grenade Launcher
		if (weapons.isMember("grenadelauncher") && weapons["grenadelauncher"].isObject())
		{
			const Json::Value& w = weapons["grenadelauncher"];
			g_config.grenadelauncher.damage_normal = GetJsonInt(w, "damage_normal", 115);
			g_config.grenadelauncher.damage_napalm = GetJsonInt(w, "damage_napalm", 95);
			g_config.grenadelauncher.radius_normal = GetJsonFloat(w, "radius_normal", 155.0f);
			g_config.grenadelauncher.radius_napalm = GetJsonFloat(w, "radius_napalm", 135.0f);
			g_config.grenadelauncher.speed = GetJsonInt(w, "speed", 1200);
		}

		// Rocket Launcher
		if (weapons.isMember("rocket") && weapons["rocket"].isObject())
		{
			const Json::Value& w = weapons["rocket"];
			g_config.rocket.damage_min = GetJsonInt(w, "damage_min", 100);
			g_config.rocket.damage_max = GetJsonInt(w, "damage_max", 120);
			g_config.rocket.speed = GetJsonInt(w, "speed", 1230);
			g_config.rocket.radius = GetJsonInt(w, "radius", 125);
		}

		// Railgun
		if (weapons.isMember("railgun") && weapons["railgun"].isObject())
		{
			const Json::Value& w = weapons["railgun"];
			g_config.railgun.damage = GetJsonInt(w, "damage", 150);
			g_config.railgun.damage_horde = GetJsonInt(w, "damage_horde", 225);
			g_config.railgun.kick = GetJsonInt(w, "kick", 285);
		}

		// BFG
		if (weapons.isMember("bfg") && weapons["bfg"].isObject())
		{
			const Json::Value& w = weapons["bfg"];
			g_config.bfg.damage = GetJsonInt(w, "damage", 700);
			g_config.bfg.radius = GetJsonFloat(w, "radius", 1000.0f);
			g_config.bfg.speed = GetJsonInt(w, "speed", 600);
			g_config.bfg.ammo_normal = GetJsonInt(w, "ammo_normal", 50);
			g_config.bfg.ammo_slide = GetJsonInt(w, "ammo_slide", 25);
		}

		// Ion Ripper (Xatrix)
		if (weapons.isMember("ionripper") && weapons["ionripper"].isObject())
		{
			const Json::Value& w = weapons["ionripper"];
			g_config.ionripper.damage = GetJsonInt(w, "damage", 50);
		}

		// Phalanx (Xatrix)
		if (weapons.isMember("phalanx") && weapons["phalanx"].isObject())
		{
			const Json::Value& w = weapons["phalanx"];
			g_config.phalanx.damage_min = GetJsonInt(w, "damage_min", 80);
			g_config.phalanx.damage_max = GetJsonInt(w, "damage_max", 95);
			g_config.phalanx.radius_damage = GetJsonInt(w, "radius_damage", 120);
			g_config.phalanx.damage_radius = GetJsonInt(w, "damage_radius", 120);
		}

		// Plasma Beam (Rogue)
		if (weapons.isMember("plasmabeam") && weapons["plasmabeam"].isObject())
		{
			const Json::Value& w = weapons["plasmabeam"];
			g_config.plasmabeam.damage = GetJsonInt(w, "damage", 145);
			g_config.plasmabeam.damage_singleplayer = GetJsonInt(w, "damage_singleplayer", 135);
			g_config.plasmabeam.kick = GetJsonInt(w, "kick", 3);
		}

		// ETF Rifle (Rogue)
		if (weapons.isMember("etfrifle") && weapons["etfrifle"].isObject())
		{
			const Json::Value& w = weapons["etfrifle"];
			g_config.etfrifle.kick_normal = GetJsonInt(w, "kick_normal", 3);
			g_config.etfrifle.kick_homing = GetJsonInt(w, "kick_homing", 75);
		}
	}

	// Load deployables configs
	if (root.isMember("deployables") && root["deployables"].isObject())
	{
		const Json::Value& deployables = root["deployables"];

		// Prox Mine
		if (deployables.isMember("prox_mine") && deployables["prox_mine"].isObject())
		{
			const Json::Value& p = deployables["prox_mine"];
			g_config.prox_mine.damage = GetJsonInt(p, "damage", 95);
			g_config.prox_mine.damage_radius = GetJsonInt(p, "damage_radius", 220);
			g_config.prox_mine.health = GetJsonInt(p, "health", 30);
			g_config.prox_mine.time_to_live_sec = GetJsonInt(p, "time_to_live_sec", 45);
			g_config.prox_mine.time_delay_ms = GetJsonInt(p, "time_delay_ms", 350);
			g_config.prox_mine.damage_open_multiplier = GetJsonFloat(p, "damage_open_multiplier", 1.5f);
			g_config.prox_mine.bound_size = GetJsonFloat(p, "bound_size", 96.0f);
		}

		// Laser
		if (deployables.isMember("laser") && deployables["laser"].isObject())
		{
			const Json::Value& l = deployables["laser"];
			g_config.laser.health_base = GetJsonInt(l, "health_base", 150);
			g_config.laser.health_addon_per_wave = GetJsonInt(l, "health_addon_per_wave", 120);
			g_config.laser.damage_initial = GetJsonInt(l, "damage_initial", 1);
			g_config.laser.damage_addon_per_wave = GetJsonInt(l, "damage_addon_per_wave", 4);
		}

		// Trap
		if (deployables.isMember("trap") && deployables["trap"].isObject())
		{
			const Json::Value& t = deployables["trap"];
			g_config.trap.speed_min = GetJsonInt(t, "speed_min", 500);
			g_config.trap.speed_max = GetJsonInt(t, "speed_max", 900);
			g_config.trap.timer_sec = GetJsonInt(t, "timer_sec", 5);
			g_config.trap.pull_radius = GetJsonFloat(t, "pull_radius", 400.0f);
			g_config.trap.pull_speed_monster = GetJsonFloat(t, "pull_speed_monster", 210.0f);
			g_config.trap.pull_speed_player = GetJsonFloat(t, "pull_speed_player", 290.0f);
			g_config.trap.duration_sec = GetJsonInt(t, "duration_sec", 80);
			g_config.trap.health = GetJsonInt(t, "health", 125);
			g_config.trap.explosion_damage = GetJsonInt(t, "explosion_damage", 300);
			g_config.trap.explosion_radius = GetJsonInt(t, "explosion_radius", 100);
		}

		// Tesla
		if (deployables.isMember("tesla") && deployables["tesla"].isObject())
		{
			const Json::Value& t = deployables["tesla"];
			g_config.tesla.damage = GetJsonInt(t, "damage", 4);
			g_config.tesla.damage_radius = GetJsonInt(t, "damage_radius", 200);
			g_config.tesla.health = GetJsonInt(t, "health", 50);
			g_config.tesla.time_to_live_sec = GetJsonInt(t, "time_to_live_sec", 30);
			g_config.tesla.activate_time_ms = GetJsonInt(t, "activate_time_ms", 1200);
			g_config.tesla.explosion_damage_multiplier = GetJsonInt(t, "explosion_damage_multiplier", 50);
			g_config.tesla.explosion_radius = GetJsonInt(t, "explosion_radius", 200);
			g_config.tesla.knockback = GetJsonInt(t, "knockback", 8);
		}

		// Doppleganger
		if (deployables.isMember("doppleganger") && deployables["doppleganger"].isObject())
		{
			const Json::Value& d = deployables["doppleganger"];
			g_config.doppleganger.time_to_live_sec = GetJsonInt(d, "time_to_live_sec", 30);
			g_config.doppleganger.health_base = GetJsonInt(d, "health_base", 100);
			g_config.doppleganger.explosion_damage = GetJsonInt(d, "explosion_damage", 160);
			g_config.doppleganger.explosion_radius = GetJsonInt(d, "explosion_radius", 140);
		}
	}

	// Load special abilities configs
	if (root.isMember("special_abilities") && root["special_abilities"].isObject())
	{
		const Json::Value& abilities = root["special_abilities"];

		// Bombspell
		if (abilities.isMember("bomb_spell") && abilities["bomb_spell"].isObject())
		{
			const Json::Value& b = abilities["bomb_spell"];
			g_config.bomb_spell.initial_damage = GetJsonInt(b, "initial_damage", 75);
			g_config.bomb_spell.addon_damage = GetJsonInt(b, "addon_damage", 10);
			g_config.bomb_spell.damage_radius = GetJsonInt(b, "damage_radius", 150);
			g_config.bomb_spell.duration_sec = GetJsonInt(b, "duration_sec", 5);
			g_config.bomb_spell.forward_cooldown_ms = GetJsonInt(b, "forward_cooldown_ms", 1500);
			g_config.bomb_spell.area_cooldown_ms = GetJsonInt(b, "area_cooldown_ms", 10000);
			g_config.bomb_spell.max_height = GetJsonInt(b, "max_height", 256);
			g_config.bomb_spell.step_size = GetJsonInt(b, "step_size", 96);
			g_config.bomb_spell.carpet_width = GetJsonInt(b, "carpet_width", 200);
		}
	}

	// Load hook config
	if (root.isMember("hook") && root["hook"].isObject())
	{
		const Json::Value& h = root["hook"];
		g_config.hook.speed = GetJsonInt(h, "speed", 900);
		g_config.hook.pull_speed = GetJsonInt(h, "pull_speed", 700);
		g_config.hook.damage = GetJsonInt(h, "damage", 20);
		g_config.hook.init_damage = GetJsonInt(h, "init_damage", 10);
		g_config.hook.max_damage = GetJsonInt(h, "max_damage", 20);
		g_config.hook.max_time_sec = GetJsonInt(h, "max_time_sec", 5);
		g_config.hook.delay_sec = GetJsonFloat(h, "delay_sec", 0.2f);
		g_config.hook.bot_chain_speed = GetJsonInt(h, "bot_chain_speed", 800);
		g_config.hook.bot_throw_speed = GetJsonInt(h, "bot_throw_speed", 1800);
		g_config.hook.allow_sky_attach = h.get("allow_sky_attach", false).asBool();
	}

	// Load grapple config
	if (root.isMember("grapple") && root["grapple"].isObject())
	{
		const Json::Value& g = root["grapple"];
		g_config.grapple.fly_speed = GetJsonInt(g, "fly_speed", 650);
		g_config.grapple.pull_speed = GetJsonInt(g, "pull_speed", 650);
		g_config.grapple.damage = GetJsonInt(g, "damage", 10);
	}

	// Load ammo regeneration config
	if (root.isMember("ammo_regen") && root["ammo_regen"].isObject())
	{
		const Json::Value& ammo_regen = root["ammo_regen"];
		g_config.ammo_regen.enabled = ammo_regen.get("enabled", true).asBool();

		if (ammo_regen.isMember("rates") && ammo_regen["rates"].isObject())
		{
			const Json::Value& rates = ammo_regen["rates"];

			// Helper lambda to parse rate config
			auto parseRate = [](const Json::Value& rate, int defaultQty, int defaultInterval) -> AmmoRegenRateConfig {
				AmmoRegenRateConfig config;
				config.quantity = GetJsonInt(rate, "quantity", defaultQty);
				config.interval_ms = GetJsonInt(rate, "interval_ms", defaultInterval);
				return config;
			};

			if (rates.isMember("bullets")) g_config.ammo_regen.bullets = parseRate(rates["bullets"], 10, 3000);
			if (rates.isMember("shells")) g_config.ammo_regen.shells = parseRate(rates["shells"], 5, 3000);
			if (rates.isMember("grenades")) g_config.ammo_regen.grenades = parseRate(rates["grenades"], 3, 4000);
			if (rates.isMember("rockets")) g_config.ammo_regen.rockets = parseRate(rates["rockets"], 2, 5000);
			if (rates.isMember("cells")) g_config.ammo_regen.cells = parseRate(rates["cells"], 10, 3000);
			if (rates.isMember("slugs")) g_config.ammo_regen.slugs = parseRate(rates["slugs"], 5, 4000);
			if (rates.isMember("magslug")) g_config.ammo_regen.magslug = parseRate(rates["magslug"], 3, 5000);
			if (rates.isMember("prox")) g_config.ammo_regen.prox = parseRate(rates["prox"], 1, 6000);
			if (rates.isMember("trap")) g_config.ammo_regen.trap = parseRate(rates["trap"], 1, 6000);
			if (rates.isMember("tesla")) g_config.ammo_regen.tesla = parseRate(rates["tesla"], 2, 5000);
		}
	}

	gi.Com_PrintFmt("Config: Successfully loaded config/player_config.json\n");
	gi.Com_PrintFmt("Config: Entity limits - Sentries: {}, Lasers: {}, Teslas: {}, Barrels: {}, Prox: {}, Traps: {}, Summons: {}\n",
		g_config.entity_limits.max_sentries,
		g_config.entity_limits.max_lasers,
		g_config.entity_limits.max_teslas,
		g_config.entity_limits.max_barrels,
		g_config.entity_limits.max_prox,
		g_config.entity_limits.max_traps,
		g_config.entity_limits.max_summons);

	// Load monster configs
	Config_LoadMonsters(basedir);
}

void Config_Reload()
{
	gi.Com_PrintFmt("Config: Reloading configuration...\n");

	// Get the basedir from game locals or cvar
	cvar_t* gamedir = gi.cvar("game", "", CVAR_NOFLAGS);
	std::string basedir = gi.cvar("basedir", "", CVAR_NOFLAGS)->string;

	if (basedir.empty())
	{
		gi.Com_PrintFmt("Config: Could not determine basedir\n");
		return;
	}

	// Ensure trailing slash
	if (basedir.back() != '/' && basedir.back() != '\\')
		basedir += "/";

	// Add game directory if set
	if (gamedir && gamedir->string[0])
		basedir += std::string(gamedir->string) + "/";
	else
		basedir += "baseq2/";

	Config_Load(basedir.c_str());
}

// Get monster configuration by MonsterTypeID
const MonsterStatsConfig* GetMonsterConfig(uint8_t monster_type_id)
{
	auto it = g_config.monsters.monsters.find(monster_type_id);
	if (it != g_config.monsters.monsters.end())
	{
		return &it->second;
	}
	return nullptr;
}

// Get specific weapon damage for a monster
int GetMonsterWeaponDamage(uint8_t monster_type_id, const char* weapon_name)
{
	const MonsterStatsConfig* config = GetMonsterConfig(monster_type_id);
	if (!config)
		return 0;

	// Use static map for O(1) lookup instead of strcmp chain
	using WeaponDamageGetter = int MonsterWeaponDamage::*;
	static const std::unordered_map<std::string_view, WeaponDamageGetter> weapon_map = {
		{"blaster", &MonsterWeaponDamage::blaster},
		{"shotgun", &MonsterWeaponDamage::shotgun},
		{"machinegun", &MonsterWeaponDamage::machinegun},
		{"grenade", &MonsterWeaponDamage::grenade},
		{"rocket", &MonsterWeaponDamage::rocket},
		{"railgun", &MonsterWeaponDamage::railgun},
		{"bfg", &MonsterWeaponDamage::bfg},
		{"ionripper", &MonsterWeaponDamage::ionripper},
		{"hyperblaster", &MonsterWeaponDamage::hyperblaster},
		{"tracker", &MonsterWeaponDamage::tracker},
		{"plasma", &MonsterWeaponDamage::plasma},
		{"dabeam", &MonsterWeaponDamage::dabeam},
		{"heatbeam", &MonsterWeaponDamage::heatbeam},
		{"melee", &MonsterWeaponDamage::melee},
		{"slam", &MonsterWeaponDamage::slam},
		{"lightning", &MonsterWeaponDamage::lightning},
		{"flechette", &MonsterWeaponDamage::flechette}
	};

	auto it = weapon_map.find(weapon_name);
	return (it != weapon_map.end()) ? config->weapon_damage.*(it->second) : 0;
}
