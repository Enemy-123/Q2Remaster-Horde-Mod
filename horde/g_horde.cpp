// Includes y definiciones relevantes
#include "../g_local.h"
#include "g_horde.h"
#include "../shared.h"
#include <set>
#include "g_horde_benefits.h"
#include "horde_ids.h"
#include <span>

static bool need_spawn_cache_reset = false;
static bool need_frame_timer_reset = false;
static bool need_queue_monitor_reset = false;

// Ambush system tracking variables
static gtime_t last_ambush_time = 0_sec;
static int32_t ambush_cooldown_frames = 0;
static int32_t waves_since_ambush = 0;
static bool ambush_system_initialized = false;

void ResetSpawnMonsterVars() {
	need_spawn_cache_reset = true;
	// Add reset for new trackers
	horde::g_monsterSpawnTracker.Reset();
}

void ResetFrameTimers() {
	need_frame_timer_reset = true;
}

void ResetQueueMonitorVars() {
	need_queue_monitor_reset = true;
	// Add reset for spawn point tracker
	horde::g_spawnPointTimeTracker.Reset();
}

//champion monster to spawn on wave
bool champion_spawned_this_wave = false;
int champion_spawn_cooldown = 0;

// Monster count verification tracking
static int consistent_zero_counts = 0;
static int counter_mismatch_frames = 0;

// Maximum number of spawn points to track
constexpr size_t MAX_SPAWN_POINTS = 32;

namespace HordeConstants {
// constexpr float PLAYER_MULTIPLIER = 0.2f;
	constexpr float TIME_REDUCTION_MULTIPLIER = 0.95f;
	//constexpr float DIFFICULTY_PLAYER_FACTOR = 0.075f;
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
struct SpawnPointData {
	uint16_t attempts = 0;
	gtime_t teleport_cooldown = 0_sec;  // Added teleport_cooldown
	gtime_t lastSpawnTime = 0_sec;      // Added lastSpawnTime
	bool isTemporarilyDisabled = false;
	gtime_t cooldownEndsAt = 0_sec;
	int32_t successfulSpawns = 0;

	// New fields for alternative position tracking
	uint16_t alternative_attempts = 0;    // Added alternative_attempts
	gtime_t alternative_cooldown = 0_sec; // Added alternative_cooldown
	bool needs_long_alternative_cooldown = false; // Added needs_long_alternative_cooldown

	// Update the success rate calculation method to accept current_time
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

void ApplySuccessfulAlternativeCooldown(edict_t* spawn_point) {
	if (!spawn_point || !spawn_point->inuse) {
		return;
	}

	auto& data = spawnPointsData[spawn_point];
	const gtime_t current_time = level.time;

	// Reset alternative attempts since we had a success
	data.alternative_attempts = 0;
	data.needs_long_alternative_cooldown = false;

	// Use a shorter cooldown for successful alternatives
	// This prevents spawning too many monsters in the same area at once
	const gtime_t success_cooldown = 3.0_sec;

	// Apply the cooldown
	data.alternative_cooldown = current_time + success_cooldown;

	if (developer->integer > 1) {
		gi.Com_PrintFmt("Success cooldown applied to spawn at {}: {:.1f}s\n",
			spawn_point->s.origin, success_cooldown.seconds());
	}
}

void ApplyAlternativePositionCooldown(edict_t* spawn_point) {
	if (!spawn_point || !spawn_point->inuse) {
		return;
	}

	auto& data = spawnPointsData[spawn_point];
	const gtime_t current_time = level.time;

	// Increment alternative attempts counter
	data.alternative_attempts++;

	// Determine cooldown duration based on past attempts
	gtime_t cooldown_duration;

	// For the first few failures, use shorter cooldowns
	if (data.alternative_attempts <= 2) {
		cooldown_duration = 1.5_sec;
	}
	// For intermediate failures, use medium cooldowns
	else if (data.alternative_attempts <= 5) {
		cooldown_duration = 3.0_sec;
	}
	// For persistent failures, use longer cooldowns
	else {
		// Fixed: Use gtime_t::from_sec to properly calculate the additional time
		cooldown_duration = 5.0_sec + gtime_t::from_sec(0.5f * (data.alternative_attempts - 5));
		// Cap at 10 seconds
		cooldown_duration = std::min(cooldown_duration, 10.0_sec);

		// Mark for longer alternative cooldown on persistent failures
		if (data.alternative_attempts >= 8) {
			data.needs_long_alternative_cooldown = true;
		}
	}

	// Apply the cooldown
	data.alternative_cooldown = current_time + cooldown_duration;

	// Also apply shorter disable on the spawn point itself
	data.isTemporarilyDisabled = true;
	data.cooldownEndsAt = current_time + cooldown_duration;

	if (developer->integer) {
		gi.Com_PrintFmt("Alternative position cooldown applied to spawn at {}: {:.1f}s (attempts: {})\n",
			spawn_point->s.origin, cooldown_duration.seconds(), data.alternative_attempts);
	}
}

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
	// Cache success rate calculation - FIX: Initialize it in the same statement
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
    data.attempts = 0; // Reset attempts after success
    data.isTemporarilyDisabled = false;

    // Short cooldown after successful spawn to prevent instant respawn
    data.cooldownEndsAt = level.time + 0.5_sec;
    
    // Update the spawn time tracker
    horde::g_spawnPointTimeTracker.SetLastSpawnTime(spawn_point, level.time);
}

//// Función de filtro optimizada
//// Modified SpawnPointFilter function
//static BoxEdictsResult_t SpawnPointFilter(edict_t* ent, void* data) {
//	FilterData* filter_data = static_cast<FilterData*>(data);
//
//	// Ignore the specified entity (if exists)
//	if (ent == filter_data->ignore_ent) {
//		return BoxEdictsResult_t::Skip;
//	}
//
//	// Check if the entity is a player or bot
//	if (ent->client && ent->inuse) {
//		filter_data->count++;
//		return BoxEdictsResult_t::End; // Stop searching if a player or bot is found
//	}
//
//	// Check if the entity is a monster (using the SVF_MONSTER flag)
//	if (ent->svflags & SVF_MONSTER && !ent->deadflag) {
//		filter_data->count++;
//		return BoxEdictsResult_t::End; // Stop searching if a monster is found
//	}
//
//	// NEW: Check if the entity is a player-deployed defense
//	if (ent->inuse && IsPlayerDefense(ent)) {
//		filter_data->count++;
//		return BoxEdictsResult_t::End; // Stop searching if a player defense is found
//	}
//
//	return BoxEdictsResult_t::Skip;
//}

struct SpawnPointCache {
	gtime_t last_check_time;
	vec3_t last_check_origin;
	gtime_t frame_time;  // Changed from frame_number to frame_time
	bool was_occupied = false;
	bool has_obstacle = false;
};

struct SpawnPointCacheArray {
    SpawnPointCache data[MAX_EDICTS];
    
    SpawnPointCache& operator[](const edict_t* ent) {
        return data[ent - g_edicts];
    }
    
    const SpawnPointCache& operator[](const edict_t* ent) const {
        return data[ent - g_edicts];
    }
    
    void clear() {
        for (auto& item : data) {
            item = SpawnPointCache{};
        }
    }
};
static SpawnPointCacheArray spawn_point_cache;

// ¿Está el punto de spawn ocupado?
// Verify if any spawn points are occupied, using span for efficient iteration
// Original single point check with optimized vector validation
bool IsSpawnPointOccupied(const edict_t* spawn_point, const edict_t* ignore_ent = nullptr) {
	// Fast path: Add validation first with combined checks
	if (!spawn_point || !is_valid_vector(spawn_point->s.origin)) {
		// Only print warning in developer mode to avoid performance impact
		if (developer->integer) {
			if (!spawn_point)
				gi.Com_PrintFmt("Warning: Null spawn_point passed to IsSpawnPointOccupied\n");
			else
				gi.Com_PrintFmt("Warning: Invalid origin vector in spawn point\n");
		}
		return true; // Safer to assume occupied if invalid
	}

	// Get cache entry using direct array access - much faster than map lookup
	SpawnPointCache& cache = spawn_point_cache[spawn_point];

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

	// Optimized space check - precalculate scaled vectors once as static constants
	static const vec3_t mins_scale = vec3_t{ 16, 16, 24 }.scaled(vec3_t{ 1.75f, 1.75f, 1.75f });
	static const vec3_t maxs_scale = vec3_t{ 16, 16, 32 }.scaled(vec3_t{ 1.75f, 1.75f, 1.75f });

	// Direct vector operations for bounding box - using modern C++ operators
	const vec3_t spawn_mins = spawn_point->s.origin - mins_scale;
	const vec3_t spawn_maxs = spawn_point->s.origin + maxs_scale;

	// Use stack-allocated filter data
	FilterData filter_data = { ignore_ent, 0 };

	// Only check for players - we'll handle other obstructions via alternative position system
	gi.BoxEdicts(spawn_mins, spawn_maxs, nullptr, 0, AREA_SOLID,
		[](edict_t* ent, void* data) -> BoxEdictsResult_t {
			FilterData* filter_data = static_cast<FilterData*>(data);

			// Ignore the specified entity (if exists)
			if (ent == filter_data->ignore_ent) {
				return BoxEdictsResult_t::Skip;
			}

			// Only check for players - they're the only immediate blockers
			if (ent->client && ent->inuse) {
				filter_data->count++;
				return BoxEdictsResult_t::End; // Stop searching if a player is found
			}

			return BoxEdictsResult_t::Skip;
		}, &filter_data);

	// Check for obstruction by any entity using a secondary check
	// This marks the point as "needs alternative" without rejecting it completely
	FilterData obstacle_data = { ignore_ent, 0 };
	gi.BoxEdicts(spawn_mins, spawn_maxs, nullptr, 0, AREA_SOLID,
		[](edict_t* ent, void* data) -> BoxEdictsResult_t {
			FilterData* filter_data = static_cast<FilterData*>(data);

			// Ignore the specified entity
			if (ent == filter_data->ignore_ent) {
				return BoxEdictsResult_t::Skip;
			}

			// Check for monsters or player defenses
			if ((ent->svflags & SVF_MONSTER && !ent->deadflag) ||
				(ent->inuse && IsPlayerDefense(ent))) {
				filter_data->count++;
				return BoxEdictsResult_t::End;
			}

			return BoxEdictsResult_t::Skip;
		}, &obstacle_data);

	// Store both states in the cache
	cache.was_occupied = (filter_data.count > 0);
	cache.has_obstacle = (obstacle_data.count > 0);

	// Only block if a player is directly in the way
	// Other obstacles will be handled via alternative position system
	return cache.was_occupied;
}

static void CleanupSpawnPointCache() noexcept {
	spawn_point_cache.clear();
}

template <typename TFilter>
edict_t* SelectRandomSpawnPoint(TFilter filter) {
	// Pre-allocate on stack instead of using static vector
	edict_t* availableSpawns[MAX_SPAWN_POINTS]{};
	edict_t* occupiedButUsableSpawns[MAX_SPAWN_POINTS]{}; // NEW: Store occupied but potentially usable points
	int availableCount = 0;
	int occupiedCount = 0; // NEW: Track occupied but potentially usable points

	// Get all spawn points but don't process them yet
	auto spawnPoints = monster_spawn_points();

	// First pass: collect all potentially valid spawn points
	for (edict_t* spawnPoint : spawnPoints) {
		// Combined validation check to reduce branches
		if (!spawnPoint || !spawnPoint->inuse || !is_valid_vector(spawnPoint->s.origin))
			continue;

		// Check if spawn point is on cooldown - direct array access
		auto const& data = spawnPointsData[spawnPoint];
		if (data.isTemporarilyDisabled && level.time < data.cooldownEndsAt)
			continue;

		// NEW: Check filter conditions first
		if (filter(spawnPoint)) {
			// NEW: Check if spawn point is occupied but could be used with alternatives
			if (IsSpawnPointOccupied(spawnPoint)) {
				// Store in occupied but potentially usable array
				if (occupiedCount < MAX_SPAWN_POINTS) {
					occupiedButUsableSpawns[occupiedCount++] = spawnPoint;
				}
			}
			else {
				// Not occupied - add to available array
				if (availableCount < MAX_SPAWN_POINTS) {
					availableSpawns[availableCount++] = spawnPoint;
				}
			}
		}
	}

	// If we have unoccupied spawn points, use those preferentially
	if (availableCount > 0) {
		// Pick a random index
		const size_t idx = irandom(availableCount);
		return availableSpawns[idx];
	}

	// If no unoccupied points but we have occupied points that pass the filter,
	// return one of those and let SpawnMonsters try to find alternatives
	if (occupiedCount > 0) {
		// Pick a random occupied point
		const size_t idx = irandom(occupiedCount);
		return occupiedButUsableSpawns[idx];
	}

	return nullptr; // No valid spawn points found
}
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

enum class BossType {
	CARRIER,
	BOSS2,
	BOSS2KL,
	FIXBOTKL,
	CARRIER_MINI,
	TANK_64,
	SHAMBLERKL,
	GUNCMDRKL,
	GUARDIAN,
	PSX_GUARDIAN,
	BOSS5,
	WIDOW,
	WIDOW2,
	JORG,
	MAKRONKL,
	PSX_ARACHNID,
	REDMUTANT,
	OTHER
};

inline bool HasWaveType(MonsterWaveType entityTypes, MonsterWaveType typeToCheck);

// Spawn point selection filter
struct SpawnMonsterFilter {
	gtime_t currentTime;

	SpawnMonsterFilter(gtime_t time) : currentTime(time) {}

	bool operator()(edict_t* spawnPoint) const {
		// Combined validation to reduce branches
		if (!spawnPoint || !spawnPoint->inuse || !is_valid_vector(spawnPoint->s.origin))
			return false;

		// Direct array access for cooldown check
		const auto& data = spawnPointsData[spawnPoint];

		// Check normal cooldown
		if (data.isTemporarilyDisabled && currentTime < data.cooldownEndsAt)
			return false;

		// NEW: Check alternative position cooldown
		if (data.alternative_cooldown > 0_sec && currentTime < data.alternative_cooldown)
			return false;

		// Check if this is a flying spawn point but we need non-flying monsters
		if (spawnPoint->style == 1 && !HasWaveType(current_wave_type, MonsterWaveType::Flying))
			return false;

		// Cache the origin reference to avoid multiple member accesses
		const vec3_t& origin = spawnPoint->s.origin;

		// Check minimum distance from players using modern vector operations
		for (const auto* const player : active_players()) {
			if (!player || !player->inuse)
				continue;

			// Use squared distance to avoid unnecessary sqrt calculations
			if ((origin - player->s.origin).lengthSquared() < 22500.0f) // 150^2 = 22500
				return false;
		}

		return true;
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
	constexpr float REDUCTION_FACTOR = 0.4f;  // Changed from 0.15f to 0.4f to make late-wave cooldown reduction less aggressive

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
		return 0.5_sec;  // Increased from 0.2 to 0.5
	else if (isBigMap)
		return 2.0_sec;  // Increased from 1.4 to 2.0
	else
		return 1.2_sec;  // Increased from 0.8 to 1.2
}

static float CalculateCooldownScale(int32_t lvl, const MapSize& mapSize) {
	// Early return for low levels - improves branch prediction
	if (lvl <= 10) {
		return 1.0f;
	}

	// Cache player count - only compute once
	const int32_t numHumanPlayers = GetNumHumanPlayers();

	// Compute base scale - more gradual linear scaling with level
	float scale = 1.0f + (lvl * 0.015f);  // Changed from 0.02f to 0.015f for more gradual scaling

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

// Cache for common calculations in UnifiedAdjustSpawnRate
struct WaveScalingCache {
    // Lookup tables for frequent calculations
    static constexpr int32_t MAX_WAVE_LEVEL = 50;
    static constexpr int32_t MAX_HUMAN_PLAYERS = 16;
    
    // Pre-computed cooldown scales to avoid recalculation
    float cooldownScales[3][MAX_WAVE_LEVEL+1] = {}; // [mapType][level]
    
    // Cached base counts and additional spawns
    int32_t baseCountsByLevel[3][MAX_WAVE_LEVEL+1] = {}; // [mapType][level]
    int32_t additionalSpawnsByLevel[MAX_WAVE_LEVEL+1] = {};
    
    // Player multipliers (precomputed)
    float playerMultipliers[MAX_HUMAN_PLAYERS+1] = {};
    
    // Initialize all cache tables
    void initialize() {
        using namespace HordeConstants;
        
        // Initialize player multipliers
        for (int32_t players = 0; players <= MAX_HUMAN_PLAYERS; ++players) {
            playerMultipliers[players] = (players <= 1) ? 
                1.0f : BASE_DIFFICULTY_MULTIPLIER + ((players - 1) * PLAYER_COUNT_SCALE);
        }
        
        // Initialize base counts by map type and level
        for (int mapType = 0; mapType < 3; ++mapType) {
            for (int32_t level = 0; level <= MAX_WAVE_LEVEL; ++level) {
                // Select the appropriate base count based on level ranges
                int32_t countIndex;
                if (level <= 5) countIndex = 0;
                else if (level <= 10) countIndex = 1;
                else if (level <= 15) countIndex = 2;
                else countIndex = 3;
                
                // Store pre-computed base count
                baseCountsByLevel[mapType][level] = BASE_COUNTS[mapType][countIndex];
            }
        }
        
        // Initialize additional spawns by level
        for (int32_t level = 0; level <= MAX_WAVE_LEVEL; ++level) {
            if (level < 8) {
                additionalSpawnsByLevel[level] = 6;
            } else {
                // Base values from ADDITIONAL_SPAWNS constants
                int smallMapSpawn = ADDITIONAL_SPAWNS[0];
                int mediumMapSpawn = ADDITIONAL_SPAWNS[1];
                int bigMapSpawn = ADDITIONAL_SPAWNS[2];
                
                // Apply level-based adjustments
                if (level > 25) {
                    smallMapSpawn = static_cast<int32_t>(smallMapSpawn * 1.6f);
                    mediumMapSpawn = static_cast<int32_t>(mediumMapSpawn * 1.6f);
                    bigMapSpawn = static_cast<int32_t>(bigMapSpawn * 1.6f);
                }
                
                // Store by map type
                additionalSpawnsByLevel[level] = mediumMapSpawn; // Default to medium
            }
        }
        
        // Initialize cooldown scales (can be accessed by mapType and level)
        MapSize smallMap = {true, false, false};
        MapSize mediumMap = {false, false, true};
        MapSize bigMap = {false, true, false};
        
        for (int32_t level = 0; level <= MAX_WAVE_LEVEL; ++level) {
            cooldownScales[0][level] = CalculateCooldownScale(level, smallMap);
            cooldownScales[1][level] = CalculateCooldownScale(level, mediumMap);
            cooldownScales[2][level] = CalculateCooldownScale(level, bigMap);
        }
    }
} g_waveScalingCache;

void UnifiedAdjustSpawnRate(const MapSize& mapSize, int32_t lvl, int32_t humanPlayers) noexcept {
    using namespace HordeConstants;
    
    // Initialize cache if needed (one-time operation)
    static bool cache_initialized = false;
    if (!cache_initialized) {
        g_waveScalingCache.initialize();
        cache_initialized = true;
    }
    
    // Clamp input values for safety
    const int32_t safeLevel = std::min(lvl, WaveScalingCache::MAX_WAVE_LEVEL);
    const int32_t safePlayerCount = std::min(humanPlayers, WaveScalingCache::MAX_HUMAN_PLAYERS);
    
    // Determine map type index for lookups
    const int mapTypeIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);
    
    // Get base values from cache
    int32_t baseCount = g_waveScalingCache.baseCountsByLevel[mapTypeIndex][safeLevel];
    
    // Apply player multiplier using cached value
    if (safePlayerCount > 1) {
        const float playerMultiplier = g_waveScalingCache.playerMultipliers[safePlayerCount];
        baseCount = static_cast<int32_t>(baseCount * playerMultiplier);
    }
    
    // Get additional spawn count (with map type adjustment)
    int32_t additionalSpawn = g_waveScalingCache.additionalSpawnsByLevel[safeLevel];
    
    // Apply map-specific adjustment to additional spawns
    if (safeLevel >= 8) {
        additionalSpawn = mapSize.isSmallMap ? ADDITIONAL_SPAWNS[0] :
            mapSize.isBigMap ? ADDITIONAL_SPAWNS[2] : ADDITIONAL_SPAWNS[1];
            
        // Level-based adjustment for high levels
        if (safeLevel > 25) {
            additionalSpawn = static_cast<int32_t>(additionalSpawn * 1.6f);
        }
    }
    
    // Apply difficulty adjustments
    if (safeLevel >= 3 && (g_chaotic->integer || g_insane->integer)) {
        additionalSpawn += CalculateChaosInsanityBonus(safeLevel);
    }
    
    // Get cooldown from cache and apply adjustments
    SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);
    const float cooldownScale = g_waveScalingCache.cooldownScales[mapTypeIndex][safeLevel];
    SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);
    
    // Apply difficulty-based cooldown adjustments
    if (g_chaotic->integer || g_insane->integer) {
        SPAWN_POINT_COOLDOWN *= TIME_REDUCTION_MULTIPLIER;
    } else {
        // Normal difficulty adjustment
        SPAWN_POINT_COOLDOWN *= 1.2f;
    }
    
    // Apply periodic difficulty scaling
    if (safeLevel % 3 == 0) {
        const float difficultyMultiplier = g_waveScalingCache.playerMultipliers[safePlayerCount];
        baseCount = static_cast<int32_t>(baseCount * difficultyMultiplier);
        
        const float cooldownReduction = (mapSize.isBigMap ? 0.1f : 0.15f) * difficultyMultiplier;
        SPAWN_POINT_COOLDOWN = std::max(
            SPAWN_POINT_COOLDOWN - gtime_t::from_sec(cooldownReduction),
            1.0_sec
        );
    }
    
    // Final cooldown clamping
    SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN, 1.5_sec, 3.5_sec);
    
    // Calculate final spawn count
    g_horde_local.num_to_spawn = baseCount + additionalSpawn;
    ClampNumToSpawn(mapSize); // Handle clamping
    
    // Calculate queued monsters
    const bool isHardMode = g_insane->integer || g_chaotic->integer;
    g_horde_local.queued_monsters = CalculateQueuedMonsters(mapSize, safeLevel, isHardMode);
    
    // Debug output
    if (developer->integer == 3) {
        gi.Com_PrintFmt("DEBUG: Wave {} settings:\n", safeLevel);
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
constexpr int MONSTERS_FOR_AGGRESSIVE_REDUCTION = 5;
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
			params.timeThreshold = random_time(17_sec, 22_sec);
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

	// Early wave time REDUCTION (Modified)
	float timeMultiplier = 1.0f;
	if (lvl <= 10) {
		// Instead of increasing time, we now DECREASE it.
		// Start with a multiplier of 0.7 at level 1
		// and go to 1.0 at level 10.
		// Formula:  1.0 - (0.25 * (lvl - 1) / 9)
		timeMultiplier = 1.0f - (0.25f * (static_cast<float>(lvl - 1) / 9.0f));
		timeMultiplier = std::max(0.1f, timeMultiplier); // Ensure multiplier is never below 0.1 (10% of original time)
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

	// Apply multiplier to time thresholds for early waves (No Change, just applying the modified multiplier)
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


inline int32_t GetAdjustedMonsterCap(const MapSize& mapSize, int32_t waveLevel);

void ResetChampionMonsterState() {
	champion_spawned_this_wave = false;
	champion_spawn_cooldown = 0;
}

void InitializeWaveType(int32_t waveLevel);

static void Horde_InitLevel(const int32_t lvl) {

	ResetChampionMonsterState();

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
	//case 45:
	//	gi.cvar_set("g_damage_scale", "4.5");
	//	break;
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
template<typename E>
constexpr auto to_underlying(E e) noexcept {
	return static_cast<std::underlying_type_t<E>>(e);
}

constexpr MonsterWaveType operator|(MonsterWaveType a, MonsterWaveType b) {
	return static_cast<MonsterWaveType>(
		to_underlying(a) | to_underlying(b)
		);
}

inline MonsterWaveType operator&(MonsterWaveType a, MonsterWaveType b) noexcept {
	return static_cast<MonsterWaveType>(
		to_underlying(a) & to_underlying(b)
		);
}

inline MonsterWaveType& operator|=(MonsterWaveType& a, MonsterWaveType b) {
	a = a | b;
	return a;
}

// Helper function to check if a monster has a specific wave type
inline bool HasWaveType(MonsterWaveType entityTypes, MonsterWaveType typeToCheck) {
	return (entityTypes & typeToCheck) != MonsterWaveType::None;
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

// Helper function to check if a wave type is a special wave
static bool IsSpecialWaveType(MonsterWaveType type) noexcept {
	return HasWaveType(type, MonsterWaveType::Gekk) ||
		HasWaveType(type, MonsterWaveType::Berserk) ||
		HasWaveType(type, MonsterWaveType::Mutant) ||
		HasWaveType(type, MonsterWaveType::Spawner) ||
		HasWaveType(type, MonsterWaveType::Bomber) ||
		HasWaveType(type, MonsterWaveType::Heavy) ||
		HasWaveType(type, MonsterWaveType::Shambler) ||
		HasWaveType(type, MonsterWaveType::Arachnophobic);
}

// check if the previous wave was a special wave
static bool WasLastWaveSpecial() noexcept {
	if (previous_wave_types.empty()) {
		return false;
	}

	// Get the most recent wave type (the one right before the current index)
	size_t last_index = (wave_memory_index == 0) ?
		WAVE_MEMORY_SIZE - 1 :
		wave_memory_index - 1;

	return IsSpecialWaveType(previous_wave_types[last_index]);
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

	// Try special waves first only if the previous wave wasn't special
	// This prevents consecutive special waves
	if (!forceSpecialWave && !WasLastWaveSpecial()) {
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

void InitializeWaveType(int32_t waveLevel) {
	current_wave_type = GetWaveComposition(waveLevel);
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

// Update the function to accept MonsterTypeID

// Structure to include wave level information
struct MonsterTypeInfo {
	horde::MonsterTypeID typeId;  // Instead of const char* classname
	MonsterWaveType types;
	int minWave;
	float weight;


	constexpr MonsterTypeInfo(horde::MonsterTypeID id, MonsterWaveType t, int w, float wt)
		: typeId(id), types(t), minWave(w), weight(wt) {
	}
};

static const MonsterTypeInfo monsterTypes[] = {
	// Basic Infantry (Waves 1-5)
	{horde::MonsterTypeID::SOLDIER_LIGHT, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 1.0f},
	{horde::MonsterTypeID::SOLDIER, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 0.9f},
	{horde::MonsterTypeID::SOLDIER_SS, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 2, 0.8f},
	{horde::MonsterTypeID::INFANTRY_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 3, 0.85f},

	// Early Flying Units (Waves 1-8)
	{horde::MonsterTypeID::FLYER, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Fast, 1, 0.7f},
	{horde::MonsterTypeID::FIXBOT, MonsterWaveType::Flying | MonsterWaveType::Light |MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.45f},
	{horde::MonsterTypeID::HOVER_VANILLA, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Ranged, 7, 0.6f},
	{horde::MonsterTypeID::FLOATER, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Ranged, 12, 0.6f},

	// Special Wave Units (Waves 4-9)
	{horde::MonsterTypeID::GEKK, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Small | MonsterWaveType::Mutant | MonsterWaveType::Gekk, 4, 0.7f},
	{horde::MonsterTypeID::PARASITE, MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Melee, 5, 0.6f},
	{horde::MonsterTypeID::STALKER, MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Fast | MonsterWaveType::Arachnophobic, 7, 0.6f},
	{horde::MonsterTypeID::BRAIN, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special | MonsterWaveType::Melee | MonsterWaveType::Mutant, 6, 0.7f},

	// Elite Infantry (Waves 4-12)
	{horde::MonsterTypeID::SOLDIER_HYPERGUN, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 4, 0.7f},
	{horde::MonsterTypeID::SOLDIER_RIPPER, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.8f},
	{horde::MonsterTypeID::SOLDIER_LASERGUN, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 10, 0.8f},
	{horde::MonsterTypeID::CHICK, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 7, 0.6f},

	// Medium Units (Waves 7-12)
	{horde::MonsterTypeID::GUNNER_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 8, 0.8f},
	{horde::MonsterTypeID::INFANTRY, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Heavy | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 11, 0.85f},
	{horde::MonsterTypeID::MEDIC, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special, 7, 0.5f},
	{horde::MonsterTypeID::BERSERK, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Melee | MonsterWaveType::Berserk , 6, 0.8f},

	// Arachnophobic Units (Waves 8-18)
	{horde::MonsterTypeID::SPIDER, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 7, 0.35f},
	{horde::MonsterTypeID::GUNCMDR_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 12, 0.4f},
	{horde::MonsterTypeID::ARACHNID2, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 18, 0.4f},
	{horde::MonsterTypeID::GM_ARACHNID, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Heavy | MonsterWaveType::Elite, 15, 0.45f},
	{horde::MonsterTypeID::PSX_ARACHNID, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite | MonsterWaveType::Spawner, 25, 0.35f},

	// Mutant Units (Waves 9-14)
	{horde::MonsterTypeID::MUTANT, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 9, 0.7f},
	{horde::MonsterTypeID::SHAMBLER_SMALL, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Mutant | MonsterWaveType::Shambler, 14, 0.4f},
	{horde::MonsterTypeID::REDMUTANT, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Elite | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 14, 0.35f},

	// Heavy Ground Units (Waves 12-18)
	{horde::MonsterTypeID::GLADIATOR, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged, 12, 0.7f},
	{horde::MonsterTypeID::GUNNER, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 12, 0.8f},
	{horde::MonsterTypeID::TANK_SPAWNER, MonsterWaveType::Ground | MonsterWaveType::Spawner | MonsterWaveType::Heavy | MonsterWaveType::Medium | MonsterWaveType::Elite, 13, 0.6f},
	{horde::MonsterTypeID::TANK, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 14, 0.4f},
	{horde::MonsterTypeID::TANK_COMMANDER, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 16, 0.5f},
	{horde::MonsterTypeID::GUNCMDR, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Bomber, 15, 0.7f},
	{horde::MonsterTypeID::RUNNERTANK, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Fast, 21, 0.5f},
	{horde::MonsterTypeID::CHICK_HEAT, MonsterWaveType::Ground | MonsterWaveType::Heavy| MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Fast, 13, 0.6f},

	// Elite Flying Units (Waves 18-27)
	{horde::MonsterTypeID::HOVER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 18, 0.5f},
	{horde::MonsterTypeID::DAEDALUS, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 21, 0.4f},
	{horde::MonsterTypeID::FLOATER_TRACKER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 19, 0.45f},
	{horde::MonsterTypeID::DAEDALUS_BOMBER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite | MonsterWaveType::Bomber, 19, 0.35f},

	// Elite Ground Units (Waves 18+)
	{horde::MonsterTypeID::GLADIATOR_B, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f},
	{horde::MonsterTypeID::GLADIATOR_C, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f},
	{horde::MonsterTypeID::SHAMBLER, MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 22, 0.4f},
	{horde::MonsterTypeID::TANK_64, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 28, 0.3f},

	// Special Heavy Units (Waves 20+)
	{horde::MonsterTypeID::JANITOR, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Bomber, 21, 0.5f},
	{horde::MonsterTypeID::JANITOR2, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Special | MonsterWaveType::Bomber, 26, 0.4f},
	{horde::MonsterTypeID::MEDIC_COMMANDER, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.3f},

	// Semi-Boss Units (Waves 16+)
	{horde::MonsterTypeID::MAKRON, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 23, 0.01f},
	{horde::MonsterTypeID::PERRO_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Fast | MonsterWaveType::Small, 20, 0.4f},
	{horde::MonsterTypeID::WIDOW1, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 29, 0.3f},
	{horde::MonsterTypeID::SHAMBLER_KL, MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 33, 0.23f},
	{horde::MonsterTypeID::GUNCMDR_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 33, 0.2f},
	{horde::MonsterTypeID::MAKRON_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Elite, 41, 0.2f},
	{horde::MonsterTypeID::BOSS2_KL, MonsterWaveType::Flying | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 46, 0.2f},
	{horde::MonsterTypeID::JORG_SMALL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Medium, 33, 0.4f},

	// Boss Units
	{horde::MonsterTypeID::BOSS2_64, MonsterWaveType::Flying | MonsterWaveType::Elite, 19, 0.2f},
	{horde::MonsterTypeID::BOSS2_MINI, MonsterWaveType::Flying | MonsterWaveType::Elite, 19, 0.2f},
	{horde::MonsterTypeID::CARRIER_MINI, MonsterWaveType::Flying | MonsterWaveType::Heavy |  MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.2f}
};

// Optimized version using a precomputed map
static std::array<MonsterWaveType, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_monsterWaveTypes;
static bool g_monsterWaveTypesInitialized = false;

// Initialize the wave types array once
static void InitializeMonsterWaveTypes() {
	if (g_monsterWaveTypesInitialized) return;

	g_monsterWaveTypes.fill(MonsterWaveType::None);

	// Fill in all the wave types for each monster
	for (const auto& monster : monsterTypes) {
		horde::MonsterTypeID typeId = monster.typeId;
		if (typeId != horde::MonsterTypeID::UNKNOWN) {
			g_monsterWaveTypes[static_cast<size_t>(typeId)] = monster.types;
		}
	}

	g_monsterWaveTypesInitialized = true;
}

// Update GetMonsterWaveTypes to use the precomputed array
// Add a TypeID overload for GetMonsterWaveTypes
inline MonsterWaveType GetMonsterWaveTypes(horde::MonsterTypeID typeId) noexcept {
	if (typeId == horde::MonsterTypeID::UNKNOWN) return MonsterWaveType::None;

	// Make sure the wave types are initialized
	if (!g_monsterWaveTypesInitialized) {
		InitializeMonsterWaveTypes();
	}

	return g_monsterWaveTypes[static_cast<size_t>(typeId)];
}

#include <array>
#include <unordered_set>
#include <random>

// Modified boss_t structure
struct boss_t {
	horde::MonsterTypeID typeId;  // Instead of const char* classname
	int32_t min_level;
	int32_t max_level;
	float weight;
	BossSizeCategory sizeCategory;
	BossType type;
};
static constexpr boss_t BOSS_SMALL[] = {
	{horde::MonsterTypeID::CARRIER_MINI, 24, -1, 0.1f, BossSizeCategory::Small, BossType::CARRIER_MINI},
	{horde::MonsterTypeID::BOSS2_KL, 24, -1, 0.1f, BossSizeCategory::Small, BossType::BOSS2KL},
	{horde::MonsterTypeID::FIXBOT_KL, 9, -1, 0.4f, BossSizeCategory::Small, BossType::FIXBOTKL},
	{horde::MonsterTypeID::WIDOW2, 19, -1, 0.1f, BossSizeCategory::Small, BossType::WIDOW2},
	{horde::MonsterTypeID::TANK_64, -1, -1, 0.25f, BossSizeCategory::Small, BossType::TANK_64},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Small, BossType::SHAMBLERKL},
	{horde::MonsterTypeID::GUNCMDR_KL, -1, 20, 0.3f, BossSizeCategory::Small, BossType::GUNCMDRKL},
	{horde::MonsterTypeID::MAKRON_KL, 36, -1, 0.2f, BossSizeCategory::Small, BossType::MAKRONKL},
	{horde::MonsterTypeID::MAKRON, 16, 26, 0.1f, BossSizeCategory::Small, BossType::OTHER},
	{horde::MonsterTypeID::PSX_ARACHNID, 15, -1, 0.1f, BossSizeCategory::Small, BossType::PSX_ARACHNID},
	{horde::MonsterTypeID::REDMUTANT, -1, 24, 0.1f, BossSizeCategory::Small, BossType::REDMUTANT}
};

static constexpr boss_t BOSS_MEDIUM[] = {
	{horde::MonsterTypeID::CARRIER, 24, -1, 0.1f, BossSizeCategory::Medium, BossType::CARRIER},
	{horde::MonsterTypeID::BOSS2, 19, -1, 0.1f, BossSizeCategory::Medium, BossType::BOSS2},
	{horde::MonsterTypeID::FIXBOT_KL, 9, -1, 0.4f, BossSizeCategory::Small, BossType::FIXBOTKL},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Medium, BossType::SHAMBLERKL},
	{horde::MonsterTypeID::TANK_64, 21, -1, 0.1f, BossSizeCategory::Medium, BossType::TANK_64},
	{horde::MonsterTypeID::SHAMBLER_KL, 21, -1, 0.1f, BossSizeCategory::Medium, BossType::SHAMBLERKL},
	{horde::MonsterTypeID::GUNCMDR_KL, 21, -1, 0.1f, BossSizeCategory::Medium, BossType::GUNCMDRKL},
	{horde::MonsterTypeID::PSX_GUARDIAN, -1, 24, 0.1f, BossSizeCategory::Medium, BossType::PSX_GUARDIAN},
	{horde::MonsterTypeID::WIDOW2, 19, -1, 0.1f, BossSizeCategory::Medium, BossType::WIDOW2},
	{horde::MonsterTypeID::PSX_ARACHNID, -14, -1, 0.1f, BossSizeCategory::Medium, BossType::PSX_ARACHNID},
	{horde::MonsterTypeID::MAKRON_KL, 26, -1, 0.2f, BossSizeCategory::Medium, BossType::MAKRONKL},
	{horde::MonsterTypeID::MAKRON, 16, 25, 0.1f, BossSizeCategory::Medium, BossType::OTHER},
	{horde::MonsterTypeID::REDMUTANT, -1, 24, 0.1f, BossSizeCategory::Small, BossType::REDMUTANT}
};

static constexpr boss_t BOSS_LARGE[] = {
	{horde::MonsterTypeID::CARRIER, 24, -1, 0.1f, BossSizeCategory::Large, BossType::CARRIER},
	{horde::MonsterTypeID::BOSS2, 19, -1, 0.1f, BossSizeCategory::Large, BossType::BOSS2},
	{horde::MonsterTypeID::FIXBOT_KL, 9, -1, 0.4f, BossSizeCategory::Small, BossType::FIXBOTKL},
	{horde::MonsterTypeID::BOSS5, -1, -1, 0.1f, BossSizeCategory::Large, BossType::BOSS5},
	{horde::MonsterTypeID::TANK_64, -1, 20, 0.45f, BossSizeCategory::Large, BossType::TANK_64},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Large, BossType::SHAMBLERKL},
	{horde::MonsterTypeID::GUNCMDR_KL, -1, 20, 0.3f, BossSizeCategory::Large, BossType::GUNCMDRKL},
	{horde::MonsterTypeID::TANK_64, 21, -1, 0.1f, BossSizeCategory::Large, BossType::TANK_64},
	{horde::MonsterTypeID::SHAMBLER_KL, 21, -1, 0.1f, BossSizeCategory::Large, BossType::SHAMBLERKL},
	{horde::MonsterTypeID::PSX_ARACHNID, 14, -1, 0.1f, BossSizeCategory::Large, BossType::PSX_ARACHNID},
	{horde::MonsterTypeID::WIDOW, -1, -1, 0.1f, BossSizeCategory::Large, BossType::WIDOW},
	{horde::MonsterTypeID::PSX_GUARDIAN, -1, -1, 0.1f, BossSizeCategory::Large, BossType::PSX_GUARDIAN},
	{horde::MonsterTypeID::BOSS5, -1, 24, 0.1f, BossSizeCategory::Large, BossType::BOSS5},
	{horde::MonsterTypeID::JORG, 30, -1, 0.15f, BossSizeCategory::Large, BossType::JORG},
	{horde::MonsterTypeID::MAKRON_KL, 30, -1, 0.2f, BossSizeCategory::Large, BossType::MAKRONKL},
	{horde::MonsterTypeID::REDMUTANT, -1, 24, 0.1f, BossSizeCategory::Small, BossType::REDMUTANT}
};

// Optimized GetBossList function
static std::span<const boss_t> GetBossList(const MapSize& mapSize, horde::MapID mapId) {
    if (mapSize.isSmallMap || 
        mapId == horde::MapID::Q2DM4 || 
        mapId == horde::MapID::Q64_COMM || 
        mapId == horde::MapID::TEST_TEST_KAISER) {
        return std::span<const boss_t>(BOSS_SMALL);
    }

	if (mapSize.isMediumMap ||
		mapId == horde::MapID::RDM8 ||
		mapId == horde::MapID::XDM1) {
		if (mapId == horde::MapID::MGU6M3 ||
			mapId == horde::MapID::RBOSS) {
			static std::array<boss_t, std::size(BOSS_MEDIUM)> filteredMediumBossList;
			size_t count = 0;

			for (const auto& boss : std::span<const boss_t>(BOSS_MEDIUM)) {
				if (boss.type != BossType::GUARDIAN && boss.type != BossType::PSX_GUARDIAN) {
					filteredMediumBossList[count++] = boss;
				}
			}

			return std::span<const boss_t>(filteredMediumBossList.data(), count);
		}
		return std::span<const boss_t>(BOSS_MEDIUM);
	}

	if (mapSize.isBigMap ||
		mapId == horde::MapID::TEST_SPBOX
		|| mapId == horde::MapID::Q2CTF4) {
		if (mapId == horde::MapID::TEST_SPBOX
			|| mapId == horde::MapID::Q2CTF4) {
			static std::array<boss_t, std::size(BOSS_LARGE)> filteredLargeBossList;
			size_t count = 0;

			for (const auto& boss : std::span<const boss_t>(BOSS_LARGE)) {
				if (boss.type != BossType::BOSS5) {
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
struct RecentBosses {
	horde::MonsterTypeID items[MAX_RECENT_BOSSES] = { horde::MonsterTypeID::UNKNOWN };
	size_t count = 0;

	void add(horde::MonsterTypeID boss) noexcept {
		// Ignore invalid TypeIDs
		if (boss == horde::MonsterTypeID::UNKNOWN)
			return;

		// Add to array, shift if needed
		if (count < MAX_RECENT_BOSSES) {
			items[count++] = boss;
		}
		else {
			// Shift array entries
			memmove(&items[0], &items[1], sizeof(items[0]) * (MAX_RECENT_BOSSES - 1));
			items[MAX_RECENT_BOSSES - 1] = boss;
		}
	}

	bool contains(horde::MonsterTypeID boss) const noexcept {
		// Check if boss is in recent list
		if (boss == horde::MonsterTypeID::UNKNOWN)
			return false;

		for (size_t i = 0; i < count; ++i) {
			if (items[i] == boss)
				return true;
		}
		return false;
	}

	void clear() noexcept {
		count = 0;
		std::fill_n(items, MAX_RECENT_BOSSES, horde::MonsterTypeID::UNKNOWN);
	}

	// Compatibility method for string interface
	void add(const char* bossClassname) noexcept {
		add(horde::MonsterTypeRegistry::GetTypeID(bossClassname));
	}

	// Compatibility method for string interface
	bool contains(const char* bossClassname) const noexcept {
		return contains(horde::MonsterTypeRegistry::GetTypeID(bossClassname));
	}
};
static RecentBosses recent_bosses;

// Precomputed boss eligibility cache to avoid repeated calculations
struct BossEligibilityCache {
	static constexpr int32_t MAX_PRECOMPUTED_WAVE = 50;

	struct LevelEligibility {
		horde::MonsterTypeID typeIds[MAX_ELIGIBLE_BOSSES] = { horde::MonsterTypeID::UNKNOWN };
		uint8_t count = 0;
	};

	// Cache by map type (0=small, 1=medium, 2=large) and wave level
	LevelEligibility eligibility[3][MAX_PRECOMPUTED_WAVE + 1];
	inline static bool initialized = false;

	void initialize() {
		// For each map type
		for (int mapType = 0; mapType < 3; ++mapType) {
			// Get mapSize for this type
			MapSize mapSize;
			if (mapType == 0) mapSize = { true, false, false };  // Small
			else if (mapType == 2) mapSize = { false, true, false };  // Large
			else mapSize = { false, false, true };  // Medium

			// Get boss list
			const auto bossList = GetBossList(mapSize, horde::MapID::UNKNOWN);

			// For each wave level
			for (int32_t wave = 0; wave <= MAX_PRECOMPUTED_WAVE; ++wave) {
				auto& levelData = eligibility[mapType][wave];
				levelData.count = 0;

				// Filter bosses by level and store TypeIDs directly
				for (const auto& boss : bossList) {
					if ((wave >= boss.min_level || boss.min_level == -1) &&
						(wave <= boss.max_level || boss.max_level == -1)) {

						if (levelData.count < MAX_ELIGIBLE_BOSSES) {
							levelData.typeIds[levelData.count++] = boss.typeId;
						}
					}
				}
			}
		}
		initialized = true;
	}
};
static BossEligibilityCache g_bossEligibilityCache;

// Utility function to spawn a monster by TypeID
edict_t* SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t& origin, const vec3_t& angles, bool applyHordeFlags = true) {
	// Convert TypeID to classname for engine functions
	const char* classname = horde::MonsterTypeRegistry::GetClassname(typeId);
	if (!classname) {
		if (developer->integer) {
			gi.Com_PrintFmt("SpawnMonsterByTypeID: Invalid TypeID\n");
		}
		return nullptr;
	}

	// Create monster entity
	edict_t* monster = G_Spawn();
	if (!monster) {
		return nullptr;
	}

	// Set basic properties
	monster->classname = classname;
	monster->s.origin = origin;
	monster->s.angles = angles;

	// Apply common Horde flags if requested
	if (applyHordeFlags) {
		monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
		monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
		monster->monsterinfo.last_sentrygun_target_time = 0_ms;
		monster->was_spawned_by_horde = true;

		// In spawning state, mark specifically
		if (g_horde_local.state == horde_state_t::spawning) {
			monster->spawned_in_spawn_state = true;
		}
	}

	// Spawn the entity with protection from errors
	monster->solid = SOLID_NOT;  // Start as non-solid to avoid immediate collisions
	ED_CallSpawn(monster);

	// Check if spawn succeeded
	if (!monster->inuse) {
		G_FreeEdict(monster);
		return nullptr;
	}

	// Re-set solid state and properly link
	monster->solid = SOLID_BBOX;
	gi.linkentity(monster);

	return monster;
}

// Optional overload with fewer parameters for common cases
edict_t* SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t& origin) {
	return SpawnMonsterByTypeID(typeId, origin, vec3_origin, true);
}


static horde::MonsterTypeID G_HordePickBOSSType(const MapSize& mapSize, std::string_view mapname, int32_t waveNumber, edict_t* bossEntity) {
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(mapname.data());

	// Initialize boss eligibility cache if needed
	static bool cache_initialized = false;
	if (!cache_initialized) {
		g_bossEligibilityCache.initialize();
		cache_initialized = true;
	}

	// Determine map type index for cache lookup
	const int mapTypeIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);

	// Safe level clamping
	const int32_t safeWaveNumber = std::min(waveNumber, BossEligibilityCache::MAX_PRECOMPUTED_WAVE);

	// Get boss list once
	const auto boss_list = GetBossList(mapSize, mapId);
	if (boss_list.empty()) {
		if (developer->integer) {
			gi.Com_PrintFmt("WARNING: Empty boss list for map {} at wave {}\n",
				mapname.data(), waveNumber);
		}
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Get precomputed eligible bosses for this level
	const auto& eligibilityData = g_bossEligibilityCache.eligibility[mapTypeIndex][safeWaveNumber];

	// Validate precomputed data
	if (eligibilityData.count == 0 || eligibilityData.count > MAX_ELIGIBLE_BOSSES) {
		if (developer->integer) {
			gi.Com_PrintFmt("WARNING: Invalid eligible boss count: {}\n",
				eligibilityData.count);
		}
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Use stack-based array for weight calculations
	struct WeightedBoss {
		const boss_t* boss;
		float weight;
		float cumulativeWeight;
	};

	WeightedBoss weightedBosses[MAX_ELIGIBLE_BOSSES];
	size_t weightedCount = 0;
	float totalWeight = 0.0f;

	// First pass - check precomputed eligible bosses against recent history
	for (size_t i = 0; i < eligibilityData.count; ++i) {
		// Get typeId directly from eligibility data
		horde::MonsterTypeID bossTypeId = eligibilityData.typeIds[i];

		// Skip if unknown type
		if (bossTypeId == horde::MonsterTypeID::UNKNOWN) {
			continue;
		}

		// Find the boss_t entry with this typeId
		const boss_t* boss = nullptr;
		for (const auto& b : boss_list) {
			if (b.typeId == bossTypeId) {
				boss = &b;
				break;
			}
		}

		if (!boss) {
			continue;
		}

		// Validate boss classname before checking contains
		const char* bossClassname = horde::MonsterTypeRegistry::GetClassname(boss->typeId);
		if (!bossClassname) {
			if (developer->integer) {
				gi.Com_PrintFmt("WARNING: Null classname for boss type ID\n");
			}
			continue;
		}

		// Skip if in recent bosses list
		if (recent_bosses.contains(bossClassname)) {
			continue;
		}

		// Calculate weight with optimized factors
		float weight = boss->weight;

		// Combined weight adjustment with fewer branches
		if (waveNumber >= boss->min_level && boss->min_level != -1) {
			// Boost if within ideal level range
			if (waveNumber <= boss->min_level + 5) {
				weight *= 1.3f;
			}
			// Reduce if well beyond ideal level
			else if (waveNumber > boss->min_level + 10) {
				weight *= 0.8f;
			}
		}

		// Apply difficulty adjustment in one check
		if ((g_insane->integer || g_chaotic->integer) &&
			boss->sizeCategory == BossSizeCategory::Large) {
			weight *= 1.2f;
		}

		// Add to weighted list
		if (weightedCount < MAX_ELIGIBLE_BOSSES) {
			totalWeight += weight;
			weightedBosses[weightedCount].boss = boss;
			weightedBosses[weightedCount].weight = weight;
			weightedBosses[weightedCount].cumulativeWeight = totalWeight;
			weightedCount++;
		}
	}

	// If no eligible bosses with history filter, retry without filter
	if (weightedCount == 0 && recent_bosses.count > 0) {
		recent_bosses.clear(); // Reset history

		// Try all bosses from the list
		for (const auto& boss : boss_list) {
			// Check if this boss is eligible for the current wave
			if ((waveNumber >= boss.min_level || boss.min_level == -1) &&
				(waveNumber <= boss.max_level || boss.max_level == -1)) {

				float weight = boss.weight;

				// Add to weighted list
				if (weightedCount < MAX_ELIGIBLE_BOSSES) {
					totalWeight += weight;
					weightedBosses[weightedCount].boss = &boss;
					weightedBosses[weightedCount].weight = weight;
					weightedBosses[weightedCount].cumulativeWeight = totalWeight;
					weightedCount++;
				}
			}
		}
	}

	// Still no eligible bosses? Return unknown
	if (weightedCount == 0 || totalWeight <= 0.0f) {
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Select boss with binary search for better performance
	const float randomValue = frandom() * totalWeight;

	// Binary search through accumulated weights
	size_t left = 0;
	size_t right = weightedCount - 1;

	while (left < right) {
		const size_t mid = (left + right) / 2;
		if (weightedBosses[mid].cumulativeWeight < randomValue) {
			left = mid + 1;
		}
		else {
			right = mid;
		}
	}

	// Get selected boss
	const boss_t* chosen_boss = weightedBosses[left].boss;
	if (chosen_boss) {
		// Add to recent bosses - need to use classname for recent_bosses storage
		recent_bosses.add(horde::MonsterTypeRegistry::GetClassname(chosen_boss->typeId));

		// Set boss size category for the entity
		bossEntity->bossSizeCategory = chosen_boss->sizeCategory;

		// Return the TypeID directly
		return chosen_boss->typeId;
	}

	return horde::MonsterTypeID::UNKNOWN;
}

static const char* G_HordePickBOSS(const MapSize& mapSize, std::string_view mapname, int32_t waveNumber, edict_t* bossEntity) {
	// Simply delegate to the TypeID version and convert the result
	horde::MonsterTypeID typeId = G_HordePickBOSSType(mapSize, mapname, waveNumber, bossEntity);
	return (typeId != horde::MonsterTypeID::UNKNOWN) ?
		horde::MonsterTypeRegistry::GetClassname(typeId) : nullptr;
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

static void UpdateCooldowns(edict_t* spawn_point) {
	auto& data = spawnPointsData[spawn_point];
	data.lastSpawnTime = level.time;
	data.isTemporarilyDisabled = true;
	data.cooldownEndsAt = level.time + SPAWN_POINT_COOLDOWN;
}

// Category check functions using TypeIDs
inline bool IsFlying(horde::MonsterTypeID typeId) {
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Flying);
}

inline bool IsGroundUnit(horde::MonsterTypeID typeId) {
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Ground);
}

inline bool IsSmallUnit(horde::MonsterTypeID typeId) {
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Small);
}

inline bool IsBossUnit(horde::MonsterTypeID typeId) {
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Boss);
}

inline bool IsSpecialUnit(horde::MonsterTypeID typeId) {
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Special);
}

inline bool IsValidMonsterForWave(horde::MonsterTypeID typeId, MonsterWaveType waveRequirements) {
	// Fast exit for no requirements
	if (waveRequirements == MonsterWaveType::None) {
		return true;
	}

	// Special wave types should be exclusive - if one of these is set, the monster MUST have it
	const bool isSpecialWaveType =
		HasWaveType(waveRequirements, MonsterWaveType::Gekk) ||
		HasWaveType(waveRequirements, MonsterWaveType::Berserk) ||
		HasWaveType(waveRequirements, MonsterWaveType::Mutant) ||
		HasWaveType(waveRequirements, MonsterWaveType::Spawner) ||
		HasWaveType(waveRequirements, MonsterWaveType::Shambler) ||
		HasWaveType(waveRequirements, MonsterWaveType::Arachnophobic);

	// For special wave types, enforce strict matching
	if (isSpecialWaveType) {
		// If wave is Gekk, only allow Gekk monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Gekk) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Gekk))
			return false;

		// If wave is Berserker, only allow Berserker monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Berserk) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Berserk))
			return false;

		// If wave is Mutant, only allow Mutant monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Mutant) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Mutant))
			return false;

		// If wave is Spawner, only allow Spawner monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Spawner) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Spawner))
			return false;

		// If wave is Shambler, only allow Shambler monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Shambler) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Shambler))
			return false;

		// If wave is Arachnophobic, only allow Arachnophobic monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Arachnophobic) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Arachnophobic))
			return false;
	}
	else {
		// For regular wave types, check all required flags
		// First check the most important exclusive categories that must match
		if (HasWaveType(waveRequirements, MonsterWaveType::Flying) && !IsFlying(typeId))
			return false;

		if (HasWaveType(waveRequirements, MonsterWaveType::Small) && !IsSmallUnit(typeId))
			return false;

		if (HasWaveType(waveRequirements, MonsterWaveType::Heavy) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Heavy))
			return false;

		if (HasWaveType(waveRequirements, MonsterWaveType::Melee) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Melee))
			return false;

		if (HasWaveType(waveRequirements, MonsterWaveType::Bomber) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Bomber))
			return false;
	}

	// For mixed waves, check if there's at least one match in other categories
	// This only applies to non-special wave types now
	return isSpecialWaveType || (GetMonsterWaveTypes(typeId) & waveRequirements) != MonsterWaveType::None;
}

static horde::MonsterTypeID G_HordePickMonsterType(edict_t* spawn_point) {
	// Static cache to avoid repeated allocations
	static struct {
		struct Entry {
			horde::MonsterTypeID typeId;
			float weight;
			float cumulative_weight;
		};
		Entry entries[32];
		size_t count;
		float total_weight;
	} monster_cache;

	// Track consecutive failures for emergency handling
	static int spawn_selection_failures = 0;
	static MonsterWaveType saved_wave_type = MonsterWaveType::None;
	static bool emergency_mode = false;

	// Reset cache counters
	monster_cache.count = 0;
	monster_cache.total_weight = 0.0f;

	// Early validation for quick exit
	if (!spawn_point || !spawn_point->inuse) {
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Check spawn point availability
	auto& data = spawnPointsData[spawn_point];
	if (data.isTemporarilyDisabled) {
		if (level.time < data.cooldownEndsAt) {
			spawn_selection_failures++;
			return horde::MonsterTypeID::UNKNOWN;
		}
		// Reset state if cooldown expired
		data.isTemporarilyDisabled = false;
		data.attempts = 0;
	}

	// Check if spawn point is occupied
	if (IsSpawnPointOccupied(spawn_point)) {
		IncreaseSpawnAttempts(spawn_point);
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Cache commonly used values
	const int32_t currentLevel = g_horde_local.level;
	MonsterWaveType currentWaveTypes = current_wave_type;
	const bool isSpawnPointFlying = spawn_point->style == 1;

	// Enter emergency mode after multiple failures
	if (spawn_selection_failures >= 3 && !emergency_mode) {
		if (developer->integer) {
			gi.Com_PrintFmt("WARNING: Entering emergency monster selection mode after {} failures\n",
				spawn_selection_failures);
		}

		// Save the original wave type if not already saved
		if (saved_wave_type == MonsterWaveType::None && currentWaveTypes != MonsterWaveType::None) {
			saved_wave_type = currentWaveTypes;
		}

		// Simplify wave type requirements
		if (HasWaveType(currentWaveTypes, MonsterWaveType::Flying)) {
			currentWaveTypes = MonsterWaveType::Flying; // Keep only flying requirement
		}
		else {
			currentWaveTypes = MonsterWaveType::Ground; // Default to ground only
		}

		emergency_mode = true;
	}

	// In severe failure cases, disable wave type filtering completely
	if (spawn_selection_failures >= 6) {
		if (developer->integer && currentWaveTypes != MonsterWaveType::None) {
			gi.Com_PrintFmt("CRITICAL: Disabling wave type filtering after {} failures\n",
				spawn_selection_failures);
		}
		currentWaveTypes = MonsterWaveType::None; // Allow any monster type
	}

	// Determine if we should spawn a higher level monster
	bool spawn_higher_level = false;
	if (currentLevel <= 10) {
		spawn_higher_level = frandom() < 0.16f;
	}
	else if (currentLevel <= 15) {
		spawn_higher_level = frandom() < 0.05f;
	}
	else {
		spawn_higher_level = frandom() < 0.02f;
	}

	// Disable higher level monsters in emergency mode
	if (emergency_mode) {
		spawn_higher_level = false;
	}

	// Calculate effective level for monster selection
	int32_t effectiveLevel = currentLevel;

	if (spawn_higher_level) {
		// Calculate level boost
		int32_t levelBoost;
		if (currentLevel < 7) {
			levelBoost = irandom(2, 4);  // Reduced from 4-7 to 2-4
		}
		else if (currentLevel <= 10) {
			levelBoost = irandom(2, 3);  // Reduced from 3-5 to 2-3
		}
		else {
			levelBoost = irandom(1, 2);  // Reduced from 2-3 to 1-2
		}

		// Cap the effective level conservatively
		int32_t maxLevel = currentLevel < 7 ? irandom(5, 7) :
			(currentLevel < 15 ? currentLevel + 5 : currentLevel + 3);
		effectiveLevel = std::min(currentLevel + levelBoost, maxLevel);

		// Absolute cap at 40
		effectiveLevel = std::min(effectiveLevel, 40);

		// Check if any monster is eligible at this level
		bool any_eligible_monsters = false;
		for (const auto& monster : monsterTypes) {
			if (monster.minWave <= effectiveLevel &&
				(currentWaveTypes == MonsterWaveType::None || IsValidMonsterForWave(monster.typeId, currentWaveTypes))) {

				const bool isFlyingMonster = HasWaveType(GetMonsterWaveTypes(monster.typeId), MonsterWaveType::Flying);
				if (!(isSpawnPointFlying && !isFlyingMonster)) {
					any_eligible_monsters = true;
					break;
				}
			}
		}

		// If no eligible monsters, revert to current level
		if (!any_eligible_monsters) {
			if (developer->integer) {
				gi.Com_PrintFmt("WARNING: No eligible monsters at level {}. Reverting to current wave {}\n",
					effectiveLevel, currentLevel);
			}
			effectiveLevel = currentLevel;
		}
	}

	// Skip boss wave checks in emergency mode
	if (!emergency_mode && currentWaveTypes == MonsterWaveType::None &&
		currentLevel >= 10 && currentLevel % 5 == 0 && !boss_spawned_for_wave) {
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Pre-compute flying spawn adjustment
	const int32_t flyingSpawns = countFlyingSpawns();
	const float adjustmentFactor = adjustFlyingSpawnProbability(flyingSpawns);

	// Build monster cache - prioritize compatibility with spawn point type
	for (const auto& monster : monsterTypes) {
		if (monster_cache.count >= 32) break;

		// Basic level check
		if (monster.minWave > effectiveLevel) continue;

		// Skip if not valid for current wave type using typeId directly
		if (currentWaveTypes != MonsterWaveType::None &&
			!IsValidMonsterForWave(monster.typeId, currentWaveTypes)) {
			continue;
		}

		// Flying compatibility check - always maintain this basic requirement
		if (isSpawnPointFlying && !IsFlying(monster.typeId)) continue;

		// Calculate weight
		float weight = monster.weight;

		// Apply level-based adjustments
		if (currentLevel <= 5) {
			if (!HasWaveType(monster.types, MonsterWaveType::Light)) weight *= 0.3f;
		}
		else if (currentLevel <= 10) {
			if (!HasWaveType(monster.types, MonsterWaveType::Light | MonsterWaveType::Small)) weight *= 0.4f;
		}
		else if (currentLevel <= 15) {
			if (!HasWaveType(monster.types, MonsterWaveType::Medium)) weight *= 0.5f;
		}
		else {
			// Special handling for Flying monsters to ensure they remain relevant in higher waves
			if (IsFlying(monster.typeId)) {
				// Less severe penalty for flying monsters, which are more specialized
				weight *= 0.8f;
			}
			else if (!HasWaveType(monster.types, MonsterWaveType::Heavy | MonsterWaveType::Elite)) {
				weight *= 0.6f;
			}
			else {
				weight *= 1.0f + ((currentLevel - 15) * 0.02f);
			}
		}

		// Ensure earlier monsters don't become too rare
		if (monster.minWave < currentLevel) {
			float relevance = 1.0f - std::min(0.4f, (currentLevel - monster.minWave) * 0.01f);
			weight *= relevance;
		}

		// Ensure no monster drops below a reasonable minimum weight
		if (weight < 0.2f && monster.weight >= 0.2f) {
			weight = 0.2f;
		}

		// In emergency mode, make weights more uniform
		if (emergency_mode) {
			weight = std::max(weight, 0.5f);
		}

		// Special handling for boss wave minions
		if (currentLevel >= 10 && currentLevel % 5 == 0 && boss_spawned_for_wave) {
			weight *= HasWaveType(monster.types, currentWaveTypes) ? 2.0f : 0.3f;
		}

		// Apply difficulty adjustments
		if (g_insane->integer || g_chaotic->integer) {
			const float difficultyScale = currentLevel <= 10 ? 1.1f :
				currentLevel <= 20 ? 1.2f : currentLevel <= 30 ? 1.3f : 1.4f;
			weight *= difficultyScale;
			if (HasWaveType(monster.types, MonsterWaveType::Elite)) weight *= 1.2f;
		}

		// Apply flying adjustment
		weight *= adjustmentFactor;

		// Higher level monster adjustment
		if (spawn_higher_level && monster.minWave > currentLevel) {
			if (monster.minWave - currentLevel > 5) weight *= 0.5f;
		}

		// Add to cache if weight is valid
		if (weight > 0.0f) {
			monster_cache.total_weight += weight;
			auto& entry = monster_cache.entries[monster_cache.count];
			entry.typeId = monster.typeId;
			entry.weight = weight;
			entry.cumulative_weight = monster_cache.total_weight;
			monster_cache.count++;
		}
	}

	horde::MonsterTypeID chosen_monster = horde::MonsterTypeID::UNKNOWN;
	if (monster_cache.count == 0) {
		// Create a weighted collection of appropriate fallback monsters based on wave level
		WeightedSelection<MonsterTypeInfo> fallback_monsters;

		// Find monsters appropriate for this wave level
		int best_min_wave = 1;

		// First pass - find the highest minWave that's still <= currentLevel
		for (const auto& monster : monsterTypes) {
			if (monster.minWave <= currentLevel && monster.minWave > best_min_wave) {
				best_min_wave = monster.minWave;
			}
		}

		// Allow some wiggle room (prefer monsters with minWave within 5 levels of best)
		int min_acceptable_wave = std::max(1, best_min_wave - 5);

		// Second pass - collect appropriate monsters
		for (const auto& monster : monsterTypes) {
			if (monster.minWave >= min_acceptable_wave && monster.minWave <= currentLevel) {
				// Check basic flying compatibility
				const bool isFlyingMonster = HasWaveType(GetMonsterWaveTypes(monster.typeId), MonsterWaveType::Flying);

				// Only enforce flying vs ground compatibility
				if (!(isSpawnPointFlying && !isFlyingMonster)) {
					// Higher weight for monsters closer to the current level
					float weight = 1.0f + ((monster.minWave - min_acceptable_wave) * 0.5f);
					fallback_monsters.add(&monster, weight);
				}
			}
		}

		// If we found any appropriate monsters, select one randomly (weighted)
		if (fallback_monsters.item_count > 0) {
			const MonsterTypeInfo* selected = fallback_monsters.select();
			if (selected) {
				chosen_monster = selected->typeId;
			}
		}

		// Last-resort fallback if still nothing found
		if (chosen_monster == horde::MonsterTypeID::UNKNOWN) {
			// Find ANY monster that meets basic requirements
			for (const auto& monster : monsterTypes) {
				if (monster.minWave <= currentLevel) {
					const bool isFlyingMonster = HasWaveType(GetMonsterWaveTypes(monster.typeId), MonsterWaveType::Flying);
					if (!(isSpawnPointFlying && !isFlyingMonster)) {
						chosen_monster = monster.typeId;
						break;
					}
				}
			}
		}

		if (chosen_monster != horde::MonsterTypeID::UNKNOWN && developer->integer) {
			gi.Com_PrintFmt("EMERGENCY FALLBACK: Selected {} after {} failed attempts\n",
				horde::MonsterTypeRegistry::GetClassname(chosen_monster), spawn_selection_failures);
		}

		// If we found a monster via emergency fallback
		if (chosen_monster != horde::MonsterTypeID::UNKNOWN) {
			// Update cooldowns - Need to use GetClassname here as UpdateCooldowns requires a classname
			UpdateCooldowns(spawn_point);

			// Reset failure counters on success
			spawn_selection_failures = 0;
			emergency_mode = false;

			// Restore original wave type if we were in emergency mode
			if (saved_wave_type != MonsterWaveType::None) {
				current_wave_type = saved_wave_type;
				saved_wave_type = MonsterWaveType::None;
			}

			return chosen_monster;
		}

		// No monster available even with emergency fallback
		IncreaseSpawnAttempts(spawn_point);
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN;
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
	chosen_monster = monster_cache.entries[left].typeId;

	// Update cooldowns if we have a valid selection
	if (chosen_monster != horde::MonsterTypeID::UNKNOWN) {
		UpdateCooldowns(spawn_point);
		// Reset failure counters on success
		spawn_selection_failures = 0;
		emergency_mode = false;

		// Restore original wave type if we were in emergency mode
		if (saved_wave_type != MonsterWaveType::None) {
			current_wave_type = saved_wave_type;
			saved_wave_type = MonsterWaveType::None;
		}
	}

	return chosen_monster;
}

static const char* G_HordePickMonster(edict_t* spawn_point) {
	// Simply delegate to the TypeID version and convert the result
	horde::MonsterTypeID typeId = G_HordePickMonsterType(spawn_point);
	return (typeId != horde::MonsterTypeID::UNKNOWN) ?
		horde::MonsterTypeRegistry::GetClassname(typeId) : nullptr;
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

static inline bool items_precached = false;  // Inline static variable for C++17+

static void PrecacheItemsAndBosses() noexcept {
	// Only precache once
	if (items_precached)
		return;

	std::span<const weighted_item_t> items_view{ items };
	std::span<const MonsterTypeInfo> monsters_view{ monsterTypes };

	// Size hint for better performance
	std::unordered_set<std::string_view> unique_classnames;
	unique_classnames.reserve(items_view.size() + monsters_view.size());

	// Add item classnames using spans
	for (const auto& item : items_view)
		if (item.classname)  // Safety check
			unique_classnames.emplace(item.classname);

	// Add monster classnames using MonsterTypeRegistry::GetClassname
	for (const auto& monster : monsters_view) {
		const char* classname = horde::MonsterTypeRegistry::GetClassname(monster.typeId);
		if (classname)  // Safety check
			unique_classnames.emplace(classname);
	}

	// Precache items
	for (const auto& classname : unique_classnames)
		if (gitem_t* item = FindItemByClassname(classname.data()))
			PrecacheItem(item);

	items_precached = true;
}

static inline bool monsters_precached = false;  // Use inline static for C++17+

// Modified precache function with safety check
static void PrecacheAllMonsters() noexcept {
	// Only precache once
	if (monsters_precached)
		return;

	for (const auto& monster : monsterTypes) {
		// Get classname from typeId using the registry
		const char* classname = horde::MonsterTypeRegistry::GetClassname(monster.typeId);

		// Safety check and precache
		if (classname && *classname) {
			if (gitem_t* item = FindItemByClassname(classname)) {
				PrecacheItem(item);
			}
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
	horde::InitializeHordeIDs();

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

	// Initialize wave types
	InitializeMonsterWaveTypes();
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
	// Fast early-out with combined validation
	if (!g_horde->integer || !boss || !boss->inuse || !boss->monsterinfo.IS_BOSS ||
		boss->monsterinfo.BOSS_DEATH_HANDLED || boss->health > 0) {
		return;
	}

	// Mark as handled immediately to prevent double processing
	boss->monsterinfo.BOSS_DEATH_HANDLED = true;

	// Clean up entity tracking
	OnEntityDeath(boss);
	OnEntityRemoved(boss);
	auto_spawned_bosses.erase(boss);

	// Static drop tables to avoid repeated lookups
	static const char* standardItems[] = {
		"item_adrenaline", "item_pack", "item_sentrygun",
		"item_sphere_defender", "item_armor_combat", "item_bandolier",
		"item_invulnerability", "ammo_nuke"
	};

	// Drop primary weapon based on wave level
	if (const char* weapon_name = SelectBossWeaponDrop(current_wave_level)) {
		if (gitem_t* weapon_item = FindItemByClassname(weapon_name)) {
			if (edict_t* weapon = Drop_Item(boss, weapon_item)) {
				// Set up weapon with enhanced visual effects
				weapon->s.origin = boss->s.origin;
				weapon->velocity = {
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VERTICAL_VELOCITY, MAX_VERTICAL_VELOCITY))
				};
				weapon->movetype = MOVETYPE_BOUNCE;
				weapon->s.effects = EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER;
				weapon->s.renderfx = RF_GLOW;
				weapon->s.alpha = 0.85f;
				weapon->s.scale = 1.25f;
				weapon->spawnflags = SPAWNFLAG_ITEM_DROPPED_PLAYER;
				weapon->flags &= ~FL_RESPAWN;
				gi.linkentity(weapon);
			}
		}
	}

	// Drop special power-up (quad or quadfire)
	if (gitem_t* special_item = FindItemByClassname(brandom() ? "item_quadfire" : "item_quad")) {
		if (edict_t* powerup = Drop_Item(boss, special_item)) {
			powerup->s.origin = boss->s.origin;
			powerup->velocity = {
				static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
				static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
				static_cast<float>(irandom(300, 400))
			};
			powerup->movetype = MOVETYPE_BOUNCE;
			powerup->s.effects = EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER | EF_HOLOGRAM;
			powerup->s.alpha = 0.8f;
			powerup->s.scale = 1.5f;
			powerup->flags &= ~FL_RESPAWN;
			gi.linkentity(powerup);
		}
	}

	// Randomize standard item drops using Fisher-Yates shuffle
	std::array<const char*, 8> shuffled;
	std::copy(std::begin(standardItems), std::end(standardItems), shuffled.begin());
	for (int i = 7; i > 0; --i) {
		int j = irandom(0, i);
		if (i != j) {
			std::swap(shuffled[i], shuffled[j]);
		}
	}

	// Drop standard items
	for (const char* item_name : shuffled) {
		if (gitem_t* item = FindItemByClassname(item_name)) {
			if (edict_t* drop = Drop_Item(boss, item)) {
				drop->s.origin = boss->s.origin;
				drop->velocity = {
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VERTICAL_VELOCITY, MAX_VERTICAL_VELOCITY))
				};
				drop->movetype = MOVETYPE_BOUNCE;
				drop->flags &= ~FL_RESPAWN;
				drop->s.effects |= EF_GIB;
				drop->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
				gi.linkentity(drop);
			}
		}
	}

	// Clean up boss entity
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
	// Clear any existing bosses (existing code remains unchanged)
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end(); ) {
		edict_t* existing_boss = *it;
		if (existing_boss && existing_boss->inuse) {
			existing_boss->monsterinfo.BOSS_DEATH_HANDLED = true;
			OnEntityRemoved(existing_boss);
			G_FreeEdict(existing_boss);
		}
		it = auto_spawned_bosses.erase(it);
	}

	// Reset wave type (existing code remains unchanged)
	current_wave_type = MonsterWaveType::None;

	// Clear health bar (existing code remains unchanged)
	for (size_t i = 0; i < MAX_HEALTH_BARS; ++i) {
		if (level.health_bar_entities[i]) {
			G_FreeEdict(level.health_bar_entities[i]);
			level.health_bar_entities[i] = nullptr;
		}
	}

	// Clear health bar name (existing code remains unchanged)
	gi.configstring(CONFIG_HEALTH_BAR_NAME, "");

	// Early return if not a boss wave (existing code remains unchanged)
	if (g_horde_local.level < 10 || g_horde_local.level % 5 != 0) {
		return;
	}

	// Get map name (existing code remains unchanged)
	const char* map_name = GetCurrentMapName();
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(map_name);

	// MODIFIED CODE: Use the new MapOriginRegistry instead of mapOrigins map
	vec3_t spawn_origin;
	if (mapId == horde::MapID::UNKNOWN || !horde::MapOriginRegistry::GetOrigin(mapId, spawn_origin)) {
		if (developer->integer) {
			gi.Com_PrintFmt("Error: No spawn origin found for map {}\n", map_name);
		}
		return;
	}

	// Validate origin (slightly modified)
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

	// Use TypeID version for boss selection
	horde::MonsterTypeID boss_type = G_HordePickBOSSType(
		g_horde_local.current_map_size, map_name, g_horde_local.level, boss);

	if (boss_type == horde::MonsterTypeID::UNKNOWN) {
		if (orb) G_FreeEdict(orb);
		G_FreeEdict(boss);
		if (developer->integer) {
			gi.Com_PrintFmt("Error: Failed to pick a boss type\n");
		}
		return;
	}

	// Get the classname from TypeID for engine functions
	const char* boss_classname = horde::MonsterTypeRegistry::GetClassname(boss_type);

	// Set up boss entity
	boss_spawned_for_wave = true;
	boss->classname = boss_classname;
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

// Define the array
static std::array<std::string_view, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_bossMessagesArray;
static bool g_bossMessagesInitialized = false;

// Initialize function
void InitializeBossMessages() {
	if (g_bossMessagesInitialized)
		return;

	// Fill with default message first
	g_bossMessagesArray.fill("A Strogg Boss has spawned!\nPrepare for battle!\n");

	// Set specific messages individually by index
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2)] = "***** Boss incoming! Hornet is here, ready for some fresh Marine meat! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2_KL)] = "***** Boss incoming! Hornet is about to strike! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::FIXBOT_KL)] = "***** Boss incoming! The Fixer is coming to fix this... *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER_MINI)] = "***** Boss incoming! Carrier Mini is delivering pain right to your face! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER)] = "***** Boss incoming! Carrier's here with a deadly payload! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::TANK_64)] = "***** Boss incoming! Tank Commander is here to take limbs! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::SHAMBLER_KL)] = "***** Boss incoming! The Shambler is emerging watch out! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::GUNCMDR_KL)] = "***** Boss incoming! Gunner Commander has you in his sights! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::MAKRON_KL)] = "***** Boss incoming! Makron is here to personally finish you off! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::PSX_GUARDIAN)] = "***** Boss incoming! The Enhanced Guardian is ready to spam rockets! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::BOSS5)] = "***** Boss incoming! Super-Tank is here to show Strogg's might! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::WIDOW2)] = "***** Boss incoming! The Widow is weaving disruptor beams just for you! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::PSX_ARACHNID)] = "***** Boss incoming! Arachnid is here *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::REDMUTANT)] = "***** Boss incoming! The Bloody Mutant is out for blood—yours! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::JORG)] = "***** Boss incoming! Jorg's mech is upgraded and deadly! *****\n";

	g_bossMessagesInitialized = true;
}

// Helper function to get message
inline std::string_view GetBossMessage(horde::MonsterTypeID typeId) {
	if (!g_bossMessagesInitialized)
		InitializeBossMessages();

	size_t index = static_cast<size_t>(typeId);
	if (index < g_bossMessagesArray.size())
		return g_bossMessagesArray[index];
	return "A Strogg Boss has spawned!\nPrepare for battle!\n";
}

// Define a struct for boss wave info
struct BossWaveInfo {
	MonsterWaveType waveType;
	const char* message;
};

// Create an array for boss wave types
static std::array<BossWaveInfo, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_bossWaveTypeArray;
static bool g_bossWaveTypesInitialized = false;

// Initialize function
void InitializeBossWaveTypes() {
	if (g_bossWaveTypesInitialized)
		return;

	// Fill with default values first
	for (auto& entry : g_bossWaveTypeArray) {
		entry = { MonsterWaveType::Medium, "\n\n\nDefault wave incoming!\n" };
	}

	// Set specific entries
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::REDMUTANT)] =
	{ MonsterWaveType::Mutant, "\n\n\nMutant's invasion approaches!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::SHAMBLER_KL)] =
	{ MonsterWaveType::Shambler, "\n\n\nMutant's invasion approaches!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::WIDOW)] =
	{ MonsterWaveType::Small, "\n\n\nWidow swarm incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::WIDOW2)] =
	{ MonsterWaveType::Small, "\n\n\nWidow swarm incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::PSX_ARACHNID)] =
	{ MonsterWaveType::Arachnophobic, "\n\n\nArachnophobia wave incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2)] =
	{ MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER)] =
	{ MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER_MINI)] =
	{ MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2_KL)] =
	{ MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::FIXBOT_KL)] =
	{ MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::TANK_64)] =
	{ MonsterWaveType::Medium, "\n\n\nHeavy/Mid armored division incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::PSX_GUARDIAN)] =
	{ MonsterWaveType::Medium, "\n\n\nHeavy/Mid armored division incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS5)] =
	{ MonsterWaveType::Medium, "\n\n\nHeavy/Mid armored division incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::GUNCMDR_KL)] =
	{ MonsterWaveType::Medium, "\n\n\nPrepare bayonets!The invaders are about to get up close and personal!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::MAKRON)] =
	{ MonsterWaveType::Medium, "\n\n\nPrepare bayonets!The invaders are about to get up close and personal!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::MAKRON_KL)] =
	{ MonsterWaveType::Medium, "\n\n\nPrepare bayonets!The invaders are about to get up close and personal!\n" };

	g_bossWaveTypesInitialized = true;
}

// Helper function
inline std::pair<MonsterWaveType, const char*> GetBossWaveType(horde::MonsterTypeID typeId) {
	if (!g_bossWaveTypesInitialized)
		InitializeBossWaveTypes();

	size_t index = static_cast<size_t>(typeId);
	if (index < g_bossWaveTypeArray.size())
		return { g_bossWaveTypeArray[index].waveType, g_bossWaveTypeArray[index].message };
	return { MonsterWaveType::Medium, "\n\n\nDefault wave incoming!\n" };
}

THINK(BossSpawnThink)(edict_t* self) -> void {
	// Remove the black light effect immediately
	if (self->owner) {
		G_FreeEdict(self->owner);
		self->owner = nullptr;
	}

	// Get the monster TypeID directly from the entity's classname
	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);

	// Set wave type based on boss type - using array lookup
	auto bossWaveInfo = GetBossWaveType(typeId);
	if (TrySetWaveType(bossWaveInfo.first)) {
		gi.LocBroadcast_Print(PRINT_CHAT, "{}", bossWaveInfo.second);
	}
	else if (HasWaveType(bossWaveInfo.first, MonsterWaveType::Mutant) ||
		HasWaveType(bossWaveInfo.first, MonsterWaveType::Shambler)) {
		// Fallback for mutant/shambler types
		current_wave_type = MonsterWaveType::Medium;
		StoreWaveType(MonsterWaveType::Medium);
	}

	// Boss spawn message - using array lookup
	std::string_view bossMessage = GetBossMessage(typeId);
	gi.LocBroadcast_Print(PRINT_CHAT, "\n\n\n{}\n", bossMessage.data());

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

bool CheckAndTeleportBoss(edict_t* self, BossTeleportReason reason = BossTeleportReason::DROWNING) {
	// Early returns - unchanged
	if (!self || !self->inuse || self->deadflag ||
		self->monsterinfo.IS_BOSS || level.intermissiontime || !g_horde->integer)
		return false;

	// Get TypeID once instead of multiple string comparisons
	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
	if (typeId == horde::MonsterTypeID::MISC_INSANE || typeId == horde::MonsterTypeID::TURRET) return false;
	// Cache map name once to reduce string comparisons
	static std::string last_map_name;
	static horde::MapID cached_map_id = horde::MapID::UNKNOWN;

	// Only look up map ID if the map has changed
	const char* current_map = GetCurrentMapName();
	if (last_map_name != current_map) {
		last_map_name = current_map;
		cached_map_id = horde::MapOriginRegistry::GetMapID(current_map);

		// If map not found, cache the UNKNOWN ID for quick rejection
		if (cached_map_id == horde::MapID::UNKNOWN)
			return false;
	}
	else if (cached_map_id == horde::MapID::UNKNOWN) {
		// Use cached failure result
		return false;
	}

	// Get the origin and use it directly
	vec3_t spawn_origin;
	if (!horde::MapOriginRegistry::GetOrigin(cached_map_id, spawn_origin)) {
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

	// Process active players efficiently using iterator pattern
	for (auto* player : active_players()) {
		// Use proper null and inuse checks
		if (player && player->inuse && player->client &&
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

	horde::g_monsterSpawnTracker.Reset();
	horde::g_spawnPointTimeTracker.Reset();

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

			// Reset alternative position tracking too
			data.alternative_attempts = 0;
			data.alternative_cooldown = 0_sec;
			data.needs_long_alternative_cooldown = false;
		}
	}
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


// Reset ambush system state
void ResetAmbushSystem() {
	last_ambush_time = 0_sec;
	ambush_cooldown_frames = 0;
	waves_since_ambush = 0;
	ambush_system_initialized = false;
}


void ResetWaveMemory() {
	previous_wave_types.fill(MonsterWaveType::None);
	wave_memory_index = 0;

}

static void ResetRecentBosses() noexcept {
	recent_bosses.clear();
}
void ResetGame() {

	// Si ya se ha ejecutado una vez, retornar inmediatamente
	if (hasBeenReset) {
		gi.Com_PrintFmt("PRINT: Reset already performed, skipping...\n");
		return;
	}

	// Establecer el flag al inicio de la ejecución
	hasBeenReset = true;

	ResetRecentBosses();
	ResetAmbushSystem();
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
	horde::g_monsterSpawnTracker.Reset();
	horde::g_spawnPointTimeTracker.Reset();

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

	// First priority - all monsters are dead or wave advance flag set (checking this first is more efficient)
	if (allowWaveAdvance || Horde_AllMonstersDead()) {
		reason = WaveEndReason::AllMonstersDead;
		ResetWaveAdvanceState();
		return true;
	}

	// Second priority - independent time limit
	if (currentTime >= g_independent_timer_start + g_lastParams.independentTimeThreshold) {
		reason = WaveEndReason::TimeLimitReached;
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
		}
	}

	// Apply aggressive time reduction for few monsters
	// This happens even if conditions are already triggered
	if (g_horde_local.conditionTriggered && remainingMonsters <= MONSTERS_FOR_AGGRESSIVE_REDUCTION) {
		// Base calculation - smoother scaling based on remaining monsters
		float base_time = 3.0f + (remainingMonsters * 0.8f); // 3.8s for 1, 4.6s for 2, 5.4s for 3, etc.
		
		// Boss wave consideration - give substantially more time
		if (IsBossWave() && boss_spawned_for_wave) {
			// Boss waves get significantly more time
			base_time *= 2.0f + (0.2f * remainingMonsters); // 2.2x-3.0x multiplier based on monsters
			
			// Add a minimum time threshold for boss waves
			base_time = std::max(base_time, 8.0f);
		}
		else {
			// Non-boss wave adjustments
			
			// Wave level factor - higher waves get less time (only for non-boss waves)
			if (current_wave_level >= 15) {
				float reduction = std::min((current_wave_level - 15) * 0.02f, 0.3f); // Up to 30% reduction
				base_time *= (1.0f - reduction);
			}
			
			// Player count consideration (only for non-boss waves)
			int32_t playerCount = GetNumHumanPlayers();
			if (playerCount > 1) {
				// Slightly reduce time with more players (they can clear faster)
				float player_reduction = std::min((playerCount - 1) * 0.07f, 0.2f); // Up to 20% reduction
				base_time *= (1.0f - player_reduction);
			}
		}
		
		// Ensure minimum time thresholds (higher for boss waves)
		float min_time = (IsBossWave() && boss_spawned_for_wave) ? 5.0f : 2.0f;
		gtime_t aggressive_time = gtime_t::from_sec(std::max(min_time, base_time));
		
		// Calculate how much original time remains
		const gtime_t original_remaining = g_horde_local.waveEndTime - currentTime;
		
		// Only apply reduction if it's actually faster than current end time
		if (aggressive_time < original_remaining) {
			g_horde_local.waveEndTime = currentTime + aggressive_time;
			
			if (developer->integer) {
				gi.Com_PrintFmt("Aggressive time reduction: {} seconds remaining for {} monsters (wave: {}, boss: {})\n",
					aggressive_time.seconds(), remainingMonsters, current_wave_level,
					(IsBossWave() && boss_spawned_for_wave) ? "yes" : "no");
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
			//    gi.Com_PrintFmt("High wave failsafe: Only {} monsters remain after {}% of timer\n",
			//        remainingMonsters,
			//        static_cast<int>((elapsed / g_horde_local.conditionTimeThreshold) * 100));
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

// Function to attempt dropping a monster to the floor
bool AttemptDropToFloor(vec3_t& position, const vec3_t& mins, const vec3_t& maxs);

// Enhanced emergency spawn position finder
bool FindEmergencySpawnPosition(vec3_t& position, vec3_t& angles, bool& used_human_player, horde::MonsterTypeID typeId)
{
	// Debug trace to identify where freezes occur
	if (developer->integer) {
		gi.Com_PrintFmt("DEBUG: Starting FindEmergencySpawnPosition\n");
	}

	// Initialize human player flag to false
	used_human_player = false;

	// Constants for spawn attempts
	constexpr int MAX_ATTEMPTS = 40;
	constexpr float MIN_PLAYER_DIST = 200.0f;
	constexpr vec3_t MONSTER_MINS = { -16, -16, -24 };
	constexpr vec3_t MONSTER_MAXS = { 16, 16, 32 };

	// FIXED: Replace STL vectors with fixed-size arrays for better performance
	constexpr size_t MAX_PLAYERS = 32; // Adjust based on your game's limits
	edict_t* top_damage_humans[MAX_PLAYERS] = { nullptr };
	edict_t* high_spree_humans[MAX_PLAYERS] = { nullptr };
	edict_t* normal_humans[MAX_PLAYERS] = { nullptr };
	edict_t* bots[MAX_PLAYERS] = { nullptr };
	size_t top_damage_count = 0, high_spree_count = 0, normal_count = 0, bot_count = 0;

	// Track highest damage/spree values for relative comparison
	int32_t highest_damage = 0;
	int highest_spree = 0;

	// First pass - categorize players
	for (auto* player : active_players()) {
		if (!player || !player->inuse || !player->client ||
			player->health <= 0 || ClientIsSpectating(player->client))
			continue;

		// Check if this is a human or bot
		bool is_human = !(player->svflags & SVF_BOT);

		if (is_human) {
			// Track damage and spree stats
			int32_t player_damage = player->client->total_damage;
			int player_spree = player->client->resp.spree;

			// Track highest values for relative comparison
			highest_damage = std::max(highest_damage, player_damage);
			highest_spree = std::max(highest_spree, player_spree);

			// Add to appropriate human categories using fixed arrays
			if (player_damage > 0 && player_damage >= highest_damage * 0.7f) {
				if (top_damage_count < MAX_PLAYERS) {
					top_damage_humans[top_damage_count++] = player;
				}
			}
			else if (player_spree >= 20) {
				if (high_spree_count < MAX_PLAYERS) {
					high_spree_humans[high_spree_count++] = player;
				}
			}
			else {
				if (normal_count < MAX_PLAYERS) {
					normal_humans[normal_count++] = player;
				}
			}
		}
		else {
			// It's a bot
			if (bot_count < MAX_PLAYERS) {
				bots[bot_count++] = player;
			}
		}
	}

	// If we have no players at all, return false - no valid position
	if (top_damage_count == 0 && high_spree_count == 0 &&
		normal_count == 0 && bot_count == 0) {
		if (developer->integer) {
			gi.Com_PrintFmt("DEBUG: Finished FindEmergencySpawnPosition, result: false (no players)\n");
		}
		return false;
	}

	// Define priority groups using fixed arrays
	struct PlayerGroup {
		edict_t** players;
		size_t count;
		bool is_human;
	};

	PlayerGroup priority_groups[4] = {
		{ top_damage_humans, top_damage_count, true },
		{ high_spree_humans, high_spree_count, true },
		{ normal_humans, normal_count, true },
		{ bots, bot_count, false }
	};

	// Helper function to validate a position
	auto validatePosition = [&](const vec3_t& pos) -> bool {
		// First check if position is fundamentally valid
		if (!is_valid_vector(pos))
			return false;

		// Check if the position is in solid
		trace_t trace = gi.trace(pos, MONSTER_MINS, MONSTER_MAXS, pos, nullptr, MASK_MONSTERSOLID);
		if (trace.startsolid || trace.allsolid)
			return false;

		// Specific checks based on monster type
		if (typeId != horde::MonsterTypeID::UNKNOWN) {
			// FIXED: Simplified Gekk water validation logic
			if (typeId == horde::MonsterTypeID::GEKK) {
				int contents = gi.pointcontents(pos);
				bool in_water = (contents & CONTENTS_WATER);
				bool in_bad_liquid = (contents & (CONTENTS_LAVA | CONTENTS_SLIME));

				// Gekks should generally be in water, but never in lava/slime
				if (in_bad_liquid || (!in_water && frandom() < 0.5f)) {
					return false;
				}
			}
			// For non-gekk, non-flying monsters, generally avoid water
			else if (!IsFlying(typeId) && (gi.pointcontents(pos) & MASK_WATER)) {
				return false;
			}
		}

		return true;
		};

	// Try each category in priority order
	for (int group_idx = 0; group_idx < 4; group_idx++) {
		const auto& group = priority_groups[group_idx];
		if (group.count == 0)
			continue;

		// Try to find a position near a player from this group
		for (int attempt = 0; attempt < MAX_ATTEMPTS / 4; attempt++) {
			// Pick a random player from the group
			edict_t* player = group.players[irandom(group.count)];

			// Calculate random offset - bias toward closer distances for better gameplay
			float radius;
			if (frandom() < 0.6f) {
				// Closer range (200-600)
				radius = MIN_PLAYER_DIST + frandom() * 400.0f;
			}
			else {
				// Farther range (600-1200)
				radius = 600.0f + frandom() * 600.0f;
			}

			float angle = frandom() * 2.0f * PI;

			// Set position at offset from player
			vec3_t test_pos = {
				player->s.origin[0] + cosf(angle) * radius,
				player->s.origin[1] + sinf(angle) * radius,
				player->s.origin[2] + 24.0f  // Start slightly above player Z
			};

			// Trace down to find floor
			trace_t trace = gi.traceline(test_pos, test_pos - vec3_t{ 0, 0, 512 }, nullptr, MASK_SOLID);

			if (trace.fraction < 1.0f) {
				// Found something below - adjust to slightly above hit point
				test_pos = trace.endpos + vec3_t{ 0, 0, 1.0f };

				// Try different heights if initial position is invalid
				vec3_t final_pos = test_pos;
				bool found_valid = false;

				// First try the base position
				if (validatePosition(final_pos)) {
					found_valid = true;
				}

				// Try a few different heights around this position
				if (!found_valid) {
					for (float height_adj : {8.0f, 16.0f, 24.0f, -8.0f, -16.0f}) {
						final_pos = test_pos;
						final_pos.z += height_adj;

						if (validatePosition(final_pos)) {
							found_valid = true;
							break;
						}
					}
				}

				// Try slight lateral adjustments if still not valid
				if (!found_valid) {
					for (float x_adj : {-16.0f, 16.0f, -32.0f, 32.0f}) {
						for (float y_adj : {-16.0f, 16.0f, -32.0f, 32.0f}) {
							final_pos = test_pos + vec3_t{ x_adj, y_adj, 0.0f };

							if (validatePosition(final_pos)) {
								found_valid = true;
								break;
							}
						}
						if (found_valid) break;
					}
				}

				if (found_valid) {
					// Found a valid position!
					position = final_pos;

					// Calculate angle facing the player
					vec3_t dir = position - player->s.origin;
					dir.z = 0; // Keep angle level
					angles = vectoangles(dir);

					// Set flag indicating whether we used a human player
					used_human_player = group.is_human;

					if (developer->integer) {
						gi.Com_PrintFmt("DEBUG: Finished FindEmergencySpawnPosition, result: true\n");
					}
					return true;
				}
			}
		}
	}

	// No valid position found after all attempts
	if (developer->integer) {
		gi.Com_PrintFmt("DEBUG: Finished FindEmergencySpawnPosition, result: false (no valid position)\n");
	}
	return false;
}

// String-based overload that delegates to the TypeID version
bool FindEmergencySpawnPosition(vec3_t& position, vec3_t& angles, bool& used_human_player, const char* monster_classname)
{
	// Convert classname to TypeID
	horde::MonsterTypeID typeId = monster_classname ?
		horde::MonsterTypeRegistry::GetTypeID(monster_classname) :
		horde::MonsterTypeID::UNKNOWN;

	// Delegate to the TypeID version
	return FindEmergencySpawnPosition(position, angles, used_human_player, typeId);
}

// Helper function to check if any human (non-bot) players are active and not spectating
bool AreHumanPlayersPresent() {
	for (auto* player : active_players()) {
		if (player && player->inuse && player->client &&
			!(player->svflags & SVF_BOT) &&
			!ClientIsSpectating(player->client) &&
			player->health > 0) {
			return true;
		}
	}
	return false;
}

// Create a comprehensive validation function that tries multiple heights
// Improved ValidateSpawnPosition function
bool ValidateSpawnPosition(vec3_t& position, const vec3_t& mins, const vec3_t& maxs, bool allow_defense_fallback = false) {
	// Try original position first
	trace_t trace = gi.trace(position, mins, maxs, position, nullptr, MASK_MONSTERSOLID);
	if (!trace.startsolid && !trace.allsolid) {
		// Check for player defenses specifically
		trace_t defense_trace = gi.trace(position, mins, maxs, position, nullptr, MASK_PLAYERSOLID);
		bool has_defense = (defense_trace.ent && defense_trace.ent != world && IsPlayerDefense(defense_trace.ent));

		// If no defenses found, position is valid
		if (!has_defense) {
			return true;
		}

		// Position has defenses but is otherwise valid
		if (allow_defense_fallback) {
			if (developer->integer) {
				gi.Com_PrintFmt("WARNING: Using fallback position with player defense\n");
			}
			return true;
		}
	}

	// Define offsets with increasing distances
	const std::array<float, 5> offsets = { 0.0f, 10.0f, -10.0f, 20.0f, -20.0f };

	// Track positions blocked by defenses vs. solidness for debug
	int defense_blocks = 0;
	int solid_blocks = 0;

	// Track the best position so far (blocked only by defense, not solid)
	vec3_t best_fallback_pos = position;
	bool found_fallback = false;

	// Try different offsets in increasing distance
	for (float xOffset : offsets) {
		for (float yOffset : offsets) {
			for (float zOffset : offsets) {
				// Skip origin position (already checked)
				if (xOffset == 0.0f && yOffset == 0.0f && zOffset == 0.0f)
					continue;

				vec3_t test_pos = position + vec3_t{ xOffset, yOffset, zOffset };

				// Check if position is non-solid
				trace_t trace = gi.trace(test_pos, mins, maxs, test_pos, nullptr, MASK_MONSTERSOLID);
				if (trace.startsolid || trace.allsolid) {
					solid_blocks++;
					continue;  // Skip solid positions
				}

				// Check for player defenses
				trace_t defense_trace = gi.trace(test_pos, mins, maxs, test_pos, nullptr, MASK_PLAYERSOLID);
				if (defense_trace.ent && defense_trace.ent != world && IsPlayerDefense(defense_trace.ent)) {
					defense_blocks++;

					// Save as potential fallback position
					if (!found_fallback) {
						best_fallback_pos = test_pos;
						found_fallback = true;
					}
					continue;  // Skip positions with player defenses
				}

				// Found a valid position
				position = test_pos;

				if (developer->integer > 1) {
					gi.Com_PrintFmt("Adjusted spawn position by offset ({}, {}, {})\n",
						xOffset, yOffset, zOffset);
				}

				return true;
			}
		}
	}

	// Enhanced grid search with wider range
	const float extended_offsets[] = { 30.0f, -30.0f, 40.0f, -40.0f, 50.0f, -50.0f };
	for (float xOffset : extended_offsets) {
		for (float yOffset : extended_offsets) {
			for (float zOffset : {0.0f, 10.0f, -10.0f, 20.0f, -20.0f}) {
				vec3_t test_pos = position + vec3_t{ xOffset, yOffset, zOffset };

				// Check if position is non-solid
				trace_t trace = gi.trace(test_pos, mins, maxs, test_pos, nullptr, MASK_MONSTERSOLID);
				if (trace.startsolid || trace.allsolid) {
					continue;  // Skip solid positions
				}

				// Check for player defenses
				trace_t defense_trace = gi.trace(test_pos, mins, maxs, test_pos, nullptr, MASK_PLAYERSOLID);
				if (defense_trace.ent && defense_trace.ent != world && IsPlayerDefense(defense_trace.ent)) {
					continue;  // Skip positions with player defenses
				}

				// Found a valid position
				position = test_pos;
				return true;
			}
		}
	}

	// No valid position found
	if (developer->integer) {
		gi.Com_PrintFmt("ValidateSpawnPosition failed: {} solid blocks, {} defense blocks\n",
			solid_blocks, defense_blocks);
	}

	// Use fallback position if allowed and found
	if (allow_defense_fallback && found_fallback) {
		position = best_fallback_pos;

		if (developer->integer) {
			gi.Com_PrintFmt("WARNING: Using fallback position with player defense\n");
		}

		return true;
	}

	return false;
}
// Improved CheckAndTeleportStuckMonster with enhanced safety
// Add these at file scope (outside any function)
static int recent_teleport_count = 0;
static gtime_t last_teleport_reset_time = 0_sec;
static constexpr gtime_t GLOBAL_TELEPORT_RESET_INTERVAL = 3_sec;
static constexpr int MAX_TELEPORTS_PER_INTERVAL = 2; // Cap at 2 teleports per 3 seconds

// For tracking recent teleport locations
static constexpr int MAX_RECENT_TELEPORT_LOCATIONS = 5;
static std::array<vec3_t, MAX_RECENT_TELEPORT_LOCATIONS> recent_teleport_locations;
static std::array<gtime_t, MAX_RECENT_TELEPORT_LOCATIONS> recent_teleport_times;
static int next_teleport_location_index = 0;
static constexpr float MIN_TELEPORT_DISTANCE_SQUARED = 250.0f * 250.0f; // 250 units min distance

bool CheckAndTeleportStuckMonster(edict_t* self) {
	// Reset the global counter periodically
	if (level.time - last_teleport_reset_time > GLOBAL_TELEPORT_RESET_INTERVAL) {
		recent_teleport_count = 0;
		last_teleport_reset_time = level.time;
	}

	// Check if global rate limit is reached
	int max_teleports = MAX_TELEPORTS_PER_INTERVAL;
	if ((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) {
		max_teleports += 1; // Allow one more teleport in high difficulty modes
	}

	if (recent_teleport_count >= max_teleports) {
		return false; // Too many recent teleports
	}

	// Early returns - unchanged
	if (!self || !self->inuse || self->deadflag ||
		self->monsterinfo.IS_BOSS || level.intermissiontime || !g_horde->integer)
		return false;

	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname); 
	if (typeId == horde::MonsterTypeID::MISC_INSANE || typeId == horde::MonsterTypeID::TURRET) return false;

	constexpr gtime_t NO_DAMAGE_TIMEOUT = 25_sec;
	constexpr gtime_t STUCK_CHECK_TIME = 10_sec;

	// Using randomized cooldown range instead of fixed
	constexpr gtime_t MIN_TELEPORT_COOLDOWN = 12_sec;
	constexpr gtime_t MAX_TELEPORT_COOLDOWN = 20_sec;

	// If can see enemy, don't teleport
	if (self->monsterinfo.issummoned ||
		(self->enemy && self->enemy->inuse && visible(self, self->enemy, false))) {
		self->monsterinfo.was_stuck = false;
		self->monsterinfo.stuck_check_time = 0_sec;
		return false;
	}

	// Check individual teleport cooldown
	if (self->teleport_time && level.time < self->teleport_time)
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

	// Add 40% chance to skip processing - additional natural staggering
	if (frandom() < 0.4f) {
		return false;
	}

	// Check if human players are present before attempting player-based teleport
	bool humans_present = AreHumanPlayersPresent();

	// Reduced teleport chance scaling - more conservative
	float teleport_chance = 0.01f; // Base 1% chance
	if (current_wave_level > 5) {
		// Different scaling based on presence of humans
		if (humans_present) {
			// Cap at 8% instead of 15%
			teleport_chance = std::min(0.01f + (current_wave_level - 5) * 0.003f, 0.08f);
		}
		else {
			// Cap at 5% instead of 8%
			teleport_chance = std::min(0.01f + (current_wave_level - 5) * 0.002f, 0.05f);
		}
	}

	const bool use_player_teleport = (frandom() < teleport_chance);

	if (use_player_teleport) {
		// Hide from clients before unlinking
		self->svflags |= SVF_NOCLIENT;
		gi.unlinkentity(self);

		// Get teleport position near players
		vec3_t new_origin, new_angles;
		bool teleported_to_human = false;

		if (FindEmergencySpawnPosition(new_origin, new_angles, teleported_to_human, self->classname)) {
			// Check if too close to recent teleport locations
			bool too_close_to_recent = false;
			for (int i = 0; i < MAX_RECENT_TELEPORT_LOCATIONS; i++) {
				if (level.time - recent_teleport_times[i] < 5_sec) { // Only check recent teleports
					if ((new_origin - recent_teleport_locations[i]).lengthSquared() < MIN_TELEPORT_DISTANCE_SQUARED) {
						too_close_to_recent = true;
						break;
					}
				}
			}

			if (too_close_to_recent) {
				// Too close to recent teleport, restore visibility and try again later
				self->svflags &= ~SVF_NOCLIENT;
				gi.linkentity(self);
				return false;
			}

			// Backup original properties
			const vec3_t old_velocity = self->velocity;
			const vec3_t old_origin = self->s.origin;
			const vec3_t old_angles = self->s.angles;

			// Set new position
			self->s.origin = new_origin;
			self->s.old_origin = new_origin;
			self->s.angles = new_angles;
			self->velocity = vec3_origin;

			// Use improved position validation
			bool position_valid = ValidateSpawnPosition(self->s.origin, self->mins, self->maxs);

			if (!position_valid) {
				// Restore original values if teleport position is invalid
				self->s.origin = old_origin;
				self->s.old_origin = old_origin;
				self->s.angles = old_angles;
				self->velocity = old_velocity;

				// Make visible again and return
				self->svflags &= ~SVF_NOCLIENT;
				gi.linkentity(self);
				return false;
			}

			// Make visible again after successful teleport
			self->svflags &= ~SVF_NOCLIENT;
			gi.linkentity(self);

			// Apply effects
			gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
			SpawnGrow_Spawn(self->s.origin, 80.0f, 10.0f);

			// Update monster state
			self->monsterinfo.was_stuck = false;
			self->monsterinfo.stuck_check_time = 0_sec;
			self->monsterinfo.react_to_damage_time = level.time;

			// Apply randomized cooldown instead of fixed
			self->teleport_time = level.time + random_time(MIN_TELEPORT_COOLDOWN, MAX_TELEPORT_COOLDOWN);

			// Update global teleport tracking
			recent_teleport_count++;

			// Record teleport location
			recent_teleport_locations[next_teleport_location_index] = self->s.origin;
			recent_teleport_times[next_teleport_location_index] = level.time;
			next_teleport_location_index = (next_teleport_location_index + 1) % MAX_RECENT_TELEPORT_LOCATIONS;

			// Only show human message if we actually teleported near a human
			if (developer->integer) {
				if (teleported_to_human) {
					gi.Com_PrintFmt("Monster teleported near human player: {}\n", self->classname);
				}
				else {
					gi.Com_PrintFmt("Monster teleported near bot: {}\n", self->classname);
				}
			}

			return true;
		}

		// Emergency teleport failed, restore visibility
		self->svflags &= ~SVF_NOCLIENT;
		gi.linkentity(self);

		// Fall through to normal teleport method
	}

	// If not using player teleport or it failed, continue with original method
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

		// Use improved position validation
		bool position_suitable = ValidateSpawnPosition(self->s.origin, self->mins, self->maxs);

		if (!position_suitable) {
			// Restore position if position is unsuitable
			self->s.origin = old_origin;
			self->s.old_origin = old_origin;
			self->velocity = old_velocity;

			// Make visible again and return
			self->svflags &= ~SVF_NOCLIENT;
			gi.linkentity(self);
			return false;
		}

		// Make visible again only after successful teleport
		self->svflags &= ~SVF_NOCLIENT;
		gi.linkentity(self);

		// Effects after we're visible
		gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
		SpawnGrow_Spawn(self->s.origin, 80.0f, 10.0f);

		self->monsterinfo.was_stuck = false;
		self->monsterinfo.stuck_check_time = 0_sec;
		self->monsterinfo.react_to_damage_time = level.time;

		// Apply randomized cooldown
		self->teleport_time = level.time + random_time(MIN_TELEPORT_COOLDOWN, MAX_TELEPORT_COOLDOWN);

		// Update global teleport tracking
		recent_teleport_count++;

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

		// Calculate spawn progress - how many monsters were spawned vs. total planned
		const int32_t total_planned = g_totalMonstersInWave + g_horde_local.num_to_spawn + g_horde_local.queued_monsters;
		const float spawn_progress = total_planned > 0 ?
			static_cast<float>(g_totalMonstersInWave) / total_planned : 0.0f;

		// Only transition if:
		// 1. Multiple monsters have been killed during spawning AND
		// 2. We've either spawned a significant portion of the wave OR a minimum number of monsters
		// 3. The last condition ensures we don't transition too early in the wave
		if (spawn_state_deaths >= 4 && (spawn_progress >= 0.5f || g_totalMonstersInWave >= 8)) {
			if (developer->integer) {
				gi.Com_PrintFmt("SPAWN TRANSITION: {} monsters killed during spawning. "
					"{} spawned of {} planned ({:.1f}%). Transitioning to active_wave.\n",
					spawn_state_deaths, g_totalMonstersInWave, total_planned,
					spawn_progress * 100.0f);
			}

			// Force transition to active_wave
			if (!next_wave_message_sent) {
				VerifyAndAdjustBots();
				// Use a more immersive message
				//gi.LocBroadcast_Print(PRINT_CHAT,
				//	"\n\n\nStrogg forces engaged! Combat phase activated.\n");
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

// Function to attempt dropping a monster to the floor
bool AttemptDropToFloor(vec3_t& position, const vec3_t& mins, const vec3_t& maxs) {
	// Try a trace straight down to find floor
	vec3_t start = position;
	vec3_t end = position - vec3_t{ 0, 0, 512 };

	trace_t trace = gi.trace(start, mins, maxs, end, nullptr, MASK_MONSTERSOLID);

	// If we hit something that isn't immediately below us
	if (trace.fraction > 0.01f && trace.fraction < 1.0f) {
		// Found floor, set position to just above it
		position = trace.endpos + vec3_t{ 0, 0, 1.0f };

		// Final check to make sure position is valid
		trace = gi.trace(position, mins, maxs, position, nullptr, MASK_MONSTERSOLID);
		if (!trace.startsolid && !trace.allsolid)
			return true;
	}

	return false;
}

// Add this struct definition near the top of your file
struct RecentSpawnPosition {
	vec3_t position;
	gtime_t cooldown_until;
};

// Add these global variables
static constexpr size_t MAX_RECENT_POSITIONS = 32;
static std::array<RecentSpawnPosition, MAX_RECENT_POSITIONS> g_recent_spawn_positions;
static size_t g_recent_position_index = 0;

// Add these helper functions
void MarkPositionAsRecentlyUsed(const vec3_t& position, gtime_t cooldown_duration) {
	g_recent_spawn_positions[g_recent_position_index] = {
		position,
		level.time + cooldown_duration
	};
	g_recent_position_index = (g_recent_position_index + 1) % MAX_RECENT_POSITIONS;
}

bool IsPositionTooCloseToRecent(const vec3_t& position, float min_distance) {
	const gtime_t current_time = level.time;
	for (const auto& recent : g_recent_spawn_positions) {
		if (recent.cooldown_until > current_time) {
			const float distance_squared = (position - recent.position).lengthSquared();
			if (distance_squared < min_distance * min_distance) {
				return true;
			}
		}
	}
	return false;
}

bool TryAlternativeSpawnPosition(edict_t* spawn_point, horde::MonsterTypeID typeId, vec3_t& final_origin, vec3_t& final_angles) {
	// Constants for alternative spawn positions
	constexpr float HEIGHT_OFFSET = 8.0f;
	constexpr vec3_t MONSTER_MINS = { -16.0f, -16.0f, -24.0f };  // Approximate monster bounds
	constexpr vec3_t MONSTER_MAXS = { 16.0f, 16.0f, 32.0f };     // Adjust as needed

	// Start with the spawn point's position and angles
	const vec3_t base_origin = spawn_point->s.origin;
	const vec3_t base_angles = spawn_point->s.angles;

	// Test different heights first at the original position
	for (float height_offset : {0.0f, HEIGHT_OFFSET, -HEIGHT_OFFSET, HEIGHT_OFFSET * 2, -HEIGHT_OFFSET * 2}) {
		vec3_t test_origin = base_origin;
		test_origin.z += height_offset;

		// Use improved validation
		if (ValidateSpawnPosition(test_origin, MONSTER_MINS, MONSTER_MAXS)) {
			// Add check for proximity to recent spawn positions
			if (!IsPositionTooCloseToRecent(test_origin, 60.0f)) {
				final_origin = test_origin;
				final_angles = base_angles;
				// Mark this position as recently used
				MarkPositionAsRecentlyUsed(test_origin, 3.0_sec);
				return true;
			}
		}
	}

	// Try concentric circles with increasing radius
	const float radii[] = { 30.0f, 60.0f, 90.0f, 120.0f, 150.0f };
	const int angles_per_circle = 8;  // Try 8 angles per radius

	for (float radius : radii) {
		for (int i = 0; i < angles_per_circle; i++) {
			// Evenly distribute points around the circle
			float angle = (i * 2.0f * PI) / angles_per_circle;

			// Calculate offset
			vec3_t offset = {
				cosf(angle) * radius,
				sinf(angle) * radius,
				0.0f
			};

			// Test different heights at this position
			for (float height_offset : {0.0f, HEIGHT_OFFSET, -HEIGHT_OFFSET, HEIGHT_OFFSET * 2, -HEIGHT_OFFSET * 2}) {
				vec3_t test_origin = base_origin + offset;
				test_origin.z += height_offset;

				// Trace from spawn point to test position to ensure no obstacles
				trace_t trace = gi.traceline(base_origin, test_origin, spawn_point, MASK_SOLID);
				if (radius > 60.0f && trace.fraction < 0.8f)  // More forgiving than before
					continue;

				// Try drop to floor if we need to
				if (height_offset >= 0) {  // Only drop from zero or above positions
					AttemptDropToFloor(test_origin, MONSTER_MINS, MONSTER_MAXS);
				}

				// Special handling for specific monster types
				if (typeId != horde::MonsterTypeID::UNKNOWN) {
					// Check for Gekk specifically to handle water requirements
					if (typeId == horde::MonsterTypeID::GEKK) {
						int contents = gi.pointcontents(test_origin);
						bool in_water = (contents & CONTENTS_WATER);
						bool in_bad_liquid = (contents & (CONTENTS_LAVA | CONTENTS_SLIME));

						// Gekks should generally be in water, but never in lava/slime
						if (in_bad_liquid || (!in_water && frandom() < 0.5f)) {
							continue;
						}
					}
					// For non-Gekk, non-flying monsters, generally avoid water
					else if (!IsFlying(typeId) && (gi.pointcontents(test_origin) & MASK_WATER)) {
						continue;
					}
				}

				// Validate final position
				if (ValidateSpawnPosition(test_origin, MONSTER_MINS, MONSTER_MAXS)) {
					// Check for proximity to recent spawn positions
					if (!IsPositionTooCloseToRecent(test_origin, 60.0f)) {
						final_origin = test_origin;
						final_angles = base_angles;
						// Adjust angles to face away from center
						final_angles[YAW] = atan2f(offset.y, offset.x) * (180.0f / PI);
						// Mark this position as recently used
						MarkPositionAsRecentlyUsed(test_origin, 3.0_sec);
						return true;
					}
				}
			}
		}
	}

	// Enhanced grid-based search as last resort
	const float x_offsets[] = { 0.0f, 20.0f, -20.0f, 40.0f, -40.0f, 60.0f, -60.0f, 80.0f, -80.0f };
	const float y_offsets[] = { 0.0f, 20.0f, -20.0f, 40.0f, -40.0f, 60.0f, -60.0f, 80.0f, -80.0f };
	const float z_offsets[] = { 0.0f, 8.0f, -8.0f, 16.0f, -16.0f, 24.0f, -24.0f, 32.0f, -32.0f };

	for (float xOffset : x_offsets) {
		for (float yOffset : y_offsets) {
			for (float zOffset : z_offsets) {
				// Skip origin (already checked)
				if (xOffset == 0.0f && yOffset == 0.0f && zOffset == 0.0f)
					continue;

				vec3_t test_origin = base_origin + vec3_t{ xOffset, yOffset, zOffset };

				// Try drop to floor for positions with positive Z offset
				if (zOffset >= 0) {
					AttemptDropToFloor(test_origin, MONSTER_MINS, MONSTER_MAXS);
				}

				if (ValidateSpawnPosition(test_origin, MONSTER_MINS, MONSTER_MAXS)) {
					final_origin = test_origin;
					final_angles = base_angles;
					// Adjust yaw to face away from the center point for better positioning
					if (xOffset != 0.0f || yOffset != 0.0f) {
						final_angles[YAW] = atan2f(yOffset, xOffset) * (180.0f / PI);
					}
					return true;
				}
			}
		}
	}

	// Final attempt - try allowing spawn even with defenses if we couldn't find alternatives
	for (float radius : {30.0f, 60.0f, 90.0f}) {
		for (int i = 0; i < 8; i++) {
			float angle = (i * 2.0f * PI) / 8;
			vec3_t offset = {
				cosf(angle) * radius,
				sinf(angle) * radius,
				0.0f
			};

			vec3_t test_origin = base_origin + offset;
			// Allow defense fallback set to true as last resort
			if (ValidateSpawnPosition(test_origin, MONSTER_MINS, MONSTER_MAXS, true)) {
				final_origin = test_origin;
				final_angles = base_angles;
				final_angles[YAW] = atan2f(offset.y, offset.x) * (180.0f / PI);
				return true;
			}
		}
	}

	return false;  // No valid position found
}

// String-based overload that delegates to the TypeID version
bool TryAlternativeSpawnPosition(edict_t* spawn_point, const char* monster_classname, vec3_t& final_origin, vec3_t& final_angles)
{
	// Convert classname to TypeID
	horde::MonsterTypeID typeId = monster_classname ?
		horde::MonsterTypeRegistry::GetTypeID(monster_classname) :
		horde::MonsterTypeID::UNKNOWN;

	// Delegate to the TypeID version
	return TryAlternativeSpawnPosition(spawn_point, typeId, final_origin, final_angles);
}

// TypeID-based overload for EmergencySpawnMonster
bool EmergencySpawnMonster(const int32_t levelNum, horde::MonsterTypeID typeId) {
	// Convert TypeID to classname only for the engine functions that need strings
	const char* monster_classname = horde::MonsterTypeRegistry::GetClassname(typeId);
	if (!monster_classname) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Invalid monster type ID\n");
		}
		return false;
	}

	// Find emergency position with our improved function
	vec3_t emergency_origin, emergency_angles;
	bool used_human_player = false;

	if (!FindEmergencySpawnPosition(emergency_origin, emergency_angles, used_human_player, monster_classname)) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Could not find valid position\n");
		}
		return false;
	}

	// Additional validation of final position with improved validation
	constexpr vec3_t MONSTER_MINS = { -16, -16, -24 };
	constexpr vec3_t MONSTER_MAXS = { 16, 16, 32 };

	// Use improved validation logic
	if (!ValidateSpawnPosition(emergency_origin, MONSTER_MINS, MONSTER_MAXS, true)) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Position validation failed\n");
		}
		return false;
	}

	// Create the monster entity
	edict_t* monster = G_Spawn();
	if (!monster) {
		return false;
	}

	// Use provided classname from TypeID
	monster->classname = monster_classname;
	monster->s.origin = emergency_origin;
	monster->s.angles = emergency_angles;
	monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	monster->monsterinfo.last_sentrygun_target_time = 0_ms;

	// Mark it as spawned by horde
	monster->was_spawned_by_horde = true;

	// In spawning state, mark specifically
	if (g_horde_local.state == horde_state_t::spawning) {
		monster->spawned_in_spawn_state = true;
	}

	// Spawn the entity with protection from errors
	monster->solid = SOLID_NOT;  // Start as non-solid to avoid immediate collisions
	ED_CallSpawn(monster);

	// Check if spawn succeeded
	if (!monster->inuse) {
		G_FreeEdict(monster);
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: ED_CallSpawn failed\n");
		}
		return false;
	}

	// Re-set solid state and properly link
	monster->solid = SOLID_BBOX;
	gi.linkentity(monster);

	// Apply modifiers for emergency spawned monsters

	// Higher chance of special abilities in emergency spawns
	if (levelNum >= 10 && frandom() < 0.5f) {
		int flag_type = irandom(0, 5);
		switch (flag_type) {
		case 0: monster->monsterinfo.bonus_flags |= BF_CHAMPION; break;
		case 1: monster->monsterinfo.bonus_flags |= BF_CORRUPTED; break;
		case 2: monster->monsterinfo.bonus_flags |= BF_BERSERKING; break;
		case 3: monster->monsterinfo.bonus_flags |= BF_POSSESSED; break;
		case 4: monster->monsterinfo.bonus_flags |= BF_STYGIAN; break;
		}

		// Apply bonuses
		ApplyMonsterBonusFlags(monster);
	}

	// Always add armor in higher waves
	if (levelNum >= 14 && monster->monsterinfo.power_armor_type == IT_NULL) {
		SetMonsterArmor(monster);
	}

	// Always drop an item for emergency spawns
	monster->item = G_HordePickItem();
	monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP;

	// Visual effects
	SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
	gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

	// Success!
	if (developer->integer) {
		gi.Com_PrintFmt("EMERGENCY SPAWN SUCCESSFUL: Spawned {} at emergency position\n",
			monster->classname);
	}

	return true;
}
// Modified emergency spawn function integrated with the SpawnMonsters system
// Improved EmergencySpawnMonster function using Tank's safety principles
bool EmergencySpawnMonster(const int32_t levelNum, const char* monster_classname) {
	// Find emergency position with our improved function
	vec3_t emergency_origin, emergency_angles;
	bool used_human_player = false;

	if (!FindEmergencySpawnPosition(emergency_origin, emergency_angles, used_human_player, monster_classname)) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Could not find valid position\n");
		}
		return false;
	}

	// Additional validation of final position with improved validation
	constexpr vec3_t MONSTER_MINS = { -16, -16, -24 };
	constexpr vec3_t MONSTER_MAXS = { 16, 16, 32 };

	// Use improved validation logic
	if (!ValidateSpawnPosition(emergency_origin, MONSTER_MINS, MONSTER_MAXS, true)) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Position validation failed\n");
		}
		return false;
	}

	// Create the monster entity
	edict_t* monster = G_Spawn();
	if (!monster) {
		return false;
	}

	// Use provided classname or fallback to soldier
	monster->classname = monster_classname ? monster_classname : "monster_soldier";
	monster->s.origin = emergency_origin;
	monster->s.angles = emergency_angles;
	monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	monster->monsterinfo.last_sentrygun_target_time = 0_ms;

	// Mark it as spawned by horde
	monster->was_spawned_by_horde = true;

	// In spawning state, mark specifically
	if (g_horde_local.state == horde_state_t::spawning) {
		monster->spawned_in_spawn_state = true;
	}

	// Spawn the entity with protection from errors
	monster->solid = SOLID_NOT;  // Start as non-solid to avoid immediate collisions
	ED_CallSpawn(monster);

	// Check if spawn succeeded
	if (!monster->inuse) {
		G_FreeEdict(monster);
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: ED_CallSpawn failed\n");
		}
		return false;
	}

	// Re-set solid state and properly link
	monster->solid = SOLID_BBOX;
	gi.linkentity(monster);

	// Apply modifiers for emergency spawned monsters

	// Higher chance of special abilities in emergency spawns
	if (levelNum >= 10 && frandom() < 0.5f) {
		int flag_type = irandom(0, 5);
		switch (flag_type) {
		case 0: monster->monsterinfo.bonus_flags |= BF_CHAMPION; break;
		case 1: monster->monsterinfo.bonus_flags |= BF_CORRUPTED; break;
		case 2: monster->monsterinfo.bonus_flags |= BF_BERSERKING; break;
		case 3: monster->monsterinfo.bonus_flags |= BF_POSSESSED; break;
		case 4: monster->monsterinfo.bonus_flags |= BF_STYGIAN; break;
		}

		// Apply bonuses
		ApplyMonsterBonusFlags(monster);
	}

	// Always add armor in higher waves
	if (levelNum >= 14 && monster->monsterinfo.power_armor_type == IT_NULL) {
		SetMonsterArmor(monster);
	}

	// Always drop an item for emergency spawns
	monster->item = G_HordePickItem();
	monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP;

	// Visual effects
	SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
	gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

	// Success!
	if (developer->integer) {
		gi.Com_PrintFmt("EMERGENCY SPAWN SUCCESSFUL: Spawned {} at emergency position\n",
			monster->classname);
	}

	return true;
}

// Modified ShouldTriggerAmbushSpawn function for more frequent ambushes
bool ShouldTriggerAmbushSpawn() {
	// Static variables for tracking
	static int waves_since_ambush = 0;

	// Only consider ambush spawning after wave 3 (earlier than before)
	if (current_wave_level < 3) {
		return false;
	}

	// Check if cooldown has expired - REDUCED cooldown
	if (ambush_cooldown_frames > 0) {
		ambush_cooldown_frames--;
		return false;
	}

	// Check if enough time has passed since last ambush - REDUCED from 45 to 25 seconds
	if (level.time - last_ambush_time < 25_sec) {
		return false;
	}

	// Base chance increases with waves since last ambush - INCREASED base chance
	float baseChance = 0.08f + (waves_since_ambush * 0.03f);  // Higher starting value and progression

	// Higher chance in higher waves, capped at 45% (increased from 35%)
	int cappedLevel = (current_wave_level > 25) ? 25 : current_wave_level;
	baseChance += (cappedLevel - 3) * 0.015f;  // Faster scaling with wave level
	baseChance = (baseChance > 0.45f) ? 0.45f : baseChance;

	// Higher chance if players have high health/armor or are on killing sprees
	float playerBonus = 0.0f;
	int playerCount = 0;
	for (auto* player : active_players_no_spect()) {
		if (player && player->inuse && player->health > 0) {
			// Add chance bonus for healthy players or those on sprees
			if (player->health >= 125 || player->client->resp.spree >= 50) {
				playerBonus += 0.04f;  // Increased from 0.03f
			}
			playerCount++;
		}
	}

	// Only apply bonus if there are players, capped at reasonable amount
	if (playerCount > 0) {
		baseChance += std::min(playerBonus, 0.15f);  // Increased cap from 0.12f
	}

	// Roll for chance
	if (frandom() < baseChance) {
		// If successful, set cooldown and timestamp
		last_ambush_time = level.time;
		ambush_cooldown_frames = irandom(100, 220);  // 3-7 seconds cooldown (reduced from 5-10)
		waves_since_ambush = 0;
		return true;
	}

	return false;
}

// Updated SpawnAmbushMonsters to use improved emergency spawning with TypeIDs
int SpawnAmbushMonsters(const MapSize& mapSize, int32_t waveLevel) {
	if (developer->integer) {
		gi.Com_PrintFmt("DEBUG: Starting SpawnAmbushMonsters\n");
	}

	// Determine monster type - try to get a valid monster for current wave
	const char* monster_classname = nullptr;
	horde::MonsterTypeID monster_typeId = horde::MonsterTypeID::UNKNOWN;

	// Try to get a valid monster type from existing spawn points
	for (auto* point : monster_spawn_points()) {
		if (point && point->inuse) {
			// Use TypeID version for more efficient selection
			monster_typeId = G_HordePickMonsterType(point);
			if (monster_typeId != horde::MonsterTypeID::UNKNOWN) {
				monster_classname = horde::MonsterTypeRegistry::GetClassname(monster_typeId);
				break;
			}
		}
	}

	// Fallback to appropriate monster if none found
	if (monster_typeId == horde::MonsterTypeID::UNKNOWN) {
		if (waveLevel >= 15) {
			// Higher-level monsters for later waves
			const horde::MonsterTypeID high_level_monsters[] = {
				horde::MonsterTypeID::GUNNER,
				horde::MonsterTypeID::GLADIATOR,
				horde::MonsterTypeID::TANK,
				horde::MonsterTypeID::SOLDIER_HYPERGUN,
				horde::MonsterTypeID::SOLDIER_LASERGUN
			};
			monster_typeId = high_level_monsters[irandom(0, 4)];
			monster_classname = horde::MonsterTypeRegistry::GetClassname(monster_typeId);
		}
		else {
			// Basic monsters for early waves
			const horde::MonsterTypeID basic_monsters[] = {
				horde::MonsterTypeID::SOLDIER_LIGHT,
				horde::MonsterTypeID::SOLDIER,
				horde::MonsterTypeID::INFANTRY,
				horde::MonsterTypeID::SOLDIER_SS,
				horde::MonsterTypeID::FLYER
			};
			monster_typeId = basic_monsters[irandom(0, 4)];
			monster_classname = horde::MonsterTypeRegistry::GetClassname(monster_typeId);
		}
	}

	// Determine ambush size based on wave and map - INCREASED size
	const int baseCount = mapSize.isSmallMap ? 3 :
		mapSize.isBigMap ? 5 : 4;
	const int ambushSize = baseCount + (waveLevel >= 15 ? 2 : 1);

	// Track successful spawns
	int ambushSuccessCount = 0;

	// Keep track of spawn attempts per position
	constexpr int MAX_FAILURES_PER_POSITION = 3;
	int consecutive_failures = 0;

	// Track timing for spawn attempts
	static gtime_t last_failed_spawn_time = 0_sec;
	constexpr gtime_t SPAWN_RETRY_DELAY = 0.1_sec; // 100ms delay between failed attempts

	// FIXED: Add safety counter to prevent infinite loops
	int safety_counter = 0;
	const int MAX_TOTAL_ITERATIONS = 100;

	// Spawn ambush monsters
	for (int i = 0; i < ambushSize && consecutive_failures < MAX_FAILURES_PER_POSITION; i++) {
		// FIXED: Add safety check to prevent infinite loops
		if (safety_counter++ > MAX_TOTAL_ITERATIONS) {
			gi.Com_PrintFmt("WARNING: Emergency spawn safety limit reached\n");
			break;
		}

		// Check if we need to wait after a failed attempt
		if (consecutive_failures > 0) {
			if (level.time < last_failed_spawn_time + SPAWN_RETRY_DELAY) {
				// FIXED: Don't decrement i if we're going to postpone spawning
				if (safety_counter % 10 == 0) {
					// Only retry at regular intervals
					continue;
				}
				else {
					// Skip this attempt but advance the counter
					continue;
				}
			}
		}

		// Use the TypeID version for more efficient spawning
		if (monster_typeId != horde::MonsterTypeID::UNKNOWN &&
			EmergencySpawnMonster(waveLevel, monster_typeId)) {
			ambushSuccessCount++;
			consecutive_failures = 0;

			// Update spawn counters
			if (g_horde_local.num_to_spawn > 0) {
				--g_horde_local.num_to_spawn;
			}
			else if (g_horde_local.queued_monsters > 0) {
				--g_horde_local.queued_monsters;
			}

			// Update total monsters counter
			if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
				++g_totalMonstersInWave;
			}
		}
		else {
			// Track failures to avoid getting stuck in endless attempts
			consecutive_failures++;
			last_failed_spawn_time = level.time;
		}
	}

	// Only play sound effect without text message for subtlety
	if (ambushSuccessCount > 0) {
		gi.sound(world, CHAN_AUTO, sound_spawn1, 1, ATTN_NONE, 0);
		if (developer->integer) {
			gi.Com_PrintFmt("AMBUSH: Spawned {}/{} monsters near players\n",
				ambushSuccessCount, ambushSize);
		}
	}

	if (developer->integer) {
		gi.Com_PrintFmt("DEBUG: Finished SpawnAmbushMonsters, spawned: {}\n", ambushSuccessCount);
	}

	return ambushSuccessCount;
}

// Cache for potentially valid spawn points (pointers)
static std::vector<edict_t*> g_potential_spawn_points;
static bool g_spawn_points_cached = false;
static size_t g_spawn_point_shuffle_index = 0; // Index for iterating shuffled list

// State for failure tracking and recovery
static int g_consecutive_spawn_failures = 0;
static constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY = 5;
static constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY = 10;
static bool g_recovery_mode_active = false;
static MonsterWaveType g_original_wave_type_before_recovery = MonsterWaveType::None;

edict_t* SpawnMonsters() {
	// --- Initial Checks & Caching ---
	if (developer->integer == 2) return nullptr; // Debug mode check

	const gtime_t current_time = level.time;
	const MapSize& mapSize = g_horde_local.current_map_size; // Cache map size
	const horde_state_t current_state = g_horde_local.state; // Cache current state

	// Cache and validate spawn points once per level load or reset
	if (!g_spawn_points_cached || need_spawn_cache_reset) {
		g_potential_spawn_points.clear();
		g_potential_spawn_points.reserve(MAX_SPAWN_POINTS); // Avoid reallocations
		for (auto* point : monster_spawn_points()) {
			// Basic validation: in use, correct classname, valid origin
			if (point && point->inuse && point->classname &&
				strcmp(point->classname, "info_player_deathmatch") == 0 &&
				is_valid_vector(point->s.origin))
			{
				g_potential_spawn_points.push_back(point);
			}
		}
		std::shuffle(g_potential_spawn_points.begin(), g_potential_spawn_points.end(), mt_rand);
		g_spawn_point_shuffle_index = 0;
		g_spawn_points_cached = true;
		need_spawn_cache_reset = false; // Reset the flag
		g_consecutive_spawn_failures = 0; // Reset failures on cache rebuild
		g_recovery_mode_active = false;
		g_original_wave_type_before_recovery = MonsterWaveType::None;

		if (developer->integer) {
			gi.Com_PrintFmt("Spawn Point Cache Rebuilt: {} potential points found and shuffled.\n",
				g_potential_spawn_points.size());
		}
	}

	// Early exit if no potential spawn points exist at all
	if (g_potential_spawn_points.empty()) {
		if (developer->integer) {
			gi.Com_PrintFmt("SpawnMonsters: No potential spawn points found in level.\n");
		}
		g_consecutive_spawn_failures++; // Still count as failure
		return nullptr;
	}

	// Cache monster counts and caps
	const int32_t activeMonsters = CalculateRemainingMonsters();
	const int32_t softCap = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap :
		(mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
			(mapSize.isBigMap ? MAX_MONSTERS_BIG_MAP : MAX_MONSTERS_MEDIUM_MAP));
	const int32_t hardCap = static_cast<int32_t>(softCap * 1.2f);

	// --- Hard Cap Check ---
	if (activeMonsters >= hardCap) {
		if (developer->integer) {
			gi.Com_PrintFmt("CRITICAL: Hard monster cap exceeded ({}/{}). Halting spawns.\n", activeMonsters, hardCap);
		}
		if (current_state == horde_state_t::spawning) {
			g_horde_local.state = horde_state_t::active_wave; // Force state transition
		}
		g_horde_local.num_to_spawn = 0; // Prevent further attempts this frame
		g_consecutive_spawn_failures++;
		return nullptr;
	}

	// --- AMBUSH SYSTEM CHECK ---
	// Only check during active wave and if there's room below soft cap
	if (current_state == horde_state_t::active_wave && activeMonsters < softCap && ShouldTriggerAmbushSpawn()) {
		int spawnedCount = SpawnAmbushMonsters(mapSize, current_wave_level);
		if (spawnedCount > 0) {
			// Successfully spawned ambush, reset failure counter and return (find last spawned)
			g_consecutive_spawn_failures = 0;
			g_recovery_mode_active = false; // Reset recovery mode on any success
			// Find and return the last spawned monster (simple approach)
			edict_t* last_spawned = nullptr;
			for (int i = globals.num_edicts - 1; i >= 0; --i) {
				edict_t* ent = &g_edicts[i];
				if (ent->inuse && (ent->svflags & SVF_MONSTER) && ent->was_spawned_by_horde && ent->health > 0) {
					last_spawned = ent;
					break;
				}
			}
			return last_spawned; // Return last spawned or nullptr
		}
		// If ambush failed, continue to normal spawning
	}


	// --- Check Spawn Conditions & Calculate Batch Size ---
	int32_t availableSpace = softCap - activeMonsters;
	if (availableSpace <= 0) {
		if (developer->integer > 1) {
			gi.Com_PrintFmt("SpawnMonsters: At or above soft monster cap ({}/{})\n", activeMonsters, softCap);
		}
		// Don't increment failure count if we're just at cap
		return nullptr;
	}

	// Queue transfer logic (if primary pool is empty)
	if (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters > 0) {
		const int32_t base_transfer = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
		const int32_t transfer_amount = std::min({ g_horde_local.queued_monsters, availableSpace, base_transfer });
		if (transfer_amount > 0) {
			g_horde_local.num_to_spawn += transfer_amount;
			g_horde_local.queued_monsters -= transfer_amount;
			if (developer->integer > 1) {
				gi.Com_PrintFmt("SpawnMonsters: Transferred {} monsters from queue.\n", transfer_amount);
			}
		}
	}

	// If still nothing to spawn, exit
	if (g_horde_local.num_to_spawn <= 0) {
		if (developer->integer > 1) {
			gi.Com_PrintFmt("SpawnMonsters: No monsters available in spawn pool or queue.\n");
		}
		g_consecutive_spawn_failures++;
		return nullptr;
	}

	// Determine batch size
	const int32_t base_batch = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
	const int32_t spawnable_this_call = std::min({ g_horde_local.num_to_spawn, base_batch, availableSpace });

	// --- Failure Recovery Logic ---
	MonsterWaveType wave_type_override = MonsterWaveType::None; // Use this if recovery is active
	if (g_consecutive_spawn_failures >= MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY && !g_recovery_mode_active) {
		g_recovery_mode_active = true;
		g_original_wave_type_before_recovery = current_wave_type; // Save original

		// Simplify wave type for recovery
		if (HasWaveType(current_wave_type, MonsterWaveType::Flying)) {
			wave_type_override = MonsterWaveType::Flying;
		}
		else {
			wave_type_override = MonsterWaveType::Ground; // Default to ground
		}

		// Reset spawn point cooldowns to give them another chance
		ResetAllSpawnAttempts();

		if (developer->integer) {
			gi.Com_PrintFmt("RECOVERY MODE ACTIVATED: Simplified wave type, reset spawn points.\n");
		}
	}
	else if (g_consecutive_spawn_failures >= MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY) {
		// Severe failure: Try emergency spawning near players
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN TRIGGERED after {} consecutive failures.\n", g_consecutive_spawn_failures);
		}
		int emergency_spawned = 0;
		for (int i = 0; i < std::min(spawnable_this_call, 3); ++i) { // Try spawning a few
			// Pick a fallback monster type appropriate for the level
			horde::MonsterTypeID emergency_type = (current_wave_level < 10) ? horde::MonsterTypeID::SOLDIER : horde::MonsterTypeID::GUNNER;
			if (EmergencySpawnMonster(current_wave_level, emergency_type)) {
				emergency_spawned++;
				--g_horde_local.num_to_spawn;
				if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
					++g_totalMonstersInWave;
				}
			}
			else {
				break; // Stop trying if emergency spawn fails
			}
		}
		if (emergency_spawned > 0) {
			g_consecutive_spawn_failures = 0; // Reset failures on success
			g_recovery_mode_active = false;   // Exit recovery mode
			if (g_original_wave_type_before_recovery != MonsterWaveType::None) {
				current_wave_type = g_original_wave_type_before_recovery; // Restore original
				g_original_wave_type_before_recovery = MonsterWaveType::None;
			}
			SetNextMonsterSpawnTime(mapSize); // Set timer for next regular spawn
			// Find and return the last spawned monster (simple approach)
			edict_t* last_spawned = nullptr;
			for (int i = globals.num_edicts - 1; i >= 0; --i) {
				edict_t* ent = &g_edicts[i];
				if (ent->inuse && (ent->svflags & SVF_MONSTER) && ent->was_spawned_by_horde && ent->health > 0) {
					last_spawned = ent;
					break;
				}
			}
			return last_spawned;
		}
		// If emergency spawn also failed, we let the normal loop try one last time, potentially failing again.
	}

	// Determine the wave type to use (original or recovery override)
	const MonsterWaveType effective_wave_type = g_recovery_mode_active ? wave_type_override : current_wave_type;

	// --- Main Spawn Loop ---
	edict_t* last_spawned_this_call = nullptr;
	int spawned_count_this_call = 0;
	int points_checked = 0;
	const size_t total_potential_points = g_potential_spawn_points.size();

	for (int i = 0; i < spawnable_this_call; ++i) {
		bool spawn_successful = false;
		points_checked = 0; // Reset checked count for each monster we try to spawn

		// Iterate through the shuffled list of potential points
		while (points_checked < total_potential_points) {
			// Cycle through the shuffled list
			if (g_spawn_point_shuffle_index >= total_potential_points) {
				g_spawn_point_shuffle_index = 0; // Wrap around
				// Optional: Re-shuffle here if desired for more randomness per batch,
				// but less performant. Sticking to per-wave shuffle is usually enough.
				// std::shuffle(g_potential_spawn_points.begin(), g_potential_spawn_points.end(), mt_rand);
			}
			edict_t* spawn_point = g_potential_spawn_points[g_spawn_point_shuffle_index++];
			points_checked++;

			// --- Dynamic Spawn Point Checks ---
			const auto& sp_data = spawnPointsData[spawn_point]; // Direct access

			// 1. Cooldown Check
			if (sp_data.isTemporarilyDisabled && current_time < sp_data.cooldownEndsAt) continue;
			if (current_time < sp_data.alternative_cooldown) continue; // Check alternative cooldown

			// 2. Flying Mismatch Check
			const bool is_flying_spawn = (spawn_point->style == 1);
			if (is_flying_spawn && !HasWaveType(effective_wave_type, MonsterWaveType::Flying) && effective_wave_type != MonsterWaveType::None) continue;

			// 3. Player Proximity Check (Optimize by checking squared distance)
			bool too_close_to_player = false;
			for (const auto* const player : active_players_no_spect()) {
				if ((spawn_point->s.origin - player->s.origin).lengthSquared() < 22500.0f) { // 150*150
					too_close_to_player = true;
					break;
				}
			}
			if (too_close_to_player) continue;

			// 4. Occupancy Check (Player blocking)
			if (IsSpawnPointOccupied(spawn_point)) {
				// Maybe apply a short cooldown here if frequently blocked by players?
				// spawnPointsData[spawn_point].cooldownEndsAt = current_time + 0.5_sec;
				continue;
			}

			// --- Point Passed Dynamic Checks - Attempt Spawn ---

			// Select Monster Type
			horde::MonsterTypeID monster_type = G_HordePickMonsterType(spawn_point);
			if (monster_type == horde::MonsterTypeID::UNKNOWN) continue; // Failed to pick type

			// Check Flying Compatibility (Monster vs Point) - Redundant if G_HordePickMonsterType handles it, but safe.
			if (is_flying_spawn && !IsFlying(monster_type)) continue;
			if (!is_flying_spawn && IsFlying(monster_type)) continue; // Don't spawn flyers at ground points

			// Check for Obstruction & Alternative Positions
			vec3_t final_origin = spawn_point->s.origin;
			vec3_t final_angles = spawn_point->s.angles;
			bool used_alternative = false;
			SpawnPointCache& cache = spawn_point_cache[spawn_point]; // Direct access

			if (cache.has_obstacle) {
				if (TryAlternativeSpawnPosition(spawn_point, monster_type, final_origin, final_angles)) {
					used_alternative = true;
					ApplySuccessfulAlternativeCooldown(spawn_point); // Short cooldown on success
				}
				else {
					ApplyAlternativePositionCooldown(spawn_point); // Longer cooldown on failure
					continue; // Failed to find alternative, try next spawn point
				}
			}

			// --- Spawn the Monster ---
			edict_t* monster = SpawnMonsterByTypeID(monster_type, final_origin, final_angles);

			if (monster) {
				spawn_successful = true;
				last_spawned_this_call = monster;
				spawned_count_this_call++;
				--g_horde_local.num_to_spawn;
				if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
					++g_totalMonstersInWave;
				}

				// Apply Bonuses/Armor/Item Drop
				// Champion Check (simplified example)
				if (g_horde_local.level >= 3 && !champion_spawned_this_wave && champion_spawn_cooldown <= 0 && !monster->monsterinfo.IS_BOSS && frandom() < 0.2f) {
					monster->monsterinfo.bonus_flags |= BF_CHAMPION; // Example flag
					ApplyMonsterBonusFlags(monster);
					monster->item = G_HordePickItem(); // Champions always drop
					monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP;
					champion_spawned_this_wave = true;
					champion_spawn_cooldown = irandom(15, 25);
					gi.LocBroadcast_Print(PRINT_HIGH, "*** A Champion {} has appeared! ***\n", GetDisplayName(monster).c_str());
				}
				else {
					// Regular item drop chance
					const float drop_chance = g_horde_local.level <= 5 ? 0.8f : (g_horde_local.level <= 8 ? 0.6f : 0.45f);
					if (frandom() < drop_chance) {
						monster->item = G_HordePickItem();
					}
				}

				// Armor
				if (g_horde_local.level >= 14 && monster->monsterinfo.power_armor_type == IT_NULL) {
					SetMonsterArmor(monster);
				}

				// Effects
				SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
				gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

				// Apply cooldown to the *original* spawn point if no alternative was used
				if (!used_alternative) {
					OnSuccessfulSpawn(spawn_point); // Applies short cooldown
				}
				// If alternative was used, ApplySuccessfulAlternativeCooldown already applied a cooldown

				break; // --- Break inner loop (spawn point search) --- move to next monster in batch
			}
			else {
				// SpawnMonsterByTypeID failed (rare)
				if (developer->integer) {
					gi.Com_PrintFmt("SpawnMonsters: SpawnMonsterByTypeID failed for {}\n",
						horde::MonsterTypeRegistry::GetClassname(monster_type));
				}
				// Don't immediately continue, let the outer loop try again unless spawnable limit reached
			}
		} // End of spawn point check loop

		// If we checked all points and couldn't spawn this monster
		if (!spawn_successful) {
			if (developer->integer > 1) {
				gi.Com_PrintFmt("SpawnMonsters: Failed to find suitable spawn point for monster attempt {}.\n", i + 1);
			}
			// Increment failure count here, as the entire attempt for this monster failed
			g_consecutive_spawn_failures++;
			// Don't break the outer loop, allow trying for the remaining monsters in the batch
		}
		else {
			// Reset failure count on any successful spawn within the batch
			g_consecutive_spawn_failures = 0;
			if (g_recovery_mode_active) {
				g_recovery_mode_active = false; // Exit recovery mode
				if (g_original_wave_type_before_recovery != MonsterWaveType::None) {
					current_wave_type = g_original_wave_type_before_recovery; // Restore original type
					g_original_wave_type_before_recovery = MonsterWaveType::None;
					if (developer->integer) {
						gi.Com_PrintFmt("RECOVERY MODE DEACTIVATED: Restored original wave type.\n");
					}
				}
			}
		}
	} // --- End of Main Spawn Loop (Batch) ---

	// --- Post-Spawn ---
	if (spawned_count_this_call == 0 && g_horde_local.num_to_spawn > 0) {
		// Batch failed completely, increment failure count (already done inside loop if individual attempts failed)
		if (developer->integer) {
			gi.Com_PrintFmt("SpawnMonsters: Batch failed, {} consecutive failures.\n", g_consecutive_spawn_failures);
		}
	}

	SetNextMonsterSpawnTime(mapSize); // Set timer for the next spawn attempt

	return last_spawned_this_call; // Return last successfully spawned monster in this batch
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

// Pre-computed total reward weight (calculated once)
static const int TOTAL_REWARD_WEIGHT = [] {
	int total = 0;
	for (const auto& [type, info] : REWARD_TABLE) {
		total += info.weight;
	}
	return total;
	}();

// Direct-access array of reward items for faster lookup
static const std::array<std::pair<item_id_t, int>, 2> REWARD_ITEMS = { {
	{IT_ITEM_BANDOLIER, 60},
	{IT_ITEM_SENTRYGUN, 40}
} };

static bool GiveTopDamagerReward(const PlayerStats& topDamager, const std::string& playerName) {
	// Quick validation with early return
	if (!topDamager.player || !topDamager.player->inuse || !topDamager.player->client)
		return false;

	// Select reward using pre-computed weights
	const int roll = irandom(1, TOTAL_REWARD_WEIGHT);

	// Determine selected item
	item_id_t selectedItemId = IT_ITEM_BANDOLIER; // Default fallback
	int currentWeight = 0;

	for (const auto& [itemId, weight] : REWARD_ITEMS) {
		currentWeight += weight;
		if (roll <= currentWeight) {
			selectedItemId = itemId;
			break;
		}
	}

	// Get item by ID
	gitem_t* item = GetItemByIndex(selectedItemId);
	if (!item || !item->classname)
		return false;

	// Spawn and give item directly to player
	edict_t* entity = G_Spawn();
	if (!entity)
		return false;

	entity->classname = item->classname;
	entity->item = item;
	SpawnItem(entity, item, spawn_temp_t::empty);

	if (!entity->inuse)
		return false;

	// Give item to player
	Touch_Item(entity, topDamager.player, null_trace, true);
	if (entity->inuse)
		G_FreeEdict(entity);

	// Announce reward (safely handle potential null strings)
	const char* itemName = item->use_name ? item->use_name :
		(item->classname ? item->classname : "reward");

	gi.LocBroadcast_Print(PRINT_HIGH, "{} receives a {} for top damage!\n",
		playerName.empty() ? "Unknown Player" : playerName.c_str(),
		itemName);

	return true;
}
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
			// Reset all player stats in one pass using iterator
			for (auto* player : active_players()) {
				if (player && player->client) {
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

		// Fast path for all monsters dead
		if (Horde_AllMonstersDead()) {
			currentWaveEndReason = WaveEndReason::AllMonstersDead;
			waveEnded = true;
			break;
		}

		// Check wave completion conditions
		if (CheckRemainingMonstersCondition(mapSize, currentWaveEndReason)) {
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

