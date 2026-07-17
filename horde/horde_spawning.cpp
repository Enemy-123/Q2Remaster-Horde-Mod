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

struct PositionValidationResult {
    bool is_valid;
    vec3_t adjusted_position;
};

// ============================================================================
// SPAWN STATE VARIABLES (Definitions)
// ============================================================================

// Global spawn system state instance (definition)
SpawnSystemState g_spawn_system;

// Classes EmergencySpawnOptimizer and MonsterSpawnPipeline are in g_horde.cpp
// We use wrapper functions FindEmergencySpawnPosition and ExecuteSpawnPlan instead

// External declarations (from g_horde.cpp)
extern void ED_CallSpawnMonsterByID(edict_t* ent, horde::MonsterTypeID typeId);
extern bool ApplyHordeBonuses(edict_t *monster, int32_t currentLevel, float champion_chance);
extern void SpawnGrow_Spawn(const vec3_t &origin, float size_start, float size_end);
extern cached_soundindex sound_spawn1;
extern cached_soundindex sound_quake;
extern cached_soundindex tele1;

extern cvar_t* developer;
extern cvar_t* g_horde;
extern MonsterWaveType current_wave_type;
extern bool g_recovery_mode_active;
extern boost::container::small_vector<edict_t*, 64> g_spawn_point_list;
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
extern bool ShouldUseFallbackGrid();
extern cvar_t* g_horde_spawn_dist_cap;
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
    if (new_size > MAX_STATE_SPAWN_POINTS) {
        gi.Com_PrintFmt("WARNING: Spawn points data resize {} exceeds fixed capacity {}, clamping\n",
            new_size, MAX_STATE_SPAWN_POINTS);
        new_size = MAX_STATE_SPAWN_POINTS;
    }

    active_count = new_size;
    isTemporarilyDisabled.fill(false);
    cooldownEndsAt.fill(0_sec);
    alternative_cooldown.fill(0_sec);
    teleport_cooldown.fill(0_sec);
    lastSpawnTime.fill(0_sec);
    attempts.fill(0);
    successfulSpawns.fill(0);
    alternative_attempts.fill(0);
    needs_long_alternative_cooldown.fill(false);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/// Applies visual and audio effects when a monster spawns
/// Note: SpawnGrow is now handled inside Horde_SpawnMonster
static void ApplyMonsterSpawnEffects(edict_t* monster) noexcept
{
    // No additional effects needed - handled in Horde_SpawnMonster
    (void)monster;
}

/// Applies special spawn effects - differentiated for Ambush vs Retaliation
/// Note: SpawnGrow is now handled inside Horde_SpawnMonster
static void ApplySpecialSpawnEffects(edict_t* monster, bool is_retaliation) noexcept
{
    if (!monster || !monster->inuse || monster->deadflag || monster->health <= 0)
        return;

    if (is_retaliation) {
        // Retaliation: More aggressive sound effect
        if (sound_quake) {
            gi.sound(monster, CHAN_AUTO, sound_quake, 1.0f, ATTN_NORM, 0);  // Use quake sound for impact
        }
    } else {
        // Ambush: Sneakier sound effect
        if (tele1) {
            gi.sound(monster, CHAN_AUTO, tele1, 0.7f, ATTN_NORM, 0);  // Quieter teleport sound
        }
    }
}

/// Determines appropriate emergency monster type based on level
static horde::MonsterTypeID GetEmergencyMonsterType(int32_t currentLevel) noexcept
{
    if (currentLevel >= HordeConstants::EMERGENCY_GLADIATOR_LEVEL_THRESHOLD)
        return horde::MonsterTypeID::GLADIATOR;
    if (currentLevel >= HordeConstants::EMERGENCY_TANK_LEVEL_THRESHOLD)
        return horde::MonsterTypeID::TANK;
    if (currentLevel >= HordeConstants::EMERGENCY_GUNNER_LEVEL_THRESHOLD)
        return horde::MonsterTypeID::GUNNER;
    return horde::MonsterTypeID::SOLDIER;
}

/// Get map-size-aware emergency spawn spacing
static float GetEmergencySpacingForMap(const horde::MapSize& mapSize) noexcept
{
    if (mapSize.isSmallMap)
        return HordeConstants::EMERGENCY_MIN_BATCH_SPACING * 0.7f;  // 196 units for small maps
    else if (mapSize.isBigMap)
        return HordeConstants::EMERGENCY_MIN_BATCH_SPACING * 1.2f;  // 336 units for big maps
    else
        return HordeConstants::EMERGENCY_MIN_BATCH_SPACING;         // 280 units for medium maps
}

/// Persistent emergency spawn tracking across frames
static struct EmergencySpawnHistory {
    static constexpr size_t MAX_HISTORY = 16;
    std::array<vec3_t, MAX_HISTORY> positions;
    std::array<gtime_t, MAX_HISTORY> spawn_times;
    size_t write_index = 0;

    void AddPosition(const vec3_t& pos) {
        positions[write_index] = pos;
        spawn_times[write_index] = level.time;
        write_index = (write_index + 1) % MAX_HISTORY;
    }

    bool IsTooCloseToRecent(const vec3_t& position, float min_spacing, gtime_t cooldown_duration = 3_sec) const {
        const float min_spacing_sq = min_spacing * min_spacing;
        for (size_t i = 0; i < MAX_HISTORY; ++i) {
            if (spawn_times[i] + cooldown_duration > level.time) {
                if ((position - positions[i]).lengthSquared() < min_spacing_sq)
                    return true;
            }
        }
        return false;
    }

    // Reset history on map change or game reset
    void Reset() {
        positions.fill(vec3_origin);
        spawn_times.fill(0_sec);
        write_index = 0;
    }
} g_emergency_spawn_history;

/// Checks if position is too close to other positions in batch or recent emergency spawns
static bool IsTooCloseToBatchPositions(const vec3_t& position, const std::array<vec3_t, HordeConstants::EMERGENCY_SPAWN_LIMIT_PER_CALL>& batch_positions, int batch_count) noexcept
{
    const horde::MapSize& mapSize = g_horde_local.current_map_size;
    const float min_spacing = GetEmergencySpacingForMap(mapSize);
    const float min_spacing_sq = min_spacing * min_spacing;

    // Check current batch (only up to batch_count)
    for (int i = 0; i < batch_count; ++i)
    {
        if ((position - batch_positions[i]).lengthSquared() < min_spacing_sq)
            return true;
    }

    // Check recent emergency spawns across frames (map-size-aware)
    return g_emergency_spawn_history.IsTooCloseToRecent(position, min_spacing);
}

/// Attempts to find a valid emergency spawn position with batch spacing
static bool FindSpacedEmergencyPosition(
    vec3_t& out_position,
    vec3_t& out_angles,
    horde::MonsterTypeID typeId,
    const std::array<vec3_t, HordeConstants::EMERGENCY_SPAWN_LIMIT_PER_CALL>& batch_positions,
    int batch_count)
{
    for (int attempt = 0; attempt < HordeConstants::MAX_EMERGENCY_POSITION_ATTEMPTS; ++attempt)
    {
        if (!FindEmergencySpawnPositionNearPlayer(out_position, out_angles, typeId))
            return false;

        if (!IsTooCloseToBatchPositions(out_position, batch_positions, batch_count))
            return true;
    }
    return false;
}

/// Checks if spawn point cooldowns allow spawning
static bool CheckSpawnPointCooldowns(uint16_t index, gtime_t current_time, bool emergency_mode) noexcept
{
    if (emergency_mode)
        return true; // Skip cooldowns in emergency mode for faster spawning

    return current_time >= g_spawn_system.spawn_points_data.teleport_cooldown[index] &&
           current_time >= g_spawn_system.spawn_points_data.alternative_cooldown[index] &&
           (!g_spawn_system.spawn_points_data.isTemporarilyDisabled[index] ||
            current_time >= g_spawn_system.spawn_points_data.cooldownEndsAt[index]);
}

/// Checks if spawn position is in the Potentially Visible Set (PVS) of any active player
/// Uses gi.inPVS which checks if the position could be visible from any angle, not just direct line-of-sight
static bool IsSpawnPositionInPlayerPVS(const vec3_t& spawn_pos) noexcept
{
    for (const auto* player : active_players_no_spect())
    {
        // Use PVS check - returns true if spawn_pos is in the player's potentially visible set
        // This is broader than line-of-sight: it catches positions visible from any angle
        // The 'false' parameter means don't check through portals (stricter)
        if (gi.inPVS(spawn_pos, player->s.origin, false))
            return true;  // Position is in PVS of at least one player
    }
    return false;  // Not in PVS of any player
}

/// Squared distance from a position to the nearest active (non-spectator) player.
/// Returns 0 when there are no players to measure against.
static float MinDistSqToActivePlayer(const vec3_t& pos) noexcept
{
    float min_sq = std::numeric_limits<float>::max();
    bool found = false;
    for (const auto* player : active_players_no_spect())
    {
        const float d = DistanceSquared(pos, player->s.origin);
        if (d < min_sq)
        {
            min_sq = d;
            found = true;
        }
    }
    return found ? min_sq : 0.0f;
}

/// Checks if spawn point is too close to players and optionally checks visibility
/// UPDATED: Now includes 25% chance to require out-of-visibility spawning
static bool CheckSpawnPointPlayerProximity(const vec3_t& spawn_pos, bool emergency_mode, bool recovery_mode) noexcept
{
    // No active players to measure against (all spectating, or a no-player test). The distance
    // gates are meaningless without a reference point, and the max-distance cap below would
    // otherwise reject EVERY spawn point ("no player within range"), stalling the whole wave.
    bool any_active_player = false;
    for (const auto* player : active_players_no_spect()) { (void)player; any_active_player = true; break; }
    if (!any_active_player)
        return true;

    float min_dist = HordeConstants::GetMinPlayerDistSpawnpoint(g_horde_local.current_map_size);

    if (recovery_mode)
        min_dist *= 0.7f; // 30% reduction in recovery mode
    if (emergency_mode)
        min_dist *= 0.5f; // 50% reduction in emergency mode

    const float MIN_DIST_SQ = min_dist * min_dist;

    for (const auto* player : active_players_no_spect())
    {
        if (DistanceSquared(spawn_pos, player->s.origin) < MIN_DIST_SQ)
            return false;
    }

    // Maximum-distance cap: reject spawn points stranded far from every player (distant or
    // sealed-off wings on big maps). Relaxed in recovery/emergency so spawning is never
    // fully starved; toggle via g_horde_spawn_dist_cap.
    if (g_horde_spawn_dist_cap && g_horde_spawn_dist_cap->integer)
    {
        float max_dist = HordeConstants::GetMaxPlayerDistSpawnpoint(g_horde_local.current_map_size);
        if (recovery_mode)  max_dist *= 1.5f;
        if (emergency_mode) max_dist *= 2.0f;
        const float MAX_DIST_SQ = max_dist * max_dist;

        bool within_range_of_any = false;
        for (const auto* player : active_players_no_spect())
        {
            if (DistanceSquared(spawn_pos, player->s.origin) <= MAX_DIST_SQ)
            {
                within_range_of_any = true;
                break;
            }
        }
        if (!within_range_of_any)
            return false;
    }

    // 25% chance to require out-of-visibility position (only in normal mode, not recovery/emergency)
    if (!emergency_mode && !recovery_mode)
    {
        // Deterministic per spawn position and short time window (500ms).
        // This avoids flickering while preventing one global roll from affecting all points.
        constexpr int64_t VISIBILITY_TIME_BUCKET_MS = 500;
        constexpr float POSITION_QUANTIZE_FACTOR = 0.125f; // 8-unit buckets

        const auto quantize = [](float value) -> int32_t {
            return static_cast<int32_t>(value * POSITION_QUANTIZE_FACTOR);
        };
        const auto hash_combine = [](uint32_t hash, uint32_t value) -> uint32_t {
            hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2);
            return hash;
        };

        uint32_t hash = 0x811C9DC5u;
        hash = hash_combine(hash, static_cast<uint32_t>(quantize(spawn_pos.x)));
        hash = hash_combine(hash, static_cast<uint32_t>(quantize(spawn_pos.y)));
        hash = hash_combine(hash, static_cast<uint32_t>(quantize(spawn_pos.z)));
        hash = hash_combine(hash, static_cast<uint32_t>(level.time.milliseconds() / VISIBILITY_TIME_BUCKET_MS));

        const float clamped_visibility_chance = std::clamp(HordeConstants::OUT_OF_VISIBILITY_CHANCE, 0.0f, 1.0f);
        const uint32_t threshold = static_cast<uint32_t>(clamped_visibility_chance * 100.0f + 0.5f);
        const bool visibility_required = (hash % 100u) < threshold;

        if (visibility_required)
        {
            if (IsSpawnPositionInPlayerPVS(spawn_pos))
                return false;  // Position is in player's PVS but we required out-of-visibility
        }
    }

    return true;
}

/// Updates spawn validation cache with result
static void UpdateSpawnValidationCache(
    CachedSpawnPointData& cached,
    gtime_t current_time,
    bool is_valid,
    const vec3_t& spawn_origin) noexcept
{
    cached.last_validation_time = current_time;
    cached.last_validation_result = is_valid;
    cached.last_validation_origin = spawn_origin;

    if (!is_valid)
    {
        g_spawn_system.consecutive_spawn_failures++;
    }
    else
    {
        // Gradually reduce failure count on successful validation
        if (g_spawn_system.consecutive_spawn_failures > 0)
        {
            g_spawn_system.consecutive_spawn_failures = std::max(0, g_spawn_system.consecutive_spawn_failures - 2);
        }
    }
}

/// Checks and validates an alternative spawn position candidate
/// Returns true if position is valid and sets final_origin and final_angles
// Diagnostics for why alternative-spawn candidates get rejected (dumped by TryAlternativeSpawnPosition
// when it fails). Reset at the start of each TryAlternativeSpawnPosition call.
static int g_alt_diag_considered = 0;
static int g_alt_diag_los = 0;
static int g_alt_diag_phys = 0;
static int g_alt_diag_recent = 0;

static bool CheckAndSetAlternativePosition(
    const vec3_t& base_origin,
    const vec3_t& base_angles,
    const vec3_t& candidate_pos,
    const vec3_t& offset_dir,
    const vec3_t& predicted_mins,
    const vec3_t& predicted_maxs,
    bool is_flying,
    edict_t* spawn_point,
    vec3_t& final_origin,
    vec3_t& final_angles,
    bool ignore_recent_spawn = false)
{
    g_alt_diag_considered++;

    // Check line of sight from base origin
    trace_t los_trace = gi.traceline(base_origin, candidate_pos, spawn_point, MASK_SOLID);
    if (los_trace.fraction < 1.0f)
    {
        g_alt_diag_los++;
        return false;
    }

    // Validate position physically
    const auto validation = IsPositionPhysicallyValid(candidate_pos, predicted_mins, predicted_maxs, is_flying);
    if (!validation.is_valid)
    {
        g_alt_diag_phys++;
        return false;
    }

    // Check if too close to recent spawns. Skipped in the relaxed fallback pass so a wave can
    // still deploy around limited/congested spawn points instead of stalling into emergency --
    // the radial offset already spaces the candidate away from the base point.
    if (!ignore_recent_spawn &&
        IsPositionTooCloseToRecentSpawn(validation.adjusted_position, g_horde_local.current_map_size))
    {
        g_alt_diag_recent++;
        return false;
    }

    // Position is valid - set output parameters
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
    // Phase 1: Create and initialize the monster.
    // NOTE: ED_CallSpawnMonsterByID may already register counted monsters before link.
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
                // Monster successfully spawned - show spawn grow at final position
                SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
                if (sound_spawn1) {
                    gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
                }
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
        // Keep level monster stats coherent when a pre-registered monster is removed during spawn failure.
        if (!monster->deadflag && !monster->spawnflags.has(SPAWNFLAG_MONSTER_DEAD)) {
            G_MonsterKilled(monster);
        }
        G_FreeEdict(monster);
        return nullptr;
    }

    // Monster successfully spawned at original position - show spawn grow
    SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
    if (sound_spawn1) {
        gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
    }
    return monster;
}

bool EmergencySpawnMonster(const int32_t levelNum,
                           horde::MonsterTypeID typeId,
                           bool is_additional_monster,
                           float champion_chance_for_this_spawn,
                           int special_spawn_type)
{
    PROFILE_SCOPE("EmergencySpawnMonster");

    vec3_t emergency_origin, emergency_angles;
    if (!FindEmergencySpawnPositionNearPlayer(emergency_origin, emergency_angles, typeId))
    {
        if (developer->integer)
        {
            // Count active players for debugging
            int active_player_count = 0;
            for (const auto* player : active_players_no_spect()) {
                if (player->health > 0) {
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

    // Apply appropriate spawn effects based on type
    if (special_spawn_type == 1 || special_spawn_type == 2) {
        ApplySpecialSpawnEffects(monster, special_spawn_type == 2);  // true for retaliation
    } else {
        ApplyMonsterSpawnEffects(monster);
    }

    if (is_additional_monster)
    {
        if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max())
        {
            g_totalMonstersInWave++;
        }
    }

    if (developer->integer > 1)
    {
        const char* spawn_type_str = (special_spawn_type == 2) ? "RETALIATION" : (special_spawn_type == 1) ? "AMBUSH" : "EMERGENCY";
        gi.Com_PrintFmt("{} SPAWN SUCCESSFUL: Spawned '{}' (Additional: {}).\n",
                        spawn_type_str, monster->classname, is_additional_monster ? "Yes" : "No");
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
    const bool is_flying_only_lane = (spawn_point->style == 1);

    vec3_t predicted_mins = {-16.0f, -16.0f, -24.0f};
    vec3_t predicted_maxs = {16.0f, 16.0f, 32.0f};
    GetPredictedScaledBounds(typeId, predicted_mins, predicted_maxs);

    g_alt_diag_considered = g_alt_diag_los = g_alt_diag_phys = g_alt_diag_recent = 0;

    // Phase 1: Use radial attempts to find alternative spawn positions
    for (int i = 0; i < HordeConstants::ALTERNATIVE_RADIAL_ATTEMPTS; ++i)
    {
        const float radius = frandom(HordeConstants::ALTERNATIVE_MIN_RADIUS, HordeConstants::ALTERNATIVE_MAX_RADIUS);
        const float angle_rad = frandom() * 2.0f * PIf;
        const vec3_t offset = {
            cosf(angle_rad) * radius,
            sinf(angle_rad) * radius,
            frandom(HordeConstants::ALTERNATIVE_MIN_Z_OFFSET, HordeConstants::ALTERNATIVE_MAX_Z_OFFSET)
        };

        const vec3_t candidate_pos = base_origin + offset;

        if (CheckAndSetAlternativePosition(base_origin, base_angles, candidate_pos, offset,
                                           predicted_mins, predicted_maxs, is_flying, spawn_point,
                                           final_origin, final_angles))
        {
            return true;
        }
    }

    // Phase 1b: Relaxed retry -- same radial search but ignore the global recent-spawn spacing
    // gate. On maps with few spawn points (or when monsters don't disperse), every nearby
    // position reads as "too close to a recent spawn", which would otherwise stall deployment
    // and escalate to emergency mode. The radial offset still spaces each candidate from the
    // point, so this lets the wave finish deploying around the available points.
    for (int i = 0; i < HordeConstants::ALTERNATIVE_RADIAL_ATTEMPTS; ++i)
    {
        const float radius = frandom(HordeConstants::ALTERNATIVE_MIN_RADIUS, HordeConstants::ALTERNATIVE_MAX_RADIUS);
        const float angle_rad = frandom() * 2.0f * PIf;
        const vec3_t offset = {
            cosf(angle_rad) * radius,
            sinf(angle_rad) * radius,
            frandom(HordeConstants::ALTERNATIVE_MIN_Z_OFFSET, HordeConstants::ALTERNATIVE_MAX_Z_OFFSET)
        };

        const vec3_t candidate_pos = base_origin + offset;

        if (CheckAndSetAlternativePosition(base_origin, base_angles, candidate_pos, offset,
                                           predicted_mins, predicted_maxs, is_flying, spawn_point,
                                           final_origin, final_angles, /*ignore_recent_spawn=*/true))
        {
            return true;
        }
    }

    // Phase 2: If radial search failed, try spawn grid positions near this spawn point.
    // Exception: for flying monsters on dedicated style-1 lanes, keep them on-lane and
    // do not fall back to generic grid points.
    if (ShouldUseFallbackGrid() && !(is_flying && is_flying_only_lane))
    {
        constexpr int GRID_ATTEMPTS = 32;
        constexpr float GRID_MIN_DIST = 64.0f;
        constexpr float GRID_MAX_DIST = 512.0f;

        for (int attempt = 0; attempt < GRID_ATTEMPTS; ++attempt)
        {
            vec3_t grid_pos;

            // Use tactical spawning if enabled (modes 1 or 2)
            // Mode 1: Distance checks only
            // Mode 2: Distance + visibility checks
            bool got_position = false;
            if (g_horde_tactical_spawn->integer > 0)
            {
                // Tactical spawn: check distance from players and optionally visibility
                got_position = HordePhys::g_spawn_grid.GetTacticalSpawnPosition(grid_pos, 256.0f, 10);
            }
            else
            {
                // Standard spawn: just use random position near spawn point
                got_position = HordePhys::g_spawn_grid.GetRandomPositionNear(base_origin, GRID_MIN_DIST, GRID_MAX_DIST, grid_pos);
            }

            if (!got_position)
                continue;

            // Validate grid position
            const auto validation = IsPositionPhysicallyValid(grid_pos, predicted_mins, predicted_maxs, is_flying);
            if (validation.is_valid)
            {
                final_origin = validation.adjusted_position;
                final_angles = base_angles;

                if (developer->integer > 1)
                {
                    gi.Com_PrintFmt("Alternative spawn used GRID position at {} (dist from spawn: {:.1f})\n",
                        final_origin, (final_origin - base_origin).length());
                }

                return true;
            }
        }
    }

    // Both radial passes (strict + relaxed) and the grid fallback failed for this point. Report
    // the rejection breakdown so we can see which gate is starving alternatives (throttled).
    if (developer->integer)
    {
        static gtime_t last_alt_diag = 0_sec;
        if (level.time >= last_alt_diag + 1_sec)
        {
            last_alt_diag = level.time;
            gi.Com_PrintFmt("ALT DIAG (TypeID {}): considered={} rejected[los={} phys={} recent={}] near point {}\n",
                static_cast<int>(typeId), g_alt_diag_considered, g_alt_diag_los,
                g_alt_diag_phys, g_alt_diag_recent, base_origin);
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
    g_spawn_system.spawn_plan.clear();
    if (num_to_plan <= 0)
        return;

    // Optimized reserve: only allocate if capacity insufficient (avoids reallocation)
    size_t reserve_size = std::min(static_cast<size_t>(num_to_plan), MAX_ENTITIES_PER_FRAME);
    if (g_spawn_system.spawn_plan.capacity() < reserve_size) {
        if (!safe_reserve(g_spawn_system.spawn_plan, reserve_size)) {
            gi.Com_Print("ERROR: Failed to reserve memory for spawn plan\n");
            return;
        }
    }

    g_spawn_system.champion_chance_for_current_batch = champion_chance_param;

    const size_t total_potential_points = g_spawn_system.potential_spawn_points.size();
    if (total_potential_points == 0)
    {
        if (developer->integer)
            gi.Com_PrintFmt("SPAWN PLANNING: No potential spawn points available.\n");
        return;
    }

    size_t points_checked = 0;
    size_t monster_pick_attempts = 0;
    int planned_count = 0;
    int failed_validation = 0;
    int failed_monster_pick = 0;
    bool plan_capacity_exhausted = false;

    // Points already assigned within THIS planning batch. The round-robin prefers points not yet
    // used so a batch of N monsters spreads across N distinct points instead of piling several onto
    // one (which then alternative-spawns them into a visible cluster). Point cooldowns are only
    // applied at execution (OnSuccessfulSpawn), so without this the same valid point could be handed
    // to multiple monsters in one batch before any cooldown takes effect.
    boost::container::small_vector<edict_t*, 64> used_spawn_points_this_batch;
    auto used_this_batch = [&](const edict_t* sp) -> bool
    {
        for (const edict_t* p : used_spawn_points_this_batch)
            if (p == sp) return true;
        return false;
    };
    // "Oldest cooldown" = the point whose cooldown ends soonest (i.e. has been waiting longest).
    auto effective_cooldown_end = [](edict_t* sp) -> gtime_t
    {
        const uint16_t idx = GetSpawnPointIndexSafe(sp);
        if (idx == 0xFFFF) return gtime_t::from_sec(1e9f); // never prefer an unmapped point
        const auto& d = g_spawn_system.spawn_points_data;
        return std::max(d.cooldownEndsAt[idx], d.alternative_cooldown[idx]);
    };

    auto find_spawn_point_for_monster = [&](horde::MonsterTypeID monster_type_id) -> edict_t*
    {
        const bool monster_is_flying = IsFlying(monster_type_id);

        // With some chance, bias toward the farthest spawn point so the horde appears to come from
        // nowhere. We scan a short run of valid candidates and keep the one farthest from the nearest
        // player instead of taking the first valid one. Normal waves use the tunable
        // g_horde_far_spawn_chance; fog / limit-break waves bias at least 0.9 toward the far edge so
        // the swarm pours from a distance.
        const float base_far_chance = g_horde_far_spawn_chance
            ? std::clamp(g_horde_far_spawn_chance->value, 0.0f, 1.0f)
            : LimitBreakWave::FARTHEST_SPAWN_CHANCE;
        const float farthest_chance = IsLimitBreakWave(current_wave_type)
            ? std::max(base_far_chance, LimitBreakWave::FARTHEST_SPAWN_CHANCE_FOG)
            : base_far_chance;
        const bool prefer_farthest = frandom() < farthest_chance;
        edict_t* farthest_point = nullptr;        float farthest_dist_sq = -1.0f;        // fallback: plain farthest
        edict_t* farthest_spaced = nullptr;       float farthest_spaced_dist_sq = -1.0f; // preferred: far AND spaced from batch-mates
        int candidates_seen = 0;

        // Spread the far-biased picks: a batch shouldn't pile its far spawns into one wing (the
        // farthest points on a big map tend to be geographically bunched). Prefer the farthest point
        // that's also at least a batch-spacing away from points already used this batch; fall back to
        // the plain farthest so this never starves.
        const float batch_spacing = GetEmergencySpacingForMap(g_horde_local.current_map_size);
        const float batch_spacing_sq = batch_spacing * batch_spacing;
        auto too_close_to_batch = [&](const edict_t* sp) -> bool {
            for (const edict_t* p : used_spawn_points_this_batch)
                if (p && DistanceSquared(p->s.origin, sp->s.origin) < batch_spacing_sq)
                    return true;
            return false;
        };

        // Keep-flowing fallbacks: when every style-compatible point is on cooldown/occupied we still
        // spawn (so domination/fog waves never stall) by reusing the point whose cooldown is oldest.
        // Prefer one not used this batch (better spread); else the global oldest, so a batch larger
        // than the point count still deploys. A relaxed proximity gate keeps us off players' heads.
        edict_t* oldest_unused = nullptr; gtime_t oldest_unused_end = gtime_t::from_sec(1e9f);
        edict_t* oldest_any = nullptr;    gtime_t oldest_any_end = gtime_t::from_sec(1e9f);

        for (size_t i = 0; i < total_potential_points; ++i)
        {
            if (g_spawn_system.spawn_point_shuffle_index >= total_potential_points)
            {
                g_spawn_system.spawn_point_shuffle_index = 0;
            }

            edict_t* spawn_point = g_spawn_system.potential_spawn_points[g_spawn_system.spawn_point_shuffle_index++];
            points_checked++;

            if (!spawn_point || !spawn_point->inuse)
            {
                failed_validation++;
                continue;
            }

            if (!monster_is_flying && spawn_point->style == 1)
            {
                failed_validation++;
                continue;
            }

            // Remember this style-compatible point as an oldest-cooldown fallback candidate, but
            // only if it isn't right on top of a player (cooldown is the gate we want to bypass,
            // not player proximity). Flying-only lanes skip the proximity test, as elsewhere.
            const bool already_used = used_this_batch(spawn_point);
            const bool proximity_ok = (spawn_point->style == 1) ||
                                      CheckSpawnPointPlayerProximity(spawn_point->s.origin, true, true);
            if (proximity_ok)
            {
                const gtime_t cd_end = effective_cooldown_end(spawn_point);
                if (cd_end < oldest_any_end) { oldest_any_end = cd_end; oldest_any = spawn_point; }
                if (!already_used && cd_end < oldest_unused_end) { oldest_unused_end = cd_end; oldest_unused = spawn_point; }
            }

            // Prefer points not yet used this batch so the batch disperses across distinct points.
            if (already_used)
                continue;

            // Respect this point's post-spawn cooldown for PRIMARY selection, but treat it as a SKIP,
            // not a validation failure: it's already recorded as an oldest-cooldown fallback above, so
            // the batch flows to a different point - or, if every point is cooling, to the oldest one
            // (placed at an alternative offset). Routing the cooldown through ValidateSpawnPointForMonster
            // instead would call IncreaseSpawnAttempts and inflate consecutive_spawn_failures (which trips
            // recovery at 5 / emergency at 10) for every cooling point a batch scans.
            if (level.time < effective_cooldown_end(spawn_point))
                continue;

            if (!ValidateSpawnPointForMonster(spawn_point, level.time))
            {
                failed_validation++;
                continue;
            }

            if (developer->integer > 2 && monster_is_flying)
            {
                gi.Com_PrintFmt("FLYING SPAWN POINT: using {} bucket at {}.\n",
                                spawn_point->style == 1 ? "style-1" : "normal",
                                spawn_point->s.origin);
            }

            if (!prefer_farthest)
            {
                used_spawn_points_this_batch.push_back(spawn_point);
                return spawn_point;
            }

            const float dist_sq = MinDistSqToActivePlayer(spawn_point->s.origin);
            if (dist_sq > farthest_dist_sq)
            {
                farthest_dist_sq = dist_sq;
                farthest_point = spawn_point;
            }
            if (!too_close_to_batch(spawn_point) && dist_sq > farthest_spaced_dist_sq)
            {
                farthest_spaced_dist_sq = dist_sq;
                farthest_spaced = spawn_point;
            }
            if (++candidates_seen >= LimitBreakWave::FARTHEST_SPAWN_CANDIDATES)
            {
                edict_t* pick = farthest_spaced ? farthest_spaced : farthest_point;
                used_spawn_points_this_batch.push_back(pick);
                return pick;
            }
        }

        // Best valid & unused farthest point found in the full scan (spaced from batch-mates if one
        // qualified, else plain farthest).
        if (prefer_farthest)
        {
            edict_t* pick = farthest_spaced ? farthest_spaced : farthest_point;
            if (pick)
            {
                used_spawn_points_this_batch.push_back(pick);
                return pick;
            }
        }

        // No valid unused point this scan. Keep the wave flowing by reusing the oldest-cooldown
        // point. Execution ignores cooldown, so the monster still deploys and OnSuccessfulSpawn
        // then refreshes that point's cooldown for the next batch.
        edict_t* fallback = oldest_unused;
        if (!fallback)
        {
            // Every style-compatible point has already been used this batch (batch larger than the
            // point count). Start a fresh round so the overflow distributes across the points again
            // via the round-robin instead of all piling onto the single global-oldest point.
            used_spawn_points_this_batch.clear();
            fallback = oldest_any;
        }
        if (fallback)
        {
            used_spawn_points_this_batch.push_back(fallback);
            return fallback;
        }
        return nullptr;
    };

    auto try_plan_next_monster = [&]() -> bool
    {
        monster_pick_attempts++;
        horde::MonsterTypeID monster_type_id = G_HordePickMonsterType(
            nullptr, currentLevel_param, current_actual_wave_type_param,
            is_retaliation_active_param, is_recovery_mode_active_param,
            original_wave_type_before_recovery_param);

        if (monster_type_id == horde::MonsterTypeID::UNKNOWN)
        {
            failed_monster_pick++;
            return false;
        }

        edict_t* spawn_point = find_spawn_point_for_monster(monster_type_id);
        if (!spawn_point)
        {
            return false;
        }

        // Use safe emplace_back with overflow protection
        if (!safe_emplace_back_limit(g_spawn_system.spawn_plan, MAX_ENTITIES_PER_FRAME, monster_type_id, spawn_point)) {
            gi.Com_Print("WARNING: Spawn plan full, stopping planning\n");
            plan_capacity_exhausted = true;
            return false;
        }

        planned_count++;
        return true;
    };

    // Headroom: a dispersing batch may scan the full point list once per monster (no early return
    // when it has to fall back to the oldest-cooldown point), so budget one extra full scan.
    const size_t max_points_to_check = total_potential_points * (std::max<size_t>(2, static_cast<size_t>(num_to_plan)) + 1);
    const size_t max_monster_pick_attempts = std::max<size_t>(static_cast<size_t>(num_to_plan) * 4, 4);
    while (!plan_capacity_exhausted &&
           planned_count < num_to_plan &&
           points_checked < max_points_to_check &&
           monster_pick_attempts < max_monster_pick_attempts)
    {
        try_plan_next_monster();
    }

/*    if (developer->integer > 1)
    {
        gi.Com_PrintFmt("SPAWN PLAN: Target={}, Planned={}, FailedValidation={}, FailedPick={}, PointsChecked={}/{} (Remaining: {})\n",
                        num_to_plan, planned_count, failed_validation, failed_monster_pick,
                        points_checked, max_points_to_check, g_horde_local.num_to_spawn);
    }*/
}

void PlanNextSpawnBatch()
{
    if (level.intermissiontime) return;
    // developer >= 3: Freeze monster spawning for debugging (bots still disabled at developer >= 2)
    if (developer->integer >= 3 && g_horde->integer) return;

    if (!g_spawn_system.spawn_plan.empty()) {
        return;
    }

    const horde::MapSize& mapSize = g_horde_local.current_map_size;
    const int32_t currentLevel = g_horde_local.level;

    RebuildSpawnPointCacheIfNeeded();
    if (g_spawn_system.potential_spawn_points.empty()) {
        if (g_spawn_system.consecutive_spawn_failures < HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY - 1)
            g_spawn_system.consecutive_spawn_failures++;
        if (!g_spawn_system.need_spawn_cache_reset) {
            g_spawn_system.need_spawn_cache_reset = true;
        }
        return;
    }

    const int32_t activeMonsters = CalculateRemainingMonsters();
    const int32_t softCap = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap : GetMonsterCapForMap(GetCurrentMapName(), mapSize);

    // Integrated logic from the old TrySpawnAmbush function directly here.
    if (g_horde_local.state == horde_state_t::active_wave && activeMonsters < softCap && ShouldTriggerAmbushSpawn()) {
        TriggerAmbush(mapSize, currentLevel);
        // If the trigger was successful, it will have set the special spawn state.
        if (g_spawn_system.special_spawn_state.type == SpecialSpawnType::Ambush) {
            g_spawn_system.consecutive_spawn_failures = 0;
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
            current_wave_type = g_spawn_system.original_wave_type_before_recovery;
        }
    }
    else if (spawnable_this_call_local > 0) {
        PlanMonsterSpawnBatch(
            spawnable_this_call_local,
            currentLevel,
            champion_chance_for_batch,
            g_recovery_mode_active,
            (g_spawn_system.special_spawn_state.type == SpecialSpawnType::Retaliation),
            current_wave_type,
            g_spawn_system.original_wave_type_before_recovery);
    }

    SetNextMonsterSpawnTime(mapSize);
}

// ============================================================================
// SPAWN POINT VALIDATION AND MANAGEMENT
// ============================================================================

void RebuildSpawnPointCacheIfNeeded()
{
    if (!g_spawn_system.spawn_points_cached || g_spawn_system.need_spawn_cache_reset)
    {
        // Ensure spawn map is built first
        if (g_spawn_system.spawn_map_needs_build) {
            BuildSpawnPointMap();
            g_spawn_system.spawn_map_needs_build = false;
        }
        // Reserve capacity before copying to avoid reallocation
        g_spawn_system.potential_spawn_points.clear();
        g_spawn_system.potential_spawn_points.reserve(g_spawn_point_list.size());
        g_spawn_system.potential_spawn_points.assign(g_spawn_point_list.begin(), g_spawn_point_list.end());

        g_spawn_system.cached_flying_spawn_count = 0;
        for (const auto* point : g_spawn_system.potential_spawn_points) {
            if (point->style == 1) {
                g_spawn_system.cached_flying_spawn_count++;
            }
        }

        if (!g_spawn_system.potential_spawn_points.empty())
        {
            std::shuffle(g_spawn_system.potential_spawn_points.begin(), g_spawn_system.potential_spawn_points.end(), mt_rand);
        }

        g_spawn_system.spawn_point_shuffle_index = 0;
        g_spawn_system.spawn_points_cached = true;
        g_spawn_system.need_spawn_cache_reset = false;
        g_spawn_system.consecutive_spawn_failures = 0;

        if (developer->integer > 1)
            gi.Com_PrintFmt("Spawn Point Cache Rebuilt: {} points shuffled ({} flying).\n", g_spawn_system.potential_spawn_points.size(), g_spawn_system.cached_flying_spawn_count);
    }
}

bool ValidateSpawnPointForMonster(edict_t* spawn_point, gtime_t current_time)
{
    if (!spawn_point || !spawn_point->inuse)
        return false;
    const bool is_flying_only_lane = (spawn_point->style == 1);

    // OPTIMIZATION: O(1) vector lookup instead of O(log N) map lookup
    // SAFETY: Use bounds-checked helper to prevent overflow
    const uint16_t index = GetSpawnPointIndexSafe(spawn_point);
    if (index == 0xFFFF) [[unlikely]]
        return false;
    if (index >= g_spawn_system.spawn_validation_cache.size())
        return false;

    // Direct vector access - much faster than hash map
    auto& cached = g_spawn_system.spawn_validation_cache[index];

    // Initialize index if first time
    if (cached.index == 0)
        cached.index = index;

    // Check if we can use cached result (within cache duration and same position)
    if (current_time - cached.last_validation_time < HordeConstants::SPAWN_VALIDATION_CACHE_DURATION)
    {
        cached.InvalidateIfMoved(spawn_point->s.origin);
        if (cached.last_validation_time > 0_sec)
            return cached.last_validation_result;
    }

    // Progressive validation - become more lenient as failures increase
    const bool emergency_mode = g_spawn_system.consecutive_spawn_failures >= HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY;
    const bool recovery_mode = g_spawn_system.consecutive_spawn_failures >= HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY;

    // Perform validation checks
    bool is_valid = CheckSpawnPointCooldowns(index, current_time, emergency_mode);

    if (is_valid && !is_flying_only_lane)
        is_valid = CheckSpawnPointPlayerProximity(spawn_point->s.origin, emergency_mode, recovery_mode);

    // Check occupation (skip in emergency mode)
    if (is_valid && !emergency_mode)
        is_valid = !IsSpawnPointOccupied(spawn_point);

    // Update cache and failure tracking
    if (!is_valid)
        IncreaseSpawnAttempts(spawn_point);

    UpdateSpawnValidationCache(cached, current_time, is_valid, spawn_point->s.origin);

    return is_valid;
}

void DetermineSpawnStrategy(const horde::MapSize& mapSize, int32_t& out_spawnable_this_call, bool& out_use_emergency_spawn, bool& out_recovery_mode_active_ref, float& out_champion_chance, int32_t availableSpace)
{
    // Batch size grows with wave: 6 up to wave 10 (calmer early waves), then 8 from wave 11 on to
    // sustain pressure. Applied uniformly across map sizes per design.
    int32_t base_batch = (current_wave_level > 10) ? 8 : 6;
    // Fog / limit-break waves spawn in bigger batches so the swarm ramps up to the raised cap.
    if (IsLimitBreakWave(current_wave_type))
        base_batch *= 2;
    // Controlled-map rush: bigger batches so the arena refills fast when players are dominating.
    // (Still clamped by availableSpace below, which respects the live cap.)
    if (IsControlledRushActive())
        base_batch += (base_batch + 1) / 2;  // +50% (rounded up)
    out_spawnable_this_call = std::min({g_horde_local.num_to_spawn, base_batch, availableSpace});

    //if (developer->integer >= 2 && out_spawnable_this_call < g_horde_local.num_to_spawn)
    //{
    //    gi.Com_PrintFmt("SPAWN STRATEGY: Wanted={}, BatchCap={}, AvailableSpace={}, Planning={} (Deferred={})\n",
    //                    g_horde_local.num_to_spawn, base_batch, availableSpace,
    //                    out_spawnable_this_call, g_horde_local.num_to_spawn - out_spawnable_this_call);
    //}

    out_use_emergency_spawn = false;
    if (g_spawn_system.consecutive_spawn_failures >= HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY)
    {
        if (developer->integer > 1)
            gi.Com_PrintFmt("EMERGENCY SPAWN TRIGGERED: Failures={}.\n", g_spawn_system.consecutive_spawn_failures);
        out_use_emergency_spawn = true;
    }
    else if (g_spawn_system.consecutive_spawn_failures >= HordeConstants::MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY && !out_recovery_mode_active_ref)
    {
        if (developer->integer)
            gi.Com_PrintFmt("RECOVERY MODE ACTIVATED: Failures={}.\n", g_spawn_system.consecutive_spawn_failures);
        out_recovery_mode_active_ref = true; // Modifies g_recovery_mode_active
        g_spawn_system.original_wave_type_before_recovery = current_wave_type;
        current_wave_type = HasWaveType(current_wave_type, MonsterWaveType::Flying) ? MonsterWaveType::Flying : MonsterWaveType::Ground;
    }

    out_champion_chance = 0.2f;
    if (g_spawn_system.special_spawn_state.type == SpecialSpawnType::Retaliation)
    {
        out_champion_chance = 0.40f;
        if (developer->integer > 1)
            gi.Com_PrintFmt("Retaliation: Champion chance {:.0f}%\n", out_champion_chance * 100.0f);
    }
}

int ExecuteEmergencySpawnProcedure(int32_t spawnable_this_call,
                                    int32_t currentLevel,
                                    float champion_chance_param)
{
    int emergency_spawned_count = 0;
    // Heap optimization: Changed from std::vector to std::array (compile-time, zero heap allocation)
    std::array<vec3_t, HordeConstants::EMERGENCY_SPAWN_LIMIT_PER_CALL> batch_spawn_positions;
    int batch_position_count = 0;

    const int spawn_limit = std::min(spawnable_this_call, HordeConstants::EMERGENCY_SPAWN_LIMIT_PER_CALL);

    for (int i = 0; i < spawn_limit; ++i)
    {
        const horde::MonsterTypeID emergency_type = GetEmergencyMonsterType(currentLevel);

        // Find spawn position with spacing from other monsters in this batch
        vec3_t emergency_origin, emergency_angles;
        if (!FindSpacedEmergencyPosition(emergency_origin, emergency_angles, emergency_type, batch_spawn_positions, batch_position_count))
        {
            if (developer->integer > 1)
                gi.Com_PrintFmt("EMERGENCY SPAWN FAILED for type {}. (From ExecuteEmergencySpawnProcedure)\n",
                                static_cast<int>(emergency_type));
            break; // Stop trying if position finding fails
        }

        // Spawn the monster at the validated position
        edict_t* monster = Horde_SpawnMonster(emergency_origin, emergency_angles, emergency_type, currentLevel, champion_chance_param);
        if (!monster || !monster->inuse || monster->deadflag || monster->health <= 0)
            continue;

        // Track position in both current batch and persistent history
        if (batch_position_count < HordeConstants::EMERGENCY_SPAWN_LIMIT_PER_CALL) {
            batch_spawn_positions[batch_position_count++] = emergency_origin;
        }
        g_emergency_spawn_history.AddPosition(emergency_origin);
        ApplyMonsterSpawnEffects(monster);

        // Update spawn counts
        emergency_spawned_count++;
        if (g_horde_local.num_to_spawn > 0)
            --g_horde_local.num_to_spawn;
        else if (g_horde_local.queued_monsters > 0)
            --g_horde_local.queued_monsters;
    }

    if (emergency_spawned_count > 0)
    {
        g_spawn_system.consecutive_spawn_failures = 0; // Reset failures on any success
        g_spawn_system.consecutive_emergency_failures = 0; // Reset emergency failures too
        if (developer->integer > 1)
            gi.Com_PrintFmt("EMERGENCY SPAWN PROCEDURE: Spawned {}.\n", emergency_spawned_count);
    }
    else
    {
        // FIX: Track consecutive emergency spawn failures
        g_spawn_system.consecutive_emergency_failures++;

        // FIX: Reset recovery mode if emergency spawning is also failing repeatedly
        if (g_recovery_mode_active && 
            g_spawn_system.consecutive_emergency_failures >= HordeConstants::MAX_CONSECUTIVE_EMERGENCY_FAILURES_BEFORE_RESET)
        {
            if (developer->integer)
            {
                gi.Com_PrintFmt("RECOVERY MODE RESET: Emergency spawning failed {} times. "
                               "Resetting to normal wave type and clearing failures.\n",
                               g_spawn_system.consecutive_emergency_failures);
            }

            // Reset recovery mode
            g_recovery_mode_active = false;
            current_wave_type = g_spawn_system.original_wave_type_before_recovery;
            g_spawn_system.original_wave_type_before_recovery = MonsterWaveType::None;

            // Reset failure counters to give the system a fresh start
            g_spawn_system.consecutive_spawn_failures = 0;
            g_spawn_system.consecutive_emergency_failures = 0;

            // Force a spawn cache rebuild to try different spawn points
            g_spawn_system.need_spawn_cache_reset = true;
        }
    }

    return emergency_spawned_count;
}

// ============================================================================
// EMERGENCY SPAWN HISTORY RESET
// ============================================================================

void ResetEmergencySpawnHistory()
{
    g_emergency_spawn_history.Reset();
    if (developer && developer->integer > 1)
    {
        gi.Com_PrintFmt("INFO: Emergency spawn history cleared.\\n");
    }
}
