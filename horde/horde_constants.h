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
	inline constexpr int8_t MAX_MONSTERS_MEDIUM_MAP = 14;
	inline constexpr int8_t MAX_MONSTERS_SMALL_MAP = 12;

	inline constexpr std::array<std::array<int32_t, 4>, 3> BASE_COUNTS = {{
		{{4, 6, 8, 9}},    // Small maps
		{{6, 9, 11, 12}},  // Medium maps
		{{11, 14, 18, 20}} // Large maps
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
	inline constexpr gtime_t SPAWN_INTERVAL = 1.5_sec;
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
	inline constexpr int32_t SPAWN_BATCH_SMALL_MAP = 4;
	inline constexpr int32_t SPAWN_BATCH_MEDIUM_MAP = 5;
	inline constexpr int32_t SPAWN_BATCH_BIG_MAP = 6;

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

	// Helper function for min player distance based on map size
	inline float GetMinPlayerDistSpawnpoint(const horde::MapSize& mapSize) {
		if (mapSize.isSmallMap) {
			return 100.0f; // Reduced from 150.0f for small maps
		} else if (mapSize.isMediumMap) {
			return 125.0f; // Slightly reduced for medium maps
		} else {
			return MIN_PLAYER_DIST_SPAWNPOINT_BASE; // Keep original for big maps
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

