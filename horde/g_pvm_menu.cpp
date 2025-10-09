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
constexpr int32_t BASE_XP_PER_LEVEL = 100;
constexpr float XP_GROWTH_FACTOR = 2.f;
constexpr int32_t MAX_AMMO_LEVEL_CAP = 10;
constexpr int32_t VITALITY_LEVEL_CAP = 10;

// Calculate XP required for a specific level
int32_t PvM_GetXPForLevel(int32_t level)
{
    if (level <= 1)
        return 0;

    // XP for level N = BASE_XP * (GROWTH_FACTOR ^ (N-1))
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

    static pmenu_t entries[12];
    memset(entries, 0, sizeof(entries));
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
    add_entry("*PvM Character Stats*", PMENU_ALIGN_CENTER);
    add_entry("", PMENU_ALIGN_CENTER); // Separator

    // Level and XP display
    add_entry(G_Fmt("Level: {} | XP: {}/{}",
                    player->client->pers.pvm_level,
                    player->client->pers.pvm_xp,
                    player->client->pers.pvm_xp + xp_needed).data(),
              PMENU_ALIGN_CENTER);

    add_entry(G_Fmt("Stat Points: {}", player->client->pers.pvm_stat_points).data(),
              PMENU_ALIGN_CENTER);

    add_entry("", PMENU_ALIGN_CENTER); // Separator

    // Max Ammo stat
    bool can_upgrade_ammo = player->client->pers.pvm_stat_points > 0 &&
                           player->client->pers.pvm_max_ammo_level < MAX_AMMO_LEVEL_CAP;
    add_entry(G_Fmt("Max Ammo [{}/{}] {}",
                    player->client->pers.pvm_max_ammo_level,
                    MAX_AMMO_LEVEL_CAP,
                    can_upgrade_ammo ? "[+]" : "").data(),
              PMENU_ALIGN_LEFT,
              can_upgrade_ammo ? PvM_StatsMenuHandler : nullptr);

    // Vitality stat
    bool can_upgrade_vitality = player->client->pers.pvm_stat_points > 0 &&
                               player->client->pers.pvm_vitality_level < VITALITY_LEVEL_CAP;
    add_entry(G_Fmt("Vitality [{}/{}] {}",
                    player->client->pers.pvm_vitality_level,
                    VITALITY_LEVEL_CAP,
                    can_upgrade_vitality ? "[+]" : "").data(),
              PMENU_ALIGN_LEFT,
              can_upgrade_vitality ? PvM_StatsMenuHandler : nullptr);

    add_entry("", PMENU_ALIGN_CENTER); // Separator

    // Reset option (only if there are allocated stats)
    int32_t total_allocated = player->client->pers.pvm_max_ammo_level +
                             player->client->pers.pvm_vitality_level;
    if (total_allocated > 0)
    {
        add_entry("Reset Stats (Free)", PMENU_ALIGN_LEFT, PvM_StatsMenuHandler);
    }

    add_entry("", PMENU_ALIGN_CENTER); // Separator
    add_entry("Back", PMENU_ALIGN_LEFT, PvM_StatsMenuHandler);
    add_entry("Close", PMENU_ALIGN_LEFT, PvM_StatsMenuHandler);

    PMenu_Open(player, entries, -1, count, nullptr, nullptr);
}

// Award XP to player
void PvM_AwardExperience(edict_t* player, int32_t xp_amount)
{
    if (!player || !player->client)
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
        player->client->pers.skill_points++; // Grant 1 skill point per level

        // Show level-up message
        gi.LocClient_Print(player, PRINT_TYPEWRITER, nullptr,
                           "LEVEL UP!\\nYou are now level {}!\n+1 Skill Point\n",
                           current_level);

        gi.LocClient_Print(player, PRINT_HIGH, nullptr,
                           "*** LEVEL UP! You are now level {} (+1 skill point ***\n",
                           current_level);

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

        // Apply health bonus per level
        int32_t health_bonus_level = LEVELUP_PLAYER_ADDON_HEALTH;
        player->client->pers.max_health += health_bonus_level;
        player->client->resp.max_health += health_bonus_level;
        player->max_health += health_bonus_level;
        player->health += health_bonus_level; // Also heal

    // Get stat levels
    int32_t max_ammo_level = player->client->pers.pvm_max_ammo_level;
    int32_t vitality_level = player->client->pers.pvm_vitality_level;

    // Apply Max Ammo bonuses (from pvm_stats.json)
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

    // Apply Vitality bonuses (from pvm_stats.json)
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
