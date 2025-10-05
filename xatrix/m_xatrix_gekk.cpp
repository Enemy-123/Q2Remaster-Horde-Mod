// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
	xatrix
	gekk.c
*/

#include "../g_local.h"
#include "m_xatrix_gekk.h"
#include "../shared.h"
#include "../horde/g_horde_scaling.h"
#include "g_weapon_constants.h"

constexpr spawnflags_t SPAWNFLAG_GEKK_CHANT = 8_spawnflag;
constexpr spawnflags_t SPAWNFLAG_GEKK_NOJUMPING = 16_spawnflag;
constexpr spawnflags_t SPAWNFLAG_GEKK_NOSWIM = 32_spawnflag;

static cached_soundindex sound_hordespawn;
static cached_soundindex sound_swing;
static cached_soundindex sound_hit;
static cached_soundindex sound_hit2;
static cached_soundindex sound_speet;
static cached_soundindex loogie_hit;
static cached_soundindex sound_death;
static cached_soundindex sound_pain1;
static cached_soundindex sound_sight;
static cached_soundindex sound_search;
static cached_soundindex sound_step1;
static cached_soundindex sound_step2;
static cached_soundindex sound_step3;
static cached_soundindex sound_thud;
static cached_soundindex sound_explod;
static cached_soundindex sound_chantlow;
static cached_soundindex sound_chantmid;
static cached_soundindex sound_chanthigh;

void gekk_swim(edict_t* self);
void gekk_save_enemy_pos(edict_t* self);
void gekk_jump_takeoff(edict_t* self);
void gekk_jump_takeoff2(edict_t* self);
void gekk_check_landing(edict_t* self);
void gekk_stop_skid(edict_t* self);

void water_to_land(edict_t* self);
void land_to_water(edict_t* self);

void gekk_check_underwater(edict_t* self);
void gekk_bite(edict_t* self);

void gekk_hit_left(edict_t* self);
void gekk_hit_right(edict_t* self);

extern const mmove_t gekk_move_attack1;
extern const mmove_t gekk_move_attack2;
extern const mmove_t gekk_move_chant;
extern const mmove_t gekk_move_swim_start;
extern const mmove_t gekk_move_swim_loop;
extern const mmove_t gekk_move_spit;
extern const mmove_t gekk_move_run_start;
extern const mmove_t gekk_move_run;

bool gekk_check_jump(edict_t* self);

//
// CHECKATTACK
//

bool gekk_check_melee(edict_t* self)
{
	if (!self->enemy || self->enemy->health <= 0 || self->monsterinfo.melee_debounce_time > level.time)
		return false;

	return range_to(self, self->enemy) <= RANGE_MELEE;
}

bool gekk_check_jump(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return false; // Can't jump at a non-existent or dead target.
	}

	vec3_t v{};
	float  distance;

	// don't jump if there's no way we can reach standing height
	if (self->absmin[2] + 125 < self->enemy->absmin[2])
		return false;

	v[0] = self->s.origin[0] - self->enemy->s.origin[0];
	v[1] = self->s.origin[1] - self->enemy->s.origin[1];
	v[2] = 0;
	distance = v.length();

	if (distance < 100)
	{
		return false;
	}
	if (distance > 100)
	{
		if (frandom() < (self->waterlevel >= WATER_WAIST ? 0.2f : 0.9f))
			return false;
	}

	return true;
}

bool gekk_check_jump_close(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return false; // Can't at a non-existent or dead target.
	}

	vec3_t v{};
	float  distance;

	v[0] = self->s.origin[0] - self->enemy->s.origin[0];
	v[1] = self->s.origin[1] - self->enemy->s.origin[1];
	v[2] = 0;

	distance = v.length();

	if (distance < 100)
	{
		// don't do this if our head is below their feet
		if (self->absmax[2] <= self->enemy->absmin[2])
			return false;
	}

	return true;
}

MONSTERINFO_CHECKATTACK(gekk_checkattack) (edict_t* self) -> bool
{
	if (!M_HasValidTarget(self))
		return false;

	if (gekk_check_melee(self))
	{
		self->monsterinfo.attack_state = AS_MELEE;
		return true;
	}

	if (self->monsterinfo.attack_state == AS_STRAIGHT && self->monsterinfo.attack_finished > level.time)
	{
		// keep running fool
		return false;
	}

	if (visible(self, self->enemy, false))
	{
		if (gekk_check_jump(self))
		{
			self->monsterinfo.attack_state = AS_MISSILE;
			return true;
		}

		if (gekk_check_jump_close(self) && !(self->flags & FL_SWIM))
		{
			self->monsterinfo.attack_state = AS_MISSILE;
			return true;
		}
	}

	return false;
}

//
// SOUNDS
//

void gekk_step(edict_t* self)
{
	int n = irandom(3);
	if (n == 0)
		gi.sound(self, CHAN_VOICE, sound_step1, 1, ATTN_NORM, 0);
	else if (n == 1)
		gi.sound(self, CHAN_VOICE, sound_step2, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_step3, 1, ATTN_NORM, 0);
}

MONSTERINFO_SIGHT(gekk_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_SEARCH(gekk_search) (edict_t* self) -> void
{
	float r;

	if (self->spawnflags.has(SPAWNFLAG_GEKK_CHANT))
	{
		r = frandom();
		if (r < 0.33f)
			gi.sound(self, CHAN_VOICE, sound_chantlow, 1, ATTN_NORM, 0);
		else if (r < 0.66f)
			gi.sound(self, CHAN_VOICE, sound_chantmid, 1, ATTN_NORM, 0);
		else
			gi.sound(self, CHAN_VOICE, sound_chanthigh, 1, ATTN_NORM, 0);
	}
	else
		gi.sound(self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);

	self->health += irandom(10, 20);
	if (self->health > self->max_health)
		self->health = self->max_health;

	self->monsterinfo.setskin(self);
}

MONSTERINFO_SETSKIN(gekk_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 4))
		self->s.skinnum = 2;
	else if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

void gekk_swing(edict_t* self)
{
	gi.sound(self, CHAN_VOICE, sound_swing, 1, ATTN_NORM, 0);
}

void gekk_face(edict_t* self)
{
	M_SetAnimation(self, &gekk_move_run);
}

//
// STAND
//

void ai_stand_gekk(edict_t* self, float dist)
{
	if (self->spawnflags.has(SPAWNFLAG_GEKK_CHANT))
	{
		ai_move(self, dist);
		if (!self->spawnflags.has(SPAWNFLAG_MONSTER_AMBUSH) && (self->monsterinfo.idle) && (level.time > self->monsterinfo.idle_time))
		{
			if (self->monsterinfo.idle_time)
			{
				self->monsterinfo.idle(self);
				self->monsterinfo.idle_time = level.time + random_time(15_sec, 30_sec);
			}
			else
			{
				self->monsterinfo.idle_time = level.time + random_time(15_sec);
			}
		}
	}
	else
		ai_stand(self, dist);
}

mframe_t gekk_frames_stand[] = {
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk }, // 10

	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk }, // 20

	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk }, // 30

	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },

	{ ai_stand_gekk, 0, gekk_check_underwater },
};
MMOVE_T(gekk_move_stand) = { FRAME_stand_01, FRAME_stand_39, gekk_frames_stand, nullptr };

mframe_t gekk_frames_standunderwater[] = {
	{ ai_stand_gekk, 14 },
	{ ai_stand_gekk, 14 },
	{ ai_stand_gekk, 14 },
	{ ai_stand_gekk, 14 },
	{ ai_stand_gekk, 16 },
	{ ai_stand_gekk, 16 },
	{ ai_stand_gekk, 16 },
	{ ai_stand_gekk, 18 },
	{ ai_stand_gekk, 18 },
	{ ai_stand_gekk, 18 },

	{ ai_stand_gekk, 20 },
	{ ai_stand_gekk, 20 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 24 },
	{ ai_stand_gekk, 24 },
	{ ai_stand_gekk, 26 },
	{ ai_stand_gekk, 26 },
	{ ai_stand_gekk, 24 },
	{ ai_stand_gekk, 24 },

	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 18 },
	{ ai_stand_gekk, 18 },

	{ ai_stand_gekk, 18 },
	{ ai_stand_gekk, 18 }
};

MMOVE_T(gekk_move_standunderwater) = { FRAME_swim_01, FRAME_swim_32, gekk_frames_standunderwater, nullptr };

void gekk_swim_loop(edict_t* self)
{
	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	self->flags |= FL_SWIM;
	M_SetAnimation(self, &gekk_move_swim_loop);
}

mframe_t gekk_frames_swim[] = {
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },

	{ ai_run, 20 },
	{ ai_run, 20 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 24 },
	{ ai_run, 24 },
	{ ai_run, 26 },
	{ ai_run, 26 },
	{ ai_run, 24 },
	{ ai_run, 24 },

	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 18 },
	{ ai_run, 18 },

	{ ai_run, 18 },
	{ ai_run, 18 }
};
MMOVE_T(gekk_move_swim_loop) = { FRAME_swim_01, FRAME_swim_32, gekk_frames_swim, gekk_swim_loop };

mframe_t gekk_frames_swim_start[] = {
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 18 },
	{ ai_run, 18, gekk_hit_left },
	{ ai_run, 18 },

	{ ai_run, 20 },
	{ ai_run, 20 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 24, gekk_hit_right },
	{ ai_run, 24 },
	{ ai_run, 26 },
	{ ai_run, 26 },
	{ ai_run, 24 },
	{ ai_run, 24 },

	{ ai_run, 22, gekk_bite },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 18 },
	{ ai_run, 18 },

	{ ai_run, 18 },
	{ ai_run, 18 }
};
MMOVE_T(gekk_move_swim_start) = { FRAME_swim_01, FRAME_swim_32, gekk_frames_swim_start, gekk_swim_loop };

void gekk_swim(edict_t* self)
{
	if (gekk_checkattack(self))
	{
		if (self->enemy->waterlevel < WATER_WAIST && frandom() > 0.7f)
			water_to_land(self);
		else
			M_SetAnimation(self, &gekk_move_swim_start);
	}
	else
		M_SetAnimation(self, &gekk_move_swim_start);
}

MONSTERINFO_STAND(gekk_stand) (edict_t* self) -> void
{
	if (self->waterlevel >= WATER_WAIST)
	{
		self->flags |= FL_SWIM;
		self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
		M_SetAnimation(self, &gekk_move_standunderwater);
	}
	else
		// Don't break out of the chant loop, which is initiated in the spawn function
		if (self->monsterinfo.active_move != &gekk_move_chant)
			M_SetAnimation(self, &gekk_move_stand);
}

void gekk_chant(edict_t* self)
{
	M_SetAnimation(self, &gekk_move_chant);
}

//
// IDLE
//

void gekk_idle_loop(edict_t* self)
{
	if (frandom() > 0.75f && self->health < self->max_health)
		self->monsterinfo.nextframe = FRAME_idle_01;
}

mframe_t gekk_frames_idle[] = {
	{ ai_stand_gekk, 0, gekk_search },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },

	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },

	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },

	{ ai_stand_gekk },
	{ ai_stand_gekk, 0, gekk_idle_loop }
};
MMOVE_T(gekk_move_idle) = { FRAME_idle_01, FRAME_idle_32, gekk_frames_idle, gekk_stand };
MMOVE_T(gekk_move_idle2) = { FRAME_idle_01, FRAME_idle_32, gekk_frames_idle, gekk_face };

mframe_t gekk_frames_idle2[] = {
	{ ai_move, 0, gekk_search },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move },
	{ ai_move, 0, gekk_idle_loop }
};
MMOVE_T(gekk_move_chant) = { FRAME_idle_01, FRAME_idle_32, gekk_frames_idle2, gekk_chant };

MONSTERINFO_IDLE(gekk_idle) (edict_t* self) -> void
{
	if (self->spawnflags.has(SPAWNFLAG_GEKK_NOSWIM) || self->waterlevel < WATER_WAIST)
		M_SetAnimation(self, &gekk_move_idle);
	else
		M_SetAnimation(self, &gekk_move_swim_start);
	// gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

//
// WALK
//

mframe_t gekk_frames_walk[] = {
	{ ai_walk, 3.849f, gekk_check_underwater }, // frame 0
	{ ai_walk, 19.606f },						// frame 1
	{ ai_walk, 25.583f },						// frame 2
	{ ai_walk, 34.625f, gekk_step },			// frame 3
	{ ai_walk, 27.365f },						// frame 4
	{ ai_walk, 28.480f },						// frame 5
};

MMOVE_T(gekk_move_walk) = { FRAME_run_01, FRAME_run_06, gekk_frames_walk, nullptr };

MONSTERINFO_WALK(gekk_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &gekk_move_walk);
}

//
// RUN
//

MONSTERINFO_RUN(gekk_run_start) (edict_t* self) -> void
{
	if (!self->spawnflags.has(SPAWNFLAG_GEKK_NOSWIM) && self->waterlevel >= WATER_WAIST)
	{
		M_SetAnimation(self, &gekk_move_swim_start);
	}
	else
	{
		M_SetAnimation(self, &gekk_move_run_start);
	}
}

void gekk_run(edict_t* self)
{

	if (!self->spawnflags.has(SPAWNFLAG_GEKK_NOSWIM) && self->waterlevel >= WATER_WAIST)
	{
		M_SetAnimation(self, &gekk_move_swim_start);
		return;
	}
	else
	{
		if (self->monsterinfo.aiflags & AI_STAND_GROUND)
			M_SetAnimation(self, &gekk_move_stand);
		else
			M_SetAnimation(self, &gekk_move_run);
	}
}

mframe_t gekk_frames_run[] = {
	{ ai_run, 3.849f, gekk_check_underwater }, // frame 0
	{ ai_run, 19.606f },					   // frame 1
	{ ai_run, 25.583f },					   // frame 2
	{ ai_run, 34.625f, gekk_step },			   // frame 3
	{ ai_run, 27.365f },					   // frame 4
	{ ai_run, 28.480f },					   // frame 5
};
MMOVE_T(gekk_move_run) = { FRAME_run_01, FRAME_run_06, gekk_frames_run, nullptr };

mframe_t gekk_frames_run_st[] = {
	{ ai_run, 0.212f },	 // frame 0
	{ ai_run, 19.753f }, // frame 1
};
MMOVE_T(gekk_move_run_start) = { FRAME_stand_01, FRAME_stand_02, gekk_frames_run_st, gekk_run };

//
// MELEE
//

void gekk_hit_left(edict_t* self)
{
	// ROBUST FIX: Check if the enemy pointer is valid AND the entity is in use and alive.
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	int damage = M_GET_DMG_OR(self, MELEE, 17);

	vec3_t aim = { MELEE_DISTANCE, self->mins[0], 8 };
	if (fire_hit(self, aim, damage, 100))
		gi.sound(self, CHAN_WEAPON, sound_hit, 1, ATTN_NORM, 0);
	else
	{
		gi.sound(self, CHAN_WEAPON, sound_swing, 1, ATTN_NORM, 0);
		self->monsterinfo.melee_debounce_time = level.time + 1.5_sec;
	}
}


void gekk_hit_right(edict_t* self)
{
	// ROBUST FIX: Apply the same check here for consistency and safety.
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	int damage = M_GET_DMG_OR(self, MELEE, 17);

	vec3_t aim = { MELEE_DISTANCE, self->maxs[0], 8 };
	if (fire_hit(self, aim, damage, 100))
		gi.sound(self, CHAN_WEAPON, sound_hit2, 1, ATTN_NORM, 0);
	else
	{
		gi.sound(self, CHAN_WEAPON, sound_swing, 1, ATTN_NORM, 0);
		self->monsterinfo.melee_debounce_time = level.time + 1.5_sec;
	}
}

void gekk_check_refire(edict_t* self)
{
	// ROBUST FIX: Apply the same check here for consistency and safety.
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	if (range_to(self, self->enemy) <= RANGE_MELEE &&
		self->monsterinfo.melee_debounce_time <= level.time)
	{
		if (self->s.frame == FRAME_clawatk3_09)
			M_SetAnimation(self, &gekk_move_attack2);
		else if (self->s.frame == FRAME_clawatk5_09)
			M_SetAnimation(self, &gekk_move_attack1);
	}
}

TOUCH(loogie_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
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
		T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal, self->dmg, 1, DAMAGE_ENERGY, MOD_GEKK); // MOD_UNKNOWN);//

	gi.sound(self, CHAN_AUTO, loogie_hit, 1.0f, ATTN_NORM, 0);

	G_FreeEdict(self);
}

void fire_loogie(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed)
{
	edict_t* loogie;
	trace_t	 tr;

	loogie = G_Spawn();
	loogie->s.origin = start;
	loogie->s.old_origin = start;
	loogie->s.angles = vectoangles(dir);
	loogie->velocity = dir * speed;
	loogie->movetype = MOVETYPE_FLYMISSILE;
	loogie->clipmask = MASK_PROJECTILE;
	loogie->solid = SOLID_BBOX;
	// Paril: this was originally the wrong effect,
	// but it makes it look more acid-y.
	loogie->s.effects |= EF_BLASTER;
	loogie->s.renderfx |= RF_FULLBRIGHT;
	loogie->s.modelindex = gi.modelindex("models/objects/loogy/tris.md2");
	loogie->owner = self;

	// Store attacker info in case owner dies before projectile hits
	if (self) {
		if (self->client) {
			loogie->projectile_was_player_attacker = true;
			loogie->projectile_attacker_type_id = 0;
		} else if (self->svflags & SVF_MONSTER) {
			loogie->projectile_was_player_attacker = false;
			loogie->projectile_attacker_type_id = self->monsterinfo.monster_type_id;
		}
	}

	loogie->touch = loogie_touch;
	loogie->nextthink = level.time + 2_sec;
	loogie->think = G_FreeEdict;
	loogie->dmg = damage = static_cast<int>(round(damage * M_DamageModifier(self)));
	loogie->svflags |= SVF_PROJECTILE;
	gi.linkentity(loogie);

	tr = gi.traceline(self->s.origin, loogie->s.origin, loogie, MASK_PROJECTILE);
	if (tr.fraction < 1.0f)
	{
		loogie->s.origin = tr.endpos + (tr.plane.normal * 1.f);
		loogie->touch(loogie, tr.ent, tr, false);
	}
}

void loogie(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t start;
	vec3_t forward, right, up;
	vec3_t dir;
	vec3_t gekkoffset = { -18, -0.8f, 24 };

	if (!self->enemy || self->enemy->health <= 0)
		return;

	AngleVectors(self->s.angles, forward, right, up);
	start = M_ProjectFlashSource(self, gekkoffset, forward, right);

	start += (up * 2);

	// The target position 'end' is now self->pos1, which was set by gekk_save_enemy_pos
	vec3_t end = self->pos1;
	dir = end - start;
	dir.normalize();

	int damage = M_GET_DMG_OR(self, PLASMA, 7);
	int speed = M_PLASMA_SPEED(self);
	fire_loogie(self, start, dir, damage, speed > 0 ? speed : 850);

	gi.sound(self, CHAN_BODY, sound_speet, 1.0f, ATTN_NORM, 0);
}

void reloogie(edict_t* self);

//readded spitharder, frandom chance to spit again in a burst
mframe_t gekk_frames_spitharder[] = {
	{ ai_charge, 0, gekk_save_enemy_pos },
	{ ai_charge, 0, loogie },
	{ ai_charge },
	{ ai_charge, 0, gekk_save_enemy_pos },
	{ ai_charge, 0, loogie },
	{ ai_charge, 0, gekk_save_enemy_pos },
	{ ai_charge, 0, reloogie }
};;
MMOVE_T(gekk_move_spitharder) = { FRAME_spit_01, FRAME_spit_07, gekk_frames_spitharder, gekk_run_start };

void reloogie(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return;
	}

	float idle_chance = IsBonusMonster(self) ? 0.95f : 0.8f;
	
	if (frandom() > idle_chance && self->health < self->max_health)
	{
		M_SetAnimation(self, &gekk_move_idle2);
		return;
	}

	if (self->enemy->health > 0)
		if ((range_to(self, self->enemy) <= RANGE_NEAR))
		{
			gekk_save_enemy_pos(self);
			// Bonus monsters use spitharder more often in reloogie chains
			if (IsBonusMonster(self) && frandom() > 0.4f) {
				M_SetAnimation(self, &gekk_move_spitharder);
			} else {
				M_SetAnimation(self, &gekk_move_spit);
			}
		}
}


// This new function checks if the Gekk should spit again, looping the animation.
// It replaces the old `reloogie` and the need for a separate `spitharder` move.
void gekk_continue_spit(edict_t* self)
{
	// Stop if the enemy is dead, gone, or not visible
	if (!self->enemy || self->enemy->health <= 0 || !visible(self, self->enemy, false))
	{
		self->monsterinfo.attack_finished = level.time + 1.0_sec;
		return;
	}

	// In later waves, the Gekk has a higher chance to spit multiple times
	float chance_to_refire = IsFirstThreeWaves(current_wave_level) ? 0.2f : 0.45f;

	if (frandom() < chance_to_refire)
	{
		// Loop back to an earlier frame in the animation to spit again.
		// This creates a variable-length burst of spit.
		self->monsterinfo.nextframe = FRAME_spit_04;
		return;
	}

	// If we don't refire, the animation continues to its endfunc (gekk_run_start)
	self->monsterinfo.attack_finished = level.time + 1.0_sec;
}

void gekk_save_enemy_pos(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Can't at a non-existent or dead target.
	}

	// Save the enemy's current position for the loogie to aim at.
	self->pos1 = self->enemy->s.origin;
	self->pos1[2] += self->enemy->viewheight;
}

void gekk_aim_and_spit(edict_t* self)
{
	if (!self->enemy)
		return;

	// Force the monster to face the enemy's current location.
	// This ensures each spit in a volley is aimed correctly.
	//ai_face(self, 0);

	// Now fire the projectile.
	loogie(self);
}

// A single, dynamic spit animation.
mframe_t gekk_frames_spit[] = {
	{ ai_charge },						// FRAME_spit_01
	{ ai_charge },						// FRAME_spit_02
	{ ai_charge },						// FRAME_spit_03
	{ ai_charge },						// FRAME_spit_04 (Loop point)
	{ ai_charge, 0, gekk_save_enemy_pos },		// FRAME_spit_05 (Aim)
	{ ai_charge, 0, loogie },			// FRAME_spit_06 (Fire!)
	{ ai_charge, 0, gekk_continue_spit }	// FRAME_spit_07 (Check for refire)
};
MMOVE_T(gekk_move_spit) = { FRAME_spit_01, FRAME_spit_07, gekk_frames_spit, gekk_run_start };

mframe_t gekk_frames_attack1[] = {
	{ ai_charge },
	{ ai_charge, 0, gekk_hit_left },
	{ ai_charge },

	{ ai_charge, 0, gekk_hit_left },
	{ ai_charge },
	{ ai_charge, 0, gekk_hit_left },

	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekk_check_refire }
};
MMOVE_T(gekk_move_attack1) = { FRAME_clawatk3_01, FRAME_clawatk3_09, gekk_frames_attack1, gekk_run_start };

mframe_t gekk_frames_attack2[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekk_hit_left },

	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekk_hit_right },

	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekk_check_refire }
};
MMOVE_T(gekk_move_attack2) = { FRAME_clawatk5_01, FRAME_clawatk5_09, gekk_frames_attack2, gekk_run_start };

void gekk_check_underwater(edict_t* self)
{
	if (!self->spawnflags.has(SPAWNFLAG_GEKK_NOSWIM) && self->waterlevel >= WATER_WAIST)
		land_to_water(self);
}

mframe_t gekk_frames_leapatk[] = {
	{ ai_charge },								// frame 0
	{ ai_charge, -0.387f },						// frame 1
	{ ai_charge, -1.113f },						// frame 2
	{ ai_charge, -0.237f },						// frame 3
	{ ai_charge, 6.720f, gekk_jump_takeoff },	// frame 4  last frame on ground
	{ ai_charge, 6.414f },						// frame 5  leaves ground
	{ ai_charge, 0.163f },						// frame 6
	{ ai_charge, 28.316f },						// frame 7
	{ ai_charge, 24.198f },						// frame 8
	{ ai_charge, 31.742f },						// frame 9
	{ ai_charge, 35.977f, gekk_check_landing }, // frame 10  last frame in air
	{ ai_charge, 12.303f, gekk_stop_skid },		// frame 11  feet back on ground
	{ ai_charge, 20.122f, gekk_stop_skid },		// frame 12
	{ ai_charge, -1.042f, gekk_stop_skid },		// frame 13
	{ ai_charge, 2.556f, gekk_stop_skid },		// frame 14
	{ ai_charge, 0.544f, gekk_stop_skid },		// frame 15
	{ ai_charge, 1.862f, gekk_stop_skid },		// frame 16
	{ ai_charge, 1.224f, gekk_stop_skid },		// frame 17

	{ ai_charge, -0.457f, gekk_check_underwater }, // frame 18
};
MMOVE_T(gekk_move_leapatk) = { FRAME_leapatk_01, FRAME_leapatk_19, gekk_frames_leapatk, gekk_run_start };

mframe_t gekk_frames_leapatk2[] = {
	{ ai_charge },								// frame 0
	{ ai_charge, -0.387f },						// frame 1
	{ ai_charge, -1.113f },						// frame 2
	{ ai_charge, -0.237f },						// frame 3
	{ ai_charge, 6.720f, gekk_jump_takeoff2 },	// frame 4  last frame on ground
	{ ai_charge, 6.414f },						// frame 5  leaves ground
	{ ai_charge, 0.163f },						// frame 6
	{ ai_charge, 28.316f },						// frame 7
	{ ai_charge, 24.198f },						// frame 8
	{ ai_charge, 31.742f },						// frame 9
	{ ai_charge, 35.977f, gekk_check_landing }, // frame 10  last frame in air
	{ ai_charge, 12.303f, gekk_stop_skid },		// frame 11  feet back on ground
	{ ai_charge, 20.122f, gekk_stop_skid },		// frame 12
	{ ai_charge, -1.042f, gekk_stop_skid },		// frame 13
	{ ai_charge, 2.556f, gekk_stop_skid },		// frame 14
	{ ai_charge, 0.544f, gekk_stop_skid },		// frame 15
	{ ai_charge, 1.862f, gekk_stop_skid },		// frame 16
	{ ai_charge, 1.224f, gekk_stop_skid },		// frame 17

	{ ai_charge, -0.457f, gekk_check_underwater }, // frame 18
};
MMOVE_T(gekk_move_leapatk2) = { FRAME_leapatk_01, FRAME_leapatk_19, gekk_frames_leapatk2, gekk_run_start };

void gekk_bite(edict_t* self)
{
	// ROBUST FIX: Apply the same check here as well.
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t aim = { MELEE_DISTANCE, 0, 0 };
	fire_hit(self, aim, 5, 0);
}

void gekk_preattack(edict_t* self)
{
	// underwater attack sound
	// gi.sound (self, CHAN_WEAPON, something something underwater sound, 1, ATTN_NORM, 0);
	return;
}

mframe_t gekk_frames_attack[] = {
	{ ai_charge, 16, gekk_preattack },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16, gekk_bite },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16, gekk_bite },

	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16, gekk_hit_left },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16, gekk_hit_right },
	{ ai_charge, 16 },

	{ ai_charge, 16 }
};
MMOVE_T(gekk_move_attack) = { FRAME_attack_01, FRAME_attack_21, gekk_frames_attack, gekk_run_start };

MONSTERINFO_MELEE(gekk_melee) (edict_t* self) -> void
{
	if (self->waterlevel >= WATER_WAIST)
	{
		M_SetAnimation(self, &gekk_move_attack);
	}
	else
	{
		float r = frandom();

		if (r > 0.66f)
			M_SetAnimation(self, &gekk_move_attack1);
		else
			M_SetAnimation(self, &gekk_move_attack2);
	}
}

//
// ATTACK
//

TOUCH(gekk_jump_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (self->health <= 0)
	{
		self->touch = nullptr;
		return;
	}

	// WALL BOUNCE: If we hit a wall/obstacle (not an enemy), re-jump toward enemy
	if (self->style == 1 && other->solid == SOLID_BSP && M_HasValidTarget(self))
	{
		// Calculate enemy height including viewheight
		float const enemy_height = self->enemy->s.origin[2] + self->enemy->viewheight;
		float const self_height = self->s.origin[2];
		float const height_diff = enemy_height - self_height;

		// BOUNCE RESTRICTIONS: Don't bounce if:
		// 1. Bounce window expired (more than 2 seconds since jump started)
		// 2. Gekk is already ABOVE enemy (would get stuck in wall)
		if (level.time > self->teleport_time)//|| self_height > enemy_height)
		{
			return; // Don't bounce - let physics handle it naturally
		}

		// Aim directly at enemy
		vec3_t const dir_to_enemy = (self->enemy->s.origin - self->s.origin).normalized();
		self->s.angles[YAW] = vectoyaw(dir_to_enemy);
		auto const vectors = AngleVectors(self->s.angles);

		// HEIGHT-AWARE UPWARD VELOCITY: Don't jump up if enemy is below!
		float up_velocity = 250.0f; // Base
		float forward_velocity = 400.0f; // Base

		if (height_diff > 64.0f) {
			// Enemy is significantly higher - jump up more
			up_velocity = 400.0f + (height_diff * 0.4f);
			forward_velocity = 400.0f;
		}
		else if (height_diff < -64.0f) {
			// Enemy is significantly lower - minimal upward jump (just arc over)
			up_velocity = 100.0f;
			forward_velocity = 400.0f;
		}
		else {
			// Enemy is roughly at SAME LEVEL - 50% chance for LOW HORIZONTAL POUNCE
			if (frandom() < 0.5f) {
				// LOW HORIZONTAL POUNCE - fast ground approach
				up_velocity = 120.0f; // Minimal height
				forward_velocity = 600.0f; // FAST forward pounce
			}
			else {
				// Normal moderate jump
				up_velocity = 250.0f;
				forward_velocity = 400.0f;
			}
		}

		// Clamp to reasonable ranges
		up_velocity = clamp(up_velocity, 50.0f, 500.0f);
		forward_velocity = clamp(forward_velocity, 300.0f, 700.0f);

		// Re-launch toward enemy with smart height and speed
		self->velocity = vectors.forward * forward_velocity + vectors.up * up_velocity;
		self->groundentity = nullptr;
		self->gravity = 1.0f;

		gi.sound(self, CHAN_VOICE, sound_sight, 0.3f, ATTN_NORM, 0);
		return;
	}

	// ENEMY HIT: Deal damage on impact
	if (self->style == 1 && other->takedamage)
	{
		if (self->velocity.length() > 200)
		{
			vec3_t point;
			vec3_t normal;
			int	   damage;

			normal = self->velocity;
			normal.normalize();
			point = self->s.origin + (normal * self->maxs[0]);
			damage = irandom(10, 20);
			T_Damage(other, self, self, self->velocity, point, normal, damage, damage, DAMAGE_NONE, MOD_GEKK);
			self->style = 0;
		}
	}

	if (!M_CheckBottom(self))
	{
		if (self->groundentity)
		{
			self->monsterinfo.nextframe = FRAME_leapatk_11;
			self->touch = nullptr;
		}
		return;
	}

	self->touch = nullptr;
}

void gekk_jump_takeoff(edict_t* self)
{
	vec3_t forward;

	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	self->s.origin[2] += 1;

	// high jump
	if (gekk_check_jump(self))
	{
		self->velocity = forward * 950;
		self->velocity[2] = 250;
	}
	else
	{
		self->velocity = forward * 450;
		self->velocity[2] = 400;
	}

	self->groundentity = nullptr;
	self->monsterinfo.aiflags |= AI_DUCKED;
	self->monsterinfo.attack_finished = level.time + 3_sec;
	self->touch = gekk_jump_touch;
	self->style = 1;
}

void gekk_jump_takeoff2(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Can't at a non-existent or dead target.
	}

	vec3_t forward;

	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	self->s.origin[2] = self->enemy->s.origin[2];

	if (gekk_check_jump(self))
	{
		self->velocity = forward * 300;
		self->velocity[2] = 250;
	}
	else
	{
		self->velocity = forward * 150;
		self->velocity[2] = 300;
	}

	self->groundentity = nullptr;
	self->monsterinfo.aiflags |= AI_DUCKED;
	self->monsterinfo.attack_finished = level.time + 3_sec;
	self->touch = gekk_jump_touch;
	self->style = 1;
}

void gekk_stop_skid(edict_t* self)
{
	if (self->groundentity)
		self->velocity = {};
}

void gekk_check_landing(edict_t* self)
{
	if (self->groundentity)
	{
		gi.sound(self, CHAN_WEAPON, sound_thud, 1, ATTN_NORM, 0);
		self->monsterinfo.attack_finished = 0_ms;

		if (self->monsterinfo.unduck)
			self->monsterinfo.unduck(self);

		self->velocity = {};
		return;
	}

	// Paril: allow them to "pull" up ledges
	vec3_t fwd;
	AngleVectors(self->s.angles, fwd, nullptr, nullptr);

	if (fwd.dot(self->velocity) < 200)
		self->velocity += (fwd * 200.f);

	// note to self
	// causing skid
	if (level.time > self->monsterinfo.attack_finished)
		self->monsterinfo.nextframe = FRAME_leapatk_11;
	else
	{
		self->monsterinfo.nextframe = FRAME_leapatk_12;
	}
}

// The attack logic is now simpler, as it only needs to call one dynamic spit attack.
MONSTERINFO_ATTACK(gekk_attack) (edict_t* self) -> void
{
	const float r = range_to(self, self->enemy);

	if (self->flags & FL_SWIM)
	{
		if (self->enemy && self->enemy->waterlevel >= WATER_WAIST && r <= RANGE_NEAR)
			return;

		self->flags &= ~FL_SWIM;
		self->monsterinfo.aiflags &= ~AI_ALTERNATE_FLY;
		M_SetAnimation(self, &gekk_move_leapatk);
		self->monsterinfo.nextframe = FRAME_leapatk_05;
	}
	else
	{
		if (r >= RANGE_MID) {
			if (frandom() > 0.4f) {
				if (IsBonusMonster(self)) {
					// Bonus monsters prefer spitharder more often
					self->count++;
					if (self->count % 2 == 0) {
						M_SetAnimation(self, &gekk_move_spitharder);
					} else {
						M_SetAnimation(self, &gekk_move_spit);
					}
				} else {
					// Use counter-based alternation for more predictable pattern
					self->count++;
					if (self->count % 3 == 0) {
						M_SetAnimation(self, &gekk_move_spitharder);
					} else {
						M_SetAnimation(self, &gekk_move_spit);
					}
				}
			}
			else {
				M_SetAnimation(self, &gekk_move_run_start);
				self->monsterinfo.attack_finished = level.time + 2_sec;
			}
		}
		else if (frandom() > 0.7f) {
				if (IsBonusMonster(self)) {
					// Bonus monsters prefer spitharder more often
					self->count++;
					if (self->count % 2 == 0) {
						M_SetAnimation(self, &gekk_move_spitharder);
					} else {
						M_SetAnimation(self, &gekk_move_spit);
					}
				} else {
					// Use counter-based alternation for more predictable pattern
					self->count++;
					if (self->count % 3 == 0) {
						M_SetAnimation(self, &gekk_move_spitharder);
					} else {
						M_SetAnimation(self, &gekk_move_spit);
					}
				}
		}
		else {
			if (self->spawnflags.has(SPAWNFLAG_GEKK_NOJUMPING) || frandom() > 0.7f) {
				M_SetAnimation(self, &gekk_move_run_start);
				self->monsterinfo.attack_finished = level.time + 1.4_sec;
			}
			else {
				M_SetAnimation(self, &gekk_move_leapatk);
			}
		}
	}
}

//
// PAIN
//

mframe_t gekk_frames_pain[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
};
MMOVE_T(gekk_move_pain) = { FRAME_pain_01, FRAME_pain_06, gekk_frames_pain, gekk_run_start };

mframe_t gekk_frames_pain1[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
	{ ai_move }, // frame 6
	{ ai_move }, // frame 7
	{ ai_move }, // frame 8
	{ ai_move }, // frame 9

	{ ai_move, 0, gekk_check_underwater }
};
MMOVE_T(gekk_move_pain1) = { FRAME_pain3_01, FRAME_pain3_11, gekk_frames_pain1, gekk_run_start };

mframe_t gekk_frames_pain2[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
	{ ai_move }, // frame 6
	{ ai_move }, // frame 7
	{ ai_move }, // frame 8
	{ ai_move }, // frame 9

	{ ai_move }, // frame 10
	{ ai_move }, // frame 11
	{ ai_move, 0, gekk_check_underwater },
};
MMOVE_T(gekk_move_pain2) = { FRAME_pain4_01, FRAME_pain4_13, gekk_frames_pain2, gekk_run_start };

PAIN(gekk_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	float r;

	if (self->spawnflags.has(SPAWNFLAG_GEKK_CHANT))
	{
		self->spawnflags &= ~SPAWNFLAG_GEKK_CHANT;
		return;
	}

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;

	gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);

	if (self->waterlevel >= WATER_WAIST)
	{
		if (!(self->flags & FL_SWIM))
		{
			self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
			self->flags |= FL_SWIM;
		}

		if (M_ShouldReactToPain(self, mod)) // no pain anims in nightmare
			M_SetAnimation(self, &gekk_move_pain);
	}
	else if (M_ShouldReactToPain(self, mod)) // no pain anims in nightmare
	{
		r = frandom();

		if (r > 0.5f)
			M_SetAnimation(self, &gekk_move_pain1);
		else
			M_SetAnimation(self, &gekk_move_pain2);
	}
}

//
// DEATH
//

void gekk_dead(edict_t* self)
{
	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, -8 };
	monster_dead(self);
}

void gekk_gib(edict_t* self, int damage)
{
	gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

	ThrowGibs(self, damage, {
		{ "models/objects/gekkgib/pelvis/tris.md2", GIB_ACID },
		{ 2, "models/objects/gekkgib/arm/tris.md2", GIB_ACID },
		{ "models/objects/gekkgib/torso/tris.md2", GIB_ACID },
		{ "models/objects/gekkgib/claw/tris.md2", GIB_ACID },
		{ 2, "models/objects/gekkgib/leg/tris.md2", GIB_ACID },
		{ "models/objects/gekkgib/head/tris.md2", GIB_ACID | GIB_HEAD }
		});
}

void gekk_gibfest(edict_t* self)
{
	gekk_gib(self, 20);
	self->deadflag = true;
}

void isgibfest(edict_t* self)
{
	if (frandom() > 0.9f)
		gekk_gibfest(self);
}

static void gekk_shrink(edict_t* self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t gekk_frames_death1[] = {
	{ ai_move, -5.151f },			   // frame 0
	{ ai_move, -12.223f },			   // frame 1
	{ ai_move, -11.484f },			   // frame 2
	{ ai_move, -17.952f },			   // frame 3
	{ ai_move, -6.953f },			   // frame 4
	{ ai_move, -7.393f, gekk_shrink }, // frame 5
	{ ai_move, -10.713f },			   // frame 6
	{ ai_move, -17.464f },			   // frame 7
	{ ai_move, -11.678f },			   // frame 8
	{ ai_move, -11.678f }			   // frame 9
};
MMOVE_T(gekk_move_death1) = { FRAME_death1_01, FRAME_death1_10, gekk_frames_death1, gekk_dead };

mframe_t gekk_frames_death3[] = {
	{ ai_move },					 // frame 0
	{ ai_move, 0.022f },			 // frame 1
	{ ai_move, 0.169f },			 // frame 2
	{ ai_move, -0.710f },			 // frame 3
	{ ai_move, -13.446f },			 // frame 4
	{ ai_move, -7.654f, isgibfest }, // frame 5
	{ ai_move, -31.951f },			 // frame 6
};
MMOVE_T(gekk_move_death3) = { FRAME_death3_01, FRAME_death3_07, gekk_frames_death3, gekk_dead };

mframe_t gekk_frames_death4[] = {
	{ ai_move, 5.103f },			   // frame 0
	{ ai_move, -4.808f },			   // frame 1
	{ ai_move, -10.509f },			   // frame 2
	{ ai_move, -9.899f },			   // frame 3
	{ ai_move, 4.033f, isgibfest },	   // frame 4
	{ ai_move, -5.197f },			   // frame 5
	{ ai_move, -0.919f },			   // frame 6
	{ ai_move, -8.821f },			   // frame 7
	{ ai_move, -5.626f },			   // frame 8
	{ ai_move, -8.865f, isgibfest },   // frame 9
	{ ai_move, -0.845f },			   // frame 10
	{ ai_move, 1.986f },			   // frame 11
	{ ai_move, 0.170f },			   // frame 12
	{ ai_move, 1.339f, isgibfest },	   // frame 13
	{ ai_move, -0.922f },			   // frame 14
	{ ai_move, 0.818f },			   // frame 15
	{ ai_move, -1.288f },			   // frame 16
	{ ai_move, -1.408f, isgibfest },   // frame 17
	{ ai_move, -7.787f },			   // frame 18
	{ ai_move, -3.995f },			   // frame 19
	{ ai_move, -4.604f },			   // frame 20
	{ ai_move, -1.715f, isgibfest },   // frame 21
	{ ai_move, -0.564f },			   // frame 22
	{ ai_move, -0.597f },			   // frame 23
	{ ai_move, 0.074f },			   // frame 24
	{ ai_move, -0.309f, isgibfest },   // frame 25
	{ ai_move, -0.395f },			   // frame 26
	{ ai_move, -0.501f },			   // frame 27
	{ ai_move, -0.325f },			   // frame 28
	{ ai_move, -0.931f, isgibfest },   // frame 29
	{ ai_move, -1.433f },			   // frame 30
	{ ai_move, -1.626f },			   // frame 31
	{ ai_move, 4.680f },			   // frame 32
	{ ai_move, 0.560f },			   // frame 33
	{ ai_move, -0.549f, gekk_gibfest } // frame 34
};
MMOVE_T(gekk_move_death4) = { FRAME_death4_01, FRAME_death4_35, gekk_frames_death4, gekk_dead };

mframe_t gekk_frames_wdeath[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
	{ ai_move }, // frame 6
	{ ai_move }, // frame 7
	{ ai_move }, // frame 8
	{ ai_move }, // frame 9
	{ ai_move }, // frame 10
	{ ai_move }, // frame 11
	{ ai_move }, // frame 12
	{ ai_move }, // frame 13
	{ ai_move }, // frame 14
	{ ai_move }, // frame 15
	{ ai_move }, // frame 16
	{ ai_move }, // frame 17
	{ ai_move }, // frame 18
	{ ai_move }, // frame 19
	{ ai_move }, // frame 20
	{ ai_move }, // frame 21
	{ ai_move }, // frame 22
	{ ai_move }, // frame 23
	{ ai_move }, // frame 24
	{ ai_move }, // frame 25
	{ ai_move }, // frame 26
	{ ai_move }, // frame 27
	{ ai_move }, // frame 28
	{ ai_move }, // frame 29
	{ ai_move }, // frame 30
	{ ai_move }, // frame 31
	{ ai_move }, // frame 32
	{ ai_move }, // frame 33
	{ ai_move }, // frame 34
	{ ai_move }, // frame 35
	{ ai_move }, // frame 36
	{ ai_move }, // frame 37
	{ ai_move }, // frame 38
	{ ai_move }, // frame 39
	{ ai_move }, // frame 40
	{ ai_move }, // frame 41
	{ ai_move }, // frame 42
	{ ai_move }, // frame 43
	{ ai_move }	 // frame 44
};
MMOVE_T(gekk_move_wdeath) = { FRAME_wdeath_01, FRAME_wdeath_45, gekk_frames_wdeath, gekk_dead };

DIE(gekk_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	float r;

	if (M_CheckGib(self, mod))
	{
		gekk_gib(self, damage);
		self->deadflag = true;
		return;
	}

	if (self->deadflag)
		return;

	gi.sound(self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
	self->deadflag = true;
	self->takedamage = true;

	if (self->waterlevel >= WATER_WAIST)
	{
		gekk_shrink(self);
		M_SetAnimation(self, &gekk_move_wdeath);
	}
	else
	{
		r = frandom();
		if (r > 0.66f)
			M_SetAnimation(self, &gekk_move_death1);
		else if (r > 0.33f)
			M_SetAnimation(self, &gekk_move_death3);
		else
			M_SetAnimation(self, &gekk_move_death4);
	}
}

/*
	duck
*/
mframe_t gekk_frames_lduck[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
	{ ai_move }, // frame 6
	{ ai_move }, // frame 7
	{ ai_move }, // frame 8
	{ ai_move }, // frame 9

	{ ai_move }, // frame 10
	{ ai_move }, // frame 11
	{ ai_move }	 // frame 12
};
MMOVE_T(gekk_move_lduck) = { FRAME_lduck_01, FRAME_lduck_13, gekk_frames_lduck, gekk_run_start };

mframe_t gekk_frames_rduck[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
	{ ai_move }, // frame 6
	{ ai_move }, // frame 7
	{ ai_move }, // frame 8
	{ ai_move }, // frame 9
	{ ai_move }, // frame 10
	{ ai_move }, // frame 11
	{ ai_move }	 // frame 12
};
MMOVE_T(gekk_move_rduck) = { FRAME_rduck_01, FRAME_rduck_13, gekk_frames_rduck, gekk_run_start };

// MONSTERINFO_DODGE(gekk_dodge) (edict_t* self, edict_t* attacker, gtime_t eta, trace_t* tr, bool gravity) -> void
// {
// 	// [Paril-KEX] this dodge is bad
// #if 0
// 	float r;

// 	r = frandom();
// 	if (r > 0.25f)
// 		return;

// 	if (!self->enemy)
// 		self->enemy = attacker;

// 	if (self->waterlevel)
// 	{
// 		M_SetAnimation(self, &gekk_move_attack);
// 		return;
// 	}

// 	if (skill->integer == 0)
// 	{
// 		r = frandom();
// 		if (r > 0.5f)
// 			M_SetAnimation(self, &gekk_move_lduck);
// 		else
// 			M_SetAnimation(self, &gekk_move_rduck);
// 		return;
// 	}

// 	self->monsterinfo.pausetime = level.time + eta + 300_ms;
// 	r = frandom();

// 	if (skill->integer == 1)
// 	{
// 		if (r > 0.33f)
// 		{
// 			r = frandom();
// 			if (r > 0.5f)
// 				M_SetAnimation(self, &gekk_move_lduck);
// 			else
// 				M_SetAnimation(self, &gekk_move_rduck);
// 		}
// 		else
// 		{
// 			r = frandom();
// 			if (r > 0.66f)
// 				M_SetAnimation(self, &gekk_move_attack1);
// 			else
// 				M_SetAnimation(self, &gekk_move_attack2);
// 		}
// 		return;
// 	}

// 	if (skill->integer == 2)
// 	{
// 		if (r > 0.66f)
// 		{
// 			r = frandom();
// 			if (r > 0.5f)
// 				M_SetAnimation(self, &gekk_move_lduck);
// 			else
// 				M_SetAnimation(self, &gekk_move_rduck);
// 		}
// 		else
// 		{
// 			r = frandom();
// 			if (r > 0.66f)
// 				M_SetAnimation(self, &gekk_move_attack1);
// 			else
// 				M_SetAnimation(self, &gekk_move_attack2);
// 		}
// 		return;
// 	}

// 	r = frandom();
// 	if (r > 0.66f)
// 		M_SetAnimation(self, &gekk_move_attack1);
// 	else
// 		M_SetAnimation(self, &gekk_move_attack2);
// #endif
// }

//
// SPAWN
//

static void gekk_set_fly_parameters(edict_t* self)
{
	self->monsterinfo.fly_thrusters = false;
	self->monsterinfo.fly_acceleration = 25.f;
	self->monsterinfo.fly_speed = 150.f;
	// only melee, so get in close
	self->monsterinfo.fly_min_distance = 10.f;
	self->monsterinfo.fly_max_distance = 10.f;
}


//================
// ROGUE
void gekk_jump_down(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 100);
	self->velocity += (up * 300);
}

void gekk_jump_up(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 200);
	self->velocity += (up * 450);
}

void gekk_jump_wait_land(edict_t* self)
{
	if (!monster_jump_finished(self) && self->groundentity == nullptr)
		self->monsterinfo.nextframe = self->s.frame;
	else
		self->monsterinfo.nextframe = self->s.frame + 1;
}

mframe_t gekk_frames_jump_up[] = {
	{ ai_move, -8, gekk_jump_up },
	{ ai_move, -8 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, gekk_jump_wait_land },
	{ ai_move }
};
MMOVE_T(gekk_move_jump_up) = { FRAME_leapatk_04, FRAME_leapatk_11, gekk_frames_jump_up, gekk_run };

mframe_t gekk_frames_jump_down[] = {
	{ ai_move, 0, gekk_jump_down },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, gekk_jump_wait_land },
	{ ai_move }
};
MMOVE_T(gekk_move_jump_down) = { FRAME_leapatk_04, FRAME_leapatk_11, gekk_frames_jump_down, gekk_run };

void gekk_jump_updown(edict_t* self, blocked_jump_result_t result)
{
	if (!self->enemy)
		return;

	if (result == blocked_jump_result_t::JUMP_JUMP_UP)
		M_SetAnimation(self, &gekk_move_jump_up);
	else
		M_SetAnimation(self, &gekk_move_jump_down);
}

/*
===
Blocked
===
*/
MONSTERINFO_BLOCKED(gekk_blocked) (edict_t* self, float dist) -> bool
{
	if (auto result = blocked_checkjump(self, dist); result != blocked_jump_result_t::NO_JUMP)
	{
		if (result != blocked_jump_result_t::JUMP_TURN)
			gekk_jump_updown(self, result);
		return true;
	}

	if (blocked_checkplat(self, dist))
		return true;

	return false;
}
// ROGUE
//================

/*QUAKED monster_gekk (1 .5 0) (-16 -16 -24) (16 16 24) Ambush Trigger_Spawn Sight Chant NoJumping
 */
void SP_monster_gekk(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::GEKK);
	const MonsterStatsConfig* config = GetMonsterConfig(self->monsterinfo.monster_type_id);
	if (g_horde->integer) {
		{
			if (brandom())
				gi.sound(self, CHAN_VOICE, sound_hordespawn, 1, ATTN_NORM, 0);
		}
	}

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	sound_hordespawn.assign("gek/wolfboy3.wav");
	sound_swing.assign("gek/gk_atck1.wav");
	sound_hit.assign("gek/gk_atck2.wav");
	sound_hit2.assign("gek/gk_atck3.wav");
	sound_speet.assign("gek/gk_atck4.wav");
	loogie_hit.assign("gek/loogie_hit.wav");
	sound_death.assign("gek/gk_deth1.wav");
	sound_pain1.assign("gek/gk_pain1.wav");
	sound_sight.assign("gek/gk_sght1.wav");
	sound_search.assign("gek/gk_idle1.wav");
	sound_step1.assign("gek/gk_step1.wav");
	sound_step2.assign("gek/gk_step2.wav");
	sound_step3.assign("gek/gk_step3.wav");
	sound_thud.assign("mutant/thud1.wav");
	sound_explod.assign("weapons/rocklx1a.wav");

	sound_chantlow.assign("gek/gek_low.wav");
	sound_chantmid.assign("gek/gek_mid.wav");
	sound_chanthigh.assign("gek/gek_high.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/gekk/tris.md2");
	self->mins = { -18, -18, -24 };
	self->maxs = { 18, 18, 24 };

	gi.modelindex("models/objects/gekkgib/pelvis/tris.md2");
	gi.modelindex("models/objects/gekkgib/arm/tris.md2");
	gi.modelindex("models/objects/gekkgib/torso/tris.md2");
	gi.modelindex("models/objects/gekkgib/claw/tris.md2");
	gi.modelindex("models/objects/gekkgib/leg/tris.md2");
	gi.modelindex("models/objects/gekkgib/head/tris.md2");
	gi.modelindex("models/objects/loogy/tris.md2");

	// Power armor configuration from config
	if (!st.was_key_specified("power_armor_type")) {
		if (config && config->power_armor_type != IT_NULL) {
			self->monsterinfo.power_armor_type = static_cast<item_id_t>(config->power_armor_type);
			if (!st.was_key_specified("power_armor_power"))
				self->monsterinfo.power_armor_power = config->power_armor_power;
		}
	}

	// Regular armor configuration from config
	if (!st.was_key_specified("armor_type")) {
		if (config && config->armor_type != IT_NULL) {
			self->monsterinfo.armor_type = static_cast<item_id_t>(config->armor_type);
			if (!st.was_key_specified("armor_power"))
				self->monsterinfo.armor_power = config->armor_power;
		}
	}


	int base_health = config ? config->health : 125;
	extern int16_t current_wave_level;
	if (g_horde && g_horde->integer && current_wave_level > 0) {
		self->health = ScaleMonsterHealth(base_health, current_wave_level, false);
	} else {
		self->health = base_health * st.health_multiplier;
	}

	self->gib_health = -30;
	self->mass = 300;

	self->pain = gekk_pain;
	self->die = gekk_die;

	self->monsterinfo.stand = gekk_stand;

	self->monsterinfo.walk = gekk_walk;
	self->monsterinfo.run = gekk_run_start;
	//self->monsterinfo.dodge = gekk_dodge;
	self->monsterinfo.attack = gekk_attack;
	self->monsterinfo.melee = gekk_melee;
	self->monsterinfo.sight = gekk_sight;
	self->monsterinfo.search = gekk_search;
	self->monsterinfo.idle = gekk_idle;
	self->monsterinfo.checkattack = gekk_checkattack;
	self->monsterinfo.setskin = gekk_setskin;

	gi.linkentity(self);

	M_SetAnimation(self, &gekk_move_stand);

	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start(self);

	if (self->spawnflags.has(SPAWNFLAG_GEKK_CHANT))
		M_SetAnimation(self, &gekk_move_chant);

	self->monsterinfo.can_jump = !(self->spawnflags & SPAWNFLAG_GEKK_NOJUMPING);
	self->monsterinfo.drop_height = 256;
	// HORDE MOD: Increased jump height from 68 to 88 (30% increase) for better obstacle navigation
	self->monsterinfo.jump_height = 88;
	self->monsterinfo.blocked = gekk_blocked;

	gekk_set_fly_parameters(self);

	ApplyMonsterBonusFlags(self);
}

//======================================================================
void gekk_kl_spit(edict_t* self);
void gekkkl_check_refire(edict_t* self);

void gekkkl_hit_left(edict_t* self)
{
	// ROBUST FIX: Check if the enemy pointer is valid AND the entity is in use and alive.
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t aim = { MELEE_DISTANCE, self->mins[0], 8 };
	if (fire_hit(self, aim, irandom(15, 20), 100)) {
		gi.sound(self, CHAN_WEAPON, sound_hit, 1, ATTN_NORM, 0);
	T_Damage(self, self, self, vec3_origin, self->enemy->s.origin, vec3_origin,
	0, 700, DAMAGE_NONE, MOD_UNKNOWN);
	}
	else
	{
		gi.sound(self, CHAN_WEAPON, sound_swing, 1, ATTN_NORM, 0);
		self->monsterinfo.melee_debounce_time = level.time + 0.2_sec;
	}
}


void gekkkl_hit_right(edict_t* self)
{
	// ROBUST FIX: Apply the same check here for consistency and safety.
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t aim = { MELEE_DISTANCE, self->maxs[0], 8 };
	if (fire_hit(self, aim, irandom(15, 20), 100)) {
		gi.sound(self, CHAN_WEAPON, sound_hit2, 1, ATTN_NORM, 0);
	T_Damage(self, self, self, vec3_origin, self->enemy->s.origin, vec3_origin,
	0, 700, DAMAGE_NONE, MOD_UNKNOWN);
	}
	else
	{
		gi.sound(self, CHAN_WEAPON, sound_swing, 1, ATTN_NORM, 0);
		self->monsterinfo.melee_debounce_time = level.time + 0.2_sec;
	}
}

mframe_t gekkkl_frames_attack1[] = {
	{ ai_charge },
	{ ai_charge, 0, gekkkl_hit_left },
	{ ai_charge },

	{ ai_charge, 0, gekkkl_hit_left },
	{ ai_charge },
	{ ai_charge, 0, gekkkl_hit_left },

	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekkkl_check_refire }
};
MMOVE_T(gekkkl_move_attack1) = { FRAME_clawatk3_01, FRAME_clawatk3_09, gekkkl_frames_attack1, gekk_run_start };

mframe_t gekkkl_frames_attack2[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekkkl_hit_left },

	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekkkl_hit_right },

	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekkkl_check_refire }
};
MMOVE_T(gekkkl_move_attack2) = { FRAME_clawatk5_01, FRAME_clawatk5_09, gekkkl_frames_attack2, gekk_run_start };


void gekkkl_jump_takeoff(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return;
	}

	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	self->s.origin[2] += 1;

	// Save target location
	self->pos1 = self->enemy->s.origin;
	self->pos1[2] += self->enemy->viewheight;

	// Calculate direction and distance to target
	const vec3_t dir = self->pos1 - self->s.origin;
	const float distance = dir.length();
	const float height_diff = self->pos1[2] - self->s.origin[2];

	// 40% chance for HIGH JUMP PLASMA SPAM (ranged harassment)
	if (distance > 256.0f && frandom() < 0.4f)
	{
		// HIGH JUMP variant - jump high and spam plasma from air
		vec3_t aim_dir;
		PredictAim(self, self->enemy, self->s.origin, 500.0f, false, 0.f, &aim_dir, nullptr);
		self->s.angles[YAW] = vectoyaw(aim_dir);

		auto const vectors = AngleVectors(self->s.angles);

		// High jump with reduced forward speed (more vertical)
		self->velocity = vectors.forward * 400.0f + vectors.up * 700.0f;

		self->groundentity = nullptr;
		self->monsterinfo.aiflags |= AI_DUCKED;
		self->monsterinfo.attack_finished = level.time + 3_sec;
		self->touch = gekk_jump_touch;
		self->style = 2; // Mark as PLASMA JUMP (not slam)
		self->gravity = 1.0f;
		self->teleport_time = level.time + 2_sec; // Wall bounce window (2 seconds)
		return;
	}

	// 60% chance for AGGRESSIVE DIVE SLAM (close combat)

	// REDUCED forward speed to prevent overshooting
	const float fwd_speed = clamp(distance * 1.5f, 400.0f, 700.0f);

	// REDUCED base upward velocity to prevent jumping too high
	float up_velocity = 400.0f;

	// Adjust for enemy height
	if (height_diff > 32.0f) {
		up_velocity += height_diff * 0.8f;
	}
	else if (height_diff < -32.0f) {
		up_velocity += height_diff * 0.3f;
	}

	up_velocity = clamp(up_velocity, 250.0f, 600.0f);

	// Aim toward target
	vec3_t aim_dir;
	PredictAim(self, self->enemy, self->s.origin, fwd_speed, false, 0.f, &aim_dir, nullptr);
	self->s.angles[YAW] = vectoyaw(aim_dir);

	// Set velocity toward target
	auto const vectors = AngleVectors(self->s.angles);
	self->velocity = vectors.forward * fwd_speed + vectors.up * up_velocity;

	self->groundentity = nullptr;
	self->monsterinfo.aiflags |= AI_DUCKED;
	self->monsterinfo.attack_finished = level.time + 3_sec;
	self->touch = gekk_jump_touch;
	self->style = 1; // Mark as SLAM DIVE
	self->gravity = 1.0f;
	self->teleport_time = level.time + 2_sec; // Wall bounce window (2 seconds)
}

void gekkkl_check_landing(edict_t* self)
{
	if (self->groundentity)
	{
		// AGGRESSIVE SLAM: If style=1, we're doing the aggressive jump-slam attack
		if (self->style == 1)
		{
			// Guaranteed slam attack on landing
			gi.sound(self, CHAN_WEAPON, sound_thud, 1, ATTN_NORM, 0);
			gi.sound(self, CHAN_AUTO, sound_explod, 0.75f, ATTN_NORM, 0);
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_BERSERK_SLAM);
			vec3_t f, r, start;
			AngleVectors(self->s.angles, f, r, nullptr);
			start = M_ProjectFlashSource(self, { 20.f, -14.3f, -21.f }, f, r);
			trace_t const tr = gi.traceline(self->s.origin, start, self, MASK_SOLID);
			gi.WritePosition(tr.endpos);
			gi.WriteDir({ 0.f, 0.f, 1.f });
			gi.multicast(tr.endpos, MULTICAST_PHS, false);
			self->gravity = 1.0f;
			self->velocity = {};
			self->flags |= FL_KILL_VELOCITY;

			int damage = M_GET_DMG_OR(self, SLAM, 60);
			void T_SlamRadiusDamage(vec3_t point, edict_t* inflictor, edict_t* attacker, float damage, float kick, edict_t* ignore, float radius, mod_t mod);
			T_SlamRadiusDamage(tr.endpos, self, self, damage, 600.f, self, 165, MOD_UNKNOWN);

			// HIGH UPWARD PUSH: Launch enemies into the air
			edict_t* ent = nullptr;
			while ((ent = findradius(ent, tr.endpos, 165)) != nullptr)
			{
				if (!ent->takedamage)
					continue;
				if (ent == self)
					continue;
				if (!CanDamage(ent, self))
					continue;

				// STRONG UPWARD PUSH
				if (ent->groundentity)
				{
					// Stronger push if on ground
					ent->velocity[2] += 500.0f;
				}
				else
				{
					// Medium push if already airborne
					ent->velocity[2] += 350.0f;
				}

				// Remove ground entity so they fly
				ent->groundentity = nullptr;
			}

			self->style = 0; // Reset slam flag
		}
		else if (self->style == 2)
		{
			// PLASMA JUMP landing - normal landing, reset gravity
			gi.sound(self, CHAN_WEAPON, sound_thud, 1, ATTN_NORM, 0);
			self->gravity = 1.0f;
			self->style = 0; // Reset plasma jump flag
		}
		else
		{
			// Normal landing behavior
			gi.sound(self, CHAN_WEAPON, sound_thud, 1, ATTN_NORM, 0);
		}

		self->monsterinfo.attack_finished = 0_ms;

		if (self->monsterinfo.unduck)
			self->monsterinfo.unduck(self);

		self->velocity = {};
		return;
	}

	// PLASMA JUMP: Fire plasma while floating (style=2)
	if (self->style == 2 && self->pos1.lengthSquared() > 0)
	{
		// Fire plasma at saved target position
		gekk_kl_spit(self);

		// Gentle drift toward target (no aggressive dive)
		vec3_t const dir_to_target = (self->pos1 - self->s.origin).normalized();

		// Face target
		self->ideal_yaw = vectoyaw(dir_to_target);
		M_ChangeYaw(self);

		// Update target position
		if (M_HasValidTarget(self))
		{
			self->pos1 = self->enemy->s.origin;
			self->pos1[2] += self->enemy->viewheight;
		}

		// Normal gravity for plasma jump
		self->gravity = 1.0f;
	}
	// AGGRESSIVE DIVE MECHANICS: Steer toward saved target position while airborne (style=1)
	else if (self->style == 1 && self->pos1.lengthSquared() > 0)
	{
		// Calculate direction to saved target
		vec3_t const dir_to_target = (self->pos1 - self->s.origin).normalized();
		float const dist_to_target = (self->pos1 - self->s.origin).length();

		// MUCH MORE AGGRESSIVE: Strong gravity pull + velocity override to land ON enemy
		self->gravity = 2.5f; // Increased gravity pulls down harder

		// Damp horizontal velocity to slow down
		self->velocity[0] *= 0.85f;
		self->velocity[1] *= 0.85f;

		// Strong downward dive force (was 150, now 300+)
		const float dive_strength = 300.0f + (dist_to_target * 0.5f);

		// OVERRIDE velocity to point AT target (not just add to it)
		vec3_t desired_velocity = dir_to_target * dive_strength;

		// Blend current velocity with desired (aggressive steering)
		self->velocity[0] = self->velocity[0] * 0.3f + desired_velocity[0] * 0.7f;
		self->velocity[1] = self->velocity[1] * 0.3f + desired_velocity[1] * 0.7f;
		self->velocity[2] = self->velocity[2] * 0.3f + desired_velocity[2] * 0.7f;

		// Face the target while diving
		self->ideal_yaw = vectoyaw(dir_to_target);
		M_ChangeYaw(self);

		// Much higher max fall speed for aggressive dive
		if (self->velocity[2] < -1800.0f)
			self->velocity[2] = -1800.0f;
	}

	// Paril: allow them to "pull" up ledges
	vec3_t fwd;
	AngleVectors(self->s.angles, fwd, nullptr, nullptr);

	if (fwd.dot(self->velocity) < 200)
		self->velocity += (fwd * 200.f);

	// note to self
	// causing skid
	if (level.time > self->monsterinfo.attack_finished)
		self->monsterinfo.nextframe = FRAME_leapatk_11;
	else
	{
		self->monsterinfo.nextframe = FRAME_leapatk_12;
	}
}

extern const mmove_t gekkkl_move_leapatk;

void gekkkl_check_refire(edict_t* self)
{
	// ROBUST FIX: Apply the same check here for consistency and safety.
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	if (range_to(self, self->enemy) >= RANGE_MID) {
	M_SetAnimation(self, &gekkkl_move_leapatk);
	return gekk_run_start(self);
	}

	else if (range_to(self, self->enemy) <= RANGE_MELEE &&
		self->monsterinfo.melee_debounce_time <= level.time)
	{
		if (self->s.frame == FRAME_clawatk3_09)
			M_SetAnimation(self, &gekkkl_move_attack2);
		else if (self->s.frame == FRAME_clawatk5_09)
			M_SetAnimation(self, &gekkkl_move_attack1);
	}
}

void GekkKLSaveLoc(edict_t* self)
{
	if (M_HasValidTarget(self))
	{
		self->pos1 = self->enemy->s.origin;
		self->pos1[2] += self->enemy->viewheight;
	}
}

// Aggressive follow-up after slam landing
void gekkkl_slam_followup(edict_t* self)
{
	// First check underwater transition
	gekk_check_underwater(self);

	// If we just did a slam (style was recently 1), aggressively follow up with melee
	if (M_HasValidTarget(self) && range_to(self, self->enemy) <= RANGE_MELEE)
	{
		// Immediately transition to melee attack - no hesitation!
		if (frandom() < 0.5f)
			M_SetAnimation(self, &gekkkl_move_attack1);
		else
			M_SetAnimation(self, &gekkkl_move_attack2);
	}
}

mframe_t gekkkl_frames_leapatk[] = {
	{ ai_charge }, 			// frame 0 - Save target location
	{ ai_charge, -0.387f },						// frame 1
	{ ai_charge, -1.113f },						// frame 2
	{ ai_charge, -0.237f },						// frame 3
	{ ai_charge, 6.720f, gekkkl_jump_takeoff },	// frame 4  last frame on ground
	{ ai_charge, 6.414f },						// frame 5  leaves ground
	{ ai_charge, 0.163f, GekkKLSaveLoc },		// frame 6  track target
	{ ai_charge, 28.316f, gekk_kl_spit },						// frame 7
	{ ai_charge, 24.198f, GekkKLSaveLoc },		// frame 8  track target
	{ ai_charge, 31.742f, gekk_kl_spit },						// frame 9
	{ ai_charge, 35.977f, gekkkl_check_landing }, // frame 10  landing + slam check
	{ ai_charge, 12.303f, gekk_stop_skid },		// frame 11  feet back on ground
	{ ai_charge, 20.122f, gekk_stop_skid },		// frame 12
	{ ai_charge, -1.042f, gekk_stop_skid },		// frame 13
	{ ai_charge, 2.556f, gekk_stop_skid },		// frame 14
	{ ai_charge, 0.544f },		// frame 15  recovery
	{ ai_charge, 1.862f },		// frame 16  recovery
	{ ai_charge, 1.224f },		// frame 17  recovery

	{ ai_charge, -0.457f, gekkkl_slam_followup }, // frame 18  AGGRESSIVE MELEE FOLLOWUP
};
MMOVE_T(gekkkl_move_leapatk) = { FRAME_leapatk_01, FRAME_leapatk_19, gekkkl_frames_leapatk, gekk_run_start };




//======================================================================
// GEKKKL - "Inferno Gekk"
// Boss variant with plasma attacks and fire damage
//======================================================================

// Forward declaration for plasma touch (from m_chick.cpp or xatrix)
void plasma_touch(edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self);

// Fire plasma instead of loogie for gekkkl
void fire_gekk_plasma(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed)
{
	edict_t* plasma = G_Spawn();

	plasma->s.origin = start;
	plasma->s.old_origin = start;
	plasma->s.angles = vectoangles(dir);
	plasma->velocity = dir * speed;
	plasma->movetype = MOVETYPE_FLYMISSILE;
	plasma->clipmask = MASK_PROJECTILE;
	plasma->solid = SOLID_BBOX;
	plasma->svflags |= SVF_PROJECTILE;
	plasma->flags |= FL_DODGE;
	plasma->owner = self;

	// Store attacker info
	if (self->svflags & SVF_MONSTER) {
		plasma->projectile_was_player_attacker = false;
		plasma->projectile_attacker_type_id = self->monsterinfo.monster_type_id;
	}

	plasma->touch = plasma_touch;
	plasma->nextthink = level.time + 8_sec;
	plasma->think = G_FreeEdict;
	plasma->dmg = damage;
	plasma->radius_dmg = damage;
	plasma->dmg_radius = 120;
	plasma->s.sound = gi.soundindex("weapons/rockfly.wav");
	plasma->s.modelindex = gi.modelindex("sprites/s_photon.sp2");
	plasma->s.effects |= EF_PLASMA | EF_ANIM_ALLFAST;

	gi.linkentity(plasma);
}

// Gekkkl spit plasma attack
void gekk_kl_spit(edict_t* self)
{
	if (!M_HasValidTarget(self))
		return;

	vec3_t start, dir, forward, right, up;
	vec3_t gekkoffset = { -18, -0.8f, 24 };

	AngleVectors(self->s.angles, forward, right, up);
	start = M_ProjectFlashSource(self, gekkoffset, forward, right);
	start += (up * 2);

	// Blindfire support for plasma
	bool blindfire = (self->monsterinfo.aiflags & AI_MANUAL_STEERING);
	vec3_t end;

	if (blindfire && self->monsterinfo.blind_fire_target)
	{
		end = self->monsterinfo.blind_fire_target;
	}
	else
	{
		// Use saved enemy position from jump/attack
		end = self->pos1;
	}

	dir = end - start;
	dir.normalize();

	int damage = M_GET_DMG_OR(self, PLASMA, 25);
	// Fire plasma bolt instead of loogie
	fire_gekk_plasma(self, start, dir, damage, 900); // Faster and more damaging than loogie

	gi.sound(self, CHAN_WEAPON, sound_speet, 1.0f, ATTN_NORM, 0);
}

// Enhanced attack AI for GekkKL
MONSTERINFO_ATTACK(gekkkl_attack) (edict_t* self) -> void
{
	if (!M_HasValidTarget(self))
		return;

	float dist = range_to(self, self->enemy);

	// Check if this is actually a gekkkl
	if (self->monsterinfo.monster_type_id == static_cast<uint8_t>(horde::MonsterTypeID::GEKKKL))
	{
		// Prefer ranged plasma attacks
		if (dist > RANGE_MELEE)
		{
			// 70% chance to use plasma at range
			if (frandom() < 0.7f)
			{
				M_SetAnimation(self, &gekkkl_move_leapatk); // Spit animation
			}
			else
			{
				// Jump attack
				M_SetAnimation(self, &gekkkl_move_leapatk);
			}
		}
		else
		{
			// Close range - melee or quick jump back and shoot
			if (frandom() < 0.5f)
			{
				gekk_melee(self);
			}
			else
			{
				// Jump back and shoot
				M_SetAnimation(self, &gekkkl_move_leapatk);
			}
		}
	}
	else
	{
		// Fallback to normal gekk attack
		gekk_run(self);
	}
}

/*QUAKED monster_gekkkl (1 .5 0) (-22 -22 -34) (22 22 -11) Ambush Trigger_Spawn Sight
"Inferno Gekk" - Boss variant with plasma attacks and fire damage
Only spawns during special foggy Gekk waves
*/
void SP_monster_gekkkl(edict_t* self)
{
	// Set monster type first
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::GEKKKL);
	const MonsterStatsConfig* config = GetMonsterConfig(self->monsterinfo.monster_type_id);
	// Call base gekk spawn
	SP_monster_gekk(self);

	// Override with gekkkl specifics
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::GEKKKL);
	// Boss stats
	int base_health = config ? config->health : 680;
	extern int16_t current_wave_level;
	if (g_horde && g_horde->integer && current_wave_level > 0) {
		self->health = ScaleMonsterHealth(base_health, current_wave_level, true);  // true = is_boss
	} else {
		self->health = base_health * ED_GetSpawnTemp().health_multiplier;
	}
	self->gib_health = -80;
	self->mass = 400;
	self->s.skinnum = 1; // Different skin if available
	//self->monsterinfo.bonus_flags |= BF_POSSESSED;
	// Scale up
	if (!self->s.scale)
		self->s.scale = 1.2f;

	// Redmutant-like speed
	self->yaw_speed = 25; // Faster turning

	// Enhanced movement like redmutant
	self->monsterinfo.drop_height = 384;
	self->monsterinfo.jump_height = 120; // Higher jumps
	self->monsterinfo.can_jump = true;

	// Override attack
	self->monsterinfo.attack = gekkkl_attack;
	self->monsterinfo.dodge = bonus_monster_dodge;

	// Sound precaching for plasma
	gi.soundindex("weapons/rockfly.wav");
	gi.modelindex("sprites/s_photon.sp2");

	ApplyMonsterBonusFlags(self);
}

void water_to_land(edict_t* self)
{
	self->monsterinfo.aiflags &= ~AI_ALTERNATE_FLY;
	self->flags &= ~FL_SWIM;
	self->yaw_speed = 20;
	self->viewheight = 25;

	M_SetAnimation(self, &gekk_move_leapatk2);

	self->mins = { -18, -18, -24 };
	self->maxs = { 18, 18, 24 };
}

void land_to_water(edict_t* self)
{
	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	self->flags |= FL_SWIM;
	self->yaw_speed = 10;
	self->viewheight = 10;

	M_SetAnimation(self, &gekk_move_swim_start);

	self->mins = { -18, -18, -24 };
	self->maxs = { 18, 18, 16 };
}