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
    HA_PICKUP,     // H/A Pickup - multiplies health/armor pickups
    TRACED_BULLETS,
    ENERGY_SHELLS,
    CLUSTER_PROX,
    PIERCING_PLASMA,
    NAPALM_GRENADES,
    BFG_SLIDE,     // BFG Slide mode upgrade
    BFG_GRAV_PULL, // BFG Gravity Pull upgrade (requires slide)
    TESLA_CHAIN_LIGHTNING, // Tesla Chain Lightning upgrade
    // ---
    COUNT, // The total number of benefits
    NONE = 255
};;

// Benefit categories for menu separation
enum class BenefitCategory : uint8_t {
    ABILITY,  // Passive player benefits (vampire, ammo regen, etc.)
    WEAPON    // Weapon modifications (traced bullets, energy shells, etc.)
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
    std::array<BenefitCategory, NUM_BENEFITS> categories;
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
std::string GetPlayerActiveBonusesString(edict_t* player); // Per-player version of GetActiveBonusesString

// Per-player benefit functions
bool PlayerHasBenefit(edict_t* player, BenefitID benefit_id);
bool PlayerHasAbility(edict_t* player, BenefitID ability_id);
bool PlayerHasWeaponUpgrade(edict_t* player, BenefitID weapon_id);
void PlayerActivateBenefit(edict_t* player, BenefitID benefit_id);
void PlayerDeactivateBenefit(edict_t* player, BenefitID benefit_id);

// Specific benefit helpers (replace global cvar checks)
bool PlayerHasVampire(edict_t* player);
bool PlayerHasAmmoRegen(edict_t* player);
bool PlayerHasAutoHaste(edict_t* player);
bool PlayerHasStartArmor(edict_t* player);

bool PlayerHasHAPickup(edict_t* player);
bool PlayerHasTracedBullets(edict_t* player);
bool PlayerHasEnergyShells(edict_t* player);
bool PlayerHasClusterProx(edict_t* player);
bool PlayerHasPiercingPlasma(edict_t* player);
bool PlayerHasNapalmGL(edict_t* player);
bool PlayerHasTeslaChainLightning(edict_t* player);

// BFG mode helpers
bool PlayerHasBFGSlide(edict_t* player);
bool PlayerHasBFGPull(edict_t* player);
BFGMode PlayerGetBFGMode(edict_t* player);
void PlayerSetBFGMode(edict_t* player, BFGMode mode);

// Point management
void PlayerEarnAbilityPoints(edict_t* player, int32_t points);
void PlayerEarnWeaponPoints(edict_t* player, int32_t points);
bool PlayerCanAffordBenefit(edict_t* player, BenefitID benefit_id, int32_t cost);
void PlayerSpendPoints(edict_t* player, BenefitID benefit_id, int32_t cost);

// Benefit purchasing (with per-player messages)
bool PlayerPurchaseBenefit(edict_t* player, BenefitID benefit_id, int32_t cost);
void PlayerShowBenefitMessage(edict_t* player, BenefitID benefit_id);

// Auto-buy system
void CheckPlayerAutoBuy(edict_t* player);
void ProcessWaveRewards(int32_t wave);

// Point management and restore system
void PlayerRestoreAllPoints(edict_t* player);

// Refund system for manual auto-buy disable
void PlayerRefundAutoPurchasedBenefits(edict_t* player);