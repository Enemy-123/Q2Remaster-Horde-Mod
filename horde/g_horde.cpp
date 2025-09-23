// Includes y definiciones relevantes
#include "../shared.h"
#include "g_horde_phys.h"
#include "../g_local.h"
#include "g_horde.h"
#include <set>
#include <algorithm>  // For std::count_if
#include "g_horde_benefits.h"
#include "horde_ids.h"
#include <span>
#include "g_laser.h"
#include "../profiler.h"
#include <cassert>
#include "horde_performance.h"

// This represents the maximum possible level boost for the "elite spawn" mechanic.
// It ensures we precache and consider all potential elite monsters for a given wave.
constexpr int32_t MAX_EFFECTIVE_LEVEL_BOOST = 20;

// Maps an edict_t* to a compact index [0...N-1]
static std::unordered_map<int, uint16_t> g_spawn_point_map;


std::unordered_map<int, trap_state_t> g_trap_states;
std::unordered_map<int, EmitterState> g_emitter_states;

//aiming for special entities for idview.cpp
std::vector<edict_t*> g_targetable_special_entities;
// Provides a direct list of spawn point edicts for easy iteration
std::vector<edict_t*> g_spawn_point_list;
// The actual number of spawn points found on the map
size_t g_num_spawn_points = 0;

static bool g_spawn_map_needs_build = true;

// *** NEW: Use std::unordered_map instead of a giant static array ***
static std::unordered_map<int, gtime_t> last_boss_teleport_attempt_time; 

// Forward declaration for the new map-building function
static void BuildSpawnPointMap();

struct SpawnPlanEntry
{
	horde::MonsterTypeID typeId;
	edict_t *spawn_point;
};

static std::array<bool, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_precached_monster_types_flags = {}; // Initializes all to false
static bool g_full_precache_done = false;

static bool monsters_precached = false;
MonsterWaveType current_wave_type = MonsterWaveType::None;
std::vector<const MonsterTypeInfo *> g_eligible_monsters_for_wave;
std::vector<size_t> g_eligible_item_indices_for_wave;

// --- Asset Family System Implementation ---
std::array<MonsterAssetFamily, static_cast<size_t>(AssetFamilyID::MAX_FAMILIES)> g_asset_families;
std::array<AssetFamilyID, 128> g_monster_to_family; // 128 = MAX_TYPES from horde_ids.h
std::unordered_set<std::string> g_precached_models;
std::unordered_set<std::string> g_precached_sounds;
int32_t g_total_precached_models = 0;
int32_t g_total_precached_sounds = 0;

// CVars for precaching control
static cvar_t* g_horde_precache_mode = nullptr;  // 0=JIT, 1=Smart Window, 2=Full
static cvar_t* g_horde_precache_window = nullptr; // How many waves ahead to precache
static cvar_t* g_horde_precache_debug = nullptr;  // Show debug info about precaching

int32_t monsters_spawned_in_current_phase = 0;
int32_t initial_total_monsters_for_spawning_phase_timeout = 0;


// UNIFIED BATCH & PLAN VARIABLES
static std::vector<SpawnPlanEntry> g_spawn_plan;
static float g_champion_chance_for_current_batch = 0.2f;

// Ambush/Retaliation still needs its own simple state, as it doesn't use spawn points


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
// REMOVED: Using g_special_spawn_state.type == SpecialSpawnType::Retaliation instead

// --- NEW: UNIFIED SPECIAL SPAWN SYSTEM ---
enum class SpecialSpawnType {
	None,
	Ambush,
	Retaliation
};

struct SpecialSpawnState {
	SpecialSpawnType type = SpecialSpawnType::None;
	int32_t remaining_count = 0;
	horde::MonsterTypeID monster_type_id = horde::MonsterTypeID::UNKNOWN;
	float champion_chance = 0.0f;
	edict_t* target_player = nullptr;

	void clear() {
		type = SpecialSpawnType::None;
		remaining_count = 0;
		monster_type_id = horde::MonsterTypeID::UNKNOWN;
		champion_chance = 0.0f;
		target_player = nullptr;
	}
};
static SpecialSpawnState g_special_spawn_state;

static gtime_t g_horde_retaliation_end_time = 0_sec;
static gtime_t g_horde_retaliation_last_trigger_time = 0_sec;  // Cooldown tracking
// Optional: Store the targeted player edict for focus (can be nullptr)
static edict_t *g_horde_retaliation_target_player = nullptr;

// Ambush system tracking variables
static gtime_t last_ambush_time = 0_sec;
static gtime_t ambush_cooldown_end = 0_sec;
static int32_t waves_since_ambush = 0;
// static bool ambush_system_initialized = false;

// --- Recent Spawn Position Tracking ---
struct RecentSpawnsSoA
{
	std::array<vec3_t, 32> positions;		 // Tightly packed array of vectors
	std::array<gtime_t, 32> cooldowns_until; // Tightly packed array of times
};
static constexpr size_t MAX_RECENT_POSITIONS = 32; // History for TryAlternativeSpawnPosition
static RecentSpawnsSoA g_recent_spawns;
static size_t g_recent_spawn_index = 0; // Renamed from g_recent_position_index for clarity

// --- Recent Teleport Position Tracking (SoA) ---
static constexpr int MAX_RECENT_TELEPORT_LOCATIONS = 8;

struct RecentTeleportsSoA
{
	std::array<vec3_t, MAX_RECENT_TELEPORT_LOCATIONS> positions;
	std::array<gtime_t, MAX_RECENT_TELEPORT_LOCATIONS> teleport_times;
};

static RecentTeleportsSoA g_recent_teleports;
static size_t g_recent_teleport_index = 0;

namespace HordeConstants
{
	// --- Gameplay Balance ---
	constexpr float TIME_REDUCTION_MULTIPLIER = 0.95f;
	constexpr float BASE_DIFFICULTY_MULTIPLIER = 1.1f;
	constexpr float PLAYER_COUNT_SCALE = 0.2f;

	// --- Monster Counts & Caps ---
	constexpr int8_t MAX_MONSTERS_BIG_MAP = 26;
	constexpr int8_t MAX_MONSTERS_MEDIUM_MAP = 14;
	constexpr int8_t MAX_MONSTERS_SMALL_MAP = 12;

	constexpr std::array<std::array<int32_t, 4>, 3> BASE_COUNTS = {{
		{{4, 6, 8, 9}},    // Small maps
		{{6, 9, 11, 12}},  // Medium maps
		{{11, 14, 18, 20}} // Large maps
	}};
	constexpr std::array<int32_t, 3> ADDITIONAL_SPAWNS = {6, 5, 9};

	// --- Spawn Point Cooldowns ---
	constexpr gtime_t MIN_GLOBAL_SPAWN_COOLDOWN = 1.5_sec;
	constexpr gtime_t MIN_INDIVIDUAL_SUCCESS_COOLDOWN = 0.5_sec;
	constexpr gtime_t MIN_INDIVIDUAL_FAILURE_COOLDOWN = 0.5_sec;
	constexpr gtime_t MIN_REDUCED_INDIVIDUAL_COOLDOWN = 0.5_sec;
	constexpr gtime_t SPAWN_POINT_INACTIVITY_RESET_THRESHOLD = 6.0_sec;
	constexpr gtime_t SPAWN_POINT_TELEPORT_COOLDOWN = 3.5_sec;

	// --- Alternative Spawn Position Cooldowns & Logic ---
	constexpr gtime_t ALT_SPAWN_COOLDOWN_SHORT = 1.5_sec;
	constexpr gtime_t ALT_SPAWN_COOLDOWN_MEDIUM = 3.0_sec;
	constexpr gtime_t MIN_ALT_SUCCESS_COOLDOWN = 3.0_sec;
	constexpr gtime_t MIN_ALT_FAILURE_COOLDOWN = 1.0_sec;
	constexpr size_t NUM_HORDE_ALT_POSITIONS = 24;
	constexpr std::array<vec3_t, NUM_HORDE_ALT_POSITIONS> horde_alternative_positions = { /* ... your positions ... */ };

	// --- Monster Spawning Timing ---
	constexpr gtime_t MIN_MONSTER_SPAWN_INTERVAL = 0.8_sec;
	constexpr std::array<std::pair<gtime_t, gtime_t>, 3> BASE_SPAWN_TIMES = {{
		{0.6_sec, 0.8_sec}, // Small maps
		{0.8_sec, 1.0_sec}, // Medium maps
		{0.4_sec, 0.6_sec}	// Big maps
	}};

	// --- Base Stuck Monster / Teleport Logic ---
	constexpr gtime_t NO_DAMAGE_TIMEOUT_BASE = 32_sec;
	constexpr gtime_t DAMAGED_MONSTER_INACTIVITY_TIMEOUT_BASE = 15.0_sec;
	constexpr gtime_t NO_ENEMY_TIMEOUT_BASE = 12.0_sec;
	constexpr gtime_t MIN_TELEPORT_COOLDOWN_MONSTER = 12_sec;
	constexpr gtime_t MAX_TELEPORT_COOLDOWN_MONSTER = 20_sec;
	constexpr gtime_t GLOBAL_TELEPORT_RESET_INTERVAL = 12_sec;
	constexpr int MAX_TELEPORTS_PER_INTERVAL = 2;
	inline int g_teleport_rate_count = 0;
	inline gtime_t g_teleport_rate_reset_time = level.time;

	// --- Map-Size-Aware Timeout Functions ---
	inline gtime_t GetNoDamageTimeout(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 45_sec; // Increased from 32_sec for small maps
		} else if (mapSize.isMediumMap) {
			return 38_sec; // Slightly increased for medium maps
		} else {
			return NO_DAMAGE_TIMEOUT_BASE; // Keep original for big maps
		}
	}

	inline gtime_t GetDamagedMonsterTimeout(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 20_sec; // Increased from 15_sec for small maps
		} else if (mapSize.isMediumMap) {
			return 17_sec; // Slightly increased for medium maps
		} else {
			return DAMAGED_MONSTER_INACTIVITY_TIMEOUT_BASE; // Keep original for big maps
		}
	}

	inline gtime_t GetNoEnemyTimeout(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 15_sec; // Increased from 12_sec for small maps
		} else if (mapSize.isMediumMap) {
			return 13_sec; // Slightly increased for medium maps
		} else {
			return NO_ENEMY_TIMEOUT_BASE; // Keep original for big maps
		}
	}

	// --- Base Proximity / Distance Checks ---
	constexpr vec3_t VALIDATE_CHECK_MINS = {-16, -16, -24};
	constexpr vec3_t VALIDATE_CHECK_MAXS = {16, 16, 32};
	constexpr float MIN_PLAYER_DIST_GENERATE_BASE = 200.0f;
	constexpr float MIN_PLAYER_DIST_CHECK_BASE = 360.0f;
	constexpr float MIN_PLAYER_DIST_SPAWNPOINT_BASE = 150.0f;
	constexpr float MIN_RECENT_SPAWN_DIST_BASE = 120.0f;
	constexpr float MIN_RECENT_TELEPORT_DIST_BASE = 300.0f;

	// --- Map-Size-Aware Distance Functions ---
	inline float GetMinPlayerDistGenerate(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 120.0f; // Reduced from 200.0f for small maps
		} else if (mapSize.isMediumMap) {
			return 160.0f; // Slightly reduced for medium maps
		} else {
			return MIN_PLAYER_DIST_GENERATE_BASE; // Keep original for big maps
		}
	}

	inline float GetMinPlayerDistCheck(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 220.0f; // Reduced from 360.0f for small maps
		} else if (mapSize.isMediumMap) {
			return 290.0f; // Slightly reduced for medium maps
		} else {
			return MIN_PLAYER_DIST_CHECK_BASE; // Keep original for big maps
		}
	}

	inline float GetMinPlayerDistSpawnpoint(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 100.0f; // Reduced from 150.0f for small maps
		} else if (mapSize.isMediumMap) {
			return 125.0f; // Slightly reduced for medium maps
		} else {
			return MIN_PLAYER_DIST_SPAWNPOINT_BASE; // Keep original for big maps
		}
	}

	inline float GetMinRecentSpawnDist(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 80.0f; // Reduced from 120.0f for small maps
		} else if (mapSize.isMediumMap) {
			return 100.0f; // Slightly reduced for medium maps
		} else {
			return MIN_RECENT_SPAWN_DIST_BASE; // Keep original for big maps
		}
	}

	inline float GetMinRecentTeleportDist(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 200.0f; // Reduced from 300.0f for small maps
		} else if (mapSize.isMediumMap) {
			return 250.0f; // Slightly reduced for medium maps
		} else {
			return MIN_RECENT_TELEPORT_DIST_BASE; // Keep original for big maps
		}
	}

	// Legacy constants kept for backward compatibility - use getter functions for map-size-aware values
	constexpr float MIN_PLAYER_DIST_SQ_SPAWNPOINT = MIN_PLAYER_DIST_SPAWNPOINT_BASE * MIN_PLAYER_DIST_SPAWNPOINT_BASE;
	constexpr float MIN_RECENT_SPAWN_DIST_SQ = MIN_RECENT_SPAWN_DIST_BASE * MIN_RECENT_SPAWN_DIST_BASE;
	constexpr float MIN_RECENT_TELEPORT_DIST_SQ = MIN_RECENT_TELEPORT_DIST_BASE * MIN_RECENT_TELEPORT_DIST_BASE;

	constexpr gtime_t RECENT_SPAWN_COOLDOWN = 5.0_sec;
	constexpr gtime_t RECENT_TELEPORT_COOLDOWN = 20.0_sec;

	// --- Failure Recovery ---
	constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY = 5;
	constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY = 10;

	// --- Wave Timing ---
	constexpr gtime_t BOSS_TIME_BONUS = 100_sec;
	constexpr int MONSTERS_FOR_AGGRESSIVE_REDUCTION = 5;
	constexpr gtime_t WAVE_STUCK_TIMEOUT = 3_min;
	constexpr std::array<float, 3> WAVE_END_WARNING_TIMES = {30.0f, 10.0f, 5.0f};

	// --- Ambush & Retaliation System ---
	constexpr gtime_t RETALIATION_DURATION = 12_sec;
	constexpr gtime_t RETALIATION_COOLDOWN = 30_sec;  // Prevent retaliation from triggering too frequently
	constexpr int32_t MIN_DEATHS_FOR_RETALIATION = 12;
	constexpr float   MIN_SPAWN_PROGRESS_FOR_RETALIATION = 0.25f;  // Increased from 0.05f (5%) to 0.25f (25%)
	constexpr uint16_t MIN_SPAWNED_FOR_RETALIATION = 15;
	constexpr gtime_t AMBUSH_GLOBAL_COOLDOWN = 25_sec;
	constexpr gtime_t AMBUSH_MIN_ATTEMPT_INTERVAL = 1_sec;
	constexpr gtime_t AMBUSH_MAX_ATTEMPT_INTERVAL = 3_sec;
	constexpr float   AMBUSH_BASE_CHANCE = 0.08f;
	constexpr float   AMBUSH_CHANCE_PER_WAVE = 0.03f;

} // namespace HordeConstants // namespace HordeConstants

// --- Forward Declarations ---
bool Horde_AttemptToUnstickMonster(edict_t* self); 
bool FindEmergencySpawnPositionNearPlayer(vec3_t &out_position, vec3_t &out_angles, horde::MonsterTypeID typeId, edict_t *specific_target = nullptr);
static void Horde_InitLevel(const int32_t lvl);
static bool ApplyHordeBonuses(edict_t *monster, int32_t currentLevel, float champion_chance); // monster bonuses
void CalculateTopDamager(PlayerStats &topDamager, float &percentage);
[[nodiscard]] bool IsPositionPhysicallyValid(vec3_t &io_position, const vec3_t &monster_mins, const vec3_t &monster_maxs, bool is_flying);
bool CheckAndTeleportStuckMonster(edict_t *self);
bool FindEmergencySpawnPosition(vec3_t &position, vec3_t &angles, bool &used_human_player, horde::MonsterTypeID typeId, edict_t *specific_target = nullptr);
bool TryAlternativeSpawnPosition(edict_t *spawn_point, horde::MonsterTypeID typeId, vec3_t &final_origin, vec3_t &final_angles);
edict_t *SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t &origin, const vec3_t &angles, bool link_immediately = true);
static void AnnounceIncomingWave(gtime_t duration = 3_sec);
bool EmergencySpawnMonster(const int32_t levelNum,horde::MonsterTypeID typeId,bool is_additional_monster,float champion_chance_for_this_spawn);
static edict_t *FindBestPlayerTargetForTeleport();

static edict_t* Horde_SpawnMonster(
    const vec3_t& origin,
    const vec3_t& angles,
    horde::MonsterTypeID monster_type,
    int32_t currentLevel,
    float champion_chance);


// class EmergencySpawnOptimizer;
// static EmergencySpawnOptimizer g_emergency_spawn_optimizer;

// class MonsterSpawnPipeline;
// static MonsterSpawnPipeline g_monster_spawn_pipeline;



// High-performance position tracking with SIMD-friendly layout and reduced checks
struct alignas(32) RecentPositions {
    // Pack data for better cache usage - process 4 positions at once
    static constexpr size_t BATCH_SIZE = 4;
    static constexpr size_t MAX_POSITIONS = 32;
    static constexpr size_t NUM_BATCHES = MAX_POSITIONS / BATCH_SIZE;
    
    // SoA layout  for SIMD operations
    alignas(32) std::array<float, MAX_POSITIONS> x_coords;
    alignas(32) std::array<float, MAX_POSITIONS> y_coords; 
    alignas(32) std::array<float, MAX_POSITIONS> z_coords;
    alignas(32) std::array<gtime_t, MAX_POSITIONS> expiry_times;
    
    size_t current_index = 0;
    gtime_t last_cleanup_time = 0_sec;
    
    void AddPosition(const vec3_t& position, gtime_t duration) {
        const size_t idx = current_index;
        
        x_coords[idx] = position.x;
        y_coords[idx] = position.y;
        z_coords[idx] = position.z;
        expiry_times[idx] = level.time + duration;
        
        current_index = (current_index + 1) % MAX_POSITIONS;
        
        // Cleanup expired entries periodically (every 2 seconds)
        if (level.time - last_cleanup_time > 2_sec) {
            CleanupExpired();
            last_cleanup_time = level.time;
        }
    }
    
    bool IsPositionTooClose(const vec3_t& test_pos, float min_dist_sq) const {
        const gtime_t current_time = level.time;
        const float test_x = test_pos.x;
        const float test_y = test_pos.y;
        const float test_z = test_pos.z;
        
        // Process positions in batches for better cache usage
        for (size_t batch = 0; batch < NUM_BATCHES; ++batch) {
            const size_t base_idx = batch * BATCH_SIZE;
            
            // Check 4 positions at once - compiler can vectorize this
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                const size_t idx = base_idx + i;
                
                // Early exit if expired (most common case)
                if (expiry_times[idx] <= current_time) continue;
                
                // Calculate squared distance without sqrt
                const float dx = test_x - x_coords[idx];
                const float dy = test_y - y_coords[idx];
                const float dz = test_z - z_coords[idx];
                const float dist_sq = dx*dx + dy*dy + dz*dz;
                
                if (dist_sq < min_dist_sq) {
                    return true;
                }
            }
        }
        return false;
    }
    
private:
    void CleanupExpired() {
        const gtime_t current_time = level.time;
        
        // Mark expired entries by setting their expiry to 0
        for (size_t i = 0; i < MAX_POSITIONS; ++i) {
            if (expiry_times[i] <= current_time && expiry_times[i] != 0_sec) {
                expiry_times[i] = 0_sec;
            }
        }
    }
};

// Global instances with better memory layout
static RecentPositions g_recent_spawns_opt;
static RecentPositions g_recent_teleports_opt;

//  interface functions
inline void MarkPositionAsRecentlyUsed(const vec3_t& position) {
    g_recent_spawns_opt.AddPosition(position, HordeConstants::RECENT_SPAWN_COOLDOWN);
}

inline bool IsPositionTooCloseToRecentSpawn(const vec3_t& position, const horde::MapSize& mapSize) {
    const float min_dist = HordeConstants::GetMinRecentSpawnDist(mapSize);
    return g_recent_spawns_opt.IsPositionTooClose(position, min_dist * min_dist);
}

inline void MarkPositionAsRecentlyTeleported(const vec3_t& position) {
    g_recent_teleports_opt.AddPosition(position, HordeConstants::RECENT_TELEPORT_COOLDOWN);
}

inline bool IsPositionTooCloseToRecentTeleport(const vec3_t& position, const horde::MapSize& mapSize) {
    const float min_dist = HordeConstants::GetMinRecentTeleportDist(mapSize);
    return g_recent_teleports_opt.IsPositionTooClose(position, min_dist * min_dist);
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

struct SpawnPointsSoA
{
	// --- HOT DATA ---
	std::vector<bool> isTemporarilyDisabled;
	std::vector<gtime_t> cooldownEndsAt;
	std::vector<gtime_t> alternative_cooldown;
	std::vector<gtime_t> teleport_cooldown;

	// --- COLD DATA ---
	std::vector<gtime_t> lastSpawnTime;
	std::vector<uint16_t> attempts;
	std::vector<int32_t> successfulSpawns;
	std::vector<uint16_t> alternative_attempts;
	std::vector<bool> needs_long_alternative_cooldown;

	// Helper to resize all vectors at once
	void resize(size_t new_size)
	{
		isTemporarilyDisabled.assign(new_size, false);
		cooldownEndsAt.assign(new_size, 0_sec);
		alternative_cooldown.assign(new_size, 0_sec);
		teleport_cooldown.assign(new_size, 0_sec);
		lastSpawnTime.assign(new_size, 0_sec);
		attempts.assign(new_size, 0);
		successfulSpawns.assign(new_size, 0);
		alternative_attempts.assign(new_size, 0);
		needs_long_alternative_cooldown.assign(new_size, false);
	}

	// Helper to clear all data
	void clear()
	{
		resize(0);
	}
};

// The single global instance that replaces the old `spawnPointsData`
static SpawnPointsSoA g_spawnPointsData;

void ApplyAlternativePositionCooldown(edict_t *spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;

	const uint16_t index = g_spawn_point_map.at(spawn_point->s.number);

	g_spawnPointsData.alternative_attempts[index]++;
	gtime_t cooldown_duration;
	const uint16_t alt_attempts = g_spawnPointsData.alternative_attempts[index];

	if (alt_attempts <= 2)
		cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_SHORT;
	else if (alt_attempts <= 5)
		cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_MEDIUM;
	else
	{
		cooldown_duration = 5.0_sec + gtime_t::from_sec(0.5f * (alt_attempts - 5));
		cooldown_duration = std::min(cooldown_duration, 10.0_sec);
		if (alt_attempts >= 8)
			g_spawnPointsData.needs_long_alternative_cooldown[index] = true;
	}

	const gtime_t final_alt_duration = std::max(cooldown_duration, HordeConstants::MIN_ALT_FAILURE_COOLDOWN);
	g_spawnPointsData.alternative_cooldown[index] = level.time + final_alt_duration;

	g_spawnPointsData.isTemporarilyDisabled[index] = true;
	const gtime_t final_normal_duration = std::max(final_alt_duration * 0.5f, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
	g_spawnPointsData.cooldownEndsAt[index] = level.time + final_normal_duration;
	g_spawnPointsData.lastSpawnTime[index] = level.time;

// 	if (developer->integer)
// 		gi.Com_PrintFmt("Alternative position cooldown applied to spawn at ({:.1f}, {:.1f}, {:.1f}): {:.1f}s (attempts: {})\n",
// 						spawn_point->s.origin.x, spawn_point->s.origin.y, spawn_point->s.origin.z,
// 						final_alt_duration.seconds(), alt_attempts);
}

void IncreaseSpawnAttempts(edict_t *spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;

	const uint16_t index = g_spawn_point_map.at(spawn_point->s.number);

	if (level.time - g_spawnPointsData.lastSpawnTime[index] > HordeConstants::SPAWN_POINT_INACTIVITY_RESET_THRESHOLD)
	{
		g_spawnPointsData.attempts[index] = 0;
		g_spawnPointsData.isTemporarilyDisabled[index] = false;
		g_spawnPointsData.cooldownEndsAt[index] = 0_sec;
		g_spawnPointsData.lastSpawnTime[index] = level.time;
		return;
	}

	g_spawnPointsData.attempts[index]++;

	const uint16_t current_attempts = g_spawnPointsData.attempts[index];
	const int32_t current_successes = g_spawnPointsData.successfulSpawns[index];
	const float success_rate = (current_attempts > 0) ? (static_cast<float>(current_successes) / current_attempts) : 1.0f;

	const int max_attempts = 4 + (success_rate >= 0.5f ? 2 : (success_rate >= 0.25f ? 1 : 0));

	gtime_t calculated_duration = 0_sec;

	if (current_attempts >= max_attempts)
	{
		g_spawnPointsData.isTemporarilyDisabled[index] = true;
		const float cooldown_factor = success_rate < 0.3f ? 1.5f : 0.75f;
		const float attempt_multiplier = current_attempts <= 8 ? current_attempts * 0.25f : 2.0f;
		calculated_duration = gtime_t::from_sec(cooldown_factor * attempt_multiplier);
		if (developer->integer == 1)
			gi.Com_PrintFmt("SpawnPoint at {} inactivated for adaptive cooldown.\n", spawn_point->s.origin);
	}
	else if ((current_attempts & 1) == 0)
	{
		calculated_duration = gtime_t::from_sec(0.2f * current_attempts);
	}

	if (calculated_duration > 0_sec)
	{
		const gtime_t final_duration = std::max(calculated_duration, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
		g_spawnPointsData.cooldownEndsAt[index] = level.time + final_duration;
	}
	g_spawnPointsData.lastSpawnTime[index] = level.time;
}
//s
void OnSuccessfulSpawn(edict_t *spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;

	const uint16_t index = g_spawn_point_map.at(spawn_point->s.number);

	g_spawnPointsData.successfulSpawns[index]++;
	g_spawnPointsData.attempts[index] = 0;
	g_spawnPointsData.isTemporarilyDisabled[index] = false;
	g_spawnPointsData.cooldownEndsAt[index] = level.time + HordeConstants::MIN_INDIVIDUAL_SUCCESS_COOLDOWN;

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
	std::vector<SpawnPointCache> data;

	// Access operator now uses the compact index map
	SpawnPointCache& operator[](const edict_t* ent) {
		// First check if map needs building
		if (g_spawn_map_needs_build) {
			gi.Com_PrintFmt("WARNING: Accessing spawn point cache before BuildSpawnPointMap() - building now\n");
			BuildSpawnPointMap();
			g_spawn_map_needs_build = false;
		}

		auto it = g_spawn_point_map.find(ent->s.number);
		if (it == g_spawn_point_map.end()) {
			gi.Com_PrintFmt("ERROR: Spawn point {} not found in map!\n", ent->s.number);

			// Debug assertion to catch this during development
			assert(false && "spawn point not found in map!");

			// Robust fallback: return reference to static default cache
			static SpawnPointCache default_cache{};
			return default_cache;
		}

		// Additional safety check
		if (it->second >= data.size()) {
			gi.Com_PrintFmt("ERROR: Invalid spawn point index {} (size: {})\n", it->second, data.size());
			gi.Com_PrintFmt("DEBUG: Map built? {} Spawn points: {}\n", !g_spawn_map_needs_build, g_num_spawn_points);
			assert(false && "invalid spawn point index!");
			static SpawnPointCache default_cache{};
			return default_cache;
		}

		return data[it->second];
	}
	
	const SpawnPointCache& operator[](const edict_t* ent) const {
		// Note: const version can't modify g_spawn_map_needs_build, so this is a warning only
		if (g_spawn_map_needs_build) {
			gi.Com_PrintFmt("ERROR: Const access to spawn point cache before BuildSpawnPointMap()\n");
		}

		auto it = g_spawn_point_map.find(ent->s.number);
		if (it == g_spawn_point_map.end()) {
			gi.Com_PrintFmt("ERROR: Spawn point {} not found in map!\n", ent->s.number);

			// Debug assertion to catch this during development
			assert(false && "spawn point not found in map!");

			// Robust fallback: return reference to static default cache
			static const SpawnPointCache default_cache{};
			return default_cache;
		}

		// Additional safety check
		if (it->second >= data.size()) {
			gi.Com_PrintFmt("ERROR: Invalid spawn point index {} (size: {})\n", it->second, data.size());
			gi.Com_PrintFmt("DEBUG: Map built? {} Spawn points: {}\n", !g_spawn_map_needs_build, g_num_spawn_points);
			assert(false && "invalid spawn point index!");
			static const SpawnPointCache default_cache{};
			return default_cache;
		}

		return data[it->second];
	}

	void resize(size_t new_size) {
		data.assign(new_size, {});
	}

	void clear() {
		data.clear();
	}
};;


struct CachedSpawnPointData {
    uint16_t index;
    gtime_t last_validation_time;
    bool last_validation_result;
    vec3_t last_validation_origin;

    // Reset cache when position changes
    void InvalidateIfMoved(const vec3_t& current_origin) {
        if ((current_origin - last_validation_origin).lengthSquared() > 1.0f) {
            last_validation_time = 0_sec;
        }
    }
};

// Static vector cache using compact indices - faster than unordered_map
static std::vector<CachedSpawnPointData> g_spawn_validation_cache;
static SpawnPointCacheArray spawn_point_cache;

static void BuildSpawnPointMap()
{
	g_spawn_point_map.clear();
	g_spawn_point_list.clear();

	// Clear and rebuild spatial index
	HordePerf::g_spawn_spatial_index.Clear();

	// Reserve capacity to avoid repeated allocations
	g_spawn_point_list.reserve(64); // Most maps have fewer than 64 spawn points

	// Single pass to build both the list and the map
	for (edict_t* sp : monster_spawn_points()) {
		if (sp && sp->inuse && sp->classname && strcmp(sp->classname, "info_player_deathmatch") == 0) {
			// Add to map first, using the current size of the list as the compact index
			g_spawn_point_map[sp->s.number] = static_cast<uint16_t>(g_spawn_point_list.size());
			// Then add the pointer to the list
			g_spawn_point_list.push_back(sp);
			// Add to spatial index for fast spatial queries
			HordePerf::g_spawn_spatial_index.AddSpawnPoint(sp);
		}
	}

	g_num_spawn_points = g_spawn_point_list.size();

	// Shrink to fit to release any excess capacity
	g_spawn_point_list.shrink_to_fit();

	// Finally, resize all data structures to the exact size needed
	g_spawnPointsData.resize(g_num_spawn_points);
	spawn_point_cache.resize(g_num_spawn_points);
	g_spawn_validation_cache.resize(g_num_spawn_points);

	if (developer->integer) {
		gi.Com_PrintFmt("Spawn Point Map Built: Found {} spawn points with spatial index.\n", g_num_spawn_points);
	}
}

// A dedicated struct to pass data to our unified BoxEdicts lambda.
// This is clearer than reusing a generic struct.
struct OccupiedCheckData
{
	const edict_t *ignore_ent; // The entity to ignore in the check (usually the monster being spawned)
	SpawnPointCache *cache;	   // A pointer to the cache entry we need to modify
};

[[nodiscard]] bool IsSpawnPointOccupied(const edict_t *spawn_point, const edict_t *ignore_ent)
{
	if (!spawn_point || !spawn_point->inuse || !is_valid_vector(spawn_point->s.origin))
	{
		if (developer->integer)
			gi.Com_PrintFmt("Warning: IsSpawnPointOccupied called with invalid spawn_point or origin.\n");
		return true;
	}

	SpawnPointCache &cache = spawn_point_cache[spawn_point];
	static constexpr auto CACHE_DURATION = 100_ms;

	if ((level.time - cache.frame_time) < FRAME_TIME_MS * 2 &&
		level.time - cache.last_check_time < CACHE_DURATION &&
		cache.last_check_origin == spawn_point->s.origin)
	{
		return cache.was_occupied_by_player;
	}

	cache.last_check_time = level.time;
	cache.frame_time = level.time;
	cache.last_check_origin = spawn_point->s.origin;
	cache.was_occupied_by_player = false;
	cache.has_obstacle = false;

	static constexpr float OCCUPANCY_CHECK_SCALE = 1.75f;
	static constexpr vec3_t check_mins = vec3_t{-16, -16, -24} * OCCUPANCY_CHECK_SCALE;
	static constexpr vec3_t check_maxs = vec3_t{16, 16, 32} * OCCUPANCY_CHECK_SCALE;
	const vec3_t absolute_mins = spawn_point->s.origin + check_mins;
	const vec3_t absolute_maxs = spawn_point->s.origin + check_maxs;

	OccupiedCheckData check_data = {ignore_ent, &cache};

	gi.BoxEdicts(absolute_mins, absolute_maxs, nullptr, 0, AREA_SOLID, [](edict_t *ent, void *data) -> BoxEdictsResult_t
	{
		auto *cd = static_cast<OccupiedCheckData *>(data);
		if (ent == cd->ignore_ent) return BoxEdictsResult_t::Skip;
		if (ent->client && ent->inuse) {
			cd->cache->was_occupied_by_player = true;
			return BoxEdictsResult_t::End;
		}
		if ((ent->svflags & SVF_MONSTER && !ent->deadflag) || IsPlayerDefense(ent)) {
			cd->cache->has_obstacle = true;
		}
		return BoxEdictsResult_t::Skip;
	}, &check_data);

	return cache.was_occupied_by_player;
}

template <typename TFilter>
edict_t* SelectNextShuffledSpawnPoint(TFilter filter)
{
	if (g_potential_spawn_points.empty()) {
		return nullptr;
	}

	const size_t num_potential = g_potential_spawn_points.size();
	for (size_t i = 0; i < num_potential; ++i)
	{
		if (g_spawn_point_shuffle_index >= num_potential) {
			g_spawn_point_shuffle_index = 0; // Cycle back to the start
		}
		edict_t* spawnPoint = g_potential_spawn_points[g_spawn_point_shuffle_index++];

		const uint16_t index = g_spawn_point_map.at(spawnPoint->s.number);

		if (!spawnPoint || !spawnPoint->inuse || !is_valid_vector(spawnPoint->s.origin) ||
			(g_spawnPointsData.isTemporarilyDisabled[index] && level.time < g_spawnPointsData.cooldownEndsAt[index]) ||
			(level.time < g_spawnPointsData.alternative_cooldown[index]))
		{
			continue;
		}

		if (!filter(spawnPoint)) {
			continue;
		}

		if (IsSpawnPointOccupied(spawnPoint)) {
			if (developer->integer > 2)
				gi.Com_PrintFmt("SelectNextShuffled: Point #{} at {} skipped (player occupied).\n", index, spawnPoint->s.origin);
			continue;
		}

		// If we reach here, the point is valid.
		return spawnPoint;
	}

	return nullptr; // No valid point found in the entire shuffled list
}


[[nodiscard]] std::optional<edict_t*> FindRandomHordeSpawnPoint(bool for_flying_monster)
{
	edict_t* spawnPoint = SelectNextShuffledSpawnPoint([&](const edict_t* sp) {
		return (for_flying_monster == (sp->style == 1));
	});

	if (spawnPoint) {
		// Check if the point is usable for alternative spawns if the main spot is blocked
		const SpawnPointCache& cache = spawn_point_cache[spawnPoint];
		if (cache.has_obstacle) {
			// This point is blocked by a monster/defense, but we can try an alternative position nearby.
			// The caller will handle this logic.
		}
		return spawnPoint;
	}

	return std::nullopt;
}
static void CleanupSpawnPointCache() {
	// Reset cache entries but preserve allocated size
	for (auto& cache : spawn_point_cache.data) {
		cache = {};  // Reset to default state
	}
}

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

static const char *GetCurrentMapName()
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

int32_t g_adjusted_monster_cap = 0;

bool allowWaveAdvance = false;		// Global variable to control wave advancement
bool boss_spawned_for_wave = false; // to avoid boss spamming

int16_t last_wave_number = 0;		// Reducido de uint64_t
uint16_t g_totalMonstersInWave = 0; // Reducido de uint32_t

gtime_t horde_message_end_time = 0_sec;
gtime_t SPAWN_POINT_COOLDOWN = 2.8_sec; // spawns Cooldown

void CheckAndReduceSpawnCooldowns()
{
	if (GetStroggsNum() > 6 || IsBossWave()) {
		return;
	}

	bool found_cooldowns_to_reset = false;
	const gtime_t current_time = level.time;
	constexpr float REDUCTION_FACTOR = 0.4f;

	// Iterate using the compact list of actual spawn points
	for (size_t index = 0; index < g_num_spawn_points; ++index)
	{
		if (g_spawnPointsData.isTemporarilyDisabled[index] && current_time < g_spawnPointsData.cooldownEndsAt[index])
		{
			found_cooldowns_to_reset = true;

			const gtime_t remaining_time = g_spawnPointsData.cooldownEndsAt[index] - current_time;
			const gtime_t reduced_duration = remaining_time * REDUCTION_FACTOR;
			const gtime_t final_duration = std::max(reduced_duration, HordeConstants::MIN_REDUCED_INDIVIDUAL_COOLDOWN);

			g_spawnPointsData.cooldownEndsAt[index] = current_time + final_duration;
			g_spawnPointsData.attempts[index] = 0;
		}
	}

	if (found_cooldowns_to_reset)
	{
		SPAWN_POINT_COOLDOWN *= REDUCTION_FACTOR;
		SPAWN_POINT_COOLDOWN = std::max(SPAWN_POINT_COOLDOWN, HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN);

		// if (developer->integer > 1)
		// {
		// 	// FIX: Explicitly cast the result to a standard float to satisfy the
		// 	// compile-time (consteval) format string validation.
		// 	gi.Com_PrintFmt("Global spawn cooldown reduced and clamped to {:.2f}s\n", static_cast<float>(SPAWN_POINT_COOLDOWN.seconds()));
		// }
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
static inline int32_t CalculateChaosInsanityBonus(int32_t lvl)
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
		constexpr int32_t BONUS_PER_PLAYER = 1;	 // +1 monsters per extra contributing player
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

static int32_t CalculateQueuedMonsters(const horde::MapSize& mapSize, int32_t lvl, bool isHardMode)
{
	if (lvl <= 3) // No queue for first 3 waves still seems fine
		return 0;

	// Use global fast math cache for performance
	static bool cache_initialized = false;
	if (!cache_initialized) {
		HordePerf::g_fast_math.Initialize();
		cache_initialized = true;
	}

	float baseQueued = HordePerf::g_fast_math.GetSqrt(lvl) * 2.5f; // Reduced base from 3.0f
	baseQueued *= (1.0f + (lvl) * 0.13f); // Reduced scaling from 0.18f

	float mapSizeMultiplier = 1.0f;
	if (mapSize.isSmallMap)
	{
		mapSizeMultiplier = 1.1f; // Slightly reduced from 1.3
	}
	else if (mapSize.isMediumMap)
	{
		mapSizeMultiplier = 1.2f; // Slightly reduced from 1.4
	}
	else if (mapSize.isBigMap)
	{
		if (lvl <= 7)
		{							   // For early waves on big maps
			mapSizeMultiplier = 1.15f; // Significantly reduced from 1.5
		}
		else if (lvl <= 12)
		{
			mapSizeMultiplier = 1.3f; // Moderately reduced
		}
		else
		{
			mapSizeMultiplier = 1.5f; // Full multiplier for later waves
		}
	}
	baseQueued *= mapSizeMultiplier;

	const int32_t maxQueuedBase = mapSize.isSmallMap ? 20 : (mapSize.isBigMap ? 32 : 25); // Reduced maxes
	// Further reduce max queue for very early waves
	int32_t maxQueued = maxQueuedBase;
	if (lvl <= 7)
	{
		maxQueued = std::max(5, static_cast<int32_t>(maxQueuedBase * 0.5f)); // e.g., half max, but at least 5
	}
	else if (lvl <= 12)
	{
		maxQueued = std::max(10, static_cast<int32_t>(maxQueuedBase * 0.75f));
	}

	if (lvl > 20)
	{ // Bonus for high levels
		// FIX: Explicitly cast the result of std::pow (a double) to a float.
		baseQueued *= static_cast<float>(std::pow(1.15f, std::min(lvl - 20, 18)));
	}

	if (isHardMode)
	{ // Difficulty adjustment
		float difficultyMultiplier = 1.25f;
		if (lvl > 25)
		{
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
			playerMultipliers[static_cast<size_t>(players)] = (players <= 1) ? 1.0f : BASE_DIFFICULTY_MULTIPLIER + ((players - 1) * PLAYER_COUNT_SCALE);
		}

		// Initialize base counts by map type and level
		for (int mapType = 0; mapType < 3; ++mapType)
		{
			// FIX: Renamed 'level' to 'waveLevel' to avoid shadowing the global 'level' struct.
			for (int32_t waveLevel = 0; waveLevel <= MAX_WAVE_LEVEL; ++waveLevel)
			{
				// Select the appropriate base count based on level ranges
				int32_t countIndex;
				if (waveLevel <= 5)
					countIndex = 0;
				else if (waveLevel <= 10)
					countIndex = 1;
				else if (waveLevel <= 15)
					countIndex = 2;
				else
					countIndex = 3; // Levels > 15

				// Store pre-computed base count
				// FIX: Used static_cast to safely convert signed indexes to unsigned size_t.
				baseCountsByLevel[static_cast<size_t>(mapType)][static_cast<size_t>(waveLevel)] = BASE_COUNTS[static_cast<size_t>(mapType)][static_cast<size_t>(countIndex)];
			}
		}

		// Removed initialization for additionalSpawnsByLevel as it's calculated directly
		// in UnifiedAdjustSpawnRate now, eliminating the unused variables.

		// Initialize cooldown scales (can be accessed by mapType and level)
		horde::MapSize smallMap = { true, false, false };
		horde::MapSize mediumMap = { false, false, true }; // Corrected: Medium map should have isMediumMap true
		horde::MapSize bigMap = { false, true, false };

		// FIX: Renamed 'level' to 'waveLevel' to avoid shadowing.
		for (int32_t waveLevel = 0; waveLevel <= MAX_WAVE_LEVEL; ++waveLevel)
		{
			cooldownScales[0][static_cast<size_t>(waveLevel)] = CalculateCooldownScale(waveLevel, smallMap);	 // Index 0 = Small
			cooldownScales[1][static_cast<size_t>(waveLevel)] = CalculateCooldownScale(waveLevel, mediumMap); // Index 1 = Medium
			cooldownScales[2][static_cast<size_t>(waveLevel)] = CalculateCooldownScale(waveLevel, bigMap);	 // Index 2 = Big
		}
	}
} g_waveScalingCache;

void UnifiedAdjustSpawnRate(const horde::MapSize& mapSize, int32_t lvl, int32_t humanPlayers)
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
// 	if (developer->integer == 3)
// 	{
// 		gi.Com_PrintFmt("DEBUG: Wave {} settings:\n", safeLevel);
// 		gi.Com_PrintFmt("  - Spawn cooldown: {:.2f}s (Scale {:.2f}x)\n",
// 			SPAWN_POINT_COOLDOWN.seconds(), cooldownScale);
// 		gi.Com_PrintFmt("  - Base monsters: {}\n", baseCount);
// 		gi.Com_PrintFmt("  - Additional spawns: {}\n", additionalSpawn);
// 		gi.Com_PrintFmt("  - Queued monsters: {}\n", g_horde_local.queued_monsters);
// 		gi.Com_PrintFmt("  - Map type: {}\n",
// 			mapSize.isBigMap ? "big" : (mapSize.isSmallMap ? "small" : "medium"));
// 	}
}

void VerifyAndAdjustBots();
static void TriggerRetaliation(const horde::MapSize& mapSize, int32_t waveLevel, edict_t* target_player);

struct ConditionParams
{
	int32_t maxMonsters;
	gtime_t timeThreshold;
	gtime_t lowPercentageTimeThreshold;
	gtime_t independentTimeThreshold;
	float lowPercentageThreshold;
	float aggressiveTimeReductionThreshold;

	ConditionParams() : maxMonsters(0),
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
// static int32_t g_lastNumHumanPlayers = -1;
// static bool g_maxMonstersReached = false;
// static bool g_lowPercentageTriggered = false;

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
static bool WasRecentlyUsed(MonsterWaveType wave_type)
{
	// Define the major "themes" that should not repeat in consecutive waves.
	// This is the core of the fix.
	static constexpr std::array<MonsterWaveType, 7> MAJOR_THEMES = {{MonsterWaveType::Flying,
																	 MonsterWaveType::Gekk,
																	 MonsterWaveType::Mutant,
																	 MonsterWaveType::Berserk,
																	 MonsterWaveType::Spawner,
																	 MonsterWaveType::Shambler,
																	 MonsterWaveType::Arachnophobic}};

	// Iterate through the history of the last few waves.
	for (const auto &prev_type : previous_wave_types)
	{
		// If the historical slot is empty, skip it.
		if (prev_type == MonsterWaveType::None)
		{
			continue;
		}

		// --- 1. Theme Repetition Check ---
		// Check if the new wave and a previous wave share a major theme.
		for (const auto &theme : MAJOR_THEMES)
		{
			// First, check if the new wave even has this theme. If not, no need to check history for it.
			if (HasWaveType(wave_type, theme))
			{
				// The new wave has the theme. Now, did a previous wave ALSO have it?
				if (HasWaveType(prev_type, theme))
				{
					// Yes, this is a theme repeat (e.g., a flying wave following a flying wave).
					return true;
				}
			}
		}

		// --- 2. Exact Match Fallback Check ---
		// If no theme was repeated, we still check if the entire wave composition is identical.
		// This prevents, for example, two identical "Light | Medium | Ground" waves in a row.
		if (wave_type == prev_type)
		{
			return true;
		}
	}

	// If we've checked all recent waves and found no theme repeats or exact matches, it's not a repeat.
	return false;
}

// Helper function to store wave type in memory
static void StoreWaveType(MonsterWaveType wave_type)
{
	previous_wave_types[wave_memory_index] = wave_type;
	wave_memory_index = (wave_memory_index + 1) % WAVE_MEMORY_SIZE;
}


// Helper function to check if a wave type is a special wave
static bool IsSpecialWaveType(MonsterWaveType type)
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
static bool WasLastWaveSpecial()
{
	if (previous_wave_types.empty())
	{
		return false;
	}

	// Get the most recent wave type (the one right before the current index)
	size_t last_index = (wave_memory_index == 0) ? WAVE_MEMORY_SIZE - 1 : wave_memory_index - 1;

	return IsSpecialWaveType(previous_wave_types[last_index]);
}

//======================================================================
// SECTION: Wave Composition and Initialization (SoA Refactor)
//======================================================================

// --- Step 1: Define Source Data Structures ---

struct WaveOptionalComponent
{
	MonsterWaveType type = MonsterWaveType::None;
	float chance = 0.0f;
};

struct WaveDefinition
{
	int max_wave;
	MonsterWaveType base_type;
	std::array<WaveOptionalComponent, 4> optionals;
	// size_t num_optionals;
};

struct SpecialWave
{
	MonsterWaveType type;
	float chance;
	int min_wave;
	int max_wave;
	const char *message;
};

// --- Step 2: Define Source Data in Human-Readable Format (AoS) ---

// Corrected WAVE_DEFINITIONS_SRC array
constexpr std::array<WaveDefinition, 8> WAVE_DEFINITIONS_SRC = {{
	// Waves 1-5:
	{5, MonsterWaveType::Light | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{}, {}, {}, {}}}}, // REMOVED the trailing ", 0"

	// Waves 6-10:
	{10, MonsterWaveType::Light | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::Small, 0.45f}, {}, {}, {}}}}, // REMOVED the trailing ", 1"

	// Waves 11-15:
	{15, MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::Special, 0.65f}, {MonsterWaveType::Small, 0.2f}, {}, {}}}}, // REMOVED the trailing ", 2"

	// Waves 16-20:
	{20, MonsterWaveType::Medium | MonsterWaveType::Heavy | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::Fast, 0.55f}, {MonsterWaveType::Special, 0.3f}, {}, {}}}}, // REMOVED the trailing ", 2"

	// Waves 21-25:
	{25, MonsterWaveType::Bomber | MonsterWaveType::Heavy | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::Fast, 0.55f}, {MonsterWaveType::Special, 0.3f}, {}, {}}}}, // REMOVED the trailing ", 2"

	// Waves 26-35:
	{35, MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::Special | MonsterWaveType::Fast, 0.75f}, {MonsterWaveType::Medium, 0.28f}, {}, {}}}}, // REMOVED the trailing ", 2"

	// Waves 36-40:
	{40, MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Special | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::SemiBoss, 0.45f}, {MonsterWaveType::Fast | MonsterWaveType::Bomber, 0.35f}, {MonsterWaveType::Medium, 0.30f}, {}}}}, // REMOVED the trailing ", 3"

	// Waves 41+:
	{999, MonsterWaveType::Elite | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Ground | MonsterWaveType::Flying, {{{MonsterWaveType::SemiBoss, 0.35f}, {MonsterWaveType::Bomber | MonsterWaveType::Spawner, 0.6f}, {}, {}}}} // REMOVED the trailing ", 2"
}};

// Source data for special waves (chance is calculated at runtime based on player count)
static constexpr std::array<SpecialWave, 7> SPECIAL_WAVES_SRC = {{{MonsterWaveType::Gekk, 0.0f, 5, 7, "*** Gekk invasion incoming! ***\n"},
																  {MonsterWaveType::Mutant | MonsterWaveType::Melee, 0.30f, 8, 25, "*** Enraged Horde approaching! ***\n"},
																  {MonsterWaveType::Flying | MonsterWaveType::Fast, 0.2f, 9, -1, "*** Aerial assault incoming! ***\n"},
																  {MonsterWaveType::Berserk, 0.2f, 8, 12, "*** Berserkers incoming! ***\n"},
																  {MonsterWaveType::Bomber, 0.35f, 10, -1, "*** Strogg Bomber Units Arrived! ***\n"},
																  {MonsterWaveType::Heavy, 0.2f, 12, -1, "*** Heavy Armored Units incoming! ***\n"},
																  {MonsterWaveType::Spawner, 0.75f, 25, -1, "*** Spawners Deployed! ***\n"}}};

// --- Step 3: Define  Data Structures (SoA) ---

struct WaveDefinitionsSoA
{
	static constexpr size_t DEF_COUNT = WAVE_DEFINITIONS_SRC.size();
	static constexpr size_t OPTIONALS_PER_DEF = 4;

	std::array<int, DEF_COUNT> max_waves;
	std::array<MonsterWaveType, DEF_COUNT> base_types;
	std::array<MonsterWaveType, DEF_COUNT * OPTIONALS_PER_DEF> optional_types;
	std::array<float, DEF_COUNT * OPTIONALS_PER_DEF> optional_chances;
};

struct SpecialWavesSoA
{
	static constexpr size_t WAVE_COUNT = SPECIAL_WAVES_SRC.size();
	std::array<MonsterWaveType, WAVE_COUNT> types;
	std::array<float, WAVE_COUNT> base_chances;
	std::array<int, WAVE_COUNT> min_waves;
	std::array<int, WAVE_COUNT> max_waves;
	std::array<const char *, WAVE_COUNT> messages;
};

// --- Step 4: Define Compile-Time Transformation Functions ---

constexpr WaveDefinitionsSoA create_wave_definitions_soa()
{
	WaveDefinitionsSoA soa_data{};
	for (size_t i = 0; i < WAVE_DEFINITIONS_SRC.size(); ++i)
	{
		soa_data.max_waves[i] = WAVE_DEFINITIONS_SRC[i].max_wave;
		soa_data.base_types[i] = WAVE_DEFINITIONS_SRC[i].base_type;
		for (size_t j = 0; j < WaveDefinitionsSoA::OPTIONALS_PER_DEF; ++j)
		{
			const size_t flat_index = i * WaveDefinitionsSoA::OPTIONALS_PER_DEF + j;
			if (j < WAVE_DEFINITIONS_SRC[i].optionals.size())
			{
				soa_data.optional_types[flat_index] = WAVE_DEFINITIONS_SRC[i].optionals[j].type;
				soa_data.optional_chances[flat_index] = WAVE_DEFINITIONS_SRC[i].optionals[j].chance;
			}
		}
	}
	return soa_data;
}

constexpr SpecialWavesSoA create_special_waves_soa()
{
	SpecialWavesSoA soa_data{};
	for (size_t i = 0; i < SPECIAL_WAVES_SRC.size(); ++i)
	{
		soa_data.types[i] = SPECIAL_WAVES_SRC[i].type;
		soa_data.base_chances[i] = SPECIAL_WAVES_SRC[i].chance;
		soa_data.min_waves[i] = SPECIAL_WAVES_SRC[i].min_wave;
		soa_data.max_waves[i] = SPECIAL_WAVES_SRC[i].max_wave;
		soa_data.messages[i] = SPECIAL_WAVES_SRC[i].message;
	}
	return soa_data;
}

// --- Step 5: Create Global, Constant, SoA Data Instances ---
static const WaveDefinitionsSoA g_waveDefinitions = create_wave_definitions_soa();
static const SpecialWavesSoA g_specialWaves = create_special_waves_soa();

// --- Step 6: REPLACEMENT for GetWaveComposition and InitializeWaveType ---

inline MonsterWaveType GetWaveComposition(int waveNumber, bool forceSpecialWave = false)
{
	// --- Part 1: Check for Special Waves ---
	if (!forceSpecialWave && !WasLastWaveSpecial())
	{
		const int32_t numHumanPlayers = GetNumHumanPlayers();
		for (size_t i = 0; i < g_specialWaves.WAVE_COUNT; ++i)
		{
			// Fast checks on contiguous SoA data
			if (waveNumber >= g_specialWaves.min_waves[i] &&
				(g_specialWaves.max_waves[i] == -1 || waveNumber <= g_specialWaves.max_waves[i]))
			{
				// Slower checks only if level is valid
				const MonsterWaveType type = g_specialWaves.types[i];
				if (!WasRecentlyUsed(type))
				{
					float chance = g_specialWaves.base_chances[i];

					// --- THIS IS THE FIX ---
					// Use the HasWaveType helper to correctly check for the Gekk flag,
					// even if it's combined with other flags like Ground or Small.
					if (HasWaveType(type, MonsterWaveType::Gekk))
					{
						chance = (numHumanPlayers <= 2 ? 0.35f : 0.20f);
					}
					

					if (frandom() < chance)
					{
						gi.LocBroadcast_Print(PRINT_HIGH, g_specialWaves.messages[i]);
						StoreWaveType(type);
						return type;
					}
				}
			}
		}
	}

	// --- Part 2: Regular Wave Composition ---
	MonsterWaveType selected_type = MonsterWaveType::None;

	// Find the correct wave definition index by iterating the fast `max_waves` array
	size_t def_index = 0;
	for (size_t i = 0; i < g_waveDefinitions.DEF_COUNT; ++i)
	{
		if (waveNumber <= g_waveDefinitions.max_waves[i])
		{
			def_index = i;
			break;
		}
	}

	// Apply the base type
	selected_type = g_waveDefinitions.base_types[def_index];

	if (HasWaveType(selected_type, MonsterWaveType::Flying) && WasRecentlyUsed(MonsterWaveType::Flying))
	{
		selected_type &= ~MonsterWaveType::Flying;

		if (developer->integer)
		{
			gi.Com_PrintFmt("GetWaveComposition: Detected repeating Flying wave. Forcing ground-only.\n");
		}
	}

	// Process optional components for this definition
	const size_t start_optional_index = def_index * WaveDefinitionsSoA::OPTIONALS_PER_DEF;
	for (size_t i = 0; i < WaveDefinitionsSoA::OPTIONALS_PER_DEF; ++i)
	{
		const size_t current_optional_index = start_optional_index + i;
		const MonsterWaveType optional_type = g_waveDefinitions.optional_types[current_optional_index];

		if (optional_type != MonsterWaveType::None &&
			frandom() < g_waveDefinitions.optional_chances[current_optional_index] &&
			!WasRecentlyUsed(optional_type))
		{
			selected_type |= optional_type;
			if (HasWaveType(optional_type, MonsterWaveType::Flying) && incoming)
			{
				gi.sound(world, CHAN_VOICE, incoming, 1, ATTN_NONE, 0);
			}
		}
	}

	StoreWaveType(selected_type);
	return selected_type;
}

void InitializeWaveType(int32_t waveLevel)
{
	current_wave_type = GetWaveComposition(waveLevel);
}
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
// This array is sorted by `minWave` to allow for  iteration.
static const MonsterTypeInfo monsterTypes[] = {
	// --- WAVE 1 ---
	{horde::MonsterTypeID::SOLDIER_LIGHT, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 1.0f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
	{horde::MonsterTypeID::SOLDIER, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 0.9f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
	{horde::MonsterTypeID::FLYER, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Fast, 1, 0.7f, {-16, -16, -24}, {16, 16, 16}, 1.0f},

	// --- WAVE 2 ---
	{horde::MonsterTypeID::SOLDIER_SS, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 2, 0.8f, {-16, -16, -24}, {16, 16, 32}, 1.0f},

	// --- WAVE 3 ---
	{horde::MonsterTypeID::INFANTRY_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 3, 0.85f, {-16, -16, -24}, {16, 16, 32}, 1.0f},

	// --- WAVE 4 ---
	{horde::MonsterTypeID::SOLDIER_HYPERGUN, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 4, 0.7f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
	{horde::MonsterTypeID::GEKK, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Small | MonsterWaveType::Mutant | MonsterWaveType::Gekk, 4, 0.7f, {-16, -16, -24}, {16, 16, -8}, 1.0f},

	// --- WAVE 5 ---
	{horde::MonsterTypeID::PARASITE, MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Melee, 5, 0.6f, {-16, -16, -24}, {16, 16, 24}, 1.0f},

	// --- WAVE 6 ---
	{horde::MonsterTypeID::SOLDIER_RIPPER, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.8f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
	{horde::MonsterTypeID::FIXBOT, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.45f, {-16, -16, -12}, {16, 16, 12}, 1.4f},
	{horde::MonsterTypeID::BRAIN, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special | MonsterWaveType::Melee | MonsterWaveType::Mutant, 6, 0.7f, {-16, -16, -24}, {16, 16, -8}, 1.0f},
	{horde::MonsterTypeID::BERSERK, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Melee | MonsterWaveType::Berserk, 6, 0.8f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
	{horde::MonsterTypeID::CHICK, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.6f, {-32, -32, -24}, {32, 32, 64}, 1.0f},

	// --- WAVE 7 ---
	{horde::MonsterTypeID::HOVER_VANILLA, MonsterWaveType::Flying | MonsterWaveType::Medium | MonsterWaveType::Light | MonsterWaveType::Ranged, 7, 0.6f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
	{horde::MonsterTypeID::STALKER, MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Fast | MonsterWaveType::Arachnophobic, 7, 0.6f, {-28, -28, -18}, {28, 28, -4}, 1.0f},
	{horde::MonsterTypeID::MEDIC, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special, 7, 0.5f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
	{horde::MonsterTypeID::SPIDER, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 7, 0.35f, {-48, -48, -20}, {48, 48, 48}, 0.7f},

	// --- WAVE 8 ---
	{horde::MonsterTypeID::GUNNER_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 8, 0.8f, {-16, -16, -24}, {16, 16, 36}, 1.0f},

	// --- WAVE 9 ---
	{horde::MonsterTypeID::MUTANT, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 9, 0.7f, {-18, -18, -24}, {18, 18, 30}, 1.0f},

	// --- WAVE 10 ---
	{horde::MonsterTypeID::SOLDIER_LASERGUN, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 10, 0.8f, {-16, -16, -24}, {16, 16, 32}, 1.0f},

	// --- WAVE 11 ---
	{horde::MonsterTypeID::FLOATER, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Ranged, 11, 0.6f, {-24, -24, -24}, {24, 24, 48}, 0.9f},
	{horde::MonsterTypeID::INFANTRY, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Heavy | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 11, 0.85f, {-16, -16, -24}, {16, 16, 32}, 1.2f},

	// --- WAVE 12 ---
	{horde::MonsterTypeID::GUNCMDR_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 12, 0.4f, {-16, -16, -24}, {16, 16, 36}, 1.25f},
	{horde::MonsterTypeID::GLADIATOR, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged, 12, 0.7f, {-32, -32, -24}, {32, 32, 42}, 1.0f},
	{horde::MonsterTypeID::GUNNER, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 12, 0.8f, {-16, -16, -24}, {16, 16, 36}, 1.0f},

	// --- WAVE 13 ---
	{horde::MonsterTypeID::TANK_SPAWNER, MonsterWaveType::Ground | MonsterWaveType::Spawner | MonsterWaveType::Heavy | MonsterWaveType::Medium | MonsterWaveType::Elite, 13, 0.6f, {-32, -32, -16}, {32, 32, 64}, 1.0f},
	{horde::MonsterTypeID::CHICK_HEAT, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Fast, 13, 0.6f, {-32, -32, -24}, {32, 32, 64}, 1.0f},

	// --- WAVE 14 ---
	{horde::MonsterTypeID::SHAMBLER_SMALL, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Mutant | MonsterWaveType::Shambler, 14, 0.4f, {-32, -32, -24}, {32, 32, 64}, 0.6f},
	{horde::MonsterTypeID::REDMUTANT, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Elite | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 14, 0.35f, {-18, -18, -24}, {18, 18, 30}, 1.1f},
	{horde::MonsterTypeID::TANK, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 14, 0.4f, {-32, -32, -16}, {32, 32, 64}, 1.0f},

	// --- WAVE 15 ---
	{horde::MonsterTypeID::GM_ARACHNID, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Heavy | MonsterWaveType::Elite, 15, 0.45f, {-48, -48, -20}, {48, 48, 48}, 0.85f},
	{horde::MonsterTypeID::GUNCMDR, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Bomber, 15, 0.7f, {-16, -16, -24}, {16, 16, 36}, 1.25f},

	// --- WAVE 16 ---
	{horde::MonsterTypeID::TANK_COMMANDER, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 16, 0.5f, {-32, -32, -16}, {32, 32, 64}, 1.0f},

	// --- WAVE 17 ---
	{horde::MonsterTypeID::RUNNERTANK, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Fast, 17, 0.5f, {-32, -32, -16}, {32, 32, 64}, 1.0f},

	// --- WAVE 18 ---
	{horde::MonsterTypeID::ARACHNID2, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 18, 0.4f, {-48, -48, -20}, {48, 48, 48}, 0.85f},
	{horde::MonsterTypeID::HOVER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.5f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
	{horde::MonsterTypeID::GLADIATOR_B, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f, {-32, -32, -24}, {32, 32, 42}, 1.0f},
	{horde::MonsterTypeID::GLADIATOR_C, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f, {-32, -32, -24}, {32, 32, 42}, 1.0f},

	// --- WAVE 19 ---
	{horde::MonsterTypeID::FLOATER_TRACKER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 19, 0.45f, {-24, -24, -24}, {24, 24, 48}, 1.0f},
	{horde::MonsterTypeID::DAEDALUS_BOMBER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Bomber, 19, 0.35f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
	{horde::MonsterTypeID::BOSS2_64, MonsterWaveType::Flying | MonsterWaveType::Elite, 19, 0.2f, {-60, -60, 0}, {60, 60, 90}, 0.6f},
	{horde::MonsterTypeID::BOSS2_MINI, MonsterWaveType::Flying | MonsterWaveType::Elite, 19, 0.2f, {-60, -60, 0}, {60, 60, 90}, 0.6f},

	// --- WAVE 20 ---
	{horde::MonsterTypeID::PERRO_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Fast | MonsterWaveType::Small, 20, 0.4f, {-16, -16, -24}, {16, 16, 24}, 1.0f},

	// --- WAVE 21 ---
	{horde::MonsterTypeID::DAEDALUS, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 21, 0.4f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
	{horde::MonsterTypeID::JANITOR, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Bomber, 21, 0.5f, {-64, -64, -0}, {64, 64, 112}, 0.6f},

	// --- WAVE 22 ---
	{horde::MonsterTypeID::SHAMBLER, MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 22, 0.4f, {-32, -32, -24}, {32, 32, 64}, 1.0f},

	// --- WAVE 23 ---
	{horde::MonsterTypeID::MAKRON, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 23, 0.01f, {-30, -30, 0}, {30, 30, 90}, 1.0f},
	{horde::MonsterTypeID::WIDOW1, MonsterWaveType::Ground | MonsterWaveType::Spawner | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 23, 0.15f, {-40, -40, 0}, {40, 40, 144}, 0.6f},

	// --- WAVE 25 ---
	{horde::MonsterTypeID::PSX_ARACHNID, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite | MonsterWaveType::Spawner, 25, 0.35f, {-48, -48, -20}, {48, 48, 48}, 1.0f},

	// --- WAVE 26 ---
	{horde::MonsterTypeID::JANITOR2, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Special | MonsterWaveType::Bomber, 26, 0.4f, {-96, -96, -66}, {96, 96, 62}, 0.4f},

	// --- WAVE 27 ---
	{horde::MonsterTypeID::MEDIC_COMMANDER, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.3f, {-24, -24, -24}, {24, 24, 32}, 1.0f},
	{horde::MonsterTypeID::CARRIER_MINI, MonsterWaveType::Flying | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.2f, {-56, -56, -44}, {56, 56, 44}, 0.6f},

	// --- WAVE 28 ---
	{horde::MonsterTypeID::TANK_64, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 28, 0.3f, {-32, -32, -16}, {32, 32, 64}, 1.1f},

	// --- WAVE 33 ---
	{horde::MonsterTypeID::SHAMBLER_KL, MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 33, 0.23f, {-32, -32, -24}, {32, 32, 64}, 1.0f},
	{horde::MonsterTypeID::GUNCMDR_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 33, 0.2f, {-16, -16, -24}, {16, 16, 36}, 1.25f},
	{horde::MonsterTypeID::JORG_SMALL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Medium, 33, 0.4f, {-80, -80, 0}, {80, 80, 140}, 0.35f},

	// --- WAVE 41 ---
	{horde::MonsterTypeID::MAKRON_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Elite, 41, 0.2f, {-30, -30, 0}, {30, 30, 90}, 1.0f},

	// --- SPECIAL / NOT NORMALLY SPAWNED (minWave 999) ---
	{horde::MonsterTypeID::TURRET, MonsterWaveType::Ground | MonsterWaveType::Special, 999, 0.0f, {-16, -16, -16}, {16, 16, 16}, 1.0f},
	{horde::MonsterTypeID::SENTRYGUN, MonsterWaveType::Ground | MonsterWaveType::Special, 999, 0.0f, {-16, -16, -16}, {16, 16, 16}, 1.0f},
	{horde::MonsterTypeID::BOSS2, MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-60, -60, 0}, {60, 60, 90}, 1.0f},
	{horde::MonsterTypeID::CARRIER, MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-56, -56, -44}, {56, 56, 44}, 1.0f},
	{horde::MonsterTypeID::FIXBOT_KL, MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-16, -16, -12}, {16, 16, 12}, 2.6f},
	{horde::MonsterTypeID::WIDOW, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-40, -40, 0}, {40, 40, 144}, 1.0f},
	{horde::MonsterTypeID::WIDOW2, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 999, 0.0f, {-40, -40, 0}, {40, 40, 144}, 0.8f},
	{horde::MonsterTypeID::BOSS5, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-32, -32, -16}, {32, 32, 64}, 1.0f},
	{horde::MonsterTypeID::JORG, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-80, -80, 0}, {80, 80, 140}, 1.0f},
	{horde::MonsterTypeID::PSX_GUARDIAN, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-32, -32, -24}, {32, 32, 64}, 1.0f}};

// Define a constant for the number of monsters, derived from the array itself.
constexpr size_t MONSTER_DATA_COUNT = std::size(monsterTypes);

// The new SoA (Structure of Arrays) definition.
struct MonsterDataSoA
{
	// We use std::array for compile-time safety and size checking.
	// The size is based on the highest enum value for direct indexing.
	static constexpr size_t MONSTER_ARRAY_SIZE = static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES);

	std::array<MonsterWaveType, MONSTER_ARRAY_SIZE> waveTypes;
	std::array<int, MONSTER_ARRAY_SIZE> minWaves;
	std::array<float, MONSTER_ARRAY_SIZE> weights;
	std::array<vec3_t, MONSTER_ARRAY_SIZE> default_mins;
	std::array<vec3_t, MONSTER_ARRAY_SIZE> default_maxs;
	std::array<float, MONSTER_ARRAY_SIZE> s_scales;
};

// This is the core of the compile-time conversion.
// This function is executed by the compiler, not at runtime.
constexpr MonsterDataSoA create_monster_data_soa()
{
	MonsterDataSoA soa_data{}; // Initialize with default values (zeros)

	// The compiler will iterate through the AoS array...
	// By using a traditional for-loop, we ensure compatibility with older constexpr rules.
	for (size_t i = 0; i < std::size(monsterTypes); ++i)
	{
		const auto &monster_info = monsterTypes[i];

		// ...and place each piece of data into the correct parallel array
		// using the MonsterTypeID as the index.
		const size_t index = static_cast<size_t>(monster_info.typeId);
		if (index < soa_data.MONSTER_ARRAY_SIZE)
		{
			soa_data.waveTypes[index] = monster_info.types;
			soa_data.minWaves[index] = monster_info.minWave;
			soa_data.weights[index] = monster_info.weight;
			soa_data.default_mins[index] = monster_info.default_mins;
			soa_data.default_maxs[index] = monster_info.default_maxs;
			soa_data.s_scales[index] = monster_info.s_scale;
		}
	}
	return soa_data;
}

// Create the global, constant, SoA data structure.
// The compiler runs create_monster_data_soa() and bakes the result directly into the executable.
static const MonsterDataSoA g_monsterData = create_monster_data_soa();

inline MonsterWaveType GetMonsterWaveTypes(horde::MonsterTypeID typeId)
{
	const size_t index = static_cast<size_t>(typeId);
	if (index >= g_monsterData.MONSTER_ARRAY_SIZE)
	{
		return MonsterWaveType::None;
	}
	return g_monsterData.waveTypes[index];
}
#include <array>
#include <unordered_set>
#include <random>

//======================================================================
// SECTION: Boss Definitions and Selection
//======================================================================
struct boss_t
{
	horde::MonsterTypeID typeId;
	int32_t min_level;
	int32_t max_level;
	float weight;
	BossSizeCategory sizeCategory;
	BossType type;
};

static constexpr std::array<boss_t, 11> BOSS_SMALL_SRC = {{{horde::MonsterTypeID::CARRIER_MINI, 24, -1, 0.1f, BossSizeCategory::Small, BossType::CARRIER_MINI},
														   {horde::MonsterTypeID::BOSS2_KL, 24, -1, 0.1f, BossSizeCategory::Small, BossType::BOSS2KL},
														   {horde::MonsterTypeID::FIXBOT_KL, 9, -1, 0.4f, BossSizeCategory::Small, BossType::FIXBOTKL},
														   {horde::MonsterTypeID::WIDOW2, 19, -1, 0.15f, BossSizeCategory::Small, BossType::WIDOW2},
														   {horde::MonsterTypeID::TANK_64, -1, -1, 0.25f, BossSizeCategory::Small, BossType::TANK_64},
														   {horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Small, BossType::SHAMBLERKL},
														   {horde::MonsterTypeID::GUNCMDR_KL, -1, 20, 0.3f, BossSizeCategory::Small, BossType::GUNCMDRKL},
														   {horde::MonsterTypeID::MAKRON_KL, 36, -1, 0.2f, BossSizeCategory::Small, BossType::MAKRONKL},
														   {horde::MonsterTypeID::MAKRON, 16, 26, 0.1f, BossSizeCategory::Small, BossType::OTHER},
														   {horde::MonsterTypeID::PSX_ARACHNID, 15, -1, 0.1f, BossSizeCategory::Small, BossType::PSX_ARACHNID},
														   {horde::MonsterTypeID::REDMUTANT, -1, 24, 0.1f, BossSizeCategory::Small, BossType::REDMUTANT}}};

static constexpr std::array<boss_t, 13> BOSS_MEDIUM_SRC = {{{horde::MonsterTypeID::CARRIER, 24, -1, 0.1f, BossSizeCategory::Medium, BossType::CARRIER},
															{horde::MonsterTypeID::BOSS2, 19, -1, 0.1f, BossSizeCategory::Medium, BossType::BOSS2},
															{horde::MonsterTypeID::FIXBOT_KL, 9, -1, 0.4f, BossSizeCategory::Small, BossType::FIXBOTKL},
															{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Medium, BossType::SHAMBLERKL},
															{horde::MonsterTypeID::TANK_64, 21, -1, 0.1f, BossSizeCategory::Medium, BossType::TANK_64},
															{horde::MonsterTypeID::SHAMBLER_KL, 21, -1, 0.1f, BossSizeCategory::Medium, BossType::SHAMBLERKL},
															{horde::MonsterTypeID::GUNCMDR_KL, 21, -1, 0.1f, BossSizeCategory::Medium, BossType::GUNCMDRKL},
															{horde::MonsterTypeID::PSX_GUARDIAN, -1, 24, 0.1f, BossSizeCategory::Medium, BossType::PSX_GUARDIAN},
															{horde::MonsterTypeID::WIDOW2, 19, -1, 0.15f, BossSizeCategory::Medium, BossType::WIDOW2},
															{horde::MonsterTypeID::PSX_ARACHNID, 14, -1, 0.1f, BossSizeCategory::Medium, BossType::PSX_ARACHNID},
															{horde::MonsterTypeID::MAKRON_KL, 26, -1, 0.2f, BossSizeCategory::Medium, BossType::MAKRONKL},
															{horde::MonsterTypeID::MAKRON, 16, 25, 0.1f, BossSizeCategory::Medium, BossType::OTHER},
															{horde::MonsterTypeID::REDMUTANT, -1, 24, 0.1f, BossSizeCategory::Small, BossType::REDMUTANT}}};

static constexpr std::array<boss_t, 17> BOSS_LARGE_SRC = {{{horde::MonsterTypeID::CARRIER, 24, -1, 0.1f, BossSizeCategory::Large, BossType::CARRIER},
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
														   {horde::MonsterTypeID::WIDOW2, -1, 24, 0.19f, BossSizeCategory::Small, BossType::WIDOW2}}};

// --- Step 2:  Data Structure (Structure of Arrays) ---
// This is the cache-friendly structure the game will actually use.
struct BossDataSoA
{
	static constexpr size_t MAX_BOSSES = 17; // Size of the largest list

	std::array<horde::MonsterTypeID, MAX_BOSSES> typeIds;
	std::array<int32_t, MAX_BOSSES> min_levels;
	std::array<int32_t, MAX_BOSSES> max_levels;
	std::array<float, MAX_BOSSES> weights;
	std::array<BossSizeCategory, MAX_BOSSES> sizeCategories;
	size_t count; // Number of actual bosses in this list
};

// --- Step 3: Compile-Time Transformation Function ---
// This function is executed by the compiler to convert the AoS data to SoA.
template <size_t N>
constexpr BossDataSoA create_boss_soa(const std::array<boss_t, N> &boss_list)
{
	BossDataSoA soa_data{};
	soa_data.count = N;
	for (size_t i = 0; i < N; ++i)
	{
		soa_data.typeIds[i] = boss_list[i].typeId;
		soa_data.min_levels[i] = boss_list[i].min_level;
		soa_data.max_levels[i] = boss_list[i].max_level;
		soa_data.weights[i] = boss_list[i].weight;
		soa_data.sizeCategories[i] = boss_list[i].sizeCategory;
	}
	return soa_data;
}

// --- Step 4: Create the Global, Constant, SoA Data Instances ---
// The compiler runs create_boss_soa() and bakes the results directly into the executable.
static const BossDataSoA g_smallBossData = create_boss_soa(BOSS_SMALL_SRC);
static const BossDataSoA g_mediumBossData = create_boss_soa(BOSS_MEDIUM_SRC);
static const BossDataSoA g_largeBossData = create_boss_soa(BOSS_LARGE_SRC);

// --- Step 5: REPLACEMENT for GetBossList ---
// This function now returns a pointer to the appropriate pre-built SoA data.
static const BossDataSoA *GetBossListSoA(const horde::MapSize &mapSize, horde::MapID mapId)
{
	// NOTE: The special map filtering logic from the old code is not included here
	// for simplicity. If needed, you would create additional `_SRC` arrays and
	// `g_...Data` SoA instances for those filtered lists and return them here.
	if (mapSize.isSmallMap || mapId == horde::MapID::Q2DM4 || mapId == horde::MapID::Q64_COMM || mapId == horde::MapID::TEST_TEST_KAISER)
	{
		return &g_smallBossData;
	}
	if (mapSize.isMediumMap || mapId == horde::MapID::RDM8 || mapId == horde::MapID::XDM1)
	{
		return &g_mediumBossData;
	}
	if (mapSize.isBigMap || mapId == horde::MapID::TEST_SPBOX || mapId == horde::MapID::Q2CTF4)
	{
		return &g_largeBossData;
	}
	return nullptr; // Return null if no list is appropriate
}

// This struct tracks the history of spawned bosses to prevent repetition.
struct RecentBosses
{
	std::array<horde::MonsterTypeID, MAX_RECENT_BOSSES> items;
	size_t count;

	RecentBosses() : count(0)
	{
		items.fill(horde::MonsterTypeID::UNKNOWN);
	}

	void add(horde::MonsterTypeID boss_id)
	{
		if (boss_id == horde::MonsterTypeID::UNKNOWN)
			return;
		if (count < MAX_RECENT_BOSSES)
		{
			items[count++] = boss_id;
		}
		else
		{
			for (size_t i = 0; i < MAX_RECENT_BOSSES - 1; ++i)
			{
				items[i] = items[i + 1];
			}
			items[MAX_RECENT_BOSSES - 1] = boss_id;
		}
	}

	void add(const char *boss_classname)
	{
		if (!boss_classname)
			return;
		add(horde::MonsterTypeRegistry::GetTypeID(boss_classname));
	}

	bool contains(horde::MonsterTypeID boss_id) const
	{
		if (boss_id == horde::MonsterTypeID::UNKNOWN)
			return false;
		for (size_t i = 0; i < count; ++i)
		{
			if (items[i] == boss_id)
				return true;
		}
		return false;
	}

	bool contains(const char *boss_classname) const
	{
		if (!boss_classname)
			return false;
		return contains(horde::MonsterTypeRegistry::GetTypeID(boss_classname));
	}

	void clear()
	{
		items.fill(horde::MonsterTypeID::UNKNOWN);
		count = 0;
	}

	size_t size() const { return count; }
	bool empty() const { return count == 0; }
};
static RecentBosses recent_bosses;

// This struct pre-calculates which bosses are eligible for each wave and map type.
// Its initialize method is now updated to work with the new SoA data structures.
struct BossEligibilityCache
{
	static constexpr int32_t MAX_PRECOMPUTED_WAVE = 50;

	struct LevelEligibility
	{
		// MODIFICATION: Store the index into the SoA array, not the TypeID.
		uint8_t soa_indices[MAX_ELIGIBLE_BOSSES] = { 0 };
		uint8_t count = 0;
	};

	// Cache by map type (0=small, 1=medium, 2=large) and wave level
	LevelEligibility eligibility[3][MAX_PRECOMPUTED_WAVE + 1];
	inline static bool initialized = false;

	void initialize()
	{
		if (initialized)
			return;

		// For each map type (0=small, 1=medium, 2=large)
		for (int mapType = 0; mapType < 3; ++mapType)
		{
			horde::MapSize mapSize;
			const BossDataSoA* boss_list_soa = nullptr;

			if (mapType == 0)
			{
				mapSize = { true, false, false }; // Small
				boss_list_soa = &g_smallBossData;
			}
			else if (mapType == 1)
			{
				mapSize = { false, false, true }; // Medium
				boss_list_soa = &g_mediumBossData;
			}
			else
			{
				mapSize = { false, true, false }; // Large
				boss_list_soa = &g_largeBossData;
			}

			if (!boss_list_soa)
				continue;

			// For each wave level we want to pre-compute
			for (int32_t wave = 0; wave <= MAX_PRECOMPUTED_WAVE; ++wave)
			{
				auto& levelData = eligibility[mapType][wave];
				levelData.count = 0;

				// Filter bosses by iterating through the SoA data, which is very fast.
				for (size_t i = 0; i < boss_list_soa->count; ++i)
				{
					const int32_t min_level = boss_list_soa->min_levels[i];
					const int32_t max_level = boss_list_soa->max_levels[i];

					if ((wave >= min_level || min_level == -1) &&
						(wave <= max_level || max_level == -1))
					{
						if (levelData.count < MAX_ELIGIBLE_BOSSES)
						{
							// MODIFICATION: Store the index 'i' directly.
							levelData.soa_indices[levelData.count++] = static_cast<uint8_t>(i);
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
	const char *classname = horde::MonsterTypeRegistry::GetClassname(typeId);
	if (!classname)
	{
		return nullptr;
	}

	edict_t *monster = G_Spawn();
	if (!monster)
	{
		return nullptr;
	}

	monster->classname = classname;
	monster->s.origin = origin;
	monster->s.angles = angles;

	monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	monster->monsterinfo.last_reacttodamage_target_time = 0_ms;
	monster->monsterinfo.was_spawned_by_horde = true;

	if (g_horde_local.state == horde_state_t::spawning)
	{
		monster->monsterinfo.spawned_in_spawn_state = true;
	}

	return monster;
}

void ED_CallSpawnMonsterByID(edict_t* ent, horde::MonsterTypeID typeId);
// FIX: Removed unused 'applyHordeFlags' parameter.
edict_t *SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t &origin, const vec3_t &angles, bool link_immediately)
{
	edict_t *monster = CreateBaseHordeMonster(typeId, origin, angles);
	if (!monster)
	{
		return nullptr;
	}

	monster->solid = SOLID_NOT;
	ED_CallSpawnMonsterByID(monster, typeId);

	if (!monster->inuse)
	{
		return nullptr;
	}

	if (link_immediately)
	{
		monster->solid = SOLID_BBOX;
		gi.linkentity(monster);

		trace_t post_link_trace = gi.trace(monster->s.origin, monster->mins, monster->maxs, monster->s.origin, monster, MASK_SOLID);
		if (post_link_trace.startsolid)
		{
			monster->monsterinfo.was_stuck = true;
			monster->monsterinfo.stuck_check_time = level.time + random_time(12.0_sec, 17.0_sec);
		}
	}
	
	monster->monsterinfo.react_to_damage_time = level.time;
	return monster;
}

// Update the simpler overload to pass the new parameter
edict_t *SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t &origin)
{
    // FIX: Call the updated function signature.
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

// This function is completely refactored to use the new SoA data for high performance.
static BossPickResult G_HordePickBOSSType(const horde::MapSize& mapSize, std::string_view mapname, int32_t waveNumber)
{
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(mapname.data());

	if (!g_bossEligibilityCache.initialized)
	{
		g_bossEligibilityCache.initialize();
	}

	const BossDataSoA* boss_list_soa = GetBossListSoA(mapSize, mapId);
	if (!boss_list_soa)
	{
		if (developer->integer)
			gi.Com_PrintFmt("WARNING: Empty boss list for map {} at wave {}\n", mapname.data(), waveNumber);
		return BossPickResult();
	}

	const int mapTypeIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);
	const int32_t safeWaveNumber = std::min(waveNumber, BossEligibilityCache::MAX_PRECOMPUTED_WAVE);

	const auto& eligibilityData = g_bossEligibilityCache.eligibility[static_cast<size_t>(mapTypeIndex)][static_cast<size_t>(safeWaveNumber)];
	if (eligibilityData.count == 0)
	{
		if (developer->integer)
			gi.Com_PrintFmt("WARNING: No bosses eligible for wave {} on this map type.\n", waveNumber);
		return BossPickResult();
	}

	struct WeightedBoss
	{
		size_t index_in_soa;
		float weight;
		float cumulativeWeight;
	};
	std::array<WeightedBoss, MAX_ELIGIBLE_BOSSES> weightedBosses{};
	size_t weightedCount = 0;
	float totalWeight = 0.0f;

	for (size_t i = 0; i < eligibilityData.count; ++i)
	{
		// MODIFICATION: Get the index directly from the cache.
		const size_t boss_index_in_soa = eligibilityData.soa_indices[i];
		const horde::MonsterTypeID bossTypeId = boss_list_soa->typeIds[boss_index_in_soa];

		if (recent_bosses.contains(bossTypeId))
		{
			continue;
		}

		// MODIFICATION: No more linear search needed. We already have the index.
		float weight = boss_list_soa->weights[boss_index_in_soa];

		if (weightedCount < MAX_ELIGIBLE_BOSSES)
		{
			totalWeight += weight;
			weightedBosses[weightedCount].index_in_soa = boss_index_in_soa;
			weightedBosses[weightedCount].weight = weight;
			weightedBosses[weightedCount].cumulativeWeight = totalWeight;
			weightedCount++;
		}
	}

	if (weightedCount == 0 && recent_bosses.size() > 0)
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("INFO: No non-recent bosses eligible, ignoring history for this pick.\n");
		totalWeight = 0.0f;
		weightedCount = 0;

		for (size_t i = 0; i < eligibilityData.count; ++i)
		{
			// MODIFICATION: Get the index directly from the cache.
			const size_t boss_index_in_soa = eligibilityData.soa_indices[i];

			float weight = boss_list_soa->weights[boss_index_in_soa];
			if (weightedCount < MAX_ELIGIBLE_BOSSES)
			{
				totalWeight += weight;
				weightedBosses[weightedCount].index_in_soa = boss_index_in_soa;
				weightedBosses[weightedCount].weight = weight;
				weightedBosses[weightedCount].cumulativeWeight = totalWeight;
				weightedCount++;
			}
		}
	}

	if (weightedCount == 0 || totalWeight <= 0.0f)
	{
		if (developer->integer)
			gi.Com_PrintFmt("WARNING: No eligible bosses found after all filtering.\n");
		return BossPickResult();
	}

	const float randomValue = frandom() * totalWeight;
	auto it = std::lower_bound(
		weightedBosses.begin(),
		weightedBosses.begin() + weightedCount,
		randomValue,
		[](const WeightedBoss& boss, float value)
		{
			return boss.cumulativeWeight < value;
		});

	if (it == weightedBosses.begin() + weightedCount)
	{
		it = std::prev(it);
	}

	const size_t chosen_index = it->index_in_soa;
	const horde::MonsterTypeID chosen_typeId = boss_list_soa->typeIds[chosen_index];
	const BossSizeCategory chosen_sizeCategory = boss_list_soa->sizeCategories[chosen_index];

	recent_bosses.add(chosen_typeId);
	if (developer->integer > 1)
	{
		const char* chosen_name = horde::MonsterTypeRegistry::GetClassname(chosen_typeId);
		gi.Com_PrintFmt("Selected Boss: {} (Weight: {:.2f})\n", chosen_name ? chosen_name : "Unknown", it->weight);
	}
	return BossPickResult(chosen_typeId, chosen_sizeCategory);
}
// --- Step 1: Define the SoA structure for item data ---
struct HordeItemDataSoA
{
	static constexpr size_t NUM_ITEMS = std::size(hordeItemData);

	std::array<item_id_t, NUM_ITEMS> ids;
	std::array<float, NUM_ITEMS> weights;
	std::array<int, NUM_ITEMS> minWaves;
};

// --- Step 2: Define the compile-time transformation function ---
constexpr HordeItemDataSoA create_horde_item_data_soa()
{
	HordeItemDataSoA soa_data{};
	// FIX: Use std::size() for C-style arrays, not .size()
	for (size_t i = 0; i < std::size(hordeItemData); ++i)
	{
		soa_data.ids[i] = hordeItemData[i].id;
		soa_data.weights[i] = hordeItemData[i].weight;
		soa_data.minWaves[i] = hordeItemData[i].minWave;
	}
	return soa_data;
}

// --- Step 3: Create the global, constant, SoA data instance ---
static const HordeItemDataSoA g_hordeItemDataSoA = create_horde_item_data_soa();

// --- Step 4: Correct the Caching Structure ---
struct HordeItemSelectionCache
{
	// FIX: Define MAX_ENTRIES inside the struct to scope it correctly.
	static constexpr size_t MAX_ENTRIES = std::size(hordeItemData);

	struct Entry
	{
		// We store the index into the SoA data, not a pointer to the old AoS data.
		size_t item_index;
		float weight;
		float cumulative_weight;
	};

	size_t count = 0;
	float total_weight = 0.0f;
	// FIX: This now compiles because MAX_ENTRIES is in scope.
	std::array<Entry, MAX_ENTRIES> entries{};

	void clear()
	{
		count = 0;
		total_weight = 0.0f;
	}
};
// Static cache instance specifically for Horde item selection
static HordeItemSelectionCache horde_item_cache;

// --- Step 5: Correct the G_HordePickItem function ---
gitem_t* G_HordePickItem()
{
	horde_item_cache.clear();

	// --- REPLACEMENT: EFFICIENT LOOP ---
	// Iterate only over the pre-filtered list of eligible item indices.
	for (size_t item_index : g_eligible_item_indices_for_wave)
	{
		if (horde_item_cache.count >= HordeItemSelectionCache::MAX_ENTRIES)
		{
			break; // Safety break
		}

		// The filtering 'if (level >= minWave)' is NO LONGER NEEDED here.

		// We access the SoA data using the pre-filtered index.
		float adjusted_weight = g_hordeItemDataSoA.weights[item_index];

		if (adjusted_weight > 0.0f)
		{
			horde_item_cache.total_weight += adjusted_weight;
			auto& entry = horde_item_cache.entries[horde_item_cache.count];
			entry.item_index = item_index; // Store the index
			entry.weight = adjusted_weight;
			entry.cumulative_weight = horde_item_cache.total_weight;
			horde_item_cache.count++;
		}
	}
	// --- END REPLACEMENT ---

	// Check if any eligible items were found
	if (horde_item_cache.count == 0 || horde_item_cache.total_weight <= 0.0f)
	{
		return nullptr;
	}

	// --- Weighted Random Selection ---
	const float random_value = frandom() * horde_item_cache.total_weight;

	auto it = std::lower_bound(
		horde_item_cache.entries.begin(),
		horde_item_cache.entries.begin() + horde_item_cache.count,
		random_value,
		[](const HordeItemSelectionCache::Entry& entry, float value)
		{
			return entry.cumulative_weight < value;
		});

	if (it == horde_item_cache.entries.begin() + horde_item_cache.count)
	{
		it = std::prev(it);
	}

	const size_t chosen_index = it->item_index;
	const item_id_t chosen_id = g_hordeItemDataSoA.ids[chosen_index];

	return GetItemByIndex(chosen_id);
}

static float adjustFlyingSpawnProbability(int32_t flyingSpawns)
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

// Category check functions using TypeIDs
bool IsFlying(horde::MonsterTypeID typeId)
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


inline bool IsValidMonsterForWave(horde::MonsterTypeID typeId, MonsterWaveType waveRequirements)
{
	// Fast exit for special cases like boss minion waves that have no requirements.
	if (waveRequirements == MonsterWaveType::None)
	{
		return true;
	}

	// Use the fast LUT to get the monster's properties.
	const MonsterWaveType monster_flags = GetMonsterWaveTypes(typeId);

	// --- Step 1: Handle Exclusive, Thematic Waves (Strict Matching) ---
	// If the wave is a special theme (Gekk, Mutant, etc.), the monster MUST match that theme.
	static constexpr std::array<MonsterWaveType, 6> special_themes = {
		MonsterWaveType::Gekk, MonsterWaveType::Berserk, MonsterWaveType::Mutant,
		MonsterWaveType::Spawner, MonsterWaveType::Shambler, MonsterWaveType::Arachnophobic};

	for (const auto &theme : special_themes)
	{
		if (HasWaveType(waveRequirements, theme))
		{
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
	if (wave_wants_ground && wave_wants_flying)
	{
		if (!monster_is_ground && !monster_is_flying)
			return false;
	}
	// If the wave wants ONLY ground, the monster must be ground.
	else if (wave_wants_ground)
	{
		if (!monster_is_ground)
			return false;
	}
	// If the wave wants ONLY flying, the monster must be flying.
	else if (wave_wants_flying)
	{
		if (!monster_is_flying)
			return false;
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
	if (required_categories != MonsterWaveType::None)
	{
		if ((monster_flags & required_categories) == MonsterWaveType::None)
		{
			return false;
		}
	}

	// If all checks pass, the monster is valid for this wave.
	return true;
}

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

	void clear()
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
	if (currentLevel <= 10)
		return frandom() < 0.32f; // 32% chance in early waves
	if (currentLevel <= 20)
		return frandom() < 0.19f; // 19% chance in mid waves
	return frandom() < 0.07f;	  // 7% chance in late waves
}

static int32_t CalculateEffectiveMonsterLevel(int32_t currentActualLevel, bool attemptHigherLevel, MonsterWaveType waveTypeForFiltering)
{
	if (!attemptHigherLevel)
	{
		return currentActualLevel; // No change needed.
	}

	int32_t levelBoost;
	int32_t maxLevelCap;

	if (HasWaveType(waveTypeForFiltering, MonsterWaveType::Flying))
	{
		levelBoost = irandom(6, 17);
		maxLevelCap = currentActualLevel + 11;
	}
	else
	{
		if (currentActualLevel < 7)
			levelBoost = irandom(2, 4);
		else if (currentActualLevel <= 15)
			levelBoost = irandom(4, 8);
		else
			levelBoost = irandom(3, 6);
		maxLevelCap = currentActualLevel + 8;
	}

	maxLevelCap = std::min(maxLevelCap, 45); // Absolute cap.

	int32_t potentialEffectiveLevel = std::min(currentActualLevel + levelBoost, maxLevelCap);

	// ---  SEARCH ---
	// Use std::lower_bound to efficiently find the start of the relevant monster range.
	auto it = std::lower_bound(std::begin(monsterTypes), std::end(monsterTypes), currentActualLevel,
		[](const MonsterTypeInfo& monster, int32_t level) {
			return monster.minWave <= level;
		});

	bool any_new_monsters_unlocked = false;
	// Iterate only over the small, relevant subset of monsters.
	for (; it != std::end(monsterTypes) && it->minWave <= potentialEffectiveLevel; ++it)
	{
		if (IsValidMonsterForWave(it->typeId, waveTypeForFiltering))
		{
			any_new_monsters_unlocked = true;
			break; // Found one, no need to check further.
		}
	}
	// --- END  SEARCH ---

	if (!any_new_monsters_unlocked)
	{
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

// This function now takes the index `i` and the context `ctx`
// to calculate the base weight for the monster at that index.
static float CalculateBaseWeight(size_t i, const MonsterSelectionContext &ctx)
{
	float weight = g_monsterData.weights[i];
	const int minWave = g_monsterData.minWaves[i];

	if (minWave < ctx.currentActualLevel)
	{
		float relevance = 1.0f - std::min(0.6f, (ctx.currentActualLevel - minWave) * 0.04f);
		weight *= relevance;
	}
	// Ensure a minimum weight for monsters that are still relevant.
	if (weight < 0.1f && g_monsterData.weights[i] >= 0.1f)
	{
		weight = 0.1f;
	}
	return weight;
}
// This function now takes the index `i` and the context `ctx`
// to apply special modifiers.
static float ApplySpecialModifiers(float weight, size_t i, const MonsterSelectionContext &ctx)
{
    // FIX: Removed unused variable 'currentId'.
	// const horde::MonsterTypeID currentId = static_cast<horde::MonsterTypeID>(i);
	const int minWave = g_monsterData.minWaves[i];

	if (g_insane->integer || g_chaotic->integer)
	{
		const float difficultyScale = ctx.currentActualLevel <= 15 ? 1.2f : 1.4f;
		weight *= difficultyScale;
		if (HasWaveType(g_monsterData.waveTypes[i], MonsterWaveType::Elite))
		{
			weight *= 1.25f;
		}
	}

	weight *= ctx.flyingAdjustmentFactor;

	if (ctx.effectiveLevel > ctx.currentActualLevel && minWave > ctx.currentActualLevel)
	{
		if (minWave - ctx.currentActualLevel > 5)
		{
			weight *= 0.6f;
		}
	}

	if (ctx.isRetaliationActive)
	{
		constexpr MonsterWaveType RETALIATION_FOCUS_TYPES =
			MonsterWaveType::Spawner | MonsterWaveType::Bomber | MonsterWaveType::Special;
		if (HasWaveType(g_monsterData.waveTypes[i], RETALIATION_FOCUS_TYPES))
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

// This function now takes the index `i` and the context `ctx`
// to check for compatibility.
static bool IsMonsterCompatible(size_t i, const MonsterSelectionContext &ctx)
{
	const horde::MonsterTypeID currentId = static_cast<horde::MonsterTypeID>(i);

	if (g_monsterData.minWaves[i] > ctx.effectiveLevel)
		return false;
	if (ctx.waveTypeForFiltering != MonsterWaveType::None && !IsValidMonsterForWave(currentId, ctx.waveTypeForFiltering))
		return false;

	const bool monster_is_flying = IsFlying(currentId);
	if (ctx.isSpawnPointFlying && !monster_is_flying)
		return false;

	return true;
}

static void BuildMonsterCache(MonsterCache& cache_ref, const MonsterSelectionContext& ctx)
{
	cache_ref.clear();

	// By iterating this smaller, pre-filtered list, we avoid redundant checks
	// and make the function much faster.
	for (const MonsterTypeInfo* monster_info : g_eligible_monsters_for_wave)
	{
		const size_t i = static_cast<size_t>(monster_info->typeId);

		if (g_monsterData.minWaves[i] > ctx.effectiveLevel) {
			continue;
		}

		const bool monster_is_flying = IsFlying(monster_info->typeId);
		if (ctx.isSpawnPointFlying && !monster_is_flying) {
			continue;
		}

		float weight = CalculateBaseWeight(i, ctx);
		weight = ApplySpecialModifiers(weight, i, ctx);
		cache_ref.addMonster(monster_info->typeId, weight);
	}
}

static horde::MonsterTypeID SelectFromCache(const MonsterCache &cache_ref)
{
	if (cache_ref.count == 0 || cache_ref.total_weight <= 0.0f)
		return horde::MonsterTypeID::UNKNOWN;

	const float random_value = frandom() * cache_ref.total_weight;

	// Use std::lower_bound to find the first entry whose cumulative_weight is not less than random_value.
	auto it = std::lower_bound(
		cache_ref.entries.begin(),
		cache_ref.entries.begin() + cache_ref.count, // Search only the valid range
		random_value,
		[](const MonsterCache::Entry &entry, float value)
		{
			return entry.cumulative_weight < value;
		});

	// Robustness check: if lower_bound returns the end iterator, fall back to the last valid element.
	if (it == cache_ref.entries.begin() + cache_ref.count)
	{
		it = std::prev(it);
	}

	return it->typeId;
}

static horde::MonsterTypeID EmergencyFallbackSelection(const MonsterSelectionContext &ctx)
{
	if (developer->integer)
		gi.Com_PrintFmt("G_HordePickMonsterType: Fallback (Lvl: {}, FlyPoint: {})...\n", ctx.currentActualLevel, ctx.isSpawnPointFlying);
	for (const auto &monster : monsterTypes)
	{
		if (monster.minWave <= ctx.currentActualLevel)
		{
			const bool isFlyingMonster = HasWaveType(GetMonsterWaveTypes(monster.typeId), MonsterWaveType::Flying);
			if (!(ctx.isSpawnPointFlying && !isFlyingMonster))
				return monster.typeId;
		}
	}
	return horde::MonsterTypeID::UNKNOWN;
}

//-----------------------------------------------------
// G_HordePickMonsterType - Main Orchestrator (SoA Version)
//-----------------------------------------------------
static horde::MonsterTypeID G_HordePickMonsterType(
	edict_t *spawn_point,
	int32_t currentActualLevel_param,
	MonsterWaveType currentActualWaveType_param,
	bool isRetaliationActive_param,
	bool isRecoveryModeActive_param,
	MonsterWaveType originalWaveTypeBeforeRecovery_param)
{
	if (!spawn_point || !spawn_point->inuse)
		return horde::MonsterTypeID::UNKNOWN;

	// --- 1. Setup Context (no changes) ---
	MonsterSelectionContext ctx;
	ctx.currentActualLevel = currentActualLevel_param;
	ctx.currentActualWaveType = currentActualWaveType_param;
	ctx.isSpawnPointFlying = (spawn_point->style == 1);
	ctx.isRetaliationActive = isRetaliationActive_param;
	ctx.isRecoveryModeActive = isRecoveryModeActive_param;
	ctx.isBossWaveMinionPhase = (ctx.currentActualLevel >= 10 && ctx.currentActualLevel % 5 == 0 && boss_spawned_for_wave);
	ctx.flyingAdjustmentFactor = adjustFlyingSpawnProbability(g_cached_flying_spawn_count);
	ctx.waveTypeForFiltering = isRecoveryModeActive_param ? (HasWaveType(originalWaveTypeBeforeRecovery_param, MonsterWaveType::Flying) ? MonsterWaveType::Flying : MonsterWaveType::Ground) : currentActualWaveType_param;

	// --- 2. Calculate Effective Level (no changes) ---
	bool attemptHigherLevel = ShouldAttemptHigherLevelSpawn(ctx.currentActualLevel, ctx.isRetaliationActive, ctx.isRecoveryModeActive);
	ctx.effectiveLevel = CalculateEffectiveMonsterLevel(ctx.currentActualLevel, attemptHigherLevel, ctx.waveTypeForFiltering);

	// --- 3. Build Cache and Select Monster (no changes) ---
    BuildMonsterCache(g_monster_picker_internal_cache, ctx);
    horde::MonsterTypeID chosen_monster_id = SelectFromCache(g_monster_picker_internal_cache);

    if (chosen_monster_id == horde::MonsterTypeID::UNKNOWN)
    {
        chosen_monster_id = EmergencyFallbackSelection(ctx);
    }

    // =======================================================================
    // --- FINAL CHECK AND FLAG SET ---
    // If we successfully picked a monster AND it was an elite spawn, set the flag.
    // =======================================================================
    if (chosen_monster_id != horde::MonsterTypeID::UNKNOWN && ctx.effectiveLevel > ctx.currentActualLevel)
    {
        const size_t index = static_cast<size_t>(chosen_monster_id);
        if (index < g_monsterData.MONSTER_ARRAY_SIZE && g_monsterData.minWaves[index] > ctx.currentActualLevel)
        {
            g_special_high_level_monster_spawned_this_wave = true;
            if (developer->integer)
            {
                gi.Com_PrintFmt("ELITE SPAWN: '{}' (minWave {}) spawned in wave {}. Flag set.\n",
                                horde::MonsterTypeRegistry::GetClassname(chosen_monster_id),
                                g_monsterData.minWaves[index],
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

void InitializeWaveSystem();

// Guard variable
static bool items_precached = false;

// Renamed function for clarity
static void PrecacheAllGameItems()
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

static void PrecacheWaveSounds()
{
	if (sounds_precached)
		return;

	static const std::array<std::pair<cached_soundindex *, const char *>, 7> individual_sounds = {{{&sound_tele3, "misc/r_tele3.wav"},
																								   {&sound_tele_up, "misc/tele_up.wav"},
																								   {&sound_spawn1, "misc/spawn1.wav"},
																								   {&incoming, "world/incoming.wav"},
																								   {&talk, "misc/talk.wav"},
																								   {&tele1, "misc/tele1.wav"},
																								   {&sound_quake, "world/quake.wav"}}};

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
	constexpr int MAX_ATTEMPTS = 100; // Safety guard against infinite loop
	for (int attempts = 0; attempts < MAX_ATTEMPTS; ++attempts)
	{
		// FIX: Cast the signed result of irandom to the unsigned size_t.
		size_t const index = static_cast<size_t>(irandom(NUM_WAVE_SOUNDS));
		if (!used_wave_sounds[index])
		{
			used_wave_sounds[index] = true;
			remaining_wave_sounds--;
			return wave_sounds[index];
		}
	}
	// Fallback if we somehow fail to find an unused sound
	gi.Com_Print("WARNING: Failed to find unused wave sound, using first available\n");
	return wave_sounds[0];
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
	constexpr int MAX_ATTEMPTS = 100; // Safety guard against infinite loop
	for (int attempts = 0; attempts < MAX_ATTEMPTS; ++attempts)
	{
		// FIX: Cast the signed result of irandom to the unsigned size_t.
		size_t const index = static_cast<size_t>(irandom(NUM_START_SOUNDS));
		if (!used_start_sounds[index])
		{
			used_start_sounds[index] = true;
			remaining_start_sounds--;
			gi.sound(world, CHAN_VOICE, start_sounds[index], 1, ATTN_NONE, 0);
			return;
		}
	}
	// Fallback if we somehow fail to find an unused sound
	gi.Com_Print("WARNING: Failed to find unused start sound, using first available\n");
	gi.sound(world, CHAN_VOICE, start_sounds[0], 1, ATTN_NONE, 0);
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


// This function now uses the family system to efficiently precache monsters for Wave 1
// Subsequent waves are handled by the JIT precacher in Horde_InitLevel.
void PrecacheAllMonsters()
{
	// Only run this initial precache once per map load.
	if (monsters_precached)
	{
		return;
	}

	// Reset all flags to false.
	g_precached_monster_types_flags.fill(false);

	// Track which families we've already precached to avoid duplicates
	std::unordered_set<AssetFamilyID> precached_families;

	if (developer->integer)
	{
		gi.Com_Print("INITIAL PRECACHE: Loading monster families for Wave 1...\n");
	}

	int families_precached_count = 0;
	int monsters_covered_count = 0;

	for (const auto &monster_info : monsterTypes)
	{
		if (monster_info.minWave > 1)
		{
			break;
		}

		// Get the family for this monster
		AssetFamilyID family = GetMonsterAssetFamily(monster_info.typeId);

		// Skip if we've already precached this family
		if (family != AssetFamilyID::UNKNOWN_FAMILY &&
		    precached_families.find(family) == precached_families.end())
		{
			// Precache the entire family
			PrecacheAssetFamily(family);
			precached_families.insert(family);
			families_precached_count++;

			// Mark all members of this family as precached
			auto& family_data = g_asset_families[static_cast<size_t>(family)];
			for (auto member : family_data.members) {
				if (static_cast<size_t>(member) < g_precached_monster_types_flags.size()) {
					g_precached_monster_types_flags[static_cast<size_t>(member)] = true;
					monsters_covered_count++;
				}
			}
		}
		else if (family == AssetFamilyID::UNKNOWN_FAMILY)
		{
			// Fallback for monsters not in a family (shouldn't happen if mapping is complete)
			const char *classname = horde::MonsterTypeRegistry::GetClassname(monster_info.typeId);
			if (classname && *classname)
			{
				edict_t *temp_monster = G_Spawn();
				if (temp_monster)
				{
					temp_monster->classname = classname;
					temp_monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
					ED_CallSpawn(temp_monster);
					if (temp_monster->inuse)
					{
						G_FreeEdict(temp_monster);
					}
					g_precached_monster_types_flags[static_cast<size_t>(monster_info.typeId)] = true;
				}
			}
		}
	}

	if (developer->integer)
	{
		gi.Com_PrintFmt("Precached {} asset families covering {} monsters\n",
			families_precached_count, monsters_covered_count);
	}

	monsters_precached = true;
}

void Horde_Init()
{
	sounds_precached = false;
	items_precached = false;
	ResetBosses();

	// Initialize CVars for precaching control
	if (!g_horde_precache_mode) {
		g_horde_precache_mode = gi.cvar("g_horde_precache_mode", "1", CVAR_SERVERINFO);
	}
	if (!g_horde_precache_window) {
		g_horde_precache_window = gi.cvar("g_horde_precache_window", "3", CVAR_SERVERINFO);
	}
	if (!g_horde_precache_debug) {
		g_horde_precache_debug = gi.cvar("g_horde_precache_debug", "0", CVAR_SERVERINFO);
	}

	// Initialize the asset family system
	InitializeAssetFamilies();

	PrecacheAllGameItems();
	PrecacheWaveSounds();
	monsters_precached = false; // Reset the precache flag for the new map.
	PrecacheAllMonsters();
	InitializeWaveSystem();
	last_wave_number = 0;

	AllowReset();
	g_spawn_map_needs_build = true; // SET THE FLAG INSTEAD
	ResetGame(); // This will call RebuildSpawnPointCacheIfNeeded indirectly or directly

	if (g_horde_precache_debug && g_horde_precache_debug->integer) {
		gi.Com_PrintFmt("Horde precache mode: {} (0=JIT, 1=Smart Window, 2=Full)\n",
			g_horde_precache_mode->integer);
	}

	gi.Com_Print("PRINT: Horde game state initialized with all necessary resources precached.\n");
}

// Helper function to select a suitable boss weapon drop based on wave level
static item_id_t SelectBossWeaponDrop(int32_t wave_level)
{
	// Define potential weapon drops with their minimum required wave level
	static const std::array<std::pair<item_id_t, int32_t>, 8> boss_weapon_drops = { {
		{IT_WEAPON_HYPERBLASTER, 9},
		{IT_WEAPON_RLAUNCHER, 9},
		{IT_WEAPON_PHALANX, 9},	   // Xatrix
		{IT_WEAPON_IONRIPPER, 9},  // Xatrix (originally boomer)
		{IT_WEAPON_PLASMABEAM, 9}, // Rogue
		{IT_WEAPON_RAILGUN, 9},	   // Moved slightly later
		{IT_WEAPON_DISRUPTOR, 9}, // Rogue (originally disintegrator)
		{IT_WEAPON_BFG, 9}		   // Moved slightly later
	} };

	// Use a stack-allocated array to store indices of eligible weapons, avoiding heap allocation.
	std::array<size_t, boss_weapon_drops.size()> eligible_indices;
	size_t eligible_count = 0;

	for (size_t i = 0; i < boss_weapon_drops.size(); ++i)
	{
		if (wave_level >= boss_weapon_drops[i].second)
		{
			// Store the index 'i' if the weapon is eligible.
			eligible_indices[eligible_count++] = i;
		}
	}

	// If no weapons are eligible, return IT_NULL
	if (eligible_count == 0)
	{
		return IT_NULL;
	}

	// Select a random index from our list of eligible indices.
	const size_t random_eligible_index = static_cast<size_t>(irandom(static_cast<int32_t>(eligible_count)));

	// Get the actual index into the main weapon drop array.
	const size_t chosen_weapon_array_index = eligible_indices[random_eligible_index];

	// Return the ID of the chosen weapon.
	return boss_weapon_drops[chosen_weapon_array_index].first;
}


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

	boss_spawned_for_wave = false;
	if (developer->integer)
	{ // Optional debug print
		gi.Com_PrintFmt("BossDeathHandler: Reset boss_spawned_for_wave flag for wave {}.\n", current_wave_level);
	}

	// Clean up entity tracking (no change needed)
	OnEntityDeath(boss);
	OnEntityRemoved(boss);

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

static void ClearBossHealthBar(const edict_t* boss)
{
    if (!boss) return;

    for (size_t i = 0; i < MAX_HEALTH_BARS; ++i) {
        if (level.health_bar_entities[i] && level.health_bar_entities[i]->enemy == boss) {
            G_FreeEdict(level.health_bar_entities[i]);
            level.health_bar_entities[i] = nullptr;
            break; // Found and removed, no need to search further
        }
    }
    // If this boss was the one on the HUD, clear the name.
    // This check is safe even if another boss's bar is now active.
    if (strcmp(gi.get_configstring(CONFIG_HEALTH_BAR_NAME), GetDisplayName(boss)) == 0) {
        gi.configstring(CONFIG_HEALTH_BAR_NAME, "");
    }
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

	ClearBossHealthBar(boss);
}

// IDEAL REPLACEMENT for Horde_AllMonstersDead
// This version uses the reliable GetStroggsNum() for a fast check,
// and only performs a full loop for final cleanup if necessary.
static bool Horde_AllMonstersDead()
{
    // Step 1: Use the new, reliable counter as the primary check. This is our new "fast path".
    if (GetStroggsNum() > 0)
    {
        // If there are any live monsters, we can exit immediately.
        return false;
    }

    // Step 2: If GetStroggsNum() is 0, we know no *live* monsters remain.
    // Now, we must perform a final cleanup pass to handle any entities
    // that are dead but haven't been fully processed (like a boss).
    // This is a rare but important edge case.
    for (auto ent : active_or_dead_monsters())
    {
        // We only care about bosses that are dead but not yet handled.
        if (ent && ent->inuse && ent->monsterinfo.IS_BOSS && ent->health <= 0)
        {
            if (auto_spawned_bosses.count(ent) && !ent->monsterinfo.BOSS_DEATH_HANDLED)
            {
                // This ensures the boss drops its loot and its health bar is cleared.
                boss_die(ent);
            }
        }
    }

    // Step 3: After the check and potential cleanup, we can confidently say the wave is over.
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
	const char* boss_name = GetDisplayName(boss);
	healthbar->message = G_CopyString(boss_name, TAG_LEVEL);

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
bool CheckAndTeleportBoss(edict_t *self, BossTeleportReason reason) // Removed default value to match your likely definition
{
	PROFILE_SCOPE("CheckAndTeleportBoss");
	if (level.intermissiontime)
	{
		return false;
	}

	if (!self || !self->inuse || self->deadflag || !self->monsterinfo.IS_BOSS || !g_horde || !g_horde->integer)
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

	static char last_map_name_boss_teleport[MAX_QPATH] = "";
	static horde::MapID cached_map_id_boss_teleport = horde::MapID::UNKNOWN;
	const char *current_map = GetCurrentMapName();

	if (strcmp(last_map_name_boss_teleport, current_map) != 0)
	{
		Q_strlcpy(last_map_name_boss_teleport, current_map, sizeof(last_map_name_boss_teleport));
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

    // <<< FIX #1: Use self->s.number as the key for the map lookup.
	auto it = last_boss_teleport_attempt_time.find(self->s.number);
	if (it != last_boss_teleport_attempt_time.end() && level.time < it->second + selected_trigger_cooldown)
	{
		// if (developer->integer > 1)
		// {
		// 	gtime_t cooldown_remaining = (it->second + selected_trigger_cooldown) - level.time;
		// 	gi.Com_PrintFmt("CTB: Boss {} on REASON-specific cooldown. Remaining: {:.2f}s\n",
		// 					self->classname ? self->classname : "UNKNOWN",
		// 					cooldown_remaining.seconds());
		// }
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

	// <<< FIX #2: Use self->s.number as the key to update the map.
	last_boss_teleport_attempt_time[self->s.number] = level.time;

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

	const char* boss_display_name = GetDisplayName(self);
	switch (reason)
	{
	case BossTeleportReason::DROWNING:
		if (sound_tele3)
			gi.sound(self, CHAN_AUTO, sound_tele3, 1, ATTN_NORM, 0);
		gi.LocBroadcast_Print(PRINT_HIGH, "{} emerges from the depths!\n", boss_display_name);
		break;
	case BossTeleportReason::TRIGGER_HURT:
		if (sound_tele_up)
			gi.sound(self, CHAN_AUTO, sound_tele_up, 1, ATTN_NORM, 0);
		gi.LocBroadcast_Print(PRINT_HIGH, "{} escapes certain death!\n", boss_display_name);
		break;
	}

	PushEntitiesAway(self->s.origin, 3, 600.0f, 1200.0f, 4000.0f, 1800.0f);

	if (self->inuse && !self->deadflag && self->health > 0)
	{
		SpawnGrow_Spawn(self->s.origin, 100.0f, 15.0f);
	}
	else if (self->inuse && developer->integer > 1)
	{
		gi.Com_PrintFmt("SpawnGrow_Spawn (boss teleport effect) skipped: Boss {} (idx {}) not fully alive. DeadFlag:{}, Health:%.0f\n",
						(self->classname ? self->classname : "Unknown"),
						static_cast<int>(self - g_edicts),
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
	// Early validation
	if (!boss || !boss->inuse)
	{
		gi.configstring(CONFIG_HEALTH_BAR_NAME, "");
		return;
	}

	// --- THE FIX ---
	// 1. Call the _Fast version to get a non-allocating const char*.
	const char* display_name = GetDisplayName(boss);

	// 2. Check if the C-string is null or empty.
	if (!display_name || display_name[0] == '\0')
	{
		gi.configstring(CONFIG_HEALTH_BAR_NAME, "");
		return;
	}

	// 3. Set the configstring directly. The engine will handle copying the string.
	//    There is no need for an intermediate static buffer in this specific case,
	//    as configstring makes its own copy.
	gi.configstring(CONFIG_HEALTH_BAR_NAME, display_name);
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

// NEW FUNCTION: HandleForcedBossRemoval
// Purpose: To handle the removal of a boss that is still alive,
// typically when a new boss is spawning. This function ensures that
// players are credited for the damage dealt to the boss (by attributing
// its remaining health as damage) but prevents the boss from dropping
// items, as it was not legitimately killed.
static void HandleForcedBossRemoval(edict_t *boss)
{
	// 1. Safety checks: Ensure we have a valid, living boss that hasn't been handled yet.
	if (!boss || !boss->inuse || !boss->monsterinfo.IS_BOSS || boss->monsterinfo.BOSS_DEATH_HANDLED || boss->health <= 0)
	{
		return;
	}

	// 2. Mark as handled to prevent this logic from running again.
	boss->monsterinfo.BOSS_DEATH_HANDLED = true;

	// 3. Attribute remaining health as damage to the most appropriate player.
	edict_t *attacker = boss->enemy;

	// Find a valid player attacker. If the boss's last enemy isn't a valid player,
	// fall back to the player who has dealt the most damage during the current wave.
	if (!attacker || !attacker->client || !attacker->inuse || attacker->health <= 0)
	{
		PlayerStats top_damager_stats;
		float percentage;
		CalculateTopDamager(top_damager_stats, percentage);
		if (top_damager_stats.player)
		{
			attacker = top_damager_stats.player;
		}
	}

	if (attacker && attacker->client)
	{
		// Add the boss's remaining health to the attacker's total_damage for the wave.
		attacker->client->total_damage += boss->health;

		if (developer->integer)
		{
				gi.Com_PrintFmt("Forced Boss Removal: Attributed {} remaining HP from '{}' to '{}'.\n",
                boss->health, GetDisplayName(boss), GetPlayerName(attacker));
		}
	}

	// 4. Simulate the boss's death state without triggering normal death effects or drops.
	if (!boss->deadflag)
	{
		level.killed_monsters++; // Ensure the monster count is updated correctly.
	}
	boss->health = 0;
	boss->deadflag = true;
	boss->takedamage = false;
	boss->solid = SOLID_NOT;	   // Make it non-solid.
	boss->svflags |= SVF_NOCLIENT; // Hide it from clients.

	// 5. Clean up the boss's health bar from the HUD.
	ClearBossHealthBar(boss);


	// 6. Notify other systems that the entity is dying and being removed.
	OnEntityDeath(boss);
	OnEntityRemoved(boss);
}
// =======================================================================
// This ensures the area is cleared and entities are pushed away
// BEFORE the boss spawn is finalized, preventing crashes and adding effect.
// =======================================================================
static void SpawnBossAutomatically()
{
	// --- IMMEDIATE GUARD ---
	// This flag prevents multiple bosses from spawning in the same wave due to re-entry.
	if (boss_spawned_for_wave) {
		return;
	}
	boss_spawned_for_wave = true; // Set flag immediately to prevent re-entry

	// --- 1. Cleanup Existing Bosses ---
	// If a previous boss is somehow still alive, we handle its removal gracefully
	// to prevent conflicts and ensure player damage is credited.
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end(); /* no increment */)
	{
		edict_t *existing_boss = *it;
		if (existing_boss && existing_boss->inuse)
		{
			if (existing_boss->health > 0 && !existing_boss->deadflag) {
				HandleForcedBossRemoval(existing_boss);
			} else if (!existing_boss->monsterinfo.BOSS_DEATH_HANDLED) {
				BossDeathHandler(existing_boss);
			}
			OnEntityRemoved(existing_boss);
			G_FreeEdict(existing_boss);
		}
		it = auto_spawned_bosses.erase(it);
	}

	// --- 2. Basic Wave Check ---
	if (current_wave_level < 10 || current_wave_level % 5 != 0) {
		boss_spawned_for_wave = false; // Not a boss wave, reset flag and exit.
		return;
	}

	// --- 3. Select Boss Type ---
	const char *map_name = GetCurrentMapName();
	BossPickResult boss_pick_result = G_HordePickBOSSType(g_horde_local.current_map_size, map_name, current_wave_level);

	if (boss_pick_result.typeId == horde::MonsterTypeID::UNKNOWN) {
		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: Failed to pick a boss type for wave {}.\n", current_wave_level);
		boss_spawned_for_wave = false; // Reset flag on failure
		return;
	}

	// --- 4. Determine Spawn Location from MapOriginRegistry ---
	vec3_t spawn_origin;
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(map_name);
	if (!horde::MapOriginRegistry::GetOrigin(mapId, spawn_origin))
	{
		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: No designated boss spawn origin found for map '{}'. Retrying next frame.\n", map_name);
		boss_spawned_for_wave = false; // Reset flag and try again later
		return;
	}

	// --- 5. PREPARE THE SPAWN AREA (THIS IS THE FIX) ---
	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(boss_pick_result.typeId, predicted_mins, predicted_maxs);
	
	// STEP 5A: Remove any items, defenses, or other blocking entities.
	// This prevents the boss from spawning inside a sentry gun, for example.
	ClearSpawnArea(spawn_origin, predicted_mins, predicted_maxs);

	// STEP 5B: Forcefully push players and monsters away from the spawn point.
	// This creates a dramatic "shockwave" effect and prevents telefrag crashes.
	PushEntitiesAway(spawn_origin, 3, 768.0f, 1500.0f, 4500.0f, 2000.0f);


	// --- 6. Validate the Designated Spawn Location ---
	// After clearing the area, we do a final check to ensure the spot isn't blocked by world geometry.
	if (!IsPositionPhysicallyValid(spawn_origin, predicted_mins, predicted_maxs, IsFlying(boss_pick_result.typeId)))
	{
		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: Designated boss spawn at {} is blocked by world geometry. Retrying next frame.\n", spawn_origin);
		boss_spawned_for_wave = false; // Reset flag and try again later
		return;
	}

	// --- 7. Setup Delayed Spawn ---
	// We spawn a temporary orb to mark the spot and then create the "pre-boss" entity.
	// This entity is not solid and has no logic yet; it's just a placeholder.
	edict_t *orb = G_Spawn();
	if (orb)
	{
		orb->classname = "target_orb";
		orb->s.origin = spawn_origin;
		SP_target_orb(orb);
	}

	edict_t *boss = G_Spawn();
	if (!boss)
	{
		boss_spawned_for_wave = false; // Reset flag on failure
		return;
	}

	boss->classname = horde::MonsterTypeRegistry::GetClassname(boss_pick_result.typeId);
	boss->s.origin = spawn_origin;
	
		boss->s.angles = vec3_origin;


	boss->bossSizeCategory = boss_pick_result.sizeCategory;
	boss->owner = orb;

	// Set the think function to run in 750ms. This is when the boss will actually "materialize".
	boss->nextthink = level.time + 750_ms;
	boss->think = BossSpawnThink;
	gi.linkentity(boss);
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
	if (!current_msg_str.empty())
	{
		current_msg_str += "\n"; // Add a newline before appending
	}
	current_msg_str += message;

	// Ensure the combined message does not exceed configstring limits
	if (current_msg_str.length() >= MAX_STRING_CHARS)
	{
		current_msg_str.resize(MAX_STRING_CHARS - 1); // Truncate to fit
	}

	// Set the combined message back to the configstring
	gi.configstring(CONFIG_HORDEMSG, current_msg_str.c_str());

	// Extend or set the duration for the new combined message
	horde_message_end_time = level.time + duration;
}

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

	self->monsterinfo.IS_BOSS = true;
	self->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	self->monsterinfo.last_reacttodamage_target_time = 0_ms;

	self->solid = SOLID_NOT;
	ED_CallSpawnMonsterByID(self, typeId);

	if (!self->inuse)
	{
		if (developer->integer)
			gi.Com_PrintFmt("BossSpawnThink: ED_CallSpawn failed for boss {}.\n", self->classname ? self->classname : "Unknown");
		boss_spawned_for_wave = false;
		return;
	}

	boss_spawned_for_wave = true;

	// --- IMPROVEMENT: Use AppendHordeMessage for a more dynamic announcement ---
	const char* boss_display_name = GetDisplayName(self);
	if (boss_display_name && boss_display_name[0] != '\0')
	{
		static constexpr std::array<const char *, 6> arrival_phrases = {
			"enters the arena!",
			"has joined the fight!",
			"has spawned!",
			"teleported in!",
			"is here to end this!",
			"makes its presence known!"};
		const char *random_phrase = arrival_phrases[static_cast<size_t>(irandom(static_cast<int32_t>(arrival_phrases.size())))];

		auto announce_message = G_Fmt("\nBOSS: {} {}", boss_display_name, random_phrase);
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
		// FIX: Replaced C-style cast with static_cast.
		gi.Com_PrintFmt("SpawnGrow_Spawn/ImprovedSpawnGrow skipped in BossSpawnThink: Boss {} (idx {}) not fully alive. DeadFlag:{}, Health:%.0f\n",
						(self->classname ? self->classname : "Unknown"),
						static_cast<int>(self - g_edicts),
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

static void ResetRecentBosses()
{
	recent_bosses.clear();
}

static void ResetTeleportTracking()
{
	// Reset recent teleport position history
	g_recent_teleports.positions.fill(vec3_origin);
	g_recent_teleports.teleport_times.fill(0_sec);
	g_recent_teleport_index = 0;

	// Reset global teleport rate limiting
	HordeConstants::g_teleport_rate_count = 0;
	HordeConstants::g_teleport_rate_reset_time = level.time; // Reset the timer completely

	if (developer && developer->integer > 1)
	{ // Optional debug print
		gi.Com_PrintFmt("Teleport tracking reset.\n");
	}
}

// =======================================================================
// This function centralizes the reset logic for all spawn point data and
// re-initializes the global spawn cooldown to a baseline state.
// =======================================================================
static void ResetAllSpawnPointDataAndTrackers()
{
	// 1. Reset all individual spawn point data with a single, efficient call.
	// This clears all arrays within the SoA struct to their default values.
	g_spawnPointsData.clear();

	// 2. Reset global helper trackers.
	horde::g_monsterSpawnTracker.Reset();
	horde::g_spawnPointTimeTracker.Reset();

	// 3. Recalculate the global SPAWN_POINT_COOLDOWN for a fresh start.
	// This logic is a simplified version of what happens in UnifiedAdjustSpawnRate,
	// tailored for a level 0 (reset) state.
	const horde::MapSize &mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level; // Should be 0 after ResetGame
	const int32_t humanPlayers = GetNumHumanPlayers();

	// Start with the base cooldown determined by map size.
	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);

	// Get the scaling factor for the current level (will be 1.0f for level 0).
	const float cooldownScale = CalculateCooldownScale(currentLevel, mapSize);
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	// Apply a small adjustment based on the number of players present at the start.
	if (humanPlayers > 1)
	{
		// Slightly reduce cooldown for more players (e.g., 5% reduction per player, capped).
		const float playerAdjustment = 1.0f - (std::min(humanPlayers - 1, 3) * 0.05f);
		SPAWN_POINT_COOLDOWN *= playerAdjustment;
	}

	// Check difficulty cvars, though they should be 0 after a full reset.
	// This ensures correctness if the function were ever called in another context.
	if ((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer))
	{
		SPAWN_POINT_COOLDOWN *= HordeConstants::TIME_REDUCTION_MULTIPLIER;
	}

	// Finally, clamp the cooldown to ensure it's within reasonable bounds.
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN,
									  HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN,
									  3.0_sec); // A reasonable upper limit for the start.

	// Optional: Log the result for debugging purposes.
	if (developer->integer > 1)
	{
		gi.Com_PrintFmt("ResetAllSpawnPointDataAndTrackers: Complete. Global Cooldown set to: %.2fs\n", SPAWN_POINT_COOLDOWN.seconds());
	}
}

void ResetPlayerDeployedItems()
{
	for (uint32_t i = 0; i < game.maxclients; ++i)
	{
		edict_t *player_ent = g_edicts + 1 + i;
		if (!player_ent || !player_ent->inuse || !player_ent->client)
			continue;

		gclient_t *client = player_ent->client;

		// Lasers
		client->resp.num_lasers = 0;
		std::fill(client->resp.deployed_lasers, client->resp.deployed_lasers + LaserConstants::MAX_LASERS_PER_PLAYER, nullptr);
		client->resp.oldest_tesla_idx = 0; // Assuming this is for tesla index

		// Teslas
		client->resp.num_teslas = 0;
		std::fill(client->resp.deployed_teslas, client->resp.deployed_teslas + TeslaConstants::MAX_TESLAS_PER_PLAYER, nullptr);
		client->resp.oldest_tesla_idx = 0;

		// Traps
		client->resp.num_traps = 0;
		std::fill(client->resp.deployed_traps, client->resp.deployed_traps + TrapConstants::MAX_TRAPS_PER_PLAYER, nullptr);
		client->resp.oldest_trap_idx = 0;

		// Prox Mines
		client->resp.num_proxs = 0;
		std::fill(client->resp.deployed_proxs, client->resp.deployed_proxs + ProxConstants::MAX_PROXS_PER_PLAYER, nullptr);
		client->resp.oldest_prox_idx = 0;

		// Sentries
		client->resp.num_sentries = 0;
		std::fill(client->resp.deployed_sentries, client->resp.deployed_sentries + SentryConstants::MAX_SENTRIES_PER_PLAYER, nullptr);

		// Timers
		client->resp.teleport_cooldown = 3_sec; // Reset to default or 0_sec if appropriate
		client->resp.lasthbshot = 0_sec;

		// Horde specific state
		client->last_wave_timer_horde_update = 0;
		Q_strlcpy(client->voted_map, "", sizeof(client->voted_map));
		client->emergency_teleport = false;
	}
}

// In g_horde.cpp

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

	g_horde_retaliation_end_time = 0_sec;
	g_horde_retaliation_last_trigger_time = 0_sec;
	g_horde_retaliation_target_player = nullptr;

	//resetting idview special entities
	g_targetable_special_entities.clear();
	// recent spawns
	g_recent_spawns.positions.fill(vec3_origin);
	g_recent_spawns.cooldowns_until.fill(0_sec);
	g_recent_spawn_index = 0;

	// FIX: Clear the boss teleport map on map change ---
	last_boss_teleport_attempt_time.clear();

	// recent teleport
	ResetTeleportTracking();
	ResetPlayerDeployedItems();
	HordeConstants::g_teleport_rate_count = 0;
	HordeConstants::g_teleport_rate_reset_time = level.time;

	ResetRecentBosses();
	ResetAmbushSystem();
	ResetWaveMemory();
	ResetChampionMonsterState();
	ResetBosses();

    // --- FIX: Clear all global entity state maps ---
	g_emitter_states.clear();
	g_trap_states.clear();
	
    // =======================================================================
	// --- UNIFIED RESET (THIS IS THE FIX) ---
    // =======================================================================
    g_spawn_plan.clear();
    g_special_spawn_state.clear(); // This replaces the old ambush/retaliation resets
    // REMOVED: g_horde_retaliation_active - using g_special_spawn_state.type instead
    g_horde_retaliation_end_time = 0_sec;
    g_horde_retaliation_target_player = nullptr;
    // =======================================================================

	g_adjusted_monster_cap = 0;
	consistent_zero_counts = 0;
	counter_mismatch_frames = 0;
	horde_message_end_time = 0_sec;
	g_totalMonstersInWave = 0;

	CleanupSpawnPointCache();

	ResetAllSpawnPointDataAndTrackers();
	need_spawn_cache_reset = true;
	need_frame_timer_reset = true;
	need_queue_monitor_reset = true;
	g_consecutive_spawn_failures = 0;
	g_recovery_mode_active = false;
	g_original_wave_type_before_recovery = MonsterWaveType::None;

	g_horde_local = HordeState();
	g_horde_local.level = 0;
	current_wave_level = 0;
	boss_spawned_for_wave = false;
	next_wave_message_sent = false;
	allowWaveAdvance = false;

	ResetBenefits();
	ResetWaveAdvanceState();

	g_horde_local.update_map_size(GetCurrentMapName());

    // Cvar resets...
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

	// Reset precaching system but don't actually clear cached assets
	// (they persist in engine memory across resets)
	ResetPrecacheTracking();

	std::fill(used_wave_sounds.begin(), used_wave_sounds.end(), false);
	remaining_wave_sounds = NUM_WAVE_SOUNDS;
	std::fill(used_start_sounds.begin(), used_start_sounds.end(), false);
	remaining_start_sounds = NUM_START_SOUNDS;

	ClearHordeMessage();
	g_horde_local.reset_hud_state();

	gi.Com_PrintFmt("INFO: Horde game state reset complete.\n");
}

int32_t CalculateRemainingMonsters() noexcept
{
	// Simple counter approach - more reliable from old version
	const int32_t remaining = GetStroggsNum();
	return std::max(0, remaining); // Ensure non-negative
}

// --- Helper Struct to pass context around ---
struct WaveConditionContext
{
	const gtime_t currentTime;
	const int32_t liveMonsters;
	const int32_t currentLevel;
	const float remainingPercentage;
	const bool isBossWaveActive;
	const horde::MapSize &mapSize;
	const ConditionParams &params;
};

// --- Forward declarations for new helper functions ---
static void StartConditionalTimer(const WaveConditionContext &ctx);
static void ApplyAggressiveTimeReduction(const WaveConditionContext &ctx);
static void IssueTimeWarnings();

// This is the new main function that orchestrates the wave end checks.
static bool CheckRemainingMonstersCondition(const horde::MapSize &mapSize, WaveEndReason &reason)
{
	// --- 1. Setup Context ---
	// Cache all frequently used values once at the start.
	const WaveConditionContext ctx = {
		.currentTime = level.time,
		.liveMonsters = CalculateRemainingMonsters(),
		.currentLevel = current_wave_level,
		.remainingPercentage = static_cast<float>(CalculateRemainingMonsters()) / ((g_totalMonstersInWave > 0) ? g_totalMonstersInWave : 1),
		.isBossWaveActive = IsBossWave(),
		.mapSize = mapSize,
		.params = g_lastParams};

	// --- 2. Immediate Win/Advance Condition ---
	// Check if the wave is over because all monsters are gone or an admin forced it.
	if (allowWaveAdvance || (ctx.liveMonsters == 0 && g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters <= 0))
	{
		if (Horde_AllMonstersDead())
		{
			reason = WaveEndReason::AllMonstersDead;
			ResetWaveAdvanceState();
			return true;
		}
		else if (developer->integer)
		{
			// This warning is useful for debugging desyncs between counters and reality.
			gi.Com_PrintFmt("WARN: CheckRemaining: Pools empty, live count 0, but Horde_AllMonstersDead is false. Live: {}, NumSpawn: {}, Queued: {}. Totalalives: {}.\n",
							ctx.liveMonsters, g_horde_local.num_to_spawn, g_horde_local.queued_monsters, GetStroggsNum());
		}
	}

	// --- 3. Absolute Failsafe Timer ---
	// A hard time limit for the wave, regardless of other conditions.
	if (ctx.currentTime >= g_independent_timer_start + ctx.params.independentTimeThreshold)
	{
		reason = WaveEndReason::TimeLimitReached;
		if (developer->integer)
			gi.Com_PrintFmt("Wave ended: Independent time limit reached ({:.1f}s).\n", ctx.params.independentTimeThreshold.seconds());
		return true;
	}

	// --- 4. Conditional Timer Logic ---
	// This block manages the "mop-up" timer that starts when the wave is winding down.
	if (!g_horde_local.conditionTriggered)
	{
		// If the timer hasn't started yet, check if it should.
		StartConditionalTimer(ctx);
	}
	else
	{
		// If the timer is running, check if it should be shortened.
		ApplyAggressiveTimeReduction(ctx);
	}

	// --- 5. Issue Warnings and Check for Timer Expiry ---
	// This runs only if a timer (either the main one or the conditional one) is active.
	bool should_issue_warnings = g_horde_local.conditionTriggered || (g_horde_local.state == horde_state_t::active_wave && next_wave_message_sent);
	if (should_issue_warnings)
	{
		IssueTimeWarnings();
	}

	// Check if the conditional timer has run out.
	if (g_horde_local.conditionTriggered && ctx.currentTime >= g_horde_local.waveEndTime)
	{
		reason = WaveEndReason::MonstersRemaining;
		if (developer->integer)
			gi.Com_PrintFmt("Wave ended: Conditional timer expired. Live: {}, Queued: {}.\n", ctx.liveMonsters, g_horde_local.queued_monsters);
		return true;
	}

	// --- 6. Special Mop-Up Condition for High Levels ---
	// End the wave early if it's a high level, very few monsters are left, and most of the timer has passed.
	if (ctx.currentLevel >= 15 && ctx.liveMonsters <= 3 && g_horde_local.queued_monsters < 2 && g_horde_local.conditionTriggered)
	{
		const gtime_t elapsed_since_condition_start = ctx.currentTime - g_horde_local.conditionStartTime;
		if (g_horde_local.conditionTimeThreshold > 0_sec &&
			elapsed_since_condition_start >= (g_horde_local.conditionTimeThreshold * 0.7f))
		{
			reason = WaveEndReason::MonstersRemaining;
			if (developer->integer)
				gi.Com_PrintFmt("Wave ended: High level, few monsters, 70%% of conditional timer elapsed.\n");
			return true;
		}
	}

	// If no end condition was met, the wave continues.
	return false;
}

// Helper to start the conditional "mop-up" timer.
static void StartConditionalTimer(const WaveConditionContext &ctx)
{
	// Determine if a trigger condition is met.
	const bool maxMonstersReached = !next_wave_message_sent && (ctx.liveMonsters <= ctx.params.maxMonsters);
	const bool lowPercentageReached = !next_wave_message_sent && (ctx.remainingPercentage <= ctx.params.lowPercentageThreshold);
	const bool postDeploymentTrigger = next_wave_message_sent;

	if (!maxMonstersReached && !lowPercentageReached && !postDeploymentTrigger)
	{
		return; // No trigger, do nothing.
	}

	// Set the base duration for the timer.
	gtime_t time_threshold;
	if (postDeploymentTrigger)
	{
		time_threshold = ctx.params.timeThreshold;
	}
	else if (maxMonstersReached && lowPercentageReached)
	{
		time_threshold = std::min(ctx.params.timeThreshold, ctx.params.lowPercentageTimeThreshold);
	}
	else
	{
		time_threshold = maxMonstersReached ? ctx.params.timeThreshold : ctx.params.lowPercentageTimeThreshold;
	}

	// Add a bonus to the timer if there are still monsters in the queue.
	if (g_horde_local.queued_monsters > 5)
	{
		const float bonus_per_monster = postDeploymentTrigger ? 0.5f : 0.3f;
		const gtime_t max_bonus = postDeploymentTrigger ? 15_sec : 10_sec;
		gtime_t queue_bonus = gtime_t::from_sec(static_cast<float>(g_horde_local.queued_monsters) * bonus_per_monster);
		time_threshold += std::min(queue_bonus, max_bonus);
	}

	// Activate the timer state.
	g_horde_local.conditionTriggered = true;
	g_horde_local.conditionStartTime = ctx.currentTime;
	g_horde_local.conditionTimeThreshold = time_threshold;
	g_horde_local.waveEndTime = ctx.currentTime + time_threshold;

	// Special reduction for high-level waves with very few monsters.
	if (ctx.currentLevel >= 15 && ctx.liveMonsters <= 5 && g_horde_local.queued_monsters < 3)
	{
		const float reduction_factor = 0.6f;
		g_horde_local.waveEndTime = ctx.currentTime + std::max(1_sec, time_threshold * reduction_factor);
	}

	if (developer->integer)
	{
		const char *trigger_reason = postDeploymentTrigger ? "Post-Deploy" : (maxMonstersReached ? "MaxMonsters" : "LowPercentage");
		gi.Com_PrintFmt("Conditional timer started ({:.1f}s). Trigger: {}. Queue: {}.\n",
						time_threshold.seconds(), trigger_reason, g_horde_local.queued_monsters);
	}
}

// Helper to apply the aggressive time reduction when few monsters are left.
static void ApplyAggressiveTimeReduction(const WaveConditionContext &ctx)
{
	// This logic only applies if few monsters are left alive and in the queue.
	if (ctx.liveMonsters > HordeConstants::MONSTERS_FOR_AGGRESSIVE_REDUCTION || g_horde_local.queued_monsters >= 3)
	{
		return;
	}

	// --- Calculate the new, shorter time limit ---
	float base_time = 6.0f + (ctx.liveMonsters * 1.5f);

	// Map size multiplier
	if (ctx.mapSize.isSmallMap)
		base_time *= 1.3f;
	else if (ctx.mapSize.isMediumMap)
		base_time *= 1.15f;

	// Boss wave multiplier
	if (ctx.isBossWaveActive && boss_spawned_for_wave)
	{
		base_time *= 2.0f + (0.2f * ctx.liveMonsters);
	}
	else
	{
		// High level reduction
		if (ctx.currentLevel >= 15)
		{
			float reduction = std::min((ctx.currentLevel - 15) * 0.02f, 0.3f);
			base_time *= (1.0f - reduction);
		}
		// Player count reduction (now called infrequently)
		int32_t playerCount = GetNumHumanPlayers();
		if (playerCount > 1)
		{
			float player_reduction = std::min((playerCount - 1) * 0.07f, 0.2f);
			base_time *= (1.0f - player_reduction);
		}
	}

	// Determine minimum allowed time
	float min_time = (ctx.isBossWaveActive && boss_spawned_for_wave) ? 7.0f : 5.0f;
	if (!ctx.isBossWaveActive || !boss_spawned_for_wave)
	{
		if (ctx.mapSize.isSmallMap)
			min_time *= 1.3f;
		else if (ctx.mapSize.isMediumMap)
			min_time *= 1.15f;
	}

	gtime_t aggressive_time = gtime_t::from_sec(std::max(min_time, base_time));

	// --- Apply the new time if it's shorter than the current remaining time ---
	const gtime_t original_remaining_conditional = (g_horde_local.waveEndTime > ctx.currentTime) ? (g_horde_local.waveEndTime - ctx.currentTime) : 0_sec;
	if (original_remaining_conditional > 0_sec && aggressive_time < original_remaining_conditional)
	{
		g_horde_local.waveEndTime = ctx.currentTime + aggressive_time;
		if (developer->integer)
		{
			gi.Com_PrintFmt("Aggressive time reduction: New limit is {:.1f}s for {} monsters.\n",
							aggressive_time.seconds(), ctx.liveMonsters);
		}
	}
}

// Helper to issue the 30, 10, 5 second warnings.
static void IssueTimeWarnings()
{
	const gtime_t actualRelevantRemainingTime = GetWaveTimer();
	if (actualRelevantRemainingTime <= 0_sec)
	{
		return;
	}

	for (size_t i = 0; i < WARNING_TIMES.size(); ++i)
	{
		const gtime_t warning_time = gtime_t::from_sec(WARNING_TIMES[i]);
		// Check if the timer is within a 1-second window of the warning time.
		if (!g_horde_local.warningIssued[i] &&
			actualRelevantRemainingTime <= warning_time &&
			actualRelevantRemainingTime > warning_time - 1_sec)
		{
			gi.LocBroadcast_Print(PRINT_HIGH, "{} seconds remaining!\n", static_cast<int>(WARNING_TIMES[i]));
			g_horde_local.warningIssued[i] = true;
		}
	}
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
	// g_lastNumHumanPlayers = -1;

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
	Horde_InitLevel(current_wave_level + 1);

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

inline int8_t GetNumSpectPlayers()
{
	const auto &players = active_players();
	return std::count_if(players.begin(), players.end(),
						 [](const edict_t *const player)
						 {
							 return ClientIsSpectating(player->client);
						 });
}

// Implementación de DisplayWaveMessage
static void DisplayWaveMessage(gtime_t duration = 5_sec)
{
	static const std::array<const char *, 6> messages = {
		"Horde Menu available upon opening Inventory or using TURTLE on POWERUP WHEEL\n\nMAKE THEM PAY!\n",
		"Welcome to Hell.\n\nUse FlipOff <Key> looking at walls to spawn lasers (cost: 25 cells)\n",
		"New Tactics!\n\nTeslas/Traps can now be placed on walls and ceilings!\n\nUse them wisely!",
		"Adrenalines will improve sentry guns and traps/teslas",
		"Improved Traps!\n\nTraps are ready again after 5sec of eating a strogg!",
		"You can choose your own path on bonuses, check Horde Menu!"};

	// Usar distribución uniforme con mt_rand
	std::uniform_int_distribution<size_t> dist(0, messages.size() - 1);
	const size_t choice = dist(mt_rand);
	UpdateHordeMessage(messages[choice], duration);
}

void HandleWaveCleanupMessage(const horde::MapSize &mapSize, const WaveEndReason reason)
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

// MODIFIED FUNCTION: AnnounceIncomingWave
static void AnnounceIncomingWave(gtime_t duration)
{
	const char* message;

	// Define message pools for each difficulty level
	static constexpr std::array<const char*, 4> normal_messages = {
		"Strogg forces are pushing! Stay alert!",
		"Incoming wave detected! Hold position!",
		"Prepare for the next assault!",
		"The horde advances! Brace yourselves!" };

	static constexpr std::array<const char*, 4> chaotic1_messages = {
		"Chaotic wave incoming! Steel yourself!",
		"Chaos approaches! Ready for battle!",
		"The horde is restless! Expect the unexpected!",
		"Unpredictable forces approaching! Adapt or die!" };

	static constexpr std::array<const char*, 4> chaotic2_messages = {
		"Relentless wave incoming! Stand your ground!",
		"Overwhelming forces approaching! Hold the line!",
		"The horde shows no mercy! Fight with all you have!",
		"An unstoppable tide approaches! This is it!" };

	static constexpr std::array<const char*, 4> insane1_messages = {
		"Intense wave incoming! Show no mercy!",
		"Fierce battle ahead! Stand ready!",
		"The Strogg are enraged! Push them back!",
		"Survival is not guaranteed! Fight for every inch!" };

	// Expanded and refined insane2_messages
	static constexpr std::array<const char*, 5> insane2_messages = {
		// Increased size to 5
		"This is hell! Make it count!",			  // Keep this classic
		"No retreat! Fight until your last breath!",	  // Desperate, but determined
		"Overwhelmed! Make them pay for every inch!",	  // Focus on making them suffer
		"They're everywhere! Don't give an inch!",		  // Sense of being surrounded
		"Looks like a glorious death! Take 'em with you!" // Dark humor
	};

	// Select message based on difficulty and level
	if (g_chaotic->integer > 0 && current_wave_level >= 5)
	{
		if (g_chaotic->integer == 2)
		{
			// FIX: Cast the result of irandom to size_t for array indexing.
			message = chaotic2_messages[static_cast<size_t>(irandom(static_cast<int32_t>(chaotic2_messages.size())))];
		}
		else // g_chaotic->integer == 1
		{
			// FIX: Cast the result of irandom to size_t for array indexing.
			message = chaotic1_messages[static_cast<size_t>(irandom(static_cast<int32_t>(chaotic1_messages.size())))];
		}
	}
	else if (g_insane->integer > 0)
	{
		if (g_insane->integer == 2)
		{
			// FIX: Cast the result of irandom to size_t for array indexing.
			message = insane2_messages[static_cast<size_t>(irandom(static_cast<int32_t>(insane2_messages.size())))];
		}
		else // g_insane->integer == 1
		{
			// FIX: Cast the result of irandom to size_t for array indexing.
			message = insane1_messages[static_cast<size_t>(irandom(static_cast<int32_t>(insane1_messages.size())))];
		}
	}
	else // Normal difficulty
	{
		// FIX: Cast the result of irandom to size_t for array indexing.
		message = normal_messages[static_cast<size_t>(irandom(static_cast<int32_t>(normal_messages.size())))];
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

// Initialize the asset family system - maps monsters to families and sets up shared assets
void InitializeAssetFamilies()
{
	// Clear all mappings first
	g_monster_to_family.fill(AssetFamilyID::UNKNOWN_FAMILY);

	// Initialize all families as not precached
	for (auto& family : g_asset_families) {
		family.is_precached = false;
		family.members.clear(); // Clear any existing members
	}

	// Initialize family names for debugging
	g_asset_families[static_cast<size_t>(AssetFamilyID::SOLDIER_FAMILY)].family_name = "SOLDIER_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::TANK_FAMILY)].family_name = "TANK_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::GLADIATOR_FAMILY)].family_name = "GLADIATOR_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::GUNNER_FAMILY)].family_name = "GUNNER_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::INFANTRY_FAMILY)].family_name = "INFANTRY_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::ARACHNID_FAMILY)].family_name = "ARACHNID_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::GUARDIAN_FAMILY)].family_name = "GUARDIAN_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::SUPERTANK_FAMILY)].family_name = "SUPERTANK_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::FIXBOT_FAMILY)].family_name = "FIXBOT_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::TURRET_FAMILY)].family_name = "TURRET_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::DAEDALUS_FAMILY)].family_name = "DAEDALUS_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::PARASITE_FAMILY)].family_name = "PARASITE_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::BOSS2_FAMILY)].family_name = "BOSS2_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::CARRIER_FAMILY)].family_name = "CARRIER_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::WIDOW_FAMILY)].family_name = "WIDOW_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::SHAMBLER_FAMILY)].family_name = "SHAMBLER_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::MAKRON_FAMILY)].family_name = "MAKRON_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::BERSERK_FAMILY)].family_name = "BERSERK_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::BRAIN_FAMILY)].family_name = "BRAIN_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::CHICK_FAMILY)].family_name = "CHICK_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::FLYER_FAMILY)].family_name = "FLYER_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::HOVER_FAMILY)].family_name = "HOVER_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::MEDIC_FAMILY)].family_name = "MEDIC_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::MUTANT_FAMILY)].family_name = "MUTANT_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::FLOATER_FAMILY)].family_name = "FLOATER_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::STALKER_FAMILY)].family_name = "STALKER_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::GEKK_FAMILY)].family_name = "GEKK_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::INSANE_FAMILY)].family_name = "INSANE_FAMILY";
	g_asset_families[static_cast<size_t>(AssetFamilyID::MISC_FAMILY)].family_name = "MISC_FAMILY";

	// --- Map monsters to their families ---

	// SOLDIER FAMILY - All soldier variants share base models
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SOLDIER)] = AssetFamilyID::SOLDIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SOLDIER_LIGHT)] = AssetFamilyID::SOLDIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SOLDIER_SS)] = AssetFamilyID::SOLDIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SOLDIER_HYPERGUN)] = AssetFamilyID::SOLDIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SOLDIER_RIPPER)] = AssetFamilyID::SOLDIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SOLDIER_LASERGUN)] = AssetFamilyID::SOLDIER_FAMILY;

	// TANK FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::TANK)] = AssetFamilyID::TANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::TANK_COMMANDER)] = AssetFamilyID::TANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::TANK_64)] = AssetFamilyID::TANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::TANK_SPAWNER)] = AssetFamilyID::TANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::RUNNERTANK)] = AssetFamilyID::TANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::EASTERTANK)] = AssetFamilyID::TANK_FAMILY;

	// GLADIATOR FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::GLADIATOR)] = AssetFamilyID::GLADIATOR_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::GLADIATOR_B)] = AssetFamilyID::GLADIATOR_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::GLADIATOR_C)] = AssetFamilyID::GLADIATOR_FAMILY;

	// GUNNER FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::GUNNER)] = AssetFamilyID::GUNNER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::GUNNER_VANILLA)] = AssetFamilyID::GUNNER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::GUNCMDR)] = AssetFamilyID::GUNNER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::GUNCMDR_VANILLA)] = AssetFamilyID::GUNNER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::GUNCMDR_KL)] = AssetFamilyID::GUNNER_FAMILY;

	// INFANTRY FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::INFANTRY)] = AssetFamilyID::INFANTRY_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::INFANTRY_VANILLA)] = AssetFamilyID::INFANTRY_FAMILY;

	// ARACHNID FAMILY - All spider variants
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SPIDER)] = AssetFamilyID::ARACHNID_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::ARACHNID)] = AssetFamilyID::ARACHNID_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::ARACHNID2)] = AssetFamilyID::ARACHNID_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::PSX_ARACHNID)] = AssetFamilyID::ARACHNID_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::GM_ARACHNID)] = AssetFamilyID::ARACHNID_FAMILY;

	// GUARDIAN FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::GUARDIAN)] = AssetFamilyID::GUARDIAN_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::PSX_GUARDIAN)] = AssetFamilyID::GUARDIAN_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::JANITOR2)] = AssetFamilyID::GUARDIAN_FAMILY;

	// SUPERTANK FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::JANITOR)] = AssetFamilyID::SUPERTANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::BOSS5)] = AssetFamilyID::SUPERTANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SUPERTANK)] = AssetFamilyID::SUPERTANK_FAMILY;

	// FIXBOT FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::FIXBOT)] = AssetFamilyID::FIXBOT_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::FIXBOT_KL)] = AssetFamilyID::FIXBOT_FAMILY;

	// TURRET FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SENTRYGUN)] = AssetFamilyID::TURRET_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::TURRET)] = AssetFamilyID::TURRET_FAMILY;

	// DAEDALUS FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::DAEDALUS)] = AssetFamilyID::DAEDALUS_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::DAEDALUS_BOMBER)] = AssetFamilyID::DAEDALUS_FAMILY;

	// PARASITE FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::PARASITE)] = AssetFamilyID::PARASITE_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::PERRO_KL)] = AssetFamilyID::PARASITE_FAMILY;

	// BOSS2 FAMILY (Hornet variants)
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::BOSS2)] = AssetFamilyID::BOSS2_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::BOSS2_64)] = AssetFamilyID::BOSS2_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::BOSS2_MINI)] = AssetFamilyID::BOSS2_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::BOSS2_KL)] = AssetFamilyID::BOSS2_FAMILY;

	// CARRIER FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::CARRIER)] = AssetFamilyID::CARRIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::CARRIER_MINI)] = AssetFamilyID::CARRIER_FAMILY;

	// WIDOW FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::WIDOW)] = AssetFamilyID::WIDOW_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::WIDOW1)] = AssetFamilyID::WIDOW_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::WIDOW2)] = AssetFamilyID::WIDOW_FAMILY;

	// SHAMBLER FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SHAMBLER)] = AssetFamilyID::SHAMBLER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SHAMBLER_SMALL)] = AssetFamilyID::SHAMBLER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::SHAMBLER_KL)] = AssetFamilyID::SHAMBLER_FAMILY;

	// MAKRON/JORG FAMILY
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::MAKRON)] = AssetFamilyID::MAKRON_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::MAKRON_KL)] = AssetFamilyID::MAKRON_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::JORG)] = AssetFamilyID::MAKRON_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::JORG_SMALL)] = AssetFamilyID::MAKRON_FAMILY;

	// Individual unique monsters
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::BERSERK)] = AssetFamilyID::BERSERK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::BRAIN)] = AssetFamilyID::BRAIN_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::CHICK)] = AssetFamilyID::CHICK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::CHICK_HEAT)] = AssetFamilyID::CHICK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::EASTERCHICK)] = AssetFamilyID::CHICK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::EASTERCHICK2)] = AssetFamilyID::CHICK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::FLYER)] = AssetFamilyID::FLYER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::KAMIKAZE)] = AssetFamilyID::FLYER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::HOVER)] = AssetFamilyID::HOVER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::HOVER_VANILLA)] = AssetFamilyID::HOVER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::MEDIC)] = AssetFamilyID::MEDIC_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::MEDIC_COMMANDER)] = AssetFamilyID::MEDIC_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::MUTANT)] = AssetFamilyID::MUTANT_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::REDMUTANT)] = AssetFamilyID::MUTANT_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::FLOATER)] = AssetFamilyID::FLOATER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::FLOATER_TRACKER)] = AssetFamilyID::FLOATER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::STALKER)] = AssetFamilyID::STALKER_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::GEKK)] = AssetFamilyID::GEKK_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::MISC_INSANE)] = AssetFamilyID::INSANE_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::COMMANDER_BODY)] = AssetFamilyID::MISC_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::BOSS3_STAND)] = AssetFamilyID::MISC_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::BIGVIPER)] = AssetFamilyID::MISC_FAMILY;
	g_monster_to_family[static_cast<size_t>(horde::MonsterTypeID::FLIPPER)] = AssetFamilyID::MISC_FAMILY;

	// Now populate the members lists for each family
	for (size_t i = 0; i < g_monster_to_family.size(); ++i) {
		AssetFamilyID family = g_monster_to_family[i];
		if (family != AssetFamilyID::UNKNOWN_FAMILY && family < AssetFamilyID::MAX_FAMILIES) {
			g_asset_families[static_cast<size_t>(family)].members.push_back(static_cast<horde::MonsterTypeID>(i));
		}
	}

	if (g_horde_precache_debug && g_horde_precache_debug->integer) {
		gi.Com_Print("Asset families initialized - monsters mapped to families\n");
		// Debug output showing family sizes
		for (size_t i = 0; i < static_cast<size_t>(AssetFamilyID::MAX_FAMILIES); ++i) {
			auto& family = g_asset_families[i];
			if (!family.members.empty()) {
				gi.Com_PrintFmt("  {} has {} members\n", family.family_name, family.members.size());
			}
		}
	}
}

// Get the asset family for a specific monster
AssetFamilyID GetMonsterAssetFamily(horde::MonsterTypeID monster_id) {
	size_t index = static_cast<size_t>(monster_id);
	if (index >= g_monster_to_family.size()) {
		return AssetFamilyID::UNKNOWN_FAMILY;
	}
	return g_monster_to_family[index];
}

// Check if we can safely precache a family without hitting limits
bool CanPrecacheFamily(AssetFamilyID family_id) {
	if (family_id == AssetFamilyID::UNKNOWN_FAMILY || family_id >= AssetFamilyID::MAX_FAMILIES) {
		return false;
	}

	// Check if already precached
	if (g_asset_families[static_cast<size_t>(family_id)].is_precached) {
		return true; // Already done, so "can" precache (no-op)
	}

	// For now, simple check - could be enhanced with actual asset counting
	// Conservative limit: stop at 80% of max to leave room for other assets
	const int32_t MAX_SAFE_MODELS = static_cast<int32_t>(MAX_MODELS * 0.8);
	const int32_t MAX_SAFE_SOUNDS = static_cast<int32_t>(MAX_SOUNDS * 0.8);

	if (g_total_precached_models >= MAX_SAFE_MODELS || g_total_precached_sounds >= MAX_SAFE_SOUNDS) {
		if (g_horde_precache_debug && g_horde_precache_debug->integer) {
			gi.Com_PrintFmt("Warning: Approaching precache limits (models: {}/{}, sounds: {}/{})\n",
				g_total_precached_models, MAX_MODELS, g_total_precached_sounds, MAX_SOUNDS);
		}
		return false;
	}

	return true;
}

// Precache all assets for a given family
void PrecacheAssetFamily(AssetFamilyID family_id) {
	if (family_id == AssetFamilyID::UNKNOWN_FAMILY || family_id >= AssetFamilyID::MAX_FAMILIES) {
		return;
	}

	auto& family = g_asset_families[static_cast<size_t>(family_id)];

	// Skip if already precached
	if (family.is_precached) {
		return;
	}

	// Check if we can safely precache
	if (!CanPrecacheFamily(family_id)) {
		if (g_horde_precache_debug && g_horde_precache_debug->integer) {
			gi.Com_PrintFmt("Cannot precache family {} - limits would be exceeded\n", family.family_name);
		}
		return;
	}

	// Precache the first member of the family to get all shared assets
	// The spawn function will precache all models/sounds
	if (!family.members.empty()) {
		horde::MonsterTypeID first_member = family.members[0];
		const char* classname = horde::MonsterTypeRegistry::GetClassname(first_member);

		if (classname && *classname) {
			if (g_horde_precache_debug && g_horde_precache_debug->integer) {
				gi.Com_PrintFmt("Precaching family {} via {}\n", family.family_name, classname);
			}

			// Create temporary entity to trigger precaching
			edict_t* temp_monster = G_Spawn();
			if (temp_monster) {
				temp_monster->classname = classname;
				temp_monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
				ED_CallSpawnMonsterByID(temp_monster, first_member);
				if (temp_monster->inuse) {
					G_FreeEdict(temp_monster);
				}
			}
		}
	}

	family.is_precached = true;

	// Update counts (approximate - would need actual tracking for accuracy)
	g_total_precached_models += 5; // Rough estimate per family
	g_total_precached_sounds += 10; // Rough estimate per family
}

// Precache a monster by using its family
void PrecacheMonsterByFamily(horde::MonsterTypeID monster_id) {
	AssetFamilyID family = GetMonsterAssetFamily(monster_id);
	if (family != AssetFamilyID::UNKNOWN_FAMILY) {
		PrecacheAssetFamily(family);
	}
}

// Reset precache tracking (e.g., on map change or reset)
void ResetPrecacheTracking() {
	// Clear tracking sets
	g_precached_models.clear();
	g_precached_sounds.clear();
	g_total_precached_models = 0;
	g_total_precached_sounds = 0;

	// Reset family precache flags
	for (auto& family : g_asset_families) {
		family.is_precached = false;
	}

	// Reset monster precache flags
	g_precached_monster_types_flags.fill(false);
	g_full_precache_done = false;
	monsters_precached = false;

	if (g_horde_precache_debug && g_horde_precache_debug->integer) {
		gi.Com_Print("Precache tracking reset\n");
	}
}

void InitializeWaveSystem()
{
	PrecacheWaveSounds();
}

static void SetMonsterArmor(edict_t *monster);
static void SetNextMonsterSpawnTime(const horde::MapSize &mapSize);

// =======================================================================
// This version fixes the bug preventing alternative spawns
// =======================================================================
[[nodiscard]] bool IsPositionPhysicallyValid(vec3_t &io_position, const vec3_t &monster_mins, const vec3_t &monster_maxs, bool is_flying)
{
    // 1. Basic checks from your new version (these are good).
    if (!is_valid_vector(io_position)) return false;

    // Check for dangerous liquids (water/lava/slime) - monsters shouldn't spawn in them
    contents_t point_contents = gi.pointcontents(io_position);
    if (point_contents & MASK_SOLID) return false;

    // Prevent spawning in hazardous liquids unless flying
    if (!is_flying && (point_contents & (CONTENTS_LAVA | CONTENTS_SLIME))) {
        return false;
    }

    // Water is ok for some monsters but should be avoided for most ground units
    if (!is_flying && (point_contents & CONTENTS_WATER)) {
        // Allow water spawning only if it's shallow (check if head would be above water)
        vec3_t head_pos = io_position;
        head_pos.z += monster_maxs.z - monster_mins.z; // Approximate height
        if (gi.pointcontents(head_pos) & CONTENTS_WATER) {
            return false; // Would be fully submerged
        }
    }

    // 2. The robust single trace from your OLD version's logic. This is the key fix.
    // It checks against both world geometry AND other solid monsters in one go.
    trace_t trace = gi.trace(io_position, monster_mins, monster_maxs, io_position, nullptr, MASK_MONSTERSOLID);
    if (trace.startsolid)
    {
        // If the entire volume is occupied by anything solid (world or monster), it's invalid.
        return false;
    }

    // 3. Keep the beneficial droptofloor logic from your NEW version for ground units.
    vec3_t final_pos = io_position;
    if (!is_flying)
    {
        vec3_t end = final_pos;
        end.z -= 1024; // Search a long way down for ground.
        trace_t ground_trace = gi.trace(final_pos, monster_mins, monster_maxs, end, nullptr, MASK_SOLID);

        // If there's no ground below, it's an invalid spot for a ground unit.
        if (ground_trace.fraction == 1.0f) {
            return false;
        }

        // Use the engine's droptofloor to place the monster snugly on the ground.
        // This is a good feature to retain.
        if (!M_droptofloor_generic(final_pos, monster_mins, monster_maxs, false, nullptr, MASK_SOLID, false))
        {
            return false; // Failed to find a floor.
        }
    }

    // 4. The flawed logic (the separate entity_trace and the is_predefined_location check) is now removed.

    // 5. Success. Update the position with the potentially adjusted (dropped to floor) coordinates and return true.
    io_position = final_pos;
    return true;
}

// helper function
static edict_t *FindBestPlayerTargetForTeleport()
{
	edict_t *target_player = nullptr;
	int max_spree = -1;
	int64_t max_damage = -1;

	// First pass: Find the player with the highest spree or damage
	for (auto *player : active_players_no_spect())
	{
		if (player && player->client)
		{
			if (player->client->resp.spree > max_spree)
			{
				max_spree = player->client->resp.spree;
				target_player = player;
			}
            
			if (static_cast<int64_t>(player->client->total_damage) > max_damage)
			{
				// FIX: Explicitly cast the unsigned total_damage to a signed type for the assignment.
				max_damage = static_cast<int64_t>(player->client->total_damage);
				if (max_spree < 5)
				{ // Damage only takes precedence if spree is low
					target_player = player;
				}
			}
		}
	}

	// If no one has spree or damage, just pick a random active player
	if (!target_player)
	{
		std::vector<edict_t *> active;
		for (auto *p : active_players_no_spect())
		{
			active.push_back(p);
		}
		if (!active.empty())
		{
			target_player = random_element(active);
		}
	}

	return target_player;
}

// Finds a safe, fair, and tactically reasonable spawn point for a monster being rescued via teleport.
// It prioritizes spots that are near a player but not directly visible to them.
// MODIFIED: g_horde.cpp
static edict_t* FindSafeTeleportDestination(edict_t* self)
{
	// --- 1. Determine the Target Player ---
	edict_t* target_player = self->enemy;
	if (!target_player || !target_player->client || !target_player->inuse || target_player->health <= 0)
	{
		target_player = FindBestPlayerTargetForTeleport();
		if (!target_player)
		{
			if (developer->integer)
				gi.Com_PrintFmt("FindSafeTeleportDestination: No valid player target found.\n");
			return nullptr;
		}
	}

	// --- 2. Get Monster Properties ---
	const bool can_monster_fly = IsFlying(horde::MonsterTypeRegistry::GetTypeID(self->classname));

	// --- 3. Search for a Suitable Spawn Point ---
	edict_t* best_spot = nullptr;
	float best_score = -1.0f;

	// --- PERFORMANCE FIX: Use spatial index for O(1) nearby spawn point lookup ---
	constexpr float SEARCH_RADIUS = 1500.0f;
	auto nearby_spawn_points = HordePerf::g_spawn_spatial_index.GetNearbySpawnPoints(target_player->s.origin, SEARCH_RADIUS);

	for (edict_t* spawn_point : nearby_spawn_points)
	{
		// Spawn points from spatial index are already validated as info_player_deathmatch
		if (!spawn_point->inuse) {
			continue;
		}
		// --- END PERFORMANCE FIX ---

		// --- A. Filter for Valid Spawn Points ---
		const uint16_t index = g_spawn_point_map.at(spawn_point->s.number);
		if (level.time < g_spawnPointsData.teleport_cooldown[index] || IsSpawnPointOccupied(spawn_point))
		{
			continue;
		}

		if (can_monster_fly != (spawn_point->style == 1))
		{
			continue;
		}

		// --- B. Score the Validated Spawn Point ---
		float score = 100.0f;
		float dist_sq = (spawn_point->s.origin - target_player->s.origin).lengthSquared();

		constexpr float MIN_DIST_SQ = 400.0f * 400.0f;
		constexpr float MAX_DIST_SQ = 1200.0f * 1200.0f;
		if (dist_sq > MIN_DIST_SQ && dist_sq < MAX_DIST_SQ)
		{
			score += 100.0f;
		}

		vec3_t player_eye_pos = target_player->s.origin + vec3_t{ 0, 0, static_cast<float>(target_player->viewheight) };
		trace_t los = gi.trace(player_eye_pos, vec3_origin, vec3_origin, spawn_point->s.origin, target_player, MASK_SOLID);
		if (los.fraction < 1.0f)
		{
			score += 150.0f;
		}

		if (dist_sq < (350.0f * 350.0f))
		{
			score -= 200.0f;
		}

		score += frandom() * 25.0f;

		// --- C. Update Best Candidate ---
		if (score > best_score)
		{
			best_score = score;
			best_spot = spawn_point;
		}
	}

	if (developer->integer > 1 && best_spot)
	{
		gi.Com_PrintFmt("FindSafeTeleportDestination: Selected spot at {} with score {:.1f}\n", best_spot->s.origin, best_score);
	}
	else if (developer->integer > 1 && !best_spot)
	{
		gi.Com_PrintFmt("FindSafeTeleportDestination: No suitable teleport spot found.\n");
	}

	return best_spot;
}

bool CheckAndTeleportStuckMonster(edict_t* self)
{
	PROFILE_SCOPE("CheckAndTeleportStuckMonster");

	// --- 1. Initial Validation ---
	if (level.intermissiontime || !self || !self->inuse || self->deadflag || self->monsterinfo.IS_BOSS || !g_horde->integer || self->monsterinfo.issummoned)
		return false;

	if (level.time < self->monsterinfo.stuck_check_time)
		return false;
	self->monsterinfo.stuck_check_time = level.time + random_time(12.0_sec, 17.0_sec);

	if (horde::IsMonsterType(self, horde::MonsterTypeID::MISC_INSANE) || horde::IsMonsterType(self, horde::MonsterTypeID::SENTRYGUN) || (horde::IsMonsterType(self, horde::MonsterTypeID::TURRET) || (horde::IsMonsterType(self, horde::MonsterTypeID::FLIPPER))))
		return false;

	if (IsMonsterJumping(self))
	{
		self->teleport_time = level.time + 1.5_sec;
		return false;
	}

	// --- 2. Global Rate Limiting ---
	if (level.time > HordeConstants::g_teleport_rate_reset_time)
	{
		HordeConstants::g_teleport_rate_count = 0;
		HordeConstants::g_teleport_rate_reset_time = level.time + HordeConstants::GLOBAL_TELEPORT_RESET_INTERVAL;
	}
	int max_teleports = HordeConstants::MAX_TELEPORTS_PER_INTERVAL + ((g_insane->integer || g_chaotic->integer) ? 1 : 0);
	if (HordeConstants::g_teleport_rate_count >= max_teleports)
	{
		return false;
	}

	// --- 3. Determine if Teleport is Needed ---
	bool needs_teleport = false;
	const char* reason_str = "Unknown";

	if (gi.trace(self->s.origin, self->mins, self->maxs, self->s.origin, self, MASK_SOLID).startsolid)
	{
		needs_teleport = true;
		reason_str = "Stuck in Geometry";
	}
	else if (self->waterlevel > 0 && !(self->enemy && visible(self, self->enemy, false)))
	{
		needs_teleport = true;
		reason_str = "Drowning";
	}

	if (!needs_teleport)
	{
		if (self->teleport_time > level.time) return false;
		if (self->enemy && self->monsterinfo.attack_finished > level.time) return false;

		// Get map-size-aware timeout for no enemy situations
		const horde::MapSize& mapSize = g_horde_local.current_map_size;
		const gtime_t no_enemy_timeout = HordeConstants::GetNoEnemyTimeout(mapSize);

		if (!self->enemy || !self->enemy->inuse)
		{
			if (self->monsterinfo.no_enemy_timeout_start_time == 0_sec)
				self->monsterinfo.no_enemy_timeout_start_time = level.time;
			if (level.time > self->monsterinfo.no_enemy_timeout_start_time + no_enemy_timeout)
			{
				needs_teleport = true;
				reason_str = "No Enemy Timeout";
			}
		}
		else
		{
			self->monsterinfo.no_enemy_timeout_start_time = 0_sec;
		}

		if (!needs_teleport && self->max_health > 0)
		{
			// Use map-size-aware timeouts for damage-based teleportation
			const gtime_t timeout_duration = (self->health < self->max_health) ?
				HordeConstants::GetDamagedMonsterTimeout(mapSize) :
				HordeConstants::GetNoDamageTimeout(mapSize);
			const char* timeout_reason = (self->health < self->max_health) ?
				"Damaged Monster Inactivity" : "No Damage Timeout (Failsafe)";

			if (level.time > self->monsterinfo.react_to_damage_time + timeout_duration)
			{
				needs_teleport = true;
				reason_str = timeout_reason;
			}
		}
	}

	if (!needs_teleport)
		return false;

	if (developer->integer)
		gi.Com_PrintFmt("[CATS] Trigger for {}: {}.\n", self->classname, reason_str);

	// --- 4. Find Teleport Destination & Execute ---
	vec3_t dest_origin = vec3_origin;
	vec3_t dest_angles = self->s.angles;
	edict_t* used_spawn_point = FindSafeTeleportDestination(self);

	if (used_spawn_point)
	{
		dest_origin = used_spawn_point->s.origin;
		dest_angles = used_spawn_point->s.angles;
	}
	else
	{
		if (!FindEmergencySpawnPositionNearPlayer(dest_origin, dest_angles, horde::MonsterTypeRegistry::GetTypeID(self->classname)))
		{
			self->teleport_time = level.time + 5.0_sec;
			return false;
		}
	}

	dest_angles[PITCH] = 0;

	if (Horde_TeleportMonster(self, dest_origin, dest_angles, true, false))
	{
		MarkPositionAsRecentlyTeleported(self->s.origin);
		if (used_spawn_point)
		{
			// --- THIS IS THE FIX ---
			// Check if the key exists before trying to access the map.
			if (g_spawn_point_map.count(used_spawn_point->s.number)) {
				const uint16_t index = g_spawn_point_map.at(used_spawn_point->s.number);
				g_spawnPointsData.teleport_cooldown[index] = level.time + HordeConstants::SPAWN_POINT_TELEPORT_COOLDOWN;
			}
			
		}
		HordeConstants::g_teleport_rate_count++;
		self->monsterinfo.was_stuck = false;
		self->monsterinfo.stuck_check_time = level.time + random_time(12.0_sec, 17.0_sec);
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


// Corrected HandleSpawnPhaseAggression
void HandleSpawnPhaseAggression(edict_t *monster)
{
	if (!monster || !monster->inuse)
		return;

	if (monster->monsterinfo.spawned_in_spawn_state && g_horde_local.state == horde_state_t::spawning)
	{
		static int32_t spawn_state_deaths = 0;
		static gtime_t last_death_time = 0_sec;

		if (level.time - last_death_time > 8_sec)
		{
			spawn_state_deaths = 0;
		}
		spawn_state_deaths++;
		last_death_time = level.time;

		const uint16_t initial_wave_size_for_progress = (g_totalMonstersInWave > 0) ? g_totalMonstersInWave : 1;
		const float spawn_progress = static_cast<float>(monsters_spawned_in_current_phase) / static_cast<float>(initial_wave_size_for_progress);

		if (spawn_state_deaths >= HordeConstants::MIN_DEATHS_FOR_RETALIATION &&
			(spawn_progress >= HordeConstants::MIN_SPAWN_PROGRESS_FOR_RETALIATION || monsters_spawned_in_current_phase >= HordeConstants::MIN_SPAWNED_FOR_RETALIATION))
		{
			// Check cooldown to prevent retaliation from triggering too frequently
			if (g_special_spawn_state.type != SpecialSpawnType::Retaliation &&
				(level.time - g_horde_retaliation_last_trigger_time) >= HordeConstants::RETALIATION_COOLDOWN)
			{
				// Find the best player to target for the retaliation
				edict_t* target_player = nullptr;
				edict_t* top_spree_player = nullptr;
				int max_spree = 0;

				for (auto* p : active_players_no_spect()) {
					if (p && p->client && p->client->resp.spree > max_spree) {
						max_spree = p->client->resp.spree;
						top_spree_player = p;
					}
				}

				if (top_spree_player) {
					target_player = top_spree_player;
				} else {
					PlayerStats top_player_stats;
					float percentage;
					CalculateTopDamager(top_player_stats, percentage);
					if (top_player_stats.player && top_player_stats.player->client) {
						target_player = top_player_stats.player;
					}
				}

				if (!target_player) {
					std::vector<edict_t*> candidates;
					for (auto* p : active_players_no_spect()) {
						candidates.push_back(p);
					}
					if (!candidates.empty()) {
						target_player = random_element(candidates);
					}
				}

				// Call the new trigger function instead of the old one.
				TriggerRetaliation(g_horde_local.current_map_size, current_wave_level, target_player);

				// Update cooldown tracking
				g_horde_retaliation_last_trigger_time = level.time;

				// Add bonus monsters to the queue
				int32_t base_retaliation_add = (current_wave_level <= 7) ? 4 : 7;
				if (current_wave_level > 12)
					base_retaliation_add = 10;
				int32_t monsters_to_add_to_queue = base_retaliation_add + (current_wave_level / 4);
				if (g_horde_local.current_map_size.isBigMap)
					monsters_to_add_to_queue += (current_wave_level > 7 ? 5 : 2);
				else if (g_horde_local.current_map_size.isMediumMap)
					monsters_to_add_to_queue += (current_wave_level > 7 ? 3 : 1);
				monsters_to_add_to_queue = std::min(monsters_to_add_to_queue, 15);

				if (monsters_to_add_to_queue > 0) {
					g_horde_local.queued_monsters += monsters_to_add_to_queue;
					if (g_horde_local.state == horde_state_t::spawning) {
						initial_total_monsters_for_spawning_phase_timeout += monsters_to_add_to_queue;
					}
					if (developer->integer) {
						gi.Com_PrintFmt("HORDE: Retaliation added {} monsters to queue (New total: {}). Cap check bypassed.\n",
										monsters_to_add_to_queue, g_horde_local.queued_monsters);
					}
				}
				spawn_state_deaths = 0; // Reset counter after triggering
			}
		}
		monster->monsterinfo.was_spawned_by_horde = false;
		monster->monsterinfo.spawned_in_spawn_state = false;
	}
}

// This function now incorporates the Line-of-Sight (LOS) check inspired by the tank's
// spawning logic, preventing monsters from spawning behind walls relative to the
// original spawn point.
[[nodiscard]] bool TryAlternativeSpawnPosition(edict_t *spawn_point, horde::MonsterTypeID typeId, vec3_t &final_origin, vec3_t &final_angles)
{
	PROFILE_SCOPE("TryAlternativeSpawnPosition");

	if (!spawn_point || !spawn_point->inuse || !is_valid_vector(spawn_point->s.origin))
	{
		return false;
	}

	const vec3_t base_origin = spawn_point->s.origin;
	const vec3_t base_angles = spawn_point->s.angles;
	const bool is_flying = IsFlying(typeId);

	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(typeId, predicted_mins, predicted_maxs);

	auto check_and_set_position =
		[&](const vec3_t &candidate_pos, const vec3_t &offset_dir) -> bool
	{
		trace_t los_trace = gi.traceline(base_origin, candidate_pos, spawn_point, MASK_SOLID);
		if (los_trace.fraction < 1.0f)
		{
			return false;
		}

		vec3_t validated_pos = candidate_pos;
		// --- THE FIX IS HERE ---
		// Add 'false' as the 5th argument.
		if (IsPositionPhysicallyValid(validated_pos, predicted_mins, predicted_maxs, is_flying))
		{

			if (!IsPositionTooCloseToRecentSpawn(validated_pos, g_horde_local.current_map_size))
			{
				final_origin = validated_pos;
				if (offset_dir.lengthSquared() > VECTOR_LENGTH_SQ_EPSILON)
				{
					final_angles = vectoangles(offset_dir);
					final_angles[PITCH] = 0;
				}
				else
				{
					final_angles = base_angles;
				}
				MarkPositionAsRecentlyUsed(final_origin);
				return true;
			}
		}
		return false;
	};

	auto alternative_offsets = HordeConstants::horde_alternative_positions;
	std::shuffle(alternative_offsets.begin(), alternative_offsets.end(), mt_rand);

	for (const auto &offset : alternative_offsets)
	{
		if (check_and_set_position(base_origin + offset, offset))
		{
			return true;
		}
	}

	constexpr int RADIAL_ATTEMPTS = 35;
	constexpr float MIN_RADIUS = 40.0f;
	constexpr float MAX_RADIUS = 225.0f;

	for (int i = 0; i < RADIAL_ATTEMPTS; ++i)
	{
		float radius = frandom(MIN_RADIUS, MAX_RADIUS);
		float angle_rad = frandom() * 2.0f * PIf;
		vec3_t offset = {cosf(angle_rad) * radius, sinf(angle_rad) * radius, frandom(-8.0f, 24.0f)};

		if (check_and_set_position(base_origin + offset, offset))
		{
			return true;
		}
	}

	return false;
}
#include "g_horde_phys.h"

bool Horde_AttemptToUnstickMonster(edict_t* self)
{
	if (!self || !self->inuse)
	{
		return false;
	}

	// Try to find a safe, tactical teleport spot first.
	edict_t* dest_spot = FindSafeTeleportDestination(self);
	if (dest_spot)
	{
		// Use Horde_TeleportMonster. Force it despite visibility since the monster is just spawning.
		if (Horde_TeleportMonster(self, dest_spot->s.origin, dest_spot->s.angles, true, true))
		{
			if (developer->integer)
			{
				gi.Com_PrintFmt("FIXED STUCK (Safe): Relocated '{}' to spawn point at {}.\n", self->classname, dest_spot->s.origin);
			}
			return true;
		}
	}

	// If that fails, fall back to the emergency grid search.
	vec3_t emergency_origin, emergency_angles;
	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);

	// The target for the emergency spawn is the monster's current enemy, if it has one.
	if (FindEmergencySpawnPositionNearPlayer(emergency_origin, emergency_angles, typeId, self->enemy))
	{
		if (Horde_TeleportMonster(self, emergency_origin, emergency_angles, true, true))
		{
			if (developer->integer)
			{
				gi.Com_PrintFmt("FIXED STUCK (Emergency): Relocated '{}' to {}.\n", self->classname, emergency_origin);
			}
			return true;
		}
	}

	// All attempts failed.
	if (developer->integer)
	{
		gi.Com_PrintFmt("FIX STUCK FAILED: Could not find any valid location for '{}'.\n", self->classname);
	}
	return false;
}

static edict_t* Horde_SpawnMonster(
    const vec3_t& origin,
    const vec3_t& angles,
    horde::MonsterTypeID monster_type,
    int32_t currentLevel,
    float champion_chance)
{
    // Phase 1: Create and initialize the monster. It is NOT counted yet.
    edict_t* monster = SpawnMonsterByTypeID(monster_type, origin, angles, false); // false = don't link yet
    if (!monster) {
        return nullptr;
    }

    // Phase 2: Safely apply all bonuses while the monster is unlinked.
    if (!ApplyHordeBonuses(monster, currentLevel, champion_chance)) {
        // The monster was freed during bonus application.
        return nullptr;
    }

    // Phase 3: Link the fully configured monster into the world and validate.
    monster->solid = SOLID_BBOX;
    gi.linkentity(monster);

    trace_t post_link_trace = gi.trace(monster->s.origin, monster->mins, monster->maxs, monster->s.origin, monster, MASK_SOLID);
    if (post_link_trace.startsolid)
    {
        // MODIFICATION: Instead of freeing, try to relocate the stuck monster.
        if (Horde_AttemptToUnstickMonster(monster))
        {
            // Success! The monster was moved to a valid spot.
            // Re-run the trace to be 100% sure the new spot is clear.
            trace_t recheck_trace = gi.trace(monster->s.origin, monster->mins, monster->maxs, monster->s.origin, monster, MASK_SOLID);
            if (!recheck_trace.startsolid) {
                return monster;
            }
            // If it's still stuck after the fix, something is very wrong. Fall through to free it.
        }
        // END MODIFICATION

        // If relocation failed or the re-check failed, then we free it.
        if (developer->integer) {
            gi.Com_PrintFmt("SPAWN FAILURE: Monster '{}' at ({}) became stuck immediately after linking. Freeing.\n",
                monster->classname, monster->s.origin);
        }
        G_FreeEdict(monster);
        return nullptr;
    }

    return monster;
}

bool EmergencySpawnMonster(const int32_t levelNum,
						   horde::MonsterTypeID typeId,
						   bool is_additional_monster,
						   float champion_chance_for_this_spawn)
{
	PROFILE_SCOPE("EmergencySpawnMonster");

	vec3_t emergency_origin, emergency_angles;
	if (!FindEmergencySpawnPositionNearPlayer(emergency_origin, emergency_angles, typeId))
	{
		if (developer->integer)
		{
			// Count active players for debugging
			int active_player_count = 0;
			for (uint32_t p = 0; p < game.maxclients; p++) {
				edict_t* player = g_edicts + 1 + p;
				if (player->inuse && player->client && player->health > 0) {
					active_player_count++;
				}
			}

			const char* monster_name = horde::MonsterTypeRegistry::GetClassname(typeId);
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Could not find valid position for TypeID {} ({}) with {} active players. Consider adjusting emergency spawn distances or map constraints.\n",
							static_cast<int>(typeId), monster_name ? monster_name : "Unknown", active_player_count);
		}
		return false;
	}

    // FIX: This call now correctly links to the restored Horde_SpawnMonster function.
    edict_t* monster = Horde_SpawnMonster(emergency_origin, emergency_angles, typeId, levelNum, champion_chance_for_this_spawn);

	if (!monster)
	{
		return false;
	}

	if (monster->inuse && !monster->deadflag && monster->health > 0)
	{
		SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
		if (sound_spawn1)
		{
			gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
		}
	}

	if (is_additional_monster)
	{
		if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max())
		{
			g_totalMonstersInWave++;
		}
	}

	if (developer->integer)
	{
		gi.Com_PrintFmt("EMERGENCY SPAWN SUCCESSFUL: Spawned '{}' (Additional: {}).\n",
						monster->classname, is_additional_monster ? "Yes" : "No");
	}

	return true;
}

// =======================================================================
// COMPLETE FUNCTION: FindEmergencySpawnPositionNearPlayer
//
// grid didnt work for this, but keeping the same grid for future uses hopefully
// =======================================================================
bool FindEmergencySpawnPositionNearPlayer(vec3_t &out_position, vec3_t &out_angles, horde::MonsterTypeID typeId, edict_t *specific_target)
{
	bool used_human_player;
	return FindEmergencySpawnPosition(out_position, out_angles, used_human_player, typeId, specific_target);
}

// Modified ShouldTriggerAmbushSpawn function for more frequent ambushes
bool ShouldTriggerAmbushSpawn()
{
	// Static variables for tracking time-based cooldowns

	// Only consider ambush spawning after wave 5 (increased from 3)
	if (current_wave_level < 5)
	{
		return false;
	}

	// Check global cooldown (60 seconds between ambushes - reduced frequency)
	if (level.time - last_ambush_time < 60_sec)
	{
		return false;
	}

	// Check short-term cooldown (random 3-7 seconds between attempts)
	if (level.time < ambush_cooldown_end)
	{
		return false;
	}

	// Calculate base chance (reduced from 8% to 4%)
	float baseChance = 0.04f + (waves_since_ambush * 0.02f);

	// Wave level modifier (capped at 25%, reduced from 45%)
	const int cappedLevel = (current_wave_level > 25) ? 25 : current_wave_level;
	baseChance += (cappedLevel - 5) * 0.008f; // Updated to use new minimum wave level
	baseChance = std::min(baseChance, 0.25f);

	// Player performance bonus
	float playerBonus = 0.0f;
	int playerCount = 0;
	for (auto *player : active_players_no_spect())
	{
		if (player && player->inuse && player->health > 0)
		{
			// Reduced performance bonus requirements and amount
			if (player->health >= 150 || player->client->resp.spree >= 75)
			{
				playerBonus += 0.02f; // Reduced from 0.04f
			}
			playerCount++;
		}
	}

	// Apply player bonus with reduced cap
	if (playerCount > 0)
	{
		baseChance += std::min(playerBonus, 0.08f); // Reduced from 0.15f
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

static void TriggerRetaliation(const horde::MapSize& mapSize, int32_t waveLevel, edict_t* target_player)
{
	if (g_special_spawn_state.type != SpecialSpawnType::None) return; // Another special spawn is already planned

	int baseSize = mapSize.isSmallMap ? 2 : (mapSize.isBigMap ? 4 : 2);
	int spreeBonus = (target_player && target_player->client) ? (target_player->client->resp.spree / 8) : 0;
	int levelBonus = waveLevel / 10;
	int ambushSize = std::min(baseSize + spreeBonus + levelBonus, 5);

	horde::MonsterTypeID typeId = PickRetaliationMonsterTypeID(waveLevel);
	if (typeId == horde::MonsterTypeID::UNKNOWN) return;

	if (developer->integer) {
		gi.Com_PrintFmt("HORDE: PLANNING Retaliation (Size: {}). Target: {}\n",
						ambushSize, GetPlayerName(target_player));
	}

	g_special_spawn_state.type = SpecialSpawnType::Retaliation;
	g_special_spawn_state.remaining_count = ambushSize;
	g_special_spawn_state.monster_type_id = typeId;
	g_special_spawn_state.champion_chance = 0.6f + (frandom() * 0.25f);
	g_special_spawn_state.target_player = target_player;

	// Retaliation state is now managed through g_special_spawn_state
	// The type was already set above in g_special_spawn_state.type = SpecialSpawnType::Retaliation
	g_horde_retaliation_end_time = level.time + HordeConstants::RETALIATION_DURATION;
	g_horde_retaliation_target_player = target_player;
}

// This function is called when a random ambush is triggered.
static void TriggerAmbush(const horde::MapSize& mapSize, int32_t waveLevel)
{
	if (g_special_spawn_state.type != SpecialSpawnType::None) return;

	horde::MonsterTypeID monster_typeId_for_ambush = horde::MonsterTypeID::UNKNOWN;
    // Use a simple, robust fallback for picking the monster type
    static const std::array<horde::MonsterTypeID, 5> fallback_types = {
        horde::MonsterTypeID::SOLDIER_LIGHT, horde::MonsterTypeID::SOLDIER,
        horde::MonsterTypeID::INFANTRY, horde::MonsterTypeID::SOLDIER_SS, horde::MonsterTypeID::FLYER
    };
    monster_typeId_for_ambush = random_element(fallback_types);

	const int baseCount = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
	const int ambushSize = baseCount + (waveLevel >= 15 ? 2 : 1);

	if (developer->integer) {
		gi.Com_PrintFmt("HORDE: PLANNING Ambush (Size: {}). Spawning will be time-sliced.\n", ambushSize);
	}

	g_special_spawn_state.type = SpecialSpawnType::Ambush;
	g_special_spawn_state.remaining_count = ambushSize;
	g_special_spawn_state.monster_type_id = monster_typeId_for_ambush;
	g_special_spawn_state.champion_chance = 0.20f;
	g_special_spawn_state.target_player = nullptr; // Target will be chosen per-spawn
}

// This single function runs every frame to execute one spawn from the special plan.
static void ExecuteNextSpecialSpawn()
{
	if (g_special_spawn_state.remaining_count <= 0) {
		return;
	}

	// EmergencySpawnMonster is correct here as it finds a spot near a player.
	// It will use the specific target if one is set (for retaliation), or find a random one (for ambush).
	if (EmergencySpawnMonster(current_wave_level, g_special_spawn_state.monster_type_id, true, g_special_spawn_state.champion_chance))
	{
		// Success
	}
	else if (developer->integer)
	{
		gi.Com_PrintFmt("Special Spawn FAILED: EmergencySpawnMonster returned false.\n");
	}

	g_special_spawn_state.remaining_count--;

	// If this was the last one, clear the state.
	if (g_special_spawn_state.remaining_count <= 0) {
		g_special_spawn_state.clear();
	}
}

// Forward declarations for new helper functions
static void RebuildSpawnPointCacheIfNeeded();
static bool CheckHardCapAndLog(int32_t activeMonsters, int32_t hardCap, int32_t softCap, horde_state_t currentState, int32_t currentLevel);
static int32_t ManageSpawnCountsAndQueue(const horde::MapSize &mapSize, int32_t availableSpace);
static void DetermineSpawnStrategy(const horde::MapSize &mapSize, int32_t &out_spawnable_this_call, bool &out_use_emergency_spawn, bool &out_recovery_mode_active, float &out_champion_chance, int32_t availableSpace);
static bool ValidateSpawnPointForMonster(edict_t *spawn_point, gtime_t current_time);
static int ExecuteEmergencySpawnProcedure(int32_t spawnable_this_call,
										  int32_t currentLevel,
										  float champion_chance_param);


static void PlanMonsterSpawnBatch(
	int32_t num_to_plan,
	int32_t currentLevel_param,
	float champion_chance_param,
	bool is_recovery_mode_active_param,
	bool is_retaliation_active_param,
	MonsterWaveType current_actual_wave_type_param,
	MonsterWaveType original_wave_type_before_recovery_param)
{
	g_spawn_plan.clear();
	if (num_to_plan <= 0)
		return;

	g_spawn_plan.reserve(static_cast<size_t>(num_to_plan));

	g_champion_chance_for_current_batch = champion_chance_param;

	const size_t total_potential_points = g_potential_spawn_points.size();
	if (total_potential_points == 0)
	{
		return;
	}

	size_t points_checked = 0;
	int planned_count = 0;

	while (planned_count < num_to_plan && points_checked < total_potential_points * 2)
	{
		if (g_spawn_point_shuffle_index >= total_potential_points)
		{
			g_spawn_point_shuffle_index = 0;
		}
		edict_t* spawn_point = g_potential_spawn_points[g_spawn_point_shuffle_index++];
		points_checked++;

		if (!ValidateSpawnPointForMonster(spawn_point, level.time))
		{
			continue;
		}

		horde::MonsterTypeID monster_type_id = G_HordePickMonsterType(
			spawn_point, currentLevel_param, current_actual_wave_type_param,
			is_retaliation_active_param, is_recovery_mode_active_param,
			original_wave_type_before_recovery_param);

		if (monster_type_id != horde::MonsterTypeID::UNKNOWN)
		{
			// Use emplace_back to construct in-place, avoiding a temporary copy
			g_spawn_plan.emplace_back(monster_type_id, spawn_point);
			planned_count++;
		}
	}

	if (developer->integer && planned_count < num_to_plan)
	{
		gi.Com_PrintFmt("Spawn Plan: Only able to plan {} of {} requested monsters.\n", planned_count, num_to_plan);
	}
}

void PlanNextSpawnBatch()
{
	if (level.intermissiontime) return;
	if (developer->integer == 2 && g_horde->integer) return;

	if (!g_spawn_plan.empty()) {
		return;
	}

	const horde::MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level;

	RebuildSpawnPointCacheIfNeeded();
	if (g_potential_spawn_points.empty()) {
		if (g_consecutive_spawn_failures < HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY - 1)
			g_consecutive_spawn_failures++;
		if (!need_spawn_cache_reset) {
			need_spawn_cache_reset = true;
		}
		return;
	}

	const int32_t activeMonsters = CalculateRemainingMonsters();
	const int32_t softCap = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap : (mapSize.isSmallMap ? HordeConstants::MAX_MONSTERS_SMALL_MAP : (mapSize.isBigMap ? HordeConstants::MAX_MONSTERS_BIG_MAP : HordeConstants::MAX_MONSTERS_MEDIUM_MAP));

	// Integrated logic from the old TrySpawnAmbush function directly here.
	if (g_horde_local.state == horde_state_t::active_wave && activeMonsters < softCap && ShouldTriggerAmbushSpawn()) {
		TriggerAmbush(mapSize, currentLevel);
		// If the trigger was successful, it will have set the special spawn state.
		if (g_special_spawn_state.type == SpecialSpawnType::Ambush) {
			g_consecutive_spawn_failures = 0;
			SetNextMonsterSpawnTime(mapSize);
			return; // Exit early, the special spawn system will take over.
		}
	}

	const int32_t hardCap = static_cast<int32_t>(softCap * 1.4f);
	if (CheckHardCapAndLog(activeMonsters, hardCap, softCap, g_horde_local.state, currentLevel)) {
		return;
	}

	int32_t availableSpace = softCap - activeMonsters;
	if (availableSpace <= 0) {
		return;
	}
	availableSpace = ManageSpawnCountsAndQueue(mapSize, availableSpace);
	if (g_horde_local.num_to_spawn <= 0) {
		if (g_horde_local.queued_monsters <= 0 && g_horde_local.state == horde_state_t::spawning && !next_wave_message_sent) {
			VerifyAndAdjustBots();
			gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Fully Deployed.\nWave Level: {}\n", currentLevel);
			next_wave_message_sent = true;
			g_horde_local.state = horde_state_t::active_wave;
		}
		return;
	}

	int32_t spawnable_this_call;
	bool use_emergency_spawn_flag;
	float champion_chance_for_batch;
	DetermineSpawnStrategy(mapSize, spawnable_this_call, use_emergency_spawn_flag, g_recovery_mode_active, champion_chance_for_batch, availableSpace);

	if (use_emergency_spawn_flag) {
		int num_spawned = ExecuteEmergencySpawnProcedure(spawnable_this_call, currentLevel, champion_chance_for_batch);
		if (num_spawned > 0 && g_recovery_mode_active) {
			g_recovery_mode_active = false;
			current_wave_type = g_original_wave_type_before_recovery;
		}
	}
	else if (spawnable_this_call > 0) {
		PlanMonsterSpawnBatch(
			spawnable_this_call,
			currentLevel,
			champion_chance_for_batch,
			g_recovery_mode_active,
			(g_special_spawn_state.type == SpecialSpawnType::Retaliation),
			current_wave_type,
			g_original_wave_type_before_recovery);
	}

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
				// FIX: Replaced C-style cast with static_cast for type safety and clarity.
				gi.Com_PrintFmt("EMERGENCY SPAWN FAILED for type {}. (From ExecuteEmergencySpawnProcedure)\n", static_cast<int>(emergency_type));
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
		// Ensure spawn map is built first
		if (g_spawn_map_needs_build) {
			BuildSpawnPointMap();
			g_spawn_map_needs_build = false;
		}
		// Reserve capacity before copying to avoid reallocation
		g_potential_spawn_points.clear();
		g_potential_spawn_points.reserve(g_spawn_point_list.size());
		g_potential_spawn_points = g_spawn_point_list;

		g_cached_flying_spawn_count = 0;
		for (const auto* point : g_potential_spawn_points) {
			if (point->style == 1) {
				g_cached_flying_spawn_count++;
			}
		}

		if (!g_potential_spawn_points.empty())
		{
			std::shuffle(g_potential_spawn_points.begin(), g_potential_spawn_points.end(), mt_rand);
		}

		g_spawn_point_shuffle_index = 0;
		g_spawn_points_cached = true;
		need_spawn_cache_reset = false;
		g_consecutive_spawn_failures = 0;

		if (developer->integer)
			gi.Com_PrintFmt("Spawn Point Cache Rebuilt: {} points shuffled ({} flying).\n", g_potential_spawn_points.size(), g_cached_flying_spawn_count);
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

static void DetermineSpawnStrategy(const horde::MapSize &mapSize, int32_t &out_spawnable_this_call, bool &out_use_emergency_spawn, bool &out_recovery_mode_active_ref, float &out_champion_chance, int32_t availableSpace)
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
	if (g_special_spawn_state.type == SpecialSpawnType::Retaliation)
	{
		out_champion_chance = 0.40f;
		if (developer->integer > 1)
			gi.Com_PrintFmt("Retaliation: Champion chance {:.0f}%\n", out_champion_chance * 100.0f);
	}
}

//  spawn point validation with reduced lookups and better caching
static bool ValidateSpawnPointForMonster(edict_t* spawn_point, gtime_t current_time)
{
    if (!spawn_point || !spawn_point->inuse) return false;

    // Get compact index for direct vector access
    auto it = g_spawn_point_map.find(spawn_point->s.number);
    if (it == g_spawn_point_map.end()) return false;

    const uint16_t index = it->second;
    if (index >= g_spawn_validation_cache.size()) return false;

    // Direct vector access - much faster than hash map
    auto& cached = g_spawn_validation_cache[index];

    // Initialize index if first time
    if (cached.index == 0) {
        cached.index = index;
    }

    // Check if we can use cached result (within 100ms and same position)
    constexpr gtime_t CACHE_DURATION = 100_ms;
    if (current_time - cached.last_validation_time < CACHE_DURATION) {
        cached.InvalidateIfMoved(spawn_point->s.origin);
        if (cached.last_validation_time > 0_sec) {
            return cached.last_validation_result;
        }
    }

    // Progressive validation - become more lenient as failures increase
    bool is_valid = true;
    const bool emergency_mode = g_consecutive_spawn_failures >= HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY;
    const bool recovery_mode = g_consecutive_spawn_failures >= HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY;

    // Check cooldowns (skip in emergency mode for faster spawning)
    if (!emergency_mode) {
        if (current_time < g_spawnPointsData.teleport_cooldown[index] ||
            current_time < g_spawnPointsData.alternative_cooldown[index] ||
            (g_spawnPointsData.isTemporarilyDisabled[index] &&
             current_time < g_spawnPointsData.cooldownEndsAt[index])) {
            is_valid = false;
        }
    }

    // Check player proximity (relax distance progressively)
    if (is_valid) {
        float min_dist = HordeConstants::GetMinPlayerDistSpawnpoint(g_horde_local.current_map_size);
        if (recovery_mode) {
            min_dist *= 0.7f; // 30% reduction in recovery mode
        }
        if (emergency_mode) {
            min_dist *= 0.5f; // 50% reduction in emergency mode
        }

        const float MIN_DIST_SQ = min_dist * min_dist;
        const vec3_t spawn_pos = spawn_point->s.origin;

        for (const auto* player : active_players_no_spect()) {
            if (DistanceSquared(spawn_pos, player->s.origin) < MIN_DIST_SQ) {
                is_valid = false;
                break;
            }
        }
    }

    // Check occupation (skip in emergency mode)
    if (is_valid && !emergency_mode) {
        is_valid = !IsSpawnPointOccupied(spawn_point);
    }

    // Cache result
    cached.last_validation_time = current_time;
    cached.last_validation_result = is_valid;
    cached.last_validation_origin = spawn_point->s.origin;

    if (!is_valid) {
        IncreaseSpawnAttempts(spawn_point);
        g_consecutive_spawn_failures++;
    } else {
        // Gradually reduce failure count on successful validation
        if (g_consecutive_spawn_failures > 0) {
            g_consecutive_spawn_failures = std::max(0, g_consecutive_spawn_failures - 2);
        }
    }

    return is_valid;
}

static bool ApplyHordeBonuses(edict_t* monster, int32_t currentLevel, float champion_chance)
{
	bool became_champion = false;
	if (currentLevel >= 3 && !champion_spawned_this_wave && champion_spawn_cooldown_ends_at < level.time && !monster->monsterinfo.IS_BOSS && frandom() < champion_chance)
	{
		monster->monsterinfo.bonus_flags |= frandom() < 0.09f ? BF_BERSERKING : brandom() ? BF_CHAMPION : BF_POSSESSED;
		ApplyMonsterBonusFlags(monster);
		if (!monster->inuse) return false; // Check if monster was freed

		monster->item = G_HordePickItem();
		monster->spawnflags = monster->item ? (monster->spawnflags & ~SPAWNFLAG_MONSTER_NO_DROP) : (monster->spawnflags | SPAWNFLAG_MONSTER_NO_DROP);

		champion_spawned_this_wave = true;
		champion_spawn_cooldown_ends_at = level.time + random_time(10_sec, 20_sec);
		gi.LocBroadcast_Print(PRINT_HIGH, "*** A {} has appeared! ***\n", GetDisplayName(monster));
		became_champion = true;
	}

	if (!became_champion)
	{
		const float drop_chance = currentLevel <= 5 ? 0.8f : (currentLevel <= 8 ? 0.6f : 0.45f);
		if (frandom() < drop_chance)
		{
			monster->item = G_HordePickItem();
			if (!monster->inuse) return false;
			monster->spawnflags = monster->item ? (monster->spawnflags & ~SPAWNFLAG_MONSTER_NO_DROP) : (monster->spawnflags | SPAWNFLAG_MONSTER_NO_DROP);
		}
		else
		{
			monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;
		}
	}

	if (currentLevel >= 6 && monster->monsterinfo.power_armor_type == IT_NULL && monster->monsterinfo.armor_type == IT_NULL)
	{
		SetMonsterArmor(monster);
		if (!monster->inuse) return false;
	}

	return true; // Monster is still valid
}

// This is the high-level orchestrator for finding a valid spawn spot for normal spawns.
static bool FindValidSpawnLocation(
	edict_t* spawn_point,
	horde::MonsterTypeID monster_type,
	vec3_t& out_origin,
	vec3_t& out_angles,
	bool& out_used_alternative)
{
	const vec3_t base_origin = spawn_point->s.origin;
	const vec3_t base_angles = spawn_point->s.angles;
	const bool is_flying = IsFlying(monster_type);
	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(monster_type, predicted_mins, predicted_maxs);

	// Attempt Direct Spawn
	vec3_t direct_pos = base_origin;
	// The 'true' here indicates this is a predefined, trusted location.
	if (IsPositionPhysicallyValid(direct_pos, predicted_mins, predicted_maxs, is_flying))
	{
		out_origin = direct_pos;
		out_angles = base_angles;
		out_used_alternative = false;
		return true;
	}

	// Attempt Alternative Spawn
	if (TryAlternativeSpawnPosition(spawn_point, monster_type, out_origin, out_angles))
	{
		out_used_alternative = true;
		return true;
	}

	return false;
}

// Dependency for the main function below
void ApplySuccessfulAlternativeCooldown(edict_t *spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;

	const uint16_t index = g_spawn_point_map.at(spawn_point->s.number);

	// FIX: Cast the signed 'index' to the unsigned 'size_t' for each array access.
	g_spawnPointsData.alternative_attempts[static_cast<size_t>(index)] = 0;
	g_spawnPointsData.needs_long_alternative_cooldown[static_cast<size_t>(index)] = false;
	g_spawnPointsData.alternative_cooldown[static_cast<size_t>(index)] = level.time + std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN);

	if (developer->integer > 1)
		gi.Com_PrintFmt("Success cooldown applied to spawn at {}: {:.1f}s\n", spawn_point->s.origin, std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN).seconds());
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
	if (current_wave_level >= 1 && current_wave_level <= 10)
	{
		// Linearly decreases interval multiplier from 2.0x at wave 1 down to 1.1x at wave 10.
		// After wave 10, the multiplier is 1.0x (no modification).
		earlyWaveMultiplier = 2.0f - ((static_cast<float>(current_wave_level) - 1.0f) * 0.1f);
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
	//		current_wave_level,
	//		mapIndex,
	//		earlyWaveMultiplier,
	//		base_min_time.seconds(), base_max_time.seconds(),
	//		min_time.seconds(), max_time.seconds(),
	//		calculated_interval.seconds(),
	//		final_interval.seconds());
	// }
}

class EmergencySpawnOptimizer {
private:
    // Pre-computed candidate positions around each player
    struct PlayerSpawnCandidates {
        edict_t* player = nullptr;
        std::array<vec3_t, 16> positions;  // Pre-computed positions
        std::array<float, 16> scores;      // Pre-computed base scores
        uint8_t valid_count = 0;
        gtime_t last_update_time = 0_sec;
    };

    static constexpr size_t MAX_CACHED_PLAYERS = 16;
    std::array<PlayerSpawnCandidates, MAX_CACHED_PLAYERS> player_cache;
    size_t cached_player_count = 0;

    // Map-size-aware distance scaling
    struct ScaledDistances {
        float min_radius;
        float max_radius;
        float line_of_sight_tolerance;
    };

    ScaledDistances GetScaledDistances(const horde::MapSize& mapSize, int fallback_level = 0) const {
        ScaledDistances distances;
        
        if (mapSize.isSmallMap) {
            // Small maps: much tighter distances
            switch (fallback_level) {
                case 0: distances = {120.0f, 280.0f, 0.85f}; break;  // Was 300-800
                case 1: distances = {100.0f, 200.0f, 0.80f}; break;  // Was 200-600  
                case 2: distances = {80.0f, 150.0f, 0.75f}; break;   // Was 150-400
                default: distances = {60.0f, 120.0f, 0.70f}; break;  // New ultra-tight fallback
            }
        } else if (mapSize.isMediumMap) {
            // Medium maps: moderately scaled
            switch (fallback_level) {
                case 0: distances = {200.0f, 500.0f, 0.90f}; break;
                case 1: distances = {150.0f, 400.0f, 0.85f}; break;
                case 2: distances = {120.0f, 300.0f, 0.80f}; break;
                default: distances = {100.0f, 200.0f, 0.75f}; break;
            }
        } else {
            // Big maps: use original or even larger distances
            switch (fallback_level) {
                case 0: distances = {350.0f, 900.0f, 0.95f}; break;  // Slightly larger than original
                case 1: distances = {250.0f, 700.0f, 0.90f}; break;
                case 2: distances = {180.0f, 500.0f, 0.85f}; break;
                default: distances = {150.0f, 400.0f, 0.80f}; break;
            }
        }
        
        return distances;
    }

    // Helper method to check if a position has line-of-sight to at least one spawn point
    bool HasLineOfSightToSpawnPoint(const vec3_t& candidate_pos, float tolerance = 0.95f) const {
        // Check against all spawn points for line-of-sight
        for (edict_t* spawn_point : g_spawn_point_list) {
            if (!spawn_point || !spawn_point->inuse) continue;
            
            // Use the same line-of-sight check as TryAlternativeSpawnPosition
            trace_t los_trace = gi.traceline(spawn_point->s.origin, candidate_pos, spawn_point, MASK_SOLID);
            if (los_trace.fraction >= tolerance) { // Use adjustable tolerance
                return true; // Found at least one spawn point with clear line-of-sight
            }
        }
        return false; // No spawn point has clear line-of-sight
    }

    // Updates the list of candidate spawn points around a specific player.
    void UpdatePlayerCandidates(edict_t* player, PlayerSpawnCandidates& candidates, const horde::MapSize& mapSize) {
        constexpr size_t NUM_CANDIDATES = 16;
        
        ScaledDistances distances = GetScaledDistances(mapSize);

        candidates.player = player;
        candidates.valid_count = 0;
        candidates.last_update_time = level.time;

        const vec3_t player_origin = player->s.origin;

        // Generate candidate positions in a distributed radial pattern
        for (size_t i = 0; i < NUM_CANDIDATES; ++i) {
            const float radius = frandom(distances.min_radius, distances.max_radius);
            const float angle_rad = (2.0f * PIf * static_cast<float>(i)) / NUM_CANDIDATES + frandom(-0.2f, 0.2f); // Add jitter

            vec3_t candidate_pos = {
                player_origin.x + cosf(angle_rad) * radius,
                player_origin.y + sinf(angle_rad) * radius,
                player_origin.z + frandom(8.0f, 48.0f) // Vary height
            };

            // A quick, cheap check to see if the point is inside a solid.
            // This filters out many bad spots before the more expensive IsPositionPhysicallyValid call.
            if (gi.pointcontents(candidate_pos) & MASK_SOLID) {
                continue;
            }

            // Map-size-aware line-of-sight validation
            if (!HasLineOfSightToSpawnPoint(candidate_pos, distances.line_of_sight_tolerance)) {
                continue;
            }

            candidates.positions[candidates.valid_count] = candidate_pos;
            candidates.scores[candidates.valid_count] = CalculatePositionScore(candidate_pos, player_origin, mapSize);
            candidates.valid_count++;
        }

        // Sort candidates by score (best first) for faster searching later
        std::array<uint8_t, 16> indices;
        for(uint8_t i = 0; i < candidates.valid_count; ++i) {
            indices[i] = i;
        }

        // Use stable_sort with explicit return type to avoid consteval issues
        std::stable_sort(
            indices.begin(),
            indices.begin() + candidates.valid_count,
            [&](uint8_t a, uint8_t b) -> bool {
                return candidates.scores[a] > candidates.scores[b];
            }
        );

        // Reorder the positions and scores based on the sorted indices
        auto original_positions = candidates.positions;
        auto original_scores = candidates.scores;
        for (uint8_t i = 0; i < candidates.valid_count; ++i) {
            candidates.positions[i] = original_positions[indices[i]];
            candidates.scores[i] = original_scores[indices[i]];
        }
    }

    // Scores a potential position based on distance and proximity to recent events.
    float CalculatePositionScore(const vec3_t& pos, const vec3_t& player_pos, const horde::MapSize& mapSize) const {
        const float dist_sq = (pos - player_pos).lengthSquared();
        constexpr float OPTIMAL_DIST_SQ = 450.0f * 450.0f;

        // Score is higher the closer it is to the optimal distance
        float score = 100.0f - std::abs(dist_sq - OPTIMAL_DIST_SQ) * 0.0001f;

        // Bonus for positions that are not in recently used areas
        if (!IsPositionTooCloseToRecentSpawn(pos, mapSize)) score += 50.0f;
        if (!IsPositionTooCloseToRecentTeleport(pos, mapSize)) score += 30.0f;

        return score;
    }

    // Refreshes the cache of player candidates, adding new players or updating stale entries.
    void RefreshPlayerCache(const horde::MapSize& mapSize) {
        cached_player_count = 0;
        for (auto* player : active_players_no_spect()) {
            if (cached_player_count >= MAX_CACHED_PLAYERS) break;

            // Find if this player is already cached
            PlayerSpawnCandidates* candidates = nullptr;
            for (size_t i = 0; i < cached_player_count; ++i) {
                if (player_cache[i].player == player) {
                    candidates = &player_cache[i];
                    break;
                }
            }

            if (!candidates) {
                 candidates = &player_cache[cached_player_count];
            }

            // Only update if cache is stale (older than 1 second) or for a new player
            if (level.time - candidates->last_update_time > 1_sec || candidates->player != player) {
                UpdatePlayerCandidates(player, *candidates, mapSize);
            }

            if (candidates->player == player) {
                 cached_player_count++;
            }
        }
    }

    // Iterates through a player's cached candidates to find a valid spawn location.
    bool TryPlayerCandidatesFromCache(const PlayerSpawnCandidates& candidates,
                                    const vec3_t& mins, const vec3_t& maxs, bool is_flying,
                                    vec3_t& out_pos, vec3_t& out_angles) {
        static constexpr float MIN_PLAYER_DISTANCE_SQ = 120.0f * 120.0f; // 120 units minimum

        for (size_t i = 0; i < candidates.valid_count; ++i) {
            vec3_t test_pos = candidates.positions[i];

            // Check distance to all players to avoid spawning too close
            bool too_close_to_player = false;
            for (uint32_t p = 0; p < game.maxclients; p++) {
                edict_t* player = g_edicts + 1 + p;
                if (!player->inuse || !player->client || player->health <= 0)
                    continue;

                float dist_sq = (test_pos - player->s.origin).lengthSquared();
                if (dist_sq < MIN_PLAYER_DISTANCE_SQ) {
                    too_close_to_player = true;
                    break;
                }
            }

            if (!too_close_to_player && IsPositionPhysicallyValid(test_pos, mins, maxs, is_flying)) {
                out_pos = test_pos;
                const vec3_t to_player = candidates.player->s.origin - test_pos;
                out_angles = (to_player.lengthSquared() > VECTOR_LENGTH_SQ_EPSILON) ?
                           vectoangles(to_player) : candidates.player->s.angles;
                out_angles[PITCH] = 0;
                return true;
            }
        }
        return false;
    }

    // Updates candidates with custom distance range (for fallback logic)
    void UpdatePlayerCandidatesWithRange(edict_t* player, PlayerSpawnCandidates& candidates,
                                        const ScaledDistances& distances) {
        constexpr size_t NUM_CANDIDATES = 16;

        candidates.player = player;
        candidates.valid_count = 0;
        candidates.last_update_time = level.time;

        const vec3_t player_origin = player->s.origin;

        // Generate candidate positions in a distributed radial pattern
        for (size_t i = 0; i < NUM_CANDIDATES; ++i) {
            const float radius = frandom(distances.min_radius, distances.max_radius);
            const float angle_rad = (2.0f * PIf * static_cast<float>(i)) / NUM_CANDIDATES + frandom(-0.2f, 0.2f); // Add jitter

            vec3_t candidate_pos = {
                player_origin.x + cosf(angle_rad) * radius,
                player_origin.y + sinf(angle_rad) * radius,
                player_origin.z + frandom(-32.0f, 48.0f) // Z variation for different floor levels
            };

            // Map-size-aware line-of-sight validation
            if (!HasLineOfSightToSpawnPoint(candidate_pos, distances.line_of_sight_tolerance)) {
                continue;
            }

            // Basic validation and scoring
            float base_score = 1.0f / (1.0f + radius * 0.001f); // Closer is slightly better

            candidates.positions[candidates.valid_count] = candidate_pos;
            candidates.scores[candidates.valid_count] = base_score;
            candidates.valid_count++;
        }
    }

public:
    // The main public function to find an optimal emergency spawn position.
    // The main public function to find an optimal emergency spawn position.
    bool FindOptimalPosition(vec3_t& out_position, vec3_t& out_angles,
                           horde::MonsterTypeID typeId, edict_t* specific_target = nullptr, edict_t** out_used_player = nullptr) {

        const horde::MapSize& mapSize = g_horde_local.current_map_size;
        
        vec3_t predicted_mins, predicted_maxs;
        GetPredictedScaledBounds(typeId, predicted_mins, predicted_maxs);
        const bool is_flying = IsFlying(typeId);

        // Try with map-size-scaled distances
        for (int fallback_level = 0; fallback_level < 4; ++fallback_level) {
            ScaledDistances distances = GetScaledDistances(mapSize, fallback_level);
            
            edict_t* used_player = nullptr;
            if (TryFindPositionWithDistances(out_position, out_angles, typeId, specific_target,
                                           predicted_mins, predicted_maxs, is_flying, distances, &used_player)) {
                if (developer->integer && fallback_level > 0) {
                    gi.Com_PrintFmt("EMERGENCY SPAWN: Used fallback level {} ({:.0f}-{:.0f}) for TypeID {}.\\n", 
                                  fallback_level, distances.min_radius, distances.max_radius, static_cast<int>(typeId));
                }
                if (out_used_player) {
                    *out_used_player = used_player;
                }
                return true;
            }
        }

        return false;
    }

private:
    // Helper function to try finding position with specific distance ranges
    // Helper function to try finding position with specific distance ranges
    bool TryFindPositionWithDistances(vec3_t& out_position, vec3_t& out_angles,
                                    horde::MonsterTypeID typeId, edict_t* specific_target,
                                    const vec3_t& predicted_mins, const vec3_t& predicted_maxs,
                                    bool is_flying, const ScaledDistances& distances, edict_t** out_used_player = nullptr) {

        // If a specific player is targeted, generate and check candidates for them first.
        if (specific_target && specific_target->inuse && specific_target->health > 0) {
            PlayerSpawnCandidates temp_candidates;
            UpdatePlayerCandidatesWithRange(specific_target, temp_candidates, distances);
            if (TryPlayerCandidatesFromCache(temp_candidates, predicted_mins, predicted_maxs, is_flying, out_position, out_angles)) {
                if (out_used_player) {
                    *out_used_player = specific_target;
                }
                return true;
            }
        }

        // Try all active players with custom distance range
        for (uint32_t p = 0; p < game.maxclients; p++) {
            edict_t* player = g_edicts + 1 + p;
            if (!player->inuse || !player->client || player->health <= 0)
                continue;
            if (player == specific_target) continue; // Skip if already checked

            PlayerSpawnCandidates temp_candidates;
            UpdatePlayerCandidatesWithRange(player, temp_candidates, distances);
            if (TryPlayerCandidatesFromCache(temp_candidates, predicted_mins, predicted_maxs, is_flying, out_position, out_angles)) {
                if (out_used_player) {
                    *out_used_player = player;
                }
                return true;
            }
        }

        return false;
    }
};;;

// Global optimizer instance (defined AFTER the class)
static EmergencySpawnOptimizer g_emergency_spawn_optimizer;
// =======================================================================
// END: High-performance emergency spawn system
// =======================================================================

class MonsterSpawnPipeline {
private:
    // A batch of monsters to be processed together
    struct SpawnBatch {
        static constexpr size_t MAX_BATCH_SIZE = 8;
        std::array<SpawnPlanEntry, MAX_BATCH_SIZE> entries;
        std::array<vec3_t, MAX_BATCH_SIZE> final_origins;
        std::array<vec3_t, MAX_BATCH_SIZE> final_angles;
        std::array<bool, MAX_BATCH_SIZE> used_alternatives;
        std::array<edict_t*, MAX_BATCH_SIZE> spawned_monsters;
        size_t count = 0;

        void Clear() { count = 0; }
        bool IsFull() const { return count >= MAX_BATCH_SIZE; }
        void AddEntry(const SpawnPlanEntry& entry) {
            if (count < MAX_BATCH_SIZE) {
                entries[count] = entry;
                // Initialize to a known state
                spawned_monsters[count] = reinterpret_cast<edict_t*>(1); // Use a non-null placeholder
                count++;
            }
        }
    };

    SpawnBatch current_batch;

    // Cache for monster properties to avoid repeated lookups
    struct MonsterTypeCache {
        horde::MonsterTypeID type_id;
        vec3_t predicted_mins;
        vec3_t predicted_maxs;
        bool is_flying;
    };
    std::unordered_map<horde::MonsterTypeID, MonsterTypeCache> type_cache;

    // Helper to handle a successful spawn
    void HandleSuccessfulSpawn(edict_t* spawn_point, bool used_alternative, edict_t* monster) {
        g_consecutive_spawn_failures = 0;
        if (used_alternative) {
            ApplySuccessfulAlternativeCooldown(spawn_point);
        } else {
            OnSuccessfulSpawn(spawn_point);
        }

        if (g_horde_local.num_to_spawn > 0) {
            g_horde_local.num_to_spawn--;
        }
        monsters_spawned_in_current_phase++;

        if (monster->inuse && !monster->deadflag && monster->health > 0) {
            SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
            if (sound_spawn1) {
                gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
            }
        }
    }

    // Helper to handle a failed spawn
    void HandleSpawnFailure(edict_t* spawn_point, bool used_alternative) {
        g_consecutive_spawn_failures++;
        if (used_alternative) {
            // If the alternative position itself failed, apply a cooldown for trying alternatives again.
            ApplyAlternativePositionCooldown(spawn_point);
        } else {
            // If the primary position failed, just increment its attempt counter.
            IncreaseSpawnAttempts(spawn_point);
        }
    }

    // Gets monster properties from cache or computes and stores them.
    const MonsterTypeCache& GetOrCacheMonsterType(horde::MonsterTypeID type_id) {
        auto it = type_cache.find(type_id);
        if (it != type_cache.end()) {
            return it->second;
        }

        MonsterTypeCache new_entry;
        new_entry.type_id = type_id;
        new_entry.is_flying = IsFlying(type_id);
        GetPredictedScaledBounds(type_id, new_entry.predicted_mins, new_entry.predicted_maxs);

        auto [inserted_it, success] = type_cache.emplace(type_id, new_entry);
        return inserted_it->second;
    }

    // Fills the current batch from the global spawn plan.
    void PrepareBatch() {
        current_batch.Clear();
        while (!g_spawn_plan.empty() && !current_batch.IsFull()) {
            current_batch.AddEntry(g_spawn_plan.back());
            g_spawn_plan.pop_back();
        }
    }

    // Validates spawn locations for the entire batch.
    void ValidateLocations() {
        for (size_t i = 0; i < current_batch.count; ++i) {
            const auto& entry = current_batch.entries[i];
            const auto& type_info = GetOrCacheMonsterType(entry.typeId);

            bool found = FindValidSpawnLocation(
                entry.spawn_point,
                entry.typeId,
                current_batch.final_origins[i],
                current_batch.final_angles[i],
                current_batch.used_alternatives[i]
            );

            if (!found) {
                current_batch.spawned_monsters[i] = nullptr; // Mark as invalid for the next stage
            }
        }
    }

    // Spawns all valid monsters in the batch.
    void SpawnMonsters() {
        for (size_t i = 0; i < current_batch.count; ++i) {
            if (current_batch.spawned_monsters[i] == nullptr) continue; // Skip failed validations

            const auto& entry = current_batch.entries[i];
            current_batch.spawned_monsters[i] = Horde_SpawnMonster(
                current_batch.final_origins[i],
                current_batch.final_angles[i],
                entry.typeId,
                current_wave_level,
                g_champion_chance_for_current_batch
            );
        }
    }

    // Processes the results of the spawning attempt for the entire batch.
    void ProcessResults() {
        for (size_t i = 0; i < current_batch.count; ++i) {
            const auto& entry = current_batch.entries[i];
            edict_t* monster = current_batch.spawned_monsters[i];

            if (monster && monster->inuse) {
                HandleSuccessfulSpawn(entry.spawn_point, current_batch.used_alternatives[i], monster);
            } else {
                HandleSpawnFailure(entry.spawn_point, current_batch.used_alternatives[i]);
            }
        }
    }

public:
    // The main public function that orchestrates the entire pipeline.
    void ProcessSpawnPlan() {
        if (g_spawn_plan.empty()) return;

        // Process the global plan in manageable batches
        while (!g_spawn_plan.empty()) {
            PrepareBatch();

            if (current_batch.count > 0) {
                ValidateLocations();
                SpawnMonsters();
                ProcessResults();
            }
        }
    }
};

// Global pipeline instance (defined AFTER the class)
static MonsterSpawnPipeline g_monster_spawn_pipeline;
// =======================================================================
// END: High-performance monster spawning pipeline
// =======================================================================

bool FindEmergencySpawnPosition(vec3_t &position, vec3_t &angles, bool &used_human_player, horde::MonsterTypeID typeId, edict_t *specific_target)
{
    edict_t* actual_used_player = nullptr;
    if (g_emergency_spawn_optimizer.FindOptimalPosition(position, angles, typeId, specific_target, &actual_used_player)) {
        // Determine if the actually used player was human
        if (actual_used_player) {
            used_human_player = !(actual_used_player->svflags & SVF_BOT);
        } else {
            // Fallback to the old heuristic if no specific player was returned
            used_human_player = AreHumanPlayersPresent();
        }
        return true;
    }
    return false;
}

// REPLACE the old ExecuteSpawnPlan function with this one.
static void ExecuteSpawnPlan()
{
    // The new pipeline handles the entire global spawn plan in one call.
    g_monster_spawn_pipeline.ProcessSpawnPlan();
}

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

static bool GiveTopDamagerReward(const PlayerStats& topDamager, std::string_view playerName)
{
	// Quick validation with early return
	if (!topDamager.player || !topDamager.player->inuse || !topDamager.player->client)
		return false;

	const int roll = irandom(1, TOTAL_REWARD_WEIGHT);
	item_id_t selectedItemId = TOP_DAMAGER_REWARDS[0].item_id; // Default fallback to the first item
	int currentWeight = 0;

	for (const auto& reward : TOP_DAMAGER_REWARDS)
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
	gitem_t* item = GetItemByIndex(selectedItemId);
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
	edict_t* entity = G_Spawn();
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
	const char* itemName = item->use_name ? item->use_name : (item->classname ? item->classname : "reward");

	gi.LocBroadcast_Print(PRINT_HIGH, "{} receives a {} for top damage!\n",
		playerName.empty() ? "Unknown Player" : playerName.data(),
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
			current_wave_level);
		break;
	case WaveEndReason::MonstersRemaining:
		gi.LocBroadcast_Print(PRINT_HIGH, "Wave {} Pushed Back - But Still Threatening!\n",
			current_wave_level);
		break;
	case WaveEndReason::TimeLimitReached:
		gi.LocBroadcast_Print(PRINT_HIGH, "Wave {} Contained - Time Limit Reached!\n",
			current_wave_level);
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
		const char* playerName = GetPlayerName(topDamager.player);

		gi.LocBroadcast_Print(PRINT_HIGH, "{} dealt the most damage with {}! ({}% of total)\n",
			playerName, topDamager.total_damage, static_cast<int>(percentage));

		if (GiveTopDamagerReward(topDamager, std::string_view(playerName)))
		{
			// (rest of the function is unchanged)
			for (auto* player : active_players())
			{
				if (player && player->client)
				{
					player->client->total_damage = 0;
					player->client->lastdmg = level.time;
					player->client->dmg_counter = 0;
					player->client->ps.stats[STAT_ID_DAMAGE] = 0;
					player->client->respawn_time = 0_sec;
					player->client->coop_respawn_state = COOP_RESPAWN_NONE;
					player->client->last_damage_time = level.time;
				}
			}
		}
	}
}

void CheckAndResetDisabledSpawnPoints()
{
	// Iterate using the compact index, which is much more efficient.
	for (size_t index = 0; index < g_num_spawn_points; ++index)
	{
		if (g_spawnPointsData.isTemporarilyDisabled[index])
		{
			g_spawnPointsData.isTemporarilyDisabled[index] = false;
			g_spawnPointsData.attempts[index] = 0;
			g_spawnPointsData.cooldownEndsAt[index] = 0_sec;
		}
	}
}
// RESTORED and CORRECTED: This function was also removed but is required.
static edict_t* Horde_SpawnMonster(
    const vec3_t& origin,
    const vec3_t& angles,
    horde::MonsterTypeID monster_type,
    int32_t currentLevel,
    float champion_chance); // Already shown above

	void VerifyEntityProperties();
void Horde_RunFrame()
{

//	VerifyEntityProperties();

	if (level.intermissiontime) {
		return;
	}

	// --- TIME-SLICED EXECUTION PHASE ---
	// These functions run every frame to execute any existing plans.
	ExecuteSpawnPlan();
	ExecuteNextSpecialSpawn();
	// --- END EXECUTION PHASE ---

	const gtime_t currentTime = level.time;
	const horde::MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level;

	CleanupSpawnPointCache();
	CheckAndReduceSpawnCooldowns();

	// Check if retaliation mode has timed out
	if (g_special_spawn_state.type == SpecialSpawnType::Retaliation && currentTime >= g_horde_retaliation_end_time) {
		g_special_spawn_state.clear();  // Clear the entire special spawn state
		g_horde_retaliation_end_time = 0_sec;
		g_horde_retaliation_target_player = nullptr;
		if (developer->integer) {
			gi.Com_PrintFmt("HORDE: Retaliation Mode Ended.\n");
		}
	}

	// Failsafe for stuck waves
	static gtime_t last_wave_change_time = 0_sec;
	static int32_t wave_at_last_check = 0;
	if (wave_at_last_check != currentLevel) {
		last_wave_change_time = currentTime;
		wave_at_last_check = currentLevel;
	}
	else if (g_horde_local.state != horde_state_t::warmup && currentTime > last_wave_change_time + HordeConstants::WAVE_STUCK_TIMEOUT) {
		if (GetStroggsNum() == 0) {
			if (developer->integer) {
				gi.Com_PrintFmt("CRITICAL: Wave {} stuck for over ({:.1f}s with 0 monsters. Forcing progression.\n",
								currentLevel, HordeConstants::WAVE_STUCK_TIMEOUT.seconds());
			}
			g_horde_local.state = horde_state_t::cleanup;
			g_horde_local.monster_spawn_time = currentTime;
		} else {
			last_wave_change_time = currentTime;
		}
	}

	bool waveEnded = false;
	WaveEndReason currentWaveEndReason = WaveEndReason::AllMonstersDead;

	switch (g_horde_local.state)
	{
	case horde_state_t::warmup:
		if (g_horde_local.warm_time < currentTime) {
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(1);
			PlayWaveStartSound();
			DisplayWaveMessage();
		}
		break;

	case horde_state_t::spawning:
	{
static gtime_t spawning_phase_timeout_start_time = 0_sec;
		static int32_t prev_wave_level_for_spawning_timers = -1;

		if (currentLevel != prev_wave_level_for_spawning_timers)
		{
			spawning_phase_timeout_start_time = currentTime;
			prev_wave_level_for_spawning_timers = currentLevel;
			initial_total_monsters_for_spawning_phase_timeout = g_horde_local.num_to_spawn + g_horde_local.queued_monsters;
			monsters_spawned_in_current_phase = 0;
		}

		if (currentTime > spawning_phase_timeout_start_time + 90_sec)
		{
			if (!next_wave_message_sent)
			{
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
			else if (!IsBossWave() || boss_spawned_for_wave) {
				// This now just PLANS the next batch, it doesn't spawn directly.
				PlanNextSpawnBatch();
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
			// SetNextMonsterSpawnTime is now called inside PlanNextSpawnBatch
		}
		break;
	}

	case horde_state_t::active_wave:
		if (CheckRemainingMonstersCondition(mapSize, currentWaveEndReason)) {
			waveEnded = true;
			break;
		}
		// Check if we need to plan more spawns from the queue or for an ambush
		if (g_horde_local.monster_spawn_time <= currentTime) {
			PlanNextSpawnBatch();
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
    // Simple cache to avoid redundant iterations
    static int32_t cached_count = 0;
    static gtime_t last_cache_time = 0_ms;

    // Cache valid for 100ms - monsters don't spawn/die that frequently
    constexpr gtime_t CACHE_DURATION = 100_ms;
    if (level.time - last_cache_time < CACHE_DURATION) {
        return cached_count;
    }

    int32_t live_monster_count = 0;

    // Use the efficient 'active_monsters' iterator.
    for (edict_t* ent : active_monsters())
    {
        // This is the crucial check you suggested.
        // If a monster has this flag, it's a temporary entity (like for precaching)
        // or a special case that shouldn't be counted towards the wave total.
        if (ent->monsterinfo.aiflags & AI_DO_NOT_COUNT)
        {
            continue; // Skip this monster.
        }

        // The monster is inuse, alive, and meant to be counted.
        live_monster_count++;
    }

	    // Update cache
    cached_count = live_monster_count;
    last_cache_time = level.time;

    return live_monster_count;
}

// Helper function to check if it's a boss wave
inline bool IsBossWave() noexcept
{
	return g_horde_local.level >= 10 && g_horde_local.level % 5 == 0;
}

bool GetPredictedScaledBounds(horde::MonsterTypeID typeId, vec3_t &out_mins, vec3_t &out_maxs)
{
	const size_t index = static_cast<size_t>(typeId);
	if (typeId == horde::MonsterTypeID::UNKNOWN || index >= g_monsterData.MONSTER_ARRAY_SIZE)
	{
		// Fallback for invalid ID
		out_mins = HordeConstants::VALIDATE_CHECK_MINS;
		out_maxs = HordeConstants::VALIDATE_CHECK_MAXS;
		return false;
	}

	const float scale = g_monsterData.s_scales[index];

	if (scale <= 0.0f)
	{
		// Use unscaled bounds if scale is invalid
		out_mins = g_monsterData.default_mins[index];
		out_maxs = g_monsterData.default_maxs[index];
	}
	else
	{
		out_mins = g_monsterData.default_mins[index] * scale;
		out_maxs = g_monsterData.default_maxs[index] * scale;
	}
	return true;
}

bool Horde_TeleportMonster(edict_t *self, const vec3_t &destination_origin, const vec3_t &destination_angles, bool play_effects, bool force_despite_visibility)
{
	PROFILE_SCOPE("Horde_TeleportMonster");
	if (level.intermissiontime)
		return false;

	if (!self || !self->inuse || self->deadflag || !is_valid_vector(destination_origin) || !is_valid_vector(destination_angles))
	{
		return false;
	}

	if (self->monsterinfo.issummoned ||
		(!force_despite_visibility && (self->enemy && self->enemy->inuse && visible(self, self->enemy, false))))
	{
		self->teleport_time = level.time + 1.5_sec;
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
	GetPredictedScaledBounds(monsterTypeId, predicted_mins, predicted_maxs);

	// --- THE FIX IS HERE ---
	// Add 'false' as the 5th argument.
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
		if (self->inuse && !self->deadflag && self->health > 0)
		{
			SpawnGrow_Spawn(self->s.origin, 80.0f, 10.0f);
			if (sound_spawn1)
			{
				gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
			}
		}
	}

	self->monsterinfo.was_stuck = false;
	self->monsterinfo.stuck_check_time = level.time + random_time(12.0_sec, 17.0_sec);
	self->monsterinfo.react_to_damage_time = level.time;
	self->teleport_time = level.time + random_time(HordeConstants::MIN_TELEPORT_COOLDOWN_MONSTER, HordeConstants::MAX_TELEPORT_COOLDOWN_MONSTER);

	return true;
}

static void Horde_InitLevel(const int32_t lvl)
{

	// Build the map of spawn points once, right before the first wave,
	// ensuring all map entities have been loaded.
	if (g_spawn_map_needs_build) {
		BuildSpawnPointMap();
		g_spawn_map_needs_build = false;
	}

	g_spawn_plan.clear();
	g_special_spawn_state.clear(); // This replaces the old ambush/retaliation resets
	// REMOVED: g_horde_retaliation_active - using g_special_spawn_state.type instead
	g_horde_retaliation_end_time = 0_sec;
	g_horde_retaliation_last_trigger_time = 0_sec;
	g_horde_retaliation_target_player = nullptr;
	ResetChampionMonsterState();
	waves_since_ambush++;

	// --- 2. Set up the new wave's parameters ---
	g_horde_local.level = lvl;
	current_wave_level = lvl;

	// Determine the wave type. Boss waves start with no type; it's set when the boss spawns.
	if (!(lvl >= 10 && lvl % 5 == 0))
	{
		InitializeWaveType(lvl);
	}
	else
	{
		current_wave_type = MonsterWaveType::None;
	}

	g_eligible_monsters_for_wave.clear();
	g_eligible_item_indices_for_wave.clear();

	g_eligible_monsters_for_wave.reserve(MONSTER_DATA_COUNT);

	// --- THIS IS THE LOGIC FIX ---
	// We now build the eligible list by considering monsters up to the current level
	// PLUS the maximum possible boost from the elite spawn mechanic. This ensures
	// that potential elite monsters are included in the list for the JIT precacher
	// and the monster picker.
	const int32_t max_level_for_eligibility = current_wave_level + MAX_EFFECTIVE_LEVEL_BOOST;

	for (size_t i = 0; i < MONSTER_DATA_COUNT; ++i)
	{
		const auto& monster = monsterTypes[i];

		// The loop now correctly includes monsters that might be chosen as elites.
		if (monster.minWave > max_level_for_eligibility)
		{
			break;
		}

		if (IsValidMonsterForWave(monster.typeId, current_wave_type))
		{
			g_eligible_monsters_for_wave.push_back(&monster);
		}
	}
	

	g_eligible_item_indices_for_wave.reserve(g_hordeItemDataSoA.NUM_ITEMS); // Pre-allocate memory
	for (size_t i = 0; i < g_hordeItemDataSoA.NUM_ITEMS; ++i)
	{
		if (lvl >= g_hordeItemDataSoA.minWaves[i])
		{
			g_eligible_item_indices_for_wave.push_back(i);
		}
	}

	if (developer->integer)
	{
		gi.Com_PrintFmt("Horde_InitLevel: Built cache with {} eligible monsters for wave {}.\n",
			g_eligible_monsters_for_wave.size(), current_wave_level);
	}

	// --- 4. JIT PRECACHE LOGIC WITH FAMILY SYSTEM ---
	const int32_t precache_mode = g_horde_precache_mode ? g_horde_precache_mode->integer : 1;
	const int32_t precache_window = g_horde_precache_window ? g_horde_precache_window->integer : 3;

	if (precache_mode > 0) // Not pure JIT
	{
		// Track which families we've already precached this wave
		std::unordered_set<AssetFamilyID> families_to_precache;

		// Determine range of waves to precache based on mode
		int32_t waves_ahead = (precache_mode == 2) ? 100 : precache_window; // Mode 2 = full precache

		if (g_horde_precache_debug && g_horde_precache_debug->integer)
		{
			gi.Com_PrintFmt("Wave {}: Precaching {} waves ahead (mode {})\n", lvl, waves_ahead, precache_mode);
		}

		// Collect families for current and future waves
		for (const MonsterTypeInfo* monster_info : g_eligible_monsters_for_wave)
		{
			// Check if this monster should be precached based on wave window
			if (monster_info->minWave <= lvl + waves_ahead)
			{
				AssetFamilyID family = GetMonsterAssetFamily(monster_info->typeId);
				if (family != AssetFamilyID::UNKNOWN_FAMILY)
				{
					families_to_precache.insert(family);
				}
			}
		}

		// Precache the collected families
		int families_precached_count = 0;
		for (AssetFamilyID family : families_to_precache)
		{
			if (!g_asset_families[static_cast<size_t>(family)].is_precached)
			{
				if (CanPrecacheFamily(family))
				{
					PrecacheAssetFamily(family);
					families_precached_count++;

					// Mark all members of this family as precached
					auto& family_data = g_asset_families[static_cast<size_t>(family)];
					for (auto member : family_data.members)
					{
						if (static_cast<size_t>(member) < g_precached_monster_types_flags.size())
						{
							g_precached_monster_types_flags[static_cast<size_t>(member)] = true;
						}
					}
				}
				else
				{
					if (g_horde_precache_debug && g_horde_precache_debug->integer)
					{
						gi.Com_Print("Warning: Cannot precache more families - approaching limits\n");
					}
					break; // Stop precaching if we're hitting limits
				}
			}
		}

		if (g_horde_precache_debug && g_horde_precache_debug->integer)
		{
			gi.Com_PrintFmt("Wave {}: Precached {} new families, {} total families loaded\n",
				lvl, families_precached_count,
				std::count_if(g_asset_families.begin(), g_asset_families.end(),
					[](const MonsterAssetFamily& f) { return f.is_precached; }));
		}
	}
	else // Pure JIT mode (mode 0) - only precache what's needed now
	{
		// Original JIT behavior but using families
		std::unordered_set<AssetFamilyID> families_needed;

		for (const MonsterTypeInfo* monster_info : g_eligible_monsters_for_wave)
		{
			if (monster_info->minWave <= lvl) // Only current wave
			{
				AssetFamilyID family = GetMonsterAssetFamily(monster_info->typeId);
				if (family != AssetFamilyID::UNKNOWN_FAMILY &&
				    !g_asset_families[static_cast<size_t>(family)].is_precached)
				{
					families_needed.insert(family);
				}
			}
		}

		for (AssetFamilyID family : families_needed)
		{
			PrecacheAssetFamily(family);
			// Mark all members as precached
			auto& family_data = g_asset_families[static_cast<size_t>(family)];
			for (auto member : family_data.members)
			{
				if (static_cast<size_t>(member) < g_precached_monster_types_flags.size())
				{
					g_precached_monster_types_flags[static_cast<size_t>(member)] = true;
				}
			}
		}
	}

	if (developer->integer)
	{
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

	for (size_t i = 0; i < WARNING_TIMES.size(); i++)
	{
		g_horde_local.warningIssued[i] = false;
	}

	UnifiedAdjustSpawnRate(g_horde_local.current_map_size, lvl, GetNumHumanPlayers());

	int32_t total_planned_for_wave = g_horde_local.num_to_spawn + g_horde_local.queued_monsters;
	g_totalMonstersInWave = static_cast<uint16_t>(
		std::min(total_planned_for_wave, static_cast<int32_t>(std::numeric_limits<uint16_t>::max())));

	switch (lvl)
	{
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

	if (developer->integer)
	{
		gi.Com_PrintFmt("Horde_InitLevel: Wave {}. num_to_spawn: {}, queued: {}. Total for wave: {}\n",
			lvl, g_horde_local.num_to_spawn, g_horde_local.queued_monsters, g_totalMonstersInWave);
	}

	ProcessWaveRewards(lvl);
	Horde_CleanBodies();
}
