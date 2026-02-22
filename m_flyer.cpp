// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

flyer

==============================================================================
*/

#include "g_local.h"
#include "m_flyer.h"
#include "m_flash.h"
#include "shared.h"
#include "horde/g_horde_scaling.h"
#include "monster_constants.h"

static cached_soundindex sound_sight;
static cached_soundindex sound_idle;
static cached_soundindex sound_pain1;
static cached_soundindex sound_pain2;
static cached_soundindex sound_slash;
static cached_soundindex sound_sproing;
static cached_soundindex sound_die;
static cached_soundindex sound_laser;

void flyer_check_melee(edict_t* self);
void flyer_loop_melee(edict_t* self);
//void flyer_setstart(edict_t* self);

// ROGUE - kamikaze stuff
void flyer_kamikaze(edict_t* self);
void flyer_kamikaze_check(edict_t* self);
void flyer_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);

MONSTERINFO_SIGHT(flyer_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_IDLE(flyer_idle) (edict_t* self) -> void
{
	gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

void flyer_pop_blades(edict_t* self)
{
	gi.sound(self, CHAN_VOICE, sound_sproing, 1, ATTN_NORM, 0);
}

mframe_t flyer_frames_stand[] = {
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
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }
};
MMOVE_T(flyer_move_stand) = { FRAME_stand01, FRAME_stand45, flyer_frames_stand, nullptr };

mframe_t flyer_frames_walk[] = {
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 }
};
MMOVE_T(flyer_move_walk) = { FRAME_stand01, FRAME_stand45, flyer_frames_walk, nullptr };

mframe_t flyer_frames_run[] = {
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 }
};
MMOVE_T(flyer_move_run) = { FRAME_stand01, FRAME_stand45, flyer_frames_run, nullptr };

mframe_t flyer_frames_kamizake[] = {
	{ ai_charge, 40, flyer_kamikaze_check },
	{ ai_charge, 40, flyer_kamikaze_check },
	{ ai_charge, 40, flyer_kamikaze_check },
	{ ai_charge, 40, flyer_kamikaze_check },
	{ ai_charge, 40, flyer_kamikaze_check }
};
MMOVE_T(flyer_move_kamikaze) = { FRAME_rollr02, FRAME_rollr06, flyer_frames_kamizake, flyer_kamikaze };

MONSTERINFO_RUN(flyer_run) (edict_t* self) -> void
{
	if (self->mass > 50)
		M_SetAnimation(self, &flyer_move_kamikaze);
	else if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &flyer_move_stand);
	else
		M_SetAnimation(self, &flyer_move_run);
}

MONSTERINFO_WALK(flyer_walk) (edict_t* self) -> void
{
	if (self->mass > 50)
		flyer_run(self);
	else
		M_SetAnimation(self, &flyer_move_walk);
}

MONSTERINFO_STAND(flyer_stand) (edict_t* self) -> void
{
	if (self->mass > 50)
		flyer_run(self);
	else
		M_SetAnimation(self, &flyer_move_stand);
}

// ROGUE - kamikaze stuff

void flyer_kamikaze_explode(edict_t* self)
{
	// FIX: Use the more robust M_HasValidTarget check.
	if (!M_HasValidTarget(self))
	{
		// If there's no valid target, we should still explode but without a direction.
		flyer_die(self, nullptr, nullptr, 0, vec3_origin, MOD_EXPLOSIVE);
		return;
	}

	vec3_t dir;

	edict_t* commander = self->monsterinfo.commander;

	// Check if the commander is valid and is a Carrier type.
	if (commander && commander->inuse && horde::IsMonsterType(commander, horde::MonsterTypeID::CARRIER))
	{
		commander->monsterinfo.monster_slots++;
	}

	// This block is now safe.
	dir = self->enemy->s.origin - self->s.origin;
	T_Damage(self->enemy, self, self, dir, self->s.origin, vec3_origin, 50, 50, DAMAGE_RADIUS, MOD_UNKNOWN);

	flyer_die(self, nullptr, nullptr, 0, dir, MOD_EXPLOSIVE);
}
void flyer_kamikaze(edict_t* self)
{
	M_SetAnimation(self, &flyer_move_kamikaze);
}

void flyer_kamikaze_check(edict_t* self)
{
	float dist;

	// PMM - this needed because we could have gone away before we get here (blocked code)
	if (!self->inuse)
		return;

	if (!M_HasValidTarget(self) || !visible(self, self->enemy))
	{
		flyer_kamikaze_explode(self);
		return;
	}

	self->s.angles[0] = vectoangles(self->enemy->s.origin - self->s.origin).x;

	self->goalentity = self->enemy;

	dist = realrange(self, self->enemy);

	if (dist < 90)
		flyer_kamikaze_explode(self);
}

static void flyer_checkstrafe(edict_t* self)
{
	// --- Tunable Parameters ---
	// The forward speed of the flyer during its attack run.
	constexpr float FORWARD_ATTACK_SPEED = 350.0f;
	// How far to the side the flyer checks for walls before strafing.
	constexpr float STRAFE_CHECK_DISTANCE = 192.0f;
	// The base speed for the strafe.
	constexpr float BASE_STRAFE_SPEED = 300.0f;
	// The random additional speed for the strafe.
	constexpr float RANDOM_STRAFE_SPEED = 200.0f;

	// Validate enemy exists and is visible
	if (!self->enemy || !visible(self, self->enemy))
		return;

	const float range = range_to(self, self->enemy);
	if (range > RANGE_MID) // Only perform this maneuver at mid-range or closer
		return;

	// --- Strafe Decision Logic ---
	float strafe_chance = 0.5f; // Base chance to try strafing
	if (self->enemy->client && (self->enemy->client->buttons & BUTTON_ATTACK))
		strafe_chance += 0.25f; // More likely to dodge if player is firing
	if (self->health < self->max_health * 0.65f)
		strafe_chance += 0.4f; // More likely to dodge if wounded

	if (frandom() < strafe_chance)
	{
		vec3_t forward, right;
		AngleVectors(self->s.angles, forward, right, nullptr);

		// Ensure vectors are valid before proceeding
		if (!is_valid_vector(right) || !is_valid_vector(forward)) {
			return;
		}
		// PERFORMANCE: AngleVectors already returns normalized vectors.
		// right.normalize();
		// forward.normalize();

		// --- Intelligent Strafe Direction Check ---
		// 1. Randomly pick a preferred direction (-1 for left, 1 for right)
		float strafe_direction = (frandom() < 0.5f) ? -1.0f : 1.0f;

		// 2. Check if the preferred direction is clear
		vec3_t check_end = self->s.origin + (right * strafe_direction * STRAFE_CHECK_DISTANCE);
		trace_t tr = gi.traceline(self->s.origin, check_end, self, MASK_MONSTERSOLID);

		if (tr.fraction < 1.0f) // The path is blocked
		{
			// 3. Try the other direction
			strafe_direction *= -1.0f; // Flip direction
			check_end = self->s.origin + (right * strafe_direction * STRAFE_CHECK_DISTANCE);
			tr = gi.traceline(self->s.origin, check_end, self, MASK_MONSTERSOLID);

			if (tr.fraction < 1.0f) // Both directions are blocked
			{
				return; // Abort the strafe entirely
			}
		}

		// --- Apply Strafe ---
		// At this point, 'strafe_direction' is guaranteed to be a clear path.
		const float strafe_speed = BASE_STRAFE_SPEED + (frandom() * RANDOM_STRAFE_SPEED);
		const float vertical_velocity = self->velocity.z; // Preserve existing vertical speed

		// Construct a new velocity instead of adding to the old one.
		// This prevents runaway speed buildup from ai_charge and this function.
		self->velocity = (forward * FORWARD_ATTACK_SPEED) + (right * strafe_direction * strafe_speed);
		self->velocity.z = vertical_velocity; // Restore vertical speed

		// Set lefty for compatibility with any other code that might check it
		self->monsterinfo.lefty = (strafe_direction < 0);

		// Prevent the AI from making another move too quickly
		self->monsterinfo.pausetime = level.time + random_time(0.75_sec, 1.3_sec);
	}
}

void flyer_rocket(edict_t* self)
{
	// Basic enemy check - blindfire logic needs to execute
	if (!M_HasEnemy(self))
		return;

	// Per-shot cooldown gate to prevent rocket chain spam in refire loops.
	if (self->monsterinfo.fire_wait > level.time)
		return;

	int damage = M_GET_DMG_OR(self, ROCKET, 35);

	vec3_t	forward;
	vec3_t	start, end, dir;
	float	dist, chance;
	trace_t trace;
	int config_speed = M_ROCKET_SPEED(self);
	int rocketSpeed = config_speed > 0 ? config_speed : 850;
	bool blindfire = (self->monsterinfo.aiflags & AI_MANUAL_STEERING);

	if (blindfire && !visible(self, self->enemy))
	{
		if (!self->monsterinfo.blind_fire_target)
			return;
		end = self->monsterinfo.blind_fire_target;
	}
	else
	{
		// Not blindfiring - need fully valid target
		if (!M_HasValidTarget(self))
			return;

		end = self->enemy->s.origin;
	}

	dir = end - self->s.origin;
	dir.normalize();
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	chance = dir.dot(forward);
	if (chance < 0.98f)
		return;

	// Allow firing when visible OR during blindfire
	if (visible(self, self->enemy) || blindfire)
	{
		start = self->s.origin;

		// aim for the head.
		if (!blindfire)
		{
			if ((self->enemy) && (self->enemy->client))
				end[2] += self->enemy->viewheight;
			else
				end[2] += 22;
		}

		dir = end - start;
		dist = dir.length();

		// check for predictive fire
		// Paril: adjusted to be a bit more fair
		if (!blindfire)
		{
			PredictAim(self, self->enemy, start, rocketSpeed, true, (frandom(3.f - skill->integer) / 3.f) - frandom(0.05f * (3.f - skill->integer)), &dir, nullptr);
		}

		dir.normalize();
		trace = gi.traceline(start, end, self, MASK_PROJECTILE);
		if (trace.ent == self->enemy || trace.ent == world)
		{
			if (dist * trace.fraction > 72)
			{
				monster_fire_rocket(self, start, dir, damage, rocketSpeed, MZ2_TURRET_ROCKET);
				self->monsterinfo.fire_wait = level.time + random_time(850_ms, 1.4_sec);
			}
		}
	}
}

void flyer_reattack_rocket(edict_t* self)
{
	// if our enemy is still valid, then continue firing
	if (self->enemy && !level.intermissiontime)
	{
		if (frandom() < 0.6f && self->monsterinfo.fire_wait <= level.time && frandom() < 0.2f)
		{
			flyer_rocket(self);
			self->monsterinfo.nextframe = FRAME_rollr03;
			return;
		}
	}

	// end attack
	self->monsterinfo.attack_finished = level.time + random_time(1.1_sec, 1.9_sec);
}

mframe_t flyer_frames_rollright[] = {
	{ ai_charge, 3,flyer_checkstrafe },
	{ ai_charge, 3,flyer_checkstrafe },
	{ ai_charge, 3,flyer_checkstrafe },
	{ ai_charge, 0, flyer_rocket },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 3, flyer_reattack_rocket }
};
MMOVE_T(flyer_move_rollright) = { FRAME_rollr01, FRAME_rollr09, flyer_frames_rollright, flyer_run };

mframe_t flyer_frames_rollleft[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(flyer_move_rollleft) = { FRAME_rollf01, FRAME_rollf09, flyer_frames_rollleft, flyer_run };


mframe_t flyer_frames_pain3[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(flyer_move_pain3) = { FRAME_pain301, FRAME_pain304, flyer_frames_pain3, flyer_run };

mframe_t flyer_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(flyer_move_pain2) = { FRAME_pain201, FRAME_pain204, flyer_frames_pain2, flyer_run };

mframe_t flyer_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(flyer_move_pain1) = { FRAME_pain101, FRAME_pain109, flyer_frames_pain1, flyer_run };

#if 0
mframe_t flyer_frames_defense[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move }, // Hold this frame
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(flyer_move_defense) = { FRAME_defens01, FRAME_defens06, flyer_frames_defense, nullptr };

mframe_t flyer_frames_bankright[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(flyer_move_bankright) = { FRAME_bankr01, FRAME_bankr07, flyer_frames_bankright, nullptr };

mframe_t flyer_frames_bankleft[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(flyer_move_bankleft) = { FRAME_bankl01, FRAME_bankl07, flyer_frames_bankleft, nullptr };
#endif

void flyer_fire(edict_t* self, monster_muzzleflash_id_t flash_number)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	int damage = M_GET_DMG_OR(self, BLASTER_BOLT, 4);

	vec3_t	  start;
	vec3_t	  forward, right;
	vec3_t	  end;
	vec3_t	  dir;

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	int config_speed = M_BLASTER_BOLT_SPEED(self);
	int speed = config_speed > 0 ? config_speed : 1150;

	if (frandom() < 0.3f)
		PredictAim(self, self->enemy, start, speed, true, 0, &dir, &end);
	else
		end = self->enemy->s.origin;
	end[2] += self->enemy->viewheight;
	dir = end - start;
	dir.normalize();

	monster_fire_blaster_bolt(self, start, forward, damage, speed, flash_number, EF_HYPERBLASTER, 0);
}

void flyer_fireleft(edict_t* self)
{
	flyer_fire(self, MZ2_FLYER_BLASTER_1);
}

void flyer_fireright(edict_t* self)
{
	flyer_fire(self, MZ2_FLYER_BLASTER_2);
}

void flyer_reattack_blaster(edict_t* self)
{
	if (frandom() < 0.7f && visible(self, self->enemy))
		self->monsterinfo.nextframe = FRAME_attak204;
	else
		flyer_run(self);
}


mframe_t flyer_frames_attack2normal[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge , -15, flyer_reattack_blaster},
	{ ai_charge },
	{ ai_charge },
};
MMOVE_T(flyer_move_attack2normal) = { FRAME_attak201, FRAME_attak217, flyer_frames_attack2normal, flyer_run };

// PMM
// circle strafe frames

mframe_t flyer_frames_attack3normal[] = {
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge , -15, flyer_reattack_blaster},
	{ ai_charge, 10 },
	{ ai_charge, 10 }
};
MMOVE_T(flyer_move_attack3normal) = { FRAME_attak201, FRAME_attak217, flyer_frames_attack3normal, flyer_run };

mframe_t flyer_frames_attack2[] = {
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge },
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge },
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge }
};
MMOVE_T(flyer_move_attack2) = { FRAME_attak201, FRAME_attak217, flyer_frames_attack2, flyer_run };

// PMM
// circle strafe frames

mframe_t flyer_frames_attack3[] = {
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10 },
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10 }
};
MMOVE_T(flyer_move_attack3) = { FRAME_attak201, FRAME_attak217, flyer_frames_attack3, flyer_run };
// pmm

void flyer_slash_left(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		self->monsterinfo.melee_debounce_time = level.time + 1.5_sec;
		return; // Stop immediately if the target is invalid.
	}

	const vec3_t aim = { MELEE_DISTANCE, self->mins[0], 0 };
	if (!fire_hit(self, aim, 3, 0))
		self->monsterinfo.melee_debounce_time = level.time + 1.5_sec;
	gi.sound(self, CHAN_WEAPON, sound_slash, 1, ATTN_NORM, 0);
}

void flyer_slash_right(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		self->monsterinfo.melee_debounce_time = level.time + 1.5_sec;
		return; // Stop immediately if the target is invalid.
	}

	const	vec3_t aim = { MELEE_DISTANCE, self->maxs[0], 0 };
	if (!fire_hit(self, aim, 3, 0))
		self->monsterinfo.melee_debounce_time = level.time + 1.5_sec;
	gi.sound(self, CHAN_WEAPON, sound_slash, 1, ATTN_NORM, 0);
}

mframe_t flyer_frames_start_melee[] = {
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(flyer_move_start_melee) = { FRAME_attak101, FRAME_attak106, flyer_frames_start_melee, flyer_loop_melee };

mframe_t flyer_frames_end_melee[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(flyer_move_end_melee) = { FRAME_attak119, FRAME_attak121, flyer_frames_end_melee, flyer_run };

mframe_t flyer_frames_loop_melee[] = {
	{ ai_charge }, // Loop Start
	{ ai_charge },
	{ ai_charge, 0, flyer_slash_left }, // Left Wing Strike
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, flyer_slash_left }, // Left Wing Strike
	{ ai_charge },
	{ ai_charge, 0, flyer_slash_right }, // Right Wing Strike
	{ ai_charge },
	{ ai_charge, 0, flyer_slash_right }, // Right Wing Strike
	{ ai_charge },
	{ ai_charge } // Loop Ends
};
MMOVE_T(flyer_move_loop_melee) = { FRAME_attak107, FRAME_attak118, flyer_frames_loop_melee, flyer_check_melee };

void flyer_loop_melee(edict_t* self)
{
	M_SetAnimation(self, &flyer_move_loop_melee);
}

static void flyer_set_fly_parameters(edict_t* self, bool melee)
{
	if (melee)
	{
		// engage thrusters for a slice
		self->monsterinfo.fly_pinned = false;
		self->monsterinfo.fly_thrusters = true;
		self->monsterinfo.fly_position_time = 0_sec;
		self->monsterinfo.fly_acceleration = 20.f;
		self->monsterinfo.fly_speed = 210.f;
		self->monsterinfo.fly_min_distance = 0.f;
		self->monsterinfo.fly_max_distance = 10.f;
	}
	else
	{
		self->monsterinfo.fly_thrusters = false;
		self->monsterinfo.fly_acceleration = 15.f;
		self->monsterinfo.fly_speed = 165.f;
		self->monsterinfo.fly_min_distance = 45.f;
		self->monsterinfo.fly_max_distance = 200.f;
	}
}

// Posiciones de los láseres
constexpr vec3_t FLYER_LEFT_LASER = { 14.1f, -13.4f, -7.0f };
constexpr vec3_t FLYER_RIGHT_LASER = { 14.1f, 13.4f, -7.0f };


// This function runs on a temporary "flare controller" entity.
// It's responsible for updating the position of the two warning flares
// so they stick to the flyer as it moves.
THINK(flyer_flare_think) (edict_t* controller) -> void
{
    edict_t* self = controller->owner; // The flyer is the owner of the controller

    // --- Self-Destruct and Safety Checks ---
    if (level.time >= controller->timestamp || !self || !self->inuse || self->deadflag || controller->enemy != self->enemy)
    {
        // Find and remove the flares using the correct pointers.
        if (controller->mynoise && controller->mynoise->inuse)
            G_FreeEdict(controller->mynoise); // Free left flare
        if (controller->mynoise2 && controller->mynoise2->inuse)
            G_FreeEdict(controller->mynoise2); // Free right flare
        
        G_FreeEdict(controller); // Free the controller itself
        return;
    }

    // --- Update Flare Positions ---
    vec3_t forward, right, up;
    AngleVectors(self->s.angles, forward, right, up);

    // Update left flare (stored in mynoise)
    if (controller->mynoise && controller->mynoise->inuse)
    {
        vec3_t left_pos = self->s.origin + (forward * FLYER_LEFT_LASER.x);
        left_pos += (right * FLYER_LEFT_LASER.y);
        left_pos += (up * FLYER_LEFT_LASER.z);
        controller->mynoise->s.origin = left_pos;
        gi.linkentity(controller->mynoise);
    }

    // Update right flare (stored in mynoise2)
    if (controller->mynoise2 && controller->mynoise2->inuse)
    {
        vec3_t right_pos = self->s.origin + (forward * FLYER_RIGHT_LASER.x);
        right_pos += (right * FLYER_RIGHT_LASER.y);
        right_pos += (up * FLYER_RIGHT_LASER.z);
        controller->mynoise2->s.origin = right_pos;
        gi.linkentity(controller->mynoise2);
    }

    // Keep thinking until the timer runs out
    controller->nextthink = level.time + FRAME_TIME_MS;
}

// Spawns a controller that manages two temporary warning flares.
void flyer_laser_warn(edict_t* self)
{
	constexpr uint32_t WARNING_FLARE_COLOR = 0xFF0000FF;
	constexpr gtime_t FLARE_LIFETIME = 0.6_sec;

	if (!self || !self->inuse)
		return;

	// --- Clean up any previous controller and its flares before creating a new one ---
	// This prevents orphaned entities if the function is called unexpectedly again.
	if (self->teamchain && self->teamchain->inuse)
	{
		edict_t* controller = self->teamchain;

		// Must free the flares BEFORE freeing the controller
		// (G_FreeEdict doesn't run the think function)
		if (controller->mynoise && controller->mynoise->inuse)
			G_FreeEdict(controller->mynoise);
		if (controller->mynoise2 && controller->mynoise2->inuse)
			G_FreeEdict(controller->mynoise2);

		G_FreeEdict(controller);
		self->teamchain = nullptr;
	}
	// --- END cleanup ---

	// --- Spawn the Controller Entity ---
	edict_t* controller = G_Spawn();
	if (!controller)
		return;

	// --- MODIFICATION: Store the controller pointer on the flyer ---
	self->teamchain = controller;
	// --- END MODIFICATION ---

	controller->classname = "flyer_flare_controller";
	controller->owner = self;
	controller->movetype = MOVETYPE_NONE;
	controller->solid = SOLID_NOT;
	controller->enemy = self->enemy;

	// --- Timer Setup ---
	controller->timestamp = level.time + FLARE_LIFETIME;
	controller->think = flyer_flare_think;
	controller->nextthink = level.time + FRAME_TIME_MS;

	// --- Spawn the Flares and Attach to Controller ---
	edict_t* left_flare = G_Spawn();
	if (left_flare)
	{
		left_flare->classname = "misc_flare";
		left_flare->owner = controller;
		left_flare->s.skinnum = WARNING_FLARE_COLOR;
		left_flare->spawnflags = 9_spawnflag;
		spawn_temp_t st{};
		st.radius = 0.5f;
		ED_CallSpawn(left_flare, st);
		gi.linkentity(left_flare);
		controller->mynoise = left_flare; // Store left flare reference in mynoise
	}

	edict_t* right_flare = G_Spawn();
	if (right_flare)
	{
		right_flare->classname = "misc_flare";
		right_flare->owner = controller;
		right_flare->s.skinnum = WARNING_FLARE_COLOR;
		right_flare->spawnflags = 9_spawnflag;
		spawn_temp_t st{};
		st.radius = 0.5f;
		ED_CallSpawn(right_flare, st);
		gi.linkentity(right_flare);
		controller->mynoise2 = right_flare; // Store right flare reference in mynoise2
	}

	// The first call to flyer_flare_think will position the flares correctly.
	flyer_flare_think(controller);
}

PRETHINK(flyer_left_laser_update) (edict_t* laser) -> void
{
	edict_t* self = laser->owner;

	if (!M_HasValidTarget(self))
	{
		return; // Can't at a non-existent or dead target.
	}

	vec3_t start, forward, right, up, dir;

	// Obtener los vectores de dirección
	AngleVectors(self->s.angles, forward, right, up);

	// Calcular el punto de inicio del láser izquierdo
	start = self->s.origin + (forward * FLYER_LEFT_LASER.x);
	start += (right * FLYER_LEFT_LASER.y);
	start += (up * FLYER_LEFT_LASER.z);

	// Predicción de objetivo con ligera dispersión para efecto de abanico
	if (self->enemy)
	{
		const	float spread = sinf(level.time.seconds() * 15.0f) * 0.2f; // Oscilación suave
		vec3_t target = self->enemy->s.origin;
		target += right * spread * 32.0f;

		PredictAim(self, self->enemy, start, 0, false, 0.0f, &dir, nullptr);
		dir += right * spread;
		dir.normalize();
	}
	else
		dir = forward;

	laser->s.origin = start;
	laser->movedir = dir;
	gi.linkentity(laser);
	dabeam_update(laser, false);
}

PRETHINK(flyer_right_laser_update) (edict_t* laser) -> void
{
	edict_t* self = laser->owner;

	if (!M_HasValidTarget(self))
	{
		return; // Can't at a non-existent or dead target.
	}

	vec3_t start, forward, right, up, dir;

	// Obtener los vectores de dirección
	AngleVectors(self->s.angles, forward, right, up);

	// Calcular el punto de inicio del láser derecho
	start = self->s.origin + (forward * FLYER_RIGHT_LASER.x);
	start += (right * FLYER_RIGHT_LASER.y);
	start += (up * FLYER_RIGHT_LASER.z);

	// Predicción de objetivo con ligera dispersión para efecto de abanico
	if (self->enemy)
	{
		const	float spread = sinf(level.time.seconds() * 5.0f + PIf) * 0.02f; // Desfasado del izquierdo
		vec3_t target = self->enemy->s.origin;
		target += right * spread * 32.0f;

		PredictAim(self, self->enemy, start, 0, false, 0.3f, &dir, nullptr);
		dir += right * spread;
		dir.normalize();
	}
	else
		dir = forward;

	laser->s.origin = start;
	laser->movedir = dir;
	gi.linkentity(laser);
	dabeam_update(laser, false);
}

void flyer_laser_on(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	int damage = M_GET_DMG_OR(self, DABEAM, 3);

	// Sonido de láser
	gi.sound(self, CHAN_WEAPON, sound_laser, 1, ATTN_NORM, 0);

	// Disparar ambos láseres
	monster_fire_dabeam(self, damage, false, flyer_left_laser_update);
	monster_fire_dabeam(self, damage, true, flyer_right_laser_update);
}

void flyer_laser_off(edict_t* self)
{
	// --- Original beam cleanup ---
	if (self->beam) {
		G_FreeEdict(self->beam);
		self->beam = nullptr;
	}
	if (self->beam2) {
		G_FreeEdict(self->beam2);
		self->beam2 = nullptr;
	}

	// --- NEW: Direct cleanup using the stored pointer ---
	// This is much more efficient than looping through all entities.
	if (self->teamchain && self->teamchain->inuse)
	{
		edict_t* controller = self->teamchain;

		// The controller's think function frees the flares when it expires,
		// but if we're turning off the laser early, we need to clean them up ourselves.
		if (controller->mynoise && controller->mynoise->inuse)
			G_FreeEdict(controller->mynoise);
		if (controller->mynoise2 && controller->mynoise2->inuse)
			G_FreeEdict(controller->mynoise2);

		G_FreeEdict(controller);
		self->teamchain = nullptr; // Clear the pointer to prevent reuse
	}
}

void flyer_recharge(edict_t* self);

// Frames for the laser attack - CORRECTED to fit within 17 frames
mframe_t flyer_frames_laser_right[] = {
    // --- Warning Phase (Telegraph) ---
    { ai_charge, 0, flyer_laser_warn },  // Frame 1 (attak201): Spawn warning flares
    { ai_charge, 0, nullptr },           // Frame 2 (attak202): Hold
    { ai_charge, 0, nullptr },           // Frame 3 (attak203): Hold
    { ai_charge, 0, nullptr },           // Frame 4 (attak204): Hold
    { ai_charge, 0, nullptr },           // Frame 5 (attak205): Hold (Total warning time: ~0.5s)
    
    // --- Firing Phase ---
    { ai_charge, 0, flyer_laser_on },    // Frame 6 (attak206): Start firing
    { ai_charge, 0, flyer_laser_on },    // Frame 7 (attak207)
    { ai_charge, 0, flyer_laser_on },    // Frame 8 (attak208)
    { ai_charge, 0, flyer_laser_on },    // Frame 9 (attak209)
    { ai_charge, 0, flyer_laser_on },    // Frame 10 (attak210)
    { ai_charge, 0, flyer_laser_on },    // Frame 11 (attak211)
    { ai_charge, 0, flyer_laser_on },    // Frame 12 (attak212)
    { ai_charge, 0, flyer_laser_on },    // Frame 13 (attak213)
    { ai_charge, 0, flyer_laser_on },    // Frame 14 (attak214)
    { ai_charge, 0, flyer_laser_on },    // Frame 15 (attak215)
    { ai_charge, 0, flyer_laser_on },    // Frame 16 (attak216)
    { ai_charge, 0, flyer_laser_on },    // Frame 17 (attak217)
};
// CORRECTED MMOVE_T definition
MMOVE_T(flyer_move_laser_right) = { FRAME_attak201, FRAME_attak217, flyer_frames_laser_right, flyer_recharge };

mframe_t flyer_frames_laser_left[] = {
	{ ai_charge, 0, nullptr },           // Preparación
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	//	{ ai_charge, 0, flyer_laser_off },   // Fin del láser
		{ ai_charge, 0, nullptr }            // Recuperación
};
//  Changed end frame from FRAME_bankl07 to FRAME_bankl06 to match the 6-element array.
MMOVE_T(flyer_move_laser_left) = { FRAME_bankl01, FRAME_bankl06, flyer_frames_laser_left, flyer_recharge };

mframe_t flyer_frames_laser_recharge[] = {
	{ ai_charge, 0, nullptr },           // Preparación
	{ ai_charge, 0, flyer_laser_off },   // Fin del láser
	{ ai_charge, 0, nullptr },            // Recuperación
	{ ai_charge, 0, nullptr },            // Recuperación
	{ ai_charge, 0, nullptr },            // Recuperación
	{ ai_charge, 0, nullptr }            // Recuperación
};
MMOVE_T(flyer_move_laser_recharge) = { FRAME_defens01, FRAME_defens06, flyer_frames_laser_recharge, flyer_run };

void flyer_recharge(edict_t* self)
{
	M_SetAnimation(self, &flyer_move_laser_recharge);
}

MONSTERINFO_ATTACK(flyer_attack)(edict_t* self) -> void
{
	monster_done_dodge(self);

	if (!M_HasValidTarget(self))
	{
		return; // Can't at a non-existent or dead target.
	}

	// Kamikaze flyers have their own run/attack logic
	if (self->mass > 50)
	{
		flyer_run(self);
		return;
	}

	if (!self->enemy)
		return;

	const float range = range_to(self, self->enemy);

	// --- Attack Priority 1: Melee Attack ---
	// If we are very close, perform a fly-by slicing attack.
	if (!IsBonusMonster(self) && visible(self, self->enemy) && range <= 225.f && frandom() > (range / 225.f) * 0.35f)
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		M_SetAnimation(self, &flyer_move_start_melee);
		flyer_set_fly_parameters(self, true);
		return; // Attack chosen, exit function
	}

	// --- Attack Priority 2: Special Laser Attack ---
	// If not doing melee, consider the special laser attack under specific conditions.
	// We check if the enemy is wounded and at a good medium range.
	if (range > 150 && range < 400 && frandom() < 0.28f)
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		M_SetAnimation(self, &flyer_move_laser_right);
		return; // Attack chosen, exit function
	}

	// --- Attack Priority 3: Default Ranged Attack (Blaster or Rocket) ---
	// If no other special attacks were chosen, decide between blasters and rockets.
	self->monsterinfo.attack_state = AS_STRAIGHT;

	bool use_rocket_attack;

	if (self->monsterinfo.bonus_flags != BF_NONE)
	{
		// BONUSSED FLYER: Prefers rockets.
		// 80% chance for rockets, 20% chance for blasters.
		use_rocket_attack = (frandom() < 0.80f);
	}
	else
	{
		// NORMAL FLYER: Prefers blasters.
		// 40% chance for rockets, 60% chance for blasters.
		use_rocket_attack = (frandom() < 0.40f);
	}

	// Set the animation based on the decision
	const bool rocket_ready = (self->monsterinfo.fire_wait <= level.time);
	if (use_rocket_attack && rocket_ready)
	{
		M_SetAnimation(self, &flyer_move_rollright);
	}
	else
	{
		// Use the blaster attack with re-attack logic (or fallback while rockets are cooling down).
		M_SetAnimation(self, &flyer_move_attack2normal);
	}

	// --- General Flight Behavior (runs after an attack is chosen) ---
	// Pin down behavior to hover in place occasionally.
	if (!self->monsterinfo.fly_pinned && brandom() && visible(self, self->enemy))
	{
		self->monsterinfo.fly_pinned = true;
		self->monsterinfo.fly_position_time = max(self->monsterinfo.fly_position_time,
			self->monsterinfo.fly_position_time + 1.7_sec);
		if (brandom())
			self->monsterinfo.fly_ideal_position = self->s.origin + (self->velocity * frandom());
		else
			self->monsterinfo.fly_ideal_position += self->enemy->s.origin;
	}
}

MONSTERINFO_MELEE(flyer_melee) (edict_t* self) -> void
{
	if (self->mass > 50)
		flyer_run(self);
	else
	{
		M_SetAnimation(self, &flyer_move_start_melee);
		flyer_set_fly_parameters(self, true);
	}
}

void flyer_check_melee(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		M_SetAnimation(self, &flyer_move_end_melee);
		flyer_set_fly_parameters(self, false);
		return;
	}

	// Check if enemy is still visible - prevent looping on lost target
	if (!visible(self, self->enemy))
	{
		M_SetAnimation(self, &flyer_move_end_melee);
		flyer_set_fly_parameters(self, false);
		return;
	}

	// Limit melee loops to prevent infinite slashing
	constexpr int MAX_MELEE_LOOPS = 3;

	// Use attack_finished as a loop counter
	if (self->monsterinfo.attack_finished == 0_ms)
	{
		// First loop - initialize counter
		self->monsterinfo.attack_finished = gtime_t::from_sec(1);
	}
	else
	{
		// Increment loop counter
		self->monsterinfo.attack_finished += gtime_t::from_sec(1);
	}

	if (range_to(self, self->enemy) <= RANGE_MELEE)
	{
		// Check loop limit
		if (self->monsterinfo.attack_finished.seconds() < MAX_MELEE_LOOPS)
		{
			if (self->monsterinfo.melee_debounce_time <= level.time)
			{
				M_SetAnimation(self, &flyer_move_loop_melee);
				return;
			}
		}
	}

	// Exit melee - reset counter
	self->monsterinfo.attack_finished = 0_ms;
	M_SetAnimation(self, &flyer_move_end_melee);
	flyer_set_fly_parameters(self, false);
}

PAIN(flyer_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	int n;

	// kamikazes don't feel pain
	if (self->mass != 50)
		return;

	// Don't take pain during special attacks (laser or rocket).
	// This prevents these key attacks from being interrupted.
	if (self->monsterinfo.active_move == &flyer_move_laser_right ||
		self->monsterinfo.active_move == &flyer_move_laser_left ||
		self->monsterinfo.active_move == &flyer_move_laser_recharge ||
		self->monsterinfo.active_move == &flyer_move_rollright)
	{
		return;
	}

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;

	// Play a random pain sound
	n = irandom(3);
	if (n == 0)
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else if (n == 1)
		gi.sound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);

	// Don't play pain animation on nightmare skill
	if (!M_ShouldReactToPain(self, mod))
		return;

	// Reset flight parameters to default non-melee behavior
	flyer_set_fly_parameters(self, false);

	// Set a random pain animation
	if (n == 0)
		M_SetAnimation(self, &flyer_move_pain1);
	else if (n == 1)
		M_SetAnimation(self, &flyer_move_pain2);
	else
		M_SetAnimation(self, &flyer_move_pain3);
}

MONSTERINFO_SETSKIN(flyer_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

DIE(flyer_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	gi.sound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);

    flyer_laser_off(self);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_EXPLOSION1);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PHS, false);

	self->s.skinnum /= 2;

	ThrowGibs(self, 55, {
		{ 2, "models/objects/gibs/sm_metal/tris.md2" },
		{ 2, "models/objects/gibs/sm_meat/tris.md2" },
		{ "models/monsters/flyer/gibs/base.md2", GIB_SKINNED },
		{ 2, "models/monsters/flyer/gibs/gun.md2", GIB_SKINNED },
		{ 2, "models/monsters/flyer/gibs/wing.md2", GIB_SKINNED },
		{ "models/monsters/flyer/gibs/head.md2", GIB_SKINNED | GIB_HEAD }
		});

	self->touch = nullptr;
}

// PMM - kamikaze code .. blow up if blocked
MONSTERINFO_BLOCKED(flyer_blocked) (edict_t* self, float dist) -> bool
{
	// kamikaze = 100, normal = 50
	if (self->mass == 100)
	{
		flyer_kamikaze_check(self);

		// if the above didn't blow us up (i.e. I got blocked by the player)
		if (self->inuse)
			T_Damage(self, self, self, vec3_origin, self->s.origin, vec3_origin, 9999, 100, DAMAGE_NONE, MOD_UNKNOWN);

		return true;
	}

	return false;
}

TOUCH(kamikaze_touch) (edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	T_Damage(ent, ent, ent, ent->velocity.normalized(), ent->s.origin, ent->velocity.normalized(), 9999, 100, DAMAGE_NONE, MOD_UNKNOWN);
}

TOUCH(flyer_touch) (edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if ((other->monsterinfo.aiflags & AI_ALTERNATE_FLY) && (other->flags & FL_FLY) &&
		(ent->monsterinfo.duck_wait_time < level.time))
	{
		ent->monsterinfo.duck_wait_time = level.time + 1_sec;
		ent->monsterinfo.fly_thrusters = false;

		const	vec3_t dir = (ent->s.origin - other->s.origin).normalized();
		ent->velocity = dir * 500.f;

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_SPLASH);
		gi.WriteByte(32);
		gi.WritePosition(tr.endpos);
		gi.WriteDir(dir);
		gi.WriteByte(SPLASH_SPARKS);
		gi.multicast(tr.endpos, MULTICAST_PVS, false);
	}
}

/*QUAKED monster_flyer (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 */
void SP_monster_flyer(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

    if (self->monsterinfo.monster_type_id == MONSTER_TYPE_UNKNOWN) { // Check if it hasn't been set yet
        self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::FLYER);
    }	if (g_horde->integer && current_wave_level <= 18) {
		const	float randomsearch = frandom(); // Generar un número aleatorio entre 0 y 1

		if (randomsearch < 0.32f)
			gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_NORM, 0);
	}

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	sound_sight.assign("flyer/flysght1.wav");
	sound_idle.assign("flyer/flysrch1.wav");
	sound_pain1.assign("flyer/flypain1.wav");
	sound_pain2.assign("flyer/flypain2.wav");
	sound_slash.assign("flyer/flyatck2.wav");
	sound_sproing.assign("flyer/flyatck1.wav");
	sound_die.assign("flyer/flydeth1.wav");

	gi.soundindex("flyer/flyatck3.wav");

	self->s.modelindex = gi.modelindex("models/monsters/flyer/tris.md2");

	gi.modelindex("models/monsters/flyer/gibs/base.md2");
	gi.modelindex("models/monsters/flyer/gibs/wing.md2");
	gi.modelindex("models/monsters/flyer/gibs/gun.md2");
	gi.modelindex("models/monsters/flyer/gibs/head.md2");

	self->mins = { -16, -16, -24 };
	// PMM - shortened to 16 from 32
	self->maxs = { 16, 16, 16 };
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	self->viewheight = 12;

	self->monsterinfo.engine_sound = gi.soundindex("flyer/flyidle1.wav");

	// Power armor
	if (!st.was_key_specified("power_armor_type") && M_FLYER_POWER_ARMOR_TYPE != IT_NULL) {
		self->monsterinfo.power_armor_type = static_cast<item_id_t>(M_FLYER_POWER_ARMOR_TYPE);
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = M_FLYER_ADDON_POWER_ARMOR(self);
	}

	// Armor
	if (!st.was_key_specified("armor_type") && M_FLYER_INITIAL_ARMOR > 0) {
		self->monsterinfo.armor_type = IT_ARMOR_COMBAT;
		if (!st.was_key_specified("armor_power"))
			self->monsterinfo.armor_power = M_FLYER_ADDON_ARMOR(self);
	}

	if (g_horde && g_horde->integer && current_wave_level > 0) {
		self->health = M_FLYER_ADDON_HEALTH(self);
	} else {
		self->health = static_cast<int>(M_FLYER_INITIAL_HEALTH * st.health_multiplier);
	}
	self->mass = 50;

	self->pain = flyer_pain;
	self->die = flyer_die;

	self->monsterinfo.stand = flyer_stand;
	self->monsterinfo.walk = flyer_walk;
	self->monsterinfo.run = flyer_run;
	self->monsterinfo.attack = flyer_attack;
	self->monsterinfo.melee = flyer_melee;
	self->monsterinfo.sight = flyer_sight;
	self->monsterinfo.idle = flyer_idle;
	self->monsterinfo.blocked = flyer_blocked;
	self->monsterinfo.setskin = flyer_setskin;

	gi.linkentity(self);

	M_SetAnimation(self, &flyer_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	if (self->s.effects & EF_ROCKET)
	{
		// PMM - normal flyer has mass of 50
		self->mass = 100;
		self->yaw_speed = 5;
		self->touch = kamikaze_touch;
	}
	else
	{
		self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
		self->monsterinfo.fly_buzzard = true;
		flyer_set_fly_parameters(self, false);
		self->touch = flyer_touch;
	}

	flymonster_start(self);

	ApplyMonsterBonusFlags(self);
}

// PMM - suicide fliers
void SP_monster_kamikaze(edict_t* self)
{
	const spawn_temp_t &st = ED_GetSpawnTemp();

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	self->s.effects |= EF_ROCKET;
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::KAMIKAZE);	SP_monster_flyer(self);

	// Power armor
	if (!st.was_key_specified("power_armor_type") && M_FLYER_POWER_ARMOR_TYPE != IT_NULL) {
		self->monsterinfo.power_armor_type = static_cast<item_id_t>(M_FLYER_POWER_ARMOR_TYPE);
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = M_FLYER_ADDON_POWER_ARMOR(self);
	}

	// Armor
	if (!st.was_key_specified("armor_type") && M_FLYER_INITIAL_ARMOR > 0) {
		self->monsterinfo.armor_type = IT_ARMOR_COMBAT;
		if (!st.was_key_specified("armor_power"))
			self->monsterinfo.armor_power = M_FLYER_ADDON_ARMOR(self);
	}

	if (g_horde && g_horde->integer && current_wave_level > 0) {
		self->health = M_FLYER_ADDON_HEALTH(self);
	} else {
		self->health = static_cast<int>(M_FLYER_INITIAL_HEALTH * st.health_multiplier);
	}

	ApplyMonsterBonusFlags(self);

}
