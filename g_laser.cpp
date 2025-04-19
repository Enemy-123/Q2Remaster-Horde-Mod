// --- START OF FILE g_laser.cpp ---
#include "g_local.h"
#include "shared.h"
#include "laser.h" // Include the header file
#include <array>
#include <unordered_map>
#include <new>
#include <cmath>

// Forward declarations
void laser_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);

// Constants are now in laser.h

// Estructuras de apoyo
struct LaserState {
    vec3_t last_trace_start;
    vec3_t last_trace_end;
    gtime_t last_trace_time = 0_ms;
    bool needs_retrace = true;
};

struct EmitterState {
    bool is_warning_phase = false;
    bool is_blink_on = false;
    gtime_t last_blink_time = 0_ms;
};

// --- PlayerLaserManager Implementation ---
PlayerLaserManager::PlayerLaserManager(edict_t* player)
  : owner(player) // Initialize owner. active_count(0) is handled by the in-class initializer.
                  // lasers is default-constructed.
{
    // Constructor body is empty, which is fine
}
bool PlayerLaserManager::can_add_laser() const {
    return active_count < LaserConstants::MAX_LASERS;
}

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
    // Iterate backwards to avoid issues if laser_die modifies the array during iteration
    for (int i = LaserConstants::MAX_LASERS - 1; i >= 0; --i) {
        auto& entry = lasers[i];
        if (entry.active) {
            edict_t* entity_to_die = nullptr;
            // Prioritize killing the emitter if it exists
            if (entry.emitter && entry.emitter->inuse) {
                entity_to_die = entry.emitter;
            }
            else if (entry.beam && entry.beam->inuse) {
                entity_to_die = entry.beam; // Fallback to beam
            }

            if (entity_to_die) {
                laser_die(entity_to_die, nullptr, owner, 9999, vec3_origin, MOD_UNKNOWN);
            }
            else {
                // If both are somehow null/not inuse but entry is active, clear it manually
                entry.active = false;
                entry.emitter = nullptr;
                entry.beam = nullptr;
                active_count--; // Decrement count here too
            }
            // laser_die should call remove_laser, which updates the entry and count,
            // but clearing here ensures consistency even if laser_die fails partially.
        }
    }
    // Double-check count after loop
    // int final_count = 0;
    // for(const auto& entry : lasers) { if(entry.active) final_count++; }
    // active_count = final_count;
}


int PlayerLaserManager::get_active_count() const {
    return active_count;
}

// --- LaserHelpers Namespace ---
namespace LaserHelpers {
    struct LaserHealth {
        bool healthy;
        uint32_t laser_color;
        uint32_t flare_color;
    };

    [[nodiscard]] static LaserHealth get_laser_health_state(const edict_t* laser) {
        if (!laser) return { false, 0xd0d1d2d3, 0x00FF00FF }; // Default unhealthy state if laser is null
        const bool healthy = laser->health > laser->max_health * 0.20f;
        return {
            healthy,
            healthy ? 0xf2f2f0f0 : 0xd0d1d2d3,  // Laser color
            healthy ? 0xFF0000FF : 0x00FF00FF   // Flare color (Red=Healthy, Green=Damaged)
        };
    }

    static bool is_valid_target(const edict_t* ent) {
        return ent && ent->inuse && (ent->svflags & SVF_MONSTER);
    }

    static bool is_same_team(const edict_t* ent1, const edict_t* ent2) {
        return ent1 && ent2 && ent1->team && ent2->team &&
            strcmp(ent1->team, ent2->team) == 0;
    }

    static float calculate_damage_multiplier(const edict_t* target) {
        if (!target) return 0.0f;
        if (target->client || target->monsterinfo.issummoned) return 0.0f;
        if (target->svflags & SVF_MONSTER) {
            if (target->monsterinfo.invincible_time > level.time) return 0.0f;
            return target->monsterinfo.IS_BOSS ? 1.25f : 1.0f;
        }
        return LaserConstants::LASER_NONCLIENT_MOD;
    }

    static void update_visual_state(edict_t* ent, bool warning_state, bool blink_state) {
        if (!ent) return;
        if (warning_state && blink_state) {
            ent->s.renderfx |= RF_SHELL_GREEN;
            ent->s.effects |= EF_COLOR_SHELL;
        }
        else {
            ent->s.renderfx &= ~RF_SHELL_GREEN;
            ent->s.effects &= ~EF_COLOR_SHELL;
        }
    }

    PlayerLaserManager* get_laser_manager(edict_t* ent) {
        if (!ent || !ent->client) return nullptr;
        // Use .get() to return the raw pointer managed by the unique_ptr
        return ent->client->laser_manager.get();
    }

    [[nodiscard]] static float get_angle_between_vectors(const vec3_t& v1, const vec3_t& v2) {
        if (!is_valid_vector(v1) || !is_valid_vector(v2)) return 0.0f;
        vec3_t const normalized_v1 = safe_normalized(v1);
        vec3_t const normalized_v2 = safe_normalized(v2);
        float const dot = normalized_v1.dot(normalized_v2);
        return acosf(std::clamp(dot, -1.0f, 1.0f)) * (180.0f / PIf);
    }

    [[nodiscard]] static bool is_vector_within_angle(const vec3_t& vec, const vec3_t& reference, float max_angle) {
        return get_angle_between_vectors(vec, reference) <= max_angle;
    }
} // End namespace LaserHelpers

// --- laser_pierce_t Struct ---
struct laser_pierce_t : pierce_args_t {
    edict_t* self;
    bool damaged_thing = false;

    inline laser_pierce_t(edict_t* self_ptr) : pierce_args_t(), self(self_ptr) {}

    virtual bool hit(contents_t& mask, vec3_t& end) override {
        if (!self || self->health <= 0) return false;
        if (!tr.ent) return mark(nullptr); // Continue trace if hit nothing or world

        if (tr.ent->client && OnSameTeam(self->teammaster, tr.ent)) return false; // Stop on teammates

        if (self->dmg > 0 && tr.ent->takedamage && tr.ent != self->teammaster) {
            damaged_thing = true;
            if ((tr.ent->svflags & SVF_MONSTER) && tr.ent->health <= 100) {
                tr.ent->gib_health = 10;
            }
            vec3_t forward;
            AngleVectors(self->s.angles, &forward, nullptr, nullptr);
            T_Damage(tr.ent, self, self->teammaster, forward, tr.endpos, vec3_origin,
                self->dmg, 0, DAMAGE_ENERGY, MOD_PLAYER_LASER);

            float const damageMult = LaserHelpers::calculate_damage_multiplier(tr.ent);
            if (damageMult >= 0.0f) {
                self->health -= static_cast<int>(self->dmg * damageMult);
            }

            if (self->health <= 0) {
                laser_die(self, self, self->teammaster, self->dmg, tr.endpos, MOD_PLAYER_LASER);
                return false; // Stop piercing, laser died
            }
        }

        // Stop piercing if it hits something solid that's not a monster or client
        if (!(tr.ent->svflags & SVF_MONSTER) && !tr.ent->client && (tr.ent->solid != SOLID_NOT && tr.ent->solid != SOLID_TRIGGER)) {
            return false;
        }


        // Continue piercing otherwise (monster, client, trigger, non-solid)
        return mark(tr.ent);
    }
};

// --- Funciones principales optimizadas ---

// laser_remove: Initiates the removal/explosion sequence for a laser component
void laser_remove(edict_t* self) {
    if (!self || !self->inuse) return;

    // Set up explosion animation/think for the component being removed
    self->think = BecomeExplosion1;
    self->nextthink = level.time + FRAME_TIME_MS;

    edict_t* emitter = nullptr;
    edict_t* beam = nullptr;
    edict_t* flare = nullptr;
    edict_t* teammaster = self->teammaster; // Cache teammaster

    // Identify components
    if (self->classname && strcmp(self->classname, "emitter") == 0) {
        emitter = self;
        beam = self->owner;
        // Flare is owned by emitter, but we need the beam's owner's owner if self is beam
        // Let's find flare based on emitter later
    }
    else if (self->classname && strcmp(self->classname, "laser") == 0) {
        beam = self;
        emitter = self->owner;
    }

    // Find the flare associated with the emitter (if emitter exists)
    if (emitter && emitter->inuse) {
        // Iterate to find the flare owned by this emitter
        for (uint32_t i = 1; i <= globals.num_edicts; i++) {
            edict_t* potential_flare = &g_edicts[i];
            if (potential_flare->inuse && potential_flare->classname &&
                strcmp(potential_flare->classname, "misc_flare") == 0 &&
                potential_flare->owner == emitter)
            {
                flare = potential_flare;
                break;
            }
        }
    }

    // Free the flare if found
    if (flare && flare->inuse) {
        G_FreeEdict(flare);
    }

    // Free the *other* component (beam if self is emitter, emitter if self is beam)
    if (emitter && emitter != self && emitter->inuse) {
        G_FreeEdict(emitter);
    }
    if (beam && beam != self && beam->inuse) {
        G_FreeEdict(beam);
    }

    // Update the player's laser manager
    if (teammaster && teammaster->inuse && teammaster->client) {
        if (auto* manager = LaserHelpers::get_laser_manager(teammaster)) {
            manager->remove_laser(self); // Remove the component that triggered removal
            if (manager) { // Check manager again after removal
                gi.LocClient_Print(teammaster, PRINT_HIGH, "Laser destroyed. {}/{} remaining.\n",
                    manager->get_active_count(), LaserConstants::MAX_LASERS);
            }
        }
    }
    // Note: G_FreeEdict(self) is implicitly handled by BecomeExplosion1 sequence
}

// laser_die: Handles the death/destruction of a laser component (emitter or beam)
DIE(laser_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void {
    if (!self || !self->inuse) return;

    edict_t* emitter = nullptr;
    edict_t* beam = nullptr;
    edict_t* flare = nullptr;
    edict_t* teammaster = self->teammaster; // Cache teammaster

    // Identify components based on classname
    if (self->classname && strcmp(self->classname, "emitter") == 0) {
        emitter = self;
        beam = self->owner;
    }
    else if (self->classname && strcmp(self->classname, "laser") == 0) {
        beam = self;
        emitter = self->owner;
    }
    else {
        // Unknown type, attempt basic cleanup
        if (self->owner && self->owner->inuse) G_FreeEdict(self->owner);
        G_FreeEdict(self);
        return;
    }

    // Update manager first (using the component that died)
    if (teammaster && teammaster->inuse && teammaster->client) {
        if (auto* manager = LaserHelpers::get_laser_manager(teammaster)) {
            manager->remove_laser(self);
            // Optional: Print message here if needed
        }
    }

    // Find and clean up flare associated with the emitter
    if (emitter && emitter->inuse) {
        for (uint32_t i = 1; i <= globals.num_edicts; i++) {
            edict_t* potential_flare = &g_edicts[i];
            if (potential_flare->inuse && potential_flare->classname &&
                strcmp(potential_flare->classname, "misc_flare") == 0 &&
                potential_flare->owner == emitter)
            {
                G_FreeEdict(potential_flare);
                break; // Assume only one flare per emitter
            }
        }
    }

    // Clean up the other component and start explosion on the emitter (if it exists)
    if (emitter && emitter->inuse) {
        if (beam && beam != emitter && beam->inuse) { // Free beam if it exists and isn't self
            G_FreeEdict(beam);
        }
        BecomeExplosion1(emitter); // Start explosion on the emitter
    }
    else if (beam && beam->inuse) {
        // If emitter is already gone, just free the beam
        G_FreeEdict(beam);
    }
    // If self was the emitter, BecomeExplosion1 handles its freeing.
    // If self was the beam and emitter was already gone, self was freed above.
}


// laser_beam_think: Per-frame logic for the laser beam entity
THINK(laser_beam_think)(edict_t* self) -> void {
    if (!self || !self->inuse || !self->owner || !self->owner->inuse) {
        if (self && self->inuse) G_FreeEdict(self);
        return;
    }

    const int size = (self->health < 1) ? 0 : (self->health >= 1000) ? 4 : 2;
    self->s.frame = size;

    const auto health_state = LaserHelpers::get_laser_health_state(self);
    self->s.skinnum = health_state.laser_color;

    // Find and update associated flare color (Flare is owned by emitter, emitter is self->owner)
    edict_t* emitter = self->owner;
    if (emitter && emitter->inuse) {
        for (uint32_t i = 1; i <= globals.num_edicts; i++) {
            edict_t* potential_flare = &g_edicts[i];
            if (potential_flare->inuse && potential_flare->classname &&
                strcmp(potential_flare->classname, "misc_flare") == 0 &&
                potential_flare->owner == emitter) // Check ownership against emitter
            {
                potential_flare->s.skinnum = health_state.flare_color;
                break;
            }
        }
    }


    vec3_t forward;
    AngleVectors(self->s.angles, &forward, nullptr, nullptr);
    const vec3_t start = self->pos1;
    const vec3_t end = start + forward * 8192;

    laser_pierce_t args(self);
    pierce_trace(start, end, self, args, MASK_SHOT);

    self->s.origin = args.tr.endpos;
    self->s.old_origin = self->pos1;

    if (self->health <= 0) {
        // laser_die was called inside hit()
        return;
    }

    self->nextthink = level.time + FRAME_TIME_MS;
    gi.linkentity(self);
}

// emitter_think: Per-frame logic for the laser emitter entity (grenade model)
THINK(emitter_think)(edict_t* self) -> void {
    if (!self || !self->inuse) return;

    static std::unordered_map<const edict_t*, EmitterState> emitter_states;
    auto it = emitter_states.find(self);
    if (it == emitter_states.end()) {
        // Initialize state if not found
        it = emitter_states.emplace(self, EmitterState{}).first;
    }
    auto& state = it->second;


    if (level.time >= self->timestamp) {
        if (self->teammaster && self->teammaster->client) {
            gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser timed out and was removed.\n");
        }
        laser_die(self, nullptr, self->teammaster, 9999, self->s.origin, MOD_UNKNOWN);
        emitter_states.erase(it); // Use iterator to erase
        return;
    }

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

    LaserHelpers::update_visual_state(self, state.is_warning_phase, state.is_blink_on);

    self->nextthink = level.time + FRAME_TIME_MS;
    gi.linkentity(self);
}

// create_laser: Handles the command/action to create a new laser
void create_laser(edict_t* ent) {
    // --- Initial Checks (Remain the same) ---
    if (!ent || !ent->client) return;

    if (!g_horde || !g_horde->integer) {
        gi.Client_Print(ent, PRINT_HIGH, "Need to be on Horde Mode to spawn a laser\n");
        return;
    }
    if (ent->movetype != MOVETYPE_WALK) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Need to be Non-Spect to create laser.\n");
        return;
    }

    // --- Lazy Initialization with unique_ptr ---
    // Initialize the manager directly if needed using make_unique
    if (!ent->client->laser_manager) {
        try {
            // Use std::make_unique for exception safety and conciseness
            ent->client->laser_manager = std::make_unique<PlayerLaserManager>(ent);
            if (developer && developer->integer > 1) {
                gi.Com_PrintFmt("Allocated PlayerLaserManager for client {}\n", (int)(ent - g_edicts));
            }
        }
        catch (const std::bad_alloc& e) {
            // Handle allocation failure gracefully
            gi.Com_PrintFmt("ERROR: Failed to allocate PlayerLaserManager: %s\n", e.what());
            // Ensure the unique_ptr is null after a failed allocation attempt
            ent->client->laser_manager = nullptr;
            return; // Cannot proceed without the manager
        }
        // Check again after make_unique in case it returned null (though unlikely with standard make_unique)
        if (!ent->client->laser_manager) {
            gi.Com_Print("Error: PlayerLaserManager allocation resulted in null pointer.\n");
            return;
        }
    }
    // --- End Lazy Initialization ---

    // Get the raw pointer from the unique_ptr for use within this function scope
    // .get() returns the managed pointer without transferring ownership.
    PlayerLaserManager* manager = ent->client->laser_manager.get();

    // --- Subsequent Checks (Use the raw pointer 'manager') ---
    // Check if the manager pointer is valid (it should be after the block above)
    // and if the player can add more lasers.
    if (!manager || !manager->can_add_laser()) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Can't build any more lasers.\n");
        return;
    }

    if (ent->client->pers.inventory[IT_AMMO_CELLS] < LaserConstants::LASER_COST) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Not enough cells to create a laser.\n");
        return;
    }

    // --- Trace and Geometry Checks (Remain the same) ---
    vec3_t forward, right;
    AngleVectors(ent->client->v_angle, &forward, &right, nullptr);
    vec3_t const offset{ 0.0f, 8.0f, static_cast<float>(ent->viewheight) - 8.0f };
    vec3_t const start = G_ProjectSource(ent->s.origin, offset, forward, right);
    vec3_t const end = start + forward * 64;
    trace_t const tr = gi.traceline(start, end, ent, MASK_SOLID);

    if (tr.fraction == 1.0f) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Too far from wall.\n");
        return;
    }

    // --- Spawn Laser Components (Remain the same) ---
    edict_t* laser = G_Spawn();
    edict_t* grenade = G_Spawn(); // Emitter
    edict_t* flare = G_Spawn();

    if (!laser || !grenade || !flare) {
        if (laser) G_FreeEdict(laser);
        if (grenade) G_FreeEdict(grenade);
        if (flare) G_FreeEdict(flare);
        gi.Com_Print("Error: Failed to spawn all laser components.\n");
        return;
    }

    // --- Configure Grenade (Emitter) FIRST (Remains the same) ---
    grenade->classname = "emitter";
    grenade->s.origin = tr.endpos;
    grenade->s.angles = vectoangles(tr.plane.normal);
    grenade->movetype = MOVETYPE_NONE;
    grenade->clipmask = MASK_SHOT;
    grenade->solid = SOLID_BBOX;
    grenade->mins = vec3_t{ -3, -3, 0 };
    grenade->maxs = vec3_t{ 3, 3, 6 };
    grenade->takedamage = true;
    grenade->health = 100;
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
    // Set team based on player
    if (ent->client->resp.ctf_team == CTF_TEAM1) grenade->team = TEAM1;
    else if (ent->client->resp.ctf_team == CTF_TEAM2) grenade->team = TEAM2;
    else grenade->team = "neutral";

    // --- Configure Laser (Beam) (Remains the same) ---
    laser->classname = "laser";
    laser->movetype = MOVETYPE_NONE;
    laser->solid = SOLID_NOT;
    laser->s.renderfx = RF_BEAM | RF_TRANSLUCENT;
    laser->s.modelindex = 1;
    laser->s.sound = gi.soundindex("world/laser.wav");
    laser->teammaster = ent;
    laser->owner = grenade; // Beam is owned by emitter
    laser->pos1 = tr.endpos;
    laser->s.angles = grenade->s.angles; // Match emitter angles
    laser->dmg = LaserConstants::LASER_INITIAL_DAMAGE +
        (LaserConstants::LASER_ADDON_DAMAGE * (current_wave_level - 1));
    laser->health = std::min(LaserConstants::LASER_INITIAL_HEALTH +
        (LaserConstants::LASER_ADDON_HEALTH * (current_wave_level - 1)),
        LaserConstants::MAX_LASER_HEALTH);
    laser->max_health = laser->health;
    laser->gib_health = -100;
    laser->mass = 50;
    laser->takedamage = false;
    laser->die = laser_die;
    laser->think = laser_beam_think;
    laser->nextthink = level.time + LaserConstants::LASER_SPAWN_DELAY;
    laser->flags |= FL_NO_KNOCKBACK;
    laser->team = grenade->team; // Match emitter's team
    // Set initial colors/size
    const auto initial_health_state = LaserHelpers::get_laser_health_state(laser);
    laser->s.skinnum = initial_health_state.laser_color;
    laser->s.frame = (laser->health < 1) ? 0 : (laser->health >= 1000) ? 4 : 2;


    // --- Configure Flare (Remains the same) ---
    flare->classname = "misc_flare";
    flare->s.origin = tr.endpos;
    flare->s.angles = { 90, 0, 0 };
    flare->owner = grenade; // Flare owned by emitter
    flare->spawnflags = 9_spawnflag;
    flare->s.skinnum = initial_health_state.flare_color;
    spawn_temp_t st{};
    st.radius = 0.5f;
    ED_CallSpawn(flare, st);

    // --- Link Entities (Remains the same) ---
    gi.linkentity(grenade); // Link emitter first
    gi.linkentity(laser);   // Then beam
    if (flare->inuse) gi.linkentity(flare); // Then flare if valid

    // --- Update Player State (Remains the same) ---
    ent->client->pers.inventory[IT_AMMO_CELLS] -= LaserConstants::LASER_COST;

    // --- Add to Manager (Use the raw pointer 'manager') ---
    manager->add_laser(grenade, laser);
    gi.LocClient_Print(ent, PRINT_HIGH, "Laser built. You have {}/{} lasers.\n",
        manager->get_active_count(), LaserConstants::MAX_LASERS);
}

// remove_lasers: Removes all lasers owned by a player
void remove_lasers(edict_t* ent) noexcept {
    if (!ent || !ent->client) return;
    if (auto* manager = LaserHelpers::get_laser_manager(ent)) {
        manager->remove_all_lasers();
    }
}

// --- END OF FILE g_laser.cpp ---