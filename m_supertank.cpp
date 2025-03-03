// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

SUPERTANK

==============================================================================
*/

#include "g_local.h"
#include "m_supertank.h"
#include "m_flash.h"
#include "shared.h"

constexpr spawnflags_t SPAWNFLAG_SUPERTANK_POWERSHIELD = 8_spawnflag;
// n64
constexpr spawnflags_t SPAWNFLAG_SUPERTANK_LONG_DEATH = 16_spawnflag;

static cached_soundindex sound_pain1;
static cached_soundindex sound_pain2;
static cached_soundindex sound_pain3;
static cached_soundindex sound_death;
static cached_soundindex sound_search1;
static cached_soundindex sound_search2;

static cached_soundindex tread_sound;

void TreadSound(edict_t* self)
{
	gi.sound(self, CHAN_BODY, tread_sound, 1, ATTN_NORM, 0);
}

MONSTERINFO_SEARCH(supertank_search) (edict_t* self) -> void
{
	if (frandom() < 0.5f)
		gi.sound(self, CHAN_VOICE, sound_search1, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_search2, 1, ATTN_NORM, 0);
}

void supertank_dead(edict_t* self);
void supertankRocket(edict_t* self);
void supertankMachineGun(edict_t* self);
void supertank_reattack1(edict_t* self);

//
// stand
//

mframe_t supertank_frames_stand[] = {
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
MMOVE_T(supertank_move_stand) = { FRAME_stand_1, FRAME_stand_60, supertank_frames_stand, nullptr };

MONSTERINFO_STAND(supertank_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &supertank_move_stand);
}

mframe_t supertank_frames_run[] = {
	{ ai_run, 18, TreadSound },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 }
};
MMOVE_T(supertank_move_run) = { FRAME_forwrd_1, FRAME_forwrd_18, supertank_frames_run, nullptr };

//
// walk
//

mframe_t supertank_frames_forward[] = {
	{ ai_walk, 4, TreadSound },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 }
};
MMOVE_T(supertank_move_forward) = { FRAME_forwrd_1, FRAME_forwrd_18, supertank_frames_forward, nullptr };

void supertank_forward(edict_t* self)
{
	M_SetAnimation(self, &supertank_move_forward);
}

MONSTERINFO_WALK(supertank_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &supertank_move_forward);
}

MONSTERINFO_RUN(supertank_run) (edict_t* self) -> void
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &supertank_move_stand);
	else
		M_SetAnimation(self, &supertank_move_run);
}

#if 0
mframe_t supertank_frames_turn_right[] = {
	{ ai_move, 0, TreadSound },
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
MMOVE_T(supertank_move_turn_right) = { FRAME_right_1, FRAME_right_18, supertank_frames_turn_right, supertank_run };

mframe_t supertank_frames_turn_left[] = {
	{ ai_move, 0, TreadSound },
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
MMOVE_T(supertank_move_turn_left) = { FRAME_left_1, FRAME_left_18, supertank_frames_turn_left, supertank_run };
#endif

mframe_t supertank_frames_pain3[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(supertank_move_pain3) = { FRAME_pain3_9, FRAME_pain3_12, supertank_frames_pain3, supertank_run };

mframe_t supertank_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(supertank_move_pain2) = { FRAME_pain2_5, FRAME_pain2_8, supertank_frames_pain2, supertank_run };

mframe_t supertank_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(supertank_move_pain1) = { FRAME_pain1_1, FRAME_pain1_4, supertank_frames_pain1, supertank_run };

static void BossLoop(edict_t* self)
{
	if (!(self->spawnflags & SPAWNFLAG_SUPERTANK_LONG_DEATH))
		return;

	if (self->count)
		self->count--;
	else
		self->spawnflags &= ~SPAWNFLAG_SUPERTANK_LONG_DEATH;

	self->monsterinfo.nextframe = FRAME_death_19;
}

static void supertankGrenade(edict_t* self)
{
	vec3_t forward, right;
	vec3_t start;
	monster_muzzleflash_id_t flash_number;

	if (!self->enemy || !self->enemy->inuse)
		return;

	if (self->s.frame == FRAME_attak4_1)
		flash_number = MZ2_SUPERTANK_GRENADE_1;
	else
		flash_number = MZ2_SUPERTANK_GRENADE_2;

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	vec3_t aim_point;
	PredictAim(self, self->enemy, start, 0, false, crandom_open() * 0.1f, &forward, &aim_point);

	// Usar un entero como contador y convertir a float cuando sea necesario
	for (int i = 0; i < 5; i++) // 5 iteraciones para cubrir el rango 500-900
	{
		// Ajustar la velocidad y trayectoria para teslas
		if (!strcmp(self->enemy->classname, "tesla_mine")) {
			float const speed = 400.0f;
			vec3_t target = self->enemy->s.origin;

			// Si la tesla está en el suelo, ajustar el punto de impacto
			if (self->enemy->movedir[2] > 0.7) {
				vec3_t forward;
				AngleVectors(self->s.angles, forward, nullptr, nullptr);
				// En lugar de VectorMA usamos la suma de vectores
				target = target - (forward * 32); // Apuntar 32 unidades antes de la tesla
				target[2] += 8; // Altura ajustada para mejor rebote
			}

			vec3_t dir = target - start;
			dir.normalize();

			monster_fire_grenade(self, start, dir, 50, speed, flash_number, 0.f, 2.f);
			return;
		}
		else {
			float const speed = 500.0f + (i * 100.0f);

			if (!M_CalculatePitchToFire(self, aim_point, start, forward, speed, 2.5f, true))
				continue;

			monster_fire_grenade(self, start, forward, 50, speed, flash_number, 0.f, 0.f);
			break;
		}
	}
}

mframe_t supertank_frames_death1boss[] = {
	{ ai_move, 0, BossExplode },
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
	{ ai_move, 0, BossLoop }
};
MMOVE_T(supertank_move_deathboss) = { FRAME_death_1, FRAME_death_24, supertank_frames_death1boss, supertank_dead };


mframe_t supertank_frames_death1[] = {
	{ ai_move, 0, BossExplode },
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
MMOVE_T(supertank_move_death) = { FRAME_death_1, FRAME_death_10, supertank_frames_death1, supertank_dead };

mframe_t supertank_frames_attack4[] = {
	{ ai_move, 0, supertankGrenade },
	{ ai_move, 0, supertankGrenade },
	{ ai_move, 0, supertankGrenade },
	{ ai_move, 0, supertankGrenade },
	{ ai_move, 0, supertankGrenade },
	{ ai_move }
};
MMOVE_T(supertank_move_attack4) = { FRAME_attak4_1, FRAME_attak4_6, supertank_frames_attack4, supertank_run };

mframe_t supertank_frames_attack2[] = {
	{ ai_charge, 0, supertankRocket },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge },
	{ ai_charge, 0, supertankRocket },
	{ ai_move },
	{ ai_move },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge, 0, supertankRocket },
	{ ai_move }
};
MMOVE_T(supertank_move_attack2) = { FRAME_attak2_1, FRAME_attak2_27, supertank_frames_attack2, supertank_run };

mframe_t supertank_frames_attack1[] = {
	{ ai_charge, 0, supertankMachineGun },
	{ ai_charge, 0, supertankMachineGun },
	{ ai_charge, 0, supertankMachineGun },
	{ ai_charge, 0, supertankMachineGun },
	{ ai_charge, 0, supertankMachineGun },
	{ ai_charge, 0, supertankMachineGun },
};
MMOVE_T(supertank_move_attack1) = { FRAME_attak1_1, FRAME_attak1_6, supertank_frames_attack1, supertank_reattack1 };

mframe_t supertank_frames_end_attack1[] = {
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
MMOVE_T(supertank_move_end_attack1) = { FRAME_attak1_7, FRAME_attak1_20, supertank_frames_end_attack1, supertank_run };

void supertank_reattack1(edict_t* self)
{
	if (visible(self, self->enemy))
	{
		if (self->timestamp >= level.time || frandom() < 0.3f)
			M_SetAnimation(self, &supertank_move_attack1);
		else
			M_SetAnimation(self, &supertank_move_end_attack1);
	}
	else
		M_SetAnimation(self, &supertank_move_end_attack1);
}

PAIN(supertank_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (level.time < self->pain_debounce_time)
		return;

	// Lessen the chance of him going into his pain frames
	if (mod.id != MOD_CHAINFIST)
	{
		if (damage <= 25)
			if (frandom() < 0.2f)
				return;

		// Don't go into pain if he's firing his rockets
		if ((self->s.frame >= FRAME_attak2_1) && (self->s.frame <= FRAME_attak2_14))
			return;
	}

	if (damage <= 10)
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else if (damage <= 25)
		gi.sound(self, CHAN_VOICE, sound_pain3, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	self->pain_debounce_time = level.time + 3_sec;

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	if (damage <= 10)
		M_SetAnimation(self, &supertank_move_pain1);
	else if (damage <= 25)
		M_SetAnimation(self, &supertank_move_pain2);
	else
		M_SetAnimation(self, &supertank_move_pain3);
}

MONSTERINFO_SETSKIN(supertank_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

void supertankRocket(edict_t* self)
{
	vec3_t					 forward, right;
	vec3_t					 start;
	vec3_t					 dir;
	vec3_t					 vec;
	monster_muzzleflash_id_t flash_number;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	if (self->s.frame == FRAME_attak2_8)
		flash_number = MZ2_SUPERTANK_ROCKET_1;
	else if (self->s.frame == FRAME_attak2_11)
		flash_number = MZ2_SUPERTANK_ROCKET_2;
	else // (self->s.frame == FRAME_attak2_14)
		flash_number = MZ2_SUPERTANK_ROCKET_3;

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	if (self->monsterinfo.IS_BOSS)
	{
		vec = self->enemy->s.origin;
		vec[2] += self->enemy->viewheight;
		dir = vec - start;
		dir.normalize();
		monster_fire_heat(self, start, dir, 40, 980, flash_number, 0.075f);
	}
	else
	{
		PredictAim(self, self->enemy, start, 750, false, 0.f, &forward, nullptr);
		monster_fire_rocket(self, start, forward, 50, 1100, flash_number);
	}
}

void supertankMachineGun(edict_t* self)
{
	vec3_t					 dir;
	vec3_t					 start;
	vec3_t					 forward, right;
	monster_muzzleflash_id_t flash_number;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	flash_number = static_cast<monster_muzzleflash_id_t>(MZ2_SUPERTANK_MACHINEGUN_1 + (self->s.frame - FRAME_attak1_1));

	dir[0] = 0;
	dir[1] = self->s.angles[1];
	dir[2] = 0;

	AngleVectors(dir, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);
	PredictAim(self, self->enemy, start, 0, true, -0.1f, &forward, nullptr);
	monster_fire_bullet(self, start, forward, 6, 4, DEFAULT_BULLET_HSPREAD * 3, DEFAULT_BULLET_VSPREAD * 3, flash_number);
}

MONSTERINFO_ATTACK(supertank_attack) (edict_t* self) -> void
{
	vec3_t vec;
	float  range;

	vec = self->enemy->s.origin - self->s.origin;
	range = range_to(self, self->enemy);

	// Attack 1 == Chaingun
	// Attack 2 == Rocket Launcher
	// Attack 3 == Grenade Launcher
	const bool chaingun_good = M_CheckClearShot(self, monster_flash_offset[MZ2_SUPERTANK_MACHINEGUN_1]);
	const bool rocket_good = M_CheckClearShot(self, monster_flash_offset[MZ2_SUPERTANK_ROCKET_1]);
	const bool grenade_good = M_CheckClearShot(self, monster_flash_offset[MZ2_SUPERTANK_GRENADE_1]);
	const bool isTesla = (!strcmp(self->enemy->classname, "tesla_mine"));

	if (isTesla) {
		// Si la tesla está en una pared o techo (normal no apunta hacia arriba)
		if (vec.z > 8) {
			if (rocket_good)
				M_SetAnimation(self, &supertank_move_attack2);
		}
		else {
			// Tesla en el suelo, usar granadas
			if (grenade_good)
				M_SetAnimation(self, &supertank_move_attack4);
			else if (rocket_good) // Fallback a cohetes si no podemos usar granadas
				M_SetAnimation(self, &supertank_move_attack2);
		}
		return;
	}

	// fire rockets more often at distance
	if (chaingun_good && (!rocket_good || range <= 540 || frandom() < 0.3f))
	{
		// prefer grenade if the enemy is above us
		if (grenade_good && (range >= 350 || vec.z > 120.f || frandom() < 0.2f))
			M_SetAnimation(self, &supertank_move_attack4);
		else
		{
			M_SetAnimation(self, &supertank_move_attack1);
			self->timestamp = level.time + random_time(1500_ms, 2700_ms);
		}
	}
	else if (rocket_good)
	{
		// prefer grenade if the enemy is above us
		if (grenade_good && (vec.z > 120.f || frandom() < 0.2f))
			M_SetAnimation(self, &supertank_move_attack4);
		else
			M_SetAnimation(self, &supertank_move_attack2);
	}
	else if (grenade_good)
		M_SetAnimation(self, &supertank_move_attack4);
}

//
// death
//

static void supertank_gib(edict_t* self)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_EXPLOSION1_BIG);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PHS, false);

	self->s.sound = 0;
	self->s.skinnum /= 2;

	ThrowGibs(self, 500, {
		{ 2, "models/objects/gibs/sm_meat/tris.md2" },
		{ 2, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
		{ "models/monsters/boss1/gibs/cgun.md2", GIB_SKINNED | GIB_METALLIC },
		{ "models/monsters/boss1/gibs/chest.md2", GIB_SKINNED },
		{ "models/monsters/boss1/gibs/core.md2", GIB_SKINNED },
		{ "models/monsters/boss1/gibs/ltread.md2", GIB_SKINNED | GIB_UPRIGHT },
		{ "models/monsters/boss1/gibs/rgun.md2", GIB_SKINNED | GIB_UPRIGHT },
		{ "models/monsters/boss1/gibs/rtread.md2", GIB_SKINNED | GIB_UPRIGHT },
		{ "models/monsters/boss1/gibs/tube.md2", GIB_SKINNED | GIB_UPRIGHT },
		{ "models/monsters/boss1/gibs/head.md2", GIB_SKINNED | GIB_METALLIC | GIB_HEAD }
		});
}

void supertank_dead(edict_t* self)
{
	// no blowy on deady
	if (self->spawnflags.has(SPAWNFLAG_MONSTER_DEAD))
	{
		self->deadflag = false;
		self->takedamage = true;
		return;
	}

	supertank_gib(self);
}

DIE(supertank_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	if (self->spawnflags.has(SPAWNFLAG_MONSTER_DEAD))
	{
		// check for gib
		if (M_CheckGib(self, mod))
		{
			supertank_gib(self);
			self->deadflag = true;
			return;
		}

		if (self->deadflag)
			return;
	}
	else
	{
		gi.sound(self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
		self->deadflag = true;
		self->takedamage = false;
	}
	g_horde->integer && self->monsterinfo.IS_BOSS ?
		M_SetAnimation(self, &supertank_move_deathboss) :
		M_SetAnimation(self, &supertank_move_death);
}

//===========
// PGM
MONSTERINFO_BLOCKED(supertank_blocked) (edict_t* self, float dist) -> bool
{
	if (blocked_checkplat(self, dist))
		return true;

	return false;
}
// PGM
//===========

//
// monster_supertank
//

// RAFAEL (Powershield)

/*QUAKED monster_supertank (1 .5 0) (-64 -64 0) (64 64 72) Ambush Trigger_Spawn Sight Powershield LongDeath
 */
void SP_monster_supertank(edict_t* self)
{

	const spawn_temp_t& st = ED_GetSpawnTemp();
	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	if (g_horde->integer && current_wave_level <= 18) {
		const float randomChance = frandom();

		if (self->monsterinfo.IS_BOSS) {
			const int soundToPlay = (randomChance < 0.5f) ? sound_search2 : sound_search1;
			gi.sound(self, CHAN_VOICE, soundToPlay, 1, ATTN_NONE, 0);
			self->health = 3300 * st.health_multiplier;
		}
		else {
			constexpr float FIRST_SOUND_CHANCE = 0.2f;
			constexpr float SECOND_SOUND_CHANCE = 0.4f;

			const int soundToPlay = (randomChance < FIRST_SOUND_CHANCE)
				? sound_search2
				: (randomChance < SECOND_SOUND_CHANCE)
				? sound_search1
				: 0;

			if (soundToPlay) {
				gi.sound(self, CHAN_VOICE, soundToPlay, 1, ATTN_NORM, 0);
			}
		}
	}

	sound_pain1.assign("bosstank/btkpain1.wav");
	sound_pain2.assign("bosstank/btkpain2.wav");
	sound_pain3.assign("bosstank/btkpain3.wav");
	sound_death.assign("bosstank/btkdeth1.wav");
	sound_search1.assign("bosstank/btkunqv1.wav");
	sound_search2.assign("bosstank/btkunqv2.wav");

	tread_sound.assign("bosstank/btkengn1.wav");

	gi.soundindex("gunner/gunatck3.wav");
	gi.soundindex("infantry/infatck1.wav");
	gi.soundindex("tank/rocket.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/boss1/tris.md2");

	gi.modelindex("models/monsters/boss1/gibs/cgun.md2");
	gi.modelindex("models/monsters/boss1/gibs/chest.md2");
	gi.modelindex("models/monsters/boss1/gibs/core.md2");
	gi.modelindex("models/monsters/boss1/gibs/head.md2");
	gi.modelindex("models/monsters/boss1/gibs/ltread.md2");
	gi.modelindex("models/monsters/boss1/gibs/rgun.md2");
	gi.modelindex("models/monsters/boss1/gibs/rtread.md2");
	gi.modelindex("models/monsters/boss1/gibs/tube.md2");

	self->mins = { -64, -64, 0 };
	self->maxs = { 64, 64, 112 };

	if (g_horde->integer)
	self->health = 3300 * st.health_multiplier;

	if (!g_horde->integer)
	self->health = 2000 * st.health_multiplier;

	self->gib_health = -500;
	self->mass = 800;

	self->pain = supertank_pain;
	self->die = supertank_die;
	self->monsterinfo.stand = supertank_stand;
	self->monsterinfo.walk = supertank_walk;
	self->monsterinfo.run = supertank_run;
	self->monsterinfo.dodge = nullptr;
	self->monsterinfo.attack = supertank_attack;
	self->monsterinfo.search = supertank_search;
	self->monsterinfo.melee = nullptr;
	self->monsterinfo.sight = nullptr;
	self->monsterinfo.blocked = supertank_blocked; // PGM
	self->monsterinfo.setskin = supertank_setskin;

	gi.linkentity(self);

	M_SetAnimation(self, &supertank_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	// RAFAEL
	if (!st.was_key_specified("power_armor_type"))
		self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
	if (!st.was_key_specified("power_armor_power"))
		self->monsterinfo.power_armor_power = 1300;

	if (self->spawnflags.has(SPAWNFLAG_SUPERTANK_POWERSHIELD))
	{
		if (!st.was_key_specified("power_armor_type"))
			self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = 3100;
	}
	// RAFAEL

	walkmonster_start(self);

	// PMM
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	// pmm

	// TODO
	if (level.is_n64)
	{
		self->spawnflags |= SPAWNFLAG_SUPERTANK_LONG_DEATH;
		self->count = 10;
	}

	ApplyMonsterBonusFlags(self);
}

//
// monster_boss5
// RAFAEL
//

/*QUAKED monster_boss5 (1 .5 0) (-64 -64 0) (64 64 72) Ambush Trigger_Spawn Sight
 */
void SP_monster_boss5(edict_t* self)
{
	self->spawnflags |= SPAWNFLAG_SUPERTANK_POWERSHIELD;
	SP_monster_supertank(self);
	gi.soundindex("weapons/railgr1a.wav");
	self->s.skinnum = 2;
	self->health = 5600 + (1.08 * current_wave_level);
	ApplyMonsterBonusFlags(self);
}

void SP_monster_janitor(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	self->spawnflags |= SPAWNFLAG_SUPERTANK_POWERSHIELD;
	self->count = 10;
	SP_monster_supertank(self);
	gi.soundindex("weapons/railgr1a.wav");
	self->s.skinnum = 2;

	self->s.scale = 0.6f;
	self->mins *= 0.6f;
	self->maxs *= 0.6f;

	if (!st.was_key_specified("power_armor_type"))
		self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
	if (!st.was_key_specified("power_armor_power"))
		self->monsterinfo.power_armor_power = 800;
	if (!strcmp(self->classname, "monster_janitor")) {
		self->health = 1300 * st.health_multiplier;
	}
	self->mass = 200;

	ApplyMonsterBonusFlags(self);
}

void SP_monster_supertankkl(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (g_horde->integer &&  !strcmp(self->classname, "monster_supertankkl")) {
		self->count = 10;
		SP_monster_supertank(self);
		gi.soundindex("weapons/railgr1a.wav");

		self->health = 675 * current_wave_level;
		self->spawnflags |= SPAWNFLAG_SUPERTANK_POWERSHIELD;
		self->spawnflags |= SPAWNFLAG_SUPERTANK_LONG_DEATH;

		self->health = 5500;
	
	

		if (!st.was_key_specified("power_armor_type"))
			self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = 1800;

		self->mass = 1200;
		self->s.alpha = 0.3f;
		self->s.effects = EF_FLAG1 | EF_QUAD;
	}
	ApplyMonsterBonusFlags(self);
}