
#include "../shared.h"

// Forward declaration
void T_RadiusDamage_TeamSafe(edict_t* inflictor, edict_t* attacker, float damage,
                             edict_t* ignore, float radius, damageflags_t dflags, mod_t mod);

// Constants for trap positioning and behavior
constexpr float TRAP_WALL_OFFSET = 3.0f;       // Offset for walls
constexpr float TRAP_CEILING_OFFSET = -20.4f;  // Offset for ceilings
constexpr float TRAP_FLOOR_OFFSET = -12.0f;    // Offset for floor
constexpr float TRAP_ORB_OFFSET = 12.0f;       // Normal sphere height
constexpr float TRAP_ORB_OFFSET_CEIL = -18.0f; // Ceiling sphere height
constexpr float TRAP_RADIUS = 400.0f;          // Trap pull radius
constexpr float TRAP_RADIUS_SQUARED = TRAP_RADIUS * TRAP_RADIUS; // Pre-computed squared radius
constexpr float TRAP_CONSUME_DISTANCE = 48.0f; // Distance at which trap consumes target
constexpr float TRAP_MAX_TARGET_MASS = 400.0f; // Maximum mass that can be consumed
constexpr int TRAP_WAIT_START = 64;            // Initial wait value for consuming
constexpr int TRAP_FRAME_CONSUME = 5;          // Frame for consumption animation
constexpr int TRAP_FRAME_ACTIVE = 4;           // Frame for active trap
constexpr gtime_t TRAP_DURATION = 80_sec;      // Total trap lifetime in seconds
constexpr gtime_t TRAP_COOLDOWN_DURATION = 10_sec; // Cooldown after consuming
constexpr gtime_t TRAP_GIB_LIFETIME = 3_sec;   // Maximum lifetime for gibs rotating around trap

// Physics constants
constexpr float TRAP_GIB_ROTATION_SPEED = 150.0f;  // Degrees per second for gib rotation
constexpr float TRAP_GIB_PULL_SPEED = 15.0f;       // Speed at which gibs are pulled toward center
constexpr float TRAP_PULL_SPEED_MIN = 64.0f;       // Minimum pull speed for targets
constexpr float TRAP_PULL_SPEED_MONSTER = 210.0f;  // Maximum pull speed for monsters
constexpr float TRAP_PULL_SPEED_PLAYER = 290.0f;   // Maximum pull speed for players
constexpr int TRAP_WAIT_ANIMATION_END = 19;        // Wait value when animation advances to next frame

// Bounce physics constants
constexpr float TRAP_BOUNCE_MIN_VELOCITY = 120.0f; // Minimum velocity component after bounce
constexpr float TRAP_BOUNCE_RANDOM = 70.0f;        // Random velocity variation on bounce
constexpr float TRAP_BOUNCE_UPWARD = 180.0f;       // Extra upward velocity on ground bounce

// Helper to get a pointer to a trap's state.
trap_state_t* GetTrapState(const edict_t* ent) {
    if (!ent) return nullptr;
    auto it = g_trap_states.find(ent->s.number);
    return (it != g_trap_states.end()) ? &it->second : nullptr;
}

trap_state_t* CreateTrapState(edict_t* ent) {
    if (!ent) return nullptr;
    // This creates a new entry if it doesn't exist and returns a reference to it.
    auto& state = g_trap_states[ent->s.number];
    state.clear(); // Ensure it's in a clean, default state
    return &state;
}

void RemoveTrapState(const edict_t* ent) {
    if (!ent) return;
    g_trap_states.erase(ent->s.number);
}

// Throw sparks at a specific target (or self->enemy if target is nullptr)
void trap_throwsparks(edict_t* self, edict_t* target = nullptr)
{
    if (!self || !self->inuse)
        return;

    // Use self->enemy if no target specified
    if (!target)
        target = self->enemy;

    if (!target || !target->inuse)
        return;

    // Calculate spark origin and direction
    vec3_t forward, right, up;
    AngleVectors(self->s.angles, forward, right, up);

    // Offset the sparks from the trap's origin
    const vec3_t spark_origin = self->s.origin + (up * 12.0f);

    // Direction will be towards the enemy being pulled
    vec3_t dir = (target->s.origin - spark_origin).normalized();

    // Create the spark effect
    gi.WriteByte(svc_temp_entity);
    gi.WriteByte(TE_SPLASH);
    gi.WriteByte(16);  // number of sparks
    gi.WritePosition(spark_origin);
    gi.WriteDir(dir);
    gi.WriteByte(SPLASH_SLIME);
    gi.multicast(spark_origin, MULTICAST_PVS, false);
}

THINK(Trap_Gib_Think) (edict_t* ent) -> void
{
    // Verify entity is valid
    if (!ent)
        return;

    // Verify owner is valid
    if (!ent->owner || !ent->owner->inuse)
    {
        G_FreeEdict(ent);
        return;
    }

    // Free the gib if the trap is no longer consuming (frames 5-7 are consuming states)
    if (ent->owner->s.frame < TRAP_FRAME_CONSUME || ent->owner->s.frame > 7)
    {
        G_FreeEdict(ent);
        return;
    }

    // CRITICAL FIX: Enforce maximum gib lifetime to prevent indefinite rotation
    if (ent->timestamp > 0_ms && level.time >= ent->timestamp)
    {
        G_FreeEdict(ent);
        return;
    }

    vec3_t forward, right, up;
    vec3_t vec;

    AngleVectors(ent->owner->s.angles, forward, right, up);

    // Rotate us around the center
    float degrees = (TRAP_GIB_ROTATION_SPEED * gi.frame_time_s) + ent->owner->delay;
    vec3_t diff = ent->owner->s.origin - ent->s.origin;
    vec = RotatePointAroundVector(up, diff, degrees);
    ent->s.angles[1] += degrees;
    vec3_t new_origin = ent->owner->s.origin - vec;

    trace_t tr = gi.traceline(ent->s.origin, new_origin, ent, MASK_SOLID);
    ent->s.origin = tr.endpos;

    // Pull us towards the trap's center
    diff.normalize();
    ent->s.origin += diff * (TRAP_GIB_PULL_SPEED * gi.frame_time_s);

    ent->watertype = gi.pointcontents(ent->s.origin);
    if (ent->watertype & MASK_WATER)
        ent->waterlevel = WATER_FEET;

    ent->nextthink = level.time + FRAME_TIME_S;
    gi.linkentity(ent);
}

DIE(trap_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
    auto& vec = g_targetable_special_entities;
    vec.erase(std::remove(vec.begin(), vec.end(), self), vec.end());

    // --- CLEAN UP ANY ROTATING GIBS ---
    // PERFORMANCE IMPROVEMENT: Use vector tracking instead of O(n) search
    trap_state_t* trap_state = GetTrapState(self);
    if (trap_state) {
        for (edict_t* gib : trap_state->owned_gibs) {
            if (gib && gib->inuse) {
                G_FreeEdict(gib);
            }
        }
        trap_state->owned_gibs.clear();
    }

    // --- UPDATE PLAYER TRACKING ---
    if (self->owner && self->owner->client) {
        gclient_t* client = self->owner->client;
        if (client->resp.num_traps > 0) {
            client->resp.num_traps--;
        }

        // Find and null out this trap in the owner's tracking array
        for (int i = 0; i < TrapConstants::MAX_TRAPS_PER_PLAYER; ++i) {
            if (client->resp.deployed_traps[i] == self) {
                client->resp.deployed_traps[i] = nullptr;
                break; // Found and removed, no need to search further.
            }
        }
    }

    // --- CLEAN UP GLOBAL STATE ---
    RemoveTrapState(self);

    // Check flag to use quiet removal effect
    if (g_use_quiet_deployable_removal) {
        BecomeTE(self);
    } else {
        BecomeExplosion1(self);
    }
}

void Trap_Think(edict_t* ent);
void SP_item_foodcube(edict_t* best);
void SpawnDamage(int type, const vec3_t& origin, const vec3_t& normal, int damage);

// New touch function for trap sticking behavior
// New touch function for trap sticking behavior
TOUCH(trap_stick)(edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
    if (!other || !other->inuse || other == ent->owner)
        return;

    // Only stick to BSP entities (world geometry) or PUSH movers (doors, platforms)
    // Don't stick to monsters, players, or other entities
    bool can_stick = (other->solid == SOLID_BSP) ||
                     (other->movetype == MOVETYPE_PUSH && !(other->svflags & SVF_MONSTER) && !other->client);

    if (!can_stick)
        return;

    // Handle non-world entities (bounce off them instead of sticking)
    if (other != world && (other->svflags & SVF_MONSTER || other->client ||
        (other->movetype != MOVETYPE_PUSH && other->solid == SOLID_BBOX)))
    {
        if (tr.plane.normal) {
            vec3_t out{};
            float const backoff = ent->velocity.dot(tr.plane.normal) * 1.35f;
            for (int i = 0; i < 3; i++) {
                float change = tr.plane.normal[i] * backoff;
                out[i] = ent->velocity[i] - change;
                out[i] += crandom() * TRAP_BOUNCE_RANDOM;
                if (fabs(out[i]) < TRAP_BOUNCE_MIN_VELOCITY && out[i] != 0) {
                    out[i] = (out[i] < 0 ? -TRAP_BOUNCE_MIN_VELOCITY : TRAP_BOUNCE_MIN_VELOCITY);
                }
            }
            if (tr.plane.normal[2] > 0) {
                out[2] += TRAP_BOUNCE_UPWARD;
            }
            if (out.length() < TRAP_BOUNCE_MIN_VELOCITY) {
                out.normalize();
                out = out * TRAP_BOUNCE_MIN_VELOCITY;
            }
            ent->velocity = out;
            ent->avelocity = { crandom() * 240, crandom() * 240, crandom() * 240 };
            gi.sound(ent, CHAN_VOICE, gi.soundindex(frandom() > 0.5f ?
                "weapons/hgrenb1a.wav" : "weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
            return;
        }
    }

    // Surface sticking logic
    if (tr.plane.normal) {
        const float slope = fabs(tr.plane.normal[2]);
        if (slope > 0.85f) {
            if (tr.plane.normal[2] > 0) {
                // Floor
                ent->s.angles = {};
                ent->mins = { -4, -4, 0 };
                ent->maxs = { 4, 4, 8 };
                ent->s.origin = ent->s.origin + (tr.plane.normal * TRAP_FLOOR_OFFSET);
                ent->s.origin[2] += TRAP_ORB_OFFSET;
            }
            else {
                // Ceiling
                ent->s.angles = { 180, 0, 0 };
                ent->mins = { -4, -4, -8 };
                ent->maxs = { 4, 4, 0 };
                ent->s.origin = ent->s.origin + (tr.plane.normal * TRAP_CEILING_OFFSET);
                ent->s.origin[2] += TRAP_ORB_OFFSET_CEIL;
            }
        }
        else {
            vec3_t dir = vectoangles(tr.plane.normal);
            vec3_t forward;
            AngleVectors(dir, &forward, nullptr, nullptr);

            const bool is_flat_wall = (fabs(tr.plane.normal[0]) > 0.95f || fabs(tr.plane.normal[1]) > 0.95f);

            if (is_flat_wall) {
                ent->s.angles[PITCH] = dir[PITCH] + 90;
                ent->s.angles[YAW] = dir[YAW];
                ent->s.angles[ROLL] = 0;
                ent->mins = { 0, -4, -4 };
                ent->maxs = { 8, 4, 4 };
                ent->s.origin = ent->s.origin + (forward * -TRAP_WALL_OFFSET);
            }
            else {
                ent->s.angles = dir;
                ent->s.angles[PITCH] += 90;
                ent->mins = { -4, -4, -4 };
                ent->maxs = { 4, 4, 4 };
                ent->s.origin = ent->s.origin + (forward * -TRAP_WALL_OFFSET);
            }
        }
    }

    // Stop movement and set up trap behavior
    ent->velocity = {};
    ent->avelocity = {};
    ent->movetype = MOVETYPE_NONE;
    ent->touch = nullptr;
    ent->solid = SOLID_BBOX;

    // The state is already part of the global array, so no new/Set is needed.
    // It was initialized in fire_trap.

    ent->think = Trap_Think;
    ent->nextthink = level.time + 10_hz;
    gi.linkentity(ent);

    gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
}

// Helper function to handle trap cooldown state
bool HandleTrapCooldown(edict_t* ent) {
    trap_state_t* trap_state = GetTrapState(ent);
    if (!trap_state)
        return true; // No data, skip processing

    // Check if trap is in cooldown
    if (trap_state->in_cooldown) {
        // If cooldown is over, reset trap to active state
        if (level.time > trap_state->cooldown_end) {
            trap_state->in_cooldown = false;
            trap_state->num_targets = 0;
            ent->s.frame = TRAP_FRAME_ACTIVE; // Reset to active frame
            ent->s.effects |= EF_BLUEHYPERBLASTER; // Re-enable effect
            ent->takedamage = true;
            ent->solid = SOLID_BBOX;
            ent->die = trap_die;
        }

        ent->nextthink = level.time + 10_hz;
        return true; // Skip normal processing while in cooldown
    }

    return false; // Not in cooldown, continue normal processing
}

// Handle trap animation sequence
bool HandleTrapAnimation(edict_t* ent) {
    // ok lets do the blood effect
    if (ent->s.frame > TRAP_FRAME_ACTIVE) {
        if (ent->s.frame == TRAP_FRAME_CONSUME) {
            bool spawn = ent->wait == TRAP_WAIT_START;

            ent->wait -= 2;

            if (spawn)
                gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/trapdown.wav"), 1, ATTN_IDLE, 0);

            ent->delay += 2.f;

            if (ent->wait < TRAP_WAIT_ANIMATION_END)
                ent->s.frame++;

            return true;
        }

        ent->s.frame++;
        if (ent->s.frame == 8) {
            // Get trap data
            trap_state_t* trap_state = GetTrapState(ent);
            if (trap_state) {
                // CRITICAL FIX: Free any remaining gibs when entering cooldown
                for (edict_t* gib : trap_state->owned_gibs) {
                    if (gib && gib->inuse) {
                        G_FreeEdict(gib);
                    }
                }
                trap_state->owned_gibs.clear();

                // Set cooldown instead of freeing
                trap_state->in_cooldown = true;
                trap_state->cooldown_end = level.time + TRAP_COOLDOWN_DURATION;

                // Reset trap state
                ent->s.frame = 0;
                ent->wait = 0;
                ent->delay = 0;
                ent->s.sound = 0; // Stop sound
            }

            ent->nextthink = level.time + 10_hz;
            // Don't free the entity, just make it inactive temporarily
            ent->s.effects &= ~EF_BLUEHYPERBLASTER;
            ent->s.effects &= ~EF_BARREL_EXPLODING;

            // Spawn food cube
            edict_t* best = G_Spawn();
            best->count = ent->mass;
            best->s.scale = 1.f + ((ent->accel - 100.f) / 300.f) * 1.0f;
            SP_item_foodcube(best);
            best->s.origin = ent->s.origin;
            best->s.origin[2] += 24 * best->s.scale;
            best->s.angles[YAW] = frandom() * 360;
            best->velocity[2] = 400;
            best->think(best);
            // Calculate trap cleanup time with adrenaline bonus
            gtime_t cleanup_time = CalculateDeployableLifetime(30_sec, best->teammaster ? best->teammaster->client : nullptr);
            best->nextthink = level.time + cleanup_time;
            best->think = G_FreeEdict;
            best->svflags &= ~SVF_INSTANCED;
            best->s.old_origin = best->s.origin;
            gi.linkentity(best);
            gi.sound(best, CHAN_AUTO, gi.soundindex("misc/fhit3.wav"), 1.f, ATTN_NORM, 0.f);

            return true;
        }
        return true;
    }

    ent->s.effects &= ~EF_BLUEHYPERBLASTER;
    if (ent->s.frame >= TRAP_FRAME_ACTIVE) {
        ent->s.effects |= EF_BLUEHYPERBLASTER;
        // clear the owner if in deathmatch
        if (G_IsDeathmatch())
            ent->owner = nullptr;
    }

    if (ent->s.frame < TRAP_FRAME_ACTIVE) {
        ent->s.frame++;
        return true;
    }

    return false; // Continue with normal processing
}

#include "../horde/g_horde_phys.h"
// Find potential targets for the trap using the Proximity Grid
void FindTrapTargets(edict_t* ent, trap_state_t* trap_state) {
    // Reset target count for this frame
    trap_state->num_targets = 0;

    const auto nearby_entities = HordePhys::g_monster_grid.QueryRadius(ent->s.origin, TRAP_RADIUS);

    for (auto* target : nearby_entities)
    {
        if (!(target->svflags & SVF_MONSTER))
            continue;
        if (target == ent)
            continue;
        if (target != ent->teammaster && CheckTeamDamage(target, ent->teammaster))
            continue;
        if (horde::IsMonsterType(target, horde::MonsterTypeID::TURRET) || (horde::IsMonsterType(target, horde::MonsterTypeID::MISC_INSANE)))
            continue;

        const float len_squared = DistanceSquared(ent->s.origin, target->s.origin);
        if (len_squared > TRAP_RADIUS_SQUARED)
            continue;
        if (!visible(ent, target, false))
            continue;

        const float len = sqrtf(len_squared);

        if (trap_state->num_targets < TRAP_MAX_TARGETS) {
            trap_state->targets[trap_state->num_targets].entity_num = target->s.number; // CORRECTED
            trap_state->targets[trap_state->num_targets].distance = len;
            trap_state->num_targets++;
        }
        else {
            int farthest_idx = 0;
            float farthest_dist = trap_state->targets[0].distance;

            for (int i = 1; i < TRAP_MAX_TARGETS; i++) {
                if (trap_state->targets[i].distance > farthest_dist) {
                    farthest_dist = trap_state->targets[i].distance;
                    farthest_idx = i;
                }
            }

            if (len < farthest_dist) {
                trap_state->targets[farthest_idx].entity_num = target->s.number; // CORRECTED
                trap_state->targets[farthest_idx].distance = len;
            }
        }
    }

    // PERFORMANCE: Sort targets by distance using std::sort for cleaner code
    if (trap_state->num_targets > 1) {
        std::sort(trap_state->targets, trap_state->targets + trap_state->num_targets,
            [](const trap_target_state_t& a, const trap_target_state_t& b) {
                return a.distance < b.distance;
            });
    }
}

// Handle consumption of a target
void ConsumeTarget(edict_t* ent, edict_t* target, const vec3_t& vec) {
    // Consume the target
    ent->takedamage = false;
    ent->solid = SOLID_NOT;
    ent->die = nullptr;

    // Deal damage to consume the target
    T_Damage(target, ent, ent->teammaster, vec3_origin, target->s.origin, vec3_origin,
        10000, 1, DAMAGE_NONE, MOD_TRAP);

    if (target->svflags & SVF_MONSTER)
        M_ProcessPain(target);

    ent->enemy = target;
    ent->wait = TRAP_WAIT_START;
    ent->s.old_origin = ent->s.origin;
    // BUG FIX: Don't recalculate lifetime - trap should keep its original lifetime
    // The timestamp was already set when the trap was created in fire_trap
    // Removing these lines that were causing cumulative lifetime increases:
    // gtime_t trap_lifetime = CalculateDeployableLifetime(30_sec, ent->teammaster ? ent->teammaster->client : nullptr);
    // ent->timestamp = level.time + trap_lifetime;
    ent->accel = target->mass;

    if (G_IsDeathmatch())
        ent->mass = target->mass / 4;
    else
        ent->mass = target->mass / 10;

    // ok spawn the food cube
    ent->s.frame = TRAP_FRAME_CONSUME;

    // PERFORMANCE IMPROVEMENT & BUG FIX: Track gibs in vector and enforce lifetime
    trap_state_t* trap_state = GetTrapState(ent);
    gtime_t gib_expire_time = level.time + TRAP_GIB_LIFETIME;

    // Link up any gibs that this monster may have spawned
    for (uint32_t i = 0; i < globals.num_edicts; i++) {
        edict_t* e = &g_edicts[i];

        if (!e->inuse)
            continue;
        else if (strcmp(e->classname, "gib"))
            continue;
        else if ((e->s.origin - ent->s.origin).lengthSquared() > 16384.f) // 128^2
            continue;

        e->movetype = MOVETYPE_NONE;
        e->nextthink = level.time + FRAME_TIME_S;
        e->think = Trap_Gib_Think;
        e->owner = ent;
        e->timestamp = gib_expire_time;  // CRITICAL FIX: Set expiration time

        // Track this gib for fast cleanup
        if (trap_state) {
            trap_state->owned_gibs.push_back(e);
        }

        Trap_Gib_Think(e);
    }
}

// Handle trap explosion
void ExplodeTrap(edict_t* ent) {
    ent->s.effects &= ~EF_BARREL_EXPLODING;

    T_RadiusDamage_TeamSafe(ent, ent->teammaster, 300, nullptr, 100, DAMAGE_ENERGY, MOD_TRAP);
    BecomeExplosion1(ent);
}

// Process trap targets (pull and potentially consume)
bool ProcessTrapTargets(edict_t* ent, trap_state_t* trap_state) {
    bool consumed_target = false;
    static constexpr gtime_t target_cooldown_react = 1.5_sec;

    for (int i = 0; i < trap_state->num_targets; i++) {
        edict_t* target = &g_edicts[trap_state->targets[i].entity_num];
        const float len = trap_state->targets[i].distance;

        if (!target || !target->inuse)
            continue;

        // Lift target off ground to enable pulling
        if (target->groundentity) {
            target->s.origin[2] += 1;
            target->groundentity = nullptr;
        }

        // PERFORMANCE: Calculate direction vector once
        vec3_t vec = ent->s.origin - target->s.origin;
        float vec_len = vec.normalize();

        // Determine pull speed based on target type and consumption state
        const float max_speed = target->client ? TRAP_PULL_SPEED_PLAYER :
                               (consumed_target ? 190.0f : TRAP_PULL_SPEED_MONSTER);
        target->velocity += (vec * clamp(max_speed - vec_len, TRAP_PULL_SPEED_MIN, max_speed));

        // Make monsters target the trap (like pathfinding "bad area" detection)
        if (target->svflags & SVF_MONSTER) {
            if (level.time - target->monsterinfo.last_reacttodamage_target_time > target_cooldown_react) {
                if (!target->enemy || !horde::IsSpecialType(target->enemy, horde::SpecialEntityTypeID::FOOD_CUBE_TRAP)) {
                    TargetTesla(target, ent);
                    target->monsterinfo.last_reacttodamage_target_time = level.time;
                }
            }
        }

        // Only the first target gets sound and sparks
        if (i == 0) {
            ent->s.sound = gi.soundindex("weapons/trapsuck.wav");
            ent->enemy = target;
            trap_throwsparks(ent, target);
        }

        // Check for consumption (only for non-consumed targets within range)
        if (!consumed_target && len < TRAP_CONSUME_DISTANCE)
        {
            if (target->mass < TRAP_MAX_TARGET_MASS)
            {
                ConsumeTarget(ent, target, vec);
                consumed_target = true;
            }
            else
            {
                ExplodeTrap(ent);
                return true;
            }
        }
    }
    return consumed_target;
}

// Main trap thinking function
THINK(Trap_Think) (edict_t* ent) -> void
{

    if (!ent->teammaster || !ent->teammaster->inuse)
    {
        BecomeExplosion1(ent);
        return;
    }

    // Check if owner is menu protected - pause trap effects but keep it alive
    if (IsPlayerMenuProtected(ent->teammaster)) {
        // Keep the trap alive but skip pull/consume effects
        ent->nextthink = level.time + 10_hz;
        return;
    }

    if (ent->timestamp < level.time) {
        // The state will be freed inside trap_die, which is called by BecomeExplosion1
        trap_die(ent, ent, ent, 0, ent->s.origin, MOD_UNKNOWN);
        return;
    }

    ent->nextthink = level.time + 10_hz;

    if (HandleTrapCooldown(ent))
        return;

    if (HandleTrapAnimation(ent))
        return;

    // Get the trap's state. It should always exist if the trap is active.
    trap_state_t* trap_state = GetTrapState(ent);
    if (!trap_state) {
        // This is a safety check. If the state is missing, something is wrong.
        // Best to just remove the trap.
        BecomeExplosion1(ent);
        return;
    }

    FindTrapTargets(ent, trap_state);
    ProcessTrapTargets(ent, trap_state);
}

// RAFAEL
void fire_trap(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int speed)
    {
        // Check if player is menu protected
        if (IsPlayerMenuProtected(self)) {
            gi.LocClient_Print(self, PRINT_HIGH, "You cannot use this while in a menu.\n");
            return;
        }

        // --- "REPLACE OLDEST" LOGIC ---
        if (self->client && self->client->resp.num_traps >= TrapConstants::MAX_TRAPS_PER_PLAYER) {
            edict_t* oldest = self->client->resp.deployed_traps[self->client->resp.oldest_trap_idx];

            if (oldest && oldest->inuse && horde::IsSpecialType(oldest, horde::SpecialEntityTypeID::FOOD_CUBE_TRAP)) {
                // Call the die function to ensure full cleanup, including decrementing the count.
                // Use quiet removal effect for oldest trap auto-replacement
                g_use_quiet_deployable_removal = true;
                trap_die(oldest, self, self, 0, oldest->s.origin, MOD_UNKNOWN);
                g_use_quiet_deployable_removal = false;
            }
        }

    edict_t* trap;
    vec3_t dir;
    vec3_t forward, right, up;

    dir = vectoangles(aimdir);
    AngleVectors(dir, forward, right, up);

    const float gravityAdjustment = level.gravity / 800.f;

    trap = G_Spawn();
    trap->s.origin = start;
    trap->velocity += aimdir * (speed + crandom() * 10.0f) * gravityAdjustment;
    trap->velocity += right * (crandom() * 10.0f);
    trap->avelocity = { crandom() * 90, crandom() * 90, crandom() * 120 };

    trap->velocity += up * (200 + crandom() * 10.0f) * gravityAdjustment;
    trap->velocity += right * (crandom() * 10.0f);

    trap->avelocity = { 0, 300, 0 };
    trap->movetype = MOVETYPE_BOUNCE;

    trap->solid = SOLID_BBOX;
    trap->takedamage = true;
    trap->mins = { -4, -4, 0 };
    trap->maxs = { 4, 4, 8 };
    trap->die = trap_die;
    trap->health = 125;
    trap->s.modelindex = gi.modelindex("models/weapons/z_trap/tris.md2");
    trap->owner = trap->teammaster = self;

    // Store attacker info in case owner dies before projectile hits
    if (self) {
        if (self->client) {
            trap->projectile_was_player_attacker = true;
            trap->projectile_attacker_type_id = 0;
        } else if (self->svflags & SVF_MONSTER) {
            trap->projectile_was_player_attacker = false;
            trap->projectile_attacker_type_id = self->monsterinfo.monster_type_id;
        }
    }

    // Team assignment
     if (self->client) {
        trap->ctf_team = self->client->resp.ctf_team;
    } else {
        trap->ctf_team = CTF_NOTEAM; // Or determine from owner if owner is not a client
    }

    trap->nextthink = level.time + 1_sec;
    trap->think = Trap_Think;
    trap->classname = "food_cube_trap";
    trap->special_type_id = static_cast<uint8_t>(horde::SpecialTypeRegistry::GetTypeID(trap->classname));
    trap->s.sound = gi.soundindex("weapons/traploop.wav");

    trap->flags |= (FL_DAMAGEABLE | FL_MECHANICAL | FL_TRAP);
    trap->clipmask = MASK_PROJECTILE & ~CONTENTS_DEADMONSTER;

    trap->touch = trap_stick;

    if (self->client && !G_ShouldPlayersCollide(true))
        trap->clipmask &= ~CONTENTS_PLAYER;

    // --- INITIALIZE STATE ---
    // Get the state for this new trap and clear it.
    CreateTrapState(trap); 

    gi.linkentity(trap);
    // Calculate trap lifetime with adrenaline bonus
    gtime_t trap_lifetime = CalculateDeployableLifetime(TRAP_DURATION, self ? self->client : nullptr);
    trap->timestamp = level.time + trap_lifetime;

    g_targetable_special_entities.push_back(trap);

    // --- TRACK ENTITY ---
    if (self->client) {
        // Add the new trap to our tracking array.
        self->client->resp.deployed_traps[self->client->resp.oldest_trap_idx] = trap;
        // Advance the index for the next "oldest".
        self->client->resp.oldest_trap_idx = (self->client->resp.oldest_trap_idx + 1) % TrapConstants::MAX_TRAPS_PER_PLAYER;
        self->client->resp.num_traps++;
    }
}