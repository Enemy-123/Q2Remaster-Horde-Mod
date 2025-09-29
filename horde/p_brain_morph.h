// Player to Brain Morph System for Q2Remaster Horde Mod
#pragma once

#include "../g_local.h"
#include "p_flyer_morph.h"

// Brain frame definitions from m_brain.h
// Standing animation - brain uses stand01 to stand30 (frames 170-199)
constexpr int32_t BRAIN_FRAMES_STAND_START = 162;  // FRAME_stand01;  // FRAME_stand01
constexpr int32_t BRAIN_FRAMES_STAND_END = 191;    // FRAME_stand30;    // FRAME_stand30

// Walking animation - walk101 to walk111 (frames 0-10)
constexpr int32_t BRAIN_FRAMES_WALK_START = 0;     // FRAME_walk101;     // FRAME_walk101 (line 9 in enum, 0-indexed = 8);     // FRAME_walk101
constexpr int32_t BRAIN_FRAMES_WALK_END = 10;      // FRAME_walk111;      // FRAME_walk111 (line 19 in enum, 0-indexed = 18);      // FRAME_walk111

// Attack animation - initial attack and continuous loop
constexpr int32_t BRAIN_FRAMES_ATTACK_START = 74;      // FRAME_attak201 - initial attack start
constexpr int32_t BRAIN_FRAMES_ATTACK_LOOP_START = 76; // FRAME_attak206 - continuous loop start
constexpr int32_t BRAIN_FRAMES_ATTACK_LOOP_END = 80;   // FRAME_attak210 - continuous loop end
constexpr int32_t BRAIN_FRAMES_ATTACK_END = 81;        // FRAME_attak211 - attack sequence end

// Jump animation - duck01 to duck08 (frames 154-161) used for jumping
constexpr int32_t BRAIN_FRAMES_JUMP_START = 146;   // FRAME_duck01
constexpr int32_t BRAIN_FRAMES_JUMP_HOLD = 148;    // FRAME_duck05 - hold while in air (the correct mid-air frame)
constexpr int32_t BRAIN_FRAMES_JUMP_END = 153;     // FRAME_duck08

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