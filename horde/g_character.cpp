#include "g_character.h"
#include "../g_local.h"
#include <json/json.h>
#include <fstream>
#include <string>
#include <algorithm>
#include <filesystem>

// Forward declaration
extern const char* GetPlayerName(const edict_t* player);

// Default respawn weapon for PvM mode
constexpr const char* DEFAULT_RESPAWN_WEAPON = "Rocket Launcher";

// Character system initialization
void Character_Init()
{
    // Create characters directory if it doesn't exist
    try {
        std::filesystem::create_directories("baseq2/characters");
        gi.Com_PrintFmt("Character system: Initialized (baseq2/characters/)\n");
    }
    catch (const std::exception& e) {
        gi.Com_PrintFmt("Character system: Warning - Could not create characters directory: {}\n", e.what());
    }
}

// Sanitize player name for use as filename
std::string Character_SanitizeName(const char* name)
{
    if (!name || !name[0])
        return "unknown";

    std::string sanitized;
    sanitized.reserve(MAX_NETNAME);

    for (const char* p = name; *p && sanitized.length() < MAX_NETNAME - 1; ++p)
    {
        char c = *p;
        // Allow alphanumeric, underscore, hyphen
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')
        {
            sanitized += c;
        }
        else if (c == ' ')
        {
            sanitized += '_';
        }
        // Skip other characters
    }

    if (sanitized.empty())
        return "unknown";

    return sanitized;
}

// Get character file path for player
std::string Character_GetFilePath(edict_t* player)
{
    if (!player || !player->client)
        return "";

    // Use GetPlayerName() for proper name retrieval
    const char* player_name = GetPlayerName(player);
    std::string sanitized = Character_SanitizeName(player_name);
    return "baseq2/characters/" + sanitized + ".json";
}

// Create default character file
void Character_CreateDefault(edict_t* player)
{
    if (!player || !player->client)
        return;

    CharacterData data;
    Q_strlcpy(data.player_name, GetPlayerName(player), sizeof(data.player_name));
    Q_strlcpy(data.respawn_weapon, DEFAULT_RESPAWN_WEAPON, sizeof(data.respawn_weapon));
    data.id_display = player->client->pers.id_state;
    data.iddmg_display = player->client->pers.iddmg_state;
    data.sentry_gun_choice = static_cast<int32_t>(player->client->pers.sentry_gun_choice);
    data.morph_preference = player->client->pers.morph_preference;
    data.level = 1;
    data.xp = 0;

    // Save to file
    Json::Value root;
    root["player_name"] = data.player_name;
    root["respawn_weapon"] = data.respawn_weapon;
    root["preferences"]["id_display"] = data.id_display;
    root["preferences"]["iddmg_display"] = data.iddmg_display;
    root["preferences"]["sentry_gun_choice"] = data.sentry_gun_choice;
    root["preferences"]["morph_preference"] = data.morph_preference;
    root["stats"]["pvm_level"] = 1;
    root["stats"]["pvm_xp"] = 0;
    root["stats"]["pvm_stat_points"] = 0;
    root["stats"]["pvm_max_ammo_level"] = 0;
    root["stats"]["pvm_vitality_level"] = 0;

    std::string filepath = Character_GetFilePath(player);
    std::ofstream file(filepath, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        gi.Com_PrintFmt("Character: Failed to create character file for {}\n", GetPlayerName(player));
        return;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
    file.close();

    gi.Com_PrintFmt("Character: Created new character file for {}\n", GetPlayerName(player));
}

// Load character from file
bool Character_Load(edict_t* player)
{
    if (!player || !player->client)
        return false;

    // Don't load for bots - they use default settings
    if (player->svflags & SVF_BOT)
        return false;

    std::string filepath = Character_GetFilePath(player);
    std::ifstream file(filepath, std::ios::in);

    if (!file.is_open())
    {
        // No character file exists, create default
        gi.Com_PrintFmt("Character: No character file found for {}, creating default\n",
                        GetPlayerName(player));
        Character_CreateDefault(player);
        return false;
    }

    // Parse JSON
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;

    if (!Json::parseFromStream(builder, file, &root, &errs))
    {
        gi.Com_PrintFmt("Character: Failed to parse character file for {}: {}\n",
                        GetPlayerName(player), errs);
        file.close();
        return false;
    }

    file.close();

    // Load data into client persistent
    if (root.isMember("respawn_weapon") && root["respawn_weapon"].isString())
    {
        Q_strlcpy(player->client->pers.respawn_weapon_name,
                  root["respawn_weapon"].asCString(),
                  sizeof(player->client->pers.respawn_weapon_name));
    }
    else
    {
        Q_strlcpy(player->client->pers.respawn_weapon_name,
                  DEFAULT_RESPAWN_WEAPON,
                  sizeof(player->client->pers.respawn_weapon_name));
    }

    // Load preferences
    if (root.isMember("preferences") && root["preferences"].isObject())
    {
        const Json::Value& prefs = root["preferences"];
        if (prefs.isMember("id_display") && prefs["id_display"].isBool())
            player->client->pers.id_state = prefs["id_display"].asBool();
        if (prefs.isMember("iddmg_display") && prefs["iddmg_display"].isBool())
            player->client->pers.iddmg_state = prefs["iddmg_display"].asBool();
        if (prefs.isMember("sentry_gun_choice") && prefs["sentry_gun_choice"].isInt())
        {
            player->client->pers.sentry_gun_choice = static_cast<sentrytype_t>(prefs["sentry_gun_choice"].asInt());
            player->client->resp.sentry_gun_choice = player->client->pers.sentry_gun_choice;
        }
        if (prefs.isMember("morph_preference") && prefs["morph_preference"].isInt())
            player->client->pers.morph_preference = prefs["morph_preference"].asInt();
    }

    // Load PvM stats
    if (root.isMember("stats") && root["stats"].isObject())
    {
        const Json::Value& stats = root["stats"];
        if (stats.isMember("pvm_level") && stats["pvm_level"].isInt())
            player->client->pers.pvm_level = stats["pvm_level"].asInt();
        if (stats.isMember("pvm_xp") && stats["pvm_xp"].isInt())
            player->client->pers.pvm_xp = stats["pvm_xp"].asInt();
        if (stats.isMember("pvm_stat_points") && stats["pvm_stat_points"].isInt())
            player->client->pers.pvm_stat_points = stats["pvm_stat_points"].asInt();
        if (stats.isMember("pvm_max_ammo_level") && stats["pvm_max_ammo_level"].isInt())
            player->client->pers.pvm_max_ammo_level = stats["pvm_max_ammo_level"].asInt();
        if (stats.isMember("pvm_vitality_level") && stats["pvm_vitality_level"].isInt())
            player->client->pers.pvm_vitality_level = stats["pvm_vitality_level"].asInt();
    }

    gi.Com_PrintFmt("Character: Loaded character for {} (respawn weapon: {}, PvM level: {})\n",
                    GetPlayerName(player),
                    player->client->pers.respawn_weapon_name,
                    player->client->pers.pvm_level);

    return true;
}

// Save character to file
bool Character_Save(edict_t* player)
{
    if (!player || !player->client)
        return false;

    // Don't save for bots - they don't need persistent data
    if (player->svflags & SVF_BOT)
        return false;

    // Build JSON
    Json::Value root;
    root["player_name"] = GetPlayerName(player);
    root["respawn_weapon"] = player->client->pers.respawn_weapon_name;
    root["preferences"]["id_display"] = player->client->pers.id_state;
    root["preferences"]["iddmg_display"] = player->client->pers.iddmg_state;
    root["preferences"]["sentry_gun_choice"] = static_cast<int32_t>(player->client->pers.sentry_gun_choice);
    root["preferences"]["morph_preference"] = player->client->pers.morph_preference;

    // Save PvM stats
    root["stats"]["pvm_level"] = player->client->pers.pvm_level;
    root["stats"]["pvm_xp"] = player->client->pers.pvm_xp;
    root["stats"]["pvm_stat_points"] = player->client->pers.pvm_stat_points;
    root["stats"]["pvm_max_ammo_level"] = player->client->pers.pvm_max_ammo_level;
    root["stats"]["pvm_vitality_level"] = player->client->pers.pvm_vitality_level;

    // Write to file
    std::string filepath = Character_GetFilePath(player);
    std::ofstream file(filepath, std::ios::out | std::ios::trunc);

    if (!file.is_open())
    {
        gi.Com_PrintFmt("Character: Failed to save character file for {}\n",
                        GetPlayerName(player));
        return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
    file.close();

    gi.Com_PrintFmt("Character: Saved character for {}\n", GetPlayerName(player));
    return true;
}

// Set respawn weapon preference
void Character_SetRespawnWeapon(edict_t* player, const char* weapon_name)
{
    if (!player || !player->client || !weapon_name)
        return;

    Q_strlcpy(player->client->pers.respawn_weapon_name, weapon_name,
              sizeof(player->client->pers.respawn_weapon_name));

    // Save immediately
    Character_Save(player);

    gi.LocClient_Print(player, PRINT_HIGH, nullptr, "Respawn weapon set to: {}\n", weapon_name);
}

// Get respawn weapon preference
const char* Character_GetRespawnWeapon(edict_t* player)
{
    if (!player || !player->client)
        return DEFAULT_RESPAWN_WEAPON;

    if (player->client->pers.respawn_weapon_name[0] == '\0')
        return DEFAULT_RESPAWN_WEAPON;

    return player->client->pers.respawn_weapon_name;
}
