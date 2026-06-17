#include "horde_monster_data.h"
#include "../shared.h"
#include "g_horde.h"  // For MonsterWaveType operators and HasWaveType
#include "horde_constants.h"
#include <algorithm>

// Complete monster type definitions.
// Note: order is not strictly by minWave; SoA lookups use typeId as the index.
const MonsterTypeInfo monsterTypes[] = {
	// --- WAVE 1 ---
	{horde::MonsterTypeID::SOLDIER_LIGHT, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 1.0f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
	{horde::MonsterTypeID::SOLDIER, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 0.9f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
	{horde::MonsterTypeID::FLYER, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Fast, 1, 0.7f, {-16, -16, -24}, {16, 16, 16}, 1.0f},

	// --- WAVE 2 ---
	{horde::MonsterTypeID::SOLDIER_SS, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 2, 0.8f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
	{horde::MonsterTypeID::GEKK, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Small | MonsterWaveType::Mutant | MonsterWaveType::Gekk, 4, 0.7f, {-16, -16, -24}, {16, 16, -8}, 1.0f},
	// --- WAVE 3 ---
	{horde::MonsterTypeID::INFANTRY_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 3, 0.85f, {-16, -16, -24}, {16, 16, 32}, 1.0f},

	// --- WAVE 4 ---
	{horde::MonsterTypeID::SOLDIER_HYPERGUN, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 4, 0.7f, {-16, -16, -24}, {16, 16, 32}, 1.0f},


	// --- WAVE 5 ---
	{horde::MonsterTypeID::PARASITE, MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Melee, 5, 0.6f, {-16, -16, -24}, {16, 16, 24}, 1.0f},

	// --- WAVE 6 ---
	{horde::MonsterTypeID::SOLDIER_RIPPER, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.8f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
	{horde::MonsterTypeID::FIXBOT, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 7, 0.45f, {-16, -16, -12}, {16, 16, 12}, 1.4f},
	{horde::MonsterTypeID::BRAIN, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special | MonsterWaveType::Melee | MonsterWaveType::Mutant, 6, 0.7f, {-16, -16, -24}, {16, 16, -8}, 1.0f},
	{horde::MonsterTypeID::BERSERK, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Melee | MonsterWaveType::Berserk, 6, 0.8f, {-16, -16, -24}, {16, 16, 32}, 1.0f},
	{horde::MonsterTypeID::CHICK, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 7, 0.6f, {-32, -32, -24}, {32, 32, 64}, 1.0f},

	// Special fog wave bosses
	{horde::MonsterTypeID::BERSERKERKL, MonsterWaveType::SemiBoss | MonsterWaveType::Melee | MonsterWaveType::Ground | MonsterWaveType::Berserk, 16, 0.65f, {-22, -22, -36}, {22, 22, 44}, 1.4f},
	{horde::MonsterTypeID::GEKKKL, MonsterWaveType::SemiBoss | MonsterWaveType::Melee | MonsterWaveType::Ground | MonsterWaveType::Gekk, 16, 0.65f, {-19, -19, -29}, {19, 19, -10}, 1.4f},

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
	{horde::MonsterTypeID::CHICK_HEAT, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Fast, 13, 0.6f, {-32, -32, -24}, {32, 32, 64}, 1.0f},
	{horde::MonsterTypeID::REDMUTANT, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Elite | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 13, 0.35f, {-18, -18, -24}, {18, 18, 30}, 1.1f},

	// --- WAVE 14 ---
	{horde::MonsterTypeID::SHAMBLER_SMALL, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Mutant | MonsterWaveType::Shambler, 14, 0.4f, {-32, -32, -24}, {32, 32, 64}, 0.6f},
	{horde::MonsterTypeID::TANK, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 14, 0.4f, {-32, -32, -16}, {32, 32, 64}, 1.0f},

	// --- WAVE 15 ---
	{horde::MonsterTypeID::TANK_SPAWNER, MonsterWaveType::Ground | MonsterWaveType::Spawner | MonsterWaveType::Heavy | MonsterWaveType::Medium | MonsterWaveType::Elite, 15, 0.6f, {-32, -32, -16}, {32, 32, 64}, 1.0f},
	{horde::MonsterTypeID::GM_ARACHNID, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Heavy | MonsterWaveType::Elite, 15, 0.45f, {-48, -48, -20}, {48, 48, 48}, 0.85f},
	{horde::MonsterTypeID::GUNCMDR, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Bomber, 15, 0.7f, {-16, -16, -24}, {16, 16, 36}, 1.25f},

	// --- WAVE 16 ---
	{horde::MonsterTypeID::TANK_COMMANDER, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 16, 0.5f, {-32, -32, -16}, {32, 32, 64}, 1.0f},

	// --- WAVE 17 ---
	{horde::MonsterTypeID::RUNNERTANK, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Fast, 17, 0.35f, {-32, -32, -16}, {32, 32, 64}, 1.0f},

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
	{horde::MonsterTypeID::PERRO_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Fast | MonsterWaveType::Small, 20, 0.4f, {-16, -16, -24}, {16, 16, 24}, 1.2f},

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
	{horde::MonsterTypeID::CARRIER_MINI, MonsterWaveType::Flying | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.2f, {-56, -56, -44}, {56, 56, 44}, 0.6f },
	{horde::MonsterTypeID::CHICKKL, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Medium, 28, 0.3f, {-16, -16, 0}, {16, 16, 56}, 1.5f },

	// --- WAVE 28 ---
	{horde::MonsterTypeID::TANK_64, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 28, 0.3f, {-32, -32, -16}, {32, 32, 64}, 1.1f},

	// --- WAVE 33 ---
	{horde::MonsterTypeID::SHAMBLER_KL, MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 33, 0.23f, {-32, -32, -24}, {32, 32, 64}, 1.0f},
	{horde::MonsterTypeID::GUNCMDR_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 33, 0.2f, {-16, -16, -24}, {16, 16, 36}, 1.25f},
	{horde::MonsterTypeID::JORG_SMALL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Medium, 33, 0.4f, {-80, -80, 0}, {80, 80, 140}, 0.35f},

	// --- WAVE 41 ---
	{horde::MonsterTypeID::MAKRON_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Elite, 45, 0.35f, {-30, -30, 0}, {30, 30, 90}, 1.0f},

	// --- SPECIAL / NOT NORMALLY SPAWNED (minWave 999) ---
	{horde::MonsterTypeID::TURRET, MonsterWaveType::Ground | MonsterWaveType::Special, 999, 0.0f, {-16, -16, -16}, {16, 16, 16}, 1.0f},
	{horde::MonsterTypeID::SENTRYGUN, MonsterWaveType::Ground | MonsterWaveType::Special, 999, 0.0f, {-16, -16, -16}, {16, 16, 16}, 1.0f},
	{horde::MonsterTypeID::BOSS2, MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-60, -60, 0}, {60, 60, 90}, 1.0f},
	{horde::MonsterTypeID::CARRIER, MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-56, -56, -44}, {56, 56, 44}, 1.0f},
	{horde::MonsterTypeID::FIXBOT_KL, MonsterWaveType::Flying | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-16, -16, -12}, {16, 16, 12}, 2.6f},
	{horde::MonsterTypeID::WIDOW, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-40, -40, 0}, {40, 40, 144}, 1.0f},
	{horde::MonsterTypeID::WIDOW2, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 999, 0.0f, {-70, -70, 0}, {70, 70, 144}, 0.8f},
	{horde::MonsterTypeID::BOSS5, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-32, -32, -16}, {32, 32, 64}, 1.0f},
	{horde::MonsterTypeID::JORG, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-80, -80, 0}, {80, 80, 140}, 1.0f},
	{horde::MonsterTypeID::PSX_GUARDIAN, MonsterWaveType::Ground | MonsterWaveType::Boss | MonsterWaveType::Heavy, 999, 0.0f, {-78, -78, -66}, {78, 78, 62}, 0.0f}};

static_assert(std::size(monsterTypes) == MONSTER_DATA_COUNT,
	"MONSTER_DATA_COUNT must match monsterTypes[]");
static_assert(static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES) == MonsterDataSoA::MONSTER_ARRAY_SIZE,
	"MonsterDataSoA::MONSTER_ARRAY_SIZE must match MonsterTypeID::MAX_TYPES");

// Convert monster data from AoS to SoA for fast indexed lookups.
constexpr MonsterDataSoA create_monster_data_soa()
{
	MonsterDataSoA soa_data{};

	for (size_t i = 0; i < std::size(monsterTypes); ++i)
	{
		const auto &monster_info = monsterTypes[i];
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

// Global SoA instance
const MonsterDataSoA g_monsterData = create_monster_data_soa();

// Get wave types for a monster
MonsterWaveType GetMonsterWaveTypes(horde::MonsterTypeID typeId)
{
	const size_t index = static_cast<size_t>(typeId);
	if (index >= g_monsterData.MONSTER_ARRAY_SIZE)
	{
		return MonsterWaveType::None;
	}
	return g_monsterData.waveTypes[index];
}

// Get predicted scaled bounds for a monster type
bool GetPredictedScaledBounds(horde::MonsterTypeID typeId, vec3_t &out_mins, vec3_t &out_maxs)
{
	const size_t index = static_cast<size_t>(typeId);
	if (typeId == horde::MonsterTypeID::UNKNOWN || index >= g_monsterData.MONSTER_ARRAY_SIZE)
	{
		// Fallback for invalid ID
		constexpr vec3_t VALIDATE_CHECK_MINS = {-16, -16, -24};
		constexpr vec3_t VALIDATE_CHECK_MAXS = {16, 16, 32};
		out_mins = VALIDATE_CHECK_MINS;
		out_maxs = VALIDATE_CHECK_MAXS;
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

// Category check functions
bool IsFlying(horde::MonsterTypeID typeId)
{
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Flying);
}

bool IsGroundUnit(horde::MonsterTypeID typeId)
{
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Ground);
}

bool IsSmallUnit(horde::MonsterTypeID typeId)
{
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Small);
}

bool IsSpecialUnit(horde::MonsterTypeID typeId)
{
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Special);
}

// Validate monster for wave requirements
bool IsValidMonsterForWave(horde::MonsterTypeID typeId, MonsterWaveType waveRequirements)
{
	// Fast exit for special cases like boss minion waves that have no requirements
	if (waveRequirements == MonsterWaveType::None)
	{
		return true;
	}

	// Use the fast LUT to get the monster's properties
	const MonsterWaveType monster_flags = GetMonsterWaveTypes(typeId);

	// Step 1: Handle Exclusive, Thematic Waves (Strict Matching)
	static constexpr std::array<MonsterWaveType, 6> special_themes = {
		MonsterWaveType::Gekk, MonsterWaveType::Berserk, MonsterWaveType::Mutant,
		MonsterWaveType::Spawner, MonsterWaveType::Shambler, MonsterWaveType::Arachnophobic};

	for (const auto &theme : special_themes)
	{
		if (HasWaveType(waveRequirements, theme) && !HasWaveType(monster_flags, theme))
		{
			// If the wave has this theme, the monster must also have it
			return false;
		}
	}

	// Step 2: Handle General Wave Composition (Flexible Matching)

	// A. Movement Type Check
	const bool wave_wants_ground = HasWaveType(waveRequirements, MonsterWaveType::Ground);
	const bool wave_wants_flying = HasWaveType(waveRequirements, MonsterWaveType::Flying);
	const bool monster_is_ground = HasWaveType(monster_flags, MonsterWaveType::Ground);
	const bool monster_is_flying = HasWaveType(monster_flags, MonsterWaveType::Flying);

	// If the wave wants BOTH ground and flying, the monster must be one or the other
	if (wave_wants_ground && wave_wants_flying)
	{
		if (!monster_is_ground && !monster_is_flying)
			return false;
	}
	// If the wave wants ONLY ground, the monster must be ground
	else if (wave_wants_ground)
	{
		if (!monster_is_ground)
			return false;
	}
	// If the wave wants ONLY flying, the monster must be flying
	else if (wave_wants_flying)
	{
		if (!monster_is_flying)
			return false;
	}

	// B. Category Check
	constexpr MonsterWaveType category_mask =
		MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Heavy |
		MonsterWaveType::Small | MonsterWaveType::Fast | MonsterWaveType::Elite |
		MonsterWaveType::Melee | MonsterWaveType::Ranged | MonsterWaveType::Bomber |
		MonsterWaveType::Special;

	const MonsterWaveType required_categories = waveRequirements & category_mask;

	// If the wave specifies any categories, the monster must have at least one of them
	if (required_categories != MonsterWaveType::None)
	{
		if ((monster_flags & required_categories) == MonsterWaveType::None)
		{
			return false;
		}
	}

	// All checks passed
	return true;
}

int32_t GetAdjustedMinWave(horde::MonsterTypeID typeId, int32_t map_seed)
{
	(void) map_seed;

	const size_t index = static_cast<size_t>(typeId);
	if (index >= g_monsterData.MONSTER_ARRAY_SIZE)
		return 999;

	const int32_t base_wave = g_monsterData.minWaves[index];
	if (base_wave <= 0)
		return 999;

	return base_wave;
}
