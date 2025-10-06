// Player to Flyer Morph System for Q2Remaster Horde Mod
#include "p_flyer_morph.h"
#include "../m_flash.h"
#include "../bots/bot_includes.h"
#include "g_horde_benefits.h"
#include "horde_ids.h"
#include <unordered_map>

[[nodiscard]] constexpr float SHORT2ANGLE(int16_t x) {
    return static_cast<float>(x) * (360.0f / 65536.0f);
}

// Static storage for morph data - using entity userdata would be cleaner but this works
std::unordered_map<edict_t*, morph_data_t> s_morph_data;

morph_data_t* GetMorphData(edict_t* ent) {
    auto it = s_morph_data.find(ent);
    if (it != s_morph_data.end()) {
        return &it->second;
    }
    return nullptr;
}

// Backward compatibility
flyer_data_t* GetFlyerData(edict_t* ent) {
    return GetMorphData(ent);
}

void InitMorphData(edict_t* ent, morph_type_t type) {
    morph_data_t& data = s_morph_data[ent];
    memset(&data, 0, sizeof(data));
    data.morph_type = type;
    data.ability_level = 1; // Default ability level
}

// Backward compatibility
void InitFlyerData(edict_t* ent) {
    InitMorphData(ent, MORPH_FLYER);
}

void ClearMorphData(edict_t* ent) {
    s_morph_data.erase(ent);
}

// Backward compatibility
void ClearFlyerData(edict_t* ent) {
    ClearMorphData(ent);
}

bool IsMorphed(edict_t* ent) {
    auto* data = GetMorphData(ent);
    return data && data->morph_type != MORPH_NONE;
}

// ==================== Flight Physics ====================

static void FlyerBrakeVertical(edict_t* ent) {
    if (ent->velocity[2] > 0) {
        ent->velocity[2] -= FLYER_BRAKE_SPEED;
        if (ent->velocity[2] < 0)
            ent->velocity[2] = 0;
    } else if (ent->velocity[2] < 0) {
        ent->velocity[2] += FLYER_BRAKE_SPEED;
        if (ent->velocity[2] > 0)
            ent->velocity[2] = 0;
    }
}

static void FlyerBrakeHorizontal(edict_t* ent) {
    // Brake X velocity
    if (ent->velocity[0] > 0) {
        ent->velocity[0] -= FLYER_BRAKE_SPEED;
        if (ent->velocity[0] < 0)
            ent->velocity[0] = 0;
    } else if (ent->velocity[0] < 0) {
        ent->velocity[0] += FLYER_BRAKE_SPEED;
        if (ent->velocity[0] > 0)
            ent->velocity[0] = 0;
    }

    // Brake Y velocity
    if (ent->velocity[1] > 0) {
        ent->velocity[1] -= FLYER_BRAKE_SPEED;
        if (ent->velocity[1] < 0)
            ent->velocity[1] = 0;
    } else if (ent->velocity[1] < 0) {
        ent->velocity[1] += FLYER_BRAKE_SPEED;
        if (ent->velocity[1] > 0)
            ent->velocity[1] = 0;
    }
}

static void FlyerAccelerate(edict_t* ent, const vec3_t& dir, float speed, float max_speed) {
    vec3_t move = ent->velocity + (dir * speed);
    float nspd = move.length();
    float cspd = ent->velocity.length();
    float value = max_speed - cspd;
    float max = 60.0f; // maximum brake speed

    if ((speed > 0) && (speed > value) && (nspd > cspd)) {
        if (value > -max)
            speed = value;
        else
            speed = -max;
        vec3_t newdir = ent->velocity;
        newdir.normalize();
        move = ent->velocity + (newdir * speed);
    } else if ((speed < 0) && (-speed > value) && (nspd > cspd)) {
        if (value > -max)
            speed = value;
        else
            speed = -max;
        vec3_t newdir = ent->velocity;
        newdir.normalize();
        move = ent->velocity + (newdir * speed);
    }

    ent->velocity = move;
}

static void FlyerVerticalThrust(edict_t* ent, float speed, float max_speed) {
    float max = 60.0f;
    float cspd = ent->velocity[2];
    float nspd = cspd + speed;
    float delta = max_speed - fabsf(cspd);

    if (speed > 0) { // going up
        if ((delta > speed) || (nspd < 0))
            ent->velocity[2] += speed;
        else if (delta > 0)
            ent->velocity[2] += delta;
        else if (delta < 0) {
            if (delta < -max)
                ent->velocity[2] -= max;
            else
                ent->velocity[2] += delta;
        }
    } else { // going down
        if ((delta > -speed) || (nspd > 0))
            ent->velocity[2] += speed;
        else if (delta > 0)
            ent->velocity[2] -= delta;
        else if (delta < 0) {
            if (delta < -max)
                ent->velocity[2] += max;
            else
                ent->velocity[2] += delta;
        }
    }
}

static void PlayerAutoThrust(edict_t* ent, const usercmd_t& ucmd) {
    vec3_t forward, right, up;
    AngleVectors(ent->client->v_angle, &forward, &right, &up);

    float speed = 400.0f; // Base flying speed
    auto* data = GetFlyerData(ent);
    if (data && data->ability_level > 3) {
        speed += (data->ability_level - 3) * 50;
    }

    // Calculate desired movement velocity
    vec3_t move_vel = { 0, 0, 0 };

    // Forward/backward movement with vertical component based on view pitch
    // This makes flying feel like spectator/noclip mode
    if (ucmd.forwardmove > 0) {
        move_vel = move_vel + (forward * speed);
    } else if (ucmd.forwardmove < 0) {
        move_vel = move_vel + (forward * -speed);
    }

    // Strafe movement (stays horizontal)
    if (ucmd.sidemove > 0) {
        vec3_t right_horizontal = right;
        right_horizontal[2] = 0;  // Remove vertical component
        right_horizontal.normalize();
        move_vel = move_vel + (right_horizontal * speed);
    } else if (ucmd.sidemove < 0) {
        vec3_t right_horizontal = right;
        right_horizontal[2] = 0;  // Remove vertical component
        right_horizontal.normalize();
        move_vel = move_vel + (right_horizontal * -speed);
    }

    // Optional: Add jump/crouch for direct vertical movement
    // This gives additional control beyond pitch-based movement
    if (ucmd.buttons & BUTTON_JUMP) {
        move_vel[2] += speed * 0.5f;  // Slower direct vertical
    } else if (ucmd.buttons & BUTTON_CROUCH) {
        move_vel[2] -= speed * 0.5f;
    }

    // Apply movement with momentum for smoother flight
    float momentum = 0.85f;  // Slightly higher momentum for smoother flight
    ent->velocity = (ent->velocity * momentum) + (move_vel * (1.0f - momentum));

    // Cap maximum velocity
    float vel = ent->velocity.length();
    if (vel > FLYER_MAX_VELOCITY) {
        ent->velocity = ent->velocity.normalized() * FLYER_MAX_VELOCITY;
    }
}

// ==================== Combat Functions ====================

static void FlyerCheckForImpact(edict_t* ent, flyer_data_t* data) {
    float speed = ent->velocity.length();

    if (data->old_speed - speed > FLYER_IMPACT_VELOCITY) {
        gi.sound(ent, CHAN_AUTO, gi.soundindex("tank/thud.wav"), 1, ATTN_NORM, 0);
        T_Damage(ent, ent, ent, vec3_origin, ent->s.origin,
            vec3_origin, FLYER_IMPACT_DAMAGE, FLYER_IMPACT_DAMAGE, DAMAGE_NONE, MOD_UNKNOWN);
        T_RadiusDamage(ent, ent, FLYER_IMPACT_DAMAGE, nullptr, 64, DAMAGE_NONE, MOD_UNKNOWN);
    }
    data->old_speed = speed;
}

static void FireSmartRocket(edict_t* ent, const vec3_t& start, const vec3_t& dir, int damage, int speed, int radius) {
    // Create rocket entity
    edict_t* rocket = G_Spawn();
    rocket->s.origin = start;
    rocket->s.angles = vectoangles(dir);
    rocket->velocity = dir * speed;
    rocket->movetype = MOVETYPE_FLYMISSILE;
    rocket->clipmask = MASK_SHOT;
    rocket->solid = SOLID_BBOX;
    rocket->s.effects |= EF_ROCKET;
    rocket->s.modelindex = gi.modelindex("models/objects/rocket/tris.md2");
    rocket->owner = ent;
    rocket->touch = rocket_touch;
    rocket->nextthink = level.time + 8_sec;
    rocket->think = G_FreeEdict;
    rocket->dmg = damage;
    rocket->radius_dmg = damage;
    rocket->dmg_radius = radius;
    rocket->classname = "rocket";

    // if (ent->client)
    //     check_dodge(ent, rocket->s.origin, dir, speed);

    gi.linkentity(rocket);
}

static void FlyerAttackHyperblaster(edict_t* ent, flyer_data_t* data) {
    if (level.time < data->attack_finished)
        return;

    // Use the same blaster ammo system as the blaster weapon
    if (ent->client->blaster_ammo < FLYER_HB_AMMO)
        return;

    ent->client->blaster_ammo -= FLYER_HB_AMMO;

    int damage = FLYER_HB_INITIAL_DMG + FLYER_HB_ADDON_DMG * data->ability_level;

    vec3_t forward, right, start;
    AngleVectors(ent->client->v_angle, &forward, &right, nullptr);

    // Fire from both sides like the flyer monster
    vec3_t offset1 = { 12, -12, -12 };
    start = G_ProjectSource(ent->s.origin, offset1, forward, right);
    fire_blaster(ent, start, forward, damage, FLYER_HB_SPEED, EF_HYPERBLASTER, MOD_HYPERBLASTER, 0);

    vec3_t offset2 = { 12, 12, -12 };
    start = G_ProjectSource(ent->s.origin, offset2, forward, right);
    fire_blaster(ent, start, forward, damage, FLYER_HB_SPEED, EF_HYPERBLASTER, MOD_HYPERBLASTER, 0);

    gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/hyprbf1a.wav"), 1, ATTN_NORM, 0);

    data->attack_finished = level.time + FLYER_HB_REFIRE_TIME;
}

static void FlyerAttackRocket(edict_t* ent, flyer_data_t* data) {
    // Charge up the rocket
    if (!data->refire_frames) {
        gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/rockfly.wav"), 1, ATTN_IDLE, 0);
    }
    data->refire_frames++;

    // Check for lock-on target
    vec3_t forward, right, start, end;
    AngleVectors(ent->client->v_angle, &forward, &right, nullptr);

    vec3_t offset = { 0.f, 8.f, (float)(ent->viewheight - 8) };
    start = G_ProjectSource(ent->s.origin, offset, forward, right);
    end = start + (forward * 8192);

    trace_t tr = gi.traceline(start, end, ent, MASK_SHOT);

    if (tr.ent && tr.ent->takedamage && tr.ent != ent) {
        if (data->lock_target != tr.ent) {
            data->lock_target = tr.ent;
            data->lock_frames = 1;
        } else {
            data->lock_frames++;
            if (data->lock_frames == SMARTROCKET_LOCKFRAMES) {
                gi.LocClient_Print(ent, PRINT_CENTER, "Target locked\n");
                if (tr.ent->client)
                    gi.LocClient_Print(tr.ent, PRINT_CENTER, "Incoming rocket\n");
            }
        }
    } else {
        data->lock_target = nullptr;
        data->lock_frames = 0;
    }

    // Fire rocket after charge time
    if (level.time >= data->attack_finished && data->refire_frames >= 10) {
        if (data->current_ammo < FLYER_ROCKET_AMMO)
            return;

        data->current_ammo -= FLYER_ROCKET_AMMO;

        int damage = FLYER_ROCKET_INITIAL_DMG + FLYER_ROCKET_ADDON_DMG * data->ability_level;
        int radius = FLYER_ROCKET_INITIAL_RADIUS + FLYER_ROCKET_ADDON_RADIUS * data->ability_level;

        AngleVectors(ent->client->v_angle, &forward, nullptr, nullptr);
        FireSmartRocket(ent, ent->s.origin, forward, damage, FLYER_ROCKET_SPEED, radius);

        data->refire_frames = 0;
        data->lock_frames = 0;
        data->lock_target = nullptr;
        data->attack_finished = level.time + 1_sec;

        gi.sound(ent, CHAN_VOICE, gi.soundindex("tank/rocket.wav"), 1, ATTN_NORM, 0);
    }
}

static void FlyerAttack(edict_t* ent, flyer_data_t* data) {
    if (data->weapon_mode) {
        FlyerAttackRocket(ent, data);
    } else {
        FlyerAttackHyperblaster(ent, data);
    }
}

// ==================== Regeneration ====================

void MorphRegenerate(edict_t* ent) {
    auto* data = GetFlyerData(ent);
    if (!data || data->morph_type != MORPH_FLYER)
        return;

    // Note: Ammo regeneration is now handled by the blaster ammo system in p_view.cpp

    // Regenerate health
    if (ent->health < ent->max_health) {
        if (level.time >= ent->timestamp + FLYER_REGEN_DELAY) {
            ent->health += FLYER_REGEN_AMOUNT;
            if (ent->health > ent->max_health)
                ent->health = ent->max_health;
            ent->timestamp = level.time;
        }
    }
}

// ==================== Frame Management ====================

// PASTE THIS ENTIRE FUNCTION INTO YOUR CODE
void RunFlyerFrames(edict_t* ent, const usercmd_t& ucmd) {
    auto* data = GetFlyerData(ent);
    if (!data || data->morph_type != MORPH_FLYER || ent->deadflag == true)
        return;

    // Clear weapon model
    ent->s.modelindex2 = 0;
    ent->s.skinnum = 0;
    ent->s.sound = 0;

    // Store old frame for interpolation
    ent->s.old_frame = ent->s.frame;

    // Animation timing control - only advance animations at proper intervals
    const gtime_t current_time = level.time;
    const bool should_advance_frame = (current_time >= data->next_frame_time);

    // Set weapon stats to show actual blaster ammo when morphed as flyer
    // Ensure blaster is selected and show its regenerating ammo
    auto* blaster = FindItem("Blaster");
    if (blaster) {
        // Select blaster if not already selected
        if (ent->client->pers.weapon != blaster) {
            ent->client->pers.weapon = blaster;
            ent->client->pers.selected_item = blaster->id;
        }
        
        // Show blaster ammo icon and actual blaster ammo count
        ent->client->ps.stats[STAT_AMMO_ICON] = gi.imageindex("a_cells");
        ent->client->ps.stats[STAT_AMMO] = ent->client->blaster_ammo;
    }

    // NOTE: All movement, collision, and angle updates have been removed.
    // Pmove in ClientThink is now handling this for smooth, correct flight.

    // Check for impact damage
    FlyerCheckForImpact(ent, data);

    // Regeneration
    MorphRegenerate(ent);

    // Handle attacks - This will now use the correct angles updated in ClientThink
    if (ent->client->buttons & BUTTON_ATTACK) {
        FlyerAttack(ent, data);
    } else {
        data->refire_frames = 0;
        data->lock_frames = 0;
        data->lock_target = nullptr;
    }

    // Handle weapon mode switching
    if ((ent->client->buttons & BUTTON_USE) &&
        !(ent->client->oldbuttons & BUTTON_USE)) {
        data->weapon_mode = !data->weapon_mode;
        gi.LocClient_Print(ent, PRINT_HIGH, data->weapon_mode ?
            "Switched to Smart Rockets\n" : "Switched to Hyperblaster\n");
    }

    // Animation frames for banking with proper timing
    if (ucmd.sidemove > 0) {
        // Banking right animation
        if (ent->s.frame < FLYER_FRAMES_BANK_R_START || ent->s.frame > FLYER_FRAMES_BANK_R_END) {
            ent->s.frame = FLYER_FRAMES_BANK_R_START;
            data->next_frame_time = current_time;
        }
        else if (ent->s.frame == FLYER_FRAMES_BANK_R_END) {
            // Hold the final banking frame
            ent->s.renderfx |= RF_OLD_FRAME_LERP;
        }
        else if (should_advance_frame && ent->s.frame < FLYER_FRAMES_BANK_R_END) {
            ent->s.frame++;
            data->next_frame_time = current_time + 20_hz; // 50ms per frame for banking
            ent->s.renderfx |= RF_OLD_FRAME_LERP;
        }
    } else if (ucmd.sidemove < 0) {
        // Banking left animation
        if (ent->s.frame < FLYER_FRAMES_BANK_L_START || ent->s.frame > FLYER_FRAMES_BANK_L_END) {
            ent->s.frame = FLYER_FRAMES_BANK_L_START;
            data->next_frame_time = current_time;
        }
        else if (ent->s.frame == FLYER_FRAMES_BANK_L_END) {
            // Hold the final banking frame
            ent->s.renderfx |= RF_OLD_FRAME_LERP;
        }
        else if (should_advance_frame && ent->s.frame < FLYER_FRAMES_BANK_L_END) {
            ent->s.frame++;
            data->next_frame_time = current_time + 20_hz; // 50ms per frame for banking
            ent->s.renderfx |= RF_OLD_FRAME_LERP;
        }
    } else {
        // Default standing/hover animation - slow for smooth idle
        if (ent->s.frame < FLYER_FRAMES_STAND_START || ent->s.frame > FLYER_FRAMES_STAND_END) {
            ent->s.frame = FLYER_FRAMES_STAND_START;
            data->next_frame_time = current_time;
        }
        else if (should_advance_frame) {
            ent->s.frame++;
            if (ent->s.frame > FLYER_FRAMES_STAND_END)
                ent->s.frame = FLYER_FRAMES_STAND_START;
            // Use 5Hz (200ms) for slow smooth idle animation like brain
            data->next_frame_time = current_time + 15_hz;
            ent->s.renderfx |= RF_OLD_FRAME_LERP;
        }
    }
}

// ==================== Transformation ====================

void RestoreMorphed(edict_t* ent) {
    auto* data = GetMorphData(ent);
    if (!data || data->morph_type == MORPH_NONE)
        return;

    // Clear morph data first
    ClearMorphData(ent);

    // Clear special entity type
    ent->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::UNKNOWN);

    // Clear quad shell effect
    ent->s.effects &= ~EF_QUAD;

    // Store current position before respawn
    vec3_t old_origin = ent->s.origin;
    vec3_t old_angles = ent->s.angles;
    vec3_t old_velocity = ent->velocity;

    // Clear monster team
    ent->monsterinfo.team = Team_None;

    // Reset movement and physics flags
    ent->movetype = MOVETYPE_WALK;
    ent->flags &= ~FL_FLY;
    ent->gravity = 1.0;
    ent->solid = SOLID_BBOX;
    ent->clipmask = MASK_PLAYERSOLID;
    ent->svflags = SVF_PLAYER;
    ent->deadflag = false;

    // Call PutClientInServer to properly reinitialize the player
    // This will reset models, bounds, viewheight, etc.
    PutClientInServer(ent);

    // Restore position after respawn
    ent->s.origin = old_origin;
    ent->s.old_origin = old_origin;
    ent->s.angles = old_angles;
    ent->velocity = old_velocity;

    // Make sure player is slightly above ground to prevent getting stuck
    ent->s.origin[2] += 10;

    // Clear any remaining velocity from flying
    ent->velocity[2] = 0;

    // Force a ground check
    ent->groundentity = nullptr;

    // Update entity linkage
    gi.linkentity(ent);

    // Play transformation sound
    gi.sound(ent, CHAN_WEAPON, gi.soundindex("misc/tele_up.wav"), 1, ATTN_NORM, 0);
}

void Cmd_PlayerToFlyer_f(edict_t* ent) {
    if (!ent->client)
        return;

    // Check if already morphed - if so, unmorph
    if (IsMorphed(ent)) {
        RestoreMorphed(ent);
        gi.LocClient_Print(ent, PRINT_HIGH, "Transformed back to human form.\n");
        return;
    }

    // Add cooldown to prevent spam morphing (2 second cooldown)
    auto* existing_data = GetFlyerData(ent);
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

    // Check for power cubes cost
   // int cost = FLYER_INIT_COST;
   // auto* inventory = ent->client->pers.inventory;
  //  item_id_t cube_index = IT_KEY_POWER_CUBE;

    // if (inventory[cube_index] < cost) {
    //     gi.LocClient_Print(ent, PRINT_HIGH, "Not enough power cubes! Need {} cubes.\n", cost);
    //     return;
    // }

    // Cannot morph while carrying flag in CTF
    // if (CTFPlayerHasFlag(ent)) {
    //     gi.LocClient_Print(ent, PRINT_HIGH, "Can't morph while carrying flag!\n");
    //     return;
    // }

    // Deduct cost
   // inventory[cube_index] -= cost;

    // Initialize flyer data
    InitFlyerData(ent);
    auto* data = GetFlyerData(ent);

    // Set ability level based on player stats if available
    data->ability_level = 1 + (ent->client->resp.score / 100); // Example scaling
    if (data->ability_level > 10)
        data->ability_level = 10;

    // Transform into flyer
    data->morph_type = MORPH_FLYER;
    data->morph_time = level.time;
    data->attack_finished = level.time + 500_ms;
    data->next_frame_time = level.time + 10_hz; // Initialize frame timing for animations

    // Set special entity type for M_ReactToDamage
    ent->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::MORPHED_PLAYER);

    // Clear any looping sounds (like chainsaw idle)
    ent->s.sound = 0;
    ent->client->weapon_sound = 0;

    // Set model and bounds
    ent->s.modelindex = gi.modelindex("models/monsters/flyer/tris.md2");
    ent->s.modelindex2 = 0;
    ent->s.skinnum = 0;
    ent->s.frame = FLYER_FRAMES_STAND_START; // Start with standing animation
    ent->s.old_frame = ent->s.frame; // Initialize old frame for interpolation
    ent->s.renderfx |= RF_OLD_FRAME_LERP; // Enable smooth frame interpolation
    ent->s.effects |= EF_QUAD; // Add quad shell effect

    // Set team for bot recognition (don't set isfriendlyspawn to avoid AI treating as friendly summon)
    ent->monsterinfo.team = ent->client->resp.ctf_team;

    // Use proper flyer bounds from monster definition
    ent->mins = { -16, -16, -24 };
    ent->maxs = { 16, 16, 32 }; // Fixed: was 8, should be 32
    ent->viewheight = 12; // Give some view height for flying

    // Keep MOVETYPE_NOCLIP for player control but set clipmask for shooting
    ent->movetype = MOVETYPE_WALK ; // Keeps player controls working
    ent->flags |= FL_FLY;
    ent->gravity = 0; // No gravity while flying
    
    // Set clipmask so we can be shot and shoot others
    ent->clipmask = MASK_PLAYERSOLID; // This allows traces to work for shooting
    ent->svflags = SVF_PLAYER;
    // Mark that we need velocity-based movement
    ent->groundentity = nullptr; // Not on ground

    // Lift player much higher off ground to prevent getting stuck
    ent->s.origin[2] += 40;
    ent->velocity[2] = 100; // Give upward velocity to start

    // Update collision after position change
    gi.linkentity(ent);

    // Flyer morph now uses the blaster ammo system (capped at 50 cells)
    // Initialize with some ammo if player has none
    if (ent->client->blaster_ammo < 10)
        ent->client->blaster_ammo = 10;

    // Clear weapon
    ent->client->ps.gunindex = 0;

    // Play transformation sound
    gi.sound(ent, CHAN_WEAPON, gi.soundindex("misc/tele_up.wav"), 1, ATTN_NORM, 0);

    ent->svflags = SVF_PLAYER;

    gi.LocClient_Print(ent, PRINT_HIGH, "Transformed into Flyer! Type 'flyer' again to transform back.\n");

    gi.linkentity(ent);
}

// This function should be called from ClientThink after pmove
void ApplyFlyerPhysics(edict_t* ent) {
    auto* data = GetFlyerData(ent);
    if (!data || data->morph_type != MORPH_FLYER || ent->deadflag)
        return;

    // Keep MOVETYPE_NOCLIP for player control
    ent->movetype = MOVETYPE_WALK ;
    ent->gravity = 0;
    ent->groundentity = nullptr;

    // Maintain proper clipmask and solid for shooting/being shot
    ent->clipmask = MASK_PLAYERSOLID;
    ent->svflags = SVF_PLAYER;

    ent->solid = SOLID_BBOX;


    gi.linkentity(ent);
    // IMPORTANT: Clear velocity after each frame to prevent the engine
    // from moving us (since MOVETYPE_NOCLIP would apply velocity without collision)
    // We handle movement manually in RunFlyerFrames
    // Note: We store velocity for our calculations but clear it after physics
}