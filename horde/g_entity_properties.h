#pragma once
#include "horde_ids.h" // For SpecialEntityTypeID
#include "../g_local.h"      // For edict_t, vec3_t, mod_t

// Type alias for the entity die handler function pointer
using EntityDieHandler = void(*)(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);

// The new AoS (Array of Structures) data structure
struct EntityProperties {
    horde::SpecialEntityTypeID id;
    bool is_defense;
    bool is_removable;
    EntityDieHandler die_handler;
};

// The total number of unique special entity types.
constexpr size_t NUM_SPECIAL_ENTITY_TYPES = static_cast<size_t>(horde::SpecialEntityTypeID::COUNT);

// Declare the global instance that will hold the data.
// This is an array of structs, which is more cache-friendly for single lookups.
extern const std::array<EntityProperties, NUM_SPECIAL_ENTITY_TYPES> g_entityProperties;

// --- Safe, Inline Accessor Functions ---

// A robust, compile-time function to get the array index from an enum ID.
// This prevents silent errors if the enum values are ever changed or have gaps.
constexpr size_t GetEntityIndex(horde::SpecialEntityTypeID id) {
    switch (id) {
        case horde::SpecialEntityTypeID::TESLA_MINE:     return 0;
        case horde::SpecialEntityTypeID::FOOD_CUBE_TRAP: return 1;
        case horde::SpecialEntityTypeID::PROX_MINE:      return 2;
        case horde::SpecialEntityTypeID::TURRET:         return 3; // Corrected order
        case horde::SpecialEntityTypeID::SENTRY_GUN:     return 4; // Corrected order
        case horde::SpecialEntityTypeID::NUKE_MINE:      return 5; // Added NUKE_MINE
        case horde::SpecialEntityTypeID::LASER_EMITTER:  return 6;
        case horde::SpecialEntityTypeID::LASER_BEAM:     return 7;
        case horde::SpecialEntityTypeID::DOPPLEGANGER:   return 8;
        default:                                         return NUM_SPECIAL_ENTITY_TYPES; // Invalid index
    }
}

inline bool IsDefense(horde::SpecialEntityTypeID id) {
    const size_t index = GetEntityIndex(id);
    return (index < NUM_SPECIAL_ENTITY_TYPES) ? g_entityProperties[index].is_defense : false;
}

inline bool IsRemovable(horde::SpecialEntityTypeID id) {
    const size_t index = GetEntityIndex(id);
    return (index < NUM_SPECIAL_ENTITY_TYPES) ? g_entityProperties[index].is_removable : false;
}

inline EntityDieHandler GetDieHandler(horde::SpecialEntityTypeID id) {
    const size_t index = GetEntityIndex(id);
    return (index < NUM_SPECIAL_ENTITY_TYPES) ? g_entityProperties[index].die_handler : nullptr;
}

#ifdef _DEBUG
// Declaration for the debug-mode verification function.
void VerifyEntityProperties();
#endif