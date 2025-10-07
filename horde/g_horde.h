#pragma once

#include "horde_ids.h"  // For horde::MonsterTypeID

// --- C++ Standard Library Includes ---
// It's good practice to include necessary headers here.
// This ensures that any file including g_horde.h gets what it needs.
#include <vector>
#include <array>
#include <unordered_set>

constexpr const char* HORDE_MOD_VERSION_STRING = "Horde BETA MOD v0.00995.4";

extern std::vector<edict_t*> g_spawn_point_list;
extern size_t g_num_spawn_points;

// --- Forward Declarations ---
// These tell the compiler that these types exist, without needing their full definition.
// This is crucial for preventing circular include dependencies.
struct MonsterTypeInfo;  // Full definition in horde_monster_data.h
struct PlayerStats;

// --- Horde Mode Game Initialization and Management Functions ---
extern cvar_t* g_horde;
extern cvar_t* g_horde_grid_first;  // Test cvar: prioritize grid spawning
extern cvar_t* g_pvm;  // PvM mode (Player vs Monster with character persistence)
void Horde_PreInit();
void Horde_Init();
void Horde_RunFrame();
void ResetGame();
void HandleResetEvent();
const char* GetCurrentMapName();

void ResetSpawnMonsterVars();

bool IsMonsterTypePrecached(horde::MonsterTypeID typeId);
void UnlockModelFamilyMembers(horde::MonsterTypeID boss_typeId, int32_t current_wave);
void ResetQueueMonitorVars();

// Monster pack system save/load functions
void SaveMonsterPackState(int& current_pack_out, int pack_history_out[3], int& rotation_index_out);
void RestoreMonsterPackState(int current_pack_in, const int pack_history_in[3], int rotation_index_in);

[[nodiscard]] bool IsSpawnPointOccupied(const edict_t *spawn_point, const edict_t *ignore_ent = nullptr);
// Item selection in Horde mode
gitem_t* G_HordePickItem();

// Game mode checks
bool G_IsDeathmatch() noexcept;
bool G_IsCooperative() noexcept;

// HORDE CS
//extern void ClearHordeMessage();
extern void CleanupInvalidEntities();
extern void CleanupStuckEntities();

extern uint16_t g_totalMonstersInWave;
extern int32_t CalculateRemainingMonsters() noexcept; // Changed from inline to extern

// Forzar limpieza de cuerpos
extern void Horde_CleanBodies();
extern void ResetWaveAdvanceState() noexcept;

// Monster wave type flags
enum class MonsterWaveType : uint32_t {
	None = 0,
	Flying = 1 << 0,  // Flying units
	Ground = 1 << 1,  // Basic ground units
	Small = 1 << 2,  // Small units (parasite, stalker)
	Light = 1 << 3,  // Light units (soldiers, basic infantry)
	Heavy = 1 << 4,  // Heavy units (tanks, enforcers)
	Medium = 1 << 5,  // Medium units (gladiators, medics)
	Fast = 1 << 6,  // Fast moving units
	SemiBoss = 1 << 7,  // Mini-boss tier units
	Boss = 1 << 8,  // Full boss units
	Ranged = 1 << 9, // Primarily ranged attackers
	Melee = 1 << 10, // Primarily melee attackers
	Special = 1 << 11, // Special units (medics, commanders)
	Elite = 1 << 12,  // Elite variants of basic units
	Gekk = 1 << 13,  // Gekk initial wave?
	Shambler = 1 << 14,  // Shambler boss wave?
	Mutant = 1 << 15, // Mutant boss wave?
	Arachnophobic = 1 << 16,  // Mutant boss wave?
	Berserk = 1 << 17,  // Berserk  wave
	Bomber = 1 << 18,  // Grenade users wave?
	Spawner = 1 << 19  // Spawning reinforcements users wave
};

// Horde game state
enum class horde_state_t
{
	warmup,
	spawning,
	active_wave,
	cleanup,
	rest
};

// Warning times in seconds
constexpr std::array<float, 3> WARNING_TIMES = {30.0f, 10.0f, 5.0f};

// HordeState structure - main game state
struct HordeState
{
	gtime_t warm_time = 4_sec;
	horde_state_t state = horde_state_t::warmup;
	gtime_t monster_spawn_time;
	int32_t num_to_spawn = 0;
	int32_t level = 0;
	int32_t queued_monsters = 0;
	gtime_t lastPrintTime = 0_sec;

	bool conditionTriggered = false;
	gtime_t conditionStartTime = 0_sec;
	gtime_t conditionTimeThreshold = 0_sec;
	bool timeWarningIssued = false;
	gtime_t waveEndTime = 0_sec;
	bool warningIssued[WARNING_TIMES.size()] = {false};

	gtime_t failsafe_timeout = 0_sec;

	gtime_t last_successful_hud_update = 0_sec;
	uint32_t failed_updates_count = 0;

	horde::MapSize current_map_size;
	int32_t max_monsters{};
	gtime_t base_spawn_cooldown;

	// FIXED: Moved static state from Horde_RunFrame
	gtime_t last_wave_change_time = 0_sec;
	int32_t wave_at_last_check = 0;
	gtime_t spawning_phase_timeout_start = 0_sec;
	int32_t prev_wave_level_for_spawning_timers = -1;

	// Time acceleration for smooth wave ending
	float timeAcceleration = 1.0f;           // Current acceleration multiplier
	float targetTimeAcceleration = 1.0f;     // Target acceleration to interpolate toward
	gtime_t accelerationStartTime = 0_sec;   // When acceleration began
	gtime_t accelerationDuration = 2_sec;    // How long to interpolate

	void update_map_size(const char *mapname);
	void reset_hud_state();
};

extern HordeState g_horde_local;

// --- Global Variable DECLARATIONS ---
// 'extern' tells other .cpp files that these variables exist and are defined elsewhere.
extern MonsterWaveType current_wave_type;
extern std::vector<const MonsterTypeInfo*> g_eligible_monsters_for_wave;
extern std::vector<size_t> g_eligible_item_indices_for_wave;


// Boss types
enum class BossType {
	CARRIER, BOSS2, BOSS2KL, FIXBOTKL, CARRIER_MINI, TANK_64,
	SHAMBLERKL, GUNCMDRKL, GUARDIAN, PSX_GUARDIAN, BOSS5,
	WIDOW, WIDOW2, JORG, MAKRONKL, PSX_ARACHNID, REDMUTANT, OTHER
};

// --- Asset Family System for Efficient Precaching ---
// Groups monsters that share the same models/sounds to avoid duplicate precaching
enum class AssetFamilyID : uint8_t {
	SOLDIER_FAMILY,      // All soldier variants
	TANK_FAMILY,         // Tank, Tank Commander, Tank 64, etc.
	GLADIATOR_FAMILY,    // Gladiator A, B, C
	GUNNER_FAMILY,       // Gunner, Gunner Commander variants
	INFANTRY_FAMILY,     // Infantry, Infantry Vanilla
	ARACHNID_FAMILY,     // Spider, Arachnid, PSX Arachnid, GM Arachnid
	GUARDIAN_FAMILY,     // Guardian, PSX Guardian, Janitor2
	SUPERTANK_FAMILY,    // Janitor, Supertank (Boss5)
	FIXBOT_FAMILY,       // Fixbot, Fixbot KL
	TURRET_FAMILY,       // Sentrygun, Turret
	DAEDALUS_FAMILY,     // Daedalus, Daedalus Bomber
	PARASITE_FAMILY,     // Parasite, Perro KL
	BOSS2_FAMILY,        // Boss2, Boss2_64, Boss2_Mini, Boss2_KL
	CARRIER_FAMILY,      // Carrier, Carrier Mini
	WIDOW_FAMILY,        // Widow, Widow1, Widow2
	SHAMBLER_FAMILY,     // Shambler, Shambler Small, Shambler KL
	MAKRON_FAMILY,       // Makron, Makron KL, Jorg, Jorg Small
	// Individual families for unique monsters
	BERSERK_FAMILY,
	BRAIN_FAMILY,
	CHICK_FAMILY,
	FLYER_FAMILY,
	HOVER_FAMILY,
	MEDIC_FAMILY,
	MUTANT_FAMILY,
	FLOATER_FAMILY,
	STALKER_FAMILY,
	GEKK_FAMILY,
	INSANE_FAMILY,
	MISC_FAMILY,         // Misc entities like easter variants

	MAX_FAMILIES,
	UNKNOWN_FAMILY = 255
};

// Structure to manage asset precaching by family
struct MonsterAssetFamily {
	AssetFamilyID family_id;
	bool is_precached;
	const char* family_name;
	std::vector<const char*> models;
	std::vector<const char*> sounds;
	std::vector<horde::MonsterTypeID> members; // All monsters in this family

	// Dynamic unloading support
	int32_t reference_count;        // How many active monsters use this family
	int32_t last_wave_used;         // Last wave this family was used
	gtime_t last_access_time;        // Last time this family was accessed
	int32_t estimated_memory_size;  // Estimated memory usage in bytes
};

// Global asset tracking (defined in g_horde.cpp)
extern std::unordered_set<std::string> g_precached_models;
extern std::unordered_set<std::string> g_precached_sounds;
extern int32_t g_total_precached_models;
extern int32_t g_total_precached_sounds;
extern std::array<bool, 128> g_precached_monster_types_flags; // Precache flags for each monster type
extern std::unordered_set<horde::MonsterTypeID> g_precached_monsters_this_map; // Monster types precached this map
extern std::unordered_set<std::string> g_precached_models_this_map; // Models loaded this map

// --- Operator Overloads for MonsterWaveType ---
// These are fine to keep in the header as they are small, inline, and constexpr.
template<typename E>
constexpr auto to_underlying(E e) noexcept {
	static_assert(std::is_enum_v<E>, "to_underlying can only be used with enum types");
	return static_cast<std::underlying_type_t<E>>(e);
}

constexpr MonsterWaveType operator|(MonsterWaveType a, MonsterWaveType b) noexcept {
	return static_cast<MonsterWaveType>(to_underlying(a) | to_underlying(b));
}

constexpr MonsterWaveType operator&(MonsterWaveType a, MonsterWaveType b) noexcept {
	return static_cast<MonsterWaveType>(to_underlying(a) & to_underlying(b));
}

inline MonsterWaveType& operator|=(MonsterWaveType& a, MonsterWaveType b) noexcept {
	a = a | b;
	return a;
}

inline MonsterWaveType& operator&=(MonsterWaveType& a, MonsterWaveType b) noexcept {
	a = a & b;
	return a;
}

constexpr MonsterWaveType operator~(MonsterWaveType val) noexcept {
    using T = std::underlying_type_t<MonsterWaveType>;
    return static_cast<MonsterWaveType>(~static_cast<T>(val));
}

inline bool HasWaveType(MonsterWaveType entityTypes, MonsterWaveType typeToCheck) noexcept {
	return (entityTypes & typeToCheck) != MonsterWaveType::None;
}

// --- Horde Module Includes (after MonsterWaveType is defined) ---
#include "horde_monster_data.h"  // Monster type definitions and queries

// Generic weighted selection template
template <typename T>
struct WeightedSelection {
	static constexpr size_t MAX_ITEMS = 32;

	struct ItemEntry {
		const T* item = nullptr;
		float weight = 0.0f;
	};

	// Pre-allocate fixed array instead of using dynamic memory
	ItemEntry items[MAX_ITEMS];
	size_t item_count = 0;
	float total_weight = 0.0f;

	void clear() {
		item_count = 0;
		total_weight = 0.0f;
	}

	bool add(const T* item, float weight) {
		if (!item || item_count >= MAX_ITEMS || weight <= 0.0f)
			return false;

		items[item_count] = { item, weight };
		total_weight += weight;
		item_count++;
		return true;
	}

	const T* select() const {
		if (item_count == 0 || total_weight <= 0.0f)
			return nullptr;

		// Generate uniform random value once
		const float random_weight = frandom() * total_weight;

		// Use linear search - more efficient for small arrays than binary search
		float cumulative = 0.0f;
		for (size_t i = 0; i < item_count; i++) {
			cumulative += items[i].weight;
			if (cumulative >= random_weight)
				return items[i].item;
		}

		// Fallback to last item
		return items[item_count - 1].item;
	}

	// More efficient range selection
	const T* select_range(float min_weight, float max_weight) const {
		if (item_count == 0 || total_weight <= 0.0f)
			return nullptr;

		// Stack-allocated array for eligible items to avoid heap allocation
		ItemEntry eligible_items[MAX_ITEMS]{};
		size_t eligible_count = 0;
		float eligible_total = 0.0f;

		// Filter by weight range
		for (size_t i = 0; i < item_count; i++) {
			if (items[i].weight >= min_weight && items[i].weight <= max_weight) {
				eligible_items[eligible_count++] = items[i];
				eligible_total += items[i].weight;
			}
		}

		if (eligible_count == 0)
			return nullptr;

		// Generate random value once
		const float random_weight = frandom() * eligible_total;

		// Linear search through eligible items
		float cumulative = 0.0f;
		for (size_t i = 0; i < eligible_count; i++) {
			cumulative += eligible_items[i].weight;
			if (cumulative >= random_weight)
				return eligible_items[i].item;
		}

		return eligible_items[eligible_count - 1].item;
	}
};