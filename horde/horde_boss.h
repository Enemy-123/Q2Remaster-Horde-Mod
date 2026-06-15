#pragma once

#include "../g_local.h"
#include "horde_ids.h"
#include "g_horde.h"
#include <array>
#include <string_view>

// Boss size categories (moved from g_local.h if needed)
// Note: Keep this in g_local.h if it's used elsewhere

// Boss type definitions
struct boss_t
{
    horde::MonsterTypeID typeId;
    int32_t min_level;
    int32_t max_level;
    float weight;
    BossSizeCategory sizeCategory;
};

// Boss data structure of arrays for performance
struct BossDataSoA
{
    static constexpr size_t MAX_BOSSES = 20;
    std::array<horde::MonsterTypeID, MAX_BOSSES> typeIds;
    std::array<int32_t, MAX_BOSSES> min_levels;
    std::array<int32_t, MAX_BOSSES> max_levels;
    std::array<float, MAX_BOSSES> weights;
    std::array<BossSizeCategory, MAX_BOSSES> sizeCategories;
    size_t count; // Number of actual bosses in this list
};

// Recent bosses tracking
struct RecentBosses
{
    static constexpr size_t MAX_RECENT_BOSSES = 5;
    std::array<horde::MonsterTypeID, MAX_RECENT_BOSSES> items;
    size_t count;

    RecentBosses();
    void add(horde::MonsterTypeID boss_id);
    void add(const char *boss_classname);
    bool contains(horde::MonsterTypeID boss_id) const;
    bool contains(horde::MonsterTypeID boss_id, size_t limit) const;
    bool contains(const char *boss_classname) const;
    void clear();
};

// Boss eligibility cache for performance
struct BossEligibilityCache
{
    static constexpr size_t MAX_ELIGIBLE_BOSSES = 20;
    static constexpr int32_t MAX_PRECOMPUTED_WAVE = 60;

    struct EligibilityData
    {
        std::array<size_t, MAX_ELIGIBLE_BOSSES> soa_indices;
        size_t count;
    };

    std::array<std::array<EligibilityData, 3>, MAX_PRECOMPUTED_WAVE + 1> cache{};

    void initialize();
    const EligibilityData& get(int32_t wave, horde::MapSize size) const;
};

// Boss pick result
struct BossPickResult
{
    horde::MonsterTypeID typeId;
    BossSizeCategory sizeCategory;

    BossPickResult(horde::MonsterTypeID id = horde::MonsterTypeID::UNKNOWN,
                   BossSizeCategory size = BossSizeCategory::Medium);
};

// Boss wave info
struct BossWaveInfo
{
    MonsterWaveType waveType;
    const char* announcement;
};

// Global boss variables
extern bool boss_spawned_for_wave;
extern RecentBosses recent_bosses;

// Boss data arrays
extern const BossDataSoA g_smallBossData;
extern const BossDataSoA g_mediumBossData;
extern const BossDataSoA g_largeBossData;

// Boss selection and spawning
// When restrictToMapSizePool is true, the candidate pool is hard-restricted to the
// single boss list matching mapSize (used by maps with a boss_size override) instead
// of weighting across all size pools.
BossPickResult G_HordePickBOSSType(const horde::MapSize& mapSize, std::string_view mapname, int32_t waveNumber, bool restrictToMapSizePool = false);
void SpawnBossAutomatically();
void BossSpawnThink(edict_t *self);

// Boss death and drops
void BossDeathHandler(edict_t *boss) noexcept;
void boss_die(edict_t *boss) noexcept;
item_id_t SelectBossWeaponDrop(int32_t wave_level);

// Boss UI and display
void AttachHealthBar(edict_t *boss);
void SetHealthBarName(const edict_t *boss);
void ClearBossHealthBar(const edict_t* boss);
std::pair<MonsterWaveType, const char *> GetBossWaveType(horde::MonsterTypeID typeId);
void InitializeBossWaveTypes();

// Boss utilities
bool IsBossUnit(horde::MonsterTypeID typeId);
bool IsBossWave() noexcept;
void ResetBosses();
void ResetRecentBosses();
void HandleForcedBossRemoval(edict_t *boss);

// Helper functions
template<size_t N>
constexpr BossDataSoA create_boss_soa(const std::array<boss_t, N> &boss_list);
const BossDataSoA *GetBossListSoA(const horde::MapSize &mapSize);
