// --- START OF FILE g_laser.cpp ---
#include "g_local.h"
#include "shared.h"
#include "g_laser.h" // Include the header file
#include <array>
#include <unordered_map>
#include <new>
#include <cmath>

struct EmitterState {
    bool is_warning_phase = false;
    bool is_blink_on = false;
    gtime_t last_blink_time = 0_ms;
};

static std::unordered_map<const edict_t*, EmitterState> g_emitter_states;
// Forward declarations
void laser_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
void laser_beam_think(edict_t* self); // Forward declare the renamed think function

static int CalculateWaveBasedLaserDamage(int wave_level) {
    int effective_wave_level = std::max(1, wave_level);
    return LaserConstants::LASER_INITIAL_DAMAGE + (LaserConstants::LASER_ADDON_DAMAGE * (effective_wave_level - 1));
}

static int CalculateWaveBasedLaserMaxHealth(int wave_level) {
    int effective_wave_level = std::max(1, wave_level);
    return std::min(LaserConstants::LASER_INITIAL_HEALTH + (LaserConstants::LASER_ADDON_HEALTH * (effective_wave_level - 1)), LaserConstants::MAX_LASER_HEALTH);
}

// CORRECTED & IMPROVED Global function to update all active lasers
void G_UpdateActiveLasersForWaveProgression(int current_wave_level_from_game) {
    if (!g_horde || !g_horde->integer) return;
    if (developer && developer->integer) gi.Com_PrintFmt("Updating active lasers for wave: {}\n", current_wave_level_from_game);

    for (uint32_t i = 1; i <= globals.num_edicts; i++) {
        edict_t* ent = &g_edicts[i];
        if (!ent->inuse) continue;

        // Find the EMITTER first
        if (horde::IsSpecialType(ent, horde::SpecialEntityTypeID::LASER_EMITTER)) {
            edict_t* emitter = ent;
            edict_t* laser_beam = emitter->owner; // Get the beam from the emitter

            if (!laser_beam || !laser_beam->inuse) continue;

            int new_damage = CalculateWaveBasedLaserDamage(current_wave_level_from_game);
            int new_max_health = CalculateWaveBasedLaserMaxHealth(current_wave_level_from_game);

            if (laser_beam->dmg != new_damage) {
                if (developer && developer->integer > 1) gi.Com_PrintFmt("Laser (ent {}) damage: {} -> {}\n", (ptrdiff_t)(laser_beam - g_edicts), laser_beam->dmg, new_damage);
                laser_beam->dmg = new_damage;
            }

            if (new_max_health != laser_beam->max_health) {
                if (developer && developer->integer > 1) gi.Com_PrintFmt("Laser (ent {}) max_health: {} -> {} (current health: {})\n", (ptrdiff_t)(laser_beam - g_edicts), laser_beam->max_health, new_max_health, laser_beam->health);
                if (laser_beam->health > 0) {
                    float health_ratio = (laser_beam->max_health > 0) ? (float)laser_beam->health / (float)laser_beam->max_health : 1.0f;
                    int new_current_health = static_cast<int>(health_ratio * new_max_health);
                    laser_beam->health = std::max(1, std::min(new_current_health, new_max_health));
                    if (developer && developer->integer > 1) gi.Com_PrintFmt("Laser (ent {}) new current health: {}\n", (ptrdiff_t)(laser_beam - g_edicts), laser_beam->health);
                }
                laser_beam->max_health = new_max_health;
            }
        }
    }
}

PlayerLaserManager::PlayerLaserManager(edict_t* player) : owner(player) {}
bool PlayerLaserManager::can_add_laser() const { return active_count < LaserConstants::MAX_LASERS; }
void PlayerLaserManager::add_laser(edict_t* emitter, edict_t* beam) {
    if (!can_add_laser()) return;
    for (auto& entry : lasers) {
        if (!entry.active) {
            entry.emitter = emitter;
            entry.beam = beam;
            entry.active = true;
            active_count++;
            break;
        }
    }
}
void PlayerLaserManager::remove_laser(const edict_t* entity) {
    for (auto& entry : lasers) {
        if (entry.active && (entry.emitter == entity || entry.beam == entity)) {
            entry.active = false;
            entry.emitter = nullptr;
            entry.beam = nullptr;
            active_count--;
            break;
        }
    }
}
void PlayerLaserManager::remove_all_lasers() {
    for (int i = LaserConstants::MAX_LASERS - 1; i >= 0; --i) {
        auto& entry = lasers[i];
        if (entry.active) {
            edict_t* entity_to_die = (entry.emitter && entry.emitter->inuse) ? entry.emitter : entry.beam;
            if (entity_to_die && entity_to_die->inuse) {
                laser_die(entity_to_die, nullptr, owner, 9999, vec3_origin, MOD_UNKNOWN);
            } else {
                entry.active = false;
                entry.emitter = nullptr;
                entry.beam = nullptr;
                active_count--;
            }
        }
    }
}
int PlayerLaserManager::get_active_count() const { return active_count; }

namespace LaserHelpers {
    struct LaserHealth { bool healthy; uint32_t laser_color; uint32_t flare_color; };
    [[nodiscard]] static LaserHealth get_laser_health_state(const edict_t* laser) {
        if (!laser) return { false, 0xd0d1d2d3, 0x00FF00FF };
        const bool healthy = laser->health > laser->max_health * 0.20f;
        return { healthy, healthy ? 0xf2f2f0f0 : 0xd0d1d2d3, healthy ? 0xFF0000FF : 0x00FF00FF };
    }
    static float calculate_damage_multiplier(const edict_t* target) {
        if (!target || target->client || target->monsterinfo.issummoned) return 0.0f;
        if (target->svflags & SVF_MONSTER) {
            if (target->monsterinfo.invincible_time > level.time) return 0.0f;
            return target->monsterinfo.IS_BOSS ? 1.25f : 1.0f;
        }
        return LaserConstants::LASER_NONCLIENT_MOD;
    }
    static void update_visual_state(edict_t* ent, bool warning_state, bool blink_state) {
        if (!ent) return;
        if (warning_state && blink_state) { ent->s.renderfx |= RF_SHELL_GREEN; ent->s.effects |= EF_COLOR_SHELL; }
        else { ent->s.renderfx &= ~RF_SHELL_GREEN; ent->s.effects &= ~EF_COLOR_SHELL; }
    }
    PlayerLaserManager* get_laser_manager(edict_t* ent) {
        if (!ent || !ent->client) return nullptr;
        return ent->client->laser_manager.get();
    }
}

struct laser_pierce_t : pierce_args_t {
    edict_t* self;
    inline laser_pierce_t(edict_t* self_ptr) : pierce_args_t(), self(self_ptr) {}
    virtual bool hit(contents_t& mask, vec3_t& end) override {
        if (!self || self->health <= 0) return false;
        if (!tr.ent) return mark(nullptr);
        if (tr.ent->client && OnSameTeam(self->teammaster, tr.ent)) return false;
        if (self->dmg > 0 && tr.ent->takedamage && tr.ent != self->teammaster) {
            if ((tr.ent->svflags & SVF_MONSTER) && tr.ent->health <= 100) tr.ent->gib_health = 10;
            vec3_t forward;
            AngleVectors(self->s.angles, &forward, nullptr, nullptr);
            T_Damage(tr.ent, self, self->teammaster, forward, tr.endpos, vec3_origin, self->dmg, 0, DAMAGE_ENERGY, MOD_PLAYER_LASER);
            float const damageMult = LaserHelpers::calculate_damage_multiplier(tr.ent);
            if (damageMult >= 0.0f) self->health -= static_cast<int>(self->dmg * damageMult);
            if (self->health <= 0) {
                laser_die(self, self, self->teammaster, self->dmg, tr.endpos, MOD_PLAYER_LASER);
                return false;
            }
        }
        if (!(tr.ent->svflags & SVF_MONSTER) && !tr.ent->client && (tr.ent->solid != SOLID_NOT && tr.ent->solid != SOLID_TRIGGER)) return false;
        return mark(tr.ent);
    }
};

// A single, robust function to handle the death of any laser component.
DIE(laser_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void {
    if (!self || !self->inuse) {
        return;
    }

    edict_t* emitter = nullptr;
    edict_t* beam = nullptr;
    edict_t* teammaster = self->teammaster; // Cache before we start freeing things

    // Step 1: Identify the core components (emitter and beam)
     if (horde::IsSpecialType(self, horde::SpecialEntityTypeID::LASER_EMITTER)) {
        emitter = self;
        beam = self->owner; // Emitter owns the beam
    } else  if (horde::IsSpecialType(self, horde::SpecialEntityTypeID::LASER_BEAM)) {
        beam = self;
        emitter = self->owner; // Beam is owned by the emitter
    } else {
        // Not a known laser part, just free it to be safe.
        G_FreeEdict(self);
        return;
    }

    // Step 2: Update the player's manager FIRST. This is critical.
    // This prevents logic errors if we try to find the manager after freeing the player.
    if (teammaster && teammaster->inuse && teammaster->client) {
        if (auto* manager = LaserHelpers::get_laser_manager(teammaster)) {
            // Remove based on the emitter, which is the primary entity.
            // If emitter is null, use the beam. This covers all cases.
            manager->remove_laser(emitter ? emitter : beam);
            
            // Optional: You can print the "Laser destroyed" message here if you want.
            // gi.LocClient_Print(teammaster, PRINT_HIGH, "Laser destroyed. {}/{} remaining.\n",
            //    manager->get_active_count(), LaserConstants::MAX_LASERS);
        }
    }

    // Step 3: Clean up all associated edicts. Check for `inuse` at each step.
    if (emitter && emitter->inuse) {
        // Clean up the state map for the emitter
        g_emitter_states.erase(emitter);

        // Find and free the flare owned by the emitter
        for (uint32_t i = 1; i <= globals.num_edicts; i++) {
            edict_t* potential_flare = &g_edicts[i];
            if (potential_flare->inuse && potential_flare->classname &&
                strcmp(potential_flare->classname, "misc_flare") == 0 &&
                potential_flare->owner == emitter) {
                G_FreeEdict(potential_flare);
                break; // Assume only one
            }
        }
    }

    // Free the beam if it exists and isn't the entity we're about to explode
    if (beam && beam->inuse && beam != emitter) {
        G_FreeEdict(beam);
    }

    // Finally, handle the emitter's death.
    // If the emitter is the entity that died, start its explosion sequence.
    // This will handle freeing the emitter edict itself.
    if (emitter && emitter->inuse) {
        // If self is the emitter, it will be handled by BecomeExplosion1.
        // If self was the beam, we are now explicitly killing the emitter.
        emitter->health = 0; // Ensure it's dead
        emitter->takedamage = false;
        BecomeExplosion1(emitter);
    }
    
    // If self was the entity that initiated the die call, and it wasn't the emitter
    // (which is handled by BecomeExplosion1), it needs to be freed if it hasn't been already.
    // The check `if (beam && beam->inuse && beam != emitter)` handles this.
}
THINK(laser_beam_think)(edict_t* self) -> void {
    if (!self || !self->owner || !self->owner->inuse) {
        if (self && self->inuse) G_FreeEdict(self);
        return;
    }

    edict_t* emitter = self->owner;
    edict_t* player = self->teammaster; 
    if (!player || !player->inuse) {
        laser_die(self, self, nullptr, 0, vec3_origin, MOD_UNKNOWN);
        return;
    }

    // --- VISUAL UPDATE LOGIC ---
    
    // Check if the laser is in its final warning phase.
    bool const is_in_warning_phase = (level.time >= emitter->timestamp - LaserConstants::WARNING_TIME);

    // Only update colors and flares if NOT in the warning phase.
    // During the warning phase, emitter_think() has full control over the beam's color to make it blink.
    if (!is_in_warning_phase) {
        const auto health_state = LaserHelpers::get_laser_health_state(self);
        self->s.skinnum = health_state.laser_color;

        // Find the associated flare and update its color
        for (uint32_t i = 1; i <= globals.num_edicts; i++) {
            edict_t* potential_flare = &g_edicts[i];
            if (potential_flare->inuse && potential_flare->classname &&
                strcmp(potential_flare->classname, "misc_flare") == 0 &&
                potential_flare->owner == emitter)
            {
                potential_flare->s.skinnum = health_state.flare_color;
                break;
            }
        }
    }
    
    // Update the beam thickness regardless of the warning state.
    self->s.frame = (self->health < 1) ? 0 : (self->health >= 1000) ? 4 : 2;

    // --- TRACE LOGIC ---
    vec3_t forward;
    AngleVectors(emitter->s.angles, &forward, nullptr, nullptr);
    const vec3_t start = emitter->s.origin;
    
    const vec3_t end = start + (forward * 8192.0f);

    laser_pierce_t args(self);
    pierce_trace(start, end, emitter, args, MASK_SHOT);

    self->s.origin = args.tr.endpos;
    self->s.old_origin = start;
    self->nextthink = level.time + FRAME_TIME_MS;
    gi.linkentity(self);
}
THINK(emitter_think)(edict_t* self) -> void {
    // --- Step 1: Basic Validity and Ownership Check ---
    if (!self || !self->owner || !self->owner->inuse) {
        if (self && self->inuse) {
            laser_die(self, self, self->teammaster, 0, self->s.origin, MOD_UNKNOWN);
        }
        g_emitter_states.erase(self);
        return;
    }

    edict_t* beam = self->owner;

    // --- Step 2: Handle Timeout ---
    if (level.time >= self->timestamp) {
        if (self->teammaster && self->teammaster->client) {
            gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser timed out and was removed.\n");
        }
        laser_die(self, self, self->teammaster, 0, self->s.origin, MOD_UNKNOWN);
        return;
    }

    // --- Step 3: Handle Warning/Blink Visuals for ALL components ---
    auto& state = g_emitter_states[self];
    bool const should_warn = level.time >= self->timestamp - LaserConstants::WARNING_TIME;
    
    if (should_warn != state.is_warning_phase) {
        state.is_warning_phase = should_warn;
        state.last_blink_time = 0_ms;
        state.is_blink_on = false;
    }

    if (state.is_warning_phase && level.time >= state.last_blink_time + LaserConstants::BLINK_INTERVAL) {
        state.is_blink_on = !state.is_blink_on;
        state.last_blink_time = level.time;
    }

    // Update the emitter's visual effect (the green shell)
    if (state.is_warning_phase && state.is_blink_on) {
        self->s.renderfx |= RF_SHELL_GREEN;
        self->s.effects |= EF_COLOR_SHELL;
    } else {
        self->s.renderfx &= ~RF_SHELL_GREEN;
        self->s.effects &= ~EF_COLOR_SHELL;
    }

    // --- NEW: Update the laser beam AND flare COLOR to blink in sync ---
    if (state.is_warning_phase) {
        // Find the flare owned by this emitter
        edict_t* flare = nullptr;
        for (uint32_t i = 1; i <= globals.num_edicts; i++) {
            edict_t* potential_flare = &g_edicts[i];
            if (potential_flare->inuse && potential_flare->classname &&
                strcmp(potential_flare->classname, "misc_flare") == 0 &&
                potential_flare->owner == self) {
                flare = potential_flare;
                break;
            }
        }

        // Define the colors we'll be using
        constexpr uint32_t BLINK_COLOR_LASER_GREEN = 0xd0d1d2d3; // Damaged laser color
        constexpr uint32_t BLINK_COLOR_FLARE_GREEN = 0x00FF00FF; // Damaged flare color

        if (state.is_blink_on) {
            // When the blink is "on" (emitter is green), force the beam and flare to their "warning" colors.
            beam->s.skinnum = BLINK_COLOR_LASER_GREEN;
            if (flare) {
                flare->s.skinnum = BLINK_COLOR_FLARE_GREEN;
            }
        } else {
            // When the blink is "off", restore their normal health-based colors.
            const auto health_state = LaserHelpers::get_laser_health_state(beam);
            beam->s.skinnum = health_state.laser_color;
            if (flare) {
                flare->s.skinnum = health_state.flare_color;
            }
        }
    }

    // --- Step 4: Schedule the Next Think ---
    self->nextthink = level.time + FRAME_TIME_MS;
}
void create_laser(edict_t* ent) {
    if (!ent || !ent->client) return;
    if (!g_horde || !g_horde->integer) { gi.Client_Print(ent, PRINT_HIGH, "Need to be on Horde Mode to spawn a laser\n"); return; }
    if (ent->movetype != MOVETYPE_WALK) { gi.LocClient_Print(ent, PRINT_HIGH, "Need to be Non-Spect to create laser.\n"); return; }
    if (!ent->client->laser_manager) {
        try { ent->client->laser_manager = std::make_unique<PlayerLaserManager>(ent); }
        catch (const std::bad_alloc&) { ent->client->laser_manager = nullptr; return; }
    }
    PlayerLaserManager* manager = ent->client->laser_manager.get();
    if (!manager || !manager->can_add_laser()) { gi.LocClient_Print(ent, PRINT_HIGH, "Can't build any more lasers.\n"); return; }
    if (ent->client->pers.inventory[IT_AMMO_CELLS] < LaserConstants::LASER_COST) { gi.LocClient_Print(ent, PRINT_HIGH, "Not enough cells to create a laser.\n"); return; }
    vec3_t forward, right;
    AngleVectors(ent->client->v_angle, &forward, &right, nullptr);
    vec3_t const offset{ 0.0f, 8.0f, static_cast<float>(ent->viewheight) - 8.0f };
    vec3_t const start = G_ProjectSource(ent->s.origin, offset, forward, right);
    vec3_t const end = start + forward * 64;
    trace_t const tr = gi.traceline(start, end, ent, MASK_SOLID);
    if (tr.fraction == 1.0f) { gi.LocClient_Print(ent, PRINT_HIGH, "Too far from wall.\n"); return; }
    edict_t* laser = G_Spawn();
    edict_t* grenade = G_Spawn();
    edict_t* flare = G_Spawn();
    if (!laser || !grenade || !flare) {
        if (laser) G_FreeEdict(laser);
        if (grenade) G_FreeEdict(grenade);
        if (flare) G_FreeEdict(flare);
        gi.Com_Print("Error: Failed to spawn all laser components.\n");
        return;
    }
    
    // --- Configure Grenade (Emitter) ---
    grenade->classname = "emitter";
    grenade->special_type_id = static_cast<uint8_t>(horde::SpecialTypeRegistry::GetTypeID(grenade->classname));
    grenade->s.origin = tr.endpos;
    grenade->s.angles = vectoangles(tr.plane.normal);
    grenade->movetype = MOVETYPE_NONE;
    grenade->clipmask = MASK_SHOT;
    grenade->solid = SOLID_BBOX;
    grenade->mins = vec3_t{ -3, -3, 0 };
    grenade->maxs = vec3_t{ 3, 3, 6 };
    grenade->takedamage = true;
    grenade->health = 100; // Emitter has its own health, but it's not used for damage calc
    grenade->gib_health = -50;
    grenade->mass = 25;
    grenade->s.modelindex = gi.modelindex("models/objects/grenade2/tris.md2");
    grenade->teammaster = ent;
    grenade->owner = laser; // Emitter owns the beam
    grenade->think = emitter_think;
    grenade->nextthink = level.time + FRAME_TIME_MS;
    grenade->die = laser_die;
    grenade->svflags = SVF_BOT;
    grenade->timestamp = level.time + LaserConstants::LASER_TIMEOUT_DELAY;
    grenade->flags |= FL_NO_KNOCKBACK;
    if (ent->client->resp.ctf_team == CTF_TEAM1) grenade->team = TEAM1;
    else if (ent->client->resp.ctf_team == CTF_TEAM2) grenade->team = TEAM2;
    else grenade->team = "neutral";

    // --- Configure Laser (Beam) ---
    laser->classname = "laser";
    laser->special_type_id = static_cast<uint8_t>(horde::SpecialTypeRegistry::GetTypeID(laser->classname));
    laser->movetype = MOVETYPE_NONE;
    laser->solid = SOLID_NOT;
    laser->s.renderfx = RF_BEAM | RF_TRANSLUCENT;
    laser->s.modelindex = 1;
    laser->s.sound = gi.soundindex("world/laser.wav");
    laser->teammaster = ent;
    laser->owner = grenade; // Beam is owned by emitter
    laser->pos1 = tr.endpos;
    laser->s.angles = grenade->s.angles;
    laser->dmg = CalculateWaveBasedLaserDamage(current_wave_level);
    laser->max_health = CalculateWaveBasedLaserMaxHealth(current_wave_level);
    laser->health = laser->max_health;
    laser->gib_health = -100;
    laser->mass = 50;
    laser->takedamage = false; // The beam itself doesn't take damage directly, its health is a resource
    laser->die = laser_die;
    laser->think = laser_beam_think; // <-- USE THE RENAMED FUNCTION
    laser->nextthink = level.time + LaserConstants::LASER_SPAWN_DELAY;
    laser->flags |= FL_NO_KNOCKBACK;
    laser->team = grenade->team;
    const auto initial_health_state = LaserHelpers::get_laser_health_state(laser);
    laser->s.skinnum = initial_health_state.laser_color;
    laser->s.frame = (laser->health < 1) ? 0 : (laser->health >= 1000) ? 4 : 2;
    
    // --- Configure Flare ---
    flare->classname = "misc_flare";
    flare->s.origin = tr.endpos;
    flare->s.angles = { 90, 0, 0 };
    flare->owner = grenade; // Flare owned by emitter
    flare->spawnflags = 9_spawnflag;
    flare->s.skinnum = initial_health_state.flare_color;
    spawn_temp_t st{};
    st.radius = 0.5f;
    ED_CallSpawn(flare, st);
    
    gi.linkentity(grenade);
    gi.linkentity(laser);
    if (flare->inuse) gi.linkentity(flare);
    
    ent->client->pers.inventory[IT_AMMO_CELLS] -= LaserConstants::LASER_COST;
    manager->add_laser(grenade, laser);
    gi.LocClient_Print(ent, PRINT_HIGH, "Laser built. You have {}/{} lasers.\n", manager->get_active_count(), LaserConstants::MAX_LASERS);
}

void remove_lasers(edict_t* ent) noexcept {
    if (!ent || !ent->client) return;
    if (auto* manager = LaserHelpers::get_laser_manager(ent)) {
        manager->remove_all_lasers();
    }
}
// --- END OF FILE g_laser.cpp ---