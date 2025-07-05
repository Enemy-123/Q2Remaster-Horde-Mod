#pragma once
#include "horde_ids.h" // For SpecialEntityTypeID
#include "../g_local.h"      // For edict_t, vec3_t, mod_t

// Type alias for the entity die handler function pointer
using EntityDieHandler = void(*)(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);

// The optimized SoA data structure for special entity properties
struct EntityPropertiesSoA {
    static constexpr size_t NUM_TYPES = static_cast<size_t>(horde::SpecialEntityTypeID::COUNT);

    std::array<bool, NUM_TYPES> is_defense;
    std::array<bool, NUM_TYPES> is_removable;
    std::array<EntityDieHandler, NUM_TYPES> die_handler;
};

// Declare the global instance that will hold the data
extern const EntityPropertiesSoA g_entityProperties;