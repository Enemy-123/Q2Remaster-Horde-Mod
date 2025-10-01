// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

chick

==============================================================================
*/

#include "g_local.h"
#include "m_chick.h"
#include "m_flash.h"
#include "shared.h"

// Forward declare plasma_touch from xatrix
void plasma_touch(edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self);

void chick_stand(edict_t* self);
void chick_run(edict_t* self);
void chick_reslash(edict_t* self);
void chick_rerocket(edict_t* self);
void chick_attack1(edict_t* self);
void ChickSaveLoc(edict_t* self);

static cached_soundindex sound_missile_prelaunch;
static cached_soundindex sound_missile_launch;
static cached_soundindex sound_melee_swing;
static cached_soundindex sound_melee_hit;
static cached_soundindex sound_missile_reload;
static cached_soundindex sound_death1;
static cached_soundindex sound_death2;
static cached_soundindex sound_fall_down;
static cached_soundindex sound_idle1;
static cached_soundindex sound_idle2;
static cached_soundindex sound_pain1;
static cached_soundindex sound_pain2;
static cached_soundindex sound_pain3;
static cached_soundindex sound_sight;
static cached_soundindex sound_search;
static cached_soundindex sound_railgun;

void ChickMoan(edict_t* self)
{
	if (frandom() < 0.5f)
		gi.sound(self, CHAN_VOICE, sound_idle1, 1, ATTN_IDLE, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_idle2, 1, ATTN_IDLE, 0);
}

mframe_t chick_frames_fidget[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, ChickMoan },
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
MMOVE_T(chick_move_fidget) = { FRAME_stand201, FRAME_stand230, chick_frames_fidget, chick_stand };

void chick_fidget(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		return;
	else if (self->enemy)
		return;
	if (frandom() <= 0.3f)
		M_SetAnimation(self, &chick_move_fidget);
}

mframe_t chick_frames_stand[] = {
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
	{ ai_stand, 0, chick_fidget },
};
MMOVE_T(chick_move_stand) = { FRAME_stand101, FRAME_stand130, chick_frames_stand, nullptr };

MONSTERINFO_STAND(chick_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &chick_move_stand);
}

mframe_t chick_frames_start_run[] = {
	{ ai_run, 1 },
	{ ai_run },
	{ ai_run, 0, monster_footstep },
	{ ai_run, -1 },
	{ ai_run, -1, monster_footstep },
	{ ai_run },
	{ ai_run, 1 },
	{ ai_run, 3 },
	{ ai_run, 6 },
	{ ai_run, 3 }
};
MMOVE_T(chick_move_start_run) = { FRAME_walk01, FRAME_walk10, chick_frames_start_run, chick_run };

mframe_t chick_frames_run[] = {
	{ ai_run, 6 },
	{ ai_run, 8, monster_footstep },
	{ ai_run, 13 },
	{ ai_run, 5, monster_done_dodge }, // make sure to clear dodge bit
	{ ai_run, 7 },
	{ ai_run, 4 },
	{ ai_run, 11, monster_footstep },
	{ ai_run, 5 },
	{ ai_run, 9 },
	{ ai_run, 7 }
};

MMOVE_T(chick_move_run) = { FRAME_walk11, FRAME_walk20, chick_frames_run, nullptr };

mframe_t chick_frames_walk[] = {
	{ ai_walk, 6 },
	{ ai_walk, 8, monster_footstep },
	{ ai_walk, 13 },
	{ ai_walk, 5 },
	{ ai_walk, 7 },
	{ ai_walk, 4 },
	{ ai_walk, 11, monster_footstep },
	{ ai_walk, 5 },
	{ ai_walk, 9 },
	{ ai_walk, 7 }
};

MMOVE_T(chick_move_walk) = { FRAME_walk11, FRAME_walk20, chick_frames_walk, nullptr };

MONSTERINFO_WALK(chick_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &chick_move_walk);
}

MONSTERINFO_RUN(chick_run) (edict_t* self) -> void
{
	monster_done_dodge(self);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		M_SetAnimation(self, &chick_move_stand);
		return;
	}

	if (self->monsterinfo.active_move == &chick_move_walk ||
		self->monsterinfo.active_move == &chick_move_start_run)
	{
		M_SetAnimation(self, &chick_move_run);
	}
	else
	{
		M_SetAnimation(self, &chick_move_start_run);
	}
}

mframe_t chick_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(chick_move_pain1) = { FRAME_pain101, FRAME_pain105, chick_frames_pain1, chick_run };

mframe_t chick_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(chick_move_pain2) = { FRAME_pain201, FRAME_pain205, chick_frames_pain2, chick_run };

mframe_t chick_frames_pain3[] = {
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move, -6 },
	{ ai_move, 3, monster_footstep },
	{ ai_move, 11 },
	{ ai_move, 3, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move, 4 },
	{ ai_move, 1 },
	{ ai_move },
	{ ai_move, -3 },
	{ ai_move, -4 },
	{ ai_move, 5 },
	{ ai_move, 7 },
	{ ai_move, -2 },
	{ ai_move, 3 },
	{ ai_move, -5 },
	{ ai_move, -2 },
	{ ai_move, -8 },
	{ ai_move, 2, monster_footstep }
};
MMOVE_T(chick_move_pain3) = { FRAME_pain301, FRAME_pain321, chick_frames_pain3, chick_run };

PAIN(chick_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	float r;

	monster_done_dodge(self);

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;

	r = frandom();
	if (r < 0.33f)
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else if (r < 0.66f)
		gi.sound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_pain3, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	// PMM - clear this from blindfire
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

	if (damage <= 10)
		M_SetAnimation(self, &chick_move_pain1);
	else if (damage <= 25)
		M_SetAnimation(self, &chick_move_pain2);
	else
		M_SetAnimation(self, &chick_move_pain3);

	// PMM - clear duck flag
	if (self->monsterinfo.aiflags & AI_DUCKED)
		monster_duck_up(self);
}

MONSTERINFO_SETSKIN(chick_setpain) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

void chick_dead(edict_t* self)
{
	self->mins = { -16, -16, 0 };
	self->maxs = { 16, 16, 8 };
	monster_dead(self);
}

static void chick_shrink(edict_t* self)
{
	self->maxs[2] = 12;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t chick_frames_death2[] = {
	{ ai_move, -6 },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, -5, monster_footstep },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, -2 },
	{ ai_move, 1 },
	{ ai_move, 10 },
	{ ai_move, 2 },
	{ ai_move, 3, monster_footstep },
	{ ai_move, 1 },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move, 3 },
	{ ai_move, 3 },
	{ ai_move, 1, monster_footstep },
	{ ai_move, -3 },
	{ ai_move, -5 },
	{ ai_move, 4 },
	{ ai_move, 15, chick_shrink },
	{ ai_move, 14, monster_footstep },
	{ ai_move, 1 }
};
MMOVE_T(chick_move_death2) = { FRAME_death201, FRAME_death223, chick_frames_death2, chick_dead };

mframe_t chick_frames_death1[] = {
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move, -7 },
	{ ai_move, 4, monster_footstep },
	{ ai_move, 11, chick_shrink },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move }
};
MMOVE_T(chick_move_death1) = { FRAME_death101, FRAME_death112, chick_frames_death1, chick_dead };

DIE(chick_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	int n;

	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		ThrowGibs(self, damage, {
			{ 2, "models/objects/gibs/bone/tris.md2" },
			{ 3, "models/objects/gibs/sm_meat/tris.md2" },
			{ "models/monsters/bitch/gibs/arm.md2", GIB_SKINNED | GIB_UPRIGHT },
			{ "models/monsters/bitch/gibs/foot.md2", GIB_SKINNED | GIB_UPRIGHT },
			{ "models/monsters/bitch/gibs/tube.md2", GIB_SKINNED | GIB_UPRIGHT },
			{ "models/monsters/bitch/gibs/chest.md2", GIB_SKINNED },
			{ "models/monsters/bitch/gibs/head.md2", GIB_HEAD | GIB_SKINNED }
			});
		self->deadflag = true;

		return;
	}

	if (self->deadflag)
		return;

	// regular death
	self->deadflag = true;
	self->takedamage = true;

	n = brandom();

	if (n == 0)
	{
		M_SetAnimation(self, &chick_move_death1);
		gi.sound(self, CHAN_VOICE, sound_death1, 1, ATTN_NORM, 0);
	}
	else
	{
		M_SetAnimation(self, &chick_move_death2);
		gi.sound(self, CHAN_VOICE, sound_death2, 1, ATTN_NORM, 0);
	}
}

// PMM - changes to duck code for new dodge

mframe_t chick_frames_duck[] = {
	{ ai_move, 0, monster_duck_down },
	{ ai_move, 1 },
	{ ai_move, 4, monster_duck_hold },
	{ ai_move, -4 },
	{ ai_move, -5, monster_duck_up },
	{ ai_charge, 0, chick_attack1 },
	{ ai_charge, 0, chick_attack1 },
};
MMOVE_T(chick_move_duck) = { FRAME_duck01, FRAME_duck07, chick_frames_duck, chick_run };

void chickkl_fire_plasma(edict_t* self);

void ChickSlash(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t const aim = { MELEE_DISTANCE, self->mins[0], 10 };
	gi.sound(self, CHAN_WEAPON, sound_melee_swing, 1, ATTN_NORM, 0);
	fire_hit(self, aim, irandom(10, 16), 100);
}
void chickkl_fire_plasma(edict_t* self);

// External bomb spell functions from horde/g_bombspell.cpp
extern void carpetbomb_think(edict_t* self);
extern void bombarea_think(edict_t* self);

// Heat-seeking plasma think for chickkl
static inline vec3_t heat_chick_get_dist_vec(const edict_t* heat, const edict_t* target, float dist_to_target)
{
	return safe_normalized(((target->s.origin + vec3_t{ 0.f, 0.f, target->mins.z }) + (target->velocity * (clamp(dist_to_target / 500.f, 0.f, 1.f)) * 0.5f)) - heat->s.origin);
}

THINK(heat_chick_think) (edict_t* self) -> void
{
	edict_t* acquire = self->enemy;
	float oldlen = 99999.0f;
	float olddot = 1.0f;

	// Search for new target periodically
	bool search_for_new_target = (!M_HasValidTarget(self) || (level.time > self->timestamp));

	if (search_for_new_target)
	{
		self->timestamp = level.time + 200_ms;
		acquire = nullptr;
		vec3_t const fwd = AngleVectors(self->s.angles).forward;

		edict_t* target = nullptr;
		while ((target = findradius(target, self->s.origin, 1024)) != nullptr)
		{
			// Look for enemies (not clients like turret does)
			if (self->owner == target || !target->takedamage || !target->inuse || target->health <= 0 || !visible(self, target))
				continue;

			// Don't target other monsters if owner is a monster
			if (self->owner && (self->owner->svflags & SVF_MONSTER) && (target->svflags & SVF_MONSTER))
				continue;

			float const dist_to_target = (self->s.origin - target->s.origin).length();
			vec3_t vec = heat_chick_get_dist_vec(self, target, dist_to_target);
			float const dot = vec.dot(fwd);

			if (dot >= olddot)
				continue;

			if (acquire == nullptr || dot < olddot || dist_to_target < oldlen)
			{
				acquire = target;
				oldlen = dist_to_target;
				olddot = dot;
			}
		}
		self->enemy = acquire;
	}

	// Wall avoidance
	vec3_t wall_avoid = vec3_origin;
	float wall_avoid_strength = 0.0f;

	vec3_t check_dirs[] = {
		self->movedir,
		AngleVectors(self->s.angles).right,
		-AngleVectors(self->s.angles).right,
		AngleVectors(self->s.angles).up,
		-AngleVectors(self->s.angles).up
	};

	for (int i = 0; i < 5; i++)
	{
		vec3_t end = self->s.origin + check_dirs[i] * 100.0f;
		trace_t tr = gi.traceline(self->s.origin, end, self, MASK_PROJECTILE);

		if (tr.fraction < 1.0f)
		{
			float distance = tr.fraction * 100.0f;
			float avoid_force = 1.0f - (distance / 100.0f);
			wall_avoid = wall_avoid - check_dirs[i] * avoid_force;
			wall_avoid_strength = max(wall_avoid_strength, avoid_force);
		}
	}

	if (wall_avoid.lengthSquared() > 0)
		wall_avoid = safe_normalized(wall_avoid);

	// Update direction
	vec3_t preferred_dir = self->pos1;

	if (M_HasValidTarget(self))
	{
		float const dist_to_target = (self->s.origin - self->enemy->s.origin).length();
		preferred_dir = heat_chick_get_dist_vec(self, self->enemy, dist_to_target);

		if (wall_avoid_strength > 0.1f)
		{
			float blend = wall_avoid_strength * 0.3f;
			preferred_dir = safe_normalized(preferred_dir * (1.0f - blend) + wall_avoid * blend);
		}
	}
	else if (wall_avoid_strength > 0.1f)
	{
		preferred_dir = safe_normalized(preferred_dir * 0.7f + wall_avoid * 0.3f);
	}

	self->pos1 = preferred_dir;

	float t = self->accel;
	if (self->enemy)
		t *= 0.85f;

	if (self->movedir.lengthSquared() > 0)
		self->movedir = slerp(self->movedir, preferred_dir, t).normalized();
	else
		self->movedir = preferred_dir;

	self->s.angles = vectoangles(self->movedir);

	if (self->speed < self->yaw_speed)
		self->speed += self->yaw_speed * gi.frame_time_s;

	self->velocity = self->movedir * self->speed;
	self->nextthink = level.time + FRAME_TIME_MS;
}

// Save enemy location for bombspell targeting
void ChickSaveLoc(edict_t* self)
{
	if (M_HasValidTarget(self))
	{
		self->pos1 = self->enemy->s.origin;
		self->pos1[2] += self->enemy->viewheight;
	}
}

// Chickkl bomb spell - distance-based selection
void ChickBombSpell(edict_t* self)
{
	if (!M_HasValidTarget(self))
		return;

	// Check if on cooldown using timestamp (separate from attack_finished)
	if (self->timestamp > level.time)
		return;

	vec3_t forward, dir;
	AngleVectors(self->s.angles, forward, nullptr, nullptr);

	// Use saved location if available, otherwise use current enemy position
	vec3_t target_pos = (self->pos1.lengthSquared() > 0) ? self->pos1 : self->enemy->s.origin;
	dir = (target_pos - self->s.origin).normalized();

	// Calculate distance to saved/enemy location
	float dist_to_enemy = (target_pos - self->s.origin).length();
	float height_diff = fabs(target_pos.z - self->s.origin.z);

	// Distance-based selection:
	// - Carpet bomb: 200-600 range, same height (within 128 units)
	// - Area bomb: 400+ range (has overlap for flexibility)
	bool prefer_carpet = (dist_to_enemy >= 200 && dist_to_enemy <= 600 && height_diff < 128);
	bool prefer_area = (dist_to_enemy > 400);

	// Too close for any bombspell (under 200 units) - skip this attack
	if (dist_to_enemy < 200)
	{
		self->timestamp = level.time + 1_sec;
		return;
	}

	// Choose based on distance and what was tried last
	// In overlap range (400-600), alternate; otherwise use distance preference
	bool use_carpet;
	if (dist_to_enemy >= 400 && dist_to_enemy <= 600)
	{
		// Overlap range - try both alternating
		use_carpet = (self->monsterinfo.lefty == 0);
	}
	else if (dist_to_enemy < 400)
	{
		// Only carpet viable (200-400 range)
		use_carpet = true;
	}
	else
	{
		// Only area viable (600+ range)
		use_carpet = false;
	}

	if (use_carpet)
	{
		// Monsters always succeed with carpet bomb - no validation needed
		edict_t* spell = G_Spawn();
		spell->think = carpetbomb_think;
		spell->nextthink = level.time + FRAME_TIME_MS;
		spell->s.origin = self->s.origin;
		spell->move_origin = self->s.origin;
		spell->dmg = 35 + irandom(10, 20);
		spell->dmg_radius = 150;
		spell->timestamp = level.time + 3_sec;
		spell->owner = self;
		spell->mins = vec3_origin;
		spell->maxs = vec3_origin;
		spell->solid = SOLID_NOT;
		spell->svflags |= SVF_NOCLIENT | SVF_PROJECTILE;
		spell->classname = "bombspell";
		spell->s.angles = vectoangles(forward);

		// Mark as monster-owned to skip strict visibility checks
		spell->count = 1; // Use count to indicate monster owner

		gi.linkentity(spell);

		// Set cooldown using timestamp
		self->timestamp = level.time + 2_sec;

		// Next time try area bomb
		self->monsterinfo.lefty = 1;
	}
	else
	{
		// Area bomb at enemy location - monsters always succeed
		vec3_t ground_pos;
		trace_t ground_tr;

		// Aim towards saved enemy position
		ground_pos = target_pos;
		ground_pos[2] += 32; // Start slightly above target
		vec3_t ground_end = ground_pos;
		ground_end[2] -= 512; // Trace down to find floor

		ground_tr = gi.traceline(ground_pos, ground_end, self, MASK_SOLID);

		// For monsters, use target position if no floor found
		if (ground_tr.fraction >= 1.0f || ground_tr.plane.normal.z <= 0.5f)
		{
			ground_tr.endpos = target_pos;
			ground_tr.plane.normal = { 0, 0, 1 }; // Up vector
		}

		// Monsters always succeed - spawn the area bomb
		edict_t* spell = G_Spawn();
		spell->think = bombarea_think;
		spell->nextthink = level.time + 0.3_sec; // Start bombing after short delay
		spell->s.origin = ground_tr.endpos;
		spell->dmg = 30 + irandom(5, 15);
		spell->dmg_radius = 120;
		spell->timestamp = level.time + 4_sec;
		spell->owner = self;
		spell->mins = vec3_origin;
		spell->maxs = vec3_origin;
		spell->solid = SOLID_NOT;
		spell->svflags |= SVF_NOCLIENT | SVF_PROJECTILE;
		spell->classname = "bombarea";

		// Set angles to point up (floor bombing)
		spell->s.angles = vectoangles(ground_tr.plane.normal);
		gi.linkentity(spell);

		// Set cooldown using timestamp
		self->timestamp = level.time + 2_sec;

		// Next time try carpet bomb
		self->monsterinfo.lefty = 0;
	}

	gi.sound(self, CHAN_WEAPON, sound_missile_launch, 1, ATTN_NORM, 0);
}

// Chickkl grenade attack frame function
void chickkl_grenade(edict_t* self)
{
	if (!M_HasValidTarget(self))
		return;

	vec3_t start, forward, right, target, aim;
	AngleVectors(self->s.angles, forward, right, nullptr);
	start = G_ProjectSource(self->s.origin, monster_flash_offset[MZ2_CHICK_ROCKET_1], forward, right);

	// Aim at enemy
	target = self->enemy->s.origin;
	target[2] += self->enemy->viewheight;
	aim = (target - start).normalized();

	// Fire grenade
	monster_fire_grenade(self, start, aim, 40, 600, MZ2_CHICK_ROCKET_1, 2.5f, 120);
}

void ChickRocket(edict_t* self)
{
	// Basic enemy check - blindfire logic needs to execute
	if (!M_HasEnemy(self))
		return;

	// Check if this is a chickkl - use rocket attack instead
	if (self->monsterinfo.monster_type_id == static_cast<uint8_t>(horde::MonsterTypeID::CHICKKL))
	{
		chickkl_fire_plasma(self);
		return;
	}

	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	dir;
	vec3_t	vec;
	trace_t trace; // PMM - check target
	int		rocketSpeed;
	// pmm - blindfire
	vec3_t target;
	bool   blindfire = false;

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
		blindfire = true;
	else
		blindfire = false;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_CHICK_ROCKET_1], forward, right);
	// [Paril-KEX]
	if (self->s.skinnum > 1)
		rocketSpeed = 850;
	else
		rocketSpeed = 1060;

	// PMM
	if (blindfire)
		target = self->monsterinfo.blind_fire_target;
	else
	{
		// Not blindfiring - need fully valid target
		if (!M_HasValidTarget(self))
			return;
		target = self->enemy->s.origin;
	}
	// pmm
	// PGM
	//  PMM - blindfire shooting
	if (blindfire)
	{
		vec = target;
		dir = vec - start;
	}
	// pmm
	// don't shoot at feet if they're above where i'm shooting from.
	else if (frandom() < 0.33f || (start[2] < self->enemy->absmin[2]))
	{
		vec = target;
		vec[2] += self->enemy->viewheight;
		dir = vec - start;
	}
	else
	{
		vec = target;
		vec[2] = self->enemy->absmin[2] + 1;
		dir = vec - start;
	}
	// PGM

	//======
	// PMM - lead target  (not when blindfiring)
	// 20, 35, 50, 65 chance of leading
	if ((!blindfire) && (frandom() < 0.35f))
		PredictAim(self, self->enemy, start, rocketSpeed, false, 0.f, &dir, &vec);
	// PMM - lead target
	//======

	dir.normalize();

	// pmm blindfire doesn't check target (done in checkattack)
	// paranoia, make sure we're not shooting a target right next to us
	trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
	if (blindfire)
	{
		// blindfire has different fail criteria for the trace
		if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
		{
			// RAFAEL
			if (self->s.skinnum > 1)
				monster_fire_heat(self, start, dir, 50, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.075f);
			else
				// RAFAEL
				monster_fire_rocket(self, start, dir, 50, rocketSpeed, MZ2_CHICK_ROCKET_1);
		}
		else
		{
			// geez, this is bad.  she's avoiding about 80% of her blindfires due to hitting things.
			// hunt around for a good shot
			// try shifting the target to the left a little (to help counter her large offset)
			vec = target;
			vec += (right * -10);
			dir = vec - start;
			dir.normalize();
			trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
			if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
			{
				// RAFAEL
				if (self->s.skinnum > 1)
					monster_fire_heat(self, start, dir, 50, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.095f);
				else
					// RAFAEL
					monster_fire_rocket(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1);
			}
			else
			{
				// ok, that failed.  try to the right
				vec = target;
				vec += (right * 10);
				dir = vec - start;
				dir.normalize();
				trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
				if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
				{
					// RAFAEL
					if (self->s.skinnum > 1)
						monster_fire_heat(self, start, dir, 50, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.075f);
					else
						// RAFAEL
						monster_fire_rocket(self, start, dir, 50, rocketSpeed, MZ2_CHICK_ROCKET_1);
				}
			}
		}
	}
	else
	{
		if (trace.fraction > 0.5f || trace.ent->solid != SOLID_BSP)
		{
			// RAFAEL
			if (self->s.skinnum > 1)
				monster_fire_heat(self, start, dir, 50, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.15f);
			else
				// RAFAEL
				monster_fire_rocket(self, start, dir, 50, rocketSpeed, MZ2_CHICK_ROCKET_1);
		}
	}
}

void ChickLocRail(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t	target;
	float	r;

	target = self->enemy->s.origin;
	target[2] += self->enemy->viewheight; // Aim at enemy's viewheight

	// Introduce a random offset for "miss by a small delay"
	r = frandom();
	if (r < 0.3f) // 30% chance to miss slightly
	{
		vec3_t forward, right;
		AngleVectors(self->s.angles, forward, right, nullptr);
		if (frandom() < 0.5f)
			target += (right * (frandom() * 20 - 15)); // Random horizontal offset
		else
			target += (forward * (frandom() * 20 - 15)); // Random forward/backward offset
	}

	self->pos1 = target; // Store the calculated target for ChickRailgun
}

void ChickRailgun(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t	start;
	vec3_t	dir;
	vec3_t	forward, right;
	vec3_t	target;
	bool	blindfire = false;

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
		blindfire = true;

	if (!self->enemy || !self->enemy->inuse)
		return;

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_CHICK_ROCKET_1], forward, right);

	if (blindfire)
		target = self->monsterinfo.blind_fire_target;
	else
		target = self->pos1;

	dir = target - start;
	dir.normalize();

	gi.sound(self, CHAN_WEAPON, sound_railgun, 1, ATTN_NORM, 0);
	monster_fire_railgun(self, start, dir, 80, 100, MZ2_CHICK_ROCKET_1); // Using MZ2_CHICK_ROCKET_1 for flash
}

void Chick_PreAttack1(edict_t* self)
{
	gi.sound(self, CHAN_VOICE, sound_missile_prelaunch, 1, ATTN_NORM, 0);

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		vec3_t const aim = self->monsterinfo.blind_fire_target - self->s.origin;
		self->ideal_yaw = vectoyaw(aim);
	}
}

void chick_endanim(edict_t* self);

mframe_t chick_frames_attack_railgun[] = {
	{ ai_charge, 0, Chick_PreAttack1 },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 4 },
	{ ai_charge },
	{ ai_charge, -3 },
	{ ai_charge, 3 },
	{ ai_charge, 5 },
	{ ai_charge, 13, monster_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, ChickLocRail }, // Call ChickLocRail to set target
	{ ai_charge, 0, ChickRailgun }, // ChickRailgun uses self->pos1
};
MMOVE_T(chick_move_attack_railgun) = { FRAME_attak114, FRAME_attak127, chick_frames_attack_railgun, chick_endanim };

void ChickReload(edict_t* self)
{
	gi.sound(self, CHAN_VOICE, sound_missile_reload, 1, ATTN_NORM, 0);
}

mframe_t chick_frames_start_attack1[] = {
	{ ai_charge, 0, Chick_PreAttack1 },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 4 },
	{ ai_charge },
	{ ai_charge, -3 },
	{ ai_charge, 3 },
	{ ai_charge, 5 },
	{ ai_charge, 13, monster_footstep },
	{ ai_charge },
	{ ai_charge, 0, chick_attack1 },
	{ ai_charge },
	{ ai_charge, 0, chick_attack1 }
};
MMOVE_T(chick_move_start_attack1) = { FRAME_attak101, FRAME_attak113, chick_frames_start_attack1, nullptr };

mframe_t chick_frames_attack1[] = {
	{ ai_charge, 19, ChickRocket },
	{ ai_charge, -6, monster_footstep },
	{ ai_charge, -5 },
	{ ai_charge, 19, ChickRocket },
	{ ai_charge, -7, monster_footstep },
	{ ai_charge },
	{ ai_charge, 1 },
	{ ai_charge, 10, ChickReload },
	{ ai_charge, 4 },
	{ ai_charge, 5, monster_footstep },
	{ ai_charge, 6 },
	{ ai_charge, 6 },
	{ ai_charge, 4 },
	{ ai_charge, 3, [](edict_t* self) { chick_rerocket(self); monster_footstep(self); } }
};
MMOVE_T(chick_move_attack1) = { FRAME_attak114, FRAME_attak127, chick_frames_attack1, nullptr };

mframe_t chick_frames_end_attack1[] = {
	{ ai_charge, -3 },
	{ ai_charge },
	{ ai_charge, -6 },
	{ ai_charge, -4 },
	{ ai_charge, -2, monster_footstep }
};
MMOVE_T(chick_move_end_attack1) = { FRAME_attak128, FRAME_attak132, chick_frames_end_attack1, chick_run };

void chick_rerocket(edict_t* self)
{
	// PMM - blindfire support: continue attacking with probability during blindfire
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		// Give a chance to continue blindfiring (similar to normal refire)
		if (frandom() <= 0.5f) // 50% chance to fire another burst
		{
			M_SetAnimation(self, &chick_move_attack1);
			return;
		}
		// End blindfire
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		M_SetAnimation(self, &chick_move_end_attack1);
		return;
	}
	// pmm

	if (!M_CheckClearShot(self, monster_flash_offset[MZ2_CHICK_ROCKET_1]))
	{
		M_SetAnimation(self, &chick_move_end_attack1);
		return;
	}

	if (self->enemy->health > 0)
	{
		if (range_to(self, self->enemy) > RANGE_MELEE)
			if (visible(self, self->enemy))
				if (frandom() <= 0.7f)
				{
					M_SetAnimation(self, &chick_move_attack1);
					return;
				}
	}
	M_SetAnimation(self, &chick_move_end_attack1);
}

void chick_endanim(edict_t* self)
{
	M_SetAnimation(self, &chick_move_end_attack1);
}

void chick_attack1(edict_t* self)
{
	M_SetAnimation(self, &chick_move_attack1);
}

static void chickkl_rerocket(edict_t* self);

mframe_t chickkl_frames_attack1[] = {
	{ ai_charge, 19, chickkl_fire_plasma },
	{ ai_charge, -6, monster_footstep },
	{ ai_charge, -5 },
	{ ai_charge, 19, chickkl_fire_plasma },
	{ ai_charge, -7, monster_footstep },
	{ ai_charge },
	{ ai_charge, 1 },
	{ ai_charge, 10, ChickReload },
	{ ai_charge, 4 },
	{ ai_charge, 5, monster_footstep },
	{ ai_charge, 6 },
	{ ai_charge, 6 },
	{ ai_charge, 4 },
	{ ai_charge, 3, [](edict_t* self) { chickkl_rerocket(self); monster_footstep(self); } }
};
MMOVE_T(chickkl_move_attack1) = { FRAME_attak114, FRAME_attak127, chickkl_frames_attack1, nullptr };

// CHICKKL attack1 frames - uses rocket attacks
static void chickkl_rerocket(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		M_SetAnimation(self, &chick_move_end_attack1);
		return;
	}

	if (!M_CheckClearShot(self, monster_flash_offset[MZ2_CHICK_ROCKET_1]))
	{
		M_SetAnimation(self, &chick_move_end_attack1);
		return;
	}

	if (self->enemy->health > 0)
	{
		if (range_to(self, self->enemy) > RANGE_MELEE)
			if (visible(self, self->enemy))
				if (frandom() <= 0.6f) // Slightly less aggressive than regular chick
				{
					M_SetAnimation(self, &chickkl_move_attack1);
					return;
				}
	}
	M_SetAnimation(self, &chick_move_end_attack1);
}



mframe_t chick_frames_slash[] = {
	{ ai_charge, 7, ChickSlash },
	{ ai_charge, 7, ChickSlash },
	{ ai_charge, -7, monster_footstep },
	{ ai_charge, 1 },
	{ ai_charge, -1 },
	{ ai_charge, 1 },
	{ ai_charge },
	{ ai_charge, 1 },
	{ ai_charge, -2, chick_reslash }
};
MMOVE_T(chick_move_slash) = { FRAME_attak204, FRAME_attak212, chick_frames_slash, nullptr };

mframe_t chick_frames_end_slash[] = {
	{ ai_charge, -6 },
	{ ai_charge, -1 },
	{ ai_charge, -6 },
	{ ai_charge, 0, monster_footstep }
};
MMOVE_T(chick_move_end_slash) = { FRAME_attak213, FRAME_attak216, chick_frames_end_slash, chick_run };

// CHICKKL melee frames - uses bomb spells instead of slash
static void chickkl_reslash(edict_t* self)
{
	if (M_HasValidTarget(self) && visible(self, self->enemy))
		return; // Continue with another slash
	M_SetAnimation(self, &chick_move_end_slash);
}

mframe_t chickkl_frames_slash[] = {
	{ ai_charge, 1 },
	{ ai_charge, -7, monster_footstep },
	{ ai_charge, 1 },
	{ ai_charge, -1 },
	{ ai_charge, 1 },
	{ ai_charge, 1 },
	{ ai_charge, 1, ChickSaveLoc },  // Save enemy location before bombspell
	{ ai_charge, 7, ChickBombSpell }
//	{ ai_charge, -2, chickkl_reslash }
};
MMOVE_T(chickkl_move_slash) = { FRAME_attak204, FRAME_attak212, chickkl_frames_slash, chick_run };

// Chickkl grenade attack frames (reuse rocket animation)
mframe_t chickkl_frames_grenade[] = {
	{ ai_charge, 1 },
	{ ai_charge, -7, monster_footstep },
	{ ai_charge, 1 },
	{ ai_charge, -1 },
	{ ai_charge, 1 },
	{ ai_charge, 7, chickkl_grenade },  // Fire grenade
	{ ai_charge, 1 },
	{ ai_charge, 1 }
};
MMOVE_T(chickkl_move_grenade) = { FRAME_attak204, FRAME_attak212, chickkl_frames_grenade, chick_run };

void chick_reslash(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		// If the target is gone, always end the slash combo.
		M_SetAnimation(self, &chick_move_end_slash);
		return;
	}

	if (range_to(self, self->enemy) <= RANGE_MELEE)
	{
		if (frandom() <= 0.9f)
		{
			// Continue the combo
			M_SetAnimation(self, &chick_move_slash);
		}
		else
		{
			// End the combo
			M_SetAnimation(self, &chick_move_end_slash);
		}
	}
	else // Not in melee range anymore
	{
		M_SetAnimation(self, &chick_move_end_slash);
	}
}

void chick_slash(edict_t* self)
{
	M_SetAnimation(self, &chick_move_slash);
}

mframe_t chick_frames_start_slash[] = {
	{ ai_charge, 1 },
	{ ai_charge, 8 },
	{ ai_charge, 3 }
};
MMOVE_T(chick_move_start_slash) = { FRAME_attak201, FRAME_attak203, chick_frames_start_slash, chick_slash };

MONSTERINFO_MELEE(chick_melee) (edict_t* self) -> void
{
	M_SetAnimation(self, &chick_move_start_slash);
}

MONSTERINFO_ATTACK(chick_attack) (edict_t* self) -> void
{
	float r, chance;

	monster_done_dodge(self);

	// PMM
	if (self->monsterinfo.attack_state == AS_BLIND)
	{
		// setup shot probabilities
		if (self->monsterinfo.blind_fire_delay < 1.0_sec)
			chance = 1.0;
		else if (self->monsterinfo.blind_fire_delay < 7.5_sec)
			chance = 0.4f;
		else
			chance = 0.1f;

		r = frandom();

		// minimum of 5.5 seconds, plus 0-1, after the shots are done
		self->monsterinfo.blind_fire_delay += random_time(5.5_sec, 6.5_sec);

		// don't shoot at the origin
		if (!self->monsterinfo.blind_fire_target)
			return;

		// don't shoot if the dice say not to
		if (r > chance)
			return;

		// turn on manual steering to signal both manual steering and blindfire
		self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
		M_SetAnimation(self, &chick_move_start_attack1);
		self->monsterinfo.attack_finished = level.time + random_time(2_sec);
		return;
	}
	// pmm

	// Choose between rocket and railgun attack
	r = frandom();
	if (r < 0.8f
	) // 80% chance for rocket
	{
		if (!M_CheckClearShot(self, monster_flash_offset[MZ2_CHICK_ROCKET_1]))
			return;
		M_SetAnimation(self, &chick_move_start_attack1);
	}
	else // 20% chance for railgun
	{
		M_SetAnimation(self, &chick_move_attack_railgun);
	}
}

MONSTERINFO_SIGHT(chick_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

void chick_jump_now(edict_t* self)
{
	//	gi.Com_PrintFmt("PRINT: chick_jump_now called\n");
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 100);
	self->velocity += (up * 300);
}

void chick_jump2_now(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 150);
	self->velocity += (up * 400);
}

void chick_jump_wait_land(edict_t* self)
{
	if (self->groundentity == nullptr)
	{
		self->monsterinfo.nextframe = self->s.frame;

		if (monster_jump_finished(self))
			self->monsterinfo.nextframe = self->s.frame + 1;
	}
	else
		self->monsterinfo.nextframe = self->s.frame + 1;
}

mframe_t chick_frames_jump[] = {
	{ ai_move },
	{ ai_move, 0, chick_jump_now },
	{ ai_move },
	{ ai_move, 0, chick_jump_wait_land },
	{ ai_move },
	{ ai_move },
	{ ai_move },
};
MMOVE_T(chick_move_jump) = { FRAME_duck01, FRAME_duck07, chick_frames_jump, chick_run };

mframe_t chick_frames_jump2[] = {
	{ ai_move },
	{ ai_move, 0, chick_jump_now },
	{ ai_move },
	{ ai_move, 0, chick_jump_wait_land },
	{ ai_move },
	{ ai_move },
	{ ai_move },
};
MMOVE_T(chick_move_jump2) = { FRAME_duck01, FRAME_duck07, chick_frames_jump2, chick_run };
//===========
// PGM
void chick_jump(edict_t* self, blocked_jump_result_t result)
{
	if (!self->enemy)
		return;

	monster_done_dodge(self);

	if (result == blocked_jump_result_t::JUMP_JUMP_UP)
		M_SetAnimation(self, &chick_move_jump2);
	else
		M_SetAnimation(self, &chick_move_jump);
}
// pmm - blocking code

MONSTERINFO_BLOCKED(chick_blocked) (edict_t* self, float dist) -> bool
{
	if (self->monsterinfo.can_jump)
	{
		if (auto const result = blocked_checkjump(self, dist); result != blocked_jump_result_t::NO_JUMP)
		{
			chick_jump(self, result);
			return true;
		}
	}

	if (blocked_checkplat(self, dist))
		return true;

	return false;
}

// PGM
//===========

MONSTERINFO_DUCK(chick_duck) (edict_t* self, gtime_t eta) -> bool
{
	if ((self->monsterinfo.active_move == &chick_move_start_attack1) ||
		(self->monsterinfo.active_move == &chick_move_attack1))
	{
		// if we're shooting don't dodge
		self->monsterinfo.unduck(self);
		return false;
	}

	M_SetAnimation(self, &chick_move_duck);

	return true;
}

MONSTERINFO_SIDESTEP(chick_sidestep) (edict_t* self) -> bool
{
	if ((self->monsterinfo.active_move == &chick_move_start_attack1) ||
		(self->monsterinfo.active_move == &chick_move_attack1) ||
		(self->monsterinfo.active_move == &chick_move_pain3))
	{
		// if we're shooting, don't dodge
		return false;
	}

	if (self->monsterinfo.active_move != &chick_move_run)
		M_SetAnimation(self, &chick_move_run);

	return true;
}

/*QUAKED monster_chick (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 */
void SP_monster_chick(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

    if (self->monsterinfo.monster_type_id == MONSTER_TYPE_UNKNOWN) { // Check if it hasn't been set yet
        self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::CHICK);
    }

	if (g_horde->integer)
	{
		const float randomsearch = frandom(); // Generar un número aleatorio entre 0 y 1

		if (randomsearch < 0.24f)
			gi.sound(self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
	}

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	sound_missile_prelaunch.assign("chick/chkatck1.wav");
	sound_missile_launch.assign("chick/chkatck2.wav");
	sound_melee_swing.assign("chick/chkatck3.wav");
	sound_melee_hit.assign("chick/chkatck4.wav");
	sound_missile_reload.assign("chick/chkatck5.wav");
	sound_death1.assign("chick/chkdeth1.wav");
	sound_death2.assign("chick/chkdeth2.wav");
	sound_fall_down.assign("chick/chkfall1.wav");
	sound_idle1.assign("chick/chkidle1.wav");
	sound_idle2.assign("chick/chkidle2.wav");
	sound_pain1.assign("chick/chkpain1.wav");
	sound_pain2.assign("chick/chkpain2.wav");
	sound_pain3.assign("chick/chkpain3.wav");
	sound_sight.assign("chick/chksght1.wav");
	sound_search.assign("chick/chksrch1.wav");
	sound_railgun.assign("weapons/railgr1a.wav"); // Using railgun sound from chick_heat

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/bitch/tris.md2");

	gi.modelindex("models/monsters/bitch/gibs/arm.md2");
	gi.modelindex("models/monsters/bitch/gibs/chest.md2");
	gi.modelindex("models/monsters/bitch/gibs/foot.md2");
	gi.modelindex("models/monsters/bitch/gibs/head.md2");
	gi.modelindex("models/monsters/bitch/gibs/tube.md2");

	self->mins = { -16, -16, 0 };
	self->maxs = { 16, 16, 56 };
	if (!st.was_key_specified("power_armor_type"))
		self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
	if (!st.was_key_specified("power_armor_power"))
		self->monsterinfo.power_armor_power = 75;

	self->health = 145 * st.health_multiplier;
	self->gib_health = -70;
	self->mass = 200;

	self->pain = chick_pain;
	self->die = chick_die;

	self->monsterinfo.stand = chick_stand;
	self->monsterinfo.walk = chick_walk;
	self->monsterinfo.run = chick_run;
	// pmm
	self->monsterinfo.dodge = M_MonsterDodge;
	self->monsterinfo.duck = chick_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = chick_sidestep;
	self->monsterinfo.blocked = chick_blocked; // PGM
	// pmm
	self->monsterinfo.attack = chick_attack;
	self->monsterinfo.melee = chick_melee;
	self->monsterinfo.sight = chick_sight;
	self->monsterinfo.setskin = chick_setpain;

	gi.linkentity(self);

	M_SetAnimation(self, &chick_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	// PMM
	self->monsterinfo.blindfire = true;

	self->monsterinfo.drop_height = 256;
	// HORDE MOD: Increased jump height from 68 to 88 (30% increase) for better obstacle navigation
	self->monsterinfo.jump_height = 88;
	self->monsterinfo.can_jump = true;
	// pmm
	walkmonster_start(self);
	ApplyMonsterBonusFlags(self);
}

// RAFAEL
/*QUAKED monster_chick_heat (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 */
void SP_monster_chick_heat(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::CHICK_HEAT);
	SP_monster_chick(self);

	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::CHICK_HEAT);

	// BALANCE FIX: Wave 13 Elite should have significantly more health than Wave 6 base Chick
	self->health = 500 * st.health_multiplier;

	self->s.skinnum = 2;
	self->monsterinfo.drop_height = 256;
	// HORDE MOD: Increased jump height from 68 to 88 (30% increase) for better obstacle navigation
	self->monsterinfo.jump_height = 88;
	self->monsterinfo.can_jump = true;

	gi.soundindex("weapons/railgr1a.wav");
	ApplyMonsterBonusFlags(self);
}
// RAFAEL

// ===============
// CHICKKL - Boss variant with dodge, carpet bomb, and plasma attacks
// ===============

// Forward declarations for chickkl
void chickkl_dodge(edict_t* self, edict_t* attacker, gtime_t eta, trace_t* tr, bool gravity);
void chickkl_attack(edict_t* self);

// External functions from other files
extern void fire_guardianpsx_heat(edict_t* self, const vec3_t& start, const vec3_t& dir, const vec3_t& rest_dir, int damage, int speed, float damage_radius, int radius_damage, float turn_fraction);
extern void carpetbomb_think(edict_t* self);
extern void bombarea_think(edict_t* self);

// Chickkl dodge implementation (based on runnertank)
MONSTERINFO_DODGE(chickkl_dodge) (edict_t* self, edict_t* attacker, gtime_t eta, trace_t* tr, bool gravity) -> void
{
	// Basic checks
	if (!self->groundentity || self->health <= 0)
		return;

	// Set enemy if we don't have one
	if (!self->enemy && attacker)
	{
		self->enemy = attacker;
		FoundTarget(self);
		return;
	}

	// Don't dodge if we're attacking
	if (self->monsterinfo.active_move == &chick_move_start_attack1 ||
		self->monsterinfo.active_move == &chick_move_attack1 ||
		self->monsterinfo.active_move == &chick_move_slash)
		return;

	// Check dodge cooldown using timestamp
	if (self->timestamp > level.time)
		return;

	// Don't dodge if projectile impact is too soon or too far away
	if (eta < FRAME_TIME_MS || eta > 3_sec)
		return;

	// Don't dodge if attacker is invalid
	if (!attacker)
		return;

	// Calculate dodge direction based on attacker position
	vec3_t dodge_dir;
	vec3_t right;
	AngleVectors(self->s.angles, nullptr, right, nullptr);

	// Decide dodge direction - prefer moving away from attacker
	vec3_t to_attacker = (attacker->s.origin - self->s.origin).normalized();
	float side_dot = to_attacker.dot(right);

	// Dodge perpendicular to attack direction, away from attacker
	if (side_dot > 0)
		dodge_dir = right * -1.0f; // Dodge left
	else
		dodge_dir = right; // Dodge right

	// Add some forward/backward component based on distance
	vec3_t forward;
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	float dist = (self->s.origin - attacker->s.origin).length();

	if (dist < 300.0f) {
		// Close range - dodge backward
		dodge_dir += forward * -0.4f;
	} else if (dist > 600.0f) {
		// Long range - dodge forward to close distance
		dodge_dir += forward * 0.3f;
	}

	dodge_dir = dodge_dir.normalized();

	// Calculate dodge speed based on urgency (eta)
	float base_dodge_speed = 350.0f; // Faster than runnertank
	float eta_seconds = eta.seconds();
	float urgency_multiplier = std::clamp(2.0f - eta_seconds, 1.0f, 2.5f);
	float dodge_speed = base_dodge_speed * urgency_multiplier;

	// Apply dodge velocity
	vec3_t dodge_velocity = dodge_dir * dodge_speed;

	// Preserve some vertical momentum but replace horizontal
	dodge_velocity.z = self->velocity.z * 0.5f;
	self->velocity = dodge_velocity;

	// Set animation to running for dodge
	if (self->monsterinfo.active_move != &chick_move_run)
		M_SetAnimation(self, &chick_move_run);

	// Set cooldown using timestamp
	self->timestamp = level.time + random_time(0.4_sec, 1.2_sec);

	// Also set pausetime for movement consistency
	self->monsterinfo.pausetime = level.time + random_time(0.3_sec, 0.7_sec);

	// Update lefty for consistency with sidestep
	self->monsterinfo.lefty = (side_dot > 0) ? 1 : 0;

	// Mark that we're dodging
	monster_done_dodge(self);
}

// Chickkl plasma attack - fires 2 heat-seeking plasma bolts
void chickkl_fire_plasma(edict_t* self)
{
	if (!M_HasValidTarget(self))
		return;

	vec3_t forward, right, up;
	AngleVectors(self->s.angles, forward, right, up);

	// Scale the offset for larger monsters
	vec3_t scaled_offset = monster_flash_offset[MZ2_CHICK_ROCKET_1] * self->s.scale;
	vec3_t start = G_ProjectSource2(self->s.origin, scaled_offset, forward, right, up);

	// Calculate fire direction with prediction
	const float speed = 780.0f; 
	vec3_t fire_dir;

	if (self->enemy->velocity.lengthSquared() > 1.0f) {
		vec3_t predicted_dir, predicted_pos;
		PredictAim(self, self->enemy, start, speed, true, 0, &predicted_dir, &predicted_pos);
		fire_dir = predicted_dir;
	} else {
		vec3_t target_pos = self->enemy->s.origin;
		target_pos[2] += self->enemy->viewheight * 0.5f;
		fire_dir = (target_pos - start).normalized();
	}

	// Calculate perpendicular vector for spread
	vec3_t angles = vectoangles(fire_dir);
	vec3_t spread_right;
	AngleVectors(angles, nullptr, &spread_right, nullptr);

	// Fire two plasma bolts with horizontal spread
	for (int i = 0; i < 2; i++)
	{
		// Apply spread to initial fire direction
		vec3_t spread_dir = fire_dir;
		float spread_amount = 0.06f; // Small spread

		if (i == 0)
			spread_dir = (fire_dir + spread_right * spread_amount).normalized(); // Right plasma
		else
			spread_dir = (fire_dir - spread_right * spread_amount).normalized(); // Left plasma

		// Create plasma projectile manually since fire_plasma returns void
		edict_t* plasma = G_Spawn();
		plasma->s.origin = start;
		plasma->movedir = spread_dir;
		plasma->s.angles = vectoangles(spread_dir);
		plasma->velocity = spread_dir * speed;
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
		plasma->nextthink = level.time + (0.15_sec + gtime_t::from_sec(i * 0.05f));
		plasma->think = heat_chick_think;
		plasma->dmg = 35;
		plasma->radius_dmg = 35;
		plasma->dmg_radius = 120;
		plasma->s.sound = gi.soundindex("weapons/rockfly.wav");
		plasma->s.modelindex = gi.modelindex("sprites/s_photon.sp2");
		plasma->s.effects |= EF_PLASMA | EF_ANIM_ALLFAST;

		// Heat-seeking parameters
		const float turn_fraction = 0.18f;
		plasma->speed = speed / 1.35f;
		plasma->yaw_speed = speed * 0.75f;
		plasma->accel = turn_fraction;
		plasma->pos1 = fire_dir;

		if (visible(plasma, self->enemy)) {
			plasma->enemy = self->enemy;
			plasma->timestamp = level.time + 0.6_sec;
		}

		gi.linkentity(plasma);
	}

	gi.sound(self, CHAN_WEAPON, sound_missile_launch, 1, ATTN_NORM, 0);
}

// Override attack for chickkl
MONSTERINFO_ATTACK(chickkl_attack) (edict_t* self) -> void
{
	if (!M_HasValidTarget(self))
		return;

	float range = range_to(self, self->enemy);

	// Check if this is actually a chickkl
	if (self->monsterinfo.monster_type_id == static_cast<uint8_t>(horde::MonsterTypeID::CHICKKL))
	{
		// Check if bombspell is on cooldown (using timestamp)
		bool bombspell_on_cooldown = (self->timestamp > level.time);

		if (bombspell_on_cooldown)
		{
			// During cooldown - use grenades and plasma
			if (range <= RANGE_MELEE)
			{
				// Close range - melee slash (no bombspell)
				M_SetAnimation(self, &chick_move_slash);
			}
			else
			{
				// Ranged - fire grenades or plasma
				if (frandom() <= 0.5f)
				{
					// Grenade attack using proper mmove
					M_SetAnimation(self, &chickkl_move_grenade);
				}
				else
				{
					// Plasma attack
					M_SetAnimation(self, &chickkl_move_attack1);
				}
			}
		}
		else
		{
			// Not on cooldown - can use bombspells
			if (range <= RANGE_MELEE)
			{
				// Close range - melee slash (no bombspell in melee)
				M_SetAnimation(self, &chick_move_slash);
			}
			else if (range <= RANGE_NEAR)
			{
				// Close-mid range (200-600) - use bomb spell
				M_SetAnimation(self, &chickkl_move_slash);
			}
			else
			{
				// Long range - mix between bomb spell (40%) and plasma (60%)
				if (frandom() <= 0.4f)
				{
					// Use bomb spell (will be area bomb at long range)
					M_SetAnimation(self, &chickkl_move_slash);
				}
				else
				{
					// Use plasma attack with ultrathink
					M_SetAnimation(self, &chickkl_move_attack1);
				}
			}
		}
	}
	else
	{
		// Fallback to normal chick attack
		if (range <= RANGE_MELEE)
			M_SetAnimation(self, &chick_move_slash);
		else
			M_SetAnimation(self, &chick_move_start_attack1);
	}
}

// Main chickkl spawn function
/*QUAKED monster_chickkl (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
Boss variant of chick with dodge, carpet bomb, and plasma attacks
*/
void SP_monster_chickkl(edict_t* self)
{
	// Set monster type first
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::CHICKKL);

	// Call base chick spawn
	SP_monster_chick(self);

	// Override with chickkl specifics
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::CHICKKL);

	// Boss stats
	self->health = 450 * ED_GetSpawnTemp().health_multiplier; // Much more health
	self->gib_health = -120;
	self->mass = 300;
	self->s.skinnum = 3; // Different skin if available

		(!self->s.scale);
		self->s.scale = 1.5f;
		self->mins *= self->s.scale;
		self->maxs *= self->s.scale;

	// Enhanced movement
	self->monsterinfo.drop_height = 384;
	self->monsterinfo.jump_height = 128;
	self->monsterinfo.can_jump = true;

	// Override dodge function with chickkl's enhanced dodge
	self->monsterinfo.dodge = chickkl_dodge;

	// Override attack
	self->monsterinfo.attack = chickkl_attack;

	// Power armor
	if (!ED_GetSpawnTemp().was_key_specified("power_armor_type"))
		self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
	if (!ED_GetSpawnTemp().was_key_specified("power_armor_power"))
		self->monsterinfo.power_armor_power = 150; // More armor

	// Additional sounds
	gi.soundindex("weapons/railgr1a.wav");
	gi.soundindex("weapons/rockfly.wav");

	ApplyMonsterBonusFlags(self);
}
