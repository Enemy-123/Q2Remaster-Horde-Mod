#include "horde_boss.h"
#include "g_horde.h"
#include "../g_local.h"
#include "../shared.h"
#include "g_horde_phys.h"
#include "horde_spawning.h"
#include <algorithm>
#include <boost/container/flat_set.hpp>
#include <cmath>
#include <string>

// Global boss variables
bool boss_spawned_for_wave = false;
RecentBosses recent_bosses;  // Not static - declared extern in header
static BossEligibilityCache g_bossEligibilityCache;
static std::array<BossWaveInfo, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_bossWaveTypeArray;

// External references
extern int16_t current_wave_level;  // Note: defined as int16_t in g_horde.cpp
extern boost::container::flat_set<edict_t *> auto_spawned_bosses;

// External function to get current map size
extern horde::MapSize GetCurrentMapSize();
extern cvar_t *g_horde;
extern cvar_t *developer;
extern MonsterWaveType current_wave_type;
extern horde::MonsterTypeID current_boss_minion_source;  // Boss driving an explicit minion roster (Widow/Widow2/Arachnid)
extern cached_soundindex sound_spawn1;  // Sound for boss spawn

// FIXED: Return struct for position validation
struct PositionValidationResult {
	bool is_valid;
	vec3_t adjusted_position;
};

// Relaxed boss-specific validation - less restrictive than regular monster spawning
// Bosses can handle tighter spaces due to ClearSpawnArea + PushEntitiesAway combo
static PositionValidationResult IsPositionPhysicallyValidForBoss(const vec3_t &position, const vec3_t &monster_mins, const vec3_t &monster_maxs, const bool is_flying)
{
	PositionValidationResult result = {false, position};

	if (!is_valid_vector(position)) return result;

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

	// RELAXED: Sky clearance check - bosses only care about very close sky (64 units for ground, 128 for flying)
	// Much more lenient than regular monsters (128/256 units)
	{
		vec3_t sky_check_start = position;
		sky_check_start.z += monster_maxs.z - monster_mins.z;
		vec3_t sky_check_end = sky_check_start;
		sky_check_end.z += is_flying ? 128.0f : 64.0f; // Reduced from 256/128

		trace_t sky_trace = gi.trace(sky_check_start, vec3_origin, vec3_origin, sky_check_end, nullptr, MASK_SOLID);
		if (sky_trace.surface && (sky_trace.surface->flags & SURF_SKY)) {
			return result; // Sky is very close - still reject
		}
	}

	// Check if spawning above sky brush (in the void)
	{
		vec3_t down_check_start = position;
		down_check_start.z += monster_mins.z;
		vec3_t down_check_end = down_check_start;
		down_check_end.z -= (is_flying ? 2000.0f : 2048.0f);

		trace_t down_trace = gi.trace(down_check_start, vec3_origin, vec3_origin, down_check_end, nullptr, MASK_SOLID);

		if (down_trace.surface && (down_trace.surface->flags & SURF_SKY)) {
			return result; // Spawning above sky
		}

		if (is_flying && down_trace.fraction >= 1.0f) {
			return result; // No ground within 2000 units
		}
	}

	// RELAXED: No extra padding for large monsters - ClearSpawnArea + PushEntitiesAway handles this
	const trace_t trace = gi.trace(position, monster_mins, monster_maxs, position, nullptr, MASK_MONSTERSOLID);
	
	// RELAXED: Allow slight startsolid - bosses have unstuck mechanisms
	// We check that it's not deeply stuck (allsolid would be bad)
	if (trace.allsolid) {
		return result; // Completely stuck - reject
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

static constexpr std::array<vec3_t, 12> BOSS_RADIAL_OFFSETS = {{
	{128.0f, 0.0f, 0.0f},
	{-128.0f, 0.0f, 0.0f},
	{0.0f, 128.0f, 0.0f},
	{0.0f, -128.0f, 0.0f},
	{256.0f, 0.0f, 0.0f},
	{-256.0f, 0.0f, 0.0f},
	{0.0f, 256.0f, 0.0f},
	{0.0f, -256.0f, 0.0f},
	{180.0f, 180.0f, 0.0f},
	{-180.0f, 180.0f, 0.0f},
	{180.0f, -180.0f, 0.0f},
	{-180.0f, -180.0f, 0.0f}
}};

// Try alternative spawn points in a radial pattern around the primary spawn
// Returns true if a valid position was found, with final_pos set to the valid position
static bool TryAlternativeSpawnPoints(const vec3_t &primary_spawn, const vec3_t &predicted_mins, const vec3_t &predicted_maxs, bool is_flying,
	int push_iterations, float push_radius, float push_strength, float push_player_str, float push_monster_str, vec3_t &final_pos)
{

	for (const auto &offset : BOSS_RADIAL_OFFSETS)
	{
		vec3_t test_pos = primary_spawn + offset;

		// OPTIMIZATION: Check geometry first (cheap) before ClearSpawnArea (expensive)
		// Only proceed to ClearSpawnArea if the point isn't inside a wall
		if (gi.pointcontents(test_pos) & MASK_SOLID)
			continue;

		// Clear the alternative spawn area (expensive - calls findradius)
		ClearSpawnArea(test_pos, predicted_mins, predicted_maxs);

		// Validate using relaxed boss checks
		const auto validation = IsPositionPhysicallyValidForBoss(test_pos, predicted_mins, predicted_maxs, is_flying);

		if (validation.is_valid)
		{
			final_pos = validation.adjusted_position;

			// Apply the same push entities logic
			PushEntitiesAway(final_pos, push_iterations, push_radius, push_strength, push_player_str, push_monster_str);

			if (developer->integer > 1)
				gi.Com_PrintFmt("TryAlternativeSpawnPoints: Found valid alternative spawn at {} (offset: {})\n", final_pos, offset);

			return true;
		}
	}

	return false;
}

// External functions
const char* GetCurrentMapName();
extern bool GetPredictedScaledBounds(horde::MonsterTypeID typeId, vec3_t &mins, vec3_t &maxs);
extern void ClearSpawnArea(const vec3_t &origin, const vec3_t &mins, const vec3_t &maxs);
extern void PushEntitiesAway(const vec3_t &origin, int num_pushes, float base_radius, float base_strength, float player_strength, float monster_strength);
extern PositionValidationResult IsPositionPhysicallyValid(const vec3_t &position, const vec3_t &mins, const vec3_t &maxs, bool flying);
extern bool IsFlying(horde::MonsterTypeID typeId);
extern void SP_target_orb(edict_t *ent);
extern void ED_CallSpawnMonsterByID(edict_t* ent, horde::MonsterTypeID typeId);
extern void StoreWaveType(MonsterWaveType wave_type);
extern void AppendHordeMessage(const char *message, gtime_t duration);
extern bool Horde_TeleportMonster(edict_t *self, const vec3_t &destination_origin, const vec3_t &destination_angles, bool play_effects, bool force_despite_visibility);
extern void ConfigureBossArmor(edict_t *self);
extern void ApplyBossEffects(edict_t *self);
extern void StoreWaveType(MonsterWaveType type);
extern const char* GetDisplayName(const edict_t *ent);
extern const char* GetPlayerName(const edict_t *ent);
extern void AppendHordeMessage(const char *message, gtime_t duration);
extern void ImprovedSpawnGrow(const vec3_t &position, float start_size, float end_size, edict_t *owner);
extern void SpawnGrow_Spawn(const vec3_t &position, float start_size, float end_size);
extern bool Horde_TeleportMonster(edict_t *ent, vec3_t *dest, bool force);
extern void OnEntityDeath(edict_t *ent) noexcept;
extern void OnEntityRemoved(edict_t *ent);
extern MonsterWaveType GetMonsterWaveTypes(horde::MonsterTypeID typeId);
extern bool HasWaveType(MonsterWaveType entityTypes, MonsterWaveType typeToCheck) noexcept;

// Hell Maidens helpers (defined further below, before SpawnBossAutomatically). Forward-declared
// here so boss_die() can react when a maiden dies.
static void HealHellMaidenSurvivors(const edict_t *dead_maiden);

// Fixer Trio mutual-heal helper (defined further below). Forward-declared so boss_die() can react
// when a fixer dies; IsFixer() is declared in horde_boss.h.
static void HealFixerSurvivors(const edict_t *dead_fixer);

// Boss spawning constants
namespace {
	// Wave configuration
	// NOTE: boss cadence checks now live in IsBossWaveLevel (g_horde.h), which also
	// handles the Horde 2 variant (every 4 from wave 8). These document the classic values.
	[[maybe_unused]] constexpr int32_t MIN_BOSS_WAVE = 10;
	[[maybe_unused]] constexpr int32_t BOSS_WAVE_INTERVAL = 5;

	// Item drop physics
	constexpr int32_t DROP_MIN_VELOCITY = -800;
	constexpr int32_t DROP_MAX_VELOCITY = 800;
	constexpr int32_t DROP_MIN_VERTICAL_VELOCITY = 400;
	constexpr int32_t DROP_MAX_VERTICAL_VELOCITY = 950;
	constexpr int32_t DROP_POWERUP_MIN_VERTICAL_VELOCITY = 300;
	constexpr int32_t DROP_POWERUP_MAX_VERTICAL_VELOCITY = 400;

	// Spawn effects
	constexpr float SPAWN_GROW_BASE_SIZE_MULTIPLIER = 0.15f;
	constexpr float SPAWN_GROW_MIN_BASE_SIZE = 100.0f;
	constexpr float SPAWN_GROW_END_SIZE_MULTIPLIER = 0.01f;
	constexpr gtime_t BOSS_SPAWN_DELAY = 750_ms;

	// Teleportation cooldowns
	constexpr gtime_t DROWNING_COOLDOWN_BOSS = 4500_ms;
	constexpr gtime_t TRIGGER_HURT_RETRIGGER_COOLDOWN = 3000_ms;
	constexpr gtime_t GENERAL_TELEPORT_COOLDOWN = 2500_ms;

	// Spawn area clearing
	constexpr int32_t PUSH_ITERATIONS = 3;
	constexpr float PUSH_BASE_RADIUS = 768.0f;
	constexpr float PUSH_BASE_STRENGTH = 1500.0f;
	constexpr float PUSH_PLAYER_STRENGTH = 4500.0f;
	constexpr float PUSH_MONSTER_STRENGTH = 2000.0f;

	// Visual effects
	constexpr float WEAPON_DROP_ALPHA = 0.85f;
	constexpr float WEAPON_DROP_SCALE = 1.25f;
	constexpr float POWERUP_DROP_ALPHA = 0.8f;
	constexpr float POWERUP_DROP_SCALE = 1.5f;
	constexpr float TELEPORT_EFFECT_SIZE = 100.0f;

	// Boss announcement timing
	constexpr gtime_t BOSS_ANNOUNCEMENT_DURATION = 4_sec;

	// Boss grid fallback
	constexpr int32_t BOSS_GRID_FALLBACK_ATTEMPTS = 32;
	constexpr int32_t BOSS_GRID_TACTICAL_ATTEMPTS = 16;
	constexpr int32_t BOSS_GRID_TACTICAL_SAMPLE_ATTEMPTS = 12;
	constexpr float BOSS_GRID_MIN_PLAYER_DISTANCE = 512.0f;

	// Boss selection space scoring
	constexpr int32_t BOSS_SPACE_GRID_SAMPLE_ATTEMPTS = 24;
	constexpr int32_t BOSS_SPACE_VALIDATION_CAP = 8;
}

static float GetBossEffectScale(BossSizeCategory sizeCategory)
{
	switch (sizeCategory) {
		case BossSizeCategory::Large:
			return 1.5f;
		case BossSizeCategory::Medium:
			return 1.3f;
		case BossSizeCategory::Small:
			return 1.1f;
	}

	return 1.0f;
}

static void GetBossPlacementBounds(horde::MonsterTypeID typeId, BossSizeCategory sizeCategory, vec3_t &mins, vec3_t &maxs)
{
	GetPredictedScaledBounds(typeId, mins, maxs);

	const float boss_effect_scale = GetBossEffectScale(sizeCategory);
	mins *= boss_effect_scale;
	maxs *= boss_effect_scale;
}

static bool GetBossGridCandidate(int32_t attempt, bool prefer_out_of_visibility, vec3_t &candidate)
{
	if (attempt < BOSS_GRID_TACTICAL_ATTEMPTS)
	{
		return HordePhys::g_spawn_grid.GetTacticalSpawnPosition(
			candidate,
			BOSS_GRID_MIN_PLAYER_DISTANCE,
			BOSS_GRID_TACTICAL_SAMPLE_ATTEMPTS,
			prefer_out_of_visibility);
	}

	return HordePhys::g_spawn_grid.GetRandomPosition(candidate);
}

static bool PrepareBossPlacementAt(const vec3_t &candidate, const vec3_t &predicted_mins, const vec3_t &predicted_maxs,
	bool is_flying, vec3_t &final_pos)
{
	if (!is_valid_vector(candidate))
		return false;

	if (gi.pointcontents(candidate) & MASK_SOLID)
		return false;

	ClearSpawnArea(candidate, predicted_mins, predicted_maxs);

	const auto validation = IsPositionPhysicallyValidForBoss(candidate, predicted_mins, predicted_maxs, is_flying);
	if (!validation.is_valid)
		return false;

	final_pos = validation.adjusted_position;
	PushEntitiesAway(final_pos, PUSH_ITERATIONS, PUSH_BASE_RADIUS, PUSH_BASE_STRENGTH, PUSH_PLAYER_STRENGTH, PUSH_MONSTER_STRENGTH);
	return true;
}

static bool TryBossGridFallbackPosition(const vec3_t &predicted_mins, const vec3_t &predicted_maxs, bool is_flying,
	bool prefer_out_of_visibility, const char *context, vec3_t &final_pos)
{
	if (!HordePhys::g_spawn_grid.IsGenerated())
	{
		if (developer->integer)
			gi.Com_PrintFmt("Boss grid fallback unavailable for {}: spawn grid is not generated.\n", context);
		return false;
	}

	for (int32_t attempt = 0; attempt < BOSS_GRID_FALLBACK_ATTEMPTS; ++attempt)
	{
		vec3_t candidate;
		if (!GetBossGridCandidate(attempt, prefer_out_of_visibility, candidate))
			continue;

		if (!PrepareBossPlacementAt(candidate, predicted_mins, predicted_maxs, is_flying, final_pos))
			continue;

		HordePhys::g_spawn_grid.MarkPositionUsed(final_pos);

		if (developer->integer > 1)
			gi.Com_PrintFmt("Boss grid fallback selected {} position at {}.\n", context, final_pos);

		return true;
	}

	if (developer->integer)
	{
		gi.Com_PrintFmt("Boss grid fallback failed for {} after {} attempts.\n",
			context, BOSS_GRID_FALLBACK_ATTEMPTS);
	}

	return false;
}

static bool TryTeleportBossToGridFallback(edict_t *self, const vec3_t &predicted_mins, const vec3_t &predicted_maxs,
	bool is_flying, const vec3_t &destination_angles, bool force_teleport)
{
	if (!HordePhys::g_spawn_grid.IsGenerated())
	{
		if (developer->integer)
			gi.Com_PrintFmt("CTB: Boss grid fallback unavailable: spawn grid is not generated.\n");
		return false;
	}

	for (int32_t attempt = 0; attempt < BOSS_GRID_FALLBACK_ATTEMPTS; ++attempt)
	{
		vec3_t candidate;
		if (!GetBossGridCandidate(attempt, false, candidate))
			continue;

		vec3_t final_pos;
		if (!PrepareBossPlacementAt(candidate, predicted_mins, predicted_maxs, is_flying, final_pos))
			continue;

		if (!Horde_TeleportMonster(self, final_pos, destination_angles, true, force_teleport))
			continue;

		HordePhys::g_spawn_grid.MarkPositionUsed(final_pos);

		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("CTB: Boss {} teleported via grid fallback to ({},{},{}).\n",
				self->classname ? self->classname : "UNKNOWN",
				self->s.origin[0], self->s.origin[1], self->s.origin[2]);
		}

		return true;
	}

	if (developer->integer)
	{
		gi.Com_PrintFmt("CTB: Boss grid fallback failed for {} after {} attempts.\n",
			self->classname ? self->classname : "UNKNOWN",
			BOSS_GRID_FALLBACK_ATTEMPTS);
	}

	return false;
}

// Boss data arrays
static constexpr std::array<boss_t, 12> BOSS_SMALL_SRC = {{
	{horde::MonsterTypeID::CARRIER_MINI, 24, -1, 0.1f, BossSizeCategory::Small},
	{horde::MonsterTypeID::BOSS2_KL, 24, -1, 0.1f, BossSizeCategory::Small},
	{horde::MonsterTypeID::FIXBOT_KL, 15, -1, 0.2f, BossSizeCategory::Small},
	{horde::MonsterTypeID::WIDOW2, 19, -1, 0.15f, BossSizeCategory::Small},
	{horde::MonsterTypeID::TANK_64, -1, -1, 0.25f, BossSizeCategory::Small},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Small},
	{horde::MonsterTypeID::GUNCMDR_KL, -1, 20, 0.3f, BossSizeCategory::Small},
	{horde::MonsterTypeID::MAKRON_KL, 36, -1, 0.2f, BossSizeCategory::Small},
	{horde::MonsterTypeID::MAKRON, 16, 26, 0.1f, BossSizeCategory::Small},
	{horde::MonsterTypeID::PSX_ARACHNID, 15, -1, 0.1f, BossSizeCategory::Small},
	{horde::MonsterTypeID::REDMUTANT, -1, 24, 0.1f, BossSizeCategory::Small},
	// Hell Praetors mini-raid (3 buffed chicks). Intercepted in SpawnBossAutomatically and spawned as
	// the trio; sits in the pool like any other boss so recent_bosses spaces it from repeating.
	{horde::MonsterTypeID::CHICKKL, 15, -1, 0.15f, BossSizeCategory::Small}
}};

static constexpr std::array<boss_t, 12> BOSS_MEDIUM_SRC = {{
	{horde::MonsterTypeID::CARRIER, 24, -1, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::BOSS2, 19, -1, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::FIXBOT_KL, 15, -1, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::TANK_64, 21, -1, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::GUNCMDR_KL, 15, -1, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::PSX_GUARDIAN, -1, 24, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::WIDOW2, 19, -1, 0.15f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::PSX_ARACHNID, 14, -1, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::MAKRON_KL, 26, -1, 0.2f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::MAKRON, 16, 25, 0.1f, BossSizeCategory::Medium},
	// Hell Praetors mini-raid (intercepted in SpawnBossAutomatically; see small pool note).
	{horde::MonsterTypeID::CHICKKL, 15, -1, 0.15f, BossSizeCategory::Medium}
}};

static constexpr std::array<boss_t, 14> BOSS_LARGE_SRC = {{
	{horde::MonsterTypeID::CARRIER, 24, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::BOSS2, 19, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::FIXBOT_KL, 15, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::BOSS5, -1, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::TANK_64, -1, 20, 0.45f, BossSizeCategory::Large},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Large},
	{horde::MonsterTypeID::GUNCMDR_KL, -1, 20, 0.3f, BossSizeCategory::Large},
	{horde::MonsterTypeID::PSX_ARACHNID, 14, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::WIDOW, -1, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::PSX_GUARDIAN, -1, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::JORG, 30, -1, 0.15f, BossSizeCategory::Large},
	{horde::MonsterTypeID::MAKRON_KL, 30, -1, 0.2f, BossSizeCategory::Large},
	{horde::MonsterTypeID::WIDOW2, 25, -1, 0.15f, BossSizeCategory::Large},
	// Hell Praetors mini-raid (intercepted in SpawnBossAutomatically; see small pool note).
	{horde::MonsterTypeID::CHICKKL, 15, -1, 0.15f, BossSizeCategory::Large}
}};

// Compile-time transformation function
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

// Global boss data instances
const BossDataSoA g_smallBossData = create_boss_soa(BOSS_SMALL_SRC);
const BossDataSoA g_mediumBossData = create_boss_soa(BOSS_MEDIUM_SRC);
const BossDataSoA g_largeBossData = create_boss_soa(BOSS_LARGE_SRC);

// RecentBosses implementation
RecentBosses::RecentBosses() : count(0)
{
	items.fill(horde::MonsterTypeID::UNKNOWN);
}

void RecentBosses::add(horde::MonsterTypeID boss_id)
{
	if (boss_id == horde::MonsterTypeID::UNKNOWN)
		return;

	// Keep the list unique: if this boss already exists, move it to the most recent slot.
	for (size_t i = 0; i < count; ++i)
	{
		if (items[i] != boss_id)
			continue;

		for (size_t j = i; j + 1 < count; ++j)
			items[j] = items[j + 1];

		items[count - 1] = boss_id;
		return;
	}

	if (count < MAX_RECENT_BOSSES)
	{
		items[count++] = boss_id;
	}
	else
	{
		// Shift left to make room for the new entry
		for (size_t i = 0; i < MAX_RECENT_BOSSES - 1; ++i)
		{
			items[i] = items[i + 1];
		}
		items[MAX_RECENT_BOSSES - 1] = boss_id;
	}
}

void RecentBosses::add(const char *boss_classname)
{
	if (!boss_classname)
		return;
	add(horde::MonsterTypeRegistry::GetTypeID(boss_classname));
}

bool RecentBosses::contains(horde::MonsterTypeID boss_id) const
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

bool RecentBosses::contains(horde::MonsterTypeID boss_id, size_t limit) const
{
	if (boss_id == horde::MonsterTypeID::UNKNOWN)
		return false;

	// Only check the most recent 'limit' entries
	const size_t start_index = (count > limit) ? (count - limit) : 0;

	for (size_t i = start_index; i < count; ++i)
	{
		if (items[i] == boss_id)
			return true;
	}
	return false;
}

bool RecentBosses::contains(const char *boss_classname) const
{
	if (!boss_classname)
		return false;
	return contains(horde::MonsterTypeRegistry::GetTypeID(boss_classname));
}

void RecentBosses::clear()
{
	count = 0;
	items.fill(horde::MonsterTypeID::UNKNOWN);
}

// BossPickResult implementation
BossPickResult::BossPickResult(horde::MonsterTypeID id, BossSizeCategory size)
	: typeId(id), sizeCategory(size)
{
}

// BossEligibilityCache implementation
void BossEligibilityCache::initialize()
{
	for (int32_t wave = 1; wave <= MAX_PRECOMPUTED_WAVE; ++wave)
	{
		for (size_t map_size_idx = 0; map_size_idx < 3; ++map_size_idx)
		{
			const BossDataSoA* boss_list_soa = nullptr;

			// Determine which boss list based on map size index
			if (map_size_idx == 0)
				boss_list_soa = &g_smallBossData;
			else if (map_size_idx == 1)
				boss_list_soa = &g_mediumBossData;
			else if (map_size_idx == 2)
				boss_list_soa = &g_largeBossData;

			if (!boss_list_soa)
				continue;

			EligibilityData& eligibilityData = cache[wave][map_size_idx];
			eligibilityData.count = 0;

			// Filter bosses by iterating through the SoA data
			for (size_t i = 0; i < boss_list_soa->count; ++i)
			{
				const int32_t min_level = boss_list_soa->min_levels[i];
				const int32_t max_level = boss_list_soa->max_levels[i];

				bool eligible = (min_level == -1 || wave >= min_level) &&
								(max_level == -1 || wave <= max_level);


				if (eligible)
				{
					eligibilityData.soa_indices[eligibilityData.count++] = i;
				}
			}
		}
	}
}

const BossEligibilityCache::EligibilityData&
BossEligibilityCache::get(int32_t wave, horde::MapSize size) const
{
	const int32_t safe_wave = std::clamp(wave, 1, MAX_PRECOMPUTED_WAVE);
	// Determine map size index based on MapSize struct
	size_t map_size_idx = size.isSmallMap ? 0 : (size.isBigMap ? 2 : 1);

	const auto& result = cache[safe_wave][map_size_idx];


	return result;
}

// Helper function to get boss list based on map size
const BossDataSoA *GetBossListSoA(const horde::MapSize &mapSize)
{
	if (mapSize.isSmallMap)
		return &g_smallBossData;
	else if (mapSize.isMediumMap)
		return &g_mediumBossData;
	else if (mapSize.isBigMap)
		return &g_largeBossData;

	return nullptr;
}

struct WeightedBossCandidate
{
	horde::MonsterTypeID typeId = horde::MonsterTypeID::UNKNOWN;
	BossSizeCategory sizeCategory = BossSizeCategory::Medium;
	float weight = 0.0f;
	float cumulativeWeight = 0.0f;
	int32_t validPlacementCount = 0;
};

static const char *BossSizeCategoryName(BossSizeCategory sizeCategory)
{
	switch (sizeCategory) {
		case BossSizeCategory::Small:
			return "small";
		case BossSizeCategory::Medium:
			return "medium";
		case BossSizeCategory::Large:
			return "large";
	}

	return "unknown";
}

static int32_t GetBossSizeRank(BossSizeCategory sizeCategory)
{
	switch (sizeCategory) {
		case BossSizeCategory::Small:
			return 0;
		case BossSizeCategory::Medium:
			return 1;
		case BossSizeCategory::Large:
			return 2;
	}

	return 1;
}

static int32_t GetPreferredBossSizeRank(const horde::MapSize &mapSize)
{
	if (mapSize.isSmallMap)
		return 0;
	if (mapSize.isBigMap)
		return 2;
	return 1;
}

static float GetMapBossSizePreferenceMultiplier(const horde::MapSize &mapSize, BossSizeCategory sizeCategory)
{
	const int32_t delta = GetBossSizeRank(sizeCategory) - GetPreferredBossSizeRank(mapSize);
	if (delta == 0)
		return 1.0f;
	if (delta < 0)
		return delta == -1 ? 0.75f : 0.55f;
	return delta == 1 ? 0.70f : 0.35f;
}

static float GetBossSpaceMultiplier(int32_t validPlacementCount)
{
	if (validPlacementCount >= BOSS_SPACE_VALIDATION_CAP)
		return 1.25f;
	if (validPlacementCount >= 5)
		return 1.0f;
	if (validPlacementCount >= 3)
		return 0.75f;
	if (validPlacementCount >= 1)
		return 0.45f;
	return 0.0f;
}

static bool IsBossEntryEligibleForWave(const BossDataSoA &bossList, size_t index, int32_t waveNumber)
{
	const int32_t min_level = bossList.min_levels[index];
	const int32_t max_level = bossList.max_levels[index];

	return (min_level == -1 || waveNumber >= min_level) &&
		   (max_level == -1 || waveNumber <= max_level);
}

static bool IsBossPlacementCandidatePhysicallyValid(const vec3_t &position, const vec3_t &predicted_mins,
	const vec3_t &predicted_maxs, bool is_flying)
{
	if (!is_valid_vector(position))
		return false;

	if (gi.pointcontents(position) & MASK_SOLID)
		return false;

	return IsPositionPhysicallyValidForBoss(position, predicted_mins, predicted_maxs, is_flying).is_valid;
}

static int32_t CountValidBossPlacements(horde::MonsterTypeID typeId, BossSizeCategory sizeCategory, const char *mapname)
{
	vec3_t predicted_mins, predicted_maxs;
	GetBossPlacementBounds(typeId, sizeCategory, predicted_mins, predicted_maxs);
	const bool is_flying = IsFlying(typeId);
	int32_t valid_count = 0;

	auto count_if_valid = [&](const vec3_t &position) {
		if (valid_count >= BOSS_SPACE_VALIDATION_CAP)
			return;
		if (IsBossPlacementCandidatePhysicallyValid(position, predicted_mins, predicted_maxs, is_flying))
			valid_count++;
	};

	vec3_t boss_origin;
	const horde::MapID mapId = horde::MapOriginRegistry::GetMapID(mapname);
	if (horde::MapOriginRegistry::GetOrigin(mapId, boss_origin))
	{
		count_if_valid(boss_origin);

		for (const vec3_t &offset : BOSS_RADIAL_OFFSETS)
		{
			count_if_valid(boss_origin + offset);
		}
	}

	if (HordePhys::g_spawn_grid.IsGenerated())
	{
		for (int32_t attempt = 0; attempt < BOSS_SPACE_GRID_SAMPLE_ATTEMPTS && valid_count < BOSS_SPACE_VALIDATION_CAP; ++attempt)
		{
			vec3_t grid_position;
			if (!HordePhys::g_spawn_grid.GetRandomPosition(grid_position))
				break;

			count_if_valid(grid_position);
		}
	}

	return valid_count;
}

static size_t CountEligibleBossEntries(const std::array<const BossDataSoA *, 3> &bossLists, int32_t waveNumber)
{
	size_t eligible_count = 0;
	for (const BossDataSoA *boss_list : bossLists)
	{
		if (!boss_list)
			continue;

		for (size_t i = 0; i < boss_list->count; ++i)
		{
			if (boss_list->weights[i] > 0.0f && IsBossEntryEligibleForWave(*boss_list, i, waveNumber))
				eligible_count++;
		}
	}

	return eligible_count;
}

// Distinctive "feel" of a boss's wave, used to space wave TYPES apart (recent_bosses already spaces
// the same boss). Ground+Heavy is the common majority, so it's lumped as Generic and NOT spaced --
// spacing it would starve the pool. Flying/Shambler/Spawner/Mutant are the distinctive ones.
enum class BossWaveSignature { Generic, Flying, Shambler, Spawner, Mutant };

static BossWaveSignature GetBossWaveSignature(horde::MonsterTypeID typeId)
{
	const MonsterWaveType wt = GetBossWaveType(typeId).first;
	if (HasWaveType(wt, MonsterWaveType::Flying))   return BossWaveSignature::Flying;
	if (HasWaveType(wt, MonsterWaveType::Shambler)) return BossWaveSignature::Shambler;
	if (HasWaveType(wt, MonsterWaveType::Spawner))  return BossWaveSignature::Spawner;
	if (HasWaveType(wt, MonsterWaveType::Mutant))   return BossWaveSignature::Mutant;
	return BossWaveSignature::Generic;
}

// Soft weight penalty so a distinctive wave type doesn't stack back-to-back across DIFFERENT bosses
// (recent_bosses only spaces the same boss by id). E.g. fixer -> hornet -> carrier are three
// different bosses but all Flying; this keeps the 2nd/3rd flying pick from winning unless nothing
// else is eligible. Compares the candidate against the last few recent bosses' signatures.
static float GetBossWaveTypeRepeatPenalty(horde::MonsterTypeID candidate)
{
	const BossWaveSignature sig = GetBossWaveSignature(candidate);
	if (sig == BossWaveSignature::Generic)
		return 1.0f; // ground-heavy majority: don't space it or the pool starves

	constexpr size_t WAVE_TYPE_SPACING_WINDOW = 2;     // avoid the same feel within the last 2 bosses
	constexpr float  WAVE_TYPE_REPEAT_PENALTY = 0.12f; // strong soft-exclusion (never a hard zero)

	const size_t window = std::min<size_t>(WAVE_TYPE_SPACING_WINDOW, recent_bosses.count);
	for (size_t k = 0; k < window; ++k)
	{
		const horde::MonsterTypeID recent = recent_bosses.items[recent_bosses.count - 1 - k];
		if (GetBossWaveSignature(recent) == sig)
			return WAVE_TYPE_REPEAT_PENALTY;
	}
	return 1.0f;
}

static size_t BuildWeightedBossCandidates(const std::array<const BossDataSoA *, 3> &bossLists,
	const horde::MapSize &mapSize, const char *mapname, int32_t waveNumber, bool excludeRecent, size_t recentLimit,
	bool requireSpaceValidation, std::array<WeightedBossCandidate, BossDataSoA::MAX_BOSSES * 3> &weightedBosses,
	float &totalWeight)
{
	size_t count = 0;
	totalWeight = 0.0f;

	for (const BossDataSoA *boss_list : bossLists)
	{
		if (!boss_list)
			continue;

		for (size_t i = 0; i < boss_list->count; ++i)
		{
			const horde::MonsterTypeID typeId = boss_list->typeIds[i];
			const BossSizeCategory sizeCategory = boss_list->sizeCategories[i];

			if (!IsBossEntryEligibleForWave(*boss_list, i, waveNumber))
				continue;

			if (excludeRecent && recent_bosses.contains(typeId, recentLimit))
				continue;

			const int32_t valid_placements = requireSpaceValidation ?
				CountValidBossPlacements(typeId, sizeCategory, mapname) :
				BOSS_SPACE_VALIDATION_CAP;

			const float space_multiplier = GetBossSpaceMultiplier(valid_placements);
			if (space_multiplier <= 0.0f)
				continue;

			const float size_preference = GetMapBossSizePreferenceMultiplier(mapSize, sizeCategory);
			// Wave-type spacing: deprioritize a boss whose distinctive wave feel (e.g. Flying) matches
			// a recent boss, so flying/shambler/spawner waves don't run back-to-back.
			const float wave_type_penalty = GetBossWaveTypeRepeatPenalty(typeId);
			const float weight = boss_list->weights[i] * size_preference * space_multiplier * wave_type_penalty;
			if (weight <= 0.0f)
				continue;

			if (count >= weightedBosses.size())
				break;

			totalWeight += weight;
			weightedBosses[count] = { typeId, sizeCategory, weight, totalWeight, valid_placements };
			count++;
		}
	}

	return count;
}

// Boss selection function
BossPickResult G_HordePickBOSSType(const horde::MapSize& mapSize, std::string_view mapname, int32_t waveNumber, bool restrictToMapSizePool)
{
	// Ensure cache is initialized (call InitializeBossWaveTypes which handles the one-time init)
	if (!g_bossWaveTypeArray[0].announcement)
		InitializeBossWaveTypes();

	const std::string map_name_storage(mapname);
	const char *map_name = map_name_storage.c_str();
	// Maps with a boss_size override hard-restrict the pool to the single matching list
	// so their bosses (and resulting s.scale) never come from a larger pool.
	const std::array<const BossDataSoA *, 3> all_boss_lists = restrictToMapSizePool
		? std::array<const BossDataSoA *, 3>{ GetBossListSoA(mapSize), nullptr, nullptr }
		: std::array<const BossDataSoA *, 3>{ &g_smallBossData, &g_mediumBossData, &g_largeBossData };
	const size_t eligible_entry_count = CountEligibleBossEntries(all_boss_lists, waveNumber);
	if (eligible_entry_count == 0)
	{
		if (developer && developer->integer)
			gi.Com_PrintFmt("WARNING: No bosses eligible for wave {}.\n", waveNumber);
		return BossPickResult();
	}

	const size_t max_recent_to_track = std::clamp(
		eligible_entry_count * 2 / 5,
		static_cast<size_t>(2),
		static_cast<size_t>(RecentBosses::MAX_RECENT_BOSSES));

	std::array<WeightedBossCandidate, BossDataSoA::MAX_BOSSES * 3> weightedBosses{};
	float totalWeight = 0.0f;
	size_t weightedCount = BuildWeightedBossCandidates(
		all_boss_lists,
		mapSize,
		map_name,
		waveNumber,
		true,
		max_recent_to_track,
		true,
		weightedBosses,
		totalWeight);

	if (developer && developer->integer > 1)
	{
		gi.Com_PrintFmt("Boss Selection: {} eligible entries, {} space-valid after recent filtering, tracking {} recent.\n",
			eligible_entry_count, weightedCount, max_recent_to_track);
	}

	// If no non-recent bosses found, use all eligible bosses
	if (weightedCount == 0)
	{
		if (developer && developer->integer > 1)
			gi.Com_PrintFmt("INFO: No non-recent space-valid bosses eligible, ignoring history for this pick.\n");

		weightedCount = BuildWeightedBossCandidates(
			all_boss_lists,
			mapSize,
			map_name,
			waveNumber,
			false,
			max_recent_to_track,
			true,
			weightedBosses,
			totalWeight);
	}

	if (weightedCount == 0)
	{
		const BossDataSoA *fallback_list = GetBossListSoA(mapSize);
		const std::array<const BossDataSoA *, 3> fallback_lists = { fallback_list, nullptr, nullptr };

		if (developer && developer->integer)
		{
			gi.Com_PrintFmt("WARNING: No physically validated boss candidates for map {} wave {}. Using map-size fallback pool.\n",
				map_name, waveNumber);
		}

		weightedCount = BuildWeightedBossCandidates(
			fallback_lists,
			mapSize,
			map_name,
			waveNumber,
			true,
			max_recent_to_track,
			false,
			weightedBosses,
			totalWeight);

		if (weightedCount == 0)
		{
			weightedCount = BuildWeightedBossCandidates(
				fallback_lists,
				mapSize,
				map_name,
				waveNumber,
				false,
				max_recent_to_track,
				false,
				weightedBosses,
				totalWeight);
		}
	}

	if (weightedCount == 0 || totalWeight <= 0.0f)
	{
		if (developer && developer->integer)
			gi.Com_PrintFmt("WARNING: No eligible bosses found after all filtering.\n");
		return BossPickResult();
	}

	// Pick a random boss based on weights.
	float randomValue = frandom(totalWeight);
	auto it = std::lower_bound(
		weightedBosses.begin(),
		weightedBosses.begin() + weightedCount,
		randomValue,
		[](const WeightedBossCandidate &boss, float value)
		{
			return boss.cumulativeWeight < value;
		});

	if (it == weightedBosses.begin() + weightedCount)
		it = weightedBosses.begin() + (weightedCount - 1);

	if (developer && developer->integer > 1)
	{
		const char* chosen_name = horde::MonsterTypeRegistry::GetClassname(it->typeId);
		gi.Com_PrintFmt("Selected Boss: {} ({}, Weight: {:.2f}, SpaceScore: {})\n",
			chosen_name ? chosen_name : "Unknown",
			BossSizeCategoryName(it->sizeCategory),
			it->weight,
			it->validPlacementCount);
	}

	return BossPickResult(it->typeId, it->sizeCategory);
}

// Helper function to select boss weapon drop
item_id_t SelectBossWeaponDrop(int32_t wave_level)
{
	// Define potential weapon drops with their minimum required wave level
	static const std::array<std::pair<item_id_t, int32_t>, 8> boss_weapon_drops = { {
		{IT_WEAPON_HYPERBLASTER, 9},
		{IT_WEAPON_RLAUNCHER, 9},
		{IT_WEAPON_PHALANX, 9},	   // Xatrix
		{IT_WEAPON_IONRIPPER, 9},  // Xatrix (originally boomer)
		{IT_WEAPON_PLASMABEAM, 9}, // Rogue
		{IT_WEAPON_RAILGUN, 9},	   // Moved slightly later
		{IT_WEAPON_DISRUPTOR, 19}, // Rogue (originally disintegrator)
		{IT_WEAPON_BFG, 19}		   // Moved slightly later
	} };

	// Use a stack-allocated array to store indices of eligible weapons
	std::array<size_t, boss_weapon_drops.size()> eligible_indices;
	size_t eligible_count = 0;

	for (size_t i = 0; i < boss_weapon_drops.size(); ++i)
	{
		if (wave_level >= boss_weapon_drops[i].second)
		{
			eligible_indices[eligible_count++] = i;
		}
	}

	// If no weapons are eligible, return IT_NULL
	if (eligible_count == 0)
	{
		return IT_NULL;
	}

	// Select a random index from our list of eligible indices
	const size_t random_eligible_index = static_cast<size_t>(irandom(static_cast<int32_t>(eligible_count)));
	const size_t chosen_weapon_array_index = eligible_indices[random_eligible_index];

	return boss_weapon_drops[chosen_weapon_array_index].first;
}

void BossDeathHandler(edict_t *boss) noexcept
{
	// Fast early-out with combined validation
	if (!g_horde || !g_horde->integer || !boss || !boss->inuse || !boss->monsterinfo.IS_BOSS ||
		boss->monsterinfo.BOSS_DEATH_HANDLED || boss->health > 0)
	{
		return;
	}

	// Mark as handled immediately
	boss->monsterinfo.BOSS_DEATH_HANDLED = true;

	// Don't reset boss_spawned_for_wave here - it should remain true for the entire wave
	// to prevent spawning multiple bosses. It will be reset when starting a new wave.
	// boss_spawned_for_wave = false;  // REMOVED - this was causing instant respawn
	if (developer->integer)
	{
		gi.Com_PrintFmt("BossDeathHandler: Boss died on wave {}. Flag remains set to prevent respawn.\n", current_wave_level);
	}

	// Clean up entity tracking
	OnEntityDeath(boss);
	OnEntityRemoved(boss);

	// Drop items
	static const std::array<item_id_t, 9> standardItemIDs = {
		IT_ITEM_ADRENALINE, IT_ITEM_PACK, IT_ITEM_SENTRYGUN,
		IT_ITEM_SPHERE_DEFENDER, IT_ARMOR_COMBAT, IT_ITEM_BANDOLIER,
		IT_ITEM_INVULNERABILITY, IT_AMMO_NUKE, IT_ITEM_STROGGSUMM };

	// Boss-group loot share: members of a multi-boss encounter (Hell Praetors, Fixer Trio) split a
	// single boss's loot so the group doesn't drop N x the items. A count of 0/1 is a lone boss and
	// keeps the full drop. The weapon goes to member 0, the power-up to a different member, and the
	// standard items are partitioned across the group by index.
	const int32_t loot_share_count = std::max<int32_t>(1, boss->monsterinfo.boss_loot_share_count);
	const int32_t loot_share_index = boss->monsterinfo.boss_loot_share_index;
	const bool drops_weapon  = (loot_share_index == 0);
	const bool drops_powerup = (loot_share_index == (loot_share_count > 1 ? 1 : 0));

	// Drop primary weapon
	item_id_t weapon_id = drops_weapon ? SelectBossWeaponDrop(current_wave_level) : IT_NULL;
	if (weapon_id != IT_NULL)
	{
		if (gitem_t *weapon_item = GetItemByIndex(weapon_id))
		{
			if (edict_t *weapon = Drop_Item(boss, weapon_item))
			{
				weapon->s.origin = boss->s.origin;
				weapon->velocity = {
					static_cast<float>(irandom(DROP_MIN_VELOCITY, DROP_MAX_VELOCITY)),
					static_cast<float>(irandom(DROP_MIN_VELOCITY, DROP_MAX_VELOCITY)),
					static_cast<float>(irandom(DROP_MIN_VERTICAL_VELOCITY, DROP_MAX_VERTICAL_VELOCITY))};
				weapon->movetype = MOVETYPE_BOUNCE;
				weapon->s.effects = EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER;
				weapon->s.renderfx = RF_GLOW;
				weapon->s.alpha = WEAPON_DROP_ALPHA;
				weapon->s.scale = WEAPON_DROP_SCALE;
				weapon->spawnflags = SPAWNFLAG_ITEM_DROPPED_PLAYER;
				weapon->flags &= ~FL_RESPAWN;
				gi.linkentity(weapon);
			}
		}
	}

	// Drop special power-up
	item_id_t special_id = drops_powerup ? (brandom() ? IT_ITEM_QUADFIRE : IT_ITEM_QUAD) : IT_NULL;
	if (special_id != IT_NULL)
	if (gitem_t *special_item = GetItemByIndex(special_id))
	{
		if (edict_t *powerup = Drop_Item(boss, special_item))
		{
			powerup->s.origin = boss->s.origin;
			powerup->velocity = {
				static_cast<float>(irandom(DROP_MIN_VELOCITY, DROP_MAX_VELOCITY)),
				static_cast<float>(irandom(DROP_MIN_VELOCITY, DROP_MAX_VELOCITY)),
				static_cast<float>(irandom(DROP_POWERUP_MIN_VERTICAL_VELOCITY, DROP_POWERUP_MAX_VERTICAL_VELOCITY))};
			powerup->movetype = MOVETYPE_BOUNCE;
			powerup->s.effects = EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER | EF_HOLOGRAM;
			powerup->s.alpha = POWERUP_DROP_ALPHA;
			powerup->s.scale = POWERUP_DROP_SCALE;
			powerup->flags &= ~FL_RESPAWN;
			powerup->spawnflags = SPAWNFLAG_ITEM_DROPPED_PLAYER;
			gi.linkentity(powerup);
		}
	}

	// Randomize and drop standard items
	std::array<item_id_t, standardItemIDs.size()> shuffledIDs = standardItemIDs;
	std::shuffle(shuffledIDs.begin(), shuffledIDs.end(), mt_rand);

	for (size_t item_slot = 0; item_slot < shuffledIDs.size(); ++item_slot)
	{
		// Partition the standard items across the group: a lone boss (count 1) drops them all,
		// while group members each drop their slice (count totals back to the full set).
		if (static_cast<int32_t>(item_slot % loot_share_count) != loot_share_index)
			continue;

		item_id_t item_id = shuffledIDs[item_slot];
		if (item_id == IT_NULL)
			continue;

		if (gitem_t *item = GetItemByIndex(item_id))
		{
			if (edict_t *drop = Drop_Item(boss, item))
			{
				drop->s.origin = boss->s.origin;
				drop->velocity = {
					static_cast<float>(irandom(DROP_MIN_VELOCITY, DROP_MAX_VELOCITY)),
					static_cast<float>(irandom(DROP_MIN_VELOCITY, DROP_MAX_VELOCITY)),
					static_cast<float>(irandom(DROP_MIN_VERTICAL_VELOCITY, DROP_MAX_VERTICAL_VELOCITY))};
				drop->movetype = MOVETYPE_BOUNCE;
				drop->flags &= ~FL_RESPAWN;
				drop->s.effects |= EF_GIB;
				drop->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
				gi.linkentity(drop);
			}
		}
	}

	// Clean up boss entity
	gi.linkentity(boss);
}

void ClearBossHealthBar(const edict_t* boss)
{
    if (!boss) return;

    for (size_t i = 0; i < MAX_HEALTH_BARS; ++i) {
        if (level.health_bar_entities[i] && level.health_bar_entities[i]->enemy == boss) {
            G_FreeEdict(level.health_bar_entities[i]);
            level.health_bar_entities[i] = nullptr;
            break;
        }
    }

    // If this boss was the one on the HUD, clear the name
    if (strcmp(gi.get_configstring(CONFIG_HEALTH_BAR_NAME), GetDisplayName(boss)) == 0) {
        gi.configstring(CONFIG_HEALTH_BAR_NAME, "");
    }
}

void boss_die(edict_t *boss) noexcept
{
	if (!boss || !boss->inuse || !g_horde->integer ||
		!boss->monsterinfo.IS_BOSS || boss->health > 0 ||
		boss->monsterinfo.BOSS_DEATH_HANDLED ||
		auto_spawned_bosses.find(boss) == auto_spawned_bosses.end())
	{
		return;
	}

	// Hell Maidens: when one sister falls, the survivors are fully restored (health + power shield),
	// so the encounter resets unless the players burn all three down quickly. The shared aggregate
	// health bar belongs to the invisible aggregator (not this maiden), so ClearBossHealthBar below
	// only tears down this maiden's (nonexistent) individual bar, not the group's bar.
	if (IsHellMaiden(boss))
		HealHellMaidenSurvivors(boss);

	// Fixer Trio: same mutual-heal mechanic — surviving fixers are fully restored when one falls, so
	// the group resets unless all three are burned down quickly. Their turrets are handled separately
	// in fixbot_die (kept alive until the last fixer dies).
	if (IsFixer(boss))
		HealFixerSurvivors(boss);

	BossDeathHandler(boss);
	ClearBossHealthBar(boss);
}

// Health bar management functions
void AttachHealthBar(edict_t *boss)
{
	// Find empty slot in health bar array
	size_t empty_slot = MAX_HEALTH_BARS;
	for (size_t i = 0; i < MAX_HEALTH_BARS; ++i)
	{
		if (!level.health_bar_entities[i])
		{
			empty_slot = i;
			break;
		}
	}

	if (empty_slot == MAX_HEALTH_BARS)
		return;

	edict_t *healthbar = G_Spawn();
	healthbar->classname = "misc_health_bar";
	healthbar->s.origin = boss->s.origin + vec3_t{0, 0, 20};
	healthbar->movetype = MOVETYPE_NONE;
	healthbar->solid = SOLID_NOT;
	
	//healthbar->spawnflags |= SPAWNFLAG_HEALTHBAR_PVS_ONLY;  //add a misc option for this later?
	healthbar->target = boss->targetname;

	// Store display name
	const char* boss_name = GetDisplayName(boss);
	healthbar->message = G_CopyString(boss_name, TAG_LEVEL);

	// Set enemy pointer
	healthbar->enemy = boss;

	// Register and link
	SetHealthBarName(boss);
	level.health_bar_entities[empty_slot] = healthbar;
	gi.linkentity(healthbar);
}

void SetHealthBarName(const edict_t *boss)
{
	// Guard against invalid input
	if (!boss || !boss->inuse)
	{
		return;
	}

	// Get the display name
	const char* display_name = GetDisplayName(boss);

	// Set the configstring
	gi.configstring(CONFIG_HEALTH_BAR_NAME, display_name);
}

// Handle forced boss removal when replacing with a new boss
void HandleForcedBossRemoval(edict_t *boss)
{
	// Safety checks: Ensure we have a valid, living boss that hasn't been handled yet
	if (!boss || !boss->inuse || !boss->monsterinfo.IS_BOSS || boss->monsterinfo.BOSS_DEATH_HANDLED || boss->health <= 0)
	{
		return;
	}

	// Mark as handled
	boss->monsterinfo.BOSS_DEATH_HANDLED = true;

	// Find attacker for damage credit
	edict_t *attacker = boss->enemy;

	// Find a valid player attacker
	if (!attacker || !attacker->client)
	{
		for (size_t i = 0; i < game.maxclients; i++)
		{
			edict_t *player = &g_edicts[1 + i];
			if (player->inuse && player->client)
			{
				attacker = player;
				break;
			}
		}
	}

	// Credit damage if we have a valid attacker
	if (attacker && attacker->client)
	{
		attacker->client->total_damage += boss->health;
		if (developer->integer)
		{
                gi.Com_PrintFmt("Forced Boss Removal: Attributed {} remaining HP from '{}' to '{}'.\n",
                boss->health, GetDisplayName(boss), GetPlayerName(attacker));
		}
	}

	// Simulate the boss's death state without triggering normal death effects or drops
	if (!boss->deadflag)
	{
		boss->monsterinfo.melee = nullptr;
	}
	boss->health = 0;
	boss->deadflag = true;
	boss->takedamage = false;
	boss->solid = SOLID_NOT;
	boss->svflags |= SVF_NOCLIENT;

	// Clean up the boss's health bar from the HUD
	ClearBossHealthBar(boss);

	// Clean up entity tracking
	OnEntityDeath(boss);
	OnEntityRemoved(boss);
}

// ============================================================================================
// Hell Maidens — "Reverse Horde" alternative boss encounter
// ============================================================================================
// Instead of a single big boss, spawn a 3-monster mini-raid of heavily buffed chicks. Each maiden
// carries a different (non-friendly) BF_ modifier for visual/buff variety. They share ONE aggregate
// health bar (driven by an invisible aggregator entity whose health is the live sum of the maidens),
// and whenever a maiden dies the survivors are fully healed — so the group must be burned down fast.

namespace {
	constexpr int32_t  HELL_MAIDEN_COUNT       = 3;
	constexpr int32_t  HELL_MAIDEN_HEALTH      = 2500;
	constexpr int32_t  HELL_MAIDEN_SHIELD      = 1750;
	constexpr float    HELL_MAIDEN_SCALE       = 1.35f;   // "bit bigger"; tunable
	constexpr const char *HELL_MAIDEN_NAME     = "Hell Praetors";

	// Three visually distinct, non-friendly modifiers (red fiery shell / green / dark tracker).
	constexpr std::array<bonus_flags_t, HELL_MAIDEN_COUNT> HELL_MAIDEN_FLAGS = {
		BF_CHAMPION, BF_POSSESSED, BF_RAGEQUITTER
	};

	// Spread the three maidens around the chosen anchor point.
	constexpr std::array<vec3_t, HELL_MAIDEN_COUNT> HELL_MAIDEN_OFFSETS = {{
		{  96.0f,   0.0f, 0.0f},
		{ -64.0f,  96.0f, 0.0f},
		{ -64.0f, -96.0f, 0.0f}
	}};
}

struct HellMaidenGroup {
	std::array<edict_t *, HELL_MAIDEN_COUNT> members{};
	edict_t *aggregator = nullptr;   // invisible entity that owns the shared health bar
	int32_t  spawned = 0;            // how many maidens actually spawned (group size)
	int32_t  alive = 0;
	bool     active = false;
};
static HellMaidenGroup g_hellMaidens;

bool IsHellMaiden(const edict_t *ent) noexcept
{
	if (!g_hellMaidens.active || !ent)
		return false;
	for (const edict_t *m : g_hellMaidens.members)
		if (m == ent)
			return true;
	return false;
}

// Free the shared bar (the misc_health_bar entity) and the aggregator, and clear the HUD name.
static void ClearHellMaidensBar()
{
	if (g_hellMaidens.aggregator)
	{
		if (g_hellMaidens.aggregator->inuse)
		{
			ClearBossHealthBar(g_hellMaidens.aggregator); // frees the misc_health_bar slot
			G_FreeEdict(g_hellMaidens.aggregator);
		}
		g_hellMaidens.aggregator = nullptr;
	}

	// The aggregator's display name doesn't round-trip through GetDisplayName, so clear it directly.
	if (strcmp(gi.get_configstring(CONFIG_HEALTH_BAR_NAME), HELL_MAIDEN_NAME) == 0)
		gi.configstring(CONFIG_HEALTH_BAR_NAME, "");
}

static void HealHellMaidenSurvivors(const edict_t *dead_maiden)
{
	int32_t alive = 0;
	for (edict_t *&m : g_hellMaidens.members)
	{
		// Drop the dead maiden from the group so a later corpse free/reuse can't dangle.
		if (m == dead_maiden)
		{
			m = nullptr;
			continue;
		}

		if (!m || !m->inuse || m->deadflag || m->health <= 0)
			continue;

		// Full restore: health and power shield.
		m->health = m->max_health;
		m->monsterinfo.power_armor_power = m->monsterinfo.max_power_armor_power;
		++alive;

		// Visible "renewed" sparkle at each survivor.
		SpawnGrow_Spawn(m->s.origin, 60.0f, 6.0f);
	}

	g_hellMaidens.alive = alive;

	if (alive > 0)
		AppendHordeMessage("A Hell Maiden falls - her sisters are restored!", 3_sec);
}

// Per-frame think on the invisible aggregator: drive the shared bar as a dynamic aggregate of the
// living maidens' health, and clean everything up once all maidens are dead.
void HellMaidensAggregatorThink(edict_t *self);
THINK(HellMaidensAggregatorThink)(edict_t *self) -> void
{
	if (!g_hellMaidens.active || g_hellMaidens.aggregator != self)
		return;

	int32_t alive = 0;
	int32_t total_health = 0;
	for (edict_t *m : g_hellMaidens.members)
	{
		if (m && m->inuse && !m->deadflag && m->health > 0)
		{
			++alive;
			total_health += m->health;
		}
	}
	g_hellMaidens.alive = alive;

	if (alive == 0)
	{
		ClearHellMaidensBar();        // frees the bar AND this aggregator entity (self)
		g_hellMaidens = HellMaidenGroup{};
		return;                       // self freed; do not reschedule
	}

	self->health = total_health;      // HUD shows total_health / max_health (= group size * per-maiden)
	self->nextthink = level.time + FRAME_TIME_S;
}

// Deferred spawn: runs BOSS_SPAWN_DELAY after SpawnHellMaidens so the maidens pop in with the same
// short delay + grow effect as a normal boss (see BossSpawnThink). The controller stores the anchor
// in its origin, then frees itself once the maidens and shared bar are set up.
void HellMaidensSpawnThink(edict_t *self);
THINK(HellMaidensSpawnThink)(edict_t *self) -> void
{
	const vec3_t anchor = self->s.origin;

	// Use the (scaled) chick bounds for per-slot validation.
	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(horde::MonsterTypeID::CHICKKL, predicted_mins, predicted_maxs);
	predicted_mins *= HELL_MAIDEN_SCALE;
	predicted_maxs *= HELL_MAIDEN_SCALE;

	constexpr bool is_flying = false;

	// Announce now — like BossSpawnThink, the announcement lands when the boss actually appears.
	current_wave_type = MonsterWaveType::Boss;
	StoreWaveType(current_wave_type);
	gi.LocBroadcast_Print(PRINT_CHAT, "{}", "The Hell Maidens descend - slay them as one!\n");
	AppendHordeMessage("\nBOSS: The Hell Maidens descend!", BOSS_ANNOUNCEMENT_DURATION);

	int32_t spawned = 0;
	for (int32_t i = 0; i < HELL_MAIDEN_COUNT; ++i)
	{
		// Pick this maiden's spot; fall back to the anchor if the offset doesn't validate.
		vec3_t slot = anchor + HELL_MAIDEN_OFFSETS[i];
		const auto slot_validation = IsPositionPhysicallyValidForBoss(slot, predicted_mins, predicted_maxs, is_flying);
		if (slot_validation.is_valid)
			slot = slot_validation.adjusted_position;
		else
			slot = anchor;

		edict_t *maiden = G_Spawn();
		if (!maiden)
			continue;

		maiden->classname = "monster_chickkl";
		maiden->s.origin = slot;
		maiden->s.angles = vec3_origin;

		// Set IS_BOSS BEFORE spawning so SP_monster_chick's internal ApplyMonsterBonusFlags is skipped
		// (same trick BossSpawnThink uses); we apply our own distinct modifier afterward.
		maiden->monsterinfo.IS_BOSS = true;
		maiden->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
		maiden->monsterinfo.last_reacttodamage_target_time = 0_ms;
		maiden->solid = SOLID_NOT;

		ED_CallSpawnMonsterByID(maiden, horde::MonsterTypeID::CHICKKL);
		if (!maiden->inuse)
		{
			if (developer->integer)
				gi.Com_PrintFmt("SpawnHellMaidens: ED_CallSpawn failed for maiden {}.\n", i);
			continue;
		}

		// Bot-targetable enemy monster (mirror BossSpawnThink).
		maiden->svflags |= SVF_MONSTER;
		maiden->monsterinfo.team = CTF_TEAM2;
		maiden->sv.init = false;

		// Distinct visible/buff modifier (sets bonus_flags + effects, no stat scaling).
		ApplyBonusFlagVisuals(maiden, HELL_MAIDEN_FLAGS[i]);

		// Fixed boss-grade stats.
		maiden->health = maiden->max_health = HELL_MAIDEN_HEALTH;
		maiden->monsterinfo.base_health = HELL_MAIDEN_HEALTH;
		maiden->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
		maiden->monsterinfo.power_armor_power = HELL_MAIDEN_SHIELD;
		maiden->monsterinfo.max_power_armor_power = HELL_MAIDEN_SHIELD;

		// "Bit bigger" — scale model + collision box (same approach as ApplyBossEffects).
		const float original_mins_z = maiden->mins[2];
		maiden->s.scale *= HELL_MAIDEN_SCALE;
		maiden->mins *= HELL_MAIDEN_SCALE;
		maiden->maxs *= HELL_MAIDEN_SCALE;
		maiden->mass = static_cast<int>(maiden->mass * HELL_MAIDEN_SCALE);
		maiden->s.origin[2] -= (original_mins_z * HELL_MAIDEN_SCALE - original_mins_z);

		// Loot share: the group splits one boss's worth of loot (see BossDeathHandler). The index is
		// this member's slot; the count is finalized after the loop once we know how many spawned.
		maiden->monsterinfo.boss_loot_share_index = static_cast<uint8_t>(spawned);

		// Extra bonus item: only the first maiden carries one, so the group drops a single horde item
		// (BossDeathHandler also drops on death). Without this gate all three would each drop one.
		if (spawned == 0)
		{
			maiden->item = G_HordePickItem();
			maiden->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP;
		}

		maiden->solid = SOLID_BBOX;
		gi.linkentity(maiden);

		// Spawn-in FX.
		const float base_size = std::max(SPAWN_GROW_MIN_BASE_SIZE, slot.length() * SPAWN_GROW_BASE_SIZE_MULTIPLIER);
		ImprovedSpawnGrow(slot, base_size, base_size * SPAWN_GROW_END_SIZE_MULTIPLIER, maiden);
		SpawnGrow_Spawn(slot, base_size, base_size * SPAWN_GROW_END_SIZE_MULTIPLIER);

		auto_spawned_bosses.insert(maiden);
		g_hellMaidens.members[spawned] = maiden;
		++spawned;
	}

	if (spawned == 0)
	{
		if (developer->integer)
			gi.Com_PrintFmt("HellMaidensSpawnThink: failed to spawn any maidens on wave {}.\n", current_wave_level);
		boss_spawned_for_wave = false;   // allow a normal boss to spawn next tick instead
		G_FreeEdict(self);
		return;
	}

	// Finalize the loot-share group size now that we know how many maidens actually spawned.
	for (int32_t i = 0; i < spawned; ++i)
		if (g_hellMaidens.members[i])
			g_hellMaidens.members[i]->monsterinfo.boss_loot_share_count = static_cast<uint8_t>(spawned);

	if (sound_spawn1)
		gi.sound(g_hellMaidens.members[0], CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

	// Create the invisible aggregator that owns the shared "Hell Maidens" health bar.
	edict_t *agg = G_Spawn();
	agg->classname = "hell_maidens_bar";
	agg->s.origin = anchor + vec3_t{0.0f, 0.0f, 40.0f};
	agg->movetype = MOVETYPE_NONE;
	agg->solid = SOLID_NOT;
	agg->takedamage = false;
	agg->svflags |= SVF_NOCLIENT;
	agg->max_health = spawned * HELL_MAIDEN_HEALTH;
	agg->health = agg->max_health;
	agg->think = HellMaidensAggregatorThink;
	agg->nextthink = level.time + FRAME_TIME_S;
	gi.linkentity(agg);

	g_hellMaidens.aggregator = agg;
	g_hellMaidens.spawned = spawned;
	g_hellMaidens.alive = spawned;
	g_hellMaidens.active = true;

	AttachHealthBar(agg);
	gi.configstring(CONFIG_HEALTH_BAR_NAME, HELL_MAIDEN_NAME);

	if (developer->integer)
		gi.Com_PrintFmt("HellMaidensSpawnThink: spawned {} maidens on wave {} at {}.\n", spawned, current_wave_level, anchor);

	G_FreeEdict(self);   // controller no longer needed
}

void SpawnHellMaidens()
{
	// Tear down any stale group first (defensive — only one encounter is ever active at a time),
	// then start from a fully cleared group so no stale member pointers can linger.
	if (g_hellMaidens.active)
		ClearHellMaidensBar();
	g_hellMaidens = HellMaidenGroup{};

	// Chicks are ground monsters; use chick bounds scaled up for placement validation.
	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(horde::MonsterTypeID::CHICKKL, predicted_mins, predicted_maxs);
	predicted_mins *= HELL_MAIDEN_SCALE;
	predicted_maxs *= HELL_MAIDEN_SCALE;

	const char *map_name = GetCurrentMapName();
	if (!map_name)
		map_name = "";

	constexpr bool is_flying = false;

	// Find an anchor: prefer the map's registered boss origin, else the validated spawn grid.
	vec3_t anchor;
	bool has_anchor = false;

	vec3_t spawn_origin;
	const horde::MapID mapId = horde::MapOriginRegistry::GetMapID(map_name);
	if (horde::MapOriginRegistry::GetOrigin(mapId, spawn_origin))
	{
		ClearSpawnArea(spawn_origin, predicted_mins, predicted_maxs);
		const auto validation = IsPositionPhysicallyValidForBoss(spawn_origin, predicted_mins, predicted_maxs, is_flying);
		if (validation.is_valid)
		{
			anchor = validation.adjusted_position;
			has_anchor = true;
		}
	}

	if (!has_anchor)
		has_anchor = TryBossGridFallbackPosition(predicted_mins, predicted_maxs, is_flying, true, "hell maidens", anchor);

	if (!has_anchor)
	{
		if (developer->integer)
			gi.Com_PrintFmt("SpawnHellMaidens: No valid anchor position for wave {}. Will retry next frame.\n", current_wave_level);
		boss_spawned_for_wave = false;
		return;
	}

	PushEntitiesAway(anchor, PUSH_ITERATIONS, PUSH_BASE_RADIUS, PUSH_BASE_STRENGTH, PUSH_PLAYER_STRENGTH, PUSH_MONSTER_STRENGTH);

	// Defer the actual spawn so the maidens appear after the same short delay + grow effect as a
	// normal boss (BossSpawnThink runs BOSS_SPAWN_DELAY after the boss entity is created). The
	// controller carries the anchor in its origin and spawns the trio when it fires.
	edict_t *controller = G_Spawn();
	controller->classname = "hell_maidens_spawner";
	controller->s.origin = anchor;
	controller->svflags |= SVF_NOCLIENT;
	controller->solid = SOLID_NOT;
	controller->movetype = MOVETYPE_NONE;
	controller->think = HellMaidensSpawnThink;
	controller->nextthink = level.time + BOSS_SPAWN_DELAY;
	gi.linkentity(controller);

	// Commit the encounter now and register it in the rotation so recent_bosses spaces it like any
	// other boss (mirrors SpawnFixerTrio).
	boss_spawned_for_wave = true;
	recent_bosses.add(horde::MonsterTypeID::CHICKKL);

	if (developer->integer)
		gi.Com_PrintFmt("SpawnHellMaidens: anchor reserved for wave {} at {}; maidens arrive after spawn delay.\n", current_wave_level, anchor);
}

// ============================================================================================
// Fixer Trio — the Fixer boss as a 3-fixbot mini-raid (same shared-bar + mutual-heal mechanic as
// the Hell Maidens). It additionally keeps every turret the group spawns alive until the LAST
// fixer dies: a dying member hands its turrets to a surviving sister (see fixbot_die), so the final
// fixer owns the whole group's turrets and they all clear at once when it falls.
// ============================================================================================
namespace {
	constexpr int32_t FIXER_COUNT  = 3;
	constexpr int32_t FIXER_HEALTH = 2500;   // same fixed value as the Hell Praetors (HELL_MAIDEN_HEALTH)
	constexpr int32_t FIXER_SHIELD = 1750;   // same fixed value as the Hell Praetors (HELL_MAIDEN_SHIELD)
	constexpr const char *FIXER_NAME = "The Fixers";

	// Distinct visible/buff modifier per fixer (cosmetic only, no stat scaling) — mirrors the maidens.
	constexpr std::array<bonus_flags_t, FIXER_COUNT> FIXER_FLAGS = {
		BF_CHAMPION, BF_POSSESSED, BF_RAGEQUITTER
	};

	// Spread the three fixbots around the anchor; they fly, so lift the flankers a little.
	constexpr std::array<vec3_t, FIXER_COUNT> FIXER_OFFSETS = {{
		{ 112.0f,    0.0f, 32.0f},
		{ -72.0f,  112.0f, 64.0f},
		{ -72.0f, -112.0f, 64.0f}
	}};
}

struct FixerGroup {
	std::array<edict_t *, FIXER_COUNT> members{};
	edict_t *aggregator = nullptr;   // invisible entity that owns the shared health bar
	int32_t  spawned = 0;
	int32_t  alive = 0;
	bool     active = false;
};
static FixerGroup g_fixers;

bool IsFixer(const edict_t *ent) noexcept
{
	if (!g_fixers.active || !ent)
		return false;
	for (const edict_t *m : g_fixers.members)
		if (m == ent)
			return true;
	return false;
}

// A surviving trio member to inherit a dying member's turrets (nullptr if none survive). Exposed via
// g_local.h so fixbot_die can funnel turrets to the last fixer instead of destroying them early.
edict_t *FirstAliveFixer() noexcept
{
	if (!g_fixers.active)
		return nullptr;
	for (edict_t *m : g_fixers.members)
		if (m && m->inuse && !m->deadflag && m->health > 0)
			return m;
	return nullptr;
}

// Free the shared bar (the misc_health_bar entity) and the aggregator, and clear the HUD name.
static void ClearFixersBar()
{
	if (g_fixers.aggregator)
	{
		if (g_fixers.aggregator->inuse)
		{
			ClearBossHealthBar(g_fixers.aggregator);
			G_FreeEdict(g_fixers.aggregator);
		}
		g_fixers.aggregator = nullptr;
	}

	if (strcmp(gi.get_configstring(CONFIG_HEALTH_BAR_NAME), FIXER_NAME) == 0)
		gi.configstring(CONFIG_HEALTH_BAR_NAME, "");
}

static void HealFixerSurvivors(const edict_t *dead_fixer)
{
	int32_t alive = 0;
	for (edict_t *&m : g_fixers.members)
	{
		// Drop the dead fixer from the group so a later corpse free/reuse can't dangle.
		if (m == dead_fixer)
		{
			m = nullptr;
			continue;
		}

		if (!m || !m->inuse || m->deadflag || m->health <= 0)
			continue;

		// Full restore: health and power shield — the group must be burned down quickly.
		m->health = m->max_health;
		m->monsterinfo.power_armor_power = m->monsterinfo.max_power_armor_power;
		++alive;

		SpawnGrow_Spawn(m->s.origin, 60.0f, 6.0f);
	}

	g_fixers.alive = alive;

	if (alive > 0)
		AppendHordeMessage("A Fixer is scrapped - the rest reassemble!", 3_sec);
}

// Per-frame think on the invisible aggregator: drive the shared bar from the living fixers' health,
// and clean everything up once all fixers are dead.
void FixersAggregatorThink(edict_t *self);
THINK(FixersAggregatorThink)(edict_t *self) -> void
{
	if (!g_fixers.active || g_fixers.aggregator != self)
		return;

	int32_t alive = 0;
	int32_t total_health = 0;
	for (edict_t *m : g_fixers.members)
	{
		if (m && m->inuse && !m->deadflag && m->health > 0)
		{
			++alive;
			total_health += m->health;
		}
	}
	g_fixers.alive = alive;

	if (alive == 0)
	{
		ClearFixersBar();
		g_fixers = FixerGroup{};
		return;
	}

	self->health = total_health;
	self->nextthink = level.time + FRAME_TIME_S;
}

// Deferred spawn: runs BOSS_SPAWN_DELAY after SpawnFixerTrio so the fixers pop in with the same
// delay + grow effect as a normal boss. The controller stores the anchor + chosen size category,
// then frees itself once the fixers and shared bar are set up.
void FixersSpawnThink(edict_t *self);
THINK(FixersSpawnThink)(edict_t *self) -> void
{
	const vec3_t anchor = self->s.origin;
	const BossSizeCategory sizeCategory = static_cast<BossSizeCategory>(self->count);

	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(horde::MonsterTypeID::FIXBOT_KL, predicted_mins, predicted_maxs);

	const bool is_flying = IsFlying(horde::MonsterTypeID::FIXBOT_KL);

	current_wave_type = GetBossWaveType(horde::MonsterTypeID::FIXBOT_KL).first;
	StoreWaveType(current_wave_type);
	gi.LocBroadcast_Print(PRINT_CHAT, "{}", "The Fixers deploy - dismantle them before they rebuild!\n");
	AppendHordeMessage("\nBOSS: The Fixers deploy!", BOSS_ANNOUNCEMENT_DURATION);

	const char *fixer_classname = horde::MonsterTypeRegistry::GetClassname(horde::MonsterTypeID::FIXBOT_KL);

	int32_t spawned = 0;
	for (int32_t i = 0; i < FIXER_COUNT; ++i)
	{
		vec3_t slot = anchor + FIXER_OFFSETS[i];
		const auto slot_validation = IsPositionPhysicallyValidForBoss(slot, predicted_mins, predicted_maxs, is_flying);
		if (slot_validation.is_valid)
			slot = slot_validation.adjusted_position;
		else
			slot = anchor;

		edict_t *fixer = G_Spawn();
		if (!fixer)
			continue;

		fixer->classname = fixer_classname;
		fixer->s.origin = slot;
		fixer->s.angles = vec3_origin;
		fixer->bossSizeCategory = sizeCategory;

		// Set IS_BOSS before spawning so the monster's own bonus-flag pass is skipped (mirrors
		// BossSpawnThink / Hell Maidens); we apply our own fixed stats + visuals afterward.
		fixer->monsterinfo.IS_BOSS = true;
		fixer->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
		fixer->monsterinfo.last_reacttodamage_target_time = 0_ms;
		fixer->solid = SOLID_NOT;

		ED_CallSpawnMonsterByID(fixer, horde::MonsterTypeID::FIXBOT_KL);
		if (!fixer->inuse)
		{
			if (developer->integer)
				gi.Com_PrintFmt("SpawnFixerTrio: ED_CallSpawn failed for fixer {}.\n", i);
			continue;
		}

		// Bot-targetable enemy monster (mirror BossSpawnThink / Hell Maidens).
		fixer->svflags |= SVF_MONSTER;
		fixer->monsterinfo.team = CTF_TEAM2;
		fixer->sv.init = false;

		// Distinct visible/buff modifier (sets bonus_flags + effects, no stat scaling) — like maidens.
		ApplyBonusFlagVisuals(fixer, FIXER_FLAGS[i]);

		// Same fixed boss-grade stats as the Hell Praetors: fixed health + power shield, no wave scaling
		// and no separate combat armor (the power shield is the only protection, exactly like a maiden).
		fixer->health = fixer->max_health = FIXER_HEALTH;
		fixer->monsterinfo.base_health = FIXER_HEALTH;
		fixer->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
		fixer->monsterinfo.power_armor_power = FIXER_SHIELD;
		fixer->monsterinfo.max_power_armor_power = FIXER_SHIELD;
		fixer->monsterinfo.armor_power = 0;

		// Loot share: the group splits one boss's worth of loot (see BossDeathHandler).
		fixer->monsterinfo.boss_loot_share_index = static_cast<uint8_t>(spawned);

		// Only the first fixer carries a bonus item so the group drops a single horde item.
		if (spawned == 0)
		{
			fixer->item = G_HordePickItem();
			fixer->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP;
		}

		fixer->solid = SOLID_BBOX;
		gi.linkentity(fixer);

		const float base_size = std::max(SPAWN_GROW_MIN_BASE_SIZE, slot.length() * SPAWN_GROW_BASE_SIZE_MULTIPLIER);
		ImprovedSpawnGrow(slot, base_size, base_size * SPAWN_GROW_END_SIZE_MULTIPLIER, fixer);
		SpawnGrow_Spawn(slot, base_size, base_size * SPAWN_GROW_END_SIZE_MULTIPLIER);

		auto_spawned_bosses.insert(fixer);
		g_fixers.members[spawned] = fixer;
		++spawned;
	}

	if (spawned == 0)
	{
		if (developer->integer)
			gi.Com_PrintFmt("FixersSpawnThink: failed to spawn any fixers on wave {}.\n", current_wave_level);
		boss_spawned_for_wave = false;   // allow a normal boss to spawn next tick instead
		G_FreeEdict(self);
		return;
	}

	// Finalize the loot-share group size now that we know how many fixers actually spawned.
	for (int32_t i = 0; i < spawned; ++i)
		if (g_fixers.members[i])
			g_fixers.members[i]->monsterinfo.boss_loot_share_count = static_cast<uint8_t>(spawned);

	if (sound_spawn1)
		gi.sound(g_fixers.members[0], CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

	// Create the invisible aggregator that owns the shared "The Fixers" health bar.
	edict_t *agg = G_Spawn();
	agg->classname = "fixer_trio_bar";
	agg->s.origin = anchor + vec3_t{0.0f, 0.0f, 40.0f};
	agg->movetype = MOVETYPE_NONE;
	agg->solid = SOLID_NOT;
	agg->takedamage = false;
	agg->svflags |= SVF_NOCLIENT;

	int32_t group_max_health = 0;
	for (int32_t i = 0; i < spawned; ++i)
		if (g_fixers.members[i])
			group_max_health += g_fixers.members[i]->max_health;

	agg->max_health = group_max_health;
	agg->health = agg->max_health;
	agg->think = FixersAggregatorThink;
	agg->nextthink = level.time + FRAME_TIME_S;
	gi.linkentity(agg);

	g_fixers.aggregator = agg;
	g_fixers.spawned = spawned;
	g_fixers.alive = spawned;
	g_fixers.active = true;

	AttachHealthBar(agg);
	gi.configstring(CONFIG_HEALTH_BAR_NAME, FIXER_NAME);

	if (developer->integer)
		gi.Com_PrintFmt("FixersSpawnThink: spawned {} fixers on wave {} at {}.\n", spawned, current_wave_level, anchor);

	G_FreeEdict(self);
}

void SpawnFixerTrio(BossSizeCategory sizeCategory)
{
	// Tear down any stale group first (only one encounter is ever active at a time).
	if (g_fixers.active)
		ClearFixersBar();
	g_fixers = FixerGroup{};

	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(horde::MonsterTypeID::FIXBOT_KL, predicted_mins, predicted_maxs);

	const char *map_name = GetCurrentMapName();
	if (!map_name)
		map_name = "";

	const bool is_flying = IsFlying(horde::MonsterTypeID::FIXBOT_KL);

	// Find an anchor: prefer the map's registered boss origin, else the validated spawn grid.
	vec3_t anchor;
	bool has_anchor = false;

	vec3_t spawn_origin;
	const horde::MapID mapId = horde::MapOriginRegistry::GetMapID(map_name);
	if (horde::MapOriginRegistry::GetOrigin(mapId, spawn_origin))
	{
		ClearSpawnArea(spawn_origin, predicted_mins, predicted_maxs);
		const auto validation = IsPositionPhysicallyValidForBoss(spawn_origin, predicted_mins, predicted_maxs, is_flying);
		if (validation.is_valid)
		{
			anchor = validation.adjusted_position;
			has_anchor = true;
		}
	}

	if (!has_anchor)
		has_anchor = TryBossGridFallbackPosition(predicted_mins, predicted_maxs, is_flying, true, "fixer trio", anchor);

	if (!has_anchor)
	{
		if (developer->integer)
			gi.Com_PrintFmt("SpawnFixerTrio: No valid anchor position for wave {}. Will retry next frame.\n", current_wave_level);
		boss_spawned_for_wave = false;
		return;
	}

	PushEntitiesAway(anchor, PUSH_ITERATIONS, PUSH_BASE_RADIUS, PUSH_BASE_STRENGTH, PUSH_PLAYER_STRENGTH, PUSH_MONSTER_STRENGTH);

	// Defer the actual spawn (same delay + grow as a normal boss). The controller carries the anchor
	// in its origin and the chosen size category in its count field.
	edict_t *controller = G_Spawn();
	controller->classname = "fixer_trio_spawner";
	controller->s.origin = anchor;
	controller->count = static_cast<int32_t>(sizeCategory);
	controller->svflags |= SVF_NOCLIENT;
	controller->solid = SOLID_NOT;
	controller->movetype = MOVETYPE_NONE;
	controller->think = FixersSpawnThink;
	controller->nextthink = level.time + BOSS_SPAWN_DELAY;
	gi.linkentity(controller);

	// Commit the encounter now and register it in the rotation so order/cooldown are respected.
	boss_spawned_for_wave = true;
	recent_bosses.add(horde::MonsterTypeID::FIXBOT_KL);

	// Commit the Fixer's flying wave type now (mirrors the normal-boss path in
	// SpawnBossAutomatically) so minions spawning during the deferred-spawn window are
	// flying-filtered. FixersSpawnThink re-affirms it + StoreWaveType + announces on arrival.
	current_wave_type = GetBossWaveType(horde::MonsterTypeID::FIXBOT_KL).first;

	if (developer->integer)
		gi.Com_PrintFmt("SpawnFixerTrio: anchor reserved for wave {} at {}; fixers arrive after spawn delay.\n", current_wave_level, anchor);
}

void SpawnBossAutomatically()
{
	// Immediate guard against re-entry
	if (boss_spawned_for_wave) {
		return;
	}
	boss_spawned_for_wave = true;

	// Cleanup existing bosses
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end();)
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

	// Basic wave check (cadence lives in IsBossWaveLevel, g_horde.h)
	if (!IsBossWaveLevel(current_wave_level)) {
		boss_spawned_for_wave = false;
		return;
	}

	// Select boss type
	const char *map_name = GetCurrentMapName();
	if (!map_name)
		map_name = "";
	// Bosses use the boss-pool map size, which honors a per-map boss_size override so a
	// map can keep its gameplay size (monster caps/spawning) while drawing bosses from a
	// different pool. When overridden, hard-restrict the pool to that size's list.
	bool boss_size_is_override = false;
	const horde::MapSize boss_map_size = GetBossMapSize(map_name, boss_size_is_override);
	BossPickResult boss_pick_result = G_HordePickBOSSType(boss_map_size, map_name, current_wave_level, boss_size_is_override);

	if (boss_pick_result.typeId == horde::MonsterTypeID::UNKNOWN) {
		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: Failed to pick a boss type for wave {}.\n", current_wave_level);
		boss_spawned_for_wave = false;
		return;
	}

	// Two boss picks are actually 3-monster mini-raids spawned in place of the single boss. Because
	// they're picked by the normal rotation, they inherit the recent-boss order/spacing. Each finds
	// its own anchor and commits the wave (or resets boss_spawned_for_wave to retry).
	if (boss_pick_result.typeId == horde::MonsterTypeID::FIXBOT_KL) {
		SpawnFixerTrio(boss_pick_result.sizeCategory);
		return;
	}
	if (boss_pick_result.typeId == horde::MonsterTypeID::CHICKKL) {
		SpawnHellMaidens();
		return;
	}

	// Prepare spawn area
	vec3_t predicted_mins, predicted_maxs;
	GetBossPlacementBounds(boss_pick_result.typeId, boss_pick_result.sizeCategory, predicted_mins, predicted_maxs);

	// HYBRID APPROACH: Try relaxed validation first, then alternative spawns if needed
	const bool is_flying = IsFlying(boss_pick_result.typeId);
	vec3_t final_spawn_origin;
	bool has_spawn_position = false;

	// Determine spawn location. Prefer registered map coordinates, but fall back
	// to the generated spawn grid when a map has no boss coordinate.
	vec3_t spawn_origin;
	const horde::MapID mapId = horde::MapOriginRegistry::GetMapID(map_name);
	const bool has_registered_spawn_origin = horde::MapOriginRegistry::GetOrigin(mapId, spawn_origin);

	// Step 1: Clear and validate primary spawn with relaxed boss checks
	if (has_registered_spawn_origin)
	{
		ClearSpawnArea(spawn_origin, predicted_mins, predicted_maxs);
		const auto primary_validation = IsPositionPhysicallyValidForBoss(spawn_origin, predicted_mins, predicted_maxs, is_flying);

		if (primary_validation.is_valid)
		{
			final_spawn_origin = primary_validation.adjusted_position;
			PushEntitiesAway(final_spawn_origin, PUSH_ITERATIONS, PUSH_BASE_RADIUS, PUSH_BASE_STRENGTH, PUSH_PLAYER_STRENGTH, PUSH_MONSTER_STRENGTH);
			has_spawn_position = true;

			if (developer->integer > 1)
				gi.Com_PrintFmt("SpawnBossAutomatically: Primary spawn validated at {:.0f}\n", final_spawn_origin);
		}
		// Step 2: If primary fails, try alternative spawn points
		else if (TryAlternativeSpawnPoints(spawn_origin, predicted_mins, predicted_maxs, is_flying,
			PUSH_ITERATIONS, PUSH_BASE_RADIUS, PUSH_BASE_STRENGTH, PUSH_PLAYER_STRENGTH, PUSH_MONSTER_STRENGTH, final_spawn_origin))
		{
			has_spawn_position = true;

			if (developer->integer > 1)
				gi.Com_PrintFmt("SpawnBossAutomatically: Using alternative spawn at {:.0f}\n", final_spawn_origin);
		}
		else if (developer->integer > 1)
		{
			gi.Com_PrintFmt("SpawnBossAutomatically: Registered boss origin failed validation at {:.0f}. Trying grid fallback.\n", spawn_origin);
		}
	}
	else if (developer->integer > 1)
	{
		gi.Com_PrintFmt("SpawnBossAutomatically: No designated boss spawn origin found for map '{}'. Trying grid fallback.\n", map_name);
	}

	// Step 3: If coordinates are missing or unusable, use the validated spawn grid.
	if (!has_spawn_position)
	{
		has_spawn_position = TryBossGridFallbackPosition(
			predicted_mins,
			predicted_maxs,
			is_flying,
			true,
			"boss spawn",
			final_spawn_origin);
	}

	// Step 4: All attempts failed - abort and retry next frame
	if (!has_spawn_position)
	{
		if (developer->integer)
		{
			if (has_registered_spawn_origin)
				gi.Com_PrintFmt("SpawnBossAutomatically: All coordinate and grid spawn attempts failed at {:.0f}. Will retry next frame.\n", spawn_origin);
			else
				gi.Com_PrintFmt("SpawnBossAutomatically: No boss coordinate or valid grid fallback for map '{}'. Will retry next frame.\n", map_name);
		}
		boss_spawned_for_wave = false;
		return;
	}

	// Create boss entity
	edict_t *boss = G_Spawn();
	if (!boss)
	{
		boss_spawned_for_wave = false;
		return;
	}

	boss->classname = horde::MonsterTypeRegistry::GetClassname(boss_pick_result.typeId);
	boss->s.origin = final_spawn_origin;
	boss->s.angles = vec3_origin;
	boss->bossSizeCategory = boss_pick_result.sizeCategory;

	// Commit the boss's wave type NOW, not in the deferred BossSpawnThink (which runs
	// BOSS_SPAWN_DELAY later). SpawnBossAutomatically fires on the boss wave's first spawn
	// tick, ahead of any PlanNextSpawnBatch, so setting it here guarantees the flying/themed
	// minion filter (BossMinionExcludesMonster) is active for EVERY minion in the wave.
	// Otherwise the first ~750ms of minions would spawn while current_wave_type is still None.
	// StoreWaveType + the announcement stay in BossSpawnThink so they land with the boss visual.
	current_wave_type = GetBossWaveType(boss_pick_result.typeId).first;
	// Bosses with an explicit minion roster (Widow/Widow2/Arachnid) are filtered by boss identity,
	// not wave type; record which boss is driving the wave (UNKNOWN/no-roster bosses are unaffected).
	current_boss_minion_source = boss_pick_result.typeId;

	// Set the think function to run after spawn delay
	boss->nextthink = level.time + BOSS_SPAWN_DELAY;
	boss->think = BossSpawnThink;
	gi.linkentity(boss);
}

// Boss spawn think function
THINK(BossSpawnThink)(edict_t *self)->void
{
	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);

	// Set wave type and print message
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

	recent_bosses.add(typeId);
	boss_spawned_for_wave = true;

	// Set SVF_MONSTER flag so bots can target this boss
	// flymonster_start/walkmonster_start normally sets this in monster_start_go,
	// but that happens in a delayed think. We need it set immediately for bot targeting.
	self->svflags |= SVF_MONSTER;

	// Set team so bots can target this boss
	// Bosses skip ApplyMonsterBonusFlags due to IS_BOSS flag, so team must be set explicitly
	self->monsterinfo.team = CTF_TEAM2;

	// Reset sv.init to force bot registration on next frame
	// The boss entity existed for multiple frames before SVF_MONSTER was set,
	// causing Edict_UpdateState to set sv.init=true prematurely, which would
	// prevent Monster_UpdateState from calling gi.Bot_RegisterEdict()
	self->sv.init = false; // this did the trick

	// Dynamic boss announcement
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
		AppendHordeMessage(announce_message.data(), BOSS_ANNOUNCEMENT_DURATION);
	}

	self->solid = SOLID_BBOX;
	gi.linkentity(self);

	ConfigureBossArmor(self);
	ApplyBossEffects(self);

	if (self->inuse && !self->deadflag && self->health > 0)
	{
		const vec3_t spawn_pos = self->s.origin;
		const float magnitude = spawn_pos.length();
		const float base_size = std::max(SPAWN_GROW_MIN_BASE_SIZE, magnitude * SPAWN_GROW_BASE_SIZE_MULTIPLIER);
		const float end_size = base_size * SPAWN_GROW_END_SIZE_MULTIPLIER;
		ImprovedSpawnGrow(spawn_pos, base_size, end_size, self);
		SpawnGrow_Spawn(spawn_pos, base_size, end_size);
		if (sound_spawn1)
		{
			gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
		}
	}
	else if (self->inuse && developer->integer > 1)
	{
		gi.Com_PrintFmt("SpawnGrow_Spawn/ImprovedSpawnGrow skipped in BossSpawnThink: Boss {} (idx {}) not fully alive. DeadFlag:{}, Health:%.0f\n",
						(self->classname ? self->classname : "Unknown"),
						static_cast<int>(self - g_edicts),
						self->deadflag,
						self->health);
	}

	AttachHealthBar(self);
	SetHealthBarName(self);
	auto_spawned_bosses.insert(self);

	// Unlock monsters that share the same model as this boss (free precache)
	UnlockModelFamilyMembers(typeId, current_wave_level);

	if (developer->integer > 1)
	{
		gi.Com_PrintFmt("BossSpawnThink: Finalized spawn for boss {} at {:.0f}.\n", self->classname, self->s.origin);
	}
}

// Boss teleportation function
bool CheckAndTeleportBoss(edict_t *self, BossTeleportReason reason)
{

	// Early validation
	if (!self || !self->inuse || self->deadflag || !self->monsterinfo.IS_BOSS || !g_horde->integer)
	{
		return false;
	}

	// Check cooldowns
	const gtime_t current_time = level.time;

	// General teleport cooldown
	if (self->teleport_time > 0_ms)
	{
		const gtime_t elapsed_since_last_teleport = current_time - self->teleport_time;
		if (elapsed_since_last_teleport < GENERAL_TELEPORT_COOLDOWN)
		{
			if (developer->integer > 1)
			{
				gi.Com_PrintFmt("CTB: Boss {} on general monster teleport cooldown. Remaining: {:.2f}s\n",
								self->classname ? self->classname : "UNKNOWN",
								(GENERAL_TELEPORT_COOLDOWN - elapsed_since_last_teleport).seconds());
			}
			return false;
		}
	}

	// Get teleport destination
	const char *current_map = GetCurrentMapName();
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(current_map);

	// Perform teleportation
	bool force_teleport = (reason == BossTeleportReason::TRIGGER_HURT || reason == BossTeleportReason::DROWNING || reason == BossTeleportReason::TELEFRAG);
	vec3_t destination_angles = self->s.angles;
	bool teleported = false;

	vec3_t destination_origin;
	if (mapId != horde::MapID::UNKNOWN && horde::MapOriginRegistry::GetOrigin(mapId, destination_origin))
	{
		if (developer->integer > 1)
		{
			gi.Com_PrintFmt("CTB: Attempting Horde_TeleportMonster for boss {} to ({},{},{}) (ForceVisible: {})\n",
							self->classname ? self->classname : "UNKNOWN",
							destination_origin[0], destination_origin[1], destination_origin[2],
							force_teleport);
		}

		teleported = Horde_TeleportMonster(self, destination_origin, destination_angles, true, force_teleport);

		if (!teleported && developer->integer > 1)
		{
			gi.Com_PrintFmt("CTB: Registered boss teleport origin failed for {}. Trying grid fallback.\n",
							self->classname ? self->classname : "UNKNOWN");
		}
	}
	else if (developer->integer > 1)
	{
		if (mapId == horde::MapID::UNKNOWN)
			gi.Com_PrintFmt("CTB: MapID unknown for {}, trying grid fallback.\n", current_map ? current_map : "");
		else
			gi.Com_PrintFmt("CTB: Failed to get teleport origin for map_id {}, trying grid fallback.\n", (int)mapId);
	}

	if (!teleported)
	{
		horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
		vec3_t predicted_mins, predicted_maxs;
		GetBossPlacementBounds(typeId, self->bossSizeCategory, predicted_mins, predicted_maxs);
		const bool is_flying = IsFlying(typeId);

		teleported = TryTeleportBossToGridFallback(
			self,
			predicted_mins,
			predicted_maxs,
			is_flying,
			destination_angles,
			force_teleport);
	}

	if (!teleported)
	{
		return false;
	}

	// Update cooldowns and announce teleportation
	const char* boss_display_name = GetDisplayName(self);

	switch(reason)
	{
	case BossTeleportReason::DROWNING:
		self->teleport_time = current_time;  // Use teleport_time instead
		gi.LocBroadcast_Print(PRINT_HIGH, "{} emerges from the depths!\n", boss_display_name);
		break;

	case BossTeleportReason::TRIGGER_HURT:
		self->teleport_time = current_time;  // Use teleport_time instead
		gi.LocBroadcast_Print(PRINT_HIGH, "{} escapes certain death!\n", boss_display_name);
		break;

	case BossTeleportReason::TELEFRAG:
		self->teleport_time = current_time;  // Use teleport_time instead
		gi.LocBroadcast_Print(PRINT_HIGH, "{} narrowly avoids being crushed!\n", boss_display_name);
		break;

	default:
		break;
	}

	// Visual effect for teleportation
	if (self->inuse && !self->deadflag && self->health > 0)
	{
		const vec3_t spawn_pos = self->s.origin;
		const float end_size = TELEPORT_EFFECT_SIZE * SPAWN_GROW_END_SIZE_MULTIPLIER;
		SpawnGrow_Spawn(spawn_pos, TELEPORT_EFFECT_SIZE, end_size);
	}

	if (developer->integer > 1)
	{
		const char *reason_str = reason == BossTeleportReason::DROWNING ? "drowning" :
								 reason == BossTeleportReason::TELEFRAG ? "telefrag" : "trigger_hurt";
		gi.Com_PrintFmt("CTB: Boss {} successfully teleported due to {} to ({},{},{}).\n",
						self->classname ? self->classname : "UNKNOWN",
						reason_str,
						self->s.origin[0], self->s.origin[1], self->s.origin[2]);
	}

	return true;
}

// Boss wave type initialization
void InitializeBossWaveTypes()
{
	static bool initialized = false;
	if (initialized)
		return;
	initialized = true;

	// Initialize boss eligibility cache
	g_bossEligibilityCache.initialize();

	// Default initialization
	for (auto& info : g_bossWaveTypeArray)
	{
		info.waveType = MonsterWaveType::None;
		info.announcement = "";
	}

	// Flying Bosses
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2)] =
		{MonsterWaveType::Flying | MonsterWaveType::Boss, "Hornet's deadly sting descends from above!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER)] =
		{MonsterWaveType::Flying | MonsterWaveType::Boss, "The Carrier unleashes its aerial swarm!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER_MINI)] =
		{MonsterWaveType::Flying | MonsterWaveType::Boss, "Carrier Mini is delivering pain right to your face!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2_KL)] =
		{MonsterWaveType::Flying | MonsterWaveType::Boss, "The Hornet buzzes with destructive intent!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::FIXBOT_KL)] =
		{MonsterWaveType::Flying | MonsterWaveType::Boss, "The Fixer arrives to repair your demise\n!"};

	// Heavy/Medium Armored Bosses
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::TANK_64)] =
		{MonsterWaveType::Ground | MonsterWaveType::Heavy, "Tank 64 brings double-barreled devastation!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::SHAMBLER_KL)] =
		{MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::Heavy, "The Shambler's electric fury is unleashed!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::GUNCMDR_KL)] =
		{MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Bomber, "The Gunner Commander leads the assault!\n"};

	// Close Combat / Ranged Heavy Bosses
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::WIDOW)] =
		{MonsterWaveType::Ground | MonsterWaveType::Spawner | MonsterWaveType::Heavy, "The Black Widow spins her web of death!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::WIDOW2)] =
		{MonsterWaveType::Ground | MonsterWaveType::Spawner | MonsterWaveType::Medium, "The Stalker emerges from the shadows!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS5)] =
		{MonsterWaveType::Ground | MonsterWaveType::Heavy, "The Janitor has arrived to clean house!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::JORG)] =
		{MonsterWaveType::Ground | MonsterWaveType::Heavy, "Jorg rains chaos from above!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::JORG_SMALL)] =
		{MonsterWaveType::Ground | MonsterWaveType::Medium, "Mini Jorg marches into battle!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::MAKRON)] =
		{MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, "Makron rises to power!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::MAKRON_KL)] =
		{MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, "Makron's wrath descends!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::PSX_GUARDIAN)] =
		{MonsterWaveType::Ground | MonsterWaveType::Heavy, "The Guardian awakens!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::PSX_ARACHNID)] =
		{MonsterWaveType::Ground | MonsterWaveType::Medium, "The Arachnid hunts its prey!\n"};
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::REDMUTANT)] =
		{MonsterWaveType::Mutant | MonsterWaveType::Shambler | MonsterWaveType::Ground, "The Red Mutant seeks vengeance!\n"};
}

std::pair<MonsterWaveType, const char *> GetBossWaveType(horde::MonsterTypeID typeId)
{
	if (!g_bossWaveTypeArray[0].announcement)
		InitializeBossWaveTypes();

	const auto& info = g_bossWaveTypeArray[static_cast<size_t>(typeId)];
	return {info.waveType, info.announcement};
}

// Boss utility functions
bool IsBossUnit(horde::MonsterTypeID typeId)
{
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Boss);
}

bool IsBossWave() noexcept
{
	// Cadence lives in IsBossWaveLevel (g_horde.h): classic every 5 from wave 10,
	// Horde 2 every 4 from wave 8.
	return IsBossWaveLevel(current_wave_level);
}

void ResetBosses()
{
	// Clear all boss entities
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end();)
	{
		edict_t *boss = *it;
		if (boss && boss->inuse)
		{
			// Ensure boss death logic is finalized if needed
			if (!boss->monsterinfo.BOSS_DEATH_HANDLED)
			{
				BossDeathHandler(boss);
			}
			OnEntityRemoved(boss);
			G_FreeEdict(boss);
		}
		it = auto_spawned_bosses.erase(it);
	}

	// Tear down any active Hell Maidens encounter (the aggregator isn't tracked in auto_spawned_bosses).
	if (g_hellMaidens.active || g_hellMaidens.aggregator)
	{
		ClearHellMaidensBar();
		g_hellMaidens = HellMaidenGroup{};
	}
}

void ResetRecentBosses()
{
	recent_bosses.clear();
}
