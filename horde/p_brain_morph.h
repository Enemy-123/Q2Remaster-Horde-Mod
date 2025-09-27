// Player to Brain Morph System for Q2Remaster Horde Mod
#pragma once

#include "../g_local.h"
#include "p_flyer_morph.h"

// Brain frame definitions from m_brain.h
// Standing animation - brain uses stand01 to stand30 (frames 170-199)
constexpr int32_t BRAIN_FRAMES_STAND_START = 170;  // FRAME_stand01
constexpr int32_t BRAIN_FRAMES_STAND_END = 199;    // FRAME_stand30

// Walking animation - walk101 to walk111 (frames 0-10)
constexpr int32_t BRAIN_FRAMES_WALK_START = 0;     // FRAME_walk101
constexpr int32_t BRAIN_FRAMES_WALK_END = 10;      // FRAME_walk111

// Attack animation - attak201 to attak217 (frames 79-95) for tongue attack
constexpr int32_t BRAIN_FRAMES_ATTACK_START = 79;  // FRAME_attak201
constexpr int32_t BRAIN_FRAMES_ATTACK_END = 95;    // FRAME_attak217

// Jump animation - duck01 to duck08 (frames 154-161) used for jumping
constexpr int32_t BRAIN_FRAMES_JUMP_START = 154;   // FRAME_duck01
constexpr int32_t BRAIN_FRAMES_JUMP_HOLD = 155;    // FRAME_duck02 - hold while in air
constexpr int32_t BRAIN_FRAMES_JUMP_END = 161;     // FRAME_duck08

// Tongue attack constants
constexpr float BRAIN_TONGUE_RANGE = 512.0f;
constexpr int32_t BRAIN_TONGUE_DAMAGE = 6;  // Increased from 3
constexpr int32_t BRAIN_TONGUE_PULL_BASE = 175;
constexpr int32_t BRAIN_TONGUE_PULL_GROUNDED = 350; // Double when target on ground
constexpr gtime_t BRAIN_TONGUE_COOLDOWN = 500_ms;

// Health steal constants
constexpr int32_t BRAIN_STEAL_MIN = 5;  // Increased from 3
constexpr int32_t BRAIN_STEAL_MAX = 10; // Increased from 6
constexpr gtime_t BRAIN_STEAL_INTERVAL = 250_ms; // Steal health 4 times per second

// Regeneration constants
constexpr gtime_t BRAIN_REGEN_DELAY = 2_sec;
constexpr int32_t BRAIN_REGEN_AMOUNT = 3;

// Jump constants from Vortex and m_brain.cpp
constexpr float BRAIN_JUMP_FORWARD_VELOCITY = 800.0f;
constexpr float BRAIN_JUMP_UP_VELOCITY = 300.0f;       // Reduced from 400 for lower forward jumps
constexpr float BRAIN_JUMP_HEIGHT = 68.0f;            

// Function declarations
void Cmd_PlayerToBrain_f(edict_t* ent);
void RunBrainFrames(edict_t* ent, const usercmd_t& ucmd);
void BrainRegenerate(edict_t* ent);
void ApplyBrainPhysics(edict_t* ent);