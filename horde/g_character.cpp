#include "g_character.h"
#include "../g_local.h"

#include "sqlite3.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>

extern const char* GetPlayerName(const edict_t* player);

constexpr const char* DEFAULT_RESPAWN_WEAPON = "Rocket Launcher";

static sqlite3* s_character_db = nullptr;
static std::string s_character_db_path;
static bool s_shutdown_registered = false;

static void Character_CloseDatabase()
{
    if (s_character_db)
    {
        sqlite3_close(s_character_db);
        s_character_db = nullptr;
    }
}

static bool IsBotCharacter(edict_t* player)
{
    if (!player || !player->client)
        return true;

    if (player->svflags & SVF_BOT)
        return true;

    const char* player_name = GetPlayerName(player);
    return player_name && strncmp(player_name, "[BOT]", 5) == 0;
}

static std::string GetGameDirectory()
{
    cvar_t* gamedir = gi.cvar("gamedir", "", CVAR_NOFLAGS);
    if (gamedir && gamedir->string[0])
        return gamedir->string;

    cvar_t* game = gi.cvar("game", "", CVAR_NOFLAGS);
    if (game && game->string[0])
        return game->string;

    return "baseq2";
}

static std::string GetDatabasePath()
{
    std::filesystem::path path(GetGameDirectory());
    path /= "characters.db";
    return path.string();
}

static std::string GetCharacterKey(edict_t* player)
{
    if (!player || !player->client)
        return "unknown";

    return Character_SanitizeName(GetPlayerName(player));
}

static bool ExecSql(const char* sql)
{
    char* error = nullptr;
    if (sqlite3_exec(s_character_db, sql, nullptr, nullptr, &error) != SQLITE_OK)
    {
        gi.Com_PrintFmt("Character DB: SQL error: {}\n", error ? error : "unknown");
        sqlite3_free(error);
        return false;
    }

    return true;
}

static bool EnsureCharacterDatabase()
{
    if (s_character_db)
        return true;

    s_character_db_path = GetDatabasePath();

    try
    {
        std::filesystem::path db_path(s_character_db_path);
        if (db_path.has_parent_path())
            std::filesystem::create_directories(db_path.parent_path());
    }
    catch (const std::exception& e)
    {
        gi.Com_PrintFmt("Character DB: Could not create game directory: {}\n", e.what());
        return false;
    }

    if (sqlite3_open(s_character_db_path.c_str(), &s_character_db) != SQLITE_OK)
    {
        gi.Com_PrintFmt("Character DB: Failed to open {}: {}\n",
            s_character_db_path, sqlite3_errmsg(s_character_db));
        Character_CloseDatabase();
        return false;
    }

    if (!s_shutdown_registered)
    {
        std::atexit(Character_CloseDatabase);
        s_shutdown_registered = true;
    }

    if (!ExecSql(
        "CREATE TABLE IF NOT EXISTS characters ("
        "name TEXT PRIMARY KEY,"
        "respawn_weapon TEXT,"
        "id_display INTEGER,"
        "iddmg_display INTEGER,"
        "sentry_gun_choice INTEGER,"
        "morph_preference INTEGER,"
        "pvm_level INTEGER,"
        "pvm_xp INTEGER,"
        "pvm_stat_points INTEGER,"
        "pvm_max_ammo_level INTEGER,"
        "pvm_vitality_level INTEGER,"
        "skill_points INTEGER,"
        "weapon_points INTEGER,"
        "horde_power_cubes INTEGER,"
        "updated_at INTEGER"
        ");"))
    {
        Character_CloseDatabase();
        return false;
    }

    if (!ExecSql(
        "CREATE TABLE IF NOT EXISTS character_values ("
        "name TEXT,"
        "scope TEXT,"
        "key TEXT,"
        "value INTEGER,"
        "PRIMARY KEY(name, scope, key)"
        ");"))
    {
        Character_CloseDatabase();
        return false;
    }

    return true;
}

static bool BindText(sqlite3_stmt* stmt, int index, const std::string& value)
{
    return sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

static bool CharacterRowExists(const std::string& name)
{
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(s_character_db,
        "SELECT 1 FROM characters WHERE name = ? LIMIT 1;", -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    BindText(stmt, 1, name);
    bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

static bool SaveScopedValue(const std::string& name, const char* scope, const char* key, int value)
{
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(s_character_db,
        "INSERT OR REPLACE INTO character_values(name, scope, key, value) VALUES(?, ?, ?, ?);",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    BindText(stmt, 1, name);
    sqlite3_bind_text(stmt, 2, scope, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, key, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, value);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok && developer && developer->integer)
        gi.Com_PrintFmt("Character DB: Failed to save {}.{} for {}: {}\n", scope, key, name, sqlite3_errmsg(s_character_db));

    sqlite3_finalize(stmt);
    return ok;
}

static std::unordered_map<std::string, int> LoadScopedValues(const std::string& name, const char* scope)
{
    std::unordered_map<std::string, int> values;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(s_character_db,
        "SELECT key, value FROM character_values WHERE name = ? AND scope = ?;",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        return values;
    }

    BindText(stmt, 1, name);
    sqlite3_bind_text(stmt, 2, scope, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char* key = sqlite3_column_text(stmt, 0);
        if (key)
            values.emplace(reinterpret_cast<const char*>(key), sqlite3_column_int(stmt, 1));
    }

    sqlite3_finalize(stmt);
    return values;
}

static bool SaveCharacterRow(edict_t* player, const std::string& name)
{
    if (player->client->pers.respawn_weapon_name[0] == '\0')
    {
        Q_strlcpy(player->client->pers.respawn_weapon_name,
            DEFAULT_RESPAWN_WEAPON,
            sizeof(player->client->pers.respawn_weapon_name));
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(s_character_db,
        "INSERT OR REPLACE INTO characters("
        "name, respawn_weapon, id_display, iddmg_display, sentry_gun_choice, morph_preference,"
        "pvm_level, pvm_xp, pvm_stat_points, pvm_max_ammo_level, pvm_vitality_level,"
        "skill_points, weapon_points, horde_power_cubes, updated_at"
        ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s','now'));",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        gi.Com_PrintFmt("Character DB: Failed to prepare character save: {}\n", sqlite3_errmsg(s_character_db));
        return false;
    }

    BindText(stmt, 1, name);
    sqlite3_bind_text(stmt, 2, player->client->pers.respawn_weapon_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, player->client->pers.id_state ? 1 : 0);
    sqlite3_bind_int(stmt, 4, player->client->pers.iddmg_state ? 1 : 0);
    sqlite3_bind_int(stmt, 5, static_cast<int>(player->client->pers.sentry_gun_choice));
    sqlite3_bind_int(stmt, 6, player->client->pers.morph_preference);
    sqlite3_bind_int(stmt, 7, player->client->pers.pvm_level);
    sqlite3_bind_int(stmt, 8, player->client->pers.pvm_xp);
    sqlite3_bind_int(stmt, 9, player->client->pers.pvm_stat_points);
    sqlite3_bind_int(stmt, 10, player->client->pers.pvm_max_ammo_level);
    sqlite3_bind_int(stmt, 11, player->client->pers.pvm_vitality_level);
    sqlite3_bind_int(stmt, 12, player->client->pers.skill_points);
    sqlite3_bind_int(stmt, 13, player->client->pers.weapon_points);
    sqlite3_bind_int(stmt, 14, player->client->pers.horde_power_cubes);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok)
        gi.Com_PrintFmt("Character DB: Failed to save row for {}: {}\n", name, sqlite3_errmsg(s_character_db));

    sqlite3_finalize(stmt);
    return ok;
}

static bool LoadCharacterRow(edict_t* player, const std::string& name)
{
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(s_character_db,
        "SELECT respawn_weapon, id_display, iddmg_display, sentry_gun_choice, morph_preference,"
        "pvm_level, pvm_xp, pvm_stat_points, pvm_max_ammo_level, pvm_vitality_level,"
        "skill_points, weapon_points, horde_power_cubes "
        "FROM characters WHERE name = ?;",
        -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    BindText(stmt, 1, name);
    if (sqlite3_step(stmt) != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        return false;
    }

    const unsigned char* respawn_weapon = sqlite3_column_text(stmt, 0);
    Q_strlcpy(player->client->pers.respawn_weapon_name,
        respawn_weapon ? reinterpret_cast<const char*>(respawn_weapon) : DEFAULT_RESPAWN_WEAPON,
        sizeof(player->client->pers.respawn_weapon_name));
    player->client->pers.id_state = sqlite3_column_int(stmt, 1) != 0;
    player->client->pers.iddmg_state = sqlite3_column_int(stmt, 2) != 0;
    player->client->pers.sentry_gun_choice = static_cast<sentrytype_t>(sqlite3_column_int(stmt, 3));
    player->client->resp.sentry_gun_choice = player->client->pers.sentry_gun_choice;
    player->client->pers.morph_preference = sqlite3_column_int(stmt, 4);
    player->client->pers.pvm_level = sqlite3_column_int(stmt, 5);
    player->client->pers.pvm_xp = sqlite3_column_int(stmt, 6);
    player->client->pers.pvm_stat_points = sqlite3_column_int(stmt, 7);
    player->client->pers.pvm_max_ammo_level = sqlite3_column_int(stmt, 8);
    player->client->pers.pvm_vitality_level = sqlite3_column_int(stmt, 9);
    player->client->pers.skill_points = sqlite3_column_int(stmt, 10);
    player->client->pers.weapon_points = sqlite3_column_int(stmt, 11);
    player->client->pers.horde_power_cubes = sqlite3_column_int(stmt, 12);

    sqlite3_finalize(stmt);
    return true;
}

static void ApplyDefaultCharacter(edict_t* player)
{
    Q_strlcpy(player->client->pers.respawn_weapon_name,
        DEFAULT_RESPAWN_WEAPON,
        sizeof(player->client->pers.respawn_weapon_name));
    player->client->pers.pvm_level = 0;
    player->client->pers.pvm_xp = 0;
    player->client->pers.pvm_stat_points = 0;
    player->client->pers.pvm_max_ammo_level = 0;
    player->client->pers.pvm_vitality_level = 0;
    player->client->pers.skill_points = 0;
    player->client->pers.weapon_points = 0;
    player->client->pers.horde_power_cubes = 0;
    player->client->pers.skills = {};
}

void Character_Init()
{
    if (EnsureCharacterDatabase())
        gi.Com_PrintFmt("Character system: Initialized SQLite database ({})\n", s_character_db_path);
}

std::string Character_SanitizeName(const char* name)
{
    if (!name || !name[0])
        return "unknown";

    if (strcmp(name, "N/A") == 0 || strcmp(name, "n/a") == 0)
        return "unknown";

    std::string sanitized;
    sanitized.reserve(MAX_NETNAME);

    for (const char* p = name; *p && sanitized.length() < MAX_NETNAME - 1; ++p)
    {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')
        {
            sanitized += c;
        }
        else if (c == ' ')
        {
            sanitized += '_';
        }
    }

    return sanitized.empty() ? "unknown" : sanitized;
}

std::string Character_GetFilePath(edict_t*)
{
    return s_character_db_path.empty() ? GetDatabasePath() : s_character_db_path;
}

void Character_CreateDefault(edict_t* player)
{
    if (IsBotCharacter(player) || !EnsureCharacterDatabase())
        return;

    ApplyDefaultCharacter(player);
    SaveCharacterRow(player, GetCharacterKey(player));
}

bool Character_Load(edict_t* player)
{
    if (IsBotCharacter(player) || !EnsureCharacterDatabase())
        return false;

    const std::string name = GetCharacterKey(player);
    if (!CharacterRowExists(name))
    {
        Character_CreateDefault(player);
        return false;
    }

    if (!LoadCharacterRow(player, name))
        return false;

    auto skill_values = LoadScopedValues(name, "skills");
    auto weapon_values = LoadScopedValues(name, "weapons");

#define LOAD_SKILL(scope_values, field) \
    do { \
        auto it = scope_values.find(#field); \
        if (it != scope_values.end()) \
            player->client->pers.skills.field = static_cast<decltype(player->client->pers.skills.field)>(it->second); \
    } while (0)

    LOAD_SKILL(skill_values, vampire);
    LOAD_SKILL(skill_values, ammo_regen);
    LOAD_SKILL(skill_values, vitality);
    LOAD_SKILL(skill_values, ha_pickup);
    LOAD_SKILL(skill_values, start_armor);
    LOAD_SKILL(skill_values, max_ammo);
    LOAD_SKILL(skill_values, free_vitality);
    LOAD_SKILL(skill_values, free_max_ammo);
    LOAD_SKILL(skill_values, auto_haste);
    LOAD_SKILL(skill_values, teleport_fwd);
    LOAD_SKILL(skill_values, armor_vampirism);
    LOAD_SKILL(skill_values, sentry_upgrade);
    LOAD_SKILL(skill_values, tesla_chain);
    LOAD_SKILL(skill_values, fireball);
    LOAD_SKILL(skill_values, pc_regen);
    LOAD_SKILL(skill_values, free_pc_regen);
    LOAD_SKILL(skill_values, sentrygun);
    LOAD_SKILL(skill_values, lasers);
    LOAD_SKILL(skill_values, monster_summon);
    LOAD_SKILL(skill_values, exploding_barrel);
    LOAD_SKILL(skill_values, bombspell);

    LOAD_SKILL(weapon_values, gl_damage);
    LOAD_SKILL(weapon_values, gl_range);
    LOAD_SKILL(weapon_values, gl_radius);
    LOAD_SKILL(weapon_values, gl_trails);
    LOAD_SKILL(weapon_values, gl_silent);
    LOAD_SKILL(weapon_values, gl_bouncy);
    LOAD_SKILL(weapon_values, rl_damage);
    LOAD_SKILL(weapon_values, rl_speed);
    LOAD_SKILL(weapon_values, rl_radius);
    LOAD_SKILL(weapon_values, rl_trails);
    LOAD_SKILL(weapon_values, rl_silent);
    LOAD_SKILL(weapon_values, mg_damage);
    LOAD_SKILL(weapon_values, mg_pierce);
    LOAD_SKILL(weapon_values, mg_tracers);
    LOAD_SKILL(weapon_values, mg_spread);
    LOAD_SKILL(weapon_values, mg_silent);
    LOAD_SKILL(weapon_values, cg_damage);
    LOAD_SKILL(weapon_values, cg_spin);
    LOAD_SKILL(weapon_values, cg_tracers);
    LOAD_SKILL(weapon_values, cg_spread);
    LOAD_SKILL(weapon_values, cg_silent);
    LOAD_SKILL(weapon_values, sg_damage);
    LOAD_SKILL(weapon_values, sg_strike);
    LOAD_SKILL(weapon_values, sg_pellets);
    LOAD_SKILL(weapon_values, sg_spread);
    LOAD_SKILL(weapon_values, sg_silent);
    LOAD_SKILL(weapon_values, sg_energized);
    LOAD_SKILL(weapon_values, ssg_damage);
    LOAD_SKILL(weapon_values, ssg_strike);
    LOAD_SKILL(weapon_values, ssg_pellets);
    LOAD_SKILL(weapon_values, ssg_spread);
    LOAD_SKILL(weapon_values, ssg_silent);
    LOAD_SKILL(weapon_values, ssg_energized);
    LOAD_SKILL(weapon_values, hg_damage);
    LOAD_SKILL(weapon_values, hg_range);
    LOAD_SKILL(weapon_values, hg_radius_damage);
    LOAD_SKILL(weapon_values, bl_damage);
    LOAD_SKILL(weapon_values, bl_speed);
    LOAD_SKILL(weapon_values, bl_trails);
    LOAD_SKILL(weapon_values, bl_silent);
    LOAD_SKILL(weapon_values, hb_damage);
    LOAD_SKILL(weapon_values, hb_speed);
    LOAD_SKILL(weapon_values, hb_trails);
    LOAD_SKILL(weapon_values, hb_silent);
    LOAD_SKILL(weapon_values, etf_damage);
    LOAD_SKILL(weapon_values, etf_speed);
    LOAD_SKILL(weapon_values, etf_kick);
    LOAD_SKILL(weapon_values, etf_silent);
    LOAD_SKILL(weapon_values, ir_damage);
    LOAD_SKILL(weapon_values, ir_speed);
    LOAD_SKILL(weapon_values, ir_trails);
    LOAD_SKILL(weapon_values, ir_silent);
    LOAD_SKILL(weapon_values, pb_damage);
    LOAD_SKILL(weapon_values, pb_burn);
    LOAD_SKILL(weapon_values, pb_pierce);
    LOAD_SKILL(weapon_values, pb_silent);
    LOAD_SKILL(weapon_values, rg_damage);
    LOAD_SKILL(weapon_values, rg_burn);
    LOAD_SKILL(weapon_values, rg_pierce);
    LOAD_SKILL(weapon_values, rg_trails);
    LOAD_SKILL(weapon_values, rg_silent);
    LOAD_SKILL(weapon_values, bfg_damage);
    LOAD_SKILL(weapon_values, bfg_speed);
    LOAD_SKILL(weapon_values, bfg_duration);
    LOAD_SKILL(weapon_values, bfg_silent);
    LOAD_SKILL(weapon_values, cannon20mm_damage);
    LOAD_SKILL(weapon_values, cannon20mm_range);
    LOAD_SKILL(weapon_values, cannon20mm_recoil);
    LOAD_SKILL(weapon_values, cannon20mm_silent);
    LOAD_SKILL(weapon_values, pl_damage);
    LOAD_SKILL(weapon_values, pl_range);
    LOAD_SKILL(weapon_values, pl_radius);
    LOAD_SKILL(weapon_values, pl_trails);
    LOAD_SKILL(weapon_values, pl_silent);
    LOAD_SKILL(weapon_values, pl_improved_traps);
    LOAD_SKILL(weapon_values, cf_damage);
    LOAD_SKILL(weapon_values, cf_range);
    LOAD_SKILL(weapon_values, cf_silent);
    LOAD_SKILL(weapon_values, tesla_damage);
    LOAD_SKILL(weapon_values, tesla_range);
    LOAD_SKILL(weapon_values, tesla_radius);
    LOAD_SKILL(weapon_values, trap_damage);
    LOAD_SKILL(weapon_values, trap_range);
    LOAD_SKILL(weapon_values, trap_radius);
    LOAD_SKILL(weapon_values, phalanx_damage);
    LOAD_SKILL(weapon_values, phalanx_speed);
    LOAD_SKILL(weapon_values, phalanx_radius);
    LOAD_SKILL(weapon_values, phalanx_silent);
    LOAD_SKILL(weapon_values, disruptor_damage);
    LOAD_SKILL(weapon_values, disruptor_speed);
    LOAD_SKILL(weapon_values, disruptor_duration);
    LOAD_SKILL(weapon_values, disruptor_silent);

#undef LOAD_SKILL

    return true;
}

bool Character_Save(edict_t* player)
{
    if (IsBotCharacter(player) || !EnsureCharacterDatabase())
        return false;

    const std::string name = GetCharacterKey(player);
    ExecSql("BEGIN IMMEDIATE TRANSACTION;");

    bool ok = SaveCharacterRow(player, name);

#define SAVE_SKILL(scope, field) \
    ok = SaveScopedValue(name, scope, #field, static_cast<int>(player->client->pers.skills.field)) && ok

    SAVE_SKILL("skills", vampire);
    SAVE_SKILL("skills", ammo_regen);
    SAVE_SKILL("skills", vitality);
    SAVE_SKILL("skills", ha_pickup);
    SAVE_SKILL("skills", start_armor);
    SAVE_SKILL("skills", max_ammo);
    SAVE_SKILL("skills", free_vitality);
    SAVE_SKILL("skills", free_max_ammo);
    SAVE_SKILL("skills", auto_haste);
    SAVE_SKILL("skills", teleport_fwd);
    SAVE_SKILL("skills", armor_vampirism);
    SAVE_SKILL("skills", sentry_upgrade);
    SAVE_SKILL("skills", tesla_chain);
    SAVE_SKILL("skills", fireball);
    SAVE_SKILL("skills", pc_regen);
    SAVE_SKILL("skills", free_pc_regen);
    SAVE_SKILL("skills", sentrygun);
    SAVE_SKILL("skills", lasers);
    SAVE_SKILL("skills", monster_summon);
    SAVE_SKILL("skills", exploding_barrel);
    SAVE_SKILL("skills", bombspell);

    SAVE_SKILL("weapons", gl_damage);
    SAVE_SKILL("weapons", gl_range);
    SAVE_SKILL("weapons", gl_radius);
    SAVE_SKILL("weapons", gl_trails);
    SAVE_SKILL("weapons", gl_silent);
    SAVE_SKILL("weapons", gl_bouncy);
    SAVE_SKILL("weapons", rl_damage);
    SAVE_SKILL("weapons", rl_speed);
    SAVE_SKILL("weapons", rl_radius);
    SAVE_SKILL("weapons", rl_trails);
    SAVE_SKILL("weapons", rl_silent);
    SAVE_SKILL("weapons", mg_damage);
    SAVE_SKILL("weapons", mg_pierce);
    SAVE_SKILL("weapons", mg_tracers);
    SAVE_SKILL("weapons", mg_spread);
    SAVE_SKILL("weapons", mg_silent);
    SAVE_SKILL("weapons", cg_damage);
    SAVE_SKILL("weapons", cg_spin);
    SAVE_SKILL("weapons", cg_tracers);
    SAVE_SKILL("weapons", cg_spread);
    SAVE_SKILL("weapons", cg_silent);
    SAVE_SKILL("weapons", sg_damage);
    SAVE_SKILL("weapons", sg_strike);
    SAVE_SKILL("weapons", sg_pellets);
    SAVE_SKILL("weapons", sg_spread);
    SAVE_SKILL("weapons", sg_silent);
    SAVE_SKILL("weapons", sg_energized);
    SAVE_SKILL("weapons", ssg_damage);
    SAVE_SKILL("weapons", ssg_strike);
    SAVE_SKILL("weapons", ssg_pellets);
    SAVE_SKILL("weapons", ssg_spread);
    SAVE_SKILL("weapons", ssg_silent);
    SAVE_SKILL("weapons", ssg_energized);
    SAVE_SKILL("weapons", hg_damage);
    SAVE_SKILL("weapons", hg_range);
    SAVE_SKILL("weapons", hg_radius_damage);
    SAVE_SKILL("weapons", bl_damage);
    SAVE_SKILL("weapons", bl_speed);
    SAVE_SKILL("weapons", bl_trails);
    SAVE_SKILL("weapons", bl_silent);
    SAVE_SKILL("weapons", hb_damage);
    SAVE_SKILL("weapons", hb_speed);
    SAVE_SKILL("weapons", hb_trails);
    SAVE_SKILL("weapons", hb_silent);
    SAVE_SKILL("weapons", etf_damage);
    SAVE_SKILL("weapons", etf_speed);
    SAVE_SKILL("weapons", etf_kick);
    SAVE_SKILL("weapons", etf_silent);
    SAVE_SKILL("weapons", ir_damage);
    SAVE_SKILL("weapons", ir_speed);
    SAVE_SKILL("weapons", ir_trails);
    SAVE_SKILL("weapons", ir_silent);
    SAVE_SKILL("weapons", pb_damage);
    SAVE_SKILL("weapons", pb_burn);
    SAVE_SKILL("weapons", pb_pierce);
    SAVE_SKILL("weapons", pb_silent);
    SAVE_SKILL("weapons", rg_damage);
    SAVE_SKILL("weapons", rg_burn);
    SAVE_SKILL("weapons", rg_pierce);
    SAVE_SKILL("weapons", rg_trails);
    SAVE_SKILL("weapons", rg_silent);
    SAVE_SKILL("weapons", bfg_damage);
    SAVE_SKILL("weapons", bfg_speed);
    SAVE_SKILL("weapons", bfg_duration);
    SAVE_SKILL("weapons", bfg_silent);
    SAVE_SKILL("weapons", cannon20mm_damage);
    SAVE_SKILL("weapons", cannon20mm_range);
    SAVE_SKILL("weapons", cannon20mm_recoil);
    SAVE_SKILL("weapons", cannon20mm_silent);
    SAVE_SKILL("weapons", pl_damage);
    SAVE_SKILL("weapons", pl_range);
    SAVE_SKILL("weapons", pl_radius);
    SAVE_SKILL("weapons", pl_trails);
    SAVE_SKILL("weapons", pl_silent);
    SAVE_SKILL("weapons", pl_improved_traps);
    SAVE_SKILL("weapons", cf_damage);
    SAVE_SKILL("weapons", cf_range);
    SAVE_SKILL("weapons", cf_silent);
    SAVE_SKILL("weapons", tesla_damage);
    SAVE_SKILL("weapons", tesla_range);
    SAVE_SKILL("weapons", tesla_radius);
    SAVE_SKILL("weapons", trap_damage);
    SAVE_SKILL("weapons", trap_range);
    SAVE_SKILL("weapons", trap_radius);
    SAVE_SKILL("weapons", phalanx_damage);
    SAVE_SKILL("weapons", phalanx_speed);
    SAVE_SKILL("weapons", phalanx_radius);
    SAVE_SKILL("weapons", phalanx_silent);
    SAVE_SKILL("weapons", disruptor_damage);
    SAVE_SKILL("weapons", disruptor_speed);
    SAVE_SKILL("weapons", disruptor_duration);
    SAVE_SKILL("weapons", disruptor_silent);

#undef SAVE_SKILL

    ExecSql(ok ? "COMMIT;" : "ROLLBACK;");
    return ok;
}

bool Character_Reset(edict_t* player)
{
    if (IsBotCharacter(player) || !EnsureCharacterDatabase())
        return false;

    const std::string name = GetCharacterKey(player);
    sqlite3_stmt* stmt = nullptr;

    ExecSql("BEGIN IMMEDIATE TRANSACTION;");
    bool ok = sqlite3_prepare_v2(s_character_db,
        "DELETE FROM character_values WHERE name = ?;",
        -1, &stmt, nullptr) == SQLITE_OK;
    if (ok)
    {
        BindText(stmt, 1, name);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    sqlite3_finalize(stmt);

    stmt = nullptr;
    ok = ok && sqlite3_prepare_v2(s_character_db,
        "DELETE FROM characters WHERE name = ?;",
        -1, &stmt, nullptr) == SQLITE_OK;
    if (ok)
    {
        BindText(stmt, 1, name);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    sqlite3_finalize(stmt);

    ExecSql(ok ? "COMMIT;" : "ROLLBACK;");
    return ok;
}

void Character_SetRespawnWeapon(edict_t* player, const char* weapon_name)
{
    if (!player || !player->client || !weapon_name)
        return;

    Q_strlcpy(player->client->pers.respawn_weapon_name, weapon_name,
              sizeof(player->client->pers.respawn_weapon_name));

    Character_Save(player);
    gi.LocClient_Print(player, PRINT_HIGH, nullptr, "Respawn weapon set to: {}\n", weapon_name);
}

const char* Character_GetRespawnWeapon(edict_t* player)
{
    if (!player || !player->client)
        return DEFAULT_RESPAWN_WEAPON;

    if (player->client->pers.respawn_weapon_name[0] == '\0')
        return DEFAULT_RESPAWN_WEAPON;

    return player->client->pers.respawn_weapon_name;
}
