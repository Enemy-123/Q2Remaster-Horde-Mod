// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"
/*
================ =
fire_hit

Used for all impact(hit / punch / slash) attacks
================ =
*/
bool fire_hit(edict_t* self, vec3_t aim, int damage, int kick)
{
	trace_t tr;
	vec3_t	forward, right, up;
	vec3_t	v;
	vec3_t	point;
	float	range;
	vec3_t	dir;
	//	char buffer[256];

		// Verificación inicial de null para enemy
	if (!self->enemy) {
		return false; // Manejar el error apropiadamente
	}
	//// Verificación de null para attacker si es "monster_sentrygun"
	//if (self->enemy && self->enemy->classname && !strcmp(self->enemy->classname, "monster_sentrygun")) {
	//	//std::snprintf(buffer, sizeof(buffer), "Error: attacker is monster_sentrygun\n");
	//	//gi.Com_Print(buffer);
	//	return false; // Manejar el error apropiadamente
	//}

	// see if enemy is in range
	range = distance_between_boxes(self->enemy->absmin, self->enemy->absmax, self->absmin, self->absmax);
	if (range > aim[0])
		return false;

	if (!(aim[1] > self->mins[0] && aim[1] < self->maxs[0]))
	{
		// this is a side hit so adjust the "right" value out to the edge of their bbox
		if (aim[1] < 0)
			aim[1] = self->enemy->mins[0];
		else
			aim[1] = self->enemy->maxs[0];
	}

	point = closest_point_to_box(self->s.origin, self->enemy->absmin, self->enemy->absmax);

	// check that we can hit the point on the bbox
	tr = gi.traceline(self->s.origin, point, self, MASK_PROJECTILE);

	if (tr.fraction < 1)
	{
		if (!tr.ent->takedamage)
			return false;
		// if it will hit any client/monster then hit the one we wanted to hit
		if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client))
			tr.ent = self->enemy;
	}

	// check that we can hit the player from the point
	tr = gi.traceline(point, self->enemy->s.origin, self, MASK_PROJECTILE);

	if (tr.fraction < 1)
	{
		if (!tr.ent->takedamage)
			return false;
		// if it will hit any client/monster then hit the one we wanted to hit
		if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client))
			tr.ent = self->enemy;
	}

	AngleVectors(self->s.angles, forward, right, up);
	point = self->s.origin + (forward * range);
	point += (right * aim[1]);
	point += (up * aim[2]);
	dir = point - self->enemy->s.origin;

	// do the damage
	T_Damage(tr.ent, self, self, dir, point, vec3_origin, damage, kick / 2, DAMAGE_NO_KNOCKBACK, MOD_HIT);

	if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
		return false;

	// do our special form of knockback here
	v = (self->enemy->absmin + self->enemy->absmax) * 0.5f;
	v -= point;
	v.normalize();
	self->enemy->velocity += v * kick;
	if (self->enemy->velocity[2] > 0)
		self->enemy->groundentity = nullptr;
	return true;
}
// helper routine for piercing traces;
// mask = the input mask for finding what to hit
// you can adjust the mask for the re-trace (for water, etc).
// note that you must take care in your pierce callback to mark
// the entities that are being pierced.
void pierce_trace(const vec3_t& start, const vec3_t& end, edict_t* ignore, pierce_args_t& pierce, contents_t mask)
{
	int	   loop_count = MAX_EDICTS;
	vec3_t own_start, own_end;
	own_start = start;
	own_end = end;

	while (--loop_count)
	{
		pierce.tr = gi.traceline(start, own_end, ignore, mask);

		// didn't hit anything, so we're done
		if (!pierce.tr.ent || pierce.tr.fraction == 1.0f)
			return;

		// hit callback said we're done
		if (!pierce.hit(mask, own_end))
			return;

		own_start = pierce.tr.endpos;
	}

	gi.Com_Print("runaway pierce_trace\n");
}

struct fire_lead_pierce_t : pierce_args_t
{
	edict_t* self;
	vec3_t		 start;
	vec3_t		 aimdir;
	int			 damage;
	int			 kick;
	int			 hspread;
	int			 vspread;
	mod_t		 mod;
	int			 te_impact;
	contents_t   mask;
	bool	     water = false;
	vec3_t	     water_start = {};
	edict_t* chain = nullptr;

	inline fire_lead_pierce_t(edict_t* self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, mod_t mod, int te_impact, contents_t mask) :
		pierce_args_t(),
		self(self),
		start(start),
		aimdir(aimdir),
		damage(damage),
		kick(kick),
		hspread(hspread),
		vspread(vspread),
		mod(mod),
		te_impact(te_impact),
		mask(mask)
	{
	}

	// we hit an entity; return false to stop the piercing.
	// you can adjust the mask for the re-trace (for water, etc).
	bool hit(contents_t& mask, vec3_t& end) override
	{
		// see if we hit water
		if (tr.contents & MASK_WATER)
		{
			int color;

			water = true;
			water_start = tr.endpos;

			// CHECK: is this compare ever true?
			if (te_impact != -1 && start != tr.endpos)
			{
				if (tr.contents & CONTENTS_WATER)
				{
					// FIXME: this effectively does nothing..
					if (strcmp(tr.surface->name, "brwater") == 0)
						color = SPLASH_BROWN_WATER;
					else
						color = SPLASH_BLUE_WATER;
				}
				else if (tr.contents & CONTENTS_SLIME)
					color = SPLASH_SLIME;
				else if (tr.contents & CONTENTS_LAVA)
					color = SPLASH_LAVA;
				else
					color = SPLASH_UNKNOWN;

				if (color != SPLASH_UNKNOWN)
				{
					gi.WriteByte(svc_temp_entity);
					gi.WriteByte(TE_SPLASH);
					gi.WriteByte(8);
					gi.WritePosition(tr.endpos);
					gi.WriteDir(tr.plane.normal);
					gi.WriteByte(color);
					gi.multicast(tr.endpos, MULTICAST_PVS, false);
				}

				// change bullet's course when it enters water
				vec3_t dir, forward, right, up;
				dir = end - start;
				dir = vectoangles(dir);
				AngleVectors(dir, forward, right, up);
				float const r = crandom() * hspread * 2;
				float const u = crandom() * vspread * 2;
				end = water_start + (forward * 8192);
				end += (right * r);
				end += (up * u);
			}

			// re-trace ignoring water this time
			mask &= ~MASK_WATER;
			return true;
		}

		// did we hit an hurtable entity?
		if (tr.ent->takedamage)
		{
			T_Damage(tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, mod.id == MOD_TESLA ? DAMAGE_ENERGY : DAMAGE_BULLET, mod);

			// only deadmonster is pierceable, or actual dead monsters
			// that haven't been made non-solid yet
			if ((tr.ent->svflags & SVF_DEADMONSTER) ||
				(tr.ent->health <= 0 && (tr.ent->svflags & SVF_MONSTER)))
			{
				if (!mark(tr.ent))
					return false;

				return true;
			}
		}
		else
		{
			// send gun puff / flash
			// don't mark the sky
			if (te_impact != -1 && !(tr.surface && ((tr.surface->flags & SURF_SKY) || strncmp(tr.surface->name, "sky", 3) == 0)))
			{
				gi.WriteByte(svc_temp_entity);
				gi.WriteByte(te_impact);
				gi.WritePosition(tr.endpos);
				gi.WriteDir(tr.plane.normal);
				gi.multicast(tr.endpos, MULTICAST_PVS, false);

				if (self->client)
					PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
			}
		}

		// hit a solid, so we're stopping here

		return false;
	}
};

/*
=================
fire_lead

This is an internal support routine used for bullet/pellet based weapons.
=================
*/
static void fire_lead(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int kick, int te_impact, int hspread, int vspread, mod_t mod)
{
	fire_lead_pierce_t args = {
		self,
		start,
		aimdir,
		damage,
		kick,
		hspread,
		vspread,
		mod,
		te_impact,
		MASK_PROJECTILE | MASK_WATER
	};

	// [Paril-KEX]
	if (self->client && !G_ShouldPlayersCollide(true))
		args.mask &= ~CONTENTS_PLAYER;

	// special case: we started in water.
	if (gi.pointcontents(start) & MASK_WATER)
	{
		args.water = true;
		args.water_start = start;
		args.mask &= ~MASK_WATER;
	}

	// check initial firing position
	pierce_trace(self->s.origin, start, self, args, args.mask);

	// we're clear, so do the second pierce
	if (args.tr.fraction == 1.f)
	{
		args.restore();

		vec3_t end, dir, forward, right, up;
		dir = vectoangles(aimdir);
		AngleVectors(dir, forward, right, up);

		float const r = crandom() * hspread;
		float const u = crandom() * vspread;
		end = start + (forward * 8192);
		end += (right * r);
		end += (up * u);

		pierce_trace(args.tr.endpos, end, self, args, args.mask);
	}

	// if went through water, determine where the end is and make a bubble trail
	if (args.water && te_impact != -1)
	{
		vec3_t pos, dir;

		dir = args.tr.endpos - args.water_start;
		dir.normalize();
		pos = args.tr.endpos + (dir * -2);
		if (gi.pointcontents(pos) & MASK_WATER)
			args.tr.endpos = pos;
		else
			args.tr = gi.traceline(pos, args.water_start, args.tr.ent != world ? args.tr.ent : nullptr, MASK_WATER);

		pos = args.water_start + args.tr.endpos;
		pos *= 0.5f;

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BUBBLETRAIL);
		gi.WritePosition(args.water_start);
		gi.WritePosition(args.tr.endpos);
		gi.multicast(pos, MULTICAST_PVS, false);
	}
}

/*
=================
fire_bullet

Fires a single round.  Used for machinegun and chaingun.  Would be fine for
pistols, rifles, etc....
=================
*/
void fire_bullet(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int kick, int hspread, int vspread, mod_t mod)
{
	if (self->svflags & SVF_MONSTER) {
		damage *= M_DamageModifier(self);
	}
	fire_lead(self, start, aimdir, damage, kick, mod.id == MOD_TESLA ? -1 : TE_GUNSHOT, hspread, vspread, mod);
}

/*
=================
fire_shotgun

Shoots shotgun pellets.  Used by shotgun and super shotgun.
=================
*/
void fire_shotgun(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int kick, int hspread, int vspread, int count, mod_t mod)
{
	if (self->svflags & SVF_MONSTER) {
		damage *= M_DamageModifier(self);
	}
	for (int i = 0; i < count; i++)
		fire_lead(self, start, aimdir, damage, kick, TE_SHOTGUN, hspread, vspread, mod);
}

/*
=================
fire_blaster

Fires a single blaster bolt.  Used by the blaster and hyper blaster.
=================
*/

TOUCH(blaster_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (!self || !self->owner) {
		return;
	}
	if (other == self->owner) {
		return;
	}
	if (tr.surface && (tr.surface->flags & SURF_SKY)) {
		G_FreeEdict(self);
		return;
	}
	if (self->owner->client) {
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);
	}
	if (other->takedamage) {
		T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal,
			self->dmg, 1, DAMAGE_ENERGY, MOD_BLASTER);
		G_FreeEdict(self);
	}
	else
	{
		{
			// No bounce, destroy the bolt
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte((self->style != MOD_BLUEBLASTER) ? TE_BLASTER : TE_BLUEHYPERBLASTER);
			gi.WritePosition(self->s.origin);
			gi.WriteDir(tr.plane.normal);
			gi.multicast(self->s.origin, MULTICAST_PHS, false);
			G_FreeEdict(self);
			return;
		}

		// Bounce logic
		if (tr.ent && tr.ent->solid == SOLID_BSP) // check if bouncing against walls 
		{
			self->bounce_count--;
			if (self->bounce_count <= 0)
			{
				// vanilla effect for last bounce
				gi.WriteByte(svc_temp_entity);
				gi.WriteByte((self->style != MOD_BLUEBLASTER) ? TE_BLASTER : TE_BLUEHYPERBLASTER);
				gi.WritePosition(self->s.origin);
				gi.WriteDir(tr.plane.normal);
				gi.multicast(self->s.origin, MULTICAST_PHS, false);
				G_FreeEdict(self);
				return;
			}
		}
	}
}

TOUCH(blaster_bolt_touch)(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (!self || !self->owner) {
		return;
	}
	if (other == self->owner)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY)) {
		G_FreeEdict(self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage) {
		// Verificar si es un blaster normal
		const bool useblaster = self->owner->client &&
			self->owner->client->pers.weapon &&
			self->owner->client->pers.weapon->id == IT_WEAPON_BLASTER;

		// Primero aplicamos el daño directo
		T_Damage(other, self, self->owner, self->velocity, self->s.origin,
			tr.plane.normal, self->dmg, 1, DAMAGE_ENERGY, MOD_BLASTER);

		// Luego aplicamos el radio damage si no es un blaster normal
		if ((!useblaster && self->dmg >= 5) || self->owner->svflags & SVF_MONSTER) {
			T_RadiusDamage(self, self->owner, (float)self->dmg, self,
				self->dmg_radius, DAMAGE_ENERGY, MOD_HYPERBLASTER);
		}

		G_FreeEdict(self);
	}
	else {
		// Estos blaster siempre intentan rebotar
		if (tr.ent && tr.ent->solid == SOLID_BSP) {
			self->bounce_count--;
			if (self->bounce_count > 0) {
				// Verificar si es un blaster normal
				const bool useblaster = self->owner->client &&
					self->owner->client->pers.weapon &&
					self->owner->client->pers.weapon->id == IT_WEAPON_BLASTER;

				// Solo mostrar efecto de rebote si NO es un blaster normal
				if (!useblaster || self->owner->svflags & SVF_MONSTER)
				{
					// Agregar radio damage al impactar
					if (self->dmg >= 5)
					{
						T_RadiusDamage(self, self->owner, (float)self->dmg, self,
							self->dmg_radius, DAMAGE_ENERGY, MOD_HYPERBLASTER);
					}

					// Efecto visual del rebote
					gi.WriteByte(svc_temp_entity);
					gi.WriteByte((self->style != MOD_BLUEBLASTER) ? TE_BLASTER : TE_BLUEHYPERBLASTER);
					gi.WritePosition(self->s.origin);
					gi.WriteDir(tr.plane.normal);
					gi.multicast(self->s.origin, MULTICAST_PHS, false);
				}
				return;
			}
		}

		// Efecto final y destrucción
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte((self->style != MOD_BLUEBLASTER) ? TE_BLASTER : TE_BLUEHYPERBLASTER);
		gi.WritePosition(self->s.origin);
		gi.WriteDir(tr.plane.normal);
		gi.multicast(self->s.origin, MULTICAST_PHS, false);
		G_FreeEdict(self);
	}
}

edict_t* fire_blaster_bolt(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, effects_t effect, mod_t mod, int bounces )
{
	edict_t* bolt = fire_blaster(self, start, dir, damage, speed, effect, mod, bounces);

	// unlike monster_fire_blaster_bolt, there's no default bouncing. set it on arguments
	if (bolt)
	{
		// Si es un blaster normal, dejamos el bolt como está
		const bool useblaster = self->client && self->client->pers.weapon &&
			self->client->pers.weapon->id == IT_WEAPON_BLASTER;

		// Si no es un blaster normal, aplicamos los efectos especiales
		if (!useblaster)
		{
			bolt->s.scale = 0.5f;                 // Escala visual más pequeña
			bolt->s.renderfx = RF_SHELL_HALF_DAM; // Efecto visual especial
			bolt->dmg_radius = 128; //radius for HB
			bolt->bounce_count = bounces;
		}
	}
	return bolt;
}

edict_t* fire_blaster(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, effects_t effect, mod_t mod, int bounces)
{
	edict_t* bolt = G_Spawn();
	bolt->svflags = SVF_PROJECTILE;
	bolt->s.origin = start;
	bolt->s.old_origin = start;
	bolt->s.angles = vectoangles(dir);
	bolt->velocity = dir * speed;
	bolt->movetype = bounces > 0 ? MOVETYPE_WALLBOUNCE : MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_PROJECTILE;
	if (self->client && !G_ShouldPlayersCollide(true)) {
		bolt->clipmask &= ~CONTENTS_PLAYER;
	}

	bolt->flags |= FL_DODGE;
	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	bolt->s.modelindex = gi.modelindex("models/objects/laser/tris.md2");
	bolt->s.sound = gi.soundindex("misc/lasfly.wav");
	bolt->owner = self;

	// Si tiene rebotes, usa el touch especial
	bolt->bounce_count = bounces;
	bolt->touch = bounces > 0 ? blaster_bolt_touch : blaster_touch;

	bolt->nextthink = level.time + 2_sec;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	bolt->style = mod.id;

	gi.linkentity(bolt);

	trace_t tr = gi.traceline(self->s.origin, bolt->s.origin, bolt, bolt->clipmask);
	if (tr.fraction < 1.0f) {
		bolt->s.origin = tr.endpos + (tr.plane.normal * 1.f);
		bolt->touch(bolt, tr.ent, tr, false);
	}

	return bolt;
}

constexpr spawnflags_t SPAWNFLAG_GRENADE_HAND = 1_spawnflag;
constexpr spawnflags_t SPAWNFLAG_GRENADE_HELD = 2_spawnflag;

/*
=================
fire_grenade
=================
*/
static void Grenade_ExplodeReal(edict_t* ent, edict_t* other, vec3_t normal)
{
	vec3_t origin;
	mod_t  mod;

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// FIXME: if we are onground then raise our Z just a bit since we are a point?
	if (other)
	{
		vec3_t const dir = other->s.origin - ent->s.origin;
		if (ent->spawnflags.has(SPAWNFLAG_GRENADE_HAND))
			mod = MOD_HANDGRENADE;
		else
			mod = MOD_GRENADE;
		T_Damage(other, ent, ent->owner, dir, ent->s.origin, normal, ent->dmg, ent->dmg, mod.id == MOD_HANDGRENADE ? DAMAGE_RADIUS : DAMAGE_NONE, mod);
	}

	if (ent->spawnflags.has(SPAWNFLAG_GRENADE_HELD))
		mod = MOD_HELD_GRENADE;
	else if (ent->spawnflags.has(SPAWNFLAG_GRENADE_HAND))
		mod = MOD_HG_SPLASH;
	else
		mod = MOD_G_SPLASH;
	T_RadiusDamage(ent, ent->owner, (float)ent->dmg, other, ent->dmg_radius, DAMAGE_NONE, mod);

	origin = ent->s.origin + normal;
	gi.WriteByte(svc_temp_entity);
	if (ent->waterlevel)
	{
		if (ent->groundentity)
			gi.WriteByte(TE_GRENADE_EXPLOSION_WATER);
		else
			gi.WriteByte(TE_ROCKET_EXPLOSION_WATER);
	}
	else
	{
		if (ent->groundentity)
			gi.WriteByte(TE_GRENADE_EXPLOSION);
		else
			gi.WriteByte(TE_ROCKET_EXPLOSION);
	}
	gi.WritePosition(origin);
	gi.multicast(ent->s.origin, MULTICAST_PHS, false);

	G_FreeEdict(ent);
}

THINK(Grenade_Explode) (edict_t* ent) -> void
{
	Grenade_ExplodeReal(ent, nullptr, ent->velocity * -0.02f);
}

TOUCH(Grenade_Touch) (edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (other == ent->owner)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(ent);
		return;
	}

	if (!other->takedamage)
	{
		if (ent->spawnflags.has(SPAWNFLAG_GRENADE_HAND))
		{
			if (frandom() > 0.5f)
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
			else
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
		}
		else
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/grenlb1b.wav"), 1, ATTN_NORM, 0);
		}
		return;
	}

	ent->enemy = other;
	Grenade_ExplodeReal(ent, other, tr.plane.normal);
}

THINK(Grenade4_Think) (edict_t* self) -> void
{
	if (level.time >= self->timestamp)
	{
		Grenade_Explode(self);
		return;
	}

	if (self->velocity)
	{
		const float p = self->s.angles.x;
		const float z = self->s.angles.z;
		const float speed_frac = clamp(self->velocity.lengthSquared() / (self->speed * self->speed), 0.f, 1.f);
		self->s.angles = vectoangles(self->velocity);
		self->s.angles.x = LerpAngle(p, self->s.angles.x, speed_frac);
		self->s.angles.z = z + (gi.frame_time_s * 360 * speed_frac);
	}

	self->nextthink = level.time + FRAME_TIME_S;
}

// Constants for better readability
constexpr float GRAVITY_ADJUSTMENT_FACTOR = 800.0f;
constexpr float BOUNCY_SPEED_MULTIPLIER = 1.5f;
constexpr float BOUNCY_DAMAGE_MULTIPLIER = 1.3f;
constexpr float BOUNCY_RADIUS_MULTIPLIER = 1.5f;
constexpr float MIN_VELOCITY_FOR_NORMAL = 10.0f;

// Configuration for bouncy grenades with clear documentation
struct BouncyGrenadeConfig {
	int max_bounces = 4;                  // Maximum number of bounces before final explosion
	float bounce_scale = 1.4f;            // Velocity multiplier after bounce
	float damage_decay = 0.85f;           // Damage reduction per bounce (percentage)
	float min_damage_fraction = 0.35f;    // Minimum damage as a fraction of original
	float random_dir_scale = 360.0f;      // Maximum angle for random direction generation
	gtime_t think_time = 1.0_sec;         // Time between bounces/explosions
};

static const BouncyGrenadeConfig BOUNCY_CONFIG;

/**
 * Handles grenade explosion and potential bounce
 * @param ent The grenade entity
 * @param other The entity hit (if any)
 * @param normal Surface normal at impact point
 */
void BouncyGrenade_ExplodeReal(edict_t* ent, edict_t* other, vec3_t normal)
{
	if (!ent || !ent->owner)
		return;

	// Ensure normal is normalized
	if (normal.length() < 0.1f) {
		// If normal is too small, use a default up vector
		normal = { 0.0f, 0.0f, 1.0f };
	}
	else {
		normal = normal.normalized();
	}

	// Notify owner of impact noise for AI awareness
	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// Handle direct impact damage
	if (other) {
		vec3_t dir = other->s.origin - ent->s.origin;
		float distance = dir.normalize();

		mod_t const mod = ent->spawnflags.has(SPAWNFLAG_GRENADE_HAND)
			? MOD_HANDGRENADE
			: MOD_GRENADE;


		T_Damage(other, ent, ent->owner, dir, ent->s.origin, normal,
			ent->dmg, ent->dmg, mod.id == MOD_HANDGRENADE ? DAMAGE_RADIUS : DAMAGE_RADIUS
			, mod);
	}

	// Handle area damage
	mod_t const splash_mod = ent->spawnflags.has(SPAWNFLAG_GRENADE_HELD)
		? MOD_HELD_GRENADE
		: ent->spawnflags.has(SPAWNFLAG_GRENADE_HAND)
		? MOD_HG_SPLASH
		: MOD_G_SPLASH;

	T_RadiusDamage(ent, ent->owner, static_cast<float>(ent->dmg), other,
		ent->dmg_radius, DAMAGE_NONE, splash_mod);

	// Visual explosion effects
	vec3_t const origin = ent->s.origin + normal;
	gi.WriteByte(svc_temp_entity);

	// Select appropriate explosion effect based on environment
	int explosion_type;
	if (ent->waterlevel) {
		explosion_type = ent->groundentity
			? TE_GRENADE_EXPLOSION_WATER
			: TE_ROCKET_EXPLOSION_WATER;
	}
	else {
		explosion_type = ent->groundentity
			? TE_GRENADE_EXPLOSION
			: TE_ROCKET_EXPLOSION;
	}

	gi.WriteByte(explosion_type);
	gi.WritePosition(origin);
	gi.multicast(ent->s.origin, MULTICAST_PHS, false);

	// Process bounce if there are bounces remaining
	if (ent->count > 0) {
		// Generate new random direction if on ground
		if (ent->groundentity) {
			// Create random direction within spherical bounds
			float pitch = crandom() * BOUNCY_CONFIG.random_dir_scale;
			float yaw = crandom() * BOUNCY_CONFIG.random_dir_scale;
			float roll = crandom() * BOUNCY_CONFIG.random_dir_scale;

			// Convert angles to direction vector more accurately
			vec3_t angles = { pitch, yaw, roll };
			auto [forward, right, up] = AngleVectors(angles);

			// Apply new velocity - use forward direction with maintained speed
			ent->velocity = forward * (ent->speed * BOUNCY_CONFIG.bounce_scale);

			// Add a bit of upward force to ensure it bounces
			if (forward.z < 0.3f) {
				ent->velocity.z += ent->speed * 0.4f;
			}
		}

		// Reduce damage for next explosion, maintaining minimum threshold
		const float min_damage = ent->dmg * BOUNCY_CONFIG.min_damage_fraction;
		ent->dmg = std::max(min_damage, ent->dmg * BOUNCY_CONFIG.damage_decay);

		// Decrement bounce count and set next think time
		ent->count--;
		ent->nextthink = level.time + BOUNCY_CONFIG.think_time;
	}
	else {
		// No more bounces - destroy entity
		G_FreeEdict(ent);
	}
}

/**
 * Think function for timed explosion
 */
THINK(BouncyGrenade_Explode)(edict_t* ent) -> void
{
	if (!ent)
		return;

	vec3_t normal;

	// Get a reliable normal vector from velocity
	if (ent->velocity.lengthSquared() > MIN_VELOCITY_FOR_NORMAL * MIN_VELOCITY_FOR_NORMAL) {
		normal = -ent->velocity.normalized();
	}
	else {
		// If velocity is too low, use a default up vector
		normal = { 0.0f, 0.0f, 1.0f };
	}

	BouncyGrenade_ExplodeReal(ent, ent->groundentity, normal);
}

/**
 * Touch function for collision-based explosion
 */
TOUCH(BouncyGrenade_Touch)(edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (!ent || !other)
		return;

	// Ignore collision with owner
	if (other == ent->owner)
		return;

	// Handle skybox collision - just remove the grenade
	if (tr.surface && (tr.surface->flags & SURF_SKY)) {
		G_FreeEdict(ent);
		return;
	}

	// Remember what was hit for damage purposes
	if (other->takedamage)
		ent->enemy = other;

	// Use the surface normal from the trace for accurate bounce physics
	BouncyGrenade_ExplodeReal(ent, other, tr.plane.normal);
}

/**
 * Think function for checking bounce conditions
 */
THINK(BouncyGrenade_Think)(edict_t* ent) -> void
{
	if (!ent)
		return;

	// Check if on ground or stopped moving vertically
	if (ent->groundentity || fabsf(ent->velocity.z) < 1.0f) {
		vec3_t normal;

		// Get normal from velocity or use ground normal
		if (ent->groundentity && ent->groundentity->solid == SOLID_BSP) {
			// Try to get the ground plane normal if possible
			normal = { 0.0f, 0.0f, 1.0f }; // Default to up
		}
		else if (ent->velocity.lengthSquared() > MIN_VELOCITY_FOR_NORMAL * MIN_VELOCITY_FOR_NORMAL) {
			normal = -ent->velocity.normalized();
		}
		else {
			normal = { 0.0f, 0.0f, 1.0f }; // Default to up
		}

		BouncyGrenade_ExplodeReal(ent, ent->groundentity, normal);
	}
	else {
		// Still in air, continue checking
		ent->nextthink = level.time + FRAME_TIME_S;

		// Update grenade angles to match velocity for visual effect
		if (ent->velocity.lengthSquared() > 1.0f) {
			ent->s.angles = vectoangles(ent->velocity);
		}
	}
}

/**
 * Creates and fires a grenade projectile
 *
 * @param self Entity firing the grenade
 * @param start Starting position
 * @param aimdir Direction vector
 * @param damage Base damage amount
 * @param speed Projectile speed
 * @param timer Time until explosion
 * @param damage_radius Explosion radius
 * @param right_adjust Right vector adjustment (for offset)
 * @param up_adjust Up vector adjustment (for arc)
 * @param monster Whether fired by a monster
 */
void fire_grenade(edict_t* self, const vec3_t& start, const vec3_t& aimdir,
	int damage, int speed, gtime_t timer, float damage_radius,
	float right_adjust, float up_adjust, bool monster)
{
	if (!self || speed <= 0)
		return;

	// Calculate firing vectors from aim direction
	vec3_t dir = vectoangles(aimdir);
	auto [forward, right, up] = AngleVectors(dir);

	// Create the grenade entity
	edict_t* grenade = G_Spawn();
	if (!grenade)
		return;

	// Set position and base velocity
	grenade->s.origin = start;
	grenade->velocity = aimdir * speed;

	// Apply trajectory adjustments
	if (up_adjust != 0.0f) {
		float const gravityAdjustment = level.gravity / GRAVITY_ADJUSTMENT_FACTOR;
		grenade->velocity += up * up_adjust * gravityAdjustment;
	}

	if (right_adjust != 0.0f) {
		grenade->velocity += right * right_adjust;
	}

	// Set common grenade properties
	grenade->movetype = MOVETYPE_BOUNCE;
	grenade->clipmask = MASK_PROJECTILE;

	// Skip player collision if configured
	if (self->client && !G_ShouldPlayersCollide(true)) {
		grenade->clipmask &= ~CONTENTS_PLAYER;
	}

	grenade->solid = SOLID_BBOX;
	grenade->svflags |= SVF_PROJECTILE;
	grenade->flags |= (FL_DODGE | FL_TRAP);
	grenade->s.effects |= EF_GRENADE;
	grenade->speed = speed;
	grenade->dmg = damage;
	grenade->dmg_radius = damage_radius;
	grenade->owner = self;
	grenade->classname = "grenade";
	grenade->s.modelindex = gi.modelindex("models/objects/grenade/tris.md2");

	// Apply specific behavior based on type
	// Fix for the SVF_MONSTER check - using bitwise AND instead of negated AND
	const bool use_bouncy = (g_bouncygl->integer && !(self->svflags & SVF_MONSTER));

	if (use_bouncy) {
		// Bouncy grenade behavior
		grenade->s.angles = vectoangles(grenade->velocity);
		grenade->nextthink = level.time + FRAME_TIME_S;
		grenade->timestamp = level.time + timer;
		grenade->think = BouncyGrenade_Think;
		grenade->s.renderfx |= RF_MINLIGHT;
		grenade->s.effects |= EF_GRENADE;
		grenade->count = BOUNCY_CONFIG.max_bounces;
		grenade->touch = BouncyGrenade_Touch;

		// Enhanced properties for bouncy grenades
		grenade->speed *= BOUNCY_SPEED_MULTIPLIER;
		grenade->dmg *= BOUNCY_DAMAGE_MULTIPLIER;
		grenade->dmg_radius *= BOUNCY_RADIUS_MULTIPLIER;
	}
	else if (monster) {
		// Monster-thrown grenade
		grenade->avelocity = { crandom() * 360, crandom() * 360, crandom() * 360 };
		grenade->nextthink = level.time + timer;
		grenade->think = Grenade_Explode;
		grenade->s.effects |= EF_GRENADE_LIGHT;
		grenade->touch = Grenade_Touch;
	}
	else {
		// Regular player grenade
		grenade->s.angles = vectoangles(grenade->velocity);
		grenade->nextthink = level.time + FRAME_TIME_S;
		grenade->timestamp = level.time + timer;
		grenade->think = Grenade4_Think;
		grenade->s.renderfx |= RF_MINLIGHT;
		grenade->touch = Grenade_Touch;
	}

	// Finalize entity creation
	gi.linkentity(grenade);
}

void fire_grenade2(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int speed, gtime_t timer, float damage_radius, bool held)
{
	edict_t* grenade;
	vec3_t	 dir;
	vec3_t	 forward, right, up;

	dir = vectoangles(aimdir);
	AngleVectors(dir, forward, right, up);

	grenade = G_Spawn();
	grenade->s.origin = start;
	grenade->velocity = aimdir * speed;

	float const gravityAdjustment = level.gravity / 800.f;

	grenade->velocity += up * (200 + crandom() * 10.0f) * gravityAdjustment;
	grenade->velocity += right * (crandom() * 10.0f);

	grenade->avelocity = { crandom() * 360, crandom() * 360, crandom() * 360 };
	grenade->movetype = MOVETYPE_BOUNCE;
	grenade->clipmask = MASK_PROJECTILE;
	// [Paril-KEX]
	if (self->client && !G_ShouldPlayersCollide(true))
		grenade->clipmask &= ~CONTENTS_PLAYER;
	grenade->solid = SOLID_BBOX;
	grenade->svflags |= SVF_PROJECTILE;
	grenade->flags |= (FL_DODGE | FL_TRAP);
	grenade->s.effects |= EF_GRENADE;

	grenade->s.modelindex = gi.modelindex("models/objects/grenade3/tris.md2");
	grenade->owner = self;
	grenade->touch = Grenade_Touch;
	grenade->nextthink = level.time + timer;
	grenade->think = Grenade_Explode;
	grenade->dmg = damage;
	grenade->dmg_radius = damage_radius;
	grenade->classname = "hand_grenade";
	grenade->spawnflags = SPAWNFLAG_GRENADE_HAND;
	if (held)
		grenade->spawnflags |= SPAWNFLAG_GRENADE_HELD;
	grenade->s.sound = gi.soundindex("weapons/hgrenc1b.wav");

	if (timer <= 0_ms)
		Grenade_Explode(grenade);
	else
	{
		gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/hgrent1a.wav"), 1, ATTN_NORM, 0);
		gi.linkentity(grenade);
	}
}


//FIREBALL

TOUCH(fireball_touch) (edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	vec3_t origin;

	if (other == ent->owner)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	origin = ent->s.origin + tr.plane.normal;

	if (other->takedamage)
	{
		T_Damage(other, ent, ent->owner, ent->velocity, ent->s.origin, tr.plane.normal, ent->dmg, ent->dmg, DAMAGE_NONE, MOD_FIREBALL);
	}
	else
	{
		// don't throw any debris in net games  // check horde later
		if (!G_IsDeathmatch() || !g_horde->integer || !G_IsCooperative())
		{
			if (tr.surface && !(tr.surface->flags & (SURF_WARP | SURF_TRANS33 | SURF_TRANS66 | SURF_FLOWING)))
			{
				/*ThrowGibs(ent, 2, {
					{ (size_t)irandom(5), "models/objects/debris2/tris.md2", GIB_METALLIC | GIB_DEBRIS }
					});*/
			}
		}
	}

	T_RadiusDamage(ent, ent->owner, (float)ent->radius_dmg, other, ent->dmg_radius, DAMAGE_NONE, MOD_R_SPLASH);

	gi.WriteByte(svc_temp_entity);
	if (ent->waterlevel)
		gi.WriteByte(TE_ROCKET_EXPLOSION_WATER);
	else
		gi.WriteByte(TE_ROCKET_EXPLOSION);
	gi.WritePosition(origin);
	gi.multicast(ent->s.origin, MULTICAST_PHS, false);

	G_FreeEdict(ent);
}
/*
=================
fire_rocket
=================
*/
TOUCH(rocket_touch) (edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	vec3_t origin;

	if (other == ent->owner)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	origin = ent->s.origin + tr.plane.normal;

	if (other->takedamage)
	{
		T_Damage(other, ent, ent->owner, ent->velocity, ent->s.origin, tr.plane.normal, ent->dmg, ent->dmg, DAMAGE_NONE, MOD_ROCKET);
	}
	else
	{
		// don't throw any debris in net games  // check horde later
		if (!G_IsDeathmatch() || !g_horde->integer || !G_IsCooperative())
		{
			if (tr.surface && !(tr.surface->flags & (SURF_WARP | SURF_TRANS33 | SURF_TRANS66 | SURF_FLOWING)))
			{
				/*ThrowGibs(ent, 2, {
					{ (size_t)irandom(5), "models/objects/debris2/tris.md2", GIB_METALLIC | GIB_DEBRIS }
					});*/
			}
		}
	}

	T_RadiusDamage(ent, ent->owner, (float)ent->radius_dmg, other, ent->dmg_radius, DAMAGE_NONE, MOD_R_SPLASH);

	gi.WriteByte(svc_temp_entity);
	if (ent->waterlevel)
		gi.WriteByte(TE_ROCKET_EXPLOSION_WATER);
	else
		gi.WriteByte(TE_ROCKET_EXPLOSION);
	gi.WritePosition(origin);
	gi.multicast(ent->s.origin, MULTICAST_PHS, false);

	G_FreeEdict(ent);
}

edict_t* fire_rocket(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t* rocket;

	rocket = G_Spawn();
	rocket->s.origin = start;
	rocket->s.angles = vectoangles(dir);
	rocket->velocity = dir * speed;
	rocket->movetype = MOVETYPE_FLYMISSILE;
	rocket->svflags |= SVF_PROJECTILE;
	rocket->flags |= FL_DODGE;
	rocket->clipmask = MASK_PROJECTILE;
	// [Paril-KEX]
	if (self->client && !G_ShouldPlayersCollide(true))
		rocket->clipmask &= ~CONTENTS_PLAYER;
	rocket->solid = SOLID_BBOX;
	rocket->s.effects |= EF_ROCKET;
	rocket->s.modelindex = gi.modelindex("models/objects/rocket/tris.md2");
	rocket->owner = self;
	rocket->touch = rocket_touch;
	rocket->nextthink = level.time + gtime_t::from_sec(8000.f / speed);
	rocket->think = G_FreeEdict;
	rocket->dmg = damage;
	rocket->radius_dmg = radius_damage;
	rocket->dmg_radius = damage_radius;
	rocket->s.sound = gi.soundindex("weapons/rockfly.wav");
	rocket->classname = "rocket";

	gi.linkentity(rocket);

	return rocket;
}


using search_callback_t = decltype(game_import_t::inPVS);

bool binary_positional_search_r(const vec3_t& viewer, const vec3_t& start, const vec3_t& end, search_callback_t cb, int32_t split_num)
{
	// check half-way point
	vec3_t const mid = (start + end) * 0.5f;

	if (cb(viewer, mid, true))
		return true;

	// no more splits
	if (!split_num)
		return false;

	// recursively check both sides
	return binary_positional_search_r(viewer, start, mid, cb, split_num - 1) || binary_positional_search_r(viewer, mid, end, cb, split_num - 1);
}

// [Paril-KEX] simple binary search through a line to see if any points along
// the line (in a binary split) pass the callback
bool binary_positional_search(const vec3_t& viewer, const vec3_t& start, const vec3_t& end, search_callback_t cb, int32_t num_splits)
{
	// check start/end first
	if (cb(viewer, start, true) || cb(viewer, end, true))
		return true;

	// recursive split
	return binary_positional_search_r(viewer, start, end, cb, num_splits);
}

struct fire_rail_pierce_t : pierce_args_t
{
	edict_t* self;
	vec3_t	 aimdir;
	int		 damage;
	int		 kick;
	bool	 water = false;

	inline fire_rail_pierce_t(edict_t* self, vec3_t aimdir, int damage, int kick) :
		pierce_args_t(),
		self(self),
		aimdir(aimdir),
		damage(damage),
		kick(kick)
	{
	}

	// we hit an entity; return false to stop the piercing.
	// you can adjust the mask for the re-trace (for water, etc).
	bool hit(contents_t& mask, vec3_t& end) override
	{
		if (tr.contents & (CONTENTS_SLIME | CONTENTS_LAVA))
		{
			mask &= ~(CONTENTS_SLIME | CONTENTS_LAVA);
			water = true;
			return true;
		}
		else
		{
			// try to kill it first
			if ((tr.ent != self) && (tr.ent->takedamage))
				T_Damage(tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_NONE, MOD_RAILGUN);

			// dead, so we don't need to care about checking pierce
			if (!tr.ent->inuse || (!tr.ent->solid || tr.ent->solid == SOLID_TRIGGER))
				return true;

			// ZOID--added so rail goes through SOLID_BBOX entities (gibs, etc)
			if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client) ||
				// ROGUE
				(tr.ent->flags & FL_DAMAGEABLE) ||
				// ROGUE
				(tr.ent->solid == SOLID_BBOX))
			{
				if (!mark(tr.ent))
					return false;

				return true;
			}
		}

		return false;
	}
};

// [Paril-KEX] get the current unique unicast key
uint32_t GetUnicastKey()
{
	static uint32_t key = 1;

	if (!key)
		return key = 1;

	return key++;
}

/*
=================
fire_rail
=================
*/
bool fire_rail(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int kick)
{
	fire_rail_pierce_t args = {
		self,
		aimdir,
		damage,
		kick
	};

	contents_t mask = MASK_PROJECTILE | CONTENTS_SLIME | CONTENTS_LAVA;

	// [Paril-KEX]
	if (self->client && !G_ShouldPlayersCollide(true))
		mask &= ~CONTENTS_PLAYER;

	vec3_t const end = start + (aimdir * 8192);

	pierce_trace(start, end, self, args, mask);

	uint32_t const unicast_key = GetUnicastKey();

	// send gun puff / flash
	// [Paril-KEX] this often makes double noise, so trying
	// a slightly different approach...
	for (auto player : active_players())
	{
		vec3_t const org = player->s.origin + player->client->ps.viewoffset + vec3_t{ 0, 0, (float)player->client->ps.pmove.viewheight };

		if (binary_positional_search(org, start, args.tr.endpos, gi.inPHS, 3))
		{
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte((deathmatch->integer && g_instagib->integer) ? TE_RAILTRAIL2 : TE_RAILTRAIL);
			gi.WritePosition(start);
			gi.WritePosition(args.tr.endpos);
			gi.unicast(player, false, unicast_key);
		}
	}

	if (self->client)
		PlayerNoise(self, args.tr.endpos, PNOISE_IMPACT);

	return args.num_pierced;
}

// Constants moved to the top for better maintenance
constexpr float BFG10K_INITIAL_SPEED = 400.0f;
constexpr gtime_t BFG_WALL_EXPIRE_TIME = 2_sec;
constexpr gtime_t BFG_MAX_LIFETIME = 8_sec;
constexpr int BFG_MAX_SAFE_RANGE = 2048;
constexpr int BFG_MONSTER_RANGE = 256;
constexpr int BFG_PLAYER_RANGE = 1536;
constexpr float BFG_LASER_DISTANCE = 256.0f;
constexpr gtime_t BFG_LASER_LIFETIME = 300_ms;

static vec3_t bfg_laser_pos(const vec3_t& p, float dist)
{
	const float theta = frandom(2 * PIf);
	const float phi = acos(crandom());

	const vec3_t d{
		sin(phi) * cos(theta),
		sin(phi) * sin(theta),
		cos(phi)
	};

	return p + (d * dist);
}

THINK(bfg_laser_update) (edict_t* self) -> void
{
	if (level.time > self->timestamp || !self->owner->inuse)
	{
		G_FreeEdict(self);
		return;
	}

	self->s.origin = self->owner->s.origin;
	self->nextthink = level.time + 1_ms;
	gi.linkentity(self);
}

static void bfg_spawn_laser(edict_t* self)
{
	const vec3_t end = bfg_laser_pos(self->s.origin, BFG_LASER_DISTANCE);
	const trace_t tr = gi.traceline(self->s.origin, end, self, MASK_OPAQUE | CONTENTS_PROJECTILECLIP);

	if (tr.fraction == 1.0f)
		return;

	edict_t* laser = G_Spawn();
	laser->s.frame = 3;
	laser->s.renderfx = RF_BEAM_LIGHTNING;
	laser->movetype = MOVETYPE_NONE;
	laser->solid = SOLID_NOT;
	laser->s.modelindex = MODELINDEX_WORLD; // must be non-zero
	laser->s.origin = self->s.origin;
	laser->s.old_origin = tr.endpos;
	laser->s.skinnum = 0xD0D0D0D0;
	laser->think = bfg_laser_update;
	laser->nextthink = level.time + 1_ms;
	laser->timestamp = level.time + BFG_LASER_LIFETIME;
	laser->owner = self;
	gi.linkentity(laser);
}

THINK(bfg_explode) (edict_t* self) -> void
{
	bfg_spawn_laser(self);

	if (self->s.frame == 0)
	{
		edict_t* ent = nullptr;
		while ((ent = findradius(ent, self->s.origin, self->dmg_radius)) != nullptr)
		{
			if (!ent->takedamage)
				continue;
			if (ent == self->owner)
				continue;
			if (!CanDamage(ent, self))
				continue;
			if (!CanDamage(ent, self->owner))
				continue;
			if (!(ent->svflags & SVF_MONSTER) && !(ent->flags & FL_DAMAGEABLE) &&
				(!ent->client) && (strcmp(ent->classname, "misc_explobox") != 0))
				continue;
			if (CheckTeamDamage(ent, self->owner))
				continue;

			// Calculate entity center once
			const vec3_t centroid = (ent->mins + ent->maxs) * 0.5f + ent->s.origin;

			// Calculate direction and distance squared to avoid sqrt
			const vec3_t diff = self->s.origin - centroid;
			const float dist_squared = diff.lengthSquared();
			const float dist = sqrtf(dist_squared);

			// Calculate damage
			const float points = self->radius_dmg * (1.0f - sqrtf(dist / self->dmg_radius));

			T_Damage(ent, self, self->owner, self->velocity, centroid, vec3_origin,
				(int)points, 0, DAMAGE_ENERGY, MOD_BFG_EFFECT);

			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_BFG_ZAP);
			gi.WritePosition(self->s.origin);
			gi.WritePosition(centroid);
			gi.multicast(self->s.origin, MULTICAST_PHS, false);
		}
	}

	self->nextthink = level.time + 10_hz;
	self->s.frame++;
	if (self->s.frame == 5)
		self->think = G_FreeEdict;
}

int calculate_bfg_range(const edict_t* self)
{
	int range;

	if (self->owner->svflags & SVF_MONSTER)
		range = BFG_MONSTER_RANGE;
	else if (g_bfgpull->integer && self->owner->client)
		range = BFG_PLAYER_RANGE;
	else
		range = BFG_MONSTER_RANGE;

	return std::min(range, BFG_MAX_SAFE_RANGE); // Safety clamp
}

struct bfg_laser_pierce_t : pierce_args_t
{
	edict_t* self;
	vec3_t   dir;
	int      damage;

	inline bfg_laser_pierce_t(edict_t* self, const vec3_t& dir, int damage) :
		pierce_args_t(),
		self(self),
		dir(dir),
		damage(damage)
	{
	}

	// we hit an entity; return false to stop the piercing.
	// you can adjust the mask for the re-trace (for water, etc).
	bool hit(contents_t& mask, vec3_t& end) override
	{
		// hurt it if we can
		if ((tr.ent->takedamage) && !(tr.ent->flags & FL_IMMUNE_LASER) && (tr.ent != self->owner))
			T_Damage(tr.ent, self, self->owner, dir, tr.endpos, vec3_origin, damage, 1, DAMAGE_ENERGY, MOD_BFG_LASER);

		// if we hit something that's not a monster or player we're done
		if (!(tr.ent->svflags & SVF_MONSTER) && !(tr.ent->flags & FL_DAMAGEABLE) && (!tr.ent->client))
		{
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_LASER_SPARKS);
			gi.WriteByte(4);
			gi.WritePosition(tr.endpos + tr.plane.normal);
			gi.WriteDir(tr.plane.normal);
			gi.WriteByte(208);
			gi.multicast(tr.endpos + tr.plane.normal, MULTICAST_PVS, false);
			return false;
		}

		if (!mark(tr.ent))
			return false;

		return true;
	}
};

int calculate_pull_force(const edict_t* ent)
{
	constexpr int MAX_FORCE = 100;
	const int force = ent->groundentity ? 20 : 10;
	return std::min(force, MAX_FORCE);
}

THINK(bfg_think) (edict_t* self) -> void
{
	// Simplified lifetime check
	const gtime_t expiry_time = (self->timestamp != 0_ms) ?
		self->timestamp :
		(self->air_finished + BFG_MAX_LIFETIME);
	if (level.time >= expiry_time)
	{
		G_FreeEdict(self);
		return;
	}

	int dmg = deathmatch->integer ? 5 : 10;
	bfg_spawn_laser(self);

	const int bfgrange = calculate_bfg_range(self);
	const int bfgrange_squared = bfgrange * bfgrange; // Precalculate squared range

	edict_t* ent = nullptr;
	while ((ent = findradius(ent, self->s.origin, bfgrange)) != nullptr)
	{
		if (ent == self || ent == self->owner || !ent->takedamage)
			continue;
		if (!(ent->svflags & SVF_MONSTER) && !(ent->flags & FL_DAMAGEABLE) &&
			(!ent->client) && (strcmp(ent->classname, "misc_explobox") != 0))
			continue;
		if (CheckTeamDamage(ent, self->owner))
			continue;

		// Calculate entity center once
		const vec3_t point = (ent->absmin + ent->absmax) * 0.5f;

		// Calculate direction once
		vec3_t dir = self->s.origin - point;

		// Use dot product or length_squared for efficiency
// Use lengthSquared for efficiency
		if (dir.lengthSquared() > bfgrange_squared)
			continue;
		dir.normalize();

		const vec3_t start = self->s.origin;
		const vec3_t end = start + (dir * -2048.0f);

		// [Paril-KEX] don't fire a laser if we're blocked by the world
		trace_t tr = gi.traceline(start, point, nullptr, CONTENTS_SOLID | CONTENTS_PROJECTILECLIP);

		if (tr.fraction < 1.0f)
			continue;

		T_Damage(ent, self, self->owner, dir, point, vec3_origin, dmg, 1, DAMAGE_ENERGY, MOD_BFG_LASER);

		tr = gi.traceline(start, end, nullptr, CONTENTS_SOLID | CONTENTS_PROJECTILECLIP);

		// Use reference to dir to avoid copy
		bfg_laser_pierce_t args{
			self,
			dir,
			dmg
		};

		pierce_trace(start, end, self, args,
			CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER |
			CONTENTS_DEADMONSTER | CONTENTS_PROJECTILECLIP);

		if (g_bfgpull->integer && self->owner->client)
		{
			if (ent->movetype != MOVETYPE_NONE && ent->movetype != MOVETYPE_PUSH)
			{
				// Use the calculated pull force
				const int knockback = calculate_pull_force(ent);
				T_Damage(ent, self, self->owner, dir, point, vec3_origin, 0,  // No direct damage 
					knockback, // Use calculated force for knockback
					DAMAGE_ENERGY,
					MOD_BFG_LASER);
			}
		}

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BFG_LASER);
		gi.WritePosition(self->s.origin);
		gi.WritePosition(tr.endpos);
		gi.multicast(self->s.origin, MULTICAST_PHS, false);
	}

	// Use constant for the next think time
	const gtime_t next_think_time = g_bfgslide->integer ?
		FRAME_TIME_MS * 1.75 :
		FRAME_TIME_MS * 3;
	self->nextthink = level.time + next_think_time;
}

TOUCH(bfg_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
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

	if (g_bfgslide->integer) {
		// Update timestamp only if it hasn't been set yet
		if (self->timestamp == 0_ms) {
			self->timestamp = level.time + BFG_WALL_EXPIRE_TIME;
		}

		// Calculate new velocity once
		const float oldVelocity = self->velocity.length();
		float newVelocity = (2 * BFG10K_INITIAL_SPEED) - oldVelocity;
		newVelocity = std::max(100.0f, std::min(newVelocity, oldVelocity));

		// Normalize and scale in one operation for efficiency
		self->velocity.normalize();
		self->velocity *= newVelocity;
	}
	else
	{
		// Non-slide behavior - apply damage and explode
		if (other->takedamage)
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal, 200, 0, DAMAGE_ENERGY, MOD_BFG_BLAST);

		T_RadiusDamage(self, self->owner, 200, other, 100, DAMAGE_ENERGY, MOD_BFG_BLAST);

		gi.sound(self, CHAN_VOICE, gi.soundindex("weapons/bfg__x1b.wav"), 1, ATTN_NORM, 0);
		self->solid = SOLID_NOT;
		self->touch = nullptr;

		// Adjust position based on velocity and time
		self->s.origin += self->velocity * (-1 * gi.frame_time_s);
		self->velocity = {};

		// Setup explosion visuals and behavior
		self->s.modelindex = gi.modelindex("sprites/s_bfg3.sp2");
		self->s.frame = 0;
		self->s.sound = 0;
		self->s.effects &= ~EF_ANIM_ALLFAST;
		self->think = bfg_explode;
		self->nextthink = level.time + 10_hz;
		self->enemy = other;

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BFG_BIGEXPLOSION);
		gi.WritePosition(self->s.origin);
		gi.multicast(self->s.origin, MULTICAST_PHS, false);
	}
}

void fire_bfg(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, float damage_radius)
{
	edict_t* bfg = G_Spawn();
	bfg->s.origin = start;
	bfg->s.angles = vectoangles(dir);
	bfg->velocity = dir * speed;
	bfg->movetype = g_bfgslide->integer ? MOVETYPE_SLIDE : MOVETYPE_FLYMISSILE;
	bfg->clipmask = MASK_PROJECTILE;
	bfg->svflags = SVF_PROJECTILE;

	// Collision handling for players
	if (self->client && !G_ShouldPlayersCollide(true))
		bfg->clipmask &= ~CONTENTS_PLAYER;

	bfg->solid = SOLID_BBOX;
	bfg->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	bfg->s.modelindex = gi.modelindex("sprites/s_bfg1.sp2");
	bfg->owner = self;
	bfg->touch = bfg_touch;
	bfg->nextthink = level.time + FRAME_TIME_S;
	bfg->think = bfg_think;
	bfg->radius_dmg = damage;
	bfg->dmg_radius = damage_radius;
	bfg->classname = "bfg blast";
	bfg->s.sound = gi.soundindex("weapons/bfg__l1a.wav");
	bfg->timestamp = 0_ms;
	bfg->air_finished = level.time;
	bfg->teammaster = bfg;
	bfg->teamchain = nullptr;

	gi.linkentity(bfg);
}

TOUCH(disintegrator_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_WIDOWSPLASH);
	gi.WritePosition(self->s.origin - (self->velocity * 0.01f));
	gi.multicast(self->s.origin, MULTICAST_PHS, false);

	G_FreeEdict(self);

	if (other->svflags & (SVF_MONSTER | SVF_PLAYER))
	{
		other->disintegrator_time += 50_sec;
		other->disintegrator = self->owner;
	}
}


void fire_disintegrator(edict_t* self, const vec3_t& start, const vec3_t& forward, int speed)
{
	edict_t* bfg;

	bfg = G_Spawn();
	bfg->s.origin = start;
	bfg->s.angles = vectoangles(forward);
	bfg->velocity = forward * speed;
	bfg->movetype = MOVETYPE_FLYMISSILE;
	bfg->clipmask = MASK_PROJECTILE;
	// [Paril-KEX]
	if (self->client && !G_ShouldPlayersCollide(true))
		bfg->clipmask &= ~CONTENTS_PLAYER;
	bfg->solid = SOLID_BBOX;
	bfg->s.effects |= EF_TAGTRAIL | EF_ANIM_ALL;
	bfg->s.renderfx |= RF_TRANSLUCENT;
	bfg->svflags |= SVF_PROJECTILE;
	bfg->flags |= FL_DODGE;
	bfg->s.modelindex = gi.modelindex("sprites/s_bfg1.sp2");
	bfg->owner = self;
	bfg->touch = disintegrator_touch;
	bfg->nextthink = level.time + gtime_t::from_sec(8000.f / speed);
	bfg->think = G_FreeEdict;
	bfg->classname = "disint ball";
	bfg->s.sound = gi.soundindex("weapons/bfg__l1a.wav");

	gi.linkentity(bfg);
}