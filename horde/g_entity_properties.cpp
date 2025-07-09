#include "g_entity_properties.h"

// --- Forward declare all necessary die functions ---
extern void turret2_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void turret_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void prox_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void tesla_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void trap_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void laser_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void BecomeExplosion1(edict_t* ent); // Generic explosion
extern void doppleganger_die (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
// --- Wrappers from your original code ---
void Turret2DieWrapper(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) {
    if (self && self->health > 0) self->health = -1;
    turret2_die(self, inflictor, attacker, damage, point, mod);
}
void TurretDieWrapper(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) {
    if (self && self->health > 0) self->health = -1;
    turret_die(self, inflictor, attacker, damage, point, mod);
}

// --- Source Data (The Single Source of Truth) ---
struct entity_properties_source_t {
    horde::SpecialEntityTypeID id;
    bool is_defense;
    bool is_removable;
    EntityDieHandler die_handler;
};

const entity_properties_source_t ENTITY_PROPERTIES_SRC[] = {
    {horde::SpecialEntityTypeID::TESLA_MINE,     true,  true,  tesla_die},
    {horde::SpecialEntityTypeID::FOOD_CUBE_TRAP, true,  true,  trap_die},
    {horde::SpecialEntityTypeID::PROX_MINE,      true,  true,  prox_die},
    {horde::SpecialEntityTypeID::SENTRY_GUN,     true,  true,  Turret2DieWrapper},
    {horde::SpecialEntityTypeID::TURRET,         true,  true,  TurretDieWrapper},
    {horde::SpecialEntityTypeID::LASER_EMITTER,  true, true,  laser_die},
    {horde::SpecialEntityTypeID::LASER_BEAM,     true, true,  laser_die},
    {horde::SpecialEntityTypeID::DOPPLEGANGER,   false, true,  doppleganger_die}
};

// --- Compile-Time Transformation ---
constexpr EntityPropertiesSoA create_entity_properties_soa() {
    EntityPropertiesSoA data{};
    data.is_defense.fill(false);
    data.is_removable.fill(false);
    data.die_handler.fill(nullptr);

    for (const auto& src : ENTITY_PROPERTIES_SRC) {
        size_t index = static_cast<size_t>(src.id);
        if (index < data.NUM_TYPES) {
            data.is_defense[index] = src.is_defense;
            data.is_removable[index] = src.is_removable;
            data.die_handler[index] = src.die_handler;
        }
    }
    return data;
}

// --- Global Definition ---
const EntityPropertiesSoA g_entityProperties = create_entity_properties_soa();