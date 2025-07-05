#include "g_horde_benefits.h"
#include "../g_local.h" // Include g_local.h for gi, etc.

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
} BENEFITS_SRC[] = {
    {BenefitID::VAMPIRE, "vampire", "\n\n\n\nYou're covered in blood!\n\nVampire Ability\nENABLED!\n", "RECOVERING HEALTH FROM DAMAGE DONE!\n", "g_vampire", "1", 4, -1, 0.2f, BenefitID::NONE},
    {BenefitID::VAMPIRE_UPGRADED, "vampire upgraded", "\n\n\n\nIMPROVED VAMPIRE ABILITY\n", "RECOVERING HEALTH & ARMOR FROM DAMAGE DONE!\n", "g_vampire", "2", 24, -1, 0.1f, BenefitID::VAMPIRE},
    {BenefitID::AMMO_REGEN, "ammo regen", "\n\n\n\n\nAMMO REGEN\n\nENABLED!\n", "AMMO REGEN IS NOW ENABLED!\n", "g_ammoregen", "1", 8, -1, 0.15f, BenefitID::NONE},
    {BenefitID::AUTO_HASTE, "auto haste", "\n\n\n\nDUAL-FIRE IS RUNNING THROUGH YOUR VEINS\nFRAGGING WHILE HASTE\nWILL EXTEND QUAD DMG AND DUAL-FIRE TIME!\n", "AUTO-HASTE ENABLED!\n", "g_autohaste", "1", 9, -1, 0.15f, BenefitID::NONE},
    {BenefitID::START_ARMOR, "start armor", "\n\n\n\nSTARTING ARMOR\nENABLED!\n", "STARTING WITH 100 BODY-ARMOR!\n", "g_startarmor", "1", 9, -1, 0.1f, BenefitID::NONE},
    {BenefitID::TRACED_BULLETS, "Traced-Piercing Bullets", "\n\n\n\nBULLETS\nUPGRADED!\n", "Piercing-PowerShield Bullets!\n", "g_tracedbullets", "1", 9, -1, 0.2f, BenefitID::NONE},
    {BenefitID::ENERGY_SHELLS, "Energy Shells", "\n\n\n\nSHELLS\nUPGRADED!\n", "Piercing-PowerShield Shells!\n", "g_energyshells", "1", 9, -1, 0.2f, BenefitID::NONE},
    {BenefitID::CLUSTER_PROX, "Cluster Prox Grenades", "\n\n\n\nIMPROVED PROX GRENADES\n", "Prox Cluster Launcher Enabled\n", "g_upgradeproxs", "1", 25, -1, 0.2f, BenefitID::NONE},
    {BenefitID::PIERCING_PLASMA, "Piercing Plasma", "\n\n\n\nPlasma-Beam Piercing Mode Enabled\n", "IMPROVED Plasma-Beam!\n", "g_piercingbeam", "1", 25, -1, 0.2f, BenefitID::NONE},
    {BenefitID::NAPALM_GRENADES, "Napalm-Grenade Launcher", "\n\n\n\nIMPROVED GRENADE LAUNCHER!\n", "Napalm-Grenade Launcher Enabled\n", "g_bouncygl", "1", 25, -1, 0.2f, BenefitID::NONE},
    {BenefitID::BFG_PULL, "BFG Grav-Pull Lasers", "\n\n\n\nBFG LASERS UPGRADED!\n", "BFG Grav-Pull Lasers Enabled\n", "g_bfgpull", "1", 35, -1, 0.2f, BenefitID::NONE}
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
        }
    }
    return soa_data;
}

// --- The single global instance of our optimized data ---
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

// Apply a benefit using its ID
static void apply_benefit(BenefitID id) {
    size_t index = static_cast<size_t>(id);
    if (id == BenefitID::BFG_PULL) {
        bfg_pull_active = !bfg_pull_active;
        if (bfg_pull_active) {
            gi.cvar_set("g_bfgpull", "1");
            gi.LocBroadcast_Print(PRINT_CENTER, g_benefitsData.center_msgs[index]);
            gi.LocBroadcast_Print(PRINT_CHAT, g_benefitsData.chat_msgs[index]);
        } else {
            gi.cvar_set("g_bfgpull", "0");
            gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\nBFG LASERS NORMAL MODE\n");
            gi.LocBroadcast_Print(PRINT_CHAT, "BFG Slide Mode Active\n");
        }
        return;
    }
    gi.cvar_set(g_benefitsData.cvar_names[index], g_benefitsData.cvar_values[index]);
    gi.LocBroadcast_Print(PRINT_CENTER, g_benefitsData.center_msgs[index]);
    gi.LocBroadcast_Print(PRINT_CHAT, g_benefitsData.chat_msgs[index]);
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
    gi.cvar_set("g_bfgpull", "0");
    gi.cvar_set("g_bfgslide", "1");
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