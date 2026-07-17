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

// Player-owned summons and deployables are teammates, but OnSameTeam can't see that
// in horde (players are CTF_NOTEAM), so the tongue must skip them explicitly.
static bool BrainTongueIsFriendly(const edict_t* ent) {
    return ent->monsterinfo.isfriendlyspawn ||
           horde::IsSpecialType(ent, horde::SpecialEntityTypeID::TESLA_MINE) ||
           horde::IsSpecialType(ent, horde::SpecialEntityTypeID::SENTRY_GUN) ||
           horde::IsSpecialType(ent, horde::SpecialEntityTypeID::LASER_EMITTER) ||
           horde::IsSpecialType(ent, horde::SpecialEntityTypeID::DOPPLEGANGER) ||
           horde::IsSpecialType(ent, horde::SpecialEntityTypeID::STROGG_SUMMONER) ||
           horde::IsSpecialType(ent, horde::SpecialEntityTypeID::FOOD_CUBE_TRAP);
}

static void BrainTongueAttack(edict_t* self, morph_data_t* data) {
    // Allow attack animation even without target
    data->tongue_active = true;
    data->attack_finished = level.time + FRAME_TIME_MS; // Start attacking next frame
    data->tongue_target = nullptr; // Always reset target on new attack press
    data->last_steal_time = 0_ms;

    // Always find the nearest target - don't stick to one enemy
    BrainFindTarget(self);

     // Play initial attack sound for new target
    gi.sound(self, CHAN_WEAPON, gi.soundindex("brain/brnatck1.wav"), 1, ATTN_NORM, 0);

    if (self->enemy) {
        // Start attacking the nearest enemy
        data->tongue_target = self->enemy;
    }
}

static void BrainTongueAttackContinue(edict_t* self, morph_data_t* data) {
    // Continue damaging while holding attack - but not every frame!
    if (!data->tongue_active || !data->tongue_target)
        return;

    // Only continue attack at proper intervals (not every frame)
    if (level.time < data->attack_finished)
        return;

    // Check if target is still valid (and didn't become friendly, e.g. via summon)
    if (!data->tongue_target->inuse || data->tongue_target->health <= 0 ||
        BrainTongueIsFriendly(data->tongue_target)) {
        data->tongue_active = false;
        data->tongue_target = nullptr;
        return;
    }

    // Calculate distance to midpoint for range check
    vec3_t start;
    G_EntMidPoint(data->tongue_target, start);
    vec3_t const diff = start - self->s.origin;
    float const dist_to_center = diff.length();

    // Release if out of range (check center distance)
    if (dist_to_center > BRAIN_TONGUE_RANGE) {
        data->tongue_active = false;
        data->tongue_target = nullptr;
        data->attack_finished = level.time + 1_sec;
        return;
    }

    // Check line of sight - trace in direction (like monster brain does) not to specific point
    vec3_t const normalized_dir = diff.normalized();
    vec3_t const end = self->s.origin + (normalized_dir * BRAIN_TONGUE_RANGE);
    trace_t tr = gi.traceline(self->s.origin, end, self, MASK_SHOT);
    bool has_los = (tr.ent == data->tongue_target);

    if (!has_los) {
        // Lost line of sight - target not hit by directional trace
        data->tongue_active = false;
        data->tongue_target = nullptr;
        return;
    }

    // Check if this is a destructible object (barrels, func_explosive, windows, etc)
    bool is_destructible = false;
    if (!data->tongue_target->client && !(data->tongue_target->svflags & SVF_MONSTER)) {
        // Check various ways to identify destructibles
        if ((data->tongue_target->flags & FL_DAMAGEABLE) ||
            (data->tongue_target->flags & FL_TRAP) ||
            horde::IsSpecialType(data->tongue_target, horde::SpecialEntityTypeID::BARREL) ||
            strcmp(data->tongue_target->classname, "misc_explobox") == 0 ||
            strcmp(data->tongue_target->classname, "func_explosive") == 0 ||
            strcmp(data->tongue_target->classname, "func_object") == 0 ||
            strcmp(data->tongue_target->classname, "horde_barrel") == 0) {
            is_destructible = true;
        }
    }

    // Only face living targets (not inanimate objects like barrels)
    if (!is_destructible) {
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
    }

    // Calculate pull and damage values
    const vec3_t dir = diff.normalized();
    int pull = 70;  // Same as m_brain.cpp
    int damage = BRAIN_TONGUE_DAMAGE; // Use constant from header

    // Increase pull if on ground (but not for destructibles)
    if (!is_destructible && data->tongue_target->groundentity)
        pull *= 2;

    // For damage check, calculate distance to nearest point on bounding box (surface)
    // This is critical for large bosses - we want to damage when touching their surface, not center
    vec3_t closest_point;
    for (int i = 0; i < 3; i++) {
        closest_point[i] = std::clamp(self->s.origin[i],
            data->tongue_target->absmin[i],
            data->tongue_target->absmax[i]);
    }
    float const dist_to_surface = (closest_point - self->s.origin).length();

    // Apply effects only when in melee range (64 units to surface, not center!)
    if (dist_to_surface <= 64) {
        // Apply damage and pull
        if (is_destructible) {
            // For destructibles, don't pull - just damage
            T_Damage(data->tongue_target, self, self, dir, data->tongue_target->s.origin,
                vec3_origin, damage, 0, DAMAGE_DESTROY_ARMOR, MOD_BRAINTENTACLE);
        } else {
            // For enemies, apply damage and pull
            T_Damage(data->tongue_target, self, self, dir, data->tongue_target->s.origin,
                vec3_origin, damage, -pull, DAMAGE_DESTROY_ARMOR, MOD_BRAINTENTACLE);
        }

        // Steal health only from living enemies (not destructibles)
        if (!is_destructible && level.time >= data->last_steal_time) {
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
        // If not close enough to damage, just pull (non-destructibles only)
        if (!is_destructible) {
            T_Damage(data->tongue_target, self, self, dir, data->tongue_target->s.origin,
                vec3_origin, 0, -pull, DAMAGE_DESTROY_ARMOR, MOD_BRAINTENTACLE);
        }
    }

    // Set next attack time - attack continues at regular intervals
    data->attack_finished = level.time + FRAME_TIME_MS * 2; // Every 2 frames like monster AI

    // Update enemy pointer for targeting (only for actual enemies)
    if (!is_destructible) {
        self->enemy = data->tongue_target;
    }
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

        // Skip stationary entities
        if (target->flags & FL_STATIONARY)
            continue;

        // Check if it's a valid enemy (monster, player, or other damageable entity)
        bool is_valid_target = false;

        // Check if it's a monster
        if (target->svflags & SVF_MONSTER) {
            if (!OnSameTeam(self, target) && !BrainTongueIsFriendly(target))
                is_valid_target = true;
        }
        // Check if it's a player
        else if (target->client) {
            // In horde every player is a teammate, whatever their ctf_team says
            if (!g_horde->integer && !OnSameTeam(self, target))
                is_valid_target = true;
        }
        // Check if it's a damageable entity like barrel, explosive box, etc.
        else if (target->takedamage && !target->client && !(target->svflags & SVF_MONSTER)) {
            // Target destructible objects like barrels, buttons, etc.
            // These don't have teams, so they're always valid targets
            is_valid_target = true;
        }

        if (!is_valid_target)
            continue;

        // Use midpoint for distance calculation
        vec3_t target_mid;
        G_EntMidPoint(target, target_mid);
        float dist = (target_mid - self->s.origin).length();
        if (dist < best_dist) {
            // Check line of sight - trace in direction like monster brain does
            vec3_t const dir = (target_mid - self->s.origin).normalized();
            vec3_t const end = self->s.origin + (dir * BRAIN_TONGUE_RANGE);
            trace_t tr = gi.traceline(self->s.origin, end, self, MASK_SHOT);
            if (tr.ent == target) {
                best = target;
                best_dist = dist;
            }
        }
    }

    // Also check players specifically (in case they're not in the monster grid)
    // -- never in horde, where every player is a teammate
    for (int i = 1; !g_horde->integer && i <= static_cast<int>(game.maxclients); i++) {
        edict_t* ent = &g_edicts[i];

        if (!ent || !ent->inuse || !ent->client || ent == self)
            continue;

        if (ent->health <= 0 || ent->deadflag)
            continue;

        // Skip spectators
        if (ent->client->resp.spectator)
            continue;

        // Skip stationary entities (though players shouldn't normally have this flag)
        if (ent->flags & FL_STATIONARY)
            continue;

        // Check team
        if (OnSameTeam(self, ent))
            continue;

        // Use midpoint for distance calculation
        vec3_t ent_mid;
        G_EntMidPoint(ent, ent_mid);
        float dist = (ent_mid - self->s.origin).length();
        if (dist < best_dist) {
            // Check line of sight - trace in direction like monster brain does
            vec3_t const dir = (ent_mid - self->s.origin).normalized();
            vec3_t const end = self->s.origin + (dir * BRAIN_TONGUE_RANGE);
            trace_t tr = gi.traceline(self->s.origin, end, self, MASK_SHOT);
            if (tr.ent == ent) {
                best = ent;
                best_dist = dist;
            }
        }
    }

    // IMPORTANT: Also check for destructible objects like barrels (misc_explobox)
    // These aren't in the monster grid, so we need to search all entities
    for (int i = 0; i < static_cast<int>(globals.num_edicts); i++) {
        edict_t* ent = &g_edicts[i];

        if (!ent || !ent->inuse || ent == self)
            continue;

        // Skip if not damageable or already dead
        if (!ent->takedamage || ent->health <= 0 || ent->deadflag)
            continue;

        // Skip entities with teams, monsters, and players (already handled above)
        if (ent->svflags & SVF_MONSTER || ent->client)
            continue;

        // Player deployables carry FL_TRAP/FL_DAMAGEABLE but are teammates, not prey
        if (BrainTongueIsFriendly(ent))
            continue;

        // Check if it's a destructible object (barrels, func_explosive, windows, etc)
        bool is_valid_destructible = false;
        if ((ent->flags & FL_DAMAGEABLE) ||
            (ent->flags & FL_TRAP) ||
            horde::IsSpecialType(ent, horde::SpecialEntityTypeID::BARREL) ||
            strcmp(ent->classname, "misc_explobox") == 0 ||
            strcmp(ent->classname, "func_explosive") == 0 ||
            strcmp(ent->classname, "func_object") == 0 ||
            strcmp(ent->classname, "horde_barrel") == 0) {
            is_valid_destructible = true;
        }

        if (!is_valid_destructible)
            continue;

        float dist = (ent->s.origin - self->s.origin).length();
        if (dist < best_dist) {
            // Check line of sight - try origin, midpoint, and for BSP entities, absmin/absmax center
            vec3_t mid;
            G_EntMidPoint(ent, mid);

            trace_t tr = gi.traceline(self->s.origin, ent->s.origin, self, MASK_SHOT);
            bool can_hit = (tr.fraction >= 0.9f || tr.ent == ent);

            if (!can_hit) {
                // Try midpoint if origin trace failed
                tr = gi.traceline(self->s.origin, mid, self, MASK_SHOT);
                can_hit = (tr.fraction >= 0.9f || tr.ent == ent);
            }

            // For SOLID_BSP entities (func_explosive), also try absmin/absmax center
            if (!can_hit && ent->solid == SOLID_BSP) {
                vec3_t center = (ent->absmin + ent->absmax) * 0.5f;
                tr = gi.traceline(self->s.origin, center, self, MASK_SHOT);
                can_hit = (tr.fraction >= 0.9f || tr.ent == ent);
            }

            if (can_hit) {
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


    // // POWER ARMOR REGENERATION (using blaster ammo as cells)

    // // Regenerate power screen cells (slower than flyer's blaster regeneration)
    // // Brain regenerates cells slower - every 15 frames instead of flyer's 8
    // if (!(ent->client->buttons & BUTTON_ATTACK) && ent->client->blaster_ammo < 100) {
    //     // Regenerate 1 cell every 15 frames (750ms at 20fps) - slower than flyer
    //     if (level.time >= ent->client->blaster_regen_time) {
    //         ent->client->blaster_ammo++;
    //         // Cap at 100 cells for brain (double the flyer's 50)
    //         if (ent->client->blaster_ammo > 100)
    //             ent->client->blaster_ammo = 100;

    //         // Set next regen time - 15 frames worth (750ms) - slower than flyer's 8 frames
    //         ent->client->blaster_regen_time = level.time + (FRAME_TIME_MS * 15);
    //     }
    // }
}

// ==================== Frame Management ====================

void RunBrainFrames(edict_t* ent, const usercmd_t& ucmd) {
    auto* data = GetMorphData(ent);
    if (!data || data->morph_type != MORPH_BRAIN || ent->deadflag)
        return;

    // Clear weapon model
    ent->s.modelindex2 = 0;
    ent->s.skinnum = 0;

    // Display power screen cells using the blaster ammo system
    ent->client->ps.stats[STAT_AMMO_ICON] = gi.imageindex("a_cells");
    ent->client->ps.stats[STAT_AMMO] = ent->client->blaster_ammo;

    // // Keep power screen always active for brain
    // ent->flags |= FL_POWER_ARMOR;
    // ent->client->pers.inventory[IT_ITEM_POWER_SCREEN] = 1;

    // // Make power screen use blaster ammo as cells
    // ent->client->pers.inventory[IT_AMMO_CELLS] = ent->client->blaster_ammo;

    // Clear effects
    ent->s.effects &= ~EF_TELEPORTER;

    // Store old frame for interpolation
    ent->s.old_frame = ent->s.frame;

    // Regeneration
    BrainRegenerate(ent);

    // Clear enemy pointer each frame - we'll find fresh target when attacking
    ent->enemy = nullptr;

    // Track if we're jumping for animation
    bool is_jumping = !ent->groundentity && (ent->waterlevel < 2);

    // Check if we're moving
    bool is_moving = (ucmd.forwardmove != 0) || (ucmd.sidemove != 0);

    // Only advance animations at controlled intervals (not every frame)
    // Use 10Hz (100ms) for normal animation speed
    const bool should_advance_frame = (level.time >= data->next_frame_time);

    // Handle attacks - only allow when on ground and not moving
    if ((ucmd.buttons & BUTTON_ATTACK) && ent->groundentity && !is_moving) {
        // Start attack if not already attacking
        if (!data->tongue_active) {
            BrainTongueAttack(ent, data);
        }

        // Continue damaging while button is held
        if (data->tongue_active) {
            BrainTongueAttackContinue(ent, data);

            // Animation for attack - initial frames then continuous loop
            if (ent->s.frame < BRAIN_FRAMES_ATTACK_START || ent->s.frame > BRAIN_FRAMES_ATTACK_END) {
                // Start the attack animation
                ent->s.frame = BRAIN_FRAMES_ATTACK_START;
                ent->s.renderfx |= RF_OLD_FRAME_LERP;
            }
            else if (should_advance_frame) {
                // Advance through attack frames
                if (ent->s.frame < BRAIN_FRAMES_ATTACK_LOOP_START) {
                    // Still in initial attack frames, advance normally
                    ent->s.frame++;
                }
                else if (ent->s.frame >= BRAIN_FRAMES_ATTACK_LOOP_END) {
                    // Reached end of loop, go back to loop start
                    ent->s.frame = BRAIN_FRAMES_ATTACK_LOOP_START;
                }
                else {
                    // In the loop frames, advance and wrap around
                    ent->s.frame++;
                }
                data->next_frame_time = level.time + 10_hz; // 100ms per frame (10Hz)
                ent->s.renderfx |= RF_OLD_FRAME_LERP;
            }
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
        // Jump animation - simply hold FRAME_duck05 (158) while airborne
        ent->s.frame = BRAIN_FRAMES_JUMP_HOLD;  // FRAME_duck05
        ent->s.renderfx |= RF_OLD_FRAME_LERP;
    } else if (!data->tongue_active) {
        if (is_moving) {
            // Walking animation - controlled speed with interpolation
            if (ent->s.frame < BRAIN_FRAMES_WALK_START || ent->s.frame > BRAIN_FRAMES_WALK_END) {
                ent->s.frame = BRAIN_FRAMES_WALK_START;
                data->next_frame_time = level.time;
            }
            else if (should_advance_frame) {
                ent->s.frame++;
                if (ent->s.frame > BRAIN_FRAMES_WALK_END)
                    ent->s.frame = BRAIN_FRAMES_WALK_START;
                data->next_frame_time = level.time + 10_hz; // 100ms per frame (10Hz)
                ent->s.renderfx |= RF_OLD_FRAME_LERP;
            }
        } else {
            // Standing/idle animation
            if (ent->s.frame < BRAIN_FRAMES_STAND_START || ent->s.frame > BRAIN_FRAMES_STAND_END) {
                ent->s.frame = BRAIN_FRAMES_STAND_START;
                data->next_frame_time = level.time;
            }
            else if (should_advance_frame) {
                ent->s.frame++;
                if (ent->s.frame > BRAIN_FRAMES_STAND_END)
                    ent->s.frame = BRAIN_FRAMES_STAND_START;
                data->next_frame_time = level.time + 40_hz;
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
    data->next_frame_time = level.time + 10_hz; // Initialize frame timing for 10Hz animation speed

    // Clear any looping sounds (like chainsaw idle)
    ent->s.sound = 0;
    ent->client->weapon_sound = 0;

    // Set model and bounds
    ent->s.modelindex = gi.modelindex("models/monsters/brain/tris.md2");
    ent->s.modelindex2 = 0;
    ent->s.skinnum = 0;
    ent->s.frame = BRAIN_FRAMES_STAND_START; // Start with standing animation
    ent->s.old_frame = ent->s.frame; // Initialize old frame for interpolation
    ent->s.renderfx |= RF_OLD_FRAME_LERP; // Enable smooth frame interpolation
    ent->s.effects |= EF_QUAD | EF_COLOR_SHELL; // Add quad glow and shell effect
    ent->s.renderfx |= RF_SHELL_BLUE; // Add blue shell rendering

    // Set team - use player team field, not monster field
    ent->ctf_team = ent->client->resp.ctf_team;

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

    // // Initialize power screen using blaster ammo cells
    // ent->client->blaster_ammo = 50; // Start with 50 cells for power screen
    // ent->client->blaster_regen_time = level.time; // Initialize regen timer

    // // Enable power screen (always on for brain)
    // ent->flags |= FL_POWER_ARMOR;
    // ent->client->pers.inventory[IT_ITEM_POWER_SCREEN] = 1;

    // Play transformation sound
    gi.sound(ent, CHAN_WEAPON, gi.soundindex("misc/tele_up.wav"), 1, ATTN_NORM, 0);

    gi.LocClient_Print(ent, PRINT_HIGH, "Transformed into Brain! Hold attack to use tongue pull. Power screen active using cells. Type 'brain' again to transform back.\n");

    gi.linkentity(ent);
}

// This function is kept for compatibility but brain uses normal physics
void ApplyBrainPhysics(edict_t* ent) {
    // Brain uses normal player physics, nothing special needed
}