#pragma once
#include "../g_local.h"

// Upgrade categories for organization
enum class UpgradeCategory : uint8_t {
    ABILITY,   // Passive player abilities (vampire, ammo regen, vitality, etc.)
    WEAPON,    // Weapon modifications (traced bullets, energy shells, etc.)
    TALENT     // Advanced unlockable abilities (armor vampirism, sentry upgrades, etc.)
};

// Upgrade definition structure - describes each upgradeable skill
struct UpgradeDefinition {
    const char* id;               // Unique identifier (e.g., "vampire", "ammo_regen")
    const char* name;             // Display name (e.g., "Vampirism")
    const char* description;      // Multi-line description with \n for formatting
    int8_t max_level;             // Maximum level (1 for boolean, 5-10 for scalable)
    int32_t cost_per_level;       // Cost in skill points per level
    UpgradeCategory category;     // Category for menu organization
    const char* prereq_id;        // Prerequisite upgrade ID (nullptr if none)
    int8_t prereq_level;          // Required level of prerequisite (0 if none)
};

// Get upgrade definitions
const UpgradeDefinition* GetUpgradeDefinitions();
size_t GetUpgradeDefinitionCount();

// Upgrade management functions
bool CanUpgrade(edict_t* player, const char* upgrade_id);
bool UpgradeSkill(edict_t* player, const char* upgrade_id);
int8_t GetSkillLevel(edict_t* player, const char* upgrade_id);
int8_t GetSkillMaxLevel(const char* upgrade_id);
const UpgradeDefinition* FindUpgradeByID(const char* id);

// Helper functions for specific skills
int32_t GetVampireHealAmount(edict_t* player, int32_t damage_dealt);
float GetAmmoRegenRate(edict_t* player);
int32_t GetVitalityHealthBonus(edict_t* player);
float GetHAPickupMultiplier(edict_t* player);

// Apply skill bonuses to player (called on spawn/respawn)
void ApplySkillBonuses(edict_t* player);

// Reset all skills and refund skill points
void ResetAllSkills(edict_t* player);
