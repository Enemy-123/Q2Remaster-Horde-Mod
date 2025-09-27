// Player to Brain Morph System for Q2Remaster Horde Mod
#pragma once

#include "../g_local.h"
#include "p_flyer_morph.h"

// Brain frame definitions
constexpr int32_t BRAIN_FRAMES_STAND_START = 1;
constexpr int32_t BRAIN_FRAMES_STAND_END = 30;
constexpr int32_t BRAIN_FRAMES_WALK_START = 101;
constexpr int32_t BRAIN_FRAMES_WALK_END = 111;
constexpr int32_t BRAIN_FRAMES_ATTACK_START = 201;
constexpr int32_t BRAIN_FRAMES_ATTACK_END = 211;

// Tongue attack constants
constexpr float BRAIN_TONGUE_RANGE = 512.0f;
constexpr int32_t BRAIN_TONGUE_DAMAGE = 3;
constexpr int32_t BRAIN_TONGUE_PULL_BASE = 175;
constexpr int32_t BRAIN_TONGUE_PULL_GROUNDED = 350; // Double when target on ground
constexpr gtime_t BRAIN_TONGUE_COOLDOWN = 500_ms;

// Health steal constants
constexpr int32_t BRAIN_STEAL_MIN = 3;
constexpr int32_t BRAIN_STEAL_MAX = 6;
constexpr gtime_t BRAIN_STEAL_INTERVAL = 250_ms; // Steal health 4 times per second

// Regeneration constants
constexpr gtime_t BRAIN_REGEN_DELAY = 2_sec;
constexpr int32_t BRAIN_REGEN_AMOUNT = 3;

// Function declarations
void Cmd_PlayerToBrain_f(edict_t* ent);
void RunBrainFrames(edict_t* ent, const usercmd_t& ucmd);
void BrainRegenerate(edict_t* ent);
void ApplyBrainPhysics(edict_t* ent);