#pragma once
#include "horde_ids.h" 
#include "../g_local.h" 

using EntityDieHandler = void(*)(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);

struct EntityProperties {
    horde::SpecialEntityTypeID id;
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

constexpr size_t GetEntityIndex(horde::SpecialEntityTypeID id) {
    switch (id) {
        case horde::SpecialEntityTypeID::TESLA_MINE: return 0;
        case horde::SpecialEntityTypeID::FOOD_CUBE_TRAP: return 1;
        case horde::SpecialEntityTypeID::PROX_MINE: return 2;
        case horde::SpecialEntityTypeID::TURRET: return 3;
        case horde::SpecialEntityTypeID::SENTRY_GUN: return 4;
        case horde::SpecialEntityTypeID::NUKE_MINE: return 5;
        case horde::SpecialEntityTypeID::LASER_EMITTER: return 6;
        case horde::SpecialEntityTypeID::LASER_BEAM: return 7;
        case horde::SpecialEntityTypeID::DOPPLEGANGER: return 8;
        default: return NUM_SPECIAL_ENTITY_TYPES;
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
void VerifyEntityProperties();
#endif