//#pragma once
//
//#ifndef HORDE_SPAWNING_H
//#define HORDE_SPAWNING_H
//
//#include <array> // Needed for std::array in HordeConstants
//#include "../q_vec3.h" // Needed for vec3_t
//#include "../game.h" // Needed for gtime_t
//
//// --- Horde Mode Constants ---
//namespace HordeConstants {
//
//	// --- Gameplay Balance ---
//	constexpr float TIME_REDUCTION_MULTIPLIER = 0.95f;     // General multiplier for reducing certain timers
//	constexpr float BASE_DIFFICULTY_MULTIPLIER = 1.1f;     // Base multiplier for player count scaling
//	constexpr float PLAYER_COUNT_SCALE = 0.2f;         // How much each additional player scales difficulty/counts
//
//	// --- Monster Counts & Caps ---
//	constexpr int8_t MAX_MONSTERS_BIG_MAP = 32;           // Default max monsters for large maps
//	constexpr int8_t MAX_MONSTERS_MEDIUM_MAP = 16;        // Default max monsters for medium maps
//	constexpr int8_t MAX_MONSTERS_SMALL_MAP = 14;         // Default max monsters for small maps
//	// Base counts for different map sizes and early wave levels (Level Ranges: <=5, <=10, <=15, >15)
//	constexpr std::array<std::array<int32_t, 4>, 3> BASE_COUNTS = { {
//		{{6, 8, 10, 12}},  // Small maps
//		{{8, 12, 14, 16}}, // Medium maps
//		{{15, 18, 23, 26}} // Large maps
//	} };
//	// Base additional spawns per wave (Small, Medium, Large) applied from wave 8+
//	constexpr std::array<int32_t, 3> ADDITIONAL_SPAWNS = { 8, 7, 12 };
//
//	// --- Spawn Point Cooldowns ---
//	// Note: SPAWN_POINT_COOLDOWN (the global variable) is calculated dynamically
//	constexpr gtime_t MIN_GLOBAL_SPAWN_COOLDOWN = 1.5_sec;       // Absolute minimum for the dynamic SPAWN_POINT_COOLDOWN
//	constexpr gtime_t MIN_INDIVIDUAL_SUCCESS_COOLDOWN = 0.5_sec; // Min time a point is disabled after *successful* direct spawn
//	constexpr gtime_t MIN_INDIVIDUAL_FAILURE_COOLDOWN = 0.5_sec; // Min time a point is disabled after *failed* direct spawn attempts
//	constexpr gtime_t MIN_REDUCED_INDIVIDUAL_COOLDOWN = 0.5_sec; // Min cooldown when reduced late-wave (CheckAndReduceSpawnCooldowns)
//
//	// --- Alternative Spawn Position Cooldowns & Logic ---
//	constexpr gtime_t ALT_SPAWN_COOLDOWN_SHORT = 1.5_sec;         // Cooldown after 1-2 failed alternative position attempts
//	constexpr gtime_t ALT_SPAWN_COOLDOWN_MEDIUM = 3.0_sec;        // Cooldown after 3-5 failed alternative position attempts
//	// Min cooldowns applied after alternative spawn success/failure
//	constexpr gtime_t MIN_ALT_SUCCESS_COOLDOWN = 1.0_sec;
//	constexpr gtime_t MIN_ALT_FAILURE_COOLDOWN = 1.0_sec;
//	// Predefined offsets tried by TryAlternativeSpawnPosition
//	constexpr size_t NUM_HORDE_ALT_POSITIONS = 8;
//	constexpr std::array<vec3_t, NUM_HORDE_ALT_POSITIONS> horde_alternative_positions = {
//		vec3_t{ 40, 0, 8 },  vec3_t{ -40, 0, 8 },
//		vec3_t{ 0, 40, 8 },  vec3_t{ 0, -40, 8 },
//		vec3_t{ 30, 30, 0 }, vec3_t{ -30, 30, 0 },
//		vec3_t{ 30, -30, 0 }, vec3_t{ -30, -30, 0 }
//	};
//
//	// --- Monster Spawning Timing ---
//	constexpr gtime_t MIN_MONSTER_SPAWN_INTERVAL = 0.8_sec;      // Absolute minimum time between any two monster spawns (SetNextMonsterSpawnTime)
//
//	// --- Stuck Monster / Teleport Logic ---
//	constexpr gtime_t STUCK_CHECK_TIME = 10_sec;                   // How long monster must be stuck before teleport check
//	constexpr gtime_t NO_DAMAGE_TIMEOUT = 25_sec;
//	// CONSOLIDATED: Cooldown applied *to the monster* after any teleport (stuck or boss)
//	constexpr gtime_t MIN_TELEPORT_COOLDOWN_MONSTER = 12_sec;
//	constexpr gtime_t MAX_TELEPORT_COOLDOWN_MONSTER = 20_sec;
//	// Global rate limiting for stuck monster teleports
//	constexpr gtime_t GLOBAL_TELEPORT_RESET_INTERVAL = 8_sec;    // Interval to reset the rate limit counter
//	constexpr int MAX_TELEPORTS_PER_INTERVAL = 2;                // Max stuck teleports allowed within the interval (adjusted for difficulty)
//	// Rate limit counter and timer (defined as static within the namespace)
//	// These need to be extern if defined in .cpp, or defined here if truly constant/static
//	// extern int g_teleport_rate_count; // If defined in .cpp
//	// extern gtime_t g_teleport_rate_reset_time; // If defined in .cpp
//	// extern int recent_teleport_count; // If defined in .cpp
//	// For simplicity, let's assume they are managed within the .cpp for now
//
//	// --- Proximity / Distance Checks ---
//	constexpr vec3_t VALIDATE_CHECK_MINS = { -16, -16, -24 };     // Default mins for IsValidSpawnLocation fallback
//	constexpr vec3_t VALIDATE_CHECK_MAXS = { 16,  16,  32 };      // Default maxs for IsValidSpawnLocation fallback
//	// Emergency Spawn Position Generation
//	constexpr float MIN_PLAYER_DIST_GENERATE = 200.0f;           // Min distance from player when *generating* emergency spots
//	// Minimum Distances (Final Checks) - Use squared values for performance
//	constexpr float MIN_PLAYER_DIST_CHECK = 180.0f;              // Min final distance from player for *emergency* spawns
//	constexpr float MIN_PLAYER_DIST_SQ_CHECK = MIN_PLAYER_DIST_CHECK * MIN_PLAYER_DIST_CHECK;
//	constexpr float MIN_PLAYER_DIST_SPAWNPOINT = 150.0f;         // Min distance from player for *regular* spawn point filtering
//	constexpr float MIN_PLAYER_DIST_SQ_SPAWNPOINT = MIN_PLAYER_DIST_SPAWNPOINT * MIN_PLAYER_DIST_SPAWNPOINT;
//	// Recent Spawn/Teleport Proximity
//	constexpr float MIN_RECENT_SPAWN_DIST = 60.0f;               // Min distance between sequential regular/alternative spawns
//	constexpr float MIN_RECENT_SPAWN_DIST_SQ = MIN_RECENT_SPAWN_DIST * MIN_RECENT_SPAWN_DIST;
//	constexpr float MIN_RECENT_TELEPORT_DIST = 250.0f;           // Min distance between sequential *teleport* destinations
//	constexpr float MIN_RECENT_TELEPORT_DIST_SQ = MIN_RECENT_TELEPORT_DIST * MIN_RECENT_TELEPORT_DIST;
//	constexpr gtime_t RECENT_SPAWN_COOLDOWN = 3.0_sec;           // How long a regular spawn position affects proximity checks
//	constexpr gtime_t RECENT_TELEPORT_COOLDOWN = 5.0_sec;        // How long a teleport destination affects proximity checks
//
//	// --- Failure Recovery ---
//	constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY = 5;  // Threshold to enter recovery mode (simplify wave type)
//	constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY = 10; // Threshold to trigger emergency spawning near players
//
//	// --- Wave Timing ---
//	// For calculate_max_wave_time
//	constexpr gtime_t BASE_MAX_WAVE_TIME = 105_sec;              // Increased base time limit for waves
//	constexpr gtime_t TIME_INCREASE_PER_LEVEL = 1.8_sec;         // How much max time increases per wave
//	constexpr gtime_t BOSS_TIME_BONUS = 60_sec;                  // Extra time added for boss waves
//	// For CheckRemainingMonstersCondition (aggressive timer)
//	constexpr int MONSTERS_FOR_AGGRESSIVE_REDUCTION = 6;       // Trigger aggressive timer when <= this many monsters remain
//
//	// --- Boss Spawning Limits ---
//	constexpr size_t MAX_ELIGIBLE_BOSSES = 16; // Max bosses considered for a wave
//	constexpr size_t MAX_RECENT_BOSSES = 4;    // History size to prevent boss repetition
//
//} // namespace HordeConstants
//
//
//// Declarations for spawning-related functions and variables will go here.
//// Forward declare MapSize if needed, or include the header where it's defined
//namespace horde { struct MapSize; }
//
//// Define ConditionParams struct (moved from g_horde.cpp)
//struct ConditionParams {
//	int32_t maxMonsters;
//	gtime_t timeThreshold;
//	gtime_t lowPercentageTimeThreshold;
//	gtime_t independentTimeThreshold;
//	float lowPercentageThreshold;
//	float aggressiveTimeReductionThreshold;
//
//	ConditionParams() noexcept :
//		maxMonsters(0),
//		timeThreshold(0_sec),
//		lowPercentageTimeThreshold(0_sec),
//		independentTimeThreshold(0_sec),
//		lowPercentageThreshold(0.3f),
//		aggressiveTimeReductionThreshold(0.3f) {
//	}
//};
//
//
//// Function declarations moved from g_horde.cpp / g_horde.h
//void CheckAndReduceSpawnCooldowns();
//constexpr gtime_t GetBaseSpawnCooldown(bool isSmallMap, bool isBigMap);
//float CalculateCooldownScale(int32_t lvl, const horde::MapSize& mapSize);
//int32_t GetAdjustedMonsterCap(const horde::MapSize& mapSize, int32_t waveLevel);
//void ClampNumToSpawn(const horde::MapSize& mapSize);
//int32_t CalculateQueuedMonsters(const horde::MapSize& mapSize, int32_t lvl, bool isHardMode) noexcept;
//void UnifiedAdjustSpawnRate(const horde::MapSize& mapSize, int32_t lvl, int32_t humanPlayers) noexcept;
//int32_t CalculateChaosInsanityBonus(int32_t lvl) noexcept; // Moved from g_horde.cpp
//
//// Other spawning related declarations...
//// ...
//
//#endif // HORDE_SPAWNING_H
