// Player to Brain Morph System for Q2Remaster Horde Mod
#pragma once

#include "../g_local.h"
#include "p_flyer_morph.h"

// Brain frame definitions from m_brain.h
// Standing animation - brain uses stand01 to stand60 (frames 171-230)
constexpr int32_t BRAIN_FRAMES_STAND_START = 171;  // FRAME_stand01
constexpr int32_t BRAIN_FRAMES_STAND_END = 230;    // FRAME_stand60

// Walking animation - walk101 to walk111 (frames 0-10)
constexpr int32_t BRAIN_FRAMES_WALK_START = 0;     // FRAME_walk101
constexpr int32_t BRAIN_FRAMES_WALK_END = 10;      // FRAME_walk111

// Attack animation - attak101 to attak118 (frames 53-70) for tongue attack
constexpr int32_t BRAIN_FRAMES_ATTACK_START = 53;  // FRAME_attak101
constexpr int32_t BRAIN_FRAMES_ATTACK_END = 70;    // FRAME_attak118

// Jump animation - duck01 to duck08 (frames 155-162) used for jumping
constexpr int32_t BRAIN_FRAMES_JUMP_START = 155;   // FRAME_duck01
constexpr int32_t BRAIN_FRAMES_JUMP_HOLD = 156;    // FRAME_duck02 - hold while in air
constexpr int32_t BRAIN_FRAMES_JUMP_END = 162;     // FRAME_duck08

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

// Jump constants from Vortex and m_brain.cpp
constexpr float BRAIN_JUMP_FORWARD_VELOCITY = 800.0f;  // From mybrain_jumpattack_takeoff
constexpr float BRAIN_JUMP_UP_VELOCITY = 400.0f;       // From brain_jump2_now
constexpr float BRAIN_JUMP_HEIGHT = 68.0f;             // From m_brain.cpp

// Function declarations
void Cmd_PlayerToBrain_f(edict_t* ent);
void RunBrainFrames(edict_t* ent, const usercmd_t& ucmd);
void BrainRegenerate(edict_t* ent);
void ApplyBrainPhysics(edict_t* ent);