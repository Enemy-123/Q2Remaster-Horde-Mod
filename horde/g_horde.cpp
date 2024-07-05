// Includes y definiciones relevantes
#include "../g_local.h"
#include "g_horde.h"
#include "../shared.h"
#include <set>
int GetNumActivePlayers() noexcept;
int GetNumSpectPlayers() noexcept;

constexpr int MAX_MONSTERS_BIG_MAP = 27;
constexpr int MAX_MONSTERS_MEDIUM_MAP = 18;
constexpr int MAX_MONSTERS_SMALL_MAP = 14;

bool allowWaveAdvance = false; // Variable global para controlar el avance de la ola
bool boss_spawned_for_wave = false; // Variable de control para el jefe
bool flying_monsters_mode = false; // Variable de control para el jefe volador

int current_wave_number = 1;
int last_wave_number = 0;
static int cachedRemainingMonsters = -1;

gtime_t MONSTER_COOLDOWN = gtime_t::from_sec(2.3); // Cooldown en segundos para los monstruos 2.3
gtime_t SPAWN_POINT_COOLDOWN = gtime_t::from_sec(3.0); // Cooldown en segundos para los puntos de spawn 3.0

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
std::unordered_set<edict_t*> auto_spawned_bosses;
std::unordered_set<std::string> obtained_benefits;
std::unordered_map<std::string, gtime_t> lastMonsterSpawnTime;
std::unordered_map<edict_t*, gtime_t> lastSpawnPointTime;
std::unordered_map<edict_t*, int> spawnAttempts;
std::unordered_map<edict_t*, float> spawnPointCooldowns;

const std::unordered_set<std::string> smallMaps = {
	"q2dm3", "q2dm7", "q2dm2", "q64/dm10", "q64\\dm10",
	"q64/dm9", "q64\\dm9", "q64/dm7", "q64\\dm7", "q64/dm2",
	"q64\\dm2", "q64/dm1", "fact3", "q2ctf4", "rdm4", "q64/command", "q64\\command",
	"mgu3m4", "mgu4trial", "mgu6trial", "ec/base_ec", "mgdm1", "ndctf0"
};

const std::unordered_set<std::string> bigMaps = {
	"q2ctf5", "old/kmdm3", "xdm2", "xdm6"
};

// Función para obtener el tamaño del mapa
MapSize GetMapSize(const std::string& mapname) noexcept {
	MapSize mapSize;
	mapSize.isSmallMap = smallMaps.count(mapname) > 0;
	mapSize.isBigMap = bigMaps.count(mapname) > 0;
	mapSize.isMediumMap = !mapSize.isSmallMap && !mapSize.isBigMap;
	return mapSize;
}

// Definición de la estructura weighted_benefit_t
struct weighted_benefit_t {
	const char* benefit_name;
	int min_level;
	int max_level;
	float weight;
};

// Lista de beneficios ponderados
constexpr weighted_benefit_t benefits[] = {
	{ "vampire", 4, -1, 0.2f },
	{ "ammo regen", 8, -1, 0.15f },
	{ "auto haste", 9, -1, 0.15f },
	{ "vampire upgraded", 24, -1, 0.1f },
	{ "Cluster Prox Grenades", 28, -1, 0.1f },
	{ "start armor", 9, -1, 0.1f },
	{ "Traced-Piercing Bullets", 9, -1, 0.2f }
};

static std::random_device rd;
static std::mt19937 gen(rd());

// Función para mezclar los beneficios
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


// Función para seleccionar un beneficio aleatorio
std::string SelectRandomBenefit(int wave) {
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

	if (total_weight == 0.0f) return "";

	float random_weight = frandom() * total_weight;
	for (const auto& picked_benefit : picked_benefits) {
		if (random_weight < picked_benefit.weight) {
			return picked_benefit.benefit->benefit_name;
		}
	}
	return "";
}

// Función para aplicar un beneficio específico
void ApplyBenefit(const std::string& benefit) {
	if (benefit == "start armor") {
		gi.cvar_set("g_startarmor", "1");
		gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nSTARTING ARMOR\nENABLED!\n");
		gi.LocBroadcast_Print(PRINT_CHAT, "STARTING WITH 50 BODY-ARMOR!\n");
	}
	else if (benefit == "vampire") {
		vampire_level = 1;
		gi.cvar_set("g_vampire", "1");
		gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nYou're covered in blood!\n\n\nVampire Ability\nENABLED!\n");
		gi.LocBroadcast_Print(PRINT_CHAT, "RECOVERING A HEALTH PERCENTAGE OF DAMAGE DONE!\n");
	}
	else if (benefit == "ammo regen") {
		gi.cvar_set("g_ammoregen", "1");
		gi.LocBroadcast_Print(PRINT_TYPEWRITER, "AMMO REGEN\n\nENABLED!\n");
		gi.LocBroadcast_Print(PRINT_CHAT, "AMMO REGEN IS NOW ENABLED!\n");
	}
	else if (benefit == "auto haste") {
		gi.cvar_set("g_autohaste", "1");
		gi.LocBroadcast_Print(PRINT_CENTER, "\n\nDUAL-FIRE IS RUNNING THROUGH YOUR VEINS \nFRAGGING WHILE HASTE\nWILL EXTEND QUAD DMG AND DUAL-FIRE TIME!\n");
		gi.LocBroadcast_Print(PRINT_CHAT, "AUTO-HASTE ENABLED !\n");
	}
	else if (benefit == "vampire upgraded") {
		vampire_level = 2;
		gi.cvar_set("g_vampire", "2");
		gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\nIMPROVED VAMPIRE ABILITY\n");
		gi.LocBroadcast_Print(PRINT_CHAT, "RECOVERING HEALTH & ARMOR NOW!\n");
	}
	else if (benefit == "Cluster Prox Grenades") {
		gi.cvar_set("g_upgradeproxs", "1");
		gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\nIMPROVED PROX GRENADES\n");
	}
	else if (benefit == "Traced-Piercing Bullets") {
		gi.cvar_set("g_tracedbullets", "1");
		gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\nBULLETS\nUPGRADED!\n");
	}
	obtained_benefits.insert(benefit);
}

// Función para verificar y aplicar beneficios basados en la ola
void CheckAndApplyBenefit(int wave) {
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

// Función para ajustar la tasa de aparición de monstruos
void AdjustMonsterSpawnRate() {
	if (g_horde_local.level % 5 == 0) {  // Cada 5 olas, incrementa la dificultad
		g_horde_local.num_to_spawn++;
		g_horde_local.monster_spawn_time -= 0.8_sec;  // Reducir el tiempo de enfriamiento entre spawns
		if (g_horde_local.monster_spawn_time < 0.7_sec) {
			g_horde_local.monster_spawn_time = 0.7_sec;  // No menos de 0.7 segundos
		}

		// Reducir los cooldowns de monstruos y puntos de spawn
		MONSTER_COOLDOWN -= 0.3_sec;
		SPAWN_POINT_COOLDOWN -= 0.3_sec;

		// Asegurarse de que los cooldowns no sean menores a un límite mínimo
		if (MONSTER_COOLDOWN < 1.2_sec) MONSTER_COOLDOWN = 1.2_sec;
		if (SPAWN_POINT_COOLDOWN < 2.3_sec) SPAWN_POINT_COOLDOWN = 2.3_sec;
	}
}

// Función para calcular la cantidad de monstruos estándar a spawnear
void CalculateStandardSpawnCount(const MapSize& mapSize, int32_t lvl) noexcept {
	if (mapSize.isSmallMap) {
		g_horde_local.num_to_spawn = std::min(9 + lvl, MAX_MONSTERS_SMALL_MAP);
	}
	else if (mapSize.isBigMap) {
		g_horde_local.num_to_spawn = std::min(27 + static_cast<int>(lvl * 1.5), MAX_MONSTERS_BIG_MAP);
	}
	else {
		g_horde_local.num_to_spawn = std::min(8 + lvl, MAX_MONSTERS_MEDIUM_MAP);
	}
}

// Función para calcular el bono de locura y caos
int CalculateChaosInsanityBonus(int32_t lvl) noexcept {
	if (g_chaotic->integer == 2 && current_wave_number >= 7) {
		return g_insane->integer ? 6 : 5;
	}
	else if (g_insane->integer) {
		return g_insane->integer == 2 ? 16 : 8;
	}
	return 0;
}

// Función para incluir ajustes de dificultad
void IncludeDifficultyAdjustments(const MapSize& mapSize, int32_t lvl) noexcept {
	int additionalSpawn = 0;
	if (mapSize.isSmallMap) {
		additionalSpawn = 6;
	}
	else if (mapSize.isBigMap) {
		additionalSpawn = 9;
	}
	else {
		additionalSpawn = 6;
	}

	if (current_wave_number > 27) {
		additionalSpawn *= 1.6;
	}

	if (g_chaotic->integer || g_insane->integer) {
		additionalSpawn += CalculateChaosInsanityBonus(lvl);
	}

	g_horde_local.num_to_spawn += additionalSpawn;
}

// Función para determinar la cantidad de monstruos a spawnear
void DetermineMonsterSpawnCount(const MapSize& mapSize, int32_t lvl) noexcept {
	int custom_monster_count = dm_monsters->integer;
	if (custom_monster_count > 0) {
		g_horde_local.num_to_spawn = custom_monster_count;
	}
	else {
		CalculateStandardSpawnCount(mapSize, lvl);
		IncludeDifficultyAdjustments(mapSize, lvl);
	}
}

// Función para inicializar el nivel de la horda
void Horde_InitLevel(int32_t lvl) noexcept {
	current_wave_number++;
	last_wave_number++;
	g_horde_local.level = lvl;
	g_horde_local.monster_spawn_time = level.time;
	flying_monsters_mode = false;
	boss_spawned_for_wave = false;

	// Inicializar cachedRemainingMonsters
	cachedRemainingMonsters = -1;

	auto mapSize = GetMapSize(level.mapname);
	CheckAndApplyBenefit(g_horde_local.level);

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
	DetermineMonsterSpawnCount(mapSize, lvl);

	AdjustMonsterSpawnRate();
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
	{ "item_armor_jacket", -1, 12, 0.12f, adjust_weight_armor },
	{ "item_armor_combat", 13, -1, 0.06f, adjust_weight_armor },
	{ "item_armor_body", 23, -1, 0.015f, adjust_weight_armor },
	{ "item_power_screen", 2, 8, 0.03f, adjust_weight_armor },
	{ "item_power_shield", 14, -1, 0.07f, adjust_weight_armor },

	{ "item_quad", 6, -1, 0.054f, adjust_weight_powerup },
	{ "item_double", 5, -1, 0.07f, adjust_weight_powerup },
	{ "item_quadfire", 3, -1, 0.06f, adjust_weight_powerup },
	{ "item_invulnerability", 4, -1, 0.051f, adjust_weight_powerup },
	{ "item_sphere_defender", 2, -1, 0.06f, adjust_weight_powerup },
	{ "item_sphere_hunter", 9, -1, 0.06f, adjust_weight_powerup },
	{ "item_invisibility", 4, -1, 0.07f, adjust_weight_powerup },
	{ "item_doppleganger", -1, 8, 0.028f, adjust_weight_powerup },
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
	{ "weapon_boomer", 13, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_hyperblaster", 12, -1, 0.22f, adjust_weight_weapon },
	{ "weapon_phalanx", 15, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_rocketlauncher", 14, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_railgun", 22, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_plasmabeam", 17, -1, 0.29f, adjust_weight_weapon },
	{ "weapon_disintegrator", 24, -1, 0.15f, adjust_weight_weapon },
	{ "weapon_bfg", 25, -1, 0.15f, adjust_weight_weapon },


	{ "ammo_trap", 2, -1, 0.14f, adjust_weight_ammo },
	{ "ammo_shells", -1, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_bullets", -1, -1, 0.30f, adjust_weight_ammo },
	{ "ammo_flechettes", 4, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_grenades", -1, -1, 0.35f, adjust_weight_ammo },
	{ "ammo_prox", 12, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_tesla", 2, -1, 0.1f, adjust_weight_ammo },
	{ "ammo_cells", 9, -1, 0.30f, adjust_weight_ammo },
	{ "ammo_magslug", 15, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_slugs", 9, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_disruptor", 14, -1, 0.24f, adjust_weight_ammo },
	{ "ammo_rockets", 7, -1, 0.25f, adjust_weight_ammo },

	{ "item_bandolier", 4, -1, 0.28f, adjust_weight_ammo },
	{ "item_pack", 15, -1, 0.32f, adjust_weight_ammo },
	{ "item_silencer", 15, -1, 0.12f, adjust_weight_ammo },
};

// Definición de monstruos ponderados
constexpr weighted_item_t monsters[] = {
	{ "monster_soldier_light", -1, 19, 0.35f },
	{ "monster_soldier_ss", -1, 20, 0.45f },
	{ "monster_soldier", -1, 2, 0.45f },
	{ "monster_soldier", 3, 7, 0.19f },
	{ "monster_soldier_hypergun", 3, -1, 0.35f },
	{ "monster_soldier_lasergun", 5, -1, 0.45f },
	{ "monster_soldier_ripper", 2, 7, 0.45f },
	{ "monster_infantry2", 2, -1, 0.36f },
	{ "monster_infantry", 9, -1, 0.36f },
	{ "monster_flyer", -1, 2, 0.1f },
	{ "monster_flyer", 3, -1, 0.18f },
	{ "monster_hover2", 8, 19, 0.24f },
	{ "monster_fixbot", 4, 21, 0.11f },
	{ "monster_gekk", -1, 3, 0.1f },
	{ "monster_gekk", 4, 17, 0.17f },
	{ "monster_gunner2", 3, -1, 0.35f },
	{ "monster_gunner", 9, -1, 0.34f },
	{ "monster_brain", 5, 22, 0.2f },
	{ "monster_brain", 23, -1, 0.35f },
	{ "monster_stalker", 2, 3, 0.05f },
	{ "monster_stalker", 4, 13, 0.19f },
	{ "monster_parasite", 4, 17, 0.23f },
	{ "monster_tank", 10, -1, 0.3f },
	{ "monster_tank2", 6, 15, 0.3f },
	{ "monster_guncmdr2", 7, 10, 0.18f },
	{ "monster_mutant", 4, -1, 0.35f },
	{ "monster_redmutant", 6, 12, 0.06f },
	{ "monster_redmutant", 13, -1, 0.35f },
	{ "monster_chick", 6, 18, 0.3f },
	{ "monster_chick_heat", 10, -1, 0.34f },
	{ "monster_berserk", 6, -1, 0.35f },
	{ "monster_floater", 6, -1, 0.26f },
	{ "monster_hover", 15, -1, 0.18f },
	{ "monster_daedalus", 13, -1, 0.13f },
	{ "monster_daedalus2", 16, -1, 0.11f },
	{ "monster_medic", 5, -1, 0.1f },
	{ "monster_medic_commander", 16, -1, 0.06f },
	{ "monster_tank_commander", 11, -1, 0.15f },
	{ "monster_spider", 13, -1, 0.27f },
	{ "monster_guncmdr", 11, -1, 0.28f },
	{ "monster_gladc", 7, -1, 0.3f },
	{ "monster_gladiator", 9, -1, 0.3f },
	{ "monster_shambler", 14, 28, 0.03f },
	{ "monster_shambler", 29, -1, 0.33f },
	{ "monster_floater2", 19, -1, 0.35f },
	{ "monster_tank_64", 22, -1, 0.14f },
	{ "monster_janitor", 16, -1, 0.14f },
	{ "monster_janitor2", 19, -1, 0.12f },
	{ "monster_makron", 18, 19, 0.04f },
	{ "monster_gladb", 16, -1, 0.45f },
	{ "monster_boss2_64", 16, -1, 0.09f },
	{ "monster_carrier2", 19, -1, 0.09f },
	{ "monster_perrokl", 21, -1, 0.33f },
	{ "monster_widow1", 23, -1, 0.08f }
};

// Definición de jefes por tamaño de mapa
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
	{"monster_redmutant", -1, 24, 0.05f},
};

constexpr boss_t BOSS_MEDIUM[] = {
	{"monster_carrier", 24, -1, 0.1f},
	{"monster_boss2", 19, -1, 0.1f},
	{"monster_tank_64", -1, 24, 0.1f},
	{"monster_guardian", -1, 24, 0.1f},
	{"monster_shamblerkl", -1, 24, 0.1f},
	{"monster_guncmdrkl", -1, 24, 0.1f},
	{"monster_makronkl", 36, -1, 0.1f},
};

constexpr boss_t BOSS_LARGE[] = {
	{"monster_carrier", 24, -1, 0.15f},
	{"monster_boss2", 19, -1, 0.15f},
	{"monster_boss5", -1, -1, 0.15f},
	{"monster_tank_64", -1, 24, 0.15f},
	{"monster_guardian", -1, 24, 0.15f},
	{"monster_shamblerkl", -1, 24, 0.15f},
	{"monster_boss5", -1, 24, 0.15f},
	{"monster_makronkl", 36, -1, 0.15f},
	{"monster_jorg", 36, -1, 0.15f},
};

// Función para obtener la lista de jefes basada en el tamaño del mapa
const boss_t* GetBossList(const MapSize& mapSize, const std::string& mapname) noexcept {
	if (mapSize.isSmallMap || mapname == "q2dm4" || mapname == "q64\\comm" || mapname == "q64/comm") return BOSS_SMALL;
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

constexpr int MAX_RECENT_BOSSES = 3;
std::set<const char*> recent_bosses;  // Conjunto de jefes recientes para evitar selecciones repetidas rápidamente.

// Función para seleccionar un jefe basado en el tamaño del mapa y el nombre del mapa
const char* G_HordePickBOSS(const MapSize& mapSize, const std::string& mapname, int waveNumber) noexcept {
	const boss_t* boss_list = GetBossList(mapSize, mapname);
	if (!boss_list) return nullptr;

	std::vector<const boss_t*> eligible_bosses;
	int boss_list_size = mapSize.isSmallMap ? std::size(BOSS_SMALL) :
		mapSize.isMediumMap ? std::size(BOSS_MEDIUM) :
		std::size(BOSS_LARGE);

	for (int i = 0; i < boss_list_size; ++i) {
		const boss_t& boss = boss_list[i];
		if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
			(waveNumber <= boss.max_level || boss.max_level == -1) &&
			recent_bosses.find(boss.classname) == recent_bosses.end()) {
			eligible_bosses.push_back(&boss);
		}
	}

	if (eligible_bosses.empty()) {
		recent_bosses.clear();
		for (int i = 0; i < boss_list_size; ++i) {
			const boss_t& boss = boss_list[i];
			if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
				(waveNumber <= boss.max_level || boss.max_level == -1)) {
				eligible_bosses.push_back(&boss);
			}
		}
	}

	if (!eligible_bosses.empty()) {
		const boss_t* chosen_boss = eligible_bosses[static_cast<int>(frandom() * eligible_bosses.size())];
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

int WAVE_TO_ALLOW_FLYING = 0; // Permitir monstruos voladores a partir de esta oleada

constexpr const char* flying_monster_classnames[] = {
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

// Contar puntos de spawn con style == 1
int countFlyingSpawns() noexcept {
	int count = 0;
	for (size_t i = 0; i < globals.num_edicts; i++) {
		if (g_edicts[i].inuse && strcmp(g_edicts[i].classname, "info_player_deathmatch") == 0 && g_edicts[i].style == 1) {
			count++;
		}
	}
	return count;
}

bool IsFlyingMonster(const char* classname) noexcept {
	for (const char* flying_classname : flying_monster_classnames) {
		if (strcmp(classname, flying_classname) == 0) {
			return true;
		}
	}
	return false;
}

// Ajustar probabilidad de spawn de monstruos voladores
float adjustFlyingSpawnProbability(int flyingSpawns) noexcept {
	// Si hay puntos de spawn voladores, reducir la probabilidad en los normales
	return (flyingSpawns > 0) ? 0.5f : 1.0f;
}

// Verificar elegibilidad del monstruo para aparecer
bool IsMonsterEligible(edict_t* spawn_point, const weighted_item_t& item, bool isFlyingMonster, int currentWave, int flyingSpawns) noexcept {
	// Verificar si el monstruo es volador y si el punto de spawn lo permite
	// Permitir monstruos voladores en puntos de spawn normales si no hay puntos de spawn voladores
	if (spawn_point->style == 1 && !isFlyingMonster) {
		return false;
	}

	// Verificar si el monstruo es elegible para la ola actual
	if (item.min_level > currentWave || (item.max_level != -1 && item.max_level < currentWave)) {
		return false;
	}

	// Verificar si el monstruo es volador y si la ola actual permite voladores
	if (isFlyingMonster && currentWave < WAVE_TO_ALLOW_FLYING) {
		return false;
	}

	return true;
}

// Calcular el peso de un monstruo para aparecer
float CalculateWeight(const weighted_item_t& item, bool isFlyingMonster, float adjustmentFactor) noexcept {
	return item.weight * (isFlyingMonster ? adjustmentFactor : 1.0f);
}

// Reiniciar intentos de spawn
void ResetSpawnAttempts(edict_t* spawn_point) noexcept {
	spawnAttempts[spawn_point] = 0;
	spawnPointCooldowns[spawn_point] = SPAWN_POINT_COOLDOWN.seconds<float>();
}

// Actualizar tiempos de cooldown
void UpdateCooldowns(edict_t* spawn_point, const char* classname) noexcept {
	lastSpawnPointTime[spawn_point] = level.time;
	lastMonsterSpawnTime[classname] = level.time;
	spawnPointCooldowns[spawn_point] = SPAWN_POINT_COOLDOWN.seconds<float>();
}

// Incrementar intentos de spawn
void IncreaseSpawnAttempts(edict_t* spawn_point) noexcept {
	spawnAttempts[spawn_point]++;
	if (spawnAttempts[spawn_point] % 3 == 0) {
		spawnPointCooldowns[spawn_point] *= 0.9f;
	}
}

// Función para seleccionar un monstruo basado en el punto de spawn
const char* G_HordePickMonster(edict_t* spawn_point) noexcept {
	float currentCooldown = SPAWN_POINT_COOLDOWN.seconds<float>();
	if (spawnPointCooldowns.find(spawn_point) != spawnPointCooldowns.end()) {
		currentCooldown = spawnPointCooldowns[spawn_point];
	}

	if (lastSpawnPointTime.find(spawn_point) != lastSpawnPointTime.end() &&
		(level.time - lastSpawnPointTime[spawn_point]).seconds<float>() < currentCooldown) {
		return nullptr;
	}

	std::vector<picked_item_t> picked_monsters;
	float total_weight = 0.0f;
	int flyingSpawns = countFlyingSpawns();
	float adjustmentFactor = adjustFlyingSpawnProbability(flyingSpawns);

	for (const auto& item : monsters) {
		bool isFlyingMonster = IsFlyingMonster(item.classname);

		if (flying_monsters_mode && !isFlyingMonster) {
			continue;
		}

		if (spawn_point->style == 1 && !isFlyingMonster) {
			continue;
		}

		if (!flying_monsters_mode && isFlyingMonster && spawn_point->style != 1 && flyingSpawns > 0) {
			continue;
		}

		if (!IsMonsterEligible(spawn_point, item, isFlyingMonster, g_horde_local.level, flyingSpawns)) {
			continue;
		}

		float weight = item.weight * (isFlyingMonster ? adjustmentFactor : 1.0f);
		if (weight > 0) {
			picked_monsters.push_back({ &item, total_weight += weight });
		}
	}

	if (picked_monsters.empty()) {
		return nullptr;
	}

	float r = frandom() * total_weight;
	for (const auto& monster : picked_monsters) {
		if (r < monster.weight) {
			UpdateCooldowns(spawn_point, monster.item->classname);
			ResetSpawnAttempts(spawn_point);
			return monster.item->classname;
		}
	}

	IncreaseSpawnAttempts(spawn_point);
	return nullptr;
}

void Horde_PreInit() noexcept {
	//wavenext = gi.cvar("wavenext", "0", CVAR_SERVERINFO);
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
	}
}

// Función para obtener el número de jugadores humanos activos (excluyendo bots)
int GetNumHumanPlayers() noexcept {
	int numHumanPlayers = 0;
	for (auto player : active_players()) {
		if (player->client->resp.ctf_team == CTF_TEAM1 && !(player->svflags & SVF_BOT)) {
			numHumanPlayers++;
		}
	}
	return numHumanPlayers;
}

void VerifyAndAdjustBots() noexcept {
	const auto mapSize = GetMapSize(level.mapname);
	const int humanPlayers = GetNumHumanPlayers();
	const int spectPlayers = GetNumSpectPlayers();
	const int baseBots = mapSize.isBigMap ? 6 : 4;

	// Calcular el número requerido de bots
	int requiredBots = baseBots + spectPlayers;

	// Asegurar que el número de bots no sea menor que el valor base
	requiredBots = std::max(requiredBots, baseBots);

	// Establecer el número de bots mínimos necesarios
	gi.cvar_set("bot_minClients", std::to_string(requiredBots).c_str());
}
void Horde_Init() noexcept {
	VerifyAndAdjustBots();

	// Precache all items
	for (auto& item : itemlist) PrecacheItem(&item);

	// Precache monsters
	for (const auto& monster : monsters) {
		edict_t* e = G_Spawn();
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
		"world/yelforce.wav",
		"insane/insane9.wav"
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
		"world/v_fac3.wav",
		"world/v_fac2.wav",
		"insane/insane5.wav",
		"insane/insane2.wav",
		"world/won.wav"
	};

	for (const auto& sound : other_sounds) {
		gi.soundindex(sound.c_str());
	}

	g_horde_local.warm_time = level.time + 4_sec;
}



inline void VectorCopy(const vec3_t& src, vec3_t& dest) noexcept {
	dest[0] = src[0];
	dest[1] = src[1];
	dest[2] = src[2];
}

// Manejador de muerte de jefe
void BossDeathHandler(edict_t* boss) noexcept {
	if (g_horde->integer && boss->spawnflags.has(SPAWNFLAG_IS_BOSS) && !boss->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
		boss->spawnflags |= SPAWNFLAG_BOSS_DEATH_HANDLED; // Marcar como manejado

		std::vector<const char*> itemsToDrop = {
			"item_adrenaline",
			"item_health_large",
			"item_health_mega",
			"item_armor_body"
		};

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
		if (ent->svflags & SVF_DEADMONSTER) {
			if (ent->spawnflags.has(SPAWNFLAG_IS_BOSS) && !ent->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
				boss_die(ent);
			}
			G_FreeEdict(ent);
		}
		else if (ent->health <= 0) {
			G_FreeEdict(ent);
		}
	}
}

// attaching healthbar
void AttachHealthBar(edict_t* boss) noexcept {
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

// spawning boss origin
std::unordered_map<std::string, std::array<int, 3>> mapOrigins = {
	{"q2dm1", {1184, 568, 704}},
	{"rdm4", {-336, 2456, -288}},
	{"rdm14", {1248, 664, 896}},
	{"ec/base_ec", {-112, 704, 128}},
	{"q2dm2", {128, -960, 704}},
	{"q2dm3", {192, -136, 72}},
	{"q2dm4", {504, 876, 292}},
	{"q2dm5", {48, 952, 376}},
	{"q2dm7", {816, 832, 56}},
	{"q2dm8", {112, 1216, 88}},
	{"ndctf0", {-608, -304, 184}},
	{"q2ctf4", {-2390, 1112, 218}},
	{"q2ctf5", {2432, -960, 168}},
	{"old/kmdm3", {-480, -572, 144}},
	{"xdm2", {-232, 472, 424}},
	{"xdm6", {-1088, -128, 528}},
	{"mgu3m4", {3312, 3344, 864}},
	{"mgdm1", {176, 64, 288}},
	{"mgu6trial", {-848, 176, 96}},
	{"fact3", {0, -64, 192}},
	{"mgu6m3", {0, 592, 1600}},
	{"q64/dm7", {64, 224, 120}},
	{"q64/comm", {1464, -88, -432}},
	{"q64/command", {0, -208, 56}},
	{"q64/dm1", {-192, -320, 80}},
	{"q64/dm9", {160, 56, 40}},
	{"q64/dm2", {840, 80, 96}},
	{"q64/dm7", {840, 80, 960}},
	{"q64/dm10", {-304, 512, -92}},
	{"q64/dm3", {488, 392, 64}},
	{"mgu4trial", {-960, -528, -328}}
};

extern void SP_target_earthquake(edict_t* self);
extern constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_ONE_SHOT = 8_spawnflag;

void SpawnBossAutomatically() noexcept {
	const auto mapSize = GetMapSize(level.mapname);
	if (g_horde_local.level >= 9 && g_horde_local.level % 5 == 0) {
		const auto it = mapOrigins.find(level.mapname);
		if (it != mapOrigins.end()) {
			edict_t* boss = G_Spawn();
			if (!boss) return;

			const char* desired_boss = G_HordePickBOSS(mapSize, level.mapname, g_horde_local.level);
			if (!desired_boss) return;
			boss->classname = desired_boss;

			// Crear el efecto de terremoto
			edict_t* earthquake = G_Spawn();
			earthquake->classname = "target_earthquake";
			earthquake->spawnflags = SPAWNFLAGS_EARTHQUAKE_ONE_SHOT; // Usar flag de un solo uso para activarlo una vez
			earthquake->speed = 200; // Severidad del terremoto
			earthquake->count = 5; // Duración del terremoto en segundos
			SP_target_earthquake(earthquake);
			earthquake->use(earthquake, boss, boss); // Activar el terremoto

			// Establecer la posición del jefe
			boss->s.origin[0] = it->second[0];
			boss->s.origin[1] = it->second[1];
			boss->s.origin[2] = it->second[2];

			// Decidir directamente qué mensaje mostrar basado en el classname
			if (strcmp(desired_boss, "monster_boss2") == 0 || strcmp(desired_boss, "monster_boss2kl") == 0) {
				gi.LocBroadcast_Print(PRINT_CHAT, "***** A Hornet arrives, leading a swarming wave! *****\n");
			}
			else if (strcmp(desired_boss, "monster_carrier2") == 0 || strcmp(desired_boss, "monster_carrier") == 0) {
				gi.LocBroadcast_Print(PRINT_CHAT, "***** A Menacing Carrier, leading a swarming wave! *****\n");
			}
			else if (strcmp(desired_boss, "monster_tank_64") == 0) {
				gi.LocBroadcast_Print(PRINT_CHAT, "***** The ground shakes as the Tank Commander arrives! *****\n");
			}
			else if (strcmp(desired_boss, "monster_shamblerkl") == 0) {
				gi.LocBroadcast_Print(PRINT_CHAT, "***** The Shambler emerges from the darkness! *****\n");
			}
			else if (strcmp(desired_boss, "monster_guncmdrkl") == 0) {
				gi.LocBroadcast_Print(PRINT_CHAT, "***** The Gunner Commander is ready for battle! *****\n");
			}
			else if (strcmp(desired_boss, "monster_makronkl") == 0) {
				gi.LocBroadcast_Print(PRINT_CHAT, "***** Makron descends upon the battlefield! *****\n");
			}
			else if (strcmp(desired_boss, "monster_guardian") == 0) {
				gi.LocBroadcast_Print(PRINT_CHAT, "***** The Guardian has arrived, imminent destruction! *****\n");
			}
			else {
				gi.LocBroadcast_Print(PRINT_CHAT, "***** A Strogg Boss has spawned! *****\n");
			}

			// Asignar flags y configurar el jefe
			const int random_flag = 1 << (std::rand() % 6); // Incluir todas las flags definidas
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

			int base_health = static_cast<int>(boss->health * health_multiplier);
			SetMonsterHealth(boss, base_health, current_wave_number); // Pasar current_wave_number

			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * power_armor_multiplier);
			boss->monsterinfo.power_armor_power *= g_horde_local.level * 1.45;

			// spawngro effect
			vec3_t spawngrow_pos = boss->s.origin;
			const float start_size = (sqrt(spawngrow_pos[0] * spawngrow_pos[0] + spawngrow_pos[1] * spawngrow_pos[1] + spawngrow_pos[2] * spawngrow_pos[2])) * 0.45f;
			const float end_size = start_size;
			SpawnGrow_Spawn(spawngrow_pos, start_size, end_size);

			ED_CallSpawn(boss);

			AttachHealthBar(boss);

			// Activar el modo de monstruos voladores si corresponde
			if (strcmp(boss->classname, "monster_boss2") == 0 ||
				strcmp(boss->classname, "monster_carrier") == 0 ||
				strcmp(boss->classname, "monster_carrier2") == 0 ||
				strcmp(boss->classname, "monster_boss2kl") == 0) {
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
}
void ResetGame() noexcept {
	// Reset core gameplay elements
	ResetSpawnAttempts();
	ResetCooldowns();
	ResetBenefits();
	ResetAutoSpawnedBosses();

	// Reset wave information
	current_wave_number = 2;
	g_horde_local.level = 0;  // Reset current wave level
	g_horde_local.state = horde_state_t::warmup;  // Set game state to warmup
	next_wave_message_sent = false;
	boss_spawned_for_wave = false;
	allowWaveAdvance = false;

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
	gi.cvar_set("g_autohaste", "0");

	// Reset spawn cooldowns
	MONSTER_COOLDOWN = 2.3_sec;
	SPAWN_POINT_COOLDOWN = 3.0_sec;

	// Reset the number of monsters to be spawned
	g_horde_local.num_to_spawn = 0;

	// Reset the count of remaining monsters
	cachedRemainingMonsters = -1;
}

// Función para obtener el número de jugadores activos (incluyendo bots)
int GetNumActivePlayers() noexcept {
	int numActivePlayers = 0;
	for (const auto player : active_players()) {
		if (player->client->resp.ctf_team == CTF_TEAM1) {
			numActivePlayers++;
		}
	}
	return numActivePlayers;
}

// Función para obtener el número de jugadores en espectador
int GetNumSpectPlayers() noexcept {
	int numSpectPlayers = 0;
	for (const auto player : active_players()) {
		if (player->client->resp.ctf_team != CTF_TEAM1) {
			numSpectPlayers++;
		}
	}
	return numSpectPlayers;
}

// Estructura para los parámetros de condición
struct ConditionParams {
	int maxMonsters;
	gtime_t timeThreshold;
};


// Calcular los parámetros de la condición en función del tamaño del mapa y el número de jugadores
// Variables globales para el estado de la condición usando gtime_t
gtime_t condition_start_time = gtime_t::from_sec(0);
int previous_remainingMonsters = 0;

// Función para decidir los parámetros de la condición en función del tamaño del mapa y el número de jugadores
ConditionParams GetConditionParams(const MapSize& mapSize, int numHumanPlayers) noexcept {
	ConditionParams params = { 0, 0_sec };

	if (mapSize.isBigMap) {
		params.maxMonsters = 20;
		params.timeThreshold = 16_sec;
		return params;
	}

	if (numHumanPlayers >= 3) {
		if (mapSize.isSmallMap) {
			params.maxMonsters = 7;
			params.timeThreshold = 4_sec;
		}
		else {
			params.maxMonsters = 12;
			params.timeThreshold = 7_sec;
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
				params.timeThreshold = 13_sec;
			}
		}
		else {
			if (current_wave_number <= 4) {
				params.maxMonsters = 4;
				params.timeThreshold = 8_sec;
			}
			else {
				params.maxMonsters = 8;
				params.timeThreshold = 15_sec;
			}
		}

		if ((g_chaotic->integer && numHumanPlayers <= 5) || (g_insane->integer && numHumanPlayers <= 5)) {
			params.timeThreshold += 4_sec;
		}
	}

	return params;
}
void AllowNextWaveAdvance() noexcept {
	allowWaveAdvance = true;
}

int CalculateRemainingMonsters() noexcept {
	int remaining = 0;
	for (auto ent : active_monsters()) {
		if (!ent->deadflag && !(ent->monsterinfo.aiflags & AI_DO_NOT_COUNT)) {
			++remaining;
		}
	}
	return remaining;
}

bool CheckRemainingMonstersCondition(const MapSize& mapSize) noexcept {
	if (allowWaveAdvance) {
		allowWaveAdvance = false;
		return true;
	}
	const int numHumanPlayers = GetNumHumanPlayers();
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

// Función para decidir si se usa el spawn más lejano basado en el nivel actual
bool UseFarthestSpawn() noexcept {
	if (g_horde_local.level >= 15) {
		return (rand() % 4 == 0);  // 25% de probabilidad a partir del nivel 15
	}
	return false;
}

void PlayWaveStartSound() noexcept {
	// Vector para almacenar los sonidos, definido como estático para que solo se inicialice una vez
	static const std::vector<std::string> sounds = {
		"misc/r_tele3.wav",
		"world/klaxon2.wav",
		"misc/tele_up.wav",
		"world/incoming.wav",
		"world/yelforce.wav",
		"insane/insane9.wav",
	};

	// Generar un índice aleatorio para seleccionar un sonido
	int sound_index = static_cast<int>(frandom() * sounds.size());

	// Asegurarse de que el índice esté dentro del rango del vector
	if (sound_index >= 0 && sound_index < sounds.size()) {
		gi.sound(world, CHAN_VOICE, gi.soundindex(sounds[sound_index].c_str()), 1, ATTN_NONE, 0);
	}
}

// Función para mostrar el mensaje de la ola
void DisplayWaveMessage() noexcept {
	if (brandom()) {
		gi.LocBroadcast_Print(PRINT_CENTER, "\nKILL THEM ALL !\n");
	}
	else {
		gi.LocBroadcast_Print(PRINT_CENTER, "\nWelcome to hell.\n");
	}
}

// Función para manejar el mensaje de limpieza de ola
void HandleWaveCleanupMessage(const MapSize& mapSize) noexcept {
	if (current_wave_number >= 15 && current_wave_number <= 28) {
		gi.cvar_set("g_insane", "1");
		gi.cvar_set("g_chaotic", "0");
	}
	else if (current_wave_number >= 31) {
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
}

// Función para manejar el mensaje de descanso de ola
void HandleWaveRestMessage() noexcept {
	if (!g_insane->integer) {
		gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\n\n STROGGS STARTING TO PUSH !\n\n\n ");
	}
	else if (g_insane->integer) {
		gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\n**************\n\n\n--STRONGER WAVE COMING--\n\n\n STROGGS STARTING TO PUSH !\n\n\n **************");
	}

	// Vector para almacenar los sonidos, definido como estático para que solo se inicialice una vez
	static const std::vector<std::string> sounds = {
		"nav_editor/action_fail.wav",
		"makron/roar1.wav",
		"zortemp/ack.wav",
		"misc/spawn1.wav",
		"makron/voice3.wav",
		"world/v_fac3.wav",
		"world/v_fac2.wav",
		"insane/insane5.wav",
		"insane/insane2.wav",
		"world/won.wav"
	};

	float r = frandom();
	size_t sound_index = static_cast<size_t>(r * sounds.size());

	// Asegurarse de que el índice esté dentro del rango del vector
	if (sound_index < sounds.size()) {
		gi.sound(world, CHAN_VOICE, gi.soundindex(sounds[sound_index].c_str()), 1, ATTN_NONE, 0);
	}
}

void SpawnMonsters() noexcept {
	const auto mapSize = GetMapSize(level.mapname);
	// Calcular la cantidad de monstruos por spawn según el tamaño del mapa y el nivel de la horda
	int monsters_per_spawn{};
	if (mapSize.isSmallMap) {
		monsters_per_spawn = (g_horde_local.level >= 5) ? 3 : 2;
	}
	else if (mapSize.isBigMap) {
		monsters_per_spawn = (g_horde_local.level >= 5) ? 4 : 3;
	}
	else { // Mapas medianos (por defecto)
		monsters_per_spawn = (g_horde_local.level >= 5) ? 4 : 2;
	}

	// Verificar que monsters_per_spawn no exceda un valor razonable (por ejemplo, 10)
	if (monsters_per_spawn > 4) {
		monsters_per_spawn = 4;
	}

	int spawned = 0;
	constexpr float drop_probability = 0.6f;

	for (int i = 0; i < monsters_per_spawn && g_horde_local.num_to_spawn > 0; ++i) {
		edict_t* spawn_point = SelectDeathmatchSpawnPoint(UseFarthestSpawn(), true, false).spot;
		if (!spawn_point) continue;

		const char* monster_classname = G_HordePickMonster(spawn_point);
		if (!monster_classname) continue;

		edict_t* monster = G_Spawn();
		monster->classname = monster_classname;
		monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
		monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

		if (g_horde_local.level >= 22) {
			if (!st.was_key_specified("power_armor_power"))
				monster->monsterinfo.armor_type = IT_ARMOR_COMBAT;
			if (!st.was_key_specified("power_armor_type"))
				monster->monsterinfo.armor_power = 175;
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
		const float start_size = (sqrt(spawngrow_pos[0] * spawngrow_pos[0] + spawngrow_pos[1] * spawngrow_pos[1] + spawngrow_pos[2] * spawngrow_pos[2])) * 0.035f;
		const float end_size = start_size;
		SpawnGrow_Spawn(spawngrow_pos, start_size, end_size);

		--g_horde_local.num_to_spawn;
		++spawned;
	}

	// Ajustar el tiempo de spawn para evitar spawns rápidos basado en el tamaño del mapa
	if (mapSize.isSmallMap) {
		g_horde_local.monster_spawn_time = level.time + 2.0_sec;
	}
	else {
		g_horde_local.monster_spawn_time = level.time + 1.3_sec;
	}
}

void Horde_RunFrame() noexcept {
	const auto mapSize = GetMapSize(level.mapname);

	// Comprobar si es necesario ajustar los bots solo en los estados pertinentes
	if (g_horde_local.state == horde_state_t::warmup ||
		g_horde_local.state == horde_state_t::rest) {
		VerifyAndAdjustBots();
	}

	if (dm_monsters->integer > 0) {
		g_horde_local.num_to_spawn = dm_monsters->integer;
	}

	const int activeMonsters = level.total_monsters - level.killed_monsters;
	const int maxMonsters = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
		mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP :
		MAX_MONSTERS_BIG_MAP;

	switch (g_horde_local.state) {
	case horde_state_t::warmup:
		if (g_horde_local.warm_time < level.time + 0.4_sec) {
			cachedRemainingMonsters = CalculateRemainingMonsters();
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(1);
			current_wave_number = 2;
			PlayWaveStartSound();
			DisplayWaveMessage();
		}
		break;

	case horde_state_t::spawning:
		if (!next_wave_message_sent) {
			next_wave_message_sent = true;
		}

		if (g_horde_local.monster_spawn_time <= level.time) {
			if (g_horde_local.level >= 9 && g_horde_local.level % 5 == 0 && !boss_spawned_for_wave) {
				SpawnBossAutomatically();
				boss_spawned_for_wave = true;
			}

			if (activeMonsters < maxMonsters) {
				SpawnMonsters();
			}

			if (g_horde_local.num_to_spawn == 0) {
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
			HandleWaveCleanupMessage(mapSize);
		}

		if (g_horde_local.monster_spawn_time < level.time) {
			if (Horde_AllMonstersDead()) {
				g_horde_local.warm_time = level.time + random_time(2.2_sec, 5_sec);
				g_horde_local.state = horde_state_t::rest;
				cachedRemainingMonsters = CalculateRemainingMonsters();

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
				cachedRemainingMonsters = CalculateRemainingMonsters();
				g_horde_local.monster_spawn_time = level.time + 3_sec;
			}
		}
		break;

	case horde_state_t::rest:
		if (g_horde_local.warm_time < level.time) {
			if (g_chaotic->integer || g_insane->integer) {
				HandleWaveRestMessage();
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

// Función para manejar el evento de reinicio
void HandleResetEvent() noexcept {
	ResetGame();
}
