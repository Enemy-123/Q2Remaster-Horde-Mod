// Monster Spawning System for Horde Mode
// Contains all core spawning, position finding, and batch planning functions

#include "horde_spawning.h"
#include "../shared.h"
#include "g_horde_phys.h"
#include "../g_local.h"
#include "g_horde.h"
#include "g_horde_benefits.h"
#include "horde_ids.h"
#include "../profiler.h"
#include "horde_performance.h"
#include "../memory_safety.h"
#include "horde_constants.h"
#include <algorithm>

// NOTE: horde_state_t is now defined in g_horde.h

struct SpawnPlanEntry {
    horde::MonsterTypeID typeId;
    edict_t *spawn_point;
};

struct PositionValidationResult {
    bool is_valid;
    vec3_t adjusted_position;
};

// ============================================================================
// SPAWN STATE VARIABLES (Definitions)
// ============================================================================

// Cache for potentially valid spawn points (pointers)
std::vector<edict_t *> g_potential_spawn_points;
bool g_spawn_points_cached = false;
size_t g_spawn_point_shuffle_index = 0; // Index for iterating shuffled list
int32_t g_cached_flying_spawn_count = 0;

// State for failure tracking and recovery
int32_t g_consecutive_spawn_failures = 0;
MonsterWaveType g_original_wave_type_before_recovery = MonsterWaveType::None;

bool need_spawn_cache_reset = false;
bool g_spawn_map_needs_build = true;

// Spawn point data structures
std::unordered_map<int, uint16_t> g_spawn_point_map;
SpawnPointsSoA g_spawnPointsData;

// Spawn validation cache
std::vector<CachedSpawnPointData> g_spawn_validation_cache;

// Special spawn state
SpecialSpawnState g_special_spawn_state;

// Spawn plan and batch variables
std::vector<SpawnPlanEntry> g_spawn_plan;
float g_champion_chance_for_current_batch = 0.2f;

// Classes EmergencySpawnOptimizer and MonsterSpawnPipeline are in g_horde.cpp
// We use wrapper functions FindEmergencySpawnPosition and ExecuteSpawnPlan instead

// External declarations (from g_horde.cpp)
extern void ED_CallSpawnMonsterByID(edict_t* ent, horde::MonsterTypeID typeId);
extern bool ApplyHordeBonuses(edict_t *monster, int32_t currentLevel, float champion_chance);
extern void SpawnGrow_Spawn(const vec3_t &origin, float size_start, float size_end);
extern int sound_spawn1;
extern cvar_t* developer;
extern cvar_t* g_horde;
extern MonsterWaveType current_wave_type;
extern bool g_recovery_mode_active;
extern std::vector<edict_t*> g_spawn_point_list;
extern uint16_t g_totalMonstersInWave;
extern bool next_wave_message_sent;
extern int32_t g_adjusted_monster_cap;

// External functions
extern void BuildSpawnPointMap();
extern bool GetPredictedScaledBounds(horde::MonsterTypeID typeId, vec3_t& out_mins, vec3_t& out_maxs);
extern bool IsFlying(horde::MonsterTypeID typeId);
extern void MarkPositionAsRecentlyUsed(const vec3_t& position);
extern bool IsPositionTooCloseToRecentSpawn(const vec3_t& position, const horde::MapSize& mapSize);
extern PositionValidationResult IsPositionPhysicallyValid(const vec3_t& position, const vec3_t& monster_mins, const vec3_t& monster_maxs, bool is_flying);
extern bool Horde_TeleportMonster(edict_t* self, const vec3_t& dest, const vec3_t& angles, bool force_teleport, bool ignore_visibility);
extern edict_t* FindSafeTeleportDestination(edict_t* self);
extern bool Horde_AttemptToUnstickMonster(edict_t* self);
extern horde::MonsterTypeID G_HordePickMonsterType(edict_t* spawn_point, int32_t currentLevel, MonsterWaveType waveType, bool is_retaliation, bool is_recovery_mode, MonsterWaveType original_wave_type_before_recovery);
extern void IncreaseSpawnAttempts(edict_t* spawn_point);
extern void OnSuccessfulSpawn(edict_t* spawn_point);
extern void SetNextMonsterSpawnTime(const horde::MapSize& mapSize);
extern bool AreHumanPlayersPresent();
extern void VerifyAndAdjustBots();
extern int32_t CalculateRemainingMonsters() noexcept;
extern bool ShouldTriggerAmbushSpawn();
extern void TriggerAmbush(const horde::MapSize& mapSize, int32_t currentLevel);
extern bool CheckHardCapAndLog(int32_t activeMonsters, int32_t hardCap, int32_t softCap, horde_state_t state, int32_t currentLevel);
extern int32_t ManageSpawnCountsAndQueue(const horde::MapSize& mapSize, int32_t availableSpace);

// Wrapper functions that use classes in g_horde.cpp
extern bool FindEmergencySpawnPosition(vec3_t& position, vec3_t& angles, bool& used_human_player, horde::MonsterTypeID typeId, edict_t* specific_target);
extern void ExecuteSpawnPlan();

// NOTE: HordeState and g_horde_local are now declared in g_horde.h

// ============================================================================
// STRUCT IMPLEMENTATIONS
// ============================================================================

void SpawnPointsSoA::resize(size_t new_size) {
    // Clamp size to prevent overflow
    constexpr size_t MAX_SAFE_CONTAINER_SIZE = 100000;
    if (new_size > MAX_SAFE_CONTAINER_SIZE) {
        gi.Com_PrintFmt("WARNING: Spawn points data resize {} exceeds max {}, clamping\n",
            new_size, MAX_SAFE_CONTAINER_SIZE);
        new_size = MAX_SAFE_CONTAINER_SIZE;
    }

    try {
        isTemporarilyDisabled.assign(new_size, false);
        cooldownEndsAt.assign(new_size, 0_sec);
        alternative_cooldown.assign(new_size, 0_sec);
        teleport_cooldown.assign(new_size, 0_sec);
        lastSpawnTime.assign(new_size, 0_sec);
        attempts.assign(new_size, 0);
        successfulSpawns.assign(new_size, 0);
        alternative_attempts.assign(new_size, 0);
        needs_long_alternative_cooldown.assign(new_size, false);
    } catch (const std::bad_alloc&) {
        gi.Com_Print("ERROR: Failed to allocate memory for spawn points data\n");
        clear(); // Clear all on failure
    }
}

// ============================================================================
// CORE SPAWNING FUNCTIONS
// ============================================================================

edict_t* SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t& origin, const vec3_t& angles, bool link_immediately)
{
    if (typeId == horde::MonsterTypeID::UNKNOWN)
        return nullptr;

    edict_t* monster = G_Spawn();
    if (!monster)
        return nullptr;

    monster->classname = horde::MonsterTypeRegistry::GetClassname(typeId);
    monster->s.origin = origin;
    monster->s.angles = angles;

    ED_CallSpawnMonsterByID(monster, typeId);

    if (!monster->inuse) {
        G_FreeEdict(monster);
        return nullptr;
    }

    if (link_immediately)
        gi.linkentity(monster);

    return monster;
}

edict_t* Horde_SpawnMonster(
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

    monster->spawnflags |= (SPAWNFLAG_MONSTER_SUPER_STEP);

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

// ============================================================================
// POSITION FINDING FUNCTIONS
// ============================================================================

// Wrapper function (FindEmergencySpawnPosition is in g_horde.cpp)
bool FindEmergencySpawnPositionNearPlayer(vec3_t& out_position, vec3_t& out_angles, horde::MonsterTypeID typeId, edict_t* specific_target)
{
    bool used_human_player;
    return FindEmergencySpawnPosition(out_position, out_angles, used_human_player, typeId, specific_target);
}

bool TryAlternativeSpawnPosition(edict_t* spawn_point, horde::MonsterTypeID typeId, vec3_t& final_origin, vec3_t& final_angles)
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
        [&](const vec3_t& candidate_pos, const vec3_t& offset_dir) -> bool
    {
        trace_t los_trace = gi.traceline(base_origin, candidate_pos, spawn_point, MASK_SOLID);
        if (los_trace.fraction < 1.0f)
        {
            return false;
        }

        // FIXED: Use struct return instead of in-out parameter
        const auto validation = IsPositionPhysicallyValid(candidate_pos, predicted_mins, predicted_maxs, is_flying);
        if (validation.is_valid)
        {
            if (!IsPositionTooCloseToRecentSpawn(validation.adjusted_position, g_horde_local.current_map_size))
            {
                final_origin = validation.adjusted_position;
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

    // Use radial attempts to find alternative spawn positions
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

// ============================================================================
// SPAWN BATCH PLANNING AND EXECUTION
// ============================================================================

// ExecuteSpawnPlan is in g_horde.cpp (uses g_monster_spawn_pipeline)

void PlanMonsterSpawnBatch(
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

    // Safe reserve with overflow check
    size_t reserve_size = std::min(static_cast<size_t>(num_to_plan), MAX_ENTITIES_PER_FRAME);
    if (!safe_reserve(g_spawn_plan, reserve_size)) {
        gi.Com_Print("ERROR: Failed to reserve memory for spawn plan\n");
        return;
    }

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
            // Use safe emplace_back with overflow protection
            if (!safe_emplace_back_limit(g_spawn_plan, MAX_ENTITIES_PER_FRAME, monster_type_id, spawn_point)) {
                gi.Com_Print("WARNING: Spawn plan full, stopping planning\n");
                break;
            }
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
    // developer >= 3: Freeze monster spawning for debugging (bots still disabled at developer >= 2)
    if (developer->integer >= 3 && g_horde->integer) return;

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

    int32_t spawnable_this_call_local;
    bool use_emergency_spawn_flag;
    float champion_chance_for_batch;
    DetermineSpawnStrategy(mapSize, spawnable_this_call_local, use_emergency_spawn_flag, g_recovery_mode_active, champion_chance_for_batch, availableSpace);

    if (use_emergency_spawn_flag) {
        int num_spawned = ExecuteEmergencySpawnProcedure(spawnable_this_call_local, currentLevel, champion_chance_for_batch);
        if (num_spawned > 0 && g_recovery_mode_active) {
            g_recovery_mode_active = false;
            current_wave_type = g_original_wave_type_before_recovery;
        }
    }
    else if (spawnable_this_call_local > 0) {
        PlanMonsterSpawnBatch(
            spawnable_this_call_local,
            currentLevel,
            champion_chance_for_batch,
            g_recovery_mode_active,
            (g_special_spawn_state.type == SpecialSpawnType::Retaliation),
            current_wave_type,
            g_original_wave_type_before_recovery);
    }

    SetNextMonsterSpawnTime(mapSize);
}

// ============================================================================
// SPAWN POINT VALIDATION AND MANAGEMENT
// ============================================================================

void RebuildSpawnPointCacheIfNeeded()
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

bool ValidateSpawnPointForMonster(edict_t* spawn_point, gtime_t current_time)
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

void DetermineSpawnStrategy(const horde::MapSize& mapSize, int32_t& out_spawnable_this_call, bool& out_use_emergency_spawn, bool& out_recovery_mode_active_ref, float& out_champion_chance, int32_t availableSpace)
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

int ExecuteEmergencySpawnProcedure(int32_t spawnable_this_call,
                                    int32_t currentLevel,
                                    float champion_chance_param)
{ // Added champion_chance_param
    int emergency_spawned_count = 0;
    std::vector<vec3_t> batch_spawn_positions; // Track positions used in this batch

    for (int i = 0; i < std::min(spawnable_this_call, 3); ++i)
    { // Limit emergency spawns per call
        horde::MonsterTypeID emergency_type = (currentLevel < 10) ? horde::MonsterTypeID::SOLDIER : horde::MonsterTypeID::GUNNER;
        if (currentLevel >= 15)
            emergency_type = horde::MonsterTypeID::TANK;
        if (currentLevel >= 20)
            emergency_type = horde::MonsterTypeID::GLADIATOR;

        // Find spawn position with spacing from other monsters in this batch
        vec3_t emergency_origin, emergency_angles;
        bool found_position = false;

        // Try to find a position that's spaced from previous spawns in this batch
        constexpr float MIN_BATCH_SPACING = 150.0f; // Minimum distance between monsters in same batch
        constexpr int MAX_POSITION_ATTEMPTS = 10;

        for (int attempt = 0; attempt < MAX_POSITION_ATTEMPTS && !found_position; ++attempt)
        {
            if (FindEmergencySpawnPositionNearPlayer(emergency_origin, emergency_angles, emergency_type))
            {
                // Check if this position is too close to other monsters spawned in this batch
                bool too_close_to_batch = false;
                for (const vec3_t& batch_pos : batch_spawn_positions)
                {
                    float dist_sq = (emergency_origin - batch_pos).lengthSquared();
                    if (dist_sq < (MIN_BATCH_SPACING * MIN_BATCH_SPACING))
                    {
                        too_close_to_batch = true;
                        break;
                    }
                }

                if (!too_close_to_batch)
                {
                    found_position = true;
                }
                // If too close, the loop will try again with a new position
            }
            else
            {
                break; // No valid position found at all
            }
        }

        if (found_position)
        {
            // Spawn the monster at the validated position
            edict_t* monster = Horde_SpawnMonster(emergency_origin, emergency_angles, emergency_type, currentLevel, champion_chance_param);

            if (monster && monster->inuse && !monster->deadflag && monster->health > 0)
            {
                // Add this position to our batch tracking
                batch_spawn_positions.push_back(emergency_origin);

                // Apply spawn effects
                SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
                if (sound_spawn1)
                {
                    gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
                }

                emergency_spawned_count++;
                if (g_horde_local.num_to_spawn > 0)
                    --g_horde_local.num_to_spawn;
                else if (g_horde_local.queued_monsters > 0)
                    --g_horde_local.queued_monsters;
            }
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