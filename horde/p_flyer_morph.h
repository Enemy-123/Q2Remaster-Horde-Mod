// Player to Flyer Morph System for Q2Remaster Horde Mod
#pragma once

#include "../g_local.h"

// Flyer frame definitions
constexpr int32_t FLYER_FRAMES_STAND_START = 13;
constexpr int32_t FLYER_FRAMES_STAND_END = 57;
constexpr int32_t FLYER_FRAMES_BANK_R_START = 96;
constexpr int32_t FLYER_FRAMES_BANK_R_END = 99;
constexpr int32_t FLYER_FRAMES_BANK_L_START = 103;
constexpr int32_t FLYER_FRAMES_BANK_L_END = 106;

// Movement constants
constexpr float FLYER_BRAKE_SPEED = 5.0f;
constexpr float FLYER_ACCEL_SPEED = 20.0f;
constexpr float FLYER_MAX_VELOCITY = 400.0f;
constexpr float FLYER_IMPACT_VELOCITY = 200.0f;
constexpr int32_t FLYER_IMPACT_DAMAGE = 50;

// Hyperblaster constants
constexpr int32_t FLYER_HB_INITIAL_DMG = 15;
constexpr int32_t FLYER_HB_ADDON_DMG = 3;
constexpr int32_t FLYER_HB_SPEED = 1000;
constexpr int32_t FLYER_HB_AMMO = 1;
constexpr int32_t FLYER_HB_INITIAL_AMMO = 100;
constexpr int32_t FLYER_HB_ADDON_AMMO = 20;
constexpr int32_t FLYER_HB_START_AMMO = 50;
constexpr gtime_t FLYER_HB_REFIRE_TIME = 100_ms;
constexpr gtime_t FLYER_HB_REGEN_DELAY = 1_sec;
constexpr int32_t FLYER_HB_REGEN_AMOUNT = 5;

// Rocket constants
constexpr int32_t FLYER_ROCKET_INITIAL_DMG = 70;
constexpr int32_t FLYER_ROCKET_ADDON_DMG = 10;
constexpr int32_t FLYER_ROCKET_INITIAL_RADIUS = 120;
constexpr int32_t FLYER_ROCKET_ADDON_RADIUS = 20;
constexpr int32_t FLYER_ROCKET_SPEED = 650;
constexpr int32_t FLYER_ROCKET_AMMO = 10;
constexpr int32_t FLYER_ROCKET_LOCKFRAMES = 5;
constexpr gtime_t FLYER_ROCKET_PREFIRE_TIME = 1_sec;
constexpr int32_t SMARTROCKET_LOCKFRAMES = 5;

// Regeneration constants
constexpr gtime_t FLYER_REGEN_DELAY = 1_sec;
constexpr int32_t FLYER_REGEN_AMOUNT = 2;

// Cost constants
constexpr int32_t FLYER_INIT_COST = 50;

// Morph type enum
enum morph_type_t : uint8_t {
    MORPH_NONE = 0,
    MORPH_FLYER = 1,
    MORPH_CACODEMON = 2,
    MORPH_MUTANT = 3,
    MORPH_BRAIN = 4,
    MORPH_MEDIC = 5,
    MORPH_TANK = 6,
    MORPH_BERSERK = 7,
};

// Extended entity data for morphed players
struct flyer_data_t {
    morph_type_t morph_type;
    int32_t max_ammo;
    int32_t current_ammo;
    gtime_t last_regen_time;
    gtime_t attack_finished;
    int32_t refire_frames;
    bool weapon_mode; // false = hyperblaster, true = rockets
    edict_t* lock_target;
    int32_t lock_frames;
    float old_speed; // for impact detection
    gtime_t morph_time;
    int32_t ability_level; // flyer ability level
};

// Function declarations
void Cmd_PlayerToFlyer_f(edict_t* ent);
void RunFlyerFrames(edict_t* ent, const usercmd_t& ucmd);
void MorphRegenerate(edict_t* ent);
void RestoreMorphed(edict_t* ent);
bool IsMorphed(edict_t* ent);
flyer_data_t* GetFlyerData(edict_t* ent);
void InitFlyerData(edict_t* ent);
void ClearFlyerData(edict_t* ent);
void ApplyFlyerPhysics(edict_t* ent); // Called after pmove to apply flyer physics