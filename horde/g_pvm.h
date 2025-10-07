#pragma once

#include "../g_local.h"

// PvM (Player vs Monster) mode declarations
//
// PvM mode adds the following features to the base game:
// - Backpack drops on death containing all weapons/ammo
// - Customizable respawn weapons
// - Character persistence system
//
// NOTE: PvM mode requires Horde mode to be active (g_horde 1) for proper
// spawning and gameplay. When g_pvm is set to 1, Horde mode will be
// automatically enabled during initialization.

// Check if PvM mode is active
bool IsPvMMode();

// Drop backpack on player death (contains all weapons/ammo)
void PVM_DropBackpack(edict_t* player);

// Give player their respawn weapon on spawn
void PVM_GiveRespawnWeapon(edict_t* player);
