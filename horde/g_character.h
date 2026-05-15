#pragma once

#include "../g_local.h"
#include <string>

// Character persistence system - works for both Horde and PvM modes
// Stores player preferences, respawn weapon, progression, skills, and weapon upgrades in SQLite.

// Character data structure
struct CharacterData
{
    char player_name[MAX_NETNAME];
    char respawn_weapon[64];  // Weapon name (e.g., "Rocket Launcher")

    // Display preferences
    bool id_display;
    bool iddmg_display;

    // Gameplay preferences
    int32_t sentry_gun_choice;  // Preferred sentry type (sentrytype_t)
    int32_t morph_preference;   // Preferred morph type (0=Brain, 1=Flyer)

    // Future: leveling system
    int32_t level;
    int32_t xp;

    CharacterData() :
        respawn_weapon{0},
        id_display(false),
        iddmg_display(false),
        sentry_gun_choice(0),  // SENTRY_RANDOM
        morph_preference(0),   // Brain (default)
        level(1),
        xp(0)
    {
        player_name[0] = '\0';
        respawn_weapon[0] = '\0';
    }
};

// Character system initialization
void Character_Init();

// Load character from SQLite (called on player connect)
bool Character_Load(edict_t* player);

// Save character to SQLite (called on disconnect, preference changes, etc.)
bool Character_Save(edict_t* player);

// Create default character row if it doesn't exist
void Character_CreateDefault(edict_t* player);

// Delete a character row and all scoped values from SQLite.
bool Character_Reset(edict_t* player);

// Set respawn weapon preference (called from menu)
void Character_SetRespawnWeapon(edict_t* player, const char* weapon_name);

// Get respawn weapon preference
const char* Character_GetRespawnWeapon(edict_t* player);

// Utility: Sanitize player name for DB key compatibility.
std::string Character_SanitizeName(const char* name);

// Utility: Get character database path.
std::string Character_GetFilePath(edict_t* player);
