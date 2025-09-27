// Player to Brain Morph System for Q2Remaster Horde Mod
#include "p_brain_morph.h"
#include "../m_flash.h"
#include "../bots/bot_includes.h"
#include "g_horde_benefits.h"
#include "g_horde_phys.h"
#include "horde_ids.h"

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
    // Always find the nearest target - don't stick to one enemy
    BrainFindTarget(self);

    // Allow attack animation even without target
    data->tongue_active = true;
    data->attack_finished = level.time + FRAME_TIME_MS; // Start attacking next frame

    if (!self->enemy) {
        // No target found - just play animation
        data->tongue_target = nullptr;
        data->last_steal_time = 0_ms;
    } else {
        // Start attacking the nearest enemy
        if (data->tongue_target != self->enemy) {
            // New target
            data->tongue_target = self->enemy;
            data->last_steal_time = 0_ms; // Reset steal timer

            // Play initial attack sound for new target
            gi.sound(self, CHAN_WEAPON, gi.soundindex("brain/brnatck1.wav"), 1, ATTN_NORM, 0);
        }
    }
}

static void BrainTongueAttackContinue(edict_t* self, morph_data_t* data) {
    // Continue damaging while holding attack - but not every frame!
    if (!data->tongue_active || !data->tongue_target)
        return;

    // Only continue attack at proper intervals (not every frame)
    if (level.time < data->attack_finished)
        return;

    // Check if target is still valid
    if (!data->tongue_target->inuse || data->tongue_target->health <= 0) {
        data->tongue_active = false;
        data->tongue_target = nullptr;
        return;
    }

    // Calculate distance
    vec3_t start;
    G_EntMidPoint(data->tongue_target, start);
    vec3_t const diff = start - self->s.origin;
    float const dist = diff.length();

    // Release if out of range or no line of sight
    if (dist > BRAIN_TONGUE_RANGE) {
        data->tongue_active = false;
        data->tongue_target = nullptr;
        data->attack_finished = level.time + 1_sec;
        return;
    }

    // Check line of sight
    trace_t tr = gi.traceline(self->s.origin, data->tongue_target->s.origin, self, MASK_SHOT);
    if (tr.fraction < 1.0f && tr.ent != data->tongue_target) {
        // Lost line of sight
        data->tongue_active = false;
        data->tongue_target = nullptr;
        return;
    }

    // Face the target
    vec3_t ideal_angles = vectoangles(diff);

    // Only update yaw (left-right rotation)
    if (self->client) {
        float angle_diff = ideal_angles[YAW] - self->client->resp.cmd_angles[YAW];
        self->client->ps.pmove.delta_angles[YAW] = angle_diff;
        self->client->v_angle[YAW] = ideal_angles[YAW];
        self->client->ps.viewangles[YAW] = ideal_angles[YAW];
    }
    self->s.angles[YAW] = ideal_angles[YAW];

    // Calculate pull and damage values
    const vec3_t dir = diff.normalized();
    int pull = 70;  // Same as m_brain.cpp
    int damage = BRAIN_TONGUE_DAMAGE; // Use constant from header

    // Increase pull if on ground
    if (data->tongue_target->groundentity)
        pull *= 2;

    // Apply effects if in close range (64 units like m_brain.cpp)
    if (dist <= 64) {
        // Apply damage and pull
        T_Damage(data->tongue_target, self, self, dir, data->tongue_target->s.origin,
            vec3_origin, damage, -pull, DAMAGE_RADIUS, MOD_UNKNOWN);

        // Steal health 4 times per second (like m_brain.cpp)
        if (level.time >= data->last_steal_time) {
            int steal_amount = irandom(BRAIN_STEAL_MIN, BRAIN_STEAL_MAX);

            // Apply health steal
            self->health = min(self->max_health, self->health + steal_amount);

            // Visual feedback
            self->s.effects |= EF_TELEPORTER;
            gi.sound(self, CHAN_AUTO, gi.soundindex("brain/brnatck2.wav"), 0.5f, ATTN_NORM, 0);

            // Set next steal time (4 times per second)
            data->last_steal_time = level.time + 250_ms;
        }
    } else {
        // If not close enough, just pull without damage
        T_Damage(data->tongue_target, self, self, dir, data->tongue_target->s.origin,
            vec3_origin, 0, -pull, DAMAGE_RADIUS, MOD_UNKNOWN);
    }

    // Set next attack time - attack continues at regular intervals
    data->attack_finished = level.time + FRAME_TIME_MS * 2; // Every 2 frames like monster AI

    // Update enemy pointer for targeting
    self->enemy = data->tongue_target;
}

static void BrainFindTarget(edict_t* self) {
    // Find nearest enemy within range using the proper grid system
    edict_t* best = nullptr;
    float best_dist = BRAIN_TONGUE_RANGE;

    // Use the monster grid to find nearby entities efficiently
    const auto nearby_entities = HordePhys::g_monster_grid.QueryRadius(self->s.origin, BRAIN_TONGUE_RANGE);

    for (auto* target : nearby_entities) {
        if (!target || target == self)
            continue;

        if (!target->takedamage || target->health <= 0 || target->deadflag)
            continue;

        // Check if it's a valid enemy (monster or player)
        bool is_valid_target = false;

        // Check if it's a monster
        if (target->svflags & SVF_MONSTER) {
            if (!OnSameTeam(self, target))
                is_valid_target = true;
        }
        // Check if it's a player
        else if (target->client) {
            if (!OnSameTeam(self, target))
                is_valid_target = true;
        }

        if (!is_valid_target)
            continue;

        float dist = (target->s.origin - self->s.origin).length();
        if (dist < best_dist) {
            // Check line of sight for brain tongue
            trace_t tr = gi.traceline(self->s.origin, target->s.origin, self, MASK_SHOT);
            if (tr.fraction == 1.0f || tr.ent == target) {
                best = target;
                best_dist = dist;
            }
        }
    }

    // Also check players specifically (in case they're not in the monster grid)
    for (int i = 1; i <= game.maxclients; i++) {
        edict_t* ent = &g_edicts[i];

        if (!ent || !ent->inuse || !ent->client || ent == self)
            continue;

        if (ent->health <= 0 || ent->deadflag)
            continue;

        // Skip spectators
        if (ent->client->resp.spectator)
            continue;

        // Check team
        if (OnSameTeam(self, ent))
            continue;

        float dist = (ent->s.origin - self->s.origin).length();
        if (dist < best_dist) {
            // Check line of sight
            trace_t tr = gi.traceline(self->s.origin, ent->s.origin, self, MASK_SHOT);
            if (tr.fraction == 1.0f || tr.ent == ent) {
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

    // Clear ammo display completely for brain morph (no weapons shown)
    ent->client->ps.stats[STAT_AMMO_ICON] = 0;
    ent->client->ps.stats[STAT_AMMO] = 0;

    // Clear effects
    ent->s.effects &= ~EF_TELEPORTER;

    // Regeneration
    BrainRegenerate(ent);

    // Clear enemy pointer each frame - we'll find fresh target when attacking
    ent->enemy = nullptr;

    // Track if we're jumping for animation
    bool is_jumping = !ent->groundentity && (ent->waterlevel < 2);

    // Check if we're moving
    bool is_moving = (ucmd.forwardmove != 0) || (ucmd.sidemove != 0);

    // Handle attacks - only allow when on ground and not moving
    if ((ucmd.buttons & BUTTON_ATTACK) && ent->groundentity && !is_moving) {
        // Start attack if not already attacking
        if (!data->tongue_active) {
            BrainTongueAttack(ent, data);
        }

        // Continue damaging while button is held
        if (data->tongue_active) {
            BrainTongueAttackContinue(ent, data);

            // Animation for attack
            if (ent->s.frame < BRAIN_FRAMES_ATTACK_START || ent->s.frame > BRAIN_FRAMES_ATTACK_END)
                ent->s.frame = BRAIN_FRAMES_ATTACK_START;
            else if (ent->s.frame < BRAIN_FRAMES_ATTACK_END)
                ent->s.frame++;
            else
                ent->s.frame = BRAIN_FRAMES_ATTACK_START + 4; // Loop middle frames during continuous attack
        }
    } else {
        // Release attack when button is released or not on ground
        if (data->tongue_active) {
            data->tongue_active = false;
            data->tongue_target = nullptr;
            data->attack_finished = level.time + BRAIN_TONGUE_COOLDOWN;
        }
    }

    // Animation frames
    if (is_jumping) {
        // Jump animation - cycle through all 8 duck frames
        if (ent->s.frame < BRAIN_FRAMES_JUMP_START || ent->s.frame > BRAIN_FRAMES_JUMP_END)
            ent->s.frame = BRAIN_FRAMES_JUMP_START;
        else {
            ent->s.frame++;
            if (ent->s.frame > BRAIN_FRAMES_JUMP_END)
                ent->s.frame = BRAIN_FRAMES_JUMP_START;
        }
    } else if (!data->tongue_active) {
        if (is_moving) {
            // Walking animation - using proper brain walk frames
            if (ent->s.frame < BRAIN_FRAMES_WALK_START || ent->s.frame > BRAIN_FRAMES_WALK_END)
                ent->s.frame = BRAIN_FRAMES_WALK_START;
            else {
                ent->s.frame++;
                if (ent->s.frame > BRAIN_FRAMES_WALK_END)
                    ent->s.frame = BRAIN_FRAMES_WALK_START;
            }
        } else {
            // Standing animation - cycle through all 30 stand frames slowly
            if (ent->s.frame < BRAIN_FRAMES_STAND_START || ent->s.frame > BRAIN_FRAMES_STAND_END)
                ent->s.frame = BRAIN_FRAMES_STAND_START;
            else {
                // Slow down standing animation (advance every 2 game frames)
                if (level.time >= data->next_frame_time) {
                    ent->s.frame++;
                    if (ent->s.frame > BRAIN_FRAMES_STAND_END)
                        ent->s.frame = BRAIN_FRAMES_STAND_START;
                    // Set next frame time (slower for smooth idle animation)
                    data->next_frame_time = level.time + (FRAME_TIME_MS * 2);
                }
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

    // Set special entity type for M_ReactToDamage
    ent->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::MORPHED_PLAYER);
    data->last_steal_time = 0_ms;
    data->tongue_active = false;
    data->tongue_target = nullptr;
    data->next_frame_time = level.time;

    // Clear any looping sounds (like chainsaw idle)
    ent->s.sound = 0;
    ent->client->weapon_sound = 0;

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