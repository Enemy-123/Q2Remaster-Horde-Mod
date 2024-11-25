// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

TANK

==============================================================================
*/

#include "g_local.h"
#include "m_tank.h"
#include "m_flash.h"
#include "shared.h"

void tank_refire_rocket(edict_t* self);
void tank_doattack_rocket(edict_t* self);
void tank_reattack_blaster(edict_t* self);
void tank_reattack_grenades(edict_t* self);

static cached_soundindex sound_thud;
static cached_soundindex sound_pain, sound_pain2;
static cached_soundindex sound_idle;
static cached_soundindex sound_die;
static cached_soundindex sound_step;
static cached_soundindex sound_sight;
static cached_soundindex sound_windup;
static cached_soundindex sound_strike;
static cached_soundindex sound_grenade;

constexpr spawnflags_t SPAWNFLAG_TANK_COMMANDER_GUARDIAN = 8_spawnflag;
constexpr spawnflags_t SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING = 16_spawnflag;

//
// misc
//

MONSTERINFO_SIGHT(tank_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

void tank_footstep(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_step, 1, ATTN_NORM, 0);
}

void tank_thud(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_thud, 1, ATTN_NORM, 0);
}

void tank_windup(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_windup, 1, ATTN_NORM, 0);
}

MONSTERINFO_IDLE(tank_idle) (edict_t* self) -> void
{
	gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

//
// stand
//

mframe_t tank_frames_stand[] = {
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
MMOVE_T(tank_move_stand) = { FRAME_stand01, FRAME_stand30, tank_frames_stand, nullptr };

MONSTERINFO_STAND(tank_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &tank_move_stand);
}

//
// walk
//

void tank_walk(edict_t* self);

#if 0
mframe_t tank_frames_start_walk[] = {
	{ ai_walk },
	{ ai_walk, 6 },
	{ ai_walk, 6 },
	{ ai_walk, 11, tank_footstep }
};
MMOVE_T(tank_move_start_walk) = { FRAME_walk01, FRAME_walk04, tank_frames_start_walk, tank_walk };
#endif

mframe_t tank_frames_walk[] = {
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 3 },
	{ ai_walk, 2 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 4 },
	{ ai_walk, 4, tank_footstep },
	{ ai_walk, 3 },
	{ ai_walk, 5 },
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 7 },
	{ ai_walk, 7 },
	{ ai_walk, 6 },
	{ ai_walk, 6, tank_footstep }
};
MMOVE_T(tank_move_walk) = { FRAME_walk05, FRAME_walk20, tank_frames_walk, nullptr };

#if 0
mframe_t tank_frames_stop_walk[] = {
	{ ai_walk, 3 },
	{ ai_walk, 3 },
	{ ai_walk, 2 },
	{ ai_walk, 2 },
	{ ai_walk, 4, tank_footstep }
};
MMOVE_T(tank_move_stop_walk) = { FRAME_walk21, FRAME_walk25, tank_frames_stop_walk, tank_stand };
#endif

MONSTERINFO_WALK(tank_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &tank_move_walk);
}

//
// run
//

void tank_run(edict_t* self);

mframe_t tank_frames_start_run[] = {
	{ ai_run },
	{ ai_run, 6 },
	{ ai_run, 6 },
	{ ai_run, 11, tank_footstep }
};
MMOVE_T(tank_move_start_run) = { FRAME_walk01, FRAME_walk04, tank_frames_start_run, tank_run };

mframe_t tank_frames_run[] = {
	{ ai_run, 4 },
	{ ai_run, 5 },
	{ ai_run, 3 },
	{ ai_run, 2 },
	{ ai_run, 5 },
	{ ai_run, 5 },
	{ ai_run, 4 },
	{ ai_run, 4, tank_footstep },
	{ ai_run, 3 },
	{ ai_run, 5 },
	{ ai_run, 4 },
	{ ai_run, 5 },
	{ ai_run, 7 },
	{ ai_run, 7 },
	{ ai_run, 6 },
	{ ai_run, 6, tank_footstep }
};
MMOVE_T(tank_move_run) = { FRAME_walk05, FRAME_walk20, tank_frames_run, nullptr };

#if 0
mframe_t tank_frames_stop_run[] = {
	{ ai_run, 3 },
	{ ai_run, 3 },
	{ ai_run, 2 },
	{ ai_run, 2 },
	{ ai_run, 4, tank_footstep }
};
MMOVE_T(tank_move_stop_run) = { FRAME_walk21, FRAME_walk25, tank_frames_stop_run, tank_walk };
#endif

MONSTERINFO_RUN(tank_run) (edict_t* self) -> void
{
	//if (self->enemy && self->enemy->client)
	//	self->monsterinfo.aiflags |= AI_BRUTAL;
	//else
		self->monsterinfo.aiflags &= ~AI_BRUTAL;    //trying to unable tank brutal, so he don't lose time after killing a player and its being attacked by another

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		M_SetAnimation(self, &tank_move_stand);
		return;
	}

	if (self->monsterinfo.active_move == &tank_move_walk ||
		self->monsterinfo.active_move == &tank_move_start_run)
	{
		M_SetAnimation(self, &tank_move_run);
	}
	else
	{
		M_SetAnimation(self, &tank_move_start_run);
	}
}

//
// pain
//

mframe_t tank_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(tank_move_pain1) = { FRAME_pain101, FRAME_pain104, tank_frames_pain1, tank_run };

mframe_t tank_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(tank_move_pain2) = { FRAME_pain201, FRAME_pain205, tank_frames_pain2, tank_run };

mframe_t tank_frames_pain3[] = {
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
	{ ai_move, 0, tank_footstep }
};
MMOVE_T(tank_move_pain3) = { FRAME_pain301, FRAME_pain316, tank_frames_pain3, tank_run };

PAIN(tank_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
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

	if (damage <= 30)
		M_SetAnimation(self, &tank_move_pain1);
	else if (damage <= 60)
		M_SetAnimation(self, &tank_move_pain2);
	else
		M_SetAnimation(self, &tank_move_pain3);
}

MONSTERINFO_SETSKIN(tank_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

// [Paril-KEX]
bool M_AdjustBlindfireTarget(edict_t* self, const vec3_t& start, const vec3_t& target, const vec3_t& right, vec3_t& out_dir)
{
	trace_t trace = gi.traceline(start, target, self, MASK_PROJECTILE);

	// blindfire has different fail criteria for the trace
	if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
	{
		out_dir = target - start;
		out_dir.normalize();
		return true;
	}

	// try shifting the target to the left a little (to help counter large offset)
	const vec3_t left_target = target + (right * -20);
	trace = gi.traceline(start, left_target, self, MASK_PROJECTILE);

	if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
	{
		out_dir = left_target - start;
		out_dir.normalize();
		return true;
	}

	// ok, that failed.  try to the right
	const vec3_t right_target = target + (right * 20);
	trace = gi.traceline(start, right_target, self, MASK_PROJECTILE);
	if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
	{
		out_dir = right_target - start;
		out_dir.normalize();
		return true;
	}

	return false;
}

//
// attacks
//

constexpr float MORTAR_SPEED = 1850.f;
constexpr float GRENADE_SPEED = 1600.f;

void TankGrenades(edict_t* self)
{
	if (!self->enemy || !self->enemy->inuse)
		return;

	vec3_t forward, right;
	AngleVectors(self->s.angles, forward, right, nullptr);

	// Definir el offset según el frame
	vec3_t offset;
	if (self->s.frame == FRAME_attak110)
		offset = { 28.7f, -18.5f, 28.7f };
	else if (self->s.frame == FRAME_attak113)
		offset = { 24.6f, -21.5f, 30.1f };
	else // FRAME_attak116
		offset = { 19.8f, -23.9f, 32.1f };

	// Calcular punto de inicio usando M_ProjectFlashSource
	vec3_t start = M_ProjectFlashSource(self, offset, forward, right);

	// Determinar si es disparo de mortero
	const bool is_mortar = (self->s.frame == FRAME_attak110);
	const float speed = is_mortar ? MORTAR_SPEED : GRENADE_SPEED;

	vec3_t aim, aimpoint;
	// Calcular dirección de disparo con PredictAim
	PredictAim(self, self->enemy, start, speed, true, 0, &aim, &aimpoint);

	// Añadir ligera dispersión
	aim += right * 0.05f;
	aim.normalize();

	// Intentar encontrar el mejor pitch
	if (M_CalculatePitchToFire(self, aimpoint, start, aim, speed, 2.5f, is_mortar))
		monster_fire_grenade(self, start, aim, 50, speed, MZ2_UNUSED_0, crandom_open() * 10.0f, frandom() * 10.f);
	else
		monster_fire_grenade(self, start, aim, 50, speed, MZ2_UNUSED_0, crandom_open() * 10.0f, 200.f + (crandom_open() * 10.0f));

	gi.sound(self, CHAN_WEAPON, sound_grenade, 1, ATTN_NORM, 0);
}
void TankBlaster(edict_t* self)
{
	vec3_t forward, right;
	vec3_t dir;
	monster_muzzleflash_id_t flash_number;

	if (!self->enemy || !self->enemy->inuse)
		return;

	const bool blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;

	if (self->s.frame == FRAME_attak110)
		flash_number = MZ2_TANK_BLASTER_1;
	else if (self->s.frame == FRAME_attak113)
		flash_number = MZ2_TANK_BLASTER_2;
	else // (self->s.frame == FRAME_attak116)
		flash_number = MZ2_TANK_BLASTER_3;

	AngleVectors(self->s.angles, forward, right, nullptr);

	const vec3_t start = G_ProjectSource(self->s.origin, monster_flash_offset[flash_number], forward, right);

	vec3_t bullet_offset;
	if (self->s.frame == FRAME_attak110) {
		bullet_offset = vec3_t{ 28.7f, -18.5f, 28.7f };
	}
	else if (self->s.frame == FRAME_attak113) {
		bullet_offset = vec3_t{ 24.6f, -21.5f, 30.1f };
	}
	else { // FRAME_attak116
		bullet_offset = vec3_t{ 19.8f, -23.9f, 32.1f };
	}
	const vec3_t bullet_start = G_ProjectSource(self->s.origin, bullet_offset, forward, right);

	if (blindfire) {
		vec3_t target = self->monsterinfo.blind_fire_target;
		if (!M_AdjustBlindfireTarget(self, start, target, right, dir))
			return;
	}
	else
		PredictAim(self, self->enemy, start, 0, false, 0.f, &dir, nullptr);

	const bool isBoss = !strcmp(self->classname, "monster_tank_64") || g_hardcoop->integer || self->monsterinfo.IS_BOSS;

	if (isBoss) {
		PredictAim(self, self->enemy, bullet_start, 0, false, 0.075f, &dir, nullptr);
		const vec3_t end = bullet_start + (dir * 8192);
		const trace_t tr = gi.traceline(bullet_start, end, self, MASK_PROJECTILE | CONTENTS_SLIME | CONTENTS_LAVA);

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_LIGHTNING);
		gi.WriteEntity(self);
		gi.WriteEntity(world);
		gi.WritePosition(bullet_start);
		gi.WritePosition(tr.endpos);
		gi.multicast(bullet_start, MULTICAST_PVS, false);

		fire_bullet(self, bullet_start, dir, irandom(8, 12), 15, 0, 0, MOD_TESLA);
	}
	else
		monster_fire_blaster2(self, start, dir, 30, 950, flash_number, EF_BLASTER);
}

void TankStrike(edict_t* self)
{
		gi.sound(self, CHAN_WEAPON, sound_strike, 1, ATTN_NORM, 0);

		{
			gi.sound(self, CHAN_WEAPON, sound_strike, 1, ATTN_NORM, 0);


			// Efecto visual similar al berserker
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_BERSERK_SLAM);

			vec3_t f, r, start;
			AngleVectors(self->s.angles, f, r, nullptr);
			start = M_ProjectFlashSource(self, { 20.f, -14.3f, -21.f }, f, r);
			const trace_t tr = gi.traceline(self->s.origin, start, self, MASK_SOLID);

			gi.WritePosition(tr.endpos);
			gi.WriteDir({ 0.f, 0.f, 1.f });
			gi.multicast(tr.endpos, MULTICAST_PHS, false);
			void T_SlamRadiusDamage(vec3_t point, edict_t * inflictor, edict_t * attacker, float damage, float kick, edict_t * ignore, float radius, mod_t mod);
			// Daño radial
			T_SlamRadiusDamage(tr.endpos, self, self, self->monsterinfo.IS_BOSS ? 150 : 75, 450.f, self, 165, MOD_TANK_PUNCH);

		}
}


mframe_t tank_frames_punch[] =
{
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, TankStrike},  // FRAME_attak225 - Añadir footstep aquí
	{ai_charge, -1, nullptr},
	{ai_charge, -1, nullptr},
	{ai_charge, -2, nullptr},   // FRAME_attak229
	{ai_charge, -2, nullptr},   // FRAME_attak229
	{ai_charge, -2, nullptr},   // FRAME_attak229
	{ai_charge, -2, nullptr},   // FRAME_attak229
	{ai_charge, -2, nullptr},   // FRAME_attak229
	{ai_charge, -2, nullptr},   // FRAME_attak229
	{ai_charge, -2, nullptr}   // FRAME_attak229
};
MMOVE_T(tank_move_punch) = { FRAME_attak224, FRAME_attak235, tank_frames_punch, tank_run };



void TankRocket(edict_t* self)
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

	const bool blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;

	if (self->s.frame == FRAME_attak322 || (self->s.frame == FRAME_attak324))
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
	else if (self->spawnflags.has(SPAWNFLAG_TANK_COMMANDER_GUARDIAN))
		rocketSpeed = 600;
	else
		rocketSpeed = 480;

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
	//======//

	dir.normalize();

	// pmm blindfire doesn't check target (done in checkattack)
	// paranoia, make sure we're not shooting a target right next to us
	if (blindfire)
	{
		// blindfire has different fail criteria for the trace
		if (M_AdjustBlindfireTarget(self, start, vec, right, dir))
		{
			if (self->spawnflags.has(SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING))
				monster_fire_heat(self, start, dir, 50, rocketSpeed, flash_number, self->accel);
			else
				monster_fire_rocket(self, start, dir, 50, (!strcmp(self->classname, "monster_tank_commander")) ? rocketSpeed * 1.5f : rocketSpeed, flash_number);
		}
	}
	else
	{
		const trace_t trace = gi.traceline(start, vec, self, MASK_PROJECTILE);

		if (trace.fraction > 0.5f || trace.ent->solid != SOLID_BSP)
		{
			if (self->spawnflags.has(SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING))
				monster_fire_heat(self, start, dir, 50, rocketSpeed, flash_number, self->accel);
			else
				monster_fire_rocket(self, start, dir, 50, (!strcmp(self->classname, "monster_tank_commander")) ? rocketSpeed * 1.5f : rocketSpeed, flash_number);
		}
	}
}

void TankMachineGun(edict_t* self)
{
	if (!self->enemy || !self->enemy->inuse)
		return;

	// Aumenta velocidad de giro
	self->yaw_speed = 45; // Valor original era más bajo

	vec3_t dir = self->enemy->s.origin - self->s.origin;
	self->ideal_yaw = vectoyaw(dir);
	M_ChangeYaw(self);

	// Resto del código igual...
	monster_muzzleflash_id_t flash_number = static_cast<monster_muzzleflash_id_t>(MZ2_TANK_MACHINEGUN_1 + (self->s.frame - FRAME_attak406));
	vec3_t forward, right;
	AngleVectors(self->s.angles, forward, right, nullptr);
	vec3_t start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	if (self->enemy) {
		vec3_t vec = self->enemy->s.origin;
		vec[2] += self->enemy->viewheight;
		vec -= start;
		vec = vectoangles(vec);
		dir[0] = vec[0];
	}
	else {
		dir[0] = 0;
	}

	if (self->s.frame <= FRAME_attak415)
		dir[1] = self->s.angles[1] - 8 * (self->s.frame - FRAME_attak411);
	else
		dir[1] = self->s.angles[1] + 8 * (self->s.frame - FRAME_attak419);
	dir[2] = 0;

	AngleVectors(dir, forward, nullptr, nullptr);

	if (!strcmp(self->classname, "monster_tank_commander") || self->spawnflags.has(SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING)) {
		monster_fire_flechette(self, start, forward, 20,
			self->monsterinfo.IS_BOSS ? 1150 : 700,
			flash_number);

		vec3_t right_offset;
		AngleVectors(vectoangles(forward), nullptr, &right_offset, nullptr);
		vec3_t forward_right = forward + (right_offset * 0.05f);
		forward_right.normalize();
		monster_fire_flechette(self, start, forward_right, 20,
			self->monsterinfo.IS_BOSS ? 1150 : 700,
			flash_number);
	}
	else {
		monster_fire_bullet(self, start, forward, 20, 8,
			DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD,
			flash_number);
	}
}

static void tank_blind_check(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		const vec3_t aim = self->monsterinfo.blind_fire_target - self->s.origin;
		self->ideal_yaw = vectoyaw(aim);
	}
}

mframe_t tank_frames_attack_grenade[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, -1 },
	{ ai_charge, -2 },
	{ ai_charge, -1 },
	{ ai_charge, -1, tank_blind_check },
	{ ai_charge },
	{ ai_charge, 0, TankGrenades }, // 10
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, TankGrenades },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, TankGrenades } // 16
};
MMOVE_T(tank_move_attack_grenade) = { FRAME_attak101, FRAME_attak116, tank_frames_attack_grenade, tank_reattack_grenades };



mframe_t tank_frames_attack_blast[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, -1 },
	{ ai_charge, -2 },
	{ ai_charge, -1 },
	{ ai_charge, -1, tank_blind_check },
	{ ai_charge },
	{ ai_charge, 0, TankBlaster }, // 10
	{ ai_charge, 0, TankBlaster },
	{ ai_charge, 0, TankBlaster },
	{ ai_charge, 0, TankBlaster },
	{ ai_charge, 0, TankBlaster },
	{ ai_charge, 0, TankBlaster },
	{ ai_charge, 0, TankBlaster } // 16
};
MMOVE_T(tank_move_attack_blast) = { FRAME_attak101, FRAME_attak116, tank_frames_attack_blast, tank_reattack_blaster };

mframe_t tank_frames_reattack_blast[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, TankBlaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, TankBlaster } // 16
};
MMOVE_T(tank_move_reattack_blast) = { FRAME_attak111, FRAME_attak116, tank_frames_reattack_blast, tank_reattack_blaster };

mframe_t tank_frames_reattack_grenade[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, TankGrenades },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, TankGrenades } // 16
};
MMOVE_T(tank_move_reattack_grenade) = { FRAME_attak111, FRAME_attak116, tank_frames_reattack_grenade, tank_reattack_grenades };

mframe_t tank_frames_attack_post_blast[] = {
	{ ai_move }, // 17
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 3 },
	{ ai_move, 2 },
	{ ai_move, -2, tank_footstep } // 22
};
MMOVE_T(tank_move_attack_post_blast) = { FRAME_attak117, FRAME_attak122, tank_frames_attack_post_blast, tank_run };

void tank_reattack_blaster(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		M_SetAnimation(self, &tank_move_attack_post_blast);
		return;
	}

	if (visible(self, self->enemy))
		if (self->enemy->health > 0)
			if (frandom() <= 0.6f)
			{
				M_SetAnimation(self, &tank_move_reattack_blast);
				return;
			}
	M_SetAnimation(self, &tank_move_attack_post_blast);
}

void tank_reattack_grenades(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		M_SetAnimation(self, &tank_move_attack_post_blast);
		return;
	}

	if (visible(self, self->enemy))
		if (self->enemy->health > 0)
			if (frandom() <= 0.75f)
			{
				M_SetAnimation(self, &tank_move_reattack_grenade);
				return;
			}
	M_SetAnimation(self, &tank_move_attack_post_blast);
}

void tank_poststrike(edict_t* self)
{
	self->enemy = nullptr;
	// [Paril-KEX]
	self->monsterinfo.pausetime = HOLD_FOREVER;
	self->monsterinfo.stand(self);
}

mframe_t tank_frames_attack_strike[] = {
	{ ai_move, 3 },
	{ ai_move, 2 },
	{ ai_move, 2 },
	{ ai_move, 1 },
	{ ai_move, 6 },
	{ ai_move, 7 },
	{ ai_move, 9, tank_footstep },
	{ ai_move, 2 },
	{ ai_move, 1 },
	{ ai_move, 2 },
	{ ai_move, 2, tank_footstep },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -2 },
	{ ai_move, -2 },
	{ ai_move, 0, tank_windup },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, TankStrike },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -3 },
	{ ai_move, -10 },
	{ ai_move, -10 },
	{ ai_move, -2 },
	{ ai_move, -3 },
	{ ai_move, -2, tank_footstep }
};
MMOVE_T(tank_move_attack_strike) = { FRAME_attak201, FRAME_attak238, tank_frames_attack_strike, tank_poststrike };

mframe_t tank_frames_attack_pre_rocket[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, // 10

	{ ai_charge },
	{ ai_charge, 1 },
	{ ai_charge, 2 },
	{ ai_charge, 7 },
	{ ai_charge, 7 },
	{ ai_charge, 7, tank_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, // 20

	{ ai_charge, -3 }
};
MMOVE_T(tank_move_attack_pre_rocket) = { FRAME_attak301, FRAME_attak321, tank_frames_attack_pre_rocket, tank_doattack_rocket };

mframe_t tank_frames_attack_fire_rocket[] = {
	{ ai_charge, -3, tank_blind_check }, // Loop Start	22
	{ ai_charge },
	{ ai_charge, 0, TankRocket }, // 24
	{ ai_charge, 0, TankRocket },
	{ ai_charge },
	{ ai_charge, 0, TankRocket },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, -1, TankRocket } // 30	Loop End
};
MMOVE_T(tank_move_attack_fire_rocket) = { FRAME_attak322, FRAME_attak330, tank_frames_attack_fire_rocket, tank_refire_rocket };

mframe_t tank_frames_attack_post_rocket[] = {
	{ ai_charge }, // 31
	{ ai_charge, -1 },
	{ ai_charge, -1 },
	{ ai_charge },
	{ ai_charge, 2 },
	{ ai_charge, 3 },
	{ ai_charge, 4 },
	{ ai_charge, 2 },
	{ ai_charge },
	{ ai_charge }, // 40

	{ ai_charge },
	{ ai_charge, -9 },
	{ ai_charge, -8 },
	{ ai_charge, -7 },
	{ ai_charge, -1 },
	{ ai_charge, -1, tank_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, // 50

	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(tank_move_attack_post_rocket) = { FRAME_attak331, FRAME_attak353, tank_frames_attack_post_rocket, tank_run };

mframe_t tank_frames_attack_chain[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ nullptr, 0, TankMachineGun },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(tank_move_attack_chain) = { FRAME_attak401, FRAME_attak429, tank_frames_attack_chain, tank_run };

void tank_refire_rocket(edict_t* self)
{
	// PMM - blindfire cleanup
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		M_SetAnimation(self, &tank_move_attack_post_rocket);
		return;
	}
	// pmm

	if (self->enemy->health > 0)
		if (visible(self, self->enemy))
			if (frandom() <= 0.4f)
			{
				M_SetAnimation(self, &tank_move_attack_fire_rocket);
				return;
			}
	M_SetAnimation(self, &tank_move_attack_post_rocket);
}

void tank_doattack_rocket(edict_t* self)
{
	M_SetAnimation(self, &tank_move_attack_fire_rocket);
}

MONSTERINFO_ATTACK(tank_attack) (edict_t* self) -> void
{
	vec3_t vec;
	float  range{};
	float  r;
	// PMM
	float chance;

	// PMM
	if (!self->enemy || !self->enemy->inuse)
		return;

	if (range_to(self, self->enemy) <= RANGE_MELEE * 2)
	{
		// Ataque melee (punch)
		M_SetAnimation(self, &tank_move_punch);
		self->monsterinfo.attack_finished = level.time + 0.2_sec;
		return;
	}
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

		const bool rocket_visible = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_ROCKET_1]);
		const bool blaster_visible = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]);

		if (!rocket_visible && !blaster_visible)
			return;

		const bool use_rocket = (rocket_visible && blaster_visible) ? brandom() : rocket_visible;

		// turn on manual steering to signal both manual steering and blindfire
		self->monsterinfo.aiflags |= AI_MANUAL_STEERING;

		if (use_rocket)
			M_SetAnimation(self, &tank_move_attack_fire_rocket);
		else
		{
			self->s.skinnum == 2 && strcmp(self->enemy->classname, "monster_tank_64") ?
				M_SetAnimation(self, &tank_move_attack_grenade):
				M_SetAnimation(self, &tank_move_attack_blast);
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
		const bool can_machinegun = (!self->enemy->classname || strcmp(self->enemy->classname, "tesla_mine")) && M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_MACHINEGUN_5]);

		if (can_machinegun && r < 0.5f)
			M_SetAnimation(self, &tank_move_attack_chain);
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]))
			self->s.skinnum == 2 && strcmp(self->classname, "monster_tank_64") ?
			M_SetAnimation(self, &tank_move_attack_grenade) :
			M_SetAnimation(self, &tank_move_attack_blast);
	}
	else if (range <= 250)
	{
		const bool can_machinegun = (!self->enemy->classname || strcmp(self->enemy->classname, "tesla_mine")) && M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_MACHINEGUN_5]);

		if (can_machinegun && r < 0.25f)
			M_SetAnimation(self, &tank_move_attack_chain);
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]))
			self->s.skinnum == 2 && strcmp(self->classname, "monster_tank_64") ?
			M_SetAnimation(self, &tank_move_attack_grenade) :
			M_SetAnimation(self, &tank_move_attack_blast);
	}
	else
	{
		const bool can_machinegun = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_MACHINEGUN_5]);
		const bool can_rocket = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_ROCKET_1]);

		if (can_machinegun && r < 0.33f)
			M_SetAnimation(self, &tank_move_attack_chain);
		else if (can_rocket && r < 0.66f)
		{
			M_SetAnimation(self, &tank_move_attack_pre_rocket);
			self->pain_debounce_time = level.time + 5_sec; // no pain for a while
		}
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]))
			self->s.skinnum == 2 && strcmp(self->classname, "monster_tank_64") ?
			M_SetAnimation(self, &tank_move_attack_grenade) :
			M_SetAnimation(self, &tank_move_attack_blast);
	}
}

//
// death
//

void tank_dead(edict_t* self)
{
	self->mins = { -16, -16, -16 };
	self->maxs = { 16, 16, -0 };
	monster_dead(self);
}

static void tank_shrink(edict_t* self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t tank_frames_death1[] = {
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
	{ ai_move, -7, tank_shrink },
	{ ai_move, -15, tank_thud },
	{ ai_move, -5 },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(tank_move_death) = { FRAME_death101, FRAME_death132, tank_frames_death1, tank_dead };

DIE(tank_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	OnEntityDeath(self);
	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		ThrowGibs(self, damage, {
			{ "models/objects/gibs/sm_meat/tris.md2" },
			{ 3, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
			{ "models/objects/gibs/gear/tris.md2", GIB_METALLIC },
			{ 2, "models/monsters/tank/gibs/foot.md2", GIB_SKINNED | GIB_METALLIC },
			{ 2, "models/monsters/tank/gibs/thigh.md2", GIB_SKINNED | GIB_METALLIC },
			{ "models/monsters/tank/gibs/chest.md2", GIB_SKINNED },
			{ "models/monsters/tank/gibs/head.md2", GIB_HEAD | GIB_SKINNED }
			});

		if (!self->style)
			ThrowGib(self, "models/monsters/tank/gibs/barm.md2", damage, GIB_SKINNED | GIB_UPRIGHT, self->s.scale);

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

		edict_t* arm_gib = ThrowGib(self, "models/monsters/tank/gibs/barm.md2", damage, GIB_SKINNED | GIB_UPRIGHT, self->s.scale);
		
		if (!arm_gib) {
			return;
		}

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

	M_SetAnimation(self, &tank_move_death);

	if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED) {
		BossDeathHandler(self);

	}
}

//===========
// PGM
MONSTERINFO_BLOCKED(tank_blocked) (edict_t* self, float dist) -> bool
{
	if (blocked_checkplat(self, dist))
		return true;

	return false;
}
// PGM
//===========

//
// monster_tank
//

/*QUAKED monster_tank (1 .5 0) (-32 -32 -16) (32 32 72) Ambush Trigger_Spawn Sight
model="models/monsters/tank/tris.md2"
*/
/*QUAKED monster_tank_commander (1 .5 0) (-32 -32 -16) (32 32 72) Ambush Trigger_Spawn Sight Guardian HeatSeeking
 */
void SP_monster_tank(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (g_horde->integer) {
		{
			if (brandom())
				gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_NORM, 0);
			else
				NULL;
		}

	}

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	self->s.modelindex = gi.modelindex("models/monsters/tank/tris.md2");
	self->mins = { -32, -32, -16 };
	self->maxs = { 32, 32, 64 };
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	gi.modelindex("models/monsters/tank/gibs/barm.md2");
	gi.modelindex("models/monsters/tank/gibs/head.md2");
	gi.modelindex("models/monsters/tank/gibs/chest.md2");
	gi.modelindex("models/monsters/tank/gibs/foot.md2");
	gi.modelindex("models/monsters/tank/gibs/thigh.md2");

	sound_thud.assign("tank/tnkdeth2.wav");
	sound_idle.assign("tank/tnkidle1.wav");
	sound_die.assign("tank/death.wav");
	sound_step.assign("tank/step.wav");
	sound_windup.assign("tank/tnkatck4.wav");
	sound_strike.assign("tank/tnkatck5.wav");
	sound_sight.assign("tank/sight1.wav");
	sound_grenade.assign("guncmdr/gcdratck3.wav");

	gi.soundindex("tank/tnkatck1.wav");
	gi.soundindex("tank/tnkatk2a.wav");
	gi.soundindex("tank/tnkatk2b.wav");
	gi.soundindex("tank/tnkatk2c.wav");
	gi.soundindex("tank/tnkatk2d.wav");
	gi.soundindex("tank/tnkatk2e.wav");
	gi.soundindex("tank/tnkatck3.wav");

	if (strcmp(self->classname, "monster_tank_commander") == 0)
	{
		self->health = 1000 * st.health_multiplier;
		self->gib_health = -225;
		self->count = 1;
		sound_pain2.assign("tank/pain.wav");
	}
	else
	{
		self->health = 630 * st.health_multiplier;
		self->gib_health = -200;
		sound_pain.assign("tank/tnkpain2.wav");
	}

	self->monsterinfo.scale = MODEL_SCALE;

	// [Paril-KEX] N64 tank commander is a chonky boy
	if (!strcmp(self->classname, "monster_tank_64"))
	{
		self->accel = 0.075f;
		if (g_horde->integer) {
			if (!self->s.scale)
				self->s.scale = 1.25f;

			self->health = 1750 + (1.005 * current_wave_level);
			if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED) {
				self->gib_health = -999777;
				self->health *= 2.3;

			}
		}
		if (G_IsCooperative()) {
			self->s.scale = 1.1f;
			self->health = 1000;
		}

		self->gib_health = -250;

		if (self->monsterinfo.bonus_flags & BF_BERSERKING)
			self->accel *= 0.1f;
	}


	self->mass = 500;

	self->pain = tank_pain;
	self->die = tank_die;
	self->monsterinfo.stand = tank_stand;
	self->monsterinfo.walk = tank_walk;
	self->monsterinfo.run = tank_run;
	self->monsterinfo.dodge = nullptr;
	self->monsterinfo.attack = tank_attack;
	self->monsterinfo.melee = nullptr;
	self->monsterinfo.sight = tank_sight;
	self->monsterinfo.idle = tank_idle;
	self->monsterinfo.blocked = tank_blocked; // PGM
	self->monsterinfo.setskin = tank_setskin;

	gi.linkentity(self);

	M_SetAnimation(self, &tank_move_stand);

	walkmonster_start(self);

	// PMM
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	self->monsterinfo.blindfire = true;
	// pmm
	if (strcmp(self->classname, "monster_tank_commander") == 0)
		self->s.skinnum = 2;

	ApplyMonsterBonusFlags(self);
}

void Use_Boss3(edict_t* ent, edict_t* other, edict_t* activator);

THINK(Think_TankStand) (edict_t* ent) -> void
{
	if (ent->s.frame == FRAME_stand30)
		ent->s.frame = FRAME_stand01;
	else
		ent->s.frame++;
	ent->nextthink = level.time + 10_hz;
}

/*QUAKED monster_tank_stand (1 .5 0) (-32 -32 0) (32 32 90)

Just stands and cycles in one place until targeted, then teleports away.
N64 edition!
*/
void SP_monster_tank_stand(edict_t* self)
{
	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->model = "models/monsters/tank/tris.md2";
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
	self->think = Think_TankStand;
	self->nextthink = level.time + 10_hz;
	gi.linkentity(self);
}

void SP_monster_tank_64(edict_t* self)
{
	brandom() ?
	self->spawnflags |= SPAWNFLAG_TANK_COMMANDER_GUARDIAN :
	self->spawnflags |= SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING;
	SP_monster_tank(self);
	self->s.skinnum = 2;

	ApplyMonsterBonusFlags(self);
}