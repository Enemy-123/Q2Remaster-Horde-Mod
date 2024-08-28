// Includes y definiciones relevantes
#include "../g_local.h"
#include "g_horde.h"
#include "../shared.h"
#include <set>
int GetNumActivePlayers();
int GetNumSpectPlayers();
int GetNumHumanPlayers();

constexpr int32_t MAX_MONSTERS_BIG_MAP = 32;
constexpr int32_t MAX_MONSTERS_MEDIUM_MAP = 18;
constexpr int32_t MAX_MONSTERS_SMALL_MAP = 16;

bool allowWaveAdvance = false; // Variable global para controlar el avance de la ola
bool boss_spawned_for_wave = false; // Variable de control para el jefe
bool flying_monsters_mode = false; // Variable de control para el jefe volador

int32_t last_wave_number = 0;
static int32_t cachedRemainingMonsters = -1;

gtime_t horde_message_end_time = 0_sec;
gtime_t SPAWN_POINT_COOLDOWN = 3.9_sec; // Cooldown en segundos para los puntos de spawn 3.0

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
}g_horde_local;

int32_t current_wave_level = g_horde_local.level;
bool next_wave_message_sent = false;
int32_t vampire_level = 0;

std::vector<std::string> shuffled_benefits;
std::unordered_set<edict_t*> auto_spawned_bosses;
std::unordered_set<std::string> obtained_benefits;
std::unordered_map<std::string, gtime_t> lastMonsterSpawnTime;
std::unordered_map<edict_t*, gtime_t> lastSpawnPointTime;
std::unordered_map<edict_t*, int32_t> spawnAttempts;
std::unordered_map<edict_t*, gtime_t> spawnPointCooldowns;

const std::unordered_set<std::string> smallMaps = {
	"q2dm3", "q2dm7", "q2dm2", "q64/dm10", "test/mals_barrier_test",
	"q64/dm9", "q64/dm7", "q64\\dm7", "q64/dm2", "test/spbox",
	"q64/dm1", "fact3", "q2ctf4", "rdm4", "q64/command","mgu3m4",
	"mgu4trial", "mgu6trial", "ec/base_ec", "mgdm1", "ndctf0", "q64/dm6",
	"q64/dm8", "q64/dm4", "industry"
};

const std::unordered_set<std::string> bigMaps = {
	"q2ctf5", "old/kmdm3", "xdm2", "xdm6", "rdm6", "rdm8", "xdm1", "waste2"
};

// Funci�n para obtener el tama�o del mapa
MapSize GetMapSize(const std::string& mapname) {
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
	{ "Cluster Prox Grenades", 25, -1, 0.2f },
	{ "Napalm-Grenade Launcher", 25, -1, 0.2f },
	{ "BFG Grav-Pull Lasers", 35, -1, 0.2f }
};



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

	const float random_weight = frandom() * total_weight;
	auto it = std::find_if(picked_benefits.begin(), picked_benefits.end(),
		[random_weight](const picked_benefit_t& picked_benefit) {
			return random_weight < picked_benefit.weight;
		});
	return (it != picked_benefits.end()) ? it->benefit->benefit_name : "";
}


#include <string_view>

// Función hash simple para strings en tiempo de compilación
constexpr uint32_t hash(std::string_view str) {
	uint32_t hash = 5381;
	for (char c : str)
		hash = ((hash << 5) + hash) + c;
	return hash;
}

// Función para aplicar un beneficio específico
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
		{"BFG Grav-Pull Lasers", {"\n\n\n\nBFG LASERS UPGRADED!\n", "BFG Grav-Pull Lasers Enabled\n"}}
	};

	const auto it = benefitMessages.find(benefit);
	if (it != benefitMessages.end()) {
		// Aplicar el beneficio
		switch (hash(benefit)) {
		case hash("start armor"):
			gi.cvar_set("g_startarmor", "1");
			break;
		case hash("vampire"):
			vampire_level = 1;
			gi.cvar_set("g_vampire", "1");
			break;
		case hash("vampire upgraded"):
			vampire_level = 2;
			gi.cvar_set("g_vampire", "2");
			break;
		case hash("ammo regen"):
			gi.cvar_set("g_ammoregen", "1");
			break;
		case hash("auto haste"):
			gi.cvar_set("g_autohaste", "1");
			break;
		case hash("Cluster Prox Grenades"):
			gi.cvar_set("g_upgradeproxs", "1");
			break;
		case hash("Traced-Piercing Bullets"):
			gi.cvar_set("g_tracedbullets", "1");
			break;
		case hash("Napalm-Grenade Launcher"):
			gi.cvar_set("g_bouncygl", "1");
			break;
		case hash("BFG Grav-Pull Lasers"):
			gi.cvar_set("g_bfgpull", "1");
			break;
		default:
			gi.Com_PrintFmt("Unknown benefit: {}\n", benefit.c_str());
			return;
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
		// Mensaje de depuración en caso de que el beneficio no sea encontrado
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

// Función para calcular la cantidad de monstruos estándar a spawnear
static void CalculateStandardSpawnCount(const MapSize& mapSize, int32_t lvl) noexcept {
	if (mapSize.isSmallMap) {
		g_horde_local.num_to_spawn = std::min((lvl <= 6) ? 7 : 9 + lvl, MAX_MONSTERS_SMALL_MAP);
	}
	else if (mapSize.isBigMap) {
		g_horde_local.num_to_spawn = std::min((lvl <= 4) ? 24 : 27 + lvl, MAX_MONSTERS_BIG_MAP);
	}
	else { // Medium map
		g_horde_local.num_to_spawn = std::min((lvl <= 4) ? 5 : 8 + lvl, MAX_MONSTERS_MEDIUM_MAP);
	}
}

// Función para calcular el bono de locura y caos
static inline int32_t CalculateChaosInsanityBonus(int32_t lvl) noexcept {
	if (g_chaotic->integer) return (lvl <= 3) ? 6 : 3;
	switch (g_insane->integer) {
	case 2: return 16;
	case 1: return 8;
	default: return 0;
	}
}

// Función para incluir ajustes de dificultad
static void IncludeDifficultyAdjustments(const MapSize& mapSize, int32_t lvl) noexcept {
	int32_t additionalSpawn;
	if (mapSize.isSmallMap) {
		additionalSpawn = (lvl >= 8) ? 8 : 6;
	}
	else if (mapSize.isBigMap) {
		additionalSpawn = (lvl >= 8) ? 12 : 8;
	}
	else { // Medium map
		additionalSpawn = (lvl >= 8) ? 7 : 6;
	}

	if (lvl > 25) {
		additionalSpawn = static_cast<int32_t>(additionalSpawn * 1.6);
	}

	if (lvl >= 3 && (g_chaotic->integer || g_insane->integer)) {
		additionalSpawn += CalculateChaosInsanityBonus(lvl);
	}

	g_horde_local.num_to_spawn += additionalSpawn;
}

// Función para ajustar la tasa de aparición de monstruos
void AdjustMonsterSpawnRate() {
	const auto humanPlayers = GetNumHumanPlayers();
	const float difficultyMultiplier = 1.0f + (humanPlayers - 1) * 0.1f;

	if (g_horde_local.level % 3 == 0) {
		g_horde_local.num_to_spawn = static_cast<int32_t>(g_horde_local.num_to_spawn * difficultyMultiplier);

		const bool isChaoticOrInsane = g_chaotic->integer || g_insane->integer;
		const gtime_t spawnTimeReduction = isChaoticOrInsane ? 0.5_sec : 0.3_sec;
		const gtime_t cooldownReduction = isChaoticOrInsane ? 0.7_sec : 0.4_sec;

		g_horde_local.monster_spawn_time -= spawnTimeReduction * difficultyMultiplier;
		g_horde_local.monster_spawn_time = std::max(g_horde_local.monster_spawn_time, 0.9_sec);

		SPAWN_POINT_COOLDOWN -= cooldownReduction * difficultyMultiplier;
		SPAWN_POINT_COOLDOWN = std::max(SPAWN_POINT_COOLDOWN, 2.0_sec);
	}

	const auto mapSize = GetMapSize(level.mapname);
	// Asegurar que el número de spawn no exceda el máximo permitido para el tamaño del mapa
	const int32_t maxSpawn = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
		(mapSize.isBigMap ? MAX_MONSTERS_BIG_MAP : MAX_MONSTERS_MEDIUM_MAP);
	g_horde_local.num_to_spawn = std::min(g_horde_local.num_to_spawn, maxSpawn);
}

// Función principal para determinar y ajustar el número de monstruos a spawnear
static void DetermineMonsterSpawnCount(const MapSize& mapSize, int32_t lvl) noexcept {
	const int32_t custom_monster_count = dm_monsters->integer;
	if (custom_monster_count > 0) {
		g_horde_local.num_to_spawn = custom_monster_count;
	}
	else {
		CalculateStandardSpawnCount(mapSize, lvl);
		IncludeDifficultyAdjustments(mapSize, lvl);
	}
}

static void Horde_CleanBodies();
void ResetAllSpawnAttempts() noexcept;
void VerifyAndAdjustBots();
void ResetCooldowns() noexcept;

void Horde_InitLevel(const int32_t lvl) {
	last_wave_number++;
	g_horde_local.level = lvl;
	current_wave_level = lvl;
	//g_horde_local.monster_spawn_time = level.time;
	flying_monsters_mode = false;
	boss_spawned_for_wave = false;
	cachedRemainingMonsters = -1;
	VerifyAndAdjustBots();
	// scaling damage switch
	switch (g_horde_local.level) {
	case 17: gi.cvar_set("g_damage_scale", "1.7"); break;
	case 27: gi.cvar_set("g_damage_scale", "2.7"); break;
	case 37: gi.cvar_set("g_damage_scale", "3.7"); break;
	default: break; // Mantener el valor actual si no coincide
	}
	const auto mapSize = GetMapSize(level.mapname);
	DetermineMonsterSpawnCount(mapSize, lvl);
	CheckAndApplyBenefit(g_horde_local.level);
	AdjustMonsterSpawnRate();
	ResetAllSpawnAttempts();
	ResetCooldowns();
	Horde_CleanBodies();
	gi.Com_PrintFmt("Horde level initialized: {}\n", lvl);
}
bool G_IsDeathmatch() noexcept {
	return deathmatch->integer && g_horde->integer;
}

bool G_IsCooperative() noexcept {
	return coop->integer && !g_horde->integer;
}

struct weighted_item_t;
using weight_adjust_func_t = void(*)(const weighted_item_t& item, float& weight);

static void adjust_weight_health(const weighted_item_t& item, float& weight) noexcept {}
static void adjust_weight_weapon(const weighted_item_t& item, float& weight) noexcept {}
static void adjust_weight_ammo(const weighted_item_t& item, float& weight) noexcept {}
static void adjust_weight_armor(const weighted_item_t& item, float& weight) noexcept {}
static void adjust_weight_powerup(const weighted_item_t& item, float& weight) noexcept {}

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
	{ "ammo_cells", 2, 12, 0.12f, adjust_weight_ammo },
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
	{ "monster_soldier", 2, 9, 0.35f },
	{ "monster_soldier_hypergun", -1, -1, 0.35f },
	{ "monster_soldier_lasergun", 5, -1, 0.45f },
	{ "monster_soldier_ripper", 2, 12, 0.45f },
	{ "monster_infantry_vanilla", -1, 1, 0.1f },
	{ "monster_infantry_vanilla", 2, -1, 0.39f },
	{ "monster_infantry", 9, -1, 0.36f },
	{ "monster_flyer", -1, 2, 0.07f },
	{ "monster_flyer", 3, -1, 0.1f },
	{ "monster_hover_vanilla", 6, 19, 0.24f },
	{ "monster_fixbot", 8, 21, 0.11f },
	{ "monster_gekk", -1, 3, 0.1f },
	{ "monster_gekk", 4, 17, 0.17f },
	{ "monster_gunner_vanilla", 5, -1, 0.35f },
	{ "monster_gunner", 14, -1, 0.34f },
	{ "monster_brain", 5, 22, 0.27f },
	{ "monster_brain", 17, -1, 0.39f },
	{ "monster_stalker", 2, 3, 0.1f },
	{ "monster_stalker", 4, 13, 0.19f },
	{ "monster_parasite", 4, 17, 0.23f },
	{ "monster_tank", 12, -1, 0.3f },
	{ "monster_tank_vanilla", 5, 6, 0.15f },
	{ "monster_tank_vanilla", 7, 15, 0.24f },
	{ "monster_runnertank", 10, -1, 0.24f },
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
	{ "monster_daedalus_bomber", 19, -1, 0.14f },
	{ "monster_medic", 5, -1, 0.1f },
	{ "monster_medic_commander", 16, -1, 0.06f },
	{ "monster_tank_commander", 11, -1, 0.15f },
	{ "monster_spider", 11, 19, 0.27f },
	{ "monster_gm_arachnid", 22, -1, 0.34f },
	{ "monster_arachnid", 20, -1, 0.27f },
	{ "monster_guncmdr", 11, -1, 0.28f },
	{ "monster_gladc", 14, -1, 0.3f },
	{ "monster_gladiator", 12, -1, 0.3f },
	{ "monster_shambler", 14, 28, 0.03f },
	{ "monster_shambler", 29, -1, 0.33f },
	{ "monster_floater_tracker", 19, -1, 0.35f },
	{ "monster_tank_64", 24, -1, 0.14f },
	{ "monster_janitor", 16, -1, 0.14f },
	{ "monster_janitor2", 26, -1, 0.12f },
	{ "monster_makron", 17, 22, 0.02f },
	{ "monster_gladb", 18, -1, 0.45f },
	{ "monster_boss2_64", 16, -1, 0.1f },
	{ "monster_carrier_mini", 20, -1, 0.07f },
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
	{"monster_carrier_mini", 24, -1, 0.1f},
	{"monster_boss2kl", 24, -1, 0.1f},
	{"monster_widow2", 19, -1, 0.1f},
	{"monster_tank_64", -1, -1, 0.1f},
	{"monster_shamblerkl", -1, -1, 0.1f},
	{"monster_guncmdrkl", -1, 19, 0.1f},
	{"monster_makronkl", 36, -1, 0.1f},
	{"monster_gm_arachnid", -1, 19, 0.1f},
	{"monster_arachnid", -1, 19, 0.1f},
	{"monster_redmutant", -1, 24, 0.1f}
};

constexpr boss_t BOSS_MEDIUM[] = {
	{"monster_carrier", 24, -1, 0.1f},
	{"monster_boss2", 19, -1, 0.1f},
	{"monster_tank_64", -1, 24, 0.1f},
	{"monster_guardian", -1, 24, 0.1f},
	{"monster_shamblerkl", -1, 24, 0.1f},
	{"monster_guncmdrkl", -1, 24, 0.1f},
	{"monster_widow2", 19, -1, 0.1f},
	{"monster_gm_arachnid", -1, 24, 0.1f},
	{"monster_arachnid", -1, 24, 0.1f},
	{"monster_makronkl", 26, -1, 0.1f}
};

constexpr boss_t BOSS_LARGE[] = {
	{"monster_carrier", 24, -1, 0.1f},
	{"monster_boss2", 19, -1, 0.1f},
	{"monster_boss5", -1, -1, 0.1f},
	{"monster_tank_64", -1, 24, 0.1f},
	{"monster_guardian", -1, 24, 0.1f},
	{"monster_shamblerkl", -1, 24, 0.1f},
	{"monster_boss5", -1, 24, 0.1f},
	{"monster_jorg", 30, -1, 0.1f}
};

// Funci�n para obtener la lista de jefes basada en el tama�o del mapa
const boss_t* GetBossList(const MapSize& mapSize, const std::string& mapname) {
	if (mapSize.isSmallMap || mapname == "q2dm4" || mapname == "q64/comm" || mapname == "test/test_kaiser") {
		return BOSS_SMALL;
	}

	if (mapSize.isMediumMap || mapname == "rdm8" || mapname == "xdm1") {
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

#include <array>
#include <unordered_set>
#include <random>

constexpr int32_t MAX_RECENT_BOSSES = 3;
std::vector<const char*> recent_bosses;  // Cambiado de std::deque a std::vector

const char* G_HordePickBOSS(const MapSize& mapSize, const std::string& mapname, int32_t waveNumber) {
	const boss_t* boss_list = GetBossList(mapSize, mapname);
	if (!boss_list) return nullptr;

	const size_t boss_list_size = mapSize.isSmallMap ? std::size(BOSS_SMALL) :
		mapSize.isMediumMap ? std::size(BOSS_MEDIUM) :
		std::size(BOSS_LARGE);

	std::vector<const boss_t*> eligible_bosses;
	eligible_bosses.reserve(boss_list_size);

	auto is_boss_eligible = [waveNumber](const boss_t& boss) {
		return (waveNumber >= boss.min_level || boss.min_level == -1) &&
			(waveNumber <= boss.max_level || boss.max_level == -1);
		};

	auto is_boss_recent = [&](const char* classname) {
		return std::find(recent_bosses.begin(), recent_bosses.end(), classname) != recent_bosses.end();
		};

	for (size_t i = 0; i < boss_list_size; ++i) {
		if (is_boss_eligible(boss_list[i]) && !is_boss_recent(boss_list[i].classname)) {
			eligible_bosses.push_back(&boss_list[i]);
		}
	}

	if (eligible_bosses.empty()) {
		recent_bosses.clear();
		for (size_t i = 0; i < boss_list_size; ++i) {
			if (is_boss_eligible(boss_list[i])) {
				eligible_bosses.push_back(&boss_list[i]);
			}
		}
	}

	if (!eligible_bosses.empty()) {
		const boss_t* chosen_boss = eligible_bosses[static_cast<size_t>(frandom() * eligible_bosses.size())];
		recent_bosses.push_back(chosen_boss->classname);
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

gitem_t* G_HordePickItem() {
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

	if (picked_items.empty()) return nullptr;

	const float random_weight = frandom() * total_weight;
	auto it = std::lower_bound(picked_items.begin(), picked_items.end(), random_weight,
		[](const picked_item_t& item, float value) { return item.weight < value; });

	return (it != picked_items.end()) ? FindItemByClassname(it->item->classname) : nullptr;
}

int32_t WAVE_TO_ALLOW_FLYING;
extern bool flying_monsters_mode;
extern gtime_t SPAWN_POINT_COOLDOWN;

// Keep the existing array
constexpr std::array<const char*, 10> flying_monster_classnames = {
	"monster_boss2_64",
	"monster_carrier_mini",
	"monster_floater",
	"monster_floater_tracker",
	"monster_flyer",
	"monster_fixbot",
	"monster_hover",
	"monster_hover_vanilla",
	"monster_daedalus",
	"monster_daedalus_bomber"
};

// Create a static set for faster lookup
static const std::unordered_set<std::string> flying_monsters_set(
	flying_monster_classnames.begin(), flying_monster_classnames.end());

static int32_t countFlyingSpawns() noexcept {
	int32_t count = 0;
	for (size_t i = 0; i < globals.num_edicts; i++) {
		const auto& ent = g_edicts[i];
		if (ent.inuse && strcmp(ent.classname, "info_player_deathmatch") == 0 && ent.style == 1) {
			count++;
		}
	}
	return count;
}

inline bool IsFlyingMonster(const char* classname) {
	return flying_monsters_set.find(classname) != flying_monsters_set.end();
}

constexpr float adjustFlyingSpawnProbability(int32_t flyingSpawns) {
	return (flyingSpawns > 0) ? 0.25f : 1.0f;
}

bool IsMonsterEligible(const edict_t* spawn_point, const weighted_item_t& item, bool isFlyingMonster, int32_t currentWave, int32_t flyingSpawns) noexcept {
	return !(spawn_point->style == 1 && !isFlyingMonster) &&
		!(item.min_level > currentWave || (item.max_level != -1 && item.max_level < currentWave)) &&
		!(isFlyingMonster && currentWave < WAVE_TO_ALLOW_FLYING);
}

float CalculateWeight(const weighted_item_t& item, bool isFlyingMonster, float adjustmentFactor) noexcept {
	return item.weight * (isFlyingMonster ? adjustmentFactor : 1.0f);
}

void ResetSingleSpawnPointAttempts(edict_t* spawn_point) noexcept {
	spawnAttempts[spawn_point] = 0;
	spawnPointCooldowns[spawn_point] = level.time;
}

void UpdateCooldowns(edict_t* spawn_point, const char* classname) {
	lastSpawnPointTime[spawn_point] = level.time;
	lastMonsterSpawnTime[classname] = level.time;
	spawnPointCooldowns[spawn_point] = level.time;
}

void IncreaseSpawnAttempts(edict_t* spawn_point) {
	spawnAttempts[spawn_point]++;
	if (spawnAttempts[spawn_point] % 3 == 0) {
		spawnPointCooldowns[spawn_point] = gtime_t::from_sec(spawnPointCooldowns[spawn_point].seconds() * 0.9f);
	}
}


// Estructura para pasar datos adicionales a la función de filtro
struct FilterData {
	const edict_t* ignore_ent;
	int count;
};

// Función de filtro optimizada
static BoxEdictsResult_t SpawnPointFilter(edict_t* ent, void* data) {
	FilterData* filter_data = static_cast<FilterData*>(data);

	// Ignorar la entidad especificada (si existe)
	if (ent == filter_data->ignore_ent) {
		return BoxEdictsResult_t::Skip;
	}

	// Filtrar solo jugadores y bots
	for (auto const* player : active_players_no_spect()) {
		if (player) filter_data->count++;
		// Si encontramos al menos una entidad, podemos detener la búsqueda
		return BoxEdictsResult_t::End;
	}
	return BoxEdictsResult_t::Skip;
}

// is spawn occupied?
static bool IsSpawnPointOccupied(const edict_t* spawn_point, const edict_t* ignore_ent = nullptr) {
	vec3_t mins, maxs;
	VectorAdd(spawn_point->s.origin, vec3_t{ -16, -16, -24 }, mins);
	VectorAdd(spawn_point->s.origin, vec3_t{ 16, 16, 32 }, maxs);

	FilterData filter_data = { ignore_ent, 0 };

	// Usar BoxEdicts para verificar si hay entidades relevantes en el área
	gi.BoxEdicts(mins, maxs, nullptr, 0, AREA_SOLID, SpawnPointFilter, &filter_data);

	return filter_data.count > 0;
}

#include <vector>

const char* G_HordePickMonster(edict_t* spawn_point) {
	// Check if the spawn point is occupied
	if (IsSpawnPointOccupied(spawn_point)) {
		return nullptr;
	}

	// Check cooldowns
	gtime_t currentCooldown = SPAWN_POINT_COOLDOWN;
	if (const auto it = spawnPointCooldowns.find(spawn_point); it != spawnPointCooldowns.end()) {
		currentCooldown = it->second;
	}

	if (const auto it = lastSpawnPointTime.find(spawn_point); it != lastSpawnPointTime.end()) {
		if (level.time < it->second + SPAWN_POINT_COOLDOWN) {
			return nullptr;  // aún en cooldown, no permitir spawn
		}
	}

	// Calculate flying spawn adjustment
	const int32_t flyingSpawns = countFlyingSpawns();
	const float adjustmentFactor = adjustFlyingSpawnProbability(flyingSpawns);

	// Use a static vector to avoid repeated allocations
	static std::vector<std::pair<const weighted_item_t*, float>> eligible_monsters;
	eligible_monsters.clear();
	float total_weight = 0.0f;

	for (const auto& item : monsters) {
		const bool isFlyingMonster = IsFlyingMonster(item.classname);

		if (IsMonsterEligible(spawn_point, item, isFlyingMonster, g_horde_local.level, flyingSpawns)) {
			const float weight = CalculateWeight(item, isFlyingMonster, adjustmentFactor);
			if (weight > 0) {
				total_weight += weight;
				eligible_monsters.emplace_back(&item, weight);  // Store individual weights, not cumulative
			}
		}
	}

	if (eligible_monsters.empty()) {
		IncreaseSpawnAttempts(spawn_point);
		return nullptr;
	}

	// Create a vector of weights for std::discrete_distribution
	std::vector<float> weights;
	weights.reserve(eligible_monsters.size());
	for (const auto& monster : eligible_monsters) {
		weights.push_back(monster.second);
	}

	// Create and use the discrete distribution
	std::discrete_distribution<> dist(weights.begin(), weights.end());
	const size_t chosen_index = dist(mt_rand);

	const char* chosen_monster = eligible_monsters[chosen_index].first->classname;
	UpdateCooldowns(spawn_point, chosen_monster);
	ResetSingleSpawnPointAttempts(spawn_point);
	return chosen_monster;
}

void Horde_PreInit() {
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
		gi.cvar_set("g_bfgpull", "0");
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
inline int32_t GetNumHumanPlayers() {
	int32_t numHumanPlayers = 0;
	for (auto const* player : active_players()) {
		if (player->client->resp.ctf_team == CTF_TEAM1 && !(player->svflags & SVF_BOT)) {
			numHumanPlayers++;
		}
	}
	return numHumanPlayers;
}

void VerifyAndAdjustBots() {
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


#include <array>
#include <string_view>
#include <chrono>
void InitializeWaveSystem() noexcept;
void Horde_Init() {
	// Precache all items
	for (auto& item : itemlist) {
		PrecacheItem(&item);
	}

	// Precache monsters
	for (const auto& monster : monsters) {
		edict_t* e = G_Spawn();
		if (!e) {
			gi.Com_Print("Error: Failed to spawn monster for precaching.\n");
			continue;
		}
		e->classname = monster.classname;
		e->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
		ED_CallSpawn(e);
		G_FreeEdict(e);
	}

	// Initialize wave system (includes precaching of wave sounds)
	InitializeWaveSystem();

	// Precache items and bosses
	const auto precacheEntity = [](const auto& entity) {
		if (entity.classname) {
			PrecacheItem(FindItemByClassname(entity.classname));
		}
		else {
			gi.Com_Print("Error: Invalid entity classname for precaching.\n");
		}
		};

	for (const auto& item : items) precacheEntity(item);
	for (const auto& boss : BOSS_SMALL) precacheEntity(boss);
	for (const auto& boss : BOSS_MEDIUM) precacheEntity(boss);
	for (const auto& boss : BOSS_LARGE) precacheEntity(boss);

	// Precache additional sounds
	constexpr std::array<const char*, 5> additional_sounds = {
		 "misc/r_tele3.wav",
		 "world/klaxon2.wav",
		 "misc/tele_up.wav",
		 "world/incoming.wav",
		 "world/yelforce.wav"
	};

	for (const auto& sound : additional_sounds) {
		gi.soundindex(sound);
	}

	ResetGame();
}

// Constantes para mejorar la legibilidad y mantenibilidad
constexpr int MIN_VELOCITY = -200;
constexpr int MAX_VELOCITY = 200;
constexpr int MIN_VERTICAL_VELOCITY = 650;
constexpr int MAX_VERTICAL_VELOCITY = 800;
constexpr int VERTICAL_VELOCITY_RANDOM_RANGE = 200;

// Función auxiliar para generar velocidad aleatoria
vec3_t GenerateRandomVelocity(int minHorizontal, int maxHorizontal, int minVertical, int maxVertical) {
	std::uniform_int_distribution<> horizontalDis(minHorizontal, maxHorizontal);
	std::uniform_int_distribution<> verticalDis(minVertical, maxVertical);

	return {
		static_cast<float>(horizontalDis(mt_rand)),
		static_cast<float>(horizontalDis(mt_rand)),
		static_cast<float>(verticalDis(mt_rand) + std::uniform_int_distribution<>(0, VERTICAL_VELOCITY_RANDOM_RANGE)(mt_rand))
	};
}

// Función auxiliar para configurar un ítem soltado
void SetupDroppedItem(edict_t* item, const vec3_t& origin, const vec3_t& velocity, bool applyFlags) {
	VectorCopy(origin, item->s.origin);
	VectorCopy(velocity, item->velocity);
	if (applyFlags) {
		item->spawnflags &= ~SPAWNFLAG_ITEM_DROPPED;
		item->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
	}
	item->movetype = MOVETYPE_BOUNCE;
	item->s.effects |= EF_GIB;
	item->flags &= ~FL_RESPAWN;
}

void BossDeathHandler(edict_t* boss) {
	if (!g_horde->integer || !boss->spawnflags.has(SPAWNFLAG_IS_BOSS) || boss->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
		return;
	}
	OnEntityDeath(boss);
	OnEntityRemoved(boss); // Añadido para liberar el configstring
	boss->spawnflags |= SPAWNFLAG_BOSS_DEATH_HANDLED;

	const std::array<const char*, 6> itemsToDrop = {
		"item_adrenaline", "item_pack", "item_doppleganger",
		"item_sphere_defender", "item_armor_combat", "item_bandolier"
	};

	// Soltar ítem especial (quad o quadfire)
	const char* specialItemName = (rand() % 2 == 0) ? "item_quad" : "item_quadfire";
	edict_t* specialItem = Drop_Item(boss, FindItemByClassname(specialItemName));

	const vec3_t specialVelocity = GenerateRandomVelocity(MIN_VELOCITY, MAX_VELOCITY, 300, 400);
	SetupDroppedItem(specialItem, boss->s.origin, specialVelocity, false);

	// Configuración adicional para el ítem especial
	specialItem->s.effects |= EF_BFG | EF_COLOR_SHELL | EF_BLUEHYPERBLASTER;
	specialItem->s.renderfx |= RF_SHELL_LITE_GREEN;

	// Soltar los demás ítems
	std::vector<const char*> shuffledItems(itemsToDrop.begin(), itemsToDrop.end());
	std::shuffle(shuffledItems.begin(), shuffledItems.end(), std::mt19937(std::random_device()()));

	for (const auto& itemClassname : shuffledItems) {
		edict_t* droppedItem = Drop_Item(boss, FindItemByClassname(itemClassname));
		const vec3_t itemVelocity = GenerateRandomVelocity(MIN_VELOCITY, MAX_VELOCITY, MIN_VERTICAL_VELOCITY, MAX_VERTICAL_VELOCITY);
		SetupDroppedItem(droppedItem, boss->s.origin, itemVelocity, true);
	}

	// Marcar al boss como no atacable para evitar doble manejo
	boss->takedamage = false;

	// Resetear el modo de monstruos voladores si el jefe corresponde a los tipos específicos
	const std::array<const char*, 4> flyingBosses = {
		"monster_boss2", "monster_carrier", "monster_carrier_mini", "monster_boss2kl"
	};
	if (std::find(flyingBosses.begin(), flyingBosses.end(), boss->classname) != flyingBosses.end()) {
		flying_monsters_mode = false;
	}
}

void boss_die(edict_t* boss) {
	if (g_horde->integer && boss->spawnflags.has(SPAWNFLAG_IS_BOSS) && boss->health <= 0 &&
		auto_spawned_bosses.find(boss) != auto_spawned_bosses.end() && !boss->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
		BossDeathHandler(boss);

		// Limpiar la barra de salud
		for (size_t i = 0; i < MAX_HEALTH_BARS; ++i) {
			if (level.health_bar_entities[i] && level.health_bar_entities[i]->enemy == boss) {
				G_FreeEdict(level.health_bar_entities[i]);
				level.health_bar_entities[i] = nullptr;
				break;
			}
		}

		// Limpiar el configstring del nombre de la barra de salud
		gi.configstring(CONFIG_HEALTH_BAR_NAME, "");
	}
}
static bool Horde_AllMonstersDead() {
	for (auto ent : active_or_dead_monsters()) {
		if (ent->monsterinfo.aiflags & AI_DO_NOT_COUNT) continue; // Excluir monstruos con AI_DO_NOT_COUNT
		if (!ent->deadflag && ent->health > 0) {
			return false;
		}
		if (ent->spawnflags.has(SPAWNFLAG_IS_BOSS) && ent->health <= 0) {
			if (auto_spawned_bosses.find(ent) != auto_spawned_bosses.end() && !ent->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
				boss_die(ent);
				// No es necesario llamar a OnEntityRemoved aquí, ya que BossDeathHandler ya lo hace
			}
		}
	}
	return true;
}

static void Horde_CleanBodies() {
	int32_t cleaned_count = 0;
	for (auto ent : active_or_dead_monsters()) {
		if ((ent->svflags & SVF_DEADMONSTER) || ent->health <= 0) {
			if (ent->spawnflags.has(SPAWNFLAG_IS_BOSS) && !ent->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
				boss_die(ent);
			}
			else {
				OnEntityDeath(ent);
			}
			G_FreeEdict(ent);
			cleaned_count++;
		}
	}
	if (cleaned_count > 0) {
		gi.Com_PrintFmt("Cleaned {} monster bodies\n", cleaned_count);
	}
}

// spawning boss origin
std::unordered_map<std::string, std::array<int, 3>> mapOrigins = {
	{"q2dm1", {1184, 568, 704}},
	{"rdm4", {-336, 2456, -288}},
	{"rdm8", {-1516, 976, -156}},
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
	{"xdm1", {-312, 600, 144}},
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
	{"waste2", {-1152, -288, -40}},
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
	{"test/spbox", {112, 192, 168}},
	{"test/test_kaiser", {1344, 176, -8}}
};


// Incluye otras cabeceras y definiciones necesarias
static const std::unordered_map<std::string, std::string> bossMessagesMap = {
	{"monster_boss2", "***** A Strogg Boss has spawned! *****\n***** A Hornet descends, ready to add to the body count! *****\n"},
	{"monster_boss2kl", "***** A Strogg Boss has spawned! *****\n***** A Hornet descends, ready to add to the body count! *****\n"},
	{"monster_carrier_mini", "***** A Strogg Boss has spawned! *****\n***** A Carrier arrives, dropping death like it's hot! *****\n"},
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

void SetHealthBarName(edict_t* boss);

// attaching healthbar
void AttachHealthBar(edict_t* boss) {
	auto healthbar = G_Spawn();
	if (!healthbar) return;

	healthbar->classname = "target_healthbar";
	VectorCopy(boss->s.origin, healthbar->s.origin);
	healthbar->s.origin[2] += 20;
	healthbar->delay = 2.0f;
	healthbar->target = boss->targetname;

	// Copiar el nombre del jefe correctamente
	std::string boss_name = GetDisplayName(boss);
	healthbar->message = G_CopyString(boss_name.c_str(), TAG_LEVEL);

	SP_target_healthbar(healthbar);
	healthbar->enemy = boss;

	// Llamar a SetHealthBarName después de configurar el mensaje
	SetHealthBarName(boss);

	for (size_t i = 0; i < MAX_HEALTH_BARS; ++i) {
		if (!level.health_bar_entities[i]) {
			level.health_bar_entities[i] = healthbar;
			break;
		}
	}

	healthbar->think = check_target_healthbar;
	healthbar->nextthink = level.time + 20_sec;
}

static int boss_counter = 0; // Declaramos boss_counter como variable estática
void BossSpawnThink(edict_t* self); // Forward declaration of the think function

void SpawnBossAutomatically() {
	const auto mapSize = GetMapSize(level.mapname);
	if (g_horde_local.level < 10 || g_horde_local.level % 5 != 0) {
		return;
	}

	const auto it = mapOrigins.find(level.mapname);
	if (it == mapOrigins.end()) {
		gi.Com_PrintFmt("Error: No spawn origin found for map {}\n", level.mapname);
		return;
	}

	edict_t* boss = G_Spawn();
	if (!boss) {
		gi.Com_PrintFmt("Error: Failed to spawn boss entity\n");
		return;
	}

	const char* desired_boss = G_HordePickBOSS(mapSize, level.mapname, g_horde_local.level);
	if (!desired_boss) {
		G_FreeEdict(boss);
		gi.Com_PrintFmt("Error: Failed to pick a boss type\n");
		return;
	}

	boss_spawned_for_wave = true;
	boss->classname = desired_boss;

	// Set boss origin
	if (it->second.size() < 3) {
		G_FreeEdict(boss);
		gi.Com_PrintFmt("Error: Invalid spawn origin for map {}\n", level.mapname);
		return;
	}
	VectorCopy(vec3_t{ static_cast<float>(it->second[0]),
					   static_cast<float>(it->second[1]),
					   static_cast<float>(it->second[2]) },
		boss->s.origin);

	gi.Com_PrintFmt("Preparing to spawn boss at position: {}\n", boss->s.origin);

	// Push entities away
	PushEntitiesAway(boss->s.origin, 2, 500, 300.0f, 750.0f, 1000.0f, 500.0f);

	// Delay boss spawn
	boss->nextthink = level.time + 800_ms; // 0.8 seconds delay
	boss->think = BossSpawnThink;

	gi.Com_PrintFmt("Boss spawn preparation complete. Boss will appear in 1 second.\n");
}

THINK(BossSpawnThink) (edict_t* self) -> void
{
	// Boss spawn message
	const auto it_msg = bossMessagesMap.find(self->classname);
	if (it_msg != bossMessagesMap.end()) {
		const char* message = it_msg->second.c_str();
		gi.LocBroadcast_Print(PRINT_CHAT, "\n\n\n{}\n", message);
	}
	else {
		gi.Com_PrintFmt("Warning: No specific message found for boss type '{}'. Using default message.\n", self->classname);
		gi.LocBroadcast_Print(PRINT_CHAT, "\n\n\nA Strogg Boss has spawned!\nPrepare for battle!\n");
	}

	// Configure boss
	self->spawnflags |= SPAWNFLAG_IS_BOSS | SPAWNFLAG_MONSTER_SUPER_STEP;
	self->monsterinfo.last_sentrygun_target_time = 0_ms;

	ED_CallSpawn(self);

	const float health_multiplier = 1.0f;
	const float power_armor_multiplier = 1.0f;
	const auto mapSize = GetMapSize(level.mapname);


	// Apply bonus flags and effects
	ApplyBossEffects(self, mapSize.isSmallMap, mapSize.isMediumMap, mapSize.isBigMap);

	self->monsterinfo.attack_state = AS_BLIND;

	// Adjust final power armor
	if (self->monsterinfo.power_armor_power > 0)
	{
		self->monsterinfo.power_armor_power = static_cast<int32_t>(self->monsterinfo.power_armor_power);
	}

	// Spawn grow effect
	vec3_t const spawngrow_pos = self->s.origin;
	const float size = VectorLength(spawngrow_pos) * 0.35f;
	const float end_size = size * 0.005f;
	ImprovedSpawnGrow(spawngrow_pos, size, end_size, self);
	SpawnGrow_Spawn(spawngrow_pos, size, end_size);

	// Attach health bar and set its name after all health adjustments
	AttachHealthBar(self);
	SetHealthBarName(self);

	// Set flying monsters mode if applicable
	if (self->classname && (
		strcmp(self->classname, "monster_boss2") == 0 ||
		strcmp(self->classname, "monster_carrier") == 0 ||
		strcmp(self->classname, "monster_carrier_mini") == 0 ||
		strcmp(self->classname, "monster_boss2kl") == 0)) {
		flying_monsters_mode = true;
	}

	auto_spawned_bosses.insert(self);
	gi.Com_PrintFmt("Boss of type {} spawned successfully with {} health and {} power armor\n",
		self->classname, self->health, self->monsterinfo.power_armor_power);
}

// En SetHealthBarName
void SetHealthBarName(edict_t* boss)
{
	std::string full_display_name = GetDisplayName(boss);
	gi.configstring(CONFIG_HEALTH_BAR_NAME, full_display_name.c_str());

	// Preparar el mensaje para multicast
	gi.WriteByte(svc_configstring);
	gi.WriteShort(CONFIG_HEALTH_BAR_NAME);
	gi.WriteString(full_display_name.c_str());

	// Usar multicast para enviar la actualización a todos los clientes de manera confiable
	gi.multicast(vec3_origin, MULTICAST_ALL, true);
}

//CS HORDE

void UpdateHordeHUD() {
	for (auto player : active_players()) {
		if (player->inuse && player->client) {
			if (!player->client->voted_map[0]) {
				player->client->ps.stats[STAT_HORDEMSG] = CONFIG_HORDEMSG;
			}
			else {
				player->client->ps.stats[STAT_HORDEMSG] = 0;
			}
		}
	}
}

// Declaración de UpdateHordeMessage
void UpdateHordeMessage(std::string_view message, gtime_t duration);

// Implementación de UpdateHordeMessage
void UpdateHordeMessage(std::string_view message, gtime_t duration = 5_sec) {
	std::string fullMessage(message);


	gi.configstring(CONFIG_HORDEMSG, fullMessage.c_str());
	horde_message_end_time = level.time + std::max(duration, 0_sec);
}
// Implementación de ClearHordeMessage
void ClearHordeMessage() {
	gi.configstring(CONFIG_HORDEMSG, "");
	horde_message_end_time = 0_sec;
}
// reset cooldowns, fixed no monster spawning on next map
void ResetCooldowns() noexcept {
	lastSpawnPointTime.clear();
	lastMonsterSpawnTime.clear();
}

// For resetting bonus 
static void ResetBenefits() noexcept {
	shuffled_benefits.clear();
	obtained_benefits.clear();
	vampire_level = 0;
}

void ResetAllSpawnAttempts() noexcept {
	for (auto& attempt : spawnAttempts) {
		attempt.second = 0;
	}
	for (auto& cooldown : spawnPointCooldowns) {
		cooldown.second = SPAWN_POINT_COOLDOWN;
	}
}

static void ResetRecentBosses() noexcept {
	// Reinicia la lista de bosses recientes
	recent_bosses.clear();
	}

void ResetGame() {
	// Reiniciar estructuras de datos globales
	lastSpawnPointTime.clear();
	spawnPointCooldowns.clear();
	lastMonsterSpawnTime.clear();
	spawnAttempts.clear();

	// Reiniciar variables de estado global
	g_horde_local = HordeState();  // Asume que HordeState tiene un constructor por defecto adecuado
	current_wave_level = 0;
	flying_monsters_mode = false;
	boss_spawned_for_wave = false;
	next_wave_message_sent = false;
	allowWaveAdvance = false;

	// Reiniciar otras variables relevantes
	WAVE_TO_ALLOW_FLYING = 0;
	SPAWN_POINT_COOLDOWN = 3.8_sec;

	//// this fixes monsters travelling to the next map for now lol
	//for (unsigned int i = 1; i < globals.num_edicts; i++) {
	//    edict_t* ent = &g_edicts[i];
	//    if (ent->inuse && (ent->svflags & SVF_MONSTER)) {
	//        G_FreeEdict(ent);
	//    }
	//}

	//// Reiniciar contadores de monstruos
	//level.total_monsters = 0;
	//level.killed_monsters = 0;
	cachedRemainingMonsters = 0;

	// Reset core gameplay elements
	ResetAllSpawnAttempts();
	ResetCooldowns();
	ResetBenefits();
	ResetRecentBosses();


	// Reset wave information
	g_horde_local.level = 0;  // Reset current wave level
	g_horde_local.state = horde_state_t::warmup;  // Set game state to warmup
	g_horde_local.warm_time = level.time + 4_sec; // Reiniciar el tiempo de warmup
	g_horde_local.monster_spawn_time = level.time; // Reiniciar el tiempo de spawn de monstruos
	g_horde_local.num_to_spawn = 0;

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
	gi.cvar_set("g_bfgpull", "0");
	gi.cvar_set("g_autohaste", "0");

	// Reiniciar semilla aleatoria
	srand(static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count()));


	// Registrar el reinicio
	gi.Com_PrintFmt("Horde game state reset complete.\n");
}

// Funci�n para obtener el n�mero de jugadores activos (incluyendo bots)
inline int32_t GetNumActivePlayers() {
	int32_t numActivePlayers = 0;
	for (auto const* player : active_players()) {
		if (player->client->resp.ctf_team == CTF_TEAM1) {
			numActivePlayers++;
		}
	}
	return numActivePlayers;
}

// Funci�n para obtener el n�mero de jugadores en espectador
inline int32_t GetNumSpectPlayers() {
	int32_t numSpectPlayers = 0;
	for (auto const* player : active_players()) {
		if (player->client->resp.ctf_team != CTF_TEAM1) {
			numSpectPlayers++;
		}
	}
	return numSpectPlayers;
}

void AllowNextWaveAdvance() noexcept;
static void ResetTimers() noexcept;

// Estructura para los parámetros de condición
struct ConditionParams {
	int32_t maxMonsters{};
	gtime_t timeThreshold{};
	gtime_t independentTimeThreshold = random_time(75_sec, 90_sec);
};

// Variables globales
static gtime_t g_condition_start_time;
static gtime_t g_independent_timer_start;
static bool g_allowWaveAdvance = false;
static ConditionParams g_lastParams;
static int32_t g_lastWaveNumber = -1;
static int32_t g_lastNumHumanPlayers = -1;
static bool g_maxMonstersReached = false;

// Función para decidir los parámetros de la condición
ConditionParams GetConditionParams(const MapSize& mapSize, int32_t numHumanPlayers, int32_t lvl) {
	ConditionParams params;

	if (mapSize.isBigMap) {
		params.maxMonsters = 24;
		params.timeThreshold = random_time(20_sec, 25_sec);
	}
	else if (mapSize.isSmallMap) {
		params.maxMonsters = numHumanPlayers >= 3 ? 10 : 8;
		params.timeThreshold = random_time(5_sec, 7_sec);
	}
	else { // Medium map
		params.maxMonsters = numHumanPlayers >= 3 ? 16 : 14;
		params.timeThreshold = random_time(10_sec, 15_sec);
	}

	// Ajustar basado en el nivel de ola
	if (lvl > 10) {
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * 1.2f);
		params.timeThreshold += 3_sec;
	}

	if ((g_chaotic->integer || g_insane->integer) && numHumanPlayers <= 3) {
		params.timeThreshold += random_time(5_sec, 8_sec);
	}

	return params;
}

static int32_t CalculateRemainingMonsters() {
	static int32_t lastCalculatedRemaining = -1;
	static gtime_t lastCalculationTime;

	if (lastCalculatedRemaining == -1 || (level.time - lastCalculationTime) >= 0.5_sec) {
		int32_t remaining = 0;
		for (auto const* ent : active_monsters()) {
			if (!ent->deadflag && !(ent->monsterinfo.aiflags & AI_DO_NOT_COUNT)) {
				++remaining;
			}
		}
		lastCalculatedRemaining = remaining;
		lastCalculationTime = level.time;
	}
	return lastCalculatedRemaining;
}

void ResetWaveAdvanceState() {
	g_independent_timer_start = level.time;
	g_maxMonstersReached = false;
	g_allowWaveAdvance = false;
}

bool CheckRemainingMonstersCondition(const MapSize& mapSize) {
	if (g_allowWaveAdvance) {
		ResetWaveAdvanceState();
		return true;
	}

	const int32_t numHumanPlayers = GetNumHumanPlayers();

	if (current_wave_level != g_lastWaveNumber || numHumanPlayers != g_lastNumHumanPlayers) {
		g_lastParams = GetConditionParams(mapSize, numHumanPlayers, current_wave_level);
		g_lastWaveNumber = current_wave_level;
		g_lastNumHumanPlayers = numHumanPlayers;
		ResetWaveAdvanceState();
	}
	
	if (cachedRemainingMonsters == -1) {
		cachedRemainingMonsters = CalculateRemainingMonsters();
	}

	if (!g_maxMonstersReached && cachedRemainingMonsters <= g_lastParams.maxMonsters) {
		g_maxMonstersReached = true;
		g_condition_start_time = level.time;
	}

	const gtime_t conditionElapsed = g_maxMonstersReached ? (level.time - g_condition_start_time) : gtime_t();

	if (g_maxMonstersReached && (conditionElapsed >= g_lastParams.timeThreshold)) {
		ResetWaveAdvanceState();
		cachedRemainingMonsters = -1;
		return true;
	}

	const gtime_t independentElapsed = level.time - g_independent_timer_start;
	if (independentElapsed >= g_lastParams.independentTimeThreshold) {
		ResetWaveAdvanceState();
		cachedRemainingMonsters = -1;
		return true;
	}

	return false;
}

void AllowNextWaveAdvance() noexcept {
	g_allowWaveAdvance = true;
}

static void MonsterDied(const edict_t* monster) {
	if (!monster->deadflag && !(monster->monsterinfo.aiflags & AI_DO_NOT_COUNT)) {
		cachedRemainingMonsters--;
	}
}

static void MonsterSpawned(const edict_t* monster) {
	if (!monster->deadflag && !(monster->monsterinfo.aiflags & AI_DO_NOT_COUNT)) {
		cachedRemainingMonsters++;
	}
}
// Funci�n para decidir si se usa el spawn m�s lejano basado en el nivel actual
static bool UseFarthestSpawn() noexcept {
	if (g_horde_local.level >= 15) {
		return (rand() % 4 == 0);  // 25% de probabilidad a partir del nivel 15
	}
	return false;
}

static void PlayWaveStartSound() {
	static const std::vector<std::string> sounds = {
		"misc/r_tele3.wav",
		"world/klaxon2.wav",
		"misc/tele_up.wav",
		"world/incoming.wav",
		"world/yelforce.wav"
	};

	const int32_t sound_index = static_cast<int32_t>(frandom() * sounds.size());
	if (sound_index >= 0 && sound_index < sounds.size()) {
		gi.sound(world, CHAN_VOICE, gi.soundindex(sounds[sound_index].c_str()), 1, ATTN_NONE, 0);
	}
}

// Implementación de DisplayWaveMessage
static void DisplayWaveMessage(gtime_t duration = 5_sec) {
	if (brandom()) {
		UpdateHordeMessage("Use Inventory <KEY> or Use Compass To Open Horde Menu.\n\nMAKE THEM PAY!\n", duration);
	}
	else {
		UpdateHordeMessage("Welcome to Hell.\n\nNew! Use FlipOff <Key> looking to the wall to spawn a laser (cost: 25 cells)", duration);
	}
}

// Funci�n para manejar el mensaje de limpieza de ola
static void HandleWaveCleanupMessage(const MapSize& mapSize) noexcept {
	if (current_wave_level >= 15 && current_wave_level <= 28) {
		gi.cvar_set("g_insane", "1");
		gi.cvar_set("g_chaotic", "0");
	}
	else if (current_wave_level >= 31) {
		gi.cvar_set("g_insane", "2");
		gi.cvar_set("g_chaotic", "0");
	}
	else if (current_wave_level <= 14) {
		gi.cvar_set("g_chaotic", mapSize.isSmallMap ? "2" : "1");
	}

	g_horde_local.state = horde_state_t::rest;
}

// Array de sonidos constante
constexpr std::array<const char*, 6> WAVE_SOUNDS = {
	"nav_editor/action_fail.wav",
	"makron/roar1.wav",
	"zortemp/ack.wav",
	"misc/spawn1.wav",
	"makron/voice3.wav",
	"world/v_fac3.wav"
};


// Función para precarga de sonidos
static void PrecacheWaveSounds() noexcept {
	for (const auto& sound : WAVE_SOUNDS) {
		gi.soundindex(sound);
	}
}

// Función para obtener un sonido aleatorio
static const char* GetRandomWaveSound() {
	std::uniform_int_distribution<size_t> dist(0, WAVE_SOUNDS.size() - 1);
	return WAVE_SOUNDS[dist(mt_rand)];
}

static void HandleWaveRestMessage(gtime_t duration = 4_sec) {
	const char* message;

	if (!g_insane->integer) {
		message = "STROGGS STARTING TO PUSH!\n\n";
	}
	else if (g_insane->integer == 1) {
		message = brandom() ?
			"--STRONGER WAVE INCOMING--\n\n\n" :
			"--STRONGER WAVE INCOMING--\n\nSHOW NO MERCY!\n";
	}
	else {
		message = "***CHAOTIC WAVE INCOMING***\n\nNO RETREAT!\n";
	}

	UpdateHordeMessage(message, duration);

	// Reproducir un sonido aleatorio
	gi.sound(world, CHAN_VOICE, gi.soundindex(GetRandomWaveSound()), 1, ATTN_NONE, 0);

	//// Resetear el daño total para todos los jugadores activos
	//for (const auto player : active_players()) {
	//    if (player->client) {
	//        player->client->total_damage = 0;
	//    }
	//}
}

// Llamar a esta función durante la inicialización del juego
void InitializeWaveSystem() noexcept {
	PrecacheWaveSounds();
}

static void SetMonsterArmor(edict_t* monster);
static void SetNextMonsterSpawnTime(const MapSize& mapSize);

static edict_t* SpawnMonsters() {
	static const auto mapSize = GetMapSize(level.mapname);
	static std::vector<edict_t*> available_spawns;
	available_spawns.clear();
	available_spawns.reserve(MAX_EDICTS);

	// Determinar la cantidad de monstruos a generar por spawn
	const int32_t monsters_per_spawn = std::min(
		mapSize.isSmallMap ? (g_horde_local.level >= 5 ? 4 : 3) :
		mapSize.isBigMap ? (g_horde_local.level >= 5 ? 6 : 5) :
		(g_horde_local.level >= 5 ? 5 : 4),
		6
	);

	// Calcular la probabilidad de que un monstruo suelte un ítem
	const float drop_probability = (current_wave_level <= 2) ? 0.8f :
		(current_wave_level <= 7) ? 0.6f : 0.45f;

	// Pre-seleccionar puntos de spawn disponibles
	for (unsigned int edictIndex = 1; edictIndex < globals.num_edicts; edictIndex++) {
		edict_t* e = g_edicts + edictIndex;
		if (!e->inuse || !e->classname || strcmp(e->classname, "info_player_deathmatch") != 0)
			continue;
		available_spawns.push_back(e);
	}

	if (available_spawns.empty()) {
		gi.Com_PrintFmt("Warning: No spawn points found\n");
		return nullptr;
	}

	edict_t* spawn_point = nullptr;
	edict_t* last_spawned_monster = nullptr;

	// Generar monstruos
	for (int32_t spawnCount = 0; spawnCount < monsters_per_spawn && g_horde_local.num_to_spawn > 0 && !available_spawns.empty(); ++spawnCount) {
		// Seleccionar un punto de spawn aleatoriamente
		const size_t spawn_index = static_cast<size_t>(frandom() * available_spawns.size());
		spawn_point = available_spawns[spawn_index];

		const char* monster_classname = G_HordePickMonster(spawn_point);
		if (!monster_classname) {
			available_spawns.erase(available_spawns.begin() + spawn_index);
			continue;
		}

		auto monster = G_Spawn();
		if (!monster) {
			gi.Com_PrintFmt("G_Spawn Warning: Failed to spawn monster\n");
			continue;
		}

		monster->classname = monster_classname;
		monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
		monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
		monster->monsterinfo.last_sentrygun_target_time = 0_sec;
		VectorCopy(spawn_point->s.origin, monster->s.origin);
		VectorCopy(spawn_point->s.angles, monster->s.angles);

		ED_CallSpawn(monster);

		// Verificar si el monstruo sigue siendo válido después de ED_CallSpawn
		if (!monster->inuse) {
			gi.Com_PrintFmt("ED_CallSpawn Warning: Monster spawn failed\n");
			continue;
		}

		if (g_horde_local.level >= 17) {
			SetMonsterArmor(monster);
		}

		if (frandom() <= drop_probability) {
			monster->item = G_HordePickItem();
		}

		const vec3_t spawngrow_pos = monster->s.origin;
		const float magnitude = VectorLength(spawngrow_pos);
		const float start_size = magnitude * 0.055f;
		const float end_size = magnitude * 0.005f;
		ImprovedSpawnGrow(spawngrow_pos, start_size, end_size, monster);

		--g_horde_local.num_to_spawn;
		MonsterSpawned(monster);
		available_spawns.erase(available_spawns.begin() + spawn_index);
		last_spawned_monster = monster;
	}

	SetNextMonsterSpawnTime(mapSize);
	return last_spawned_monster;
}

// Funciones auxiliares para reducir el tamaño de SpawnMonsters
static void SetMonsterArmor(edict_t* monster) {
	if (!st.was_key_specified("power_armor_power"))
		monster->monsterinfo.armor_type = IT_ARMOR_COMBAT;
	if (!st.was_key_specified("power_armor_type")) {
		const float health_factor = sqrt(monster->max_health / 100.0f);
		const int base_armor = (current_wave_level <= 25) ?
			irandom(75, 225) + health_factor * irandom(1, 3) :
			irandom(150, 320) + health_factor * irandom(2, 5);

		const int additional_armor = static_cast<int>((current_wave_level - 20) * 10 * health_factor);
		monster->monsterinfo.armor_power = base_armor + additional_armor;
	}
}

static void SetNextMonsterSpawnTime(const MapSize& mapSize) {
	g_horde_local.monster_spawn_time = level.time +
		(mapSize.isSmallMap ? (1.2_sec, 1.5_sec) :
			mapSize.isBigMap ? random_time(0.9_sec, 1.1_sec) :
			random_time(1.7_sec, 1.8_sec));
}
#include <unordered_map>
#include <fmt/core.h>

// Usar enum class para mejorar la seguridad de tipos
enum class MessageType {
	Standard,
	Chaotic,
	Insane
};

// Definición de los mensajes de limpieza con placeholders nombrados
const std::unordered_map<MessageType, std::string_view> cleanupMessages = {
	{MessageType::Standard, "Wave Level {level} Defeated, GG!\n"},
	{MessageType::Chaotic, "Harder Wave Level {level} Controlled, GG!\n"},
	{MessageType::Insane, "Insane Wave Level {level} Controlled, GG!\n"}
};

// Función para enviar el mensaje de limpieza actualizada
void SendCleanupMessage(const std::unordered_map<MessageType, std::string_view>& messages,
	gtime_t duration = 5_sec) {
	const MessageType messageType = g_insane->integer ? MessageType::Insane :
		(g_chaotic->integer ? MessageType::Chaotic : MessageType::Standard);

	const auto messageIt = messages.find(messageType);
	if (messageIt != messages.end()) {
		std::string formattedMessage = fmt::format(messageIt->second,
			fmt::arg("level", g_horde_local.level));

		// Actualizar el mensaje de Horde con la duración correcta
		UpdateHordeMessage(formattedMessage, duration);
	}
	else {
		gi.Com_PrintFmt("Warning: Unknown message type '{}'\n", static_cast<int>(messageType));
	}
}

void Horde_RunFrame() {
	const auto mapSize = GetMapSize(level.mapname);

	if (dm_monsters->integer > 0) {
		g_horde_local.num_to_spawn = dm_monsters->integer;
	}

	level.coop_scale_players = 1 + GetNumHumanPlayers();
	G_Monster_CheckCoopHealthScaling();

	const int32_t activeMonsters = level.total_monsters - level.killed_monsters;
	const int32_t maxMonsters = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
		(mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP);

	CleanupInvalidEntities();

	switch (g_horde_local.state) {
	case horde_state_t::warmup:
		if (g_horde_local.warm_time < level.time + 0.4_sec) {
			cachedRemainingMonsters = CalculateRemainingMonsters();
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(1);
			current_wave_level = 1;
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
				g_horde_local.warm_time = level.time + random_time(2.2_sec, 3.0_sec);
				g_horde_local.state = horde_state_t::rest;
				cachedRemainingMonsters = CalculateRemainingMonsters();
				SendCleanupMessage(cleanupMessages, 5_sec);
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
				HandleWaveRestMessage(4_sec);
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

	if (horde_message_end_time && level.time >= horde_message_end_time) {
		ClearHordeMessage();
	}

	UpdateHordeHUD();
}

// Función para manejar el evento de reinicio
void HandleResetEvent() {
	ResetGame();
}
