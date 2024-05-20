#include "../g_local.h"
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <chrono>
#include <random>
#include <algorithm>

static const int MAX_MONSTERS_BIG_MAP = 44;
static const int MAX_MONSTERS_MEDIUM_MAP = 18;
static const int MAX_MONSTERS_SMALL_MAP = 15;

int remainingMonsters = 0;
int current_wave_number = 1;
const int BOSS_TO_SPAWN = 1;
cvar_t* g_horde;
enum class horde_state_t
{
    warmup,
    spawning,
    cleanup,
    rest
};

static struct {
    gtime_t         warm_time = 5_sec;
    horde_state_t   state = horde_state_t::warmup;

    gtime_t         monster_spawn_time;
    int32_t         num_to_spawn = 0;
    int32_t         level = 0;
} g_horde_local;

bool next_wave_message_sent = false;
bool isMediumMap = true;
bool isSmallMap = false;
bool isBigMap = false;
int vampire_level = 0;
std::vector<std::string> shuffled_benefits;
std::unordered_set<std::string> obtained_benefits;

void IsMapSize(const std::string& mapname, bool& isSmallMap, bool& isBigMap, bool& isMediumMap) {
    static const std::unordered_set<std::string> smallMaps = {
        "q2dm3", "q2dm7", "q2dm2", "q2ctf4", "q64/dm10", "q64\\dm10",
        "q64/dm9", "q64\\dm9", "q64/dm7", "q64\\dm7", "q64/dm2",
        "q64\\dm2", "q64/dm1", "q64\\dm1", "fact3", "q2ctf4",
        "mgu3m4", "mgu4trial", "mgu6trial", "ec/base_ec", "mgdm1"
    };

    static const std::unordered_set<std::string> bigMaps = {
        "q2ctf5", "old/kmdm3", "xdm2", "xdm6"
    };

    isSmallMap = smallMaps.count(mapname) > 0;
    isBigMap = bigMaps.count(mapname) > 0;
    isMediumMap = !isSmallMap && !isBigMap;
}

static std::vector<std::pair<int, std::string>> benefits = {
    {5, "vampire"},
    {10, "ammo regen"},
    {15, "auto haste"},
    {20, "vampire upgraded"}
};

void ShuffleBenefits() {
    shuffled_benefits.clear();
    for (const auto& benefit : benefits) {
        shuffled_benefits.push_back(benefit.second);
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(shuffled_benefits.begin(), shuffled_benefits.end(), gen);

    // Ensure 'vampire upgraded' is after 'vampire'
    auto vampire_it = std::find(shuffled_benefits.begin(), shuffled_benefits.end(), "vampire");
    auto upgraded_it = std::find(shuffled_benefits.begin(), shuffled_benefits.end(), "vampire upgraded");
    if (vampire_it != shuffled_benefits.end() && upgraded_it != shuffled_benefits.end() && vampire_it > upgraded_it) {
        std::iter_swap(vampire_it, upgraded_it);
    }
}

static std::string SelectRandomBenefit(int wave) {
    std::vector<std::string> possible_benefits;

    if (vampire_level == 0 && wave >= 5) {
        possible_benefits.push_back("vampire");
    }
    else if (vampire_level == 1 && wave >= 10) {
        possible_benefits.push_back("vampire upgraded");
    }

    for (const auto& benefit : shuffled_benefits) {
        if (obtained_benefits.find(benefit) == obtained_benefits.end() && benefit != "vampire" && benefit != "vampire upgraded") {
            possible_benefits.push_back(benefit);
        }
    }

    if (!possible_benefits.empty()) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, possible_benefits.size() - 1);
        return possible_benefits[dis(gen)];
    }

    return "";
}

static void ApplyBenefit(const std::string& benefit) {
    if (benefit == "vampire") {
        vampire_level = 1;
        gi.cvar_set("g_vampire", "1");
        gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nYou're covered in blood!\n\n\nVampire Ability\nENABLED!\n");
        gi.LocBroadcast_Print(PRINT_CHAT, "RECOVERING A PERCENTAGE OF DAMAGE DONE!\n");
    }
    else if (benefit == "ammo regen") {
        gi.cvar_set("g_ammoregen", "1");
        gi.LocBroadcast_Print(PRINT_TYPEWRITER, "AMMO REGEN\n\nENABLED!\n");
        gi.LocBroadcast_Print(PRINT_CHAT, "AMMO REGEN IS NOW ENABLED!\n");
    }
    else if (benefit == "auto haste") {
        gi.cvar_set("g_autohaste", "1");
        gi.LocBroadcast_Print(PRINT_CENTER, "\n\n TIME ACCEL IS RUNNING THROUGH YOUR VEINS \nDAMAGING WHILE ACCEL\nWILL EXTEND TIME!\n");
        gi.LocBroadcast_Print(PRINT_CHAT, "AUTO-HASTE ENABLED !\n");
    }
    else if (benefit == "vampire upgraded") {
        vampire_level = 2;
        gi.cvar_set("g_vampire", "2");
        gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\nVampire Ability\nUPGRADED!\n");
        gi.LocBroadcast_Print(PRINT_CHAT, "RECOVERING HEALTH & ARMOR NOW!\n");
    }

    // Track the obtained benefit
    obtained_benefits.insert(benefit);
}
static void Horde_InitLevel(int32_t lvl) {
    current_wave_number++;
    g_horde_local.level = lvl;
    g_horde_local.monster_spawn_time = level.time;

    isSmallMap = false;
    isMediumMap = false;
    isBigMap = false;

    IsMapSize(level.mapname, isSmallMap, isBigMap, isMediumMap);

    if (g_horde_local.level % 5 == 0) {
        if (shuffled_benefits.empty()) {
            ShuffleBenefits();
        }
        std::string benefit = SelectRandomBenefit(g_horde_local.level);
        if (!benefit.empty()) {
            ApplyBenefit(benefit);
        }
    }

    int custom_monster_count = dm_monsters->integer;
    if (custom_monster_count > 0) {
        g_horde_local.num_to_spawn = custom_monster_count;
    }
    else {
        if (isSmallMap) {
            g_horde_local.num_to_spawn = 6 + (lvl * 1);
            if (g_horde_local.num_to_spawn > MAX_MONSTERS_SMALL_MAP) {
                g_horde_local.num_to_spawn = MAX_MONSTERS_SMALL_MAP;
            }
            if ((g_chaotic->integer == 2 && current_wave_number >= 7) || g_insane->integer) {
                g_horde_local.num_to_spawn += (g_insane->integer ? 5 : 3);
            }
        }
        else if (isBigMap) {
            g_horde_local.num_to_spawn = 27 + (lvl * 1.5);
            if (g_horde_local.num_to_spawn > MAX_MONSTERS_BIG_MAP) {
                g_horde_local.num_to_spawn = MAX_MONSTERS_BIG_MAP;
            }
            if ((g_chaotic->integer && current_wave_number >= 7) || g_insane->integer) {
                g_horde_local.num_to_spawn += (g_insane->integer ? 16 : 8);
            }
        }
        else {
            g_horde_local.num_to_spawn = 8 + (lvl * 1);
            if (g_horde_local.num_to_spawn >= MAX_MONSTERS_MEDIUM_MAP) {
                g_horde_local.num_to_spawn = MAX_MONSTERS_MEDIUM_MAP;
            }
            if ((g_chaotic->integer && current_wave_number >= 7) || g_insane->integer) {
                g_horde_local.num_to_spawn += (g_insane->integer ? 9 : 6);
            }
        }
    }

    // Ajuste adicional según el número de jugadores
    int numActiveHPlayers = 0;
    for (auto Hplayer : active_players()) {
        numActiveHPlayers++;
    }
    if (numActiveHPlayers >= 6 || current_wave_number <= 27) {
        int additionalSpawn = 0;
        if (isSmallMap) {
            additionalSpawn = 3;
        }
        else if (isBigMap) {
            additionalSpawn = 8;
        }
        else {
            additionalSpawn = 5;
        }
        if (current_wave_number > 27) {
            additionalSpawn *= 1.6;
        }
        g_horde_local.num_to_spawn += additionalSpawn;
    }
}

bool G_IsDeathmatch() {
    return deathmatch->integer && g_horde->integer;
}

bool G_IsCooperative() {
    return coop->integer && !g_horde->integer;
}

struct weighted_item_t;

using weight_adjust_func_t = void(*)(const weighted_item_t& item, float& weight);

void adjust_weight_health(const weighted_item_t& item, float& weight);
void adjust_weight_weapon(const weighted_item_t& item, float& weight);
void adjust_weight_ammo(const weighted_item_t& item, float& weight);
void adjust_weight_armor(const weighted_item_t& item, float& weight);
void adjust_weight_powerup(const weighted_item_t& item, float& weight);

constexpr struct weighted_item_t {
    const char* classname;
    int32_t min_level = -1, max_level = -1;
    float weight = 1.0f;
    weight_adjust_func_t adjust_weight = nullptr;
} items[] = {
    { "item_health", -1, 5, 0.20f, adjust_weight_health },
    { "item_health_large", -1, -1, 0.12f, adjust_weight_health },
    { "item_health_mega", -1, -1, 0.06f, adjust_weight_health },
    { "item_adrenaline", -1, -1, 0.17f, adjust_weight_health },
    { "item_armor_shard", -1, -1, 0.09f, adjust_weight_armor },
    { "item_armor_jacket", -1, 5, 0.12f, adjust_weight_armor },
    { "item_armor_combat", 6, -1, 0.06f, adjust_weight_armor },
    { "item_armor_body", 8, -1, 0.05f, adjust_weight_armor },
    { "item_power_screen", 2, 8, 0.03f, adjust_weight_armor },
    { "item_power_shield", 9, -1, 0.07f, adjust_weight_armor },
    { "item_quad", 6, -1, 0.07f, adjust_weight_powerup },
    { "item_double", 5, -1, 0.076f, adjust_weight_powerup },
    { "item_quadfire", 4, -1, 0.056f, adjust_weight_powerup },
    { "item_invulnerability", 4, -1, 0.051f, adjust_weight_powerup },
    { "item_sphere_defender", -1, -1, 0.1f, adjust_weight_powerup },
    { "item_sphere_hunter", 9, -1, 0.06f, adjust_weight_powerup },
    { "item_invisibility", 4, -1, 0.08f, adjust_weight_powerup },
    { "item_doppleganger", 6, -1, 0.09f, adjust_weight_powerup },
    { "weapon_chainfist", -1, 2, 0.12f, adjust_weight_weapon },
    { "weapon_shotgun", -1, -1, 0.27f, adjust_weight_weapon },
    { "weapon_supershotgun", 4, -1, 0.19f, adjust_weight_weapon },
    { "weapon_machinegun", -1, -1, 0.29f, adjust_weight_weapon },
    { "weapon_etf_rifle", 3, -1, 0.19f, adjust_weight_weapon },
    { "weapon_boomer", 4, -1, 0.19f, adjust_weight_weapon },
    { "weapon_chaingun", 5, -1, 0.19f, adjust_weight_weapon },
    { "weapon_grenadelauncher", 6, -1, 0.19f, adjust_weight_weapon },
    { "weapon_proxlauncher", 8, -1, 0.19f, adjust_weight_weapon },
    { "weapon_hyperblaster", 7, -1, 0.19f, adjust_weight_weapon },
    { "weapon_phalanx", 9, -1, 0.19f, adjust_weight_weapon },
    { "weapon_rocketlauncher", 6, -1, 0.19f, adjust_weight_weapon },
    { "weapon_railgun", 9, -1, 0.19f, adjust_weight_weapon },
    { "weapon_plasmabeam", 7, -1, 0.19f, adjust_weight_weapon },
    { "weapon_disintegrator", 14, -1, 0.15f, adjust_weight_weapon },
    { "weapon_bfg", 12, -1, 0.15f, adjust_weight_weapon },
    { "ammo_trap", 5, -1, 0.18f, adjust_weight_ammo },
    { "ammo_shells", -1, -1, 0.25f, adjust_weight_ammo },
    { "ammo_bullets", -1, -1, 0.30f, adjust_weight_ammo },
    { "ammo_flechettes", 5, -1, 0.25f, adjust_weight_ammo },
    { "ammo_grenades", -1, -1, 0.35f, adjust_weight_ammo },
    { "ammo_prox", 7, -1, 0.25f, adjust_weight_ammo },
    { "ammo_tesla", 4, -1, 0.15f, adjust_weight_ammo },
    { "ammo_cells", 5, -1, 0.30f, adjust_weight_ammo },
    { "ammo_magslug", 9, -1, 0.25f, adjust_weight_ammo },
    { "ammo_slugs", 7, -1, 0.25f, adjust_weight_ammo },
    { "ammo_disruptor", 12, -1, 0.24f, adjust_weight_ammo },
    { "ammo_rockets", 7, -1, 0.25f, adjust_weight_ammo },
    { "item_bandolier", 4, -1, 0.32f, adjust_weight_ammo },
    { "item_pack", 11, -1, 0.34f, adjust_weight_ammo },
};

void adjust_weight_health(const weighted_item_t& item, float& weight) {}
void adjust_weight_weapon(const weighted_item_t& item, float& weight) {}
void adjust_weight_ammo(const weighted_item_t& item, float& weight) {}
void adjust_weight_armor(const weighted_item_t& item, float& weight) {}
void adjust_weight_powerup(const weighted_item_t& item, float& weight) {}

constexpr weighted_item_t monsters[] = {
    { "monster_soldier_light", -1, 19, 0.35f },
    { "monster_soldier_ss", -1, 20, 0.45f },
    { "monster_soldier", -1, 4, 0.45f },
    { "monster_soldier_hypergun", 2, 7, 0.55f },
    { "monster_soldier_lasergun", 3, 9, 0.45f },
    { "monster_soldier_ripper", 3, 7, 0.45f },
    { "monster_infantry2", 2, -1, 0.36f },
    { "monster_infantry", 8, -1, 0.36f },
    { "monster_flyer", -1, -1, 0.14f },
    { "monster_hover2", 5, 13, 0.14f },
    { "monster_fixbot", 5, 18, 0.16f },
    { "monster_gekk", 3, 17, 0.12f },
    { "monster_gunner2", 3, 11, 0.35f },
    { "monster_gunner", 8, -1, 0.34f },
    { "monster_medic", 5, 12, 0.1f },
    { "monster_brain", 6, -1, 0.23f },
    { "monster_stalker", 4, 11, 0.19f },
    { "monster_parasite", 4, 14, 0.23f },
    { "monster_tank", 14, -1, 0.3f },
    { "monster_tank2", 5, 13, 0.3f },
    { "monster_guncmdr2", 6, 10, 0.18f },
    { "monster_mutant", 5, -1, 0.55f },
    { "monster_chick", 6, 18, 0.3f },
    { "monster_chick_heat", 10, -1, 0.34f },
    { "monster_berserk", 7, -1, 0.45f },
    { "monster_floater", 9, 16, 0.13f },
    { "monster_hover", 11, -1, 0.18f },
    { "monster_daedalus", 13, -1, 0.08f },
    { "monster_medic_commander", 13, -1, 0.18f },
    { "monster_tank_commander", 11, -1, 0.15f },
    { "monster_spider", 12, -1, 0.24f },
    { "monster_guncmdr", 11, -1, 0.28f },
    { "monster_gladc", 6, -1, 0.3f },
    { "monster_gladiator", 8, -1, 0.3f },
    { "monster_shambler", 10, -1, 0.03f },
    { "monster_floater2", 17, -1, 0.35f },
    { "monster_carrier2", 19, -1, 0.09f },
    { "monster_tank_64", 18, -1, 0.1f },
    { "monster_janitor", 16, -1, 0.18f },
    { "monster_janitor2", 19, -1, 0.12f },
    { "monster_makron", 16, 19, 0.03f },
    { "monster_gladb", 16, -1, 0.55f},
    { "monster_boss2_64", 16, -1, 0.08f },
    { "monster_perrokl", 21, -1, 0.27f },
    { "monster_guncmdrkl", 23, -1, 0.1f },
    { "monster_shamblerkl", 18, -1, 0.14f },
    { "monster_makronkl", 20, -1, 0.05f },
    { "monster_widow1", 23, -1, 0.08f }
};

struct boss_t {
    const char* classname;
    int32_t min_level;
    int32_t max_level;
    float weight;
};

constexpr boss_t BOSS[] = {
    //   {"monster_jorg", -1, -1, 0.7f},
     //  {"monster_makronkl", -1, -1, 0.07f},
       //  {"monster_perrokl", -1, -1, 0.07f},
      //   {"monster_shamblerkl", -1, -1, 0.07f},
       //  {"monster_guncmdrkl", -1, -1, 0.07f},
       //  {"monster_boss2kl", -1, -1, 0.07f},
         {"monster_carrier", -1, -1, 0.07f},
         //  {"monster_widow", -1, -1, 0.07f},
         //  {"monster_widow2", -1, -1, 0.07f},
         //  {"monster_supertankkl", -1, -1, 0.07f},
          // {"monster_supertank", -1, -1, 0.07f},
           {"monster_boss2", -1, -1, 0.07f},
           {"monster_guardian", -1, -1, 0.07f},
};

struct picked_item_t {
    const weighted_item_t* item;
    float weight;
};

gitem_t* G_HordePickItem()
{
    static std::array<picked_item_t, q_countof(items)> picked_items;
    static size_t num_picked_items;

    num_picked_items = 0;

    float total_weight = 0;

    for (auto& item : items)
    {
        if (item.min_level != -1 && g_horde_local.level < item.min_level)
            continue;
        if (item.max_level != -1 && g_horde_local.level > item.max_level)
            continue;

        float weight = item.weight;

        if (item.adjust_weight)
            item.adjust_weight(item, weight);

        if (weight <= 0)
            continue;

        total_weight += weight;
        picked_items[num_picked_items++] = { &item, total_weight };
    }

    if (!total_weight)
        return nullptr;

    float r = frandom() * total_weight;

    for (size_t i = 0; i < num_picked_items; i++)
        if (r < picked_items[i].weight)
            return FindItemByClassname(picked_items[i].item->classname);

    return nullptr;
}

const char* G_HordePickMonster()
{
    static std::array<picked_item_t, q_countof(items)> picked_monsters;
    static size_t num_picked_monsters;

    num_picked_monsters = 0;

    float total_weight = 0;

    for (auto& item : monsters)
    {
        if (item.min_level != -1 && g_horde_local.level < item.min_level)
            continue;
        if (item.max_level != -1 && g_horde_local.level > item.max_level)
            continue;

        float weight = item.weight;

        if (item.adjust_weight)
            item.adjust_weight(item, weight);

        if (weight <= 0)
            continue;

        total_weight += weight;
        picked_monsters[num_picked_monsters++] = { &item, total_weight };
    }

    if (!total_weight)
        return nullptr;

    float r = frandom() * total_weight;

    for (size_t i = 0; i < num_picked_monsters; i++)
        if (r < picked_monsters[i].weight)
            return picked_monsters[i].item->classname;

    return nullptr;
}

const char* G_HordePickBOSS()
{
    float total_weight = 0.0f;
    for (const auto& boss : BOSS) {
        total_weight += boss.weight;
    }

    float r = frandom() * total_weight;
    for (const auto& boss : BOSS) {
        if (r < boss.weight) {
            return boss.classname;
        }
        r -= boss.weight;
    }

    return nullptr; // This should never happen if the weights are set correctly
}

void Horde_PreInit()
{

    wavenext = gi.cvar("wavenext", "0", CVAR_SERVERINFO);
    dm_monsters = gi.cvar("dm_monsters", "0", CVAR_SERVERINFO); // Nuevo cvar
    g_horde = gi.cvar("horde", "0", CVAR_LATCH);

    if (!g_horde->integer)
        return;

    if (!deathmatch->integer || ctf->integer || teamplay->integer || coop->integer) {
        gi.Com_Print("Horde mode must be DM.\n");
        gi.cvar_set("deathmatch", "1");
        gi.cvar_set("ctf", "0");
        gi.cvar_set("teamplay", "0");
        gi.cvar_set("coop", "0");
        gi.cvar_set("timelimit", "20");
        gi.cvar_set("fraglimit", "0");
    }

    if (g_horde->integer)
    {
        gi.Com_Print("Horde mode must be DM.\n");
        gi.cvar_set("deathmatch", "1");
        gi.cvar_set("ctf", "0");
        gi.cvar_set("teamplay", "0");
        gi.cvar_set("coop", "0");
        gi.cvar_set("timelimit", "25");
        gi.cvar_set("fraglimit", "0");
        gi.cvar_set("sv_target_id", "1");
        gi.cvar_set("g_speedstuff", "2.3f");
        gi.cvar_set("sv_eyecam", "1");
        gi.cvar_set("g_dm_instant_items", "1");
        gi.cvar_set("g_disable_player_collision", "1");
        //	gi.cvar_set("g_instagib", "1");
        gi.cvar_set("g_dm_no_self_damage", "1");
        gi.cvar_set("g_allow_techs", "1");
        gi.cvar_set("g_no_nukes", "1");
        gi.cvar_set("g_use_hook", "1");
        gi.cvar_set("set g_hook_wave", "1");
        gi.cvar_set("hook_pullspeed", "1200");
        gi.cvar_set("hook_speed", "3000");
        gi.cvar_set("hook_sky", "1");
        gi.cvar_set("g_allow_grapple 1", "1");
        gi.cvar_set("g_grapple_fly_speed", "3000");
        gi.cvar_set("g_grapple_pull_speed", "1200");
        gi.cvar_set("g_instant_weapon_switch", "1");
        gi.cvar_set("g_dm_no_quad_drop", "0");
        gi.cvar_set("g_dm_no_quadfire_drop", "0");
        gi.cvar_set("g_vampire", "0");
        gi.cvar_set("g_ammoregen", "0");
        gi.cvar_set("g_autohaste", "0");
        gi.cvar_set("g_chaotic", "0");
        gi.cvar_set("g_insane", "0");
        gi.cvar_set("g_hardcoop", "0");
        gi.cvar_set("capturelimit", "0");
        gi.cvar_set("g_dm_spawns", "0");
        gi.cvar_set("g_damage_scale", "1");
        gi.cvar_set("ai_damage_scale", "1");
        gi.cvar_set("g_loadent", "1");
        gi.cvar_set("bot_chat_enable", "0");
        gi.cvar_set("bot_skill", "5");
        gi.cvar_set("g_coop_squad_respawn", "1");
    }
}
void Horde_Init()
{
    for (auto& item : itemlist)
        PrecacheItem(&item);

    for (auto& monster : monsters)
    {
        edict_t* e = G_Spawn();
        e->classname = monster.classname;
        e->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
        ED_CallSpawn(e);
        G_FreeEdict(e);
    }

    g_horde_local.warm_time = level.time + 4_sec;
}

static bool Horde_AllMonstersDead()
{
    for (size_t i = 0; i < globals.max_edicts; i++)
    {
        if (!g_edicts[i].inuse)
            continue;
        else if (g_edicts[i].svflags & SVF_MONSTER)
        {
            if (!g_edicts[i].deadflag && g_edicts[i].health > 0)
                return false;
        }
    }
    return true;
}

static void Horde_CleanBodies()
{
    for (size_t i = 0; i < globals.max_edicts; i++)
    {
        if (!g_edicts[i].inuse)
            continue;
        else if (g_edicts[i].svflags & SVF_DEADMONSTER)
        {
            G_FreeEdict(&g_edicts[i]);
        }
    }
}

inline void VectorCopy(const vec3_t& src, vec3_t& dest) {
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
}

// Declarar funciones externas
extern void SP_target_healthbar(edict_t* self);
extern void use_target_healthbar(edict_t* self, edict_t* other, edict_t* activator);
extern void check_target_healthbar(edict_t* self);

// Declarar la función GetDisplayName
extern std::string GetDisplayName(const std::string& classname);

void AttachHealthBar(edict_t* boss) {
    edict_t* healthbar = G_Spawn();
    if (!healthbar) {
        return;
    }

    healthbar->classname = "target_healthbar";

    // Colocar la barra de salud ligeramente por encima del monstruo
    VectorCopy(boss->s.origin, healthbar->s.origin);
    healthbar->s.origin[2] += 20; // Ajustar este valor según sea necesario

    // Retardo antes de que la barra de salud desaparezca después de la muerte del monstruo
    healthbar->delay = 4.0f;  // Ajustar este valor según sea necesario

    // Inicializar timestamp a cero para asegurar que no desaparezca inmediatamente
    healthbar->timestamp = 0_ms;

    // Vincular la barra de salud al monstruo usando target y targetname
    healthbar->target = boss->targetname;

    // Asegurar que SP_target_healthbar esté definida o reemplazar con función equivalente
    SP_target_healthbar(healthbar);

    // Vincular la barra de salud al monstruo
    healthbar->enemy = boss;

    // Agregar healthbar a las entidades de barra de salud del nivel, asegurando no exceder el límite
    for (size_t i = 0; i < MAX_HEALTH_BARS; i++) {
        if (!level.health_bar_entities[i]) {
            level.health_bar_entities[i] = healthbar;
            break;
        }
    }

    // Asegurar que la barra de salud se actualice regularmente
    healthbar->think = check_target_healthbar;
    healthbar->nextthink = level.time + 0.1_sec; // Ajusta el tiempo de verificación según sea necesario
}


void SpawnBossAutomatically()
{
    if ((Q_strcasecmp(level.mapname, "q2dm1") == 0 && g_horde_local.level % 1 == 0 && g_horde_local.level != 0) ||
        (Q_strcasecmp(level.mapname, "rdm14") == 0 && g_horde_local.level % 5 == 0 && g_horde_local.level != 0) ||
        (Q_strcasecmp(level.mapname, "q2dm2") == 0 && g_horde_local.level % 5 == 0 && g_horde_local.level != 0) ||
        (Q_strcasecmp(level.mapname, "q2dm8") == 0 && g_horde_local.level % 5 == 0 && g_horde_local.level != 0) ||
        (Q_strcasecmp(level.mapname, "xdm2") == 0 && g_horde_local.level % 5 == 0 && g_horde_local.level != 0) ||
        (Q_strcasecmp(level.mapname, "q2ctf5") == 0 && g_horde_local.level % 5 == 0 && g_horde_local.level != 0) ||
        ((!Q_strcasecmp(level.mapname, "q64/dm10") || !Q_strcasecmp(level.mapname, "q64\\dm10")) && g_horde_local.level % 5 == 0 && g_horde_local.level != 0) ||
        ((!Q_strcasecmp(level.mapname, "q64/dm7") || !Q_strcasecmp(level.mapname, "q64\\dm7")) && g_horde_local.level % 5 == 0 && g_horde_local.level != 0) ||
        ((!Q_strcasecmp(level.mapname, "q64/dm2") || !Q_strcasecmp(level.mapname, "q64\\dm2")) && g_horde_local.level % 5 == 0 && g_horde_local.level != 0))
    {

        // Solo necesitas un bucle aquí para generar un jefe
        edict_t* boss = G_Spawn(); // Creas un nuevo edict_t solo si es necesario
        if (!boss)
            return;

        // Aquí selecciona el jefe deseado
        const char* desired_boss = G_HordePickBOSS();
        if (!desired_boss) {
            return; // No se pudo encontrar un jefe válido
        }
        boss->classname = desired_boss;
        // origin for monsters
// Define un mapa que mapea los nombres de los mapas a las coordenadas de origen
        std::unordered_map<std::string, std::array<int, 3>> mapOrigins = {
            {"q2dm1", {1184, 568, 704}},
            {"rdm14", {1248, 664, 896}},
            {"q2dm2", {128, -960, 704}},
            {"q2dm8", {112, 1216, 88}},
            {"q2ctf5", {2432, -960, 168}},
            {"xdm2", {-232, 472, 424}},
            {"q64/dm7", {64, 224, 120}},
            {"q64\\dm7", {64, 224, 120}},
            {"q64/dm10", {-304, 512, -92}},
            {"q64\\dm10", {-304, 512, -92}},
            {"q64/dm2", {1328, -256, 272}},
            {"q64\\dm2", {1328, -256, 272}}
        };

        auto it = mapOrigins.find(level.mapname);
        if (it != mapOrigins.end()) {
            boss->s.origin[0] = it->second[0];
            boss->s.origin[1] = it->second[1];
            boss->s.origin[2] = it->second[2];
        }
        else {
            return;
        }

        boss->s.angles[0] = 0; // are these needed?
        boss->s.angles[1] = 0;
        boss->s.angles[2] = 0;

        gi.LocBroadcast_Print(PRINT_TYPEWRITER, "***** A CHAMPION STROGG HAS SPAWNED *****");
        boss->maxs *= 1.4f;
        boss->mins *= 1.4f;
        boss->s.scale = 1.4f;
        boss->health *= pow(1.2, current_wave_number);
        boss->monsterinfo.power_armor_power *= current_wave_number * 1.45; // Escalar la armadura de energía basada en la oleada actual

        boss->gib_health *= 3;

        vec3_t effectPosition = boss->s.origin;
        effectPosition[0] += (boss->s.origin[0] - effectPosition[0]) * (boss->s.scale - 3);
        effectPosition[1] += (boss->s.origin[1] - effectPosition[1]) * (boss->s.scale - 3);
        effectPosition[2] += (boss->s.origin[2] - effectPosition[2]) * (boss->s.scale - 3);

                gi.WriteByte(svc_temp_entity);
        gi.WriteByte(TE_BOSSTPORT);
        gi.WritePosition(effectPosition);
        gi.multicast(effectPosition, MULTICAST_PHS, false);
        ED_CallSpawn(boss);

        // Adjuntar barra de salud al jefe
        AttachHealthBar(boss);
    }
}

void ResetGame() {
    g_horde_local.state = horde_state_t::warmup;
    next_wave_message_sent = false;
    gi.cvar_set("g_chaotic", "0");
    gi.cvar_set("g_insane", "0");
    gi.cvar_set("g_vampire", "0");
    gi.cvar_set("ai_damage_scale", "1");
    gi.cvar_set("g_damage_scale", "1");
    gi.cvar_set("g_ammoregen", "0");
    gi.cvar_set("g_hardcoop", "0");
    gi.cvar_set("g_autohaste", "0");
}

std::chrono::steady_clock::time_point condition_start_time = std::chrono::steady_clock::time_point::min();
int previous_remainingMonsters = 0;

bool CheckRemainingMonstersCondition(bool isSmallMap, bool isBigMap, bool isMediumMap) {
    int maxMonsters{};
    int timeThreshold{};

    edict_t* ent;
    for (uint32_t player = 1; player <= game.maxclients; player++) {
        ent = &g_edicts[player];
        if (!ent->inuse || !ent->client || !ent->solid)
            continue;

        int numActivePlayers = 0;
        for (auto player : active_players()) {
            numActivePlayers++;
        }

        if (numActivePlayers >= 6) {
            if (isSmallMap) {
                maxMonsters = 7;
                timeThreshold = 4;
            }
            else if (isBigMap) {
                maxMonsters = 25;
                timeThreshold = 18;
            }
            else {
                maxMonsters = 12;
                timeThreshold = 8;
            }
        }
        else {
            if (isSmallMap) {
                maxMonsters = (current_wave_number <= 4) ? 3 : 6;
                timeThreshold = (current_wave_number <= 4) ? 7 : 13;
            }
            else if (isBigMap) {
                maxMonsters = (current_wave_number <= 4) ? 17 : 23;
                timeThreshold = (current_wave_number <= 4) ? 18 : 12;
            }
            else {
                maxMonsters = (current_wave_number <= 4) ? 3 : 6;
                timeThreshold = (current_wave_number <= 4) ? 7 : 15;
            }
            if ((g_chaotic->integer && numActivePlayers <= 5) || (g_insane->integer && numActivePlayers <= 5)) {
                timeThreshold += 4;
            }
        }
    }

    if ((level.total_monsters - level.killed_monsters) <= maxMonsters) {
        if (condition_start_time == std::chrono::steady_clock::time_point::min()) {
            condition_start_time = std::chrono::steady_clock::now();
        }

        auto current_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(current_time - condition_start_time);

        if (duration.count() >= timeThreshold) {
            condition_start_time = std::chrono::steady_clock::time_point::min();
            return true;
        }
    }
    else {
        if (remainingMonsters > previous_remainingMonsters) {
            condition_start_time = std::chrono::steady_clock::time_point::min();
        }
    }

    previous_remainingMonsters = remainingMonsters;
    return false;
}

void Horde_RunFrame() {
    switch (g_horde_local.state) {
    case horde_state_t::warmup:
        if (g_horde_local.warm_time < level.time + 0.4_sec) {
            remainingMonsters = 0;
            g_horde_local.state = horde_state_t::spawning;
            Horde_InitLevel(1);
            current_wave_number = 2;

            std::srand(static_cast<unsigned int>(std::time(nullptr)));
            float r = frandom();

            if (r < 0.333f) {
                gi.sound(world, CHAN_VOICE, gi.soundindex("misc/r_tele3.wav"), 1, ATTN_NONE, 0);
            }
            else if (r < 0.666f) {
                gi.sound(world, CHAN_VOICE, gi.soundindex("world/klaxon2.wav"), 1, ATTN_NONE, 0);
            }
            else {
                gi.sound(world, CHAN_VOICE, gi.soundindex("misc/tele_up.wav"), 1, ATTN_NONE, 0);
            }

            if (brandom()) {
                gi.LocBroadcast_Print(PRINT_CENTER, "\n???\n");
            }
            else {
                gi.LocBroadcast_Print(PRINT_CENTER, "\n?????\n");
            }

            if (!g_chaotic->integer || (g_chaotic->integer && r > 0.666f)) {
                gi.sound(world, CHAN_VOICE, gi.soundindex("misc/r_tele3.wav"), 1, ATTN_NONE, 0);
            }
            else if (r > 0.333f) {
                gi.sound(world, CHAN_VOICE, gi.soundindex("world/incoming.wav"), 1, ATTN_NONE, 0);
            }
            else {
                gi.sound(world, CHAN_VOICE, gi.soundindex("misc/tele_up.wav"), 1, ATTN_NONE, 0);
            }
        }
        break;

    case horde_state_t::spawning:
        if (!next_wave_message_sent) {
            next_wave_message_sent = true;
        }

        if (g_horde_local.monster_spawn_time <= level.time) {
            if (g_horde_local.num_to_spawn == BOSS_TO_SPAWN) {
                SpawnBossAutomatically();
            }

            edict_t* e = G_Spawn();
            e->classname = G_HordePickMonster();
            select_spawn_result_t result = SelectDeathmatchSpawnPoint(false, true, false);

            if (result.any_valid) {
                e->s.origin = result.spot->s.origin;
                e->s.angles = result.spot->s.angles;
                e->item = G_HordePickItem();
                ED_CallSpawn(e);
                remainingMonsters = level.total_monsters - level.killed_monsters;

                vec3_t spawngrow_pos = e->s.origin;
                float start_size = (sqrt(spawngrow_pos[0] * spawngrow_pos[0] + spawngrow_pos[1] * spawngrow_pos[1] + spawngrow_pos[1] * spawngrow_pos[1])) * 0.025f;
                float end_size = start_size;
                SpawnGrow_Spawn(spawngrow_pos, start_size, end_size);

                if (!Q_strcasecmp(level.mapname, "mgu4trial")) {
                    e->s.renderfx = RF_GLOW;
                    e->s.effects = EF_GRENADE_LIGHT;
                }

                e->health *= 1 + (0.02 * current_wave_number);
                e->monsterinfo.power_armor_power *= current_wave_number * 1.115;
                e->gib_health = -100;

                std::srand(static_cast<unsigned int>(std::time(nullptr)));
                int randomIndex = std::rand() % game.maxclients + 1;
                e->enemy = &g_edicts[randomIndex];

                HuntTarget(e);

                if ((g_chaotic->integer == 2 && current_wave_number >= 7) || (g_insane->integer && current_wave_number < 20)) {
                    g_horde_local.monster_spawn_time = level.time + random_time(0.7_sec, 1.2_sec);
                }
                else if (!g_insane->integer || !g_chaotic->integer) {
                    g_horde_local.monster_spawn_time = level.time + random_time(0.7_sec, 0.9_sec);
                }
                else if (g_chaotic->integer == 1 || (g_insane->integer && current_wave_number >= 21)) {
                    g_horde_local.monster_spawn_time = level.time + random_time(0.5_sec, 0.7_sec);
                }

                --g_horde_local.num_to_spawn;

                if (!g_horde_local.num_to_spawn) {
                    std::ostringstream message_stream;
                    message_stream << "New Wave Is Here.\nWave Level: " << g_horde_local.level << "\n";
                    gi.LocBroadcast_Print(PRINT_TYPEWRITER, message_stream.str().c_str());
                    g_horde_local.state = horde_state_t::cleanup;
                    g_horde_local.monster_spawn_time = level.time + 1_sec;
                }
            }
            else {
                remainingMonsters = level.total_monsters - level.killed_monsters;
                g_horde_local.monster_spawn_time = level.time + 0.5_sec;
            }
        }
        break;

    case horde_state_t::cleanup:
        if (CheckRemainingMonstersCondition(isSmallMap, isBigMap, isMediumMap)) {
            if (current_wave_number >= 15) {
                gi.cvar_set("g_insane", "1");
                gi.cvar_set("g_chaotic", "0");
                g_horde_local.state = horde_state_t::rest;
                break;
            }
            else if (!isSmallMap && current_wave_number <= 14) {
                gi.cvar_set("g_chaotic", "1");
            }
            else if (isSmallMap && current_wave_number <= 14) {
                gi.cvar_set("g_chaotic", "2");
            }
            g_horde_local.state = horde_state_t::rest;
            break;
        }

        if (g_horde_local.monster_spawn_time < level.time) {
            if (Horde_AllMonstersDead()) {
                g_horde_local.warm_time = level.time + random_time(2.2_sec, 5_sec);
                g_horde_local.state = horde_state_t::rest;
                remainingMonsters = 0;

                if (g_chaotic->integer || g_insane->integer) {
                    gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nHarder Wave Controlled, GG\n");
                    gi.sound(world, CHAN_VOICE, gi.soundindex("world/x_light.wav"), 1, ATTN_NONE, 0);
                    gi.cvar_set("g_chaotic", "0");
                    gi.cvar_set("g_insane", "0");
                }
                else {
                    gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\n\n\nWave Defeated, GG !\n");
                }
            }
            else {
                remainingMonsters = level.total_monsters + 1 - level.killed_monsters;
                g_horde_local.monster_spawn_time = level.time + 3_sec;
            }
        }
        break;

    case horde_state_t::rest:
        if (g_horde_local.warm_time < level.time) {
            if (g_chaotic->integer || g_insane->integer) {
                if (!g_insane->integer) {
                    gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\n\n STROGGS STARTING TO PUSH !\n\n\n ");
                }
                else if (g_insane->integer) {
                    gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\n**************\n\n\n--STRONGER WAVE COMING--\n\n\n STROGGS STARTING TO PUSH !\n\n\n **************");
                }

                float r = frandom();
                if (r < 0.167f) {
                    gi.sound(world, CHAN_VOICE, gi.soundindex("nav_editor/action_fail.wav"), 1, ATTN_NONE, 0);
                }
                else if (r < 0.333f) {
                    gi.sound(world, CHAN_VOICE, gi.soundindex("makron/roar1.wav"), 1, ATTN_NONE, 0);
                }
                else if (r < 0.5f) {
                    gi.sound(world, CHAN_VOICE, gi.soundindex("zortemp/ack.wav"), 1, ATTN_NONE, 0);
                }
                else if (r < 0.667f) {
                    gi.sound(world, CHAN_VOICE, gi.soundindex("misc/spawn1.wav"), 1, ATTN_NONE, 0);
                }
                else if (r < 0.833f) {
                    gi.sound(world, CHAN_VOICE, gi.soundindex("makron/voice3.wav"), 1, ATTN_NONE, 0);
                }
                else {
                    gi.sound(world, CHAN_VOICE, gi.soundindex("world/v_fac3.wav"), 1, ATTN_NONE, 0);
                }
            }
            else {
                gi.LocBroadcast_Print(PRINT_CENTER, "Loading Next Wave");
                gi.sound(world, CHAN_VOICE, gi.soundindex("world/lite_on1.wav"), 1, ATTN_NONE, 0);
            }
            g_horde_local.state = horde_state_t::spawning;
            Horde_InitLevel(g_horde_local.level + 1);
            Horde_CleanBodies();
        }
        break;
}
}

void HandleResetEvent() {
    ResetGame();
}
