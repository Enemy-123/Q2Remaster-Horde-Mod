// g_laser.cpp

#include "g_local.h"
#include "shared.h"
#include <new>
#include <cmath>

// Private state for emitters, like blinking status.
// FIXED: Only one definition of EmitterState, with the clear() method.
struct EmitterState
{
    bool is_warning_phase = false;
    bool is_blink_on = false;
    gtime_t last_blink_time = 0_ms;

    // Add a clear() method for convenience
    void clear() {
        is_warning_phase = false;
        is_blink_on = false;
        last_blink_time = 0_ms;
    }
};

static EmitterState g_emitter_states[MAX_EDICTS];

static EmitterState* GetEmitterState(const edict_t* ent) {
    if (!ent || !ent->inuse) {
        return nullptr;
    }
    // The entity's number is its index into the global state array.
    return &g_emitter_states[ent->s.number];
}

// Forward declarations for internal functions
void laser_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t &point, const mod_t &mod);
void laser_beam_think(edict_t *self); // Forward declare the renamed think function

static int CalculateWaveBasedLaserDamage(int wave_level)
{
    int effective_wave_level = std::max(1, wave_level);
    return LaserConstants::LASER_INITIAL_DAMAGE + (LaserConstants::LASER_ADDON_DAMAGE * (effective_wave_level - 1));
}

static int CalculateWaveBasedLaserMaxHealth(int wave_level)
{
    int effective_wave_level = std::max(1, wave_level);
    return std::min(LaserConstants::LASER_INITIAL_HEALTH + (LaserConstants::LASER_ADDON_HEALTH * (effective_wave_level - 1)), LaserConstants::MAX_LASER_HEALTH);
}

void G_UpdateActiveLasersForWaveProgression(int current_wave_level_from_game)
{
    if (!g_horde || !g_horde->integer)
        return;
    if (developer && developer->integer)
        gi.Com_PrintFmt("Updating active lasers for wave: {}\n", current_wave_level_from_game);

    for (uint32_t i = 1; i <= globals.num_edicts; i++)
    {
        edict_t *ent = &g_edicts[i];
        if (!ent->inuse || !horde::IsSpecialType(ent, horde::SpecialEntityTypeID::LASER_EMITTER))
        {
            continue;
        }

        edict_t *emitter = ent;
        edict_t *laser_beam = emitter->chain; // The beam is stored in the emitter's chain

        if (!laser_beam || !laser_beam->inuse)
            continue;

        int new_damage = CalculateWaveBasedLaserDamage(current_wave_level_from_game);
        int new_max_health = CalculateWaveBasedLaserMaxHealth(current_wave_level_from_game);

        laser_beam->dmg = new_damage;
        if (new_max_health != laser_beam->max_health)
        {
            if (laser_beam->health > 0)
            {
                float health_ratio = (laser_beam->max_health > 0) ? (float)laser_beam->health / (float)laser_beam->max_health : 1.0f;
                laser_beam->health = std::max(1, static_cast<int>(health_ratio * new_max_health));
            }
            laser_beam->max_health = new_max_health;
        }
    }
}

// --- PlayerLaserManager and its implementation are now DELETED ---

// --- Helper Namespace ---
namespace LaserHelpers
{
    struct LaserHealth
    {
        bool healthy;
        uint32_t laser_color;
        uint32_t flare_color;
    };

    [[nodiscard]] static LaserHealth get_laser_health_state(const edict_t *laser)
    {
        if (!laser)
            return {false, 0xd0d1d2d3, 0x00FF00FF};
        const bool healthy = laser->health > laser->max_health * 0.25f;
        return {healthy, healthy ? 0xf2f2f0f0 : 0xd0d1d2d3, healthy ? 0xFF0000FF : 0x00FF00FF};
    }

    // get_laser_manager is now DELETED
}

// --- Pierce Logic (Unchanged) ---
// In g_laser.cpp

struct laser_pierce_t : pierce_args_t
{
    edict_t *self; // The beam entity
    inline laser_pierce_t(edict_t *self_ptr) : pierce_args_t(), self(self_ptr) {}

    virtual bool hit(contents_t &mask, vec3_t &end) override
    {
        if (!self || self->health <= 0 || !tr.ent)
            return false;

        // The beam's owner is the emitter. Ignore it.
        if (tr.ent == self->owner) {
            return true; 
        }

        if (tr.ent->client && OnSameTeam(self->teammaster, tr.ent))
            return false;

        // --- START OF NEW DAMAGE LOGIC ---
        if (self->dmg > 0 && tr.ent->takedamage && tr.ent != self->teammaster)
        {
            // 1. Check for target immunity BEFORE doing anything else.
            // This applies to both monsters and players with invulnerability.
            if (tr.ent->client && tr.ent->client->invincible_time > level.time) {
                return mark(tr.ent); // Pierce through invincible players without effect.
            }
            if (tr.ent->svflags & SVF_MONSTER && tr.ent->monsterinfo.invincible_time > level.time) {
                return mark(tr.ent); // Pierce through invincible monsters without effect.
            }

            // 2. The damage to be dealt is ALWAYS the beam's base damage.
            // We do not apply any modifiers from the player (teammaster).
            int damage_to_deal = self->dmg;

            // 3. Apply the damage directly.
            // We still use T_Damage to handle armor, pain functions, etc., but we pass our fixed damage value.
            vec3_t forward;
            AngleVectors(self->s.angles, &forward, nullptr, nullptr);
            T_Damage(tr.ent, self, self->teammaster, forward, tr.endpos, vec3_origin, damage_to_deal, 0, DAMAGE_ENERGY, MOD_PLAYER_LASER);

            // 4. Drain the laser's health based on the damage dealt.
            // This ensures that if the target was immune, no health is drained.
            float health_drain_multiplier = (tr.ent->svflags & SVF_MONSTER) ? 1.0f : LaserConstants::LASER_NONCLIENT_MOD;
            self->health -= static_cast<int>(damage_to_deal * health_drain_multiplier);

            // 5. Check if the laser is depleted.
            if (self->health <= 0)
            {
                laser_die(self, self, self->teammaster, damage_to_deal, tr.endpos, MOD_PLAYER_LASER);
                return false; // Stop piercing
            }
        }
        // --- END OF NEW DAMAGE LOGIC ---

        if (!(tr.ent->svflags & SVF_MONSTER) && !tr.ent->client && (tr.ent->solid != SOLID_NOT && tr.ent->solid != SOLID_TRIGGER))
        {
            return false; // Stop at solid world geometry
        }
        return mark(tr.ent); // Pierce through
    }
};

// --- Entity Functions ---
DIE(laser_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t &point, const mod_t &mod)->void
{
    if (!self || !self->inuse)
    {
        return;
    }

    // Step 1: Reliably identify the root emitter entity.
    edict_t *emitter = nullptr;
    if (horde::IsSpecialType(self, horde::SpecialEntityTypeID::LASER_EMITTER))
    {
        emitter = self;
    }
    else if (self->owner && horde::IsSpecialType(self->owner, horde::SpecialEntityTypeID::LASER_EMITTER))
    {
        emitter = self->owner;
    }

    if (!emitter || !emitter->inuse)
    {
        if (self->inuse)
            G_FreeEdict(self);
        return;
    }

    // Step 2: Update the player's tracking data. (This is already perfect)
    edict_t *teammaster = emitter->teammaster;
    if (teammaster && teammaster->inuse && teammaster->client)
    {
        if (teammaster->client->resp.num_lasers > 0)
        {
            teammaster->client->resp.num_lasers--;
        }
        for (int i = 0; i < LaserConstants::MAX_LASERS_PER_PLAYER; ++i)
        {
            if (teammaster->client->resp.deployed_lasers[i] == emitter)
            {
                teammaster->client->resp.deployed_lasers[i] = nullptr;
                break;
            }
        }
    }

    // --- REVISED AND EFFICIENT Step 3 ---
    // Clean up all associated resources and entities.
    EmitterState* state = GetEmitterState(emitter);
    if (state) {
        state->clear();
    }

    // Free known children directly using the stored pointers.
    // This is highly efficient and robust.
    if (emitter->chain && emitter->chain->inuse) {
        G_FreeEdict(emitter->chain); // Free the beam
    }
    if (emitter->goalentity && emitter->goalentity->inuse) {
        // We must check the classname to be 100% sure it's the flare,
        // in case goalentity was used for something else by another system.
        if (strcmp(emitter->goalentity->classname, "misc_flare") == 0) {
             G_FreeEdict(emitter->goalentity); // Free the flare
        }
    }
    // --- END REVISED Step 3 ---

    // Step 4: Finally, kill the emitter itself.
    emitter->health = 0;
    emitter->takedamage = false;
    BecomeExplosion1(emitter);
}

THINK(laser_beam_think)(edict_t * self)->void
{
    if (!self || !self->owner || !self->owner->inuse)
    {
        laser_die(self, self, nullptr, 0, vec3_origin, MOD_UNKNOWN);
        return;
    }

    edict_t *emitter = self->owner;
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

THINK(emitter_think)(edict_t * self)->void
{
    if (!self || !self->chain || !self->chain->inuse)
    {
        laser_die(self, self, self->teammaster, 0, self->s.origin, MOD_UNKNOWN);
        return;
    }

    edict_t *beam = self->chain;

    if (level.time >= self->timestamp)
    {
        if (self->teammaster && self->teammaster->client)
        {
            gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser timed out and was removed.\n");
        }
        laser_die(self, self, self->teammaster, 0, self->s.origin, MOD_UNKNOWN);
        return;
    }

    // Safely get the state using the helper
    EmitterState* state = GetEmitterState(self);
    if (!state) {
        // This should not happen if the entity is valid, but it's a good safety check.
        laser_die(self, self, self->teammaster, 0, self->s.origin, MOD_UNKNOWN);
        return;
    }

    bool const should_warn = level.time >= self->timestamp - LaserConstants::WARNING_TIME;

    // FIXED: Use -> for pointers
    if (should_warn != state->is_warning_phase)
    {
        state->is_warning_phase = should_warn;
        state->last_blink_time = 0_ms;
        state->is_blink_on = false;
    }

    if (state->is_warning_phase && level.time >= state->last_blink_time + LaserConstants::BLINK_INTERVAL)
    {
        state->is_blink_on = !state->is_blink_on;
        state->last_blink_time = level.time;
    }

    if (state->is_warning_phase && state->is_blink_on)
        self->s.renderfx |= RF_SHELL_GREEN;
    else
        self->s.renderfx &= ~RF_SHELL_GREEN;

    const auto health_state = LaserHelpers::get_laser_health_state(beam);
    beam->s.skinnum = (state->is_warning_phase && state->is_blink_on) ? 0xd0d1d2d3 : health_state.laser_color;
    beam->s.frame = (beam->health < 1) ? 0 : (beam->health >= 1000) ? 4 : 2;

    for (uint32_t i = 1; i <= globals.num_edicts; i++)
    {
        edict_t *flare = &g_edicts[i];
        if (flare->inuse && flare->owner == self && strcmp(flare->classname, "misc_flare") == 0)
        {
            flare->s.skinnum = (state->is_warning_phase && state->is_blink_on) ? 0x00FF00FF : health_state.flare_color;
            break;
        }
    }

    self->nextthink = level.time + FRAME_TIME_MS;
}

PAIN(laser_emitter_pain)(edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
    // This is the emitter. Its 'chain' is the beam.
    edict_t* beam = self->chain;

    // If the beam doesn't exist or is already dead, do nothing.
    if (!beam || !beam->inuse || beam->health <= 0) {
        return;
    }

    // Redirect the damage to the beam.
    beam->health -= damage;

    // If the beam's health drops to 0 or below, kill the entire laser assembly.
    if (beam->health <= 0) {
        // Use the beam's die function, which will correctly clean up the emitter.
        laser_die(beam, other, other, damage, self->s.origin, mod);
    }
}

void create_laser(edict_t * ent)
{
    if (!ent || !ent->client)
        return;
    if (!g_horde || !g_horde->integer)
    {
        gi.Client_Print(ent, PRINT_HIGH, "Need to be on Horde Mode to spawn a laser\n");
        return;
    }
    if (ent->movetype != MOVETYPE_WALK)
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Need to be Non-Spect to create laser.\n");
        return;
    }

    if (ent->client->resp.num_lasers >= LaserConstants::MAX_LASERS_PER_PLAYER)
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Can't build any more lasers.\n");
        return;
    }

    if (ent->client->pers.inventory[IT_AMMO_CELLS] < LaserConstants::LASER_COST)
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Not enough cells to create a laser.\n");
        return;
    }

    vec3_t forward, right;
    AngleVectors(ent->client->v_angle, &forward, &right, nullptr);
    vec3_t const offset{0.0f, 8.0f, static_cast<float>(ent->viewheight) - 8.0f};
    vec3_t const start = G_ProjectSource(ent->s.origin, offset, forward, right);
    vec3_t const end = start + forward * 64;
    trace_t const tr = gi.traceline(start, end, ent, MASK_SOLID);
    if (tr.fraction == 1.0f)
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Too far from wall.\n");
        return;
    }

    edict_t *emitter = G_Spawn();
    edict_t *beam = G_Spawn();
    edict_t *flare = G_Spawn();
    if (!emitter || !beam || !flare)
    {
        if (emitter) G_FreeEdict(emitter);
        if (beam) G_FreeEdict(beam);
        if (flare) G_FreeEdict(flare);
        gi.Com_Print("Error: Failed to spawn all laser components.\n");
        return;
    }

    emitter->classname = "emitter";
    emitter->special_type_id = static_cast<uint8_t>(horde::SpecialTypeRegistry::GetTypeID(emitter->classname));
    emitter->s.origin = tr.endpos;
    emitter->s.angles = vectoangles(tr.plane.normal);
    emitter->movetype = MOVETYPE_NONE;
    emitter->solid = SOLID_BBOX;
    emitter->mins = vec3_t{-3, -3, 0};
    emitter->maxs = vec3_t{3, 3, 6};
  // --- FIX: Make it targetable but resilient ---
    emitter->takedamage = true;
    emitter->health = 10000; // Give it a huge health pool so it can't be killed directly.
    emitter->pain = laser_emitter_pain; // Damage is redirected to the beam.
    // --- END FIX ---
    emitter->s.modelindex = gi.modelindex("models/objects/grenade2/tris.md2");
    emitter->teammaster = ent;
    emitter->chain = beam;
    emitter->goalentity = flare; //test
    emitter->think = emitter_think;
    emitter->nextthink = level.time + FRAME_TIME_MS;
    emitter->die = laser_die;
    emitter->timestamp = level.time + LaserConstants::LASER_TIMEOUT_DELAY;
    emitter->flags |= FL_NO_KNOCKBACK;
    if (ent->client->resp.ctf_team == CTF_TEAM1)
        emitter->team = TEAM1;
    else if (ent->client->resp.ctf_team == CTF_TEAM2)
        emitter->team = TEAM2;

    beam->classname = "laser";
    beam->special_type_id = static_cast<uint8_t>(horde::SpecialTypeRegistry::GetTypeID(beam->classname));
    beam->movetype = MOVETYPE_NONE;
    beam->solid = SOLID_NOT;
    beam->s.renderfx = RF_BEAM | RF_TRANSLUCENT;
    beam->s.modelindex = 1;
    beam->s.sound = gi.soundindex("world/laser.wav");
    beam->teammaster = ent;
    beam->owner = emitter;
    beam->s.angles = emitter->s.angles;
    beam->dmg = CalculateWaveBasedLaserDamage(current_wave_level);
    beam->max_health = CalculateWaveBasedLaserMaxHealth(current_wave_level);
    beam->health = beam->max_health;
    beam->die = laser_die;
    beam->think = laser_beam_think;
    beam->nextthink = level.time + LaserConstants::LASER_SPAWN_DELAY;
    beam->flags |= FL_NO_KNOCKBACK;
    beam->team = emitter->team;

    flare->classname = "misc_flare";
    flare->s.origin = tr.endpos;
    flare->owner = emitter;
    flare->spawnflags = 9_spawnflag;
    spawn_temp_t st{};
    st.radius = 0.5f;
    ED_CallSpawn(flare, st);

    EmitterState* state = GetEmitterState(emitter);
    if (state) {
        state->clear(); // Reset to default values
    }

    gi.linkentity(emitter);
    gi.linkentity(beam);
    if (flare->inuse)
        gi.linkentity(flare);

    ent->client->pers.inventory[IT_AMMO_CELLS] -= LaserConstants::LASER_COST;

    for (int i = 0; i < LaserConstants::MAX_LASERS_PER_PLAYER; ++i) {
        if (ent->client->resp.deployed_lasers[i] == nullptr) {
            ent->client->resp.deployed_lasers[i] = emitter;
            break;
        }
    }
    ent->client->resp.num_lasers++;

    gi.LocClient_Print(ent, PRINT_HIGH, "Laser built. You have {}/{} lasers.\n", ent->client->resp.num_lasers, LaserConstants::MAX_LASERS_PER_PLAYER);
}

void remove_lasers(edict_t* ent) noexcept {
    if (!ent || !ent->client) {
        return;
    }

    for (int i = LaserConstants::MAX_LASERS_PER_PLAYER - 1; i >= 0; --i) {
        edict_t* laser_emitter = ent->client->resp.deployed_lasers[i];
        
        if (laser_emitter && laser_emitter->inuse && horde::IsSpecialType(laser_emitter, horde::SpecialEntityTypeID::LASER_EMITTER)) {
            laser_die(laser_emitter, ent, ent, 9999, laser_emitter->s.origin, MOD_UNKNOWN);
        }
    }

    ent->client->resp.num_lasers = 0;
    for (int i = 0; i < LaserConstants::MAX_LASERS_PER_PLAYER; ++i) {
        ent->client->resp.deployed_lasers[i] = nullptr;
    }
}