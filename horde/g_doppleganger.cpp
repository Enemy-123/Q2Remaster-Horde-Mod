#include "../shared.h"
#include "../g_local.h"
#include "../m_player.h"

// ***************************
//  DOPPLEGANGER
// ***************************

edict_t* Sphere_Spawn(edict_t* owner, spawnflags_t spawnflags);

DIE(doppleganger_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	edict_t* sphere;
	float	 dist;
	vec3_t	 dir;

    auto& vec = g_targetable_special_entities;
    vec.erase(std::remove(vec.begin(), vec.end(), self), vec.end());

	if ((self->enemy) && (self->enemy != self->teammaster))
	{
		dir = self->enemy->s.origin - self->s.origin;
		dist = dir.length();

		if (dist > 80.f)
		{
			if (dist > 768)
			{
				sphere = Sphere_Spawn(self, SPHERE_HUNTER | SPHERE_DOPPLEGANGER);
				sphere->pain(sphere, attacker, 0, 0, mod);
			}
			else
			{
				sphere = Sphere_Spawn(self, SPHERE_VENGEANCE | SPHERE_DOPPLEGANGER);
				sphere->pain(sphere, attacker, 0, 0, mod);
			}
		}
	}

	self->takedamage = DAMAGE_NONE;

	// [Paril-KEX]
	T_RadiusDamage(self, self->teammaster, 160.f, self, 140.f, DAMAGE_NONE, MOD_DOPPLE_EXPLODE);

	if (self->teamchain)
		BecomeExplosion1(self->teamchain);
	BecomeExplosion1(self);
}

PAIN(doppleganger_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	self->enemy = other;
}

THINK(doppleganger_timeout) (edict_t* self) -> void
{
	doppleganger_die(self, self, self, 9999, self->s.origin, MOD_UNKNOWN);
}

THINK(body_think) (edict_t* self) -> void
{
	float r;

	if (fabsf(self->ideal_yaw - anglemod(self->s.angles[YAW])) < 2)
	{
		if (self->timestamp < level.time)
		{
			r = frandom();
			if (r < 0.10f)
			{
				self->ideal_yaw = frandom(350.0f);
				self->timestamp = level.time + 1_sec;
			}
		}
	}
	else
		M_ChangeYaw(self);

	if (self->teleport_time <= level.time)
	{
		self->s.frame++;
		if (self->s.frame > FRAME_stand40)
			self->s.frame = FRAME_stand01;

		self->teleport_time = level.time + 10_hz;
	}

	self->nextthink = level.time + FRAME_TIME_MS;
}

void fire_doppleganger(edict_t* ent, const vec3_t& start, const vec3_t& aimdir)
{
	edict_t* base;
	edict_t* body;
	vec3_t	 dir;
	vec3_t	 forward, right, up;
	int		 number;

	dir = vectoangles(aimdir);
	AngleVectors(dir, forward, right, up);

	base = G_Spawn();
	base->s.origin = start;
	base->s.angles = dir;
	base->movetype = MOVETYPE_TOSS;
	base->solid = SOLID_BBOX;
	base->s.renderfx |= RF_IR_VISIBLE;
	base->s.angles[PITCH] = 0;
	base->mins = { -16, -16, -24 };
	base->maxs = { 16, 16, 32 };
	base->s.modelindex = gi.modelindex("models/objects/dopplebase/tris.md2");
	base->s.alpha = 0.1f;
	base->teammaster = ent;
	base->flags |= (FL_DAMAGEABLE | FL_TRAP);
	base->takedamage = true;
	base->health = 30;
	base->pain = doppleganger_pain;
	base->die = doppleganger_die;

	base->nextthink = level.time + 30_sec;
	base->think = doppleganger_timeout;

	base->classname = "doppleganger";

	base->monsterinfo.issummoned = true;

    base->special_type_id = static_cast<uint8_t>(horde::SpecialTypeRegistry::GetTypeID(base->classname));
    g_targetable_special_entities.push_back(base);

	gi.linkentity(base);

	body = G_Spawn();
	number = body->s.number;
	body->s = ent->s;
	body->s.sound = 0;
	body->s.event = EV_NONE;
	body->s.number = number;
	body->yaw_speed = 30;
	body->ideal_yaw = 0;
	body->s.origin = start;
	body->s.origin[2] += 8;
	body->teleport_time = level.time + 10_hz;
	body->think = body_think;
	body->nextthink = level.time + FRAME_TIME_MS;
	body->monsterinfo.issummoned = true;
    body->classname = "doppleganger";  //this is the visual, shouldn't get stats from id view here
	
	gi.linkentity(body);

	base->teamchain = body;
	body->teammaster = base;

	// [Paril-KEX]
	body->owner = ent;
	//base->special_type_id = static_cast<uint8_t>(horde::SpecialTypeRegistry::GetTypeID(base->classname));

	gi.sound(body, CHAN_AUTO, gi.soundindex("medic_commander/monsterspawn1.wav"), 1.f, ATTN_NORM, 0.f);
}