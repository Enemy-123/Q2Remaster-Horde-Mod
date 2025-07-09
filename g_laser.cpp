// --- START OF FILE g_laser.cpp ---
#include "g_local.h"
#include "shared.h"
#include "g_laser.h"
#include <array>
#include <unordered_map>
#include <new>
#include <cmath>

// State for the blinking effect on the emitter
struct EmitterState {
    bool is_warning_phase = false;
    bool is_blink_on = false;
    gtime_t last_blink_time = 0_ms;
};
static std::unordered_map<const edict_t*, EmitterState> g_emitter_states;

// Forward declarations for functions within this file
void laser_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
void laser_beam_think(edict_t* self);
void emitter_think(edict_t* self);

// Helper function to calculate laser damage based on wave level
static int CalculateWaveBasedLaserDamage(int wave_level) {
    int effective_wave_level = std::max(1, wave_level);
    return LaserConstants::LASER_INITIAL_DAMAGE + (LaserConstants::LASER_ADDON_DAMAGE * (effective_wave_level - 1));
}

// Helper function to calculate laser max health based on wave level
static int CalculateWaveBasedLaserMaxHealth(int wave_level) {
    int effective_wave_level = std::max(1, wave_level);
    return std::min(LaserConstants::LASER_INITIAL_HEALTH + (LaserConstants::LASER_ADDON_HEALTH * (effective_wave_level - 1)), LaserConstants::MAX_LASER_HEALTH);
}

// Global function to update all active lasers based on the current wave level
void G_UpdateActiveLasersForWaveProgression(int current_wave_level_from_game) {
    if (!g_horde || !g_horde->integer) return;

    if (developer && developer->integer) {
        gi.Com_PrintFmt("Updating active lasers for wave: {}\n", current_wave_level_from_game);
    }

    for (uint32_t i = 1; i <= globals.num_edicts; i++) {
        edict_t* ent = &g_edicts[i];
        if (!ent->inuse) continue;

        // Use the fast, safe ID check instead of strcmp
        if (horde::IsSpecialType(ent, horde::SpecialEntityTypeID::LASER_BEAM)) {
            edict_t* laser_beam = ent;
            int new_damage = CalculateWaveBasedLaserDamage(current_wave_level_from_game);
            int new_max_health = CalculateWaveBasedLaserMaxHealth(current_wave_level_from_game);

            laser_beam->dmg = new_damage;

            if (new_max_health != laser_beam->max_health) {
                if (laser_beam->health > 0) {
                    float health_ratio = (laser_beam->max_health > 0) ? ((float)laser_beam->health / (float)laser_beam->max_health) : 1.0f;
                    laser_beam->health = std::max(1, static_cast<int>(health_ratio * new_max_health));
                }
                laser_beam->max_health = new_max_health;
            }
        }
    }
}

// --- PlayerLaserManager Implementation ---

PlayerLaserManager::PlayerLaserManager(edict_t* player) : owner(player) {}

// Destructor implementation: This is the key to automatic cleanup.
PlayerLaserManager::~PlayerLaserManager() {
    remove_all_lasers();
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
    for (int i = LaserConstants::MAX_LASERS - 1; i >= 0; --i) {
        auto& entry = lasers[i];
        if (entry.active) {
            edict_t* entity_to_die = (entry.emitter && entry.emitter->inuse) ? entry.emitter :
                                     (entry.beam && entry.beam->inuse) ? entry.beam : nullptr;
            if (entity_to_die) {
                // laser_die will handle calling remove_laser and decrementing the count
                laser_die(entity_to_die, nullptr, owner, 9999, vec3_origin, MOD_UNKNOWN);
            } else {
                // Manually clear the entry if entities are already gone
                entry.active = false;
                entry.emitter = nullptr;
                entry.beam = nullptr;
                active_count--;
            }
        }
    }
}

int PlayerLaserManager::get_active_count() const {
    return active_count;
}

// --- LaserHelpers Namespace ---
namespace LaserHelpers {
    PlayerLaserManager* get_laser_manager(edict_t* ent) {
        if (!ent || !ent->client) return nullptr;
        return ent->client->laser_manager.get();
    }
}

// --- laser_pierce_t Struct ---
struct laser_pierce_t : pierce_args_t {
    edict_t* self; // The laser beam entity
    inline laser_pierce_t(edict_t* self_ptr) : pierce_args_t(), self(self_ptr) {}

    virtual bool hit(contents_t& mask, vec3_t& end) override {
        if (!self || !self->owner || !self->owner->inuse) return false; // Emitter is gone
        if (!tr.ent) return mark(nullptr);

        edict_t* emitter = self->owner;
        edict_t* player = emitter->owner;

        if (tr.ent->client && OnSameTeam(player, tr.ent)) return false;

        if (self->dmg > 0 && tr.ent->takedamage && tr.ent != player) {
            vec3_t forward;
            AngleVectors(emitter->s.angles, &forward, nullptr, nullptr);
            T_Damage(tr.ent, emitter, player, forward, tr.endpos, vec3_origin, self->dmg, 0, DAMAGE_ENERGY, MOD_PLAYER_LASER);
        }

        if (!(tr.ent->svflags & SVF_MONSTER) && !tr.ent->client && (tr.ent->solid != SOLID_NOT && tr.ent->solid != SOLID_TRIGGER)) {
            return false; // Stop on solid world geometry
        }
        return mark(tr.ent); // Pierce through monsters and players
    }
};

// --- Core Laser Functions ---

DIE(laser_die)(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void {
    if (!self || !self->inuse) return;

    edict_t* emitter = nullptr;
    edict_t* beam = nullptr;

    if (horde::IsSpecialType(self, horde::SpecialEntityTypeID::LASER_EMITTER)) {
        emitter = self;
        beam = self->target_ent;
    } else if (horde::IsSpecialType(self, horde::SpecialEntityTypeID::LASER_BEAM)) {
        beam = self;
        emitter = self->owner;
    } else {
        G_FreeEdict(self);
        return;
    }

    edict_t* player = (emitter && emitter->inuse) ? emitter->owner : nullptr;
    if (player && player->client && player->client->laser_manager) {
        player->client->laser_manager->remove_laser(emitter ? emitter : beam);
    }

    if (emitter && emitter->inuse) {
        g_emitter_states.erase(emitter);
        // Find and free the flare
        for (uint32_t i = 1; i <= globals.num_edicts; i++) {
            edict_t* flare = &g_edicts[i];
            if (flare->inuse && flare->owner == emitter && strcmp(flare->classname, "misc_flare") == 0) {
                G_FreeEdict(flare);
                break;
            }
        }
    }

    if (beam && beam->inuse) G_FreeEdict(beam);
    if (emitter && emitter->inuse) BecomeExplosion1(emitter);
}

THINK(laser_beam_think)(edict_t* self) -> void {
    if (!self || !self->owner || !self->owner->inuse) {
        if (self && self->inuse) G_FreeEdict(self);
        return;
    }

    edict_t* emitter = self->owner;
    edict_t* player = emitter->owner;
    if (!player || !player->inuse) {
        G_FreeEdict(self);
        return;
    }

    self->dmg = CalculateWaveBasedLaserDamage(current_wave_level);
    self->max_health = CalculateWaveBasedLaserMaxHealth(current_wave_level);

    vec3_t forward;
    AngleVectors(emitter->s.angles, &forward, nullptr, nullptr);
    const vec3_t start = emitter->s.origin;
    const vec3_t end = start + forward * 8192;

    laser_pierce_t args(self);
    pierce_trace(start, end, emitter, args, MASK_SHOT);

    self->s.origin = args.tr.endpos;
    self->s.old_origin = start;
    self->nextthink = level.time + FRAME_TIME_MS;
    gi.linkentity(self);
}

THINK(emitter_think)(edict_t* self) -> void {
    if (!self || !self->owner || !self->owner->inuse) {
        if (self && self->inuse) G_FreeEdict(self);
        return;
    }

    if (level.time >= self->timestamp) {
        G_FreeEdict(self);
        return;
    }

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
    if (state.is_warning_phase && state.is_blink_on) {
        self->s.renderfx |= RF_SHELL_GREEN;
        self->s.effects |= EF_COLOR_SHELL;
    } else {
        self->s.renderfx &= ~RF_SHELL_GREEN;
        self->s.effects &= ~EF_COLOR_SHELL;
    }

    self->nextthink = level.time + FRAME_TIME_MS;
}

void create_laser(edict_t* ent) {
    if (!ent || !ent->client) return;
    if (!g_horde || !g_horde->integer) {
        gi.Client_Print(ent, PRINT_HIGH, "Need to be on Horde Mode to spawn a laser\n");
        return;
    }
    if (ent->movetype != MOVETYPE_WALK) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Need to be Non-Spect to create laser.\n");
        return;
    }
    if (!ent->client->laser_manager) {
        ent->client->laser_manager = std::make_unique<PlayerLaserManager>(ent);
    }
    if (!ent->client->laser_manager->can_add_laser()) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Can't build any more lasers.\n");
        return;
    }
    if (ent->client->pers.inventory[IT_AMMO_CELLS] < LaserConstants::LASER_COST) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Not enough cells to create a laser.\n");
        return;
    }

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

    edict_t* emitter = G_Spawn();
    edict_t* beam = G_Spawn();
    edict_t* flare = G_Spawn();

    if (!emitter || !beam || !flare) {
        if (emitter) G_FreeEdict(emitter);
        if (beam) G_FreeEdict(beam);
        if (flare) G_FreeEdict(flare);
        return;
    }

    // Configure Emitter
    emitter->classname = "emitter";
    emitter->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::LASER_EMITTER);
    emitter->s.origin = tr.endpos;
    emitter->s.angles = vectoangles(tr.plane.normal);
    emitter->movetype = MOVETYPE_NONE;
    emitter->solid = SOLID_BBOX;
    emitter->mins = vec3_t{ -3, -3, 0 };
    emitter->maxs = vec3_t{ 3, 3, 6 };
    emitter->takedamage = true;
    emitter->health = 100;
    emitter->s.modelindex = gi.modelindex("models/objects/grenade2/tris.md2");
    emitter->owner = ent;
    emitter->think = emitter_think;
    emitter->nextthink = level.time + FRAME_TIME_MS;
    emitter->die = laser_die;
    emitter->timestamp = level.time + LaserConstants::LASER_TIMEOUT_DELAY;
    emitter->target_ent = beam; // Emitter points to its beam
    gi.linkentity(emitter);

    // Configure Beam
    beam->classname = "laser_beam";
    beam->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::LASER_BEAM);
    beam->owner = emitter;
    beam->movetype = MOVETYPE_NONE;
    beam->solid = SOLID_NOT;
    beam->s.renderfx = RF_BEAM | RF_TRANSLUCENT;
    beam->s.modelindex = 1;
    beam->s.sound = gi.soundindex("world/laser.wav");
    beam->think = laser_beam_think;
    beam->nextthink = level.time + LaserConstants::LASER_SPAWN_DELAY;
    beam->takedamage = false;
    beam->s.origin = emitter->s.origin;
    beam->s.angles = emitter->s.angles;

    // The values will be updated per-frame in laser_beam_think, but they need a valid starting value.
    beam->max_health = CalculateWaveBasedLaserMaxHealth(current_wave_level);
    beam->health = beam->max_health;
    beam->dmg = CalculateWaveBasedLaserDamage(current_wave_level);

// Also restore the die function for robustness.
beam->die = laser_die;

    gi.linkentity(beam);

    // Configure Flare
    flare->classname = "misc_flare";
    flare->s.origin = tr.endpos;
    flare->s.angles = { 90, 0, 0 };
    flare->owner = emitter;
    flare->spawnflags = 9_spawnflag;
    spawn_temp_t st{};
    st.radius = 0.5f;
    ED_CallSpawn(flare, st);
    gi.linkentity(flare);

    // Finalize
    ent->client->laser_manager->add_laser(emitter, beam);
    ent->client->pers.inventory[IT_AMMO_CELLS] -= LaserConstants::LASER_COST;
    gi.LocClient_Print(ent, PRINT_HIGH, "Laser built. You have {}/{} lasers.\n", ent->client->laser_manager->get_active_count(), LaserConstants::MAX_LASERS);
}

void remove_lasers(edict_t* ent) noexcept {
    if (!ent || !ent->client || !ent->client->laser_manager) return;
    ent->client->laser_manager->remove_all_lasers();
}
// --- END OF FILE g_laser.cpp ---