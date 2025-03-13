// Includes y definiciones relevantes
#include "../g_local.h"
#include "g_horde.h"
#include "../shared.h"
#include <set>
#include "g_horde_benefits.h"
#include <span>


static bool need_spawn_cache_reset = false;
static bool need_frame_timer_reset = false;
static bool need_queue_monitor_reset = false;

void ResetSpawnMonsterVars() {
	need_spawn_cache_reset = true;
}

void ResetFrameTimers() {
	need_frame_timer_reset = true;
}

void ResetQueueMonitorVars() {
	need_queue_monitor_reset = true;
}

// Monster count verification tracking
static int consistent_zero_counts = 0;
static int counter_mismatch_frames = 0;

// Maximum number of spawn points to track
constexpr size_t MAX_SPAWN_POINTS = 32;

namespace HordeConstants {
// constexpr float PLAYER_MULTIPLIER = 0.2f;
	constexpr float TIME_REDUCTION_MULTIPLIER = 0.95f;
	constexpr float DIFFICULTY_PLAYER_FACTOR = 0.075f;
	constexpr float BASE_DIFFICULTY_MULTIPLIER = 1.0f;
	constexpr float PLAYER_COUNT_SCALE = 0.2f;

	// Base counts for different map sizes and levels
	constexpr std::array<std::array<int32_t, 4>, 3> BASE_COUNTS = { {
		{{6, 8, 10, 12}},  // Small maps
		{{8, 12, 14, 16}}, // Medium maps
		{{15, 18, 23, 26}} // Large maps
	} };

	// Additional spawn counts
	constexpr std::array<int32_t, 3> ADDITIONAL_SPAWNS = { 8, 7, 12 }; // Small, Medium, Large
}

// Optimized spawn cooldown data structure
// Optimized spawn cooldown data structure
struct SpawnPointData {
	uint16_t attempts = 0;
	gtime_t teleport_cooldown = 0_sec;  // Teleport cooldown
	gtime_t lastSpawnTime = 0_sec;
	bool isTemporarilyDisabled = false;
	gtime_t cooldownEndsAt = 0_sec;
	int32_t successfulSpawns = 0;

	// Simplified success rate calculation
	float getSuccessRate(gtime_t current_time) const {
		if (attempts == 0) return 1.0f;
		// Use faster approximation - avoid division when possible
		const float time_factor = (current_time - lastSpawnTime).seconds() >= 5.0f ?
			1.0f : (current_time - lastSpawnTime).seconds() * 0.2f;
		return (float(successfulSpawns) / float(attempts)) + time_factor;
	}
};

struct SpawnPointDataArray {
	SpawnPointData data[MAX_EDICTS];

	SpawnPointData& operator[](const edict_t* ent) {
		return data[ent - g_edicts];
	}

	const SpawnPointData& operator[](const edict_t* ent) const {
		return data[ent - g_edicts];
	}

	void clear() {
		for (auto& item : data) {
			item = SpawnPointData{};
		}
	}

	// Add methods to simulate find/emplace behavior for transition
	bool find_and_access(const edict_t* key, SpawnPointData*& data_ptr) {
		data_ptr = &data[key - g_edicts];
		return true;
	}
};
SpawnPointDataArray spawnPointsData;

void IncreaseSpawnAttempts(edict_t* spawn_point) {
	if (!spawn_point || !spawn_point->inuse) {
		return;
	}

	// Direct array access instead of map lookup
	auto& data = spawnPointsData[spawn_point];
	const gtime_t current_time = level.time;

	// Reset attempts if enough time has passed - early return optimization
	if (current_time - data.lastSpawnTime > 6_sec) {
		data.attempts = 0;
		data.isTemporarilyDisabled = false;
		data.cooldownEndsAt = current_time;
		return;
	}

	data.attempts++;

	// Dynamic attempt limit based on current success rate - improved calculation
	// Cache success rate calculation
	const float success_rate = data.getSuccessRate(current_time);
	const int max_attempts = 4 + (success_rate >= 0.5f ? 2 : (success_rate >= 0.25f ? 1 : 0));

	// Adaptive cooldown duration - fewer branches with ternary
	if (data.attempts >= max_attempts) {
		data.isTemporarilyDisabled = true;

		// Simplified cooldown calculation
		const float cooldown_factor = success_rate < 0.3f ? 1.5f : 0.75f;
		const float attempt_multiplier = data.attempts <= 8 ? data.attempts * 0.25f : 2.0f;
		data.cooldownEndsAt = current_time + gtime_t::from_sec(cooldown_factor * attempt_multiplier);

		if (developer->integer == 1) {
			gi.Com_PrintFmt("SpawnPoint at {} inactivated for adaptive cooldown.\n",
				spawn_point->s.origin);
		}
	}
	else if ((data.attempts & 1) == 0) { // Check if even using bitwise AND instead of modulo
		// Small incremental cooldown every 2 attempts
		data.cooldownEndsAt = current_time + gtime_t::from_sec(0.2f * data.attempts);
	}
}

void OnSuccessfulSpawn(edict_t* spawn_point) {
	if (!spawn_point) return;

	auto& data = spawnPointsData[spawn_point];
	data.successfulSpawns++;
	data.lastSpawnTime = level.time;
	data.attempts = 0; // Reset attempts after success
	data.isTemporarilyDisabled = false;

	// Short cooldown after successful spawn to prevent instant respawn
	data.cooldownEndsAt = level.time + 0.5_sec;
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
	if (ent->svflags & SVF_MONSTER && !ent->deadflag) {
		filter_data->count++;
		return BoxEdictsResult_t::End; // Stop searching if a monster is found
	}

	return BoxEdictsResult_t::Skip;
}

struct SpawnPointCache {
	gtime_t last_check_time;
	vec3_t last_check_origin;
	gtime_t frame_time;  // Changed from frame_number to frame_time
	bool was_occupied = false;
};
static std::unordered_map<const edict_t*, SpawnPointCache> spawn_point_cache;

// ¿Está el punto de spawn ocupado?
// Verify if any spawn points are occupied, using span for efficient iteration
// Original single point check
bool IsSpawnPointOccupied(const edict_t* spawn_point, const edict_t* ignore_ent = nullptr) {
	// Add validation first
	if (!spawn_point) {
		gi.Com_PrintFmt("Warning: Null spawn_point passed to IsSpawnPointOccupied\n");
		return true; // Safer to assume occupied if invalid
	}

	// Validate origin
	if (!is_valid_vector(spawn_point->s.origin)) {
		gi.Com_PrintFmt("Warning: Invalid origin vector in spawn point\n");
		return true;
	}

	// Get or create cache entry - with proper reference
	auto cache_it = spawn_point_cache.find(spawn_point);
	SpawnPointCache* cache_ptr = nullptr;

	if (cache_it != spawn_point_cache.end()) {
		cache_ptr = &cache_it->second;
	}
	else {
		auto [it, inserted] = spawn_point_cache.emplace(spawn_point, SpawnPointCache{});
		cache_ptr = &it->second;
	}

	SpawnPointCache& cache = *cache_ptr;

	// Static duration to avoid reconstructing
	static constexpr auto CACHE_DURATION = 25_ms;

	// Check time-based cache with frame validation
	if (level.time - cache.last_check_time < CACHE_DURATION
		&& cache.last_check_origin == spawn_point->s.origin
		&& (level.time - cache.frame_time) < FRAME_TIME_MS) {
		return cache.was_occupied;
	}

	// Update cache
	cache.last_check_time = level.time;
	cache.last_check_origin = spawn_point->s.origin;
	cache.frame_time = level.time;

	// Optimized space check - precalculate scaled vectors
	static const vec3_t mins_scale = vec3_t{ 16, 16, 24 }.scaled(vec3_t{ 1.75f, 1.75f, 1.75f });
	static const vec3_t maxs_scale = vec3_t{ 16, 16, 32 }.scaled(vec3_t{ 1.75f, 1.75f, 1.75f });

	const vec3_t spawn_mins = spawn_point->s.origin - mins_scale;
	const vec3_t spawn_maxs = spawn_point->s.origin + maxs_scale;

	// Use stack-allocated filter data
	FilterData filter_data = { ignore_ent, 0 };
	gi.BoxEdicts(spawn_mins, spawn_maxs, nullptr, 0, AREA_SOLID, SpawnPointFilter, &filter_data);

	// Cache and return result
	cache.was_occupied = (filter_data.count > 0);
	return cache.was_occupied;
}

//// New span-based batch check
//bool AreSpawnPointsOccupied(std::span<const edict_t* const> spawn_points, const edict_t* ignore_ent = nullptr) {
//	if (spawn_points.empty()) {
//		return true;
//	}
//
//	// Process spawn points in batches for better cache utilization
//	static constexpr size_t BATCH_SIZE = 8;
//	for (size_t i = 0; i < spawn_points.size(); i += BATCH_SIZE) {
//		const size_t batch_end = std::min(i + BATCH_SIZE, spawn_points.size());
//		std::span<const edict_t* const> batch = spawn_points.subspan(i, batch_end - i);
//
//		for (const edict_t* spawn_point : batch) {
//			if (IsSpawnPointOccupied(spawn_point, ignore_ent)) {
//				return true;
//			}
//		}
//	}
//
//	return false;
//}

static void CleanupSpawnPointCache() noexcept {
	spawn_point_cache.clear();
}

//// Optimized function to select a random unoccupied monster spawn point
//edict_t* SelectRandomMonsterSpawnPoint() {
//	static std::vector<edict_t*> availableSpawns;
//	availableSpawns.clear();
//
//	auto spawnPoints = monster_spawn_points();
//
//	for (edict_t* spawnPoint : spawnPoints) {
//		if (!IsSpawnPointOccupied(spawnPoint)) {
//			availableSpawns.push_back(spawnPoint);
//		}
//	}
//
//	if (availableSpawns.empty()) {
//		return nullptr;
//	}
//
//	return availableSpawns[irandom(availableSpawns.size())];
//}

template <typename TFilter>
edict_t* SelectRandomSpawnPoint(TFilter filter) {
	// Pre-allocate on stack instead of using static vector
	edict_t* availableSpawns[MAX_SPAWN_POINTS]{};
	int availableCount = 0;

	// Get all spawn points but don't process them yet
	auto spawnPoints = monster_spawn_points();

	// First pass: collect only potentially valid spawn points
	for (edict_t* spawnPoint : spawnPoints) {
		if (!spawnPoint || !spawnPoint->inuse)
			continue;

		// Check if spawn point is on cooldown
		auto const& data = spawnPointsData[spawnPoint];
		if (data.isTemporarilyDisabled && level.time < data.cooldownEndsAt)
			continue;

		// Only check filter if point isn't disabled
		if (filter(spawnPoint) && availableCount < MAX_SPAWN_POINTS) {
			availableSpawns[availableCount++] = spawnPoint;
		}
	}

	if (availableCount == 0)
		return nullptr;

	// Try spawn points in random order until we find a valid one
	const int maxAttempts = std::min(3, availableCount);
	for (int attempts = 0; attempts < maxAttempts; attempts++) {
		// Pick a random index
		const size_t idx = irandom(availableCount);
		edict_t* chosen = availableSpawns[idx];

		// If spawn point is occupied, increase attempts and try another
		if (IsSpawnPointOccupied(chosen)) {
			IncreaseSpawnAttempts(chosen);

			// Remove this point from available spawns and try another
			availableSpawns[idx] = availableSpawns[--availableCount];
			if (availableCount == 0)
				break;

			continue;
		}

		return chosen; // Found a valid spawn point
	}

	return nullptr; // No valid spawn points found
}

// Spawn point selection filter
struct SpawnMonsterFilter {
	gtime_t currentTime;

	SpawnMonsterFilter(gtime_t time) : currentTime(time) {}

	bool operator()(edict_t* spawnPoint) const {
		if (!spawnPoint || !spawnPoint->inuse)
			return false;

		// Direct array access for cooldown check
		const auto& data = spawnPointsData[spawnPoint];
		if (data.isTemporarilyDisabled && currentTime < data.cooldownEndsAt)
			return false;

		// Quick proximity check to players
		const vec3_t& origin = spawnPoint->s.origin;
		if (!is_valid_vector(origin))
			return false;

		for (const auto* const player : active_players()) {
			if (!player || !player->inuse)
				continue;

			if ((origin - player->s.origin).length() < 150.0f)
				return false;
		}

		return !IsSpawnPointOccupied(spawnPoint);
	}
};

// Definir tamaños máximos para arrays estáticos
constexpr size_t MAX_ELIGIBLE_BOSSES = 16;
constexpr size_t MAX_RECENT_BOSSES = 4;

static constexpr size_t NUM_WAVE_SOUNDS = 12;
static constexpr size_t NUM_START_SOUNDS = 8;

//precache//
// Arrays estáticos de cached_soundindex
static cached_soundindex wave_sounds[NUM_WAVE_SOUNDS];
static cached_soundindex start_sounds[NUM_START_SOUNDS];
static cached_soundindex sound_tele3;      // Para teleport
static cached_soundindex sound_tele_up;     // Para teleport escape
static cached_soundindex sound_spawn1;      // Para spawn de monstruos
static cached_soundindex incoming;      // Para spawn de monstruos

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

int32_t g_adjusted_monster_cap = 0;
constexpr int8_t MAX_MONSTERS_BIG_MAP = 32;
constexpr int8_t MAX_MONSTERS_MEDIUM_MAP = 16;
constexpr int8_t MAX_MONSTERS_SMALL_MAP = 14;

bool allowWaveAdvance = false; // Global variable to control wave advancement

bool boss_spawned_for_wave = false; // to avoid boss spamming

//bool flying_monsters_mode = false;  // flying wave
bool melee_monsters_mode = false;   // For RedMutant waves
bool small_monsters_mode = false;   // For Widow waves

int16_t last_wave_number = 0;              // Reducido de uint64_t
uint16_t g_totalMonstersInWave = 0;         // Reducido de uint32_t

gtime_t horde_message_end_time = 0_sec;
gtime_t SPAWN_POINT_COOLDOWN = 2.8_sec; //spawns Cooldown 

// Function to check and reduce spawn cooldowns when few monsters remain
void CheckAndReduceSpawnCooldowns() {
	// Only proceed if fewer than 7 stroggs remain and not in a boss wave
	const int32_t remaining_stroggs = GetStroggsNum();
	if (remaining_stroggs > 6 || IsBossWave()) {
		return;
	}

	// Track if we found any valid cooldowns to reset
	bool found_cooldowns_to_reset = false;
	const gtime_t current_time = level.time;

	// Pre-compute the reduction factor once
	constexpr float REDUCTION_FACTOR = 0.15f;

	// Process all spawn points in use
	// We need to track the spawn points to process
	std::vector<edict_t*> spawn_points;
	spawn_points.reserve(MAX_SPAWN_POINTS);

	// Find all active spawn points by scanning entities
	for (unsigned int i = 1; i < globals.num_edicts; i++) {
		edict_t* ent = &g_edicts[i];
		if (ent && ent->inuse && ent->classname &&
			!strcmp(ent->classname, "info_player_deathmatch")) {
			spawn_points.push_back(ent);
		}
	}

	// Process spawn points with early termination after collecting
	for (edict_t* spawn_point : spawn_points) {
		auto& data = spawnPointsData[spawn_point];

		// Check if spawn point is disabled and cooldown is still active
		if (data.isTemporarilyDisabled && current_time < data.cooldownEndsAt) {
			// Found at least one cooldown to reset
			found_cooldowns_to_reset = true;

			// Calculate new cooldown directly
			const gtime_t remaining_time = data.cooldownEndsAt - current_time;
			data.cooldownEndsAt = current_time + (remaining_time * REDUCTION_FACTOR);

			// Reset attempt counter for fresh spawning
			data.attempts = 0;
		}
	}

	// Only reduce global cooldown if we actually found cooldowns to reset
	if (found_cooldowns_to_reset) {
		SPAWN_POINT_COOLDOWN *= REDUCTION_FACTOR;

		if (developer->integer > 1) {
			gi.Com_PrintFmt("Global spawn cooldown reduced to {:.2f}s\n", SPAWN_POINT_COOLDOWN.seconds());
		}
	}
}

static constexpr gtime_t GetBaseSpawnCooldown(bool isSmallMap, bool isBigMap) {
	if (isSmallMap)
		return 0.2_sec;  // Reduced from 0.3 to 0.2
	else if (isBigMap)
		return 1.4_sec;  // Reduced from 1.8 to 1.4
	else
		return 0.8_sec;  // Reduced from 1.0 to 0.8
}

static float CalculateCooldownScale(int32_t lvl, const MapSize& mapSize) {
	// Early return for low levels - improves branch prediction
	if (lvl <= 10) {
		return 1.0f;
	}

	// Cache player count - only compute once
	const int32_t numHumanPlayers = GetNumHumanPlayers();

	// Compute base scale - linear scaling with level
	float scale = 1.0f + (lvl * 0.02f);

	// Compute player reduction factor once
	float playerReduction = 0.0f;
	if (numHumanPlayers > 1) {
		constexpr float PLAYER_REDUCTION = 0.1f;
		constexpr float MAX_REDUCTION = 0.45f;
		playerReduction = std::min((numHumanPlayers - 1) * PLAYER_REDUCTION, MAX_REDUCTION);
		scale *= (1.0f - playerReduction);
	}

	// Determine map multipliers with fewer branches
	float multiplier, maxScale;

	if (mapSize.isSmallMap) {
		multiplier = 0.7f;
		maxScale = 1.3f;
	}
	else if (mapSize.isBigMap) {
		multiplier = 0.85f;
		maxScale = 1.75f;
	}
	else { // Medium map
		multiplier = 0.8f;
		maxScale = 1.5f;
	}

	// Apply map multiplier and clamp to max scale
	return std::min(scale * multiplier, maxScale);
}
cvar_t* g_horde;

// Monster wave type flags
enum class MonsterWaveType : uint32_t {
	None = 0,
	Flying = 1 << 0,  // Flying units
	Ground = 1 << 1,  // Basic ground units
	Small = 1 << 2,  // Small units (parasite, stalker)
	Light = 1 << 3,  // Light units (soldiers, basic infantry)
	Heavy = 1 << 4,  // Heavy units (tanks, enforcers)
	Medium = 1 << 5,  // Medium units (gladiators, medics)
	Fast = 1 << 6,  // Fast moving units
	SemiBoss = 1 << 7,  // Mini-boss tier units
	Boss = 1 << 8,  // Full boss units
	Ranged = 1 << 9, // Primarily ranged attackers
	Melee = 1 << 10, // Primarily melee attackers
	Special = 1 << 11, // Special units (medics, commanders)
	Elite = 1 << 12,  // Elite variants of basic units
	Gekk = 1 << 13,  // Gekk initial wave?
	Shambler = 1 << 14,  // Shambler boss wave?
	Mutant = 1 << 15, // Mutant boss wave?
	Arachnophobic = 1 << 16,  // Mutant boss wave?
	Berserk = 1 << 17,  // Berserk  wave
	Bomber = 1 << 18,  // Grenade users wave?
	Spawner = 1 << 19  // Spawning reinforcements users wave
};

MonsterWaveType current_wave_type = MonsterWaveType::None;


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

	// Failsafe timeout to prevent getting stuck in a state
	gtime_t         failsafe_timeout = 0_sec;

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
template <typename T>
struct WeightedSelection {
	static constexpr size_t MAX_ITEMS = 32;

	struct ItemEntry {
		const T* item;
		float weight;
	};

	// Pre-allocate fixed array instead of using dynamic memory
	ItemEntry items[MAX_ITEMS];
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

		// Generate uniform random value once
		const float random_weight = frandom() * total_weight;

		// Use linear search - more efficient for small arrays than binary search
		float cumulative = 0.0f;
		for (size_t i = 0; i < item_count; i++) {
			cumulative += items[i].weight;
			if (cumulative >= random_weight)
				return items[i].item;
		}

		// Fallback to last item
		return items[item_count - 1].item;
	}

	// More efficient range selection
	const T* select_range(float min_weight, float max_weight) const {
		if (item_count == 0 || total_weight <= 0.0f)
			return nullptr;

		// Stack-allocated array for eligible items to avoid heap allocation
		ItemEntry eligible_items[MAX_ITEMS]{};
		size_t eligible_count = 0;
		float eligible_total = 0.0f;

		// Filter by weight range
		for (size_t i = 0; i < item_count; i++) {
			if (items[i].weight >= min_weight && items[i].weight <= max_weight) {
				eligible_items[eligible_count++] = items[i];
				eligible_total += items[i].weight;
			}
		}

		if (eligible_count == 0)
			return nullptr;

		// Generate random value once
		const float random_weight = frandom() * eligible_total;

		// Linear search through eligible items
		float cumulative = 0.0f;
		for (size_t i = 0; i < eligible_count; i++) {
			cumulative += eligible_items[i].weight;
			if (cumulative >= random_weight)
				return eligible_items[i].item;
		}

		return eligible_items[eligible_count - 1].item;
	}
};
int16_t current_wave_level = g_horde_local.level;
bool next_wave_message_sent = false;
auto auto_spawned_bosses = std::unordered_set<edict_t*>{};
auto lastMonsterSpawnTime = std::unordered_map<std::string, gtime_t>{};
auto lastSpawnPointTime = std::unordered_map<edict_t*, gtime_t>{};

const std::unordered_set<std::string> smallMaps = {
	"q2dm3", "q2dm7", "q2dm2", "q64/dm10", "test/mals_barrier_test",
	"q64/dm9", "q64/dm7", "q64/dm2", "test/spbox",
	"q64/dm1", "fact3", "q2ctf4", "rdm4", "q64/command","mgu3m4",
	"mgu4trial", "mgu6trial", "ec/base_ec", "mgdm1", "ndctf0", "q64/dm6",
	"q64/dm8", "q64/dm4", "q64/dm3", "industry", "e3/jail_e3"
};

const std::unordered_set<std::string> bigMaps = {
	"q2ctf5", "old/kmdm3", "xdm2", "xdm4", "xdm6", "xdm3", "rdm6", "rdm8", "xdm1", "waste2", "rdm5", "rdm9", "rdm12", "xintell", "sewer64", "base64", "city64"
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
	// Use adjusted cap instead of fixed values
	int32_t maxAllowed = g_adjusted_monster_cap;

	// If g_adjusted_monster_cap hasn't been initialized yet, use defaults
	if (maxAllowed == 0) {
		maxAllowed = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
			(mapSize.isBigMap ? MAX_MONSTERS_BIG_MAP : MAX_MONSTERS_MEDIUM_MAP);
	}

	// Ajuste dinámico basado en jugadores activos
	const int32_t activePlayers = GetNumHumanPlayers();
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

void UnifiedAdjustSpawnRate(const MapSize& mapSize, int32_t lvl, int32_t humanPlayers) noexcept {
	using namespace HordeConstants;

	// Base count determination using explicit conditions
	int32_t baseCount;
	if (mapSize.isSmallMap) {
		if (lvl <= 5) baseCount = BASE_COUNTS[0][0];
		else if (lvl <= 10) baseCount = BASE_COUNTS[0][1];
		else if (lvl <= 15) baseCount = BASE_COUNTS[0][2];
		else baseCount = BASE_COUNTS[0][3];
	}
	else if (mapSize.isBigMap) {
		if (lvl <= 5) baseCount = BASE_COUNTS[2][0];
		else if (lvl <= 10) baseCount = BASE_COUNTS[2][1];
		else if (lvl <= 15) baseCount = BASE_COUNTS[2][2];
		else baseCount = BASE_COUNTS[2][3];
	}
	else {
		if (lvl <= 5) baseCount = BASE_COUNTS[1][0];
		else if (lvl <= 10) baseCount = BASE_COUNTS[1][1];
		else if (lvl <= 15) baseCount = BASE_COUNTS[1][2];
		else baseCount = BASE_COUNTS[1][3];
	}

	// Player count adjustment
	if (humanPlayers > 1) {
		// Use int64_t for the scaled result to prevent overflow
		int64_t scaledBaseCount = static_cast<int64_t>(baseCount) *
			(BASE_DIFFICULTY_MULTIPLIER + ((humanPlayers - 1) * PLAYER_COUNT_SCALE));

		// Clamp the scaledBaseCount to prevent exceeding int32_t max
		scaledBaseCount = std::min(scaledBaseCount, static_cast<int64_t>(std::numeric_limits<int32_t>::max()));

		// Assign back to baseCount safely
		baseCount = static_cast<int32_t>(scaledBaseCount);
	}

	// Additional spawn calculation
	int32_t additionalSpawn;
	if (lvl >= 8) {
		additionalSpawn = mapSize.isSmallMap ? ADDITIONAL_SPAWNS[0] :
			mapSize.isBigMap ? ADDITIONAL_SPAWNS[2] :
			ADDITIONAL_SPAWNS[1];
	}
	else {
		additionalSpawn = 6;
	}

	// Cooldown calculation
	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);
	const float cooldownScale = CalculateCooldownScale(lvl, mapSize);
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	// Level-based adjustments
	if (lvl > 25) {
		additionalSpawn = static_cast<int32_t>(additionalSpawn * 1.6f);
	}

	// Difficulty adjustments
	if (lvl >= 3 && (g_chaotic->integer || g_insane->integer)) {
		additionalSpawn += CalculateChaosInsanityBonus(lvl);
		SPAWN_POINT_COOLDOWN *= TIME_REDUCTION_MULTIPLIER;
	}

	// Player count difficulty scaling
	const float difficultyMultiplier = BASE_DIFFICULTY_MULTIPLIER + (humanPlayers - 1) * DIFFICULTY_PLAYER_FACTOR;
	if (lvl % 3 == 0) {
		// To prevent baseCount from exceeding the maximum value of int32_t after multiplication,
		// use int64_t to hold the temporary result
		int64_t tempBaseCount = static_cast<int64_t>(baseCount) * difficultyMultiplier;

		// Ensure we don't exceed the maximum value of int32_t
		tempBaseCount = std::min(tempBaseCount, static_cast<int64_t>(std::numeric_limits<int32_t>::max()));

		// Assign the safely scaled value back to baseCount
		baseCount = static_cast<int32_t>(tempBaseCount);
		SPAWN_POINT_COOLDOWN = std::max(
			SPAWN_POINT_COOLDOWN - gtime_t::from_sec((mapSize.isBigMap ? 0.1f : 0.15f) * difficultyMultiplier),
			1.0_sec
		);
	}

	// Final cooldown clamping
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN, 1.0_sec, 3.0_sec);

	// Calculate num_to_spawn: Clamping the result after addition
	g_horde_local.num_to_spawn = baseCount + additionalSpawn;
	ClampNumToSpawn(mapSize); //  <---- IMPORTANT - This already handles clamping

	const bool isHardMode = g_insane->integer || g_chaotic->integer;
	g_horde_local.queued_monsters = CalculateQueuedMonsters(mapSize, lvl, isHardMode);

	// Debug output
	if (developer->integer == 3) {
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
		aggressiveTimeReductionThreshold(0.3f) {
	}
};

// Constantes y funciones auxiliares
constexpr gtime_t BASE_MAX_WAVE_TIME = 85_sec;
constexpr gtime_t TIME_INCREASE_PER_LEVEL = 1.5_sec;
constexpr gtime_t BOSS_TIME_BONUS = 60_sec;
constexpr int MONSTERS_FOR_AGGRESSIVE_REDUCTION = 3;
constexpr gtime_t AGGRESSIVE_TIME_REDUCTION_PER_MONSTER = 1.5_sec;

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
static gtime_t g_independent_timer_start;
static ConditionParams g_lastParams;
static int32_t g_lastWaveNumber = -1;
static int32_t g_lastNumHumanPlayers = -1;
static bool g_maxMonstersReached = false;
static bool g_lowPercentageTriggered = false;

static ConditionParams GetConditionParams(const MapSize& mapSize, int32_t numHumanPlayers, int32_t lvl) {
	ConditionParams params;

	// Validación inicial
	if (g_horde_local.level < 0 || lvl < 0) {
		return params; // Retorna parámetros por defecto seguros
	}

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
	params.maxMonsters += std::min(lvl / 4, 8);
	params.timeThreshold += gtime_t::from_ms(75ll * std::min(lvl / 3, 4));

	// Early wave time extension - NEW CODE
	float timeMultiplier = 1.0f;
	if (lvl <= 10) {
		// Start with 1.8x longer at wave 1, gradually reduce to 1.0x at wave 10
		timeMultiplier = 1.8f - ((lvl - 1) * 0.08f);
	}

	// Ajuste para niveles altos
	if (lvl > 10) {
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * 1.2f);
		params.timeThreshold += 0.15_sec;
	}

	// Ajuste basado en dificultad
	if (g_chaotic->integer || g_insane->integer) {
		if (numHumanPlayers <= 3) {
			params.timeThreshold += random_time(5_sec, 8_sec);
		}
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * 1.1f);
	}

	// Configuración para el porcentaje bajo de monstruos restantes
	params.lowPercentageTimeThreshold = random_time(8_sec, 17_sec);
	params.lowPercentageThreshold = 0.3f;

	// Configuración para tiempo independiente basado en el nivel
	params.independentTimeThreshold = calculate_max_wave_time(lvl);

	// Apply multiplier to time thresholds for early waves - NEW CODE
	if (lvl <= 10) {
		params.timeThreshold = gtime_t::from_sec(params.timeThreshold.seconds() * timeMultiplier);
		params.lowPercentageTimeThreshold = gtime_t::from_sec(params.lowPercentageTimeThreshold.seconds() * timeMultiplier);
		params.independentTimeThreshold = gtime_t::from_sec(params.independentTimeThreshold.seconds() * timeMultiplier);
	}

	// Validación final de parámetros
	params.maxMonsters = std::max(1, params.maxMonsters);
	params.timeThreshold = std::max(1_sec, params.timeThreshold);
	params.lowPercentageTimeThreshold = std::max(1_sec, params.lowPercentageTimeThreshold);
	params.independentTimeThreshold = std::max(1_sec, params.independentTimeThreshold);

	return params;
}
// Warning times in seconds
constexpr std::array<float, 3> WARNING_TIMES = { 30.0f, 10.0f, 5.0f };

static void InitializeWaveType(int32_t lvl);
inline int32_t GetAdjustedMonsterCap(const MapSize& mapSize, int32_t waveLevel);

static void Horde_InitLevel(const int32_t lvl) {

	//CleanupStaleCS();

	// Only initialize wave type for non-boss waves
	if (!(lvl >= 10 && lvl % 5 == 0)) {
		InitializeWaveType(lvl);
	}
	else {
		current_wave_type = MonsterWaveType::None;  // Reset for boss waves
	}

	g_horde_local.update_map_size(GetCurrentMapName());

	// Calculate adjusted monster cap for this wave
	GetAdjustedMonsterCap(g_horde_local.current_map_size, lvl);

	// Reset independent timer for this wave - this is critical for correct wave timing
	g_independent_timer_start = level.time;

	// Configuración de variables iniciales para el nivel
	// Verify that we're not somehow about to overflow
	if (g_horde_local.num_to_spawn > std::numeric_limits<uint16_t>::max())
		g_horde_local.num_to_spawn = std::numeric_limits<uint16_t>::max();

	g_totalMonstersInWave = static_cast<uint16_t>(g_horde_local.num_to_spawn);

	last_wave_number++;
	g_horde_local.level = lvl;
	current_wave_level = lvl;
	//current_wave_type = MonsterWaveType::None;
	boss_spawned_for_wave = false;
	next_wave_message_sent = false;

	CleanupSpawnPointCache();
	VerifyAndAdjustBots();

	// Configurar tiempos iniciales - reset all timing variables
	g_independent_timer_start = level.time;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTriggered = false;
	g_horde_local.waveEndTime = 0_sec;
	g_horde_local.lastPrintTime = 0_sec;

	// Calculate fresh parameters for this wave - this fixes using stale parameters
	g_lastParams = GetConditionParams(g_horde_local.current_map_size, GetNumHumanPlayers(), lvl);

	// Reset warning flags for time warnings
	for (size_t i = 0; i < 4; i++) {
		g_horde_local.warningIssued[i] = false;
	}

	if (developer->integer) {
		gi.Com_PrintFmt("Debug: Wave {} init - Timer threshold: {:.2f}s\n",
			lvl, g_lastParams.timeThreshold.seconds());
	}

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
		gi.cvar_set("g_damage_scale", "4.5");
		break;
	default:
		break;
	}

	UnifiedAdjustSpawnRate(g_horde_local.current_map_size, lvl, GetNumHumanPlayers());

	CheckAndApplyBenefit(lvl);
	ResetAllSpawnAttempts();
	ResetCooldowns();
	Horde_CleanBodies();

	//gi.Com_PrintFmt("PRINT: Horde level initialized: {}\n", lvl);
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
	uint32_t flags = 0;
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
	//{ "item_power_shield", 14, -1, 0.07f, adjust_weight_armor },

	{ "item_ir_goggles", 10, -1, 0.03f, adjust_weight_powerup },
	{ "item_quad", 6, -1, 0.04f, adjust_weight_powerup },
	{ "item_double", 4, -1, 0.05f, adjust_weight_powerup },
	{ "item_quadfire", 2, -1, 0.04f, adjust_weight_powerup },
	{ "item_invulnerability", 4, -1, 0.03f, adjust_weight_powerup },
	{ "item_sphere_defender", 2, -1, 0.05f, adjust_weight_powerup },
	//{ "item_sphere_vengeance", 23, -1, 0.06f, adjust_weight_powerup },
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
	{ "weapon_boomer", 17, -1, 0.17f, adjust_weight_weapon },
	{ "weapon_hyperblaster", 12, -1, 0.2f, adjust_weight_weapon },
	{ "weapon_rocketlauncher", 14, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_railgun", 24, -1, 0.17f, adjust_weight_weapon },
	{ "weapon_phalanx", 20, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_plasmabeam", 16, -1, 0.25f, adjust_weight_weapon },
	{ "weapon_disintegrator", 31, -1, 0.15f, adjust_weight_weapon },
	{ "weapon_bfg", 26, -1, 0.17f, adjust_weight_weapon },

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



// Allow flag operations on MonsterWaveType
constexpr MonsterWaveType operator|(MonsterWaveType a, MonsterWaveType b) {
	return static_cast<MonsterWaveType>(
		static_cast<std::underlying_type_t<MonsterWaveType>>(a) |
		static_cast<std::underlying_type_t<MonsterWaveType>>(b)
		);
}

inline MonsterWaveType operator& (MonsterWaveType a, MonsterWaveType b) noexcept {
	return static_cast<MonsterWaveType>(
		static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
		);
}

inline MonsterWaveType& operator|=(MonsterWaveType& a, MonsterWaveType b) {
	a = a | b;
	return a;
}

// Helper function to check if a monster has a specific wave type
inline bool HasWaveType(MonsterWaveType entityTypes, MonsterWaveType typeToCheck) {
	return static_cast<uint32_t>(entityTypes & typeToCheck) != 0;
}



// First, add these at the top with other global variables
static constexpr size_t WAVE_MEMORY_SIZE = 3;  // Remember last 3 waves
static std::array<MonsterWaveType, WAVE_MEMORY_SIZE> previous_wave_types = {};
static size_t wave_memory_index = 0;


// Helper function to check if a wave type was recently used
static bool WasRecentlyUsed(MonsterWaveType wave_type) noexcept {
	for (const auto& prev_type : previous_wave_types) {
		if (prev_type == wave_type) {
			return true;
		}
	}
	return false;
}

// Helper function to store wave type in memory
static void StoreWaveType(MonsterWaveType wave_type) noexcept {
	previous_wave_types[wave_memory_index] = wave_type;
	wave_memory_index = (wave_memory_index + 1) % WAVE_MEMORY_SIZE;
}

// Helper function to try setting a wave type with validation
static bool TrySetWaveType(MonsterWaveType new_type) {
	// Special case for boss waves - allow flying waves to override the recent wave restriction
	if (HasWaveType(new_type, MonsterWaveType::Flying | MonsterWaveType::Boss)) {
		// If this is a flying boss, we always allow flying waves
		current_wave_type = new_type;
		StoreWaveType(new_type);
		return true;
	}

	// Make flying waves more restrictive for non-boss waves
	if (HasWaveType(new_type, MonsterWaveType::Flying) && !HasWaveType(new_type, MonsterWaveType::Boss)) {
		// Check if any of the recent waves were flying
		for (const auto& prev_type : previous_wave_types) {
			if (HasWaveType(prev_type, MonsterWaveType::Flying)) {
				return false;  // Don't allow flying waves if we had one recently
			}
		}

		// Also check if the previous wave was a boss wave
		if (g_horde_local.level > 0 && (g_horde_local.level - 1) >= 10 &&
			(g_horde_local.level - 1) % 5 == 0) {
			return false;  // Don't allow flying waves right after boss waves
		}
	}

	// Check if the wave type was recently used
	if (!WasRecentlyUsed(new_type)) {
		current_wave_type = new_type;
		StoreWaveType(new_type);
		return true;
	}

	// Fallback for specific wave types
	if (HasWaveType(new_type, MonsterWaveType::Mutant) || HasWaveType(new_type, MonsterWaveType::Shambler)) {
		// Fallback for mutant/shambler types
		current_wave_type = MonsterWaveType::Medium;
		StoreWaveType(MonsterWaveType::Medium);
		return true;
	}
	else if (HasWaveType(new_type, MonsterWaveType::Flying)) {
		// Fallback for flying types
		current_wave_type = MonsterWaveType::Flying | MonsterWaveType::Medium;
		StoreWaveType(current_wave_type);
		return true;
	}
	else if (HasWaveType(new_type, MonsterWaveType::Arachnophobic)) {
		// Fallback for arachnophobic types
		current_wave_type = MonsterWaveType::Small | MonsterWaveType::Arachnophobic;
		StoreWaveType(current_wave_type);
		return true;
	}

	return false;
}

inline MonsterWaveType GetWaveComposition(int waveNumber, bool forceSpecialWave = false) {
	const int32_t numHumanPlayers = GetNumHumanPlayers();
	MonsterWaveType selected_type = MonsterWaveType::None;

	// Special waves check - removed the wave 5-9 restriction
	struct SpecialWave {
		MonsterWaveType type;
		float chance;
		int min_wave;
		int max_wave;
		const char* message;
	};

	// Define special waves that can occur throughout the game
	const SpecialWave special_waves[] = {
		// Early game special waves (waves 5-15)
		{MonsterWaveType::Gekk, (numHumanPlayers <= 2 ? 0.35f : 0.20f), 5, 7, "*** Gekk invasion incoming! ***\n"},
		{MonsterWaveType::Mutant | MonsterWaveType::Melee, 0.30f, 8, 25, "*** Enraged Horde approaching! ***\n"},
		{MonsterWaveType::Flying | MonsterWaveType::Fast, 0.2f, 9, -1, "*** Aerial assault incoming! ***\n"},

		// Mid game special waves (waves 8+)
		{MonsterWaveType::Berserk, 0.2f, 8, 12, "*** Berserkers incoming! ***\n"},
		{MonsterWaveType::Bomber, 0.35f, 10, -1, "*** Strogg Bomber Units Arrived! ***\n"},

		// Late game special waves
		{MonsterWaveType::Heavy, 0.2f, 12, -1, "*** Heavy Armored Units incoming! ***\n"},
		{MonsterWaveType::Spawner, 0.75f, 25, -1, "*** Spawners Deployed! ***\n"}
	};

	// Try special waves first regardless of wave number
	if (!forceSpecialWave) {
		for (const auto& wave : special_waves) {
			// Check if wave is eligible (within min/max range)
			if (waveNumber >= wave.min_wave &&
				(wave.max_wave == -1 || waveNumber <= wave.max_wave) &&
				!WasRecentlyUsed(wave.type) &&
				frandom() < wave.chance) {
				selected_type = wave.type;
				gi.LocBroadcast_Print(PRINT_HIGH, wave.message);
				StoreWaveType(selected_type);
				return selected_type;
			}
		}
	}

	// If no special wave was selected, fall back to regular wave composition
	// [Rest of the existing wave composition code remains the same]
	struct WaveComposition {
		MonsterWaveType base_type;
		MonsterWaveType optional_type;
		float optional_chance;
	};
	const WaveComposition wave_types[] = {
		// Waves 1-5
		{MonsterWaveType::Light | MonsterWaveType::Ground, MonsterWaveType::None, 0.0f},

		// Waves 6-10
		{MonsterWaveType::Light | MonsterWaveType::Ground,
		 MonsterWaveType::Small, 0.4f},

		 // Waves 11-15
		 {MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ground,
		  MonsterWaveType::Special, 0.6f},

		  // Waves 16-20
		  {MonsterWaveType::Medium | MonsterWaveType::Heavy | MonsterWaveType::Ground,
		   MonsterWaveType::Fast, 0.5f},

		   // Waves 21-25
		   {MonsterWaveType::Heavy | MonsterWaveType::Elite,
			MonsterWaveType::Special | MonsterWaveType::Fast, 0.7f},

			// Waves 26-30
			{MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Special,
			 MonsterWaveType::SemiBoss, 0.4f},

			 // Waves 31+
			 {MonsterWaveType::Elite | MonsterWaveType::Heavy | MonsterWaveType::Special,
			  MonsterWaveType::SemiBoss, 0.6f}
	};

	// Select appropriate wave composition based on wave number
	const WaveComposition* comp;
	if (waveNumber <= 5) comp = &wave_types[0];
	else if (waveNumber <= 10) comp = &wave_types[1];
	else if (waveNumber <= 15) comp = &wave_types[2];
	else if (waveNumber <= 20) comp = &wave_types[3];
	else if (waveNumber <= 25) comp = &wave_types[4];
	else if (waveNumber <= 30) comp = &wave_types[5];
	else comp = &wave_types[6];

	// Build wave type with base + optional components
	selected_type = comp->base_type;
	if (frandom() < comp->optional_chance && !WasRecentlyUsed(comp->optional_type)) {
		selected_type = selected_type | comp->optional_type;
	}

	// Special case for flying waves
	if (waveNumber > 5 && frandom() < 0.30f && !WasRecentlyUsed(MonsterWaveType::Flying)) {
		selected_type = selected_type | MonsterWaveType::Flying;
		gi.sound(world, CHAN_VOICE, incoming, 1, ATTN_NONE, 0);
	}

	StoreWaveType(selected_type);
	return selected_type;
}

// Wave difficulty multiplier
//inline static float GetWaveDifficultyMultiplier(int waveNumber) noexcept {
//	if (waveNumber <= 5) return 1.0f;
//	if (waveNumber <= 10) return 1.2f;
//	if (waveNumber <= 15) return 1.4f;
//	if (waveNumber <= 20) return 1.6f;
//	if (waveNumber <= 25) return 1.8f;
//	if (waveNumber <= 30) return 2.0f;
//	return 2.0f + ((waveNumber - 30) * 0.1f);
//}
inline MonsterWaveType GetMonsterWaveTypes(const char* classname) noexcept;
// Example function to filter monsters by wave type
// First the IsValidMonsterForWave function:
inline bool IsValidMonsterForWave(const char* classname, MonsterWaveType waveRequirements) {
	// Fast exit for no requirements
	if (waveRequirements == MonsterWaveType::None) {
		return true;
	}

	// Get monster types once
	const MonsterWaveType monsterTypes = GetMonsterWaveTypes(classname);

	// Skip all the extra lookups and bit operations, check directly the critical flags
	const uint32_t requirements = static_cast<uint32_t>(waveRequirements);
	const uint32_t monster_flags = static_cast<uint32_t>(monsterTypes);

	// First check the most important exclusive categories that must match
	if ((requirements & static_cast<uint32_t>(MonsterWaveType::Flying)) &&
		!(monster_flags & static_cast<uint32_t>(MonsterWaveType::Flying)))
		return false;

	if ((requirements & static_cast<uint32_t>(MonsterWaveType::Small)) &&
		!(monster_flags & static_cast<uint32_t>(MonsterWaveType::Small)))
		return false;

	if ((requirements & static_cast<uint32_t>(MonsterWaveType::Arachnophobic)) &&
		!(monster_flags & static_cast<uint32_t>(MonsterWaveType::Arachnophobic)))
		return false;

	if ((requirements & static_cast<uint32_t>(MonsterWaveType::Heavy)) &&
		!(monster_flags & static_cast<uint32_t>(MonsterWaveType::Heavy)))
		return false;

	if ((requirements & static_cast<uint32_t>(MonsterWaveType::Mutant)) &&
		!(monster_flags & static_cast<uint32_t>(MonsterWaveType::Mutant)))
		return false;

	if ((requirements & static_cast<uint32_t>(MonsterWaveType::Shambler)) &&
		!(monster_flags & static_cast<uint32_t>(MonsterWaveType::Shambler)))
		return false;

	if ((requirements & static_cast<uint32_t>(MonsterWaveType::Melee)) &&
		!(monster_flags & static_cast<uint32_t>(MonsterWaveType::Melee)))
		return false;

	if ((requirements & static_cast<uint32_t>(MonsterWaveType::Berserk)) &&
		!(monster_flags & static_cast<uint32_t>(MonsterWaveType::Berserk)))
		return false;

	if ((requirements & static_cast<uint32_t>(MonsterWaveType::Bomber)) &&
		!(monster_flags & static_cast<uint32_t>(MonsterWaveType::Bomber)))
		return false;

	if ((requirements & static_cast<uint32_t>(MonsterWaveType::Spawner)) &&
		!(monster_flags & static_cast<uint32_t>(MonsterWaveType::Spawner)))
		return false;

	// For mixed waves, check if there's at least one match in other categories
	return (requirements & monster_flags) != 0;
}

// Structure to include wave level information
struct MonsterTypeInfo {
	const char* classname;
	MonsterWaveType types;
	int minWave;
	float weight;

	// Add a constexpr constructor
	constexpr MonsterTypeInfo(const char* n, MonsterWaveType t, int w, float wt)
		: classname(n), types(t), minWave(w), weight(wt) {
	}
};

static const MonsterTypeInfo monsterTypes[] = {
	// Basic Infantry (Waves 1-5)
	{"monster_soldier_light", MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 1.0f},
	{"monster_soldier", MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 0.9f},
	{"monster_soldier_ss", MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 2, 0.8f},
	{"monster_infantry_vanilla", MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 3, 0.85f},

	// Early Flying Units (Waves 1-8)
	{"monster_flyer", MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Fast, 1, 0.7f},
	{"monster_hover_vanilla", MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Ranged, 8, 0.6f},
	{"monster_floater", MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Ranged, 12, 0.5f},

	// Special Wave Units (Waves 4-9)
	{"monster_gekk", MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Small | MonsterWaveType::Mutant | MonsterWaveType::Gekk, 4, 0.7f},
	{"monster_parasite", MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Melee, 5, 0.6f},
	{"monster_stalker", MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Fast | MonsterWaveType::Arachnophobic, 7, 0.6f},
	{"monster_brain", MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special | MonsterWaveType::Melee | MonsterWaveType::Mutant, 6, 0.7f},

	// Elite Infantry (Waves 4-12)
	{"monster_soldier_hypergun", MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Elite | MonsterWaveType::Ranged, 4, 0.7f},
	{"monster_soldier_ripper", MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Elite | MonsterWaveType::Ranged, 7, 0.8f},
	{"monster_soldier_lasergun", MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Elite | MonsterWaveType::Ranged, 10, 0.8f},
	{"monster_chick", MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 7, 0.6f},

	// Medium Units (Waves 7-12)
	{"monster_gunner_vanilla", MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 8, 0.8f},
	{"monster_infantry", MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Heavy | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 11, 0.85f},
	{"monster_medic", MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special, 7, 0.5f},
	{"monster_berserk", MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Melee | MonsterWaveType::Berserk , 6, 0.8f},

	// Arachnophobic Units (Waves 8-18)
	{"monster_spider", MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 8, 0.1f},
	{"monster_guncmdr_vanilla", MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 12, 0.4f},
	{"monster_arachnid2", MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 18, 0.4f},
	{"monster_gm_arachnid", MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Heavy | MonsterWaveType::Elite, 15, 0.45f},
	{"monster_psxarachnid", MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite | MonsterWaveType::Spawner, 25, 0.35f},

	// Mutant Units (Waves 9-14)
	{"monster_mutant", MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 9, 0.7f},
	{"monster_shambler_small", MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Mutant | MonsterWaveType::Shambler, 14, 0.4f},
	{"monster_redmutant", MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Elite | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 14, 0.35f},

	// Heavy Ground Units (Waves 12-18)
	{"monster_gladiator", MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged, 12, 0.7f},
	{"monster_gunner", MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 12, 0.8f},
	{"monster_tank_spawner", MonsterWaveType::Ground | MonsterWaveType::Spawner | MonsterWaveType::Heavy | MonsterWaveType::Medium, 13, 0.6f},
	{"monster_tank", MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 14, 0.4f},
	{"monster_tank_commander", MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 16, 0.5f},
	{"monster_guncmdr", MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Bomber, 15, 0.7f},
	{"monster_runnertank", MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Fast, 21, 0.5f},
	{"monster_chick_heat", MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Fast, 13, 0.6f},

	// Elite Flying Units (Waves 18-27)
	{"monster_hover", MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 18, 0.5f},
	{"monster_daedalus", MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 21, 0.4f},
	{"monster_floater_tracker", MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 27, 0.45f},
	{"monster_daedalus_bomber", MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite | MonsterWaveType::Bomber, 24, 0.35f},

	// Elite Ground Units (Waves 18+)
	{"monster_gladb", MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f},
	{"monster_gladc", MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f},
	{"monster_shambler", MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 22, 0.4f},
	{"monster_tank_64", MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 28, 0.3f},

	// Special Heavy Units (Waves 20+)
	{"monster_janitor", MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Bomber, 21, 0.5f},
	{"monster_janitor2", MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Special | MonsterWaveType::Bomber, 26, 0.4f},
	{"monster_medic_commander", MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.3f},

	// Semi-Boss Units (Waves 16+)
	{"monster_makron", MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 23, 0.02f},
	{"monster_perrokl", MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Fast | MonsterWaveType::Small, 20, 0.4f},
	{"monster_widow1", MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 29, 0.3f},
	{"monster_shamblerkl", MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 33, 0.23f},
	{"monster_guncmdrkl", MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 33, 0.2f},
	{"monster_makronkl", MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Elite, 41, 0.2f},
	{"monster_boss2kl", MonsterWaveType::Flying | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 46, 0.2f},
	{"monster_jorg_small", MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Medium, 33, 0.2f},

	// Boss Units
	{"monster_boss2_64", MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 19, 0.2f},
	{"monster_boss2_mini", MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 19, 0.2f},
	{"monster_carrier_mini", MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy | MonsterWaveType::Spawner, 27, 0.2f}
};

// Function to get wave types for a monster based on its classname
inline MonsterWaveType GetMonsterWaveTypes(const char* classname) noexcept {
	if (!classname) return MonsterWaveType::None;

	for (const auto& info : monsterTypes) {
		if (strcmp(classname, info.classname) == 0) {
			return info.types;
		}
	}

	return MonsterWaveType::Ground; // Default to ground type
}

static void InitializeWaveType(int32_t lvl) {
	// Only initialize wave type for non-boss waves
	if (!(lvl >= 10 && lvl % 5 == 0)) {
		current_wave_type = GetWaveComposition(lvl);
	}
	else {
		current_wave_type = MonsterWaveType::None;  // Reset for boss waves
	}
}
#include <array>
#include <unordered_set>
#include <random>


// Definición de jefes por tamaño de mapa
struct boss_t {
	const char* classname;
	int32_t min_level;
	int32_t max_level;
	float weight;
	BossSizeCategory sizeCategory; // Si decides extender la estructura
};

static constexpr boss_t BOSS_SMALL[] = {
	{"monster_carrier_mini", 24, -1, 0.1f, BossSizeCategory::Small},
	{"monster_boss2kl", 24, -1, 0.1f, BossSizeCategory::Small},
	{"monster_widow2", 19, -1, 0.1f, BossSizeCategory::Small},
	{"monster_tank_64", -1, -1, 0.25f, BossSizeCategory::Small},
	{"monster_shamblerkl", -1, 20, 0.3f, BossSizeCategory::Small},
	{"monster_guncmdrkl", -1, 20, 0.3f, BossSizeCategory::Small},
	{"monster_tank_64", 21, -1, 0.1f, BossSizeCategory::Small},
	{"monster_shamblerkl", 21, -1, 0.1f, BossSizeCategory::Small},
	{"monster_makronkl", 36, -1, 0.2f, BossSizeCategory::Small},
	{"monster_makron", 16, 26, 0.1f, BossSizeCategory::Small},
	{"monster_psxarachnid", 15, -1, 0.1f, BossSizeCategory::Small},
	{"monster_redmutant", -1, 24, 0.1f, BossSizeCategory::Small}
};

static constexpr boss_t BOSS_MEDIUM[] = {
	{"monster_carrier", 24, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_boss2", 19, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_tank_64", -1, 20, 0.45f, BossSizeCategory::Medium},
	{"monster_shamblerkl", -1, 20, 0.3f, BossSizeCategory::Medium},
	{"monster_guncmdrkl", -1, 20, 0.3f, BossSizeCategory::Medium},
	{"monster_tank_64", 21, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_shamblerkl", 21, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_guncmdrkl", 21, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_psxguardian", -1, 24, 0.1f, BossSizeCategory::Medium},
	{"monster_widow2", 19, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_psxarachnid", -14, -1, 0.1f, BossSizeCategory::Medium},
	{"monster_makronkl", 26, -1, 0.2f, BossSizeCategory::Medium},
	{"monster_makron", 16, 25, 0.1f, BossSizeCategory::Medium},
	{"monster_redmutant", -1, 24, 0.1f, BossSizeCategory::Small }
};

static constexpr boss_t BOSS_LARGE[] = {
	{"monster_carrier", 24, -1, 0.1f, BossSizeCategory::Large},
	{"monster_boss2", 19, -1, 0.1f, BossSizeCategory::Large},
	{"monster_boss5", -1, -1, 0.1f, BossSizeCategory::Large},
	{"monster_tank_64", -1, 20, 0.45f, BossSizeCategory::Large},
	{"monster_shamblerkl", -1, 20, 0.3f, BossSizeCategory::Large},
	{"monster_guncmdrkl", -1, 20, 0.3f, BossSizeCategory::Large},
	{"monster_tank_64", 21, -1, 0.1f, BossSizeCategory::Large},
	{"monster_shamblerkl", 21, -1, 0.1f, BossSizeCategory::Large},
	{"monster_guncmdrkl", 21, -1, 0.1f, BossSizeCategory::Large},
	{"monster_psxarachnid", 14, -1, 0.1f, BossSizeCategory::Large},
	{"monster_widow", -1, -1, 0.1f, BossSizeCategory::Large},
	{"monster_psxguardian", -1, -1, 0.1f, BossSizeCategory::Large},
	{"monster_boss5", -1, 24, 0.1f, BossSizeCategory::Large},
	{"monster_jorg", 30, -1, 0.15f, BossSizeCategory::Large},
	{"monster_makronkl", 30, -1, 0.2f, BossSizeCategory::Large},
	{"monster_redmutant", -1, 24, 0.1f, BossSizeCategory::Small }
};

// Modified GetBossList function using std::span
static std::span<const boss_t> GetBossList(const MapSize& mapSize, std::string_view mapname) {
	if (mapSize.isSmallMap || mapname == "q2dm4" || mapname == "q64/comm" || mapname == "test/test_kaiser") {
		return std::span<const boss_t>(BOSS_SMALL);
	}

	if (mapSize.isMediumMap || mapname == "rdm8" || mapname == "xdm1") {
		if (mapname == "mgu6m3" || mapname == "rboss") {
			static std::array<boss_t, std::size(BOSS_MEDIUM)> filteredMediumBossList;
			size_t count = 0;

			for (const auto& boss : std::span<const boss_t>(BOSS_MEDIUM)) {
				if (std::strcmp(boss.classname, "monster_guardian") != 0 &&
					std::strcmp(boss.classname, "monster_psxguardian") != 0) {
					filteredMediumBossList[count++] = boss;
				}
			}

			return std::span<const boss_t>(filteredMediumBossList.data(), count);
		}
		return std::span<const boss_t>(BOSS_MEDIUM);
	}

	if (mapSize.isBigMap || mapname == "test/spbox" || mapname == "q2ctf4") {
		if (mapname == "test/spbox" || mapname == "q2ctf4") {
			static std::array<boss_t, std::size(BOSS_LARGE)> filteredLargeBossList;
			size_t count = 0;

			for (const auto& boss : std::span<const boss_t>(BOSS_LARGE)) {
				if (std::strcmp(boss.classname, "monster_boss5") != 0) {
					filteredLargeBossList[count++] = boss;
				}
			}

			return std::span<const boss_t>(filteredLargeBossList.data(), count);
		}
		return std::span<const boss_t>(BOSS_LARGE);
	}

	return std::span<const boss_t>(); // Empty span for no match
}

// static arrays to replace std::vectors
struct EligibleBosses {
	const boss_t* items[MAX_ELIGIBLE_BOSSES] = {};
	size_t count = 0;

	void clear() noexcept { count = 0; }

	bool add(const boss_t* boss) noexcept {
		if (!boss || count >= MAX_ELIGIBLE_BOSSES)
			return false;

		// Use constant expressions for array access
		switch (count) {
		case 0:  items[0] = boss; break;
		case 1:  items[1] = boss; break;
		case 2:  items[2] = boss; break;
		case 3:  items[3] = boss; break;
		case 4:  items[4] = boss; break;
		case 5:  items[5] = boss; break;
		case 6:  items[6] = boss; break;
		case 7:  items[7] = boss; break;
		case 8:  items[8] = boss; break;
		case 9:  items[9] = boss; break;
		case 10: items[10] = boss; break;
		case 11: items[11] = boss; break;
		case 12: items[12] = boss; break;
		case 13: items[13] = boss; break;
		case 14: items[14] = boss; break;
		case 15: items[15] = boss; break;
		default: return false;
		}
		count++;
		return true;
	}
};

// static array for recent bosses
// static array for recent bosses
struct RecentBosses {
	const char* items[MAX_RECENT_BOSSES] = {};
	size_t count = 0;

	void add(const char* boss) noexcept {
		if (!boss)
			return;

		// Early return for empty slot
		if (count < MAX_RECENT_BOSSES) {
			items[count++] = boss;
			return;
		}

		// Fixed-size shift using memmove for better performance
		memmove(&items[0], &items[1], sizeof(const char*) * (MAX_RECENT_BOSSES - 1));
		items[MAX_RECENT_BOSSES - 1] = boss;
	}

	bool contains(const char* boss) const noexcept {
		if (!boss || count == 0)
			return false;

		// Linear search - fastest for small arrays like this
		for (size_t i = 0; i < count; ++i) {
			if (strcmp(items[i], boss) == 0)
				return true;
		}
		return false;
	}

	void clear() noexcept {
		count = 0;
		// No need to zero the array - count will prevent access
	}
};
static RecentBosses recent_bosses;

static const char* G_HordePickBOSS(const MapSize& mapSize, std::string_view mapname, int32_t waveNumber, edict_t* bossEntity) {
	// Use static stack-based arrays instead of dynamic allocations
	static struct {
		const boss_t* items[MAX_ELIGIBLE_BOSSES]{};
		double weights[MAX_ELIGIBLE_BOSSES]{};
		size_t count = 0;
	} eligible_bosses;

	// Reset state
	eligible_bosses.count = 0;
	double total_weight = 0.0;

	// Get boss list once
	const auto boss_list = GetBossList(mapSize, mapname);
	if (boss_list.empty())
		return nullptr;

	// First pass: collect eligible bosses
	for (const auto& boss : boss_list) {
		// Check eligibility criteria
		const bool level_match = (waveNumber >= boss.min_level || boss.min_level == -1) &&
			(waveNumber <= boss.max_level || boss.max_level == -1);

		if (level_match && !recent_bosses.contains(boss.classname)) {
			// Calculate adjusted weight
			float adjusted_weight = boss.weight;

			// Apply weight adjustments
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

			// Add to array directly
			if (eligible_bosses.count < MAX_ELIGIBLE_BOSSES) {
				total_weight += adjusted_weight;
				eligible_bosses.weights[eligible_bosses.count] = total_weight;
				eligible_bosses.items[eligible_bosses.count] = &boss;
				eligible_bosses.count++;
			}
		}
	}

	// If no bosses eligible, retry with cleared history
	if (eligible_bosses.count == 0) {
		recent_bosses.clear();
		total_weight = 0.0;

		// Second pass with reset history
		for (const auto& boss : boss_list) {
			if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
				(waveNumber <= boss.max_level || boss.max_level == -1)) {

				const float adjusted_weight = boss.weight;
				total_weight += adjusted_weight;

				if (eligible_bosses.count < MAX_ELIGIBLE_BOSSES) {
					eligible_bosses.weights[eligible_bosses.count] = total_weight;
					eligible_bosses.items[eligible_bosses.count] = &boss;
					eligible_bosses.count++;
				}
			}
		}
	}

	// Exit if no eligible bosses
	if (eligible_bosses.count == 0)
		return nullptr;

	// Select boss with binary search
	const double random_value = std::uniform_real_distribution<double>(0.0, total_weight)(mt_rand);
	size_t left = 0;
	size_t right = eligible_bosses.count - 1;

	while (left < right) {
		const size_t mid = (left + right) / 2;
		if (eligible_bosses.weights[mid] < random_value) {
			left = mid + 1;
		}
		else {
			right = mid;
		}
	}

	// Get selected boss
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
	_Field_size_(MAX_ENTRIES) Entry entries[MAX_ENTRIES] = { {} }; // Doble llaves  // Inicializar array

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

static int32_t countFlyingSpawns() noexcept {
	return std::count_if(g_edicts + 1, g_edicts + globals.num_edicts,
		[](const edict_t& ent) {
			return ent.inuse &&
				strcmp(ent.classname, "info_player_deathmatch") == 0 &&
				ent.style == 1;
		});
}

static float adjustFlyingSpawnProbability(int32_t flyingSpawns) noexcept {
	switch (flyingSpawns) {
	case 0: return 1.0f;
	case 1: return 0.9f;
	case 2: return 0.8f;
	case 3: return 0.6f;
	default: return 0.5f;
	}
}

//inline static bool IsMonsterEligible(const edict_t* spawn_point, const weighted_item_t& item, bool isFlyingMonster, int32_t currentWave, int32_t flyingSpawns) noexcept {
//	// Check for flying wave requirement
//	const bool isFlyingWave = HasWaveType(current_wave_type, MonsterWaveType::Flying);
//
//	// During flying waves, only allow flying monsters
//	if (isFlyingWave) {
//		return isFlyingMonster &&
//			!(spawn_point->style == 1 && !isFlyingMonster) &&
//			!(item.min_level > currentWave || (item.max_level != -1 && item.max_level < currentWave));
//	}
//
//	// For non-flying waves, just check spawn point compatibility and level requirements
//	return !(spawn_point->style == 1 && !isFlyingMonster) &&
//		!(item.min_level > currentWave || (item.max_level != -1 && item.max_level < currentWave));
//}

static void UpdateCooldowns(edict_t* spawn_point, const char* chosen_monster) {
	auto& data = spawnPointsData[spawn_point];
	data.lastSpawnTime = level.time;
	//data.spawn_cooldown = level.time + SPAWN_POINT_COOLDOWN;
	data.isTemporarilyDisabled = true;
	data.cooldownEndsAt = level.time + SPAWN_POINT_COOLDOWN;
}

// Function to increase spawn attempts and adjust cooldown as necessary
// Single spawn point version
//void IncreaseSpawnAttempts(edict_t* spawn_point) {
//	if (!spawn_point || !spawn_point->inuse) {
//		return;
//	}
//
//	auto& data = spawnPointsData[spawn_point];
//	data.attempts++;
//
//	// Check for nearby players
//	bool players_nearby = false;
//	for (const auto* const player : active_players()) {
//		if ((spawn_point->s.origin - player->s.origin).length() < 300.0f) {
//			players_nearby = true;
//			break;
//		}
//	}
//
//	// Adjust max attempts based on player proximity
//	const int max_attempts = players_nearby ? 8 : 6;  // More attempts before disable
//
//	if (data.attempts >= max_attempts) {
//		if (developer->integer) {
//			gi.Com_PrintFmt("SpawnPoint at position ({}, {}, {}) inactivated.\n",
//				spawn_point->s.origin[0], spawn_point->s.origin[1], spawn_point->s.origin[2]);
//		}
//
//		data.isTemporarilyDisabled = true;
//		const gtime_t cooldown = players_nearby ? 0.5_sec : 1_sec;  // Shorter cooldowns
//		data.cooldownEndsAt = level.time + cooldown;
//	}
//	else if (data.attempts % 3 == 0) {
//		data.cooldownEndsAt = std::max(data.cooldownEndsAt + 1_sec, 2_sec);
//	}
//
//	// Reset attempts after sufficient time
//	if (level.time - data.lastSpawnTime > 5_sec) {
//		data.attempts = 0;
//	}
//}

//// Batch version using span
//void IncreaseSpawnAttemptsBatch(std::span<edict_t*> spawn_points) {
//    if (spawn_points.empty()) {
//        return;
//    }
//
//    // Validate span contents
//    for (edict_t* spawn : spawn_points) {
//        if (!spawn || !spawn->inuse) {
//            return; // Skip batch if any invalid points
//        }
//    }
//
//	static constexpr size_t BATCH_SIZE = 8;
//	for (size_t i = 0; i < spawn_points.size(); i += BATCH_SIZE) {
//		const size_t batch_end = std::min(i + BATCH_SIZE, spawn_points.size());
//		std::span<edict_t*> batch = spawn_points.subspan(i, batch_end - i);
//
//		for (edict_t* spawn_point : batch) {
//			IncreaseSpawnAttempts(spawn_point);
//		}
//	}
//}

static const char* G_HordePickMonster(edict_t* spawn_point) {
	// Static cache to avoid repeated allocations
	static struct {
		struct Entry {
			const char* classname;
			float weight;
			float cumulative_weight;
		};
		Entry entries[32]; // Max number of entries
		size_t count;
		float total_weight;
	} monster_cache;

	// Reset cache counters
	monster_cache.count = 0;
	monster_cache.total_weight = 0.0f;

	// Early exit checks
	if (!spawn_point || !spawn_point->inuse) {
		return nullptr;
	}

	// Check spawn point availability
	auto& data = spawnPointsData[spawn_point];
	if (data.isTemporarilyDisabled) {
		if (level.time < data.cooldownEndsAt) {
			return nullptr;
		}
		// Reset state if cooldown expired
		data.isTemporarilyDisabled = false;
		data.attempts = 0;
	}

	// Check if spawn point is occupied
	if (IsSpawnPointOccupied(spawn_point)) {
		IncreaseSpawnAttempts(spawn_point);
		return nullptr;
	}

	// Cache commonly used values
	const int32_t currentLevel = g_horde_local.level;
	const MonsterWaveType currentWaveTypes = current_wave_type;
	const bool isSpawnPointFlying = spawn_point->style == 1;

	// Boss wave check - no regular monsters during boss wave before boss spawns
	if (currentWaveTypes == MonsterWaveType::None &&
		currentLevel >= 10 && currentLevel % 5 == 0) {
		return nullptr;
	}

	// Pre-compute flying spawn adjustment
	const int32_t flyingSpawns = countFlyingSpawns();
	const float adjustmentFactor = adjustFlyingSpawnProbability(flyingSpawns);

	// Iterate monsters and build cache
	for (const auto& monster : monsterTypes) {
		// Skip if we've reached capacity
		if (monster_cache.count >= 32) {
			break;
		}

		// Basic level check
		if (monster.minWave > currentLevel) {
			continue;
		}

		// Skip if not valid for current wave type
		if (!IsValidMonsterForWave(monster.classname, currentWaveTypes)) {
			continue;
		}

		// Flying compatibility check
		const bool isFlyingMonster = HasWaveType(monster.types, MonsterWaveType::Flying);
		if (isSpawnPointFlying && !isFlyingMonster) {
			continue;
		}

		// Calculate base weight
		float weight = monster.weight;

		// Apply level-based adjustments - use if/else ladder for better branch prediction
		if (currentLevel <= 5) {
			if (!HasWaveType(monster.types, MonsterWaveType::Light)) {
				weight *= 0.3f;
			}
		}
		else if (currentLevel <= 10) {
			if (!HasWaveType(monster.types, MonsterWaveType::Light | MonsterWaveType::Small)) {
				weight *= 0.4f;
			}
		}
		else if (currentLevel <= 15) {
			if (!HasWaveType(monster.types, MonsterWaveType::Medium)) {
				weight *= 0.5f;
			}
		}
		else {
			if (!HasWaveType(monster.types, MonsterWaveType::Heavy | MonsterWaveType::Elite)) {
				weight *= 0.6f;
			}
			else {
				weight *= 1.0f + ((currentLevel - 15) * 0.02f);
			}
		}

		// Special handling for boss wave minions
		if (currentLevel >= 10 && currentLevel % 5 == 0 && boss_spawned_for_wave) {
			weight *= HasWaveType(monster.types, currentWaveTypes) ? 2.0f : 0.3f;
		}

		// Apply difficulty adjustments
		if (g_insane->integer || g_chaotic->integer) {
			// Pre-compute difficulty scale based on level
			const float difficultyScale = currentLevel <= 10 ? 1.1f :
				currentLevel <= 20 ? 1.2f :
				currentLevel <= 30 ? 1.3f : 1.4f;

			weight *= difficultyScale;

			if (HasWaveType(monster.types, MonsterWaveType::Elite)) {
				weight *= 1.2f;
			}
		}

		// Apply flying adjustment
		weight *= adjustmentFactor;

		// Add to cache if weight is valid
		if (weight > 0.0f) {
			monster_cache.total_weight += weight;
			auto& entry = monster_cache.entries[monster_cache.count];
			entry.classname = monster.classname;
			entry.weight = weight;
			entry.cumulative_weight = monster_cache.total_weight;
			monster_cache.count++;
		}
	}

	// Handle empty results
	if (monster_cache.count == 0) {
		IncreaseSpawnAttempts(spawn_point);
		return nullptr;
	}

	// Select monster using binary search for better performance
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

	// Get the selected monster
	const char* chosen_monster = monster_cache.entries[left].classname;

	// Update cooldowns if we have a valid selection
	if (chosen_monster) {
		UpdateCooldowns(spawn_point, chosen_monster);
	}

	return chosen_monster;
}

void Horde_PreInit() {
	gi.Com_Print("Horde mode must be DM. Set <deathmatch 1> and <horde 1>, then <map mapname>.\n");
	gi.Com_Print("COOP requires <coop 1> and <horde 0>, optionally <g_hardcoop 1/0>.\n");

	g_horde = gi.cvar("horde", "0", CVAR_LATCH);
	//gi.Com_Print("After starting a normal server type: starthorde to start a game.\n");


	if (!g_horde->integer) {
		//deathmatch->integer == 0;
		gi.cvar_forceset("deathmatch", "0");
		return;
	}

	//if ((!deathmatch->integer) || (ctf->integer || teamplay->integer || coop->integer)) {
	//	gi.Com_Print("Horde mode must be DM.\n");
	//	//gi.cvar_set("deathmatch", "1");
	//	//gi.cvar_set("ctf", "0");
	//	//gi.cvar_set("teamplay", "0");
	//	//gi.cvar_set("coop", "0");
	//	//gi.cvar_set("timelimit", "20");
	//	//gi.cvar_set("fraglimit", "0");
	//}

	//if (deathmatch->integer && !g_horde->integer)
	//gi.cvar_set("g_coop_player_collision", "0");
	//gi.cvar_set("g_coop_squad_respawn", "0");
	//gi.cvar_set("g_coop_instanced_items", "0");
	//gi.cvar_set("g_disable_player_collision", "0");

	// Configuración automática cuando horde está activo
	if (g_horde->integer) {
		//deathmatch->integer == 1;
		dm_monsters = gi.cvar("dm_monsters", "0", CVAR_SERVERINFO);

		gi.Com_Print("Initializing Horde mode settings...\n");

		// Configuración de tiempo y límites
		gi.cvar_forceset("deathmatch", "1");
		gi.cvar_forceset("coop", "0");
		gi.cvar_forceset("g_teamplay_force_join", "0");
		gi.cvar_forceset("timelimit", "50");
		gi.cvar_forceset("fraglimit", "0");
		gi.cvar_forceset("capturelimit", "0");

		// Configuración de jugabilidad
		gi.cvar_forceset("sv_target_id", "1");
		gi.cvar_forceset("g_speedstuff", "1.8f");
		gi.cvar_forceset("sv_eyecam", "1");
		gi.cvar_forceset("g_dm_instant_items", "1");
		gi.cvar_forceset("g_disable_player_collision", "1");
		gi.cvar_forceset("g_dm_no_self_damage", "1");
		gi.cvar_forceset("g_allow_techs", "1");

		// Configuración de physics
		gi.cvar_forceset("g_override_physics_flags", "-1");

		// Configuración de armas y daño
		gi.cvar_forceset("g_no_nukes", "0");
		gi.cvar_forceset("g_instant_weapon_switch", "1");
		gi.cvar_forceset("g_dm_no_quad_drop", "0");
		gi.cvar_forceset("g_dm_no_quadfire_drop", "0");

		// Configuración del hook/grapple
		gi.cvar_forceset("g_use_hook", "1");
		gi.cvar_forceset("g_hook_wave", "1");
		gi.cvar_forceset("hook_pullspeed", "1200");
		gi.cvar_forceset("hook_speed", "3000");
		gi.cvar_forceset("hook_sky", "1");
		gi.cvar_forceset("g_allow_grapple", "1");
		gi.cvar_forceset("g_grapple_fly_speed", "3000");
		gi.cvar_forceset("g_grapple_pull_speed", "1200");

		// Configuración de gameplay específica
		gi.cvar_forceset("g_startarmor", "0");
		gi.cvar_forceset("g_vampire", "0");
		gi.cvar_forceset("g_ammoregen", "0");
		gi.cvar_forceset("g_tracedbullets", "0");
		gi.cvar_forceset("g_energyshells", "0");
		gi.cvar_forceset("g_bouncygl", "0");
		gi.cvar_forceset("g_bfgpull", "0");
		gi.cvar_forceset("g_bfgslide", "1");
		gi.cvar_forceset("g_energyshells", "0");
		gi.cvar_forceset("g_autohaste", "0");
		gi.cvar_forceset("g_chaotic", "0");
		gi.cvar_forceset("g_insane", "0");
		gi.cvar_forceset("g_hardcoop", "0");

		// Configuración de IA y bots
		gi.cvar_forceset("g_dm_spawns", "0");
		gi.cvar_forceset("g_damage_scale", "1");
		gi.cvar_forceset("ai_allow_dm_spawn", "1");
		gi.cvar_forceset("ai_damage_scale", "1");
		gi.cvar_forceset("g_loadent", "1");
		gi.cvar_forceset("bot_chat_enable", "0");
		gi.cvar_forceset("bot_skill", "5");
		gi.cvar_forceset("g_coop_squad_respawn", "1");
		gi.cvar_forceset("g_iddmg", "1");

		// Activar monstruos automáticamente
		gi.cvar_forceset("dm_monsters", "0");

		// Resetear el estado del juego
		HandleResetEvent();

		// Mensaje de confirmación
		gi.Com_Print("Horde mode initialized successfully.\n");
	}
}

void VerifyAndAdjustBots() {
	if (developer->integer == 2) {
		gi.cvar_set("bot_minClients", "-1");
	}
	else {
		const MapSize mapSize = GetMapSize(static_cast<const char*>(level.mapname));
		const int32_t spectPlayers = GetNumSpectPlayers();
		const int32_t baseBots = mapSize.isBigMap ? 6 : 4;

		// Agregar bot extra si current_wave_level >= 20
		const int32_t extraBot = (current_wave_level >= 20) ? 1 : 0;
		const int32_t requiredBots = std::max(baseBots + spectPlayers + extraBot, baseBots);

		gi.cvar_set("bot_minClients", std::to_string(requiredBots).c_str());
	}
}

void InitializeWaveSystem() noexcept;

static bool items_precached = false;

static void PrecacheItemsAndBosses() noexcept {
	// Only precache once
	if (items_precached)
		return;

	std::span<const weighted_item_t> items_view{ items };
	std::span<const MonsterTypeInfo> monsters_view{ monsterTypes };

	// Size hint for better performance
	std::unordered_set<std::string_view> unique_classnames;
	unique_classnames.reserve(items_view.size() + monsters_view.size());

	// Add classnames using spans
	for (const auto& item : items_view)
		if (item.classname)  // Safety check
			unique_classnames.emplace(item.classname);

	for (const auto& monster : monsters_view)
		if (monster.classname)  // Safety check
			unique_classnames.emplace(monster.classname);

	// Precache items
	for (const auto& classname : unique_classnames)
		if (gitem_t* item = FindItemByClassname(classname.data()))
			PrecacheItem(item);

	items_precached = true;
}

static bool monsters_precached = false;

// Modified precache function with safety check
static void PrecacheAllMonsters() noexcept {
	// Only precache once
	if (monsters_precached)
		return;

	for (const auto& monster : monsterTypes) {
		// Instead of spawning entities, just precache the models and sounds directly
		if (gitem_t* item = FindItemByClassname(monster.classname)) {
			PrecacheItem(item);
		}
	}

	monsters_precached = true;
}


// Función para precarga de sonidos
static bool sounds_precached = false;

static void PrecacheWaveSounds() noexcept {
	// Only precache once
	if (sounds_precached)
		return;

	// Individual sounds - using an array for better organization
	static const std::array<std::pair<cached_soundindex*, const char*>, 4> individual_sounds = { {
		{&sound_tele3, "misc/r_tele3.wav"},
		{&sound_tele_up, "misc/tele_up.wav"},
		{&sound_spawn1, "misc/spawn1.wav"},
		{&incoming, "world/incoming.wav"}
	} };

	// Precache individual sounds using span
	std::span individual_view{ individual_sounds };
	for (const auto& [sound_index, path] : individual_view) {
		sound_index->assign(path);
	}

	// Precache wave sounds using span
	std::span wave_view{ WAVE_SOUND_PATHS };
	for (size_t i = 0; i < NUM_WAVE_SOUNDS; ++i) {
		wave_sounds[i].assign(wave_view[i]);
	}

	// Precache start sounds using span
	std::span start_view{ START_SOUND_PATHS };
	for (size_t i = 0; i < NUM_START_SOUNDS; ++i) {
		start_sounds[i].assign(start_view[i]);
	}

	sounds_precached = true;
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
		size_t const index = irandom(NUM_WAVE_SOUNDS);
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
		size_t const index = irandom(NUM_START_SOUNDS);
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
void AllowReset() noexcept {
	hasBeenReset = false;
}

void Horde_Init() {
	// Reset precache state
	sounds_precached = false;
	items_precached = false;
	monsters_precached = false;

	// Clear existing bosses
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end();) {
		edict_t* boss = *it;
		if (boss && boss->inuse) {
			boss->monsterinfo.BOSS_DEATH_HANDLED = true;
			OnEntityRemoved(boss);
		}
		it = auto_spawned_bosses.erase(it);
	}
	auto_spawned_bosses.clear();

	// Do precaching
	PrecacheAllMonsters();
	PrecacheItemsAndBosses();
	PrecacheWaveSounds();
	InitializeWaveSystem();
	last_wave_number = 0;

	AllowReset();
	ResetGame();

	gi.Com_Print("PRINT: Horde game state initialized with all necessary resources precached.\n");
}

// Constantes para mejorar la legibilidad y mantenibilidad
constexpr int MIN_VELOCITY = -800;
constexpr int MAX_VELOCITY = 800;
constexpr int MIN_VERTICAL_VELOCITY = 400;
constexpr int MAX_VERTICAL_VELOCITY = 950;

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

void BossDeathHandler(edict_t* boss) {
	// All validations in a single condition with early return
	if (!g_horde->integer || !boss || !boss->inuse || !boss->monsterinfo.IS_BOSS ||
		boss->monsterinfo.BOSS_DEATH_HANDLED || boss->health > 0) {
		return;
	}

	// Mark as handled immediately to prevent double processing
	boss->monsterinfo.BOSS_DEATH_HANDLED = true;

	// Handle entity tracking in one block
	OnEntityDeath(boss);
	OnEntityRemoved(boss);
	auto_spawned_bosses.erase(boss);

	// Pre-define item drops as constants to avoid repeated string lookups
	static const char* itemsToDrop[] = {
		"item_adrenaline", "item_pack", "item_sentrygun",
		"item_sphere_defender", "item_armor_combat", "item_bandolier",
		"item_invulnerability", "ammo_nuke"
	};

	// Pre-calculate velocity parameters once for all items
	static const vec3_t base_velocity(MIN_VELOCITY, MIN_VELOCITY, MIN_VERTICAL_VELOCITY);
	static const vec3_t velocity_range(
		MAX_VELOCITY - MIN_VELOCITY,
		MAX_VELOCITY - MIN_VELOCITY,
		MAX_VERTICAL_VELOCITY - MIN_VERTICAL_VELOCITY
	);

	// Weapon drop with improved selection and null checks
	const char* weapon_classname = SelectBossWeaponDrop(current_wave_level);
	if (weapon_classname) {
		gitem_t* weapon_item = FindItemByClassname(weapon_classname);
		if (weapon_item) {
			edict_t* weapon = Drop_Item(boss, weapon_item);
			if (weapon) {
				// Generate velocity components in a single operation
				const vec3_t weaponVelocity = base_velocity + velocity_range.scaled(
					vec3_t{ frandom(), frandom(), frandom() }
				);

				// Set all properties in groups for better cache coherency
				weapon->s.origin = boss->s.origin;
				weapon->velocity = weaponVelocity;
				weapon->movetype = MOVETYPE_BOUNCE;

				// Set visual effects in one batch
				weapon->s.effects = EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER;
				weapon->s.renderfx = RF_GLOW;
				weapon->s.alpha = 0.85f;
				weapon->s.scale = 1.25f;

				// Set flags in one operation
				weapon->spawnflags = SPAWNFLAG_ITEM_DROPPED_PLAYER;
				weapon->flags &= ~FL_RESPAWN;

				// Link entity once after all properties are set
				gi.linkentity(weapon);
			}
		}
	}

	// Special item drop (quad or quadfire) with single random check
	const char* specialItemName = brandom() ? "item_quadfire" : "item_quad";
	gitem_t* special_item = FindItemByClassname(specialItemName);
	if (special_item) {
		edict_t* specialItem = Drop_Item(boss, special_item);
		if (specialItem) {
			// Set velocity in a single operation using static distributions
			static std::uniform_int_distribution<int> vel_dist(MIN_VELOCITY, MAX_VELOCITY);
			static std::uniform_int_distribution<int> vert_dist(300, 400);

			const vec3_t specialVelocity{
				static_cast<float>(vel_dist(mt_rand)),
				static_cast<float>(vel_dist(mt_rand)),
				static_cast<float>(vert_dist(mt_rand))
			};

			// Group properties by type
			specialItem->s.origin = boss->s.origin;
			specialItem->velocity = specialVelocity;
			specialItem->movetype = MOVETYPE_BOUNCE;

			// Set effects in one operation
			specialItem->s.effects = EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER | EF_HOLOGRAM;
			specialItem->s.alpha = 0.8f;
			specialItem->s.scale = 1.5f;
			specialItem->flags &= ~FL_RESPAWN;

			// Link entity once
			gi.linkentity(specialItem);
		}
	}

	// Fisher-Yates shuffle with stack-allocated array
	// Use memcpy instead of manual assignment for array initialization
	char const* shuffledItems[8];
	memcpy(shuffledItems, itemsToDrop, sizeof(itemsToDrop));

	// Optimize shuffle to use fewer operations
	for (int i = 7; i > 0; --i) {
		int j = mt_rand() % (i + 1);
		if (i != j) {
			std::swap(shuffledItems[i], shuffledItems[j]);
		}
	}

	// Pre-create velocity distributions once
	static std::uniform_int_distribution<int> vel_dist(MIN_VELOCITY, MAX_VELOCITY);
	static std::uniform_int_distribution<int> vert_dist(MIN_VERTICAL_VELOCITY, MAX_VERTICAL_VELOCITY);

	// Drop regular items in batches
	for (int i = 0; i < 8; ++i) {
		gitem_t* item = FindItemByClassname(shuffledItems[i]);
		if (!item) continue;

		edict_t* droppedItem = Drop_Item(boss, item);
		if (droppedItem) {
			// Create velocity vector in a single operation
			droppedItem->velocity = vec3_t{
				static_cast<float>(vel_dist(mt_rand)),
				static_cast<float>(vel_dist(mt_rand)),
				static_cast<float>(vert_dist(mt_rand))
			};

			// Set origin and properties in grouped operations
			droppedItem->s.origin = boss->s.origin;
			droppedItem->movetype = MOVETYPE_BOUNCE;
			droppedItem->flags &= ~FL_RESPAWN;
			droppedItem->s.effects |= EF_GIB;
			droppedItem->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;

			// Link entity once
			gi.linkentity(droppedItem);
		}
	}

	// Clear combat flags at the end
	boss->takedamage = false;
	boss->solid = SOLID_NOT;
	gi.linkentity(boss);
}

void boss_die(edict_t* boss) {
	if (!boss || !boss->inuse || !g_horde->integer ||
		!boss->monsterinfo.IS_BOSS || boss->health > 0 ||
		boss->monsterinfo.BOSS_DEATH_HANDLED ||
		auto_spawned_bosses.find(boss) == auto_spawned_bosses.end()) {
		return;
	}

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

static bool Horde_AllMonstersDead() {
	// Fast path using level counters
	if (level.total_monsters == level.killed_monsters) {
		return true;
	}

	// Secondary verification by direct entity check
	// Using active_or_dead_monsters() for more complete coverage
	for (auto ent : active_or_dead_monsters()) {
		// Skip invalid entities and non-counting monsters
		if (!ent || !ent->inuse || (ent->monsterinfo.aiflags & AI_DO_NOT_COUNT))
			continue;

		// If any live monster found, return false immediately
		if (!ent->deadflag && ent->health > 0) {
			return false;
		}

		// Handle dying bosses
		if (ent->monsterinfo.IS_BOSS && ent->health <= 0) {
			if (auto_spawned_bosses.find(ent) != auto_spawned_bosses.end() &&
				!ent->monsterinfo.BOSS_DEATH_HANDLED) {
				boss_die(ent);
			}
		}
	}

	// If we get here, no live monsters found
	return true;
}

// Modify the CheckAndRestoreMonsterAlpha function to batch updates
void CheckAndRestoreMonsterAlpha(edict_t* const ent) {
	if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER)) {
		return;
	}

	// Batch multiple attribute changes before linking
	bool needs_update = false;
	if (ent->health > 0 && !ent->deadflag && ent->s.alpha < 1.0f) {
		ent->s.alpha = 0.0f;
		ent->s.renderfx &= ~RF_TRANSLUCENT;
		ent->takedamage = true;
		needs_update = true;
	}

	// Only link if necessary
	if (needs_update) {
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
	if ((ent->health > 0 && !ent->deadflag) ||
		ent->is_fading_out ||
		(ent->monsterinfo.aiflags & (AI_CLEANUP_FADE | AI_CLEANUP_NORMAL))) {
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
	for (edict_t* ent : active_or_dead_monsters()) {
		if (ent->deadflag || ent->health <= 0) {
			if (!ent->is_fading_out) { // Only check once before starting fade
				StartFadeOut(ent);
			}
		}
		else { // If the monster is alive but somehow flagged for removal:
			CheckAndRestoreMonsterAlpha(ent); // Restore alpha for live monsters
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
	{"e3/jail_e3", {-572, -1312, 76}},
	{"xintell", {2096, -992, 376}}
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
	// Clear any existing bosses
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end(); ) {
		edict_t* existing_boss = *it;
		if (existing_boss && existing_boss->inuse) {
			existing_boss->monsterinfo.BOSS_DEATH_HANDLED = true;
			OnEntityRemoved(existing_boss);
			G_FreeEdict(existing_boss);
		}
		it = auto_spawned_bosses.erase(it);
	}

	// Reset wave type
	current_wave_type = MonsterWaveType::None;

	// Clear health bar
	for (size_t i = 0; i < MAX_HEALTH_BARS; ++i) {
		if (level.health_bar_entities[i]) {
			G_FreeEdict(level.health_bar_entities[i]);
			level.health_bar_entities[i] = nullptr;
		}
	}

	// Clear health bar name
	gi.configstring(CONFIG_HEALTH_BAR_NAME, "");

	// Early return if not a boss wave
	if (g_horde_local.level < 10 || g_horde_local.level % 5 != 0) {
		return;
	}

	// Get map name and find spawn origin
	const char* map_name = GetCurrentMapName();
	const auto it = mapOrigins.find(map_name);
	if (it == mapOrigins.end()) {
		if (developer->integer) {
			gi.Com_PrintFmt("Error: No spawn origin found for map {}\n", map_name);
		}
		return;
	}

	// Convert to vec3_t once
	const vec3_t spawn_origin{
		static_cast<float>(it->second[0]),
		static_cast<float>(it->second[1]),
		static_cast<float>(it->second[2])
	};

	// Validate origin
	if (!is_valid_vector(spawn_origin) || spawn_origin == vec3_origin) {
		if (developer->integer) {
			gi.Com_PrintFmt("Error: Invalid spawn origin for map {}\n", map_name);
		}
		return;
	}

	// Create orb effect
	edict_t* orb = G_Spawn();
	if (orb) {
		orb->classname = "target_orb";
		orb->s.origin = spawn_origin;
		SP_target_orb(orb);
	}

	// Spawn boss entity
	edict_t* boss = G_Spawn();
	if (!boss) {
		if (orb) G_FreeEdict(orb);
		if (developer->integer) {
			gi.Com_PrintFmt("Error: Failed to spawn boss entity\n");
		}
		return;
	}

	// Select boss type
	const char* desired_boss = G_HordePickBOSS(
		g_horde_local.current_map_size, map_name, g_horde_local.level, boss);

	if (!desired_boss) {
		if (orb) G_FreeEdict(orb);
		G_FreeEdict(boss);
		if (developer->integer) {
			gi.Com_PrintFmt("Error: Failed to pick a boss type\n");
		}
		return;
	}

	// Set up boss entity
	boss_spawned_for_wave = true;
	boss->classname = desired_boss;
	boss->s.origin = spawn_origin;

	// Push away nearby entities
	constexpr float push_radius = 500.0f;
	constexpr float push_force = 600.0f;
	PushEntitiesAway(spawn_origin, 3, push_radius, push_force, 750.0f, 75.0f);

	// Store orb entity
	boss->owner = orb;

	// Set up delayed spawn
	boss->nextthink = level.time + 750_ms;
	boss->think = BossSpawnThink;
}

THINK(BossSpawnThink)(edict_t* self) -> void {
	// Remove the black light effect immediately
	if (self->owner) {
		G_FreeEdict(self->owner);
		self->owner = nullptr;
	}

	// Static mapping for boss types to reduce string comparisons
	static const std::unordered_map<std::string_view, std::pair<MonsterWaveType, const char*>> bossTypeMap = {
		{"monster_redmutant", {MonsterWaveType::Mutant, "\n\n\nMutant's invasion approaches!\n"}},
		{"monster_shamblerkl", {MonsterWaveType::Shambler, "\n\n\nMutant's invasion approaches!\n"}},
		{"monster_widow", {MonsterWaveType::Small, "\n\n\nWidow swarm incoming!\n"}},
		{"monster_widow2", {MonsterWaveType::Small, "\n\n\nWidow swarm incoming!\n"}},
		{"monster_psxarachnid", {MonsterWaveType::Arachnophobic, "\n\n\nArachnophobia wave incoming!\n"}},
		{"monster_boss2", {MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n"}},
		{"monster_carrier", {MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n"}},
		{"monster_carrier_mini", {MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n"}},
		{"monster_boss2kl", {MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n"}},
		{"monster_tank_64", {MonsterWaveType::Medium, "\n\n\nHeavy/Mid armored division incoming!\n"}},
		{"monster_supertank", {MonsterWaveType::Medium, "\n\n\nHeavy/Mid armored division incoming!\n"}},
		{"monster_psxguardian", {MonsterWaveType::Medium, "\n\n\nHeavy/Mid armored division incoming!\n"}},
		{"monster_boss5", {MonsterWaveType::Medium, "\n\n\nHeavy/Mid armored division incoming!\n"}},
		{"monster_guncmdrkl", {MonsterWaveType::Medium, "\n\n\nPrepare bayonets!The invaders are about to get up close and personal!\n"}},
		{"monster_makron", {MonsterWaveType::Medium, "\n\n\nPrepare bayonets!The invaders are about to get up close and personal!\n"}},
		{"monster_makronkl", {MonsterWaveType::Medium, "\n\n\nPrepare bayonets!The invaders are about to get up close and personal!\n"}}
	};

	// Set wave type based on boss type with single lookup
	auto it = bossTypeMap.find(self->classname);
	if (it != bossTypeMap.end()) {
		const auto& [waveType, message] = it->second;
		if (TrySetWaveType(waveType)) {
			gi.LocBroadcast_Print(PRINT_CHAT, message);
		}
		else if (waveType == MonsterWaveType::Mutant || waveType == MonsterWaveType::Shambler) {
			// Fallback for mutant/shambler types
			current_wave_type = MonsterWaveType::Medium;
			StoreWaveType(MonsterWaveType::Medium);
			gi.LocBroadcast_Print(PRINT_CHAT, "\n\n\nFallback wave: Medium forces incoming!\n");
		}
	}
	else if (current_wave_type == MonsterWaveType::None) {
		// Default to medium if no specific type
		current_wave_type = MonsterWaveType::Medium;
		StoreWaveType(current_wave_type);
	}

	// Boss spawn message
	const auto it_msg = bossMessagesMap.find(self->classname);
	if (it_msg != bossMessagesMap.end()) {
		gi.LocBroadcast_Print(PRINT_CHAT, "\n\n\n{}\n", it_msg->second.data());
	}
	else {
		gi.LocBroadcast_Print(PRINT_CHAT, "\n\n\nA Strogg Boss has spawned!\nPrepare for battle!\n");
	}

	// Set boss flags in a single group
	self->monsterinfo.IS_BOSS = true;
	self->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	self->monsterinfo.last_sentrygun_target_time = 0_ms;

	// Spawn entity
	{
		self->solid = SOLID_NOT;
		ED_CallSpawn(self);
		ClearSpawnArea(self->s.origin, self->mins, self->maxs);
		self->solid = SOLID_BBOX;
		gi.linkentity(self);
	}

	// Configure boss properties
	ConfigureBossArmor(self);
	ApplyBossEffects(self);
	self->monsterinfo.attack_state = AS_BLIND;

	// Calculate spawn effect sizes once
	const vec3_t spawn_pos = self->s.origin;
	const float magnitude = spawn_pos.length();
	const float base_size = magnitude * 0.35f;
	const float end_size = base_size * 0.005f;

	// Apply visual effects
	ImprovedSpawnGrow(spawn_pos, base_size, end_size, self);
	SpawnGrow_Spawn(spawn_pos, base_size, end_size);

	// Set up health bar and track boss
	AttachHealthBar(self);
	SetHealthBarName(self);
	auto_spawned_bosses.insert(self);
}

bool CheckAndTeleportBoss(edict_t* self, const BossTeleportReason reason) {
	// Early validation for quick exit
	if (!self || !self->inuse || self->deadflag || !self->monsterinfo.IS_BOSS ||
		level.intermissiontime || !g_horde->integer)
		return false;

	// Cache map name once to reduce string comparisons
	static std::string last_map_name;
	static std::unordered_map<std::string, std::array<int, 3>>::const_iterator map_origin_it;

	// Only look up map origin if the map has changed
	const char* current_map = GetCurrentMapName();
	if (last_map_name != current_map) {
		last_map_name = current_map;
		map_origin_it = mapOrigins.find(current_map);

		// If map not found, cache the end iterator for quick rejection
		if (map_origin_it == mapOrigins.end())
			return false;
	}
	else if (map_origin_it == mapOrigins.end()) {
		// Use cached failure result
		return false;
	}

	// Select appropriate cooldown based on reason - use constexpr for compiler optimization
	constexpr gtime_t DROWNING_COOLDOWN = 1_sec;
	constexpr gtime_t TRIGGER_COOLDOWN = 3_sec;

	const gtime_t selected_cooldown = reason == BossTeleportReason::DROWNING ?
		DROWNING_COOLDOWN : TRIGGER_COOLDOWN;

	// Check if recently teleported
	if (self->teleport_time && level.time < self->teleport_time + selected_cooldown)
		return false;

	// Convert spawn point to vec3_t - directly use map_origin_it
	const vec3_t spawn_origin{
		static_cast<float>(map_origin_it->second[0]),
		static_cast<float>(map_origin_it->second[1]),
		static_cast<float>(map_origin_it->second[2])
	};

	// Verify spawn point validity
	if (!is_valid_vector(spawn_origin) || spawn_origin == vec3_origin)
		return false;

	// Hide from clients before teleport attempt
	self->svflags |= SVF_NOCLIENT;
	gi.unlinkentity(self);

	// Store current velocity and origin
	const vec3_t old_velocity = self->velocity;
	const vec3_t old_origin = self->s.origin;

	// Attempt teleport
	self->s.origin = spawn_origin;
	self->s.old_origin = spawn_origin;
	self->velocity = vec3_origin;

	// Check if new position is valid - use early returns for performance
	bool teleport_success = true;
	if (!(self->flags & (FL_FLY | FL_SWIM))) {
		teleport_success = M_droptofloor(self);
		if (!teleport_success) {
			// Restore on failure
			self->s.origin = old_origin;
			self->s.old_origin = old_origin;
			self->velocity = old_velocity;
			self->svflags &= ~SVF_NOCLIENT;
			gi.linkentity(self);
			return false;
		}
	}

	// Check collisions at new position
	if (gi.trace(self->s.origin, self->mins, self->maxs,
		self->s.origin, self, MASK_MONSTERSOLID).startsolid) {
		// Restore on failure
		self->s.origin = old_origin;
		self->s.old_origin = old_origin;
		self->velocity = old_velocity;
		self->svflags &= ~SVF_NOCLIENT;
		gi.linkentity(self);
		return false;
	}

	// Make visible again before effects
	self->svflags &= ~SVF_NOCLIENT;
	gi.linkentity(self);

	// Visual and sound effects based on reason - use cached display name
	static std::string cached_name;
	if (cached_name.empty() || self->client) {
		cached_name = GetDisplayName(self);
	}

	// Sound effects and messages based on reason
	switch (reason) {
	case BossTeleportReason::DROWNING:
		gi.sound(self, CHAN_AUTO, sound_tele3, 1, ATTN_NORM, 0);
		gi.LocBroadcast_Print(PRINT_HIGH, "{} emerges from the depths!\n", cached_name.c_str());
		break;
	case BossTeleportReason::TRIGGER_HURT:
		gi.sound(self, CHAN_AUTO, sound_tele_up, 1, ATTN_NORM, 0);
		gi.LocBroadcast_Print(PRINT_HIGH, "{} escapes certain death!\n", cached_name.c_str());
		break;
	}

	SpawnGrow_Spawn(self->s.origin, 80.0f, 10.0f);
	self->teleport_time = level.time;

	// Push away nearby entities
	PushEntitiesAway(spawn_origin, 3, 500.0f, 1000.0f, 3750.0f, 1600.0f);

	// Restore some health if heavily damaged - use multiplication instead of division
	if (self->health < (self->max_health >> 2)) { // Bit shift is faster than division by 4
		self->health = (self->max_health >> 2);
	}

	if (developer->integer) {
		const char* reason_str = reason == BossTeleportReason::DROWNING ? "drowning" : "trigger_hurt";
		gi.Com_PrintFmt("Boss teleported due to {} to: {}\n", reason_str, self->s.origin);
	}

	return true;
}

void SetHealthBarName(const edict_t* boss) {
	// Use a static buffer to avoid allocation
	static char buffer[MAX_STRING_CHARS];

	// Early validation
	if (!boss || !boss->inuse) {
		gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, "");
		return;
	}

	// Get name once
	const std::string display_name = GetDisplayName(boss);
	if (display_name.empty()) {
		gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, "");
		return;
	}

	// Calculate safe buffer size once
	const size_t name_len = std::min(display_name.length(),
		MAX_STRING_CHARS - 1);  // -1 for null terminator

	// Copy directly to buffer
	memcpy(buffer, display_name.c_str(), name_len);
	buffer[name_len] = '\0';  // Ensure null termination

	// Set the configstring once
	gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, buffer);
}
//CS HORDE

void UpdateHordeHUD() {
	// Rate limiting - exit early if called too frequently
	static gtime_t last_update = 0_ms;
	const gtime_t current_time = level.time;

	if (current_time - last_update < 99_ms) {
		return;
	}
	last_update = current_time;

	// Get configstring once
	const std::string_view current_msg = gi.get_configstring(CONFIG_HORDEMSG);
	if (current_msg.empty()) {
		return;
	}

	// Track success
	bool update_successful = false;

	// Process active players efficiently
	for (auto* player : active_players()) {
		// Skip null checks and directly check voted_map
		if (player && player->client &&
			(player->client->voted_map[0] == '\0')) {
			player->client->ps.stats[STAT_HORDEMSG] = CONFIG_HORDEMSG;
			update_successful = true;
		}
	}

	// Update HUD state tracking
	if (update_successful) {
		g_horde_local.last_successful_hud_update = current_time;
		g_horde_local.failed_updates_count = 0;
	}
	else {
		// If we exceed failure threshold, clear message
		if (++g_horde_local.failed_updates_count > 5) {
			ClearHordeMessage();
			g_horde_local.reset_hud_state();
		}
	}
}

// Implementación de UpdateHordeMessage
void UpdateHordeMessage(std::string_view message, gtime_t duration = 5_sec) {
	// Early validation for empty messages
	if (message.empty() || duration <= 0_ms) {
		ClearHordeMessage();
		return;
	}

	// Get current message once
	const char* current_msg = gi.get_configstring(CONFIG_HORDEMSG);

	// Only update if changed
	if (!current_msg || strcmp(current_msg, message.data()) != 0) {
		gi.configstring(CONFIG_HORDEMSG, message.data());
	}

	// Set duration
	horde_message_end_time = level.time + duration;
}

void ClearHordeMessage() {
	std::string_view const current_msg = gi.get_configstring(CONFIG_HORDEMSG);
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
	// Instead of clearing the map, we'll reset every entry to default values
	// This is much simpler with the array approach
	for (size_t i = 0; i < MAX_EDICTS; i++) {
		spawnPointsData.data[i] = SpawnPointData{};
	}

	lastSpawnPointTime.clear();
	lastMonsterSpawnTime.clear();

	const MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level;
	const int32_t humanPlayers = GetNumHumanPlayers();

	// Get base cooldown based on map size
	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);

	// Apply scale based on level
	const float cooldownScale = CalculateCooldownScale(currentLevel, mapSize);
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	// Additional adjustments (reduced but maintained for balance)
	if (humanPlayers > 1) {
		const float playerAdjustment = 1.0f - (std::min(humanPlayers - 1, 3) * 0.05f);
		SPAWN_POINT_COOLDOWN *= playerAdjustment;
	}

	// Difficulty mode adjustments with safety verification
	if ((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) {
		SPAWN_POINT_COOLDOWN *= 0.95f;
	}

	// Apply absolute limits
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN, 1.0_sec, 3.0_sec);
}

void ResetAllSpawnAttempts() noexcept {
	// Find all active spawn points and reset them
	for (unsigned int i = 1; i < globals.num_edicts; i++) {
		edict_t* ent = &g_edicts[i];
		if (ent && ent->inuse && ent->classname &&
			!strcmp(ent->classname, "info_player_deathmatch")) {
			auto& data = spawnPointsData[ent];
			data.attempts = 0;
			data.isTemporarilyDisabled = false;
			data.cooldownEndsAt = 0_sec;
		}
	}
}

// Función modificada para resetear la lista de jefes recientes
static void ResetRecentBosses() noexcept {
	recent_bosses.clear();
}

void ResetWaveAdvanceState() noexcept;

static bool CheckRemainingMonstersCondition(WaveEndReason& reason) {
	// Cache frequently used values
	const gtime_t currentTime = level.time;

	// Check for fast path conditions first
	if (allowWaveAdvance || Horde_AllMonstersDead()) {
		reason = WaveEndReason::AllMonstersDead;
		ResetWaveAdvanceState();
		return true;
	}

	// Maximum time limit check
	if (currentTime >= g_independent_timer_start + g_lastParams.independentTimeThreshold) {
		reason = WaveEndReason::TimeLimitReached;
		return true;
	}

	// Get remaining monster count - simple and reliable
	const int32_t remainingMonsters = CalculateRemainingMonsters();

	// Initialize wave end time if not set
	if (g_horde_local.waveEndTime == 0_sec) {
		g_horde_local.waveEndTime = g_independent_timer_start + g_lastParams.independentTimeThreshold;
	}

	// Handle wave deployment completion
	if (next_wave_message_sent && !g_horde_local.conditionTriggered) {
		g_independent_timer_start = currentTime;
		g_horde_local.conditionTriggered = true;
		g_horde_local.conditionTimeThreshold = g_lastParams.timeThreshold;
		g_horde_local.waveEndTime = currentTime + g_horde_local.conditionTimeThreshold;
	}

	// Determine if conditions should trigger countdown
	if (!g_horde_local.conditionTriggered) {
		// Calculate percentage of monsters remaining
		float percentageRemaining = 0.0f;
		if (g_totalMonstersInWave > 0) {
			percentageRemaining = static_cast<float>(remainingMonsters) / g_totalMonstersInWave;
		}

		const bool maxMonstersReached = remainingMonsters <= g_lastParams.maxMonsters;
		const bool lowPercentageReached = percentageRemaining <= g_lastParams.lowPercentageThreshold;

		if (maxMonstersReached || lowPercentageReached) {
			g_horde_local.conditionTriggered = true;
			g_horde_local.conditionStartTime = currentTime;

			// Choose appropriate timer threshold
			g_horde_local.conditionTimeThreshold = (maxMonstersReached && lowPercentageReached) ?
				std::min(g_lastParams.timeThreshold, g_lastParams.lowPercentageTimeThreshold) :
				(maxMonstersReached ? g_lastParams.timeThreshold : g_lastParams.lowPercentageTimeThreshold);

			g_horde_local.waveEndTime = currentTime + g_horde_local.conditionTimeThreshold;

			// Special handling for very few monsters
			if (remainingMonsters <= MONSTERS_FOR_AGGRESSIVE_REDUCTION) {
				const gtime_t reduction = AGGRESSIVE_TIME_REDUCTION_PER_MONSTER *
					(MONSTERS_FOR_AGGRESSIVE_REDUCTION - remainingMonsters);
				g_horde_local.waveEndTime = std::min(g_horde_local.waveEndTime, currentTime + reduction);
			}
		}
	}

	// Handle time warnings and check for time expiration
	if (g_horde_local.conditionTriggered) {
		const gtime_t remainingTime = g_horde_local.waveEndTime - currentTime;

		// Process time warnings
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

		// Check if time has expired
		if (currentTime >= g_horde_local.waveEndTime) {
			reason = WaveEndReason::MonstersRemaining;
			return true;
		}
	}

	return false;
}

//
// game resetting
//
// Add this to your reset functions to clear the memory when starting a new game
void ResetWaveMemory() {
	previous_wave_types.fill(MonsterWaveType::None);
	wave_memory_index = 0;

}

void ResetGame() {

	// Si ya se ha ejecutado una vez, retornar inmediatamente
	if (hasBeenReset) {
		gi.Com_PrintFmt("PRINT: Reset already performed, skipping...\n");
		return;
	}

	// Establecer el flag al inicio de la ejecución
	hasBeenReset = true;

	ResetWaveMemory();

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

	g_adjusted_monster_cap = 0;

	// Reset global variables that ARE accessible
	spawn_point_cache.clear();
	item_cache.clear();

	// Reset static counters (only ones accessible at this scope)
	consistent_zero_counts = 0;
	counter_mismatch_frames = 0;

	// Resetear todas las variables globales
	horde_message_end_time = 0_sec;
	g_totalMonstersInWave = 0;

	// Resetear flags de control
	g_maxMonstersReached = false;
	g_lowPercentageTriggered = false;

	// Limpiar cachés
	CleanupSpawnPointCache();
	for (size_t i = 0; i < MAX_EDICTS; i++) {
		spawnPointsData.data[i] = SpawnPointData{};
	}
	lastMonsterSpawnTime.clear();
	lastSpawnPointTime.clear();

	// Reset static function variables
	ResetSpawnMonsterVars();
	ResetFrameTimers();
	ResetQueueMonitorVars();

	// Reiniciar variables de estado global
	g_horde_local = HordeState(); // Asume que HordeState tiene un constructor por defecto adecuado
	current_wave_level = 0;
	boss_spawned_for_wave = false;
	next_wave_message_sent = false;
	allowWaveAdvance = false;

	// Reiniciar otras variables relevantes
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
	current_wave_type = MonsterWaveType::None;

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

	if (!developer->integer)
		gi.cvar_set("bot_pause", "0");

	// Reset gameplay configuration variables
	gi.cvar_set("g_chaotic", "0");
	gi.cvar_set("g_insane", "0");
	gi.cvar_set("g_hardcoop", "0");
	gi.cvar_set("dm_monsters", "0");
	gi.cvar_set("timelimit", "60");
	gi.cvar_set("set cheats 0 s", "");
	gi.cvar_set("ai_damage_scale", "1");
	gi.cvar_set("ai_allow_dm_spawn", "1");
	gi.cvar_set("g_damage_scale", "1");

	// Reset bonuses
	gi.cvar_set("g_vampire", "0");
	gi.cvar_set("g_startarmor", "0");
	gi.cvar_set("g_ammoregen", "0");
	gi.cvar_set("g_upgradeproxs", "0");
	gi.cvar_set("g_piercingbeam", "0");
	gi.cvar_set("g_tracedbullets", "0");
	gi.cvar_set("g_energyshells", "0");
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

// Replace the existing CalculateRemainingMonsters() function
int32_t CalculateRemainingMonsters() noexcept {
	// Simple counter approach - more reliable from old version
	const int32_t remaining = level.total_monsters - level.killed_monsters;
	return std::max(0, remaining);  // Ensure non-negative
}
// Enhanced version of CheckRemainingMonstersCondition
static bool CheckRemainingMonstersCondition(const MapSize& mapSize, WaveEndReason& reason) {
	// Cache frequently used values
	const gtime_t currentTime = level.time;
	const int32_t remainingMonsters = CalculateRemainingMonsters();

	// Transition timer safely when deployment completes
	if (next_wave_message_sent && !g_horde_local.conditionTriggered) {
		// Reset the independent timer when deployment completes
		g_independent_timer_start = currentTime;
		g_horde_local.waveEndTime = currentTime + g_lastParams.timeThreshold;
		g_horde_local.conditionTriggered = true;
		g_horde_local.conditionTimeThreshold = g_lastParams.timeThreshold;

		if (developer->integer) {
			gi.Com_PrintFmt("Debug: Timer reset after wave deployment. New end time: {:.2f}s\n",
				g_lastParams.timeThreshold.seconds());
		}
	}

	// First priority - independent time limit
	if (currentTime >= g_independent_timer_start + g_lastParams.independentTimeThreshold) {
		reason = WaveEndReason::TimeLimitReached;
		return true;
	}

	// Second priority - all monsters are dead or wave advance flag set
	if (allowWaveAdvance || Horde_AllMonstersDead()) {
		reason = WaveEndReason::AllMonstersDead;
		ResetWaveAdvanceState();
		return true;
	}

	// Calculate percentage remaining (for conditional timers)
	// Make sure g_totalMonstersInWave is at least 1 to avoid division by zero
	uint16_t safeTotal = g_totalMonstersInWave;
	if (safeTotal < 1) safeTotal = 1;
	const float percentageRemaining = static_cast<float>(remainingMonsters) / safeTotal;

	// Initialize end time if needed
	if (g_horde_local.waveEndTime == 0_sec) {
		g_horde_local.waveEndTime = g_independent_timer_start + g_lastParams.independentTimeThreshold;
	}

	// Check if we should trigger conditional timers
	if (!g_horde_local.conditionTriggered && !next_wave_message_sent) {
		const bool maxMonstersReached = remainingMonsters <= g_lastParams.maxMonsters;
		const bool lowPercentageReached = percentageRemaining <= g_lastParams.lowPercentageThreshold;

		if (maxMonstersReached || lowPercentageReached) {
			g_horde_local.conditionTriggered = true;
			g_horde_local.conditionStartTime = currentTime;

			// Choose appropriate timer threshold based on conditions
			if (maxMonstersReached && lowPercentageReached) {
				// Both conditions met - use the shorter timer
				g_horde_local.conditionTimeThreshold = std::min(
					g_lastParams.timeThreshold, g_lastParams.lowPercentageTimeThreshold);
			}
			else {
				// Only one condition met
				g_horde_local.conditionTimeThreshold = maxMonstersReached ?
					g_lastParams.timeThreshold : g_lastParams.lowPercentageTimeThreshold;
			}

			g_horde_local.waveEndTime = currentTime + g_horde_local.conditionTimeThreshold;

			// Apply special handling for high waves with few monsters
			if (current_wave_level >= 15 && remainingMonsters <= 5) {
				// More aggressive timeout for higher waves with few monsters
				const float reduction_factor = 0.6f;
				g_horde_local.waveEndTime = currentTime +
					(g_horde_local.conditionTimeThreshold * reduction_factor);

				if (developer->integer) {
					gi.Com_PrintFmt("High wave with few monsters: reduced timeout by {}%\n",
						static_cast<int>((1.0f - reduction_factor) * 100));
				}
			}

			// Aggressive reduction for very low monster counts
			if (remainingMonsters <= MONSTERS_FOR_AGGRESSIVE_REDUCTION) {
				gtime_t aggressive_time = AGGRESSIVE_TIME_REDUCTION_PER_MONSTER *
					(MONSTERS_FOR_AGGRESSIVE_REDUCTION - remainingMonsters);

				// Scale by wave level for higher waves
				if (current_wave_level >= 15) {
					aggressive_time *= 0.7f;  // 30% faster for high waves
				}

				g_horde_local.waveEndTime = std::min(
					g_horde_local.waveEndTime,
					currentTime + aggressive_time
				);
			}
		}
	}

	// Handle time warnings
	if (g_horde_local.conditionTriggered) {
		const gtime_t remainingTime = g_horde_local.waveEndTime - currentTime;

		for (size_t i = 0; i < WARNING_TIMES.size(); ++i) {
			if (!g_horde_local.warningIssued[i] &&
				remainingTime <= gtime_t::from_sec(WARNING_TIMES[i]) &&
				remainingTime > gtime_t::from_sec(WARNING_TIMES[i]) - 1_sec) {
				gi.LocBroadcast_Print(PRINT_HIGH, "{} seconds remaining!\n",
					static_cast<int>(WARNING_TIMES[i]));
				g_horde_local.warningIssued[i] = true;
			}
		}

		// Check if time has expired
		if (currentTime >= g_horde_local.waveEndTime) {
			reason = WaveEndReason::MonstersRemaining;
			return true;
		}
	}

	// Additional check for wave 15+ with very few monsters and slow progress
	if (current_wave_level >= 15 && remainingMonsters <= 3 && g_horde_local.conditionTriggered) {
		// Get elapsed time since condition triggered
		const gtime_t elapsed = currentTime - g_horde_local.conditionStartTime;

		// If we've spent over 70% of allowed time and few monsters remain, force completion
		if (elapsed >= (g_horde_local.conditionTimeThreshold * 0.7f)) {
			//if (developer->integer) {
			//	gi.Com_PrintFmt("High wave failsafe: Only {} monsters remain after {}% of timer\n",
			//		remainingMonsters,
			//		static_cast<int>((elapsed / g_horde_local.conditionTimeThreshold) * 100));
			//}
			reason = WaveEndReason::MonstersRemaining;
			return true;
		}
	}

	return false;
}

void ResetWaveAdvanceState() noexcept {
	// Reset independent timer
	g_independent_timer_start = level.time;

	// Reset condition variables
	g_horde_local.conditionTriggered = false;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTimeThreshold = 0_sec;
	g_horde_local.timeWarningIssued = false;
	g_horde_local.waveEndTime = 0_sec;

	// Make sure warning flags are reset
	for (size_t i = 0; i < 4; i++) {
		g_horde_local.warningIssued[i] = false;
	}

	allowWaveAdvance = false;
	g_horde_local.lastPrintTime = 0_sec;
	g_totalMonstersInWave = 0;
	boss_spawned_for_wave = false;

	g_lastWaveNumber = -1;
	g_lastNumHumanPlayers = -1;

	// Reset monster detection variables
	consistent_zero_counts = 0;
	counter_mismatch_frames = 0;
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
		[](const edict_t* const player) noexcept {
			return ClientIsSpectating(player->client);
		});
}

// Implementación de DisplayWaveMessage
static void DisplayWaveMessage(gtime_t duration = 5_sec) {
	static const std::array<const char*, 4> messages = {
		"Horde Menu available upon opening Inventory or using TURTLE on POWERUP WHEEL\n\nMAKE THEM PAY!\n",
		"Welcome to Hell.\n\nUse FlipOff <Key> looking at walls to spawn lasers (cost: 25 cells)\n",
		"New Tactics!\n\nTeslas can now be placed on walls and ceilings!\n\nUse them wisely!", 
		"Improved Traps!\n\nTraps are reutilizable after 5secs of eating a strogg!\n\nExploding if strogg is bigger!, up to 60 seconds of life!"
	};

	// Usar distribución uniforme con mt_rand
	std::uniform_int_distribution<size_t> dist(0, messages.size() - 1);
	const size_t choice = dist(mt_rand);
	UpdateHordeMessage(messages[choice], duration);
}

void HandleWaveCleanupMessage(const MapSize& mapSize, const WaveEndReason reason) noexcept {
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
				// Activar chaotic2 si es mapa pequeño Y hay 3+ jugadores, sino chaotic1
				gi.cvar_set("g_chaotic", (mapSize.isSmallMap && numHumanPlayers >= 3) ? "2" : "1");
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

static void AnnounceIncomingWave(gtime_t duration = 3_sec) {
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

struct StuckMonsterSpawnFilter {
	bool operator()(edict_t* ent) const {
		if (!ent || !ent->inuse || !ent->classname ||
			strcmp(ent->classname, "info_player_deathmatch") != 0 ||
			ent->style == 1)  // Exclude flying spawns
			return false;

		// Cooldown check - direct array access
		if (level.time < spawnPointsData[ent].teleport_cooldown)
			return false;

		if (IsSpawnPointOccupied(ent))
			return false;

		// Check proximity to players
		for (const auto* const player : active_players_no_spect()) {
			if ((ent->s.origin - player->s.origin).length() < 512.0f) {
				return true;  // Accept spawn points near players
			}
		}
		return false; // No player nearby
	}
};

bool CheckAndTeleportStuckMonster(edict_t* self) {
	// Early returns
	if (!self || !self->inuse || self->deadflag ||
		self->monsterinfo.IS_BOSS || level.intermissiontime || !g_horde->integer)
		return false;

	if (!strcmp(self->classname, "misc_insane"))
		return false;

	constexpr gtime_t NO_DAMAGE_TIMEOUT = 25_sec;
	constexpr gtime_t STUCK_CHECK_TIME = 10_sec;
	constexpr gtime_t TELEPORT_COOLDOWN = 15_sec;

	// If can see enemy, don't teleport
	if (self->monsterinfo.issummoned ||
		(self->enemy && self->enemy->inuse && visible(self, self->enemy, false))) {
		self->monsterinfo.was_stuck = false;
		self->monsterinfo.stuck_check_time = 0_sec;
		return false;
	}

	// Check general teleport cooldown
	if (self->teleport_time && level.time < self->teleport_time + TELEPORT_COOLDOWN)
		return false;

	// For non-water damage, check stuck conditions
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

	// Hide from clients before unlinking
	self->svflags |= SVF_NOCLIENT;
	gi.unlinkentity(self);

	// Use StuckMonsterSpawnFilter with SelectRandomSpawnPoint
	StuckMonsterSpawnFilter filter;
	const edict_t* const spawn_point = SelectRandomSpawnPoint(filter);
	if (!spawn_point) {
		// Show entity again if we fail early
		self->svflags &= ~SVF_NOCLIENT;
		gi.linkentity(self);
		return false;
	}

	// Store old values and attempt teleport...
	const vec3_t old_velocity = self->velocity;
	const vec3_t old_origin = self->s.origin;

	// Set new position...
	self->s.origin = spawn_point->s.origin;
	self->s.old_origin = spawn_point->s.origin;
	self->velocity = vec3_origin;

	// Check if teleport succeeded
	bool teleport_success = true;
	if (!(self->flags & (FL_FLY | FL_SWIM)))
		teleport_success = M_droptofloor(self);

	if (teleport_success && !gi.trace(self->s.origin, self->mins, self->maxs,
		self->s.origin, self, MASK_MONSTERSOLID).startsolid) {
		// Make visible again only after successful teleport
		self->svflags &= ~SVF_NOCLIENT;
		gi.linkentity(self);

		// Effects after we're visible
		gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
		SpawnGrow_Spawn(self->s.origin, 80.0f, 10.0f);

		self->monsterinfo.was_stuck = false;
		self->monsterinfo.stuck_check_time = 0_sec;
		self->monsterinfo.react_to_damage_time = level.time;
		return true;
	}

	// Restore position if teleport failed
	self->s.origin = old_origin;
	self->s.old_origin = old_origin;
	self->velocity = old_velocity;

	// Make visible again before final link
	self->svflags &= ~SVF_NOCLIENT;
	gi.linkentity(self);
	return false;
}

// Function to track created entities
void OnEntityCreated(edict_t* ent) {
	if (!ent || !ent->inuse)
		return;

	// Add entity tracking logic here if needed
	// For most implementations, this can be a simple stub function
	// that can be expanded later with actual tracking needs

	if (developer->integer > 2) {
		gi.Com_PrintFmt("Entity created: {} ({})\n",
			ent->classname ? ent->classname : "unknown",
			ent - g_edicts);
	}
}

void CheckForMonsterDeathsInSpawningState(edict_t* monster) {
	if (!monster || !monster->inuse)
		return;

	// Check if monster was spawned during spawning state and died in that state
	if (monster->spawned_in_spawn_state && g_horde_local.state == horde_state_t::spawning) {
		// Track monsters killed during spawning
		static int32_t spawn_state_deaths = 0;
		static gtime_t last_death_time = 0_sec;

		// Reset counter if too much time has passed
		if (level.time - last_death_time > 8_sec) {
			spawn_state_deaths = 0;
		}

		spawn_state_deaths++;
		last_death_time = level.time;

		if (developer->integer) {
			gi.Com_PrintFmt("Monster killed during spawning state ({} total)\n", spawn_state_deaths);
		}

		// If multiple monsters die during spawning, transition to active wave
		if (spawn_state_deaths >= 4) {
			if (developer->integer) {
				gi.Com_PrintFmt("{} monsters killed during spawning. Forcing transition to active_wave\n",
					spawn_state_deaths);
			}

			// Force transition to active_wave
			if (!next_wave_message_sent) {
				VerifyAndAdjustBots();
				//gi.LocBroadcast_Print(PRINT_CENTER,
				//	"\n\n\nWave deployment interrupted. Combat phase activated.\n");
				next_wave_message_sent = true;
			}

			g_horde_local.state = horde_state_t::active_wave;
			spawn_state_deaths = 0; // Reset for next wave
		}
	}

	// Clear flags once monster is dead
	monster->was_spawned_by_horde = false;
	monster->spawned_in_spawn_state = false;
}

static edict_t* SpawnMonsters() {
	if (developer->integer == 2)
		return nullptr;

	// Static cache for spawn point management
	static struct {
		std::vector<edict_t*> available_spawns;
		gtime_t last_spawn_time = 0_sec;
		int32_t consecutive_failures = 0;
	} spawn_cache;

	if (need_spawn_cache_reset) {
		spawn_cache.available_spawns.clear();
		spawn_cache.last_spawn_time = 0_sec;
		spawn_cache.consecutive_failures = 0;
		need_spawn_cache_reset = false;
	}

	// Clear previous data
	spawn_cache.available_spawns.clear();

	// Cache variables
	const MapSize& mapSize = g_horde_local.current_map_size;
	const horde_state_t current_state = g_horde_local.state;
	const gtime_t current_time = level.time;

	// Define monster caps
	const int32_t softCap = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap :
		(mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
			(mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP));

	// Hard cap is 20% higher than soft cap to prevent excessive spawning
	const int32_t hardCap = static_cast<int32_t>(softCap * 1.2f);

	// Get current monster count
	const int32_t activeMonsters = CalculateRemainingMonsters();

	// HARD CAP CHECK - Emergency brake if monster count exceeds hard cap
	if (activeMonsters >= hardCap) {
		if (developer->integer) {
			gi.Com_PrintFmt("CRITICAL: Hard monster cap exceeded ({}/{}). Halting spawns.\n",
				activeMonsters, hardCap);
		}

		// Force transition if in spawning state
		if (current_state == horde_state_t::spawning) {
			g_horde_local.state = horde_state_t::active_wave;
			gi.Com_PrintFmt("Force state transition due to hard cap breach\n");
		}

		// Clear spawn pool to prevent further attempts
		g_horde_local.num_to_spawn = 0;

		return nullptr;
	}

	// Early exit checks with better debugging
	if (activeMonsters >= softCap) {
		if (developer->integer) {
			gi.Com_PrintFmt("SpawnMonsters: Already at monster cap ({}/{})\n",
				activeMonsters, softCap);
		}
		return nullptr;
	}

	// Calculate available space for new monsters
	int32_t availableSpace = softCap - activeMonsters;

	// Check if we have anything to spawn
	if (g_horde_local.num_to_spawn <= 0) {
		// Try to rescue from consecutive failures
		if (spawn_cache.consecutive_failures > 3 && g_horde_local.queued_monsters > 0) {
			// Emergency transfer from queue
			const int32_t emergency_transfer = std::min(g_horde_local.queued_monsters, 3);
			g_horde_local.num_to_spawn = emergency_transfer;
			g_horde_local.queued_monsters -= emergency_transfer;

			if (developer->integer) {
				gi.Com_PrintFmt("Emergency transfer: {} monsters from queue after consecutive failures\n",
					emergency_transfer);
			}

			spawn_cache.consecutive_failures = 0;
		}
		else {
			if (developer->integer > 1) {
				gi.Com_PrintFmt("SpawnMonsters: No monsters to spawn (num_to_spawn={}, queue={})\n",
					g_horde_local.num_to_spawn, g_horde_local.queued_monsters);
			}
			return nullptr;
		}
	}

	// Ensure num_to_spawn doesn't exceed available space
	g_horde_local.num_to_spawn = std::min(g_horde_local.num_to_spawn, availableSpace);

	// Calculate batch size for this spawn attempt
	const int32_t base_batch = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
	int32_t monsters_per_batch = std::min({
		g_horde_local.num_to_spawn,
		base_batch,
		availableSpace
		});

	// Always try to spawn at least 1 if we have any (prevents stuck queues)
	const int32_t spawnable = std::max(1, monsters_per_batch);

	if (developer->integer > 1) {
		gi.Com_PrintFmt("Spawning: Attempting {} monsters (pool={}, space={})\n",
			spawnable, g_horde_local.num_to_spawn, availableSpace);
	}

	// Collect valid spawn points
	SpawnMonsterFilter filter{ current_time };
	auto spawnPoints = monster_spawn_points();

	for (edict_t* point : spawnPoints) {
		if (filter(point)) {
			spawn_cache.available_spawns.push_back(point);
		}
	}

	// If no spawns available, try emergency recovery measures
	if (spawn_cache.available_spawns.empty()) {
		spawn_cache.consecutive_failures++;

		if (spawn_cache.consecutive_failures >= 2) {
			// Reset disabled spawn points in emergency
			for (edict_t* point : spawnPoints) {
				auto& data = spawnPointsData[point];
				// Clear any disabled states
				if (data.isTemporarilyDisabled || data.attempts > 0) {
					data.isTemporarilyDisabled = false;
					data.attempts = 0;
					data.cooldownEndsAt = 0_sec;

					// Re-check if usable now
					if (filter(point)) {
						spawn_cache.available_spawns.push_back(point);
					}
				}
			}

			if (developer->integer) {
				gi.Com_PrintFmt("Emergency spawn point reset: recovered {} points\n",
					spawn_cache.available_spawns.size());
			}
		}

		// If still no points available, return monsters to queue
		if (spawn_cache.available_spawns.empty()) {
			if (developer->integer) {
				gi.Com_PrintFmt("NO SPAWN POINTS AVAILABLE: Returning {} to queue\n",
					g_horde_local.num_to_spawn);
			}

			// Return monsters to queue if in spawning state
			if (current_state == horde_state_t::spawning) {
				g_horde_local.queued_monsters += g_horde_local.num_to_spawn;
			}

			g_horde_local.num_to_spawn = 0;
			return nullptr;
		}
	}
	else {
		// Reset failure counter on success
		spawn_cache.consecutive_failures = 0;
	}

	// Initialize for spawn loop
	edict_t* last_spawned = nullptr;
	const float drop_chance = g_horde_local.level <= 2 ? 0.8f :
		g_horde_local.level <= 7 ? 0.6f : 0.45f;

	int32_t spawned_count = 0;

	// Main spawn loop - attempt to spawn up to 'spawnable' monsters
	for (int32_t i = 0; i < spawnable && g_horde_local.num_to_spawn > 0; ++i) {
		// Double-check monster cap during loop
		if (CalculateRemainingMonsters() >= softCap) {
			if (developer->integer) {
				gi.Com_PrintFmt("SpawnMonsters: Cap reached mid-loop ({}/{})\n",
					CalculateRemainingMonsters(), softCap);
			}
			break;
		}

		// Get random spawn point
		if (spawn_cache.available_spawns.empty())
			break;

		// Select random spawn point
		size_t spawn_idx = irandom(spawn_cache.available_spawns.size());
		edict_t* spawn_point = spawn_cache.available_spawns[spawn_idx];

		// Remove from available points and set cooldown
		spawn_cache.available_spawns[spawn_idx] = spawn_cache.available_spawns.back();
		spawn_cache.available_spawns.pop_back();
		spawnPointsData[spawn_point].teleport_cooldown = current_time + 1.5_sec;

		// Get monster type for this spawn
		const char* monster_classname = G_HordePickMonster(spawn_point);
		if (!monster_classname)
			continue;

		// Create the monster entity
		if (edict_t* monster = G_Spawn()) {
			monster->classname = monster_classname;
			monster->s.origin = G_ProjectSource(spawn_point->s.origin,
				vec3_t{ 0, 0, 8 },
				vec3_t{ 1,0,0 },
				vec3_t{ 0,1,0 });
			monster->s.angles = spawn_point->s.angles;
			monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
			monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
			monster->monsterinfo.last_sentrygun_target_time = 0_ms;

			// Mark it as spawned by horde
			monster->was_spawned_by_horde = true;

			// If in spawning state, mark it specifically
			if (g_horde_local.state == horde_state_t::spawning) {
				monster->spawned_in_spawn_state = true;
			}

			// Spawn the entity
			ED_CallSpawn(monster);

			// Check if spawn succeeded
			if (monster->inuse) {
				// Apply armor based on wave level
				if (g_horde_local.level >= 14 && monster->monsterinfo.power_armor_type == IT_NULL)
					SetMonsterArmor(monster);

				// Random item drop chance
				if (frandom() < drop_chance)
					monster->item = G_HordePickItem();

				// Visual effects
				const vec3_t spawn_pos = monster->s.origin + vec3_t{ 0, 0, monster->mins[2] };
				SpawnGrow_Spawn(spawn_pos, 80.0f, 10.0f);
				gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

				// Update spawn counters
				--g_horde_local.num_to_spawn;
				++spawned_count;
				last_spawned = monster;

				// Update tracking variables
				spawn_cache.last_spawn_time = current_time;

				// Track successful spawn for this point
				OnSuccessfulSpawn(spawn_point);

				// Update total monsters counter with safety check
				if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
					++g_totalMonstersInWave;
				}
				else {
					gi.Com_PrintFmt("WARNING: Total monsters counter maxed out\n");
					++g_horde_local.num_to_spawn; // Don't lose track
				}
			}
			else {
				// Free failed entity
				G_FreeEdict(monster);
			}
		}
	}

	// Debug output
	if (developer->integer) {
		if (spawned_count > 0) {
			gi.Com_PrintFmt("Spawned {} monsters, {} remaining to spawn, {} in queue\n",
				spawned_count, g_horde_local.num_to_spawn, g_horde_local.queued_monsters);
		}
		else if (g_horde_local.num_to_spawn > 0) {
			gi.Com_PrintFmt("Failed to spawn any monsters ({} remaining in pool)\n",
				g_horde_local.num_to_spawn);

			// Increment failure counter
			spawn_cache.consecutive_failures++;
		}
	}

	// Handle complete spawn failure
	if (spawned_count == 0 && g_horde_local.num_to_spawn > 0) {
		// If in spawning state, return to queue
		if (current_state == horde_state_t::spawning && spawn_cache.consecutive_failures > 2) {
			gi.Com_PrintFmt("Returning {} monsters to queue after failed spawns\n",
				g_horde_local.num_to_spawn);

			g_horde_local.queued_monsters += g_horde_local.num_to_spawn;
			g_horde_local.num_to_spawn = 0;
		}
	}

	// Set next spawn time
	SetNextMonsterSpawnTime(mapSize);

	return last_spawned;
}

static void SetMonsterArmor(edict_t* monster) {
	// Cache frequently used constants to avoid recalculating
	static constexpr float HEALTH_RATIO_POW = 1.1f;
	static constexpr float SIZE_FACTOR_POW = 0.7f;
	static constexpr float MASS_FACTOR_POW = 0.6f;
	static constexpr float BASE_ARMOR = 75.0f;
	static constexpr float MAX_HEALTH_ARMOR_FACTOR = 0.2f;

	// Get spawn temp once
	const spawn_temp_t& st = ED_GetSpawnTemp();

	// Assign default armor type if not specified
	if (!st.was_key_specified("power_armor_power")) {
		monster->monsterinfo.armor_type = IT_ARMOR_COMBAT;
	}

	// Calculate base factors once
	const float health_ratio = monster->health / static_cast<float>(monster->max_health);
	const float size_factor = (monster->maxs - monster->mins).length() / 64.0f;
	const float mass_factor = std::min(monster->mass / 200.0f, 1.5f);

	// Pre-compute level scaling factor
	float level_scaling;
	// Use if-else instead of switch for better optimization with constants
	if (current_wave_level <= 15) {
		level_scaling = 1.0f + (current_wave_level * 0.04f);
	}
	else if (current_wave_level <= 25) {
		level_scaling = 1.6f + ((current_wave_level - 15) * 0.06f);
	}
	else {
		level_scaling = 2.2f + ((current_wave_level - 25) * 0.08f);
	}

	// Compute base armor power
	float armor_power = (BASE_ARMOR + monster->max_health * MAX_HEALTH_ARMOR_FACTOR);

	// Use pre-computed powers or fast approximations
	armor_power *= powf(health_ratio, HEALTH_RATIO_POW);
	armor_power *= powf(size_factor, SIZE_FACTOR_POW);
	armor_power *= powf(mass_factor, MASS_FACTOR_POW);
	armor_power *= level_scaling;

	// Apply level-based multiplier
	float armor_multiplier = current_wave_level <= 30 ? 0.4f :
		current_wave_level <= 40 ? 0.5f : 0.6f;
	armor_power *= armor_multiplier;

	// Difficulty adjustment
	if (g_insane->integer) {
		armor_power *= 1.2f;
	}
	else if (g_chaotic->integer) {
		armor_power *= 1.1f;
	}

	// Apply random factor - use faster random generation
	const float random_factor = 0.9f + (0.2f * frandom());
	armor_power *= random_factor;

	// High-level bonus
	if (current_wave_level > 25) {
		armor_power *= 1.0f + ((current_wave_level - 25) * 0.03f);
	}

	// Calculate min/max armor values efficiently
	const int min_armor = std::max(25, static_cast<int>(monster->max_health * 0.08f));
	const int max_armor = static_cast<int>(monster->max_health *
		(current_wave_level > 25 ? 1.5f : 1.2f));

	// Clamp final armor value 
	const int final_armor = std::clamp(static_cast<int>(armor_power), min_armor, max_armor);

	// Assign power directly based on armor type
	if (monster->monsterinfo.power_armor_type == IT_NULL) {
		monster->monsterinfo.power_armor_power = 0;
	}
	else if (monster->monsterinfo.power_armor_type == IT_ITEM_POWER_SHIELD ||
		monster->monsterinfo.power_armor_type == IT_ITEM_POWER_SCREEN) {
		monster->monsterinfo.power_armor_power = final_armor;
	}

	// Assign armor power
	if (monster->monsterinfo.armor_type == IT_NULL) {
		monster->monsterinfo.armor_power = 0;
	}
	else if (monster->monsterinfo.armor_type == IT_ARMOR_COMBAT) {
		monster->monsterinfo.armor_power = final_armor;
	}
}

static void SetNextMonsterSpawnTime(const MapSize& mapSize) {
	// Original spawn time ranges
	constexpr std::array<std::pair<gtime_t, gtime_t>, 3> BASE_SPAWN_TIMES = { {
		{0.6_sec, 0.8_sec},  // Small maps
		{0.8_sec, 1.0_sec},  // Medium maps
		{0.4_sec, 0.6_sec}   // Big maps
	} };

	// Apply early wave modifier (waves 1-10)
	float earlyWaveMultiplier = 1.0f;
	if (g_horde_local.level <= 10) {
		// Start with 2.0x slower at wave 1, gradually reduce to 1.0x at wave 10
		earlyWaveMultiplier = 2.0f - ((g_horde_local.level - 1) * 0.1f);
	}

	const size_t mapIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);
	const auto& [base_min_time, base_max_time] = BASE_SPAWN_TIMES[mapIndex];

	// Apply multiplier to spawn times
	const gtime_t min_time = gtime_t::from_sec(base_min_time.seconds() * earlyWaveMultiplier);
	const gtime_t max_time = gtime_t::from_sec(base_max_time.seconds() * earlyWaveMultiplier);

	g_horde_local.monster_spawn_time = level.time + random_time(min_time, max_time);
}
// Usar enum class para mejorar la seguridad de tipos
enum class MessageType {
	Standard,
	Chaotic,
	Insane
};

static void CalculateTopDamager(PlayerStats& topDamager, float& percentage) {
	constexpr int32_t MAX_DAMAGE = 100000;
	int32_t total_damage = 0;
	topDamager = PlayerStats(); // Reset stats

	// First pass - calculate total damage
	for (const auto& player : active_players()) {
		if (!player || !player->client || !player->inuse)
			continue;

		// Fix 1: Initialize const variable with explicit casting to resolve type ambiguity
		const int32_t player_damage = static_cast<int32_t>(std::min(
			static_cast<int32_t>(player->client->total_damage),
			MAX_DAMAGE));

		if (player_damage > 0) {
			total_damage += player_damage;
		}
	}

	// Second pass - find top damager
	for (const auto& player : active_players()) {
		if (!player || !player->client || !player->inuse)
			continue;

		// Fix 2: Same explicit casting approach for the second instance
		const int32_t player_damage = static_cast<int32_t>(std::min(
			static_cast<int32_t>(player->client->total_damage),
			MAX_DAMAGE));

		if (player_damage > topDamager.total_damage) {
			topDamager.total_damage = player_damage;
			topDamager.player = player;
		}
	}

	// Calculate percentage with extra safety
	percentage = 0.0f;
	if (total_damage > 0 && topDamager.total_damage > 0) {
		percentage = std::min(
			(static_cast<float>(topDamager.total_damage) / total_damage) * 100.0f,
			100.0f);
		percentage = std::round(percentage * 100.0f) / 100.0f;
	}
}

// Enumeration for different reward types with their relative weights
enum class RewardType {
	BANDOLIER = 0,
	SENTRY_GUN = 1
};

struct RewardInfo {
	item_id_t item_id;
	int weight;  // Higher weight = more common
};

// Simplified reward table without tesla ammo
static const std::unordered_map<RewardType, RewardInfo> REWARD_TABLE = {
	{RewardType::BANDOLIER, {IT_ITEM_BANDOLIER, 60}},    // More common
	{RewardType::SENTRY_GUN, {IT_ITEM_SENTRYGUN, 40}}    // Less common
};

// Function to handle reward selection and distribution
static bool GiveTopDamagerReward(const PlayerStats& topDamager, const std::string& playerName) {
	if (!topDamager.player || !topDamager.player->inuse || !topDamager.player->client)
		return false;

	try {
		// Calculate total weight
	const	int totalWeight = std::accumulate(REWARD_TABLE.begin(), REWARD_TABLE.end(), 0,
			[](int sum, const auto& pair) { return sum + pair.second.weight; });

		// Select reward
		std::uniform_int_distribution<int> dist(1, totalWeight);
		const int randomNum = dist(mt_rand);

		item_id_t selectedItemId = IT_ITEM_BANDOLIER; // Default fallback
		int currentWeight = 0;
		for (const auto& [type, info] : REWARD_TABLE) {
			currentWeight += info.weight;
			if (randomNum <= currentWeight) {
				selectedItemId = info.item_id;
				break;
			}
		}

		// Spawn and give item
		gitem_t* it = GetItemByIndex(selectedItemId);
		if (!it || !it->classname)
			return false;

		edict_t* it_ent = G_Spawn();
		if (!it_ent)
			return false;

		it_ent->classname = it->classname;
		it_ent->item = it;
		SpawnItem(it_ent, it, spawn_temp_t::empty);

		if (!it_ent->inuse)
			return false;

		Touch_Item(it_ent, topDamager.player, null_trace, true);
		if (it_ent->inuse)
			G_FreeEdict(it_ent);

		// Safe announcement
		const char* itemName = it->use_name ? it->use_name : it->classname;
		gi.LocBroadcast_Print(PRINT_HIGH, "{} receives a {} for top damage!\n",
			playerName.empty() ? "Unknown Player" : playerName.c_str(),
			itemName ? itemName : "reward");

		return true;
	}
	catch (const std::exception& e) {
		gi.Com_PrintFmt("Error giving reward: {}\n", e.what());
		return false;
	}
}

////debugging
//static void PrintRemainingMonsterCounts() {
//	std::unordered_map<std::string, int> monster_counts;
//
//	for (const auto* const ent : active_monsters()) {
//		// Ignorar monstruos con AI_DO_NOT_COUNT
//		//if (ent->monsterinfo.aiflags & AI_DO_NOT_COUNT)
//		//	continue;
//
//		// Solo contar monstruos activos y vivos
//		if (ent->inuse && !ent->deadflag && ent->health > 0) {
//			monster_counts[ent->classname]++;
//		}
//	}
//
//	// Solo mostrar advertencia si hay una discrepancia real
//	const bool has_discrepancy = (level.total_monsters != level.killed_monsters);
//	const bool has_remaining = !monster_counts.empty();
//
//	if (has_discrepancy || has_remaining) {
//		gi.Com_PrintFmt("WARNING: Monster count discrepancy detected:\n");
//		gi.Com_PrintFmt("Total monsters according to level: {}\n", level.total_monsters);
//		gi.Com_PrintFmt("Killed monsters: {}\n", level.killed_monsters);
//
//		if (has_remaining) {
//			gi.Com_PrintFmt("Remaining monster types:\n");
//			for (const auto& [classname, count] : monster_counts) {
//				gi.Com_PrintFmt("- {} : {}\n", classname, count);
//			}
//		}
//	}
//}

static void SendCleanupMessage(WaveEndReason reason) {
	// Avoid try-catch for performance in normal operation
	// Wave completion message - use switch for better performance
	switch (reason) {
	case WaveEndReason::AllMonstersDead:
		gi.LocBroadcast_Print(PRINT_HIGH, "Wave {} Completely Cleared - Perfect Victory!\n",
			g_horde_local.level);
		break;
	case WaveEndReason::MonstersRemaining:
		gi.LocBroadcast_Print(PRINT_HIGH, "Wave {} Pushed Back - But Still Threatening!\n",
			g_horde_local.level);
		break;
	case WaveEndReason::TimeLimitReached:
		gi.LocBroadcast_Print(PRINT_HIGH, "Wave {} Contained - Time Limit Reached!\n",
			g_horde_local.level);
		break;
	}

	// Calculate top damager once
	PlayerStats topDamager;
	float percentage = 0.0f;
	CalculateTopDamager(topDamager, percentage);

	// Only process if we have a valid player
	if (topDamager.player && topDamager.player->inuse && topDamager.player->client) {
		// Get player name once
		const std::string playerName = GetPlayerName(topDamager.player);

		// Send damage announcement
		gi.LocBroadcast_Print(PRINT_HIGH, "{} dealt the most damage with {}! ({}% of total)\n",
			playerName.c_str(), topDamager.total_damage, static_cast<int>(percentage));

		// Give reward and reset stats if successful
		if (GiveTopDamagerReward(topDamager, playerName)) {
			// Reset all player stats in one pass
			for (auto player : active_players()) {
				if (player->client) {
					// Group related resets together for better cache coherence
					// Damage counters
					player->client->total_damage = 0;
					player->client->lastdmg = level.time;
					player->client->dmg_counter = 0;
					player->client->ps.stats[STAT_ID_DAMAGE] = 0;

					// Respawn states
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
	// Find all active spawn points that are disabled
	for (unsigned int i = 1; i < globals.num_edicts; i++) {
		edict_t* ent = &g_edicts[i];
		if (ent && ent->inuse && ent->classname &&
			strcmp(ent->classname, "info_player_deathmatch") == 0) {

			auto& data = spawnPointsData[ent];
			if (data.isTemporarilyDisabled) {
				// Simply reset the disabled status
				data.isTemporarilyDisabled = false;
				data.attempts = 0;
				data.cooldownEndsAt = 0_sec;
			}
		}
	}
}

inline int32_t GetAdjustedMonsterCap(const MapSize& mapSize, int32_t waveLevel) {
	// Original caps
	const int32_t baseSmallCap = MAX_MONSTERS_SMALL_MAP;  // 14
	const int32_t baseMediumCap = MAX_MONSTERS_MEDIUM_MAP; // 16
	const int32_t baseBigCap = MAX_MONSTERS_BIG_MAP;      // 32

	// Only reduce for small and medium maps in early waves
	if (waveLevel <= 10 && !mapSize.isBigMap) {
		float reductionFactor = 0.6f + (waveLevel * 0.04f); // 60% at wave 1, scaling to 100% by wave 10

		if (mapSize.isSmallMap) {
			g_adjusted_monster_cap = static_cast<int32_t>(baseSmallCap * reductionFactor);
		}
		else {
			g_adjusted_monster_cap = static_cast<int32_t>(baseMediumCap * reductionFactor);
		}
	}
	else {
		// Use default caps for later waves or big maps
		g_adjusted_monster_cap = mapSize.isSmallMap ? baseSmallCap :
			(mapSize.isBigMap ? baseBigCap : baseMediumCap);
	}

	return g_adjusted_monster_cap;
}

// Add this function to your code
void CheckAndFixQueuedMonsters() {
	static gtime_t last_queue_check = 0_sec;
	static int consecutive_empty_spawns = 0;

	if (need_queue_monitor_reset) {
		last_queue_check = 0_sec;
		consecutive_empty_spawns = 0;
		need_queue_monitor_reset = false;
	}

	// Check every 5 seconds
	if (level.time - last_queue_check < 5_sec)
		return;

	last_queue_check = level.time;

	// If we're in active_wave state, have queued monsters, but nothing is spawning
	if (g_horde_local.state == horde_state_t::active_wave &&
		g_horde_local.queued_monsters > 0 &&
		g_horde_local.num_to_spawn == 0) {

		consecutive_empty_spawns++;

		// After 3 consecutive checks with no progress, take action
		if (consecutive_empty_spawns >= 3) {
			if (developer->integer) {
				gi.Com_PrintFmt("WARNING: Detected stalled queue with {} monsters in wave {}\n",
					g_horde_local.queued_monsters, current_wave_level);
			}

			// Force some monsters into num_to_spawn
			const int32_t force_spawn = std::min(g_horde_local.queued_monsters, 5);
			g_horde_local.num_to_spawn += force_spawn;
			g_horde_local.queued_monsters -= force_spawn;

			// If still no monsters can be spawned, and we're in a high wave, consider forcing completion
			if (g_horde_local.num_to_spawn == 0 && g_horde_local.queued_monsters == 0 &&
				current_wave_level >= 18 && GetStroggsNum() == 0) {
				if (developer->integer) {
					gi.Com_PrintFmt("CRITICAL: Queue empty but wave not completing. Forcing wave {}"
						"completion\n", current_wave_level);
				}
				allowWaveAdvance = true;
			}

			consecutive_empty_spawns = 0;
		}
	}
	else {
		consecutive_empty_spawns = 0; // Reset if conditions don't match
	}

	// Safety check for negative queued_monsters
	if (g_horde_local.queued_monsters < 0) {
		if (developer->integer) {
			gi.Com_PrintFmt("CRITICAL: Negative queued_monsters ({}) detected and fixed\n",
				g_horde_local.queued_monsters);
		}
		g_horde_local.queued_monsters = 0;
	}
}

// Add this function to fix stuck waves
//void ForceWaveCompletion() {
//	if (developer->integer) {
//		gi.Com_PrintFmt("CRITICAL: Forcing wave {} completion due to stalled state\n",
//			current_wave_level);
//	}
//
//	// Allow wave advance
//	allowWaveAdvance = true;
//
//	// Set state to cleanup
//	g_horde_local.state = horde_state_t::cleanup;
//	g_horde_local.monster_spawn_time = level.time + 0.5_sec;
//}


void Horde_RunFrame() {
	// Cache frequently used state variables
	const MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level;
	const horde_state_t currentState = g_horde_local.state;
	const gtime_t currentTime = level.time;

	// Regular maintenance
	CleanupSpawnPointCache();
	CheckAndReduceSpawnCooldowns();

	// Safety check for high waves - detect stuck states
	static gtime_t last_wave_change_time = 0_sec;
	static int32_t last_wave_level = 0;

	if (need_frame_timer_reset) {
		last_wave_change_time = 0_sec;
		last_wave_level = 0;
		need_frame_timer_reset = false;
	}

	if (last_wave_level != currentLevel) {
		last_wave_change_time = currentTime;
		last_wave_level = currentLevel;
	}
	else if (currentTime - last_wave_change_time > 3_min &&
		currentState != horde_state_t::warmup &&
		GetStroggsNum() == 0) {
		// Force transition if stuck in same wave for too long
		if (developer->integer) {
			gi.Com_PrintFmt("CRITICAL: Wave {} stuck for over 3 minutes. Forcing progression.\n",
				currentLevel);
		}
		g_horde_local.state = horde_state_t::spawning;
		g_horde_local.monster_spawn_time = currentTime + 1.5_sec;
		Horde_InitLevel(currentLevel + 1);
		return;
	}

	// Handle custom monster settings
	if (dm_monsters->integer >= 1) {
		g_horde_local.num_to_spawn = dm_monsters->integer;
		g_horde_local.queued_monsters = 0;
		ClampNumToSpawn(mapSize);
	}

	bool waveEnded = false;
	WaveEndReason currentWaveEndReason = WaveEndReason::AllMonstersDead;

	//// Periodically validate monster count
	//static gtime_t last_validation_time = 0_sec;
	//if (currentTime - last_validation_time >= 0.5_sec) {
	//	ValidateMonsterCount();
	//	last_validation_time = currentTime;
	//}

	// STATE MACHINE
	switch (g_horde_local.state) {
	case horde_state_t::warmup:
		if (g_horde_local.warm_time < currentTime) {
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(1);
			current_wave_level = 1;
			PlayWaveStartSound();
			DisplayWaveMessage();
		}
		break;

	case horde_state_t::spawning: {
		// Failsafe for stuck in spawning state
		static gtime_t spawning_start_time = 0_sec;
		if (spawning_start_time == 0_sec) {
			spawning_start_time = currentTime;
		}
		else if (currentTime - spawning_start_time > 25_sec) {
			if (!next_wave_message_sent) {
				VerifyAndAdjustBots();
				//gi.LocBroadcast_Print(PRINT_CENTER,
				//	"\n\n\nWave deployment timeout. Moving to active phase.\n");
				next_wave_message_sent = true;
			}
			g_horde_local.state = horde_state_t::active_wave;
			spawning_start_time = 0_sec;
			break;
		}

		// Independent time limit check
		const gtime_t independentTimeLimit = g_independent_timer_start + g_lastParams.independentTimeThreshold;
		if (currentTime >= independentTimeLimit) {
			currentWaveEndReason = WaveEndReason::TimeLimitReached;
			waveEnded = true;
			spawning_start_time = 0_sec;
			break;
		}

		if (g_horde_local.monster_spawn_time <= currentTime) {
			// Boss wave handling
			if (currentLevel >= 10 && currentLevel % 5 == 0 && !boss_spawned_for_wave) {
				SpawnBossAutomatically();
			}

			const int32_t activeMonsters = CalculateRemainingMonsters();
			const int32_t maxMonsters = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap :
				(mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
					(mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP));

			// Queue transfer handling
			if (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters > 0 &&
				activeMonsters < maxMonsters) {

				const int32_t base_transfer = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
				const int32_t transfer_amount = std::min({
					g_horde_local.queued_monsters,
					maxMonsters - activeMonsters,
					base_transfer
					});

				if (transfer_amount > 0) {
					g_horde_local.num_to_spawn += transfer_amount;
					g_horde_local.queued_monsters -= transfer_amount;
				}
			}

			// Spawn monsters
			if (g_horde_local.num_to_spawn > 0 && activeMonsters < maxMonsters) {
				SpawnMonsters();
			}

			// Check if all monsters have been spawned
			if (g_horde_local.num_to_spawn == 0 && g_horde_local.queued_monsters == 0) {
				if (!next_wave_message_sent) {
					VerifyAndAdjustBots();
					gi.LocBroadcast_Print(PRINT_CENTER,
						"\n\n\nWave Fully Deployed.\nWave Level: {}\n",
						currentLevel);
					next_wave_message_sent = true;
				}
				g_horde_local.state = horde_state_t::active_wave;
				spawning_start_time = 0_sec;
			}

			// Set next spawn attempt time
			SetNextMonsterSpawnTime(mapSize);
		}
		break;
	}

	case horde_state_t::active_wave: {
		//// Safety timeout for active_wave state
		//static gtime_t active_state_start = currentTime;
		//if (last_wave_level != currentLevel) {
		//	active_state_start = currentTime;
		//}
		//else if (currentTime - active_state_start > 3_min) {
		//	if (developer->integer) {
		//		gi.Com_PrintFmt("TIMEOUT: Forcing end of wave {} after 3 minutes\n", currentLevel);
		//	}
		//	ForceWaveCompletion();
		//	waveEnded = true;
		//	break;
		//}

		// Fast path for all monsters dead
		if (Horde_AllMonstersDead()) {
			currentWaveEndReason = WaveEndReason::AllMonstersDead;
			waveEnded = true;
			break;
		}

		// Check wave completion conditions
		if (CheckRemainingMonstersCondition(currentWaveEndReason)) {
			waveEnded = true;
			break;
		}

		// Spawn queued monsters periodically
		if (g_horde_local.monster_spawn_time <= currentTime) {
			const int32_t activeMonsters = CalculateRemainingMonsters();
			const int32_t maxMonsters = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap :
				(mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
					(mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP));

			// Quick queue transfer if there's room
			if (activeMonsters < maxMonsters && g_horde_local.queued_monsters > 0) {
				const int32_t available_slots = maxMonsters - activeMonsters;
				const int32_t transfer_amount = std::min(g_horde_local.queued_monsters, available_slots);

				if (transfer_amount > 0) {
					g_horde_local.num_to_spawn = transfer_amount;
					g_horde_local.queued_monsters -= transfer_amount;
					SpawnMonsters();
				}
			}

			// Set next spawn time
			SetNextMonsterSpawnTime(mapSize);
		}

		// Check for stuck queued monsters
		static gtime_t last_queue_check = 0_sec;
		static int consecutive_stuck_checks = 0;

		if (currentTime - last_queue_check > 2_sec) {
			last_queue_check = currentTime;

			// Fix negative queue size
			if (g_horde_local.queued_monsters < 0) {
				g_horde_local.queued_monsters = 0;
			}

			// Check for stuck queue
			if (g_horde_local.queued_monsters > 0 && g_horde_local.num_to_spawn == 0) {
				consecutive_stuck_checks++;

				if (consecutive_stuck_checks >= 3) {
					// Unstick queue - move some to spawn pool
					const int32_t to_move = std::min(g_horde_local.queued_monsters, 5);
					g_horde_local.num_to_spawn += to_move;
					g_horde_local.queued_monsters -= to_move;
					consecutive_stuck_checks = 0;
				}
			}
			else {
				consecutive_stuck_checks = 0;
			}
		}
		break;
	}

	case horde_state_t::cleanup:
		if (g_horde_local.monster_spawn_time < currentTime) {
			HandleWaveCleanupMessage(mapSize, currentWaveEndReason);
			g_horde_local.warm_time = currentTime + random_time(0.8_sec, 1.5_sec);
			g_horde_local.state = horde_state_t::rest;

			// Reset spawn points for next wave
			CheckAndResetDisabledSpawnPoints();
		}
		break;

	case horde_state_t::rest:
		if (g_horde_local.warm_time < currentTime) {
			AnnounceIncomingWave(3_sec);
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(g_horde_local.level + 1);
		}
		break;
	}

	// Cleanup logic - outside the switch for better clarity
	if (waveEnded) {
		SendCleanupMessage(currentWaveEndReason);
		g_horde_local.monster_spawn_time = currentTime + 0.5_sec;
		g_horde_local.state = horde_state_t::cleanup;

		// Reset variables for next wave
		ResetWaveAdvanceState();
	}

	// Process HUD updates and expiring messages
	UpdateHordeHUD();
	if (horde_message_end_time != 0_sec && currentTime >= horde_message_end_time) {
		ClearHordeMessage();
	}
}

// Función para manejar el evento de reinicio
void HandleResetEvent() {
	ResetGame();
}

// Get the remaining time for the current wave
inline gtime_t GetWaveTimer() {
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

// Helper functionget stroggs alive on the map
inline int32_t GetStroggsNum() noexcept {
	return level.total_monsters - level.killed_monsters;
}

// Helper function to check if it's a boss wave
inline bool IsBossWave() noexcept {
	return g_horde_local.level >= 10 && g_horde_local.level % 5 == 0;
}

