#pragma once

#include "../g_local.h"

// PvM Stats Menu System
// Allows players to allocate stat points into Max Ammo and Vitality

// Open the PvM stats menu
void PvM_OpenStatsMenu(edict_t* player);

// Menu handler for PvM stats menu interactions
void PvM_StatsMenuHandler(edict_t* player, pmenuhnd_t* p);

// Allocate a stat point to a specific stat
void PvM_AllocateStat(edict_t* player, const char* stat_name);

// Reset all allocated stats (refund points)
void PvM_ResetStats(edict_t* player);

// Calculate XP required for next level
int32_t PvM_GetXPForLevel(int32_t level);

// Get current XP required for player's next level
int32_t PvM_GetXPToNextLevel(edict_t* player);

// Award XP to player (called on monster kill)
void PvM_AwardExperience(edict_t* player, int32_t xp_amount);

// Check and process level-ups
void PvM_CheckLevelUp(edict_t* player);

// Apply PvM stat bonuses to player (called on spawn/respawn)
void PvM_ApplyStatBonuses(edict_t* player);
