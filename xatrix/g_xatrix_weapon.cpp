// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.h"

// RAFAEL
void fire_blueblaster(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, effects_t effect)
{
	edict_t* bolt;
	trace_t	 tr;

	bolt = G_Spawn();
	bolt->s.origin = start;
	bolt->s.old_origin = start;
	bolt->s.angles = vectoangles(dir);
	bolt->velocity = dir * speed;
	bolt->svflags |= SVF_PROJECTILE;
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->flags |= FL_DODGE;
	bolt->clipmask = MASK_PROJECTILE;
	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	bolt->s.modelindex = gi.modelindex("models/objects/laser/tris.md2");
	bolt->s.skinnum = 1;
	bolt->s.sound = gi.soundindex("misc/lasfly.wav");
	bolt->owner = self;
	bolt->touch = blaster_unified_touch;
	bolt->nextthink = level.time + 2_sec;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	bolt->style = MOD_BLUEBLASTER;
	gi.linkentity(bolt);

	tr = gi.traceline(self->s.origin, bolt->s.origin, bolt, bolt->clipmask);

	if (tr.fraction < 1.0f)
	{
		bolt->s.origin = tr.endpos + (tr.plane.normal * 1.f);
		bolt->touch(bolt, tr.ent, tr, false);
	}
}

// RAFAEL

/*
fire_ionripper
*/

THINK(ionripper_sparks) (edict_t* self) -> void
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_WELDING_SPARKS);
	gi.WriteByte(0);
	gi.WritePosition(self->s.origin);
	gi.WriteDir(vec3_origin);
	gi.WriteByte(irandom(0xe4, 0xe8));
	gi.multicast(self->s.origin, MULTICAST_PVS, false);

	G_FreeEdict(self);
}

// RAFAEL
TOUCH(ionripper_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (other == self->owner)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal, self->dmg, 1, DAMAGE_ENERGY, MOD_RIPPER);
	}
	else
	{
		return;
	}

	G_FreeEdict(self);
}

// RAFAEL
void fire_ionripper(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, effects_t effect)
{
	edict_t* ion;
	trace_t	 tr;

	ion = G_Spawn();
	ion->s.origin = start;
	ion->s.old_origin = start;
	ion->s.angles = vectoangles(dir);
	ion->velocity = dir * speed;
	ion->movetype = MOVETYPE_WALLBOUNCE;
	ion->clipmask = MASK_PROJECTILE;

	// [Paril-KEX]
if (self && self->client && !G_ShouldPlayersCollide(true))
		ion->clipmask &= ~CONTENTS_PLAYER;

	ion->solid = SOLID_BBOX;
	ion->s.effects |= effect;
	ion->svflags |= SVF_PROJECTILE;
	ion->flags |= FL_DODGE;
	ion->s.renderfx |= RF_FULLBRIGHT;
	ion->s.modelindex = gi.modelindex("models/objects/boomrang/tris.md2");
	ion->s.sound = gi.soundindex("misc/lasfly.wav");
	ion->owner = self;
	ion->touch = ionripper_touch;
	ion->nextthink = level.time + 3_sec;
	ion->think = ionripper_sparks;
	ion->dmg = damage;
	ion->dmg_radius = 100;
	gi.linkentity(ion);

	tr = gi.traceline(self->s.origin, ion->s.origin, ion, ion->clipmask);
	if (tr.fraction < 1.0f)
	{
		ion->s.origin = tr.endpos + (tr.plane.normal * 1.f);
		ion->touch(ion, tr.ent, tr, false);
	}
}

// RAFAEL
/*
fire_heat
*/

THINK(heat_think) (edict_t* self) -> void
{
	edict_t* acquire = nullptr;
	float	 oldlen = 0;
	float	 olddot = 1;

	vec3_t fwd = AngleVectors(self->s.angles).forward;

	// try to stay on current target if possible
	if (self->enemy)
	{
		acquire = self->enemy;

		if (acquire->health <= 0 ||
			!visible(self, acquire))
		{
			self->enemy = acquire = nullptr;
		}
	}

	if (!acquire)
	{
		edict_t* target = nullptr;

		// acquire new target
		while ((target = findradius(target, self->s.origin, 1024)) != nullptr)
		{
			if (self->owner == target)
				continue;
			if (!target->client)
				continue;
			if (target->health <= 0)
				continue;
			if (!visible(self, target))
				continue;

			vec3_t vec = self->s.origin - target->s.origin;
			float len = vec.length();

			float dot = vec.normalized().dot(fwd);

			// targets that require us to turn less are preferred
			if (dot >= olddot)
				continue;

			if (acquire == nullptr || dot < olddot || len < oldlen)
			{
				acquire = target;
				oldlen = len;
				olddot = dot;
			}
		}
	}

	if (acquire != nullptr)
	{
		vec3_t vec = (acquire->s.origin - self->s.origin).normalized();
		float t = self->accel;

		float d = self->movedir.dot(vec);

		if (d < 0.45f && d > -0.45f)
			vec = -vec;

		self->movedir = slerp(self->movedir, vec, t).normalized();
		self->s.angles = vectoangles(self->movedir);

		if (self->enemy != acquire)
		{
			gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/railgr1a.wav"), 1.f, 0.25f, 0);
			self->enemy = acquire;
		}
	}
	else
		self->enemy = nullptr;

	self->velocity = self->movedir * self->speed;
	self->nextthink = level.time + FRAME_TIME_MS;
}

// RAFAEL
void fire_heat(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, float damage_radius, int radius_damage, float turn_fraction)
{
	edict_t* heat;

	heat = G_Spawn();
	heat->s.origin = start;
	heat->movedir = dir;
	heat->s.angles = vectoangles(dir);
	heat->velocity = dir * speed;
	heat->flags |= FL_DODGE;
	heat->movetype = MOVETYPE_FLYMISSILE;
	heat->svflags |= SVF_PROJECTILE;
	heat->clipmask = MASK_PROJECTILE;
	heat->solid = SOLID_BBOX;
	heat->s.effects |= EF_ROCKET;
	heat->s.modelindex = gi.modelindex("models/objects/rocket/tris.md2");
	heat->owner = self;
	heat->touch = rocket_touch;
	heat->speed = speed;
	heat->accel = turn_fraction;

	heat->nextthink = level.time + FRAME_TIME_MS;
	heat->think = heat_think;

	heat->dmg = damage;
	heat->radius_dmg = radius_damage;
	heat->dmg_radius = damage_radius;
	heat->s.sound = gi.soundindex("weapons/rockfly.wav");

	if (visible(heat, self->enemy))
	{
		heat->enemy = self->enemy;
		gi.sound(heat, CHAN_WEAPON, gi.soundindex("weapons/railgr1a.wav"), 1.f, 0.25f, 0);
	}

	gi.linkentity(heat);
}


// RAFAEL

/*
fire_plasma
*/


TOUCH(plasma_touch) (edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	vec3_t origin;

	if (other == ent->owner)
		return;

    if (tr.surface && (tr.surface->flags & SURF_SKY))
    {
        // Check if owner is a fixbot
        if (ent->owner && (strcmp(ent->owner->classname, "monster_fixbot") == 0 ||
            strcmp(ent->owner->classname, "monster_fixbotkl") == 0))
        {
            // Don't destroy the plasma - just let it continue through the sky
            // Optionally adjust direction slightly for more variety
            vec3_t newdir = ent->velocity;
            newdir.normalize();

            // Add slight random deviation
            newdir[0] += crandom() * 0.05f;
            newdir[1] += crandom() * 0.05f;
            newdir[2] += crandom() * 0.05f;
            newdir.normalize();

            ent->velocity = newdir * ent->speed;
            ent->s.angles = vectoangles(newdir);
            ent->movedir = newdir;

            return;
        }

        // Standard behavior for non-fixbot owners
        G_FreeEdict(ent);
        return;
    }

	if (ent->owner && ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	origin = ent->s.origin + tr.plane.normal;

	if (other->takedamage)
	{
		T_Damage(other, ent, ent->owner, ent->velocity, ent->s.origin, tr.plane.normal, ent->dmg, ent->dmg, DAMAGE_ENERGY, MOD_PHALANX);
	}

	T_RadiusDamage(ent, ent->owner, (float)ent->radius_dmg, other, ent->dmg_radius, DAMAGE_ENERGY, MOD_PHALANX);

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_PLASMA_EXPLOSION);
	gi.WritePosition(origin);
	gi.multicast(ent->s.origin, MULTICAST_PHS, false);

	G_FreeEdict(ent);
}

// RAFAEL
void fire_plasma(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t* plasma;

	plasma = G_Spawn();
	plasma->s.origin = start;
	plasma->movedir = dir;
	plasma->s.angles = vectoangles(dir);
	plasma->velocity = dir * speed;
	plasma->movetype = MOVETYPE_FLYMISSILE;
	plasma->clipmask = MASK_PROJECTILE;

	// [Paril-KEX]
	if (self && self->client && !G_ShouldPlayersCollide(true))
		plasma->clipmask &= ~CONTENTS_PLAYER;

	plasma->solid = SOLID_BBOX;
	plasma->svflags |= SVF_PROJECTILE;
	plasma->flags |= FL_DODGE;
	plasma->owner = self;
	plasma->touch = plasma_touch;
	plasma->nextthink = level.time + gtime_t::from_sec(8000.f / speed);
	plasma->think = G_FreeEdict;
	plasma->dmg = damage;
	plasma->radius_dmg = radius_damage;
	plasma->dmg_radius = damage_radius;
	plasma->s.sound = gi.soundindex("weapons/rockfly.wav");

	plasma->s.modelindex = gi.modelindex("sprites/s_photon.sp2");
	plasma->s.effects |= EF_PLASMA | EF_ANIM_ALLFAST;

	gi.linkentity(plasma);
}

// Constants for trap positioning and behavior
constexpr float TRAP_WALL_OFFSET = 3.0f;       // Offset for walls
constexpr float TRAP_CEILING_OFFSET = -20.4f;  // Offset for ceilings
constexpr float TRAP_FLOOR_OFFSET = -12.0f;    // Offset for floor
constexpr float TRAP_ORB_OFFSET = 12.0f;       // Normal sphere height
constexpr float TRAP_ORB_OFFSET_CEIL = -18.0f; // Ceiling sphere height
constexpr int TRAP_MAX_TARGETS = 3;            // Maximum number of targets
constexpr float TRAP_RADIUS = 400.0f;          // Trap pull radius
constexpr float TRAP_RADIUS_SQUARED = TRAP_RADIUS * TRAP_RADIUS; // Pre-computed squared radius
constexpr float TRAP_CONSUME_DISTANCE = 48.0f; // Distance at which trap consumes target
constexpr float TRAP_MAX_TARGET_MASS = 400.0f; // Maximum mass that can be consumed
constexpr int TRAP_WAIT_START = 64;            // Initial wait value for consuming
constexpr int TRAP_FRAME_CONSUME = 5;          // Frame for consumption animation
constexpr int TRAP_FRAME_ACTIVE = 4;           // Frame for active trap
constexpr gtime_t TRAP_DURATION = 80_sec;         // Total trap lifetime in seconds


// State for a single trap target
struct trap_target_state_t {
    int      entity_num; // Use entity number for safety
    float    distance;
};

// The main state for a trap entity. Only Plain Old Data.
struct trap_state_t {
    trap_target_state_t targets[TRAP_MAX_TARGETS];
    int                 num_targets;
    bool                in_cooldown;
    gtime_t             cooldown_end;

    // Helper to reset the state to its default values
    void clear() {
        for (auto& target : targets) {
            target.entity_num = 0;
            target.distance = 0.0f;
        }
        num_targets = 0;
        in_cooldown = false;
        cooldown_end = 0_sec;
    }
};

// --- GLOBAL STATE ARRAY ---
// This is the safe, engine-native replacement for `new`/`delete`.
// It holds the state for every possible entity.
static trap_state_t g_trap_states[MAX_EDICTS];

// Helper to get a pointer to a trap's state.
// Returns nullptr if the entity is invalid.
static trap_state_t* GetTrapState(const edict_t* ent) {
    if (!ent || !ent->inuse) {
        return nullptr;
    }
    // The entity's number is its index into the global state array.
    return &g_trap_states[ent->s.number];
}

// Modified to throw sparks at a specific target
// Modified to throw sparks at a specific target with improved validation
void trap_throwsparks(edict_t* self, edict_t* target)
{
    if (!self || !self->inuse || !target || !target->inuse)
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

// Original version with validation for backward compatibility
void trap_throwsparks(edict_t* self)
{
    if (!self || !self->inuse || !self->enemy || !self->enemy->inuse)
        return;

    trap_throwsparks(self, self->enemy);
}

THINK(Trap_Gib_Think) (edict_t* ent) -> void
{
    // Verificar si ent es válido
    if (!ent)
        return;

    // Verificar si owner es válido
    if (!ent->owner || !ent->owner->inuse)
    {
        G_FreeEdict(ent);
        return;
    }

    if (ent->owner->s.frame != TRAP_FRAME_CONSUME)
    {
        G_FreeEdict(ent);
        return;
    }

    vec3_t forward, right, up;
    vec3_t vec;

    AngleVectors(ent->owner->s.angles, forward, right, up);

    // rotate us around the center
    float degrees = (150.f * gi.frame_time_s) + ent->owner->delay;
    vec3_t diff = ent->owner->s.origin - ent->s.origin;
    vec = RotatePointAroundVector(up, diff, degrees);
    ent->s.angles[1] += degrees;
    vec3_t new_origin = ent->owner->s.origin - vec;

    trace_t tr = gi.traceline(ent->s.origin, new_origin, ent, MASK_SOLID);
    ent->s.origin = tr.endpos;

    // pull us towards the trap's center
    diff.normalize();
    ent->s.origin += diff * (15.0f * gi.frame_time_s);

    ent->watertype = gi.pointcontents(ent->s.origin);
    if (ent->watertype & MASK_WATER)
        ent->waterlevel = WATER_FEET;

    ent->nextthink = level.time + FRAME_TIME_S;
    gi.linkentity(ent);
}

DIE(trap_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
    // --- UPDATE PLAYER TRACKING ---
    if (self->owner && self->owner->client) {
        gclient_t* client = self->owner->client;
        if (client->resp.num_traps > 0) {
            client->resp.num_traps--;
        }

        // --- FIX: ADD THIS LOOP ---
        // Find this trap in the owner's tracking array and null it out.
        for (int i = 0; i < TrapConstants::MAX_TRAPS_PER_PLAYER; ++i) {
            if (client->resp.deployed_traps[i] == self) {
                client->resp.deployed_traps[i] = nullptr;
                break; // Found and removed, no need to search further.
            }
        }
        // --- END FIX ---
    }

    // --- CLEAN UP GLOBAL STATE ---
    trap_state_t* state = GetTrapState(self);
    if (state) {
        state->clear();
    }

    BecomeExplosion1(self);
}

void Trap_Think(edict_t* ent);
void SP_item_foodcube(edict_t* best);
void SpawnDamage(int type, const vec3_t& origin, const vec3_t& normal, int damage);

// New touch function for trap sticking behavior
// New touch function for trap sticking behavior
TOUCH(trap_stick)(edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
    if (!other || !other->inuse || other == ent->owner || !(other->solid == SOLID_BSP || other->movetype == MOVETYPE_PUSH || other->solid == SOLID_BBOX))
        return;

    // Handle non-world entities (similar to tesla)
    if (other != world && (other->movetype != MOVETYPE_PUSH || other->svflags & SVF_MONSTER ||
        other->client || strcmp(other->classname, "func_train") == 0))
    {
        if (tr.plane.normal) {
            vec3_t out{};
            float const backoff = ent->velocity.dot(tr.plane.normal) * 1.35f;
            for (int i = 0; i < 3; i++) {
                float change = tr.plane.normal[i] * backoff;
                out[i] = ent->velocity[i] - change;
                out[i] += crandom() * 70.0f;
                if (fabs(out[i]) < 120.0f && out[i] != 0) {
                    out[i] = (out[i] < 0 ? -120.0f : 120.0f);
                }
            }
            if (tr.plane.normal[2] > 0) {
                out[2] += 180.0f;
            }
            if (out.length() < 120.0f) {
                out.normalize();
                out = out * 120.0f;
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

            if (ent->wait < 19)
                ent->s.frame++;

            return true;
        }

        ent->s.frame++;
        if (ent->s.frame == 8) {
            // Get trap data
            trap_state_t* trap_state = GetTrapState(ent);
            if (trap_state) {
                // Set cooldown instead of freeing
                trap_state->in_cooldown = true;
                trap_state->cooldown_end = level.time + 10_sec; // Use literal directly

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
            best->nextthink = level.time + 30_sec;  // Use literal directly
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

    // Sort targets by distance (optimized for small array)
    if (trap_state->num_targets > 1) {
        if (trap_state->num_targets == 2) {
            if (trap_state->targets[0].distance > trap_state->targets[1].distance) {
                trap_target_state_t temp = trap_state->targets[0]; // CORRECTED
                trap_state->targets[0] = trap_state->targets[1];
                trap_state->targets[1] = temp;
            }
        }
        else if (trap_state->num_targets == 3) {
            if (trap_state->targets[0].distance > trap_state->targets[1].distance) {
                trap_target_state_t temp = trap_state->targets[0]; // CORRECTED
                trap_state->targets[0] = trap_state->targets[1];
                trap_state->targets[1] = temp;
            }
            if (trap_state->targets[1].distance > trap_state->targets[2].distance) {
                trap_target_state_t temp = trap_state->targets[1]; // CORRECTED
                trap_state->targets[1] = trap_state->targets[2];
                trap_state->targets[2] = temp;

                if (trap_state->targets[0].distance > trap_state->targets[1].distance) {
                    trap_target_state_t temp = trap_state->targets[0]; // CORRECTED
                    trap_state->targets[0] = trap_state->targets[1];
                    trap_state->targets[1] = temp;
                }
            }
        }
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
    ent->timestamp = level.time + 30_sec;
    ent->accel = target->mass;

    if (G_IsDeathmatch())
        ent->mass = target->mass / 4;
    else
        ent->mass = target->mass / 10;

    // ok spawn the food cube
    ent->s.frame = TRAP_FRAME_CONSUME;

    // link up any gibs that this monster may have spawned
    for (uint32_t i = 0; i < globals.num_edicts; i++) {
        edict_t* e = &g_edicts[i];

        if (!e->inuse)
            continue;
        else if (strcmp(e->classname, "gib"))
            continue;
        else if ((e->s.origin - ent->s.origin).length() > 128.f)
            continue;

        e->movetype = MOVETYPE_NONE;
        e->nextthink = level.time + FRAME_TIME_S;
        e->think = Trap_Gib_Think;
        e->owner = ent;
        Trap_Gib_Think(e);
    }
}

// Handle trap explosion
void ExplodeTrap(edict_t* ent) {
    ent->s.effects &= ~EF_BARREL_EXPLODING;

    T_RadiusDamage(ent, ent->teammaster, 300, nullptr, 100, DAMAGE_ENERGY, MOD_TRAP);
    BecomeExplosion1(ent);
}

// Process trap targets (pull and potentially consume)
bool ProcessTrapTargets(edict_t* ent, trap_state_t* trap_state) {
    bool consumed_target = false;

    for (int i = 0; i < trap_state->num_targets; i++) {
        // --- CORRECTED: Get edict from entity number ---
        edict_t* target = &g_edicts[trap_state->targets[i].entity_num];
        float len = trap_state->targets[i].distance;

        if (!target || !target->inuse)
            continue;

        if (consumed_target && len < TRAP_CONSUME_DISTANCE) {
            if (target->groundentity) {
                target->s.origin[2] += 1;
                target->groundentity = nullptr;
            }
            vec3_t vec = ent->s.origin - target->s.origin;
            float vec_len = vec.normalize();
            const float max_speed = target->client ? 290.f : 190.f;
            target->velocity += (vec * clamp(max_speed - vec_len, 64.f, max_speed));
            continue;
        }

        if (target->groundentity) {
            target->s.origin[2] += 1;
            target->groundentity = nullptr;
        }

        vec3_t vec = ent->s.origin - target->s.origin;
        float vec_len = vec.normalize();
        const float max_speed = target->client ? 290.f : 210.f;
        target->velocity += (vec * clamp(max_speed - vec_len, 64.f, max_speed));

        if (i == 0) {
            ent->s.sound = gi.soundindex("weapons/trapsuck.wav");
        }

        ent->enemy = target;
        trap_throwsparks(ent, target);

        if (len < TRAP_CONSUME_DISTANCE && !consumed_target)
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
    if (ent->timestamp < level.time) {
        // The state will be freed inside trap_die, which is called by BecomeExplosion1
        BecomeExplosion1(ent);
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
    // --- "REPLACE OLDEST" LOGIC ---
    if (self->client && self->client->resp.num_traps >= TrapConstants::MAX_TRAPS_PER_PLAYER) {
        // Get the oldest trap from our circular buffer.
        edict_t* oldest = self->client->resp.deployed_traps[self->client->resp.oldest_trap_idx];

        // Ensure it's a valid, in-use trap before freeing.
        if (oldest && oldest->inuse && oldest->classname && horde::IsSpecialType(oldest, horde::SpecialEntityTypeID::FOOD_CUBE_TRAP)) {
            // G_FreeEdict will call trap_die, which correctly handles all cleanup.
            G_FreeEdict(oldest);
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
    trap->health = 250;
    trap->s.modelindex = gi.modelindex("models/weapons/z_trap/tris.md2");
    trap->owner = trap->teammaster = self;

    // Team assignment
    const char* trap_team;
    if (self->client && self->client->resp.ctf_team == CTF_TEAM1) {
        trap_team = TEAM1;
    }
    else if (self->client && self->client->resp.ctf_team == CTF_TEAM2) {
        trap_team = TEAM2;
    }
    else {
        trap_team = "neutral";
    }
    trap->team = trap_team;
    trap->teammaster->team = trap_team;

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
    trap_state_t* state = GetTrapState(trap);
    if (state) {
        state->clear();
    }

    gi.linkentity(trap);
    trap->timestamp = level.time + TRAP_DURATION;

    // --- TRACK ENTITY ---
    if (self->client) {
        // Add the new trap to our tracking array.
        self->client->resp.deployed_traps[self->client->resp.oldest_trap_idx] = trap;
        // Advance the index for the next "oldest".
        self->client->resp.oldest_trap_idx = (self->client->resp.oldest_trap_idx + 1) % TrapConstants::MAX_TRAPS_PER_PLAYER;
        self->client->resp.num_traps++;
    }
}