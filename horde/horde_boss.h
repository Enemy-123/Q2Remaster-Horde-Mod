//#pragma once
//
//#ifndef HORDE_BOSS_H
//#define HORDE_BOSS_H
//
//#include "../g_local.h" // For edict_t, gtime_t, vec3_t etc.
//#include "horde_ids.h"  // For horde::MonsterTypeID, horde::MapID, horde::MapSize
//#include "horde_spawning.h" // For HordeConstants, ConditionParams? (Check dependencies)
//#include "g_horde.h" // For MonsterWaveType
//#include <array>
//#include <vector>
//#include <string_view>
//#include <unordered_set>
//#include <span> // If GetBossList returns span
//
//// --- Struct Definitions (Reconstructed/Moved) ---
//
//// Struct defining a boss type entry
//struct boss_t {
//	horde::MonsterTypeID typeId; // Correctly namespaced
//	const char* classname;
//	int minWave;
//	float weight;
//};
//
//// Struct to hold eligible bosses for selection
//struct EligibleBosses {
//	std::array<const boss_t*, HordeConstants::MAX_ELIGIBLE_BOSSES> bosses;
//	size_t count = 0;
//	float total_weight = 0.0f;
//
//	void clear();
//	bool add(const boss_t* boss, float weight);
//	const boss_t* select() const;
//};
//
//// Struct to track recently spawned bosses
//struct RecentBosses {
//	std::array<horde::MonsterTypeID, HordeConstants::MAX_RECENT_BOSSES> types; // Correctly namespaced
//	size_t index = 0;
//	size_t filled_count = 0; // Track how many slots are actually used
//
//	void add(horde::MonsterTypeID typeId); // Correctly namespaced
//	bool contains(horde::MonsterTypeID typeId) const; // Correctly namespaced
//	void clear();
//};
//
//// Struct to cache boss eligibility based on wave, map, and size
//struct BossEligibilityCache {
//	EligibleBosses eligible_cache;
//	int32_t cached_wave = -1;
//	horde::MapID cached_mapId = horde::MapID::UNKNOWN; // Correctly namespaced
//	horde::MapSize cached_mapSize; // Correctly namespaced
//
//	void clear();
//};
//
//// Struct to hold info about boss wave types and sounds
//struct BossWaveInfo {
//	MonsterWaveType waveType = MonsterWaveType::None; // Now defined via g_horde.h
//	const char* soundPath = nullptr;
//};
//
//// Enum for Boss Teleport Reasons
//enum class BossTeleportReason {
//	DROWNING,
//	STUCK,
//	TRIGGER_HURT // Added missing reason
//};
//
//
//// --- Global Variable Declarations (Defined in horde_boss.cpp) ---
//extern bool boss_spawned_for_wave;
//extern std::unordered_set<edict_t*> auto_spawned_bosses;
//extern std::array<std::string_view, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_bossMessagesArray; // Correctly namespaced
//extern bool g_bossMessagesInitialized;
//extern std::array<BossWaveInfo, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_bossWaveTypeArray; // Correctly namespaced
//extern bool g_bossWaveTypesInitialized;
//extern RecentBosses recent_bosses;
//extern BossEligibilityCache g_bossEligibilityCache;
//extern const boss_t BOSS_SMALL[];
//extern const boss_t BOSS_MEDIUM[];
//extern const boss_t BOSS_LARGE[];
//
//
//// --- Function Declarations (Implementations in horde_boss.cpp) ---
//void BossDeathHandler(edict_t* boss);
//void boss_die(edict_t* boss); // Consider if this should be static in .cpp if not called externally
//item_id_t SelectBossWeaponDrop(int32_t wave_level); // Made non-static as it might be useful elsewhere
//horde::MonsterTypeID G_HordePickBOSSType(const horde::MapSize& mapSize, std::string_view mapname, int32_t waveNumber, edict_t* bossEntity); // Correctly namespaced MapSize
//void AttachHealthBar(edict_t* boss); // Made non-static
//void BossSpawnThink(edict_t* self);
//void SP_target_orb(edict_t* ent); // Standard spawn function signature
//void InitializeBossMessages();
//std::string_view GetBossMessage(horde::MonsterTypeID typeId); // Correctly namespaced
//void InitializeBossWaveTypes();
//std::pair<MonsterWaveType, const char*> GetBossWaveType(horde::MonsterTypeID typeId); // Correctly namespaced MonsterTypeID, MonsterWaveType now defined
//bool CheckAndTeleportBoss(edict_t* self, BossTeleportReason reason = BossTeleportReason::DROWNING);
//void SetHealthBarName(const edict_t* boss);
//void SpawnBossAutomatically(); // Made non-static
//void ResetBosses();
//void ResetRecentBosses() noexcept; // Made non-static
//
//// Helper function declaration (if needed externally, otherwise keep static in .cpp)
//// static std::span<const boss_t> GetBossList(const horde::MapSize& mapSize, horde::MapID mapId); // Keep static in .cpp for now
//
//
//#endif // HORDE_BOSS_H
