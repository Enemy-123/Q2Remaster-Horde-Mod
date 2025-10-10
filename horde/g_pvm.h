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
// spawning and gameplay. When pvm is set to 1, Horde mode will be
// automatically enabled during initialization.

// Check if PvM mode is active
bool IsPvMMode();

// Drop backpack on player death (contains all weapons/ammo)
void PVM_DropBackpack(edict_t* player);

// Give player their respawn weapon on spawn
void PVM_GiveRespawnWeapon(edict_t* player);

// PvM Monster Spawning
// Get minimum wave for PvM monster spawning (starts at wave 8)
constexpr int PVM_MIN_WAVE = 1;

// Check if a monster type is valid for PvM mode
// (only monsters from wave 8+ to limit precaching)
bool PVM_IsValidMonster(int minWave);

// PvM Random Monster Rotation
// For PVM mode, we randomly select 10 monsters per map to avoid large precache
constexpr int PVM_RANDOM_MONSTER_COUNT = 24;

// Get the list of randomly selected monsters for this map
// Returns nullptr if PVM is not active or list not initialized
const std::vector<horde::MonsterTypeID>* PVM_GetRandomMonsters();

// Initialize random monster selection for the current map
void PVM_InitRandomMonsters();

// Check if a specific monster type is excluded from PVM random selection
bool PVM_IsMonsterExcluded(horde::MonsterTypeID typeId);
