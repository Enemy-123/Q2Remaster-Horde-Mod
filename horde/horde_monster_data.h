#pragma once

#include "horde_ids.h"
#include <array>
#include <cstdint>

// Forward declarations
struct vec3_t;
enum class MonsterWaveType : uint32_t;

// Monster type information structure
struct MonsterTypeInfo
{
	horde::MonsterTypeID typeId;
	MonsterWaveType types;
	int minWave;
	float weight;

	// Bounding box defaults
	vec3_t default_mins;
	vec3_t default_maxs;
	float s_scale; // Intended scale for this monster type

	// Constructor
	constexpr MonsterTypeInfo(horde::MonsterTypeID id, MonsterWaveType t, int w, float wt,
							  vec3_t d_mins, vec3_t d_maxs, float scale = 1.0f)
		: typeId(id), types(t), minWave(w), weight(wt),
		  default_mins(d_mins), default_maxs(d_maxs), s_scale(scale)
	{
	}
};

// Structure of Arrays for efficient monster data access
struct MonsterDataSoA
{
	static constexpr size_t MONSTER_ARRAY_SIZE = 128;  // Must match horde::MonsterTypeID::MAX_TYPES

	std::array<MonsterWaveType, MONSTER_ARRAY_SIZE> waveTypes;
	std::array<int, MONSTER_ARRAY_SIZE> minWaves;
	std::array<float, MONSTER_ARRAY_SIZE> weights;
	std::array<vec3_t, MONSTER_ARRAY_SIZE> default_mins;
	std::array<vec3_t, MONSTER_ARRAY_SIZE> default_maxs;
	std::array<float, MONSTER_ARRAY_SIZE> s_scales;
};

// Monster data access functions
MonsterWaveType GetMonsterWaveTypes(horde::MonsterTypeID typeId);
bool GetPredictedScaledBounds(horde::MonsterTypeID typeId, vec3_t &out_mins, vec3_t &out_maxs);

// Get the effective unlock wave for a monster type.
// This currently returns the authored progression value directly.
int32_t GetAdjustedMinWave(horde::MonsterTypeID typeId, int32_t map_seed);

// Monster category checks
bool IsFlying(horde::MonsterTypeID typeId);
bool IsGroundUnit(horde::MonsterTypeID typeId);
bool IsSmallUnit(horde::MonsterTypeID typeId);
bool IsSpecialUnit(horde::MonsterTypeID typeId);
bool IsValidMonsterForWave(horde::MonsterTypeID typeId, MonsterWaveType waveRequirements);

// Global monster data SoA (extern declaration)
extern const MonsterDataSoA g_monsterData;
extern const MonsterTypeInfo monsterTypes[];

// Const expression for number of monsters - can be used at compile time
constexpr size_t MONSTER_DATA_COUNT = 69;  // Must match monsterTypes[] entry count
