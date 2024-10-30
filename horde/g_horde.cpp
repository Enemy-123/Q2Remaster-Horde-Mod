// Includes y definiciones relevantes
#include "../g_local.h"
#include "g_horde.h"
#include "../shared.h"
#include <set>

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
gtime_t SPAWN_POINT_COOLDOWN = 3.9_sec; // Cooldown en segundos para los puntos de spawn 3.0

cvar_t* g_horde;

enum class horde_state_t {
	warmup,
	spawning,
	active_wave,
	cleanup,
	rest
};

struct HordeState {
	gtime_t         warm_time = 4_sec;
	horde_state_t   state = horde_state_t::warmup;
	gtime_t         monster_spawn_time;
	int32_t         num_to_spawn = 0;
	int32_t         level = 0;
	int32_t         queued_monsters = 0;
	gtime_t         lastPrintTime = 0_sec; // Nueva variable

	// Variables para condiciones
	bool            conditionTriggered = false;
	gtime_t         conditionStartTime = 0_sec;
	gtime_t         conditionTimeThreshold = 0_sec;
	bool            timeWarningIssued = false;
	gtime_t         waveEndTime = 0_sec;
	std::vector<bool> warningIssued = { false, false, false, false }; // Assuming 4 warning times
} g_horde_local;

struct weighted_benefit_t {
	const char* benefit_name;
	int32_t min_level;
	int32_t max_level;
	float weight;
};

// Clase de selección genérica usando templates
template <typename T>
struct WeightedSelection {
	std::vector<const T*> items;
	std::vector<double> cumulative_weights;
	double total_weight;

	WeightedSelection() : total_weight(0.0) {}

	void rebuild(const std::vector<const T*>& eligible_items) {
		items = eligible_items;
		cumulative_weights.clear();
		total_weight = 0.0;

		for (const auto& item : items) {
			total_weight += item->weight;
			cumulative_weights.push_back(total_weight);
		}
	}

	const T* select() const {
		if (items.empty()) return nullptr;

		std::uniform_real_distribution<double> dist(0.0, total_weight);
		double random_weight = dist(mt_rand);

		auto it = std::upper_bound(cumulative_weights.begin(), cumulative_weights.end(), random_weight);
		return items[it == cumulative_weights.end() ? items.size() - 1 :
			std::distance(cumulative_weights.begin(), it)];
	}
};

int32_t current_wave_level = g_horde_local.level;
bool next_wave_message_sent = false;
int32_t vampire_level = 0;

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
	// Buscar en el cache
	for (size_t i = 0; i < mapSizeCacheSize; ++i) {
		if (mapSizeCache[i].first == mapname) {
			return mapSizeCache[i].second;
		}
	}

	// Si no está en cache, crear nuevo MapSize
	MapSize mapSize;
	mapSize.isSmallMap = smallMaps.count(mapname) > 0;
	mapSize.isBigMap = bigMaps.count(mapname) > 0;
	mapSize.isMediumMap = !mapSize.isSmallMap && !mapSize.isBigMap;

	// Añadir al cache si hay espacio
	if (mapSizeCacheSize < MAX_MAPS) {
		mapSizeCache[mapSizeCacheSize] = std::make_pair(mapname, mapSize);
		++mapSizeCacheSize;
	}

	return mapSize;
}
// Lista de beneficios ponderados (constexpr para ser evaluado en tiempo de compilación)
const std::array<weighted_benefit_t, 9> benefits = { {
	{ "vampire", 4, -1, 0.2f },
	{ "vampire upgraded", 24, -1, 0.1f },
	{ "ammo regen", 8, -1, 0.15f },
	{ "auto haste", 9, -1, 0.15f },
	{ "start armor", 9, -1, 0.1f },
	{ "Traced-Piercing Bullets", 9, -1, 0.2f },
	{ "Cluster Prox Grenades", 25, -1, 0.2f },
	{ "Napalm-Grenade Launcher", 25, -1, 0.2f },
	{ "BFG Grav-Pull Lasers", 35, -1, 0.2f }
} };

// Lista para los beneficios mezclados
std::vector<const weighted_benefit_t*> shuffled_benefits;

// Set para almacenar los beneficios obtenidos
std::unordered_set<std::string> obtained_benefits;

// Actualizar ShuffleBenefits para usar el generador existente
void ShuffleBenefits(std::mt19937& rng) {
	shuffled_benefits.clear();
	shuffled_benefits.reserve(benefits.size());

	for (const auto& benefit : benefits) {
		if (obtained_benefits.find(benefit.benefit_name) == obtained_benefits.end()) {
			shuffled_benefits.push_back(&benefit);
		}
	}
	std::shuffle(shuffled_benefits.begin(), shuffled_benefits.end(), rng);

	// Asegurar que 'vampire' esté antes de 'vampire upgraded'
	auto vampire_it = std::find_if(shuffled_benefits.begin(), shuffled_benefits.end(),
		[](const weighted_benefit_t* b) { return std::strcmp(b->benefit_name, "vampire") == 0; });
	auto upgraded_it = std::find_if(shuffled_benefits.begin(), shuffled_benefits.end(),
		[](const weighted_benefit_t* b) { return std::strcmp(b->benefit_name, "vampire upgraded") == 0; });

	if (vampire_it != shuffled_benefits.end() && upgraded_it != shuffled_benefits.end() && vampire_it > upgraded_it) {
		std::iter_swap(vampire_it, upgraded_it);
	}
}

// Actualizar otras funciones que usan generación aleatoria para reutilizar el generador global
const weighted_benefit_t* SelectRandomBenefit(int32_t wave, WeightedSelection<weighted_benefit_t>& selection) {
	static std::vector<const weighted_benefit_t*> eligible_benefits;
	eligible_benefits.clear();

	for (const auto& benefit : benefits) {
		if (wave >= benefit.min_level &&
			(benefit.max_level == -1 || wave <= benefit.max_level) &&
			obtained_benefits.find(benefit.benefit_name) == obtained_benefits.end()) {
			eligible_benefits.emplace_back(&benefit);
		}
	}

	selection.rebuild(eligible_benefits);
	return selection.select();
}


// Aplicar el beneficio específico
void ApplyBenefit(const weighted_benefit_t* benefit) {
	if (!benefit) return;

	static const std::unordered_map<std::string_view, std::pair<std::string_view, std::string_view>> benefitMessages = {
		{"start armor", {"\n\n\nSTARTING ARMOR\nENABLED!\n", "STARTING WITH 50 BODY-ARMOR!\n"}},
		{"vampire", {"\n\n\nYou're covered in blood!\n\nVampire Ability\nENABLED!\n", "RECOVERING A HEALTH PERCENTAGE OF DAMAGE DONE!\n"}},
		{"ammo regen", {"AMMO REGEN\n\nENABLED!\n", "AMMO REGEN IS NOW ENABLED!\n"}},
		{"auto haste", {"\n\nDUAL-FIRE IS RUNNING THROUGH YOUR VEINS \nFRAGGING WHILE HASTE\nWILL EXTEND QUAD DMG AND DUAL-FIRE TIME!\n", "AUTO-HASTE ENABLED!\n"}},
		{"vampire upgraded", {"\n\n\n\nIMPROVED VAMPIRE ABILITY\n", "RECOVERING HEALTH & ARMOR NOW!\n"}},
		{"Cluster Prox Grenades", {"\n\n\n\nIMPROVED PROX GRENADES\n", "Prox Cluster Launcher Enabled\n"}},
		{"Traced-Piercing Bullets", {"\n\n\n\nBULLETS\nUPGRADED!\n", "Piercing-PowerShield Bullets!\n"}},
		{"Napalm-Grenade Launcher", {"\n\n\n\nIMPROVED GRENADE LAUNCHER!\n", "Napalm-Grenade Launcher Enabled\n"}},
		{"BFG Grav-Pull Lasers", {"\n\n\n\nBFG LASERS UPGRADED!\n", "BFG Grav-Pull Lasers Enabled\n"}}
	};

	// Aplicar cambios de juego específicos
	if (std::strcmp(benefit->benefit_name, "start armor") == 0) {
		gi.cvar_set("g_startarmor", "1");
	}
	else if (std::strcmp(benefit->benefit_name, "vampire") == 0) {
		vampire_level = 1;
		gi.cvar_set("g_vampire", "1");
	}
	else if (std::strcmp(benefit->benefit_name, "vampire upgraded") == 0) {
		vampire_level = 2;
		gi.cvar_set("g_vampire", "2");
	}
	else if (std::strcmp(benefit->benefit_name, "ammo regen") == 0) {
		gi.cvar_set("g_ammoregen", "1");
	}
	else if (std::strcmp(benefit->benefit_name, "auto haste") == 0) {
		gi.cvar_set("g_autohaste", "1");
	}
	else if (std::strcmp(benefit->benefit_name, "Cluster Prox Grenades") == 0) {
		gi.cvar_set("g_upgradeproxs", "1");
	}
	else if (std::strcmp(benefit->benefit_name, "Traced-Piercing Bullets") == 0) {
		gi.cvar_set("g_tracedbullets", "1");
	}
	else if (std::strcmp(benefit->benefit_name, "Napalm-Grenade Launcher") == 0) {
		gi.cvar_set("g_bouncygl", "1");
	}
	else if (std::strcmp(benefit->benefit_name, "BFG Grav-Pull Lasers") == 0) {
		gi.cvar_set("g_bfgpull", "1");
		gi.cvar_set("g_bfgslide", "0");
	}
	else {
		gi.Com_PrintFmt("PRINT: Unknown benefit: %s\n", benefit->benefit_name);
		return;
	}

	// Enviar los mensajes de beneficio
	auto it = benefitMessages.find(benefit->benefit_name);
	if (it != benefitMessages.end()) {
		gi.LocBroadcast_Print(PRINT_CENTER, it->second.first.data());
		gi.LocBroadcast_Print(PRINT_CHAT, it->second.second.data());
	}

	// Marcar el beneficio como obtenido
	obtained_benefits.emplace(benefit->benefit_name);
}

// Verificar y aplicar beneficios basados en la ola
void CheckAndApplyBenefit(const int32_t wave) {
	if (wave % 4 != 0) return;

	static WeightedSelection<weighted_benefit_t> selection;
	static std::vector<const weighted_benefit_t*> eligible_benefits;
	eligible_benefits.clear();

	if (shuffled_benefits.empty()) {
		ShuffleBenefits(mt_rand);
	}

	for (const auto& benefit : benefits) {
		if (wave >= benefit.min_level &&
			(benefit.max_level == -1 || wave <= benefit.max_level) &&
			obtained_benefits.find(benefit.benefit_name) == obtained_benefits.end()) {
			eligible_benefits.push_back(&benefit);
		}
	}

	if (eligible_benefits.empty()) return;

	selection.rebuild(eligible_benefits);
	if (const auto benefit = selection.select()) {
		ApplyBenefit(benefit);
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

inline void ClampNumToSpawn(const MapSize& mapSize) {
	int32_t maxAllowed = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
		(mapSize.isBigMap ? MAX_MONSTERS_BIG_MAP : MAX_MONSTERS_MEDIUM_MAP);
	g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, maxAllowed);
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

	// Enhanced level scaling for higher levels
	if (lvl > 25) {
		additionalSpawn = static_cast<int32_t>(additionalSpawn * 1.6f);
	}

	// Bonus for chaotic/insane modes
	if (lvl >= 3 && (g_chaotic->integer || g_insane->integer)) {
		additionalSpawn += CalculateChaosInsanityBonus(lvl);
	}

	// Dynamic difficulty scaling based on player count
	float difficultyMultiplier = 1.0f + (humanPlayers - 1) * 0.075f;

	// Periodic scaling adjustments
	if (lvl % 3 == 0) {
		baseCount = static_cast<int32_t>(baseCount * difficultyMultiplier);
		SPAWN_POINT_COOLDOWN = std::max(
			SPAWN_POINT_COOLDOWN - 0.15_sec * difficultyMultiplier,
			3.0_sec
		);
	}

	// Update spawn count with clamping
	g_horde_local.num_to_spawn = baseCount + static_cast<int32_t>(additionalSpawn);
	ClampNumToSpawn(mapSize);

	// Cooldown adjustments for higher levels
	if (lvl % 3 == 0) {
		const gtime_t spawnTimeReduction =
			(g_chaotic->integer || g_insane->integer) ? 0.25_sec : 0.15_sec;

		g_horde_local.monster_spawn_time -= spawnTimeReduction * difficultyMultiplier;
		g_horde_local.monster_spawn_time = std::max(
			g_horde_local.monster_spawn_time,
			2.0_sec
		);

		SPAWN_POINT_COOLDOWN -= spawnTimeReduction * difficultyMultiplier;
		SPAWN_POINT_COOLDOWN = std::max(SPAWN_POINT_COOLDOWN, 3.0_sec);
	}

	// Calculate queued monsters with better balance
	int32_t baseQueued = 0;

	// Solo aplicar cola si el nivel es mayor a 3
	if (lvl > 3) {
		// Base queue calculation
		baseQueued = lvl;

		// Ajuste por tamaño de mapa
		if (mapSize.isSmallMap) {
			baseQueued = static_cast<int32_t>(baseQueued * 0.7f);
		}
		else if (mapSize.isBigMap) {
			baseQueued = static_cast<int32_t>(baseQueued * 1.3f);
		}

		// Ajuste para niveles altos
		if (lvl > 20) {
			baseQueued += std::min((lvl - 20) * 2, 15);
		}

		// Bonus por dificultad
		if (g_insane->integer || g_chaotic->integer) {
			baseQueued = static_cast<int32_t>(baseQueued * 1.2f);
		}

		// Limitar el máximo de monstruos en cola según el tamaño del mapa
		int32_t maxQueued = mapSize.isSmallMap ? 25 : (mapSize.isBigMap ? 45 : 35);
		baseQueued = std::min(baseQueued, maxQueued);
	}

	// Actualizar la cola, reemplazando el valor anterior en lugar de acumular
	g_horde_local.queued_monsters = baseQueued;
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
constexpr gtime_t BASE_MAX_WAVE_TIME = 45_sec;
constexpr gtime_t TIME_INCREASE_PER_LEVEL = 0.8_sec;
constexpr int MONSTERS_FOR_AGGRESSIVE_REDUCTION = 5;
constexpr gtime_t AGGRESSIVE_TIME_REDUCTION_PER_MONSTER = 10_sec;

constexpr gtime_t calculate_max_wave_time(int32_t wave_level) {
	return (BASE_MAX_WAVE_TIME + TIME_INCREASE_PER_LEVEL * wave_level <= 65_sec) ?
		BASE_MAX_WAVE_TIME + TIME_INCREASE_PER_LEVEL * wave_level : 65_sec;
}

// Variables globales
static gtime_t g_condition_start_time;
static gtime_t g_independent_timer_start;
static ConditionParams g_lastParams;
static int32_t g_lastWaveNumber = -1;
static int32_t g_lastNumHumanPlayers = -1;
static bool g_maxMonstersReached = false;
static bool g_lowPercentageTriggered = false;

ConditionParams GetConditionParams(const MapSize& mapSize, int32_t numHumanPlayers, int32_t lvl) {
	ConditionParams params;

	auto configureMapParams = [&](ConditionParams& params) {
		if (mapSize.isBigMap) {
			params.maxMonsters = (numHumanPlayers >= 3) ? 26 : 24;
			params.timeThreshold = random_time(18_sec, 24_sec);
		}
		else if (mapSize.isSmallMap) {
			params.maxMonsters = (numHumanPlayers >= 3) ? 12 : 9;
			params.timeThreshold = random_time(14_sec, 20_sec);
		}
		else {
			params.maxMonsters = (numHumanPlayers >= 3) ? 17 : 15;
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
			params.timeThreshold += random_time(3_sec, 6_sec); // Reducido de 5-10 a 3-6
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
		gi.cvar_set("g_damage_scale", "2.35");
		break;
	case 25:
		gi.cvar_set("g_damage_scale", "3.25");
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

	{ "item_quad", 6, -1, 0.054f, adjust_weight_powerup },
	{ "item_double", 4, -1, 0.07f, adjust_weight_powerup },
	{ "item_quadfire", 2, -1, 0.06f, adjust_weight_powerup },
	{ "item_invulnerability", 4, -1, 0.041f, adjust_weight_powerup },
	{ "item_sphere_defender", 2, -1, 0.06f, adjust_weight_powerup },
	{ "item_sphere_vengeance", 23, -1, 0.06f, adjust_weight_powerup },
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
	{ "weapon_rocketlauncher", 14, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_railgun", 24, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_phalanx", 16, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_plasmabeam", 17, -1, 0.29f, adjust_weight_weapon },
	{ "weapon_disintegrator", 28, -1, 0.15f, adjust_weight_weapon },
	{ "weapon_bfg", 24, -1, 0.17f, adjust_weight_weapon },


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
	{ "ammo_nuke", 25, -1, 0.01f, adjust_weight_ammo },

	{ "item_bandolier", 4, -1, 0.2f, adjust_weight_ammo },
	{ "item_pack", 15, -1, 0.34f, adjust_weight_ammo },
	{ "item_silencer", 15, -1, 0.12f, adjust_weight_ammo },
};

// Definici�n de monstruos ponderados
constexpr weighted_item_t monsters[] = {
	{ "monster_soldier_light", -1, -1, 0.27f },
	{ "monster_tank_vanilla", 2, 4, 0.03f },
	{ "monster_soldier_ss", -1, 22, 0.25f },
	{ "monster_soldier", -1, 8, 0.2f },
	{ "monster_soldier_hypergun", -1, -1, 0.2f },
	{ "monster_soldier_lasergun", 5, -1, 0.35f },
	{ "monster_soldier_ripper", -1, 12, 0.25f },
	{ "monster_infantry_vanilla", -1, 1, 0.2f },
	{ "monster_infantry_vanilla", 2, -1, 0.39f },
	{ "monster_infantry", 9, -1, 0.36f },
	{ "monster_flyer", -1, 2, 0.07f },
	{ "monster_flyer", 3, -1, 0.1f },
	{ "monster_hover_vanilla", 8, 19, 0.2f },
	{ "monster_fixbot", 8, 21, 0.11f },
	{ "monster_gekk", -1, 3, 0.1f },
	{ "monster_gekk", 4, 17, 0.17f },
	{ "monster_gunner_vanilla", 5, -1, 0.35f },
	{ "monster_gunner", 14, -1, 0.34f },
	{ "monster_brain", 7, 15, 0.27f },
	{ "monster_brain", 16, -1, 0.4f },
	{ "monster_stalker", 2, 3, 0.1f },
	{ "monster_stalker", 4, 13, 0.19f },
	{ "monster_parasite", 4, 17, 0.23f },
	{ "monster_tank", 12, -1, 0.3f },
	{ "monster_tank_vanilla", 5, 10, 0.1f },
	{ "monster_tank_vanilla", 11, 23, 0.2f },
	{ "monster_tank_vanilla", 32, -1, 0.25f },
	{ "monster_runnertank", 14, -1, 0.24f },
	{ "monster_guncmdr2", 13, 10, 0.18f },
	{ "monster_mutant", 4, -1, 0.35f },
	{ "monster_redmutant", 6, 12, 0.02f },
	{ "monster_redmutant", 13, -1, 0.22f },
	{ "monster_chick", 5, 26, 0.3f },
	{ "monster_chick_heat", 10, -1, 0.34f },
	{ "monster_berserk", 4, -1, 0.3f },
	{ "monster_floater", 10, -1, 0.26f },
	{ "monster_hover", 15, -1, 0.18f },
	{ "monster_daedalus", 16, -1, 0.1f },
	{ "monster_daedalus_bomber", 21, -1, 0.14f },
	{ "monster_medic", 5, -1, 0.1f },
	{ "monster_medic_commander", 16, -1, 0.06f },
	{ "monster_tank_commander", 11, -1, 0.15f },
	{ "monster_spider", 11, 19, 0.27f },
	{ "monster_gm_arachnid", 22, -1, 0.24f },
	{ "monster_arachnid", 20, -1, 0.27f },
	{ "monster_guncmdr", 11, -1, 0.28f },
	{ "monster_gladc", 17, -1, 0.3f },
	{ "monster_gladiator", 14, -1, 0.3f },
	{ "monster_shambler", 16, 28, 0.03f },
	{ "monster_shambler", 29, -1, 0.33f },
	{ "monster_floater_tracker", 23, -1, 0.18f },
	{ "monster_tank_64", 24, -1, 0.11f },
	{ "monster_janitor", 24, -1, 0.14f },
	{ "monster_janitor2", 18, -1, 0.12f },
	{ "monster_makron", 17, 22, 0.015f },
	{ "monster_gladb", 18, -1, 0.45f },
	{ "monster_boss2_64", 16, -1, 0.1f },
	{ "monster_carrier_mini", 20, -1, 0.07f },
	{ "monster_perrokl", 21, -1, 0.33f },
	{ "monster_widow1", 23, -1, 0.08f }
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
	{"monster_psxarachnid", -1, 19, 0.1f, BossSizeCategory::Small},
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
	{"monster_psxarachnid", -1, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_makronkl", 26, -1, 0.1f, BossSizeCategory::Medium}
};

constexpr boss_t BOSS_LARGE[] = {
	{"monster_carrier", 24, -1, 0.1f, BossSizeCategory::Large},
	{"monster_boss2", 19, -1, 0.1f, BossSizeCategory::Large},
	{"monster_boss5", -1, -1, 0.1f, BossSizeCategory::Large},
	{"monster_tank_64", -1, 24, 0.1f, BossSizeCategory::Large},
	{"monster_psxarachnid", -1, -1, 0.1f, BossSizeCategory::Large},
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

constexpr int32_t MAX_RECENT_BOSSES = 3;
std::deque<const char*> recent_bosses;

// Función para agregar un jefe reciente de manera segura
static void AddRecentBoss(const char* classname) {
	if (classname == nullptr) return;

	recent_bosses.emplace_back(classname);
	if (recent_bosses.size() > MAX_RECENT_BOSSES) {
		recent_bosses.pop_front();
	}
}

const char* G_HordePickBOSS(const MapSize& mapSize, const std::string& mapname, int32_t waveNumber, edict_t* bossEntity) {
	const boss_t* boss_list = GetBossList(mapSize, mapname);
	if (!boss_list) return nullptr;

	const size_t boss_list_size = GetBossListSize(mapSize, mapname, boss_list);
	if (boss_list_size == 0) return nullptr;

	std::vector<const boss_t*> eligible_bosses;
	eligible_bosses.reserve(boss_list_size);

	const auto is_boss_recent = [](const char* classname) {
		return std::find(recent_bosses.begin(), recent_bosses.end(), classname) != recent_bosses.end();
		};

	// Filtrar jefes elegibles
	for (size_t i = 0; i < boss_list_size; ++i) {
		const boss_t& boss = boss_list[i];
		if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
			(waveNumber <= boss.max_level || boss.max_level == -1) &&
			!is_boss_recent(boss.classname)) {
			eligible_bosses.emplace_back(&boss);
		}
	}

	// Si no hay jefes elegibles, limpiar historial y reintentar
	if (eligible_bosses.empty()) {
		recent_bosses.clear();
		for (size_t i = 0; i < boss_list_size; ++i) {
			const boss_t& boss = boss_list[i];
			if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
				(waveNumber <= boss.max_level || boss.max_level == -1)) {
				eligible_bosses.push_back(&boss);
			}
		}
	}

	if (eligible_bosses.empty()) return nullptr;

	WeightedSelection<boss_t> selection;
	selection.rebuild(eligible_bosses);
	const boss_t* chosen_boss = selection.select();

	if (chosen_boss) {
		AddRecentBoss(chosen_boss->classname);
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
	std::vector<const weighted_item_t*> eligible_items;
	eligible_items.reserve(std::size(items));

	for (const auto& item : items) {
		if ((item.min_level == -1 || g_horde_local.level >= item.min_level) &&
			(item.max_level == -1 || g_horde_local.level <= item.max_level)) {
			float weight = item.weight;
			if (item.adjust_weight) {
				item.adjust_weight(item, weight);
			}
			if (weight > 0.0f) {
				eligible_items.push_back(&item);
			}
		}
	}

	if (eligible_items.empty()) return nullptr;

	WeightedSelection<weighted_item_t> selection;
	selection.rebuild(eligible_items);
	const weighted_item_t* chosen_item = selection.select();

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


bool IsFlyingMonster(const std::string& classname) {
	return flying_monsters_set.find(classname) != flying_monsters_set.end();
}

inline bool IsMonsterEligible(const edict_t* spawn_point, const weighted_item_t& item, bool isFlyingMonster, int32_t currentWave, int32_t flyingSpawns) noexcept {
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

inline double CalculateMonsterWeight(const weighted_item_t& item, bool isFlyingMonster, float adjustmentFactor) noexcept {
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
SpawnPointData& EnsureSpawnPointDataExists(edict_t* spawn_point) {
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

void CleanUpSpawnPointsData() {
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


// Function to update spawn point cooldowns and the last spawn times for the monster
void UpdateCooldowns(edict_t* spawn_point, const char* chosen_monster) {
	// Ensure the spawn point entry exists in spawnPointsData
	SpawnPointData& spawn_data = EnsureSpawnPointDataExists(spawn_point);

	// Update spawn time
	spawn_data.lastSpawnTime = level.time;

	if (chosen_monster) {
		spawn_data.lastSpawnedMonsterClassname = chosen_monster;
	}

	// Reset cooldown timer
	spawn_data.isTemporarilyDisabled = true;
	spawn_data.cooldownEndsAt = level.time + SPAWN_POINT_COOLDOWN;

	// Clean up old spawn points if necessary (could be called periodically elsewhere instead)
	CleanUpSpawnPointsData();
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
static bool IsSpawnPointOccupied(const edict_t* spawn_point, const edict_t* ignore_ent = nullptr, const edict_t* monster = nullptr) {
	// Define the bounding box for checking occupation
	vec3_t mins, maxs;
	// If there is a specific monster, use its bounding box dimensions
	if (monster) {
		mins = spawn_point->s.origin + monster->mins;
		maxs = spawn_point->s.origin + monster->maxs;
	}
	else {
		// Default bounding box for player size
		mins = spawn_point->s.origin + vec3_t{ -16, -16, -24 };
		maxs = spawn_point->s.origin + vec3_t{ 16, 16, 32 };
	}
	// Data structure to hold information for filtering entities
	FilterData filter_data = { ignore_ent, 0 };
	// Use BoxEdicts to check for any relevant entities in the area
	gi.BoxEdicts(mins, maxs, nullptr, 0, AREA_SOLID, SpawnPointFilter, &filter_data);
	// Return true if we found at least one player or bot in the area
	return filter_data.count > 0;
}

const char* G_HordePickMonster(edict_t* spawn_point) {
	if (spawnPointsData[spawn_point].isTemporarilyDisabled || IsSpawnPointOccupied(spawn_point)) {
		return nullptr;
	}

	int32_t flyingSpawns = countFlyingSpawns();
	float adjustmentFactor = adjustFlyingSpawnProbability(flyingSpawns);

	std::vector<const weighted_item_t*> eligible_monsters;
	for (const auto& item : monsters) {
		bool isFlyingMonster = IsFlyingMonster(item.classname);
		if (IsMonsterEligible(spawn_point, item, isFlyingMonster, g_horde_local.level, flyingSpawns)) {
			eligible_monsters.push_back(&item);
		}
	}

	if (eligible_monsters.empty()) {
		IncreaseSpawnAttempts(spawn_point);
		return nullptr;
	}

	WeightedSelection<weighted_item_t> selection;
	selection.rebuild(eligible_monsters);

	// Ajustar los pesos antes de seleccionar
	std::vector<const weighted_item_t*> adjusted_monsters;
	for (const auto& monster : eligible_monsters) {
		double adjusted_weight = CalculateMonsterWeight(*monster, IsFlyingMonster(monster->classname), adjustmentFactor);
		if (adjusted_weight > 0.0) {
			adjusted_monsters.push_back(monster);
		}
	}
	selection.rebuild(adjusted_monsters);

	const weighted_item_t* chosen_monster = selection.select();

	if (chosen_monster) {
		UpdateCooldowns(spawn_point, chosen_monster->classname);
		ResetSingleSpawnPointAttempts(spawn_point);
		return chosen_monster->classname;
	}

	return nullptr;
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
		gi.cvar_set("timelimit", "50");
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
void PrecacheItemsAndBosses() noexcept {
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

void Horde_Init() {
	// Precache items, bosses, monsters, and sounds
	PrecacheItemsAndBosses();
	PrecacheAllMonsters();
	PrecacheAllSounds();
	PrecacheWaveSounds();

	// Inicializar otros sistemas de la horda (e.g., sistema de oleadas)
	InitializeWaveSystem();
	last_wave_number = 0;

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
	std::uniform_int_distribution<> horizontalDis(minHorizontal, maxHorizontal);
	std::uniform_int_distribution<> verticalDis(minVertical, maxVertical);

	return {
		static_cast<float>(horizontalDis(mt_rand)),
		static_cast<float>(horizontalDis(mt_rand)),
		static_cast<float>(verticalDis(mt_rand) + std::uniform_int_distribution<>(0, VERTICAL_VELOCITY_RANDOM_RANGE)(mt_rand))
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

void BossDeathHandler(edict_t* boss) {
	if (!g_horde->integer || !boss->spawnflags.has(SPAWNFLAG_IS_BOSS) || boss->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
		return;
	}
	OnEntityDeath(boss);
	OnEntityRemoved(boss); // Añadido para liberar el configstring
	boss->spawnflags |= SPAWNFLAG_BOSS_DEATH_HANDLED;

	// Liberar el jefe del conjunto de jefes generados automáticamente
	auto_spawned_bosses.erase(boss);

	const std::array<const char*, 7> itemsToDrop = {
		"item_adrenaline", "item_pack", "item_doppleganger",
		"item_sphere_defender", "item_armor_combat", "item_bandolier",
		"item_invulnerability"
	};

	// Soltar ítem especial (quad o quadfire) usando brandom()
	const char* specialItemName = brandom() ? "item_quadfire" : "item_quad";


	edict_t* specialItem = Drop_Item(boss, FindItemByClassname(specialItemName));

	const vec3_t specialVelocity = GenerateRandomVelocity(MIN_VELOCITY, MAX_VELOCITY, 300, 400);
	SetupDroppedItem(specialItem, boss->s.origin, specialVelocity, false);

	// Configuración adicional para el ítem especial
	specialItem->s.effects |= EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER | EF_HOLOGRAM;
	//specialItem->s.renderfx |= RF_SHELL_RED;
	specialItem->s.alpha = 0.8f;
	specialItem->s.scale = 1.5f;

	// Soltar los demás ítems
	std::vector<const char*> shuffledItems(itemsToDrop.begin(), itemsToDrop.end());
	std::shuffle(shuffledItems.begin(), shuffledItems.end(), mt_rand);

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
			}
		}
	}
	gi.Com_Print("DEBUG: All monsters are dead.\n");
	return true;
}

void CheckAndRestoreMonsterAlpha(edict_t* ent) {
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

THINK(fade_out_think)(edict_t* self) -> void {
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
			if (ent->spawnflags.has(SPAWNFLAG_IS_BOSS) &&
				!ent->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
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

	// Crear el vector de origen una sola vez
	const vec3_t spawn_origin = {
		static_cast<float>(it->second[0]),
		static_cast<float>(it->second[1]),
		static_cast<float>(it->second[2])
	};

	// Validar origen antes de continuar
	if (spawn_origin == vec3_origin) {
		gi.Com_PrintFmt("PRINT: Error: Invalid spawn origin for map {}\n", level.mapname);
		return;
	}

	// Create black light effect first
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

	// Set flying monsters mode if applicable
	if (boss->classname && (
		strcmp(boss->classname, "monster_boss2") == 0 ||
		strcmp(boss->classname, "monster_carrier") == 0 ||
		strcmp(boss->classname, "monster_carrier_mini") == 0 ||
		strcmp(boss->classname, "monster_boss2kl") == 0)) {
		flying_monsters_mode = true;
	}

	// Set boss origin
	boss->s.origin = spawn_origin;
	gi.Com_PrintFmt("PRINT: Preparing to spawn boss at position: {}\n", boss->s.origin);

	// Push entities away
	PushEntitiesAway(boss->s.origin, 3, 500, 300.0f, 750.0f, 1000.0f, 500.0f);

	// Store orb entity in boss for later removal
	boss->owner = orb;

	// Delay boss spawn
	boss->nextthink = level.time + 1000_ms; // 1 second delay
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
	self->spawnflags |= SPAWNFLAG_IS_BOSS | SPAWNFLAG_MONSTER_SUPER_STEP;
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
	for (auto& [spawn_point, data] : spawnPointsData) {
		data.attempts = 0;
		data.cooldown = SPAWN_POINT_COOLDOWN;
		data.isTemporarilyDisabled = false;
	}
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
	for (auto& [spawn_point, data] : spawnPointsData) {
		data.attempts = 0;
		data.cooldown = SPAWN_POINT_COOLDOWN;
	}
}

static void ResetRecentBosses() noexcept {
	// Reinicia la lista de bosses recientes
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
bool CheckRemainingMonstersCondition(const MapSize& mapSize, WaveEndReason& reason) {
	const gtime_t currentTime = level.time;
	const bool allMonstersDead = Horde_AllMonstersDead();

	// Verificar si se ha permitido avanzar la ola manualmente
	if (allowWaveAdvance) {
		gi.Com_PrintFmt("PRINT: Wave advance allowed manually.\n");
		ResetWaveAdvanceState();
		reason = WaveEndReason::AllMonstersDead;
		return true;
	}

	// Verificar si todos los monstruos han sido derrotados
	if (allMonstersDead) {
		reason = WaveEndReason::AllMonstersDead;
		return true;
	}

	// Verificar tiempo límite independiente
	if (currentTime >= g_independent_timer_start + g_lastParams.independentTimeThreshold) {
		reason = WaveEndReason::TimeLimitReached;
		return true;
	}

	// Inicializar waveEndTime si no está establecido
	if (g_horde_local.waveEndTime == 0_sec) {
		g_horde_local.waveEndTime = g_independent_timer_start + g_lastParams.independentTimeThreshold;
	}

	// Obtener número de monstruos restantes una sola vez
	int32_t remainingMonsters = CalculateRemainingMonsters();
	float percentageRemaining = static_cast<float>(remainingMonsters) / static_cast<float>(g_totalMonstersInWave);

	bool shouldAdvance = false;

	// Determinar si alguna condición se ha cumplido
	if (!g_horde_local.conditionTriggered) {
		if (remainingMonsters <= g_lastParams.maxMonsters || percentageRemaining <= g_lastParams.lowPercentageThreshold) {
			g_horde_local.conditionTriggered = true;
			g_horde_local.conditionStartTime = currentTime;

			// Elegir el menor umbral de tiempo
			if (remainingMonsters <= g_lastParams.maxMonsters && percentageRemaining <= g_lastParams.lowPercentageThreshold) {
				g_horde_local.conditionTimeThreshold = std::min(g_lastParams.timeThreshold, g_lastParams.lowPercentageTimeThreshold);
				gi.LocBroadcast_Print(PRINT_HIGH, "Conditions met. Wave time reduced!\n");
			}
			else if (remainingMonsters <= g_lastParams.maxMonsters) {
				g_horde_local.conditionTimeThreshold = g_lastParams.timeThreshold;
			}
			else {
				g_horde_local.conditionTimeThreshold = g_lastParams.lowPercentageTimeThreshold;
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
				gi.LocBroadcast_Print(PRINT_HIGH, "Very few monsters remaining. Wave time reduced!\n");
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
			gi.LocBroadcast_Print(PRINT_HIGH, "{} seconds remaining in this wave!\n", static_cast<int>(WARNING_TIMES[i]));
			g_horde_local.warningIssued[i] = true;
		}
	}

	// Verificar si el tiempo de la ola ha llegado a cero
	if (currentTime >= g_horde_local.waveEndTime) {
		if (currentTime >= (g_independent_timer_start + g_lastParams.independentTimeThreshold)) {
			reason = WaveEndReason::TimeLimitReached;
			shouldAdvance = true;
		}
		else if (g_horde_local.conditionTriggered && currentTime >= (g_horde_local.conditionStartTime + g_horde_local.conditionTimeThreshold)) {
			reason = WaveEndReason::MonstersRemaining;
			shouldAdvance = true;
		}
	}

	if (shouldAdvance) {
		// No resetear el timer aquí, se manejará en HandleWaveRestMessage
		gi.Com_PrintFmt("PRINT: Wave advance triggered. Reason: {}\n",
			reason == WaveEndReason::TimeLimitReached ? "Time Limit" : "Monsters Remaining");
		return true;
	}

	// Proveer actualizaciones periódicas sobre monstruos restantes y tiempo
	if (currentTime - g_horde_local.lastPrintTime >= 10_sec) {
		gi.Com_PrintFmt("PRINT: Wave status: {} monsters remaining. {:.2f} seconds left.\n",
			remainingMonsters, remainingTime.seconds());
		g_horde_local.lastPrintTime = currentTime;
	}

	return false;
}

void ResetGame() {
	// Resetear todas las variables globales
	last_wave_number = 0;
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
	SPAWN_POINT_COOLDOWN = 3.9_sec;

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
	auto_spawned_bosses.clear(); // Limpiar todos los jefes generados automáticamente
	ResetAllSpawnAttempts();
	ResetCooldowns();
	ResetBenefits();
	vampire_level = 0;

	// Reiniciar la lista de bosses recientes
	ResetRecentBosses();

	// Reiniciar wave advance state
	ResetWaveAdvanceState();


	// Log del reset
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

	// Reiniciar semilla aleatoria
	srand(static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count()));

	// Registrar el reinicio
	gi.Com_PrintFmt("PRINT: Horde game state reset complete.\n");
}
static gtime_t g_lastMonsterCountVerification = 0_ms;
constexpr gtime_t MONSTER_COUNT_VERIFICATION_INTERVAL = 5_sec;

// Función para contar los monstruos activos
static int32_t CountActiveMonsters() {
	return std::count_if(active_monsters().begin(), active_monsters().end(),
		[](const edict_t* ent) {
			return !ent->deadflag && !(ent->monsterinfo.aiflags & AI_DO_NOT_COUNT);
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

	// Forzar la actualización del estado de la ola
	g_monster_check_cache.Reset();
//	g_horde_local.state = horde_state_t::cleanup;
	g_horde_local.monster_spawn_time = level.time;

	g_horde_local.warm_time = level.time;
	g_horde_local.num_to_spawn = 0; // Establecer a cero para indicar que no hay más monstruos por spawnear
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
	WaveMessageType msgType = (brandom()) ? WaveMessageType::InventoryPrompt : WaveMessageType::Welcome;
	switch (msgType) {
	case WaveMessageType::InventoryPrompt:
		UpdateHordeMessage("Use Inventory <KEY> or Use Compass To Open Horde Menu.\n\nMAKE THEM PAY!\n", duration);
		break;
	case WaveMessageType::Welcome:
		UpdateHordeMessage("Welcome to Hell.\n\nNew! Use FlipOff <Key> looking to the wall to spawn a laser (cost: 25 cells)", duration);
		break;
	}
}

// Funci�n para manejar el mensaje de limpieza de ola
static void HandleWaveCleanupMessage(const MapSize& mapSize) noexcept {
	bool isStandardWave = (current_wave_level >= 15 && current_wave_level <= 26);
	bool isAdvancedWave = (current_wave_level >= 27);
	bool isInitialWave = (current_wave_level <= 14);

	if (isStandardWave) {
		gi.cvar_set("g_insane", "1");
		gi.cvar_set("g_chaotic", "0");
	}
	else if (isAdvancedWave) {
		gi.cvar_set("g_insane", "2");
		gi.cvar_set("g_chaotic", "0");
	}
	else if (isInitialWave) {
		gi.cvar_set("g_chaotic", mapSize.isSmallMap ? "2" : "1");
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

	// Resetear el timer y estados relacionados aquí
	g_independent_timer_start = level.time;
	g_horde_local.waveEndTime = 0_sec;
	g_horde_local.conditionTriggered = false;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTimeThreshold = 0_sec;

	// Resetear las advertencias
	std::fill(g_horde_local.warningIssued.begin(), g_horde_local.warningIssued.end(), false);

	// Reproducir un sonido aleatorio
	gi.sound(world, CHAN_VOICE, gi.soundindex(GetRandomWaveSound()), 1, ATTN_NONE, 0);
}

// Llamar a esta función durante la inicialización del juego
void InitializeWaveSystem() noexcept {
	PrecacheWaveSounds();
}

static void SetMonsterArmor(edict_t* monster);
static void SetNextMonsterSpawnTime(const MapSize& mapSize);

static edict_t* SpawnMonsters() {
	const MapSize& mapSize = GetMapSize(level.mapname);
	std::vector<edict_t*> available_spawns;
	available_spawns.reserve(MAX_EDICTS);

	// Collect all available non-boss spawn points
	for (unsigned int edictIndex = 1; edictIndex < globals.num_edicts; ++edictIndex) {
		edict_t* e = &g_edicts[edictIndex];
		if (e->inuse && e->classname && std::strcmp(e->classname, "info_player_deathmatch") == 0 && !e->spawnflags.has(SPAWNFLAG_IS_BOSS)) {
			available_spawns.emplace_back(e);
		}
	}

	if (available_spawns.empty()) {
		gi.Com_PrintFmt("PRINT: Warning: No spawn points found\n");
		return nullptr;
	}

	// Determine how many monsters to spawn in this call
	const int32_t default_monsters_per_spawn = mapSize.isSmallMap ? 4 :
		(mapSize.isBigMap ? 6 : 5);
	const int32_t monsters_per_spawn = (g_horde_local.queued_monsters > 0) ?
		std::min(g_horde_local.queued_monsters, static_cast<int32_t>(3)) :
		std::min(default_monsters_per_spawn, static_cast<int32_t>(6));

	// Ensure we don't spawn more than allowed by the map
	const int32_t activeMonsters = CountActiveMonsters();
	const int32_t maxMonsters = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
		(mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP);
	const int32_t spawnable = maxMonsters - activeMonsters;
	const int32_t actual_spawn_count = std::min(monsters_per_spawn, spawnable);

	if (actual_spawn_count <= 0) {
		gi.Com_Print("PRINT: Maximum number of monsters reached. No more spawns.\n");
		return nullptr;
	}

	edict_t* last_spawned_monster = nullptr;

	// Randomly shuffle spawn points using mt_rand
	std::shuffle(available_spawns.begin(), available_spawns.end(), mt_rand);

	int32_t spawnIndex = 0;
	for (int32_t spawnCount = 0; spawnCount < actual_spawn_count && g_horde_local.num_to_spawn > 0 && spawnIndex < static_cast<int32_t>(available_spawns.size()); ++spawnCount, ++spawnIndex) {
		edict_t* spawn_point = available_spawns[spawnIndex];

		const char* monster_classname = G_HordePickMonster(spawn_point);
		if (!monster_classname) {
			continue; // Skip if no valid monster could be selected
		}

		// Spawn the monster
		edict_t* monster = G_Spawn();
		if (!monster) {
			gi.Com_PrintFmt("PRINT: G_Spawn Warning: Failed to spawn monster\n");
			continue;
		}

		monster->classname = monster_classname;
		monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
		monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
		monster->monsterinfo.last_sentrygun_target_time = 0_ms;

		// Reemplazamos VectorCopy con asignación directa
		monster->s.origin = spawn_point->s.origin;
		monster->s.angles = spawn_point->s.angles;

		ED_CallSpawn(monster);

		if (!monster->inuse) {
			gi.Com_PrintFmt("PRINT: ED_CallSpawn Warning: Monster spawn failed\n");
			G_FreeEdict(monster);
			continue;
		}

		// Adjust armor if needed
		if (g_horde_local.level >= 17) {
			SetMonsterArmor(monster);
		}

		// Determine if monster will drop an item
		float drop_probability = (g_horde_local.level <= 2) ? 0.8f :
			(g_horde_local.level <= 7) ? 0.6f : 0.45f;
		if (std::uniform_real_distribution<float>(0.0f, 1.0f)(mt_rand) <= drop_probability) {
			monster->item = G_HordePickItem();
		}

		// Apply spawn grow effects
		SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
		gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

		// Update counters
		g_horde_local.num_to_spawn = std::max(g_horde_local.num_to_spawn - 1, 0);
		g_horde_local.queued_monsters = std::max(g_horde_local.queued_monsters - 1, 0);
		g_totalMonstersInWave++;
		last_spawned_monster = monster;
	}

	// Handle additional queued monsters if there's still spawn capacity
	if (g_horde_local.queued_monsters > 0 && g_horde_local.num_to_spawn > 0) {
		const int32_t additional_spawnable = maxMonsters - CountActiveMonsters();
		const int32_t additional_to_spawn = std::min(g_horde_local.queued_monsters, additional_spawnable);
		g_horde_local.num_to_spawn += additional_to_spawn;
		g_horde_local.queued_monsters = std::max(g_horde_local.queued_monsters - additional_to_spawn, 0);
		ClampNumToSpawn(mapSize); // Ensure num_to_spawn doesn't exceed the allowed limit
	}

	SetNextMonsterSpawnTime(mapSize);
	return last_spawned_monster;
}
// Funciones auxiliares para reducir el tamaño de SpawnMonsters
static void SetMonsterArmor(edict_t* monster) {
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (!st.was_key_specified("power_armor_power"))
		monster->monsterinfo.armor_type = IT_ARMOR_COMBAT;
	if (!st.was_key_specified("power_armor_type")) {
		const float health_factor = sqrt(std::max(0.0f, monster->max_health / 100.0f));
		const int base_armor = (current_wave_level <= 25) ?
			irandom(75, 225) + health_factor * irandom(1, 3) :
			irandom(150, 320) + health_factor * irandom(2, 5);

		const int additional_armor = static_cast<int>((current_wave_level - 20) * 10 * health_factor);
		monster->monsterinfo.armor_power = base_armor + additional_armor;
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

// Definición de los mensajes de limpieza con placeholders nombrados
const std::unordered_map<MessageType, std::string_view> cleanupMessages = {
	{MessageType::Standard, "Wave Level {level} Defeated, GG!\n"},
	{MessageType::Chaotic, "Harder Wave Level {level} Controlled, GG!\n"},
	{MessageType::Insane, "Insane Wave Level {level} Controlled, GG!\n"}
};

// Ejemplo: Uso de enum class para WaveEndReason
void SendCleanupMessage(WaveEndReason reason) {
	gtime_t duration = 3_sec; // Duración por defecto

	// Si allowWaveAdvance está activo y la razón es AllMonstersDead, establecer duración a 0
	if (allowWaveAdvance && reason == WaveEndReason::AllMonstersDead) {
		duration = 0_sec;
	}

	std::string formattedMessage;

	switch (reason) {
	case WaveEndReason::AllMonstersDead:
		formattedMessage = fmt::format("Wave Level {} Defeated, GG!\n", g_horde_local.level);
		break;
	case WaveEndReason::MonstersRemaining:
		formattedMessage = fmt::format("Wave Level {} Pushed Back, But Still Threatening!\n", g_horde_local.level);
		break;
	case WaveEndReason::TimeLimitReached:
		formattedMessage = fmt::format("Time's up! Wave Level {} Ended!\n", g_horde_local.level);
		break;
	}

	UpdateHordeMessage(formattedMessage, duration);
}

// Add this function in the appropriate source file that deals with spawn management.
static void CheckAndResetDisabledSpawnPoints() {
	for (auto& [spawn_point, data] : spawnPointsData) {
		if (data.isTemporarilyDisabled && level.time >= data.cooldown) {
			data.isTemporarilyDisabled = false;  // Restablecer el punto de spawn
			data.attempts = 0;  // Resetear los intentos fallidos
			//	gi.Com_PrintFmt("PRINT: SpawnPoint {} is active again.\n", spawn_point->s.origin);
		}
	}
}

void Horde_RunFrame() {
	const MapSize& mapSize = GetMapSize(level.mapname);
	const int32_t currentLevel = g_horde_local.level;
	CheckAndResetDisabledSpawnPoints();

	// Manejo de monstruos personalizados
	if (dm_monsters->integer > 0) {
		g_horde_local.num_to_spawn = dm_monsters->integer;
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
				std::fill(g_horde_local.warningIssued.begin(), g_horde_local.warningIssued.end(), false);
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
			g_horde_local.monster_spawn_time = level.time;
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
			// Ajustar configuraciones de dificultad
			if (current_wave_level >= 15 && current_wave_level <= 28) {
				gi.cvar_set("g_insane", "1");
				gi.cvar_set("g_chaotic", "0");
			}
			else if (current_wave_level >= 31) {
				gi.cvar_set("g_insane", "2");
				gi.cvar_set("g_chaotic", "0");
			}
			else if (current_wave_level <= 14) {
				gi.cvar_set("g_insane", "0");
				gi.cvar_set("g_chaotic", mapSize.isSmallMap ? "2" : "1");
			}

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
	// If we're not in an active wave or cleanup state, return 0
	if (g_horde_local.state != horde_state_t::active_wave &&
		g_horde_local.state != horde_state_t::cleanup) {
		return 0_sec;
	}

	const gtime_t currentTime = level.time;
	gtime_t remainingTime = 0_sec;

	// If condition is triggered, use condition time
	if (g_horde_local.conditionTriggered && g_horde_local.waveEndTime > currentTime) {
		remainingTime = g_horde_local.waveEndTime - currentTime;
	}
	// If no condition triggered or condition time is higher, check independent timer
	if (!g_horde_local.conditionTriggered ||
		(g_independent_timer_start + g_lastParams.independentTimeThreshold - currentTime < remainingTime)) {
		gtime_t independentRemaining = g_independent_timer_start + g_lastParams.independentTimeThreshold - currentTime;
		if (independentRemaining > 0_sec) {
			remainingTime = (remainingTime > 0_sec) ?
				std::min(remainingTime, independentRemaining) : independentRemaining;
		}
	}

	return remainingTime;
}