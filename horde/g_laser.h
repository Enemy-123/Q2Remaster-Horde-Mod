#pragma once

#include "../g_local.h" // Includes edict_t, mod_t, and now LaserConstants

//
// g_laser.c
//

// Creates a laser for the given player entity.
// Called from a player command.
void create_laser(edict_t* ent);

// Removes all lasers owned by a given player entity.
// Called from a player command.
void remove_lasers(edict_t* ent) noexcept;

// Updates the damage and health of all active lasers based on game progression.
// Called from the main game logic (e.g., when a new wave starts).
void G_UpdateActiveLasersForWaveProgression(int current_wave_level_from_game);

void G_UpdateAdrenalineBasedDeployables();


// --- Engine-level Function Declarations ---
// These functions are assigned to edict_t members (like think, die, etc.)
// and need to be visible to the compiler when those assignments happen.

// The "die" function for all laser components (emitter, beam).
void laser_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);

// The "think" function for the laser beam entity (handles piercing and damage).
void laser_beam_think(edict_t* self);

// The "think" function for the laser emitter entity (handles visuals and timeouts).
void emitter_think(edict_t* self);
