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

	// Store attacker info in case owner dies before projectile hits
	if (self) {
		if (self->client) {
			bolt->projectile_was_player_attacker = true;
			bolt->projectile_attacker_type_id = 0;
		} else if (self->svflags & SVF_MONSTER) {
			bolt->projectile_was_player_attacker = false;
			bolt->projectile_attacker_type_id = self->monsterinfo.monster_type_id;
		}
	}

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

	if (self->owner && self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal, self->dmg, 1, DAMAGE_ENERGY, MOD_RIPPER);
	}
	else
	{
        // This 'return' was causing the projectile to persist after hitting a non-damageable surface.
        // It should be freed instead.
		G_FreeEdict(self);
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

	// Store attacker info in case owner dies before projectile hits
	if (self) {
		if (self->client) {
			ion->projectile_was_player_attacker = true;
			ion->projectile_attacker_type_id = 0;
		} else if (self->svflags & SVF_MONSTER) {
			ion->projectile_was_player_attacker = false;
			ion->projectile_attacker_type_id = self->monsterinfo.monster_type_id;
		}
	}

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
        // FIX: Parentheses added to group the OR conditions and fix a null-dereference bug.
        if (ent->owner && (horde::IsMonsterType(ent->owner, horde::MonsterTypeID::FIXBOT) ||
                           horde::IsMonsterType(ent->owner, horde::MonsterTypeID::FIXBOT_KL)))
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

	// Store attacker info in case owner dies before projectile hits
	if (self) {
		if (self->client) {
			plasma->projectile_was_player_attacker = true;
			plasma->projectile_attacker_type_id = 0;
		} else if (self->svflags & SVF_MONSTER) {
			plasma->projectile_was_player_attacker = false;
			plasma->projectile_attacker_type_id = self->monsterinfo.monster_type_id;
		}
	}

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