#include "g_config.h"
#include "g_local.h"
#include <json/json.h>
#include <fstream>
#include <string>

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

void Config_SetDefaults()
{
	// Reset to default values (already set in struct definitions)
	g_config = GameConfig();
}

void Config_Load(const char* basedir)
{
	// Set defaults first
	Config_SetDefaults();

	// Build config file path
	std::string config_path = std::string(basedir) + "horde_config.json";

	// Try to open config file
	std::ifstream config_file(config_path, std::ifstream::binary);
	if (!config_file.is_open())
	{
		gi.Com_PrintFmt("Config: horde_config.json not found, using default values\n");
		gi.Com_PrintFmt("Config: You can create {} to customize settings\n", config_path);
		return;
	}

	// Parse JSON
	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errs;

	if (!Json::parseFromStream(builder, config_file, &root, &errs))
	{
		gi.Com_PrintFmt("Config: Failed to parse horde_config.json: {}\n", errs);
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
		}

		// Hyperblaster
		if (weapons.isMember("hyperblaster") && weapons["hyperblaster"].isObject())
		{
			const Json::Value& w = weapons["hyperblaster"];
			g_config.hyperblaster.damage = GetJsonInt(w, "damage", 15);
			g_config.hyperblaster.speed = GetJsonInt(w, "speed", 1700);
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
		}

		// Super Shotgun
		if (weapons.isMember("supershotgun") && weapons["supershotgun"].isObject())
		{
			const Json::Value& w = weapons["supershotgun"];
			g_config.supershotgun.damage_min = GetJsonInt(w, "damage_min", 4);
			g_config.supershotgun.damage_max = GetJsonInt(w, "damage_max", 6);
			g_config.supershotgun.kick = GetJsonInt(w, "kick", 12);
		}

		// Machinegun
		if (weapons.isMember("machinegun") && weapons["machinegun"].isObject())
		{
			const Json::Value& w = weapons["machinegun"];
			g_config.machinegun.damage = GetJsonInt(w, "damage", 8);
			g_config.machinegun.kick = GetJsonInt(w, "kick", 2);
		}

		// Chaingun
		if (weapons.isMember("chaingun") && weapons["chaingun"].isObject())
		{
			const Json::Value& w = weapons["chaingun"];
			g_config.chaingun.damage = GetJsonInt(w, "damage", 6);
			g_config.chaingun.kick = GetJsonInt(w, "kick", 2);
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
			g_config.grenadelauncher.damage_min = GetJsonInt(w, "damage_min", 100);
			g_config.grenadelauncher.damage_max = GetJsonInt(w, "damage_max", 120);
			g_config.grenadelauncher.radius = GetJsonFloat(w, "radius", 165.0f);
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

	gi.Com_PrintFmt("Config: Successfully loaded horde_config.json\n");
	gi.Com_PrintFmt("Config: Entity limits - Sentries: {}, Lasers: {}, Teslas: {}\n",
		g_config.entity_limits.max_sentries,
		g_config.entity_limits.max_lasers,
		g_config.entity_limits.max_teslas);
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
