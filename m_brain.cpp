// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

brain

==============================================================================
*/

#include "g_local.h"
#include "m_brain.h"
#include "shared.h"
#include "horde/g_horde_scaling.h"
#include "monster_constants.h"

static cached_soundindex sound_chest_open;
static cached_soundindex sound_tentacles_extend;
static cached_soundindex sound_tentacles_retract;
static cached_soundindex sound_death;
static cached_soundindex sound_idle1;
static cached_soundindex sound_idle2;
static cached_soundindex sound_idle3;
static cached_soundindex sound_pain1;
static cached_soundindex sound_pain2;
static cached_soundindex sound_sight;
static cached_soundindex sound_search;
static cached_soundindex sound_melee1;
static cached_soundindex sound_melee2;
static cached_soundindex sound_melee3;

MONSTERINFO_SIGHT(brain_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_SEARCH(brain_search) (edict_t* self) -> void
{
	gi.sound(self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
}

void brain_run(edict_t* self);
void brain_dead(edict_t* self);

constexpr spawnflags_t SPAWNFLAG_BRAIN_NO_LASERS = 8_spawnflag;

//
// STAND
//

mframe_t brain_frames_stand[] = {
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
MMOVE_T(brain_move_stand) = { FRAME_stand01, FRAME_stand30, brain_frames_stand, nullptr };

MONSTERINFO_STAND(brain_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &brain_move_stand);
}

//
// IDLE
//

mframe_t brain_frames_idle[] = {
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
MMOVE_T(brain_move_idle) = { FRAME_stand31, FRAME_stand60, brain_frames_idle, brain_stand };

MONSTERINFO_IDLE(brain_idle) (edict_t* self) -> void
{
	gi.sound(self, CHAN_AUTO, sound_idle3, 1, ATTN_IDLE, 0);
	M_SetAnimation(self, &brain_move_idle);
}

//
// WALK
//
mframe_t brain_frames_walk1[] = {
	{ ai_walk, 7 },
	{ ai_walk, 2 },
	{ ai_walk, 3 },
	{ ai_walk, 3, monster_footstep },
	{ ai_walk, 1 },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk, 9 },
	{ ai_walk, -4 },
	{ ai_walk, -1, monster_footstep },
	{ ai_walk, 2 }
};
MMOVE_T(brain_move_walk1) = { FRAME_walk101, FRAME_walk111, brain_frames_walk1, nullptr };

MONSTERINFO_WALK(brain_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &brain_move_walk1);
}

#if 0
mframe_t brain_frames_defense[] = {
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
MMOVE_T(brain_move_defense) = { FRAME_defens01, FRAME_defens08, brain_frames_defense, nullptr };
#endif

mframe_t brain_frames_pain3[] = {
	{ ai_move, -2 },
	{ ai_move, 2 },
	{ ai_move, 1 },
	{ ai_move, 3 },
	{ ai_move },
	{ ai_move, -4 }
};
MMOVE_T(brain_move_pain3) = { FRAME_pain301, FRAME_pain306, brain_frames_pain3, brain_run };

mframe_t brain_frames_pain2[] = {
	{ ai_move, -2 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 3 },
	{ ai_move, 1 },
	{ ai_move, -2 }
};
MMOVE_T(brain_move_pain2) = { FRAME_pain201, FRAME_pain208, brain_frames_pain2, brain_run };

mframe_t brain_frames_pain1[] = {
	{ ai_move, -6 },
	{ ai_move, -2 },
	{ ai_move, -6, monster_footstep },
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
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 1 },
	{ ai_move, 7 },
	{ ai_move },
	{ ai_move, 3, monster_footstep },
	{ ai_move, -1 }
};
MMOVE_T(brain_move_pain1) = { FRAME_pain101, FRAME_pain121, brain_frames_pain1, brain_run };

mframe_t brain_frames_duck[] = {
	{ ai_move },
	{ ai_move, -2, [](edict_t* self) { monster_duck_down(self); monster_footstep(self); } },
	{ ai_move, 17, monster_duck_hold },
	{ ai_move, -3 },
	{ ai_move, -1, monster_duck_up },
	{ ai_move, -5 },
	{ ai_move, -6 },
	{ ai_move, -6, monster_footstep }
};
MMOVE_T(brain_move_duck) = { FRAME_duck01, FRAME_duck08, brain_frames_duck, brain_run };

static void brain_shrink(edict_t* self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t brain_frames_death2[] = {
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move, 0, brain_shrink },
	{ ai_move, 9 },
	{ ai_move }
};
MMOVE_T(brain_move_death2) = { FRAME_death201, FRAME_death205, brain_frames_death2, brain_dead };

mframe_t brain_frames_death1[] = {
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move, -2 },
	{ ai_move, 9, [](edict_t* self) { brain_shrink(self); monster_footstep(self); } },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(brain_move_death1) = { FRAME_death101, FRAME_death118, brain_frames_death1, brain_dead };

//
// MELEE
//

void brain_swing_right(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_melee1, 1, ATTN_NORM, 0);
}

void brain_swing_left(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_melee2, 1, ATTN_NORM, 0);
}

void brain_hit_right(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		self->monsterinfo.melee_debounce_time = level.time + 3_sec;
		return;
	}

	int damage = M_GET_DMG_OR(self, MELEE, 17);

	vec3_t const aim = { MELEE_DISTANCE, self->maxs[0], 8 };
	if (fire_hit(self, aim, damage, 40))
		gi.sound(self, CHAN_WEAPON, sound_melee3, 1, ATTN_NORM, 0);
	else
		self->monsterinfo.melee_debounce_time = level.time + 3_sec;
}

void brain_hit_left(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		self->monsterinfo.melee_debounce_time = level.time + 0.2_sec;
		return;
	}

	int damage = M_GET_DMG_OR(self, MELEE, 17);

	vec3_t const aim = { MELEE_DISTANCE, self->mins[0], 8 };
	if (fire_hit(self, aim, damage, 40))
		gi.sound(self, CHAN_WEAPON, sound_melee3, 1, ATTN_NORM, 0);
	else
		self->monsterinfo.melee_debounce_time = level.time + 0.2_sec;
}

mframe_t brain_frames_attack1[] = {
	{ ai_charge, 0, monster_footstep },
	{ ai_charge, -3, brain_swing_right },
	{ ai_charge, -3, brain_swing_right },
	{ ai_charge, 8 },
	{ ai_charge, 5 },
	{ ai_charge },
	{ ai_charge, -7, brain_hit_right },
	{ ai_charge, -7, brain_hit_right },
	{ ai_charge },
	{ ai_charge, 6, brain_swing_left },
	{ ai_charge, 1 },
	{ ai_charge, 2, brain_hit_left },
	{ ai_charge, 2, brain_hit_left },
	{ ai_charge, 6 },
	{ ai_charge, -1 },
	{ ai_charge, -3 },
	{ ai_charge, 2 },
	{ ai_charge, -11, monster_footstep }
};
MMOVE_T(brain_move_attack1) = { FRAME_attak101, FRAME_attak118, brain_frames_attack1, brain_run };

void brain_chest_open(edict_t* self)
{
	self->count = 0;
	self->monsterinfo.power_armor_type = IT_NULL;
	gi.sound(self, CHAN_BODY, sound_chest_open, 1, ATTN_NORM, 0);
}

void brain_tentacle_attack(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		self->monsterinfo.melee_debounce_time = level.time + 0.3_sec;
		return;
	}

	int damage = M_GET_DMG_OR(self, MELEE, 17);

	vec3_t  const aim = { MELEE_DISTANCE, 0, 8 };
	if (fire_hit(self, aim, damage * 0.7, -600))
		self->count = 1;
	else
		self->monsterinfo.melee_debounce_time = level.time + 3_sec;
	gi.sound(self, CHAN_WEAPON, sound_tentacles_retract, 1, ATTN_NORM, 0);
}

void brain_chest_closed(edict_t* self)
{
	self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
	if (self->count)
	{
		self->count = 0;
		M_SetAnimation(self, &brain_move_attack1);
	}
}

mframe_t brain_frames_attack2[] = {
	{ ai_charge, 5 },
	{ ai_charge, -4 },
	{ ai_charge, -4 },
	{ ai_charge, 0, brain_chest_open },
	{ ai_charge, 13, brain_tentacle_attack },
	{ ai_charge, 13, brain_tentacle_attack },
	{ ai_charge, 13, brain_tentacle_attack },
	{ ai_charge },
	{ ai_charge, -4, brain_tentacle_attack },

	{ ai_charge },
	{ ai_charge, -9, brain_chest_closed },
	{ ai_charge },
	{ ai_charge, 4 },
	{ ai_charge, 3 },
	{ ai_charge, 2 },
	{ ai_charge, -3 },
	{ ai_charge, -6 }
};
MMOVE_T(brain_move_attack2) = { FRAME_attak201, FRAME_attak217, brain_frames_attack2, brain_run };

MONSTERINFO_MELEE(brain_melee) (edict_t* self) -> void
{
	if (frandom() <= 0.5f)
		M_SetAnimation(self, &brain_move_attack1);
	else
		M_SetAnimation(self, &brain_move_attack2);
}

// RAFAEL
bool G_EntExists(const edict_t* ent) {
	return (ent && ent->inuse && ent->takedamage && ent->solid != SOLID_NOT);
}

inline void G_EntMidPoint(const edict_t* ent, vec3_t& point)
{
	// Asignar el origen usando asignación directa de vec3_t
	point = ent->s.origin;

	// Calcular la altura media usando el bbox absoluto
	float const midheight = 0.5f * (ent->absmax[2] - ent->absmin[2]);

	// Ajustar la coordenada Z al punto medio
	point[2] = ent->absmin[2] + midheight;
}

void brain_tounge_attack(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Can't at a non-existent or dead target.
	}

	// Calculate enemy position and direction
	vec3_t start;
	G_EntMidPoint(self->enemy, start);
	vec3_t const dir = start - self->s.origin;
	float const range = dir.length();
	vec3_t const normalized_dir = dir.normalized();

	// Adjust yaw to face enemy
	self->ideal_yaw = vectoyaw(dir);
	M_ChangeYaw(self);

	// Check pull range
	const vec3_t end = self->s.origin + (normalized_dir * 512);
	const trace_t tr = gi.traceline(self->s.origin, end, self, MASK_SHOT);

	if (G_EntExists(tr.ent) && (tr.ent == self->enemy))
	{
		int damage = 0;
		int pull = 175; // Base pull force

		// Double pull force if on ground
		if (self->enemy->groundentity)
			pull *= 2;

		// Apply damage only if close enough
		if (range <= 64) {
			damage = 5;

			// Add initial health steal on contact
			int steal_amount = irandom(2, 4);
			if (M_DamageModifier(self)) {
				steal_amount *= M_DamageModifier(self);
			}

			// Steal health and limit to max_health
			self->health = min(self->max_health, self->health + steal_amount);
			if (self->monsterinfo.setskin) {
				self->monsterinfo.setskin(self);
			}

			// Reset timestamp to allow immediate stealing in continue function
			self->timestamp = 0_ms;

			// Visual feedback
			//gi.sound(self, CHAN_WEAPON, sound_tentacles_retract, 1, ATTN_NORM, 0);
		}

		// Apply damage and pull
		T_Damage(self->enemy, self, self, dir, self->enemy->s.origin, vec3_origin,
			damage, -pull, DAMAGE_RADIUS, MOD_BRAINTENTACLE);
	}
}

mframe_t brain_frames_run[] = {
	{ ai_run, 12 },
	{ ai_run, 5 },
	{ ai_run, 6 },
	{ ai_run, 6 },
	{ ai_run, 4 },
	{ ai_run,  1},
	{ ai_run,  1},
	{ ai_run, 13 },
	{ ai_run, -4 },
	{ ai_run, -1 },
	{ ai_run, 4 }
};
MMOVE_T(brain_move_run) = { FRAME_walk101, FRAME_walk111, brain_frames_run, nullptr };

void brain_tounge_attack_continue(edict_t* self);
mframe_t brain_frames_continue[] = {
	{ ai_charge, 0, brain_tounge_attack_continue },
	{ ai_charge, 0, brain_tounge_attack_continue },
	{ ai_charge, 0, brain_tounge_attack_continue },
	{ ai_charge, 0, brain_tounge_attack_continue },
	{ ai_charge, 0, brain_tounge_attack_continue },
};
MMOVE_T(brain_move_continue) = { FRAME_attak206, FRAME_attak210, brain_frames_continue, brain_tounge_attack };

void brain_tounge_attack_continue(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		M_SetAnimation(self, &brain_move_run);
		return;
	}

	// Get enemy position and calculate distance
	vec3_t start;
	G_EntMidPoint(self->enemy, start);
	vec3_t const diff = start - self->s.origin;
	float const dist = diff.length();

	// Release if out of range
	if (dist > 512) {
		M_SetAnimation(self, &brain_move_run);
		self->monsterinfo.attack_finished = level.time + 1_hz;
		return;
	}

	// Calculate pull and damage values
	const vec3_t dir = diff.normalized();
	int pull = 70;
	int damage = 3; // Lower base damage since we're applying it every frame

	// Increase pull if on ground
	if (self->enemy->groundentity)
		pull *= 2;

	// Apply effects if in close range
	if (dist <= 64) {
		// Apply damage every frame
		T_Damage(self->enemy, self, self, dir, self->enemy->s.origin,
			vec3_origin, damage, -pull, DAMAGE_RADIUS, MOD_BRAINTENTACLE);

		// Steal health more frequently (4 times per second)
		if (level.time >= self->timestamp) {
			// Randomized health steal
			int steal_amount = irandom(3, 6); // Increased to make it more noticeable
			if (M_DamageModifier(self)) {
				steal_amount *= M_DamageModifier(self);
			}

			// Apply health steal
			self->health = min(self->max_health, self->health + steal_amount);
			if (self->monsterinfo.setskin) {
				self->monsterinfo.setskin(self);
			}

			// Visual feedback on each steal
			gi.sound(self, CHAN_WEAPON, sound_tentacles_retract, 1, ATTN_NORM, 0);

			// Set next steal time (4 times per second)
			self->timestamp = level.time + 25_hz; // Much more frequent stealing
		}

		// Keep attack animation going
		self->monsterinfo.nextframe = FRAME_attak206;
	}

	// Set next update time
	self->monsterinfo.attack_finished = level.time + 1_hz;
	self->nextthink = level.time + FRAME_TIME_MS;
}

// Brian right eye center
constexpr vec3_t brain_reye[] = {
	{ 0.746700f, 0.238370f, 34.167690f },
	{ -1.076390f, 0.238370f, 33.386372f },
	{ -1.335500f, 5.334300f, 32.177170f },
	{ -0.175360f, 8.846370f, 30.635479f },
	{ -2.757590f, 7.804610f, 30.150860f },
	{ -5.575090f, 5.152840f, 30.056160f },
	{ -7.017550f, 3.262470f, 30.552521f },
	{ -7.915740f, 0.638800f, 33.176189f },
	{ -3.915390f, 8.285730f, 33.976349f },
	{ -0.913540f, 10.933030f, 34.141811f },
	{ -0.369900f, 8.923900f, 34.189079f }
};

// Brain left eye center
constexpr vec3_t brain_leye[] = {
	{ -3.364710f, 0.327750f, 33.938381f },
	{ -5.140450f, 0.493480f, 32.659851f },
	{ -5.341980f, 5.646980f, 31.277901f },
	{ -4.134480f, 9.277440f, 29.925621f },
	{ -6.598340f, 6.815090f, 29.322620f },
	{ -8.610840f, 2.529650f, 29.251591f },
	{ -9.231360f, 0.093280f, 29.747959f },
	{ -11.004110f, 1.936930f, 32.395260f },
	{ -7.878310f, 7.648190f, 33.148151f },
	{ -4.947370f, 11.430050f, 33.313610f },
	{ -4.332820f, 9.444570f, 33.526340f }
};

PRETHINK(brain_right_eye_laser_update) (edict_t* laser) -> void
{
	edict_t* self = laser->owner;

	if (!M_HasValidTarget(self))
	{
		G_FreeEdict(laser);
		return;
	}
	
	vec3_t start, forward, right, up, dir;

	// check for max distance
	AngleVectors(self->s.angles, forward, right, up);

	// FIXED: Ensure frame index is within valid bounds
	int frame_index = self->s.frame - FRAME_walk101;
	if (frame_index < 0 || frame_index >= 11) // brain_reye has 11 elements (0-10)
	{
		frame_index = 0; // Use default position if frame is out of range
	}

	// dis is my right eye
	start = self->s.origin + (right * brain_reye[frame_index].x);
	start += forward * brain_reye[frame_index].y;
	start += up * brain_reye[frame_index].z;

	PredictAim(self, self->enemy, start, 0, false, frandom(0.1f, 0.2f), &dir, nullptr);

	laser->s.origin = start;
	laser->movedir = dir;
	gi.linkentity(laser);
}

PRETHINK(brain_left_eye_laser_update) (edict_t* laser) -> void
{
	edict_t* self = laser->owner;

	if (!M_HasValidTarget(self))
	{
		G_FreeEdict(laser);
		return;
	}

	vec3_t start, forward, right, up, dir;

	// check for max distance
	AngleVectors(self->s.angles, forward, right, up);

	// FIXED: Ensure frame index is within valid bounds
	int frame_index = self->s.frame - FRAME_walk101;
	if (frame_index < 0 || frame_index >= 11) // brain_leye has 11 elements (0-10)
	{
		frame_index = 0; // Use default position if frame is out of range
	}

	// dis is my left eye
	start = self->s.origin + (right * brain_leye[frame_index].x);
	start += forward * brain_leye[frame_index].y;
	start += up * brain_leye[frame_index].z;

	PredictAim(self, self->enemy, start, 0, false, frandom(0.1f, 0.2f), &dir, nullptr);

	laser->s.origin = start;
	laser->movedir = dir;
	gi.linkentity(laser);
	dabeam_update(laser, false);
}

void brain_laserbeam(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}
	// dis is my right eye
	monster_fire_dabeam(self, 1, false, brain_right_eye_laser_update);

	// dis is me left eye
	monster_fire_dabeam(self, 1, true, brain_left_eye_laser_update);
}

// Función auxiliar para validar frames
bool IsValidFrame(const edict_t* self, int frame)
{
	if (!self || !self->monsterinfo.active_move.pointer())
		return false;

	const mmove_t* move = self->monsterinfo.active_move.pointer();
	return (frame >= move->firstframe && frame <= move->lastframe);
}

void brain_laserbeam_reattack(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	if (frandom() < 0.5f && visible(self, self->enemy) && self->enemy->health > 0) {
		// Verificar si FRAME_walk101 es válido para el movimiento actual
		if (IsValidFrame(self, FRAME_walk101)) {
			self->s.frame = FRAME_walk101;
		}
		else {
			// Si el frame no es válido, cambiar a una animación apropiada
			M_SetAnimation(self, &brain_move_run);
		}
	}
}

mframe_t brain_frames_attack3[] = {
	{ ai_charge, 5 },
	{ ai_charge, -4 },
	{ ai_charge, -4 },
	{ ai_charge, -3 },
	{ ai_charge, 0, brain_chest_open },
	{ ai_charge, 0, brain_tounge_attack },
	{ ai_charge, 0, brain_tounge_attack },
	{ ai_charge, 0, brain_tounge_attack },
	{ ai_charge, 0, brain_tounge_attack },
	{ ai_charge, 0, brain_tounge_attack_continue },
	{ ai_charge, -9, brain_chest_closed },
	//{ ai_charge },
	//{ ai_charge },
	//{ ai_charge, 4 },
	//{ ai_charge, 3 },
	//{ ai_charge, 2 },
	//{ ai_charge, -3 },
	//{ ai_charge, -6 }
};
MMOVE_T(brain_move_attack3) = { FRAME_attak201, FRAME_attak211, brain_frames_attack3, brain_tounge_attack };


mframe_t brain_frames_attack4[] = {
	{ ai_charge, 9, brain_laserbeam },
	{ ai_charge, 2, brain_laserbeam },
	{ ai_charge, 3, brain_laserbeam },
	{ ai_charge, 3, brain_laserbeam },
	{ ai_charge, 1, brain_laserbeam },
	{ ai_charge, 0, brain_laserbeam },
	{ ai_charge, 0, brain_laserbeam },
	{ ai_charge, 10, brain_laserbeam },
	{ ai_charge, -4, brain_laserbeam },
	{ ai_charge, -1, brain_laserbeam },
	{ ai_charge, 2, brain_laserbeam_reattack }
};
MMOVE_T(brain_move_attack4) = { FRAME_walk101, FRAME_walk111, brain_frames_attack4, brain_run };

void brain_jump2_now(edict_t* self);
void brain_jump_wait_land(edict_t* self);
void brain_jump_attack(edict_t* self);
void brain_jump_wait_land_attack(edict_t* self);

mframe_t brain_frames_jumpattack[] = {
	{ ai_move },
	{ ai_move, 0, brain_jump_attack },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, brain_jump_wait_land_attack },
	{ ai_move }
};
MMOVE_T(brain_move_jumpattack) = { FRAME_duck01, FRAME_duck08, brain_frames_jumpattack, brain_run };

// RAFAEL
MONSTERINFO_ATTACK(brain_attack) (edict_t* self) -> void
{
	if (!M_HasValidTarget(self))
	{
		return; // Can't at a non-existent or dead target.
	}

	const float r = range_to(self, self->enemy);
	constexpr float closeEnought = 80.0f;
	constexpr float JUMP_MIN_DISTANCE = MELEE_DISTANCE * 2;
	constexpr float JUMP_MAX_DISTANCE = MELEE_DISTANCE * 6;

	// Asegurarse de que la animación actual es válida
	if (!self->monsterinfo.active_move.pointer()) {
		M_SetAnimation(self, &brain_move_run);
		return;
	}

	// Contra objetivos especiales siempre usar láser
	if (horde::IsRangedOnlyTarget(self->enemy))
	{
		if (!self->spawnflags.has(SPAWNFLAG_BRAIN_NO_LASERS))
			M_SetAnimation(self, &brain_move_attack4);
		return;
	}

	// Verificar línea de visión y altura relativa
	bool  const has_los = visible(self, self->enemy);
	float const height_diff = fabs(self->s.origin[2] - self->enemy->s.origin[2]);

	// Si estamos muy cerca, priorizar el pull/melee
	if (r <= closeEnought && has_los)
	{
		// Intentar hacer pull directamente
		M_SetAnimation(self, &brain_move_attack3);
		return;
	}

	// Para rango medio/cercano, mezclar ataques
	if (r <= JUMP_MIN_DISTANCE)
	{
		float const attack_chance = frandom();

		if (attack_chance < 0.4f && !self->spawnflags.has(SPAWNFLAG_BRAIN_NO_LASERS))
			M_SetAnimation(self, &brain_move_attack4); // Laser
		else if (attack_chance < 0.7f)
			M_SetAnimation(self, &brain_move_attack3); // Pull
		else
			M_SetAnimation(self, &brain_move_jumpattack); // Salto corto

		return;
	}

	// Para rango medio/largo, evaluar salto
	if (r <= JUMP_MAX_DISTANCE && has_los && height_diff < 120.0f)
	{
		// Calcular punto medio entre brain y enemigo
		vec3_t const midpoint = (self->s.origin + self->enemy->s.origin) * 0.5f;

		// Verificar si hay espacio para saltar
		trace_t const tr = gi.trace(self->s.origin, self->mins, self->maxs,
			midpoint, self, MASK_MONSTERSOLID);

		if (tr.fraction >= 0.8f) // Si hay suficiente espacio libre
		{
			M_SetAnimation(self, &brain_move_jumpattack);
			return;
		}
	}

	// Rango largo o sin línea de visión clara, usar láser si está disponible
	if (!self->spawnflags.has(SPAWNFLAG_BRAIN_NO_LASERS))
	{
		M_SetAnimation(self, &brain_move_attack4);
		return;
	}

	// Si nada más es viable, intentar acercarse
	M_SetAnimation(self, &brain_move_run);
}

// RAFAEL

//
// RUN
//


MONSTERINFO_RUN(brain_run) (edict_t* self) -> void
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &brain_move_stand);
	else
		M_SetAnimation(self, &brain_move_run);
}

PAIN(brain_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	float r;

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;

	r = frandom();

	if (r < 0.33f)
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else if (r < 0.66f)
		gi.sound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	if (r < 0.33f)
		M_SetAnimation(self, &brain_move_pain1);
	else if (r < 0.66f)
		M_SetAnimation(self, &brain_move_pain2);
	else
		M_SetAnimation(self, &brain_move_pain3);

	// PMM - clear duck flag
	if (self->monsterinfo.aiflags & AI_DUCKED)
		monster_duck_up(self);
}

MONSTERINFO_SETSKIN(brain_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

void brain_dead(edict_t* self)
{
	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, -8 };
	monster_dead(self);
}

DIE(brain_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	self->s.effects = EF_NONE;
	self->monsterinfo.power_armor_type = IT_NULL;

	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		if (self->beam)
		{
			G_FreeEdict(self->beam);
			self->beam = nullptr;
		}
		if (self->beam2)
		{
			G_FreeEdict(self->beam2);
			self->beam2 = nullptr;
		}

		ThrowGibs(self, damage, {
			{ 1, "models/objects/gibs/bone/tris.md2" },
			{ 2, "models/objects/gibs/sm_meat/tris.md2" },
			{ 2, "models/monsters/brain/gibs/arm.md2", GIB_SKINNED | GIB_UPRIGHT },
			{ "models/monsters/brain/gibs/boot.md2", GIB_SKINNED | GIB_UPRIGHT },
			{ "models/monsters/brain/gibs/pelvis.md2", GIB_SKINNED },
			{ "models/monsters/brain/gibs/chest.md2", GIB_SKINNED },
			{ 2, "models/monsters/brain/gibs/door.md2", GIB_SKINNED | GIB_UPRIGHT },
			{ "models/monsters/brain/gibs/head.md2", GIB_SKINNED | GIB_HEAD }
			});
		self->deadflag = true;
		return;
	}

	if (self->deadflag)
		return;

	// regular death
	gi.sound(self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
	self->deadflag = true;
	self->takedamage = true;
	if (frandom() <= 0.5f)
		M_SetAnimation(self, &brain_move_death1);
	else
		M_SetAnimation(self, &brain_move_death2);
}

MONSTERINFO_DUCK(brain_duck) (edict_t* self, gtime_t eta) -> bool
{
	M_SetAnimation(self, &brain_move_duck);

	return true;
}



void brain_jump_now(edict_t* self)
{
	//	gi.Com_PrintFmt("PRINT: brain_jump_now called\n");
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 100);
	self->velocity += (up * 300);

	// brain jumps play the duck frames (FRAME_duck01-08), so carry the shorter duck bbox
	// while airborne to clear low obstacles. Restored on landing in brain_jump_wait_land.
	monster_duck_down(self);
}

void brain_jump2_now(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 150);
	self->velocity += (up * 400);

	monster_duck_down(self); // shorter duck bbox while airborne; restored in brain_jump_wait_land
}

void brain_jump_attack(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Can't at a non-existent or dead target.
	}

	// Calculate direction and distance to enemy using vec3_t operators
	const vec3_t dir = self->enemy->s.origin - self->s.origin;
	const float distance = dir.length();  // Usar length() en vez de VectorLength

	// VORTEX-STYLE: Calculate height difference for better verticality
	const float height_diff = self->enemy->s.origin[2] - self->s.origin[2];

	// Base upward velocity (increased from 125 to 300 to match vortex)
	float up_velocity = 300.0f;

	// Adjust upward velocity based on enemy height (vortex-style verticality)
	if (height_diff > 32.0f) {
		// Enemy is higher - add more upward boost
		up_velocity += height_diff * 0.8f;
	}
	else if (height_diff < -32.0f) {
		// Enemy is lower - reduce upward velocity but keep minimum for arc
		up_velocity += height_diff * 0.4f;
	}

	// Clamp to reasonable range (200-650)
	up_velocity = clamp(up_velocity, 200.0f, 650.0f);

	// Calculate forward speed based on distance
	const float fwd_speed = distance * 2.0f;

	// Predict aim
	vec3_t aim_dir;
	PredictAim(self, self->enemy, self->s.origin, fwd_speed, false, 0.f, &aim_dir, nullptr);

	// Set angles based on aim direction
	self->s.angles[YAW] = vectoyaw(aim_dir);

	// Calculate velocity using angle_vectors_t struct
	auto const vectors = AngleVectors(self->s.angles);  // Obtiene forward, right, up de una vez

	// Construir el vector de velocidad usando operadores de vec3_t (with improved verticality)
	self->velocity = vectors.forward * fwd_speed + vectors.up * up_velocity;

	// Set other properties
	self->groundentity = nullptr;
	// brain_move_jumpattack plays the duck frames (FRAME_duck01-08): shrink to the duck bbox
	// (sets AI_DUCKED + maxs[2]) so the crouch box matches the pose. Restored on landing in
	// brain_jump_wait_land_attack.
	monster_duck_down(self);
	self->monsterinfo.attack_finished = level.time + 2.0_sec;

	// Play jump sound
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

void brain_jump_wait_land_attack(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		M_SetAnimation(self, &brain_move_run); // Fallback if target is lost
		return;
	}

	if (self->groundentity == nullptr)
	{
		self->monsterinfo.nextframe = self->s.frame;
		if (monster_jump_finished(self))
			self->monsterinfo.nextframe = self->s.frame + 1;
	}
	else
	{
		self->monsterinfo.nextframe = self->s.frame + 1;
		// Restore full bbox only after landing (mirrors brain_jump_wait_land). Never re-expand
		// while airborne — doing so near a ceiling embeds the box and gets the brain stuck.
		// monster_duck_up is headroom-checked, so this is a no-op if there's no clearance yet.
		monster_duck_up(self);
	}

	// Determine the actual target. If the enemy is a laser beam,
	// the real target for this animation check is its owner (the emitter).
	edict_t* target = self->enemy;
	if (horde::IsSpecialType(target, horde::SpecialEntityTypeID::LASER_BEAM))
	{
		target = target->owner;
	}

	// Check if the resolved target is a deployable that warrants a special attack animation.
	if (target && (horde::IsSpecialType(target, horde::SpecialEntityTypeID::TESLA_MINE) ||
		horde::IsSpecialType(target, horde::SpecialEntityTypeID::SENTRY_GUN) ||
		horde::IsSpecialType(target, horde::SpecialEntityTypeID::FOOD_CUBE_TRAP) ||
		horde::IsSpecialType(target, horde::SpecialEntityTypeID::LASER_EMITTER)))
	{
		M_SetAnimation(self, &brain_move_attack4);
	}
	else if (visible(self, self->enemy))
	{
		M_SetAnimation(self, &brain_move_attack3);
	}
	else // Target is valid but not visible, just run
	{
		M_SetAnimation(self, &brain_move_run);
	}
}

void brain_jump_wait_land(edict_t* self)
{
	if (self->groundentity == nullptr)
	{
		self->monsterinfo.nextframe = self->s.frame;

		if (monster_jump_finished(self))
			self->monsterinfo.nextframe = self->s.frame + 1;
	}
	else
	{
		self->monsterinfo.nextframe = self->s.frame + 1;
		monster_duck_up(self); // Restore full bbox after the crouch jump (no-op if not ducked)
	}
}

mframe_t brain_frames_jump[] = {
	{ ai_move },
	{ ai_move, 0, brain_jump_now },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, brain_jump_wait_land },
	{ ai_move },
	{ ai_move },
	{ ai_move }

};
MMOVE_T(brain_move_jump) = { FRAME_duck01, FRAME_duck08, brain_frames_jump, brain_run };

mframe_t brain_frames_jump2[] = {
	{ ai_move },
	{ ai_move, 0, brain_jump2_now },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, brain_jump_wait_land },
	{ ai_move },
	{ ai_move },
	{ ai_move }

};
MMOVE_T(brain_move_jump2) = { FRAME_duck01, FRAME_duck08, brain_frames_jump2, brain_run };
//===========
// PGM
void brain_jump(edict_t* self, blocked_jump_result_t result)
{
	if (!self->enemy)
		return;

	monster_done_dodge(self);

	if (result == blocked_jump_result_t::JUMP_JUMP_UP && !visible(self, self->enemy))
		M_SetAnimation(self, &brain_move_jump2);
	else if (!visible(self, self->enemy))
		M_SetAnimation(self, &brain_move_jump);
	else
		M_SetAnimation(self, &brain_move_jumpattack);
}

MONSTERINFO_BLOCKED(brain_blocked) (edict_t* self, float dist) -> bool
{
	if (self->monsterinfo.can_jump)
	{
		if (auto const result = blocked_checkjump(self, dist); result != blocked_jump_result_t::NO_JUMP)
		{
			brain_jump(self, result);
			return true;
		}
	}

	if (blocked_checkplat(self, dist))
		return true;

	return false;
}

/*QUAKED monster_brain (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 */
void SP_monster_brain(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::BRAIN);

	if (g_horde->integer) {
		{
			if (brandom())
				gi.sound(self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
		}
	}

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	sound_chest_open.assign("brain/brnatck1.wav");
	sound_tentacles_extend.assign("brain/brnatck2.wav");
	sound_tentacles_retract.assign("brain/brnatck3.wav");
	sound_death.assign("brain/brndeth1.wav");
	sound_idle1.assign("brain/brnidle1.wav");
	sound_idle2.assign("brain/brnidle2.wav");
	sound_idle3.assign("brain/brnlens1.wav");
	sound_pain1.assign("brain/brnpain1.wav");
	sound_pain2.assign("brain/brnpain2.wav");
	sound_sight.assign("brain/brnsght1.wav");
	sound_search.assign("brain/brnsrch1.wav");
	sound_melee1.assign("brain/melee1.wav");
	sound_melee2.assign("brain/melee2.wav");
	sound_melee3.assign("brain/melee3.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/brain/tris.md2");

	gi.modelindex("models/monsters/brain/gibs/arm.md2");
	gi.modelindex("models/monsters/brain/gibs/boot.md2");
	gi.modelindex("models/monsters/brain/gibs/chest.md2");
	gi.modelindex("models/monsters/brain/gibs/door.md2");
	gi.modelindex("models/monsters/brain/gibs/head.md2");
	gi.modelindex("models/monsters/brain/gibs/pelvis.md2");

	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, 32 };

	self->health = M_ADDON_HEALTH(self);
	self->max_health = self->health;

	if (!st.was_key_specified("power_armor_type") && M_BRAIN_POWER_ARMOR_TYPE != IT_NULL) {
		self->monsterinfo.power_armor_type = static_cast<item_id_t>(M_BRAIN_POWER_ARMOR_TYPE);
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = M_BRAIN_ADDON_POWER_ARMOR(self);
	}

	self->gib_health = -90;
	self->mass = 400;
	//self->yaw_speed = 400;

	self->pain = brain_pain;
	self->die = brain_die;

	self->monsterinfo.stand = brain_stand;
	self->monsterinfo.walk = brain_walk;
	self->monsterinfo.run = brain_run;

	// PMM
	self->monsterinfo.dodge = M_MonsterDodge;
	self->monsterinfo.duck = brain_duck;
	self->monsterinfo.unduck = monster_duck_up;
	// pmm
	// RAFAEL
	self->monsterinfo.attack = brain_attack;
	// RAFAEL
	self->monsterinfo.melee = brain_melee;
	self->monsterinfo.sight = brain_sight;
	self->monsterinfo.search = brain_search;
	self->monsterinfo.idle = brain_idle;
	self->monsterinfo.setskin = brain_setskin;
	self->monsterinfo.blocked = brain_blocked; // PGM

	gi.linkentity(self);

	M_SetAnimation(self, &brain_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	self->monsterinfo.drop_height = 256;
	// HORDE MOD: Increased jump height from 68 to 88 (30% increase) for better obstacle navigation
	self->monsterinfo.jump_height = 88;
	self->monsterinfo.can_jump = true;

	walkmonster_start(self);

	ApplyMonsterBonusFlags(self);
}
