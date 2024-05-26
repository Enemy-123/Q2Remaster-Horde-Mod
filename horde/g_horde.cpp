// Includes y definiciones relevantes
#include "../g_local.h"
#include "../shared.h"
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <chrono>
#include <random>
#include <algorithm>
#include <deque>

constexpr int MAX_MONSTERS_BIG_MAP = 44;
constexpr int MAX_MONSTERS_MEDIUM_MAP = 18;
constexpr int MAX_MONSTERS_SMALL_MAP = 15;

int remainingMonsters = 0; // needed, else will cause error
int current_wave_number = 1;
int last_wave_number = 0;
constexpr int BOSS_TO_SPAWN = 1;
cvar_t* g_horde;

enum class horde_state_t {
    warmup,
    spawning,
    cleanup,
    rest
};

struct HordeState {
    gtime_t         warm_time = 5_sec;
    horde_state_t   state = horde_state_t::warmup;
    gtime_t         monster_spawn_time;
    int32_t         num_to_spawn = 0;
    int32_t         level = 0;
} g_horde_local;

bool next_wave_message_sent = false;
int vampire_level = 0;
std::vector<std::string> shuffled_benefits;
std::unordered_set<std::string> obtained_benefits;

const std::unordered_set<std::string> smallMaps = {
    "q2dm3", "q2dm7", "q2dm2", "q2ctf4", "q64/dm10", "q64\\dm10",
    "q64/dm9", "q64\\dm9", "q64/dm7", "q64\\dm7", "q64/dm2",
    "q64\\dm2", "q64/dm1", "fact3", "q2ctf4",
    "mgu3m4", "mgu4trial", "mgu6trial", "ec/base_ec", "mgdm1"
};

const std::unordered_set<std::string> bigMaps = {
    "q2ctf5", "old/kmdm3", "xdm2", "xdm6"
};

struct MapSize {
    bool isSmallMap = false;
    bool isMediumMap = false;
    bool isBigMap = false;
};

MapSize GetMapSize(const std::string& mapname) {
    MapSize mapSize;
    mapSize.isSmallMap = smallMaps.count(mapname) > 0;
    mapSize.isBigMap = bigMaps.count(mapname) > 0;
    mapSize.isMediumMap = !mapSize.isSmallMap && !mapSize.isBigMap;
    return mapSize;
}

const std::vector<std::pair<int, std::string>> benefits = {
    {5, "vampire"},
    {10, "ammo regen"},
    {15, "auto haste"},
    {20, "vampire upgraded"}
};

#include <random>

// Declaración global
static std::random_device rd;
static std::mt19937 gen(rd());

void ShuffleBenefits() {
    shuffled_benefits.clear();
    for (const auto& benefit : benefits) {
        shuffled_benefits.push_back(benefit.second);
    }

    std::shuffle(shuffled_benefits.begin(), shuffled_benefits.end(), gen);

    // Asegurar que 'vampire' viene antes que 'vampire upgraded'
    auto vampire_it = std::find(shuffled_benefits.begin(), shuffled_benefits.end(), "vampire");
    auto upgraded_it = std::find(shuffled_benefits.begin(), shuffled_benefits.end(), "vampire upgraded");
    if (vampire_it != shuffled_benefits.end() && upgraded_it != shuffled_benefits.end() && vampire_it > upgraded_it) {
        std::iter_swap(vampire_it, upgraded_it);
    }
}


std::string SelectRandomBenefit(int wave) {
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
        std::uniform_int_distribution<> dis(0, possible_benefits.size() - 1);
        return possible_benefits[dis(gen)];
    }

    return "";
}

void ApplyBenefit(const std::string& benefit) {
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
        gi.LocBroadcast_Print(PRINT_CENTER, "\n\n TIME ACCEL IS RUNNING THROUGH YOUR VEINS \nFRAGGING WHILE ACCEL\nWILL EXTEND TIME!\n");
        gi.LocBroadcast_Print(PRINT_CHAT, "AUTO-HASTE ENABLED !\n");
    }
    else if (benefit == "vampire upgraded") {
        vampire_level = 2;
        gi.cvar_set("g_vampire", "2");
        gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\nVampire Ability\nUPGRADED!\n");
        gi.LocBroadcast_Print(PRINT_CHAT, "RECOVERING HEALTH & ARMOR NOW!\n");
    }

    obtained_benefits.insert(benefit);
}

void CheckAndApplyBenefit(int wave) {
    if (wave % 5 == 0) {
        if (shuffled_benefits.empty()) {
            ShuffleBenefits();
        }
        std::string benefit = SelectRandomBenefit(wave);
        if (!benefit.empty()) {
            ApplyBenefit(benefit);
        }
    }
}

void Horde_InitLevel(int32_t lvl) {
    current_wave_number++;
    last_wave_number++;
    g_horde_local.level = lvl;
    g_horde_local.monster_spawn_time = level.time;

    auto mapSize = GetMapSize(level.mapname);

    CheckAndApplyBenefit(g_horde_local.level);

    if (g_horde_local.level == 18) {
        gi.cvar_set("g_damage_scale", "1.7");
    }
    if (g_horde_local.level == 35) {
        gi.cvar_set("g_damage_scale", "3");
    }

    int custom_monster_count = dm_monsters->integer;
    if (custom_monster_count > 0) {
        g_horde_local.num_to_spawn = custom_monster_count;
    }
    else {
        if (mapSize.isSmallMap) {
            g_horde_local.num_to_spawn = 9 + (lvl * 1);
            if (g_horde_local.num_to_spawn > MAX_MONSTERS_SMALL_MAP) {
                g_horde_local.num_to_spawn = MAX_MONSTERS_SMALL_MAP;
            }
            if ((g_chaotic->integer == 2 && current_wave_number >= 7) || g_insane->integer) {
                g_horde_local.num_to_spawn += (g_insane->integer ? 6 : 5);
            }
        }
        else if (mapSize.isBigMap) {
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

    int numActiveHPlayers = 0;
    for (auto Hplayer : active_players()) {
        numActiveHPlayers++;
    }
    if (numActiveHPlayers >= 6 || g_insane->integer == 2) {
        int additionalSpawn = 0;
        if (mapSize.isSmallMap) {
            additionalSpawn = 4;
        }
        else if (mapSize.isBigMap) {
            additionalSpawn = 9;
        }
        else {
            additionalSpawn = 7;
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

void adjust_weight_health(const weighted_item_t& item, float& weight) {}
void adjust_weight_weapon(const weighted_item_t& item, float& weight) {}
void adjust_weight_ammo(const weighted_item_t& item, float& weight) {}
void adjust_weight_armor(const weighted_item_t& item, float& weight) {}
void adjust_weight_powerup(const weighted_item_t& item, float& weight) {}

constexpr struct weighted_item_t {
    const char* classname;
    int32_t min_level = -1, max_level = -1;
    float weight = 1.0f;
    weight_adjust_func_t adjust_weight = nullptr;
} items[] = {
    { "item_health", -1, 5, 0.20f, adjust_weight_health },
    { "item_health_large", -1, -1, 0.12f, adjust_weight_health },
    { "item_health_mega", -1, -1, 0.06f, adjust_weight_health },
    { "item_adrenaline", -1, -1, 0.08f, adjust_weight_health },

    { "item_armor_shard", -1, -1, 0.09f, adjust_weight_armor },
    { "item_armor_jacket", -1, 5, 0.12f, adjust_weight_armor },
    { "item_armor_combat", 6, -1, 0.06f, adjust_weight_armor },
    { "item_armor_body", 8, -1, 0.05f, adjust_weight_armor },
    { "item_power_screen", 2, 8, 0.03f, adjust_weight_armor },
    { "item_power_shield", 9, -1, 0.07f, adjust_weight_armor },

    { "item_quad", 6, -1, 0.054f, adjust_weight_powerup },
    { "item_double", 5, -1, 0.07f, adjust_weight_powerup },
    { "item_quadfire", 3, -1, 0.06f, adjust_weight_powerup },
    { "item_invulnerability", 4, -1, 0.051f, adjust_weight_powerup },
    { "item_sphere_defender", -1, -1, 0.06f, adjust_weight_powerup },
    { "item_sphere_hunter", 9, -1, 0.06f, adjust_weight_powerup },
    { "item_invisibility", 4, -1, 0.08f, adjust_weight_powerup },
    { "item_doppleganger", 6, -1, 0.05f, adjust_weight_powerup },

    { "weapon_chainfist", -1, 3, 0.12f, adjust_weight_weapon },
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
    { "monster_hover2", 5, 16, 0.16f },
    { "monster_fixbot", 5, 21, 0.13f },
    { "monster_gekk", 3, 17, 0.12f },
    { "monster_gunner2", 3, -1, 0.35f },
    { "monster_gunner", 8, -1, 0.34f },
    { "monster_medic", 5, -1, 0.1f },
    { "monster_brain", 6, -1, 0.23f },
    { "monster_stalker", 4, 11, 0.19f },
    { "monster_parasite", 4, 14, 0.23f },
    { "monster_tank", 12, -1, 0.3f },
    { "monster_tank2", 5, 13, 0.3f },
    { "monster_guncmdr2", 6, 10, 0.18f },
    { "monster_mutant", 5, -1, 0.55f },
    { "monster_chick", 6, 18, 0.3f },
    { "monster_chick_heat", 10, -1, 0.34f },
    { "monster_berserk", 7, -1, 0.45f },
    { "monster_floater", 9, -1, 0.13f },
    { "monster_hover", 11, -1, 0.18f },
    { "monster_daedalus", 13, -1, 0.08f },
    { "monster_daedalus2", 18, -1, 0.08f },
    { "monster_medic_commander", 13, -1, 0.06f },
    { "monster_tank_commander", 11, -1, 0.15f },
    { "monster_spider", 12, -1, 0.24f },
    { "monster_guncmdr", 11, -1, 0.28f },
    { "monster_gladc", 6, -1, 0.3f },
    { "monster_gladiator", 8, -1, 0.3f },
    { "monster_shambler", 10, -1, 0.03f },
    { "monster_floater2", 17, -1, 0.35f },
    { "monster_carrier2", 19, -1, 0.09f },
    { "monster_tank_64", 22, -1, 0.14f },
    { "monster_janitor", 16, -1, 0.14f },
    { "monster_janitor2", 19, -1, 0.12f },
    { "monster_makron", 16, 19, 0.03f },
    { "monster_gladb", 16, -1, 0.45f},
    { "monster_boss2_64", 16, -1, 0.08f },
    { "monster_perrokl", 21, -1, 0.33f },
    { "monster_widow1", 23, -1, 0.08f }
};

struct boss_t {
    const char* classname;
    int32_t min_level;
    int32_t max_level;
    float weight;
};

constexpr boss_t BOSS_SMALL[] = {
    {"monster_carrier2", -1, -1, 0.05f},
    {"monster_tank_64", -1, -1, 0.05f},
    {"monster_shamblerkl", -1, -1, 0.05f},
    {"monster_guncmdrkl", -1, -1, 0.05f},
    {"monster_makronkl", -1, -1, 0.05f},
};

constexpr boss_t BOSS_MEDIUM[] = {
    {"monster_carrier", -1, -1, 0.1f},
    {"monster_boss2", -1, -1, 0.1f},
    {"monster_tank_64", -1, -1, 0.1f},
    {"monster_guardian", -1, -1, 0.1f},
    {"monster_shamblerkl", -1, -1, 0.1f},
    {"monster_makronkl", 17, -1, 0.1f},
};

constexpr boss_t BOSS_LARGE[] = {
    {"monster_carrier", -1, -1, 0.15f},
    {"monster_boss5", -1, -1, 0.15f},
    {"monster_boss2", -1, -1, 0.15f},
    {"monster_tank_64", -1, -1, 0.15f},
    {"monster_guardian", -1, -1, 0.15f},
    {"monster_shamblerkl", -1, -1, 0.15f},
    {"monster_boss5", -1, -1, 0.1f},
    {"monster_makronkl", 16, -1, 0.15f},
    {"monster_jorg", 14, -1, 0.15f},
};


const boss_t* GetBossList(const MapSize& mapSize, const std::string& mapname) {
    if (mapSize.isSmallMap) return BOSS_SMALL;
    if (mapSize.isMediumMap) {
        if (mapname == "q64/dm3" ||
            mapname == "q64\\dm3" ||
            mapname == "mgu6m3") {

            // Crear una copia de BOSS_MEDIUM sin "monster_guardian"
            static std::vector<boss_t> filteredBossList;
            if (filteredBossList.empty()) {
                for (const auto& boss : BOSS_MEDIUM) {
                    if (std::strcmp(boss.classname, "monster_guardian") != 0) {
                        filteredBossList.push_back(boss);
                    }
                }
            }
            return filteredBossList.data();
        }
        return BOSS_MEDIUM;
    }


    if (mapSize.isBigMap) return BOSS_LARGE;
    return nullptr;
}

#include <set>
#include <vector>
#include <algorithm>
#include <random>
constexpr int MAX_RECENT_BOSSES = 3;
std::set<const char*> recent_bosses;  // Conjunto de jefes recientes para evitar selecciones repetidas rápidamente.

const char* G_HordePickBOSS(const MapSize& mapSize, const std::string& mapname) {
    const boss_t* boss_list = GetBossList(mapSize, mapname);
    if (!boss_list) return nullptr;

    std::vector<const boss_t*> eligible_bosses;
    for (int i = 0; i < 5; ++i) {
        if (recent_bosses.find(boss_list[i].classname) == recent_bosses.end()) {
            eligible_bosses.push_back(&boss_list[i]);
        }
    }

    // Si todos los jefes están marcados como recientes, permite la selección de cualquier jefe.
    if (eligible_bosses.empty()) {
        for (int i = 0; i < 5; ++i) {
            eligible_bosses.push_back(&boss_list[i]);
        }
    }

    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    float total_weight = 0.0f;
    for (const auto& boss : eligible_bosses) {
        total_weight += boss->weight;
    }

    float random_point = distribution(gen) * total_weight;
    for (const auto& boss : eligible_bosses) {
        random_point -= boss->weight;
        if (random_point <= 0) {
            recent_bosses.insert(boss->classname);
            if (recent_bosses.size() > MAX_RECENT_BOSSES) {
                recent_bosses.erase(recent_bosses.begin());
            }
            return boss->classname;
        }
    }

    return nullptr; // En teoría nunca debería llegar aquí.
}

struct picked_item_t {
    const weighted_item_t* item;
    float weight;
};



const char* flying_monster_classnames[] = {
    "monster_boss2_64",
    "monster_carrier2",
    "monster_flyer",
    "monster_fixbot",
    "monster_hover",
    "monster_hover2",
    "monster_daedalus",
    "monster_daedalus2"
};
int num_flying_monsters = sizeof(flying_monster_classnames) / sizeof(flying_monster_classnames[0]);

const char* SelectFlyingMonster() {
    int random_index = rand() % num_flying_monsters;
    return flying_monster_classnames[random_index];
}




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

int countFlyingSpawns() {
    int count = 0;
    for (int i = 0; i < globals.num_edicts; i++) {
        if (g_edicts[i].inuse && strcmp(g_edicts[i].classname, "info_player_deathmatch") == 0 && g_edicts[i].style == 1) {
            count++;
        }
    }
    return count;
}

float adjustFlyingSpawnProbability(int flyingSpawns) {
    if (flyingSpawns > 0) {
        // Si hay spawns dedicados solo para monstruos voladores, reducir la probabilidad de seleccionar voladores en otros puntos.
        return 0.75f; // Reduce la probabilidad a la mitad, ajustable según tus necesidades.
    }
    return 1.0f; // Mantén la probabilidad normal si no hay spawns dedicados.
}

const char* G_HordePickMonster(edict_t* spawn_point) {
    int flyingSpawns = countFlyingSpawns();  // Contar los spawns de tipo `style = 1`
    float adjustmentFactor = adjustFlyingSpawnProbability(flyingSpawns);

    std::vector<picked_item_t> picked_monsters;
    float total_weight = 0.0f;
    bool spawn_flying = (spawn_point->style == 1); // Check if the style is 1

    // Check wave number before allowing flying monsters
    int wave_to_allow_flying = 3; // Set this to the wave number you want to start allowing flying spawns
    if (g_horde_local.level < wave_to_allow_flying && spawn_flying) {
        return nullptr; // Disable flying spawns if the current wave number is less than the threshold
    }

    for (auto& item : monsters) {
        float weight = item.weight;
        if (spawn_flying) {
            if (std::find(std::begin(flying_monster_classnames), std::end(flying_monster_classnames), item.classname) != std::end(flying_monster_classnames)) {
                weight *= 1.0f; // Mantiene el peso de los monstruos voladores
            }
            else {
                weight = 0.0f; // Establece el peso de los monstruos no voladores a 0
            }
        }
        else {
            // Ajustar el peso de los monstruos voladores en spawns normales
            if (std::find(std::begin(flying_monster_classnames), std::end(flying_monster_classnames), item.classname) != std::end(flying_monster_classnames)) {
                weight *= adjustmentFactor;
            }
        }

        if (item.min_level != -1 && g_horde_local.level < item.min_level) continue;
        if (item.max_level != -1 && g_horde_local.level > item.max_level) continue;

        if (weight > 0.0f) {
            picked_monsters.push_back({ &item, total_weight += weight });
        }
    }

    if (total_weight == 0.0f) return nullptr;

    float r = frandom() * total_weight;
    for (const auto& monster : picked_monsters) {
        if (r < monster.weight) {
            return monster.item->classname;
        }
    }

    return nullptr;
}

void Horde_PreInit() {
    wavenext = gi.cvar("wavenext", "0", CVAR_SERVERINFO);
    dm_monsters = gi.cvar("dm_monsters", "0", CVAR_SERVERINFO);
    g_horde = gi.cvar("horde", "0", CVAR_LATCH);

    if (!g_horde->integer) return;

    if (!deathmatch->integer || ctf->integer || teamplay->integer || coop->integer) {
        gi.Com_Print("Horde mode must be DM.\n");
        gi.cvar_set("deathmatch", "1");
        gi.cvar_set("ctf", "0");
        gi.cvar_set("teamplay", "0");
        gi.cvar_set("coop", "0");
        gi.cvar_set("timelimit", "20");
        gi.cvar_set("fraglimit", "0");
    }

    if (g_horde->integer) {
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
        gi.cvar_set("g_dm_no_self_damage", "1");
        gi.cvar_set("g_allow_techs", "1");
        gi.cvar_set("g_no_nukes", "1");
        gi.cvar_set("g_use_hook", "1");
        gi.cvar_set("set g_hook_wave", "1");
        gi.cvar_set("hook_pullspeed", "1200");
        gi.cvar_set("hook_speed", "3000");
        gi.cvar_set("hook_sky", "1");
        gi.cvar_set("g_allow_grapple", "1");
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
        gi.cvar_set("bot_minClients", "5");
    }
}

void Horde_Init() {
    for (auto& item : itemlist) PrecacheItem(&item);

    for (const auto& monster : monsters) {
        edict_t* e = G_Spawn();
        e->classname = monster.classname;
        e->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
        ED_CallSpawn(e);
        G_FreeEdict(e);
    }

    g_horde_local.warm_time = level.time + 4_sec;
}


inline void VectorCopy(const vec3_t& src, vec3_t& dest) {
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
}

void BossDeathHandler(edict_t* boss) {
    if (boss->spawnflags.has(SPAWNFLAG_IS_BOSS) && !boss->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
        boss->spawnflags |= SPAWNFLAG_BOSS_DEATH_HANDLED; // Marcar como manejado

        std::vector<const char*> itemsToDrop = {
            "item_adrenaline",
            "item_health_large",
            "item_health_mega",
            "item_armor_body"
        };

        // Soltar ítem especial (quad o quadfire)
        edict_t* specialItem;
        if (rand() % 2 == 0) {
            specialItem = Drop_Item(boss, FindItemByClassname("item_quad"));
        }
        else {
            specialItem = Drop_Item(boss, FindItemByClassname("item_quadfire"));
        }

        // Establecer posición del ítem especial y hacer que salga volando
        VectorCopy(boss->s.origin, specialItem->s.origin);
        vec3_t velocity;
        velocity[0] = (rand() % 400) - 200;
        velocity[1] = (rand() % 400) - 200;
        velocity[2] = 300 + (rand() % 200);
        VectorCopy(velocity, specialItem->velocity);

        // Soltar los demás ítems y hacer que cada uno salga volando en diferentes direcciones
        for (const auto& itemClassname : itemsToDrop) {
            edict_t* droppedItem = Drop_Item(boss, FindItemByClassname(itemClassname));

            // Establecer posición del ítem
            VectorCopy(boss->s.origin, droppedItem->s.origin);

            // Aplicar velocidad al ítem
            velocity[0] = (rand() % 400) - 200;
            velocity[1] = (rand() % 400) - 200;
            velocity[2] = 700 + (rand() % 200);
            VectorCopy(velocity, droppedItem->velocity);

            // Asegurar que el ítem tenga una velocidad instantánea
            droppedItem->movetype = MOVETYPE_TOSS;
            droppedItem->s.effects |= EF_BLASTER;
        }

        // Asegurar que el ítem especial tenga una velocidad instantánea
        specialItem->movetype = MOVETYPE_TOSS;
        specialItem->s.effects |= EF_BLASTER;

        // Marcar al boss como no atacable para evitar doble manejo
        boss->takedamage = false;
    }
}

void boss_die(edict_t* boss) {
    if (boss->spawnflags.has(SPAWNFLAG_IS_BOSS) && boss->deadflag == true && !boss->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
        BossDeathHandler(boss);
    }
}

static bool Horde_AllMonstersDead() {
    for (size_t i = 0; i < globals.max_edicts; i++) {
        if (!g_edicts[i].inuse)
            continue;
        else if (g_edicts[i].svflags & SVF_MONSTER) {
            if (!g_edicts[i].deadflag && g_edicts[i].health > 0)
                return false;
            if (g_edicts[i].spawnflags.has(SPAWNFLAG_IS_BOSS) && g_edicts[i].health <= 0) {
                if (!g_edicts[i].spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
                    boss_die(&g_edicts[i]);
                    g_edicts[i].spawnflags |= SPAWNFLAG_BOSS_DEATH_HANDLED; // Marcar como manejado
                }
            }
        }
    }
    return true;
}

static void Horde_CleanBodies() {
    for (size_t i = 0; i < globals.max_edicts; i++) {
        if (!g_edicts[i].inuse)
            continue;
        else if (g_edicts[i].svflags & SVF_DEADMONSTER) {
            if (g_edicts[i].s.effects & EF_GIB) {
                // No limpiar cuerpos gibbeados
                continue;
            }
            if (g_edicts[i].spawnflags.has(SPAWNFLAG_IS_BOSS) && !g_edicts[i].spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
                boss_die(&g_edicts[i]);
            }
            G_FreeEdict(&g_edicts[i]);
        }
    }
}


void AttachHealthBar(edict_t* boss) {
    edict_t* healthbar = G_Spawn();
    if (!healthbar) return;

    healthbar->classname = "target_healthbar";
    VectorCopy(boss->s.origin, healthbar->s.origin);
    healthbar->s.origin[2] += 20;
    healthbar->delay = 4.0f;
    healthbar->timestamp = 0_ms;
    healthbar->target = boss->targetname;
    SP_target_healthbar(healthbar);
    healthbar->enemy = boss;

    for (size_t i = 0; i < MAX_HEALTH_BARS; ++i) {
        if (!level.health_bar_entities[i]) {
            level.health_bar_entities[i] = healthbar;
            break;
        }
    }

    healthbar->think = check_target_healthbar;
    healthbar->nextthink = level.time + 20_sec;
}

// Define los orígenes del mapa como un unordered_map
const std::unordered_map<std::string, std::array<int, 3>> mapOrigins = {
    {"q2dm1", {1184, 568, 704}},
    {"rdm14", {1248, 664, 896}},
    {"ec/base_ec", {-112, 704, 128}},
    {"q2dm2", {128, -960, 704}},
    {"q2dm3", {192, -136, 72}},
    {"q2dm5", {48, 952, 376}},
    {"q2dm8", {112, 1216, 88}},
    {"ndctf0", {-608, -304, 184}},
    {"q2ctf4", {-2390, 1112, 218}},
    {"q2ctf5", {2432, -960, 168}},
    {"old/kmdm3", {-480, -572, 144}},
    {"xdm2", {-232, 472, 424}},
    {"xdm6", {-1088, -128, 528}},
    {"mgu3m4", {3312, 3344, 864}},
    {"mgdm1", {176, 64, 288}},
    {"fact3", {0, -64, 192}},
    {"mgu6m3", {0, 592, 1600}},
    {"q64/dm7", {64, 224, 120}},
    {"q64\\dm7", {64, 224, 120}},
    {"q64\\dm2", {840, 80, 96}},
    {"q64/dm7", {840, 80, 960}},
    {"q64/dm10", {-304, 512, -92}},
    {"q64\\dm10", {-304, 512, -92}},
    {"q64/dm3", {488, 392, 64}},
    {"q64\\dm3", {488, 392, 64}}
};
void SpawnBossAutomatically() {
    auto mapSize = GetMapSize(level.mapname);
    if (g_horde_local.level % 5 == 0 && g_horde_local.level != 1) { // Evita que el jefe aparezca en la primera ola
        const auto it = mapOrigins.find(level.mapname);
        if (it != mapOrigins.end()) {
            edict_t* boss = G_Spawn();
            if (!boss) return;

            const char* desired_boss = G_HordePickBOSS(mapSize, level.mapname);
            if (!desired_boss) return;
            boss->classname = desired_boss;

            boss->s.origin[0] = it->second[0];
            boss->s.origin[1] = it->second[1];
            boss->s.origin[2] = it->second[2];

            gi.LocBroadcast_Print(PRINT_TYPEWRITER, "***** A CHAMPION STROGG HAS SPAWNED *****");

            int random_flag = 1 << (std::rand() % 6); // Include all defined flags
            boss->monsterinfo.bonus_flags |= random_flag;
            boss->spawnflags |= SPAWNFLAG_IS_BOSS; // Marcar como jefe

            // Apply bonus flags and ensure health multiplier is applied correctly
            ApplyMonsterBonusFlags(boss);

            boss->monsterinfo.attack_state = AS_BLIND;
            boss->accel *= 2;
            boss->maxs *= boss->s.scale;
            boss->mins *= boss->s.scale;

            float health_multiplier = 1.0f;
            float power_armor_multiplier = 1.0f;
            ApplyBossEffects(boss, mapSize.isSmallMap, mapSize.isMediumMap, mapSize.isBigMap, health_multiplier, power_armor_multiplier);

            std::string full_display_name = GetDisplayName(boss);
            gi.configstring(CONFIG_HEALTH_BAR_NAME, full_display_name.c_str());

            int base_health = static_cast<int>(boss->health * health_multiplier);
            SetMonsterHealth(boss, base_health, current_wave_number); // Pasar current_wave_number

            boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * power_armor_multiplier);
            boss->monsterinfo.power_armor_power *= g_horde_local.level * 1.45;
            boss->gib_health = -2000000;

            vec3_t effectPosition = boss->s.origin;
            effectPosition[0] += (boss->s.origin[0] - effectPosition[0]) * (boss->s.scale - 3);
            effectPosition[1] += (boss->s.origin[1] - effectPosition[1]) * (boss->s.scale - 3);
            effectPosition[2] += (boss->s.origin[2] - effectPosition[2]) * (boss->s.scale - 3);

            gi.WriteByte(svc_temp_entity);
            gi.WriteByte(TE_BOSSTPORT);
            gi.WritePosition(effectPosition);
            gi.multicast(effectPosition, MULTICAST_PHS, false);
            ED_CallSpawn(boss);

            AttachHealthBar(boss);
        }
    }
}

void ResetBenefits() {
    shuffled_benefits.clear();
    obtained_benefits.clear();
    vampire_level = 0;
}

void ResetGame() {
    ResetBenefits();
    current_wave_number = 1;
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
    gi.cvar_set("dm_monsters", "0");
    gi.cvar_set("timelimit", "25");
}

bool CheckRemainingMonstersCondition(const MapSize& mapSize) {
    static std::chrono::steady_clock::time_point condition_start_time = std::chrono::steady_clock::time_point::min();
    static int previous_remainingMonsters = 0;

    int maxMonsters = 0;
    int timeThreshold = 0;

    int numActivePlayers = 0;
    for (uint32_t player = 1; player <= game.maxclients; ++player) {
        edict_t* ent = &g_edicts[player];
        if (!ent->inuse || !ent->client || !ent->solid) continue;

        numActivePlayers++;
    }

    if (numActivePlayers >= 6) {
        if (mapSize.isSmallMap) {
            maxMonsters = 7;
            timeThreshold = 4;
        }
        else if (mapSize.isBigMap) {
            maxMonsters = 25;
            timeThreshold = 18;
        }
        else {
            maxMonsters = 12;
            timeThreshold = 8;
        }
    }
    else {
        if (mapSize.isSmallMap) {
            maxMonsters = (current_wave_number <= 4) ? 3 : 6;
            timeThreshold = (current_wave_number <= 4) ? 7 : 13;
        }
        else if (mapSize.isBigMap) {
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

    int remainingMonsters = level.total_monsters - level.killed_monsters;

    if (remainingMonsters <= maxMonsters) {
        if (condition_start_time == std::chrono::steady_clock::time_point::min()) {
            condition_start_time = std::chrono::steady_clock::now();
        }

        auto duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - condition_start_time);

        if (duration.count() >= timeThreshold) {
            condition_start_time = std::chrono::steady_clock::time_point::min();
            return true;
        }
    }
    else if (remainingMonsters > previous_remainingMonsters) {
        condition_start_time = std::chrono::steady_clock::time_point::min();
    }

    previous_remainingMonsters = remainingMonsters;
    return false;
}

void SpawnMonsters() {
    const int monsters_per_spawn = 3; // Ajusta la cantidad de monstruos que deseas spawnar en cada llamada
    int spawned = 0;

    for (int i = 0; i < monsters_per_spawn && g_horde_local.num_to_spawn > 0; ++i) {
        edict_t* spawn_point = SelectDeathmatchSpawnPoint(true, true, false).spot; // Seleccionar punto de spawn
        if (!spawn_point) continue;

        const char* monster_classname = G_HordePickMonster(spawn_point);
        if (!monster_classname) continue;

        edict_t* monster = G_Spawn();
        monster->classname = monster_classname;
        monster->item = G_HordePickItem();
        VectorCopy(spawn_point->s.origin, monster->s.origin);
        VectorCopy(spawn_point->s.angles, monster->s.angles);
        ED_CallSpawn(monster);

        --g_horde_local.num_to_spawn;
        ++spawned;
    }

    // Ajusta el tiempo de spawn para evitar spawn rápido
    g_horde_local.monster_spawn_time = level.time + 1.5_sec; // Ajusta el tiempo entre spawns según sea necesario
}


void Horde_RunFrame() {
    auto mapSize = GetMapSize(level.mapname);

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

            // Limitar el número de monstruos activos simultáneamente en el mapa
            int activeMonsters = level.total_monsters - level.killed_monsters;
            int maxMonsters = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
                mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP :
                MAX_MONSTERS_BIG_MAP;

            if (activeMonsters < maxMonsters) {
                SpawnMonsters();
            }

            if (!g_horde_local.num_to_spawn) {
                std::ostringstream message_stream;
                message_stream << "New Wave Is Here.\nWave Level: " << g_horde_local.level << "\n";
                gi.LocBroadcast_Print(PRINT_TYPEWRITER, message_stream.str().c_str());
                g_horde_local.state = horde_state_t::cleanup;
                g_horde_local.monster_spawn_time = level.time + 1_sec;
            }
        }
        break;

    case horde_state_t::cleanup:
        if (CheckRemainingMonstersCondition(mapSize)) {
            if (current_wave_number >= 15 && current_wave_number <= 33) {
                gi.cvar_set("g_insane", "1");
                gi.cvar_set("g_chaotic", "0");
                g_horde_local.state = horde_state_t::rest;
                break;
            }
            else if (current_wave_number >= 34) {
                gi.cvar_set("g_insane", "2");
                gi.cvar_set("g_chaotic", "0");
            }
            else if (!mapSize.isSmallMap && current_wave_number <= 14) {
                gi.cvar_set("g_chaotic", "1");
            }
            else if (mapSize.isSmallMap && current_wave_number <= 14) {
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

                if (g_insane->integer == 2) {
                    gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nInsane Wave Controlled, GG!\n");
                    gi.sound(world, CHAN_VOICE, gi.soundindex("world/x_light.wav"), 1, ATTN_NONE, 0);
                    gi.cvar_set("g_chaotic", "0");
                    gi.cvar_set("g_insane", "1");
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