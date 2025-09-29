#pragma once
#include "horde_ids.h" 
#include "../g_local.h" 

using EntityDieHandler = void(*)(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);

struct EntityProperties {
    bool is_defense;
    bool is_removable;
    EntityDieHandler die_handler;
};

constexpr size_t NUM_SPECIAL_ENTITY_TYPES = static_cast<size_t>(horde::SpecialEntityTypeID::COUNT);

extern const std::array<EntityProperties, NUM_SPECIAL_ENTITY_TYPES> g_entityProperties;

// Move template to header for proper linkage
template<auto OriginalDieFunc>
void DieWrapper(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) {
    if (self && self->health > 0) {
        self->health = -1;
    }
    OriginalDieFunc(self, inflictor, attacker, damage, point, mod);
}

// Compile-time verification that enum values are sequential starting from 0
static_assert(static_cast<size_t>(horde::SpecialEntityTypeID::TESLA_MINE) == 0, "Enum must start at 0");
static_assert(static_cast<size_t>(horde::SpecialEntityTypeID::COUNT) == NUM_SPECIAL_ENTITY_TYPES, "Enum count mismatch");

// Direct array access using enum value as index (enum is sequential 0-11)
inline bool IsDefense(horde::SpecialEntityTypeID id) {
    const size_t index = static_cast<size_t>(id);
    return (index < NUM_SPECIAL_ENTITY_TYPES) ? g_entityProperties[index].is_defense : false;
}

inline bool IsRemovable(horde::SpecialEntityTypeID id) {
    const size_t index = static_cast<size_t>(id);
    return (index < NUM_SPECIAL_ENTITY_TYPES) ? g_entityProperties[index].is_removable : false;
}

inline EntityDieHandler GetDieHandler(horde::SpecialEntityTypeID id) {
    const size_t index = static_cast<size_t>(id);
    return (index < NUM_SPECIAL_ENTITY_TYPES) ? g_entityProperties[index].die_handler : nullptr;
}