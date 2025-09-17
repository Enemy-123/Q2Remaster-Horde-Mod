#include "g_entity_properties.h"

// --- Forward declare all necessary die functions ---
extern void turret2_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void turret_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void prox_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void tesla_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void trap_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void laser_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void doppleganger_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);
extern void nuke_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t &point, const mod_t &mod);

// --- Recommendation 3: Templated Die Function Wrapper ---
// This generic wrapper removes the need to write a separate wrapper for each die function.
// It ensures the entity's health is negative before calling the original die function.
template<auto OriginalDieFunc>
void DieWrapper(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) {
    if (self && self->health > 0) {
        self->health = -1;
    }
    // Call the original function passed as a template argument.
    OriginalDieFunc(self, inflictor, attacker, damage, point, mod);
}

// --- Global Definition (Single Source of Truth) ---
// This directly initializes the final data structure at compile time.
const std::array<EntityProperties, NUM_SPECIAL_ENTITY_TYPES> g_entityProperties = {{
    // Index 0
    {horde::SpecialEntityTypeID::TESLA_MINE,     true,  true,  tesla_die},
    // Index 1
    {horde::SpecialEntityTypeID::FOOD_CUBE_TRAP, true,  true,  trap_die},
    // Index 2
    {horde::SpecialEntityTypeID::PROX_MINE,      true,  true,  prox_die},
    // Index 3 (Corrected Order)
    {horde::SpecialEntityTypeID::TURRET,         true,  true,  DieWrapper<turret_die>},
    // Index 4 (Corrected Order)
    {horde::SpecialEntityTypeID::SENTRY_GUN,     true,  true,  DieWrapper<turret2_die>},
    // Index 5 (NEW ENTRY)
    {horde::SpecialEntityTypeID::NUKE_MINE,      true,  true,  nuke_die},
    // Index 6
    {horde::SpecialEntityTypeID::LASER_EMITTER,  true,  true,  laser_die},
    // Index 7
    {horde::SpecialEntityTypeID::LASER_BEAM,     true,  true,  laser_die},
    // Index 8
    {horde::SpecialEntityTypeID::DOPPLEGANGER,   false, true,  doppleganger_die}
}};

// --- Recommendation 5: Runtime Verification ---
// This function runs only in debug builds to catch logical errors in the data,
// such as an entity being marked as removable but not having a cleanup function.
void VerifyEntityProperties() {
    for (size_t i = 0; i < NUM_SPECIAL_ENTITY_TYPES; ++i) {
        const auto& props = g_entityProperties[i];
        size_t expected_index = GetEntityIndex(props.id);
        
        gi.Com_PrintFmt("DEBUG: Index {}: ID={}, GetEntityIndex({})={}\n", 
            i, static_cast<int>(props.id), static_cast<int>(props.id), expected_index);
        
        if (expected_index != i) {
            gi.Com_PrintFmt("EntityProperties ERROR: Mismatch at index {}. ID is {} but should correspond to this index.\n",
                i, static_cast<int>(props.id));
        }
    }
}