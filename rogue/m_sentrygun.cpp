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

constexpr spawnflags_t SPAWNFLAG_TURRET2_BLASTER = spawnflags_t(0x0008);
constexpr spawnflags_t SPAWNFLAG_TURRET2_MACHINEGUN = spawnflags_t(0x0010);
constexpr spawnflags_t SPAWNFLAG_TURRET2_ROCKET = spawnflags_t(0x0020); // Keep original rocket flag
constexpr spawnflags_t SPAWNFLAG_TURRET2_FLECHETTE = spawnflags_t(0x0100); // New unique valuef
constexpr spawnflags_t SPAWNFLAG_TURRET2_HEATBEAM = SPAWNFLAG_TURRET2_BLASTER; // Same as blaster
constexpr spawnflags_t SPAWNFLAG_TURRET2_WEAPONCHOICE = SPAWNFLAG_TURRET2_HEATBEAM | SPAWNFLAG_TURRET2_MACHINEGUN | SPAWNFLAG_TURRET2_ROCKET | SPAWNFLAG_TURRET2_FLECHETTE;
constexpr spawnflags_t SPAWNFLAG_TURRET2_NO_LASERSIGHT = spawnflags_t(1 << 18);

void turret2_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
void turret2Aim(edict_t* self);
void turret2_ready_gun(edict_t* self);
void turret2_run(edict_t* self);
void TurretSparks(edict_t* self);

// animation frames for cooling down after combat
mframe_t turret2_frames_cool_down[] = {
	{ ai_stand, 0, turret2Aim },  // Still aiming during cooldown
	{ ai_stand, 0, turret2Aim },  // Keep aiming to track new targets
	{ ai_stand, 0, turret2Aim },
	{ ai_stand, 0, turret2Aim }
};
//  Corrected the start and end frames to define a valid 4-frame sequence.
MMOVE_T(turret2_move_cool_down) = { FRAME_pow01, FRAME_pow04, turret2_frames_cool_down, turret2_run };

extern const mmove_t turret2_move_fire;
extern const mmove_t turret2_move_fire_blind;

static cached_soundindex sound_moved, sound_moving, sound_pew;

// Add new sound caches for grenade launcher and flechette
static cached_soundindex sound_grenade_launcher, sound_flechette;

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
    // Get a pointer to our custom state for this entity
    sentry_state_t* state = self->monsterinfo.sentry_state;
    if (!state) {
        return; // Safety check
    }

	// Check for enemy changes
	if (!self->enemy || !self->enemy->inuse) {
		// Lost target but didn't transition yet
		if (state->previous_enemy && state->transition_state != 3 && state->was_attacking) {
			// Enter cooldown state
			state->transition_state = 3;
			state->last_target_time = level.time;

			if (self->monsterinfo.active_move != &turret2_move_cool_down)
				M_SetAnimation(self, &turret2_move_cool_down);
		}
	}

 	if (self->enemy && self->enemy != state->previous_enemy) {
		// Target changed
        state->last_enemy_change_time = level.time;
	}

	if (!self->enemy || self->enemy == world)
	{
		// Use the state's next_target_search_time
		if (level.time >= state->next_target_search_time) {
			if (!FindMTarget(self)) {
				state->next_target_search_time = level.time + 300_ms;
				return;
			}
			state->next_target_search_time = level.time + 100_ms;
			self->monsterinfo.search_time = level.time + 300_ms;
		}
		else {
			return;
		}
	}

	// Update previous enemy
	state->previous_enemy = self->enemy;

	// Actualizar la posición del efecto visual
	UpdateSmokePosition(self);

	TurretSparks(self);

	//  Check if we have an enemy but haven't been able to attack
	if (self->enemy && self->enemy->inuse) {
		// If we haven't attacked for more than 1 second, consider changing targets
		// Use attack_finished to check when we last attempted to fire
		if (self->monsterinfo.attack_finished + 1_sec < level.time &&
			self->monsterinfo.last_sentry_missile_fire_time + 2_sec < level.time) {

			// Try to find a better target
			if (FindMTarget(self)) {
				// Reset attack finished time if we found a new target
				self->monsterinfo.attack_finished = level.time;
			}
		}
	}

	vec3_t end, dir;
	vec3_t ang;
	float  move, idealPitch, idealYaw, current, speed;
	int    orientation;

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

		// Add predictive lead aiming if target is moving
		if (self->enemy->velocity.lengthSquared() > 1.0f) {
			float dist = (end - self->s.origin).length();
			float projectile_speed = 1000.0f; // Adjust based on weapon type
			float lead_time = dist / projectile_speed;

			// Add predicted movement to target position
			end[0] += self->enemy->velocity[0] * lead_time;
			end[1] += self->enemy->velocity[1] * lead_time;
			end[2] += self->enemy->velocity[2] * lead_time;
		}
	}

	dir = end - self->s.origin;
	ang = vectoangles(dir);


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
	//
	// Calculate dynamic speed based on angle difference
	//
	float base_speed = self->yaw_speed / (gi.tick_rate / 10);

	//
	// adjust pitch - improved movement calculation
	//
	current = self->s.angles[PITCH];
	float pitchDiff = idealPitch - current;

	// Normalize angle difference for shortest path rotation
	while (pitchDiff > 180)
		pitchDiff -= 360;
	while (pitchDiff < -180)
		pitchDiff += 360;

	// Only move if difference is significant to avoid jitter
	if (fabs(pitchDiff) > 0.1f)
	{
		// Calculate appropriate speed based on difference
		float absAngleDiff = fabs(pitchDiff);

		// Adjust speed based on angle difference
		if (absAngleDiff < 3.0f)
			speed = base_speed * 0.5f; // Slower for small adjustments
		else if (absAngleDiff < 30.0f)
			speed = base_speed; // Normal speed for medium adjustments
		else
			speed = base_speed * 1.5f; // Faster for large adjustments

		if (pitchDiff > 0)
		{
			move = (pitchDiff < speed) ? pitchDiff : speed;
		}
		else
		{
			move = (pitchDiff > -speed) ? pitchDiff : -speed;
		}

		// Apply slowdown near target angle
		float slowdownFactor = 1.0f;
		if (fabs(pitchDiff) < 10.0f)
			slowdownFactor = fabs(pitchDiff) / 10.0f;

		if (slowdownFactor < 0.2f)
			slowdownFactor = 0.2f;

		move *= slowdownFactor;

		// Normalize final angle
		float newAngle = current + move;
		while (newAngle > 360.0f)
			newAngle -= 360.0f;
		while (newAngle < 0.0f)
			newAngle += 360.0f;

		self->s.angles[PITCH] = newAngle;
	}

	//
	// adjust yaw - improved movement calculation
	//
	current = self->s.angles[YAW];
	float yawDiff = idealYaw - current;

	// Normalize angle difference for shortest path rotation
	while (yawDiff > 180)
		yawDiff -= 360;
	while (yawDiff < -180)
		yawDiff += 360;

	// Only move if difference is significant to avoid jitter
	if (fabs(yawDiff) > 0.1f)
	{
		// Calculate appropriate speed based on difference
		float absAngleDiff = fabs(yawDiff);

		// Adjust speed based on angle difference
		if (absAngleDiff < 3.0f)
			speed = base_speed * 0.5f; // Slower for small adjustments
		else if (absAngleDiff < 30.0f)
			speed = base_speed; // Normal speed for medium adjustments
		else
			speed = base_speed * 1.5f; // Faster for large adjustments

		if (yawDiff > 0)
		{
			move = (yawDiff < speed) ? yawDiff : speed;
		}
		else
		{
			move = (yawDiff > -speed) ? yawDiff : -speed;
		}

		// Apply slowdown near target angle
		float slowdownFactor = 1.0f;
		if (fabs(yawDiff) < 10.0f)
			slowdownFactor = fabs(yawDiff) / 10.0f;

		if (slowdownFactor < 0.2f)
			slowdownFactor = 0.2f;

		move *= slowdownFactor;

		// Normalize final angle
		float newAngle = current + move;
		while (newAngle > 360.0f)
			newAngle -= 360.0f;
		while (newAngle < 0.0f)
			newAngle += 360.0f;

		self->s.angles[YAW] = newAngle;
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

	// Adjust laser sight behavior based on visibility 
	float scan_range = 64.f;

	// More precise aiming when the target is visible
	if (visible(self, self->enemy)) {
		scan_range = 8.f; // Tighter pattern when target is visible

		// Check if target is stationary - make even more precise
		if (self->enemy->velocity.lengthSquared() < 1.0f)
			scan_range = 4.f;
	}

	// Smoother laser pattern with improved sine wave calculation
	float timeBase = level.time.seconds();

	// Use different frequencies to create more natural movement
	tr.endpos[0] += sinf(timeBase * 1.1f + self->s.number) * scan_range;
	tr.endpos[1] += cosf((timeBase * 1.3f - self->s.number) * 3.0f) * scan_range;
	tr.endpos[2] += sinf((timeBase * 1.7f - self->s.number) * 2.5f) * scan_range;

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

mframe_t turret2_frames_run[] = {
	{ ai_run, 0, turret2Aim },
	{ ai_run, 0, turret2Aim }
};
MMOVE_T(turret2_move_run) = { FRAME_run01, FRAME_run02, turret2_frames_run, turret2_run };

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


void CreateTurretGlowEffect(edict_t* turret);

MONSTERINFO_RUN(turret2_run) (edict_t* self) -> void
{

	if (!self->owner || !self->owner->inuse)
	{
		turret2_die(self, self, self, 100, self->s.origin, MOD_UNKNOWN);
		return;
	}

	CreateTurretGlowEffect(self);

    sentry_state_t* state = self->monsterinfo.sentry_state;
    if (!state) return;

	// Check if we have an enemy
	bool has_valid_enemy = (self->enemy && self->enemy->inuse &&
		self->enemy->health > 0 && !self->enemy->deadflag);

	// Detect enemy changes
	if (has_valid_enemy && self->enemy != state->previous_enemy) {
		state->last_enemy_change_time = level.time;
		state->previous_enemy = self->enemy;
	}

	// Track attacking state changes
	bool currently_attacking = has_valid_enemy &&
		(self->monsterinfo.attack_finished > level.time - 500_ms);

	// Sentry regeneration when out of combat
	if (self->owner && self->owner->client && !currently_attacking) {
		// Only regenerate if health is above 30% of max health
		int health_threshold = (int)(self->max_health * 0.3f);
		if (self->health > health_threshold) {
			// Regenerate +5% health and power armor every 2 seconds
			if (level.time >= state->last_regeneration_time + 2_sec) {
				// Regenerate 5% health
				int health_regen = (int)(self->max_health * 0.05f);
				if (health_regen > 0) {
					self->health += health_regen;
					if (self->health > self->max_health) {
						self->health = self->max_health;
					}
				}

				// Regenerate 5% power armor
				if (self->monsterinfo.power_armor_type != IT_NULL) {
					int max_power_armor = static_cast<int>(round(self->max_health * 0.4f));
					int armor_regen = (int)(max_power_armor * 0.05f); // 5% of max power armor
					if (armor_regen > 0) {
						self->monsterinfo.power_armor_power += armor_regen;
						if (self->monsterinfo.power_armor_power > max_power_armor) {
							self->monsterinfo.power_armor_power = max_power_armor;
						}
					}
				}

				state->last_regeneration_time = level.time;
			}
		}
	}

	// Handle transitions between states
	if (currently_attacking) {
		state->last_target_time = level.time;
		state->was_attacking = true;
		state->transition_state = 2; // active state
	}
	else if (state->was_attacking) {
		if (state->transition_state != 3) {
			state->transition_state = 3; // cooling down state
			if (self->monsterinfo.active_move != &turret2_move_cool_down) {
				M_SetAnimation(self, &turret2_move_cool_down);
				return;
			}
		}

		gtime_t cooldown_time = has_valid_enemy ? 800_ms : 1_sec;
		if (level.time > state->last_target_time + cooldown_time) {
			state->was_attacking = false;
			state->transition_state = 2; // back to active state
		}
	}

	// Use normal run animation for active state
	if (self->s.frame < FRAME_run01) {
		turret2_ready_gun(self);
	}
	else {
		self->monsterinfo.aiflags |= AI_HIGH_TICK_RATE;

		if (state->transition_state != 3) {
			M_SetAnimation(self, &turret2_move_run);
		}

		if (self->monsterinfo.weapon_sound) {
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

// **********************
//  ATTACK
// **********************


int32_t TURRET2_BLASTER_DAMAGE = 3;
int32_t TURRET2_BULLET_DAMAGE = 3;
//bool M_CheckClearShot(edict_t* self, const vec3_t& offset, vec3_t& start);

// Common helper function for damage calculation
static float CalculateDamage(edict_t* self, int baseDamage) {
	// Only apply the standard damage modifier from M_DamageModifier
	// which already handles quad_time (4x) and double_time (2x)
	const float damageModifier = M_DamageModifier(self);

	// Remove the quadfire multiplier for damage calculations
	// const float quadMultiplier = self->monsterinfo.quadfire_time > level.time ? 1.5f : 1.0f;

	return baseDamage * damageModifier;
}
// Fire heatbeam
static void TurretFireHeatbeam(edict_t* self, const vec3_t& start, const vec3_t& dir, trace_t& tr) {
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	const int damage = static_cast<int>(CalculateDamage(self, TURRET2_BLASTER_DAMAGE));
	// Fire heatbeam with continuous flag - let the heatbeam entity handle persistence
	monster_fire_heatbeam(self, start, dir, vec3_origin, damage, 10, MZ2_WIDOW2_BEAM_SWEEP_1);
}

// Fire machinegun
static void TurretFireMachinegun(edict_t* self, const vec3_t& start, const vec3_t& dir) {
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	if (self->monsterinfo.melee_debounce_time > level.time) {
		return;
	}

	const int damage = static_cast<int>(CalculateDamage(self, TURRET2_BULLET_DAMAGE));
	const float spread_mult = self->monsterinfo.quadfire_time > level.time ? 0.4f : 0.7f;


	monster_fire_bullet(self, start, dir, damage, 8, DEFAULT_BULLET_HSPREAD * spread_mult, DEFAULT_BULLET_VSPREAD * spread_mult, MZ2_TURRET_MACHINEGUN);

	self->monsterinfo.melee_debounce_time = level.time +
		(self->monsterinfo.quadfire_time > level.time ? 9_hz : 15_hz);
}

//Fire rocket
static void TurretFireRocket(edict_t* self, const vec3_t& start, const vec3_t& dir, float dist) {
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	// Check rate limit for rockets
	if (level.time <= self->monsterinfo.last_sentry_missile_fire_time +
		(self->monsterinfo.quadfire_time > level.time ? 0.75_sec : 1.5_sec)) {
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
	self->monsterinfo.last_sentry_missile_fire_time = level.time; // Reset timer to current time so we can fire again in 1.5/0.75 seconds
	gi.sound(self, CHAN_VOICE, sound_pew, 1, ATTN_NORM, 0);
}

//Fire plasma
static void TurretFirePlasma(edict_t* self, const vec3_t& start, const vec3_t& dir) {
	// Check fire rate - REMOVED

	// Check rate limit for plasma
	if (level.time <= self->monsterinfo.last_sentry_missile_fire_time +
		(self->monsterinfo.quadfire_time > level.time ? 0.8_sec : 2_sec)) {
		return;
	}

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
	self->monsterinfo.last_sentry_missile_fire_time = level.time; // Reset timer to current time
	gi.sound(self, CHAN_VOICE, sound_pew, 1, ATTN_NORM, 0);
}

static void TurretFireFlechette(edict_t* self, const vec3_t& start, const vec3_t& dir) {
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

    sentry_state_t* state = self->monsterinfo.sentry_state;
    if (!state) return;

	// Flechette burst timing logic - REFINED RAPID like ETF rifle
	if (state->flechette_burst_count == 0) {
		// Starting new burst - set target count and reset timing
		state->flechette_burst_target = 5 + (rand() % 4); // Random 5-8 flechettes
		state->last_flechette_burst_time = level.time;
		state->flechette_to_grenade_pause_time = 0_sec; // Reset grenade pause
	}
	else if (level.time < state->last_flechette_burst_time + gtime_t::from_sec(0.04f + (frandom() * 0.06f))) {
		// REFINED RAPID: 0.02-0.05 second intervals between flechettes (slightly slower than before)
		return;
	}

	// Calculate custom muzzle position
	vec3_t forward, right, up;
	AngleVectors(self->s.angles, forward, right, up);
	const vec3_t offset = { 20.f, 0.f, 0.f };
	vec3_t muzzle_pos = G_ProjectSource2(self->s.origin, offset, forward, right, up);

	const int damage = static_cast<int>(CalculateDamage(self, 6));
	const float speed = self->monsterinfo.quadfire_time > level.time ? 4000.0f : 3200.0f;

	// Calculate distance
	float dist = (self->enemy->s.origin - muzzle_pos).length();

	// Improved aiming
	vec3_t aim_dir;

	// Flechettes are fast, so PredictAim works well at all distances
	// But we'll add more careful prediction for moving targets
	if (self->enemy->velocity.lengthSquared() > 1.0f) {
		// Moving target - use prediction with error compensation
		float pred_time = dist / speed;
		vec3_t pred_pos = self->enemy->s.origin + (self->enemy->velocity * pred_time * 0.9f); // 90% compensation

		// Verify predicted position with trace
		trace_t tr = gi.traceline(muzzle_pos, pred_pos, self, MASK_SHOT);
		if (tr.fraction >= 0.9f || tr.ent == self->enemy) {
			aim_dir = (pred_pos - muzzle_pos).normalized();
		}
		else {
			// If prediction is blocked, use standard PredictAim
			PredictAim(self, self->enemy, muzzle_pos, speed, true, 0.0f, &aim_dir, nullptr);
		}
	}
	else {
		// Stationary target - aim with small random variation for realism
		aim_dir = (self->enemy->s.origin - muzzle_pos).normalized();
		aim_dir += right * (crandom() * 0.01f);
		aim_dir.normalize();
	}
	const float spread_energymult = self->monsterinfo.quadfire_time > level.time ? 0.8f : 1.6f;

	// Main shot with improved aiming
	monster_fire_flechette(self, muzzle_pos, aim_dir, damage, speed, MZ2_UNUSED_0);
	monster_fire_energy_bullet(self, start, dir, 1, 8, DEFAULT_BULLET_HSPREAD * spread_energymult, DEFAULT_BULLET_VSPREAD * spread_energymult, MZ2_TURRET_MACHINEGUN);

	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(self);
	gi.WriteByte(frandom() < 0.2f ? MZ_ETF_RIFLE_2 : MZ_ETF_RIFLE);
	gi.multicast(self->s.origin, MULTICAST_PVS, false);
	
	// Secondary spread shots
	if (self->monsterinfo.quadfire_time > level.time || frandom() < 0.4f) {
		// Calculate perpendicular vectors for spread
		vec3_t spread_right;
		AngleVectors(vectoangles(aim_dir), nullptr, &spread_right, nullptr);

		// Right spread
		vec3_t forward_right = aim_dir + (spread_right * 0.02f);
		forward_right.normalize();
		monster_fire_flechette(self, muzzle_pos, aim_dir, damage, speed, MZ2_UNUSED_0);

		// Left spread (only for quad)
		if (self->monsterinfo.quadfire_time > level.time) {
			vec3_t forward_left = aim_dir - (spread_right * 0.02f);
			forward_left.normalize();
			monster_fire_flechette(self, muzzle_pos, aim_dir, damage, speed, MZ2_UNUSED_0);
		}
	}

	// Update burst state
	state->flechette_burst_count++;
	state->last_flechette_burst_time = level.time;

	// Check if burst is complete
	if (state->flechette_burst_count >= state->flechette_burst_target) {
		state->flechette_burst_count = 0;
		// Set pause time before grenades can fire (0.4-0.5 seconds)
		state->flechette_to_grenade_pause_time = level.time + gtime_t::from_sec(0.4f + (frandom() * 0.1f));
	}
}
// Grenade fire function needs to use the state
static void TurretFireGrenade(edict_t* self, const vec3_t& start, const vec3_t& dir, float dist) {
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

    sentry_state_t* state = self->monsterinfo.sentry_state;
    if (!state) return;

	// Check if we need to wait for flechette-to-grenade pause
	if (level.time < state->flechette_to_grenade_pause_time) {
		return;
	}

	// Burst timing logic
	if (state->grenade_burst_count == 0) {
		if (level.time <= self->monsterinfo.last_sentry_missile_fire_time +
			(self->monsterinfo.quadfire_time > level.time ? 0.8_sec : 1.2_sec)) {
			return;
		}
		state->last_grenade_burst_time = level.time;
	}
	else if (level.time < state->last_grenade_burst_time + 0.5_sec) {
		return;
	}

	// Shot preparation
	vec3_t forward, right, up;
	AngleVectors(self->s.angles, forward, right, up);
	const vec3_t offset = { 20.f, 0.f, 0.f };
	vec3_t muzzle_pos = G_ProjectSource2(self->s.origin, offset, forward, right, up);

	if (!M_CheckClearShot(self, offset, muzzle_pos))
		return;

	const float speed = self->monsterinfo.quadfire_time > level.time ? 2000.0f : 1720.0f;
	vec3_t fire_dir, aimpoint;

	// Decide between high arc (mortar-style) and direct fire
	bool use_high_arc = (dist > 500 || !visible(self, self->enemy));

	// Similar dual-range strategy but with improved calculation
	if (dist < 400) {
		// SHORT RANGE: Direct prediction with small adjustments
		PredictAim(self, self->enemy, muzzle_pos, speed, true, 0.0f, &fire_dir, &aimpoint);

		// Add natural variance (more like tank grenades)
		fire_dir += right * (crandom() * 0.02f);
		fire_dir += up * (crandom() * 0.02f - 0.01f); // Slight downward bias
		fire_dir.normalize();
	}
	else {
		// LONG RANGE: Calculate better arc trajectory
		// Predict target position with velocity compensation
		float pred_time = dist / speed;
		vec3_t predicted_pos = self->enemy->s.origin + (self->enemy->velocity * pred_time * 0.8f);

		// Add slight height adjustment to hit at feet level for splash damage
		predicted_pos[2] -= 8.0f;

		fire_dir = predicted_pos - muzzle_pos;
		fire_dir.normalize();

		// Use M_CalculatePitchToFire with high arc for long distance
		if (M_CalculatePitchToFire(self, predicted_pos, muzzle_pos, fire_dir,
			speed, pred_time, use_high_arc)) {
			// Add tiny variation
			fire_dir[2] += crandom_open() * 0.005f;
			fire_dir.normalize();
		}
	}

	// Fire grenade with calculated parameters
	const int damage = static_cast<int>(CalculateDamage(self, 120));
	fire_grenade(self, muzzle_pos, fire_dir, damage, speed, 3_sec, 0,
		crandom_open() * 5.0f, // Add slight spin
		200.f, false);

// Manage burst state
	state->grenade_burst_count++;
	if (state->grenade_burst_count >= 2) {
		state->grenade_burst_count = 0;
		self->monsterinfo.last_sentry_missile_fire_time = level.time + 1_sec;
	}

	gi.sound(self, CHAN_VOICE, sound_grenade_launcher, 1, ATTN_NORM, 0);
}
void turret2Fire(edict_t* self) {

	if (!M_HasValidTarget(self))
	{
		return; // Can't at a non-existent or dead target.
	}

	// Check powerups and inherit from owner
	if (self->owner && self->owner->client) {
		TurretRespondPowerup(self, self->owner);
	}

	// Update aim
	turret2Aim(self);

	// Validate enemy with more specific checks
	if (!self->enemy || !self->enemy->inuse ||
		OnSameTeam(self, self->enemy) || self->enemy->deadflag) {
		if (self->monsterinfo.search_time < level.time) {
			if (!FindMTarget(self))
				return;
			self->monsterinfo.search_time = level.time + 300_ms;
		}
		return; // Return if we don't have a valid enemy and couldn't find one
	}

	//  PREVENT STALLING ANIMATION
	// Reset hold frame flag to prevent animation from getting stuck
	if (self->monsterinfo.aiflags & AI_HOLD_FRAME &&
		self->monsterinfo.duck_wait_time < level.time) {
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
	}

	// Mark that we attempted to attack
	self->monsterinfo.attack_finished = level.time;

	// Determine target point with better calculations
	vec3_t end;
	if (self->monsterinfo.aiflags & AI_LOST_SIGHT) {
		end = self->monsterinfo.blind_fire_target;
	}
	else {
		end = self->enemy->s.origin;
		// Adjust targeting height based on enemy type
		if (self->enemy->client)
			end[2] += self->enemy->viewheight;
		else
			end[2] += (self->enemy->maxs[2] - self->enemy->mins[2]) * 0.5f;
	}

	// Calculate direction with safer normalization
	vec3_t start = self->s.origin;
	vec3_t dir = end - start;
	if (!is_valid_vector(dir)) {
		return;
	}

	float length = dir.normalize(); // Normalize and get length
	if (length <= 1.0f) {
		// Enemy is too close, adjust end point
		end = self->enemy->s.origin;
		dir = end - start;
		dir.normalize();
	}

	// Check firing angle with more lenient threshold
	vec3_t forward;
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	const float chance = dir.dot(forward);

	// Relaxed angle check - 0.92 is about 23 degrees from center
	if (chance < 0.92f) {
		return;
	}

	// Calculate distance for weapon selection
	const float dist = (end - start).length();

	// More permissive trace to ensure we can hit the target
	trace_t tr = gi.traceline(start, end, self, MASK_SHOT);

	// Only consider it a failed trace if we hit something that isn't the enemy or world
	// AND it's not close to the enemy (sometimes entities overlap)
	if (tr.ent != self->enemy && tr.ent != world) {
		// Check if trace endpoint is close to enemy
		float dist_to_enemy = (tr.endpos - self->enemy->s.origin).length();
		if (dist_to_enemy > 32.0f) { // Not close enough to enemy
			return;
		}
	}

	// Fire appropriate weapon with reduced constraints
	if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN)) {
		// Handle machinegun state
		if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME)) {
			self->monsterinfo.aiflags |= AI_HOLD_FRAME;
			self->monsterinfo.duck_wait_time = level.time +
				(self->monsterinfo.quadfire_time > level.time ? 3_sec : 5_sec);
			self->monsterinfo.next_duck_time = level.time + 0.1_sec;
			gi.sound(self, CHAN_VOICE, gi.soundindex("weapons/chngnu1a.wav"), 1, ATTN_NORM, 0);
		}

		// Allow firing even if we just started holding frame
		TurretFireMachinegun(self, start, dir);

		// Only try rockets outside of minimum range
		if (dist > 200.0f) {
			TurretFireRocket(self, start, dir, dist);
		}
	}
	else if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER)) {
		// HEATBEAM - Use AI_HOLD_FRAME like machinegun for continuous beam
		sentry_state_t* state = self->monsterinfo.sentry_state;
		if (!state) return;
		
		// Handle heatbeam continuous firing state
		if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME)) {
			// Start continuous heatbeam mode
			self->monsterinfo.aiflags |= AI_HOLD_FRAME;
			self->monsterinfo.duck_wait_time = level.time +
				(self->monsterinfo.quadfire_time > level.time ? 4_sec : 3_sec);
			self->monsterinfo.next_duck_time = level.time + 0.05_sec; // Fast update for smooth beam
			state->heatbeam_active = true;
			state->heatbeam_start_time = level.time;
		}

		// Fire heatbeam continuously while holding frame
		if (self->monsterinfo.next_duck_time <= level.time) {
			self->monsterinfo.next_duck_time = level.time + 0.05_sec; // Keep updating rapidly
			
			// Simplified blaster/heatbeam logic
			vec3_t offset = { 20.f, 0.f, 0.f };
			const vec3_t hbstart = start + (forward * offset[0]);

			// Simpler prediction calculation
			vec3_t predictedDir;
			PredictAim(self, self->enemy, hbstart, 9999, false,
				self->monsterinfo.quadfire_time > level.time ? 0.01f : 0.03f,
				&predictedDir, nullptr);

			// Check if the predicted direction is valid
			if (is_valid_vector(predictedDir)) {
				trace_t hbtr = gi.traceline(hbstart, hbstart + predictedDir * 8192,
					self, MASK_SHOT);

				// Fire continuous heatbeam
				TurretFireHeatbeam(self, hbstart, predictedDir, hbtr);

				// Only try plasma at medium to long range
				if (dist > 300.0f && frandom() < 0.1f) { // Lower chance for plasma
					TurretFirePlasma(self, hbstart, predictedDir);
				}
			}
			else {
				// Fallback to direct fire if prediction fails
				TurretFireHeatbeam(self, hbstart, dir, tr);
			}
		}
	}
	// NEW: Flechette launcher option
	else if (self->spawnflags.has(SPAWNFLAG_TURRET2_FLECHETTE)) {
		// Fire flechettes as primary attack
		TurretFireFlechette(self, start, dir);

		// Always fire grenades when possible, just like rockets
		TurretFireGrenade(self, start, dir, dist);
	}

	// last_sentry_missile_fire_time is now set inside each fire function (rocket, plasma, grenade)
}
// PMM
void turret2FireBlind(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

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

// Reattack function for continuous heatbeam firing
// Reattack function for continuous heatbeam firing
void turret2_reattack_heatbeam(edict_t* self) {
	if (!M_HasValidTarget(self))
	{
		// Lost target, return to normal state
		self->monsterinfo.run(self);
		return;
	}

	sentry_state_t* state = self->monsterinfo.sentry_state;
	if (!state) {
		self->monsterinfo.run(self);
		return;
	}

	// Check if using heatbeam (blaster spawnflag)
	if (!self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER)) {
		// Not using heatbeam, return to normal
		self->monsterinfo.run(self);
		return;
	}

	// Check if beam should continue
	if (state->heatbeam_active && level.time < state->heatbeam_start_time + state->heatbeam_duration) {
		// Continue firing - loop back to frame 1 (second frame) to keep firing
		// 95% chance to continue for smooth continuous beam
		if (frandom() > 0.05f) { 
			// Loop animation from frame 1 to maintain continuous beam
			self->s.frame = self->monsterinfo.active_move->firstframe + 1;
			self->monsterinfo.nextframe = self->s.frame;
			return;
		}
	}

	// Beam complete or interrupted, return to normal state
	state->heatbeam_active = false;
	self->monsterinfo.run(self);
}
// pmm

mframe_t turret2_frames_fire[] = {
	{ ai_run, 0, turret2Aim },
	{ ai_run, 0, turret2Fire },
	{ ai_run, 0, turret2Fire },
	{ ai_run, 0, turret2Fire },
	{ ai_run, 0, turret2_reattack_heatbeam }, // Check for heatbeam continuation
};;
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

// attack function
MONSTERINFO_ATTACK(turret2_attack) (edict_t* self) -> void
{
    sentry_state_t* state = self->monsterinfo.sentry_state;
    if (!state) return;

	const gtime_t animation_cooldown = 250_ms;

	if (self->s.frame < FRAME_run01)
	{
		turret2_ready_gun(self);
	}
	else if (self->monsterinfo.attack_state != AS_BLIND)
	{
		if (level.time >= state->last_animation_change_time + animation_cooldown) {
			M_SetAnimation(self, &turret2_move_fire);
			state->last_animation_change_time = level.time;
		}
	}
	else
	{
		if (!self->monsterinfo.blind_fire_target)
			return;

		if (level.time >= state->last_animation_change_time + animation_cooldown) {
			M_SetAnimation(self, &turret2_move_fire_blind);
			state->last_animation_change_time = level.time;
		}
	}
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
	auto& vec = g_targetable_special_entities;
    vec.erase(std::remove(vec.begin(), vec.end(), self), vec.end());

	// --- FIX: Free the allocated sentry state to prevent a memory leak ---
	if (self->monsterinfo.sentry_state)
	{
		gi.TagFree(self->monsterinfo.sentry_state);
		self->monsterinfo.sentry_state = nullptr;
	}
	// --- END FIX ---

	 // Handle summoned entity notifications
    if (self->monsterinfo.issummoned && self->owner && self->owner->client) {
        if (horde::IsMonsterType(self, horde::MonsterTypeID::SENTRYGUN)) {
            gi.Client_Print(self->owner, PRINT_HIGH, "Your sentry gun was destroyed.\n");

            // This line already correctly decrements the count.
            self->owner->client->resp.num_sentries--;

            // --- MODIFIED LOGIC ---
            // Find this sentry in the owner's tracking array and null it out.
            // This is the crucial step to prevent dangling pointers and keep the array clean.
            for (int i = 0; i < SentryConstants::MAX_SENTRIES_PER_PLAYER; ++i) {
                if (self->owner->client->resp.deployed_sentries[i] == self) {
                    self->owner->client->resp.deployed_sentries[i] = nullptr;
                    break; // Found it, we're done.
                }
            }
        }
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


void remove_sentries(edict_t* ent) noexcept {
    if (!ent || !ent->client) {
        return;
    }

    // Iterate backwards through the player's specific sentry list.
    for (int i = SentryConstants::MAX_SENTRIES_PER_PLAYER - 1; i >= 0; --i) {
        edict_t* sentry = ent->client->resp.deployed_sentries[i];
        
        // If a valid sentry is found, call its die function.
        if (sentry && sentry->inuse && horde::IsMonsterType(sentry, horde::MonsterTypeID::SENTRYGUN)) {
            // turret2_die will handle decrementing the count and clearing this array slot.
            turret2_die(sentry, ent, ent, 99999, sentry->s.origin, MOD_UNKNOWN);
        }
    }

    // As a safeguard, ensure the count and array are fully cleared.
    ent->client->resp.num_sentries = 0;
    for (int i = 0; i < SentryConstants::MAX_SENTRIES_PER_PLAYER; ++i) {
        ent->client->resp.deployed_sentries[i] = nullptr;
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
	// Basic validity checks
	if (!M_HasValidTarget(self))
	{
		return false; // Can't at a non-existent or dead target.
	}

	// Ignore monsters that should never be attacked
	if (OnSameTeam(self, self->enemy) || self->enemy->deadflag)
		return false;

	// Get positions for line of sight check
	vec3_t spot1 = self->s.origin;
	spot1[2] += self->viewheight;
	vec3_t spot2 = self->enemy->s.origin;
	spot2[2] += self->enemy->client ? self->enemy->viewheight :
		(self->enemy->maxs[2] - self->enemy->mins[2]) * 0.5f * self->enemy->s.scale;

	// Check line of sight with more thorough mask
	trace_t const tr = gi.traceline(spot1, spot2, self,
		MASK_SHOT);

	//  More permissive trace validation
	// If we can't directly see the enemy but we've been trying for a while, 
	// find a new target instead of just returning false
	if (tr.fraction < 1.0f && tr.ent != self->enemy) {
		if (self->monsterinfo.attack_finished + 1_sec < level.time) {
			// We've been trying to attack for over a second but couldn't trace to enemy
			// Try to find a new target instead
			FindMTarget(self);
		}
		return false;
	}

	// Check if enemy is within firing arc
	vec3_t dir = self->enemy->s.origin - self->s.origin;
	vec3_t forward;
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	dir.normalize();
	float const dot = dir.dot(forward);

	//  More permissive angle check
	// Use 0.9 (about 26 degrees) instead of 0.95 (18 degrees)
	if (dot < 0.9) {
		// Not directly in front - check if we're already turning
		// If we've been trying for more than 1 second, find a new target
		if (self->monsterinfo.attack_finished + 1_sec < level.time) {
			FindMTarget(self);
		}
		return false;
	}

	// Calculate distance and adjust firing probability
	float const range = range_to(self, self->enemy);

	// Don't try to fire if enemy is too far away
	if (range > 1500.0f) {
		return false;
	}

	// Adjust chance based on range
	float chance = range <= RANGE_NEAR ? 0.9f :
		(range <= RANGE_MID ? 0.7f : 0.4f);

	//  Gradually increase chance based on time since last attack
	// This ensures that even if RNG is bad, we'll eventually fire
	float time_since_attack = (level.time - self->monsterinfo.attack_finished).seconds();
	if (time_since_attack > 0.5f)
		chance += time_since_attack * 0.2f; // +20% per second

	// Cap at 99% to always leave some small RNG factor
	if (chance > 0.99f)
		chance = 0.99f;

	// Roll for attack
	if (frandom() < chance) {
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

	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::SENTRYGUN);
	self->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::SENTRY_GUN);

	  // Allocate memory for the custom state
    self->monsterinfo.sentry_state = (sentry_state_t*)gi.TagMalloc(sizeof(sentry_state_t), TAG_LEVEL);
    if (!self->monsterinfo.sentry_state) {
        gi.Com_PrintFmt("ERROR: Failed to allocate sentry_state for monster_sentrygun\n");
        G_FreeEdict(self);
        return;
    }


	    // NOTE: Sentry tracking is handled by the deployment function (Use_SentryGun)
    // to avoid duplicate tracking. This spawn function only creates the entity.

    // IMPORTANT: Initialize all the fields to their default values.
    // This is the answer to your question: "is it really needed?" -> YES!
    sentry_state_t* state = self->monsterinfo.sentry_state;
    state->last_target_time = 0_sec;
    state->last_enemy_change_time = 0_sec;
    state->previous_enemy = nullptr;
    state->was_attacking = false;
    state->transition_state = 0;
    state->next_target_search_time = 0_sec;
    state->last_animation_change_time = 0_sec;
    state->grenade_burst_count = 0;
    state->last_grenade_burst_time = 0_sec;
    state->last_regeneration_time = 0_sec;

	// --- Unconditional Pre-caching Block ---
	// By placing all asset loading at the top, we guarantee that every possible
	// model and sound for every sentry variant is precached when this function
	// is called once by PrecacheAllMonsters(), regardless of spawnflags.

	// Common Models & Sounds
	gi.modelindex("models/monsters/turret/tris.md2");
	gi.modelindex("models/objects/debris1/tris.md2");
	gi.soundindex("tank/tnkpain2.wav");
	gi.soundindex("gunner/gunidle1.wav");
	gi.soundindex("turret/moving.wav");
	gi.soundindex("makron/blaster.wav");

	// Machinegun & Rocket-specific Assets
	gi.modelindex("models/objects/laser/tris.md2"); // Laser sight model
	gi.modelindex("models/objects/rocket/tris.md2");
	gi.soundindex("infantry/infatck1.wav");
	gi.soundindex("weapons/chngnu1a.wav");
	gi.soundindex("weapons/rockfly.wav");
	gi.soundindex("chick/chkatck2.wav");

	// Blaster/Heatbeam-specific Assets
	gi.soundindex("misc/lasfly.wav");
	gi.soundindex("soldier/solatck2.wav");

	// Flechette & Grenade-specific Assets
	gi.modelindex("models/objects/blaser/tris.md2"); // Flechette projectile model
	gi.modelindex("models/objects/grenade/tris.md2");
	gi.soundindex("tank/tnkatck3.wav");
	gi.soundindex("weapons/hyprbf1a.wav");
	gi.soundindex("gunner/gunatck3.wav");
	gi.soundindex("weapons/grenlx1a.wav");

	// Assign to cached_soundindex variables
	sound_pew.assign("makron/blaster.wav");
	sound_moved.assign("gunner/gunidle1.wav");
	sound_moving.assign("turret/moving.wav");
	sound_grenade_launcher.assign("gunner/gunatck3.wav");
	sound_flechette.assign("tank/tnkatck3.wav");
	// --- End of Pre-caching Block ---


	// Standard monster setup begins here
	if (self->owner && self->owner->client) {
		TurretRespondPowerup(self, self->owner);
	}

	self->monsterinfo.last_sentry_missile_fire_time = gtime_t::from_sec(2);
	self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
	self->monsterinfo.attack_state = AS_BLIND;
	self->s.effects = EF_BOB;
	self->target_hint_chain = nullptr;
	
	ApplyMonsterBonusFlags(self);

	if (!M_AllowSpawn(self))
	{
		G_FreeEdict(self);
		return;
	}

	self->s.modelindex = gi.modelindex("models/monsters/turret/tris.md2");
	self->mins = { -12, -12, -12 };
	self->maxs = { 12, 12, 12 };
	self->movetype = MOVETYPE_NONE;

	// Calculate health with adrenaline bonus first
	int base_health = 125;
	self->health = CalculateSentryHealth(base_health, self->owner ? self->owner->client : nullptr);
	self->max_health = self->health;

	self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
	self->monsterinfo.power_armor_power = static_cast<int>(round(self->max_health * 0.4f));
	self->gib_health = -100;
	self->mass = 100;
	self->yaw_speed = 16;
	self->solid = SOLID_BBOX;
	self->flags |= FL_MECHANICAL;
	self->pain = turret2_pain;
	self->die = turret2_die;

	// Determine weapon type for this specific instance
	if (!self->spawnflags.has(SPAWNFLAG_TURRET2_WEAPONCHOICE))
	{
		sentrytype_t choice = SENTRY_RANDOM;
		if (self->owner && self->owner->client)
		{
			choice = self->owner->client->pers.sentry_gun_choice;
		}

		switch (choice)
		{
		case SENTRY_HEATBEAM:
			self->spawnflags |= SPAWNFLAG_TURRET2_BLASTER;
			break;
		case SENTRY_MACHINEGUN:
			self->spawnflags |= SPAWNFLAG_TURRET2_MACHINEGUN;
			break;
		case SENTRY_FLECHETTE:
			self->spawnflags |= SPAWNFLAG_TURRET2_FLECHETTE;
			break;
		case SENTRY_RANDOM:
		default:
		{
			const float randomValue = frandom();
			if (randomValue < 0.3f) {
				self->spawnflags |= SPAWNFLAG_TURRET2_BLASTER;
			}
			else if (randomValue < 0.7f) {
				self->spawnflags |= SPAWNFLAG_TURRET2_MACHINEGUN;
			}
			else {
				self->spawnflags |= SPAWNFLAG_TURRET2_FLECHETTE;
			}
			break;
		}
		}
	}

	if (self->spawnflags.has(SPAWNFLAG_TURRET2_HEATBEAM))
	{
		self->spawnflags &= ~SPAWNFLAG_TURRET2_HEATBEAM;
		self->spawnflags |= SPAWNFLAG_TURRET2_BLASTER;
	}

	// Set AI functions
	self->monsterinfo.stand = turret2_stand;
	self->monsterinfo.walk = turret2_walk;
	self->monsterinfo.run = turret2_run;
	self->monsterinfo.dodge = nullptr;
	self->monsterinfo.attack = turret2_attack;
	self->monsterinfo.melee = nullptr;
	self->monsterinfo.sight = turret2_sight;
	self->monsterinfo.search = turret2_search;
	self->monsterinfo.checkattack = turret2_checkattack;
	M_SetAnimation(self, &turret2_move_stand);

	self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
	self->monsterinfo.scale = MODEL_SCALE;
	self->gravity = 0;

	// Wall placement logic
	self->offset = self->s.angles;
	int angle = (int)self->s.angles[1];
	switch (angle)
	{
	case -1: self->s.angles = { 270, 0, 0 }; self->s.origin.z += 2; break;
	case -2: self->s.angles = { 90, 0, 0 };  self->s.origin.z -= 2; break;
	case 0:  self->s.origin.x += 2; break;
	case 90: self->s.origin.y += 2; break;
	case 180:self->s.origin.x -= 2; break;
	case 270:self->s.origin.y -= 2; break;
	default: break;
	}

	gi.linkentity(self);
	
	stationarymonster_start(self, spawn_temp_t::empty);
	self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
	
	// Create visual effects after linking
	CreateTurretGlowEffect(self);

	// Set runtime properties based on the chosen weapon
	if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN))
	{
		self->s.skinnum = 2;
	}
	else if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
	{
		self->s.skinnum = 0;
	}
	else if (self->spawnflags.has(SPAWNFLAG_TURRET2_FLECHETTE))
	{
		self->s.skinnum = 0;
	}

	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
		self->yaw_speed = 15;
	else if (self->spawnflags.has(SPAWNFLAG_TURRET2_FLECHETTE))
		self->yaw_speed = 20;

	if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN | SPAWNFLAG_TURRET2_BLASTER | SPAWNFLAG_TURRET2_FLECHETTE))
		self->monsterinfo.blindfire = true;

	if (self->monsterinfo.quadfire_time > level.time) {
		self->yaw_speed *= 2.0f;
	}
}