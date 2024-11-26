// Includes y definiciones relevantes
#include "../g_local.h"
#include "g_horde.h"
#include "../shared.h"
#include <set>
#include "g_horde_benefits.h"

// Definir tamaños máximos para arrays estáticos
constexpr size_t MAX_ELIGIBLE_BOSSES = 16;
constexpr size_t MAX_ELIGIBLE_MONSTERS = 32;
constexpr size_t MAX_ELIGIBLE_ITEMS = 32;
constexpr size_t MAX_RECENT_BOSSES = 3;
constexpr size_t MAX_SPAWN_POINTS = 32;

static constexpr size_t NUM_WAVE_SOUNDS = 12;
static constexpr size_t NUM_START_SOUNDS = 9;

//precache//
// Arrays estáticos de cached_soundindex
static cached_soundindex wave_sounds[NUM_WAVE_SOUNDS];
static cached_soundindex start_sounds[NUM_START_SOUNDS];
static cached_soundindex sound_tele3;      // Para teleport
static cached_soundindex sound_tele_up;     // Para teleport escape
static cached_soundindex sound_spawn1;      // Para spawn de monstruos

// Arrays de strings con los nombres de los sonidos
static constexpr const char* WAVE_SOUND_PATHS[NUM_WAVE_SOUNDS] = {
	"nav_editor/action_fail.wav",
	"nav_editor/clear_test_node.wav",
	"makron/roar1.wav",
	"zortemp/ack.wav",
	"makron/voice3.wav",
	"world/v_fac3.wav",
	"makron/voice4.wav",
	"world/battle2.wav",
	"world/battle3.wav",
	"world/battle4.wav",
	"world/battle5.wav",
	"misc/alarm.wav"
};

static constexpr const char* START_SOUND_PATHS[NUM_START_SOUNDS] = {
	"misc/r_tele3.wav",
	"world/fish.wav",
	"world/klaxon2.wav",
	"world/klaxon3.wav",
	"misc/tele_up.wav",
	"world/incoming.wav",
	"world/redforceact.wav",
	"makron/voice2.wav",
	"makron/voice.wav"
};

static const char* GetCurrentMapName() noexcept {
	return static_cast<const char*>(level.mapname);
}

enum class WaveEndReason {
	AllMonstersDead,
	MonstersRemaining,
	TimeLimitReached
};

inline int8_t GetNumActivePlayers();
inline int8_t GetNumSpectPlayers();
inline int8_t GetNumHumanPlayers();

constexpr int8_t MAX_MONSTERS_BIG_MAP = 32;
constexpr int8_t MAX_MONSTERS_MEDIUM_MAP = 16;
constexpr int8_t MAX_MONSTERS_SMALL_MAP = 14;

bool allowWaveAdvance = false; // Global variable to control wave advancement
bool boss_spawned_for_wave = false; // Variable de control para el jefe
bool flying_monsters_mode = false; // Variable de control para el jefe volador

int8_t last_wave_number = 0;              // Reducido de uint64_t
uint16_t g_totalMonstersInWave = 0;         // Reducido de uint32_t

gtime_t horde_message_end_time = 0_sec;
gtime_t SPAWN_POINT_COOLDOWN = 2.8_sec; //spawns Cooldown 

// Añadir cerca de las otras constexpr al inicio del archivo
static constexpr gtime_t GetBaseSpawnCooldown(bool isSmallMap, bool isBigMap) {
	if (isSmallMap)
		return 0.3_sec;
	else if (isBigMap)
		return 1.8_sec;
	else
		return 1.0_sec;
}

// Nueva función para calcular el factor de escala del cooldown basado en el nivel
static float CalculateCooldownScale(int32_t lvl, const MapSize& mapSize) {
	if (lvl <= 10)
		return 1.0f;

	const int32_t numHumanPlayers = GetNumHumanPlayers();

	// Factores de ajuste constantes
	constexpr float LVL_SCALE = 0.02f;
	constexpr float PLAYER_REDUCTION = 0.08f;
	constexpr float MAX_REDUCTION = 0.45f;

	// Escala base por nivel
	float scale = 1.0f + ((lvl - 10) * LVL_SCALE);

	// Ajuste por número de jugadores
	if (numHumanPlayers > 1) {
		scale *= (1.0f - std::min((numHumanPlayers - 1) * PLAYER_REDUCTION, MAX_REDUCTION));
	}

	// Ajustes por tamaño de mapa usando constexpr
	constexpr float SMALL_MAP_MULTIPLIER = 0.7f;
	constexpr float SMALL_MAP_MAX_SCALE = 1.3f;
	constexpr float MEDIUM_MAP_MULTIPLIER = 0.80f;
	constexpr float MEDIUM_MAP_MAX_SCALE = 1.5f;
	constexpr float BIG_MAP_MULTIPLIER = 0.85f;
	constexpr float BIG_MAP_MAX_SCALE = 1.75f;

	// Seleccionar valores basados en el tamaño del mapa usando if-else
	float multiplier, maxScale;
	if (mapSize.isSmallMap) {
		multiplier = SMALL_MAP_MULTIPLIER;
		maxScale = SMALL_MAP_MAX_SCALE;
	}
	else if (mapSize.isBigMap) {
		multiplier = BIG_MAP_MULTIPLIER;
		maxScale = BIG_MAP_MAX_SCALE;
	}
	else {
		multiplier = MEDIUM_MAP_MULTIPLIER;
		maxScale = MEDIUM_MAP_MAX_SCALE;
	}

	return std::min(scale * multiplier, maxScale);
}

cvar_t* g_horde;
#include <span>

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
    bool            warningIssued[4] = { false, false, false, false };

    gtime_t         last_successful_hud_update = 0_sec;
    uint32_t        failed_updates_count = 0;

	MapSize current_map_size;
	int32_t max_monsters{};  // Cacheado basado en map_size
	gtime_t base_spawn_cooldown;  // Cacheado basado en map_size

	void update_map_size(const char* mapname) {
		current_map_size = GetMapSize(mapname);
		max_monsters = current_map_size.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
			(current_map_size.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP);
		base_spawn_cooldown = GetBaseSpawnCooldown(
			current_map_size.isSmallMap,
			current_map_size.isBigMap
		);
	}

    void reset_hud_state() {
        last_successful_hud_update = 0_sec;
        failed_updates_count = 0;
    }
} g_horde_local;

// Clase de selección genérica usando templates
// Nueva implementación de WeightedSelection usando arrays estáticos
// Implementación optimizada de WeightedSelection usando std::span
template <typename T>
struct WeightedSelection {
	static constexpr size_t MAX_ITEMS = 32;

	struct ItemEntry {
		const T* item;
		float weight;
	};

	std::array<ItemEntry, MAX_ITEMS> items;
	size_t item_count = 0;
	float total_weight = 0.0f;

	void clear() {
		item_count = 0;
		total_weight = 0.0f;
	}

	bool add(const T* item, float weight) {
		if (!item || item_count >= MAX_ITEMS || weight <= 0.0f)
			return false;

		items[item_count] = { item, weight };
		total_weight += weight;
		item_count++;
		return true;
	}

	const T* select() const {
		if (item_count == 0 || total_weight <= 0.0f)
			return nullptr;

		// Crear vista de los items actuales usando span
		std::span<const ItemEntry> items_view{ items.data(), item_count };

		// Generar valor aleatorio
		std::uniform_real_distribution<float> dist(0.0f, total_weight);
		const float random_weight = dist(mt_rand);

		// Búsqueda binaria optimizada usando span
		float cumulative = 0.0f;
		size_t left = 0;
		size_t right = item_count - 1;

		while (left <= right) {
			const size_t mid = (left + right) / 2;
			cumulative += items_view[mid].weight;

			if (cumulative >= random_weight) {
				// Encontrado el item
				return items_view[mid].item;
			}
			else if (mid == right) {
				// Caso especial: último elemento
				return items_view[right].item;
			}

			if (cumulative < random_weight) {
				left = mid + 1;
				if (left > right) break;
			}
			else {
				if (mid == 0) break;
				right = mid - 1;
			}
		}

		// Fallback al último item si algo sale mal
		return items_view[item_count - 1].item;
	}

	// Método helper para selección por rango
	const T* select_range(float min_weight, float max_weight) const {
		if (item_count == 0 || total_weight <= 0.0f)
			return nullptr;

		std::span<const ItemEntry> items_view{ items.data(), item_count };

		// Filtrar items por rango de peso
		std::array<const ItemEntry*, MAX_ITEMS> eligible_items;
		size_t eligible_count = 0;
		float eligible_total = 0.0f;

		for (const auto& entry : items_view) {
			if (entry.weight >= min_weight && entry.weight <= max_weight) {
				eligible_items[eligible_count++] = &entry;
				eligible_total += entry.weight;
			}
		}

		if (eligible_count == 0)
			return nullptr;

		// Selección aleatoria solo de items elegibles
		std::uniform_real_distribution<float> dist(0.0f, eligible_total);
		const float random_weight = dist(mt_rand);

		float cumulative = 0.0f;
		for (size_t i = 0; i < eligible_count; ++i) {
			cumulative += eligible_items[i]->weight;
			if (cumulative >= random_weight)
				return eligible_items[i]->item;
		}

		return eligible_items[eligible_count - 1]->item;
	}

	// Iterador para span de items activos
	std::span<const ItemEntry> items_view() const {
		return std::span<const ItemEntry>{items.data(), item_count};
	}
};

int8_t current_wave_level = g_horde_local.level;
bool next_wave_message_sent = false;
auto auto_spawned_bosses = std::unordered_set<edict_t*>{};
auto lastMonsterSpawnTime = std::unordered_map<std::string, gtime_t>{};
auto lastSpawnPointTime = std::unordered_map<edict_t*, gtime_t>{};
struct SpawnPointData {
	uint16_t attempts = 0;
	gtime_t spawn_cooldown = 0_sec;     // Regular spawn cooldown
	gtime_t teleport_cooldown = 0_sec;  // Teleport cooldown
	gtime_t lastSpawnTime = 0_sec;
	uint16_t successfulSpawns = 0;
	bool isTemporarilyDisabled = false;
	gtime_t cooldownEndsAt = 0_sec;
};

std::unordered_map<edict_t*, SpawnPointData> spawnPointsData;



const std::unordered_set<std::string> smallMaps = {
	"q2dm3", "q2dm7", "q2dm2", "q64/dm10", "test/mals_barrier_test",
	"q64/dm9", "q64/dm7", "q64\\dm7", "q64/dm2", "test/spbox",
	"q64/dm1", "fact3", "q2ctf4", "rdm4", "q64/command","mgu3m4",
	"mgu4trial", "mgu6trial", "ec/base_ec", "mgdm1", "ndctf0", "q64/dm6",
	"q64/dm8", "q64/dm4", "q64/dm3", "industry", "e3/jail_e3"
};

const std::unordered_set<std::string> bigMaps = {
	"q2ctf5", "old/kmdm3", "xdm2", "xdm4", "xdm6", "xdm3", "rdm6", "rdm8", "xdm1", "waste2", "rdm5", "rdm9", "rdm12", "sewer64", "base64", "city64"
};

MapSize GetMapSize(const char* mapname) {
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

static int32_t CalculateQueuedMonsters(const MapSize& mapSize, int32_t lvl, bool isHardMode) noexcept {
	if (lvl <= 3)
		return 0;

	// Base más agresiva con mejor cálculo matemático
	float baseQueued = std::sqrt(static_cast<float>(lvl)) * 3.0f;
	baseQueued *= (1.0f + (lvl) * 0.18f);

	// Multiplicadores optimizados por tamaño de mapa
	const float mapSizeMultiplier = mapSize.isSmallMap ? 1.3f :
		mapSize.isBigMap ? 1.5f : 1.4f;

	const int32_t maxQueued = mapSize.isSmallMap ? 30 :
		mapSize.isBigMap ? 45 : 35;

	baseQueued *= mapSizeMultiplier;

	// Bonus exponencial mejorado para niveles altos
	if (lvl > 20) {
		baseQueued *= std::pow(1.15f, std::min(lvl - 20, 18));
	}

	// Ajuste de dificultad mejorado
	if (isHardMode) {
		float difficultyMultiplier = 1.25f;
		if (lvl > 25) {
			difficultyMultiplier += (lvl - 25) * 0.025f;
			difficultyMultiplier = std::min(difficultyMultiplier, 1.75f);
		}
		baseQueued *= difficultyMultiplier;
	}

	// Factor de reducción final
	baseQueued *= 0.85f;

	return std::min(static_cast<int32_t>(baseQueued), maxQueued);
}

static void UnifiedAdjustSpawnRate(const MapSize& mapSize, int32_t lvl, int32_t humanPlayers) noexcept {
	// Lookup table para base counts
	constexpr std::array<std::array<int32_t, 4>, 3> BASE_COUNTS = { {
		{{6, 8, 10, 12}},  // Small maps
		{{8, 12, 14, 16}}, // Medium maps
		{{15, 18, 23, 26}} // Large maps
	} };

	const size_t mapIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);
	const size_t levelIndex = lvl <= 5 ? 0 : (lvl <= 10 ? 1 : (lvl <= 15 ? 2 : 3));

	int32_t baseCount = BASE_COUNTS[mapIndex][levelIndex];

	// Ajuste de jugadores optimizado
	if (humanPlayers > 1) {
		baseCount = static_cast<int32_t>(baseCount * (1.0f + ((humanPlayers - 1) * 0.2f)));
	}

	// Cálculo de spawns adicionales optimizado
	constexpr std::array<int32_t, 3> ADDITIONAL_SPAWNS = { 8, 7, 12 }; // Small, Medium, Large
	int32_t additionalSpawn = (lvl >= 8) ? ADDITIONAL_SPAWNS[mapIndex] : 6;

	// Optimizar cálculo de cooldown
	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);
	const float cooldownScale = CalculateCooldownScale(lvl, mapSize);
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	// Scaling para niveles altos
	if (lvl > 25) {
		additionalSpawn = static_cast<int32_t>(additionalSpawn * 1.6f);
	}

	// Bonus para modos difíciles
	if (lvl >= 3 && (g_chaotic->integer || g_insane->integer)) {
		additionalSpawn += CalculateChaosInsanityBonus(lvl);
		SPAWN_POINT_COOLDOWN *= 0.95f;
	}

	// Ajuste dinámico final
	const float difficultyMultiplier = 1.0f + (humanPlayers - 1) * 0.075f;
	if (lvl % 3 == 0) {
		baseCount = static_cast<int32_t>(baseCount * difficultyMultiplier);
		SPAWN_POINT_COOLDOWN = std::max(
			SPAWN_POINT_COOLDOWN - gtime_t::from_sec((mapSize.isBigMap ? 0.1f : 0.15f) * difficultyMultiplier),
			1.0_sec
		);
	}

	// Apply final limits
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN, 1.0_sec, 3.0_sec);
	g_horde_local.num_to_spawn = baseCount + additionalSpawn;
	ClampNumToSpawn(mapSize);

	// Actualizar cola con sistema optimizado
	const bool isHardMode = g_insane->integer || g_chaotic->integer;
	g_horde_local.queued_monsters = CalculateQueuedMonsters(mapSize, lvl, isHardMode);

	if (developer->integer) {// Debug info mejorado
		gi.Com_PrintFmt("DEBUG: Wave {} settings:\n", lvl);
		gi.Com_PrintFmt("  - Spawn cooldown: {:.2f}s (Scale {:.2f}x)\n",
			SPAWN_POINT_COOLDOWN.seconds(), cooldownScale);
		gi.Com_PrintFmt("  - Base monsters: {}\n", baseCount);
		gi.Com_PrintFmt("  - Additional spawns: {}\n", additionalSpawn);
		gi.Com_PrintFmt("  - Queued monsters: {}\n", g_horde_local.queued_monsters);
		gi.Com_PrintFmt("  - Map type: {}\n",
			mapSize.isBigMap ? "big" : (mapSize.isSmallMap ? "small" : "medium"));
	}
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
	params.timeThreshold += gtime_t::from_ms(75ll * std::min(lvl / 3, 4)); // Reducido el incremento de tiempo

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
	g_horde_local.update_map_size(GetCurrentMapName());
	g_independent_timer_start = level.time;

	// Configuración de variables iniciales para el nivel
	g_totalMonstersInWave = g_horde_local.num_to_spawn;
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

	g_lastParams = GetConditionParams(g_horde_local.current_map_size, GetNumHumanPlayers(), lvl);

	// Ajustar la escala de daño según el nivel
	switch (lvl) {
	case 15:
		gi.cvar_set("g_damage_scale", "1.5");
		break;
	case 25:
		gi.cvar_set("g_damage_scale", "2.0");
		break;
	case 35:
		gi.cvar_set("g_damage_scale", "3.0");
		break;
	case 45:
		gi.cvar_set("g_damage_scale", "3.75");
		break;
	default:
		break;
	}

	UnifiedAdjustSpawnRate(g_horde_local.current_map_size, lvl, GetNumHumanPlayers());

	CheckAndApplyBenefit(lvl);
	ResetAllSpawnAttempts();
	ResetCooldowns();
	Horde_CleanBodies();

	gi.Com_PrintFmt("PRINT: Horde level initialized: {}\n", lvl);
}

bool G_IsDeathmatch() noexcept {
	return deathmatch->integer;
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

	{ "item_ir_goggles", 10, -1, 0.03f, adjust_weight_powerup },
	{ "item_quad", 6, -1, 0.04f, adjust_weight_powerup },
	{ "item_double", 4, -1, 0.05f, adjust_weight_powerup },
	{ "item_quadfire", 2, -1, 0.04f, adjust_weight_powerup },
	{ "item_invulnerability", 4, -1, 0.03f, adjust_weight_powerup },
	{ "item_sphere_defender", 2, -1, 0.05f, adjust_weight_powerup },
	{ "item_sphere_vengeance", 23, -1, 0.06f, adjust_weight_powerup },
	{ "item_sphere_hunter", 9, -1, 0.04f, adjust_weight_powerup },
	{ "item_invisibility", 4, -1, 0.06f, adjust_weight_powerup },
	{ "item_teleport_device", 4, -1, 0.06f, adjust_weight_powerup },
	{ "item_doppleganger", 5, -1, 0.038f, adjust_weight_powerup },
	{ "item_sentrygun", 2, 8, 0.028f, adjust_weight_powerup },
	{ "item_sentrygun", 9, 19, 0.062f, adjust_weight_powerup },
	{ "item_sentrygun", 9, 19, 0.062f, adjust_weight_powerup },
	{ "item_sentrygun", 20, -1, 0.1f, adjust_weight_powerup },

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
	{ "ammo_nuke", 12, -1, 0.03f, adjust_weight_ammo },

	{ "item_bandolier", 4, -1, 0.2f, adjust_weight_ammo },
	{ "item_pack", 15, -1, 0.34f, adjust_weight_ammo },
	{ "item_silencer", 15, -1, 0.1f, adjust_weight_ammo },
};


constexpr weighted_item_t monsters[] = {
	// Enemigos básicos (mejor distribuidos)
	{ "monster_soldier_light", -1, -1, 0.25f },
	{ "monster_soldier", -1, -1, 0.23f },
	{ "monster_soldier_ss", -1, -1, 0.20f },
	{ "monster_infantry_vanilla", -1, 3, 0.25f },
	{ "monster_infantry_vanilla", 3, 17, 0.28f },     // Adelantado a ola 3

	// Enemigos intermedios (progresión más suave)
	{ "monster_soldier_hypergun", 4, -1, 0.15f },     // Adelantado a ola 4
	{ "monster_soldier_lasergun", 8, -1, 0.25f },     // Retrasado a ola 8
	{ "monster_soldier_ripper", 6, -1, 0.18f },
	{ "monster_infantry", 9, -1, 0.28f },

	// Enemigos de apoyo temprano
	{ "monster_medic", 4, 12, 0.08f },                // Adelantado a ola 4
	{ "monster_medic", 13, -1, 0.09f },
	{ "monster_medic_commander", 27, -1, 0.08f },

	// Voladores básicos y early challengers
	{ "monster_flyer", -1, 4, 0.1f },
	{ "monster_flyer", 5, -1, 0.14f },
	{ "monster_hover_vanilla", 7, 25, 0.14f },        // Adelantado a ola 7
	{ "monster_hover", 17, -1, 0.16f },
	{ "monster_gekk", 3, 5, 0.12f },                  // Cambiado a olas 3-5
	{ "monster_gekk", 7, 23, 0.13f },

	// Enemigos técnicos y Gunners (mejor espaciados)
	{ "monster_fixbot", 8, 19, 0.11f },               // Ajustado a ola 8
	{ "monster_gunner_vanilla", 5, 15, 0.25f },       // Adelantado a ola 5
	{ "monster_gunner", 12, -1, 0.28f },
	{ "monster_guncmdr_vanilla", 10, 15, 0.15f },
	{ "monster_guncmdr", 14, -1, 0.25f },

	// Enemigos especializados (mejor distribuidos)
	{ "monster_brain", 9, 14, 0.18f },                // Ajustado a ola 9
	{ "monster_brain", 15, -1, 0.25f },
	{ "monster_stalker", 6, 15, 0.14f },              // Adelantado a ola 6
	{ "monster_parasite", 4, 16, 0.15f },             // Adelantado a ola 4

	// Tanques y variantes (progresión más gradual)
	{ "monster_tank_spawner", 3, 6, 0.015f },         // Adelantado a ola 3
	{ "monster_tank_spawner", 7, 12, 0.06f },
	{ "monster_tank_spawner", 13, 25, 0.15f },
	{ "monster_tank_spawner", 26, -1, 0.2f },
	{ "monster_tank", 14, -1, 0.25f },
	{ "monster_tank_commander", 15, -1, 0.15f },      // Retrasado a ola 15
	{ "monster_runnertank", 16, -1, 0.22f },

	// Voladores avanzados (mejor espaciados)
	{ "monster_floater", 11, -1, 0.22f },             // Adelantado a ola 11
	{ "monster_floater_tracker", 22, -1, 0.16f },
	{ "monster_daedalus", 18, -1, 0.12f },
	{ "monster_daedalus_bomber", 27, -1, 0.14f },
	{ "monster_daedalus_bomber", 7, 26, 0.038f },     // Adelantado a ola 5

	// Mutantes y variantes (mejor distribuidos)
	{ "monster_mutant", 6, 12, 0.08f },               // Adelantado a ola 3
	{ "monster_mutant", 13, -1, 0.25f },
	{ "monster_redmutant", 14, 21, 0.03f },
	{ "monster_redmutant", 22, -1, 0.14f },
	{ "monster_berserk", 7, -1, 0.25f },              // Retrasado a ola 8

	// Gladiators (mejor espaciados)
	{ "monster_gladiator", 11, -1, 0.35f },
	{ "monster_gladb", 13, -1, 0.25f },
	{ "monster_gladc", 18, -1, 0.28f },

	// Cazadores y arácnidos
	{ "monster_chick", 7, 20, 0.25f },                // Adelantado a ola 5
	{ "monster_chick_heat", 13, -1, 0.3f },
	{ "monster_spider", 15, 31, 0.25f },
	{ "monster_gm_arachnid", 29, -1, 0.22f },
	{ "monster_psxarachnid", 32, -1, 0.22f },
	{ "monster_arachnid", 23, 31, 0.25f },

	// Mini-jefes y especiales (mantenidos igual)
	{ "monster_shambler", 15, 25, 0.08f },
	{ "monster_shambler", 26, -1, 0.25f },
	{ "monster_tank_64", 28, -1, 0.13f },
	{ "monster_janitor", 21, -1, 0.12f },
	{ "monster_janitor2", 26, 34, 0.1f },
	{ "monster_janitor2", 35, -1, 0.25f },

	{ "monster_boss2_64", 19, -1, 0.09f },
	{ "monster_carrier_mini", 27, -1, 0.09f },

	//last waves
	{ "monster_boss2kl", 46, -1, 0.2f },
	{ "monster_shamblerkl", 33, -1, 0.15f },
	{ "monster_makron", 16, 32, 0.015f },
	{ "monster_makron", 36, -1, 0.04f },
	{ "monster_guncmdrkl", 33, -1, 0.04f },
	{ "monster_makronkl", 41, -1, 0.015f },
	{ "monster_perrokl", 20, -1, 0.25f },
	{ "monster_widow1", 29, 34, 0.1f },
	{ "monster_widow1", 35, -1, 0.25f }
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

// Listas de jefes con probabilidades modificadas
constexpr boss_t BOSS_SMALL[] = {
	{"monster_carrier_mini", 24, -1, 0.1f, BossSizeCategory::Small},
	{"monster_boss2kl", 24, -1, 0.1f, BossSizeCategory::Small},
	{"monster_widow2", 19, -1, 0.1f, BossSizeCategory::Small},
	{"monster_tank_64", -1, 20, 0.25f, BossSizeCategory::Small},  // Aumentada probabilidad hasta nivel 20
	{"monster_shamblerkl", -1, 20, 0.3f, BossSizeCategory::Small},  // Aumentada probabilidad hasta nivel 20
	{"monster_guncmdrkl", -1, 20, 0.3f, BossSizeCategory::Small},  // Aumentada probabilidad hasta nivel 20
	{"monster_tank_64", 21, -1, 0.1f, BossSizeCategory::Small},  // Normal después del nivel 20
	{"monster_shamblerkl", 21, -1, 0.1f, BossSizeCategory::Small},  // Normal después del nivel 20
	{"monster_guncmdrkl", 21, -1, 0.1f, BossSizeCategory::Small},  // Normal después del nivel 20
	{"monster_makronkl", 36, -1, 0.1f, BossSizeCategory::Small},
	{"monster_psxarachnid", 15, -1, 0.1f, BossSizeCategory::Small},
	{"monster_redmutant", -1, 24, 0.1f, BossSizeCategory::Small}
};

constexpr boss_t BOSS_MEDIUM[] = {
	{"monster_carrier", 24, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_boss2", 19, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_tank_64", -1, 20, 0.45f, BossSizeCategory::Medium},  // Aumentada probabilidad hasta nivel 20
	{"monster_shamblerkl", -1, 20, 0.3f, BossSizeCategory::Medium},  // Aumentada probabilidad hasta nivel 20
	{"monster_guncmdrkl", -1, 20, 0.3f, BossSizeCategory::Medium},  // Aumentada probabilidad hasta nivel 20
	{"monster_tank_64", 21, -1, 0.1f, BossSizeCategory::Medium},  // Normal después del nivel 20
	{"monster_shamblerkl", 21, -1, 0.1f, BossSizeCategory::Medium},  // Normal después del nivel 20
	{"monster_guncmdrkl", 21, -1, 0.1f, BossSizeCategory::Medium},  // Normal después del nivel 20
	{"monster_psxguardian", -1, 24, 0.1f, BossSizeCategory::Medium},
	{"monster_widow2", 19, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_psxarachnid", -14, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_makronkl", 26, -1, 0.1f, BossSizeCategory::Medium}
};

constexpr boss_t BOSS_LARGE[] = {
	{"monster_carrier", 24, -1, 0.1f, BossSizeCategory::Large},
	{"monster_boss2", 19, -1, 0.1f, BossSizeCategory::Large},
	{"monster_boss5", -1, -1, 0.1f, BossSizeCategory::Large},
	{"monster_tank_64", -1, 20, 0.45f, BossSizeCategory::Large},  // Aumentada probabilidad hasta nivel 20
	{"monster_shamblerkl", -1, 20, 0.3f, BossSizeCategory::Large},  // Aumentada probabilidad hasta nivel 20
	{"monster_guncmdrkl", -1, 20, 0.3f, BossSizeCategory::Large},  // Aumentada probabilidad hasta nivel 20
	{"monster_tank_64", 21, -1, 0.1f, BossSizeCategory::Large},  // Normal después del nivel 20
	{"monster_shamblerkl", 21, -1, 0.1f, BossSizeCategory::Large},  // Normal después del nivel 20
	{"monster_guncmdrkl", 21, -1, 0.1f, BossSizeCategory::Large},  // Normal después del nivel 20
	{"monster_psxarachnid", 14, -1, 0.1f, BossSizeCategory::Large},
	{"monster_widow", -1, -1, 0.1f, BossSizeCategory::Large},
	{"monster_psxguardian", -1, -1, 0.1f, BossSizeCategory::Large},
	{"monster_boss5", -1, 24, 0.1f, BossSizeCategory::Large},
	{"monster_jorg", 30, -1, 0.1f, BossSizeCategory::Large}
};

// Definir constantes de tamaño para cada arreglo de jefes
constexpr size_t BOSS_SMALL_SIZE = std::size(BOSS_SMALL);
constexpr size_t BOSS_MEDIUM_SIZE = std::size(BOSS_MEDIUM);
constexpr size_t BOSS_LARGE_SIZE = std::size(BOSS_LARGE);

static std::span<const boss_t> GetBossList(const MapSize& mapSize, std::string_view mapname) {
	if (mapSize.isSmallMap || mapname == "q2dm4" || mapname == "q64/comm" || mapname == "test/test_kaiser") {
		return BOSS_SMALL;
	}

	if (mapSize.isMediumMap || mapname == "rdm8" || mapname == "xdm1") {
		if (mapname == "mgu6m3" || mapname == "rboss") {
			static std::vector<boss_t> filteredMediumBossList;
			if (filteredMediumBossList.empty()) {
				filteredMediumBossList.reserve(std::size(BOSS_MEDIUM));
				std::copy_if(std::begin(BOSS_MEDIUM), std::end(BOSS_MEDIUM),
					std::back_inserter(filteredMediumBossList),
					[](const boss_t& boss) noexcept {
						return std::strcmp(boss.classname, "monster_guardian") != 0 &&
							std::strcmp(boss.classname, "monster_psxguardian") != 0;
					});
			}
			return std::span<const boss_t>{filteredMediumBossList};
		}
		return std::span<const boss_t>{BOSS_MEDIUM};
	}

	if (mapSize.isBigMap || mapname == "test/spbox" || mapname == "q2ctf4") {
		if (mapname == "test/spbox" || mapname == "q2ctf4") {
			static std::vector<boss_t> filteredLargeBossList;
			if (filteredLargeBossList.empty()) {
				filteredLargeBossList.reserve(std::size(BOSS_LARGE));
				std::copy_if(std::begin(BOSS_LARGE), std::end(BOSS_LARGE),
					std::back_inserter(filteredLargeBossList),
					[](const boss_t& boss) noexcept {
						return std::strcmp(boss.classname, "monster_boss5") != 0;
					});
			}
			return std::span<const boss_t>{filteredLargeBossList};
		}
		return std::span<const boss_t>{BOSS_LARGE};
	}

	return std::span<const boss_t>{};
}

static size_t GetBossListSize(const MapSize& mapSize, std::string_view mapname, std::span<const boss_t> boss_list) noexcept {
    const boss_t* list_data = boss_list.data();
    if (list_data == &BOSS_SMALL[0]) return BOSS_SMALL_SIZE;
    if (list_data == &BOSS_MEDIUM[0]) return BOSS_MEDIUM_SIZE;
    if (list_data == &BOSS_LARGE[0]) return BOSS_LARGE_SIZE;
    // Para las listas filtradas, simplemente devolvemos el tamaño del span
    return boss_list.size();
}

// Estructuras de arrays estáticos para reemplazar std::vectors
struct EligibleBosses {
	const boss_t* items[MAX_ELIGIBLE_BOSSES] = {};
	size_t count = 0;

	void clear() noexcept { count = 0; }
	void add(const boss_t* boss) noexcept {
		if (count < MAX_ELIGIBLE_BOSSES) {
			items[count++] = boss;
		}
	}
};

// Array estático para bosses recientes
struct RecentBosses {
	const char* items[MAX_RECENT_BOSSES] = {};
	size_t count = 0;

	void add(const char* boss) noexcept {
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
	const weighted_item_t* items[MAX_ELIGIBLE_MONSTERS] = {}; 
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

static const char* G_HordePickBOSS(const MapSize& mapSize, std::string_view mapname, int32_t waveNumber, edict_t* bossEntity) {
	static EligibleBosses eligible_bosses;
	static double cumulative_weights[MAX_ELIGIBLE_BOSSES];
	eligible_bosses.clear();
	double total_weight = 0.0;

	auto const boss_list = GetBossList(mapSize, mapname);
	if (boss_list.empty()) return nullptr;

	const size_t boss_list_size = boss_list.size();
	if (boss_list_size == 0) return nullptr;

	// Recolectar bosses elegibles usando span
	std::span<const boss_t> boss_view{ boss_list };
	for (const auto& boss : boss_view) {
		if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
			(waveNumber <= boss.max_level || boss.max_level == -1) &&
			!recent_bosses.contains(boss.classname)) {

			float adjusted_weight = boss.weight;
			if (waveNumber >= boss.min_level && waveNumber <= boss.min_level + 5) {
				adjusted_weight *= 1.3f;
			}

			if (g_insane->integer || g_chaotic->integer) {
				if (boss.sizeCategory == BossSizeCategory::Large) {
					adjusted_weight *= 1.2f;
				}
			}

			if (boss.min_level != -1 && waveNumber > boss.min_level + 10) {
				adjusted_weight *= 0.8f;
			}

			total_weight += adjusted_weight;
			cumulative_weights[eligible_bosses.count] = total_weight;
			eligible_bosses.add(&boss);
		}
	}

	if (eligible_bosses.count == 0) {
		recent_bosses.clear();
		total_weight = 0.0;

		for (const auto& boss : boss_view) {
			if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
				(waveNumber <= boss.max_level || boss.max_level == -1)) {
				const float adjusted_weight = boss.weight;
				total_weight += adjusted_weight;
				cumulative_weights[eligible_bosses.count] = total_weight;
				eligible_bosses.add(&boss);
			}
		}
	}

	if (eligible_bosses.count == 0) return nullptr;

	const double random_value = std::uniform_real_distribution<double>(0.0, total_weight)(mt_rand);
	size_t left = 0;
	size_t right = eligible_bosses.count - 1;

	while (left < right) {
		const size_t mid = (left + right) / 2;
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

// Estructura optimizada para mantener los datos de selección
struct SelectionCache {
	static constexpr size_t MAX_ENTRIES = 32;
	struct Entry {
		const weighted_item_t* item;
		const char* monster_classname;
		float weight;
		float cumulative_weight;
	};
	_Field_range_(0, MAX_ENTRIES) size_t count = 0;
	float total_weight = 0.0f;
	_Field_size_(MAX_ENTRIES) Entry entries[MAX_ENTRIES] = { 0 };  // Inicializar array

	void clear() noexcept {
		count = 0;
		total_weight = 0.0f;
	}
	_Success_(return != false)
		bool add_entry(_In_ const Entry& new_entry) noexcept {
		if (count >= MAX_ENTRIES) {
			return false;
		}
		entries[count] = new_entry;
		count++;
		return true;
	}
	_Ret_maybenull_
		const Entry* get_entry(_In_range_(0, count) size_t index) const noexcept {
		if (index >= count) {
			return nullptr;
		}
		return &entries[index];
	}
};
static SelectionCache item_cache;
static SelectionCache monster_cache;

gitem_t* G_HordePickItem() {
	// Reset cache
	item_cache.clear();

	// Recolectar items elegibles con mejor localidad de caché usando span
	std::span<const weighted_item_t> items_view{ items };

	for (const auto& item : items_view) {
		if (item_cache.count >= SelectionCache::MAX_ENTRIES)
			break;

		if ((item.min_level == -1 || g_horde_local.level >= item.min_level) &&
			(item.max_level == -1 || g_horde_local.level <= item.max_level)) {

			float adjusted_weight = item.weight;
			if (item.adjust_weight) {
				item.adjust_weight(item, adjusted_weight);
			}

			if (adjusted_weight > 0.0f) {
				item_cache.total_weight += adjusted_weight;
				auto& entry = item_cache.entries[item_cache.count];
				entry.item = &item;
				entry.weight = adjusted_weight;
				entry.cumulative_weight = item_cache.total_weight;
				item_cache.count++;
			}
		}
	}

	if (item_cache.count == 0)
		return nullptr;

	// Generar valor aleatorio una sola vez
	const float random_value = frandom() * item_cache.total_weight;

	// Búsqueda binaria optimizada usando span
	size_t left = 0;
	size_t right = item_cache.count - 1;

	while (left < right) {
		const size_t mid = (left + right) / 2;
		if (item_cache.entries[mid].cumulative_weight < random_value) {
			left = mid + 1;
		}
		else {
			right = mid;
		}
	}

	const auto* chosen_item = item_cache.entries[left].item;
	return chosen_item ? FindItemByClassname(chosen_item->classname) : nullptr;
}

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
	return std::count_if(g_edicts + 1, g_edicts + globals.num_edicts,
		[](const edict_t& ent) {
			return ent.inuse &&
				strcmp(ent.classname, "info_player_deathmatch") == 0 &&
				ent.style == 1;
		});
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
	data.spawn_cooldown = level.time;
	data.teleport_cooldown = level.time;
	data.isTemporarilyDisabled = false;
}

constexpr gtime_t CLEANUP_THRESHOLD = 2_sec;

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

constexpr size_t MAX_SPAWN_POINTS_DATA = 16; // Define un límite razonable

static void CleanUpSpawnPointsData() {
	const gtime_t currentTime = level.time;

	// Remove spawn points that are temporarily disabled and past cooldown
	for (auto it = spawnPointsData.begin(); it != spawnPointsData.end(); ) {
		if (it->second.isTemporarilyDisabled && currentTime > it->second.cooldownEndsAt + CLEANUP_THRESHOLD) {
			if (developer->integer)	gi.Com_PrintFmt("Removed spawn_point at address {} due to extended inactivity.\n", static_cast<void*>(it->first));
			it = spawnPointsData.erase(it);
		}
		else {
			++it;
		}
	}

	// Limit the size of the container
	if (spawnPointsData.size() > MAX_SPAWN_POINTS_DATA) {
		if (developer->integer)	gi.Com_Print("WARNING: spawnPointsData exceeded maximum size. Clearing data.\n");
		spawnPointsData.clear();
	}
}


static void UpdateCooldowns(edict_t* spawn_point, const char* chosen_monster) {
	auto& data = spawnPointsData[spawn_point];
	data.lastSpawnTime = level.time;
	data.spawn_cooldown = level.time + SPAWN_POINT_COOLDOWN;
	data.isTemporarilyDisabled = true;
	data.cooldownEndsAt = level.time + SPAWN_POINT_COOLDOWN;
}

// Function to increase spawn attempts and adjust cooldown as necessary
static void IncreaseSpawnAttempts(edict_t* spawn_point) {
	auto& data = spawnPointsData[spawn_point];
	data.attempts++;

	// Verificar si hay jugadores cerca antes de desactivar
	bool players_nearby = false;
	for (const auto* const player : active_players()) {
		if ((spawn_point->s.origin - player->s.origin).length() < 300.0f) {
			players_nearby = true;
			break;
		}
	}

	// Si hay jugadores cerca, ser más tolerante con los reintentos
	const int max_attempts = players_nearby ? 5 : 4;

	if (data.attempts >= max_attempts) {
		if (developer->integer)
			gi.Com_PrintFmt("PRINT: SpawnPoint at position ({}, {}, {}) inactivated.\n",
				spawn_point->s.origin[0], spawn_point->s.origin[1], spawn_point->s.origin[2]);

		data.isTemporarilyDisabled = true;

		// Tiempo de desactivación más corto si hay jugadores cerca
		const gtime_t cooldown = players_nearby ? 2_sec : 3_sec;
		data.cooldownEndsAt = level.time + cooldown;
	}
	else if (data.attempts % 3 == 0) {
		// Incremento más gradual del cooldown
		data.cooldownEndsAt = std::max(data.cooldownEndsAt + 1_sec, 2_sec);
	}

	// Resetear intentos si ha pasado suficiente tiempo
	if (level.time - data.lastSpawnTime > 5_sec) {
		data.attempts = 0;
	}
}

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
static bool IsSpawnPointOccupied(const edict_t* spawn_point, const edict_t* ignore_ent = nullptr) {
	// Define el espacio adicional usando vec3_t
	const vec3_t space_multiplier{ 2.5f, 2.5f, 2.5f };
	const vec3_t spawn_mins = spawn_point->s.origin - (vec3_t{ 16, 16, 24 }.scaled(space_multiplier));
	const vec3_t spawn_maxs = spawn_point->s.origin + (vec3_t{ 16, 16, 32 }.scaled(space_multiplier));

	FilterData filter_data = { ignore_ent, 0 };
	gi.BoxEdicts(spawn_mins, spawn_maxs, nullptr, 0, AREA_SOLID, SpawnPointFilter, &filter_data);

	return filter_data.count > 0;
}

static const char* G_HordePickMonster(edict_t* spawn_point) {
	auto& data = spawnPointsData[spawn_point];
	if (data.isTemporarilyDisabled) {
		if (level.time < data.cooldownEndsAt)
			return nullptr;
		data.isTemporarilyDisabled = false;
		data.attempts = 0;
	}

	if (IsSpawnPointOccupied(spawn_point)) {
		IncreaseSpawnAttempts(spawn_point);
		return nullptr;
	}

	// Reset monster cache
	monster_cache.clear();

	// Cache valores frecuentemente usados
	const int32_t currentLevel = g_horde_local.level;
	const int32_t flyingSpawns = countFlyingSpawns();
	const float adjustmentFactor = adjustFlyingSpawnProbability(flyingSpawns);
	const bool isSpawnPointFlying = spawn_point->style == 1;

	// Crear vista de los monstruos usando span
	std::span<const weighted_item_t> monsters_view{ monsters };

	// Precalcular flags de fase del juego
	const struct GamePhase {
		bool early_game;
		bool early_mid_game;
		bool mid_game;
		bool late_game;
	} phase = {
		currentLevel <= 5,
		currentLevel <= 10,
		currentLevel <= 15,
		currentLevel > 15
	};

	// Recolectar monstruos elegibles con mejor localidad de caché
	for (const auto& monster : monsters_view) {
		if (monster_cache.count >= SelectionCache::MAX_ENTRIES)
			break;

		const bool isFlyingMonster = IsFlyingMonster(monster.classname);

		// Verificaciones de elegibilidad optimizadas
		if (!IsMonsterEligible(spawn_point, monster, isFlyingMonster, currentLevel, flyingSpawns))
			continue;

		if ((monster.min_level != -1 && currentLevel < monster.min_level) ||
			(monster.max_level != -1 && currentLevel > monster.max_level))
			continue;

		float weight = monster.weight;

		// Ajustes de peso optimizados usando la estructura phase
		if (phase.early_game) {
			if (monster.min_level <= 3)
				weight *= 1.4f;
		}
		else if (phase.early_mid_game) {
			if (monster.min_level >= 4 && monster.min_level <= 8)
				weight *= 1.3f;
		}
		else if (phase.mid_game) {
			if (monster.min_level >= 8 && monster.min_level <= 12)
				weight *= 1.25f;
		}
		else {
			if (monster.min_level >= 12)
				weight *= 1.35f + ((currentLevel - 15) * 0.02f);
		}

		// Ajustes de modo y spawn point
		if (flying_monsters_mode)
			weight *= isFlyingMonster ? 2.0f : 0.5f;

		if (isSpawnPointFlying && isFlyingMonster)
			weight *= 1.5f;

		// Ajuste por dificultad
		if (g_insane->integer || g_chaotic->integer) {
			const float difficultyScale = currentLevel <= 10 ? 1.1f :
				currentLevel <= 20 ? 1.2f :
				currentLevel <= 30 ? 1.3f : 1.4f;

			weight *= difficultyScale * (monster.min_level >= 10 ? 1.2f : 1.0f);
		}

		weight *= adjustmentFactor;

		if (weight > 0.0f) {
			monster_cache.total_weight += weight;
			auto& entry = monster_cache.entries[monster_cache.count];
			entry.monster_classname = monster.classname;
			entry.weight = weight;
			entry.cumulative_weight = monster_cache.total_weight;
			monster_cache.count++;
		}
	}

	if (monster_cache.count == 0) {
		IncreaseSpawnAttempts(spawn_point);
		return nullptr;
	}

	// Selección usando búsqueda binaria optimizada
	const float random_value = frandom() * monster_cache.total_weight;
	size_t left = 0;
	size_t right = monster_cache.count - 1;

	while (left < right) {
		const size_t mid = (left + right) / 2;
		if (monster_cache.entries[mid].cumulative_weight < random_value) {
			left = mid + 1;
		}
		else {
			right = mid;
		}
	}

	const char* chosen_monster = monster_cache.entries[left].monster_classname;
	if (chosen_monster) {
		UpdateCooldowns(spawn_point, chosen_monster);
	}

	return chosen_monster;
}

void Horde_PreInit() {
	gi.Com_Print("Horde mode must be DM set <deathmatch 1> and <horde 1>.\n");
	gi.Com_Print("COOP requires <coop 1> and <horde 0>, optionally <g_hardcoop 1/0>.\n");

	g_horde = gi.cvar("horde", "0", CVAR_LATCH);
	//gi.Com_Print("After starting a normal server type: starthorde to start a game.\n");


	if (!g_horde->integer) return;

	if (!deathmatch->integer || ctf->integer || teamplay->integer || coop->integer) {
		gi.Com_Print("Horde mode must be DM.\n");
		//gi.cvar_set("deathmatch", "1");
		//gi.cvar_set("ctf", "0");
		//gi.cvar_set("teamplay", "0");
		//gi.cvar_set("coop", "0");
		//gi.cvar_set("timelimit", "20");
		//gi.cvar_set("fraglimit", "0");
	}

	if (deathmatch->integer && !g_horde->integer)
	gi.cvar_set("g_coop_player_collision", "0");
	gi.cvar_set("g_coop_squad_respawn", "0");
	gi.cvar_set("g_coop_instanced_items", "0");
	gi.cvar_set("g_disable_player_collision", "0");

	// Configuración automática cuando horde está activo
	if (g_horde->integer) {
		dm_monsters = gi.cvar("dm_monsters", "0", CVAR_SERVERINFO);

		gi.Com_Print("Initializing Horde mode settings...\n");

		// Configuración de tiempo y límites
		gi.cvar_set("timelimit", "50");
		gi.cvar_set("fraglimit", "0");
		gi.cvar_set("capturelimit", "0");

		// Configuración de jugabilidad
		gi.cvar_set("sv_target_id", "1");
		gi.cvar_set("g_speedstuff", "2.3f");
		gi.cvar_set("sv_eyecam", "1");
		gi.cvar_set("g_dm_instant_items", "1");
		gi.cvar_set("g_disable_player_collision", "1");
		gi.cvar_set("g_dm_no_self_damage", "1");
		gi.cvar_set("g_allow_techs", "1");

		// Configuración de physics
		gi.cvar_set("g_override_physics_flags", "-1");

		// Configuración de armas y daño
		gi.cvar_set("g_no_nukes", "0");
		gi.cvar_set("g_instant_weapon_switch", "1");
		gi.cvar_set("g_dm_no_quad_drop", "0");
		gi.cvar_set("g_dm_no_quadfire_drop", "0");

		// Configuración del hook/grapple
		gi.cvar_set("g_use_hook", "1");
		gi.cvar_set("g_hook_wave", "1");
		gi.cvar_set("hook_pullspeed", "1200");
		gi.cvar_set("hook_speed", "3000");
		gi.cvar_set("hook_sky", "1");
		gi.cvar_set("g_allow_grapple", "1");
		gi.cvar_set("g_grapple_fly_speed", "3000");
		gi.cvar_set("g_grapple_pull_speed", "1200");

		// Configuración de gameplay específica
		gi.cvar_set("g_startarmor", "0");
		gi.cvar_set("g_vampire", "0");
		gi.cvar_set("g_ammoregen", "0");
		gi.cvar_set("g_tracedbullets", "0");
		gi.cvar_set("g_bouncygl", "0");
		gi.cvar_set("g_bfgpull", "0");
		gi.cvar_set("g_bfgslide", "1");
		gi.cvar_set("g_improvedchaingun", "0");
		gi.cvar_set("g_autohaste", "0");
		gi.cvar_set("g_chaotic", "0");
		gi.cvar_set("g_insane", "0");
		gi.cvar_set("g_hardcoop", "0");

		// Configuración de IA y bots
		gi.cvar_set("g_dm_spawns", "0");
		gi.cvar_set("g_damage_scale", "1");
		gi.cvar_set("ai_allow_dm_spawn", "1");
		gi.cvar_set("ai_damage_scale", "1");
		gi.cvar_set("g_loadent", "1");
		gi.cvar_set("bot_chat_enable", "0");
		gi.cvar_set("bot_skill", "5");
		gi.cvar_set("g_coop_squad_respawn", "1");
		gi.cvar_set("g_iddmg", "1");

		// Activar monstruos automáticamente
		gi.cvar_set("dm_monsters", "0");

		// Resetear el estado del juego
		HandleResetEvent();

		// Mensaje de confirmación
		gi.Com_Print("Horde mode initialized successfully.\n");
	}
}

void VerifyAndAdjustBots() {
	const MapSize mapSize = GetMapSize(static_cast<const char*>(level.mapname));
	const int32_t spectPlayers = GetNumSpectPlayers();  // Solo necesitamos spectPlayers
	const int32_t baseBots = mapSize.isBigMap ? 6 : 4;
	const int32_t requiredBots = std::max(baseBots + spectPlayers, baseBots);

	gi.cvar_set("bot_minClients", std::to_string(requiredBots).c_str());
}

#include <chrono>
void InitializeWaveSystem() noexcept;

// Función para precargar todos los ítems y jefes
static void PrecacheItemsAndBosses() noexcept {
	std::unordered_set<std::string_view> unique_classnames;
	constexpr size_t total_items = std::size(items) + std::size(monsters) +
		std::size(BOSS_SMALL) + std::size(BOSS_MEDIUM) + std::size(BOSS_LARGE);

	unique_classnames.reserve(total_items);

	// Crear vistas span para cada array
	std::span<const weighted_item_t> items_view{ items };
	std::span<const weighted_item_t> monsters_view{ monsters };
	std::span<const boss_t> small_boss_view{ BOSS_SMALL };
	std::span<const boss_t> medium_boss_view{ BOSS_MEDIUM };
	std::span<const boss_t> large_boss_view{ BOSS_LARGE };

	// Llenar el set con los classnames únicos
	for (const auto& item : items_view)
		unique_classnames.emplace(item.classname);

	for (const auto& monster : monsters_view)
		unique_classnames.emplace(monster.classname);

	for (const auto& boss : small_boss_view)
		unique_classnames.emplace(boss.classname);

	for (const auto& boss : medium_boss_view)
		unique_classnames.emplace(boss.classname);

	for (const auto& boss : large_boss_view)
		unique_classnames.emplace(boss.classname);

	// Precargar cada item único
	for (const std::string_view classname : unique_classnames) {
		PrecacheItem(FindItemByClassname(classname.data()));
	}
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
		PrecacheItem(FindItemByClassname(monster.classname));
		G_FreeEdict(e);
	}
}


// Función para precarga de sonidos
static void PrecacheWaveSounds() noexcept {
	// Precachear sonidos individuales
	sound_tele3.assign("misc/r_tele3.wav");
	sound_tele_up.assign("misc/tele_up.wav");
	sound_spawn1.assign("misc/spawn1.wav");

	// Precachear arrays de sonidos
	for (size_t i = 0; i < NUM_WAVE_SOUNDS; ++i) {
		wave_sounds[i].assign(WAVE_SOUND_PATHS[i]);
	}

	for (size_t i = 0; i < NUM_START_SOUNDS; ++i) {
		start_sounds[i].assign(START_SOUND_PATHS[i]);
	}
}

// Agregar un nuevo array para tracking
static std::array<bool, NUM_WAVE_SOUNDS> used_wave_sounds = {};
static size_t remaining_wave_sounds = NUM_WAVE_SOUNDS;

static int GetRandomWaveSound() {
	// Si todos los sonidos han sido usados, resetear
	if (remaining_wave_sounds == 0) {
		std::fill(used_wave_sounds.begin(), used_wave_sounds.end(), false);
		remaining_wave_sounds = NUM_WAVE_SOUNDS;
	}

	// Seleccionar un sonido no usado
	while (true) {
		size_t index = irandom(NUM_WAVE_SOUNDS);
		if (!used_wave_sounds[index]) {
			used_wave_sounds[index] = true;
			remaining_wave_sounds--;
			return wave_sounds[index];
		}
	}
}

static std::array<bool, NUM_START_SOUNDS> used_start_sounds = {};
static size_t remaining_start_sounds = NUM_START_SOUNDS;

static void PlayWaveStartSound() {
	// Si todos los sonidos han sido usados, resetear
	if (remaining_start_sounds == 0) {
		std::fill(used_start_sounds.begin(), used_start_sounds.end(), false);
		remaining_start_sounds = NUM_START_SOUNDS;
	}

	// Seleccionar un sonido no usado
	while (true) {
		size_t index = irandom(NUM_START_SOUNDS);
		if (!used_start_sounds[index]) {
			used_start_sounds[index] = true;
			remaining_start_sounds--;
			gi.sound(world, CHAN_VOICE, start_sounds[index], 1, ATTN_NONE, 0);
			break;
		}
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

////CRAZY BOSS ANIMATION DROP ITEMS
//constexpr float ITEM_ROTATION_SPEED = 550.0f;
//constexpr float ITEM_ORBIT_RADIUS = 254.0f;
//constexpr float ITEM_VERTICAL_OFFSET = 12.0f;
//constexpr float SCATTER_SPEED = 650.0f;
//constexpr float RISE_SPEED_BASE = 70.0f;            // Velocidad base de elevación
//constexpr float RISE_SPEED_VARIANCE = 30.0f;        // Variación de velocidad
//constexpr float MAX_HEIGHT_BASE = 150.0f;           // Altura base máxima
//constexpr float MAX_HEIGHT_VARIANCE = 50.0f;        // Variación de altura
//constexpr float ABSOLUTE_MAX_HEIGHT = 256.0f;
//
//// Estado de la rotación
//enum class ItemState {
//	RISING,
//	ORBITING,
//	SCATTERING,
//	SCATTERED
//};
//
//// Declarar el think function antes de usarlo
//void(Item_Scatter_Think)(edict_t* ent);
//
//THINK(Item_Orbit_Think) (edict_t* ent) -> void {
//	if (!ent || !ent->owner) {
//		G_FreeEdict(ent);
//		return;
//	}
//
//	// Control de estado basado en altura
//	float height_diff = ent->s.origin[2] - ent->owner->s.origin[2];
//	ItemState current_state = static_cast<ItemState>(ent->count);
//
//	// Usamos pos2 para almacenar valores únicos por item
//	if (ent->pos2[0] == 0) { // Si no está inicializado
//		ent->pos2[0] = RISE_SPEED_BASE + (crandom() * RISE_SPEED_VARIANCE);  // Velocidad única
//		ent->pos2[1] = MAX_HEIGHT_BASE + (crandom() * MAX_HEIGHT_VARIANCE);  // Altura máxima única
//	}
//
//	switch (current_state) {
//	case ItemState::RISING: {
//		// Fase de elevación con límite personalizado
//		if (height_diff < ent->pos2[1]) {
//			float rise_amount = std::min(ent->pos2[0] * gi.frame_time_s,
//				ent->pos2[1] - height_diff);
//			ent->s.origin[2] += rise_amount;
//
//			// Añadir pequeña oscilación vertical durante la subida
//			ent->s.origin[2] += sinf(level.time.seconds() * 3.0f + ent->s.number) * 2.0f;
//		}
//		else {
//			ent->count = static_cast<int>(ItemState::ORBITING);
//		}
//		break;
//	}
//
//
//	case ItemState::ORBITING: {
//		// Verificar si es tiempo de dispersar
//		if (level.time >= ent->owner->timestamp) {
//			vec3_t scatter_dir = ent->s.origin - ent->owner->s.origin;
//			scatter_dir.normalize();
//
//			// Añadir más variación vertical en la dispersión
//			scatter_dir[2] *= 0.2f + (crandom() * 0.3f); // 0.2 ± 0.3
//			scatter_dir.normalize();
//
//			// Variación horizontal con componente vertical aleatorio
//			scatter_dir += vec3_t{
//				crandom() * 0.15f,
//				crandom() * 0.15f,
//				crandom() * 0.55f  // Añadido componente vertical aleatorio
//			};
//			scatter_dir.normalize();
//
//			ent->velocity = scatter_dir * (SCATTER_SPEED + (crandom() * 100.0f));
//
//			ent->pos1[2] = ent->owner->s.origin[2];
//			ent->owner = nullptr;
//			ent->think = Item_Scatter_Think;
//			ent->nextthink = level.time + FRAME_TIME_S;
//			return;
//		}
//
//		// Altura objetivo con oscilación suave
//		vec3_t target_pos = ent->owner->s.origin;
//		target_pos[2] += ent->pos2[1] + (sinf(level.time.seconds() * 2.0f + ent->s.number) * 15.0f);
//
//		float height_diff = target_pos[2] - ent->s.origin[2];
//		ent->s.origin[2] += height_diff * 0.1f;
//
//		// Resto del código de órbita igual...
//		vec3_t forward, right, up;
//		AngleVectors(ent->owner->s.angles, forward, right, up);
//
//		float degrees = (ITEM_ROTATION_SPEED * gi.frame_time_s) + ent->owner->delay;
//		vec3_t diff = ent->owner->s.origin - ent->s.origin;
//		diff[2] = 0;
//
//		vec3_t vec = RotatePointAroundVector(up, diff, degrees);
//		ent->s.angles[1] += degrees;
//
//		vec3_t new_origin = ent->owner->s.origin - vec;
//		new_origin[2] = ent->s.origin[2];
//
//		trace_t tr = gi.traceline(ent->s.origin, new_origin, ent, MASK_SOLID | CONTENTS_PLAYERCLIP);
//		if (tr.fraction == 1.0f) {
//			ent->s.origin = new_origin;
//		}
//		break;
//	}
//	}
//
//	// Comprobar agua
//	ent->watertype = gi.pointcontents(ent->s.origin);
//	if (ent->watertype & MASK_WATER)
//		ent->waterlevel = WATER_FEET;
//
//	ent->nextthink = level.time + FRAME_TIME_S;
//	gi.linkentity(ent);
//}
//
//THINK(Item_Scatter_Think) (edict_t* ent) -> void {
//	if (!ent) {
//		G_FreeEdict(ent);
//		return;
//	}
//
//	// Limitar la velocidad vertical para evitar que suban demasiado
//	if (ent->velocity[2] > 100.0f) {
//		ent->velocity[2] = 100.0f;
//	}
//
//	// Comprobar altura máxima absoluta usando pos1.z como referencia
//	if (ent->s.origin[2] > ent->pos1[2] + ABSOLUTE_MAX_HEIGHT) {
//		ent->s.origin[2] = ent->pos1[2] + ABSOLUTE_MAX_HEIGHT;
//		ent->velocity[2] = 0; // Detener movimiento vertical
//	}
//
//	// Velocidad mínima para evitar que se peguen
//	constexpr float MIN_VELOCITY = 60.0f;
//
//	// Comprobar colisiones antes de mover
//	vec3_t next_pos = ent->s.origin + (ent->velocity * gi.frame_time_s);
//	trace_t tr = gi.traceline(ent->s.origin, next_pos, ent, MASK_SOLID | CONTENTS_PLAYERCLIP);
//
//	if (tr.fraction < 1.0f) {
//		// Si estamos empezando en sólido, intentar "empujar" hacia afuera
//		if (tr.startsolid || tr.allsolid) {
//			// Intentar mover en dirección opuesta
//			ent->velocity = -ent->velocity;
//			ent->velocity *= 1.5f; // Dar un empujón extra
//			return;
//		}
//
//		// Calcular rebote con más energía para evitar pegarse
//		vec3_t bounce_vel = SlideClipVelocity(ent->velocity, tr.plane.normal, 1.5f);
//
//		// Si el rebote es muy vertical, agregar componente horizontal
//		if (fabs(tr.plane.normal[2]) > 0.7f) {
//			bounce_vel[0] += crandom() * 100.0f;
//			bounce_vel[1] += crandom() * 100.0f;
//		}
//
//		// Si la velocidad es muy baja, dar un empujón en dirección aleatoria
//		if (bounce_vel.length() < MIN_VELOCITY) {
//			bounce_vel[0] += crandom() * MIN_VELOCITY;
//			bounce_vel[1] += crandom() * MIN_VELOCITY;
//			if (bounce_vel[2] < MIN_VELOCITY * 0.5f)
//				bounce_vel[2] += MIN_VELOCITY * 0.5f;
//		}
//
//		ent->velocity = bounce_vel;
//
//		// Movernos un poco más lejos de la superficie de colisión
//		ent->s.origin = tr.endpos + (tr.plane.normal * 8.0f);
//	}
//	else {
//		ent->s.origin = next_pos;
//	}
//
//	// Reducir velocidad más gradualmente
//	float speed = ent->velocity.length();
//	if (speed > MIN_VELOCITY) {
//		ent->velocity[0] *= 0.99f;
//		ent->velocity[1] *= 0.99f;
//		ent->velocity[2] *= 0.97f; // Más fricción vertical
//	}
//
//	// Gravedad más suave, pero asegurándonos de mantener velocidad mínima
//	ent->velocity[2] -= 175.0f * gi.frame_time_s;
//
//	// Verificar si la velocidad es muy baja y el item está cerca de una pared
//	if (speed < MIN_VELOCITY) {
//		trace_t side_traces[4];
//		vec3_t check_dirs[4] = {
//			{1, 0, 0}, {-1, 0, 0},
//			{0, 1, 0}, {0, -1, 0}
//		};
//
//		bool near_wall = false;
//		for (int i = 0; i < 4; i++) {
//			vec3_t check_pos = ent->s.origin + (check_dirs[i] * 10.0f);
//			side_traces[i] = gi.traceline(ent->s.origin, check_pos, ent, MASK_SOLID | CONTENTS_PLAYERCLIP);
//			if (side_traces[i].fraction < 1.0f) {
//				near_wall = true;
//				break;
//			}
//		}
//
//		if (near_wall) {
//			// Dar un empujón aleatorio si estamos cerca de una pared
//			ent->velocity[0] += crandom() * MIN_VELOCITY * 2.0f;
//			ent->velocity[1] += crandom() * MIN_VELOCITY * 2.0f;
//			ent->velocity[2] += MIN_VELOCITY;
//		}
//	}
//
//	// Solo detenerse si realmente tiene poca velocidad y no está cerca de paredes
//	if (ent->velocity.length() < 5.0f) {
//		trace_t ground_tr = gi.traceline(ent->s.origin, ent->s.origin + vec3_t{ 0, 0, -16.0f },
//			ent, MASK_SOLID | CONTENTS_PLAYERCLIP);
//		if (ground_tr.fraction < 1.0f) {
//			ent->think = nullptr;
//			ent->nextthink = {};
//			ent->movetype = MOVETYPE_TOSS;
//			return;
//		}
//	}
//
//	ent->nextthink = level.time + FRAME_TIME_S;
//	gi.linkentity(ent);
//}
static void OldBossDeathHandler(edict_t* boss)
{
	// Verificación más estricta para el manejo de muerte del boss
	if (!g_horde->integer ||
		!boss ||
		!boss->inuse ||
		!boss->monsterinfo.IS_BOSS ||  // Cambiado de spawnflags
		boss->monsterinfo.BOSS_DEATH_HANDLED ||  // Cambiado de spawnflags
		boss->health > 0) {
		return;
	}

	// Marcar el boss como manejado inmediatamente
	boss->monsterinfo.BOSS_DEATH_HANDLED = true;  // Cambiado de spawnflags

	OnEntityDeath(boss);
	OnEntityRemoved(boss);
	auto_spawned_bosses.erase(boss);

	// Items normales que el boss dropea (array estático)
	static const std::array<const char*, 8> itemsToDrop = {
		"item_adrenaline", "item_pack", "item_sentrygun",
		"item_sphere_defender", "item_armor_combat", "item_bandolier",
		"item_invulnerability", "ammo_nuke"
	};

	// Dropear un arma especial de nivel superior
	if (const char* weapon_classname = SelectBossWeaponDrop(current_wave_level)) {
		if (edict_t* weapon = Drop_Item(boss, FindItemByClassname(weapon_classname))) {

			const vec3_t base_velocity = vec3_t{
				static_cast<float>(MIN_VELOCITY),
				static_cast<float>(MIN_VELOCITY),
				static_cast<float>(MIN_VERTICAL_VELOCITY)
			};
			const vec3_t velocity_range = vec3_t{
				static_cast<float>(MAX_VELOCITY - MIN_VELOCITY),
				static_cast<float>(MAX_VELOCITY - MIN_VELOCITY),
				static_cast<float>(MAX_VERTICAL_VELOCITY - MIN_VERTICAL_VELOCITY)
			};
			const vec3_t weaponVelocity = base_velocity + velocity_range.scaled(vec3_t{ frandom(), frandom(), frandom() });
			weapon->s.origin = boss->s.origin;
			weapon->velocity = weaponVelocity;
			weapon->movetype = MOVETYPE_BOUNCE;
			weapon->s.effects |= EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER;
			weapon->s.renderfx |= RF_GLOW;
			weapon->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
			weapon->s.alpha = 0.85f;
			weapon->s.scale = 1.25f;
			weapon->flags &= ~FL_RESPAWN;
		}
	}

	// Soltar ítem especial (quad o quadfire)
	const char* specialItemName = brandom() ? "item_quadfire" : "item_quad";
	if (edict_t* specialItem = Drop_Item(boss, FindItemByClassname(specialItemName))) {
		const vec3_t specialVelocity = {
			static_cast<float>(std::uniform_int_distribution<>(MIN_VELOCITY, MAX_VELOCITY)(mt_rand)),
			static_cast<float>(std::uniform_int_distribution<>(MIN_VELOCITY, MAX_VELOCITY)(mt_rand)),
			static_cast<float>(std::uniform_int_distribution<>(300, 400)(mt_rand))
		};

		specialItem->s.origin = boss->s.origin;
		specialItem->velocity = specialVelocity;
		specialItem->movetype = MOVETYPE_BOUNCE;
		specialItem->s.effects |= EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER | EF_HOLOGRAM;
		specialItem->s.alpha = 0.8f;
		specialItem->s.scale = 1.5f;
		specialItem->flags &= ~FL_RESPAWN;
	}

	// Fisher-Yates shuffle optimizado
	std::array<const char*, 8> shuffledItems = itemsToDrop;
	for (int i = 6; i > 0; i--) {
		const int j = mt_rand() % (i + 1);
		if (i != j) {
			std::swap(shuffledItems[i], shuffledItems[j]);
		}
	}

	// Soltar los items shuffleados
	for (const auto& itemClassname : shuffledItems) {
		if (edict_t* droppedItem = Drop_Item(boss, FindItemByClassname(itemClassname))) {
			static std::uniform_int_distribution<> vel_dist(MIN_VELOCITY, MAX_VELOCITY);
			static std::uniform_int_distribution<> vert_dist(MIN_VERTICAL_VELOCITY, MAX_VERTICAL_VELOCITY);
			const vec3_t itemVelocity{
				static_cast<float>(vel_dist(mt_rand)),
				static_cast<float>(vel_dist(mt_rand)),
				static_cast<float>(vert_dist(mt_rand))
			};
			droppedItem->s.origin = boss->s.origin;
			droppedItem->velocity = itemVelocity;
			droppedItem->movetype = MOVETYPE_BOUNCE;
			droppedItem->flags &= ~FL_RESPAWN;
			droppedItem->s.effects |= EF_GIB;
			droppedItem->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
		}
	}

	// Finalizar el manejo del boss
	boss->takedamage = false;

	// Verificar si es un jefe volador (array estático)
	static constexpr std::array<const char*, 4> flyingBossTypes = {
		"monster_boss2", "monster_carrier",
		"monster_carrier_mini", "monster_boss2kl"
	};

	// Búsqueda simple en el array
	for (const auto& bossType : flyingBossTypes) {
		if (strcmp(boss->classname, bossType) == 0) {
			flying_monsters_mode = false;
			break;
		}
	}
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

	//if (frandom() < 0.80f) {
	OldBossDeathHandler(boss);
	return;
}
	//}
//	else
//
//	boss->monsterinfo.BOSS_DEATH_HANDLED = true;
//
//	OnEntityDeath(boss);
//	OnEntityRemoved(boss);
//	auto_spawned_bosses.erase(boss);
//
//
//
//	// Crear punto central invisible para la rotación
//	edict_t* center_point = G_Spawn();
//	center_point->s.origin = boss->s.origin;
//	center_point->s.angles = boss->s.angles;
//	center_point->delay = 0;
//	// Tiempo en que los items comenzarán a dispersarse
//	center_point->timestamp = level.time + 2_sec;
//	center_point->think = G_FreeEdict;
//	// Dar tiempo extra para que los items se dispersen
//	center_point->nextthink = level.time + 3_sec;
//
//	static const std::array<const char*, 7> itemsToDrop = {
//		"item_adrenaline", "item_pack", "item_doppleganger",
//		"item_sphere_defender", "item_armor_combat", "item_bandolier",
//		"item_invulnerability"
//	};
//
//	// Dropear arma especial con rotación
//	if (const char* weapon_classname = SelectBossWeaponDrop(current_wave_level)) {
//		if (edict_t* weapon = Drop_Item(boss, FindItemByClassname(weapon_classname))) {
//			float angle = frandom() * PI * 2;
//			weapon->s.origin = boss->s.origin + vec3_t{
//				cosf(angle) * ITEM_ORBIT_RADIUS,
//				sinf(angle) * ITEM_ORBIT_RADIUS,
//				ITEM_VERTICAL_OFFSET
//			};
//
//			weapon->movetype = MOVETYPE_FLY;
//			weapon->s.effects |= EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER;
//			weapon->s.renderfx |= RF_GLOW;
//			weapon->s.alpha = 0.85f;
//			weapon->s.scale = 1.25f;
//			weapon->flags &= ~FL_RESPAWN;
//			weapon->owner = center_point;
//			weapon->think = Item_Orbit_Think;
//			weapon->nextthink = level.time + FRAME_TIME_S;
//		}
//	}
//
//	// Items normales con rotación
//	std::array<const char*, 7> shuffledItems = itemsToDrop;
//	for (int i = 6; i > 0; i--) {
//		int j = mt_rand() % (i + 1);
//		if (i != j) {
//			std::swap(shuffledItems[i], shuffledItems[j]);
//		}
//	}
//
//	for (size_t i = 0; i < shuffledItems.size(); i++) {
//		if (edict_t* item = Drop_Item(boss, FindItemByClassname(shuffledItems[i]))) {
//			// Distribuir items en círculo más pequeño inicialmente
//			float angle = (static_cast<float>(i) / shuffledItems.size()) * PI * 2;
//			item->s.origin = boss->s.origin + vec3_t{
//				cosf(angle) * (ITEM_ORBIT_RADIUS * 0.5f),
//				sinf(angle) * (ITEM_ORBIT_RADIUS * 0.5f),
//				ITEM_VERTICAL_OFFSET
//			};
//
//			item->movetype = MOVETYPE_FLY;
//			item->s.effects |= EF_GIB;
//			item->flags &= ~FL_RESPAWN;
//			item->owner = center_point;
//			item->think = Item_Orbit_Think;
//			item->nextthink = level.time + FRAME_TIME_S;
//			item->count = static_cast<int>(ItemState::RISING); // Iniciar en estado RISING
//		}
//	}
//
//	boss->takedamage = false;
//
//	static constexpr std::array<const char*, 4> flyingBossTypes = {
//		"monster_boss2", "monster_carrier",
//		"monster_carrier_mini", "monster_boss2kl"
//	};
//
//	for (const auto& bossType : flyingBossTypes) {
//		if (strcmp(boss->classname, bossType) == 0) {
//			flying_monsters_mode = false;
//			break;
//		}
//	}
//}

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
	if (developer->integer) gi.Com_Print("DEBUG: All monsters are dead.\n");
	return true;
}

void CheckAndRestoreMonsterAlpha(edict_t* const ent) {
	if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER)) {
		return;
	}

	if (ent->health > 0 && !ent->deadflag && ent->s.alpha < 1.0f) {
		ent->s.alpha = 0.0f;
		ent->s.renderfx &= ~RF_TRANSLUCENT;
		ent->takedamage = true;
		gi.linkentity(ent);
	}
}


// Constante para el tiempo de vida del fade
constexpr gtime_t FADE_LIFESPAN = 0.5_sec;

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
	const float t = 1.f - ((level.time - self->teleport_time).seconds() / self->wait);
	self->s.alpha = t * t; // Usar t^2 para un fade más suave como spawngrow

	self->nextthink = level.time + FRAME_TIME_MS;
}


static void StartFadeOut(edict_t* ent) {
	// No iniciar fade out si el monstruo está vivo o ya está en fade
	if ((ent->health > 0 && !ent->deadflag) || ent->is_fading_out) {
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

// Asegúrate de limpiar entidades muertas
void Horde_CleanBodies() {
	for (auto ent : active_or_dead_monsters()) {
		// Limpiar inmediatamente si está muerto
		if (ent->deadflag || ent->health <= 0) {
			// Remover inmediatamente sin fade si está muy lejos de los jugadores
			bool far_from_players = true;
			for (const auto* const player : active_players_no_spect()) {
				if ((ent->s.origin - player->s.origin).length() < 1000) {
					far_from_players = false;
					break;
				}
			}

			if (far_from_players) {
				G_FreeEdict(ent);
			}
			else {
				StartFadeOut(ent);
			}
		}
	}
}

// spawning boss origin
std::unordered_map<std::string, std::array<int, 3>> mapOrigins = {
	{"q2dm1", {1184, 568, 704}},
	{"rdm4", {-336, 2456, -288}},
	{"rdm8", {-1516, 976, -156}},
	{"rdm9", {-984, -80, 232}},
	{"rdm12", {32, -1888, 120}},
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
	{"xdm3", {96, -96, 360}},
	{"xdm4", {-160, -368, 360}},
	{"xdm6", {-1088, -128, 528}},
	{"rdm5", {1088, 592, -568}},
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
	{"test/test_kaiser", {1344, 176, -8}},
	{"e3/jail_e3", {-572, -1312, 76}}
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
	{"monster_psxarachnid", "***** Boss incoming! Arachnid is here *****\n"},
	{"monster_gm_arachnid", "***** Boss incoming! Missile Arachnid is armed and ready! *****\n"},
	{"monster_redmutant", "***** Boss incoming! The Bloody Mutant is out for blood—yours! *****\n"},
	{"monster_jorg", "***** Boss incoming! Jorg’s mech is upgraded and deadly! *****\n"}
};

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

void BossSpawnThink(edict_t* self); // Forward declaration of the think function
void SP_target_orb(edict_t* ent);
static void SpawnBossAutomatically() {

	//IF THERE'S A BOSS BEFORE THE NEW SPAWNING: remove him!
	// Primero, eliminar cualquier boss existente
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end(); ) {
		edict_t* existing_boss = *it;
		if (existing_boss && existing_boss->inuse) {
			// Marcar como manejado antes de eliminar
			existing_boss->monsterinfo.BOSS_DEATH_HANDLED = true;
			OnEntityRemoved(existing_boss);
			G_FreeEdict(existing_boss);
			it = auto_spawned_bosses.erase(it);
		}
		else {
			++it;
		}
	}

	// Limpiar la barra de salud existente
	for (size_t i = 0; i < MAX_HEALTH_BARS; ++i) {
		if (level.health_bar_entities[i]) {
			G_FreeEdict(level.health_bar_entities[i]);
			level.health_bar_entities[i] = nullptr;
		}
	}

	// Limpiar el configstring del nombre de la barra de salud
	gi.configstring(CONFIG_HEALTH_BAR_NAME, "");

	// removed previous boss, ready to spawn new

	if (g_horde_local.level < 10 || g_horde_local.level % 5 != 0) {
		return;
	}

	const auto it = mapOrigins.find(GetCurrentMapName());
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

	const char* desired_boss = G_HordePickBOSS(g_horde_local.current_map_size, GetCurrentMapName(), g_horde_local.level, boss);
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
	constexpr float push_radius = 500.0f;
	constexpr float push_force = 1000.0f;
	PushEntitiesAway(spawn_origin, 3, push_radius, push_force, 3750.0f, 1600.0f);

	// Almacenar entidad orbe en el jefe
	boss->owner = orb;

	// Retardar spawn del jefe
	boss->nextthink = level.time + 750_ms;
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
	const auto it_msg = bossMessagesMap.find(self->classname);
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

	self->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	self->monsterinfo.last_sentrygun_target_time = 0_ms;

	// Proceso de spawn seguro
	{
		self->solid = SOLID_NOT;

		ED_CallSpawn(self);
		ClearSpawnArea(self->s.origin, self->mins, self->maxs);

		// Restaurar solidez y actualizar
		self->solid = SOLID_BBOX;
		gi.linkentity(self);
	}


	ConfigureBossArmor(self);

	ApplyBossEffects(self);
	self->monsterinfo.attack_state = AS_BLIND;

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


bool CheckAndTeleportBoss(edict_t* self, BossTeleportReason reason) {
	// Verificaciones iniciales
	if (!self || !self->inuse || self->deadflag || !self->monsterinfo.IS_BOSS ||
		level.intermissiontime || !g_horde->integer)
		return false;

	// Obtener el punto de inicio del mapa
	const auto it = mapOrigins.find(GetCurrentMapName());
	if (it == mapOrigins.end())
		return false;

	// Cooldowns diferentes según la razón
	constexpr gtime_t DROWNING_COOLDOWN = 1_sec;
	constexpr gtime_t TRIGGER_COOLDOWN = 3_sec;
	// constexpr gtime_t STUCK_COOLDOWN = 2_sec;

	// Seleccionar el cooldown apropiado
	const gtime_t selected_cooldown = [reason]() {
		switch (reason) {
		case BossTeleportReason::DROWNING:
			return DROWNING_COOLDOWN;
		case BossTeleportReason::TRIGGER_HURT:
			return TRIGGER_COOLDOWN;
		//case BossTeleportReason::STUCK:
		//	return STUCK_COOLDOWN;
		}
		return TRIGGER_COOLDOWN;
		}();

	// Verificar si ya se teletransportó recientemente
	if (self->teleport_time && level.time < self->teleport_time + selected_cooldown)
		return false;

	// Convertir el punto de inicio a vec3_t
	const vec3_t spawn_origin{
		static_cast<float>(it->second[0]),
		static_cast<float>(it->second[1]),
		static_cast<float>(it->second[2])
	};

	// Verificar si el punto de inicio es válido
	if (!is_valid_vector(spawn_origin) || spawn_origin == vec3_origin)
		return false;

	// Guardar velocidad y origen actuales
	const vec3_t old_velocity = self->velocity;
	const vec3_t old_origin = self->s.origin;

	// Intentar teletransporte
	self->s.origin = spawn_origin;
	self->s.old_origin = spawn_origin;
	self->velocity = vec3_origin;

	// Verificar si la nueva posición es válida
	bool teleport_success = true;
	if (!(self->flags & (FL_FLY | FL_SWIM))) {
		teleport_success = M_droptofloor(self);
	}

	// Verificar colisiones en la nueva posición
	if (teleport_success && !gi.trace(self->s.origin, self->mins, self->maxs,
		self->s.origin, self, MASK_MONSTERSOLID).startsolid) {

		// Efectos visuales y sonoros diferenciados por razón
		switch (reason) {
		case BossTeleportReason::DROWNING:
			gi.sound(self, CHAN_AUTO, sound_tele3, 1, ATTN_NORM, 0);
			gi.LocBroadcast_Print(PRINT_HIGH, "{} emerges from the depths!\n",
				GetDisplayName(self).c_str());
			break;
		case BossTeleportReason::TRIGGER_HURT:
			gi.sound(self, CHAN_AUTO, sound_tele_up, 1, ATTN_NORM, 0);
			gi.LocBroadcast_Print(PRINT_HIGH, "{} escapes certain death!\n",
				GetDisplayName(self).c_str());
			break;
		//case BossTeleportReason::STUCK:
		//	gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
		//	gi.LocBroadcast_Print(PRINT_HIGH, "{} repositions for battle!\n",
		//		GetDisplayName(self).c_str());
		//	break;
		}

		SpawnGrow_Spawn(self->s.origin, 80.0f, 10.0f);


		//TPORT effect
		//gi.WriteByte(svc_temp_entity);
		//gi.WriteByte(TE_BOSSTPORT);
		//gi.WritePosition(self->s.origin);
		//gi.multicast(self->s.origin, MULTICAST_PHS, false);

		// Actualizar tiempo de último teletransporte
		self->teleport_time = level.time;

		// Empujar otras entidades cercanas
		PushEntitiesAway(spawn_origin, 3, 500.0f, 1000.0f, 3750.0f, 1600.0f);

		// Restaurar algo de salud si estaba muy dañado (solo para drowning y trigger_hurt)
		if (/*reason != BossTeleportReason::STUCK && */self->health < self->max_health * 0.25f) {
			self->health = static_cast<int32_t>(self->max_health * 0.25f);
		}

		// Actualizar la entidad
		gi.linkentity(self);

		if (developer->integer) {
			const char* reason_str = reason == BossTeleportReason::DROWNING ? "drowning" :
		/*		reason == BossTeleportReason::TRIGGER_HURT ?*/ "trigger_hurt" /*: "stuck"*/;
			gi.Com_PrintFmt("Boss teleported due to {} to: {}\n", reason_str, self->s.origin);
		}

		return true;
	}

	// Si falló el teletransporte, restaurar posición original
	self->s.origin = old_origin;
	self->s.old_origin = old_origin;
	self->velocity = old_velocity;
	gi.linkentity(self);

	return false;
}

void SetHealthBarName(const edict_t* boss) {
	static std::array<char, MAX_STRING_CHARS> buffer = {};

	if (!boss || !boss->inuse) {
		gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, "");
		return;
	}

	const std::string display_name = GetDisplayName(boss);
	if (display_name.empty()) {
		gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, "");
		return;
	}

	// Usar una constante para el null terminator
	constexpr size_t null_terminator_space = 1;
	const size_t name_len = std::min(display_name.length(),
		MAX_STRING_CHARS - null_terminator_space);

	// Copiar el string directamente al array
	std::copy_n(display_name.begin(), name_len, buffer.begin());
	buffer[name_len] = '\0';

	gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, buffer.data());
}

//CS HORDE

void UpdateHordeHUD() {
	bool update_successful = false;

	const std::string_view current_msg = gi.get_configstring(CONFIG_HORDEMSG);
	if (!current_msg.empty()) {
		// Usar active_players() en lugar de iterar manualmente
		for (auto* player : active_players()) {
			const std::span voted_map(player->client->voted_map);
			if (voted_map.empty() || voted_map[0] == '\0') {
				player->client->ps.stats[STAT_HORDEMSG] = CONFIG_HORDEMSG;
				update_successful = true;
			}
		}
	}

	if (update_successful) {
		g_horde_local.last_successful_hud_update = level.time;
		g_horde_local.failed_updates_count = 0;
	}
	else {
		g_horde_local.failed_updates_count++;
		if (g_horde_local.failed_updates_count > 5) {
			ClearHordeMessage();
			g_horde_local.reset_hud_state();
		}
	}
}

// Implementación de UpdateHordeMessage
void UpdateHordeMessage(std::string_view message, gtime_t duration = 5_sec) {
	// Ensure message isn't empty and duration is valid
	if (message.empty() || duration <= 0_ms) {
		ClearHordeMessage();
		return;
	}

	// Only update if message actually changed
	const char* current_msg = gi.get_configstring(CONFIG_HORDEMSG);
	if (!current_msg || strcmp(current_msg, message.data()) != 0) {
		gi.configstring(CONFIG_HORDEMSG, message.data());
	}

	// Update duration
	horde_message_end_time = level.time + duration;
}

void ClearHordeMessage() {
	std::string_view current_msg = gi.get_configstring(CONFIG_HORDEMSG);
	if (!current_msg.empty()) {
		gi.configstring(CONFIG_HORDEMSG, "");
		// Usar active_players() para resetear stats
		for (auto* player : active_players()) {
			player->client->ps.stats[STAT_HORDEMSG] = 0;
		}
	}
	horde_message_end_time = 0_sec;
}

// reset cooldowns, fixed no monster spawning on next map
// En UnifiedAdjustSpawnRate y ResetCooldowns:
void ResetCooldowns() noexcept {
	spawnPointsData.clear();
	lastSpawnPointTime.clear();
	lastMonsterSpawnTime.clear();

	const MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level;
	const int32_t humanPlayers = GetNumHumanPlayers();

	// Obtener cooldown base según el tamaño del mapa
	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);

	// Aplicar escala basada en nivel
	const float cooldownScale = CalculateCooldownScale(currentLevel, mapSize);
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	// Ajustes adicionales (reducidos pero mantenidos para balance)
	if (humanPlayers > 1) {
	const	float playerAdjustment = 1.0f - (std::min(humanPlayers - 1, 3) * 0.05f);
		SPAWN_POINT_COOLDOWN *= playerAdjustment;
	}

	// Ajustes por modo de dificultad (reducidos) - Con verificación de seguridad
	if ((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) {
		SPAWN_POINT_COOLDOWN *= 0.95f;
	}

	// Aplicar límites absolutos
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN, 1.0_sec, 3.0_sec);

	if (developer->integer) gi.Com_PrintFmt("DEBUG: Reset spawn cooldown to {:.2f} seconds (Level {})\n",
		SPAWN_POINT_COOLDOWN.seconds(), currentLevel);
}

void ResetAllSpawnAttempts() noexcept {
	for (auto& [spawn_point, data] : spawnPointsData) {
		data.attempts = 0;
		data.spawn_cooldown = SPAWN_POINT_COOLDOWN;
		data.teleport_cooldown = level.time;
		data.isTemporarilyDisabled = false;
		data.cooldownEndsAt = 0_sec;
	}
}

// Función modificada para resetear la lista de jefes recientes
static void ResetRecentBosses() noexcept {
	recent_bosses.clear();
}

void ResetWaveAdvanceState() noexcept;

static bool CheckRemainingMonstersCondition(const MapSize& mapSize, WaveEndReason& reason) {
	const gtime_t currentTime = level.time;
	const int32_t remainingMonsters = CalculateRemainingMonsters();
	const float percentageRemaining = static_cast<float>(remainingMonsters) / g_totalMonstersInWave;

	// Check independent time limit first
	if (currentTime >= g_independent_timer_start + g_lastParams.independentTimeThreshold) {
		reason = WaveEndReason::TimeLimitReached;
		return true;
	}

	if (allowWaveAdvance || Horde_AllMonstersDead()) {
		reason = WaveEndReason::AllMonstersDead;
		ResetWaveAdvanceState();
		return true;
	}

	// Initialize wave end time if not set
	if (g_horde_local.waveEndTime == 0_sec) {
		g_horde_local.waveEndTime = g_independent_timer_start + g_lastParams.independentTimeThreshold;
	}

	// Check for condition triggers
	if (!g_horde_local.conditionTriggered) {
		const bool maxMonstersReached = remainingMonsters <= g_lastParams.maxMonsters;
		const bool lowPercentageReached = percentageRemaining <= g_lastParams.lowPercentageThreshold;

		if (maxMonstersReached || lowPercentageReached) {
			g_horde_local.conditionTriggered = true;
			g_horde_local.conditionStartTime = currentTime;

			// Select shortest time threshold based on conditions
			g_horde_local.conditionTimeThreshold = (maxMonstersReached && lowPercentageReached) ?
				std::min(g_lastParams.timeThreshold, g_lastParams.lowPercentageTimeThreshold) :
				maxMonstersReached ? g_lastParams.timeThreshold : g_lastParams.lowPercentageTimeThreshold;

			g_horde_local.waveEndTime = currentTime + g_horde_local.conditionTimeThreshold;

			// Aggressive time reduction for very few monsters
			if (remainingMonsters <= MONSTERS_FOR_AGGRESSIVE_REDUCTION) {
				const gtime_t reduction = AGGRESSIVE_TIME_REDUCTION_PER_MONSTER *
					(MONSTERS_FOR_AGGRESSIVE_REDUCTION - remainingMonsters);
				g_horde_local.waveEndTime = std::min(g_horde_local.waveEndTime, currentTime + reduction);
				gi.LocBroadcast_Print(PRINT_HIGH, "Wave time reduced!\n");
			}
		}
	}

	// Handle warnings and time checks
	if (g_horde_local.conditionTriggered) {
		const gtime_t remainingTime = g_horde_local.waveEndTime - currentTime;

		// Issue warnings at predefined intervals
		for (size_t i = 0; i < WARNING_TIMES.size(); ++i) {
			const gtime_t warningTime = gtime_t::from_sec(WARNING_TIMES[i]);
			if (!g_horde_local.warningIssued[i] &&
				remainingTime <= warningTime &&
				remainingTime > (warningTime - 1_sec)) {
				gi.LocBroadcast_Print(PRINT_HIGH, "{} seconds remaining!\n",
					static_cast<int>(WARNING_TIMES[i]));
				g_horde_local.warningIssued[i] = true;
			}
		}

		// Check for wave completion
		if (currentTime >= g_horde_local.waveEndTime) {
			reason = WaveEndReason::MonstersRemaining;
			return true;
		}
	}

	// Update debug info periodically
	if (currentTime - g_horde_local.lastPrintTime >= 10_sec) {
		const gtime_t remainingTime = g_horde_local.waveEndTime - currentTime;
		gi.Com_PrintFmt("Wave status: {} monsters remaining. {:.2f} seconds left.\n",
			remainingMonsters, remainingTime.seconds());
		g_horde_local.lastPrintTime = currentTime;
	}

	return false;
}

//
// game resetting
//

void ResetGame() {

	// Si ya se ha ejecutado una vez, retornar inmediatamente
	if (hasBeenReset) {
		gi.Com_PrintFmt("PRINT: Reset already performed, skipping...\n");
		return;
	}

	// Establecer el flag al inicio de la ejecución
	hasBeenReset = true;

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
	horde_message_end_time = 0_sec;
	g_totalMonstersInWave = 0;

	// Resetear flags de control
	g_maxMonstersReached = false;
	g_lowPercentageTriggered = false;

	// Limpiar cachés
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
	SPAWN_POINT_COOLDOWN = 2.8_sec;

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
	gi.cvar_set("timelimit", "60");
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

	// Reset sound tracking
	std::fill(used_wave_sounds.begin(), used_wave_sounds.end(), false);
	remaining_wave_sounds = NUM_WAVE_SOUNDS;
	std::fill(used_start_sounds.begin(), used_start_sounds.end(), false);
	remaining_start_sounds = NUM_START_SOUNDS;

	// Registrar el reinicio
	gi.Com_PrintFmt("PRINT: Horde game state reset complete.\n");
}

inline int8_t CalculateRemainingMonsters() {
    // Usar variables del nivel en lugar de recorrer entidades
    const int32_t remaining = level.total_monsters - level.killed_monsters;
    return std::max(0, remaining);  // Asegurar que no sea negativo
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

	g_totalMonstersInWave = 0;

	boss_spawned_for_wave = false;
	flying_monsters_mode = false;

	g_lastWaveNumber = -1;
	g_lastNumHumanPlayers = -1;

	g_horde_local.waveEndTime = 0_sec;
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

	g_horde_local.conditionTriggered = true;
	g_horde_local.waveEndTime = level.time;

	// Limpiar cualquier mensaje pendiente
	ClearHordeMessage();

	g_horde_local.state = horde_state_t::spawning;
	Horde_InitLevel(g_horde_local.level + 1);
}
inline int8_t GetNumActivePlayers() {
    const auto& players = active_players();
    return std::count_if(players.begin(), players.end(),
        [](const edict_t* const player) {
            return player->client && player->client->resp.ctf_team == CTF_TEAM1;
        });
}

inline int8_t GetNumHumanPlayers() {
	const auto& players = active_players_no_spect();
	return std::count_if(players.begin(), players.end(),
		[](const edict_t* const player) {
			return !(player->svflags & SVF_BOT);
		});
}

inline int8_t GetNumSpectPlayers() {
	const auto& players = active_players();
	return std::count_if(players.begin(), players.end(),
		[](const edict_t* const player) {
			return ClientIsSpectating(player->client);
		});
}

// Implementación de DisplayWaveMessage
static void DisplayWaveMessage(gtime_t duration = 5_sec) {
	static const std::array<const char*, 3> messages = {
		"Horde Menu available upon opening Inventory or using TURTLE on POWERUP WHEEL\n\nMAKE THEM PAY!\n",
		"Welcome to Hell.\n\nUse FlipOff <Key> looking at walls to spawn lasers (cost: 25 cells)\n",
		"New Tactics!\n\nTeslas can now be placed on walls and ceilings!\n\nUse them wisely!"
	};

	// Usar distribución uniforme con mt_rand
	std::uniform_int_distribution<size_t> dist(0, messages.size() - 1);
	const size_t choice = dist(mt_rand);
	UpdateHordeMessage(messages[choice], duration);
}

static void HandleWaveCleanupMessage(const MapSize& mapSize, WaveEndReason reason) noexcept {
	// Obtener el número de jugadores humanos
	const int8_t numHumanPlayers = GetNumHumanPlayers();

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
			// Activar chaotic2 si es mapa pequeño Y hay 2+ jugadores, sino chaotic1
			gi.cvar_set("g_chaotic", (mapSize.isSmallMap && numHumanPlayers >= 2) ? "2" : "1");
		}
	}
	else {
		// Si la ola no terminó por victoria completa, pequeña probabilidad de mantener la dificultad
		const float probability = mapSize.isBigMap ? 0.3f :
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
				// Activar chaotic2 si es mapa pequeño Y hay 2+ jugadores, sino chaotic1
				gi.cvar_set("g_chaotic", (mapSize.isSmallMap && numHumanPlayers >= 2) ? "2" : "1");
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

static void HandleWaveRestMessage(gtime_t duration = 3_sec) {
	const char* message;

	if (g_chaotic->integer > 0 && g_horde_local.level >= 5) {  // Solo después de la ola 5
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

	gi.sound(world, CHAN_VOICE, GetRandomWaveSound(), 1, ATTN_NONE, 0);
}

// Llamar a esta función durante la inicialización del juego
void InitializeWaveSystem() noexcept {
	PrecacheWaveSounds();
}

static void SetMonsterArmor(edict_t* monster);
static void SetNextMonsterSpawnTime(const MapSize& mapSize);

bool CheckAndTeleportStuckMonster(edict_t* self) {
	// Early returns optimizados
	if (!self || !self->inuse || self->deadflag ||
		self->monsterinfo.IS_BOSS || level.intermissiontime || !g_horde->integer)
		return false;
	if (!strcmp(self->classname, "misc_insane"))
		return false;

	constexpr gtime_t NO_DAMAGE_TIMEOUT = 25_sec;
	constexpr gtime_t STUCK_CHECK_TIME = 5_sec;
	constexpr gtime_t TELEPORT_COOLDOWN = 2_sec;

	// Si puede ver al enemigo, no teleportar
	if (self->monsterinfo.issummoned ||
		(self->enemy && self->enemy->inuse && visible(self, self->enemy, false))) {
		self->monsterinfo.was_stuck = false;
		self->monsterinfo.stuck_check_time = 0_sec;
		return false;
	}

	// Verificar cooldown general de teleport
	if (self->teleport_time && level.time < self->teleport_time + TELEPORT_COOLDOWN)
		return false;

	// Para daño no-agua, verificar condiciones de stuck
	if (!self->waterlevel) {
		const bool is_stuck = gi.trace(self->s.origin, self->mins, self->maxs,
			self->s.origin, self, MASK_MONSTERSOLID).startsolid;
		const bool no_damage_timeout = (level.time - self->monsterinfo.react_to_damage_time) >= NO_DAMAGE_TIMEOUT;

		if (!is_stuck && !no_damage_timeout && !self->monsterinfo.was_stuck)
			return false;

		if (!self->monsterinfo.was_stuck) {
			self->monsterinfo.stuck_check_time = level.time;
			self->monsterinfo.was_stuck = true;
			return false;
		}

		if (level.time < self->monsterinfo.stuck_check_time + STUCK_CHECK_TIME)
			return false;
	}

	std::array<edict_t*, MAX_SPAWN_POINTS> available_spawns = {};
	size_t spawn_count = 0;

	// Crear span de forma más segura
	std::span<edict_t> all_edicts(g_edicts, globals.num_edicts);
	// Crear subspan excluyendo el primer elemento
	std::span<edict_t> edicts_view = all_edicts.subspan(1);

	// Recolección optimizada de spawn points usando span
	for (edict_t& e : edicts_view) {
		if (spawn_count >= MAX_SPAWN_POINTS)
			break;

		if (!e.inuse || !e.classname ||
			strcmp(e.classname, "info_player_deathmatch") != 0 ||
			e.style == 1)  // Excluir spawns para voladores
			continue;

		auto const it = spawnPointsData.find(&e);
		if (it != spawnPointsData.end()) {
			if (level.time < it->second.teleport_cooldown)
				continue;
		}

		if (!IsSpawnPointOccupied(&e)) {
			bool can_see_player = false;
			for (const auto* const player : active_players_no_spect()) {
				if (!player->inuse || player->deadflag)
					continue;
				if (G_IsClearPath(player, MASK_SOLID, e.s.origin, player->s.origin)) {
					can_see_player = true;
					break;
				}
			}
			if (can_see_player)
				available_spawns[spawn_count++] = &e;
		}
	}

	if (spawn_count == 0)
		return false;
	// Crear vista de los spawns disponibles usando std::array
	std::span<edict_t* const> spawns_view(available_spawns.data(), spawn_count);

	// Seleccionar spawn point aleatorio usando span
	edict_t* spawn_point = spawns_view[irandom(spawn_count)];

	// Set teleport cooldown
	auto& spawn_data = spawnPointsData[spawn_point];
	spawn_data.teleport_cooldown = level.time + 2_sec;

	const vec3_t old_velocity = self->velocity;
	const vec3_t old_origin = self->s.origin;
	self->s.origin = spawn_point->s.origin;
	self->s.old_origin = spawn_point->s.origin;
	self->velocity = vec3_origin;

	bool teleport_success = true;
	if (!(self->flags & (FL_FLY | FL_SWIM)))
		teleport_success = M_droptofloor(self);

	if (teleport_success && !gi.trace(self->s.origin, self->mins, self->maxs,
		self->s.origin, self, MASK_MONSTERSOLID).startsolid) {
		gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
		SpawnGrow_Spawn(self->s.origin, 80.0f, 10.0f);
		self->monsterinfo.was_stuck = false;
		self->monsterinfo.stuck_check_time = 0_sec;
		self->monsterinfo.react_to_damage_time = level.time;
		gi.linkentity(self);
		return true;
	}

	// Restaurar posición si falló
	self->s.origin = old_origin;
	self->s.old_origin = old_origin;
	self->velocity = old_velocity;
	gi.linkentity(self);
	return false;
}

static edict_t* SpawnMonsters() {
	edict_t* monster_spawns[MAX_SPAWN_POINTS] = {};
	size_t spawn_count = 0;
	const auto currentTime = level.time;

	// Crear span desde g_edicts de forma más segura
	std::span<edict_t> all_edicts(g_edicts, globals.num_edicts);
	// Crear subspan excluyendo el primer elemento (índice 0)
	std::span<edict_t> active_edicts = all_edicts.subspan(1);

	// Recolección de spawn points con validación temprana
	for (edict_t& e : active_edicts) {
		if (spawn_count >= MAX_SPAWN_POINTS)
			break;

		if (!e.inuse || !e.classname || e.monsterinfo.IS_BOSS ||
			strcmp(e.classname, "info_player_deathmatch") != 0)
			continue;

		auto const it = spawnPointsData.find(&e);
		if (it != spawnPointsData.end() && it->second.isTemporarilyDisabled) {
			if (currentTime < it->second.cooldownEndsAt)
				continue;
			it->second.isTemporarilyDisabled = false;
		}

		// Verificar si el punto de spawn es válido usando vec3_t
		if (!is_valid_vector(e.s.origin))
			continue;

		monster_spawns[spawn_count++] = &e;
	}

	if (spawn_count == 0)
		return nullptr;

	// Shuffle optimizado usando Fisher-Yates con span
	if (spawn_count > 1) {
		for (size_t i = spawn_count - 1; i > 0; --i) {
			const size_t j = irandom(i + 1);
			if (i != j) {
				std::swap(monster_spawns[i], monster_spawns[j]);
			}
		}
	}

	// Cache de valores del mapa y límites
	const MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t maxMonsters = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
		(mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP);

	// Cálculos de spawn optimizados
	const int32_t activeMonsters = CalculateRemainingMonsters();
	const int32_t base_spawn = mapSize.isSmallMap ? 4 : (mapSize.isBigMap ? 6 : 5);
	const int32_t min_spawn = std::min(g_horde_local.queued_monsters, base_spawn);
	const int32_t monsters_per_spawn = irandom(min_spawn, std::min(base_spawn + 1, 6));
	const int32_t spawnable = std::clamp(monsters_per_spawn, 0, maxMonsters - activeMonsters);

	if (spawnable <= 0)
		return nullptr;

	edict_t* last_spawned = nullptr;
	const float drop_chance = g_horde_local.level <= 2 ? 0.8f :
		g_horde_local.level <= 7 ? 0.6f : 0.45f;

	const size_t spawn_limit = std::min(spawn_count, static_cast<size_t>(spawnable));

	// Spawn loop optimizado usando operaciones vec3_t
	for (size_t i = 0; i < spawn_limit; ++i) {
		if (g_horde_local.num_to_spawn <= 0)
			break;

		edict_t* spawn_point = monster_spawns[i];
		const char* monster_classname = G_HordePickMonster(spawn_point);
		if (!monster_classname)
			continue;

		if (edict_t* monster = G_Spawn()) {
			monster->classname = monster_classname;

			// Usar ProjectSource para posición más precisa con offset vertical
			monster->s.origin = G_ProjectSource(spawn_point->s.origin,
				vec3_t{ 0, 0, 8 },  // Pequeño offset vertical
				vec3_t{ 1,0,0 },
				vec3_t{ 0,1,0 });

			monster->s.angles = spawn_point->s.angles;
			monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
			monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
			monster->monsterinfo.last_sentrygun_target_time = 0_ms;

			ED_CallSpawn(monster);
			if (monster->inuse) {
				if (g_horde_local.level >= 14 && !monster->monsterinfo.power_armor_type != IT_NULL)
					SetMonsterArmor(monster);

				if (frandom() < drop_chance)
					monster->item = G_HordePickItem();

				// Usar vec3_t para los efectos visuales
				const vec3_t spawn_pos = monster->s.origin + vec3_t{ 0, 0, monster->mins[2] };
				SpawnGrow_Spawn(spawn_pos, 80.0f, 10.0f);
				gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

				--g_horde_local.num_to_spawn;
				--g_horde_local.queued_monsters;
				++g_totalMonstersInWave;
				last_spawned = monster;
			}
			else {
				G_FreeEdict(monster);
			}
		}
	}

	// Gestión optimizada de monstruos en cola
	if (g_horde_local.queued_monsters > 0 && g_horde_local.num_to_spawn > 0) {
		const int32_t additional_spawnable = maxMonsters - CalculateRemainingMonsters();
		const int32_t additional_to_spawn = std::min(g_horde_local.queued_monsters, additional_spawnable);
		g_horde_local.num_to_spawn += additional_to_spawn;
		g_horde_local.queued_monsters = std::max(0, g_horde_local.queued_monsters - additional_to_spawn);
	}

	SetNextMonsterSpawnTime(mapSize);
	return last_spawned;
}

static void SetMonsterArmor(edict_t* monster) {
	const spawn_temp_t& st = ED_GetSpawnTemp();

	// Asignar tipo de armadura por defecto solo si no tiene ninguno especificado
	if (!st.was_key_specified("power_armor_power")) {
		monster->monsterinfo.armor_type = IT_ARMOR_COMBAT;
	}

	// Calcular el poder de armadura para todos los casos
	const float health_ratio = monster->health / static_cast<float>(monster->max_health);
	const float size_factor = (monster->maxs - monster->mins).length() / 64.0f;
	const float mass_factor = std::min(monster->mass / 200.0f, 1.5f);

	float level_scaling;
	if (current_wave_level <= 15) {
		level_scaling = 1.0f + (current_wave_level * 0.04f);
	}
	else if (current_wave_level <= 25) {
		level_scaling = 1.6f + ((current_wave_level - 15) * 0.06f);
	}
	else {
		level_scaling = 2.2f + ((current_wave_level - 25) * 0.08f);
	}

	float armor_power = (75 + monster->max_health * 0.15f) *
		std::pow(health_ratio, 1.1f) *
		std::pow(size_factor, 0.7f) *
		std::pow(mass_factor, 0.6f) *
		level_scaling;

	float armor_multiplier = 1.0f;
	if (current_wave_level <= 30) {
		armor_multiplier = 0.4f;
	}
	else if (current_wave_level <= 40) {
		armor_multiplier = 0.5f;
	}
	armor_power *= armor_multiplier;

	if (g_insane->integer) {
		armor_power *= 1.2f;
	}
	else if (g_chaotic->integer) {
		armor_power *= 1.1f;
	}

	const float random_factor = std::uniform_real_distribution<float>(0.9f, 1.1f)(mt_rand);
	armor_power *= random_factor;

	if (current_wave_level > 25) {
		armor_power *= 1.0f + ((current_wave_level - 25) * 0.03f);
	}

	const int min_armor = std::max(25, static_cast<int>(monster->max_health * 0.08f));
	const int max_armor = static_cast<int>(monster->max_health *
		(current_wave_level > 25 ? 1.5f : 1.2f));

	const int final_armor = std::clamp(static_cast<int>(armor_power), min_armor, max_armor);

	// Asignar el poder según el tipo de armadura
	if (monster->monsterinfo.power_armor_type == IT_NULL) {
		monster->monsterinfo.power_armor_power = 0;
	}
	else if (monster->monsterinfo.power_armor_type == IT_ITEM_POWER_SHIELD ||
		monster->monsterinfo.power_armor_type == IT_ITEM_POWER_SCREEN) {
		monster->monsterinfo.power_armor_power = final_armor;
	}

	if (monster->monsterinfo.armor_type == IT_NULL) {
		monster->monsterinfo.armor_power = 0;
	}
	else if (monster->monsterinfo.armor_type == IT_ARMOR_COMBAT) {
		monster->monsterinfo.armor_power = final_armor;
	}
}
static void SetNextMonsterSpawnTime(const MapSize& mapSize) {
	constexpr std::array<std::pair<gtime_t, gtime_t>, 3> SPAWN_TIMES = { {
		{0.6_sec, 0.8_sec},  // Small maps
		{0.8_sec, 1.0_sec},  // Medium maps
		{0.4_sec, 0.6_sec}   // Big maps
	} };

	const size_t mapIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);
	const auto& [min_time, max_time] = SPAWN_TIMES[mapIndex];

	g_horde_local.monster_spawn_time = level.time + random_time(min_time, max_time);
}

// Usar enum class para mejorar la seguridad de tipos
enum class MessageType {
	Standard,
	Chaotic,
	Insane
};

static void CalculateTopDamager(PlayerStats& topDamager, float& percentage) {
	constexpr int32_t MAX_DAMAGE = 100000; // Añadir límite máximo de daño
	int32_t total_damage = 0;
	topDamager = PlayerStats(); // Reset usando el constructor por defecto

	for (const auto& player : active_players()) {
		if (!player->client)
			continue;

		const int32_t player_damage = std::min(player->client->total_damage, MAX_DAMAGE);

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
	{RewardType::SENTRY_GUN, {IT_ITEM_SENTRYGUN, 20}} // Least common
};

// Function to handle reward selection and distribution
static bool GiveTopDamagerReward(const PlayerStats& topDamager, const std::string& playerName) {
	if (!topDamager.player)
		return false;

	// Calculate total weight for weighted random selection
	int totalWeight = 0;
	for (const auto& reward : REWARD_TABLE)
		totalWeight += reward.second.weight;

	// Generate random number within total weight range
	std::uniform_int_distribution<int> dist(1, totalWeight);
	const int randomNum = dist(mt_rand);

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

//debugging

static void PrintRemainingMonsterCounts() {
	std::unordered_map<std::string, int> monster_counts;

	for (const auto* const ent : active_monsters()) {
		// Ignorar monstruos con AI_DO_NOT_COUNT
		if (ent->monsterinfo.aiflags & AI_DO_NOT_COUNT)
			continue;

		// Solo contar monstruos activos y vivos
		if (ent->inuse && !ent->deadflag && ent->health > 0) {
			monster_counts[ent->classname]++;
		}
	}

	// Solo mostrar advertencia si hay una discrepancia real
	const bool has_discrepancy = (level.total_monsters != level.killed_monsters);
	const bool has_remaining = !monster_counts.empty();

	if (has_discrepancy || has_remaining) {
		gi.Com_PrintFmt("WARNING: Monster count discrepancy detected:\n");
		gi.Com_PrintFmt("Total monsters according to level: {}\n", level.total_monsters);
		gi.Com_PrintFmt("Killed monsters: {}\n", level.killed_monsters);

		if (has_remaining) {
			gi.Com_PrintFmt("Remaining monster types (excluding AI_DO_NOT_COUNT):\n");
			for (const auto& [classname, count] : monster_counts) {
				gi.Com_PrintFmt("- {} : {}\n", classname, count);
			}
		}
	}
}

static void SendCleanupMessage(WaveEndReason reason) {
	gtime_t duration = 2_sec;
	if (allowWaveAdvance && reason == WaveEndReason::AllMonstersDead) {
		duration = 0_sec;
	}

	PlayerStats topDamager;
	float percentage = 0.0f;
	CalculateTopDamager(topDamager, percentage);

	// Simplificar el mensaje usando condicionales directos
	std::string message;
	switch (reason) {
	case WaveEndReason::AllMonstersDead:
		message = fmt::format("Wave {} Completely Cleared - Perfect Victory!\n", g_horde_local.level);
		PrintRemainingMonsterCounts();
		break;
	case WaveEndReason::MonstersRemaining:
		message = fmt::format("Wave {} Pushed Back - But Still Threatening!\n", g_horde_local.level);
		break;
	case WaveEndReason::TimeLimitReached:
		message = fmt::format("Wave {} Contained - Time Limit Reached!\n", g_horde_local.level);
		break;
	}

	UpdateHordeMessage(message, duration);

	if (topDamager.player) {
		const std::string playerName = GetPlayerName(topDamager.player);
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
void CheckAndResetDisabledSpawnPoints() {
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
	const MapSize& mapSize = g_horde_local.current_map_size;	const int32_t currentLevel = g_horde_local.level;
	static WaveEndReason currentWaveEndReason;  // Agregar esta declaración

	// Manejo de monstruos personalizados
	if (dm_monsters->integer >= 1) {
		g_horde_local.num_to_spawn = dm_monsters->integer;
		g_horde_local.queued_monsters = 0;
		ClampNumToSpawn(mapSize);
	}

	switch (g_horde_local.state) {
	case horde_state_t::warmup:
		if (g_horde_local.warm_time < level.time) {
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
			const WaveEndReason reason = WaveEndReason::TimeLimitReached;
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

			const int32_t activeMonsters = CalculateRemainingMonsters();
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

				if (developer->integer) gi.Com_PrintFmt("PRINT: Transitioning to 'active_wave' state. Wave timer starting.\n");
			}
		}
		break;

	case horde_state_t::active_wave: {
		WaveEndReason reason;
		const bool shouldAdvance = CheckRemainingMonstersCondition(mapSize, reason);

		if (shouldAdvance) {
			SendCleanupMessage(reason);
			gi.Com_PrintFmt("PRINT: Wave {} completed. Transitioning to cleanup.\n", currentLevel);
			g_horde_local.state = horde_state_t::cleanup;
			g_horde_local.monster_spawn_time = level.time + 0.5_sec;
			currentWaveEndReason = reason; // Guardar la razón aquí
		}
		else if (g_horde_local.monster_spawn_time <= level.time) {
			const int32_t activeMonsters = CalculateRemainingMonsters();
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
		}
		break;
	}
	case horde_state_t::rest:
		if (g_horde_local.warm_time < level.time) {
			HandleWaveRestMessage(3_sec);
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(g_horde_local.level + 1);
		}
		break;
	}
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
	const gtime_t independentRemaining = g_independent_timer_start + g_lastParams.independentTimeThreshold - currentTime;

	// Siempre retornar el menor tiempo entre ambos si son válidos
	if (independentRemaining > 0_sec) {
		remainingTime = (remainingTime > 0_sec) ?
			std::min(remainingTime, independentRemaining) :
			independentRemaining;
	}

	return remainingTime;
}