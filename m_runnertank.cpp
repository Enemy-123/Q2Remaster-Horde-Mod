// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

runnertank

==============================================================================
*/

#include "g_local.h"

#include "m_runnertank.h"

#include "m_flash.h"

void runnertank_refire_rocket(edict_t* self);
void runnetank_doattack_rocket(edict_t* self);
void runnertank_reattack_blaster(edict_t* self);

static cached_soundindex sound_thud;
static cached_soundindex sound_pain, sound_pain2;
static cached_soundindex sound_idle;
static cached_soundindex sound_die;
static cached_soundindex sound_step;
static cached_soundindex sound_sight;
static cached_soundindex sound_windup;
static cached_soundindex sound_strike;

constexpr spawnflags_t SPAWNFLAG_runnertank_COMMANDER_GUARDIAN = 8_spawnflag;
constexpr spawnflags_t SPAWNFLAG_runnertank_COMMANDER_HEAT_SEEKING = 16_spawnflag;

//
// misc
//

MONSTERINFO_SIGHT(runnertank_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

void runnertank_footstep(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_step, 1, ATTN_NORM, 0);
}

void runnertank_thud(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_thud, 1, ATTN_NORM, 0);
}

void runnertank_windup(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_windup, 1, ATTN_NORM, 0);
}

MONSTERINFO_IDLE(runnertank_idle) (edict_t* self) -> void
{
	gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

//
// stand
//

mframe_t runnertank_frames_stand[] = {
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
MMOVE_T(runnertank_move_stand) = { FRAME_stand01, FRAME_stand30, runnertank_frames_stand, nullptr };

MONSTERINFO_STAND(runnertank_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &runnertank_move_stand);
}

//
// walk
//

void runnertank_walk(edict_t* self);

#if 0
mframe_t runnertank_frames_start_walk[] = {
	{ ai_walk },
	{ ai_walk, 6 },
	{ ai_walk, 6 },
	{ ai_walk, 11, runnertank_footstep }
};
MMOVE_T(runnertank_move_start_walk) = { FRAME_walk01, FRAME_walk04, runnertank_frames_start_walk, runnertank_walk };
#endif

mframe_t runnertank_frames_walk[] = {
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 3 },
	{ ai_walk, 2 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 4 },
	{ ai_walk, 4, runnertank_footstep },
	{ ai_walk, 3 },
	{ ai_walk, 5 },
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 7 },
	{ ai_walk, 7 },
	{ ai_walk, 6 },
	{ ai_walk, 6, runnertank_footstep }
};
MMOVE_T(runnertank_move_walk) = { FRAME_walk05, FRAME_walk20, runnertank_frames_walk, nullptr };

#if 0
mframe_t runnertank_frames_stop_walk[] = {
	{ ai_walk, 3 },
	{ ai_walk, 3 },
	{ ai_walk, 2 },
	{ ai_walk, 2 },
	{ ai_walk, 4, runnertank_footstep }
};
MMOVE_T(runnertank_move_stop_walk) = { FRAME_walk21, FRAME_walk25, runnertank_frames_stop_walk, runnertank_stand };
#endif

MONSTERINFO_WALK(runnertank_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &runnertank_move_walk);
}

//
// run
//

void runnertank_run(edict_t* self);

mframe_t runnertank_frames_start_run[] = {
	{ ai_run },
	{ ai_run, 6 },
	{ ai_run, 6 },
	{ ai_run, 11, runnertank_footstep }
};
MMOVE_T(runnertank_move_start_run) = { FRAME_walk01, FRAME_walk04, runnertank_frames_start_run, runnertank_run };

mframe_t runnertank_frames_run[] = {
	{ ai_run, 4 },
	{ ai_run, 5 },
	{ ai_run, 3 },
	{ ai_run, 2 },
	{ ai_run, 5 },
	{ ai_run, 5 },
	{ ai_run, 4 },
	{ ai_run, 4, runnertank_footstep },
	{ ai_run, 3 },
	{ ai_run, 5 },
	{ ai_run, 4 },
	{ ai_run, 5 },
	{ ai_run, 7 },
	{ ai_run, 7 },
	{ ai_run, 6 },
	{ ai_run, 6, runnertank_footstep }
};
MMOVE_T(runnertank_move_run) = { FRAME_walk05, FRAME_walk20, runnertank_frames_run, nullptr };

#if 0
mframe_t runnertank_frames_stop_run[] = {
	{ ai_run, 3 },
	{ ai_run, 3 },
	{ ai_run, 2 },
	{ ai_run, 2 },
	{ ai_run, 4, runnertank_footstep }
};
MMOVE_T(runnertank_move_stop_run) = { FRAME_walk21, FRAME_walk25, runnertank_frames_stop_run, runnertank_walk };
#endif

MONSTERINFO_RUN(runnertank_run) (edict_t* self) -> void
{
	if (self->enemy && self->enemy->client)
		self->monsterinfo.aiflags |= AI_BRUTAL;
	else
		self->monsterinfo.aiflags &= ~AI_BRUTAL;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		M_SetAnimation(self, &runnertank_move_stand);
		return;
	}

	if (self->monsterinfo.active_move == &runnertank_move_walk ||
		self->monsterinfo.active_move == &runnertank_move_start_run)
	{
		M_SetAnimation(self, &runnertank_move_run);
	}
	else
	{
		M_SetAnimation(self, &runnertank_move_start_run);
	}
}

//
// pain
//

mframe_t runnertank_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(runnertank_move_pain1) = { FRAME_pain201, FRAME_pain204, runnertank_frames_pain1, runnertank_run };

mframe_t runnertank_frames_pain3[] = {
	{ ai_move, -7 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move, 3 },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, runnertank_footstep }
};
MMOVE_T(runnertank_move_pain3) = { FRAME_pain301, FRAME_pain316, runnertank_frames_pain3, runnertank_run };

PAIN(runnertank_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (mod.id != MOD_CHAINFIST && damage <= 10)
		return;

	if (level.time < self->pain_debounce_time)
		return;

	if (mod.id != MOD_CHAINFIST)
	{
		if (damage <= 30)
			if (frandom() > 0.2f)
				return;

		// don't go into pain while attacking
		if ((self->s.frame >= FRAME_attak301) && (self->s.frame <= FRAME_attak330))
			return;
		if ((self->s.frame >= FRAME_attak101) && (self->s.frame <= FRAME_attak116))
			return;
	}

	self->pain_debounce_time = level.time + 3_sec;

	if (self->count)
		gi.sound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	// PMM - blindfire cleanup
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
	// pmm

	if (damage <= 50)
		M_SetAnimation(self, &runnertank_move_pain1);
	else
		M_SetAnimation(self, &runnertank_move_pain3);
}

MONSTERINFO_SETSKIN(runnertank_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

// [Paril-KEX]
bool M_AdjustBlindfireTarget(edict_t* self, const vec3_t& start, const vec3_t& target, const vec3_t& right, vec3_t& out_dir);

//
// attacks
//

void runnertankBlaster(edict_t* self)
{
	vec3_t					 forward, right;
	vec3_t					 start;
	vec3_t					 dir;
	monster_muzzleflash_id_t flash_number;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	bool   blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;

	if (self->s.frame == FRAME_attak110)
		flash_number = MZ2_TANK_BLASTER_1;
	else if (self->s.frame == FRAME_attak113)
		flash_number = MZ2_TANK_BLASTER_2;
	else // (self->s.frame == FRAME_attak116)
		flash_number = MZ2_TANK_BLASTER_3;

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	// pmm - blindfire support
	vec3_t target;

	// PMM
	if (blindfire)
	{
		target = self->monsterinfo.blind_fire_target;

		if (!M_AdjustBlindfireTarget(self, start, target, right, dir))
			return;
	}
	else
		PredictAim(self, self->enemy, start, 0, false, 0.f, &dir, nullptr);
	// pmm

	monster_fire_blaster(self, start, dir, 30, 800, flash_number, EF_BLASTER);
}

void runnertankStrike(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_strike, 1, ATTN_NORM, 0);
}

void runnertankRocket(edict_t* self)
{
	vec3_t					 forward, right;
	vec3_t					 start;
	vec3_t					 dir;
	vec3_t					 vec;
	monster_muzzleflash_id_t flash_number;
	int						 rocketSpeed; // PGM
	// pmm - blindfire support
	vec3_t target;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	bool   blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;

	if (self->s.frame == FRAME_attak324)
		flash_number = MZ2_TANK_ROCKET_1;
	else if (self->s.frame == FRAME_attak327)
		flash_number = MZ2_TANK_ROCKET_2;
	else // (self->s.frame == FRAME_attak330)
		flash_number = MZ2_TANK_ROCKET_3;

	AngleVectors(self->s.angles, forward, right, nullptr);

	// [Paril-KEX] scale
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	if (self->speed)
		rocketSpeed = self->speed;
	else if (self->spawnflags.has(SPAWNFLAG_runnertank_COMMANDER_HEAT_SEEKING))
		rocketSpeed = 500;
	else
		rocketSpeed = 650;

	// PMM
	if (blindfire)
		target = self->monsterinfo.blind_fire_target;
	else
		target = self->enemy->s.origin;
	// pmm

	// PGM
	//  PMM - blindfire shooting
	if (blindfire)
	{
		vec = target;
		dir = vec - start;
	}
	// pmm
	// don't shoot at feet if they're above me.
	else if (frandom() < 0.66f || (start[2] < self->enemy->absmin[2]))
	{
		vec = self->enemy->s.origin;
		vec[2] += self->enemy->viewheight;
		dir = vec - start;
	}
	else
	{
		vec = self->enemy->s.origin;
		vec[2] = self->enemy->absmin[2] + 1;
		dir = vec - start;
	}
	// PGM

	//======
	// PMM - lead target  (not when blindfiring)
	// 20, 35, 50, 65 chance of leading
	if ((!blindfire) && ((frandom() < (0.2f + ((3 - skill->integer) * 0.15f)))))
		PredictAim(self, self->enemy, start, rocketSpeed, false, 0, &dir, &vec);
	// PMM - lead target
	//======

	dir.normalize();

	// pmm blindfire doesn't check target (done in checkattack)
	// paranoia, make sure we're not shooting a target right next to us
	if (blindfire)
	{
		// blindfire has different fail criteria for the trace
		if (M_AdjustBlindfireTarget(self, start, vec, right, dir))
		{
			if (self->spawnflags.has(SPAWNFLAG_runnertank_COMMANDER_HEAT_SEEKING))
				monster_fire_heat(self, start, dir, 50, rocketSpeed, flash_number, self->accel);
			else
				monster_fire_rocket(self, start, dir, 50, rocketSpeed, flash_number);
		}
	}
	else
	{
		trace_t trace = gi.traceline(start, vec, self, MASK_PROJECTILE);

		if (trace.fraction > 0.5f || trace.ent->solid != SOLID_BSP)
		{
			if (self->spawnflags.has(SPAWNFLAG_runnertank_COMMANDER_HEAT_SEEKING))
				monster_fire_heat(self, start, dir, 50, rocketSpeed, flash_number, self->accel);
			else
				monster_fire_rocket(self, start, dir, 50, rocketSpeed, flash_number);
		}
	}
}

void runnertankMachineGun(edict_t* self)
{
	vec3_t					 dir;
	vec3_t					 vec;
	vec3_t					 start;
	vec3_t					 forward, right;
	monster_muzzleflash_id_t flash_number;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	flash_number = static_cast<monster_muzzleflash_id_t>(MZ2_TANK_MACHINEGUN_1 + (self->s.frame - FRAME_attak406));

	AngleVectors(self->s.angles, forward, right, nullptr);

	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	if (self->enemy)
	{
		vec = self->enemy->s.origin;
		vec[2] += self->enemy->viewheight;
		vec -= start;
		vec = vectoangles(vec);
		dir[0] = vec[0];
	}
	else
	{
		dir[0] = 0;
	}
	if (self->s.frame <= FRAME_attak415)
		dir[1] = self->s.angles[1] - 8 * (self->s.frame - FRAME_attak411);
	else
		dir[1] = self->s.angles[1] + 8 * (self->s.frame - FRAME_attak419);
	dir[2] = 0;

	AngleVectors(dir, forward, nullptr, nullptr);

	monster_fire_bullet(self, start, forward, 20, 4, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, flash_number);
}

static void runnertank_blind_check(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		vec3_t aim = self->monsterinfo.blind_fire_target - self->s.origin;
		self->ideal_yaw = vectoyaw(aim);
	}
}

mframe_t runnertank_frames_attack_blast[] = {
    { ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
    { ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
    { ai_charge }, { ai_charge, 0, runnertankBlaster },
    { ai_charge }, { ai_charge }, { ai_charge, 0, runnertankBlaster },
    { ai_charge }, { ai_charge }, { ai_charge, 0, runnertankBlaster },
    { ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
    { ai_charge }, { ai_charge }
};
MMOVE_T(runnertank_move_attack_blast) = { FRAME_attak101, FRAME_attak122, runnertank_frames_attack_blast, runnertank_reattack_blaster };

mframe_t runnertank_frames_reattack_blast[] = {
	{ ai_charge },
	{ ai_charge, 0, runnertankBlaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, runnertankBlaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, runnertankBlaster } // 16
};
MMOVE_T(runnertank_move_reattack_blast) = { FRAME_attak111, FRAME_attak122, runnertank_frames_reattack_blast, runnertank_reattack_blaster };

mframe_t runnertank_frames_attack_post_blast[] = {
	{ ai_move }, // 17
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 3 },
	{ ai_move, 2 },
	{ ai_move, -2, runnertank_footstep } // 22
};
MMOVE_T(runnertank_move_attack_post_blast) = { FRAME_attak117, FRAME_attak122, runnertank_frames_attack_post_blast, runnertank_run };

void runnertank_reattack_blaster(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		M_SetAnimation(self, &runnertank_move_attack_post_blast);
		return;
	}

	if (visible(self, self->enemy))
		if (self->enemy->health > 0)
			if (frandom() <= 0.6f)
			{
				M_SetAnimation(self, &runnertank_move_reattack_blast);
				return;
			}
	M_SetAnimation(self, &runnertank_move_attack_post_blast);
}
void runnertank_doattack_rocket(edict_t* self);

void runnertank_poststrike(edict_t* self)
{
	self->enemy = nullptr;
	// [Paril-KEX]
	self->monsterinfo.pausetime = HOLD_FOREVER;
	self->monsterinfo.stand(self);
}

mframe_t runnertank_frames_attack_strike[] = {
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge, 0, runnertankStrike }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }
};
MMOVE_T(runnertank_move_attack_strike) = { FRAME_attak201, FRAME_attak238, runnertank_frames_attack_strike, runnertank_poststrike };

mframe_t runnertank_frames_attack_pre_rocket[] = {
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }
};
MMOVE_T(runnertank_move_attack_pre_rocket) = { FRAME_attak301, FRAME_attak321, runnertank_frames_attack_pre_rocket, runnertank_doattack_rocket };

mframe_t runnertank_frames_attack_fire_rocket[] = {
	{ ai_charge }, { ai_charge }, { ai_charge, 0, runnertankRocket },
	{ ai_charge }, { ai_charge }, { ai_charge, 0, runnertankRocket },
	{ ai_charge }, { ai_charge }, { ai_charge, 0, runnertankRocket },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge }, { ai_charge }
};
MMOVE_T(runnertank_move_attack_fire_rocket) = { FRAME_attak322, FRAME_attak335, runnertank_frames_attack_fire_rocket, runnertank_refire_rocket };

mframe_t runnertank_frames_attack_post_rocket[] = {

	{ ai_charge, -9 },
	{ ai_charge, -8 },
	{ ai_charge, -7 },
	{ ai_charge, -1 },
	{ ai_charge, -1, runnertank_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, // 50
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(runnertank_move_attack_post_rocket) = { FRAME_attak326, FRAME_attak335, runnertank_frames_attack_post_rocket, runnertank_run };

mframe_t runnertank_frames_attack_chain[] = {
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { nullptr, 0, runnertankMachineGun },
	{ nullptr, 0, runnertankMachineGun }, { nullptr, 0, runnertankMachineGun },
	{ nullptr, 0, runnertankMachineGun }, { nullptr, 0, runnertankMachineGun },
	{ nullptr, 0, runnertankMachineGun }, { nullptr, 0, runnertankMachineGun },
	{ nullptr, 0, runnertankMachineGun }, { nullptr, 0, runnertankMachineGun },
	{ nullptr, 0, runnertankMachineGun }, { nullptr, 0, runnertankMachineGun },
	{ nullptr, 0, runnertankMachineGun }, { nullptr, 0, runnertankMachineGun },
	{ nullptr, 0, runnertankMachineGun }, { nullptr, 0, runnertankMachineGun },
	{ nullptr, 0, runnertankMachineGun }, { nullptr, 0, runnertankMachineGun },
	{ nullptr, 0, runnertankMachineGun }, { nullptr, 0, runnertankMachineGun },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge }, { ai_charge }
};
MMOVE_T(runnertank_move_attack_chain) = { FRAME_attak401, FRAME_attak429, runnertank_frames_attack_chain, runnertank_run };

void runnertank_refire_rocket(edict_t* self)
{
	// PMM - blindfire cleanup
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		M_SetAnimation(self, &runnertank_move_attack_post_rocket);
		return;
	}
	// pmm

	if (self->enemy->health > 0)
		if (visible(self, self->enemy))
			if (frandom() <= 0.4f)
			{
				M_SetAnimation(self, &runnertank_move_attack_fire_rocket);
				return;
			}
	M_SetAnimation(self, &runnertank_move_attack_post_rocket);
}

void runnertank_doattack_rocket(edict_t* self)
{
	M_SetAnimation(self, &runnertank_move_attack_fire_rocket);
}

MONSTERINFO_ATTACK(runnertank_attack) (edict_t* self) -> void
{
	vec3_t vec;
	float  range;
	float  r;
	// PMM
	float chance;

	// PMM
	if (!self->enemy || !self->enemy->inuse)
		return;

	//if (self->enemy->health <= 0)
	//{
	//	M_SetAnimation(self, &runnertank_move_attack_strike);
	//	self->monsterinfo.aiflags &= ~AI_BRUTAL;
	//	return;
	//}

	// PMM
	if (self->monsterinfo.attack_state == AS_BLIND)
	{
		// setup shot probabilities
		if (self->monsterinfo.blind_fire_delay < 1_sec)
			chance = 1.0f;
		else if (self->monsterinfo.blind_fire_delay < 7.5_sec)
			chance = 0.4f;
		else
			chance = 0.1f;

		r = frandom();

		self->monsterinfo.blind_fire_delay += 5.2_sec + random_time(3_sec);

		// don't shoot at the origin
		if (!self->monsterinfo.blind_fire_target)
			return;

		// don't shoot if the dice say not to
		if (r > chance)
			return;

		bool rocket_visible = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_ROCKET_1]);
		bool blaster_visible = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]);

		if (!rocket_visible && !blaster_visible)
			return;

		bool use_rocket = (rocket_visible && blaster_visible) ? brandom() : rocket_visible;

		// turn on manual steering to signal both manual steering and blindfire
		self->monsterinfo.aiflags |= AI_MANUAL_STEERING;

		if (use_rocket)
			M_SetAnimation(self, &runnertank_move_attack_fire_rocket);
		else
		{
			M_SetAnimation(self, &runnertank_move_attack_blast);
			self->monsterinfo.nextframe = FRAME_attak108;
		}

		self->monsterinfo.attack_finished = level.time + random_time(3_sec, 5_sec);
		self->pain_debounce_time = level.time + 5_sec; // no pain for a while
		return;
	}
	// pmm

	vec = self->enemy->s.origin - self->s.origin;
	range = vec.length();

	r = frandom();

	if (range <= 125)
	{
		bool can_machinegun = (!self->enemy->classname || strcmp(self->enemy->classname, "tesla_mine")) && M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_MACHINEGUN_5]);

		if (can_machinegun && r < 0.5f)
			M_SetAnimation(self, &runnertank_move_attack_chain);
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]))
			M_SetAnimation(self, &runnertank_move_attack_blast);
	}
	else if (range <= 250)
	{
		bool can_machinegun = (!self->enemy->classname || strcmp(self->enemy->classname, "tesla_mine")) && M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_MACHINEGUN_5]);

		if (can_machinegun && r < 0.25f)
			M_SetAnimation(self, &runnertank_move_attack_chain);
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]))
			M_SetAnimation(self, &runnertank_move_attack_blast);
	}
	else
	{
		bool can_machinegun = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_MACHINEGUN_5]);
		bool can_rocket = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_ROCKET_1]);

		if (can_machinegun && r < 0.33f)
			M_SetAnimation(self, &runnertank_move_attack_chain);
		else if (can_rocket && r < 0.66f)
		{
			M_SetAnimation(self, &runnertank_move_attack_pre_rocket);
			self->pain_debounce_time = level.time + 5_sec; // no pain for a while
		}
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]))
			M_SetAnimation(self, &runnertank_move_attack_blast);
	}
}

//
// death
//

void runnertank_dead(edict_t* self)
{
	self->mins = { -16, -16, -16 };
	self->maxs = { 16, 16, -0 };
	monster_dead(self);
}

static void runnertank_shrink(edict_t* self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t runnertank_frames_death1[] = {
	{ ai_move, -7 },
	{ ai_move, -2 },
	{ ai_move, -2 },
	{ ai_move, 1 },
	{ ai_move, 3 },
	{ ai_move, 6 },
	{ ai_move, 1 },
	{ ai_move, 1 },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -2 },
	{ ai_move },
	{ ai_move },
	{ ai_move, -3 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -4 },
	{ ai_move, -6 },
	{ ai_move, -4 },
	{ ai_move, -5 },
	{ ai_move, -7, runnertank_shrink },
	{ ai_move, -15, runnertank_thud },
	{ ai_move, -5 },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(runnertank_move_death) = { FRAME_death01, FRAME_death32, runnertank_frames_death1, runnertank_dead };

DIE(runnertank_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		ThrowGibs(self, damage, {
			{ "models/objects/gibs/sm_meat/tris.md2" },
			{ 3, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
			{ "models/objects/gibs/gear/tris.md2", GIB_METALLIC },
			{ 2, "models/monsters/runnertank/gibs/foot.md2", GIB_SKINNED | GIB_METALLIC },
			{ 2, "models/monsters/runnertank/gibs/thigh.md2", GIB_SKINNED | GIB_METALLIC },
			{ "models/monsters/runnertank/gibs/chest.md2", GIB_SKINNED },
			{ "models/monsters/runnertank/gibs/head.md2", GIB_HEAD | GIB_SKINNED }
			});

		if (!self->style)
			ThrowGib(self, "models/monsters/runnertank/gibs/barm.md2", damage, GIB_SKINNED | GIB_UPRIGHT, self->s.scale);

		self->deadflag = true;
		return;
	}

	if (self->deadflag)
		return;

	// [Paril-KEX] dropped arm
	if (!self->style)
	{
		self->style = 1;

		auto [fwd, rgt, up] = AngleVectors(self->s.angles);

		edict_t* arm_gib = ThrowGib(self, "models/monsters/runnertank/gibs/barm.md2", damage, GIB_SKINNED | GIB_UPRIGHT, self->s.scale);
		arm_gib->s.origin = self->s.origin + (rgt * -16.f) + (up * 23.f);
		arm_gib->s.old_origin = arm_gib->s.origin;
		arm_gib->avelocity = { crandom() * 15.f, crandom() * 15.f, 180.f };
		arm_gib->velocity = (up * 100.f) + (rgt * -120.f);
		arm_gib->s.angles = self->s.angles;
		arm_gib->s.angles[2] = -90.f;
		arm_gib->s.skinnum /= 2;
		gi.linkentity(arm_gib);
	}

	// regular death
	gi.sound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	self->deadflag = true;
	self->takedamage = true;

	M_SetAnimation(self, &runnertank_move_death);
}

//===========
// PGM
MONSTERINFO_BLOCKED(runnertank_blocked) (edict_t* self, float dist) -> bool
{
	if (blocked_checkplat(self, dist))
		return true;

	return false;
}
// PGM
//===========

//
// monster_runnertank
//

/*QUAKED monster_runnertank (1 .5 0) (-32 -32 -16) (32 32 72) Ambush Trigger_Spawn Sight
model="models/monsters/runnertank/tris.md2"
*/
/*QUAKED monster_runnertank_commander (1 .5 0) (-32 -32 -16) (32 32 72) Ambush Trigger_Spawn Sight Guardian HeatSeeking
 */
void SP_monster_runnertank(edict_t* self)
{
	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	self->s.modelindex = gi.modelindex("models/vault/monsters/tank/tris.md2");
	self->mins = { -32, -32, -16 };
	self->maxs = { 32, 32, 64 };
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	gi.modelindex("models/monsters/runnertank/gibs/barm.md2");
	gi.modelindex("models/monsters/runnertank/gibs/head.md2");
	gi.modelindex("models/monsters/runnertank/gibs/chest.md2");
	gi.modelindex("models/monsters/runnertank/gibs/foot.md2");
	gi.modelindex("models/monsters/runnertank/gibs/thigh.md2");

	sound_thud.assign("runnertank/tnkdeth2.wav");
	sound_idle.assign("runnertank/tnkidle1.wav");
	sound_die.assign("runnertank/death.wav");
	sound_step.assign("runnertank/step.wav");
	sound_windup.assign("runnertank/tnkatck4.wav");
	sound_strike.assign("runnertank/tnkatck5.wav");
	sound_sight.assign("runnertank/sight1.wav");

	gi.soundindex("runnertank/tnkatck1.wav");
	gi.soundindex("runnertank/tnkatk2a.wav");
	gi.soundindex("runnertank/tnkatk2b.wav");
	gi.soundindex("runnertank/tnkatk2c.wav");
	gi.soundindex("runnertank/tnkatk2d.wav");
	gi.soundindex("runnertank/tnkatk2e.wav");
	gi.soundindex("runnertank/tnkatck3.wav");

	if (strcmp(self->classname, "monster_runnertank_commander") == 0)
	{
		self->health = 1000 * st.health_multiplier;
		self->gib_health = -225;
		self->count = 1;
		sound_pain2.assign("runnertank/pain.wav");
	}
	else
	{
		self->health = 750 * st.health_multiplier;
		self->gib_health = -200;
		sound_pain.assign("runnertank/tnkpain2.wav");
	}

	self->monsterinfo.scale = MODEL_SCALE;

	// [Paril-KEX] N64 runnertank commander is a chonky boy
	if (self->spawnflags.has(SPAWNFLAG_runnertank_COMMANDER_GUARDIAN))
	{
		if (!self->s.scale)
			self->s.scale = 1.5f;
		self->health = 1500 * st.health_multiplier;
	}

	// heat seekingness
	if (!self->accel)
		self->accel = 0.075f;

	self->mass = 500;

	self->pain = runnertank_pain;
	self->die = runnertank_die;
	self->monsterinfo.stand = runnertank_stand;
	self->monsterinfo.walk = runnertank_walk;
	self->monsterinfo.run = runnertank_run;
	self->monsterinfo.dodge = nullptr;
	self->monsterinfo.attack = runnertank_attack;
	self->monsterinfo.melee = nullptr;
	self->monsterinfo.sight = runnertank_sight;
	self->monsterinfo.idle = runnertank_idle;
	self->monsterinfo.blocked = runnertank_blocked; // PGM
	self->monsterinfo.setskin = runnertank_setskin;

	gi.linkentity(self);

	M_SetAnimation(self, &runnertank_move_stand);

	walkmonster_start(self);

	// PMM
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	self->monsterinfo.blindfire = true;
	// pmm
	if (strcmp(self->classname, "monster_runnertank_commander") == 0)
		self->s.skinnum = 2;
}

void Use_Boss3(edict_t* ent, edict_t* other, edict_t* activator);

THINK(Think_runnertankStand) (edict_t* ent) -> void
{
	if (ent->s.frame == FRAME_stand30)
		ent->s.frame = FRAME_stand01;
	else
		ent->s.frame++;
	ent->nextthink = level.time + 10_hz;
}

/*QUAKED monster_runnertank_stand (1 .5 0) (-32 -32 0) (32 32 90)

Just stands and cycles in one place until targeted, then teleports away.
N64 edition!
*/
void SP_monster_runnertank_stand(edict_t* self)
{
	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->model = "models/vault/monsters/tank/tris.md2";
	self->s.modelindex = gi.modelindex(self->model);
	self->s.frame = FRAME_stand01;
	self->s.skinnum = 2;

	gi.soundindex("misc/bigtele.wav");

	self->mins = { -32, -32, -16 };
	self->maxs = { 32, 32, 64 };

	if (!self->s.scale)
		self->s.scale = 1.5f;

	self->mins *= self->s.scale;
	self->maxs *= self->s.scale;

	self->use = Use_Boss3;
	self->think = Think_runnertankStand;
	self->nextthink = level.time + 10_hz;
	gi.linkentity(self);
}