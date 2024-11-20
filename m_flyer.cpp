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
void flyer_setstart(edict_t* self);

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
	vec3_t dir;

	if (self->monsterinfo.commander && self->monsterinfo.commander->inuse &&
		!strcmp(self->monsterinfo.commander->classname, "monster_carrier"))
		self->monsterinfo.commander->monsterinfo.monster_slots++;

	if (self->enemy)
	{
		dir = self->enemy->s.origin - self->s.origin;
		T_Damage(self->enemy, self, self, dir, self->s.origin, vec3_origin, (int)50, (int)50, DAMAGE_RADIUS, MOD_UNKNOWN);
	}

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

	edict_t* ent2 = self->enemy; // Inicializar ent2

	if ((!self->enemy) || (!self->enemy->inuse) || !visible(self, ent2))
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

void flyer_checkstrafe(edict_t* self)
{
	// Validate enemy exists and is visible 
	if (!self->enemy || !visible(self, self->enemy))
		return;

	const float range = range_to(self, self->enemy);
	if (range <= RANGE_MID)
	{
		vec3_t right;
		AngleVectors(self->s.angles, nullptr, right, nullptr);

		// Validate the direction vector
		if (!is_valid_vector(right)) {
			// Handle invalid vector case
			return;
		}

		// Ensure right vector is normalized
		right = safe_normalized(right);

		float strafe_chance = 1.0f;
		if (self->enemy->client && (self->enemy->client->buttons & BUTTON_ATTACK))
			strafe_chance += 0.2f;
		if (self->health < self->max_health * 0.65f)
			strafe_chance += 0.4f;

		if (frandom() < strafe_chance)
		{
			self->monsterinfo.lefty = frandom() < 0.5;
			const float strafe_speed = 300 + (frandom() * 200);

			// Apply strafe movement using validated vector
			self->velocity = self->velocity + (right * (self->monsterinfo.lefty ? -strafe_speed : strafe_speed));
			self->monsterinfo.pausetime = level.time + random_time(0.75_sec, 1.3_sec);
		}
	}
}

void flyer_rocket(edict_t* self)
{
	vec3_t	forward;
	vec3_t	start, end, dir;
	float	dist, chance;
	trace_t trace;
	constexpr int rocketSpeed = 850;

	if (!self->enemy || !self->enemy->inuse)
		return;

	if (self->monsterinfo.aiflags & AI_LOST_SIGHT)
		end = self->monsterinfo.blind_fire_target;
	else
		end = self->enemy->s.origin;
	dir = end - self->s.origin;
	dir.normalize();
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	chance = dir.dot(forward);
	if (chance < 0.98f)
		return;

	chance = frandom();

	if (visible(self, self->enemy))
	{
		start = self->s.origin;

		// aim for the head.
		if (!(self->monsterinfo.aiflags & AI_LOST_SIGHT))
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
		if (!(self->monsterinfo.aiflags & AI_LOST_SIGHT))
		{

			PredictAim(self, self->enemy, start, (float)rocketSpeed, true, (frandom(3.f - skill->integer) / 3.f) - frandom(0.05f * (3.f - skill->integer)), &dir, nullptr);
		}

		dir.normalize();
		trace = gi.traceline(start, end, self, MASK_PROJECTILE);
		if (trace.ent == self->enemy || trace.ent == world)
		{
			if (dist * trace.fraction > 72)
				monster_fire_rocket(self, start, dir, 35, rocketSpeed, MZ2_TURRET_ROCKET);
		}
	}
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
	{ ai_charge }
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
	vec3_t	  start;
	vec3_t	  forward, right;
	vec3_t	  end;
	vec3_t	  dir;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	if (frandom() < 0.3f)
		PredictAim(self, self->enemy, start, 1000, true, 0, &dir, &end);
	else
		end = self->enemy->s.origin;
	end[2] += self->enemy->viewheight;
	dir = end - start;
	dir.normalize();

	monster_fire_blaster2(self, start, dir, 2, 1000, flash_number, (self->s.frame % 4) ? EF_NONE : EF_HYPERBLASTER);
}

void flyer_fireleft(edict_t* self)
{
	flyer_fire(self, MZ2_FLYER_BLASTER_1);
}

void flyer_fireright(edict_t* self)
{
	flyer_fire(self, MZ2_FLYER_BLASTER_2);
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
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
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
	{ ai_charge, 10 },
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
	vec3_t aim = { MELEE_DISTANCE, self->mins[0], 0 };

	// Verificar si self->enemy está correctamente inicializado
	if (self->enemy) {
		if (!fire_hit(self, aim, 3, 0))
			self->monsterinfo.melee_debounce_time = level.time + 1.5_sec;
		gi.sound(self, CHAN_WEAPON, sound_slash, 1, ATTN_NORM, 0);
	}
	else {
		//char buffer[256];
		//std::snprintf(buffer, sizeof(buffer), "flyer_slash_left: Error: enemy not properly initialized\n");
		//gi.Com_Print(buffer);

		// Manejar el caso donde self->enemy no está inicializado
		self->monsterinfo.melee_debounce_time = level.time + 1.5_sec; // Ajustar según sea necesario
	}
}


void flyer_slash_right(edict_t* self)
{
	vec3_t aim = { MELEE_DISTANCE, self->maxs[0], 0 };

	// Verificar si self->enemy está correctamente inicializado
	if (self->enemy) {
		if (!fire_hit(self, aim, 3, 0))
			self->monsterinfo.melee_debounce_time = level.time + 1.5_sec;
		gi.sound(self, CHAN_WEAPON, sound_slash, 1, ATTN_NORM, 0);
	}
	else {
		//char buffer[256];
		//std::snprintf(buffer, sizeof(buffer), "flyer_slash_right: Error: enemy not properly initialized\n");
		//gi.Com_Print(buffer);

		// Manejar el caso donde self->enemy no está inicializado
		self->monsterinfo.melee_debounce_time = level.time + 1.5_sec; // Ajustar según sea necesario
	}
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

PRETHINK(flyer_left_laser_update) (edict_t* laser) -> void
{
	edict_t* self = laser->owner;
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
		float spread = sinf(level.time.seconds() * 15.0f) * 0.2f; // Oscilación suave
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
		float spread = sinf(level.time.seconds() * 15.0f + PIf) * 0.02f; // Desfasado del izquierdo
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
	if (!self->enemy)
		return;

	// Sonido de láser
	gi.sound(self, CHAN_WEAPON, sound_laser, 1, ATTN_NORM, 0);

	// Disparar ambos láseres
	monster_fire_dabeam(self, 8, false, flyer_left_laser_update);
	monster_fire_dabeam(self, 8, true, flyer_right_laser_update);
}

void flyer_laser_off(edict_t* self)
{
	if (self->beam) {
		G_FreeEdict(self->beam);
		self->beam = nullptr;
	}
	if (self->beam2) {
		G_FreeEdict(self->beam2);
		self->beam2 = nullptr;
	}
}

void flyer_recharge(edict_t* self);

// Frames para el ataque láser
mframe_t flyer_frames_laser_right[] = {
	{ ai_charge, 0, nullptr },           // Preparación
	{ ai_charge, 0, nullptr },    // Inicio del láser
	{ ai_charge, 0, nullptr },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
};
MMOVE_T(flyer_move_laser_right) = { FRAME_attak201, FRAME_attak216, flyer_frames_laser_right, flyer_recharge };

mframe_t flyer_frames_laser_left[] = {
	{ ai_charge, 0, nullptr },           // Preparación
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	{ ai_charge, 0, flyer_laser_on },    // Inicio del láser
	//	{ ai_charge, 0, flyer_laser_off },   // Fin del láser
		{ ai_charge, 0, nullptr }            // Recuperación
};
MMOVE_T(flyer_move_laser_left) = { FRAME_bankl01, FRAME_bankl07, flyer_frames_laser_left, flyer_recharge };

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
MONSTERINFO_ATTACK(flyer_attack) (edict_t* self) -> void
{
	if (self->mass > 50)
	{
		flyer_run(self);
		return;
	}

	const float range = range_to(self, self->enemy);
	const float attack_chance = frandom();

	// Ataque láser a media distancia
	if (self && self->enemy && self->enemy->health <= 65 && range > 100 && range < 1400 && attack_chance < 0.75f) {
		M_SetAnimation(self, &flyer_move_laser_right);
		return;
	}

	if (self->enemy && visible(self, self->enemy) && range <= 225.f && frandom() > (range / 225.f) * 0.35f)
	{
		// fly-by slicing!
		self->monsterinfo.attack_state = AS_STRAIGHT;
		M_SetAnimation(self, &flyer_move_start_melee);
		flyer_set_fly_parameters(self, true);
	}
	else
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;

		if (first3waves)
		{
			frandom() > 0.2f ?
				M_SetAnimation(self, &flyer_move_attack2normal) :
				M_SetAnimation(self, &flyer_move_rollright);
		}
		else
		{
			if (frandom() > 0.4f)
			{
				M_SetAnimation(self, &flyer_move_attack2);
			}
			else
			{
				M_SetAnimation(self, &flyer_move_rollright);
			}
		}
	}


	// [Paril-KEX] for alternate fly mode, sometimes we'll pin us
	// down, kind of like a pseudo-stand ground
	if (!self->monsterinfo.fly_pinned && brandom() && self->enemy && visible(self, self->enemy))
	{
		self->monsterinfo.fly_pinned = true;
		self->monsterinfo.fly_position_time = max(self->monsterinfo.fly_position_time, self->monsterinfo.fly_position_time + 1.7_sec); // make sure there's enough time for attack2/3

		if (brandom())
			self->monsterinfo.fly_ideal_position = self->s.origin + (self->velocity * frandom()); // pin to our current position
		else
			self->monsterinfo.fly_ideal_position += self->enemy->s.origin; // make un-relative
	}

	// if we're currently pinned, fly_position_time will unpin us eventually
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
	if (range_to(self, self->enemy) <= RANGE_MELEE)
	{
		if (self->monsterinfo.melee_debounce_time <= level.time)
		{
			M_SetAnimation(self, &flyer_move_loop_melee);
			return;
		}
	}

	M_SetAnimation(self, &flyer_move_end_melee);
	flyer_set_fly_parameters(self, false);
}

PAIN(flyer_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	int n;

	//	pmm	 - kamikaze's don't feel pain
	if (self->mass != 50)
		return;
	// pmm

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;

	n = irandom(3);
	if (n == 0)
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else if (n == 1)
		gi.sound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	flyer_set_fly_parameters(self, false);

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
	OnEntityDeath(self);
	gi.sound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);

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

		vec3_t dir = (ent->s.origin - other->s.origin).normalized();
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

	if (g_horde->integer && current_wave_level <= 18) {
		float randomsearch = frandom(); // Generar un número aleatorio entre 0 y 1

		if (randomsearch < 0.32f)
			gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_NORM, 0);
		else
			nullptr;
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

	self->health = 50 * st.health_multiplier;
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
	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	self->s.effects |= EF_ROCKET;

	SP_monster_flyer(self);
	ApplyMonsterBonusFlags(self);

}
