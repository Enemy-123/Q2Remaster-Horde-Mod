// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"
/*
================ =
fire_hit

Used for all impact(hit / punch / slash) attacks
================ =
*/
// MODIFIED FUNCTION
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

	// Verificación inicial de null para enemy
	if (!self->enemy) {
		return false;
	}

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

	// --- MODIFICATION START ---
	// The original code would forcefully change the hit entity to self->enemy,
	// causing it to "hit through" obstacles. This version respects what the trace hits.

	// check that we can hit the point on the bbox
	tr = gi.traceline(self->s.origin, point, self, MASK_PROJECTILE);

	// if the trace hit something before reaching the target point...
	if (tr.fraction < 1.0f)
	{
		// ...and it wasn't our intended enemy, then the attack is blocked.
		if (tr.ent != self->enemy)
			return false;
	}

	// check that we can hit the player's origin from the point on their bbox
	tr = gi.traceline(point, self->enemy->s.origin, self, MASK_PROJECTILE);

	// if the trace hit something before reaching the enemy's origin...
	if (tr.fraction < 1.0f)
	{
		// ...and it wasn't our intended enemy, then the attack is blocked.
		if (tr.ent != self->enemy)
			return false;
	}
	// --- MODIFICATION END ---

	// If we've reached here, we have a clear line of sight.
	tr.ent = self->enemy; // We can now be certain this is the correct entity to hit.

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

/*
=================
fire_lead_energy

This is a modified version of fire_lead for energy-based weapons.
Used for implementing energy shells that work like bullets but with energy effects.
=================
*/
	struct fire_energy_pierce_t : pierce_args_t
	{
		edict_t* self;
		edict_t* attacker;  // Track the actual attacker
		vec3_t   start;
		vec3_t   aimdir;
		int      damage;
		int      kick;
		int      hspread;
		int      vspread;
		mod_t    mod;
		int      te_impact;
		contents_t mask;
		bool     water = false;
		vec3_t   water_start = {};
		edict_t* chain = nullptr;

		inline fire_energy_pierce_t(edict_t* self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, mod_t mod, int te_impact, contents_t mask) :
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
			// Determine the actual attacker inside the constructor
			if (self && self->owner)
			{
				if (self->owner->classname && Q_strcasecmp(self->owner->classname, "monster_sentrygun") == 0)
				{
					// If the owner is a turret, the attacker is the turret's owner (the player)
					attacker = self->owner->owner ? self->owner->owner : self->owner;
				}
				else
				{
					// Otherwise, the attacker is the owner
					attacker = self->owner;
				}
			}
			else
			{
				// Default to self if no owner
				attacker = self;
			}
		}

		// we hit an entity; return false to stop the piercing.
		bool hit(contents_t& mask, vec3_t& end) override
		{
			// see if we hit water
			if (tr.contents & MASK_WATER)
			{
				int color;

				water = true;
				water_start = tr.endpos;

				if (te_impact != -1 && start != tr.endpos)
				{
					if (tr.contents & CONTENTS_WATER)
					{
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

				mask &= ~MASK_WATER;
				return true;
			}

			// did we hit a damageable entity?
			if (tr.ent->takedamage)
			{
				// Use attacker instead of self for damage credit
				T_Damage(tr.ent, self, attacker, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_ENERGY, mod);

				// Check for piercing conditions
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
				// Energy impact effect
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

			return false;
		}
	};
/*
=================
fire_lead_energy

This is an internal support routine used for energy-based instant hit weapons.
Similar to bullets but with energy effects and damage.
=================
*/
static void fire_lead_energy(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int kick, int te_impact, int hspread, int vspread, mod_t mod)
{
	// Create the pierce structure which will determine the attacker in its constructor
	fire_energy_pierce_t args = {
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
fire_energy_bullet

Fires a single energy round. Similar to fire_bullet but with energy effects.
Used for energy shells bonus.
=================
*/
void fire_energy_bullet(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int kick, int hspread, int vspread, mod_t mod)
{
    // REMOVED: Redundant attacker logic. It's handled in the pierce_t struct.
    // edict_t *attacker;
    // if(self->owner->classname && Q_strcasecmp(self->owner->classname, "monster_sentrygun") == 0) ...

    // Apply damage modifier, if applicable.
    if (self->svflags & SVF_MONSTER) {
        damage *= M_DamageModifier(self);
    }

    // Use TE_BLASTER for energy impact effect
    fire_lead_energy(self, start, aimdir, damage, kick, TE_BLASTER, hspread, vspread, mod);
}
/*
=================
fire_energy_shotgun

Shoots energy pellets. Similar to fire_shotgun but with energy effects.
Used for energy shells bonus for shotguns.
=================
*/
void fire_energy_shotgun(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int kick, int hspread, int vspread, int count, mod_t mod)
{
	if (self->svflags & SVF_MONSTER) {
		damage *= M_DamageModifier(self);
	}

	for (int i = 0; i < count; i++)
		fire_lead_energy(self, start, aimdir, damage, kick, TE_BLASTER, hspread, vspread, mod);
}

//normal lead
struct fire_lead_pierce_t : pierce_args_t
{
	edict_t* self;
	edict_t* attacker;  // Track the actual attacker (may be different from self)
	vec3_t   start;
	vec3_t   aimdir;
	int      damage;
	int      kick;
	int      hspread;
	int      vspread;
	mod_t    mod;
	int      te_impact;
	contents_t mask;
	bool     water = false;
	vec3_t   water_start = {};
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
		// Determine the actual attacker inside the constructor
		if (self && self->owner)
		{
			if (self->owner->classname && Q_strcasecmp(self->owner->classname, "monster_sentrygun") == 0)
			{
				// If the owner is a turret, the attacker is the turret's owner (the player)
				attacker = self->owner->owner ? self->owner->owner : self->owner;
			}
			else
			{
				// Otherwise, the attacker is the owner
				attacker = self->owner;
			}
		}
		else
		{
			// Default to self if no owner
			attacker = self;
		}
	}

	// we hit an entity; return false to stop the piercing.
	bool hit(contents_t& mask, vec3_t& end) override
	{
		// see if we hit water
		if (tr.contents & MASK_WATER)
		{
			int color;

			water = true;
			water_start = tr.endpos;

			if (te_impact != -1 && start != tr.endpos)
			{
				if (tr.contents & CONTENTS_WATER)
				{
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
			// Use attacker instead of self for damage credit
			T_Damage(tr.ent, self, attacker, aimdir, tr.endpos, tr.plane.normal, damage, kick, mod.id == MOD_TESLA ? DAMAGE_ENERGY : DAMAGE_BULLET, mod);

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
	// Create the pierce structure which will determine the attacker in its constructor
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
	// Apply damage modifier if monster
	if (self->svflags & SVF_MONSTER) {
		damage *= M_DamageModifier(self);
	}

	// Use the standard fire_lead function - attacker is determined inside the pierce_t constructor
	fire_lead(self, start, aimdir, damage, kick, mod.id == MOD_TESLA ? -1 : TE_GUNSHOT, hspread, vspread, mod);
}

/*
=================
fire_shotgun

Shoots shotgun pellets.  Used by shotgun and super shotgun.
Now checks for g_energyshells and redirects to fire_energy_shotgun if enabled.
=================
*/
void fire_shotgun(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int kick, int hspread, int vspread, int count, mod_t mod)
{
	// Check if energy shells are enabled
	if (g_energyshells->integer)
	{
		// Call the energy version instead
		fire_energy_shotgun(self, start, aimdir, damage, kick, hspread, vspread, count, mod);
		return;
	}

	// Original implementation for regular shotgun
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

// FINAL CORRECTED FUNCTION
TOUCH(blaster_unified_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (!self || !self->owner) return;
	if (other == self->owner) return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner->client)
	{
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);
	}

	// If we hit a damageable entity
	if (other->takedamage)
	{
		mod_t mod = mod_t(static_cast<mod_id_t>(self->style));

		T_Damage(other, self, self->owner, self->velocity, self->s.origin,
			tr.plane.normal, self->dmg, 1, DAMAGE_ENERGY, mod);

		if (self->bounce_count > 0 && self->dmg_radius > 0)
		{
			T_RadiusDamage(self, self->owner, (float)self->dmg, self,
				self->dmg_radius, DAMAGE_ENERGY, MOD_HYPERBLASTER);
		}

		G_FreeEdict(self);
		return;
	}

	// If we hit a non-damageable surface (a wall)
	if (self->bounce_count > 0)
	{
		// --- MODIFICATION START ---
		// Determine if we should show a bounce effect based on the weapon type and bounce count.

		bool show_effect = false;
		mod_id_t mod_id = static_cast<mod_id_t>(self->style);

		if (mod_id == MOD_BLASTER)
		{
			// For the standard blaster, ONLY show an effect on the final bounce.
			if (self->bounce_count == 1)
			{
				show_effect = true;
			}
		}
		else
		{
			// For all other bouncing types (Hyperblaster, etc.), show an effect on EVERY bounce.
			show_effect = true;
		}

		// Now, decrement the bounce count for the next impact.
		self->bounce_count--;

		if (show_effect)
		{
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte((self->style != MOD_BLUEBLASTER) ? TE_BLASTER : TE_BLUEHYPERBLASTER);
			gi.WritePosition(self->s.origin);
			gi.WriteDir(tr.plane.normal);
			gi.multicast(self->s.origin, MULTICAST_PHS, false);
		}
		// --- MODIFICATION END ---

		// Return without freeing the edict to allow the physics engine to handle the bounce.
		return;
	}

	// No bounces left, or it wasn't a bouncing bolt to begin with. Explode on the wall.
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte((self->style != MOD_BLUEBLASTER) ? TE_BLASTER : TE_BLUEHYPERBLASTER);
	gi.WritePosition(self->s.origin);
	gi.WriteDir(tr.plane.normal);
	gi.multicast(self->s.origin, MULTICAST_PHS, false);
	G_FreeEdict(self);
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

// MODIFIED FUNCTION
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

	// --- MODIFICATION ---
	// Assign the new unified touch function
	bolt->touch = blaster_unified_touch;
	bolt->bounce_count = bounces;
	// Set damage radius if it's a bouncing (hyperblaster-style) bolt
	if (bounces > 0) {
		bolt->dmg_radius = 128;
	}
	// --- END MODIFICATION ---

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

// ... (previous code in the file remains the same) ...

constexpr spawnflags_t SPAWNFLAG_GRENADE_HAND = 1_spawnflag;
constexpr spawnflags_t SPAWNFLAG_GRENADE_HELD = 2_spawnflag;

/*
=================
fire_grenade
=================
*/
static void Grenade_ExplodeReal(edict_t* ent, edict_t* other, vec3_t normal, edict_t* attacker, bool is_final_explosion)
{
	vec3_t origin;
	mod_t  mod;

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// Direct impact damage is applied here if 'other' is not null
	if (other && other->takedamage)
	{
		vec3_t const dir = other->s.origin - ent->s.origin;
		if (ent->spawnflags.has(SPAWNFLAG_GRENADE_HAND))
			mod = MOD_HANDGRENADE;
		else
			mod = MOD_GRENADE;
		T_Damage(other, ent, attacker, dir, ent->s.origin, normal, ent->dmg, ent->dmg, DAMAGE_RADIUS, mod);
	}

	// Radius damage for the explosion
	if (ent->spawnflags.has(SPAWNFLAG_GRENADE_HELD))
		mod = MOD_HELD_GRENADE;
	else if (ent->spawnflags.has(SPAWNFLAG_GRENADE_HAND))
		mod = MOD_HG_SPLASH;
	else
		mod = MOD_G_SPLASH;
	T_RadiusDamage(ent, attacker, (float)ent->dmg, other, ent->dmg_radius, DAMAGE_NONE, mod);

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
        // Use a bigger explosion effect for the final bounce
		if (ent->groundentity || is_final_explosion)
			gi.WriteByte(TE_ROCKET_EXPLOSION);
		else
			gi.WriteByte(TE_GRENADE_EXPLOSION);
	}
	gi.WritePosition(origin);
	gi.multicast(ent->s.origin, MULTICAST_PHS, false);

	G_FreeEdict(ent);
}

THINK(Grenade_Explode) (edict_t* ent) -> void
{
	edict_t* attacker = ent->owner;
	if (ent->owner && ent->owner->classname && Q_strcasecmp(ent->owner->classname, "monster_sentrygun") == 0)
	{
		attacker = ent->owner->owner;
	}
	Grenade_ExplodeReal(ent, nullptr, ent->velocity * -0.02f, attacker, true);
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
	edict_t* attacker = ent->owner;
	if (ent->owner && ent->owner->classname && Q_strcasecmp(ent->owner->classname, "monster_sentrygun") == 0)
	{
		attacker = ent->owner->owner;
	}
	Grenade_ExplodeReal(ent, other, tr.plane.normal, attacker, true);
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

//====================================================================================
// FINAL IMPROVED BOUNCY/CLUSTER GRENADE LOGIC (V5 - Immediate Final Boom & More Damage)
//====================================================================================

struct BouncyGrenadeConfig {
    int max_bounces = 4;
    // Final explosion is 40% stronger with a 60% larger radius
    float damage_multiplier = 1.40f;
    float radius_multiplier = 1.60f;
    // Cluster explosions are now much more powerful
    float cluster_damage_fraction = 0.60f; // 60% of final damage
    float cluster_radius_fraction = 0.75f; // 75% of final radius
    gtime_t life_time = 5.0_sec;
};
static const BouncyGrenadeConfig BOUNCY_CONFIG;

/**
 * @brief Triggers a smaller "cluster" explosion without destroying the grenade.
 */
static void BouncyGrenade_ClusterExplode(edict_t* ent, const trace_t& tr)
{
    if (!ent || !ent->owner) return;

    float cluster_damage = (ent->dmg * BOUNCY_CONFIG.cluster_damage_fraction);
    float cluster_radius = (ent->dmg_radius * BOUNCY_CONFIG.cluster_radius_fraction);

    T_RadiusDamage(ent, ent->owner, cluster_damage, nullptr, cluster_radius, DAMAGE_NONE, MOD_G_SPLASH);

    vec3_t origin = ent->s.origin + tr.plane.normal;
    gi.WriteByte(svc_temp_entity);
    gi.WriteByte(TE_GRENADE_EXPLOSION);
    gi.WritePosition(origin);
    gi.multicast(ent->s.origin, MULTICAST_PVS, false);

    gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/grenlb1b.wav"), 1, ATTN_NORM, 0);
}

/**
 * @brief The "think" function for the cluster grenade, acts as a failsafe timer.
 */
THINK(BouncyGrenade_Think)(edict_t* self) -> void
{
    if (level.time >= self->timestamp)
    {
        Grenade_Explode(self);
        return;
    }
    self->nextthink = level.time + FRAME_TIME_S;
}

/**
 * @brief The "touch" function for the cluster grenade, handles all impact logic.
 */
TOUCH(BouncyGrenade_Touch)(edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
    if (other == ent->owner) return;
    if (tr.surface && (tr.surface->flags & SURF_SKY)) { G_FreeEdict(ent); return; }

    edict_t* attacker = ent->owner;
    if (ent->owner && ent->owner->classname && Q_strcasecmp(ent->owner->classname, "monster_sentrygun") == 0) {
        attacker = ent->owner->owner;
    }

    // If we hit a damageable entity, it's always a final, full-damage explosion.
    if (other->takedamage)
    {
        Grenade_ExplodeReal(ent, other, tr.plane.normal, attacker, true);
        return;
    }

    // --- KEY CHANGE: Immediate Final Explosion on Last Bounce ---
    // If this is the last bounce (`count` is 1), this impact IS the final explosion.
    if (ent->count <= 1)
    {
        Grenade_ExplodeReal(ent, nullptr, tr.plane.normal, attacker, true);
        return;
    }

    // Otherwise, it's a regular cluster bounce.
    ent->count--;
    BouncyGrenade_ClusterExplode(ent, tr);
}

/**
 * Creates and fires a grenade projectile.
 */
void fire_grenade(edict_t* self, const vec3_t& start, const vec3_t& aimdir,
	int damage, int speed, gtime_t timer, float damage_radius,
	float right_adjust, float up_adjust, bool monster)
{
	if (!self || speed <= 0) return;

	vec3_t dir = vectoangles(aimdir);
	auto [forward, right, up] = AngleVectors(dir);

	edict_t* grenade = G_Spawn();
	if (!grenade) return;

	grenade->s.origin = start;
	grenade->velocity = aimdir * speed;

	if (up_adjust != 0.0f) {
		float const gravityAdjustment = level.gravity / 800.0f;
		grenade->velocity += up * up_adjust * gravityAdjustment;
	}
	if (right_adjust != 0.0f) {
		grenade->velocity += right * right_adjust;
	}

	grenade->movetype = MOVETYPE_BOUNCE;
	grenade->clipmask = MASK_PROJECTILE;
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

	const bool use_bouncy = (g_bouncygl->integer && !(self->svflags & SVF_MONSTER));

	if (use_bouncy) {
		// --- NEW Cluster-Bouncy Grenade Behavior ---
		grenade->think = BouncyGrenade_Think;
		grenade->touch = BouncyGrenade_Touch;
        grenade->nextthink = level.time + FRAME_TIME_S;
		grenade->timestamp = level.time + BOUNCY_CONFIG.life_time;
		grenade->count = BOUNCY_CONFIG.max_bounces;
		grenade->s.renderfx |= RF_MINLIGHT;

        // Apply damage and radius multipliers to make it more powerful
        grenade->dmg = static_cast<int>(grenade->dmg * BOUNCY_CONFIG.damage_multiplier);
        grenade->dmg_radius *= BOUNCY_CONFIG.radius_multiplier;
	}
	else if (monster) {
		// --- Monster-thrown grenade ---
		grenade->avelocity = { crandom() * 360, crandom() * 360, crandom() * 360 };
		grenade->nextthink = level.time + timer;
		grenade->think = Grenade_Explode;
		grenade->s.effects |= EF_GRENADE_LIGHT;
		grenade->touch = Grenade_Touch;
	}
	else {
		// --- Regular player grenade ---
		grenade->s.angles = vectoangles(grenade->velocity);
		grenade->nextthink = level.time + FRAME_TIME_S;
		grenade->timestamp = level.time + timer;
		grenade->think = Grenade4_Think;
		grenade->s.renderfx |= RF_MINLIGHT;
		grenade->touch = Grenade_Touch;
	}

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

// ... (rest of the file remains the same) ...

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

	// FIXED: Limit push force for players to prevent being knocked through walls
	if (other->client)
	{
		// Limit push force for players
		vec3_t limited_velocity = ent->velocity;
		float max_push = 300.0f;

		if (limited_velocity.length() > max_push)
			limited_velocity = limited_velocity.normalized() * max_push;

		// Use limited_velocity for damage calculation
		T_Damage(other, ent, ent->owner, limited_velocity, ent->s.origin,
			tr.plane.normal, ent->dmg, ent->dmg, DAMAGE_NONE, MOD_FIREBALL);
	}
	else if (other->takedamage)
	{
		// Normal damage for non-players
		T_Damage(other, ent, ent->owner, ent->velocity, ent->s.origin,
			tr.plane.normal, ent->dmg, ent->dmg, DAMAGE_NONE, MOD_FIREBALL);
	}
	else
	{
		// don't throw any debris in net games
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

	// FIXED: Reduced radius damage for players
	if (other->client)
	{
		//Reduced radius damage amount for players to prevent excessive knockback
		float reduced_radius_dmg = ent->radius_dmg * 0.7f;
		T_RadiusDamage(ent, ent->owner, reduced_radius_dmg, other, ent->dmg_radius, DAMAGE_NONE, MOD_R_SPLASH);
	}
	else
	{
		//Normal radius damage for non-players
		T_RadiusDamage(ent, ent->owner, (float)ent->radius_dmg, other, ent->dmg_radius, DAMAGE_NONE, MOD_R_SPLASH);
	}

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

// Added constants for clarity and optimization
constexpr float BFG_LASER_LENGTH = 2048.0f;     // Previously hardcoded as -2048.0f
constexpr float BFG_MIN_VELOCITY = 100.0f;      // Minimum velocity after reflection
constexpr int BFG_DMG_DEATHMATCH = 5;           // Damage in deathmatch
constexpr int BFG_DMG_SINGLEPLAYER = 10;        // Damage in singleplayer
constexpr int BFG_PULL_FORCE_GROUNDED = 20;     // Pull force when grounded
constexpr int BFG_PULL_FORCE_AIRBORNE = 10;     // Pull force when in air
constexpr int BFG_EXPLOSION_DAMAGE = 200;       // Explosion damage
constexpr float BFG_EXPLOSION_RADIUS = 100.0f;  // Explosion radius
constexpr float BFG_VELOCITY_EPSILON = 0.001f;  // Small threshold for velocity checks
constexpr int MAX_POOLED_LASERS = 128;          // Maximum lasers to keep in pool
constexpr int MAX_FORCE = 100;                  // Maximum pull force

// Laser object pool for efficient memory management
class LaserPool {
private:
	static std::vector<edict_t*> pool;
	static constexpr size_t MAX_POOL_SIZE = MAX_POOLED_LASERS;

public:
	// Get a laser from the pool or create a new one
	static edict_t* get() {
		if (pool.empty()) {
			return G_Spawn();
		}
		edict_t* laser = pool.back();
		pool.pop_back();
		return laser;
	}

	// Return a laser to the pool if there's room
	static void release(edict_t* laser) {
		if (!laser) return;

		if (pool.size() < MAX_POOL_SIZE) {
			// Reset any necessary fields to default values
			laser->s.frame = 0;
			laser->s.skinnum = 0;
			laser->think = nullptr;
			laser->nextthink = 0_ms;
			laser->timestamp = 0_ms;
			laser->owner = nullptr;
			pool.push_back(laser);
		}
		else {
			G_FreeEdict(laser);
		}
	}

	// Clear the pool (e.g., on level change)
	static void clear() {
		for (auto& laser : pool) {
			G_FreeEdict(laser);
		}
		pool.clear();
	}
};

// Static member initialization
std::vector<edict_t*> LaserPool::pool;

// Helper function to determine if an entity can be affected by BFG
inline bool can_be_affected_by_bfg(const edict_t* ent) {
	return (ent->svflags & SVF_MONSTER) ||
		(ent->flags & FL_DAMAGEABLE) ||
		ent->client ||
		(strcmp(ent->classname, "misc_explobox") == 0);
}

// Helper function to calculate entity center once (avoid repeated calculation)
inline vec3_t calculate_entity_center(const edict_t* ent) {
	return (ent->absmin + ent->absmax) * 0.5f;
}

/**
 * Calculates a random position around a point for BFG laser effect.
 * Uses spherical coordinates to evenly distribute positions.
 *
 * @param p The center point
 * @param dist Distance from center
 * @return A randomly generated position at the specified distance
 */
static vec3_t bfg_laser_pos(const vec3_t& p, float dist)
{
	// These are already floats, which is fine for the 'f' functions
	const float theta = frandom(2 * PIf);
	const float phi = acosf(crandom()); // Use acosf

	// Use sinf and cosf which return float
	const vec3_t d{
		sinf(phi) * cosf(theta), // Use sinf, cosf
		sinf(phi) * sinf(theta), // Use sinf, sinf
		cosf(phi)                // Use cosf
	};

	return p + (d * dist);
}

/**
 * Update function for BFG laser entities.
 * Keeps the laser attached to its owner and handles lifetime.
 */
THINK(bfg_laser_update) (edict_t* self) -> void
{
	// Check if the laser should be removed due to timeout or owner being freed
	if (level.time > self->timestamp || !self->owner || !self->owner->inuse)
	{
		// Return the laser to the pool instead of immediately freeing it
		LaserPool::release(self);
		return;
	}

	// Update laser position to match owner
	self->s.origin = self->owner->s.origin;

	// Schedule the next update
	self->nextthink = level.time + 1_ms;

	// Update the entity in the world
	gi.linkentity(self);
}

/**
 * Spawns a visual laser effect emanating from the BFG.
 * Uses object pooling for better memory efficiency.
 *
 * @param self The BFG entity
 */
static void bfg_spawn_laser(edict_t* self)
{
	// Calculate a random end position for the laser
	const vec3_t end = bfg_laser_pos(self->s.origin, BFG_LASER_DISTANCE);

	// Trace to find what the laser hits
	const trace_t tr = gi.traceline(self->s.origin, end, self, MASK_OPAQUE | CONTENTS_PROJECTILECLIP);

	// Don't spawn a laser if it doesn't hit anything
	if (tr.fraction == 1.0f)
		return;

	// Get a laser from the pool instead of always spawning new ones
	edict_t* laser = LaserPool::get();

	// Set up laser properties
	laser->s.frame = 3;
	laser->s.renderfx = RF_BEAM_LIGHTNING;
	laser->movetype = MOVETYPE_NONE;
	laser->solid = SOLID_NOT;
	laser->s.modelindex = MODELINDEX_WORLD; // must be non-zero
	laser->s.origin = self->s.origin;
	laser->s.old_origin = tr.endpos;
	laser->s.skinnum = 0xD0D0D0D0; // Color of the laser
	laser->think = bfg_laser_update;
	laser->nextthink = level.time + 1_ms;
	laser->timestamp = level.time + BFG_LASER_LIFETIME;
	laser->owner = self;

	// Link the entity into the world
	gi.linkentity(laser);
}

/**
 * Handles the BFG explosion animation and damage effects.
 * Creates visual lasers and damages entities in radius.
 */
THINK(bfg_explode) (edict_t* self) -> void
{
	// Spawn visual laser effect
	bfg_spawn_laser(self);

	// Only process damage on the first frame of explosion
	if (self->s.frame == 0)
	{
		// Use unordered_set to track processed entities
		std::unordered_set<edict_t*> processed_entities;

		// Find all entities in the damage radius
		edict_t* ent = nullptr;
		while ((ent = findradius(ent, self->s.origin, self->dmg_radius)) != nullptr)
		{
			// Skip invalid entities or those already processed
			if (!ent->takedamage || processed_entities.find(ent) != processed_entities.end())
				continue;

			if (ent == self->owner)
				continue;

			if (!CanDamage(ent, self) || !CanDamage(ent, self->owner))
				continue;

			if (!can_be_affected_by_bfg(ent))
				continue;

			if (CheckTeamDamage(ent, self->owner))
				continue;

			// Mark entity as processed
			processed_entities.insert(ent);

			// Calculate entity center once for efficiency
			const vec3_t centroid = calculate_entity_center(ent);

			// Calculate direction and distance
			const vec3_t diff = self->s.origin - centroid;
			const float dist_squared = diff.lengthSquared();
			const float dist = sqrtf(dist_squared);

			// Calculate damage
			const float points = self->radius_dmg * (1.0f - sqrtf(dist / self->dmg_radius));

			// Apply damage
			T_Damage(ent, self, self->owner, self->velocity, centroid, vec3_origin,
				(int)points, 0, DAMAGE_ENERGY, MOD_BFG_EFFECT);

			// Visual effect
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_BFG_ZAP);
			gi.WritePosition(self->s.origin);
			gi.WritePosition(centroid);
			gi.multicast(self->s.origin, MULTICAST_PHS, false);
		}
	}

	// Advance the explosion animation
	self->nextthink = level.time + 10_hz;
	self->s.frame++;

	// Free the entity when animation completes
	if (self->s.frame == 5)
		self->think = G_FreeEdict;
}

/**
 * Calculates the appropriate range for BFG effects based on owner type.
 *
 * @param self The BFG entity
 * @return Range value in world units, clamped to safe maximum
 */
int calculate_bfg_range(const edict_t* self)
{
	int range;

	if (self->owner->svflags & SVF_MONSTER) {
		// Monster-owned BFG has shorter range
		range = BFG_MONSTER_RANGE;
	}
	else if (g_bfgpull->integer && self->owner->client) {
		// Player-owned BFG with pull enabled has longer range
		range = BFG_PLAYER_RANGE;
	}
	else {
		// Default to monster range for other cases
		range = BFG_MONSTER_RANGE;
	}

	// Ensure the range doesn't exceed the maximum safe value
	return std::min(range, BFG_MAX_SAFE_RANGE);
}

/**
 * Calculates the appropriate pull force for an entity affected by BFG.
 * Applies different forces based on whether the entity is grounded.
 *
 * @param ent The entity being pulled
 * @return Force value to apply
 */
int calculate_pull_force(const edict_t* ent)
{
	// Apply stronger force if entity is grounded
	const int force = ent->groundentity ? BFG_PULL_FORCE_GROUNDED : BFG_PULL_FORCE_AIRBORNE;

	// Ensure force doesn't exceed maximum allowed value
	return std::min(force, MAX_FORCE);
}

/**
 * Structure for handling BFG laser piercing through multiple entities.
 * Inherits from pierce_args_t to provide custom piercing behavior.
 */
struct bfg_laser_pierce_t : pierce_args_t
{
	edict_t* self;    // The BFG entity
	vec3_t   dir;     // Direction of the laser
	int      damage;  // Damage to apply

	/**
	 * Constructor initializing pierce args for a BFG laser.
	 *
	 * @param self The BFG entity
	 * @param dir Direction vector of the laser
	 * @param damage Damage amount to apply
	 */
	inline bfg_laser_pierce_t(edict_t* self, const vec3_t& dir, int damage) :
		pierce_args_t(),
		self(self),
		dir(dir),
		damage(damage)
	{
	}

	/**
	 * Called when the pierce trace hits an entity.
	 * Applies damage and determines whether to continue piercing.
	 *
	 * @param mask Reference to the contents mask used for tracing
	 * @param end Reference to the end position of the trace
	 * @return true to continue piercing, false to stop
	 */
	bool hit(contents_t& mask, vec3_t& end) override
	{
		// Apply damage if the entity can take damage and isn't immune to lasers
		if ((tr.ent->takedamage) && !(tr.ent->flags & FL_IMMUNE_LASER) && (tr.ent != self->owner))
			T_Damage(tr.ent, self, self->owner, dir, tr.endpos, vec3_origin, damage, 1, DAMAGE_ENERGY, MOD_BFG_LASER);

		// Stop piercing if we hit a non-damageable object
		if (!can_be_affected_by_bfg(tr.ent))
		{
			// Visual effect for hitting a solid object
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_LASER_SPARKS);
			gi.WriteByte(4);
			gi.WritePosition(tr.endpos + tr.plane.normal);
			gi.WriteDir(tr.plane.normal);
			gi.WriteByte(208);
			gi.multicast(tr.endpos + tr.plane.normal, MULTICAST_PVS, false);
			return false;
		}

		// Check if we've already pierced this entity
		if (!mark(tr.ent))
			return false;

		// Continue piercing
		return true;
	}
};

/**
 * Main update function for active BFG projectiles.
 * Handles laser effects, damage application, and entity pulling.
 */
THINK(bfg_think) (edict_t* self) -> void
{
	// Early exit checks
	if (!self || !self->owner)
		return;

	// Determine expiry time
	const gtime_t expiry_time = (self->timestamp != 0_ms) ?
		self->timestamp :
		(self->air_finished + BFG_MAX_LIFETIME);

	// Free entity if it has expired
	if (level.time >= expiry_time)
	{
		G_FreeEdict(self);
		return;
	}

	// Spawn visual laser effect
	bfg_spawn_laser(self);

	// Calculate damage based on game mode
	const int dmg = deathmatch->integer ? BFG_DMG_DEATHMATCH : BFG_DMG_SINGLEPLAYER;

	// Calculate range for effects
	const int bfgrange = calculate_bfg_range(self);
	const int bfgrange_squared = bfgrange * bfgrange;

	// Determine if pulling should be applied
	const bool should_pull = g_bfgpull->integer && self->owner->client;

	// Cache origin for performance
	const vec3_t self_origin = self->s.origin;

	// Use unordered_set for efficient entity tracking
	std::unordered_set<edict_t*> processed_entities;

	// Find all entities in range
	edict_t* ent = nullptr;
	while ((ent = findradius(ent, self_origin, bfgrange)) != nullptr)
	{
		// Skip entities that can't be damaged
		if (!ent->takedamage || ent == self || ent == self->owner)
			continue;

		// Skip entities that shouldn't be affected by BFG
		if (!can_be_affected_by_bfg(ent))
			continue;

		// Skip team members
		if (CheckTeamDamage(ent, self->owner))
			continue;

		// Calculate entity center once
		const vec3_t point = calculate_entity_center(ent);

		// Calculate direction and distance squared for efficiency
		vec3_t dir = self_origin - point;
		const float dist_squared = dir.lengthSquared();

		// Skip entities outside range
		if (dist_squared > bfgrange_squared)
			continue;

		// Calculate actual distance and normalize direction
		const float dist = sqrtf(dist_squared);
		if (dist > BFG_VELOCITY_EPSILON) {
			dir *= (1.0f / dist); // Normalize efficiently
		}
		else {
			// Handle zero distance case
			dir = vec3_t{ 0, 0, 1 }; // Default up direction
		}

		// Skip entities we've already processed
		if (processed_entities.find(ent) != processed_entities.end())
			continue;

		// Check visibility
		trace_t tr = gi.traceline(self_origin, point, nullptr, CONTENTS_SOLID | CONTENTS_PROJECTILECLIP);
		if (tr.fraction < 1.0f)
			continue; // Not visible

		// Mark entity as processed
		processed_entities.insert(ent);

		// Apply damage
		T_Damage(ent, self, self->owner, dir, point, vec3_origin, dmg, 1, DAMAGE_ENERGY, MOD_BFG_LASER);

		// Calculate laser endpoint for visuals
		vec3_t laser_end = self_origin + (dir * -BFG_LASER_LENGTH);

		// Perform piercing trace
		trace_t pierce_tr = gi.traceline(self_origin, laser_end, nullptr, CONTENTS_SOLID | CONTENTS_PROJECTILECLIP);
		bfg_laser_pierce_t pierce_args{ self, dir, dmg };
		pierce_trace(self_origin, laser_end, self, pierce_args,
			CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER |
			CONTENTS_DEADMONSTER | CONTENTS_PROJECTILECLIP);
		laser_end = pierce_tr.endpos; // Update laser end to actual pierce end point

		// Apply pull effect if enabled
		if (should_pull && !OnSameTeam(ent, self->owner) && ent->movetype != MOVETYPE_NONE && ent->movetype != MOVETYPE_PUSH)
		{
			// Calculate pull force based on entity state
			const int pull_force = calculate_pull_force(ent);

			// Apply velocity change
			ent->velocity -= dir * pull_force;

			// Break ground contact if pulling strongly
			if (ent->groundentity && pull_force >= BFG_PULL_FORCE_GROUNDED)
				ent->groundentity = nullptr;

			// Apply knockback with consolidated call
			T_Damage(ent, self, self->owner, dir, point, vec3_origin, 0,
				pull_force, DAMAGE_ENERGY, MOD_BFG_LASER);
		}

		// Visual laser effect
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BFG_LASER);
		gi.WritePosition(self_origin);
		gi.WritePosition(laser_end);
		gi.multicast(self_origin, MULTICAST_PHS, false);
	}

	// Calculate next think time based on game mode
	const gtime_t next_think_time = g_bfgslide->integer ?
		FRAME_TIME_MS * 1.6 :  // Slightly faster for slide mode
		FRAME_TIME_MS * 2.5;   // Slightly slower for normal mode

	// Schedule next update
	self->nextthink = level.time + next_think_time;
}

/**
 * Handles what happens when a BFG projectile touches something.
 * Either causes it to slide along surfaces or explode on impact.
 */
TOUCH(bfg_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	// Ignore collisions with owner
	if (other == self->owner)
		return;

	// Destroy entity if it hits the sky
	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	// Play noise if owner is a player
	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	// Handle sliding mode
	if (g_bfgslide->integer) {
		// Set expiry timestamp if not already set
		if (self->timestamp == 0_ms) {
			self->timestamp = level.time + BFG_WALL_EXPIRE_TIME;
		}

		// Calculate new velocity more efficiently
		const float oldVelocitySq = self->velocity.lengthSquared();
		const float oldVelocity = std::sqrt(oldVelocitySq);

		// Target velocity is twice the initial speed minus current speed
		float newVelocity = (2 * BFG10K_INITIAL_SPEED) - oldVelocity;

		// Clamp velocity between minimum and original
		newVelocity = std::max(BFG_MIN_VELOCITY, std::min(newVelocity, oldVelocity));

		// Apply new velocity without redundant normalization if possible
		if (oldVelocity > BFG_VELOCITY_EPSILON) {
			// Scale existing velocity vector (more efficient than normalize + multiply)
			self->velocity *= (newVelocity / oldVelocity);
		}
		else {
			// In case velocity is near zero, use a default direction
			self->velocity = vec3_t{ 1, 0, 0 } *newVelocity;
		}
	}
	else
	{
		// Non-slide behavior - apply damage and explode
		if (other->takedamage)
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal,
				BFG_EXPLOSION_DAMAGE, 0, DAMAGE_ENERGY, MOD_BFG_BLAST);

		// Apply radius damage
		T_RadiusDamage(self, self->owner, BFG_EXPLOSION_DAMAGE, other, BFG_EXPLOSION_RADIUS,
			DAMAGE_ENERGY, MOD_BFG_BLAST);

		// Play explosion sound
		gi.sound(self, CHAN_VOICE, gi.soundindex("weapons/bfg__x1b.wav"), 1, ATTN_NORM, 0);

		// Make projectile non-solid
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

		// Visual explosion effect
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BFG_BIGEXPLOSION);
		gi.WritePosition(self->s.origin);
		gi.multicast(self->s.origin, MULTICAST_PHS, false);
	}
}

/**
 * Creates and fires a BFG projectile.
 *
 * @param self The entity firing the BFG
 * @param start Starting position of the projectile
 * @param dir Direction vector for the projectile
 * @param damage Base damage value
 * @param speed Initial speed of the projectile
 * @param damage_radius Radius for explosion damage
 */
void fire_bfg(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, float damage_radius)
{
	// Create a new BFG entity
	edict_t* bfg = G_Spawn();

	// Set position and movement
	bfg->s.origin = start;
	bfg->s.angles = vectoangles(dir);
	bfg->velocity = dir * speed;
	bfg->movetype = g_bfgslide->integer ? MOVETYPE_SLIDE : MOVETYPE_FLYMISSILE;
	bfg->clipmask = MASK_PROJECTILE;
	bfg->svflags = SVF_PROJECTILE;

	// Special handling for player-fired BFGs
	if (self->client && !G_ShouldPlayersCollide(true))
		bfg->clipmask &= ~CONTENTS_PLAYER;

	// Set physical properties
	bfg->solid = SOLID_BBOX;
	bfg->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	bfg->s.modelindex = gi.modelindex("sprites/s_bfg1.sp2");

	// Set ownership and callbacks
	bfg->owner = self;
	bfg->touch = bfg_touch;
	bfg->nextthink = level.time + FRAME_TIME_S;
	bfg->think = bfg_think;

	// Set damage properties
	bfg->radius_dmg = damage;
	bfg->dmg_radius = damage_radius;
	bfg->classname = "bfg blast";
	bfg->s.sound = gi.soundindex("weapons/bfg__l1a.wav");

	// Set timing properties
	bfg->timestamp = 0_ms;
	bfg->air_finished = level.time;

	// Set team properties
	bfg->teammaster = bfg;
	bfg->teamchain = nullptr;

	// Link entity into the world
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