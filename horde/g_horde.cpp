// Includes y definiciones relevantes
#include "../g_local.h"
#include "g_horde.h"
#include "../shared.h"
#include <set>
#include "g_horde_benefits.h"

// Definir tamaños máximos para arrays estáticos
constexpr size_t MAX_ELIGIBLE_BOSSES = 32;
constexpr size_t MAX_ELIGIBLE_MONSTERS = 64;
constexpr size_t MAX_ELIGIBLE_ITEMS = 64;
constexpr size_t MAX_RECENT_BOSSES = 3;
constexpr size_t MAX_SPAWN_POINTS = 64;


//precache
static cached_soundindex sound_tele3;
static cached_soundindex sound_klaxon2;
static cached_soundindex sound_tele_up;
static cached_soundindex sound_incoming;
static cached_soundindex sound_yelforce;
static cached_soundindex sound_action_fail;
static cached_soundindex sound_roar1;
static cached_soundindex sound_ack;
static cached_soundindex sound_spawn1;
static cached_soundindex sound_voice3;
static cached_soundindex sound_v_fac3;

enum class WaveEndReason {
	AllMonstersDead,
	MonstersRemaining,
	TimeLimitReached
};

int GetNumActivePlayers();
int GetNumSpectPlayers();
int GetNumHumanPlayers();

constexpr int32_t MAX_MONSTERS_BIG_MAP = 27;
constexpr int32_t MAX_MONSTERS_MEDIUM_MAP = 16;
constexpr int32_t MAX_MONSTERS_SMALL_MAP = 14;

bool allowWaveAdvance = false; // Global variable to control wave advancement
bool boss_spawned_for_wave = false; // Variable de control para el jefe
bool flying_monsters_mode = false; // Variable de control para el jefe volador

uint64_t last_wave_number = 0;
uint16_t cachedRemainingMonsters = 0;
uint32_t g_totalMonstersInWave = 0;

gtime_t horde_message_end_time = 0_sec;
gtime_t SPAWN_POINT_COOLDOWN = 2.2_sec; //spawns Cooldown 

// Añadir cerca de las otras constexpr al inicio del archivo
constexpr gtime_t GetBaseSpawnCooldown(bool isSmallMap, bool isBigMap) {
	if (isSmallMap)
		return 0.5_sec;  // Mapas pequeños necesitan más cooldown
	else if (isBigMap)
		return 2.5_sec;  // Mapas grandes tienen menos cooldown
	else
		return 1.5_sec;  // Mapas medianos tienen cooldown intermedio
}

// Nueva función para calcular el factor de escala del cooldown basado en el nivel
static float CalculateCooldownScale(int32_t level) {
	if (level <= 10) {
		return 1.0f; // Sin cambios hasta nivel 10
	}

	// Comenzar a escalar después del nivel 10
	const float scale = 1.0f + ((level - 10) * 0.05f); // 5% de aumento por nivel

	// Limitar el máximo de escala para que no exceda 3 segundos
	return std::min(scale, 1.75f); // 1.75x del base cooldown como máximo
}


cvar_t* g_horde;

enum class horde_state_t {
	warmup,
	spawning,
	active_wave,
	cleanup,
	rest
};

// En HordeState, reemplazar el vector con array estático
struct HordeState {
	gtime_t         warm_time = 4_sec;
	horde_state_t   state = horde_state_t::warmup;
	gtime_t         monster_spawn_time;
	int32_t         num_to_spawn = 0;
	int32_t         level = 0;
	int32_t         queued_monsters = 0;
	gtime_t         lastPrintTime = 0_sec;

	bool            conditionTriggered = false;
	gtime_t         conditionStartTime = 0_sec;
	gtime_t         conditionTimeThreshold = 0_sec;
	bool            timeWarningIssued = false;
	gtime_t         waveEndTime = 0_sec;
	bool            warningIssued[4] = { false, false, false, false }; // Array estático en lugar de vector
}g_horde_local;

// Clase de selección genérica usando templates
// Nueva implementación de WeightedSelection usando arrays estáticos
template <typename T>
struct WeightedSelection {
	static constexpr size_t MAX_ITEMS = 64;
	const T* items[MAX_ITEMS];
	double cumulative_weights[MAX_ITEMS];
	size_t item_count = 0;
	double total_weight = 0.0;

	void clear() {
		item_count = 0;
		total_weight = 0.0;
	}

	void add(const T* item, double weight) {
		if (item_count < MAX_ITEMS) {
			items[item_count] = item;
			total_weight += weight;
			cumulative_weights[item_count] = total_weight;
			item_count++;
		}
	}

	const T* select() const {
		if (item_count == 0) return nullptr;

		std::uniform_real_distribution<double> dist(0.0, total_weight);
		double random_weight = dist(mt_rand);

		// Búsqueda binaria en el array de pesos acumulativos
		size_t left = 0;
		size_t right = item_count - 1;

		while (left < right) {
			size_t mid = (left + right) / 2;
			if (cumulative_weights[mid] < random_weight) {
				left = mid + 1;
			}
			else {
				right = mid;
			}
		}

		return items[left];
	}
};


int32_t current_wave_level = g_horde_local.level;
bool next_wave_message_sent = false;
auto auto_spawned_bosses = std::unordered_set<edict_t*>{};
auto lastMonsterSpawnTime = std::unordered_map<std::string, gtime_t>{};
auto lastSpawnPointTime = std::unordered_map<edict_t*, gtime_t>{};
struct SpawnPointData {
	uint32_t attempts = 0;               // Number of failed spawn attempts
	gtime_t cooldown = 0_sec;            // Cooldown time before retrying
	gtime_t lastSpawnTime = 0_sec;       // Last recorded spawn attempt time
	uint32_t successfulSpawns = 0;       // Number of successful spawns
	bool isTemporarilyDisabled = false;  // Indicates if the spawn point is temporarily disabled
	std::string lastSpawnedMonsterClassname; // Stores the classname of the last spawned monster
	gtime_t cooldownEndsAt = 0_sec;      // Time when the cooldown ends for the spawn point
};

std::unordered_map<edict_t*, SpawnPointData> spawnPointsData;


const std::unordered_set<std::string> smallMaps = {
	"q2dm3", "q2dm7", "q2dm2", "q64/dm10", "test/mals_barrier_test",
	"q64/dm9", "q64/dm7", "q64\\dm7", "q64/dm2", "test/spbox",
	"q64/dm1", "fact3", "q2ctf4", "rdm4", "q64/command","mgu3m4",
	"mgu4trial", "mgu6trial", "ec/base_ec", "mgdm1", "ndctf0", "q64/dm6",
	"q64/dm8", "q64/dm4", "q64/dm3", "industry"
};

const std::unordered_set<std::string> bigMaps = {
	"q2ctf5", "old/kmdm3", "xdm2", "xdm6", "rdm6", "rdm8", "xdm1", "waste2", "rdm9"
};

constexpr size_t MAX_MAPS = 64;
std::array<std::pair<std::string, MapSize>, MAX_MAPS> mapSizeCache;
size_t mapSizeCacheSize = 0;

MapSize GetMapSize(const std::string& mapname) {
	static std::unordered_map<std::string, MapSize> cache;

	const auto it = cache.find(mapname);
	if (it != cache.end())
		return it->second;

	MapSize size;
	size.isSmallMap = smallMaps.count(mapname) > 0;
	size.isBigMap = bigMaps.count(mapname) > 0;
	size.isMediumMap = !size.isSmallMap && !size.isBigMap;

	cache[mapname] = size;
	return size;
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

inline static void ClampNumToSpawn(const MapSize& mapSize) {
	int32_t maxAllowed = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
		(mapSize.isBigMap ? MAX_MONSTERS_BIG_MAP : MAX_MONSTERS_MEDIUM_MAP);

	// Ajuste dinámico basado en jugadores activos
	const int32_t activePlayers = GetNumActivePlayers();
	maxAllowed += std::min(activePlayers - 1, 4) * 2;

	g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, maxAllowed);
}

static int32_t CalculateQueuedMonsters(const MapSize& mapSize, int32_t lvl, bool isHardMode) {
	if (lvl <= 3) return 0;
	// Base más agresiva
	float baseQueued = std::sqrt(static_cast<float>(lvl)) * 3.0f;
	baseQueued *= (1.0f + (lvl) * 0.18f);
	// Mejores multiplicadores por tamaño de mapa
	const float mapSizeMultiplier = mapSize.isSmallMap ? 1.3f :
		mapSize.isBigMap ? 1.5f : 1.4f;
	const int32_t maxQueued = mapSize.isSmallMap ? 30 :
		mapSize.isBigMap ? 45 : 35;
	baseQueued *= mapSizeMultiplier;
	// Bonus exponencial mejorado
	if (lvl > 20) {
		const	float highLevelBonus = std::pow(1.15f, std::min(lvl - 20, 18));
		baseQueued *= highLevelBonus;
	}
	// Mejor bonus por dificultad
	if (isHardMode) {
		float difficultyMultiplier = 1.25f;
		if (lvl > 25) {
			difficultyMultiplier += (lvl - 25) * 0.025f;
			difficultyMultiplier = std::min(difficultyMultiplier, 1.75f);
		}
		baseQueued *= difficultyMultiplier;
	}

	// Aplicamos el factor de reducción de 0.75 antes del clamp final
	baseQueued *= 0.85f;

	return std::min(static_cast<int32_t>(baseQueued), maxQueued);
}
static void UnifiedAdjustSpawnRate(const MapSize& mapSize, int32_t lvl, int32_t humanPlayers) noexcept {
	// Base count calculation with level scaling
	int32_t baseCount = (mapSize.isSmallMap) ?
		std::min((lvl <= 6) ? 7 : 9 + lvl, MAX_MONSTERS_SMALL_MAP) :
		(mapSize.isBigMap) ?
		std::min((lvl <= 4) ? 24 : 27 + lvl, MAX_MONSTERS_BIG_MAP) :
		std::min((lvl <= 4) ? 5 : 8 + lvl, MAX_MONSTERS_MEDIUM_MAP);

	// Additional spawn calculation with progressive scaling
	int32_t additionalSpawn = (lvl >= 8) ?
		((mapSize.isBigMap) ? 12 : (mapSize.isSmallMap ? 8 : 7)) : 6;

	// Ajuste dinámico del cooldown basado en el nivel
	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);

	// Aplicar escala basada en nivel después del nivel 10
	float cooldownScale = CalculateCooldownScale(lvl);
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	// Ajuste de spawn rate basado en jugadores
	if (humanPlayers > 1) {
		const float playerAdjustment = 1.0f - (std::min(humanPlayers - 1, 3) * 0.05f);
		SPAWN_POINT_COOLDOWN *= playerAdjustment;
	}

	// Enhanced level scaling for higher levels
	if (lvl > 25) {
		additionalSpawn = static_cast<int32_t>(additionalSpawn * 1.6f);
	}

	// Bonus for chaotic/insane modes
	if (lvl >= 3 && (g_chaotic->integer || g_insane->integer)) {
		additionalSpawn += CalculateChaosInsanityBonus(lvl);
		// Reducción del cooldown en modos difíciles
		SPAWN_POINT_COOLDOWN *= 0.95f;
	}

	// Dynamic difficulty scaling based on player count
	const float difficultyMultiplier = 1.0f + (humanPlayers - 1) * 0.075f;

	// Periodic scaling adjustments
	if (lvl % 3 == 0) {
		baseCount = static_cast<int32_t>(baseCount * difficultyMultiplier);

		// Convertir el multiplicador de dificultad a tiempo
		const gtime_t periodicReduction = gtime_t::from_sec(
			(mapSize.isBigMap ? 0.1f : 0.15f) * difficultyMultiplier
		);

		// Aplicar la reducción con límites apropiados
		SPAWN_POINT_COOLDOWN = std::max(
			SPAWN_POINT_COOLDOWN - periodicReduction,
			1.0_sec
		);
	}

	// Límites absolutos finales para el cooldown
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN, 1.0_sec, 3.0_sec);

	// Update spawn count with clamping
	g_horde_local.num_to_spawn = baseCount + static_cast<int32_t>(additionalSpawn);
	ClampNumToSpawn(mapSize);

	// Actualizar la cola usando el nuevo sistema
	const bool isHardMode = g_insane->integer || g_chaotic->integer;
	g_horde_local.queued_monsters = CalculateQueuedMonsters(mapSize, lvl, isHardMode);

	// Debug info mejorado
	gi.Com_PrintFmt("DEBUG: Wave {} settings:\n", lvl);
	gi.Com_PrintFmt("  - Spawn cooldown: {:.2f}s (Scale {:.2f}x)\n",
		SPAWN_POINT_COOLDOWN.seconds(), cooldownScale);
	gi.Com_PrintFmt("  - Base monsters: {}\n", baseCount);
	gi.Com_PrintFmt("  - Additional spawns: {}\n", additionalSpawn);
	gi.Com_PrintFmt("  - Queued monsters: {}\n", g_horde_local.queued_monsters);
	gi.Com_PrintFmt("  - Map type: {}\n",
		mapSize.isBigMap ? "big" : (mapSize.isSmallMap ? "small" : "medium"));
}


void ResetAllSpawnAttempts() noexcept;
void VerifyAndAdjustBots();
void ResetCooldowns() noexcept;

struct ConditionParams {
	int32_t maxMonsters;
	gtime_t timeThreshold;
	gtime_t lowPercentageTimeThreshold;
	gtime_t independentTimeThreshold;
	float lowPercentageThreshold;
	float aggressiveTimeReductionThreshold;

	ConditionParams() noexcept :
		maxMonsters(0),
		timeThreshold(0_sec),
		lowPercentageTimeThreshold(0_sec),
		independentTimeThreshold(0_sec),
		lowPercentageThreshold(0.3f),
		aggressiveTimeReductionThreshold(0.3f) {}
};

// Función para calcular el rendimiento del jugador (implementa según tus necesidades)
static float CalculatePlayerPerformance() {
	// Implementación de ejemplo. Ajusta según tus necesidades.
	const float killRate = static_cast<float>(level.killed_monsters) / std::max(1.0f, static_cast<float>(g_totalMonstersInWave));
	return std::clamp(killRate, 0.5f, 2.0f);  // Limita el factor entre 0.5 y 2
}

// Constantes y funciones auxiliares
constexpr gtime_t BASE_MAX_WAVE_TIME = 85_sec;
constexpr gtime_t TIME_INCREASE_PER_LEVEL = 1.5_sec;
constexpr gtime_t BOSS_TIME_BONUS = 60_sec;
constexpr int MONSTERS_FOR_AGGRESSIVE_REDUCTION = 5;
constexpr gtime_t AGGRESSIVE_TIME_REDUCTION_PER_MONSTER = 10_sec;

static constexpr gtime_t calculate_max_wave_time(int32_t wave_level) {
	// Calcular el tiempo base según el nivel
	gtime_t base_time = BASE_MAX_WAVE_TIME + TIME_INCREASE_PER_LEVEL * wave_level;

	// Limitar el tiempo base a 90 segundos
	base_time = (base_time <= 200_sec) ? base_time : 200_sec;

	// Añadir tiempo extra si es una ola con jefe (niveles múltiplos de 5 después del 10)
	if (wave_level >= 10 && wave_level % 5 == 0) {
		base_time += BOSS_TIME_BONUS;
	}

	return base_time;
}

// Variables globales
static gtime_t g_condition_start_time;
static gtime_t g_independent_timer_start;
static ConditionParams g_lastParams;
static int32_t g_lastWaveNumber = -1;
static int32_t g_lastNumHumanPlayers = -1;
static bool g_maxMonstersReached = false;
static bool g_lowPercentageTriggered = false;

static ConditionParams GetConditionParams(const MapSize& mapSize, int32_t numHumanPlayers, int32_t lvl) {
	ConditionParams params;

	if (g_horde_local.level == 0)
		return params; //maybe this will prevent wave timer pre wave 1

	auto configureMapParams = [&](ConditionParams& params) {
		if (mapSize.isBigMap) {
			params.maxMonsters = (numHumanPlayers >= 3) ? 26 : 22;
			params.timeThreshold = random_time(20_sec, 26_sec);
		}
		else if (mapSize.isSmallMap) {
			params.maxMonsters = (numHumanPlayers >= 3) ? 12 : 9;
			params.timeThreshold = random_time(14_sec, 20_sec);
		}
		else {
			params.maxMonsters = (numHumanPlayers >= 3) ? 17 : 13;
			params.timeThreshold = random_time(18_sec, 25_sec);
		}
		};

	configureMapParams(params);


	// Ajuste progresivo basado en el nivel - más agresivo
	params.maxMonsters += std::min(lvl / 4, 8); // Ajustado de lvl/5,10 a lvl/4,8
	params.timeThreshold += gtime_t::from_ms(75 * std::min(lvl / 3, 4)); // Reducido el incremento de tiempo

	// Ajuste para niveles altos - más dinámico
	if (lvl > 10) {
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * 1.2f); // Reducido de 1.3 a 1.2
		params.timeThreshold += 0.15_sec; // Reducido de 0.2 a 0.15
	}

	// Ajuste basado en dificultad - más agresivo
	if (g_chaotic->integer || g_insane->integer) {
		if (numHumanPlayers <= 3) {
			params.timeThreshold += random_time(5_sec, 8_sec); // Reducido de 5-10 a 5-8
		}
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * 1.1f);
	}

	//// Ajuste basado en el rendimiento del jugador
	//const float playerPerformanceFactor = CalculatePlayerPerformance();
	//params.maxMonsters = static_cast<int32_t>(params.maxMonsters * playerPerformanceFactor);

	//// Aplicar un factor de reducción adicional si el rendimiento es alto
	//if (playerPerformanceFactor > 1.0f) {
	//	params.timeThreshold = gtime_t::from_sec(params.timeThreshold.seconds() / playerPerformanceFactor);
	//}
	//else {
	//	params.timeThreshold = gtime_t::from_sec(params.timeThreshold.seconds() * playerPerformanceFactor);
	//}

	// Configuración para el porcentaje bajo de monstruos restantes
	params.lowPercentageTimeThreshold = random_time(8_sec, 17_sec);
	params.lowPercentageThreshold = 0.3f;

	// Configuración para tiempo independiente basado en el nivel
	params.independentTimeThreshold = calculate_max_wave_time(lvl);

	gi.Com_PrintFmt("PRINT: Wave {} parameters set: Max monsters: {}, Time threshold: {:.1f}s, Low percentage threshold: {:.2f}, Independent time threshold: {:.1f}s\n",
		lvl, params.maxMonsters, params.timeThreshold.seconds(), params.lowPercentageThreshold, params.independentTimeThreshold.seconds());

	return params;
}

// Warning times in seconds
constexpr std::array<float, 3> WARNING_TIMES = { 30.0f, 10.0f, 5.0f };

static void Horde_InitLevel(const int32_t lvl) {
	const MapSize& mapSize = GetMapSize(level.mapname);

	g_independent_timer_start = level.time;

	// Configuración de variables iniciales para el nivel
	g_totalMonstersInWave = g_horde_local.num_to_spawn;
	cachedRemainingMonsters = g_totalMonstersInWave;
	last_wave_number++;
	g_horde_local.level = lvl;
	current_wave_level = lvl;
	flying_monsters_mode = false;
	boss_spawned_for_wave = false;
	next_wave_message_sent = false;

	VerifyAndAdjustBots();

	// Configurar tiempos iniciales
	g_independent_timer_start = level.time;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTriggered = false;
	g_horde_local.waveEndTime = 0_sec;
	g_horde_local.lastPrintTime = 0_sec;

	g_lastParams = GetConditionParams(mapSize, GetNumHumanPlayers(), lvl);

	// Ajustar la escala de daño según el nivel
	switch (lvl) {
	case 15:
		gi.cvar_set("g_damage_scale", "1.75");
		break;
	case 25:
		gi.cvar_set("g_damage_scale", "2.25");
		break;
	case 35:
		gi.cvar_set("g_damage_scale", "3.5");
		break;
	case 45:
		gi.cvar_set("g_damage_scale", "4");
		break;
	default:
		break;
	}

	UnifiedAdjustSpawnRate(mapSize, lvl, GetNumHumanPlayers());

	CheckAndApplyBenefit(lvl);
	ResetAllSpawnAttempts();
	ResetCooldowns();
	Horde_CleanBodies();

	gi.Com_PrintFmt("PRINT: Horde level initialized: {}\n", lvl);
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

	{ "item_ir_goggles", 2, -1, 0.03f, adjust_weight_powerup },
	{ "item_quad", 6, -1, 0.04f, adjust_weight_powerup },
	{ "item_double", 4, -1, 0.05f, adjust_weight_powerup },
	{ "item_quadfire", 2, -1, 0.04f, adjust_weight_powerup },
	{ "item_invulnerability", 4, -1, 0.03f, adjust_weight_powerup },
	{ "item_sphere_defender", 2, -1, 0.05f, adjust_weight_powerup },
	{ "item_sphere_vengeance", 23, -1, 0.06f, adjust_weight_powerup },
	{ "item_sphere_hunter", 9, -1, 0.04f, adjust_weight_powerup },
	{ "item_invisibility", 4, -1, 0.06f, adjust_weight_powerup },
	{ "teleport_device", 4, -1, 0.06f, adjust_weight_powerup },
	{ "item_doppleganger", 2, 8, 0.028f, adjust_weight_powerup },
	{ "item_doppleganger", 9, 19, 0.062f, adjust_weight_powerup },
	{ "item_doppleganger", 20, -1, 0.1f, adjust_weight_powerup },

	{ "weapon_chainfist", -1, 3, 0.05f, adjust_weight_weapon },
	{ "weapon_shotgun", -1, -1, 0.22f, adjust_weight_weapon },
	{ "weapon_supershotgun", 5, -1, 0.17f, adjust_weight_weapon },
	{ "weapon_machinegun", -1, -1, 0.25f, adjust_weight_weapon },
	{ "weapon_etf_rifle", 4, -1, 0.17f, adjust_weight_weapon },
	{ "weapon_chaingun", 9, -1, 0.17f, adjust_weight_weapon },
	{ "weapon_grenadelauncher", 10, -1, 0.17f, adjust_weight_weapon },
	{ "weapon_proxlauncher", 4, -1, 0.1f, adjust_weight_weapon },
	{ "weapon_boomer", 14, -1, 0.17f, adjust_weight_weapon },
	{ "weapon_hyperblaster", 12, -1, 0.2f, adjust_weight_weapon },
	{ "weapon_rocketlauncher", 14, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_railgun", 24, -1, 0.17f, adjust_weight_weapon },
	{ "weapon_phalanx", 16, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_plasmabeam", 17, -1, 0.25f, adjust_weight_weapon },
	{ "weapon_disintegrator", 28, -1, 0.15f, adjust_weight_weapon },
	{ "weapon_bfg", 24, -1, 0.17f, adjust_weight_weapon },


	{ "ammo_trap", 4, -1, 0.18f, adjust_weight_ammo },
	{ "ammo_bullets", -1, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_flechettes", 4, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_grenades", -1, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_prox", 5, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_tesla", -1, -1, 0.1f, adjust_weight_ammo },
	{ "ammo_cells", 13, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_cells", 2, 12, 0.12f, adjust_weight_ammo },
	{ "ammo_magslug", 15, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_slugs", 22, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_disruptor", 24, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_rockets", 13, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_nuke", 20, -1, 0.01f, adjust_weight_ammo },

	{ "item_bandolier", 4, -1, 0.2f, adjust_weight_ammo },
	{ "item_pack", 15, -1, 0.34f, adjust_weight_ammo },
	{ "item_silencer", 15, -1, 0.1f, adjust_weight_ammo },
};

// Definici�n de monstruos ponderados
constexpr weighted_item_t monsters[] = {
	// Enemigos básicos (Olas 1-5)
	{ "monster_soldier_light", -1, 8, 0.27f },
	{ "monster_soldier", -1, 8, 0.25f },
	{ "monster_soldier_ss", -1, 12, 0.23f },
	{ "monster_infantry_vanilla", -1, 3, 0.25f },
	{ "monster_infantry_vanilla", 4, 12, 0.32f },

	// Enemigos intermedios (Olas 4-10)
	{ "monster_soldier_hypergun", 4, -1, 0.2f },
	{ "monster_soldier_lasergun", 6, -1, 0.3f },
	{ "monster_soldier_ripper", 5, 14, 0.22f },
	{ "monster_infantry", 8, -1, 0.32f },

	// Enemigos de apoyo temprano
	{ "monster_medic", 3, 12, 0.13f },
	{ "monster_medic", 13, 20, 0.12f },
	{ "monster_medic_commander", 21, -1, 0.08f },       // Retrasado a ola 21

	// Voladores básicos y early challengers
	{ "monster_flyer", -1, 4, 0.1f },
	{ "monster_flyer", 5, -1, 0.17f },
	{ "monster_hover_vanilla", 7, 16, 0.18f },
	{ "monster_hover", 13, -1, 0.16f },
	{ "monster_gekk", -1, 5, 0.12f },
	{ "monster_gekk", 6, 15, 0.15f },

	// Enemigos técnicos y Gunners
	{ "monster_fixbot", 8, 19, 0.13f },
	{ "monster_gunner_vanilla", 5, 15, 0.3f },
	{ "monster_gunner", 12, -1, 0.28f },
	{ "monster_guncmdr_vanilla", 8, 15, 0.18f },              // Versión más débil
	{ "monster_guncmdr", 14, -1, 0.25f },              // Versión fuerte

	// Enemigos especializados
	{ "monster_brain", 8, 14, 0.22f },
	{ "monster_brain", 15, -1, 0.25f },
	{ "monster_stalker", 6, 15, 0.17f },
	{ "monster_parasite", 4, 16, 0.2f },

	// Tanques y variantes (progresión gradual)
	{ "monster_tank_spawner", 3, 6, 0.015f },
	{ "monster_tank_spawner", 7, 12, 0.08f },
	{ "monster_tank_spawner", 13, 25, 0.15f },
	{ "monster_tank_spawner", 26, -1, 0.2f },
	{ "monster_tank", 11, -1, 0.25f },
	{ "monster_tank_commander", 14, -1, 0.15f },
	{ "monster_runnertank", 16, -1, 0.22f },

	// Voladores avanzados
	{ "monster_floater", 12, -1, 0.22f },               // Desde ola 12
	{ "monster_floater_tracker", 20, -1, 0.16f },
	{ "monster_daedalus", 18, -1, 0.12f },              // Retrasado a ola 18
	{ "monster_daedalus_bomber", 22, -1, 0.14f },       // Retrasado a ola 22
	{ "monster_daedalus_bomber", 6, -1, 0.041f },       // Retrasado a ola 22

	// Enemigos brutales
	{ "monster_mutant", 4, 12, 0.18f },
	{ "monster_mutant", 13, -1, 0.25f },
	{ "monster_redmutant", 9, 14, 0.04f },
	{ "monster_redmutant", 15, -1, 0.18f },
	{ "monster_berserk", 7, -1, 0.25f },

	// Gladiators
	{ "monster_gladiator", 13, -1, 0.25f },
	{ "monster_gladb", 16, -1, 0.35f },
	{ "monster_gladc", 18, -1, 0.28f },

	// Cazadores especializados
	{ "monster_chick", 6, 20, 0.25f },
	{ "monster_chick_heat", 13, -1, 0.3f },

	// Arachnids (después de ola 15)
	{ "monster_spider", 15, 20, 0.25f },
	{ "monster_gm_arachnid", 20, -1, 0.22f },
	{ "monster_arachnid", 18, -1, 0.25f },

	// Mini-jefes
	{ "monster_shambler", 15, 25, 0.08f },
	{ "monster_shambler", 26, -1, 0.28f },
	{ "monster_tank_64", 23, -1, 0.13f },

	// Enemigos especiales late-game
	{ "monster_janitor", 23, -1, 0.12f },
	{ "monster_janitor2", 19, -1, 0.1f },
	{ "monster_makron", 16, 21, 0.015f },
	{ "monster_boss2_64", 15, -1, 0.09f },
	{ "monster_carrier_mini", 19, -1, 0.09f },
	{ "monster_perrokl", 20, -1, 0.3f },
	{ "monster_widow1", 22, -1, 0.1f }
};

#include <array>
#include <unordered_set>
#include <random>
#include <deque>

// Definición de jefes por tamaño de mapa
struct boss_t {
	const char* classname;
	int32_t min_level;
	int32_t max_level;
	float weight;
	BossSizeCategory sizeCategory; // Si decides extender la estructura
};

// Listas de jefes con la categoría de tamaño asignada
constexpr boss_t BOSS_SMALL[] = {
	{"monster_carrier_mini", 24, -1, 0.1f, BossSizeCategory::Small},
	{"monster_boss2kl", 24, -1, 0.1f, BossSizeCategory::Small},
	{"monster_widow2", 19, -1, 0.1f, BossSizeCategory::Small},
	{"monster_tank_64", -1, -1, 0.1f, BossSizeCategory::Small},
	{"monster_shamblerkl", -1, -1, 0.1f, BossSizeCategory::Small},
	{"monster_guncmdrkl", -1, 19, 0.1f, BossSizeCategory::Small},
	{"monster_makronkl", 36, -1, 0.1f, BossSizeCategory::Small},
	// {"monster_gm_arachnid", -1, 19, 0.1f, BossSizeCategory::Small},
	{"monster_psxarachnid", 15, -1, 0.1f, BossSizeCategory::Small},
	{"monster_redmutant", -1, 24, 0.1f, BossSizeCategory::Small}
};

constexpr boss_t BOSS_MEDIUM[] = {
	{"monster_carrier", 24, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_boss2", 19, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_tank_64", -1, 24, 0.1f, BossSizeCategory::Medium},
	// {"monster_guardian", -1, 24, 0.1f, BossSizeCategory::Medium},
	{"monster_psxguardian", -1, 24, 0.1f, BossSizeCategory::Medium},
	{"monster_shamblerkl", -1, 24, 0.1f, BossSizeCategory::Medium},
	{"monster_guncmdrkl", -1, 24, 0.1f, BossSizeCategory::Medium},
	{"monster_widow2", 19, -1, 0.1f, BossSizeCategory::Medium},
	// {"monster_gm_arachnid", -1, 24, 0.1f, BossSizeCategory::Medium},
	{"monster_psxarachnid", -14, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_makronkl", 26, -1, 0.1f, BossSizeCategory::Medium}
};

constexpr boss_t BOSS_LARGE[] = {
	{"monster_carrier", 24, -1, 0.1f, BossSizeCategory::Large},
	{"monster_boss2", 19, -1, 0.1f, BossSizeCategory::Large},
	{"monster_boss5", -1, -1, 0.1f, BossSizeCategory::Large},
	{"monster_tank_64", -1, 24, 0.1f, BossSizeCategory::Large},
	{"monster_psxarachnid", 14, -1, 0.1f, BossSizeCategory::Large},
	{"monster_widow", -1, -1, 0.1f, BossSizeCategory::Large},
	// {"monster_guardian", -1, 24, 0.1f, BossSizeCategory::Large},
	{"monster_psxguardian", -1, -1, 0.1f, BossSizeCategory::Large},
	{"monster_shamblerkl", -1, 24, 0.1f, BossSizeCategory::Large},
	{"monster_boss5", -1, 24, 0.1f, BossSizeCategory::Large},
	{"monster_jorg", 30, -1, 0.1f, BossSizeCategory::Large}
};


// Definir constantes de tamaño para cada arreglo de jefes
constexpr size_t BOSS_SMALL_SIZE = std::size(BOSS_SMALL);
constexpr size_t BOSS_MEDIUM_SIZE = std::size(BOSS_MEDIUM);
constexpr size_t BOSS_LARGE_SIZE = std::size(BOSS_LARGE);

static const boss_t* GetBossList(const MapSize& mapSize, const std::string& mapname) {
	if (mapSize.isSmallMap || mapname == "q2dm4" || mapname == "q64/comm" || mapname == "test/test_kaiser") {
		return BOSS_SMALL;
	}

	if (mapSize.isMediumMap || mapname == "rdm8" || mapname == "xdm1") {
		if (mapname == "mgu6m3" || mapname == "rboss") {
			static std::vector<boss_t> filteredMediumBossList;
			if (filteredMediumBossList.empty()) {
				for (const auto& boss : BOSS_MEDIUM) {
					if (std::strcmp(boss.classname, "monster_guardian") != 0 &&
						std::strcmp(boss.classname, "monster_psxguardian") != 0) {
						filteredMediumBossList.emplace_back(boss);
					}
				}
			}
			return filteredMediumBossList.empty() ? nullptr : filteredMediumBossList.data();
		}
		return BOSS_MEDIUM;
	}

	if (mapSize.isBigMap || mapname == "test/spbox" || mapname == "q2ctf4") {
		if (mapname == "test/spbox" || mapname == "q2ctf4") {
			static std::vector<boss_t> filteredLargeBossList;
			if (filteredLargeBossList.empty()) {
				for (const auto& boss : BOSS_LARGE) {
					if (std::strcmp(boss.classname, "monster_boss5") != 0) {
						filteredLargeBossList.emplace_back(boss);
					}
				}
			}
			return filteredLargeBossList.empty() ? nullptr : filteredLargeBossList.data();
		}
		return BOSS_LARGE;
	}

	return nullptr;
}

static size_t GetBossListSize(const MapSize& mapSize, const std::string& mapname, const boss_t* boss_list) {
	if (boss_list == BOSS_SMALL) return BOSS_SMALL_SIZE;
	if (boss_list == BOSS_MEDIUM) return BOSS_MEDIUM_SIZE;
	if (boss_list == BOSS_LARGE) return BOSS_LARGE_SIZE;

	if ((mapname == "mgu6m3" || mapname == "rboss") && boss_list) {
		size_t size = 0;
		for (const auto& boss : BOSS_MEDIUM) {
			if (std::strcmp(boss.classname, "monster_guardian") != 0 &&
				std::strcmp(boss.classname, "monster_psxguardian") != 0) {
				size++;
			}
		}
		return size;
	}

	if ((mapname == "test/spbox" || mapname == "q2ctf4") && boss_list) {
		size_t size = 0;
		for (const auto& boss : BOSS_LARGE) {
			if (std::strcmp(boss.classname, "monster_boss5") != 0) {
				size++;
			}
		}
		return size;
	}

	return 0;
}



// Estructuras de arrays estáticos para reemplazar std::vectors
struct EligibleBosses {
	const boss_t* items[MAX_ELIGIBLE_BOSSES];
	size_t count = 0;

	void clear() { count = 0; }
	void add(const boss_t* boss) {
		if (count < MAX_ELIGIBLE_BOSSES) {
			items[count++] = boss;
		}
	}
};

// Array estático para bosses recientes
struct RecentBosses {
	const char* items[MAX_RECENT_BOSSES];
	size_t count = 0;

	void add(const char* boss) {
		if (count < MAX_RECENT_BOSSES) {
			items[count++] = boss;
		}
		else {
			// Desplazar elementos y agregar el nuevo al final
			for (size_t i = 1; i < MAX_RECENT_BOSSES; ++i) {
				items[i - 1] = items[i];
			}
			items[MAX_RECENT_BOSSES - 1] = boss;
		}
	}

	bool contains(const char* boss) const {
		for (size_t i = 0; i < count; ++i) {
			if (strcmp(items[i], boss) == 0) return true;
		}
		return false;
	}

	void clear() { count = 0; }
};


struct EligibleMonsters {
	const weighted_item_t* items[MAX_ELIGIBLE_MONSTERS];
	size_t count = 0;

	void clear() { count = 0; }
	void add(const weighted_item_t* monster) {
		if (count < MAX_ELIGIBLE_MONSTERS) {
			items[count++] = monster;
		}
	}
};

static RecentBosses recent_bosses;

// Función modificada para agregar un jefe reciente
static void AddRecentBoss(const char* classname) {
	if (classname == nullptr) return;
	recent_bosses.add(classname);
}




// Modifica G_HordePickBOSS para usar arrays estáticos
static const char* G_HordePickBOSS(const MapSize& mapSize, const std::string& mapname, int32_t waveNumber, edict_t* bossEntity) {
	static EligibleBosses eligible_bosses;
	static double cumulative_weights[MAX_ELIGIBLE_BOSSES];
	eligible_bosses.clear();
	double total_weight = 0.0;

	const boss_t* boss_list = GetBossList(mapSize, mapname);
	if (!boss_list) return nullptr;

	const size_t boss_list_size = GetBossListSize(mapSize, mapname, boss_list);
	if (boss_list_size == 0) return nullptr;

	// Recolectar bosses elegibles y calcular pesos acumulativos
	for (size_t i = 0; i < boss_list_size; ++i) {
		const boss_t& boss = boss_list[i];
		if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
			(waveNumber <= boss.max_level || boss.max_level == -1) &&
			!recent_bosses.contains(boss.classname)) {

			// Ajustar peso base según nivel y dificultad
			float adjusted_weight = boss.weight;

			// Boost para bosses apropiados al nivel
			if (waveNumber >= boss.min_level && waveNumber <= boss.min_level + 5) {
				adjusted_weight *= 1.3f;
			}

			// Ajuste por dificultad
			if (g_insane->integer || g_chaotic->integer) {
				if (boss.sizeCategory == BossSizeCategory::Large) {
					adjusted_weight *= 1.2f;
				}
			}

			// Penalización suave para bosses de nivel muy bajo
			if (boss.min_level != -1 && waveNumber > boss.min_level + 10) {
				adjusted_weight *= 0.8f;
			}

			total_weight += adjusted_weight;
			cumulative_weights[eligible_bosses.count] = total_weight;
			eligible_bosses.add(&boss);
		}
	}

	// Si no hay bosses elegibles, limpiar historial y reintentar
	if (eligible_bosses.count == 0) {
		recent_bosses.clear();
		total_weight = 0.0;

		for (size_t i = 0; i < boss_list_size; ++i) {
			const boss_t& boss = boss_list[i];
			if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
				(waveNumber <= boss.max_level || boss.max_level == -1)) {
				float adjusted_weight = boss.weight;
				total_weight += adjusted_weight;
				cumulative_weights[eligible_bosses.count] = total_weight;
				eligible_bosses.add(&boss);
			}
		}
	}

	if (eligible_bosses.count == 0) return nullptr;

	// Selección basada en peso usando búsqueda binaria
	double random_value = frandom() * total_weight;
	size_t left = 0;
	size_t right = eligible_bosses.count - 1;

	while (left < right) {
		size_t mid = (left + right) / 2;
		if (cumulative_weights[mid] < random_value) {
			left = mid + 1;
		}
		else {
			right = mid;
		}
	}

	const boss_t* chosen_boss = eligible_bosses.items[left];
	if (chosen_boss) {
		recent_bosses.add(chosen_boss->classname);
		bossEntity->bossSizeCategory = chosen_boss->sizeCategory;
		return chosen_boss->classname;
	}

	return nullptr;
}

struct picked_item_t {
	const weighted_item_t* item;
	float weight;
};

gitem_t* G_HordePickItem() {
	static const weighted_item_t* eligible_items[MAX_ELIGIBLE_ITEMS];
	static double cumulative_weights[MAX_ELIGIBLE_ITEMS];
	size_t eligible_count = 0;
	double total_weight = 0.0;

	// Recolectar items elegibles y calcular pesos
	for (const auto& item : items) {
		if ((item.min_level == -1 || g_horde_local.level >= item.min_level) &&
			(item.max_level == -1 || g_horde_local.level <= item.max_level)) {

			float adjusted_weight = item.weight;
			if (item.adjust_weight) {
				item.adjust_weight(item, adjusted_weight);
			}

			if (adjusted_weight > 0.0f && eligible_count < MAX_ELIGIBLE_ITEMS) {
				eligible_items[eligible_count] = &item;
				total_weight += adjusted_weight;
				cumulative_weights[eligible_count] = total_weight;
				eligible_count++;
			}
		}
	}

	if (eligible_count == 0) {
		return nullptr;
	}

	// Generar un valor aleatorio entre 0 y el peso total
	const double random_value = frandom() * total_weight;

	// Búsqueda binaria para encontrar el ítem seleccionado
	size_t left = 0;
	size_t right = eligible_count - 1;

	while (left < right) {
		size_t mid = (left + right) / 2;
		if (cumulative_weights[mid] < random_value) {
			left = mid + 1;
		}
		else {
			right = mid;
		}
	}

	const weighted_item_t* chosen_item = eligible_items[left];
	return chosen_item ? FindItemByClassname(chosen_item->classname) : nullptr;
}

int32_t WAVE_TO_ALLOW_FLYING;


// Keep the existing array
static const std::array<const char*, 10> flying_monster_classnames = {
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

// Variables que no cambian después de la inicialización
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

static constexpr float adjustFlyingSpawnProbability(int32_t flyingSpawns) {
	return (flyingSpawns > 0) ? 0.25f : 1.0f;
}


static bool IsFlyingMonster(const std::string& classname) {
	return flying_monsters_set.find(classname) != flying_monsters_set.end();
}

inline static bool IsMonsterEligible(const edict_t* spawn_point, const weighted_item_t& item, bool isFlyingMonster, int32_t currentWave, int32_t flyingSpawns) noexcept {
	if (flying_monsters_mode) {
		return isFlyingMonster &&
			!(spawn_point->style == 1 && !isFlyingMonster) &&
			!(item.min_level > currentWave || (item.max_level != -1 && item.max_level < currentWave));
	}
	else {
		return !(spawn_point->style == 1 && !isFlyingMonster) &&
			!(item.min_level > currentWave || (item.max_level != -1 && item.max_level < currentWave));
	}
}

inline static double CalculateMonsterWeight(const weighted_item_t& item, bool isFlyingMonster, float adjustmentFactor) noexcept {
	if (flying_monsters_mode && isFlyingMonster) {
		return item.weight * 1.5; // Aumentar el peso para preferir monstruos voladores
	}
	return item.weight;
}


// Function to reset spawn point attempts and cooldown
static void ResetSingleSpawnPointAttempts(edict_t* spawn_point) noexcept {
	auto& data = spawnPointsData[spawn_point];
	data.attempts = 0;
	data.cooldown = level.time;
}

constexpr gtime_t CLEANUP_THRESHOLD = 3_sec;

// Función modificada sin lanzar excepciones
static SpawnPointData& EnsureSpawnPointDataExists(edict_t* spawn_point) {
	if (!spawn_point) {
		gi.Com_PrintFmt("Warning: Attempted to ensure spawn point data for a nullptr.\n");
		// Manejar el caso de manera segura sin retornar una referencia a un objeto estático
		static SpawnPointData dummy;
		return dummy;
	}

	auto [insert_it, inserted] = spawnPointsData.emplace(spawn_point, SpawnPointData());
	return insert_it->second;
}

constexpr size_t MAX_SPAWN_POINTS_DATA = 30; // Define un límite razonable

static void CleanUpSpawnPointsData() {
	const gtime_t currentTime = level.time;

	// Remove spawn points that are temporarily disabled and past cooldown
	for (auto it = spawnPointsData.begin(); it != spawnPointsData.end(); ) {
		if (it->second.isTemporarilyDisabled && currentTime > it->second.cooldownEndsAt + CLEANUP_THRESHOLD) {
			gi.Com_PrintFmt("Removed spawn_point at address {} due to extended inactivity.\n", static_cast<void*>(it->first));
			it = spawnPointsData.erase(it);
		}
		else {
			++it;
		}
	}

	// Limit the size of the container
	if (spawnPointsData.size() > MAX_SPAWN_POINTS_DATA) {
		gi.Com_Print("WARNING: spawnPointsData exceeded maximum size. Clearing data.\n");
		spawnPointsData.clear();
	}
}


static void UpdateCooldowns(edict_t* spawn_point, const char* chosen_monster) {
	auto& data = spawnPointsData[spawn_point];
	data.lastSpawnTime = level.time;
	data.lastSpawnedMonsterClassname = chosen_monster;
	data.isTemporarilyDisabled = true;
	data.cooldownEndsAt = level.time + SPAWN_POINT_COOLDOWN;
}

// Function to increase spawn attempts and adjust cooldown as necessary
static void IncreaseSpawnAttempts(edict_t* spawn_point) {
	auto& data = spawnPointsData[spawn_point];
	data.attempts++;

	if (data.attempts >= 6) {
		gi.Com_PrintFmt("PRINT: SpawnPoint at position ({}, {}, {}) inactivated for 10 seconds.\n", spawn_point->s.origin[0], spawn_point->s.origin[1], spawn_point->s.origin[2]);
		data.isTemporarilyDisabled = true; // Temporarily deactivate
		data.cooldownEndsAt = level.time + 10_sec;
	}
	else if (data.attempts % 3 == 0) {
		data.cooldownEndsAt = std::max(data.cooldownEndsAt + (data.cooldownEndsAt / 2), 2.5_sec);
	}
}

// Estructura para pasar datos adicionales a la función de filtro
struct FilterData {
	const edict_t* ignore_ent;
	int count;
};

// Función de filtro optimizada
// Modified SpawnPointFilter function
static BoxEdictsResult_t SpawnPointFilter(edict_t* ent, void* data) {
	FilterData* filter_data = static_cast<FilterData*>(data);

	// Ignore the specified entity (if exists)
	if (ent == filter_data->ignore_ent) {
		return BoxEdictsResult_t::Skip;
	}

	// Check if the entity is a player or bot
	if (ent->client && ent->inuse) {
		filter_data->count++;
		return BoxEdictsResult_t::End; // Stop searching if a player or bot is found
	}

	// Check if the entity is a monster (using the SVF_MONSTER flag)
	if (ent->svflags & SVF_MONSTER) {
		filter_data->count++;
		return BoxEdictsResult_t::End; // Stop searching if a monster is found
	}

	return BoxEdictsResult_t::Skip;
}

// ¿Está el punto de spawn ocupado?
static bool IsSpawnPointOccupied(const edict_t* spawn_point, const edict_t* monster = nullptr) {
	// Verificar primero si el punto está en cooldown
	const auto it = spawnPointsData.find(const_cast<edict_t*>(spawn_point));
	if (it != spawnPointsData.end() && it->second.isTemporarilyDisabled) {
		if (level.time < it->second.cooldownEndsAt) {
			return true;
		}
	}

	// Factor de multiplicación para garantizar espacio adicional
	constexpr float space_multiplier = 2.0f;  // Ajustado a 1.5 para balance

	// Calcular dimensiones del área a verificar
	vec3_t mins, maxs;
	if (monster) {
		// Usar dimensiones del monstruo con factor de espacio
		mins = spawn_point->s.origin + (monster->mins * space_multiplier);
		maxs = spawn_point->s.origin + (monster->maxs * space_multiplier);
	}
	else {
		// Dimensiones por defecto para jugadores con factor de espacio
		mins = spawn_point->s.origin + vec3_t{ -16 * space_multiplier, -16 * space_multiplier, -24 * space_multiplier };
		maxs = spawn_point->s.origin + vec3_t{ 16 * space_multiplier, 16 * space_multiplier, 32 * space_multiplier };
	}

	// Verificar colisiones con jugadores y monstruos activos
	for (const auto ent : active_monsters()) {
		if (ent != monster && // Ignorar la propia entidad si se proporciona
			boxes_intersect(mins, maxs, ent->absmin, ent->absmax)) {
			return true; // Punto ocupado
		}
	}

	// Verificar colisiones con jugadores
	for (auto player : active_players()) {
		if (boxes_intersect(mins, maxs, player->absmin, player->absmax)) {
			return true; // Punto ocupado por un jugador
		}
	}

	return false; // Punto libre
}
const char* G_HordePickMonster(edict_t* spawn_point) {
	// Verificar el cooldown del spawn point de manera más estricta
	auto& data = spawnPointsData[spawn_point];
	if (data.isTemporarilyDisabled) {
		if (level.time < data.cooldownEndsAt) {
			return nullptr;
		}
		// Si el cooldown ha terminado, resetear el estado
		data.isTemporarilyDisabled = false;
		data.attempts = 0;
	}

	// Verificar si el punto está realmente ocupado
	if (IsSpawnPointOccupied(spawn_point)) {
		IncreaseSpawnAttempts(spawn_point);
		return nullptr;
	}

	// Arrays estáticos para almacenar monstruos elegibles y sus pesos
	static const weighted_item_t* eligible_monsters[MAX_ELIGIBLE_MONSTERS];
	static double cumulative_weights[MAX_ELIGIBLE_MONSTERS];
	size_t eligible_count = 0;
	double total_weight = 0.0;

	// Calcular factores de ajuste una sola vez
	const int32_t flyingSpawns = countFlyingSpawns();
	const float adjustmentFactor = adjustFlyingSpawnProbability(flyingSpawns);
	const bool isLateGame = g_horde_local.level >= 20;
	const bool isMidGame = g_horde_local.level >= 10 && g_horde_local.level < 20;

	// Colectar monstruos elegibles y calcular pesos en un solo paso
	for (const auto& monster : monsters) {
		const bool isFlyingMonster = IsFlyingMonster(monster.classname);

		if (!IsMonsterEligible(spawn_point, monster, isFlyingMonster, g_horde_local.level, flyingSpawns)) {
			continue;
		}

		// Base weight
		double weight = monster.weight;

		// Ajustes de peso basados en el modo de juego
		if (flying_monsters_mode) {
			if (isFlyingMonster) {
				weight *= 2.0; // Boost para monstruos voladores en modo volador
			}
			else {
				weight *= 0.5; // Reducción para no voladores en modo volador
			}
		}

		// Ajustes de peso basados en el nivel del juego
		if (isLateGame) {
			if (monster.min_level >= 15) {
				weight *= 1.3; // Boost para monstruos de nivel alto en late game
			}
		}
		else if (isMidGame) {
			if (monster.min_level >= 8 && monster.min_level <= 15) {
				weight *= 1.2; // Boost para monstruos de nivel medio en mid game
			}
		}
		else {
			if (monster.min_level <= 5) {
				weight *= 1.1; // Ligero boost para monstruos básicos en early game
			}
		}

		// Ajustes por dificultad
		if (g_insane->integer || g_chaotic->integer) {
			if (monster.min_level >= 10) {
				weight *= 1.25; // Boost para monstruos más difíciles en modos duros
			}
		}

		// Ajuste final por spawn point type
		if (spawn_point->style == 1 && isFlyingMonster) {
			weight *= 1.5; // Boost para voladores en spawn points aéreos
		}

		weight *= adjustmentFactor;

		if (weight > 0.0 && eligible_count < MAX_ELIGIBLE_MONSTERS) {
			eligible_monsters[eligible_count] = &monster;
			total_weight += weight;
			cumulative_weights[eligible_count] = total_weight;
			eligible_count++;
		}
	}

	// Verificar si hay monstruos elegibles
	if (eligible_count == 0) {
		IncreaseSpawnAttempts(spawn_point);
		return nullptr;
	}

	// Selección basada en peso usando búsqueda binaria
	double random_value = frandom() * total_weight;
	size_t selected_index = 0;

	// Búsqueda binaria optimizada
	{
		size_t left = 0;
		size_t right = eligible_count - 1;

		while (left < right) {
			size_t mid = (left + right) / 2;
			if (cumulative_weights[mid] < random_value) {
				left = mid + 1;
			}
			else {
				right = mid;
			}
		}
		selected_index = left;
	}

	// Verificar selección válida
	if (selected_index < eligible_count) {
		const weighted_item_t* chosen_monster = eligible_monsters[selected_index];
		UpdateCooldowns(spawn_point, chosen_monster->classname);
		ResetSingleSpawnPointAttempts(spawn_point);
		return chosen_monster->classname;
	}

	return nullptr;
}
void Horde_PreInit() {
	dm_monsters = gi.cvar("dm_monsters", "0", CVAR_SERVERINFO);
	g_horde = gi.cvar("horde", "0", CVAR_LATCH);
	//gi.Com_Print("After starting a normal server type: starthorde to start a game.\n");

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
		gi.cvar_set("timelimit", "50");
		gi.cvar_set("fraglimit", "0");
		gi.cvar_set("sv_target_id", "1");
		gi.cvar_set("g_speedstuff", "2.3f");
		gi.cvar_set("sv_eyecam", "1");
		gi.cvar_set("g_dm_instant_items", "1");
		gi.cvar_set("g_disable_player_collision", "1");
		gi.cvar_set("g_dm_no_self_damage", "1");
		gi.cvar_set("g_allow_techs", "1");
		gi.cvar_set("g_no_nukes", "0");
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
		gi.cvar_set("g_bfgslide", "1");
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
	return std::count_if(active_players().begin(), active_players().end(),
		[](const edict_t* player) {
			return player->client->resp.ctf_team == CTF_TEAM1 && !(player->svflags & SVF_BOT);
		});
}

void VerifyAndAdjustBots() {
	const MapSize mapSize = GetMapSize(level.mapname);
	const int32_t humanPlayers = GetNumHumanPlayers();
	const int32_t spectPlayers = GetNumSpectPlayers();
	const int32_t baseBots = mapSize.isBigMap ? 6 : 4;
	int32_t requiredBots = baseBots + spectPlayers;
	requiredBots = std::max(requiredBots, baseBots);
	gi.cvar_set("bot_minClients", std::to_string(requiredBots).c_str());
}

#include <chrono>
void InitializeWaveSystem() noexcept;

// Función para precargar todos los ítems y jefes
static void PrecacheItemsAndBosses() noexcept {
	std::unordered_set<std::string_view> unique_classnames;
	unique_classnames.reserve(sizeof(items) / sizeof(items[0]) + sizeof(monsters) / sizeof(monsters[0]) +
		sizeof(BOSS_SMALL) / sizeof(BOSS_SMALL[0]) + sizeof(BOSS_MEDIUM) / sizeof(BOSS_MEDIUM[0]) +
		sizeof(BOSS_LARGE) / sizeof(BOSS_LARGE[0]));

	for (const auto& item : items) unique_classnames.emplace(item.classname);
	for (const auto& monster : monsters) unique_classnames.emplace(monster.classname);
	for (const auto& boss : BOSS_SMALL) unique_classnames.emplace(boss.classname);
	for (const auto& boss : BOSS_MEDIUM) unique_classnames.emplace(boss.classname);
	for (const auto& boss : BOSS_LARGE) unique_classnames.emplace(boss.classname);

	for (const auto& classname : unique_classnames) {
		PrecacheItem(FindItemByClassname(classname.data()));
	}
}


static void PrecacheAllSounds() noexcept {
	sound_tele3.assign("misc/r_tele3.wav");
	sound_klaxon2.assign("world/klaxon2.wav");
	sound_tele_up.assign("misc/tele_up.wav");
	sound_incoming.assign("world/incoming.wav");
	sound_yelforce.assign("world/yelforce.wav");

	sound_action_fail.assign("nav_editor/action_fail.wav");
	sound_roar1.assign("makron/roar1.wav");
	sound_ack.assign("zortemp/ack.wav");
	sound_spawn1.assign("misc/spawn1.wav");
	sound_voice3.assign("makron/voice3.wav");
	sound_v_fac3.assign("world/v_fac3.wav");
}

static void PrecacheAllMonsters() noexcept {
	for (const auto& monster : monsters) {
		edict_t* e = G_Spawn();
		if (!e) {
			gi.Com_Print("Error: Failed to spawn monster for precaching.\n");
			continue;
		}
		e->classname = monster.classname;
		e->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
		ED_CallSpawn(e);
		PrecacheItem(const_cast<gitem_t*>(FindItemByClassname(monster.classname)));
		G_FreeEdict(e);
	}
}
// Array de sonidos constante
constexpr std::array<std::string_view, 6> WAVE_SOUNDS = {
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
		gi.soundindex(sound.data());
	}
}

//Capping resets on map end

static bool hasBeenReset = false;
void AllowReset() {
	hasBeenReset = false;
}

void Horde_Init() {

	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end();) {
		edict_t* boss = *it;
		if (boss && boss->inuse) {
			// Asegurar que el boss está marcado como manejado
			boss->monsterinfo.BOSS_DEATH_HANDLED = true;
			OnEntityRemoved(boss);
		}
		it = auto_spawned_bosses.erase(it);
	}
	auto_spawned_bosses.clear();

	// Precache items, bosses, monsters, and sounds
	PrecacheItemsAndBosses();
	PrecacheAllMonsters();
	PrecacheAllSounds();
	PrecacheWaveSounds();

	// Inicializar otros sistemas de la horda (e.g., sistema de oleadas)
	InitializeWaveSystem();
	last_wave_number = 0;

	AllowReset();
	// Resetear el estado del juego para la horda
	ResetGame();

	gi.Com_PrintFmt("PRINT: Horde game state initialized with all necessary resources precached.\n");
}

// Constantes para mejorar la legibilidad y mantenibilidad
constexpr int MIN_VELOCITY = -800;
constexpr int MAX_VELOCITY = 800;
constexpr int MIN_VERTICAL_VELOCITY = 400;
constexpr int MAX_VERTICAL_VELOCITY = 950;
constexpr int VERTICAL_VELOCITY_RANDOM_RANGE = 300;

// Función auxiliar para generar velocidad aleatoria
static vec3_t GenerateRandomVelocity(int minHorizontal, int maxHorizontal, int minVertical, int maxVertical) {
	return {
		static_cast<float>(std::uniform_int_distribution<>(minHorizontal, maxHorizontal)(mt_rand)),
		static_cast<float>(std::uniform_int_distribution<>(minHorizontal, maxHorizontal)(mt_rand)),
		static_cast<float>(std::uniform_int_distribution<>(minVertical, maxVertical)(mt_rand))
	};
}

// Función auxiliar para configurar un ítem soltado
static void SetupDroppedItem(edict_t* item, const vec3_t& origin, const vec3_t& velocity, bool applyFlags) {
	item->s.origin = origin;
	item->velocity = velocity;

	if (applyFlags) {
		item->spawnflags &= ~SPAWNFLAG_ITEM_DROPPED;
		item->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
	}

	item->movetype = MOVETYPE_BOUNCE;
	item->s.effects |= EF_GIB;
	item->flags &= ~FL_RESPAWN;
}

// Función auxiliar para seleccionar un arma apropiada según el nivel
static const char* SelectBossWeaponDrop(int32_t wave_level) {
	// Array fijo de armas disponibles con sus niveles mínimos requeridos
	static const std::array<std::pair<const char*, int32_t>, 8> weapons = { {
		{"weapon_hyperblaster", 12},
		{"weapon_railgun", 24},
		{"weapon_rocketlauncher", 14},
		{"weapon_phalanx", 16},
		{"weapon_boomer", 14},
		{"weapon_plasmabeam", 17},
		{"weapon_disintegrator", 28},
		{"weapon_bfg", 24}
	} };

	// Filtrar armas que son de nivel inferior o igual al actual
	std::vector<const char*> eligible_weapons;
	eligible_weapons.reserve(weapons.size()); // Reservar espacio para evitar reallocaciones

	for (const auto& [weapon, min_level] : weapons) {
		if (min_level <= wave_level) {
			eligible_weapons.push_back(weapon);
		}
	}

	// Si no hay armas elegibles, retornar nullptr explícitamente
	if (eligible_weapons.empty()) {
		return nullptr;
	}

	// Usar mt_rand para una mejor generación de números aleatorios
	// Asegurarnos de que el índice está dentro del rango válido
	size_t random_index;
	if (eligible_weapons.size() == 1) {
		random_index = 0;
	}
	else {
		random_index = mt_rand() % eligible_weapons.size();
	}

	// Verificación adicional de seguridad
	if (random_index >= eligible_weapons.size()) {
		return nullptr;
	}

	return eligible_weapons[random_index];
}

//CRAZY BOSS ANIMATION DROP ITEMS
constexpr float ITEM_ROTATION_SPEED = 550.0f;       // Reducida para más control
constexpr float ITEM_ORBIT_RADIUS = 254.0f;
constexpr float ITEM_VERTICAL_OFFSET = 12.0f;        // Reducido para control inicial
constexpr float SCATTER_SPEED = 650.0f;             // Reducida para dispersión más suave
constexpr float RISE_SPEED = 70.0f;                 // Velocidad de elevación
constexpr float MAX_HEIGHT = 150.0f;                // Altura máxima antes de dispersar
constexpr float ABSOLUTE_MAX_HEIGHT = 256.0f; // Altura máxima absoluta

// Estado de la rotación
enum class ItemState {
	RISING,         // Nueva fase inicial
	ORBITING,
	SCATTERING,
	SCATTERED
};

// Declarar el think function antes de usarlo
void(Item_Scatter_Think) (edict_t* ent);

THINK(Item_Orbit_Think) (edict_t* ent) -> void {
	if (!ent || !ent->owner) {
		G_FreeEdict(ent);
		return;
	}

	// Control de estado basado en altura
	float height_diff = ent->s.origin[2] - ent->owner->s.origin[2];
	ItemState current_state = static_cast<ItemState>(ent->count);

	switch (current_state) {
	case ItemState::RISING: {
		// Fase de elevación con límite más estricto
		if (height_diff < MAX_HEIGHT) {
			float rise_amount = std::min(RISE_SPEED * gi.frame_time_s,
				MAX_HEIGHT - height_diff);
			ent->s.origin[2] += rise_amount;
		}
		else {
			ent->count = static_cast<int>(ItemState::ORBITING);
		}
		break;
	}

	case ItemState::ORBITING: {
		// Verificar si es tiempo de dispersar
		if (level.time >= ent->owner->timestamp) {
			// Calcular dirección de dispersión más horizontal
			vec3_t scatter_dir = ent->s.origin - ent->owner->s.origin;
			scatter_dir.normalize();

			// Reducir componente vertical de la dispersión
			scatter_dir[2] *= 0.2f; // Reduce movimiento vertical
			scatter_dir.normalize();

			// Agregar ligera variación horizontal
			scatter_dir += vec3_t{ crandom() * 0.15f, crandom() * 1.15f, 0.0f };
			scatter_dir.normalize();

			ent->velocity = scatter_dir * SCATTER_SPEED;

			// Guardar la altura inicial en pos1.z (que está disponible en edict_t)
			ent->pos1[2] = ent->owner->s.origin[2];
			ent->owner = nullptr;
			ent->think = Item_Scatter_Think;
			ent->nextthink = level.time + FRAME_TIME_S;
			return;
		}

		// Mantener altura constante durante la órbita
		vec3_t target_pos = ent->owner->s.origin;
		target_pos[2] += MAX_HEIGHT;

		// Interpolar suavemente hacia la altura objetivo
		float height_diff = target_pos[2] - ent->s.origin[2];
		ent->s.origin[2] += height_diff * 0.1f;

		// Comportamiento de órbita normal
		vec3_t forward, right, up;
		AngleVectors(ent->owner->s.angles, forward, right, up);

		float degrees = (ITEM_ROTATION_SPEED * gi.frame_time_s) + ent->owner->delay;
		vec3_t diff = ent->owner->s.origin - ent->s.origin;
		diff[2] = 0; // Mantener altura constante durante órbita

		vec3_t vec = RotatePointAroundVector(up, diff, degrees);
		ent->s.angles[1] += degrees;

		vec3_t new_origin = ent->owner->s.origin - vec;
		new_origin[2] = ent->s.origin[2]; // Mantener altura actual

		trace_t tr = gi.traceline(ent->s.origin, new_origin, ent, MASK_SOLID | CONTENTS_PLAYERCLIP);
		if (tr.fraction == 1.0f) {
			ent->s.origin = new_origin;
		}
		break;
	}
	}

	// Comprobar agua
	ent->watertype = gi.pointcontents(ent->s.origin);
	if (ent->watertype & MASK_WATER)
		ent->waterlevel = WATER_FEET;

	ent->nextthink = level.time + FRAME_TIME_S;
	gi.linkentity(ent);
}

THINK(Item_Scatter_Think) (edict_t* ent) -> void {
	if (!ent) {
		G_FreeEdict(ent);
		return;
	}

	// Limitar la velocidad vertical para evitar que suban demasiado
	if (ent->velocity[2] > 100.0f) {
		ent->velocity[2] = 100.0f;
	}

	// Comprobar altura máxima absoluta usando pos1.z como referencia
	if (ent->s.origin[2] > ent->pos1[2] + ABSOLUTE_MAX_HEIGHT) {
		ent->s.origin[2] = ent->pos1[2] + ABSOLUTE_MAX_HEIGHT;
		ent->velocity[2] = 0; // Detener movimiento vertical
	}

	// Velocidad mínima para evitar que se peguen
	constexpr float MIN_VELOCITY = 60.0f;

	// Comprobar colisiones antes de mover
	vec3_t next_pos = ent->s.origin + (ent->velocity * gi.frame_time_s);
	trace_t tr = gi.traceline(ent->s.origin, next_pos, ent, MASK_SOLID | CONTENTS_PLAYERCLIP);

	if (tr.fraction < 1.0f) {
		// Si estamos empezando en sólido, intentar "empujar" hacia afuera
		if (tr.startsolid || tr.allsolid) {
			// Intentar mover en dirección opuesta
			ent->velocity = -ent->velocity;
			ent->velocity *= 1.5f; // Dar un empujón extra
			return;
		}

		// Calcular rebote con más energía para evitar pegarse
		vec3_t bounce_vel = SlideClipVelocity(ent->velocity, tr.plane.normal, 1.5f);

		// Si el rebote es muy vertical, agregar componente horizontal
		if (fabs(tr.plane.normal[2]) > 0.7f) {
			bounce_vel[0] += crandom() * 100.0f;
			bounce_vel[1] += crandom() * 100.0f;
		}

		// Si la velocidad es muy baja, dar un empujón en dirección aleatoria
		if (bounce_vel.length() < MIN_VELOCITY) {
			bounce_vel[0] += crandom() * MIN_VELOCITY;
			bounce_vel[1] += crandom() * MIN_VELOCITY;
			if (bounce_vel[2] < MIN_VELOCITY * 0.5f)
				bounce_vel[2] += MIN_VELOCITY * 0.5f;
		}

		ent->velocity = bounce_vel;

		// Movernos un poco más lejos de la superficie de colisión
		ent->s.origin = tr.endpos + (tr.plane.normal * 8.0f);
	}
	else {
		ent->s.origin = next_pos;
	}

	// Reducir velocidad más gradualmente
	float speed = ent->velocity.length();
	if (speed > MIN_VELOCITY) {
		ent->velocity[0] *= 0.99f;
		ent->velocity[1] *= 0.99f;
		ent->velocity[2] *= 0.97f; // Más fricción vertical
	}

	// Gravedad más suave, pero asegurándonos de mantener velocidad mínima
	ent->velocity[2] -= 175.0f * gi.frame_time_s;

	// Verificar si la velocidad es muy baja y el item está cerca de una pared
	if (speed < MIN_VELOCITY) {
		trace_t side_traces[4];
		vec3_t check_dirs[4] = {
			{1, 0, 0}, {-1, 0, 0},
			{0, 1, 0}, {0, -1, 0}
		};

		bool near_wall = false;
		for (int i = 0; i < 4; i++) {
			vec3_t check_pos = ent->s.origin + (check_dirs[i] * 10.0f);
			side_traces[i] = gi.traceline(ent->s.origin, check_pos, ent, MASK_SOLID | CONTENTS_PLAYERCLIP);
			if (side_traces[i].fraction < 1.0f) {
				near_wall = true;
				break;
			}
		}

		if (near_wall) {
			// Dar un empujón aleatorio si estamos cerca de una pared
			ent->velocity[0] += crandom() * MIN_VELOCITY * 2.0f;
			ent->velocity[1] += crandom() * MIN_VELOCITY * 2.0f;
			ent->velocity[2] += MIN_VELOCITY;
		}
	}

	// Solo detenerse si realmente tiene poca velocidad y no está cerca de paredes
	if (ent->velocity.length() < 5.0f) {
		trace_t ground_tr = gi.traceline(ent->s.origin, ent->s.origin + vec3_t{ 0, 0, -16.0f },
			ent, MASK_SOLID | CONTENTS_PLAYERCLIP);
		if (ground_tr.fraction < 1.0f) {
			ent->think = nullptr;
			ent->nextthink = {};
			ent->movetype = MOVETYPE_TOSS;
			return;
		}
	}

	ent->nextthink = level.time + FRAME_TIME_S;
	gi.linkentity(ent);
}

void BossDeathHandler(edict_t* boss) {
	if (!g_horde->integer ||
		!boss ||
		!boss->inuse ||
		!boss->monsterinfo.IS_BOSS ||
		boss->monsterinfo.BOSS_DEATH_HANDLED ||
		boss->health > 0) {
		return;
	}

	boss->monsterinfo.BOSS_DEATH_HANDLED = true;

	OnEntityDeath(boss);
	OnEntityRemoved(boss);
	auto_spawned_bosses.erase(boss);



	// Crear punto central invisible para la rotación
	edict_t* center_point = G_Spawn();
	center_point->s.origin = boss->s.origin;
	center_point->s.angles = boss->s.angles;
	center_point->delay = 0;
	// Tiempo en que los items comenzarán a dispersarse
	center_point->timestamp = level.time + 2_sec;
	center_point->think = G_FreeEdict;
	// Dar tiempo extra para que los items se dispersen
	center_point->nextthink = level.time + 3_sec;

	static const std::array<const char*, 7> itemsToDrop = {
		"item_adrenaline", "item_pack", "item_doppleganger",
		"item_sphere_defender", "item_armor_combat", "item_bandolier",
		"item_invulnerability"
	};

	// Dropear arma especial con rotación
	if (const char* weapon_classname = SelectBossWeaponDrop(current_wave_level)) {
		if (edict_t* weapon = Drop_Item(boss, FindItemByClassname(weapon_classname))) {
			float angle = frandom() * PI * 2;
			weapon->s.origin = boss->s.origin + vec3_t{
				cosf(angle) * ITEM_ORBIT_RADIUS,
				sinf(angle) * ITEM_ORBIT_RADIUS,
				ITEM_VERTICAL_OFFSET
			};

			weapon->movetype = MOVETYPE_FLY;
			weapon->s.effects |= EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER;
			weapon->s.renderfx |= RF_GLOW;
			weapon->s.alpha = 0.85f;
			weapon->s.scale = 1.25f;
			weapon->flags &= ~FL_RESPAWN;
			weapon->owner = center_point;
			weapon->think = Item_Orbit_Think;
			weapon->nextthink = level.time + FRAME_TIME_S;
		}
	}

	// Items normales con rotación
	std::array<const char*, 7> shuffledItems = itemsToDrop;
	for (int i = 6; i > 0; i--) {
		int j = mt_rand() % (i + 1);
		if (i != j) {
			std::swap(shuffledItems[i], shuffledItems[j]);
		}
	}

	for (size_t i = 0; i < shuffledItems.size(); i++) {
		if (edict_t* item = Drop_Item(boss, FindItemByClassname(shuffledItems[i]))) {
			// Distribuir items en círculo más pequeño inicialmente
			float angle = (static_cast<float>(i) / shuffledItems.size()) * PI * 2;
			item->s.origin = boss->s.origin + vec3_t{
				cosf(angle) * (ITEM_ORBIT_RADIUS * 0.5f),
				sinf(angle) * (ITEM_ORBIT_RADIUS * 0.5f),
				ITEM_VERTICAL_OFFSET
			};

			item->movetype = MOVETYPE_FLY;
			item->s.effects |= EF_GIB;
			item->flags &= ~FL_RESPAWN;
			item->owner = center_point;
			item->think = Item_Orbit_Think;
			item->nextthink = level.time + FRAME_TIME_S;
			item->count = static_cast<int>(ItemState::RISING); // Iniciar en estado RISING
		}
	}

	boss->takedamage = false;

	static constexpr std::array<const char*, 4> flyingBossTypes = {
		"monster_boss2", "monster_carrier",
		"monster_carrier_mini", "monster_boss2kl"
	};

	for (const auto& bossType : flyingBossTypes) {
		if (strcmp(boss->classname, bossType) == 0) {
			flying_monsters_mode = false;
			break;
		}
	}
}

void boss_die(edict_t* boss) {
	if (!boss || !boss->inuse) {
		return;
	}

	// Verificación más estricta para la muerte del boss
	if (g_horde->integer &&
		boss->monsterinfo.IS_BOSS &&
		boss->health <= 0 &&
		auto_spawned_bosses.find(boss) != auto_spawned_bosses.end() &&
		!boss->monsterinfo.BOSS_DEATH_HANDLED) {

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
		if (ent->monsterinfo.IS_BOSS && ent->health <= 0) {
			if (auto_spawned_bosses.find(ent) != auto_spawned_bosses.end() && !ent->monsterinfo.BOSS_DEATH_HANDLED) {
				boss_die(ent);
			}
		}
	}
	gi.Com_Print("DEBUG: All monsters are dead.\n");
	return true;
}

static void CheckAndRestoreMonsterAlpha(edict_t* ent) {
	// Si la entidad no es válida o no es un monstruo, retornar
	if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER)) {
		return;
	}

	// Si el monstruo está vivo pero tiene alpha reducido (está en fade)
	if (ent->health > 0 && !ent->deadflag && ent->s.alpha < 1.0f) {
		// Restaurar el alpha y otros estados relevantes
		ent->s.alpha = 0.0f;
		ent->s.renderfx &= ~RF_TRANSLUCENT;

		// Asegurar que el monstruo puede tomar daño
		ent->takedamage = true;

		// Actualizar la entidad
		gi.linkentity(ent);

		//gi.Com_PrintFmt("PRINT: Restored alpha for monster {}\n",
		//	ent->classname);
	}
}


// Constante para el tiempo de vida del fade
constexpr gtime_t FADE_LIFESPAN = 1_sec;

static THINK(fade_out_think)(edict_t* self) -> void {
	// Si el monstruo está vivo, restaurar su estado
	if (self->health > 0 && !self->deadflag) {
		CheckAndRestoreMonsterAlpha(self);
		//	self->think = monster_think;
		self->nextthink = level.time + FRAME_TIME_MS;
		self->is_fading_out = false;  // Usar bool
		return;
	}

	if (level.time >= self->timestamp) {
		self->is_fading_out = false;  // Limpiar el bool antes de liberar
		G_FreeEdict(self);
		return;
	}

	// Calcular el factor de fade usando el mismo método que spawngrow
	float t = 1.f - ((level.time - self->teleport_time).seconds() / self->wait);
	self->s.alpha = t * t; // Usar t^2 para un fade más suave como spawngrow

	self->nextthink = level.time + FRAME_TIME_MS;
}


static void StartFadeOut(edict_t* ent) {
	// No iniciar fade out si el monstruo está vivo o ya está en fade
	if (ent->health > 0 && !ent->deadflag || ent->is_fading_out) {
		return;
	}

	// Configurar tiempos
	ent->teleport_time = level.time;
	ent->timestamp = level.time + FADE_LIFESPAN;
	ent->wait = FADE_LIFESPAN.seconds();

	// Configurar pensamiento
	ent->think = fade_out_think;
	ent->nextthink = level.time + FRAME_TIME_MS;

	// Marcar que está en proceso de fade
	ent->is_fading_out = true;

	// Configurar estados
	ent->solid = SOLID_NOT;
	ent->movetype = MOVETYPE_NONE;
	ent->takedamage = false;
	ent->svflags &= ~SVF_NOCLIENT;
	ent->s.renderfx &= ~RF_DOT_SHADOW;

	// Asegurar que la entidad está enlazada
	gi.linkentity(ent);
}

void Horde_CleanBodies() {
	int32_t cleaned_count = 0;

	// Cached model indices para mejor rendimiento
	static int32_t widow2_model = -1;
	static int32_t head_models[3] = { -1, -1, -1 };

	// Inicializar índices de modelos solo una vez
	if (widow2_model == -1) {
		widow2_model = gi.modelindex("models/monsters/blackwidow2/tris.md2");
		head_models[0] = gi.modelindex("models/objects/gibs/head/tris.md2");
		head_models[1] = gi.modelindex("models/objects/gibs/head2/tris.md2");
		head_models[2] = gi.modelindex("models/objects/gibs/skull/tris.md2");
	}

	for (auto ent : active_or_dead_monsters()) {
		// Skip entidades que ya están en proceso de fade
		if (ent->is_fading_out) {
			continue;
		}

		// Función helper para verificar si es una cabeza
		auto is_head_model = [&](int32_t model_index) {
			return std::find(std::begin(head_models), std::end(head_models), model_index) != std::end(head_models);
			};

		// Verificar condiciones para limpieza
		bool should_clean = false;

		// Verificar si es un monstruo muerto O si es widow2 muerta
		if ((ent->svflags & SVF_DEADMONSTER) || ent->health <= 0 ||
			(ent->s.modelindex == widow2_model && ent->health <= 0)) {
			should_clean = true;
		}
		// Verificar si es una cabeza con movetype bounce
		else if (is_head_model(ent->s.modelindex) && ent->movetype == MOVETYPE_BOUNCE) {
			should_clean = true;
		}

		if (should_clean) {
			// Manejar muerte de jefes si es necesario
			if (ent->monsterinfo.IS_BOSS &&
				!ent->monsterinfo.BOSS_DEATH_HANDLED) {
				BossDeathHandler(ent);
			}
			else {
				OnEntityDeath(ent);
			}

			// Limpieza y fade out
			auto_spawned_bosses.erase(ent);
			StartFadeOut(ent);
			cleaned_count++;
		}
	}

	if (cleaned_count > 0) {
		gi.Com_PrintFmt("Marked {} monster bodies and heads for fade out\n", cleaned_count);
	}
}
// spawning boss origin
std::unordered_map<std::string, std::array<int, 3>> mapOrigins = {
	{"q2dm1", {1184, 568, 704}},
	{"rdm4", {-336, 2456, -288}},
	{"rdm8", {-1516, 976, -156}},
	{"rdm9", {-984, -80, 232}},
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
static const std::unordered_map<std::string_view, std::string_view> bossMessagesMap = {
	{"monster_boss2", "***** Boss incoming! Hornet is here, ready for some fresh Marine meat! *****\n"},
	{"monster_boss2kl", "***** Boss incoming! Hornet 'the swarm' is about to strike! *****\n"},
	{"monster_carrier_mini", "***** Boss incoming! Carrier Mini is delivering pain right to your face! *****\n"},
	{"monster_carrier", "***** Boss incoming! Carrier’s here with a deadly payload! *****\n"},
	{"monster_tank_64", "***** Boss incoming! Tank Commander is here to take limbs! *****\n"},
	{"monster_shamblerkl", "***** Boss incoming! The Shambler is emerging watch out! *****\n"},
	{"monster_guncmdrkl", "***** Boss incoming! Gunner Commander has you in his sights! *****\n"},
	{"monster_makronkl", "***** Boss incoming! Makron is here to personally finish you off! *****\n"},
	{"monster_guardian", "***** Boss incoming! The Guardian is ready to claim your head! *****\n"},
	{"monster_psxguardian", "***** Boss incoming! The Enhanced Guardian is ready to spam rockets! *****\n"},
	{"monster_supertank", "***** Boss incoming! Super-Tank has more firepower than you can handle! *****\n"},
	{"monster_boss5", "***** Boss incoming! Super-Tank is here to show Strogg’s might! *****\n"},
	{"monster_widow2", "***** Boss incoming! The Widow is weaving disruptor beams just for you! *****\n"},
	{"monster_arachnid", "***** Boss incoming! Arachnid is here for some Marine BBQ! *****\n"},
	{"monster_psxarachnid", "***** Boss incoming! Arachnid is here, Is this a new arachnid?!?! *****\n"},
	{"monster_gm_arachnid", "***** Boss incoming! Missile Arachnid is armed and ready! *****\n"},
	{"monster_redmutant", "***** Boss incoming! The Bloody Mutant is out for blood—yours! *****\n"},
	{"monster_jorg", "***** Boss incoming! Jorg’s mech is upgraded and deadly! *****\n"}
};

void SetHealthBarName(edict_t* boss);

// attaching healthbar
static void AttachHealthBar(edict_t* boss) {
	auto healthbar = G_Spawn();
	if (!healthbar) return;

	healthbar->classname = "target_healthbar";
	// Usar asignación directa y operador de suma de vec3_t
	healthbar->s.origin = boss->s.origin + vec3_t{ 0, 0, 20 };

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
void SP_target_orb(edict_t* ent);
static void SpawnBossAutomatically() {
	const auto mapSize = GetMapSize(level.mapname);
	if (g_horde_local.level < 10 || g_horde_local.level % 5 != 0) {
		return;
	}

	const auto it = mapOrigins.find(level.mapname);
	if (it == mapOrigins.end()) {
		gi.Com_PrintFmt("PRINT: Error: No spawn origin found for map {}\n", level.mapname);
		return;
	}

	// Usar vec3_t con inicialización agregada
	const vec3_t spawn_origin{
		static_cast<float>(it->second[0]),
		static_cast<float>(it->second[1]),
		static_cast<float>(it->second[2])
	};

	// Validar origen usando vec3_t
	if (!is_valid_vector(spawn_origin) || spawn_origin == vec3_origin) {
		gi.Com_PrintFmt("PRINT: Error: Invalid spawn origin for map {}\n", level.mapname);
		return;
	}

	// Crear efecto de orbe primero
	edict_t* orb = G_Spawn();
	if (orb) {
		orb->classname = "target_orb";
		orb->s.origin = spawn_origin;
		SP_target_orb(orb);
	}

	edict_t* boss = G_Spawn();
	if (!boss) {
		if (orb) G_FreeEdict(orb);
		gi.Com_PrintFmt("PRINT: Error: Failed to spawn boss entity\n");
		return;
	}

	const char* desired_boss = G_HordePickBOSS(mapSize, level.mapname, g_horde_local.level, boss);
	if (!desired_boss) {
		if (orb) G_FreeEdict(orb);
		G_FreeEdict(boss);
		gi.Com_PrintFmt("PRINT: Error: Failed to pick a boss type\n");
		return;
	}

	boss_spawned_for_wave = true;
	boss->classname = desired_boss;

	// Configurar modo de monstruos voladores si aplica
	flying_monsters_mode = (boss->classname && (
		strcmp(boss->classname, "monster_boss2") == 0 ||
		strcmp(boss->classname, "monster_carrier") == 0 ||
		strcmp(boss->classname, "monster_carrier_mini") == 0 ||
		strcmp(boss->classname, "monster_boss2kl") == 0));

	// Posicionar jefe usando vec3_t
	boss->s.origin = spawn_origin;
	gi.Com_PrintFmt("PRINT: Preparing to spawn boss at position: {}\n", boss->s.origin);

	// Empujar entidades usando vec3_t
	const float push_radius = 500.0f;
	const float push_force = 1000.0f;
	PushEntitiesAway(spawn_origin, 3, push_radius, push_force, 3750.0f, 1600.0f);

	// Almacenar entidad orbe en el jefe
	boss->owner = orb;

	// Retardar spawn del jefe
	boss->nextthink = level.time + 1000_ms;
	boss->think = BossSpawnThink;
	gi.Com_PrintFmt("PRINT: Boss spawn preparation complete. Boss will appear in 1 second.\n");
}
THINK(BossSpawnThink)(edict_t* self) -> void
{
	// Remove the black light effect if it exists
	if (self->owner) {
		G_FreeEdict(self->owner);
		self->owner = nullptr;
	}

	// Boss spawn message
	auto it_msg = bossMessagesMap.find(self->classname);
	if (it_msg != bossMessagesMap.end()) {
		gi.LocBroadcast_Print(PRINT_CHAT, "\n\n\n{}\n", it_msg->second.data());
	}
	else {
		gi.Com_PrintFmt("PRINT: Warning: No specific message found for boss type '{}'. Using default message.\n",
			self->classname);
		gi.LocBroadcast_Print(PRINT_CHAT, "\n\n\nA Strogg Boss has spawned!\nPrepare for battle!\n");
	}

	// Configuración inicial del boss
	self->monsterinfo.IS_BOSS = true;
	//self->monsterinfo.BOSS_DEATH_HANDLED = false;

	self->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	self->monsterinfo.last_sentrygun_target_time = 0_ms;

	// Proceso de spawn seguro
	{
		// Temporalmente no sólido durante el spawn
		const auto original_solid = self->solid;
		self->solid = SOLID_NOT;

		ED_CallSpawn(self);
		ClearSpawnArea(self->s.origin, self->mins, self->maxs);

		// Restaurar solidez y actualizar
		self->solid = SOLID_BBOX;
		gi.linkentity(self);
	}

	// Aplicar multiplicadores y efectos
	constexpr float health_multiplier = 1.0f;
	constexpr float power_armor_multiplier = 1.0f;

	ApplyBossEffects(self);
	self->monsterinfo.attack_state = AS_BLIND;

	// Ajustar power armor si está presente
	if (self->monsterinfo.power_armor_power > 0) {
		self->monsterinfo.power_armor_power =
			static_cast<int32_t>(self->monsterinfo.power_armor_power);
	}

	// Efectos visuales de spawn
	const vec3_t spawn_pos = self->s.origin;  // Usar asignación directa de vec3_t
	const float magnitude = spawn_pos.length();  // Usar método length() en vez de VectorLength
	const float base_size = magnitude * 0.35f;
	const float end_size = base_size * 0.005f;

	// Aplicar efectos de spawn
	ImprovedSpawnGrow(spawn_pos, base_size, end_size, self);
	SpawnGrow_Spawn(spawn_pos, base_size, end_size);

	// Configuración final
	AttachHealthBar(self);
	SetHealthBarName(self);
	auto_spawned_bosses.insert(self);

	// Log de spawn exitoso
	gi.Com_PrintFmt("PRINT: Boss of type {} spawned successfully with {} health and {} power armor\n",
		self->classname, self->health, self->monsterinfo.power_armor_power);
}
// En SetHealthBarName
void SetHealthBarName(edict_t* boss) {
	static char buffer[MAX_STRING_CHARS];

	// Evitar asignaciones dinámicas
	const std::string_view display_name = GetDisplayName(boss);
	const size_t name_len = std::min(display_name.length(), sizeof(buffer) - 1);
	memcpy(buffer, display_name.data(), name_len);
	buffer[name_len] = '\0';

	gi.configstring(CONFIG_HEALTH_BAR_NAME, buffer);

	// Preparar mensaje una vez
	gi.WriteByte(svc_configstring);
	gi.WriteShort(CONFIG_HEALTH_BAR_NAME);
	gi.WriteString(buffer);

	// Enviar a todos los clientes
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
// En UnifiedAdjustSpawnRate y ResetCooldowns:
void ResetCooldowns() noexcept {
	spawnPointsData.clear();
	lastSpawnPointTime.clear();
	lastMonsterSpawnTime.clear();

	const MapSize& mapSize = GetMapSize(level.mapname);
	const int32_t currentLevel = g_horde_local.level;
	const int32_t humanPlayers = GetNumHumanPlayers();

	// Obtener cooldown base según el tamaño del mapa
	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);

	// Aplicar escala basada en nivel
	float cooldownScale = CalculateCooldownScale(currentLevel);
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	// Ajustes adicionales (reducidos pero mantenidos para balance)
	if (humanPlayers > 1) {
		float playerAdjustment = 1.0f - (std::min(humanPlayers - 1, 3) * 0.05f);
		SPAWN_POINT_COOLDOWN *= playerAdjustment;
	}

	// Ajustes por modo de dificultad (reducidos) - Con verificación de seguridad
	if ((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) {
		SPAWN_POINT_COOLDOWN *= 0.95f;
	}

	// Aplicar límites absolutos
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN, 1.0_sec, 3.0_sec);

	gi.Com_PrintFmt("DEBUG: Reset spawn cooldown to {:.2f} seconds (Level {})\n",
		SPAWN_POINT_COOLDOWN.seconds(), currentLevel);
}

void ResetAllSpawnAttempts() noexcept {
	for (auto& [spawn_point, data] : spawnPointsData) {
		data.attempts = 0;
		data.cooldown = SPAWN_POINT_COOLDOWN;
	}
}

// Función modificada para resetear la lista de jefes recientes
static void ResetRecentBosses() noexcept {
	recent_bosses.clear();
}

void ResetWaveAdvanceState() noexcept;

#include <algorithm>
// Variable global para la caché checkremainingmonsterconditions
struct alignas(64) MonsterCheckCacheData {
	gtime_t last_check_time = 0_ms;
	bool result = false;
	WaveEndReason cached_reason = WaveEndReason::AllMonstersDead;
	int32_t remaining_monsters = 0;
	float remaining_percentage = 0.0f;
	bool cache_valid = false;

	void Reset() {
		last_check_time = 0_ms;
		result = false;
		cached_reason = WaveEndReason::AllMonstersDead;
		remaining_monsters = 0;
		remaining_percentage = 0.0f;
		cache_valid = false;
	}
};

static MonsterCheckCacheData g_monster_check_cache;

#include <algorithm>
static bool CheckRemainingMonstersCondition(const MapSize& mapSize, WaveEndReason& reason) {
	const gtime_t currentTime = level.time;
	const bool allMonstersDead = Horde_AllMonstersDead();

	// Verificar tiempo límite independiente primero
	if (currentTime >= g_independent_timer_start + g_lastParams.independentTimeThreshold) {
		gi.Com_PrintFmt("PRINT: Independent time limit reached. Forcing wave advance.\n");
		reason = WaveEndReason::TimeLimitReached;
		return true;
	}

	// Verificar si se ha permitido avanzar la ola manualmente
	if (allowWaveAdvance) {
		ResetWaveAdvanceState();
		reason = WaveEndReason::AllMonstersDead;
		// Establecer explícitamente monsters restantes a 0
		//level.killed_monsters = level.total_monsters;
		cachedRemainingMonsters = 0;
		return true;
	}

	// Verificar si todos los monstruos han sido derrotados
	if (allMonstersDead) {
		reason = WaveEndReason::AllMonstersDead;
		// Establecer explícitamente monsters restantes a 0
	//	level.killed_monsters = level.total_monsters;
		cachedRemainingMonsters = 0;
		return true;
	}

	// Inicializar waveEndTime si no está establecido
	if (g_horde_local.waveEndTime == 0_sec) {
		g_horde_local.waveEndTime = g_independent_timer_start + g_lastParams.independentTimeThreshold;
	}

	// Obtener número de monstruos restantes una sola vez
	int32_t remainingMonsters = CalculateRemainingMonsters();
	float percentageRemaining = static_cast<float>(remainingMonsters) / static_cast<float>(g_totalMonstersInWave);

	// Determinar si alguna condición se ha cumplido
	if (!g_horde_local.conditionTriggered) {
		bool timeReduced = false;
		if (remainingMonsters <= g_lastParams.maxMonsters || percentageRemaining <= g_lastParams.lowPercentageThreshold) {
			g_horde_local.conditionTriggered = true;
			g_horde_local.conditionStartTime = currentTime;

			// Elegir el menor umbral de tiempo
			if (remainingMonsters <= g_lastParams.maxMonsters && percentageRemaining <= g_lastParams.lowPercentageThreshold) {
				g_horde_local.conditionTimeThreshold = std::min(g_lastParams.timeThreshold, g_lastParams.lowPercentageTimeThreshold);
				timeReduced = true;
			}
			else if (remainingMonsters <= g_lastParams.maxMonsters) {
				g_horde_local.conditionTimeThreshold = g_lastParams.timeThreshold;
				timeReduced = true;
			}
			else {
				g_horde_local.conditionTimeThreshold = g_lastParams.lowPercentageTimeThreshold;
				timeReduced = true;
			}

			g_horde_local.waveEndTime = g_horde_local.conditionStartTime + g_horde_local.conditionTimeThreshold;

			gi.Com_PrintFmt("PRINT: Condition triggered. Remaining monsters: {}, Percentage remaining: {:.2f}%\n",
				remainingMonsters, percentageRemaining * 100);

			// Reducción agresiva del tiempo si quedan muy pocos monstruos
			if (remainingMonsters <= MONSTERS_FOR_AGGRESSIVE_REDUCTION) {
				g_horde_local.waveEndTime = std::min(
					g_horde_local.waveEndTime,
					currentTime + AGGRESSIVE_TIME_REDUCTION_PER_MONSTER * (MONSTERS_FOR_AGGRESSIVE_REDUCTION - remainingMonsters)
				);
				timeReduced = true;
			}

			// Solo mostrar el mensaje una vez si hubo alguna reducción
			if (timeReduced) {
				gi.LocBroadcast_Print(PRINT_HIGH, "Wave time reduced!\n");
			}
		}
	}

	// Verificar si la condición ha sido activada
	if (!g_horde_local.conditionTriggered) {
		return false;
	}

	// Calcular el tiempo restante
	const gtime_t remainingTime = g_horde_local.waveEndTime - currentTime;

	// Emitir advertencias en tiempos predefinidos
	for (size_t i = 0; i < WARNING_TIMES.size(); ++i) {
		const gtime_t warningTime = gtime_t::from_sec(WARNING_TIMES[i]);
		if (!g_horde_local.warningIssued[i] && remainingTime <= warningTime && remainingTime > (warningTime - 1_sec)) {
			gi.LocBroadcast_Print(PRINT_HIGH, "{} seconds remaining!\n", static_cast<int>(WARNING_TIMES[i]));
			g_horde_local.warningIssued[i] = true;
		}
	}

	// Verificar si el tiempo de la ola ha llegado a cero
	if (currentTime >= g_horde_local.waveEndTime) {
		bool shouldAdvance = false;

		// Aquí también verificamos el tiempo independiente de nuevo
		if (currentTime >= g_independent_timer_start + g_lastParams.independentTimeThreshold) {
			reason = WaveEndReason::TimeLimitReached;
			shouldAdvance = true;
		}
		else if (g_horde_local.conditionTriggered &&
			currentTime >= (g_horde_local.conditionStartTime + g_horde_local.conditionTimeThreshold)) {
			reason = WaveEndReason::MonstersRemaining;
			shouldAdvance = true;
		}

		if (shouldAdvance) {
			// No resetear el timer aquí, se manejará en HandleWaveRestMessage
			gi.Com_PrintFmt("PRINT: Wave advance triggered. Reason: {}\n",
				reason == WaveEndReason::TimeLimitReached ? "Time Limit" : "Monsters Remaining");
			return true;
		}
	}

	// Proveer actualizaciones periódicas sobre monstruos restantes y tiempo
	if (currentTime - g_horde_local.lastPrintTime >= 10_sec) {
		gi.Com_PrintFmt("PRINT: Wave status: {} monsters remaining. {:.2f} seconds left.\n",
			remainingMonsters, remainingTime.seconds());
		g_horde_local.lastPrintTime = currentTime;
	}

	return false;
}

/// ///
/// game reset
void ResetGame() {

	// Si ya se ha ejecutado una vez, retornar inmediatamente
	if (hasBeenReset) {
		gi.Com_PrintFmt("PRINT: Reset already performed, skipping...\n");
		return;
	}

	// Establecer el flag al inicio de la ejecución
	hasBeenReset = true;

	// Añadir limpieza explícita de bosses
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end();) {
		edict_t* boss = *it;
		if (boss && boss->inuse) {
			// Asegurarse de que el boss esté marcado como manejado
			boss->monsterinfo.BOSS_DEATH_HANDLED = true;
			// Limpiar cualquier estado pendiente
			OnEntityRemoved(boss);
		}
		it = auto_spawned_bosses.erase(it);
	}

	// Resetear todas las variables globales
//	last_wave_number = 0;
	horde_message_end_time = 0_sec;
	cachedRemainingMonsters = 0;
	g_totalMonstersInWave = 0;

	// Resetear el caché de verificación de monstruos
	g_monster_check_cache.Reset();

	// Resetear flags de control
	g_maxMonstersReached = false;
	g_lowPercentageTriggered = false;

	// Limpiar cachés
	mapSizeCacheSize = 0;
	spawnPointsData.clear();
	lastMonsterSpawnTime.clear();
	lastSpawnPointTime.clear();

	// Reiniciar variables de estado global
	g_horde_local = HordeState(); // Asume que HordeState tiene un constructor por defecto adecuado
	current_wave_level = 0;
	flying_monsters_mode = false;
	boss_spawned_for_wave = false;
	next_wave_message_sent = false;
	allowWaveAdvance = false;

	// Reiniciar otras variables relevantes
	//WAVE_TO_ALLOW_FLYING = 0;
	SPAWN_POINT_COOLDOWN = 2.2_sec;

	cachedRemainingMonsters = 0;
	g_totalMonstersInWave = 0;

	// Resetear el estado de las condiciones
	g_horde_local.conditionTriggered = false;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTimeThreshold = 0_sec;
	g_horde_local.waveEndTime = 0_sec;
	g_horde_local.timeWarningIssued = false;

	// Resetear cualquier otro estado específico de la ola según sea necesario
	boss_spawned_for_wave = false;
	flying_monsters_mode = false;

	// Reset core gameplay elements
	ResetAllSpawnAttempts();
	ResetCooldowns();
	ResetBenefits();

	// Reiniciar la lista de bosses recientes
	ResetRecentBosses();

	// Reiniciar wave advance state
	ResetWaveAdvanceState();

	// Reset wave information
	g_horde_local.level = 0; // Reset current wave level
	g_horde_local.state = horde_state_t::warmup; // Set game state to warmup
	g_horde_local.warm_time = level.time + 4_sec; // Reiniciar el tiempo de warmup
	g_horde_local.monster_spawn_time = level.time; // Reiniciar el tiempo de spawn de monstruos
	g_horde_local.num_to_spawn = 0;
	g_horde_local.queued_monsters = 0;

	// Reset gameplay configuration variables
	gi.cvar_set("g_chaotic", "0");
	gi.cvar_set("g_insane", "0");
	gi.cvar_set("g_hardcoop", "0");
	gi.cvar_set("dm_monsters", "0");
	gi.cvar_set("timelimit", "50");
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
	gi.cvar_set("g_bfgslide", "1");
	gi.cvar_set("g_autohaste", "0");

	// Registrar el reinicio
	gi.Com_PrintFmt("PRINT: Horde game state reset complete.\n");
}
static gtime_t g_lastMonsterCountVerification = 0_ms;
constexpr gtime_t MONSTER_COUNT_VERIFICATION_INTERVAL = 5_sec;

// Función para contar los monstruos activos
static int32_t CountActiveMonsters() {
	return std::count_if(active_monsters().begin(), active_monsters().end(),
		[](const edict_t* ent) {
			return !ent->deadflag &&
				!(ent->monsterinfo.aiflags & AI_DO_NOT_COUNT) &&
				is_valid_vector(ent->s.origin);
		});
}

inline int32_t CalculateRemainingMonsters() {
	int32_t remainingMonsters = level.total_monsters - level.killed_monsters + (g_horde_local.queued_monsters);
	return (remainingMonsters < 0) ? 0 : remainingMonsters;
}

void ResetWaveAdvanceState() noexcept {
	g_independent_timer_start = level.time;

	// Reiniciar variables de condición
	g_horde_local.conditionTriggered = false;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTimeThreshold = 0_sec;
	g_horde_local.timeWarningIssued = false;

	allowWaveAdvance = false;

	g_horde_local.lastPrintTime = 0_sec;

	cachedRemainingMonsters = -1;
	g_totalMonstersInWave = 0;

	boss_spawned_for_wave = false;
	flying_monsters_mode = false;

	g_lastWaveNumber = -1;
	g_lastNumHumanPlayers = -1;

	g_horde_local.waveEndTime = 0_sec;
	// Reiniciar las advertencias
//	std::fill(warningIssued.begin(), warningIssued.end(), false);
}

void AllowNextWaveAdvance() noexcept {
	allowWaveAdvance = true;
}

void fastNextWave() noexcept {
	g_horde_local.monster_spawn_time = level.time;
	g_horde_local.warm_time = level.time;

	// Permitir el avance inmediato
	allowWaveAdvance = true;

	// Resetear variables importantes
	g_horde_local.num_to_spawn = 0;
	g_horde_local.queued_monsters = 0;

	// Forzar la actualización del estado
	g_monster_check_cache.Reset();
	g_horde_local.conditionTriggered = true;
	g_horde_local.waveEndTime = level.time;

	// Limpiar cualquier mensaje pendiente
	ClearHordeMessage();

	g_horde_local.state = horde_state_t::spawning;
	Horde_InitLevel(g_horde_local.level + 1);
}

static void MonsterSpawned(const edict_t* monster) {
	if (!monster->deadflag && !(monster->monsterinfo.aiflags & AI_DO_NOT_COUNT)) {
		cachedRemainingMonsters++;
		g_totalMonstersInWave++;
	}
}

void MonsterDied(const edict_t* monster) {
	if (!(monster->monsterinfo.aiflags & AI_DO_NOT_COUNT)) {
		int32_t remaining = CalculateRemainingMonsters();
		if (remaining < 0) {
			remaining = 0;
		}
		cachedRemainingMonsters = static_cast<uint16_t>(remaining);
	}
}

inline int32_t GetNumActivePlayers() {
	return std::count_if(active_players().begin(), active_players().end(),
		[](const edict_t* player) { return player->client->resp.ctf_team == CTF_TEAM1; });
}

inline int32_t GetNumSpectPlayers() {
	return std::count_if(active_players().begin(), active_players().end(),
		[](const edict_t* player) { return player->client->resp.ctf_team != CTF_TEAM1; });
}

static bool UseFarthestSpawn() noexcept {
	if (g_horde_local.level >= 15) {
		return (rand() % 4 == 0);  // 25% de probabilidad a partir del nivel 15
	}
	return false;
}

static void PlayWaveStartSound() {
	static std::array<int, 5> cached_sound_indices = { -1, -1, -1, -1, -1 };
	static bool indices_initialized = false;

	if (!indices_initialized) {
		const std::array<const char*, 5> sound_files = {
			"misc/r_tele3.wav",
			"world/klaxon2.wav",
			"misc/tele_up.wav",
			"world/incoming.wav",
			"world/yelforce.wav"
		};

		for (size_t i = 0; i < sound_files.size(); ++i) {
			cached_sound_indices[i] = gi.soundindex(sound_files[i]);
		}
		indices_initialized = true;
	}

	const int32_t sound_index = static_cast<int32_t>(frandom() * cached_sound_indices.size());
	gi.sound(world, CHAN_VOICE, cached_sound_indices[sound_index], 1, ATTN_NONE, 0);
}


enum class WaveMessageType {
	InventoryPrompt,
	Welcome
};

// Implementación de DisplayWaveMessage
void DisplayWaveMessage(gtime_t duration = 5_sec) {
	static const std::array<const char*, 3> messages = {
		"Horde Menu available upon opening Inventory or using Compass\n\nMAKE THEM PAY!\n",
		"Welcome to Hell.\n\nUse FlipOff <Key> looking at walls to spawn lasers (cost: 25 cells)\n",
		"NEW!\n\nTeslas can now be placed on walls and ceilings!\n\nUse them wisely!"
	};

	int choice = irandom(0, messages.size() - 1);
	UpdateHordeMessage(messages[choice], duration);
}

static void HandleWaveCleanupMessage(const MapSize& mapSize, WaveEndReason reason) noexcept {
	// Si la ola terminó con todos los monstruos muertos, aplicar reglas normales
	if (reason == WaveEndReason::AllMonstersDead) {
		if (current_wave_level >= 15 && current_wave_level <= 26) {
			gi.cvar_set("g_insane", "1");
			gi.cvar_set("g_chaotic", "0");
		}
		else if (current_wave_level >= 27) {
			gi.cvar_set("g_insane", "2");
			gi.cvar_set("g_chaotic", "0");
		}
		else if (current_wave_level <= 14) {
			gi.cvar_set("g_insane", "0");
			gi.cvar_set("g_chaotic", mapSize.isSmallMap ? "2" : "1");
		}
	}
	else {
		// Si la ola no terminó por victoria completa, pequeña probabilidad de mantener la dificultad
		float probability = mapSize.isBigMap ? 0.3f :
			mapSize.isSmallMap ? 0.2f : 0.25f;  // 20-30% según tamaño de mapa
		if (frandom() < probability) {
			// Si gana la probabilidad, aplicar la dificultad según el nivel actual
			if (current_wave_level >= 15 && current_wave_level <= 26) {
				gi.cvar_set("g_insane", "1");
				gi.cvar_set("g_chaotic", "0");
			}
			else if (current_wave_level >= 27) {
				gi.cvar_set("g_insane", "2");
				gi.cvar_set("g_chaotic", "0");
			}
			else if (current_wave_level <= 14) {
				gi.cvar_set("g_insane", "0");
				gi.cvar_set("g_chaotic", mapSize.isSmallMap ? "2" : "1");
			}
		}
		else {
			// Si no gana la probabilidad, desactivar ambos modos
			gi.cvar_set("g_insane", "0");
			gi.cvar_set("g_chaotic", "0");
		}
	}
	g_horde_local.state = horde_state_t::rest;
}
// Función para obtener un sonido aleatorio
static const char* GetRandomWaveSound() {
	std::uniform_int_distribution<size_t> dist(0, WAVE_SOUNDS.size() - 1);
	return WAVE_SOUNDS[dist(mt_rand)].data(); // Usar .data() para obtener const char*
}

static void HandleWaveRestMessage(gtime_t duration = 4_sec) {
	const char* message;

	if (g_chaotic->integer > 0) {
		if (g_chaotic->integer == 2) {
			message = brandom() ?
				"***RELENTLESS WAVE INCOMING***\n\nSTAND YOUR GROUND!\n" :
				"***OVERWHELMING FORCES APPROACHING***\n\nHOLD THE LINE!\n";
		}
		else {
			message = brandom() ?
				"***CHAOTIC WAVE INCOMING***\n\nSTEEL YOURSELF!\n" :
				"***CHAOS APPROACHES***\n\nREADY FOR BATTLE!\n";
		}
	}
	else if (g_insane->integer > 0) {
		if (g_insane->integer == 2) {
			message = brandom() ?
				"***MERCILESS WAVE INCOMING***\n\nNO RETREAT!\n" :
				"***DEADLY WAVE APPROACHES***\n\nFIGHT TO SURVIVE!\n";
		}
		else {
			message = brandom() ?
				"***INTENSE WAVE INCOMING***\n\nSHOW NO MERCY!\n" :
				"***FIERCE BATTLE AHEAD***\n\nSTAND READY!\n";
		}
	}
	else {
		message = brandom() ?
			"STROGGS STARTING TO PUSH!\n\nSTAY ALERT!\n" :
			"PREPARE FOR INCOMING WAVE!\n\nHOLD POSITION!\n";
	}

	for (auto player : active_players()) {
		if (player->client) {
			player->client->total_damage = 0;
		}
	}

	UpdateHordeMessage(message, duration);

	g_independent_timer_start = level.time;
	g_horde_local.waveEndTime = 0_sec;
	g_horde_local.conditionTriggered = false;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTimeThreshold = 0_sec;

	// Resetear las advertencias usando un bucle for simple
	for (size_t i = 0; i < 4; i++) {
		g_horde_local.warningIssued[i] = false;
	}

	gi.sound(world, CHAN_VOICE, gi.soundindex(GetRandomWaveSound()), 1, ATTN_NONE, 0);
}

// Llamar a esta función durante la inicialización del juego
void InitializeWaveSystem() noexcept {
	PrecacheWaveSounds();
}

static void SetMonsterArmor(edict_t* monster);
static void SetNextMonsterSpawnTime(const MapSize& mapSize);

static edict_t* SpawnMonsters() {
	static edict_t* available_spawns[MAX_SPAWN_POINTS];
	const MapSize& mapSize = GetMapSize(level.mapname);
	size_t spawn_count = 0;

	// Recolectar solo puntos de spawn que no estén en cooldown
	for (uint32_t i = 1; i < globals.num_edicts && spawn_count < MAX_SPAWN_POINTS; ++i) {
		edict_t* e = &g_edicts[i];
		if (!e->inuse || !e->classname ||
			strcmp(e->classname, "info_player_deathmatch") != 0 ||
			e->monsterinfo.IS_BOSS)
			continue;

		auto it = spawnPointsData.find(e);
		if (it != spawnPointsData.end() && it->second.isTemporarilyDisabled) {
			if (level.time < it->second.cooldownEndsAt)
				continue;
		}

		available_spawns[spawn_count++] = e;
	}

	if (spawn_count == 0)
		return nullptr;

	// Optimizar el Fisher-Yates shuffle usando distribución uniforme
	std::uniform_int_distribution<size_t> dist(0, spawn_count - 1);
	for (size_t i = spawn_count - 1; i > 0; --i) {
		size_t j = dist(mt_rand);
		if (i != j)
			std::swap(available_spawns[i], available_spawns[j]);
	}

	// Calcular monsters per spawn una sola vez
	const int32_t monsters_per_spawn = std::min(
		g_horde_local.queued_monsters > 0
		? std::min(g_horde_local.queued_monsters, 3)
		: (mapSize.isSmallMap ? 4 : (mapSize.isBigMap ? 6 : 5)),
		6
	);

	// Cache de límites
	const int32_t activeMonsters = CountActiveMonsters();
	const int32_t maxMonsters = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
		(mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP);
	const int32_t spawnable = std::max(0, std::min(monsters_per_spawn, maxMonsters - activeMonsters));

	if (spawnable <= 0)
		return nullptr;

	edict_t* last_spawned = nullptr;

	// Spawn optimizado usando spawn_temp_t::empty
	for (size_t i = 0; i < spawn_count && i < static_cast<size_t>(spawnable) && g_horde_local.num_to_spawn > 0; ++i) {
		edict_t* spawn_point = available_spawns[i];
		const char* monster_classname = G_HordePickMonster(spawn_point);

		if (!monster_classname)
			continue;

		edict_t* monster = G_Spawn();
		if (!monster)
			continue;

		// Configuración básica pre-spawn
		monster->classname = monster_classname;
		monster->s.origin = spawn_point->s.origin;
		monster->s.angles = spawn_point->s.angles;
		monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
		monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
		monster->monsterinfo.last_sentrygun_target_time = 0_ms;

		// Spawn del monstruo usando spawn_temp_t::empty
		ED_CallSpawn(monster, spawn_temp_t::empty);

		if (!monster->inuse) {
			G_FreeEdict(monster);
			continue;
		}

		// Configuración post-spawn
		if (g_horde_local.level >= 14)
			SetMonsterArmor(monster);

		if (frandom() <= (g_horde_local.level <= 2 ? 0.8f :
			g_horde_local.level <= 7 ? 0.6f : 0.45f))
			monster->item = G_HordePickItem();

		// Efectos visuales y sonoros
		SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
		gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

		// Actualizar contadores
		g_horde_local.num_to_spawn = std::max(0, g_horde_local.num_to_spawn - 1);
		g_horde_local.queued_monsters = std::max(0, g_horde_local.queued_monsters - 1);
		g_totalMonstersInWave++;
		last_spawned = monster;
	}

	// Manejar monstruos en cola restantes de manera más eficiente
	if (g_horde_local.queued_monsters > 0 && g_horde_local.num_to_spawn > 0) {
		const int32_t additional_spawnable = maxMonsters - CountActiveMonsters();
		const int32_t additional_to_spawn = std::min(g_horde_local.queued_monsters, additional_spawnable);
		g_horde_local.num_to_spawn += additional_to_spawn;
		g_horde_local.queued_monsters = std::max(0, g_horde_local.queued_monsters - additional_to_spawn);
	}

	SetNextMonsterSpawnTime(mapSize);
	return last_spawned;
}

static void SetMonsterArmor(edict_t* monster) {
	const spawn_temp_t& st = ED_GetSpawnTemp();
	if (!st.was_key_specified("power_armor_power")) {
		monster->monsterinfo.armor_type = IT_ARMOR_COMBAT;

		// Factores base más conservadores
		const float health_ratio = monster->health / static_cast<float>(monster->max_health);
		const float size_factor = (monster->maxs - monster->mins).length() / 64.0f;
		const float mass_factor = std::min(monster->mass / 200.0f, 1.5f);  // Reducido de 2.0f a 1.5f

		// Factor de nivel dinámico más suave
		float level_scaling;
		if (current_wave_level <= 15) {
			level_scaling = 1.0f + (current_wave_level * 0.04f);  // Reducido de 0.08f a 0.04f
		}
		else if (current_wave_level <= 25) {
			level_scaling = 1.6f + ((current_wave_level - 15) * 0.06f);  // Reducido de 2.2f y 0.12f
		}
		else {
			level_scaling = 2.2f + ((current_wave_level - 25) * 0.08f);  // Reducido de 3.4f y 0.15f
		}

		// Base armor más baja con mejor balance
		float base_armor = (75 + monster->max_health * 0.15f) *  // Reducido de 100 y 0.2f
			std::pow(health_ratio, 1.1f) *                       // Reducido de 1.2f
			std::pow(size_factor, 0.7f) *                        // Reducido de 0.8f
			std::pow(mass_factor, 0.6f) *                        // Reducido de 0.7f
			level_scaling;

		// Reducción de armadura progresiva
		float armor_multiplier = 1.0f;
		if (current_wave_level <= 30) {
			armor_multiplier = 0.4f;                             // Reducido de 0.5f
		}
		else if (current_wave_level <= 40) {
			armor_multiplier = 0.5f;                             // Nueva etapa intermedia
		}
		base_armor *= armor_multiplier;

		// Bonus por dificultad reducidos
		if (g_insane->integer) {
			base_armor *= 1.2f;                                 // Reducido de 1.3f
		}
		else if (g_chaotic->integer) {
			base_armor *= 1.1f;                                 // Reducido de 1.15f
		}

		// Factor aleatorio más controlado
		const float random_factor = 1.0f + (crandom() * 0.1f);  // Reducido de 0.15f
		base_armor *= random_factor;

		// Ajustes finales por nivel más suaves
		if (current_wave_level > 25) {
			base_armor *= 1.0f + ((current_wave_level - 25) * 0.03f);  // Reducido de 0.05f
		}

		// Límites dinámicos más restrictivos
		const int min_armor = std::max(25, static_cast<int>(monster->max_health * 0.08f));  // Reducido de 50 y 0.1f
		const int max_armor = static_cast<int>(monster->max_health *
			(current_wave_level > 25 ? 1.5f : 1.2f));  // Reducido de 2.0f y 1.5f

		// Asignar armor usando los campos correctos de monsterinfo
		monster->monsterinfo.armor_power = std::clamp(static_cast<int>(base_armor), min_armor, max_armor);
		monster->monsterinfo.base_power_armor = monster->monsterinfo.armor_power;
	}
}

static void SetNextMonsterSpawnTime(const MapSize& mapSize) {
	g_horde_local.monster_spawn_time = level.time +
		(mapSize.isSmallMap ? random_time(1.2_sec, 1.5_sec) :
			mapSize.isBigMap ? random_time(0.9_sec, 1.1_sec) :
			random_time(1.7_sec, 1.8_sec) / 2);
}
#include <unordered_map>
#include <fmt/core.h>

// Usar enum class para mejorar la seguridad de tipos
enum class MessageType {
	Standard,
	Chaotic,
	Insane
};

static void CalculateTopDamager(PlayerStats& topDamager, float& percentage) {
	int32_t total_damage = 0;
	topDamager = PlayerStats(); // Reset usando el constructor por defecto

	for (const auto& player : active_players()) {
		if (!player->client)
			continue;

		int32_t player_damage = player->client->total_damage;

		// Solo considerar jugadores que hayan hecho daño
		if (player_damage > 0) {
			total_damage += player_damage;
			if (player_damage > topDamager.total_damage) {
				topDamager.total_damage = player_damage;
				topDamager.player = player;
			}
		}
	}

	// Calcular porcentaje solo si hubo daño total
	percentage = (total_damage > 0) ?
		(static_cast<float>(topDamager.total_damage) / total_damage) * 100.0f : 0.0f;

	// Redondear el porcentaje a dos decimales
	percentage = std::round(percentage * 100) / 100;
}

// Enumeration for different reward types with their relative weights
enum class RewardType {
	BANDOLIER = 0,
	AMMO_TESLA = 1,
	SENTRY_GUN = 2
};

struct RewardInfo {
	item_id_t item_id;
	int weight;  // Higher weight = more common
};

// Define reward table with weights
static const std::unordered_map<RewardType, RewardInfo> REWARD_TABLE = {
	{RewardType::BANDOLIER, {IT_ITEM_BANDOLIER, 50}},    // Most common
	{RewardType::AMMO_TESLA, {IT_AMMO_TESLA, 30}},         // Medium rarity
	{RewardType::SENTRY_GUN, {IT_ITEM_DOPPELGANGER, 20}} // Least common
};

// Function to handle reward selection and distribution
bool GiveTopDamagerReward(const PlayerStats& topDamager, const std::string& playerName) {
	if (!topDamager.player)
		return false;

	// Calculate total weight for weighted random selection
	int totalWeight = 0;
	for (const auto& reward : REWARD_TABLE)
		totalWeight += reward.second.weight;

	// Generate random number within total weight range
	std::uniform_int_distribution<int> dist(1, totalWeight);
	int randomNum = dist(mt_rand);

	// Select reward based on weights
	item_id_t selectedItemId = IT_ITEM_BANDOLIER; // Default fallback
	int currentWeight = 0;

	for (const auto& reward : REWARD_TABLE) {
		currentWeight += reward.second.weight;
		if (randomNum <= currentWeight) {
			selectedItemId = reward.second.item_id;
			break;
		}
	}

	// Spawn and give the selected item
	if (gitem_t* it = GetItemByIndex(selectedItemId)) {
		edict_t* it_ent = G_Spawn();
		if (!it_ent)
			return false;

		it_ent->classname = it->classname;
		it_ent->item = it;

		SpawnItem(it_ent, it, spawn_temp_t::empty);
		if (it_ent->inuse) {
			Touch_Item(it_ent, topDamager.player, null_trace, true);
			if (it_ent->inuse)
				G_FreeEdict(it_ent);

			// Announce reward
			gi.LocBroadcast_Print(PRINT_HIGH, "{} receives a {} for top damage!\n",
				playerName.c_str(), it->use_name);
			return true;
		}
	}
	return false;
}

static void SendCleanupMessage(WaveEndReason reason) {
	gtime_t duration = 3_sec;
	if (allowWaveAdvance && reason == WaveEndReason::AllMonstersDead) {
		duration = 0_sec;
	}

	// Calculate top damager
	PlayerStats topDamager;
	float percentage = 0.0f;
	CalculateTopDamager(topDamager, percentage);

	// Show main message based on completion type
	switch (reason) {
	case WaveEndReason::AllMonstersDead:
		UpdateHordeMessage(fmt::format("Wave {} Completely Cleared - Perfect Victory!\n", g_horde_local.level), duration);
		break;
	case WaveEndReason::MonstersRemaining:
		UpdateHordeMessage(fmt::format("Wave {} Pushed Back - But Still Threatening!\n", g_horde_local.level), duration);
		break;
	case WaveEndReason::TimeLimitReached:
		UpdateHordeMessage(fmt::format("Wave {} Contained - Time Limit Reached!\n", g_horde_local.level), duration);
		break;
	}

	// Handle top damager rewards
	if (topDamager.player) {
		std::string playerName = GetPlayerName(topDamager.player);
		gi.LocBroadcast_Print(PRINT_HIGH, "{} dealt the most damage with {}! ({}% of total)\n",
			playerName.c_str(), topDamager.total_damage, static_cast<int>(percentage));

		// Give reward and reset stats if successful
		if (GiveTopDamagerReward(topDamager, playerName)) {

			for (auto player : active_players()) {
				if (player->client) {
					// Reset damage counters
					player->client->total_damage = 0;
					player->lastdmg = level.time;
					player->client->dmg_counter = 0;
					player->client->ps.stats[STAT_ID_DAMAGE] = 0;

					//revive all players waiting on squad
					player->client->respawn_time = 0_sec;
					player->client->coop_respawn_state = COOP_RESPAWN_NONE;
					player->client->last_damage_time = level.time;
				}
			}
		}
	}
}

// Add this function in the appropriate source file that deals with spawn management.
static void CheckAndResetDisabledSpawnPoints() {
	const gtime_t currentTime = level.time;
	for (auto& [spawn_point, data] : spawnPointsData) {
		if (data.isTemporarilyDisabled && currentTime >= data.cooldownEndsAt) {
			data.isTemporarilyDisabled = false;
			data.attempts = 0;
			data.cooldownEndsAt = 0_sec;
			spawn_point->nextthink = 0_sec;
		}
	}
}

void Horde_RunFrame() {
	const MapSize& mapSize = GetMapSize(level.mapname);
	const int32_t currentLevel = g_horde_local.level;
	static WaveEndReason currentWaveEndReason;  // Agregar esta declaración
	CheckAndResetDisabledSpawnPoints();

	// Manejo de monstruos personalizados
	if (dm_monsters->integer >= 1) {
		g_horde_local.num_to_spawn = dm_monsters->integer;
		g_horde_local.queued_monsters = 0;
		ClampNumToSpawn(mapSize);
	}

	// Obtener contadores de jugadores
	const int32_t numHumanPlayers = GetNumHumanPlayers();
	const int32_t numSpectPlayers = GetNumSpectPlayers();

	// Ajustes de cooperación
	level.coop_scale_players = 1 + numHumanPlayers;
	G_Monster_CheckCoopHealthScaling();

	// Verificaciones constantes
	for (auto ent : active_monsters()) {
		CheckAndRestoreMonsterAlpha(ent);
	}
	CleanupInvalidEntities();

	switch (g_horde_local.state) {
	case horde_state_t::warmup:
		if (g_horde_local.warm_time < level.time) {
			cachedRemainingMonsters = CalculateRemainingMonsters();
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(1);
			current_wave_level = 1;
			PlayWaveStartSound();
			DisplayWaveMessage();
		}
		break;

	case horde_state_t::spawning:
		// Verificar tiempo límite independiente primero
		if (level.time >= g_independent_timer_start + g_lastParams.independentTimeThreshold) {
			WaveEndReason reason = WaveEndReason::TimeLimitReached;
			SendCleanupMessage(reason);
			gi.Com_PrintFmt("PRINT: Wave {} time limit reached during spawn. Transitioning to cleanup.\n", currentLevel);
			g_horde_local.state = horde_state_t::cleanup;
			g_horde_local.monster_spawn_time = level.time + 0.5_sec;
			currentWaveEndReason = reason;
			break;
		}

		if (g_horde_local.monster_spawn_time <= level.time) {
			// Spawn de jefe si corresponde
			if (currentLevel >= 10 && currentLevel % 5 == 0 && !boss_spawned_for_wave) {
				SpawnBossAutomatically();
			}

			const int32_t activeMonsters = CountActiveMonsters();
			const int32_t maxMonsters = (mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
				(mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP));

			if (activeMonsters < maxMonsters) {
				SpawnMonsters();
			}

			// Transición a active_wave cuando se complete el spawn
			if (g_horde_local.num_to_spawn == 0) {
				if (!next_wave_message_sent) {
					VerifyAndAdjustBots();
					gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Fully Deployed.\nWave Level: {}\n", currentLevel);
					next_wave_message_sent = true;
				}

				// Inicializar el estado active_wave
				g_horde_local.state = horde_state_t::active_wave;
				g_horde_local.conditionTriggered = false;
				g_horde_local.conditionStartTime = 0_sec;
				g_horde_local.waveEndTime = 0_sec;
				g_horde_local.lastPrintTime = 0_sec;

				// Resetear las warningtimes usando un bucle for simple
				for (size_t i = 0; i < 4; i++) {
					g_horde_local.warningIssued[i] = false;
				}

				gi.Com_PrintFmt("PRINT: Transitioning to 'active_wave' state. Wave timer starting.\n");
			}
		}
		break;

	case horde_state_t::active_wave: {
		WaveEndReason reason;
		bool shouldAdvance = CheckRemainingMonstersCondition(mapSize, reason);

		if (shouldAdvance) {
			SendCleanupMessage(reason);
			gi.Com_PrintFmt("PRINT: Wave {} completed. Transitioning to cleanup.\n", currentLevel);
			g_horde_local.state = horde_state_t::cleanup;
			g_horde_local.monster_spawn_time = level.time + 0.5_sec;
			currentWaveEndReason = reason; // Guardar la razón aquí
		}
		else if (g_horde_local.monster_spawn_time <= level.time) {
			const int32_t activeMonsters = CountActiveMonsters();
			const int32_t maxMonsters = (mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
				(mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP));

			if (activeMonsters < maxMonsters) {
				SpawnMonsters();
			}
		}
		break;
	}

	case horde_state_t::cleanup: {
		if (g_horde_local.monster_spawn_time < level.time) {
			HandleWaveCleanupMessage(mapSize, currentWaveEndReason); // Pasar la razón guardada

			// Transición al estado de descanso
			g_horde_local.warm_time = level.time + random_time(0.8_sec, 1.5_sec);
			g_horde_local.state = horde_state_t::rest;
			cachedRemainingMonsters = CalculateRemainingMonsters();
		}
		break;
	}
	case horde_state_t::rest:
		if (g_horde_local.warm_time < level.time) {
			HandleWaveRestMessage(4_sec);
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(g_horde_local.level + 1);
		}
		break;
	}

	// Actualizar mensajes y HUD
	if (horde_message_end_time && level.time >= horde_message_end_time) {
		ClearHordeMessage();
	}
	UpdateHordeHUD();
}

// Función para manejar el evento de reinicio
void HandleResetEvent() {
	ResetGame();
}

// Get the remaining time for the current wave
gtime_t GetWaveTimer() {
	const gtime_t currentTime = level.time;
	gtime_t remainingTime = 0_sec;

	// Calcular tiempo de condición si está activa
	if (g_horde_local.conditionTriggered && g_horde_local.waveEndTime > currentTime) {
		remainingTime = g_horde_local.waveEndTime - currentTime;
	}

	// Calcular tiempo independiente
	gtime_t independentRemaining = g_independent_timer_start + g_lastParams.independentTimeThreshold - currentTime;

	// Siempre retornar el menor tiempo entre ambos si son válidos
	if (independentRemaining > 0_sec) {
		remainingTime = (remainingTime > 0_sec) ?
			std::min(remainingTime, independentRemaining) :
			independentRemaining;
	}

	return remainingTime;
}