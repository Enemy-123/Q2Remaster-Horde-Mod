// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

Makron -- Final Boss

==============================================================================
*/

#include "g_local.h"
#include "m_boss32.h"
#include "m_flash.h"
#include "shared.h"
#include "monster_constants.h"
void SP_monster_makronkl(edict_t* self);
void MakronRailgun(edict_t *self);
void MakronSaveloc(edict_t *self);
void MakronHyperblaster(edict_t *self);
void makron_step_left(edict_t *self);
void makron_step_right(edict_t *self);
void makronBFG(edict_t *self);
void makron_dead(edict_t *self);

static cached_soundindex sound_pain4;
static cached_soundindex sound_pain5;
static cached_soundindex sound_pain6;
static cached_soundindex sound_death;
static cached_soundindex sound_step_left;
static cached_soundindex sound_step_right;
static cached_soundindex sound_attack_bfg;
static cached_soundindex sound_brainsplorch;
static cached_soundindex sound_prerailgun;
static cached_soundindex sound_popup;
static cached_soundindex sound_taunt1;
static cached_soundindex sound_taunt2;
static cached_soundindex sound_taunt3;
static cached_soundindex sound_hit;

void makron_taunt(edict_t *self)
{
	float r;

	r = frandom();
	if (r <= 0.3f)
		gi.sound(self, CHAN_AUTO, sound_taunt1, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
	else if (r <= 0.6f)
		gi.sound(self, CHAN_AUTO, sound_taunt2, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_AUTO, sound_taunt3, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
}

//
// stand
//

mframe_t makron_frames_stand[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }, // 10
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }, // 20
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }, // 30
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }, // 40
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }, // 50
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand } // 60
};
MMOVE_T(makron_move_stand) = { FRAME_stand201, FRAME_stand260, makron_frames_stand, nullptr };

MONSTERINFO_STAND(makron_stand) (edict_t *self) -> void
{
	M_SetAnimation(self, &makron_move_stand);
}

mframe_t makron_frames_run[] = {
	{ ai_run, 5, makron_step_left },
	{ ai_run, 15 },
	{ ai_run, 11 },
	{ ai_run, 11 },
	{ ai_run, 11, makron_step_right },
	{ ai_run, 9 },
	{ ai_run, 15 },
	{ ai_run, 13 },
	{ ai_run, 9 },
	{ ai_run, 15 }
};
MMOVE_T(makron_move_run) = { FRAME_walk204, FRAME_walk213, makron_frames_run, nullptr };

void makron_hit(edict_t *self)
{
	gi.sound(self, CHAN_AUTO, sound_hit, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
}

void makron_popup(edict_t *self)
{
	gi.sound(self, CHAN_BODY, sound_popup, 1, ATTN_NONE, 0);
}

void makron_step_left(edict_t *self)
{
	gi.sound(self, CHAN_BODY, sound_step_left, 1, ATTN_NORM, 0);
}

void makron_step_right(edict_t *self)
{
	gi.sound(self, CHAN_BODY, sound_step_right, 1, ATTN_NORM, 0);
}

void makron_brainsplorch(edict_t *self)
{
	gi.sound(self, CHAN_VOICE, sound_brainsplorch, 1, ATTN_NORM, 0);
}

void makron_prerailgun(edict_t *self)
{
	gi.sound(self, CHAN_WEAPON, sound_prerailgun, 1, ATTN_NORM, 0);
}

mframe_t makron_frames_walk[] = {
	{ ai_walk, 3, makron_step_left },
	{ ai_walk, 12 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8, makron_step_right },
	{ ai_walk, 6 },
	{ ai_walk, 12 },
	{ ai_walk, 9 },
	{ ai_walk, 6 },
	{ ai_walk, 12 }
};
MMOVE_T(makron_move_walk) = { FRAME_walk204, FRAME_walk213, makron_frames_run, nullptr };

MONSTERINFO_WALK(makron_walk) (edict_t *self) -> void
{
	M_SetAnimation(self, &makron_move_walk);
}

MONSTERINFO_RUN(makron_run) (edict_t *self) -> void
{
/* unused atm to avoid makron spawning bfgs at player corpse, spawn camping
if (self->enemy && self->enemy->client)
		self->monsterinfo.aiflags |= AI_BRUTAL;
	else
		self->monsterinfo.aiflags &= ~AI_BRUTAL;
		*/
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &makron_move_stand);
	else
		M_SetAnimation(self, &makron_move_run);
}

mframe_t makron_frames_pain6[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 10
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, makron_popup },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 20
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, makron_taunt },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(makron_move_pain6) = { FRAME_pain601, FRAME_pain627, makron_frames_pain6, makron_run };

mframe_t makron_frames_pain5[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(makron_move_pain5) = { FRAME_pain501, FRAME_pain504, makron_frames_pain5, makron_run };

mframe_t makron_frames_pain4[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(makron_move_pain4) = { FRAME_pain401, FRAME_pain404, makron_frames_pain4, makron_run };

/*
---
Makron Torso. This needs to be spawned in
---
*/

THINK(makron_torso_think) (edict_t *self) -> void
{
	if (++self->s.frame >= 365)
		self->s.frame = 346;
	
	self->nextthink = level.time + 10_hz;

	if (self->s.angles[0] > 0)
		self->s.angles[0] = max(0.f, self->s.angles[0] - 15);
}

void makron_torso(edict_t* ent)
{
	// Ensure ent is valid before proceeding
	if (!ent) {
		// Use Com_PrintFmt for error logging
		// gi.Com_PrintFmt("ERROR: makron_torso called with NULL entity.\n");
		return;
	}

	ent->s.frame = 346;
	ent->s.modelindex = gi.modelindex("models/monsters/boss3/rider/tris.md2");
	ent->s.skinnum = 1;
	ent->think = makron_torso_think;
	ent->nextthink = level.time + 10_hz;
	ent->s.sound = gi.soundindex("makron/spine.wav");
	ent->movetype = MOVETYPE_TOSS;
	ent->s.effects = EF_GIB;

	// Use AngleVectors from q_vec3.h to get orientation vectors
	vec3_t forward, up;
	// The AngleVectors overload taking references and nullptr_t is available
	AngleVectors(ent->s.angles, forward, nullptr, up);

	// Use vec3_t operators for velocity and origin adjustments
	ent->velocity += (up * 120.f);      // operator*=(scalar), operator+=
	ent->velocity += (forward * -120.f);
	ent->s.origin += (forward * -10.f);

	// Use operator[] to set angle component
	// Assuming PITCH corresponds to index 0 based on typical Quake engine conventions
	ent->s.angles[PITCH] = 90.f;

	// Use vec3_origin for clarity when zeroing a vector
	ent->avelocity = vec3_origin;

	gi.linkentity(ent);
}

void makron_spawn_torso(edict_t* self)
{
	// Ensure self is valid
	if (!self) {
		// Use Com_PrintFmt for error logging
		// gi.Com_PrintFmt("ERROR: makron_spawn_torso called with NULL self entity.\n");
		return;
	}

	// Call ThrowGib - assumes it correctly returns edict_t* and initializes basic properties
	edict_t* tempent = ThrowGib(self, "models/monsters/boss3/rider/tris.md2", 0, GIB_NONE, self->s.scale);

	// Check if ThrowGib returned a valid entity
	if (!tempent)
	{
		// gi.Com_PrintFmt("ERROR: makron_spawn_torso failed to spawn torso gib for {} ({}) at {}\n",
		// 	self->classname ? self->classname : "unknown", // Safely handle potentially null classname
		// 	self->s.number,
		// 	self->s.origin); // Pass vec3_t directly, fmt should handle it

		// Attempt to handle boss death even if gib fails, to avoid blocking game progression.
		if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED) {
			BossDeathHandler(self);
		}
		return; // Exit the function to prevent dereferencing a NULL pointer
	}

	tempent->s.origin = self->s.origin;
	tempent->s.angles = self->s.angles;

	if (tempent->maxs[2] > 0.f) { // Basic sanity check using floating point comparison
		self->maxs[2] -= tempent->maxs[2];
	}
	else {
		// Log if tempent->maxs[2] seems invalid
		// gi.Com_PrintFmt("Warning: makron_spawn_torso encountered non-positive tempent->maxs[2] ({}) for {} ({})\n",
			// tempent->maxs[2], self->classname ? self->classname : "unknown", self->s.number);
	}

	tempent->s.origin[2] += self->maxs[2] - 15.f;

	// Apply torso-specific properties and link the entity
	makron_torso(tempent);

	// Handle boss death logic after successfully spawning the torso
	// Ensure IS_BOSS and BOSS_DEATH_HANDLED flags are managed correctly elsewhere
	if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED) {
		BossDeathHandler(self);
	}
}

mframe_t makron_frames_death2[] = {
	{ ai_move, -15 },
	{ ai_move, 3 },
	{ ai_move, -12 },
	{ ai_move, 0, makron_step_left },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 10
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 11 },
	{ ai_move, 12 },
	{ ai_move, 11, makron_step_right },
	{ ai_move },
	{ ai_move }, // 20
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 30
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 5 },
	{ ai_move, 7 },
	{ ai_move, 6, makron_step_left },
	{ ai_move },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, 2 }, // 40
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 50
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -6 },
	{ ai_move, -4 },
	{ ai_move, -6, makron_step_right },
	{ ai_move, -4 },
	{ ai_move, -4, makron_step_left },
	{ ai_move },
	{ ai_move }, // 60
	{ ai_move },
	{ ai_move },
	{ ai_move, -2 },
	{ ai_move, -5 },
	{ ai_move, -3, makron_step_right },
	{ ai_move, -8 },
	{ ai_move, -3, makron_step_left },
	{ ai_move, -7 },
	{ ai_move, -4 },
	{ ai_move, -4, makron_step_right }, // 70
	{ ai_move, -6 },
	{ ai_move, -7 },
	{ ai_move, 0, makron_step_left },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 80
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -2 },
	{ ai_move },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move }, // 90
	{ ai_move, 27, makron_hit },
	{ ai_move, 26 },
	{ ai_move, 0, makron_brainsplorch },
	{ ai_move },
	{ ai_move } // 95
};
MMOVE_T(makron_move_death2) = { FRAME_death201, FRAME_death295, makron_frames_death2, makron_dead };

#if 0
mframe_t makron_frames_death3[] = {
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
	{ ai_move }
};
MMOVE_T(makron_move_death3) = { FRAME_death301, FRAME_death320, makron_frames_death3, nullptr };
#endif

mframe_t makron_frames_sight[] = {
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
	{ ai_move }
};
MMOVE_T(makron_move_sight) = { FRAME_active01, FRAME_active13, makron_frames_sight, makron_run };

void makronBFG(edict_t *self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t forward, right;
	vec3_t start;
	vec3_t dir;
	vec3_t vec;

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_MAKRON_BFG], forward, right);

	vec = self->enemy->s.origin;
	vec[2] += self->enemy->viewheight;
	dir = vec - start;
	dir.normalize();
	int damage = M_BFG_DMG(self);
	if (damage <= 0) damage = (horde::IsMonsterType(self, horde::MonsterTypeID::MAKRON_KL)) ? 40 : 15;
	gi.sound(self, CHAN_VOICE, sound_attack_bfg, 1, ATTN_NORM, 0);
	monster_fire_bfg(self, start, dir, damage, 300, 100, 300, MZ2_MAKRON_BFG);
}

mframe_t makron_frames_attack3boss[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, makronBFG },
	{ ai_move },
	{ ai_move },
	{ ai_charge, 0, makronBFG },
	{ ai_move }
};
MMOVE_T(makron_move_attack3boss) = { FRAME_attak301, FRAME_attak308, makron_frames_attack3boss, makron_run };

mframe_t makron_frames_attack3[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, makronBFG },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(makron_move_attack3) = { FRAME_attak301, FRAME_attak308, makron_frames_attack3, makron_run };


mframe_t makron_frames_attack4[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move, 0, MakronHyperblaster }, // fire
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(makron_move_attack4) = { FRAME_attak401, FRAME_attak426, makron_frames_attack4, makron_run };

void MakronSaveloc(edict_t* self)
{
	if (M_HasValidTarget(self)) 
	{
		self->pos1 = self->enemy->s.origin; // save for aiming the shot
		self->pos1[2] += self->enemy->viewheight;
	}
}

mframe_t makron_frames_attack_rail[] = {
	{ ai_charge, 0, makron_prerailgun },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, MakronSaveloc },
	{ ai_move, 0, MakronRailgun }, // Fire railgun
	{ ai_charge },
	{ ai_charge, 0, MakronSaveloc },
	{ ai_move, 0, MakronRailgun }, // Fire railgun
	{ ai_move, 0, MakronRailgun }, // Fire railgun
	{ ai_charge, 0, MakronSaveloc },
	{ ai_move, 0, MakronRailgun }, // Fire railgun
	{ ai_move, 0, MakronRailgun }, // Fire railgun
	{ ai_charge, 0, MakronSaveloc },
	{ ai_move, 0, MakronRailgun }, // Fire railgun
	{ ai_move, 0, MakronRailgun }, // Fire railgun
};
MMOVE_T(makron_move_attack_rail) = { FRAME_attak501, FRAME_attak516, makron_frames_attack_rail, makron_run };

void makron_reattack_railgun(edict_t* self);

mframe_t makron_frames_attack5[] = {
	{ ai_charge, 0, makron_prerailgun },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, MakronSaveloc },
	{ ai_move, 0, MakronRailgun }, // Fire railgun
	{ ai_move, 0, makron_reattack_railgun},
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(makron_move_attack5) = { FRAME_attak501, FRAME_attak516, makron_frames_attack5, makron_run };

void makron_reattack_railgun(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	// if our enemy is still valid, then continue firing
	if (frandom() < 0.8f && !level.intermissiontime && !self->enemy->deadflag)
	{
		if (frandom() < 0.7f)
		MakronSaveloc(self);

		MakronRailgun(self);
		self->monsterinfo.nextframe = FRAME_attak509;
		return;
	}

	// end attack
	self->monsterinfo.attack_finished = level.time + 1.0_sec;
}

void MakronRailgun(edict_t *self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t start;
	vec3_t dir;
	vec3_t forward, right;

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_MAKRON_RAILGUN_1], forward, right);

	// calc direction to where we targted
	dir = self->pos1 - start;
	dir.normalize();

	int damage = M_GET_DMG_OR(self, RAILGUN, 50);
	monster_fire_railgun(self, start, dir, damage, 100, MZ2_MAKRON_RAILGUN_1);
}

void MakronHyperblaster(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t dir;
	vec3_t vec;
	vec3_t start;
	vec3_t forward, right;

	const monster_muzzleflash_id_t flash_number = (monster_muzzleflash_id_t)(MZ2_MAKRON_BLASTER_1 + (self->s.frame - FRAME_attak405));

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	vec = self->enemy->s.origin;
	vec[2] += self->enemy->viewheight;
	vec -= start;
	vec = vectoangles(vec);
	dir[0] = vec[0];

	if (self->s.frame <= FRAME_attak413)
		dir[1] = self->s.angles[1] - 10 * (self->s.frame - FRAME_attak413);
	else
		dir[1] = self->s.angles[1] + 10 * (self->s.frame - FRAME_attak421);
	dir[2] = 0;

	AngleVectors(dir, forward, nullptr, nullptr);

	int damage = M_GET_DMG_OR(self, BLASTER, 35);

	if (horde::IsMonsterType(self, horde::MonsterTypeID::MAKRON))
	{
		int speed = M_BLASTER2_SPEED(self);
		monster_fire_blaster2(self, start, forward, damage, speed > 0 ? speed : 1300, flash_number, EF_BLASTER);
	}
	else
	{
		int speed = M_BLASTER_BOLT_SPEED(self);
		monster_fire_blaster_bolt(self, start, forward, damage, speed > 0 ? speed : 2300, flash_number, EF_HYPERBLASTER);
	}
}

PAIN(makron_pain) (edict_t *self, edict_t *other, float kick, int damage, const mod_t &mod) -> void
{
	if (self->monsterinfo.active_move == &makron_move_sight)
		return;

	if (level.time < self->pain_debounce_time)
		return;

	// Lessen the chance of him going into his pain frames
	if (mod.id != MOD_CHAINFIST && damage <= 25)
		if (frandom() < 0.2f)
			return;

	self->pain_debounce_time = level.time + 3_sec;

	bool do_pain6 = false;

	if (damage <= 40)
		gi.sound(self, CHAN_VOICE, sound_pain4, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
	else if (damage <= 110)
		gi.sound(self, CHAN_VOICE, sound_pain5, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
	else
	{
		if (damage <= 150)
		{
			if (frandom() <= 0.45f)
			{
				do_pain6 = true;
				gi.sound(self, CHAN_VOICE, sound_pain6, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
			}
		}
		else
		{
			if (frandom() <= 0.35f)
			{
				do_pain6 = true;
				gi.sound(self, CHAN_VOICE, sound_pain6, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
			}
		}
	}
	
	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	if (damage <= 40)
		M_SetAnimation(self, &makron_move_pain4);
	else if (damage <= 110)
		M_SetAnimation(self, &makron_move_pain5);
	else if (do_pain6)
		M_SetAnimation(self, &makron_move_pain6);
}

MONSTERINFO_SETSKIN(makron_setskin) (edict_t *self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

MONSTERINFO_SIGHT(makron_sight) (edict_t *self, edict_t *other) -> void
{
	M_SetAnimation(self, &makron_move_sight);
}


MONSTERINFO_ATTACK(makron_attack) (edict_t* self) -> void
{
	float r;


	r = frandom();
	if (r <= 0.3f) {
		if (horde::IsMonsterType(self, horde::MonsterTypeID::MAKRON_KL)) {
			M_SetAnimation(self, &makron_move_attack3boss);
		}
		else {
			M_SetAnimation(self, &makron_move_attack3);
		}
	}
	else if (r <= 0.6f) {
		M_SetAnimation(self, &makron_move_attack4);
	}
	else {
		if (self->health <= (self->max_health / 1.5) || frandom() <= 0.3f || self->monsterinfo.IS_BOSS) {
			gi.sound(self, CHAN_VOICE, sound_taunt1, 1, ATTN_NORM, 0);
			M_SetAnimation(self, &makron_move_attack5);
		}
		else {
			M_SetAnimation(self, &makron_move_attack_rail);
		}
	}
}
//
// death
//

void makron_dead(edict_t *self)
{
	self->mins = { -24, -24, -16 };
	self->maxs = { 24, 24, 48 };
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
	monster_dead(self);
}

DIE(makron_die) (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t &point, const mod_t &mod) -> void
{
	//OnEntityDeath(self);
	self->s.sound = 0;

	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);
		ThrowGibs(self, damage, {
			{ "models/objects/gibs/sm_meat/tris.md2" },
			{ 4, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
			{ "models/objects/gibs/gear/tris.md2", GIB_METALLIC | GIB_HEAD }
		});
		self->deadflag = true;
		return;
	}

	if (self->deadflag)
		return;

	// regular death
	gi.sound(self, CHAN_VOICE, sound_death, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
	self->deadflag = true;
	self->takedamage = true;
	self->svflags |= SVF_DEADMONSTER;

	M_SetAnimation(self, &makron_move_death2);

	makron_spawn_torso(self);

	self->mins = { -60, -60, 0 };
	self->maxs = { 60, 60, 48 };

	/*	unused: when makron dies, it will trigger teleport, leaving torso.
    if (self->enemy->health <= 0)
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BOSSTPORT);
		gi.WritePosition(self->s.origin);
		gi.multicast(self->s.origin, MULTICAST_PHS, false);

		// just hide, don't kill ent so we can trigger it again
		self->svflags |= SVF_NOCLIENT|SVF_DEADMONSTER;
		self->solid = SOLID_NOT;
		gi.linkentity(self);
		monster_dead(self);
		self->monsterinfo.aiflags &= ~AI_BRUTAL;
		return;
	}

	*/
}

// [Paril-KEX] use generic function
MONSTERINFO_CHECKATTACK(Makron_CheckAttack) (edict_t *self) -> bool
{
	return M_CheckAttack_Base(self, 0.4f, 0.8f, 0.4f, 0.2f, 0.0f, 0.f);
}

//
// monster_makron
//

void MakronPrecache()
{
	sound_pain4.assign("makron/pain3.wav");
	sound_pain5.assign("makron/pain2.wav");
	sound_pain6.assign("makron/pain1.wav");
	sound_death.assign("makron/death.wav");
	sound_step_left.assign("makron/step1.wav");
	sound_step_right.assign("makron/step2.wav");
	sound_attack_bfg.assign("makron/bfg_fire.wav");
	sound_brainsplorch.assign("makron/brain1.wav");
	sound_prerailgun.assign("makron/rail_up.wav");
	sound_popup.assign("makron/popup.wav");
	sound_taunt1.assign("makron/voice4.wav");
	sound_taunt2.assign("makron/voice3.wav");
	sound_taunt3.assign("makron/voice.wav");
	sound_hit.assign("makron/bhit.wav");

	gi.modelindex("models/monsters/boss3/rider/tris.md2");
}

/*QUAKED monster_makron (1 .5 0) (-30 -30 0) (30 30 90) Ambush Trigger_Spawn Sight
 */
void SP_monster_makron(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (self->monsterinfo.monster_type_id == MONSTER_TYPE_UNKNOWN) { // Check if it hasn't been set yet
		self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::MAKRON);
	}	if (g_horde->integer) {
		// --- REFACTORED ---
		// Use the fast helper function.
		if (horde::IsMonsterType(self, horde::MonsterTypeID::MAKRON_KL))
		{
			const float randomsearch = frandom();
			if (randomsearch < 0.23f)
				gi.sound(self, CHAN_VOICE, sound_taunt1, 1, ATTN_NONE, 0);
			else if (randomsearch < 0.56f)
				gi.sound(self, CHAN_VOICE, gi.soundindex("makron/voice2.wav"), 1, ATTN_NONE, 0);
			else
				gi.sound(self, CHAN_VOICE, gi.soundindex("makron/voice2.wav"), 1, ATTN_NONE, 0);
		}
	}


	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	MakronPrecache();

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/boss3/rider/tris.md2");
	self->mins = { -30, -30, 0 };
	self->maxs = { 30, 30, 90 };
	// Power armor
	if (!st.was_key_specified("power_armor_type") && M_MAKRON_POWER_ARMOR_TYPE != IT_NULL) {
		self->monsterinfo.power_armor_type = static_cast<item_id_t>(M_MAKRON_POWER_ARMOR_TYPE);
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = M_MAKRON_ADDON_POWER_ARMOR(self);
	}

	// Armor
	if (!st.was_key_specified("armor_type") && M_MAKRON_INITIAL_ARMOR > 0) {
		self->monsterinfo.armor_type = IT_ARMOR_COMBAT;
		if (!st.was_key_specified("armor_power"))
			self->monsterinfo.armor_power = M_MAKRON_ADDON_ARMOR(self);
	}


	self->health = static_cast<int>(M_JANITOR2_INITIAL_HEALTH * st.health_multiplier);

	// --- REFACTORED ---
	// This logic will be run *after* the ID has been potentially overridden by the KL spawner.
	// Override with variant-specific config values from monsters.lua
	if (horde::IsMonsterType(self, horde::MonsterTypeID::MAKRON_KL)) {
		if (!st.was_key_specified("power_armor_type"))
			self->monsterinfo.power_armor_type = static_cast<item_id_t>(M_MAKRON_KL_POWER_ARMOR_TYPE);
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = M_MAKRON_KL_ADDON_POWER_ARMOR(self);
	}
	else if (horde::IsMonsterType(self, horde::MonsterTypeID::MAKRON)) { // Explicitly check for base Makron
		if (!st.was_key_specified("armor_type"))
			self->monsterinfo.armor_type = IT_ARMOR_COMBAT;
		if (!st.was_key_specified("armor_power"))
			self->monsterinfo.armor_power = M_MAKRON_ADDON_ARMOR(self);
	}

	if (g_horde->integer && !self->monsterinfo.IS_BOSS) {
		self->health = 3500;
		if (self->health >= 6500)
			self->health = 6500;
	}
	self->gib_health = -800;
	self->mass = 500;

	self->pain = makron_pain;
	self->die = makron_die;
	self->monsterinfo.stand = makron_stand;
	self->monsterinfo.walk = makron_walk;
	self->monsterinfo.run = makron_run;
	self->monsterinfo.dodge = nullptr;
	self->monsterinfo.attack = makron_attack;
	self->monsterinfo.melee = nullptr;
	self->monsterinfo.sight = makron_sight;
	self->monsterinfo.checkattack = Makron_CheckAttack;
	self->monsterinfo.blocked = M_MonsterBlocked;
	self->monsterinfo.setskin = makron_setskin;

	gi.linkentity(self);


	//	M_SetAnimation(self, &makron_move_stand);
	M_SetAnimation(self, &makron_move_sight);
	self->monsterinfo.scale = MODEL_SCALE;
	if (!self->s.scale)
		self->s.scale = 1.0f;

	walkmonster_start(self);

	// PMM
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	// pmm

	ApplyMonsterBonusFlags(self);
}

//HORDE BOSS
void SP_monster_makronkl(edict_t* self)
{
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::MAKRON_KL);	// 1. Call the base spawner. It sets up a standard Makron.
	SP_monster_makron(self);

	self->s.skinnum = 2;

	// Makronkl boss scaling
	if (g_horde && g_horde->integer && current_wave_level > 0) {
		self->health = M_MAKRON_KL_ADDON_HEALTH(self);
	} else {
		self->health = M_MAKRON_KL_INITIAL_HEALTH;
	}

	self->s.alpha = 0.4f;
	self->s.effects = EF_FLAG1;

    // --- REFACTORED ---
    // This check is now clean and specific to this function.
	if (horde::IsMonsterType(self, horde::MonsterTypeID::MAKRON_KL)) {
		ApplyMonsterBonusFlags(self);
	}
}
/*
=================
MakronSpawn

=================
*/
THINK(MakronSpawn) (edict_t* self) -> void
{
	vec3_t	 vec;
	edict_t* player;

	if (g_horde->integer && current_wave_level >= 21) {
		// Spawn Makronkl when in horde mode and wave level is 21 or higher
		SP_monster_makronkl(self);
	}
	else if (g_horde->integer || !deathmatch->integer) {
		// Spawn Makron when in horde mode or not in deathmatch
		SP_monster_makron(self);
	}

	self->think(self);

	// jump at player
	if (M_HasValidTarget(self))
		player = self->enemy;
	else
		player = AI_GetSightClient(self);

	if (!player)
		return;

	vec = player->s.origin - self->s.origin;
	self->s.angles[YAW] = vectoyaw(vec);
	vec.normalize();
	self->velocity = vec * 400;
	self->velocity[2] = 200;
	self->groundentity = nullptr;
	self->enemy = player;
	FoundTarget(self);
	self->monsterinfo.sight(self, self->enemy);
	self->s.frame = self->monsterinfo.nextframe = FRAME_active01; // FIXME: why????

	if (g_horde->integer)
	self->monsterinfo.IS_BOSS = true;
}

/*
=================
MakronToss

Jorg is just about dead, so set up to launch Makron out
=================
*/
void MakronToss(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	// // Store summoned properties early before any returns ////maybe for makron_spawn?
	// bool was_summoned = (self->chain && self->teammaster);
	// edict_t* stored_chain = self->chain;
	// edict_t* stored_teammaster = self->teammaster;
	// bonus_flags_t stored_bonus_flags = self->monsterinfo.bonus_flags;

	// // Debug output
	// if (was_summoned) {
	// 	gi.Com_PrintFmt("MakronToss: Jorg was summoned! chain={}, teammaster={}, bonus_flags={}\n",
	// 		(void*)stored_chain, (void*)stored_teammaster, (int)stored_bonus_flags);
	// }

	edict_t* ent = G_Spawn();

	if (self->monsterinfo.IS_BOSS) {
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BOSSTPORT);
		gi.WritePosition(self->s.origin);
		gi.multicast(self->s.origin, MULTICAST_PHS, false);
		// just hide, don't kill ent so we can trigger it again
		self->svflags |= SVF_NOCLIENT | SVF_DEADMONSTER;
		self->solid = SOLID_NOT;
		gi.linkentity(self);
		monster_dead(self);
		self->monsterinfo.aiflags &= ~AI_BRUTAL;
		self->monsterinfo.aiflags &= ~AI_DOUBLE_TROUBLE;
		return;
	}
	// Determine if we should spawn a non-boss Makron
	const bool shouldSpawnMakron = (g_horde->integer && current_wave_level <= 20 && !self->monsterinfo.IS_BOSS) ||
		(!g_horde->integer);

	// Determine if we should spawn a Makronkl
	const bool shouldSpawnMakronkl = (g_horde->integer && current_wave_level >= 21 && !self->monsterinfo.IS_BOSS);

	if (shouldSpawnMakron) {
		ent->classname = "monster_makron";
		ent->target = self->target;
		ent->s.origin = self->s.origin;
		ent->enemy = self->enemy;
	}
	else if (shouldSpawnMakronkl) {
		ent->classname = "monster_makronkl";
		ent->target = self->target;
		ent->s.origin = self->s.origin;
		ent->enemy = self->enemy;
	}

	// Decide whether to spawn based on horde settings and boss status
	if (!g_horde->integer || (g_horde->integer && !self->monsterinfo.IS_BOSS)) {
		MakronSpawn(ent);
	}

	// // Restore summoned properties if Jorg was summoned //maybe for makron_spawn?
	// if (was_summoned && stored_chain && stored_teammaster) {
	// 	gi.Com_PrintFmt("MakronToss: Restoring summoned properties to Makron\n");
	// 	ent->chain = stored_chain;
	// 	ent->teammaster = stored_teammaster;
	// 	ent->monsterinfo.isfriendlyspawn = true;
	// 	ent->monsterinfo.bonus_flags |= (stored_bonus_flags & BF_FRIENDLY);  // Preserve friendly flag

	// 	// Reapply touch function for summoned monsters
	// 	if (ent->teammaster && ent->teammaster->client) {
	// 		ent->touch = strogg_summoned_touch;
	// 	}

	// 	gi.Com_PrintFmt("MakronToss: After restore - chain={}, teammaster={}, isfriendlyspawn={}, bonus_flags={}\n",
	// 		(void*)ent->chain, (void*)ent->teammaster, ent->monsterinfo.isfriendlyspawn, (int)ent->monsterinfo.bonus_flags);
	// }

	// [Paril-KEX] set health bar over to Makron when we throw him out
	for (size_t i = 0; i < 2; i++)
		if (level.health_bar_entities[i] && level.health_bar_entities[i]->enemy == self)
			level.health_bar_entities[i]->enemy = ent;
}
