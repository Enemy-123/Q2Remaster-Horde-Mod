#include "g_horde_benefits.h"
#include "../g_local.h" // Include g_local.h for gi, etc.

const char* GetPlayerName(const edict_t* player);

// --- The original, human-readable source data ---
// This should ONLY exist in the .cpp file.
const struct benefit_source_t {
    BenefitID id;
    const char* name;
    const char* center_msg;
    const char* chat_msg;
    int32_t min_level;
    int32_t max_level;
    float weight;
    BenefitID prereq; // Prerequisite benefit
    BenefitCategory category; // Benefit category
} BENEFITS_SRC[] = {
    {BenefitID::VAMPIRE, "vampire", "\n\n\n\nYou're covered in blood!\n\nVampire Ability\nENABLED!\n", "RECOVERING HEALTH FROM DAMAGE DONE!\n", 4, -1, 0.2f, BenefitID::NONE, BenefitCategory::ABILITY},
    {BenefitID::VAMPIRE_UPGRADED, "vampire upgraded", "\n\n\n\nIMPROVED VAMPIRE ABILITY\n", "RECOVERING HEALTH & ARMOR FROM DAMAGE DONE!\n", 24, -1, 0.1f, BenefitID::VAMPIRE, BenefitCategory::ABILITY},
    {BenefitID::AMMO_REGEN, "ammo regen", "\n\n\n\n\nAMMO REGEN\n\nENABLED!\n", "AMMO REGEN IS NOW ENABLED!\n", 8, -1, 0.15f, BenefitID::NONE, BenefitCategory::ABILITY},
    {BenefitID::AUTO_HASTE, "auto haste", "\n\n\n\nDUAL-FIRE IS RUNNING THROUGH YOUR VEINS\nFRAGGING WHILE HASTE\nWILL EXTEND QUAD DMG AND DUAL-FIRE TIME!\n", "AUTO-HASTE ENABLED!\n", 9, -1, 0.15f, BenefitID::NONE, BenefitCategory::ABILITY},
    {BenefitID::START_ARMOR, "start armor", "\n\n\n\nSTARTING ARMOR\nENABLED!\n", "STARTING WITH 100 BODY-ARMOR!\n", 9, -1, 0.1f, BenefitID::NONE, BenefitCategory::ABILITY},
    {BenefitID::HA_PICKUP, "H/A Pickup", "\n\n\n\nENHANCED HEALTH & ARMOR PICKUPS\nENABLED!\n", "Health & Armor pickups increased by 60%!\n", 1, -1, 0.15f, BenefitID::NONE, BenefitCategory::ABILITY},
    {BenefitID::TRACED_BULLETS, "Traced Bullets", "\n\n\n\nBULLETS\nUPGRADED!\n", "Piercing-PowerShield Bullets!\n", 9, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::ENERGY_SHELLS, "Energy Shells", "\n\n\n\nSHELLS\nUPGRADED!\n", "Piercing-PowerShield Shells!\n", 9, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::CLUSTER_PROX, "Cluster Prox", "\n\n\n\nIMPROVED PROX GRENADES\n", "Prox Cluster Launcher Enabled\n", 25, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::PIERCING_PLASMA, "Piercing Plasma", "\n\n\n\nPlasma-Beam Piercing Mode Enabled\n", "IMPROVED Plasma-Beam!\n", 25, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::NAPALM_GRENADES, "Napalm GL", "\n\n\n\nIMPROVED GRENADE LAUNCHER!\n", "Napalm-Grenade Launcher Enabled\n", 25, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::BFG_SLIDE, "BFG Slide Mode", "\n\n\n\nBFG SLIDE MODE\nENABLED!\n", "BFG Slide Mode Active!\n", 12, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON},
    {BenefitID::BFG_GRAV_PULL, "BFG Gravity Pull", "\n\n\n\nBFG GRAVITY PULL\nENABLED!\n", "BFG Gravity Pull Active!\n", 12, -1, 0.15f, BenefitID::BFG_SLIDE, BenefitCategory::WEAPON},
    {BenefitID::TESLA_CHAIN_LIGHTNING, "Tesla Chain Lightning", "\n\n\n\nTESLA CHAIN LIGHTNING\nENABLED!\n", "Tesla Chain Lightning Upgrade Active!\n", 1, -1, 0.2f, BenefitID::NONE, BenefitCategory::WEAPON}
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

void CheckBotAndApplyBenefit(const int32_t wave) {
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
	std::string_view display_text; // The full name for menus
	std::string_view short_name = "";   // The shorter name for the scoreboard (optional, defaults to empty)
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
	static const std::array<BonusMapping, 12> bonus_mappings = { {
		{"vampire upgraded", "Health & Armor Vampirism"},
		{"vampire", "Health Vampirism"},
		{"ammo regen", "Ammo Regen"},
		{"start armor", "Starting Armor"},
		{"auto haste", "Auto-Haste"},
		{"Tesla Chain Lightning", "Tesla Chain Lightning"},
		{"Cluster Prox", "Upgraded Prox Launcher"},
		{"Traced Bullets", "Traced-Energy Bullets"},
		{"Napalm GL", "Napalm-Grenade Launcher"},
		{"BFG Gravity Pull", "BFG Grav-Pull Lasers"},
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

    // Use std::string to format the result
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

// Get active bonuses string for a specific player (per-player version)
std::string GetPlayerActiveBonusesString(edict_t* player) {
    if (!player || !player->client) {
        return "";
    }

    // Define mappings from internal names to display names
    static const std::array<BonusMapping, 13> bonus_mappings = { {
        {"vampire upgraded", "Health & Armor Vampirism"},
        {"vampire", "Health Vampirism"},
        {"ammo regen", "Ammo Regen"},
        {"start armor", "Starting Armor"},
        {"H/A Pickup", "H/A Pickup"},
        {"auto haste", "Auto-Haste"},
        {"Tesla Chain Lightning", "Tesla Chain Lightning"},
        {"Cluster Prox", "Upgraded Prox Launcher"},
        {"Traced Bullets", "Traced-Energy Bullets"},
        {"Napalm GL", "Napalm-Grenade Launcher"},
        {"BFG Gravity Pull", "BFG Grav-Pull Lasers"},
        {"Piercing Plasma", "Piercing Plasma-Beam"},
        {"Energy Shells", "Energy Shells"}
    } };

    std::vector<std::string_view> active_bonuses;
    active_bonuses.reserve(bonus_mappings.size());

    bool has_vampire_upgraded = ClassicPlayerHasBenefit(player, BenefitID::VAMPIRE_UPGRADED);

    for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS; ++i) {
        BenefitID id = static_cast<BenefitID>(i);
        if (id == BenefitID::VAMPIRE && has_vampire_upgraded) {
            continue; // Skip base vampire if upgraded is active
        }
        if (ClassicPlayerHasBenefit(player, id)) {
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

    // Use std::string to format the result
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
bool ClassicPlayerHasBenefit(edict_t* player, BenefitID benefit_id) {
    if (!player || !player->client) return false;

    // In Classic Mode (vortex=0), everyone uses benefits
    // In RPG Mode (vortex=1), only bots use benefits (humans use skills)
    if (g_vortex->integer != 0 && !(player->svflags & SVF_BOT)) {
        return false;
    }

    auto category = g_benefitsData.categories[static_cast<size_t>(benefit_id)];
    uint32_t mask = (category == BenefitCategory::ABILITY) ?
                    player->client->pers.active_abilities_mask :
                    player->client->pers.active_weapons_mask;

    // Adjust bit position for weapons (start from 0 in weapons mask)
    uint8_t bit_pos = static_cast<uint8_t>(benefit_id);
    if (category == BenefitCategory::WEAPON) {
        // First weapon benefit is TRACED_BULLETS (value 5)
        bit_pos = static_cast<uint8_t>(benefit_id) - static_cast<uint8_t>(BenefitID::TRACED_BULLETS);
    }

    return (mask & (1u << bit_pos)) != 0;
}

// Check if player has purchased a benefit (regardless of activation status)
bool ClassicPlayerHasPurchasedBenefit(edict_t* player, BenefitID benefit_id) {
    if (!player || !player->client) return false;

    // In Classic Mode (vortex=0), everyone uses benefits
    // In RPG Mode (vortex=1), only bots use benefits (humans use skills)
    if (g_vortex->integer != 0 && !(player->svflags & SVF_BOT)) {
        return false;
    }

    uint32_t mask = player->client->pers.purchased_benefits_mask;
    uint8_t bit_pos = static_cast<uint8_t>(benefit_id);
    
    return (mask & (1u << bit_pos)) != 0;
}

// Check if player has ability (category check)
bool ClassicPlayerHasBenefitAbility(edict_t* player, BenefitID ability_id) {
    if (g_benefitsData.categories[static_cast<size_t>(ability_id)] != BenefitCategory::ABILITY) {
        return false;
    }
    return ClassicPlayerHasBenefit(player, ability_id);
}

// Check if player has weapon upgrade (category check)
bool ClassicPlayerHasBenefitWeaponUpgrade(edict_t* player, BenefitID weapon_id) {
    if (g_benefitsData.categories[static_cast<size_t>(weapon_id)] != BenefitCategory::WEAPON) {
        return false;
    }
    return ClassicPlayerHasBenefit(player, weapon_id);
}

// Activate benefit for player
void BotActivateBenefit(edict_t* player, BenefitID benefit_id) {
    if (!player || !player->client) return;

    auto category = g_benefitsData.categories[static_cast<size_t>(benefit_id)];
    uint32_t* mask = (category == BenefitCategory::ABILITY) ?
                     &player->client->pers.active_abilities_mask :
                     &player->client->pers.active_weapons_mask;

    // Adjust bit position for weapons (start from 0 in weapons mask)
    uint8_t bit_pos = static_cast<uint8_t>(benefit_id);
    if (category == BenefitCategory::WEAPON) {
        // First weapon benefit is TRACED_BULLETS (value 5)
        bit_pos = static_cast<uint8_t>(benefit_id) - static_cast<uint8_t>(BenefitID::TRACED_BULLETS);
    }

    *mask |= (1u << bit_pos);
    player->client->pers.purchased_benefits_mask |= (1u << static_cast<uint8_t>(benefit_id));

    // Benefit activated successfully

    // Handle special cases
    if (benefit_id == BenefitID::BFG_SLIDE) {
        player->client->pers.bfg_mode = BFGMode::SLIDE;
        gi.LocClient_Print(player, PRINT_HIGH, "BFG Slide Mode enabled!\n");
    } else if (benefit_id == BenefitID::BFG_GRAV_PULL) {
        player->client->pers.bfg_mode = BFGMode::GRAV_PULL;
        gi.LocClient_Print(player, PRINT_HIGH, "BFG Gravity Pull enabled!\n");
    }
}

// Deactivate benefit for player
void BotDeactivateBenefit(edict_t* player, BenefitID benefit_id) {
    if (!player || !player->client) return;

    auto category = g_benefitsData.categories[static_cast<size_t>(benefit_id)];
    uint32_t* mask = (category == BenefitCategory::ABILITY) ?
                     &player->client->pers.active_abilities_mask :
                     &player->client->pers.active_weapons_mask;

    // Adjust bit position for weapons (start from 0 in weapons mask)
    uint8_t bit_pos = static_cast<uint8_t>(benefit_id);
    if (category == BenefitCategory::WEAPON) {
        // First weapon benefit is TRACED_BULLETS (value 5)
        bit_pos = static_cast<uint8_t>(benefit_id) - static_cast<uint8_t>(BenefitID::TRACED_BULLETS);
    }

    *mask &= ~(1u << bit_pos);
}

// Specific benefit helpers (replace global cvar checks)
bool ClassicPlayerHasBenefitVampire(edict_t* player) {
    return ClassicPlayerHasBenefit(player, BenefitID::VAMPIRE) ||
           ClassicPlayerHasBenefit(player, BenefitID::VAMPIRE_UPGRADED);
}

bool ClassicPlayerHasBenefitAmmoRegen(edict_t* player) {
    return ClassicPlayerHasBenefit(player, BenefitID::AMMO_REGEN);
}

bool ClassicPlayerHasBenefitAutoHaste(edict_t* player) {
    if (!player || !player->client) return false;

    // In Classic Mode (vortex=0), everyone uses benefit system
    if (g_vortex->integer == 0) {
        return ClassicPlayerHasBenefit(player, BenefitID::AUTO_HASTE);
    }

    // In RPG Mode (vortex=1), bots use benefits, humans use skills
    if (player->svflags & SVF_BOT) {
        return ClassicPlayerHasBenefit(player, BenefitID::AUTO_HASTE);
    } else {
        return player->client->pers.skills.auto_haste;
    }
}

bool ClassicPlayerHasBenefitStartArmor(edict_t* player) {
    return ClassicPlayerHasBenefit(player, BenefitID::START_ARMOR);
}

bool ClassicPlayerHasBenefitHAPickup(edict_t* player) {
    return ClassicPlayerHasBenefit(player, BenefitID::HA_PICKUP);
}

bool ClassicPlayerHasBenefitTracedBullets(edict_t* player) {
    return ClassicPlayerHasBenefit(player, BenefitID::TRACED_BULLETS);
}

bool ClassicPlayerHasBenefitEnergyShells(edict_t* player) {
    return ClassicPlayerHasBenefit(player, BenefitID::ENERGY_SHELLS);
}

bool ClassicPlayerHasBenefitClusterProx(edict_t* player) {
    return ClassicPlayerHasBenefit(player, BenefitID::CLUSTER_PROX);
}

bool ClassicPlayerHasBenefitPiercingPlasma(edict_t* player) {
    return ClassicPlayerHasBenefit(player, BenefitID::PIERCING_PLASMA);
}

bool ClassicPlayerHasBenefitNapalmGL(edict_t* player) {
    return ClassicPlayerHasBenefit(player, BenefitID::NAPALM_GRENADES);
}

bool ClassicPlayerHasBenefitTeslaChainLightning(edict_t* player) {
    // In Classic Mode (vortex=0), only check benefit system
    if (g_vortex->integer == 0) {
        return ClassicPlayerHasBenefit(player, BenefitID::TESLA_CHAIN_LIGHTNING);
    }

    // In RPG Mode (vortex=1), check both benefit (bots) and skill (humans)
    return ClassicPlayerHasBenefit(player, BenefitID::TESLA_CHAIN_LIGHTNING) ||
           (player && player->client && player->client->pers.skills.tesla_chain);
}

// BFG mode helpers
bool ClassicPlayerHasBenefitBFGSlide(edict_t* player) {
    if (!player || !player->client) return false; // Default to normal mode
    return player->client->pers.bfg_mode >= BFGMode::SLIDE;
}

bool ClassicPlayerHasBenefitBFGPull(edict_t* player) {
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
void BotEarnAbilityPoints(edict_t* player, int32_t points) {
    if (!player || !player->client) return;
    player->client->pers.ability_points += points;
}

void BotEarnWeaponPoints(edict_t* player, int32_t points) {
    if (!player || !player->client) return;
    player->client->pers.weapon_points += points;
}

bool BotCanAffordBenefit(edict_t* player, BenefitID benefit_id, int32_t cost) {
    if (!player || !player->client) return false;

    // In RPG Mode (vortex=1), only bots can use this
    if (g_vortex->integer != 0 && !(player->svflags & SVF_BOT)) return false;

    auto category = g_benefitsData.categories[static_cast<size_t>(benefit_id)];
    int32_t available_points = (category == BenefitCategory::ABILITY) ?
                               player->client->pers.ability_points :
                               player->client->pers.weapon_points;

    return available_points >= cost;
}

void BotSpendPoints(edict_t* player, BenefitID benefit_id, int32_t cost) {
    if (!player || !player->client) return;

    // In RPG Mode (vortex=1), only bots can use this
    if (g_vortex->integer != 0 && !(player->svflags & SVF_BOT)) return;

    auto category = g_benefitsData.categories[static_cast<size_t>(benefit_id)];
    int32_t* points = (category == BenefitCategory::ABILITY) ?
                      &player->client->pers.ability_points :
                      &player->client->pers.weapon_points;

    *points -= cost;
}

// Define the default auto-upgrade priority order
static const BenefitID AUTO_UPGRADE_PRIORITY_ABILITIES[] = {
    BenefitID::VAMPIRE,
    BenefitID::AMMO_REGEN,
    BenefitID::VAMPIRE_UPGRADED,
    BenefitID::AUTO_HASTE,
    BenefitID::START_ARMOR,
    BenefitID::HA_PICKUP
};

static const BenefitID AUTO_UPGRADE_PRIORITY_WEAPONS[] = {
    BenefitID::TRACED_BULLETS,
    BenefitID::ENERGY_SHELLS,
    BenefitID::TESLA_CHAIN_LIGHTNING,
    BenefitID::BFG_SLIDE,
    BenefitID::BFG_GRAV_PULL,
    BenefitID::PIERCING_PLASMA,
    BenefitID::NAPALM_GRENADES,
    BenefitID::CLUSTER_PROX
};

// Helper function to auto-buy from a specific category using priority order
static void AutoBuyCategory(edict_t* player, BenefitCategory category) {
    if (!player || !player->client) return;

    bool is_bot = (player->svflags & SVF_BOT);
    const char* player_name = GetPlayerName(player);

    int32_t* points = (category == BenefitCategory::ABILITY) ?
                      &player->client->pers.ability_points :
                      &player->client->pers.weapon_points;

    const char* category_name = (category == BenefitCategory::ABILITY) ? "ABILITY" : "WEAPON";
    // gi.Com_PrintFmt("[DEBUG] AutoBuyCategory for {}: category={}, points={}\n",
    //     player_name, category_name, *points);

    // Use the appropriate priority list
    const BenefitID* priority_list;
    size_t list_size;

    if (category == BenefitCategory::ABILITY) {
        priority_list = AUTO_UPGRADE_PRIORITY_ABILITIES;
        list_size = sizeof(AUTO_UPGRADE_PRIORITY_ABILITIES) / sizeof(BenefitID);
    } else {
        priority_list = AUTO_UPGRADE_PRIORITY_WEAPONS;
        list_size = sizeof(AUTO_UPGRADE_PRIORITY_WEAPONS) / sizeof(BenefitID);
    }

    // Try to buy benefits in priority order
    for (size_t idx = 0; idx < list_size && *points > 0; ++idx) {
        BenefitID benefit_id = priority_list[idx];
        size_t i = static_cast<size_t>(benefit_id);
        const char* benefit_name = g_benefitsData.names[i];

        // Skip if already owned
        if (ClassicPlayerHasBenefit(player, benefit_id)) {
            // gi.Com_PrintFmt("[DEBUG] {} already has {}, skipping\n", player_name, benefit_name);
            continue;
        }

        // Set costs based on benefit type
        int32_t cost = 1; // Default cost
        if (category == BenefitCategory::WEAPON &&
            (benefit_id == BenefitID::BFG_GRAV_PULL || benefit_id == BenefitID::CLUSTER_PROX)) {
            cost = 3;
        }

        // Check if can afford
        if (*points < cost) {
            // gi.Com_PrintFmt("[DEBUG] {} cannot afford {} (cost={}, points={})\n",
            //     player_name, benefit_name, cost, *points);
            continue;
        }

        // Check wave requirements (for auto-buy only)
        int32_t min_wave = g_benefitsData.min_levels[i];
        if (current_wave_level < min_wave) {
            // gi.Com_PrintFmt("[DEBUG] {} wave req not met for {} (wave={}, min={})\n",
            //     player_name, benefit_name, current_wave_level, min_wave);
            continue;
        }

        // Check prerequisites
        auto prereq = g_benefitsData.prerequisites[i];
        if (prereq != BenefitID::NONE && !ClassicPlayerHasBenefit(player, prereq)) {
            const char* prereq_name = g_benefitsData.names[static_cast<size_t>(prereq)];
            // gi.Com_PrintFmt("[DEBUG] {} missing prereq {} for {}\n",
            //     player_name, prereq_name, benefit_name);
            continue;
        }

        // Purchase the benefit
        // gi.Com_PrintFmt("[DEBUG] {} attempting to purchase {} (cost={})\n",
        //     player_name, benefit_name, cost);

        if (BotPurchaseBenefit(player, benefit_id, cost)) {
            player->client->pers.auto_purchased_benefits_mask |= (1u << static_cast<uint8_t>(benefit_id));
            // gi.Com_PrintFmt("[DEBUG] {} successfully purchased {}!\n", player_name, benefit_name);
        } else {
            // gi.Com_PrintFmt("[DEBUG] {} FAILED to purchase {}!\n", player_name, benefit_name);
        }
    }
}

// Auto-buy system implementation
void CheckBotAutoBuy(edict_t* player) {
    if (!player || !player->client) return;

    bool is_bot = (player->svflags & SVF_BOT);
    const char* player_name = GetPlayerName(player);

    // Update last check time (for tracking purposes, no longer used for throttling)
    player->client->pers.last_auto_buy_check = level.time;

    // gi.Com_PrintFmt("[DEBUG] CheckBotAutoBuy for {}: ability_pts={}, weapon_pts={}, auto_buy_ability={}, auto_buy_weapon={}\n",
    //     player_name,
    //     player->client->pers.ability_points,
    //     player->client->pers.weapon_points,
    //     player->client->pers.auto_buy_benefit_bot,
    //     player->client->pers.auto_buy_benefit_weapons_bot);

    // Auto-buy abilities if enabled and player has points
    if (player->client->pers.auto_buy_benefit_bot && player->client->pers.ability_points > 0) {
        // if (is_bot) {
        //     gi.Com_PrintFmt("[DEBUG] {} calling AutoBuyCategory for ABILITY\n", player_name);
        // }
        AutoBuyCategory(player, BenefitCategory::ABILITY);
    }

    // Auto-buy weapons if enabled and player has points
    if (player->client->pers.auto_buy_benefit_weapons_bot && player->client->pers.weapon_points > 0) {
        // if (is_bot) {
        //     gi.Com_PrintFmt("[DEBUG] {} calling AutoBuyCategory for WEAPON\n", player_name);
        // }
        AutoBuyCategory(player, BenefitCategory::WEAPON);
    }
}

// Benefit purchasing with per-player messages
bool BotPurchaseBenefit(edict_t* player, BenefitID benefit_id, int32_t cost) {
    if (!player || !player->client) return false;

    // In RPG Mode (vortex=1), only bots can use this
    if (g_vortex->integer != 0 && !(player->svflags & SVF_BOT)) {
        return false;
    }

    bool is_bot = (player->svflags & SVF_BOT);
    const char* player_name = GetPlayerName(player);
    const char* benefit_name = g_benefitsData.names[static_cast<size_t>(benefit_id)];

    // Check if player can afford it
    if (!BotCanAffordBenefit(player, benefit_id, cost)) {
        if (is_bot) {
            // gi.Com_PrintFmt("[DEBUG] {} cannot afford {} in BotPurchaseBenefit\n", player_name, benefit_name);
        }
        // gi.LocClient_Print(player, PRINT_HIGH, "Not enough points!\n");
        return false;
    }

    // Check if already owned
    if (ClassicPlayerHasBenefit(player, benefit_id)) {
        if (is_bot) {
            // gi.Com_PrintFmt("[DEBUG] {} already owns {} in BotPurchaseBenefit\n", player_name, benefit_name);
        }
        // gi.LocClient_Print(player, PRINT_HIGH, "Already owned!\n");
        return false;
    }

    // No wave requirements for manual purchases - players can buy anything they can afford
    // Wave requirements are only enforced for auto-buy

    // Check prerequisites
    auto prereq = g_benefitsData.prerequisites[static_cast<size_t>(benefit_id)];
    if (prereq != BenefitID::NONE && !ClassicPlayerHasBenefit(player, prereq)) {
        const char* prereq_name = g_benefitsData.names[static_cast<size_t>(prereq)];
        if (is_bot) {
            // gi.Com_PrintFmt("[DEBUG] {} missing prereq {} for {} in BotPurchaseBenefit\n",
                // player_name, prereq_name, benefit_name);
        }
        // gi.LocClient_Print(player, PRINT_HIGH, "Requires {} first!\n", prereq_name);
        return false;
    }

    // Purchase the benefit
    if (is_bot) {
        // gi.Com_PrintFmt("[DEBUG] {} spending points and activating {} in BotPurchaseBenefit\n",
            // player_name, benefit_name);
    }

    BotSpendPoints(player, benefit_id, cost);
    BotActivateBenefit(player, benefit_id);
    BotShowBenefitMessage(player, benefit_id);

    // Special handling for BFG upgrades - set default mode
    if (benefit_id == BenefitID::BFG_GRAV_PULL) {
        // When getting grav pull, default to using it
        player->client->pers.bfg_mode = BFGMode::GRAV_PULL;
        gi.LocClient_Print(player, PRINT_HIGH, "BFG mode set to Slide+Pull (configurable in Misc menu)\n");
    }

    return true;
}

// Show benefit message to specific player only
void BotShowBenefitMessage(edict_t* player, BenefitID benefit_id) {
    if (!player || !player->client) return;

    size_t index = static_cast<size_t>(benefit_id);

    // Send center print message (big screen message)
    // gi.LocCenter_Print(player, "{}", g_benefitsData.center_msgs[index]);

    // Send chat message
    // gi.LocClient_Print(player, PRINT_HIGH, "{}\n", g_benefitsData.chat_msgs[index]);
}

// Process wave rewards - replaces the old CheckBotAndApplyBenefit for point distribution
void ProcessWaveRewards(int32_t wave) {
    // Distribute points to all active players (both humans and bots)
    for (auto player : active_players()) {
        if (!player || !player->client) continue;

        bool is_bot = (player->svflags & SVF_BOT);
        const char* player_name = GetPlayerName(player);

        // Ability points every 4 waves starting from wave 4
        if (wave >= 4 && (wave % 4) == 0) {
            BotEarnAbilityPoints(player, 1);
            // Notify all players in Classic Mode
            if (!is_bot) {
                // gi.LocClient_Print(player, PRINT_HIGH, "+1 Ability Point! (Total: {})\n",
                //           player->client->pers.ability_points);
            } else {
                // gi.Com_PrintFmt("[DEBUG] Bot {} earned ability point. Total: {}\n",
                //     player_name, player->client->pers.ability_points);
            }
        }

        // Weapon points every 8 waves starting from wave 8
        if (wave >= 8 && (wave % 8) == 0) {
            BotEarnWeaponPoints(player, 1);
            // Only notify human players
            // if (!is_bot) {
            //     // gi.LocClient_Print(player, PRINT_HIGH, "+1 Weapon Point! (Total: {})\n",
            //     //           player->client->pers.weapon_points);
            // } else {
            //     // gi.Com_PrintFmt("[DEBUG] Bot {} earned weapon point. Total: {}\n",
            //     //     player_name, player->client->pers.weapon_points);
            // }
        }

        // Check for auto-buy after earning points
        // This will auto-purchase for bots (who have auto-buy enabled by default)
        // Humans can also use auto-buy if they enable it
        CheckBotAutoBuy(player);
    }
}

// Restore all player points by clearing all benefits
void BotRestoreAllBonusPoints(edict_t* player) {
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

    // Reset auto-purchased tracking
    player->client->pers.auto_purchased_benefits_mask = 0;

    gi.LocClient_Print(player, PRINT_HIGH,
        "All benefits cleared! Restored: {} ability points, {} weapon points\n",
        expected_ability_points, expected_weapon_points);
    gi.LocCenter_Print(player, "All upgrades restored!\nChoose your path again!");
}

// Refund all auto-purchased benefits when player manually disables auto-buy
void BotRefundAutoPurchasedBenefits(edict_t* player) {
    if (!player || !player->client) return;

    // Mark that player has manually disabled auto-buy
    player->client->pers.bot_has_manually_disabled_auto_buy = true;

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
            "Auto-buy disabled! Refunded: {} ability points, {} weapon points. Choose your own path!\n",
            refunded_ability_points, refunded_weapon_points);
        gi.LocCenter_Print(player, "Auto-buy disabled!\nAll auto-spent points refunded!");
    } else {
        gi.LocClient_Print(player, PRINT_HIGH, "Auto-buy disabled. No auto-purchases to refund.\n");
    }
}