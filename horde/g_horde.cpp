// Includes y definiciones relevantes
#include "../shared.h"
#include "../g_local.h"
#include "g_horde.h"
#include <set>
#include "g_horde_benefits.h"
#include "horde_ids.h"
#include <span>
#include "../g_laser.h"
#include "../profiler.h"

static std::unordered_set<horde::MonsterTypeID> g_precached_monster_types;
static bool g_full_precache_done = false;

static bool monsters_precached = false; 
MonsterWaveType current_wave_type = MonsterWaveType::None;
std::vector<const MonsterTypeInfo*> g_eligible_monsters_for_wave;

int32_t monsters_spawned_in_current_phase = 0;
int32_t initial_total_monsters_for_spawning_phase_timeout = 0;


// NEW state variables for time-slicing batches
static int32_t g_monsters_to_spawn_in_current_batch = 0;
static gtime_t g_next_single_monster_spawn_time = 0_sec;
static float g_champion_chance_for_current_batch = 0.2f; // Store champion chance for the batch

// State variables for time-slicing AMBUSH/RETALIATION batches
static int32_t g_monsters_to_spawn_in_current_ambush = 0;
static gtime_t g_next_single_ambush_monster_spawn_time = 0_sec;    

// Store the context for the current ambush
struct AmbushSpawnInfo {
    horde::MonsterTypeID typeId = horde::MonsterTypeID::UNKNOWN;
    float champion_chance = 0.0f;
    bool is_retaliation = false;
    edict_t* target_player = nullptr;
};
static AmbushSpawnInfo g_current_ambush_info;

// Cache for potentially valid spawn points (pointers)
static std::vector<edict_t *> g_potential_spawn_points;
static bool g_spawn_points_cached = false;
static size_t g_spawn_point_shuffle_index = 0; // Index for iterating shuffled list
static int32_t g_cached_flying_spawn_count = 0;


static bool g_special_high_level_monster_spawned_this_wave = false;

// State for failure tracking and recovery
static int g_consecutive_spawn_failures = 0;
static bool g_recovery_mode_active = false;
static MonsterWaveType g_original_wave_type_before_recovery = MonsterWaveType::None;

static bool need_spawn_cache_reset = false;
static bool need_frame_timer_reset = false;
static bool need_queue_monitor_reset = false;

// retaliation horde ( for when players are killing way too ez new/current wave in spawning state)

static bool g_horde_retaliation_active = false;
static gtime_t g_horde_retaliation_end_time = 0_sec;
// Optional: Store the targeted player edict for focus (can be nullptr)
static edict_t *g_horde_retaliation_target_player = nullptr;

// Ambush system tracking variables
static gtime_t last_ambush_time = 0_sec;
static gtime_t ambush_cooldown_end = 0_sec;
static int32_t waves_since_ambush = 0;
// static bool ambush_system_initialized = false;

// --- Recent Spawn Position Tracking ---
struct RecentSpawnPosition
{
	vec3_t position = {};
	gtime_t cooldown_until = 0_sec;
};
static constexpr size_t MAX_RECENT_POSITIONS = 32; // History for TryAlternativeSpawnPosition
static std::array<RecentSpawnPosition, MAX_RECENT_POSITIONS> g_recent_spawn_positions;
static size_t g_recent_position_index = 0;

// --- Recent Teleport Position Tracking ---
struct RecentTeleportPosition
{
	vec3_t position = {};
	gtime_t teleport_time = 0_sec;
};
static constexpr int MAX_RECENT_TELEPORT_LOCATIONS = 8; // History for CheckAndTeleportStuckMonster
static std::array<RecentTeleportPosition, MAX_RECENT_TELEPORT_LOCATIONS> g_recent_teleport_positions;
static int g_recent_teleport_index = 0;

// --- Horde Mode Constants ---
namespace HordeConstants
{
	// --- Gameplay Balance ---
	constexpr float TIME_REDUCTION_MULTIPLIER = 0.95f;
	constexpr float BASE_DIFFICULTY_MULTIPLIER = 1.1f;
	constexpr float PLAYER_COUNT_SCALE = 0.2f;

	// --- Monster Counts & Caps ---
	constexpr int8_t MAX_MONSTERS_BIG_MAP = 32;
	constexpr int8_t MAX_MONSTERS_MEDIUM_MAP = 16;
	constexpr int8_t MAX_MONSTERS_SMALL_MAP = 14;
	constexpr std::array<std::array<int32_t, 4>, 3> BASE_COUNTS = {{
		{{6, 8, 10, 12}},  // Small maps
		{{8, 12, 14, 16}}, // Medium maps
		{{15, 18, 23, 26}} // Large maps
	}};
	constexpr std::array<int32_t, 3> ADDITIONAL_SPAWNS = {8, 7, 12};

	// --- Spawn Point Cooldowns ---
	constexpr gtime_t MIN_GLOBAL_SPAWN_COOLDOWN = 1.5_sec;
	constexpr gtime_t MIN_INDIVIDUAL_SUCCESS_COOLDOWN = 0.5_sec;
	constexpr gtime_t MIN_INDIVIDUAL_FAILURE_COOLDOWN = 0.5_sec;
	constexpr gtime_t MIN_REDUCED_INDIVIDUAL_COOLDOWN = 0.5_sec;
	constexpr gtime_t SPAWN_POINT_INACTIVITY_RESET_THRESHOLD = 6.0_sec; // ADDED

	// --- Alternative Spawn Position Cooldowns & Logic ---
	constexpr gtime_t ALT_SPAWN_COOLDOWN_SHORT = 1.5_sec;
	constexpr gtime_t ALT_SPAWN_COOLDOWN_MEDIUM = 3.0_sec;
	constexpr gtime_t MIN_ALT_SUCCESS_COOLDOWN = 1.0_sec;
	constexpr gtime_t MIN_ALT_FAILURE_COOLDOWN = 1.0_sec;

	constexpr size_t NUM_HORDE_ALT_POSITIONS = 24; // Changed from 8 to 24

	constexpr std::array<vec3_t, NUM_HORDE_ALT_POSITIONS> horde_alternative_positions = {
		// --- Original 8 Positions ---
		vec3_t{40, 0, 8}, vec3_t{-40, 0, 8},
		vec3_t{0, 40, 8}, vec3_t{0, -40, 8},
		vec3_t{30, 30, 0}, vec3_t{-30, 30, 0},
		vec3_t{30, -30, 0}, vec3_t{-30, -30, 0},

		// --- New 8 Positions (Suggestions) ---
		// Further axial positions with slightly different Z
		vec3_t{60, 0, 4}, vec3_t{-60, 0, 4},
		vec3_t{0, 60, 4}, vec3_t{0, -60, 4},

		// Further diagonal positions, could also vary Z more
		vec3_t{50, 50, 2}, vec3_t{-50, 50, 2},
		vec3_t{50, -50, 2}, vec3_t{-50, -50, 2},

		// Alternative new 8 positions (more varied Z)
		vec3_t{20, 0, 16}, vec3_t{-20, 0, 16}, // Closer, higher up
		vec3_t{0, 20, 16}, vec3_t{0, -20, 16},
		vec3_t{45, 45, -4}, vec3_t{-45, 45, -4}, // Wider, slightly below origin if possible
		vec3_t{45, -45, -4}, vec3_t{-45, -45, -4}};
	// --- Monster Spawning Timing ---
	constexpr gtime_t MIN_MONSTER_SPAWN_INTERVAL = 0.8_sec;

	// --- Stuck Monster / Teleport Logic ---
	constexpr gtime_t STUCK_CHECK_TIME = 18_sec;
	constexpr gtime_t NO_DAMAGE_TIMEOUT = 32_sec;
	constexpr gtime_t MIN_TELEPORT_COOLDOWN_MONSTER = 12_sec;
	constexpr gtime_t MAX_TELEPORT_COOLDOWN_MONSTER = 20_sec;
	constexpr gtime_t GLOBAL_TELEPORT_RESET_INTERVAL = 12_sec;
	constexpr int MAX_TELEPORTS_PER_INTERVAL = 2;
	inline int g_teleport_rate_count = 0;					// Changed to inline static
	inline gtime_t g_teleport_rate_reset_time = level.time; // Changed to inline static

	// --- Proximity / Distance Checks ---
	constexpr vec3_t VALIDATE_CHECK_MINS = {-16, -16, -24};
	constexpr vec3_t VALIDATE_CHECK_MAXS = {16, 16, 32};
	constexpr float MIN_PLAYER_DIST_GENERATE = 200.0f;
	constexpr float MIN_PLAYER_DIST_CHECK = 360.0f;
	constexpr float MIN_PLAYER_DIST_SQ_CHECK = MIN_PLAYER_DIST_CHECK * MIN_PLAYER_DIST_CHECK;
	constexpr float MIN_PLAYER_DIST_SPAWNPOINT = 150.0f;
	constexpr float MIN_PLAYER_DIST_SQ_SPAWNPOINT = MIN_PLAYER_DIST_SPAWNPOINT * MIN_PLAYER_DIST_SPAWNPOINT;
	constexpr float MIN_RECENT_SPAWN_DIST = 120.0f;
	constexpr float MIN_RECENT_SPAWN_DIST_SQ = MIN_RECENT_SPAWN_DIST * MIN_RECENT_SPAWN_DIST;
	constexpr float MIN_RECENT_TELEPORT_DIST = 300.0f;
	constexpr float MIN_RECENT_TELEPORT_DIST_SQ = MIN_RECENT_TELEPORT_DIST * MIN_RECENT_TELEPORT_DIST;
	constexpr gtime_t RECENT_SPAWN_COOLDOWN = 5.0_sec;
	constexpr gtime_t RECENT_TELEPORT_COOLDOWN = 20.0_sec;

	// --- Failure Recovery ---
	constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY = 5;
	constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY = 10;

	// --- Wave Timing ---
	constexpr gtime_t BASE_MAX_WAVE_TIME = 125_sec;
	constexpr gtime_t TIME_INCREASE_PER_LEVEL = 3.0_sec;
	constexpr gtime_t BOSS_TIME_BONUS = 100_sec;
	constexpr int MONSTERS_FOR_AGGRESSIVE_REDUCTION = 6;
} // namespace HordeConstants

// --- Forward Declarations ---
static void Horde_InitLevel(const int32_t lvl);
static bool ApplyHordeBonuses(edict_t* monster, int32_t currentLevel, float champion_chance); // monster bonuses
void CalculateTopDamager(PlayerStats &topDamager, float &percentage);
[[nodiscard]] bool IsPositionPhysicallyValid(vec3_t& io_position, const vec3_t& monster_mins, const vec3_t& monster_maxs, bool is_flying, bool is_predefined_location = false);
bool CheckAndTeleportStuckMonster(edict_t *self);
bool FindEmergencySpawnPosition(vec3_t &position, vec3_t &angles, bool &used_human_player, horde::MonsterTypeID typeId, edict_t* specific_target = nullptr);
bool TryAlternativeSpawnPosition(edict_t *spawn_point, horde::MonsterTypeID typeId, vec3_t &final_origin, vec3_t &final_angles);
edict_t *SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t &origin, const vec3_t &angles, bool applyHordeFlags);
static void AnnounceIncomingWave(gtime_t duration = 3_sec);
bool EmergencySpawnMonster(const int32_t levelNum,
						   horde::MonsterTypeID typeId,
						   bool is_additional_monster,
						   float champion_chance_for_this_spawn);

// --- Helper Functions ---
void MarkPositionAsRecentlyUsed(const vec3_t &position)
{
	g_recent_spawn_positions[g_recent_position_index] = {
		position,
		level.time + HordeConstants::RECENT_SPAWN_COOLDOWN};
	g_recent_position_index = (g_recent_position_index + 1) % MAX_RECENT_POSITIONS;
}

bool IsPositionTooCloseToRecentSpawn(const vec3_t &position)
{
	const gtime_t current_time = level.time;
	for (const auto &recent : g_recent_spawn_positions)
	{
		if (recent.cooldown_until > current_time)
		{
			if ((position - recent.position).lengthSquared() < HordeConstants::MIN_RECENT_SPAWN_DIST_SQ)
			{
				return true;
			}
		}
	}
	return false;
}

void MarkPositionAsRecentlyTeleported(const vec3_t &position)
{
	g_recent_teleport_positions[g_recent_teleport_index] = {
		position,
		level.time + HordeConstants::RECENT_TELEPORT_COOLDOWN // Mark when it becomes not recent
	};
	g_recent_teleport_index = (g_recent_teleport_index + 1) % MAX_RECENT_TELEPORT_LOCATIONS;
}

bool IsPositionTooCloseToRecentTeleport(const vec3_t &position)
{
	const gtime_t current_time = level.time;
	for (const auto &recent : g_recent_teleport_positions)
	{
		// Check if the cooldown has expired (teleport_time stores the time *until* it's no longer recent)
		if (recent.teleport_time > current_time)
		{
			if ((position - recent.position).lengthSquared() < HordeConstants::MIN_RECENT_TELEPORT_DIST_SQ)
			{
				return true;
			}
		}
	}
	return false;
}

// (Keep ResetSpawnMonsterVars, ResetFrameTimers, ResetQueueMonitorVars as they were)
void ResetSpawnMonsterVars()
{
	need_spawn_cache_reset = true;
	horde::g_monsterSpawnTracker.Reset();
}

void ResetQueueMonitorVars()
{
	need_queue_monitor_reset = true;
	horde::g_spawnPointTimeTracker.Reset();
}

// --- Global/Static Variables ---
bool champion_spawned_this_wave = false;
// int champion_spawn_cooldown = 0;
static gtime_t champion_spawn_cooldown_ends_at = 0_sec;
int consistent_zero_counts = 0;
int counter_mismatch_frames = 0;
constexpr size_t MAX_SPAWN_POINTS = 32;

// (Keep SpawnPointData and SpawnPointDataArray structs as they were)
struct SpawnPointData
{
	uint16_t attempts = 0;
	gtime_t teleport_cooldown = 0_sec; // Added teleport_cooldown
	gtime_t lastSpawnTime = 0_sec;	   // Added lastSpawnTime
	bool isTemporarilyDisabled = false;
	gtime_t cooldownEndsAt = 0_sec;
	int32_t successfulSpawns = 0;

	// New fields for alternative position tracking
	uint16_t alternative_attempts = 0;			  // Added alternative_attempts
	gtime_t alternative_cooldown = 0_sec;		  // Added alternative_cooldown
	bool needs_long_alternative_cooldown = false; // Added needs_long_alternative_cooldown

	// Update the success rate calculation method to accept current_time
	float getSuccessRate(gtime_t current_time) const
	{
		if (attempts == 0)
			return 1.0f;
		// Use faster approximation - avoid division when possible
		const float time_factor = (current_time - lastSpawnTime).seconds() >= 5.0f ? 1.0f : (current_time - lastSpawnTime).seconds() * 0.2f;
		// Clamp time_factor to avoid excessive influence
		const float clamped_time_factor = std::min(time_factor, 1.0f);
		// Calculate base success rate, ensure attempts is not zero
		const float base_rate = (attempts > 0) ? (float(successfulSpawns) / float(attempts)) : 1.0f;

		// Combine base rate and time factor, ensuring result is between 0 and 1
		return std::clamp(base_rate + clamped_time_factor, 0.0f, 1.0f);
	}
};
struct SpawnPointDataArray
{
	SpawnPointData data[MAX_EDICTS];
	SpawnPointData &operator[](const edict_t *ent) { return data[ent - g_edicts]; }
	const SpawnPointData &operator[](const edict_t *ent) const { return data[ent - g_edicts]; }
	void clear()
	{
		for (auto &item : data)
			item = SpawnPointData{};
	}
};
SpawnPointDataArray spawnPointsData;

void ApplyAlternativePositionCooldown(edict_t *spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;
	auto &data = spawnPointsData[spawn_point];
	data.alternative_attempts++;
	gtime_t cooldown_duration;
	if (data.alternative_attempts <= 2)
		cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_SHORT;
	else if (data.alternative_attempts <= 5)
		cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_MEDIUM;
	else
	{
		cooldown_duration = 5.0_sec + gtime_t::from_sec(0.5f * (data.alternative_attempts - 5));
		cooldown_duration = std::min(cooldown_duration, 10.0_sec);
		if (data.alternative_attempts >= 8)
			data.needs_long_alternative_cooldown = true;
	}

	// Clamp the calculated alternative failure cooldown duration
	const gtime_t final_alt_duration = std::max(cooldown_duration, HordeConstants::MIN_ALT_FAILURE_COOLDOWN);
	data.alternative_cooldown = level.time + final_alt_duration;

	data.isTemporarilyDisabled = true; // Also disable original point shortly
	// Also clamp the normal point's shorter cooldown based on the *clamped* alternative duration
	const gtime_t final_normal_duration = std::max(final_alt_duration * 0.5f, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
	data.cooldownEndsAt = level.time + final_normal_duration;
	data.lastSpawnTime = level.time;

	if (developer->integer)
		gi.Com_PrintFmt("Alternative position cooldown applied to spawn at {}: {:.1f}s (attempts: {})\n", spawn_point->s.origin, final_alt_duration.seconds(), data.alternative_attempts);
}

void ApplySuccessfulAlternativeCooldown(edict_t *spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;
	auto &data = spawnPointsData[spawn_point];
	data.alternative_attempts = 0;
	data.needs_long_alternative_cooldown = false;
	// Ensure the 3.0s meets the minimum alternative success cooldown
	data.alternative_cooldown = level.time + std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN);
	if (developer->integer > 1)
		gi.Com_PrintFmt("Success cooldown applied to spawn at {}: {:.1f}s\n", spawn_point->s.origin, std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN).seconds());
}

// --- MODIFIED IncreaseSpawnAttempts ---
void IncreaseSpawnAttempts(edict_t *spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;
	auto &data = spawnPointsData[spawn_point];

	// --- FIX: Less aggressive reset for inactive points ---
	// Use HordeConstants::SPAWN_POINT_INACTIVITY_RESET_THRESHOLD
	if (level.time - data.lastSpawnTime > HordeConstants::SPAWN_POINT_INACTIVITY_RESET_THRESHOLD)
	{
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

	if (data.attempts >= max_attempts)
	{
		data.isTemporarilyDisabled = true;
		const float cooldown_factor = success_rate < 0.3f ? 1.5f : 0.75f;
		const float attempt_multiplier = data.attempts <= 8 ? data.attempts * 0.25f : 2.0f;
		calculated_duration = gtime_t::from_sec(cooldown_factor * attempt_multiplier);
		if (developer->integer == 1)
			gi.Com_PrintFmt("SpawnPoint at {} inactivated for adaptive cooldown.\n", spawn_point->s.origin);
	}
	else if ((data.attempts & 1) == 0)
	{ // Every 2 attempts
		calculated_duration = gtime_t::from_sec(0.2f * data.attempts);
	}

	if (calculated_duration > 0_sec)
	{
		const gtime_t final_duration = std::max(calculated_duration, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
		data.cooldownEndsAt = level.time + final_duration;
	}
	data.lastSpawnTime = level.time;
}

void OnSuccessfulSpawn(edict_t *spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;
	auto &data = spawnPointsData[spawn_point];
	data.successfulSpawns++;
	data.attempts = 0;
	data.isTemporarilyDisabled = false;
	// Use the minimum success cooldown constant
	data.cooldownEndsAt = level.time + HordeConstants::MIN_INDIVIDUAL_SUCCESS_COOLDOWN;
	horde::g_spawnPointTimeTracker.SetLastSpawnTime(spawn_point, level.time);
}
struct SpawnPointCache
{
	gtime_t last_check_time = 0_sec;
	vec3_t last_check_origin = {};
	gtime_t frame_time = 0_sec;			 // Frame time of the last check
	bool was_occupied_by_player = false; // True if a player was found in the last check
	bool has_obstacle = false;			 // True if a monster/defense was found (excluding player)
};

struct SpawnPointCacheArray
{
	SpawnPointCache data[MAX_EDICTS];
	SpawnPointCache &operator[](const edict_t *ent) { return data[ent - g_edicts]; }
	const SpawnPointCache &operator[](const edict_t *ent) const { return data[ent - g_edicts]; }
	void clear()
	{
		for (auto &item : data)
			item = SpawnPointCache{};
	}
};
static SpawnPointCacheArray spawn_point_cache;

// A dedicated struct to pass data to our unified BoxEdicts lambda.
// This is clearer than reusing a generic struct.
struct OccupiedCheckData {
    const edict_t* ignore_ent; // The entity to ignore in the check (usually the monster being spawned)
    SpawnPointCache* cache;    // A pointer to the cache entry we need to modify
};

/**
 * @brief Checks if a spawn point is occupied and updates a cache with detailed results.
 * 
 * This function is highly optimized to perform a single pass over nearby entities.
 * It prioritizes checking for players for an early exit, as a player block is a hard "no".
 * 
 * @param spawn_point The spawn point entity to check.
 * @param ignore_ent An optional entity to ignore during the check.
 * @return [[nodiscard]] bool - Returns true ONLY if a player/bot is directly occupying the space. 
 *                              Returns false otherwise. The caller should check the cache's `has_obstacle`
 *                              flag to see if a non-player obstacle (monster/defense) was found.
 */
[[nodiscard]] bool IsSpawnPointOccupied(const edict_t *spawn_point, const edict_t *ignore_ent)
{
	// --- 1. Basic Validation ---
	if (!spawn_point || !spawn_point->inuse || !is_valid_vector(spawn_point->s.origin))
	{
		if (developer->integer) {
			gi.Com_PrintFmt("Warning: IsSpawnPointOccupied called with invalid spawn_point or origin.\n");
		}
		return true; // Safer to assume occupied if the point itself is invalid.
	}

	// --- 2. Cache Check ---
	SpawnPointCache &cache = spawn_point_cache[spawn_point];
	static constexpr auto CACHE_DURATION = 100_ms; // 0.1 seconds

	// Check if a recent, valid cache entry exists for this exact spot.
	if ((level.time - cache.frame_time) < FRAME_TIME_MS * 2 && // Re-use within 2 frames
		level.time - cache.last_check_time < CACHE_DURATION &&
		cache.last_check_origin == spawn_point->s.origin)
	{
		// The cache is fresh. Return the cached player occupation status.
		return cache.was_occupied_by_player;
	}

	// --- 3. Cache Miss - Perform Live Check ---
	cache.last_check_time = level.time;
	cache.frame_time = level.time;
	cache.last_check_origin = spawn_point->s.origin;
	// Reset flags before the new check.
	cache.was_occupied_by_player = false;
	cache.has_obstacle = false;

	// Define a slightly generous bounding box for the check.
    // --- FIX: Replaced .scaled(float) with operator*(float) for scalar multiplication ---
	static const vec3_t check_mins = vec3_t{16, 16, 24} * -1.75f;
	static const vec3_t check_maxs = vec3_t{16, 16, 32} * 1.75f;
    // --- END FIX ---
	const vec3_t absolute_mins = spawn_point->s.origin + check_mins;
	const vec3_t absolute_maxs = spawn_point->s.origin + check_maxs;

	// --- 4. Unified BoxEdicts Check (Single Pass Optimization) ---
	OccupiedCheckData check_data = { ignore_ent, &cache };

	gi.BoxEdicts(absolute_mins, absolute_maxs, nullptr, 0, AREA_SOLID, 
        [](edict_t *ent, void *data) -> BoxEdictsResult_t {
            auto* cd = static_cast<OccupiedCheckData*>(data);

            if (ent == cd->ignore_ent) {
                return BoxEdictsResult_t::Skip;
            }

            // Player/Bot check has the highest priority. If found, we can stop the search immediately.
            if (ent->client && ent->inuse) {
                cd->cache->was_occupied_by_player = true;
                return BoxEdictsResult_t::End; // Early exit for max performance.
            }

            // Obstacle check (monster or defense). We set the flag but continue searching,
            // because a player might also be in the box, and that's more important.
            if ((ent->svflags & SVF_MONSTER && !ent->deadflag) || IsPlayerDefense(ent)) {
                cd->cache->has_obstacle = true;
            }

            return BoxEdictsResult_t::Skip; // Continue searching for a player.
        }, 
        &check_data);

	// The function's primary job is to report if a *player* is blocking the spawn.
	// The `has_obstacle` flag has been correctly set in the cache for the caller
	// (like SelectRandomSpawnPoint) to decide if alternative positions should be tried.
	return cache.was_occupied_by_player;
}

template <typename TFilter>
edict_t *SelectRandomSpawnPoint(TFilter filter)
{
    // Use std::array for compile-time safety and to avoid magic numbers.
    std::array<edict_t*, MAX_SPAWN_POINTS> availableSpawns{};
    std::array<edict_t*, MAX_SPAWN_POINTS> occupiedButUsableSpawns{};
    size_t availableCount = 0;
    size_t occupiedCount = 0;

    for (edict_t *spawnPoint : monster_spawn_points())
    {
        // Consolidated initial validation and cooldown checks for clarity
        const auto& data = spawnPointsData[spawnPoint];
        if (!spawnPoint || !spawnPoint->inuse || !is_valid_vector(spawnPoint->s.origin) ||
            (data.isTemporarilyDisabled && level.time < data.cooldownEndsAt) ||
            (level.time < data.alternative_cooldown))
        {
            continue;
        }

        // Apply the custom filter first. If it fails, no need for further checks.
        if (!filter(spawnPoint))
        {
            continue;
        }

        // Check if a player is directly blocking the spawn.
        // This is a hard "no" for direct spawning.
        if (IsSpawnPointOccupied(spawnPoint))
        {
            if (developer->integer > 2)
                gi.Com_PrintFmt("SelectRandomSpawnPoint: Point #{} at {} skipped (player occupied).\n",
                                (int)(spawnPoint - g_edicts), spawnPoint->s.origin);
            continue;
        }

        // Not blocked by a player. Now check the cache for non-player obstacles.
        // The IsSpawnPointOccupied call above already updated the cache for us.
        const SpawnPointCache& cache = spawn_point_cache[spawnPoint];
        if (cache.has_obstacle)
        {
            // Blocked by monster/defense - add to the list for potential alternative spawn.
            if (occupiedCount < occupiedButUsableSpawns.size())
            {
                occupiedButUsableSpawns[occupiedCount++] = spawnPoint;
            }
            if (developer->integer > 2)
                gi.Com_PrintFmt("SelectRandomSpawnPoint: Point #{} at {} added to occupiedButUsable (obstacle present).\n",
                                (int)(spawnPoint - g_edicts), spawnPoint->s.origin);
        }
        else
        {
            // Not blocked by player AND no obstacle found -> add to the best-case list.
            if (availableCount < availableSpawns.size())
            {
                availableSpawns[availableCount++] = spawnPoint;
            }
            if (developer->integer > 2)
                gi.Com_PrintFmt("SelectRandomSpawnPoint: Point #{} at {} added to available.\n",
                                (int)(spawnPoint - g_edicts), spawnPoint->s.origin);
        }
    }

    // --- Selection Logic ---

    // Prioritize completely available spawns.
    if (availableCount > 0)
    {
        // Create a lightweight, non-owning view of the valid part of the array.
        std::span<edict_t* const> valid_spawns(availableSpawns.data(), availableCount);

        // Use your brilliant random_element helper! It's perfect for this.
        edict_t* chosen_spawn = random_element(valid_spawns);
        if (developer->integer > 1)
            gi.Com_PrintFmt("SelectRandomSpawnPoint: Selected from {} available points. Chose #{} at {}.\n",
                            availableCount, (int)(chosen_spawn - g_edicts), chosen_spawn->s.origin);
        return chosen_spawn;
    }

    // If no completely available spawns, try one that is blocked by an obstacle
    // (which can then be used as a base for an alternative position).
    if (occupiedCount > 0)
    {
        std::span<edict_t* const> valid_spawns(occupiedButUsableSpawns.data(), occupiedCount);
        edict_t* chosen_spawn = random_element(valid_spawns);
        if (developer->integer > 1)
            gi.Com_PrintFmt("SelectRandomSpawnPoint: Selected from {} obstacle-occupied points. Chose #{} at {} (will try alternative).\n",
                            occupiedCount, (int)(chosen_spawn - g_edicts), chosen_spawn->s.origin);
        return chosen_spawn;
    }

    // If we reach here, no suitable spawn points were found at all.
    if (developer->integer > 1)
        gi.Com_PrintFmt("SelectRandomSpawnPoint: No suitable spawn points found after checking all.\n");

    return nullptr;
}

static void CleanupSpawnPointCache() noexcept { spawn_point_cache.clear(); }

//  Definir tamaños máximos para arrays estáticos
constexpr size_t MAX_ELIGIBLE_BOSSES = 16;
constexpr size_t MAX_RECENT_BOSSES = 4;

static constexpr size_t NUM_WAVE_SOUNDS = 12;
static constexpr size_t NUM_START_SOUNDS = 8;

// precache//
static cached_soundindex wave_sounds[NUM_WAVE_SOUNDS];
static cached_soundindex start_sounds[NUM_START_SOUNDS];
static cached_soundindex sound_tele3;
static cached_soundindex sound_tele_up;
static cached_soundindex sound_spawn1;
static cached_soundindex incoming;
static cached_soundindex sound_quake; 
static cached_soundindex talk; 
static cached_soundindex tele1; 

// Arrays de strings con los nombres de los sonidos
static constexpr const char *WAVE_SOUND_PATHS[NUM_WAVE_SOUNDS] = {
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
	"misc/alarm.wav"};

static constexpr const char *START_SOUND_PATHS[NUM_START_SOUNDS] = {
	"misc/r_tele3.wav",
	"world/fish.wav",
	"world/klaxon2.wav",
	"misc/tele_up.wav",
	"world/incoming.wav",
	"world/redforceact.wav",
	"makron/voice2.wav",
	"makron/voice.wav"};

static const char *GetCurrentMapName() noexcept
{
	return static_cast<const char *>(level.mapname);
}

enum class WaveEndReason
{
	AllMonstersDead,
	MonstersRemaining,
	TimeLimitReached
};

inline int8_t GetNumActivePlayers();
inline int8_t GetNumSpectPlayers();
int8_t GetNumHumanPlayers();

int32_t g_adjusted_monster_cap = 0;

bool allowWaveAdvance = false;		// Global variable to control wave advancement
bool boss_spawned_for_wave = false; // to avoid boss spamming

int16_t last_wave_number = 0;		// Reducido de uint64_t
uint16_t g_totalMonstersInWave = 0; // Reducido de uint32_t

gtime_t horde_message_end_time = 0_sec;
gtime_t SPAWN_POINT_COOLDOWN = 2.8_sec; // spawns Cooldown

// Function to check and reduce spawn cooldowns when few monsters remain
void CheckAndReduceSpawnCooldowns()
{
	// Only proceed if fewer than 7 stroggs remain and not in a boss wave
	const int32_t remaining_stroggs = GetStroggsNum();
	if (remaining_stroggs > 6 || IsBossWave())
	{
		return;
	}

	// Track if we found any valid cooldowns to reset
	bool found_cooldowns_to_reset = false;
	const gtime_t current_time = level.time;

	// Pre-compute the reduction factor once
	constexpr float REDUCTION_FACTOR = 0.4f; // Changed from 0.15f to 0.4f to make late-wave cooldown reduction less aggressive

	// Process spawn points by directly iterating over monster_spawn_points()
	// This avoids a manual scan of all g_edicts.
	for (edict_t *spawn_point : monster_spawn_points())
	{
		// Basic validation: monster_spawn_points() should ideally only return valid
		// info_player_deathmatch entities that are inuse.
		if (!spawn_point || !spawn_point->inuse)
		{
			// Optionally log if unexpected entities are returned
			if (developer->integer > 1)
			{
				gi.Com_PrintFmt("CheckAndReduceSpawnCooldowns: monster_spawn_points() returned an invalid or !inuse entity.\n");
			}
			continue;
		}
		// Further classname check can be added if monster_spawn_points() is not guaranteed
		// to return only "info_player_deathmatch", though its name implies it should.
		// if (!spawn_point->classname || strcmp(spawn_point->classname, "info_player_deathmatch") != 0) {
		//     continue;
		// }

		auto &data = spawnPointsData[spawn_point];

		// Check if spawn point is disabled and cooldown is still active
		if (data.isTemporarilyDisabled && current_time < data.cooldownEndsAt)
		{
			found_cooldowns_to_reset = true;

			const gtime_t remaining_time = data.cooldownEndsAt - current_time;
			// Calculate reduced duration and ensure it meets the minimum
			const gtime_t reduced_duration = remaining_time * REDUCTION_FACTOR;
			const gtime_t final_duration = std::max(reduced_duration, HordeConstants::MIN_REDUCED_INDIVIDUAL_COOLDOWN);
			data.cooldownEndsAt = current_time + final_duration; // Apply clamped duration

			data.attempts = 0;
		}
	}

	if (found_cooldowns_to_reset)
	{
		SPAWN_POINT_COOLDOWN *= REDUCTION_FACTOR;
		// Clamp the global cooldown after reduction
		SPAWN_POINT_COOLDOWN = std::max(SPAWN_POINT_COOLDOWN, HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN);

		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("Global spawn cooldown reduced and clamped to {:.2f}s\n", SPAWN_POINT_COOLDOWN.seconds());
		}
	}
}

static constexpr gtime_t GetBaseSpawnCooldown(bool isSmallMap, bool isBigMap)
{
	if (isSmallMap)
		return 0.5_sec; // Increased from 0.2 to 0.5
	else if (isBigMap)
		return 2.0_sec; // Increased from 1.4 to 2.0
	else
		return 1.2_sec; // Increased from 0.8 to 1.2
}

static float CalculateCooldownScale(int32_t lvl, const horde::MapSize &mapSize)
{
	// Early return for low levels - improves branch prediction
	if (lvl <= 10)
	{
		return 1.0f;
	}

	// Cache player count - only compute once
	const int32_t numHumanPlayers = GetNumHumanPlayers();

	// Compute base scale - more gradual linear scaling with level
	float scale = 1.0f + (lvl * 0.015f); // Changed from 0.02f to 0.015f for more gradual scaling

	// Compute player reduction factor once
	float playerReduction = 0.0f;
	if (numHumanPlayers > 1)
	{
		constexpr float PLAYER_REDUCTION = 0.1f;
		constexpr float MAX_REDUCTION = 0.45f;
		playerReduction = std::min((numHumanPlayers - 1) * PLAYER_REDUCTION, MAX_REDUCTION);
		scale *= (1.0f - playerReduction);
	}

	// Determine map multipliers with fewer branches
	float multiplier, maxScale;

	if (mapSize.isSmallMap)
	{
		multiplier = 0.7f;
		maxScale = 1.3f;
	}
	else if (mapSize.isBigMap)
	{
		multiplier = 0.85f;
		maxScale = 1.75f;
	}
	else
	{ // Medium map
		multiplier = 0.8f;
		maxScale = 1.5f;
	}

	// Apply map multiplier and clamp to max scale
	return std::min(scale * multiplier, maxScale);
}
cvar_t *g_horde;

enum class horde_state_t
{
	warmup,
	spawning,
	active_wave,
	cleanup,
	rest
};

// Warning times in seconds
constexpr std::array<float, 3> WARNING_TIMES = {30.0f, 10.0f, 5.0f};

// En HordeState, reemplazar el vector con array estático
struct HordeState
{
	gtime_t warm_time = 4_sec;
	horde_state_t state = horde_state_t::warmup;
	gtime_t monster_spawn_time;
	int32_t num_to_spawn = 0;
	int32_t level = 0;
	int32_t queued_monsters = 0;
	gtime_t lastPrintTime = 0_sec;

	bool conditionTriggered = false;
	gtime_t conditionStartTime = 0_sec;
	gtime_t conditionTimeThreshold = 0_sec;
	bool timeWarningIssued = false;
	gtime_t waveEndTime = 0_sec;
	bool warningIssued[WARNING_TIMES.size()] = {false}; // Initialize all to false

	// Failsafe timeout to prevent getting stuck in a state
	gtime_t failsafe_timeout = 0_sec;

	gtime_t last_successful_hud_update = 0_sec;
	uint32_t failed_updates_count = 0;

	horde::MapSize current_map_size;
	int32_t max_monsters{};		 // Cacheado basado en map_size
	gtime_t base_spawn_cooldown; // Cacheado basado en map_size

	void update_map_size(const char *mapname)
	{
		current_map_size = GetMapSize(mapname);
		max_monsters = current_map_size.isSmallMap ? HordeConstants::MAX_MONSTERS_SMALL_MAP : (current_map_size.isMediumMap ? HordeConstants::MAX_MONSTERS_MEDIUM_MAP : HordeConstants::MAX_MONSTERS_BIG_MAP);
		base_spawn_cooldown = GetBaseSpawnCooldown(
			current_map_size.isSmallMap,
			current_map_size.isBigMap);
	}

	void reset_hud_state()
	{
		last_successful_hud_update = 0_sec;
		failed_updates_count = 0;
	}
} g_horde_local;

int16_t current_wave_level = g_horde_local.level;
bool next_wave_message_sent = false;
auto auto_spawned_bosses = std::unordered_set<edict_t *>{};

// Función para calcular el bono de locura y caos
static inline int32_t CalculateChaosInsanityBonus(int32_t lvl) noexcept
{
	if (g_chaotic->integer)
		return (lvl <= 3) ? 6 : 3;
	switch (g_insane->integer)
	{
	case 2:
		return 16;
	case 1:
		return 8;
	default:
		return 0;
	}
}

inline int32_t GetAdjustedMonsterCap(const horde::MapSize &mapSize, int32_t waveLevel)
{
	// Original base caps
	const int32_t baseSmallCap = HordeConstants::MAX_MONSTERS_SMALL_MAP;   // 14
	const int32_t baseMediumCap = HordeConstants::MAX_MONSTERS_MEDIUM_MAP; // 16
	const int32_t baseBigCap = HordeConstants::MAX_MONSTERS_BIG_MAP;	   // 32

	int32_t baseCap;

	// Determine base cap based on map size first
	if (mapSize.isSmallMap)
	{
		baseCap = baseSmallCap;
	}
	else if (mapSize.isMediumMap)
	{
		baseCap = baseMediumCap;
	}
	else
	{ // Big map
		baseCap = baseBigCap;
	}

	// Apply early wave reduction (only for non-big maps)
	if (waveLevel <= 10 && !mapSize.isBigMap)
	{
		// Scale from 60% at wave 1 up to 100% at wave 10
		float reductionFactor = 0.6f + ((waveLevel - 1) * 0.0444f); // (1.0 - 0.6) / 9 = 0.0444...
		reductionFactor = std::clamp(reductionFactor, 0.6f, 1.0f);	// Ensure it stays within bounds
		baseCap = static_cast<int32_t>(baseCap * reductionFactor);
	}

	// --- Player Count Bonus ---
	int32_t finalAdjustedCap = baseCap; // Start with the potentially reduced base cap
	const int32_t numHumanPlayers = GetNumHumanPlayers();

	if (numHumanPlayers > 1)
	{
		constexpr int32_t MAX_BONUS_PLAYERS = 3; // Cap bonus contribution at 3 extra players (i.e., players 2, 3, 4)
		constexpr int32_t BONUS_PER_PLAYER = 2;	 // +2 monsters per extra contributing player
		const int32_t extraPlayers = std::min(numHumanPlayers - 1, MAX_BONUS_PLAYERS);
		const int32_t playerBonus = extraPlayers * BONUS_PER_PLAYER;
		finalAdjustedCap += playerBonus;

		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("GetAdjustedMonsterCap: Wave={}, BaseCap={}, Humans={}, BonusPlayers={}, Bonus={}, FinalCap={}\n",
							waveLevel, baseCap, numHumanPlayers, extraPlayers, playerBonus, finalAdjustedCap);
		}
	}
	else
	{
		if (developer->integer > 1)
		{
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
inline static void ClampNumToSpawn(const horde::MapSize &mapSize)
{ // mapSize parameter might no longer be needed here
	// g_adjusted_monster_cap is now calculated in GetAdjustedMonsterCap and includes player bonus
	const int32_t maxAllowed = g_adjusted_monster_cap;

	// Ensure g_adjusted_monster_cap was initialized (fallback if called too early)
	if (maxAllowed <= 0)
	{
		// Fallback to basic map defaults if the global cap isn't ready
		const int32_t fallbackCap = mapSize.isSmallMap ? HordeConstants::MAX_MONSTERS_SMALL_MAP : (mapSize.isBigMap ? HordeConstants::MAX_MONSTERS_BIG_MAP : HordeConstants::MAX_MONSTERS_MEDIUM_MAP);
		g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, fallbackCap);
		if (developer->integer)
		{
			gi.Com_PrintFmt("ClampNumToSpawn: WARN - g_adjusted_monster_cap not ready, used fallback {}\n", fallbackCap);
		}
	}
	else
	{
		// Clamp using the globally calculated value
		g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, maxAllowed);
		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("ClampNumToSpawn: Clamping num_to_spawn to {} (g_adjusted_monster_cap)\n", maxAllowed);
		}
	}
}

static int32_t CalculateQueuedMonsters(const horde::MapSize& mapSize, int32_t lvl, bool isHardMode) noexcept {
    if (lvl <= 3) // No queue for first 3 waves still seems fine
        return 0;

    float baseQueued = std::sqrt(static_cast<float>(lvl)) * 3.0f;
    baseQueued *= (1.0f + (lvl) * 0.18f); // Base scaling with level

    float mapSizeMultiplier = 1.0f;
    if (mapSize.isSmallMap) {
        mapSizeMultiplier = 1.1f; // Slightly reduced from 1.3
    } else if (mapSize.isMediumMap) {
        mapSizeMultiplier = 1.2f; // Slightly reduced from 1.4
    } else if (mapSize.isBigMap) {
        if (lvl <= 7) { // For early waves on big maps
            mapSizeMultiplier = 1.15f; // Significantly reduced from 1.5
        } else if (lvl <= 12) {
            mapSizeMultiplier = 1.3f;  // Moderately reduced
        }
        else {
            mapSizeMultiplier = 1.5f; // Full multiplier for later waves
        }
    }
    baseQueued *= mapSizeMultiplier;

    const int32_t maxQueuedBase = mapSize.isSmallMap ? 25 : (mapSize.isBigMap ? 40 : 30); // Slightly reduced maxes
    // Further reduce max queue for very early waves
    int32_t maxQueued = maxQueuedBase;
    if (lvl <= 7) {
        maxQueued = std::max(5, static_cast<int32_t>(maxQueuedBase * 0.5f)); // e.g., half max, but at least 5
    } else if (lvl <= 12) {
        maxQueued = std::max(10, static_cast<int32_t>(maxQueuedBase * 0.75f));
    }


    if (lvl > 20) { // Bonus for high levels
        baseQueued *= std::pow(1.15f, std::min(lvl - 20, 18));
    }

    if (isHardMode) { // Difficulty adjustment
        float difficultyMultiplier = 1.25f;
        if (lvl > 25) {
            difficultyMultiplier += (lvl - 25) * 0.025f;
            difficultyMultiplier = std::min(difficultyMultiplier, 1.75f);
        }
        baseQueued *= difficultyMultiplier;
    }

    baseQueued *= 0.85f; // Final reduction factor

    return std::min(static_cast<int32_t>(baseQueued), maxQueued);
}

// Cache for common calculations in UnifiedAdjustSpawnRate
struct WaveScalingCache
{
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
	void initialize()
	{
		using namespace HordeConstants;

		// Initialize player multipliers
		for (int32_t players = 0; players <= MAX_HUMAN_PLAYERS; ++players)
		{
			playerMultipliers[players] = (players <= 1) ? 1.0f : BASE_DIFFICULTY_MULTIPLIER + ((players - 1) * PLAYER_COUNT_SCALE);
		}

		// Initialize base counts by map type and level
		for (int mapType = 0; mapType < 3; ++mapType)
		{
			for (int32_t level = 0; level <= MAX_WAVE_LEVEL; ++level)
			{
				// Select the appropriate base count based on level ranges
				int32_t countIndex;
				if (level <= 5)
					countIndex = 0;
				else if (level <= 10)
					countIndex = 1;
				else if (level <= 15)
					countIndex = 2;
				else
					countIndex = 3; // Levels > 15

				// Store pre-computed base count
				baseCountsByLevel[mapType][level] = BASE_COUNTS[mapType][countIndex];
			}
		}

		// Removed initialization for additionalSpawnsByLevel as it's calculated directly
		// in UnifiedAdjustSpawnRate now, eliminating the unused variables.

		// Initialize cooldown scales (can be accessed by mapType and level)
		horde::MapSize smallMap = {true, false, false};
		horde::MapSize mediumMap = {false, false, true}; // Corrected: Medium map should have isMediumMap true
		horde::MapSize bigMap = {false, true, false};

		for (int32_t level = 0; level <= MAX_WAVE_LEVEL; ++level)
		{
			cooldownScales[0][level] = CalculateCooldownScale(level, smallMap);	 // Index 0 = Small
			cooldownScales[1][level] = CalculateCooldownScale(level, mediumMap); // Index 1 = Medium
			cooldownScales[2][level] = CalculateCooldownScale(level, bigMap);	 // Index 2 = Big
		}
	}
} g_waveScalingCache;

void UnifiedAdjustSpawnRate(const horde::MapSize &mapSize, int32_t lvl, int32_t humanPlayers) noexcept
{
	using namespace HordeConstants;

	// Initialize cache if needed (one-time operation)
	static bool cache_initialized = false;
	if (!cache_initialized)
	{
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
	if (safePlayerCount > 1)
	{
		const float playerMultiplier = g_waveScalingCache.playerMultipliers[safePlayerCount];
		baseCount = static_cast<int32_t>(baseCount * playerMultiplier);
	}

	// --- CORRECTED SECTION ---
	// Calculate additional spawn count directly, without using the removed cache array
	int32_t additionalSpawn;
	if (safeLevel < 8)
	{
		additionalSpawn = 6; // Default for early levels
	}
	else
	{
		// Apply map-specific base value from constants
		additionalSpawn = mapSize.isSmallMap ? ADDITIONAL_SPAWNS[0] : mapSize.isBigMap ? ADDITIONAL_SPAWNS[2]
																					   : ADDITIONAL_SPAWNS[1];

		// Level-based adjustment for high levels
		if (safeLevel > 25)
		{
			additionalSpawn = static_cast<int32_t>(additionalSpawn * 1.6f);
		}
	}
	// --- END CORRECTED SECTION ---

	// Apply difficulty adjustments (Chaos/Insanity bonus)
	if (safeLevel >= 3 && (g_chaotic->integer || g_insane->integer))
	{
		additionalSpawn += CalculateChaosInsanityBonus(safeLevel);
	}

	// Get cooldown from cache and apply adjustments
	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);
	const float cooldownScale = g_waveScalingCache.cooldownScales[mapTypeIndex][safeLevel];
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	// Apply difficulty-based cooldown adjustments
	if (g_chaotic->integer || g_insane->integer)
	{
		SPAWN_POINT_COOLDOWN *= HordeConstants::TIME_REDUCTION_MULTIPLIER;
	}
	else
	{
		// Normal difficulty adjustment
		SPAWN_POINT_COOLDOWN *= 1.2f;
	}

	// Apply periodic difficulty scaling (every 3 levels)
	if (safeLevel > 0 && safeLevel % 3 == 0)
	{ // Added check for safeLevel > 0
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
									  3.5_sec);									 // Keep upper bound or define a MAX constant
	// Calculate final spawn count
	g_horde_local.num_to_spawn = baseCount + additionalSpawn;
	ClampNumToSpawn(mapSize); // Handle clamping

	// Calculate queued monsters
	const bool isHardMode = g_insane->integer || g_chaotic->integer;
	g_horde_local.queued_monsters = CalculateQueuedMonsters(mapSize, safeLevel, isHardMode);

	// Debug output
	if (developer->integer == 3)
	{
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

struct ConditionParams
{
	int32_t maxMonsters;
	gtime_t timeThreshold;
	gtime_t lowPercentageTimeThreshold;
	gtime_t independentTimeThreshold;
	float lowPercentageThreshold;
	float aggressiveTimeReductionThreshold;

	ConditionParams() noexcept : maxMonsters(0),
								 timeThreshold(0_sec),
								 lowPercentageTimeThreshold(0_sec),
								 independentTimeThreshold(0_sec),
								 lowPercentageThreshold(0.3f),
								 aggressiveTimeReductionThreshold(0.3f)
	{
	}
};

static constexpr gtime_t calculate_max_wave_time(int32_t wave_level)
{
	// Calculate the time base based on level
	// MODIFIED: Increased base time and per-level increase
	gtime_t base_time = 130_sec + (gtime_t::from_sec(3.2f) * wave_level);

	// Limit base time to 240 seconds (Increased from 220)
	base_time = (base_time <= 240_sec) ? base_time : 240_sec;

	// Add extra time if it's a boss wave (levels multiples of 5 after 10)
	if (wave_level >= 10 && wave_level % 5 == 0)
	{
		base_time += HordeConstants::BOSS_TIME_BONUS; // Assuming HordeConstants::BOSS_TIME_BONUS is defined
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

// Forward declaration for calculate_max_wave_time if it's not already visible
// static constexpr gtime_t calculate_max_wave_time(int32_t wave_level); // (already provided in previous context)

static ConditionParams GetConditionParams(const horde::MapSize &mapSize, int32_t numHumanPlayers, int32_t lvl)
{
	ConditionParams params; // Default constructor initializes members

	// Initial validation
	if (lvl < 0)
	{
		// Optionally, log an error or warning if level is invalid
		// gi.Com_PrintFmt("Warning: GetConditionParams called with invalid level: {}\n", lvl);
		return params; // Return default initialized params
	}

	// 1. Configure map-specific base parameters (maxMonsters, initial timeThreshold)
	auto configureMapParams = [&](ConditionParams &p)
	{
		if (mapSize.isBigMap)
		{
			p.maxMonsters = (numHumanPlayers >= 3) ? 26 : 24;
			p.timeThreshold = random_time(24_sec, 30_sec); // From previous modification
		}
		else if (mapSize.isSmallMap)
		{
			p.maxMonsters = (numHumanPlayers >= 3) ? 12 : 9;
			p.timeThreshold = random_time(22_sec, 28_sec); // From previous modification
		}
		else
		{ // Medium maps
			p.maxMonsters = (numHumanPlayers >= 3) ? 17 : 13;
			p.timeThreshold = random_time(24_sec, 31_sec); // From previous modification
		}
	};
	configureMapParams(params);

	// 2. Progressive adjustments based on level
	params.maxMonsters += std::min(lvl / 4, 8);
	params.timeThreshold += gtime_t::from_ms(80ll * std::min(lvl / 3, 5)); // From previous modification

	// 3. Early wave time multiplier (for conditional timers)
	float timeMultiplier = 1.0f;
	if (lvl >= 1 && lvl <= 10)
	{
		timeMultiplier = 1.0f - (0.20f * (static_cast<float>(lvl - 1) / 9.0f));
		timeMultiplier = std::max(0.75f, timeMultiplier); // Clamp, from previous modification
	}

	// 4. High level adjustments (for levels > 10)
	if (lvl > 10)
	{
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * 1.2f);
		params.timeThreshold += 0.20_sec; // From previous modification
	}

	// 5. Difficulty mode adjustments (chaotic/insane)
	if (g_chaotic->integer || g_insane->integer)
	{
		if (numHumanPlayers <= 3)
		{
			params.timeThreshold += random_time(7_sec, 12_sec); // From previous modification
		}
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * 1.1f);
	}

	// ***** NEW: Early Wave Big Map Fix for maxMonsters *****
	if (lvl <= 5 && mapSize.isBigMap)
	{ // Target very early waves on big maps
		// Estimate initial spawn count for big maps, early levels.
		// This needs to roughly match what UnifiedAdjustSpawnRate would produce for num_to_spawn.
		// From HordeConstants::BASE_COUNTS, for big maps, level <= 5, it's 15.
		// Let's use a slightly higher base to be safe.
		int32_t estimatedInitialSpawn = HordeConstants::BASE_COUNTS[2][0]; // 15 for Big Map, Level <= 5
		if (numHumanPlayers > 1)
		{ // Rough approximation of player scaling in UnifiedAdjustSpawnRate
			estimatedInitialSpawn = static_cast<int32_t>(estimatedInitialSpawn * (HordeConstants::BASE_DIFFICULTY_MULTIPLIER + ((std::min(numHumanPlayers, 4) - 1) * HordeConstants::PLAYER_COUNT_SCALE)));
		}
		// Ensure params.maxMonsters is comfortably above this initial spawn count
		// Add a buffer, e.g., 5-8 monsters, or a percentage.
		params.maxMonsters = std::max(params.maxMonsters, estimatedInitialSpawn + 5);
		if (developer->integer)
		{
			gi.Com_PrintFmt("GetConditionParams: BigMap Early Wave (lvl {}) Fix: maxMonsters adjusted to {} (estimated initial: {})\n", lvl, params.maxMonsters, estimatedInitialSpawn);
		}
	}
	// ***** END NEW FIX *****

	// 6. DYNAMIC lowPercentageThreshold based on wave level
	if (lvl <= 10)
	{
		params.lowPercentageThreshold = 0.35f; // More lenient for early waves
	}
	else if (lvl <= 20)
	{
		params.lowPercentageThreshold = 0.25f; // Moderate for mid waves
	}
	else if (lvl <= 30)
	{
		params.lowPercentageThreshold = 0.15f; // Stricter for late waves
	}
	else
	{
		params.lowPercentageThreshold = 0.10f; // Very strict for very late waves
	}
	// Ensure it's always a positive, reasonable minimum if logic were to produce <=0
	params.lowPercentageThreshold = std::max(0.05f, params.lowPercentageThreshold);

	// 7. Configure time threshold for the low percentage condition
	params.lowPercentageTimeThreshold = random_time(14_sec, 24_sec); // From previous modification

	// 8. Aggressive time reduction threshold (when very few monsters are left, this is the % of total wave)
	// This seems to be a threshold for when the *aggressive time reduction itself* can be considered,
	// not the direct percentage of monsters that triggers the aggressive timer (that's MONSTERS_FOR_AGGRESSIVE_REDUCTION).
	// Kept original value as its interaction is distinct.
	params.aggressiveTimeReductionThreshold = 0.3f;

	// 9. Configure independent (absolute maximum) wave time
	params.independentTimeThreshold = calculate_max_wave_time(lvl); // Uses the separately improved function

	// 10. Apply early wave timeMultiplier to the conditional timers
	if (lvl >= 1 && lvl <= 10)
	{
		params.timeThreshold = gtime_t::from_sec(params.timeThreshold.seconds() * timeMultiplier);
		params.lowPercentageTimeThreshold = gtime_t::from_sec(params.lowPercentageTimeThreshold.seconds() * timeMultiplier);
		// The `independentTimeThreshold` is generally not affected by this multiplier as it's an overall cap.
		// The `lowPercentageThreshold` (the float value itself) is also not multiplied; the timer it triggers is.
	}

	// 11. Final parameter validation (ensure reasonable minimums)
	params.maxMonsters = std::max(1, params.maxMonsters);									// At least 1 monster
	params.timeThreshold = std::max(5_sec, params.timeThreshold);							// Minimum 5s for conditional timer
	params.lowPercentageTimeThreshold = std::max(5_sec, params.lowPercentageTimeThreshold); // Minimum 5s for low % timer
	params.independentTimeThreshold = std::max(10_sec, params.independentTimeThreshold);	// Minimum 10s for overall wave

	return params;
}

inline int32_t GetAdjustedMonsterCap(const horde::MapSize &mapSize, int32_t waveLevel);

void ResetChampionMonsterState()
{
	champion_spawned_this_wave = false;
	// champion_spawn_cooldown = 0; // REMOVED
	champion_spawn_cooldown_ends_at = 0_sec;
}

void InitializeWaveType(int32_t waveLevel);

bool G_IsDeathmatch() noexcept
{
	return deathmatch->integer;
}

bool G_IsCooperative() noexcept
{
	return coop->integer && !g_horde->integer;
}

struct weighted_item_t;
using weight_adjust_func_t = void (*)(const weighted_item_t &item, float &weight);

// Define the function pointer type (keep this)
struct weighted_item_t;
using weight_adjust_func_t = void (*)(const weighted_item_t &item, float &weight);

// Structure definition (as you provided)
struct HordeItemInfo
{
	item_id_t id; // Use item_id_t instead of const char*
	float weight; // Relative probability (higher is more common)
	int minWave;  // Minimum wave level for this item to appear

	constexpr HordeItemInfo(item_id_t item_id, float w, int mw)
		: id(item_id), weight(w), minWave(mw)
	{
	}
};

// The comprehensive static array
static const HordeItemInfo hordeItemData[] = {
	// --- Armor ---
	{IT_ARMOR_SHARD, 1.5f, 1},		   // Common, small boost
	{IT_ARMOR_JACKET, 0.8f, 3},		   // Basic armor
	{IT_ARMOR_COMBAT, 0.5f, 8},		   // Better armor
	{IT_ARMOR_BODY, 0.2f, 15},		   // Best standard armor
	{IT_ITEM_POWER_SCREEN, 0.3f, 7},   // Power Armor (early)
	{IT_ITEM_POWER_SHIELD, 0.01f, 19}, // Power Armor (late)

	// --- Weapons ---
	// Blaster is skipped (always have)
	{IT_WEAPON_CHAINFIST, 0.1f, 1},	   // Melee, niche drop
	{IT_WEAPON_SHOTGUN, 1.0f, 1},	   // Basic weapon
	{IT_WEAPON_SSHOTGUN, 0.7f, 3},	   // Strong early/mid weapon
	{IT_WEAPON_MACHINEGUN, 1.0f, 1},   // Basic weapon
	{IT_WEAPON_ETF_RIFLE, 0.6f, 5},	   // Rogue weapon (mid)
	{IT_WEAPON_CHAINGUN, 0.5f, 7},	   // Mid-tier DPS
	{IT_WEAPON_GLAUNCHER, 0.5f, 5},	   // Area denial (mid)
	{IT_WEAPON_PROXLAUNCHER, 0.3f, 8}, // Rogue weapon (mid/late trap)
	{IT_WEAPON_RLAUNCHER, 0.4f, 7},	   // Powerful splash (mid/late)
	{IT_WEAPON_HYPERBLASTER, 0.4f, 9}, // Energy weapon (mid/late)
	{IT_WEAPON_IONRIPPER, 0.4f, 10},   // Xatrix weapon (mid/late)
	{IT_WEAPON_PLASMABEAM, 0.2f, 12},  // Rogue weapon (late, high DPS)
	{IT_WEAPON_RAILGUN, 0.25f, 15},	   // High skill/damage (late)
	{IT_WEAPON_PHALANX, 0.25f, 14},	   // Xatrix weapon (late)
	{IT_WEAPON_BFG, 0.05f, 20},		   // Ultimate weapon (very late, rare)
	{IT_WEAPON_DISRUPTOR, 0.15f, 18},  // Rogue weapon (late, piercing)
	// Grenades/Traps/Tesla as 'weapons' (for direct drop maybe?)
	{IT_AMMO_GRENADES, 0.4f, 2}, // Hand grenades item
	{IT_AMMO_TRAP, 0.2f, 6},	 // Xatrix trap item
	{IT_AMMO_TESLA, 0.15f, 8},	 // Rogue tesla item

	// --- Ammo --- (Generally higher weight than corresponding weapons)
	{IT_AMMO_SHELLS, 2.0f, 1},	   // Common
	{IT_AMMO_BULLETS, 2.0f, 1},	   // Common
	{IT_AMMO_GRENADES, 1.2f, 2},   // Mid frequency
	{IT_AMMO_ROCKETS, 1.0f, 6},	   // Needed for RL
	{IT_AMMO_CELLS, 1.5f, 7},	   // Needed for energy weapons
	{IT_AMMO_SLUGS, 0.8f, 14},	   // Needed for Railgun
	{IT_AMMO_MAGSLUG, 0.8f, 13},   // Needed for Phalanx (Xatrix)
	{IT_AMMO_FLECHETTES, 1.5f, 4}, // Needed for ETF Rifle (Rogue)
	{IT_AMMO_PROX, 0.6f, 7},	   // Needed for Prox Launcher (Rogue)
	{IT_AMMO_ROUNDS, 0.5f, 17},	   // Needed for Disruptor (Rogue)
	{IT_AMMO_TRAP, 0.4f, 5},	   // Needed for Trap (Xatrix)
	{IT_AMMO_TESLA, 0.3f, 7},	   // Needed for Tesla (Rogue)
	{IT_AMMO_NUKE, 0.02f, 25},	   // Very rare ammo/powerup (Rogue)

	// --- Powerups ---
	{IT_ITEM_QUAD, 0.1f, 8},			  // Rare, powerful
	{IT_ITEM_QUADFIRE, 0.1f, 6},		  // Rare, powerful (Xatrix)
	{IT_ITEM_INVULNERABILITY, 0.05f, 12}, // Very rare, very powerful
	{IT_ITEM_INVISIBILITY, 0.2f, 5},	  // Situational
	{IT_ITEM_SILENCER, 0.15f, 4},		  // Situational
	//{ IT_ITEM_REBREATHER,     0.1f,  1 }, // Map dependent, low weight
	//{ IT_ITEM_ENVIROSUIT,     0.1f,  1 }, // Map dependent, low weight
	{IT_ITEM_ADRENALINE, 0.3f, 3},	// Permanent health boost, good reward
	{IT_ITEM_BANDOLIER, 0.5f, 4},	// Good ammo capacity boost
	{IT_ITEM_PACK, 0.25f, 10},		// Excellent ammo capacity boost
	{IT_ITEM_IR_GOGGLES, 0.15f, 7}, // Situational (Rogue)
	{IT_ITEM_DOUBLE, 0.15f, 5},		// Damage boost (Rogue)
	//{ IT_ITEM_SPHERE_VENGEANCE, 0.1f, 15 }, // Powerful sphere (Rogue)
	{IT_ITEM_SPHERE_HUNTER, 0.1f, 12},	// Powerful sphere (Rogue)
	{IT_ITEM_SPHERE_DEFENDER, 0.1f, 8}, // Powerful sphere (Rogue)
	{IT_ITEM_DOPPELGANGER, 0.15f, 6},	// Utility/Distraction (Rogue)
	{IT_ITEM_TELEPORT, 0.2f, 3},		// Utility Escape
	{IT_ITEM_SENTRYGUN, 0.3f, 5},		// Deployable Defense

	// --- Health ---
	{IT_HEALTH_SMALL, 1.8f, 1},	 // Very common
	{IT_HEALTH_MEDIUM, 1.5f, 1}, // Common
	{IT_HEALTH_LARGE, 0.8f, 3},	 // Less common
	{IT_HEALTH_MEGA, 0.1f, 10},	 // Rare, powerful boost

	// --- Legacy Heads (Optional - Minor permanent boost) ---
	//{ IT_ITEM_ANCIENT_HEAD,   0.05f, 10 },
	//{ IT_ITEM_LEGACY_HEAD,    0.05f, 10 },

	// End of list marker (optional, but good practice if needed)
	// { IT_NULL, 0.0f, 0 }
};

// First, add these at the top with other global variables
static constexpr size_t WAVE_MEMORY_SIZE = 3; // Remember last 3 waves
static std::array<MonsterWaveType, WAVE_MEMORY_SIZE> previous_wave_types = {};
static size_t wave_memory_index = 0;

// Helper function to check if a wave type was recently used
static bool WasRecentlyUsed(MonsterWaveType wave_type) noexcept
{
	for (const auto &prev_type : previous_wave_types)
	{
		if (prev_type == wave_type)
		{
			return true;
		}
	}
	return false;
}

// Helper function to store wave type in memory
static void StoreWaveType(MonsterWaveType wave_type) noexcept
{
	previous_wave_types[wave_memory_index] = wave_type;
	wave_memory_index = (wave_memory_index + 1) % WAVE_MEMORY_SIZE;
}

// Helper function to try setting a wave type with validation
static bool TrySetWaveType(MonsterWaveType new_type)
{
	// Special case for boss waves - allow flying waves to override the recent wave restriction
	if (HasWaveType(new_type, MonsterWaveType::Flying | MonsterWaveType::Boss))
	{
		// If this is a flying boss, we always allow flying waves
		current_wave_type = new_type;
		StoreWaveType(new_type);
		return true;
	}

	// Make flying waves more restrictive for non-boss waves
	if (HasWaveType(new_type, MonsterWaveType::Flying) && !HasWaveType(new_type, MonsterWaveType::Boss))
	{
		// Check if any of the recent waves were flying
		for (const auto &prev_type : previous_wave_types)
		{
			if (HasWaveType(prev_type, MonsterWaveType::Flying))
			{
				return false; // Don't allow flying waves if we had one recently
			}
		}

		// Also check if the previous wave was a boss wave
		if (g_horde_local.level > 0 && (g_horde_local.level - 1) >= 10 &&
			(g_horde_local.level - 1) % 5 == 0)
		{
			return false; // Don't allow flying waves right after boss waves
		}
	}

	// Check if the wave type was recently used
	if (!WasRecentlyUsed(new_type))
	{
		current_wave_type = new_type;
		StoreWaveType(new_type);
		return true;
	}

	// Fallback for specific wave types
	if (HasWaveType(new_type, MonsterWaveType::Mutant) || HasWaveType(new_type, MonsterWaveType::Shambler))
	{
		// Fallback for mutant/shambler types
		current_wave_type = MonsterWaveType::Medium;
		StoreWaveType(MonsterWaveType::Medium);
		return true;
	}
	else if (HasWaveType(new_type, MonsterWaveType::Flying))
	{
		// Fallback for flying types
		current_wave_type = MonsterWaveType::Flying | MonsterWaveType::Medium;
		StoreWaveType(current_wave_type);
		return true;
	}
	else if (HasWaveType(new_type, MonsterWaveType::Arachnophobic))
	{
		// Fallback for arachnophobic types
		current_wave_type = MonsterWaveType::Small | MonsterWaveType::Arachnophobic;
		StoreWaveType(current_wave_type);
		return true;
	}

	return false;
}

// Helper function to check if a wave type is a special wave
static bool IsSpecialWaveType(MonsterWaveType type) noexcept
{
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
static bool WasLastWaveSpecial() noexcept
{
	if (previous_wave_types.empty())
	{
		return false;
	}

	// Get the most recent wave type (the one right before the current index)
	size_t last_index = (wave_memory_index == 0) ? WAVE_MEMORY_SIZE - 1 : wave_memory_index - 1;

	return IsSpecialWaveType(previous_wave_types[last_index]);
}

// Structure for an optional component to add to a wave
struct WaveOptionalComponent
{
	MonsterWaveType type = MonsterWaveType::None; // The type to potentially add
	float chance = 0.0f;						  // Probability (0.0 to 1.0) of adding this type
												  // int min_wave = 1;           // Optional: Could add min wave *within the range*
};

// Define the wave progression using constexpr std::array
// IMPORTANT: Keep this sorted by max_wave ascending!
struct WaveDefinition
{
	int max_wave;			   // This definition applies for waves <= max_wave
	MonsterWaveType base_type; // Guaranteed monster types for this range
	// Use std::array for optionals if the max number is known and fixed,
	// otherwise std::vector is acceptable here as it's only initialized once.
	// Let's assume a max of 4 optionals for this example.
	std::array<WaveOptionalComponent, 4> optionals;
	size_t num_optionals; // Track how many are actually used

	// Constexpr constructor if needed, or aggregate initialization
};

constexpr std::array<WaveDefinition, 8> wave_definitions = {{
    // Waves 1-5: Allow Light units, both Ground and Flying.
    {5, MonsterWaveType::Light | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{}, {}, {}, {}}}, 0},

    // Waves 6-10: Still Light, Ground/Flying, but add a chance for Small units.
    {10, MonsterWaveType::Light | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::Small, 0.45f}, {}, {}, {}}}, 1},

    // Waves 11-15: Introduce Medium units, increase Special and keep Flying chance.
    {15, MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::Special, 0.65f}, {MonsterWaveType::Small, 0.2f}, {}, {}}}, 2},

    // Waves 16-20: Introduce Heavy units, high chance for Fast, keep Flying.
    {20, MonsterWaveType::Medium | MonsterWaveType::Heavy | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::Fast, 0.55f}, {MonsterWaveType::Special, 0.3f}, {}, {}}}, 2},

    // Waves 21-25: Introduce Bomber units, keep others relevant.
    {25, MonsterWaveType::Bomber | MonsterWaveType::Heavy | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::Fast, 0.55f}, {MonsterWaveType::Special, 0.3f}, {}, {}}}, 2},

    // Waves 26-35: Focus on Heavy/Elite, high chance for Fast/Special, moderate Flying
    {35, MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::Special | MonsterWaveType::Fast, 0.75f}, {MonsterWaveType::Medium, 0.28f}, {}, {}}}, 2},

    // Waves 36-40: Add SemiBoss chance, keep others relevant
    {40, MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Special | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::SemiBoss, 0.45f}, {MonsterWaveType::Fast | MonsterWaveType::Bomber, 0.35f}, {MonsterWaveType::Medium, 0.30f}, {}}}, 3},

    // Waves 41+: High chance for SemiBoss, Flying, Fast
    {999, MonsterWaveType::Elite | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::SemiBoss, 0.35f}, {MonsterWaveType::Bomber | MonsterWaveType::Spawner, 0.6f}, {}, {}}}, 2}
}};
// Updated function to get the wave type
inline MonsterWaveType GetWaveComposition(int waveNumber, bool forceSpecialWave = false)
{
	const int32_t numHumanPlayers = GetNumHumanPlayers();
	MonsterWaveType selected_type = MonsterWaveType::None;

	// Special waves check - removed the wave 5-9 restriction
	struct SpecialWave
	{
		MonsterWaveType type;
		float chance;
		int min_wave;
		int max_wave;
		const char *message;
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
		{MonsterWaveType::Spawner, 0.75f, 25, -1, "*** Spawners Deployed! ***\n"}};

	if (!forceSpecialWave && !WasLastWaveSpecial())
	{
		for (const auto &wave : special_waves)
		{
			if (waveNumber >= wave.min_wave &&
				(wave.max_wave == -1 || waveNumber <= wave.max_wave) &&
				!WasRecentlyUsed(wave.type) &&
				frandom() < wave.chance)
			{
				selected_type = wave.type;
				gi.LocBroadcast_Print(PRINT_HIGH, wave.message);
				StoreWaveType(selected_type); // Store special wave type
				return selected_type;		  // Return immediately
			}
		}
	}
	// --- End Special Wave Logic ---

	// --- Regular Wave Composition using the new structure ---

	// Find the first definition where waveNumber <= max_wave
	// This works because the vector is sorted by max_wave
	auto it = std::find_if(wave_definitions.begin(), wave_definitions.end(),
						   [waveNumber](const WaveDefinition &def)
						   {
							   return waveNumber <= def.max_wave;
						   });

	// Handle the case where no definition matches (shouldn't happen with a catch-all)
	if (it == wave_definitions.end())
	{
		if (developer->integer)
		{
			gi.Com_PrintFmt("Warning: No wave definition found for wave {}. Using default.\n", waveNumber);
		}
		// Return a safe default (e.g., the first definition's base)
		selected_type = wave_definitions[0].base_type;
	}
	else
	{
		// Apply the base type from the found definition
		const WaveDefinition &def = *it;
		selected_type = def.base_type;

		// Process optional components for this definition
		for (const auto &optional : def.optionals)
		{
			// Check if the type is valid, passes the random chance, and wasn't recently used
			if (optional.type != MonsterWaveType::None &&
				frandom() < optional.chance &&
				!WasRecentlyUsed(optional.type))
			{
				// Combine the optional type with the selected type using the overloaded '|' operator
				selected_type |= optional.type;

				// Optional: Play sound only for specific significant additions like Flying
				if (HasWaveType(optional.type, MonsterWaveType::Flying))
				{
					// Make sure 'incoming' sound index is valid and loaded
					if (incoming)
					{ // Check if the sound index is valid
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
void InitializeWaveType(int32_t waveLevel)
{
	current_wave_type = GetWaveComposition(waveLevel);
}

// Wave difficulty multiplier
// inline static float GetWaveDifficultyMultiplier(int waveNumber) noexcept {
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
bool GetPredictedScaledBounds(horde::MonsterTypeID typeId, vec3_t &out_mins, vec3_t &out_maxs);

// 1. Modified MonsterTypeInfo struct definition (ensure this is defined before the array)
struct MonsterTypeInfo
{
	// wave typeid
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
// This array is sorted by `minWave` to allow for optimized iteration.
static const MonsterTypeInfo monsterTypes[] = {
    // --- WAVE 1 ---
    {horde::MonsterTypeID::SOLDIER_LIGHT, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 1.0f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
    {horde::MonsterTypeID::SOLDIER,       MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 0.9f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
    {horde::MonsterTypeID::FLYER,         MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Fast,   1, 0.7f, {-16, -16, -24}, {16, 16, 16}, 1.0f},

    // --- WAVE 2 ---
    {horde::MonsterTypeID::SOLDIER_SS,    MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 2, 0.8f, {-16, -16, -24}, {16, 16, 32}, 1.0f},

    // --- WAVE 3 ---
    {horde::MonsterTypeID::INFANTRY_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 3, 0.85f, {-16, -16, -24}, {16, 16, 32}, 1.0f},

    // --- WAVE 4 ---
    {horde::MonsterTypeID::SOLDIER_HYPERGUN, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 4, 0.7f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
    {horde::MonsterTypeID::GEKK,             MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Small | MonsterWaveType::Mutant | MonsterWaveType::Gekk, 4, 0.7f, {-16, -16, -24}, {16, 16, -8}, 1.0f},

    // --- WAVE 5 ---
    {horde::MonsterTypeID::PARASITE,         MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Melee, 5, 0.6f, {-16, -16, -24}, {16, 16, 24}, 1.0f},

    // --- WAVE 6 ---
    {horde::MonsterTypeID::SOLDIER_RIPPER,   MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.8f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
    {horde::MonsterTypeID::FIXBOT,           MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.45f, {-16, -16, -12}, {16, 16, 12}, 1.4f},
    {horde::MonsterTypeID::BRAIN,            MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special | MonsterWaveType::Melee | MonsterWaveType::Mutant, 6, 0.7f, {-16, -16, -24}, {16, 16, -8}, 1.0f},
    {horde::MonsterTypeID::BERSERK,          MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Melee | MonsterWaveType::Berserk, 6, 0.8f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
    {horde::MonsterTypeID::CHICK,            MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.6f, {-32, -32, -24}, {32, 32, 64}, 1.0f},

    // --- WAVE 7 ---
    {horde::MonsterTypeID::HOVER_VANILLA,    MonsterWaveType::Flying | MonsterWaveType::Medium | MonsterWaveType::Light | MonsterWaveType::Ranged, 7, 0.6f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
    {horde::MonsterTypeID::STALKER,          MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Fast | MonsterWaveType::Arachnophobic, 7, 0.6f, {-28, -28, -18}, {28, 28, -4}, 1.0f},
    {horde::MonsterTypeID::MEDIC,            MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special, 7, 0.5f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
    {horde::MonsterTypeID::SPIDER,           MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 7, 0.35f, {-48, -48, -20}, {48, 48, 48}, 0.7f},

    // --- WAVE 8 ---
    {horde::MonsterTypeID::GUNNER_VANILLA,   MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 8, 0.8f, {-16, -16, -24}, {16, 16, 36}, 1.0f},

    // --- WAVE 9 ---
    {horde::MonsterTypeID::MUTANT,           MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 9, 0.7f, {-18, -18, -24}, {18, 18, 30}, 1.0f},

    // --- WAVE 10 ---
    {horde::MonsterTypeID::SOLDIER_LASERGUN, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 10, 0.8f, {-16, -16, -24}, {16, 16, 32}, 1.0f},

    // --- WAVE 11 ---
    {horde::MonsterTypeID::FLOATER,          MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Ranged, 11, 0.6f, {-24, -24, -24}, {24, 24, 48}, 0.9f},
    {horde::MonsterTypeID::INFANTRY,         MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Heavy | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 11, 0.85f, {-16, -16, -24}, {16, 16, 32}, 1.2f},

    // --- WAVE 12 ---
    {horde::MonsterTypeID::GUNCMDR_VANILLA,  MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 12, 0.4f, {-16, -16, -24}, {16, 16, 36}, 1.25f},
    {horde::MonsterTypeID::GLADIATOR,        MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged, 12, 0.7f, {-32, -32, -24}, {32, 32, 42}, 1.0f},
    {horde::MonsterTypeID::GUNNER,           MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 12, 0.8f, {-16, -16, -24}, {16, 16, 36}, 1.0f},

    // --- WAVE 13 ---
    {horde::MonsterTypeID::TANK_SPAWNER,     MonsterWaveType::Ground | MonsterWaveType::Spawner | MonsterWaveType::Heavy | MonsterWaveType::Medium | MonsterWaveType::Elite, 13, 0.6f, {-32, -32, -16}, {32, 32, 64}, 1.0f},
    {horde::MonsterTypeID::CHICK_HEAT,       MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Fast, 13, 0.6f, {-32, -32, -24}, {32, 32, 64}, 1.0f},

    // --- WAVE 14 ---
    {horde::MonsterTypeID::SHAMBLER_SMALL,   MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Mutant | MonsterWaveType::Shambler, 14, 0.4f, {-32, -32, -24}, {32, 32, 64}, 0.6f},
    {horde::MonsterTypeID::REDMUTANT,        MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Elite | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 14, 0.35f, {-18, -18, -24}, {18, 18, 30}, 1.1f},
    {horde::MonsterTypeID::TANK,             MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 14, 0.4f, {-32, -32, -16}, {32, 32, 64}, 1.0f},

    // --- WAVE 15 ---
    {horde::MonsterTypeID::GM_ARACHNID,      MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Heavy | MonsterWaveType::Elite, 15, 0.45f, {-48, -48, -20}, {48, 48, 48}, 0.85f},
    {horde::MonsterTypeID::GUNCMDR,          MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Bomber, 15, 0.7f, {-16, -16, -24}, {16, 16, 36}, 1.25f},

    // --- WAVE 16 ---
    {horde::MonsterTypeID::TANK_COMMANDER,   MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 16, 0.5f, {-32, -32, -16}, {32, 32, 64}, 1.0f},

    // --- WAVE 17 ---
    {horde::MonsterTypeID::RUNNERTANK,       MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Fast, 17, 0.5f, {-32, -32, -16}, {32, 32, 64}, 1.0f},

    // --- WAVE 18 ---
    {horde::MonsterTypeID::ARACHNID2,        MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 18, 0.4f, {-48, -48, -20}, {48, 48, 48}, 0.85f},
    {horde::MonsterTypeID::HOVER,            MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.5f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
    {horde::MonsterTypeID::GLADIATOR_B,      MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f, {-32, -32, -24}, {32, 32, 42}, 1.0f},
    {horde::MonsterTypeID::GLADIATOR_C,      MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f, {-32, -32, -24}, {32, 32, 42}, 1.0f},

    // --- WAVE 19 ---
    {horde::MonsterTypeID::FLOATER_TRACKER,  MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 19, 0.45f, {-24, -24, -24}, {24, 24, 48}, 1.0f},
    {horde::MonsterTypeID::DAEDALUS_BOMBER,  MonsterWaveType::Flying | MonsterWaveType::Fast| MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Bomber, 19, 0.35f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
    {horde::MonsterTypeID::BOSS2_64,         MonsterWaveType::Flying | MonsterWaveType::Elite, 19, 0.2f, {-60, -60, 0}, {60, 60, 90}, 0.6f},
    {horde::MonsterTypeID::BOSS2_MINI,       MonsterWaveType::Flying | MonsterWaveType::Elite, 19, 0.2f, {-60, -60, 0}, {60, 60, 90}, 0.6f},

    // --- WAVE 20 ---
    {horde::MonsterTypeID::PERRO_KL,         MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Fast | MonsterWaveType::Small, 20, 0.4f, {-16, -16, -24}, {16, 16, 24}, 1.0f},

    // --- WAVE 21 ---
    {horde::MonsterTypeID::DAEDALUS,         MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 21, 0.4f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
    {horde::MonsterTypeID::JANITOR,          MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Bomber, 21, 0.5f, {-64, -64, -0}, {64, 64, 112}, 0.6f},

    // --- WAVE 22 ---
    {horde::MonsterTypeID::SHAMBLER,         MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 22, 0.4f, {-32, -32, -24}, {32, 32, 64}, 1.0f},

    // --- WAVE 23 ---
    {horde::MonsterTypeID::MAKRON,           MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 23, 0.01f, {-30, -30, 0}, {30, 30, 90}, 1.0f},
    {horde::MonsterTypeID::WIDOW1,           MonsterWaveType::Ground | MonsterWaveType::Spawner | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 23, 0.15f, {-40, -40, 0}, {40, 40, 144}, 0.6f},

    // --- WAVE 25 ---
    {horde::MonsterTypeID::PSX_ARACHNID,     MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite | MonsterWaveType::Spawner, 25, 0.35f, {-48, -48, -20}, {48, 48, 48}, 1.0f},

    // --- WAVE 26 ---
    {horde::MonsterTypeID::JANITOR2,         MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Special | MonsterWaveType::Bomber, 26, 0.4f, {-96, -96, -66}, {96, 96, 62}, 0.4f},

    // --- WAVE 27 ---
    {horde::MonsterTypeID::MEDIC_COMMANDER,  MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.3f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
    {horde::MonsterTypeID::CARRIER_MINI,     MonsterWaveType::Flying | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.2f, {-56, -56, -44}, {56, 56, 44}, 0.6f},

    // --- WAVE 28 ---
    {horde::MonsterTypeID::TANK_64,          MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 28, 0.3f, {-32, -32, -16}, {32, 32, 64}, 1.1f},

    // --- WAVE 33 ---
    {horde::MonsterTypeID::SHAMBLER_KL,      MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 33, 0.23f, {-32, -32, -24}, {32, 32, 64}, 1.0f},
    {horde::MonsterTypeID::GUNCMDR_KL,       MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 33, 0.2f, {-16, -16, -24}, {16, 16, 36}, 1.25f},
    {horde::MonsterTypeID::JORG_SMALL,       MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Medium, 33, 0.4f, {-80, -80, 0}, {80, 80, 140}, 0.35f},

    // --- WAVE 41 ---
    {horde::MonsterTypeID::MAKRON_KL,        MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Elite, 41, 0.2f, {-30, -30, 0}, {30, 30, 90}, 1.0f},

    // --- SPECIAL / NOT NORMALLY SPAWNED (minWave 999) ---
    {horde::MonsterTypeID::TURRET,           MonsterWaveType::Ground | MonsterWaveType::Special, 999, 0.0f, {-16, -16, -16}, {16, 16, 16}, 1.0f},
    {horde::MonsterTypeID::SENTRYGUN,        MonsterWaveType::Ground | MonsterWaveType::Special, 999, 0.0f, {-16, -16, -16}, {16, 16, 16}, 1.0f},
    {horde::MonsterTypeID::BOSS2,            MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-60,-60,0}, {60,60,90}, 1.0f},
    {horde::MonsterTypeID::CARRIER,          MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-56,-56,-44}, {56,56,44}, 1.0f},
    {horde::MonsterTypeID::FIXBOT_KL,        MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-16,-16,-12}, {16,16,12}, 2.6f},
    {horde::MonsterTypeID::WIDOW,            MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-40,-40,0}, {40,40,144}, 1.0f},
    {horde::MonsterTypeID::WIDOW2,           MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 999, 0.0f, {-40,-40,0}, {40,40,144}, 0.8f},
    {horde::MonsterTypeID::BOSS5,            MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-32,-32,-16}, {32,32,64}, 1.0f},
    {horde::MonsterTypeID::JORG,             MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-80,-80,0}, {80,80,140}, 1.0f},
    {horde::MonsterTypeID::PSX_GUARDIAN,     MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-32,-32,-24}, {32,32,64}, 1.0f}
};
// Optimized version using a precomputed map
static std::array<MonsterWaveType, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_monsterWaveTypes;
static bool g_monsterWaveTypesInitialized = false;

// Initialize the wave types array once
static void InitializeMonsterWaveTypes()
{
	if (g_monsterWaveTypesInitialized)
		return;

	g_monsterWaveTypes.fill(MonsterWaveType::None);

	// Fill in all the wave types for each monster
	for (const auto &monster : monsterTypes)
	{
		horde::MonsterTypeID typeId = monster.typeId;
		if (typeId != horde::MonsterTypeID::UNKNOWN)
		{
			g_monsterWaveTypes[static_cast<size_t>(typeId)] = monster.types;
		}
	}

	g_monsterWaveTypesInitialized = true;
}

// Update GetMonsterWaveTypes to use the precomputed array
// Add a TypeID overload for GetMonsterWaveTypes
inline MonsterWaveType GetMonsterWaveTypes(horde::MonsterTypeID typeId) noexcept
{
	if (typeId == horde::MonsterTypeID::UNKNOWN)
		return MonsterWaveType::None;

	// Make sure the wave types are initialized
	if (!g_monsterWaveTypesInitialized)
	{
		InitializeMonsterWaveTypes();
	}

	return g_monsterWaveTypes[static_cast<size_t>(typeId)];
}

#include <array>
#include <unordered_set>
#include <random>

//======================================================================
// SECTION: Boss Definitions and Selection
//======================================================================

// The structure defining a boss type for the spawner.
struct boss_t
{
	horde::MonsterTypeID typeId;
	int32_t min_level;
	int32_t max_level;
	float weight;
	BossSizeCategory sizeCategory;
	BossType type;
};

// --- Boss Lists ---
// Converted to std::array for type safety and modern C++ practices.
// We explicitly provide the type and size for maximum compiler compatibility.

static constexpr std::array<boss_t, 11> BOSS_SMALL = {{
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
}};

static constexpr std::array<boss_t, 13> BOSS_MEDIUM = {{
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
}};

static constexpr std::array<boss_t, 17> BOSS_LARGE = {{
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
	{horde::MonsterTypeID::WIDOW2, -1, 24, 0.19f, BossSizeCategory::Small, BossType::WIDOW2}
}};

// This function selects the appropriate boss list based on map size and specific map IDs.
// It returns a std::span, which is a safe, non-owning view of the underlying array data.
static std::span<const boss_t> GetBossList(const horde::MapSize &mapSize, horde::MapID mapId)
{
	// --- Small Maps ---
	if (mapSize.isSmallMap ||
		mapId == horde::MapID::Q2DM4 ||
		mapId == horde::MapID::Q64_COMM ||
		mapId == horde::MapID::TEST_TEST_KAISER)
	{
		return BOSS_SMALL;
	}

	// --- Medium Maps ---
	if (mapSize.isMediumMap ||
		mapId == horde::MapID::RDM8 ||
		mapId == horde::MapID::XDM1)
	{
		if (mapId == horde::MapID::MGU6M3 || mapId == horde::MapID::RBOSS)
		{
			// Create a static, filtered list once at program startup.
			static const auto filtered_medium_list = [] {
				std::vector<boss_t> list;
				list.reserve(std::size(BOSS_MEDIUM));
				for (const auto& boss : BOSS_MEDIUM) {
					if (boss.type != BossType::PSX_GUARDIAN) {
						list.push_back(boss);
					}
				}
				return list;
			}();
			return filtered_medium_list;
		}
		return BOSS_MEDIUM;
	}

	// --- Large Maps ---
	if (mapSize.isBigMap ||
		mapId == horde::MapID::TEST_SPBOX || mapId == horde::MapID::Q2CTF4)
	{
		if (mapId == horde::MapID::TEST_SPBOX || mapId == horde::MapID::Q2CTF4)
		{
			// Create a static, filtered list once at program startup.
			static const auto filtered_large_list = [] {
				std::vector<boss_t> list;
				list.reserve(std::size(BOSS_LARGE));
				for (const auto& boss : BOSS_LARGE) {
					if (boss.type != BossType::BOSS5) {
						list.push_back(boss);
					}
				}
				return list;
			}();
			return filtered_large_list;
		}
		return BOSS_LARGE;
	}

	return {}; // Return an empty span if no conditions match
}

struct EligibleBosses
{
	// The array stores pointers to const boss_t objects.
	// The pointers themselves can be changed (which boss_t they point to),
	// but the boss_t objects they point to cannot be modified through these pointers.
	std::array<const boss_t *, MAX_ELIGIBLE_BOSSES> items;
	size_t count; // Number of valid pointers currently in the array

	// Type alias for the iterator type returned by const begin()/end()
	// items.data() in a const member function returns: const (value_type)*
	// where value_type is (const boss_t*).
	// So, it's const (const boss_t*)*, which is const boss_t* const*
	using const_iterator = const boss_t *const *;

	// Default constructor
	EligibleBosses() noexcept : count(0)
	{
		items.fill(nullptr); // Initialize all pointers to nullptr
	}

	// Adds a pointer to a const boss_t struct to the list.
	// Returns true if successful, false if the list is full or boss_ptr is null.
	bool add(const boss_t *boss_ptr) noexcept
	{
		if (!boss_ptr)
		{
			return false; // Do not add null pointers
		}
		if (count >= MAX_ELIGIBLE_BOSSES)
		{
			// Optionally log an error or warning if the list is full
			// Example: gi.Com_PrintFmt("Warning: EligibleBosses list is full (max {}). Cannot add more.\n", MAX_ELIGIBLE_BOSSES);
			return false; // List is full
		}

		items[count] = boss_ptr;
		count++;
		return true;
	}

	// Clears the list of eligible bosses.
	void clear() noexcept
	{
		// Only fill the portion that was used, for minor efficiency.
		// Or, items.fill(nullptr) if you prefer to clear the whole array.
		if (count > 0)
		{
			std::fill_n(items.begin(), count, nullptr);
		}
		count = 0;
	}

	// Gets the number of eligible bosses currently tracked.
	size_t size() const noexcept
	{
		return count;
	}

	// Checks if the list is empty.
	bool empty() const noexcept
	{
		return count == 0;
	}

	// Accesses an item by index (const version).
	// Returns nullptr if the index is out of bounds.
	const boss_t *get(size_t index) const noexcept
	{
		if (index < count)
		{
			return items[index];
		}
		return nullptr;
	}

	// Provides a const_iterator to the beginning of the valid data.
	// Allows: for (const boss_t* boss_ptr : eligible_boss_instance)
	const_iterator begin() const noexcept
	{
		return items.data(); // Returns const (const boss_t*)*
	}

	// Provides a const_iterator to one past the end of the valid data.
	const_iterator end() const noexcept
	{
		return items.data() + count; // Pointer arithmetic
	}
};

struct RecentBosses
{
	std::array<horde::MonsterTypeID, MAX_RECENT_BOSSES> items;
	size_t count; // Number of valid items currently in the array (from index 0 to count-1)

	// Default constructor
	RecentBosses() noexcept : count(0)
	{
		items.fill(horde::MonsterTypeID::UNKNOWN); // Initialize all slots to UNKNOWN
	}

	// Adds a boss TypeID to the recent list.
	// If the list is full, the oldest entry is removed (shifts elements).
	void add(horde::MonsterTypeID boss_id) noexcept
	{
		if (boss_id == horde::MonsterTypeID::UNKNOWN)
		{
			return; // Do not add unknown/invalid bosses
		}

		// Optional: Prevent adding if it's already the most recent one.
		// if (count > 0 && items[count - 1] == boss_id) {
		//     return;
		// }

		if (count < MAX_RECENT_BOSSES)
		{
			// List is not full, just add to the next available slot
			items[count] = boss_id;
			count++;
		}
		else
		{
			// List is full. Shift all elements to the left to make space at the end.
			// The oldest element (items[0]) is overwritten.
			for (size_t i = 0; i < MAX_RECENT_BOSSES - 1; ++i)
			{
				items[i] = items[i + 1];
			}
			items[MAX_RECENT_BOSSES - 1] = boss_id;
			// count remains MAX_RECENT_BOSSES
		}
	}

	// Compatibility overload: Adds a boss by its classname.
	void add(const char *boss_classname) noexcept
	{
		if (!boss_classname)
		{
			return;
		}
		// Assuming MonsterTypeRegistry is accessible, e.g., via g_MonsterTypeRegistry or similar
		horde::MonsterTypeID boss_id = horde::MonsterTypeRegistry::GetTypeID(boss_classname);
		add(boss_id); // Delegate to the TypeID version
	}

	// Checks if a boss TypeID is in the recent list.
	bool contains(horde::MonsterTypeID boss_id) const noexcept
	{
		if (boss_id == horde::MonsterTypeID::UNKNOWN)
		{
			return false;
		}
		// Iterate only up to 'count' valid items
		for (size_t i = 0; i < count; ++i)
		{
			if (items[i] == boss_id)
			{
				return true;
			}
		}
		return false;
	}

	// Compatibility overload: Checks if a boss classname is in the recent list.
	bool contains(const char *boss_classname) const noexcept
	{
		if (!boss_classname)
		{
			return false;
		}
		horde::MonsterTypeID boss_id = horde::MonsterTypeRegistry::GetTypeID(boss_classname);
		return contains(boss_id); // Delegate to the TypeID version
	}

	// Clears the list of recent bosses.
	void clear() noexcept
	{
		items.fill(horde::MonsterTypeID::UNKNOWN);
		count = 0;
	}

	// Gets the number of bosses currently tracked.
	size_t size() const noexcept
	{
		return count;
	}

	// Checks if the list is empty.
	bool empty() const noexcept
	{
		return count == 0;
	}
};
static RecentBosses recent_bosses;

// Precomputed boss eligibility cache to avoid repeated calculations
struct BossEligibilityCache
{
	static constexpr int32_t MAX_PRECOMPUTED_WAVE = 50;

	struct LevelEligibility
	{
		horde::MonsterTypeID typeIds[MAX_ELIGIBLE_BOSSES] = {horde::MonsterTypeID::UNKNOWN};
		uint8_t count = 0;
	};

	// Cache by map type (0=small, 1=medium, 2=large) and wave level
	LevelEligibility eligibility[3][MAX_PRECOMPUTED_WAVE + 1];
	inline static bool initialized = false;

	// REPLACEMENT: BossEligibilityCache::initialize (with bug fix)
	void initialize()
	{
		if (initialized) return;

		// For each map type (0=small, 1=medium, 2=large)
		for (int mapType = 0; mapType < 3; ++mapType)
		{
			// FIX: Correctly create a MapSize struct for the current type
			horde::MapSize mapSize;
			if (mapType == 0)       mapSize = {true, false, false}; // Small
			else if (mapType == 1)  mapSize = {false, false, true}; // Medium
			else                    mapSize = {false, true, false}; // Large

			// Get the correct boss list for this map type
			const auto bossList = GetBossList(mapSize, horde::MapID::UNKNOWN);

			// For each wave level we want to pre-compute
			for (int32_t wave = 0; wave <= MAX_PRECOMPUTED_WAVE; ++wave)
			{
				auto& levelData = eligibility[mapType][wave];
				levelData.count = 0;

				// Filter bosses from this map's list by level and store their TypeIDs
				for (const auto& boss : bossList)
				{
					if ((wave >= boss.min_level || boss.min_level == -1) &&
						(wave <= boss.max_level || boss.max_level == -1))
					{
						if (levelData.count < MAX_ELIGIBLE_BOSSES)
						{
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

static edict_t *CreateBaseHordeMonster(horde::MonsterTypeID typeId, const vec3_t &origin, const vec3_t &angles)
{
	// Convert TypeID to classname
	const char *classname = horde::MonsterTypeRegistry::GetClassname(typeId);
	if (!classname)
	{
		if (developer->integer)
		{
			gi.Com_PrintFmt("CreateBaseHordeMonster: Invalid TypeID provided.\n");
		}
		return nullptr;
	}

	// Create the entity
	edict_t *monster = G_Spawn();
	if (!monster)
	{
		if (developer->integer)
		{
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
	if (g_horde_local.state == horde_state_t::spawning)
	{
		monster->spawned_in_spawn_state = true;
	}

	// Return the initialized (but not yet fully spawned/linked) entity
	return monster;
}

edict_t *SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t &origin, const vec3_t &angles, bool applyHordeFlags)
{
	const char *classname = horde::MonsterTypeRegistry::GetClassname(typeId);
	if (!classname)
	{
		if (developer->integer)
		{
			gi.Com_PrintFmt("SpawnMonsterByTypeID: Failed to get classname for TypeID {}\n", static_cast<int>(typeId));
		}
		return nullptr;
	}

	edict_t *monster = CreateBaseHordeMonster(typeId, origin, angles);
	if (!monster)
	{
		return nullptr;
	}

	monster->classname = classname;
	monster->s.origin = origin;
	monster->s.angles = angles;

	if (applyHordeFlags)
	{
		monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
		monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
		monster->monsterinfo.last_sentrygun_target_time = 0_ms;
		monster->was_spawned_by_horde = true;

		if (g_horde_local.state == horde_state_t::spawning)
		{
			monster->spawned_in_spawn_state = true;
		}
	}

	// Set non-solid for ED_CallSpawn
	monster->solid = SOLID_NOT;
	ED_CallSpawn(monster);

	// Check if ED_CallSpawn freed the entity
	if (!monster->inuse)
	{
		if (developer->integer)
		{ // Added dev print for clarity
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
	if (spawn_was_stuck)
	{
		// Only flag if not already flagged by monster_start_go
		if (!monster->monsterinfo.was_stuck)
		{
			edict_t *blocker = post_link_trace.ent;
			if (developer->integer)
			{
				// Clarified log message
				gi.Com_PrintFmt("SpawnMonsterByTypeID: WARNING - {} stuck in GEOMETRY (Post-Link Check) at {}. Blocker: {} ({}). Flagging for teleport.\n",
								monster->classname, monster->s.origin,
								blocker ? (blocker->classname ? blocker->classname : "unknown") : "world/unknown",
								blocker ? (blocker - g_edicts) : -1);
			}
			monster->monsterinfo.was_stuck = true;
			monster->monsterinfo.stuck_check_time = level.time; // Start timer immediately
		}
		else if (developer->integer > 1)
		{
			// Already flagged by monster_start_go, just log confirmation
			gi.Com_PrintFmt("SpawnMonsterByTypeID: {} confirmed stuck (already flagged by monster_start_go).\n", monster->classname);
		}
	}

	monster->monsterinfo.spawn_complete_time = level.time;

	return monster;
}

// Optional overload with fewer parameters for common cases
edict_t *SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t &origin)
{
	return SpawnMonsterByTypeID(typeId, origin, vec3_origin, true);
}

// Add this struct definition in your header file or at the top
struct BossPickResult
{
	horde::MonsterTypeID typeId;
	BossSizeCategory sizeCategory;

	// Constructor for convenience
	BossPickResult(horde::MonsterTypeID id = horde::MonsterTypeID::UNKNOWN,
				   BossSizeCategory size = BossSizeCategory::Medium)
		: typeId(id), sizeCategory(size) {}
};

//======================================================================
// REPLACEMENT: G_HordePickBOSSType
// Uses the pre-computed cache for massive performance improvement.
//======================================================================
static BossPickResult G_HordePickBOSSType(const horde::MapSize& mapSize, std::string_view mapname, int32_t waveNumber)
{
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(mapname.data());

	// Initialize boss eligibility cache if needed (one-time)
	if (!g_bossEligibilityCache.initialized) {
		g_bossEligibilityCache.initialize();
	}

	// Determine map type index for cache lookup
	const int mapTypeIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);
	const int32_t safeWaveNumber = std::min(waveNumber, BossEligibilityCache::MAX_PRECOMPUTED_WAVE);

	// Get the full boss list to look up details like weight and size category later
	const auto boss_list = GetBossList(mapSize, mapId);
	if (boss_list.empty()) {
		if (developer->integer) gi.Com_PrintFmt("WARNING: Empty boss list for map {} at wave {}\n", mapname.data(), waveNumber);
		return BossPickResult(); // Returns UNKNOWN
	}

	// Get the pre-filtered list of eligible boss TypeIDs from the cache
	const auto& eligibilityData = g_bossEligibilityCache.eligibility[mapTypeIndex][safeWaveNumber];
	if (eligibilityData.count == 0) {
		if (developer->integer) gi.Com_PrintFmt("WARNING: No bosses eligible for wave {} on this map type.\n", waveNumber);
		return BossPickResult();
	}

	// Use a stack-based array for weight calculations
	struct WeightedBoss {
		const boss_t* boss;
		float weight;
		float cumulativeWeight;
	};
	std::array<WeightedBoss, MAX_ELIGIBLE_BOSSES> weightedBosses{};
	size_t weightedCount = 0;
	float totalWeight = 0.0f;

	// --- First Pass: Filter pre-computed list against recent history ---
	for (size_t i = 0; i < eligibilityData.count; ++i)
	{
		horde::MonsterTypeID bossTypeId = eligibilityData.typeIds[i];
		if (bossTypeId == horde::MonsterTypeID::UNKNOWN || recent_bosses.contains(bossTypeId)) {
			continue;
		}

		// Find the full boss_t entry to get its weight and other properties
		const boss_t* boss_details = nullptr;
		for (const auto& b : boss_list) {
			if (b.typeId == bossTypeId) {
				boss_details = &b;
				break;
			}
		}
		if (!boss_details) continue; // Should not happen if cache is correct

		// Calculate weight
		float weight = boss_details->weight;
		// (Add other dynamic weight adjustments here if needed)

		// Add to our temporary weighted list
		if (weightedCount < MAX_ELIGIBLE_BOSSES) {
			totalWeight += weight;
			weightedBosses[weightedCount].boss = boss_details;
			weightedBosses[weightedCount].weight = weight;
			weightedBosses[weightedCount].cumulativeWeight = totalWeight;
			weightedCount++;
		}
	}

	// --- Fallback: If history filter removed all options, ignore history ---
	if (weightedCount == 0 && recent_bosses.size() > 0)
	{
		if (developer->integer > 1) gi.Com_PrintFmt("INFO: No non-recent bosses eligible, ignoring history for this pick.\n");
		totalWeight = 0.0f; // Reset for the new pass
        weightedCount = 0;  // Reset the counter

		for (size_t i = 0; i < eligibilityData.count; ++i) {
			horde::MonsterTypeID bossTypeId = eligibilityData.typeIds[i];
			if (bossTypeId == horde::MonsterTypeID::UNKNOWN) continue;

			const boss_t* boss_details = nullptr;
			for (const auto& b : boss_list) {
				if (b.typeId == bossTypeId) {
					boss_details = &b;
					break;
				}
			}
			if (!boss_details) continue;

			float weight = boss_details->weight;
			if (weightedCount < MAX_ELIGIBLE_BOSSES) {
				totalWeight += weight;
				weightedBosses[weightedCount].boss = boss_details;
				weightedBosses[weightedCount].weight = weight;
				weightedBosses[weightedCount].cumulativeWeight = totalWeight;
				weightedCount++;
			}
		}
	}

	// --- Final Selection ---
	if (weightedCount == 0 || totalWeight <= 0.0f) {
		if (developer->integer) gi.Com_PrintFmt("WARNING: No eligible bosses found after all filtering.\n");
		return BossPickResult();
	}

	// Weighted random selection using binary search for efficiency
	const float randomValue = frandom() * totalWeight;
	size_t left = 0, right = weightedCount - 1;
	while (left < right) {
		const size_t mid = left + (right - left) / 2;
		if (weightedBosses[mid].cumulativeWeight < randomValue) {
			left = mid + 1;
		} else {
			right = mid;
		}
	}

	const boss_t* chosen_boss = weightedBosses[left].boss;
	if (chosen_boss) {
		recent_bosses.add(chosen_boss->typeId);
		if (developer->integer > 1) {
			const char* chosen_name = horde::MonsterTypeRegistry::GetClassname(chosen_boss->typeId);
			gi.Com_PrintFmt("Selected Boss: {} (Weight: {:.2f})\n", chosen_name ? chosen_name : "Unknown", weightedBosses[left].weight);
		}
		return BossPickResult(chosen_boss->typeId, chosen_boss->sizeCategory);
	}

	return BossPickResult(); // Fallback
}

struct picked_item_t
{
	const weighted_item_t *item;
	float weight;
};

// Adapted Caching Structure (using std::array for fixed size based on input)
struct HordeItemSelectionCache
{
	// Automatically size based on the actual data array
	static constexpr size_t MAX_ENTRIES = std::size(hordeItemData);
	struct Entry
	{
		const HordeItemInfo *itemInfo; // Pointer to the HordeItemInfo entry
		float weight;
		float cumulative_weight;
	};

	size_t count = 0;
	float total_weight = 0.0f;
	// Use std::array for compile-time sized array based on hordeItemData
	std::array<Entry, MAX_ENTRIES> entries{}; // Value-initialize

	void clear() noexcept
	{
		count = 0;
		total_weight = 0.0f;
		// No need to clear array elements explicitly when count = 0
	}
};
// Static cache instance specifically for Horde item selection
static HordeItemSelectionCache horde_item_cache;

// Modified Function using HordeItemInfo and item_id_t
gitem_t *G_HordePickItem()
{
	// Reset cache for this selection attempt
	horde_item_cache.clear();

	// Use std::span for safe iteration over the hordeItemData array
	std::span<const HordeItemInfo> items_view{hordeItemData};

	// --- Collect Eligible Items ---
	for (const auto &hordeItemInfo : items_view)
	{
		// Check if cache is full (safety check, should not happen with std::array)
		if (horde_item_cache.count >= HordeItemSelectionCache::MAX_ENTRIES)
		{
			if (developer->integer)
			{ // Log error if this happens unexpectedly
				gi.Com_PrintFmt("Warning: HordeItemSelectionCache full! Increase MAX_ENTRIES if not using std::array.\n");
			}
			break;
		}

		// Filter based on minimum wave level required for the item
		if (g_horde_local.level >= hordeItemInfo.minWave)
		{

			// Use the weight directly from HordeItemInfo
			// Future Enhancements: Add more complex weight adjustments here
			// (e.g., based on player count, current inventory, game state)
			float adjusted_weight = hordeItemInfo.weight;

			// Ensure weight is positive before adding to the cache
			if (adjusted_weight > 0.0f)
			{
				horde_item_cache.total_weight += adjusted_weight;
				// Get reference to the next entry in the cache array
				auto &entry = horde_item_cache.entries[horde_item_cache.count];
				entry.itemInfo = &hordeItemInfo; // Store pointer to the HordeItemInfo struct
				entry.weight = adjusted_weight;
				entry.cumulative_weight = horde_item_cache.total_weight;
				horde_item_cache.count++; // Increment the count of eligible items
			}
		}
	} // End of item collection loop

	// Check if any eligible items were found
	if (horde_item_cache.count == 0 || horde_item_cache.total_weight <= 0.0f)
	{
		// Log if no items found (useful for debugging balance/data issues)
		if (developer->integer)
		{
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
		while (left < right)
		{
			// Calculate midpoint safely to avoid potential overflow with large indices
			const size_t mid = left + (right - left) / 2;
			if (horde_item_cache.entries[mid].cumulative_weight < random_value)
			{
				left = mid + 1; // Random value is in the upper half
			}
			else
			{
				right = mid; // Random value is in the lower half (including mid)
			}
		}
	}
	// After the loop, 'left' holds the index of the chosen item entry in the cache

	// *** ADDED SAFETY CHECK ***
	// Although the binary search should ensure 'left' is valid, this check
	// satisfies static analysis and guards against potential edge cases.
	if (left >= horde_item_cache.count)
	{
		if (developer->integer)
		{
			gi.Com_PrintFmt("CRITICAL ERROR: G_HordePickItem - Binary search resulted in invalid index 'left' ({}) >= 'count' ({}).\n",
							left, horde_item_cache.count);
		}
		return nullptr; // Prevent accessing invalid memory
	}
	// *** END SAFETY CHECK ***

	// --- Retrieve and Return the Item ---
	// Get the chosen HordeItemInfo pointer from the cache entry
	const HordeItemInfo *chosen_info = horde_item_cache.entries[left].itemInfo; // Now accessing with checked 'left'

	// Safety check: Ensure chosen_info is not null (shouldn't happen if count > 0 and check above passed)
	if (!chosen_info)
	{
		if (developer->integer)
		{
			// This would indicate an error storing the pointer earlier, less likely
			gi.Com_PrintFmt("Error: G_HordePickItem - chosen_info is null despite valid index {}.\n", left);
		}
		return nullptr;
	}

	// Use the item_id_t from the chosen info to get the actual gitem_t pointer
	// This replaces the old FindItemByClassname call
	return GetItemByIndex(chosen_info->id);
}

static int32_t countFlyingSpawns() noexcept
{
	return std::count_if(g_edicts + 1, g_edicts + globals.num_edicts,
						 [](const edict_t &ent)
						 {
							 return ent.inuse &&
									strcmp(ent.classname, "info_player_deathmatch") == 0 &&
									ent.style == 1;
						 });
}

static float adjustFlyingSpawnProbability(int32_t flyingSpawns) noexcept
{
	switch (flyingSpawns)
	{
	case 0:
		return 1.0f;
	case 1:
		return 0.9f;
	case 2:
		return 0.8f;
	case 3:
		return 0.6f;
	default:
		return 0.5f;
	}
}

static void UpdateCooldowns(edict_t *spawn_point)
{
	auto &data = spawnPointsData[spawn_point];
	data.lastSpawnTime = level.time;
	data.isTemporarilyDisabled = true;
	data.cooldownEndsAt = level.time + SPAWN_POINT_COOLDOWN;
}

// Category check functions using TypeIDs
inline bool IsFlying(horde::MonsterTypeID typeId)
{
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Flying);
}

inline bool IsGroundUnit(horde::MonsterTypeID typeId)
{
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Ground);
}

inline bool IsSmallUnit(horde::MonsterTypeID typeId)
{
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Small);
}

inline bool IsBossUnit(horde::MonsterTypeID typeId)
{
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Boss);
}

inline bool IsSpecialUnit(horde::MonsterTypeID typeId)
{
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Special);
}

// REPLACEMENT: IsValidMonsterForWave

inline bool IsValidMonsterForWave(horde::MonsterTypeID typeId, MonsterWaveType waveRequirements)
{
    // Fast exit for special cases like boss minion waves that have no requirements.
    if (waveRequirements == MonsterWaveType::None) {
        return true;
    }

    // Use the fast LUT to get the monster's properties.
    const MonsterWaveType monster_flags = GetMonsterWaveTypes(typeId);

    // --- Step 1: Handle Exclusive, Thematic Waves (Strict Matching) ---
    // If the wave is a special theme (Gekk, Mutant, etc.), the monster MUST match that theme.
    static constexpr std::array<MonsterWaveType, 6> special_themes = {
        MonsterWaveType::Gekk, MonsterWaveType::Berserk, MonsterWaveType::Mutant,
        MonsterWaveType::Spawner, MonsterWaveType::Shambler, MonsterWaveType::Arachnophobic
    };

    for (const auto& theme : special_themes) {
        if (HasWaveType(waveRequirements, theme)) {
            // If the wave has this theme, the monster must also have it.
            return HasWaveType(monster_flags, theme);
        }
    }

    // --- Step 2: Handle General Wave Composition (Flexible Matching) ---
    // This part is for non-special waves, allowing a mix of units.

    // A. Movement Type Check: This is now flexible.
    const bool wave_wants_ground = HasWaveType(waveRequirements, MonsterWaveType::Ground);
    const bool wave_wants_flying = HasWaveType(waveRequirements, MonsterWaveType::Flying);
    const bool monster_is_ground = HasWaveType(monster_flags, MonsterWaveType::Ground);
    const bool monster_is_flying = HasWaveType(monster_flags, MonsterWaveType::Flying);

    // If the wave wants BOTH ground and flying, the monster must be one or the other.
    if (wave_wants_ground && wave_wants_flying) {
        if (!monster_is_ground && !monster_is_flying) return false;
    }
    // If the wave wants ONLY ground, the monster must be ground.
    else if (wave_wants_ground) {
        if (!monster_is_ground) return false;
    }
    // If the wave wants ONLY flying, the monster must be flying.
    else if (wave_wants_flying) {
        if (!monster_is_flying) return false;
    }
    // If the wave specifies no movement type, any monster is fine in this regard.

    // B. Category Check: The monster must match at least one of the main categories of the wave.
    constexpr MonsterWaveType category_mask =
        MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Heavy |
        MonsterWaveType::Small | MonsterWaveType::Fast | MonsterWaveType::Elite |
        MonsterWaveType::Melee | MonsterWaveType::Ranged | MonsterWaveType::Bomber |
        MonsterWaveType::Special;

    const MonsterWaveType required_categories = waveRequirements & category_mask;

    // If the wave specifies any categories, the monster must have at least one of them.
    if (required_categories != MonsterWaveType::None) {
        if ((monster_flags & required_categories) == MonsterWaveType::None) {
            return false;
        }
    }

    // If all checks pass, the monster is valid for this wave.
    return true;
}

// ADDED: Monster Info LUT global variable
static std::array<const MonsterTypeInfo *, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_monster_info_lut;
static bool g_monster_info_lut_initialized = false;

// =======================================================================
// BEGIN: COMPLETE MONSTER PICKING SYSTEM
// =======================================================================

//-----------------------------------------------------
// Helper structures for monster selection
//-----------------------------------------------------
struct MonsterSelectionContext
{
	int32_t currentActualLevel;			   // The true current wave level
	int32_t effectiveLevel;				   // Potentially higher level for selection
	MonsterWaveType waveTypeForFiltering;  // The (potentially simplified) wave type for general filtering
	MonsterWaveType currentActualWaveType; // The true current wave type for specific checks
	bool isSpawnPointFlying;
	bool isRetaliationActive;
	bool isRecoveryModeActive;
	float flyingAdjustmentFactor;
	bool isBossWaveMinionPhase;
};

struct MonsterCache
{
	struct Entry
	{
		horde::MonsterTypeID typeId;
		float weight;
		float cumulative_weight;
	};

	static constexpr size_t MONSTER_CACHE_SIZE = std::size(monsterTypes);
	std::array<Entry, MONSTER_CACHE_SIZE> entries{};
	size_t count = 0;
	float total_weight = 0.0f;

	void clear() noexcept
	{
		count = 0;
		total_weight = 0.0f;
	}

	void addMonster(horde::MonsterTypeID typeId, float weight)
	{
		if (count >= MONSTER_CACHE_SIZE || weight <= 0.0f)
			return;

		total_weight += weight;
		auto &entry = entries[count];
		entry.typeId = typeId;
		entry.weight = weight;
		entry.cumulative_weight = total_weight;
		count++;
	}
};
// Global static instance for the picker's internal cache.
static MonsterCache g_monster_picker_internal_cache;


//-----------------------------------------------------
// Determine if we should try to spawn a higher level monster
//-----------------------------------------------------
static bool ShouldAttemptHigherLevelSpawn(int32_t currentLevel, bool isRetaliationActive, bool isRecoveryModeActive)
{
	// Don't spawn a special elite if we're already in a special mode or if one has already spawned
	if (g_special_high_level_monster_spawned_this_wave || isRetaliationActive || isRecoveryModeActive)
	{
		return false;
	}

	// Define probabilities based on wave progression
	if (currentLevel <= 10) return frandom() < 0.32f; // 32% chance in early waves
	if (currentLevel <= 20) return frandom() < 0.19f; // 19% chance in mid waves
	return frandom() < 0.07f;	                      // 7% chance in late waves
}

// REPLACEMENT for CalculateEffectiveMonsterLevel (with bug fix)
static int32_t CalculateEffectiveMonsterLevel(int32_t currentActualLevel, bool attemptHigherLevel, MonsterWaveType waveTypeForFiltering)
{
    if (!attemptHigherLevel || g_horde_local.level <= 3)
    {
        return currentActualLevel; // No change needed.
    }

    int32_t levelBoost;
    int32_t maxLevelCap;
    const bool isFlyingWave = HasWaveType(waveTypeForFiltering, MonsterWaveType::Flying);

    // Use a more aggressive boost for flying waves to introduce elite flyers.
    if (isFlyingWave)
    {
        levelBoost = irandom(6, 17);
        maxLevelCap = currentActualLevel + 11; 
    }
    else
    {
        // Use the original, more conservative boost for ground waves.
        if (currentActualLevel < 7)      levelBoost = irandom(2, 4);
        else if (currentActualLevel <= 15) levelBoost = irandom(4, 8);
        else                             levelBoost = irandom(3, 6);
        maxLevelCap = currentActualLevel + 8;
    }

    maxLevelCap = std::min(maxLevelCap, 45); // Absolute cap.

    int32_t potentialEffectiveLevel = std::min(currentActualLevel + levelBoost, maxLevelCap);

    // --- CRITICAL FIX ---
    // We must check the MASTER list of all monsters (`monsterTypes`) to see if any
    // are unlocked by the new effective level. Checking `g_eligible_monsters_for_wave`
    // will never work because it's already filtered for the current level.
    bool any_new_monsters_unlocked = false;
    for (const auto& monster : monsterTypes) // Iterate the full list
    {
        // Is there a monster that is eligible at the new level but was NOT at the old one?
        if (monster.minWave > currentActualLevel && 
            monster.minWave <= potentialEffectiveLevel &&
            IsValidMonsterForWave(monster.typeId, waveTypeForFiltering))
        {
            any_new_monsters_unlocked = true;
            break; // Found one, no need to check further.
        }
    }

    if (!any_new_monsters_unlocked)
    {
        if (developer->integer > 1)
        {
            gi.Com_PrintFmt("CalculateEffectiveMonsterLevel: No new monsters unlocked at level {}. Reverting to {}.\n",
                            potentialEffectiveLevel, currentActualLevel);
        }
        return currentActualLevel; // Revert if the boost is meaningless.
    }

    if (developer->integer)
    {
        gi.Com_PrintFmt("CalculateEffectiveMonsterLevel: Attempting ELITE spawn. Using effective level {} (Current is {}).\n", potentialEffectiveLevel, currentActualLevel);
    }
    return potentialEffectiveLevel;
}

//-----------------------------------------------------
// (The following helper functions are unchanged but required)
//-----------------------------------------------------

static float CalculateBaseWeight(const MonsterTypeInfo &monster, const MonsterSelectionContext &ctx)
{
	float weight = monster.weight;
	if (monster.minWave < ctx.currentActualLevel)
	{
		float relevance = 1.0f - std::min(0.6f, (ctx.currentActualLevel - monster.minWave) * 0.04f);
		weight *= relevance;
	}
	if (weight < 0.1f && monster.weight >= 0.1f)
	{
		weight = 0.1f;
	}
	return weight;
}

static float ApplySpecialModifiers(float weight, const MonsterTypeInfo &monster, const MonsterSelectionContext &ctx)
{
	if (g_insane->integer || g_chaotic->integer)
	{
		const float difficultyScale = ctx.currentActualLevel <= 15 ? 1.2f : 1.4f;
		weight *= difficultyScale;
		if (HasWaveType(monster.types, MonsterWaveType::Elite))
		{
			weight *= 1.25f;
		}
	}
	weight *= ctx.flyingAdjustmentFactor;
	if (ctx.effectiveLevel > ctx.currentActualLevel && monster.minWave > ctx.currentActualLevel)
	{
		if (monster.minWave - ctx.currentActualLevel > 5)
		{
			weight *= 0.6f;
		}
	}
	if (ctx.isRetaliationActive)
	{
		constexpr MonsterWaveType RETALIATION_FOCUS_TYPES =
			MonsterWaveType::Spawner | MonsterWaveType::Bomber | MonsterWaveType::Special;
		if (HasWaveType(GetMonsterWaveTypes(monster.typeId), RETALIATION_FOCUS_TYPES))
		{
			weight *= 1.8f;
		}
		else
		{
			weight *= 0.7f;
		}
	}
	return weight;
}

static bool IsMonsterCompatible(const MonsterTypeInfo &monster, const MonsterSelectionContext &ctx)
{
	if (monster.minWave > ctx.effectiveLevel) return false;
	if (ctx.waveTypeForFiltering != MonsterWaveType::None && !IsValidMonsterForWave(monster.typeId, ctx.waveTypeForFiltering)) return false;
	const bool monster_is_flying = IsFlying(monster.typeId);
	if (ctx.isSpawnPointFlying && !monster_is_flying) return false;
	return true;
}

// REPLACEMENT: BuildMonsterCache (with bug fix)
static void BuildMonsterCache(MonsterCache &cache_ref, const MonsterSelectionContext &ctx)
{
	cache_ref.clear();

	// --- CRITICAL FIX ---
	// We must iterate the MASTER list (`monsterTypes`) to find monsters that
	// are unlocked by the new `effectiveLevel`. Iterating the pre-filtered
	// `g_eligible_monsters_for_wave` list will never find them.
	for (const auto& monster : monsterTypes) // CORRECT: Iterate the full list
	{
		// We now perform all compatibility checks here.
		if (IsMonsterCompatible(monster, ctx)) 
		{
			float weight = CalculateBaseWeight(monster, ctx);
			weight = ApplySpecialModifiers(weight, monster, ctx);
			cache_ref.addMonster(monster.typeId, weight);
		}
	}
}

static horde::MonsterTypeID SelectFromCache(const MonsterCache &cache_ref)
{
	if (cache_ref.count == 0 || cache_ref.total_weight <= 0.0f) return horde::MonsterTypeID::UNKNOWN;
	const float random_value = frandom() * cache_ref.total_weight;
	size_t left = 0, right = cache_ref.count - 1;
	while (left < right)
	{
		const size_t mid = left + (right - left) / 2;
		if (cache_ref.entries[mid].cumulative_weight < random_value) left = mid + 1;
		else right = mid;
	}
	return cache_ref.entries[left].typeId;
}

static horde::MonsterTypeID EmergencyFallbackSelection(const MonsterSelectionContext &ctx)
{
    if (developer->integer) gi.Com_PrintFmt("G_HordePickMonsterType: Fallback (Lvl: {}, FlyPoint: {})...\n", ctx.currentActualLevel, ctx.isSpawnPointFlying);
    for (const auto &monster : monsterTypes)
    {
        if (monster.minWave <= ctx.currentActualLevel)
        {
            const bool isFlyingMonster = HasWaveType(GetMonsterWaveTypes(monster.typeId), MonsterWaveType::Flying);
            if (!(ctx.isSpawnPointFlying && !isFlyingMonster)) return monster.typeId;
        }
    }
    return horde::MonsterTypeID::UNKNOWN;
}

//-----------------------------------------------------
// G_HordePickMonsterType - Main Orchestrator
//-----------------------------------------------------
static horde::MonsterTypeID G_HordePickMonsterType(
	edict_t *spawn_point,
	int32_t currentActualLevel_param,
	MonsterWaveType currentActualWaveType_param,
	bool isRetaliationActive_param,
	bool isRecoveryModeActive_param,
	MonsterWaveType originalWaveTypeBeforeRecovery_param)
{
	if (!spawn_point || !spawn_point->inuse) return horde::MonsterTypeID::UNKNOWN;

	MonsterSelectionContext ctx;
	ctx.currentActualLevel = currentActualLevel_param;
	ctx.currentActualWaveType = currentActualWaveType_param;
	ctx.isSpawnPointFlying = (spawn_point->style == 1);
	ctx.isRetaliationActive = isRetaliationActive_param;
	ctx.isRecoveryModeActive = isRecoveryModeActive_param;
	ctx.isBossWaveMinionPhase = (ctx.currentActualLevel >= 10 && ctx.currentActualLevel % 5 == 0 && boss_spawned_for_wave);
	ctx.flyingAdjustmentFactor = adjustFlyingSpawnProbability(g_cached_flying_spawn_count);
	ctx.waveTypeForFiltering = isRecoveryModeActive_param ? (HasWaveType(originalWaveTypeBeforeRecovery_param, MonsterWaveType::Flying) ? MonsterWaveType::Flying : MonsterWaveType::Ground) : currentActualWaveType_param;

	// Calculate the effective level using our newly modified function
	bool attemptHigherLevel = ShouldAttemptHigherLevelSpawn(ctx.currentActualLevel, ctx.isRetaliationActive, ctx.isRecoveryModeActive);
	ctx.effectiveLevel = CalculateEffectiveMonsterLevel(ctx.currentActualLevel, attemptHigherLevel, ctx.waveTypeForFiltering);

	BuildMonsterCache(g_monster_picker_internal_cache, ctx);
	horde::MonsterTypeID chosen_monster_id = SelectFromCache(g_monster_picker_internal_cache);

	if (chosen_monster_id == horde::MonsterTypeID::UNKNOWN)
	{
		chosen_monster_id = EmergencyFallbackSelection(ctx);
	}

	// Set the "elite spawned" flag if we successfully picked a higher-level monster
	if (chosen_monster_id != horde::MonsterTypeID::UNKNOWN && ctx.effectiveLevel > ctx.currentActualLevel)
	{
		const MonsterTypeInfo* info = g_monster_info_lut[static_cast<size_t>(chosen_monster_id)];
		if (info && info->minWave > ctx.currentActualLevel)
		{
			g_special_high_level_monster_spawned_this_wave = true;
			if (developer->integer)
			{
				gi.Com_PrintFmt("ELITE SPAWN: '{}' (minWave {}) spawned in wave {}. Flag set.\n",
								horde::MonsterTypeRegistry::GetClassname(chosen_monster_id),
								info->minWave,
								ctx.currentActualLevel);
			}
		}
	}

	return chosen_monster_id;
}

// =======================================================================
// END: COMPLETE MONSTER PICKING SYSTEM
// =======================================================================

void Horde_PreInit()
{
	gi.Com_Print("Horde mode must be DM. Set <deathmatch 1> and <horde 1>, then <map mapname>.\n");
	gi.Com_Print("COOP requires <coop 1> and <horde 0>.\n");

	g_horde = gi.cvar("horde", "0", CVAR_LATCH);
	// gi.Com_Print("After starting a normal server type: starthorde to start a game.\n");

	if (!g_horde->integer)
	{ // If horde mode is OFF (0)
		// Ensure g_hardcoop is registered if it isn't already (safe check)
		// Assuming g_hardcoop is already declared in g_local.h and registered in InitGame
		// We just need to set its value here.
		gi.cvar_set("g_hardcoop", "1");		 // Force hardcoop ON
		gi.cvar_forceset("deathmatch", "0"); // Keep this line
		return;								 // Exit Horde_PreInit early if horde mode is off
	}

	// Configuración automática cuando horde está activo
	if (g_horde->integer)
	{
		// deathmatch->integer == 1;
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

void VerifyAndAdjustBots()
{
	if (developer->integer == 2)
	{
		gi.cvar_set("bot_minClients", "-1");
	}
	else
	{
		const horde::MapSize mapSize = GetMapSize(static_cast<const char *>(level.mapname));
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
static void PrecacheAllGameItems() noexcept
{
	// Only precache once
	if (items_precached)
		return;

	if (developer->integer)
	{
		gi.Com_Print("Precaching all game items...\n");
	}

	// Iterate directly through the global itemlist using item_id_t
	// Start from IT_NULL + 1 because index 0 is unused.
	for (item_id_t i = static_cast<item_id_t>(IT_NULL + 1); i < IT_TOTAL; i = static_cast<item_id_t>(i + 1))
	{
		// Get the gitem_t pointer directly using the index (ID)
		gitem_t *item = &itemlist[i]; // Or use GetItemByIndex(i) if preferred

		// Basic validation: Skip if item pointer is null (shouldn't happen)
		// or if it lacks a classname (often indicates an internal/placeholder item)
		if (!item || !item->classname)
		{
			continue;
		}

		// Call the existing PrecacheItem function, which handles all necessary assets
		// associated with this gitem_t (models, sounds, icons, nested precaches).
		PrecacheItem(item);
	}

	items_precached = true; // Mark as done

	if (developer->integer)
	{
		gi.Com_Print("Item precaching complete.\n");
	}
}

// Función para precarga de sonidos
static bool sounds_precached = false;

static void PrecacheWaveSounds() noexcept
{
	if (sounds_precached)
		return;

	static const std::array<std::pair<cached_soundindex *, const char *>, 7> individual_sounds = { {
        {&sound_tele3, "misc/r_tele3.wav"},
        {&sound_tele_up, "misc/tele_up.wav"},
        {&sound_spawn1, "misc/spawn1.wav"},
        {&incoming, "world/incoming.wav"},
		{&talk, "misc/talk.wav"},
		{&tele1, "misc/tele1.wav"},
        {&sound_quake, "world/quake.wav"} } };


	// Use std::span for safe iteration
	std::span individual_view{individual_sounds};
	for (const auto &[sound_index, path] : individual_view)
	{
		sound_index->assign(path);
	}

	std::span wave_view{WAVE_SOUND_PATHS};
	for (size_t i = 0; i < NUM_WAVE_SOUNDS; ++i)
	{
		wave_sounds[i].assign(wave_view[i]);
	}

	std::span start_view{START_SOUND_PATHS};
	for (size_t i = 0; i < NUM_START_SOUNDS; ++i)
	{
		start_sounds[i].assign(start_view[i]);
	}

	sounds_precached = true;
}

// Agregar un nuevo array para tracking
static std::array<bool, NUM_WAVE_SOUNDS> used_wave_sounds = {};
static size_t remaining_wave_sounds = NUM_WAVE_SOUNDS;

static int GetRandomWaveSound()
{
	// Si todos los sonidos han sido usados, resetear
	if (remaining_wave_sounds == 0)
	{
		std::fill(used_wave_sounds.begin(), used_wave_sounds.end(), false);
		remaining_wave_sounds = NUM_WAVE_SOUNDS;
	}

	// Seleccionar un sonido no usado
	while (true)
	{
		size_t const index = irandom(NUM_WAVE_SOUNDS);
		if (!used_wave_sounds[index])
		{
			used_wave_sounds[index] = true;
			remaining_wave_sounds--;
			return wave_sounds[index];
		}
	}
}

static std::array<bool, NUM_START_SOUNDS> used_start_sounds = {};
static size_t remaining_start_sounds = NUM_START_SOUNDS;

static void PlayWaveStartSound()
{
	// Si todos los sonidos han sido usados, resetear
	if (remaining_start_sounds == 0)
	{
		std::fill(used_start_sounds.begin(), used_start_sounds.end(), false);
		remaining_start_sounds = NUM_START_SOUNDS;
	}

	// Seleccionar un sonido no usado
	while (true)
	{
		size_t const index = irandom(NUM_START_SOUNDS);
		if (!used_start_sounds[index])
		{
			used_start_sounds[index] = true;
			remaining_start_sounds--;
			gi.sound(world, CHAN_VOICE, start_sounds[index], 1, ATTN_NONE, 0);
			break;
		}
	}
}
// Capping resets on map end

static bool hasBeenReset = false;
void AllowReset() noexcept
{
	hasBeenReset = false;
}

void ResetBosses()
{
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end(); /* no increment here */)
	{
		edict_t *boss = *it;
		// CRITICAL CHECK: Ensure the pointer is not null AND the entity is still in use
		// before attempting to access or free it.
		if (boss && boss->inuse)
		{
			// Ensure boss death logic is finalized if needed, then free
			if (!boss->monsterinfo.BOSS_DEATH_HANDLED)
			{
				// Optional: Add a check here if boss->die is valid before calling
				// if (boss->die.pointer()) { boss->die(boss, boss, boss, 0, boss->s.origin, mod_t{}); }
				// Or just call the handler which should be safe
				BossDeathHandler(boss); // Attempt final cleanup if not done
			}
			OnEntityRemoved(boss); // Call removal hook
			G_FreeEdict(boss);	   // Free the entity
		}
		// Erase the pointer from the set regardless of whether it was valid/freed,
		// then advance the iterator safely.
		it = auto_spawned_bosses.erase(it);
	}
}

// ADDED: Function to initialize Monster Info LUT
void InitializeMonsterInfoLUT()
{
	if (g_monster_info_lut_initialized)
		return;
	g_monster_info_lut.fill(nullptr);
	for (const auto &monster_entry : monsterTypes)
	{ // Iterate over the actual monsterTypes array
		if (monster_entry.typeId != horde::MonsterTypeID::UNKNOWN &&
			static_cast<size_t>(monster_entry.typeId) < g_monster_info_lut.size())
		{
			g_monster_info_lut[static_cast<size_t>(monster_entry.typeId)] = &monster_entry;
		}
	}
	g_monster_info_lut_initialized = true;
	if (developer->integer)
		gi.Com_PrintFmt("Monster Info LUT Initialized.\n");
}

// --- MODIFIED ---
// This function now ONLY precaches monsters for Wave 1 for a very fast initial map load.
// Subsequent waves are handled by the JIT precacher in Horde_InitLevel.
void PrecacheAllMonsters() noexcept
{
    // Only run this initial precache once per map load.
    if (monsters_precached) {
        return;
    }
    g_precached_monster_types.clear(); // Ensure our tracking set is empty.

    if (developer->integer) {
        gi.Com_Print("INITIAL PRECACHE: Loading all monsters for Wave 1...\n");
    }

    // The monsterTypes array is sorted by minWave, so we can iterate efficiently.
    for (const auto& monster_info : monsterTypes)
    {
        // Since the array is sorted, we can stop as soon as we're past wave 1.
        if (monster_info.minWave > 1) {
            break; 
        }

        // This monster is for Wave 1, so precache it.
        const char* classname = horde::MonsterTypeRegistry::GetClassname(monster_info.typeId);
        if (classname && *classname)
        {
            edict_t* temp_monster = G_Spawn();
            if (temp_monster)
            {
                temp_monster->classname = classname;
                // *** CRITICAL FIX: Add this flag to prevent precaching from affecting monster counts. ***
                temp_monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
                
                ED_CallSpawn(temp_monster);

                if (temp_monster->inuse) {
                    G_FreeEdict(temp_monster);
                }
                // Mark this monster type as loaded.
                g_precached_monster_types.insert(monster_info.typeId);
            }
        }
    }
    
    // Mark the initial precache as done for this map.
    monsters_precached = true;
}

void Horde_Init()
{
	InitializeMonsterMoveSets(); //jump animations 
	horde::InitializeHordeIDs();
	sounds_precached = false;
	items_precached = false;
	ResetBosses();

	PrecacheAllGameItems();
	PrecacheWaveSounds();
	monsters_precached = false; // Reset the precache flag for the new map.
	PrecacheAllMonsters();

	InitializeMonsterWaveTypes();
	InitializeMonsterInfoLUT(); // ADDED: Initialize LUT here
	InitializeWaveSystem();
	last_wave_number = 0;

	AllowReset();
	ResetGame(); // This will call RebuildSpawnPointCacheIfNeeded indirectly or directly

	gi.Com_Print("PRINT: Horde game state initialized with all necessary resources precached.\n");
}

// Helper function to select a suitable boss weapon drop based on wave level
static item_id_t SelectBossWeaponDrop(int32_t wave_level)
{
	// Define potential weapon drops with their minimum required wave level
	static const std::array<std::pair<item_id_t, int32_t>, 8> boss_weapon_drops = {{
		{IT_WEAPON_HYPERBLASTER, 9},
		{IT_WEAPON_RLAUNCHER, 9},
		{IT_WEAPON_PHALANX, 9},	   // Xatrix
		{IT_WEAPON_IONRIPPER, 9},  // Xatrix (originally boomer)
		{IT_WEAPON_PLASMABEAM, 9}, // Rogue
		{IT_WEAPON_RAILGUN, 9},	   // Moved slightly later
		{IT_WEAPON_DISRUPTOR, 19}, // Rogue (originally disintegrator)
		{IT_WEAPON_BFG, 19}		   // Moved slightly later
	}};

	// Collect weapons eligible for the current wave level
	std::vector<item_id_t> eligible_weapons;
	eligible_weapons.reserve(boss_weapon_drops.size()); // Pre-allocate memory

	for (const auto &[weapon_id, min_level] : boss_weapon_drops)
	{
		if (wave_level >= min_level)
		{
			eligible_weapons.push_back(weapon_id);
		}
	}

	// If no weapons are eligible, return IT_NULL
	if (eligible_weapons.empty())
	{
		return IT_NULL;
	}

	// Select a random weapon from the eligible list
	// Use a more robust random number generator if possible, otherwise fallback
	// std::uniform_int_distribution<size_t> dist(0, eligible_weapons.size() - 1);
	// size_t random_index = dist(mt_rand); // Requires <random> and mt_rand setup
	size_t random_index = irandom(eligible_weapons.size());
	// Safety check (shouldn't be needed with correct random range)
	if (random_index >= eligible_weapons.size())
	{
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

void BossDeathHandler(edict_t *boss)
{
	// Fast early-out with combined validation (no change needed)
	if (!g_horde || !g_horde->integer || !boss || !boss->inuse || !boss->monsterinfo.IS_BOSS ||
		boss->monsterinfo.BOSS_DEATH_HANDLED || boss->health > 0)
	{
		return;
	}

	// Mark as handled immediately (no change needed)
	boss->monsterinfo.BOSS_DEATH_HANDLED = true;

	//boss_spawned_for_wave = false;
	// if (developer->integer)
	// { // Optional debug print
	// 	gi.Com_PrintFmt("BossDeathHandler: Reset boss_spawned_for_wave flag for wave {}.\n", g_horde_local.level);
	// }

	// Clean up entity tracking (no change needed)
	OnEntityDeath(boss);
	OnEntityRemoved(boss);
	auto_spawned_bosses.erase(boss); // Ensure this set is accessible

	// --- Use item_id_t for static drop tables ---
	static const std::array<item_id_t, 8> standardItemIDs = {
		IT_ITEM_ADRENALINE, IT_ITEM_PACK, IT_ITEM_SENTRYGUN,
		IT_ITEM_SPHERE_DEFENDER, IT_ARMOR_COMBAT, IT_ITEM_BANDOLIER,
		IT_ITEM_INVULNERABILITY, IT_AMMO_NUKE};
	// Note: Ensure the size `8` matches the number of items listed.

	// --- Drop primary weapon using item_id_t ---
	item_id_t weapon_id = SelectBossWeaponDrop(current_wave_level);
	if (weapon_id != IT_NULL)
	{ // Check against IT_NULL
		if (gitem_t *weapon_item = GetItemByIndex(weapon_id))
		{ // Use GetItemByIndex
			if (edict_t *weapon = Drop_Item(boss, weapon_item))
			{
				// Set up weapon visuals/physics (logic remains the same)
				weapon->s.origin = boss->s.origin;
				weapon->velocity = {
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VERTICAL_VELOCITY, MAX_VERTICAL_VELOCITY))};
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
	if (gitem_t *special_item = GetItemByIndex(special_id))
	{ // Use GetItemByIndex
		if (edict_t *powerup = Drop_Item(boss, special_item))
		{
			// Set up powerup visuals/physics (logic remains the same)
			powerup->s.origin = boss->s.origin;
			powerup->velocity = {
				static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
				static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
				static_cast<float>(irandom(300, 400))};
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

	// Shuffle using std::shuffle and your mt_rand engine
	std::shuffle(shuffledIDs.begin(), shuffledIDs.end(), mt_rand);

	// Now proceed to drop items from the shuffledIDs
	for (item_id_t item_id : shuffledIDs)
	{
		if (item_id == IT_NULL)
			continue; // Safety check

		if (gitem_t *item = GetItemByIndex(item_id))
		{ // Use GetItemByIndex
			if (edict_t *drop = Drop_Item(boss, item))
			{
				// Set up standard drop visuals/physics (logic remains the same)
				drop->s.origin = boss->s.origin;
				drop->velocity = {
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VERTICAL_VELOCITY, MAX_VERTICAL_VELOCITY))};
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

void boss_die(edict_t *boss)
{
	if (!boss || !boss->inuse || !g_horde->integer ||
		!boss->monsterinfo.IS_BOSS || boss->health > 0 ||
		boss->monsterinfo.BOSS_DEATH_HANDLED ||
		auto_spawned_bosses.find(boss) == auto_spawned_bosses.end())
	{
		return;
	}

	BossDeathHandler(boss);

	// Limpiar la barra de salud
	for (size_t i = 0; i < MAX_HEALTH_BARS; ++i)
	{
		if (level.health_bar_entities[i] && level.health_bar_entities[i]->enemy == boss)
		{
			G_FreeEdict(level.health_bar_entities[i]);
			level.health_bar_entities[i] = nullptr;
			break;
		}
	}

	// Limpiar el configstring del nombre de la barra de salud
	gi.configstring(CONFIG_HEALTH_BAR_NAME, "");
}

static bool Horde_AllMonstersDead()
{
	// Fast path using level counters
	if (level.total_monsters == level.killed_monsters)
	{
		return true;
	}

	// Secondary verification by direct entity check
	// Using active_or_dead_monsters() for more complete coverage
	for (auto ent : active_or_dead_monsters())
	{
		// Skip invalid entities and non-counting monsters
		if (!ent || !ent->inuse || (ent->monsterinfo.aiflags & AI_DO_NOT_COUNT))
			continue;

		// If any live monster found, return false immediately
		if (!ent->deadflag && ent->health > 0)
		{
			return false;
		}

		// Handle dying bosses
		if (ent->monsterinfo.IS_BOSS && ent->health <= 0)
		{
			if (auto_spawned_bosses.find(ent) != auto_spawned_bosses.end() &&
				!ent->monsterinfo.BOSS_DEATH_HANDLED)
			{
				boss_die(ent);
			}
		}
	}

	// If we get here, no live monsters found
	return true;
}

// Asegúrate de limpiar entidades muertas
void Horde_CleanBodies()
{
	for (edict_t *ent : active_or_dead_monsters())
	{
		if (ent->deadflag || ent->health <= 0)
		{
			if (!ent->is_fading_out)
			{ // Only check once before starting fade
				StartFadeOut(ent);
			}
		}
		else
		{									  // If the monster is alive but somehow flagged for removal:
			CheckAndRestoreMonsterAlpha(ent); // Restore alpha for live monsters
		}
	}
}

// attaching healthbar
static void AttachHealthBar(edict_t *boss)
{
	auto healthbar = G_Spawn();
	if (!healthbar)
		return;

	healthbar->classname = "target_healthbar";
	// Usar asignación directa y operador de suma de vec3_t
	healthbar->s.origin = boss->s.origin + vec3_t{0, 0, 20};

	healthbar->delay = 2.0f;
	healthbar->target = boss->targetname;

	// Copiar el nombre del jefe correctamente
	std::string boss_name = GetDisplayName(boss);
	healthbar->message = G_CopyString(boss_name.c_str(), TAG_LEVEL);

	SP_target_healthbar(healthbar);
	healthbar->enemy = boss;

	// Llamar a SetHealthBarName después de configurar el mensaje
	SetHealthBarName(boss);

	for (size_t i = 0; i < MAX_HEALTH_BARS; ++i)
	{
		if (!level.health_bar_entities[i])
		{
			level.health_bar_entities[i] = healthbar;
			break;
		}
	}

	healthbar->think = check_target_healthbar;
	healthbar->nextthink = level.time + 20_sec;
}

void BossSpawnThink(edict_t *self); // Forward declaration of the think function
void SP_target_orb(edict_t *ent);

// Define a struct for boss wave info
struct BossWaveInfo
{
	MonsterWaveType waveType;
	const char *message;
};

// Create an array for boss wave types
static std::array<BossWaveInfo, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_bossWaveTypeArray;
static bool g_bossWaveTypesInitialized = false;

// Initialize function
void InitializeBossWaveTypes()
{
	if (g_bossWaveTypesInitialized)
		return;

	// Fill with default values first
	// Removed leading newlines, AppendHordeMessage will add one.
	for (auto &entry : g_bossWaveTypeArray)
	{
		entry = {MonsterWaveType::Medium, "A new wave of Strogg approaches!"};
	}

	// Set specific entries with more descriptive and varied messages
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::REDMUTANT)] =
		{MonsterWaveType::Mutant, "The Bloody Mutant hungers for flesh!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::SHAMBLER_KL)] =
		{MonsterWaveType::Shambler, "The Shambler's thunderous steps approach!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::WIDOW)] =
		{MonsterWaveType::Small, "Widow's disruptor beams seek new targets!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::WIDOW2)] =
		{MonsterWaveType::Small, "The Widow weaves a web of destruction!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::PSX_ARACHNID)] =
		{MonsterWaveType::Arachnophobic, "Arachnid's venomous brood swarms!"};

	// Flying Bosses - more varied messages
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2)] =
		{MonsterWaveType::Flying | MonsterWaveType::Boss, "Hornet's deadly sting descends from above!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER)] =
		{MonsterWaveType::Flying | MonsterWaveType::Boss, "The Carrier unleashes its aerial swarm!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER_MINI)] =
		{MonsterWaveType::Flying | MonsterWaveType::Boss, "Carrier Mini is delivering pain right to your face!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2_KL)] =
		{MonsterWaveType::Flying | MonsterWaveType::Boss, "The Hornet buzzes with destructive intent!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::FIXBOT_KL)] =
		{MonsterWaveType::Flying | MonsterWaveType::Boss, "The Fixer arrives to repair your demise!"};

	// Heavy/Medium Armored Bosses - more varied messages
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::TANK_64)] =
		{MonsterWaveType::Medium, "Tank Commander rolls in, prepare for impact!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::PSX_GUARDIAN)] =
		{MonsterWaveType::Medium, "The Guardian unleashes a barrage of rockets!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS5)] =
		{MonsterWaveType::Medium, "Supertank: The ultimate Strogg war machine!"};

	// Close Combat / Ranged Heavy Bosses - more varied messages
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::GUNCMDR_KL)] =
		{MonsterWaveType::Medium, "Gunner Commander has you in his sights!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::MAKRON)] =
		{MonsterWaveType::Medium, "Makron demands your surrender!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::MAKRON_KL)] =
		{MonsterWaveType::Medium, "Makron: The Strogg's true leader has arrived!"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::JORG)] =
		{MonsterWaveType::Medium, "Jorg's upgraded mech is a force of destruction!"};


	g_bossWaveTypesInitialized = true;
}

// Helper function
inline std::pair<MonsterWaveType, const char *> GetBossWaveType(horde::MonsterTypeID typeId)
{
	if (!g_bossWaveTypesInitialized)
		InitializeBossWaveTypes();

	size_t index = static_cast<size_t>(typeId);
	if (index < g_bossWaveTypeArray.size())
		return {g_bossWaveTypeArray[index].waveType, g_bossWaveTypeArray[index].message};
	return {MonsterWaveType::Medium, "\n\n\nDefault wave incoming!\n"};
}

// --- REVISED CheckAndTeleportBoss ---
bool CheckAndTeleportBoss(edict_t *self, BossTeleportReason reason = BossTeleportReason::DROWNING)
{
	PROFILE_SCOPE("CheckAndTeleportBoss");
	if (level.intermissiontime)
	{
		return false;
	}

	if (!self)
	{
		return false;
	}
	if (!self->inuse || self->deadflag || !self->monsterinfo.IS_BOSS || !g_horde || !g_horde->integer)
	{
		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("CTB: Early exit for {}. InUse:{} Dead:{} IsBoss:{} HordeInt:{}\n",
							self->classname ? self->classname : "NO_CLASSNAME",
							self->inuse, self->deadflag, self->monsterinfo.IS_BOSS,
							g_horde ? g_horde->integer : -1);
		}
		return false;
	}

	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
	if (typeId == horde::MonsterTypeID::MISC_INSANE || typeId == horde::MonsterTypeID::TURRET)
	{
		return false;
	}

	static std::string last_map_name_boss_teleport;
	static horde::MapID cached_map_id_boss_teleport = horde::MapID::UNKNOWN;
	const char *current_map = GetCurrentMapName();

	if (last_map_name_boss_teleport != current_map)
	{
		last_map_name_boss_teleport = current_map;
		cached_map_id_boss_teleport = horde::MapOriginRegistry::GetMapID(current_map);
	}
	if (cached_map_id_boss_teleport == horde::MapID::UNKNOWN)
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("CTB: MapID unknown for {}, cannot get teleport origin.\n", current_map);
		return false;
	}

	vec3_t destination_origin;
	if (!horde::MapOriginRegistry::GetOrigin(cached_map_id_boss_teleport, destination_origin))
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("CTB: Failed to get MapOriginRegistry origin for map_id {}.\n", (int)cached_map_id_boss_teleport);
		return false;
	}
	if (!is_valid_vector(destination_origin) || destination_origin == vec3_origin)
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("CTB: Invalid destination_origin ({},{},{}) from MapOriginRegistry.\n", destination_origin.x, destination_origin.y, destination_origin.z);
		return false;
	}
	vec3_t destination_angles = self->s.angles;
	destination_angles.x = 0;
	destination_angles.z = 0;
	constexpr gtime_t TRIGGER_HURT_RETRIGGER_COOLDOWN = 0.1_sec;
	constexpr gtime_t DROWNING_COOLDOWN_BOSS = 1_sec;
	const gtime_t selected_trigger_cooldown = (reason == BossTeleportReason::DROWNING) ? DROWNING_COOLDOWN_BOSS : TRIGGER_HURT_RETRIGGER_COOLDOWN;

	static gtime_t last_boss_teleport_attempt_time[MAX_EDICTS] = {};
	const int boss_edict_num = self - g_edicts;

	if (level.time < last_boss_teleport_attempt_time[boss_edict_num] + selected_trigger_cooldown)
	{
		if (developer->integer > 1)
		{
			gtime_t cooldown_remaining = (last_boss_teleport_attempt_time[boss_edict_num] + selected_trigger_cooldown) - level.time;
			gi.Com_PrintFmt("CTB: Boss {} on REASON-specific cooldown. Remaining: {:.2f}s\n",
							self->classname ? self->classname : "UNKNOWN",
							cooldown_remaining.seconds());
		}
		return false;
	}
	if (self->teleport_time > level.time)
	{
		if (developer->integer > 1)
		{
			gtime_t cooldown_remaining = self->teleport_time - level.time;
			gi.Com_PrintFmt("CTB: Boss {} on general monster teleport cooldown. Remaining: {:.2f}s\n",
							self->classname ? self->classname : "UNKNOWN",
							cooldown_remaining.seconds());
		}
		return false;
	}

	last_boss_teleport_attempt_time[boss_edict_num] = level.time;

	bool force_teleport = (reason == BossTeleportReason::TRIGGER_HURT || reason == BossTeleportReason::DROWNING);

	if (developer->integer > 1)
	{
		gi.Com_PrintFmt("CTB: Attempting Horde_TeleportMonster for boss {} to ({},{},{}) (ForceVisible: {})\n",
						self->classname ? self->classname : "UNKNOWN",
						destination_origin.x, destination_origin.y, destination_origin.z,
						force_teleport);
	}

	if (!Horde_TeleportMonster(self, destination_origin, destination_angles, false, force_teleport))
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("CTB: Horde_TeleportMonster returned false for boss {}.\n", self->classname ? self->classname : "UNKNOWN");
		return false;
	}

	std::string boss_display_name = GetDisplayName(self);

	switch (reason)
	{
	case BossTeleportReason::DROWNING:
		if (sound_tele3)
			gi.sound(self, CHAN_AUTO, sound_tele3, 1, ATTN_NORM, 0);
		gi.LocBroadcast_Print(PRINT_HIGH, "{} emerges from the depths!\n", boss_display_name.c_str());
		break;
	case BossTeleportReason::TRIGGER_HURT:
		if (sound_tele_up)
			gi.sound(self, CHAN_AUTO, sound_tele_up, 1, ATTN_NORM, 0);
		gi.LocBroadcast_Print(PRINT_HIGH, "{} escapes certain death!\n", boss_display_name.c_str());
		break;
	}

	PushEntitiesAway(self->s.origin, 3, 600.0f, 1200.0f, 4000.0f, 1800.0f);

	// MODIFIED GUARD for SpawnGrow_Spawn
	if (self->inuse && !self->deadflag && self->health > 0)
	{
		SpawnGrow_Spawn(self->s.origin, 100.0f, 15.0f);
	}
	else if (self->inuse && developer->integer > 1)
	{
		gi.Com_PrintFmt("SpawnGrow_Spawn (boss teleport effect) skipped: Boss {} (idx {}) not fully alive. DeadFlag:{}, Health:%.0f\n",
						(self->classname ? self->classname : "Unknown"),
						(int)(self - g_edicts),
						self->deadflag,
						self->health);
	}

	if (self->health < (self->max_health >> 2))
	{
		self->health = (self->max_health >> 2);
	}

	MarkPositionAsRecentlyTeleported(destination_origin);

	if (developer->integer)
	{
		const char *reason_str = reason == BossTeleportReason::DROWNING ? "drowning" : "trigger_hurt";
		gi.Com_PrintFmt("CTB: Boss {} successfully teleported due to {} to ({},{},{}).\n",
						self->classname ? self->classname : "UNKNOWN",
						reason_str,
						self->s.origin.x, self->s.origin.y, self->s.origin.z);
	}

	return true;
}

void SetHealthBarName(const edict_t *boss)
{
	// Use a static buffer to avoid allocation
	static char buffer[MAX_STRING_CHARS];

	// Early validation
	if (!boss || !boss->inuse)
	{
		gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, "");
		return;
	}

	// Get name once
	const std::string display_name = GetDisplayName(boss);
	if (display_name.empty())
	{
		gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, "");
		return;
	}

	// Calculate safe buffer size once
	const size_t name_len = std::min(display_name.length(),
									 MAX_STRING_CHARS - 1); // -1 for null terminator

	// Copy directly to buffer
	memcpy(buffer, display_name.c_str(), name_len);
	buffer[name_len] = '\0'; // Ensure null termination

	// Set the configstring once
	gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, buffer);
}

// Implementación de UpdateHordeMessage
void UpdateHordeMessage(std::string_view message, gtime_t duration = 5_sec)
{
	// Early validation for empty messages
	if (message.empty() || duration <= 0_ms)
	{
		ClearHordeMessage();
		return;
	}

	// Get current message once
	const char *current_msg = gi.get_configstring(CONFIG_HORDEMSG);

	// Only update if changed
	if (!current_msg || strcmp(current_msg, message.data()) != 0)
	{
		gi.configstring(CONFIG_HORDEMSG, message.data());
	}

	// Set duration
	horde_message_end_time = level.time + duration;
}

static void SpawnBossAutomatically()
{
	// --- 1. Cleanup Existing Bosses ---
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end(); /* no increment */)
	{
		edict_t *existing_boss = *it;
		if (existing_boss && existing_boss->inuse)
		{
			if (!existing_boss->monsterinfo.BOSS_DEATH_HANDLED)
			{
				BossDeathHandler(existing_boss);
			}
			OnEntityRemoved(existing_boss);
			G_FreeEdict(existing_boss);
		}
		it = auto_spawned_bosses.erase(it);
	}
	boss_spawned_for_wave = false; // Reset flag

	// --- 2. Basic Wave Check ---
	if (g_horde_local.level < 10 || g_horde_local.level % 5 != 0)
	{
		return; // Not a boss wave
	}

	// --- 3. Select Boss Type ---
	const char *map_name = GetCurrentMapName();
	BossPickResult boss_pick_result = G_HordePickBOSSType(
		g_horde_local.current_map_size, map_name, g_horde_local.level);

	horde::MonsterTypeID boss_type = boss_pick_result.typeId;
	BossSizeCategory selected_boss_size = boss_pick_result.sizeCategory;

	if (boss_type == horde::MonsterTypeID::UNKNOWN)
	{
		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: Failed to pick a boss type for wave {}.\n", g_horde_local.level);
		return;
	}
	const char *boss_classname = horde::MonsterTypeRegistry::GetClassname(boss_type);
	if (!boss_classname)
	{
		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: Failed to get classname for boss type ID {}.\n", (int)boss_type);
		return;
	}

	// --- 4. Determine Spawn Location (Fixed Origin or Random Fallback) ---
	vec3_t spawn_origin = vec3_origin;
	vec3_t spawn_angles = vec3_origin;
	bool location_found = false;
	edict_t *selected_point = nullptr;

	// Get boss properties needed for validation
	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(boss_type, predicted_mins, predicted_maxs);
	bool boss_is_flying = IsFlying(boss_type);

	// --- Pass 1: Try getting a fixed origin from the map registry ---
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(map_name);
	vec3_t fixed_origin;
	if (mapId != horde::MapID::UNKNOWN && horde::MapOriginRegistry::GetOrigin(mapId, fixed_origin))
	{
		if (is_valid_vector(fixed_origin) && fixed_origin != vec3_origin)
		{
			vec3_t validated_fixed_origin = fixed_origin;
			// Call with the new flag set to TRUE, allowing mid-air spawns for predefined locations.
			if (IsPositionPhysicallyValid(validated_fixed_origin, predicted_mins, predicted_maxs, boss_is_flying, true))
			{
				spawn_origin = validated_fixed_origin;
				location_found = true;
				if (developer->integer > 1)
					gi.Com_PrintFmt("SpawnBossAutomatically: Using validated predefined map origin {} for boss {}.\n", spawn_origin, boss_classname);
			}
			else
			{
				if (developer->integer)
					gi.Com_PrintFmt("SpawnBossAutomatically: Predefined map origin {} failed validation (occupied). Falling back to random.\n", fixed_origin, boss_classname);
			}
		}
	}
	else
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("SpawnBossAutomatically: No predefined map origin found for {}. Using random spawn point fallback.\n", map_name);
	}

	// --- Pass 2: Fallback to random spawn point selection if fixed origin wasn't found or wasn't valid ---
	if (!location_found)
	{
		constexpr float MIN_BOSS_PLAYER_DIST_SQ = 250.0f * 250.0f;
		auto BossSpawnFilter = [&](edict_t *spawnPoint) -> bool
		{
			if (!spawnPoint || !spawnPoint->inuse)
				return false;
			const auto &data = spawnPointsData[spawnPoint];
			if ((data.isTemporarilyDisabled && level.time < data.cooldownEndsAt) ||
				level.time < data.teleport_cooldown ||
				level.time < data.alternative_cooldown)
				return false;
			bool is_flying_point = (spawnPoint->style == 1);
			if (boss_is_flying != is_flying_point) // Simplified check: must match flying status
				return false;
			for (const auto *const player : active_players_no_spect())
			{
				if ((spawnPoint->s.origin - player->s.origin).lengthSquared() < MIN_BOSS_PLAYER_DIST_SQ)
					return false;
			}
			if (IsSpawnPointOccupied(spawnPoint))
				return false;
			return true;
		};

		selected_point = SelectRandomSpawnPoint(BossSpawnFilter);

		if (selected_point)
		{
			spawn_origin = selected_point->s.origin;
			spawn_angles = selected_point->s.angles;
			location_found = true;
			if (developer->integer > 1)
				gi.Com_PrintFmt("SpawnBossAutomatically: Using random spawn point {} for boss {}.\n", spawn_origin, boss_classname);
		}
	}

	// --- Pass 3: Fallback to emergency spawn if no other location was found ---
	if (!location_found)
	{
		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: Failed to find suitable spawn point. Trying emergency spawn.\n", boss_classname);
		
		vec3_t emergency_origin, emergency_angles;
		bool used_human = false;
		if (FindEmergencySpawnPosition(emergency_origin, emergency_angles, used_human, boss_type))
		{
			// The emergency position is already checked for validity inside FindEmergencySpawnPosition
			spawn_origin = emergency_origin;
			spawn_angles = emergency_angles;
			location_found = true;
			if (developer->integer)
				gi.Com_PrintFmt("SpawnBossAutomatically: Using validated emergency spawn location {}.\n", spawn_origin);
		}
		else
		{
			if (developer->integer)
				gi.Com_PrintFmt("SpawnBossAutomatically: CRITICAL - All spawn attempts failed for boss.\n");
			return; // Absolutely no spot found, abort.
		}
	}

	// --- 5. Setup Delayed Spawn if a Location Was Found ---
	if (location_found)
	{
		// Create orb effect at the chosen location
		edict_t *orb = G_Spawn();
		if (orb)
		{
			orb->classname = "target_orb";
			orb->s.origin = spawn_origin;
			SP_target_orb(orb);
		}
		else if (developer->integer)
		{
			gi.Com_PrintFmt("SpawnBossAutomatically: Failed to spawn orb effect.\n");
		}

		// Spawn the boss entity placeholder
		edict_t *boss = G_Spawn();
		if (!boss)
		{
			if (orb) G_FreeEdict(orb);
			if (developer->integer) gi.Com_PrintFmt("SpawnBossAutomatically: Failed to G_Spawn boss entity.\n");
			return;
		}

		// Set basic info needed before think
		boss->classname = boss_classname;
		boss->s.origin = spawn_origin;
		boss->s.angles = spawn_angles;
		boss->owner = orb; // Link orb for removal in think
		boss->bossSizeCategory = selected_boss_size;

		// Perform Area Clearing Actions NOW
		constexpr float push_radius = 500.0f;
		constexpr float push_force = 1000.0f;
		PushEntitiesAway(spawn_origin, 3, push_radius, push_force, 3750.0f, 1600.0f);

		// Set up the delayed spawn via think function
		boss->nextthink = level.time + 750_ms;
		boss->think = BossSpawnThink;
		gi.linkentity(boss);
	}
}

void AppendHordeMessage(std::string_view message, gtime_t duration = 5_sec)
{
    // Early validation for empty messages or zero duration
    if (message.empty() || duration <= 0_ms)
    {
        return;
    }

    // Get current message from the configstring
    std::string current_msg_str = gi.get_configstring(CONFIG_HORDEMSG);

    // Append the new message with a newline for readability
    if (!current_msg_str.empty()) {
        current_msg_str += "\n"; // Add a newline before appending
    }
    current_msg_str += message;

    // Ensure the combined message does not exceed configstring limits
    if (current_msg_str.length() >= MAX_STRING_CHARS) {
        current_msg_str.resize(MAX_STRING_CHARS - 1); // Truncate to fit
    }

    // Set the combined message back to the configstring
    gi.configstring(CONFIG_HORDEMSG, current_msg_str.c_str());

    // Extend or set the duration for the new combined message
    horde_message_end_time = level.time + duration;
}

// --- MODIFIED ---
THINK(BossSpawnThink)(edict_t *self)->void
{
	if (self->owner)
	{
		G_FreeEdict(self->owner);
		self->owner = nullptr;
	}

	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);

	// This part sets the wave type and prints a wave theme message.
	auto bossWaveInfo = GetBossWaveType(typeId);
    current_wave_type = bossWaveInfo.first;
    StoreWaveType(current_wave_type);
	gi.LocBroadcast_Print(PRINT_CHAT, "{}", bossWaveInfo.second);

    // =======================================================================
    // --- NEW JIT PRECACHE BLOCK FOR BOSS MINIONS ---
    // =======================================================================
    if (developer->integer) {
        gi.Com_PrintFmt("BossSpawnThink: Precaching minions for boss wave...\n");
    }
    // Re-build the eligible list specifically for the minions.
    g_eligible_monsters_for_wave.clear();
    for (const auto& monster : monsterTypes) {
        if (monster.minWave <= current_wave_level && IsValidMonsterForWave(monster.typeId, current_wave_type)) {
            g_eligible_monsters_for_wave.push_back(&monster);
        }
    }

    // Now run the same JIT precache loop as in Horde_InitLevel.
	for (const MonsterTypeInfo* monster_info : g_eligible_monsters_for_wave)
	{
		if (g_precached_monster_types.find(monster_info->typeId) == g_precached_monster_types.end())
		{
			const char* classname = horde::MonsterTypeRegistry::GetClassname(monster_info->typeId);
			if (classname && *classname)
			{
				if (developer->integer) {
					gi.Com_PrintFmt("JIT Precache (Boss Minion): Loading assets for '{}'.\n", classname);
				}
				edict_t* temp_monster = G_Spawn();
				if (temp_monster) {
					temp_monster->classname = classname;
					// *** CRITICAL FIX APPLIED HERE ***
					temp_monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

					ED_CallSpawn(temp_monster);
					if (temp_monster->inuse) {
						G_FreeEdict(temp_monster);
					}
					g_precached_monster_types.insert(monster_info->typeId);
				}
			}
		}
	}
    // =======================================================================
    // --- END OF NEW BLOCK ---
    // =======================================================================

	self->monsterinfo.IS_BOSS = true;
	self->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	self->monsterinfo.last_sentrygun_target_time = 0_ms;

	self->solid = SOLID_NOT;
	ED_CallSpawn(self);

	if (!self->inuse)
	{
		if (developer->integer)
			gi.Com_PrintFmt("BossSpawnThink: ED_CallSpawn failed for boss {}.\n", self->classname ? self->classname : "Unknown");
		boss_spawned_for_wave = false;
		return;
	}

	boss_spawned_for_wave = true;

	// --- IMPROVEMENT: Use AppendHordeMessage for a more dynamic announcement ---
	std::string boss_display_name = GetDisplayName(self);
	if (!boss_display_name.empty()) {
		static constexpr std::array<const char*, 6> arrival_phrases = {
			"enters the arena!",
			"has joined the fight!",
			"has spawned!",
			"teleported in!",
			"is here to end this!",
			"makes its presence known!"
		};
		const char* random_phrase = arrival_phrases[irandom(arrival_phrases.size() - 1)];

		auto announce_message = G_Fmt("\nBOSS: {} {}", boss_display_name.c_str(), random_phrase);
		AppendHordeMessage(announce_message.data(), 4_sec);
	}

	self->solid = SOLID_BBOX;
	gi.linkentity(self);

	ConfigureBossArmor(self);
	ApplyBossEffects(self);
	self->monsterinfo.attack_state = AS_BLIND;

	if (self->inuse && !self->deadflag && self->health > 0)
	{
		const vec3_t spawn_pos = self->s.origin;
		const float magnitude = spawn_pos.length();
		const float base_size = std::max(100.0f, magnitude * 0.15f);
		const float end_size = base_size * 0.01f;
		ImprovedSpawnGrow(spawn_pos, base_size, end_size, self);
		SpawnGrow_Spawn(spawn_pos, base_size, end_size);
		if (sound_spawn1)
		{
			gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
		}
	}
	else if (self->inuse && developer->integer)
	{
		gi.Com_PrintFmt("SpawnGrow_Spawn/ImprovedSpawnGrow skipped in BossSpawnThink: Boss {} (idx {}) not fully alive. DeadFlag:{}, Health:%.0f\n",
						(self->classname ? self->classname : "Unknown"),
						(int)(self - g_edicts),
						self->deadflag,
						self->health);
	}

	AttachHealthBar(self);
	SetHealthBarName(self);
	auto_spawned_bosses.insert(self);

	if (developer->integer > 1)
	{
		gi.Com_PrintFmt("BossSpawnThink: Finalized spawn for boss {} at {}.\n", self->classname, self->s.origin);
	}
}

void ClearHordeMessage()
{
	std::string_view const current_msg = gi.get_configstring(CONFIG_HORDEMSG);
	if (!current_msg.empty())
	{
		gi.configstring(CONFIG_HORDEMSG, "");
		// Usar active_players() para resetear stats
		for (auto *player : active_players())
		{
			player->client->ps.stats[STAT_HORDEMSG] = 0;
		}
	}
	horde_message_end_time = 0_sec;
}

// reset cooldowns, fixed no monster spawning on next map
void ResetCooldowns() noexcept
{
	// Reset data for all *active* spawn points
	for (edict_t *spawnPoint : monster_spawn_points())
	{
		if (spawnPoint && spawnPoint->inuse)
		{													// Added explicit inuse check for safety
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

	if (developer->integer > 1)
	{
		gi.Com_PrintFmt("ResetCooldowns: Individual spawn point data and specific trackers reset.\n");
	}
}

void ResetAllSpawnAttempts() noexcept
{
	// Find all active spawn points and reset them
	for (uint32_t i = 1; i < globals.num_edicts; i++)
	{
		edict_t *ent = &g_edicts[i];
		if (ent && ent->inuse && ent->classname &&
			!strcmp(ent->classname, "info_player_deathmatch"))
		{
			auto &data = spawnPointsData[ent];
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
void ResetAmbushSystem()
{
	last_ambush_time = 0_sec;
	ambush_cooldown_end = 0_sec;
	waves_since_ambush = 0;
	// ambush_system_initialized = false;
}

void ResetWaveMemory()
{
	previous_wave_types.fill(MonsterWaveType::None);
	wave_memory_index = 0;
}

static void ResetRecentBosses() noexcept
{
	recent_bosses.clear();
}

static void ResetTeleportTracking() noexcept
{
	// Reset recent teleport position history
	std::fill(g_recent_teleport_positions.begin(), g_recent_teleport_positions.end(), RecentTeleportPosition{});
	g_recent_teleport_index = 0;

	// Reset global teleport rate limiting
	HordeConstants::g_teleport_rate_count = 0;
	HordeConstants::g_teleport_rate_reset_time = level.time; // Reset the timer completely

	if (developer && developer->integer > 1)
	{ // Optional debug print
		gi.Com_PrintFmt("Teleport tracking reset.\n");
	}
}

static void ResetAllSpawnPointDataAndTrackers()
{
	// 1. Reset data for all potential spawn points
	// Iterate all edicts to find spawn points, similar to ResetAllSpawnAttempts
	for (uint32_t i = 1; i < globals.num_edicts; i++)
	{
		edict_t *ent = &g_edicts[i];
		if (ent && ent->inuse && ent->classname &&
			!strcmp(ent->classname, "info_player_deathmatch"))
		{
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
	const horde::MapSize &mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level; // Should be 0 after ResetGame
	const int32_t humanPlayers = GetNumHumanPlayers();

	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);
	const float cooldownScale = CalculateCooldownScale(currentLevel, mapSize);
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	if (humanPlayers > 1)
	{
		const float playerAdjustment = 1.0f - (std::min(humanPlayers - 1, 3) * 0.05f);
		SPAWN_POINT_COOLDOWN *= playerAdjustment;
	}
	if ((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer))
	{
		SPAWN_POINT_COOLDOWN *= 0.95f;
	}
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN,
									  HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN,
									  3.0_sec);

	if (developer->integer > 1)
	{
		gi.Com_PrintFmt("ResetAllSpawnPointDataAndTrackers: Complete. Global Cooldown: %.2fs\n", SPAWN_POINT_COOLDOWN.seconds());
	}
}

// NEW HELPER FUNCTION
static void ResetSpawnBatchState()
{
    // Reset normal spawn batch state
    g_monsters_to_spawn_in_current_batch = 0;
    g_next_single_monster_spawn_time = 0_sec;
    g_champion_chance_for_current_batch = 0.2f; // Reset to default

    // Reset ambush/retaliation batch state
    g_monsters_to_spawn_in_current_ambush = 0;
    g_next_single_ambush_monster_spawn_time = 0_sec;
    g_current_ambush_info = AmbushSpawnInfo{}; // Reset to default
}

void ResetGame()
{
	if (hasBeenReset)
	{
		if (developer && developer->integer > 1)
		{
			gi.Com_PrintFmt("INFO: Reset already performed, skipping...\n");
		}
		return;
	}
	hasBeenReset = true;

	if (developer && developer->integer)
	{
		gi.Com_PrintFmt("INFO: Performing full game state reset...\n");
	}

	g_horde_retaliation_active = false;
	g_horde_retaliation_end_time = 0_sec;
	g_horde_retaliation_target_player = nullptr;

	for (size_t i = 0; i < game.maxclients; ++i)
	{
		if (game.clients[i].laser_manager)
		{
			if (developer && developer->integer > 1)
			{
				gi.Com_PrintFmt("Resetting PlayerLaserManager for client slot {} during ResetGame\n", i);
			}
			game.clients[i].laser_manager.reset();
		}
	}

	std::fill(g_recent_spawn_positions.begin(), g_recent_spawn_positions.end(), RecentSpawnPosition{});
	g_recent_position_index = 0;
	std::fill(g_recent_teleport_positions.begin(), g_recent_teleport_positions.end(), RecentTeleportPosition{});
	g_recent_teleport_index = 0;

	// HordeConstants::recent_teleport_count = 0;
	HordeConstants::g_teleport_rate_count = 0;
	HordeConstants::g_teleport_rate_reset_time = level.time;

	ResetTeleportTracking();
	ResetRecentBosses();
	ResetAmbushSystem();
	ResetWaveMemory();
	ResetChampionMonsterState();
	ResetBosses();
	ResetSpawnBatchState();

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
	if (g_chaotic)
		gi.cvar_set("g_chaotic", "0");
	if (g_insane)
		gi.cvar_set("g_insane", "0");
	if (g_hardcoop)
		gi.cvar_set("g_hardcoop", "0");
	if (dm_monsters)
		gi.cvar_set("dm_monsters", "0");
	if (timelimit)
		gi.cvar_set("timelimit", "60");
	if (ai_damage_scale)
		gi.cvar_set("ai_damage_scale", "1");
	if (ai_allow_dm_spawn)
		gi.cvar_set("ai_allow_dm_spawn", "1");
	if (g_damage_scale)
		gi.cvar_set("g_damage_scale", "1");
	if (g_vampire)
		gi.cvar_set("g_vampire", "0");
	if (g_startarmor)
		gi.cvar_set("g_startarmor", "0");
	if (g_ammoregen)
		gi.cvar_set("g_ammoregen", "0");
	if (g_upgradeproxs)
		gi.cvar_set("g_upgradeproxs", "0");
	if (g_piercingbeam)
		gi.cvar_set("g_piercingbeam", "0");
	if (g_tracedbullets)
		gi.cvar_set("g_tracedbullets", "0");
	if (g_energyshells)
		gi.cvar_set("g_energyshells", "0");
	if (g_bouncygl)
		gi.cvar_set("g_bouncygl", "0");
	if (g_bfgpull)
		gi.cvar_set("g_bfgpull", "0");
	if (g_bfgslide)
		gi.cvar_set("g_bfgslide", "1");
	if (g_autohaste)
		gi.cvar_set("g_autohaste", "0");

	g_full_precache_done = false;

	std::fill(used_wave_sounds.begin(), used_wave_sounds.end(), false);
	remaining_wave_sounds = NUM_WAVE_SOUNDS;
	std::fill(used_start_sounds.begin(), used_start_sounds.end(), false);
	remaining_start_sounds = NUM_START_SOUNDS;

	ClearHordeMessage();
	g_horde_local.reset_hud_state();

	gi.Com_PrintFmt("INFO: Horde game state reset complete.\n");
}

// Replace the existing CalculateRemainingMonsters() function
int32_t CalculateRemainingMonsters() noexcept
{
	// Simple counter approach - more reliable from old version
	const int32_t remaining = level.total_monsters - level.killed_monsters;
	return std::max(0, remaining); // Ensure non-negative
}

static bool CheckRemainingMonstersCondition(const horde::MapSize& mapSize, WaveEndReason& reason) {
    const gtime_t currentTime = level.time;
    const int32_t liveMonsters = CalculateRemainingMonsters();
    // Use g_horde_local.level which is the definitive current wave level
    const int32_t local_currentLevel = g_horde_local.level; 
    const uint16_t initialWaveTotalForPercentage = (g_totalMonstersInWave > 0) ? g_totalMonstersInWave : 1;


    if (next_wave_message_sent && !g_horde_local.conditionTriggered) {
        g_independent_timer_start = currentTime;
        g_horde_local.conditionTimeThreshold = g_lastParams.timeThreshold;
        if (g_horde_local.queued_monsters > initialWaveTotalForPercentage * 0.2f && g_horde_local.queued_monsters > 5) {
            gtime_t queue_bonus = gtime_t::from_sec(static_cast<float>(g_horde_local.queued_monsters) * 0.5f);
            queue_bonus = std::min(queue_bonus, 15_sec);
            g_horde_local.conditionTimeThreshold += queue_bonus;
            if (developer->integer) {
                gi.Com_PrintFmt("Post-Deploy Timer: Adding {}s bonus ({} queued). New threshold: {}s.\n",
                    queue_bonus.seconds(), g_horde_local.queued_monsters, g_horde_local.conditionTimeThreshold.seconds());
            }
        }
        g_horde_local.waveEndTime = currentTime + g_horde_local.conditionTimeThreshold;
        g_horde_local.conditionTriggered = true;
        g_horde_local.conditionStartTime = currentTime;

        if (developer->integer) {
            gi.Com_PrintFmt("Debug: Conditional timer initiated post-deployment. Ends in {}s.\n",
                g_horde_local.conditionTimeThreshold.seconds());
        }
    }

    if (allowWaveAdvance || (liveMonsters == 0 && g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters <= 0)) {
        if (Horde_AllMonstersDead()) {
             reason = WaveEndReason::AllMonstersDead;
             ResetWaveAdvanceState();
             return true;
        } else if (developer->integer) {
            gi.Com_PrintFmt("WARN: CheckRemaining: Pools empty, live count 0, but Horde_AllMonstersDead is false. Live: {}, NumSpawn: {}, Queued: {}. TotalLevel: {}. KilledLevel: {}\n",
                liveMonsters, g_horde_local.num_to_spawn, g_horde_local.queued_monsters, level.total_monsters, level.killed_monsters);
        }
    }
    
    if (currentTime >= g_independent_timer_start + g_lastParams.independentTimeThreshold) {
        reason = WaveEndReason::TimeLimitReached;
        if (developer->integer) gi.Com_PrintFmt("Wave ended: Independent time limit reached ({}s).\n", g_lastParams.independentTimeThreshold.seconds());
        return true;
    }

    const float percentageOfInitialWaveRemaining = static_cast<float>(liveMonsters) / initialWaveTotalForPercentage;

    if (!g_horde_local.conditionTriggered && !next_wave_message_sent) {
        const bool maxMonstersReached = liveMonsters <= g_lastParams.maxMonsters;
        const bool lowPercentageReached = percentageOfInitialWaveRemaining <= g_lastParams.lowPercentageThreshold;

        if (maxMonstersReached || lowPercentageReached) {
            g_horde_local.conditionTriggered = true;
            g_horde_local.conditionStartTime = currentTime;

            if (maxMonstersReached && lowPercentageReached) {
                g_horde_local.conditionTimeThreshold = std::min(
                    g_lastParams.timeThreshold, g_lastParams.lowPercentageTimeThreshold);
            } else {
                g_horde_local.conditionTimeThreshold = maxMonstersReached ?
                    g_lastParams.timeThreshold : g_lastParams.lowPercentageTimeThreshold;
            }

            if (g_horde_local.queued_monsters > 5) {
                gtime_t queue_bonus = gtime_t::from_sec(static_cast<float>(g_horde_local.queued_monsters) * 0.3f);
                queue_bonus = std::min(queue_bonus, 10_sec);
                g_horde_local.conditionTimeThreshold += queue_bonus;
                if (developer->integer) {
                    gi.Com_PrintFmt("Pre-Deploy ConditionalTimer: Adding {}s bonus ({} queued). New threshold: {}s.\n",
                        queue_bonus.seconds(), g_horde_local.queued_monsters, g_horde_local.conditionTimeThreshold.seconds());
                }
            }
            g_horde_local.waveEndTime = currentTime + g_horde_local.conditionTimeThreshold;

            if (local_currentLevel >= 15 && liveMonsters <= 5 && g_horde_local.queued_monsters < 3) {
                const float reduction_factor = 0.6f;
                g_horde_local.waveEndTime = currentTime + std::max(1_sec, g_horde_local.conditionTimeThreshold * reduction_factor);
                if (developer->integer) {
                    gi.Com_PrintFmt("High wave with few live & queued monsters (pre-deploy): reduced timeout by {}%%. New end in {}s.\n",
                        static_cast<int>((1.0f - reduction_factor) * 100.0f), (g_horde_local.waveEndTime - currentTime).seconds());
                }
            }
             if (developer->integer) {
                gi.Com_PrintFmt("Debug: Conditional timer initiated pre-deployment. Ends in {}s. Trigger: maxM ({}), lowP ({}). Queue: {}\n",
                    g_horde_local.conditionTimeThreshold.seconds(), maxMonstersReached, lowPercentageReached, g_horde_local.queued_monsters);
            }
        }
    }

    if (g_horde_local.conditionTriggered &&
        liveMonsters <= HordeConstants::MONSTERS_FOR_AGGRESSIVE_REDUCTION &&
        g_horde_local.queued_monsters < 3) { 
		float base_time = 6.0f + (liveMonsters * 1.5f);
		float map_size_multiplier = 1.0f;
		if (mapSize.isSmallMap) map_size_multiplier = 1.3f;
		else if (mapSize.isMediumMap) map_size_multiplier = 1.15f;
		base_time *= map_size_multiplier;

		if (IsBossWave() && boss_spawned_for_wave) {
			base_time *= 2.0f + (0.2f * liveMonsters);
			base_time = std::max(base_time, 10.0f);
		} else {
			if (local_currentLevel >= 15) { // Use local_currentLevel
				float reduction = std::min((local_currentLevel - 15) * 0.02f, 0.3f);
				base_time *= (1.0f - reduction);
			}
			int32_t playerCount = GetNumHumanPlayers();
			if (playerCount > 1) {
				float player_reduction = std::min((playerCount - 1) * 0.07f, 0.2f);
				base_time *= (1.0f - player_reduction);
			}
		}
		float min_time = (IsBossWave() && boss_spawned_for_wave) ? 7.0f : 5.0f;
		if (!IsBossWave() || !boss_spawned_for_wave) min_time *= map_size_multiplier;
		gtime_t aggressive_time = gtime_t::from_sec(std::max(min_time, base_time));
		const gtime_t original_remaining_conditional = (g_horde_local.waveEndTime > currentTime) ? (g_horde_local.waveEndTime - currentTime) : 0_sec;

        if (original_remaining_conditional > 0_sec && aggressive_time < original_remaining_conditional) {
            g_horde_local.waveEndTime = currentTime + aggressive_time;
            if (developer->integer) {
                gi.Com_PrintFmt("Aggressive time reduction (low queue): {}s remaining for {} live monsters.\n",
                    aggressive_time.seconds(), liveMonsters);
            }
        }
    }

    bool should_issue_warnings = g_horde_local.conditionTriggered || (g_horde_local.state == horde_state_t::active_wave && next_wave_message_sent);
    if (should_issue_warnings) {
        const gtime_t actualRelevantRemainingTime = GetWaveTimer();
        if (actualRelevantRemainingTime > 0_sec) {
            for (size_t i = 0; i < WARNING_TIMES.size(); ++i) {
                if (!g_horde_local.warningIssued[i] &&
                    actualRelevantRemainingTime <= gtime_t::from_sec(WARNING_TIMES[i]) &&
                    actualRelevantRemainingTime > gtime_t::from_sec(WARNING_TIMES[i]) - 1_sec) {
                    gi.LocBroadcast_Print(PRINT_HIGH, "{} seconds remaining!\n", static_cast<int>(WARNING_TIMES[i]));
                    g_horde_local.warningIssued[i] = true;
                }
            }
        }
    }

    if (g_horde_local.conditionTriggered && currentTime >= g_horde_local.waveEndTime) {
        reason = WaveEndReason::MonstersRemaining;
        if (developer->integer) gi.Com_PrintFmt("Wave ended: Conditional timer expired. Live: {}, Queued: {}.\n", liveMonsters, g_horde_local.queued_monsters);
        return true;
    }

    if (local_currentLevel >= 15 && liveMonsters <= 3 && g_horde_local.queued_monsters < 2 && g_horde_local.conditionTriggered) { // Use local_currentLevel
        const gtime_t elapsed_since_condition_start = currentTime - g_horde_local.conditionStartTime;
        if (g_horde_local.conditionTimeThreshold > 0_sec &&
            elapsed_since_condition_start >= (g_horde_local.conditionTimeThreshold * 0.7f)) {
            reason = WaveEndReason::MonstersRemaining;
            if (developer->integer) gi.Com_PrintFmt("Wave ended: High level, few monsters, 70%% of conditional timer elapsed.\n");
            return true;
        }
    }

    return false;
}

void ResetWaveAdvanceState() noexcept
{
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

void AllowNextWaveAdvance() noexcept
{
	allowWaveAdvance = true;
}

// This new version correctly and instantly starts the next wave.
void fastNextWave() noexcept
{
	// 1. Clean up any lingering state from the wave we are skipping.
	ClearHordeMessage();
	ResetWaveAdvanceState(); // Resets timers and flags.

	// 2. Initialize all game variables for the *next* wave.
	// Horde_InitLevel already handles setting the level, calculating monster counts, etc.
	Horde_InitLevel(g_horde_local.level + 1);

	// 3. Force the state machine directly into the spawning state for the new wave.
	g_horde_local.state = horde_state_t::spawning;

	// 4. Set timers to the current time to ensure spawning begins on the very next frame.
	g_horde_local.monster_spawn_time = level.time;
	g_horde_local.warm_time = level.time; // Also reset the warmup timer for consistency.

	// 5. Announce the new wave so the player knows what's happening.
	AnnounceIncomingWave(3_sec);
}

inline int8_t GetNumActivePlayers()
{
	const auto &players = active_players();
	return std::count_if(players.begin(), players.end(),
						 [](const edict_t *const player)
						 {
							 return player->client && player->client->resp.ctf_team == CTF_TEAM1;
						 });
}

inline int8_t GetNumHumanPlayers()
{
	const auto &players = active_players_no_spect();
	return std::count_if(players.begin(), players.end(),
						 [](const edict_t *const player)
						 {
							 return !(player->svflags & SVF_BOT);
						 });
}

inline int8_t GetNumSpectPlayers()
{
	const auto &players = active_players();
	return std::count_if(players.begin(), players.end(),
						 [](const edict_t *const player) noexcept
						 {
							 return ClientIsSpectating(player->client);
						 });
}

// Implementación de DisplayWaveMessage
static void DisplayWaveMessage(gtime_t duration = 5_sec)
{
	static const std::array<const char *, 4> messages = {
		"Horde Menu available upon opening Inventory or using TURTLE on POWERUP WHEEL\n\nMAKE THEM PAY!\n",
		"Welcome to Hell.\n\nUse FlipOff <Key> looking at walls to spawn lasers (cost: 25 cells)\n",
		"New Tactics!\n\nTeslas can now be placed on walls and ceilings!\n\nUse them wisely!",
		"Improved Traps!\n\nTraps are reutilizable after 5sec of eating a strogg!"};

	// Usar distribución uniforme con mt_rand
	std::uniform_int_distribution<size_t> dist(0, messages.size() - 1);
	const size_t choice = dist(mt_rand);
	UpdateHordeMessage(messages[choice], duration);
}

void HandleWaveCleanupMessage(const horde::MapSize &mapSize, const WaveEndReason reason) noexcept
{
	// Obtener el número de jugadores humanos
	const int8_t numHumanPlayers = GetNumHumanPlayers();

	// Si la ola terminó con todos los monstruos muertos, aplicar reglas normales
	if (reason == WaveEndReason::AllMonstersDead)
	{
		if (current_wave_level >= 15 && current_wave_level <= 26)
		{
			gi.cvar_set("g_insane", "1");
			gi.cvar_set("g_chaotic", "0");
		}
		else if (current_wave_level >= 27)
		{
			gi.cvar_set("g_insane", "2");
			gi.cvar_set("g_chaotic", "0");
		}
		else if (current_wave_level <= 14)
		{
			gi.cvar_set("g_insane", "0");
			// Activar chaotic2 si es mapa pequeño Y hay 2+ jugadores, sino chaotic1
			gi.cvar_set("g_chaotic", (mapSize.isSmallMap && numHumanPlayers >= 2) ? "2" : "1");
		}
	}
	else
	{
		// Si la ola no terminó por victoria completa, pequeña probabilidad de mantener la dificultad
		const float probability = mapSize.isBigMap ? 0.3f : mapSize.isSmallMap ? 0.2f
																			   : 0.25f; // 20-30% según tamaño de mapa
		if (frandom() < probability)
		{
			// Si gana la probabilidad, aplicar la dificultad según el nivel actual
			if (current_wave_level >= 15 && current_wave_level <= 26)
			{
				gi.cvar_set("g_insane", "1");
				gi.cvar_set("g_chaotic", "0");
			}
			else if (current_wave_level >= 27)
			{
				gi.cvar_set("g_insane", "2");
				gi.cvar_set("g_chaotic", "0");
			}
			else if (current_wave_level <= 14)
			{
				gi.cvar_set("g_insane", "0");
				// Activar chaotic2 si es mapa pequeño Y hay 3+ jugadores, sino chaotic1
				gi.cvar_set("g_chaotic", (mapSize.isSmallMap && numHumanPlayers >= 3) ? "2" : "1");
			}
		}
		else
		{
			// Si no gana la probabilidad, desactivar ambos modos
			gi.cvar_set("g_insane", "0");
			gi.cvar_set("g_chaotic", "0");
		}
	}
	g_horde_local.state = horde_state_t::rest;
}

// ... (rest of your includes and global variables) ...

// MODIFIED FUNCTION: AnnounceIncomingWave
static void AnnounceIncomingWave(gtime_t duration)
{
	const char *message;

    // Define message pools for each difficulty level
    static constexpr std::array<const char*, 4> normal_messages = {
        "Strogg forces are pushing! Stay alert!",
        "Incoming wave detected! Hold position!",
        "Prepare for the next assault!",
        "The horde advances! Brace yourselves!"
    };

    static constexpr std::array<const char*, 4> chaotic1_messages = {
        "Chaotic wave incoming! Steel yourself!",
        "Chaos approaches! Ready for battle!",
        "The horde is restless! Expect the unexpected!",
        "Unpredictable forces approaching! Adapt or die!"
    };

    static constexpr std::array<const char*, 4> chaotic2_messages = {
        "Relentless wave incoming! Stand your ground!",
        "Overwhelming forces approaching! Hold the line!",
        "The horde shows no mercy! Fight with all you have!",
        "An unstoppable tide approaches! This is it!"
    };

    static constexpr std::array<const char*, 4> insane1_messages = {
        "Intense wave incoming! Show no mercy!",
        "Fierce battle ahead! Stand ready!",
        "The Strogg are enraged! Push them back!",
        "Survival is not guaranteed! Fight for every inch!"
    };

    // Expanded and refined insane2_messages
    static constexpr std::array<const char*, 5> insane2_messages = { // Increased size to 5
        "This is it, marines! Make it count!",       // Keep this classic
        "No retreat! Fight until your last breath!", // Desperate, but determined
        "Overwhelmed! Make them pay for every inch!",// Focus on making them suffer
        "They're everywhere! Don't give an inch!",   // Sense of being surrounded
        "Looks like a glorious death! Take 'em with you!" // Dark humor
    };

    // Select message based on difficulty and level
	if (g_chaotic->integer > 0 && g_horde_local.level >= 5)
	{
		if (g_chaotic->integer == 2)
		{
            message = chaotic2_messages[irandom(chaotic2_messages.size() - 1)];
		}
		else // g_chaotic->integer == 1
		{
            message = chaotic1_messages[irandom(chaotic1_messages.size() - 1)];
		}
	}
	else if (g_insane->integer > 0)
	{
		if (g_insane->integer == 2)
		{
            message = insane2_messages[irandom(insane2_messages.size() - 1)];
		}
		else // g_insane->integer == 1
		{
            message = insane1_messages[irandom(insane1_messages.size() - 1)];
		}
	}
	else // Normal difficulty
	{
        message = normal_messages[irandom(normal_messages.size() - 1)];
	}

	for (auto player : active_players())
	{
		if (player->client)
		{
			player->client->total_damage = 0;
		}
	}

	// This general wave announcement is now appended, not replaced.
	// The boss announcement (if applicable) will be appended by BossSpawnThink.
	AppendHordeMessage(message, duration); // Changed from UpdateHordeMessage to AppendHordeMessage


	g_independent_timer_start = level.time;
	g_horde_local.waveEndTime = 0_sec;
	g_horde_local.conditionTriggered = false;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTimeThreshold = 0_sec;

	for (size_t i = 0; i < WARNING_TIMES.size(); i++)
	{
		g_horde_local.warningIssued[i] = false;
	}

	gi.sound(world, CHAN_VOICE, GetRandomWaveSound(), 1, ATTN_NONE, 0);
}

void InitializeWaveSystem() noexcept
{
	PrecacheWaveSounds();
}

static void SetMonsterArmor(edict_t *monster);
static void SetNextMonsterSpawnTime(const horde::MapSize &mapSize);

// REPLACEMENT: FindEmergencySpawnPosition (with robust checks)
bool FindEmergencySpawnPosition(vec3_t &position, vec3_t &angles, bool &used_human_player, horde::MonsterTypeID typeId, edict_t* specific_target)
{
	PROFILE_SCOPE("FindEmergencySpawnPosition");
	used_human_player = false;

	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(typeId, predicted_mins, predicted_maxs);
	const bool is_flying = IsFlying(typeId);

	std::vector<edict_t*> target_candidates;
	target_candidates.reserve(MAX_CLIENTS);

	if (specific_target && specific_target->inuse && specific_target->health > 0) {
		target_candidates.push_back(specific_target);
	} else {
		for (auto* p : active_players_no_spect()) {
			target_candidates.push_back(p);
		}
		if (!target_candidates.empty()) {
			std::shuffle(target_candidates.begin(), target_candidates.end(), mt_rand);
		}
	}

	if (target_candidates.empty()) {
		return false;
	}

	constexpr int MAX_ATTEMPTS_PER_PLAYER = 16;
	constexpr float MIN_RADIUS = HordeConstants::MIN_PLAYER_DIST_GENERATE;
	constexpr float MAX_RADIUS = 800.0f; // Reduced radius slightly
    static const vec3_t trace_box = {-4, -4, -4}; // Box for fat trace

	for (edict_t* player : target_candidates)
	{
		const vec3_t player_origin = player->s.origin;
		for (int attempt = 0; attempt < MAX_ATTEMPTS_PER_PLAYER; ++attempt)
		{
			const float radius = frandom(MIN_RADIUS, MAX_RADIUS);
			const float angle_rad = frandom() * 2.0f * PIf;
			vec3_t candidate_pos = {
				player_origin.x + cosf(angle_rad) * radius,
				player_origin.y + sinf(angle_rad) * radius,
				player_origin.z + frandom(8.0f, 48.0f) // Adjusted Z offset
			};

            // TWO-STEP VALIDATION FOR EMERGENCY SPAWNS
            
            // Step A: "Fat" trace from PLAYER to candidate position to ensure reachability.
            trace_t los_trace = gi.trace(player_origin, trace_box, trace_box, candidate_pos, player, MASK_SOLID);
            if (los_trace.fraction < 1.0f) {
                continue; // Path from player is blocked by world geometry.
            }

			// Step B: Check if the spot itself is physically valid.
			if (IsPositionPhysicallyValid(candidate_pos, predicted_mins, predicted_maxs, is_flying) &&
				!IsPositionTooCloseToRecentSpawn(candidate_pos) &&
				!IsPositionTooCloseToRecentTeleport(candidate_pos))
			{
				position = candidate_pos;
				vec3_t dir_to_player = player_origin - candidate_pos;
				angles = (dir_to_player.lengthSquared() > VECTOR_LENGTH_SQ_EPSILON) ? vectoangles(dir_to_player) : player->s.angles;
				angles[PITCH] = 0;

				used_human_player = !(player->svflags & SVF_BOT);
				return true;
			}
		}
	}

	return false;
}

// String-based overload that delegates to the TypeID version
bool FindEmergencySpawnPosition(vec3_t &position, vec3_t &angles, bool &used_human_player, const char *monster_classname, edict_t* specific_target)
{
	horde::MonsterTypeID typeId = monster_classname ? horde::MonsterTypeRegistry::GetTypeID(monster_classname) : horde::MonsterTypeID::UNKNOWN;
	return FindEmergencySpawnPosition(position, angles, used_human_player, typeId, specific_target);
}

// Helper function to check if any human (non-bot) players are active and not spectating
bool AreHumanPlayersPresent()
{
	for (auto *player : active_players())
	{
		if (player && player->inuse && player->client &&
			!(player->svflags & SVF_BOT) &&
			!ClientIsSpectating(player->client) &&
			player->health > 0)
		{
			return true;
		}
	}
	return false;
}

static BoxEdictsResult_t SpawnPointFilter(edict_t *ent, void *data)
{
	FilterData *filter_data = static_cast<FilterData *>(data);

	if (ent == filter_data->ignore_ent)
	{ // Ignore self if specified
		return BoxEdictsResult_t::Skip;
	}
	if (ent->client && ent->inuse)
	{ // Player/Bot check
		filter_data->count++;
		return BoxEdictsResult_t::End;
	}
	if ((ent->svflags & SVF_MONSTER) && ent->inuse && !ent->deadflag)
	{ // Live Monster check
		filter_data->count++;
		return BoxEdictsResult_t::End;
	}
	if (ent->inuse && IsPlayerDefense(ent))
	{ // Player Defense check
		filter_data->count++;
		return BoxEdictsResult_t::End;
	}
	return BoxEdictsResult_t::Skip;
}

// REPLACEMENT for IsPositionPhysicallyValid
// This is a low-level validator. Its only job is to answer:
// "Can a monster physically exist at this specific coordinate?"
// It checks for solid world geometry, finds a valid floor, and checks for entity occupation.
[[nodiscard]] bool IsPositionPhysicallyValid(vec3_t& io_position, const vec3_t& monster_mins, const vec3_t& monster_maxs, bool is_flying, bool is_predefined_location)
{
    // For flying monsters OR any monster at a predefined spot, we only care if the space is clear.
    // We don't need to find a floor.
    if (is_flying || is_predefined_location) {
        trace_t trace = gi.trace(io_position, monster_mins, monster_maxs, io_position, nullptr, MASK_MONSTERSOLID);
        if (trace.startsolid || trace.allsolid) {
            return false; // The space is occupied by something.
        }
        return true; // The space is clear, it's a valid spawn.
    }

    // --- This block now ONLY runs for NON-FLYING monsters at NON-PREDEFINED locations (e.g., random spawns) ---
    // Stage A: We MUST find a floor for these.
    vec3_t original_pos = io_position;
    if (!M_droptofloor_generic(io_position, monster_mins, monster_maxs, false, nullptr, MASK_SOLID, false)) {
        io_position = original_pos;
        return false;
    }

    // Stage B: Check for other entities at the new, floor-dropped position.
    trace_t entity_trace = gi.trace(io_position, monster_mins, monster_maxs, io_position, nullptr, MASK_MONSTERSOLID);
    if (entity_trace.startsolid) {
        return false;
    }

    return true;
}

// helper function 
static edict_t* FindBestPlayerTargetForTeleport()
{
    edict_t* target_player = nullptr;
    int max_spree = -1;
    int32_t max_damage = -1;

    // First pass: Find the player with the highest spree or damage
    for (auto* player : active_players_no_spect()) {
        if (player && player->client) {
            if (player->client->resp.spree > max_spree) {
                max_spree = player->client->resp.spree;
                target_player = player;
            }
            if (player->client->total_damage > max_damage) {
                max_damage = player->client->total_damage;
                if (max_spree < 5) { // Damage only takes precedence if spree is low
                    target_player = player;
                }
            }
        }
    }

    // If no one has spree or damage, just pick a random active player
    if (!target_player) {
        std::vector<edict_t*> active;
        for (auto* p : active_players_no_spect()) {
            active.push_back(p);
        }
        if (!active.empty()) {
            target_player = random_element(active);
        }
    }

    return target_player;
}

// --- HELPER: Finds a valid standard spawn point for teleporting ---
// REPLACEMENT: FindTeleportDestination (Corrected Logic)
static edict_t* FindTeleportDestination(edict_t* self)
{
    // Get the monster's fundamental capability (can it EVER fly?)
    horde::MonsterTypeID monsterTypeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
    const bool can_monster_fly = IsFlying(monsterTypeId);

    std::vector<edict_t*> candidates;
    candidates.reserve(MAX_SPAWN_POINTS);

    for (edict_t* spawn_point : monster_spawn_points())
    {
        if (!spawn_point || !spawn_point->inuse) continue;

        // Check cooldowns and occupation
        if (level.time < spawnPointsData[spawn_point].teleport_cooldown || IsSpawnPointOccupied(spawn_point)) {
            continue;
        }

        const bool is_flying_point = (spawn_point->style == 1);

        // --- REVISED LOGIC ---
        // The only invalid combination is a ground-only monster trying to use a flying point.
        if (!can_monster_fly && is_flying_point) {
            // This monster cannot fly, but the point is for flyers. Skip it.
            continue;
        }
        
        // In all other cases, the point is valid:
        // - A flyer (can_monster_fly = true) can use a ground point.
        // - A flyer (can_monster_fly = true) can use a flying point.
        // - A grounder (can_monster_fly = false) can use a ground point.

        candidates.push_back(spawn_point);
    }

    if (candidates.empty()) {
        return nullptr;
    }

    return random_element(candidates);
}

// REPLACEMENT: CheckAndTeleportStuckMonster (with global rate limiting and bug fixes)
bool CheckAndTeleportStuckMonster(edict_t *self)
{
    PROFILE_SCOPE("CheckAndTeleportStuckMonster");

    // --- 1. Initial Validation & Cooldowns ---
    if (level.intermissiontime || !self || !self->inuse || self->deadflag || self->monsterinfo.IS_BOSS || !g_horde->integer || self->monsterinfo.issummoned)
        return false;

    if (level.time < self->monsterinfo.stuck_check_time)
        return false;
    self->monsterinfo.stuck_check_time = level.time + random_time(2.0_sec, 4.0_sec);

    if (!strcmp(self->classname, "misc_insane") || !strcmp(self->classname, "monster_turret"))
        return false;
    
    if (IsMonsterJumping(self)) {
        self->teleport_time = level.time + 0.5_sec; // Don't teleport mid-jump
        return false;
    }

    // --- 2. Global Rate Limiting ---
    if (level.time > HordeConstants::g_teleport_rate_reset_time) {
        HordeConstants::g_teleport_rate_count = 0;
        HordeConstants::g_teleport_rate_reset_time = level.time + HordeConstants::GLOBAL_TELEPORT_RESET_INTERVAL;
    }
    int max_teleports = HordeConstants::MAX_TELEPORTS_PER_INTERVAL + ((g_insane->integer || g_chaotic->integer) ? 1 : 0);
    if (HordeConstants::g_teleport_rate_count >= max_teleports) {
        return false; // Global limit reached for this interval.
    }

    // --- 3. Determine if Teleport is Needed ---
    bool needs_teleport = false;
    const char* reason_str = "Unknown";

    if (self->waterlevel > 0 && !(self->enemy && visible(self, self->enemy, false))) {
        needs_teleport = true;
        reason_str = "Drowning";
    } else if (gi.trace(self->s.origin, self->mins, self->maxs, self->s.origin, self, MASK_SOLID).startsolid) {
        needs_teleport = true;
        reason_str = "Stuck in Geometry";
    } else if (self->teleport_time <= level.time) {
        if (self->enemy && self->monsterinfo.attack_finished > level.time) return false; // Don't teleport mid-attack
        
        if (!self->enemy || !self->enemy->inuse) {
            if (self->monsterinfo.no_enemy_timeout_start_time == 0_sec) self->monsterinfo.no_enemy_timeout_start_time = level.time;
            if (level.time > self->monsterinfo.no_enemy_timeout_start_time + 12_sec) {
                needs_teleport = true;
                reason_str = "No Enemy Timeout";
            }
        } else {
            self->monsterinfo.no_enemy_timeout_start_time = 0_sec; // Reset if it finds an enemy
        }

        if (!needs_teleport && level.time > self->monsterinfo.react_to_damage_time + HordeConstants::NO_DAMAGE_TIMEOUT) {
            needs_teleport = true;
            reason_str = "No Damage Timeout";
        }
    }

    if (!needs_teleport) return false;

    if (developer->integer) gi.Com_PrintFmt("[CATS] Trigger for {}: {}.\n", self->classname, reason_str);

    // --- 4. Find Teleport Destination (Multi-Pass Strategy) ---
    vec3_t dest_origin = vec3_origin;
    vec3_t dest_angles = self->s.angles;
    edict_t* used_spawn_point = nullptr;

    used_spawn_point = FindTeleportDestination(self);
    if (used_spawn_point) {
        dest_origin = used_spawn_point->s.origin;
        dest_angles = used_spawn_point->s.angles;
    }

    if (dest_origin == vec3_origin || frandom() < 0.40f) {
        if (developer->integer > 1) gi.Com_PrintFmt("[CATS] Attempting Pass 2 (Emergency Teleport) for {}.\n", self->classname);
        
        bool used_human_player = false;
        horde::MonsterTypeID monsterTypeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
        
        if (FindEmergencySpawnPosition(dest_origin, dest_angles, used_human_player, monsterTypeId, FindBestPlayerTargetForTeleport())) {
            used_spawn_point = nullptr;
        } else if (dest_origin == vec3_origin) {
            if (developer->integer) gi.Com_PrintFmt("[CATS] CRITICAL: All teleport attempts failed for {}.\n", self->classname);
            self->teleport_time = level.time + 5.0_sec;
            return false;
        }
    }

    // --- 5. Execute the Teleport ---
    if (IsPositionTooCloseToRecentTeleport(dest_origin)) {
        if (developer->integer > 1) gi.Com_PrintFmt("[CATS] Chosen destination is too close to recent teleport, skipping.\n");
        return false;
    }
    
    // <<< BUG FIX: Ensure monster isn't looking up/down after teleport >>>
    dest_angles[PITCH] = 0;

    if (Horde_TeleportMonster(self, dest_origin, dest_angles, true, false)) {
        MarkPositionAsRecentlyTeleported(self->s.origin);
        if (used_spawn_point) {
            spawnPointsData[used_spawn_point].teleport_cooldown = level.time + 3.5_sec;
        }

        horde::MonsterTypeID monsterTypeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
        if (monsterTypeId == horde::MonsterTypeID::STALKER || monsterTypeId == horde::MonsterTypeID::SPIDER) {
            self->gravityVector = {0, 0, -1};
            self->s.angles[ROLL] = 0;
            self->gravity = 1.0f;
            if (self->movetype == MOVETYPE_FLY || self->movetype == MOVETYPE_NOCLIP) self->movetype = MOVETYPE_STEP;
            self->groundentity = nullptr;
            self->s.origin.z += 16.0f;
            gi.linkentity(self);
        }

        HordeConstants::g_teleport_rate_count++;
        self->monsterinfo.was_stuck = false;
        self->monsterinfo.stuck_check_time = 0_sec;
        self->monsterinfo.no_enemy_timeout_start_time = 0_sec;
        return true;
    }

    return false;
}

// Helper function to select a retaliation-themed monster
horde::MonsterTypeID PickRetaliationMonsterTypeID(int32_t waveLevel)
{
	WeightedSelection<MonsterTypeInfo> selection;
	selection.clear();

	// Define the desired types for retaliation
	constexpr MonsterWaveType RETALIATION_TYPES = MonsterWaveType::Spawner | MonsterWaveType::Bomber | MonsterWaveType::Special;

	for (const auto &monsterInfo : monsterTypes)
	{
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
	const MonsterTypeInfo *chosenInfo = selection.select();
	if (chosenInfo)
	{
		return chosenInfo->typeId;
	}

	// Fallback if no specific types found (should be rare)
	if (developer->integer)
	{
		gi.Com_PrintFmt("PickRetaliationMonsterTypeID: Warning - No Spawner/Bomber/Special found for level {}. Falling back.\n", waveLevel);
	}
	// Fallback to a common medium monster
	return (waveLevel > 10) ? horde::MonsterTypeID::GUNNER : horde::MonsterTypeID::INFANTRY;
}


// REPLACEMENT: SpawnRetaliationAmbush (initiates a time-sliced batch)
int SpawnRetaliationAmbush(const horde::MapSize &mapSize, int32_t waveLevel, edict_t *target_player)
{
	if (g_monsters_to_spawn_in_current_ambush > 0) return 0;

	// <<< FIX: New dynamic calculation for ambush size >>>
	int baseSize = mapSize.isSmallMap ? 2 : (mapSize.isBigMap ? 4 : 3);
	int spreeBonus = 0;
	int levelBonus = waveLevel / 10; // +1 monster for every 10 waves

	// Add a bonus based on the target player's performance
	if (target_player && target_player->client) {
		// Add +1 monster for every 8 kills in the player's spree
		spreeBonus = target_player->client->resp.spree / 8;
	}

	// Combine and cap the size to prevent it from getting out of control
	int ambushSize = baseSize + spreeBonus + levelBonus;
	ambushSize = std::min(ambushSize, 7); // Cap at a max of 7 retaliation monsters

	if (developer->integer) {
		gi.Com_PrintFmt("HORDE: INITIATING Retaliation Ambush (Size: {}). Target: {} (Spree: {}, Lvl: {})\n",
			ambushSize,
			GetPlayerName(target_player).c_str(),
			(target_player && target_player->client) ? target_player->client->resp.spree : 0,
			waveLevel);
	}

	horde::MonsterTypeID typeId = PickRetaliationMonsterTypeID(waveLevel);
	if (typeId == horde::MonsterTypeID::UNKNOWN) return 0;

	// Set a very high champion chance for these priority spawns
	g_current_ambush_info = {typeId, 0.6f + (frandom() * 0.25f), true, target_player};
	g_monsters_to_spawn_in_current_ambush = ambushSize;
	g_next_single_ambush_monster_spawn_time = level.time;

	return ambushSize;
}

// Corrected HandleSpawnPhaseAggression
// Corrected HandleSpawnPhaseAggression
void HandleSpawnPhaseAggression(edict_t* monster) {
	if (!monster || !monster->inuse)
		return;

	if (monster->spawned_in_spawn_state && g_horde_local.state == horde_state_t::spawning) {
		static int32_t spawn_state_deaths = 0;
		static gtime_t last_death_time = 0_sec;

		if (level.time - last_death_time > 8_sec) {
			spawn_state_deaths = 0;
		}
		spawn_state_deaths++;
		last_death_time = level.time;

		if (developer->integer) {
			std::string killer_name = "Unknown";
			if (monster->enemy && monster->enemy->client) {
				killer_name = GetPlayerName(monster->enemy);
			}
			gi.Com_PrintFmt("Monster killed during spawning state by {} ({} total recent)\n", killer_name.c_str(), spawn_state_deaths);
		}

		const uint16_t initial_wave_size_for_progress = (g_totalMonstersInWave > 0) ? g_totalMonstersInWave : 1;
		const float spawn_progress = static_cast<float>(monsters_spawned_in_current_phase) / static_cast<float>(initial_wave_size_for_progress);

		constexpr int32_t MIN_RECENT_DEATHS_FOR_RETALIATION = 8;
		constexpr float MIN_SPAWN_PROGRESS_FOR_RETALIATION = 0.05f;
		constexpr uint16_t MIN_TOTAL_SPAWNED_FOR_RETALIATION = 8;

		if (spawn_state_deaths >= MIN_RECENT_DEATHS_FOR_RETALIATION &&
            (spawn_progress >= MIN_SPAWN_PROGRESS_FOR_RETALIATION || monsters_spawned_in_current_phase >= MIN_TOTAL_SPAWNED_FOR_RETALIATION)) {
			if (!g_horde_retaliation_active) {
				g_horde_retaliation_active = true;
				g_horde_retaliation_end_time = level.time + 12_sec;

				// <<< FIX: New targeting logic based on spree, then damage >>>
				g_horde_retaliation_target_player = nullptr;
				edict_t* top_spree_player = nullptr;
				int max_spree = 0; // Start at 0, as we only care about positive sprees.

				// --- 1. Find player with the highest spree ---
				for (auto* p : active_players_no_spect()) {
					if (p && p->client && p->client->resp.spree > max_spree) {
						max_spree = p->client->resp.spree;
						top_spree_player = p;
					}
				}

				if (top_spree_player) {
					// We found a player with the highest spree.
					g_horde_retaliation_target_player = top_spree_player;
				} else {
					// --- 2. If no one has a spree, fall back to the top damager ---
					PlayerStats top_player_stats;
					float percentage;
					CalculateTopDamager(top_player_stats, percentage);
					if (top_player_stats.player && top_player_stats.player->client) {
						g_horde_retaliation_target_player = top_player_stats.player;
					}
				}

				// --- 3. Final fallback: If no target was found, pick a random player ---
				if (!g_horde_retaliation_target_player) {
					std::vector<edict_t*> candidates;
					for (auto* p : active_players_no_spect()) {
						candidates.push_back(p);
					}
					if (!candidates.empty()) {
						g_horde_retaliation_target_player = random_element(candidates);
					}
				}
				// <<< END FIX >>>

				if (developer->integer) {
					std::string target_player_name = GetPlayerName(g_horde_retaliation_target_player);
					gi.Com_PrintFmt("HORDE: Retaliation Mode Activated for {}s (Target: {}). Triggered by rapid kills during spawning.\n",
						(g_horde_retaliation_end_time - level.time).seconds(), target_player_name.c_str());
				}

				SpawnRetaliationAmbush(g_horde_local.current_map_size, g_horde_local.level, g_horde_retaliation_target_player);

                int32_t base_retaliation_add = (g_horde_local.level <= 7) ? 4 : 7;
                if (g_horde_local.level > 12) base_retaliation_add = 10;
                int32_t monsters_to_add_to_queue = base_retaliation_add + (g_horde_local.level / 4);
                if (g_horde_local.current_map_size.isBigMap)
                    monsters_to_add_to_queue += (g_horde_local.level > 7 ? 5 : 2);
                else if (g_horde_local.current_map_size.isMediumMap)
                    monsters_to_add_to_queue += (g_horde_local.level > 7 ? 3 : 1);
                monsters_to_add_to_queue = std::min(monsters_to_add_to_queue, 15);

				if (monsters_to_add_to_queue > 0) {
                    g_horde_local.queued_monsters += monsters_to_add_to_queue;
                    if(g_horde_local.state == horde_state_t::spawning) {
                        initial_total_monsters_for_spawning_phase_timeout += monsters_to_add_to_queue;
                    }
                    if (developer->integer) {
                        gi.Com_PrintFmt("HORDE: Retaliation added {} monsters to queue (New total: {}). Cap check bypassed.\n",
                            monsters_to_add_to_queue, g_horde_local.queued_monsters);
                    }
                } else if (developer->integer) {
                    gi.Com_PrintFmt("HORDE: Retaliation wanted to add monsters, but calculated 0 to add.\n");
                }
				spawn_state_deaths = 0;
			}
		}
		monster->was_spawned_by_horde = false;
		monster->spawned_in_spawn_state = false;
	}
}

// REPLACEMENT: SpawnAmbushMonsters (initiates a time-sliced batch)
int SpawnAmbushMonsters(const horde::MapSize &mapSize, int32_t waveLevel)
{
	if (g_monsters_to_spawn_in_current_ambush > 0) return 0;

	horde::MonsterTypeID monster_typeId_for_ambush = horde::MonsterTypeID::UNKNOWN;
	const int32_t currentLevel_ctx = g_horde_local.level;
	const MonsterWaveType actualWaveType_ctx = current_wave_type;
	
	for (int i = 0; i < 5; ++i) {
        if (g_potential_spawn_points.empty()) break;
		edict_t* point = g_potential_spawn_points[irandom(g_potential_spawn_points.size() - 1)];
		if (point && point->inuse) {
			monster_typeId_for_ambush = G_HordePickMonsterType(point, currentLevel_ctx, actualWaveType_ctx, false, false, MonsterWaveType::None);
			if (monster_typeId_for_ambush != horde::MonsterTypeID::UNKNOWN) break;
		}
	}

	if (monster_typeId_for_ambush == horde::MonsterTypeID::UNKNOWN) {
		if (waveLevel >= 15) {
			static const std::array<horde::MonsterTypeID, 5> types = {horde::MonsterTypeID::GUNNER, horde::MonsterTypeID::GLADIATOR, horde::MonsterTypeID::TANK, horde::MonsterTypeID::SOLDIER_HYPERGUN, horde::MonsterTypeID::SOLDIER_LASERGUN};
			monster_typeId_for_ambush = types[irandom(types.size() - 1)];
		} else {
			static const std::array<horde::MonsterTypeID, 5> types = {horde::MonsterTypeID::SOLDIER_LIGHT, horde::MonsterTypeID::SOLDIER, horde::MonsterTypeID::INFANTRY, horde::MonsterTypeID::SOLDIER_SS, horde::MonsterTypeID::FLYER};
			monster_typeId_for_ambush = types[irandom(types.size() - 1)];
		}
	}

	const int baseCount = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
	const int ambushSize = baseCount + (waveLevel >= 15 ? 2 : 1);

	if (developer->integer) gi.Com_PrintFmt("HORDE: INITIATING Ambush (Size: {}). Spawning will be time-sliced.\n", ambushSize);

	g_current_ambush_info = {monster_typeId_for_ambush, 0.20f, false, nullptr};
	g_monsters_to_spawn_in_current_ambush = ambushSize;
	g_next_single_ambush_monster_spawn_time = level.time;

	return ambushSize;
}

// REPLACEMENT: TryAlternativeSpawnPosition
// This function now incorporates the Line-of-Sight (LOS) check inspired by the tank's
// spawning logic, preventing monsters from spawning behind walls relative to the
// original spawn point.
[[nodiscard]] bool TryAlternativeSpawnPosition(edict_t* spawn_point, horde::MonsterTypeID typeId, vec3_t& final_origin, vec3_t& final_angles)
{
	PROFILE_SCOPE("TryAlternativeSpawnPosition");

	// --- 1. Input Validation and Prerequisites ---
	if (!spawn_point || !spawn_point->inuse || !is_valid_vector(spawn_point->s.origin)) {
		if (developer->integer) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Invalid spawn_point parameter.\n");
		return false;
	}

	const vec3_t base_origin = spawn_point->s.origin;
	const vec3_t base_angles = spawn_point->s.angles;
	const bool is_flying = IsFlying(typeId);

	vec3_t predicted_mins, predicted_maxs;
	if (!GetPredictedScaledBounds(typeId, predicted_mins, predicted_maxs)) {
		if (developer->integer > 1) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Using fallback bounds for TypeID {}.\n", (int)typeId);
	}

	// --- 2. Helper Lambda for Core Validation Logic ---
	// This lambda encapsulates the repeated checks to keep our code DRY (Don't Repeat Yourself).
	auto check_and_set_position = 
		[&](const vec3_t& candidate_pos, const vec3_t& offset_dir) -> bool 
	{
		// 2a. Line-of-Sight Check (THE "TANK" LOGIC)
		// Trace from the original spawn point to the candidate position.
		// If it's blocked by the world (MASK_SOLID), this spot is invalid.
		trace_t los_trace = gi.traceline(base_origin, candidate_pos, spawn_point, MASK_SOLID);
		if (los_trace.fraction < 1.0f) { // Use 1.0f for a strict check
			return false; // Path is blocked by the world, this is a bad spot.
		}

		// 2b. Full Location Validation (using our refined function)
		vec3_t validated_pos = candidate_pos;
		if (IsPositionPhysicallyValid(validated_pos, predicted_mins, predicted_maxs, is_flying)) {
			
			// 2c. Final Proximity Check (to avoid clustering)
			if (!IsPositionTooCloseToRecentSpawn(validated_pos)) {
				// SUCCESS! We found a valid, visible, and non-clustered spot.
				final_origin = validated_pos;
				
				// Calculate angle to face away from the original blocked point.
				if (offset_dir.lengthSquared() > VECTOR_LENGTH_SQ_EPSILON) {
					final_angles = vectoangles(offset_dir);
					// Always zero out the pitch to prevent monsters from looking up/down on spawn.
					final_angles[PITCH] = 0; 
				} else {
					final_angles = base_angles; // Fallback to original angle if offset is zero.
				}

				MarkPositionAsRecentlyUsed(final_origin);
				return true;
			}
		}
		return false; // One of the checks failed.
	};

	// --- 3. Phase 1: Try Shuffled Predefined Offsets ---
	// This is a fast way to check common, nearby valid spots.
	auto alternative_offsets = HordeConstants::horde_alternative_positions;
	std::shuffle(alternative_offsets.begin(), alternative_offsets.end(), mt_rand);

	for (const auto& offset : alternative_offsets) {
		if (check_and_set_position(base_origin + offset, offset)) {
			if (developer->integer > 1) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Success with predefined offset {}.\n", offset);
			return true;
		}
	}

	// --- 4. Phase 2: Fallback to Radial Offsets ---
	// If predefined offsets fail, we search in a wider, random radius.
	constexpr int RADIAL_ATTEMPTS = 35;
	constexpr float MIN_RADIUS = 40.0f;
	constexpr float MAX_RADIUS = 225.0f;

	for (int i = 0; i < RADIAL_ATTEMPTS; ++i) {
		float radius = frandom(MIN_RADIUS, MAX_RADIUS);
		float angle_rad = frandom() * 2.0f * PIf;
		vec3_t offset = { cosf(angle_rad) * radius, sinf(angle_rad) * radius, frandom(-8.0f, 24.0f) };
		
		if (check_and_set_position(base_origin + offset, offset)) {
			if (developer->integer > 1) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Success with radial offset {}.\n", offset);
			return true;
		}
	}

	// --- 5. Failure ---
	if (developer->integer) {
		gi.Com_PrintFmt("TryAlternativeSpawnPosition: Failed to find any valid alternative for spawn point at {}.\n", base_origin);
	}
	return false;
}

// TypeID-based EmergencySpawnMonster
// Takes is_additional_monster to control g_totalMonstersInWave increment
// Takes champion_chance_for_this_spawn to control champion probability
bool EmergencySpawnMonster(const int32_t levelNum,
                           horde::MonsterTypeID typeId,
                           bool is_additional_monster,
                           float champion_chance_for_this_spawn)
{
    PROFILE_SCOPE("EmergencySpawnMonster");

    // --- Phase 1: Find a valid spot ---
    vec3_t emergency_origin, emergency_angles;
    bool used_human_player = false; // This isn't used after, but FindEmergency needs it.

    if (!FindEmergencySpawnPosition(emergency_origin, emergency_angles, used_human_player, typeId)) {
        if (developer->integer) {
            gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Could not find valid position for TypeID {}.\n", static_cast<int>(typeId));
        }
        return false;
    }

    // --- Phase 2: Spawn the monster ---
    edict_t* monster = SpawnMonsterByTypeID(typeId, emergency_origin, emergency_angles, true);
    if (!monster) {
        if (developer->integer) {
            gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: SpawnMonsterByTypeID failed for TypeID {}.\n", static_cast<int>(typeId));
        }
        return false;
    }

    // --- Phase 3: Apply bonuses using the new helper ---
    if (!ApplyHordeBonuses(monster, levelNum, champion_chance_for_this_spawn)) {
        // The monster was freed or became invalid during bonus application.
        if (developer->integer) {
            const char* classname = horde::MonsterTypeRegistry::GetClassname(typeId);
            gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Monster '{}' became invalid after applying bonuses.\n",
                            classname ? classname : "Unknown");
        }
        // The monster is already !inuse, so we just return failure.
        return false;
    }

    // --- Phase 4: Finalize and add effects ---
    SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
    if (sound_spawn1) {
        gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
    }

    // If this was an "additional" monster (like from an ambush), update the total count for the wave.
    if (is_additional_monster) {
        if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
            g_totalMonstersInWave++;
        }
    }

    if (developer->integer) {
        gi.Com_PrintFmt("EMERGENCY SPAWN SUCCESSFUL: Spawned '{}' (Additional: {}).\n",
                        monster->classname, is_additional_monster ? "Yes" : "No");
    }

    return true;
}

// Modified ShouldTriggerAmbushSpawn function for more frequent ambushes
bool ShouldTriggerAmbushSpawn()
{
	// Static variables for tracking time-based cooldowns

	// Only consider ambush spawning after wave 3
	if (current_wave_level < 3)
	{
		return false;
	}

	// Check global cooldown (25 seconds between ambushes)
	if (level.time - last_ambush_time < 25_sec)
	{
		return false;
	}

	// Check short-term cooldown (random 3-7 seconds between attempts)
	if (level.time < ambush_cooldown_end)
	{
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
	for (auto *player : active_players_no_spect())
	{
		if (player && player->inuse && player->health > 0)
		{
			if (player->health >= 125 || player->client->resp.spree >= 50)
			{
				playerBonus += 0.04f;
			}
			playerCount++;
		}
	}

	// Apply player bonus with cap
	if (playerCount > 0)
	{
		baseChance += std::min(playerBonus, 0.15f);
	}

	// Final chance roll
	if (frandom() < baseChance)
	{
		// Update timestamps
		last_ambush_time = level.time;
		ambush_cooldown_end = level.time + random_time(3_sec, 7_sec);
		waves_since_ambush = 0;
		return true;
	}
	else
	{
		// Set short cooldown even on failure to prevent spam checks
		ambush_cooldown_end = level.time + random_time(1_sec, 3_sec);
		return false;
	}
}

// NEW HELPER: Spawns one monster from an active ambush/retaliation batch.
static void SpawnSingleAmbushMonsterFromBatch()
{
	// Early exit if no ambush batch is active or it's not time yet.
	if (g_monsters_to_spawn_in_current_ambush <= 0 || level.time < g_next_single_ambush_monster_spawn_time)
	{
		return;
	}

	const AmbushSpawnInfo& info = g_current_ambush_info;

	if (info.typeId != horde::MonsterTypeID::UNKNOWN)
	{
		vec3_t spawn_pos, spawn_angles;
		bool used_human_player = false;
		bool position_found = false;

		// <<< FIX: Implement a two-pass spawn attempt for reliability >>>
		// --- Attempt 1: Try to spawn near the specific retaliation target ---
		if (info.is_retaliation && info.target_player) {
			position_found = FindEmergencySpawnPosition(spawn_pos, spawn_angles, used_human_player, info.typeId, info.target_player);
			if (!position_found && developer->integer) {
				gi.Com_PrintFmt("Retaliation Spawn: Failed to find spot near primary target. Trying fallback...\n");
			}
		}

		// --- Attempt 2: If the first attempt failed or it's a general ambush, try spawning near ANY player ---
		if (!position_found) {
			position_found = FindEmergencySpawnPosition(spawn_pos, spawn_angles, used_human_player, info.typeId, nullptr);
		}
		// <<< END FIX >>>

		if (position_found)
		{
			// Spawn the monster at the found position
			edict_t* monster = SpawnMonsterByTypeID(info.typeId, spawn_pos, spawn_angles, true);
			if (monster)
			{
				// Apply bonuses and effects
				if (ApplyHordeBonuses(monster, g_horde_local.level, info.champion_chance) && monster->inuse) {
					SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
					if (sound_spawn1) {
						gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
					}
					// This is an "additional" monster, so we must increment the total wave count
					if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
						g_totalMonstersInWave++;
					}
				}
			}
		}
		else if (developer->integer) {
			// This log will now only appear if both passes failed.
			gi.Com_PrintFmt("Ambush Spawn FAILED: Could not find any valid emergency spawn position on the map.\n");
		}
	}

	// Decrement the batch counter regardless of success to prevent infinite loops.
	g_monsters_to_spawn_in_current_ambush--;

	// If more monsters are left in this batch, set the timer for the next frame.
	if (g_monsters_to_spawn_in_current_ambush > 0)
	{
		g_next_single_ambush_monster_spawn_time = level.time + FRAME_TIME_MS;
	}
}

// Forward declarations for new helper functions
static void RebuildSpawnPointCacheIfNeeded();
static bool CheckHardCapAndLog(int32_t activeMonsters, int32_t hardCap, int32_t softCap, horde_state_t currentState, int32_t currentLevel);
static bool TrySpawnAmbush(const horde::MapSize &mapSize, int32_t currentLevel, int32_t activeMonsters, int32_t softCap);
static int32_t ManageSpawnCountsAndQueue(const horde::MapSize &mapSize, int32_t availableSpace);
static void DetermineSpawnStrategy(const horde::MapSize &mapSize, int32_t &out_spawnable_this_call, bool &out_use_emergency_spawn, bool &out_recovery_mode_active, float &out_champion_chance, int32_t availableSpace, int32_t currentLevel);
static bool ValidateSpawnPointForMonster(edict_t *spawn_point, const SpawnPointData &sp_data, gtime_t current_time, bool recovery_mode_active);
static edict_t *FindValidSpotAndSpawn(edict_t *spawn_point, horde::MonsterTypeID monster_type, int32_t currentLevel, float champion_chance);

static int ExecuteEmergencySpawnProcedure(int32_t spawnable_this_call,
										  int32_t currentLevel,
										  float champion_chance_param);

static bool AttemptSpawnSingleMonster(
	int32_t currentLevel_param,	 // Input: Current wave level
	float champion_chance_param, // Input: Chance for champion
	// These are passed down from SpawnMonsters, reflecting global state
	bool is_recovery_mode_active_param,
	bool is_retaliation_active_param,
	MonsterWaveType current_actual_wave_type_param,
	MonsterWaveType original_wave_type_before_recovery_param);

static int ExecuteNormalSpawnProcedure(
	int32_t spawnable_this_call, // How many monsters we're trying to spawn in this batch
	int32_t currentLevel_param,	 // Current wave level
	float champion_chance_param, // Chance for a champion

	// Context parameters to be passed down to AttemptSpawnSingleMonster
	bool is_recovery_mode_active_param,
	bool is_retaliation_active_param,
	MonsterWaveType current_actual_wave_type_param,
	MonsterWaveType original_wave_type_before_recovery_param);

//======================================================================
// MODIFIED: SpawnMonsters
// This function no longer spawns monsters directly. Instead, it INITIATES
// a time-sliced batch by setting the new global state variables.
//======================================================================
void SpawnMonsters()
{
	if (level.intermissiontime)
		return;
	if (developer->integer == 2 && g_horde->integer)
		return;

	// --- Cache Globals ---
	const horde::MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level;

	// --- 1. Spawn Point Cache Management ---
	RebuildSpawnPointCacheIfNeeded();
	if (g_potential_spawn_points.empty())
	{
		if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: No potential spawn points found.\n");
		if (g_consecutive_spawn_failures < HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY - 1) g_consecutive_spawn_failures++;
		if (!need_spawn_cache_reset)
		{
			need_spawn_cache_reset = true;
			if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: Forcing spawn point cache reset.\n");
		}
		return;
	}

	// <<< FIX: Ambush System is now checked BEFORE the hard cap. >>>
	// --- 2. Ambush System (Priority Check) ---
	const int32_t activeMonsters = CalculateRemainingMonsters();
	const int32_t softCap = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap : (mapSize.isSmallMap ? HordeConstants::MAX_MONSTERS_SMALL_MAP : (mapSize.isBigMap ? HordeConstants::MAX_MONSTERS_BIG_MAP : HordeConstants::MAX_MONSTERS_MEDIUM_MAP));
	if (TrySpawnAmbush(mapSize, currentLevel, activeMonsters, softCap))
	{
		SetNextMonsterSpawnTime(mapSize);
		return; // Ambush was triggered, end this frame's spawn logic.
	}

	// <<< FIX: Hard cap check now happens AFTER the ambush check. >>>
	// --- 3. Hard Cap Check ---
	const int32_t hardCap = static_cast<int32_t>(softCap * 1.4f);
	if (CheckHardCapAndLog(activeMonsters, hardCap, softCap, g_horde_local.state, currentLevel))
	{
		return;
	}

	// --- 4. Spawn Quota Management ---
	int32_t availableSpace = softCap - activeMonsters;
	if (availableSpace <= 0)
	{
		if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Soft monster cap reached.\n");
		return;
	}
	availableSpace = ManageSpawnCountsAndQueue(mapSize, availableSpace);
	if (g_horde_local.num_to_spawn <= 0)
	{
		if (g_horde_local.queued_monsters <= 0 && g_horde_local.state == horde_state_t::spawning && !next_wave_message_sent)
		{
			VerifyAndAdjustBots();
			gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Fully Deployed.\nWave Level: {}\n", currentLevel);
			next_wave_message_sent = true;
			g_horde_local.state = horde_state_t::active_wave;
		}
		return;
	}

	// --- 5. Spawn Strategy Determination ---
	int32_t spawnable_this_call;
	bool use_emergency_spawn_flag;
	float champion_chance_for_batch;
	DetermineSpawnStrategy(mapSize, spawnable_this_call, use_emergency_spawn_flag, g_recovery_mode_active, champion_chance_for_batch, availableSpace, currentLevel);

	// ==================================================================
	// --- 6. NEW BATCH SPAWNING LOGIC ---
	// ==================================================================

	// If we are already in the middle of spawning a batch, do nothing here.
	// The SpawnSingleMonsterFromBatch() call in Horde_RunFrame will handle it.
	if (g_monsters_to_spawn_in_current_batch > 0)
	{
		return;
	}

	// Emergency spawns are still immediate, single-frame actions.
	if (use_emergency_spawn_flag)
	{
		int num_spawned = ExecuteEmergencySpawnProcedure(spawnable_this_call, currentLevel, champion_chance_for_batch);
		if (num_spawned > 0 && g_recovery_mode_active)
		{
			if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: Exiting recovery mode (emergency success).\n");
			g_recovery_mode_active = false;
			current_wave_type = g_original_wave_type_before_recovery;
			g_original_wave_type_before_recovery = MonsterWaveType::None;
		}
	}
	else // This is the normal spawn path.
	{
		// Instead of calling a loop, we just set up the batch counter.
		if (spawnable_this_call > 0)
		{
			g_monsters_to_spawn_in_current_batch = spawnable_this_call;
			g_champion_chance_for_current_batch = champion_chance_for_batch; // Store the chance
			
			// Set the timer to immediately trigger the first spawn of the batch on the next frame.
			g_next_single_monster_spawn_time = level.time;
		}
	}

	// --- 7. Final Actions ---
	// This now sets the timer for the NEXT BATCH, not the next monster.
	SetNextMonsterSpawnTime(mapSize);
}

// --- Helper Function Implementations ---

static int ExecuteEmergencySpawnProcedure(int32_t spawnable_this_call,
										  int32_t currentLevel,
										  float champion_chance_param)
{ // Added champion_chance_param
	int emergency_spawned_count = 0;

	for (int i = 0; i < std::min(spawnable_this_call, 3); ++i)
	{ // Limit emergency spawns per call
		horde::MonsterTypeID emergency_type = (currentLevel < 10) ? horde::MonsterTypeID::SOLDIER : horde::MonsterTypeID::GUNNER;
		if (currentLevel >= 15)
			emergency_type = horde::MonsterTypeID::TANK;
		if (currentLevel >= 20)
			emergency_type = horde::MonsterTypeID::GLADIATOR;

		// Call EmergencySpawnMonster with the champion_chance_param
		if (EmergencySpawnMonster(currentLevel, emergency_type, false, champion_chance_param))
		{ // Pass champion_chance_param
			emergency_spawned_count++;
			if (g_horde_local.num_to_spawn > 0)
				--g_horde_local.num_to_spawn;
			else if (g_horde_local.queued_monsters > 0)
				--g_horde_local.queued_monsters;
		}
		else
		{
			if (developer->integer)
				gi.Com_PrintFmt("EMERGENCY SPAWN FAILED for type {}. (From ExecuteEmergencySpawnProcedure)\n", (int)emergency_type);
			break; // Stop trying if one fails in this batch
		}
	}

	if (emergency_spawned_count > 0)
	{
		g_consecutive_spawn_failures = 0; // Reset failures on any success
		// Recovery mode exit is handled in SpawnMonsters based on this success
		if (developer->integer)
			gi.Com_PrintFmt("EMERGENCY SPAWN PROCEDURE: Spawned {}.\n", emergency_spawned_count);
	}
	return emergency_spawned_count;
}

static void RebuildSpawnPointCacheIfNeeded()
{
	if (!g_spawn_points_cached || need_spawn_cache_reset)
	{
		g_potential_spawn_points.clear();
		g_potential_spawn_points.reserve(MAX_SPAWN_POINTS);
		g_cached_flying_spawn_count = 0; // This line should now work correctly

		for (auto *point : monster_spawn_points())
		{
			if (point && point->inuse && point->classname && strcmp(point->classname, "info_player_deathmatch") == 0 && is_valid_vector(point->s.origin))
			{
				if (g_potential_spawn_points.size() < MAX_SPAWN_POINTS)
				{
					g_potential_spawn_points.push_back(point);
					if (point->style == 1)
					{
						g_cached_flying_spawn_count++; // This should now work
					}
				}
				else if (developer->integer)
				{
					gi.Com_PrintFmt("RebuildSpawnPointCacheIfNeeded: Warning - Exceeded MAX_SPAWN_POINTS ({}) while caching.\n", MAX_SPAWN_POINTS);
					break;
				}
			}
		}
		if (!g_potential_spawn_points.empty())
		{
			// Shuffle the collected points
			for (size_t i = g_potential_spawn_points.size() - 1; i > 0; --i)
			{
				size_t j = irandom(0, i); // irandom should be inclusive [0, i]
				if (i != j)
				{
					std::swap(g_potential_spawn_points[i], g_potential_spawn_points[j]);
				}
			}
		}
		g_spawn_point_shuffle_index = 0;
		g_spawn_points_cached = true;
		need_spawn_cache_reset = false;
		// Reset g_consecutive_spawn_failures if cache was rebuilt, as point validity might have changed.
		// This was already present and is good.
		g_consecutive_spawn_failures = 0;
		if (developer->integer)
			gi.Com_PrintFmt("Spawn Point Cache Rebuilt: {} points ({} flying).\n", g_potential_spawn_points.size(), g_cached_flying_spawn_count);
	}
}

static bool CheckHardCapAndLog(int32_t activeMonsters, int32_t hardCap, int32_t softCap, horde_state_t currentState, int32_t currentLevel)
{
	if (activeMonsters >= hardCap)
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("SpawnMonsters: Hard cap ({}/{}), SoftCap={}, AdjustedCap={}\n", activeMonsters, hardCap, softCap, g_adjusted_monster_cap);
		if (currentState == horde_state_t::spawning && !next_wave_message_sent)
		{
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

static bool TrySpawnAmbush(const horde::MapSize &mapSize, int32_t currentLevel, int32_t activeMonsters, int32_t softCap)
{
	if (g_horde_local.state == horde_state_t::active_wave && activeMonsters < softCap && ShouldTriggerAmbushSpawn())
	{
		int spawnedCount = SpawnAmbushMonsters(mapSize, currentLevel);
		if (spawnedCount > 0)
		{
			g_consecutive_spawn_failures = 0;
			return true; // Ambush spawned
		}
	}
	return false; // No ambush or ambush failed
}

static int32_t ManageSpawnCountsAndQueue(const horde::MapSize &mapSize, int32_t availableSpace)
{
	if (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters > 0)
	{
		const int32_t base_transfer = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
		const int32_t transfer_amount = std::min({g_horde_local.queued_monsters, availableSpace, base_transfer});
		if (transfer_amount > 0)
		{
			g_horde_local.num_to_spawn += transfer_amount;
			g_horde_local.queued_monsters -= transfer_amount;
			if (developer->integer > 1)
				gi.Com_PrintFmt("SpawnMonsters: Transferred {} from queue ({} left).\n", transfer_amount, g_horde_local.queued_monsters);
			return availableSpace - transfer_amount; // Return updated available space
		}
	}
	return availableSpace;
}

static void DetermineSpawnStrategy(const horde::MapSize &mapSize, int32_t &out_spawnable_this_call, bool &out_use_emergency_spawn, bool &out_recovery_mode_active_ref, float &out_champion_chance, int32_t availableSpace, int32_t currentLevel)
{
	const int32_t base_batch = mapSize.isSmallMap ? 4 : (mapSize.isBigMap ? 6 : 5);
	out_spawnable_this_call = std::min({g_horde_local.num_to_spawn, base_batch, availableSpace});

	out_use_emergency_spawn = false;
	if (g_consecutive_spawn_failures >= HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY)
	{
		if (developer->integer)
			gi.Com_PrintFmt("EMERGENCY SPAWN TRIGGERED: Failures={}.\n", g_consecutive_spawn_failures);
		out_use_emergency_spawn = true;
	}
	else if (g_consecutive_spawn_failures >= HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY && !out_recovery_mode_active_ref)
	{
		if (developer->integer)
			gi.Com_PrintFmt("RECOVERY MODE ACTIVATED: Failures={}.\n", g_consecutive_spawn_failures);
		out_recovery_mode_active_ref = true; // Modifies g_recovery_mode_active
		g_original_wave_type_before_recovery = current_wave_type;
		current_wave_type = HasWaveType(current_wave_type, MonsterWaveType::Flying) ? MonsterWaveType::Flying : MonsterWaveType::Ground;
	}

	out_champion_chance = 0.2f;
	if (g_horde_retaliation_active)
	{
		out_champion_chance = 0.40f;
		if (developer->integer > 1)
			gi.Com_PrintFmt("Retaliation: Champion chance {:.0f}%\n", out_champion_chance * 100.0f);
	}
}

// REPLACEMENT: AttemptSpawnSingleMonster
static bool AttemptSpawnSingleMonster(
	int32_t currentLevel_param,
	float champion_chance_param,
	bool is_recovery_mode_active_param,
	bool is_retaliation_active_param,
	MonsterWaveType current_actual_wave_type_param,
	MonsterWaveType original_wave_type_before_recovery_param)
{
	const size_t total_potential_points = g_potential_spawn_points.size();
	if (total_potential_points == 0)
	{
		return false;
	}
	size_t points_checked_for_this_monster = 0;

	while (points_checked_for_this_monster < total_potential_points)
	{
		if (g_spawn_point_shuffle_index >= total_potential_points)
		{
			g_spawn_point_shuffle_index = 0;
		}
		edict_t *spawn_point = g_potential_spawn_points[g_spawn_point_shuffle_index++];
		points_checked_for_this_monster++;

		const auto &sp_data = spawnPointsData[spawn_point];
		if (!ValidateSpawnPointForMonster(spawn_point, sp_data, level.time, is_recovery_mode_active_param))
		{
			continue;
		}

		horde::MonsterTypeID monster_type_id = G_HordePickMonsterType(
			spawn_point,
			currentLevel_param,
			current_actual_wave_type_param,
			is_retaliation_active_param,
			is_recovery_mode_active_param,
			original_wave_type_before_recovery_param);

		if (monster_type_id == horde::MonsterTypeID::UNKNOWN)
		{
			IncreaseSpawnAttempts(spawn_point);
			g_consecutive_spawn_failures++;
			continue;
		}

        // --- START OF CORRECTION ---
		const bool monster_is_flying = IsFlying(monster_type_id);
		const bool is_flying_spawn_point = (spawn_point->style == 1);

		// This check is CORRECT and REMAINS. A ground-only monster cannot spawn at a 
        // flying-only point because it would fall to its death.
		if (is_flying_spawn_point && !monster_is_flying)
		{
			IncreaseSpawnAttempts(spawn_point);
			g_consecutive_spawn_failures++;
			continue;
		}

		// The second, problematic 'if' block that prevented flyers from using ground
        // points has been completely REMOVED.
        // --- END OF CORRECTION ---

		edict_t *spawned_monster_entity = FindValidSpotAndSpawn(spawn_point, monster_type_id, currentLevel_param, champion_chance_param);
		if (spawned_monster_entity)
		{
			if (spawned_monster_entity->inuse && !spawned_monster_entity->deadflag && spawned_monster_entity->health > 0)
			{
				SpawnGrow_Spawn(spawned_monster_entity->s.origin, 80.0f, 10.0f);
				if (sound_spawn1)
				{
					gi.sound(spawned_monster_entity, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
				}
			}
			else if (spawned_monster_entity->inuse && developer->integer)
			{
				gi.Com_PrintFmt("SpawnGrow_Spawn skipped in AttemptSpawnSingleMonster: Monster {} (idx {}) not fully alive. InUse:{}, DeadFlag:{}, Health:%.0f\n",
								(spawned_monster_entity->classname ? spawned_monster_entity->classname : "Unknown"),
								(int)(spawned_monster_entity - g_edicts),
								spawned_monster_entity->inuse,
								spawned_monster_entity->deadflag,
								spawned_monster_entity->health);
			}

			horde::g_monsterSpawnTracker.SetLastSpawnTime(monster_type_id, level.time);
			g_consecutive_spawn_failures = 0;

			if (is_recovery_mode_active_param)
			{
				if (developer->integer)
					gi.Com_PrintFmt("AttemptSpawnSingleMonster: Successful spawn during recovery mode. Recovery may now end.\n");
			}
			return true;
		}
	}

	if (developer->integer > 1 && total_potential_points > 0)
	{
		gi.Com_PrintFmt("AttemptSpawnSingleMonster: Failed to spawn a monster after checking {} points.\n", total_potential_points);
	}
	return false;
}

static bool ValidateSpawnPointForMonster(edict_t *spawn_point, const SpawnPointData &sp_data, gtime_t current_time, bool recovery_mode_active_param)
{
	// Check various cooldowns
	if ((sp_data.isTemporarilyDisabled && current_time < sp_data.cooldownEndsAt) ||
		(current_time < sp_data.teleport_cooldown) ||
		(current_time < sp_data.alternative_cooldown))
	{
		return false;
	}

    // The incorrect "flying compatibility" check has been removed from here.

	// Check distance to players
	for (const auto *const player : active_players_no_spect())
	{
		if ((spawn_point->s.origin - player->s.origin).lengthSquared() < HordeConstants::MIN_PLAYER_DIST_SQ_SPAWNPOINT)
		{ 
			IncreaseSpawnAttempts(spawn_point);
			g_consecutive_spawn_failures++;
			return false;
		}
	}

	// Check if point is occupied by player/monster/defense
	if (IsSpawnPointOccupied(spawn_point))
	{
		IncreaseSpawnAttempts(spawn_point);
		g_consecutive_spawn_failures++;
		return false;
	}
	return true;
}

//======================================================================
// REPLACEMENT: ApplyHordeBonuses
// More robust version with safety checks to prevent crashes if the
// monster is freed during bonus application.
//======================================================================
// REPLACEMENT: ApplyHordeBonuses (more robust)
static bool ApplyHordeBonuses(edict_t* monster, int32_t currentLevel, float champion_chance)
{
    if (!monster || !monster->inuse) return false;

    bool became_champion = false;
    if (currentLevel >= 3 && !champion_spawned_this_wave && champion_spawn_cooldown_ends_at < level.time && !monster->monsterinfo.IS_BOSS && frandom() < champion_chance)
    {
        monster->monsterinfo.bonus_flags |= BF_CHAMPION;
        ApplyMonsterBonusFlags(monster);
        if (!monster->inuse) return false; // FIX: Safety check

        monster->item = G_HordePickItem();
        monster->spawnflags = monster->item ? (monster->spawnflags & ~SPAWNFLAG_MONSTER_NO_DROP) : (monster->spawnflags | SPAWNFLAG_MONSTER_NO_DROP);
        
        champion_spawned_this_wave = true;
        champion_spawn_cooldown_ends_at = level.time + random_time(10_sec, 20_sec);
        gi.LocBroadcast_Print(PRINT_HIGH, "*** A Champion {} has appeared! ***\n", GetDisplayName(monster).c_str());
        became_champion = true;
    }

    if (!became_champion) {
        const float drop_chance = currentLevel <= 5 ? 0.8f : (currentLevel <= 8 ? 0.6f : 0.45f);
        if (frandom() < drop_chance) {
            monster->item = G_HordePickItem();
            monster->spawnflags = monster->item ? (monster->spawnflags & ~SPAWNFLAG_MONSTER_NO_DROP) : (monster->spawnflags | SPAWNFLAG_MONSTER_NO_DROP);
        } else {
            monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;
        }
    }

    if (currentLevel >= 6 && monster->monsterinfo.power_armor_type == IT_NULL && monster->monsterinfo.armor_type == IT_NULL) {
        SetMonsterArmor(monster);
        if (!monster->inuse) return false; // FIX: Safety check
    }

    return true;
}

// NEW HELPER FUNCTION - This is the high-level orchestrator for finding a valid spawn spot.
// It implements the "tank logic" with robust checks.
static bool FindValidSpawnSpot(
    edict_t* spawn_point,
    horde::MonsterTypeID monster_type,
    vec3_t& out_origin,
    vec3_t& out_angles,
    bool& out_used_alternative)
{
    // --- 1. Get Prerequisites ---
    const vec3_t base_origin = spawn_point->s.origin;
    const vec3_t base_angles = spawn_point->s.angles;
    const bool is_flying = IsFlying(monster_type);
    vec3_t predicted_mins, predicted_maxs;
    GetPredictedScaledBounds(monster_type, predicted_mins, predicted_maxs);

    // --- 2. Attempt Direct Spawn ---
    vec3_t direct_pos = base_origin;
    if (IsPositionPhysicallyValid(direct_pos, predicted_mins, predicted_maxs, is_flying)) {
        out_origin = direct_pos;
        out_angles = base_angles;
        out_used_alternative = false;
        return true;
    }

    // --- 3. Attempt Alternative Spawn (if direct spawn failed) ---
    auto alternative_offsets = HordeConstants::horde_alternative_positions;
    std::shuffle(alternative_offsets.begin(), alternative_offsets.end(), mt_rand);

    static const vec3_t trace_box = {-4, -4, -4}; // A small box for our "fat" trace

    for (const auto& offset : alternative_offsets) {
        vec3_t candidate_pos = base_origin + offset;

        // Step A: "Fat" Line-of-Sight Check from the original point to the candidate.
        trace_t los_trace = gi.trace(base_origin, trace_box, trace_box, candidate_pos, spawn_point, MASK_SOLID);
        if (los_trace.fraction < 1.0f) {
            continue; // Path is blocked by world geometry.
        }

        // Step B: Physical Validity Check of the candidate position itself.
        if (IsPositionPhysicallyValid(candidate_pos, predicted_mins, predicted_maxs, is_flying)) {
            out_origin = candidate_pos;
            out_angles = vectoangles(offset); // Face away from the original blocked point
            out_angles[PITCH] = 0;
            out_used_alternative = true;
            return true;
        }
    }

    return false; // All attempts failed.
}

// REPLACEMENT: FindValidSpotAndSpawn (Now uses the new helper system)
// This function replaces the old TryAlternativeSpawnPosition.
static edict_t* FindValidSpotAndSpawn(edict_t* spawn_point, horde::MonsterTypeID monster_type, int32_t currentLevel, float champion_chance)
{
    vec3_t final_origin, final_angles;
    bool used_alternative = false;

    // --- Phase 1: Find a valid spot using our new high-level orchestrator ---
    if (!FindValidSpawnSpot(spawn_point, monster_type, final_origin, final_angles, used_alternative)) {
        // Finding a spot failed completely. Penalize the spawn point.
        if (used_alternative) {
            ApplyAlternativePositionCooldown(spawn_point);
        } else {
            IncreaseSpawnAttempts(spawn_point);
        }
        g_consecutive_spawn_failures++;
        return nullptr;
    }

    // --- Phase 2: Spawn the monster at the validated location ---
    edict_t* monster = SpawnMonsterByTypeID(monster_type, final_origin, final_angles, true);
    if (!monster) {
        if (used_alternative) ApplyAlternativePositionCooldown(spawn_point);
        else IncreaseSpawnAttempts(spawn_point);
        g_consecutive_spawn_failures++;
        return nullptr;
    }

    // --- Phase 3: Apply bonuses and finalize ---
    if (ApplyHordeBonuses(monster, currentLevel, champion_chance)) {
        // Success! The monster is live. Apply the correct cooldown.
        if (used_alternative) {
            ApplySuccessfulAlternativeCooldown(spawn_point);
        } else {
            OnSuccessfulSpawn(spawn_point);
        }
        return monster;
    } else {
        // Bonuses were applied, but the monster became invalid (was freed).
        if (developer->integer > 1) {
            gi.Com_PrintFmt("FindValidSpotAndSpawn: Monster became invalid after applying bonuses. Class: {}\n",
                            horde::MonsterTypeRegistry::GetClassname(monster_type));
        }
        g_consecutive_spawn_failures++;
        return nullptr;
    }
}

static void SetMonsterArmor(edict_t *monster)
{
	// Input validation and exception for specific monster types ---
	if (!monster || !monster->inuse || !monster->classname)
	{
		return;
	}

	// Get the monster's unique type ID from its classname
	const horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(monster->classname);

	// Check if this monster type should be excluded from getting armor
	if (typeId == horde::MonsterTypeID::MUTANT ||
		typeId == horde::MonsterTypeID::REDMUTANT ||
		typeId == horde::MonsterTypeID::GEKK)
	{
		// These monsters are fast, agile, or swarming units. Giving them armor
		// can be unbalanced, so they are explicitly excluded from this bonus.
		return;
	}
	// --- END OF ADDED CODE ---


	// Cache frequently used constants to avoid recalculating
	static constexpr float HEALTH_RATIO_POW = 1.1f;
	static constexpr float SIZE_FACTOR_POW = 0.7f;
	static constexpr float MASS_FACTOR_POW = 0.6f;
	static constexpr float BASE_ARMOR = 75.0f;
	static constexpr float MAX_HEALTH_ARMOR_FACTOR = 0.2f;

	// Get spawn temp once
	const spawn_temp_t &st = ED_GetSpawnTemp();

	// Assign default armor type if not specified
	if (!st.was_key_specified("armor_power") && !st.was_key_specified("power_armor_power"))
	{
		monster->monsterinfo.armor_type = IT_ARMOR_COMBAT;
	}

	// Calculate base factors once
	const float health_ratio = monster->health / static_cast<float>(monster->max_health);
	const float size_factor = (monster->maxs - monster->mins).length() / 64.0f;
	const float mass_factor = std::min(monster->mass / 200.0f, 1.5f);

	// Pre-compute level scaling factor
	float level_scaling;
	// Use if-else instead of switch for better optimization with constants
	if (current_wave_level <= 15)
	{
		level_scaling = 1.0f + (current_wave_level * 0.04f);
	}
	else if (current_wave_level <= 25)
	{
		level_scaling = 1.6f + ((current_wave_level - 15) * 0.06f);
	}
	else
	{
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
	float armor_multiplier = current_wave_level <= 24 ? 0.4f : current_wave_level <= 30 ? 0.5f
																						: 0.6f;
	armor_power *= armor_multiplier;

	// Difficulty adjustment
	if (g_insane->integer)
	{
		armor_power *= 1.25f;
	}
	else if (g_chaotic->integer)
	{
		armor_power *= 1.2f;
	}

	// Apply random factor - use faster random generation
	const float random_factor = 0.9f + (0.2f * frandom());
	armor_power *= random_factor;

	// High-level bonus
	if (current_wave_level > 25)
	{
		armor_power *= 1.0f + ((current_wave_level - 25) * 0.03f);
	}

	// Calculate min/max armor values efficiently
	const int min_armor = std::max(25, static_cast<int>(monster->max_health * 0.08f));
	const int max_armor = static_cast<int>(monster->max_health *
										   (current_wave_level > 25 ? 1.5f : 1.2f));

	// Clamp final armor value
	const int final_armor = std::clamp(static_cast<int>(armor_power), min_armor, max_armor);

	// Assign power directly based on armor type
	if (monster->monsterinfo.power_armor_type == IT_NULL)
	{
		monster->monsterinfo.power_armor_power = 0;
	}
	else if (monster->monsterinfo.power_armor_type == IT_ITEM_POWER_SHIELD ||
			 monster->monsterinfo.power_armor_type == IT_ITEM_POWER_SCREEN)
	{
		monster->monsterinfo.power_armor_power = final_armor;
	}

	// Assign armor power
	if (monster->monsterinfo.armor_type == IT_NULL)
	{
		monster->monsterinfo.armor_power = 0;
	}
	else if (monster->monsterinfo.armor_type == IT_ARMOR_COMBAT)
	{
		monster->monsterinfo.armor_power = final_armor;
	}
}

static void SetNextMonsterSpawnTime(const horde::MapSize &mapSize)
{
	// Original spawn time ranges (Big maps are faster)
	constexpr std::array<std::pair<gtime_t, gtime_t>, 3> BASE_SPAWN_TIMES = {{
		{0.6_sec, 0.8_sec}, // Small maps
		{0.8_sec, 1.0_sec}, // Medium maps
		{0.4_sec, 0.6_sec}	// Big maps
	}};

	// Apply early wave modifier (waves 1-10) - slows down spawn rate initially
	float earlyWaveMultiplier = 1.0f;
	// Check level is valid before calculation
	if (g_horde_local.level >= 1 && g_horde_local.level <= 10)
	{
		// Linearly decreases interval multiplier from 2.0x at wave 1 down to 1.1x at wave 10.
		// After wave 10, the multiplier is 1.0x (no modification).
		earlyWaveMultiplier = 2.0f - ((static_cast<float>(g_horde_local.level) - 1.0f) * 0.1f);
		// Clamp just in case level somehow goes outside 1-10 despite the check
		earlyWaveMultiplier = std::clamp(earlyWaveMultiplier, 1.1f, 2.0f);
	}

	// Select base times based on map size
	const size_t mapIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);
	const auto &[base_min_time, base_max_time] = BASE_SPAWN_TIMES[mapIndex];

	// Apply the early wave multiplier to get the target time range
	const gtime_t min_time = gtime_t::from_sec(base_min_time.seconds() * earlyWaveMultiplier);
	const gtime_t max_time = gtime_t::from_sec(base_max_time.seconds() * earlyWaveMultiplier);

	// Calculate the random interval within the adjusted range
	// Ensure min_time <= max_time before calling random_time to avoid potential issues
	const gtime_t calculated_interval = (min_time <= max_time) ? random_time(min_time, max_time) : min_time; // Fallback to min_time if somehow inverted

	// --- CLAMPING ---
	// Ensure the final interval is never less than the absolute minimum allowed
	const gtime_t final_interval = std::max(calculated_interval, HordeConstants::MIN_MONSTER_SPAWN_INTERVAL);

	// Set the time for the next spawn attempt
	g_horde_local.monster_spawn_time = level.time + final_interval;

	//// Optional Debugging
	// if (developer->integer > 2) {
	//	gi.Com_PrintFmt("SetNextMonsterSpawnTime: Level={}, MapIdx={}, EarlyMult={:.2f}, Base=[{:.2f}-{:.2f}], Adjusted=[{:.2f}-{:.2f}], Calculated={:.2f}, Final={:.2f}\n",
	//		g_horde_local.level,
	//		mapIndex,
	//		earlyWaveMultiplier,
	//		base_min_time.seconds(), base_max_time.seconds(),
	//		min_time.seconds(), max_time.seconds(),
	//		calculated_interval.seconds(),
	//		final_interval.seconds());
	// }
}
// Usar enum class para mejorar la seguridad de tipos
enum class MessageType
{
	Standard,
	Chaotic,
	Insane
};

void CalculateTopDamager(PlayerStats &topDamager, float &percentage)
{
	constexpr int32_t MAX_DAMAGE = 100000;
	int32_t total_damage = 0;
	topDamager = PlayerStats();	  // Reset stats
	topDamager.total_damage = -1; // Use -1 to ensure first valid player is picked

	for (const auto &player : active_players())
	{
		if (!player || !player->client || !player->inuse)
			continue;

		const int32_t player_damage = static_cast<int32_t>(std::min(
			static_cast<int32_t>(player->client->total_damage),
			MAX_DAMAGE));

		if (player_damage > 0)
		{
			total_damage += player_damage;
		}

		if (player_damage > topDamager.total_damage)
		{
			topDamager.total_damage = player_damage;
			topDamager.player = player;
		}
	}

	// Reset damage if it was initialized to -1 and no one dealt damage
	if (topDamager.total_damage == -1)
	{
		topDamager.total_damage = 0;
	}

	// Calculate percentage (same as before)
	percentage = 0.0f;
	if (total_damage > 0 && topDamager.total_damage > 0)
	{
		percentage = std::min(
			(static_cast<float>(topDamager.total_damage) / total_damage) * 100.0f,
			100.0f);
		percentage = std::round(percentage * 100.0f) / 100.0f;
	}
}

struct RewardInfo
{
	item_id_t item_id;
	int weight; // Higher weight = more common

	// Optional: Add constructor for cleaner initialization below
	constexpr RewardInfo(item_id_t id, int w) : item_id(id), weight(w) {}
};

// Define the rewards ONLY ONCE in this array
static const std::array<RewardInfo, 3> TOP_DAMAGER_REWARDS = {{
	{IT_ITEM_BANDOLIER, 60}, // More common
	{IT_ITEM_SENTRYGUN, 30}, // Less common
	{IT_AMMO_NUKE, 2}		 // More common
}};

// Pre-computed total reward weight, calculated directly from the single array
static const int TOTAL_REWARD_WEIGHT = []
{
	int total = 0;
	// Calculate sum directly from the array
	for (const auto &reward : TOP_DAMAGER_REWARDS)
	{
		// Ensure weight is positive to avoid issues
		if (reward.weight > 0)
		{
			total += reward.weight;
		}
	}
	// Return 1 if total is somehow zero to prevent division by zero later
	return (total > 0) ? total : 1;
}(); // Immediately invoke the lambda

static bool GiveTopDamagerReward(const PlayerStats &topDamager, const std::string &playerName)
{
	// Quick validation with early return
	if (!topDamager.player || !topDamager.player->inuse || !topDamager.player->client)
		return false;

	const int roll = irandom(1, TOTAL_REWARD_WEIGHT);
	item_id_t selectedItemId = TOP_DAMAGER_REWARDS[0].item_id; // Default fallback to the first item
	int currentWeight = 0;

	for (const auto &reward : TOP_DAMAGER_REWARDS)
	{
		if (reward.weight <= 0)
			continue; // Skip items with no weight

		currentWeight += reward.weight;
		if (roll <= currentWeight)
		{
			selectedItemId = reward.item_id;
			break; // Found the item
		}
	}

	// Get item by ID
	gitem_t *item = GetItemByIndex(selectedItemId);
	if (!item || !item->classname)
	{
		// Log error if item ID is invalid or item has no classname
		if (developer->integer)
		{
			gi.Com_PrintFmt("Error: GiveTopDamagerReward - Failed to get valid gitem_t for ID {}\n", static_cast<int>(selectedItemId));
		}
		return false;
	}

	// Spawn and give item directly to player
	edict_t *entity = G_Spawn();
	if (!entity)
		return false;

	entity->classname = item->classname;
	entity->item = item;
	SpawnItem(entity, item, spawn_temp_t::empty);

	// Check if SpawnItem succeeded (entity might be freed if e.g., item shouldn't spawn in DM)
	if (!entity->inuse)
	{
		// SpawnItem already freed it, just return false
		return false;
	}

	// Give item to player
	Touch_Item(entity, topDamager.player, null_trace, true);
	// Touch_Item might free the entity if pickup is successful
	if (entity->inuse)
	{
		G_FreeEdict(entity); // Free if Touch_Item didn't consume it
	}

	// Announce reward
	const char *itemName = item->use_name ? item->use_name : (item->classname ? item->classname : "reward");

	gi.LocBroadcast_Print(PRINT_HIGH, "{} receives a {} for top damage!\n",
						  playerName.empty() ? "Unknown Player" : playerName.c_str(),
						  itemName);

	return true;
}

static void SendCleanupMessage(WaveEndReason reason)
{
	// Avoid try-catch for performance in normal operation
	// Wave completion message - use switch for better performance
	switch (reason)
	{
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
	if (topDamager.player && topDamager.player->inuse && topDamager.player->client)
	{
		// Get player name once
		const std::string playerName = GetPlayerName(topDamager.player);

		// Send damage announcement
		gi.LocBroadcast_Print(PRINT_HIGH, "{} dealt the most damage with {}! ({}% of total)\n",
							  playerName.c_str(), topDamager.total_damage, static_cast<int>(percentage));

		// Give reward and reset stats if successful
		if (GiveTopDamagerReward(topDamager, playerName))
		{
			// Reset all player stats in one pass using iterator
			for (auto *player : active_players())
			{
				if (player && player->client)
				{
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
void CheckAndResetDisabledSpawnPoints()
{
	// Find all active spawn points that are disabled
	for (uint32_t i = 1; i < globals.num_edicts; i++)
	{
		edict_t *ent = &g_edicts[i];
		if (ent && ent->inuse && ent->classname &&
			strcmp(ent->classname, "info_player_deathmatch") == 0)
		{

			auto &data = spawnPointsData[ent];
			if (data.isTemporarilyDisabled)
			{
				// Simply reset the disabled status
				data.isTemporarilyDisabled = false;
				data.attempts = 0;
				data.cooldownEndsAt = 0_sec;
			}
		}
	}
}

//======================================================================
// NEW HELPER FUNCTION: SpawnSingleMonsterFromBatch
// This function attempts to spawn exactly ONE monster from an active batch
// and is the core of the time-slicing solution.
//======================================================================
static void SpawnSingleMonsterFromBatch()
{
	// Early exit if we are not currently processing a batch or it's not time yet.
	if (g_monsters_to_spawn_in_current_batch <= 0 || level.time < g_next_single_monster_spawn_time)
	{
		return;
	}

	// Attempt to spawn one monster. We reuse your existing robust function for this.
	// The context parameters are passed in from the main game state.
	bool success = AttemptSpawnSingleMonster(
		g_horde_local.level,
		g_champion_chance_for_current_batch, // Use the stored chance for this batch
		g_recovery_mode_active,
		g_horde_retaliation_active,
		current_wave_type,
		g_original_wave_type_before_recovery
	);

	if (success)
	{
		// A monster was successfully spawned and processed.
		// Decrement the main spawn pool that the game state logic uses.
		if (g_horde_local.num_to_spawn > 0)
		{
			g_horde_local.num_to_spawn--;
		}
		monsters_spawned_in_current_phase++;
	}
	// If it failed, AttemptSpawnSingleMonster already handled incrementing g_consecutive_spawn_failures.

	// Decrement the batch counter regardless of success or failure. This is crucial
	// to prevent infinite loops if spawning fails repeatedly.
	g_monsters_to_spawn_in_current_batch--;

	// If there are more monsters left to spawn in this batch, set the timer for the next frame.
	if (g_monsters_to_spawn_in_current_batch > 0)
	{
		// Use a constant frame time to ensure one spawn attempt per frame.
		g_next_single_monster_spawn_time = level.time + FRAME_TIME_MS;
	}
}

// REPLACEMENT: Horde_RunFrame (with both time-slicing workers)
void Horde_RunFrame() {
	if (level.intermissiontime) {
		return;
	}

	// --- Time-slice regular spawns ---
	SpawnSingleMonsterFromBatch();
    // --- Time-slice ambush/retaliation spawns ---
	SpawnSingleAmbushMonsterFromBatch();

	// --- Cache Frequently Used Variables ---
	const gtime_t currentTime = level.time;
	const horde::MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level;

	// --- Pre-State-Machine Maintenance ---
	CleanupSpawnPointCache();
	CheckAndReduceSpawnCooldowns();

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
	static int32_t wave_at_last_check = 0;
	constexpr gtime_t WAVE_STUCK_TIMEOUT = 3_min;

	if (wave_at_last_check != currentLevel) {
		last_wave_change_time = currentTime;
		wave_at_last_check = currentLevel;
	} else if (g_horde_local.state != horde_state_t::warmup && currentTime > last_wave_change_time + WAVE_STUCK_TIMEOUT) {
		if (GetStroggsNum() == 0) {
			if (developer->integer) {
				gi.Com_PrintFmt("CRITICAL: Wave {} stuck for over {}s with 0 monsters. Forcing progression.\n",
					currentLevel, WAVE_STUCK_TIMEOUT.seconds());
			}
			g_horde_local.state = horde_state_t::cleanup;
			g_horde_local.monster_spawn_time = currentTime;
		} else {
			last_wave_change_time = currentTime;
		}
	}

	bool waveEnded = false;
	WaveEndReason currentWaveEndReason = WaveEndReason::AllMonstersDead;

	// --- STATE MACHINE ---
	switch (g_horde_local.state) {
		case horde_state_t::warmup:
			if (g_horde_local.warm_time < currentTime) {
				g_horde_local.state = horde_state_t::spawning;
				Horde_InitLevel(1);
				PlayWaveStartSound();
				DisplayWaveMessage();
			}
			break;

		case horde_state_t::spawning: {
			static gtime_t spawning_phase_timeout_start_time = 0_sec;
			static int32_t prev_wave_level_for_spawning_timers = -1;

			if (currentLevel != prev_wave_level_for_spawning_timers) {
				spawning_phase_timeout_start_time = currentTime;
				prev_wave_level_for_spawning_timers = currentLevel;
				initial_total_monsters_for_spawning_phase_timeout = g_horde_local.num_to_spawn + g_horde_local.queued_monsters;
				monsters_spawned_in_current_phase = 0;
			}

			if (currentTime > spawning_phase_timeout_start_time + 90_sec) {
				if (!next_wave_message_sent) {
					gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Deployment Finalized (Timeout).\nWave Level: {}\n", currentLevel);
					next_wave_message_sent = true;
				}
				g_horde_local.state = horde_state_t::active_wave;
				break;
			}

			if (g_horde_local.monster_spawn_time <= currentTime) {
				if (IsBossWave() && !boss_spawned_for_wave) {
					SpawnBossAutomatically();
				}
				
				if (!IsBossWave() || boss_spawned_for_wave) {
					SpawnMonsters();
				}

				if (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters <= 0) {
					if (!IsBossWave() || boss_spawned_for_wave) {
						if (!next_wave_message_sent) {
							VerifyAndAdjustBots();
							gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Fully Deployed.\nWave Level: {}\n", currentLevel);
							next_wave_message_sent = true;
						}
						g_horde_local.state = horde_state_t::active_wave;
					}
				}
				SetNextMonsterSpawnTime(mapSize);
			}
			break;
		}

		case horde_state_t::active_wave:
			if (CheckRemainingMonstersCondition(mapSize, currentWaveEndReason)) {
				waveEnded = true;
				break;
			}
			if (g_horde_local.queued_monsters > 0 && g_horde_local.num_to_spawn == 0) {
				const int32_t activeMonsters = CalculateRemainingMonsters();
				const int32_t softCap = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap : HordeConstants::MAX_MONSTERS_MEDIUM_MAP;
				int32_t availableSpace = softCap - activeMonsters;
				if (availableSpace > 0) {
					const int32_t transfer_batch = mapSize.isSmallMap ? 4 : (mapSize.isBigMap ? 8 : 6);
					int32_t transfer_amount = std::min({g_horde_local.queued_monsters, availableSpace, transfer_batch});
					if (transfer_amount > 0) {
						g_horde_local.num_to_spawn += transfer_amount;
						g_horde_local.queued_monsters -= transfer_amount;
					}
				}
			}
			if (g_horde_local.num_to_spawn > 0 && g_horde_local.monster_spawn_time <= currentTime) {
				SpawnMonsters();
				SetNextMonsterSpawnTime(mapSize);
			}
			break;

		case horde_state_t::cleanup:
			if (g_horde_local.monster_spawn_time < currentTime) {
				HandleWaveCleanupMessage(mapSize, currentWaveEndReason);
				g_horde_local.warm_time = currentTime + random_time(0.8_sec, 1.5_sec);
				g_horde_local.state = horde_state_t::rest;
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

	if (waveEnded) {
		SendCleanupMessage(currentWaveEndReason);
		g_horde_local.monster_spawn_time = currentTime + 0.5_sec;
		g_horde_local.state = horde_state_t::cleanup;
		ResetWaveAdvanceState();
	}

	if (horde_message_end_time != 0_sec && currentTime >= horde_message_end_time) {
		ClearHordeMessage();
	}
}

// Función para manejar el evento de reinicio
void HandleResetEvent()
{
	ResetGame();
}

// Get the remaining time for the current wave
gtime_t GetWaveTimer()
{
	const gtime_t currentTime = level.time;
	
	// Calculate the time remaining on the absolute wave timer.
	const gtime_t independentRemaining = (g_independent_timer_start + g_lastParams.independentTimeThreshold) - currentTime;

	// If the mop-up timer is active, we need to decide which is shorter.
	if (g_horde_local.conditionTriggered && g_horde_local.waveEndTime > currentTime)
	{
		const gtime_t conditionalRemaining = g_horde_local.waveEndTime - currentTime;
		// Return the shorter of the two timers.
		return std::min(independentRemaining, conditionalRemaining);
	}

	// Otherwise, just return the main wave timer.
	return (independentRemaining > 0_sec) ? independentRemaining : 0_sec;
}

// Helper functionget stroggs alive on the map
int32_t GetStroggsNum() noexcept
{
	return level.total_monsters - level.killed_monsters;
}

// Helper function to check if it's a boss wave
inline bool IsBossWave() noexcept
{
	return g_horde_local.level >= 10 && g_horde_local.level % 5 == 0;
}

// Helper to get predicted *scaled* bounds for validation checks
// Returns false if typeId is invalid or info not found
// MODIFIED: GetPredictedScaledBounds to use LUT
bool GetPredictedScaledBounds(horde::MonsterTypeID typeId, vec3_t &out_mins, vec3_t &out_maxs)
{
	if (!g_monster_info_lut_initialized)
	{ // Should have been called in Horde_Init
		InitializeMonsterInfoLUT();
		if (!g_monster_info_lut_initialized && developer->integer)
		{ // Still not initialized?
			gi.Com_PrintFmt("GetPredictedScaledBounds: CRITICAL - Monster Info LUT not initialized on demand!\n");
		}
	}

	const MonsterTypeInfo *info = nullptr;
	float scale = 1.0f;

	if (typeId != horde::MonsterTypeID::UNKNOWN &&
		typeId < horde::MonsterTypeID::MAX_TYPES && // Basic bounds check for enum direct use
		static_cast<size_t>(typeId) < g_monster_info_lut.size())
	{
		info = g_monster_info_lut[static_cast<size_t>(typeId)];
	}

	if (info)
	{
		scale = info->s_scale;
	}
	else
	{
		out_mins = HordeConstants::VALIDATE_CHECK_MINS;
		out_maxs = HordeConstants::VALIDATE_CHECK_MAXS;
		if (developer->integer)
			gi.Com_PrintFmt("GetPredictedScaledBounds: WARN - MonsterTypeInfo not found or invalid TypeID {}, using generic bounds.\n", static_cast<int>(typeId));
		return false;
	}

	out_mins = info->default_mins * scale;
	out_maxs = info->default_maxs * scale;

	if (scale <= 0.0f)
	{ // Check for invalid scale
		if (developer->integer)
			gi.Com_PrintFmt("GetPredictedScaledBounds: WARN - MonsterTypeID {} has invalid scale %.2f. Using unscaled default bounds.\n", static_cast<int>(typeId), scale);
		out_mins = info->default_mins; // Revert to unscaled if scale is bad
		out_maxs = info->default_maxs;
	}
	return true;
}

bool Horde_TeleportMonster(edict_t *self, const vec3_t &destination_origin, const vec3_t &destination_angles, bool play_effects, bool force_despite_visibility = false)
{
	PROFILE_SCOPE("Horde_TeleportMonster");
	if (level.intermissiontime)
	{
		return false;
	}

	if (!self || !self->inuse || self->deadflag || !is_valid_vector(destination_origin) || !is_valid_vector(destination_angles))
	{
		if (developer->integer > 1 && self)
		{
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
		 (self->enemy && self->enemy->inuse && visible(self, self->enemy, false))))
	{
		constexpr gtime_t VISIBLE_ENEMY_CANCEL_COOLDOWN = 1.5_sec;
		self->teleport_time = level.time + VISIBLE_ENEMY_CANCEL_COOLDOWN;
		if (developer->integer > 1)
		{
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
	if (!GetPredictedScaledBounds(monsterTypeId, predicted_mins, predicted_maxs))
	{
		// Warning logged by GetPredictedScaledBounds
	}

	if (!IsPositionPhysicallyValid(final_pos_after_validation, predicted_mins, predicted_maxs, is_flying_monster))
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
		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("Horde_TeleportMonster: IsPositionPhysicallyValid failed for {} at intended ({:.1f},{:.1f},{:.1f}). Restored to ({:.1f},{:.1f},{:.1f}).\n",
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

	if (play_effects)
	{
		// MODIFIED GUARD
		if (self->inuse && !self->deadflag && self->health > 0)
		{
			SpawnGrow_Spawn(self->s.origin, 80.0f, 10.0f);
			if (sound_spawn1)
			{
				gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
			}
		}
		else if (self->inuse && developer->integer > 1)
		{
			gi.Com_PrintFmt("SpawnGrow_Spawn (teleport effect) skipped: Monster {} (idx {}) not fully alive. DeadFlag:{}, Health:%.0f\n",
							(self->classname ? self->classname : "Unknown"),
							(int)(self - g_edicts),
							self->deadflag,
							self->health);
		}
	}

	self->monsterinfo.was_stuck = false;
	self->monsterinfo.stuck_check_time = 0_sec;
	self->monsterinfo.react_to_damage_time = level.time;
	self->teleport_time = level.time + random_time(HordeConstants::MIN_TELEPORT_COOLDOWN_MONSTER, HordeConstants::MAX_TELEPORT_COOLDOWN_MONSTER);

	if (developer->integer > 1)
	{
		gtime_t time_until_next_teleport = self->teleport_time - level.time;
		gi.Com_PrintFmt("Horde_TeleportMonster: Successfully teleported {} to ({:.1f},{:.1f},{:.1f}). Next teleport possible in {:.1f}s.\n",
						self->classname ? self->classname : "UNKNOWN",
						self->s.origin.x, self->s.origin.y, self->s.origin.z,
						time_until_next_teleport.seconds());
	}
	return true;
}

// --- MODIFIED ---
// This function now contains the main JIT (Just-In-Time) precaching logic.
// It runs before each wave to load only the assets needed for that specific wave.
static void Horde_InitLevel(const int32_t lvl)
{
	// --- 1. Reset All Wave-Specific State ---
	ResetSpawnBatchState();
	g_special_high_level_monster_spawned_this_wave = false;
	g_horde_retaliation_active = false;
	g_horde_retaliation_end_time = 0_sec;
	g_horde_retaliation_target_player = nullptr;
	ResetChampionMonsterState();
   	waves_since_ambush++;

	// --- 2. Set up the new wave's parameters ---
	g_horde_local.level = lvl;
	current_wave_level = lvl;

	// Determine the wave type. Boss waves start with no type; it's set when the boss spawns.
	if (!(lvl >= 10 && lvl % 5 == 0)) {
		InitializeWaveType(lvl);
	} else {
		current_wave_type = MonsterWaveType::None;
	}

	// --- 3. Build the eligible monster cache for this wave (CRITICAL OPTIMIZATION) ---
	g_eligible_monsters_for_wave.clear();
	g_eligible_monsters_for_wave.reserve(std::size(monsterTypes));

    // The monsterTypes array is sorted by minWave, which makes this efficient.
	for (const auto& monster : monsterTypes) {
		// Stop iterating once we pass the current wave level.
		if (monster.minWave > current_wave_level) {
			break;
		}
		// Check if the monster's flags match the requirements for this wave.
		if (IsValidMonsterForWave(monster.typeId, current_wave_type)) {
			g_eligible_monsters_for_wave.push_back(&monster);
		}
	}

	if (developer->integer) {
		gi.Com_PrintFmt("Horde_InitLevel: Built cache with {} eligible monsters for wave {}.\n",
						g_eligible_monsters_for_wave.size(), current_wave_level);
	}

	// --- 4. JIT PRECACHE LOGIC ---
    // Iterate through the monsters for THIS wave and precache any that are new.
    if (developer->integer) {
        gi.Com_PrintFmt("Horde_InitLevel (Wave {}): Checking for monsters to precache...\n", lvl);
    }
	for (const MonsterTypeInfo* monster_info : g_eligible_monsters_for_wave)
	{
        // Check if this monster type has already been loaded.
		if (g_precached_monster_types.find(monster_info->typeId) == g_precached_monster_types.end())
		{
			const char* classname = horde::MonsterTypeRegistry::GetClassname(monster_info->typeId);
			if (classname && *classname)
			{
				if (developer->integer) {
					gi.Com_PrintFmt("JIT Precache: Loading assets for '{}' for wave {}.\n", classname, lvl);
				}
				edict_t* temp_monster = G_Spawn();
				if (temp_monster) {
					temp_monster->classname = classname;
					// *** CRITICAL FIX: Add this flag to prevent precaching from affecting monster counts. ***
					temp_monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

					ED_CallSpawn(temp_monster);
					if (temp_monster->inuse) {
						G_FreeEdict(temp_monster);
					}
                    // Add to the set so we don't load it again.
					g_precached_monster_types.insert(monster_info->typeId);
				}
			}
		}
	}
    if (developer->integer) {
        gi.Com_PrintFmt("JIT Precache: All necessary monsters for wave {} are now in memory.\n", lvl);
    }

	// --- 5. Continue with the rest of the wave setup ---
	g_horde_local.update_map_size(GetCurrentMapName());
	GetAdjustedMonsterCap(g_horde_local.current_map_size, lvl);

	g_independent_timer_start = level.time;
	last_wave_number++;
	boss_spawned_for_wave = false;
	next_wave_message_sent = false;

	CleanupSpawnPointCache();
	VerifyAndAdjustBots();
	G_UpdateActiveLasersForWaveProgression(current_wave_level);

	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTriggered = false;
	g_horde_local.waveEndTime = 0_sec;
	g_horde_local.lastPrintTime = 0_sec;
	g_lastParams = GetConditionParams(g_horde_local.current_map_size, GetNumHumanPlayers(), lvl);

	for (size_t i = 0; i < WARNING_TIMES.size(); i++) {
		g_horde_local.warningIssued[i] = false;
	}

	UnifiedAdjustSpawnRate(g_horde_local.current_map_size, lvl, GetNumHumanPlayers());

	int32_t total_planned_for_wave = g_horde_local.num_to_spawn + g_horde_local.queued_monsters;
	g_totalMonstersInWave = static_cast<uint16_t>(
		std::min(total_planned_for_wave, static_cast<int32_t>(std::numeric_limits<uint16_t>::max())));

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
	default:
		break;
	}

	if (developer->integer) {
		gi.Com_PrintFmt("Horde_InitLevel: Wave {}. num_to_spawn: {}, queued: {}. Total for wave: {}\n",
						lvl, g_horde_local.num_to_spawn, g_horde_local.queued_monsters, g_totalMonstersInWave);
	}

	CheckAndApplyBenefit(lvl);
	ResetCooldowns();
	Horde_CleanBodies();
}