// Includes y definiciones relevantes
#include "../shared.h"
#include "../g_local.h"
#include "g_horde.h"
#include <set>
#include "g_horde_benefits.h"
#include "horde_ids.h"
#include <span>
#include "../laser.h" 
#include "../profiler.h"

MonsterWaveType current_wave_type = MonsterWaveType::None;

// Cache for potentially valid spawn points (pointers)
static std::vector<edict_t*> g_potential_spawn_points;
static bool g_spawn_points_cached = false;
static size_t g_spawn_point_shuffle_index = 0; // Index for iterating shuffled list

// State for failure tracking and recovery
static int g_consecutive_spawn_failures = 0;
static bool g_recovery_mode_active = false;
static MonsterWaveType g_original_wave_type_before_recovery = MonsterWaveType::None;

static bool need_spawn_cache_reset = false;
static bool need_frame_timer_reset = false;
static bool need_queue_monitor_reset = false;

//retaliation horde ( for when players are killing way too ez new/current wave in spawning state)

static bool g_horde_retaliation_active = false;
static gtime_t g_horde_retaliation_end_time = 0_sec;
// Optional: Store the targeted player edict for focus (can be nullptr)
static edict_t* g_horde_retaliation_target_player = nullptr;

// Ambush system tracking variables
static gtime_t last_ambush_time = 0_sec;
static gtime_t ambush_cooldown_end = 0_sec;
static int32_t waves_since_ambush = 0;
static bool ambush_system_initialized = false;

// --- Recent Spawn Position Tracking ---
struct RecentSpawnPosition {
	vec3_t position = {};
	gtime_t cooldown_until = 0_sec;
};
static constexpr size_t MAX_RECENT_POSITIONS = 32; // History for TryAlternativeSpawnPosition
static std::array<RecentSpawnPosition, MAX_RECENT_POSITIONS> g_recent_spawn_positions;
static size_t g_recent_position_index = 0;

// --- Recent Teleport Position Tracking ---
struct RecentTeleportPosition {
	vec3_t position = {};
	gtime_t teleport_time = 0_sec;
};
static constexpr int MAX_RECENT_TELEPORT_LOCATIONS = 8; // History for CheckAndTeleportStuckMonster
static std::array<RecentTeleportPosition, MAX_RECENT_TELEPORT_LOCATIONS> g_recent_teleport_positions;
static int g_recent_teleport_index = 0;

// --- Horde Mode Constants ---
namespace HordeConstants {

	// --- Gameplay Balance ---
	constexpr float TIME_REDUCTION_MULTIPLIER = 0.95f;     // General multiplier for reducing certain timers
	constexpr float BASE_DIFFICULTY_MULTIPLIER = 1.1f;     // Base multiplier for player count scaling
	constexpr float PLAYER_COUNT_SCALE = 0.2f;         // How much each additional player scales difficulty/counts

	// --- Monster Counts & Caps ---
	constexpr int8_t MAX_MONSTERS_BIG_MAP = 32;           // Default max monsters for large maps
	constexpr int8_t MAX_MONSTERS_MEDIUM_MAP = 16;        // Default max monsters for medium maps
	constexpr int8_t MAX_MONSTERS_SMALL_MAP = 14;         // Default max monsters for small maps
	// Base counts for different map sizes and early wave levels (Level Ranges: <=5, <=10, <=15, >15)
	constexpr std::array<std::array<int32_t, 4>, 3> BASE_COUNTS = { {
		{{6, 8, 10, 12}},  // Small maps
		{{8, 12, 14, 16}}, // Medium maps
		{{15, 18, 23, 26}} // Large maps
	} };
	// Base additional spawns per wave (Small, Medium, Large) applied from wave 8+
	constexpr std::array<int32_t, 3> ADDITIONAL_SPAWNS = { 8, 7, 12 };

	// --- Spawn Point Cooldowns ---
	// Note: SPAWN_POINT_COOLDOWN (the global variable) is calculated dynamically
	constexpr gtime_t MIN_GLOBAL_SPAWN_COOLDOWN = 1.5_sec;       // Absolute minimum for the dynamic SPAWN_POINT_COOLDOWN
	constexpr gtime_t MIN_INDIVIDUAL_SUCCESS_COOLDOWN = 0.5_sec; // Min time a point is disabled after *successful* direct spawn
	constexpr gtime_t MIN_INDIVIDUAL_FAILURE_COOLDOWN = 0.5_sec; // Min time a point is disabled after *failed* direct spawn attempts
	constexpr gtime_t MIN_REDUCED_INDIVIDUAL_COOLDOWN = 0.5_sec; // Min cooldown when reduced late-wave (CheckAndReduceSpawnCooldowns)

	// --- Alternative Spawn Position Cooldowns & Logic ---
	constexpr gtime_t ALT_SPAWN_COOLDOWN_SHORT = 1.5_sec;         // Cooldown after 1-2 failed alternative position attempts
	constexpr gtime_t ALT_SPAWN_COOLDOWN_MEDIUM = 3.0_sec;        // Cooldown after 3-5 failed alternative position attempts
	// Min cooldowns applied after alternative spawn success/failure
	constexpr gtime_t MIN_ALT_SUCCESS_COOLDOWN = 1.0_sec;
	constexpr gtime_t MIN_ALT_FAILURE_COOLDOWN = 1.0_sec;
	// Predefined offsets tried by TryAlternativeSpawnPosition
	constexpr size_t NUM_HORDE_ALT_POSITIONS = 8;
	constexpr std::array<vec3_t, NUM_HORDE_ALT_POSITIONS> horde_alternative_positions = {
		vec3_t{ 40, 0, 8 },  vec3_t{ -40, 0, 8 },
		vec3_t{ 0, 40, 8 },  vec3_t{ 0, -40, 8 },
		vec3_t{ 30, 30, 0 }, vec3_t{ -30, 30, 0 },
		vec3_t{ 30, -30, 0 }, vec3_t{ -30, -30, 0 }
	};

	// --- Monster Spawning Timing ---
	constexpr gtime_t MIN_MONSTER_SPAWN_INTERVAL = 0.8_sec;      // Absolute minimum time between any two monster spawns (SetNextMonsterSpawnTime)

	// --- Stuck Monster / Teleport Logic ---
	constexpr gtime_t STUCK_CHECK_TIME = 18_sec;                   // How long monster must be stuck before teleport check
	constexpr gtime_t NO_DAMAGE_TIMEOUT = 25_sec;
	// CONSOLIDATED: Cooldown applied *to the monster* after any teleport (stuck or boss)
	constexpr gtime_t MIN_TELEPORT_COOLDOWN_MONSTER = 12_sec;
	constexpr gtime_t MAX_TELEPORT_COOLDOWN_MONSTER = 20_sec;
	// Global rate limiting for stuck monster teleports
	constexpr gtime_t GLOBAL_TELEPORT_RESET_INTERVAL = 12_sec;    // Interval to reset the rate limit counter
	constexpr int MAX_TELEPORTS_PER_INTERVAL = 2;                // Max stuck teleports allowed within the interval (adjusted for difficulty)
	// Rate limit counter and timer (defined as static within the namespace)
	int g_teleport_rate_count = 0;                        // Current count within the interval
	gtime_t g_teleport_rate_reset_time = level.time;      // Time the counter was last reset
	//int recent_teleport_count = 0;          // Tracks teleports within the current interval

	// --- Proximity / Distance Checks ---
	constexpr vec3_t VALIDATE_CHECK_MINS = { -16, -16, -24 };     // Default mins for IsValidSpawnLocation fallback
	constexpr vec3_t VALIDATE_CHECK_MAXS = { 16,  16,  32 };      // Default maxs for IsValidSpawnLocation fallback
	// Emergency Spawn Position Generation
	constexpr float MIN_PLAYER_DIST_GENERATE = 200.0f;           // Min distance from player when *generating* emergency spots
	// Minimum Distances (Final Checks) - Use squared values for performance
	constexpr float MIN_PLAYER_DIST_CHECK = 360.0f;              // Min final distance from player for *emergency* spawns
	constexpr float MIN_PLAYER_DIST_SQ_CHECK = MIN_PLAYER_DIST_CHECK * MIN_PLAYER_DIST_CHECK;
	constexpr float MIN_PLAYER_DIST_SPAWNPOINT = 150.0f;         // Min distance from player for *regular* spawn point filtering
	constexpr float MIN_PLAYER_DIST_SQ_SPAWNPOINT = MIN_PLAYER_DIST_SPAWNPOINT * MIN_PLAYER_DIST_SPAWNPOINT;
	// Recent Spawn/Teleport Proximity
	constexpr float MIN_RECENT_SPAWN_DIST = 120.0f;               // Min distance between sequential regular/alternative spawns
	constexpr float MIN_RECENT_SPAWN_DIST_SQ = MIN_RECENT_SPAWN_DIST * MIN_RECENT_SPAWN_DIST;
	constexpr float MIN_RECENT_TELEPORT_DIST = 300.0f;           // Min distance between sequential *teleport* destinations
	constexpr float MIN_RECENT_TELEPORT_DIST_SQ = MIN_RECENT_TELEPORT_DIST * MIN_RECENT_TELEPORT_DIST;
	constexpr gtime_t RECENT_SPAWN_COOLDOWN = 5.0_sec;           // How long a regular spawn position affects proximity checks
	constexpr gtime_t RECENT_TELEPORT_COOLDOWN = 20.0_sec;        // How long a teleport destination affects proximity checks

	// --- Failure Recovery ---
	constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY = 5;  // Threshold to enter recovery mode (simplify wave type)
	constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY = 10; // Threshold to trigger emergency spawning near players

	// --- Wave Timing ---
	// For calculate_max_wave_time
	constexpr gtime_t BASE_MAX_WAVE_TIME = 115_sec;              // Increased base time limit for waves
	constexpr gtime_t TIME_INCREASE_PER_LEVEL = 2.5_sec;         // How much max time increases per wave
	constexpr gtime_t BOSS_TIME_BONUS = 80_sec;                  // Extra time added for boss waves
	// For CheckRemainingMonstersCondition (aggressive timer)
	constexpr int MONSTERS_FOR_AGGRESSIVE_REDUCTION = 6;       // Trigger aggressive timer when <= this many monsters remain


} // namespace HordeConstants
// --- Forward Declarations ---
bool EmergencySpawnMonster(const int32_t levelNum, horde::MonsterTypeID typeId);
void CalculateTopDamager(PlayerStats& topDamager, float& percentage);
[[nodiscard]] bool IsValidSpawnLocation(vec3_t& io_position, const vec3_t& monster_mins, const vec3_t& monster_maxs, bool is_flying);
bool CheckAndTeleportStuckMonster(edict_t* self);
bool FindEmergencySpawnPosition(vec3_t& position, vec3_t& angles, bool& used_human_player, horde::MonsterTypeID typeId);
bool TryAlternativeSpawnPosition(edict_t* spawn_point, horde::MonsterTypeID typeId, vec3_t& final_origin, vec3_t& final_angles);
edict_t* SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t& origin, const vec3_t& angles, bool applyHordeFlags);

// --- Helper Functions ---
void MarkPositionAsRecentlyUsed(const vec3_t& position) {
	g_recent_spawn_positions[g_recent_position_index] = {
		position,
		level.time + HordeConstants::RECENT_SPAWN_COOLDOWN
	};
	g_recent_position_index = (g_recent_position_index + 1) % MAX_RECENT_POSITIONS;
}

bool IsPositionTooCloseToRecentSpawn(const vec3_t& position) {
	const gtime_t current_time = level.time;
	for (const auto& recent : g_recent_spawn_positions) {
		if (recent.cooldown_until > current_time) {
			if ((position - recent.position).lengthSquared() < HordeConstants::MIN_RECENT_SPAWN_DIST_SQ) {
				return true;
			}
		}
	}
	return false;
}

void MarkPositionAsRecentlyTeleported(const vec3_t& position) {
	g_recent_teleport_positions[g_recent_teleport_index] = {
		position,
		level.time + HordeConstants::RECENT_TELEPORT_COOLDOWN // Mark when it becomes not recent
	};
	g_recent_teleport_index = (g_recent_teleport_index + 1) % MAX_RECENT_TELEPORT_LOCATIONS;
}

bool IsPositionTooCloseToRecentTeleport(const vec3_t& position) {
	const gtime_t current_time = level.time;
	for (const auto& recent : g_recent_teleport_positions) {
		// Check if the cooldown has expired (teleport_time stores the time *until* it's no longer recent)
		if (recent.teleport_time > current_time) {
			if ((position - recent.position).lengthSquared() < HordeConstants::MIN_RECENT_TELEPORT_DIST_SQ) {
				return true;
			}
		}
	}
	return false;
}

// (Keep ResetSpawnMonsterVars, ResetFrameTimers, ResetQueueMonitorVars as they were)
void ResetSpawnMonsterVars() {
	need_spawn_cache_reset = true;
	horde::g_monsterSpawnTracker.Reset();
}

void ResetQueueMonitorVars() {
	need_queue_monitor_reset = true;
	horde::g_spawnPointTimeTracker.Reset();
}

// --- Global/Static Variables ---
bool champion_spawned_this_wave = false;
int champion_spawn_cooldown = 0;
int consistent_zero_counts = 0;
int counter_mismatch_frames = 0;
constexpr size_t MAX_SPAWN_POINTS = 32;

// (Keep SpawnPointData and SpawnPointDataArray structs as they were)
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
		// Clamp time_factor to avoid excessive influence
		const float clamped_time_factor = std::min(time_factor, 1.0f);
		// Calculate base success rate, ensure attempts is not zero
		const float base_rate = (attempts > 0) ? (float(successfulSpawns) / float(attempts)) : 1.0f;

		// Combine base rate and time factor, ensuring result is between 0 and 1
		return std::clamp(base_rate + clamped_time_factor, 0.0f, 1.0f);
	}
};
struct SpawnPointDataArray {
	SpawnPointData data[MAX_EDICTS];
	SpawnPointData& operator[](const edict_t* ent) { return data[ent - g_edicts]; }
	const SpawnPointData& operator[](const edict_t* ent) const { return data[ent - g_edicts]; }
	void clear() { for (auto& item : data) item = SpawnPointData{}; }
	bool find_and_access(const edict_t* key, SpawnPointData*& data_ptr) {
		data_ptr = &data[key - g_edicts]; return true;
	}
};
SpawnPointDataArray spawnPointsData;

void ApplyAlternativePositionCooldown(edict_t* spawn_point) {
	if (!spawn_point || !spawn_point->inuse) return;
	auto& data = spawnPointsData[spawn_point];
	data.alternative_attempts++;
	gtime_t cooldown_duration;
	if (data.alternative_attempts <= 2) cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_SHORT;
	else if (data.alternative_attempts <= 5) cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_MEDIUM;
	else {
		cooldown_duration = 5.0_sec + gtime_t::from_sec(0.5f * (data.alternative_attempts - 5));
		cooldown_duration = std::min(cooldown_duration, 10.0_sec);
		if (data.alternative_attempts >= 8) data.needs_long_alternative_cooldown = true;
	}

	// Clamp the calculated alternative failure cooldown duration
	const gtime_t final_alt_duration = std::max(cooldown_duration, HordeConstants::MIN_ALT_FAILURE_COOLDOWN);
	data.alternative_cooldown = level.time + final_alt_duration;

	data.isTemporarilyDisabled = true; // Also disable original point shortly
	// Also clamp the normal point's shorter cooldown based on the *clamped* alternative duration
	const gtime_t final_normal_duration = std::max(final_alt_duration * 0.5f, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
	data.cooldownEndsAt = level.time + final_normal_duration;

	if (developer->integer) gi.Com_PrintFmt("Alternative position cooldown applied to spawn at {}: {:.1f}s (attempts: {})\n", spawn_point->s.origin, final_alt_duration.seconds(), data.alternative_attempts);
}

void ApplySuccessfulAlternativeCooldown(edict_t* spawn_point) {
	if (!spawn_point || !spawn_point->inuse) return;
	auto& data = spawnPointsData[spawn_point];
	data.alternative_attempts = 0;
	data.needs_long_alternative_cooldown = false;
	// Ensure the 3.0s meets the minimum alternative success cooldown
	data.alternative_cooldown = level.time + std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN);
	if (developer->integer > 1) gi.Com_PrintFmt("Success cooldown applied to spawn at {}: {:.1f}s\n", spawn_point->s.origin, std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN).seconds());
}

// --- MODIFIED IncreaseSpawnAttempts ---
void IncreaseSpawnAttempts(edict_t* spawn_point) {
	if (!spawn_point || !spawn_point->inuse) return;
	auto& data = spawnPointsData[spawn_point];

    // --- FIX: Less aggressive reset for inactive points ---
if (level.time - data.lastSpawnTime > 6_sec) {
    data.attempts = 0;
    data.isTemporarilyDisabled = false;
    data.cooldownEndsAt = 0_sec;
    data.lastSpawnTime = level.time; // Mark this reset attempt time
    return;
}
    // --- END FIX ---

	data.attempts++;
	const float success_rate = data.getSuccessRate(level.time);
	const int max_attempts = 4 + (success_rate >= 0.5f ? 2 : (success_rate >= 0.25f ? 1 : 0));

	gtime_t calculated_duration = 0_sec;

	if (data.attempts >= max_attempts) {
		data.isTemporarilyDisabled = true;
		const float cooldown_factor = success_rate < 0.3f ? 1.5f : 0.75f;
		const float attempt_multiplier = data.attempts <= 8 ? data.attempts * 0.25f : 2.0f;
		calculated_duration = gtime_t::from_sec(cooldown_factor * attempt_multiplier);
		if (developer->integer == 1) gi.Com_PrintFmt("SpawnPoint at {} inactivated for adaptive cooldown.\n", spawn_point->s.origin);
	}
	else if ((data.attempts & 1) == 0) { // Every 2 attempts
		calculated_duration = gtime_t::from_sec(0.2f * data.attempts);
	}

	if (calculated_duration > 0_sec) {
		const gtime_t final_duration = std::max(calculated_duration, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
		data.cooldownEndsAt = level.time + final_duration;
	}
    // Update lastSpawnTime on every attempt, successful or not, to track activity
    data.lastSpawnTime = level.time;
}
void OnSuccessfulSpawn(edict_t* spawn_point) {
	if (!spawn_point || !spawn_point->inuse) return;
	auto& data = spawnPointsData[spawn_point];
	data.successfulSpawns++;
	data.attempts = 0;
	data.isTemporarilyDisabled = false;
	// Use the minimum success cooldown constant
	data.cooldownEndsAt = level.time + HordeConstants::MIN_INDIVIDUAL_SUCCESS_COOLDOWN;
	horde::g_spawnPointTimeTracker.SetLastSpawnTime(spawn_point, level.time);
}
struct SpawnPointCache {
	gtime_t last_check_time = 0_sec;
	vec3_t last_check_origin = {};
	gtime_t frame_time = 0_sec;         // Frame time of the last check
	bool was_occupied_by_player = false; // True if a player was found in the last check
	bool has_obstacle = false;          // True if a monster/defense was found (excluding player)
};

struct SpawnPointCacheArray {
	SpawnPointCache data[MAX_EDICTS];
	SpawnPointCache& operator[](const edict_t* ent) { return data[ent - g_edicts]; }
	const SpawnPointCache& operator[](const edict_t* ent) const { return data[ent - g_edicts]; }
	void clear() { for (auto& item : data) item = SpawnPointCache{}; }
};
static SpawnPointCacheArray spawn_point_cache;

// Filter specifically for finding teleport destinations for stuck monsters
struct StuckMonsterSpawnFilter {
	bool operator()(edict_t* ent) const {
		// Basic validation: Must be an info_player_deathmatch spawn point
		if (!ent || !ent->inuse || !ent->classname ||
			strcmp(ent->classname, "info_player_deathmatch") != 0)
			return false;

        // Exclude flying-only spawn points (style == 1) for stuck ground monsters
        // (Assuming most stuck monsters are ground-based, adjust if needed)
        if (ent->style == 1)
            return false;

		// Check if the spawn point itself is on teleport cooldown
		if (level.time < spawnPointsData[ent].teleport_cooldown)
			return false;

        // Check if the spawn point is occupied by a player (using the cached check)
		if (IsSpawnPointOccupied(ent))
			return false;

        // Check proximity to players - prefer points somewhat near players
		for (const auto* const player : active_players_no_spect()) {
			// Check distance squared for efficiency
			if ((ent->s.origin - player->s.origin).lengthSquared() < (512.0f * 512.0f)) {
				return true;  // Accept spawn points within 512 units of any active player
			}
		}

		// If no player is nearby, reject this point for stuck monster teleport
		return false;
	}
};

bool IsSpawnPointOccupied(const edict_t* spawn_point, const edict_t* ignore_ent) {
	// --- Basic Validation ---
	if (!spawn_point || !spawn_point->inuse || !is_valid_vector(spawn_point->s.origin)) {
		if (developer->integer) {
			if (!spawn_point) gi.Com_PrintFmt("Warning: Null spawn_point passed to IsSpawnPointOccupied\n");
			else gi.Com_PrintFmt("Warning: Invalid origin vector in spawn point {}\n", spawn_point->s.origin);
		}
		return true; // Safer to assume occupied if invalid
	}

	// --- Cache Check ---
	SpawnPointCache& cache = spawn_point_cache[spawn_point];
	// Increased cache duration slightly for better performance vs accuracy balance
	static constexpr auto CACHE_DURATION = 100_ms; // 0.1 seconds

	// Check time-based cache validity AND frame validity
	if (level.time - cache.last_check_time < CACHE_DURATION &&
		cache.last_check_origin == spawn_point->s.origin &&
		(level.time - cache.frame_time) < FRAME_TIME_MS * 2) // Allow cache reuse within 2 frames
	{
		// Return the cached player occupation status.
		// The caller (SelectRandomSpawnPoint) will use cache.has_obstacle separately
		// if this function returns false.
		return cache.was_occupied_by_player;
	}

	// --- Cache Miss - Perform Live Check ---
	cache.last_check_time = level.time;
	cache.last_check_origin = spawn_point->s.origin;
	cache.frame_time = level.time;
	// Reset flags before checking
	cache.was_occupied_by_player = false;
	cache.has_obstacle = false;

	// Define bounding box for the check - use static const for efficiency
	static const vec3_t mins_scale = vec3_t{ 16, 16, 24 }.scaled(vec3_t{ 1.75f, 1.75f, 1.75f });
	static const vec3_t maxs_scale = vec3_t{ 16, 16, 32 }.scaled(vec3_t{ 1.75f, 1.75f, 1.75f });
	const vec3_t spawn_mins = spawn_point->s.origin - mins_scale;
	const vec3_t spawn_maxs = spawn_point->s.origin + maxs_scale;

	// --- BoxEdicts Checks ---
	// We need to check for players AND obstacles separately to update the cache correctly.
	FilterData check_data = { ignore_ent, 0 }; // Reusable filter data

	// Check 1: Players
	gi.BoxEdicts(spawn_mins, spawn_maxs, nullptr, 0, AREA_SOLID,
		[](edict_t* ent, void* data) -> BoxEdictsResult_t {
			FilterData* fd = static_cast<FilterData*>(data);
			if (ent == fd->ignore_ent) return BoxEdictsResult_t::Skip;
			// Check only for active clients (players or bots)
			if (ent->client && ent->inuse) {
				fd->count++;
				return BoxEdictsResult_t::End; // Found a player/bot
			}
			return BoxEdictsResult_t::Skip;
		}, &check_data);

	cache.was_occupied_by_player = (check_data.count > 0);

	// Check 2: Obstacles (Monsters/Defenses) - ONLY if not already blocked by a player
	if (!cache.was_occupied_by_player) {
		check_data.count = 0; // Reset count for the next check
		gi.BoxEdicts(spawn_mins, spawn_maxs, nullptr, 0, AREA_SOLID,
			[](edict_t* ent, void* data) -> BoxEdictsResult_t {
				FilterData* fd = static_cast<FilterData*>(data);
				if (ent == fd->ignore_ent) return BoxEdictsResult_t::Skip;
				// Check for live monsters OR player defenses
				if ((ent->svflags & SVF_MONSTER && !ent->deadflag) ||
					(ent->inuse && IsPlayerDefense(ent)))
				{
					fd->count++;
					return BoxEdictsResult_t::End; // Found an obstacle
				}
				return BoxEdictsResult_t::Skip;
			}, &check_data);

		cache.has_obstacle = (check_data.count > 0);
	}
	else {
		// If occupied by player, we don't need to check for other obstacles separately for the cache.has_obstacle flag,
		// because the player block takes precedence. We can optionally set has_obstacle=true here too,
		// but it doesn't change the outcome as was_occupied_by_player is already true.
		// Let's keep it simple: if player is there, only was_occupied_by_player matters for the return value.
		// The obstacle check only sets cache.has_obstacle if no player was found.
		cache.has_obstacle = false;
	}


	// This function's primary job is to say if a *player* directly blocks the spawn.
	// The 'has_obstacle' flag in the cache is used by SelectRandomSpawnPoint/SpawnMonsters
	// to decide if alternative positions should be tried.
	return cache.was_occupied_by_player;
}
// --- Corrected SelectRandomSpawnPoint ---
template <typename TFilter>
edict_t* SelectRandomSpawnPoint(TFilter filter) {
    edict_t* availableSpawns[MAX_SPAWN_POINTS]{};
    edict_t* occupiedButUsableSpawns[MAX_SPAWN_POINTS]{};
    // CHANGE: Use size_t for counts, matching MAX_SPAWN_POINTS
    size_t availableCount = 0;
    size_t occupiedCount = 0;

    for (edict_t* spawnPoint : monster_spawn_points()) {
        if (!spawnPoint || !spawnPoint->inuse || !is_valid_vector(spawnPoint->s.origin)) continue;
        const auto& data = spawnPointsData[spawnPoint]; // Get cooldown data
        if (data.isTemporarilyDisabled && level.time < data.cooldownEndsAt) continue; // Check normal cooldown
        if (level.time < data.alternative_cooldown) continue; // Check alternative cooldown

        // Apply the custom filter first
        if (filter(spawnPoint)) {
            // Check if a player is directly blocking the spawn
            if (IsSpawnPointOccupied(spawnPoint)) {
                // Player is blocking, this point is completely unusable for direct spawn.
                // IsSpawnPointOccupied handles cache update.
                // Do NOT add to any list for this function's purpose.
                if (developer->integer > 2) gi.Com_PrintFmt("SelectRandomSpawnPoint: Point {} skipped (player occupied).\n", (int)(spawnPoint - g_edicts));
                continue; // Skip this point entirely
            }
            else {
                // Not blocked by a player. Now check the cache for non-player obstacles.
                const SpawnPointCache& cache = spawn_point_cache[spawnPoint]; // Get cache entry (already updated by IsSpawnPointOccupied call)
                if (cache.has_obstacle) {
                    // Blocked by monster/defense - add to the list for potential alternative spawn.
                    // COMPARISON IS NOW: size_t < size_t (OK)
                    if (occupiedCount < MAX_SPAWN_POINTS) {
                        occupiedButUsableSpawns[occupiedCount++] = spawnPoint;
                    }
                    if (developer->integer > 2) gi.Com_PrintFmt("SelectRandomSpawnPoint: Point {} added to occupiedButUsable (obstacle present).\n", (int)(spawnPoint - g_edicts));
                }
                else {
                    // Not blocked by player AND no obstacle found -> add to available list.
                    // COMPARISON IS NOW: size_t < size_t (OK)
                    if (availableCount < MAX_SPAWN_POINTS) {
                        availableSpawns[availableCount++] = spawnPoint;
                    }
                    if (developer->integer > 2) gi.Com_PrintFmt("SelectRandomSpawnPoint: Point {} added to available.\n", (int)(spawnPoint - g_edicts));
                }
            }
        }
    }

    // Prioritize completely available spawns
    if (availableCount > 0) {
        // NOTE: Assuming irandom can handle size_t or the cast is safe because
        // availableCount is limited by MAX_SPAWN_POINTS (32).
        // If irandom strictly takes int, you might need:
        // const size_t idx = irandom(static_cast<int>(availableCount));
        // But modern implementations often handle unsigned types correctly.
        const size_t idx = irandom(availableCount);
        if (developer->integer > 1) gi.Com_PrintFmt("SelectRandomSpawnPoint: Selected available point {}\n", (int)(availableSpawns[idx] - g_edicts));
        return availableSpawns[idx];
    }

    // If no completely available spawns, try one blocked by an obstacle
    if (occupiedCount > 0) {
        // NOTE: Same potential consideration for irandom's argument type as above.
        const size_t idx = irandom(occupiedCount);
        if (developer->integer > 1) gi.Com_PrintFmt("SelectRandomSpawnPoint: Selected obstacle-occupied point {} (will try alternative).\n", (int)(occupiedButUsableSpawns[idx] - g_edicts));
        return occupiedButUsableSpawns[idx]; // Let SpawnMonsters handle TryAlternative
    }

    if (developer->integer > 1) gi.Com_PrintFmt("SelectRandomSpawnPoint: No suitable spawn points found.\n");
    return nullptr; // No valid spawn points found
}

static void CleanupSpawnPointCache() noexcept { spawn_point_cache.clear(); }

//// Spawn point selection filter
//struct SpawnMonsterFilter {
//	gtime_t currentTime;
//
//	SpawnMonsterFilter(gtime_t time) : currentTime(time) {}
//
//	bool operator()(edict_t* spawnPoint) const {
//		// Combined validation to reduce branches
//		if (!spawnPoint || !spawnPoint->inuse || !is_valid_vector(spawnPoint->s.origin))
//			return false;
//
//		// Direct array access for cooldown check
//		const auto& data = spawnPointsData[spawnPoint];
//
//		// Check normal cooldown
//		if (data.isTemporarilyDisabled && currentTime < data.cooldownEndsAt)
//			return false;
//
//		// NEW: Check alternative position cooldown
//		if (data.alternative_cooldown > 0_sec && currentTime < data.alternative_cooldown)
//			return false;
//
//		// Check if this is a flying spawn point but we need non-flying monsters
//		if (spawnPoint->style == 1 && !HasWaveType(current_wave_type, MonsterWaveType::Flying))
//			return false;
//
//		// Cache the origin reference to avoid multiple member accesses
//		const vec3_t& origin = spawnPoint->s.origin;
//
//		// Check minimum distance from players using modern vector operations
//		for (const auto* const player : active_players()) {
//			if (!player || !player->inuse)
//				continue;
//
//			// Use squared distance to avoid unnecessary sqrt calculations
//			if ((origin - player->s.origin).lengthSquared() < HordeConstants::MIN_PLAYER_DIST_SQ_SPAWNPOINT) // 150^2 = 22500
//				return false;
//		}
//
//		return true;
//	}
//};
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
int8_t GetNumHumanPlayers();

int32_t g_adjusted_monster_cap = 0;

bool allowWaveAdvance = false; // Global variable to control wave advancement
bool boss_spawned_for_wave = false; // to avoid boss spamming

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
	for (uint32_t i = 1; i < globals.num_edicts; i++) {
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
			found_cooldowns_to_reset = true;

			const gtime_t remaining_time = data.cooldownEndsAt - current_time;
			// Calculate reduced duration and ensure it meets the minimum
			const gtime_t reduced_duration = remaining_time * REDUCTION_FACTOR;
			const gtime_t final_duration = std::max(reduced_duration, HordeConstants::MIN_REDUCED_INDIVIDUAL_COOLDOWN);
			data.cooldownEndsAt = current_time + final_duration; // Apply clamped duration

			data.attempts = 0;
		}
	}

	if (found_cooldowns_to_reset) {
		SPAWN_POINT_COOLDOWN *= REDUCTION_FACTOR;
		// Clamp the global cooldown after reduction
		SPAWN_POINT_COOLDOWN = std::max(SPAWN_POINT_COOLDOWN, HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN);

		if (developer->integer > 1) {
			gi.Com_PrintFmt("Global spawn cooldown reduced and clamped to {:.2f}s\n", SPAWN_POINT_COOLDOWN.seconds());
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

static float CalculateCooldownScale(int32_t lvl, const horde::MapSize& mapSize) {
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

// Warning times in seconds
constexpr std::array<float, 3> WARNING_TIMES = { 30.0f, 10.0f, 5.0f };

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
	bool warningIssued[WARNING_TIMES.size()] = { false }; // Initialize all to false

	// Failsafe timeout to prevent getting stuck in a state
	gtime_t         failsafe_timeout = 0_sec;

	gtime_t         last_successful_hud_update = 0_sec;
	uint32_t        failed_updates_count = 0;

	horde::MapSize current_map_size;
	int32_t max_monsters{};  // Cacheado basado en map_size
	gtime_t base_spawn_cooldown;  // Cacheado basado en map_size

	void update_map_size(const char* mapname) {
		current_map_size = GetMapSize(mapname);
		max_monsters = current_map_size.isSmallMap ? HordeConstants::MAX_MONSTERS_SMALL_MAP :
			(current_map_size.isMediumMap ? HordeConstants::MAX_MONSTERS_MEDIUM_MAP : HordeConstants::MAX_MONSTERS_BIG_MAP);
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


int16_t current_wave_level = g_horde_local.level;
bool next_wave_message_sent = false;
auto auto_spawned_bosses = std::unordered_set<edict_t*>{};

// Función para calcular el bono de locura y caos
static inline int32_t CalculateChaosInsanityBonus(int32_t lvl) noexcept {
	if (g_chaotic->integer) return (lvl <= 3) ? 6 : 3;
	switch (g_insane->integer) {
	case 2: return 16;
	case 1: return 8;
	default: return 0;
	}
}

inline int32_t GetAdjustedMonsterCap(const horde::MapSize& mapSize, int32_t waveLevel) {
	// Original base caps
	const int32_t baseSmallCap = HordeConstants::MAX_MONSTERS_SMALL_MAP;  // 14
	const int32_t baseMediumCap = HordeConstants::MAX_MONSTERS_MEDIUM_MAP; // 16
	const int32_t baseBigCap = HordeConstants::MAX_MONSTERS_BIG_MAP;      // 32

	int32_t baseCap;

	// Determine base cap based on map size first
	if (mapSize.isSmallMap) {
		baseCap = baseSmallCap;
	}
	else if (mapSize.isMediumMap) {
		baseCap = baseMediumCap;
	}
	else { // Big map
		baseCap = baseBigCap;
	}

	// Apply early wave reduction (only for non-big maps)
	if (waveLevel <= 10 && !mapSize.isBigMap) {
		// Scale from 60% at wave 1 up to 100% at wave 10
		float reductionFactor = 0.6f + ((waveLevel - 1) * 0.0444f); // (1.0 - 0.6) / 9 = 0.0444...
		reductionFactor = std::clamp(reductionFactor, 0.6f, 1.0f); // Ensure it stays within bounds
		baseCap = static_cast<int32_t>(baseCap * reductionFactor);
	}

	// --- Player Count Bonus ---
	int32_t finalAdjustedCap = baseCap; // Start with the potentially reduced base cap
	const int32_t numHumanPlayers = GetNumHumanPlayers();

	if (numHumanPlayers > 1) {
		constexpr int32_t MAX_BONUS_PLAYERS = 3; // Cap bonus contribution at 3 extra players (i.e., players 2, 3, 4)
		constexpr int32_t BONUS_PER_PLAYER = 2;  // +2 monsters per extra contributing player
		const int32_t extraPlayers = std::min(numHumanPlayers - 1, MAX_BONUS_PLAYERS);
		const int32_t playerBonus = extraPlayers * BONUS_PER_PLAYER;
		finalAdjustedCap += playerBonus;

		if (developer->integer > 1) {
			gi.Com_PrintFmt("GetAdjustedMonsterCap: Wave={}, BaseCap={}, Humans={}, BonusPlayers={}, Bonus={}, FinalCap={}\n",
				waveLevel, baseCap, numHumanPlayers, extraPlayers, playerBonus, finalAdjustedCap);
		}
	}
	else {
		if (developer->integer > 1) {
			gi.Com_PrintFmt("GetAdjustedMonsterCap: Wave={}, BaseCap={}, Humans={}, No Bonus, FinalCap={}\n",
				waveLevel, baseCap, numHumanPlayers, finalAdjustedCap);
		}
	}
	// --- End Player Count Bonus ---

	// Ensure the cap doesn't go below a minimum reasonable value (e.g., 6)
	finalAdjustedCap = std::max(6, finalAdjustedCap);

	// Store globally
	g_adjusted_monster_cap = finalAdjustedCap;

	return g_adjusted_monster_cap;
}

// Modify the existing ClampNumToSpawn function
inline static void ClampNumToSpawn(const horde::MapSize& mapSize) { // mapSize parameter might no longer be needed here
	// g_adjusted_monster_cap is now calculated in GetAdjustedMonsterCap and includes player bonus
	const int32_t maxAllowed = g_adjusted_monster_cap;

	// Ensure g_adjusted_monster_cap was initialized (fallback if called too early)
	if (maxAllowed <= 0) {
		// Fallback to basic map defaults if the global cap isn't ready
		const int32_t fallbackCap = mapSize.isSmallMap ? HordeConstants::MAX_MONSTERS_SMALL_MAP :
			(mapSize.isBigMap ? HordeConstants::MAX_MONSTERS_BIG_MAP : HordeConstants::MAX_MONSTERS_MEDIUM_MAP);
		g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, fallbackCap);
		if (developer->integer) {
			gi.Com_PrintFmt("ClampNumToSpawn: WARN - g_adjusted_monster_cap not ready, used fallback {}\n", fallbackCap);
		}
	}
	else {
		// Clamp using the globally calculated value
		g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, maxAllowed);
		if (developer->integer > 1) {
			gi.Com_PrintFmt("ClampNumToSpawn: Clamping num_to_spawn to {} (g_adjusted_monster_cap)\n", maxAllowed);
		}
	}
}

static int32_t CalculateQueuedMonsters(const horde::MapSize& mapSize, int32_t lvl, bool isHardMode) noexcept {
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
	static constexpr int32_t MAX_WAVE_LEVEL = 250;
	static constexpr int32_t MAX_HUMAN_PLAYERS = 16;

	// Pre-computed cooldown scales to avoid recalculation
	float cooldownScales[3][MAX_WAVE_LEVEL + 1] = {}; // [mapType][level]

	// Cached base counts
	int32_t baseCountsByLevel[3][MAX_WAVE_LEVEL + 1] = {}; // [mapType][level]
	// Removed: int32_t additionalSpawnsByLevel[MAX_WAVE_LEVEL+1] = {}; // Not needed, calculation moved

	// Player multipliers (precomputed)
	float playerMultipliers[MAX_HUMAN_PLAYERS + 1] = {};

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
				else countIndex = 3; // Levels > 15

				// Store pre-computed base count
				baseCountsByLevel[mapType][level] = BASE_COUNTS[mapType][countIndex];
			}
		}

		// Removed initialization for additionalSpawnsByLevel as it's calculated directly
		// in UnifiedAdjustSpawnRate now, eliminating the unused variables.

		// Initialize cooldown scales (can be accessed by mapType and level)
		horde::MapSize smallMap = { true, false, false };
		horde::MapSize mediumMap = { false, false, true }; // Corrected: Medium map should have isMediumMap true
		horde::MapSize bigMap = { false, true, false };

		for (int32_t level = 0; level <= MAX_WAVE_LEVEL; ++level) {
			cooldownScales[0][level] = CalculateCooldownScale(level, smallMap);  // Index 0 = Small
			cooldownScales[1][level] = CalculateCooldownScale(level, mediumMap); // Index 1 = Medium
			cooldownScales[2][level] = CalculateCooldownScale(level, bigMap);    // Index 2 = Big
		}
	}
} g_waveScalingCache;

void UnifiedAdjustSpawnRate(const horde::MapSize& mapSize, int32_t lvl, int32_t humanPlayers) noexcept {
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

	// --- CORRECTED SECTION ---
	// Calculate additional spawn count directly, without using the removed cache array
	int32_t additionalSpawn;
	if (safeLevel < 8) {
		additionalSpawn = 6; // Default for early levels
	}
	else {
		// Apply map-specific base value from constants
		additionalSpawn = mapSize.isSmallMap ? ADDITIONAL_SPAWNS[0] :
			mapSize.isBigMap ? ADDITIONAL_SPAWNS[2] : ADDITIONAL_SPAWNS[1];

		// Level-based adjustment for high levels
		if (safeLevel > 25) {
			additionalSpawn = static_cast<int32_t>(additionalSpawn * 1.6f);
		}
	}
	// --- END CORRECTED SECTION ---

	// Apply difficulty adjustments (Chaos/Insanity bonus)
	if (safeLevel >= 3 && (g_chaotic->integer || g_insane->integer)) {
		additionalSpawn += CalculateChaosInsanityBonus(safeLevel);
	}

	// Get cooldown from cache and apply adjustments
	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);
	const float cooldownScale = g_waveScalingCache.cooldownScales[mapTypeIndex][safeLevel];
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	// Apply difficulty-based cooldown adjustments
	if (g_chaotic->integer || g_insane->integer) {
		SPAWN_POINT_COOLDOWN *= HordeConstants::TIME_REDUCTION_MULTIPLIER;
	}
	else {
		// Normal difficulty adjustment
		SPAWN_POINT_COOLDOWN *= 1.2f;
	}

	// Apply periodic difficulty scaling (every 3 levels)
	if (safeLevel > 0 && safeLevel % 3 == 0) { // Added check for safeLevel > 0
		const float difficultyMultiplier = g_waveScalingCache.playerMultipliers[safePlayerCount];
		baseCount = static_cast<int32_t>(baseCount * difficultyMultiplier);

		const float cooldownReduction = (mapSize.isBigMap ? 0.1f : 0.15f) * difficultyMultiplier;
		SPAWN_POINT_COOLDOWN = std::max(
			SPAWN_POINT_COOLDOWN - gtime_t::from_sec(cooldownReduction),
			1.0_sec // Ensure cooldown doesn't go below 1.0 second
		);
	}

	// Final cooldown clamping
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN,
		HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN, // Use the constant
		3.5_sec); // Keep upper bound or define a MAX constant
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

static constexpr gtime_t calculate_max_wave_time(int32_t wave_level) {
	// Calculate the time base based on level
	gtime_t base_time = HordeConstants::BASE_MAX_WAVE_TIME + HordeConstants::TIME_INCREASE_PER_LEVEL * wave_level;

	// Limit base time to 240 seconds (Increased from 220)
	base_time = (base_time <= 240_sec) ? base_time : 240_sec; // MODIFIED: Increased cap

	// Add extra time if it's a boss wave (levels multiples of 5 after 10)
	if (wave_level >= 10 && wave_level % 5 == 0) {
		base_time += HordeConstants::BOSS_TIME_BONUS;
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

static ConditionParams GetConditionParams(const horde::MapSize& mapSize, int32_t numHumanPlayers, int32_t lvl) {
	ConditionParams params;

	// Initial validation (keep as is)
	if (g_horde_local.level < 0 || lvl < 0) {
		return params;
	}

	auto configureMapParams = [&](ConditionParams& params) {
		if (mapSize.isBigMap) {
			params.maxMonsters = (numHumanPlayers >= 3) ? 26 : 22;
			// params.timeThreshold = random_time(20_sec, 26_sec); // Original Increased
			params.timeThreshold = random_time(24_sec, 30_sec); // MODIFIED: Increased conditional timer
		}
		else if (mapSize.isSmallMap) {
			params.maxMonsters = (numHumanPlayers >= 3) ? 12 : 9;
			// params.timeThreshold = random_time(18_sec, 25_sec); // Original Increased more for small maps
			params.timeThreshold = random_time(22_sec, 28_sec); // MODIFIED: Increased conditional timer
		}
		else { // Medium maps
			params.maxMonsters = (numHumanPlayers >= 3) ? 17 : 13;
			// params.timeThreshold = random_time(20_sec, 27_sec); // Original Increased
			params.timeThreshold = random_time(24_sec, 31_sec); // MODIFIED: Increased conditional timer
		}
		};

	configureMapParams(params);

	// Progressive adjustment based on level (keep as is)
	params.maxMonsters += std::min(lvl / 4, 8);
	params.timeThreshold += gtime_t::from_ms(75ll * std::min(lvl / 3, 4));

	// Early wave time REDUCTION (keep as is)
	float timeMultiplier = 1.0f;
	if (lvl <= 10) {
		timeMultiplier = 1.0f - (0.25f * (static_cast<float>(lvl - 1) / 9.0f));
		timeMultiplier = std::max(0.1f, timeMultiplier);
	}

	// High level adjustment (keep as is)
	if (lvl > 10) {
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * 1.2f);
		params.timeThreshold += 0.15_sec;
	}

	// Difficulty adjustment (keep as is)
	if (g_chaotic->integer || g_insane->integer) {
		if (numHumanPlayers <= 3) {
			// params.timeThreshold += random_time(6_sec, 10_sec); // Original Slightly increased bonus time
			params.timeThreshold += random_time(7_sec, 12_sec); // MODIFIED: Slightly more bonus time
		}
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * 1.1f);
	}

	// Configuration for low percentage of remaining monsters
	// params.lowPercentageTimeThreshold = random_time(10_sec, 20_sec); // Original Increased
	params.lowPercentageTimeThreshold = random_time(14_sec, 24_sec); // MODIFIED: Increased low % timer
	// params.lowPercentageThreshold = 0.3f; // Original threshold
	params.lowPercentageThreshold = 0.35f; // MODIFIED: Start low % timer slightly later

	// Configuration for independent time based on level (uses the updated calculate_max_wave_time)
	params.independentTimeThreshold = calculate_max_wave_time(lvl); // This now uses the increased base time

	// Apply multiplier to time thresholds for early waves (keep as is)
	if (lvl <= 10) {
		params.timeThreshold = gtime_t::from_sec(params.timeThreshold.seconds() * timeMultiplier);
		params.lowPercentageTimeThreshold = gtime_t::from_sec(params.lowPercentageTimeThreshold.seconds() * timeMultiplier);
		params.independentTimeThreshold = gtime_t::from_sec(params.independentTimeThreshold.seconds() * timeMultiplier);
	}

	// Final parameter validation (keep as is)
	params.maxMonsters = std::max(1, params.maxMonsters);
	params.timeThreshold = std::max(1_sec, params.timeThreshold);
	params.lowPercentageTimeThreshold = std::max(1_sec, params.lowPercentageTimeThreshold);
	params.independentTimeThreshold = std::max(1_sec, params.independentTimeThreshold);

	return params;
}


inline int32_t GetAdjustedMonsterCap(const horde::MapSize& mapSize, int32_t waveLevel);

void ResetChampionMonsterState() {
	champion_spawned_this_wave = false;
	champion_spawn_cooldown = 0;
}

void InitializeWaveType(int32_t waveLevel);

static void Horde_InitLevel(const int32_t lvl) {

    g_horde_retaliation_active = false;
    g_horde_retaliation_end_time = 0_sec;
    g_horde_retaliation_target_player = nullptr;

    ResetChampionMonsterState();

    if (!(lvl >= 10 && lvl % 5 == 0)) { // If NOT a boss wave
        InitializeWaveType(lvl);
    }
    else {                             // If IT IS a boss wave
        current_wave_type = MonsterWaveType::None;
    }

    g_horde_local.update_map_size(GetCurrentMapName());
    GetAdjustedMonsterCap(g_horde_local.current_map_size, lvl); // Sets g_adjusted_monster_cap

    g_independent_timer_start = level.time;

    // g_totalMonstersInWave IS MOVED FURTHER DOWN

    last_wave_number++;
    g_horde_local.level = lvl;
    current_wave_level = lvl;
    boss_spawned_for_wave = false;
    next_wave_message_sent = false;

    CleanupSpawnPointCache();
    VerifyAndAdjustBots();

    g_independent_timer_start = level.time;
    g_horde_local.conditionStartTime = 0_sec;
    g_horde_local.conditionTriggered = false;
    g_horde_local.waveEndTime = 0_sec;
    g_horde_local.lastPrintTime = 0_sec;

    g_lastParams = GetConditionParams(g_horde_local.current_map_size, GetNumHumanPlayers(), lvl);

for (size_t i = 0; i < WARNING_TIMES.size(); i++) { // Use WARNING_TIMES.size()
    g_horde_local.warningIssued[i] = false;
}

    if (developer->integer) {
        gi.Com_PrintFmt("Debug: Wave {} init - Timer threshold: {:.2f}s\n",
            lvl, g_lastParams.timeThreshold.seconds());
    }

    // UnifiedAdjustSpawnRate sets g_horde_local.num_to_spawn and global SPAWN_POINT_COOLDOWN
    UnifiedAdjustSpawnRate(g_horde_local.current_map_size, lvl, GetNumHumanPlayers());

    // ***** FIX: Set g_totalMonstersInWave AFTER num_to_spawn is determined *****
    if (g_horde_local.num_to_spawn < 0) g_horde_local.num_to_spawn = 0; // Safety check
    // Ensure num_to_spawn (int32_t) doesn't overflow uint16_t for g_totalMonstersInWave.
    // ClampNumToSpawn (called within UnifiedAdjustSpawnRate) should already limit num_to_spawn.
    g_totalMonstersInWave = static_cast<uint16_t>(
        std::min(g_horde_local.num_to_spawn, static_cast<int32_t>(std::numeric_limits<uint16_t>::max()))
    );
    // Optional: If queued monsters should count towards the initial total for percentage calculations:
    // g_totalMonstersInWave = static_cast<uint16_t>(
    //    std::min(g_horde_local.num_to_spawn + g_horde_local.queued_monsters, 
    //             static_cast<int32_t>(std::numeric_limits<uint16_t>::max()))
    // );
    // Based on current logic elsewhere, g_totalMonstersInWave seems to track only the immediate spawn pool.

    CheckAndApplyBenefit(lvl);
    // ResetAllSpawnAttempts(); // This is now redundant if ResetCooldowns (revised) does a full SpawnPointData{} reset.
    ResetCooldowns();          // Call the REVISED ResetCooldowns. It resets individual spawn point data
                               // and the two specific trackers (g_monsterSpawnTracker, g_spawnPointTimeTracker).
                               // It NO LONGER sets the global SPAWN_POINT_COOLDOWN.
    Horde_CleanBodies();
}

bool G_IsDeathmatch() noexcept {
	return deathmatch->integer;
}

bool G_IsCooperative() noexcept {
	return coop->integer && !g_horde->integer;
}

struct weighted_item_t;
using weight_adjust_func_t = void(*)(const weighted_item_t& item, float& weight);


// Define the function pointer type (keep this)
struct weighted_item_t;
using weight_adjust_func_t = void(*)(const weighted_item_t& item, float& weight);


// Structure definition (as you provided)
struct HordeItemInfo {
	item_id_t id;      // Use item_id_t instead of const char*
	float weight;      // Relative probability (higher is more common)
	int minWave;       // Minimum wave level for this item to appear

	constexpr HordeItemInfo(item_id_t item_id, float w, int mw)
		: id(item_id), weight(w), minWave(mw) {
	}
};

// The comprehensive static array
static const HordeItemInfo hordeItemData[] = {
	// --- Armor ---
	{ IT_ARMOR_SHARD,         1.5f,  1 }, // Common, small boost
	{ IT_ARMOR_JACKET,        0.8f,  3 }, // Basic armor
	{ IT_ARMOR_COMBAT,        0.5f,  8 }, // Better armor
	{ IT_ARMOR_BODY,          0.2f, 15 }, // Best standard armor
	{ IT_ITEM_POWER_SCREEN,   0.3f,  7 }, // Power Armor (early)
	{ IT_ITEM_POWER_SHIELD,   0.01f, 19 }, // Power Armor (late)

	// --- Weapons ---
	// Blaster is skipped (always have)
	{ IT_WEAPON_CHAINFIST,    0.1f,  1 }, // Melee, niche drop
	{ IT_WEAPON_SHOTGUN,      1.0f,  1 }, // Basic weapon
	{ IT_WEAPON_SSHOTGUN,     0.7f,  3 }, // Strong early/mid weapon
	{ IT_WEAPON_MACHINEGUN,   1.0f,  1 }, // Basic weapon
	{ IT_WEAPON_ETF_RIFLE,    0.6f,  5 }, // Rogue weapon (mid)
	{ IT_WEAPON_CHAINGUN,     0.5f,  7 }, // Mid-tier DPS
	{ IT_WEAPON_GLAUNCHER,    0.5f,  5 }, // Area denial (mid)
	{ IT_WEAPON_PROXLAUNCHER, 0.3f,  8 }, // Rogue weapon (mid/late trap)
	{ IT_WEAPON_RLAUNCHER,    0.4f,  7 }, // Powerful splash (mid/late)
	{ IT_WEAPON_HYPERBLASTER, 0.4f,  9 }, // Energy weapon (mid/late)
	{ IT_WEAPON_IONRIPPER,    0.4f, 10 }, // Xatrix weapon (mid/late)
	{ IT_WEAPON_PLASMABEAM,   0.2f, 12 }, // Rogue weapon (late, high DPS)
	{ IT_WEAPON_RAILGUN,      0.25f, 15 }, // High skill/damage (late)
	{ IT_WEAPON_PHALANX,      0.25f, 14 }, // Xatrix weapon (late)
	{ IT_WEAPON_BFG,          0.05f, 20 }, // Ultimate weapon (very late, rare)
	{ IT_WEAPON_DISRUPTOR,    0.15f, 18 }, // Rogue weapon (late, piercing)
	// Grenades/Traps/Tesla as 'weapons' (for direct drop maybe?)
	{ IT_AMMO_GRENADES,       0.4f,  2 }, // Hand grenades item
	{ IT_AMMO_TRAP,           0.2f,  6 }, // Xatrix trap item
	{ IT_AMMO_TESLA,          0.15f, 8 }, // Rogue tesla item

	// --- Ammo --- (Generally higher weight than corresponding weapons)
	{ IT_AMMO_SHELLS,         2.0f,  1 }, // Common
	{ IT_AMMO_BULLETS,        2.0f,  1 }, // Common
	{ IT_AMMO_GRENADES,       1.2f,  2 }, // Mid frequency
	{ IT_AMMO_ROCKETS,        1.0f,  6 }, // Needed for RL
	{ IT_AMMO_CELLS,          1.5f,  7 }, // Needed for energy weapons
	{ IT_AMMO_SLUGS,          0.8f, 14 }, // Needed for Railgun
	{ IT_AMMO_MAGSLUG,        0.8f, 13 }, // Needed for Phalanx (Xatrix)
	{ IT_AMMO_FLECHETTES,     1.5f,  4 }, // Needed for ETF Rifle (Rogue)
	{ IT_AMMO_PROX,           0.6f,  7 }, // Needed for Prox Launcher (Rogue)
	{ IT_AMMO_ROUNDS,         0.5f, 17 }, // Needed for Disruptor (Rogue)
	{ IT_AMMO_TRAP,           0.4f,  5 }, // Needed for Trap (Xatrix)
	{ IT_AMMO_TESLA,          0.3f,  7 }, // Needed for Tesla (Rogue)
	{ IT_AMMO_NUKE,           0.02f, 25 }, // Very rare ammo/powerup (Rogue)

	// --- Powerups ---
	{ IT_ITEM_QUAD,           0.1f,  8 }, // Rare, powerful
	{ IT_ITEM_QUADFIRE,       0.1f,  6 }, // Rare, powerful (Xatrix)
	{ IT_ITEM_INVULNERABILITY,0.05f, 12 }, // Very rare, very powerful
	{ IT_ITEM_INVISIBILITY,   0.2f,  5 }, // Situational
	{ IT_ITEM_SILENCER,       0.15f, 4 }, // Situational
	//{ IT_ITEM_REBREATHER,     0.1f,  1 }, // Map dependent, low weight
	//{ IT_ITEM_ENVIROSUIT,     0.1f,  1 }, // Map dependent, low weight
	{ IT_ITEM_ADRENALINE,     0.3f,  3 }, // Permanent health boost, good reward
	{ IT_ITEM_BANDOLIER,      0.5f,  4 }, // Good ammo capacity boost
	{ IT_ITEM_PACK,           0.25f, 10 }, // Excellent ammo capacity boost
	{ IT_ITEM_IR_GOGGLES,     0.15f, 7 }, // Situational (Rogue)
	{ IT_ITEM_DOUBLE,         0.15f, 5 }, // Damage boost (Rogue)
	//{ IT_ITEM_SPHERE_VENGEANCE, 0.1f, 15 }, // Powerful sphere (Rogue)
	{ IT_ITEM_SPHERE_HUNTER,  0.1f, 12 }, // Powerful sphere (Rogue)
	{ IT_ITEM_SPHERE_DEFENDER,0.1f,  8 }, // Powerful sphere (Rogue)
	{ IT_ITEM_DOPPELGANGER,   0.15f, 6 }, // Utility/Distraction (Rogue)
	{ IT_ITEM_TELEPORT,       0.2f,  3 }, // Utility Escape
	{ IT_ITEM_SENTRYGUN,      0.3f,  5 }, // Deployable Defense

	// --- Health ---
	{ IT_HEALTH_SMALL,        1.8f,  1 }, // Very common
	{ IT_HEALTH_MEDIUM,       1.5f,  1 }, // Common
	{ IT_HEALTH_LARGE,        0.8f,  3 }, // Less common
	{ IT_HEALTH_MEGA,         0.1f, 10 }, // Rare, powerful boost

	// --- Legacy Heads (Optional - Minor permanent boost) ---
	//{ IT_ITEM_ANCIENT_HEAD,   0.05f, 10 },
	//{ IT_ITEM_LEGACY_HEAD,    0.05f, 10 },

	// End of list marker (optional, but good practice if needed)
	// { IT_NULL, 0.0f, 0 }
};


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

// Structure for an optional component to add to a wave
struct WaveOptionalComponent {
	MonsterWaveType type = MonsterWaveType::None; // The type to potentially add
	float chance = 0.0f;           // Probability (0.0 to 1.0) of adding this type
	// int min_wave = 1;           // Optional: Could add min wave *within the range*
};

// Define the wave progression using constexpr std::array
// IMPORTANT: Keep this sorted by max_wave ascending!
struct WaveDefinition {
	int max_wave;                       // This definition applies for waves <= max_wave
	MonsterWaveType base_type;          // Guaranteed monster types for this range
	// Use std::array for optionals if the max number is known and fixed,
	// otherwise std::vector is acceptable here as it's only initialized once.
	// Let's assume a max of 4 optionals for this example.
	std::array<WaveOptionalComponent, 4> optionals;
	size_t num_optionals; // Track how many are actually used

	// Constexpr constructor if needed, or aggregate initialization
};

constexpr std::array<WaveDefinition, 8> wave_definitions = { {
		// Waves 1-5: Basic ground light units
		{ 5, MonsterWaveType::Light | MonsterWaveType::Ground, {{{},{},{},{}}}, 0}, // No optionals
		// Waves 6-10: Add chance for Small units and a small chance for Flying
		{ 10, MonsterWaveType::Light | MonsterWaveType::Ground, {{
			{MonsterWaveType::Small, 0.45f},
			{MonsterWaveType::Flying, 0.20f},
			{},{} }}, 2},
			// Waves 11-15: Introduce Medium units, increase Special and Flying chance
			{ 15, MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ground, {{
				 {MonsterWaveType::Special, 0.65f},
				 {MonsterWaveType::Flying, 0.25f},
				 {MonsterWaveType::Small, 0.2f},
				 {} }}, 3},
				 // Waves 16-20: Introduce Heavy units, increase Fast and Flying chance
				 { 20, MonsterWaveType::Medium | MonsterWaveType::Heavy | MonsterWaveType::Ground, {{
					  {MonsterWaveType::Fast, 0.55f},
					  {MonsterWaveType::Flying, 0.30f},
					  {MonsterWaveType::Special, 0.3f},
					  {} }}, 3},
					  // Waves 21-35: Introduce Bomber units, increase Fast and Flying chance
					  { 25, MonsterWaveType::Bomber | MonsterWaveType::Heavy | MonsterWaveType::Ground, {{
						   {MonsterWaveType::Bomber, 0.55f},
						   {MonsterWaveType::Flying, 0.30f},
						   {MonsterWaveType::Special, 0.3f},
						   {} }}, 3},
						   // Waves 36-40: Focus on Heavy/Elite, high chance for Fast/Special, moderate Flying
						   { 35, MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Ground, {{
								{MonsterWaveType::Special | MonsterWaveType::Fast, 0.75f},
								{MonsterWaveType::Flying, 0.30f},
								{MonsterWaveType::Medium, 0.28f},
								{} }}, 3},
								// Waves 41-45: Add SemiBoss chance, keep others relevant
								{ 40, MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Special | MonsterWaveType::Ground, {{
									 {MonsterWaveType::SemiBoss, 0.45f},
									 {MonsterWaveType::Flying, 0.35f},
									 {MonsterWaveType::Fast | MonsterWaveType::Bomber, 0.35f },
									 { MonsterWaveType::Medium, 0.30f } }}, 4},
									 // Waves 46+: High chance for SemiBoss, Flying, Fast
									 { 999, MonsterWaveType::Elite | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Ground, {{
										  {MonsterWaveType::SemiBoss, 0.35f},
										  {MonsterWaveType::Flying, 0.40f},
										  {MonsterWaveType::Bomber | MonsterWaveType::Spawner, 0.6f},
										  {} }}, 3}
								 } };

// Updated function to get the wave type
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


	if (!forceSpecialWave && !WasLastWaveSpecial()) {
		for (const auto& wave : special_waves) {
			if (waveNumber >= wave.min_wave &&
				(wave.max_wave == -1 || waveNumber <= wave.max_wave) &&
				!WasRecentlyUsed(wave.type) &&
				frandom() < wave.chance)
			{
				selected_type = wave.type;
				gi.LocBroadcast_Print(PRINT_HIGH, wave.message);
				StoreWaveType(selected_type); // Store special wave type
				return selected_type;          // Return immediately
			}
		}
	}
	// --- End Special Wave Logic ---


	// --- Regular Wave Composition using the new structure ---

	// Find the first definition where waveNumber <= max_wave
	// This works because the vector is sorted by max_wave
	auto it = std::find_if(wave_definitions.begin(), wave_definitions.end(),
		[waveNumber](const WaveDefinition& def) {
			return waveNumber <= def.max_wave;
		});

	// Handle the case where no definition matches (shouldn't happen with a catch-all)
	if (it == wave_definitions.end()) {
		if (developer->integer) {
			gi.Com_PrintFmt("Warning: No wave definition found for wave {}. Using default.\n", waveNumber);
		}
		// Return a safe default (e.g., the first definition's base)
		selected_type = wave_definitions[0].base_type;
	}
	else {
		// Apply the base type from the found definition
		const WaveDefinition& def = *it;
		selected_type = def.base_type;

		// Process optional components for this definition
		for (const auto& optional : def.optionals) {
			// Check if the type is valid, passes the random chance, and wasn't recently used
			if (optional.type != MonsterWaveType::None &&
				frandom() < optional.chance &&
				!WasRecentlyUsed(optional.type))
			{
				// Combine the optional type with the selected type using the overloaded '|' operator
				selected_type |= optional.type;

				// Optional: Play sound only for specific significant additions like Flying
				if (HasWaveType(optional.type, MonsterWaveType::Flying)) {
					// Make sure 'incoming' sound index is valid and loaded
					if (incoming) { // Check if the sound index is valid
						gi.sound(world, CHAN_VOICE, incoming, 1, ATTN_NONE, 0);
					}
				}
			}
		}
	}

	// Store the final selected type for history tracking
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
// Update the function to accept MonsterTypeID

// Function declaration for the new bounds helper
bool GetPredictedScaledBounds(horde::MonsterTypeID typeId, vec3_t& out_mins, vec3_t& out_maxs);

// 1. Modified MonsterTypeInfo struct definition (ensure this is defined before the array)
struct MonsterTypeInfo {
	//wave typeid
	horde::MonsterTypeID typeId;
	MonsterWaveType types;
	int minWave;
	float weight;

	// bounds check
	vec3_t default_mins;
	vec3_t default_maxs;
	float s_scale; // <-- ADDED: Intended scale for this monster type

	// Constructor updated to include scale (defaults to 1.0f)
	constexpr MonsterTypeInfo(horde::MonsterTypeID id, MonsterWaveType t, int w, float wt,
		vec3_t d_mins, vec3_t d_maxs, float scale = 1.0f) // Add scale parameter
		: typeId(id), types(t), minWave(w), weight(wt),
		default_mins(d_mins), default_maxs(d_maxs), s_scale(scale) // Initialize scale
	{
	}
};

// 2. The complete monsterTypes array with s_scale added
static const MonsterTypeInfo monsterTypes[] = {
	// Basic Infantry (Scale 1.0)
	{horde::MonsterTypeID::SOLDIER_LIGHT, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 1.0f, {-16,-16,-24}, {16,16,32}, 1.0f},
	{horde::MonsterTypeID::SOLDIER, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 0.9f, {-16,-16,-24}, {16,16,32}, 1.0f},
	{horde::MonsterTypeID::SOLDIER_SS, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 2, 0.8f, {-16,-16,-24}, {16,16,32}, 1.0f},
	{horde::MonsterTypeID::INFANTRY_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 3, 0.85f, {-16,-16,-24}, {16,16,32}, 1.0f},
	{horde::MonsterTypeID::SOLDIER_HYPERGUN, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 4, 0.7f, {-16,-16,-24}, {16,16,32}, 1.0f},
	{horde::MonsterTypeID::SOLDIER_RIPPER, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.8f, {-16,-16,-24}, {16,16,32}, 1.0f},
	{horde::MonsterTypeID::SOLDIER_LASERGUN, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 10, 0.8f, {-16,-16,-24}, {16,16,32}, 1.0f},

	// Early Flying Units
	{horde::MonsterTypeID::FLYER, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Fast, 1, 0.7f, {-16,-16,-24}, {16,16,16}, 1.0f},
	{horde::MonsterTypeID::FIXBOT, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.45f, {-16,-16,-12}, {16,16,12}, 1.4f}, // Scale 1.4
	{horde::MonsterTypeID::HOVER_VANILLA, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Ranged, 7, 0.6f, {-24,-24,-24}, {24,24,32}, 1.0f},
	{horde::MonsterTypeID::FLOATER, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Ranged, 12, 0.6f, {-24,-24,-24}, {24,24,48}, 0.9f}, // Scale 0.9

	// Special Wave Units
	{horde::MonsterTypeID::GEKK, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Small | MonsterWaveType::Mutant | MonsterWaveType::Gekk, 4, 0.7f, {-16,-16,-24}, {16,16,-8}, 1.0f},
	{horde::MonsterTypeID::PARASITE, MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Melee, 5, 0.6f, {-16,-16,-24}, {16,16,24}, 1.0f},
	{horde::MonsterTypeID::STALKER, MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Fast | MonsterWaveType::Arachnophobic, 7, 0.6f, {-28,-28,-18}, {28,28,-4}, 1.0f},
	{horde::MonsterTypeID::BRAIN, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special | MonsterWaveType::Melee | MonsterWaveType::Mutant, 6, 0.7f, {-16,-16,-24}, {16,16,-8}, 1.0f},

	// Medium Units
	{horde::MonsterTypeID::GUNNER_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 8, 0.8f, {-16,-16,-24}, {16,16,36}, 1.0f},
	{horde::MonsterTypeID::INFANTRY, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Heavy | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 11, 0.85f, {-16,-16,-24}, {16,16,32}, 1.2f}, // Scale 1.2
	{horde::MonsterTypeID::MEDIC, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special, 7, 0.5f, {-24,-24,-24}, {24,24,32}, 1.0f},
	{horde::MonsterTypeID::BERSERK, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Melee | MonsterWaveType::Berserk , 6, 0.8f, {-16,-16,-24}, {16,16,32}, 1.0f},
	{horde::MonsterTypeID::CHICK, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 7, 0.6f, {-32,-32,-24}, {32,32,64}, 1.0f},

	// Arachnophobic Units
	{horde::MonsterTypeID::SPIDER, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 7, 0.35f, {-41, -41, -17}, {41, 41, 41}, 0.7f}, // Scale 0.7
	{horde::MonsterTypeID::GUNCMDR_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 12, 0.4f, {-16,-16,-24}, {16,16,36}, 1.25f}, // Scale 1.25
	{horde::MonsterTypeID::ARACHNID2, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 18, 0.4f, {-48, -48, -20}, {48, 48, 48}, 0.85f}, // Scale 0.85
	{horde::MonsterTypeID::GM_ARACHNID, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Heavy | MonsterWaveType::Elite, 15, 0.45f, {-48, -48, -20}, {48, 48, 48}, 0.85f}, // Scale 0.85
	{horde::MonsterTypeID::PSX_ARACHNID, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite | MonsterWaveType::Spawner, 25, 0.35f, {-48, -48, -20}, {48, 48, 48}, 1.0f},

	// Mutant Units
	{horde::MonsterTypeID::MUTANT, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 9, 0.7f, {-18,-18,-24}, {18,18,30}, 1.0f},
	{horde::MonsterTypeID::SHAMBLER_SMALL, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Mutant | MonsterWaveType::Shambler, 14, 0.4f, {-32,-32,-24}, {32,32,64}, 0.6f}, // Scale 0.6
	{horde::MonsterTypeID::REDMUTANT, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Elite | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 14, 0.35f, {-18,-18,-24}, {18,18,30}, 1.1f}, // Scale 1.1

	// Heavy Ground Units
	{horde::MonsterTypeID::GLADIATOR, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged, 12, 0.7f, {-32,-32,-24}, {32,32,42}, 1.0f},
	{horde::MonsterTypeID::GUNNER, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 12, 0.8f, {-16,-16,-24}, {16,16,36}, 1.0f},
	{horde::MonsterTypeID::TANK_SPAWNER, MonsterWaveType::Ground | MonsterWaveType::Spawner | MonsterWaveType::Heavy | MonsterWaveType::Medium | MonsterWaveType::Elite, 13, 0.6f, {-32,-32,-16}, {32,32,64}, 1.0f},
	{horde::MonsterTypeID::TANK, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 14, 0.4f,  {-32,-32,-16}, {32,32,64}, 1.0f},
	{horde::MonsterTypeID::TANK_COMMANDER, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 16, 0.5f,  {-32,-32,-16}, {32,32,64}, 1.0f},
	{horde::MonsterTypeID::GUNCMDR, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Bomber, 15, 0.7f, {-16,-16,-24}, {16,16,36}, 1.25f}, // Scale 1.25
	{horde::MonsterTypeID::RUNNERTANK, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Fast, 21, 0.5f,  {-32,-32,-16}, {32,32,64}, 1.0f},
	{horde::MonsterTypeID::CHICK_HEAT, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Fast, 13, 0.6f, {-32,-32,-24}, {32,32,64}, 1.0f},

	// Elite Flying Units
	{horde::MonsterTypeID::HOVER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 18, 0.5f, {-24,-24,-24}, {24,24,32}, 1.0f},
	{horde::MonsterTypeID::DAEDALUS, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 21, 0.4f,{-24,-24,-24}, {24,24,32}, 1.0f},
	{horde::MonsterTypeID::FLOATER_TRACKER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 19, 0.45f, {-24,-24,-24}, {24,24,48}, 1.0f},
	{horde::MonsterTypeID::DAEDALUS_BOMBER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite | MonsterWaveType::Bomber, 19, 0.35f,{-24,-24,-24}, {24,24,32}, 1.0f},

	// Elite Ground Units
	{horde::MonsterTypeID::GLADIATOR_B, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f, {-32,-32,-24}, {32,32,42}, 1.0f},
	{horde::MonsterTypeID::GLADIATOR_C, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f, {-32,-32,-24}, {32,32,42}, 1.0f},
	{horde::MonsterTypeID::SHAMBLER, MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 22, 0.4f,  {-32,-32,-24}, {32,32,64}, 1.0f},
	{horde::MonsterTypeID::TANK_64, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 28, 0.3f,  {-32,-32,-16}, {32,32,64}, 1.1f}, // Scale 1.1

	// Special Heavy Units
	{horde::MonsterTypeID::JANITOR, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Bomber, 21, 0.5f, {-64,-64,-0}, {64,64,112}, 0.6f}, // Scale 0.6
	{horde::MonsterTypeID::JANITOR2, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Special | MonsterWaveType::Bomber, 26, 0.4f, {-96,-96,-66}, {96,96,62}, 0.4f}, // Scale 0.4
	{horde::MonsterTypeID::MEDIC_COMMANDER, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.3f,  {-24,-24,-24}, {24,24,32}, 1.0f},

	// Semi-Boss Units
	{horde::MonsterTypeID::MAKRON, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 23, 0.01f, {-30,-30,0}, {30,30,90}, 1.0f},
	{horde::MonsterTypeID::PERRO_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Fast | MonsterWaveType::Small, 20, 0.4f, {-16,-16,-24}, {16,16,24}, 1.0f},
	{horde::MonsterTypeID::WIDOW1, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 29, 0.3f, {-40,-40,0}, {40,40,144}, 0.6f}, // Scale 0.6
	{horde::MonsterTypeID::SHAMBLER_KL, MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 33, 0.23f,  {-32,-32,-24}, {32,32,64}, 1.0f},
	{horde::MonsterTypeID::GUNCMDR_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 33, 0.2f,  {-16,-16,-24}, {16,16,36}, 1.25f}, // Scale 1.25
	{horde::MonsterTypeID::MAKRON_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Elite, 41, 0.2f, {-30,-30,0}, {30,30,90}, 1.0f},
	{horde::MonsterTypeID::BOSS2_KL, MonsterWaveType::Flying | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 46, 0.2f, {-60,-60,0}, {60,60,90}, 0.6f}, // Scale 0.6
	{horde::MonsterTypeID::JORG_SMALL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Medium, 33, 0.4f, {-80,-80,0}, {80,80,140}, 0.35f}, // Scale 0.35

	// Boss Units (Included for completeness, scale might be set differently if spawned via specific boss logic)
	{horde::MonsterTypeID::BOSS2_64, MonsterWaveType::Flying | MonsterWaveType::Elite, 19, 0.2f,  {-60,-60,0}, {60,60,90}, 0.6f}, // Scale 0.6
	{horde::MonsterTypeID::BOSS2_MINI, MonsterWaveType::Flying | MonsterWaveType::Elite, 19, 0.2f,  {-60,-60,0}, {60,60,90}, 0.6f}, // Scale 0.6
	{horde::MonsterTypeID::CARRIER_MINI, MonsterWaveType::Flying | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.2f, {-56,-56,-44}, {56,56,44}, 0.6f} // Scale 0.6

	// Other Bosses (Assuming scale 1.0 unless specified otherwise in their SP_ functions)
	//{horde::MonsterTypeID::BOSS2, MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 19, 0.1f, {-60,-60,0}, {60,60,90}, 1.0f},
	//{horde::MonsterTypeID::CARRIER, MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 24, 0.1f, {-56,-56,-44}, {56,56,44}, 1.0f},
	//{horde::MonsterTypeID::FIXBOT_KL, MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 0, 0.4f, {-16,-16,-12}, {16,16,12}, 2.6f}, // Scale 2.6
	//{horde::MonsterTypeID::WIDOW, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 30, 0.1f, {-40,-40,0}, {40,40,144}, 1.0f},
	//{horde::MonsterTypeID::WIDOW2, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 19, 0.15f, {-40,-40,0}, {40,40,144}, 0.8f}, // Scale 0.8
	//{horde::MonsterTypeID::BOSS5, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 30, 0.1f, {-32,-32,-16}, {32,32,64}, 1.0f}, // Supertank
	//{horde::MonsterTypeID::JORG, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 30, 0.15f, {-80,-80,0}, {80,80,140}, 1.0f},
	//{horde::MonsterTypeID::PSX_GUARDIAN, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 25, 0.1f, {-32,-32,-24}, {32,32,64}, 1.0f}, // Assuming 1.0

	// Turret (Not typically spawned via this array, but included)
	//{horde::MonsterTypeID::TURRET, MonsterWaveType::Ground | MonsterWaveType::Special, 1, 0.0f, {-16,-16,-16}, {16,16,16}, 1.0f}, // Weight 0, likely not picked
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
	{horde::MonsterTypeID::WIDOW2, 19, -1, 0.15f, BossSizeCategory::Small, BossType::WIDOW2},
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
	{horde::MonsterTypeID::WIDOW2, 19, -1, 0.15f, BossSizeCategory::Medium, BossType::WIDOW2},
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
	{horde::MonsterTypeID::REDMUTANT, -1, 24, 0.1f, BossSizeCategory::Small, BossType::REDMUTANT},
	{horde::MonsterTypeID::WIDOW2, -1, 24, 0.19f, BossSizeCategory::Small, BossType::WIDOW2
}
};

// Optimized GetBossList function
static std::span<const boss_t> GetBossList(const horde::MapSize& mapSize, horde::MapID mapId) {
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
        if (!boss || count >= MAX_ELIGIBLE_BOSSES) { // Check if boss is null or array is full
            return false;
        }
        items[count] = boss; // Direct assignment
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
			horde::MapSize mapSize;
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

static edict_t* CreateBaseHordeMonster(horde::MonsterTypeID typeId, const vec3_t& origin, const vec3_t& angles) {
	// Convert TypeID to classname
	const char* classname = horde::MonsterTypeRegistry::GetClassname(typeId);
	if (!classname) {
		if (developer->integer) {
			gi.Com_PrintFmt("CreateBaseHordeMonster: Invalid TypeID provided.\n");
		}
		return nullptr;
	}

	// Create the entity
	edict_t* monster = G_Spawn();
	if (!monster) {
		if (developer->integer) {
			gi.Com_PrintFmt("CreateBaseHordeMonster: G_Spawn failed for {}\n", classname);
		}
		return nullptr;
	}

	// Set basic properties
	monster->classname = classname;
	monster->s.origin = origin;
	monster->s.angles = angles;

	// Apply common Horde flags automatically
	monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	monster->monsterinfo.last_sentrygun_target_time = 0_ms;
	monster->was_spawned_by_horde = true; // Mark as horde spawned

	// Mark specifically if in spawning state
	if (g_horde_local.state == horde_state_t::spawning) {
		monster->spawned_in_spawn_state = true;
	}

	// Return the initialized (but not yet fully spawned/linked) entity
	return monster;
}

edict_t* SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t& origin, const vec3_t& angles, bool applyHordeFlags) {
	const char* classname = horde::MonsterTypeRegistry::GetClassname(typeId);
	if (!classname) {
		if (developer->integer) {
			gi.Com_PrintFmt("SpawnMonsterByTypeID: Failed to get classname for TypeID {}\n", static_cast<int>(typeId));
		}
		return nullptr;
	}

	edict_t* monster = CreateBaseHordeMonster(typeId, origin, angles);
	if (!monster) {
		return nullptr;
	}

	monster->classname = classname;
	monster->s.origin = origin;
	monster->s.angles = angles;

	if (applyHordeFlags) {
		monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
		monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
		monster->monsterinfo.last_sentrygun_target_time = 0_ms;
		monster->was_spawned_by_horde = true;

		if (g_horde_local.state == horde_state_t::spawning) {
			monster->spawned_in_spawn_state = true;
		}
	}

	// Set non-solid for ED_CallSpawn
	monster->solid = SOLID_NOT;
	ED_CallSpawn(monster);

	// Check if ED_CallSpawn freed the entity
	if (!monster->inuse) {
		if (developer->integer) { // Added dev print for clarity
			gi.Com_PrintFmt("SpawnMonsterByTypeID: ED_CallSpawn failed for {}\n", monster->classname ? monster->classname : "Unknown");
		}
		return nullptr;
	}

	// Restore solidity and link
	monster->solid = SOLID_BBOX;
	gi.linkentity(monster);

	// *** Final Post-Link Stuck Check ***
	// Check against world geometry ONLY to avoid flagging stuck on other monsters at spawn
	trace_t post_link_trace = gi.trace(monster->s.origin, monster->mins, monster->maxs,
		monster->s.origin, monster, MASK_SOLID); // <-- CHANGED MASK

	bool spawn_was_stuck = post_link_trace.startsolid;

	// --- Final Stuck Handling ---
	if (spawn_was_stuck) {
		// Only flag if not already flagged by monster_start_go
		if (!monster->monsterinfo.was_stuck) {
			edict_t* blocker = post_link_trace.ent;
			if (developer->integer) {
				// Clarified log message
				gi.Com_PrintFmt("SpawnMonsterByTypeID: WARNING - {} stuck in GEOMETRY (Post-Link Check) at {}. Blocker: {} ({}). Flagging for teleport.\n",
					monster->classname, monster->s.origin,
					blocker ? (blocker->classname ? blocker->classname : "unknown") : "world/unknown",
					blocker ? (blocker - g_edicts) : -1);
			}
			monster->monsterinfo.was_stuck = true;
			monster->monsterinfo.stuck_check_time = level.time; // Start timer immediately
		}
		else if (developer->integer > 1) {
			// Already flagged by monster_start_go, just log confirmation
			gi.Com_PrintFmt("SpawnMonsterByTypeID: {} confirmed stuck (already flagged by monster_start_go).\n", monster->classname);
		}
	}

	monster->monsterinfo.spawn_complete_time = level.time;

	return monster;
}

// Optional overload with fewer parameters for common cases
edict_t* SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t& origin) {
	return SpawnMonsterByTypeID(typeId, origin, vec3_origin, true);
}


static horde::MonsterTypeID G_HordePickBOSSType(const horde::MapSize& mapSize, std::string_view mapname, int32_t waveNumber, edict_t* bossEntity) {
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

	WeightedBoss weightedBosses[MAX_ELIGIBLE_BOSSES]{}; // Ensure MAX_ELIGIBLE_BOSSES is large enough
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

		// --- CORRECTED CHECK: Use contains(MonsterTypeID) ---
		if (recent_bosses.contains(bossTypeId)) {
			continue;
		}
		// --- END CORRECTION ---

		// Find the boss_t entry with this typeId (only if not recently used)
		const boss_t* boss = nullptr;
		for (const auto& b : boss_list) {
			if (b.typeId == bossTypeId) {
				boss = &b;
				break;
			}
		}

		if (!boss) { // Safety check
			continue;
		}

		// Calculate weight with optimized factors
		float weight = boss->weight;

		// Combined weight adjustment with fewer branches
		if (waveNumber >= boss->min_level && boss->min_level != -1) {
			if (waveNumber <= boss->min_level + 5) weight *= 1.3f;
			else if (waveNumber > boss->min_level + 10) weight *= 0.8f;
		}

		// Apply difficulty adjustment in one check
		if (((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) &&
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
	} // End of first pass loop

	// If no eligible bosses with history filter, retry without filter
	if (weightedCount == 0 && recent_bosses.count > 0) {
		if (developer->integer > 1) {
			gi.Com_PrintFmt("INFO: No non-recent bosses eligible, resetting history and retrying...\n");
		}
		recent_bosses.clear(); // Reset history

		// Try all bosses from the list based on eligibility data again (since we already filtered by level)
		for (size_t i = 0; i < eligibilityData.count; ++i) {
			horde::MonsterTypeID bossTypeId = eligibilityData.typeIds[i];
			if (bossTypeId == horde::MonsterTypeID::UNKNOWN) continue;

			// Find the boss_t entry again
			const boss_t* boss = nullptr;
			for (const auto& b : boss_list) {
				if (b.typeId == bossTypeId) {
					boss = &b;
					break;
				}
			}
			if (!boss) continue;

			float weight = boss->weight; // Use base weight again

			// Re-apply adjustments if necessary (e.g., difficulty) - copy from above
			if (((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) &&
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
	}

	// Still no eligible bosses? Return unknown
	if (weightedCount == 0 || totalWeight <= 0.0f) {
		if (developer->integer) {
			gi.Com_PrintFmt("WARNING: No eligible bosses found even after potential history reset.\n");
		}
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Select boss with binary search for better performance
	const float randomValue = frandom() * totalWeight;

	// Binary search through accumulated weights
	size_t left = 0;
	size_t right = weightedCount - 1;

	// // Handle edge case: only one item
	// if (left == right) {
	// 	// Directly select the only item
	// }
	// else
	 {
		// Perform binary search
		while (left < right) {
			const size_t mid = left + (right - left) / 2; // Safer midpoint calculation
			if (weightedBosses[mid].cumulativeWeight < randomValue) {
				left = mid + 1;
			}
			else {
				right = mid;
			}
		}
	}


	// Get selected boss
	const boss_t* chosen_boss = weightedBosses[left].boss;
	if (chosen_boss) {
		// --- CORRECTED ADD: Use add(MonsterTypeID) ---
		recent_bosses.add(chosen_boss->typeId);
		// --- END CORRECTION ---

		// Set boss size category for the entity
		bossEntity->bossSizeCategory = chosen_boss->sizeCategory;

		if (developer->integer > 1) {
			const char* chosen_name = horde::MonsterTypeRegistry::GetClassname(chosen_boss->typeId);
			gi.Com_PrintFmt("Selected Boss: {} (Weight: {:.2f})\n",
				chosen_name ? chosen_name : "Unknown", weightedBosses[left].weight);
		}

		// Return the TypeID directly
		return chosen_boss->typeId;
	}

	// Should ideally not happen if weightedCount > 0, but return UNKNOWN as a fallback
	return horde::MonsterTypeID::UNKNOWN;
}

struct picked_item_t {
	const weighted_item_t* item;
	float weight;
};

// Adapted Caching Structure (using std::array for fixed size based on input)
struct HordeItemSelectionCache {
	// Automatically size based on the actual data array
	static constexpr size_t MAX_ENTRIES = std::size(hordeItemData);
	struct Entry {
		const HordeItemInfo* itemInfo; // Pointer to the HordeItemInfo entry
		float weight;
		float cumulative_weight;
	};

	size_t count = 0;
	float total_weight = 0.0f;
	// Use std::array for compile-time sized array based on hordeItemData
	std::array<Entry, MAX_ENTRIES> entries{}; // Value-initialize

	void clear() noexcept {
		count = 0;
		total_weight = 0.0f;
		// No need to clear array elements explicitly when count = 0
	}
};
// Static cache instance specifically for Horde item selection
static HordeItemSelectionCache horde_item_cache;

// Modified Function using HordeItemInfo and item_id_t
gitem_t* G_HordePickItem() {
	// Reset cache for this selection attempt
	horde_item_cache.clear();

	// Use std::span for safe iteration over the hordeItemData array
	std::span<const HordeItemInfo> items_view{ hordeItemData };

	// --- Collect Eligible Items ---
	for (const auto& hordeItemInfo : items_view) {
		// Check if cache is full (safety check, should not happen with std::array)
		if (horde_item_cache.count >= HordeItemSelectionCache::MAX_ENTRIES) {
			if (developer->integer) { // Log error if this happens unexpectedly
				gi.Com_PrintFmt("Warning: HordeItemSelectionCache full! Increase MAX_ENTRIES if not using std::array.\n");
			}
			break;
		}

		// Filter based on minimum wave level required for the item
		if (g_horde_local.level >= hordeItemInfo.minWave) {

			// Use the weight directly from HordeItemInfo
			// Future Enhancements: Add more complex weight adjustments here
			// (e.g., based on player count, current inventory, game state)
			float adjusted_weight = hordeItemInfo.weight;

			// Ensure weight is positive before adding to the cache
			if (adjusted_weight > 0.0f) {
				horde_item_cache.total_weight += adjusted_weight;
				// Get reference to the next entry in the cache array
				auto& entry = horde_item_cache.entries[horde_item_cache.count];
				entry.itemInfo = &hordeItemInfo; // Store pointer to the HordeItemInfo struct
				entry.weight = adjusted_weight;
				entry.cumulative_weight = horde_item_cache.total_weight;
				horde_item_cache.count++; // Increment the count of eligible items
			}
		}
	} // End of item collection loop

	// Check if any eligible items were found
	if (horde_item_cache.count == 0 || horde_item_cache.total_weight <= 0.0f) {
		// Log if no items found (useful for debugging balance/data issues)
		if (developer->integer) {
			gi.Com_PrintFmt("Warning: G_HordePickItem found no eligible items for wave {}.\n", g_horde_local.level);
		}
		return nullptr; // No items eligible or they all have zero/negative weight
	}

	// --- Weighted Random Selection ---
	// Generate a random floating-point value within the total weight range
	const float random_value = frandom() * horde_item_cache.total_weight;

	// Use binary search to efficiently find the selected item based on cumulative weight
	size_t left = 0;
	size_t right = horde_item_cache.count - 1;

	// Handle edge case: only one eligible item
	// if (left == right) {
	// 	// 'left' (or 'right') is the index of the only choice
	// }
	// else 
	{
		// Perform binary search
		while (left < right) {
			// Calculate midpoint safely to avoid potential overflow with large indices
			const size_t mid = left + (right - left) / 2;
			if (horde_item_cache.entries[mid].cumulative_weight < random_value) {
				left = mid + 1; // Random value is in the upper half
			}
			else {
				right = mid; // Random value is in the lower half (including mid)
			}
		}
	}
	// After the loop, 'left' holds the index of the chosen item entry in the cache

	// *** ADDED SAFETY CHECK ***
	// Although the binary search should ensure 'left' is valid, this check
	// satisfies static analysis and guards against potential edge cases.
	if (left >= horde_item_cache.count) {
		if (developer->integer) {
			gi.Com_PrintFmt("CRITICAL ERROR: G_HordePickItem - Binary search resulted in invalid index 'left' ({}) >= 'count' ({}).\n",
				left, horde_item_cache.count);
		}
		return nullptr; // Prevent accessing invalid memory
	}
	// *** END SAFETY CHECK ***


	// --- Retrieve and Return the Item ---
	// Get the chosen HordeItemInfo pointer from the cache entry
	const HordeItemInfo* chosen_info = horde_item_cache.entries[left].itemInfo; // Now accessing with checked 'left'

	// Safety check: Ensure chosen_info is not null (shouldn't happen if count > 0 and check above passed)
	if (!chosen_info) {
		if (developer->integer) {
			// This would indicate an error storing the pointer earlier, less likely
			gi.Com_PrintFmt("Error: G_HordePickItem - chosen_info is null despite valid index {}.\n", left);
		}
		return nullptr;
	}

	// Use the item_id_t from the chosen info to get the actual gitem_t pointer
	// This replaces the old FindItemByClassname call
	return GetItemByIndex(chosen_info->id);
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

//-----------------------------------------------------
// G_HordePickMonsterType - Selects monster based on wave,
//                          spawn point, and retaliation state.
//-----------------------------------------------------
static horde::MonsterTypeID G_HordePickMonsterType(edict_t* spawn_point) {
	static constexpr size_t MONSTER_CACHE_SIZE = std::size(monsterTypes);

	// Cache for weighted selection this call
	static struct {
		struct Entry {
			horde::MonsterTypeID typeId;
			float weight;
			float cumulative_weight;
		};
		std::array<Entry, MONSTER_CACHE_SIZE> entries{};
		size_t count;
		float total_weight;
		void clear() noexcept {
			count = 0;
			total_weight = 0.0;
		}
	} monster_cache;


	// Track consecutive failures for emergency handling
	static int spawn_selection_failures = 0;
	static MonsterWaveType saved_wave_type = MonsterWaveType::None; // Used for older emergency mode, might be less relevant now
	static bool emergency_mode = false; // Legacy emergency mode flag

	// Reset cache counters for this call
	monster_cache.clear(); // Use the clear method

	// Early validation for quick exit
	if (!spawn_point || !spawn_point->inuse) {
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Check spawn point availability (cooldowns)
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
	// Also check alternative cooldown
	if (level.time < data.alternative_cooldown) {
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN;
	}


	// Check if spawn point is occupied (by player)
	// Note: IsSpawnPointOccupied checks player occupation. Non-player obstacles are handled later.
	if (IsSpawnPointOccupied(spawn_point)) {
		IncreaseSpawnAttempts(spawn_point); // Penalize point for being blocked
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Cache commonly used values
	const int32_t currentLevel = g_horde_local.level;
	MonsterWaveType currentWaveTypes = current_wave_type; // Base wave type
	const bool isSpawnPointFlying = spawn_point->style == 1;
	const bool retaliation_active = g_horde_retaliation_active; // Cache retaliation state

	// Define the desired types for retaliation weighting
	constexpr MonsterWaveType RETALIATION_FOCUS_TYPES = MonsterWaveType::Spawner | MonsterWaveType::Bomber | MonsterWaveType::Special;


	// Enter legacy emergency mode after multiple failures (kept for fallback, but retaliation mode is primary response now)
	if (spawn_selection_failures >= 3 && !emergency_mode && !retaliation_active) { // Don't enter legacy if retaliation is active
		if (developer->integer) {
			gi.Com_PrintFmt("WARNING: Entering LEGACY emergency monster selection mode after {} failures\n",
				spawn_selection_failures);
		}
		if (saved_wave_type == MonsterWaveType::None && currentWaveTypes != MonsterWaveType::None) {
			saved_wave_type = currentWaveTypes;
		}
		if (HasWaveType(currentWaveTypes, MonsterWaveType::Flying)) {
			currentWaveTypes = MonsterWaveType::Flying;
		}
		else {
			currentWaveTypes = MonsterWaveType::Ground;
		}
		emergency_mode = true;
	}

	// In severe failure cases (legacy), disable wave type filtering completely
	if (spawn_selection_failures >= 6 && emergency_mode) { // Only in legacy mode
		if (developer->integer && currentWaveTypes != MonsterWaveType::None) {
			gi.Com_PrintFmt("CRITICAL (Legacy): Disabling wave type filtering after {} failures\n",
				spawn_selection_failures);
		}
		currentWaveTypes = MonsterWaveType::None;
	}

	// Determine if we should spawn a higher level monster (kept from original logic)
	bool spawn_higher_level = false;
	if (!retaliation_active) { // Don't spawn higher level during retaliation
		if (currentLevel <= 10) {
			spawn_higher_level = frandom() < 0.16f;
		}
		else if (currentLevel <= 15) {
			spawn_higher_level = frandom() < 0.05f;
		}
		else {
			spawn_higher_level = frandom() < 0.02f;
		}
	}
	if (emergency_mode) { // Disable in legacy emergency mode too
		spawn_higher_level = false;
	}

	// Calculate effective level for monster selection
	int32_t effectiveLevel = currentLevel;
	if (spawn_higher_level) {
		int32_t levelBoost;
		if (currentLevel < 7) levelBoost = irandom(2, 4);
		else if (currentLevel <= 10) levelBoost = irandom(2, 3);
		else levelBoost = irandom(1, 2);
		int32_t maxLevel = currentLevel < 7 ? irandom(5, 7) :
			(currentLevel < 15 ? currentLevel + 5 : currentLevel + 3);
		effectiveLevel = std::min(currentLevel + levelBoost, maxLevel);
		effectiveLevel = std::min(effectiveLevel, 40);

		// Check eligibility at higher level (as before)
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
		if (!any_eligible_monsters) {
			if (developer->integer) gi.Com_PrintFmt("WARNING: No eligible monsters at higher level {}. Reverting to current wave {}\n", effectiveLevel, currentLevel);
			effectiveLevel = currentLevel;
		}
	}

	// Skip boss wave checks (as before) - maybe allow minions during retaliation? For now, keep skip.
	if (!retaliation_active && currentWaveTypes == MonsterWaveType::None &&
		currentLevel >= 10 && currentLevel % 5 == 0 && !boss_spawned_for_wave) {
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN; // Cannot pick regular monster when boss hasn't spawned
	}

	// Pre-compute flying spawn adjustment (as before)
	const int32_t flyingSpawns = countFlyingSpawns();
	const float adjustmentFactor = adjustFlyingSpawnProbability(flyingSpawns);

	// Build monster cache - iterate through all defined monster types
	for (const auto& monster : monsterTypes) {
		if (monster_cache.count >= MONSTER_CACHE_SIZE) break; // Safety break if cache somehow overflows

		// Basic level check against effective level
		if (monster.minWave > effectiveLevel) continue;

		// Skip if not valid for current wave type (using potentially simplified type from legacy mode)
		if (currentWaveTypes != MonsterWaveType::None &&
			!IsValidMonsterForWave(monster.typeId, currentWaveTypes)) {
			continue;
		}

		// Flying compatibility check - crucial
		const bool monster_is_flying = IsFlying(monster.typeId);
		if (isSpawnPointFlying && !monster_is_flying) continue; // Flying point needs flying monster
		// Optional: Ground point + Flying monster check (less critical unless you want strict ground waves)
		// if (!isSpawnPointFlying && monster_is_flying && !HasWaveType(currentWaveTypes, MonsterWaveType::Flying)) continue;

		// Calculate base weight
		float weight = monster.weight;

		// Apply level-based adjustments (as before)
		if (currentLevel <= 5) { if (!HasWaveType(monster.types, MonsterWaveType::Light)) weight *= 0.3f; }
		else if (currentLevel <= 10) { if (!HasWaveType(monster.types, MonsterWaveType::Light | MonsterWaveType::Small)) weight *= 0.4f; }
		else if (currentLevel <= 15) { if (!HasWaveType(monster.types, MonsterWaveType::Medium)) weight *= 0.5f; }
		else {
			if (monster_is_flying) weight *= 0.8f; // Less penalty for flyers
			else if (!HasWaveType(monster.types, MonsterWaveType::Heavy | MonsterWaveType::Elite)) weight *= 0.6f;
			else weight *= 1.0f + ((currentLevel - 15) * 0.02f); // Slight boost for late-game relevant types
		}

		// Ensure earlier monsters don't become too rare (as before)
		if (monster.minWave < currentLevel) {
			float relevance = 1.0f - std::min(0.4f, (currentLevel - monster.minWave) * 0.01f);
			weight *= relevance;
		}
		// Ensure minimum weight (as before)
		if (weight < 0.2f && monster.weight >= 0.2f) weight = 0.2f;

		// In legacy emergency mode, make weights more uniform (as before)
		if (emergency_mode) weight = std::max(weight, 0.5f);

		// Special handling for boss wave minions (as before)
		if (currentLevel >= 10 && currentLevel % 5 == 0 && boss_spawned_for_wave) {
			weight *= HasWaveType(monster.types, currentWaveTypes) ? 2.0f : 0.3f;
		}

		// Apply difficulty adjustments (as before)
		if (g_insane->integer || g_chaotic->integer) {
			const float difficultyScale = currentLevel <= 10 ? 1.1f : currentLevel <= 20 ? 1.2f : currentLevel <= 30 ? 1.3f : 1.4f;
			weight *= difficultyScale;
			if (HasWaveType(monster.types, MonsterWaveType::Elite)) weight *= 1.2f;
		}

		// Apply flying adjustment (as before)
		weight *= adjustmentFactor;

		// Higher level monster adjustment (as before)
		if (spawn_higher_level && monster.minWave > currentLevel) {
			if (monster.minWave - currentLevel > 5) weight *= 0.5f;
		}

		// --- Apply Retaliation Weighting ---
		if (retaliation_active) {
			MonsterWaveType monster_flags = GetMonsterWaveTypes(monster.typeId);
			if (HasWaveType(monster_flags, RETALIATION_FOCUS_TYPES)) {
				weight *= 1.8f; // Boost weight significantly for focus types (Spawner, Bomber, Special)
				if (developer->integer > 2) gi.Com_PrintFmt("Retaliation Weight: Boosting {} (Weight: %.2f)\n", horde::MonsterTypeRegistry::GetClassname(monster.typeId), weight);
			}
			else {
				// Slightly reduce weight of non-focus types during retaliation
				weight *= 0.7f;
				if (developer->integer > 2) gi.Com_PrintFmt("Retaliation Weight: Reducing non-focus {} (Weight: %.2f)\n", horde::MonsterTypeRegistry::GetClassname(monster.typeId), weight);
			}
		}
		// --- End Retaliation Weighting ---

		// Add to cache if weight is valid
		if (weight > 0.0f) {
			monster_cache.total_weight += weight;
			auto& entry = monster_cache.entries[monster_cache.count];
			entry.typeId = monster.typeId;
			entry.weight = weight;
			entry.cumulative_weight = monster_cache.total_weight;
			monster_cache.count++;
		}
	} // End monster type iteration loop

	// --- Handle Selection or Fallback ---
	horde::MonsterTypeID chosen_monster = horde::MonsterTypeID::UNKNOWN;
	if (monster_cache.count == 0) {
		// --- Emergency Fallback Logic (if no monsters fit criteria) ---
		if (developer->integer) gi.Com_PrintFmt("G_HordePickMonsterType: No eligible monsters found in cache, attempting fallback...\n");
		WeightedSelection<MonsterTypeInfo> fallback_monsters;
		int best_min_wave = 1;
		for (const auto& monster : monsterTypes) { if (monster.minWave <= currentLevel && monster.minWave > best_min_wave) best_min_wave = monster.minWave; }
		int min_acceptable_wave = std::max(1, best_min_wave - 5);
		for (const auto& monster : monsterTypes) {
			if (monster.minWave >= min_acceptable_wave && monster.minWave <= currentLevel) {
				const bool isFlyingMonster = HasWaveType(GetMonsterWaveTypes(monster.typeId), MonsterWaveType::Flying);
				if (!(isSpawnPointFlying && !isFlyingMonster)) { // Basic flying check
					float fallback_weight = 1.0f + ((monster.minWave - min_acceptable_wave) * 0.5f);
					fallback_monsters.add(&monster, fallback_weight);
				}
			}
		}
		if (fallback_monsters.item_count > 0) {
			const MonsterTypeInfo* selected = fallback_monsters.select();
			if (selected) chosen_monster = selected->typeId;
		}
		// Last resort if still nothing
		if (chosen_monster == horde::MonsterTypeID::UNKNOWN) {
			for (const auto& monster : monsterTypes) {
				if (monster.minWave <= currentLevel) {
					const bool isFlyingMonster = HasWaveType(GetMonsterWaveTypes(monster.typeId), MonsterWaveType::Flying);
					if (!(isSpawnPointFlying && !isFlyingMonster)) {
						chosen_monster = monster.typeId; break;
					}
				}
			}
		}
		if (chosen_monster != horde::MonsterTypeID::UNKNOWN && developer->integer) {
			gi.Com_PrintFmt("EMERGENCY FALLBACK PICK: Selected {} after {} failed attempts\n",
				horde::MonsterTypeRegistry::GetClassname(chosen_monster), spawn_selection_failures);
		}
		// --- End Emergency Fallback Logic ---

		// If we found a monster via emergency fallback
		if (chosen_monster != horde::MonsterTypeID::UNKNOWN) {
			// Update cooldowns for the spawn point that led to this fallback
			UpdateCooldowns(spawn_point);
			// Reset failure counters on success
			spawn_selection_failures = 0;
			emergency_mode = false; // Reset legacy mode
			if (saved_wave_type != MonsterWaveType::None) { current_wave_type = saved_wave_type; saved_wave_type = MonsterWaveType::None; } // Restore original type if needed
			// Retaliation mode times out on its own, no need to reset here
			return chosen_monster;
		}

		// No monster available even with emergency fallback
		IncreaseSpawnAttempts(spawn_point); // Penalize the point
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN; // Truly failed to pick anything

	}
	else {
		// --- Select monster from cache using binary search ---
		const float random_value = frandom() * monster_cache.total_weight;
		size_t left = 0;
		size_t right = monster_cache.count - 1;
		while (left < right) {
			const size_t mid = left + (right - left) / 2; // Safer midpoint
			if (monster_cache.entries[mid].cumulative_weight < random_value) left = mid + 1;
			else right = mid;
		}
		// 'left' now holds the index of the chosen monster
		chosen_monster = monster_cache.entries[left].typeId;
		// --- End Selection ---
	}


	// --- Final Steps for Successful Pick ---
	if (chosen_monster != horde::MonsterTypeID::UNKNOWN) {
		UpdateCooldowns(spawn_point); // Apply cooldown to the used spawn point
		// Reset failure counters on success
		spawn_selection_failures = 0;
		emergency_mode = false; // Reset legacy mode
		if (saved_wave_type != MonsterWaveType::None) { current_wave_type = saved_wave_type; saved_wave_type = MonsterWaveType::None; } // Restore original type if needed
		// Retaliation mode times out on its own, no need to reset here
	}
	else {
		// This path should theoretically not be reached if cache had entries, but as safety:
		IncreaseSpawnAttempts(spawn_point); // Penalize if something went wrong
		spawn_selection_failures++;
	}

	return chosen_monster;
}
void Horde_PreInit() {
	gi.Com_Print("Horde mode must be DM. Set <deathmatch 1> and <horde 1>, then <map mapname>.\n");
	gi.Com_Print("COOP requires <coop 1> and <horde 0>.\n");

	g_horde = gi.cvar("horde", "0", CVAR_LATCH);
	//gi.Com_Print("After starting a normal server type: starthorde to start a game.\n");


	if (!g_horde->integer) { // If horde mode is OFF (0)
		// Ensure g_hardcoop is registered if it isn't already (safe check)
		// Assuming g_hardcoop is already declared in g_local.h and registered in InitGame
		// We just need to set its value here.
		gi.cvar_set("g_hardcoop", "1"); // Force hardcoop ON
		gi.cvar_forceset("deathmatch", "0"); // Keep this line
		return; // Exit Horde_PreInit early if horde mode is off
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
		const horde::MapSize mapSize = GetMapSize(static_cast<const char*>(level.mapname));
		const int32_t spectPlayers = GetNumSpectPlayers();
		const int32_t baseBots = mapSize.isBigMap ? 6 : 4;

		// Agregar bot extra si current_wave_level >= 20
		const int32_t extraBot = (current_wave_level >= 20) ? 1 : 0;
		const int32_t requiredBots = std::max(baseBots + spectPlayers + extraBot, baseBots);

		gi.cvar_set("bot_minClients", std::to_string(requiredBots).c_str());
	}
}

void InitializeWaveSystem() noexcept;

// Guard variable
static bool items_precached = false;

// Renamed function for clarity
static void PrecacheAllGameItems() noexcept {
	// Only precache once
	if (items_precached)
		return;

	if (developer->integer) {
		gi.Com_Print("Precaching all game items...\n");
	}

	// Iterate directly through the global itemlist using item_id_t
	// Start from IT_NULL + 1 because index 0 is unused.
	for (item_id_t i = static_cast<item_id_t>(IT_NULL + 1); i < IT_TOTAL; i = static_cast<item_id_t>(i + 1))
	{
		// Get the gitem_t pointer directly using the index (ID)
		gitem_t* item = &itemlist[i]; // Or use GetItemByIndex(i) if preferred

		// Basic validation: Skip if item pointer is null (shouldn't happen)
		// or if it lacks a classname (often indicates an internal/placeholder item)
		if (!item || !item->classname) {
			continue;
		}

		// Call the existing PrecacheItem function, which handles all necessary assets
		// associated with this gitem_t (models, sounds, icons, nested precaches).
		PrecacheItem(item);
	}

	items_precached = true; // Mark as done

	if (developer->integer) {
		gi.Com_Print("Item precaching complete.\n");
	}
}

// Función para precarga de sonidos
static bool sounds_precached = false;

static void PrecacheWaveSounds() noexcept {
	if (sounds_precached)
		return;

	static const std::array<std::pair<cached_soundindex*, const char*>, 4> individual_sounds = { {
		{&sound_tele3, "misc/r_tele3.wav"},
		{&sound_tele_up, "misc/tele_up.wav"},
		{&sound_spawn1, "misc/spawn1.wav"},
		{&incoming, "world/incoming.wav"}
	} };

	// Use std::span for safe iteration
	std::span individual_view{ individual_sounds };
	for (const auto& [sound_index, path] : individual_view) {
		sound_index->assign(path);
	}

	std::span wave_view{ WAVE_SOUND_PATHS };
	for (size_t i = 0; i < NUM_WAVE_SOUNDS; ++i) {
		wave_sounds[i].assign(wave_view[i]);
	}

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

void ResetBosses()
{
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end(); /* no increment here */) {
		edict_t* boss = *it;
		// CRITICAL CHECK: Ensure the pointer is not null AND the entity is still in use
		// before attempting to access or free it.
		if (boss && boss->inuse) {
			// Ensure boss death logic is finalized if needed, then free
			if (!boss->monsterinfo.BOSS_DEATH_HANDLED) {
				// Optional: Add a check here if boss->die is valid before calling
				// if (boss->die.pointer()) { boss->die(boss, boss, boss, 0, boss->s.origin, mod_t{}); }
				// Or just call the handler which should be safe
				BossDeathHandler(boss); // Attempt final cleanup if not done
			}
			OnEntityRemoved(boss); // Call removal hook
			G_FreeEdict(boss);     // Free the entity
		}
		// Erase the pointer from the set regardless of whether it was valid/freed,
		// then advance the iterator safely.
		it = auto_spawned_bosses.erase(it);
	}
}


void Horde_Init() {
	horde::InitializeHordeIDs();

	// Reset precache state
	sounds_precached = false;
	items_precached = false;

	ResetBosses();

	// Do precaching
	PrecacheAllGameItems();
	PrecacheWaveSounds();

	// Initialize wave types
	InitializeMonsterWaveTypes();
	InitializeWaveSystem();
	last_wave_number = 0;

	AllowReset();
	ResetGame();

	gi.Com_Print("PRINT: Horde game state initialized with all necessary resources precached.\n");
}

// Helper function to select a suitable boss weapon drop based on wave level
static item_id_t SelectBossWeaponDrop(int32_t wave_level) {
	// Define potential weapon drops with their minimum required wave level
	static const std::array<std::pair<item_id_t, int32_t>, 8> boss_weapon_drops = { {
		{IT_WEAPON_HYPERBLASTER, 9},
		{IT_WEAPON_RLAUNCHER,    9},
		{IT_WEAPON_PHALANX,      9}, // Xatrix
		{IT_WEAPON_IONRIPPER,    9}, // Xatrix (originally boomer)
		{IT_WEAPON_PLASMABEAM,   9}, // Rogue
		{IT_WEAPON_RAILGUN,      9}, // Moved slightly later
		{IT_WEAPON_DISRUPTOR,    19}, // Rogue (originally disintegrator)
		{IT_WEAPON_BFG,          19}  // Moved slightly later
	} };

	// Collect weapons eligible for the current wave level
	std::vector<item_id_t> eligible_weapons;
	eligible_weapons.reserve(boss_weapon_drops.size()); // Pre-allocate memory

	for (const auto& [weapon_id, min_level] : boss_weapon_drops) {
		if (wave_level >= min_level) {
			eligible_weapons.push_back(weapon_id);
		}
	}

	// If no weapons are eligible, return IT_NULL
	if (eligible_weapons.empty()) {
		return IT_NULL;
	}

	// Select a random weapon from the eligible list
	// Use a more robust random number generator if possible, otherwise fallback
	// std::uniform_int_distribution<size_t> dist(0, eligible_weapons.size() - 1);
	// size_t random_index = dist(mt_rand); // Requires <random> and mt_rand setup
	size_t random_index = irandom(eligible_weapons.size());
	// Safety check (shouldn't be needed with correct random range)
	if (random_index >= eligible_weapons.size()) {
		return IT_NULL;
	}

	return eligible_weapons[random_index];
}


// --- Modified BossDeathHandler using item_id_t ---

// Constants for item dropping physics (if not defined globally)
constexpr int MIN_VELOCITY = -800;
constexpr int MAX_VELOCITY = 800;
constexpr int MIN_VERTICAL_VELOCITY = 400;
constexpr int MAX_VERTICAL_VELOCITY = 950;

void BossDeathHandler(edict_t* boss) {
	// Fast early-out with combined validation (no change needed)
	if (!g_horde || !g_horde->integer || !boss || !boss->inuse || !boss->monsterinfo.IS_BOSS ||
		boss->monsterinfo.BOSS_DEATH_HANDLED || boss->health > 0) {
		return;
	}

	// Mark as handled immediately (no change needed)
	boss->monsterinfo.BOSS_DEATH_HANDLED = true;


	boss_spawned_for_wave = false;
	if (developer->integer) { // Optional debug print
		gi.Com_PrintFmt("BossDeathHandler: Reset boss_spawned_for_wave flag for wave {}.\n", g_horde_local.level);
	}


	// Clean up entity tracking (no change needed)
	OnEntityDeath(boss);
	OnEntityRemoved(boss);
	auto_spawned_bosses.erase(boss); // Ensure this set is accessible

	// --- Use item_id_t for static drop tables ---
	static const std::array<item_id_t, 8> standardItemIDs = {
		IT_ITEM_ADRENALINE, IT_ITEM_PACK, IT_ITEM_SENTRYGUN,
		IT_ITEM_SPHERE_DEFENDER, IT_ARMOR_COMBAT, IT_ITEM_BANDOLIER,
		IT_ITEM_INVULNERABILITY, IT_AMMO_NUKE
	};
	// Note: Ensure the size `8` matches the number of items listed.

	// --- Drop primary weapon using item_id_t ---
	item_id_t weapon_id = SelectBossWeaponDrop(current_wave_level);
	if (weapon_id != IT_NULL) { // Check against IT_NULL
		if (gitem_t* weapon_item = GetItemByIndex(weapon_id)) { // Use GetItemByIndex
			if (edict_t* weapon = Drop_Item(boss, weapon_item)) {
				// Set up weapon visuals/physics (logic remains the same)
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

	// --- Drop special power-up using item_id_t ---
	item_id_t special_id = brandom() ? IT_ITEM_QUADFIRE : IT_ITEM_QUAD; // Select ID
	if (gitem_t* special_item = GetItemByIndex(special_id)) { // Use GetItemByIndex
		if (edict_t* powerup = Drop_Item(boss, special_item)) {
			// Set up powerup visuals/physics (logic remains the same)
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
			powerup->spawnflags = SPAWNFLAG_ITEM_DROPPED_PLAYER;
			gi.linkentity(powerup);
		}
	}

	// --- Randomize and drop standard items using item_id_t ---
	std::array<item_id_t, standardItemIDs.size()> shuffledIDs = standardItemIDs; // Create a mutable copy

	// Shuffle the IDs (using simple irandom-based shuffle as fallback)
	for (size_t i = shuffledIDs.size() - 1; i > 0; --i) {
		size_t j = irandom(0, i); // irandom needs to be inclusive [0, i]
		if (i != j) {
			std::swap(shuffledIDs[i], shuffledIDs[j]);
		}
	}
	// For a better shuffle, use std::shuffle with a proper random engine if available:
	// std::shuffle(shuffledIDs.begin(), shuffledIDs.end(), mt_rand); // Requires setup

	// Drop the shuffled standard items
	for (item_id_t item_id : shuffledIDs) { // Iterate through shuffled IDs
		if (item_id == IT_NULL) continue; // Safety check

		if (gitem_t* item = GetItemByIndex(item_id)) { // Use GetItemByIndex
			if (edict_t* drop = Drop_Item(boss, item)) {
				// Set up standard drop visuals/physics (logic remains the same)
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

	// Clean up boss entity (no change needed)
	// Setting solid/takedamage might interfere with death animations
	gi.linkentity(boss); // Re-link just in case state needs update
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
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::BOSS5)] = "***** Boss incoming! Super-Tank is here to show it's might! *****\n";
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

// --- REVISED CheckAndTeleportBoss ---
bool CheckAndTeleportBoss(edict_t* self, BossTeleportReason reason = BossTeleportReason::DROWNING) {
	PROFILE_SCOPE("CheckAndTeleportBoss");
	if (level.intermissiontime) {
		// if (developer->integer > 1) gi.Com_PrintFmt("CTB: Early exit - level.intermissiontime.\n");
		return false;
	}

	if (!self) {
		// if (developer->integer) gi.Com_PrintFmt("CTB: Early exit - self is null.\n");
		return false;
	}
	if (!self->inuse || self->deadflag || !self->monsterinfo.IS_BOSS || !g_horde || !g_horde->integer) {
		if (developer->integer > 1) {
			gi.Com_PrintFmt("CTB: Early exit for {}. InUse:{} Dead:{} IsBoss:{} HordeInt:{}\n",
				self->classname ? self->classname : "NO_CLASSNAME",
				self->inuse, self->deadflag, self->monsterinfo.IS_BOSS,
				g_horde ? g_horde->integer : -1);
		}
		return false;
	}

	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
	if (typeId == horde::MonsterTypeID::MISC_INSANE || typeId == horde::MonsterTypeID::TURRET) {
		// if (developer->integer > 1) gi.Com_PrintFmt("CTB: Early exit - {} is misc_insane or turret.\n", self->classname);
		return false;
	}

	static std::string last_map_name_boss_teleport;
	static horde::MapID cached_map_id_boss_teleport = horde::MapID::UNKNOWN;
	const char* current_map = GetCurrentMapName();

	if (last_map_name_boss_teleport != current_map) {
		last_map_name_boss_teleport = current_map;
		cached_map_id_boss_teleport = horde::MapOriginRegistry::GetMapID(current_map);
	}
	if (cached_map_id_boss_teleport == horde::MapID::UNKNOWN) {
		if (developer->integer > 1) gi.Com_PrintFmt("CTB: MapID unknown for {}, cannot get teleport origin.\n", current_map);
		return false;
	}

	vec3_t destination_origin;
	if (!horde::MapOriginRegistry::GetOrigin(cached_map_id_boss_teleport, destination_origin)) {
		if (developer->integer > 1) gi.Com_PrintFmt("CTB: Failed to get MapOriginRegistry origin for map_id {}.\n", (int)cached_map_id_boss_teleport);
		return false;
	}
	if (!is_valid_vector(destination_origin) || destination_origin == vec3_origin) {
		if (developer->integer > 1) gi.Com_PrintFmt("CTB: Invalid destination_origin ({},{},{}) from MapOriginRegistry.\n", destination_origin.x, destination_origin.y, destination_origin.z);
		return false;
	}
	vec3_t destination_angles = self->s.angles;
	destination_angles.x = 0;
	destination_angles.z = 0;
    constexpr gtime_t TRIGGER_HURT_RETRIGGER_COOLDOWN = 0.1_sec;
	constexpr gtime_t DROWNING_COOLDOWN_BOSS = 1_sec;
	const gtime_t selected_trigger_cooldown = (reason == BossTeleportReason::DROWNING) ? DROWNING_COOLDOWN_BOSS : TRIGGER_HURT_RETRIGGER_COOLDOWN; // Assuming TRIGGER_HURT_RETRIGGER_COOLDOWN is defined

	static gtime_t last_boss_teleport_attempt_time[MAX_EDICTS] = {};
	const int boss_edict_num = self - g_edicts;

	if (level.time < last_boss_teleport_attempt_time[boss_edict_num] + selected_trigger_cooldown) {
		if (developer->integer > 1) {
            // Print components of gtime_t or its .seconds() value
            gtime_t cooldown_remaining = (last_boss_teleport_attempt_time[boss_edict_num] + selected_trigger_cooldown) - level.time;
            gi.Com_PrintFmt("CTB: Boss {} on REASON-specific cooldown. Remaining: {:.2f}s\n",
                self->classname ? self->classname : "UNKNOWN",
                cooldown_remaining.seconds());
        }
		return false;
	}
	if (self->teleport_time > level.time) {
        if (developer->integer > 1) {
            gtime_t cooldown_remaining = self->teleport_time - level.time;
            gi.Com_PrintFmt("CTB: Boss {} on general monster teleport cooldown. Remaining: {:.2f}s\n",
                self->classname ? self->classname : "UNKNOWN",
                cooldown_remaining.seconds());
        }
        return false;
    }

    last_boss_teleport_attempt_time[boss_edict_num] = level.time;

    bool force_teleport = (reason == BossTeleportReason::TRIGGER_HURT || reason == BossTeleportReason::DROWNING);

	if (developer->integer > 1) {
        gi.Com_PrintFmt("CTB: Attempting Horde_TeleportMonster for boss {} to ({},{},{}) (ForceVisible: {})\n",
            self->classname ? self->classname : "UNKNOWN",
            destination_origin.x, destination_origin.y, destination_origin.z,
            force_teleport);
    }

	if (!Horde_TeleportMonster(self, destination_origin, destination_angles, false /* play_effects */, force_teleport)) {
		if (developer->integer > 1) gi.Com_PrintFmt("CTB: Horde_TeleportMonster returned false for boss {}.\n", self->classname ? self->classname : "UNKNOWN");
		return false;
	}

	std::string boss_display_name = GetDisplayName(self);

	switch (reason) {
	case BossTeleportReason::DROWNING:
		if (sound_tele3) gi.sound(self, CHAN_AUTO, sound_tele3, 1, ATTN_NORM, 0);
		gi.LocBroadcast_Print(PRINT_HIGH, "{} emerges from the depths!\n", boss_display_name.c_str());
		break;
	case BossTeleportReason::TRIGGER_HURT:
		if (sound_tele_up) gi.sound(self, CHAN_AUTO, sound_tele_up, 1, ATTN_NORM, 0);
		gi.LocBroadcast_Print(PRINT_HIGH, "{} escapes certain death!\n", boss_display_name.c_str());
		break;
	}
	SpawnGrow_Spawn(self->s.origin, 100.0f, 15.0f);
	PushEntitiesAway(self->s.origin, 3, 600.0f, 1200.0f, 4000.0f, 1800.0f);

	if (self->health < (self->max_health >> 2)) {
		self->health = (self->max_health >> 2);
	}

	MarkPositionAsRecentlyTeleported(destination_origin);

	if (developer->integer) {
		const char* reason_str = reason == BossTeleportReason::DROWNING ? "drowning" : "trigger_hurt";
		gi.Com_PrintFmt("CTB: Boss {} successfully teleported due to {} to ({},{},{}).\n",
            self->classname ? self->classname : "UNKNOWN",
            reason_str,
            self->s.origin.x, self->s.origin.y, self->s.origin.z);
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

static void SpawnBossAutomatically() {
	// --- 1. Cleanup Existing Bosses ---
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end(); /* no increment */) {
		edict_t* existing_boss = *it;
		if (existing_boss && existing_boss->inuse) {
			if (!existing_boss->monsterinfo.BOSS_DEATH_HANDLED) {
				BossDeathHandler(existing_boss);
			}
			OnEntityRemoved(existing_boss);
			G_FreeEdict(existing_boss);
		}
		it = auto_spawned_bosses.erase(it);
	}
	boss_spawned_for_wave = false; // Reset flag

	// --- 2. Basic Wave Check ---
	if (g_horde_local.level < 10 || g_horde_local.level % 5 != 0) {
		return; // Not a boss wave
	}

	// --- 3. Select Boss Type ---
	edict_t* temp_boss_for_size = G_Spawn(); // Still need this temporary entity
	if (!temp_boss_for_size) {
		if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: Failed to spawn temp entity for size category.\n");
		return;
	}
	const char* map_name = GetCurrentMapName();
	horde::MonsterTypeID boss_type = G_HordePickBOSSType(
		g_horde_local.current_map_size, map_name, g_horde_local.level, temp_boss_for_size);
	// BossSizeCategory selected_boss_size = temp_boss_for_size->bossSizeCategory; // Store size category if needed later
	G_FreeEdict(temp_boss_for_size);

	if (boss_type == horde::MonsterTypeID::UNKNOWN) {
		if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: Failed to pick a boss type for wave {}.\n", g_horde_local.level);
		return;
	}
	const char* boss_classname = horde::MonsterTypeRegistry::GetClassname(boss_type);
	if (!boss_classname) {
		if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: Failed to get classname for boss type ID {}.\n", (int)boss_type);
		return;
	}

	// --- 4. Determine Spawn Location (Fixed Origin or Random Fallback) ---
	vec3_t spawn_origin = vec3_origin;
	vec3_t spawn_angles = vec3_origin; // Default angles
	bool location_found = false;
	edict_t* selected_point = nullptr; // Used only for random fallback path
	bool free_selected_point = false; // Flag if selected_point is temporary

	// Get boss properties needed for validation
	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(boss_type, predicted_mins, predicted_maxs);
	bool boss_is_flying = IsFlying(boss_type);

	// Try getting fixed origin first
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(map_name);
	vec3_t fixed_origin;
	if (mapId != horde::MapID::UNKNOWN && horde::MapOriginRegistry::GetOrigin(mapId, fixed_origin)) {
		if (is_valid_vector(fixed_origin) && fixed_origin != vec3_origin) {
			vec3_t validated_fixed_origin = fixed_origin;
			//if (IsValidSpawnLocation(validated_fixed_origin, predicted_mins, predicted_maxs, boss_is_flying)) {
				spawn_origin = validated_fixed_origin;
				location_found = true;
				if (developer->integer > 1) gi.Com_PrintFmt("SpawnBossAutomatically: Using validated predefined map origin {} for boss {}.\n", spawn_origin, boss_classname);
		//} removed so bosses spawns where it belongs
			else {
				if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: Predefined map origin {} failed validation for boss {}. Falling back to random.\n", fixed_origin, boss_classname);
			}
		}
		else {
			if (developer->integer > 1) gi.Com_PrintFmt("SpawnBossAutomatically: Invalid predefined map origin found for {}. Falling back to random.\n", map_name);
		}
	}
	else {
		if (developer->integer > 1) gi.Com_PrintFmt("SpawnBossAutomatically: No predefined map origin found for {}. Using random spawn point fallback.\n", map_name);
	}

	// Fallback to random spawn point selection if fixed origin wasn't found or wasn't valid
	if (!location_found) {
		constexpr float MIN_BOSS_PLAYER_DIST_SQ = 250.0f * 250.0f;
		auto BossSpawnFilter = [&](edict_t* spawnPoint) -> bool {
			// (Same filter logic as before)
			if (!spawnPoint || !spawnPoint->inuse) return false;
			const auto& data = spawnPointsData[spawnPoint];
			if ((data.isTemporarilyDisabled && level.time < data.cooldownEndsAt) ||
				level.time < data.teleport_cooldown ||
				level.time < data.alternative_cooldown) return false;
			bool is_flying_point = (spawnPoint->style == 1);
			if (boss_is_flying && !is_flying_point) return false;
			if (!boss_is_flying && is_flying_point) return false;
			for (const auto* const player : active_players_no_spect()) {
				if ((spawnPoint->s.origin - player->s.origin).lengthSquared() < MIN_BOSS_PLAYER_DIST_SQ) return false;
			}
			if (IsSpawnPointOccupied(spawnPoint)) return false;
			return true;
			};

		selected_point = SelectRandomSpawnPoint(BossSpawnFilter);

		if (!selected_point) {
			if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: Failed to find suitable random spawn point for boss {}. Trying emergency spawn.\n", boss_classname);
			vec3_t emergency_origin, emergency_angles;
			bool used_human = false;
			if (FindEmergencySpawnPosition(emergency_origin, emergency_angles, used_human, boss_type)) {
				vec3_t final_emergency_pos = emergency_origin;
				if (IsValidSpawnLocation(final_emergency_pos, predicted_mins, predicted_maxs, boss_is_flying)) {
					// Use the emergency location directly
					spawn_origin = final_emergency_pos;
					spawn_angles = emergency_angles;
					location_found = true;
					if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: Using validated emergency spawn location {}.\n", final_emergency_pos);
				}
				else {
					if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: Emergency position {} failed IsValidSpawnLocation for boss.\n", emergency_origin);
					// Abort if emergency location is also invalid
					return;
				}
			}
			else {
				if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: Emergency spawn position finding failed for boss.\n");
				return; // Failed to find any spot
			}
		}
		else {
			// Use the location from the selected random spawn point
			spawn_origin = selected_point->s.origin;
			spawn_angles = selected_point->s.angles;
			location_found = true;
			if (developer->integer > 1) gi.Com_PrintFmt("SpawnBossAutomatically: Using random spawn point {} for boss {}.\n", spawn_origin, boss_classname);
		}
	}

	// --- 5. Setup Delayed Spawn if Location Found ---
	if (location_found) {
		// Create orb effect at the chosen location
		edict_t* orb = G_Spawn();
		if (orb) {
			orb->classname = "target_orb";
			orb->s.origin = spawn_origin; // Orb at the final spawn location
			SP_target_orb(orb);
		}
		else {
			if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: Failed to spawn orb effect.\n");
			// Continue without orb? Or abort? Let's continue.
		}

		// Spawn the boss entity placeholder
		edict_t* boss = G_Spawn();
		if (!boss) {
			if (orb) G_FreeEdict(orb);
			if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: Failed to G_Spawn boss entity.\n");
			return;
		}

		// Set basic info needed before think
		boss->classname = boss_classname;
		boss->s.origin = spawn_origin; // Set origin now
		boss->s.angles = spawn_angles; // Set angles now
		boss->owner = orb; // Link orb for removal in think

		// --- Perform Area Clearing Actions NOW ---
		// Push away nearby entities before the think function runs
		constexpr float push_radius = 500.0f;
		constexpr float push_force = 1000.0f; // Increased force maybe?
		PushEntitiesAway(spawn_origin, 3, push_radius, push_force, 3750.0f, 1600.0f);
		// Potentially add other clearing logic here (e.g., destroying minor obstacles)

		// Set up the delayed spawn via think function
		boss->nextthink = level.time + 750_ms; // Keep the delay
		boss->think = BossSpawnThink;
		gi.linkentity(boss); // Link the placeholder entity

		// Note: boss_spawned_for_wave is set inside BossSpawnThink after ED_CallSpawn succeeds

	}
	else {
		// This case should ideally not be reached if emergency spawn works, but is a safeguard
		if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: CRITICAL - Failed to determine any valid spawn location for boss.\n");
	}
}

// BossSpawnThink function (largely unchanged, but origin/angles are already set)
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
	else {
		// Handle other potential fallbacks or log if needed
		if (developer->integer) {
			gi.Com_PrintFmt("BossSpawnThink: Boss wave type %d recently used or invalid, falling back to Medium.\n", static_cast<int>(bossWaveInfo.first));
		}
		current_wave_type = MonsterWaveType::Medium;
		StoreWaveType(current_wave_type);
	}


	// Boss spawn message - using array lookup
	std::string_view bossMessage = GetBossMessage(typeId);
	gi.LocBroadcast_Print(PRINT_CHAT, "\n\n\n{}\n", bossMessage.data());

	// Set boss flags in a single group
	self->monsterinfo.IS_BOSS = true;
	self->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	self->monsterinfo.last_sentrygun_target_time = 0_ms;
	// Retrieve size category if needed (might need to store it on the edict before think)
	// BossSizeCategory boss_size = self->bossSizeCategory; // Assuming it was stored

	// --- Call ED_CallSpawn to fully initialize the monster ---
	self->solid = SOLID_NOT; // Temporarily non-solid for spawn function
	ED_CallSpawn(self);

	// Check if ED_CallSpawn freed the entity (e.g., spawn failed internally)
	if (!self->inuse) {
		if (developer->integer) gi.Com_PrintFmt("BossSpawnThink: ED_CallSpawn failed for boss {}.\n", self->classname ? self->classname : "Unknown");
		boss_spawned_for_wave = false; // Ensure flag is false
		return; // Abort if spawn failed
	}

	// --- Post ED_CallSpawn Setup ---
	boss_spawned_for_wave = true; // Set flag *after* successful spawn

	// Restore solidity and link
	self->solid = SOLID_BBOX;
	gi.linkentity(self);

	// Configure boss properties
	ConfigureBossArmor(self);
	ApplyBossEffects(self);
	self->monsterinfo.attack_state = AS_BLIND;

	// Apply visual effects
	const vec3_t spawn_pos = self->s.origin; // Use the final origin
	const float magnitude = spawn_pos.length();
	const float base_size = std::max(100.0f, magnitude * 0.15f);
	const float end_size = base_size * 0.01f;
	ImprovedSpawnGrow(spawn_pos, base_size, end_size, self);
	SpawnGrow_Spawn(spawn_pos, base_size, end_size);
	gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0); // Play spawn sound

	// Set up health bar and track boss
	AttachHealthBar(self);
	SetHealthBarName(self);
	auto_spawned_bosses.insert(self);

	if (developer->integer > 1) {
		gi.Com_PrintFmt("BossSpawnThink: Finalized spawn for boss {} at {}.\n", self->classname, self->s.origin);
	}
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
void ResetCooldowns() noexcept {
    // Reset data for all *active* spawn points
    for (edict_t* spawnPoint : monster_spawn_points()) {
        if (spawnPoint && spawnPoint->inuse) { // Added explicit inuse check for safety
            spawnPointsData[spawnPoint] = SpawnPointData{}; // Full reset of individual data
        }
    } // Ensure this brace is correctly placed

    // Reset global trackers
    horde::g_monsterSpawnTracker.Reset();
    horde::g_spawnPointTimeTracker.Reset();

    // DO NOT recalculate global SPAWN_POINT_COOLDOWN here.
    // It's managed by:
    // 1. ResetAllSpawnPointDataAndTrackers() during full game init.
    // 2. UnifiedAdjustSpawnRate() during per-wave init (Horde_InitLevel).

    if (developer->integer > 1) {
         gi.Com_PrintFmt("ResetCooldowns: Individual spawn point data and specific trackers reset.\n");
    }
}

void ResetAllSpawnAttempts() noexcept {
	// Find all active spawn points and reset them
	for (uint32_t i = 1; i < globals.num_edicts; i++) {
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

//
// game resetting
//


// Reset ambush system state
void ResetAmbushSystem() {
	last_ambush_time = 0_sec;
	ambush_cooldown_end = 0_sec;
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

static void ResetTeleportTracking() noexcept {
	// Reset recent teleport position history
	std::fill(g_recent_teleport_positions.begin(), g_recent_teleport_positions.end(), RecentTeleportPosition{});
	g_recent_teleport_index = 0;

	// Reset global teleport rate limiting
	HordeConstants::g_teleport_rate_count = 0;
	HordeConstants::g_teleport_rate_reset_time = level.time; // Reset the timer completely

	if (developer && developer->integer > 1) { // Optional debug print
		gi.Com_PrintFmt("Teleport tracking reset.\n");
	}
}

static void ResetAllSpawnPointDataAndTrackers() {
    // 1. Reset data for all potential spawn points
    // Iterate all edicts to find spawn points, similar to ResetAllSpawnAttempts
	for (uint32_t i = 1; i < globals.num_edicts; i++) {
		edict_t* ent = &g_edicts[i];
		if (ent && ent->inuse && ent->classname &&
			!strcmp(ent->classname, "info_player_deathmatch")) {
			spawnPointsData[ent] = SpawnPointData{}; // Reset to default values
		}
	}
    // If you prefer to iterate only known spawn points (e.g., from monster_spawn_points()):
    // for (edict_t* spawnPoint : monster_spawn_points()) {
    //     if (spawnPoint) { // monster_spawn_points() should ensure inuse
    //         spawnPointsData[spawnPoint] = SpawnPointData{};
    //     }
    // }

    // 2. Reset global trackers
	horde::g_monsterSpawnTracker.Reset();
	horde::g_spawnPointTimeTracker.Reset();

    // 3. Recalculate global SPAWN_POINT_COOLDOWN
    // (Assuming g_horde_local is already reset or will be reset by the caller)
	const horde::MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level; // Should be 0 after ResetGame
	const int32_t humanPlayers = GetNumHumanPlayers();

	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);
	const float cooldownScale = CalculateCooldownScale(currentLevel, mapSize);
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	if (humanPlayers > 1) {
		const float playerAdjustment = 1.0f - (std::min(humanPlayers - 1, 3) * 0.05f);
		SPAWN_POINT_COOLDOWN *= playerAdjustment;
	}
	if ((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) {
		SPAWN_POINT_COOLDOWN *= 0.95f;
	}
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN,
		HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN,
		3.0_sec);

    if (developer->integer > 1) {
        gi.Com_PrintFmt("ResetAllSpawnPointDataAndTrackers: Complete. Global Cooldown: %.2fs\n", SPAWN_POINT_COOLDOWN.seconds());
    }
}

void ResetGame() {
	if (hasBeenReset) {
		if (developer && developer->integer > 1) {
			gi.Com_PrintFmt("INFO: Reset already performed, skipping...\n");
		}
		return;
	}
	hasBeenReset = true;

	if (developer && developer->integer) {
		gi.Com_PrintFmt("INFO: Performing full game state reset...\n");
	}

	g_horde_retaliation_active = false;
	g_horde_retaliation_end_time = 0_sec;
	g_horde_retaliation_target_player = nullptr;

	for (size_t i = 0; i < game.maxclients; ++i) {
		if (game.clients[i].laser_manager) {
			if (developer && developer->integer > 1) {
				gi.Com_PrintFmt("Resetting PlayerLaserManager for client slot {} during ResetGame\n", i);
			}
			game.clients[i].laser_manager.reset();
		}
	}

	std::fill(g_recent_spawn_positions.begin(), g_recent_spawn_positions.end(), RecentSpawnPosition{});
	g_recent_position_index = 0;
	std::fill(g_recent_teleport_positions.begin(), g_recent_teleport_positions.end(), RecentTeleportPosition{});
	g_recent_teleport_index = 0;

	//HordeConstants::recent_teleport_count = 0;
	HordeConstants::g_teleport_rate_count = 0;
	HordeConstants::g_teleport_rate_reset_time = level.time;

	ResetTeleportTracking();
	ResetRecentBosses();
	ResetAmbushSystem();
	ResetWaveMemory();
	ResetChampionMonsterState();
	ResetBosses();

	g_adjusted_monster_cap = 0;
	// spawn_point_cache.clear(); // This is handled by CleanupSpawnPointCache
	consistent_zero_counts = 0;
	counter_mismatch_frames = 0;
	horde_message_end_time = 0_sec;
	g_totalMonstersInWave = 0;
	g_maxMonstersReached = false;
	g_lowPercentageTriggered = false;
	
    CleanupSpawnPointCache(); // Clears spawn_point_cache

    // --- FIX: Call the new consolidated reset function ---
	ResetAllSpawnPointDataAndTrackers();
    // --- END FIX ---
    // Note: spawnPointsData.clear(), ResetAllSpawnAttempts(), ResetCooldowns() are now covered by the above.

	need_spawn_cache_reset = true; // For g_potential_spawn_points cache
	need_frame_timer_reset = true;
	need_queue_monitor_reset = true; // Assuming this resets other queue-specific logic not covered
	g_consecutive_spawn_failures = 0;
	g_recovery_mode_active = false;
	g_original_wave_type_before_recovery = MonsterWaveType::None;

	g_horde_local = HordeState();
	current_wave_level = 0;
	boss_spawned_for_wave = false;
	next_wave_message_sent = false;
	allowWaveAdvance = false;
	// SPAWN_POINT_COOLDOWN is set in ResetAllSpawnPointDataAndTrackers

	// ResetAllSpawnAttempts(); // Now covered by ResetAllSpawnPointDataAndTrackers
	// ResetCooldowns();       // Now covered by ResetAllSpawnPointDataAndTrackers
	ResetBenefits();
	ResetWaveAdvanceState();

	g_horde_local.update_map_size(GetCurrentMapName());

	gi.cvar_set("bot_pause", "0");
	gi.cvar_set("cheats", "0");
	if (g_chaotic) gi.cvar_set("g_chaotic", "0");
	if (g_insane) gi.cvar_set("g_insane", "0");
	if (g_hardcoop) gi.cvar_set("g_hardcoop", "0");
	if (dm_monsters) gi.cvar_set("dm_monsters", "0");
	if (timelimit) gi.cvar_set("timelimit", "60");
	if (ai_damage_scale) gi.cvar_set("ai_damage_scale", "1");
	if (ai_allow_dm_spawn) gi.cvar_set("ai_allow_dm_spawn", "1");
	if (g_damage_scale) gi.cvar_set("g_damage_scale", "1");
	if (g_vampire) gi.cvar_set("g_vampire", "0");
	if (g_startarmor) gi.cvar_set("g_startarmor", "0");
	if (g_ammoregen) gi.cvar_set("g_ammoregen", "0");
	if (g_upgradeproxs) gi.cvar_set("g_upgradeproxs", "0");
	if (g_piercingbeam) gi.cvar_set("g_piercingbeam", "0");
	if (g_tracedbullets) gi.cvar_set("g_tracedbullets", "0");
	if (g_energyshells) gi.cvar_set("g_energyshells", "0");
	if (g_bouncygl) gi.cvar_set("g_bouncygl", "0");
	if (g_bfgpull) gi.cvar_set("g_bfgpull", "0");
	if (g_bfgslide) gi.cvar_set("g_bfgslide", "1");
	if (g_autohaste) gi.cvar_set("g_autohaste", "0");

	std::fill(used_wave_sounds.begin(), used_wave_sounds.end(), false);
	remaining_wave_sounds = NUM_WAVE_SOUNDS;
	std::fill(used_start_sounds.begin(), used_start_sounds.end(), false);
	remaining_start_sounds = NUM_START_SOUNDS;

	ClearHordeMessage();
	g_horde_local.reset_hud_state();

	gi.Com_PrintFmt("INFO: Horde game state reset complete.\n");
}

// Replace the existing CalculateRemainingMonsters() function
int32_t CalculateRemainingMonsters() noexcept {
	// Simple counter approach - more reliable from old version
	const int32_t remaining = level.total_monsters - level.killed_monsters;
	return std::max(0, remaining);  // Ensure non-negative
}
// Enhanced version of CheckRemainingMonstersCondition
static bool CheckRemainingMonstersCondition(const horde::MapSize& mapSize, WaveEndReason& reason) {
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
	if (g_horde_local.conditionTriggered && remainingMonsters <= HordeConstants::MONSTERS_FOR_AGGRESSIVE_REDUCTION) {

		// --- MODIFIED AGGRESSIVE TIME CALCULATION ---

		// Base calculation - Increased base and per-monster time significantly
		// float base_time = 4.0f + (remainingMonsters * 1.0f); // Original Increased base and per-monster time
		float base_time = 6.0f + (remainingMonsters * 1.5f); // MODIFIED: More base time, more time per monster (e.g., 7.5s for 1, 10.5s for 3)

		// Map Size Multiplier: Give more time on smaller maps (Keep this logic)
		float map_size_multiplier = 1.0f;
		if (mapSize.isSmallMap) {
			map_size_multiplier = 1.3f;
		}
		else if (mapSize.isMediumMap) {
			map_size_multiplier = 1.15f;
		}
		base_time *= map_size_multiplier;

		// --- END MODIFIED AGGRESSIVE TIME CALCULATION ---


		// Boss wave consideration - give substantially more time (Keep this logic)
		if (IsBossWave() && boss_spawned_for_wave) {
			base_time *= 2.0f + (0.2f * remainingMonsters);
			base_time = std::max(base_time, 10.0f); // MODIFIED: Increased minimum for boss waves
		}
		else {
			// Non-boss wave adjustments

			// Wave level factor - higher waves get less time (Keep this logic)
			if (current_wave_level >= 15) {
				float reduction = std::min((current_wave_level - 15) * 0.02f, 0.3f);
				base_time *= (1.0f - reduction);
			}

			// Player count consideration (Keep this logic)
			int32_t playerCount = GetNumHumanPlayers();
			if (playerCount > 1) {
				float player_reduction = std::min((playerCount - 1) * 0.07f, 0.2f);
				base_time *= (1.0f - player_reduction);
			}
		}

		// Ensure minimum time thresholds (Increased min for non-boss)
		// float min_time = (IsBossWave() && boss_spawned_for_wave) ? 5.0f : 3.0f; // Original Increased min for non-boss
		float min_time = (IsBossWave() && boss_spawned_for_wave) ? 7.0f : 5.0f; // MODIFIED: Further increased minimums
		if (!IsBossWave() || !boss_spawned_for_wave) {
			min_time *= map_size_multiplier; // Scale the minimum too
		}
		gtime_t aggressive_time = gtime_t::from_sec(std::max(min_time, base_time));

		// Calculate how much original time remains (Keep this logic)
		const gtime_t original_remaining = g_horde_local.waveEndTime - currentTime;

		// Only apply reduction if it's actually faster than current end time (Keep this logic)
		if (aggressive_time < original_remaining) {
			g_horde_local.waveEndTime = currentTime + aggressive_time;

			if (developer->integer) {
				gi.Com_PrintFmt("Aggressive time reduction: {:.1f}s remaining (Map Size Mult: {:.2f}) for {} monsters (wave: {}, boss: {})\n",
					aggressive_time.seconds(),
					map_size_multiplier,
					remainingMonsters, current_wave_level,
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

	for (size_t i = 0; i < WARNING_TIMES.size(); i++)
	{ // Use WARNING_TIMES.size()
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
	Horde_InitLevel(g_horde_local.level + 1);
	g_horde_local.monster_spawn_time = level.time;
	g_horde_local.warm_time = level.time;

	// Permitir el avance inmediato
	//allowWaveAdvance = true;

	// Resetear variables importantes
	g_horde_local.num_to_spawn = 0;
	g_horde_local.queued_monsters = 0;

	g_horde_local.conditionTriggered = true;
	g_horde_local.waveEndTime = level.time;

	// Limpiar cualquier mensaje pendiente
	ClearHordeMessage();


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
		"Improved Traps!\n\nTraps are reutilizable after 5sec of eating a strogg!"
	};

	// Usar distribución uniforme con mt_rand
	std::uniform_int_distribution<size_t> dist(0, messages.size() - 1);
	const size_t choice = dist(mt_rand);
	UpdateHordeMessage(messages[choice], duration);
}

void HandleWaveCleanupMessage(const horde::MapSize& mapSize, const WaveEndReason reason) noexcept {
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
	for (size_t i = 0; i < WARNING_TIMES.size(); i++)
	{ // Use WARNING_TIMES.size()
		g_horde_local.warningIssued[i] = false;
	}

	gi.sound(world, CHAN_VOICE, GetRandomWaveSound(), 1, ATTN_NONE, 0);
}

void InitializeWaveSystem() noexcept {
	PrecacheWaveSounds();
}

static void SetMonsterArmor(edict_t* monster);
static void SetNextMonsterSpawnTime(const horde::MapSize& mapSize);


// --- FindEmergencySpawnPosition Function (Enhanced Validation) ---
bool FindEmergencySpawnPosition(vec3_t& position, vec3_t& angles, bool& used_human_player, horde::MonsterTypeID typeId) {
	if (developer->integer > 1) gi.Com_PrintFmt("DEBUG: Starting FindEmergencySpawnPosition for TypeID {}\n", static_cast<int>(typeId));
	used_human_player = false;

	constexpr int MAX_ATTEMPTS_PER_PLAYER = 10;
	constexpr float FAR_RADIUS_MAX = 1200.0f;
	vec3_t predicted_mins, predicted_maxs;
	if (!GetPredictedScaledBounds(typeId, predicted_mins, predicted_maxs)) {
		// Fallback handled inside
	}
	bool is_flying = IsFlying(typeId);

	// --- Player Prioritization (Same as before) ---
	constexpr size_t MAX_PLAYERS = MAX_CLIENTS;
	edict_t* top_damage_humans[MAX_PLAYERS] = { nullptr };
	edict_t* high_spree_humans[MAX_PLAYERS] = { nullptr };
	edict_t* normal_humans[MAX_PLAYERS] = { nullptr };
	edict_t* bots[MAX_PLAYERS] = { nullptr };
	size_t top_damage_count = 0, high_spree_count = 0, normal_count = 0, bot_count = 0;
	int32_t highest_damage = 0; int highest_spree = 0;

	for (auto* player : active_players()) { // Use range-based for loop
		// Initial quick check
		if (!player || !player->inuse || !player->client || player->health <= 0 || ClientIsSpectating(player->client)) continue;

		bool is_human = !(player->svflags & SVF_BOT);
		int32_t player_damage = player->client->total_damage; int player_spree = player->client->resp.spree;

		if (is_human) {
			highest_damage = std::max(highest_damage, player_damage); highest_spree = std::max(highest_spree, player_spree);
			// Prioritize based on damage/spree
			if (player_damage > 0 && player_damage >= highest_damage * 0.65f && top_damage_count < MAX_PLAYERS) top_damage_humans[top_damage_count++] = player;
			else if (player_spree >= 15 && high_spree_count < MAX_PLAYERS) high_spree_humans[high_spree_count++] = player;
			else if (normal_count < MAX_PLAYERS) normal_humans[normal_count++] = player;
		}
		else if (bot_count < MAX_PLAYERS) { // It's a bot
			bots[bot_count++] = player;
		}
	}

	if (top_damage_count == 0 && high_spree_count == 0 && normal_count == 0 && bot_count == 0) {
		if (developer->integer) gi.Com_PrintFmt("FindEmergencySpawnPosition: No active, non-spectating players/bots found.\n");
		return false;
	}

	struct PlayerGroup { edict_t** players; size_t count; bool is_human; };
	PlayerGroup priority_groups[] = {
		{ top_damage_humans, top_damage_count, true },
		{ high_spree_humans, high_spree_count, true },
		{ normal_humans, normal_count, true },
		{ bots, bot_count, false }
	};
	// --- End Player Prioritization ---

	// --- Attempt Loop ---
	for (const auto& group : priority_groups) {
		if (group.count == 0) continue;

		// Shuffle the players within the group for variety
		for (size_t i = group.count - 1; i > 0; --i) {
			size_t j = irandom(0, i);
			if (i != j) std::swap(group.players[i], group.players[j]);
		}

		for (size_t player_idx = 0; player_idx < group.count; ++player_idx) {
			edict_t* player = group.players[player_idx];

			// *** CRITICAL VALIDATION 1: Check player before starting attempts for them ***
			if (!player || !player->inuse || !player->client || player->health <= 0 || ClientIsSpectating(player->client)) {
				if (developer->integer > 1) gi.Com_PrintFmt("FindEmergencySpawnPosition: Player {} in group became invalid before attempts.\n", (int)(player - g_edicts));
				continue; // Skip this player
			}
			// Cache player origin *after* validation
			const vec3_t player_origin = player->s.origin;
			if (!is_valid_vector(player_origin)) {
                 if (developer->integer > 1) gi.Com_PrintFmt("FindEmergencySpawnPosition: Player {} origin invalid.\n", (int)(player - g_edicts));
                 continue; // Skip player with invalid origin
            }


			for (int attempt = 0; attempt < MAX_ATTEMPTS_PER_PLAYER; ++attempt) {
				// Generate candidate position (same logic)
				float radius = HordeConstants::MIN_PLAYER_DIST_GENERATE + frandom() * (FAR_RADIUS_MAX - HordeConstants::MIN_PLAYER_DIST_GENERATE);
				float angle = frandom() * 2.0f * PI;
				vec3_t candidate_pos = {
					player_origin[0] + cosf(angle) * radius, // Use cached player_origin
					player_origin[1] + sinf(angle) * radius, // Use cached player_origin
					player_origin[2] + frandom(8.0f, 48.0f)  // Use cached player_origin
				};

				// Validate candidate vector itself
				if (!is_valid_vector(candidate_pos)) {
                    if (developer->integer > 2) gi.Com_PrintFmt("FindEmergencySpawnPosition: Generated candidate position is invalid.\n");
                    continue;
                }

				vec3_t validated_pos = candidate_pos; // Copy for IsValidSpawnLocation

				// --- Run IsValidSpawnLocation ---
				if (IsValidSpawnLocation(validated_pos, predicted_mins, predicted_maxs, is_flying)) {

					// *** CRITICAL VALIDATION 2: Re-check player state *before* distance/angle calcs ***
					if (!player || !player->inuse || !player->client || player->health <= 0 || ClientIsSpectating(player->client)) {
						if (developer->integer > 1) gi.Com_PrintFmt("FindEmergencySpawnPosition: Player {} became invalid *after* IsValidSpawnLocation.\n", (int)(player - g_edicts));
						break; // Stop attempts for this now-invalid player
					}
                    // Re-cache origin *just in case* it somehow changed, though unlikely if player is valid
                    const vec3_t current_player_origin = player->s.origin;
                    if (!is_valid_vector(current_player_origin)) {
                         if (developer->integer > 1) gi.Com_PrintFmt("FindEmergencySpawnPosition: Player {} origin became invalid after IsValidSpawnLocation.\n", (int)(player - g_edicts));
                         break; // Stop attempts for this player
                    }


					// --- Check Distance ---
					// Validate vectors before subtraction
					if (!is_valid_vector(validated_pos)) { // validated_pos might have been changed by IsValidSpawnLocation
                         if (developer->integer > 2) gi.Com_PrintFmt("FindEmergencySpawnPosition: validated_pos became invalid after IsValidSpawnLocation.\n");
                         continue;
                    }

					if ((validated_pos - current_player_origin).lengthSquared() < HordeConstants::MIN_PLAYER_DIST_SQ_CHECK) {
						if (developer->integer > 1) gi.Com_PrintFmt("FindEmergencySpawnPosition: Candidate {} too close to target player {}.\n", validated_pos, GetPlayerName(player).c_str());
						continue; // Too close
					}

					// --- Check Recent Teleports/Spawns ---
					if (IsPositionTooCloseToRecentTeleport(validated_pos)) {
						if (developer->integer > 2) gi.Com_PrintFmt("FindEmergencySpawnPosition: Candidate {} too close to recent teleport.\n", validated_pos);
						continue;
					}
					if (IsPositionTooCloseToRecentSpawn(validated_pos)) {
						if (developer->integer > 2) gi.Com_PrintFmt("FindEmergencySpawnPosition: Candidate {} too close to recent regular spawn.\n", validated_pos);
						continue;
					}

					// ******************************************************
					// ***** ADDED MONSTERSOLID TRACE CHECK HERE *****
					// ******************************************************
					trace_t monster_trace = gi.trace(validated_pos, predicted_mins, predicted_maxs, validated_pos, nullptr, MASK_MONSTERSOLID);
					if (monster_trace.startsolid) {
						// This trace hit something solid to monsters (another monster, player, solid defense)
						if (developer->integer > 1) {
							edict_t* blocker = monster_trace.ent;
							gi.Com_PrintFmt("FindEmergencySpawnPosition: Validated pos {} blocked by MONSTERSOLID entity: {} ({})\n",
								validated_pos,
								blocker ? (blocker->classname ? blocker->classname : "unknown") : "world/unknown",
								blocker ? (blocker - g_edicts) : -1);
						}
						continue; // Spot is blocked, try next attempt/player
					}
					// ******************************************************
					// ***** END MONSTERSOLID TRACE CHECK *****
					// ******************************************************


					// --- Calculate Angles ---
					vec3_t dir = current_player_origin - validated_pos; // Use current validated vectors
                    // Use the pre-squared epsilon for direct comparison
					if (is_valid_vector(dir) && dir.lengthSquared() > VECTOR_LENGTH_SQ_EPSILON) {
						angles = vectoangles(dir);
                        // --- ADDED FIX for PITCH on ground monsters ---
                        if (!is_flying) { // is_flying was determined earlier based on typeId
                            angles[PITCH] = 0;
                        }
                        // --- END ADDED FIX ---
					} else {
						angles = vec3_origin; // Default angle if direction is bad (pitch is already 0 here)
						if (developer->integer > 1) gi.Com_PrintFmt("FindEmergencySpawnPosition: Invalid or zero-length direction vector for vectoangles.\n");
					}

					// --- Success ---
					position = validated_pos; // Assign the final validated position
					used_human_player = group.is_human;
					if (developer->integer > 0) { // Log success less verbosely by default
						gi.Com_PrintFmt("FindEmergencySpawnPosition: Success! Found valid pos {} near {} {}.\n", position, group.is_human ? "human" : "bot", GetPlayerName(player).c_str());
					}
					return true; // Found a valid spot!

				} // End if (IsValidSpawnLocation)
			} // End attempt loop
		} // End player loop
	} // End group loop

	if (developer->integer) gi.Com_PrintFmt("FindEmergencySpawnPosition: Failed after all attempts.\n");
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

static BoxEdictsResult_t SpawnPointFilter(edict_t* ent, void* data) {
	FilterData* filter_data = static_cast<FilterData*>(data);

	if (ent == filter_data->ignore_ent) { // Ignore self if specified
		return BoxEdictsResult_t::Skip;
	}
	if (ent->client && ent->inuse) { // Player/Bot check
		filter_data->count++;
		return BoxEdictsResult_t::End;
	}
	if ((ent->svflags & SVF_MONSTER) && ent->inuse && !ent->deadflag) { // Live Monster check
		filter_data->count++;
		return BoxEdictsResult_t::End;
	}
	if (ent->inuse && IsPlayerDefense(ent)) { // Player Defense check
		filter_data->count++;
		return BoxEdictsResult_t::End;
	}
	return BoxEdictsResult_t::Skip;
}

// --- IsValidSpawnLocation Function (Refined Logging) ---
[[nodiscard]] bool IsValidSpawnLocation(vec3_t& io_position, const vec3_t& monster_mins, const vec3_t& monster_maxs, bool is_flying) {
	constexpr contents_t GEOMETRY_MASK = MASK_SOLID; // Walls, solid architecture
	constexpr contents_t LIQUID_MASK = MASK_WATER;   // Water, slime, lava
	constexpr contents_t FINAL_ENTITY_MASK = MASK_MONSTERSOLID; // Players, monsters, solid defenses
	constexpr float Z_EPSILON = 1.75f; // Small vertical offset after dropping
	constexpr float GROUND_CHECK_DISTANCE = 1024.0f;

	// 0. Initial Vector Check
	if (!is_valid_vector(io_position) || !is_valid_vector(monster_mins) || !is_valid_vector(monster_maxs)) {
		if (developer->integer > 1) gi.Com_PrintFmt("IsValidSpawnLocation: Failed initial vector validation.\n");
		return false;
	}

	// 1. Initial Point Contents Check
	int initial_contents = gi.pointcontents(io_position);
	if (initial_contents & GEOMETRY_MASK) {
		if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Failed initial pointcontents check (SOLID) at {}\n", io_position);
		return false;
	}
	// Check liquids only if the monster cannot fly or swim (assuming flying implies can handle liquids)
	if (!is_flying && (initial_contents & LIQUID_MASK)) {
		if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Failed initial pointcontents check (LIQUID) at {} for non-flying/non-swimming\n", io_position);
		return false;
	}

	// 2. Initial Volume Check (World Geometry Only)
	trace_t trace = gi.trace(io_position, monster_mins, monster_maxs, io_position, nullptr, GEOMETRY_MASK);
	if (trace.startsolid || trace.allsolid) {
		if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Failed initial solid volume check (MASK_SOLID) at {}\n", io_position);
		return false;
	}

	// 3. Drop to Floor (for non-flying units)
	vec3_t original_pos = io_position; // Store original in case drop fails or no drop needed
	if (!is_flying) {
		// Check for void below first
		vec3_t ground_check_end = io_position;
		ground_check_end.z -= GROUND_CHECK_DISTANCE;
		trace_t ground_trace = gi.trace(io_position, monster_mins, monster_maxs, ground_check_end, nullptr, GEOMETRY_MASK);

		if (ground_trace.startsolid) {
			// This can happen if the initial position is slightly embedded in the floor
			if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Ground trace started in solid at {}. Attempting drop anyway.\n", io_position);
            // Proceed to M_droptofloor, it might still succeed
		}
        else if (ground_trace.fraction == 1.0f) { // Check if ground trace hit anything
			if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Failed void check (no solid ground found below within {} units) at {}\n", GROUND_CHECK_DISTANCE, io_position);
			return false; // No ground below
		}

		// Attempt to drop the position to the floor using the geometry mask
		if (!M_droptofloor_generic(io_position, monster_mins, monster_maxs, false, nullptr, GEOMETRY_MASK, false)) {
			io_position = original_pos; // Restore original position
			if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Failed M_droptofloor at {}. Could not find valid drop spot.\n", original_pos);
			return false;
		}
		io_position.z += Z_EPSILON; // Apply small vertical offset after successful drop

		// Re-check volume at the dropped position against geometry
		trace = gi.trace(io_position, monster_mins, monster_maxs, io_position, nullptr, GEOMETRY_MASK);
		if (trace.startsolid || trace.allsolid) {
			if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Failed solid check *after* M_droptofloor + epsilon at {}\n", io_position);
			io_position = original_pos; // Restore original position
			return false;
		}
		if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: M_droptofloor succeeded, new Z (after epsilon): {:.2f}.\n", io_position.z);
	}
	// If flying, io_position remains unchanged from original_pos here

	// 4. Final Volume Check (Entities - Players, Monsters, Defenses)
	// Use a slightly expanded box for entity checks as a safety margin
	const vec3_t entity_check_mins = monster_mins - vec3_t{ 2, 2, 0 };
	const vec3_t entity_check_maxs = monster_maxs + vec3_t{ 2, 2, 0 };
	FilterData final_entity_check_data = { nullptr, 0 };
	const vec3_t check_mins = io_position + entity_check_mins;
	const vec3_t check_maxs = io_position + entity_check_maxs;

	// Check against entities using BoxEdicts and the filter
	gi.BoxEdicts(check_mins, check_maxs, nullptr, 0, AREA_SOLID, SpawnPointFilter, &final_entity_check_data);
	if (final_entity_check_data.count > 0) {
		if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Failed FINAL entity occupation check at validated position {}\n", io_position);
		return false;
	}

	// If all checks passed
	if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Success (All checks passed) for pos {} (Flying: {})\n", io_position, is_flying ? "Yes" : "No");
	return true;
}


// In g_horde.cpp

// --- MODIFIED CheckAndTeleportStuckMonster ---
bool CheckAndTeleportStuckMonster(edict_t* self) {
	PROFILE_SCOPE("CheckAndTeleportStuckMonster");
	if (level.intermissiontime) return false;

	// --- Rate Limiting & Basic Validation ---
	if (level.time - HordeConstants::g_teleport_rate_reset_time > HordeConstants::GLOBAL_TELEPORT_RESET_INTERVAL) {
		HordeConstants::g_teleport_rate_count = 0;
		HordeConstants::g_teleport_rate_reset_time = level.time;
	}
	int max_teleports = HordeConstants::MAX_TELEPORTS_PER_INTERVAL;
	if ((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) {
		max_teleports += 1;
	}
	if (HordeConstants::g_teleport_rate_count >= max_teleports) {
		return false; // Global rate limit reached
	}
	if (!self || !self->inuse || self->deadflag || self->monsterinfo.IS_BOSS || level.intermissiontime || !g_horde || !g_horde->integer) return false;
	if (!strcmp(self->classname, "misc_insane") || !strcmp(self->classname, "monster_turret")) return false;
	if (self->monsterinfo.issummoned) return false;
	// --- End Rate Limiting & Basic Validation ---

	bool attempt_teleport_now = false;

	// --- Drowning Check ---
    if (self->waterlevel > 0) {
        bool can_see_target = false;
        if (self->enemy && self->enemy->inuse && visible(self, self->enemy, false)) {
            can_see_target = true;
        } else {
            for (auto* player : active_players_no_spect()) {
                if (player && player->inuse && visible(self, player, false)) {
                    can_see_target = true;
                    break;
                }
            }
        }
        if (!can_see_target) {
            attempt_teleport_now = true;
            if (developer->integer > 1) gi.Com_PrintFmt("CATS: {} drowning, flagging for immediate teleport.\n", self->classname);
        } else {
            self->monsterinfo.was_stuck = false;
            self->monsterinfo.stuck_check_time = 0_sec;
            self->teleport_time = level.time; 
        }
    }
	// --- End Drowning Check ---

	// --- Standard Stuck/Cooldown Checks ---
	if (!attempt_teleport_now) {
		if (self->teleport_time > level.time) return false;

		const bool is_stuck_in_geometry = gi.trace(self->s.origin, self->mins, self->maxs, self->s.origin, self, MASK_SOLID).startsolid;
		const bool no_damage_timeout = (level.time - self->monsterinfo.react_to_damage_time) >= HordeConstants::NO_DAMAGE_TIMEOUT;

		if (self->monsterinfo.was_stuck && !is_stuck_in_geometry) {
			self->monsterinfo.was_stuck = false;
			self->monsterinfo.stuck_check_time = 0_sec;
			if (developer->integer > 1) {
				gi.Com_PrintFmt("CATS: Resetting 'was_stuck' for {} as it's no longer in geometry.\n", self->classname);
			}
		}

		if (!is_stuck_in_geometry && !no_damage_timeout && !self->monsterinfo.was_stuck) {
			 return false;
		}

		if (!self->monsterinfo.was_stuck && (is_stuck_in_geometry || no_damage_timeout)) {
			self->monsterinfo.stuck_check_time = level.time;
			self->monsterinfo.was_stuck = true;
            if (developer->integer > 1) gi.Com_PrintFmt("CATS: {} now flagged as stuck. StuckInGeom: {}, NoDmgTimeout: {}\n", self->classname, is_stuck_in_geometry, no_damage_timeout);
		}

		bool attempt_immediate_teleport_spawn = false;
        if (self->monsterinfo.was_stuck && self->monsterinfo.stuck_check_time > 0_sec) {
            constexpr gtime_t SPAWN_STUCK_IMMEDIATE_THRESHOLD = 0.5_sec;
            attempt_immediate_teleport_spawn = (level.time < self->monsterinfo.stuck_check_time + SPAWN_STUCK_IMMEDIATE_THRESHOLD);
            if(attempt_immediate_teleport_spawn && developer->integer > 1) {
                 gi.Com_PrintFmt("CATS: {} potentially stuck on spawn. Flagging for immediate teleport check.\n", self->classname);
            }
        }

		if (attempt_immediate_teleport_spawn ||
            (self->monsterinfo.was_stuck && level.time >= self->monsterinfo.stuck_check_time + HordeConstants::STUCK_CHECK_TIME))
        {
            attempt_teleport_now = true;
        }
		else if (self->monsterinfo.was_stuck) {
             return false; 
        }
	}
	// --- End Standard Stuck/Cooldown Checks ---

	if (!attempt_teleport_now) return false;

    bool is_attacking = (self->enemy && self->enemy->inuse && self->monsterinfo.attack_finished > level.time);
    if (is_attacking) {
        if (developer->integer > 1) {
            gi.Com_PrintFmt("CATS: Skipping teleport for {} - Attacking. Will re-evaluate.\n", self->classname);
        }
        const bool is_stuck_in_geometry_for_attack_check = gi.trace(self->s.origin, self->mins, self->maxs, self->s.origin, self, MASK_SOLID).startsolid;
        if (!is_stuck_in_geometry_for_attack_check) {
            self->monsterinfo.was_stuck = false;
            self->monsterinfo.stuck_check_time = 0_sec;
        }
        self->teleport_time = level.time; // Allow quick re-check after attack
        return false;
    }

	if (frandom() < 0.4f) {
		if (developer->integer > 2) gi.Com_PrintFmt("CATS: Randomly skipped teleport attempt for {}.\n", self->classname);
		return false;
	}

	// --- Find Teleport Destination ---
	bool teleported = false;
	vec3_t final_dest_origin = vec3_origin;
	vec3_t final_dest_angles = vec3_origin;
	edict_t* used_spawn_point = nullptr;

	float teleport_chance = 0.01f;
	if (current_wave_level > 5) {
		if (AreHumanPlayersPresent()) teleport_chance = std::min(0.01f + (current_wave_level - 5) * 0.003f, 0.08f);
		else teleport_chance = std::min(0.01f + (current_wave_level - 5) * 0.002f, 0.05f);
	}
	const bool use_player_teleport = (frandom() < teleport_chance) && AreHumanPlayersPresent();

	if (use_player_teleport) {
		vec3_t emergency_origin, emergency_angles;
		bool teleported_to_human = false;
		horde::MonsterTypeID monsterTypeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
		if (FindEmergencySpawnPosition(emergency_origin, emergency_angles, teleported_to_human, monsterTypeId)) {
			final_dest_origin = emergency_origin;
			final_dest_angles = emergency_angles;
            if (developer->integer > 1) gi.Com_PrintFmt("CATS: Found emergency player-based spot for {}: {}\n", self->classname, final_dest_origin);
		}
	}

	if (final_dest_origin == vec3_origin) { // If player-based teleport wasn't used or failed
		StuckMonsterSpawnFilter filter;
		const edict_t* const spawn_point = SelectRandomSpawnPoint(filter);
		if (spawn_point) {
			final_dest_origin = spawn_point->s.origin;
			final_dest_angles = spawn_point->s.angles;
			used_spawn_point = const_cast<edict_t*>(spawn_point);
            if (developer->integer > 1) gi.Com_PrintFmt("CATS: Found spawn point spot for {}: {}\n", self->classname, final_dest_origin);
		}
	}

	if (final_dest_origin != vec3_origin) { // If any destination was found
		if (!IsPositionTooCloseToRecentTeleport(final_dest_origin)) {
			vec3_t potential_dest = final_dest_origin;
			bool use_offset_dest = false;
			
            // Try small random offset from the chosen destination
            // This helps if the base destination (e.g. spawn point center) is slightly problematic
            // but nearby areas are fine.
			constexpr float MAX_OFFSET_RADIUS = 24.0f;
			vec3_t offset = { frandom(-MAX_OFFSET_RADIUS, MAX_OFFSET_RADIUS), frandom(-MAX_OFFSET_RADIUS, MAX_OFFSET_RADIUS), 0 };
			vec3_t offset_candidate_dest = final_dest_origin + offset;
            vec3_t validated_offset_dest = offset_candidate_dest; // For IsValidSpawnLocation

			if (IsValidSpawnLocation(validated_offset_dest, self->mins, self->maxs, IsFlying(horde::MonsterTypeRegistry::GetTypeID(self->classname)))) {
				if (!IsPositionTooCloseToRecentTeleport(validated_offset_dest)) { // Check offset pos too
					potential_dest = validated_offset_dest; 
					use_offset_dest = true;
					if (developer->integer > 1) gi.Com_PrintFmt("CATS: Using offset destination {} for {}.\n", potential_dest, self->classname);
				}
			}
            // If offset failed or wasn't better, potential_dest remains final_dest_origin (the original chosen spot)

            // Call Horde_TeleportMonster. For stuck monsters, we usually don't force despite visibility.
			teleported = Horde_TeleportMonster(self, potential_dest, final_dest_angles, true /* play_effects */, false /* force_despite_visibility */);
			
			if (teleported) {
				MarkPositionAsRecentlyTeleported(potential_dest); // Mark the actual final spot
				if (used_spawn_point && !use_offset_dest) { // Cooldown original spawn point only if it was directly used
					spawnPointsData[used_spawn_point].teleport_cooldown = level.time + 3.5_sec;
				}
				if (developer->integer) gi.Com_PrintFmt("CATS: Monster {} teleported (stuck/emergency) via Horde_TeleportMonster to {}.\n", self->classname, potential_dest);
			} else {
                 if (developer->integer > 1) {
                    gi.Com_PrintFmt("CATS: Horde_TeleportMonster call failed for {} at {}. Visible enemy or validation issue?\n",
                       self->classname, potential_dest);
                 }
            }
		} else {
			if (developer->integer > 1) gi.Com_PrintFmt("CATS: Chosen destination {} for {} too close to recent teleport, skipping.\n", final_dest_origin, self->classname);
		}
	} else {
		if (developer->integer > 1) gi.Com_PrintFmt("CATS: Failed to find any suitable teleport destination for {}.\n", self->classname);
	}

	if (teleported) {
		horde::MonsterTypeID monsterTypeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
		if (monsterTypeId == horde::MonsterTypeID::STALKER || monsterTypeId == horde::MonsterTypeID::SPIDER) {
			if (developer->integer > 1) gi.Com_PrintFmt("CATS: {} teleport: Applying specific fix...\n", self->classname);
			self->gravityVector = { 0, 0, -1 };
			self->s.angles[ROLL] = 0;
			self->gravity = 1.0f;
			if (self->movetype == MOVETYPE_FLY || self->movetype == MOVETYPE_NOCLIP) self->movetype = MOVETYPE_STEP;
			self->groundentity = nullptr;
			constexpr float TELEPORT_FIX_ELEVATION = 16.0f;
			self->s.origin[2] += TELEPORT_FIX_ELEVATION;
			gi.linkentity(self);
		}
		HordeConstants::g_teleport_rate_count++;
	}

    const bool is_still_stuck_in_geometry_after_attempt = gi.trace(self->s.origin, self->mins, self->maxs, self->s.origin, self, MASK_SOLID).startsolid;
	if (teleported || !is_still_stuck_in_geometry_after_attempt) {
        self->monsterinfo.was_stuck = false;
        self->monsterinfo.stuck_check_time = 0_sec;
    } else {
        if (developer->integer > 1 && self->monsterinfo.was_stuck) {
            gi.Com_PrintFmt("CATS: Teleport attempt failed for {}, but still stuck in geometry. Stuck timer remains active (remaining: {:.2f}s).\n",
                self->classname, (self->monsterinfo.stuck_check_time + HordeConstants::STUCK_CHECK_TIME - level.time).seconds());
        }
    }

	return teleported;
}

// Helper function to select a retaliation-themed monster
horde::MonsterTypeID PickRetaliationMonsterTypeID(int32_t waveLevel) {
	WeightedSelection<MonsterTypeInfo> selection;
	selection.clear();

	// Define the desired types for retaliation
	constexpr MonsterWaveType RETALIATION_TYPES = MonsterWaveType::Spawner | MonsterWaveType::Bomber | MonsterWaveType::Special;

	for (const auto& monsterInfo : monsterTypes) {
		// Check if monster meets wave level requirement AND has one of the retaliation types
		if (monsterInfo.minWave <= waveLevel &&
			HasWaveType(GetMonsterWaveTypes(monsterInfo.typeId), RETALIATION_TYPES))
		{
			// Basic weighting: prefer monsters closer to current level
			float weight = 1.0f + (static_cast<float>(monsterInfo.minWave) / std::max(1, waveLevel));
			selection.add(&monsterInfo, weight);
		}
	}

	// Select a monster
	const MonsterTypeInfo* chosenInfo = selection.select();
	if (chosenInfo) {
		return chosenInfo->typeId;
	}

	// Fallback if no specific types found (should be rare)
	if (developer->integer) {
		gi.Com_PrintFmt("PickRetaliationMonsterTypeID: Warning - No Spawner/Bomber/Special found for level {}. Falling back.\n", waveLevel);
	}
	// Fallback to a common medium monster
	return (waveLevel > 10) ? horde::MonsterTypeID::GUNNER : horde::MonsterTypeID::INFANTRY;
}

int SpawnRetaliationAmbush(const horde::MapSize& mapSize, int32_t waveLevel, edict_t* target_player) {
	int baseCount = mapSize.isSmallMap ? 1 : (mapSize.isBigMap ? 3 : 2); // Smaller size
	int ambushSize = baseCount; // No level scaling for this small ambush
	int spawnedCount = 0;

	if (developer->integer) {
		std::string target_name = GetPlayerName(target_player); // Use helper
		gi.Com_PrintFmt("HORDE: Attempting Retaliation Ambush (Size: {}). Target: {}\n",
			ambushSize, target_name.c_str());
	}

	for (int i = 0; i < ambushSize; ++i) {
		horde::MonsterTypeID monster_typeId = PickRetaliationMonsterTypeID(waveLevel);
		edict_t* monster = nullptr; // Will hold the spawned monster

		// --- Try spawning near the target player first ---
		if (target_player) {
			vec3_t emergency_origin, emergency_angles;
			bool used_human_player = false; // Doesn't matter much here

			// Use FindEmergencySpawnPosition to get a spot *near* the target
			if (FindEmergencySpawnPosition(emergency_origin, emergency_angles, used_human_player, monster_typeId)) {
				// SpawnMonsterByTypeID returns the edict_t* on success
				monster = SpawnMonsterByTypeID(monster_typeId, emergency_origin, emergency_angles, true);
			}
		}

		// --- Fallback to general emergency spawn if targeting failed or no target ---
		if (!monster) {
			// EmergencySpawnMonster returns bool
			if (EmergencySpawnMonster(waveLevel, monster_typeId)) {
				spawnedCount++; // Count success
				// Update counters
				if (g_horde_local.num_to_spawn > 0) --g_horde_local.num_to_spawn;
				else if (g_horde_local.queued_monsters > 0) --g_horde_local.queued_monsters;
				// Check for overflow before incrementing g_totalMonstersInWave
				if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
					++g_totalMonstersInWave;
				}
				else if (developer->integer) {
					gi.Com_PrintFmt("Warning: g_totalMonstersInWave overflow prevented in SpawnRetaliationAmbush (Emergency).\n");
				}
				// Note: Can't easily apply champion logic here as EmergencySpawnMonster doesn't return edict_t*
				continue; // Skip champion logic for emergency fallback
			}
			else {
				// Both targeted and general emergency failed
				if (developer->integer > 1) {
					gi.Com_PrintFmt("SpawnRetaliationAmbush: Failed to spawn monster type {}\n", (int)monster_typeId);
				}
				continue; // Try next monster in ambush
			}
		}

		// --- If monster was successfully spawned (likely via targeted FindEmergency...) ---
		// This block now only executes if the targeted spawn near player succeeded
		if (monster && monster->inuse) {
			spawnedCount++;
			// Update counters
			if (g_horde_local.num_to_spawn > 0) --g_horde_local.num_to_spawn;
			else if (g_horde_local.queued_monsters > 0) --g_horde_local.queued_monsters;
			// Check for overflow before incrementing g_totalMonstersInWave
			if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
				++g_totalMonstersInWave;
			}
			else if (developer->integer) {
				gi.Com_PrintFmt("Warning: g_totalMonstersInWave overflow prevented in SpawnRetaliationAmbush (Targeted).\n");
			}

			// Apply HIGH champion chance
			float champ_chance_ambush = 0.5f + (frandom() * 0.3f);
			if (waveLevel >= 3 && frandom() < champ_chance_ambush && !monster->monsterinfo.IS_BOSS) {
				monster->monsterinfo.bonus_flags |= BF_CHAMPION;
				ApplyMonsterBonusFlags(monster); // Apply flags/stats

				if (monster->inuse) { // Re-check after applying flags
					monster->item = G_HordePickItem();
					if (monster->item) monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP;
					else monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP; // Ensure flag set if no item
					champion_spawned_this_wave = true;
					// champion_spawn_cooldown = 5; // Optional cooldown
					if (developer->integer > 1) gi.Com_PrintFmt("Retaliation Ambush: Spawned Champion {}\n", monster->classname);
				}
				else {
					if (developer->integer) gi.Com_PrintFmt("SpawnRetaliationAmbush: Monster freed during ApplyMonsterBonusFlags!\n");
				}
			}
			// Other effects like armor are handled within SpawnMonsterByTypeID or EmergencySpawnMonster
		}
	} // End loop

	if (spawnedCount > 0 && developer->integer) {
		gi.Com_PrintFmt("HORDE: Retaliation Ambush Spawned {}/{} monsters.\n", spawnedCount, ambushSize);
	}
	return spawnedCount;
}

void HandleSpawnPhaseAggression(edict_t* monster) {
	if (!monster || !monster->inuse)
		return;

	// Check if monster was spawned during spawning state and died in that state
	if (monster->spawned_in_spawn_state && g_horde_local.state == horde_state_t::spawning) {
		// Track monsters killed during spawning
		static int32_t spawn_state_deaths = 0;
		static gtime_t last_death_time = 0_sec;

		// Reset counter if too much time has passed (8 seconds seems reasonable)
		if (level.time - last_death_time > 8_sec) {
			spawn_state_deaths = 0;
		}

		spawn_state_deaths++;
		last_death_time = level.time;

		if (developer->integer) {
			// Use a temporary variable to potentially show the count even if retaliation doesn't trigger yet
			std::string killer_name = "Unknown"; // Default if enemy isn't a player
			if (monster->enemy && monster->enemy->client) {
				killer_name = GetPlayerName(monster->enemy); // Use helper
			}
			gi.Com_PrintFmt("Monster killed during spawning state by {} ({} total recent)\n", killer_name.c_str(), spawn_state_deaths);
		}

		// Calculate spawn progress - how many monsters were spawned vs. total planned
		// Ensure total_planned is at least 1 to avoid division by zero issues
		const int32_t total_planned_raw = g_totalMonstersInWave + g_horde_local.num_to_spawn + g_horde_local.queued_monsters;
		const int32_t total_planned = (total_planned_raw > 0) ? total_planned_raw : 1;
		const float spawn_progress =
			static_cast<float>(g_totalMonstersInWave) / total_planned;

		constexpr int32_t MIN_RECENT_DEATHS_FOR_TRANSITION = 10;
		constexpr float MIN_SPAWN_PROGRESS_FOR_TRANSITION = 0.1f;
		constexpr uint16_t MIN_TOTAL_SPAWNED_FOR_TRANSITION = 12;

		if (spawn_state_deaths >= MIN_RECENT_DEATHS_FOR_TRANSITION
			&& (spawn_progress >= MIN_SPAWN_PROGRESS_FOR_TRANSITION || g_totalMonstersInWave >= MIN_TOTAL_SPAWNED_FOR_TRANSITION))
		{
			// --- ACTIVATE RETALIATION ---
			if (!g_horde_retaliation_active) {
				g_horde_retaliation_active = true;
				g_horde_retaliation_end_time = level.time + 10_sec; // Set duration

				// Identify the player who likely triggered this (the one who killed 'monster')
				g_horde_retaliation_target_player = nullptr;
				if (monster->enemy && monster->enemy->client/* && !(monster->enemy->svflags & SVF_BOT)*/) { // Check if enemy is a player
					g_horde_retaliation_target_player = monster->enemy;
				}
				else {
					// Fallback: Find highest damage/spree player if killer unclear or not a player
					PlayerStats top_player_stats; float percentage; // percentage unused here
					CalculateTopDamager(top_player_stats, percentage);
					if (top_player_stats.player && top_player_stats.player->client) {
						g_horde_retaliation_target_player = top_player_stats.player;
					}
				}

				if (developer->integer) {
					std::string target_player_name = GetPlayerName(g_horde_retaliation_target_player); // Use helper
					gi.Com_PrintFmt("HORDE: Retaliation Mode Activated for 10s (Target: {}). Triggered by rapid kills during spawning.\n",
						target_player_name.c_str());
				}

				// Trigger the immediate mini-ambush
				SpawnRetaliationAmbush(g_horde_local.current_map_size, g_horde_local.level, g_horde_retaliation_target_player);

				// --- START: Add monsters to the queue as reinforcement ---
				int32_t monsters_to_add_to_queue = 6 + (g_horde_local.level / 3); // Base 6, +1 every 3 levels
				// Optional: Add map size bonus
				if (g_horde_local.current_map_size.isBigMap)
					monsters_to_add_to_queue += 6;
				else if (g_horde_local.current_map_size.isMediumMap)
					monsters_to_add_to_queue += 4;

				g_horde_local.queued_monsters += monsters_to_add_to_queue;

				if (developer->integer) {
					gi.Com_PrintFmt("HORDE: Retaliation added {} monsters to the queue (New total: {}).\n",
						monsters_to_add_to_queue, g_horde_local.queued_monsters);
				}
				// --- END: Add monsters to the queue ---

				spawn_state_deaths = 0; // Reset the death counter
			}
			// --- DO NOT CHANGE g_horde_local.state here ---

		} // End if check for transition conditions

		monster->was_spawned_by_horde = false;    // Reset flag as it's processed
		monster->spawned_in_spawn_state = false; // Reset flag as it's processed
	} // End if spawned_in_spawn_state
}

// --- TryAlternativeSpawnPosition Function ---
bool TryAlternativeSpawnPosition(edict_t* spawn_point, horde::MonsterTypeID typeId, vec3_t& final_origin, vec3_t& final_angles) {
	PROFILE_SCOPE("TryAlternativeSpawnPosition");

	if (!spawn_point || !spawn_point->inuse || !is_valid_vector(spawn_point->s.origin)) {
		if (developer->integer > 1) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Invalid spawn_point.\n");
		return false;
	}

	const vec3_t base_origin = spawn_point->s.origin;
	const vec3_t base_angles = spawn_point->s.angles;
	bool is_flying = IsFlying(typeId);
	vec3_t predicted_mins, predicted_maxs;
	if (!GetPredictedScaledBounds(typeId, predicted_mins, predicted_maxs)) {
		// Fallback handled inside GetPredictedScaledBounds
	}

	// --- Try Predefined Offsets (including directly above) ---
	// Combine "above" offset with the predefined list and shuffle them (excluding index 0 which is "above")
	constexpr size_t NUM_PREDEFINED_OFFSETS = HordeConstants::NUM_HORDE_ALT_POSITIONS + 1; // +1 for the "above" offset
	std::array<vec3_t, NUM_PREDEFINED_OFFSETS> combined_offsets{};
	combined_offsets[0] = vec3_t{ 0, 0, 32 }; // "Above" offset first
	std::copy(HordeConstants::horde_alternative_positions.begin(), HordeConstants::horde_alternative_positions.end(), combined_offsets.begin() + 1);

	// Shuffle the predefined offsets (indices 1 to end) for less predictability
	for (size_t i = combined_offsets.size() - 1; i > 1; --i) { // Shuffle indices 1..N-1
		size_t j = irandom(1, i); // Random index between 1 and i (inclusive)
		if (i != j) {
			std::swap(combined_offsets[i], combined_offsets[j]);
		}
	}

	for (const auto& offset : combined_offsets) {
		vec3_t candidate_pos = base_origin + offset;
		vec3_t validated_pos = candidate_pos; // Copy for IsValidSpawnLocation

		// Check Line-of-Sight from base origin to candidate (skip for "above" offset)
		if (offset != vec3_t{ 0, 0, 32 }) { // Don't check LOS for straight up
			trace_t trace = gi.traceline(base_origin, candidate_pos, spawn_point, MASK_SOLID);
			if (trace.fraction < 0.9f) { // Require mostly clear LOS
				if (developer->integer > 2) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Predefined offset {} blocked by LOS.\n", offset);
				continue;
			}
		}

		// Check if the location itself is valid (geometry, floor, etc.)
		if (IsValidSpawnLocation(validated_pos, predicted_mins, predicted_maxs, is_flying)) {
			// Check proximity to recent spawns
			if (!IsPositionTooCloseToRecentSpawn(validated_pos)) {
				// Final check: Ensure no entities (player/monster/defense) are at the validated spot
				trace_t final_check = gi.trace(validated_pos, predicted_mins, predicted_maxs, validated_pos, nullptr, MASK_SOLID); // Check world geometry again just in case
				FilterData final_entity_check = { nullptr, 0 };
				gi.BoxEdicts(validated_pos + predicted_mins, validated_pos + predicted_maxs, nullptr, 0, AREA_SOLID, SpawnPointFilter, &final_entity_check);

				if (!final_check.startsolid && final_entity_check.count == 0) {
					// Success!
					final_origin = validated_pos;
					final_angles = base_angles; // Start with base angles
					// Adjust Yaw to face away from the offset direction (relative to base)
					if (offset.x != 0.0f || offset.y != 0.0f) {
						final_angles[YAW] = atan2f(offset.y, offset.x) * (180.0f / PI);
					}
					MarkPositionAsRecentlyUsed(final_origin); // Mark this specific spot
					if (developer->integer > 1) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Success using predefined/above offset (Final Pos: {}).\n", final_origin);
					return true;
				}
				else if (developer->integer > 2) {
					gi.Com_PrintFmt("TryAlternativeSpawnPosition: Predefined/above offset {} failed final solid/entity check.\n", validated_pos);
				}
			}
			else if (developer->integer > 2) {
				gi.Com_PrintFmt("TryAlternativeSpawnPosition: Predefined/above offset {} (validated {}) too close to recent spawn.\n", candidate_pos, validated_pos);
			}
		}
		// If IsValidSpawnLocation failed or other checks failed, continue to next offset
	}

	// --- Try Radial Offsets ---
	constexpr int RADIAL_ATTEMPTS = 20;
	constexpr float MIN_RADIUS = 40.0f;
	constexpr float MAX_RADIUS = 200.0f; // Increased max radius slightly

	for (int i = 0; i < RADIAL_ATTEMPTS; ++i) {
		float radius = frandom(MIN_RADIUS, MAX_RADIUS);
		float angle = frandom() * 2.0f * PI; // Full circle
		vec3_t offset = { cosf(angle) * radius, sinf(angle) * radius, frandom(-8.0f, 24.0f) }; // Random Z offset
		vec3_t candidate_pos = base_origin + offset;
		vec3_t validated_pos = candidate_pos; // Copy for IsValidSpawnLocation

		// Check Line-of-Sight from base origin to candidate
		trace_t trace = gi.traceline(base_origin, candidate_pos, spawn_point, MASK_SOLID);
		if (trace.fraction < 0.8f) { // Slightly more lenient LOS check for radial
			if (developer->integer > 2) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Radial offset {} blocked by LOS.\n", offset);
			continue;
		}

		// Check if the location itself is valid
		if (IsValidSpawnLocation(validated_pos, predicted_mins, predicted_maxs, is_flying)) {
			// Check proximity to recent spawns
			if (!IsPositionTooCloseToRecentSpawn(validated_pos)) {
				// Final check: Ensure no entities are at the validated spot
				trace_t final_check = gi.trace(validated_pos, predicted_mins, predicted_maxs, validated_pos, nullptr, MASK_SOLID);
				FilterData final_entity_check = { nullptr, 0 };
				gi.BoxEdicts(validated_pos + predicted_mins, validated_pos + predicted_maxs, nullptr, 0, AREA_SOLID, SpawnPointFilter, &final_entity_check);

				if (!final_check.startsolid && final_entity_check.count == 0) {
					// Success!
					final_origin = validated_pos;
					final_angles = base_angles; // Start with base angles
					final_angles[YAW] = angle * (180.0f / PI); // Set Yaw to face direction of offset
					MarkPositionAsRecentlyUsed(final_origin); // Mark this specific spot
					if (developer->integer > 1) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Success using radial offset (Final Pos: {}).\n", final_origin);
					return true;
				}
				else if (developer->integer > 2) {
					gi.Com_PrintFmt("TryAlternativeSpawnPosition: Radial offset {} failed final solid/entity check.\n", validated_pos);
				}
			}
			else if (developer->integer > 2) {
				gi.Com_PrintFmt("TryAlternativeSpawnPosition: Radial offset {} (validated {}) too close to recent spawn.\n", candidate_pos, validated_pos);
			}
		}
		// If IsValidSpawnLocation failed or other checks failed, continue to next attempt
	}

	// If all attempts failed
	if (developer->integer > 1) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Failed for point {}.\n", (int)(spawn_point - g_edicts));
	return false;
}

// TypeID-based EmergencySpawnMonster
bool EmergencySpawnMonster(const int32_t levelNum, horde::MonsterTypeID typeId) {

	PROFILE_SCOPE("EmergencySpawnMonster");


	const char* monster_classname = horde::MonsterTypeRegistry::GetClassname(typeId);
	if (!monster_classname) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Invalid TypeID {}\n", static_cast<int>(typeId));
		}
		return false;
	}

	vec3_t emergency_origin, emergency_angles;
	bool used_human_player = false;
	vec3_t predicted_mins, predicted_maxs;
	if (!GetPredictedScaledBounds(typeId, predicted_mins, predicted_maxs)) {
		// Fallback handled inside GetPredictedScaledBounds
	}
	bool is_flying = IsFlying(typeId);

	// Find a suitable position near a player
	if (!FindEmergencySpawnPosition(emergency_origin, emergency_angles, used_human_player, typeId)) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Could not find valid position for TypeID {}\n", static_cast<int>(typeId));
		}
		return false;
	}

	// Validate the found position again using IsValidSpawnLocation
	vec3_t final_valid_pos = emergency_origin; // Copy before validation potentially modifies it
	if (!IsValidSpawnLocation(final_valid_pos, predicted_mins, predicted_maxs, is_flying)) {
		if (developer->integer) {
			// Use {} for vec3_t formatting
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Position {} found by FindEmergency failed final IsValidSpawnLocation.\n", emergency_origin);
		}
		return false;
	}
	emergency_origin = final_valid_pos; // Use the validated (potentially adjusted) position

	// Create the base monster entity
	edict_t* monster = CreateBaseHordeMonster(typeId, emergency_origin, emergency_angles);
	if (!monster) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: CreateBaseHordeMonster failed for TypeID {}\n", static_cast<int>(typeId));
		}
		return false;
	}

	// Set classname and position again (redundant but safe)
	monster->classname = monster_classname;
	monster->s.origin = emergency_origin;
	monster->s.angles = emergency_angles;

	// Call the spawn function
	monster->solid = SOLID_NOT; // Temporarily non-solid for spawn function
	ED_CallSpawn(monster);

	// Check if spawn function freed the entity
	if (!monster->inuse) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: ED_CallSpawn failed for {}\n", monster_classname);
		}
		return false; // Entity was freed during spawn
	}

	// Restore solidity and link
	monster->solid = SOLID_BBOX;
	gi.linkentity(monster);

	// Post-link stuck check
	trace_t post_link_trace = gi.trace(monster->s.origin, monster->mins, monster->maxs,
		monster->s.origin, monster, MASK_SOLID);
	bool spawn_was_stuck = post_link_trace.startsolid;

	if (spawn_was_stuck) {
		if (!monster->monsterinfo.was_stuck) {
			edict_t* blocker = post_link_trace.ent;
			if (developer->integer) {
				// Use {} for vec3_t formatting and check blocker->classname
				gi.Com_PrintFmt("EMERGENCY SPAWN: WARNING - Monster ({}) stuck (Post-Link Check ONLY) at {}. Blocker: {}. Flagging for teleport.\n",
					monster->classname, monster->s.origin,
					blocker ? (blocker->classname ? blocker->classname : "unknown") : "world/unknown");
			}
			monster->monsterinfo.was_stuck = true;
			monster->monsterinfo.stuck_check_time = level.time;
			// Let monster_think handle the teleport check after grace period
		}
		else if (developer->integer > 1) {
			gi.Com_PrintFmt("EmergencySpawnMonster: {} confirmed stuck (already flagged by monster_start_go).\n", monster->classname);
		}
	}

	monster->monsterinfo.spawn_complete_time = level.time;

	// Apply bonuses (Champion, Armor, Item Drop)
	float champion_chance = 0.2f;
	if (g_horde_retaliation_active) champion_chance = 0.4f; // Higher chance during retaliation
	if (levelNum >= 3 && frandom() < champion_chance && !monster->monsterinfo.IS_BOSS) {
		int flag_type = irandom(0, 5); // Random bonus type
		switch (flag_type) {
		case 0: monster->monsterinfo.bonus_flags |= BF_CHAMPION; break;
		case 1: monster->monsterinfo.bonus_flags |= BF_CORRUPTED; break;
		case 2: monster->monsterinfo.bonus_flags |= BF_BERSERKING; break;
		case 3: monster->monsterinfo.bonus_flags |= BF_POSSESSED; break;
		case 4: monster->monsterinfo.bonus_flags |= BF_STYGIAN; break;
		default: break; // Should not happen
		}
		if (monster->monsterinfo.bonus_flags != BF_NONE) {
			ApplyMonsterBonusFlags(monster);
			if (!monster->inuse) return false; // Monster might be freed by bonus application
			monster->item = G_HordePickItem();
			if (monster->item) monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP;
			else monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP; // Ensure flag set if no item
		}
	}

	// Apply armor if applicable and not already set
	if (levelNum >= 14 && monster->monsterinfo.power_armor_type == IT_NULL && monster->monsterinfo.armor_type == IT_NULL) {
		SetMonsterArmor(monster);
		if (!monster->inuse) return false; // Monster might be freed
	}

	// Set item drop chance (if not champion)
	if (!(monster->monsterinfo.bonus_flags & BF_CHAMPION)) {
		const float drop_chance = levelNum <= 5 ? 0.8f : (levelNum <= 8 ? 0.6f : 0.45f);
		if (frandom() < drop_chance) {
			monster->item = G_HordePickItem();
			if (monster->item) monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP;
			else monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;
		}
		else {
			monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;
		}
	}
	else { // Ensure champion drop flag matches item presence
		if (!monster->item) monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;
		else monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP;
	}

	// Apply spawn effects
	SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
	gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

	// Increment total monster count for the wave (with overflow check)
	if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
		g_totalMonstersInWave++;
	}
	else if (developer->integer) {
		gi.Com_PrintFmt("Warning: g_totalMonstersInWave overflow prevented in EmergencySpawnMonster.\n");
	}

	if (developer->integer) {
		gi.Com_PrintFmt("EMERGENCY SPAWN SUCCESSFUL: Spawned {} at emergency position\n",
			monster->classname);
	}

	return true;
}
// Modified ShouldTriggerAmbushSpawn function for more frequent ambushes
bool ShouldTriggerAmbushSpawn() {
	// Static variables for tracking time-based cooldowns


	// Only consider ambush spawning after wave 3
	if (current_wave_level < 3) {
		return false;
	}

	// Check global cooldown (25 seconds between ambushes)
	if (level.time - last_ambush_time < 25_sec) {
		return false;
	}

	// Check short-term cooldown (random 3-7 seconds between attempts)
	if (level.time < ambush_cooldown_end) {
		return false;
	}

	// Calculate base chance
	float baseChance = 0.08f + (waves_since_ambush * 0.03f);

	// Wave level modifier (capped at 45%)
	const int cappedLevel = (current_wave_level > 25) ? 25 : current_wave_level;
	baseChance += (cappedLevel - 3) * 0.015f;
	baseChance = std::min(baseChance, 0.45f);

	// Player performance bonus
	float playerBonus = 0.0f;
	int playerCount = 0;
	for (auto* player : active_players_no_spect()) {
		if (player && player->inuse && player->health > 0) {
			if (player->health >= 125 || player->client->resp.spree >= 50) {
				playerBonus += 0.04f;
			}
			playerCount++;
		}
	}

	// Apply player bonus with cap
	if (playerCount > 0) {
		baseChance += std::min(playerBonus, 0.15f);
	}

	// Final chance roll
	if (frandom() < baseChance) {
		// Update timestamps
		last_ambush_time = level.time;
		ambush_cooldown_end = level.time + random_time(3_sec, 7_sec);
		waves_since_ambush = 0;
		return true;
	}
	else {
		// Set short cooldown even on failure to prevent spam checks
		ambush_cooldown_end = level.time + random_time(1_sec, 3_sec);
		return false;
	}
}

// Updated SpawnAmbushMonsters to use improved emergency spawning with TypeIDs
int SpawnAmbushMonsters(const horde::MapSize& mapSize, int32_t waveLevel) {
	if (developer->integer) {
		//	gi.Com_PrintFmt("DEBUG: Starting SpawnAmbushMonsters\n");
	}

	// Determine monster type ID - try to get a valid monster for current wave
	horde::MonsterTypeID monster_typeId = horde::MonsterTypeID::UNKNOWN;

	// Try to get a valid monster type ID from existing spawn points
	for (auto* point : monster_spawn_points()) {
		if (point && point->inuse) {
			// Use TypeID version for more efficient selection
			monster_typeId = G_HordePickMonsterType(point);
			if (monster_typeId != horde::MonsterTypeID::UNKNOWN) {
				// Found a valid type ID, no need to get classname here
				break;
			}
		}
	}

	// Fallback to appropriate monster type ID if none found
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
			// No need to get classname here
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
			// No need to get classname here
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

	// Add safety counter to prevent infinite loops
	int safety_counter = 0;
	const int MAX_TOTAL_ITERATIONS = 100;

	// Spawn ambush monsters
	for (int i = 0; i < ambushSize && consecutive_failures < MAX_FAILURES_PER_POSITION; /* i incremented below */) {
		// Add safety check to prevent infinite loops
		if (safety_counter++ > MAX_TOTAL_ITERATIONS) {
			gi.Com_PrintFmt("WARNING: Emergency spawn safety limit reached in SpawnAmbushMonsters\n");
			break;
		}

		// Check if we need to wait after a failed attempt
		if (consecutive_failures > 0) {
			if (level.time < last_failed_spawn_time + SPAWN_RETRY_DELAY) {
				// Postpone this attempt, don't increment 'i' yet, but continue outer loop check
				// To avoid busy-waiting, we only check periodically
				if (safety_counter % 10 != 0) {
					continue; // Skip this frame's attempt
				}
			}
		}

		// Use the TypeID version for more efficient spawning
		// Check if a valid type ID was determined before calling EmergencySpawnMonster
		if (monster_typeId != horde::MonsterTypeID::UNKNOWN &&
			EmergencySpawnMonster(waveLevel, monster_typeId)) { // Pass the ID directly
			ambushSuccessCount++;
			consecutive_failures = 0; // Reset failures on success

			// Update spawn counters
			if (g_horde_local.num_to_spawn > 0) {
				--g_horde_local.num_to_spawn;
			}
			else if (g_horde_local.queued_monsters > 0) {
				--g_horde_local.queued_monsters;
			}

			// Update total monsters counter (with overflow check)
			if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
				++g_totalMonstersInWave;
			}
			else if (developer->integer) {
				gi.Com_PrintFmt("Warning: g_totalMonstersInWave overflow prevented in SpawnAmbushMonsters.\n");
			}
			i++; // Increment loop counter ONLY on successful spawn attempt
		}
		else {
			// Track failures to avoid getting stuck in endless attempts
			consecutive_failures++;
			last_failed_spawn_time = level.time; // Record time of failure
			// Do NOT increment 'i' here, we retry for the same monster index
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
	else if (consecutive_failures >= MAX_FAILURES_PER_POSITION && developer->integer) {
		// Log if the loop terminated due to failures
		gi.Com_PrintFmt("SpawnAmbushMonsters: Loop terminated due to {} consecutive failures.\n", consecutive_failures);
	}


	if (developer->integer) {
		gi.Com_PrintFmt("DEBUG: Finished SpawnAmbushMonsters, successfully spawned: {}\n", ambushSuccessCount);
	}

	return ambushSuccessCount;
}

// Forward declarations for new helper functions
static void RebuildSpawnPointCacheIfNeeded();
static bool CheckHardCapAndLog(int32_t activeMonsters, int32_t hardCap, int32_t softCap, horde_state_t currentState, int32_t currentLevel);
static bool TrySpawnAmbush(const horde::MapSize& mapSize, int32_t currentLevel, int32_t activeMonsters, int32_t softCap);
static int32_t ManageSpawnCountsAndQueue(const horde::MapSize& mapSize, int32_t availableSpace);
static void DetermineSpawnStrategy(const horde::MapSize& mapSize, int32_t& out_spawnable_this_call, bool& out_use_emergency_spawn, bool& out_recovery_mode_active, float& out_champion_chance, int32_t availableSpace, int32_t currentLevel);
static int ExecuteEmergencySpawnProcedure(int32_t spawnable_this_call, int32_t currentLevel, edict_t*& last_spawned_this_call);
static int ExecuteNormalSpawnProcedure(int32_t spawnable_this_call, int32_t currentLevel, float champion_chance, edict_t*& last_spawned_this_call);
static bool AttemptSpawnSingleMonster(edict_t*& last_spawned_this_call, int32_t currentLevel, float champion_chance, bool recovery_mode_active);
static bool ValidateSpawnPointForMonster(edict_t* spawn_point, const SpawnPointData& sp_data, gtime_t current_time, bool recovery_mode_active);
static bool FindValidSpotAndSpawn(edict_t* spawn_point, horde::MonsterTypeID monster_type, int32_t currentLevel, float champion_chance, edict_t*& out_monster);

//-----------------------------------------------------
// SpawnMonsters - Main function (Refactored)
//-----------------------------------------------------
edict_t* SpawnMonsters() {
    if (level.intermissiontime)
        return nullptr;
    if (developer->integer == 2) return nullptr; // Early dev check

    // --- Cache Globals (Read-only for this scope) ---
    const gtime_t current_time = level.time; // Though not directly used here, sub-functions might
    const horde::MapSize& mapSize = g_horde_local.current_map_size;
    const horde_state_t current_state = g_horde_local.state;
    const int32_t currentLevel = g_horde_local.level;

    // --- 1. Spawn Point Cache Management ---
    RebuildSpawnPointCacheIfNeeded();
    if (g_potential_spawn_points.empty()) {
        if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: No potential spawn points. Cannot spawn.\n");
        g_consecutive_spawn_failures++;
        return nullptr;
    }

    // --- 2. Calculate Monster Caps & Hard Cap Check ---
    const int32_t activeMonsters = CalculateRemainingMonsters();
    const int32_t softCap = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap :
        (mapSize.isSmallMap ? HordeConstants::MAX_MONSTERS_SMALL_MAP : (mapSize.isBigMap ? HordeConstants::MAX_MONSTERS_BIG_MAP : HordeConstants::MAX_MONSTERS_MEDIUM_MAP));
    const int32_t hardCap = static_cast<int32_t>(softCap * 1.2f);

    if (CheckHardCapAndLog(activeMonsters, hardCap, softCap, current_state, currentLevel)) {
        return nullptr; // Hard cap reached
    }

    // --- 3. Ambush System ---
    if (TrySpawnAmbush(mapSize, currentLevel, activeMonsters, softCap)) {
        SetNextMonsterSpawnTime(mapSize); // Set time for next regular spawn check
        return nullptr; // Ambush handled, skip regular spawn this cycle
    }

    // --- 4. Spawn Quota Management ---
    int32_t availableSpace = softCap - activeMonsters;
    if (availableSpace <= 0) {
        if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Soft monster cap reached ({}/{}), PlayerAdjustedCap={}\n", activeMonsters, softCap, g_adjusted_monster_cap);
        return nullptr;
    }
    availableSpace = ManageSpawnCountsAndQueue(mapSize, availableSpace);

    if (g_horde_local.num_to_spawn <= 0) {
        if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: No monsters in spawn pool after queue check.\n");
        if (g_horde_local.queued_monsters <= 0 && current_state == horde_state_t::spawning && !next_wave_message_sent) {
            VerifyAndAdjustBots();
            gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Fully Deployed.\nWave Level: {}\n", currentLevel);
            next_wave_message_sent = true;
            g_horde_local.state = horde_state_t::active_wave;
        }
        return nullptr;
    }

    // --- 5. Spawn Strategy Determination ---
    int32_t spawnable_this_call;
    bool use_emergency_spawn;
    // bool recovery_mode_active; // g_recovery_mode_active is global
    float champion_chance;
    DetermineSpawnStrategy(mapSize, spawnable_this_call, use_emergency_spawn, g_recovery_mode_active, champion_chance, availableSpace, currentLevel);


    // --- 6. Monster Spawning Execution ---
    edict_t* last_spawned_this_call = nullptr;
    int spawned_count_this_call = 0;

    if (use_emergency_spawn) {
        spawned_count_this_call = ExecuteEmergencySpawnProcedure(spawnable_this_call, currentLevel, last_spawned_this_call);
    } else {
        spawned_count_this_call = ExecuteNormalSpawnProcedure(spawnable_this_call, currentLevel, champion_chance, last_spawned_this_call);
    }

    // --- Post-Spawn Processing ---
    if (spawned_count_this_call == 0 && spawnable_this_call > 0 && !use_emergency_spawn) {
        if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: Entire batch failed. Consecutive failures: {}\n", g_consecutive_spawn_failures);
    }

    // --- 7. Final Actions ---
    SetNextMonsterSpawnTime(mapSize);
    if (champion_spawn_cooldown > 0) {
        champion_spawn_cooldown--;
    }

    return last_spawned_this_call;
}

// --- Helper Function Implementations ---

static void RebuildSpawnPointCacheIfNeeded() {
    if (!g_spawn_points_cached || need_spawn_cache_reset) {
        g_potential_spawn_points.clear();
        g_potential_spawn_points.reserve(MAX_SPAWN_POINTS);
        for (auto* point : monster_spawn_points()) {
            if (point && point->inuse && point->classname && strcmp(point->classname, "info_player_deathmatch") == 0 && is_valid_vector(point->s.origin)) {
                g_potential_spawn_points.push_back(point);
            }
        }
        if (!g_potential_spawn_points.empty()) {
            for (size_t i = g_potential_spawn_points.size() - 1; i > 0; --i) {
                size_t j = irandom(0, i);
                if (i != j) {
                    std::swap(g_potential_spawn_points[i], g_potential_spawn_points[j]);
                }
            }
        }
        g_spawn_point_shuffle_index = 0;
        g_spawn_points_cached = true;
        need_spawn_cache_reset = false;
        g_consecutive_spawn_failures = 0;
        if (developer->integer) gi.Com_PrintFmt("Spawn Point Cache Rebuilt: {} points.\n", g_potential_spawn_points.size());
    }
}

static bool CheckHardCapAndLog(int32_t activeMonsters, int32_t hardCap, int32_t softCap, horde_state_t currentState, int32_t currentLevel) {
    if (activeMonsters >= hardCap) {
        if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Hard cap ({}/{}), SoftCap={}, AdjustedCap={}\n", activeMonsters, hardCap, softCap, g_adjusted_monster_cap);
        if (currentState == horde_state_t::spawning && !next_wave_message_sent) {
            VerifyAndAdjustBots();
            gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Deployed (Hard Cap).\nWave: {}\n", currentLevel);
            next_wave_message_sent = true;
            g_horde_local.state = horde_state_t::active_wave;
        }
        g_horde_local.num_to_spawn = 0;
        return true; // Hard cap reached
    }
    return false; // Hard cap not reached
}

static bool TrySpawnAmbush(const horde::MapSize& mapSize, int32_t currentLevel, int32_t activeMonsters, int32_t softCap) {
    if (g_horde_local.state == horde_state_t::active_wave && activeMonsters < softCap && ShouldTriggerAmbushSpawn()) {
        int spawnedCount = SpawnAmbushMonsters(mapSize, currentLevel);
        if (spawnedCount > 0) {
            g_consecutive_spawn_failures = 0;
            return true; // Ambush spawned
        }
    }
    return false; // No ambush or ambush failed
}

static int32_t ManageSpawnCountsAndQueue(const horde::MapSize& mapSize, int32_t availableSpace) {
    if (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters > 0) {
        const int32_t base_transfer = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
        const int32_t transfer_amount = std::min({ g_horde_local.queued_monsters, availableSpace, base_transfer });
        if (transfer_amount > 0) {
            g_horde_local.num_to_spawn += transfer_amount;
            g_horde_local.queued_monsters -= transfer_amount;
            if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Transferred {} from queue ({} left).\n", transfer_amount, g_horde_local.queued_monsters);
            return availableSpace - transfer_amount; // Return updated available space
        }
    }
    return availableSpace;
}

static void DetermineSpawnStrategy(const horde::MapSize& mapSize, int32_t& out_spawnable_this_call, bool& out_use_emergency_spawn, bool& out_recovery_mode_active_ref, float& out_champion_chance, int32_t availableSpace, int32_t currentLevel) {
    const int32_t base_batch = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
    out_spawnable_this_call = std::min({ g_horde_local.num_to_spawn, base_batch, availableSpace });

    out_use_emergency_spawn = false;
    if (g_consecutive_spawn_failures >= HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY) {
        if (developer->integer) gi.Com_PrintFmt("EMERGENCY SPAWN TRIGGERED: Failures={}.\n", g_consecutive_spawn_failures);
        out_use_emergency_spawn = true;
    } else if (g_consecutive_spawn_failures >= HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY && !out_recovery_mode_active_ref) {
        if (developer->integer) gi.Com_PrintFmt("RECOVERY MODE ACTIVATED: Failures={}.\n", g_consecutive_spawn_failures);
        out_recovery_mode_active_ref = true; // Modifies g_recovery_mode_active
        g_original_wave_type_before_recovery = current_wave_type;
        current_wave_type = HasWaveType(current_wave_type, MonsterWaveType::Flying) ? MonsterWaveType::Flying : MonsterWaveType::Ground;
    }

    out_champion_chance = 0.2f;
    if (g_horde_retaliation_active) {
        out_champion_chance = 0.40f;
        if (developer->integer > 1) gi.Com_PrintFmt("Retaliation: Champion chance {:.0f}%\n", out_champion_chance * 100.0f);
    }
}

static int ExecuteEmergencySpawnProcedure(int32_t spawnable_this_call, int32_t currentLevel, edict_t*& last_spawned_this_call) {
    int emergency_spawned_count = 0;
    last_spawned_this_call = nullptr; // Emergency spawn doesn't easily return edict

    for (int i = 0; i < std::min(spawnable_this_call, 3); ++i) {
        horde::MonsterTypeID emergency_type = (currentLevel < 10) ? horde::MonsterTypeID::SOLDIER : horde::MonsterTypeID::GUNNER;
        if (currentLevel >= 15) emergency_type = horde::MonsterTypeID::TANK;
        if (currentLevel >= 20) emergency_type = horde::MonsterTypeID::GLADIATOR;

        if (EmergencySpawnMonster(currentLevel, emergency_type)) {
            emergency_spawned_count++;
            if (g_horde_local.num_to_spawn > 0) --g_horde_local.num_to_spawn;
            else if (g_horde_local.queued_monsters > 0) --g_horde_local.queued_monsters;
            // g_totalMonstersInWave is incremented inside EmergencySpawnMonster
        } else {
            if (developer->integer) gi.Com_PrintFmt("EMERGENCY SPAWN FAILED for type {}.\n", (int)emergency_type);
            break;
        }
    }

    if (emergency_spawned_count > 0) {
        g_consecutive_spawn_failures = 0;
        g_recovery_mode_active = false;
        current_wave_type = g_original_wave_type_before_recovery;
        if (developer->integer) gi.Com_PrintFmt("EMERGENCY SPAWN: Spawned {}.\n", emergency_spawned_count);
    }
    return emergency_spawned_count;
}

static int ExecuteNormalSpawnProcedure(int32_t spawnable_this_call, int32_t currentLevel, float champion_chance, edict_t*& last_spawned_this_call) {
    int spawned_count_this_call = 0;
    last_spawned_this_call = nullptr;

    for (int i = 0; i < spawnable_this_call; ++i) {
        edict_t* spawned_monster_for_this_slot = nullptr;
        if (AttemptSpawnSingleMonster(spawned_monster_for_this_slot, currentLevel, champion_chance, g_recovery_mode_active)) {
            spawned_count_this_call++;
            if (spawned_monster_for_this_slot) { // It might be null if AttemptSpawnSingleMonster handles its own last_spawned
                last_spawned_this_call = spawned_monster_for_this_slot;
            }
        } else {
            // AttemptSpawnSingleMonster failed, it would have incremented g_consecutive_spawn_failures
            if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Failed to spawn monster for slot {} in batch.\n", i + 1);
        }
    }
    return spawned_count_this_call;
}

static bool AttemptSpawnSingleMonster(edict_t*& out_last_spawned_monster_this_attempt, int32_t currentLevel, float champion_chance, bool recovery_mode_active) {
    const size_t total_potential_points = g_potential_spawn_points.size();
    size_t points_checked_for_this_monster = 0;

    while (points_checked_for_this_monster < total_potential_points) {
        if (g_spawn_point_shuffle_index >= total_potential_points) g_spawn_point_shuffle_index = 0;
        edict_t* spawn_point = g_potential_spawn_points[g_spawn_point_shuffle_index++];
        points_checked_for_this_monster++;

        const auto& sp_data = spawnPointsData[spawn_point];
        if (!ValidateSpawnPointForMonster(spawn_point, sp_data, level.time, recovery_mode_active)) {
            // ValidateSpawnPointForMonster should handle IncreaseSpawnAttempts and g_consecutive_spawn_failures
            continue;
        }

        horde::MonsterTypeID monster_type = G_HordePickMonsterType(spawn_point);
        if (monster_type == horde::MonsterTypeID::UNKNOWN) {
            // G_HordePickMonsterType handles failure increments
            continue;
        }

        const bool monster_is_flying = IsFlying(monster_type);
        const bool is_flying_spawn_point = (spawn_point->style == 1);
        if (is_flying_spawn_point && !monster_is_flying) {
            IncreaseSpawnAttempts(spawn_point);
            g_consecutive_spawn_failures++;
            continue;
        }
        // Optional: if (!is_flying_spawn_point && monster_is_flying && !HasWaveType(current_wave_type, MonsterWaveType::Flying)) continue;

        edict_t* spawned_monster = nullptr;
        if (FindValidSpotAndSpawn(spawn_point, monster_type, currentLevel, champion_chance, spawned_monster)) {
            out_last_spawned_monster_this_attempt = spawned_monster; // This is the key output
            // Decrement counters
            if (g_horde_local.num_to_spawn > 0) --g_horde_local.num_to_spawn;
            // Increment total wave count
            if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
                ++g_totalMonstersInWave;
            } else if (developer->integer) {
                gi.Com_PrintFmt("Warning: g_totalMonstersInWave overflow.\n");
            }

            // Spawn effects
            SpawnGrow_Spawn(spawned_monster->s.origin, 80.0f, 10.0f);
            gi.sound(spawned_monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

            // Update trackers
            horde::g_monsterSpawnTracker.SetLastSpawnTime(monster_type, level.time);
            g_consecutive_spawn_failures = 0;
            if (g_recovery_mode_active) { // Use the passed param, not the global directly for this decision
                g_recovery_mode_active = false; // Modify global state
                current_wave_type = g_original_wave_type_before_recovery;
            }
            return true; // Success
        }
        // FindValidSpotAndSpawn failed, it would have handled failure increments
    }
    return false; // Failed to spawn after checking all points
}

static bool ValidateSpawnPointForMonster(edict_t* spawn_point, const SpawnPointData& sp_data, gtime_t current_time, bool recovery_mode_active_param) {
    // Check various cooldowns
    if ((sp_data.isTemporarilyDisabled && current_time < sp_data.cooldownEndsAt) ||
        (current_time < sp_data.teleport_cooldown) ||
        (current_time < sp_data.alternative_cooldown)) {
        return false;
    }

    // Check flying compatibility for the point itself
    const bool is_flying_spawn_point = (spawn_point->style == 1);
    MonsterWaveType type_to_check = recovery_mode_active_param ? current_wave_type : g_original_wave_type_before_recovery;
    if (type_to_check != MonsterWaveType::None && is_flying_spawn_point && !HasWaveType(type_to_check, MonsterWaveType::Flying)) {
        return false;
    }

    // Check distance to players
    for (const auto* const player : active_players_no_spect()) {
        if ((spawn_point->s.origin - player->s.origin).lengthSquared() < HordeConstants::MIN_PLAYER_DIST_SQ_CHECK) { // MIN_PLAYER_DIST_SQ_SPAWNPOINT was used before, check which is correct
            IncreaseSpawnAttempts(spawn_point);
            g_consecutive_spawn_failures++;
            return false;
        }
    }

    // Check if point is occupied by player/monster/defense
    if (IsSpawnPointOccupied(spawn_point)) {
        IncreaseSpawnAttempts(spawn_point);
        g_consecutive_spawn_failures++;
        return false;
    }
    return true;
}

static bool FindValidSpotAndSpawn(edict_t* spawn_point, horde::MonsterTypeID monster_type, int32_t currentLevel, float champion_chance, edict_t*& out_monster) {
    vec3_t final_origin;
    vec3_t final_angles;
    bool used_alternative = false;
    bool found_valid_spot = false;

    const bool monster_is_flying = IsFlying(monster_type);
    vec3_t predicted_mins, predicted_maxs;
    GetPredictedScaledBounds(monster_type, predicted_mins, predicted_maxs); // Assumes this always sets out_mins/maxs

    // Try direct spawn first
    vec3_t direct_origin = spawn_point->s.origin;
    if (IsValidSpawnLocation(direct_origin, predicted_mins, predicted_maxs, monster_is_flying)) {
        FilterData final_check_data = { nullptr, 0 };
        const vec3_t check_mins = direct_origin + predicted_mins;
        const vec3_t check_maxs = direct_origin + predicted_maxs;
        gi.BoxEdicts(check_mins, check_maxs, nullptr, 0, AREA_SOLID, SpawnPointFilter, &final_check_data);
        if (final_check_data.count == 0) {
            final_origin = direct_origin;
            final_angles = spawn_point->s.angles;
            found_valid_spot = true;
        }
    }

    // If direct failed, try alternative positions
    const auto& sp_data = spawnPointsData[spawn_point]; // Need sp_data for alternative_cooldown
    if (!found_valid_spot && level.time >= sp_data.alternative_cooldown) {
        if (TryAlternativeSpawnPosition(spawn_point, monster_type, final_origin, final_angles)) {
            used_alternative = true;
            vec3_t alternative_origin_copy = final_origin;
            if (IsValidSpawnLocation(alternative_origin_copy, predicted_mins, predicted_maxs, monster_is_flying)) {
                final_origin = alternative_origin_copy;
                FilterData final_check_data_alt = { nullptr, 0 };
                const vec3_t check_mins_alt = final_origin + predicted_mins;
                const vec3_t check_maxs_alt = final_origin + predicted_maxs;
                gi.BoxEdicts(check_mins_alt, check_maxs_alt, nullptr, 0, AREA_SOLID, SpawnPointFilter, &final_check_data_alt);
                if (final_check_data_alt.count == 0) {
                    found_valid_spot = true;
                } else { ApplyAlternativePositionCooldown(spawn_point); g_consecutive_spawn_failures++; }
            } else { ApplyAlternativePositionCooldown(spawn_point); g_consecutive_spawn_failures++; }
        } else { ApplyAlternativePositionCooldown(spawn_point); g_consecutive_spawn_failures++; }
    } else if (!found_valid_spot) {
        IncreaseSpawnAttempts(spawn_point); g_consecutive_spawn_failures++;
    }

    if (found_valid_spot) {
        edict_t* monster = SpawnMonsterByTypeID(monster_type, final_origin, final_angles, true);
        if (monster) {
            bool monster_still_valid = true;
            bool is_champion = false;

            // Champion check
            if (monster_still_valid && currentLevel >= 3 && !champion_spawned_this_wave && champion_spawn_cooldown <= 0 && !monster->monsterinfo.IS_BOSS && frandom() < champion_chance) {
                monster->monsterinfo.bonus_flags |= BF_CHAMPION;
                ApplyMonsterBonusFlags(monster);
                if (!monster->inuse) { monster_still_valid = false; }
                else {
                    monster->item = G_HordePickItem();
                    if (monster->item) monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP; else monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;
                    champion_spawned_this_wave = true; champion_spawn_cooldown = irandom(15, 25);
                    gi.LocBroadcast_Print(PRINT_HIGH, "*** A {} has appeared! ***\n", GetDisplayName(monster).c_str());
                    is_champion = true;
                }
            }
            // Regular item drop
            if (monster_still_valid && !is_champion) {
                const float drop_chance = currentLevel <= 5 ? 0.8f : (currentLevel <= 8 ? 0.6f : 0.45f);
                if (frandom() < drop_chance) {
                    monster->item = G_HordePickItem();
                    if (!monster->inuse) { monster_still_valid = false; }
                    else { if (monster->item) monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP; else monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP; }
                } else { monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP; }
            }
            // Armor
            if (monster_still_valid && currentLevel >= 14 && monster->monsterinfo.power_armor_type == IT_NULL && monster->monsterinfo.armor_type == IT_NULL) {
                SetMonsterArmor(monster);
                if (!monster->inuse) { monster_still_valid = false; }
            }

            if (monster_still_valid) {
                out_monster = monster;
                if (used_alternative) ApplySuccessfulAlternativeCooldown(spawn_point);
                else OnSuccessfulSpawn(spawn_point);
                return true; // Success
            } else {
                if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Monster {} invalid after bonuses.\n", monster->classname ? monster->classname : "Unknown");
                g_consecutive_spawn_failures++; // Failed to fully spawn
                // Monster edict might be !inuse but not freed yet. If SpawnMonsterByTypeID returns it, it might be G_FreeEdict'd by caller if it's !inuse.
                // Or, ensure it's G_FreeEdict'd here if it became invalid. For now, assume caller handles it or it's just !inuse.
                return false;
            }
        } else { // SpawnMonsterByTypeID failed
            if (used_alternative) ApplyAlternativePositionCooldown(spawn_point);
            else IncreaseSpawnAttempts(spawn_point);
            g_consecutive_spawn_failures++;
            return false;
        }
    }
    return false; // No valid spot found
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

static void SetNextMonsterSpawnTime(const horde::MapSize& mapSize) {
	// Original spawn time ranges (Big maps are faster)
	constexpr std::array<std::pair<gtime_t, gtime_t>, 3> BASE_SPAWN_TIMES = { {
		{0.6_sec, 0.8_sec},  // Small maps
		{0.8_sec, 1.0_sec},  // Medium maps
		{0.4_sec, 0.6_sec}   // Big maps
	} };

	// Apply early wave modifier (waves 1-10) - slows down spawn rate initially
	float earlyWaveMultiplier = 1.0f;
	// Check level is valid before calculation
	if (g_horde_local.level >= 1 && g_horde_local.level <= 10) {
		// Linearly decreases interval multiplier from 2.0x at wave 1 down to 1.1x at wave 10.
		// After wave 10, the multiplier is 1.0x (no modification).
		earlyWaveMultiplier = 2.0f - ((static_cast<float>(g_horde_local.level) - 1.0f) * 0.1f);
		// Clamp just in case level somehow goes outside 1-10 despite the check
		earlyWaveMultiplier = std::clamp(earlyWaveMultiplier, 1.1f, 2.0f);
	}

	// Select base times based on map size
	const size_t mapIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);
	const auto& [base_min_time, base_max_time] = BASE_SPAWN_TIMES[mapIndex];

	// Apply the early wave multiplier to get the target time range
	const gtime_t min_time = gtime_t::from_sec(base_min_time.seconds() * earlyWaveMultiplier);
	const gtime_t max_time = gtime_t::from_sec(base_max_time.seconds() * earlyWaveMultiplier);

	// Calculate the random interval within the adjusted range
	// Ensure min_time <= max_time before calling random_time to avoid potential issues
	const gtime_t calculated_interval = (min_time <= max_time) ?
		random_time(min_time, max_time) :
		min_time; // Fallback to min_time if somehow inverted

	// --- CLAMPING ---
	// Ensure the final interval is never less than the absolute minimum allowed
	const gtime_t final_interval = std::max(calculated_interval, HordeConstants::MIN_MONSTER_SPAWN_INTERVAL);

	// Set the time for the next spawn attempt
	g_horde_local.monster_spawn_time = level.time + final_interval;

	//// Optional Debugging
	//if (developer->integer > 2) {
	//	gi.Com_PrintFmt("SetNextMonsterSpawnTime: Level={}, MapIdx={}, EarlyMult={:.2f}, Base=[{:.2f}-{:.2f}], Adjusted=[{:.2f}-{:.2f}], Calculated={:.2f}, Final={:.2f}\n",
	//		g_horde_local.level,
	//		mapIndex,
	//		earlyWaveMultiplier,
	//		base_min_time.seconds(), base_max_time.seconds(),
	//		min_time.seconds(), max_time.seconds(),
	//		calculated_interval.seconds(),
	//		final_interval.seconds());
	//}
}
// Usar enum class para mejorar la seguridad de tipos
enum class MessageType {
	Standard,
	Chaotic,
	Insane
};

void CalculateTopDamager(PlayerStats& topDamager, float& percentage) {
    constexpr int32_t MAX_DAMAGE = 100000;
    int32_t total_damage = 0;
    topDamager = PlayerStats(); // Reset stats
    topDamager.total_damage = -1; // Use -1 to ensure first valid player is picked

    for (const auto& player : active_players()) {
        if (!player || !player->client || !player->inuse)
            continue;

        const int32_t player_damage = static_cast<int32_t>(std::min(
            static_cast<int32_t>(player->client->total_damage),
            MAX_DAMAGE));

        if (player_damage > 0) {
            total_damage += player_damage;
        }

        if (player_damage > topDamager.total_damage) {
            topDamager.total_damage = player_damage;
            topDamager.player = player;
        }
    }

    // Reset damage if it was initialized to -1 and no one dealt damage
    if (topDamager.total_damage == -1) {
         topDamager.total_damage = 0;
    }

    // Calculate percentage (same as before)
    percentage = 0.0f;
    if (total_damage > 0 && topDamager.total_damage > 0) {
        percentage = std::min(
            (static_cast<float>(topDamager.total_damage) / total_damage) * 100.0f,
            100.0f);
        percentage = std::round(percentage * 100.0f) / 100.0f;
    }
}

struct RewardInfo {
	item_id_t item_id;
	int weight;  // Higher weight = more common

	// Optional: Add constructor for cleaner initialization below
	constexpr RewardInfo(item_id_t id, int w) : item_id(id), weight(w) {}
};

// Define the rewards ONLY ONCE in this array
static const std::array<RewardInfo, 3> TOP_DAMAGER_REWARDS = { {
	{IT_ITEM_BANDOLIER, 60},    // More common
	{IT_ITEM_SENTRYGUN, 30},     // Less common
	{IT_AMMO_NUKE, 2}    // More common
} };

// Pre-computed total reward weight, calculated directly from the single array
static const int TOTAL_REWARD_WEIGHT = [] {
	int total = 0;
	// Calculate sum directly from the array
	for (const auto& reward : TOP_DAMAGER_REWARDS) {
		// Ensure weight is positive to avoid issues
		if (reward.weight > 0) {
			total += reward.weight;
		}
	}
	// Return 1 if total is somehow zero to prevent division by zero later
	return (total > 0) ? total : 1;
	}(); // Immediately invoke the lambda

static bool GiveTopDamagerReward(const PlayerStats& topDamager, const std::string& playerName) {
	// Quick validation with early return
	if (!topDamager.player || !topDamager.player->inuse || !topDamager.player->client)
		return false;

	const int roll = irandom(1, TOTAL_REWARD_WEIGHT);
	item_id_t selectedItemId = TOP_DAMAGER_REWARDS[0].item_id; // Default fallback to the first item
	int currentWeight = 0;

	for (const auto& reward : TOP_DAMAGER_REWARDS) {
		if (reward.weight <= 0) continue; // Skip items with no weight

		currentWeight += reward.weight;
		if (roll <= currentWeight) {
			selectedItemId = reward.item_id;
			break; // Found the item
		}
	}

	// Get item by ID
	gitem_t* item = GetItemByIndex(selectedItemId);
	if (!item || !item->classname) {
		// Log error if item ID is invalid or item has no classname
		if (developer->integer) {
			gi.Com_PrintFmt("Error: GiveTopDamagerReward - Failed to get valid gitem_t for ID {}\n", static_cast<int>(selectedItemId));
		}
		return false;
	}


	// Spawn and give item directly to player 
	edict_t* entity = G_Spawn();
	if (!entity)
		return false;

	entity->classname = item->classname;
	entity->item = item;
	SpawnItem(entity, item, spawn_temp_t::empty);

	// Check if SpawnItem succeeded (entity might be freed if e.g., item shouldn't spawn in DM)
	if (!entity->inuse) {
		// SpawnItem already freed it, just return false
		return false;
	}

	// Give item to player
	Touch_Item(entity, topDamager.player, null_trace, true);
	// Touch_Item might free the entity if pickup is successful
	if (entity->inuse) {
		G_FreeEdict(entity); // Free if Touch_Item didn't consume it
	}


	// Announce reward
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
	for (uint32_t i = 1; i < globals.num_edicts; i++) {
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

void Horde_RunFrame() {
	if (level.intermissiontime)
		return;
	const horde::MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level;
	const horde_state_t currentState = g_horde_local.state;
	const gtime_t currentTime = level.time;

	// Regular maintenance
	CleanupSpawnPointCache(); // Consider if this is needed every frame
	CheckAndReduceSpawnCooldowns(); // Check for late-wave cooldown reduction

	// --- Retaliation Mode Timeout ---
	if (g_horde_retaliation_active && currentTime >= g_horde_retaliation_end_time) {
		g_horde_retaliation_active = false;
		g_horde_retaliation_end_time = 0_sec;
		g_horde_retaliation_target_player = nullptr;
		if (developer->integer) {
			gi.Com_PrintFmt("HORDE: Retaliation Mode Ended.\n");
		}
	}

	// --- Stuck Wave Failsafe ---
	static gtime_t last_wave_change_time = 0_sec;
	static int32_t last_wave_level_for_failsafe = 0; // Use a different variable name
	constexpr gtime_t WAVE_STUCK_TIMEOUT = 3_min;

	if (need_frame_timer_reset) { // Reset on map load/reset
		last_wave_change_time = 0_sec;
		last_wave_level_for_failsafe = 0;
		need_frame_timer_reset = false;
	}

	if (last_wave_level_for_failsafe != currentLevel) {
		last_wave_change_time = currentTime;
		last_wave_level_for_failsafe = currentLevel;
	}
	else if (currentState != horde_state_t::warmup && // Don't trigger during warmup
		currentTime > last_wave_change_time + WAVE_STUCK_TIMEOUT) {
		// Check if monsters are actually present before forcing
		if (GetStroggsNum() == 0) {
			if (developer->integer) {
				gi.Com_PrintFmt("CRITICAL: Wave {} stuck for over {}s with 0 monsters. Forcing progression.\n",
					currentLevel, WAVE_STUCK_TIMEOUT.seconds());
			}
			// Force transition to spawning the *next* wave
			g_horde_local.state = horde_state_t::spawning;
			g_horde_local.monster_spawn_time = currentTime + 1.5_sec; // Short delay before starting next
			Horde_InitLevel(currentLevel + 1); // Initialize the NEXT level
			// Reset failsafe timer for the new wave
			last_wave_change_time = currentTime;
			last_wave_level_for_failsafe = currentLevel + 1;
			return; // Exit frame processing after forcing transition
		}
		else {
			// Wave is long, but monsters exist. Reset timer to give more time.
			last_wave_change_time = currentTime;
			if (developer->integer > 1) {
				gi.Com_PrintFmt("Wave {} duration exceeded {}s, but monsters remain. Resetting failsafe timer.\n",
					currentLevel, WAVE_STUCK_TIMEOUT.seconds());
			}
		}
	}

	// Handle custom monster settings (dm_monsters cvar)
	if (dm_monsters && dm_monsters->integer >= 1) {
		g_horde_local.num_to_spawn = dm_monsters->integer;
		g_horde_local.queued_monsters = 0;
		ClampNumToSpawn(mapSize); // Ensure it respects calculated caps
	}

	bool waveEnded = false;
	WaveEndReason currentWaveEndReason = WaveEndReason::AllMonstersDead; // Default reason

	// STATE MACHINE
	switch (g_horde_local.state) {
	case horde_state_t::warmup:
		if (g_horde_local.warm_time < currentTime) {
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(1); // Start at level 1
			// current_wave_level is set inside Horde_InitLevel
			PlayWaveStartSound();
			DisplayWaveMessage();
		}
		break;

	case horde_state_t::spawning: {
		// Failsafe 1: Stuck in spawning state for too long (e.g., can't spawn boss)
		static gtime_t spawning_state_start_time = 0_sec;
		constexpr gtime_t SPAWNING_TIMEOUT = 25_sec;

		if (spawning_state_start_time == 0_sec) {
			spawning_state_start_time = currentTime;
		}
		else if (currentTime > spawning_state_start_time + SPAWNING_TIMEOUT) {
			if (!next_wave_message_sent) { // Only print/transition once
				VerifyAndAdjustBots();
				gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Deployment Finalized.\nWave Level: {}\n", currentLevel);
				next_wave_message_sent = true;
			}
			g_horde_local.state = horde_state_t::active_wave; // Force transition
			spawning_state_start_time = 0_sec; // Reset timer
			break; // Exit spawning state processing
		}

		// Failsafe 2: Independent wave time limit reached during spawning
		const gtime_t independentTimeLimit = g_independent_timer_start + g_lastParams.independentTimeThreshold;
		if (currentTime >= independentTimeLimit) {
			currentWaveEndReason = WaveEndReason::TimeLimitReached;
			waveEnded = true; // Signal wave end
			spawning_state_start_time = 0_sec; // Reset timer
			break; // Exit spawning state processing
		}

		// Check if it's time for a spawn action
		if (g_horde_local.monster_spawn_time <= currentTime) {
			// --- Boss Wave Specific Logic ---
			if (IsBossWave()) {
				// Failsafe 3: Boss spawn timeout
				static gtime_t boss_wave_start_time = 0_sec;
				constexpr gtime_t BOSS_SPAWN_TIMEOUT = 30_sec;

				if (boss_wave_start_time == 0_sec) {
					boss_wave_start_time = currentTime;
				}

				if (!boss_spawned_for_wave && currentTime > boss_wave_start_time + BOSS_SPAWN_TIMEOUT) {
					if (developer->integer) {
						gi.Com_PrintFmt("CRITICAL: Boss spawn timeout ({:.1f}s) reached for wave {}. Forcing spawn attempt.\n",
							BOSS_SPAWN_TIMEOUT.seconds(), currentLevel);
					}
					SpawnBossAutomatically();
					boss_wave_start_time = 0_sec; // Reset timer
					// boss_action_taken = true; // Removed
				}
				else if (!boss_spawned_for_wave) {
					SpawnBossAutomatically();
					boss_wave_start_time = 0_sec; // Reset timer
					// boss_action_taken = true; // Removed
				}
			} // End IsBossWave() check

			// --- Regular Monster Spawning Logic ---
			// Run if not a boss wave OR boss has spawned
			if (!IsBossWave() || boss_spawned_for_wave) {
				SpawnMonsters(); // Handles caps, queue transfer, batching internally
			}

			// Check if wave deployment is complete
			if (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters <= 0) {
				if (!next_wave_message_sent) {
					VerifyAndAdjustBots();
					gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Fully Deployed.\nWave Level: {}\n", currentLevel);
					next_wave_message_sent = true;
				}
				g_horde_local.state = horde_state_t::active_wave;
				spawning_state_start_time = 0_sec; // Reset failsafe timer
			}

			// Set time for the *next* spawn attempt cycle
			SetNextMonsterSpawnTime(mapSize);

		} // End if (g_horde_local.monster_spawn_time <= currentTime)
		break; // End spawning case
	} // End spawning block

	case horde_state_t::active_wave: {
		// Check wave completion conditions first (includes all monsters dead check)
		if (CheckRemainingMonstersCondition(mapSize, currentWaveEndReason)) {
			waveEnded = true;
			break;
		}

		// Spawn queued monsters periodically if needed and under cap
		if (g_horde_local.monster_spawn_time <= currentTime) {
			SpawnMonsters(); // Handles caps, queue transfer, batching internally
			SetNextMonsterSpawnTime(mapSize); // Set next check time
		}

		// Check for stuck queued monsters (less frequent check)
		static gtime_t last_queue_check = 0_sec;
		static int consecutive_stuck_checks = 0;
		constexpr gtime_t QUEUE_CHECK_INTERVAL = 2_sec;
		constexpr int MAX_STUCK_CHECKS = 3;

		if (currentTime > last_queue_check + QUEUE_CHECK_INTERVAL) {
			last_queue_check = currentTime;

			if (g_horde_local.queued_monsters < 0) { // Sanity check
				g_horde_local.queued_monsters = 0;
			}

			// If queue has monsters but spawn pool is empty (meaning they aren't transferring)
			if (g_horde_local.queued_monsters > 0 && g_horde_local.num_to_spawn == 0) {
				consecutive_stuck_checks++;
				if (consecutive_stuck_checks >= MAX_STUCK_CHECKS) {
					// Force transfer some monsters to unstick the queue
					const int32_t to_move = std::min(g_horde_local.queued_monsters, 5); // Move up to 5
					g_horde_local.num_to_spawn += to_move;
					g_horde_local.queued_monsters -= to_move;
					consecutive_stuck_checks = 0; // Reset counter
					if (developer->integer) {
						gi.Com_PrintFmt("Queue unstick: Moved {} monsters to spawn pool.\n", to_move);
					}
				}
			}
			else {
				consecutive_stuck_checks = 0; // Reset if queue is empty or transferring normally
			}
		}
		break; // End active_wave case
	}

	case horde_state_t::cleanup:
		// Wait for a short delay before transitioning to rest
		if (g_horde_local.monster_spawn_time < currentTime) {
			HandleWaveCleanupMessage(mapSize, currentWaveEndReason); // Set difficulty for next wave
			g_horde_local.warm_time = currentTime + random_time(0.8_sec, 1.5_sec); // Set rest duration
			g_horde_local.state = horde_state_t::rest;
			CheckAndResetDisabledSpawnPoints(); // Reactivate any disabled points
		}
		break;

	case horde_state_t::rest:
		// Wait for rest duration to end
		if (g_horde_local.warm_time < currentTime) {
			AnnounceIncomingWave(3_sec); // Announce next wave
			g_horde_local.state = horde_state_t::spawning; // Transition to spawning
			Horde_InitLevel(g_horde_local.level + 1); // Initialize next level state
			// Reset failsafe timer for the new wave
			last_wave_change_time = currentTime;
			last_wave_level_for_failsafe = g_horde_local.level;
		}
		break;
	} // End State Machine Switch

	// --- Post-State Machine Processing ---

	// Handle wave end transition
	if (waveEnded) {
		SendCleanupMessage(currentWaveEndReason); // Display stats, give rewards
		g_horde_local.monster_spawn_time = currentTime + 0.5_sec; // Short delay before cleanup actions
		g_horde_local.state = horde_state_t::cleanup; // Transition to cleanup state
		ResetWaveAdvanceState(); // Reset timers and flags for next wave
	}

	// Clear timed horde message if expired
	if (horde_message_end_time != 0_sec && currentTime >= horde_message_end_time) {
		ClearHordeMessage();
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

// Helper functionget stroggs alive on the map
int32_t GetStroggsNum() noexcept {
	return level.total_monsters - level.killed_monsters;
}

// Helper function to check if it's a boss wave
inline bool IsBossWave() noexcept {
	return g_horde_local.level >= 10 && g_horde_local.level % 5 == 0;
}

// Helper to get predicted *scaled* bounds for validation checks
// Returns false if typeId is invalid or info not found
bool GetPredictedScaledBounds(horde::MonsterTypeID typeId, vec3_t& out_mins, vec3_t& out_maxs) {
	const MonsterTypeInfo* info = nullptr;
	float scale = 1.0f; // Default scale

	// Find the MonsterTypeInfo for the given TypeID
	for (const auto& entry : monsterTypes) {
		if (entry.typeId == typeId) {
			info = &entry;
			scale = info->s_scale; // Get the intended scale from the struct
			break;
		}
	}

	if (!info) {
		// Fallback to generic constants if info not found
		out_mins = HordeConstants::VALIDATE_CHECK_MINS;
		out_maxs = HordeConstants::VALIDATE_CHECK_MAXS;
		if (developer->integer) gi.Com_PrintFmt("GetPredictedScaledBounds: WARN - MonsterTypeInfo not found for TypeID {}, using generic bounds.\n", (int)typeId);
		return false; // Indicate failure to find specific info
	}

	// --- FIXED: Apply Scaling ---
	// Calculate the scaled bounds using the retrieved scale
	out_mins = info->default_mins * scale;
	out_maxs = info->default_maxs * scale;
	// --- END FIXED ---

	// Optional: Add a sanity check for zero/negative scale
	if (scale <= 0.0f && developer->integer) {
		gi.Com_PrintFmt("GetPredictedScaledBounds: WARN - MonsterTypeID {} has invalid scale {:.2f}. Using unscaled bounds.\n", (int)typeId, scale);
		out_mins = info->default_mins; // Revert to default if scale is bad
		out_maxs = info->default_maxs;
	}

	return true; // Indicate success (even if fallback bounds were used due to bad scale)
}

bool Horde_TeleportMonster(edict_t* self, const vec3_t& destination_origin, const vec3_t& destination_angles, bool play_effects, bool force_despite_visibility = false)
{
	PROFILE_SCOPE("Horde_TeleportMonster");
	if (level.intermissiontime) {
		return false;
    }

	if (!self || !self->inuse || self->deadflag || !is_valid_vector(destination_origin) || !is_valid_vector(destination_angles)) {
        if (developer->integer > 1 && self) {
            gi.Com_PrintFmt("Horde_TeleportMonster: Basic validation failed for {}. InUse:{} Dead:{} DestOrigin:({:.1f},{:.1f},{:.1f}) DestAngles:({:.1f},{:.1f},{:.1f})\n",
                self->classname ? self->classname : "NO_CLASSNAME",
                self ? self->inuse : 0, self ? self->deadflag : 0,
                destination_origin.x, destination_origin.y, destination_origin.z,
                destination_angles.x, destination_angles.y, destination_angles.z);
        }
		return false;
	}

	if (self->monsterinfo.issummoned ||
		(!force_despite_visibility &&
         (self->enemy && self->enemy->inuse && visible(self, self->enemy, false))) ) {
        constexpr gtime_t VISIBLE_ENEMY_CANCEL_COOLDOWN = 1.5_sec;
        self->teleport_time = level.time + VISIBLE_ENEMY_CANCEL_COOLDOWN;
		if (developer->integer > 1) {
			gi.Com_PrintFmt("Horde_TeleportMonster: Teleport cancelled for {} - summoned or enemy visible (force_vis: {}). Applying short cooldown ({:.1f}s).\n",
                 self->classname ? self->classname : "UNKNOWN",
                 force_despite_visibility,
                 VISIBLE_ENEMY_CANCEL_COOLDOWN.seconds());
		}
		return false;
	}

	effects_t original_effects = self->s.effects;
	renderfx_t original_renderfx = self->s.renderfx;

	self->svflags |= SVF_NOCLIENT;
	self->s.effects = EF_NONE;
	self->s.renderfx = (original_renderfx & RF_IR_VISIBLE);
	gi.unlinkentity(self);

	const vec3_t old_origin = self->s.origin;
	const vec3_t old_angles = self->s.angles;
	const vec3_t old_velocity = self->velocity;

	self->s.origin = destination_origin;
	self->s.old_origin = destination_origin;
	self->s.angles = destination_angles;
	self->velocity = vec3_origin;

	vec3_t final_pos_after_validation = self->s.origin;
	horde::MonsterTypeID monsterTypeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
	bool is_flying_monster = IsFlying(monsterTypeId);
    vec3_t predicted_mins, predicted_maxs;
    if (!GetPredictedScaledBounds(monsterTypeId, predicted_mins, predicted_maxs)) {
        // Warning logged by GetPredictedScaledBounds
    }

	if (!IsValidSpawnLocation(final_pos_after_validation, predicted_mins, predicted_maxs, is_flying_monster))
	{
		self->s.origin = old_origin;
		self->s.old_origin = old_origin;
		self->s.angles = old_angles;
		self->velocity = old_velocity;
		self->s.effects = original_effects;
		self->s.renderfx = original_renderfx;
		self->svflags &= ~SVF_NOCLIENT;
		gi.linkentity(self);
        self->teleport_time = level.time + 0.5_sec;
        if (developer->integer > 1) {
            gi.Com_PrintFmt("Horde_TeleportMonster: IsValidSpawnLocation failed for {} at intended ({:.1f},{:.1f},{:.1f}). Restored to ({:.1f},{:.1f},{:.1f}).\n",
                self->classname ? self->classname : "UNKNOWN",
                destination_origin.x, destination_origin.y, destination_origin.z,
                old_origin.x, old_origin.y, old_origin.z);
        }
		return false;
	}
	self->s.origin = final_pos_after_validation;
	self->s.old_origin = final_pos_after_validation;

	self->s.effects = original_effects;
	self->s.renderfx = original_renderfx;
	self->svflags &= ~SVF_NOCLIENT;
	gi.linkentity(self);

	if (play_effects) {
		SpawnGrow_Spawn(self->s.origin, 80.0f, 10.0f);
        if (sound_spawn1) {
		    gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
        }
	}

	self->monsterinfo.was_stuck = false;
	self->monsterinfo.stuck_check_time = 0_sec;
	self->monsterinfo.react_to_damage_time = level.time;
	self->teleport_time = level.time + random_time(HordeConstants::MIN_TELEPORT_COOLDOWN_MONSTER, HordeConstants::MAX_TELEPORT_COOLDOWN_MONSTER);

    if (developer->integer > 1) {
        gtime_t time_until_next_teleport = self->teleport_time - level.time;
        gi.Com_PrintFmt("Horde_TeleportMonster: Successfully teleported {} to ({:.1f},{:.1f},{:.1f}). Next teleport possible in {:.1f}s.\n",
            self->classname ? self->classname : "UNKNOWN",
            self->s.origin.x, self->s.origin.y, self->s.origin.z,
            time_until_next_teleport.seconds());
    }
	return true;
}