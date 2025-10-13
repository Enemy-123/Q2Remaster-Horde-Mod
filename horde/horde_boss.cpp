#include "horde_boss.h"
#include "g_horde.h"
#include "../g_local.h"
#include "../shared.h"
#include "horde_spawning.h"
#include <algorithm>
#include <boost/container/flat_set.hpp>
#include <cmath>

// Global boss variables
bool boss_spawned_for_wave = false;
RecentBosses recent_bosses;  // Not static - declared extern in header
static BossEligibilityCache g_bossEligibilityCache;
static std::array<BossWaveInfo, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_bossWaveTypeArray;

// External references
extern int16_t current_wave_level;  // Note: defined as int16_t in g_horde.cpp
extern boost::container::flat_set<edict_t *> auto_spawned_bosses;  // C++23

// External function to get current map size
extern horde::MapSize GetCurrentMapSize();
extern cvar_t *g_horde;
extern cvar_t *developer;
extern MonsterWaveType current_wave_type;
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

// Try alternative spawn points in a radial pattern around the primary spawn
// Returns true if a valid position was found, with final_pos set to the valid position
static bool TryAlternativeSpawnPoints(const vec3_t &primary_spawn, const vec3_t &predicted_mins, const vec3_t &predicted_maxs, bool is_flying,
	int push_iterations, float push_radius, float push_strength, float push_player_str, float push_monster_str, vec3_t &final_pos)
{
	// Radial offsets: try close positions first, then expand outward
	static constexpr std::array<vec3_t, 12> radial_offsets = {{
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

	for (const auto &offset : radial_offsets)
	{
		vec3_t test_pos = primary_spawn + offset;

		// Clear the alternative spawn area
		ClearSpawnArea(test_pos, predicted_mins, predicted_maxs);

		// Validate using relaxed boss checks
		const auto validation = IsPositionPhysicallyValidForBoss(test_pos, predicted_mins, predicted_maxs, is_flying);

		if (validation.is_valid)
		{
			final_pos = validation.adjusted_position;

			// Apply the same push entities logic
			PushEntitiesAway(final_pos, push_iterations, push_radius, push_strength, push_player_str, push_monster_str);

			if (developer->integer)
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

// Boss spawning constants
namespace {
	// Wave configuration
	constexpr int32_t MIN_BOSS_WAVE = 10;
	constexpr int32_t BOSS_WAVE_INTERVAL = 5;

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
}

// Boss data arrays
static constexpr std::array<boss_t, 11> BOSS_SMALL_SRC = {{
	{horde::MonsterTypeID::CARRIER_MINI, 24, -1, 0.1f, BossSizeCategory::Small},
	{horde::MonsterTypeID::BOSS2_KL, 24, -1, 0.1f, BossSizeCategory::Small},
	{horde::MonsterTypeID::FIXBOT_KL, 9, -1, 0.4f, BossSizeCategory::Small},
	{horde::MonsterTypeID::WIDOW2, 19, -1, 0.15f, BossSizeCategory::Small},
	{horde::MonsterTypeID::TANK_64, -1, -1, 0.25f, BossSizeCategory::Small},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Small},
	{horde::MonsterTypeID::GUNCMDR_KL, -1, 20, 0.3f, BossSizeCategory::Small},
	{horde::MonsterTypeID::MAKRON_KL, 36, -1, 0.2f, BossSizeCategory::Small},
	{horde::MonsterTypeID::MAKRON, 16, 26, 0.1f, BossSizeCategory::Small},
	{horde::MonsterTypeID::PSX_ARACHNID, 15, -1, 0.1f, BossSizeCategory::Small},
	{horde::MonsterTypeID::REDMUTANT, -1, 24, 0.1f, BossSizeCategory::Small}
}};

static constexpr std::array<boss_t, 11> BOSS_MEDIUM_SRC = {{
	{horde::MonsterTypeID::CARRIER, 24, -1, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::BOSS2, 19, -1, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::FIXBOT_KL, 9, -1, 0.4f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::TANK_64, 21, -1, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::GUNCMDR_KL, 21, -1, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::PSX_GUARDIAN, -1, 24, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::WIDOW2, 19, -1, 0.15f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::PSX_ARACHNID, 14, -1, 0.1f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::MAKRON_KL, 26, -1, 0.2f, BossSizeCategory::Medium},
	{horde::MonsterTypeID::MAKRON, 16, 25, 0.1f, BossSizeCategory::Medium}
}};

static constexpr std::array<boss_t, 13> BOSS_LARGE_SRC = {{
	{horde::MonsterTypeID::CARRIER, 24, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::BOSS2, 19, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::FIXBOT_KL, 9, -1, 0.4f, BossSizeCategory::Large},
	{horde::MonsterTypeID::BOSS5, -1, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::TANK_64, -1, 20, 0.45f, BossSizeCategory::Large},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Large},
	{horde::MonsterTypeID::GUNCMDR_KL, -1, 20, 0.3f, BossSizeCategory::Large},
	{horde::MonsterTypeID::PSX_ARACHNID, 14, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::WIDOW, -1, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::PSX_GUARDIAN, -1, -1, 0.1f, BossSizeCategory::Large},
	{horde::MonsterTypeID::JORG, 30, -1, 0.15f, BossSizeCategory::Large},
	{horde::MonsterTypeID::MAKRON_KL, 30, -1, 0.2f, BossSizeCategory::Large},
	{horde::MonsterTypeID::WIDOW2, 25, -1, 0.15f, BossSizeCategory::Large}
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
	const size_t check_count = std::min(count, limit);
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
			for (size_t map_id_idx = 0; map_id_idx < MAP_ID_COUNT; ++map_id_idx)
			{
				const horde::MapID mapId = static_cast<horde::MapID>(map_id_idx);

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

				EligibilityData& eligibilityData = cache[wave][map_size_idx][map_id_idx];
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
}

const BossEligibilityCache::EligibilityData&
BossEligibilityCache::get(int32_t wave, horde::MapSize size, horde::MapID mapId) const
{
	const int32_t safe_wave = std::min(wave, MAX_PRECOMPUTED_WAVE);
	// Determine map size index based on MapSize struct
	size_t map_size_idx = size.isSmallMap ? 0 : (size.isBigMap ? 2 : 1);

	const auto& result = cache[safe_wave][map_size_idx][static_cast<size_t>(mapId)];


	return result;
}

// Helper function to get boss list based on map size
const BossDataSoA *GetBossListSoA(const horde::MapSize &mapSize, horde::MapID mapId)
{
	if (mapSize.isSmallMap)
		return &g_smallBossData;
	else if (mapSize.isMediumMap)
		return &g_mediumBossData;
	else if (mapSize.isBigMap)
		return &g_largeBossData;

	return nullptr;
}

// Boss selection function
BossPickResult G_HordePickBOSSType(const horde::MapSize& mapSize, std::string_view mapname, int32_t waveNumber)
{
	// Ensure cache is initialized (call InitializeBossWaveTypes which handles the one-time init)
	if (!g_bossWaveTypeArray[0].announcement)
		InitializeBossWaveTypes();

	// Get the appropriate boss list
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(mapname.data());

	const BossDataSoA* boss_list_soa = GetBossListSoA(mapSize, mapId);
	if (!boss_list_soa)
	{
		if (developer && developer->integer)
			gi.Com_PrintFmt("WARNING: Empty boss list for map {} at wave {}\n", mapname.data(), waveNumber);
		return BossPickResult();
	}

	// Get eligible bosses from cache
	const int32_t safeWaveNumber = std::min(waveNumber, BossEligibilityCache::MAX_PRECOMPUTED_WAVE);
	const BossEligibilityCache::EligibilityData& eligibilityData = g_bossEligibilityCache.get(safeWaveNumber, mapSize, mapId);

	if (eligibilityData.count == 0)
	{
		if (developer && developer->integer)
			gi.Com_PrintFmt("WARNING: No bosses eligible for wave {} on this map type.\n", waveNumber);
		return BossPickResult();
	}

	struct WeightedBoss
	{
		size_t index_in_soa;
		float weight;
		float cumulativeWeight;
	};

	// Calculate how many recent bosses to track based on eligible count
	// Track ~40% of eligible bosses (min 2, max 5) to ensure good variety
	const size_t max_recent_to_track = std::clamp(
		eligibilityData.count * 2 / 5,  // 40% of eligible bosses
		static_cast<size_t>(2),          // Minimum 2
		static_cast<size_t>(RecentBosses::MAX_RECENT_BOSSES)  // Maximum 5
	);

	// Lambda to build weighted boss list
	auto build_weighted_list = [&](bool exclude_recent, size_t recent_limit) {
		std::array<WeightedBoss, BossEligibilityCache::MAX_ELIGIBLE_BOSSES> weighted_list{};
		size_t count = 0;
		float total_weight = 0.0f;

		for (size_t i = 0; i < eligibilityData.count; ++i)
		{
			const size_t boss_index_in_soa = eligibilityData.soa_indices[i];
			const horde::MonsterTypeID bossTypeId = boss_list_soa->typeIds[boss_index_in_soa];

			// Skip recent bosses if requested (only check the most recent N)
			if (exclude_recent && recent_bosses.contains(bossTypeId, recent_limit))
				continue;

			float weight = boss_list_soa->weights[boss_index_in_soa];
			if (weight > 0.0f)
			{
				total_weight += weight;
				weighted_list[count].index_in_soa = boss_index_in_soa;
				weighted_list[count].weight = weight;
				weighted_list[count].cumulativeWeight = total_weight;
				count++;
			}
		}

		return std::make_tuple(weighted_list, count, total_weight);
	};

	if (developer && developer->integer)
	{
		gi.Com_PrintFmt("Boss Selection: {} eligible bosses, tracking {} recent (40% rule)\n",
			eligibilityData.count, max_recent_to_track);
	}

	// First pass: try to find bosses not in recent history
	auto [weightedBosses, weightedCount, totalWeight] = build_weighted_list(true, max_recent_to_track);

	// If no non-recent bosses found, use all eligible bosses
	if (weightedCount == 0)
	{
		if (developer && developer->integer)
			gi.Com_PrintFmt("INFO: No non-recent bosses eligible, ignoring history for this pick.\n");

		std::tie(weightedBosses, weightedCount, totalWeight) = build_weighted_list(false, max_recent_to_track);
	}

	if (weightedCount == 0)
	{
		if (developer && developer->integer)
			gi.Com_PrintFmt("WARNING: No eligible bosses found after all filtering.\n");
		return BossPickResult();
	}

	// Pick a random boss based on weights
	float randomValue = brandom() * totalWeight;
	auto it = std::lower_bound(
		weightedBosses.begin(),
		weightedBosses.begin() + weightedCount,
		randomValue,
		[](const WeightedBoss& boss, float value)
		{
			return boss.cumulativeWeight < value;
		});

	if (it == weightedBosses.begin() + weightedCount)
		it = weightedBosses.begin() + (weightedCount - 1);

	const size_t chosen_index = it->index_in_soa;
	const horde::MonsterTypeID chosen_typeId = boss_list_soa->typeIds[chosen_index];
	const BossSizeCategory chosen_sizeCategory = boss_list_soa->sizeCategories[chosen_index];

	recent_bosses.add(chosen_typeId);

	if (developer && developer->integer)
	{
		const char* chosen_name = horde::MonsterTypeRegistry::GetClassname(chosen_typeId);
		gi.Com_PrintFmt("Selected Boss: {} (Weight: {:.2f})\n", chosen_name ? chosen_name : "Unknown", it->weight);
	}

	return BossPickResult(chosen_typeId, chosen_sizeCategory);
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
		{IT_WEAPON_DISRUPTOR, 9}, // Rogue (originally disintegrator)
		{IT_WEAPON_BFG, 9}		   // Moved slightly later
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
	static const std::array<item_id_t, 8> standardItemIDs = {
		IT_ITEM_ADRENALINE, IT_ITEM_PACK, IT_ITEM_SENTRYGUN,
		IT_ITEM_SPHERE_DEFENDER, IT_ARMOR_COMBAT, IT_ITEM_BANDOLIER,
		IT_ITEM_INVULNERABILITY, IT_AMMO_NUKE};

	// Drop primary weapon
	item_id_t weapon_id = SelectBossWeaponDrop(current_wave_level);
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
	item_id_t special_id = brandom() ? IT_ITEM_QUADFIRE : IT_ITEM_QUAD;
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

	for (item_id_t item_id : shuffledIDs)
	{
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

	// Basic wave check
	if (current_wave_level < MIN_BOSS_WAVE || current_wave_level % BOSS_WAVE_INTERVAL != 0) {
		boss_spawned_for_wave = false;
		return;
	}

	// Select boss type
	const char *map_name = GetCurrentMapName();
	BossPickResult boss_pick_result = G_HordePickBOSSType(GetCurrentMapSize(), map_name, current_wave_level);

	if (boss_pick_result.typeId == horde::MonsterTypeID::UNKNOWN) {
		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: Failed to pick a boss type for wave {}.\n", current_wave_level);
		boss_spawned_for_wave = false;
		return;
	}

	// Determine spawn location
	vec3_t spawn_origin;
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(map_name);
	if (!horde::MapOriginRegistry::GetOrigin(mapId, spawn_origin))
	{
		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: No designated boss spawn origin found for map '{}'. Retrying next frame.\n", map_name);
		boss_spawned_for_wave = false;
		return;
	}

	// Prepare spawn area
	vec3_t predicted_mins, predicted_maxs;
	GetPredictedScaledBounds(boss_pick_result.typeId, predicted_mins, predicted_maxs);

	// Apply worst-case boss effect scaling based on size category
	// ApplyBossEffects can scale bosses up to 1.5x (Large), 1.3x (Medium), or 1.1x (Small)
	float boss_effect_scale = 1.0f;
	switch (boss_pick_result.sizeCategory) {
		case BossSizeCategory::Large:
			boss_effect_scale = 1.5f;
			break;
		case BossSizeCategory::Medium:
			boss_effect_scale = 1.3f;
			break;
		case BossSizeCategory::Small:
			boss_effect_scale = 1.1f;
			break;
	}
	predicted_mins *= boss_effect_scale;
	predicted_maxs *= boss_effect_scale;

	// HYBRID APPROACH: Try relaxed validation first, then alternative spawns if needed
	const bool is_flying = IsFlying(boss_pick_result.typeId);
	vec3_t final_spawn_origin;
	bool spawn_valid = false;

	// Step 1: Clear and validate primary spawn with relaxed boss checks
	ClearSpawnArea(spawn_origin, predicted_mins, predicted_maxs);
	const auto primary_validation = IsPositionPhysicallyValidForBoss(spawn_origin, predicted_mins, predicted_maxs, is_flying);

	if (primary_validation.is_valid)
	{
		final_spawn_origin = primary_validation.adjusted_position;
		PushEntitiesAway(final_spawn_origin, PUSH_ITERATIONS, PUSH_BASE_RADIUS, PUSH_BASE_STRENGTH, PUSH_PLAYER_STRENGTH, PUSH_MONSTER_STRENGTH);
		spawn_valid = true;

		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: Primary spawn validated at {}\n", final_spawn_origin);
	}
	// Step 2: If primary fails, try alternative spawn points
	else if (TryAlternativeSpawnPoints(spawn_origin, predicted_mins, predicted_maxs, is_flying,
		PUSH_ITERATIONS, PUSH_BASE_RADIUS, PUSH_BASE_STRENGTH, PUSH_PLAYER_STRENGTH, PUSH_MONSTER_STRENGTH, final_spawn_origin))
	{
		spawn_valid = true;

		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: Using alternative spawn at {}\n", final_spawn_origin);
	}
	// Step 3: All attempts failed - abort and retry next frame
	else
	{
		if (developer->integer)
			gi.Com_PrintFmt("SpawnBossAutomatically: All spawn attempts failed at {}. Will retry next frame.\n", spawn_origin);
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
	else if (self->inuse && developer->integer)
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
		gi.Com_PrintFmt("BossSpawnThink: Finalized spawn for boss {} at {}.\n", self->classname, self->s.origin);
	}
}

// Boss teleportation function
bool CheckAndTeleportBoss(edict_t *self, BossTeleportReason reason)
{

	// Early validation
	if (!self || !self->inuse || self->deadflag || !self->monsterinfo.IS_BOSS || !g_horde->integer)
	{
		if (developer->integer)
		{
			const char* reason_str = (reason == BossTeleportReason::DROWNING) ? "drowning" :
									 (reason == BossTeleportReason::TRIGGER_HURT) ? "trigger_hurt" : "unknown";
			gi.Com_PrintFmt("CTB: Early exit for {}. InUse:{} Dead:{} IsBoss:{} HordeInt:{}\n",
							self->classname ? self->classname : "unknown",
							self->inuse,
							self->deadflag,
							self->monsterinfo.IS_BOSS,
							g_horde->integer);
		}
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

	if (mapId == horde::MapID::UNKNOWN)
	{
		if (developer->integer)
			gi.Com_PrintFmt("CTB: MapID unknown for {}, cannot get teleport origin.\n", current_map);
		return false;
	}

	vec3_t destination_origin;
	if (!horde::MapOriginRegistry::GetOrigin(mapId, destination_origin))
	{
		if (developer->integer)
			gi.Com_PrintFmt("CTB: Failed to get teleport origin for map_id {}.\n", (int)mapId);
		return false;
	}

	// Perform teleportation
	bool force_teleport = (reason == BossTeleportReason::TRIGGER_HURT || reason == BossTeleportReason::DROWNING);
	vec3_t destination_angles = self->s.angles;

	if (developer->integer > 1)
	{
		gi.Com_PrintFmt("CTB: Attempting Horde_TeleportMonster for boss {} to ({},{},{}) (ForceVisible: {})\n",
						self->classname ? self->classname : "UNKNOWN",
						destination_origin[0], destination_origin[1], destination_origin[2],
						force_teleport);
	}

	if (!Horde_TeleportMonster(self, destination_origin, destination_angles, true, force_teleport))
	{
		if (developer->integer > 1)
			gi.Com_PrintFmt("CTB: Horde_TeleportMonster returned false for boss {}.\n",
							self->classname ? self->classname : "UNKNOWN");
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

	if (developer->integer)
	{
		const char *reason_str = reason == BossTeleportReason::DROWNING ? "drowning" : "trigger_hurt";
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
		{MonsterWaveType::Ground | MonsterWaveType::Medium, "The Red Mutant seeks vengeance!\n"};
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
	return current_wave_level >= MIN_BOSS_WAVE && current_wave_level % BOSS_WAVE_INTERVAL == 0;
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
}

void ResetRecentBosses()
{
	recent_bosses.clear();
}