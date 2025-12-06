#include "../shared.h"
#include "../g_local.h"
#include "../m_player.h"

// ***************************
//  DOPPLEGANGER
// ***************************

edict_t* Sphere_Spawn(edict_t* owner, spawnflags_t spawnflags);

// Touch function for doppelgangers - allows anyone to push them (like bot-summoned entities)
TOUCH(doppleganger_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	// Bots pass through all doppelgangers (no collision)
	if (other->svflags & SVF_BOT)
		return;

	// Validate that the toucher is a client and owner exists
	if (!other->client || !self->teammaster || !self->teammaster->client)
		return;

	// Doppelgangers work like bot-summoned entities - anyone can push them
	// This helps prevent players and bots from getting stuck

	// Don't push if player is not on ground or not touching properly
	if (!other->groundentity || !other_touching_self)
		return;

	// Calculate push direction and strength
	vec3_t push_dir;
	float push_speed = 400.0f; // Same speed as summoned stroggs

	// Check if player is looking up (towards sky/roof)
	if (other->client->v_angle[PITCH] < -45.0f) // Looking up more than 45 degrees
	{
		// Vertical push - launch the doppleganger upward
		push_dir = { 0, 0, 1 }; // Straight up
		self->velocity[2] = push_speed * 1.5f; // Strong vertical push

		// Add some forward momentum based on view
		vec3_t forward;
		AngleVectors(other->client->v_angle, forward, nullptr, nullptr);
		self->velocity[0] += forward[0] * push_speed * 0.3f;
		self->velocity[1] += forward[1] * push_speed * 0.3f;

		// Make sure doppleganger is off ground for the jump
		self->groundentity = nullptr;
	}
	else
	{
		// Horizontal push based on player's view direction
		vec3_t forward;
		AngleVectors(other->client->v_angle, forward, nullptr, nullptr);

		push_dir[0] = forward[0];
		push_dir[1] = forward[1];
		push_dir[2] = 0; // Horizontal only

		push_dir.normalize();

		// Apply horizontal push
		self->velocity[0] = push_dir[0] * push_speed;
		self->velocity[1] = push_dir[1] * push_speed;
	}
}

DIE(doppleganger_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	edict_t* sphere;
	float	 dist;
	vec3_t	 dir;

	RemoveEntityFromGlobalList(self);

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
	T_RadiusDamage(self, self->teammaster, (float)g_config.doppleganger.explosion_damage, self, (float)g_config.doppleganger.explosion_radius, DAMAGE_NONE, MOD_DOPPLE_EXPLODE);

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

	// Track the base's position (base is teammaster)
	if (self->teammaster && self->teammaster->inuse)
	{
		self->s.origin = self->teammaster->s.origin;
		self->s.origin[2] += 8; // Offset body slightly above base
	}

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

	gi.linkentity(self); // Relink after position update
	self->nextthink = level.time + FRAME_TIME_MS;
}

void fire_doppleganger(edict_t* ent, const vec3_t& start, const vec3_t& aimdir)
{
	edict_t* base;
	edict_t* body;
	edict_t* sphere; // Add a pointer for the sphere
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
	base->health = g_config.doppleganger.health_base;
	base->pain = doppleganger_pain;
	base->die = doppleganger_die;
	base->touch = doppleganger_touch; // Allow players to push doppelgangers

	base->nextthink = level.time + gtime_t::from_sec(g_config.doppleganger.time_to_live_sec);
	base->think = doppleganger_timeout;

	base->classname = "doppleganger";

	base->monsterinfo.isfriendlyspawn = true;

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
	body->monsterinfo.isfriendlyspawn = true;
	body->classname = "doppleganger";  //this is the visual, shouldn't get stats from id view here

	// Team assignment (Newly Added)
	if (ent->client) {
		base->ctf_team = ent->client->resp.ctf_team;
		body->ctf_team = ent->client->resp.ctf_team;
	}

	gi.linkentity(body);

	base->teamchain = body;
	body->teammaster = base;

	// Spawn the defender sphere. Sphere_Spawn correctly sets sphere->owner = base.
	sphere = Sphere_Spawn(base, SPHERE_DEFENDER);

	// FIX: Make the doppelganger base own the sphere, not the player who created it
	// This prevents crashes when the player disconnects
	Own_Sphere(base, sphere);

	// [Paril-KEX]
	body->owner = ent;

	gi.sound(body, CHAN_AUTO, gi.soundindex("medic_commander/monsterspawn1.wav"), 1.f, ATTN_NORM, 0.f);
}