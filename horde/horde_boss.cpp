//// Implementations for boss-related functions will go here.
//
//#include "../g_local.h" // Include necessary base headers
//#include "g_horde.h"    // Include main horde header
//#include "horde_spawning.h" // May need spawning declarations/constants
//#include "horde_boss.h"   // Include its own header
//#include "../shared.h"
//#include <vector>
//#include <array>
//#include <string>
//#include <string_view>
//#include <numeric>
//#include <algorithm>
//#include <cmath>
//#include <random> // For std::shuffle if used later
//#include <unordered_set>
//#include <span> // Already included, but ensure it's present
//
//// Include other necessary headers if needed
//#include "horde_ids.h"
//
//// --- Definitions Moved from g_horde.cpp ---
//
//// Structure definition for HordeItemInfo
//struct HordeItemInfo {
//	item_id_t id;      // Use item_id_t instead of const char*
//	float weight;      // Relative probability (higher is more common)
//	int minWave;       // Minimum wave level for this item to appear
//
//	constexpr HordeItemInfo(item_id_t item_id, float w, int mw)
//		: id(item_id), weight(w), minWave(mw) {
//	}
//};
//
//// The comprehensive static array hordeItemData
//static const HordeItemInfo hordeItemData[] = {
//	// --- Armor ---
//	{ IT_ARMOR_SHARD,         1.5f,  1 }, // Common, small boost
//	{ IT_ARMOR_JACKET,        0.8f,  3 }, // Basic armor
//	{ IT_ARMOR_COMBAT,        0.5f,  8 }, // Better armor
//	{ IT_ARMOR_BODY,          0.2f, 15 }, // Best standard armor
//	{ IT_ITEM_POWER_SCREEN,   0.3f,  7 }, // Power Armor (early)
//	{ IT_ITEM_POWER_SHIELD,   0.01f, 19 }, // Power Armor (late)
//
//	// --- Weapons ---
//	// Blaster is skipped (always have)
//	{ IT_WEAPON_CHAINFIST,    0.1f,  1 }, // Melee, niche drop
//	{ IT_WEAPON_SHOTGUN,      1.0f,  1 }, // Basic weapon
//	{ IT_WEAPON_SSHOTGUN,     0.7f,  3 }, // Strong early/mid weapon
//	{ IT_WEAPON_MACHINEGUN,   1.0f,  1 }, // Basic weapon
//	{ IT_WEAPON_ETF_RIFLE,    0.6f,  5 }, // Rogue weapon (mid)
//	{ IT_WEAPON_CHAINGUN,     0.5f,  7 }, // Mid-tier DPS
//	{ IT_WEAPON_GLAUNCHER,    0.5f,  5 }, // Area denial (mid)
//	{ IT_WEAPON_PROXLAUNCHER, 0.3f,  8 }, // Rogue weapon (mid/late trap)
//	{ IT_WEAPON_RLAUNCHER,    0.4f,  7 }, // Powerful splash (mid/late)
//	{ IT_WEAPON_HYPERBLASTER, 0.4f,  9 }, // Energy weapon (mid/late)
//	{ IT_WEAPON_IONRIPPER,    0.4f, 10 }, // Xatrix weapon (mid/late)
//	{ IT_WEAPON_PLASMABEAM,   0.2f, 12 }, // Rogue weapon (late, high DPS)
//	{ IT_WEAPON_RAILGUN,      0.25f, 15 }, // High skill/damage (late)
//	{ IT_WEAPON_PHALANX,      0.25f, 14 }, // Xatrix weapon (late)
//	{ IT_WEAPON_BFG,          0.05f, 20 }, // Ultimate weapon (very late, rare)
//	{ IT_WEAPON_DISRUPTOR,    0.15f, 18 }, // Rogue weapon (late, piercing)
//	// Grenades/Traps/Tesla as 'weapons' (for direct drop maybe?)
//	{ IT_AMMO_GRENADES,       0.4f,  2 }, // Hand grenades item
//	{ IT_AMMO_TRAP,           0.2f,  6 }, // Xatrix trap item
//	{ IT_AMMO_TESLA,          0.15f, 8 }, // Rogue tesla item
//
//	// --- Ammo --- (Generally higher weight than corresponding weapons)
//	{ IT_AMMO_SHELLS,         2.0f,  1 }, // Common
//	{ IT_AMMO_BULLETS,        2.0f,  1 }, // Common
//	{ IT_AMMO_GRENADES,       1.2f,  2 }, // Mid frequency
//	{ IT_AMMO_ROCKETS,        1.0f,  6 }, // Needed for RL
//	{ IT_AMMO_CELLS,          1.5f,  7 }, // Needed for energy weapons
//	{ IT_AMMO_SLUGS,          0.8f, 14 }, // Needed for Railgun
//	{ IT_AMMO_MAGSLUG,        0.8f, 13 }, // Needed for Phalanx (Xatrix)
//	{ IT_AMMO_FLECHETTES,     1.5f,  4 }, // Needed for ETF Rifle (Rogue)
//	{ IT_AMMO_PROX,           0.6f,  7 }, // Needed for Prox Launcher (Rogue)
//	{ IT_AMMO_ROUNDS,         0.5f, 17 }, // Needed for Disruptor (Rogue)
//	{ IT_AMMO_TRAP,           0.4f,  5 }, // Needed for Trap (Xatrix)
//	{ IT_AMMO_TESLA,          0.3f,  7 }, // Needed for Tesla (Rogue)
//	{ IT_AMMO_NUKE,           0.02f, 25 }, // Very rare ammo/powerup (Rogue)
//
//	// --- Powerups ---
//	{ IT_ITEM_QUAD,           0.1f,  8 }, // Rare, powerful
//	{ IT_ITEM_QUADFIRE,       0.1f,  6 }, // Rare, powerful (Xatrix)
//	{ IT_ITEM_INVULNERABILITY,0.05f, 12 }, // Very rare, very powerful
//	{ IT_ITEM_INVISIBILITY,   0.2f,  5 }, // Situational
//	{ IT_ITEM_SILENCER,       0.15f, 4 }, // Situational
//	//{ IT_ITEM_REBREATHER,     0.1f,  1 }, // Map dependent, low weight
//	//{ IT_ITEM_ENVIROSUIT,     0.1f,  1 }, // Map dependent, low weight
//	{ IT_ITEM_ADRENALINE,     0.3f,  3 }, // Permanent health boost, good reward
//	{ IT_ITEM_BANDOLIER,      0.5f,  4 }, // Good ammo capacity boost
//	{ IT_ITEM_PACK,           0.25f, 10 }, // Excellent ammo capacity boost
//	{ IT_ITEM_IR_GOGGLES,     0.15f, 7 }, // Situational (Rogue)
//	{ IT_ITEM_DOUBLE,         0.15f, 5 }, // Damage boost (Rogue)
//	//{ IT_ITEM_SPHERE_VENGEANCE, 0.1f, 15 }, // Powerful sphere (Rogue)
//	{ IT_ITEM_SPHERE_HUNTER,  0.1f, 12 }, // Powerful sphere (Rogue)
//	{ IT_ITEM_SPHERE_DEFENDER,0.1f,  8 }, // Powerful sphere (Rogue)
//	{ IT_ITEM_DOPPELGANGER,   0.15f, 6 }, // Utility/Distraction (Rogue)
//	{ IT_ITEM_TELEPORT,       0.2f,  3 }, // Utility Escape
//	{ IT_ITEM_SENTRYGUN,      0.3f,  5 }, // Deployable Defense
//
//	// --- Health ---
//	{ IT_HEALTH_SMALL,        1.8f,  1 }, // Very common
//	{ IT_HEALTH_MEDIUM,       1.5f,  1 }, // Common
//	{ IT_HEALTH_LARGE,        0.8f,  3 }, // Less common
//	{ IT_HEALTH_MEGA,         0.1f, 10 }, // Rare, powerful boost
//
//	// --- Legacy Heads (Optional - Minor permanent boost) ---
//	//{ IT_ITEM_ANCIENT_HEAD,   0.05f, 10 },
//	//{ IT_ITEM_LEGACY_HEAD,    0.05f, 10 },
//
//	// End of list marker (optional, but good practice if needed)
//	// { IT_NULL, 0.0f, 0 }
//};
//
//// WeightedSelection template class definition
//template <typename T>
//struct WeightedSelection {
//	static constexpr size_t MAX_ITEMS = 32; // Adjust as needed
//
//	struct ItemEntry {
//		const T* item;
//		float weight;
//	};
//
//	ItemEntry items[MAX_ITEMS];
//	size_t item_count = 0;
//	float total_weight = 0.0f;
//
//	void clear() {
//		item_count = 0;
//		total_weight = 0.0f;
//	}
//
//	bool add(const T* item, float weight) {
//		if (!item || item_count >= MAX_ITEMS || weight <= 0.0f)
//			return false;
//
//		items[item_count] = { item, weight };
//		total_weight += weight;
//		item_count++;
//		return true;
//	}
//
//	const T* select() const {
//		if (item_count == 0 || total_weight <= 0.0f)
//			return nullptr;
//
//		const float random_weight = frandom() * total_weight;
//		float cumulative = 0.0f;
//		for (size_t i = 0; i < item_count; i++) {
//			cumulative += items[i].weight;
//			if (cumulative >= random_weight)
//				return items[i].item;
//		}
//		return items[item_count - 1].item; // Fallback
//	}
//};
//
//// --- Global/Static Variables Moved from g_horde.cpp ---
//bool boss_spawned_for_wave = false;
//std::unordered_set<edict_t*> auto_spawned_bosses;
//std::array<std::string_view, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_bossMessagesArray;
//bool g_bossMessagesInitialized = false;
//std::array<BossWaveInfo, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_bossWaveTypeArray;
//bool g_bossWaveTypesInitialized = false;
//RecentBosses recent_bosses;
//BossEligibilityCache g_bossEligibilityCache;
//
//// Define boss lists (moved from g_horde.cpp) - Adding classname back to match struct
//const boss_t BOSS_SMALL[] = {
//	{ horde::MonsterTypeID::BOSS2, "monster_boss2", 10, 1.0f },
//	{ horde::MonsterTypeID::CARRIER, "monster_carrier", 15, 0.8f },
//	{ horde::MonsterTypeID::WIDOW, "monster_widow", 20, 0.6f }, // Rogue boss
//	{ horde::MonsterTypeID::PSX_GUARDIAN, "monster_psxguardian", 25, 0.5f }, // Custom boss
//	{ horde::MonsterTypeID::REDMUTANT, "monster_redmutant", 30, 0.4f } // Custom boss
//};
//
//const boss_t BOSS_MEDIUM[] = {
//	{ horde::MonsterTypeID::BOSS2, "monster_boss2", 10, 1.0f },
//	{ horde::MonsterTypeID::CARRIER, "monster_carrier", 15, 1.0f },
//	{ horde::MonsterTypeID::WIDOW, "monster_widow", 20, 0.8f },
//	{ horde::MonsterTypeID::PSX_GUARDIAN, "monster_psxguardian", 25, 0.7f },
//	{ horde::MonsterTypeID::REDMUTANT, "monster_redmutant", 30, 0.6f },
//	{ horde::MonsterTypeID::TANK_64, "monster_tank64", 35, 0.5f }, // Custom boss
//	{ horde::MonsterTypeID::GUNCMDR_KL, "monster_guncmdrkl", 40, 0.4f } // Custom boss (Assuming ID is correct)
//};
//
//const boss_t BOSS_LARGE[] = {
//	{ horde::MonsterTypeID::BOSS2, "monster_boss2", 10, 1.0f },
//	{ horde::MonsterTypeID::CARRIER, "monster_carrier", 15, 1.0f },
//	{ horde::MonsterTypeID::WIDOW, "monster_widow", 20, 1.0f },
//	{ horde::MonsterTypeID::PSX_GUARDIAN, "monster_psxguardian", 25, 0.9f },
//	{ horde::MonsterTypeID::REDMUTANT, "monster_redmutant", 30, 0.8f },
//	{ horde::MonsterTypeID::TANK_64, "monster_tank64", 35, 0.7f },
//	{ horde::MonsterTypeID::GUNCMDR_KL, "monster_guncmdrkl", 40, 0.6f }, // Custom boss (Assuming ID is correct)
//	{ horde::MonsterTypeID::JORG, "monster_jorg", 45, 0.5f },
//	{ horde::MonsterTypeID::MAKRON_KL, "monster_makronkl", 50, 0.4f } // Custom boss (Assuming ID is correct)
//};
//
//
//// --- Function Implementations Moved from g_horde.cpp ---
//
//// --- Modified BossDeathHandler using item_id_t ---
//// Constants for item dropping physics (if not defined globally)
//constexpr int MIN_VELOCITY = -800;
//constexpr int MAX_VELOCITY = 800;
//constexpr int MIN_VERTICAL_VELOCITY = 400;
//constexpr int MAX_VERTICAL_VELOCITY = 950;
//
//void BossDeathHandler(edict_t* boss) {
//	if (!boss || !boss->inuse) return;
//
//	// Select weapon based on wave level
//	item_id_t weapon_id = SelectBossWeaponDrop(g_horde_local.level);
//	if (weapon_id != IT_NULL) {
//		gitem_t* weapon_item = GetItemByIndex(weapon_id);
//		if (weapon_item) {
//			edict_t* dropped_weapon = Drop_Item(boss, weapon_item);
//			if (dropped_weapon) {
//				// Apply random velocity
//				dropped_weapon->velocity[0] = irandom(MIN_VELOCITY, MAX_VELOCITY);
//				dropped_weapon->velocity[1] = irandom(MIN_VELOCITY, MAX_VELOCITY);
//				dropped_weapon->velocity[2] = irandom(MIN_VERTICAL_VELOCITY, MAX_VERTICAL_VELOCITY);
//				// Set think function for the dropped weapon if needed (e.g., timeout)
//				// dropped_weapon->think = Weapon_Think;
//				// dropped_weapon->nextthink = level.time + 30_sec; // Example timeout
//			}
//		}
//	}
//
//	// Drop power armor shard
//	gitem_t* power_armor_shard = FindItem("Power Armor Shard");
//	if (power_armor_shard) {
//		edict_t* dropped_shard = Drop_Item(boss, power_armor_shard);
//		if (dropped_shard) {
//			dropped_shard->velocity[0] = irandom(MIN_VELOCITY, MAX_VELOCITY);
//			dropped_shard->velocity[1] = irandom(MIN_VELOCITY, MAX_VELOCITY);
//			dropped_shard->velocity[2] = irandom(MIN_VERTICAL_VELOCITY, MAX_VERTICAL_VELOCITY);
//		}
//	}
//
//	// Additional logic from original boss_die can be added here if needed
//	// e.g., specific sound effects, visual effects, etc.
//}
//
//void boss_die(edict_t* boss) {
//	// Call the centralized handler
//	BossDeathHandler(boss);
//
//	// Original boss_die logic (if any specific parts need to remain)
//	// For example, if the original had specific target removal or other logic:
//	// boss->target = NULL; // Example
//	// BecomeNewTarget(boss); // Example
//
//	// Ensure the standard monster death function is called
//	// This handles things like becoming a target, removing from counts, etc.
//	// Note: If BossDeathHandler already calls G_FreeEdict or similar,
//	// ensure monster_death doesn't try to free it again.
//	// It might be better to integrate necessary parts of monster_death
//	// into BossDeathHandler or call specific parts.
//	// For now, assuming monster_death handles general cleanup:
//	// monster_death(boss, boss->enemy, boss->enemy, 100, vec3_origin); // Example call structure
//
//	// Standard monster death without double-freeing if BossDeathHandler handles it
//	if (boss->inuse) { // Check if still in use after BossDeathHandler potentially freed it
//		// Call appropriate death function or parts of it, avoiding G_FreeEdict if already done.
//		// This part needs careful integration based on how monster_death works.
//		// For now, let's assume BossDeathHandler doesn't free the edict, and we call a standard death.
//		// A simplified monster_death call might look like this:
//		boss->health = 0;
//		boss->deadflag = true; // Use boolean for Remastered
//		// monster_death_event(boss); // Hypothetical function for events without freeing
//		// gi.linkentity(boss); // Ensure state is updated
//	}
//}
//
//item_id_t SelectBossWeaponDrop(int32_t wave_level) { // Made non-static to match header
//	WeightedSelection<HordeItemInfo> selector; // Now defined above
//
//	// Iterate through the main item data
//	for (const auto& itemInfo : hordeItemData) { // Now defined above
//		// Check if it's a weapon and meets the minimum wave requirement
//		gitem_t* item = GetItemByIndex(itemInfo.id);
//		if (item && (item->flags & IF_WEAPON) && wave_level >= itemInfo.minWave) { // Correct flag is IF_WEAPON
//			// Adjust weight based on wave level (optional, example)
//			float adjusted_weight = itemInfo.weight;
//			if (wave_level > itemInfo.minWave + 5) {
//				adjusted_weight *= 1.2f; // Slightly more common later
//			}
//			selector.add(&itemInfo, adjusted_weight);
//		}
//	}
//
//	const HordeItemInfo* selected = selector.select();
//	return selected ? selected->id : IT_NULL; // Return IT_NULL if no weapon selected
//}
//
//
//// Helper to get the correct boss list based on map size
//// Returns a pair: {pointer to start of array, number of elements}
//// Reverted from std::span for C++17 compatibility due to compiler errors.
//static std::pair<const boss_t*, size_t> GetBossList(const horde::MapSize& mapSize, horde::MapID mapId) {
//	// Example: Special handling for a specific map
//	if (mapId == horde::MapID::Q2DM1) {
//		return { BOSS_SMALL, std::size(BOSS_SMALL) }; // Use small list for The Edge
//	} // <-- Added missing brace here
//
//	if (mapSize.isSmallMap) return { BOSS_SMALL, std::size(BOSS_SMALL) };
//	if (mapSize.isBigMap) return { BOSS_LARGE, std::size(BOSS_LARGE) };
//	return { BOSS_MEDIUM, std::size(BOSS_MEDIUM) }; // Default to medium
//}
//
//
//// Implementation of G_HordePickBOSSType
//horde::MonsterTypeID G_HordePickBOSSType(const horde::MapSize& mapSize, std::string_view mapname, int32_t waveNumber, edict_t* bossEntity) { // Made non-static to match header
//	horde::MapID currentMapId = horde::MapOriginRegistry::GetMapID(mapname.data()); // Use registry
//
//	// Check cache validity
//	if (g_bossEligibilityCache.cached_wave != waveNumber ||
//		g_bossEligibilityCache.cached_mapId != currentMapId ||
//		!(g_bossEligibilityCache.cached_mapSize.isSmallMap == mapSize.isSmallMap &&
//		  g_bossEligibilityCache.cached_mapSize.isBigMap == mapSize.isBigMap &&
//		  g_bossEligibilityCache.cached_mapSize.isMediumMap == mapSize.isMediumMap) ) // Compare MapSize members
//	{
//		// Cache is invalid, rebuild it
//		g_bossEligibilityCache.clear();
//		g_bossEligibilityCache.cached_wave = waveNumber;
//		g_bossEligibilityCache.cached_mapId = currentMapId;
//		g_bossEligibilityCache.cached_mapSize = mapSize; // Cache current map size
//
//		auto [boss_list_ptr, boss_list_size] = GetBossList(mapSize, currentMapId); // Get pointer and size
//
//		for (size_t i = 0; i < boss_list_size; ++i) { // Iterate using pointer and size
//			const auto& boss_def = boss_list_ptr[i]; // Access via pointer and index
//			if (waveNumber >= boss_def.minWave && !recent_bosses.contains(boss_def.typeId)) {
//				g_bossEligibilityCache.eligible_cache.add(&boss_def, boss_def.weight);
//			}
//		}
//		if (developer->integer) {
//			gi.Com_PrintFmt("Rebuilt boss eligibility cache for wave {}, map {}, size {}. Count: {}\n",
//				waveNumber, mapname, mapSize.isSmallMap ? "S" : (mapSize.isBigMap ? "L" : "M"),
//				g_bossEligibilityCache.eligible_cache.count);
//		}
//	}
//
//	// Select from the (potentially cached) list
//	const boss_t* selected_boss = g_bossEligibilityCache.eligible_cache.select();
//
//	if (selected_boss) {
//		recent_bosses.add(selected_boss->typeId);
//		if (developer->integer) {
//			gi.Com_PrintFmt("Selected boss: {} (TypeId: {})\n", selected_boss->classname, static_cast<int>(selected_boss->typeId));
//		}
//		return selected_boss->typeId;
//	}
//	else {
//		// Fallback if no eligible boss is found (e.g., all recently used)
//		recent_bosses.clear(); // Clear history to allow repeats
//		auto [boss_list_ptr, boss_list_size] = GetBossList(mapSize, currentMapId); // Get pointer and size again
//		if (boss_list_size > 0) {
//			// Simple fallback: pick the first available boss meeting minWave requirement
//			for (size_t i = 0; i < boss_list_size; ++i) { // Iterate using pointer and size
//				const auto& boss_def = boss_list_ptr[i]; // Access via pointer and index
//				if (waveNumber >= boss_def.minWave) {
//					recent_bosses.add(boss_def.typeId); // Add the fallback choice to history
//					if (developer->integer) {
//						// Get classname from registry for logging
//						const char* fallback_name = horde::MonsterTypeRegistry::GetClassname(boss_def.typeId);
//						gi.Com_PrintFmt("Fallback boss selection: {} (TypeId: {})\n",
//							fallback_name ? fallback_name : "Unknown", static_cast<int>(boss_def.typeId));
//					}
//					return boss_def.typeId;
//				}
//			}
//		}
//		// Ultimate fallback if list is empty or no boss meets minWave (shouldn't happen with valid data)
//		if (developer->integer) {
//			gi.Com_PrintFmt("CRITICAL FALLBACK: No boss found, defaulting to BOSS2\n");
//		}
//		return horde::MonsterTypeID::BOSS2;
//	}
//}
//
//// Implementation of AttachHealthBar
//void AttachHealthBar(edict_t* boss) { // Made non-static to match header
//	if (!boss || !boss->inuse || !boss->health)
//		return;
//
//	edict_t* orb = G_Spawn();
