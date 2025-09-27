// Player to Brain Morph System for Q2Remaster Horde Mod
#include "p_brain_morph.h"
#include "../m_flash.h"
#include "../bots/bot_includes.h"
#include "g_horde_benefits.h"

// Helper function from m_brain.cpp
inline void G_EntMidPoint(const edict_t* ent, vec3_t& point)
{
    point = ent->s.origin;
    float const midheight = 0.5f * (ent->absmax[2] - ent->absmin[2]);
    point[2] = ent->absmin[2] + midheight;
}

// ==================== Tongue Attack ====================

// Forward declaration
static void BrainFindTarget(edict_t* self);

static void BrainTongueAttack(edict_t* self, morph_data_t* data) {
    if (level.time < data->attack_finished)
        return;

    // Find target if we don't have one
    if (!self->enemy || !self->enemy->inuse || !self->enemy->takedamage || self->enemy->health <= 0) {
        BrainFindTarget(self);
    }

    if (!self->enemy)
        return;

    // Calculate enemy position and direction
    vec3_t start;
    G_EntMidPoint(self->enemy, start);
    vec3_t const dir = start - self->s.origin;
    float const range = dir.length();

    // Check if enemy is in range
    if (range > BRAIN_TONGUE_RANGE)
        return;

    vec3_t const normalized_dir = dir.normalized();

    // Trace to enemy
    const vec3_t end = self->s.origin + (normalized_dir * BRAIN_TONGUE_RANGE);
    const trace_t tr = gi.traceline(self->s.origin, end, self, MASK_SHOT);

    if (tr.ent && tr.ent == self->enemy) {
        int damage = BRAIN_TONGUE_DAMAGE;
        int pull = BRAIN_TONGUE_PULL_BASE;

        // Double pull force if on ground
        if (self->enemy->groundentity)
            pull = BRAIN_TONGUE_PULL_GROUNDED;

        // Apply damage and pull
        T_Damage(self->enemy, self, self, normalized_dir, self->enemy->s.origin,
            vec3_origin, damage, -pull, DAMAGE_RADIUS, MOD_UNKNOWN);

        // Apply health steal if close enough
        if (range <= 64) {
            if (level.time >= data->last_steal_time) {
                int steal_amount = irandom(BRAIN_STEAL_MIN, BRAIN_STEAL_MAX);

                // Apply health steal
                self->health = min(self->max_health, self->health + steal_amount);

                // Visual feedback
                self->s.effects |= EF_TELEPORTER;
                gi.sound(self, CHAN_VOICE, gi.soundindex("brain/brnatck1.wav"), 1, ATTN_NORM, 0);

                data->last_steal_time = level.time + BRAIN_STEAL_INTERVAL;
            }
        }

        // Set tongue active
        data->tongue_active = true;
        data->tongue_target = self->enemy;
        data->attack_finished = level.time + BRAIN_TONGUE_COOLDOWN;
    }
}

static void BrainFindTarget(edict_t* self) {
    // Find nearest enemy within range
    edict_t* best = nullptr;
    float best_dist = BRAIN_TONGUE_RANGE;

    for (int i = 1; i <= game.maxclients; i++) {
        edict_t* ent = &g_edicts[i];

        if (!ent->inuse || !ent->client || ent == self)
            continue;

        if (ent->health <= 0 || ent->deadflag)
            continue;

        // Check team
        if (OnSameTeam(self, ent))
            continue;

        float dist = (ent->s.origin - self->s.origin).length();
        if (dist < best_dist) {
            // Check line of sight
            trace_t tr = gi.traceline(self->s.origin, ent->s.origin, self, MASK_SHOT);
            if (tr.fraction == 1.0 || tr.ent == ent) {
                best = ent;
                best_dist = dist;
            }
        }
    }

    self->enemy = best;
}

// ==================== Regeneration ====================

void BrainRegenerate(edict_t* ent) {
    auto* data = GetMorphData(ent);
    if (!data || data->morph_type != MORPH_BRAIN)
        return;

    // Regenerate health
    if (ent->health < ent->max_health) {
        if (level.time >= ent->timestamp + BRAIN_REGEN_DELAY) {
            ent->health += BRAIN_REGEN_AMOUNT;
            if (ent->health > ent->max_health)
                ent->health = ent->max_health;
            ent->timestamp = level.time;
        }
    }
}

// ==================== Frame Management ====================

void RunBrainFrames(edict_t* ent, const usercmd_t& ucmd) {
    auto* data = GetMorphData(ent);
    if (!data || data->morph_type != MORPH_BRAIN || ent->deadflag)
        return;

    // Clear weapon model
    ent->s.modelindex2 = 0;
    ent->s.skinnum = 0;

    // Clear effects
    ent->s.effects &= ~EF_TELEPORTER;

    // Regeneration
    BrainRegenerate(ent);

    // Find target if we don't have one
    if (!ent->enemy || !ent->enemy->inuse || ent->enemy->health <= 0) {
        BrainFindTarget(ent);
        data->tongue_active = false;
        data->tongue_target = nullptr;
    }

    // Handle attacks
    if (ucmd.buttons & BUTTON_ATTACK) {
        // Always try to attack when button is pressed
        BrainTongueAttack(ent, data);

        // Animation for attack
        if (data->tongue_active) {
            if (ent->s.frame < BRAIN_FRAMES_ATTACK_START || ent->s.frame > BRAIN_FRAMES_ATTACK_END)
                ent->s.frame = BRAIN_FRAMES_ATTACK_START;
            else if (ent->s.frame < BRAIN_FRAMES_ATTACK_END)
                ent->s.frame++;
        }
    } else {
        data->tongue_active = false;
        data->tongue_target = nullptr;
    }

    // Animation frames
    if (!data->tongue_active) {
        if (ucmd.forwardmove || ucmd.sidemove) {
            // Walking animation
            if (ent->s.frame < BRAIN_FRAMES_WALK_START || ent->s.frame > BRAIN_FRAMES_WALK_END)
                ent->s.frame = BRAIN_FRAMES_WALK_START;
            else {
                ent->s.frame++;
                if (ent->s.frame > BRAIN_FRAMES_WALK_END)
                    ent->s.frame = BRAIN_FRAMES_WALK_START;
            }
        } else {
            // Standing animation
            if (ent->s.frame < BRAIN_FRAMES_STAND_START || ent->s.frame > BRAIN_FRAMES_STAND_END)
                ent->s.frame = BRAIN_FRAMES_STAND_START;
            else {
                ent->s.frame++;
                if (ent->s.frame > BRAIN_FRAMES_STAND_END)
                    ent->s.frame = BRAIN_FRAMES_STAND_START;
            }
        }
    }
}

// ==================== Transformation ====================

void Cmd_PlayerToBrain_f(edict_t* ent) {
    if (!ent->client)
        return;

    // Check if already morphed - if so, unmorph
    if (IsMorphed(ent)) {
        RestoreMorphed(ent);
        gi.LocClient_Print(ent, PRINT_HIGH, "Transformed back to human form.\n");
        return;
    }

    // Add cooldown to prevent spam morphing (2 second cooldown)
    auto* existing_data = GetMorphData(ent);
    if (existing_data && (level.time - existing_data->morph_time) < 2_sec) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Must wait before morphing again.\n");
        return;
    }

    // Prevent morphing if dead or spectating
    if (ent->health <= 0 || ent->client->resp.spectator) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Cannot morph while dead or spectating.\n");
        return;
    }

    // Prevent morphing if entity is not in proper state
    if (!ent->inuse || ent->solid == SOLID_NOT) {
        return;
    }

    // Initialize brain data
    InitMorphData(ent, MORPH_BRAIN);
    auto* data = GetMorphData(ent);

    // Set ability level based on player stats if available
    data->ability_level = 1 + (ent->client->resp.score / 100);
    if (data->ability_level > 10)
        data->ability_level = 10;

    // Transform into brain
    data->morph_type = MORPH_BRAIN;
    data->morph_time = level.time;
    data->attack_finished = level.time + 500_ms;
    data->last_steal_time = 0_ms;
    data->tongue_active = false;
    data->tongue_target = nullptr;

    // Set model and bounds
    ent->s.modelindex = gi.modelindex("models/monsters/brain/tris.md2");
    ent->s.modelindex2 = 0;
    ent->s.skinnum = 0;

    // Set team for bot recognition
    ent->monsterinfo.team = ent->client->resp.ctf_team;

    // Use proper brain bounds from monster definition
    ent->mins = { -16, -16, -24 };
    ent->maxs = { 16, 16, 32 };
    ent->viewheight = 8;

    // Ground-based movement (unlike flyer)
    ent->movetype = MOVETYPE_WALK;
    ent->flags &= ~FL_FLY;
    ent->gravity = 1.0;

    // Set clipmask for proper collision
    ent->clipmask = MASK_PLAYERSOLID;
    ent->svflags = SVF_PLAYER;
    ent->solid = SOLID_BBOX;

    // Update collision after position change
    gi.linkentity(ent);

    // Clear weapon
    ent->client->ps.gunindex = 0;

    // Play transformation sound
    gi.sound(ent, CHAN_WEAPON, gi.soundindex("misc/tele_up.wav"), 1, ATTN_NORM, 0);

    gi.LocClient_Print(ent, PRINT_HIGH, "Transformed into Brain! Hold attack to use tongue pull. Type 'brain' again to transform back.\n");

    gi.linkentity(ent);
}

// This function is kept for compatibility but brain uses normal physics
void ApplyBrainPhysics(edict_t* ent) {
    // Brain uses normal player physics, nothing special needed
}