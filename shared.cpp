#include "shared.h"
#include "horde/g_horde.h"
#include <unordered_map>
#include <algorithm>  // For std::max
#include <span>
#include "horde/horde_ids.h"
#include "horde/g_entity_properties.h"
#include <optional> // Required for the new cache
#include <vector>
#include <string>
#include <string_view>

// This function finds a random, unoccupied spawn point with high performance.
// It relies on a pre-shuffled global list (g_spawn_point_list) to avoid costly
// random lookups and leverages a thread-local index to ensure good distribution
// and cache-friendly sequential access across multiple calls.
[[nodiscard]] static edict_t* SelectRandomClearSpawnPoint() {
    // Early exit if the map has no spawn points.
    if (g_num_spawn_points == 0) {
        return nullptr;
    }

    // Use a thread-local static variable to remember the last checked index.
    // This ensures that each thread's spawn requests are distributed across the
    // available points and avoids contention on a single global index.
    thread_local size_t s_last_checked_index = 0;

    // To prevent an infinite loop on a fully occupied map, we'll check each
    // spawn point at most once.
    for (size_t i = 0; i < g_num_spawn_points; ++i) {
        // Cycle through the pre-shuffled list, wrapping around if necessary.
        const size_t check_index = (s_last_checked_index + i) % g_num_spawn_points;
        edict_t* spot = g_spawn_point_list[check_index];

        // Check if the spot is valid, in use, and not currently occupied by a player or object.
        // IsSpawnPointOccupied is assumed to be a fast, optimized check.
        if (spot && spot->inuse && !IsSpawnPointOccupied(spot)) {
            // Found a clear spot. Update the index for the next call to start from here.
            s_last_checked_index = check_index + 1;
            return spot;
        }
    }

    // FALLBACK: If no perfectly clear spot is found after checking all points,
    // it's better to return a potentially occupied one than to fail the spawn.
    // We'll just return the next spot in the shuffled sequence.
    s_last_checked_index = (s_last_checked_index + 1) % g_num_spawn_points;
    return g_spawn_point_list[s_last_checked_index];
}

#include <array>
#include <bit> // For std::countr_zero

// Data structure defining the effects and multipliers for a given monster bonus.
// This struct is designed for POD (Plain Old Data) efficiency.
struct BonusEffectData {
    effects_t effects = EF_NONE;
    renderfx_t renderfx = RF_NONE;
    float alpha = 1.0f;
    float health_mult = 1.0f;
    float power_armor_mult = 1.0f;
    gtime_t double_time_add = 0_sec;
    gtime_t quad_time_add = 0_sec;
    gtime_t invincible_time_add = 0_sec;
    monster_attack_state_t attack_state = AS_NONE;

    // Scaling multipliers per boss size
    float scale_small = 1.0f;
    float scale_medium = 1.0f;
    float scale_large = 1.0f;
    float health_mult_small = 1.0f;
    float health_mult_medium = 1.0f;
    float health_mult_large = 1.0f;
    float pa_mult_small = 1.0f;
    float pa_mult_medium = 1.0f;
    float pa_mult_large = 1.0f;
};

// A compile-time constant array holding all bonus effect data.
// Using a `constexpr std::array` ensures the data is baked into the program's
// data segment, allowing for extremely fast, direct memory lookups instead of
// slower runtime hash map lookups.
static constexpr std::array<BonusEffectData, 7> g_bonus_effects_array = {{
    // BF_NONE (index 0) - Default empty entry
    {},
    // BF_CHAMPION (index 1)
    {
        .effects = EF_ROCKET | EF_FIREBALL, .renderfx = RF_SHELL_RED,
        .double_time_add = 475_sec,
        .scale_small = 1.1f, .scale_medium = 1.25f, .scale_large = 1.5f,
        .health_mult_small = 1.13f, .health_mult_medium = 1.1f, .health_mult_large = 1.35f,
        .pa_mult_small = 1.1f, .pa_mult_medium = 1.08f, .pa_mult_large = 1.05f
    },
    // BF_CORRUPTED (index 2)
    {
        .effects = EF_PLASMA | EF_TAGTRAIL,
        .scale_small = 1.1f, .scale_medium = 1.2f, .scale_large = 1.2f,
        .health_mult_small = 1.24f, .health_mult_medium = 1.02f, .health_mult_large = 1.4f,
        .pa_mult_small = 1.04f, .pa_mult_medium = 1.15f, .pa_mult_large = 1.2f
    },
    // BF_RAGEQUITTER (index 3)
    {
        .effects = EF_BLUEHYPERBLASTER, .alpha = 0.6f,
        .power_armor_mult = 1.2f, .invincible_time_add = 12_sec
    },
    // BF_BERSERKING (index 4)
    {
        .effects = EF_GIB | EF_FLAG2, .health_mult = 0.92f, .power_armor_mult = 1.32f,
        .quad_time_add = 475_sec, .attack_state = AS_BLIND
    },
    // BF_POSSESSED (index 5)
    {
        .effects = EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE, .alpha = 0.6f,
        .health_mult = 1.2f, .power_armor_mult = 1.35f, .attack_state = AS_BLIND
    },
    // BF_STYGIAN (index 6)
    {
        .effects = EF_TRACKER | EF_FLAG1, .attack_state = AS_BLIND,
        .scale_small = 1.1f, .scale_medium = 1.3f, .scale_large = 1.2f,
        .health_mult_small = 1.2f, .health_mult_medium = 1.5f, .health_mult_large = 1.5f,
        .pa_mult_small = 1.1f, .pa_mult_medium = 1.1f, .pa_mult_large = 1.1f
    }
}};

// This function converts a single bonus flag (e.g., 1, 2, 4, 8) into a direct
// array index (0, 1, 2, 3). `constexpr` allows this to run at compile-time if the
// input is a constant, or as a very fast runtime function otherwise.
[[nodiscard]] constexpr size_t GetBonusEffectIndex(bonus_flags_t flags) noexcept {
    if (flags == BF_NONE) {
        return 0;
    }
    // Ensure only one bit is set for a valid lookup.
    if (std::popcount(static_cast<uint32_t>(flags)) != 1) {
        return 0; // Not a single flag, return default.
    }
    // `std::countr_zero` is a CPU instruction (like BSF) that finds the
    // index of the first set bit. It's the fastest way to convert a power-of-two
    // value to its corresponding exponent (e.g., 8 -> 3).
    return static_cast<size_t>(std::countr_zero(static_cast<uint32_t>(flags)));
}

[[nodiscard]] horde::MapSize GetMapSize(const char* mapname) noexcept {
    // The cache is a static array of optional values. `std::optional` is used
    // to distinguish between a cached value and a cache miss.
    static std::array<std::optional<horde::MapSize>,
                      static_cast<size_t>(horde::MapID::MAX_MAPS)> s_cache;

    // Static variable to track the last map name to know when to invalidate the cache.
    static char s_last_map_for_cache[MAX_QPATH] = "";

    // If the map has changed since the last call, clear the entire cache.
    if (strcmp(s_last_map_for_cache, level.mapname) != 0) {
        s_cache.fill(std::nullopt);
        Q_strlcpy(s_last_map_for_cache, level.mapname, sizeof(s_last_map_for_cache));
    }

    // Convert the map name to a unique, integer-based ID.
    const horde::MapID mapId = horde::MapOriginRegistry::GetMapID(mapname);
    if (mapId == horde::MapID::UNKNOWN) {
        return {}; // Return a default-constructed (e.g., medium) map size.
    }

    const size_t index = static_cast<size_t>(mapId);

    // FAST PATH: If the cache already has a value for this map ID, return it directly.
    if (s_cache[index].has_value()) {
        return s_cache[index].value();
    }

    // SLOW PATH (CACHE MISS): The size is not in the cache.
    // 1. Look up the size from the slower, central registry.
    const horde::MapSize size = horde::MapOriginRegistry::GetMapSize(mapId);

    // 2. Store the retrieved size in our fast array cache for future lookups.
    s_cache[index] = size;

    // 3. Return the size.
    return size;
}

[[nodiscard]] bool IsRemovableEntity(const edict_t* ent) noexcept;

void RemoveEntity(edict_t* ent);

void RemovePlayerOwnedEntities(edict_t* player) {
    if (!player || !player->client) {
        return;
    }

    // OPTIMIZATION: Use a small, fast, stack-allocated array for the common case
    // where a player has few deployed entities. This avoids heap allocation overhead.
    constexpr size_t STACK_CAPACITY = 32;
    edict_t* stack_entities[STACK_CAPACITY];

    // A heap-allocated vector is used as a fallback for the rare case where
    // a player has more entities than the stack buffer can hold.
    std::vector<edict_t*> heap_entities;

    edict_t** collection_ptr = stack_entities;
    size_t entity_count = 0;
    size_t current_capacity = STACK_CAPACITY;

    // Lambda to safely add an entity to the current collection (stack or heap).
    auto add_entity_to_collection = [&](edict_t* ent) {
        if (ent && ent->inuse) {
            // If the current collection is full...
            if (entity_count >= current_capacity) {
                // ...and we are currently using the stack buffer...
                if (collection_ptr == stack_entities) {
                    // ...promote to using the heap vector.
                    heap_entities.reserve(current_capacity * 2);
                    // Copy existing pointers from stack to heap.
                    std::copy(stack_entities, stack_entities + entity_count, std::back_inserter(heap_entities));
                    collection_ptr = heap_entities.data();
                    current_capacity = heap_entities.capacity();
                } else {
                    // If we are already on the heap, just grow it.
                    current_capacity *= 2;
                    heap_entities.reserve(current_capacity);
                    collection_ptr = heap_entities.data();
                }
            }
            collection_ptr[entity_count++] = ent;
        }
    };

    // --- PASS 1: COLLECT ENTITIES ---
    // Use std::span for safe, modern iteration over the C-style arrays.
    const auto& client = *player->client;
    for (edict_t* laser : std::span{client.resp.deployed_lasers}) add_entity_to_collection(laser);
    for (edict_t* sentry : std::span{client.resp.deployed_sentries}) add_entity_to_collection(sentry);
    for (edict_t* tesla : std::span{client.resp.deployed_teslas}) add_entity_to_collection(tesla);
    for (edict_t* trap : std::span{client.resp.deployed_traps}) add_entity_to_collection(trap);
    for (edict_t* prox : std::span{client.resp.deployed_proxs}) add_entity_to_collection(prox);

    // --- PASS 2: REMOVE COLLECTED ENTITIES ---
    // This batch removal is more cache-friendly and safer than interleaved removal.
    for (size_t i = 0; i < entity_count; ++i) {
        edict_t* ent_to_remove = collection_ptr[i];
        // Re-check inuse status, as one entity's death might have affected another.
        if (ent_to_remove && ent_to_remove->inuse) {
            RemoveEntity(ent_to_remove); // Assumes RemoveEntity handles the correct die() function.
        }
    }
}

// --- Refactored Functions ---

bool IsPlayerDefense(const edict_t* ent) {
    if (!ent) return false;
    auto id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);
    if (id == horde::SpecialEntityTypeID::UNKNOWN) return false;
    return IsDefense(id);
}

#include <unordered_set> // Add this include at the top of your file

[[nodiscard]] bool IsRemovableEntity(const edict_t* ent) noexcept {
    if (!ent) return false;

    const auto id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);

    // DEBUG/VALIDATION PATH: This block only runs when `developer` is enabled,
    // incurring zero performance cost in release builds. It helps developers find
    // entities that are missing their required `special_type_id`.
    if (id == horde::SpecialEntityTypeID::UNKNOWN) {
        if (developer->integer > 0) {
            // Use thread-local statics to avoid repeated warnings for the same classname per map.
            thread_local std::unordered_set<std::string_view> s_reported_classnames;
            thread_local char s_last_map_for_reporting[MAX_QPATH] = "";

            // Clear the warning cache on map change.
            if (strcmp(s_last_map_for_reporting, level.mapname) != 0) {
                s_reported_classnames.clear();
                Q_strlcpy(s_last_map_for_reporting, level.mapname, sizeof(s_last_map_for_reporting));
            }

            if (ent->classname && horde::SpecialTypeRegistry::GetTypeID(ent->classname) != horde::SpecialEntityTypeID::UNKNOWN) {
                if (s_reported_classnames.find(ent->classname) == s_reported_classnames.end()) {
                    gi.Com_PrintFmt("VALIDATION WARNING: Entity '{}' is missing its special_type_id. Set it in its spawn function.\n", ent->classname);
                    s_reported_classnames.insert(ent->classname);
                }
            }
        }
        return false;
    }

    // FAST PATH: Use the bitmask for the actual check.
    static constexpr uint64_t REMOVABLE_MASK =
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::TESLA_MINE)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::FOOD_CUBE_TRAP)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::LASER_EMITTER)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::DOPPLEGANGER));

    return (REMOVABLE_MASK & (1ULL << static_cast<uint64_t>(id))) != 0;
}


void RemoveEntity(edict_t* ent) {
    if (!ent || !ent->inuse) return;

    const auto id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);

    // If the entity has a registered type, look up its specific death handler.
    if (id != horde::SpecialEntityTypeID::UNKNOWN) {
        // Assumes GetDieHandler(id) performs a fast lookup (e.g., array access).
        EntityDieHandler handler = GetDieHandler(id);
        if (handler) {
            // Call the specific `die` function for this entity type.
            handler(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
            return;
        }
    }

    // Fallback for entities with no special handler.
    BecomeExplosion1(ent);
}

void UpdatePowerUpTimes(edict_t* monster) noexcept {
    if (!monster) return;

    // Batch time comparisons at the start of the function. This can help the CPU's
    // branch predictor, as the conditions are evaluated together.
    const gtime_t current_time = level.time;
    const bool quad_expired = monster->monsterinfo.quad_time <= current_time;
    const bool double_expired = monster->monsterinfo.double_time <= current_time;

    // The most common case is that both power-ups have expired. This single check
    // handles that efficiently.
    if (quad_expired && double_expired) {
        monster->monsterinfo.damage_modifier_applied = false;
    }
}

[[nodiscard]] float M_DamageModifier(edict_t* monster) noexcept {
    if (!monster) return 1.0f;

    const gtime_t current_time = level.time;

    // Special case for sentry guns, which receive reduced benefits from power-ups.
    if (horde::IsMonsterType(monster, horde::MonsterTypeID::SENTRYGUN)) {
        if (monster->monsterinfo.quad_time > current_time) return 2.0f;
        if (monster->monsterinfo.double_time > current_time) return 1.5f;
        return 1.0f;
    }

    // Standard logic for all other monsters.
    // Check for the most powerful effect first (Quad Damage) for an early exit.
    if (monster->monsterinfo.quad_time > current_time) return 4.0f;
    if (monster->monsterinfo.double_time > current_time) return 2.0f;

    // Default multiplier if no power-ups are active.
    return 1.0f;
}

// --- Part 1: Title Generation from Flags ---

// Generates a title prefix (e.g., "Champion ", "Friendly ") from bonus flags.
// It uses a fast path for common, single flags and combinations, avoiding
// expensive string concatenation at runtime for the majority of cases.
// Bring the string_view literal operators (like 'sv') into the current scope.
using namespace std::literals::string_view_literals;

// Generates a title prefix (e.g., "Champion ", "Friendly ") from bonus flags.
// It uses a fast path for common, single flags and combinations, avoiding
// expensive string concatenation at runtime for the majority of cases.
[[nodiscard]] const char* GetTitleFromFlags(bonus_flags_t bonus_flags) noexcept {
    if (bonus_flags == BF_NONE) {
        return ""; // Fast path for the most common case.
    }

    // A compile-time lookup table for common single flags and combinations.
    static constexpr struct {
        bonus_flags_t flags;
        const char* title;
    } COMMON_TITLES[] = {
        {BF_CHAMPION, "Champion "},
        {BF_CORRUPTED, "Corrupted "},
        {BF_RAGEQUITTER, "Ragequitter "},
        {BF_BERSERKING, "Berserking "},
        {BF_POSSESSED, "Possessed "},
        {BF_STYGIAN, "Stygian "},
        {BF_FRIENDLY, "Friendly "},
        {BF_CHAMPION | BF_CORRUPTED, "Champion Corrupted "},
        // Add more common combinations here for further optimization.
    };

    // Fast path lookup.
    for (const auto& entry : COMMON_TITLES) {
        if (entry.flags == bonus_flags) {
            return entry.title;
        }
    }

    // Fallback path for rare, complex combinations.
    // A thread-local buffer is used to avoid heap allocations and thread-safety issues.
    thread_local char title_buffer[64];
    char* ptr = title_buffer;
    const char* const end = title_buffer + sizeof(title_buffer);

    // Helper lambda for safe string appending.
    auto append_title = [&](std::string_view text) {
        if (ptr + text.length() < end) {
            memcpy(ptr, text.data(), text.length());
            ptr += text.length();
        }
    };

    if (bonus_flags & BF_CHAMPION)   append_title("Champion "sv);
    if (bonus_flags & BF_CORRUPTED)  append_title("Corrupted "sv);
    if (bonus_flags & BF_RAGEQUITTER) append_title("Ragequitter "sv);
    if (bonus_flags & BF_BERSERKING) append_title("Berserking "sv);
    if (bonus_flags & BF_POSSESSED)  append_title("Possessed "sv);
    if (bonus_flags & BF_STYGIAN)    append_title("Stygian "sv);
    if (bonus_flags & BF_FRIENDLY)   append_title("Friendly "sv);

    *ptr = '\0'; // Null-terminate the buffer.
    return title_buffer;
}

// --- Part 2: Pre-computation and Final Name Generation ---

// Static caches to hold the pre-formatted base names for all entity types.
static std::array<std::string, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_monsterDisplayNames;
static std::array<std::string, static_cast<size_t>(horde::SpecialEntityTypeID::COUNT)> g_specialDisplayNames;
static bool g_displayNamesInitialized = false;

// Forward declaration for a helper function.
std::string FormatClassname(const std::string& classname);

// This function runs once to populate the display name caches.
// It should be called during game or level initialization.
void InitializeDisplayNames() {
    if (g_displayNamesInitialized) return;

    // Pre-allocate memory to avoid reallocations during population.
    for (auto& name : g_monsterDisplayNames) name.reserve(32);
    for (auto& name : g_specialDisplayNames) name.reserve(24);

    // Initialize monster names.
    for (size_t i = 0; i < static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES); ++i) {
        auto typeId = static_cast<horde::MonsterTypeID>(i);
        auto it = monster_name_replacements.find(typeId);
        if (it != monster_name_replacements.end()) {
            g_monsterDisplayNames[i] = it->second;
        } else if (horde::MonsterTypeRegistry::IsValidType(typeId)) {
            g_monsterDisplayNames[i] = FormatClassname(horde::MonsterTypeRegistry::GetClassname(typeId));
        } else {
            g_monsterDisplayNames[i] = "Unknown Monster";
        }
    }

    // Initialize special entity names using a compile-time map for efficiency.
    static constexpr std::pair<horde::SpecialEntityTypeID, const char*> special_mappings[] = {
        {horde::SpecialEntityTypeID::TESLA_MINE, "Tesla Mine"},
        {horde::SpecialEntityTypeID::FOOD_CUBE_TRAP, "Stroggonoff Maker"},
        {horde::SpecialEntityTypeID::TURRET, "Turret"},
        {horde::SpecialEntityTypeID::SENTRY_GUN, "Sentry Gun"},
        {horde::SpecialEntityTypeID::LASER_EMITTER, "Laser Emitter"},
        {horde::SpecialEntityTypeID::DOPPLEGANGER, "Doppleganger"},
    };

    for (auto& name : g_specialDisplayNames) name = "Unknown Object"; // Default value.
    for (const auto& [typeId, name] : special_mappings) {
        g_specialDisplayNames[static_cast<size_t>(typeId)] = name;
    }

    g_displayNamesInitialized = true;
}


// Retrieves the full, formatted display name for an entity.
// This function is extremely fast as it primarily combines pre-cached strings.
[[nodiscard]] const char* GetDisplayName(const edict_t* ent) {
    if (!ent) return "Unknown";

    // Ensure the caches are populated.
    if (!g_displayNamesInitialized) {
        InitializeDisplayNames();
    }

    const char* base_name_ptr = "Unknown";
    const auto special_id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);
    const auto monster_id = static_cast<horde::MonsterTypeID>(ent->monsterinfo.monster_type_id);

    // Look up the base name from the appropriate cache.
    if (special_id != horde::SpecialEntityTypeID::UNKNOWN) {
        base_name_ptr = g_specialDisplayNames[static_cast<size_t>(special_id)].c_str();
    } else if (ent->svflags & SVF_MONSTER && monster_id != horde::MonsterTypeID::UNKNOWN) {
        base_name_ptr = g_monsterDisplayNames[static_cast<size_t>(monster_id)].c_str();
    } else if (ent->classname) {
        base_name_ptr = ent->classname;
    }

    // Get the title prefix (e.g., "Champion ").
    const char* title_ptr = GetTitleFromFlags(ent->monsterinfo.bonus_flags);

    // Use the engine's non-allocating string formatter (`G_Fmt`) for optimal performance.
    // This combines the title and base name into a static buffer without any heap allocations.
    return G_Fmt("{}{}", title_ptr, base_name_ptr).data();
}

void ApplyMonsterBonusFlags(edict_t* monster) {
    if (!monster || !monster->inuse || monster->monsterinfo.IS_BOSS) {
        return;
    }

    const spawn_temp_t& st = ED_GetSpawnTemp();

    // Grant default power armor to non-friendly monsters with any bonus.
    if (monster->monsterinfo.bonus_flags != BF_NONE && !(monster->monsterinfo.bonus_flags & BF_FRIENDLY)) {
        if (!st.was_key_specified("power_armor_power")) {
            monster->monsterinfo.power_armor_power = static_cast<int>(round(monster->max_health * 0.4f));
        }
        if (!st.was_key_specified("power_armor_type")) {
            monster->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
        }
    }

    // Handle summoned monster logic.
    if (monster->monsterinfo.issummoned) {
        monster->monsterinfo.bonus_flags |= BF_FRIENDLY;
        FindMTarget(monster);
        monster->svflags |= SVF_PLAYER;
        monster->s.renderfx &= ~RF_DOT_SHADOW;
        // Fast team assignment based on the owner's team.
        monster->monsterinfo.team = (monster->owner && monster->owner->client)
            ? monster->owner->client->resp.ctf_team
            : CTF_NOTEAM;
    }

    // Apply universal monster adjustments.
    monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;
    monster->gib_health = std::max(-200, static_cast<int>(round(monster->gib_health * 2.8f)));

    // --- CORE OPTIMIZATION ---
    // Convert the bonus flag into a direct array index.
    const size_t effect_index = GetBonusEffectIndex(monster->monsterinfo.bonus_flags);

    // If the index is valid (i.e., not the "NONE" flag), apply all effects from the data table.
    if (effect_index > 0 && effect_index < g_bonus_effects_array.size()) {
        const BonusEffectData& data = g_bonus_effects_array[effect_index];

        monster->s.effects |= data.effects;
        monster->s.renderfx |= data.renderfx;
        if (data.alpha != 1.0f) monster->s.alpha = data.alpha;

        monster->health = static_cast<int>(round(monster->health * data.health_mult));
        monster->monsterinfo.power_armor_power = static_cast<int>(round(monster->monsterinfo.power_armor_power * data.power_armor_mult));
        monster->monsterinfo.armor_power = static_cast<int>(round(monster->monsterinfo.armor_power * data.power_armor_mult));

        if (data.double_time_add > 0_sec)
            monster->monsterinfo.double_time = std::max(level.time, monster->monsterinfo.double_time) + data.double_time_add;
        if (data.quad_time_add > 0_sec)
            monster->monsterinfo.quad_time = std::max(level.time, monster->monsterinfo.quad_time) + data.quad_time_add;
        if (data.invincible_time_add > 0_sec)
            monster->monsterinfo.invincible_time = std::max(level.time, monster->monsterinfo.invincible_time) + data.invincible_time_add;
        if (data.attack_state != AS_NONE)
            monster->monsterinfo.attack_state = data.attack_state;
    }

    // Finalize monster state and link it into the world.
    monster->max_health = monster->health;
    monster->s.renderfx |= RF_IR_VISIBLE;
    gi.linkentity(monster);
}

// A compile-time lookup table (LUT) defining the base stats for bosses at different wave thresholds.
// This data-driven approach is faster and much easier to maintain and balance than a hardcoded if-else chain.
static constexpr struct {
    int wave_threshold;
    int health_base;
    int power_armor_base;
} BOSS_TIER_DATA[] = {
    {25, 16500, 9500},   // Max tier (Wave 25+)
    {20, 11250, 6750},   // High tier (Wave 20-24)
    {15, 9300,  3400},   // Mid-high tier (Wave 15-19)
    {10, 6000,  4100},   // Mid tier (Wave 10-14)
    {5,  5600,  4100},   // Low-mid tier (Wave 5-9)
    {0,  3750,  1125}    // Base tier (Wave 0-4)
};

// Calculates the minimum required health and power armor for a boss based on the current wave.
// It uses the `BOSS_TIER_DATA` lookup table for a fast, data-driven calculation.
static void CalculateBossMinimums(int wave_number, int& health_min, int& power_armor_min) noexcept
{
    const auto& tier = [&]() -> const auto& {
        // For a small, sorted array like this, a simple linear scan is often faster
        // than a binary search due to avoiding branch mispredictions.
        for (const auto& tier_data : BOSS_TIER_DATA) {
            if (wave_number >= tier_data.wave_threshold) {
                return tier_data;
            }
        }
        return BOSS_TIER_DATA[std::size(BOSS_TIER_DATA) - 1]; // Fallback to the last entry.
    }();

    // Introduce slight randomness to prevent boss stats from being completely predictable.
    const float random_multiplier = frandom(0.85f, 1.15f);

    // Calculate the final minimums, ensuring they don't exceed a reasonable cap
    // in case of a high random roll.
    health_min = std::min(static_cast<int>(tier.health_base * random_multiplier), tier.health_base + 1000);
    power_armor_min = std::min(static_cast<int>(tier.power_armor_base * random_multiplier), tier.power_armor_base + 500);
}

// A constant factor for calculating regular armor based on power armor.
constexpr float REGULAR_ARMOR_FACTOR = 0.75f;

// Configures a boss's armor values, ensuring they meet the minimum requirements for the current wave.
void ConfigureBossArmor(edict_t* self) {
    if (!self || !self->inuse || !self->monsterinfo.IS_BOSS) {
        return;
    }

    // Get the calculated minimums for the current wave.
    int health_min, power_armor_min;
    CalculateBossMinimums(current_wave_level, health_min, power_armor_min);

    // Ensure the boss's power armor is at least the minimum required value.
    if (self->monsterinfo.power_armor_power > 0) {
        self->monsterinfo.power_armor_power = std::max(
            self->monsterinfo.power_armor_power, power_armor_min);
    }

    // Ensure the boss's regular armor is at least the minimum required value.
    if (self->monsterinfo.armor_power > 0) {
        const int min_regular_armor = static_cast<int>(power_armor_min * REGULAR_ARMOR_FACTOR);
        self->monsterinfo.armor_power = std::max(
            self->monsterinfo.armor_power, min_regular_armor);
    }
}

void ApplyBossEffects(edict_t* boss)
{
	if (!boss->monsterinfo.IS_BOSS || boss->monsterinfo.effects_applied)
		return;

	const BossSizeCategory sizeCategory = boss->bossSizeCategory;
	const int32_t random_flag_int = 1 << irandom(6);
	boss->monsterinfo.bonus_flags = static_cast<bonus_flags_t>(random_flag_int);

	float health_multiplier = 1.0f;
	float power_armor_multiplier = 1.0f;
	float scale_factor = 1.0f;

    // --- CORRECTED LOGIC ---
    // Use the fast, array-based lookup instead of the old map.
    const size_t effect_index = GetBonusEffectIndex(boss->monsterinfo.bonus_flags);
    if (effect_index > 0 && effect_index < g_bonus_effects_array.size()) {
        const BonusEffectData& data = g_bonus_effects_array[effect_index];

		boss->s.effects |= data.effects;
		boss->s.renderfx |= data.renderfx;
		boss->s.alpha = data.alpha;
		boss->monsterinfo.attack_state = data.attack_state;

		if (data.double_time_add > 0_sec)
			boss->monsterinfo.double_time = std::max(level.time, boss->monsterinfo.double_time) + data.double_time_add;
		if (data.quad_time_add > 0_sec)
			boss->monsterinfo.quad_time = std::max(level.time, boss->monsterinfo.quad_time) + data.quad_time_add;
		if (data.invincible_time_add > 0_sec)
			boss->monsterinfo.invincible_time = std::max(level.time, boss->monsterinfo.invincible_time) + data.invincible_time_add;

		health_multiplier *= data.health_mult;
		power_armor_multiplier *= data.power_armor_mult;

		switch (sizeCategory) {
		case BossSizeCategory::Small:
			scale_factor = data.scale_small;
			health_multiplier *= data.health_mult_small;
			power_armor_multiplier *= data.pa_mult_small;
			break;
		case BossSizeCategory::Medium:
			scale_factor = data.scale_medium;
			health_multiplier *= data.health_mult_medium;
			power_armor_multiplier *= data.pa_mult_medium;
			break;
		case BossSizeCategory::Large:
			scale_factor = data.scale_large;
			health_multiplier *= data.health_mult_large;
			power_armor_multiplier *= data.pa_mult_large;
			break;
		}
	}

	if (scale_factor != 1.0f) {
		boss->s.scale *= scale_factor;
		boss->mass *= scale_factor;
		float height_offset = -(boss->mins[2]);
		boss->s.origin[2] += height_offset;
		gi.linkentity(boss);
	}

	int health_min, power_armor_min;
	CalculateBossMinimums(current_wave_level, health_min, power_armor_min);

	boss->health = std::max(static_cast<int>(boss->health * health_multiplier), health_min);
	boss->max_health = boss->health;

	if (boss->monsterinfo.power_armor_power > 0) {
		boss->monsterinfo.power_armor_power = std::max(
			static_cast<int>(boss->monsterinfo.power_armor_power * power_armor_multiplier),
			power_armor_min
		);
	}

	if (boss->monsterinfo.armor_power > 0) {
		const int min_regular_armor = static_cast<int>(power_armor_min * REGULAR_ARMOR_FACTOR);
		boss->monsterinfo.armor_power = std::max(
			static_cast<int>(boss->monsterinfo.armor_power * power_armor_multiplier),
			min_regular_armor
		);
	}

	switch (sizeCategory) {
	case BossSizeCategory::Small:
		boss->health = static_cast<int>(boss->health * 0.8f);
		if (boss->monsterinfo.power_armor_power > 0)
			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * 0.9f);
		if (boss->monsterinfo.armor_power > 0)
			boss->monsterinfo.armor_power = static_cast<int>(boss->monsterinfo.armor_power * 0.9f);
		break;
	case BossSizeCategory::Large:
		boss->health = static_cast<int>(boss->health * 1.2f);
		if (boss->monsterinfo.power_armor_power > 0)
			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * 1.2f);
		if (boss->monsterinfo.armor_power > 0)
			boss->monsterinfo.armor_power = static_cast<int>(boss->monsterinfo.armor_power * 1.2f);
		break;
	case BossSizeCategory::Medium:
		break;
	}

	boss->health = std::max(boss->health, health_min);
	if (boss->monsterinfo.power_armor_power > 0)
		boss->monsterinfo.power_armor_power = std::max(boss->monsterinfo.power_armor_power, power_armor_min);
	if (boss->monsterinfo.armor_power > 0) {
		const int min_regular_armor = static_cast<int>(power_armor_min * REGULAR_ARMOR_FACTOR);
		boss->monsterinfo.armor_power = std::max(boss->monsterinfo.armor_power, min_regular_armor);
	}

	boss->max_health = boss->health;
	boss->monsterinfo.effects_applied = true;
}


//getting real name
const char* GetPlayerName(const edict_t* player) {
    static char name_buffer[MAX_INFO_VALUE];

    if (!player || !player->client) {
        return "N/A";
    }

    size_t written = gi.Info_ValueForKey(player->client->pers.userinfo, "name",
        name_buffer, sizeof(name_buffer) - 1);

    if (written == 0 || written >= sizeof(name_buffer)) {
        return "N/A";
    }

    name_buffer[written] = '\0';
    return name_buffer;
}

extern void SP_target_earthquake(edict_t* self);
extern constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_SILENT = 1_spawnflag;
extern constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_ONE_SHOT = 8_spawnflag;

void ImprovedSpawnGrow(const vec3_t& position, float start_size, float end_size, edict_t* spawned_entity)
{
	// Constantes para mejor legibilidad y mantenimiento
	constexpr int   NUM_SECONDARY_EFFECTS = 12;
	constexpr float RANDOM_OFFSET_RANGE = 255.0f;
	constexpr float EFFECT_SCALE = 0.55f;
	constexpr float EARTHQUAKE_SPEED = 500.0f;
	constexpr int   EARTHQUAKE_DURATION = 12;

	// Crear el efecto principal de spawn
	SpawnGrow_Spawn(position, start_size, end_size);

	// Si no es una entidad jefe, terminamos aquí
	if (!spawned_entity || !spawned_entity->monsterinfo.IS_BOSS) {
		return;
	}

	// Efectos adicionales para spawn de jefes
	for (int i = 0; i < NUM_SECONDARY_EFFECTS; i++)
	{
		vec3_t offset{};

		// Calcular posición aleatoria para cada efecto secundario
		for (int j = 0; j < 3; j++) {
			offset[j] = position[j] + crandom() * RANDOM_OFFSET_RANGE;
		}

		// Crear efecto secundario escalado
		SpawnGrow_Spawn(offset,
			start_size * EFFECT_SCALE,
			end_size * EFFECT_SCALE);
	}

	// Crear y configurar el efecto de terremoto
	edict_t* earthquake = G_Spawn();
	if (earthquake)
	{
		// Configurar parámetros del terremoto
		earthquake->classname = "target_earthquake";
		earthquake->spawnflags = brandom()
			? SPAWNFLAGS_EARTHQUAKE_SILENT
			: SPAWNFLAGS_EARTHQUAKE_ONE_SHOT;
		earthquake->speed = EARTHQUAKE_SPEED;
		earthquake->count = EARTHQUAKE_DURATION;

		// Inicializar y activar el terremoto
		SP_target_earthquake(earthquake);
		earthquake->use(earthquake, spawned_entity, spawned_entity);
	}
}

// Optimized clear path check using modern vector operations
bool G_IsClearPath(const edict_t* ignore, contents_t mask, const vec3_t& spot1, const vec3_t& spot2) {
	// Early out if either vector is invalid
	if (!is_valid_vector(spot1) || !is_valid_vector(spot2))
		return false;
		
	// Use direct traceline call
	const trace_t tr = gi.traceline(spot1, spot2, ignore, mask);
	return (tr.fraction == 1.0f);
}

void TeleportEntity(edict_t* ent, edict_t* dest) {
	if (!ent || !ent->inuse || !dest || !dest->inuse)
		return;
	
	// Early-out if vectors are invalid
	if (!is_valid_vector(dest->s.origin))
		return;

	// Store original position for effect
	//const vec3_t old_origin = ent->s.origin;

	// Teleport effect at source
	// gi.WriteByte(svc_temp_entity);
	// gi.WriteByte(TE_TELEPORT_EFFECT);
	// gi.WritePosition(old_origin);
	// gi.multicast(old_origin, MULTICAST_PVS, false);

	// Hide entity during teleport
	ent->svflags |= SVF_NOCLIENT;
	gi.unlinkentity(ent);

	// Move entity using vec3_t operations
	ent->s.origin = dest->s.origin;
	ent->s.origin.z += 10; // Slight elevation to prevent clipping

	// Reset velocities using vec3_t assignments
	ent->velocity = vec3_origin;

	if (ent->client) {
		ent->client->ps.pmove.pm_time = 160;
		ent->client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;
		ent->s.event = EV_PLAYER_TELEPORT;

		// Direct vec3_t angle assignments
		ent->client->ps.viewangles = dest->s.angles;
		ent->client->v_angle = dest->s.angles;
		ent->s.angles = dest->s.angles;

		// Convert to pmove origin format using vec3_t
		for (int i = 0; i < 3; i++) {
			ent->client->ps.pmove.origin[i] = static_cast<int16_t>(ent->s.origin[i] * 8.0f);
			float angle_diff = anglemod(dest->s.angles[i] - ent->client->resp.cmd_angles[i]);
			ent->client->ps.pmove.delta_angles[i] = angle_diff;
		}
		ent->client->ps.pmove.velocity = vec3_origin;
	}
	else {
		ent->s.angles = dest->s.angles;
	}

	// Make entity visible again
	ent->svflags &= ~SVF_NOCLIENT;
	gi.linkentity(ent);

	// Prevent telefrag
	KillBox(ent, false, MOD_TELEFRAG, true);

	// Teleport effect at destination
	// gi.WriteByte(svc_temp_entity);
	// gi.WriteByte(TE_TELEPORT_EFFECT);
	// gi.WritePosition(ent->s.origin);
	// gi.multicast(ent->s.origin, MULTICAST_PVS, false);
}

//constexpr spawnflags_t SPAWNFLAG_LAVABALL_NO_EXPLODE = 1_spawnflag;
void fire_touch(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self);
edict_t* SelectSingleSpawnPoint(edict_t* ent);

bool EntitiesOverlap(const edict_t* ent, const vec3_t& area_mins, const vec3_t& area_maxs) {
	if (!ent)
		return false;

	// Calculate entity bounds
	const vec3_t ent_mins = ent->s.origin + ent->mins;
	const vec3_t ent_maxs = ent->s.origin + ent->maxs;

	// Use the existing boxes_intersect function directly
	return boxes_intersect(ent_mins, ent_maxs, area_mins, area_maxs);
}

void ClearSpawnArea(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs) {
	if (!is_valid_vector(origin) || !is_valid_vector(mins) || !is_valid_vector(maxs))
		return;

	const vec3_t safe_offset{ 26.0f, 26.0f, 26.0f };
	const vec3_t area_mins = origin + mins - safe_offset;
	const vec3_t area_maxs = origin + maxs + safe_offset;

	// --- CORRECTED RADIUS CALCULATION ---
	// 1. Calculate the radius required to encompass the entire clearing box.
	vec3_t farthest_corner_dist;
	farthest_corner_dist.x = std::max(fabs(area_mins.x - origin.x), fabs(area_maxs.x - origin.x));
	farthest_corner_dist.y = std::max(fabs(area_mins.y - origin.y), fabs(area_maxs.y - origin.y));
	farthest_corner_dist.z = std::max(fabs(area_mins.z - origin.z), fabs(area_maxs.z - origin.z));
	const float radius_to_contain_box = farthest_corner_dist.length();

	// 2. Add a generous "slop factor" to catch large entities whose centers are outside the box.
	constexpr float MAX_ENTITY_REACH = 128.0f;
	const float safe_radius = radius_to_contain_box + MAX_ENTITY_REACH;
	// --- END CORRECTION ---

	std::vector<edict_t*> entities_in_area;
	edict_t* ent = nullptr;
	while ((ent = findradius(ent, origin, safe_radius)) != nullptr) {
		if (!ent || !ent->inuse || (ent->svflags & SVF_MONSTER) ||
			ent->solid == SOLID_NOT || ent->solid == SOLID_TRIGGER)
			continue;

		if (!is_valid_vector(ent->s.origin) || !is_valid_vector(ent->mins) || !is_valid_vector(ent->maxs))
			continue;

		// "Narrow phase": Check for precise bounding box intersection.
		if (!EntitiesOverlap(ent, area_mins, area_maxs))
			continue;

		entities_in_area.push_back(ent);
	}

	for (edict_t* current_ent : entities_in_area) {
		if (!current_ent || !current_ent->inuse)
			continue;

		if (current_ent->client) {
			edict_t* spawn_point = SelectSingleSpawnPoint(current_ent);
			if (spawn_point && spawn_point->inuse) {
				TeleportEntity(current_ent, spawn_point);
			}
		}
		else {
			RemoveEntity(current_ent);
		}
	}
}

void PushEntitiesAway(const vec3_t& center, int num_waves, float push_radius, float push_strength, float horizontal_push_strength, float vertical_push_strength)
{
	push_radius = std::max(push_radius, 1.0f);
	const float search_radius = push_radius * 1.5f;

	std::vector<edict_t*> entities_to_process;
	std::vector<edict_t*> entities_to_remove;

	// Collect entities
	for (edict_t* ent = nullptr; (ent = findradius(ent, center, search_radius)) != nullptr;) {
		if (!ent || !ent->inuse)
			continue;

		if (gi.traceline(center, ent->s.origin, nullptr, MASK_SOLID).fraction < 1.0f)
			continue;

		// Use the safe, fast helper function to decide what to do
		if (IsRemovableEntity(ent)) {
			entities_to_remove.push_back(ent);
		}
		else if (ent->takedamage) {
			entities_to_process.push_back(ent);
		}
	}

	// Remove designated entities first
	for (edict_t* ent_to_remove : entities_to_remove) {
		if (ent_to_remove && ent_to_remove->inuse)
			RemoveEntity(ent_to_remove);
	}

	// Process waves (pushing logic remains the same)
	for (int wave = 0; wave < num_waves; wave++) {
		const float wave_progress = static_cast<float>(wave) / num_waves;
		const float size = std::max(push_radius * (1.0f - wave_progress * 0.5f), 0.030f);

		SpawnGrow_Spawn(center, size, size * 0.3f);

		if (wave > 0) {
			vec3_t effect_pos = center;
			for (int i = 0; i < 4; i++) {
				effect_pos[0] = center[0] + crandom() * size * 0.5f;
				effect_pos[1] = center[1] + crandom() * size * 0.5f;
				effect_pos[2] = center[2] + crandom() * size * 0.25f;
				SpawnGrow_Spawn(effect_pos, size * 0.5f, size * 0.15f);
			}
		}

		for (edict_t* ent : entities_to_process) {
			if (!ent || !ent->inuse)
				continue;

			if (!gi.inPVS(center, ent->s.origin, false))
				continue;

			vec3_t push_dir = ent->s.origin - center;
			const float dist = push_dir.length();

			if (dist < 0.01f) {
				push_dir = vec3_t{ crandom(), crandom(), 0.1f };
			}
			else {
				push_dir.normalize();
			}

			const float dist_factor = std::max(0.0f, 1.0f - (dist / search_radius));
			int base_push = (ent->svflags & SVF_MONSTER) ? 800 : 80;
			if (ent->groundentity)
				base_push *= 2;
			base_push = static_cast<int>(base_push * dist_factor);

			if (ent->client) {
				ent->client->landmark_free_fall = true;
			}

			T_Damage(ent, ent, ent, push_dir, ent->s.origin, vec3_origin,
				0, base_push, DAMAGE_RADIUS, MOD_UNKNOWN);

			if (vertical_push_strength > 0 && wave <= 1) {
				const float boost = (wave == 0) ? 100.0f : 75.0f;
				ent->velocity.z += boost;
			}

			if (ent->client && wave == 0) {
				ent->client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;
				ent->client->ps.pmove.pm_time = 100;
			}
		}
	}
}

[[nodiscard]] constexpr bool string_equals(const char* str1, const std::string_view& str2) noexcept {
	return str1 && str2.length() == strlen(str1) && !Q_strncasecmp(str1, str2.data(), str2.length());
}

// This map provides the specific "pretty" display names for each monster type.
// It uses the fast and type-safe MonsterTypeID enum as the key.
const std::unordered_map<horde::MonsterTypeID, std::string_view> monster_name_replacements = {
    // Guards
    {horde::MonsterTypeID::SOLDIER_LIGHT, "Blaster Guard"},
    {horde::MonsterTypeID::SOLDIER, "SG Guard"},
    {horde::MonsterTypeID::SOLDIER_SS, "SS Guard"},
    {horde::MonsterTypeID::SOLDIER_HYPERGUN, "Hyper Guard"},
    {horde::MonsterTypeID::SOLDIER_LASERGUN, "Laser Guard"},
    {horde::MonsterTypeID::SOLDIER_RIPPER, "Ripper Guard"},

    // Infantry
    {horde::MonsterTypeID::INFANTRY_VANILLA, "Infantry"},
    {horde::MonsterTypeID::INFANTRY, "Enforcer"},

    // Gunners
    {horde::MonsterTypeID::GUNNER_VANILLA, "Gunner"},
    {horde::MonsterTypeID::GUNNER, "Heavy Gunner"},
    {horde::MonsterTypeID::GUNCMDR_VANILLA, "Gunner Commander"},
    {horde::MonsterTypeID::GUNCMDR, "Gunner Grenadier"},
    {horde::MonsterTypeID::GUNCMDR_KL, "Gunner Commander"},

    // Flyers
    {horde::MonsterTypeID::FLYER, "Flyer"},
    {horde::MonsterTypeID::KAMIKAZE, "Kamikaze Flyer"},
    {horde::MonsterTypeID::HOVER_VANILLA, "Blaster Icarus"},
    {horde::MonsterTypeID::HOVER, "Rocket Icarus"},
    {horde::MonsterTypeID::DAEDALUS, "Daedalus"},
    {horde::MonsterTypeID::DAEDALUS_BOMBER, "Bomber Daedalus"},

    // Technicians & Support
    {horde::MonsterTypeID::FLOATER, "Technician"},
    {horde::MonsterTypeID::FLOATER_TRACKER, "DarkMatter Technician"},
    {horde::MonsterTypeID::MEDIC, "Medic"},
    {horde::MonsterTypeID::MEDIC_COMMANDER, "Medic Commander"},
    {horde::MonsterTypeID::FIXBOT, "Fixbot"},
    {horde::MonsterTypeID::FIXBOT_KL, "Fixer"},

    // Mutants & Beasts
    {horde::MonsterTypeID::MUTANT, "Mutant"},
    {horde::MonsterTypeID::REDMUTANT, "Raged Mutant"},
    {horde::MonsterTypeID::BERSERK, "Berserker"},
    {horde::MonsterTypeID::GEKK, "Gekk"},
    {horde::MonsterTypeID::PARASITE, "Parasite"},
    {horde::MonsterTypeID::PERRO_KL, "Infected Parasite"},
    {horde::MonsterTypeID::STALKER, "Stalker"},
    {horde::MonsterTypeID::BRAIN, "Brain"},
    {horde::MonsterTypeID::CHICK, "Iron Maiden"},
    {horde::MonsterTypeID::CHICK_HEAT, "Iron Praetor"},

    // Tanks
    {horde::MonsterTypeID::TANK, "Tank"},
    {horde::MonsterTypeID::TANK_64, "N64 Tank"},
    {horde::MonsterTypeID::TANK_COMMANDER, "Tank Commander"},
    {horde::MonsterTypeID::TANK_SPAWNER, "Spawner Tank"},
    {horde::MonsterTypeID::RUNNERTANK, "BETA Runner Tank"},

    // Gladiators
    {horde::MonsterTypeID::GLADIATOR, "Gladiator"},
    {horde::MonsterTypeID::GLADIATOR_B, "DarkMatter Gladiator"},
    {horde::MonsterTypeID::GLADIATOR_C, "Plasma Gladiator"},

    // Spiders
    {horde::MonsterTypeID::SPIDER, "Plasma Spider"},
    {horde::MonsterTypeID::ARACHNID, "Arachnid"},
    {horde::MonsterTypeID::ARACHNID2, "Arachnid"},
    {horde::MonsterTypeID::PSX_ARACHNID, "Arachnid"},
    {horde::MonsterTypeID::GM_ARACHNID, "Guided-Missile Arachnid"},

    // Shamblers
    {horde::MonsterTypeID::SHAMBLER, "Shambler"},
    {horde::MonsterTypeID::SHAMBLER_SMALL, "Tiny Shambler!"},
    {horde::MonsterTypeID::SHAMBLER_KL, "Shambler"},

    // Bosses
    {horde::MonsterTypeID::BOSS2, "Hornet"},
    {horde::MonsterTypeID::BOSS2_64, "N64 Mini Hornet"},
    {horde::MonsterTypeID::BOSS2_MINI, "Mini Hornet"},
    {horde::MonsterTypeID::BOSS2_KL, "N64 Hornet"},
    {horde::MonsterTypeID::CARRIER, "Carrier"},
    {horde::MonsterTypeID::CARRIER_MINI, "Mini Carrier"},
    {horde::MonsterTypeID::MAKRON, "Makron"},
    {horde::MonsterTypeID::MAKRON_KL, "Makron"},
    {horde::MonsterTypeID::JORG, "Jorg"},
    {horde::MonsterTypeID::JORG_SMALL, "Mini Jorg"},
    {horde::MonsterTypeID::WIDOW, "Widow Battle-Maiden"},
    {horde::MonsterTypeID::WIDOW1, "Black Widow"},
    {horde::MonsterTypeID::WIDOW2, "Widow Creator"},
    {horde::MonsterTypeID::GUARDIAN, "Guardian"},
    {horde::MonsterTypeID::PSX_GUARDIAN, "Guardian"},
    {horde::MonsterTypeID::JANITOR, "Janitor"},
    {horde::MonsterTypeID::JANITOR2, "Mini Guardian"},
    {horde::MonsterTypeID::SUPERTANK, "Super-Tank"},
    {horde::MonsterTypeID::SUPERTANKKL, "Super-Tank"},
    {horde::MonsterTypeID::BOSS5, "Super-Tank"},

    // Misc Monsters & Turrets
    {horde::MonsterTypeID::SENTRYGUN, "Sentry-Gun"},
    {horde::MonsterTypeID::TURRET, "Turret"},
    {horde::MonsterTypeID::MISC_INSANE, "Insane Grunt"},
	{horde::MonsterTypeID::FLIPPER, "Flipper"}
};

bool SpawnPointClear(edict_t* spot);
float PlayersRangeFromSpot(edict_t* spot);

bool TeleportSelf(edict_t* ent) {
	if (!ent || !ent->inuse || !ent->client || !ent->solid || ent->deadflag) {
		return false;
	}

	if (ent->client->resp.teleport_cooldown > level.time) {
		float remaining_seconds = (ent->client->resp.teleport_cooldown - level.time).seconds();
		float remaining_display = std::floor(remaining_seconds * 10.0f) / 10.0f;
		gi.LocClient_Print(ent, PRINT_HIGH, "Teleport on cooldown for {} seconds\n", remaining_display);
		return false;
	}

	ent->client->resp.teleport_cooldown = level.time + 3_sec;
	const char* playerName = GetPlayerName(ent);

	// PERFORMANCE FIX: Use the global pre-shuffled list of spawn points.
	// This avoids iterating all entities on the map to find and sort spawn points.
	if (g_num_spawn_points == 0) {
		if (developer->integer) {
			gi.Com_PrintFmt("TeleportSelf WARNING: No valid spawn points found for teleport.\n");
		}
		return false;
	}

	auto perform_teleport_actions = [&](edict_t* destination_spot) {
		TeleportEntity(ent, destination_spot);

		if (ent->client->owned_sphere) {
			edict_t* sphere = ent->client->owned_sphere;
			sphere->s.origin = ent->s.origin;
			sphere->s.origin.z = ent->absmax.z;
			sphere->s.angles[YAW] = ent->s.angles[YAW];
			gi.linkentity(sphere);
		}

		if (!ent->client->emergency_teleport) {
			gi.LocBroadcast_Print(PRINT_HIGH, "{} Teleported Away!\n", playerName);
		}

		ent->client->invincible_time = std::max(level.time, ent->client->invincible_time) + 2_sec;
		};

	bool was_emergency = ent->client->emergency_teleport;

	// Try several times to find a clear spawn point from the global list.
	constexpr int MAX_TELEPORT_ATTEMPTS = 16;
	for (int i = 0; i < MAX_TELEPORT_ATTEMPTS; ++i) {
		size_t random_index = static_cast<size_t>(irandom(g_num_spawn_points));
		edict_t* spot = g_spawn_point_list[random_index];

		// Check if the spot is for ground players and is not occupied.
		if (spot && spot->inuse && spot->style == 0 && !IsSpawnPointOccupied(spot)) {
			perform_teleport_actions(spot);
			if (was_emergency) {
				ent->client->emergency_teleport = false;
			}
			return true;
		}
	}

	// Fallback: If no clear spot was found after several tries, teleport to a random one anyway.
	if (developer->integer) {
		gi.Com_PrintFmt("TeleportSelf WARNING: No clear spawn points. Using random point (potentially blocked).\n");
	}
	size_t random_index = static_cast<size_t>(irandom(g_num_spawn_points));
	perform_teleport_actions(g_spawn_point_list[random_index]);
	if (was_emergency) {
		ent->client->emergency_teleport = false;
	}

	return true;
}

// --- Extern Declarations for Monster Jump Moves ---

// Berserk
extern const mmove_t berserk_move_jump;
extern const mmove_t berserk_move_jump2;

// Brain
extern const mmove_t brain_move_jumpattack;
extern const mmove_t brain_move_jump;
extern const mmove_t brain_move_jump2;

// Chick
extern const mmove_t chick_move_jump;
extern const mmove_t chick_move_jump2;

// Gun Commander
extern const mmove_t guncmdr_move_jump;
extern const mmove_t guncmdr_move_jump2;

// Gunner
extern const mmove_t gunner_move_jump;
extern const mmove_t gunner_move_jump2;

// Gunner Vanilla
extern const mmove_t gunner_vanilla_move_jump;
extern const mmove_t gunner_vanilla_move_jump2;

// Infantry
extern const mmove_t infantry_move_jump;
extern const mmove_t infantry_move_jump2;

// Mutant
extern const mmove_t mutant_move_jump;
extern const mmove_t mutant_move_jump_up;
extern const mmove_t mutant_move_jump_down;

// Parasite
extern const mmove_t parasite_move_jump_up;
extern const mmove_t parasite_move_jump_down;

// Red Mutant
extern const mmove_t redmutant_move_jump;
extern const mmove_t redmutant_move_jump_up;
extern const mmove_t redmutant_move_jump_down;

// Runner Tank
extern const mmove_t runnertank_move_jump;
extern const mmove_t runnertank_move_jump2;

// Shocker
extern const mmove_t shocker_move_jump;
extern const mmove_t shocker_move_jump2;

// Soldier
extern const mmove_t soldier_move_jump;
extern const mmove_t soldier_move_jump2;

// Stalker
extern const mmove_t stalker_move_jump_straightup;
extern const mmove_t stalker_move_jump_up;
extern const mmove_t stalker_move_jump_down;

// Gekk
extern const mmove_t gekk_move_jump_up;
extern const mmove_t gekk_move_jump_down;

// --- End Extern Declarations ---

// --- Global Data Structure for Fast Lookups ---
// Using a sorted vector for lookups. For a relatively small, static set of pointers,
// this offers superior cache locality and can outperform hash-based containers like
// std::unordered_set due to lower overhead from hashing and pointer chasing.
static std::vector<const mmove_t*> g_jump_moves;


// --- Initialization Function ---
// Call this function ONCE during your game/mod initialization (e.g., in InitGame).
void InitializeMonsterMoveSets() {
    // Prevent re-initialization
    if (!g_jump_moves.empty()) {
        return;
    }

    // Reserve memory to prevent reallocations while populating the vector.
    // There are 34 jump moves defined.
    g_jump_moves.reserve(34);

    // Populate the vector with all known jump moves.
    // Berserk
    g_jump_moves.push_back(&berserk_move_jump);
    g_jump_moves.push_back(&berserk_move_jump2);
    
    // Brain
    g_jump_moves.push_back(&brain_move_jumpattack);
    g_jump_moves.push_back(&brain_move_jump);
    g_jump_moves.push_back(&brain_move_jump2);
    
    // Chick
    g_jump_moves.push_back(&chick_move_jump);
    g_jump_moves.push_back(&chick_move_jump2);
    
    // Gun Commander
    g_jump_moves.push_back(&guncmdr_move_jump);
    g_jump_moves.push_back(&guncmdr_move_jump2);
    
    // Gunner
    g_jump_moves.push_back(&gunner_move_jump);
    g_jump_moves.push_back(&gunner_move_jump2);
    
    // Gunner Vanilla
    g_jump_moves.push_back(&gunner_vanilla_move_jump);
    g_jump_moves.push_back(&gunner_vanilla_move_jump2);
    
    // Infantry
    g_jump_moves.push_back(&infantry_move_jump);
    g_jump_moves.push_back(&infantry_move_jump2);
    
    // Mutant
    g_jump_moves.push_back(&mutant_move_jump);
    g_jump_moves.push_back(&mutant_move_jump_up);
    g_jump_moves.push_back(&mutant_move_jump_down);
    
    // Parasite
    g_jump_moves.push_back(&parasite_move_jump_up);
    g_jump_moves.push_back(&parasite_move_jump_down);
    
    // Red Mutant
    g_jump_moves.push_back(&redmutant_move_jump);
    g_jump_moves.push_back(&redmutant_move_jump_up);
    g_jump_moves.push_back(&redmutant_move_jump_down);
    
    // Runner Tank
    g_jump_moves.push_back(&runnertank_move_jump);
    g_jump_moves.push_back(&runnertank_move_jump2);
    
    // Soldier
    g_jump_moves.push_back(&soldier_move_jump);
    g_jump_moves.push_back(&soldier_move_jump2);
    
    // Stalker
    g_jump_moves.push_back(&stalker_move_jump_straightup);
    g_jump_moves.push_back(&stalker_move_jump_up);
    g_jump_moves.push_back(&stalker_move_jump_down);
    
    // Gekk
    g_jump_moves.push_back(&gekk_move_jump_up);
    g_jump_moves.push_back(&gekk_move_jump_down);

    // Sort the vector to allow for fast binary searching.
    std::sort(g_jump_moves.begin(), g_jump_moves.end());
}


// --- Optimized IsMonsterJumping Function ---
// This version uses a pre-sorted vector and binary search for a fast, cache-friendly lookup.
bool IsMonsterJumping(const edict_t* self) {
    // Early exit for invalid entity or if no move is active.
    if (!self || !self->monsterinfo.active_move) {
        return false;
    }

    // Get the raw pointer to the current move from the save_data_t wrapper.
    const mmove_t* current_move = self->monsterinfo.active_move.pointer();

    // Perform a binary search on the sorted vector of jump moves.
    // This is very efficient for this type of check.
    return std::binary_search(g_jump_moves.cbegin(), g_jump_moves.cend(), current_move);
}