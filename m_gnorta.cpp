// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

gnorta

==============================================================================
*/

#include "g_local.h"
#include "m_gnorta.h"
#include <float.h>
#include "shared.h"

static cached_soundindex sound_pain;
static cached_soundindex sound_die;
static cached_soundindex sound_sight;
static cached_soundindex sound_punch_hit1;
static cached_soundindex sound_punch_hit2;
static cached_soundindex sound_idle;

bool gnorta_do_pounce(edict_t* self, const vec3_t& dest);
void gnorta_walk(edict_t* self);
void gnorta_dodge_jump(edict_t* self);
void gnorta_swing_attack(edict_t* self);
void gnorta_jump_straightup(edict_t* self);
void gnorta_jump_wait_land(edict_t* self);
bool gnorta_ok_to_transition(edict_t* self);
void gnorta_stand(edict_t* self);

inline bool gnorta_ON_CEILING(edict_t* ent)
{
	return (ent->gravityVector[2] > 0);
}

//=========================
//=========================
bool gnorta_ok_to_transition(edict_t* self)
{
	trace_t trace;
	vec3_t	pt, start;
	float	max_dist;
	float	margin;
	float	end_height;

	if (gnorta_ON_CEILING(self))
	{
		// [Paril-KEX] if we get knocked off the ceiling, always
		// fall downwards
		if (!self->groundentity)
			return true;

		max_dist = -384;
		margin = self->mins[2] - 8;
	}
	else
	{
		max_dist = 180;
		margin = self->maxs[2] + 8;
	}

	pt = self->s.origin;
	pt[2] += max_dist;
	trace = gi.trace(self->s.origin, self->mins, self->maxs, pt, self, MASK_MONSTERSOLID);

	if (trace.fraction == 1.0f ||
		!(trace.contents & CONTENTS_SOLID) ||
		(trace.ent != world))
	{
		if (gnorta_ON_CEILING(self))
		{
			if (trace.plane.normal[2] < 0.9f)
				return false;
		}
		else
		{
			if (trace.plane.normal[2] > -0.9f)
				return false;
		}
	}

	end_height = trace.endpos[2];

	// check the four corners, tracing only to the endpoint of the center trace (vertically).
	pt[0] = self->absmin[0];
	pt[1] = self->absmin[1];
	pt[2] = trace.endpos[2] + margin; // give a little margin of error to allow slight inclines
	start = pt;
	start[2] = self->s.origin[2];
	trace = gi.traceline(start, pt, self, MASK_MONSTERSOLID);
	if (trace.fraction == 1.0f || !(trace.contents & CONTENTS_SOLID) || (trace.ent != world))
		return false;
	if (fabsf(end_height + margin - trace.endpos[2]) > 8)
		return false;

	pt[0] = self->absmax[0];
	pt[1] = self->absmin[1];
	start = pt;
	start[2] = self->s.origin[2];
	trace = gi.traceline(start, pt, self, MASK_MONSTERSOLID);
	if (trace.fraction == 1.0f || !(trace.contents & CONTENTS_SOLID) || (trace.ent != world))
		return false;
	if (fabsf(end_height + margin - trace.endpos[2]) > 8)
		return false;

	pt[0] = self->absmax[0];
	pt[1] = self->absmax[1];
	start = pt;
	start[2] = self->s.origin[2];
	trace = gi.traceline(start, pt, self, MASK_MONSTERSOLID);
	if (trace.fraction == 1.0f || !(trace.contents & CONTENTS_SOLID) || (trace.ent != world))
		return false;
	if (fabsf(end_height + margin - trace.endpos[2]) > 8)
		return false;

	pt[0] = self->absmin[0];
	pt[1] = self->absmax[1];
	start = pt;
	start[2] = self->s.origin[2];
	trace = gi.traceline(start, pt, self, MASK_MONSTERSOLID);
	if (trace.fraction == 1.0f || !(trace.contents & CONTENTS_SOLID) || (trace.ent != world))
		return false;
	if (fabsf(end_height + margin - trace.endpos[2]) > 8)
		return false;

	return true;
}

//=========================
//=========================
MONSTERINFO_SIGHT(gnorta_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

// ******************
// IDLE
// ******************

void gnorta_idle_noise(edict_t* self)
{
	gi.sound(self, CHAN_VOICE, sound_idle, 0.5, ATTN_IDLE, 0);
}

mframe_t gnorta_frames_idle[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand },
	{ ai_stand, 0, gnorta_idle_noise },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand }
};
MMOVE_T(gnorta_move_idle) = { FRAME_idle01, FRAME_idle21, gnorta_frames_idle, gnorta_stand };

mframe_t gnorta_frames_idle2[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand },
	{ ai_stand },
	{ ai_stand }
};
MMOVE_T(gnorta_move_idle2) = { FRAME_idle201, FRAME_idle213, gnorta_frames_idle2, gnorta_stand };

MONSTERINFO_IDLE(gnorta_idle) (edict_t* self) -> void
{
	if (frandom() < 0.35f)
		M_SetAnimation(self, &gnorta_move_idle);
	else
		M_SetAnimation(self, &gnorta_move_idle2);
}

// ******************
// STAND
// ******************

mframe_t gnorta_frames_stand[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand },
	{ ai_stand, 0, gnorta_idle_noise },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand }
};
MMOVE_T(gnorta_move_stand) = { FRAME_idle01, FRAME_idle21, gnorta_frames_stand, gnorta_stand };

MONSTERINFO_STAND(gnorta_stand) (edict_t* self) -> void
{
	if (frandom() < 0.25f)
		M_SetAnimation(self, &gnorta_move_stand);
	else
		M_SetAnimation(self, &gnorta_move_idle2);
}

// ******************
// RUN
// ******************

mframe_t gnorta_frames_run[] = {
	{ ai_run, 13, monster_footstep },
	{ ai_run, 17 },
	{ ai_run, 21, monster_footstep },
	{ ai_run, 18 }
};
MMOVE_T(gnorta_move_run) = { FRAME_run01, FRAME_run04, gnorta_frames_run, nullptr };

MONSTERINFO_RUN(gnorta_run) (edict_t* self) -> void
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &gnorta_move_stand);
	else
		M_SetAnimation(self, &gnorta_move_run);
}

// ******************
// WALK
// ******************

mframe_t gnorta_frames_walk[] = {
	{ ai_walk, 4, monster_footstep },
	{ ai_walk, 6 },
	{ ai_walk, 8 },
	{ ai_walk, 5 },

	{ ai_walk, 4, monster_footstep },
	{ ai_walk, 6 },
	{ ai_walk, 8 },
	{ ai_walk, 4 }
};
MMOVE_T(gnorta_move_walk) = { FRAME_walk01, FRAME_walk08, gnorta_frames_walk, gnorta_walk };

MONSTERINFO_WALK(gnorta_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &gnorta_move_walk);
}


// ******************
// PAIN
// ******************

mframe_t gnorta_frames_pain[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(gnorta_move_pain) = { FRAME_pain01, FRAME_pain04, gnorta_frames_pain, gnorta_run };

PAIN(gnorta_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (self->deadflag)
		return;

	if (self->groundentity == nullptr)
		return;

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;

	gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

	if (mod.id == MOD_CHAINFIST || damage > 10) // don't react unless the damage was significant
	{
		// gnorta should dodge jump periodically to help avoid damage.
		if (self->groundentity && (frandom() < 0.5f))
			gnorta_dodge_jump(self);
		else if (M_ShouldReactToPain(self, mod)) // no pain anims in nightmare
			M_SetAnimation(self, &gnorta_move_pain);
	}
}

MONSTERINFO_SETSKIN(gnorta_setskin) (edict_t* self) -> void
{
	//if (self->health < (self->max_health / 2))
	//	self->s.skinnum = 1;
	//else
	//	self->s.skinnum = 0;
}

// ******************
// gnorta ATTACK
// ******************

// ******************
// close combat
// ******************

void gnorta_swing_attack(edict_t* self)
{
	vec3_t aim = { MELEE_DISTANCE, 0, 0 };
	if (fire_hit(self, aim, irandom(5, 10), 50))
	{
		if (self->s.frame < FRAME_attack08)
			gi.sound(self, CHAN_WEAPON, sound_punch_hit2, 1, ATTN_NORM, 0);
		else
			gi.sound(self, CHAN_WEAPON, sound_punch_hit1, 1, ATTN_NORM, 0);
	}
	else
		self->monsterinfo.melee_debounce_time = level.time + 0.8_sec;
}

mframe_t gnorta_frames_swing_l[] = {
	{ ai_charge, 2 },
	{ ai_charge, 4 },
	{ ai_charge, 6 },
	{ ai_charge, 10, monster_footstep },

	{ ai_charge, 5, gnorta_swing_attack },
	{ ai_charge, 5 },
	{ ai_charge, 5 },
	{ ai_charge, 5, monster_footstep } // gnorta_swing_check_l
};
MMOVE_T(gnorta_move_swing_l) = { FRAME_attack01, FRAME_attack08, gnorta_frames_swing_l, gnorta_run };

mframe_t gnorta_frames_swing_r[] = {
	{ ai_charge, 4 },
	{ ai_charge, 6, monster_footstep },
	{ ai_charge, 6, gnorta_swing_attack },
	{ ai_charge, 10 },
	{ ai_charge, 5, monster_footstep } // gnorta_swing_check_r
};
MMOVE_T(gnorta_move_swing_r) = { FRAME_attack11, FRAME_attack15, gnorta_frames_swing_r, gnorta_run };

MONSTERINFO_MELEE(gnorta_attack_melee) (edict_t* self) -> void
{
	if (!has_valid_enemy(self))
		return;

	if (frandom() < 0.5f)
		M_SetAnimation(self, &gnorta_move_swing_l);
	else
		M_SetAnimation(self, &gnorta_move_swing_r);
}

// ******************
// POUNCE
// ******************

// ====================
// ====================
bool gnorta_check_lz(edict_t* self, edict_t* target, const vec3_t& dest)
{
	if ((gi.pointcontents(dest) & MASK_WATER) || (target->waterlevel))
		return false;

	if (!target->groundentity)
		return false;

	vec3_t jumpLZ;

	// check under the player's four corners
	// if they're not solid, bail.
	jumpLZ[0] = self->enemy->mins[0];
	jumpLZ[1] = self->enemy->mins[1];
	jumpLZ[2] = self->enemy->mins[2] - 0.25f;
	if (!(gi.pointcontents(jumpLZ) & MASK_SOLID))
		return false;

	jumpLZ[0] = self->enemy->maxs[0];
	jumpLZ[1] = self->enemy->mins[1];
	if (!(gi.pointcontents(jumpLZ) & MASK_SOLID))
		return false;

	jumpLZ[0] = self->enemy->maxs[0];
	jumpLZ[1] = self->enemy->maxs[1];
	if (!(gi.pointcontents(jumpLZ) & MASK_SOLID))
		return false;

	jumpLZ[0] = self->enemy->mins[0];
	jumpLZ[1] = self->enemy->maxs[1];
	if (!(gi.pointcontents(jumpLZ) & MASK_SOLID))
		return false;

	return true;
}

// ====================
// ====================
bool gnorta_do_pounce(edict_t* self, const vec3_t& dest)
{
	vec3_t	dist;
	float	length;
	vec3_t	jumpAngles;
	vec3_t	jumpLZ;
	float	velocity = 400.1f;

	// don't pounce when we're on the ceiling
	if (gnorta_ON_CEILING(self))
		return false;

	if (!gnorta_check_lz(self, self->enemy, dest))
		return false;

	dist = dest - self->s.origin;

	// make sure we're pointing in that direction 15deg margin of error.
	jumpAngles = vectoangles(dist);
	if (fabsf(jumpAngles[YAW] - self->s.angles[YAW]) > 45)
		return false; // not facing the player...

	if (isnan(jumpAngles[YAW]))
		return false; // Switch why

	self->ideal_yaw = jumpAngles[YAW];
	M_ChangeYaw(self);

	length = dist.length();
	if (length > 450)
		return false; // can't jump that far...

	jumpLZ = dest;
	vec3_t dir = dist.normalized();

	// find a valid angle/velocity combination
	while (velocity <= 800)
	{
		if (M_CalculatePitchToFire(self, jumpLZ, self->s.origin, dir, velocity, 3, false, true))
			break;

		velocity += 200;
	}

	// nothing found
	if (velocity > 800)
		return false;

	self->velocity = dir * velocity;
	return true;
}

// ******************
// DODGE
// ******************

//===================
// gnorta_jump_straightup
//===================
void gnorta_jump_straightup(edict_t* self)
{
	if (self->deadflag)
		return;

	if (gnorta_ON_CEILING(self))
	{
		if (gnorta_ok_to_transition(self))
		{
			self->gravityVector[2] = -1;
			self->s.angles[2] += 180.0f;
			if (self->s.angles[2] > 360.0f)
				self->s.angles[2] -= 360.0f;
			self->groundentity = nullptr;
		}
	}
	else if (self->groundentity) // make sure we're standing on SOMETHING...
	{
		self->velocity[0] += crandom() * 5;
		self->velocity[1] += crandom() * 5;
		self->velocity[2] += -400 * self->gravityVector[2];
		if (gnorta_ok_to_transition(self))
		{
			self->gravityVector[2] = 1;
			self->s.angles[2] = 180.0;
			self->groundentity = nullptr;
		}
	}
}

mframe_t gnorta_frames_jump_straightup[] = {
	{ ai_move, 1, gnorta_jump_straightup },
	{ ai_move, 1, gnorta_jump_wait_land },
	{ ai_move, -1, monster_footstep },
	{ ai_move, -1 }
};

MMOVE_T(gnorta_move_jump_straightup) = { FRAME_jump04, FRAME_jump07, gnorta_frames_jump_straightup, gnorta_run };

//===================
// gnorta_dodge_jump - abstraction so pain function can trigger a dodge jump too without
//		faking the inputs to gnorta_dodge
//===================
void gnorta_dodge_jump(edict_t* self)
{
	M_SetAnimation(self, &gnorta_move_jump_straightup);
}

#if 0
mframe_t gnorta_frames_dodge_run[] = {
	{ ai_run, 13 },
	{ ai_run, 17 },
	{ ai_run, 21 },
	{ ai_run, 18, monster_done_dodge }
};
MMOVE_T(gnorta_move_dodge_run) = { FRAME_run01, FRAME_run04, gnorta_frames_dodge_run, nullptr };
#endif

MONSTERINFO_DODGE(gnorta_dodge) (edict_t* self, edict_t* attacker, gtime_t eta, trace_t* tr, bool gravity) -> void
{
	if (!self->groundentity || self->health <= 0)
		return;

	if (!self->enemy)
	{
		self->enemy = attacker;
		FoundTarget(self);
		return;
	}

	// PMM - don't bother if it's going to hit anyway; fix for weird in-your-face etas (I was
	// seeing numbers like 13 and 14)
	if ((eta < FRAME_TIME_MS) || (eta > 5_sec))
		return;

	if (self->timestamp > level.time)
		return;

	self->timestamp = level.time + random_time(1_sec, 5_sec);
	// this will override the foundtarget call of gnorta_run
	gnorta_dodge_jump(self);
}

// ******************
// Jump onto / off of things
// ******************

//===================
//===================
void gnorta_jump_down(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 100);
	self->velocity += (up * 300);
}

//===================
//===================
void gnorta_jump_up(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 200);
	self->velocity += (up * 450);
}

//===================
//===================
void gnorta_jump_wait_land(edict_t* self)
{
	if (self->groundentity == nullptr)
	{
		self->gravity = 1.3f;
		self->monsterinfo.nextframe = self->s.frame;

		if (monster_jump_finished(self))
		{
			self->gravity = 1;
			self->monsterinfo.nextframe = self->s.frame + 1;
		}
	}
	else
	{
		self->gravity = 1;
		self->monsterinfo.nextframe = self->s.frame + 1;
	}
}

mframe_t gnorta_frames_jump_up[] = {
	{ ai_move, -8 },
	{ ai_move, -8 },
	{ ai_move, -8 },
	{ ai_move, -8 },

	{ ai_move, 0, gnorta_jump_up },
	{ ai_move, 0, gnorta_jump_wait_land },
	{ ai_move, 0, monster_footstep }
};
MMOVE_T(gnorta_move_jump_up) = { FRAME_jump01, FRAME_jump07, gnorta_frames_jump_up, gnorta_run };

mframe_t gnorta_frames_jump_down[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move, 0, gnorta_jump_down },
	{ ai_move, 0, gnorta_jump_wait_land },
	{ ai_move, 0, monster_footstep }
};
MMOVE_T(gnorta_move_jump_down) = { FRAME_jump01, FRAME_jump07, gnorta_frames_jump_down, gnorta_run };

//============
// gnorta_jump - this is only used for jumping onto or off of things. for dodge jumping,
//		use gnorta_dodge_jump
//============
void gnorta_jump(edict_t* self, blocked_jump_result_t result)
{
	if (!self->enemy)
		return;

	if (result == blocked_jump_result_t::JUMP_JUMP_UP)
		M_SetAnimation(self, &gnorta_move_jump_up);
	else
		M_SetAnimation(self, &gnorta_move_jump_down);
}

// ******************
// Blocked
// ******************
MONSTERINFO_BLOCKED(gnorta_blocked) (edict_t* self, float dist) -> bool
{
	if (!has_valid_enemy(self))
		return false;

	bool onCeiling = gnorta_ON_CEILING(self);

	if (!onCeiling)
	{
		if (auto result = blocked_checkjump(self, dist); result != blocked_jump_result_t::NO_JUMP)
		{
			if (result != blocked_jump_result_t::JUMP_TURN)
				gnorta_jump(self, result);
			return true;
		}

		if (blocked_checkplat(self, dist))
			return true;

		if (visible(self, self->enemy) && frandom() < 0.1f)
		{
			gnorta_do_pounce(self, self->enemy->s.origin);
			return true;
		}
	}
	else
	{
		if (gnorta_ok_to_transition(self))
		{
			self->gravityVector[2] = -1;
			self->s.angles[2] += 180.0f;
			if (self->s.angles[2] > 360.0f)
				self->s.angles[2] -= 360.0f;
			self->groundentity = nullptr;
			return true;
		}
	}

	return false;
}

// [Paril-KEX] quick patch-job to fix gnortas endlessly floating up into the sky
MONSTERINFO_PHYSCHANGED(gnorta_physics_change) (edict_t* self) -> void
{
	if (gnorta_ON_CEILING(self) && !self->groundentity)
	{
		self->gravityVector[2] = -1;
		self->s.angles[2] += 180.0f;
		if (self->s.angles[2] > 360.0f)
			self->s.angles[2] -= 360.0f;
	}
}

// ******************
// Death
// ******************

void gnorta_dead(edict_t* self)
{
	self->mins = { -28, -28, -18 };
	self->maxs = { 28, 28, -4 };
	monster_dead(self);
}

mframe_t gnorta_frames_death[] = {
	{ ai_move },
	{ ai_move, -5 },
	{ ai_move, -10 },
	{ ai_move, -20 },

	{ ai_move, -10 },
	{ ai_move, -10 },
	{ ai_move, -5 },
	{ ai_move, -5 },

	{ ai_move, 0, monster_footstep }
};
MMOVE_T(gnorta_move_death) = { FRAME_death01, FRAME_death09, gnorta_frames_death, gnorta_dead };

DIE(gnorta_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	// dude bit it, make him fall!
	self->movetype = MOVETYPE_TOSS;
	self->s.angles[2] = 0;
	self->gravityVector = { 0, 0, -1 };

	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		ThrowGibs(self, damage, {
			});
		self->deadflag = true;
		return;
	}

	if (self->deadflag)
		return;

	// regular death
	gi.sound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	self->deadflag = true;
	self->takedamage = true;
	M_SetAnimation(self, &gnorta_move_death);
}

// ******************
// SPAWN
// ******************

/*QUAKED monster_gnorta (1 .5 0) (-28 -28 -18) (28 28 18) Ambush Trigger_Spawn Sight OnRoof NoJumping
Spider Monster

  ONROOF - Monster starts sticking to the roof.
*/

constexpr spawnflags_t SPAWNFLAG_gnorta_ONROOF = 8_spawnflag;
constexpr spawnflags_t SPAWNFLAG_gnorta_NOJUMPING = 16_spawnflag;

void SP_monster_gnorta(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	sound_pain.assign("gnorta/pain.wav");
	sound_die.assign("gnorta/death.wav");
	sound_sight.assign("gnorta/sight.wav");
	sound_punch_hit1.assign("gnorta/melee1.wav");
	sound_punch_hit2.assign("gnorta/melee2.wav");
	sound_idle.assign("gnorta/idle.wav");

	// PMM - precache bolt2
	gi.modelindex("models/objects/laser/tris.md2");

	self->s.modelindex = gi.modelindex("models/vault/monsters/gnorta/tris.md2");

	self->mins = { -28, -28, -18 };
	self->maxs = { 28, 28, 18 };
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	self->health = 250 * st.health_multiplier;
	self->gib_health = -50;
	self->mass = 250;

	self->pain = gnorta_pain;
	self->die = gnorta_die;

	self->monsterinfo.stand = gnorta_stand;
	self->monsterinfo.walk = gnorta_walk;
	self->monsterinfo.run = gnorta_run;
	self->monsterinfo.sight = gnorta_sight;
	self->monsterinfo.idle = gnorta_idle;
	self->monsterinfo.dodge = gnorta_dodge;
	self->monsterinfo.blocked = gnorta_blocked;
	self->monsterinfo.melee = gnorta_attack_melee;
	self->monsterinfo.setskin = gnorta_setskin;
	self->monsterinfo.physics_change = gnorta_physics_change;

	gi.linkentity(self);

	M_SetAnimation(self, &gnorta_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	self->s.renderfx |= RF_CUSTOMSKIN;
	self->s.skinnum = gi.imageindex("models/vault/monsters/infantry/camo.pcx");

	if (self->spawnflags.has(SPAWNFLAG_gnorta_ONROOF))
	{
		self->s.angles[2] = 180;
		self->gravityVector[2] = 1;
	}

	self->monsterinfo.can_jump = !self->spawnflags.has(SPAWNFLAG_gnorta_NOJUMPING);
	self->monsterinfo.drop_height = 256;
	self->monsterinfo.jump_height = 68;

	walkmonster_start(self);
	ApplyMonsterBonusFlags(self);

	if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED) {
		self->health = 2500 * st.health_multiplier;
		self->gib_health = -999777;
		ApplyMonsterBonusFlags(self);
	}
	else
		self->health = 250 * st.health_multiplier;
	self->gib_health = -80;
}