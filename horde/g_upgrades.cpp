#include "g_upgrades.h"
#include <cstring>

// Initial upgrade definitions - Phase 2 will add the first 3 abilities
// This array will grow as we add more abilities over time
static const UpgradeDefinition UPGRADE_DEFS[] = {
    // Phase 2: Core abilities
    {
        "vampire",
        "Vampire",
        "Recover health from\ndamage dealt\n"
        "Base: 5% lifesteal\n"
        "Each level: +1% lifesteal\n"
        "Lvl 10: 15% lifesteal\nLevel 6+ Enables Armor Vamp\n",
        10, 1, UpgradeCategory::ABILITY, nullptr, 0
    },
    {
        "ammo_regen",
        "Ammo Regen",
        "Regenerate ammo over time\n"
        "+1 projectile and +5 cells/bullets\n"
        "per level\neach 5 sec"
        "",
        10, 1, UpgradeCategory::ABILITY, nullptr, 0
    },
    {
        "ha_pickup",
        "H/A Pickup",
        "Pickup more health and armor\n"
        "Each level: +20% pickup amount\n"
        "Max at level 5: +100% pickups",
        5, 1, UpgradeCategory::ABILITY, nullptr, 0
    },
    {
        "start_armor",
        "Start Armor",
        "Spawn with +10 armor per level\n"
        "Each level: +10 armor on spawn\n"
        "Max at level 10: 100 armor",
        10, 1, UpgradeCategory::ABILITY, nullptr, 0
    },
    {
        "vitality",
        "Vitality",
        "Increase max health by +10\n"
        "Each level: +10 max health\n"
        "Max at level 10: +100 health",
        10, 1, UpgradeCategory::ABILITY, nullptr, 0
    },
    {
        "max_ammo",
        "Max Ammo",
        "Increase ammo capacity\n"
        "increases max ammo in general\n"
        "",
        10, 1, UpgradeCategory::ABILITY, nullptr, 0
    }

    // More abilities will be added here in future phases
};

// Get the upgrade definitions array
const UpgradeDefinition* GetUpgradeDefinitions() {
    return UPGRADE_DEFS;
}

// Get the number of upgrade definitions
size_t GetUpgradeDefinitionCount() {
    return sizeof(UPGRADE_DEFS) / sizeof(UpgradeDefinition);
}

// Find an upgrade by its ID
const UpgradeDefinition* FindUpgradeByID(const char* id) {
    if (!id) return nullptr;

    for (size_t i = 0; i < GetUpgradeDefinitionCount(); ++i) {
        if (strcmp(UPGRADE_DEFS[i].id, id) == 0) {
            return &UPGRADE_DEFS[i];
        }
    }
    return nullptr;
}

// Get the current level of a skill for a player
int8_t GetSkillLevel(edict_t* player, const char* upgrade_id) {
    if (!player || !player->client || !upgrade_id)
        return 0;

    // Map upgrade ID to skill field
    if (strcmp(upgrade_id, "vampire") == 0)
        return player->client->pers.skills.vampire;
    else if (strcmp(upgrade_id, "ammo_regen") == 0)
        return player->client->pers.skills.ammo_regen;
    else if (strcmp(upgrade_id, "vitality") == 0)
        return player->client->pers.skills.vitality;
    else if (strcmp(upgrade_id, "ha_pickup") == 0)
        return player->client->pers.skills.ha_pickup;
    else if (strcmp(upgrade_id, "auto_haste") == 0)
        return player->client->pers.skills.auto_haste ? 1 : 0;
    else if (strcmp(upgrade_id, "start_armor") == 0)
        return player->client->pers.skills.start_armor;
    else if (strcmp(upgrade_id, "max_ammo") == 0)
        return player->client->pers.skills.max_ammo;
    else if (strcmp(upgrade_id, "armor_vampirism") == 0)
        return player->client->pers.skills.armor_vampirism ? 1 : 0;
    else if (strcmp(upgrade_id, "sentry_upgrade") == 0)
        return player->client->pers.skills.sentry_upgrade ? 1 : 0;
    else if (strcmp(upgrade_id, "tesla_chain") == 0)
        return player->client->pers.skills.tesla_chain ? 1 : 0;

    return 0;
}

// Get the maximum level for a skill
int8_t GetSkillMaxLevel(const char* upgrade_id) {
    const UpgradeDefinition* def = FindUpgradeByID(upgrade_id);
    return def ? def->max_level : 0;
}

// Check if a player can upgrade a skill
bool CanUpgrade(edict_t* player, const char* upgrade_id) {
    if (!player || !player->client || !upgrade_id)
        return false;

    const UpgradeDefinition* def = FindUpgradeByID(upgrade_id);
    if (!def)
        return false;

    // Check if already at max level
    int8_t current_level = GetSkillLevel(player, upgrade_id);
    if (current_level >= def->max_level)
        return false;

    // Check if player has enough skill points
    if (player->client->pers.skill_points < def->cost_per_level)
        return false;

    // Check prerequisites
    if (def->prereq_id) {
        int8_t prereq_level = GetSkillLevel(player, def->prereq_id);
        if (prereq_level < def->prereq_level)
            return false;
    }

    return true;
}

// Upgrade a skill for a player
bool UpgradeSkill(edict_t* player, const char* upgrade_id) {
    if (!CanUpgrade(player, upgrade_id))
        return false;

    const UpgradeDefinition* def = FindUpgradeByID(upgrade_id);
    if (!def)
        return false;

    // Deduct skill points
    player->client->pers.skill_points -= def->cost_per_level;

    // Increment the appropriate skill
    if (strcmp(upgrade_id, "vampire") == 0) {
        player->client->pers.skills.vampire++;
    } else if (strcmp(upgrade_id, "ammo_regen") == 0) {
        player->client->pers.skills.ammo_regen++;
    } else if (strcmp(upgrade_id, "vitality") == 0) {
        player->client->pers.skills.vitality++;
        // Apply vitality bonus immediately
        int32_t health_bonus = 10;
        player->client->pers.max_health += health_bonus;
        player->client->resp.max_health += health_bonus;
        player->max_health += health_bonus;
        player->health += health_bonus; // Also heal
    } else if (strcmp(upgrade_id, "ha_pickup") == 0) {
        player->client->pers.skills.ha_pickup++;
    } else if (strcmp(upgrade_id, "auto_haste") == 0) {
        player->client->pers.skills.auto_haste = true;
    } else if (strcmp(upgrade_id, "start_armor") == 0) {
        player->client->pers.skills.start_armor++;
    } else if (strcmp(upgrade_id, "max_ammo") == 0) {
        player->client->pers.skills.max_ammo++;
        // Apply max ammo bonus immediately
        player->client->pers.max_ammo[AMMO_SHELLS] += 5;
        player->client->pers.max_ammo[AMMO_BULLETS] += 10;
        player->client->pers.max_ammo[AMMO_ROCKETS] += 2;
        player->client->pers.max_ammo[AMMO_CELLS] += 10;
        player->client->pers.max_ammo[AMMO_GRENADES] += 3;
        player->client->pers.max_ammo[AMMO_SLUGS] += 3;
        player->client->pers.max_ammo[AMMO_MAGSLUG] += 2;
        player->client->pers.max_ammo[AMMO_PROX] += 1;
        player->client->pers.max_ammo[AMMO_TRAP] += 1;
        player->client->pers.max_ammo[AMMO_TESLA] += 2;
    } else if (strcmp(upgrade_id, "armor_vampirism") == 0) {
        player->client->pers.skills.armor_vampirism = true;
    } else if (strcmp(upgrade_id, "sentry_upgrade") == 0) {
        player->client->pers.skills.sentry_upgrade = true;
    } else if (strcmp(upgrade_id, "tesla_chain") == 0) {
        player->client->pers.skills.tesla_chain = true;
    }

    return true;
}

// Calculate vampire heal amount based on level and damage dealt
int32_t GetVampireHealAmount(edict_t* player, int32_t damage_dealt) {
    if (!player || !player->client)
        return 0;

    int8_t vampire_level = player->client->pers.skills.vampire;
    if (vampire_level == 0)
        return 0;

    // Base 5% lifesteal, +1% per level
    float lifesteal_percent = 0.05f + (vampire_level * 0.01f);
    int32_t heal_amount = static_cast<int32_t>(damage_dealt * lifesteal_percent);

    return heal_amount;
}

// Get ammo regeneration rate based on level
float GetAmmoRegenRate(edict_t* player) {
    if (!player || !player->client)
        return 0.0f;

    int8_t ammo_level = player->client->pers.skills.ammo_regen;
    if (ammo_level == 0)
        return 0.0f;

    // Levels 1-3: 1 ammo per 3 seconds
    if (ammo_level <= 3)
        return 1.0f / 3.0f;

    // Levels 4-6: 2 ammo per 3 seconds
    if (ammo_level <= 6)
        return 2.0f / 3.0f;

    // Levels 7-10: 3 ammo per 2 seconds
    return 3.0f / 2.0f;
}

// Get vitality health bonus based on level
int32_t GetVitalityHealthBonus(edict_t* player) {
    if (!player || !player->client)
        return 0;

    int8_t vitality_level = player->client->pers.skills.vitality;
    return vitality_level * 10; // +10 HP per level
}

// Get health/armor pickup multiplier based on level
float GetHAPickupMultiplier(edict_t* player) {
    if (!player || !player->client)
        return 1.0f;

    int8_t ha_level = player->client->pers.skills.ha_pickup;
    if (ha_level == 0)
        return 1.0f;

    // +20% per level, up to +100% at level 5
    return 1.0f + (ha_level * 0.20f);
}

// Apply skill bonuses to player (called on spawn/respawn)
void ApplySkillBonuses(edict_t* player) {
    if (!player || !player->client)
        return;

    // Apply vitality bonus
    int8_t vitality_level = player->client->pers.skills.vitality;
    if (vitality_level > 0) {
        int32_t health_bonus = vitality_level * 10;
        player->client->pers.max_health += health_bonus;
        player->client->resp.max_health += health_bonus;
        player->max_health += health_bonus;
    }

    // Apply max ammo bonuses
    int8_t max_ammo_level = player->client->pers.skills.max_ammo;
    if (max_ammo_level > 0) {
        player->client->pers.max_ammo[AMMO_SHELLS] += max_ammo_level * 5;
        player->client->pers.max_ammo[AMMO_BULLETS] += max_ammo_level * 10;
        player->client->pers.max_ammo[AMMO_ROCKETS] += max_ammo_level * 2;
        player->client->pers.max_ammo[AMMO_CELLS] += max_ammo_level * 10;
        player->client->pers.max_ammo[AMMO_GRENADES] += max_ammo_level * 3;
        player->client->pers.max_ammo[AMMO_SLUGS] += max_ammo_level * 3;
        player->client->pers.max_ammo[AMMO_MAGSLUG] += max_ammo_level * 2;
        player->client->pers.max_ammo[AMMO_PROX] += max_ammo_level * 1;
        player->client->pers.max_ammo[AMMO_TRAP] += max_ammo_level * 1;
        player->client->pers.max_ammo[AMMO_TESLA] += max_ammo_level * 2;
    }
}

// Reset all skills and refund skill points
void ResetAllSkills(edict_t* player) {
    if (!player || !player->client)
        return;

    // Calculate total points to refund
    int32_t total_points = 0;
    total_points += player->client->pers.skills.vampire;
    total_points += player->client->pers.skills.ammo_regen;
    total_points += player->client->pers.skills.vitality;
    total_points += player->client->pers.skills.ha_pickup;
    total_points += player->client->pers.skills.start_armor;
    total_points += player->client->pers.skills.max_ammo;
    total_points += player->client->pers.skills.auto_haste ? 1 : 0;
    total_points += player->client->pers.skills.armor_vampirism ? 1 : 0;
    total_points += player->client->pers.skills.sentry_upgrade ? 1 : 0;
    total_points += player->client->pers.skills.tesla_chain ? 1 : 0;

    if (total_points == 0) {
        gi.LocClient_Print(player, PRINT_HIGH, nullptr, "No skills to reset!\n");
        return;
    }

    // Reset all skills
    player->client->pers.skills.vampire = 0;
    player->client->pers.skills.ammo_regen = 0;
    player->client->pers.skills.vitality = 0;
    player->client->pers.skills.ha_pickup = 0;
    player->client->pers.skills.start_armor = 0;
    player->client->pers.skills.max_ammo = 0;
    player->client->pers.skills.auto_haste = false;
    player->client->pers.skills.armor_vampirism = false;
    player->client->pers.skills.sentry_upgrade = false;
    player->client->pers.skills.tesla_chain = false;

    // Refund all points
    player->client->pers.skill_points += total_points;

    gi.LocClient_Print(player, PRINT_HIGH, nullptr, "All skills reset! {} skill points refunded.\n", total_points);
}
