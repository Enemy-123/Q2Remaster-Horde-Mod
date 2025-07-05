#pragma once
#include "../g_local.h" // For gtime_t, int32_t, etc.
#include <array>        // For std::array

// --- Enum and Struct Definitions (These are fine in a header) ---

enum class BenefitID : uint8_t {
    VAMPIRE,
    VAMPIRE_UPGRADED,
    AMMO_REGEN,
    AUTO_HASTE,
    START_ARMOR,
    TRACED_BULLETS,
    ENERGY_SHELLS,
    CLUSTER_PROX,
    PIERCING_PLASMA,
    NAPALM_GRENADES,
    BFG_PULL,
    // ---
    COUNT, // The total number of benefits
    NONE = 255
};

struct BenefitsDataSoA {
    static constexpr size_t NUM_BENEFITS = static_cast<size_t>(BenefitID::COUNT);

    std::array<const char*, NUM_BENEFITS> names;
    std::array<const char*, NUM_BENEFITS> center_msgs;
    std::array<const char*, NUM_BENEFITS> chat_msgs;
    std::array<const char*, NUM_BENEFITS> cvar_names;
    std::array<const char*, NUM_BENEFITS> cvar_values;
    std::array<int32_t, NUM_BENEFITS> min_levels;
    std::array<int32_t, NUM_BENEFITS> max_levels;
    std::array<float, NUM_BENEFITS> weights;
    std::array<BenefitID, NUM_BENEFITS> prerequisites;
};

// --- Constants (These are also fine in a header) ---

constexpr size_t MAX_BENEFITS = static_cast<size_t>(BenefitID::COUNT);
constexpr size_t MAX_RECENT_BENEFITS = 3;

// --- Declarations of Global Variables and Functions ---
// Use 'extern' to tell the compiler that these exist somewhere else (in the .cpp file).

// Global Data
extern const BenefitsDataSoA g_benefitsData;

// Global State
extern uint32_t obtained_benefits_mask;
extern std::array<BenefitID, MAX_RECENT_BENEFITS> recent_benefits;
extern size_t recent_index;
extern int32_t vampire_level;
extern bool bfg_pull_active;

// Function Prototypes (Declarations)
void ResetBenefits() noexcept;
void CheckAndApplyBenefit(int32_t wave);
bool has_benefit(BenefitID id) noexcept;
void mark_benefit_obtained(BenefitID id) noexcept;
std::string GetActiveBonusesString(); // Declaration for the function used in horde_menu.cpp