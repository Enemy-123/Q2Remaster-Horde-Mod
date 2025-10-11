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

    // Check for "N/A" or similar invalid names
    if (strcmp(name, "N/A") == 0 || strcmp(name, "n/a") == 0)
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
    data.level = 0;
    data.xp = 0;

    // Save to file
    Json::Value root;
    root["player_name"] = data.player_name;
    root["respawn_weapon"] = data.respawn_weapon;
    root["preferences"]["id_display"] = data.id_display;
    root["preferences"]["iddmg_display"] = data.iddmg_display;
    root["preferences"]["sentry_gun_choice"] = data.sentry_gun_choice;
    root["preferences"]["morph_preference"] = data.morph_preference;
    root["stats"]["pvm_level"] = 0;
    root["stats"]["pvm_xp"] = 0;
    root["stats"]["pvm_stat_points"] = 0;
    root["stats"]["pvm_max_ammo_level"] = 0;
    root["stats"]["pvm_vitality_level"] = 0;

    std::string filepath = Character_GetFilePath(player);
    std::ofstream file(filepath, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        // gi.Com_PrintFmt("Character: Failed to create character file for {}\n", GetPlayerName(player));
        return;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
    file.close();

    // gi.Com_PrintFmt("Character: Created new character file for {}\n", GetPlayerName(player));
}

// Load character from file
bool Character_Load(edict_t* player)
{
    if (!player || !player->client)
        return false;

    // Don't load for bots - they use default settings
    if (player->svflags & SVF_BOT)
        return false;

    // Additional check: Don't load if name starts with "[BOT]"
    const char* player_name = GetPlayerName(player);
    if (player_name && strncmp(player_name, "[BOT]", 5) == 0)
        return false;

    std::string filepath = Character_GetFilePath(player);
    std::ifstream file(filepath, std::ios::in);

    if (!file.is_open())
    {
        // No character file exists, create default
        // gi.Com_PrintFmt("Character: No character file found for {}, creating default\n",
                        // player_name);
        Character_CreateDefault(player);
        return false;
    }

    // Parse JSON
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;

    if (!Json::parseFromStream(builder, file, &root, &errs))
    {
        // gi.Com_PrintFmt("Character: Failed to parse character file for {}: {}\n",
                        // player_name, errs);
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

    // Load PvM stats and skill-based upgrades
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

        // Load skill points
        if (stats.isMember("skill_points") && stats["skill_points"].isInt())
            player->client->pers.skill_points = stats["skill_points"].asInt();
        if (stats.isMember("weapon_points") && stats["weapon_points"].isInt())
            player->client->pers.weapon_points = stats["weapon_points"].asInt();

        // Load skill-based upgrades
        if (stats.isMember("skills") && stats["skills"].isObject())
        {
            const Json::Value& skills = stats["skills"];
            if (skills.isMember("vampire") && skills["vampire"].isInt())
                player->client->pers.skills.vampire = static_cast<int8_t>(skills["vampire"].asInt());
            if (skills.isMember("ammo_regen") && skills["ammo_regen"].isInt())
                player->client->pers.skills.ammo_regen = static_cast<int8_t>(skills["ammo_regen"].asInt());
            if (skills.isMember("vitality") && skills["vitality"].isInt())
                player->client->pers.skills.vitality = static_cast<int8_t>(skills["vitality"].asInt());
            if (skills.isMember("ha_pickup") && skills["ha_pickup"].isInt())
                player->client->pers.skills.ha_pickup = static_cast<int8_t>(skills["ha_pickup"].asInt());
            if (skills.isMember("start_armor") && skills["start_armor"].isInt())
                player->client->pers.skills.start_armor = static_cast<int8_t>(skills["start_armor"].asInt());
            if (skills.isMember("max_ammo") && skills["max_ammo"].isInt())
                player->client->pers.skills.max_ammo = static_cast<int8_t>(skills["max_ammo"].asInt());
            if (skills.isMember("auto_haste") && skills["auto_haste"].isBool())
                player->client->pers.skills.auto_haste = skills["auto_haste"].asBool();
            if (skills.isMember("armor_vampirism") && skills["armor_vampirism"].isBool())
                player->client->pers.skills.armor_vampirism = skills["armor_vampirism"].asBool();
            if (skills.isMember("sentry_upgrade") && skills["sentry_upgrade"].isBool())
                player->client->pers.skills.sentry_upgrade = skills["sentry_upgrade"].asBool();
            if (skills.isMember("tesla_chain") && skills["tesla_chain"].isBool())
                player->client->pers.skills.tesla_chain = skills["tesla_chain"].asBool();
            if (skills.isMember("fireball") && skills["fireball"].isInt())
                player->client->pers.skills.fireball = static_cast<int8_t>(skills["fireball"].asInt());
            if (skills.isMember("pc_regen") && skills["pc_regen"].isInt())
                player->client->pers.skills.pc_regen = static_cast<int8_t>(skills["pc_regen"].asInt());
            if (skills.isMember("sentrygun") && skills["sentrygun"].isInt())
                player->client->pers.skills.sentrygun = static_cast<int8_t>(skills["sentrygun"].asInt());
            if (skills.isMember("lasers") && skills["lasers"].isInt())
                player->client->pers.skills.lasers = static_cast<int8_t>(skills["lasers"].asInt());
            if (skills.isMember("monster_summon") && skills["monster_summon"].isInt())
                player->client->pers.skills.monster_summon = static_cast<int8_t>(skills["monster_summon"].asInt());
            // Load free bonuses from milestones (permanent, not resetable)
            if (skills.isMember("free_vitality") && skills["free_vitality"].isInt())
                player->client->pers.skills.free_vitality = static_cast<int8_t>(skills["free_vitality"].asInt());
            if (skills.isMember("free_max_ammo") && skills["free_max_ammo"].isInt())
                player->client->pers.skills.free_max_ammo = static_cast<int8_t>(skills["free_max_ammo"].asInt());
            if (skills.isMember("free_pc_regen") && skills["free_pc_regen"].isInt())
                player->client->pers.skills.free_pc_regen = static_cast<int8_t>(skills["free_pc_regen"].asInt());
        }

        // Load power cubes currency
        if (stats.isMember("horde_power_cubes") && stats["horde_power_cubes"].isInt())
            player->client->pers.horde_power_cubes = stats["horde_power_cubes"].asInt();

        // Load weapon upgrades
        if (stats.isMember("weapons") && stats["weapons"].isObject())
        {
            const Json::Value& weapons = stats["weapons"];

            if (weapons.isMember("gl_damage") && weapons["gl_damage"].isInt())
                player->client->pers.skills.gl_damage = static_cast<int8_t>(weapons["gl_damage"].asInt());
            if (weapons.isMember("gl_range") && weapons["gl_range"].isInt())
                player->client->pers.skills.gl_range = static_cast<int8_t>(weapons["gl_range"].asInt());
            if (weapons.isMember("gl_radius") && weapons["gl_radius"].isInt())
                player->client->pers.skills.gl_radius = static_cast<int8_t>(weapons["gl_radius"].asInt());
            if (weapons.isMember("gl_trails") && weapons["gl_trails"].isBool())
                player->client->pers.skills.gl_trails = weapons["gl_trails"].asBool();
            if (weapons.isMember("gl_silent") && weapons["gl_silent"].isBool())
                player->client->pers.skills.gl_silent = weapons["gl_silent"].asBool();
            if (weapons.isMember("gl_bouncy") && weapons["gl_bouncy"].isBool())
                player->client->pers.skills.gl_bouncy = weapons["gl_bouncy"].asBool();

            if (weapons.isMember("rl_damage") && weapons["rl_damage"].isInt())
                player->client->pers.skills.rl_damage = static_cast<int8_t>(weapons["rl_damage"].asInt());
            if (weapons.isMember("rl_speed") && weapons["rl_speed"].isInt())
                player->client->pers.skills.rl_speed = static_cast<int8_t>(weapons["rl_speed"].asInt());
            if (weapons.isMember("rl_radius") && weapons["rl_radius"].isInt())
                player->client->pers.skills.rl_radius = static_cast<int8_t>(weapons["rl_radius"].asInt());
            if (weapons.isMember("rl_trails") && weapons["rl_trails"].isBool())
                player->client->pers.skills.rl_trails = weapons["rl_trails"].asBool();
            if (weapons.isMember("rl_silent") && weapons["rl_silent"].isBool())
                player->client->pers.skills.rl_silent = weapons["rl_silent"].asBool();

            if (weapons.isMember("mg_damage") && weapons["mg_damage"].isInt())
                player->client->pers.skills.mg_damage = static_cast<int8_t>(weapons["mg_damage"].asInt());
            if (weapons.isMember("mg_pierce") && weapons["mg_pierce"].isInt())
                player->client->pers.skills.mg_pierce = static_cast<int8_t>(weapons["mg_pierce"].asInt());
            if (weapons.isMember("mg_tracers") && weapons["mg_tracers"].isInt())
                player->client->pers.skills.mg_tracers = static_cast<int8_t>(weapons["mg_tracers"].asInt());
            if (weapons.isMember("mg_spread") && weapons["mg_spread"].isInt())
                player->client->pers.skills.mg_spread = static_cast<int8_t>(weapons["mg_spread"].asInt());
            if (weapons.isMember("mg_silent") && weapons["mg_silent"].isBool())
                player->client->pers.skills.mg_silent = weapons["mg_silent"].asBool();

            if (weapons.isMember("cg_damage") && weapons["cg_damage"].isInt())
                player->client->pers.skills.cg_damage = static_cast<int8_t>(weapons["cg_damage"].asInt());
            if (weapons.isMember("cg_spin") && weapons["cg_spin"].isInt())
                player->client->pers.skills.cg_spin = static_cast<int8_t>(weapons["cg_spin"].asInt());
            if (weapons.isMember("cg_tracers") && weapons["cg_tracers"].isInt())
                player->client->pers.skills.cg_tracers = static_cast<int8_t>(weapons["cg_tracers"].asInt());
            if (weapons.isMember("cg_spread") && weapons["cg_spread"].isInt())
                player->client->pers.skills.cg_spread = static_cast<int8_t>(weapons["cg_spread"].asInt());
            if (weapons.isMember("cg_silent") && weapons["cg_silent"].isBool())
                player->client->pers.skills.cg_silent = weapons["cg_silent"].asBool();

            if (weapons.isMember("sg_damage") && weapons["sg_damage"].isInt())
                player->client->pers.skills.sg_damage = static_cast<int8_t>(weapons["sg_damage"].asInt());
            if (weapons.isMember("sg_strike") && weapons["sg_strike"].isInt())
                player->client->pers.skills.sg_strike = static_cast<int8_t>(weapons["sg_strike"].asInt());
            if (weapons.isMember("sg_pellets") && weapons["sg_pellets"].isInt())
                player->client->pers.skills.sg_pellets = static_cast<int8_t>(weapons["sg_pellets"].asInt());
            if (weapons.isMember("sg_spread") && weapons["sg_spread"].isBool())
                player->client->pers.skills.sg_spread = weapons["sg_spread"].asBool();
            if (weapons.isMember("sg_silent") && weapons["sg_silent"].isBool())
                player->client->pers.skills.sg_silent = weapons["sg_silent"].asBool();
            if (weapons.isMember("sg_energized") && weapons["sg_energized"].isBool())
                player->client->pers.skills.sg_energized = weapons["sg_energized"].asBool();

            if (weapons.isMember("ssg_damage") && weapons["ssg_damage"].isInt())
                player->client->pers.skills.ssg_damage = static_cast<int8_t>(weapons["ssg_damage"].asInt());
            if (weapons.isMember("ssg_strike") && weapons["ssg_strike"].isInt())
                player->client->pers.skills.ssg_strike = static_cast<int8_t>(weapons["ssg_strike"].asInt());
            if (weapons.isMember("ssg_pellets") && weapons["ssg_pellets"].isInt())
                player->client->pers.skills.ssg_pellets = static_cast<int8_t>(weapons["ssg_pellets"].asInt());
            if (weapons.isMember("ssg_spread") && weapons["ssg_spread"].isBool())
                player->client->pers.skills.ssg_spread = weapons["ssg_spread"].asBool();
            if (weapons.isMember("ssg_silent") && weapons["ssg_silent"].isBool())
                player->client->pers.skills.ssg_silent = weapons["ssg_silent"].asBool();
            if (weapons.isMember("ssg_energized") && weapons["ssg_energized"].isBool())
                player->client->pers.skills.ssg_energized = weapons["ssg_energized"].asBool();

            if (weapons.isMember("hg_damage") && weapons["hg_damage"].isInt())
                player->client->pers.skills.hg_damage = static_cast<int8_t>(weapons["hg_damage"].asInt());
            if (weapons.isMember("hg_range") && weapons["hg_range"].isInt())
                player->client->pers.skills.hg_range = static_cast<int8_t>(weapons["hg_range"].asInt());
            if (weapons.isMember("hg_radius_damage") && weapons["hg_radius_damage"].isInt())
                player->client->pers.skills.hg_radius_damage = static_cast<int8_t>(weapons["hg_radius_damage"].asInt());

            if (weapons.isMember("bl_damage") && weapons["bl_damage"].isInt())
                player->client->pers.skills.bl_damage = static_cast<int8_t>(weapons["bl_damage"].asInt());
            if (weapons.isMember("bl_speed") && weapons["bl_speed"].isInt())
                player->client->pers.skills.bl_speed = static_cast<int8_t>(weapons["bl_speed"].asInt());
            if (weapons.isMember("bl_trails") && weapons["bl_trails"].isBool())
                player->client->pers.skills.bl_trails = weapons["bl_trails"].asBool();
            if (weapons.isMember("bl_silent") && weapons["bl_silent"].isBool())
                player->client->pers.skills.bl_silent = weapons["bl_silent"].asBool();

            if (weapons.isMember("hb_damage") && weapons["hb_damage"].isInt())
                player->client->pers.skills.hb_damage = static_cast<int8_t>(weapons["hb_damage"].asInt());
            if (weapons.isMember("hb_speed") && weapons["hb_speed"].isInt())
                player->client->pers.skills.hb_speed = static_cast<int8_t>(weapons["hb_speed"].asInt());
            if (weapons.isMember("hb_trails") && weapons["hb_trails"].isBool())
                player->client->pers.skills.hb_trails = weapons["hb_trails"].asBool();
            if (weapons.isMember("hb_silent") && weapons["hb_silent"].isBool())
                player->client->pers.skills.hb_silent = weapons["hb_silent"].asBool();

            if (weapons.isMember("etf_damage") && weapons["etf_damage"].isInt())
                player->client->pers.skills.etf_damage = static_cast<int8_t>(weapons["etf_damage"].asInt());
            if (weapons.isMember("etf_speed") && weapons["etf_speed"].isInt())
                player->client->pers.skills.etf_speed = static_cast<int8_t>(weapons["etf_speed"].asInt());
            if (weapons.isMember("etf_kick") && weapons["etf_kick"].isInt())
                player->client->pers.skills.etf_kick = static_cast<int8_t>(weapons["etf_kick"].asInt());
            if (weapons.isMember("etf_silent") && weapons["etf_silent"].isBool())
                player->client->pers.skills.etf_silent = weapons["etf_silent"].asBool();

            if (weapons.isMember("ir_damage") && weapons["ir_damage"].isInt())
                player->client->pers.skills.ir_damage = static_cast<int8_t>(weapons["ir_damage"].asInt());
            if (weapons.isMember("ir_speed") && weapons["ir_speed"].isInt())
                player->client->pers.skills.ir_speed = static_cast<int8_t>(weapons["ir_speed"].asInt());
            if (weapons.isMember("ir_trails") && weapons["ir_trails"].isBool())
                player->client->pers.skills.ir_trails = weapons["ir_trails"].asBool();
            if (weapons.isMember("ir_silent") && weapons["ir_silent"].isBool())
                player->client->pers.skills.ir_silent = weapons["ir_silent"].asBool();

            if (weapons.isMember("pb_damage") && weapons["pb_damage"].isInt())
                player->client->pers.skills.pb_damage = static_cast<int8_t>(weapons["pb_damage"].asInt());
            if (weapons.isMember("pb_burn") && weapons["pb_burn"].isInt())
                player->client->pers.skills.pb_burn = static_cast<int8_t>(weapons["pb_burn"].asInt());
            if (weapons.isMember("pb_pierce") && weapons["pb_pierce"].isInt())
                player->client->pers.skills.pb_pierce = static_cast<int8_t>(weapons["pb_pierce"].asInt());
            if (weapons.isMember("pb_silent") && weapons["pb_silent"].isBool())
                player->client->pers.skills.pb_silent = weapons["pb_silent"].asBool();

            if (weapons.isMember("rg_damage") && weapons["rg_damage"].isInt())
                player->client->pers.skills.rg_damage = static_cast<int8_t>(weapons["rg_damage"].asInt());
            if (weapons.isMember("rg_burn") && weapons["rg_burn"].isInt())
                player->client->pers.skills.rg_burn = static_cast<int8_t>(weapons["rg_burn"].asInt());
            if (weapons.isMember("rg_pierce") && weapons["rg_pierce"].isInt())
                player->client->pers.skills.rg_pierce = static_cast<int8_t>(weapons["rg_pierce"].asInt());
            if (weapons.isMember("rg_trails") && weapons["rg_trails"].isBool())
                player->client->pers.skills.rg_trails = weapons["rg_trails"].asBool();
            if (weapons.isMember("rg_silent") && weapons["rg_silent"].isBool())
                player->client->pers.skills.rg_silent = weapons["rg_silent"].asBool();

            if (weapons.isMember("bfg_damage") && weapons["bfg_damage"].isInt())
                player->client->pers.skills.bfg_damage = static_cast<int8_t>(weapons["bfg_damage"].asInt());
            if (weapons.isMember("bfg_speed") && weapons["bfg_speed"].isInt())
                player->client->pers.skills.bfg_speed = static_cast<int8_t>(weapons["bfg_speed"].asInt());
            if (weapons.isMember("bfg_duration") && weapons["bfg_duration"].isInt())
                player->client->pers.skills.bfg_duration = static_cast<int8_t>(weapons["bfg_duration"].asInt());
            if (weapons.isMember("bfg_silent") && weapons["bfg_silent"].isBool())
                player->client->pers.skills.bfg_silent = weapons["bfg_silent"].asBool();

            if (weapons.isMember("cannon20mm_damage") && weapons["cannon20mm_damage"].isInt())
                player->client->pers.skills.cannon20mm_damage = static_cast<int8_t>(weapons["cannon20mm_damage"].asInt());
            if (weapons.isMember("cannon20mm_range") && weapons["cannon20mm_range"].isInt())
                player->client->pers.skills.cannon20mm_range = static_cast<int8_t>(weapons["cannon20mm_range"].asInt());
            if (weapons.isMember("cannon20mm_recoil") && weapons["cannon20mm_recoil"].isInt())
                player->client->pers.skills.cannon20mm_recoil = static_cast<int8_t>(weapons["cannon20mm_recoil"].asInt());
            if (weapons.isMember("cannon20mm_silent") && weapons["cannon20mm_silent"].isBool())
                player->client->pers.skills.cannon20mm_silent = weapons["cannon20mm_silent"].asBool();

            if (weapons.isMember("pl_damage") && weapons["pl_damage"].isInt())
                player->client->pers.skills.pl_damage = static_cast<int8_t>(weapons["pl_damage"].asInt());
            if (weapons.isMember("pl_range") && weapons["pl_range"].isInt())
                player->client->pers.skills.pl_range = static_cast<int8_t>(weapons["pl_range"].asInt());
            if (weapons.isMember("pl_radius") && weapons["pl_radius"].isInt())
                player->client->pers.skills.pl_radius = static_cast<int8_t>(weapons["pl_radius"].asInt());
            if (weapons.isMember("pl_trails") && weapons["pl_trails"].isBool())
                player->client->pers.skills.pl_trails = weapons["pl_trails"].asBool();
            if (weapons.isMember("pl_silent") && weapons["pl_silent"].isBool())
                player->client->pers.skills.pl_silent = weapons["pl_silent"].asBool();
            if (weapons.isMember("pl_improved_traps") && weapons["pl_improved_traps"].isBool())
                player->client->pers.skills.pl_improved_traps = weapons["pl_improved_traps"].asBool();

            if (weapons.isMember("cf_damage") && weapons["cf_damage"].isInt())
                player->client->pers.skills.cf_damage = static_cast<int8_t>(weapons["cf_damage"].asInt());
            if (weapons.isMember("cf_range") && weapons["cf_range"].isInt())
                player->client->pers.skills.cf_range = static_cast<int8_t>(weapons["cf_range"].asInt());
            if (weapons.isMember("cf_silent") && weapons["cf_silent"].isBool())
                player->client->pers.skills.cf_silent = weapons["cf_silent"].asBool();

            if (weapons.isMember("tesla_damage") && weapons["tesla_damage"].isInt())
                player->client->pers.skills.tesla_damage = static_cast<int8_t>(weapons["tesla_damage"].asInt());
            if (weapons.isMember("tesla_range") && weapons["tesla_range"].isInt())
                player->client->pers.skills.tesla_range = static_cast<int8_t>(weapons["tesla_range"].asInt());
            if (weapons.isMember("tesla_radius") && weapons["tesla_radius"].isInt())
                player->client->pers.skills.tesla_radius = static_cast<int8_t>(weapons["tesla_radius"].asInt());

            if (weapons.isMember("trap_damage") && weapons["trap_damage"].isInt())
                player->client->pers.skills.trap_damage = static_cast<int8_t>(weapons["trap_damage"].asInt());
            if (weapons.isMember("trap_range") && weapons["trap_range"].isInt())
                player->client->pers.skills.trap_range = static_cast<int8_t>(weapons["trap_range"].asInt());
            if (weapons.isMember("trap_radius") && weapons["trap_radius"].isInt())
                player->client->pers.skills.trap_radius = static_cast<int8_t>(weapons["trap_radius"].asInt());

            if (weapons.isMember("phalanx_damage") && weapons["phalanx_damage"].isInt())
                player->client->pers.skills.phalanx_damage = static_cast<int8_t>(weapons["phalanx_damage"].asInt());
            if (weapons.isMember("phalanx_speed") && weapons["phalanx_speed"].isInt())
                player->client->pers.skills.phalanx_speed = static_cast<int8_t>(weapons["phalanx_speed"].asInt());
            if (weapons.isMember("phalanx_radius") && weapons["phalanx_radius"].isInt())
                player->client->pers.skills.phalanx_radius = static_cast<int8_t>(weapons["phalanx_radius"].asInt());
            if (weapons.isMember("phalanx_silent") && weapons["phalanx_silent"].isBool())
                player->client->pers.skills.phalanx_silent = weapons["phalanx_silent"].asBool();

            if (weapons.isMember("disruptor_damage") && weapons["disruptor_damage"].isInt())
                player->client->pers.skills.disruptor_damage = static_cast<int8_t>(weapons["disruptor_damage"].asInt());
            if (weapons.isMember("disruptor_speed") && weapons["disruptor_speed"].isInt())
                player->client->pers.skills.disruptor_speed = static_cast<int8_t>(weapons["disruptor_speed"].asInt());
            if (weapons.isMember("disruptor_duration") && weapons["disruptor_duration"].isInt())
                player->client->pers.skills.disruptor_duration = static_cast<int8_t>(weapons["disruptor_duration"].asInt());
            if (weapons.isMember("disruptor_silent") && weapons["disruptor_silent"].isBool())
                player->client->pers.skills.disruptor_silent = weapons["disruptor_silent"].asBool();
        }
    }

    // Migration: Convert old level 1 starting characters to level 0
    // (Old system started at level 1, new system starts at level 0)
    if (player->client->pers.pvm_level == 1 && player->client->pers.pvm_xp == 0)
    {
        player->client->pers.pvm_level = 0;
        // gi.Com_PrintFmt("Character: Migrated {} from old level 1 start to level 0\n", player_name);
        Character_Save(player); // Save the migrated data
    }

    // gi.Com_PrintFmt("Character: Loaded character for {} (respawn weapon: {}, PvM level: {})\n",
                    // player_name,
                    // player->client->pers.respawn_weapon_name,
                    // player->client->pers.pvm_level);

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

    // Additional check: Don't save if name starts with "[BOT]"
    const char* player_name = GetPlayerName(player);
    if (player_name && strncmp(player_name, "[BOT]", 5) == 0)
        return false;

    // Build JSON
    Json::Value root;
    root["player_name"] = player_name;
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

    // Save skill-based upgrades
    root["stats"]["skill_points"] = player->client->pers.skill_points;
    root["stats"]["weapon_points"] = player->client->pers.weapon_points;
    root["stats"]["skills"]["vampire"] = player->client->pers.skills.vampire;
    root["stats"]["skills"]["ammo_regen"] = player->client->pers.skills.ammo_regen;
    root["stats"]["skills"]["vitality"] = player->client->pers.skills.vitality;
    root["stats"]["skills"]["ha_pickup"] = player->client->pers.skills.ha_pickup;
    root["stats"]["skills"]["start_armor"] = player->client->pers.skills.start_armor;
    root["stats"]["skills"]["max_ammo"] = player->client->pers.skills.max_ammo;
    root["stats"]["skills"]["auto_haste"] = player->client->pers.skills.auto_haste;
    root["stats"]["skills"]["armor_vampirism"] = player->client->pers.skills.armor_vampirism;
    root["stats"]["skills"]["sentry_upgrade"] = player->client->pers.skills.sentry_upgrade;
    root["stats"]["skills"]["tesla_chain"] = player->client->pers.skills.tesla_chain;
    root["stats"]["skills"]["fireball"] = player->client->pers.skills.fireball;
    root["stats"]["skills"]["pc_regen"] = player->client->pers.skills.pc_regen;
    root["stats"]["skills"]["sentrygun"] = player->client->pers.skills.sentrygun;
    root["stats"]["skills"]["lasers"] = player->client->pers.skills.lasers;
    root["stats"]["skills"]["monster_summon"] = player->client->pers.skills.monster_summon;
    // Save free bonuses from milestones (permanent, not resetable)
    root["stats"]["skills"]["free_vitality"] = player->client->pers.skills.free_vitality;
    root["stats"]["skills"]["free_max_ammo"] = player->client->pers.skills.free_max_ammo;
    root["stats"]["skills"]["free_pc_regen"] = player->client->pers.skills.free_pc_regen;

    // Save power cubes currency
    root["stats"]["horde_power_cubes"] = player->client->pers.horde_power_cubes;

    // Save weapon upgrades
    root["stats"]["weapons"]["gl_damage"] = player->client->pers.skills.gl_damage;
    root["stats"]["weapons"]["gl_range"] = player->client->pers.skills.gl_range;
    root["stats"]["weapons"]["gl_radius"] = player->client->pers.skills.gl_radius;
    root["stats"]["weapons"]["gl_trails"] = player->client->pers.skills.gl_trails;
    root["stats"]["weapons"]["gl_silent"] = player->client->pers.skills.gl_silent;
    root["stats"]["weapons"]["gl_bouncy"] = player->client->pers.skills.gl_bouncy;

    root["stats"]["weapons"]["rl_damage"] = player->client->pers.skills.rl_damage;
    root["stats"]["weapons"]["rl_speed"] = player->client->pers.skills.rl_speed;
    root["stats"]["weapons"]["rl_radius"] = player->client->pers.skills.rl_radius;
    root["stats"]["weapons"]["rl_trails"] = player->client->pers.skills.rl_trails;
    root["stats"]["weapons"]["rl_silent"] = player->client->pers.skills.rl_silent;

    root["stats"]["weapons"]["mg_damage"] = player->client->pers.skills.mg_damage;
    root["stats"]["weapons"]["mg_pierce"] = player->client->pers.skills.mg_pierce;
    root["stats"]["weapons"]["mg_tracers"] = player->client->pers.skills.mg_tracers;
    root["stats"]["weapons"]["mg_spread"] = player->client->pers.skills.mg_spread;
    root["stats"]["weapons"]["mg_silent"] = player->client->pers.skills.mg_silent;

    root["stats"]["weapons"]["cg_damage"] = player->client->pers.skills.cg_damage;
    root["stats"]["weapons"]["cg_spin"] = player->client->pers.skills.cg_spin;
    root["stats"]["weapons"]["cg_tracers"] = player->client->pers.skills.cg_tracers;
    root["stats"]["weapons"]["cg_spread"] = player->client->pers.skills.cg_spread;
    root["stats"]["weapons"]["cg_silent"] = player->client->pers.skills.cg_silent;

    root["stats"]["weapons"]["sg_damage"] = player->client->pers.skills.sg_damage;
    root["stats"]["weapons"]["sg_strike"] = player->client->pers.skills.sg_strike;
    root["stats"]["weapons"]["sg_pellets"] = player->client->pers.skills.sg_pellets;
    root["stats"]["weapons"]["sg_spread"] = player->client->pers.skills.sg_spread;
    root["stats"]["weapons"]["sg_silent"] = player->client->pers.skills.sg_silent;
    root["stats"]["weapons"]["sg_energized"] = player->client->pers.skills.sg_energized;

    root["stats"]["weapons"]["ssg_damage"] = player->client->pers.skills.ssg_damage;
    root["stats"]["weapons"]["ssg_strike"] = player->client->pers.skills.ssg_strike;
    root["stats"]["weapons"]["ssg_pellets"] = player->client->pers.skills.ssg_pellets;
    root["stats"]["weapons"]["ssg_spread"] = player->client->pers.skills.ssg_spread;
    root["stats"]["weapons"]["ssg_silent"] = player->client->pers.skills.ssg_silent;
    root["stats"]["weapons"]["ssg_energized"] = player->client->pers.skills.ssg_energized;

    root["stats"]["weapons"]["hg_damage"] = player->client->pers.skills.hg_damage;
    root["stats"]["weapons"]["hg_range"] = player->client->pers.skills.hg_range;
    root["stats"]["weapons"]["hg_radius_damage"] = player->client->pers.skills.hg_radius_damage;

    root["stats"]["weapons"]["bl_damage"] = player->client->pers.skills.bl_damage;
    root["stats"]["weapons"]["bl_speed"] = player->client->pers.skills.bl_speed;
    root["stats"]["weapons"]["bl_trails"] = player->client->pers.skills.bl_trails;
    root["stats"]["weapons"]["bl_silent"] = player->client->pers.skills.bl_silent;

    root["stats"]["weapons"]["hb_damage"] = player->client->pers.skills.hb_damage;
    root["stats"]["weapons"]["hb_speed"] = player->client->pers.skills.hb_speed;
    root["stats"]["weapons"]["hb_trails"] = player->client->pers.skills.hb_trails;
    root["stats"]["weapons"]["hb_silent"] = player->client->pers.skills.hb_silent;

    root["stats"]["weapons"]["etf_damage"] = player->client->pers.skills.etf_damage;
    root["stats"]["weapons"]["etf_speed"] = player->client->pers.skills.etf_speed;
    root["stats"]["weapons"]["etf_kick"] = player->client->pers.skills.etf_kick;
    root["stats"]["weapons"]["etf_silent"] = player->client->pers.skills.etf_silent;

    root["stats"]["weapons"]["ir_damage"] = player->client->pers.skills.ir_damage;
    root["stats"]["weapons"]["ir_speed"] = player->client->pers.skills.ir_speed;
    root["stats"]["weapons"]["ir_trails"] = player->client->pers.skills.ir_trails;
    root["stats"]["weapons"]["ir_silent"] = player->client->pers.skills.ir_silent;

    root["stats"]["weapons"]["pb_damage"] = player->client->pers.skills.pb_damage;
    root["stats"]["weapons"]["pb_burn"] = player->client->pers.skills.pb_burn;
    root["stats"]["weapons"]["pb_pierce"] = player->client->pers.skills.pb_pierce;
    root["stats"]["weapons"]["pb_silent"] = player->client->pers.skills.pb_silent;

    root["stats"]["weapons"]["rg_damage"] = player->client->pers.skills.rg_damage;
    root["stats"]["weapons"]["rg_burn"] = player->client->pers.skills.rg_burn;
    root["stats"]["weapons"]["rg_pierce"] = player->client->pers.skills.rg_pierce;
    root["stats"]["weapons"]["rg_trails"] = player->client->pers.skills.rg_trails;
    root["stats"]["weapons"]["rg_silent"] = player->client->pers.skills.rg_silent;

    root["stats"]["weapons"]["bfg_damage"] = player->client->pers.skills.bfg_damage;
    root["stats"]["weapons"]["bfg_speed"] = player->client->pers.skills.bfg_speed;
    root["stats"]["weapons"]["bfg_duration"] = player->client->pers.skills.bfg_duration;
    root["stats"]["weapons"]["bfg_silent"] = player->client->pers.skills.bfg_silent;

    root["stats"]["weapons"]["cannon20mm_damage"] = player->client->pers.skills.cannon20mm_damage;
    root["stats"]["weapons"]["cannon20mm_range"] = player->client->pers.skills.cannon20mm_range;
    root["stats"]["weapons"]["cannon20mm_recoil"] = player->client->pers.skills.cannon20mm_recoil;
    root["stats"]["weapons"]["cannon20mm_silent"] = player->client->pers.skills.cannon20mm_silent;

    root["stats"]["weapons"]["pl_damage"] = player->client->pers.skills.pl_damage;
    root["stats"]["weapons"]["pl_range"] = player->client->pers.skills.pl_range;
    root["stats"]["weapons"]["pl_radius"] = player->client->pers.skills.pl_radius;
    root["stats"]["weapons"]["pl_trails"] = player->client->pers.skills.pl_trails;
    root["stats"]["weapons"]["pl_silent"] = player->client->pers.skills.pl_silent;
    root["stats"]["weapons"]["pl_improved_traps"] = player->client->pers.skills.pl_improved_traps;

    root["stats"]["weapons"]["cf_damage"] = player->client->pers.skills.cf_damage;
    root["stats"]["weapons"]["cf_range"] = player->client->pers.skills.cf_range;
    root["stats"]["weapons"]["cf_silent"] = player->client->pers.skills.cf_silent;

    root["stats"]["weapons"]["tesla_damage"] = player->client->pers.skills.tesla_damage;
    root["stats"]["weapons"]["tesla_range"] = player->client->pers.skills.tesla_range;
    root["stats"]["weapons"]["tesla_radius"] = player->client->pers.skills.tesla_radius;

    root["stats"]["weapons"]["trap_damage"] = player->client->pers.skills.trap_damage;
    root["stats"]["weapons"]["trap_range"] = player->client->pers.skills.trap_range;
    root["stats"]["weapons"]["trap_radius"] = player->client->pers.skills.trap_radius;

    root["stats"]["weapons"]["phalanx_damage"] = player->client->pers.skills.phalanx_damage;
    root["stats"]["weapons"]["phalanx_speed"] = player->client->pers.skills.phalanx_speed;
    root["stats"]["weapons"]["phalanx_radius"] = player->client->pers.skills.phalanx_radius;
    root["stats"]["weapons"]["phalanx_silent"] = player->client->pers.skills.phalanx_silent;

    root["stats"]["weapons"]["disruptor_damage"] = player->client->pers.skills.disruptor_damage;
    root["stats"]["weapons"]["disruptor_speed"] = player->client->pers.skills.disruptor_speed;
    root["stats"]["weapons"]["disruptor_duration"] = player->client->pers.skills.disruptor_duration;
    root["stats"]["weapons"]["disruptor_silent"] = player->client->pers.skills.disruptor_silent;

    // Write to file
    std::string filepath = Character_GetFilePath(player);
    std::ofstream file(filepath, std::ios::out | std::ios::trunc);

    if (!file.is_open())
    {
        // gi.Com_PrintFmt("Character: Failed to save character file for {}\n",
                        // player_name);
        return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
    file.close();

    // gi.Com_PrintFmt("Character: Saved character for {}\n", player_name);
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
