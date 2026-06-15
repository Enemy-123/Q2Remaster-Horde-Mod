#include "g_config.h"
#include "g_local.h"
#include "shared.h"
#include "horde/horde_ids.h"
#include "horde/horde_monster_data.h"
#include "horde/weapon_id.h"
#include "horde/g_pvm.h"
#include <json/json.h>
#include <fstream>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <boost/container/flat_map.hpp>
#include <cctype>
#include <cstring>
#include <iterator>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// Global config instance
GameConfig g_config;

// Global variables for player levels (updated periodically in Horde_RunFrame)
int32_t g_lowest_player_level = 0;
int32_t g_highest_player_level = 0;

static void LuaValueToJson(lua_State* L, int index, Json::Value& out);
static void LuaTableToJson(lua_State* L, int index, Json::Value& out);

struct LuaNameMap
{
	const char* lua_name;
	const char* json_name;
};

static std::string ToLowerAscii(std::string_view value)
{
	std::string out;
	out.reserve(value.size());
	for (char c : value)
		out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
	return out;
}

static bool StartsWith(std::string_view value, std::string_view prefix)
{
	return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

static bool EndsWith(std::string_view value, std::string_view suffix)
{
	return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

static bool JsonFromLuaScalar(lua_State* L, int index, Json::Value& out)
{
	if (lua_isboolean(L, index) || lua_isnumber(L, index) || lua_isstring(L, index))
	{
		LuaValueToJson(L, index, out);
		return true;
	}

	return false;
}

static bool LuaTableIsArray(lua_State* L, int index, int& max_index)
{
	index = lua_absindex(L, index);
	max_index = 0;
	int count = 0;

	lua_pushnil(L);
	while (lua_next(L, index) != 0)
	{
		if (!lua_isnumber(L, -2))
		{
			lua_pop(L, 1);
			return false;
		}

		lua_Number n = lua_tonumber(L, -2);
		int key = static_cast<int>(n);
		if (key < 1 || static_cast<lua_Number>(key) != n)
		{
			lua_pop(L, 1);
			return false;
		}

		if (key > max_index)
			max_index = key;
		count++;
		lua_pop(L, 1);
	}

	return count > 0 && count == max_index;
}

static void LuaValueToJson(lua_State* L, int index, Json::Value& out)
{
	index = lua_absindex(L, index);

	switch (lua_type(L, index))
	{
	case LUA_TBOOLEAN:
		out = static_cast<bool>(lua_toboolean(L, index));
		break;
	case LUA_TNUMBER:
	{
		lua_Number value = lua_tonumber(L, index);
		double integer_part = 0.0;
		if (std::modf(static_cast<double>(value), &integer_part) == 0.0 &&
			integer_part >= static_cast<double>(std::numeric_limits<int>::min()) &&
			integer_part <= static_cast<double>(std::numeric_limits<int>::max()))
		{
			out = static_cast<int>(integer_part);
		}
		else
		{
			out = static_cast<double>(value);
		}
		break;
	}
	case LUA_TSTRING:
		out = lua_tostring(L, index);
		break;
	case LUA_TTABLE:
		LuaTableToJson(L, index, out);
		break;
	default:
		out = Json::Value();
		break;
	}
}

static void LuaTableToJson(lua_State* L, int index, Json::Value& out)
{
	index = lua_absindex(L, index);
	int max_index = 0;

	if (LuaTableIsArray(L, index, max_index))
	{
		out = Json::Value(Json::arrayValue);
		for (int i = 1; i <= max_index; ++i)
		{
			lua_rawgeti(L, index, i);
			Json::Value value;
			LuaValueToJson(L, -1, value);
			out.append(value);
			lua_pop(L, 1);
		}
		return;
	}

	out = Json::Value(Json::objectValue);
	lua_pushnil(L);
	while (lua_next(L, index) != 0)
	{
		std::string key;
		if (lua_isstring(L, -2))
		{
			key = lua_tostring(L, -2);
		}
		else if (lua_isnumber(L, -2))
		{
			key = std::to_string(static_cast<int>(lua_tonumber(L, -2)));
		}
		else
		{
			lua_pop(L, 1);
			continue;
		}

		Json::Value value;
		LuaValueToJson(L, -1, value);
		out[key] = value;
		lua_pop(L, 1);
	}
}

static void ParseFlatGlobalWeapon(Json::Value& root, std::string_view name, const Json::Value& value,
	std::string_view prefix, const char* section)
{
	if (!StartsWith(name, prefix))
		return;

	std::string weapon = ToLowerAscii(name.substr(prefix.size()));
	root[section][weapon] = value;
}

static bool ParseFlatMonsterField(Json::Value& root, std::string_view body, const Json::Value& value)
{
	static constexpr LuaNameMap kMonsterFields[] = {
		{"POWER_ARMOR_POWER", "power_armor_power"},
		{"POWER_ARMOR_TYPE", "power_armor_type"},
		{"POWER_ARMOR_SCALE", "power_armor_scale"},
		{"HEALTH_SCALE", "health_scale"},
		{"DAMAGE_SCALE", "damage_scale"},
		{"SPEED_SCALE", "speed_scale"},
		{"ARMOR_SCALE", "armor_scale"},
		{"POWER_ARMOR", "power_armor"},
		{"ARMOR_POWER", "armor_power"},
		{"ARMOR_TYPE", "armor_type"},
		{"HEALTH", "health"},
	};

	for (const LuaNameMap& field : kMonsterFields)
	{
		std::string suffix = std::string("_") + field.lua_name;
		if (!EndsWith(body, suffix))
			continue;

		std::string monster_name = ToLowerAscii(body.substr(0, body.size() - suffix.size()));
		root["monsters"][monster_name][field.json_name] = value;
		return true;
	}

	return false;
}

static bool ParseFlatMonsterWeapon(Json::Value& root, std::string_view body, const Json::Value& value)
{
	struct WeaponInfix
	{
		const char* lua_infix;
		const char* json_section;
		const char* json_field;
	};

	static constexpr WeaponInfix kWeaponInfixes[] = {
		{"_ADDON_DAMAGE_", "weapon_damage", "addon_damage"},
		{"_DAMAGE_MAX_", "weapon_damage", "damage_max"},
		{"_DAMAGE_", "weapon_damage", "damage"},
		{"_SPEED_", "weapon_speed", nullptr},
	};

	for (const WeaponInfix& infix : kWeaponInfixes)
	{
		size_t pos = body.find(infix.lua_infix);
		if (pos == std::string_view::npos)
			continue;

		std::string monster_name = ToLowerAscii(body.substr(0, pos));
		std::string weapon_name = ToLowerAscii(body.substr(pos + strlen(infix.lua_infix)));
		if (monster_name.empty() || weapon_name.empty())
			return false;

		if (infix.json_field)
			root["monsters"][monster_name][infix.json_section][weapon_name][infix.json_field] = value;
		else
			root["monsters"][monster_name][infix.json_section][weapon_name] = value;
		return true;
	}

	return false;
}

static bool ParseFlatMonsterLevel(Json::Value& root, std::string_view body, const Json::Value& value)
{
	static constexpr LuaNameMap kLevelFields[] = {
		{"INITIAL_POWER_ARMOR", "initial_power_armor"},
		{"ADDON_POWER_ARMOR", "addon_power_armor"},
		{"INITIAL_HEALTH", "initial_health"},
		{"ADDON_HEALTH", "addon_health"},
		{"INITIAL_ARMOR", "initial_armor"},
		{"ADDON_ARMOR", "addon_armor"},
	};

	for (const LuaNameMap& field : kLevelFields)
	{
		std::string suffix = std::string("_") + field.lua_name;
		if (!EndsWith(body, suffix))
			continue;

		std::string monster_name = ToLowerAscii(body.substr(0, body.size() - suffix.size()));
		root["monster_level_scaling"][monster_name][field.json_name] = value;
		return true;
	}

	return false;
}

static void ParseFlatMonsterGlobal(Json::Value& root, std::string_view name, const Json::Value& value)
{
	ParseFlatGlobalWeapon(root, name, value, "GLOBAL_DAMAGE_", "global_weapon_damage");
	ParseFlatGlobalWeapon(root, name, value, "GLOBAL_SPEED_", "global_weapon_speed");
	ParseFlatGlobalWeapon(root, name, value, "GLOBAL_RADIUS_", "global_weapon_radius");

	if (StartsWith(name, "MONSTER_LEVEL_"))
	{
		ParseFlatMonsterLevel(root, name.substr(strlen("MONSTER_LEVEL_")), value);
		return;
	}

	if (StartsWith(name, "MONSTER_"))
	{
		std::string_view body = name.substr(strlen("MONSTER_"));
		if (!ParseFlatMonsterField(root, body, value))
			ParseFlatMonsterWeapon(root, body, value);
	}
}

static void ParseFlatMapGlobal(Json::Value& root, std::string_view name, const Json::Value& value)
{
	if (StartsWith(name, "MAP_CAP_"))
	{
		root["default_caps"][ToLowerAscii(name.substr(strlen("MAP_CAP_"))) + "_map"] = value;
		return;
	}

	if (name == "MAP_DEFAULT_ENABLE_GRID")
	{
		root["default_settings"]["enable_grid"] = value;
		return;
	}

	if (!StartsWith(name, "MAP_"))
		return;

	static constexpr LuaNameMap kMapFields[] = {
		{"MONSTER_CAP", "monster_cap"},
		{"ENABLE_LOADENT", "enable_loadent"},
		{"ENABLE_GRID", "enable_grid"},
		{"BOSS_SIZE", "boss_size"},  // must precede SIZE: "..._BOSS_SIZE" also ends with "_SIZE"
		{"SIZE", "map_size"},
	};

	std::string_view body = name.substr(strlen("MAP_"));
	for (const LuaNameMap& field : kMapFields)
	{
		std::string suffix = std::string("_") + field.lua_name;
		if (!EndsWith(body, suffix))
			continue;

		std::string map_name = ToLowerAscii(body.substr(0, body.size() - suffix.size()));
		size_t slash_pos = 0;
		while ((slash_pos = map_name.find("_slash_", slash_pos)) != std::string::npos)
		{
			map_name.replace(slash_pos, strlen("_slash_"), "/");
			slash_pos += 1;
		}
		root["map_overrides"][map_name][field.json_name] = value;
		return;
	}
}

static bool ParseNamedFlatGroup(Json::Value& root, std::string_view name, const Json::Value& value,
	std::string_view prefix, const char* root_section, const LuaNameMap* groups, size_t group_count)
{
	if (!StartsWith(name, prefix))
		return false;

	std::string_view body = name.substr(prefix.size());
	for (size_t i = 0; i < group_count; ++i)
	{
		const LuaNameMap& group = groups[i];
		std::string group_prefix = std::string(group.lua_name) + "_";
		if (!StartsWith(body, group_prefix))
			continue;

		std::string field = ToLowerAscii(body.substr(group_prefix.size()));
		root[root_section][group.json_name][field] = value;
		return true;
	}

	return false;
}

static void ParseFlatPlayerGlobal(Json::Value& root, std::string_view name, const Json::Value& value)
{
	static constexpr LuaNameMap kWeapons[] = {
		{"GRENADELAUNCHER", "grenadelauncher"},
		{"HYPERBLASTER", "hyperblaster"},
		{"SUPERSHOTGUN", "supershotgun"},
		{"CANNON20MM", "cannon20mm"},
		{"PLASMABEAM", "plasmabeam"},
		{"MACHINEGUN", "machinegun"},
		{"IONRIPPER", "ionripper"},
		{"ETFRIFLE", "etfrifle"},
		{"CHAINGUN", "chaingun"},
		{"BLASTER", "blaster"},
		{"SHOTGUN", "shotgun"},
		{"GRENADE", "grenade"},
		{"PHALANX", "phalanx"},
		{"TRACKER", "tracker"},
		{"ROCKET", "rocket"},
		{"RAILGUN", "railgun"},
		{"BFG", "bfg"},
	};

	static constexpr LuaNameMap kDeployables[] = {
		{"EXPLODING_BARREL", "exploding_barrel"},
		{"DOPPLEGANGER", "doppleganger"},
		{"SENTRYGUN", "sentrygun"},
		{"PROX_MINE", "prox_mine"},
		{"LASER", "laser"},
		{"TESLA", "tesla"},
		{"TRAP", "trap"},
	};

	static constexpr LuaNameMap kAbilities[] = {
		{"EXPLODING_BARREL", "exploding_barrel"},
		{"BOMB_SPELL", "bomb_spell"},
		{"FIREBALL", "fireball"},
		{"SUMMON", "summon"},
	};

	static constexpr LuaNameMap kAmmoRates[] = {
		{"GRENADES", "grenades"},
		{"BULLETS", "bullets"},
		{"ROCKETS", "rockets"},
		{"SHELLS", "shells"},
		{"CELLS", "cells"},
		{"SLUGS", "slugs"},
		{"MAGSLUG", "magslug"},
		{"TESLA", "tesla"},
		{"PROX", "prox"},
		{"TRAP", "trap"},
	};

	if (StartsWith(name, "ENTITY_LIMIT_"))
	{
		root["entity_limits"][ToLowerAscii(name.substr(strlen("ENTITY_LIMIT_")))] = value;
		return;
	}

	if (ParseNamedFlatGroup(root, name, value, "WEAPON_", "weapons", kWeapons, std::size(kWeapons)) ||
		ParseNamedFlatGroup(root, name, value, "DEPLOYABLE_", "deployables", kDeployables, std::size(kDeployables)) ||
		ParseNamedFlatGroup(root, name, value, "ABILITY_", "special_abilities", kAbilities, std::size(kAbilities)))
	{
		return;
	}

	if (StartsWith(name, "HOOK_"))
	{
		root["hook"][ToLowerAscii(name.substr(strlen("HOOK_")))] = value;
		return;
	}

	if (StartsWith(name, "GRAPPLE_"))
	{
		root["grapple"][ToLowerAscii(name.substr(strlen("GRAPPLE_")))] = value;
		return;
	}

	if (name == "AMMO_REGEN_ENABLED")
	{
		root["ammo_regen"]["enabled"] = value;
		return;
	}

	if (StartsWith(name, "AMMO_REGEN_"))
	{
		std::string_view body = name.substr(strlen("AMMO_REGEN_"));
		for (const LuaNameMap& ammo : kAmmoRates)
		{
			std::string ammo_prefix = std::string(ammo.lua_name) + "_";
			if (StartsWith(body, ammo_prefix))
			{
				root["ammo_regen"]["rates"][ammo.json_name][ToLowerAscii(body.substr(ammo_prefix.size()))] = value;
				return;
			}
		}
	}

	if (StartsWith(name, "POWER_CUBES_REGEN_"))
	{
		root["power_cubes_regen"][ToLowerAscii(name.substr(strlen("POWER_CUBES_REGEN_")))] = value;
		return;
	}

	if (StartsWith(name, "POWER_CUBES_"))
	{
		root["power_cubes"][ToLowerAscii(name.substr(strlen("POWER_CUBES_")))] = value;
		return;
	}
}

static bool LoadFlatLuaGlobals(lua_State* L, const std::string& config_path, Json::Value& root)
{
	root = Json::Value(Json::objectValue);
	const bool is_monsters = config_path.find("monsters.lua") != std::string::npos;
	const bool is_maps = config_path.find("maps_config.lua") != std::string::npos;
	const bool is_player =
		config_path.find("player_horde_config.lua") != std::string::npos ||
		config_path.find("player_pvm_config.lua") != std::string::npos;

	lua_pushglobaltable(L);
	lua_pushnil(L);
	while (lua_next(L, -2) != 0)
	{
		if (lua_isstring(L, -2))
		{
			Json::Value value;
			if (JsonFromLuaScalar(L, -1, value))
			{
				std::string name = lua_tostring(L, -2);
				if (is_monsters)
					ParseFlatMonsterGlobal(root, name, value);
				else if (is_maps)
					ParseFlatMapGlobal(root, name, value);
				else if (is_player)
					ParseFlatPlayerGlobal(root, name, value);
			}
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	return is_monsters || is_maps || is_player;
}

static bool LoadLuaConfig(const std::string& config_path, Json::Value& root)
{
	lua_State* L = luaL_newstate();
	if (!L)
	{
		gi.Com_PrintFmt("Config: Failed to create Lua state for {}\n", config_path);
		return false;
	}

	luaL_openlibs(L);
	if (luaL_loadfile(L, config_path.c_str()) != LUA_OK)
	{
		gi.Com_PrintFmt("Config: Failed to load {}: {}\n", config_path, lua_tostring(L, -1));
		lua_close(L);
		return false;
	}

	const int base_top = lua_gettop(L) - 1;
	if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
	{
		gi.Com_PrintFmt("Config: Failed to execute {}: {}\n", config_path, lua_tostring(L, -1));
		lua_close(L);
		return false;
	}

	const int return_count = lua_gettop(L) - base_top;
	if (return_count > 0 && lua_istable(L, -1))
	{
		LuaTableToJson(L, -1, root);
		lua_close(L);
		return true;
	}

	if (!LoadFlatLuaGlobals(L, config_path, root))
	{
		gi.Com_PrintFmt("Config: {} must return a table or use Horde flat globals\n", config_path);
		lua_close(L);
		return false;
	}

	lua_close(L);
	return true;
}

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
	g_config.use_sigmoid_scaling = false;
	g_config.use_sigmoid_scaling_bosses_only = false;
	g_config.use_sigmoid_scaling_except_bosses = false;
}

void Config_LoadMonsters(const char* basedir)
{
	// Build config file path
	std::string config_path = std::string(basedir) + "config/monsters.lua";

	Json::Value root;
	if (!LoadLuaConfig(config_path, root))
	{
		gi.Com_PrintFmt("Config: config/monsters.lua not loaded, using default monster values\n");
		return;
	}

	// Load global weapon damage - OPTIMIZED: uses array-based storage with enum indexing
	if (root.isMember("global_weapon_damage") && root["global_weapon_damage"].isObject())
	{
		const Json::Value& gwd = root["global_weapon_damage"];
		// Use helper .set() method which converts weapon name to enum and stores in array
		for (const auto& weapon_name : gwd.getMemberNames())
		{
			if (gwd[weapon_name].isInt())
			{
				horde::WeaponID weapon_id = horde::WeaponRegistry::GetWeaponID(weapon_name.c_str());
				g_config.global_weapon_damage.set(weapon_id, gwd[weapon_name].asInt());
			}
		}
		gi.Com_PrintFmt("Config: Loaded global weapon damage values\n");
	}

	// Load global weapon speed - OPTIMIZED: uses array-based storage with enum indexing
	if (root.isMember("global_weapon_speed") && root["global_weapon_speed"].isObject())
	{
		const Json::Value& gws = root["global_weapon_speed"];
		// Use helper .set() method which converts weapon name to enum and stores in array
		for (const auto& weapon_name : gws.getMemberNames())
		{
			if (gws[weapon_name].isInt())
			{
				horde::WeaponID weapon_id = horde::WeaponRegistry::GetWeaponID(weapon_name.c_str());
				g_config.global_weapon_speed.set(weapon_id, gws[weapon_name].asInt());
			}
		}
		gi.Com_PrintFmt("Config: Loaded global weapon speed values\n");
	}

	// Load global weapon radius - OPTIMIZED: uses array-based storage with enum indexing
	if (root.isMember("global_weapon_radius") && root["global_weapon_radius"].isObject())
	{
		const Json::Value& gwr = root["global_weapon_radius"];
		// Use helper .set() method which converts weapon name to enum and stores in array
		for (const auto& weapon_name : gwr.getMemberNames())
		{
			if (gwr[weapon_name].isDouble() || gwr[weapon_name].isInt())
			{
				horde::WeaponID weapon_id = horde::WeaponRegistry::GetWeaponID(weapon_name.c_str());
				g_config.global_weapon_radius.set(weapon_id, static_cast<float>(gwr[weapon_name].asDouble()));
			}
		}
		gi.Com_PrintFmt("Config: Loaded global weapon radius values\n");
	}

	// Load monsters
	if (root.isMember("monsters") && root["monsters"].isObject())
	{
		const Json::Value& monsters = root["monsters"];
		int loaded_count = 0;

		for (const auto& monster_name : monsters.getMemberNames())
		{
			// Prepend "monster_" to the name from JSON to match the registry
			std::string full_classname = "monster_" + monster_name;
			horde::MonsterTypeID monster_type = horde::MonsterTypeRegistry::GetTypeID(full_classname.c_str());
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

			// Also check for "power_armor" as an alias for armor_power (normal armor)
			if (monster_data.isMember("power_armor") && monster_data["power_armor"].isInt())
			{
				config.armor_power = GetJsonInt(monster_data, "power_armor", 0);
			}

			// Load armor types
			if (monster_data.isMember("power_armor_type") && monster_data["power_armor_type"].isString())
			{
				config.power_armor_type = GetArmorTypeFromString(monster_data["power_armor_type"].asString());
			}
			if (monster_data.isMember("armor_type") && monster_data["armor_type"].isString())
			{
				config.armor_type = GetArmorTypeFromString(monster_data["armor_type"].asString());
			}

			// Load scaling multipliers
			config.health_scale = GetJsonFloat(monster_data, "health_scale", 1.0f);
			config.damage_scale = GetJsonFloat(monster_data, "damage_scale", 1.0f);
			config.speed_scale = GetJsonFloat(monster_data, "speed_scale", 1.0f);
			config.armor_scale = GetJsonFloat(monster_data, "armor_scale", 1.0f);
			config.power_armor_scale = GetJsonFloat(monster_data, "power_armor_scale", 1.0f);

			// Load weapon damage overrides - OPTIMIZED using array-based WeaponID lookup
			if (monster_data.isMember("weapon_damage") && monster_data["weapon_damage"].isObject())
			{
				const Json::Value &overrides = monster_data["weapon_damage"];
				for (const auto &weapon_name : overrides.getMemberNames())
				{
					if (overrides[weapon_name].isInt())
					{
						// Convert weapon name string to WeaponID enum (one-time cost during config loading)
						horde::WeaponID weapon_id = horde::WeaponRegistry::GetWeaponID(weapon_name.c_str());
						if (weapon_id != horde::WeaponID::UNKNOWN)
						{
							// Store in array using WeaponID as index for O(1) lookup
							config.weapon_damage_overrides[static_cast<size_t>(weapon_id)] = overrides[weapon_name].asInt();
						}
						else
						{
							gi.Com_PrintFmt("Config: WARNING - Unknown weapon '{}' in {} config\n", weapon_name, monster_name);
						}
					}
					else if (overrides[weapon_name].isObject())
					{
						horde::WeaponID weapon_id = horde::WeaponRegistry::GetWeaponID(weapon_name.c_str());
						if (weapon_id != horde::WeaponID::UNKNOWN)
						{
							const size_t weapon_index = static_cast<size_t>(weapon_id);
							const Json::Value& damage_entry = overrides[weapon_name];
							if (damage_entry.isMember("damage") && damage_entry["damage"].isInt())
								config.weapon_damage_overrides[weapon_index] = damage_entry["damage"].asInt();
							if (damage_entry.isMember("damage_max") && damage_entry["damage_max"].isInt())
								config.weapon_damage_max[weapon_index] = damage_entry["damage_max"].asInt();
							if (damage_entry.isMember("addon_damage") && damage_entry["addon_damage"].isInt())
								config.weapon_addon_damage[weapon_index] = damage_entry["addon_damage"].asInt();
						}
						else
						{
							gi.Com_PrintFmt("Config: WARNING - Unknown weapon '{}' in {} config\n", weapon_name, monster_name);
						}
					}
				}
			}

			// Load weapon speed overrides - OPTIMIZED using array-based WeaponID lookup
			if (monster_data.isMember("weapon_speed") && monster_data["weapon_speed"].isObject())
			{
				const Json::Value &overrides = monster_data["weapon_speed"];
				for (const auto &weapon_name : overrides.getMemberNames())
				{
					if (overrides[weapon_name].isInt())
					{
						// Convert weapon name string to WeaponID enum (one-time cost during config loading)
						horde::WeaponID weapon_id = horde::WeaponRegistry::GetWeaponID(weapon_name.c_str());
						if (weapon_id != horde::WeaponID::UNKNOWN)
						{
							// Store in array using WeaponID as index for O(1) lookup
							config.weapon_speed_overrides[static_cast<size_t>(weapon_id)] = overrides[weapon_name].asInt();
						}
						else
						{
							gi.Com_PrintFmt("Config: WARNING - Unknown weapon '{}' in {} config (speed)\n", weapon_name, monster_name);
						}
					}
				}
			}

			g_config.monsters.monsters[monster_id] = config;
			loaded_count++;
		}

		gi.Com_PrintFmt("Config: Loaded {} monster configurations from config/monsters.lua\n", loaded_count);

		// Verify all monster types have configs
		// Skip non-spawnable entities: misc_* (easter eggs, decorative), boss3_stand, commander_body
		int missing_count = 0;
		for (uint8_t type_id = 0; type_id < static_cast<uint8_t>(horde::MonsterTypeID::MAX_TYPES); type_id++)
		{
			const char* classname = horde::MonsterTypeRegistry::GetClassname(static_cast<horde::MonsterTypeID>(type_id));
			if (classname && classname[0])  // Valid monster type
			{
				// Skip non-spawnable entities that don't need config
				if (strncmp(classname, "misc_", 5) == 0 ||       // Easter eggs, decorative entities
				    strcmp(classname, "monster_boss3_stand") == 0 || // Decorative boss stand
				    strcmp(classname, "monster_commander_body") == 0) // Decorative body
				{
					continue;
				}

				auto it = g_config.monsters.monsters.find(type_id);
				if (it == g_config.monsters.monsters.end())
				{
					missing_count++;
					gi.Com_PrintFmt("WARNING: Monster '{}' (type_id {}) has NO config in monsters.lua!\n", classname, type_id);
				}
			}
		}

		if (missing_count > 0)
		{
			gi.Com_PrintFmt("ERROR: {} monsters are missing from config/monsters.lua! Add them or they will use hardcoded stats.\n", missing_count);
		}
		else
		{
			gi.Com_PrintFmt("Config: All monsters have configurations! ✓\n");
		}
	}

	// Load monster level scaling
	if (root.isMember("monster_level_scaling") && root["monster_level_scaling"].isObject())
	{
		const Json::Value& scaling = root["monster_level_scaling"];
		int loaded_count = 0;

		for (const auto& monster_name : scaling.getMemberNames())
		{
			const Json::Value& scaling_data = scaling[monster_name];
			if (!scaling_data.isObject())
				continue;

			std::string full_classname = "monster_" + monster_name;
			if (horde::MonsterTypeRegistry::GetTypeID(full_classname.c_str()) == horde::MonsterTypeID::UNKNOWN)
			{
				gi.Com_PrintFmt("Config: WARNING - Unknown monster '{}' in level scaling config, skipping\n", monster_name);
				continue;
			}

			MonsterLevelScaling level_scaling;
			level_scaling.initial_health = GetJsonInt(scaling_data, "initial_health", 100);
			level_scaling.addon_health = GetJsonInt(scaling_data, "addon_health", 10);
			level_scaling.initial_armor = GetJsonInt(scaling_data, "initial_armor", 0);
			level_scaling.addon_armor = GetJsonInt(scaling_data, "addon_armor", 0);
			level_scaling.initial_power_armor = GetJsonInt(scaling_data, "initial_power_armor", 0);
			level_scaling.addon_power_armor = GetJsonInt(scaling_data, "addon_power_armor", 0);

			g_config.monsters.level_scaling[monster_name] = level_scaling;
			loaded_count++;
		}

		if (loaded_count > 0)
		{
			gi.Com_PrintFmt("Config: Loaded {} monster level scaling configurations\n", loaded_count);
		}
	}
}

void Config_LoadMaps(const char* basedir)
{
	// Build config file path
	std::string config_path = std::string(basedir) + "config/maps_config.lua";

	Json::Value root;
	if (!LoadLuaConfig(config_path, root))
	{
		gi.Com_PrintFmt("Config: config/maps_config.lua not loaded, using default map values\n");
		return;
	}

	// Load default caps
	if (root.isMember("default_caps") && root["default_caps"].isObject())
	{
		const Json::Value& caps = root["default_caps"];
		g_config.maps.big_map_cap = GetJsonInt(caps, "big_map", 26);
		g_config.maps.medium_map_cap = GetJsonInt(caps, "medium_map", 14);
		g_config.maps.small_map_cap = GetJsonInt(caps, "small_map", 12);
		g_config.maps.custom_map_cap = GetJsonInt(caps, "custom_map", 20);

		gi.Com_PrintFmt("Config: Default caps - Big: {}, Medium: {}, Small: {}, Custom: {}\n",
			g_config.maps.big_map_cap,
			g_config.maps.medium_map_cap,
			g_config.maps.small_map_cap,
			g_config.maps.custom_map_cap);
	}

	// Load default settings
	if (root.isMember("default_settings") && root["default_settings"].isObject())
	{
		const Json::Value& settings = root["default_settings"];
		if (settings.isMember("enable_grid") && settings["enable_grid"].isBool())
		{
			g_config.maps.default_enable_grid = settings["enable_grid"].asBool();
			gi.Com_PrintFmt("Config: Default grid enabled: {}\n", g_config.maps.default_enable_grid);
		}
	}

	// Load map-specific overrides
	if (root.isMember("map_overrides") && root["map_overrides"].isObject())
	{
		const Json::Value& overrides = root["map_overrides"];
		int loaded_count = 0;

		for (const auto& map_name : overrides.getMemberNames())
		{
			// Skip comment fields
			if (map_name.empty() || map_name[0] == '_')
				continue;

			const Json::Value& map_data = overrides[map_name];
			if (!map_data.isObject())
				continue;

			// Convert map name to MapID
			horde::MapID mapId = horde::MapOriginRegistry::GetMapID(map_name.c_str());
			if (mapId == horde::MapID::UNKNOWN)
			{
				gi.Com_PrintFmt("Config: WARNING - Unknown map '{}' in maps_config.lua, skipping\n", map_name);
				continue;
			}

			MapOverrideConfig override_config;

			// Load monster cap override
			if (map_data.isMember("monster_cap") && map_data["monster_cap"].isInt())
			{
				override_config.monster_cap = map_data["monster_cap"].asInt();
			}

			// Load grid enable override
			if (map_data.isMember("enable_grid") && map_data["enable_grid"].isBool())
			{
				override_config.enable_grid = map_data["enable_grid"].asBool();
				override_config.has_grid_override = true;
			}

			// Load loadent enable override
			if (map_data.isMember("enable_loadent") && map_data["enable_loadent"].isBool())
			{
				override_config.enable_loadent = map_data["enable_loadent"].asBool();
				override_config.has_loadent_override = true;
			}

			// Load map size override
			if (map_data.isMember("map_size") && map_data["map_size"].isString())
			{
				std::string size_str = map_data["map_size"].asString();
				if (size_str == "small")
				{
					override_config.size_override_is_small = true;
					override_config.size_override_is_big = false;
					override_config.size_override_is_medium = false;
					override_config.has_size_override = true;
				}
				else if (size_str == "big" || size_str == "large")
				{
					override_config.size_override_is_small = false;
					override_config.size_override_is_big = true;
					override_config.size_override_is_medium = false;
					override_config.has_size_override = true;
				}
				else if (size_str == "medium")
				{
					override_config.size_override_is_small = false;
					override_config.size_override_is_big = false;
					override_config.size_override_is_medium = true;
					override_config.has_size_override = true;
				}
				else
				{
					gi.Com_PrintFmt("Config: WARNING - Invalid map_size '{}' for map '{}', ignoring\n", size_str, map_name);
				}
			}

			// Load boss size override (decoupled from map_size). Controls which boss
			// pool a map draws from without affecting monster spawning/caps.
			if (map_data.isMember("boss_size") && map_data["boss_size"].isString())
			{
				std::string boss_size_str = map_data["boss_size"].asString();
				if (boss_size_str == "small")
				{
					override_config.boss_size_override_is_small = true;
					override_config.boss_size_override_is_big = false;
					override_config.boss_size_override_is_medium = false;
					override_config.has_boss_size_override = true;
				}
				else if (boss_size_str == "big" || boss_size_str == "large")
				{
					override_config.boss_size_override_is_small = false;
					override_config.boss_size_override_is_big = true;
					override_config.boss_size_override_is_medium = false;
					override_config.has_boss_size_override = true;
				}
				else if (boss_size_str == "medium")
				{
					override_config.boss_size_override_is_small = false;
					override_config.boss_size_override_is_big = false;
					override_config.boss_size_override_is_medium = true;
					override_config.has_boss_size_override = true;
				}
				else
				{
					gi.Com_PrintFmt("Config: WARNING - Invalid boss_size '{}' for map '{}', ignoring\n", boss_size_str, map_name);
				}
			}

			// Store in array using MapID as index
			const size_t index = static_cast<size_t>(mapId);
			g_config.maps.map_overrides[index] = override_config;
			loaded_count++;

			// Determine map size string for logging
			std::string size_str = "default";
			if (override_config.has_size_override)
			{
				if (override_config.size_override_is_small)
					size_str = "small";
				else if (override_config.size_override_is_big)
					size_str = "big";
				else
					size_str = "medium";
			}

			// Determine boss size string for logging
			std::string boss_size_str = "default";
			if (override_config.has_boss_size_override)
			{
				if (override_config.boss_size_override_is_small)
					boss_size_str = "small";
				else if (override_config.boss_size_override_is_big)
					boss_size_str = "big";
				else
					boss_size_str = "medium";
			}

			gi.Com_PrintFmt("Config: Map '{}' (ID: {}) - cap: {}, grid: {}, loadent: {}, size: {}, boss_size: {}\n",
				map_name,
				static_cast<int>(mapId),
				override_config.monster_cap >= 0 ? std::to_string(override_config.monster_cap) : "default",
				override_config.has_grid_override ? (override_config.enable_grid ? "true" : "false") : "default",
				override_config.has_loadent_override ? (override_config.enable_loadent ? "true" : "false") : "default",
				size_str,
				boss_size_str);
		}

		if (loaded_count > 0)
		{
			gi.Com_PrintFmt("Config: Loaded {} map-specific overrides from config/maps_config.lua\n", loaded_count);
		}
	}
}

void Config_Load(const char* basedir)
{
	// Set defaults first
	Config_SetDefaults();

	// Build config file path based on game mode
	std::string config_filename = IsPvMMode() ? "config/player_pvm_config.lua" : "config/player_horde_config.lua";
	std::string config_path = std::string(basedir) + config_filename;

	Json::Value root;
	const bool player_config_loaded = LoadLuaConfig(config_path, root);
	if (!player_config_loaded)
	{
		gi.Com_PrintFmt("Config: Using default values\n");
	}

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

			g_config.blaster.damage_min = GetJsonInt(w, "damage_min", 5);
			g_config.blaster.damage_max = GetJsonInt(w, "damage_max", 42);
			g_config.blaster.speed = GetJsonInt(w, "speed", 1300);
			g_config.blaster.bounces = GetJsonInt(w, "bounces", 5);
			g_config.blaster.speed_addon = GetJsonInt(w, "speed_addon", 40);
		}

		// Hyperblaster
		if (weapons.isMember("hyperblaster") && weapons["hyperblaster"].isObject())
		{
			const Json::Value& w = weapons["hyperblaster"];
			g_config.hyperblaster.damage_min = GetJsonInt(w, "damage_min", 16);
			g_config.hyperblaster.damage_max = GetJsonInt(w, "damage_max", 18);
			g_config.hyperblaster.speed = GetJsonInt(w, "speed", 1700);
			g_config.hyperblaster.bounces = GetJsonInt(w, "bounces", 3);
			g_config.hyperblaster.speed_addon = GetJsonInt(w, "speed_addon", 40);
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
			g_config.machinegun.tracer_damage_per_level = GetJsonInt(w, "tracer_damage_per_level", 4);
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
			g_config.chaingun.tracer_damage_per_level = GetJsonInt(w, "tracer_damage_per_level", 2);
		}

		// Grenade
		if (weapons.isMember("grenade") && weapons["grenade"].isObject())
		{
			const Json::Value& w = weapons["grenade"];
			g_config.grenade.damage = GetJsonInt(w, "damage", 125);
			g_config.grenade.radius_offset = GetJsonFloat(w, "radius_offset", 40.0f);
			g_config.grenade.minspeed = GetJsonFloat(w, "minspeed", 600.0f);
			g_config.grenade.maxspeed = GetJsonFloat(w, "maxspeed", 900.0f);
			g_config.grenade.speed_addon = GetJsonFloat(w, "speed_addon", 30.0f);
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
			g_config.grenadelauncher.speed_addon = GetJsonInt(w, "speed_addon", 30);
		}

		// Rocket Launcher
		if (weapons.isMember("rocket") && weapons["rocket"].isObject())
		{
			const Json::Value& w = weapons["rocket"];
			g_config.rocket.damage_min = GetJsonInt(w, "damage_min", 100);
			g_config.rocket.damage_max = GetJsonInt(w, "damage_max", 120);
			g_config.rocket.speed = GetJsonInt(w, "speed", 1230);
			g_config.rocket.radius = GetJsonInt(w, "radius", 125);
			g_config.rocket.damage_addon = GetJsonInt(w, "damage_addon", 3);
			g_config.rocket.radius_addon = GetJsonInt(w, "radius_addon", 3);
			g_config.rocket.speed_addon = GetJsonInt(w, "speed_addon", 28);
		}

		// Railgun
		if (weapons.isMember("railgun") && weapons["railgun"].isObject())
		{
			const Json::Value& w = weapons["railgun"];
			g_config.railgun.damage = GetJsonInt(w, "damage", 150);
			g_config.railgun.damage_horde = GetJsonInt(w, "damage_horde", 225);
			g_config.railgun.kick = GetJsonInt(w, "kick", 285);
			g_config.railgun.damage_addon = GetJsonInt(w, "damage_addon", 8);
		}

		// 20mm Cannon
		if (weapons.isMember("cannon20mm") && weapons["cannon20mm"].isObject())
		{
			const Json::Value& w = weapons["cannon20mm"];
			g_config.cannon20mm.damage = GetJsonInt(w, "damage", 35);
			g_config.cannon20mm.kick = GetJsonInt(w, "kick", 35);
			g_config.cannon20mm.range = GetJsonInt(w, "range", 650);
			g_config.cannon20mm.recoil_force = GetJsonInt(w, "recoil_force", 500);
			g_config.cannon20mm.range_addon = GetJsonInt(w, "range_addon", 30);
		}

		// BFG
		if (weapons.isMember("bfg") && weapons["bfg"].isObject())
		{
			const Json::Value& w = weapons["bfg"];
			g_config.bfg.damage = GetJsonInt(w, "damage", 700);
			g_config.bfg.radius = GetJsonFloat(w, "radius", 1000.0f);
			g_config.bfg.speed = GetJsonInt(w, "speed", 650);
			g_config.bfg.ammo_normal = GetJsonInt(w, "ammo_normal", 50);
			g_config.bfg.ammo_slide = GetJsonInt(w, "ammo_slide", 25);
			g_config.bfg.damage_addon = GetJsonInt(w, "damage_addon", 2);
			g_config.bfg.speed_addon = GetJsonInt(w, "speed_addon", 35);
		}

		// Ion Ripper (Xatrix)
		if (weapons.isMember("ionripper") && weapons["ionripper"].isObject())
		{
			const Json::Value& w = weapons["ionripper"];
			g_config.ionripper.damage = GetJsonInt(w, "damage", 50);
			g_config.ionripper.damage_addon = GetJsonInt(w, "damage_addon", 2);
			g_config.ionripper.init_speed = GetJsonInt(w, "init_speed", 900);
			g_config.ionripper.speed_addon = GetJsonInt(w, "speed_addon", 40);
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
			g_config.plasmabeam.kick_singleplayer = GetJsonInt(w, "kick_singleplayer", 3);
			g_config.plasmabeam.damage_addon = GetJsonInt(w, "damage_addon", 1);
		}

		// Tracker / Disintegrator (Rogue)
		if (weapons.isMember("tracker") && weapons["tracker"].isObject())
		{
			const Json::Value& w = weapons["tracker"];
			g_config.tracker.damage = GetJsonInt(w, "damage", 140);
			g_config.tracker.speed = GetJsonInt(w, "speed", 1000);
		}

		// ETF Rifle (Rogue)
		if (weapons.isMember("etfrifle") && weapons["etfrifle"].isObject())
		{
			const Json::Value& w = weapons["etfrifle"];
			g_config.etfrifle.damage_min = GetJsonInt(w, "damage_min", 9);
			g_config.etfrifle.damage_max = GetJsonInt(w, "damage_max", 13);
			g_config.etfrifle.kick_normal = GetJsonInt(w, "kick_normal", 3);
			g_config.etfrifle.kick_homing = GetJsonInt(w, "kick_homing", 75);
			g_config.etfrifle.damage_addon = GetJsonInt(w, "damage_addon", 1);
			g_config.etfrifle.init_speed = GetJsonInt(w, "init_speed", 1450);
			g_config.etfrifle.speed_addon = GetJsonInt(w, "speed_addon", 40);
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
			g_config.prox_mine.damage_addon = GetJsonInt(p, "damage_addon", 0);
		}

		// Laser
		if (deployables.isMember("laser") && deployables["laser"].isObject())
		{
			const Json::Value& l = deployables["laser"];
			g_config.laser.initial_health = GetJsonInt(l, "initial_health", 0);
			g_config.laser.addon_health = GetJsonInt(l, "addon_health", 150);
			g_config.laser.initial_damage = GetJsonInt(l, "initial_damage", 1);
			g_config.laser.addon_damage = GetJsonInt(l, "addon_damage", 2);
			g_config.laser.nonclient_mod = GetJsonFloat(l, "nonclient_mod", 0.5f);
			g_config.laser.cost = GetJsonInt(l, "cost", 25);
		}

		// Trap
		if (deployables.isMember("trap") && deployables["trap"].isObject())
		{
			const Json::Value& t = deployables["trap"];
			g_config.trap.minspeed = GetJsonFloat(t, "minspeed", 500.0f);
			g_config.trap.maxspeed = GetJsonFloat(t, "maxspeed", 900.0f);
			g_config.trap.speed_addon = GetJsonFloat(t, "speed_addon", 30.0f);
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
			g_config.tesla.damage_addon = GetJsonInt(t, "damage_addon", 0);
			g_config.tesla.minspeed = GetJsonFloat(t, "minspeed", 600.0f);
			g_config.tesla.maxspeed = GetJsonFloat(t, "maxspeed", 900.0f);
			g_config.tesla.speed_addon = GetJsonFloat(t, "speed_addon", 30.0f);
		}

		// Sentry Gun
		if (deployables.isMember("sentrygun") && deployables["sentrygun"].isObject())
		{
			const Json::Value& s = deployables["sentrygun"];
			g_config.sentrygun.initial_health = GetJsonInt(s, "initial_health", 50);
			g_config.sentrygun.addon_health = GetJsonInt(s, "addon_health", 15);
			g_config.sentrygun.initial_armor = GetJsonInt(s, "initial_armor", 50);
			g_config.sentrygun.addon_armor = GetJsonInt(s, "addon_armor", 30);
			g_config.sentrygun.max_health = GetJsonInt(s, "max_health", 200);
			g_config.sentrygun.max_armor = GetJsonInt(s, "max_armor", 350);
			// Weapon damage configs
			g_config.sentrygun.initial_bullet = GetJsonInt(s, "initial_bullet", 10);
			g_config.sentrygun.addon_bullet = GetJsonInt(s, "addon_bullet", 1);
			g_config.sentrygun.initial_heatbeam = GetJsonInt(s, "initial_heatbeam", 10);
			g_config.sentrygun.addon_heatbeam = GetJsonInt(s, "addon_heatbeam", 1);
			g_config.sentrygun.initial_flechette = GetJsonInt(s, "initial_flechette", 10);
			g_config.sentrygun.addon_flechette = GetJsonInt(s, "addon_flechette", 1);
			g_config.sentrygun.initial_rocket = GetJsonInt(s, "initial_rocket", 50);
			g_config.sentrygun.addon_rocket = GetJsonInt(s, "addon_rocket", 15);
			g_config.sentrygun.initial_plasma = GetJsonInt(s, "initial_plasma", 50);
			g_config.sentrygun.addon_plasma = GetJsonInt(s, "addon_plasma", 15);
			g_config.sentrygun.initial_grenade = GetJsonInt(s, "initial_grenade", 50);
			g_config.sentrygun.addon_grenade = GetJsonInt(s, "addon_grenade", 15);
			g_config.sentrygun.cost = GetJsonInt(s, "cost", 50);
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

		// Fireball
		if (abilities.isMember("fireball") && abilities["fireball"].isObject())
		{
			const Json::Value& f = abilities["fireball"];
			g_config.fireball.initial_damage = GetJsonInt(f, "initial_damage", 50);
			g_config.fireball.addon_damage = GetJsonInt(f, "addon_damage", 25);
			g_config.fireball.initial_radius = GetJsonInt(f, "initial_radius", 80);
			g_config.fireball.addon_radius = GetJsonFloat(f, "addon_radius", 2.5f);
			g_config.fireball.initial_speed = GetJsonInt(f, "initial_speed", 650);
			g_config.fireball.addon_speed = GetJsonInt(f, "addon_speed", 35);
			g_config.fireball.cost = GetJsonInt(f, "cost", 15);
		}

		// Exploding Barrel
		if (abilities.isMember("exploding_barrel") && abilities["exploding_barrel"].isObject())
		{
			const Json::Value& eb = abilities["exploding_barrel"];
			g_config.exploding_barrel.initial_health = GetJsonInt(eb, "initial_health", 30);
			g_config.exploding_barrel.addon_health = GetJsonInt(eb, "addon_health", 0);
			g_config.exploding_barrel.initial_damage = GetJsonInt(eb, "initial_damage", 100);
			g_config.exploding_barrel.addon_damage = GetJsonInt(eb, "addon_damage", 40);
			g_config.exploding_barrel.cost = GetJsonInt(eb, "cost", 20);
			g_config.exploding_barrel.max_count = GetJsonInt(eb, "max_count", 4);
		}

		// Monster Summon
		if (abilities.isMember("summon") && abilities["summon"].isObject())
		{
			const Json::Value& s = abilities["summon"];
			g_config.summon.spawn_cost = GetJsonInt(s, "spawn_cost", 25);
			g_config.summon.upkeep_per_monster = GetJsonInt(s, "upkeep_per_monster", 1);
			g_config.summon.initial_health = GetJsonInt(s, "initial_health", 100);
			g_config.summon.addon_health = GetJsonInt(s, "addon_health", 50);
			g_config.summon.initial_armor = GetJsonInt(s, "initial_armor", 0);
			g_config.summon.addon_armor = GetJsonInt(s, "addon_armor", 25);
			g_config.summon.damage_scale = GetJsonFloat(s, "damage_scale", 1.0f);
			g_config.summon.speed_scale = GetJsonFloat(s, "speed_scale", 1.0f);
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

	// Load power cubes config
	if (root.isMember("power_cubes") && root["power_cubes"].isObject())
	{
		const Json::Value& pc = root["power_cubes"];
		g_config.power_cubes.cubes_per_ammopack = GetJsonInt(pc, "cubes_per_ammopack", 25);
		g_config.power_cubes.cubes_per_shard = GetJsonInt(pc, "cubes_per_shard", 5);
		g_config.power_cubes.use_bullets_max = pc.get("use_bullets_max", true).asBool();
		g_config.power_cubes.use_cells_max = pc.get("use_cells_max", true).asBool();
	}

	// Load power cubes regeneration config
	if (root.isMember("power_cubes_regen") && root["power_cubes_regen"].isObject())
	{
		const Json::Value& pcr = root["power_cubes_regen"];
		g_config.power_cubes_regen.base_regen_time = GetJsonFloat(pcr, "base_regen_time", 5.0f);
		g_config.power_cubes_regen.cubes_per_regen = GetJsonInt(pcr, "cubes_per_regen", 5);
	}

	if (player_config_loaded)
		gi.Com_PrintFmt("Config: Successfully loaded {}\n", config_filename);
	else
		gi.Com_PrintFmt("Config: Player config defaults active for {}\n", config_filename);
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

	// Load map configs
	Config_LoadMaps(basedir);
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

	// Invalidate map size cache since config may have changed
	InvalidateMapSizeCache();

	gi.Com_PrintFmt("Config: Reload complete\n");
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

// Include for sigmoid scaling
#include "horde/g_horde_scaling.h"

// Get specific weapon damage for a monster - FULLY OPTIMIZED with enum-based O(1) lookups
// CRITICAL HOT PATH: Called on every monster weapon attack (10-60 times per second)
int GetMonsterWeaponDamage(uint8_t monster_type_id, horde::WeaponID weapon_id)
{
	if (weapon_id == horde::WeaponID::UNKNOWN) [[unlikely]]
		return 0;

	const MonsterStatsConfig* config = GetMonsterConfig(monster_type_id);
	size_t idx = static_cast<size_t>(weapon_id);

	// Step 1: Check for monster-specific override (O(1) array access)
	bool has_override = false;
	int damage = 0;

	if (config && config->weapon_damage_overrides[idx] > 0) [[likely]]
	{
		damage = config->weapon_damage_overrides[idx];
		has_override = true;
	}
	else
	{
		// Step 2: Use global damage (O(1) array access, no string lookup!)
		damage = g_config.global_weapon_damage.values[idx];
	}

	// Step 3: Apply damage_scale ONLY if no override exists
	// Overrides are final values and shouldn't be scaled
	if (!has_override && config)
	{
		damage = static_cast<int>(damage * config->damage_scale);
	}

	// Step 4: Clamp comparable monster attacks to Remaster source max damage.
	// addon_damage is intentionally parsed but not applied until a future Lua scaling system enables it.
	if (config && config->weapon_damage_max[idx] > 0 && damage > config->weapon_damage_max[idx])
	{
		damage = config->weapon_damage_max[idx];
	}

	return damage;
}

// Get specific weapon speed for a monster - FULLY OPTIMIZED with enum-based O(1) lookups
int GetMonsterWeaponSpeed(uint8_t monster_type_id, horde::WeaponID weapon_id)
{
	if (weapon_id == horde::WeaponID::UNKNOWN) [[unlikely]]
		return 0;

	const MonsterStatsConfig* config = GetMonsterConfig(monster_type_id);
	size_t idx = static_cast<size_t>(weapon_id);

	// Step 1: Check for monster-specific speed override (O(1) array access)
	if (config && config->weapon_speed_overrides[idx] != 0) [[likely]]
	{
		// Override found - use it directly (no speed_scale applied)
		return config->weapon_speed_overrides[idx];
	}

	// Step 2: Use global speed (O(1) array access, no string lookup!)
	int base_speed = g_config.global_weapon_speed.values[idx];
	if (base_speed == 0)
	{
		// 0 means instant hit or melee (not an error)
		return 0;
	}

	// Step 3: Apply monster speed scale
	float speed_scale = config ? config->speed_scale : 1.0f;
	return static_cast<int>(base_speed * speed_scale);
}

// Get specific weapon radius for a monster - FULLY OPTIMIZED with enum-based O(1) lookups
int GetMonsterWeaponRadius(uint8_t monster_type_id, horde::WeaponID weapon_id)
{
	if (weapon_id == horde::WeaponID::UNKNOWN) [[unlikely]]
		return 0;

	// Direct O(1) array access - no string lookup!
	float base_radius = g_config.global_weapon_radius.values[static_cast<size_t>(weapon_id)];

	// Most weapons don't have radius, return 0
	if (base_radius == 0.0f)
		return 0;

	// No per-monster scaling for radius currently, just return global value
	return static_cast<int>(base_radius);
}

// Get monster cap for a specific map by MapID, considering overrides and map size
int32_t GetMonsterCapForMap(horde::MapID mapId, const horde::MapSize& mapSize)
{
	// Check for map-specific override (O(1) array lookup)
	if (mapId != horde::MapID::UNKNOWN)
	{
		const size_t index = static_cast<size_t>(mapId);
		if (index < g_config.maps.map_overrides.size())
		{
			const MapOverrideConfig& override_config = g_config.maps.map_overrides[index];
			if (override_config.monster_cap >= 0)
			{
				return override_config.monster_cap;
			}
		}
	}

	// No override, use default based on map size
	if (mapSize.isSmallMap)
		return g_config.maps.small_map_cap;
	else if (mapSize.isBigMap)
		return g_config.maps.big_map_cap;
	else
		return g_config.maps.medium_map_cap;
}

// Convenience overload that converts mapname to MapID
int32_t GetMonsterCapForMap(const char* mapname, const horde::MapSize& mapSize)
{
	horde::MapID mapId = horde::MapID::UNKNOWN;
	if (mapname && mapname[0])
	{
		mapId = horde::MapOriginRegistry::GetMapID(mapname);
	}
	return GetMonsterCapForMap(mapId, mapSize);
}

// Get whether grid spawning should be enabled for a specific map by MapID
bool GetGridEnabledForMap(horde::MapID mapId)
{
	// Check for map-specific override (O(1) array lookup)
	if (mapId != horde::MapID::UNKNOWN)
	{
		const size_t index = static_cast<size_t>(mapId);
		if (index < g_config.maps.map_overrides.size())
		{
			const MapOverrideConfig& override_config = g_config.maps.map_overrides[index];
			if (override_config.has_grid_override)
			{
				return override_config.enable_grid;
			}
		}
	}

	// No override, use default
	return g_config.maps.default_enable_grid;
}

// Convenience overload that converts mapname to MapID
bool GetGridEnabledForMap(const char* mapname)
{
	horde::MapID mapId = horde::MapID::UNKNOWN;
	if (mapname && mapname[0])
	{
		mapId = horde::MapOriginRegistry::GetMapID(mapname);
	}
	return GetGridEnabledForMap(mapId);
}

// Get whether g_loadent should be enabled for a specific map by MapID
bool GetLoadentEnabledForMap(horde::MapID mapId)
{
	// Check for map-specific override (O(1) array lookup)
	if (mapId != horde::MapID::UNKNOWN)
	{
		const size_t index = static_cast<size_t>(mapId);
		if (index < g_config.maps.map_overrides.size())
		{
			const MapOverrideConfig& override_config = g_config.maps.map_overrides[index];
			if (override_config.has_loadent_override)
			{
				return override_config.enable_loadent;
			}
		}
	}

	// No override, default to enabled
	return true;
}

// Convenience overload that converts mapname to MapID
bool GetLoadentEnabledForMap(const char* mapname)
{
	horde::MapID mapId = horde::MapID::UNKNOWN;
	if (mapname && mapname[0])
	{
		mapId = horde::MapOriginRegistry::GetMapID(mapname);
	}
	return GetLoadentEnabledForMap(mapId);
}

// Scaling helper functions
bool ShouldApplyMonsterWaveScaling(uint8_t monster_type_id)
{
	const horde::MonsterTypeID type_id = static_cast<horde::MonsterTypeID>(monster_type_id);
	if (type_id == horde::MonsterTypeID::UNKNOWN)
		return false;

	const int32_t min_wave = GetAdjustedMinWave(type_id, 0);
	return min_wave > 0 && min_wave <= 5;
}

int GetScaledHealth(int base_health, float health_scale, int wave_level, bool is_boss)
{
	// Apply monster-specific health scale
	int scaled_health = static_cast<int>(base_health * health_scale);

	// WAVE-BASED SCALING DISABLED - Using PvM level-based scaling instead
	// Apply wave scaling if enabled
	// if (g_config.use_sigmoid_scaling && wave_level > 0)
	// {
	// 	scaled_health = ScaleMonsterHealth(scaled_health, wave_level, is_boss);
	// }
	// else if (wave_level > 0)
	// {
	// 	// Fallback: basic scaling if sigmoid is disabled
	// 	scaled_health = ScaleMonsterHealth(scaled_health, wave_level, is_boss);
	// }

	return scaled_health;
}

int GetScaledArmor(int base_armor, float armor_scale, int wave_level, bool is_boss)
{
	if (base_armor == 0)
		return 0;

	// Apply monster-specific armor scale
	int scaled_armor = static_cast<int>(base_armor * armor_scale);

	// Apply wave scaling if enabled
	if (g_config.use_sigmoid_scaling && wave_level > 0)
	{
		scaled_armor = ScaleMonsterArmor(scaled_armor, wave_level);
		if (is_boss)
		{
			scaled_armor = static_cast<int>(scaled_armor * 1.5f);
		}
	}

	return scaled_armor;
}

int GetScaledPowerArmor(int base_power_armor, float power_armor_scale, int wave_level, bool is_boss)
{
	if (base_power_armor == 0)
		return 0;

	// Apply monster-specific power armor scale
	int scaled_power_armor = static_cast<int>(base_power_armor * power_armor_scale);

	// Apply wave scaling if enabled
	if (g_config.use_sigmoid_scaling && wave_level > 0)
	{
		scaled_power_armor = ScaleMonsterArmor(scaled_power_armor, wave_level);
		if (is_boss)
		{
			scaled_power_armor = static_cast<int>(scaled_power_armor * 1.5f);
		}
	}

	return scaled_power_armor;
}

// Monster health/armor helpers (for macros)
int GetMonsterBaseHealth(uint8_t monster_type_id)
{
	const MonsterStatsConfig* config = GetMonsterConfig(monster_type_id);
	return config ? config->health : 100; // Default fallback
}

int GetMonsterScaledHealth(uint8_t monster_type_id, int wave_level, bool is_boss)
{
	const MonsterStatsConfig* config = GetMonsterConfig(monster_type_id);
	if (!config)
		return 100;

	// Get classname from monster_type_id
	const char* classname = horde::MonsterTypeRegistry::GetClassname(static_cast<horde::MonsterTypeID>(monster_type_id));
	if (!classname || strncmp(classname, "monster_", 8) != 0)
	{
		// Fallback to old scaling if classname is invalid
		int base_health = config->health;
		float health_scale = config->health_scale;
		return GetScaledHealth(base_health, health_scale, wave_level, is_boss);
	}

	// Extract monster name without "monster_" prefix
	const char* monster_name = classname + 8;

	// Try to get level-based scaling config
	const MonsterLevelScaling* level_scaling = GetMonsterLevelScaling(monster_name);
	if (level_scaling)
	{
		// Unified source of truth:
		// Base health comes from monsters.<name>.health and level scaling contributes only addon_health.
		int scaled_health = config->health + (g_lowest_player_level * level_scaling->addon_health);
		// Apply monster-specific health scale multiplier
		scaled_health = static_cast<int>(scaled_health * config->health_scale);
		return scaled_health;
	}
	else
	{
		// Fallback to old scaling if no level scaling config exists
		int base_health = config->health;
		float health_scale = config->health_scale;
		return GetScaledHealth(base_health, health_scale, wave_level, is_boss);
	}
}

int GetMonsterBaseArmor(uint8_t monster_type_id)
{
	const MonsterStatsConfig* config = GetMonsterConfig(monster_type_id);
	return config ? config->armor_power : 0;
}

int GetMonsterScaledArmor(uint8_t monster_type_id, int wave_level, bool is_boss)
{
	const MonsterStatsConfig* config = GetMonsterConfig(monster_type_id);
	if (!config)
		return 0;

	// Get classname from monster_type_id
	const char* classname = horde::MonsterTypeRegistry::GetClassname(static_cast<horde::MonsterTypeID>(monster_type_id));
	if (!classname || strncmp(classname, "monster_", 8) != 0)
	{
		// Fallback to old scaling if classname is invalid
		int base_armor = config->armor_power;
		float armor_scale = config->armor_scale;
		if (!ShouldApplyMonsterWaveScaling(monster_type_id))
			return static_cast<int>(base_armor * armor_scale);
		return GetScaledArmor(base_armor, armor_scale, wave_level, is_boss);
	}

	// Extract monster name without "monster_" prefix
	const char* monster_name = classname + 8;

	// Try to get level-based scaling config
	const MonsterLevelScaling* level_scaling = GetMonsterLevelScaling(monster_name);
	if (level_scaling)
	{
		// Unified source of truth:
		// Base armor comes from monsters.<name>.armor_power and level scaling contributes only addon_armor.
		int scaled_armor = config->armor_power + (g_lowest_player_level * level_scaling->addon_armor);
		// Apply monster-specific armor scale multiplier
		scaled_armor = static_cast<int>(scaled_armor * config->armor_scale);
		return scaled_armor;
	}
	else
	{
		// Fallback to old scaling if no level scaling config exists
		int base_armor = config->armor_power;
		float armor_scale = config->armor_scale;
		if (!ShouldApplyMonsterWaveScaling(monster_type_id))
			return static_cast<int>(base_armor * armor_scale);
		return GetScaledArmor(base_armor, armor_scale, wave_level, is_boss);
	}
}

int GetMonsterBasePowerArmor(uint8_t monster_type_id)
{
	const MonsterStatsConfig* config = GetMonsterConfig(monster_type_id);
	return config ? config->power_armor_power : 0;
}

int GetMonsterScaledPowerArmor(uint8_t monster_type_id, int wave_level, bool is_boss)
{
	const MonsterStatsConfig* config = GetMonsterConfig(monster_type_id);
	if (!config || config->power_armor_power == 0)
		return 0;

	// Get classname to look up level scaling data
	const char* classname = horde::MonsterTypeRegistry::GetClassname(static_cast<horde::MonsterTypeID>(monster_type_id));
	if (!classname || strncmp(classname, "monster_", 8) != 0)
	{
		// Fallback if classname is invalid
		int base_power_armor = config->power_armor_power;
		float power_armor_scale = config->power_armor_scale;
		if (!ShouldApplyMonsterWaveScaling(monster_type_id))
			return static_cast<int>(base_power_armor * power_armor_scale);
		return GetScaledPowerArmor(base_power_armor, power_armor_scale, wave_level, is_boss);
	}

	// Extract short name (e.g. "brain")
	const char* monster_name = classname + 8;

	// Try to get level-scaling config
	const MonsterLevelScaling* level_scaling = GetMonsterLevelScaling(monster_name);
	if (level_scaling)
	{
		// Unified source of truth:
		// Base power armor comes from monsters.<name>.power_armor_power and level scaling contributes only addon_power_armor.
		int scaled_power_armor = config->power_armor_power + (g_lowest_player_level * level_scaling->addon_power_armor);
		// Apply monster-specific scale multiplier
		scaled_power_armor = static_cast<int>(scaled_power_armor * config->power_armor_scale);
		return scaled_power_armor;
	}
	else
	{
		// Fallback to old (wave-based Horde) scaling if there is no level-scaling config
		int base_power_armor = config->power_armor_power;
		float power_armor_scale = config->power_armor_scale;
		if (!ShouldApplyMonsterWaveScaling(monster_type_id))
			return static_cast<int>(base_power_armor * power_armor_scale);
		return GetScaledPowerArmor(base_power_armor, power_armor_scale, wave_level, is_boss);
	}
}

int32_t GetMonsterArmorType(uint8_t monster_type_id)
{
	const MonsterStatsConfig* config = GetMonsterConfig(monster_type_id);
	return config ? config->armor_type : static_cast<int32_t>(IT_NULL);
}

int32_t GetMonsterPowerArmorType(uint8_t monster_type_id)
{
	const MonsterStatsConfig* config = GetMonsterConfig(monster_type_id);
	return config ? config->power_armor_type : static_cast<int32_t>(IT_NULL);
}

// Monster level scaling helpers
const MonsterLevelScaling* GetMonsterLevelScaling(const char* monster_name)
{
	auto it = g_config.monsters.level_scaling.find(monster_name);
	if (it != g_config.monsters.level_scaling.end())
	{
		return &it->second;
	}
	return nullptr;
}

void GetMonsterLevelScaledStats(const char* monster_name, int32_t pvm_level, int& out_health, int& out_armor)
{
	const MonsterLevelScaling* scaling = GetMonsterLevelScaling(monster_name);
	if (scaling)
	{
		int base_health = 100;
		int base_armor = 0;

		if (monster_name && monster_name[0])
		{
			std::string full_classname = "monster_";
			full_classname += monster_name;

			horde::MonsterTypeID type_id = horde::MonsterTypeRegistry::GetTypeID(full_classname.c_str());
			if (type_id != horde::MonsterTypeID::UNKNOWN)
			{
				if (const MonsterStatsConfig* config = GetMonsterConfig(static_cast<uint8_t>(type_id)))
				{
					base_health = config->health;
					base_armor = config->armor_power;
				}
			}
		}

		out_health = base_health + (pvm_level * scaling->addon_health);
		out_armor = base_armor + (pvm_level * scaling->addon_armor);
	}
	else
	{
		// Fallback to defaults
		out_health = 100;
		out_armor = 0;
	}
}
