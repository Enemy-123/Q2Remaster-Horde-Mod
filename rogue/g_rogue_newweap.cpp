// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "../g_local.h"
#include "../shared.h"
#include "../horde/g_horde_benefits.h"

// Forward declaration for burn function from g_fire.cpp
void apply_burning(edict_t* target, edict_t* attacker, int damage, gtime_t duration);

/*
========================
fire_flechette
========================
*/
// explosive flechettes!
// THINK(delayed_flechette_explode)(edict_t* self) -> void {
//	T_RadiusDamage(self, self->owner, 40, nullptr, 100, DAMAGE_NO_REG_ARMOR, MOD_ETF_RIFLE);
//
//	gi.WriteByte(svc_temp_entity);
//	gi.WriteByte(TE_EXPLOSION1);
//	gi.WritePosition(self->s.origin);
//	gi.multicast(self->s.origin, MULTICAST_PHS, false);
//
//	G_FreeEdict(self);
// }
//
//// En flechette_touch:
// if (!other->takedamage) {
//	self->movetype = MOVETYPE_NONE;
//	self->think = delayed_flechette_explode;
//	self->nextthink = level.time + 1_sec;
//	return;
// }
///
TOUCH(flechette_touch)(edict_t *self, edict_t *other, const trace_t &tr, bool other_touching_self)->void
{
	if (other == self->owner)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner && self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		// Determine real attacker (handles sentry guns, doppelgangers, etc.)
		edict_t *attacker = GetRealAttacker(self);

		// Use MOD_TURRET for sentry gun projectiles to avoid strength tech doubling damage
		mod_t mod_type = MOD_ETF_RIFLE;
		if (self->owner && horde::IsMonsterType(self->owner, horde::MonsterTypeID::SENTRYGUN))
			mod_type = MOD_TURRET;

		// FIXED: ETF Rifle now bypasses both regular armor AND power armor completely
		T_Damage(other, self, attacker, self->velocity, self->s.origin, tr.plane.normal,
				 self->dmg, (int)self->dmg_radius, DAMAGE_NO_REG_ARMOR | DAMAGE_NO_POWER_ARMOR, mod_type);
	}
	else
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_FLECHETTE);
		gi.WritePosition(self->s.origin);
		gi.WriteDir(tr.plane.normal);
		gi.multicast(self->s.origin, MULTICAST_PHS, false);
	}

	G_FreeEdict(self);
}

void fire_flechette(edict_t *self, const vec3_t &start, const vec3_t &dir, int damage, int speed, int kick)
{
	edict_t *flechette;

	flechette = G_Spawn();
	flechette->s.origin = start;
	flechette->s.old_origin = start;
	flechette->s.angles = vectoangles(dir);
	flechette->velocity = dir * speed;
	flechette->svflags |= SVF_PROJECTILE;
	flechette->movetype = MOVETYPE_FLYMISSILE;
	flechette->clipmask = MASK_PROJECTILE;
	flechette->flags |= FL_DODGE;

	// [Paril-KEX]
	if (self && self->client && !G_ShouldPlayersCollide(true))
		flechette->clipmask &= ~CONTENTS_PLAYER;

	flechette->solid = SOLID_BBOX;
	flechette->s.renderfx = RF_FULLBRIGHT;
	flechette->s.modelindex = gi.modelindex("models/proj/flechette/tris.md2");

	flechette->owner = self;

	// Store attacker info in case owner dies before projectile hits
	SetProjectileAttackerInfo(flechette, self);

	flechette->touch = flechette_touch;
	flechette->nextthink = level.time + gtime_t::from_sec(8000.f / speed);
	flechette->think = G_FreeEdict;
	flechette->dmg = damage;
	flechette->dmg_radius = (float)kick;

	gi.linkentity(flechette);

	trace_t tr = gi.traceline(self->s.origin, flechette->s.origin, flechette, flechette->clipmask);
	// [Horde] startsolid: player overlapping a damageable entity -> hit it at spawn
	if (tr.fraction < 1.0f || (tr.startsolid && self->client && tr.ent && tr.ent->takedamage))
	{
		flechette->s.origin = tr.endpos + (tr.plane.normal * 1.f);
		flechette->touch(flechette, tr.ent, tr, false);
	}
}

// =======================================================================
// PROXIMITY MINE (g_upgradeprox) - Lethality & Intelligence Overhaul
// =======================================================================

// --- Tuned Constants for Increased Lethality - Now loaded from config ---
inline gtime_t PROX_TIME_TO_LIVE() { return gtime_t::from_sec(g_config.prox_mine.time_to_live_sec); }
inline gtime_t PROX_TIME_DELAY() { return gtime_t::from_ms(g_config.prox_mine.time_delay_ms); }
inline float PROX_BOUND_SIZE() { return g_config.prox_mine.bound_size; }
inline float PROX_DAMAGE_RADIUS() { return g_config.prox_mine.damage_radius; }
inline int32_t PROX_HEALTH() { return g_config.prox_mine.health; }
inline int32_t PROX_DAMAGE() { return g_config.prox_mine.damage; }
inline float PROX_DAMAGE_OPEN_MULTIPLIER() { return g_config.prox_mine.damage_open_multiplier; }

// --- Configuration for a much deadlier cluster explosion ---
struct ClusterConfig
{
	int num_grenades;		 // Total number of fragments
	int direct_grenades;	 // Fragments fired directly down for immediate area denial
	float spread_angle;		 // Base spread angle for fragments
	float min_velocity;		 // Minimum fragment speed
	float max_velocity;		 // Maximum fragment speed
	float min_fuse_time;	 // Minimum fragment fuse time
	float max_fuse_time;	 // Maximum fragment fuse time
	float damage_multiplier; // Damage of each fragment relative to the main explosion
	float homing_bias;		 // Aggressiveness of homing towards enemies (0-1)
	float search_radius;	 // How far the mine looks for targets to home towards
};

// --- New, more aggressive cluster configuration ---
static const ClusterConfig UPGRADED_CLUSTER_CONFIG = {
	8,		// num_grenades (reduced from 15 to 8)
	3,		// direct_grenades (reduced from 5 to 3)
	50.0f,	// spread_angle (was 45.0f)
	500.0f, // min_velocity (was 400.0f)
	750.0f, // max_velocity (was 600.0f)
	0.4f,	// min_fuse_time (was 0.5f)
	1.8f,	// max_fuse_time (was 2.0f)
	0.35f,	// damage_multiplier (reduced from 0.6f to 0.35f - weaker clusters)
	0.55f,	// homing_bias (was 0.3f, now much more aggressive)
	512.0f	// search_radius (was 384.0f)
};;

// --- Smarter Enemy Finding: Now checks for line-of-sight ---
edict_t *FindNearestVisibleEnemy(edict_t *from, const vec3_t &origin, float search_radius)
{
	edict_t *nearest = nullptr;
	float nearest_dist_squared = search_radius * search_radius;
	edict_t *current = nullptr;

	while ((current = findradius(current, origin, search_radius)) != nullptr)
	{
		if (!current->inuse || current->health <= 0)
			continue;

		if (!(current->svflags & SVF_MONSTER) && !current->client)
			continue;

		if (current == from->teammaster || OnSameTeam(current, from->teammaster))
			continue;

		// --- INTELLIGENCE UPGRADE: Ensure we can actually see the target ---
		if (!visible(from, current))
			continue;

		vec3_t const diff = current->s.origin - origin;
		float const dist_squared = diff.lengthSquared();

		if (dist_squared < nearest_dist_squared)
		{
			nearest = current;
			nearest_dist_squared = dist_squared;
		}
	}
	return nearest;
}

// --- More Lethal Clustering Logic ---
void SpawnClusterGrenades(edict_t *owner_mine, const vec3_t &origin, int base_damage)
{
	const ClusterConfig &config = UPGRADED_CLUSTER_CONFIG;

	// Find the best visible enemy to target
	const edict_t *nearest_enemy = FindNearestVisibleEnemy(owner_mine, origin, config.search_radius);

	vec3_t enemy_dir{};
	if (nearest_enemy)
	{
		enemy_dir = nearest_enemy->s.origin - origin;
		enemy_dir.normalize();
	}

	int const fragment_damage = static_cast<int>(base_damage * config.damage_multiplier);
	vec3_t gravity_influence{0, 0, -0.1f}; // Softer gravity for wider spread

	for (int n = 0; n < config.num_grenades; n++)
	{
		vec3_t forward;

		if (n < config.direct_grenades)
		{
			// Direct downward burst for anyone standing on the mine
			forward = {(frandom() - 0.5f) * 0.2f, (frandom() - 0.5f) * 0.2f, -1.0f};
		}
		else
		{
			// Create a lethal cone of shrapnel
			float yaw = crandom() * 180.0f;							  // 360 degree horizontal spread
			float pitch = -config.spread_angle - (frandom() * 30.0f); // Varying upward angle
			vec3_t angles{pitch, yaw, 0};
			auto [fwd, right, up] = AngleVectors(angles);
			forward = fwd;
		}

		// --- SMARTER HOMING: Aggressively steer fragments towards the target ---
		if (nearest_enemy)
		{
			// Interpolate between random spread and direct-to-target vector
			forward = safe_normalized(forward * (1.0f - config.homing_bias) + enemy_dir * config.homing_bias);
		}

		// Add slight gravity influence for a more natural arc
		forward = safe_normalized(forward + gravity_influence);

		float const velocity = config.min_velocity + (config.max_velocity - config.min_velocity) * frandom();
		float const explode_time = config.min_fuse_time + (config.max_fuse_time - config.min_fuse_time) * frandom();

		// Vary damage slightly to make it less predictable
		int adjusted_damage = static_cast<int>(fragment_damage * (0.9f + frandom() * 0.2f));

		// Use the player who threw the mine as the owner for proper kill credit
		fire_grenade2(owner_mine->teammaster, origin, forward, adjusted_damage, velocity,
					  gtime_t::from_sec(explode_time),
					  static_cast<float>(adjusted_damage), false, true);
	}
}

// --- HELPER FUNCTION ---
void CleanupProxFromOwner(edict_t* prox) {
    if (!prox || !prox->owner || !prox->owner->client) {
        return;
    }

    gclient_t* client = prox->owner->client;
    if (client->resp.num_proxs > 0) {
        client->resp.num_proxs--;
    }

    // --- FIX: ADD THIS LOOP ---
    // Find this prox in the owner's tracking array and null it out.
    for (int i = 0; i < ProxConstants::MAX_PROXS_ARRAY_SIZE; ++i) {
        if (client->resp.deployed_proxs[i] == prox) {
            client->resp.deployed_proxs[i] = nullptr;
            break; // Found and removed.
        }
    }
    // --- END FIX ---
}

// --- Main Explosion Logic (MODIFIED) ---
static void Prox_ExplodeReal(edict_t *ent, edict_t *other, vec3_t normal)
{
	if (ent->teamchain && ent->teamchain->owner == ent)
	{
		G_FreeEdict(ent->teamchain);
	}

	edict_t *owner = ent->teammaster ? ent->teammaster : ent;

	if (ent->teammaster)
	{
		PlayerNoise(owner, ent->s.origin, PNOISE_IMPACT);
	}

	if (other && other->takedamage)
	{
		vec3_t const dir = other->s.origin - ent->s.origin;
		T_Damage(other, ent, owner, dir, ent->s.origin, normal,
				 ent->dmg, ent->dmg, DAMAGE_NONE, MOD_PROX);
	}

	if (ent->dmg > PROX_DAMAGE() * PROX_DAMAGE_OPEN_MULTIPLIER())
	{
		gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);
	}

	ent->takedamage = false;
	vec3_t const explosion_origin = ent->s.origin + normal;

	T_RadiusDamage(ent, owner, static_cast<float>(ent->dmg), other,
				   ent->dmg_radius, DAMAGE_NONE, MOD_PROX);

	// Check flag to use quiet removal effect
	if (g_use_quiet_deployable_removal) {
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BFG_EXPLOSION);
		gi.WritePosition(ent->s.origin);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);
	} else {
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(ent->groundentity ? TE_GRENADE_EXPLOSION : TE_ROCKET_EXPLOSION);
		gi.WritePosition(explosion_origin);
		gi.multicast(ent->s.origin, MULTICAST_PHS, false);
	}

	// Check if the owner (player who fired) has the cluster prox upgrade
	if (ent->owner && ent->owner->client && (ClassicPlayerHasBenefitClusterProx(ent->owner) || ent->owner->client->pers.skills.pl_improved_traps))
	{
		SpawnClusterGrenades(ent, explosion_origin, ent->dmg);
	}

    // --- NEW: Update owner's tracking before freeing the edict ---
    CleanupProxFromOwner(ent);

	G_FreeEdict(ent);
}


THINK(Prox_Explode)(edict_t *ent)->void
{
	Prox_ExplodeReal(ent, nullptr, (ent->velocity * -0.02f));
}

//===============
//===============
DIE(prox_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t &point, const mod_t &mod)->void
{
	// Check if inflictor is not nullptr and if set off by another prox, delay a little (chained explosions)
	if (horde::IsSpecialType(inflictor, horde::SpecialEntityTypeID::PROX_MINE))
	{
		self->takedamage = false;
		Prox_Explode(self);
	}
	else
	{
		self->takedamage = false;
		self->think = Prox_Explode;
		self->nextthink = level.time + FRAME_TIME_S;
	}
}
TOUCH(Prox_Field_Touch)(edict_t *ent, edict_t *other, const trace_t &tr, bool other_touching_self)->void
{
	if (!ent || !other)
	{
		// Log error or handle null ent or other
		return;
	}

	edict_t *prox = nullptr;

	if (!(other->svflags & SVF_MONSTER))
	{
		// explode only if it's a monster
		return;
	}

	// trigger the prox mine if it's still there, and still mine.
	if (ent->owner)
	{
		prox = ent->owner;
	}
	else
	{
		// Log error or handle null owner
		return;
	}

	// teammate avoidance
	if (prox->teammaster && CheckTeamDamage(prox->teammaster, other))
	{
		return;
	}

	if (G_IsDeathmatch() && g_horde && g_horde->integer && other->client)
	{
		// no self damage using traps on DM/Horde
		return;
	}

	if (other == prox)
	{
		// don't set self off
		return;
	}

	if (prox->think == Prox_Explode)
	{
		// we're set to blow!
		return;
	}

	if (prox->teamchain == ent)
	{
		if (gi.soundindex && gi.sound)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/proxwarn.wav"), 1, ATTN_NORM, 0);
		}
		prox->think = Prox_Explode;
		prox->nextthink = level.time + PROX_TIME_DELAY();
		return;
	}

	if (ent)
	{
		ent->solid = SOLID_NOT;
		G_FreeEdict(ent);
	}
}

//===============
//===============
THINK(prox_seek)(edict_t *ent)->void
{
	if (level.time > gtime_t::from_sec(ent->wait))
	{
		Prox_Explode(ent);
	}
	else
	{
		ent->s.frame++;
		if (ent->s.frame > 13)
			ent->s.frame = 9;
		ent->think = prox_seek;
		ent->nextthink = level.time + 10_hz;
	}
}

//===============
//===============
// --- More Aggressive Ambush Logic ---
THINK(prox_open)(edict_t *ent)->void
{
	if (ent->s.frame == 9) // End of opening animation
	{
		ent->s.sound = 0;

		if (G_IsDeathmatch() && !g_horde->integer)
			ent->owner = nullptr;

		if (ent->teamchain)
			ent->teamchain->touch = Prox_Field_Touch;

		// --- SMARTER AMBUSH: Scan a larger radius for enemies upon arming ---
		edict_t *search = nullptr;
		const float ambush_radius = PROX_DAMAGE_RADIUS() * 1.2f; // Increased scan radius

		while ((search = findradius(search, ent->s.origin, ambush_radius)) != nullptr)
		{
			if (!search->inuse || search == ent || OnSameTeam(search, ent->teammaster))
				continue;

			// If a visible monster or enemy player is nearby, detonate immediately
			if (search->takedamage && (search->svflags & SVF_MONSTER)) //| search->client))
			{
				if (visible(ent, search))
				{
					gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/proxwarn.wav"), 1, ATTN_NORM, 0);
					Prox_Explode(ent);
					return;
				}
			}
		}

		// Standard lifetime logic if no ambush target is found
		ent->wait = (level.time + PROX_TIME_TO_LIVE()).seconds();
		ent->think = prox_seek;
		ent->nextthink = level.time + 200_ms;
	}
	else // Opening animation
	{
		if (ent->s.frame == 0)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/proxopen.wav"), 1, ATTN_NORM, 0);
			ent->dmg = static_cast<int>(ent->dmg * PROX_DAMAGE_OPEN_MULTIPLIER());
		}
		ent->s.frame++;
		ent->think = prox_open;
		ent->nextthink = level.time + 10_hz;
	}
}

//===============
//===============
TOUCH(prox_land)(edict_t *ent, edict_t *other, const trace_t &tr, bool other_touching_self)->void
{
	edict_t *field;
	vec3_t dir;
	vec3_t forward, right, up;
	movetype_t movetype = MOVETYPE_NONE;
	int stick_ok = 0;
	vec3_t land_point;

	// must turn off owner so owner can shoot it and set it off
	// moved to prox_open so owner can get away from it if fired at pointblank range into
	// wall
	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(ent);
		return;
	}

	if (tr.plane.normal)
	{
		land_point = ent->s.origin + (tr.plane.normal * -10.0f);
		if (gi.pointcontents(land_point) & (CONTENTS_SLIME | CONTENTS_LAVA))
		{
			Prox_Explode(ent);
			return;
		}
	}

	constexpr float PROX_STOP_EPSILON = 0.1f;

	if (!tr.plane.normal || (other->svflags & SVF_MONSTER) || other->client || (other->flags & FL_DAMAGEABLE))
	{
		if (other != ent->teammaster)
			Prox_ExplodeReal(ent, other, tr.plane.normal);

		return;
	}
	else if (other != world)
	{
		// Here we need to check to see if we can stop on this entity.
		// Note that plane can be nullptr

		// PMM - code stolen from g_phys (ClipVelocity)
		vec3_t out{};
		float backoff, change;
		int i;

		if ((other->movetype == MOVETYPE_PUSH) && (tr.plane.normal[2] > 0.7f))
			stick_ok = 1;
		else
			stick_ok = 0;

		backoff = ent->velocity.dot(tr.plane.normal) * 1.5f;
		for (i = 0; i < 3; i++)
		{
			change = tr.plane.normal[i] * backoff;
			out[i] = ent->velocity[i] - change;
			if (out[i] > -PROX_STOP_EPSILON && out[i] < PROX_STOP_EPSILON)
				out[i] = 0;
		}

		if (out[2] > 60)
			return;

		movetype = MOVETYPE_BOUNCE;

		// if we're here, we're going to stop on an entity
		if (stick_ok)
		{ // it's a happy entity
			ent->velocity = {};
			ent->avelocity = {};
		}
		else // no-stick.  teflon time
		{
			if (tr.plane.normal[2] > 0.7f)
			{
				Prox_Explode(ent);
				return;
			}
			return;
		}
	}
	else if (other->s.modelindex != MODELINDEX_WORLD)
		return;

	dir = vectoangles(tr.plane.normal);
	AngleVectors(dir, forward, right, up);

	if (gi.pointcontents(ent->s.origin) & (CONTENTS_LAVA | CONTENTS_SLIME))
	{
		Prox_Explode(ent);
		return;
	}

	ent->svflags &= ~SVF_PROJECTILE;

	field = G_Spawn();

	field->s.origin = ent->s.origin;
	field->mins = {-PROX_BOUND_SIZE(), -PROX_BOUND_SIZE(), -PROX_BOUND_SIZE()};
	field->maxs = {PROX_BOUND_SIZE(), PROX_BOUND_SIZE(), PROX_BOUND_SIZE()};
	field->movetype = MOVETYPE_NONE;
	field->solid = SOLID_TRIGGER;
	field->owner = ent;
	field->classname = "prox_field";
	field->teammaster = ent;
	gi.linkentity(field);

	ent->velocity = {};
	ent->avelocity = {};
	// rotate to vertical
	dir[PITCH] = dir[PITCH] + 90;
	ent->s.angles = dir;
	ent->takedamage = true;
	ent->movetype = movetype; // either bounce or none, depending on whether we stuck to something
	ent->die = prox_die;
	ent->teamchain = field;
	ent->health = PROX_HEALTH();
	ent->nextthink = level.time;
	ent->think = prox_open;
	ent->touch = nullptr;
	ent->solid = SOLID_BBOX;

	gi.linkentity(ent);
}

THINK(Prox_Think)(edict_t *self)->void
{
	if (self->timestamp <= level.time)
	{
		Prox_Explode(self);
		return;
	}

	self->s.angles = vectoangles(self->velocity.normalized());
	self->s.angles[PITCH] -= 90;
	self->nextthink = level.time;
}

//===============
//===============
void fire_prox(edict_t *self, const vec3_t &start, const vec3_t &aimdir, int prox_damage_multiplier, int speed)
{
	// --- Player and Limit Check ---
	if (!self || !self->client) {
		return; // Cannot fire if not owned by a player
	}

	// O(1) PERFORMANCE: If the player is at their prox limit, remove the oldest one.
	if (self->client->resp.num_proxs >= ProxConstants::MAX_PROXS_PER_PLAYER())
	{
		// Get the oldest prox from our circular buffer.
		edict_t* oldest = self->client->resp.deployed_proxs[self->client->resp.oldest_prox_idx];

		// Ensure it's a valid, in-use prox before touching it. This handles cases
		// where the prox was destroyed by other means and the pointer is stale.
		if (oldest && oldest->inuse && horde::IsSpecialType(oldest, horde::SpecialEntityTypeID::PROX_MINE))
		{
			// --- FIX ---
			// Don't use G_FreeEdict(oldest) as it orphans the trigger field.
			// Instead, trigger its explosion sequence, which handles all cleanup.
			// Use quiet removal effect for oldest prox auto-replacement
			g_use_quiet_deployable_removal = true;
			oldest->think = Prox_Explode;
			oldest->nextthink = level.time;
			g_use_quiet_deployable_removal = false;
		}
	}
	// --- END FIX ---

	edict_t *prox;
	vec3_t dir;
	vec3_t forward, right, up;

	dir = vectoangles(aimdir);
	AngleVectors(dir, forward, right, up);

	prox = G_Spawn();
	prox->s.origin = start;
	prox->velocity = aimdir * speed;

	float const gravityAdjustment = level.gravity / 800.f;

	prox->velocity += up * (200 + crandom() * 10.0f) * gravityAdjustment;
	prox->velocity += right * (crandom() * 10.0f);

	prox->s.angles = dir;
	prox->s.angles[PITCH] -= 90;
	prox->movetype = MOVETYPE_BOUNCE;
	prox->solid = SOLID_BBOX;
	prox->svflags |= SVF_PROJECTILE;
	prox->s.effects |= EF_GRENADE;
	prox->flags |= (FL_DODGE | FL_TRAP);
	prox->clipmask = MASK_PROJECTILE | CONTENTS_LAVA | CONTENTS_SLIME;

	if (self && self->client && !G_ShouldPlayersCollide(true))
		prox->clipmask &= ~CONTENTS_PLAYER;

	prox->s.renderfx |= RF_IR_VISIBLE;
	prox->mins = {-6, -6, -6};
	prox->maxs = {6, 6, 6};
	prox->s.modelindex = gi.modelindex("models/weapons/g_prox/tris.md2");
	prox->owner = self;
	prox->teammaster = self;

	// Store attacker info in case owner dies before projectile hits
	SetProjectileAttackerInfo(prox, self);

	prox->touch = prox_land;
	prox->think = Prox_Think;
	prox->nextthink = level.time;
	prox->classname = "prox_mine";
	prox->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::PROX_MINE);
	prox->flags |= FL_DAMAGEABLE;
	prox->flags |= FL_MECHANICAL;

	// --- DAMAGE & RADIUS CLAMPING ---
	int const effective_multiplier = std::min(prox_damage_multiplier, 3);
	prox->dmg = PROX_DAMAGE() * effective_multiplier;
	prox->dmg_radius = PROX_DAMAGE_RADIUS() * (1.0f + 0.5f * (effective_multiplier - 1));
	// --- END CLAMPING LOGIC ---

	prox->timestamp = level.time + PROX_TIME_TO_LIVE();

	gi.linkentity(prox);

	// --- NEW: Add to Player's Tracking Array ---
	// Track the newly deployed prox by overwriting the oldest slot.
	self->client->resp.deployed_proxs[self->client->resp.oldest_prox_idx] = prox;
	
	// Advance the index for the next "oldest".
	self->client->resp.oldest_prox_idx = (self->client->resp.oldest_prox_idx + 1) % ProxConstants::MAX_PROXS_PER_PLAYER();

	// Increment the counter of active proxs.
	self->client->resp.num_proxs++;
	// --- END NEW LOGIC ---
}

// *************************
// MELEE WEAPONS
// *************************

struct player_melee_data_t
{
	edict_t *self;
	const vec3_t &start;
	const vec3_t &aim;
	int reach;
};

static BoxEdictsResult_t fire_player_melee_BoxFilter(edict_t *check, void *data_v)
{
	const player_melee_data_t *data = (const player_melee_data_t *)data_v;

	if (!check->inuse || !check->takedamage || check == data->self)
		return BoxEdictsResult_t::Skip;

	// check distance
	vec3_t const closest_point_to_check = closest_point_to_box(data->start, check->s.origin + check->mins, check->s.origin + check->maxs);
	vec3_t const closest_point_to_self = closest_point_to_box(closest_point_to_check, data->self->s.origin + data->self->mins, data->self->s.origin + data->self->maxs);

	vec3_t dir = (closest_point_to_check - closest_point_to_self);
	float const len = dir.normalize();

	if (len > data->reach)
		return BoxEdictsResult_t::Skip;

	// check angle if we aren't intersecting
	vec3_t const shrink{2, 2, 2};
	if (!boxes_intersect(check->absmin + shrink, check->absmax - shrink, data->self->absmin + shrink, data->self->absmax - shrink))
	{
		dir = (((check->absmin + check->absmax) / 2) - data->start).normalized();

		if (dir.dot(data->aim) < 0.70f)
			return BoxEdictsResult_t::Skip;
	}

	return BoxEdictsResult_t::Keep;
}

bool fire_player_melee(edict_t *self, const vec3_t &start, const vec3_t &aim, int reach, int damage, int kick, mod_t mod)
{
	constexpr size_t MAX_HIT = 4;

	vec3_t const reach_vec{float(reach - 1), float(reach - 1), float(reach - 1)};
	edict_t *targets[MAX_HIT];

	player_melee_data_t data{
		self,
		start,
		aim,
		reach};

	// find all the things we could maybe hit
	size_t const num = gi.BoxEdicts(self->absmin - reach_vec, self->absmax + reach_vec, targets, q_countof(targets), AREA_SOLID, fire_player_melee_BoxFilter, &data);

	if (!num)
		return false;

	bool was_hit = false;

	for (size_t i = 0; i < num; i++)
	{
		edict_t *hit = targets[i];

		if (!hit->inuse || !hit->takedamage)
			continue;
		else if (!CanDamage(self, hit))
			continue;

		// do the damage
		vec3_t const closest_point_to_check = closest_point_to_box(start, hit->s.origin + hit->mins, hit->s.origin + hit->maxs);

		if (hit->svflags & SVF_MONSTER)
			hit->pain_debounce_time -= random_time(5_ms, 75_ms);

		if (mod.id == MOD_CHAINFIST)
			T_Damage(hit, self, self, aim, closest_point_to_check, -aim, damage, kick / 2,
					 DAMAGE_DESTROY_ARMOR | DAMAGE_NO_KNOCKBACK, mod);
		else
			T_Damage(hit, self, self, aim, closest_point_to_check, -aim, damage, kick / 2, DAMAGE_NO_KNOCKBACK, mod);

		was_hit = true;
	}

	return was_hit;
}

// *************************
// NUKE
// *************************

constexpr gtime_t NUKE_DELAY = 4_sec;
constexpr gtime_t NUKE_TIME_TO_LIVE = 6_sec;
constexpr float NUKE_RADIUS = 1024;
constexpr int32_t NUKE_DAMAGE = 800;
constexpr gtime_t NUKE_QUAKE_TIME = 3_sec;
constexpr float NUKE_QUAKE_STRENGTH = 100;

THINK(Nuke_Quake)(edict_t *self)->void
{
	if (self->last_move_time < level.time)
	{
		gi.positioned_sound(self->s.origin, self, CHAN_AUTO, self->noise_index, 0.75, ATTN_NONE, 0);
		self->last_move_time = level.time + 500_ms;
	}

	for (uint32_t i = 0; i < game.maxclients; ++i)
	{
		edict_t *player = &g_edicts[1 + i];
		if (!player->inuse || !player->client || !player->groundentity)
		{
			continue;
		}

		player->groundentity = nullptr;
		player->velocity[0] += crandom() * 150;
		player->velocity[1] += crandom() * 150;
		player->velocity[2] = self->speed * (100.0f / player->mass);
	}

	if (level.time < self->timestamp)
		self->nextthink = level.time + FRAME_TIME_S;
	else
		G_FreeEdict(self);
}

static void Nuke_Explode(edict_t *ent)
{

	if (ent->teammaster && ent->teammaster->client)
		PlayerNoise(ent->teammaster, ent->s.origin, PNOISE_IMPACT);

	T_RadiusNukeDamage(ent, ent->teammaster, (float)ent->dmg, ent, ent->dmg_radius, MOD_NUKE);

	edict_t *check = nullptr;
	float radius = ent->dmg_radius * 1.5f; // Un poco más de radio que el daño para estar seguros

	while ((check = findradius(check, ent->s.origin, radius)) != nullptr)
	{
		if (!check->client || !check->inuse)
			continue;

		// Marcar protección contra daño de caída
		check->client->landmark_free_fall = true;
	}

	if (ent->dmg > NUKE_DAMAGE)
		gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);

	gi.sound(ent, CHAN_NO_PHS_ADD | CHAN_VOICE, gi.soundindex("weapons/grenlx1a.wav"), 1, ATTN_NONE, 0);

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_EXPLOSION1_BIG);
	gi.WritePosition(ent->s.origin);
	gi.multicast(ent->s.origin, MULTICAST_PHS, false);

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_NUKEBLAST);
	gi.WritePosition(ent->s.origin);
	gi.multicast(ent->s.origin, MULTICAST_ALL, false);

	// become a quake
	ent->svflags |= SVF_NOCLIENT;
	ent->noise_index = gi.soundindex("world/rumble.wav");
	ent->think = Nuke_Quake;
	ent->speed = NUKE_QUAKE_STRENGTH;
	ent->timestamp = level.time + NUKE_QUAKE_TIME;
	ent->nextthink = level.time + FRAME_TIME_S;
	ent->last_move_time = 0_ms;
}

DIE(nuke_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t &point, const mod_t &mod)->void
{
	self->takedamage = false;
	if ((attacker) && !(strcmp(attacker->classname, "nuke")))
	{
		G_FreeEdict(self);
		return;
	}
	Nuke_Explode(self);
}

THINK(Nuke_Think)(edict_t *ent)->void
{
	float attenuation;
	float constexpr default_atten = 1.8f;
	int nuke_damage_multiplier;
	player_muzzle_t muzzleflash;

	nuke_damage_multiplier = ent->dmg / NUKE_DAMAGE;
	switch (nuke_damage_multiplier)
	{
	case 1:
		attenuation = default_atten / 1.4f;
		muzzleflash = MZ_NUKE1;
		break;
	case 2:
		attenuation = default_atten / 2.0f;
		muzzleflash = MZ_NUKE2;
		break;
	case 4:
		attenuation = default_atten / 3.0f;
		muzzleflash = MZ_NUKE4;
		break;
	case 8:
		attenuation = default_atten / 5.0f;
		muzzleflash = MZ_NUKE8;
		break;
	default:
		attenuation = default_atten;
		muzzleflash = MZ_NUKE1;
		break;
	}

	if (ent->wait < level.time.seconds())
		Nuke_Explode(ent);
	else if (level.time >= (gtime_t::from_sec(ent->wait) - NUKE_TIME_TO_LIVE))
	{
		ent->s.frame++;

		if (ent->s.frame > 11)
			ent->s.frame = 6;

		if (gi.pointcontents(ent->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA))
		{
			Nuke_Explode(ent);
			return;
		}

		ent->think = Nuke_Think;
		ent->nextthink = level.time + 10_hz;
		ent->health = 1;
		ent->owner = nullptr;

		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(muzzleflash);
		gi.multicast(ent->s.origin, MULTICAST_PHS, false);

		if (ent->timestamp <= level.time)
		{
			if ((gtime_t::from_sec(ent->wait) - level.time) <= (NUKE_TIME_TO_LIVE / 2.0f))
			{
				gi.sound(ent, CHAN_NO_PHS_ADD | CHAN_VOICE, gi.soundindex("weapons/nukewarn2.wav"), 1, attenuation, 0);
				ent->timestamp = level.time + 300_ms;
			}
			else
			{
				gi.sound(ent, CHAN_NO_PHS_ADD | CHAN_VOICE, gi.soundindex("weapons/nukewarn2.wav"), 1, attenuation, 0);
				ent->timestamp = level.time + 500_ms;
			}
		}
	}
	else
	{
		if (ent->timestamp <= level.time)
		{
			gi.sound(ent, CHAN_NO_PHS_ADD | CHAN_VOICE, gi.soundindex("weapons/nukewarn2.wav"), 1, attenuation, 0);
			ent->timestamp = level.time + 1_sec;
		}
		ent->nextthink = level.time + FRAME_TIME_S;
	}
}

TOUCH(nuke_bounce)(edict_t *ent, edict_t *other, const trace_t &tr, bool other_touching_self)->void
{
	if (tr.surface && tr.surface->id)
	{
		if (frandom() > 0.5f)
			gi.sound(ent, CHAN_BODY, gi.soundindex("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
		else
			gi.sound(ent, CHAN_BODY, gi.soundindex("weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
	}
}

void fire_nuke(edict_t *self, const vec3_t &start, const vec3_t &aimdir, int speed)
{
	edict_t *nuke;
	vec3_t dir;
	vec3_t forward, right, up;
	int const damage_modifier = P_DamageModifier(self);

	dir = vectoangles(aimdir);
	AngleVectors(dir, forward, right, up);

	nuke = G_Spawn();
	nuke->s.origin = start;
	nuke->velocity = aimdir * speed;
	nuke->velocity += up * (200 + crandom() * 10.0f);
	nuke->velocity += right * (crandom() * 10.0f);
	nuke->movetype = MOVETYPE_BOUNCE;
	nuke->clipmask = MASK_PROJECTILE;
	nuke->solid = SOLID_BBOX;
	nuke->s.effects |= EF_GRENADE;
	nuke->s.renderfx |= RF_IR_VISIBLE;
	nuke->mins = {-8, -8, 0};
	nuke->maxs = {8, 8, 16};
	nuke->s.modelindex = gi.modelindex("models/weapons/g_nuke/tris.md2");
	nuke->owner = self;
	nuke->teammaster = self;
	if (g_horde->integer)
		nuke->ctf_team = GetEntityTeam(self);
	nuke->nextthink = level.time + FRAME_TIME_S;
	nuke->wait = (level.time + NUKE_DELAY + NUKE_TIME_TO_LIVE).seconds();
	nuke->think = Nuke_Think;
	nuke->touch = nuke_bounce;

	nuke->health = 10000;
	nuke->takedamage = true;
	nuke->flags |= FL_DAMAGEABLE;
	nuke->dmg = NUKE_DAMAGE * damage_modifier;
	if (damage_modifier == 1)
		nuke->dmg_radius = NUKE_RADIUS;
	else
		nuke->dmg_radius = NUKE_RADIUS + NUKE_RADIUS * (0.25f * damage_modifier);
	// this yields 1.0, 1.5, 2.0, 3.0 times radius

	nuke->classname = "nuke";
	nuke->special_type_id = static_cast<uint8_t>(horde::SpecialTypeRegistry::GetTypeID(nuke->classname));
	nuke->die = nuke_die;
 
	gi.linkentity(nuke);
}

// *************************
//  HEATBEAM
// *************************

struct heatbeam_pierce_t : pierce_args_t
{
	edict_t *self;
	edict_t *attacker; // Track the actual attacker (may be different from self->owner)
	vec3_t aimdir;
	int damage;
	int kick;
	mod_t mod;
	bool water_hit;
	static constexpr gtime_t HIT_DELAY = 100_ms;

	heatbeam_pierce_t(edict_t *self, const vec3_t &aimdir, int damage, int kick, mod_t mod) : self(self), aimdir(aimdir), damage(damage), kick(kick), mod(mod), water_hit(false)
	{
		// Determine the actual attacker using helper
		attacker = GetRealAttacker(self);
	}

	bool hit(contents_t &mask, vec3_t &end) override
	{
		if (tr.contents & MASK_WATER)
		{
			water_hit = true;
			mask &= ~MASK_WATER;

			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_HEATBEAM_SPARKS);
			gi.WritePosition(tr.endpos);
			gi.WriteDir(tr.plane.normal);
			gi.multicast(tr.endpos, MULTICAST_PVS, false);

			return true;
		}

		if (tr.ent && tr.ent->takedamage)
		{
			if (!tr.ent->beam_hit_time || level.time >= tr.ent->beam_hit_time + HIT_DELAY)
			{
				int current_damage = water_hit ? damage / 2 : damage;

				// Use attacker instead of self->owner for damage credit
				T_Damage(tr.ent, self, attacker, aimdir, tr.endpos, vec3_origin,
						 current_damage, kick, DAMAGE_ENERGY, mod);

				// Apply plasmabeam burn damage if upgrade is active
				if (self->client && self->client->pers.skills.pb_burn > 0)
				{
					// Burn damage: 1 damage per tick per level (10 damage per tick at level 10)
					int burn_damage = 1 * self->client->pers.skills.pb_burn;
					apply_burning(tr.ent, attacker, burn_damage, 10_sec);
				}

				tr.ent->beam_hit_time = level.time;
				damage *= 0.98f;

				gi.WriteByte(svc_temp_entity);
				gi.WriteByte(TE_HEATBEAM_STEAM);
				gi.WritePosition(tr.endpos);
				gi.WriteDir(tr.plane.normal);
				gi.multicast(tr.endpos, MULTICAST_PVS, false);
			}

			return mark(tr.ent);
		}

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_HEATBEAM_STEAM);
		gi.WritePosition(tr.endpos);
		gi.WriteDir(tr.plane.normal);
		gi.multicast(tr.endpos, MULTICAST_PVS, false);

		return false;
	}
};

static void fire_beams(edict_t *self, const vec3_t &start, const vec3_t &aimdir, const vec3_t &offset,
					   int damage, int kick, int te_beam, int te_impact, mod_t mod)
{
	// Use MOD_TURRET for sentry gun beams to avoid strength tech doubling damage
	if (self && horde::IsMonsterType(self, horde::MonsterTypeID::SENTRYGUN))
		mod = MOD_TURRET;

	// Apply plasmabeam damage upgrade: +1 per level
	if (self && self->client)
	{
		damage += self->client->pers.skills.pb_damage;
	}

	// Calculate pierce chance: 100% if benefit active, otherwise 4% per level (max 40% at level 10)
	float pierce_chance = 0.0f;
	if (self && self->client)
	{
		if (ClassicPlayerHasBenefitPiercingPlasma(self))
		{
			pierce_chance = 1.0f; // Benefit gives 100% pierce
		}
		else if (self->client->pers.skills.pb_pierce > 0)
		{
			pierce_chance = self->client->pers.skills.pb_pierce * 0.04f; // 4% per level
		}
	}

	// Roll for pierce
	if (pierce_chance > 0.0f && frandom() < pierce_chance)
	{
		vec3_t end = start + (aimdir * 8192);
		contents_t content_mask = MASK_PROJECTILE | MASK_WATER;

		if (self && self->client && !G_ShouldPlayersCollide(true))
			content_mask &= ~CONTENTS_PLAYER;

		heatbeam_pierce_t pierce(self, aimdir, damage, kick, mod);
		pierce_trace(start, end, self, pierce, content_mask);

		// Draw beam effect
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(te_beam);
		gi.WriteEntity(self);
		gi.WritePosition(start);
		gi.WritePosition(pierce.tr.endpos);
		gi.multicast(self->s.origin, MULTICAST_ALL, false);

		if (pierce.water_hit)
		{
			vec3_t pos = pierce.tr.endpos + (aimdir * -2);

			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_BUBBLETRAIL2);
			gi.WritePosition(start);
			gi.WritePosition(pierce.tr.endpos);
			gi.multicast(pos, MULTICAST_PVS, false);
		}
	}
	else
	{
		trace_t tr;
		vec3_t dir;
		vec3_t forward, right, up;
		vec3_t end;
		vec3_t water_start, endpoint;
		bool water = false, underwater = false;
		contents_t content_mask = MASK_PROJECTILE | MASK_WATER;

		// Determine the real attacker
		edict_t *attacker = GetRealAttacker(self);

		if (self && self->client && !G_ShouldPlayersCollide(true))
			content_mask &= ~CONTENTS_PLAYER;

		vec3_t beam_endpt;

		dir = vectoangles(aimdir);
		AngleVectors(dir, forward, right, up);
		end = start + (forward * 8192);

		if (gi.pointcontents(start) & MASK_WATER)
		{
			underwater = true;
			water_start = start;
			content_mask &= ~MASK_WATER;
		}

		tr = gi.traceline(start, end, self, content_mask);

		if (tr.contents & MASK_WATER)
		{
			water = true;
			water_start = tr.endpos;

			if (start != tr.endpos)
			{
				gi.WriteByte(svc_temp_entity);
				gi.WriteByte(TE_HEATBEAM_SPARKS);
				gi.WritePosition(water_start);
				gi.WriteDir(tr.plane.normal);
				gi.multicast(tr.endpos, MULTICAST_PVS, false);
			}

			tr = gi.traceline(water_start, end, self, content_mask & ~MASK_WATER);
		}

		endpoint = tr.endpos;

		if (water)
			damage = damage / 2;

		if (!((tr.surface) && (tr.surface->flags & SURF_SKY)))
		{
			if (tr.fraction < 1.0f)
			{
				if (tr.ent->takedamage)
				{
					// Use attacker instead of self for damage credit
					T_Damage(tr.ent, self, attacker, aimdir, tr.endpos, tr.plane.normal,
							 damage, kick, DAMAGE_ENERGY, mod);

					// Apply plasmabeam burn damage if upgrade is active
					if (self->client && self->client->pers.skills.pb_burn > 0)
					{
						// Burn damage: 1 damage per tick per level (10 damage per tick at level 10)
						int burn_damage = 1 * self->client->pers.skills.pb_burn;
						apply_burning(tr.ent, attacker, burn_damage, 10_sec);
					}
				}
				else
				{
					if ((!water) && !(tr.surface && (tr.surface->flags & SURF_SKY)))
					{
						gi.WriteByte(svc_temp_entity);
						gi.WriteByte(TE_HEATBEAM_STEAM);
						gi.WritePosition(tr.endpos);
						gi.WriteDir(tr.plane.normal);
						gi.multicast(tr.endpos, MULTICAST_PVS, false);

						if (self->client)
							PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
					}
				}
			}
		}

		if ((water) || (underwater))
		{
			vec3_t pos;
			dir = tr.endpos - water_start;
			dir.normalize();
			pos = tr.endpos + (dir * -2);

			if (gi.pointcontents(pos) & MASK_WATER)
				tr.endpos = pos;
			else
				tr = gi.traceline(pos, water_start, tr.ent != world ? tr.ent : nullptr, MASK_WATER);

			pos = water_start + tr.endpos;
			pos *= 0.5f;

			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_BUBBLETRAIL2);
			gi.WritePosition(water_start);
			gi.WritePosition(tr.endpos);
			gi.multicast(pos, MULTICAST_PVS, false);
		}

		beam_endpt = (!underwater && !water) ? tr.endpos : endpoint;

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(te_beam);
		gi.WriteEntity(self);
		gi.WritePosition(start);
		gi.WritePosition(beam_endpt);
		gi.multicast(self->s.origin, MULTICAST_ALL, false);
	}
}

/*
=================
fire_heat

Fires a single heat beam.  Zap.
=================
*/
void fire_heatbeam(edict_t *self, const vec3_t &start, const vec3_t &aimdir, const vec3_t &offset, int damage, int kick, bool monster)
{
	if (monster)
		fire_beams(self, start, aimdir, offset, damage, kick, TE_MONSTER_HEATBEAM, TE_HEATBEAM_SPARKS, MOD_HEATBEAM);
	else
		fire_beams(self, start, aimdir, offset, damage, kick, TE_HEATBEAM, TE_HEATBEAM_SPARKS, MOD_HEATBEAM);
}

/*
=================
fire_blaster2

Fires a single green blaster bolt.  Used by monsters, generally.
=================
*/
TOUCH(blaster2_touch)(edict_t *self, edict_t *other, const trace_t &tr, bool other_touching_self)->void
{
	mod_t mod;

	if (other == self->owner)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner && self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		// Determine real attacker (handles sentry guns, doppelgangers, etc.)
		edict_t* real_attacker = GetRealAttacker(self);

		// Determine the means of death based on the original owner (the sphere)
		if (self->owner && self->owner->classname && strcmp(self->owner->classname, "sphere") == 0)
			mod = MOD_DEFENDER_SPHERE;
		else
			mod = MOD_BLASTER2;

		// Apply damage using the 'real_attacker' so the player gets the credit.
		if (self->dmg >= 5)
		{
			T_RadiusDamage(self, real_attacker, static_cast<float>(self->dmg * 2), other, self->dmg_radius, DAMAGE_ENERGY, MOD_UNKNOWN);
		}
		T_Damage(other, self, real_attacker, self->velocity, self->s.origin, tr.plane.normal, self->dmg, 1, DAMAGE_ENERGY, mod);
	}
	else
	{
		// Determine real attacker for radius damage
		edict_t* real_attacker = GetRealAttacker(self);

		if (self->dmg >= 5)
		{
			T_RadiusDamage(self, real_attacker, static_cast<float>(self->dmg * 2), nullptr, self->dmg_radius, DAMAGE_ENERGY, MOD_UNKNOWN);
		}

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER2);
		gi.WritePosition(self->s.origin);
		gi.WriteDir(tr.plane.normal);
		gi.multicast(self->s.origin, MULTICAST_PHS, false);
	}

	G_FreeEdict(self);
}

void fire_blaster2(edict_t *self, const vec3_t &start, const vec3_t &dir, int damage, int speed, effects_t effect, bool hyper)
{
	edict_t *bolt;
	trace_t tr;

	bolt = G_Spawn();
	bolt->s.origin = start;
	bolt->s.old_origin = start;
	bolt->s.angles = vectoangles(dir);
	bolt->velocity = dir * speed;
	bolt->svflags |= SVF_PROJECTILE;
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_PROJECTILE;
	bolt->flags |= FL_DODGE;

	// [Paril-KEX]
	if (self && self->client && !G_ShouldPlayersCollide(true))
		bolt->clipmask &= ~CONTENTS_PLAYER;

	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	if (effect)
		bolt->s.effects |= EF_TRACKER;
	bolt->dmg_radius = 128;
	bolt->s.modelindex = gi.modelindex("models/objects/laser/tris.md2");
	bolt->s.skinnum = 2;
	bolt->s.scale = 2.5f;
	if (self->client && (self->client->pers.weapon->id == IT_WEAPON_MACHINEGUN || self->client->pers.weapon->id == IT_WEAPON_CHAINGUN))
	{
		bolt->s.scale = 1.0f;
	}
	bolt->touch = blaster2_touch;

	bolt->owner = self;

	// Store attacker info in case owner dies before projectile hits
	SetProjectileAttackerInfo(bolt, self);

	bolt->nextthink = level.time + 2_sec;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	gi.linkentity(bolt);

	tr = gi.traceline(self->s.origin, bolt->s.origin, bolt, bolt->clipmask);
	// [Horde] startsolid: player overlapping a damageable entity -> hit it at spawn
	if (tr.fraction < 1.0f || (tr.startsolid && self->client && tr.ent && tr.ent->takedamage))
	{
		bolt->s.origin = tr.endpos + (tr.plane.normal * 1.f);
		bolt->touch(bolt, tr.ent, tr, false);
	}
}

// *************************
// tracker
// *************************

constexpr damageflags_t TRACKER_DAMAGE_FLAGS = (DAMAGE_NO_POWER_ARMOR | DAMAGE_ENERGY | DAMAGE_NO_KNOCKBACK);
constexpr damageflags_t TRACKER_IMPACT_FLAGS = (DAMAGE_NO_POWER_ARMOR | DAMAGE_ENERGY);

constexpr gtime_t TRACKER_DAMAGE_TIME = 500_ms;

THINK(tracker_pain_daemon_think)(edict_t *self)->void
{
	constexpr vec3_t pain_normal = {0, 0, 1};
	int hurt;

	if (!self->inuse)
		return;

	// --- FIX: Add this check ---
	// The enemy entity might have been freed since this daemon was spawned.
	// If so, the self->enemy pointer is "dangling" and unsafe to use.
	// We must check if the enemy is still valid before accessing its members.
	if (!self->enemy || !self->enemy->inuse)
	{
		G_FreeEdict(self);
		return;
	}
	// --- END FIX ---

	if ((level.time - self->timestamp) > TRACKER_DAMAGE_TIME)
	{
		if (!self->enemy->client)
			self->enemy->s.effects &= ~EF_TRACKERTRAIL;
		G_FreeEdict(self);
	}
	else
	{
		if (self->enemy->health > 0)
		{
			vec3_t const center = (self->enemy->absmax + self->enemy->absmin) * 0.5f;

			T_Damage(self->enemy, self, self->owner, vec3_origin, center, pain_normal,
					 self->dmg, 0, TRACKER_DAMAGE_FLAGS, MOD_TRACKER);

			// if we kill the player, we'll be removed.
			if (self->inuse)
			{
				// if we killed a monster, gib them.
				if (self->enemy->health < 1)
				{
					if (self->enemy->gib_health)
						hurt = -self->enemy->gib_health;
					else
						hurt = 500;

					T_Damage(self->enemy, self, self->owner, vec3_origin, center,
							 pain_normal, hurt, 0, TRACKER_DAMAGE_FLAGS, MOD_TRACKER);
				}

				self->nextthink = level.time + 10_hz;

				if (self->enemy->client)
					self->enemy->client->tracker_pain_time = self->nextthink;
				else
					self->enemy->s.effects |= EF_TRACKERTRAIL;
			}
		}
		else
		{
			if (!self->enemy->client)
				self->enemy->s.effects &= ~EF_TRACKERTRAIL;
			G_FreeEdict(self);
		}
	}
}

void tracker_pain_daemon_spawn(edict_t *owner, edict_t *enemy, int damage)
{
	edict_t *daemon;

	if (enemy == nullptr)
		return;

	daemon = G_Spawn();
	daemon->classname = "pain daemon";
	daemon->think = tracker_pain_daemon_think;
	daemon->nextthink = level.time;
	daemon->timestamp = level.time;
	daemon->owner = owner;
	daemon->enemy = enemy;
	daemon->dmg = damage;
}

void tracker_explode(edict_t *self)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_TRACKER_EXPLOSION);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PHS, false);

	G_FreeEdict(self);
}

TOUCH(tracker_touch)(edict_t *self, edict_t *other, const trace_t &tr, bool other_touching_self)->void
{
	float damagetime;

	if (other == self->owner)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner && self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if ((other->svflags & SVF_MONSTER) || other->client)
		{
			if (other->health > 0) // knockback only for living creatures
			{
				// PMM - kickback was times 4 .. reduced to 3
				// now this does no damage, just knockback
				T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal,
						 /* self->dmg */ 0, (self->dmg * 3), TRACKER_IMPACT_FLAGS, MOD_TRACKER);

				if (!(other->flags & (FL_FLY | FL_SWIM)))
					other->velocity[2] += 140;

				damagetime = ((float)self->dmg) * 0.1f;
				damagetime = damagetime / TRACKER_DAMAGE_TIME.seconds();

				tracker_pain_daemon_spawn(self->owner, other, (int)damagetime);
			}
			else // lots of damage (almost autogib) for dead bodies
			{
				T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal,
						 self->dmg * 4, (self->dmg * 3), TRACKER_IMPACT_FLAGS, MOD_TRACKER);
			}
		}
		else // full damage in one shot for inanimate objects
		{
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal,
					 self->dmg, (self->dmg * 3), TRACKER_IMPACT_FLAGS, MOD_TRACKER);
		}
	}

	tracker_explode(self);
	return;
}

THINK(tracker_fly)(edict_t *self)->void
{
	vec3_t dest;
	vec3_t dir;
	vec3_t center;

	if (!M_HasValidTarget(self))
	{
		tracker_explode(self);
		return;
	}

	// PMM - try to hunt for center of enemy, if possible and not client
	if (self->enemy->client)
	{
		dest = self->enemy->s.origin;
		dest[2] += self->enemy->viewheight;
	}
	// paranoia
	else if (!self->enemy->absmin || !self->enemy->absmax)
	{
		dest = self->enemy->s.origin;
	}
	else
	{
		center = (self->enemy->absmin + self->enemy->absmax) * 0.5f;
		dest = center;
	}

	dir = dest - self->s.origin;
	dir.normalize();
	self->s.angles = vectoangles(dir);
	self->velocity = dir * self->speed;
	self->monsterinfo.saved_goal = dest;

	self->nextthink = level.time + 10_hz;
}

void fire_tracker(edict_t *self, const vec3_t &start, const vec3_t &dir, int damage, int speed, edict_t *enemy)
{
	edict_t *bolt;
	trace_t tr;

	bolt = G_Spawn();
	bolt->s.origin = start;
	bolt->s.old_origin = start;
	bolt->s.angles = vectoangles(dir);
	bolt->velocity = dir * speed;
	bolt->svflags |= SVF_PROJECTILE;
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_PROJECTILE;

	// [Paril-KEX]
	if (self && self->client && !G_ShouldPlayersCollide(true))
		bolt->clipmask &= ~CONTENTS_PLAYER;

	bolt->solid = SOLID_BBOX;
	bolt->speed = (float)speed;
	bolt->s.effects = EF_TRACKER;
	bolt->s.sound = gi.soundindex("weapons/disrupt.wav");
	bolt->s.modelindex = gi.modelindex("models/proj/disintegrator/tris.md2");
	bolt->touch = tracker_touch;
	bolt->enemy = enemy;
	bolt->owner = self;

	// Store attacker info in case owner dies before projectile hits
	SetProjectileAttackerInfo(bolt, self);

	bolt->dmg = damage;
	bolt->classname = "tracker";
	gi.linkentity(bolt);

	if (enemy)
	{
		bolt->nextthink = level.time + 10_hz;
		bolt->think = tracker_fly;
	}
	else
	{
		bolt->nextthink = level.time + 7_sec; // reduce?
		bolt->think = G_FreeEdict;
	}

	tr = gi.traceline(self->s.origin, bolt->s.origin, bolt, bolt->clipmask);
	// [Horde] startsolid: player overlapping a damageable entity -> hit it at spawn
	if (tr.fraction < 1.0f || (tr.startsolid && self->client && tr.ent && tr.ent->takedamage))
	{
		bolt->s.origin = tr.endpos + (tr.plane.normal * 1.f);
		bolt->touch(bolt, tr.ent, tr, false);
	}
}
