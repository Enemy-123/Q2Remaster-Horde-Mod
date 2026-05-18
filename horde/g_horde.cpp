// Includes y definiciones relevantes
#include "../shared.h"
#include "g_horde_phys.h"
#include "../g_local.h"
#include "g_horde.h"
#include <set>
#include <boost/container/flat_set.hpp>
#include <boost/container/small_vector.hpp>
#include <algorithm>  // For std::count_if and std::shuffle
#include "g_horde_benefits.h"
#include "horde_ids.h"
#include <span>
#include "g_laser.h"
#include "../profiler.h"
#include <cassert>
#include "horde_performance.h"
#include "horde_boss.h"
#include "../memory_safety.h"
#include "horde_spawning.h"
#include "horde_constants.h"
#include "horde_monster_data.h"
#include "g_horde_scaling.h"
#include "g_pvm.h"  // For PvM mode checks
#include "p_flyer_morph.h"  // For ResetAllMorphData()
#include "../m_tank.h"  // For ResetTankTeleportCache()
#include <string_view>

// External function declarations
extern void ED_CallSpawnMonsterByID(edict_t* ent, horde::MonsterTypeID typeId);
extern void SP_target_orb(edict_t* ent);

// This represents the maximum possible level boost for the "elite spawn" mechanic.
// It ensures we precache and consider all potential elite monsters for a given wave.
constexpr int32_t MAX_EFFECTIVE_LEVEL_BOOST = 20;
// Style 1 spawn points are dedicated to flying lanes; keep a long reuse cooldown.
static constexpr gtime_t FLYING_ONLY_SPAWN_LONG_COOLDOWN = 12.0_sec;

// Maps an edict_t* to a compact index [0...N-1]
// g_spawn_point_map now in g_spawn_system


boost::container::flat_map<int, trap_state_t> g_trap_states;
boost::container::flat_map<int, EmitterState> g_emitter_states;

//aiming for special entities for idview.cpp
// Using small_vector to avoid heap allocation for typical maps (most have < 32 special entities)
boost::container::small_vector<edict_t*, 32> g_targetable_special_entities;

// Track monster to family mapping for reference counting
boost::container::flat_map<int, AssetFamilyID> g_monster_family_map;
// Provides a direct list of spawn point edicts for easy iteration
// Using small_vector to avoid heap allocation for typical maps (most have < 64 spawn points)
boost::container::small_vector<edict_t*, 64> g_spawn_point_list;

// The actual number of spawn points found on the map
size_t g_num_spawn_points = 0;

// Cached playable bounds for spawn validation (computed in BuildSpawnPointMap).
static vec3_t g_spawn_world_mins{};
static vec3_t g_spawn_world_maxs{};
static bool g_spawn_world_bounds_valid = false;

// spawn_map_needs_build now in g_spawn_system

// *** NEW: Use boost::container::flat_map instead of a giant static array ***
static boost::container::flat_map<int, gtime_t> last_boss_teleport_attempt_time;  // C++23 - per-boss teleport tracking 

// Forward declaration for the new map-building function
void BuildSpawnPointMap();
static const char* GetMonsterModelPath(horde::MonsterTypeID typeId);
// SpawnPlanEntry is now defined in horde_spawning.h

std::array<bool, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_precached_monster_types_flags = {}; // Initializes all to false
static bool g_full_precache_done = false;

static bool monsters_precached = false;
MonsterWaveType current_wave_type = MonsterWaveType::None;
// Using small_vector - typically < 32 entries per wave, avoids heap allocation in common case
boost::container::small_vector<const MonsterTypeInfo*, 32> g_eligible_monsters_for_wave;
boost::container::small_vector<size_t, 32> g_eligible_item_indices_for_wave;

// Progressive monster unlocking system for memory management
static boost::container::flat_set<horde::MonsterTypeID> g_excluded_monsters_this_map;  // Excluded monsters cache
boost::container::flat_set<horde::MonsterTypeID> g_precached_monsters_this_map; // Non-static for external access
boost::unordered::unordered_flat_set<std::string_view> g_precached_models_this_map; // Cache-friendly hash set for model tracking
static int g_map_rotation_seed = 0;
static int g_last_precache_wave = 0;

// Elite spawn dynamic precaching tracking (reset each wave in Horde_InitLevel)
static int32_t g_last_dynamic_precache_wave = -1;
static int g_dynamic_precache_count_this_wave = 0;

constexpr int MONSTERS_TO_EXCLUDE_PER_MAP = 15; // Reduced from 28 for more monster variety
constexpr int WAVES_BETWEEN_PRECACHE = 5; // RESTORED from 0.995: Add new monsters every 5 waves
constexpr int MIN_MONSTERS_AVAILABLE = 12; // Always have at least 12 monster types available

// --- Asset Family System Implementation ---
std::array<AssetFamilyID, 128> g_monster_to_family; // 128 = MAX_TYPES from horde_ids.h
boost::unordered::unordered_flat_set<std::string> g_precached_models;
boost::unordered::unordered_flat_set<std::string> g_precached_sounds;
int32_t g_total_precached_models = 0;
int32_t g_total_precached_sounds = 0;

// --- Spawn History Tracking ---
std::array<SpawnHistoryEntry, SPAWN_HISTORY_SIZE> g_spawn_history;
size_t g_spawn_history_index = 0;

// --- Per-Map Variety Tracking ---
// Tracks which families were heavily used in previous maps to avoid repetition across maps
constexpr size_t MAP_HISTORY_SIZE = 2; // Track last 2 maps
struct MapFamilyUsage {
	std::array<int32_t, static_cast<size_t>(AssetFamilyID::MAX_FAMILIES)> family_usage_counts{};
	int32_t map_seed = 0;
};
static std::array<MapFamilyUsage, MAP_HISTORY_SIZE> g_map_family_history;
static size_t g_map_history_index = 0;

// --- Precache Family Limit System ---
// Tracks which model families are precached this map to enforce the limit
static boost::container::flat_set<AssetFamilyID> g_precached_families_this_map;

// Core families that are ALWAYS precached (basic gameplay essentials + heavy units)
// These use CORE_FAMILY_SLOTS from PrecacheLimits (14 slots)
static constexpr std::array<AssetFamilyID, 14> CORE_FAMILIES = { {
	AssetFamilyID::SOLDIER_FAMILY,      // Always - basic enemies
	AssetFamilyID::INFANTRY_FAMILY,     // Always - basic enemies
	AssetFamilyID::GUNNER_FAMILY,       // Always - core mid-tier
	AssetFamilyID::BERSERK_FAMILY,      // Always - melee + fog wave (berserk/berserkerkl)
	AssetFamilyID::BRAIN_FAMILY,        // Always - special attacks
	AssetFamilyID::CHICK_FAMILY,        // Always - ranged variety
	AssetFamilyID::PARASITE_FAMILY,     // Always - small ground unit
	AssetFamilyID::FLYER_FAMILY,        // Always - basic flying
	AssetFamilyID::HOVER_FAMILY,        // Always - flying variety
	AssetFamilyID::STALKER_FAMILY,      // Always - small agile unit
	AssetFamilyID::TANK_FAMILY,         // Always - essential heavy
	AssetFamilyID::GLADIATOR_FAMILY,    // Always - essential heavy
	AssetFamilyID::MUTANT_FAMILY,       // Always - mutant+redmutant together
	AssetFamilyID::GEKK_FAMILY          // Always - wave 1 monster + fog wave (gekk/gekkkl)
} };

// Rotating families that vary per map (selected based on map seed)
// These compete for ROTATING_FAMILY_SLOTS from PrecacheLimits (6 slots)
// Note: TURRET removed - spawned by fixbot boss, sentrygun shares model
static constexpr std::array<AssetFamilyID, 9> ROTATING_FAMILIES = { {
	AssetFamilyID::MEDIC_FAMILY,        // Special - rotates
	AssetFamilyID::FLOATER_FAMILY,      // Flying - rotates
	AssetFamilyID::DAEDALUS_FAMILY,     // Flying - rotates
	AssetFamilyID::ARACHNID_FAMILY,     // Ground - rotates (shares model: spider, arachnid, gm_arachnid)
	AssetFamilyID::FIXBOT_FAMILY,       // Flying/special - rotates (fixbot boss spawns turrets)
	AssetFamilyID::INSANE_FAMILY,       // Special - rotates
	AssetFamilyID::GUARDIAN_FAMILY,     // Heavy - rotates (shares model: guardian, psx_guardian, janitor2)
	AssetFamilyID::SUPERTANK_FAMILY,    // Boss-tier - rotates (shares model: janitor, supertank, boss5)
	AssetFamilyID::SHAMBLER_FAMILY      // Boss-tier - rotates
} };

// Helper to check if a family is a core family (always precached)
static bool IsCoreFamily(AssetFamilyID family) {
	for (const auto& core : CORE_FAMILIES) {
		if (core == family) return true;
	}
	return false;
}

// Helper to check if we can precache a new family
static bool CanPrecacheFamily(AssetFamilyID family) {
	// Core families are always allowed
	if (IsCoreFamily(family)) return true;

	// Already precached? Allow it
	if (g_precached_families_this_map.find(family) != g_precached_families_this_map.end()) return true;

	// Check if we've hit the limit
	return static_cast<int32_t>(g_precached_families_this_map.size()) < PrecacheLimits::MAX_PRECACHED_MODEL_FAMILIES;
}

// Helper to mark a family as precached
static void MarkFamilyPrecached(AssetFamilyID family) {
	if (family != AssetFamilyID::UNKNOWN_FAMILY) {
		g_precached_families_this_map.insert(family);
	}
}

// Get current precached family count
static int32_t GetPrecachedFamilyCount() {
	return static_cast<int32_t>(g_precached_families_this_map.size());
}

int32_t monsters_spawned_in_current_phase = 0;
int32_t initial_total_monsters_for_spawning_phase_timeout = 0;


// All spawn state is now in g_spawn_system (horde/horde_spawning.h)

static bool g_special_high_level_monster_spawned_this_wave = false;

// Recovery mode (local to g_horde.cpp)
bool g_recovery_mode_active = false;

static bool need_frame_timer_reset = false;
static bool need_queue_monitor_reset = false;

static gtime_t g_horde_retaliation_end_time = 0_sec;
static gtime_t g_horde_retaliation_last_trigger_time = 0_sec;  // Cooldown tracking
// NOTE: Target player is stored in g_spawn_system.special_spawn_state.target_player (currently unused)

// Ambush system tracking variables
static gtime_t last_ambush_time = 0_sec;
static gtime_t ambush_cooldown_end = 0_sec;
static int32_t waves_since_ambush = 0;
// static bool ambush_system_initialized = false;

// --- Recent Spawn Position Tracking ---
struct RecentSpawnsSoA
{
	std::array<vec3_t, 32> positions = {};		 // Tightly packed array of vectors
	std::array<gtime_t, 32> cooldowns_until = {}; // Tightly packed array of times
};
static constexpr size_t MAX_RECENT_POSITIONS = 32; // History for TryAlternativeSpawnPosition
static RecentSpawnsSoA g_recent_spawns;
static size_t g_recent_spawn_index = 0; // Renamed from g_recent_position_index for clarity

// --- Recent Teleport Position Tracking (SoA) ---
static constexpr int MAX_RECENT_TELEPORT_LOCATIONS = 8;

struct RecentTeleportsSoA
{
	std::array<vec3_t, MAX_RECENT_TELEPORT_LOCATIONS> positions = {};
	std::array<gtime_t, MAX_RECENT_TELEPORT_LOCATIONS> teleport_times = {};
};

static RecentTeleportsSoA g_recent_teleports;
static size_t g_recent_teleport_index = 0;

// NOTE: HordeConstants namespace moved to horde_constants.h
// Additional constants that are only used in g_horde.cpp remain here
namespace HordeConstants
{
	// --- Spawn Point Cooldowns (local to g_horde.cpp) ---
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
	// NOTE: MIN_MONSTER_SPAWN_INTERVAL moved to horde_constants.h
	// NOTE: BASE_SPAWN_TIMES also defined in SetNextMonsterSpawnTime() - keep in sync!
	constexpr std::array<std::pair<gtime_t, gtime_t>, 3> BASE_SPAWN_TIMES = { {
		{0.8_sec, 1.0_sec}, // Small maps - slower for better pacing
		{1.0_sec, 1.2_sec}, // Medium maps
		{0.7_sec, 0.9_sec}	// Big maps
	} };

	// --- Base Stuck Monster / Teleport Logic ---
	constexpr gtime_t NO_DAMAGE_TIMEOUT_BASE = 32_sec;
	constexpr gtime_t DAMAGED_MONSTER_INACTIVITY_TIMEOUT_BASE = 15.0_sec;
	constexpr gtime_t NO_ENEMY_TIMEOUT_BASE = 12.0_sec;
	constexpr gtime_t MIN_TELEPORT_COOLDOWN_MONSTER = 12_sec;
	constexpr gtime_t MAX_TELEPORT_COOLDOWN_MONSTER = 20_sec;
	constexpr gtime_t GLOBAL_TELEPORT_RESET_INTERVAL = 12_sec;
	constexpr int MAX_TELEPORTS_PER_INTERVAL = 2;
	constexpr int MAX_FAILSAFE_TELEPORTS_PER_MONSTER = 5; // Kill monster after this many failsafe teleports
	inline int g_teleport_rate_count = 0;
	inline gtime_t g_teleport_rate_reset_time = level.time;

	// --- Map-Size-Aware Timeout Functions ---
	static inline gtime_t GetNoDamageTimeout(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 45_sec; // Increased from 32_sec for small maps
		}
		else if (mapSize.isMediumMap) {
			return 38_sec; // Slightly increased for medium maps
		}
		else {
			return NO_DAMAGE_TIMEOUT_BASE; // Keep original for big maps
		}
	}

	static inline gtime_t GetDamagedMonsterTimeout(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 20_sec; // Increased from 15_sec for small maps
		}
		else if (mapSize.isMediumMap) {
			return 17_sec; // Slightly increased for medium maps
		}
		else {
			return DAMAGED_MONSTER_INACTIVITY_TIMEOUT_BASE; // Keep original for big maps
		}
	}

	static inline gtime_t GetNoEnemyTimeout(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 15_sec; // Increased from 12_sec for small maps
		}
		else if (mapSize.isMediumMap) {
			return 13_sec; // Slightly increased for medium maps
		}
		else {
			return NO_ENEMY_TIMEOUT_BASE; // Keep original for big maps
		}
	}

	// --- Base Proximity / Distance Checks ---
	constexpr vec3_t VALIDATE_CHECK_MINS = { -16, -16, -24 };
	constexpr vec3_t VALIDATE_CHECK_MAXS = { 16, 16, 32 };
	constexpr float MIN_PLAYER_DIST_GENERATE_BASE = 400.0f;  // Doubled from 200
	constexpr float MIN_PLAYER_DIST_CHECK_BASE = 500.0f;    // Doubled from 360
	constexpr float MIN_RECENT_TELEPORT_DIST_BASE = 450.0f; // Doubled from 300

	// --- Map-Size-Aware Distance Functions ---
	// All distances doubled to prevent spawning too close to players
	static inline float GetMinPlayerDistGenerate(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 240.0f; // Doubled from 120.0f for small maps
		}
		else if (mapSize.isMediumMap) {
			return 320.0f; // Doubled from 160.0f for medium maps
		}
		else {
			return MIN_PLAYER_DIST_GENERATE_BASE; // 400 for big maps
		}
	}

	static inline float GetMinPlayerDistCheck(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 350.0f; // Doubled from 220.0f for small maps
		}
		else if (mapSize.isMediumMap) {
			return 420.0f; // Doubled from 290.0f for medium maps
		}
		else {
			return MIN_PLAYER_DIST_CHECK_BASE; // 500 for big maps
		}
	}

	static inline float GetMinRecentTeleportDist(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 300.0f; // Doubled from 200.0f for small maps
		}
		else if (mapSize.isMediumMap) {
			return 380.0f; // Doubled from 250.0f for medium maps
		}
		else {
			return MIN_RECENT_TELEPORT_DIST_BASE; // 450 for big maps
		}
	}

	// Legacy constants kept for backward compatibility - use getter functions for map-size-aware values
	constexpr float MIN_RECENT_TELEPORT_DIST_SQ = MIN_RECENT_TELEPORT_DIST_BASE * MIN_RECENT_TELEPORT_DIST_BASE;

	constexpr gtime_t RECENT_TELEPORT_COOLDOWN = 20.0_sec;

	// --- Wave Timing ---
	constexpr gtime_t BOSS_TIME_BONUS = 100_sec;
	constexpr int MONSTERS_FOR_AGGRESSIVE_REDUCTION = 5;
	constexpr gtime_t WAVE_STUCK_TIMEOUT = 3_min;
	constexpr std::array<float, 3> WAVE_END_WARNING_TIMES = { 30.0f, 10.0f, 5.0f };

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

} // namespace HordeConstants

// --- Forward Declarations ---
bool Horde_AttemptToUnstickMonster(edict_t* self);
static void Horde_InitLevel(const int32_t lvl);
static void RecordCurrentMapFamilyUsageToHistory();
bool ApplyHordeBonuses(edict_t* monster, int32_t currentLevel, float champion_chance); // monster bonuses
void CalculateTopDamager(PlayerStats& topDamager, float& percentage);
void AwardKillXP(edict_t* attacker, edict_t* monster); // Award XP for kills

// FIXED: Return struct instead of in-out parameter for clarity
struct PositionValidationResult {
	bool is_valid;
	vec3_t adjusted_position;
};

[[nodiscard]] PositionValidationResult IsPositionPhysicallyValid(const vec3_t& position, const vec3_t& monster_mins, const vec3_t& monster_maxs, bool is_flying);
bool CheckAndTeleportStuckMonster(edict_t* self);
static void AnnounceIncomingWave(gtime_t duration = 3_sec);
static edict_t* FindBestPlayerTargetForTeleport();


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
	alignas(32) std::array<float, MAX_POSITIONS> x_coords{};
	alignas(32) std::array<float, MAX_POSITIONS> y_coords{};
	alignas(32) std::array<float, MAX_POSITIONS> z_coords{};
	alignas(32) std::array<gtime_t, MAX_POSITIONS> expiry_times{};

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
				const float dist_sq = dx * dx + dy * dy + dz * dz;

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

//  interface functions (extern - used by horde_spawning.cpp)
void MarkPositionAsRecentlyUsed(const vec3_t& position) {
	g_recent_spawns_opt.AddPosition(position, HordeConstants::RECENT_SPAWN_COOLDOWN);
}

bool IsPositionTooCloseToRecentSpawn(const vec3_t& position, const horde::MapSize& mapSize) {
	const float min_dist = HordeConstants::GetMinRecentSpawnDist(mapSize);
	return g_recent_spawns_opt.IsPositionTooClose(position, min_dist * min_dist);
}

static inline void MarkPositionAsRecentlyTeleported(const vec3_t& position) {
	g_recent_teleports_opt.AddPosition(position, HordeConstants::RECENT_TELEPORT_COOLDOWN);
}

static inline bool IsPositionTooCloseToRecentTeleport(const vec3_t& position, const horde::MapSize& mapSize) {
	const float min_dist = HordeConstants::GetMinRecentTeleportDist(mapSize);
	return g_recent_teleports_opt.IsPositionTooClose(position, min_dist * min_dist);
}

// (Keep ResetSpawnMonsterVars, ResetFrameTimers, ResetQueueMonitorVars as they were)
void ResetSpawnMonsterVars()
{
	g_spawn_system.need_spawn_cache_reset = true;
	horde::g_monsterSpawnTracker.Reset();
}

// Check if a monster type has been precached for the current wave
bool IsMonsterTypePrecached(horde::MonsterTypeID typeId)
{
	if (!g_horde->integer)
		return true; // In non-horde mode, allow all monsters

	if (typeId == horde::MonsterTypeID::UNKNOWN)
		return false;

	size_t index = static_cast<size_t>(typeId);
	if (index >= g_precached_monster_types_flags.size())
		return false;

	return g_precached_monster_types_flags[index];
}

void ResetQueueMonitorVars()
{
	need_queue_monitor_reset = true;
	horde::g_spawnPointTimeTracker.Reset();
}

// --- Global/Static Variables ---
static constexpr int32_t MAX_ELITES_PER_WAVE = 1;  // Closer to 0.995 pacing: avoid stacking elite/champion pressure
int32_t elites_spawned_this_wave = 0;  // Counter instead of boolean
// int champion_spawn_cooldown = 0;
static gtime_t champion_spawn_cooldown_ends_at = 0_sec;
int consistent_zero_counts = 0;
int counter_mismatch_frames = 0;
// MAX_SPAWN_POINTS is now defined in memory_safety.h

// SpawnPointsSoA struct moved to horde_spawning.h
// SpawnPointsSoA now in g_spawn_system

static void ApplyAlternativePositionCooldown(edict_t* spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;

	const uint16_t index = GetSpawnPointIndexSafe(spawn_point);
	if (index == 0xFFFF) [[unlikely]]
		return;

	g_spawn_system.spawn_points_data.alternative_attempts[index]++;
	gtime_t cooldown_duration;
	const uint16_t alt_attempts = g_spawn_system.spawn_points_data.alternative_attempts[index];

	if (alt_attempts <= 2)
		cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_SHORT;
	else if (alt_attempts <= 5)
		cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_MEDIUM;
	else
	{
		cooldown_duration = 5.0_sec + gtime_t::from_sec(0.5f * (alt_attempts - 5));
		cooldown_duration = std::min(cooldown_duration, 10.0_sec);
		if (alt_attempts >= 8)
			g_spawn_system.spawn_points_data.needs_long_alternative_cooldown[index] = true;
	}

	const gtime_t final_alt_duration = std::max(cooldown_duration, HordeConstants::MIN_ALT_FAILURE_COOLDOWN);
	g_spawn_system.spawn_points_data.alternative_cooldown[index] = level.time + final_alt_duration;

	g_spawn_system.spawn_points_data.isTemporarilyDisabled[index] = true;
	const gtime_t final_normal_duration = std::max(final_alt_duration * 0.5f, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
	g_spawn_system.spawn_points_data.cooldownEndsAt[index] = level.time + final_normal_duration;
	g_spawn_system.spawn_points_data.lastSpawnTime[index] = level.time;

	// 	if (developer->integer)
	// 		gi.Com_PrintFmt("Alternative position cooldown applied to spawn at ({:.1f}, {:.1f}, {:.1f}): {:.1f}s (attempts: {})\n",
	// 						spawn_point->s.origin.x, spawn_point->s.origin.y, spawn_point->s.origin.z,
	// 						final_alt_duration.seconds(), alt_attempts);
}

static void ApplyLongFlyingLaneCooldown(edict_t* spawn_point)
{
	if (!spawn_point || !spawn_point->inuse || spawn_point->style != 1)
		return;

	const uint16_t index = GetSpawnPointIndexSafe(spawn_point);
	if (index == 0xFFFF) [[unlikely]]
		return;

	const gtime_t cooldown_end = level.time + FLYING_ONLY_SPAWN_LONG_COOLDOWN;
	g_spawn_system.spawn_points_data.teleport_cooldown[index] =
		std::max(g_spawn_system.spawn_points_data.teleport_cooldown[index], cooldown_end);
	g_spawn_system.spawn_points_data.alternative_cooldown[index] =
		std::max(g_spawn_system.spawn_points_data.alternative_cooldown[index], cooldown_end);
	g_spawn_system.spawn_points_data.cooldownEndsAt[index] =
		std::max(g_spawn_system.spawn_points_data.cooldownEndsAt[index], cooldown_end);
	g_spawn_system.spawn_points_data.isTemporarilyDisabled[index] = true;
	g_spawn_system.spawn_points_data.lastSpawnTime[index] = level.time;
}

void IncreaseSpawnAttempts(edict_t* spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;

	const uint16_t index = GetSpawnPointIndexSafe(spawn_point);
	if (index == 0xFFFF) [[unlikely]]
		return;

	if (level.time - g_spawn_system.spawn_points_data.lastSpawnTime[index] > HordeConstants::SPAWN_POINT_INACTIVITY_RESET_THRESHOLD)
	{
		g_spawn_system.spawn_points_data.attempts[index] = 0;
		g_spawn_system.spawn_points_data.isTemporarilyDisabled[index] = false;
		g_spawn_system.spawn_points_data.cooldownEndsAt[index] = 0_sec;
		g_spawn_system.spawn_points_data.lastSpawnTime[index] = level.time;
		return;
	}

	g_spawn_system.spawn_points_data.attempts[index]++;

	const uint16_t current_attempts = g_spawn_system.spawn_points_data.attempts[index];
	const int32_t current_successes = g_spawn_system.spawn_points_data.successfulSpawns[index];
	const float success_rate = (current_attempts > 0) ? (static_cast<float>(current_successes) / current_attempts) : 1.0f;

	const int max_attempts = 4 + (success_rate >= 0.5f ? 2 : (success_rate >= 0.25f ? 1 : 0));

	gtime_t calculated_duration = 0_sec;

	if (current_attempts >= max_attempts)
	{
		g_spawn_system.spawn_points_data.isTemporarilyDisabled[index] = true;
		const float cooldown_factor = success_rate < 0.3f ? 1.5f : 0.75f;
		const float attempt_multiplier = current_attempts <= 8 ? current_attempts * 0.25f : 2.0f;
		calculated_duration = gtime_t::from_sec(cooldown_factor * attempt_multiplier);
		// if (developer->integer == 1)
		// 	gi.Com_PrintFmt("SpawnPoint at {} inactivated for adaptive cooldown.\n", spawn_point->s.origin);
	}
	else if ((current_attempts & 1) == 0)
	{
		calculated_duration = gtime_t::from_sec(0.2f * current_attempts);
	}

	if (calculated_duration > 0_sec)
	{
		const gtime_t final_duration = std::max(calculated_duration, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
		g_spawn_system.spawn_points_data.cooldownEndsAt[index] = level.time + final_duration;
	}
	g_spawn_system.spawn_points_data.lastSpawnTime[index] = level.time;
}
//s
void OnSuccessfulSpawn(edict_t* spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;

	const uint16_t index = GetSpawnPointIndexSafe(spawn_point);
	if (index == 0xFFFF) [[unlikely]]
		return;

	g_spawn_system.spawn_points_data.successfulSpawns[index]++;
	g_spawn_system.spawn_points_data.attempts[index] = 0;
	g_spawn_system.spawn_points_data.isTemporarilyDisabled[index] = false;
	g_spawn_system.spawn_points_data.cooldownEndsAt[index] = level.time + HordeConstants::MIN_INDIVIDUAL_SUCCESS_COOLDOWN;
	ApplyLongFlyingLaneCooldown(spawn_point);

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
	// Using small_vector to avoid heap allocation (max 64 spawn points per map)
	boost::container::small_vector<SpawnPointCache, 64> data;

	// FIX: Access operator should NOT have side effects - removed BuildSpawnPointMap() call
	// The map must be built explicitly before accessing the cache to avoid race conditions
	SpawnPointCache& operator[](const edict_t* ent) {
		// Assert in debug builds if map wasn't built - this catches programming errors
		if (g_spawn_system.spawn_map_needs_build) {
			gi.Com_PrintFmt("ERROR: Accessing spawn point cache before BuildSpawnPointMap() was called!\n");
			gi.Com_PrintFmt("ERROR: This is a programming error - BuildSpawnPointMap() must be called explicitly.\n");
			assert(false && "spawn_map_needs_build flag set - BuildSpawnPointMap() was not called!");
		}

		// OPTIMIZATION: O(1) vector lookup instead of O(log N) map lookup
		// SAFETY: Use bounds-checked helper to prevent overflow
		const uint16_t index = GetSpawnPointIndexSafe(ent);
		if (index == 0xFFFF) [[unlikely]] {
			gi.Com_PrintFmt("ERROR: Spawn point {} not found in map (or out of bounds)!\n", ent->s.number);

			// Debug assertion to catch this during development
			assert(false && "spawn point not found in map!");

			// Robust fallback: return reference to static default cache
			static SpawnPointCache default_cache{};
			default_cache = {};
			return default_cache;
		}

		// Additional safety check
		if (index >= data.size()) {
			gi.Com_PrintFmt("ERROR: Invalid spawn point index {} (size: {})\n", index, data.size());
			gi.Com_PrintFmt("DEBUG: Map built? {} Spawn points: {}\n", !g_spawn_system.spawn_map_needs_build, g_num_spawn_points);
			assert(false && "invalid spawn point index!");
			static SpawnPointCache default_cache{};
			default_cache = {};
			return default_cache;
		}

		return data[index];
	}

	const SpawnPointCache& operator[](const edict_t* ent) const {
		// Note: const version can't modify g_spawn_system.spawn_map_needs_build, so this is a warning only
		if (g_spawn_system.spawn_map_needs_build) {
			gi.Com_PrintFmt("ERROR: Const access to spawn point cache before BuildSpawnPointMap()\n");
		}

		// OPTIMIZATION: O(1) vector lookup instead of O(log N) map lookup
		// SAFETY: Use bounds-checked helper to prevent overflow
		const uint16_t index = GetSpawnPointIndexSafe(ent);
		if (index == 0xFFFF) [[unlikely]] {
			gi.Com_PrintFmt("ERROR: Spawn point {} not found in map (or out of bounds)!\n", ent->s.number);

			// Debug assertion to catch this during development
			assert(false && "spawn point not found in map!");

			// Robust fallback: return reference to static default cache
			static const SpawnPointCache default_cache{};
			return default_cache;
		}

		// Additional safety check
		if (index >= data.size()) {
			gi.Com_PrintFmt("ERROR: Invalid spawn point index {} (size: {})\n", index, data.size());
			gi.Com_PrintFmt("DEBUG: Map built? {} Spawn points: {}\n", !g_spawn_system.spawn_map_needs_build, g_num_spawn_points);
			assert(false && "invalid spawn point index!");
			static const SpawnPointCache default_cache{};
			return default_cache;
		}

		return data[index];
	}

	void resize(size_t new_size) {
		// Use safe resize to prevent bad_alloc
		if (new_size > MAX_SAFE_CONTAINER_SIZE) {
			gi.Com_PrintFmt("WARNING: Attempted to resize spawn cache to {}, clamping to {}\n",
				new_size, MAX_SAFE_CONTAINER_SIZE);
			new_size = MAX_SAFE_CONTAINER_SIZE;
		}
		try {
			data.assign(new_size, {});
		}
		catch (const std::bad_alloc&) {
			gi.Com_Print("ERROR: Failed to allocate memory for spawn point cache\n");
			data.clear();
		}
	}

	void clear() {
		data.clear();
	}
};;


// Static vector cache using compact indices - faster than unordered_map
// spawn_validation_cache now in g_spawn_system
static SpawnPointCacheArray spawn_point_cache;

void BuildSpawnPointMap()
{
	g_spawn_system.spawn_point_map.clear();
	g_spawn_point_list.clear();

	// Initialize O(1) lookup vector (indexed by entity number)
	// Size = globals.max_edicts, initialize with 0xFFFF (invalid marker)
	g_spawn_system.spawn_point_index_lookup.assign(globals.max_edicts, 0xFFFF);

	// Clear and rebuild spatial index
	HordePerf::g_spawn_spatial_index.Clear();

	// Reserve capacity to avoid repeated allocations - use safe allocation
	// Start with 32 as initial capacity, most maps have fewer spawn points
	constexpr size_t INITIAL_SPAWN_CAPACITY = 32;
	if (!safe_reserve(g_spawn_point_list, INITIAL_SPAWN_CAPACITY)) {
		gi.Com_Print("ERROR: Failed to reserve memory for spawn points\n");
		return;
	}

	// Single pass to build both the list and the map
	for (edict_t* sp : monster_spawn_points()) {
		if (sp && sp->inuse && sp->classname && strcmp(sp->classname, "info_player_deathmatch") == 0) {
			// Check for overflow before adding
			if (g_spawn_point_list.size() >= MAX_SPAWN_POINTS) {
				gi.Com_PrintFmt("WARNING: Too many spawn points ({}), capping at {}\n",
					static_cast<size_t>(g_spawn_point_list.size() + 1), static_cast<size_t>(MAX_SPAWN_POINTS));
				break;
			}

			// OPTIMIZATION: O(1) vector lookup instead of O(log N) map lookup
			// SAFETY: Bounds check to prevent out-of-bounds write
			if (sp->s.number >= g_spawn_system.spawn_point_index_lookup.size()) [[unlikely]] {
				gi.Com_PrintFmt("ERROR: Spawn point entity number {} exceeds max_edicts {}! Skipping.\n",
					sp->s.number, g_spawn_system.spawn_point_index_lookup.size());
				continue;
			}

			const uint16_t compact_index = static_cast<uint16_t>(g_spawn_point_list.size());
			if (!safe_push_back(g_spawn_point_list, sp, MAX_SPAWN_POINTS)) {
				gi.Com_Print("WARNING: Failed to add spawn point\n");
				break;
			}

			g_spawn_system.spawn_point_map[sp->s.number] = compact_index;
			g_spawn_system.spawn_point_index_lookup[sp->s.number] = compact_index;

			// Add to spatial index for fast spatial queries
			HordePerf::g_spawn_spatial_index.AddSpawnPoint(sp);
		}
	}

	g_num_spawn_points = g_spawn_point_list.size();

	// Shrink to fit to release any excess capacity
	g_spawn_point_list.shrink_to_fit();

	// Finally, resize all data structures to the exact size needed with safety checks
	// g_spawn_system.spawn_points_data has its own resize method that handles all its vectors
	if (g_num_spawn_points > MAX_SAFE_CONTAINER_SIZE) {
		gi.Com_PrintFmt("ERROR: Too many spawn points ({}) exceeds maximum ({})\n",
			g_num_spawn_points, MAX_SAFE_CONTAINER_SIZE);
		g_num_spawn_points = MAX_SAFE_CONTAINER_SIZE;
	}
	g_spawn_system.spawn_points_data.resize(g_num_spawn_points);

	// spawn_point_cache also has its own resize method
	spawn_point_cache.resize(g_num_spawn_points);

	// g_spawn_system.spawn_validation_cache is a std::vector, use safe_resize
	if (!safe_resize(g_spawn_system.spawn_validation_cache, g_num_spawn_points)) {
		gi.Com_Print("ERROR: Failed to resize spawn validation cache\n");
	}

	if (developer->integer > 1) {
		gi.Com_PrintFmt("Spawn Point Map Built: Found {} spawn points with spatial index.\n", g_num_spawn_points);
	}

	// Generate spawn grid for better out-of-bounds prevention
	// Calculate world bounds - try multiple methods for maximum reliability
	vec3_t world_mins{}, world_maxs{};
	ClearBounds(world_mins, world_maxs);
	int bounds_source_count = 0;

	// Method 1: Parse .ent file to get ALL entity positions (most comprehensive)
	std::vector<char> ent_buffer;
	std::string ent_filename;
	extern bool LoadEntityFile(std::string_view mapname, std::vector<char>&buffer, std::string & outFilename);

	if (LoadEntityFile(level.mapname, ent_buffer, ent_filename)) {
		const char* data = ent_buffer.data();
		constexpr float STYLE1_MATCH_DIST_SQ = 64.0f * 64.0f;
		boost::container::small_vector<vec3_t, 32> style1_spawn_hints;

		// Parse entities and collect:
		// 1) all origins for world-bounds building, and
		// 2) info_player_deathmatch positions with style 1
		while (data && *data) {
			const char* token = COM_Parse(&data);
			if (!token || !*token)
				break;
			if (token[0] != '{')
				continue;

			bool is_dm_spawn = false;
			bool has_origin = false;
			int32_t style = 0;
			vec3_t origin{};

			while (data && *data) {
				const char* key = COM_Parse(&data);
				if (!key || !*key)
					break;
				if (key[0] == '}')
					break;

				const char* value = COM_Parse(&data);
				if (!value || !*value)
					break;

				if (Q_strcasecmp(key, "classname") == 0) {
					is_dm_spawn = (Q_strcasecmp(value, "info_player_deathmatch") == 0);
				}
				else if (Q_strcasecmp(key, "origin") == 0) {
					if (sscanf(value, "%f %f %f", &origin.x, &origin.y, &origin.z) == 3) {
						has_origin = true;
						AddPointToBounds(origin, world_mins, world_maxs);
						bounds_source_count++;
					}
				}
				else if (Q_strcasecmp(key, "style") == 0) {
					style = atoi(value);
				}
			}

			if (is_dm_spawn && has_origin && style == 1) {
				style1_spawn_hints.push_back(origin);
			}
		}

		// Apply style 1 hints only when runtime entities have no style-1 points.
		// This avoids remapping style lanes away from mapper-authored runtime spawns.
		size_t runtime_style1_count = 0;
		for (const edict_t* sp : g_spawn_point_list)
		{
			if (sp && sp->inuse && sp->style == 1)
				runtime_style1_count++;
		}

		size_t style1_applied = 0;
		if (runtime_style1_count == 0)
		{
			for (const vec3_t& hint_origin : style1_spawn_hints)
			{
				edict_t* best_match = nullptr;
				float best_dist_sq = STYLE1_MATCH_DIST_SQ;

				for (edict_t* sp : g_spawn_point_list)
				{
					if (!sp || !sp->inuse || !is_valid_vector(sp->s.origin))
						continue;

					const float dist_sq = (sp->s.origin - hint_origin).lengthSquared();
					if (dist_sq <= best_dist_sq)
					{
						best_dist_sq = dist_sq;
						best_match = sp;
					}
				}

				if (best_match && best_match->style != 1)
				{
					best_match->style = 1;
					style1_applied++;
				}
			}
		}

		if (developer->integer > 1 && bounds_source_count > 0) {
			gi.Com_PrintFmt("Calculated world bounds from .ent file ({} entities)\n", bounds_source_count);
		}
		if (developer->integer > 1 && !style1_spawn_hints.empty()) {
			gi.Com_PrintFmt("Applied {} style-1 DM spawn hints from .ent ({} hints found)\n",
				style1_applied, style1_spawn_hints.size());
			if (runtime_style1_count > 0)
			{
				gi.Com_PrintFmt("Skipped style-1 hint remap because runtime style-1 points already exist ({}).\n",
					runtime_style1_count);
			}
		}
	}

	// Method 2 (Fallback): Use runtime entities if .ent parsing failed
	if (bounds_source_count == 0) {
		if (developer->integer > 1)
			gi.Com_Print("Fallback: Calculating bounds from runtime entities...\n");

		for (int i = game.maxclients + 1; i < static_cast<int>(globals.num_edicts); i++) {
			edict_t* ent = &g_edicts[i];
			if (!ent->inuse) continue;

			// Include spawn points, items, weapons, etc
			if (ent->classname && is_valid_vector(ent->s.origin)) {
				if (strstr(ent->classname, "info_") ||
					strstr(ent->classname, "weapon_") ||
					strstr(ent->classname, "ammo_") ||
					strstr(ent->classname, "item_") ||
					strstr(ent->classname, "misc_")) {
					AddPointToBounds(ent->s.origin, world_mins, world_maxs);
					bounds_source_count++;
				}
			}
		}

		if (developer->integer > 1)
			gi.Com_PrintFmt("Calculated bounds from {} runtime entities\n", bounds_source_count);
	}

	// Method 3 (Last resort): Use spawn points only
	if (bounds_source_count == 0) {
		if (developer->integer > 1)
			gi.Com_Print("Last resort: Using spawn points for bounds...\n");
		for (edict_t* sp : g_spawn_point_list) {
			if (sp && sp->inuse && is_valid_vector(sp->s.origin)) {
				AddPointToBounds(sp->s.origin, world_mins, world_maxs);
				bounds_source_count++;
			}
		}
		if (developer->integer > 1)
			gi.Com_PrintFmt("Calculated bounds from {} spawn points\n", bounds_source_count);
	}

	// Safety check
	if (bounds_source_count == 0) {
		gi.Com_Print("ERROR: Failed to calculate world bounds - using default\n");
		world_mins = { -2048, -2048, -512 };
		world_maxs = { 2048, 2048, 512 };
	}
	else {
		// Add padding to ensure grid covers entire playable area
		world_mins -= vec3_t{ 512, 512, 512 };
		world_maxs += vec3_t{ 512, 512, 512 };
	}

	// Cache bounds so all spawn paths can perform in-map validation.
	g_spawn_world_mins = world_mins;
	g_spawn_world_maxs = world_maxs;
	g_spawn_world_bounds_valid = true;

	// Generate grid when enabled, when there are no classic spawn points, or when
	// boss waves need a fallback because this map has no registered boss origin.
	vec3_t boss_origin_check;
	const bool needs_boss_grid_fallback = !horde::MapOriginRegistry::GetOrigin(level.mapname, boss_origin_check);
	const bool grid_enabled_for_map = (g_horde_grid_first && g_horde_grid_first->integer != 0);
	if (grid_enabled_for_map || g_num_spawn_points == 0 || needs_boss_grid_fallback)
	{
		if (developer->integer > 1)
			gi.Com_Print("Generating spawn grid...\n");
		if (HordePhys::g_spawn_grid.Generate(world_mins, world_maxs))
		{
			if (developer->integer > 1)
				gi.Com_PrintFmt("Spawn grid ready with {} nodes.\n", HordePhys::g_spawn_grid.GetNodeCount());
		}
		else
		{
			gi.Com_Print("WARNING: Spawn grid generation failed, emergency spawning will use fallback method.\n");
		}
	}
	else
	{
		HordePhys::g_spawn_grid.Clear();
		if (developer->integer > 1)
			gi.Com_Print("Spawn grid disabled for this map, using classic spawn points.\n");
	}

	// Virtual spawn generation:
	// Only generate virtual grid spawn points when there are NO classic DM spawns.
	// This keeps style-1 behavior anchored to real info_player_deathmatch entities.
	const bool grid_available = HordePhys::g_spawn_grid.IsGenerated() && HordePhys::g_spawn_grid.GetNodeCount() > 0;
	const bool no_classic_spawns = (g_num_spawn_points == 0);
	if (grid_available && no_classic_spawns)
	{
		const int grid_node_count = HordePhys::g_spawn_grid.GetNodeCount();
		const int available_slots = std::max(0, static_cast<int>(MAX_SPAWN_POINTS) - static_cast<int>(g_spawn_point_list.size()));

		int target_spawn_count = std::min(std::max(grid_node_count / 10, 16), available_slots);
		if (developer->integer > 1)
			gi.Com_PrintFmt("No traditional spawn points found, creating virtual spawn points from grid ({} nodes available)...\n",
				grid_node_count);

		if (target_spawn_count > 0)
		{
			int virtual_spawns_created = 0;
			int attempts = 0;
			const int max_attempts = std::max(grid_node_count * 2, target_spawn_count * 8); // Prevent infinite loops

			while (virtual_spawns_created < target_spawn_count && attempts < max_attempts)
			{
				attempts++;

				vec3_t grid_pos;
				const bool got_position = HordePhys::g_spawn_grid.GetRandomPosition(grid_pos);

				if (!got_position)
					continue;

				// Keep spacing between spawn points to avoid clustering.
				bool too_close = false;
				constexpr float MIN_VIRTUAL_SPAWN_SPACING = 224.0f;
				for (edict_t* existing_sp : g_spawn_point_list)
				{
					if (existing_sp && existing_sp->inuse && is_valid_vector(existing_sp->s.origin))
					{
						if ((grid_pos - existing_sp->s.origin).lengthSquared() < MIN_VIRTUAL_SPAWN_SPACING * MIN_VIRTUAL_SPAWN_SPACING)
						{
							too_close = true;
							break;
						}
					}
				}
				if (too_close)
					continue;

				// Create a virtual DM spawn point entity.
				edict_t* virtual_spawn = G_Spawn();
				if (!virtual_spawn)
				{
					gi.Com_Print("ERROR: Failed to spawn virtual spawn point entity\n");
					break;
				}

				virtual_spawn->classname = "info_player_deathmatch";
				virtual_spawn->s.origin = grid_pos;
				virtual_spawn->s.angles = vec3_t{ 0, frandom() * 360.0f, 0 }; // Random yaw
				virtual_spawn->style = 0;
				virtual_spawn->inuse = true;
				virtual_spawn->solid = SOLID_NOT;
				virtual_spawn->movetype = MOVETYPE_NONE;
				gi.linkentity(virtual_spawn);

				if (g_spawn_point_list.size() >= MAX_SPAWN_POINTS)
				{
					gi.Com_PrintFmt("WARNING: Hit max spawn points limit while creating virtual spawns\n");
					G_FreeEdict(virtual_spawn);
					break;
				}

				if (virtual_spawn->s.number >= g_spawn_system.spawn_point_index_lookup.size())
				{
					gi.Com_PrintFmt("ERROR: Virtual spawn entity number {} exceeds lookup table size {}! Skipping.\n",
						virtual_spawn->s.number, g_spawn_system.spawn_point_index_lookup.size());
					G_FreeEdict(virtual_spawn);
					continue;
				}

				const uint16_t compact_index = static_cast<uint16_t>(g_spawn_point_list.size());
				if (!safe_push_back(g_spawn_point_list, virtual_spawn, MAX_SPAWN_POINTS))
				{
					gi.Com_Print("WARNING: Failed to add virtual spawn point to list\n");
					G_FreeEdict(virtual_spawn);
					break;
				}

				g_spawn_system.spawn_point_map[virtual_spawn->s.number] = compact_index;
				g_spawn_system.spawn_point_index_lookup[virtual_spawn->s.number] = compact_index;

				HordePerf::g_spawn_spatial_index.AddSpawnPoint(virtual_spawn);

				virtual_spawns_created++;
			}

			// Update counts and resize structures.
			g_num_spawn_points = g_spawn_point_list.size();
			g_spawn_point_list.shrink_to_fit();

			if (g_num_spawn_points > MAX_SAFE_CONTAINER_SIZE)
			{
				gi.Com_PrintFmt("ERROR: Too many spawn points ({}) exceeds maximum ({})\n",
					g_num_spawn_points, MAX_SAFE_CONTAINER_SIZE);
				g_num_spawn_points = MAX_SAFE_CONTAINER_SIZE;
			}

			g_spawn_system.spawn_points_data.resize(g_num_spawn_points);
			spawn_point_cache.resize(g_num_spawn_points);
			if (!safe_resize(g_spawn_system.spawn_validation_cache, g_num_spawn_points))
			{
				gi.Com_Print("ERROR: Failed to resize spawn validation cache for virtual spawns\n");
			}

			if (developer->integer > 1)
				gi.Com_PrintFmt("Created {} virtual spawn points from grid nodes.\n", virtual_spawns_created);
		}
	}
}

// A dedicated struct to pass data to our unified BoxEdicts lambda.
// This is clearer than reusing a generic struct.
struct OccupiedCheckData
{
	const edict_t* ignore_ent; // The entity to ignore in the check (usually the monster being spawned)
	SpawnPointCache* cache;	   // A pointer to the cache entry we need to modify
};

[[nodiscard]] bool IsSpawnPointOccupied(const edict_t* spawn_point, const edict_t* ignore_ent)
{
	if (!spawn_point || !spawn_point->inuse || !is_valid_vector(spawn_point->s.origin))
	{
		if (developer->integer)
			gi.Com_PrintFmt("Warning: IsSpawnPointOccupied called with invalid spawn_point or origin.\n");
		return true;
	}

	SpawnPointCache& cache = spawn_point_cache[spawn_point];
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
	static constexpr vec3_t check_mins = vec3_t{ -16, -16, -24 } *OCCUPANCY_CHECK_SCALE;
	static constexpr vec3_t check_maxs = vec3_t{ 16, 16, 32 } *OCCUPANCY_CHECK_SCALE;
	const vec3_t absolute_mins = spawn_point->s.origin + check_mins;
	const vec3_t absolute_maxs = spawn_point->s.origin + check_maxs;

	OccupiedCheckData check_data = { ignore_ent, &cache };

	gi.BoxEdicts(absolute_mins, absolute_maxs, nullptr, 0, AREA_SOLID, [](edict_t* ent, void* data) -> BoxEdictsResult_t
		{
			auto* cd = static_cast<OccupiedCheckData*>(data);
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
	if (g_spawn_system.potential_spawn_points.empty()) {
		return nullptr;
	}

	const size_t num_potential = g_spawn_system.potential_spawn_points.size();
	for (size_t i = 0; i < num_potential; ++i)
	{
		if (g_spawn_system.spawn_point_shuffle_index >= num_potential) {
			g_spawn_system.spawn_point_shuffle_index = 0; // Cycle back to the start
		}
		edict_t* spawnPoint = g_spawn_system.potential_spawn_points[g_spawn_system.spawn_point_shuffle_index++];

		if (!spawnPoint || !spawnPoint->inuse || !is_valid_vector(spawnPoint->s.origin))
		{
			continue;
		}

		const uint16_t index = GetSpawnPointIndexSafe(spawnPoint);
		if (index == 0xFFFF || index >= g_spawn_system.spawn_points_data.isTemporarilyDisabled.size())
		{
			continue;
		}

		if (
			(g_spawn_system.spawn_points_data.isTemporarilyDisabled[index] && level.time < g_spawn_system.spawn_points_data.cooldownEndsAt[index]) ||
			(level.time < g_spawn_system.spawn_points_data.alternative_cooldown[index]))
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
	auto try_pick = [&](bool strict_style_filter) -> edict_t*
		{
			return SelectNextShuffledSpawnPoint([&](const edict_t* sp) {
				if (!strict_style_filter)
					return true;

				const bool spawn_is_flying = (sp->style == 1);
				if (for_flying_monster)
					return spawn_is_flying;   // style 1 is dedicated to flying monsters
				return !spawn_is_flying;      // ground monsters must never use style 1
				});
		};

	// First pass: preserve dedicated spawn lanes (style 1 for flying, non-style-1 for ground).
	edict_t* spawnPoint = try_pick(true);

	// Fallback pass: if strict filtering leaves no options, allow any spawn point
	// only for flying monsters. Ground monsters must never use style 1 points.
	if (!spawnPoint && for_flying_monster)
	{
		spawnPoint = try_pick(false);
	}

	if (!spawnPoint)
		return std::nullopt;

	// Check if the point is usable for alternative spawns if the main spot is blocked.
	const SpawnPointCache& cache = spawn_point_cache[spawnPoint];
	if (cache.has_obstacle)
	{
		// This point is blocked by a monster/defense, but we can try an alternative position nearby.
		// The caller will handle this logic.
	}
	return spawnPoint;
}
// FIX: Time-sliced spawn point cache cleanup to avoid iterating all points every frame
static void CleanupSpawnPointCache() {
	// Only run this operation every 5 frames to reduce per-frame cost
	static int cleanup_frame_counter = 0;
	if (++cleanup_frame_counter < 5) {
		return;
	}
	cleanup_frame_counter = 0;

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
cached_soundindex sound_spawn1;  // Made non-static for horde_boss.cpp and horde_spawning.cpp
static cached_soundindex incoming;
cached_soundindex sound_quake;  // Made non-static for horde_spawning.cpp
static cached_soundindex talk;
cached_soundindex tele1;  // Made non-static for horde_spawning.cpp

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
	"misc/alarm.wav" };

static constexpr const char* START_SOUND_PATHS[NUM_START_SOUNDS] = {
	"misc/r_tele3.wav",
	"world/fish.wav",
	"world/klaxon2.wav",
	"misc/tele_up.wav",
	"world/incoming.wav",
	"world/redforceact.wav",
	"makron/voice2.wav",
	"makron/voice.wav" };

const char* GetCurrentMapName()  // Made non-static for horde_boss.cpp
{
	return static_cast<const char*>(level.mapname);
}

enum class WaveEndReason
{
	AllMonstersDead,
	MonstersRemaining,
	TimeLimitReached,
	FewMonstersRemaining  // Early wave start when 2-3 monsters remain
};

static constexpr gtime_t ZERO_MONSTER_DEPLOYMENT_GRACE = 5_sec;

static bool DeveloperSuppressesWaveAutoAdvance() noexcept
{
	return developer && developer->integer >= 3;
}

inline int8_t GetNumActivePlayers();
inline int8_t GetNumSpectPlayers();

int32_t g_adjusted_monster_cap = 0;

bool allowWaveAdvance = false;		// Global variable to control wave advancement
// Moved to horde_boss.cpp; // to avoid boss spamming

int16_t last_wave_number = 0;		// Reducido de uint64_t
uint16_t g_totalMonstersInWave = 0; // Reducido de uint32_t

gtime_t horde_message_end_time = 0_sec;
gtime_t SPAWN_POINT_COOLDOWN = 2.8_sec; // spawns Cooldown

// FIX: Time-sliced cooldown checking to reduce per-frame overhead on large maps
static void CheckAndReduceSpawnCooldowns()
{
	// Safety check: ensure spawn system is initialized
	if (g_num_spawn_points == 0 ||
		g_spawn_system.spawn_points_data.isTemporarilyDisabled.size() < g_num_spawn_points)
		return;

	if (GetStroggsNum() > 6 || IsBossWave()) {
		return;
	}

	// Only run this operation every 3 frames to reduce per-frame cost
	static int cooldown_frame_counter = 0;
	if (++cooldown_frame_counter < 3) {
		return;
	}
	cooldown_frame_counter = 0;

	bool found_cooldowns_to_reset = false;
	const gtime_t current_time = level.time;
	constexpr float REDUCTION_FACTOR = 0.4f;

	// Iterate using the compact list of actual spawn points
	for (size_t index = 0; index < g_num_spawn_points; ++index)
	{
		if (g_spawn_system.spawn_points_data.isTemporarilyDisabled[index] && current_time < g_spawn_system.spawn_points_data.cooldownEndsAt[index])
		{
			found_cooldowns_to_reset = true;

			const gtime_t remaining_time = g_spawn_system.spawn_points_data.cooldownEndsAt[index] - current_time;
			const gtime_t reduced_duration = remaining_time * REDUCTION_FACTOR;
			const gtime_t final_duration = std::max(reduced_duration, HordeConstants::MIN_REDUCED_INDIVIDUAL_COOLDOWN);

			g_spawn_system.spawn_points_data.cooldownEndsAt[index] = current_time + final_duration;
			g_spawn_system.spawn_points_data.attempts[index] = 0;
		}
	}

	if (found_cooldowns_to_reset)
	{
		SPAWN_POINT_COOLDOWN *= REDUCTION_FACTOR;
		SPAWN_POINT_COOLDOWN = std::max(SPAWN_POINT_COOLDOWN, HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN);
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

static float CalculateCooldownScale(int32_t lvl, const horde::MapSize& mapSize)
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
cvar_t* g_horde;
cvar_t* g_horde_grid_first;  // Test cvar: prioritize grid spawning

// NOTE: horde_state_t enum and HordeState struct moved to g_horde.h

// HordeState member function implementations
void HordeState::update_map_size(const char* mapname)
{
	current_map_size = GetMapSize(mapname);

	// Convert mapname to MapID once for efficient config lookups
	horde::MapID mapId = mapname ? horde::MapOriginRegistry::GetMapID(mapname) : horde::MapID::UNKNOWN;

	max_monsters = GetMonsterCapForMap(mapId, current_map_size);
	base_spawn_cooldown = GetBaseSpawnCooldown(
		current_map_size.isSmallMap,
		current_map_size.isBigMap);

	// Update grid spawning setting based on map config
	bool enable_grid = GetGridEnabledForMap(mapId);
	// Small maps tend to produce poor grid fallback positions; force classic spawn logic.
	if (current_map_size.isSmallMap)
	{
		enable_grid = false;
	}
	gi.cvar_set("g_horde_grid_first", enable_grid ? "1" : "0");

	// Update g_loadent setting based on map config
	bool enable_loadent = GetLoadentEnabledForMap(mapId);
	gi.cvar_forceset("g_loadent", enable_loadent ? "1" : "0");
}

void HordeState::reset_hud_state()
{
	last_successful_hud_update = 0_sec;
	failed_updates_count = 0;
}

// Global instance definition
HordeState g_horde_local;

int16_t current_wave_level = g_horde_local.level;
bool next_wave_message_sent = false;
auto auto_spawned_bosses = boost::container::flat_set<edict_t*>{};  // C++23 - boss tracking

// Function to get the current map size (for use by horde_boss.cpp)
horde::MapSize GetCurrentMapSize()
{
	return g_horde_local.current_map_size;
}

// Award XP to player for killing a monster (used by both Horde and PvM modes)
void AwardKillXP(edict_t* attacker, edict_t* monster)
{
	// Early validation
	if (!attacker || !attacker->client) return;
	if (!g_vortex->integer) return;
	if (attacker->svflags & SVF_BOT) return; // Bots don't earn XP
	if (!monster || !(monster->svflags & SVF_MONSTER)) return;

	// Base XP from monster
	int32_t base_xp = (!IsPvMMode() ? 1 : 2); // Default for standard monsters

	// Scale by monster type/difficulty
	if (monster->monsterinfo.IS_BOSS)
		base_xp = 10;
	else if (monster->monsterinfo.bonus_flags & BF_CHAMPION)
		base_xp = 7;

	// // Scale by current wave level (+2 XP per level)
	// base_xp += current_wave_level * 2;

	// Award XP
	attacker->client->pers.pvm_xp += base_xp;

	// Check for level up
	extern void PvM_CheckLevelUp(edict_t * player);
	PvM_CheckLevelUp(attacker);
}

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

inline int32_t GetAdjustedMonsterCap(const horde::MapSize& mapSize, int32_t waveLevel, const char* mapname = nullptr)
{
	// Get base cap from config (considers map overrides and map size)
	int32_t baseCap = GetMonsterCapForMap(mapname, mapSize);

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

		// if (developer->integer > 1)
		// {
		// 	gi.Com_PrintFmt("GetAdjustedMonsterCap: Wave={}, BaseCap={}, Humans={}, BonusPlayers={}, Bonus={}, FinalCap={}\n",
		// 					waveLevel, baseCap, numHumanPlayers, extraPlayers, playerBonus, finalAdjustedCap);
		// }
	}
	else
	{
		// if (developer->integer > 1)
		// {
		// 	gi.Com_PrintFmt("GetAdjustedMonsterCap: Wave={}, BaseCap={}, Humans={}, No Bonus, FinalCap={}\n",
		// 					waveLevel, baseCap, numHumanPlayers, finalAdjustedCap);
		// }
	}
	// --- End Player Count Bonus ---

	// Ensure the cap doesn't go below a minimum reasonable value (e.g., 6)
	finalAdjustedCap = std::max(6, finalAdjustedCap);

	// Store globally
	g_adjusted_monster_cap = finalAdjustedCap;

	return g_adjusted_monster_cap;
}

// Modify the existing ClampNumToSpawn function
inline static void ClampNumToSpawn(const horde::MapSize& mapSize, const char* mapname = nullptr)
{
	// g_adjusted_monster_cap is now calculated in GetAdjustedMonsterCap and includes player bonus
	const int32_t maxAllowed = g_adjusted_monster_cap;

	// Ensure g_adjusted_monster_cap was initialized (fallback if called too early)
	if (maxAllowed <= 0)
	{
		// Fallback to config-based map defaults if the global cap isn't ready
		const int32_t fallbackCap = GetMonsterCapForMap(mapname, mapSize);
		g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, fallbackCap);
		// if (developer->integer)
		// {
		// 	gi.Com_PrintFmt("ClampNumToSpawn: WARN - g_adjusted_monster_cap not ready, used fallback {}\n", fallbackCap);
		// }
	}
	else
	{
		// Clamp using the globally calculated value
		g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, maxAllowed);
		// if (developer->integer > 1)
		// {
		// 	gi.Com_PrintFmt("ClampNumToSpawn: Clamping num_to_spawn to {} (g_adjusted_monster_cap)\n", maxAllowed);
		// }
	}
}

static int32_t CalculateQueuedMonsters(const horde::MapSize& mapSize, int32_t lvl, bool isHardMode)
{
	if (lvl <= 1) // Start queueing from wave 2 (was wave 4)
		return 0;

	// Reduced base formula for slower progression
	float baseQueued = std::sqrt(static_cast<float>(lvl)) * 2.0f; // Reduced from 2.5f
	baseQueued *= (1.0f + (lvl) * 0.10f); // Reduced scaling from 0.13f

	float mapSizeMultiplier = 1.0f;
	if (mapSize.isSmallMap)
	{
		mapSizeMultiplier = 1.0f; // Reduced from 1.1 - small maps get base queue
	}
	else if (mapSize.isMediumMap)
	{
		mapSizeMultiplier = 1.1f; // Reduced from 1.2
	}
	else if (mapSize.isBigMap)
	{
		if (lvl <= 7)
		{						   // For early waves on big maps
			mapSizeMultiplier = 1.1f; // Reduced from 1.15
		}
		else if (lvl <= 12)
		{
			mapSizeMultiplier = 1.2f; // Reduced from 1.3
		}
		else
		{
			mapSizeMultiplier = 1.4f; // Reduced from 1.5
		}
	}
	baseQueued *= mapSizeMultiplier;

	const int32_t maxQueuedBase = mapSize.isSmallMap ? 15 : (mapSize.isBigMap ? 28 : 20); // Reduced maxes
	// Further reduce max queue for very early waves
	int32_t maxQueued = maxQueuedBase;
	if (lvl <= 7)
	{
		maxQueued = std::max(4, static_cast<int32_t>(maxQueuedBase * 0.4f)); // Reduced from 0.5f, min 4 instead of 5
	}
	else if (lvl <= 12)
	{
		maxQueued = std::max(8, static_cast<int32_t>(maxQueuedBase * 0.65f)); // Reduced from 0.75f
	}

	if (lvl > 20)
	{ // Bonus for high levels
		// FIX: Explicitly cast the result of std::pow (a double) to a float.
		baseQueued *= static_cast<float>(std::pow(1.12f, std::min(lvl - 20, 18))); // Reduced from 1.15f
	}

	if (isHardMode)
	{ // Difficulty adjustment
		float difficultyMultiplier = 1.20f; // Reduced from 1.25f
		if (lvl > 25)
		{
			difficultyMultiplier += (lvl - 25) * 0.02f; // Reduced from 0.025f
			difficultyMultiplier = std::min(difficultyMultiplier, 1.6f); // Reduced from 1.75f
		}
		baseQueued *= difficultyMultiplier;
	}

	baseQueued *= 0.80f; // Final reduction factor (was 0.85f)

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

static void UnifiedAdjustSpawnRate(const horde::MapSize& mapSize, int32_t lvl, int32_t humanPlayers)
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
	ClampNumToSpawn(mapSize, GetCurrentMapName()); // Handle clamping

	// Calculate queued monsters
	const bool isHardMode = g_insane->integer || g_chaotic->integer;
	g_horde_local.queued_monsters = CalculateQueuedMonsters(mapSize, safeLevel, isHardMode);

	//	// Debug output
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

// Named constants for GetConditionParams
namespace {
	constexpr float HIGH_LEVEL_MONSTER_MULT = 1.2f;
	constexpr float HIGH_LEVEL_TIME_BONUS_SEC = 0.20f;
	constexpr float CHAOTIC_MONSTER_MULT = 1.1f;
	constexpr float EARLY_WAVE_TIME_MIN_MULT = 0.75f;
	constexpr float EARLY_WAVE_TIME_REDUCTION = 0.20f;
	constexpr int32_t MAX_MONSTER_LEVEL_BONUS_DIV = 4;
	constexpr int32_t MAX_MONSTER_LEVEL_BONUS_CAP = 8;
	constexpr int32_t TIME_THRESHOLD_LEVEL_DIV = 3;
	constexpr int32_t TIME_THRESHOLD_LEVEL_CAP = 5;
	constexpr int64_t TIME_THRESHOLD_MS_PER_LEVEL = 80ll;
}

// Forward declaration for calculate_max_wave_time if it's not already visible
// static constexpr gtime_t calculate_max_wave_time(int32_t wave_level); // (already provided in previous context)
static ConditionParams GetConditionParams(const horde::MapSize& mapSize, int32_t numHumanPlayers, int32_t lvl)
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
	if (mapSize.isBigMap)
	{
		params.maxMonsters = (numHumanPlayers >= 3) ? 26 : 24;
		params.timeThreshold = random_time(24_sec, 30_sec); // From previous modification
	}
	else if (mapSize.isSmallMap)
	{
		params.maxMonsters = (numHumanPlayers >= 3) ? 12 : 9;
		params.timeThreshold = random_time(22_sec, 28_sec); // From previous modification
	}
	else
	{ // Medium maps
		params.maxMonsters = (numHumanPlayers >= 3) ? 17 : 13;
		params.timeThreshold = random_time(24_sec, 31_sec); // From previous modification
	}

	// 2. Progressive adjustments based on level
	params.maxMonsters += std::min(lvl / MAX_MONSTER_LEVEL_BONUS_DIV, MAX_MONSTER_LEVEL_BONUS_CAP);
	params.timeThreshold += gtime_t::from_ms(TIME_THRESHOLD_MS_PER_LEVEL * std::min(lvl / TIME_THRESHOLD_LEVEL_DIV, TIME_THRESHOLD_LEVEL_CAP));

	// 3. High level adjustments (for levels > 10)
	if (lvl > 10)
	{
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * HIGH_LEVEL_MONSTER_MULT);
		params.timeThreshold += gtime_t::from_sec(HIGH_LEVEL_TIME_BONUS_SEC);
	}

	// 4. Difficulty mode adjustments (chaotic/insane)
	if (g_chaotic->integer || g_insane->integer)
	{
		if (numHumanPlayers <= 3)
		{
			params.timeThreshold += random_time(7_sec, 12_sec); // From previous modification
		}
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * CHAOTIC_MONSTER_MULT);
	}

	// ***** NEW: Early Wave Big Map Fix for maxMonsters *****
	if (lvl <= 5 && mapSize.isBigMap)
	{ // Target very early waves on big maps
		// Estimate initial spawn count for big maps, early levels.
		// This needs to roughly match what UnifiedAdjustSpawnRate would produce for num_to_spawn.
		// Uses BASE_COUNTS for big maps, level <= 5.
		int32_t estimatedInitialSpawn = HordeConstants::BASE_COUNTS[2][0]; // Big Map, Level <= 5
		if (numHumanPlayers > 1)
		{ // Rough approximation of player scaling in UnifiedAdjustSpawnRate
			estimatedInitialSpawn = static_cast<int32_t>(estimatedInitialSpawn * (HordeConstants::BASE_DIFFICULTY_MULTIPLIER + ((std::min(numHumanPlayers, 4) - 1) * HordeConstants::PLAYER_COUNT_SCALE)));
		}
		// Ensure params.maxMonsters is comfortably above this initial spawn count
		// Add a buffer, e.g., 5-8 monsters, or a percentage.
		params.maxMonsters = std::max(params.maxMonsters, estimatedInitialSpawn + 5);
		// if (developer->integer)
		// {
		// 	gi.Com_PrintFmt("GetConditionParams: BigMap Early Wave (lvl {}) Fix: maxMonsters adjusted to {} (estimated initial: {})\n", lvl, params.maxMonsters, estimatedInitialSpawn);
		// }
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

	// 8. Apply early wave time multiplier to conditional timers (levels 1-10)
	if (lvl >= 1 && lvl <= 10)
	{
		float timeMultiplier = 1.0f - (EARLY_WAVE_TIME_REDUCTION * (static_cast<float>(lvl - 1) / 9.0f));
		timeMultiplier = std::max(EARLY_WAVE_TIME_MIN_MULT, timeMultiplier);

		params.timeThreshold = gtime_t::from_sec(params.timeThreshold.seconds() * timeMultiplier);
		params.lowPercentageTimeThreshold = gtime_t::from_sec(params.lowPercentageTimeThreshold.seconds() * timeMultiplier);
		// The `independentTimeThreshold` is generally not affected by this multiplier as it's an overall cap.
		// The `lowPercentageThreshold` (the float value itself) is also not multiplied; the timer it triggers is.
	}

	// 9. Aggressive time reduction threshold (when very few monsters are left, this is the % of total wave)
	// This seems to be a threshold for when the *aggressive time reduction itself* can be considered,
	// not the direct percentage of monsters that triggers the aggressive timer (that's MONSTERS_FOR_AGGRESSIVE_REDUCTION).
	// Kept original value as its interaction is distinct.
	params.aggressiveTimeReductionThreshold = 0.3f;

	// 10. Configure independent (absolute maximum) wave time
	params.independentTimeThreshold = calculate_max_wave_time(lvl); // Uses the separately improved function

	// 11. Final parameter validation (ensure reasonable minimums)
	params.maxMonsters = std::max(1, params.maxMonsters);									// At least 1 monster
	params.timeThreshold = std::max(5_sec, params.timeThreshold);							// Minimum 5s for conditional timer
	params.lowPercentageTimeThreshold = std::max(5_sec, params.lowPercentageTimeThreshold); // Minimum 5s for low % timer
	params.independentTimeThreshold = std::max(10_sec, params.independentTimeThreshold);	// Minimum 10s for overall wave

	return params;
}

inline int32_t GetAdjustedMonsterCap(const horde::MapSize& mapSize, int32_t waveLevel, const char* mapname);

static void ResetChampionMonsterState()
{
	elites_spawned_this_wave = 0;  // Reset counter for new wave
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
	return coop->integer && !g_horde->integer && !pvm->integer;
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
	{IT_ARMOR_JACKET, 0.8f, 2},		   // Basic armor
	{IT_ARMOR_COMBAT, 0.5f, 8},		   // Better armor
	{IT_ARMOR_BODY, 0.2f, 15},		   // Best standard armor
	{IT_ITEM_POWER_SCREEN, 0.3f, 7},   // Power Armor (early)
	{IT_ITEM_POWER_SHIELD, 0.01f, 19}, // Power Armor (late)

	// --- Weapons ---
	// Blaster is skipped (always have)
	{IT_WEAPON_CHAINFIST, 0.1f, 1},	   // Melee, niche drop
	{IT_WEAPON_SHOTGUN, 1.0f, 1},	   // Basic weapon
	{IT_WEAPON_SSHOTGUN, 0.7f, 3},	   // Strong early/mid weapon
	{IT_WEAPON_20MM, 1.0f, 6},			// 20MM (vrx)
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
	{IT_ITEM_SENTRYGUN, 0.23f, 5},		// Deployable Defense
	{IT_ITEM_STROGGSUMM, 0.16f, 5},		// Strogg Summoner (Debug High Chance)

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

int Horde_GetItemMinWave(int32_t item_id) noexcept
{
	int min_wave = 0;

	for (const auto& item : hordeItemData)
	{
		if (static_cast<int32_t>(item.id) != item_id)
			continue;

		if (min_wave == 0 || item.minWave < min_wave)
			min_wave = item.minWave;
	}

	return min_wave;
}

// First, add these at the top with other global variables
static constexpr size_t WAVE_MEMORY_SIZE = 3; // Remember last 3 waves
static std::array<MonsterWaveType, WAVE_MEMORY_SIZE> previous_wave_types = {};
static size_t wave_memory_index = 0;

// Track the last Gekk/Berserk special wave to enforce alternation
static MonsterWaveType last_gekk_or_berserk_special = MonsterWaveType::None;

// Helper function to check if a wave type was recently used
static bool WasRecentlyUsed(MonsterWaveType wave_type)
{
	// Define the major "themes" that should not repeat in consecutive waves.
	// This is the core of the fix.
	static constexpr std::array<MonsterWaveType, 7> MAJOR_THEMES = { {MonsterWaveType::Flying,
																	 MonsterWaveType::Gekk,
																	 MonsterWaveType::Mutant,
																	 MonsterWaveType::Berserk,
																	 MonsterWaveType::Spawner,
																	 MonsterWaveType::Shambler,
																	 MonsterWaveType::Arachnophobic} };

	// Iterate through the history of the last few waves.
	for (const auto& prev_type : previous_wave_types)
	{
		// If the historical slot is empty, skip it.
		if (prev_type == MonsterWaveType::None)
		{
			continue;
		}

		// --- 1. Theme Repetition Check ---
		// Check if the new wave and a previous wave share a major theme.
		for (const auto& theme : MAJOR_THEMES)
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
void StoreWaveType(MonsterWaveType wave_type)  // Made non-static for horde_boss.cpp
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
	const char* message;
};

// --- Step 2: Define Source Data in Human-Readable Format (AoS) ---

// Corrected WAVE_DEFINITIONS_SRC array
constexpr std::array<WaveDefinition, 8> WAVE_DEFINITIONS_SRC = { {
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
	} };

// Source data for special waves (chance is calculated at runtime based on player count)
static constexpr std::array<SpecialWave, 7> SPECIAL_WAVES_SRC = { {{MonsterWaveType::Gekk, 0.75f, 15, -1, "*** Gekk Invasion! ***\n"},
																  {MonsterWaveType::Mutant | MonsterWaveType::Melee, 0.30f, 8, -1, "*** Enraged Horde approaching! ***\n"},
																  {MonsterWaveType::Flying | MonsterWaveType::Fast, 0.2f, 9, -1, "*** Aerial assault incoming! ***\n"},
																  {MonsterWaveType::Berserk, 0.75f, 15, -1, "*** Berserkers! ***\n"},
																  {MonsterWaveType::Bomber, 0.35f, 10, -1, "*** Strogg Bomber Units Arrived! ***\n"},
																  {MonsterWaveType::Heavy, 0.2f, 12, -1, "*** Heavy Armored Units incoming! ***\n"},
																  {MonsterWaveType::Spawner | MonsterWaveType::Bomber, 0.3f, 25, -1, "*** Spawners & Bombers Deployed! ***\n"}} };;

// --- Step 3: Define  Data Structures (SoA) ---

struct WaveDefinitionsSoA
{
	static constexpr size_t DEF_COUNT = WAVE_DEFINITIONS_SRC.size();
	static constexpr size_t OPTIONALS_PER_DEF = 4;

	std::array<int, DEF_COUNT> max_waves;
	std::array<MonsterWaveType, DEF_COUNT> base_types;
	std::array<MonsterWaveType, DEF_COUNT* OPTIONALS_PER_DEF> optional_types;
	std::array<float, DEF_COUNT* OPTIONALS_PER_DEF> optional_chances;
};

struct SpecialWavesSoA
{
	static constexpr size_t WAVE_COUNT = SPECIAL_WAVES_SRC.size();
	std::array<MonsterWaveType, WAVE_COUNT> types;
	std::array<float, WAVE_COUNT> base_chances;
	std::array<int, WAVE_COUNT> min_waves;
	std::array<int, WAVE_COUNT> max_waves;
	std::array<const char*, WAVE_COUNT> messages;
};

// --- Step 4: Define Compile-Time Transformation Functions ---

static constexpr WaveDefinitionsSoA create_wave_definitions_soa()
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

static constexpr SpecialWavesSoA create_special_waves_soa()
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

static inline MonsterWaveType GetWaveComposition(int waveNumber, bool forceSpecialWave = false)
{
	// --- Part 1: Check for Special Waves ---
	if (!forceSpecialWave && !WasLastWaveSpecial())
	{
		for (size_t i = 0; i < g_specialWaves.WAVE_COUNT; ++i)
		{
			// Fast checks on contiguous SoA data
			if (waveNumber >= g_specialWaves.min_waves[i] &&
				(g_specialWaves.max_waves[i] == -1 || waveNumber <= g_specialWaves.max_waves[i]))
			{
				// Slower checks only if level is valid
				const MonsterWaveType type = g_specialWaves.types[i];

				// Skip Gekk/Berserk special waves on boss waves (every 5th wave)
				bool is_boss_wave = (waveNumber >= 10 && waveNumber % 5 == 0);
				bool is_gekk_or_berserk = HasWaveType(type, MonsterWaveType::Gekk) ||
					HasWaveType(type, MonsterWaveType::Berserk);
				if (is_boss_wave && is_gekk_or_berserk)
				{
					continue; // Skip this special wave type
				}

				// Enforce alternation between Gekk and Berserk special waves
				if (is_gekk_or_berserk && last_gekk_or_berserk_special != MonsterWaveType::None)
				{
					// Don't allow the same type to repeat
					if ((HasWaveType(type, MonsterWaveType::Gekk) && HasWaveType(last_gekk_or_berserk_special, MonsterWaveType::Gekk)) ||
						(HasWaveType(type, MonsterWaveType::Berserk) && HasWaveType(last_gekk_or_berserk_special, MonsterWaveType::Berserk)))
					{
						continue; // Skip - must alternate
					}
				}

				if (!WasRecentlyUsed(type))
				{
					float chance = g_specialWaves.base_chances[i];

					// Use base chance from special wave config (45% for Gekk/Berserk)
					// No player count override - we want these waves to be frequent

					if (frandom() < chance)
					{
						gi.LocBroadcast_Print(PRINT_HIGH, g_specialWaves.messages[i]);
						StoreWaveType(type);

						// Track Gekk/Berserk special waves for alternation
						if (is_gekk_or_berserk)
						{
							last_gekk_or_berserk_special = type;
						}

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

		// if (developer->integer)
		// {
		// 	gi.Com_PrintFmt("GetWaveComposition: Detected repeating Flying wave. Forcing ground-only.\n");
		// }
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

	// Apply fog effect and special effects for Gekk/Berserk special waves (wave 15+)
	if (waveLevel >= 15 && (HasWaveType(current_wave_type, MonsterWaveType::Gekk) ||
		HasWaveType(current_wave_type, MonsterWaveType::Berserk)))
	{
		ApplyFogEffect();
		ApplySpecialWaveEffects(current_wave_type);
	}
}

//======================================================================
// SECTION: Item Selection System
//======================================================================

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

		// Apply sigmoid scaling to item drop weights based on item tier
		if (g_horde && g_horde->integer && current_wave_level > 0) {
			// Determine item tier based on minimum wave requirement
			int item_tier = 0; // Default to common
			if (g_hordeItemDataSoA.minWaves[item_index] >= 15) {
				item_tier = 2; // Legendary tier (late game items)
			}
			else if (g_hordeItemDataSoA.minWaves[item_index] >= 7) {
				item_tier = 1; // Rare tier (mid game items)
			}

			adjusted_weight = ScaleItemDropWeight(adjusted_weight, current_wave_level, item_tier);
		}

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

//======================================================================
// SECTION: Monster Selection System
//======================================================================

//-----------------------------------------------------
// Helper structures for monster selection
//-----------------------------------------------------
struct MonsterSelectionContext
{
	int32_t currentActualLevel = 0;			   // The true current wave level
	int32_t effectiveLevel = 0;				   // Potentially higher level for selection
	MonsterWaveType waveTypeForFiltering = MonsterWaveType::None;  // The (potentially simplified) wave type for general filtering
	MonsterWaveType currentActualWaveType = MonsterWaveType::None; // The true current wave type for specific checks
	bool isSpawnPointFlying = false;
	bool isRetaliationActive = false;
	bool isRecoveryModeActive = false;
	float flyingAdjustmentFactor = 1.0f;
	bool isBossWaveMinionPhase = false;
};

struct MonsterCache
{
	struct Entry
	{
		horde::MonsterTypeID typeId;
		float weight;
		float cumulative_weight;
	};

	static constexpr size_t MONSTER_CACHE_SIZE = MONSTER_DATA_COUNT;
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
		auto& entry = entries[count];
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
	// Restore 0.995-style pacing: only attempt one higher-tier spawn path per wave,
	// and never during retaliation/recovery batches.
	if (g_special_high_level_monster_spawned_this_wave ||
		elites_spawned_this_wave >= MAX_ELITES_PER_WAVE ||
		isRetaliationActive || isRecoveryModeActive)
	{
		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("ShouldAttemptHigherLevelSpawn: BLOCKED (elites={}/{}, retaliation={}, recovery={})\n",
				elites_spawned_this_wave, MAX_ELITES_PER_WAVE, isRetaliationActive, isRecoveryModeActive);
		}
		return false;
	}

	// Restore the old odds from 0.995 instead of forcing higher-level picks in early waves.
	if (currentLevel <= 10)
	{
		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("ShouldAttemptHigherLevelSpawn: wave {} using 0.995 early-wave odds (32%%)\n", currentLevel);
		}
		return frandom() < 0.32f;
	}
	if (currentLevel <= 20)
		return frandom() < 0.19f;
	return frandom() < 0.07f;
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
	auto it = std::lower_bound(monsterTypes, monsterTypes + MONSTER_DATA_COUNT, currentActualLevel,
		[](const MonsterTypeInfo& monster, int32_t level) {
			return monster.minWave <= level;
		});

	bool any_new_monsters_unlocked = false;

	for (; it != monsterTypes + MONSTER_DATA_COUNT && it->minWave <= potentialEffectiveLevel; ++it)
	{
		if (IsValidMonsterForWave(it->typeId, waveTypeForFiltering))
		{
			any_new_monsters_unlocked = true;
			break;
		}
	}

	if (!any_new_monsters_unlocked)
	{
		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("CalculateEffectiveMonsterLevel: No valid 0.995-style higher-tier candidate for wave {} (effective {}). Reverting.\n",
				currentActualLevel, potentialEffectiveLevel);
		}
		return currentActualLevel;
	}

	if (developer->integer > 1)
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
static float CalculateBaseWeight(size_t i, const MonsterSelectionContext& ctx)
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
static float ApplySpecialModifiers(float weight, size_t i, const MonsterSelectionContext& ctx)
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

// REMOVED: IsMonsterCompatible - Functionality now inlined in BuildMonsterCache
// The checks are now performed directly during cache building for better performance

// --- Spawn History Helper Functions ---
// Counts how many times a family appears in recent spawn history
int32_t GetFamilySpawnCountInHistory(AssetFamilyID family_id)
{
	if (family_id == AssetFamilyID::UNKNOWN_FAMILY)
		return 0;

	int32_t count = 0;
	for (const auto& entry : g_spawn_history)
	{
		if (entry.family_id == family_id)
			count++;
	}
	return count;
}

// Records a spawn in the history ring buffer
void RecordSpawnInHistory(AssetFamilyID family_id, int32_t wave_number)
{
	if (family_id == AssetFamilyID::UNKNOWN_FAMILY)
		return;

	g_spawn_history[g_spawn_history_index] = { family_id, wave_number, level.time };
	g_spawn_history_index = (g_spawn_history_index + 1) % SPAWN_HISTORY_SIZE;
}

// Helper to get the asset family for a monster type
static AssetFamilyID GetMonsterAssetFamily(horde::MonsterTypeID typeId)
{
	const size_t idx = static_cast<size_t>(typeId);
	if (idx < g_monster_to_family.size())
		return g_monster_to_family[idx];
	return AssetFamilyID::UNKNOWN_FAMILY;
}

static void MarkMonsterTypePrecached(horde::MonsterTypeID typeId, const char* model_path = nullptr)
{
	const size_t idx = static_cast<size_t>(typeId);
	if (idx >= g_precached_monster_types_flags.size())
		return;

	g_precached_monster_types_flags[idx] = true;
	g_precached_monsters_this_map.insert(typeId);
	MarkFamilyPrecached(GetMonsterAssetFamily(typeId));

	if (!model_path)
		model_path = GetMonsterModelPath(typeId);

	if (model_path && *model_path)
		g_precached_models_this_map.insert(model_path);
}

// ============================================================================
// BuildMonsterCache Helper Structures and Functions
// ============================================================================

// Candidate for elite monster selection
struct EliteCandidate {
	horde::MonsterTypeID typeId;
	size_t array_index;
	float priority;
};

// Result of elite candidate search
struct EliteCandidateResult {
	horde::MonsterTypeID selected_type = horde::MonsterTypeID::UNKNOWN;
	size_t selected_array_index = 0;
	bool found = false;
};

// Check if a monster is a valid elite candidate
static bool IsValidEliteCandidate(
	const MonsterTypeInfo& monster_info,
	size_t array_index,
	const MonsterSelectionContext& ctx,
	int32_t min_wave_buffer)
{
	const char* classname = horde::MonsterTypeRegistry::GetClassname(monster_info.typeId);
	if (!classname || !*classname) return false;

	// Check wave range
	if (monster_info.minWave < ctx.currentActualLevel + min_wave_buffer) return false;
	if (monster_info.minWave > ctx.effectiveLevel) return false;
	if (monster_info.minWave >= 999) return false; // Skip bosses

	// Exclude infantry_vanilla from elite spawns
	if (monster_info.typeId == horde::MonsterTypeID::INFANTRY_VANILLA) return false;

	// Check model path
	const char* model_path = GetMonsterModelPath(monster_info.typeId);
	if (!model_path) return false;

	// For waves 11+: Require NEW families only (prevents memory bloat)
	if (ctx.currentActualLevel > 10) {
		const bool family_already_loaded = g_precached_models_this_map.find(model_path) != g_precached_models_this_map.end();
		if (family_already_loaded) return false;
	}

	return true;
}

// Collect elite candidates with a specific wave buffer
static void CollectEliteCandidates(
	boost::container::small_vector<EliteCandidate, 16>& candidates,
	const MonsterSelectionContext& ctx,
	int32_t min_wave_buffer)
{
	for (size_t i = 0; i < MONSTER_DATA_COUNT; ++i)
	{
		const auto& monster_info = monsterTypes[i];
		if (!IsValidEliteCandidate(monster_info, i, ctx, min_wave_buffer))
			continue;

		float priority = g_monsterData.weights[i] / (1.0f + abs(monster_info.minWave - ctx.effectiveLevel));
		candidates.push_back({ monster_info.typeId, i, priority });

		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("Dynamic Precache: CANDIDATE '{}' (minWave={}, buffer=+{}, priority={:.2f})\n",
				horde::MonsterTypeRegistry::GetClassname(monster_info.typeId),
				monster_info.minWave, min_wave_buffer, priority);
		}
	}
}

// Select a candidate using weighted random selection
static EliteCandidateResult SelectWeightedCandidate(
	const boost::container::small_vector<EliteCandidate, 16>& candidates)
{
	EliteCandidateResult result;
	if (candidates.empty()) return result;

	float total_priority = 0.0f;
	for (const auto& cand : candidates) {
		total_priority += cand.priority;
	}

	float random_value = frandom() * total_priority;
	float cumulative = 0.0f;

	for (const auto& cand : candidates)
	{
		cumulative += cand.priority;
		if (random_value <= cumulative)
		{
			result.selected_type = cand.typeId;
			result.selected_array_index = cand.array_index;
			result.found = true;
			break;
		}
	}

	return result;
}

// Find an elite monster candidate with fallback to smaller buffers
static EliteCandidateResult FindEliteCandidate(const MonsterSelectionContext& ctx)
{
	// Dynamic buffer based on wave: early waves use +2, later waves use +3
	const int32_t initial_buffer = (ctx.currentActualLevel < 7) ? 2 : 3;

	// Try with initial buffer first
	boost::container::small_vector<EliteCandidate, 16> candidates;
	CollectEliteCandidates(candidates, ctx, initial_buffer);

	EliteCandidateResult result = SelectWeightedCandidate(candidates);
	if (result.found)
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("Dynamic Precache: SELECTED from {} candidates (buffer=+{})\n", candidates.size(), initial_buffer);
		return result;
	}

	// Fallback: try with smaller buffers (+2, +1)
	for (int32_t fallback_buffer = std::min(2, initial_buffer - 1); fallback_buffer >= 1; --fallback_buffer)
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("Dynamic Precache: FALLBACK attempt with +{} buffer\n", fallback_buffer);

		candidates.clear();
		CollectEliteCandidates(candidates, ctx, fallback_buffer);

		result = SelectWeightedCandidate(candidates);
		if (result.found)
		{
			if (developer->integer > 1)
				gi.Com_PrintFmt("Dynamic Precache: FALLBACK SELECTED from {} candidates (buffer=+{})\n", candidates.size(), fallback_buffer);
			return result;
		}
	}

	return result; // Not found
}

// Precache the selected elite monster and add family members to eligible list
// Respects family limit - returns nullptr if the monster's family isn't allowed
static AssetFamilyID PrecacheEliteMonster(
	horde::MonsterTypeID type_id,
	size_t array_index,
	const MonsterSelectionContext& ctx)
{
	const char* classname = horde::MonsterTypeRegistry::GetClassname(type_id);
	const char* model_path = GetMonsterModelPath(type_id);
	if (!classname || !*classname || !model_path) return AssetFamilyID::UNKNOWN_FAMILY;

	// Check family limit - skip if family not allowed for this map
	AssetFamilyID family = GetMonsterAssetFamily(type_id);
	if (g_precached_families_this_map.find(family) == g_precached_families_this_map.end())
	{
		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("Dynamic Precache: BLOCKED '{}' - family {} not allowed on this map\n",
				classname, static_cast<int>(family));
		}
		return AssetFamilyID::UNKNOWN_FAMILY; // Family not allowed, don't precache
	}

	const bool already_precached = g_precached_monster_types_flags[array_index];

	if (developer->integer > 1)
	{
		if (already_precached)
			gi.Com_PrintFmt("Dynamic Precache: REUSING '{}' for wave {} elite\n", classname, ctx.currentActualLevel);
		else
			gi.Com_PrintFmt("Dynamic Precache: Loading '{}' for elite spawn (wave {} -> effective {})\n",
				classname, ctx.currentActualLevel, ctx.effectiveLevel);
	}

	// Precache if needed
	if (!already_precached)
	{
		edict_t* temp_monster = G_Spawn();
		if (temp_monster)
		{
			temp_monster->classname = classname;
			temp_monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
			ED_CallSpawnMonsterByID(temp_monster, type_id);
			if (temp_monster->inuse)
				G_FreeEdict(temp_monster);

			MarkMonsterTypePrecached(type_id, model_path);

			UnlockModelFamilyMembers(type_id, ctx.currentActualLevel);
		}
	}

	// Add elite family members to eligible list
	for (size_t j = 0; j < MONSTER_DATA_COUNT; ++j)
	{
		const auto& family_monster = monsterTypes[j];
		if (!g_precached_monster_types_flags[j]) continue;

		const AssetFamilyID family_monster_family = GetMonsterAssetFamily(family_monster.typeId);
		if (family_monster_family != family) continue;

		// Only add family members that are "elite" level (3+ waves higher)
		if (family_monster.minWave < ctx.currentActualLevel + 3) continue;

		// Check if already in eligible list
		bool already_eligible = false;
		for (const MonsterTypeInfo* existing : g_eligible_monsters_for_wave)
		{
			if (existing->typeId == family_monster.typeId) {
				already_eligible = true;
				break;
			}
		}

		if (!already_eligible)
		{
			g_eligible_monsters_for_wave.push_back(&family_monster);
			if (developer->integer > 1)
				gi.Com_PrintFmt("Dynamic Precache: Added '{}' to eligible monsters (minWave {} is elite)\n",
					horde::MonsterTypeRegistry::GetClassname(family_monster.typeId), family_monster.minWave);
		}
	}

	g_dynamic_precache_count_this_wave++;
	return family;
}

// Apply weight modifiers to a monster based on context
static float ApplyMonsterWeightModifiers(
	float base_weight,
	const MonsterTypeInfo* monster_info,
	size_t array_index,
	const MonsterSelectionContext& ctx,
	bool is_elite_family_member)
{
	float weight = base_weight;

	// Apply spawn history penalty
	AssetFamilyID monster_family = GetMonsterAssetFamily(monster_info->typeId);
	if (monster_family != AssetFamilyID::UNKNOWN_FAMILY)
	{
		int32_t recent_spawn_count = GetFamilySpawnCountInHistory(monster_family);
		if (recent_spawn_count >= 2)
			weight *= 0.3f;
	}

	// Soldier weight reduction for waves 5+
	if (ctx.currentActualLevel >= 5 && monster_family == AssetFamilyID::SOLDIER_FAMILY)
		weight *= 0.6f;

	// Infantry_vanilla progressive weight reduction for waves 11+
	if (monster_info->typeId == horde::MonsterTypeID::INFANTRY_VANILLA && ctx.currentActualLevel >= 11)
	{
		constexpr float MIN_WEIGHT_MULTIPLIER = 0.294f;
		constexpr float REDUCTION_PER_WAVE = 0.0588f;
		float waves_since_unlock = static_cast<float>(ctx.currentActualLevel - 11);
		float multiplier = std::max(MIN_WEIGHT_MULTIPLIER, 1.0f - (waves_since_unlock * REDUCTION_PER_WAVE));
		weight *= multiplier;
	}

	// Elite family boost
	if (is_elite_family_member)
		weight *= 2.0f;

	return weight;
}

// Add monsters with relaxed wave type filter (used in low variety fallback)
static void AddMonstersWithRelaxedFilter(
	MonsterCache& cache_ref,
	const MonsterSelectionContext& ctx,
	MonsterWaveType relaxed_wave_type,
	float weight_multiplier,
	int target_count)
{
	for (const MonsterTypeInfo* monster_info : g_eligible_monsters_for_wave)
	{
		const size_t i = static_cast<size_t>(monster_info->typeId);

		if (!g_precached_monster_types_flags[i]) continue;
		if (g_monsterData.minWaves[i] > ctx.effectiveLevel) continue;

		const bool monster_is_flying = IsFlying(monster_info->typeId);
		if (ctx.isSpawnPointFlying && !monster_is_flying) continue;

		MonsterWaveType monster_types = g_monsterData.waveTypes[i];
		if ((monster_types & relaxed_wave_type) == MonsterWaveType::None) continue;

		float weight = CalculateBaseWeight(i, ctx) * weight_multiplier;
		cache_ref.addMonster(monster_info->typeId, weight);

		if (cache_ref.count >= target_count) break;
	}
}

// Handle low variety fallback when cache has too few monsters
static void HandleLowVarietyFallback(
	MonsterCache& cache_ref,
	const MonsterSelectionContext& ctx)
{
	constexpr int MINIMUM_VARIETY_THRESHOLD = 5;
	if (cache_ref.count >= MINIMUM_VARIETY_THRESHOLD) return;

	if (developer->integer > 1)
		gi.Com_PrintFmt("BuildMonsterCache: LOW VARIETY ({} monsters) - Applying relaxed filters\n", cache_ref.count);

	MonsterWaveType relaxed_wave_type = ctx.waveTypeForFiltering;

	// PASS 1: Remove "Special" requirement
	if (HasWaveType(relaxed_wave_type, MonsterWaveType::Special))
	{
		relaxed_wave_type = relaxed_wave_type & ~MonsterWaveType::Special;
		if (developer->integer > 1)
			gi.Com_PrintFmt("BuildMonsterCache: PASS 1 - Removed 'Special' requirement\n");
		AddMonstersWithRelaxedFilter(cache_ref, ctx, relaxed_wave_type, 0.4f, MIN_MONSTERS_AVAILABLE);
	}

	// PASS 2: Relax Light/Medium/Heavy categories
	if (cache_ref.count < MINIMUM_VARIETY_THRESHOLD)
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("BuildMonsterCache: PASS 2 - Relaxing Light/Medium/Heavy categories\n");

		if (HasWaveType(relaxed_wave_type, MonsterWaveType::Light))
			relaxed_wave_type |= MonsterWaveType::Medium;
		if (HasWaveType(relaxed_wave_type, MonsterWaveType::Medium))
			relaxed_wave_type |= MonsterWaveType::Light | MonsterWaveType::Heavy;
		if (HasWaveType(relaxed_wave_type, MonsterWaveType::Heavy))
			relaxed_wave_type |= MonsterWaveType::Medium;

		AddMonstersWithRelaxedFilter(cache_ref, ctx, relaxed_wave_type, 0.3f, MIN_MONSTERS_AVAILABLE);
	}

	// PASS 3: Add any early-wave monsters
	if (cache_ref.count < MINIMUM_VARIETY_THRESHOLD)
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("BuildMonsterCache: PASS 3 - Adding any early-wave monsters\n");

		for (const MonsterTypeInfo* monster_info : g_eligible_monsters_for_wave)
		{
			const size_t i = static_cast<size_t>(monster_info->typeId);
			if (!g_precached_monster_types_flags[i]) continue;
			if (monster_info->minWave > 5) continue;

			const bool monster_is_flying = IsFlying(monster_info->typeId);
			if (ctx.isSpawnPointFlying && !monster_is_flying) continue;

			float weight = CalculateBaseWeight(i, ctx) * 0.2f;
			cache_ref.addMonster(monster_info->typeId, weight);

			if (cache_ref.count >= MIN_MONSTERS_AVAILABLE) break;
		}
	}
}

// ============================================================================
// Main BuildMonsterCache Function (Refactored)
// ============================================================================

static void BuildMonsterCache(MonsterCache& cache_ref, const MonsterSelectionContext& ctx)
{
	cache_ref.clear();

	// Track if we dynamically precached a monster for elite spawns
	AssetFamilyID elite_spawn_family = AssetFamilyID::UNKNOWN_FAMILY;
	const char* elite_spawn_model_path = nullptr;

	// Create a mutable copy of context to potentially revert effectiveLevel
	MonsterSelectionContext mutable_ctx = ctx;

	// --- DYNAMIC ON-DEMAND PRECACHING ---
	// When higher-level spawns are triggered, precache ONE new monster family
	if (mutable_ctx.effectiveLevel > mutable_ctx.currentActualLevel)
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("BuildMonsterCache: Elite attempt (effectiveLevel={} > currentActualLevel={})\n",
				mutable_ctx.effectiveLevel, mutable_ctx.currentActualLevel);

		// Limit: only 1 new model family per wave
		if (g_dynamic_precache_count_this_wave < 1)
		{
			EliteCandidateResult candidate = FindEliteCandidate(mutable_ctx);

			if (candidate.found)
			{
				if (developer->integer > 1)
					gi.Com_PrintFmt("Dynamic Precache: FINAL SELECTION '{}'\n",
						horde::MonsterTypeRegistry::GetClassname(candidate.selected_type));

				const char* candidate_model_path = GetMonsterModelPath(candidate.selected_type);
				elite_spawn_family = PrecacheEliteMonster(
					candidate.selected_type, candidate.selected_array_index, mutable_ctx);
				if (elite_spawn_family != AssetFamilyID::UNKNOWN_FAMILY && candidate_model_path)
					elite_spawn_model_path = candidate_model_path;
			}
			else
			{
				// No suitable elite monster found - revert to normal spawning
				mutable_ctx.effectiveLevel = mutable_ctx.currentActualLevel;
				if (developer->integer > 1)
					gi.Com_PrintFmt("Dynamic Precache: No suitable elite found, reverting to normal spawning\n");
			}
		}
		else if (developer->integer > 1)
		{
			gi.Com_PrintFmt("BuildMonsterCache: Skipping dynamic precache (already precached elite this wave)\n");
		}
	}

	// Get PVM random monsters if active
	const horde::MonsterTypeID* pvm_monsters = PVM_GetRandomMonsters();
	const int pvm_monster_count = PVM_GetRandomMonsterCount();

	// --- BUILD MAIN CACHE ---
	for (const MonsterTypeInfo* monster_info : g_eligible_monsters_for_wave)
	{
		const size_t i = static_cast<size_t>(monster_info->typeId);

		// Only use monsters that are actually precached
		if (!g_precached_monster_types_flags[i])
			continue;

		// Check if this is an elite family member
		bool is_elite_family_member = false;
		if (elite_spawn_family != AssetFamilyID::UNKNOWN_FAMILY) {
			const AssetFamilyID this_monster_family = GetMonsterAssetFamily(monster_info->typeId);
			if (this_monster_family == elite_spawn_family)
				is_elite_family_member = true;
		}

		// PvM Mode: Use random monster list instead of minWave filtering
		if (pvm_monsters) {
			bool in_random_list = false;
			for (int j = 0; j < pvm_monster_count; ++j) {
				if (pvm_monsters[j] == monster_info->typeId) {
					in_random_list = true;
					break;
				}
			}
			if (!in_random_list)
				continue;
		}
		else {
			// Normal horde mode: check minWave (skip in elite spawn mode)
			if (elite_spawn_family == AssetFamilyID::UNKNOWN_FAMILY && g_monsterData.minWaves[i] > mutable_ctx.effectiveLevel)
				continue;
		}

		// Flying check
		const bool monster_is_flying = IsFlying(monster_info->typeId);
		if (mutable_ctx.isSpawnPointFlying && !monster_is_flying)
			continue;

		// Calculate weight
		float weight = CalculateBaseWeight(i, mutable_ctx);
		weight = ApplySpecialModifiers(weight, i, mutable_ctx);
		weight = ApplyMonsterWeightModifiers(weight, monster_info, i, mutable_ctx, is_elite_family_member);

		cache_ref.addMonster(monster_info->typeId, weight);
	}

	// Handle low variety fallback
	HandleLowVarietyFallback(cache_ref, mutable_ctx);

	// Debug output
	if (developer->integer > 1)
	{
		gi.Com_PrintFmt("BuildMonsterCache: Final cache has {} monsters, total_weight={:.1f}\n",
			cache_ref.count, cache_ref.total_weight);
		if (elite_spawn_family != AssetFamilyID::UNKNOWN_FAMILY)
			gi.Com_PrintFmt("BuildMonsterCache: Elite spawn boost active - family {} ({}) gets 2x weight\n",
				static_cast<int>(elite_spawn_family),
				elite_spawn_model_path ? elite_spawn_model_path : "unknown");
	}
}


static horde::MonsterTypeID SelectFromCache(const MonsterCache& cache_ref)
{
	if (cache_ref.count == 0 || cache_ref.total_weight <= 0.0f)
		return horde::MonsterTypeID::UNKNOWN;

	const float random_value = frandom() * cache_ref.total_weight;

	// Use std::lower_bound to find the first entry whose cumulative_weight is not less than random_value.
	auto it = std::lower_bound(
		cache_ref.entries.begin(),
		cache_ref.entries.begin() + cache_ref.count, // Search only the valid range
		random_value,
		[](const MonsterCache::Entry& entry, float value)
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

static horde::MonsterTypeID EmergencyFallbackSelection(const MonsterSelectionContext& ctx)
{
	// Debug-only: This is expected for flying spawn points or restrictive wave types
	if (developer->integer >= 2)
		gi.Com_PrintFmt("DEBUG: Monster picker emergency fallback (Lvl: {}, FlyPoint: {}, WaveType: {})\n",
			ctx.currentActualLevel, ctx.isSpawnPointFlying, static_cast<int>(ctx.waveTypeForFiltering));

	// Try to respect wave type requirements first
	for (size_t i = 0; i < MONSTER_DATA_COUNT; ++i)
	{
		const auto& monster = monsterTypes[i];
		if (monster.minWave <= ctx.currentActualLevel)
		{
			// Respect wave type requirements (important for Gekk/Berserk special waves)
			if (!IsValidMonsterForWave(monster.typeId, ctx.waveTypeForFiltering))
				continue;

			const bool isFlyingMonster = HasWaveType(GetMonsterWaveTypes(monster.typeId), MonsterWaveType::Flying);
			if (!(ctx.isSpawnPointFlying && !isFlyingMonster))
				return monster.typeId;
		}
	}

	// If no valid monster found that matches wave type, fall back to ANY valid monster
	// (this prevents complete spawn failure)
	if (developer->integer)
	{
		// Rate-limit warning to prevent spam (max once per 10 seconds)
		static gtime_t last_warning_time = 0_sec;
		if (level.time - last_warning_time >= 10_sec)
		{
			// Simplified wave type description for logging
			const char* wave_type_str = "Unknown";
			if (HasWaveType(ctx.waveTypeForFiltering, MonsterWaveType::Ground))
				wave_type_str = "Ground";
			else if (HasWaveType(ctx.waveTypeForFiltering, MonsterWaveType::Flying))
				wave_type_str = "Flying";
			else if (HasWaveType(ctx.waveTypeForFiltering, MonsterWaveType::Boss))
				wave_type_str = "Boss";
			else if (HasWaveType(ctx.waveTypeForFiltering, MonsterWaveType::Gekk))
				wave_type_str = "Gekk";
			else if (ctx.waveTypeForFiltering == MonsterWaveType::None)
				wave_type_str = "None";
			else
				wave_type_str = "Mixed";

			gi.Com_PrintFmt("WARNING: Emergency fallback ignoring wave type (WaveType={}, FlyPoint={}, EffLvl={}, Recovery={})\n",
				wave_type_str, ctx.isSpawnPointFlying, ctx.effectiveLevel, ctx.isRecoveryModeActive);
			last_warning_time = level.time;
		}
	}
	for (size_t i = 0; i < MONSTER_DATA_COUNT; ++i)
	{
		const auto& monster = monsterTypes[i];
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
horde::MonsterTypeID G_HordePickMonsterType(
	edict_t* spawn_point,
	int32_t currentActualLevel_param,
	MonsterWaveType currentActualWaveType_param,
	bool isRetaliationActive_param,
	bool isRecoveryModeActive_param,
	MonsterWaveType originalWaveTypeBeforeRecovery_param)
{
	if (spawn_point && !spawn_point->inuse)
		return horde::MonsterTypeID::UNKNOWN;

	// --- 1. Setup Context (no changes) ---
	MonsterSelectionContext ctx;
	ctx.currentActualLevel = currentActualLevel_param;
	ctx.currentActualWaveType = currentActualWaveType_param;
	ctx.isSpawnPointFlying = (spawn_point && spawn_point->style == 1);
	ctx.isRetaliationActive = isRetaliationActive_param;
	ctx.isRecoveryModeActive = isRecoveryModeActive_param;
	ctx.isBossWaveMinionPhase = (ctx.currentActualLevel >= 10 && ctx.currentActualLevel % 5 == 0 && boss_spawned_for_wave);
	ctx.flyingAdjustmentFactor = adjustFlyingSpawnProbability(g_spawn_system.cached_flying_spawn_count);
	ctx.waveTypeForFiltering = isRecoveryModeActive_param ? (HasWaveType(originalWaveTypeBeforeRecovery_param, MonsterWaveType::Flying) ? MonsterWaveType::Flying : MonsterWaveType::Ground) : currentActualWaveType_param;
	if (ctx.isSpawnPointFlying)
	{
		// Dedicated style-1 lanes should always select from flying-eligible monsters.
		ctx.waveTypeForFiltering |= MonsterWaveType::Flying;
	}

	// --- 2. Calculate Effective Level ---
	// PREVENT MULTIPLE ELITES: Check flag BEFORE attempting elite spawn
	bool attemptHigherLevel;
	if (g_special_high_level_monster_spawned_this_wave)
	{
		// Elite already spawned this wave, force normal spawning
		attemptHigherLevel = false;
		if (developer->integer)
		{
			gi.Com_PrintFmt("ELITE ALREADY SPAWNED: Forcing normal spawn for remaining monsters.\n");
		}
	}
	else
	{
		attemptHigherLevel = ShouldAttemptHigherLevelSpawn(ctx.currentActualLevel, ctx.isRetaliationActive, ctx.isRecoveryModeActive);
	}
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
	// IMPORTANT: Use same wave buffer as elite candidate filtering (current wave + next 3 waves)
	// =======================================================================
	if (chosen_monster_id != horde::MonsterTypeID::UNKNOWN && ctx.effectiveLevel > ctx.currentActualLevel)
	{
		const size_t index = static_cast<size_t>(chosen_monster_id);

		// Dynamic elite buffer: Lower for early waves to ensure elites spawn
		// Waves 1-6: +2 buffer (more lenient, guarantees elites available)
		// Waves 7+: +3 buffer (stricter, ensures clear elite distinction)
		const int32_t ELITE_WAVE_BUFFER = (ctx.currentActualLevel < 7) ? 2 : 3;

		if (index < g_monsterData.MONSTER_ARRAY_SIZE && g_monsterData.minWaves[index] >= ctx.currentActualLevel + ELITE_WAVE_BUFFER)
		{
			// CRITICAL: Only set flag when we successfully select a valid elite
			// This prevents multiple elites in the same batch while allowing retries if elite selection fails
			g_special_high_level_monster_spawned_this_wave = true;

			if (developer->integer)
			{
				gi.Com_PrintFmt("ELITE SPAWN SUCCESS: '{}' (minWave {}) spawned in wave {} (buffer={}). Flag set.\n",
					horde::MonsterTypeRegistry::GetClassname(chosen_monster_id),
					g_monsterData.minWaves[index],
					ctx.currentActualLevel,
					ELITE_WAVE_BUFFER);
			}
		}
		else if (attemptHigherLevel && developer->integer)
		{
			// Elite attempt but didn't meet buffer requirement - allow retry
			gi.Com_PrintFmt("ELITE ATTEMPT FAILED: '{}' (minWave {}) doesn't meet +{} buffer (need >= {}). Flag NOT set, allowing retry.\n",
				horde::MonsterTypeRegistry::GetClassname(chosen_monster_id),
				g_monsterData.minWaves[index],
				ELITE_WAVE_BUFFER,
				ctx.currentActualLevel + ELITE_WAVE_BUFFER);
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
	g_horde_grid_first = gi.cvar("g_horde_grid_first", "0", CVAR_NOFLAGS);
	pvm = gi.cvar("pvm", "0", CVAR_LATCH);
	// gi.Com_Print("After starting a normal server type: starthorde to start a game.\n");

	if ((g_horde->integer || pvm->integer) && !deathmatch->integer)
	{
		gi.Com_Print("Clearing stale Horde/PvM cvars for non-deathmatch game start.\n");
		gi.cvar_forceset("horde", "0");
		gi.cvar_forceset("pvm", "0");
		g_horde = gi.cvar("horde", "0", CVAR_LATCH);
		pvm = gi.cvar("pvm", "0", CVAR_LATCH);
	}

	if (!deathmatch->integer && !coop->integer && !pvm->integer)
	{
		gi.cvar_forceset("ctf", "0");
		gi.cvar_forceset("teamplay", "0");
		gi.cvar_forceset("maxclients", "1");
		gi.cvar_forceset("bot_minClients", "-1");
		gi.cvar_forceset("bot_pause", "1");
		gi.cvar_forceset("g_horde_grid_first", "0");
		gi.cvar_forceset("g_instagib", "0");
		gi.cvar_forceset("g_dm_spawns", "0");
		gi.cvar_forceset("g_dm_spawn_farthest", "0");
		gi.cvar_forceset("g_use_hook", "0");
		gi.cvar_forceset("g_hook_wave", "0");
		gi.cvar_forceset("g_allow_grapple", "0");
		gi.cvar_forceset("g_allow_techs", "0");
		gi.cvar_forceset("g_loadent", "0");
		gi.cvar_forceset("dm_monsters", "0");
		gi.cvar_forceset("g_hardcoop", "0");
		gi.cvar_forceset("vortex", "0");
		gi.cvar_forceset("g_disable_player_collision", "0");
		gi.cvar_forceset("g_damage_scale", "1");
		gi.cvar_forceset("ai_damage_scale", "1");
		gi.cvar_forceset("ai_allow_dm_spawn", "0");
		gi.cvar_forceset("timelimit", "0");
		gi.cvar_forceset("fraglimit", "0");
		gi.cvar_forceset("capturelimit", "0");
		gi.cvar_forceset("maxspectators", "0");
		gi.cvar_forceset("g_start_items", "");
		gi.cvar_forceset("cheats", "0");
		gi.AddCommandString("kexmultiplayer maxplayers 1\n");
	}

	// PvM (Player vs Monster) mode requires horde mode to be active for spawning/gameplay
	// Auto-enable horde mode if PvM is enabled
	if (pvm->integer && !g_horde->integer)
	{
		gi.Com_Print("PvM mode enabled - automatically enabling Horde mode for gameplay...\n");
		gi.cvar_forceset("horde", "1");
		g_horde = gi.cvar("horde", "1", CVAR_LATCH); // Re-get the cvar with updated value
	}

	if (!g_horde->integer)
	{
		// Coop/non-Horde modes own their own difficulty settings. Do not force
		// g_hardcoop here, otherwise switching out of Horde makes that cvar sticky.
		gi.cvar_forceset("deathmatch", "0");
		return;
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
		// gi.cvar_forceset("g_vampire", "0");
		// gi.cvar_forceset("g_ammoregen", "0");
		// gi.cvar_forceset("g_tracedbullets", "0");
		// gi.cvar_forceset("g_energyshells", "0");
		// gi.cvar_forceset("g_bouncygl", "0");
		// gi.cvar_forceset("g_bfgpull", "0");
		// gi.cvar_forceset("g_bfgslide", "1");
		// gi.cvar_forceset("g_energyshells", "0");
		// gi.cvar_forceset("g_autohaste", "0");
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
	// Safety check: ensure game is fully initialized and not in map transition
	if (!g_edicts || !game.clients || game.maxclients == 0 ||
		level.intermissiontime || !level.mapname[0])
		return;

	// developer >= 2: Disable bot spawning for debugging
	if (developer->integer >= 2)
	{
		gi.cvar_set("bot_minClients", "-1");
		return;
	}

	// Track server initialization and previous map size
	static bool server_first_map = true;
	static bool previous_map_was_big = false;
	static char last_map_for_bot_init[MAX_QPATH] = { 0 };

	const horde::MapSize mapSize = GetMapSize(static_cast<const char*>(level.mapname));
	const int32_t spectPlayers = GetNumSpectPlayers();
	const int32_t baseBots = mapSize.isBigMap ? 6 : 4;

	// Calculate required bots with a maximum cap to prevent overloading
	constexpr int32_t MAX_BOTS = 8;
	const int32_t requiredBots = std::min(baseBots + spectPlayers, MAX_BOTS);

	// Detect map change
	const bool map_changed = Q_strcasecmp(last_map_for_bot_init, level.mapname) != 0;

	if (map_changed)
	{
		if (server_first_map)
		{
			// First map ever on server start - instant spawn all bots
			for (int32_t i = 0; i < baseBots; i++)
			{
				gi.AddCommandString("addbot\n");
			}
			server_first_map = false;

			if (developer->integer)
			{
				gi.Com_PrintFmt("VerifyAndAdjustBots: First map - spawned {} bots (map: {}, isBig: {})\n",
					baseBots, level.mapname, mapSize.isBigMap);
			}
		}
		else
		{
			// Subsequent map - adjust based on size change
			if (mapSize.isBigMap && !previous_map_was_big)
			{
				// Small/Medium -> Big: add 2 bots instantly
				gi.AddCommandString("addbot\n");
				gi.AddCommandString("addbot\n");

				if (developer->integer)
				{
					gi.Com_PrintFmt("VerifyAndAdjustBots: Small->Big map transition - added 2 bots\n");
				}
			}
			else if (!mapSize.isBigMap && previous_map_was_big)
			{
				// Big -> Small/Medium: kick 2 bots instantly
				gi.AddCommandString("kickbot\n");
				gi.AddCommandString("kickbot\n");

				if (developer->integer)
				{
					gi.Com_PrintFmt("VerifyAndAdjustBots: Big->Small map transition - kicked 2 bots\n");
				}
			}
		}

		// Update tracking
		Q_strlcpy(last_map_for_bot_init, level.mapname, sizeof(last_map_for_bot_init));
		previous_map_was_big = mapSize.isBigMap;
	}

	// Always set bot_minClients for the engine to maintain the bot count
	gi.cvar_set("bot_minClients", std::to_string(requiredBots).c_str());
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
		gitem_t* item = &itemlist[i]; // Or use GetItemByIndex(i) if preferred

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

	static const std::array<std::pair<cached_soundindex*, const char*>, 7> individual_sounds = { {{&sound_tele3, "misc/r_tele3.wav"},
																								   {&sound_tele_up, "misc/tele_up.wav"},
																								   {&sound_spawn1, "misc/spawn1.wav"},
																								   {&incoming, "world/incoming.wav"},
																								   {&talk, "misc/talk.wav"},
																								   {&tele1, "misc/tele1.wav"},
																								   {&sound_quake, "world/quake.wav"}} };

	// Precache special wave sounds (Gekk and Berserk waves)
	gi.soundindex("gek/gek_low.wav");
	gi.soundindex("gek/gek_amb.wav");
	gi.soundindex("world/radio3.wav");

	// Use std::span for safe iteration
	std::span individual_view{ individual_sounds };
	for (const auto& [sound_index, path] : individual_view)
	{
		sound_index->assign(path);
	}

	std::span wave_view{ WAVE_SOUND_PATHS };
	for (size_t i = 0; i < NUM_WAVE_SOUNDS; ++i)
	{
		wave_sounds[i].assign(wave_view[i]);
	}

	std::span start_view{ START_SOUND_PATHS };
	for (size_t i = 0; i < NUM_START_SOUNDS; ++i)
	{
		start_sounds[i].assign(start_view[i]);
	}

	sounds_precached = true;
}

template <size_t N>
static void ResetShuffledSoundOrder(std::array<size_t, N>& order, size_t& cursor)
{
	for (size_t i = 0; i < N; ++i)
	{
		order[i] = i;
	}

	if constexpr (N > 1)
	{
		for (size_t i = N - 1; i > 0; --i)
		{
			const size_t j = static_cast<size_t>(irandom(static_cast<int32_t>(i + 1)));
			std::swap(order[i], order[j]);
		}
	}

	cursor = 0;
}

template <size_t N>
static size_t NextShuffledSoundIndex(std::array<size_t, N>& order, size_t& cursor)
{
	if (cursor >= N)
	{
		ResetShuffledSoundOrder(order, cursor);
	}

	return order[cursor++];
}

static std::array<size_t, NUM_WAVE_SOUNDS> g_wave_sound_order = {};
static size_t g_wave_sound_cursor = NUM_WAVE_SOUNDS;

static int GetRandomWaveSound()
{
	const size_t index = NextShuffledSoundIndex(g_wave_sound_order, g_wave_sound_cursor);
	return wave_sounds[index];
}

static std::array<size_t, NUM_START_SOUNDS> g_start_sound_order = {};
static size_t g_start_sound_cursor = NUM_START_SOUNDS;

static void PlayWaveStartSound()
{
	const size_t index = NextShuffledSoundIndex(g_start_sound_order, g_start_sound_cursor);
	gi.sound(world, CHAN_VOICE, start_sounds[index], 1, ATTN_NONE, 0);
}
// Capping resets on map end

static bool hasBeenReset = false;
void AllowReset() noexcept
{
	hasBeenReset = false;
}



// This function now ONLY precaches monsters for Wave 1 for a very fast initial map load.
// Subsequent waves are handled by the JIT precacher in Horde_InitLevel.
// Enforces PrecacheLimits::MAX_PRECACHED_MODEL_FAMILIES to prevent bad_alloc on late waves.
static void PrecacheAllMonsters()
{
	// Only run this initial precache once per map load.
	if (monsters_precached)
	{
		return;
	}
	// Reset all flags to false.
	g_precached_monster_types_flags.fill(false);

	if (g_precached_families_this_map.empty())
	{
		// Fallback for callers that have not initialized rotation yet.
		for (const auto& core_family : CORE_FAMILIES) {
			g_precached_families_this_map.insert(core_family);
		}

		uint32_t map_hash = 0;
		if (level.mapname) {
			for (const char* p = level.mapname; *p; ++p) {
				map_hash = map_hash * 31 + static_cast<uint32_t>(*p);
			}
		}
		size_t rotation_offset = static_cast<size_t>(map_hash) % ROTATING_FAMILIES.size();

		for (size_t i = 0; i < PrecacheLimits::ROTATING_FAMILY_SLOTS && i < ROTATING_FAMILIES.size(); ++i) {
			size_t idx = (rotation_offset + i) % ROTATING_FAMILIES.size();
			g_precached_families_this_map.insert(ROTATING_FAMILIES[idx]);
		}
	}

	if (developer->integer) {
		gi.Com_PrintFmt("PRECACHE LIMIT: {} families allowed ({} core + {} rotating)\n",
			g_precached_families_this_map.size(),
			CORE_FAMILIES.size(),
			PrecacheLimits::ROTATING_FAMILY_SLOTS);
	}

	// PVM Mode: Precache all random monsters for this map
	if (IsPvMMode())
	{
		const horde::MonsterTypeID* pvm_monsters = PVM_GetRandomMonsters();
		const int pvm_monster_count = PVM_GetRandomMonsterCount();
		if (pvm_monsters)
		{
			if (developer->integer)
			{
				gi.Com_PrintFmt("PVM PRECACHE: Loading {} random monsters for this map...\n", pvm_monster_count);
			}

			for (int i = 0; i < pvm_monster_count; ++i)
			{
				const auto& monster_id = pvm_monsters[i];
				const char* classname = horde::MonsterTypeRegistry::GetClassname(monster_id);
				if (classname && *classname)
				{
					edict_t* temp_monster = G_Spawn();
					if (temp_monster)
					{
						temp_monster->classname = classname;
						temp_monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
						ED_CallSpawn(temp_monster);
						if (temp_monster->inuse)
						{
							G_FreeEdict(temp_monster);
						}
						MarkMonsterTypePrecached(monster_id);

						if (developer->integer)
						{
							gi.Com_PrintFmt("  - Precached: {}\n", classname);
						}
					}
				}
			}
			monsters_precached = true;
			return;
		}
	}

	// Normal Horde Mode: AGGRESSIVE PRECACHING for variety
	// Phase 1: Precache all wave 1-3 monsters (core gameplay) - respecting family limit
	if (developer->integer)
	{
		gi.Com_Print("INITIAL PRECACHE: Loading monsters for Waves 1-3 (with family limit)...\n");
	}

	int precached_count = 0;
	int skipped_family_limit = 0;
	for (size_t i = 0; i < MONSTER_DATA_COUNT; ++i)
	{
		const auto& monster_info = monsterTypes[i];
		if (monster_info.minWave > 3)
		{
			continue; // Changed from break to continue - process all monsters
		}

		// Check family limit before precaching
		AssetFamilyID family = GetMonsterAssetFamily(monster_info.typeId);
		if (g_precached_families_this_map.find(family) == g_precached_families_this_map.end())
		{
			// Family not in allowed list - skip this monster
			if (developer->integer)
			{
				const char* classname = horde::MonsterTypeRegistry::GetClassname(monster_info.typeId);
				gi.Com_PrintFmt("  - SKIPPED (family {} not allowed): {}\n", static_cast<int>(family), classname ? classname : "unknown");
			}
			skipped_family_limit++;
			continue;
		}

		const char* classname = horde::MonsterTypeRegistry::GetClassname(monster_info.typeId);
		if (classname && *classname)
		{
			edict_t* temp_monster = G_Spawn();
			if (temp_monster)
			{
				temp_monster->classname = classname;
				temp_monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
				ED_CallSpawn(temp_monster);
				if (temp_monster->inuse)
				{
					G_FreeEdict(temp_monster);
				}
				MarkMonsterTypePrecached(monster_info.typeId);
				precached_count++;

				if (developer->integer)
				{
					gi.Com_PrintFmt("  - Precached (wave {}): {}\n", monster_info.minWave, classname);
				}
			}
		}
	}

	if (developer->integer)
	{
		gi.Com_PrintFmt("INITIAL PRECACHE: {} monsters loaded, {} skipped due to family limit\n",
			precached_count, skipped_family_limit);
	}

	// Phase 2: Precache one monster from each priority family for variety
	// Only precache families that are in the allowed list for this map
	static constexpr AssetFamilyID priority_families[] = {
		AssetFamilyID::GUNNER_FAMILY,
		AssetFamilyID::TANK_FAMILY,
		AssetFamilyID::FIXBOT_FAMILY,
		AssetFamilyID::GLADIATOR_FAMILY,
		AssetFamilyID::BERSERK_FAMILY,
		AssetFamilyID::MUTANT_FAMILY,
		AssetFamilyID::FLYER_FAMILY
	};

	if (developer->integer)
	{
		gi.Com_Print("INITIAL PRECACHE: Loading priority family representatives (if allowed)...\n");
	}

	for (const auto family : priority_families)
	{
		// Skip if this family is not in the allowed list for this map
		if (g_precached_families_this_map.find(family) == g_precached_families_this_map.end())
		{
			if (developer->integer)
			{
				gi.Com_PrintFmt("  - SKIPPED family {} (not in allowed list)\n", static_cast<int>(family));
			}
			continue;
		}

		// Find the lowest minWave monster in this family that isn't already precached
		for (size_t i = 0; i < MONSTER_DATA_COUNT; ++i)
		{
			const auto& monster = monsterTypes[i];
			if (GetMonsterAssetFamily(monster.typeId) == family &&
				monster.minWave < 999 &&
				!g_precached_monster_types_flags[static_cast<size_t>(monster.typeId)])
			{
				const char* classname = horde::MonsterTypeRegistry::GetClassname(monster.typeId);
				if (classname && *classname)
				{
					edict_t* temp_monster = G_Spawn();
					if (temp_monster)
					{
						temp_monster->classname = classname;
						temp_monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
						ED_CallSpawn(temp_monster);
						if (temp_monster->inuse)
						{
							G_FreeEdict(temp_monster);
						}
						MarkMonsterTypePrecached(monster.typeId);

						if (developer->integer)
						{
							gi.Com_PrintFmt("  - Precached family rep (wave {}): {}\n", monster.minWave, classname);
						}
					}
				}
				break; // One per family
			}
		}
	}

	if (developer->integer)
	{
		gi.Com_PrintFmt("PRECACHE COMPLETE: {} total families allowed for this map\n",
			g_precached_families_this_map.size());
	}

	monsters_precached = true;
}


// Initialize the monster-to-family mapping for spawn variety tracking
static void InitializeMonsterFamilyMapping()
{
	// Initialize all to UNKNOWN_FAMILY first
	g_monster_to_family.fill(AssetFamilyID::UNKNOWN_FAMILY);

	using namespace horde;

	// Soldier family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SOLDIER_LIGHT)] = AssetFamilyID::SOLDIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SOLDIER)] = AssetFamilyID::SOLDIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SOLDIER_SS)] = AssetFamilyID::SOLDIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SOLDIER_HYPERGUN)] = AssetFamilyID::SOLDIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SOLDIER_RIPPER)] = AssetFamilyID::SOLDIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SOLDIER_LASERGUN)] = AssetFamilyID::SOLDIER_FAMILY;

	// Tank family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::TANK)] = AssetFamilyID::TANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::TANK_COMMANDER)] = AssetFamilyID::TANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::TANK_64)] = AssetFamilyID::TANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::RUNNERTANK)] = AssetFamilyID::TANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::TANK_SPAWNER)] = AssetFamilyID::TANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::EASTERTANK)] = AssetFamilyID::TANK_FAMILY;

	// Gladiator family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GLADIATOR)] = AssetFamilyID::GLADIATOR_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GLADIATOR_B)] = AssetFamilyID::GLADIATOR_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GLADIATOR_C)] = AssetFamilyID::GLADIATOR_FAMILY;

	// Gunner family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GUNNER)] = AssetFamilyID::GUNNER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GUNNER_VANILLA)] = AssetFamilyID::GUNNER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GUNCMDR)] = AssetFamilyID::GUNNER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GUNCMDR_VANILLA)] = AssetFamilyID::GUNNER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GUNCMDR_KL)] = AssetFamilyID::GUNNER_FAMILY;

	// Infantry family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::INFANTRY)] = AssetFamilyID::INFANTRY_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::INFANTRY_VANILLA)] = AssetFamilyID::INFANTRY_FAMILY;

	// Arachnid family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SPIDER)] = AssetFamilyID::ARACHNID_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::ARACHNID)] = AssetFamilyID::ARACHNID_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::ARACHNID2)] = AssetFamilyID::ARACHNID_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GM_ARACHNID)] = AssetFamilyID::ARACHNID_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::PSX_ARACHNID)] = AssetFamilyID::ARACHNID_FAMILY;

	// Guardian family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GUARDIAN)] = AssetFamilyID::GUARDIAN_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::PSX_GUARDIAN)] = AssetFamilyID::GUARDIAN_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::JANITOR2)] = AssetFamilyID::GUARDIAN_FAMILY;

	// Supertank family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::JANITOR)] = AssetFamilyID::SUPERTANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SUPERTANK)] = AssetFamilyID::SUPERTANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SUPERTANKKL)] = AssetFamilyID::SUPERTANK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::BOSS5)] = AssetFamilyID::SUPERTANK_FAMILY;

	// Fixbot family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::FIXBOT)] = AssetFamilyID::FIXBOT_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::FIXBOT_KL)] = AssetFamilyID::FIXBOT_FAMILY;

	// Turret family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::TURRET)] = AssetFamilyID::TURRET_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SENTRYGUN)] = AssetFamilyID::TURRET_FAMILY;

	// Daedalus family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::DAEDALUS)] = AssetFamilyID::DAEDALUS_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::DAEDALUS_BOMBER)] = AssetFamilyID::DAEDALUS_FAMILY;

	// Parasite family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::PARASITE)] = AssetFamilyID::PARASITE_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::PERRO_KL)] = AssetFamilyID::PARASITE_FAMILY;

	// Boss2 family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::BOSS2)] = AssetFamilyID::BOSS2_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::BOSS2_64)] = AssetFamilyID::BOSS2_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::BOSS2_MINI)] = AssetFamilyID::BOSS2_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::BOSS2_KL)] = AssetFamilyID::BOSS2_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::BOSS3_STAND)] = AssetFamilyID::BOSS2_FAMILY;

	// Carrier family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::CARRIER)] = AssetFamilyID::CARRIER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::CARRIER_MINI)] = AssetFamilyID::CARRIER_FAMILY;

	// Widow family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::WIDOW)] = AssetFamilyID::WIDOW_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::WIDOW1)] = AssetFamilyID::WIDOW_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::WIDOW2)] = AssetFamilyID::WIDOW_FAMILY;

	// Shambler family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SHAMBLER)] = AssetFamilyID::SHAMBLER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SHAMBLER_SMALL)] = AssetFamilyID::SHAMBLER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::SHAMBLER_KL)] = AssetFamilyID::SHAMBLER_FAMILY;

	// Makron family
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::MAKRON)] = AssetFamilyID::MAKRON_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::MAKRON_KL)] = AssetFamilyID::MAKRON_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::JORG)] = AssetFamilyID::MAKRON_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::JORG_SMALL)] = AssetFamilyID::MAKRON_FAMILY;

	// Individual families
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::BERSERK)] = AssetFamilyID::BERSERK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::BERSERKERKL)] = AssetFamilyID::BERSERK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::BRAIN)] = AssetFamilyID::BRAIN_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::CHICK)] = AssetFamilyID::CHICK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::CHICK_HEAT)] = AssetFamilyID::CHICK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::CHICKKL)] = AssetFamilyID::CHICK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::EASTERCHICK)] = AssetFamilyID::CHICK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::EASTERCHICK2)] = AssetFamilyID::CHICK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::FLYER)] = AssetFamilyID::FLYER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::HOVER)] = AssetFamilyID::HOVER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::HOVER_VANILLA)] = AssetFamilyID::HOVER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::FLIPPER)] = AssetFamilyID::HOVER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::MEDIC)] = AssetFamilyID::MEDIC_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::MEDIC_COMMANDER)] = AssetFamilyID::MEDIC_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::MUTANT)] = AssetFamilyID::MUTANT_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::REDMUTANT)] = AssetFamilyID::MUTANT_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::FLOATER)] = AssetFamilyID::FLOATER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::FLOATER_TRACKER)] = AssetFamilyID::FLOATER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::STALKER)] = AssetFamilyID::STALKER_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GEKK)] = AssetFamilyID::GEKK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::GEKKKL)] = AssetFamilyID::GEKK_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::MISC_INSANE)] = AssetFamilyID::INSANE_FAMILY;

	// Misc/unknown
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::COMMANDER_BODY)] = AssetFamilyID::MISC_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::BIGVIPER)] = AssetFamilyID::MISC_FAMILY;
	g_monster_to_family[static_cast<size_t>(MonsterTypeID::KAMIKAZE)] = AssetFamilyID::MISC_FAMILY;
}

void Horde_Init()
{
	sounds_precached = false;
	items_precached = false;
	ResetBosses();

	// Initialize monster family mapping for spawn variety tracking
	InitializeMonsterFamilyMapping();

	// Initialize character persistence system
	Character_Init();

	// Initialize PVM random monster selection BEFORE precaching
	PVM_InitRandomMonsters();

	PrecacheAllGameItems();
	PrecacheWaveSounds();
	monsters_precached = false; // Reset the precache flag for the new map.

	InitializeWaveSystem();
	last_wave_number = 0;

	AllowReset();
	g_spawn_system.spawn_map_needs_build = true; // SET THE FLAG INSTEAD
	ResetGame(); // This will call RebuildSpawnPointCacheIfNeeded indirectly or directly

	gi.Com_Print("PRINT: Horde game state initialized; wave monster precache will run with map rotation.\n");
}

// Helper function to select a suitable boss weapon drop based on wave level


// Constants for item dropping physics (if not defined globally)
constexpr int MIN_VELOCITY = -800;
constexpr int MAX_VELOCITY = 800;
constexpr int MIN_VERTICAL_VELOCITY = 400;

// Forward declarations (removed - now in horde_boss.h)
// void RestoreFog();

// Asegúrate de limpiar entidades muertas
// NOTE: This function uses active_or_dead_monsters() iterator for efficient monster iteration.
// The second loop over all edicts is necessary for gibs/projectiles as they are not monsters.
// This is only called during wave cleanup/skipwave, not every frame, so performance is acceptable.
void Horde_CleanBodies()
{
	// Clean up dead monsters - uses optimized iterator
	for (edict_t* ent : active_or_dead_monsters())
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

	// Clean up gibs and stuck projectiles
	// NOTE: This linear scan is necessary as gibs/projectiles are not indexed separately
	// Start after body queue to avoid trying to free special edicts
	for (int i = game.maxclients + static_cast<int>(BODY_QUEUE_SIZE) + 1; i < static_cast<int>(globals.num_edicts); i++)
	{
		edict_t* ent = &g_edicts[i];

		if (!ent->inuse || !ent->classname)
			continue;

		// Check for gibs (bodyque entities are in the protected range and handled separately)
		if (strcmp(ent->classname, "gib") == 0)
		{
			// Start fade out if not already fading
			if (!ent->is_fading_out)
			{
				StartFadeOut(ent);
			}
		}
		// Clean up stuck/old projectiles (grenades, rockets that didn't explode)
		else if ((strcmp(ent->classname, "grenade") == 0 ||
			strcmp(ent->classname, "rocket") == 0) &&
			ent->timestamp > 0_ms &&
			level.time > ent->timestamp + 5_sec) // 5 seconds past their normal timeout
		{
			if (!ent->is_fading_out)
			{
				StartFadeOut(ent);
			}
		}
	}

	// Restore normal fog and turn off flashlights
	if (horde_fog_active)
		RestoreFog();
}

// CheckAndTeleportBoss function removed - now in horde_boss.cpp

// SetHealthBarName function removed - now in horde_boss.cpp

// Forward declaration for string_view version
void AppendHordeMessage_impl(std::string_view message, gtime_t duration);

// Overload for const char* (needed by horde_boss.cpp)
void AppendHordeMessage(const char* message, gtime_t duration)
{
	if (message)
		AppendHordeMessage_impl(std::string_view(message), duration);
}

// Internal string_view overload - marked inline to avoid unused function warning
// This is called through the const char* version above
static inline void AppendHordeMessage(std::string_view message, gtime_t duration = 5_sec)
{
	AppendHordeMessage_impl(message, duration);
}

void AppendHordeMessage_impl(std::string_view message, gtime_t duration)
{
	// Early validation for empty messages or zero duration
	if (message.empty() || duration <= 0_ms)
	{
		return;
	}

	// Get current message from the configstring as string_view to avoid allocation
	const char* current_msg_ptr = gi.get_configstring(CONFIG_HORDEMSG);
	size_t current_len = current_msg_ptr ? strlen(current_msg_ptr) : 0;

	// Calculate required size
	size_t new_len = current_len;
	if (current_len > 0) {
		new_len++; // For newline
	}
	new_len += message.length();

	// Check if we exceed the limit
	if (new_len >= MAX_STRING_CHARS) {
		// If the new message alone would overflow, truncate it
		if (message.length() >= MAX_STRING_CHARS) {
			char truncated_msg[MAX_STRING_CHARS];
			size_t copy_len = std::min(message.length(), MAX_STRING_CHARS - 1);
			memcpy(truncated_msg, message.data(), copy_len);
			truncated_msg[copy_len] = '\0';
			gi.configstring(CONFIG_HORDEMSG, truncated_msg);
		}
		else {
			// Need to combine messages - only allocate when necessary
			std::string combined_msg;
			combined_msg.reserve(MAX_STRING_CHARS);

			if (current_len > 0) {
				combined_msg.append(current_msg_ptr, current_len);
				combined_msg += '\n';
			}
			combined_msg.append(message);

			// Truncate if needed
			if (combined_msg.length() >= MAX_STRING_CHARS) {
				combined_msg.resize(MAX_STRING_CHARS - 1);
			}

			gi.configstring(CONFIG_HORDEMSG, combined_msg.c_str());
		}
	}
	else {
		char msg_buffer[MAX_STRING_CHARS];
		char* write_ptr = msg_buffer;

		if (current_len > 0) {
			memcpy(write_ptr, current_msg_ptr, current_len);
			write_ptr += current_len;
			*write_ptr++ = '\n';
		}

		memcpy(write_ptr, message.data(), message.length());
		write_ptr += message.length();
		*write_ptr = '\0';

		gi.configstring(CONFIG_HORDEMSG, msg_buffer);
	}

	// Extend or set the duration for the new combined message
	horde_message_end_time = level.time + duration;
}

void ClearHordeMessage()
{
	std::string_view const current_msg = gi.get_configstring(CONFIG_HORDEMSG);
	if (!current_msg.empty())
	{
		gi.configstring(CONFIG_HORDEMSG, "");
	}
	horde_message_end_time = 0_sec;
}

void ResetWaveAdvanceState() noexcept;
static void ResetStroggCleanup();
static bool ForceKillMostStuckMonster();

//
// game resetting
//

// Reset ambush system state
static void ResetAmbushSystem()
{
	last_ambush_time = 0_sec;
	ambush_cooldown_end = 0_sec;
	waves_since_ambush = 0;
	// ambush_system_initialized = false;
}

static void ResetWaveMemory()
{
	previous_wave_types.fill(MonsterWaveType::None);
	wave_memory_index = 0;
}

// ResetRecentBosses moved to horde_boss.cpp

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
	g_spawn_system.spawn_points_data.clear();

	// Reset spawn point count to match cleared vectors (prevents out-of-bounds access)
	g_num_spawn_points = 0;

	// 2. Reset global helper trackers.
	horde::g_monsterSpawnTracker.Reset();
	horde::g_spawnPointTimeTracker.Reset();

	// 3. Recalculate the global SPAWN_POINT_COOLDOWN for a fresh start.
	// This logic is a simplified version of what happens in UnifiedAdjustSpawnRate,
	// tailored for a level 0 (reset) state.
	const horde::MapSize& mapSize = g_horde_local.current_map_size;
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

static void ResetPlayerDeployedItems()
{
	for (uint32_t i = 0; i < game.maxclients; ++i)
	{
		edict_t* player_ent = g_edicts + 1 + i;
		if (!player_ent || !player_ent->inuse || !player_ent->client)
			continue;

		gclient_t* client = player_ent->client;

		// Lasers
		client->resp.num_lasers = 0;
		std::fill(client->resp.deployed_lasers, client->resp.deployed_lasers + LaserConstants::MAX_LASERS_ARRAY_SIZE, nullptr);
		client->resp.oldest_tesla_idx = 0; // Assuming this is for tesla index

		// Teslas
		client->resp.num_teslas = 0;
		std::fill(client->resp.deployed_teslas, client->resp.deployed_teslas + TeslaConstants::MAX_TESLAS_ARRAY_SIZE, nullptr);
		client->resp.oldest_tesla_idx = 0;

		// Traps
		client->resp.num_traps = 0;
		std::fill(client->resp.deployed_traps, client->resp.deployed_traps + TrapConstants::MAX_TRAPS_ARRAY_SIZE, nullptr);
		client->resp.oldest_trap_idx = 0;

		// Prox Mines
		client->resp.num_proxs = 0;
		std::fill(client->resp.deployed_proxs, client->resp.deployed_proxs + ProxConstants::MAX_PROXS_ARRAY_SIZE, nullptr);
		client->resp.oldest_prox_idx = 0;

		// Sentries
		client->resp.num_sentries = 0;
		std::fill(client->resp.deployed_sentries, client->resp.deployed_sentries + SentryConstants::MAX_SENTRIES_ARRAY_SIZE, nullptr);

		// Barrels
		client->resp.num_barrels = 0;
		std::fill(client->resp.deployed_barrels, client->resp.deployed_barrels + BarrelConstants::MAX_BARRELS_ARRAY_SIZE, nullptr);

		// Summoned Stroggs
		client->resp.num_summons = 0;
		std::fill(client->resp.deployed_summons, client->resp.deployed_summons + SummonConstants::MAX_SUMMONS_ARRAY_SIZE, nullptr);

		// Timers
		client->resp.teleport_cooldown = 3_sec; // Reset to default or 0_sec if appropriate
		client->resp.lasthbshot = 0_sec;
		client->resp.bombspell_forward_cooldown = 0_sec;
		client->resp.bombspell_area_cooldown = 0_sec;

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
		// if (developer && developer->integer > 1)
		// {
		// 	gi.Com_PrintFmt("INFO: Reset already performed, skipping...\n");
		// }
		return;
	}
	hasBeenReset = true;

	if (developer && developer->integer)
	{
		gi.Com_PrintFmt("INFO: Performing full game state reset...\n");
	}

	// ========================================================================
	// CONTAINER RESET STRATEGY DOCUMENTATION
	// ========================================================================
	// This function resets all game state containers to prevent bugs across
	// map changes and server restarts. The strategy is:
	//
	// RESET (cleared in this function):
	//   - All boost::flat_map/flat_set tracking entity state (traps, emitters, bosses, morphs, teleport cache)
	//   - Progressive precaching state (excluded/precached monsters, models)
	//   - Spawn system state (spawn_plan, special_spawn_state, spawn_point_map, spawn_history)
	//   - Wave state arrays (previous_wave_types via ResetWaveMemory(), g_wave_sound_order, g_start_sound_order)
	//   - Recent spawns, teleport tracking, benefits
	//   - LaserPool: BFG laser entity pool (prevents dangling pointers)
	//   - Profiler data: Accumulated profiling statistics (prevents unbounded growth)
	//   - HordePhys grids: Proximity and spawn grids (prevents stale entity references)
	//
	// INTENTIONALLY PERSISTENT (NOT reset):
	//   - g_map_rotation_seed: Maintains rotation consistency across waves
	//   - g_map_family_history: Maintains variety across maps
	//   - Lookup tables (weapon IDs, monster IDs, etc.): Const-like data
	//   - g_precached_models/g_precached_sounds: Engine-cached assets persist
	//
	// PER-WAVE RESET (cleared each wave, not here):
	//   - g_eligible_monsters_for_wave: Rebuilt each wave
	//   - g_eligible_item_indices_for_wave: Rebuilt each wave
	// ========================================================================

	g_horde_retaliation_end_time = 0_sec;
	g_horde_retaliation_last_trigger_time = 0_sec;

	//resetting idview special entities
	g_targetable_special_entities.clear();

	// FIX: Clear monster family map to prevent incorrect state across map changes
	g_monster_family_map.clear();

	// FIX: Clear progressive precaching state for proper monster rotation on server restart
	g_excluded_monsters_this_map.clear();
	g_precached_monsters_this_map.clear();
	g_precached_models_this_map.clear();
	g_precached_families_this_map.clear();  // Reset family tracking for precache limit
	g_precached_monster_types_flags.fill(false);
	monsters_precached = false;
	// Note: g_map_rotation_seed is intentionally NOT reset here to maintain rotation across waves

	// recent spawns
	g_recent_spawns.positions.fill(vec3_origin);
	g_recent_spawns.cooldowns_until.fill(0_sec);
	g_recent_spawn_index = 0;

	RecordCurrentMapFamilyUsageToHistory();

	// FIX: Clear spawn history to prevent previous map's spawn patterns from affecting new map
	// Note: g_map_family_history is intentionally NOT reset to maintain variety across maps
	g_spawn_history.fill(SpawnHistoryEntry{});
	g_spawn_history_index = 0;

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
	auto_spawned_bosses.clear();  // FIX: Clear boss spawn tracking
	ResetAllMorphData();  // FIX: Clear player morph state
	ResetTankTeleportCache();  // FIX: Clear tank teleport cache

	// --- FIX: Clear memory leak sources ---
	LaserPool_Clear();  // FIX: Clear BFG laser pool to prevent dangling entity pointers
	Profiler_Reset();  // FIX: Clear profiling data to prevent unbounded memory growth
	HordePhys::g_monster_grid.Reset();  // FIX: Clear monster proximity grid to prevent stale entity references
	HordePhys::g_entity_grid.Reset();  // FIX: Clear general entity grid to prevent stale entity references
	HordePhys::g_spawn_grid.Clear();  // FIX: Clear spawn position grid to free cached spawn nodes

	// --- FIX: Clear performance cache systems to prevent stale data ---
	HordePerf::g_spawn_spatial_index.Clear();  // FIX: Clear spawn spatial index (also cleared in BuildSpawnPointMap, but explicit here)
	HordePerf::g_monster_type_cache.Clear();   // FIX: Clear monster type property cache to prevent stale monster data
	HordePerf::g_visibility_cache.Clear();     // FIX: Clear visibility check cache to prevent stale entity reference checks

	// =======================================================================
	// --- UNIFIED RESET (THIS IS THE FIX) ---
	// =======================================================================
	g_spawn_system.spawn_plan.clear();
	g_spawn_system.special_spawn_state.clear(); // This replaces the old ambush/retaliation resets
	// REMOVED: g_horde_retaliation_active - using g_spawn_system.special_spawn_state.type instead
	g_horde_retaliation_end_time = 0_sec;
	// =======================================================================

	g_adjusted_monster_cap = 0;
	consistent_zero_counts = 0;
	counter_mismatch_frames = 0;
	horde_message_end_time = 0_sec;
	g_totalMonstersInWave = 0;

	CleanupSpawnPointCache();

	ResetAllSpawnPointDataAndTrackers();
	g_spawn_system.need_spawn_cache_reset = true;
	need_frame_timer_reset = true;
	need_queue_monitor_reset = true;
	g_spawn_system.consecutive_spawn_failures = 0;
	g_spawn_system.consecutive_emergency_failures = 0;
	g_recovery_mode_active = false;
	g_spawn_system.original_wave_type_before_recovery = MonsterWaveType::None;

	// FIX: Clear emergency spawn history to prevent stale positions from previous map
	ResetEmergencySpawnHistory();

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

	if (g_nolag && g_nolag->integer) {
		gi.cvar_set("g_nolag", "0");
	}

	gi.cvar_set("bot_pause", "0");
	gi.cvar_set("cheats", "0");
	gi.cvar_set("g_start_items", "weapon_blaster 1");
	if (g_chaotic) gi.cvar_set("g_chaotic", "0");
	if (g_insane) gi.cvar_set("g_insane", "0");
	if (g_hardcoop) gi.cvar_set("g_hardcoop", "0");
	if (dm_monsters) gi.cvar_set("dm_monsters", "0");
	if (timelimit) gi.cvar_set("timelimit", "60");
	if (ai_damage_scale) gi.cvar_set("ai_damage_scale", "1");
	if (ai_allow_dm_spawn) gi.cvar_set("ai_allow_dm_spawn", "1");
	if (g_damage_scale) gi.cvar_set("g_damage_scale", "1");

	if (g_map_list_shuffle) gi.cvar_set("g_map_list_shuffle", "1");
	//	if (g_vampire) gi.cvar_set("g_vampire", "0");
	//	if (g_startarmor) gi.cvar_set("g_startarmor", "0");
	//	if (g_ammoregen) gi.cvar_set("g_ammoregen", "0");
	//	if (g_upgradeproxs) gi.cvar_set("g_upgradeproxs", "0");
	//	if (g_piercingbeam) gi.cvar_set("g_piercingbeam", "0");
	//	if (g_tracedbullets) gi.cvar_set("g_tracedbullets", "0");
	//	if (g_energyshells) gi.cvar_set("g_energyshells", "0");
	//	if (g_bouncygl) gi.cvar_set("g_bouncygl", "0");
	//	if (g_bfgpull) gi.cvar_set("g_bfgpull", "0");
	//	if (g_bfgslide) gi.cvar_set("g_bfgslide", "1");
	//	if (g_autohaste) gi.cvar_set("g_autohaste", "0");

		// Reset precaching system but don't actually clear cached assets
		// (they persist in engine memory across resets)
	g_full_precache_done = false;

	g_wave_sound_cursor = NUM_WAVE_SOUNDS;
	g_start_sound_cursor = NUM_START_SOUNDS;

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
// --- Timer Trigger Reasons ---
enum class TimerTriggerReason
{
	None,
	MaxMonsters,    // Few monsters remaining (by count)
	LowPercentage,  // Low percentage of wave remaining
	PostDeployment, // Post-deployment trigger
	BothConditions  // Both max monsters and low percentage met
};

struct WaveConditionContext
{
	const gtime_t currentTime;
	const int32_t liveMonsters;
	const int32_t currentLevel;
	const float remainingPercentage;
	const bool isBossWaveActive;
	const horde::MapSize& mapSize;
	const ConditionParams& params;
};

// --- Forward declarations for new helper functions ---
static void StartConditionalTimer(const WaveConditionContext& ctx);
static void ApplyAggressiveTimeReduction(const WaveConditionContext& ctx);
static void UpdateTimeAcceleration(const WaveConditionContext& ctx);
static void IssueTimeWarnings();

// This is the new main function that orchestrates the wave end checks.
static bool CheckRemainingMonstersCondition(const horde::MapSize& mapSize, WaveEndReason& reason)
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
		.params = g_lastParams };

	// --- 2. Immediate Win Condition (100% Elimination Required) ---
	// Wave ends ONLY when all monsters are dead (no early advancement)
	if (allowWaveAdvance || GetStroggsNum() == 0)
	{
		if (GetStroggsNum() == 0)
		{
			reason = WaveEndReason::AllMonstersDead;
			ResetWaveAdvanceState();
			ResetStroggCleanup();
			return true;
		}
	}

	// --- 2b. Strogg Cleanup System ---
	// When spawning is done and few monsters remain, start cleaning up stuck monsters
	// This prevents the game from getting stuck while still requiring 100% elimination
	using namespace HordeConstants;
	const bool spawningComplete = (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters <= 0);
	const bool fewMonstersRemain = (ctx.liveMonsters > 0 && ctx.liveMonsters <= STROGG_THRESHOLD);

	if (spawningComplete && fewMonstersRemain)
	{
		// Start strogg cleanup if not already active
		if (!g_horde_local.stroggCleanupActive)
		{
			g_horde_local.stroggCleanupActive = true;
			g_horde_local.stroggCleanupStartTime = ctx.currentTime;
			g_horde_local.lastStroggKillTime = ctx.currentTime;
			if (developer->integer)
			{
				gi.Com_PrintFmt("STROGG CLEANUP: Started. {} monsters remain. Grace period: {:.0f}s\n",
					ctx.liveMonsters, STROGG_GRACE_PERIOD.seconds());
			}
		}

		// After grace period, start force-killing stuck monsters
		const gtime_t timeSinceCleanupStart = ctx.currentTime - g_horde_local.stroggCleanupStartTime;
		if (timeSinceCleanupStart >= STROGG_GRACE_PERIOD)
		{
			// Kill one stuck monster per interval
			if (ctx.currentTime >= g_horde_local.lastStroggKillTime + STROGG_FORCE_KILL_INTERVAL)
			{
				if (ForceKillMostStuckMonster())
				{
					g_horde_local.lastStroggKillTime = ctx.currentTime;
				}
			}
		}
	}
	else if (g_horde_local.stroggCleanupActive && !fewMonstersRemain)
	{
		// Reset cleanup if monsters increased (new spawns, etc.)
		ResetStroggCleanup();
	}

	// --- 3. Absolute Failsafe Timer ---
	// A hard time limit for the wave, regardless of other conditions.
	if (ctx.currentTime >= g_independent_timer_start + ctx.params.independentTimeThreshold)
	{
		if (DeveloperSuppressesWaveAutoAdvance())
			return false;

		reason = WaveEndReason::TimeLimitReached;
		if (developer->integer)
		{
			gi.Com_PrintFmt("Wave ended: Independent time limit reached ({:.1f}s).\n",
				ctx.params.independentTimeThreshold.seconds());
		}
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

	// Update time acceleration smoothly
	UpdateTimeAcceleration(ctx);

	// Apply smooth timer reduction for post-deployment (when timer was set higher than target)
	if (g_horde_local.conditionTriggered && next_wave_message_sent)
	{
		const gtime_t remaining = (g_horde_local.waveEndTime > ctx.currentTime) ? (g_horde_local.waveEndTime - ctx.currentTime) : 0_sec;
		const gtime_t target_time = g_horde_local.conditionTimeThreshold; // The original target time (e.g., 30s)

		if (remaining > target_time * 1.2f) // Only if significantly above target
		{
			// Smoothly reduce timer toward target over time
			const float reduction_rate = 1.5f; // Faster reduction for post-deployment
			const gtime_t reduction_amount = (remaining - target_time) * (reduction_rate * gi.frame_time_s);
			g_horde_local.waveEndTime -= reduction_amount;
		}
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
		if (DeveloperSuppressesWaveAutoAdvance())
			return false;

		reason = WaveEndReason::MonstersRemaining;
		if (developer->integer)
			// gi.Com_PrintFmt("Wave ended: Conditional timer expired. Live: {}, Queued: {}.\n", ctx.liveMonsters, g_horde_local.queued_monsters);
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
			if (DeveloperSuppressesWaveAutoAdvance())
				return false;

			reason = WaveEndReason::MonstersRemaining;
			// if (developer->integer)
			// 	gi.Com_PrintFmt("Wave ended: High level, few monsters, 70%% of conditional timer elapsed.\n");
			return true;
		}
	}

	// If no end condition was met, the wave continues.
	return false;
}

// Helper to start the conditional "mop-up" timer.
static void StartConditionalTimer(const WaveConditionContext& ctx)
{
	// Determine if a trigger condition is met.
	const bool maxMonstersReached = !next_wave_message_sent && (ctx.liveMonsters <= ctx.params.maxMonsters);
	const bool lowPercentageReached = !next_wave_message_sent && (ctx.remainingPercentage <= ctx.params.lowPercentageThreshold);
	const bool postDeploymentTrigger = next_wave_message_sent;

	if (!maxMonstersReached && !lowPercentageReached && !postDeploymentTrigger)
	{
		return; // No trigger, do nothing.
	}

	// Determine trigger reason for better tracking and messaging
	TimerTriggerReason trigger_reason = TimerTriggerReason::None;
	if (postDeploymentTrigger)
	{
		trigger_reason = TimerTriggerReason::PostDeployment;
	}
	else if (maxMonstersReached && lowPercentageReached)
	{
		trigger_reason = TimerTriggerReason::BothConditions;
	}
	else if (maxMonstersReached)
	{
		trigger_reason = TimerTriggerReason::MaxMonsters;
	}
	else
	{
		trigger_reason = TimerTriggerReason::LowPercentage;
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

	// REMOVED: Duplicate high-level reduction logic - now handled by ApplyAggressiveTimeReduction

	// Apply smooth time acceleration for post-deployment to avoid jarring timer jumps
	bool applied_smooth_transition = false;
	if (postDeploymentTrigger && time_threshold < 60_sec)
	{
		// Calculate appropriate acceleration based on how aggressive the timer reduction is
		const gtime_t independent_time_remaining = (g_independent_timer_start + ctx.params.independentTimeThreshold) - ctx.currentTime;

		if (independent_time_remaining > time_threshold * 2.0f)
		{
			// There's a LOT of independent timer left, use smooth transition instead of instant jump
			// Set the timer to a longer initial value and let ApplyAggressiveTimeReduction reduce it smoothly
			const gtime_t smooth_start_time = time_threshold * 3.0f; // Start 3x longer, will reduce smoothly
			g_horde_local.waveEndTime = ctx.currentTime + std::min(smooth_start_time, independent_time_remaining * 0.5f);

			// Set acceleration target
			const float time_ratio = time_threshold.seconds() / independent_time_remaining.seconds();
			const float target_accel = std::clamp(1.5f / time_ratio, 1.5f, 2.5f);

			g_horde_local.targetTimeAcceleration = target_accel;
			g_horde_local.accelerationStartTime = ctx.currentTime;
			g_horde_local.accelerationDuration = 3_sec; // Slightly longer transition for deployment

			applied_smooth_transition = true;

			// if (developer->integer)
			// {
			// 	gi.Com_PrintFmt("Post-deployment smooth transition: {:.1f}x accel, timer: {:.1f}s → {:.1f}s (independent: {:.1f}s)\n",
			// 					target_accel, g_horde_local.waveEndTime.seconds() - ctx.currentTime.seconds(),
			// 					time_threshold.seconds(), independent_time_remaining.seconds());
			// }
		}
	}

	// Normal timer set if smooth transition not applied
	if (!applied_smooth_transition)
	{
		g_horde_local.waveEndTime = ctx.currentTime + time_threshold;
	}

	// Send contextual player messages based on trigger type
	switch (trigger_reason)
	{
	case TimerTriggerReason::MaxMonsters:
		gi.LocBroadcast_Print(PRINT_HIGH, "Few enemies remain - finish them quickly!\n");
		break;
	case TimerTriggerReason::LowPercentage:
		gi.LocBroadcast_Print(PRINT_HIGH, "Nearly there - eliminate the stroggs!\n");
		break;
	case TimerTriggerReason::PostDeployment:
		// Message already sent when wave deploys, don't spam
		break;
	case TimerTriggerReason::BothConditions:
		gi.LocBroadcast_Print(PRINT_HIGH, "Mop-up time - clear the rest!\n");
		break;
	default:
		break;
	}

	// Debug output
	if (developer->integer)
	{
		const char* trigger_name = "Unknown";
		switch (trigger_reason)
		{
		case TimerTriggerReason::MaxMonsters: trigger_name = "MaxMonsters"; break;
		case TimerTriggerReason::LowPercentage: trigger_name = "LowPercentage"; break;
		case TimerTriggerReason::PostDeployment: trigger_name = "Post-Deploy"; break;
		case TimerTriggerReason::BothConditions: trigger_name = "Both Conditions"; break;
		default: break;
		}
		gi.Com_PrintFmt("Conditional timer started ({:.1f}s). Trigger: {}. Queue: {}.\n",
			time_threshold.seconds(), trigger_name, g_horde_local.queued_monsters);
	}
}

// --- Update Time Acceleration (smooth interpolation) ---
static void UpdateTimeAcceleration(const WaveConditionContext& ctx)
{
	// Update interpolation of time acceleration
	if (std::abs(g_horde_local.timeAcceleration - g_horde_local.targetTimeAcceleration) > 0.01f)
	{
		const gtime_t elapsed = ctx.currentTime - g_horde_local.accelerationStartTime;
		const float t = std::clamp(elapsed.seconds() / g_horde_local.accelerationDuration.seconds(), 0.0f, 1.0f);

		// Exponential smoothing (feels more natural)
		const float smooth_rate = 5.0f; // Higher = faster convergence
		const float delta = (g_horde_local.targetTimeAcceleration - g_horde_local.timeAcceleration) * smooth_rate * gi.frame_time_s;
		g_horde_local.timeAcceleration += delta;

		// Snap to target when close enough or time exceeded
		if (t >= 1.0f || std::abs(g_horde_local.timeAcceleration - g_horde_local.targetTimeAcceleration) < 0.01f)
		{
			g_horde_local.timeAcceleration = g_horde_local.targetTimeAcceleration;
		}
	}
}

// Helper to apply the aggressive time reduction when few monsters are left.
static void ApplyAggressiveTimeReduction(const WaveConditionContext& ctx)
{
	// This logic only applies if few monsters are left alive and in the queue.
	if (ctx.liveMonsters > HordeConstants::MONSTERS_FOR_AGGRESSIVE_REDUCTION || g_horde_local.queued_monsters >= 3)
	{
		// Reset acceleration if conditions no longer met
		if (g_horde_local.targetTimeAcceleration > 1.0f)
		{
			g_horde_local.targetTimeAcceleration = 1.0f;
			g_horde_local.accelerationStartTime = ctx.currentTime;
		}
		return;
	}

	// --- Calculate target time acceleration factor ---
	float target_acceleration = 1.5f; // Base acceleration

	// Increase acceleration based on how few monsters remain
	if (ctx.liveMonsters <= 2)
		target_acceleration = 2.5f;
	else if (ctx.liveMonsters <= 4)
		target_acceleration = 2.0f;

	// Reduce acceleration for boss waves (more time to enjoy the fight)
	if (ctx.isBossWaveActive && boss_spawned_for_wave)
	{
		target_acceleration *= 0.7f; // Less aggressive for bosses
	}
	else
	{
		// High level waves: increase acceleration slightly
		if (ctx.currentLevel >= 15)
		{
			float level_bonus = std::min((ctx.currentLevel - 15) * 0.05f, 0.3f);
			target_acceleration *= (1.0f + level_bonus);
		}
	}

	// Apply map size adjustment (smaller maps = less acceleration needed)
	if (ctx.mapSize.isSmallMap)
		target_acceleration *= 0.85f;
	else if (ctx.mapSize.isMediumMap)
		target_acceleration *= 0.92f;

	// Cap maximum acceleration
	target_acceleration = std::min(target_acceleration, 3.0f);

	// --- Start smooth acceleration if not already at target ---
	if (std::abs(g_horde_local.targetTimeAcceleration - target_acceleration) > 0.1f)
	{
		g_horde_local.targetTimeAcceleration = target_acceleration;
		g_horde_local.accelerationStartTime = ctx.currentTime;
		g_horde_local.accelerationDuration = 2_sec; // 2 second smooth transition
	}

	// --- Optional: Also apply gentle timer reduction (hybrid approach) ---
	// Calculate a reasonable time limit
	float base_time = 8.0f + (ctx.liveMonsters * 1.5f);

	if (ctx.isBossWaveActive && boss_spawned_for_wave)
		base_time *= 1.5f;

	// Gentle reduction over time (30% reduction interpolated over 2 seconds)
	const gtime_t remaining = (g_horde_local.waveEndTime > ctx.currentTime) ? (g_horde_local.waveEndTime - ctx.currentTime) : 0_sec;
	const gtime_t desired_time = gtime_t::from_sec(std::max(5.0f, base_time));

	if (remaining > desired_time && remaining > 5_sec)
	{
		// Smoothly reduce timer by interpolating toward desired time
		const float reduction_rate = 0.7f; // 70% of the difference per second
		const gtime_t reduction_amount = (remaining - desired_time) * (reduction_rate * gi.frame_time_s);
		g_horde_local.waveEndTime -= reduction_amount;
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
	g_horde_local.zero_monster_deployment_start = 0_sec;

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
	const auto& players = active_players();
	return std::count_if(players.begin(), players.end(),
		[](const edict_t* const player)
		{
			return player->client && player->client->resp.ctf_team == CTF_TEAM1;
		});
}

inline int8_t GetNumSpectPlayers()
{
	const auto& players = active_players();
	return std::count_if(players.begin(), players.end(),
		[](const edict_t* const player)
		{
			// Only count human spectators, not bot spectators
			// Bots shouldn't be spectators, but if they are, we shouldn't spawn more bots for them
			return ClientIsSpectating(player->client) && !(player->svflags & SVF_BOT);
		});
}

// Implementación de DisplayWaveMessage
static void DisplayWaveMessage(gtime_t duration = 5_sec)
{
	static const std::array<const char*, 7> messages = {
		"Horde Menu available upon opening Inventory or using TURTLE on POWERUP WHEEL\n\nMAKE THEM PAY!\n",
		"Welcome to Hell.\n\nUse FlipOff <Key> looking at walls to spawn lasers (cost: 25 cells)\n",
		"Teslas/Traps can now be placed on walls and ceilings!\n\nUse them wisely!\n",
		"Adrenalines will improve traps/teslas duration\n",
		"Improved Traps!\n\nTraps are ready again after 5sec of eating a strogg!\n",
		"Check Menu -> Upgrading for the new stuff!\n",
		"You can choose your own path on bonuses/upgrades, check Horde Menu!\n" };

	// Usar distribución uniforme con mt_rand
	std::uniform_int_distribution<size_t> dist(0, messages.size() - 1);
	const size_t choice = dist(mt_rand);
	AppendHordeMessage(messages[choice], duration);
}

static void HandleWaveCleanupMessage(const horde::MapSize& mapSize)
{
	// Obtener el número de jugadores humanos
	const int8_t numHumanPlayers = GetNumHumanPlayers();

	// Progresión de dificultad basada en número de ola (endless horde design)
	// Waves 1-14: Chaotic mode (fast-paced continuous spawning)
	if (current_wave_level <= 14)
	{
		gi.cvar_set("g_insane", "0");
		// Activar chaotic2 si es mapa pequeño Y hay 2+ jugadores, sino chaotic1
		gi.cvar_set("g_chaotic", (mapSize.isSmallMap && numHumanPlayers >= 2) ? "2" : "1");
	}
	// Waves 15-26: Insane 1 (moderate spawn boost)
	else if (current_wave_level <= 26)
	{
		gi.cvar_set("g_insane", "1");
		gi.cvar_set("g_chaotic", "0");
	}
	// Waves 27+: Insane 2 (maximum spawn boost)
	else
	{
		gi.cvar_set("g_insane", "2");
		gi.cvar_set("g_chaotic", "0");
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
	for (auto* player : active_players())
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

void InitializeWaveSystem()
{
	PrecacheWaveSounds();
}

static void SetMonsterArmor(edict_t* monster);
void SetNextMonsterSpawnTime(const horde::MapSize& mapSize);

// =======================================================================
// This version fixes the bug preventing alternative spawns
// =======================================================================
static inline bool IsPositionWithinSpawnBounds(const vec3_t& position, const vec3_t& monster_mins, const vec3_t& monster_maxs)
{
	if (!g_spawn_world_bounds_valid)
	{
		return true; // If bounds are not initialized yet, do not hard-block spawning.
	}

	// Small tolerance to avoid false negatives on edge cases near brush boundaries.
	constexpr float EDGE_TOLERANCE = 96.0f;
	const vec3_t min_bound = g_spawn_world_mins - vec3_t{ EDGE_TOLERANCE, EDGE_TOLERANCE, EDGE_TOLERANCE };
	const vec3_t max_bound = g_spawn_world_maxs + vec3_t{ EDGE_TOLERANCE, EDGE_TOLERANCE, EDGE_TOLERANCE };

	const vec3_t abs_min = position + monster_mins;
	const vec3_t abs_max = position + monster_maxs;

	return (abs_min.x >= min_bound.x && abs_max.x <= max_bound.x) &&
		(abs_min.y >= min_bound.y && abs_max.y <= max_bound.y) &&
		(abs_min.z >= min_bound.z && abs_max.z <= max_bound.z);
}

[[nodiscard]] // FIXED: Clean implementation without historical comments, clear return struct
PositionValidationResult IsPositionPhysicallyValid(const vec3_t& position, const vec3_t& monster_mins, const vec3_t& monster_maxs, const bool is_flying)
{
	PositionValidationResult result = { false, position };

	if (!is_valid_vector(position)) return result;
	if (!IsPositionWithinSpawnBounds(position, monster_mins, monster_maxs)) return result;

	const contents_t point_contents = gi.pointcontents(position);
	if (point_contents & MASK_SOLID) return result;

	if (!is_flying && (point_contents & (CONTENTS_LAVA | CONTENTS_SLIME))) {
		return result;
	}

	if (!is_flying && (point_contents & CONTENTS_WATER)) {
		vec3_t head_pos = position;
		head_pos.z += monster_maxs.z - monster_mins.z;
		if (gi.pointcontents(head_pos) & CONTENTS_WATER) {
			return result;
		}
	}

	// Check if sky is too close above (reject positions near ceiling/sky)
	{
		vec3_t sky_check_start = position;
		sky_check_start.z += monster_maxs.z - monster_mins.z; // Start from monster's head
		vec3_t sky_check_end = sky_check_start;
		// Flying monsters: check 256 units up, ground monsters: check 128 units up
		sky_check_end.z += is_flying ? 256.0f : 128.0f;

		// Use bbox trace instead of traceline for more accurate sky detection
		trace_t sky_trace = gi.trace(sky_check_start, monster_mins, monster_maxs, sky_check_end, nullptr, MASK_SOLID);
		if (sky_trace.surface && (sky_trace.surface->flags & SURF_SKY)) {
			return result; // Too close to sky - reject position
		}
	}

	// Check if spawning above sky brush (in the void) - trace far down
	// For flying monsters, ensure there's ground within 2000 units
	{
		vec3_t down_check_start = position;
		down_check_start.z += monster_mins.z; // Start from feet
		vec3_t down_check_end = down_check_start;
		down_check_end.z -= (is_flying ? 2000.0f : 2048.0f);

		trace_t down_trace = gi.trace(down_check_start, vec3_origin, vec3_origin, down_check_end, nullptr, MASK_SOLID);

		// Reject if hitting sky surface
		if (down_trace.surface && (down_trace.surface->flags & SURF_SKY)) {
			return result; // Spawning above sky - reject position
		}

		// For flying monsters, ensure there's actually ground below (not in the void)
		if (is_flying && down_trace.fraction >= 1.0f) {
			return result; // No ground within 2000 units - reject position
		}
	}

	// Calculate bbox size to determine if we need extra clearance
	const float bbox_radius = std::max({ monster_maxs.x - monster_mins.x, monster_maxs.y - monster_mins.y }) * 0.5f;
	const bool is_large_monster = bbox_radius > 32.0f; // Large monsters (GM Arachnid, Tanks, etc.)

	// Check if the volume is occupied by world geometry or other monsters
	// For large monsters, add 8 units of padding to prevent tight spawns
	vec3_t check_mins = monster_mins;
	vec3_t check_maxs = monster_maxs;
	if (is_large_monster) {
		check_mins.x -= 8.0f;
		check_mins.y -= 8.0f;
		check_maxs.x += 8.0f;
		check_maxs.y += 8.0f;
	}

	const trace_t trace = gi.trace(position, check_mins, check_maxs, position, nullptr, MASK_MONSTERSOLID);
	if (trace.startsolid) {
		return result;
	}

	// For ground units, drop to floor
	vec3_t final_pos = position;
	if (!is_flying)
	{
		vec3_t end = final_pos;
		end.z -= 1024;
		const trace_t ground_trace = gi.trace(final_pos, monster_mins, monster_maxs, end, nullptr, MASK_SOLID);

		if (ground_trace.fraction == 1.0f) {
			return result;
		}

		if (!M_droptofloor_generic(final_pos, monster_mins, monster_maxs, false, nullptr, MASK_SOLID, false))
		{
			return result;
		}
	}

	result.is_valid = true;
	result.adjusted_position = final_pos;
	return result;
}

// helper function
static edict_t* FindBestPlayerTargetForTeleport()
{
	edict_t* target_player = nullptr;
	int max_spree = -1;
	int64_t max_damage = -1;

	// First pass: Find the player with the highest spree or damage
	for (auto* player : active_players_no_spect())
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
		// Heap optimization: Changed from std::vector to std::array (compile-time, zero heap allocation)
		std::array<edict_t*, 32> active{};
		int active_count = 0;
		for (auto* p : active_players_no_spect())
		{
			if (active_count < 32) {
				active[active_count++] = p;
			}
		}
		if (active_count > 0)
		{
			target_player = active[irandom(active_count)];
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
			if (developer->integer > 1)
				gi.Com_PrintFmt("FindSafeTeleportDestination: No valid player target found.\n");
			return nullptr;
		}
	}

	// --- 2. Get Monster Properties ---
	const bool can_monster_fly = IsFlying(static_cast<horde::MonsterTypeID>(self->monsterinfo.monster_type_id));

	// 25% chance to require out-of-visibility position for this teleport
	const bool require_out_of_visibility = frandom() < HordeConstants::OUT_OF_VISIBILITY_CHANCE;

	// --- 3. Search for a Suitable Spawn Point ---
	edict_t* best_spot = nullptr;
	float best_score = -1.0f;

	// Use increased minimum teleport distance from constants
	const float min_dist_sq = HordeConstants::TELEPORT_MIN_DIST_FROM_PLAYER * HordeConstants::TELEPORT_MIN_DIST_FROM_PLAYER;
	constexpr float MAX_DIST_SQ = 1200.0f * 1200.0f;

	// --- PERFORMANCE FIX: Use spatial index for O(1) nearby spawn point lookup ---
	constexpr float SEARCH_RADIUS = 1500.0f;
	boost::container::small_vector<edict_t*, 32> nearby_spawn_points;
	HordePerf::g_spawn_spatial_index.GetNearbySpawnPoints(target_player->s.origin, SEARCH_RADIUS, nearby_spawn_points);

	auto consider_candidates = [&](auto& candidates) -> bool
		{
			edict_t* local_best_spot = nullptr;
			float local_best_score = -1.0f;

			for (edict_t* spawn_point : candidates)
			{
				// Spawn points from spatial index are already validated as info_player_deathmatch
				if (!spawn_point || !spawn_point->inuse)
					continue;
				if (!is_valid_vector(spawn_point->s.origin))
					continue;

				// --- A. Filter for Valid Spawn Points ---
				const uint16_t index = GetSpawnPointIndexSafe(spawn_point);
				if (index == 0xFFFF) [[unlikely]]
					continue;

				if (level.time < g_spawn_system.spawn_points_data.teleport_cooldown[index] || IsSpawnPointOccupied(spawn_point))
				{
					continue;
				}

				const bool spawn_is_flying = (spawn_point->style == 1);
				if (!can_monster_fly && spawn_is_flying)
				{
					continue; // Ground monsters must never use style 1 points.
				}

				// For flying monsters using non-style-1 fallback points, ensure nearby ground.
				// Dedicated style 1 lanes are intentionally exempt.
				if (can_monster_fly && !spawn_is_flying)
				{
					vec3_t ground_check_start = spawn_point->s.origin;
					vec3_t ground_check_end = ground_check_start;
					ground_check_end.z -= 2000.0f;

					trace_t ground_trace = gi.traceline(ground_check_start, ground_check_end, spawn_point, MASK_SOLID);
					// If no ground found or hit sky, skip this spawn point
					if (ground_trace.fraction >= 1.0f || (ground_trace.surface && (ground_trace.surface->flags & SURF_SKY)))
					{
						continue;
					}
				}

				// --- B. Score the Validated Spawn Point ---
				float score = 100.0f;
				float dist_sq = (spawn_point->s.origin - target_player->s.origin).lengthSquared();

				// Use PVS check - catches positions visible from any angle, not just direct line-of-sight
				// The 'false' parameter means don't check through portals (stricter visibility)
				const bool is_in_player_pvs = gi.inPVS(spawn_point->s.origin, target_player->s.origin, false);

				// If we require out-of-visibility and position is in PVS, skip it
				if (require_out_of_visibility && is_in_player_pvs)
				{
					continue;
				}

				// Bonus for being in optimal distance range (400-1200 units)
				if (dist_sq > min_dist_sq && dist_sq < MAX_DIST_SQ)
				{
					score += 100.0f;
				}

				// Bonus for positions not in player's PVS
				if (!is_in_player_pvs)
				{
					score += 150.0f;
				}

				// Penalty for being too close to player (use constant for consistency)
				if (dist_sq < min_dist_sq)
				{
					score -= 200.0f;
				}

				score += frandom() * 25.0f;

				// --- C. Update Best Candidate ---
				if (score > local_best_score)
				{
					local_best_score = score;
					local_best_spot = spawn_point;
				}
			}

			if (local_best_spot)
			{
				best_spot = local_best_spot;
				best_score = local_best_score;
				return true;
			}

			return false;
		};

	// Flying monsters may use style-1 lanes, but those lanes do not get extra priority.
	if (!consider_candidates(nearby_spawn_points))
	{
		// Broaden search across all known DM spawn points.
		// Ground monsters still keep style 1 excluded via the filter above.
		consider_candidates(g_spawn_point_list);
	}

	if (developer->integer > 1 && best_spot)
	{
		gi.Com_PrintFmt("FindSafeTeleportDestination: Selected spot at {} with score {:.1f} (out-of-vis required: {})\n",
			best_spot->s.origin, best_score, require_out_of_visibility ? "yes" : "no");
	}
	else if (developer->integer > 1 && !best_spot)
	{
		gi.Com_PrintFmt("FindSafeTeleportDestination: No suitable teleport spot found.\n");
	}

	return best_spot;
}

// --- Strogg Cleanup System (100% Wave Elimination) ---
// Finds and force-kills stuck monsters when the wave is winding down
// Returns true if a monster was killed, false otherwise
static bool ForceKillMostStuckMonster()
{
	using namespace HordeConstants;

	edict_t* most_stuck = nullptr;
	int highest_stuck_score = 0;

	for (edict_t* ent : active_monsters())
	{
		if (!ent || !ent->inuse || ent->deadflag || ent->health <= 0)
			continue;

		// Skip bosses - let them be killed normally
		if (ent->monsterinfo.IS_BOSS)
			continue;

		int stuck_score = 0;

		// Critical: Monster is in the void (below map)
		if (ent->s.origin.z < VOID_Z_THRESHOLD)
		{
			stuck_score += 1000; // Highest priority
		}

		// Check if stuck in solid geometry
		if (gi.trace(ent->s.origin, ent->mins, ent->maxs, ent->s.origin, ent, MASK_SOLID).startsolid)
		{
			stuck_score += 500;
		}

		// Check if on/near sky (outside map)
		vec3_t sky_check = ent->s.origin;
		sky_check.z += ent->maxs.z;
		vec3_t sky_end = sky_check;
		sky_end.z += 64.0f;
		trace_t sky_trace = gi.trace(sky_check, vec3_origin, vec3_origin, sky_end, ent, MASK_SOLID);
		if (sky_trace.surface && (sky_trace.surface->flags & SURF_SKY))
		{
			stuck_score += 400;
		}

		// Check if unreachable (no visibility to any player)
		bool visible_to_player = false;
		for (auto* player : active_players_no_spect())
		{
			if (player && player->inuse && visible(ent, player, false))
			{
				visible_to_player = true;
				break;
			}
		}
		if (!visible_to_player)
		{
			stuck_score += 200;
			// Even higher if never been visible
			if (!ent->monsterinfo.was_ever_visible_to_player)
				stuck_score += 100;
		}

		// Flying monsters that can't reach players are likely stuck
		if ((ent->flags & FL_FLY) && !visible_to_player)
		{
			stuck_score += 150;
		}

		// Add score based on how long monster has been unreachable
		if (ent->monsterinfo.unreachable_start_time > 0_sec)
		{
			gtime_t unreachable_time = level.time - ent->monsterinfo.unreachable_start_time;
			stuck_score += static_cast<int>(unreachable_time.seconds() * 10);
		}

		if (stuck_score > highest_stuck_score)
		{
			highest_stuck_score = stuck_score;
			most_stuck = ent;
		}
	}

	// Kill the most stuck monster if found
	if (most_stuck && highest_stuck_score > 0)
	{
		if (developer->integer)
		{
			gi.Com_PrintFmt("STROGG CLEANUP: Force-killing {} (stuck score: {}, pos: [{:.0f}, {:.0f}, {:.0f}])\n",
				most_stuck->classname ? most_stuck->classname : "unknown",
				highest_stuck_score,
				most_stuck->s.origin.x, most_stuck->s.origin.y, most_stuck->s.origin.z);
		}

		// Use T_Damage to kill the monster properly - this ensures:
		// 1. Monster count decreases correctly
		// 2. Death callbacks fire (items drop, effects trigger)
		// 3. All game state updates properly
		T_Damage(most_stuck, world, world, vec3_origin, most_stuck->s.origin, vec3_origin,
			most_stuck->health + 1000, 0, DAMAGE_NO_PROTECTION, MOD_UNKNOWN);

		return true;
	}

	return false;
}

// Resets strogg cleanup state for new wave
static void ResetStroggCleanup()
{
	g_horde_local.stroggCleanupActive = false;
	g_horde_local.stroggCleanupStartTime = 0_sec;
	g_horde_local.lastStroggKillTime = 0_sec;
}

bool CheckAndTeleportStuckMonster(edict_t* self)
{
	PROFILE_SCOPE("CheckAndTeleportStuckMonster");

	// --- 1. Initial Validation ---
	if (level.intermissiontime || !self || !self->inuse || self->deadflag || self->monsterinfo.IS_BOSS || !g_horde->integer || self->monsterinfo.isfriendlyspawn)
		return false;

	// Periodic check rate limiting - faster checks for all monsters to catch unreachable positions
	if (level.time < self->monsterinfo.stuck_check_time)
		return false;

	// All monsters check frequently (4-6 sec) to catch unreachable roofs, sky areas, enclosed spaces, etc.
	self->monsterinfo.stuck_check_time = level.time + random_time(4.0_sec, 6.0_sec);

	// Skip certain monster types
	if (horde::IsMonsterType(self, horde::MonsterTypeID::MISC_INSANE) || horde::IsMonsterType(self, horde::MonsterTypeID::SENTRYGUN) || (horde::IsMonsterType(self, horde::MonsterTypeID::TURRET) || (horde::IsMonsterType(self, horde::MonsterTypeID::FLIPPER))))
		return false;

	// Don't teleport jumping monsters
	if (IsMonsterJumping(self))
	{
		self->teleport_time = level.time + 1.5_sec;
		return false;
	}

	// Teleport cooldown
	if (self->teleport_time > level.time)
		return false;

	// --- 2. Global Rate Limiting (ADAPTIVE BASED ON REMAINING MONSTERS) ---
	const int32_t remaining_monsters = GetStroggsNum();

	if (level.time > HordeConstants::g_teleport_rate_reset_time)
	{
		HordeConstants::g_teleport_rate_count = 0;
		HordeConstants::g_teleport_rate_reset_time = level.time + HordeConstants::GLOBAL_TELEPORT_RESET_INTERVAL;
	}

	// ADAPTIVE RATE LIMITING: Much more aggressive when few monsters remain
	int max_teleports;
	if (remaining_monsters <= 3)
	{
		// Very aggressive - allow up to 10 teleports when 3 or fewer monsters
		max_teleports = 10;
	}
	else if (remaining_monsters <= 6)
	{
		// Aggressive - allow up to 6 teleports when 4-6 monsters remain
		max_teleports = 6;
	}
	else
	{
		// Normal rate limiting for higher monster counts
		max_teleports = HordeConstants::MAX_TELEPORTS_PER_INTERVAL + ((g_insane->integer || g_chaotic->integer) ? 1 : 0);
	}

	if (HordeConstants::g_teleport_rate_count >= max_teleports)
	{
		return false;
	}

	// --- 3. Determine if Teleport is Needed ---
	bool needs_teleport = false;
	const char* reason_str = "Unknown";

	// Critical: Stuck in solid geometry
	if (gi.trace(self->s.origin, self->mins, self->maxs, self->s.origin, self, MASK_SOLID).startsolid)
	{
		needs_teleport = true;
		reason_str = "Stuck in Geometry";
	}
	// Critical: Drowning without visible enemy
	else if (self->waterlevel > 0 && !(self->enemy && visible(self, self->enemy, false)))
	{
		needs_teleport = true;
		reason_str = "Drowning";
	}
	// Critical: Flying monster blocked against walls for too long (counts as movement otherwise)
	if (!needs_teleport && (self->flags & FL_FLY))
	{
		if (self->monsterinfo.fly_wall_stuck_time > 0_ms &&
			level.time > self->monsterinfo.fly_wall_stuck_time + HordeConstants::FLY_WALL_STUCK_TELEPORT_TIME)
		{
			needs_teleport = true;
			reason_str = "Flyer Wall-Stuck";
		}
	}
	// Critical: Too close to sky (unreachable position) - check ALL monsters, not just flying
	if (!needs_teleport)
	{
		vec3_t sky_check_start = self->s.origin;
		sky_check_start.z += self->maxs.z; // Start from top of monster bbox
		vec3_t sky_check_end = sky_check_start;
		sky_check_end.z += 128.0f; // Check 128 units up

		// Use bbox trace instead of traceline for more accurate sky detection
		trace_t sky_trace = gi.trace(sky_check_start, self->mins, self->maxs, sky_check_end, self, MASK_SOLID);
		if (sky_trace.surface && (sky_trace.surface->flags & SURF_SKY))
		{
			needs_teleport = true;
			reason_str = "Too Close to Sky";
		}
	}
	// Critical: On/above sky surface (instant teleport) - check all monsters
	// This catches monsters standing on ground outside the map with sky texture below
	if (!needs_teleport)
	{
		vec3_t ground_check_start = self->s.origin;
		ground_check_start.z += self->mins.z; // Start from feet
		vec3_t ground_check_end = ground_check_start;
		ground_check_end.z -= 1024.0f; // Check 1024 units down to detect sky below ground

		trace_t ground_trace = gi.trace(ground_check_start, vec3_origin, vec3_origin, ground_check_end, self, MASK_SOLID);
		if (ground_trace.surface && (ground_trace.surface->flags & SURF_SKY))
		{
			needs_teleport = true;
			reason_str = "On Sky Surface";
		}
	}
	// Critical: No path AND no visibility to any player for 3 seconds
	if (!needs_teleport)
	{
		bool has_path_or_visibility = false;

		// Check if any player can see this monster or has clear line of sight
		for (auto* player : active_players_no_spect())
		{
			if (!player || !player->inuse)
				continue;

			// Check visibility (both ways - can monster see player OR can player see monster)
			if (visible(self, player, false) || visible(player, self, false))
			{
				has_path_or_visibility = true;
				break;
			}

			// Check for clear traceline as fallback (simpler path check)
			trace_t path_trace = gi.traceline(self->s.origin, player->s.origin, self, MASK_SOLID);
			if (path_trace.fraction >= 0.95f) // Almost clear path
			{
				has_path_or_visibility = true;
				break;
			}
		}

		if (has_path_or_visibility)
		{
			// Reset timer if path/visibility restored
			self->monsterinfo.unreachable_start_time = 0_sec;
			// Mark that this monster was visible at least once
			self->monsterinfo.was_ever_visible_to_player = true;
			// Reset failsafe teleport count since monster has visibility
			self->monsterinfo.failsafe_teleport_count = 0;
		}
		else
		{
			// Start or continue timer
			if (self->monsterinfo.unreachable_start_time == 0_sec)
			{
				self->monsterinfo.unreachable_start_time = level.time;
			}
			else
			{
				// Use shorter timeout for monsters that were never visible (prioritize them)
				// Use longer grace period for monsters that had visibility then lost it
				// ALSO: Make timeout much shorter when few monsters remain
				gtime_t timeout_duration;
				if (remaining_monsters <= 6)
				{
					// Very aggressive timeout when few monsters remain
					timeout_duration = 0.5_sec; // Teleport almost immediately
				}
				else
				{
					timeout_duration = self->monsterinfo.was_ever_visible_to_player ? 3_sec : 1_sec;
				}

				if (level.time > self->monsterinfo.unreachable_start_time + timeout_duration)
				{
					needs_teleport = true;
					reason_str = self->monsterinfo.was_ever_visible_to_player ?
						"Unreachable (Lost Visibility)" : "Unreachable (Never Visible)";
				}
			}
		}
	}
	// Non-critical checks (only if not already flagged for teleport)
	if (!needs_teleport)
	{
		// Don't check inactivity if currently attacking
		if (self->enemy && self->monsterinfo.attack_finished > level.time)
			return false;

		const horde::MapSize& mapSize = g_horde_local.current_map_size;

		// Check for no enemy timeout
		if (!self->enemy || !self->enemy->inuse)
		{
			if (self->monsterinfo.no_enemy_timeout_start_time == 0_sec)
				self->monsterinfo.no_enemy_timeout_start_time = level.time;

			gtime_t no_enemy_timeout = HordeConstants::GetNoEnemyTimeout(mapSize);
			// Reduce timeout when few monsters remain
			if (remaining_monsters <= 6)
				no_enemy_timeout = 3_sec;

			if (level.time > self->monsterinfo.no_enemy_timeout_start_time + no_enemy_timeout)
			{
				needs_teleport = true;
				reason_str = "No Enemy Timeout";
			}
		}
		else
		{
			self->monsterinfo.no_enemy_timeout_start_time = 0_sec;

			// Check if monster is actively engaged
			const bool monster_can_see_enemy = visible(self, self->enemy, false);
			const bool monster_recently_moved = (self->monsterinfo.bad_move_time > level.time - 2_sec);
			const bool monster_recently_attacked = (self->monsterinfo.attack_finished > level.time - 1_sec);

			// Reset activity timer if monster is active
			if (monster_can_see_enemy || monster_recently_moved || monster_recently_attacked)
			{
				self->monsterinfo.last_activity_time = level.time;
				self->monsterinfo.failsafe_teleport_count = 0; // Monster is engaging, reset teleport count
				return false;
			}

			// Check inactivity timeout (only if last_activity_time was initialized)
			if (self->monsterinfo.last_activity_time > 0_sec && self->max_health > 0)
			{
				gtime_t timeout_duration = (self->health < self->max_health) ?
					HordeConstants::GetDamagedMonsterTimeout(mapSize) :
					HordeConstants::GetNoDamageTimeout(mapSize);

				// Reduce timeout when few monsters remain
				if (remaining_monsters <= 6)
					timeout_duration = 5_sec;

				const char* timeout_reason = (self->health < self->max_health) ?
					"Damaged Monster Inactivity" : "No Damage Timeout (Failsafe)";

				if (level.time > self->monsterinfo.last_activity_time + timeout_duration)
				{
					needs_teleport = true;
					reason_str = timeout_reason;
				}
			}
		}
	}

	if (!needs_teleport)
		return false;

	// --- Check per-monster failsafe teleport limit ---
	// If this monster has been teleported too many times due to failsafe, kill it instead
	if (self->monsterinfo.failsafe_teleport_count >= HordeConstants::MAX_FAILSAFE_TELEPORTS_PER_MONSTER)
	{
		if (developer->integer)
		{
			gi.Com_PrintFmt("[CATS] {} exceeded max failsafe teleports ({}), killing instead\n",
				self->classname, self->monsterinfo.failsafe_teleport_count);
		}
		// Kill the monster - it's unreachable/stuck and keeps triggering failsafe
		T_Damage(self, world, world, vec3_origin, self->s.origin, vec3_origin,
			self->health + 1000, 0, DAMAGE_NO_PROTECTION, MOD_TELEFRAG);
		return false;
	}

	// **PERFORMANCE**: Only print debug in developer mode and limit frequency
	static gtime_t last_debug_print = 0_sec;
	if (developer->integer && (level.time > last_debug_print + 1_sec))
	{
		gi.Com_PrintFmt("[CATS] Trigger for {}: {}. Remaining: {}, MaxTeleports: {}, FailsafeTeleports: {}\n",
			self->classname, reason_str, remaining_monsters, max_teleports, self->monsterinfo.failsafe_teleport_count);
		last_debug_print = level.time;
	}

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
		if (!FindEmergencySpawnPositionNearPlayer(dest_origin, dest_angles, static_cast<horde::MonsterTypeID>(self->monsterinfo.monster_type_id)))
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
			const uint16_t index = GetSpawnPointIndexSafe(used_spawn_point);
			if (index != 0xFFFF && index < g_spawn_system.spawn_points_data.teleport_cooldown.size()) {
				g_spawn_system.spawn_points_data.teleport_cooldown[index] = level.time + HordeConstants::SPAWN_POINT_TELEPORT_COOLDOWN;
			}
			ApplyLongFlyingLaneCooldown(used_spawn_point);

		}
		HordeConstants::g_teleport_rate_count++;
		self->monsterinfo.failsafe_teleport_count++; // Track per-monster teleport count
		self->monsterinfo.was_stuck = false;
		self->monsterinfo.fly_wall_stuck_time = 0_ms;
		// Reset check interval for next stuck check
		self->monsterinfo.stuck_check_time = level.time + random_time(4.0_sec, 6.0_sec);
		self->monsterinfo.no_enemy_timeout_start_time = 0_sec;
		return true;
	}

	return false;
}

// Helper function to select a retaliation-themed monster
static horde::MonsterTypeID PickRetaliationMonsterTypeID(int32_t waveLevel)
{
	WeightedSelection<MonsterTypeInfo> selection;
	selection.clear();

	// Define the desired types for retaliation
	constexpr MonsterWaveType RETALIATION_TYPES = MonsterWaveType::Spawner | MonsterWaveType::Bomber | MonsterWaveType::Special;

	for (size_t i = 0; i < MONSTER_DATA_COUNT; ++i)
	{
		const auto& monsterInfo = monsterTypes[i];
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
void HandleSpawnPhaseAggression(edict_t* monster)
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
			if (g_spawn_system.special_spawn_state.type != SpecialSpawnType::Retaliation &&
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
				}
				else {
					PlayerStats top_player_stats;
					float percentage;
					CalculateTopDamager(top_player_stats, percentage);
					if (top_player_stats.player && top_player_stats.player->client) {
						target_player = top_player_stats.player;
					}
				}

				if (!target_player) {
					// Heap optimization: Changed from std::vector to std::array (compile-time, zero heap allocation)
					std::array<edict_t*, 32> candidates{};
					int candidate_count = 0;
					for (auto* p : active_players_no_spect()) {
						if (candidate_count < 32) {
							candidates[candidate_count++] = p;
						}
					}
					if (candidate_count > 0) {
						target_player = candidates[irandom(candidate_count)];
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

// TryAlternativeSpawnPosition moved to horde_spawning.cpp
// g_horde_phys.h already included at top of file

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
			ApplyLongFlyingLaneCooldown(dest_spot);
			if (developer->integer)
			{
				gi.Com_PrintFmt("FIXED STUCK (Safe): Relocated '{}' to spawn point at {}.\n", self->classname, dest_spot->s.origin);
			}
			return true;
		}
	}

	// If that fails, fall back to the emergency grid search.
	vec3_t emergency_origin, emergency_angles;
	horde::MonsterTypeID typeId = static_cast<horde::MonsterTypeID>(self->monsterinfo.monster_type_id);

	// The target for the emergency spawn is the monster's current enemy, if it has one.
	if (FindEmergencySpawnPositionNearPlayer(emergency_origin, emergency_angles, typeId, self->enemy))
	{
		if (Horde_TeleportMonster(self, emergency_origin, emergency_angles, true, true))
		{
			if (developer->integer)
			{
				gi.Com_PrintFmt("FIXED STUCK (Emergency): Relocated '{}' to {} (bbox: {:.1f}x{:.1f}x{:.1f}).\n",
					self->classname, emergency_origin,
					self->maxs.x - self->mins.x, self->maxs.y - self->mins.y, self->maxs.z - self->mins.z);
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

// Horde_SpawnMonster moved to horde_spawning.cpp
// EmergencySpawnMonster moved to horde_spawning.cpp
// FindEmergencySpawnPositionNearPlayer moved to horde_spawning.cpp

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
	for (auto* player : active_players_no_spect())
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
	if (g_spawn_system.special_spawn_state.type != SpecialSpawnType::None) return; // Another special spawn is already planned

	// Reduced retaliation sizes for better balancing
	int baseSize = mapSize.isSmallMap ? 1 : (mapSize.isBigMap ? 2 : 1); // Reduced: was 1/2/3
	int spreeBonus = (target_player && target_player->client) ? (target_player->client->resp.spree / 12) : 0; // Reduced from /10
	int levelBonus = waveLevel / 18; // Reduced from /15
	int ambushSize = std::min(baseSize + spreeBonus + levelBonus, 3); // Reduced max from 4 to 3

	horde::MonsterTypeID typeId = PickRetaliationMonsterTypeID(waveLevel);
	if (typeId == horde::MonsterTypeID::UNKNOWN) return;

	if (developer->integer) {
		gi.Com_PrintFmt("HORDE: PLANNING Retaliation (Size: {}). Target: {}\n",
			ambushSize, GetPlayerName(target_player));
	}

	g_spawn_system.special_spawn_state.type = SpecialSpawnType::Retaliation;
	g_spawn_system.special_spawn_state.remaining_count = ambushSize;
	g_spawn_system.special_spawn_state.monster_type_id = typeId;
	g_spawn_system.special_spawn_state.champion_chance = 0.6f + (frandom() * 0.25f);
	g_spawn_system.special_spawn_state.target_player = target_player;

	// Retaliation state is now managed through g_spawn_system.special_spawn_state
	// The type was already set above in g_spawn_system.special_spawn_state.type = SpecialSpawnType::Retaliation
	g_horde_retaliation_end_time = level.time + HordeConstants::RETALIATION_DURATION;
}

// This function is called when a random ambush is triggered.
// UPDATED: Smaller ambushes that follow a specific player - feels less intrusive
void TriggerAmbush(const horde::MapSize& mapSize, int32_t waveLevel)
{
	if (g_spawn_system.special_spawn_state.type != SpecialSpawnType::None) return;

	horde::MonsterTypeID monster_typeId_for_ambush = horde::MonsterTypeID::UNKNOWN;
	// Use a simple, robust fallback for picking the monster type
	static const std::array<horde::MonsterTypeID, 5> fallback_types = {
		horde::MonsterTypeID::SOLDIER_LIGHT, horde::MonsterTypeID::SOLDIER,
		horde::MonsterTypeID::INFANTRY, horde::MonsterTypeID::SOLDIER_SS, horde::MonsterTypeID::FLYER
	};
	monster_typeId_for_ambush = random_element(fallback_types);

	// Reduced ambush sizes for better balancing
	const int baseCount = mapSize.isSmallMap ? 1 : (mapSize.isBigMap ? 2 : 1); // Reduced: was 1/2/3
	const int ambushSize = baseCount + (waveLevel >= 25 ? 1 : 0);  // Only +1 at very high waves (was >= 20)

	// Pick a random player to be the target - ambush will follow this player
	edict_t* target_player = nullptr;
	for (uint32_t i = 0; i < game.maxclients; i++) {
		edict_t* player = g_edicts + 1 + i;
		if (player->inuse && player->client && player->health > 0) {
			if (!target_player || frandom() < 0.5f) {
				target_player = player;
			}
		}
	}

	if (developer->integer) {
		gi.Com_PrintFmt("HORDE: PLANNING Ambush (Size: {}) targeting {}. Spawning will be time-sliced.\n",
			ambushSize, target_player ? GetPlayerName(target_player) : "random");
	}

	g_spawn_system.special_spawn_state.type = SpecialSpawnType::Ambush;
	g_spawn_system.special_spawn_state.remaining_count = ambushSize;
	g_spawn_system.special_spawn_state.monster_type_id = monster_typeId_for_ambush;
	g_spawn_system.special_spawn_state.champion_chance = 0.15f;  // Lower champion chance
	g_spawn_system.special_spawn_state.target_player = target_player;  // Follow this player
}

// This single function runs every frame to execute one spawn from the special plan.
static void ExecuteNextSpecialSpawn()
{
	if (g_spawn_system.special_spawn_state.remaining_count <= 0) {
		return;
	}

	// Determine spawn type: 1=Ambush, 2=Retaliation
	int spawn_type = (g_spawn_system.special_spawn_state.type == SpecialSpawnType::Retaliation) ? 2 : 1;

	// EmergencySpawnMonster is correct here as it finds a spot near a player.
	// It will use the specific target if one is set (for retaliation), or find a random one (for ambush).
	if (EmergencySpawnMonster(current_wave_level, g_spawn_system.special_spawn_state.monster_type_id, true, g_spawn_system.special_spawn_state.champion_chance, spawn_type))
	{
		// Success
	}
	else if (developer->integer)
	{
		gi.Com_PrintFmt("Special Spawn FAILED: EmergencySpawnMonster returned false.\n");
	}

	g_spawn_system.special_spawn_state.remaining_count--;

	// If this was the last one, clear the state.
	if (g_spawn_system.special_spawn_state.remaining_count <= 0) {
		g_spawn_system.special_spawn_state.clear();
	}
}

// Forward declarations for helper functions (spawning functions moved to horde_spawning.cpp)
bool CheckHardCapAndLog(int32_t activeMonsters, int32_t hardCap, int32_t softCap, horde_state_t currentState, int32_t currentLevel);
int32_t ManageSpawnCountsAndQueue(const horde::MapSize& mapSize, int32_t availableSpace);

static void ClearPendingWaveSpawns(const char* reason)
{
	const int32_t pending_spawn_plan = static_cast<int32_t>(g_spawn_system.spawn_plan.size());
	if (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters <= 0 && pending_spawn_plan <= 0)
	{
		return;
	}

	if (developer->integer)
	{
		gi.Com_PrintFmt("HORDE: {}. Clearing pending spawns (num_to_spawn={}, queued={}, planned={}).\n",
			reason, g_horde_local.num_to_spawn, g_horde_local.queued_monsters, pending_spawn_plan);
	}

	g_horde_local.num_to_spawn = 0;
	g_horde_local.queued_monsters = 0;
	g_spawn_system.spawn_plan.clear();
}

static bool CheckZeroMonsterDeploymentGrace(int32_t currentLevel, gtime_t currentTime, WaveEndReason& reason)
{
	const bool bossWaveWaitingForBoss = currentLevel >= 10 && (currentLevel % 5) == 0 && !boss_spawned_for_wave;
	const bool hasPendingSpawns = g_horde_local.num_to_spawn > 0 || g_horde_local.queued_monsters > 0;
	const bool hasPlannedSpawns = !g_spawn_system.spawn_plan.empty();
	const bool zeroCountedMonsters = GetStroggsNum() == 0;

	if (bossWaveWaitingForBoss || !zeroCountedMonsters || !hasPendingSpawns || hasPlannedSpawns)
	{
		g_horde_local.zero_monster_deployment_start = 0_sec;
		return false;
	}

	if (g_horde_local.zero_monster_deployment_start == 0_sec)
	{
		g_horde_local.zero_monster_deployment_start = currentTime;
		if (developer->integer)
		{
			gi.Com_PrintFmt("HORDE: Wave {} has 0 counted monsters while deployment is pending. Starting {:.1f}s grace.\n",
				currentLevel, ZERO_MONSTER_DEPLOYMENT_GRACE.seconds());
		}
		return false;
	}

	if (currentTime < g_horde_local.zero_monster_deployment_start + ZERO_MONSTER_DEPLOYMENT_GRACE)
	{
		return false;
	}

	if (DeveloperSuppressesWaveAutoAdvance())
	{
		return false;
	}

	ClearPendingWaveSpawns("Zero-monster deployment grace expired");
	g_horde_local.zero_monster_deployment_start = 0_sec;
	reason = WaveEndReason::AllMonstersDead;
	return true;
}


// PlanMonsterSpawnBatch moved to horde_spawning.cpp
// PlanNextSpawnBatch moved to horde_spawning.cpp
// ExecuteEmergencySpawnProcedure moved to horde_spawning.cpp

// --- Helper Function Implementations ---

// REMOVED: RebuildSpawnPointCacheIfNeeded moved to horde_spawning.cpp
// See horde_spawning.cpp line 735 for the active implementation

bool CheckHardCapAndLog(int32_t activeMonsters, int32_t hardCap, int32_t softCap, horde_state_t currentState, int32_t currentLevel)
{
	if (activeMonsters >= hardCap)
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("SpawnMonsters: Hard cap ({}/{}), SoftCap={}, AdjustedCap={}\n", activeMonsters, hardCap, softCap, g_adjusted_monster_cap);
		if (currentState == horde_state_t::spawning && !next_wave_message_sent)
		{
			VerifyAndAdjustBots();
			//gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Deployed (Hard Cap).\nWave: {}\n", currentLevel);
			next_wave_message_sent = true;
			g_horde_local.state = horde_state_t::active_wave;
		}
		ClearPendingWaveSpawns("Deployment finalized by hard cap");
		return true; // Hard cap reached
	}
	return false; // Hard cap not reached
}

int32_t ManageSpawnCountsAndQueue(const horde::MapSize& mapSize, int32_t availableSpace)
{
	if (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters > 0)
	{
		const int32_t base_transfer = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
		const int32_t transfer_amount = std::min({ g_horde_local.queued_monsters, availableSpace, base_transfer });
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

// DetermineSpawnStrategy moved to horde_spawning.cpp

// ValidateSpawnPointForMonster moved to horde_spawning.cpp

// FIXED: Champion bonus type selection thresholds
namespace ChampionBonusThresholds {
	constexpr float GHOSTLY = 0.09f;
	constexpr float STYGIAN = 0.22f;
	constexpr float CORRUPTED = 0.35f;
	constexpr float CHAMPION = 0.67f;

	constexpr float DROP_CHANCE_EARLY = 0.8f;
	constexpr float DROP_CHANCE_MID = 0.6f;
	constexpr float DROP_CHANCE_LATE = 0.45f;
	constexpr int32_t LEVEL_EARLY_THRESHOLD = 5;
	constexpr int32_t LEVEL_MID_THRESHOLD = 8;
	constexpr int32_t ARMOR_MIN_LEVEL = 6;
}

// FIXED: Weighted champion bonus selection using proper table
struct ChampionBonusEntry {
	int bonus_flag;
	float weight;
};

static constexpr ChampionBonusEntry CHAMPION_BONUS_TABLE[] = {
	{BF_GHOSTLY, ChampionBonusThresholds::GHOSTLY},
	{BF_STYGIAN, ChampionBonusThresholds::STYGIAN - ChampionBonusThresholds::GHOSTLY},
	{BF_CORRUPTED, ChampionBonusThresholds::CORRUPTED - ChampionBonusThresholds::STYGIAN},
	{BF_CHAMPION, ChampionBonusThresholds::CHAMPION - ChampionBonusThresholds::CORRUPTED},
	{BF_POSSESSED, 1.0f - ChampionBonusThresholds::CHAMPION}
};

static int SelectChampionBonusType() {
	// Compile-time validation: weights should sum to ~1.0
	// Note: Due to floating-point precision, we allow a small epsilon
	constexpr float total_weight =
		(ChampionBonusThresholds::GHOSTLY)+
		(ChampionBonusThresholds::STYGIAN - ChampionBonusThresholds::GHOSTLY) +
		(ChampionBonusThresholds::CORRUPTED - ChampionBonusThresholds::STYGIAN) +
		(ChampionBonusThresholds::CHAMPION - ChampionBonusThresholds::CORRUPTED) +
		(1.0f - ChampionBonusThresholds::CHAMPION);
	static_assert(total_weight > 0.99f && total_weight < 1.01f,
		"CHAMPION_BONUS_TABLE weights must sum to approximately 1.0");

	const float roll = frandom();
	float cumulative = 0.0f;

	for (const auto& entry : CHAMPION_BONUS_TABLE) {
		cumulative += entry.weight;
		if (roll < cumulative) {
			return entry.bonus_flag;
		}
	}

	// Fallback for floating-point precision edge cases (should rarely happen)
	// This handles the case where roll == 1.0 exactly or cumulative < roll due to FP rounding
	return BF_POSSESSED;
}

bool ApplyHordeBonuses(edict_t* monster, const int32_t currentLevel, const float champion_chance)
{
	bool became_champion = false;
	// Respect the restored per-wave elite/champion cap.
	// Reduced cooldown for more dynamic gameplay
	if ((!pvm->integer && currentLevel >= 3 && elites_spawned_this_wave < MAX_ELITES_PER_WAVE && champion_spawn_cooldown_ends_at < level.time && !monster->monsterinfo.IS_BOSS && frandom() < champion_chance) ||
		(pvm->integer && currentLevel >= 10 && elites_spawned_this_wave < MAX_ELITES_PER_WAVE && champion_spawn_cooldown_ends_at < level.time && !monster->monsterinfo.IS_BOSS && frandom() < champion_chance))
	{
		// FIXED: Use weighted selection table
		const int bonus_type = SelectChampionBonusType();
		monster->monsterinfo.bonus_flags = static_cast<decltype(monster->monsterinfo.bonus_flags)>(
			static_cast<int>(monster->monsterinfo.bonus_flags) | bonus_type);

		ApplyMonsterBonusFlags(monster);
		if (!monster->inuse) return false;

		monster->item = G_HordePickItem();
		monster->spawnflags = monster->item ? (monster->spawnflags & ~SPAWNFLAG_MONSTER_NO_DROP) : (monster->spawnflags | SPAWNFLAG_MONSTER_NO_DROP);

		elites_spawned_this_wave++;  // Increment counter instead of setting boolean
		champion_spawn_cooldown_ends_at = level.time + random_time(8_sec, 15_sec);  // Reduced from 10-20 sec
		gi.LocBroadcast_Print(PRINT_HIGH, "*** A {} has appeared! ***\n", GetDisplayName(monster));
		became_champion = true;
	}

	if (!became_champion)
	{
		// FIXED: Use named constants
		const float drop_chance = currentLevel <= ChampionBonusThresholds::LEVEL_EARLY_THRESHOLD ? ChampionBonusThresholds::DROP_CHANCE_EARLY :
			(currentLevel <= ChampionBonusThresholds::LEVEL_MID_THRESHOLD ? ChampionBonusThresholds::DROP_CHANCE_MID :
				ChampionBonusThresholds::DROP_CHANCE_LATE);
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

	if (currentLevel >= ChampionBonusThresholds::ARMOR_MIN_LEVEL && monster->monsterinfo.power_armor_type == IT_NULL && monster->monsterinfo.armor_type == IT_NULL)
	{
		SetMonsterArmor(monster);
		if (!monster->inuse) return false;
	}

	return true;
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
	const bool is_flying_only_lane = (spawn_point->style == 1);
	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(monster_type, predicted_mins, predicted_maxs);

	// TEST MODE: If g_horde_grid_first is enabled, try grid spawning first.
	// Exception: for flying monsters on dedicated style-1 lanes, preserve lane spawn and don't override with grid first.
	if (g_horde_grid_first && g_horde_grid_first->integer && HordePhys::g_spawn_grid.IsGenerated() &&
		!(is_flying && is_flying_only_lane))
	{
		constexpr int GRID_ATTEMPTS = 32;
		constexpr float GRID_MIN_DIST = 64.0f;
		constexpr float GRID_MAX_DIST = 512.0f;

		for (int attempt = 0; attempt < GRID_ATTEMPTS; ++attempt)
		{
			vec3_t grid_pos;
			if (!HordePhys::g_spawn_grid.GetRandomPositionNear(base_origin, GRID_MIN_DIST, GRID_MAX_DIST, grid_pos))
				continue;

			const auto validation = IsPositionPhysicallyValid(grid_pos, predicted_mins, predicted_maxs, is_flying);
			if (validation.is_valid)
			{
				out_origin = validation.adjusted_position;
				out_angles = base_angles;
				out_used_alternative = true; // Mark as alternative since we're using grid

				if (developer->integer > 1)
				{
					gi.Com_PrintFmt("GRID FIRST: Spawned at grid position (dist: {:.1f})\n",
						(out_origin - base_origin).length());
				}

				return true;
			}
		}
	}

	// Attempt Direct Spawn
	const auto validation = IsPositionPhysicallyValid(base_origin, predicted_mins, predicted_maxs, is_flying);
	if (validation.is_valid)
	{
		out_origin = validation.adjusted_position;
		out_angles = base_angles;
		out_used_alternative = false;
		if (developer->integer > 1 && is_flying && is_flying_only_lane)
		{
			gi.Com_PrintFmt("STYLE1 DIRECT: Spawned flying monster at dedicated lane {}.\n", out_origin);
		}
		return true;
	}
	if (developer->integer > 1 && is_flying && is_flying_only_lane)
	{
		gi.Com_PrintFmt("STYLE1 DIRECT FAIL: Rejected dedicated lane {} for flying spawn.\n", base_origin);
	}

	// Attempt Alternative Spawn
	if (TryAlternativeSpawnPosition(spawn_point, monster_type, out_origin, out_angles))
	{
		out_used_alternative = true;
		return true;
	}
	if (developer->integer > 1 && is_flying && is_flying_only_lane)
	{
		gi.Com_PrintFmt("STYLE1 ALT FAIL: No valid alternative near dedicated lane {}.\n", base_origin);
	}

	return false;
}

// Dependency for the main function below
static void ApplySuccessfulAlternativeCooldown(edict_t* spawn_point)
{
	if (!spawn_point || !spawn_point->inuse)
		return;

	const uint16_t index = GetSpawnPointIndexSafe(spawn_point);
	if (index == 0xFFFF) [[unlikely]]
		return;

	// FIX: Cast the signed 'index' to the unsigned 'size_t' for each array access.
	g_spawn_system.spawn_points_data.alternative_attempts[static_cast<size_t>(index)] = 0;
	g_spawn_system.spawn_points_data.needs_long_alternative_cooldown[static_cast<size_t>(index)] = false;
	g_spawn_system.spawn_points_data.alternative_cooldown[static_cast<size_t>(index)] = level.time + std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN);
	ApplyLongFlyingLaneCooldown(spawn_point);

	if (developer->integer > 1)
		gi.Com_PrintFmt("Success cooldown applied to spawn at {}: {:.1f}s\n", spawn_point->s.origin, std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN).seconds());
}

static void SetMonsterArmor(edict_t* monster)
{
	// Input validation and exception for specific monster types ---
	if (!monster || !monster->inuse || !monster->classname)
	{
		return;
	}

	// Get the monster's unique type ID from its cached value
	const horde::MonsterTypeID typeId = static_cast<horde::MonsterTypeID>(monster->monsterinfo.monster_type_id);

	// Check if this monster type should be excluded from getting armor
	if (typeId == horde::MonsterTypeID::MUTANT ||
		typeId == horde::MonsterTypeID::REDMUTANT ||
		typeId == horde::MonsterTypeID::GEKKKL ||
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
	const spawn_temp_t& st = ED_GetSpawnTemp();

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

void SetNextMonsterSpawnTime(const horde::MapSize& mapSize)
{
	// Original spawn time ranges (Big maps are faster)
	constexpr std::array<std::pair<gtime_t, gtime_t>, 3> BASE_SPAWN_TIMES = { {
		{0.5_sec, 0.7_sec}, // Small maps - slightly faster base for smaller batches
		{0.7_sec, 0.9_sec}, // Medium maps
		{0.4_sec, 0.6_sec}	// Big maps
	} };

	// Apply early wave warmup (waves 1-4) - SLOWER spawning that speeds up to normal by wave 5
	// This gives players time to warm up in early waves, then reaches normal speed
	float earlyWaveMultiplier = 1.0f;
	if (current_wave_level >= 1 && current_wave_level < HordeConstants::EARLY_WAVE_WARMUP_END)
	{
		// Wave 1: 1.4x (40% slower), Wave 2: 1.3x, Wave 3: 1.2x, Wave 4: 1.1x, Wave 5+: 1.0x
		// Higher multiplier = slower spawning (more time between spawns)
		earlyWaveMultiplier = HordeConstants::EARLY_WAVE_SLOW_MULTIPLIER -
			(static_cast<float>(current_wave_level - 1) * HordeConstants::WAVE_SPEED_INCREASE_PER_WAVE);
		earlyWaveMultiplier = std::clamp(earlyWaveMultiplier, 1.0f, 1.5f);
	}

	// Select base times based on map size
	const size_t mapIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);
	const auto& [base_min_time, base_max_time] = BASE_SPAWN_TIMES[mapIndex];

	// Apply extra slowdown for small maps (tighter spaces need more time between spawns)
	float mapSizeMultiplier = mapSize.isSmallMap ? HordeConstants::SMALL_MAP_EXTRA_SLOWDOWN : 1.0f;
	float combinedMultiplier = earlyWaveMultiplier * mapSizeMultiplier;

	// Apply the combined multiplier to get the target time range
	const gtime_t min_time = gtime_t::from_sec(base_min_time.seconds() * combinedMultiplier);
	const gtime_t max_time = gtime_t::from_sec(base_max_time.seconds() * combinedMultiplier);

	// Calculate the random interval within the adjusted range
	// Ensure min_time <= max_time before calling random_time to avoid potential issues
	const gtime_t calculated_interval = (min_time <= max_time) ? random_time(min_time, max_time) : min_time; // Fallback to min_time if somehow inverted

	// --- CLAMPING ---
	// Ensure the final interval is never less than the absolute minimum allowed
	gtime_t final_interval = std::max(calculated_interval, HordeConstants::MIN_MONSTER_SPAWN_INTERVAL);

	// Apply time acceleration (if active)
	if (g_horde_local.timeAcceleration > 1.0f)
	{
		final_interval = gtime_t::from_sec(final_interval.seconds() / g_horde_local.timeAcceleration);
		// Ensure we don't go below minimum
		final_interval = std::max(final_interval, HordeConstants::MIN_MONSTER_SPAWN_INTERVAL);
	}

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
		std::array<vec3_t, 16> positions{};  // Pre-computed positions
		std::array<float, 16> scores{};      // Pre-computed base scores
		uint8_t valid_count = 0;
		gtime_t last_update_time = 0_sec;
	};

	// Map-size-aware distance scaling
	struct ScaledDistances {
		float min_radius = 0.0f;
		float max_radius = 0.0f;
		float line_of_sight_tolerance = 0.0f;
	};

	ScaledDistances GetScaledDistances(const horde::MapSize& mapSize, int fallback_level = 0) const {
		ScaledDistances distances;

		// All distances doubled to prevent spawning too close to players
		if (mapSize.isSmallMap) {
			// Small maps: doubled minimum distances
			switch (fallback_level) {
			case 0: distances = { 240.0f, 400.0f, 0.85f }; break;  // Doubled from 120-280
			case 1: distances = { 200.0f, 350.0f, 0.80f }; break;  // Doubled from 100-200
			case 2: distances = { 160.0f, 280.0f, 0.75f }; break;  // Doubled from 80-150
			default: distances = { 120.0f, 200.0f, 0.70f }; break; // Doubled from 60-120
			}
		}
		else if (mapSize.isMediumMap) {
			// Medium maps: doubled minimum distances
			switch (fallback_level) {
			case 0: distances = { 400.0f, 700.0f, 0.90f }; break;  // Doubled from 200-500
			case 1: distances = { 300.0f, 550.0f, 0.85f }; break;  // Doubled from 150-400
			case 2: distances = { 240.0f, 450.0f, 0.80f }; break;  // Doubled from 120-300
			default: distances = { 200.0f, 350.0f, 0.75f }; break; // Doubled from 100-200
			}
		}
		else {
			// Big maps: doubled minimum distances
			switch (fallback_level) {
			case 0: distances = { 700.0f, 1200.0f, 0.95f }; break; // Doubled from 350-900
			case 1: distances = { 500.0f, 1000.0f, 0.90f }; break; // Doubled from 250-700
			case 2: distances = { 360.0f, 750.0f, 0.85f }; break;  // Doubled from 180-500
			default: distances = { 300.0f, 600.0f, 0.80f }; break; // Doubled from 150-400
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


	// Scores a potential position based on distance and proximity to recent events.
	float CalculatePositionScore(const vec3_t& pos, const vec3_t& player_pos, const horde::MapSize& mapSize) const {
		const float dist_sq = (pos - player_pos).lengthSquared();
		constexpr float OPTIMAL_DIST_SQ = 700.0f * 700.0f;  // Increased from 450 for better spawn distance

		// Score is higher the closer it is to the optimal distance
		float score = 100.0f - std::abs(dist_sq - OPTIMAL_DIST_SQ) * 0.0001f;

		// Bonus for positions that are not in recently used areas
		if (!IsPositionTooCloseToRecentSpawn(pos, mapSize)) score += 50.0f;
		if (!IsPositionTooCloseToRecentTeleport(pos, mapSize)) score += 30.0f;

		return score;
	}


	// Iterates through a player's cached candidates to find a valid spawn location.
	bool TryPlayerCandidatesFromCache(const PlayerSpawnCandidates& candidates,
		const vec3_t& mins, const vec3_t& maxs, bool is_flying,
		vec3_t& out_pos, vec3_t& out_angles) {
		// Use increased minimum distance from HordeConstants (250 units instead of 120)
		const float min_player_dist_sq = HordeConstants::EMERGENCY_MIN_DIST_FROM_PLAYER * HordeConstants::EMERGENCY_MIN_DIST_FROM_PLAYER;

		for (size_t i = 0; i < candidates.valid_count; ++i) {
			vec3_t test_pos = candidates.positions[i];

			// Check distance to all players to avoid spawning too close
			bool too_close_to_player = false;
			for (uint32_t p = 0; p < game.maxclients; p++) {
				edict_t* player = g_edicts + 1 + p;
				if (!player->inuse || !player->client || player->health <= 0)
					continue;

				float dist_sq = (test_pos - player->s.origin).lengthSquared();
				if (dist_sq < min_player_dist_sq) {
					too_close_to_player = true;
					break;
				}
			}

			const auto validation = IsPositionPhysicallyValid(test_pos, mins, maxs, is_flying);
			if (!too_close_to_player && validation.is_valid) {
				out_pos = validation.adjusted_position;
				const vec3_t to_player = candidates.player->s.origin - validation.adjusted_position;
				out_angles = (to_player.lengthSquared() > VECTOR_LENGTH_SQ_EPSILON) ?
					vectoangles(to_player) : candidates.player->s.angles;
				out_angles[PITCH] = 0;
				return true;
			}
		}
		return false;
	}

	// Updates candidates with custom distance range (for fallback logic)
	// MODIFIED: Now uses spawn grid as primary source, with procedural generation as fallback
	// ENHANCED: Adds 25% chance for out-of-visibility spawns and grid cooldown tracking
	void UpdatePlayerCandidatesWithRange(edict_t* player, PlayerSpawnCandidates& candidates,
		const ScaledDistances& distances) {
		constexpr size_t NUM_CANDIDATES = 16;

		candidates.player = player;
		candidates.valid_count = 0;
		candidates.last_update_time = level.time;

		const vec3_t player_origin = player->s.origin;

		// 25% chance to prefer out-of-visibility positions for this spawn batch
		const bool prefer_out_of_visibility = frandom() < HordeConstants::OUT_OF_VISIBILITY_CHANCE;

		// OPTIMIZATION: Try to use spawn grid positions first (guaranteed in-map)
		if (HordePhys::g_spawn_grid.IsGenerated()) {
			constexpr int MAX_GRID_ATTEMPTS = 64;  // Try more grid positions than we need

			for (int attempt = 0; attempt < MAX_GRID_ATTEMPTS && candidates.valid_count < NUM_CANDIDATES; ++attempt) {
				vec3_t grid_pos;

				// ALWAYS use tactical spawning (distance + cooldown checks are now default)
				// The 25% out-of-visibility chance is passed through prefer_out_of_visibility
				bool got_position = HordePhys::g_spawn_grid.GetTacticalSpawnPosition(
					grid_pos,
					HordeConstants::EMERGENCY_MIN_DIST_FROM_PLAYER,  // 350 units
					10,
					prefer_out_of_visibility
				);

				if (!got_position)
					break;

				// Check if position is within desired distance range from player
				const float dist = (grid_pos - player_origin).length();
				if (dist < distances.min_radius || dist > distances.max_radius)
					continue;

				// Map-size-aware line-of-sight validation
				if (!HasLineOfSightToSpawnPoint(grid_pos, distances.line_of_sight_tolerance))
					continue;

				// Grid position is valid, add it
				float base_score = 1.0f / (1.0f + dist * 0.001f);
				candidates.positions[candidates.valid_count] = grid_pos;
				candidates.scores[candidates.valid_count] = base_score;
				candidates.valid_count++;
			}
		}

		// FALLBACK: If grid didn't provide enough candidates, use procedural generation
		const size_t grid_candidates_found = candidates.valid_count;

		if (candidates.valid_count < NUM_CANDIDATES / 2) {  // Need at least half candidates
			for (size_t i = candidates.valid_count; i < NUM_CANDIDATES; ++i) {
				const float radius = frandom(distances.min_radius, distances.max_radius);
				const float angle_rad = (2.0f * PIf * static_cast<float>(i)) / NUM_CANDIDATES + frandom(-0.2f, 0.2f);

				vec3_t candidate_pos = {
					player_origin.x + cosf(angle_rad) * radius,
					player_origin.y + sinf(angle_rad) * radius,
					player_origin.z + frandom(-32.0f, 48.0f)
				};

				// Map-size-aware line-of-sight validation
				if (!HasLineOfSightToSpawnPoint(candidate_pos, distances.line_of_sight_tolerance))
					continue;

				// Basic validation and scoring
				float base_score = 1.0f / (1.0f + radius * 0.001f);

				candidates.positions[candidates.valid_count] = candidate_pos;
				candidates.scores[candidates.valid_count] = base_score;
				candidates.valid_count++;
			}

			if (developer->integer && grid_candidates_found < NUM_CANDIDATES / 4) {
				gi.Com_PrintFmt("Emergency spawn: Grid provided {} candidates, procedural added {}\n",
					grid_candidates_found, candidates.valid_count - grid_candidates_found);
			}
		}
	}

public:
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
					gi.Com_PrintFmt("EMERGENCY SPAWN: Used fallback level {} ({:.0f}-{:.0f}) for TypeID {}.\n",
						fallback_level, distances.min_radius, distances.max_radius, static_cast<int>(typeId));
				}
				if (out_used_player) {
					*out_used_player = used_player;
				}
				return true;
			}
		}

		// Final fallback: Try spawn grid if available
		if (HordePhys::g_spawn_grid.IsGenerated()) {
			vec3_t grid_pos;

			// Use tactical spawning if enabled (final fallback)
			bool got_position = false;
			if (g_horde_tactical_spawn->integer > 0)
			{
				got_position = HordePhys::g_spawn_grid.GetTacticalSpawnPosition(grid_pos, 256.0f, 20);
			}
			else
			{
				got_position = HordePhys::g_spawn_grid.GetRandomPosition(grid_pos);
			}

			if (got_position) {
				// Validate grid position
				const auto validation = IsPositionPhysicallyValid(grid_pos, predicted_mins, predicted_maxs, is_flying);
				if (validation.is_valid) {
					out_position = validation.adjusted_position;
					out_angles = vec3_origin; // Default angles for grid positions

					if (developer->integer) {
						gi.Com_PrintFmt("EMERGENCY SPAWN: Used spawn grid as final fallback for TypeID {}.\n",
							static_cast<int>(typeId));
					}

					// No specific player used for grid fallback
					if (out_used_player) {
						*out_used_player = nullptr;
					}
					return true;
				}
			}
		}

		return false;
	}

private:
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

// Global optimizer instance (defined AFTER the class) - accessible from horde_spawning.cpp
EmergencySpawnOptimizer g_emergency_spawn_optimizer;
// =======================================================================
// END: High-performance emergency spawn system
// =======================================================================

class MonsterSpawnPipeline {
private:
	// A batch of monsters to be processed together
	struct SpawnBatch {
		static constexpr size_t MAX_BATCH_SIZE = 8;
		std::array<SpawnPlanEntry, MAX_BATCH_SIZE> entries{};
		std::array<vec3_t, MAX_BATCH_SIZE> final_origins{};
		std::array<vec3_t, MAX_BATCH_SIZE> final_angles{};
		std::array<bool, MAX_BATCH_SIZE> used_alternatives{};
		std::array<edict_t*, MAX_BATCH_SIZE> spawned_monsters{};
		std::array<bool, MAX_BATCH_SIZE> is_valid{}; // FIXED: Explicit validity tracking instead of sentinel
		size_t count = 0;

		void Clear() {
			count = 0;
			// Clear validity flags
			is_valid.fill(false);
		}

		bool IsFull() const { return count >= MAX_BATCH_SIZE; }

		void AddEntry(const SpawnPlanEntry& entry) {
			if (count < MAX_BATCH_SIZE) {
				entries[count] = entry;
				is_valid[count] = true; // FIXED: Mark as valid pending validation
				spawned_monsters[count] = nullptr; // FIXED: Initialize to nullptr, not sentinel
				count++;
			}
		}
	};

	SpawnBatch current_batch;

	// FIXED: LRU cache with bounded size
	struct MonsterTypeCache {
		horde::MonsterTypeID type_id = horde::MonsterTypeID::UNKNOWN;
		vec3_t predicted_mins = vec3_origin;
		vec3_t predicted_maxs = vec3_origin;
		bool is_flying = false;
		gtime_t last_access = 0_sec; // For LRU eviction
	};

	static constexpr size_t MAX_CACHE_SIZE = 32;
	boost::container::flat_map<horde::MonsterTypeID, MonsterTypeCache> type_cache;  // C++23 - hot path cache

	// FIXED: Evict oldest entry when cache is full
	void EvictOldestCacheEntry() {
		if (type_cache.empty()) return;

		auto oldest = type_cache.begin();
		gtime_t oldest_time = oldest->second.last_access;

		for (auto it = type_cache.begin(); it != type_cache.end(); ++it) {
			if (it->second.last_access < oldest_time) {
				oldest_time = it->second.last_access;
				oldest = it;
			}
		}

		type_cache.erase(oldest);
	}

	void HandleSuccessfulSpawn(edict_t* spawn_point, bool used_alternative, edict_t* monster) {
		g_spawn_system.consecutive_spawn_failures = 0;
		if (used_alternative) {
			ApplySuccessfulAlternativeCooldown(spawn_point);
		}
		else {
			OnSuccessfulSpawn(spawn_point);
		}

		if (g_horde_local.num_to_spawn > 0) {
			g_horde_local.num_to_spawn--;
		}
		monsters_spawned_in_current_phase++;
		// Note: spawn grow effects now handled inside Horde_SpawnMonster
	}

	void HandleSpawnFailure(edict_t* spawn_point, bool used_alternative) {
		g_spawn_system.consecutive_spawn_failures++;
		if (used_alternative) {
			ApplyAlternativePositionCooldown(spawn_point);
		}
		else {
			IncreaseSpawnAttempts(spawn_point);
		}
	}

	// FIXED: Added const correctness and LRU cache management
	const MonsterTypeCache& GetOrCacheMonsterType(horde::MonsterTypeID type_id) {
		auto it = type_cache.find(type_id);
		if (it != type_cache.end()) {
			it->second.last_access = level.time; // Update access time
			return it->second;
		}

		// FIXED: Evict if cache is full
		if (type_cache.size() >= MAX_CACHE_SIZE) {
			EvictOldestCacheEntry();
		}

		MonsterTypeCache new_entry;
		new_entry.type_id = type_id;
		new_entry.is_flying = IsFlying(type_id);
		new_entry.last_access = level.time;
		GetPredictedScaledBounds(type_id, new_entry.predicted_mins, new_entry.predicted_maxs);

		auto [inserted_it, success] = type_cache.emplace(type_id, new_entry);
		return inserted_it->second;
	}

	void PrepareBatch() {
		current_batch.Clear();
		while (!g_spawn_system.spawn_plan.empty() && !current_batch.IsFull()) {
			current_batch.AddEntry(g_spawn_system.spawn_plan.back());
			g_spawn_system.spawn_plan.pop_back();
		}
	}

	// FIXED: Removed unused variable, use cached bounds
	void ValidateLocations() {
		for (size_t i = 0; i < current_batch.count; ++i) {
			if (!current_batch.is_valid[i]) continue; // Skip already invalid entries

			const auto& entry = current_batch.entries[i];

			bool found = FindValidSpawnLocation(
				entry.spawn_point,
				entry.typeId,
				current_batch.final_origins[i],
				current_batch.final_angles[i],
				current_batch.used_alternatives[i]
			);

			if (!found) {
				current_batch.is_valid[i] = false; // FIXED: Mark as invalid explicitly
			}
		}
	}

	void SpawnMonsters() {
		for (size_t i = 0; i < current_batch.count; ++i) {
			if (!current_batch.is_valid[i]) continue; // FIXED: Check explicit validity flag

			const auto& entry = current_batch.entries[i];
			current_batch.spawned_monsters[i] = Horde_SpawnMonster(
				current_batch.final_origins[i],
				current_batch.final_angles[i],
				entry.typeId,
				current_wave_level,
				g_spawn_system.champion_chance_for_current_batch
			);
		}
	}

	void ProcessResults() {
		for (size_t i = 0; i < current_batch.count; ++i) {
			if (!current_batch.is_valid[i]) {
				// Entry failed validation, handle as failure
				HandleSpawnFailure(current_batch.entries[i].spawn_point, current_batch.used_alternatives[i]);
				continue;
			}

			const auto& entry = current_batch.entries[i];
			edict_t* monster = current_batch.spawned_monsters[i];

			if (monster && monster->inuse) {
				HandleSuccessfulSpawn(entry.spawn_point, current_batch.used_alternatives[i], monster);
			}
			else {
				HandleSpawnFailure(entry.spawn_point, current_batch.used_alternatives[i]);
			}
		}
	}

public:
	void ProcessSpawnPlan() {
		if (g_spawn_system.spawn_plan.empty()) return;

		int total_spawned = 0;
		int total_validation_failures = 0;
		int total_spawn_failures = 0;

		while (!g_spawn_system.spawn_plan.empty()) {
			PrepareBatch();

			if (current_batch.count > 0) {
				ValidateLocations();

				// Count validation failures
				for (size_t i = 0; i < current_batch.count; ++i) {
					if (!current_batch.is_valid[i]) {
						total_validation_failures++;
					}
				}

				SpawnMonsters();

				// Count actual spawns vs spawn failures
				for (size_t i = 0; i < current_batch.count; ++i) {
					if (!current_batch.is_valid[i]) continue;

					edict_t* monster = current_batch.spawned_monsters[i];
					if (monster && monster->inuse) {
						total_spawned++;
					}
					else {
						total_spawn_failures++;
					}
				}

				ProcessResults();

				// Record spawn in history for variety tracking
				// Track the most common family in this batch
				if (current_batch.count > 0 && total_spawned > 0) {
					std::array<int32_t, static_cast<size_t>(AssetFamilyID::MAX_FAMILIES)> family_counts{};

					// Count families in successful spawns
					for (size_t i = 0; i < current_batch.count; ++i) {
						if (!current_batch.is_valid[i]) continue;

						edict_t* monster = current_batch.spawned_monsters[i];
						if (monster && monster->inuse) {
							AssetFamilyID family = GetMonsterAssetFamily(current_batch.entries[i].typeId);
							if (family != AssetFamilyID::UNKNOWN_FAMILY) {
								family_counts[static_cast<size_t>(family)]++;
							}
						}
					}

					// Find most common family in this batch
					AssetFamilyID dominant_family = AssetFamilyID::UNKNOWN_FAMILY;
					int32_t max_count = 0;
					for (size_t i = 0; i < family_counts.size(); ++i) {
						if (family_counts[i] > max_count) {
							max_count = family_counts[i];
							dominant_family = static_cast<AssetFamilyID>(i);
						}
					}

					// Record the dominant family
					if (dominant_family != AssetFamilyID::UNKNOWN_FAMILY) {
						RecordSpawnInHistory(dominant_family, current_wave_level);

						if (developer->integer >= 2) {
							gi.Com_PrintFmt("Recorded spawn: family={}, count={}, wave={}\n",
								static_cast<int>(dominant_family), max_count, current_wave_level);
						}
					}
				}
			}
		}

		// Log execution summary
		// if (developer->integer && (total_spawned < total_planned || developer->integer >= 2)) {
		//     gi.Com_PrintFmt("SPAWN EXECUTION: Planned={}, Spawned={}, ValidationFailed={}, SpawnFailed={}\n",
		//                     total_planned, total_spawned, total_validation_failures, total_spawn_failures);
		// }
	}
};

// Global pipeline instance (defined AFTER the class) - accessible from horde_spawning.cpp
MonsterSpawnPipeline g_monster_spawn_pipeline;
// =======================================================================
// END: High-performance monster spawning pipeline
// =======================================================================

// Wrapper functions that use the optimizer/pipeline classes
bool FindEmergencySpawnPosition(vec3_t& position, vec3_t& angles, bool& used_human_player, horde::MonsterTypeID typeId, edict_t* specific_target)
{
	edict_t* actual_used_player = nullptr;
	if (g_emergency_spawn_optimizer.FindOptimalPosition(position, angles, typeId, specific_target, &actual_used_player)) {
		// Mark grid position as used to prevent spawning in same area too soon
		HordePhys::g_spawn_grid.MarkPositionUsed(position);

		// Determine if the actually used player was human
		if (actual_used_player) {
			used_human_player = !(actual_used_player->svflags & SVF_BOT);
		}
		else {
			// Fallback to the old heuristic if no specific player was returned
			used_human_player = AreHumanPlayersPresent();
		}
		return true;
	}
	return false;
}

void ExecuteSpawnPlan()
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

void CalculateTopDamager(PlayerStats& topDamager, float& percentage)
{
	constexpr int32_t MAX_DAMAGE = 100000;
	int32_t total_damage = 0;
	topDamager = PlayerStats();	  // Reset stats
	topDamager.total_damage = -1; // Use -1 to ensure first valid player is picked

	for (const auto& player : active_players())
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
static const std::array<RewardInfo, 4> TOP_DAMAGER_REWARDS = { {
	{IT_ITEM_BANDOLIER, 60}, // More common
	{IT_ITEM_SENTRYGUN, 20}, // More common
	{IT_ITEM_STROGGSUMM, 19}, // More common
	{IT_AMMO_NUKE, 1}		 // lessss common
} };

// Pre-computed total reward weight, calculated directly from the single array
static const int TOTAL_REWARD_WEIGHT = []
	{
		int total = 0;
		// Calculate sum directly from the array
		for (const auto& reward : TOP_DAMAGER_REWARDS)
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

static const char* GiveTopDamagerReward(const PlayerStats& topDamager, std::string_view playerName)
{
	// Quick validation with early return
	if (!topDamager.player || !topDamager.player->inuse || !topDamager.player->client || (g_vortex->integer))
		return nullptr;

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
		return nullptr;
	}

	// Spawn and give item directly to player
	edict_t* entity = G_Spawn();
	if (!entity)
		return nullptr;

	entity->classname = item->classname;
	entity->item = item;
	SpawnItem(entity, item, spawn_temp_t::empty);

	// Check if SpawnItem succeeded (entity might be freed if e.g., item shouldn't spawn in DM)
	if (!entity->inuse)
	{
		// SpawnItem already freed it, just return nullptr
		return nullptr;
	}

	// Give item to player
	Touch_Item(entity, topDamager.player, null_trace, true);
	// Touch_Item might free the entity if pickup is successful
	if (entity->inuse)
	{
		G_FreeEdict(entity); // Free if Touch_Item didn't consume it
	}

	// Return item name for announcement (caller will print combined message)
	const char* itemName = item->use_name ? item->use_name : (item->classname ? item->classname : "reward");
	return itemName;
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
	case WaveEndReason::FewMonstersRemaining:
		gi.LocBroadcast_Print(PRINT_HIGH, "Wave {} Advancing - Hostiles Still Active!\n",
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

		// Try to give reward and get item name
		const char* rewardItem = GiveTopDamagerReward(topDamager, std::string_view(playerName));

		if (rewardItem)
		{
			// Print combined message: damage stats + reward in one line
			gi.LocBroadcast_Print(PRINT_HIGH, "{} dealt {} damage ({}% of total) and receives a {}!\n",
				playerName, topDamager.total_damage, static_cast<int>(percentage), rewardItem);

			// Reset player stats
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
	// Safety check: ensure spawn system is initialized
	if (g_num_spawn_points == 0 ||
		g_spawn_system.spawn_points_data.isTemporarilyDisabled.size() < g_num_spawn_points)
		return;

	// Iterate using the compact index, which is much more efficient.
	for (size_t index = 0; index < g_num_spawn_points; ++index)
	{
		if (g_spawn_system.spawn_points_data.isTemporarilyDisabled[index])
		{
			g_spawn_system.spawn_points_data.isTemporarilyDisabled[index] = false;
			g_spawn_system.spawn_points_data.attempts[index] = 0;
			g_spawn_system.spawn_points_data.cooldownEndsAt[index] = 0_sec;
		}
	}
}
// RESTORED and CORRECTED: This function was also removed but is required.
edict_t* Horde_SpawnMonster(
	const vec3_t& origin,
	const vec3_t& angles,
	horde::MonsterTypeID monster_type,
	int32_t currentLevel,
	float champion_chance); // Already shown above

void Horde_RunFrame()
{
	// Safety check: don't run during intermission or before spawn system is initialized
	if (level.intermissiontime || !level.mapname[0])
		return;

	// Safety check: ensure spawn system vectors are initialized before any spawn point access
	if (g_num_spawn_points > 0 &&
		g_spawn_system.spawn_points_data.isTemporarilyDisabled.size() < g_num_spawn_points)
		return;

	// --- TIME-SLICED EXECUTION PHASE ---
	ExecuteSpawnPlan();
	ExecuteNextSpecialSpawn();
	// --- END EXECUTION PHASE ---

	const gtime_t currentTime = level.time;
	const horde::MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level;

	// Keep classic Horde rewards in sync while players are alive.
	// This decouples reward delivery from InitClientPersistant/respawn timing.
	static gtime_t last_wave_reward_sync = 0_sec;
	if (!g_vortex->integer && currentLevel > 0 && currentTime >= last_wave_reward_sync + 1_sec)
	{
		last_wave_reward_sync = currentTime;
		ProcessWaveRewards(currentLevel);
	}

	// Apply fog for special wave types (continuously to maintain fog and handle mid-wave joins)
	if (HasWaveType(current_wave_type, MonsterWaveType::Gekk) ||
		HasWaveType(current_wave_type, MonsterWaveType::Berserk))
	{
		ApplyFogEffect();
	}

	CleanupSpawnPointCache();
	CheckAndReduceSpawnCooldowns();

	// Update lowest and highest player level periodically (every 2 seconds)
	static gtime_t last_player_level_check = 0_sec;
	if (currentTime >= last_player_level_check + 2_sec)
	{
		last_player_level_check = currentTime;

		// Find the lowest and highest pvm_level among all active players
		int32_t lowest_level = INT32_MAX;
		int32_t highest_level = INT32_MIN;
		bool found_player = false;

		for (const auto* player : active_players_no_spect())
		{
			if (!player || !player->client)
				continue;

			found_player = true;
			int32_t player_level = player->client->pers.pvm_level;

			if (player_level < lowest_level)
			{
				lowest_level = player_level;
			}
			if (player_level > highest_level)
			{
				highest_level = player_level;
			}
		}

		// Update global variables
		g_lowest_player_level = found_player ? lowest_level : 0;
		g_highest_player_level = found_player ? highest_level : 0;
	}
	// Check if retaliation mode has timed out
	if (g_spawn_system.special_spawn_state.type == SpecialSpawnType::Retaliation && currentTime >= g_horde_retaliation_end_time) {
		g_spawn_system.special_spawn_state.clear();
		g_horde_retaliation_end_time = 0_sec;
		if (developer->integer) {
			gi.Com_PrintFmt("HORDE: Retaliation Mode Ended.\n");
		}
	}

	// FIXED: Use struct members instead of static variables
	if (g_horde_local.wave_at_last_check != currentLevel) {
		g_horde_local.last_wave_change_time = currentTime;
		g_horde_local.wave_at_last_check = currentLevel;
	}
	else if (g_horde_local.state != horde_state_t::warmup && currentTime > g_horde_local.last_wave_change_time + HordeConstants::WAVE_STUCK_TIMEOUT) {
		if (GetStroggsNum() == 0) {
			if (!DeveloperSuppressesWaveAutoAdvance()) {
				if (developer->integer) {
					gi.Com_PrintFmt("CRITICAL: Wave {} stuck for over ({:.1f}s with 0 monsters. Forcing progression.\n",
						currentLevel, HordeConstants::WAVE_STUCK_TIMEOUT.seconds());
				}
				g_horde_local.state = horde_state_t::cleanup;
				g_horde_local.monster_spawn_time = currentTime;
			}
		}
		else {
			g_horde_local.last_wave_change_time = currentTime;
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
		// FIXED: Use struct members instead of static variables
		if (currentLevel != g_horde_local.prev_wave_level_for_spawning_timers)
		{
			g_horde_local.spawning_phase_timeout_start = currentTime;
			g_horde_local.prev_wave_level_for_spawning_timers = currentLevel;
			initial_total_monsters_for_spawning_phase_timeout = g_horde_local.num_to_spawn + g_horde_local.queued_monsters;
			monsters_spawned_in_current_phase = 0;
			// Reset acceleration for new spawning phase
			g_horde_local.timeAcceleration = 1.0f;
			g_horde_local.targetTimeAcceleration = 1.0f;
		}

		// Calculate time remaining and apply smooth acceleration as timeout approaches
		const gtime_t elapsed = currentTime - g_horde_local.spawning_phase_timeout_start;
		const gtime_t timeout_limit = 90_sec;

		// Start accelerating when past 60% of timeout (54 seconds)
		if (elapsed > timeout_limit * 0.6f && (g_horde_local.num_to_spawn > 0 || g_horde_local.queued_monsters > 0))
		{
			// Calculate how far through the danger zone we are (60% to 100%)
			const float danger_progress = std::clamp((elapsed - timeout_limit * 0.6f).seconds() / (timeout_limit * 0.4f).seconds(), 0.0f, 1.0f);

			// Gradually increase target acceleration from 1.0x to 3.0x
			const float target_accel = 1.0f + (danger_progress * 2.0f);

			if (std::abs(g_horde_local.targetTimeAcceleration - target_accel) > 0.1f)
			{
				g_horde_local.targetTimeAcceleration = target_accel;
				g_horde_local.accelerationStartTime = currentTime;
				g_horde_local.accelerationDuration = 2_sec;

				// if (developer->integer)
				// {
				// 	gi.Com_PrintFmt("Spawning phase acceleration: {:.1f}x ({}s elapsed, {} remaining to spawn)\n",
				// 					target_accel, elapsed.seconds(), g_horde_local.num_to_spawn + g_horde_local.queued_monsters);
				// }
			}
		}

		// Update smooth acceleration interpolation
		if (std::abs(g_horde_local.timeAcceleration - g_horde_local.targetTimeAcceleration) > 0.01f)
		{
			const float smooth_rate = 5.0f;
			const float delta = (g_horde_local.targetTimeAcceleration - g_horde_local.timeAcceleration) * smooth_rate * gi.frame_time_s;
			g_horde_local.timeAcceleration += delta;

			if (std::abs(g_horde_local.timeAcceleration - g_horde_local.targetTimeAcceleration) < 0.01f)
			{
				g_horde_local.timeAcceleration = g_horde_local.targetTimeAcceleration;
			}
		}

		if (currentTime > g_horde_local.spawning_phase_timeout_start + timeout_limit)
		{
			if (!next_wave_message_sent)
			{
				gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Deployment Finalized (Timeout).\nWave Level: {}\n", currentLevel);
				next_wave_message_sent = true;
			}
			ClearPendingWaveSpawns("Deployment timeout reached");
			g_horde_local.state = horde_state_t::active_wave;
			// Reset acceleration when entering active wave
			g_horde_local.timeAcceleration = 1.0f;
			g_horde_local.targetTimeAcceleration = 1.0f;
			break;
		}


		if (g_horde_local.monster_spawn_time <= currentTime) {
			if (IsBossWave() && !boss_spawned_for_wave) {
				SpawnBossAutomatically();
			}
			else if (!IsBossWave() || boss_spawned_for_wave) {
				PlanNextSpawnBatch();
			}

			if (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters <= 0) {
				if (!IsBossWave() || boss_spawned_for_wave) {
					if (!next_wave_message_sent) {
						VerifyAndAdjustBots();
						gi.LocBroadcast_Print(PRINT_HIGH, "Wave fully deployed! Current Wave Level is: {}\n", currentLevel);
						next_wave_message_sent = true;
					}
					g_horde_local.state = horde_state_t::active_wave;
					// Reset acceleration when entering active wave
					g_horde_local.timeAcceleration = 1.0f;
					g_horde_local.targetTimeAcceleration = 1.0f;
				}
			}
		}

		if (g_horde_local.state == horde_state_t::spawning &&
			CheckZeroMonsterDeploymentGrace(currentLevel, currentTime, currentWaveEndReason))
		{
			waveEnded = true;
			break;
		}
		break;
	}

	case horde_state_t::active_wave:
		if (CheckRemainingMonstersCondition(mapSize, currentWaveEndReason)) {
			waveEnded = true;
			break;
		}
		if (g_horde_local.monster_spawn_time <= currentTime) {
			PlanNextSpawnBatch();
		}
		break;

	case horde_state_t::cleanup:
		if (g_horde_local.monster_spawn_time < currentTime) {
			HandleWaveCleanupMessage(mapSize);
			g_horde_local.warm_time = currentTime + random_time(0.5_sec, 1.0_sec);  // REDUCED: 1.4-1.9s -> 0.5-1.0s for faster pacing
			g_horde_local.state = horde_state_t::rest;
			CheckAndResetDisabledSpawnPoints();
		}
		break;

	case horde_state_t::rest:
		if (g_horde_local.warm_time < currentTime) {
			AnnounceIncomingWave(3_sec);  // Message display duration (doesn't affect wave timing)
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(g_horde_local.level + 1);
		}
		break;
	}

	if (waveEnded) {
		SendCleanupMessage(currentWaveEndReason);
		g_horde_local.monster_spawn_time = currentTime + 0.5_sec;  // REDUCED: 1.5s -> 0.5s for faster pacing
		g_horde_local.state = horde_state_t::cleanup;
		ResetWaveAdvanceState();
		ResetStroggCleanup();
	}

	if (horde_message_end_time != 0_sec && currentTime >= horde_message_end_time) {
		ClearHordeMessage();
	}
}

// Función para manejar el evento de reinicio
void HandleResetEvent()
{
	// FIX: Reset player benefits on map change for horde mode (but not vortex mode)
	// In vortex mode, benefits should persist across map changes
	// This happens BEFORE ResetGame() to ensure players are fully initialized

	// Safety check: g_edicts may not be initialized yet during PreInitGame
	if (!g_edicts)
		return;

	if (!g_vortex || !g_vortex->integer)
	{
		for (uint32_t i = 0; i < game.maxclients; i++)
		{
			edict_t* player = g_edicts + 1 + i;
			if (!player || !player->inuse || !player->client)
				continue;

			// Reset all benefits masks (vampire, ammo_regen, etc.)
			player->client->pers.active_abilities_mask = 0;
			player->client->pers.active_weapons_mask = 0;
			player->client->pers.purchased_benefits_mask = 0;
			player->client->pers.auto_purchased_benefits_mask = 0;

			// Reset bonus points to 0 (fresh start for new map)
			player->client->pers.ability_points = 0;
			player->client->pers.weapon_points = 0;

			// Reset BFG mode to default
			player->client->pers.bfg_mode = BFGMode::NORMAL;

			if (developer && developer->integer)
			{
				gi.Com_PrintFmt("INFO: Reset benefits for player {} ({})\n",
					i + 1, player->client->pers.netname);
			}
		}
	}

	ResetGame();
}

// Get the remaining time for the current wave
gtime_t GetWaveTimer()
{
	// Don't show a timer during non-active states (warmup, cleanup, rest)
	// Only show timer during spawning and active_wave states
	if (g_horde_local.state == horde_state_t::warmup ||
		g_horde_local.state == horde_state_t::cleanup ||
		g_horde_local.state == horde_state_t::rest)
	{
		return 0_sec;
	}

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
	return live_monster_count;
}

// Helper function to check if it's a boss wave
inline bool IsBossWave() noexcept
{
	return g_horde_local.level >= 10 && g_horde_local.level % 5 == 0;
}

bool Horde_TeleportMonster(edict_t* self, const vec3_t& destination_origin, const vec3_t& destination_angles, bool play_effects, bool force_despite_visibility)
{
	PROFILE_SCOPE("Horde_TeleportMonster");
	if (level.intermissiontime)
		return false;

	if (!self || !self->inuse || self->deadflag || !is_valid_vector(destination_origin) || !is_valid_vector(destination_angles))
	{
		return false;
	}

	if (self->monsterinfo.isfriendlyspawn ||
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
	horde::MonsterTypeID monsterTypeId = static_cast<horde::MonsterTypeID>(self->monsterinfo.monster_type_id);
	bool is_flying_monster = IsFlying(monsterTypeId);
	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(monsterTypeId, predicted_mins, predicted_maxs);

	const auto validation = IsPositionPhysicallyValid(final_pos_after_validation, predicted_mins, predicted_maxs, is_flying_monster);
	if (!validation.is_valid)
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
	self->monsterinfo.last_activity_time = level.time;
	self->teleport_time = level.time + random_time(HordeConstants::MIN_TELEPORT_COOLDOWN_MONSTER, HordeConstants::MAX_TELEPORT_COOLDOWN_MONSTER);

	return true;
}

// Get the base model path for a monster (monsters sharing the same model are cheap to add)
static const char* GetMonsterModelPath(horde::MonsterTypeID typeId)
{
	// Map monsters to their base models - monsters sharing models are cheap to precache
	switch (typeId) {
		// Regular tanks share the same model
	case horde::MonsterTypeID::TANK:
	case horde::MonsterTypeID::TANK_COMMANDER:
	case horde::MonsterTypeID::TANK_64:
	case horde::MonsterTypeID::TANK_SPAWNER:
	case horde::MonsterTypeID::EASTERTANK:
		return "models/monsters/tank/";
		// Note: RUNNERTANK uses different model (models/vault/monsters/tank/)

		// Soldiers share models
	case horde::MonsterTypeID::SOLDIER:
	case horde::MonsterTypeID::SOLDIER_LIGHT:
	case horde::MonsterTypeID::SOLDIER_SS:
	case horde::MonsterTypeID::SOLDIER_HYPERGUN:
	case horde::MonsterTypeID::SOLDIER_RIPPER:
	case horde::MonsterTypeID::SOLDIER_LASERGUN:
		return "models/monsters/soldier/";

		// Gunners share models
	case horde::MonsterTypeID::GUNNER:
	case horde::MonsterTypeID::GUNNER_VANILLA:
	case horde::MonsterTypeID::GUNCMDR:
	case horde::MonsterTypeID::GUNCMDR_VANILLA:
	case horde::MonsterTypeID::GUNCMDR_KL:
		return "models/monsters/gunner/";

		// Gladiators share models
	case horde::MonsterTypeID::GLADIATOR:
	case horde::MonsterTypeID::GLADIATOR_B:
	case horde::MonsterTypeID::GLADIATOR_C:
		return "models/monsters/gladiator/";

		// Shamblers share models
	case horde::MonsterTypeID::SHAMBLER:
	case horde::MonsterTypeID::SHAMBLER_SMALL:
	case horde::MonsterTypeID::SHAMBLER_KL:
		return "models/monsters/shambler/";

		// Mutants share models
	case horde::MonsterTypeID::MUTANT:
	case horde::MonsterTypeID::REDMUTANT:
		return "models/monsters/mutant/";

		// Hovers share models
	case horde::MonsterTypeID::HOVER:
	case horde::MonsterTypeID::HOVER_VANILLA:
		return "models/monsters/hover/";

		// Infantry share models
	case horde::MonsterTypeID::INFANTRY:
	case horde::MonsterTypeID::INFANTRY_VANILLA:
		return "models/monsters/infantry/";

		// Floaters share models
	case horde::MonsterTypeID::FLOATER:
	case horde::MonsterTypeID::FLOATER_TRACKER:
		return "models/monsters/floater/";

		// Chicks share models
	case horde::MonsterTypeID::CHICK:
	case horde::MonsterTypeID::CHICK_HEAT:
	case horde::MonsterTypeID::CHICKKL:
		return "models/monsters/chick/";

		// Arachnids share models
	case horde::MonsterTypeID::SPIDER:
	case horde::MonsterTypeID::ARACHNID:
	case horde::MonsterTypeID::ARACHNID2:
	case horde::MonsterTypeID::GM_ARACHNID:
	case horde::MonsterTypeID::PSX_ARACHNID:
		return "models/monsters/arachnid/";

		// Fixbots share models (including boss variant at wave 10)
	case horde::MonsterTypeID::FIXBOT:
	case horde::MonsterTypeID::FIXBOT_KL:
		return "models/monsters/fixbot/";

		// Boss2 variants share models
	case horde::MonsterTypeID::BOSS2:
	case horde::MonsterTypeID::BOSS2_64:
	case horde::MonsterTypeID::BOSS2_MINI:
	case horde::MonsterTypeID::BOSS2_KL:
		return "models/monsters/boss2/";

		// Carrier variants share models
	case horde::MonsterTypeID::CARRIER:
	case horde::MonsterTypeID::CARRIER_MINI:
		return "models/monsters/carrier/";

		// Widow variants - note WIDOW2 uses blackwidow2 (ultrathink) model
	case horde::MonsterTypeID::WIDOW:
	case horde::MonsterTypeID::WIDOW1:
		return "models/monsters/widow/";

	case horde::MonsterTypeID::WIDOW2:
		return "models/monsters/blackwidow2/";

		// Guardian variants share models
	case horde::MonsterTypeID::GUARDIAN:
	case horde::MonsterTypeID::PSX_GUARDIAN:
	case horde::MonsterTypeID::JANITOR2:  // Janitor2 shares Guardian model
		return "models/monsters/guardian/";

		// Janitor/Supertank share models
	case horde::MonsterTypeID::JANITOR:
	case horde::MonsterTypeID::SUPERTANK:
	case horde::MonsterTypeID::SUPERTANKKL:
	case horde::MonsterTypeID::BOSS5:
		return "models/monsters/boss5/";

		// Makron/Jorg share some assets
	case horde::MonsterTypeID::MAKRON:
	case horde::MonsterTypeID::MAKRON_KL:
		return "models/monsters/makron/";

	case horde::MonsterTypeID::JORG:
	case horde::MonsterTypeID::JORG_SMALL:
		return "models/monsters/jorg/";

		// Medics share models
	case horde::MonsterTypeID::MEDIC:
	case horde::MonsterTypeID::MEDIC_COMMANDER:
		return "models/monsters/medic/";

		// Daedalus variants share models
	case horde::MonsterTypeID::DAEDALUS:
	case horde::MonsterTypeID::DAEDALUS_BOMBER:
		return "models/monsters/daedalus/";

		// Turret/Sentrygun share models
	case horde::MonsterTypeID::TURRET:
	case horde::MonsterTypeID::SENTRYGUN:
		return "models/monsters/turret/";

		// Unique models
	case horde::MonsterTypeID::RUNNERTANK:  // RunnerTank uses unique vault model
		return "models/vault/monsters/tank/";
	case horde::MonsterTypeID::PARASITE:
	case horde::MonsterTypeID::PERRO_KL:
		return "models/monsters/parasite/";
	case horde::MonsterTypeID::BRAIN:
		return "models/monsters/brain/";
	case horde::MonsterTypeID::FLYER:
		return "models/monsters/flyer/";
	case horde::MonsterTypeID::BERSERK:
	case horde::MonsterTypeID::BERSERKERKL:
		return "models/monsters/berserk/";
	case horde::MonsterTypeID::GEKK:
	case horde::MonsterTypeID::GEKKKL:
		return "models/monsters/gekk/";
	case horde::MonsterTypeID::STALKER:
		return "models/monsters/stalker/";

	default:
		return nullptr; // Unknown or special
	}
}

// Unlock all monsters that share the same model as the given boss
// This allows "free" monsters since the model is already loaded in memory
// Note: Only unlocks monsters whose families are in the allowed list for this map
void UnlockModelFamilyMembers(horde::MonsterTypeID boss_typeId, int32_t current_wave)
{
	const char* boss_model_path = GetMonsterModelPath(boss_typeId);
	if (!boss_model_path) {
		return; // No model path, nothing to unlock
	}

	// Check if the boss's family is allowed for this map
	AssetFamilyID boss_family = GetMonsterAssetFamily(boss_typeId);
	if (g_precached_families_this_map.find(boss_family) == g_precached_families_this_map.end()) {
		if (developer->integer) {
			gi.Com_PrintFmt("Model Family Unlock: BLOCKED - family {} not allowed on this map\n",
				static_cast<int>(boss_family));
		}
		return; // Boss's family not allowed, don't unlock family members
	}

	MarkMonsterTypePrecached(boss_typeId, boss_model_path);

	int unlocked_count = 0;

	// Iterate through all monster types to find family members
	for (size_t i = 0; i < MONSTER_DATA_COUNT; ++i) {
		const auto& monster = monsterTypes[i];

		// Skip if already precached
		if (g_precached_monster_types_flags[static_cast<size_t>(monster.typeId)]) {
			continue;
		}

		// Check if this monster shares the same model
		const char* monster_model_path = GetMonsterModelPath(monster.typeId);
		if (!monster_model_path || strcmp(boss_model_path, monster_model_path) != 0) {
			continue;
		}

		// Only unlock monsters that are eligible for current or near-future waves
		// Reduced from +10 to +5 to prevent excessive unlocking at high waves
		// Don't unlock wave 999 bosses (full bosses should only come from boss waves)
		if (monster.minWave > current_wave + 5 || monster.minWave >= 999) {
			continue;
		}

		// Unlock this monster - it shares the model so it's "free"
		MarkMonsterTypePrecached(monster.typeId, monster_model_path);

		// Remove from exclusion list if it was excluded
		auto it = g_excluded_monsters_this_map.find(monster.typeId);
		if (it != g_excluded_monsters_this_map.end()) {
			g_excluded_monsters_this_map.erase(it);
			unlocked_count++;
		}

		if (developer->integer) {
			gi.Com_PrintFmt("Model Family Unlock: '{}' (wave {}) unlocked via boss '{}' (shared model: {})\n",
				horde::MonsterTypeRegistry::GetClassname(monster.typeId),
				monster.minWave,
				horde::MonsterTypeRegistry::GetClassname(boss_typeId),
				boss_model_path);
		}
	}

	if (developer->integer && unlocked_count > 0) {
		gi.Com_PrintFmt("Model Family Unlock: {} previously-excluded monsters unlocked (free precache via model sharing)\n",
			unlocked_count);
	}
}

// Initialize monster rotation for a new map
// Helper function: Get total usage count of a family in previous maps
static int32_t GetFamilyUsageInPreviousMaps(AssetFamilyID family_id)
{
	if (family_id == AssetFamilyID::UNKNOWN_FAMILY)
		return 0;

	int32_t total_usage = 0;
	for (const auto& map_usage : g_map_family_history)
	{
		if (map_usage.map_seed > 0) // Valid entry
		{
			total_usage += map_usage.family_usage_counts[static_cast<size_t>(family_id)];
		}
	}
	return total_usage;
}

static void RecordCurrentMapFamilyUsageToHistory()
{
	if (g_map_rotation_seed <= 0)
		return;

	MapFamilyUsage current_map_usage;
	current_map_usage.map_seed = g_map_rotation_seed;

	// Count how many times each family appeared in spawn history
	bool has_usage = false;
	for (const auto& entry : g_spawn_history)
	{
		if (entry.family_id != AssetFamilyID::UNKNOWN_FAMILY)
		{
			current_map_usage.family_usage_counts[static_cast<size_t>(entry.family_id)]++;
			has_usage = true;
		}
	}

	if (!has_usage)
		return;

	// Store in history ring buffer
	g_map_family_history[g_map_history_index] = current_map_usage;
	g_map_history_index = (g_map_history_index + 1) % MAP_HISTORY_SIZE;

	if (developer->integer && g_map_rotation_seed > 0)
	{
		gi.Com_PrintFmt("Saving map {} family usage to history (index {})\n",
			g_map_rotation_seed, g_map_history_index);
	}
}

static void InitializeMonsterRotation()
{
	g_excluded_monsters_this_map.clear();
	g_precached_monsters_this_map.clear();
	g_precached_models_this_map.clear();
	g_precached_families_this_map.clear();  // Reset family tracking for new map
	g_map_rotation_seed++;
	g_last_precache_wave = 0;

	// Pre-mark core families as "will be precached" to reserve their slots
	for (const auto& core_family : CORE_FAMILIES) {
		g_precached_families_this_map.insert(core_family);
	}

	// Select which rotating families are available this map based on seed
	// This ensures variety across maps while staying within the limit
	boost::container::small_vector<AssetFamilyID, 14> available_rotating;
	size_t rotation_offset = static_cast<size_t>(g_map_rotation_seed) % ROTATING_FAMILIES.size();

	for (size_t i = 0; i < PrecacheLimits::ROTATING_FAMILY_SLOTS && i < ROTATING_FAMILIES.size(); ++i) {
		size_t idx = (rotation_offset + i) % ROTATING_FAMILIES.size();
		available_rotating.push_back(ROTATING_FAMILIES[idx]);
	}

	// Pre-mark selected rotating families as allowed
	for (const auto& family : available_rotating) {
		g_precached_families_this_map.insert(family);
	}

	if (developer->integer) {
		gi.Com_PrintFmt("Precache Limit: Map {} - {} core + {} rotating = {} families allowed\n",
			g_map_rotation_seed,
			CORE_FAMILIES.size(),
			available_rotating.size(),
			g_precached_families_this_map.size());
		gi.Com_Print("  Rotating families this map: ");
		for (const auto& family : available_rotating) {
			gi.Com_PrintFmt("{} ", static_cast<int>(family));
		}
		gi.Com_Print("\n");
	}

	// Create a deterministic but rotating exclusion list
	// IMPROVED: Prefer excluding families that were heavily used in previous maps
	struct ExcludableMonsterInfo {
		horde::MonsterTypeID typeId;
		AssetFamilyID family;
		int32_t prev_map_usage; // How much this family was used in previous maps
	};
	boost::container::small_vector<ExcludableMonsterInfo, 32> excludable_monsters;

	// Build list of monsters that can be excluded (not bosses or critical monsters)
	for (size_t i = 0; i < MONSTER_DATA_COUNT; ++i) {
		const auto& monster = monsterTypes[i];

		// Never exclude bosses, semi-bosses, or wave 1-3 monsters (core monsters)
		// Also protect critical monsters that should always be available for variety
		if (monster.minWave > 3 &&
			!HasWaveType(monster.types, MonsterWaveType::Boss) &&
			!HasWaveType(monster.types, MonsterWaveType::SemiBoss) &&
			monster.typeId != horde::MonsterTypeID::MEDIC &&           // Always keep medics
			monster.typeId != horde::MonsterTypeID::GUNNER &&          // Always keep gunners
			monster.typeId != horde::MonsterTypeID::GUNNER_VANILLA &&  // Always keep vanilla gunners
			monster.typeId != horde::MonsterTypeID::FIXBOT &&          // Always keep fixbots (flying variety)
			monster.typeId != horde::MonsterTypeID::TANK &&            // Always keep tanks
			monster.typeId != horde::MonsterTypeID::TANK_COMMANDER) {  // Always keep tank commanders

			AssetFamilyID family = GetMonsterAssetFamily(monster.typeId);
			int32_t prev_usage = GetFamilyUsageInPreviousMaps(family);

			excludable_monsters.push_back({ monster.typeId, family, prev_usage });
		}
	}

	// Sort by previous usage (descending) - prefer excluding heavily used families
	std::sort(excludable_monsters.begin(), excludable_monsters.end(),
		[](const ExcludableMonsterInfo& a, const ExcludableMonsterInfo& b) {
			// Primary sort: Previous usage (higher = prefer to exclude)
			if (a.prev_map_usage != b.prev_map_usage)
				return a.prev_map_usage > b.prev_map_usage;
			// Secondary sort: Family ID for deterministic ordering
			return a.family < b.family;
		});

	// Exclude up to MONSTERS_TO_EXCLUDE_PER_MAP monsters
	// Biased toward families that were used in previous maps
	if (!excludable_monsters.empty()) {
		int to_exclude = std::min(MONSTERS_TO_EXCLUDE_PER_MAP, static_cast<int>(excludable_monsters.size()));

		// Mix: 70% from heavily-used families, 30% random rotation for variety
		int biased_count = static_cast<int>(to_exclude * 0.7f);
		int random_count = to_exclude - biased_count;

		// Add biased exclusions (from top of sorted list)
		for (int i = 0; i < biased_count && i < static_cast<int>(excludable_monsters.size()); i++) {
			g_excluded_monsters_this_map.insert(excludable_monsters[i].typeId);
		}

		// Add random rotation exclusions
		size_t rotation_offset = (static_cast<size_t>(g_map_rotation_seed) * 7) % excludable_monsters.size();
		for (int i = 0; i < random_count; i++) {
			size_t index = (rotation_offset + static_cast<size_t>(i) * 3) % excludable_monsters.size();
			g_excluded_monsters_this_map.insert(excludable_monsters[index].typeId);
		}
	}

	if (developer->integer) {
		gi.Com_PrintFmt("Monster Rotation: Map seed {}, excluding {} monsters (biased against overused families)\n",
			g_map_rotation_seed, g_excluded_monsters_this_map.size());

		// Show top 5 most used families from previous maps
		boost::container::small_vector<std::pair<AssetFamilyID, int32_t>, 24> family_usage;
		for (size_t i = 0; i < static_cast<size_t>(AssetFamilyID::MAX_FAMILIES); i++)
		{
			AssetFamilyID family = static_cast<AssetFamilyID>(i);
			int32_t usage = GetFamilyUsageInPreviousMaps(family);
			if (usage > 0)
				family_usage.push_back({ family, usage });
		}
		std::sort(family_usage.begin(), family_usage.end(),
			[](const auto& a, const auto& b) { return a.second > b.second; });

		gi.Com_Print("  Top families from previous maps: ");
		for (size_t i = 0; i < std::min(size_t(5), family_usage.size()); i++)
		{
			gi.Com_PrintFmt("{}({}) ", static_cast<int>(family_usage[i].first), family_usage[i].second);
		}
		gi.Com_Print("\n");
	}
}

// Check if we should precache more monsters this wave
static bool ShouldPrecacheMoreMonsters(int current_wave)
{
	// Gradually add more monsters every few waves
	if (current_wave > g_last_precache_wave + WAVES_BETWEEN_PRECACHE) {
		return true;
	}

	// Also ensure we have minimum variety available
	int available_count = 0;
	for (const auto* monster_info : g_eligible_monsters_for_wave) {
		if (g_precached_monsters_this_map.find(monster_info->typeId) != g_precached_monsters_this_map.end()) {
			available_count++;
		}
	}

	return available_count < MIN_MONSTERS_AVAILABLE;
}


// ============================================================================
// HORDE_INITLEVEL HELPER FUNCTIONS
// ============================================================================

// Reset wave state flags and spawn system for new wave
static void ResetWaveState(int32_t lvl)
{
	// Reset elite spawn dynamic precaching tracking for this wave
	g_last_dynamic_precache_wave = lvl;
	g_dynamic_precache_count_this_wave = 0;
	g_special_high_level_monster_spawned_this_wave = false;

	if (developer->integer)
	{
		gi.Com_PrintFmt("Horde_InitLevel: Wave {} started. Elite spawn flags RESET (count={}, flag={})\n",
			lvl, g_dynamic_precache_count_this_wave, g_special_high_level_monster_spawned_this_wave);
	}

	g_spawn_system.spawn_plan.clear();
	g_spawn_system.special_spawn_state.clear();
	g_horde_local.zero_monster_deployment_start = 0_sec;
	g_horde_retaliation_end_time = 0_sec;
	g_horde_retaliation_last_trigger_time = 0_sec;
	ResetChampionMonsterState();
	waves_since_ambush++;

	// Clear emergency spawn history - old positions from previous waves are irrelevant
	ResetEmergencySpawnHistory();

	// Initialize monster rotation and scaling for wave 1 (first wave of a new map)
	if (lvl == 1) {
		InitializeMonsterRotation();
		InitializeScalingSystem();
		monsters_precached = false;
		PrecacheAllMonsters();
	}
}

// Find monster info by type ID in the global monsterTypes array
static const MonsterTypeInfo* FindMonsterInfoByTypeId(horde::MonsterTypeID type_id)
{
	for (size_t i = 0; i < MONSTER_DATA_COUNT; ++i)
	{
		if (monsterTypes[i].typeId == type_id)
		{
			return &monsterTypes[i];
		}
	}
	return nullptr;
}

// Build eligible monster list for PVM mode (uses pre-selected random monsters)
static void BuildEligibleMonstersPVM(const horde::MonsterTypeID* pvm_monsters, int pvm_monster_count)
{
	for (int i = 0; i < pvm_monster_count; ++i)
	{
		const MonsterTypeInfo* monster_info = FindMonsterInfoByTypeId(pvm_monsters[i]);
		if (monster_info)
		{
			g_eligible_monsters_for_wave.push_back(monster_info);
		}
	}
}

// Build eligible monster list for normal horde mode (by wave progression)
static void BuildEligibleMonstersNormal(int32_t current_wave, MonsterWaveType wave_type)
{
	const int32_t max_level_for_eligibility = current_wave + MAX_EFFECTIVE_LEVEL_BOOST;

	for (size_t i = 0; i < MONSTER_DATA_COUNT; ++i)
	{
		const auto& monster = monsterTypes[i];

		// Use adjusted minWave with per-map variance for dynamic monster variety
		int32_t adjusted_min_wave = GetAdjustedMinWave(monster.typeId, g_map_rotation_seed);

		if (adjusted_min_wave > max_level_for_eligibility)
		{
			continue; // Changed from break to continue due to variance reordering
		}

		if (IsValidMonsterForWave(monster.typeId, wave_type))
		{
			if (g_excluded_monsters_this_map.find(monster.typeId) == g_excluded_monsters_this_map.end())
			{
				g_eligible_monsters_for_wave.push_back(&monster);
			}
		}
	}
}

// Add Gekk or Berserk variants for special waves (wave 15+)
static void AddSpecialWaveMonsters(int32_t lvl, MonsterWaveType wave_type)
{
	if (lvl < 15)
		return;

	bool is_gekk_wave = HasWaveType(wave_type, MonsterWaveType::Gekk);
	bool is_berserk_wave = HasWaveType(wave_type, MonsterWaveType::Berserk);

	if (!is_gekk_wave && !is_berserk_wave)
		return;

	horde::MonsterTypeID regular_type = is_gekk_wave
		? horde::MonsterTypeID::GEKK
		: horde::MonsterTypeID::BERSERK;
	horde::MonsterTypeID boss_type = is_gekk_wave
		? horde::MonsterTypeID::GEKKKL
		: horde::MonsterTypeID::BERSERKERKL;

	// Calculate spawn weights - heavily favor the main themed monsters
	int num_regular = 50;
	int num_bosses = 30;
	if (lvl >= 20)
	{
		num_bosses = std::min(60, 30 + (lvl - 20) / 2);
	}

	// Add regular monster (gekk or berserk)
	const MonsterTypeInfo* regular_info = FindMonsterInfoByTypeId(regular_type);
	if (regular_info)
	{
		for (int j = 0; j < num_regular; ++j)
		{
			g_eligible_monsters_for_wave.push_back(regular_info);
		}
	}

	// Add boss variant (gekkkl or berserkerkl)
	const MonsterTypeInfo* boss_info = FindMonsterInfoByTypeId(boss_type);
	if (boss_info)
	{
		for (int j = 0; j < num_bosses; ++j)
		{
			g_eligible_monsters_for_wave.push_back(boss_info);
		}

		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("HORDE: Added {} regular + {} boss {} to spawn pool for wave {}\n",
				num_regular, num_bosses, is_gekk_wave ? "Gekks" : "Berserkers", lvl);
		}

		if (developer->integer > 1)
		{
			int regular_count = 0;
			int boss_count = 0;
			for (const auto* monster_info : g_eligible_monsters_for_wave)
			{
				if (monster_info->typeId == regular_type) regular_count++;
				if (monster_info->typeId == boss_type) boss_count++;
			}
			gi.Com_PrintFmt("DEBUG: Eligible pool now has {} regular {} and {} {}\n",
				regular_count, is_gekk_wave ? "Gekks" : "Berserkers",
				boss_count, is_gekk_wave ? "Gekkkls" : "Berserkkls");
		}
	}
}

// Build eligible items list for the current wave
static void BuildEligibleItems(int32_t lvl)
{
	for (size_t i = 0; i < g_hordeItemDataSoA.NUM_ITEMS; ++i)
	{
		if (lvl >= g_hordeItemDataSoA.minWaves[i])
		{
			g_eligible_item_indices_for_wave.push_back(i);
		}
	}
}

// Calculate precache cost for a monster based on whether its model is already loaded
static float CalculatePrecacheCost(horde::MonsterTypeID type_id)
{
	const char* model_path = GetMonsterModelPath(type_id);
	if (model_path && g_precached_models_this_map.find(model_path) != g_precached_models_this_map.end())
	{
		return 0.2f; // Model already loaded, just new skin - very cheap
	}
	return 1.0f; // Default cost for unique model
}

// Precache a single monster and track it
static bool PrecacheMonsterForWave(const MonsterTypeInfo* monster_info, float& precache_cost_this_wave, int& precached_count)
{
	const char* classname = horde::MonsterTypeRegistry::GetClassname(monster_info->typeId);
	if (!classname || !*classname)
		return false;

	float precache_cost = CalculatePrecacheCost(monster_info->typeId);

	edict_t* temp_monster = G_Spawn();
	if (!temp_monster)
		return false;

	temp_monster->classname = classname;
	temp_monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
	ED_CallSpawnMonsterByID(temp_monster, monster_info->typeId);
	G_FreeEdict(temp_monster);

	const char* model_path = GetMonsterModelPath(monster_info->typeId);
	MarkMonsterTypePrecached(monster_info->typeId, model_path);

	precache_cost_this_wave += precache_cost;
	precached_count++;
	return true;
}

// Progressive precache - first pass: current wave monsters
// Respects family limit - only precaches monsters whose families are allowed
static void PrecacheCurrentWaveMonsters(int32_t lvl, float precache_budget, float& precache_cost_this_wave, int& precached_count)
{
	for (const MonsterTypeInfo* monster_info : g_eligible_monsters_for_wave)
	{
		if (g_precached_monster_types_flags[static_cast<size_t>(monster_info->typeId)])
		{
			MarkMonsterTypePrecached(monster_info->typeId);
			continue;
		}

		// Check family limit - skip if family not allowed for this map
		AssetFamilyID family = GetMonsterAssetFamily(monster_info->typeId);
		if (g_precached_families_this_map.find(family) == g_precached_families_this_map.end())
		{
			continue; // Family not allowed, skip silently
		}

		float precache_cost = CalculatePrecacheCost(monster_info->typeId);

		// Priority precache for monsters within a reasonable window
		if (monster_info->minWave <= lvl && monster_info->minWave >= lvl - 10 &&
			precache_cost_this_wave + precache_cost <= precache_budget)
		{
			if (developer->integer)
			{
				const char* classname = horde::MonsterTypeRegistry::GetClassname(monster_info->typeId);
				gi.Com_PrintFmt("Progressive Precache: Loading '{}' for wave {} (cost: {:.1f})\n",
					classname ? classname : "unknown", lvl, precache_cost);
			}
			PrecacheMonsterForWave(monster_info, precache_cost_this_wave, precached_count);
		}
	}
}

// Progressive precache - second pass: future wave monsters
// Respects family limit - only precaches monsters whose families are allowed
static void PrecacheFutureWaveMonsters(int32_t lvl, float precache_budget, float& precache_cost_this_wave, int& precached_count)
{
	if (precache_cost_this_wave >= precache_budget || !ShouldPrecacheMoreMonsters(lvl))
		return;

	for (const MonsterTypeInfo* monster_info : g_eligible_monsters_for_wave)
	{
		if (g_precached_monster_types_flags[static_cast<size_t>(monster_info->typeId)])
			continue;

		// Check family limit - skip if family not allowed for this map
		AssetFamilyID family = GetMonsterAssetFamily(monster_info->typeId);
		if (g_precached_families_this_map.find(family) == g_precached_families_this_map.end())
		{
			continue; // Family not allowed, skip silently
		}

		float precache_cost = CalculatePrecacheCost(monster_info->typeId);

		// Precache upcoming monsters within a narrow window
		if (monster_info->minWave > lvl && monster_info->minWave <= lvl + 3 &&
			precache_cost_this_wave + precache_cost <= precache_budget)
		{
			if (developer->integer)
			{
				const char* classname = horde::MonsterTypeRegistry::GetClassname(monster_info->typeId);
				gi.Com_PrintFmt("Progressive Precache: Loading '{}' for future waves (cost: {:.1f})\n",
					classname ? classname : "unknown", precache_cost);
			}
			PrecacheMonsterForWave(monster_info, precache_cost_this_wave, precached_count);
		}
	}
	g_last_precache_wave = lvl;
}

// Progressive precache logic - handles both current and future wave precaching
static void ProgressivePrecacheMonsters(int32_t lvl)
{
	// Increased budget for more monster variety
	float precache_budget = 10.0f + (lvl / 2.0f);  // Increased from 6.0f + lvl/3.0f
	float precache_cost_this_wave = 0.0f;
	int precached_count = 0;

	// First pass: precache critical monsters for this wave
	PrecacheCurrentWaveMonsters(lvl, precache_budget, precache_cost_this_wave, precached_count);

	// Second pass: precache future monsters if we have budget remaining
	PrecacheFutureWaveMonsters(lvl, precache_budget, precache_cost_this_wave, precached_count);

	if (developer->integer)
	{
		gi.Com_PrintFmt("Progressive Precache: {} monsters precached (cost: {:.1f}), {} total, {} models loaded\n",
			precached_count, precache_cost_this_wave, g_precached_monsters_this_map.size(),
			g_precached_models_this_map.size());
	}
}

// Finalize wave setup - map size, timers, spawn rates, rewards
static void FinalizeWaveSetup(int32_t lvl, int32_t numHumanPlayers)
{
	g_horde_local.update_map_size(GetCurrentMapName());
	GetAdjustedMonsterCap(g_horde_local.current_map_size, lvl, GetCurrentMapName());

	g_independent_timer_start = level.time;
	last_wave_number++;
	boss_spawned_for_wave = false;
	next_wave_message_sent = false;

	CleanupSpawnPointCache();
	VerifyAndAdjustBots();
	G_UpdateAdrenalineBasedDeployables(current_wave_level);

	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTriggered = false;
	g_horde_local.waveEndTime = 0_sec;
	g_horde_local.lastPrintTime = 0_sec;
	g_lastParams = GetConditionParams(g_horde_local.current_map_size, numHumanPlayers, lvl);

	for (size_t i = 0; i < WARNING_TIMES.size(); i++)
	{
		g_horde_local.warningIssued[i] = false;
	}

	UnifiedAdjustSpawnRate(g_horde_local.current_map_size, lvl, numHumanPlayers);

	int32_t total_planned_for_wave = g_horde_local.num_to_spawn + g_horde_local.queued_monsters;
	g_totalMonstersInWave = static_cast<uint16_t>(
		std::min(total_planned_for_wave, static_cast<int32_t>(std::numeric_limits<uint16_t>::max())));
}

// Apply legacy damage scaling for specific waves (when sigmoid scaling is disabled)
static void ApplyLegacyDamageScaling(int32_t lvl)
{
	if (g_config.use_sigmoid_scaling)
		return;

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
}

static void Horde_InitLevel(const int32_t lvl)
{
	// Cache player count for performance (used multiple times in this function)
	const int32_t numHumanPlayers = GetNumHumanPlayers();

	// Build the spawn point map once, right before the first wave
	if (g_spawn_system.spawn_map_needs_build) {
		BuildSpawnPointMap();
		g_spawn_system.spawn_map_needs_build = false;
	}

	// --- 1. Reset wave state and flags ---
	ResetWaveState(lvl);

	// --- 2. Set up the new wave's parameters ---
	g_horde_local.level = lvl;
	current_wave_level = lvl;

	// Auto-enable network optimization at wave 25+
	if (lvl >= 25 && !g_nolag->integer) {
		gi.cvar_set("g_nolag", "1");
	}

	// Update g_start_items for this wave's loadout
	Horde_UpdateStartItemsForWave(lvl);

	// Determine the wave type. Boss waves start with no type; it's set when the boss spawns.
	if (!(lvl >= 10 && lvl % 5 == 0))
	{
		InitializeWaveType(lvl);
	}
	else
	{
		current_wave_type = MonsterWaveType::None;
	}

	// --- 3. Build eligible monsters list ---
	g_eligible_monsters_for_wave.clear();
	g_eligible_item_indices_for_wave.clear();

	const horde::MonsterTypeID* pvm_monsters = PVM_GetRandomMonsters();
	const int pvm_monster_count = PVM_GetRandomMonsterCount();

	if (pvm_monsters)
	{
		// PVM Mode: Use pre-selected random monsters
		BuildEligibleMonstersPVM(pvm_monsters, pvm_monster_count);
	}
	else
	{
		// Normal Horde Mode: Build eligible list by wave progression
		BuildEligibleMonstersNormal(current_wave_level, current_wave_type);

		// Add Gekk/Berserk variants for special waves (wave 15+)
		if (lvl >= 15 && (HasWaveType(current_wave_type, MonsterWaveType::Gekk) ||
			HasWaveType(current_wave_type, MonsterWaveType::Berserk)))
		{
			AddSpecialWaveMonsters(lvl, current_wave_type);
		}
	}

	// --- 4. Build eligible items list ---
	BuildEligibleItems(lvl);

	// --- 5. Progressive precache logic (skip for PVM mode) ---
	if (!pvm_monsters)
	{
		ProgressivePrecacheMonsters(lvl);
	}

	if (developer->integer)
	{
		gi.Com_PrintFmt("JIT Precache: All necessary monsters for wave {} are now in memory.\n", lvl);
	}

	// --- 6. Finalize wave setup ---
	FinalizeWaveSetup(lvl, numHumanPlayers);

	// --- 7. Apply damage scaling and process rewards ---
	ApplyLegacyDamageScaling(lvl);
	ProcessWaveRewards(lvl);
	Horde_CleanBodies();
}
