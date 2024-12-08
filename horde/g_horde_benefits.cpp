#include "g_horde_benefits.h"
#include "../g_local.h"

// Global state variables - kept for compatibility with existing code
uint32_t obtained_benefits_mask = 0;
uint8_t recent_benefits[MAX_RECENT_BENEFITS] = { 0xFF, 0xFF, 0xFF };
size_t recent_index = 0;
int32_t vampire_level = 0;
bool bfg_pull_active = false;

// Array of benefits with const correctness
const benefit_t BENEFITS[MAX_BENEFITS] = {
    {
        "vampire",
        "\n\n\nYou're covered in blood!\n\nVampire Ability\nENABLED!\n",
        "RECOVERING HEALTH FROM DAMAGE DONE!\n",
        "g_vampire", "1",
        4, -1, 0.2f
    },
    {
        "vampire upgraded",
        "\n\n\n\nIMPROVED VAMPIRE ABILITY\n",
        "RECOVERING HEALTH & ARMOR FROM DAMAGE DONE!\n",
        "g_vampire", "2",
        24, -1, 0.1f
    },
    {
        "ammo regen",
        "AMMO REGEN\n\nENABLED!\n",
        "AMMO REGEN IS NOW ENABLED!\n",
        "g_ammoregen", "1",
        8, -1, 0.15f
    },
    {
        "auto haste",
        "\n\nDUAL-FIRE IS RUNNING THROUGH YOUR VEINS\nFRAGGING WHILE HASTE\nWILL EXTEND QUAD DMG AND DUAL-FIRE TIME!\n",
        "AUTO-HASTE ENABLED!\n",
        "g_autohaste", "1",
        9, -1, 0.15f
    },
    {
        "start armor",
        "\n\n\nSTARTING ARMOR\nENABLED!\n",
        "STARTING WITH 100 BODY-ARMOR!\n",
        "g_startarmor", "1",
        9, -1, 0.1f
    },
    {
        "Traced-Piercing Bullets",
        "\n\n\n\nBULLETS\nUPGRADED!\n",
        "Piercing-PowerShield Bullets!\n",
        "g_tracedbullets", "1",
        9, -1, 0.2f
    },
    {
        "Cluster Prox Grenades",
        "\n\n\n\nIMPROVED PROX GRENADES\n",
        "Prox Cluster Launcher Enabled\n",
        "g_upgradeproxs", "1",
        25, -1, 0.2f
    },
    {
        "Piercing Plasma",
        "\n\n\n\nPlasma-Beam is ready to pierce some strogg flesh\n",
        "IMPROVED Plasma-Beam!\n",
        "g_piercingbeam", "1",
        25, -1, 0.2f
    },
    {
        "Napalm-Grenade Launcher",
        "\n\n\n\nIMPROVED GRENADE LAUNCHER!\n",
        "Napalm-Grenade Launcher Enabled\n",
        "g_bouncygl", "1",
        25, -1, 0.2f
    },
    {
        "BFG Grav-Pull Lasers",
        "\n\n\n\nBFG LASERS UPGRADED!\n",
        "BFG Grav-Pull Lasers Enabled\n",
        "g_bfgpull", "1",
        35, -1, 0.2f
    }
};

// Helper functions
inline bool has_benefit(size_t index) noexcept {
    return (obtained_benefits_mask & (1u << index)) != 0;
}

static inline void mark_benefit_obtained(size_t index) noexcept {
    obtained_benefits_mask |= (1u << index);
    recent_benefits[recent_index] = static_cast<uint8_t>(index);
    recent_index = (recent_index + 1) % 3;
}

static inline bool is_benefit_eligible(const benefit_t& benefit, 
                                     int32_t wave, size_t index) noexcept {
    // Special handling for vampire upgrade prereq
    if (strcmp(benefit.name, "vampire upgraded") == 0) {
        bool has_vampire = false;
        for (size_t i = 0; i < MAX_BENEFITS; i++) {
            if (strcmp(BENEFITS[i].name, "vampire") == 0) {
                has_vampire = has_benefit(i);
                break;
            }
        }
        if (!has_vampire) return false;
    }

    return wave >= benefit.min_level &&
           (benefit.max_level == -1 || wave <= benefit.max_level) &&
           !has_benefit(index);
}

static void apply_benefit(const benefit_t& benefit) {
    // Handle BFG Pull special case
    if (strcmp(benefit.name, "BFG Grav-Pull Lasers") == 0) {
        bfg_pull_active = !bfg_pull_active;

        if (bfg_pull_active) {
            gi.cvar_set("g_bfgpull", "1");   
            gi.cvar_set("g_bfgslide", "0");
            gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\nBFG LASERS UPGRADED!\n");
            gi.LocBroadcast_Print(PRINT_CHAT, "BFG Grav-Pull Lasers Enabled\n");
        } else {
            gi.cvar_set("g_bfgpull", "0");
            gi.cvar_set("g_bfgslide", "1"); 
            gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\nBFG LASERS NORMAL MODE\n");
            gi.LocBroadcast_Print(PRINT_CHAT, "BFG Slide Mode Active\n");
        }
        return;
    }

    // Apply standard benefit
    gi.cvar_set(benefit.cvar_name, benefit.cvar_value);
    gi.LocBroadcast_Print(PRINT_CENTER, benefit.center_msg);
    gi.LocBroadcast_Print(PRINT_CHAT, benefit.chat_msg);

    // Handle vampire benefits
    if (strcmp(benefit.name, "vampire") == 0)
        vampire_level = 1;
    else if (strcmp(benefit.name, "vampire upgraded") == 0) 
        vampire_level = 2;
}

// Public interface implementations
void ResetBenefits() noexcept {
    obtained_benefits_mask = 0;
    for (size_t i = 0; i < MAX_RECENT_BENEFITS; i++)
        recent_benefits[i] = 0xFF;
    recent_index = 0;
    vampire_level = 0;
    bfg_pull_active = false;
    gi.cvar_set("g_bfgpull", "0");
    gi.cvar_set("g_bfgslide", "1");
}

void CheckAndApplyBenefit(const int32_t wave) {
    // Only apply benefits every 4 waves
    if (wave % 4 != 0)
        return;

    // Calculate eligible benefits
    float total_weight = 0.0f;
    size_t eligible_count = 0;

    struct eligible_benefit_t {
        size_t index;
        float weight;
    } eligible_benefits[MAX_BENEFITS];

    // Collect eligible benefits and weights
    for (size_t i = 0; i < MAX_BENEFITS; i++) {
        if (is_benefit_eligible(BENEFITS[i], wave, i)) {
            eligible_benefits[eligible_count].index = i;
            eligible_benefits[eligible_count].weight = BENEFITS[i].weight;
            total_weight += BENEFITS[i].weight;
            eligible_count++;
        }
    }

    if (eligible_count == 0)
        return;

    // Select benefit based on weight
    float const random_value = frandom() * total_weight;
    float cumulative_weight = 0.0f;
    size_t selected_index = eligible_benefits[0].index;

    for (size_t i = 0; i < eligible_count; i++) {
        cumulative_weight += eligible_benefits[i].weight;
        if (random_value <= cumulative_weight) {
            selected_index = eligible_benefits[i].index;
            break;
        }
    }

    // Apply selected benefit
    apply_benefit(BENEFITS[selected_index]);
    mark_benefit_obtained(selected_index);
}