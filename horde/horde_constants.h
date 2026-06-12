#pragma once

#include "../g_local.h"  // For gtime_t
#include "horde_ids.h"   // For horde::MapSize

// HordeConstants namespace - Shared constants used across horde system
namespace HordeConstants
{
	// --- Gameplay Balance ---
	inline constexpr float TIME_REDUCTION_MULTIPLIER = 0.95f;
	inline constexpr float BASE_DIFFICULTY_MULTIPLIER = 1.1f;
	inline constexpr float PLAYER_COUNT_SCALE = 0.2f;

	// --- Monster Counts & Caps ---
	inline constexpr int8_t MAX_MONSTERS_BIG_MAP = 26;
	inline constexpr int8_t MAX_MONSTERS_MEDIUM_MAP = 15;
	inline constexpr int8_t MAX_MONSTERS_SMALL_MAP = 12;

	// RESTORED from 0.995: More aggressive early wave monster counts
	inline constexpr std::array<std::array<int32_t, 4>, 3> BASE_COUNTS = {{
		{{4, 6, 8, 9}},    // Small maps (restored from 0.995)
		{{6, 9, 11, 12}},  // Medium maps (restored from 0.995)
		{{11, 14, 18, 20}} // Large maps (restored from 0.995)
	}};
	inline constexpr std::array<int32_t, 3> ADDITIONAL_SPAWNS = {6, 5, 9};

	// --- Spawn Point Cooldowns ---
	inline constexpr gtime_t MIN_GLOBAL_SPAWN_COOLDOWN = 1.5_sec;
	inline constexpr gtime_t MIN_LOCAL_SPAWN_COOLDOWN = 3.0_sec;
	inline constexpr gtime_t RECENT_SPAWN_COOLDOWN = 5.0_sec;

	// --- Player Proximity & Distances ---
	inline constexpr float MIN_PLAYER_DIST_SPAWNPOINT_BASE = 150.0f;
	inline constexpr float MIN_RECENT_SPAWN_DIST_BASE = 120.0f;
	inline constexpr int32_t RECENT_SPAWN_BUFFER_SIZE = 20;

	// --- Spawn Timing ---
	// RESTORED from 0.995: Faster spawn interval for more intense gameplay
	inline constexpr gtime_t SPAWN_INTERVAL = 1.5_sec;  // Restored from 0.995 (was 2.0s)
	inline constexpr gtime_t MIN_MONSTER_SPAWN_INTERVAL = 0.5_sec;  // Reduced from 1.0s for faster spawning
	inline constexpr gtime_t MIN_WAVE_TIME = 30_sec;
	inline constexpr gtime_t WAVE_COMPLETE_GRACE_PERIOD = 1_sec;
	inline constexpr gtime_t FOG_PERSIST_TIME = 30_sec;
	inline constexpr gtime_t AMBUSH_DURATION = 5_sec;
	inline constexpr gtime_t AMBUSH_PREP_TIME = 1_sec;

	// --- Spawn Failure & Recovery ---
	inline constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY = 5;
	inline constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY = 10;
	inline constexpr int MAX_CONSECUTIVE_EMERGENCY_FAILURES_BEFORE_RESET = 20;  // Reset recovery mode after this many failed emergency spawns
	inline constexpr int MAX_SPAWN_ATTEMPTS_PER_CYCLE = 50;
	inline constexpr int MAX_UNSTICK_ATTEMPTS = 3;

	// --- Spawn Batch Sizes ---
	// RESTORED from 0.995: Larger batches for more intense gameplay
	inline constexpr int32_t SPAWN_BATCH_SMALL_MAP = 4;   // Restored from 0.995
	inline constexpr int32_t SPAWN_BATCH_MEDIUM_MAP = 5;  // Restored from 0.995
	inline constexpr int32_t SPAWN_BATCH_BIG_MAP = 6;     // Restored from 0.995

	// --- Early Wave Warmup (Slower Start) ---
	// REDUCED: 0.995 had no warmup, keeping minimal warmup for smoother start
	inline constexpr int32_t EARLY_WAVE_WARMUP_END = 4;        // Reduced from 8 - only waves 1-3 are slower
	inline constexpr float EARLY_WAVE_SLOW_MULTIPLIER = 1.2f;  // Reduced from 1.6 - only 20% slower on wave 1
	inline constexpr float WAVE_SPEED_INCREASE_PER_WAVE = 0.1f; // Each wave gets 10% faster until normal
	inline constexpr float SMALL_MAP_EXTRA_SLOWDOWN = 1.1f;    // Reduced from 1.25 - only 10% extra slowdown

	// --- Emergency Spawn Settings ---
	inline constexpr int32_t EMERGENCY_SPAWN_LIMIT_PER_CALL = 3;
	inline constexpr float EMERGENCY_MIN_BATCH_SPACING = 280.0f;  // Increased from 150 to prevent clustering
	inline constexpr int MAX_EMERGENCY_POSITION_ATTEMPTS = 25;     // Increased from 10 for better coverage
	inline constexpr int32_t EMERGENCY_GUNNER_LEVEL_THRESHOLD = 10;
	inline constexpr int32_t EMERGENCY_TANK_LEVEL_THRESHOLD = 15;
	inline constexpr int32_t EMERGENCY_GLADIATOR_LEVEL_THRESHOLD = 20;

	// --- Alternative Spawn Position Settings ---
	inline constexpr int ALTERNATIVE_RADIAL_ATTEMPTS = 35;
	inline constexpr float ALTERNATIVE_MIN_RADIUS = 40.0f;
	inline constexpr float ALTERNATIVE_MAX_RADIUS = 225.0f;
	inline constexpr float ALTERNATIVE_MIN_Z_OFFSET = -8.0f;
	inline constexpr float ALTERNATIVE_MAX_Z_OFFSET = 24.0f;

	// --- Spawn Validation Caching ---
	inline constexpr gtime_t SPAWN_VALIDATION_CACHE_DURATION = 100_ms;

	// --- Boss & Special ---
	inline constexpr int BOSS_EVERY_N_WAVES = 5;
	inline constexpr float BOSS_HEALTH_MULTIPLIER = 2.0f;
	inline constexpr float BOSS_DAMAGE_MULTIPLIER = 1.5f;
	inline constexpr float CHAMPION_BASE_CHANCE = 0.15f;
	inline constexpr float CHAMPION_LEVEL_INCREASE = 0.02f;

	// --- Wave Types ---
	inline constexpr float SPECIAL_WAVE_CHANCE = 0.25f;
	inline constexpr float AMBUSH_CHANCE_PER_WAVE = 0.03f;

	// --- Stuck Monster Cleanup (100% Wave Elimination) ---
	// When spawning is done and few monsters remain, start a cleanup timer
	// After timeout, force-kill stuck monsters instead of ending wave early
	inline constexpr int32_t STROGG_THRESHOLD = 4;              // Start cleanup when this many or fewer remain
	inline constexpr gtime_t STROGG_GRACE_PERIOD = 20_sec;      // Time to let players find stroggs naturally
	inline constexpr gtime_t STROGG_FORCE_KILL_INTERVAL = 2_sec; // Kill one stuck monster every N seconds after grace
	inline constexpr float VOID_Z_THRESHOLD = -4096.0f;            // Monsters below this Z are considered in the void

	// --- Flying Monster Stuck Handling ---
	// Teleport flying monsters that remain wall-blocked beyond this threshold.
	inline constexpr gtime_t FLY_WALL_STUCK_TELEPORT_TIME = 3_sec;

	// --- Grid Spawn Cooldown Settings ---
	inline constexpr gtime_t GRID_POSITION_COOLDOWN = 4.0_sec;        // Cooldown before same grid area can be used again
	inline constexpr float GRID_COOLDOWN_RADIUS = 200.0f;             // Radius around used position that's on cooldown
	inline constexpr size_t MAX_GRID_COOLDOWN_POSITIONS = 32;         // Max tracked positions in cooldown buffer

	// --- Tactical Spawn Distance Settings ---
	// Doubled minimum distances to prevent spawning too close to players
	inline constexpr float TELEPORT_MIN_DIST_FROM_PLAYER = 600.0f;    // Min distance for monster teleports (doubled from 400)
	inline constexpr float EMERGENCY_MIN_DIST_FROM_PLAYER = 700.0f;   // Min distance for emergency/ambush spawns (doubled from 500)
	inline constexpr float REGULAR_SPAWN_MIN_DIST_FROM_PLAYER = 500.0f; // Min distance for regular wave spawns (doubled from 350)

	// --- Out-of-Visibility Spawn Settings ---
	// 25% chance to force spawns in areas not visible to players
	inline constexpr float OUT_OF_VISIBILITY_CHANCE = 0.25f;

	// Helper function for min player distance based on map size
	// UPDATED: Increased distances to give players more reaction time
	inline float GetMinPlayerDistSpawnpoint(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 300.0f; // Doubled from 150.0f for small maps
		} else if (mapSize.isMediumMap) {
			return 400.0f; // Doubled from 200.0f for medium maps
		} else {
			return 500.0f; // Doubled from 250.0f for big maps
		}
	}

	// Helper function for min recent spawn distance based on map size
	inline float GetMinRecentSpawnDist(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 80.0f; // Reduced from 120.0f for small maps
		} else if (mapSize.isMediumMap) {
			return 100.0f; // Slightly reduced for medium maps
		} else {
			return MIN_RECENT_SPAWN_DIST_BASE; // Keep original for big maps
		}
	}

} // namespace HordeConstants

// Wave unlock variance settings - adds randomness to when monsters first appear
namespace MonsterUnlockVariance {
	inline constexpr int32_t BASE_VARIANCE = 2;         // +/- 2 waves variance
	inline constexpr int32_t MIN_WAVE_FOR_VARIANCE = 4; // Don't apply to wave 1-3 monsters
	inline constexpr uint32_t VARIANCE_SEED_MULTIPLIER = 17; // For deterministic per-map variance
}

namespace PrecacheLimits {
	// If system memory allows, slightly increase the limit to accommodate more concurrent active models
	inline constexpr int32_t MAX_PRECACHED_MODEL_FAMILIES = 24;

	// Shrink core slots from 14 down to 6
	inline constexpr int32_t CORE_FAMILY_SLOTS = 6;

	// Expand rotating slots to fill the remaining budget (24 max - 6 core = 18 rotating)
	inline constexpr int32_t ROTATING_FAMILY_SLOTS = 18;
}
