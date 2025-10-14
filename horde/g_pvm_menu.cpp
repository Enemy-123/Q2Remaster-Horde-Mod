#include "g_pvm_menu.h"
#include "../g_local.h"
#include "g_character.h"
#include "../shared.h"
#include "../ctf/p_ctf_menu.h"
#include <cmath>
#include <cstring>

// Forward declarations
extern void OpenHordeMenu(edict_t* ent) noexcept;

// Constants from pvm_stats.json
constexpr int32_t BASE_XP_PER_LEVEL = 80;
constexpr float XP_GROWTH_FACTOR = 2.f;
constexpr int32_t MAX_AMMO_LEVEL_CAP = 10;
constexpr int32_t VITALITY_LEVEL_CAP = 10;

// Calculate XP required for a specific level
int32_t PvM_GetXPForLevel(int32_t level)
{
    if (level < 1)
        return 0;

    // XP for level N = BASE_XP * (GROWTH_FACTOR ^ (N-1))
    // Level 1 requires: 100 * (2^0) = 100 XP
    // Level 2 requires: 100 * (2^1) = 200 XP
    // Level 3 requires: 100 * (2^2) = 400 XP, etc.
    return static_cast<int32_t>(BASE_XP_PER_LEVEL * std::pow(XP_GROWTH_FACTOR, level - 1));
}

// Get XP needed to reach next level
int32_t PvM_GetXPToNextLevel(edict_t* player)
{
    if (!player || !player->client)
        return 0;

    int32_t current_level = player->client->pers.pvm_level;
    int32_t xp_for_next = PvM_GetXPForLevel(current_level + 1);
    int32_t current_xp = player->client->pers.pvm_xp;

    return xp_for_next - current_xp;
}

// Allocate a stat point
void PvM_AllocateStat(edict_t* player, const char* stat_name)
{
    if (!player || !player->client)
        return;

    // Check if player has available stat points
    if (player->client->pers.pvm_stat_points <= 0)
    {
        gi.LocClient_Print(player, PRINT_HIGH, nullptr, "No stat points available!\n");
        return;
    }

    // Determine which stat to upgrade
    if (strcmp(stat_name, "max_ammo") == 0)
    {
        if (player->client->pers.pvm_max_ammo_level >= MAX_AMMO_LEVEL_CAP)
        {
            gi.LocClient_Print(player, PRINT_HIGH, nullptr, "Max Ammo is already at maximum level!\n");
            return;
        }

        player->client->pers.pvm_max_ammo_level++;
        player->client->pers.pvm_stat_points--;
        gi.LocClient_Print(player, PRINT_HIGH, nullptr, "Max Ammo increased to level {}!\n",
                          player->client->pers.pvm_max_ammo_level);
    }
    else if (strcmp(stat_name, "vitality") == 0)
    {
        if (player->client->pers.pvm_vitality_level >= VITALITY_LEVEL_CAP)
        {
            gi.LocClient_Print(player, PRINT_HIGH, nullptr, "Vitality is already at maximum level!\n");
            return;
        }

        player->client->pers.pvm_vitality_level++;
        player->client->pers.pvm_stat_points--;
        gi.LocClient_Print(player, PRINT_HIGH, nullptr, "Vitality increased to level {}!\n",
                          player->client->pers.pvm_vitality_level);
    }

    // Save character data
    Character_Save(player);
}

// Reset all stats
void PvM_ResetStats(edict_t* player)
{
    if (!player || !player->client)
        return;

    // Calculate total points to refund
    int32_t total_points = player->client->pers.pvm_max_ammo_level +
                          player->client->pers.pvm_vitality_level;

    if (total_points == 0)
    {
        gi.LocClient_Print(player, PRINT_HIGH, nullptr, "No stats to reset!\n");
        return;
    }

    // Reset all stat levels
    player->client->pers.pvm_max_ammo_level = 0;
    player->client->pers.pvm_vitality_level = 0;

    // Refund all points
    player->client->pers.pvm_stat_points += total_points;

    gi.LocClient_Print(player, PRINT_HIGH, nullptr, "All PvM stats reset! {} stat points refunded.\n", total_points);

    // Save character data
    Character_Save(player);
}

// Menu handler
void PvM_StatsMenuHandler(edict_t* player, pmenuhnd_t* p)
{
    if (!player || !player->client || !p)
        return;

    const char* selected_text = p->entries[p->cur].text;

    // Check for "Max Ammo" option
    if (strstr(selected_text, "Max Ammo"))
    {
        PvM_AllocateStat(player, "max_ammo");
        PvM_OpenStatsMenu(player); // Refresh menu
        return;
    }

    // Check for "Vitality" option
    if (strstr(selected_text, "Vitality"))
    {
        PvM_AllocateStat(player, "vitality");
        PvM_OpenStatsMenu(player); // Refresh menu
        return;
    }

    // Check for "Reset Stats" option
    if (strstr(selected_text, "Reset Stats"))
    {
        PvM_ResetStats(player);
        PvM_OpenStatsMenu(player); // Refresh menu
        return;
    }

    // Check for "Back" option
    if (strstr(selected_text, "Back"))
    {
        PMenu_Close(player);
        OpenHordeMenu(player);
        return;
    }

    // Check for "Close" option
    if (strstr(selected_text, "Close"))
    {
        PMenu_Close(player);
        return;
    }
}

// Open PvM stats menu
void PvM_OpenStatsMenu(edict_t* player)
{
    if (!player || !player->client)
        return;

    // Close any existing menu
    if (player->client->menu)
    {
        PMenu_Close(player);
    }

    // Set menu protection
    player->client->menu_protected = true;
    player->client->menu_protection_start = level.time;

    static pmenu_t entries[20] = {};  // Increased for more entries
    int count = 0;

    auto add_entry = [&](const char* text, int align, SelectFunc_t func = nullptr)
    {
        if (count < static_cast<int>(std::size(entries)))
        {
            Q_strlcpy(entries[count].text, text, sizeof(entries[count].text));
            entries[count].align = align;
            entries[count].SelectFunc = func;
            count++;
        }
    };

    // Calculate XP to next level
    int32_t xp_needed = PvM_GetXPToNextLevel(player);

    // Title
    add_entry("*Character Info*", PMENU_ALIGN_LEFT);
    add_entry("", PMENU_ALIGN_LEFT); // Separator

    // Level and XP display
    add_entry(G_Fmt("Level: {}", player->client->pers.pvm_level).data(), PMENU_ALIGN_LEFT);
    add_entry(G_Fmt("XP: {}/{}", 
                    player->client->pers.pvm_xp,
                    player->client->pers.pvm_xp + xp_needed).data(),
              PMENU_ALIGN_LEFT);

    // Character points
    add_entry(G_Fmt("Skill Points: {}", player->client->pers.skill_points).data(),
              PMENU_ALIGN_LEFT);
    add_entry(G_Fmt("Weapon Points: {}", player->client->pers.weapon_points).data(),
              PMENU_ALIGN_LEFT);

    // Respawn weapon
    const char* respawn_weapon = Character_GetRespawnWeapon(player);
    add_entry(G_Fmt("Respawn Weapon: {}", respawn_weapon ? respawn_weapon : "None").data(),
              PMENU_ALIGN_LEFT);

    add_entry("", PMENU_ALIGN_LEFT); // Separator

    // Display current ability levels from unified skills system
    add_entry("=== Current Abilities ===", PMENU_ALIGN_LEFT);
    add_entry(G_Fmt("Max Ammo: {}/10", player->client->pers.skills.max_ammo).data(),
              PMENU_ALIGN_LEFT);
    add_entry(G_Fmt("Vitality: {}/10", player->client->pers.skills.vitality).data(),
              PMENU_ALIGN_LEFT);
    add_entry(G_Fmt("Vampirism: {}/10", player->client->pers.skills.vampire).data(),
              PMENU_ALIGN_LEFT);
    add_entry(G_Fmt("Ammo Regen: {}/10", player->client->pers.skills.ammo_regen).data(),
              PMENU_ALIGN_LEFT);
    add_entry(G_Fmt("H/A Pickup: {}/5", player->client->pers.skills.ha_pickup).data(),
              PMENU_ALIGN_LEFT);
    add_entry(G_Fmt("Start Armor: {}/10", player->client->pers.skills.start_armor).data(),
              PMENU_ALIGN_LEFT);

    add_entry("", PMENU_ALIGN_LEFT); // Separator
    add_entry("Back", PMENU_ALIGN_LEFT, PvM_StatsMenuHandler);
    add_entry("Close", PMENU_ALIGN_LEFT, PvM_StatsMenuHandler);

    PMenu_Open(player, entries, -1, count, nullptr, nullptr);
}

// Award XP to player
void PvM_AwardExperience(edict_t* player, int32_t xp_amount)
{
    if (!player || !player->client)
        return;

    // Skip XP in Classic Mode (vortex 0)
    if (g_vortex->integer == 0)
        return;

    // Award XP
    player->client->pers.pvm_xp += xp_amount;

    // Show XP award message
    gi.LocClient_Print(player, PRINT_HIGH, nullptr, "+{} XP\n", xp_amount);

    // Check for level-up
    PvM_CheckLevelUp(player);

    // Save character data
    Character_Save(player);
}

// Check and process level-ups
void PvM_CheckLevelUp(edict_t* player)
{
    if (!player || !player->client)
        return;

    // Skip leveling in Classic Mode (vortex 0)
    if (g_vortex->integer == 0)
        return;

    int32_t current_level = player->client->pers.pvm_level;
    int32_t current_xp = player->client->pers.pvm_xp;
    int32_t xp_for_next = PvM_GetXPForLevel(current_level + 1);
    const char* player_name = GetPlayerName(player);

    // Check if player has enough XP for next level
    while (current_xp >= xp_for_next && current_level < 99) // Max level 99
    {
        // Level up!
        current_level++;
        player->client->pers.pvm_level = current_level;
        player->client->pers.skill_points += 2; // Grant 2 skill point per level
        player->client->pers.weapon_points += 4; // Grant 4 weapon points per level

        // Every 5 levels, auto-grant +1 vitality and +1 max ammo (free, not using skill points)
        if (current_level % 5 == 0)
        {
            // Auto-grant vitality if not at cap (using unified skills system)
            if (player->client->pers.skills.vitality < 10)
            {
                player->client->pers.skills.vitality++;
                player->client->pers.skills.free_vitality++;  // Track as free bonus

                // Apply vitality bonus immediately (+10 max health)
                int32_t health_bonus = 10;
                player->client->pers.max_health += health_bonus;
                player->client->resp.max_health += health_bonus;
                player->max_health += health_bonus;
                player->health += health_bonus; // Also heal
            }

            // Auto-grant max ammo if not at cap (using unified skills system)
            if (player->client->pers.skills.max_ammo < 10)
            {
                player->client->pers.skills.max_ammo++;
                player->client->pers.skills.free_max_ammo++;  // Track as free bonus

                // Apply max ammo bonus immediately
                player->client->pers.max_ammo[AMMO_SHELLS] += 10;
                player->client->pers.max_ammo[AMMO_BULLETS] += 25;
                player->client->pers.max_ammo[AMMO_ROCKETS] += 10;
                player->client->pers.max_ammo[AMMO_CELLS] += 25;
                player->client->pers.max_ammo[AMMO_GRENADES] += 10;
                player->client->pers.max_ammo[AMMO_SLUGS] += 10;
                player->client->pers.max_ammo[AMMO_MAGSLUG] += 15;
                player->client->pers.max_ammo[AMMO_PROX] += 10;
                player->client->pers.max_ammo[AMMO_TRAP] += 2;
                player->client->pers.max_ammo[AMMO_TESLA] += 2;
            }
        }


        // Show level-up message
        gi.LocClient_Print(player, PRINT_TYPEWRITER,
                           "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n*****LEVEL UP!*****\n\n\n\n\n\n\n\n\n\n\nYou are now level {}!\n+2 Skill Point\n+4 Weapon Points\nOpen Menu for upgrading skills",
                           current_level);

        gi.LocClient_Print(player, PRINT_HIGH,
                           "*** LEVEL UP! You are now level {} (+2 skill point, +4 weapon points) ***\n",
                           current_level);

        // // Show bonus message if applicable
        // if (got_free_vitality && got_free_max_ammo)
        // {
        //     gi.LocClient_Print(player, PRINT_HIGH, nullptr,
        //                        "*** MILESTONE! +1 Vitality & +1 Max Ammo (Free) ***\n");
        // }
        // else if (got_free_vitality)
        // {
        //     gi.LocClient_Print(player, PRINT_HIGH, nullptr,
        //                        "*** MILESTONE! +1 Vitality (Free) ***\n");
        // }
        // else if (got_free_max_ammo)
        // {
        //     gi.LocClient_Print(player, PRINT_HIGH, nullptr,
        //                        "*** MILESTONE! +1 Max Ammo (Free) ***\n");
        // }

        gi.LocBroadcast_Print(PRINT_CHAT, "*****{} gained a level*****\n", player_name);

        gi.sound(player, CHAN_AUTO, gi.soundindex("misc/keyuse.wav"), 1.f, ATTN_NORM, 0.f);

        // Save character data
        Character_Save(player);

        // Update for next iteration
        xp_for_next = PvM_GetXPForLevel(current_level + 1);
    }
}

// Apply PvM stat bonuses to player
void PvM_ApplyStatBonuses(edict_t* player)
{
    if (!player || !player->client)
        return;

    // Apply health bonus per level (+1 HP per level)
    int32_t pvm_level = player->client->pers.pvm_level;
    int32_t health_bonus_total = LEVELUP_PLAYER_ADDON_HEALTH * pvm_level;
    player->client->pers.max_health += health_bonus_total;
    player->client->resp.max_health += health_bonus_total;
    player->max_health += health_bonus_total;
    player->health += health_bonus_total; // Also heal

    // Get stat levels from unified skills system
    int32_t max_ammo_level = player->client->pers.skills.max_ammo;
    int32_t vitality_level = player->client->pers.skills.vitality;

    // Apply Max Ammo bonuses (from unified skills system)
    if (max_ammo_level > 0)
    {
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

    // Apply Vitality bonuses (from unified skills system)
    if (vitality_level > 0)
    {
        int health_bonus = vitality_level * 10;
        player->client->pers.max_health += health_bonus;
        player->client->resp.max_health += health_bonus;
        player->max_health += health_bonus;
        player->health = player->max_health;
        // Note: Armor in Q2 is handled through inventory items, not a max_armor field
        // Future enhancement: Could give armor pickups or modify armor pickup logic
    }
}
