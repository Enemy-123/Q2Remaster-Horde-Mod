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
// strogg_summoner_die removed (no longer needed - no base entity)
extern void barrel_die(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&);

// --- Global Definition (Single Source of Truth) ---
// Array indices directly correspond to SpecialEntityTypeID enum values (0-11)
const std::array<EntityProperties, NUM_SPECIAL_ENTITY_TYPES> g_entityProperties = {{
    // Index 0: TESLA_MINE
    {true,  true,  tesla_die},
    // Index 1: FOOD_CUBE_TRAP
    {true,  true,  trap_die},
    // Index 2: PROX_MINE
    {true,  true,  prox_die},
    // Index 3: TURRET
    {true,  true,  DieWrapper<turret_die>},
    // Index 4: SENTRY_GUN
    {true,  true,  DieWrapper<turret2_die>},
    // Index 5: NUKE_MINE
    {true,  true,  nuke_die},
    // Index 6: LASER_EMITTER
    {true,  true,  laser_die},
    // Index 7: LASER_BEAM
    {true,  true,  laser_die},
    // Index 8: DOPPLEGANGER
    {false, true,  doppleganger_die},
    // Index 9: STROGG_SUMMONER (obsolete - no base entity anymore)
    {false, false, nullptr},
    // Index 10: MORPHED_PLAYER
    {false, false, nullptr},
    // Index 11: BARREL
    {true,  true,  barrel_die}
}};