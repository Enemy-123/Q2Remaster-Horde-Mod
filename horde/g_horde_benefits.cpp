#include "g_horde_benefits.h"}
#include "../g_local.h" // Include g_local.h for gi, etc.

const char* GetPlayerName(const edict_t* player);

// --- The original, human-readable source data ---
// This should ONLY exist in the .cpp file.
const struct benefit_source_t {
    BenefitID id;
    const char* name;
    const char* center_msg;
    const char* chat_msg;
    const char* cvar_name;
    const char* cvar_value;
    int32_t min_level;
    int32_t max_level;
    float weight;
    BenefitID prereq; // Prerequisite benefit
    BenefitCategory category; // Benefit category
} BENEFITS_SRC[] = {
    {BenefitID::VAMPIRE, "vampire", "\n\n\n\nYou're covered in blood!\n\nVampire Ability\nENABLED!\n", "RECOVERING HEALTH FROM DAMAGE DONE!\n", "g_vampire", "1", 4, -1, 0.2f, BenefitID::NONE, BenefitCategory::ABILITY},
    {BenefitID::VAMPIRE_UPGRADED, "vampire upgraded", "\n\n\n\nIMPROVED VAMPIRE ABILITY\n", "RECOVERING HEALTH & ARMOR FROM DAMAGE DONE!\n", "g_vampire", "2", 24, -1, 0.1f, BenefitID::VAMPIRE, BenefitCategory::ABILITY},
    {BenefitID::AMMO_REGEN, "ammo regen", "\n\n\n\n\nAMMO REGEN\n\nENABLED!\n", "AMMO REGEN IS NOW ENABLED!\n", "g_ammoregen", "1", 8, -1, 0.15f, BenefitID::NONE, BenefitCategory::ABILITY},
    {BenefitID::AUTO_HASTE, "auto haste", "\n\n\n\nDUAL-FIRE IS RUNNING THROUGH YOUR VEINS\nFRAGGING WHILE HASTE\nWILL EXTEND QUAD DMG AND DUAL-FIRE TIME!\n", "AUTO-HASTE ENABLED!\n", "g_autohaste", "1", 9, -1, 0.15f, BenefitID::NONE, BenefitCategory::ABILITY},
    {BenefitID::START_ARMOR, "start armor", "\n\n\n\nSTARTING ARMOR\nENABLED!\n", "STARTING WITH 100 BODY-ARMOR!\n", "g_startarmor", "1", 9, -1, 0.1f, BenefitID::NONE, BenefitCategory::ABILITY},
    {BenefitID::TRACED_BULLETS, "Traced Bullets", "\n\n\n\nBULLETS\nUPGRADED!\n", "Piercing-PowerShield Bullets!\n", "g_tracedbullets", "1", 9, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::ENERGY_SHELLS, "Energy Shells", "\n\n\n\nSHELLS\nUPGRADED!\n", "Piercing-PowerShield Shells!\n", "g_energyshells", "1", 9, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::CLUSTER_PROX, "Cluster Prox", "\n\n\n\nIMPROVED PROX GRENADES\n", "Prox Cluster Launcher Enabled\n", "g_upgradeproxs", "1", 25, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::PIERCING_PLASMA, "Piercing Plasma", "\n\n\n\nPlasma-Beam Piercing Mode Enabled\n", "IMPROVED Plasma-Beam!\n", "g_piercingbeam", "1", 25, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::NAPALM_GRENADES, "Napalm GL", "\n\n\n\nIMPROVED GRENADE LAUNCHER!\n", "Napalm-Grenade Launcher Enabled\n", "g_bouncygl", "1", 25, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::BFG_SLIDE, "BFG Slide Mode", "\n\n\n\nBFG SLIDE MODE\nENABLED!\n", "BFG Slide Mode Active!\n", "g_bfgslide", "1", 12, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::BFG_GRAV_PULL, "BFG Gravity Pull", "\n\n\n\nBFG GRAVITY PULL\nENABLED!\n", "BFG Gravity Pull Active!\n", "g_bfgpull", "1", 12, -1, 0.15f, BenefitID::BFG_SLIDE, BenefitCategory::WEAPON}
};

// --- Compile-time transformation function ---
// This should also ONLY exist in the .cpp file.
constexpr BenefitsDataSoA create_benefits_soa() {
    BenefitsDataSoA soa_data{};
    soa_data.prerequisites.fill(BenefitID::NONE);

    for (const auto& src : BENEFITS_SRC) {
        size_t index = static_cast<size_t>(src.id);
        if (index < soa_data.NUM_BENEFITS) {
            soa_data.names[index] = src.name;
            soa_data.center_msgs[index] = src.center_msg;
            soa_data.chat_msgs[index] = src.chat_msg;
            soa_data.cvar_names[index] = src.cvar_name;
            soa_data.cvar_values[index] = src.cvar_value;
            soa_data.min_levels[index] = src.min_level;
            soa_data.max_levels[index] = src.max_level;
            soa_data.weights[index] = src.weight;
            soa_data.prerequisites[index] = src.prereq;
            soa_data.categories[index] = src.category;
        }
    }
    return soa_data;
}

// --- The single global instance of our  data ---
// This is the DEFINITION.
const BenefitsDataSoA g_benefitsData = create_benefits_soa();

// --- Global state variable DEFINITIONS ---
uint32_t obtained_benefits_mask = 0;
std::array<BenefitID, MAX_RECENT_BENEFITS> recent_benefits;
size_t recent_index = 0;
int32_t vampire_level = 0;
bool bfg_pull_active = false;

// --- Function DEFINITIONS ---

// Check if a benefit has been obtained using its ID
bool has_benefit(BenefitID id) noexcept {
    if (id == BenefitID::NONE) return true;
    return (obtained_benefits_mask & (1u << static_cast<uint8_t>(id))) != 0;
}

// Mark a benefit as obtained using its ID
void mark_benefit_obtained(BenefitID id) noexcept {
    obtained_benefits_mask |= (1u << static_cast<uint8_t>(id));
    recent_benefits[recent_index] = id;
    recent_index = (recent_index + 1) % MAX_RECENT_BENEFITS;
}

// Check if a benefit is eligible using its ID
static inline bool is_benefit_eligible(BenefitID id, int32_t wave) noexcept {
    size_t index = static_cast<size_t>(id);
    if (has_benefit(id)) {
        return false;
    }
    if (wave < g_benefitsData.min_levels[index] ||
       (g_benefitsData.max_levels[index] != -1 && wave > g_benefitsData.max_levels[index])) {
        return false;
    }
    if (!has_benefit(g_benefitsData.prerequisites[index])) {
        return false;
    }
    return true;
}

// Apply a benefit using its ID (legacy global system - mostly unused now)
static void apply_benefit(BenefitID id) {
    size_t index = static_cast<size_t>(id);

    // Legacy global system - mostly replaced by per-player system
    gi.LocBroadcast_Print(PRINT_CENTER, g_benefitsData.center_msgs[index]);
    gi.LocBroadcast_Print(PRINT_CHAT, g_benefitsData.chat_msgs[index]);

    // Keep vampire level tracking for backward compatibility
    if (id == BenefitID::VAMPIRE)
        vampire_level = 1;
    else if (id == BenefitID::VAMPIRE_UPGRADED)
        vampire_level = 2;
}

void ResetBenefits() noexcept {
    obtained_benefits_mask = 0;
    recent_benefits.fill(BenefitID::NONE);
    recent_index = 0;
    vampire_level = 0;
    bfg_pull_active = false;
    // Global cvars no longer used - benefits are per-player now
}

void CheckAndApplyBenefit(const int32_t wave) {
    if (wave % 4 != 0)
        return;

    struct eligible_benefit_t {
        BenefitID id;
        float weight;
    };
    std::array<eligible_benefit_t, BenefitsDataSoA::NUM_BENEFITS> eligible_benefits;
    size_t eligible_count = 0;
    float total_weight = 0.0f;

    for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS; ++i) {
        BenefitID current_id = static_cast<BenefitID>(i);
        if (is_benefit_eligible(current_id, wave)) {
            eligible_benefits[eligible_count].id = current_id;
            eligible_benefits[eligible_count].weight = g_benefitsData.weights[i];
            total_weight += g_benefitsData.weights[i];
            eligible_count++;
        }
    }

    if (eligible_count == 0)
        return;

    float const random_value = frandom() * total_weight;
    float cumulative_weight = 0.0f;
    BenefitID selected_id = eligible_benefits[0].id;

    for (size_t i = 0; i < eligible_count; i++) {
        cumulative_weight += eligible_benefits[i].weight;
        if (random_value <= cumulative_weight) {
            selected_id = eligible_benefits[i].id;
            break;
        }
    }

    apply_benefit(selected_id);
    mark_benefit_obtained(selected_id);
}

struct BonusMapping {
	std::string_view benefit_name;
	std::string_view display_text;
};

// Definition for the function used in horde_menu.cpp
std::string GetActiveBonusesString() {
    // This function can remain here as it's a higher-level utility
    // that depends on the core benefit system.
    // ... (implementation from your horde_menu.cpp)
    // ...
    // NOTE: You will need to move the implementation of this function
    // from horde_menu.cpp to here.
    
    // Define mappings from internal names to display names
	static const std::array<BonusMapping, 11> bonus_mappings = { {
		{"vampire upgraded", "Health & Armor Vampirism"},
		{"vampire", "Health Vampirism"},
		{"ammo regen", "Ammo Regen"},
		{"start armor", "Starting Armor"},
		{"auto haste", "Auto-Haste"},
		{"Cluster Prox Grenades", "Upgraded Prox Launcher"},
		{"Traced-Piercing Bullets", "Traced-Energy Bullets"},
		{"Napalm-Grenade Launcher", "Napalm-Grenade Launcher"},
		{"BFG Grav-Pull Lasers", "BFG Grav-Pull Lasers"},
		{"Piercing Plasma", "Piercing Plasma-Beam"},
		{"Energy Shells", "Energy Shells"}
	} };

    std::vector<std::string_view> active_bonuses;
	active_bonuses.reserve(bonus_mappings.size());

    bool has_vampire_upgraded = has_benefit(BenefitID::VAMPIRE_UPGRADED);

    for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS; ++i) {
        BenefitID id = static_cast<BenefitID>(i);
        if (id == BenefitID::VAMPIRE && has_vampire_upgraded) {
            continue; // Skip base vampire if upgraded is active
        }
        if (has_benefit(id)) {
            // Find the corresponding display text
            for(const auto& mapping : bonus_mappings) {
                if (strcmp(g_benefitsData.names[i], mapping.benefit_name.data()) == 0) {
                    active_bonuses.push_back(mapping.display_text);
                    break;
                }
            }
        }
    }

    if (active_bonuses.empty()) {
		return "";
	}

    // Use your StringBuilder or std::string to format the result
    std::string result;
    result.reserve(active_bonuses.size() * 30);

	for (size_t i = 0; i < active_bonuses.size(); ++i) {
		result += "* ";
		result += active_bonuses[i];
		if (i < active_bonuses.size() - 1) {
			result += "\n";
		}
	}

	return result;
}

// --- Per-Player Benefit System Implementation ---

// Check if player has a specific benefit
bool PlayerHasBenefit(edict_t* player, BenefitID benefit_id) {
    if (!player || !player->client) return false;

    auto category = g_benefitsData.categories[static_cast<size_t>(benefit_id)];
    uint32_t mask = (category == BenefitCategory::ABILITY) ?
                    player->client->pers.active_abilities_mask :
                    player->client->pers.active_weapons_mask;

    return (mask & (1u << static_cast<uint8_t>(benefit_id))) != 0;
}

// Check if player has ability (category check)
bool PlayerHasAbility(edict_t* player, BenefitID ability_id) {
    if (g_benefitsData.categories[static_cast<size_t>(ability_id)] != BenefitCategory::ABILITY) {
        return false;
    }
    return PlayerHasBenefit(player, ability_id);
}

// Check if player has weapon upgrade (category check)
bool PlayerHasWeaponUpgrade(edict_t* player, BenefitID weapon_id) {
    if (g_benefitsData.categories[static_cast<size_t>(weapon_id)] != BenefitCategory::WEAPON) {
        return false;
    }
    return PlayerHasBenefit(player, weapon_id);
}

// Activate benefit for player
void PlayerActivateBenefit(edict_t* player, BenefitID benefit_id) {
    if (!player || !player->client) return;

    auto category = g_benefitsData.categories[static_cast<size_t>(benefit_id)];
    uint32_t* mask = (category == BenefitCategory::ABILITY) ?
                     &player->client->pers.active_abilities_mask :
                     &player->client->pers.active_weapons_mask;

    *mask |= (1u << static_cast<uint8_t>(benefit_id));
    player->client->pers.purchased_benefits_mask |= (1u << static_cast<uint8_t>(benefit_id));

    // Handle special cases
    if (benefit_id == BenefitID::BFG_SLIDE) {
        player->client->pers.bfg_mode = BFGMode::SLIDE;
        gi.LocClient_Print(player, PRINT_HIGH, "^2BFG Slide Mode enabled!\n");
    } else if (benefit_id == BenefitID::BFG_GRAV_PULL) {
        player->client->pers.bfg_mode = BFGMode::GRAV_PULL;
        gi.LocClient_Print(player, PRINT_HIGH, "^2BFG Gravity Pull enabled!\n");
    }
}

// Deactivate benefit for player
void PlayerDeactivateBenefit(edict_t* player, BenefitID benefit_id) {
    if (!player || !player->client) return;

    auto category = g_benefitsData.categories[static_cast<size_t>(benefit_id)];
    uint32_t* mask = (category == BenefitCategory::ABILITY) ?
                     &player->client->pers.active_abilities_mask :
                     &player->client->pers.active_weapons_mask;

    *mask &= ~(1u << static_cast<uint8_t>(benefit_id));
}

// Specific benefit helpers (replace global cvar checks)
bool PlayerHasVampire(edict_t* player) {
    return PlayerHasBenefit(player, BenefitID::VAMPIRE) ||
           PlayerHasBenefit(player, BenefitID::VAMPIRE_UPGRADED);
}

bool PlayerHasAmmoRegen(edict_t* player) {
    return PlayerHasBenefit(player, BenefitID::AMMO_REGEN);
}

bool PlayerHasAutoHaste(edict_t* player) {
    return PlayerHasBenefit(player, BenefitID::AUTO_HASTE);
}

bool PlayerHasStartArmor(edict_t* player) {
    return PlayerHasBenefit(player, BenefitID::START_ARMOR);
}

bool PlayerHasTracedBullets(edict_t* player) {
    return PlayerHasBenefit(player, BenefitID::TRACED_BULLETS);
}

bool PlayerHasEnergyShells(edict_t* player) {
    return PlayerHasBenefit(player, BenefitID::ENERGY_SHELLS);
}

bool PlayerHasClusterProx(edict_t* player) {
    return PlayerHasBenefit(player, BenefitID::CLUSTER_PROX);
}

bool PlayerHasPiercingPlasma(edict_t* player) {
    return PlayerHasBenefit(player, BenefitID::PIERCING_PLASMA);
}

bool PlayerHasNapalmGL(edict_t* player) {
    return PlayerHasBenefit(player, BenefitID::NAPALM_GRENADES);
}

// BFG mode helpers
bool PlayerHasBFGSlide(edict_t* player) {
    if (!player || !player->client) return false; // Default to normal mode
    return player->client->pers.bfg_mode >= BFGMode::SLIDE;
}

bool PlayerHasBFGPull(edict_t* player) {
    if (!player || !player->client) return false;
    return player->client->pers.bfg_mode == BFGMode::GRAV_PULL;
}

BFGMode PlayerGetBFGMode(edict_t* player) {
    if (!player || !player->client) return BFGMode::NORMAL;
    return player->client->pers.bfg_mode;
}

void PlayerSetBFGMode(edict_t* player, BFGMode mode) {
    if (!player || !player->client) return;
    player->client->pers.bfg_mode = mode;
}

// Point management
void PlayerEarnAbilityPoints(edict_t* player, int32_t points) {
    if (!player || !player->client) return;
    player->client->pers.ability_points += points;
}

void PlayerEarnWeaponPoints(edict_t* player, int32_t points) {
    if (!player || !player->client) return;
    player->client->pers.weapon_points += points;
}

bool PlayerCanAffordBenefit(edict_t* player, BenefitID benefit_id, int32_t cost) {
    if (!player || !player->client) return false;

    auto category = g_benefitsData.categories[static_cast<size_t>(benefit_id)];
    int32_t available_points = (category == BenefitCategory::ABILITY) ?
                               player->client->pers.ability_points :
                               player->client->pers.weapon_points;

    return available_points >= cost;
}

void PlayerSpendPoints(edict_t* player, BenefitID benefit_id, int32_t cost) {
    if (!player || !player->client) return;

    auto category = g_benefitsData.categories[static_cast<size_t>(benefit_id)];
    int32_t* points = (category == BenefitCategory::ABILITY) ?
                      &player->client->pers.ability_points :
                      &player->client->pers.weapon_points;

    *points -= cost;
}

// Helper function to auto-buy from a specific category
static void AutoBuyCategory(edict_t* player, BenefitCategory category) {
    if (!player || !player->client) return;

    int32_t* points = (category == BenefitCategory::ABILITY) ?
                      &player->client->pers.ability_points :
                      &player->client->pers.weapon_points;

    // Try to buy affordable benefits with prerequisites met
    for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS && *points > 0; ++i) {
        if (g_benefitsData.categories[i] != category) continue;

        BenefitID benefit_id = static_cast<BenefitID>(i);

        // Skip if already owned
        if (PlayerHasBenefit(player, benefit_id)) continue;

        // Set costs based on benefit type
        int32_t cost = 1; // Default cost
        if (category == BenefitCategory::WEAPON &&
            (benefit_id == BenefitID::BFG_GRAV_PULL || benefit_id == BenefitID::CLUSTER_PROX)) {
            cost = 3;
        }

        // Check if can afford
        if (*points < cost) continue;

        // Check wave requirements
        int32_t min_wave = g_benefitsData.min_levels[i];
        if (current_wave_level < min_wave) continue;

        // Check prerequisites
        auto prereq = g_benefitsData.prerequisites[i];
        if (prereq != BenefitID::NONE && !PlayerHasBenefit(player, prereq)) {
            continue;
        }

        // Purchase the benefit
        if (PlayerPurchaseBenefit(player, benefit_id, cost)) {
            // TODO: Track auto-purchased benefits for potential refund if needed
            gi.LocClient_Print(player, PRINT_HIGH, "^6{} auto-bought: {}\n", GetPlayerName(player), g_benefitsData.names[i]);
        }
    }
}

// Auto-buy system implementation
void CheckPlayerAutoBuy(edict_t* player) {
    if (!player || !player->client) return;

    // Check if enough time has passed since last auto-buy check
    if (level.time < player->client->pers.last_auto_buy_check + 2_sec) {
        return;
    }

    player->client->pers.last_auto_buy_check = level.time;

    // Auto-buy abilities if enabled and player has points
    if (player->client->pers.auto_buy_abilities && player->client->pers.ability_points > 0) {
        AutoBuyCategory(player, BenefitCategory::ABILITY);
    }

    // Auto-buy weapons if enabled and player has points
    if (player->client->pers.auto_buy_weapons && player->client->pers.weapon_points > 0) {
        AutoBuyCategory(player, BenefitCategory::WEAPON);
    }
}

// Benefit purchasing with per-player messages
bool PlayerPurchaseBenefit(edict_t* player, BenefitID benefit_id, int32_t cost) {
    if (!player || !player->client) return false;

    // Check if player can afford it
    if (!PlayerCanAffordBenefit(player, benefit_id, cost)) {
        gi.LocClient_Print(player, PRINT_HIGH, "^1Not enough points!\n");
        return false;
    }

    // Check if already owned
    if (PlayerHasBenefit(player, benefit_id)) {
        gi.LocClient_Print(player, PRINT_HIGH, "^3Already owned!\n");
        return false;
    }

    // Check wave requirements
    int32_t min_wave = g_benefitsData.min_levels[static_cast<size_t>(benefit_id)];
    if (current_wave_level < min_wave) {
        gi.LocClient_Print(player, PRINT_HIGH, "^1Available at wave {}!\n", min_wave);
        return false;
    }

    // Check prerequisites
    auto prereq = g_benefitsData.prerequisites[static_cast<size_t>(benefit_id)];
    if (prereq != BenefitID::NONE && !PlayerHasBenefit(player, prereq)) {
        gi.LocClient_Print(player, PRINT_HIGH, "^1Prerequisite not met!\n");
        return false;
    }

    // Purchase the benefit
    PlayerSpendPoints(player, benefit_id, cost);
    PlayerActivateBenefit(player, benefit_id);
    PlayerShowBenefitMessage(player, benefit_id);

    return true;
}

// Show benefit message to specific player only
void PlayerShowBenefitMessage(edict_t* player, BenefitID benefit_id) {
    if (!player || !player->client) return;

    size_t index = static_cast<size_t>(benefit_id);

    // Send center print message (big screen message)
    gi.LocCenter_Print(player, "{}", g_benefitsData.center_msgs[index]);

    // Send chat message
    gi.LocClient_Print(player, PRINT_HIGH, "^2{}\n", g_benefitsData.chat_msgs[index]);
}

// Process wave rewards - replaces the old CheckAndApplyBenefit for point distribution
void ProcessWaveRewards(int32_t wave) {
    // Distribute points to all active players
    for (auto player : active_players()) {
        if (!player || !player->client) continue;

        // Ability points every 4 waves starting from wave 4
        if (wave >= 4 && (wave % 4) == 0) {
            PlayerEarnAbilityPoints(player, 1);
            gi.LocClient_Print(player, PRINT_HIGH, "^2+1 Ability Point! ^7(Total: {})\n",
                      player->client->pers.ability_points);
        }

        // Weapon points every 8 waves starting from wave 8
        if (wave >= 8 && (wave % 8) == 0) {
            PlayerEarnWeaponPoints(player, 1);
            gi.LocClient_Print(player, PRINT_HIGH, "^3+1 Weapon Point! ^7(Total: {})\n",
                      player->client->pers.weapon_points);
        }

        // Check for auto-buy after earning points
        CheckPlayerAutoBuy(player);
    }
}

// Restore all player points by clearing all benefits
void PlayerRestoreAllPoints(edict_t* player) {
    if (!player || !player->client) return;

    // Calculate how many points should have been earned by current wave
    int32_t waves_completed = current_wave_level;
    int32_t expected_ability_points = 0;
    int32_t expected_weapon_points = 0;

    // Ability points awarded every 4 waves starting from wave 4
    if (waves_completed >= 4) {
        expected_ability_points = (waves_completed / 4);
    }

    // Weapon points awarded every 8 waves starting from wave 8
    if (waves_completed >= 8) {
        expected_weapon_points = (waves_completed / 8);
    }

    // Clear all benefits
    player->client->pers.active_abilities_mask = 0;
    player->client->pers.active_weapons_mask = 0;
    player->client->pers.purchased_benefits_mask = 0;

    // Restore full points based on current wave
    player->client->pers.ability_points = expected_ability_points;
    player->client->pers.weapon_points = expected_weapon_points;

    // Reset BFG mode to default
    player->client->pers.bfg_mode = BFGMode::NORMAL;

    gi.LocClient_Print(player, PRINT_HIGH,
        "^2All benefits cleared! ^7Restored: ^3{} ability points, {} weapon points\n",
        expected_ability_points, expected_weapon_points);
    gi.LocCenter_Print(player, "All upgrades restored!\nChoose your path again!");
}

// Refund all auto-purchased benefits when player manually disables auto-buy
void PlayerRefundAutoPurchasedBenefits(edict_t* player) {
    if (!player || !player->client) return;

    // Mark that player has manually disabled auto-buy
    player->client->pers.has_manually_disabled_auto_buy = true;

    // Calculate how many points should have been earned by now
    int32_t waves_completed = current_wave_level;
    int32_t expected_ability_points = 0;
    int32_t expected_weapon_points = 0;

    // Ability points awarded every 4 waves starting from wave 4
    if (waves_completed >= 4) {
        expected_ability_points = (waves_completed / 4);
    }

    // Weapon points awarded every 8 waves starting from wave 8
    if (waves_completed >= 8) {
        expected_weapon_points = (waves_completed / 8);
    }

    // Count how many benefits the player has purchased
    int32_t purchased_ability_count = 0;
    int32_t purchased_weapon_count = 0;

    for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS; ++i) {
        if (player->client->pers.purchased_benefits_mask & (1u << i)) {
            if (g_benefitsData.categories[i] == BenefitCategory::ABILITY) {
                purchased_ability_count++;
            } else {
                purchased_weapon_count++;
            }
        }
    }

    // Calculate actual refund (total expected - currently available - manually purchased)
    int32_t refunded_ability_points = expected_ability_points - player->client->pers.ability_points - purchased_ability_count;
    int32_t refunded_weapon_points = expected_weapon_points - player->client->pers.weapon_points - purchased_weapon_count;

    // Only refund if there were auto-purchases (positive refund)
    if (refunded_ability_points > 0) {
        player->client->pers.ability_points += refunded_ability_points;
    }

    if (refunded_weapon_points > 0) {
        player->client->pers.weapon_points += refunded_weapon_points;
    }

    // NOTE: We do NOT clear benefits masks here anymore!
    // Players keep their manually purchased benefits.

    // Show refund message
    if (refunded_ability_points > 0 || refunded_weapon_points > 0) {
        gi.LocClient_Print(player, PRINT_HIGH,
            "^2Auto-buy disabled! Refunded: ^3{} ability points, {} weapon points^7. Choose your own path!\n",
            refunded_ability_points, refunded_weapon_points);
        gi.LocCenter_Print(player, "Auto-buy disabled!\nAll auto-spent points refunded!");
    } else {
        gi.LocClient_Print(player, PRINT_HIGH, "^3Auto-buy disabled. No auto-purchases to refund.\n");
    }
}