// g_laser.cpp
//
// LASER SYSTEM ARCHITECTURE
// =========================
//
// Player lasers consist of three linked entities:
// 1. EMITTER (classname: "emitter")
//    - Visual component attached to wall
//    - Takes damage but redirects it to the beam
//    - Manages blinking warning state before timeout
//    - Has high health (10000) to prevent direct destruction
//
// 2. BEAM (classname: "laser")
//    - The actual damage-dealing ray
//    - Has real health that depletes when damaging enemies
//    - Performs pierce tracing every frame
//    - Owned by emitter (beam->owner = emitter)
//
// 3. FLARE (classname: "misc_flare")
//    - Visual effect at emitter position
//    - Color indicates health state and warnings
//
// ENTITY RELATIONSHIPS:
// - emitter->chain = beam
// - emitter->goalentity = flare
// - emitter->teammaster = player
// - beam->owner = emitter
// - beam->teammaster = player
//
// DAMAGE MECHANICS:
// - Laser damage scales with wave level
// - Laser health scales with wave level + adrenaline count
// - Damaging enemies consumes laser health
// - When laser health <= 0, entire assembly is destroyed
//
// PERFORMANCE CONSIDERATIONS:
// - Visual updates only occur when state changes (not every frame)
// - Wave-based stat recalculation only on wave change or adrenaline change
// - Pierce checks optimized with helper function
// - EmitterState tracks previous visual state to avoid redundant updates

#include "../g_local.h"
#include "../shared.h"
#include <new>
#include <cmath>

// Private state for emitters, like blinking status.
// Only one definition of EmitterState, with the clear() method.


EmitterState* GetEmitterState(const edict_t* ent) {
    if (!ent) return nullptr;
    auto it = g_emitter_states.find(ent->s.number);
    return (it != g_emitter_states.end()) ? &it->second : nullptr;
}

EmitterState* CreateEmitterState(edict_t* ent) {
    if (!ent) return nullptr;
    auto& state = g_emitter_states[ent->s.number];
    state.clear();
    return &state;
}

void RemoveEmitterState(const edict_t* ent) {
    if (!ent) return;
    g_emitter_states.erase(ent->s.number);
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


// Helper function to update laser damage and health for a single player
static void UpdatePlayerLasers(const edict_t* player, int current_wave_level, int current_adrenaline)
{
    if (!player || !player->client) return;

    for (int i = 0; i < LaserConstants::MAX_LASERS_PER_PLAYER(); ++i)
    {
        edict_t* emitter = player->client->resp.deployed_lasers[i];

        // Check if the emitter and its beam are valid
        if (!emitter || !emitter->inuse || !emitter->chain || !emitter->chain->inuse)
            continue;

        edict_t* laser_beam = emitter->chain;

        // Update damage based on wave level
        int new_damage = CalculateWaveBasedLaserDamage(current_wave_level);

        // Calculate max health: wave-based + adrenaline bonus (+250 per adrenaline)
        int wave_based_health = CalculateWaveBasedLaserMaxHealth(current_wave_level);
        int new_max_health = wave_based_health + (current_adrenaline * 250);
        new_max_health = std::min(new_max_health, LaserConstants::MAX_LASER_HEALTH);

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

// Helper function to update sentry gun health for a single player
static void UpdatePlayerSentryGuns(const edict_t* player)
{
    if (!player || !player->client) return;

    for (int i = 0; i < SentryConstants::MAX_SENTRIES_PER_PLAYER(); ++i)
    {
        edict_t* sentry = player->client->resp.deployed_sentries[i];

        if (!sentry || !sentry->inuse)
            continue;

        // Calculate new max health with current adrenaline count
        int base_health = 125; // Base sentry health from SP_monster_sentrygun
        int new_max_health = CalculateSentryHealth(base_health, player->client);

        // Only update max_health, leave current health untouched
        if (new_max_health != sentry->max_health)
        {
            sentry->max_health = new_max_health;

            // Update power armor accordingly (40% of max health)
            sentry->monsterinfo.power_armor_power = static_cast<int>(round(sentry->max_health * 0.4f));
        }
    }
}

// Helper function to update tesla mine lifetimes for a single player
static void UpdatePlayerTeslaMines(const edict_t* player)
{
    if (!player || !player->client) return;

    for (int i = 0; i < TeslaConstants::MAX_TESLAS_PER_PLAYER(); ++i)
    {
        edict_t* tesla = player->client->resp.deployed_teslas[i];

        if (!tesla || !tesla->inuse)
            continue;

        // Calculate new lifetime with current adrenaline count
        gtime_t tesla_lifetime = CalculateDeployableLifetime(TeslaConstants::TIME_TO_LIVE, player->client);
        gtime_t new_end_time = tesla->timestamp + tesla_lifetime;

        // Update the wait field which stores the end time in seconds
        tesla->wait = new_end_time.seconds();

        // Also update air_finished if it's being used for lifetime tracking
        if (tesla->air_finished > 0_sec)
        {
            tesla->air_finished = new_end_time;
        }
    }
}

void G_UpdateAdrenalineBasedDeployables(int current_wave_level)
{
    if (!g_horde || !g_horde->integer)
        return;

    // Cache tracking for performance optimization
    static int last_adrenaline_count[MAX_CLIENTS] = {-1}; // Initialize to -1 to force first update
    static int last_wave_level = -1; // Cache wave level to detect changes
    static int frame_counter = 0;

    // Periodic refresh every 30 frames (~0.5s) as failsafe
    bool force_refresh = (++frame_counter % 30 == 0);

    // Check if wave level changed - if so, all lasers need updates
    bool wave_changed = (current_wave_level != last_wave_level);
    if (wave_changed) {
        last_wave_level = current_wave_level;
    }

    for (const auto* player : active_players())
    {
        if (!player->client) continue;

        const int player_num = player - g_edicts - 1;
        if (player_num < 0 || player_num >= MAX_CLIENTS) continue;

        const int current_adrenaline = player->client->pers.adrenaline_count;

        // Only process if adrenaline changed or periodic refresh
        bool should_update_adrenaline = (current_adrenaline != last_adrenaline_count[player_num] || force_refresh);
        if (should_update_adrenaline) {
            last_adrenaline_count[player_num] = current_adrenaline;
        }

        // Update lasers only if wave changed or adrenaline changed
        if (wave_changed || should_update_adrenaline) {
            UpdatePlayerLasers(player, current_wave_level, current_adrenaline);
        }

        // Update other deployables only when adrenaline changes
        if (should_update_adrenaline) {
            UpdatePlayerSentryGuns(player);
            UpdatePlayerTeslaMines(player);

            // NOTE: Traps should NOT have their lifetime updated after creation
            // Their timestamp is set once when created with the adrenaline bonus at that time
            // Updating it here was causing the bug where trap lifetime kept increasing
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
            return {false, LaserConstants::COLOR_LASER_DAMAGED, LaserConstants::COLOR_FLARE_DAMAGED};
        const bool healthy = laser->health > laser->max_health * LaserConstants::HEALTH_THRESHOLD_DAMAGED;
        return {healthy,
                healthy ? LaserConstants::COLOR_LASER_HEALTHY : LaserConstants::COLOR_LASER_DAMAGED,
                healthy ? LaserConstants::COLOR_FLARE_HEALTHY : LaserConstants::COLOR_FLARE_DAMAGED};
    }

    // Helper to check if an entity should be pierced through by lasers
    [[nodiscard]] static bool ShouldPierceThrough(const edict_t* ent)
    {
        if (!ent) return false;

        // Pierce through players to prevent griefing
        if (ent->client)
            return true;

        // Pierce through friendly deployables
        if (horde::IsSpecialType(ent, horde::SpecialEntityTypeID::TESLA_MINE) ||
            horde::IsSpecialType(ent, horde::SpecialEntityTypeID::FOOD_CUBE_TRAP) ||
            horde::IsSpecialType(ent, horde::SpecialEntityTypeID::SENTRY_GUN) ||
            horde::IsSpecialType(ent, horde::SpecialEntityTypeID::TURRET) ||
            horde::IsSpecialType(ent, horde::SpecialEntityTypeID::PROX_MINE) ||
            horde::IsSpecialType(ent, horde::SpecialEntityTypeID::NUKE_MINE))
            return true;

        // Pierce through summoned monsters
        if ((ent->svflags & SVF_MONSTER) && ent->monsterinfo.isfriendlyspawn)
            return true;

        return false;
    }

    // get_laser_manager is now DELETED
}

struct player_laser_pierce_t : pierce_args_t
{
    edict_t* self; // The beam entity
    inline player_laser_pierce_t(edict_t* self_ptr) : pierce_args_t(), self(self_ptr) {}

    virtual bool hit(contents_t& mask, vec3_t& end) override
    {

        if (!self || self->health <= 0 || !tr.ent)
            return false;

        if (tr.ent == self->owner) {
            return true;
        }

        // Check if we should pierce through this entity
        if (LaserHelpers::ShouldPierceThrough(tr.ent)) {
            return mark(tr.ent); // Mark as pierced and continue through
        }

        // Only damage non-player entities
        if (self->dmg > 0 && tr.ent->takedamage)
        {
            if (tr.ent->svflags & SVF_MONSTER && tr.ent->monsterinfo.invincible_time > level.time) {
                return mark(tr.ent); // Pierce through invincible targets.
            }

            // Let g_no_self_damage system handle self-damage prevention

            int damage_to_deal = self->dmg;
            vec3_t forward;
            AngleVectors(self->s.angles, &forward, nullptr, nullptr);


            T_Damage(tr.ent, self, self->teammaster, forward, tr.endpos, vec3_origin, damage_to_deal, 0, DAMAGE_ENERGY, MOD_PLAYER_LASER);

            // Don't consume laser health when damaging barrels
            bool is_barrel = horde::IsSpecialType(tr.ent, horde::SpecialEntityTypeID::BARREL);
            if (!is_barrel) {
                float health_drain_multiplier = (tr.ent->svflags & SVF_MONSTER) ? (horde_fog_active ? 1.3f : 1.0f) : LaserConstants::LASER_NONCLIENT_MOD;
                self->health -= static_cast<int>(damage_to_deal * health_drain_multiplier);
            }

            if (self->health <= 0)
            {
                laser_die(self, self, self->teammaster, damage_to_deal, tr.endpos, MOD_PLAYER_LASER);
                return false;
            }
        }

        if (!(tr.ent->svflags & SVF_MONSTER) && !tr.ent->client && (tr.ent->solid != SOLID_NOT && tr.ent->solid != SOLID_TRIGGER))
        {
            return false;
        }

        return mark(tr.ent);
    }
};
// --- Entity Functions ---
DIE(laser_die)(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod)->void
{
    if (!self || !self->inuse)
    {
        return;
    }

    // Step 1: Reliably identify the root emitter entity.
    edict_t* emitter = nullptr;
    if (horde::IsSpecialType(self, horde::SpecialEntityTypeID::LASER_EMITTER))
    {
        emitter = self;
    }
    else if (self->owner && horde::IsSpecialType(self->owner, horde::SpecialEntityTypeID::LASER_EMITTER))
    {
        emitter = self->owner;
    }

    auto& vec = g_targetable_special_entities;
    vec.erase(std::remove(vec.begin(), vec.end(), emitter), vec.end());

    if (!emitter || !emitter->inuse)
    {
        // If we can't find the emitter but this entity is still valid, free it.
        if (self->inuse)
            G_FreeEdict(self);
        return;
    }

    // Step 2: Update the player's tracking data.
    edict_t* teammaster = emitter->teammaster;
    if (teammaster && teammaster->inuse && teammaster->client)
    {
        if (teammaster->client->resp.num_lasers > 0)
        {
            teammaster->client->resp.num_lasers--;
        }
        for (int i = 0; i < LaserConstants::MAX_LASERS_PER_PLAYER(); ++i)
        {
            if (teammaster->client->resp.deployed_lasers[i] == emitter)
            {
                teammaster->client->resp.deployed_lasers[i] = nullptr;
                break;
            }
        }
    }

    // Step 3: Clean up all associated resources and entities.
    RemoveEmitterState(emitter);

    // Free known children directly using the stored pointers.
    // CRITICAL FIX: Clear the beam's think function before freeing to prevent it from running
    if (emitter->chain && emitter->chain->inuse) {
        emitter->chain->think = nullptr;  // Stop the beam from thinking
        emitter->chain->nextthink = 0_ms; // Clear next think time
        G_FreeEdict(emitter->chain); // Free the beam
    }
    // Free the flare (goalentity is always the flare for lasers)
    // Note: Don't use strcmp on classname - it can be null if entity was partially freed
    if (emitter->goalentity && emitter->goalentity->inuse) {
        G_FreeEdict(emitter->goalentity);
    }

    // Step 4: Finally, kill the emitter itself.
    emitter->health = 0;
    emitter->takedamage = false;
    emitter->think = nullptr;  // Stop the emitter from thinking
    emitter->nextthink = 0_ms; // Clear next think time

    // Check flag to use quiet removal effect
    if (g_use_quiet_deployable_removal) {
        BecomeTE(emitter);
    } else {
        BecomeExplosion1(emitter);  // This already calls G_FreeEdict internally
    }
}

THINK(laser_beam_think)(edict_t * self)->void
{
    if (!self || !self->inuse)
    {
        return;  // Already freed or invalid
    }

    if (!self->owner || !self->owner->inuse)
    {
        // Owner (emitter) is gone, trigger proper cleanup through laser_die
        // This ensures player tracking is updated correctly
        laser_die(self, self, nullptr, 0, vec3_origin, MOD_UNKNOWN);
        return;
    }

    edict_t *emitter = self->owner;
    vec3_t forward;
    AngleVectors(emitter->s.angles, &forward, nullptr, nullptr);
    const vec3_t start = emitter->s.origin;
    const vec3_t end = start + (forward * 8192.0f);

    // Check if owner is menu protected - keep laser visible but skip damage
    if (emitter->teammaster && IsPlayerMenuProtected(emitter->teammaster)) {
        // Just trace for visual endpoint without dealing damage
        trace_t simple_trace = gi.traceline(start, end, emitter, MASK_SHOT);
        if (simple_trace.fraction == 1.0f) {
            self->s.origin = end;
        } else {
            self->s.origin = simple_trace.endpos;
        }
    } else {
        // Normal operation with damage
        player_laser_pierce_t args(self);
        pierce_trace(start, end, emitter, args, MASK_SHOT);

        // If the trace hit nothing or went the full distance, use the calculated end point
        // Otherwise use the trace endpoint where we hit something solid
        if (!args.tr.ent || args.tr.fraction == 1.0f) {
            self->s.origin = end;
        } else {
            self->s.origin = args.tr.endpos;
        }
    }

    self->s.old_origin = start;
    self->nextthink = level.time + FRAME_TIME_MS;
    gi.linkentity(self);
}

THINK(emitter_think)(edict_t * self)->void
{
    if (!self || !self->inuse)
    {
        return;  // Already freed or invalid
    }

    if (!self->chain || !self->chain->inuse)
    {
        // Beam is gone but emitter is still alive, clean up properly
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

    // Track if warning phase changed
    bool warning_changed = (should_warn != state->is_warning_phase);
    if (warning_changed)
    {
        state->is_warning_phase = should_warn;
        state->last_blink_time = 0_ms;
        state->is_blink_on = false;
    }

    // Track if blink state changed
    bool blink_changed = false;
    if (state->is_warning_phase && level.time >= state->last_blink_time + LaserConstants::BLINK_INTERVAL)
    {
        state->is_blink_on = !state->is_blink_on;
        state->last_blink_time = level.time;
        blink_changed = true;
    }

    // Update emitter renderfx only when warning/blink state changes
    if (warning_changed || blink_changed)
    {
        renderfx_t new_renderfx = self->s.renderfx;
        if (state->is_warning_phase && state->is_blink_on)
            new_renderfx = renderfx_t(new_renderfx | RF_SHELL_GREEN);
        else
            new_renderfx = renderfx_t(new_renderfx & ~RF_SHELL_GREEN);

        if (new_renderfx != renderfx_t(state->last_emitter_renderfx))
        {
            self->s.renderfx = new_renderfx;
            state->last_emitter_renderfx = int(new_renderfx);
        }
    }

    // Check beam and flare colors every frame (depends on health which can change independently)
    const auto health_state = LaserHelpers::get_laser_health_state(beam);
    uint32_t new_beam_skinnum = (state->is_warning_phase && state->is_blink_on) ? LaserConstants::COLOR_LASER_WARNING : health_state.laser_color;
    if (new_beam_skinnum != state->last_beam_skinnum)
    {
        beam->s.skinnum = new_beam_skinnum;
        state->last_beam_skinnum = new_beam_skinnum;
    }

    // Update flare color based on health and warning state
    edict_t* flare = self->goalentity;
    if (flare && flare->inuse && strcmp(flare->classname, "misc_flare") == 0)
    {
        uint32_t new_flare_skinnum = (state->is_warning_phase && state->is_blink_on) ? LaserConstants::COLOR_FLARE_WARNING : health_state.flare_color;
        if (new_flare_skinnum != state->last_flare_skinnum)
        {
            flare->s.skinnum = new_flare_skinnum;
            state->last_flare_skinnum = new_flare_skinnum;
        }
    }

    // Update beam frame based on health
    int new_beam_frame = (beam->health < 1) ? 0 : (beam->health >= 1000) ? 4 : 2;
    if (new_beam_frame != state->last_beam_frame)
    {
        beam->s.frame = new_beam_frame;
        state->last_beam_frame = new_beam_frame;
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

    // Check if player is menu protected
    if (IsPlayerMenuProtected(ent)) {
        gi.LocClient_Print(ent, PRINT_HIGH, "You cannot use this while in a menu.\n");
        return;
    }

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

    if (ent->client->resp.num_lasers >= LaserConstants::MAX_LASERS_PER_PLAYER())
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
    // Set laser lifetime with adrenaline bonus (+5 seconds per adrenaline)
    gtime_t base_lifetime = LaserConstants::LASER_TIMEOUT_DELAY;
    gtime_t adrenaline_bonus = gtime_t::from_sec(ent->client->pers.adrenaline_count * 5);
    emitter->timestamp = level.time + base_lifetime + adrenaline_bonus;
    emitter->flags |= FL_NO_KNOCKBACK;

    // CRITICAL FIX: Ensure proper team assignment for MinGW compatibility
    ctfteam_t owner_team = GetEntityTeam(ent);
    emitter->ctf_team = owner_team;
    beam->ctf_team = owner_team;

    // Additional safety: ensure beam teammaster is correctly set (redundant but explicit)
    beam->teammaster = ent;


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
    // Set laser max health with wave-based + adrenaline bonus (+250 per adrenaline)
    int wave_based_health = CalculateWaveBasedLaserMaxHealth(current_wave_level);
    int adrenaline_health_bonus = ent->client->pers.adrenaline_count * 250;
    beam->max_health = std::min(wave_based_health + adrenaline_health_bonus, LaserConstants::MAX_LASER_HEALTH);
    beam->health = beam->max_health;
    beam->die = laser_die;
    beam->think = laser_beam_think;
    beam->nextthink = level.time + LaserConstants::LASER_SPAWN_DELAY;
    beam->timestamp = emitter->timestamp; // Match emitter's lifetime
    beam->flags |= FL_NO_KNOCKBACK;
    beam->team = emitter->team;

    // ADDITIONAL MINGW FIX: Ensure all team-related fields are consistent
    beam->ctf_team = owner_team; // Redundant but ensures consistency

    flare->classname = "misc_flare";
    flare->s.origin = tr.endpos;
    flare->spawnflags = 9_spawnflag;
    spawn_temp_t st{};
    st.radius = 0.5f;
    ED_CallSpawn(flare, st);
    // FIX: Set owner AFTER ED_CallSpawn to prevent it from being reset
    flare->owner = emitter;

    CreateEmitterState(emitter);

    gi.linkentity(emitter);
    gi.linkentity(beam);
    if (flare->inuse)
        gi.linkentity(flare);

    g_targetable_special_entities.push_back(emitter);

    ent->client->pers.inventory[IT_AMMO_CELLS] -= LaserConstants::LASER_COST;

    for (int i = 0; i < LaserConstants::MAX_LASERS_PER_PLAYER(); ++i) {
        if (ent->client->resp.deployed_lasers[i] == nullptr) {
            ent->client->resp.deployed_lasers[i] = emitter;
            break;
        }
    }
    ent->client->resp.num_lasers++;

    gi.LocClient_Print(ent, PRINT_HIGH, "Laser built. You have {}/{} lasers.\n", ent->client->resp.num_lasers, LaserConstants::MAX_LASERS_PER_PLAYER());
}

void remove_lasers(edict_t* ent) noexcept {
    if (!ent || !ent->client) {
        return;
    }

    for (int i = LaserConstants::MAX_LASERS_PER_PLAYER() - 1; i >= 0; --i) {
        edict_t* laser_emitter = ent->client->resp.deployed_lasers[i];
        
        if (laser_emitter && laser_emitter->inuse && horde::IsSpecialType(laser_emitter, horde::SpecialEntityTypeID::LASER_EMITTER)) {
            laser_die(laser_emitter, ent, ent, 9999, laser_emitter->s.origin, MOD_UNKNOWN);
        }
    }

    ent->client->resp.num_lasers = 0;
    for (int i = 0; i < LaserConstants::MAX_LASERS_PER_PLAYER(); ++i) {
        ent->client->resp.deployed_lasers[i] = nullptr;
    }
}