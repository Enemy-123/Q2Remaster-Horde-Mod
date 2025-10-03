// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

black widow

==============================================================================
*/

// self->timestamp used to prevent rapid fire of railgun
// self->plat2flags used for fire count (flashes)

#include "../g_local.h"
#include "m_rogue_widow.h"
#include "../m_flash.h"
#include "../shared.h"


bool infront(edict_t* self, edict_t* other);

static cached_soundindex sound_pain1;
static cached_soundindex sound_pain2;
static cached_soundindex sound_pain3;
static cached_soundindex sound_rail;

static uint32_t shotsfired;

constexpr vec3_t spawnpoints[] = {
	{ 30, 100, 16 },
	{ 30, -100, 16 }
};

constexpr vec3_t beameffects[] = {
	{ 12.58f, -43.71f, 68.88f },
	{ 3.43f, 58.72f, 68.41f }
};

constexpr float sweep_angles[] = {
	32.f, 26.f, 20.f, 10.f, 0.f, -6.5f, -13.f, -27.f, -41.f
};

constexpr vec3_t stalker_mins = { -28, -28, -18 };
constexpr vec3_t stalker_maxs = { 28, 28, 18 };

unsigned int widow_damage_multiplier;

void widow_run(edict_t* self);
void widow_dead(edict_t* self);
void widow_attack_blaster(edict_t* self);
void widow_reattack_blaster(edict_t* self);

void widow_start_spawn(edict_t* self);
void widow_done_spawn(edict_t* self);
void widow_spawn_check(edict_t* self);
void widow_attack_rail(edict_t* self);

void widow_start_run_5(edict_t* self);
void widow_start_run_10(edict_t* self);
void widow_start_run_12(edict_t* self);

void WidowCalcSlots(edict_t* self);
// --- JUMP ATTACK ADDED ---
// Forward declaration for the jump dispatcher
void widow_jump(edict_t* self, blocked_jump_result_t result);


// --- IMPROVED: Constants are clearer and balanced ---
constexpr gtime_t RAIL_COOLDOWN = 2.0_sec;
constexpr gtime_t BLASTER_COOLDOWN = 1.0_sec;
constexpr int	WIDOW_BLASTER_DAMAGE = 20;
constexpr int	WIDOW1_BLASTER_DAMAGE = 10;
constexpr int	WIDOW_RAIL_DAMAGE = 90;
constexpr int	WIDOW1_RAIL_DAMAGE = 60;
constexpr int	WIDOW_BLASTER_SPEED = 1000;
constexpr int	WIDOW1_BLASTER_SPEED = 850;


MONSTERINFO_SEARCH(widow_search) (edict_t* self) -> void
{
}

MONSTERINFO_SIGHT(widow_sight) (edict_t* self, edict_t* other) -> void
{
	self->monsterinfo.fire_wait = 0_ms;
}

extern const mmove_t widow_move_attack_post_blaster;
extern const mmove_t widow_move_attack_post_blaster_r;
extern const mmove_t widow_move_attack_post_blaster_l;
extern const mmove_t widow_move_attack_blaster;
extern const mmove_t widow_move_run_attack;

float target_angle(edict_t* self)
{

	if (!M_HasValidTarget(self))
	{
		return 0.0f; // Stop immediately if the target is invalid.
	}


	vec3_t target;
	float  enemy_yaw;

	target = self->s.origin - self->enemy->s.origin;
	enemy_yaw = self->s.angles[YAW] - vectoyaw(target);
	if (enemy_yaw < 0)
		enemy_yaw += 360.0f;

	// this gets me 0 degrees = forward
	enemy_yaw -= 180.0f;
	// positive is to right, negative to left

	return enemy_yaw;
}

int WidowTorso(edict_t* self)
{
	float enemy_yaw = target_angle(self);

	if (enemy_yaw >= 105)
	{
		M_SetAnimation(self, &widow_move_attack_post_blaster_r);
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		return 0;
	}

	if (enemy_yaw <= -75.0f)
	{
		M_SetAnimation(self, &widow_move_attack_post_blaster_l);
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		return 0;
	}

	if (enemy_yaw >= 95)
		return FRAME_fired03;
	else if (enemy_yaw >= 85)
		return FRAME_fired04;
	else if (enemy_yaw >= 75)
		return FRAME_fired05;
	else if (enemy_yaw >= 65)
		return FRAME_fired06;
	else if (enemy_yaw >= 55)
		return FRAME_fired07;
	else if (enemy_yaw >= 45)
		return FRAME_fired08;
	else if (enemy_yaw >= 35)
		return FRAME_fired09;
	else if (enemy_yaw >= 25)
		return FRAME_fired10;
	else if (enemy_yaw >= 15)
		return FRAME_fired11;
	else if (enemy_yaw >= 5)
		return FRAME_fired12;
	else if (enemy_yaw >= -5)
		return FRAME_fired13;
	else if (enemy_yaw >= -15)
		return FRAME_fired14;
	else if (enemy_yaw >= -25)
		return FRAME_fired15;
	else if (enemy_yaw >= -35)
		return FRAME_fired16;
	else if (enemy_yaw >= -45)
		return FRAME_fired17;
	else if (enemy_yaw >= -55)
		return FRAME_fired18;
	else if (enemy_yaw >= -65)
		return FRAME_fired19;
	else if (enemy_yaw >= -75)
		return FRAME_fired20;

	return 0;
}

constexpr float VARIANCE = 15.0f;

void WidowBlaster(edict_t* self)
{
	if (!M_HasValidTarget(self) || !visible(self, self->enemy))
		return;

	vec3_t forward, right, up, target, vec, targ_angles;
	vec3_t start;
	monster_muzzleflash_id_t flashnum;
	effects_t effect;
	const bool is_widow1 = horde::IsMonsterType(self, horde::MonsterTypeID::WIDOW1);

	// Helper to get scaled flash offset for widow1
	auto GetScaledFlashOffset = [&](const vec3_t& original_offset) -> vec3_t {
		return is_widow1 ? original_offset * self->s.scale : original_offset;
	};

	shotsfired++;
	effect = (!(shotsfired % 4)) ? EF_BLASTER : EF_NONE;

	AngleVectors(self->s.angles, forward, right, up);

	if ((self->s.frame >= FRAME_spawn05) && (self->s.frame <= FRAME_spawn13))
	{
		flashnum = static_cast<monster_muzzleflash_id_t>(MZ2_WIDOW_BLASTER_SWEEP1 + self->s.frame - FRAME_spawn05);
		vec3_t scaled_offset = GetScaledFlashOffset(monster_flash_offset[flashnum]);
		start = G_ProjectSource2(self->s.origin, scaled_offset, forward, right, up);

		target = self->enemy->s.origin - start;
		targ_angles = vectoangles(target);
		vec = self->s.angles;
		vec[PITCH] += targ_angles[PITCH];
		vec[YAW] -= sweep_angles[flashnum - MZ2_WIDOW_BLASTER_SWEEP1];
		AngleVectors(vec, forward, nullptr, nullptr);

		int damage = GetMonsterWeaponDamage(self->monsterinfo.monster_type_id, "blaster");
		if (damage <= 0) damage = is_widow1 ? WIDOW1_BLASTER_DAMAGE : WIDOW_BLASTER_DAMAGE;
		if (!is_widow1) damage *= widow_damage_multiplier;
		monster_fire_blaster2(self, start, forward,
			damage,
			is_widow1 ? WIDOW1_BLASTER_SPEED : WIDOW_BLASTER_SPEED,
			flashnum, effect);
	}
	else if ((self->s.frame >= FRAME_fired02a) && (self->s.frame <= FRAME_fired20))
	{
		vec3_t angles;
		float aim_angle, target_angle;
		float error;

		self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
		self->monsterinfo.nextframe = WidowTorso(self);
		if (!self->monsterinfo.nextframe)
			self->monsterinfo.nextframe = self->s.frame;

		flashnum = (self->s.frame == FRAME_fired02a) ?
			MZ2_WIDOW_BLASTER_0 :
			static_cast<monster_muzzleflash_id_t>(MZ2_WIDOW_BLASTER_100 + self->s.frame - FRAME_fired03);

		vec3_t scaled_offset = GetScaledFlashOffset(monster_flash_offset[flashnum]);
		start = G_ProjectSource2(self->s.origin, scaled_offset, forward, right, up);

		PredictAim(self, self->enemy, start, is_widow1 ? WIDOW1_BLASTER_SPEED : WIDOW_BLASTER_SPEED, true, crandom() * 0.1f, &forward, nullptr);

		// clamp it to within 10 degrees of the aiming angle (where she's facing)
		angles = vectoangles(forward);
		aim_angle = static_cast<float>(100 - (10 * (flashnum - MZ2_WIDOW_BLASTER_100)));
		if (aim_angle <= 0)
			aim_angle += 360;
		target_angle = self->s.angles[YAW] - angles[YAW];
		if (target_angle <= 0)
			target_angle += 360;

		error = aim_angle - target_angle;

		if (error > VARIANCE)
		{
			angles[YAW] = (self->s.angles[YAW] - aim_angle) + VARIANCE;
			AngleVectors(angles, forward, nullptr, nullptr);
		}
		else if (error < -VARIANCE)
		{
			angles[YAW] = (self->s.angles[YAW] - aim_angle) - VARIANCE;
			AngleVectors(angles, forward, nullptr, nullptr);
		}

		int damage = GetMonsterWeaponDamage(self->monsterinfo.monster_type_id, "blaster");
		if (damage <= 0) damage = is_widow1 ? WIDOW1_BLASTER_DAMAGE : WIDOW_BLASTER_DAMAGE;
		if (!is_widow1) damage *= widow_damage_multiplier;
		monster_fire_blaster2(self, start, forward,
			damage,
			is_widow1 ? WIDOW1_BLASTER_SPEED : WIDOW_BLASTER_SPEED,
			flashnum, effect);
	}
	// --- FIX: This is the charging attack frame logic ---
	else if ((self->s.frame >= FRAME_run01) && (self->s.frame <= FRAME_run08))
	{
		flashnum = static_cast<monster_muzzleflash_id_t>(MZ2_WIDOW_RUN_1 + self->s.frame - FRAME_run01);
		vec3_t scaled_offset = GetScaledFlashOffset(monster_flash_offset[flashnum]);
		start = G_ProjectSource2(self->s.origin, scaled_offset, forward, right, up);

		target = self->enemy->s.origin - start;
		target[2] += self->enemy->viewheight;
		target.normalize();

		monster_fire_blaster2(self, start, target,
			is_widow1 ? WIDOW1_BLASTER_DAMAGE : WIDOW_BLASTER_DAMAGE * widow_damage_multiplier,
			is_widow1 ? WIDOW1_BLASTER_SPEED : WIDOW_BLASTER_SPEED,
			flashnum, effect);
	}
}

// --- IMPROVED: Refactored for clarity and robustness ---
void WidowSpawn(edict_t* self) {
	if (!self) {
		return;
	}

	// Check global spawner limit for horde mode
	if (g_horde->integer) {
		if (level.global_spawned_count >= level.global_spawner_limit) {
			return; // Don't spawn if we've reached the global limit
		}
	}

	// This check is now also performed in the attack function, but it's good practice here too.
	if (self->monsterinfo.monster_used >= self->monsterinfo.monster_slots) {
		return;
	}

	vec3_t f, r, u, offset, startpoint, spawnpoint;
	AngleVectors(self->s.angles, f, r, u);

	for (int i = 0; i < 2; i++) {
		if (self->monsterinfo.monster_used >= self->monsterinfo.monster_slots) {
			break;
		}

		offset = spawnpoints[i];
		startpoint = G_ProjectSource2(self->s.origin, offset, f, r, u);

		if (!FindSpawnPoint(startpoint, stalker_mins, stalker_maxs, spawnpoint, 64)) {
			continue;
		}

		edict_t* stalker = CreateGroundMonster(spawnpoint, self->s.angles, stalker_mins,
			stalker_maxs, horde::MonsterTypeID::STALKER, 256);
		if (!stalker) {
			continue;
		}

		// Show SpawnGrow effect now that stalker is actually created
		float radius = (stalker_maxs - stalker_mins).length() * 0.5f;
		SpawnGrow_Spawn(stalker->s.origin, radius, radius * 2.f);

		self->monsterinfo.monster_used++;
		stalker->monsterinfo.commander = self;
		stalker->monsterinfo.slots_from_commander = 1;
		stalker->nextthink = level.time;
		if (stalker->think) {
			stalker->think(stalker);
		}

		stalker->monsterinfo.aiflags |= AI_SPAWNED_COMMANDER | AI_DO_NOT_COUNT | AI_IGNORE_SHOTS;

		if (g_horde->integer) {
			stalker->item = brandom() ? G_HordePickItem() : nullptr;
		}
		ApplyMonsterBonusFlags(stalker);

		edict_t* designated_enemy = !G_IsCooperative() ? self->enemy : PickCoopTarget(stalker);
		if (G_IsCooperative() && (!designated_enemy || designated_enemy == self->enemy)) {
			designated_enemy = PickCoopTarget(stalker); // Try again to get a different target
		}
		if (!designated_enemy) {
			designated_enemy = self->enemy; // Fallback to widow's enemy
		}

		if (designated_enemy && designated_enemy->inuse && designated_enemy->health > 0) {
			stalker->enemy = designated_enemy;
			FoundTarget(stalker);
			if (stalker->monsterinfo.attack) {
				stalker->monsterinfo.attack(stalker);
			}
		}
	}

	// Set a cooldown to prevent instant re-spawning
	self->monsterinfo.spawn_cooldown = level.time + 8_sec;
}

void widow_spawn_check(edict_t* self)
{
	WidowBlaster(self);
	WidowSpawn(self);
}

void widow_ready_spawn(edict_t* self)
{
	vec3_t f, r, u, offset, startpoint, spawnpoint;
	int	   i;

	WidowBlaster(self);
	AngleVectors(self->s.angles, f, r, u);

	for (i = 0; i < 2; i++)
	{
		offset = spawnpoints[i];
		startpoint = G_ProjectSource2(self->s.origin, offset, f, r, u);
		if (FindSpawnPoint(startpoint, stalker_mins, stalker_maxs, spawnpoint, 64))
		{
			// Don't show SpawnGrow here - only show it when stalker is actually created
			// float radius = (stalker_maxs - stalker_mins).length() * 0.5f;
			// SpawnGrow_Spawn(spawnpoint + (stalker_mins + stalker_maxs), radius, radius * 2.f);
		}
	}
}

void widow_step(edict_t* self)
{
	gi.sound(self, CHAN_BODY, gi.soundindex("widow/bwstep3.wav"), 1, ATTN_NORM, 0);
}

mframe_t widow_frames_stand[] = {
	{ ai_stand }, { ai_stand }, { ai_stand }, { ai_stand }, { ai_stand },
	{ ai_stand }, { ai_stand }, { ai_stand }, { ai_stand }, { ai_stand },
	{ ai_stand }
};
MMOVE_T(widow_move_stand) = { FRAME_idle01, FRAME_idle11, widow_frames_stand, nullptr };

mframe_t widow_frames_walk[] = {
	{ ai_walk, 2.79f, widow_step }, { ai_walk, 2.77f }, { ai_walk, 3.53f },
	{ ai_walk, 3.97f }, { ai_walk, 4.13f }, { ai_walk, 4.09f },
	{ ai_walk, 3.84f }, { ai_walk, 3.62f, widow_step }, { ai_walk, 3.29f },
	{ ai_walk, 6.08f }, { ai_walk, 6.94f }, { ai_walk, 5.73f },
	{ ai_walk, 2.85f }
};
MMOVE_T(widow_move_walk) = { FRAME_walk01, FRAME_walk13, widow_frames_walk, nullptr };

// --- IMPROVED: Widow now spawns while running towards the player ---
mframe_t widow_frames_run[] = {
	{ ai_run, 2.79f, widow_step },
	{ ai_run, 2.77f, WidowSpawn},
	{ ai_run, 3.53f },
	{ ai_run, 3.97f },
	{ ai_run, 4.13f },
	{ ai_run, 4.09f },
	{ ai_run, 3.84f },
	{ ai_run, 3.62f, widow_step },
	{ ai_run, 3.29f, WidowSpawn},
	{ ai_run, 6.08f },
	{ ai_run, 6.94f },
	{ ai_run, 5.73f },
	{ ai_run, 2.85f }
};
MMOVE_T(widow_move_run) = { FRAME_walk01, FRAME_walk13, widow_frames_run, nullptr };

void widow_stepshoot(edict_t* self)
{
	gi.sound(self, CHAN_BODY, gi.soundindex("widow/bwstep2.wav"), 1, ATTN_NORM, 0);
	WidowBlaster(self);
}

// --- FIX: This is the animation for the restored charging attack ---
mframe_t widow_frames_run_attack[] = {
	{ ai_charge, 13, widow_stepshoot },
	{ ai_charge, 11.72f, WidowBlaster },
	{ ai_charge, 18.04f, WidowBlaster },
	{ ai_charge, 14.58f, WidowBlaster },
	{ ai_charge, 13, widow_stepshoot },
	{ ai_charge, 12.12f, WidowBlaster },
	{ ai_charge, 19.63f, WidowBlaster },
	{ ai_charge, 11.37f, WidowBlaster }
};
MMOVE_T(widow_move_run_attack) = { FRAME_run01, FRAME_run08, widow_frames_run_attack, widow_run };

void widow_start_run_5(edict_t* self)
{
	M_SetAnimation(self, &widow_move_run);
	self->monsterinfo.nextframe = FRAME_walk05;
}

void widow_start_run_10(edict_t* self)
{
	M_SetAnimation(self, &widow_move_run);
	self->monsterinfo.nextframe = FRAME_walk10;
}

void widow_start_run_12(edict_t* self)
{
	M_SetAnimation(self, &widow_move_run);
	self->monsterinfo.nextframe = FRAME_walk12;
}

mframe_t widow_frames_attack_pre_blaster[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, widow_attack_blaster }
};
MMOVE_T(widow_move_attack_pre_blaster) = { FRAME_fired01, FRAME_fired02a, widow_frames_attack_pre_blaster, nullptr };

mframe_t widow_frames_attack_blaster[] = {
	{ ai_charge, 0, widow_reattack_blaster }, { ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }, { ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }, { ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }, { ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }, { ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }, { ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }, { ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }, { ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }, { ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }
};
MMOVE_T(widow_move_attack_blaster) = { FRAME_fired02a, FRAME_fired20, widow_frames_attack_blaster, nullptr };

mframe_t widow_frames_attack_post_blaster[] = {
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(widow_move_attack_post_blaster) = { FRAME_fired21, FRAME_fired22, widow_frames_attack_post_blaster, widow_run };

mframe_t widow_frames_attack_post_blaster_r[] = {
	{ ai_charge, -2 }, { ai_charge, -10 }, { ai_charge, -2 },
	{ ai_charge }, { ai_charge, 0, widow_start_run_12 }
};
MMOVE_T(widow_move_attack_post_blaster_r) = { FRAME_transa01, FRAME_transa05, widow_frames_attack_post_blaster_r, nullptr };

mframe_t widow_frames_attack_post_blaster_l[] = {
	{ ai_charge }, { ai_charge, 14 }, { ai_charge, -2 },
	{ ai_charge, 10 }, { ai_charge, 10, widow_start_run_12 }
};
MMOVE_T(widow_move_attack_post_blaster_l) = { FRAME_transb01, FRAME_transb05, widow_frames_attack_post_blaster_l, nullptr };

extern const mmove_t widow_move_attack_rail;
extern const mmove_t widow_move_attack_rail_l;
extern const mmove_t widow_move_attack_rail_r;

void WidowRail(edict_t* self)
{
	if (!M_HasValidTarget(self) || !visible(self, self->enemy))
		return;

	vec3_t start, dir, forward, right;
	monster_muzzleflash_id_t flash;
	const bool is_widow1 = horde::IsMonsterType(self, horde::MonsterTypeID::WIDOW1);

	AngleVectors(self->s.angles, forward, right, nullptr);

	if (self->monsterinfo.active_move == &widow_move_attack_rail_l)
		flash = MZ2_WIDOW_RAIL_LEFT;
	else if (self->monsterinfo.active_move == &widow_move_attack_rail_r)
		flash = MZ2_WIDOW_RAIL_RIGHT;
	else
		flash = MZ2_WIDOW_RAIL;

	vec3_t scaled_offset = monster_flash_offset[flash];
	if (is_widow1) {
		scaled_offset = scaled_offset * self->s.scale;
	}

	start = G_ProjectSource(self->s.origin, scaled_offset, forward, right);
	dir = self->pos1 - start;
	dir.normalize();

	int damage = GetMonsterWeaponDamage(self->monsterinfo.monster_type_id, "railgun");
	if (damage <= 0) damage = is_widow1 ? WIDOW1_RAIL_DAMAGE : WIDOW_RAIL_DAMAGE;
	if (!is_widow1) damage *= widow_damage_multiplier;
	monster_fire_railgun(self, start, dir,
		damage,
		100, flash);

	self->timestamp = level.time + RAIL_COOLDOWN;
}

void WidowSaveLoc(edict_t* self)
{
	if (M_HasValidTarget(self))
	{
		self->pos1 = self->enemy->s.origin;
		self->pos1[2] += self->enemy->viewheight;
	}
}

void widow_start_rail(edict_t* self)
{
	self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
}

void widow_rail_done(edict_t* self)
{
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
}

mframe_t widow_frames_attack_pre_rail[] = {
	{ ai_charge, 0, widow_start_rail },
	{ ai_charge, 0, widow_attack_rail },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(widow_move_attack_pre_rail) = { FRAME_transc01, FRAME_transc04, widow_frames_attack_pre_rail, nullptr };

mframe_t widow_frames_attack_rail[] = {
	{ ai_charge }, { ai_charge }, { ai_charge, 0, WidowSaveLoc },
	{ ai_charge, -10, WidowRail }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge, 0, widow_rail_done }
};
MMOVE_T(widow_move_attack_rail) = { FRAME_firea01, FRAME_firea09, widow_frames_attack_rail, widow_run };

mframe_t widow_frames_attack_rail_r[] = {
	{ ai_charge }, { ai_charge }, { ai_charge, 0, WidowSaveLoc },
	{ ai_charge, -10, WidowRail }, { ai_charge }, { ai_charge },
	{ ai_charge, 0, WidowSaveLoc }, { ai_charge, -10, WidowRail },
	{ ai_charge, 0, widow_rail_done }
};
MMOVE_T(widow_move_attack_rail_r) = { FRAME_fireb01, FRAME_fireb09, widow_frames_attack_rail_r, widow_run };

mframe_t widow_frames_attack_rail_l[] = {
	{ ai_charge, 0, WidowSaveLoc }, { ai_charge, -10, WidowRail },
	{ ai_charge, 0, WidowSaveLoc }, { ai_charge, -10, WidowRail },
	{ ai_charge }, { ai_charge }, { ai_charge, 0, WidowSaveLoc },
	{ ai_charge, -10, WidowRail }, { ai_charge, 0, widow_rail_done }
};
MMOVE_T(widow_move_attack_rail_l) = { FRAME_firec01, FRAME_firec09, widow_frames_attack_rail_l, widow_run };

void widow_attack_rail(edict_t* self)
{
	float enemy_angle = target_angle(self);

	if (enemy_angle < -15)
		M_SetAnimation(self, &widow_move_attack_rail_l);
	else if (enemy_angle > 15)
		M_SetAnimation(self, &widow_move_attack_rail_r);
	else
		M_SetAnimation(self, &widow_move_attack_rail);
}

void widow_start_spawn(edict_t* self)
{
	self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
}

void widow_done_spawn(edict_t* self)
{
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
}

// --- IMPROVED: More aggressive spawning animation ---
mframe_t widow_frames_spawn[] = {
	{ ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge, 0, widow_start_spawn },
	{ ai_charge },
	{ ai_charge, 0, WidowBlaster },
	{ ai_charge, 0, widow_ready_spawn },
	{ ai_charge, 0, WidowBlaster },
	{ ai_charge, 0, WidowBlaster },
	{ ai_charge, 0, widow_spawn_check },
	{ ai_charge, 0, WidowBlaster },
	{ ai_charge, 0, widow_spawn_check },
	{ ai_charge, 0, WidowBlaster },
	{ ai_charge, 0, widow_spawn_check },
	{ ai_charge, 0, WidowBlaster },
	{ ai_charge, 0, widow_spawn_check },
	{ ai_charge },
	{ ai_charge, 0, widow_done_spawn }
};
MMOVE_T(widow_move_spawn) = { FRAME_spawn01, FRAME_spawn18, widow_frames_spawn, widow_run };

mframe_t widow_frames_pain_heavy[] = {
	{ ai_move }, { ai_move }, { ai_move }, { ai_move }, { ai_move },
	{ ai_move }, { ai_move }, { ai_move }, { ai_move }, { ai_move },
	{ ai_move }, { ai_move }, { ai_move }
};
MMOVE_T(widow_move_pain_heavy) = { FRAME_pain01, FRAME_pain13, widow_frames_pain_heavy, widow_run };

mframe_t widow_frames_pain_light[] = {
	{ ai_move }, { ai_move }, { ai_move }
};
MMOVE_T(widow_move_pain_light) = { FRAME_pain201, FRAME_pain203, widow_frames_pain_light, widow_run };

void spawn_out_start(edict_t* self)
{
	vec3_t startpoint, f, r, u;
	AngleVectors(self->s.angles, f, r, u);

	startpoint = G_ProjectSource2(self->s.origin, beameffects[0], f, r, u);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_WIDOWBEAMOUT);
	gi.WriteShort(20001);
	gi.WritePosition(startpoint);
	gi.multicast(startpoint, MULTICAST_ALL, false);

	startpoint = G_ProjectSource2(self->s.origin, beameffects[1], f, r, u);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_WIDOWBEAMOUT);
	gi.WriteShort(20002);
	gi.WritePosition(startpoint);
	gi.multicast(startpoint, MULTICAST_ALL, false);

	gi.sound(self, CHAN_VOICE, gi.soundindex("misc/bwidowbeamout.wav"), 1, ATTN_NORM, 0);
}

void spawn_out_do(edict_t* self)
{
	vec3_t startpoint, f, r, u;
	AngleVectors(self->s.angles, f, r, u);
	startpoint = G_ProjectSource2(self->s.origin, beameffects[0], f, r, u);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_WIDOWSPLASH);
	gi.WritePosition(startpoint);
	gi.multicast(startpoint, MULTICAST_ALL, false);
	startpoint = G_ProjectSource2(self->s.origin, beameffects[1], f, r, u);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_WIDOWSPLASH);
	gi.WritePosition(startpoint);
	gi.multicast(startpoint, MULTICAST_ALL, false);
	startpoint = self->s.origin;
	startpoint[2] += 36;
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_BOSSTPORT);
	gi.WritePosition(startpoint);
	gi.multicast(startpoint, MULTICAST_PHS, false);

	Widowlegs_Spawn(self->s.origin, self->s.angles, self);
	G_FreeEdict(self);
}

mframe_t widow_frames_death[] = {
	{ ai_move }, { ai_move }, { ai_move }, { ai_move }, { ai_move },
	{ ai_move }, { ai_move }, { ai_move }, { ai_move }, { ai_move, 0, spawn_out_start },
	{ ai_move }, { ai_move }, { ai_move }, { ai_move }, { ai_move },
	{ ai_move }, { ai_move }, { ai_move }, { ai_move }, { ai_move },
	{ ai_move }, { ai_move }, { ai_move }, { ai_move }, { ai_move },
	{ ai_move }, { ai_move }, { ai_move }, { ai_move }, { ai_move },
	{ ai_move, 0, spawn_out_do }
};
MMOVE_T(widow_move_death) = { FRAME_death01, FRAME_death31, widow_frames_death, nullptr };

// --- FIX: Separate, shorter death animation for widow1 ---
mframe_t widow1_frames_death[] = {
	{ ai_move, 0, spawn_out_start },
	{ ai_move }, { ai_move }, { ai_move },
	{ ai_move }, { ai_move },
	{ ai_move, 0, spawn_out_do }
};
MMOVE_T(widow1_move_death) = { FRAME_death25, FRAME_death31, widow1_frames_death, nullptr };

// --- IMPROVED: Added safety check for enemy ---
void widow_attack_kick(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t aim = { 100, 0, 4 };
	if (self->enemy->groundentity)
		fire_hit(self, aim, irandom(50, 56), 500);
	else
		fire_hit(self, aim, irandom(50, 56), 250);
}

mframe_t widow_frames_attack_kick[] = {
	{ ai_move }, { ai_move }, { ai_move },
	{ ai_move, 0, widow_attack_kick },
	{ ai_move }, { ai_move }, { ai_move }, { ai_move }
};
MMOVE_T(widow_move_attack_kick) = { FRAME_kick01, FRAME_kick08, widow_frames_attack_kick, widow_run };

MONSTERINFO_STAND(widow_stand) (edict_t* self) -> void
{
	gi.sound(self, CHAN_WEAPON, gi.soundindex("widow/laugh.wav"), 1, ATTN_NORM, 0);
	M_SetAnimation(self, &widow_move_stand);
}

MONSTERINFO_RUN(widow_run) (edict_t* self) -> void
{
	self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &widow_move_stand);
	else
		M_SetAnimation(self, &widow_move_run);
}

MONSTERINFO_WALK(widow_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &widow_move_walk);
}

MONSTERINFO_ATTACK(widow_attack) (edict_t* self) -> void {
	if (!M_HasValidTarget(self) || !visible(self, self->enemy)) {
		return; // Stop immediately if the target is invalid or not visible.

		self->movetarget = nullptr;
		const bool is_widow1 = horde::IsMonsterType(self, horde::MonsterTypeID::WIDOW1);
		const float range = realrange(self, self->enemy);

		// --- 1. Spawning Logic (Inspired by Tank Spawner) ---
		bool can_spawn = false;
		// Check cooldown and if there are enough slots for a pair of stalkers
		if (level.time >= self->monsterinfo.spawn_cooldown && M_SlotsLeft(self) >= 2)
		{
			if (is_widow1) {
				// widow1 is a skirmisher, not a dedicated spawner.
				// Limit it to a max of 2 active stalkers to encourage other attacks.
				if (self->monsterinfo.monster_used <= 2) {
					can_spawn = true;
				}
			}
			else {
				// The main widow boss can spawn as long as it has slots.
				can_spawn = true;
			}
		}

		// If all conditions are met, there's a high chance to spawn.
		if (can_spawn && range > 150 && frandom() < 0.75f) {
			M_SetAnimation(self, &widow_move_spawn);
			// The cooldown is set within the WidowSpawn function itself.
			return;
		}

		// --- 2. Charging Attack ---
		// More likely at long range. widow1 is faster and more likely to charge.
		const float charge_chance = is_widow1 ? 0.6f : 0.3f;
		if (range > 400 && frandom() < charge_chance) {
			M_SetAnimation(self, &widow_move_run_attack);
			return;
		}

		// --- 3. Ranged Attack Selection (Less Blaster Spam) ---
		const bool rail_is_ready = level.time >= self->timestamp;
		const bool blaster_is_ready = level.time >= self->monsterinfo.fire_wait;

		// If both attacks are ready, choose one probabilistically.
		if (rail_is_ready && blaster_is_ready) {
			if (frandom() < 0.6f) { // 60% chance to prefer the powerful railgun
				gi.sound(self, CHAN_WEAPON, sound_rail, 1, ATTN_NORM, 0);
				M_SetAnimation(self, &widow_move_attack_pre_rail);
			}
			else {
				M_SetAnimation(self, &widow_move_attack_pre_blaster);
			}
		}
		// Otherwise, fire whichever is ready.
		else if (rail_is_ready) {
			gi.sound(self, CHAN_WEAPON, sound_rail, 1, ATTN_NORM, 0);
			M_SetAnimation(self, &widow_move_attack_pre_rail);
		}
		else if (blaster_is_ready) {
			M_SetAnimation(self, &widow_move_attack_pre_blaster);
		}
		// If no attacks are ready, the widow will continue its run/walk cycle.
	}
}
void widow_attack_blaster(edict_t* self)
{
	self->monsterinfo.fire_wait = level.time + random_time(1_sec, 3_sec);
	M_SetAnimation(self, &widow_move_attack_blaster);
	self->monsterinfo.nextframe = WidowTorso(self);
}

void widow_reattack_blaster(edict_t* self)
{
	WidowBlaster(self);

	if ((self->monsterinfo.active_move == &widow_move_attack_post_blaster_l) ||
		(self->monsterinfo.active_move == &widow_move_attack_post_blaster_r))
		return;

	if (self->monsterinfo.fire_wait >= level.time)
		return;

	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
	M_SetAnimation(self, &widow_move_attack_post_blaster);
}

PAIN(widow_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 5_sec;
	const bool is_widow1 = horde::IsMonsterType(self, horde::MonsterTypeID::WIDOW1);

	// Set sound attenuation based on widow type
	const int attenuation = is_widow1 ? ATTN_NORM : ATTN_NONE;

	if (damage < 15)
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, attenuation, 0);
	else if (damage < 75)
		gi.sound(self, CHAN_VOICE, sound_pain2, 1, attenuation, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_pain3, 1, attenuation, 0);

	if (!M_ShouldReactToPain(self, mod))
		return;

	self->monsterinfo.fire_wait = 0_ms;

	if (damage >= 75) {
		if ((skill->integer < 3) && (frandom() < (0.75f - (0.1f * skill->integer)))) {
			M_SetAnimation(self, &widow_move_pain_heavy);
			self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		}
	}
	else if (damage >= 15) {
		if ((skill->integer < 3) && (frandom() < (0.6f - (0.2f * skill->integer)))) {
			M_SetAnimation(self, &widow_move_pain_light);
			self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		}
	}
}

MONSTERINFO_SETSKIN(widow_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

void widow_dead(edict_t* self)
{
	self->mins = { -56, -56, 0 };
	self->maxs = { 56, 56, 80 };
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0_ms;
	gi.linkentity(self);
}

DIE(widow_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED)
		boss_die(self);

	self->deadflag = true;
	self->takedamage = false;
	self->count = 0;
	self->monsterinfo.quad_time = 0_ms;
	self->monsterinfo.double_time = 0_ms;
	self->monsterinfo.invincible_time = 0_ms;

	// --- FIX: Use correct death animation for each type ---
	const bool is_widow1 = horde::IsMonsterType(self, horde::MonsterTypeID::WIDOW1);
	if (is_widow1) {
		M_SetAnimation(self, &widow1_move_death);
	}
	else {
		M_SetAnimation(self, &widow_move_death);
	}
}

MONSTERINFO_MELEE(widow_melee) (edict_t* self) -> void
{
	M_SetAnimation(self, &widow_move_attack_kick);
}

void WidowGoinQuad(edict_t* self, gtime_t time)
{
	self->monsterinfo.quad_time = time;
}

void WidowDouble(edict_t* self, gtime_t time)
{
	self->monsterinfo.double_time = time;
}
void WidowPent(edict_t* self, gtime_t time)
{
	self->monsterinfo.invincible_time = time;
}

void WidowPowerArmor(edict_t* self)
{
	self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
	if (self->monsterinfo.power_armor_power <= 0)
		self->monsterinfo.power_armor_power += 250 * skill->integer;
}

void WidowRespondPowerup(edict_t* self, edict_t* other)
{
	// --- FIX: This logic only applies to the main widow boss ---
	if (horde::IsMonsterType(self, horde::MonsterTypeID::WIDOW1)) {
		return;
	}

	if (!(self->monsterinfo.bonus_flags & (BF_BERSERKING | BF_RAGEQUITTER | BF_CHAMPION))) {
		if (other->client)
		{
			if (other->s.effects & EF_QUAD)
			{
				if (skill->integer == 1) WidowDouble(self, other->client->quad_time);
				else if (skill->integer == 2) WidowGoinQuad(self, other->client->quad_time);
				else if (skill->integer >= 3) {
					WidowGoinQuad(self, other->client->quad_time);
					WidowPowerArmor(self);
				}
			}
			else if (other->s.effects & EF_DOUBLE)
			{
				if (skill->integer == 2) WidowDouble(self, other->client->double_time);
				else if (skill->integer >= 3) {
					WidowDouble(self, other->client->double_time);
					WidowPowerArmor(self);
				}
			}
			else {
				widow_damage_multiplier = 1;
			}

			if (other->s.effects & EF_PENT)
			{
				if (skill->integer == 1) WidowPowerArmor(self);
				else if (skill->integer == 2) WidowPent(self, other->client->invincible_time);
				else if (skill->integer >= 3) {
					WidowPent(self, other->client->invincible_time);
					WidowPowerArmor(self);
				}
			}
		}
	}
}

void WidowPowerups(edict_t* self)
{
	if (horde::IsMonsterType(self, horde::MonsterTypeID::WIDOW1)) {
		return;
	}

	if (!G_IsCooperative()) {
		WidowRespondPowerup(self, self->enemy);
	}
	else {
		edict_t* ent;
		for (uint32_t player = 1; player <= game.maxclients; player++) {
			ent = &g_edicts[player];
			if (ent->inuse && ent->client && (ent->s.effects & EF_PENT)) {
				WidowRespondPowerup(self, ent);
				return;
			}
		}
		for (uint32_t player = 1; player <= game.maxclients; player++) {
			ent = &g_edicts[player];
			if (ent->inuse && ent->client && (ent->s.effects & EF_QUAD)) {
				WidowRespondPowerup(self, ent);
				return;
			}
		}
		for (uint32_t player = 1; player <= game.maxclients; player++) {
			ent = &g_edicts[player];
			if (ent->inuse && ent->client && (ent->s.effects & EF_DOUBLE)) {
				WidowRespondPowerup(self, ent);
				return;
			}
		}
	}
}

MONSTERINFO_CHECKATTACK(Widow_CheckAttack) (edict_t* self) -> bool
{
	if (!self->enemy || ClientIsSpectating(self->enemy->client))
		return false;

	WidowPowerups(self);

	// Prevent interrupting certain animations to start a new attack
	if (self->monsterinfo.active_move == &widow_move_run)
	{
		switch (self->s.frame)
		{
		case FRAME_walk04: case FRAME_walk05: case FRAME_walk06:
		case FRAME_walk07: case FRAME_walk08: case FRAME_walk12:
			return false;
		default:
			break;
		}
	}

	return M_CheckAttack_Base(self, 0.4f, 0.8f, 0.7f, 0.6f, 0.5f, 0.f);
}

// --- JUMP ATTACK ADDED ---
void widow_jump_down(edict_t* self)
{
	vec3_t forward, up;
	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 200);
	self->velocity += (up * 350);
}

void widow_jump_up(edict_t* self)
{
	vec3_t forward, up;
	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 300);
	self->velocity += (up * 500);
}

void widow_jump_wait_land(edict_t* self)
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

// --- FIX: Created two separate, constant animation definitions ---

// Animation for jumping UP
mframe_t widow_frames_jump_up[] = {
	{ ai_move, -8 },
	{ ai_move, -8 },
	{ ai_move, -8, widow_jump_up }, // Jump action is here
	{ ai_move, -8 },
	{ ai_move, 0, widow_jump_wait_land },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 }
};
MMOVE_T(widow_move_jump_up) = { FRAME_pain01, FRAME_pain08, widow_frames_jump_up, widow_run };

// Animation for jumping DOWN
mframe_t widow_frames_jump_down[] = {
	{ ai_move, -8 },
	{ ai_move, -8 },
	{ ai_move, -8, widow_jump_down }, // Jump action is here
	{ ai_move, -8 },
	{ ai_move, 0, widow_jump_wait_land },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 }
};
MMOVE_T(widow_move_jump_down) = { FRAME_pain01, FRAME_pain08, widow_frames_jump_down, widow_run };


// --- FIX: Simplified the jump function to select the correct pre-defined animation ---
void widow_jump(edict_t* self, blocked_jump_result_t result)
{
	if (!M_HasValidTarget(self))
		return; // Can't jump at a non-existent or dead target.

	if (result == blocked_jump_result_t::JUMP_JUMP_UP)
	{
		M_SetAnimation(self, &widow_move_jump_up);
	}
	else
	{
		M_SetAnimation(self, &widow_move_jump_down);
	}
}

MONSTERINFO_BLOCKED(widow_blocked) (edict_t* self, float dist) -> bool
{
	// --- JUMP ATTACK ADDED ---
	// First, check if we can jump over the obstacle
	if (auto const result = blocked_checkjump(self, dist); result != blocked_jump_result_t::NO_JUMP)
	{
		if (result != blocked_jump_result_t::JUMP_TURN)
			widow_jump(self, result);
		return true;
	}

	// Original logic for when blocked during a charge
	if (self->monsterinfo.active_move == &widow_move_run_attack)
	{
		if (self->monsterinfo.checkattack(self))
			self->monsterinfo.attack(self);
		else
			self->monsterinfo.run(self);
		return true;
	}

	if (blocked_checkplat(self, dist))
		return true;

	return false;
}
// --- END JUMP ATTACK ---

void WidowCalcSlots(edict_t* self)
{
	switch (skill->integer)
	{
	case 0:
	case 1:
		self->monsterinfo.monster_slots = 3;
		break;
	case 2:
		self->monsterinfo.monster_slots = 4;
		break;
	case 3:
		self->monsterinfo.monster_slots = 6;
		break;
	default:
		self->monsterinfo.monster_slots = 3;
		break;
	}
	if (G_IsCooperative())
	{
		self->monsterinfo.monster_slots = min(6, self->monsterinfo.monster_slots + (skill->integer * (CountPlayers() - 1)));
	}
}

void WidowPrecache()
{
	gi.soundindex("stalker/pain.wav");
	gi.soundindex("stalker/death.wav");
	gi.soundindex("stalker/sight.wav");
	gi.soundindex("stalker/melee1.wav");
	gi.soundindex("stalker/melee2.wav");
	gi.soundindex("stalker/idle.wav");
	gi.soundindex("tank/tnkatck3.wav");
	gi.modelindex("models/objects/laser/tris.md2");
	gi.modelindex("models/monsters/stalker/tris.md2");
	gi.modelindex("models/items/spawngro3/tris.md2");
	gi.modelindex("models/objects/gibs/sm_metal/tris.md2");
	gi.modelindex("models/objects/gibs/gear/tris.md2");
	gi.modelindex("models/monsters/blackwidow/gib1/tris.md2");
	gi.modelindex("models/monsters/blackwidow/gib2/tris.md2");
	gi.modelindex("models/monsters/blackwidow/gib3/tris.md2");
	gi.modelindex("models/monsters/blackwidow/gib4/tris.md2");
	gi.modelindex("models/monsters/blackwidow2/gib1/tris.md2");
	gi.modelindex("models/monsters/blackwidow2/gib2/tris.md2");
	gi.modelindex("models/monsters/blackwidow2/gib3/tris.md2");
	gi.modelindex("models/monsters/blackwidow2/gib4/tris.md2");
	gi.modelindex("models/monsters/legs/tris.md2");
	gi.soundindex("misc/bwidowbeamout.wav");
	gi.soundindex("misc/bigtele.wav");
	gi.soundindex("widow/bwstep3.wav");
	gi.soundindex("widow/bwstep2.wav");
	gi.soundindex("widow/bwstep1.wav");
}

/*QUAKED monster_widow (1 .5 0) (-40 -40 0) (40 40 144) Ambush Trigger_Spawn Sight
 */
void SP_monster_widow(edict_t* self) {
	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (self->monsterinfo.monster_type_id == MONSTER_TYPE_UNKNOWN) {
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::WIDOW);
    }
	const MonsterStatsConfig* config = GetMonsterConfig(self->monsterinfo.monster_type_id);

	sound_pain1.assign("widow/bw1pain1.wav");
	sound_pain2.assign("widow/bw1pain2.wav");
	sound_pain3.assign("widow/bw1pain3.wav");
	sound_rail.assign("gladiator/railgun.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/blackwidow/tris.md2");
	self->mins = { -40, -40, 0 };
	self->maxs = { 40, 40, 144 };

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


	self->health = (2800 * skill->integer) * st.health_multiplier;
	if (G_IsCooperative())
		self->health += 500 * skill->integer;
	self->gib_health = -5000;
	self->mass = 1500;

	if (skill->integer == 3) {
		if (!st.was_key_specified("power_armor_type"))
			self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = 500;
	}

	self->yaw_speed = 30;
	self->flags |= FL_IMMUNE_LASER;
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	self->pain = widow_pain;
	self->die = widow_die;
	self->monsterinfo.melee = widow_melee;
	self->monsterinfo.stand = widow_stand;
	self->monsterinfo.walk = widow_walk;
	self->monsterinfo.run = widow_run;
	self->monsterinfo.attack = widow_attack;
	self->monsterinfo.search = widow_search;
	self->monsterinfo.checkattack = Widow_CheckAttack;
	self->monsterinfo.sight = widow_sight;
	self->monsterinfo.setskin = widow_setskin;
	self->monsterinfo.blocked = widow_blocked;

	// --- JUMP ATTACK ADDED ---
	self->monsterinfo.can_jump = true;
	// HORDE MOD: Increased jump height from 120 to 156 (30% increase) for better obstacle navigation
	self->monsterinfo.jump_height = 156;
	self->monsterinfo.drop_height = 256;

	gi.linkentity(self);
	M_SetAnimation(self, &widow_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	WidowPrecache();
	WidowCalcSlots(self);
	widow_damage_multiplier = 1;

	self->monsterinfo.monster_used = 0;
	self->monsterinfo.spawn_cooldown = 0_sec;

	walkmonster_start(self);
	ApplyMonsterBonusFlags(self);
}

/*QUAKED monster_widow1 (1 .5 0) (-40 -40 0) (40 40 144) Ambush Trigger_Spawn Sight
*/
void SP_monster_widow1(edict_t* self) {
	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}
	const spawn_temp_t& st = ED_GetSpawnTemp();

	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::WIDOW1);
	const MonsterStatsConfig* config = GetMonsterConfig(self->monsterinfo.monster_type_id);
	sound_pain1.assign("widow/bw1pain1.wav");
	sound_pain2.assign("widow/bw1pain2.wav");
	sound_pain3.assign("widow/bw1pain3.wav");
	sound_rail.assign("gladiator/railgun.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/blackwidow/tris.md2");
	self->mins = { -40, -40, 0 };
	self->maxs = { 40, 40, 144 };
	self->health = 1750;
	if (G_IsCooperative())
		self->health += 500 * skill->integer;
	self->gib_health = -5000;
	self->mass = 1500;

	if (skill->integer == 3) {
		if (!st.was_key_specified("power_armor_type"))
			self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = 500;
	}
	self->s.scale = 0.6f;
	self->yaw_speed = 40;

	self->flags |= FL_IMMUNE_LASER;
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	self->pain = widow_pain;
	self->die = widow_die;
	self->monsterinfo.melee = widow_melee;
	self->monsterinfo.stand = widow_stand;
	self->monsterinfo.walk = widow_walk;
	self->monsterinfo.run = widow_run;
	self->monsterinfo.attack = widow_attack;
	self->monsterinfo.search = widow_search;
	self->monsterinfo.checkattack = Widow_CheckAttack;
	self->monsterinfo.sight = widow_sight;
	self->monsterinfo.setskin = widow_setskin;
	self->monsterinfo.blocked = widow_blocked;

	// --- JUMP ATTACK ADDED ---
	self->monsterinfo.can_jump = true;
	// HORDE MOD: Increased jump height from 120 to 156 (30% increase) for better obstacle navigation
	self->monsterinfo.jump_height = 156;
	self->monsterinfo.drop_height = 256;

	gi.linkentity(self);
	M_SetAnimation(self, &widow_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	WidowPrecache();
	WidowCalcSlots(self);
	widow_damage_multiplier = 1;

	self->monsterinfo.monster_used = 0;
	self->monsterinfo.spawn_cooldown = 0_sec;

	walkmonster_start(self);
	ApplyMonsterBonusFlags(self);
}