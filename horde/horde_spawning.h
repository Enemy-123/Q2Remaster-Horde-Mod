#pragma once

#include "horde_ids.h"
#include "../shared.h"

// Forward declarations (edict_t is already defined in shared.h)

namespace horde {
    struct MapSize;
}

// Forward declarations for enums used in structs
enum class SpecialSpawnType {
    None,
    Ambush,
    Retaliation
};

// Special spawn state
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

// Forward declaration for spawn plan entry
struct SpawnPlanEntry {
    horde::MonsterTypeID typeId;
    edict_t* spawn_point;
};

// Spawn points data using SoA (Structure of Arrays) for cache efficiency
struct SpawnPointsSoA {
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
    void resize(size_t new_size);

    // Helper to clear all data
    void clear() {
        resize(0);
    }
};

// Cached spawn point validation data
struct CachedSpawnPointData {
    uint16_t index = 0;
    gtime_t last_validation_time = 0_sec;
    bool last_validation_result = false;
    vec3_t last_validation_origin = vec3_origin;

    // Reset cache when position changes
    void InvalidateIfMoved(const vec3_t& current_origin) {
        if ((current_origin - last_validation_origin).lengthSquared() > 1.0f) {
            last_validation_time = 0_sec;
        }
    }
};

// ============================================================================
// SPAWN SYSTEM STATE (Encapsulation of global spawn system state)
// ============================================================================

/// Central state structure for the spawn system
/// Groups all spawn-related state variables for better encapsulation and testability
struct SpawnSystemState {
    // --- Cache State ---
    std::vector<edict_t*> potential_spawn_points;
    bool spawn_points_cached = false;
    size_t spawn_point_shuffle_index = 0;
    int32_t cached_flying_spawn_count = 0;

    // --- Spawn Point Data ---
    boost::container::flat_map<int, uint16_t> spawn_point_map;  // C++23 flat_map for better cache locality
    SpawnPointsSoA spawn_points_data;
    std::vector<CachedSpawnPointData> spawn_validation_cache;

    // --- Failure Tracking & Recovery ---
    int32_t consecutive_spawn_failures = 0;
    MonsterWaveType original_wave_type_before_recovery = MonsterWaveType::None;

    // --- Reset Flags ---
    bool need_spawn_cache_reset = false;
    bool spawn_map_needs_build = true;

    // --- Special Spawns ---
    SpecialSpawnState special_spawn_state;

    // --- Current Spawn Plan ---
    std::vector<SpawnPlanEntry> spawn_plan;
    float champion_chance_for_current_batch = 0.2f;

    // Reset all state (useful for testing and level transitions)
    void Reset() {
        potential_spawn_points.clear();
        spawn_points_cached = false;
        spawn_point_shuffle_index = 0;
        cached_flying_spawn_count = 0;

        spawn_point_map.clear();
        spawn_points_data.clear();
        spawn_validation_cache.clear();

        consecutive_spawn_failures = 0;
        original_wave_type_before_recovery = MonsterWaveType::None;

        need_spawn_cache_reset = false;
        spawn_map_needs_build = true;

        special_spawn_state.clear();

        spawn_plan.clear();
        champion_chance_for_current_batch = 0.2f;
    }
};

// Global spawn system state instance
extern SpawnSystemState g_spawn_system;

// ============================================================================
// CORE MONSTER SPAWNING FUNCTIONS
// ============================================================================

// Creates a monster entity of the specified type at the given location
// link_immediately: if true, calls gi.linkentity() before returning
edict_t* SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t& origin, const vec3_t& angles, bool link_immediately);

// Main spawn function that creates, configures, and validates a monster
// Applies horde bonuses, champion chances, and performs collision validation
edict_t* Horde_SpawnMonster(
    const vec3_t& origin,
    const vec3_t& angles,
    horde::MonsterTypeID monster_type,
    int32_t currentLevel,
    float champion_chance);

// Emergency spawn for when normal spawn points are unavailable
// Returns true if monster was successfully spawned
// special_spawn_type: 0=normal, 1=ambush, 2=retaliation
bool EmergencySpawnMonster(
    const int32_t levelNum,
    horde::MonsterTypeID typeId,
    bool is_additional_monster,
    float champion_chance_for_this_spawn,
    int special_spawn_type = 0);

// ============================================================================
// SPAWN POSITION FINDING FUNCTIONS
// ============================================================================

// Finds a valid emergency spawn position near a player
// specific_target: if provided, tries to spawn near this specific player/entity
bool FindEmergencySpawnPositionNearPlayer(
    vec3_t& out_position,
    vec3_t& out_angles,
    horde::MonsterTypeID typeId,
    edict_t* specific_target = nullptr);

// Core emergency position finder with additional tracking
// used_human_player: outputs whether a human (non-bot) player was targeted
bool FindEmergencySpawnPosition(
    vec3_t& position,
    vec3_t& angles,
    bool& used_human_player,
    horde::MonsterTypeID typeId,
    edict_t* specific_target = nullptr);

// Attempts to find an alternative spawn position around a spawn point
// Uses radial and predefined offset patterns to find valid spots
bool TryAlternativeSpawnPosition(
    edict_t* spawn_point,
    horde::MonsterTypeID typeId,
    vec3_t& final_origin,
    vec3_t& final_angles);

// ============================================================================
// SPAWN BATCH PLANNING AND EXECUTION
// ============================================================================

// Executes the global spawn plan (g_spawn_plan)
// Spawns all monsters that have been planned in the current batch
void ExecuteSpawnPlan();

// Plans a batch of monster spawns
// Selects spawn points and monster types for upcoming spawns
void PlanMonsterSpawnBatch(
    int32_t num_to_plan,
    int32_t currentLevel_param,
    float champion_chance_param,
    bool is_recovery_mode_active_param,
    bool is_retaliation_active_param,
    MonsterWaveType current_actual_wave_type_param,
    MonsterWaveType original_wave_type_before_recovery_param);

// Plans the next spawn batch and executes it
// Main entry point for batch spawning logic
void PlanNextSpawnBatch();

// ============================================================================
// SPAWN POINT VALIDATION AND MANAGEMENT
// ============================================================================

// Rebuilds the spawn point cache if needed
// Shuffles spawn points and updates cached counts
void RebuildSpawnPointCacheIfNeeded();

// Validates if a spawn point is suitable for monster spawning
// Checks cooldowns, player proximity, and occupation status
bool ValidateSpawnPointForMonster(edict_t* spawn_point, gtime_t current_time);

// Determines the spawning strategy based on current conditions
// Sets emergency spawn flags, recovery mode, and champion chances
void DetermineSpawnStrategy(
    const horde::MapSize& mapSize,
    int32_t& out_spawnable_this_call,
    bool& out_use_emergency_spawn,
    bool& out_recovery_mode_active,
    float& out_champion_chance,
    int32_t availableSpace);

// Executes emergency spawn procedure when normal spawning fails
// Returns the number of monsters successfully spawned
int ExecuteEmergencySpawnProcedure(
    int32_t spawnable_this_call,
    int32_t currentLevel,
    float champion_chance_param);

// ============================================================================
// EXTERNAL FUNCTIONS FROM g_horde.cpp (needed by horde_spawning.cpp)
// ============================================================================

// Note: horde_state_t and MonsterWaveType are already defined in g_horde.h

// Utility functions
extern bool IsPositionTooCloseToRecentSpawn(const vec3_t& position, const horde::MapSize& mapSize);
extern void MarkPositionAsRecentlyUsed(const vec3_t& position);
extern bool ApplyHordeBonuses(edict_t* monster, int32_t currentLevel, float champion_chance);
extern void BuildSpawnPointMap();

// Monster selection and spawning
extern horde::MonsterTypeID G_HordePickMonsterType(
    edict_t* spawn_point,
    int32_t currentLevel,
    MonsterWaveType current_wave_type,
    bool is_recovery_mode_active,
    bool is_retaliation_active,
    MonsterWaveType original_wave_type_before_recovery);

// Hard cap and queue management
extern bool CheckHardCapAndLog(
    int32_t current_count,
    int32_t hard_cap,
    int32_t remaining_spawns,
    horde_state_t current_state,
    int32_t available_space);

extern int32_t ManageSpawnCountsAndQueue(const horde::MapSize& mapSize, int32_t availableSpace);
extern void SetNextMonsterSpawnTime(const horde::MapSize& mapSize);

// Special spawn modes
extern void TriggerAmbush(const horde::MapSize& mapSize, int32_t currentLevel);

// Global state variables
extern bool g_recovery_mode_active;