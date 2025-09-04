#pragma once

// --- C++ Standard Library Includes ---
// It's good practice to include necessary headers here.
// This ensures that any file including g_horde.h gets what it needs.
#include <vector>

constexpr const char* HORDE_MOD_VERSION_STRING = "Horde BETA MOD v0.00991";
// --- Forward Declarations ---
// These tell the compiler that these types exist, without needing their full definition.
// This is crucial for preventing circular include dependencies.
struct MonsterTypeInfo; 
struct PlayerStats;

// --- Horde Mode Game Initialization and Management Functions ---
extern cvar_t* g_horde;
void Horde_PreInit();
void Horde_Init();
void Horde_RunFrame();
void ResetGame();
void HandleResetEvent();

void ResetSpawnMonsterVars();
void ResetQueueMonitorVars();

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

// --- Global Variable DECLARATIONS ---
// 'extern' tells other .cpp files that these variables exist and are defined elsewhere.
extern MonsterWaveType current_wave_type;
extern std::vector<const MonsterTypeInfo*> g_eligible_monsters_for_wave; // <<< THIS IS THE KEY ADDITION


// Boss types
enum class BossType {
	CARRIER, BOSS2, BOSS2KL, FIXBOTKL, CARRIER_MINI, TANK_64,
	SHAMBLERKL, GUNCMDRKL, GUARDIAN, PSX_GUARDIAN, BOSS5,
	WIDOW, WIDOW2, JORG, MAKRONKL, PSX_ARACHNID, REDMUTANT, OTHER
};

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


// Generic weighted selection template
template <typename T>
struct WeightedSelection {
	static constexpr size_t MAX_ITEMS = 32;

	struct ItemEntry {
		const T* item;
		float weight;
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