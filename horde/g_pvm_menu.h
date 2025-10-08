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
