// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

TURRET

==============================================================================
*/

#include "../g_local.h"
#include "m_rogue_turret.h"
#include "../shared.h"
constexpr spawnflags_t SPAWNFLAG_TURRET2_BLASTER = 0x0008_spawnflag;
constexpr spawnflags_t SPAWNFLAG_TURRET2_MACHINEGUN = 0x0010_spawnflag;
//constexpr spawnflags_t SPAWNFLAG_TURRET2_ROCKET = 0x0020_spawnflag;
constexpr spawnflags_t SPAWNFLAG_TURRET2_HEATBEAM = 0x0040_spawnflag;
constexpr spawnflags_t SPAWNFLAG_TURRET2_WEAPONCHOICE = SPAWNFLAG_TURRET2_HEATBEAM | SPAWNFLAG_TURRET2_MACHINEGUN | SPAWNFLAG_TURRET2_BLASTER;
constexpr spawnflags_t SPAWNFLAG_TURRET2_WALL_UNIT = 0x0080_spawnflag;
constexpr spawnflags_t SPAWNFLAG_TURRET2_NO_LASERSIGHT = 18_spawnflag_bit;

void turret2Aim(edict_t* self);
void turret2_ready_gun(edict_t* self);
void turret2_run(edict_t* self);
void TurretSparks(edict_t* self);

extern const mmove_t turret2_move_fire;
extern const mmove_t turret2_move_fire_blind;

static cached_soundindex sound_moved, sound_moving, sound_pew;

// Actualizar la posición del efecto
static void UpdateSmokePosition(edict_t* self) {
	if (!self || !self->inuse || !self->target_hint_chain || !self->target_hint_chain->inuse)
		return;

	// Actualizar frame y skin si es necesario
	self->target_hint_chain->s.frame = self->s.frame;
	self->target_hint_chain->s.skinnum = self->s.skinnum;

	// Calcular la nueva posición para el emisor de humo
	vec3_t forward;
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	const vec3_t smoke_pos = self->s.origin + (forward * 20.0f);

	// Actualizar posición del emisor
	self->target_hint_chain->s.origin = smoke_pos;
	self->target_hint_chain->s.angles = self->s.angles;

	// Emitir el efecto de humo
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_CHAINFIST_SMOKE);
	gi.WritePosition(smoke_pos);
	gi.multicast(smoke_pos, MULTICAST_PVS, false);

	gi.linkentity(self->target_hint_chain);
}

void turret2Aim(edict_t* self)
{

	// Validaciones iniciales críticas
	if (!self || !self->inuse)
		return;

	// Actualizar la posición del efecto visual
	UpdateSmokePosition(self);

	TurretSparks(self);

	vec3_t end, dir;
	vec3_t ang;
	float  move, idealPitch, idealYaw, current, speed;
	int	   orientation;

	if (!self->enemy || self->enemy == world)
	{
		if (self->monsterinfo.search_time < level.time) {
			if (!FindMTarget(self))
				return;
			self->monsterinfo.search_time = level.time + 300_ms;
		}
		return; // Return if we don't have an enemy and couldn't find one
	}

	// if turret is still in inactive mode, ready the gun, but don't aim
	if (self->s.frame < FRAME_active01)
	{
		turret2_ready_gun(self);
		return;
	}
	// if turret is still readying, don't aim.
	if (self->s.frame < FRAME_run01)
		return;

	// PMM - blindfire aiming here
	if (self->monsterinfo.active_move == &turret2_move_fire_blind)
	{
		end = self->monsterinfo.blind_fire_target;
		if (self->enemy->s.origin[2] < self->monsterinfo.blind_fire_target[2])
			end[2] += self->enemy->viewheight + 10;
		else
			end[2] += self->enemy->mins[2] - 10;
	}
	else
	{
		end = self->enemy->s.origin;
		if (self->enemy->client)
			end[2] += self->enemy->viewheight;
	}

	dir = end - self->s.origin;
	ang = vectoangles(dir);

	//
	// Clamp first
	//

	idealPitch = ang[PITCH];
	idealYaw = ang[YAW];

	orientation = (int)self->offset[1];
	switch (orientation)
	{
	case -1: // up		pitch: 0 to 90
		if (idealPitch < -90)
			idealPitch += 360;
		if (idealPitch > -5)
			idealPitch = -5;
		break;
	case -2: // down		pitch: -180 to -360
		if (idealPitch > -90)
			idealPitch -= 360;
		if (idealPitch < -355)
			idealPitch = -355;
		else if (idealPitch > -185)
			idealPitch = -185;
		break;
	case 0: // +X		pitch: 0 to -90, -270 to -360 (or 0 to 90)
		if (idealPitch < -180)
			idealPitch += 360;

		if (idealPitch > 85)
			idealPitch = 85;
		else if (idealPitch < -85)
			idealPitch = -85;

		//			yaw: 270 to 360, 0 to 90
		//			yaw: -90 to 90 (270-360 == -90-0)
		if (idealYaw > 180)
			idealYaw -= 360;
		if (idealYaw > 85)
			idealYaw = 85;
		else if (idealYaw < -85)
			idealYaw = -85;
		break;
	case 90: // +Y	pitch: 0 to 90, -270 to -360 (or 0 to 90)
		if (idealPitch < -180)
			idealPitch += 360;

		if (idealPitch > 85)
			idealPitch = 85;
		else if (idealPitch < -85)
			idealPitch = -85;

		//			yaw: 0 to 180
		if (idealYaw > 270)
			idealYaw -= 360;
		if (idealYaw > 175)
			idealYaw = 175;
		else if (idealYaw < 5)
			idealYaw = 5;

		break;
	case 180: // -X	pitch: 0 to 90, -270 to -360 (or 0 to 90)
		if (idealPitch < -180)
			idealPitch += 360;

		if (idealPitch > 85)
			idealPitch = 85;
		else if (idealPitch < -85)
			idealPitch = -85;

		//			yaw: 90 to 270
		if (idealYaw > 265)
			idealYaw = 265;
		else if (idealYaw < 95)
			idealYaw = 95;

		break;
	case 270: // -Y	pitch: 0 to 90, -270 to -360 (or 0 to 90)
		if (idealPitch < -180)
			idealPitch += 360;

		if (idealPitch > 85)
			idealPitch = 85;
		else if (idealPitch < -85)
			idealPitch = -85;

		//			yaw: 180 to 360
		if (idealYaw < 90)
			idealYaw += 360;
		if (idealYaw > 355)
			idealYaw = 355;
		else if (idealYaw < 185)
			idealYaw = 185;
		break;
	}

	//
	// adjust pitch
	//
	current = self->s.angles[PITCH];
	speed = self->yaw_speed / (gi.tick_rate / 10);

	if (idealPitch != current)
	{
		move = idealPitch - current;

		while (move >= 360)
			move -= 360;
		if (move >= 90)
		{
			move = move - 360;
		}

		while (move <= -360)
			move += 360;
		if (move <= -90)
		{
			move = move + 360;
		}

		if (move > 0)
		{
			if (move > speed)
				move = speed;
		}
		else
		{
			if (move < -speed)
				move = -speed;
		}

		self->s.angles[PITCH] = anglemod(current + move);
	}

	//
	// adjust yaw
	//
	current = self->s.angles[YAW];

	if (idealYaw != current)
	{
		move = idealYaw - current;

		//		while(move >= 360)
		//			move -= 360;
		if (move >= 180)
		{
			move = move - 360;
		}

		//		while(move <= -360)
		//			move += 360;
		if (move <= -180)
		{
			move = move + 360;
		}

		if (move > 0)
		{
			if (move > speed)
				move = speed;
		}
		else
		{
			if (move < -speed)
				move = -speed;
		}

		self->s.angles[YAW] = anglemod(current + move);
	}

	if (self->spawnflags.has(SPAWNFLAG_TURRET2_NO_LASERSIGHT))
		return;

	// Paril: improved turrets; draw lasersight
	if (!self->target_ent)
	{
		self->target_ent = G_Spawn();
		self->target_ent->s.modelindex = MODELINDEX_WORLD;
		self->target_ent->s.renderfx = RF_BEAM;
		self->target_ent->s.frame = 1;
		self->target_ent->s.skinnum = 0xf0f0f0f0;
		self->target_ent->classname = "turret_lasersight";
		self->target_ent->s.effects = EF_BOB;
		self->target_ent->s.origin = self->s.origin;
	}

	vec3_t forward;
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	end = self->s.origin + (forward * 8192);
	trace_t tr = gi.traceline(self->s.origin, end, self, MASK_SOLID);

	float scan_range = 64.f;

	if (visible(self, self->enemy))
		scan_range = 12.f;

	tr.endpos[0] += sinf(level.time.seconds() + self->s.number) * scan_range;
	tr.endpos[1] += cosf((level.time.seconds() - self->s.number) * 3.f) * scan_range;
	tr.endpos[2] += sinf((level.time.seconds() - self->s.number) * 2.5f) * scan_range;

	forward = tr.endpos - self->s.origin;
	forward.normalize();

	end = self->s.origin + (forward * 8192);
	tr = gi.traceline(self->s.origin, end, self, MASK_SOLID);

	self->target_ent->s.old_origin = tr.endpos;
	gi.linkentity(self->target_ent);
}


MONSTERINFO_SIGHT(turret2_sight) (edict_t* self, edict_t* other) -> void
{
}

MONSTERINFO_SEARCH(turret2_search) (edict_t* self) -> void
{
}

mframe_t turret2_frames_stand[] = {
	{ ai_stand },
	{ ai_stand }
};
MMOVE_T(turret2_move_stand) = { FRAME_stand01, FRAME_stand02, turret2_frames_stand, nullptr };

MONSTERINFO_STAND(turret2_stand) (edict_t* self) -> void
{

	TurretSparks(self);

	M_SetAnimation(self, &turret2_move_stand);
	if (self->target_ent)
	{
		G_FreeEdict(self->target_ent);
		self->target_ent = nullptr;
	}
}

mframe_t turret2_frames_ready_gun[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand }
};
MMOVE_T(turret2_move_ready_gun) = { FRAME_active01, FRAME_run01, turret2_frames_ready_gun, turret2_run };

void turret2_ready_gun(edict_t* self)
{
	if (self->monsterinfo.active_move != &turret2_move_ready_gun)
	{
		M_SetAnimation(self, &turret2_move_ready_gun);
//		self->monsterinfo.weapon_sound = sound_moving;
	}
}

mframe_t turret2_frames_seek[] = {
	{ ai_walk, 0, turret2Aim },
	{ ai_walk, 0, turret2Aim }
};
MMOVE_T(turret2_move_seek) = { FRAME_run01, FRAME_run02, turret2_frames_seek, nullptr };

MONSTERINFO_WALK(turret2_walk) (edict_t* self) -> void
{
	if (self->s.frame < FRAME_run01)
		turret2_ready_gun(self);
	else
		M_SetAnimation(self, &turret2_move_seek);
}

mframe_t turret2_frames_run[] = {
	{ ai_run, 0, turret2Aim },
	{ ai_run, 0, turret2Aim }
};
MMOVE_T(turret2_move_run) = { FRAME_run01, FRAME_run02, turret2_frames_run, turret2_run };

void CreateTurretGlowEffect(edict_t* turret);

MONSTERINFO_RUN(turret2_run) (edict_t* self) -> void
{
	CreateTurretGlowEffect(self);

	//if (self->s.frame < FRAME_run01)
	//	turret2_ready_gun(self);
	//else
	{
		self->monsterinfo.aiflags |= AI_HIGH_TICK_RATE;
		M_SetAnimation(self, &turret2_move_run);

		if (self->monsterinfo.weapon_sound)
		{
			self->monsterinfo.weapon_sound = 0;
			gi.sound(self, CHAN_WEAPON, sound_moved, 1.0f, ATTN_STATIC, 0.f);
		}
	}
	TurretSparks(self);
}

//Powerups

static void TurretRespondPowerup(edict_t* turret, edict_t* owner) {
	if (!turret || !owner || !owner->client)
		return;

	if (owner->client->quad_time > level.time) {
		turret->monsterinfo.quad_time = owner->client->quad_time;
	}

	if (owner->client->double_time > level.time) {
		turret->monsterinfo.double_time = owner->client->double_time;
	}

	if (owner->client->invincible_time > level.time) {
		turret->monsterinfo.invincible_time = owner->client->invincible_time;
	}
	if (owner->client->quadfire_time > level.time) {
		turret->monsterinfo.quadfire_time = owner->client->quadfire_time;
	}
}


static void TurretCheckPowerups(edict_t* turret) {
	if (!turret || !turret->owner || !turret->owner->client)
		return;

	edict_t* owner = turret->owner;

	// Ensure the turret always inherits quad, double, and invincibility from the player
	TurretRespondPowerup(turret, owner);
} // Now, turrets will also inherit invincibility from their owners, just like quad and double.

// **********************
//  ATTACK
// **********************


int32_t TURRET2_BLASTER_DAMAGE = 3;
int32_t TURRET2_BULLET_DAMAGE = 2;
//bool M_CheckClearShot(edict_t* self, const vec3_t& offset, vec3_t& start);

// Common helper function for damage calculation
static float CalculateDamage(edict_t* self, int baseDamage) {
	const float damageModifier = M_DamageModifier(self);
	const float quadMultiplier = self->monsterinfo.quadfire_time > level.time ? 1.5f : 1.0f;
	return baseDamage * damageModifier * quadMultiplier;
}



// Fire heatbeam
static void TurretFireHeatbeam(edict_t* self, const vec3_t& start, const vec3_t& dir, trace_t& tr) {
	const int damage = static_cast<int>(CalculateDamage(self, TURRET2_BLASTER_DAMAGE));

	T_Damage(tr.ent, self, self->owner, dir, tr.endpos, tr.plane.normal,
		damage, 0, DAMAGE_ENERGY, MOD_TURRET);

	monster_fire_heatbeam(self, start, dir, vec3_origin,
		self->monsterinfo.quadfire_time > level.time ? 2 : 0,
		10, MZ2_WIDOW2_BEAM_SWEEP_1);
}

// Fire machinegun
static void TurretFireMachinegun(edict_t* self, const vec3_t& start, const vec3_t& dir) {
	if (self->monsterinfo.melee_debounce_time > level.time) {
		return;
	}

	const int damage = static_cast<int>(CalculateDamage(self, TURRET2_BULLET_DAMAGE));
	const float spread_mult = self->monsterinfo.quadfire_time > level.time ? 0.5f : 1.0f;

	if (frandom() < 0.7f)
	T_Damage(self->enemy, self, self->owner, dir, self->enemy->s.origin,
		vec3_origin, damage , damage,DAMAGE_BULLET, MOD_TURRET);


	monster_fire_bullet(self, start, dir, 0, 5,
		DEFAULT_BULLET_HSPREAD * spread_mult,
		DEFAULT_BULLET_VSPREAD * spread_mult,
		MZ2_TURRET_MACHINEGUN);

	self->monsterinfo.melee_debounce_time = level.time +
		(self->monsterinfo.quadfire_time > level.time ? 9_hz : 15_hz);
}

//Fire rocket
static void TurretFireRocket(edict_t* self, const vec3_t& start, const vec3_t& dir, float dist) {
	// Check fire rate
	if (level.time <= self->monsterinfo.last_sentry_missile_fire_time +
		(self->monsterinfo.quadfire_time > level.time ? 0.75_sec : 1.5_sec)) {
		return;
	}

	// Verify clear shot
	const vec3_t offset = { 20.f, 0.f, 0.f };
	vec3_t shot_start;
	if (!M_CheckClearShot(self, offset, shot_start))
		return;

	// Check minimum effective range
	trace_t tr = gi.traceline(start, start + (dir * dist), self, MASK_PROJECTILE);
	if (dist * tr.fraction <= 72) {
		return;
	}

	const float speed = self->monsterinfo.quadfire_time > level.time ? 1600 : 1420;
	vec3_t fire_dir = dir;
	vec3_t target_pos;

	// Enhanced targeting system
	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING)) {
		// Always use prediction for moving targets
		if (self->enemy->velocity.lengthSquared() > 1.0f) {
			// Calculate leading shot with error compensation
			vec3_t predicted_dir, predicted_pos;
			PredictAim(self, self->enemy, start, speed, true, 0, &predicted_dir, &predicted_pos);

			// Verify predicted shot isn't blocked
			trace_t pred_tr = gi.traceline(start, predicted_pos, self, MASK_PROJECTILE);
			if (pred_tr.fraction >= 0.9f) {
				fire_dir = predicted_dir;
				target_pos = predicted_pos;
			}
			else {
				// Fallback to direct targeting with height variation
				target_pos = self->enemy->s.origin;
				target_pos[2] += (self->enemy->absmin[2] < start[2]) ?
					self->enemy->viewheight : self->enemy->viewheight * 0.5f;
				fire_dir = (target_pos - start).normalized();
			}
		}
		else {
			// Stationary target - aim for center mass
			target_pos = self->enemy->s.origin;
			target_pos[2] += self->enemy->viewheight * 0.5f;
			fire_dir = (target_pos - start).normalized();
		}
	}
	else {
		// Refined blindfire spread
		float spread = frandom() < 0.2f ? self->monsterinfo.quadfire_time > level.time ? 0.03f : 0.07f : 0.0f;
		fire_dir[0] += crandom() * spread;
		fire_dir[1] += crandom() * spread;
		fire_dir[2] += crandom() * spread;
		fire_dir = safe_normalized(fire_dir);
	}

	const int damage = static_cast<int>(CalculateDamage(self, 100));
	fire_rocket(self->owner, start, fire_dir, damage, speed, 120, damage);
	self->monsterinfo.last_sentry_missile_fire_time = level.time;
	gi.sound(self, CHAN_VOICE, sound_pew, 1, ATTN_NORM, 0);
}

//Fire plasma
static void TurretFirePlasma(edict_t* self, const vec3_t& start, const vec3_t& dir) {
	// Check fire rate
	if (level.time <= self->monsterinfo.last_sentry_missile_fire_time +
		(self->monsterinfo.quadfire_time > level.time ? 0.8_sec : 2_sec)) {
		return;
	}

	// Verify clear shot
	const vec3_t offset = { 20.f, 0.f, 0.f };
	vec3_t shot_start;
	if (!M_CheckClearShot(self, offset, shot_start))
		return;

	const float projectileSpeed = self->monsterinfo.quadfire_time > level.time ? 1450 : 1250;
	vec3_t fire_dir = dir;
	vec3_t target_pos;

	// Enhanced targeting system
	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING)) {
		// Plasma has higher velocity, so always use prediction for better accuracy
		vec3_t predicted_dir, predicted_pos;

		// First attempt - predict with full velocity
		PredictAim(self, self->enemy, start, projectileSpeed, true, 0, &predicted_dir, &predicted_pos);

		// Check if prediction is valid
		trace_t pred_tr = gi.traceline(start, predicted_pos, self, MASK_PROJECTILE);
		if (pred_tr.fraction >= 0.9f) {
			fire_dir = predicted_dir;
			target_pos = predicted_pos;
		}
		else {
			// Alternative prediction - try hitting lower
			PredictAim(self, self->enemy, start, projectileSpeed, false, 0, &predicted_dir, &predicted_pos);
			pred_tr = gi.traceline(start, predicted_pos, self, MASK_PROJECTILE);

			if (pred_tr.fraction >= 0.9f) {
				fire_dir = predicted_dir;
				target_pos = predicted_pos;
			}
			else {
				// Final fallback - direct targeting
				target_pos = self->enemy->s.origin;
				target_pos[2] += self->enemy->viewheight * 0.5f;
				fire_dir = (target_pos - start).normalized();
			}
		}
	}
	else {
		// Refined blindfire spread - tighter for plasma due to higher velocity
		float spread = frandom() < 0.2f ? self->monsterinfo.quadfire_time > level.time ? 0.02f : 0.04f : 0.0f;
		fire_dir[0] += crandom() * spread;
		fire_dir[1] += crandom() * spread;
		fire_dir[2] += crandom() * spread;
		fire_dir = safe_normalized(fire_dir);
	}

	const int damage = static_cast<int>(CalculateDamage(self, 100));
	fire_plasma(self->owner, start, fire_dir, damage, projectileSpeed, 120, damage);
	self->monsterinfo.last_sentry_missile_fire_time = level.time;
	gi.sound(self, CHAN_VOICE, sound_pew, 1, ATTN_NORM, 0);
}

void turret2Fire(edict_t* self) {
	if (!self || !self->inuse)
		return;

	// Check powerups and inherit from owner
	if (self->owner && self->owner->client) {
		TurretRespondPowerup(self, self->owner);
	}

	// Update aim
	turret2Aim(self);

// Validate enemy
	if (!self->enemy || !self->enemy->inuse ||
		OnSameTeam(self, self->enemy) || self->enemy->deadflag) {
		if (self->monsterinfo.search_time < level.time) {
			if (!FindMTarget(self))
				return;
			self->monsterinfo.search_time = level.time + 300_ms;
		}
		return; // Return if we don't have a valid enemy and couldn't find one
	}

	self->monsterinfo.attack_finished = level.time;

	// Determine target point
	vec3_t end = (self->monsterinfo.aiflags & AI_LOST_SIGHT) ?
		self->monsterinfo.blind_fire_target :
		self->enemy->s.origin;

	if (!(self->monsterinfo.aiflags & AI_LOST_SIGHT)) {
		if (self->enemy->client)
			end[2] += self->enemy->viewheight;
		else
			end[2] += 7;
	}

	// Calculate direction and validate
	vec3_t start = self->s.origin;
	vec3_t dir = end - start;
	if (!is_valid_vector(dir))
		return;
	dir.normalize();

	// Check firing angle
	vec3_t forward;
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	const float chance = dir.dot(forward);
	if (chance < 0.98f)
		return;

	// Calculate distance for weapon selection
	const float dist = (end - start).length();

	// Trace to target
	trace_t tr = gi.traceline(start, end, self, MASK_PROJECTILE);
	if (tr.ent != self->enemy && tr.ent != world)
		return;

	// Fire appropriate weapon
	if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN)) {
		// Handle machinegun state
		if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME)) {
			self->monsterinfo.aiflags |= AI_HOLD_FRAME;
			self->monsterinfo.duck_wait_time = level.time +
				(self->monsterinfo.quadfire_time > level.time ? 3_sec : 5_sec);
			self->monsterinfo.next_duck_time = level.time + 0.1_sec;
			gi.sound(self, CHAN_VOICE, gi.soundindex("weapons/chngnu1a.wav"), 1, ATTN_NORM, 0);
		}
		else if (self->monsterinfo.next_duck_time < level.time) {
			TurretFireMachinegun(self, start, dir);
			TurretFireRocket(self, start, dir, dist);
		}
	}
	else if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER)) {
		vec3_t offset = { 20.f, 0.f, 0.f };
		const vec3_t hbstart = start + (forward * offset[0]);

		vec3_t predictedDir;
		PredictAim(self, self->enemy, hbstart, 9999, false,
			self->monsterinfo.quadfire_time > level.time ? 0.01f : 0.03f,
			&predictedDir, nullptr);

		if (is_valid_vector(predictedDir)) {
			trace_t hbtr = gi.traceline(hbstart, hbstart + predictedDir * 8192,
				self, MASK_PROJECTILE);

			if (hbtr.ent == self->enemy || hbtr.ent == world) {
				TurretFireHeatbeam(self, hbstart, predictedDir, hbtr);
				TurretFirePlasma(self, hbstart, predictedDir);
			}
		}
	}
}

// PMM
void turret2FireBlind(edict_t* self)
{
	vec3_t forward;
	vec3_t start, end, dir;
	float  chance;
	//int	   rocketSpeed = 550;

	turret2Aim(self);

	if (!self->enemy || !self->enemy->inuse)
		return;

	dir = self->monsterinfo.blind_fire_target - self->s.origin;
	dir.normalize();
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	chance = dir.dot(forward);
	if (chance < 0.98f)
		return;

	//if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
	//	rocketSpeed = 800;
	//else
	//	rocketSpeed = 0;

	start = self->s.origin;
	end = self->monsterinfo.blind_fire_target;

	if (self->enemy->s.origin[2] < self->monsterinfo.blind_fire_target[2])
		end[2] += self->enemy->mins[2] - 10;
	else
		end[2] += self->enemy->mins[2] - 10;

	dir = end - start;

	dir.normalize();

	if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
	{
		// Aplica el daño con el mod_t configurado
		monster_fire_heatbeam(self, start, forward, vec3_origin, 1, 50, MZ2_TURRET_BLASTER);
	}
}
// pmm

mframe_t turret2_frames_fire[] = {
	{ ai_run, 0, turret2Aim },
	{ ai_run, 0, turret2Fire },
	{ ai_run, 0, turret2Fire },
	{ ai_run, 0, turret2Fire },
};
MMOVE_T(turret2_move_fire) = { FRAME_pow01, FRAME_pow04, turret2_frames_fire, turret2_run };

// PMM

// the blind frames need to aim first
mframe_t turret2_frames_fire_blind[] = {
	{ ai_run, 0, turret2Aim },
	{ ai_run, 0, turret2FireBlind },
	{ ai_run, 0, turret2FireBlind },
	{ ai_run, 0, turret2FireBlind }

};
MMOVE_T(turret2_move_fire_blind) = { FRAME_pow01, FRAME_pow04, turret2_frames_fire_blind, turret2_run };
// pmm

MONSTERINFO_ATTACK(turret2_attack) (edict_t* self) -> void
{
	if (self->s.frame < FRAME_run01)
	{
		turret2_ready_gun(self);
	}
	else if (self->monsterinfo.attack_state != AS_BLIND)
	{
		M_SetAnimation(self, &turret2_move_fire);
	}
	else
	{
		// No delays or probabilities, directly set the blind fire animation
		if (!self->monsterinfo.blind_fire_target)
			return;

		M_SetAnimation(self, &turret2_move_fire_blind);
	}

	// pmm
}

void TurretSparks(edict_t* self)
{
	if (!self || !self->inuse)
		return;

	if (self->health <= (self->max_health / 2)) {
		if (level.time >= self->monsterinfo.next_duck_time) {
			vec3_t forward, right, up;
			AngleVectors(self->s.angles, forward, right, up);

			// Calculate spark origin using offset
			const	vec3_t spark_origin = self->s.origin + (forward * 18.0f);

			vec3_t dir;
			if (!self->enemy) {
				dir = { crandom(), crandom(), crandom() };
				dir.normalize();
			}
			else {
				dir = (spark_origin - self->enemy->s.origin).normalized();
			}

			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_SPLASH);
			gi.WriteByte(32);
			gi.WritePosition(spark_origin);
			gi.WriteDir(dir);
			gi.WriteByte(SPLASH_SPARKS);
			gi.multicast(spark_origin, MULTICAST_PVS, false);

			self->monsterinfo.next_duck_time = level.time + random_time(2_sec, 4.5_sec);
		}
	}
}

// **********************
//  PAIN
// **********************

PAIN(turret2_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;
	gi.sound(self, CHAN_VOICE, gi.soundindex("tank/tnkpain2.wav"), 1, ATTN_NORM, 0);

	// Calculate spark origin with offset
	vec3_t forward, right, up;
	AngleVectors(self->s.angles, forward, right, up);
	vec3_t spark_origin = self->s.origin + (forward * 20.0f);

	// Create spark effect for heavy hits (damage >= 40)
	if (damage >= 20) {
		const vec3_t dir = (spark_origin - (other ? other->s.origin : spark_origin)).normalized();

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_SPLASH);
		gi.WriteByte(32);
		gi.WritePosition(spark_origin);
		gi.WriteDir(dir);
		gi.WriteByte(SPLASH_SPARKS);
		gi.multicast(spark_origin, MULTICAST_PVS, false);
	}

	self->enemy = other;

	// Call periodic sparks function
	TurretSparks(self);
}

// **********************
//  DEATH
// **********************

DIE(turret2_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{

	// Handle summoned entity notifications
	if (self->monsterinfo.issummoned && self->owner && self->owner->client) {
		if (strcmp(self->classname, "monster_sentrygun") == 0) {
			gi.Client_Print(self->owner, PRINT_HIGH, "Your sentry gun was destroyed.\n");
			self->owner->client->num_sentries--;
		}
		//else if (strstr(self->classname, "monster_") &&
		//	strcmp(self->classname, "monster_sentrygun") != 0) {
		//	gi.Client_Print(self->owner, PRINT_HIGH, "Your Summoned Strogg was defeated!\n");
		//	self->owner->client->num_sentries--;
		//}
	}
	//OnEntityDeath(self);
	vec3_t forward;
	edict_t* base;

	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	self->s.origin += (forward * 1);

	ThrowGibs(self, 1, {
		{ 2, "models/objects/debris1/tris.md2", GIB_METALLIC | GIB_DEBRIS }
		});

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_BFG_BIGEXPLOSION);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PHS, false);

	if (self->teamchain)
	{
		base = self->teamchain;
		base->solid = SOLID_NOT;
		base->takedamage = false;
		base->movetype = MOVETYPE_NONE;
		base->teammaster = base;
		base->teamchain = nullptr;
		base->flags &= ~FL_TEAMSLAVE;
		base->flags |= FL_TEAMMASTER;
		gi.linkentity(base);

		self->teammaster = self->teamchain = nullptr;
		self->flags &= ~(FL_TEAMSLAVE | FL_TEAMMASTER);
	}

	if (self->target)
	{
		if (self->enemy && self->enemy->inuse)
			G_UseTargets(self, self->enemy);
		else
			G_UseTargets(self, self);
	}

	// Limpiar el efecto de brillo
	if (self->target_hint_chain && self->target_hint_chain->inuse) {
		G_FreeEdict(self->target_hint_chain);
		self->target_hint_chain = nullptr;
	}

	if (self->target_ent)
	{
		G_FreeEdict(self->target_ent);
		self->target_ent = nullptr;
	}

	edict_t* gib = ThrowGib(self, "models/monsters/turret/tris.md2", damage, GIB_SKINNED | GIB_METALLIC | GIB_HEAD | GIB_DEBRIS, self->s.scale);
	gib->s.frame = 14;

	// Si la torreta murió porque su propietario desapareció
	if (!self->owner || !self->owner->inuse)
	{
		self->think = G_FreeEdict;
		self->nextthink = level.time + 0_sec;
	}
}


// **********************
//  WALL SPAWN
// **********************

void turret2_wall_spawn(edict_t* turret)
{
	edict_t* ent;
	int		 angle;

	ent = G_Spawn();
	ent->s.origin = turret->s.origin;
	ent->s.angles = turret->s.angles;

	angle = (int)ent->s.angles[1];
	if (ent->s.angles[0] == 90)
		angle = -1;
	else if (ent->s.angles[0] == 270)
		angle = -2;
	switch (angle)
	{
	case -1:
		ent->mins = { -16, -16, -8 };
		ent->maxs = { 16, 16, 0 };
		break;
	case -2:
		ent->mins = { -16, -16, 0 };
		ent->maxs = { 16, 16, 8 };
		break;
	case 0:
		ent->mins = { -8, -16, -16 };
		ent->maxs = { 0, 16, 16 };
		break;
	case 90:
		ent->mins = { -16, -8, -16 };
		ent->maxs = { 16, 0, 16 };
		break;
	case 180:
		ent->mins = { 0, -16, -16 };
		ent->maxs = { 8, 16, 16 };
		break;
	case 270:
		ent->mins = { -16, 0, -16 };
		ent->maxs = { 16, 8, 16 };
		break;
	}

	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_NOT;

	ent->teammaster = turret;
	turret->flags |= FL_TEAMMASTER;
	turret->teammaster = turret;
	turret->teamchain = ent;
	ent->teamchain = nullptr;
	ent->flags |= FL_TEAMSLAVE;
	ent->owner = turret;

	ent->s.modelindex = gi.modelindex("models/monsters/turretbase/tris.md2");

	gi.linkentity(ent);
}

MOVEINFO_ENDFUNC(turret2_wake) (edict_t* ent) -> void
{
	// the wall section will call this when it stops moving.
	// just return without doing anything. easiest way to have a null function.
	if (ent->flags & FL_TEAMSLAVE)
	{
		ent->s.sound = 0;
		return;
	}

	ent->monsterinfo.stand = turret2_stand;
	ent->monsterinfo.walk = turret2_walk;
	ent->monsterinfo.run = turret2_run;
	ent->monsterinfo.dodge = nullptr;
	ent->monsterinfo.attack = turret2_attack;
	ent->monsterinfo.melee = nullptr;
	ent->monsterinfo.sight = turret2_sight;
	ent->monsterinfo.search = turret2_search;
	M_SetAnimation(ent, &turret2_move_stand);
	ent->takedamage = true;
	ent->movetype = MOVETYPE_NONE;
	// prevent counting twice
	ent->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	gi.linkentity(ent);

	stationarymonster_start(ent, spawn_temp_t::empty);

	if (ent->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN))
	{
		ent->s.skinnum = 1;
	}
}

USE(turret2_activate) (edict_t* self, edict_t* other, edict_t* activator) -> void
{
	vec3_t	 endpos;
	vec3_t	 forward = { 0, 0, 0 };
	edict_t* base;

	self->movetype = MOVETYPE_NONE;
	if (!self->speed)
		self->speed = 15;
	self->moveinfo.speed = self->speed;
	self->moveinfo.accel = self->speed;
	self->moveinfo.decel = self->speed;

	if (self->s.angles[0] == 270)
	{
		forward = { 0, 0, 1 };
	}
	else if (self->s.angles[0] == 90)
	{
		forward = { 0, 0, -1 };
	}
	else if (self->s.angles[1] == 0)
	{
		forward = { 1, 0, 0 };
	}
	else if (self->s.angles[1] == 90)
	{
		forward = { 0, 1, 0 };
	}
	else if (self->s.angles[1] == 180)
	{
		forward = { -1, 0, 0 };
	}
	else if (self->s.angles[1] == 270)
	{
		forward = { 0, -1, 0 };
	}

	// start up the turret
	endpos = self->s.origin + (forward * 32);
	Move_Calc(self, endpos, turret2_wake);

	base = self->teamchain;
	if (base)
	{
		base->movetype = MOVETYPE_PUSH;
		base->speed = self->speed;
		base->moveinfo.speed = base->speed;
		base->moveinfo.accel = base->speed;
		base->moveinfo.decel = base->speed;

		// start up the wall section
		endpos = self->teamchain->s.origin + (forward * 32);
		Move_Calc(self->teamchain, endpos, turret2_wake);

		base->s.sound = sound_moving;
		base->s.loop_attenuation = ATTN_NORM;
	}
}
// PMM

MONSTERINFO_CHECKATTACK(turret2_checkattack) (edict_t* self) -> bool
{
	if (!self->enemy || self->enemy->health <= 0)
		return false;

	vec3_t spot1 = self->s.origin;
	spot1[2] += self->viewheight;
	vec3_t spot2 = self->enemy->s.origin;
	spot2[2] += self->enemy->client ? self->enemy->viewheight :
		(self->enemy->maxs[2] - self->enemy->mins[2]) * self->enemy->s.scale;

	trace_t const tr = gi.traceline(spot1, spot2, self,
		MASK_SOLID | CONTENTS_SLIME | CONTENTS_LAVA);

	if (tr.fraction < 1.0f && tr.ent != self->enemy)
		return false;

	float const range = range_to(self, self->enemy);
	float chance = range <= RANGE_NEAR ? 1.2f : 0.6f;
	chance += (self->enemy->s.scale < 1.0f) ? 0.2f : 0.0f;

	if (frandom() < chance)
	{
		self->monsterinfo.attack_state = AS_MISSILE;
		self->monsterinfo.attack_finished = level.time + 50_ms;
		return true;
	}

	return false;
}
// **********************
//  SPAWN
// **********************

/*QUAKED monster_sentrygun (1 .5 0) (-16 -16 -16) (16 16 16) Ambush Trigger_Spawn Sight Blaster MachineGun Rocket Heatbeam WallUnit

The automated defense turret that mounts on walls.
Check the weapon you want it to use: blaster, machinegun, rocket, heatbeam.
Default weapon is blaster.
When activated, wall units move 32 units in the direction they're facing.
*/

static THINK(EmitSmokeEffect)(edict_t* ent) -> void {
	if (!ent || !ent->owner || !ent->owner->inuse) {
		G_FreeEdict(ent);
		return;
	}

	// Solo emitir humo con una probabilidad del 40%
	if (frandom() < 0.4f) {
		// Escribir el efecto de humo
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_CHAINFIST_SMOKE);
		gi.WritePosition(ent->s.origin);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);
	}

	// Configurar el próximo think con tiempo aleatorio
	ent->nextthink = level.time + random_time(2_sec, 5_sec);
	ent->think = EmitSmokeEffect;
}

void CreateTurretGlowEffect(edict_t* turret) {
	if (!turret || !turret->inuse)
		return;

	// Eliminar el efecto anterior si existe
	if (turret->target_hint_chain && turret->target_hint_chain->inuse) {
		G_FreeEdict(turret->target_hint_chain);
		turret->target_hint_chain = nullptr;
	}

	edict_t* smoke = G_Spawn();
	if (!smoke)
		return;

	smoke->movetype = MOVETYPE_NONE;
	smoke->solid = SOLID_NOT;
	smoke->s.modelindex = 0;  // No necesitamos modelo para el efecto de humo
	smoke->s.renderfx = RF_FULLBRIGHT;
	smoke->s.effects = EF_BOB;  // Efecto de bobbing
	smoke->owner = turret;
	smoke->classname = "turret_smoke";
	smoke->think = EmitSmokeEffect;
	smoke->nextthink = level.time + random_time(8_sec, 15_sec);  // Inicio retrasado aleatorio

	// Posicionar el emisor de humo usando el nuevo vec3_t
	vec3_t forward;
	AngleVectors(turret->s.angles, forward, nullptr, nullptr);

	// Usar la nueva sintaxis de vec3_t para el posicionamiento
	smoke->s.origin = turret->s.origin + (forward * 20.0f);
	smoke->s.angles = turret->s.angles;

	gi.linkentity(smoke);
	turret->target_hint_chain = smoke;
}
void SP_monster_sentrygun(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	//self->spawnflags.has(SPAWNFLAG_TURRET2_WALL_UNIT);

	// Al crear la torreta, verificar si el owner tiene power-ups activos
	if (self->owner && self->owner->client) {
		TurretRespondPowerup(self, self->owner);
	}


#define playeref self->owner->s.effects;
	self->monsterinfo.last_sentry_missile_fire_time = gtime_t::from_sec(0); // Inicializa el tiempo de último disparo de cohete
	int angle;

	self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
	self->monsterinfo.attack_state = AS_BLIND;

	//test EF grenade
	self->s.effects = EF_BOB; // Quitar EF_GRENADE de aquí
	self->target_hint_chain = nullptr; // Inicializar el puntero del efecto
	// Crear el efecto visual después de establecer la posición y ángulos
	CreateTurretGlowEffect(self);

	ApplyMonsterBonusFlags(self);

	if (!M_AllowSpawn(self))
	{
		G_FreeEdict(self);
		return;
	}

	// pre-caches
	sound_pew.assign("makron/blaster.wav");
	sound_moved.assign("gunner/gunidle1.wav");
	sound_moving.assign("turret/moving.wav");
	gi.modelindex("models/objects/debris1/tris.md2");

	self->s.modelindex = gi.modelindex("models/monsters/turret/tris.md2");
	self->mins = { -12, -12, -12 };
	self->maxs = { 12, 12, 12 };
	self->movetype = MOVETYPE_NONE;




	self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
	self->monsterinfo.power_armor_power = 100;
	self->health = 80 * st.health_multiplier;;
	self->gib_health = -100;
	self->mass = 100;
	self->yaw_speed = 16;
	self->solid = SOLID_BBOX;
	//self->svflags = SVF_PLAYER;
	self->flags |= FL_MECHANICAL;
	self->pain = turret2_pain;
	self->die = turret2_die;

	// map designer didn't specify weapon type. set it now.
	const float randomValue = frandom();

	// Si el valor aleatorio es menor que 0.3, selecciona HEATBEAM; de lo contrario, MACHINEGUN
	if (!self->spawnflags.has(SPAWNFLAG_TURRET2_WEAPONCHOICE)) {
		if (randomValue < 0.3f) {
			self->spawnflags |= SPAWNFLAG_TURRET2_HEATBEAM;
		}
		else {
			self->spawnflags |= SPAWNFLAG_TURRET2_MACHINEGUN;
		}
	}



	if (self->spawnflags.has(SPAWNFLAG_TURRET2_HEATBEAM))
	{
		self->spawnflags &= ~SPAWNFLAG_TURRET2_HEATBEAM;
		self->spawnflags |= SPAWNFLAG_TURRET2_BLASTER;
	}

	if (!self->spawnflags.has(SPAWNFLAG_TURRET2_WALL_UNIT))
	{
		self->monsterinfo.stand = turret2_stand;
		self->monsterinfo.walk = turret2_walk;
		self->monsterinfo.run = turret2_run;
		self->monsterinfo.dodge = nullptr;
		self->monsterinfo.attack = turret2_attack;
		self->monsterinfo.melee = nullptr;
		self->monsterinfo.sight = turret2_sight;
		self->monsterinfo.search = turret2_search;
		M_SetAnimation(self, &turret2_move_stand);
	}

	// PMM
	self->monsterinfo.checkattack = turret2_checkattack;

	self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
	self->monsterinfo.scale = MODEL_SCALE;
	self->gravity = 0;

	self->offset = self->s.angles;
	angle = (int)self->s.angles[1];
	switch (angle)
	{
	case -1: // up
		self->s.angles[0] = 270;
		self->s.angles[1] = 0;
		self->s.origin[2] += 2;
		break;
	case -2: // down
		self->s.angles[0] = 90;
		self->s.angles[1] = 0;
		self->s.origin[2] -= 2;
		break;
	case 0:
		self->s.origin[0] += 2;
		break;
	case 90:
		self->s.origin[1] += 2;
		break;
	case 180:
		self->s.origin[0] -= 2;
		break;
	case 270:
		self->s.origin[1] -= 2;
		break;
	default:
		break;
	}

	gi.linkentity(self);

	if (self->spawnflags.has(SPAWNFLAG_TURRET2_WALL_UNIT))
	{
		if (!self->targetname)
		{
			G_FreeEdict(self);
			return;
		}

		self->takedamage = false;
		self->use = turret2_activate;
		turret2_wall_spawn(self);
		//if (!(self->monsterinfo.aiflags & AI_DO_NOT_COUNT))
		//{
		//	if (g_debug_monster_kills->integer)
		//		level.monsters_registered[level.total_monsters] = self;
		//	level.total_monsters++;
		//}
	}
	else
	{
		stationarymonster_start(self, spawn_temp_t::empty);
		self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
	}

	if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN))
	{
		gi.modelindex("models/objects/laser/tris.md2");
		gi.soundindex("infantry/infatck1.wav");
		gi.soundindex("weapons/chngnu1a.wav");
		gi.soundindex("weapons/rockfly.wav");
		gi.modelindex("models/objects/rocket/tris.md2");
		gi.soundindex("chick/chkatck2.wav");
		gi.soundindex("tank/tnkpain2.wav");
		gi.soundindex("makron/blaster.wav");
		gi.soundindex("gunner/gunidle1.wav");

		self->s.skinnum = 2;

		self->spawnflags &= ~SPAWNFLAG_TURRET2_WEAPONCHOICE;
		self->spawnflags |= SPAWNFLAG_TURRET2_MACHINEGUN;
	}

	else
	{
		gi.modelindex("models/objects/laser/tris.md2");
		gi.soundindex("misc/lasfly.wav");
		gi.soundindex("soldier/solatck2.wav");
		gi.soundindex("tank/tnkpain2.wav");
		gi.soundindex("makron/blaster.wav");
		gi.soundindex("gunner/gunidle1.wav");

		self->spawnflags &= ~SPAWNFLAG_TURRET2_WEAPONCHOICE;
		self->spawnflags |= SPAWNFLAG_TURRET2_BLASTER;
	}

	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
		self->yaw_speed = 15;
	if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN | SPAWNFLAG_TURRET2_BLASTER))
		self->monsterinfo.blindfire = true;

	if (self->monsterinfo.quadfire_time > level.time) {
		self->yaw_speed *= 2.0f;
	}

	self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
}