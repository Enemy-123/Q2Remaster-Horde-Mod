// Includes y definiciones relevantes
#include "../g_local.h"
#include "g_horde.h"
#include "../shared.h"
#include <set>
int GetNumActivePlayers() noexcept;
int GetNumSpectPlayers() noexcept;
int GetNumHumanPlayers() noexcept;

constexpr int32_t MAX_MONSTERS_BIG_MAP = 27;
constexpr int32_t MAX_MONSTERS_MEDIUM_MAP = 18;
constexpr int32_t MAX_MONSTERS_SMALL_MAP = 14;

bool allowWaveAdvance = false; // Variable global para controlar el avance de la ola
bool boss_spawned_for_wave = false; // Variable de control para el jefe
bool flying_monsters_mode = false; // Variable de control para el jefe volador

int32_t last_wave_number = 0;
static int32_t cachedRemainingMonsters = -1;

gtime_t horde_message_end_time = 0_sec;
gtime_t SPAWN_POINT_COOLDOWN = gtime_t::from_sec(3.9); // Cooldown en segundos para los puntos de spawn 3.0

cvar_t* g_horde;

enum class horde_state_t {
    warmup,
    spawning,
    cleanup,
    rest
};

struct HordeState {
    gtime_t         warm_time = 4_sec;
    horde_state_t   state = horde_state_t::warmup;
    gtime_t         monster_spawn_time;
    int32_t         num_to_spawn = 0;
    int32_t         level = 0;
} g_horde_local;

int32_t current_wave_number = g_horde_local.level;
bool next_wave_message_sent = false;
int32_t vampire_level = 0;

std::vector<std::string> shuffled_benefits;
std::unordered_set<edict_t*> auto_spawned_bosses;
std::unordered_set<std::string> obtained_benefits;
std::unordered_map<std::string, gtime_t> lastMonsterSpawnTime;
std::unordered_map<edict_t*, gtime_t> lastSpawnPointTime;
std::unordered_map<edict_t*, int32_t> spawnAttempts;
std::unordered_map<edict_t*, float> spawnPointCooldowns;

const std::unordered_set<std::string> smallMaps = {
    "q2dm3", "q2dm7", "q2dm2", "q64/dm10", "test/mals_barrier_test",
    "q64/dm9", "q64/dm7", "q64\\dm7", "q64/dm2", "test/spbox",
    "q64/dm1", "fact3", "q2ctf4", "rdm4", "q64/command","mgu3m4",
    "mgu4trial", "mgu6trial", "ec/base_ec", "mgdm1", "ndctf0", "q64/dm6",
    "q64/dm8", "q64/dm4", "industry"
};

const std::unordered_set<std::string> bigMaps = {
    "q2ctf5", "old/kmdm3", "xdm2", "xdm6", "rdm6"
};

// Funci�n para obtener el tama�o del mapa
MapSize GetMapSize(const std::string& mapname) noexcept {
    MapSize mapSize;
    mapSize.isSmallMap = smallMaps.count(mapname) > 0;
    mapSize.isBigMap = bigMaps.count(mapname) > 0;
    mapSize.isMediumMap = !mapSize.isSmallMap && !mapSize.isBigMap;
    return mapSize;
}

// Definici�n de la estructura weighted_benefit_t
struct weighted_benefit_t {
    const char* benefit_name;
    int32_t min_level;
    int32_t max_level;
    float weight;
};

// Lista de beneficios ponderados
constexpr weighted_benefit_t benefits[] = {
    { "vampire", 4, -1, 0.2f },
    { "vampire upgraded", 24, -1, 0.1f },
    { "ammo regen", 8, -1, 0.15f },
    { "auto haste", 9, -1, 0.15f },
    { "start armor", 9, -1, 0.1f },
    { "Traced-Piercing Bullets", 9, -1, 0.2f },
    { "Cluster Prox Grenades", 28, -1, 0.2f },
    { "Napalm-Grenade Launcher", 25, -1, 0.2f }
};

static std::random_device rd;
static std::mt19937 gen(rd());

// Funci�n para mezclar los beneficios
void ShuffleBenefits() {
    shuffled_benefits.clear();
    shuffled_benefits.reserve(std::size(benefits)); // Reservar espacio para todos los beneficios
    for (const auto& benefit : benefits) {
        shuffled_benefits.push_back(benefit.benefit_name);
    }
    std::shuffle(shuffled_benefits.begin(), shuffled_benefits.end(), std::default_random_engine());

    // Asegurar que 'vampire' viene antes que 'vampire upgraded'
    auto vampire_it = std::find(shuffled_benefits.begin(), shuffled_benefits.end(), "vampire");
    auto upgraded_it = std::find(shuffled_benefits.begin(), shuffled_benefits.end(), "vampire upgraded");
    if (vampire_it != shuffled_benefits.end() && upgraded_it != shuffled_benefits.end() && vampire_it > upgraded_it) {
        std::iter_swap(vampire_it, upgraded_it);
    }
}
// Estructura para almacenar los beneficios seleccionados
struct picked_benefit_t {
    const weighted_benefit_t* benefit;
    float weight;
};


// Funci�n para seleccionar un beneficio aleatorio
std::string SelectRandomBenefit(int32_t wave) {
    std::vector<picked_benefit_t> picked_benefits;
    picked_benefits.reserve(std::size(benefits)); // Reservar espacio para todos los beneficios
    float total_weight = 0.0f;

    for (const auto& benefit : benefits) {
        if ((wave >= benefit.min_level) &&
            (benefit.max_level == -1 || wave <= benefit.max_level) &&
            obtained_benefits.find(benefit.benefit_name) == obtained_benefits.end()) {
            total_weight += benefit.weight;
            picked_benefits.push_back({ &benefit, total_weight });
        }
    }

    if (picked_benefits.empty()) return "";  // Verificar si est� vac�o antes de proceder

    float random_weight = frandom() * total_weight;
    auto it = std::find_if(picked_benefits.begin(), picked_benefits.end(),
        [random_weight](const picked_benefit_t& picked_benefit) {
            return random_weight < picked_benefit.weight;
        });
    return (it != picked_benefits.end()) ? it->benefit->benefit_name : "";
}


// Funci�n para aplicar un beneficio espec�fico
void ApplyBenefit(const std::string& benefit) {
    static const std::unordered_map<std::string, std::pair<const char*, const char*>> benefitMessages = {
        {"start armor", {"\n\n\nSTARTING ARMOR\nENABLED!\n", "STARTING WITH 50 BODY-ARMOR!\n"}},
        {"vampire", {"\n\n\nYou're covered in blood!\n\nVampire Ability\nENABLED!\n", "RECOVERING A HEALTH PERCENTAGE OF DAMAGE DONE!\n"}},
        {"ammo regen", {"AMMO REGEN\n\nENABLED!\n", "AMMO REGEN IS NOW ENABLED!\n"}},
        {"auto haste", {"\n\nDUAL-FIRE IS RUNNING THROUGH YOUR VEINS \nFRAGGING WHILE HASTE\nWILL EXTEND QUAD DMG AND DUAL-FIRE TIME!\n", "AUTO-HASTE ENABLED !\n"}},
        {"vampire upgraded", {"\n\n\n\nIMPROVED VAMPIRE ABILITY\n", "RECOVERING HEALTH & ARMOR NOW!\n"}},
        {"Cluster Prox Grenades", {"\n\n\n\nIMPROVED PROX GRENADES\n", "Prox Cluster Launcher Enabled\n"}},
        {"Traced-Piercing Bullets", {"\n\n\n\nBULLETS\nUPGRADED!\n", "Piercing-PowerShield Bullets!\n"}},
        {"Napalm-Grenade Launcher", {"\n\n\n\nIMPROVED GRENADE LAUNCHER!\n", "Napalm-Grenade Launcher Enabled\n"}},
    };

    auto it = benefitMessages.find(benefit);
    if (it != benefitMessages.end()) {
        // Aplicar el beneficio
        if (benefit == "start armor") {
            gi.cvar_set("g_startarmor", "1");
        }
        else if (benefit == "vampire") {
            vampire_level = 1;
            gi.cvar_set("g_vampire", "1");
        }
        else if (benefit == "vampire upgraded") {
            vampire_level = 2;
            gi.cvar_set("g_vampire", "2");
        }
        else if (benefit == "ammo regen") {
            gi.cvar_set("g_ammoregen", "1");
        }
        else if (benefit == "auto haste") {
            gi.cvar_set("g_autohaste", "1");
        }
        else if (benefit == "Cluster Prox Grenades") {
            gi.cvar_set("g_upgradeproxs", "1");
        }
        else if (benefit == "Traced-Piercing Bullets") {
            gi.cvar_set("g_tracedbullets", "1");
        }
        else if (benefit == "Napalm-Grenade Launcher") {
            gi.cvar_set("g_bouncygl", "1");
        }

        // Enviar los mensajes de beneficio
        gi.LocBroadcast_Print(PRINT_CENTER, it->second.first);
        if (!std::string(it->second.second).empty()) {
            gi.LocBroadcast_Print(PRINT_CHAT, it->second.second);
        }

        // Marcar el beneficio como obtenido
        obtained_benefits.insert(benefit);
    }
    else {
        // Mensaje de depuraci�n en caso de que el beneficio no sea encontrado
        gi.Com_PrintFmt("Benefit not found: {}\n", benefit.c_str());
    }
}


// Funci�n para verificar y aplicar beneficios basados en la ola
void CheckAndApplyBenefit(int32_t wave) {
    if (wave % 4 == 0) {
        if (shuffled_benefits.empty()) {
            ShuffleBenefits();
        }

        std::string benefit = SelectRandomBenefit(wave);
        if (!benefit.empty()) {
            ApplyBenefit(benefit);
        }
    }
}

// Funci�n para ajustar la tasa de aparici�n de monstruos
void AdjustMonsterSpawnRate() noexcept {
    auto humanPlayers = GetNumHumanPlayers();
    float difficultyMultiplier = 1.0f + (humanPlayers - 1) * 0.1f; // Increase difficulty per player

    if (g_horde_local.level % 3 == 0) {
        g_horde_local.num_to_spawn = static_cast<int32_t>(g_horde_local.num_to_spawn * difficultyMultiplier);
        g_horde_local.monster_spawn_time -= ((!g_chaotic->integer || !g_insane->integer) ? 0.5_sec : 0.4_sec) * difficultyMultiplier;
        if (g_horde_local.monster_spawn_time < 0.7_sec) {
            g_horde_local.monster_spawn_time = 0.7_sec;
        }
        SPAWN_POINT_COOLDOWN -= ((!g_chaotic->integer || !g_insane->integer) ? 0.6_sec : 0.4_sec) * difficultyMultiplier;
        if (SPAWN_POINT_COOLDOWN < 2.0_sec) {
            SPAWN_POINT_COOLDOWN = 2.0_sec;
        }
    }
}

// Funci�n para calcular la cantidad de monstruos est�ndar a spawnear
void CalculateStandardSpawnCount(const MapSize& mapSize, int32_t lvl) noexcept {
    if (mapSize.isSmallMap) {
        g_horde_local.num_to_spawn = std::min((current_wave_number <= 6) ? 7 : 9 + lvl, MAX_MONSTERS_SMALL_MAP);
    }
    else if (mapSize.isBigMap) {
        g_horde_local.num_to_spawn = std::min((current_wave_number <= 4) ? 24 : 27 + (lvl), MAX_MONSTERS_BIG_MAP);
    }
    else {
        g_horde_local.num_to_spawn = std::min((current_wave_number <= 4) ? 5 : 8 + lvl, MAX_MONSTERS_MEDIUM_MAP);
    }
}
// Funci�n para calcular el bono de locura y caos
int32_t CalculateChaosInsanityBonus(int32_t lvl) noexcept {
    (g_chaotic->integer) ? 3 : 5;

    if (g_insane->integer) {
        if (g_insane->integer == 2) {
            return 16;
        }

        if (g_insane->integer == 1) {
            return 8;
        }

        if (g_chaotic->integer && current_wave_number <= 3) {
            return 6;
        }

        return 8;
    }

    return 0;
}

// Funci�n para incluir ajustes de dificultad
void IncludeDifficultyAdjustments(const MapSize& mapSize, int32_t lvl) noexcept {
    int32_t additionalSpawn = 0;
    if (mapSize.isSmallMap) {
        additionalSpawn = (current_wave_number >= 9) ? 7 : 6;
    }
    else if (mapSize.isBigMap) {
        additionalSpawn = (current_wave_number >= 9) ? 12 : 8;
    }
    else {
        additionalSpawn = (current_wave_number >= 9) ? 7 : 6;
    }

    if (current_wave_number > 25) {
        additionalSpawn *= 1.6;
    }

    if (current_wave_number >= 3 && (g_chaotic->integer || g_insane->integer)) {
        additionalSpawn += CalculateChaosInsanityBonus(lvl);
    }

    g_horde_local.num_to_spawn += additionalSpawn;
}

// Funci�n para determinar la cantidad de monstruos a spawnear
void DetermineMonsterSpawnCount(const MapSize& mapSize, int32_t lvl) noexcept {
    int32_t custom_monster_count = dm_monsters->integer;
    if (custom_monster_count > 0) {
        g_horde_local.num_to_spawn = custom_monster_count;
    }
    else {
        CalculateStandardSpawnCount(mapSize, lvl);
        IncludeDifficultyAdjustments(mapSize, lvl);
    }
}
static void Horde_CleanBodies() noexcept;
void ResetSpawnAttempts() noexcept;
void VerifyAndAdjustBots() noexcept;
void ResetCooldowns() noexcept;

void Horde_InitLevel(int32_t lvl) noexcept {
    last_wave_number++;
    g_horde_local.level = lvl;
    current_wave_number = lvl;
    g_horde_local.monster_spawn_time = level.time;
    flying_monsters_mode = false;
    boss_spawned_for_wave = false;

    // Inicializar cachedRemainingMonsters
    cachedRemainingMonsters = -1;

    // Verificar y ajustar bots
    VerifyAndAdjustBots();

    // Configurar la escala de daño según el nivel
    if (g_horde_local.level == 17) {
        gi.cvar_set("g_damage_scale", "1.7");
    }
    else if (g_horde_local.level == 27) {
        gi.cvar_set("g_damage_scale", "2.7");
    }
    else if (g_horde_local.level == 37) {
        gi.cvar_set("g_damage_scale", "3.7");
    }

    // Configuración de la cantidad de monstruos a spawnear
    auto mapSize = GetMapSize(level.mapname);
    DetermineMonsterSpawnCount(mapSize, lvl);

    // Revisar y aplicar beneficios basados en la ola
    CheckAndApplyBenefit(g_horde_local.level);

    // Ajustar tasa de aparición de monstruos
    AdjustMonsterSpawnRate();

    // Reiniciar cooldowns y contadores de intentos de spawn
    ResetSpawnAttempts();
    ResetCooldowns();

    UpdateAllClients();

    // Limpiar cuerpos de olas anteriores
    Horde_CleanBodies();

    // Imprimir mensaje de inicio del nivel
    gi.Com_PrintFmt("Horde level initialized: {}\n", lvl);

//    // Spawnear naves Strogg en la fase de inicialización del nivel
//    if (lvl % 2 == 0) { // Ajusta la condición según la frecuencia que desees
//        void SP_misc_strogg_ship(edict_t * ent);
//        edict_t* strogg_ship = G_Spawn();
//        if (strogg_ship) {
//            gi.Com_PrintFmt("Strogg Ship passing by");
//            SP_misc_strogg_ship(strogg_ship);
//        }
//    }
}




bool G_IsDeathmatch() noexcept {
    return deathmatch->integer && g_horde->integer;
}

bool G_IsCooperative() noexcept {
    return coop->integer && !g_horde->integer;
}

struct weighted_item_t;
using weight_adjust_func_t = void(*)(const weighted_item_t& item, float& weight);

void adjust_weight_health(const weighted_item_t& item, float& weight) noexcept {}
void adjust_weight_weapon(const weighted_item_t& item, float& weight) noexcept {}
void adjust_weight_ammo(const weighted_item_t& item, float& weight) noexcept {}
void adjust_weight_armor(const weighted_item_t& item, float& weight) noexcept {}
void adjust_weight_powerup(const weighted_item_t& item, float& weight) noexcept {}

constexpr struct weighted_item_t {
    const char* classname;
    int32_t min_level = -1, max_level = -1;
    float weight = 1.0f;
    weight_adjust_func_t adjust_weight = nullptr;
} items[] = {
    { "item_health", 3, 5, 0.20f, adjust_weight_health },
    { "item_health_large", -1, 4, 0.06f, adjust_weight_health },
    { "item_health_large", 5, -1, 0.12f, adjust_weight_health },
    { "item_health_mega", 4, -1, 0.04f, adjust_weight_health },
    { "item_adrenaline", -1, -1, 0.07f, adjust_weight_health },

    { "item_armor_shard", -1, 7, 0.09f, adjust_weight_armor },
    { "item_armor_jacket", -1, 12, 0.1f, adjust_weight_armor },
    { "item_armor_combat", 13, -1, 0.06f, adjust_weight_armor },
    { "item_armor_body", 27, -1, 0.015f, adjust_weight_armor },
    { "item_power_screen", 2, 8, 0.03f, adjust_weight_armor },
    { "item_power_shield", 14, -1, 0.07f, adjust_weight_armor },

    { "item_quad", 6, -1, 0.054f, adjust_weight_powerup },
    { "item_double", 4, -1, 0.07f, adjust_weight_powerup },
    { "item_quadfire", 2, -1, 0.06f, adjust_weight_powerup },
    { "item_invulnerability", 4, -1, 0.051f, adjust_weight_powerup },
    { "item_sphere_defender", 2, -1, 0.06f, adjust_weight_powerup },
    { "item_sphere_hunter", 9, -1, 0.06f, adjust_weight_powerup },
    { "item_invisibility", 4, -1, 0.06f, adjust_weight_powerup },
    { "item_doppleganger", 2, 8, 0.028f, adjust_weight_powerup },
    { "item_doppleganger", 9, 19, 0.062f, adjust_weight_powerup },
    { "item_doppleganger", 20, -1, 0.1f, adjust_weight_powerup },

    { "weapon_chainfist", -1, 3, 0.12f, adjust_weight_weapon },
    { "weapon_shotgun", -1, -1, 0.27f, adjust_weight_weapon },
    { "weapon_supershotgun", 5, -1, 0.18f, adjust_weight_weapon },
    { "weapon_machinegun", -1, -1, 0.29f, adjust_weight_weapon },
    { "weapon_etf_rifle", 4, -1, 0.19f, adjust_weight_weapon },
    { "weapon_chaingun", 9, -1, 0.19f, adjust_weight_weapon },
    { "weapon_grenadelauncher", 10, -1, 0.19f, adjust_weight_weapon },
    { "weapon_proxlauncher", 4, -1, 0.1f, adjust_weight_weapon },
    { "weapon_boomer", 14, -1, 0.19f, adjust_weight_weapon },
    { "weapon_hyperblaster", 12, -1, 0.22f, adjust_weight_weapon },
    { "weapon_phalanx", 16, -1, 0.19f, adjust_weight_weapon },
    { "weapon_rocketlauncher", 14, -1, 0.19f, adjust_weight_weapon },
    { "weapon_railgun", 22, -1, 0.19f, adjust_weight_weapon },
    { "weapon_plasmabeam", 17, -1, 0.29f, adjust_weight_weapon },
    { "weapon_disintegrator", 24, -1, 0.15f, adjust_weight_weapon },
    { "weapon_bfg", 25, -1, 0.15f, adjust_weight_weapon },


    { "ammo_trap", 4, -1, 0.18f, adjust_weight_ammo },
    { "ammo_bullets", -1, -1, 0.25f, adjust_weight_ammo },
    { "ammo_flechettes", 4, -1, 0.25f, adjust_weight_ammo },
    { "ammo_grenades", -1, -1, 0.25f, adjust_weight_ammo },
    { "ammo_prox", 5, -1, 0.25f, adjust_weight_ammo },
    { "ammo_tesla", 2, -1, 0.1f, adjust_weight_ammo },
    { "ammo_cells", 13, -1, 0.25f, adjust_weight_ammo },
    { "ammo_magslug", 15, -1, 0.25f, adjust_weight_ammo },
    { "ammo_slugs", 22, -1, 0.25f, adjust_weight_ammo },
    { "ammo_disruptor", 24, -1, 0.25f, adjust_weight_ammo },
    { "ammo_rockets", 13, -1, 0.25f, adjust_weight_ammo },

    { "item_bandolier", 5, -1, 0.18f, adjust_weight_ammo },
    { "item_pack", 15, -1, 0.32f, adjust_weight_ammo },
    { "item_silencer", 15, -1, 0.12f, adjust_weight_ammo },
};

// Definici�n de monstruos ponderados
constexpr weighted_item_t monsters[] = {
    { "monster_soldier_light", -1, 19, 0.35f },
    { "monster_soldier_ss", -1, 20, 0.45f },
    { "monster_soldier", -1, 2, 0.45f },
    { "monster_soldier", 3, 9, 0.35f },
    { "monster_soldier_hypergun", 3, -1, 0.35f },
    { "monster_soldier_lasergun", 5, -1, 0.45f },
    { "monster_soldier_ripper", 2, 7, 0.45f },
    { "monster_infantry2", 2, -1, 0.36f },
    { "monster_infantry", 13, -1, 0.36f },
    { "monster_flyer", -1, 2, 0.07f },
    { "monster_flyer", 3, -1, 0.1f },
    { "monster_hover2", 6, 19, 0.24f },
    { "monster_fixbot", 8, 21, 0.11f },
    { "monster_gekk", -1, 3, 0.1f },
    { "monster_gekk", 4, 17, 0.17f },
    { "monster_gunner2", 5, -1, 0.35f },
    { "monster_gunner", 14, -1, 0.34f },
    { "monster_brain", 7, 22, 0.2f },
    { "monster_brain", 23, -1, 0.35f },
    { "monster_stalker", 2, 3, 0.05f },
    { "monster_stalker", 4, 13, 0.19f },
    { "monster_parasite", 4, 17, 0.23f },
    { "monster_tank", 11, -1, 0.3f },
    { "monster_tank2", 7, 15, 0.24f },
    { "monster_guncmdr2", 9, 10, 0.18f },
    { "monster_mutant", 4, -1, 0.35f },
    { "monster_redmutant", 6, 12, 0.06f },
    { "monster_redmutant", 13, -1, 0.35f },
    { "monster_chick", 5, 26, 0.3f },
    { "monster_chick_heat", 10, -1, 0.34f },
    { "monster_berserk", 4, -1, 0.35f },
    { "monster_floater", 8, -1, 0.26f },
    { "monster_hover", 15, -1, 0.18f },
    { "monster_daedalus", 13, -1, 0.13f },
    { "monster_daedalus2", 19, -1, 0.14f },
    { "monster_medic", 5, -1, 0.1f },
    { "monster_medic_commander", 16, -1, 0.06f },
    { "monster_tank_commander", 11, -1, 0.15f },
    { "monster_spider", 13, 19, 0.27f },
    { "monster_gm_arachnid", 22, -1, 0.34f },
    { "monster_arachnid", 20, -1, 0.27f },
    { "monster_guncmdr", 11, -1, 0.28f },
    { "monster_gladc", 7, -1, 0.3f },
    { "monster_gladiator", 9, -1, 0.3f },
    { "monster_shambler", 14, 28, 0.03f },
    { "monster_shambler", 29, -1, 0.33f },
    { "monster_floater2", 19, -1, 0.35f },
    { "monster_tank_64", 24, -1, 0.14f },
    { "monster_janitor", 16, -1, 0.14f },
    { "monster_janitor2", 26, -1, 0.12f },
    { "monster_makron", 17, 22, 0.02f },
    { "monster_gladb", 16, -1, 0.45f },
    { "monster_boss2_64", 16, -1, 0.05f },
    { "monster_carrier2", 20, -1, 0.07f },
    { "monster_perrokl", 21, -1, 0.33f },
    { "monster_widow1", 23, -1, 0.08f }
};

// Definici�n de jefes por tama�o de mapa
struct boss_t {
    const char* classname;
    int32_t min_level;
    int32_t max_level;
    float weight;
};

constexpr boss_t BOSS_SMALL[] = {
    {"monster_carrier2", 24, -1, 0.05f},
    {"monster_boss2kl", 24, -1, 0.05f},
    {"monster_widow2", 19, -1, 0.05f},
    {"monster_tank_64", -1, -1, 0.05f},
    {"monster_shamblerkl", -1, -1, 0.05f},
    {"monster_guncmdrkl", -1, 19, 0.05f},
    {"monster_makronkl", 36, -1, 0.05f},
    {"monster_gm_arachnid", -1, 19, 0.05f},
    {"monster_arachnid", -1, 19, 0.05f},
    {"monster_redmutant", -1, 24, 0.05f}
};

constexpr boss_t BOSS_MEDIUM[] = {
    {"monster_carrier", 24, -1, 0.1f},
    {"monster_boss2", 19, -1, 0.1f},
    {"monster_tank_64", -1, 24, 0.1f},
    {"monster_guardian", -1, 24, 0.1f},
    {"monster_shamblerkl", -1, 24, 0.1f},
    {"monster_guncmdrkl", -1, 24, 0.1f},
    {"monster_gm_arachnid", -1, 24, 0.1f},
    {"monster_arachnid", -1, 24, 0.1f},
    {"monster_makronkl", 26, -1, 0.1f}
};

constexpr boss_t BOSS_LARGE[] = {
    {"monster_carrier", 24, -1, 0.15f},
    {"monster_boss2", 19, -1, 0.15f},
    {"monster_boss5", -1, -1, 0.15f},
    {"monster_tank_64", -1, 24, 0.15f},
    {"monster_guardian", -1, 24, 0.15f},
    {"monster_shamblerkl", -1, 24, 0.15f},
    {"monster_boss5", -1, 24, 0.15f},
    {"monster_jorg", 30, -1, 0.15f}
};

// Funci�n para obtener la lista de jefes basada en el tama�o del mapa
const boss_t* GetBossList(const MapSize& mapSize, const std::string& mapname) noexcept {
    if (mapSize.isSmallMap || mapname == "q2dm4" || mapname == "q64/comm") {
        return BOSS_SMALL;
    }

    if (mapSize.isMediumMap) {
        if (mapname == "q64/dm3" || mapname == "mgu6m3" || mapname == "rboss") {
            static std::vector<boss_t> filteredBossList;
            if (filteredBossList.empty()) {
                for (const auto& boss : BOSS_MEDIUM) {
                    if (std::strcmp(boss.classname, "monster_guardian") != 0) {
                        filteredBossList.push_back(boss);
                    }
                }
            }
            return filteredBossList.empty() ? nullptr : filteredBossList.data();
        }
        return BOSS_MEDIUM;
    }

    if (mapSize.isBigMap) {
        return BOSS_LARGE;
    }

    return nullptr;
}

constexpr int32_t MAX_RECENT_BOSSES = 3;
std::set<const char*> recent_bosses;  // Conjunto de jefes recientes para evitar selecciones repetidas r�pidamente.

// Funci�n para seleccionar un jefe basado en el tama�o del mapa y el nombre del mapa
const char* G_HordePickBOSS(const MapSize& mapSize, const std::string& mapname, int32_t waveNumber) noexcept {
    const boss_t* boss_list = GetBossList(mapSize, mapname);
    if (!boss_list) return nullptr;

    std::vector<const boss_t*> eligible_bosses;
    auto boss_list_size = mapSize.isSmallMap ? std::size(BOSS_SMALL) :
        mapSize.isMediumMap ? std::size(BOSS_MEDIUM) :
        std::size(BOSS_LARGE);

    for (int32_t i = 0; i < boss_list_size; ++i) {
        const boss_t& boss = boss_list[i];
        if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
            (waveNumber <= boss.max_level || boss.max_level == -1) &&
            recent_bosses.find(boss.classname) == recent_bosses.end()) {
            eligible_bosses.push_back(&boss);
        }
    }

    if (eligible_bosses.empty()) {
        recent_bosses.clear();
        for (int32_t i = 0; i < boss_list_size; ++i) {
            const boss_t& boss = boss_list[i];
            if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
                (waveNumber <= boss.max_level || boss.max_level == -1)) {
                eligible_bosses.push_back(&boss);
            }
        }
    }

    if (!eligible_bosses.empty()) {
        const boss_t* chosen_boss = eligible_bosses[static_cast<int32_t>(frandom() * eligible_bosses.size())];
        recent_bosses.insert(chosen_boss->classname);
        if (recent_bosses.size() > MAX_RECENT_BOSSES) {
            recent_bosses.erase(recent_bosses.begin());
        }
        return chosen_boss->classname;
    }

    return nullptr;
}

struct picked_item_t {
    const weighted_item_t* item;
    float weight;
};

gitem_t* G_HordePickItem() noexcept {
    std::vector<picked_item_t> picked_items;
    float total_weight = 0;

    for (const auto& item : items) {
        if ((item.min_level != -1 && g_horde_local.level < item.min_level) ||
            (item.max_level != -1 && g_horde_local.level > item.max_level)) {
            continue;
        }

        float weight = item.weight;
        if (item.adjust_weight) item.adjust_weight(item, weight);
        if (weight <= 0) continue;

        total_weight += weight;
        picked_items.push_back({ &item, total_weight });
    }

    if (total_weight == 0) return nullptr;

    const float random_weight = frandom() * total_weight;
    auto it = std::find_if(picked_items.begin(), picked_items.end(),
        [random_weight](const picked_item_t& item) { return random_weight < item.weight; });
    return it != picked_items.end() ? FindItemByClassname(it->item->classname) : nullptr;
}
#include <array>
#include <unordered_set>
#include <vector>
#include <algorithm>

 int32_t WAVE_TO_ALLOW_FLYING;
extern bool flying_monsters_mode;
extern gtime_t SPAWN_POINT_COOLDOWN;

// Keep the existing array
constexpr std::array<const char*, 10> flying_monster_classnames = {
    "monster_boss2_64",
    "monster_carrier2",
    "monster_floater",
    "monster_floater2",
    "monster_flyer",
    "monster_fixbot",
    "monster_hover",
    "monster_hover2",
    "monster_daedalus",
    "monster_daedalus2"
};

// Create a static set for faster lookup
static const std::unordered_set<std::string> flying_monsters_set(
    flying_monster_classnames.begin(), flying_monster_classnames.end());

int32_t countFlyingSpawns() noexcept {
    int32_t count = 0;
    for (size_t i = 0; i < globals.num_edicts; i++) {
        const auto& ent = g_edicts[i];
        if (ent.inuse && strcmp(ent.classname, "info_player_deathmatch") == 0 && ent.style == 1) {
            count++;
        }
    }
    return count;
}

bool IsFlyingMonster(const char* classname) noexcept {
    return flying_monsters_set.find(classname) != flying_monsters_set.end();
}

float adjustFlyingSpawnProbability(int32_t flyingSpawns) noexcept {
    return (flyingSpawns > 0) ? 0.25f : 1.0f;
}

bool IsMonsterEligible(edict_t* spawn_point, const weighted_item_t& item, bool isFlyingMonster, int32_t currentWave, int32_t flyingSpawns) noexcept {
    return !(spawn_point->style == 1 && !isFlyingMonster) &&
        !(item.min_level > currentWave || (item.max_level != -1 && item.max_level < currentWave)) &&
        !(isFlyingMonster && currentWave < WAVE_TO_ALLOW_FLYING);
}

float CalculateWeight(const weighted_item_t& item, bool isFlyingMonster, float adjustmentFactor) noexcept {
    return item.weight * (isFlyingMonster ? adjustmentFactor : 1.0f);
}

void ResetSpawnAttempts(edict_t* spawn_point) noexcept {
    spawnAttempts[spawn_point] = 0;
    spawnPointCooldowns[spawn_point] = SPAWN_POINT_COOLDOWN.seconds<float>();
}

void UpdateCooldowns(edict_t* spawn_point, const char* classname) noexcept {
    lastSpawnPointTime[spawn_point] = level.time;
    lastMonsterSpawnTime[classname] = level.time;
    spawnPointCooldowns[spawn_point] = SPAWN_POINT_COOLDOWN.seconds<float>();
}

void IncreaseSpawnAttempts(edict_t* spawn_point) noexcept {
    spawnAttempts[spawn_point]++;
    if (spawnAttempts[spawn_point] % 3 == 0) {
        spawnPointCooldowns[spawn_point] *= 0.9f;
    }
}

const char* G_HordePickMonster(edict_t* spawn_point) noexcept {
    auto currentCooldown = SPAWN_POINT_COOLDOWN.seconds<float>();
    auto it_spawnCooldown = spawnPointCooldowns.find(spawn_point);
    if (it_spawnCooldown != spawnPointCooldowns.end()) {
        currentCooldown = it_spawnCooldown->second;
    }

    auto it_lastSpawnTime = lastSpawnPointTime.find(spawn_point);
    if (it_lastSpawnTime != lastSpawnPointTime.end() &&
        (level.time - it_lastSpawnTime->second).seconds<float>() < currentCooldown) {
        return nullptr;
    }

    struct WeightedMonster {
        const weighted_item_t* monster;
        float cumulativeWeight;
    };

    std::vector<WeightedMonster> eligible_monsters;
    float total_weight = 0.0f;
    auto flyingSpawns = countFlyingSpawns();
    float adjustmentFactor = adjustFlyingSpawnProbability(flyingSpawns);

    for (const auto& item : monsters) {
        bool isFlyingMonster = IsFlyingMonster(item.classname);

        if (flying_monsters_mode && !isFlyingMonster) continue;
        if (spawn_point->style == 1 && !isFlyingMonster) continue;
        if (!flying_monsters_mode && isFlyingMonster && spawn_point->style != 1 && flyingSpawns > 0) continue;
        if (!IsMonsterEligible(spawn_point, item, isFlyingMonster, g_horde_local.level, flyingSpawns)) continue;

        float weight = CalculateWeight(item, isFlyingMonster, adjustmentFactor);
        if (weight > 0) {
            total_weight += weight;
            eligible_monsters.push_back({ &item, total_weight });
        }
    }

    if (eligible_monsters.empty()) {
        IncreaseSpawnAttempts(spawn_point);
        return nullptr;
    }

    float r = frandom() * total_weight;
    auto it = std::lower_bound(eligible_monsters.begin(), eligible_monsters.end(), r,
        [](const WeightedMonster& wm, float value) { return wm.cumulativeWeight < value; });

    if (it != eligible_monsters.end()) {
        const char* chosen_monster = it->monster->classname;
        UpdateCooldowns(spawn_point, chosen_monster);
        ResetSpawnAttempts(spawn_point);
        return chosen_monster;
    }

    IncreaseSpawnAttempts(spawn_point);
    return nullptr;
}
void Horde_PreInit() noexcept {
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
        gi.cvar_set("timelimit", "40");
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
        gi.cvar_set("g_startarmor", "0");
        gi.cvar_set("g_vampire", "0");
        gi.cvar_set("g_ammoregen", "0");
        gi.cvar_set("g_tracedbullets", "0");
        gi.cvar_set("g_bouncygl", "0");
        gi.cvar_set("g_autohaste", "0");
        gi.cvar_set("g_chaotic", "0");
        gi.cvar_set("g_insane", "0");
        gi.cvar_set("g_hardcoop", "0");
        gi.cvar_set("capturelimit", "0");
        gi.cvar_set("g_dm_spawns", "0");
        gi.cvar_set("g_damage_scale", "1");
        gi.cvar_set("ai_allow_dm_spawn", "1");
        gi.cvar_set("ai_damage_scale", "1");
        gi.cvar_set("g_loadent", "1");
        gi.cvar_set("bot_chat_enable", "0");
        gi.cvar_set("bot_skill", "5");
        gi.cvar_set("g_coop_squad_respawn", "1");
        gi.cvar_set("g_iddmg", "1");

        HandleResetEvent();
    }
}

// Funci�n para obtener el n�mero de jugadores humanos activos (excluyendo bots)
int32_t GetNumHumanPlayers() noexcept {
    int32_t numHumanPlayers = 0;
    for (auto player : active_players()) {
        if (player->client->resp.ctf_team == CTF_TEAM1 && !(player->svflags & SVF_BOT)) {
            numHumanPlayers++;
        }
    }
    return numHumanPlayers;
}

void VerifyAndAdjustBots() noexcept {
    const auto mapSize = GetMapSize(level.mapname);
    const int32_t humanPlayers = GetNumHumanPlayers();
    const int32_t spectPlayers = GetNumSpectPlayers();
    const int32_t baseBots = mapSize.isBigMap ? 6 : 4;

    // Calcular el n�mero requerido de bots
    int32_t requiredBots = baseBots + spectPlayers;

    // Asegurar que el n�mero de bots no sea menor que el valor base
    requiredBots = std::max(requiredBots, baseBots);

    // Establecer el n�mero de bots m�nimos necesarios
    gi.cvar_set("bot_minClients", std::to_string(requiredBots).c_str());
}


void Horde_Init() noexcept {

    // Precache all items
    for (auto& item : itemlist) PrecacheItem(&item);

    // Precache monsters
    for (const auto& monster : monsters) {
        auto e = G_Spawn();
        if (!e) {
            gi.Com_Print("Error: Failed to spawn monster for precaching.\n");
            continue;
        }

        // Precache items (weapons, powerups, etc.)
        for (const auto& item : items) {
            if (item.classname) {
                PrecacheItem(FindItemByClassname(item.classname));
            }
            else {
                gi.Com_Print("Error: Invalid item classname for precaching.\n");
            }
        }

        // Precache bosses
        for (const auto& boss : BOSS_SMALL) {
            if (boss.classname) {
                PrecacheItem(FindItemByClassname(boss.classname));
            }
            else {
                gi.Com_Print("Error: Invalid boss classname for precaching.\n");
            }
        }
        for (const auto& boss : BOSS_MEDIUM) {
            if (boss.classname) {
                PrecacheItem(FindItemByClassname(boss.classname));
            }
            else {
                gi.Com_Print("Error: Invalid boss classname for precaching.\n");
            }
        }
        for (const auto& boss : BOSS_LARGE) {
            if (boss.classname) {
                PrecacheItem(FindItemByClassname(boss.classname));
            }
            else {
                gi.Com_Print("Error: Invalid boss classname for precaching.\n");
            }
        }

        e->classname = monster.classname;
        e->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
        ED_CallSpawn(e);
        G_FreeEdict(e);
    }

    // Precache wave start sounds
    static const std::vector<std::string> wave_start_sounds = {
        "misc/r_tele3.wav",
        "world/klaxon2.wav",
        "misc/tele_up.wav",
        "world/incoming.wav",
        "world/yelforce.wav"
        //"insane/insane9.wav"
    };

    for (const auto& sound : wave_start_sounds) {
        gi.soundindex(sound.c_str());
    }

    // Precache other sounds
    static const std::vector<std::string> other_sounds = {
        "nav_editor/action_fail.wav",
        "makron/roar1.wav",
        "zortemp/ack.wav",
        "misc/spawn1.wav",
        "makron/voice3.wav",
        "world/v_fac3.wav"
        //    "world/v_fac2.wav",
        //    "insane/insane5.wav",
        //    "insane/insane2.wav",
        //    "world/won.wav"
    };

    for (const auto& sound : other_sounds) {
        gi.soundindex(sound.c_str());
    }

    ResetGame();
}



inline void VectorCopy(const vec3_t& src, vec3_t& dest) noexcept {
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
}

// Manejador de muerte de jefe
void BossDeathHandler(edict_t* boss) noexcept {
    if (g_horde->integer && boss->spawnflags.has(SPAWNFLAG_IS_BOSS) && !boss->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
        OnEntityDeath(boss);  // Llamar a OnEntityDeath para manejar configstrings y health bar
        boss->spawnflags |= SPAWNFLAG_BOSS_DEATH_HANDLED;  // Marcar como manejado

        std::vector<const char*> itemsToDrop;

        if (brandom()) {
            itemsToDrop = {
                "item_adrenaline",
                "item_pack",
                "item_doppleganger",
                "item_sphere_defender",
                "item_armor_combat",
                "item_bandolier"
            };
        }
        else {
            itemsToDrop = {
                "item_adrenaline",
                "item_doppleganger",
                "item_sphere_defender",
                "item_pack",
                "item_bandolier",
                "item_armor_combat"
            };
        }

        // Soltar ítem especial (quad o quadfire)
        edict_t* specialItem{};
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
        velocity[2] = irandom(300, 400) + (rand() % 200);
        VectorCopy(velocity, specialItem->velocity);

        // Soltar los demás ítems y hacer que cada uno salga volando en diferentes direcciones
        for (const auto& itemClassname : itemsToDrop) {
            edict_t* droppedItem = Drop_Item(boss, FindItemByClassname(itemClassname));

            // Establecer posición del ítem
            VectorCopy(boss->s.origin, droppedItem->s.origin);

            // Aplicar velocidad al ítem
            velocity[0] = (rand() % 400) - 200;
            velocity[1] = (rand() % 400) - 200;
            velocity[2] = irandom(650, 800) + (rand() % 200);
            VectorCopy(velocity, droppedItem->velocity);

            // Asegurar que el ítem tenga una velocidad instantánea
            droppedItem->spawnflags & ~SPAWNFLAG_ITEM_DROPPED;
            droppedItem->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
            droppedItem->movetype = MOVETYPE_BOUNCE;
            droppedItem->s.effects |= EF_GIB;
            droppedItem->flags &= ~FL_RESPAWN;
        }

        // Asegurar que el ítem especial tenga una velocidad instantánea
        specialItem->movetype = MOVETYPE_BOUNCE;
        specialItem->s.effects |= EF_BFG | EF_COLOR_SHELL | EF_BLUEHYPERBLASTER;
        specialItem->s.renderfx |= RF_SHELL_LITE_GREEN;
        specialItem->flags &= ~FL_RESPAWN;

        // Marcar al boss como no atacable para evitar doble manejo
        boss->takedamage = false;
        //    boss->gib_health += -99999;

            // Resetear el modo de monstruos voladores si el jefe corresponde a los tipos específicos
        if (strcmp(boss->classname, "monster_boss2") == 0 ||
            strcmp(boss->classname, "monster_carrier") == 0 ||
            strcmp(boss->classname, "monster_carrier2") == 0 ||
            strcmp(boss->classname, "monster_boss2kl") == 0) {
            flying_monsters_mode = false;
        }
    }
}

void boss_die(edict_t* boss) noexcept {
    if (g_horde->integer && boss->spawnflags.has(SPAWNFLAG_IS_BOSS) && boss->deadflag == true && auto_spawned_bosses.find(boss) != auto_spawned_bosses.end() && !boss->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
        BossDeathHandler(boss);
    }
}

static bool Horde_AllMonstersDead() noexcept {
    for (auto ent : active_or_dead_monsters()) {
        if (ent->monsterinfo.aiflags & AI_DO_NOT_COUNT) continue; // Excluir monstruos con AI_DO_NOT_COUNT
        if (!ent->deadflag && ent->health > 0) {
            return false;
        }
        if (ent->spawnflags.has(SPAWNFLAG_IS_BOSS) && ent->health <= 0) {
            if (auto_spawned_bosses.find(ent) != auto_spawned_bosses.end() && !ent->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
                boss_die(ent);
                ent->spawnflags |= SPAWNFLAG_BOSS_DEATH_HANDLED; // Marcar como manejado
            }
        }
    }
    return true;
}

static void Horde_CleanBodies() noexcept {
    for (auto ent : active_or_dead_monsters()) {
        if ((ent->svflags & SVF_DEADMONSTER) || ent->health <= 0) {
            // Llamar a OnEntityDeath antes de liberar la entidad
            OnEntityDeath(ent);
            if (ent->spawnflags.has(SPAWNFLAG_IS_BOSS) && !ent->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
                boss_die(ent);
            }
            G_FreeEdict(ent); // Libera la entidad
        }
    }
}




// attaching healthbar
void AttachHealthBar(edict_t* boss) noexcept {
    auto healthbar = G_Spawn();
    if (!healthbar) return;

    healthbar->classname = "target_healthbar";
    VectorCopy(boss->s.origin, healthbar->s.origin);
    healthbar->s.origin[2] += 20;
    healthbar->delay = 2.0f;
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

// spawning boss origin
std::unordered_map<std::string, std::array<int, 3>> mapOrigins = {
    {"q2dm1", {1184, 568, 704}},
    {"rdm4", {-336, 2456, -288}},
    {"rdm14", {1248, 664, 896}},
    {"q2dm2", {128, -960, 704}},
    {"q2dm3", {192, -136, 72}},
    {"q2dm4", {504, 876, 292}},
    {"q2dm5", {48, 952, 376}},
    {"q2dm6", {496, 1392, -88}},
    {"q2dm7", {816, 832, 56}},
    {"q2dm8", {112, 1216, 88}},
    {"rboss", {856, -2080, 32}},
    {"ndctf0", {-608, -304, 184}},
    {"q2ctf4", {-2390, 1112, 218}},
    {"q2ctf5", {2432, -960, 168}},
    {"xdm2", {-232, 472, 424}},
    {"xdm6", {-1088, -128, 528}},
    {"rdm6", {712, 1328, 48}},
    {"industry", {-1009, -545, 79}},
    {"mgu3m4", {3312, 3344, 864}},
    {"mgdm1", {176, 64, 288}},
    {"mgu6trial", {-848, 176, 96}},
    {"fact3", {0, -64, 192}},
    {"mgu4trial", {-960, -528, -328}},
    {"mgu6m3", {0, 592, 1600}},
    {"q64/comm", {1464, -88, -432}},
    {"q64/command", {0, -208, 56}},
    {"q64/dm7", {64, 224, 120}},
    {"q64/dm1", {-192, -320, 80}},
    {"q64/dm2", {840, 80, 96}},
    {"q64/dm3", {488, 392, 64}},
    {"q64/dm4", {176,272, -24}},
    {"q64/dm6", {-1568, 1680, 144}},
    {"q64/dm7", {840, 80, 960}},
    {"q64/dm8", {-800, 448, 56}},
    {"q64/dm9", {160, 56, 40}},
    {"q64/dm10", {-304, 512, -92}},
    {"ec/base_ec", {-112, 704, 128}},
    {"old/kmdm3", {-480, -572, 144}},
    {"test/mals_barrier_test", {24, 136, 224}},
    {"test/spbox", {112, 192, 168}}
};

extern void SP_target_earthquake(edict_t* self);
constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_SILENT = 1_spawnflag;
constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_TOGGLE = 2_spawnflag;
[[maybe_unused]] constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_UNKNOWN_ROGUE = 4_spawnflag;
constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_ONE_SHOT = 8_spawnflag;

// Incluye otras cabeceras y definiciones necesarias
static const std::unordered_map<std::string, std::string> bossMessagesMap = {
    {"monster_boss2", "***** A Strogg Boss has spawned! *****\n***** A Hornet descends, ready to add to the body count! *****\n"},
    {"monster_boss2kl", "***** A Strogg Boss has spawned! *****\n***** A Hornet descends, ready to add to the body count! *****\n"},
    {"monster_carrier2", "***** A Strogg Boss has spawned! *****\n***** A Carrier arrives, dropping death like it's hot! *****\n"},
    {"monster_carrier", "***** A Strogg Boss has spawned! *****\n***** A Carrier arrives, dropping death like it's hot! *****\n"},
    {"monster_tank_64", "***** A Strogg Boss has spawned! *****\n***** The ground shakes as the Tank Commander rolls in, ready for some human gibs! *****\n"},
    {"monster_shamblerkl", "***** A Strogg Boss has spawned! *****\n***** The Shambler steps out, eager to paint the town red! *****\n"},
    {"monster_guncmdrkl", "***** A Strogg Boss has spawned! *****\n***** The Gunner Commander marches in, and he's not here to chat! *****\n"},
    {"monster_makronkl", "***** A Strogg Boss has spawned! *****\n***** Makron drops by, craving some fresh carnage! *****\n"},
    {"monster_guardian", "***** A Strogg Boss has spawned! *****\n***** The Guardian shows up, time to meet your maker! *****\n"},
    {"monster_supertank", "***** A Strogg Boss has spawned! *****\n***** A Super-Tank rumbles in, ready to obliterate anything in its path! *****\n"},
    {"monster_boss5", "***** A Strogg Boss has spawned! *****\n***** A Super-Tank rumbles in, ready to obliterate anything in its path! *****\n"},
    {"monster_widow2", "***** A Strogg Boss has spawned! *****\n***** The Widow sneaks in, weaving disruptor shots! *****\n"},
    {"monster_arachnid", "***** A Strogg Boss has spawned! *****\n***** The Arachnid skitters in, itching to fry some flesh! *****\n"},
    {"monster_gm_arachnid", "***** A Strogg Boss has spawned! *****\n***** The Arachnid with missiles emerges, looking to blast you to bits! *****\n"},
    {"monster_jorg", "***** A Strogg Boss has spawned! *****\n***** Jorg enters the fray, prepare for the showdown! *****\n"}
};

void SpawnBossAutomatically() noexcept {
    const auto mapSize = GetMapSize(level.mapname);
    if (g_horde_local.level >= 10 && g_horde_local.level % 5 == 0) {
        const auto it = mapOrigins.find(level.mapname);
        if (it != mapOrigins.end()) {
            auto boss = G_Spawn();
            if (!boss) return;

            const char* desired_boss = G_HordePickBOSS(mapSize, level.mapname, g_horde_local.level);
            if (!desired_boss) return;
            boss->classname = desired_boss;

            // Convertir std::array a vec3_t
            vec3_t origin;
            origin[0] = it->second[0];
            origin[1] = it->second[1];
            origin[2] = it->second[2];
            VectorCopy(origin, boss->s.origin);

            // Realizar la traza para verificar colisiones
            trace_t tr = gi.trace(boss->s.origin, boss->mins, boss->maxs, boss->s.origin, boss, CONTENTS_MONSTER | CONTENTS_PLAYER);
            // if (tr.startsolid || tr.allsolid)
            if (tr.startsolid) {
                // Realizar telefrag si hay colisión
                auto hit = tr.ent;
                if (hit && (hit->svflags & SVF_MONSTER || hit->client)) {
                    T_Damage(hit, boss, boss, vec3_origin, hit->s.origin, vec3_origin, 100000, 0, DAMAGE_NO_PROTECTION, MOD_TELEFRAG_SPAWN);
                    gi.Com_PrintFmt("Telefrag performed on {}\n", hit->classname);
                }
            }

            // Crear el efecto de terremoto
            auto earthquake = G_Spawn();
            earthquake->classname = "target_earthquake";
            earthquake->spawnflags = brandom() ? SPAWNFLAGS_EARTHQUAKE_TOGGLE : SPAWNFLAGS_EARTHQUAKE_ONE_SHOT; // Usar flag de un solo uso para activarlo una vez
            earthquake->speed = (brandom()) ? 300 : 900; // Severidad del terremoto
            earthquake->count = 5; // Duración del terremoto en segundos
            SP_target_earthquake(earthquake);
            earthquake->use(earthquake, boss, boss); // Activar el terremoto

            auto it_msg = bossMessagesMap.find(desired_boss);
            if (it_msg != bossMessagesMap.end()) {
                gi.LocBroadcast_Print(PRINT_CHAT, it_msg->second.c_str());
            }
            else {
                gi.LocBroadcast_Print(PRINT_CHAT, "***** A Strogg Boss has spawned! *****\n***** A Strogg Boss has spawned! *****\n");
            }

            // Asignar flags y configurar el jefe
            const int32_t random_flag = 1 << (std::rand() % 6); // Incluir todas las flags definidas
            boss->monsterinfo.bonus_flags |= random_flag;
            boss->spawnflags |= SPAWNFLAG_IS_BOSS; // Marcar como jefe
            boss->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP; // Establecer la flag de super paso

            // Aplicar flags de bonus y asegurar que el multiplicador de salud se aplica correctamente si la ola es 10 o más
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

            int32_t base_health = static_cast<int32_t>(boss->health * health_multiplier);
            SetMonsterHealth(boss, base_health, current_wave_number); // Pasar current_wave_number

            boss->monsterinfo.power_armor_power = static_cast<int32_t>(boss->monsterinfo.power_armor_power * power_armor_multiplier);
            boss->monsterinfo.power_armor_power *= g_horde_local.level * 1.45;

            // spawngro effect
            vec3_t spawngrow_pos = boss->s.origin;
            const float size = sqrt(spawngrow_pos[0] * spawngrow_pos[0] + spawngrow_pos[1] * spawngrow_pos[1] + spawngrow_pos[2] * spawngrow_pos[2]) * 0.35f;
            const float end_size = sqrt(spawngrow_pos[0] * spawngrow_pos[0] + spawngrow_pos[1] * spawngrow_pos[1] + spawngrow_pos[2] * spawngrow_pos[2]) * 0.005f;

            // Realizar el efecto de crecimiento y aplicar telefrag si es necesario
            SpawnGrow_Spawn(spawngrow_pos, size, end_size);

            // Realizar telefrag en la posición del efecto de spawn
            trace_t tr_spawn = gi.trace(spawngrow_pos, boss->mins, boss->maxs, spawngrow_pos, boss, CONTENTS_MONSTER | CONTENTS_PLAYER);
            if (tr_spawn.startsolid) {
                auto hit = tr_spawn.ent;
                if (hit && (hit->svflags & SVF_MONSTER || hit->client)) {
                    T_Damage(hit, boss, boss, vec3_origin, hit->s.origin, vec3_origin, 100000, 0, DAMAGE_NO_PROTECTION, MOD_TELEFRAG_SPAWN);
                    gi.Com_PrintFmt("Telefrag performed on {} during spawn grow\n", hit->classname);
                }
            }

            ED_CallSpawn(boss);

            AttachHealthBar(boss);

            // Activar el modo de monstruos voladores si corresponde
            if (std::strcmp(boss->classname, "monster_boss2") == 0 ||
                std::strcmp(boss->classname, "monster_carrier") == 0 ||
                std::strcmp(boss->classname, "monster_carrier2") == 0 ||
                std::strcmp(boss->classname, "monster_boss2kl") == 0) {
                flying_monsters_mode = true;  // Activar el modo de monstruos voladores
            }

            boss_spawned_for_wave = true;  // Marcar que el jefe ha sido spawneado para esta ola

            // Agregar el jefe a la lista de jefes generados automáticamente
            auto_spawned_bosses.insert(boss);
        }
    }
}


// reset cooldowns, fixed no monster spawning on next map
void ResetCooldowns() noexcept {
    lastSpawnPointTime.clear();
    lastMonsterSpawnTime.clear();
}

// For resetting bonus 
void ResetBenefits() noexcept {
    shuffled_benefits.clear();
    obtained_benefits.clear();
    vampire_level = 0;
}

void ResetSpawnAttempts() noexcept {
    for (auto& attempt : spawnAttempts) {
        attempt.second = 0;
    }
    for (auto& cooldown : spawnPointCooldowns) {
        cooldown.second = SPAWN_POINT_COOLDOWN.seconds<float>();
    }
}

void ResetAutoSpawnedBosses() noexcept {
    auto_spawned_bosses.clear();

    // Reset recent bosses
    recent_bosses.clear();
}
void ResetGame() noexcept {
    // Reset core gameplay elements
    ResetSpawnAttempts();
    ResetCooldowns();
    ResetBenefits();
    ResetAutoSpawnedBosses();

    // Reset wave information
    current_wave_number = 0;
    g_horde_local.level = 0;  // Reset current wave level
    g_horde_local.state = horde_state_t::warmup;  // Set game state to warmup
    g_horde_local.warm_time = level.time + 4_sec; // Reiniciar el tiempo de warmup
    g_horde_local.monster_spawn_time = level.time; // Reiniciar el tiempo de spawn de monstruos
    next_wave_message_sent = false;
    boss_spawned_for_wave = false;
    allowWaveAdvance = false;
    cachedRemainingMonsters = -1;

    // Reset gameplay configuration variables
    gi.cvar_set("g_chaotic", "0");
    gi.cvar_set("g_insane", "0");
    gi.cvar_set("g_hardcoop", "0");
    gi.cvar_set("dm_monsters", "0");
    gi.cvar_set("timelimit", "40");
    gi.cvar_set("bot_pause", "0");
    gi.cvar_set("set cheats 0 s", "");
    gi.cvar_set("ai_damage_scale", "1");
    gi.cvar_set("ai_allow_dm_spawn", "1");
    gi.cvar_set("g_damage_scale", "1");

    // Reset bonuses
    gi.cvar_set("g_vampire", "0");
    gi.cvar_set("g_startarmor", "0");
    gi.cvar_set("g_ammoregen", "0");
    gi.cvar_set("g_upgradeproxs", "0");
    gi.cvar_set("g_tracedbullets", "0");
    gi.cvar_set("g_bouncygl", "0");
    gi.cvar_set("g_autohaste", "0");


    // Reset spawn cooldowns
    SPAWN_POINT_COOLDOWN = 3.8_sec;

    // Reset the number of monsters to be spawned
    g_horde_local.num_to_spawn = 0;

    // Reset the count of remaining monsters
    cachedRemainingMonsters = -1;
}

// Funci�n para obtener el n�mero de jugadores activos (incluyendo bots)
int32_t GetNumActivePlayers() noexcept {
    int32_t numActivePlayers = 0;
    for (const auto player : active_players()) {
        if (player->client->resp.ctf_team == CTF_TEAM1) {
            numActivePlayers++;
        }
    }
    return numActivePlayers;
}

// Funci�n para obtener el n�mero de jugadores en espectador
int32_t GetNumSpectPlayers() noexcept {
    int32_t numSpectPlayers = 0;
    for (const auto player : active_players()) {
        if (player->client->resp.ctf_team != CTF_TEAM1) {
            numSpectPlayers++;
        }
    }
    return numSpectPlayers;
}

// Estructura para los par�metros de condici�n
struct ConditionParams {
    int32_t maxMonsters;
    gtime_t timeThreshold;
};

// Funci�n para decidir los par�metros de la condici�n en funci�n del tama�o del mapa y el n�mero de jugadores
ConditionParams GetConditionParams(const MapSize& mapSize, int32_t numHumanPlayers) noexcept {
    ConditionParams params = { 0, 0_sec };

    if (mapSize.isBigMap) {
        params.maxMonsters = 19;
        params.timeThreshold = random_time(15_sec, 21_sec);
        return params;
    }

    if (numHumanPlayers >= 3) {
        if (mapSize.isSmallMap) {
            params.maxMonsters = 6;
            params.timeThreshold = random_time(4_sec, 5.5_sec);
        }
        else {
            params.maxMonsters = 12;
            params.timeThreshold = 9_sec;
        }
    }
    else {
        if (mapSize.isSmallMap) {
            if (current_wave_number <= 4) {
                params.maxMonsters = 5;
                params.timeThreshold = 6_sec;
            }
            else {
                params.maxMonsters = 6;
                params.timeThreshold = 6_sec;
            }
        }
        else {
            if (current_wave_number <= 4) {
                params.maxMonsters = 6;
                params.timeThreshold = 9_sec;
            }
            else {
                params.maxMonsters = 10;
                params.timeThreshold = 15_sec;
            }
        }

        if ((g_chaotic->integer && numHumanPlayers <= 3) || (g_insane->integer && numHumanPlayers <= 3)) {
            params.timeThreshold += random_time(4_sec, 6_sec);
        }
    }

    return params;
}
void AllowNextWaveAdvance() noexcept {
    allowWaveAdvance = true;
}

int32_t CalculateRemainingMonsters() noexcept {
    int32_t remaining = 0;
    for (auto ent : active_monsters()) {
        if (!ent->deadflag && !(ent->monsterinfo.aiflags & AI_DO_NOT_COUNT)) {
            ++remaining;
        }
    }
    return remaining;
}

// Calcular los par�metros de la condici�n en funci�n del tama�o del mapa y el n�mero de jugadores
// Variables globales para el estado de la condici�n usando gtime_t
gtime_t condition_start_time = gtime_t::from_sec(0);
int32_t previous_remainingMonsters = 0;

bool CheckRemainingMonstersCondition(const MapSize& mapSize) noexcept {
    if (allowWaveAdvance) {
        allowWaveAdvance = false;
        return true;
    }
    const int32_t numHumanPlayers = GetNumHumanPlayers();
    const ConditionParams params = GetConditionParams(mapSize, numHumanPlayers);

    if (cachedRemainingMonsters == -1) {
        cachedRemainingMonsters = CalculateRemainingMonsters();
    }

    if (cachedRemainingMonsters <= params.maxMonsters) {
        if (!condition_start_time) {
            condition_start_time = level.time;
        }

        if ((level.time - condition_start_time) >= params.timeThreshold) {
            condition_start_time = gtime_t::from_sec(0);
            cachedRemainingMonsters = -1; // Reset cache after condition met
            return true;
        }
    }
    else {
        condition_start_time = gtime_t::from_sec(0);
    }

    return false;
}

static void MonsterDied(edict_t* monster) {
    if (!monster->deadflag && !(monster->monsterinfo.aiflags & AI_DO_NOT_COUNT)) {
        cachedRemainingMonsters--;
    }
}

static void MonsterSpawned(edict_t* monster) {
    if (!monster->deadflag && !(monster->monsterinfo.aiflags & AI_DO_NOT_COUNT)) {
        cachedRemainingMonsters++;
    }
}

// Funci�n para decidir si se usa el spawn m�s lejano basado en el nivel actual
bool UseFarthestSpawn() noexcept {
    if (g_horde_local.level >= 15) {
        return (rand() % 4 == 0);  // 25% de probabilidad a partir del nivel 15
    }
    return false;
}

void PlayWaveStartSound() noexcept {
    static const std::vector<std::string> sounds = {
        "misc/r_tele3.wav",
        "world/klaxon2.wav",
        "misc/tele_up.wav",
        "world/incoming.wav",
        "world/yelforce.wav"
    };

    int32_t sound_index = static_cast<int32_t>(frandom() * sounds.size());
    if (sound_index >= 0 && sound_index < sounds.size()) {
        gi.sound(world, CHAN_VOICE, gi.soundindex(sounds[sound_index].c_str()), 1, ATTN_NONE, 0);
    }
}

// Funci�n para mostrar el mensaje de la ola
void DisplayWaveMessage(gtime_t duration = 5_sec) noexcept {
    if (brandom()) {
        UpdateHordeMessage("\nUse Inventory <KEY> or Use Compass To Open Horde Menu.\n\nMAKE THEM PAY!\n", duration);
    }
    else {
        UpdateHordeMessage("\nWelcome to Hell.\n", duration);
    }
}

// Funci�n para manejar el mensaje de limpieza de ola
void HandleWaveCleanupMessage(const MapSize& mapSize) noexcept {
    if (current_wave_number >= 15 && current_wave_number <= 28) {
        gi.cvar_set("g_insane", "1");
        gi.cvar_set("g_chaotic", "0");
    }
    else if (current_wave_number >= 31) {
        gi.cvar_set("g_insane", "2");
        gi.cvar_set("g_chaotic", "0");
    }
    else if (current_wave_number <= 14) {
        gi.cvar_set("g_chaotic", mapSize.isSmallMap ? "2" : "1");
    }

    g_horde_local.state = horde_state_t::rest;
}
// Vector para almacenar los sonidos, definido como est�tico para que solo se inicialice una vez
static const std::vector<std::string> sounds = {
    "nav_editor/action_fail.wav",
    "makron/roar1.wav",
    "zortemp/ack.wav",
    "misc/spawn1.wav",
    "makron/voice3.wav",
    "world/v_fac3.wav"
    //"world/v_fac2.wav",
    //"insane/insane5.wav",
    //"insane/insane2.wav",
    //"world/won.wav"
};

void HandleWaveRestMessage(gtime_t duration = 4_sec) noexcept {
    if (!g_insane->integer) {
        UpdateHordeMessage("STROGGS STARTING TO PUSH!\n\n", duration);
    }
    else if (g_insane->integer == 1) {
        if (brandom()) 
        UpdateHordeMessage("--STRONGER WAVE INCOMING--\n\n\n", duration);
        else
        UpdateHordeMessage("--STRONGER WAVE INCOMING--\n\nSHOW NO MERCY!\n", duration);
    }
    else if (g_insane->integer == 2) {
        UpdateHordeMessage("***CHAOTIC WAVE INCOMING***\n\nNO RETREAT!\n", duration);
    }

    std::uniform_int_distribution<size_t> dist(0, sounds.size() - 1);
    size_t const sound_index = dist(gen);

    if (sound_index < sounds.size()) {
        gi.sound(world, CHAN_VOICE, gi.soundindex(sounds[sound_index].c_str()), 1, ATTN_NONE, 0);
    }

    for (const auto player : active_players()) {
        if (player->client) {
            player->client->total_damage = 0;
        }
    }
}

void SpawnMonsters() noexcept {
    const auto mapSize = GetMapSize(level.mapname);
    // Calcular la cantidad de monstruos por spawn seg�n el tama�o del mapa y el nivel de la horda
    int32_t monsters_per_spawn;
    if (mapSize.isSmallMap) {
        monsters_per_spawn = (g_horde_local.level >= 5) ? 3 : 2;
    }
    else if (mapSize.isBigMap) {
        monsters_per_spawn = (g_horde_local.level >= 5) ? 5 : 3;
    }
    else { // Mapas medianos (por defecto)
        monsters_per_spawn = (g_horde_local.level >= 5) ? 4 : 3;
    }

    // Verificar que monsters_per_spawn no exceda un valor razonable (por ejemplo, 4)
    if (monsters_per_spawn > 4) {
        monsters_per_spawn = 4;
    }

    float drop_probability = (current_wave_number <= 2) ? 0.8f : (current_wave_number >= 3 && current_wave_number <= 7) ? 0.5f : 0.3f;

    for (int32_t i = 0; i < monsters_per_spawn && g_horde_local.num_to_spawn > 0; ++i) {
        auto spawn_point = SelectDeathmatchSpawnPoint(UseFarthestSpawn(), true, false).spot;
        if (!spawn_point) continue;

        const char* monster_classname = G_HordePickMonster(spawn_point);
        if (!monster_classname) continue;

        auto monster = G_Spawn();
        monster->classname = monster_classname;
        monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
        monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
        if (g_horde_local.level >= 17) {
            if (!st.was_key_specified("power_armor_power"))
                monster->monsterinfo.armor_type = IT_ARMOR_COMBAT;

            if (!st.was_key_specified("power_armor_type")) {
                // Calcular la armadura en funci�n de la salud m�xima del monstruo
                float health_factor = monster->max_health / 100.0f; // Ajusta el denominador seg�n la escala deseada
                int base_armor = 150;
                int additional_armor = static_cast<int>((current_wave_number - 20) * 10 * health_factor);
                monster->monsterinfo.armor_power = base_armor + additional_armor;
            }
        }
        if (frandom() <= drop_probability) {
            monster->item = G_HordePickItem();
        }
        else {
            monster->item = nullptr;
        }

        VectorCopy(spawn_point->s.origin, monster->s.origin);
        VectorCopy(spawn_point->s.angles, monster->s.angles);
        ED_CallSpawn(monster);

        vec3_t spawngrow_pos = monster->s.origin;
        const float size = sqrt(spawngrow_pos[0] * spawngrow_pos[0] + spawngrow_pos[1] * spawngrow_pos[1] + spawngrow_pos[2] * spawngrow_pos[2]) * 0.055f;
        const float endsize = sqrt(spawngrow_pos[0] * spawngrow_pos[0] + spawngrow_pos[1] * spawngrow_pos[1] + spawngrow_pos[2] * spawngrow_pos[2]) * 0.005f;
        SpawnGrow_Spawn(spawngrow_pos, size, endsize);

        --g_horde_local.num_to_spawn;
    }

    // Ajustar el tiempo de spawn para evitar spawns r�pidos basado en el tama�o del mapa
    if (mapSize.isSmallMap) {
        g_horde_local.monster_spawn_time = level.time + 1.5_sec;
    }
    else if (mapSize.isBigMap) {
        g_horde_local.monster_spawn_time = level.time + random_time(0.9_sec, 1.1_sec);
    }
    else {
        g_horde_local.monster_spawn_time = level.time + random_time(1.7_sec, 2_sec);
    }
}

// Funci�n para calcular el jugador con m�s da�o
void CalculateTopDamager(PlayerStats& topDamager, float& percentage) noexcept {
    int total_damage = 0;
    topDamager.total_damage = 0;

    for (const auto& player : active_players()) {
        if (!player->client) continue;
        int player_damage = player->client->total_damage;
        total_damage += player_damage;
        if (player_damage > topDamager.total_damage) {
            topDamager.total_damage = player_damage;
            topDamager.player = player;
        }
    }

    if (total_damage > 0) {
        percentage = (static_cast<float>(topDamager.total_damage) / total_damage) * 100.0f;
    }
    else {
        percentage = 0.0f;
    }

    // Redondear el porcentaje a dos decimales
    percentage = std::round(percentage * 100) / 100;
}

// Funci�n para enviar el mensaje de limpieza
void SendCleanupMessage(const std::unordered_map<std::string, std::string>& messages, const PlayerStats& topDamager, float percentage, gtime_t duration = 5_sec) noexcept {
    std::string playerName = GetPlayerName(topDamager.player);
    auto message = messages.find(playerName);

    if (message != messages.end()) {
        gi.LocBroadcast_Print(PRINT_TYPEWRITER, message->second.c_str(), topDamager.total_damage, percentage);
    }

    // Update the Horde message with the correct duration
    UpdateHordeMessage(fmt::format("Wave Level {} Defeated, GG!\n\n\n{} got the higher DMG this wave with {}. {}%\n",
        g_horde_local.level,
        playerName.c_str(),
        topDamager.total_damage,
        percentage), duration);
}


// Mensajes de limpieza
const std::unordered_map<std::string, std::string> cleanupMessages = {
    {"standard", "Wave Level {} Defeated, GG!\n\n\n{} got the higher DMG this wave with {}. {}%\n"},
    {"chaotic", "Harder Wave Controlled, GG!\n\n\n{} got the higher DMG this wave with {}. {}%\n"},
    {"insane", "Insane Wave Controlled, GG!\n\n\n{} got the higher DMG this wave with {}. {}%\n"}
};


void Horde_RunFrame() noexcept {
    const auto mapSize = GetMapSize(level.mapname);

    if (dm_monsters->integer > 0) {
        g_horde_local.num_to_spawn = dm_monsters->integer;
    }

    const int32_t activeMonsters = level.total_monsters - level.killed_monsters;
    const int32_t maxMonsters = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP : (mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP);

    switch (g_horde_local.state) {
    case horde_state_t::warmup:
        if (g_horde_local.warm_time < level.time + 0.4_sec) {
            cachedRemainingMonsters = CalculateRemainingMonsters();
            g_horde_local.state = horde_state_t::spawning;
            Horde_InitLevel(1);
            current_wave_number = 1;
            PlayWaveStartSound();
            DisplayWaveMessage();
        }
        break;

    case horde_state_t::spawning:
        if (!next_wave_message_sent) {
            next_wave_message_sent = true;
        }

        if (g_horde_local.monster_spawn_time <= level.time) {
            if (g_horde_local.level >= 10 && g_horde_local.level % 5 == 0 && !boss_spawned_for_wave) {
                SpawnBossAutomatically();
                boss_spawned_for_wave = true;
            }

            if (activeMonsters < maxMonsters) {
                SpawnMonsters();
            }

            if (g_horde_local.num_to_spawn == 0) {
                VerifyAndAdjustBots();
                gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nNew Wave Is Here.\nWave Level: {}\n", g_horde_local.level);
                g_horde_local.state = horde_state_t::cleanup;
                g_horde_local.monster_spawn_time = level.time + 1_sec;
            }
        }
        break;

    case horde_state_t::cleanup:
        if (CheckRemainingMonstersCondition(mapSize)) {
            HandleWaveCleanupMessage(mapSize);
        }

        if (g_horde_local.monster_spawn_time < level.time) {
            if (Horde_AllMonstersDead()) {
                g_horde_local.warm_time = level.time + random_time(2.2_sec, 4.5_sec);
                g_horde_local.state = horde_state_t::rest;
                cachedRemainingMonsters = CalculateRemainingMonsters();

                PlayerStats topDamager;
                float percentage = 0.0f;
                CalculateTopDamager(topDamager, percentage);

                std::string messageType = g_insane->integer == 2 ? "insane" : (g_chaotic->integer || g_insane->integer ? "chaotic" : "standard");
                SendCleanupMessage(cleanupMessages, topDamager, percentage, 5_sec); // Passing the duration here
            }
            else {
                cachedRemainingMonsters = CalculateRemainingMonsters();
                g_horde_local.monster_spawn_time = level.time + 3_sec;
            }
        }
        break;

    case horde_state_t::rest:
        if (g_horde_local.warm_time < level.time) {
            if (g_chaotic->integer || g_insane->integer) {
                HandleWaveRestMessage(4_sec);  // Provide duration argument
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

    // Clear the Horde message if the duration has passed
    if (horde_message_end_time && level.time >= horde_message_end_time) {
        ClearHordeMessage();
    }

    // Update the Horde HUD
    UpdateHordeHUD();
}

// Funci�n para manejar el evento de reinicio
void HandleResetEvent() noexcept {
    ResetGame();
}