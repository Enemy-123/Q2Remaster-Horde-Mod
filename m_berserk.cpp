// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

BERSERK

==============================================================================
*/

#include "g_local.h"
#include "m_berserk.h"
#include "shared.h"

constexpr spawnflags_t SPAWNFLAG_BERSERK_NOJUMPING = 8_spawnflag;

static cached_soundindex sound_pain;
static cached_soundindex sound_die;
static cached_soundindex sound_idle;
static cached_soundindex sound_idle2;
static cached_soundindex sound_punch;
static cached_soundindex sound_sight;
static cached_soundindex sound_search;
static cached_soundindex sound_thud;
static cached_soundindex sound_explod;
static cached_soundindex sound_jump;
static cached_soundindex sound_windup;
static cached_soundindex sound_fireball; 

MONSTERINFO_SIGHT(berserk_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_SEARCH(berserk_search) (edict_t* self) -> void
{
	if (brandom())
		gi.sound(self, CHAN_VOICE, sound_idle2, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
}

void	 berserk_fidget(edict_t* self);

mframe_t berserk_frames_stand[] = {
	{ ai_stand, 0, berserk_fidget },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }
};
MMOVE_T(berserk_move_stand) = { FRAME_stand1, FRAME_stand5, berserk_frames_stand, nullptr };

MONSTERINFO_STAND(berserk_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &berserk_move_stand);
}

mframe_t berserk_frames_stand_fidget[] = {
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
MMOVE_T(berserk_move_stand_fidget) = { FRAME_standb1, FRAME_standb20, berserk_frames_stand_fidget, berserk_stand };

void berserk_start_run_loop(edict_t* self); 
extern const mmove_t berserk_move_run_start; 
extern const mmove_t berserk_move_run_loop;
void berserk_fidget(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		return;
	else if (self->enemy)
		return;
	if (frandom() > 0.15f)
		return;

	M_SetAnimation(self, &berserk_move_stand_fidget);
	gi.sound(self, CHAN_WEAPON, sound_idle, 1, ATTN_IDLE, 0);
}

mframe_t berserk_frames_walk[] = {
	{ ai_walk, 9.1f },
	{ ai_walk, 6.3f },
	{ ai_walk, 4.9f },
	{ ai_walk, 6.7f, monster_footstep },
	{ ai_walk, 6.0f },
	{ ai_walk, 8.2f },
	{ ai_walk, 7.2f },
	{ ai_walk, 6.1f },
	{ ai_walk, 4.9f },
	{ ai_walk, 4.7f, monster_footstep },
	{ ai_walk, 4.7f }
};
MMOVE_T(berserk_move_walk) = { FRAME_walkc1, FRAME_walkc11, berserk_frames_walk, nullptr };

MONSTERINFO_WALK(berserk_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &berserk_move_walk);
}

/*

  *****************************
  SKIPPED THIS FOR NOW!
  *****************************

   Running -> Arm raised in air

void()	berserk_runb1	=[	$r_att1 ,	berserk_runb2	] {ai_run(21);};
void()	berserk_runb2	=[	$r_att2 ,	berserk_runb3	] {ai_run(11);};
void()	berserk_runb3	=[	$r_att3 ,	berserk_runb4	] {ai_run(21);};
void()	berserk_runb4	=[	$r_att4 ,	berserk_runb5	] {ai_run(25);};
void()	berserk_runb5	=[	$r_att5 ,	berserk_runb6	] {ai_run(18);};
void()	berserk_runb6	=[	$r_att6 ,	berserk_runb7	] {ai_run(19);};
// running with arm in air : start loop
void()	berserk_runb7	=[	$r_att7 ,	berserk_runb8	] {ai_run(21);};
void()	berserk_runb8	=[	$r_att8 ,	berserk_runb9	] {ai_run(11);};
void()	berserk_runb9	=[	$r_att9 ,	berserk_runb10	] {ai_run(21);};
void()	berserk_runb10	=[	$r_att10 ,	berserk_runb11	] {ai_run(25);};
void()	berserk_runb11	=[	$r_att11 ,	berserk_runb12	] {ai_run(18);};
void()	berserk_runb12	=[	$r_att12 ,	berserk_runb7	] {ai_run(19);};
// running with arm in air : end loop
*/


void berserk_check_passive_zap(edict_t* self); // Keep this forward declaration

// This is the looping part of the "hand up" run.
mframe_t berserk_frames_run_loop[] = {
	{ ai_run, 21 },
	{ ai_run, 11, monster_footstep },
	{ ai_run, 21, berserk_check_passive_zap }, // PASSIVE ZAP IS CALLED HERE!
	{ ai_run, 25 },
	{ ai_run, 18, monster_footstep },
	{ ai_run, 19 }
};
// When this animation ends, it calls berserk_start_run_loop, which sets the animation again, creating a perfect loop.
MMOVE_T(berserk_move_run_loop) = { FRAME_r_att7, FRAME_r_att12, berserk_frames_run_loop, berserk_start_run_loop };

// A tiny helper function to ensure we stay in the looping run animation.
void berserk_start_run_loop(edict_t* self)
{
	M_SetAnimation(self, &berserk_move_run_loop);
}

// This is the "wind up" that raises the arm before the main loop.
mframe_t berserk_frames_run_start[] = {
	{ ai_run, 21 },
	{ ai_run, 11, monster_footstep },
	{ ai_run, 21 },
	{ ai_run, 25, monster_done_dodge },
	{ ai_run, 18, monster_footstep },
	{ ai_run, 19 }
};
// After this finishes, it calls berserk_start_run_loop to begin the main loop.
MMOVE_T(berserk_move_run_start) = { FRAME_r_att1, FRAME_r_att6, berserk_frames_run_start, berserk_start_run_loop };

// This is now our main run function.
MONSTERINFO_RUN(berserk_run) (edict_t* self) -> void
{
	monster_done_dodge(self);

	// Don't restart the animation if we are already in the "hand up" run.
	if (self->monsterinfo.active_move == &berserk_move_run_loop ||
		self->monsterinfo.active_move == &berserk_move_run_start)
	{
		return;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &berserk_move_stand);
	else
		M_SetAnimation(self, &berserk_move_run_start); // Start the "hand up" run sequence.
}
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// END OF NEW BLOCK

void berserk_attack_spike(edict_t* self)
{
	constexpr vec3_t aim = { MELEE_DISTANCE, 0, -24 };

	if (!M_HasValidTarget(self))
	{
		self->monsterinfo.melee_debounce_time = level.time + 1.2_sec;
		return;
	}

	if (!fire_hit(self, aim, irandom(17, 26) * M_DamageModifier(self), 400))
		self->monsterinfo.melee_debounce_time = level.time + 1.2_sec;
}

void berserk_swing(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_punch, 1, ATTN_NORM, 0);
}

mframe_t berserk_frames_attack_spike[] = {

	{ ai_charge, 0, berserk_swing },
	{ ai_charge, 0, berserk_attack_spike },
	{ ai_charge, 0, berserk_attack_spike },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(berserk_move_attack_spike) = { FRAME_att_c1, FRAME_att_c8, berserk_frames_attack_spike, berserk_run };

void berserk_attack_club(edict_t* self)
{
	vec3_t  const aim = { MELEE_DISTANCE, self->mins[0], -4 };

	if (!M_HasValidTarget(self))
	{
		self->monsterinfo.melee_debounce_time = level.time + 2.5_sec;
		return;
	}

	if (!fire_hit(self, aim, irandom(21, 28) * M_DamageModifier(self), 250))
		self->monsterinfo.melee_debounce_time = level.time + 2.5_sec;
}

mframe_t berserk_frames_attack_club[] = {
	{ ai_charge, 0, monster_footstep },
	{ ai_charge },
	{ ai_charge, 0, berserk_swing },
	{ ai_charge },
	{ ai_charge, 0, berserk_attack_club },
	{ ai_charge, 0, berserk_attack_club },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(berserk_move_attack_club) = { FRAME_att_c9, FRAME_att_c20, berserk_frames_attack_club, berserk_run };

/*
============
T_RadiusDamage
============
*/
void T_SlamRadiusDamage(vec3_t point, edict_t* inflictor, edict_t* attacker, float damage, float kick, edict_t* ignore, float radius, mod_t mod)
{
	float	 points;
	edict_t* ent = nullptr;
	vec3_t	 v;
	vec3_t	 dir;

	while ((ent = findradius(ent, inflictor->s.origin, radius * 2.f)) != nullptr)
	{
		if (ent == ignore)
			continue;
		if (!ent->takedamage)
			continue;
		if (!CanDamage(ent, inflictor))
			continue;
		// don't hit players in mid air
		//if (ent->client && !ent->groundentity)
		//	continue;

		v = closest_point_to_box(point, ent->s.origin + ent->mins, ent->s.origin + ent->maxs) - point;

		// calculate contribution amount
		float amount = min(1.f, 1.f - (v.length() / radius));

		// too far away
		if (amount <= 0.f)
			continue;

		amount *= amount;

		// damage & kick are exponentially scaled
		points = max(1.f, damage * amount);

		dir = (ent->s.origin - point).normalized();

		// keep the point at their feet so they always get knocked up
		point[2] = ent->absmin[2];
		T_Damage(ent, inflictor, attacker, dir, point, dir, (int)points, static_cast<int>(kick * amount),
			DAMAGE_RADIUS, mod);

		if (ent->inuse && ent->client)
			ent->velocity.z = max(270.f, ent->velocity.z);
	}
}

static void berserk_attack_slam(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_thud, 1, ATTN_NORM, 0);
	gi.sound(self, CHAN_AUTO, sound_explod, 0.75f, ATTN_NORM, 0);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_BERSERK_SLAM);
	vec3_t f, r, start;
	AngleVectors(self->s.angles, f, r, nullptr);
	start = M_ProjectFlashSource(self, { 20.f, -14.3f, -21.f }, f, r);
	trace_t const tr = gi.traceline(self->s.origin, start, self, MASK_SOLID);
	gi.WritePosition(tr.endpos);
	gi.WriteDir({ 0.f, 0.f, 1.f });
	gi.multicast(tr.endpos, MULTICAST_PHS, false);
	self->gravity = 1.0f;
	self->velocity = {};
	self->flags |= FL_KILL_VELOCITY;

	T_SlamRadiusDamage(tr.endpos, self, self, 15, 300.f, self, 165, MOD_UNKNOWN);
}

TOUCH(berserk_jump_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	if (self->health <= 0)
	{
		self->touch = nullptr;
		return;
	}

	if (self->groundentity)
	{
		self->s.frame = FRAME_slam18;

		if (self->touch)
			berserk_attack_slam(self);

		self->touch = nullptr;
	}
}

static void berserk_high_gravity(edict_t* self)
{
	float  const gravity_scale = (800.f / level.gravity);

	if (self->velocity[2] < 0)
		self->gravity = 2.25f;
	else
		self->gravity = 5.25f;

	self->gravity *= gravity_scale;
}

void berserk_jump_takeoff(edict_t* self)
{
	vec3_t forward;

	if (!M_HasValidTarget(self))
	{
		return;
	}

	// immediately turn to where we need to go
	float  const length = (self->s.origin - self->enemy->s.origin).length();
	float  const fwd_speed = length * 1.95f;
	vec3_t dir;
	PredictAim(self, self->enemy, self->s.origin, fwd_speed, false, 0.f, &dir, nullptr);
	self->s.angles[1] = vectoyaw(dir);
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	self->s.origin[2] += 1;
	self->velocity = forward * fwd_speed;
	self->velocity[2] = 400;
	self->groundentity = nullptr;
	self->monsterinfo.aiflags |= AI_DUCKED;
	self->monsterinfo.attack_finished = level.time + 3_sec;
	self->touch = berserk_jump_touch;
	berserk_high_gravity(self);

	self->gravity = -self->gravity;
	SV_AddGravity(self);
	self->gravity = -self->gravity;

	gi.linkentity(self);
}

void berserk_check_landing(edict_t* self)
{
	berserk_high_gravity(self);

	if (self->groundentity)
	{
		self->monsterinfo.attack_finished = 0_ms;
		self->monsterinfo.unduck(self);
		self->s.frame = FRAME_slam18;
		if (self->touch)
		{
			berserk_attack_slam(self);
			self->touch = nullptr;
		}
		self->flags &= ~FL_KILL_VELOCITY;
		return;
	}

	if (level.time > self->monsterinfo.attack_finished)
		self->monsterinfo.nextframe = FRAME_slam3;
	else
		self->monsterinfo.nextframe = FRAME_slam5;
}

mframe_t berserk_frames_attack_strike[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_move, 0, berserk_jump_takeoff },
	{ ai_move, 0, berserk_high_gravity },
	{ ai_move, 0, berserk_check_landing },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
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
	{ ai_move, 0, monster_footstep }
};
MMOVE_T(berserk_move_attack_strike) = { FRAME_slam1, FRAME_slam23, berserk_frames_attack_strike, berserk_run };

extern const mmove_t berserk_move_run_attack1;

MONSTERINFO_MELEE(berserk_melee) (edict_t* self) -> void
{
	if (self->monsterinfo.melee_debounce_time > level.time)
		return;
	// if we're *almost* ready to land down the hammer from run-attack
	// don't switch us
	else if (self->monsterinfo.active_move == &berserk_move_run_attack1 && self->s.frame >= FRAME_r_att13)
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		self->monsterinfo.attack_finished = 0_ms;
		return;
	}

	monster_done_dodge(self);

	if (brandom())
		M_SetAnimation(self, &berserk_move_attack_spike);
	else
		M_SetAnimation(self, &berserk_move_attack_club);
}

static void berserk_run_attack_speed(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		self->monsterinfo.run(self);
		return; // Stop immediately if the target is invalid.
	}

	if (range_to(self, self->enemy) < MELEE_DISTANCE)
	{
		self->monsterinfo.nextframe = self->s.frame + 6;
		monster_done_dodge(self);
	}
}

static void berserk_run_swing(edict_t* self)
{
	berserk_swing(self);
	self->monsterinfo.melee_debounce_time = level.time + 0.6_sec;

	if (self->monsterinfo.attack_state == AS_SLIDING)
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		monster_done_dodge(self);
	}
}

mframe_t berserk_frames_run_attack1[] = {
	{ ai_run, 21, berserk_run_attack_speed },
	{ ai_run, 11, [](edict_t* self) { berserk_run_attack_speed(self); monster_footstep(self); } },
	{ ai_run, 21, berserk_run_attack_speed },
	{ ai_run, 25, [](edict_t* self) { berserk_run_attack_speed(self); monster_done_dodge(self); } },
	{ ai_run, 18, [](edict_t* self) { berserk_run_attack_speed(self); monster_footstep(self); } },
	{ ai_run, 19, berserk_run_attack_speed },
	{ ai_run, 21 },
	{ ai_run, 11, monster_footstep },
	{ ai_run, 21 },
	{ ai_run, 25 },
	{ ai_run, 18, monster_footstep },
	{ ai_run, 19 },
	{ ai_run, 21, berserk_run_swing },
	{ ai_run, 11, monster_footstep },
	{ ai_run, 21 },
	{ ai_run, 25 },
	{ ai_run, 18, monster_footstep },
	{ ai_run, 19, berserk_attack_club }
};
MMOVE_T(berserk_move_run_attack1) = { FRAME_r_att1, FRAME_r_att18, berserk_frames_run_attack1, berserk_run };


//============================================================================
// NEW: PASSIVE DYNAMIC LIGHTNING ATTACK
// This attack can be used while the Berserker is running, without
// interrupting its movement or preventing it from starting a melee attack.
//============================================================================

// Attack parameters
constexpr float BERSERK_ZAP_RADIUS         = 280.0f;
constexpr float BERSERK_ZAP_RADIUS_SQUARED = BERSERK_ZAP_RADIUS * BERSERK_ZAP_RADIUS; // Pre-calculate for performance
constexpr int   BERSERK_ZAP_DAMAGE         = 10;
constexpr int   BERSERK_ZAP_KNOCKBACK      = 10;
constexpr int   BERSERK_ZAP_MAX_TARGETS    = 3;
constexpr gtime_t BERSERK_ZAP_COOLDOWN       = 2.0_sec;   // Time between zaps
constexpr float BERSERK_ZAP_MIN_RANGE      = 128.0f; // Don't zap if enemy is in melee range
constexpr float BERSERK_ZAP_MAX_RANGE      = 600.0f; // Max range for zapping

// Helper function to fire a single lightning bolt.
static void berserk_fire_bolt(edict_t* self, edict_t* target, const vec3_t& zap_origin)
{
	// Get the center of the target for a better trace
	vec3_t target_center = (target->absmin + target->absmax) * 0.5f;

	// Check for a clear line of sight from the hand to the target
	trace_t tr = gi.traceline(zap_origin, target_center, self, MASK_SHOT);
	if (tr.ent != target)
		return; // Blocked by something

	// --- Attack Phase ---
	int damage = static_cast<int>(BERSERK_ZAP_DAMAGE * M_DamageModifier(self));
	T_Damage(target, self, self, vec3_origin, tr.endpos, tr.plane.normal,
		damage, BERSERK_ZAP_KNOCKBACK, DAMAGE_ENERGY, MOD_TESLA);

	// Play the zap sound at the target's location
	gi.sound(target, CHAN_AUTO, sound_windup, 1, ATTN_NORM, 0);

	// Create the visual lightning effect
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_LIGHTNING);
	gi.WriteEntity(self);
	gi.WriteEntity(target);
	gi.WritePosition(zap_origin);
	gi.WritePosition(tr.endpos);
	gi.multicast(zap_origin, MULTICAST_PVS, false);
}

// This function contains the multi-target lightning logic.
void berserk_zap_enemies(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t forward, right;
	AngleVectors(self->s.angles, forward, right, nullptr);

	// Coordinates for the Berserker's raised LEFT hand.
	vec3_t const zap_origin = G_ProjectSource(self->s.origin, { 25.f, -20.f, 40.f }, forward, right);

	int targets_hit = 0;

	// --- 1. Prioritize the main enemy ---
	if (DistanceSquared(self->s.origin, self->enemy->s.origin) <= BERSERK_ZAP_RADIUS_SQUARED)
	{
		berserk_fire_bolt(self, self->enemy, zap_origin);
		targets_hit++;
	}

	// --- 2. Find other nearby targets ---
	edict_t* target = nullptr;
	while ((target = findradius(target, self->s.origin, BERSERK_ZAP_RADIUS)) != nullptr)
	{
		if (targets_hit >= BERSERK_ZAP_MAX_TARGETS)
			break;

		if (!target->inuse || target->health <= 0 || !target->takedamage)
			continue;

		// PERFORMANCE: Skip the main enemy, as it was already processed.
		if (target == self || target == self->enemy || OnSameTeam(self, target))
			continue;

		if (DistanceSquared(self->s.origin, target->s.origin) > BERSERK_ZAP_RADIUS_SQUARED)
			continue;

		berserk_fire_bolt(self, target, zap_origin);
		targets_hit++;
	}
}

// This is called during the run animation.
// A better implementation in m_berserk.c
void berserk_check_passive_zap(edict_t* self)
{
	// Use `trail_time` as a cooldown timer for this passive ability.
	// It's an unused gtime_t field and more appropriate than a flight-related one.
	if (level.time < self->monsterinfo.trail_time)
		return;

	// All clear, fire the zaps!
	berserk_zap_enemies(self);

	// Set the cooldown.
	self->monsterinfo.trail_time = level.time + BERSERK_ZAP_COOLDOWN;
}

//============================================================================
// BERSERK FIREBALL ATTACK
// Fires fireballs from the raised hand while running, similar to Shambler
//============================================================================

void BerserkCastFireballs(edict_t* self)
{
	if (!M_HasEnemy(self))
	{
		return; // Can't attack a non-existent enemy.
	}

	vec3_t f, r;
	AngleVectors(self->s.angles, f, r, nullptr);

	// Use the same origin as the zap attack (raised LEFT hand)
	vec3_t const start = G_ProjectSource(self->s.origin, { 25.f, -20.f, 40.f }, f, r);

	vec3_t dir;
	vec3_t target;
	const float rocketSpeed = 1200;
	const bool blindfire = (self->monsterinfo.aiflags & AI_MANUAL_STEERING) != 0;

	// If in blindfire mode, use the saved target
	if (blindfire)
	{
		target = self->monsterinfo.blind_fire_target;

		if (!M_AdjustBlindfireTarget(self, start, target, r, dir))
			return;
	}
	else
	{
		if (!M_HasValidTarget(self))
			return;

		// Smart targeting like tank/shambler
		if (frandom() < 0.66f || (start[2] < self->enemy->absmin[2]))
		{
			target = self->enemy->s.origin;
			target[2] += self->enemy->viewheight;
		}
		else
		{
			target = self->enemy->s.origin;
			target[2] = self->enemy->absmin[2] + 1;
		}

		// Lead shot with probability based on difficulty
		if (frandom() <= 0.2f + ((3 - skill->integer) * 0.15f))
			PredictAim(self, self->enemy, start, rocketSpeed, false, 0, &dir, &target);
		else
		{
			dir = target - start;
			dir.normalize();
		}

		// Check line of sight
		trace_t const trace = gi.traceline(start, target, self, MASK_PROJECTILE);
		if (trace.fraction < 0.5f && !blindfire)
			return;
	}

	// Save last known position for blindfire
	self->monsterinfo.blind_fire_target = target;

	// Launch fireballs
	const int num_fireballs = (g_hardcoop->integer || self->monsterinfo.IS_BOSS) ? 3 : 1;
	const float spread_base = g_hardcoop->integer ? 0.03f : 0.06f;

	for (int i = 0; i < num_fireballs; i++)
	{
		vec3_t spread_dir = dir;
		if (i > 0)
		{
			float spread = spread_base;
			if (self->monsterinfo.IS_BOSS)
				spread *= 0.5f;

			spread_dir[0] += crandom() * spread;
			spread_dir[1] += crandom() * spread;
			spread_dir[2] += crandom() * spread;
			spread_dir.normalize();
		}

		edict_t* fireball = G_Spawn();
		if (fireball)
		{
			fireball->s.origin = start;
			fireball->s.angles = vectoangles(spread_dir);
			fireball->velocity = spread_dir * rocketSpeed;
			fireball->movetype = MOVETYPE_FLYMISSILE;
			fireball->svflags |= SVF_PROJECTILE;
			fireball->flags |= FL_DODGE;
			fireball->clipmask = MASK_PROJECTILE;
			fireball->solid = SOLID_BBOX;
			fireball->s.effects = EF_FIREBALL | EF_TELEPORTER;
			fireball->s.renderfx = RF_MINLIGHT;
			fireball->s.modelindex = gi.modelindex("models/objects/gibs/skull/tris.md2");
			fireball->owner = self;

			// Store attacker info in case owner dies before projectile hits
			if (self) {
				if (self->client) {
					fireball->projectile_was_player_attacker = true;
					fireball->projectile_attacker_type_id = 0;
				} else if (self->svflags & SVF_MONSTER) {
					fireball->projectile_was_player_attacker = false;
					fireball->projectile_attacker_type_id = self->monsterinfo.monster_type_id;
				}
			}

			fireball->touch = fireball_touch;
			fireball->nextthink = level.time + 7_sec;
			fireball->think = G_FreeEdict;
			fireball->dmg = irandom(22, 34) * M_DamageModifier(self);
			fireball->radius_dmg = 45 * M_DamageModifier(self);
			fireball->dmg_radius = 120;
			fireball->s.sound = gi.soundindex("weapons/rockfly.wav");
			fireball->classname = "berserk_fireball";

			// Fixed scale for Berserker (not frame-based like Shambler)
			fireball->s.scale = 0.8f;

			gi.linkentity(fireball);
		}
	}

	gi.sound(self, CHAN_WEAPON, sound_fireball, 1, ATTN_NORM, 0);
}

MONSTERINFO_ATTACK(berserk_attack) (edict_t* self) -> void
{
	float const dist = range_to(self, self->enemy);

	// If in melee range, perform a melee attack.
	if (self->monsterinfo.melee_debounce_time <= level.time && (dist < MELEE_DISTANCE))
	{
		berserk_melee(self);
		return;
	}

	// The old dedicated ranged attack has been removed. The new passive zap
	// happens automatically during the run animation via berserk_check_passive_zap.

	// Logic for the jump/slam attack.
	if (!self->spawnflags.has(SPAWNFLAG_BERSERK_NOJUMPING) && (self->timestamp < level.time && brandom()) && dist > 150.f)
	{
		M_SetAnimation(self, &berserk_move_attack_strike);
		gi.sound(self, CHAN_WEAPON, sound_jump, 1, ATTN_NORM, 0);
		self->timestamp = level.time + 5_sec;
		return;
	}
	
	// Logic to transition from a standard run into a running-melee attack.
if ((self->monsterinfo.active_move == &berserk_move_run_start || self->monsterinfo.active_move == &berserk_move_run_loop) && (dist <= RANGE_NEAR))
	{
		M_SetAnimation(self, &berserk_move_run_attack1);
		self->monsterinfo.nextframe = FRAME_r_att1 + (self->s.frame - FRAME_run1) + 1;
	}
}

mframe_t berserk_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(berserk_move_pain1) = { FRAME_painc1, FRAME_painc4, berserk_frames_pain1, berserk_run };

mframe_t berserk_frames_pain2[] = {
	{ ai_move },
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
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep }
};
MMOVE_T(berserk_move_pain2) = { FRAME_painb1, FRAME_painb20, berserk_frames_pain2, berserk_run };

extern const mmove_t berserk_move_jump, berserk_move_jump2;

PAIN(berserk_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	// if we're jumping, don't pain
	if ((self->monsterinfo.active_move == &berserk_move_jump) ||
		(self->monsterinfo.active_move == &berserk_move_jump2) ||
		(self->monsterinfo.active_move == &berserk_move_attack_strike))
	{
		return;
	}

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;
	gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	monster_done_dodge(self);

	if ((damage <= 50) || (frandom() < 0.5f))
		M_SetAnimation(self, &berserk_move_pain1);
	else
		M_SetAnimation(self, &berserk_move_pain2);
}

MONSTERINFO_SETSKIN(berserk_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

void berserk_dead(edict_t* self)
{
	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, -8 };
	monster_dead(self);
}

static void berserk_shrink(edict_t* self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t berserk_frames_death1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move, 0, berserk_shrink },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(berserk_move_death1) = { FRAME_death1, FRAME_death13, berserk_frames_death1, berserk_dead };

mframe_t berserk_frames_death2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, berserk_shrink },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(berserk_move_death2) = { FRAME_deathc1, FRAME_deathc8, berserk_frames_death2, berserk_dead };

DIE(berserk_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);

	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum = 0;

		ThrowGibs(self, damage, {
			{ 2, "models/objects/gibs/bone/tris.md2" },
			{ 3, "models/objects/gibs/sm_meat/tris.md2" },
			{ 1, "models/objects/gibs/gear/tris.md2" },
			{ "models/monsters/berserk/gibs/chest.md2", GIB_SKINNED },
			{ "models/monsters/berserk/gibs/hammer.md2", GIB_SKINNED | GIB_UPRIGHT },
			{ "models/monsters/berserk/gibs/thigh.md2", GIB_SKINNED },
			{ "models/monsters/berserk/gibs/head.md2", GIB_HEAD | GIB_SKINNED }
			});
		self->deadflag = true;
		return;
	}

	if (self->deadflag)
		return;

	gi.sound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	self->deadflag = true;
	self->takedamage = true;


	if (damage >= 50)
		M_SetAnimation(self, &berserk_move_death1);
	else
		M_SetAnimation(self, &berserk_move_death2);
}

//===========
// PGM
void berserk_jump_now(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 100);
	self->velocity += (up * 300);
}

void berserk_jump2_now(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 150);
	self->velocity += (up * 400);
}

void berserk_jump_wait_land(edict_t* self)
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

mframe_t berserk_frames_jump[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, berserk_jump_now },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, berserk_jump_wait_land },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(berserk_move_jump) = { FRAME_jump1, FRAME_jump9, berserk_frames_jump, berserk_run };

mframe_t berserk_frames_jump2[] = {
	{ ai_move, -8 },
	{ ai_move, -4 },
	{ ai_move, -4 },
	{ ai_move, 0, berserk_jump2_now },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, berserk_jump_wait_land },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(berserk_move_jump2) = { FRAME_jump1, FRAME_jump9, berserk_frames_jump2, berserk_run };

void berserk_jump(edict_t* self, blocked_jump_result_t result)
{
	if (!self->enemy)
		return;

	if (result == blocked_jump_result_t::JUMP_JUMP_UP)
		M_SetAnimation(self, &berserk_move_jump2);
	else
		M_SetAnimation(self, &berserk_move_jump);
}

MONSTERINFO_BLOCKED(berserk_blocked) (edict_t* self, float dist) -> bool
{
	if (auto const result = blocked_checkjump(self, dist); result != blocked_jump_result_t::NO_JUMP)
	{
		if (result != blocked_jump_result_t::JUMP_TURN)
			berserk_jump(self, result);

		return true;
	}

	if (blocked_checkplat(self, dist))
		return true;

	return false;
}
// PGM
//===========

MONSTERINFO_SIDESTEP(berserk_sidestep) (edict_t* self) -> bool
{
	// if we're jumping or in long pain, don't dodge
	if ((self->monsterinfo.active_move == &berserk_move_jump) ||
		(self->monsterinfo.active_move == &berserk_move_jump2) ||
		(self->monsterinfo.active_move == &berserk_move_attack_strike) ||
		(self->monsterinfo.active_move == &berserk_move_pain2))
		return false;

	if (self->monsterinfo.active_move != &berserk_move_run_start)
		M_SetAnimation(self, &berserk_move_run_start);

	return true;
}

mframe_t berserk_frames_duck[] = {
	{ ai_move, 0, monster_duck_down },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_duck_hold },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_duck_up },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(berserk_move_duck) = { FRAME_duck1, FRAME_duck10, berserk_frames_duck, berserk_run };

mframe_t berserk_frames_duck2[] = {
	{ ai_move, 21, monster_duck_down },
	{ ai_move, 28 },
	{ ai_move, 20 },
	{ ai_move, 12, monster_footstep },
	{ ai_move, 7 },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move, 0, monster_duck_hold },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0, monster_footstep },
	{ ai_move, 0, monster_duck_up },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
};
MMOVE_T(berserk_move_duck2) = { FRAME_fall2, FRAME_fall18, berserk_frames_duck2, berserk_run };

MONSTERINFO_DUCK(berserk_duck) (edict_t* self, gtime_t eta) -> bool
{
	// berserk only dives forward, and very rarely
	if (frandom() >= 0.05f)
	{
		return false;
	}

	// if we're jumping, don't dodge
	if ((self->monsterinfo.active_move == &berserk_move_jump) ||
		(self->monsterinfo.active_move == &berserk_move_jump2))
	{
		return false;
	}

	M_SetAnimation(self, &berserk_move_duck2);

	return true;
}

/*QUAKED monster_berserk (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 */
void SP_monster_berserk(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (g_horde->integer && current_wave_level <= 18) {
		const float randomChance = frandom();

		if (randomChance < 0.12f) {
			gi.sound(self, CHAN_VOICE, sound_idle2, 1, ATTN_NORM, 0);
		}
		else if (randomChance < 0.24f) {
			gi.sound(self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
		}
		// No need for else clause if we're not doing anything
	}

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	// pre-caches
	sound_pain.assign("berserk/berpain2.wav");
	sound_die.assign("berserk/berdeth2.wav");
	sound_idle.assign("berserk/beridle1.wav");
	sound_idle2.assign("berserk/idle.wav");
	sound_punch.assign("berserk/attack.wav");
	sound_search.assign("berserk/bersrch1.wav");
	sound_sight.assign("berserk/sight.wav");
	sound_thud.assign("mutant/thud1.wav");
	sound_explod.assign("world/explod2.wav");
	sound_jump.assign("berserk/jump.wav");
	sound_windup.assign("shambler/sattck1.wav");
	sound_fireball.assign("weapons/rocklx1a.wav");

	// Set the base ID. This is the default.
    self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::BERSERK);

	self->s.modelindex = gi.modelindex("models/monsters/berserk/tris.md2");

	gi.modelindex("models/monsters/berserk/gibs/head.md2");
	gi.modelindex("models/monsters/berserk/gibs/chest.md2");
	gi.modelindex("models/monsters/berserk/gibs/hammer.md2");
	gi.modelindex("models/monsters/berserk/gibs/thigh.md2");

	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, 32 };
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	//if (!st.was_key_specified("power_armor_type"))
	//	self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
	//if (!st.was_key_specified("power_armor_power"))
	//	self->monsterinfo.power_armor_power = 95;

	self->health = 295 * st.health_multiplier;

	// Extra health scaling for high wave special Berserk waves (wave 15+)
	extern int16_t current_wave_level;
	if (current_wave_level >= 15)
	{
		float wave_scaling = 1.0f + (current_wave_level - 15) * 0.15f; // +15% per wave after 15
		self->health = static_cast<int>(self->health * wave_scaling);
	}

	self->gib_health = -60;
	self->mass = 250;
	//self->s.scale = 1.2f;


	self->pain = berserk_pain;
	self->die = berserk_die;

	self->monsterinfo.stand = berserk_stand;
	self->monsterinfo.walk = berserk_walk;
	self->monsterinfo.run = berserk_run;
	// pmm
	self->monsterinfo.dodge = M_MonsterDodge;
	self->monsterinfo.duck = berserk_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = berserk_sidestep;
	self->monsterinfo.blocked = berserk_blocked; // PGM
	// pmm
	self->monsterinfo.attack = berserk_attack;
	self->monsterinfo.melee = berserk_melee;
	self->monsterinfo.sight = berserk_sight;
	self->monsterinfo.search = berserk_search;
	self->monsterinfo.setskin = berserk_setskin;

	M_SetAnimation(self, &berserk_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	self->monsterinfo.combat_style = COMBAT_MELEE;
	self->monsterinfo.can_jump = !self->spawnflags.has(SPAWNFLAG_BERSERK_NOJUMPING);
	self->monsterinfo.drop_height = 256;
	// HORDE MOD: Increased jump height from 40 to 52 (30% increase) for better obstacle navigation
	self->monsterinfo.jump_height = 52;

	gi.linkentity(self);

	walkmonster_start(self);

	ApplyMonsterBonusFlags(self);
}

// Forward declarations for bombspell functions (from horde/g_bombspell.cpp)
void BerserkerKLSaveLoc(edict_t* self);
void BerserkerKLBombSpell(edict_t* self);
void ZerkGoSpellbomb(edict_t* self);
void ZerkGoSlam(edict_t* self){

		M_SetAnimation(self, &berserk_move_attack_strike);
		gi.sound(self, CHAN_WEAPON, sound_jump, 1, ATTN_NORM, 0);
}



mframe_t berserkerkl_frames_forward_cast[] = {

	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge, 0, berserk_swing},
	{ai_charge, 0, BerserkerKLSaveLoc},
	{ai_charge, 0, BerserkerKLBombSpell}, //28
	{ai_charge, 0, BerserkerKLBombSpell},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge}};
MMOVE_T(berserkerkl_move_forward_cast) = { FRAME_att_c19, FRAME_att_c34, berserkerkl_frames_forward_cast, ZerkGoSpellbomb };

mframe_t berserkerkl_frames_spellbomb_cast[] = {

	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge, 0, berserk_swing},
	{ai_charge, 0, BerserkerKLSaveLoc},
	{ai_charge, 0, BerserkCastFireballs},
	{ai_charge, 0, BerserkCastFireballs},
	{ai_charge, 0, BerserkerKLBombSpell},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge}};
MMOVE_T(berserkerkl_move_spellbomb_cast) = { FRAME_att_b1, FRAME_att_b21, berserkerkl_frames_spellbomb_cast, ZerkGoSlam };

void ZerkGoSpellbomb(edict_t* self)
{
		M_SetAnimation(self, &berserkerkl_move_spellbomb_cast);
		gi.sound(self, CHAN_WEAPON, sound_jump, 1, ATTN_NORM, 0);
}

//======================================================================
// BERSERKERKL - "The Trespasser"
// Boss variant with bombspells and enhanced charge attacks
//======================================================================

// Forward declarations for bombspell functions (from horde/g_bombspell.cpp)
extern void carpetbomb_think(edict_t* self);
extern void carpetslam_think(edict_t* self);
extern void bombarea_think(edict_t* self);

// Save enemy location for bombspell targeting
void BerserkerKLSaveLoc(edict_t* self)
{
	if (M_HasValidTarget(self))
	{
		self->pos1 = self->enemy->s.origin;
		self->pos1[2] += self->enemy->viewheight;
	}
}

// BerserkerKL bomb spell - focused on carpet bombs for charge attacks
void BerserkerKLBombSpell(edict_t* self)
{
	if (!M_HasValidTarget(self))
		return;

	// Check if on cooldown using fire_wait
	if (self->monsterinfo.fire_wait > level.time)
		return;

	vec3_t forward, dir;
	AngleVectors(self->s.angles, forward, nullptr, nullptr);

	// Use saved location if available, otherwise use current enemy position
	vec3_t target_pos = (self->pos1.lengthSquared() > 0) ? self->pos1 : self->enemy->s.origin;
	dir = (target_pos - self->s.origin).normalized();

	// Calculate distance to enemy
	float dist_to_enemy = (target_pos - self->s.origin).length();

	// BerserkerKL prefers carpet bombs (melee-oriented)
	bool use_carpet = (dist_to_enemy < 800) || (frandom() < 0.7f);

	if (use_carpet)
	{
		// Carpet slam for charge paths - uses berserker slam attacks!
		edict_t* spell = G_Spawn();
		spell->think = carpetslam_think;
		spell->nextthink = level.time + FRAME_TIME_MS;
		spell->s.origin = self->s.origin;
		spell->move_origin = self->s.origin;
		spell->timestamp = level.time + 4_sec; // CARPETSLAM_DURATION
		spell->owner = self;
		spell->mins = vec3_origin;
		spell->maxs = vec3_origin;
		spell->solid = SOLID_NOT;
		spell->svflags |= SVF_NOCLIENT | SVF_PROJECTILE;
		spell->classname = "carpetslam";
		spell->s.angles = vectoangles(forward);
		spell->count = 1; // Monster-owned

		gi.linkentity(spell);

		// Shorter cooldown for aggressive playstyle
		self->monsterinfo.fire_wait = level.time + 3_sec;
		self->monsterinfo.lefty = 1;
	}
	else
	{
		// Area bomb for ranged attacks
		vec3_t ground_pos;
		trace_t ground_tr;

		ground_pos = target_pos;
		ground_pos[2] += 8;
		ground_tr = gi.traceline(ground_pos, ground_pos - vec3_t{0, 0, 8192}, self, MASK_SOLID);
		ground_pos = ground_tr.endpos;

		edict_t* spell = G_Spawn();
		spell->think = bombarea_think;
		spell->nextthink = level.time + FRAME_TIME_MS;
		spell->s.origin = ground_pos;
		spell->move_origin = ground_pos;
		spell->dmg = 50 + irandom(20, 30);
		spell->dmg_radius = 200;
		spell->timestamp = level.time + 2.5_sec;
		spell->owner = self;
		spell->mins = vec3_origin;
		spell->maxs = vec3_origin;
		spell->solid = SOLID_NOT;
		spell->svflags |= SVF_NOCLIENT | SVF_PROJECTILE;
		spell->classname = "bombspell";
		spell->count = 1;

		gi.linkentity(spell);

		self->monsterinfo.fire_wait = level.time + 3_sec;
		self->monsterinfo.lefty = 0;
	}

	// Play windup sound for feedback
	gi.sound(self, CHAN_WEAPON, sound_windup, 1, ATTN_NORM, 0);
}


void berserkkl_attack_club(edict_t* self)
{
	vec3_t  const aim = { MELEE_DISTANCE, self->mins[0], -4 };

	if (!M_HasValidTarget(self))
	{
		self->monsterinfo.melee_debounce_time = level.time + 0.8_sec;
		return;
	}

	if (!fire_hit(self, aim, irandom(21, 28) * M_DamageModifier(self), 250))
		self->monsterinfo.melee_debounce_time = level.time + 0.4_sec;
}

void berserkkl_attack_spike(edict_t* self)
{
	vec3_t  const aim = { MELEE_DISTANCE, self->mins[0], -4 };

	if (!M_HasValidTarget(self))
	{
		self->monsterinfo.melee_debounce_time = level.time + 0.8_sec;
		return;
	}

	if (!fire_hit(self, aim, irandom(15, 22) * M_DamageModifier(self), 200))
		self->monsterinfo.melee_debounce_time = level.time + 0.3_sec;
}


mframe_t berserkkl_frames_attack_spike[] = {

	{ ai_charge, 0, berserk_swing },
	{ ai_charge, 0, berserkkl_attack_spike },
	{ ai_charge, 0, berserkkl_attack_spike },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(berserkkl_move_attack_spike) = { FRAME_att_c1, FRAME_att_c8, berserkkl_frames_attack_spike, berserk_run };


mframe_t berserkkl_frames_attack_club[] = {
	{ ai_charge, 0, monster_footstep },
	{ ai_charge },
	{ ai_charge, 0, berserk_swing },
	{ ai_charge },
	{ ai_charge, 0, berserkkl_attack_club },
	{ ai_charge, 0, berserkkl_attack_club },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(berserkkl_move_attack_club) = { FRAME_att_c9, FRAME_att_c20, berserkkl_frames_attack_club, berserk_run };



// Melee attack for BerserkerKL 
MONSTERINFO_MELEE(berserkkl_melee) (edict_t* self) -> void
{
	if (self->monsterinfo.melee_debounce_time > level.time)
		return;
	// if we're *almost* ready to land down the hammer from run-attack
	// don't switch us
	else if (self->monsterinfo.active_move == &berserk_move_run_attack1 && self->s.frame >= FRAME_r_att13)
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		self->monsterinfo.attack_finished = 0_ms;
		return;
	}

	monster_done_dodge(self);

	if (brandom())
		M_SetAnimation(self, &berserk_move_attack_spike);
	else
		M_SetAnimation(self, &berserk_move_attack_club);
}

// Enhanced attack AI for BerserkerKL
MONSTERINFO_ATTACK(berserkerkl_attack) (edict_t* self) -> void
{
	float const dist = range_to(self, self->enemy);

	// Melee range - always use melee
	if (self->monsterinfo.melee_debounce_time <= level.time && (dist < MELEE_DISTANCE))
	{
		berserk_melee(self);
		return;
	}

	// Check if bombspell is ready (not on cooldown)
	bool bombspell_ready = (self->monsterinfo.fire_wait <= level.time);

	// Mid range (150-500) with bombspell ready - use carpet bomb
	if (bombspell_ready && dist >= 150 && dist < 500)
	{
		// Trigger bombspell attack (use one of the attack animations)
		brandom() ? M_SetAnimation(self, &berserkerkl_move_forward_cast):
					M_SetAnimation(self, &berserkerkl_move_spellbomb_cast);
		return;
	}

	// Jump/slam attack if available and mid-range
	if (!self->spawnflags.has(SPAWNFLAG_BERSERK_NOJUMPING) && (self->timestamp < level.time && brandom()) && dist > 150.f && dist < 600.f)
	{
		self->timestamp = level.time + 0.5_sec;
		M_SetAnimation(self, &berserk_move_jump);
		return;
	}

	// Default to standard charge attack
	berserk_run(self);
}

MONSTERINFO_DODGE(berserkerkl_dodge) (edict_t* self, edict_t* attacker, gtime_t eta, trace_t* tr, bool gravity) -> void
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

	// Don't dodge if we're attacking melee or jump attacking
	if (self->monsterinfo.active_move == &berserkerkl_move_forward_cast ||
		self->monsterinfo.active_move == &berserkerkl_move_spellbomb_cast)
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

	// Get our right vector for lateral dodge
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

	if (dist < 400.0f) {
		// Close range - dodge backward
		dodge_dir += forward * -0.3f;
	} else if (dist > 800.0f) {
		// Long range - dodge forward to close distance
		dodge_dir += forward * 0.2f;
	}

	dodge_dir = dodge_dir.normalized();

	// Calculate dodge speed based on urgency (eta)
	float base_dodge_speed = 300.0f;
	float eta_seconds = eta.seconds();
	float urgency_multiplier = std::clamp(2.0f - eta_seconds, 1.0f, 2.5f);
	float dodge_speed = base_dodge_speed * urgency_multiplier;

	// Apply dodge velocity
	vec3_t dodge_velocity = dodge_dir * dodge_speed;
	
	// Preserve some vertical momentum but replace horizontal
	dodge_velocity.z = self->velocity.z * 0.5f;
	self->velocity = dodge_velocity;

	// Set animation to running for dodge
	if (self->monsterinfo.active_move != &berserk_move_run_attack1)
		M_SetAnimation(self, &berserk_move_run_attack1);

	// Set cooldown using timestamp (like spider)
	self->timestamp = level.time + random_time(0.5_sec, 1.5_sec);

	// Also set pausetime for movement consistency
	self->monsterinfo.pausetime = level.time + random_time(0.3_sec, 0.7_sec);

	// Update lefty for consistency with sidestep
	self->monsterinfo.lefty = (side_dot > 0) ? 1 : 0;

	// Mark that we're dodging
	monster_done_dodge(self);
}

/*QUAKED monster_berserkerkl (1 .5 0) (-26 -26 -38) (26 26 51) Ambush Trigger_Spawn Sight
"The Trespasser" - Boss variant of berserker with bombspell attacks and enhanced charge
Only spawns during special foggy Berserker waves
*/
void SP_monster_berserkerkl(edict_t* self)
{
	// Set monster type first
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::BERSERKERKL);

	// Call base berserk spawn
	SP_monster_berserk(self);

	// Override with berserkerkl specifics
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::BERSERKERKL);

	// Boss stats
	self->health = 900 * ED_GetSpawnTemp().health_multiplier;
	self->gib_health = -150;
	self->mass = 400;
	self->s.skinnum = 2; // Different skin if available

	// Scale up
	if (!self->s.scale)
	{
		self->s.scale = 1.4f;
		self->mins *= self->s.scale;
		self->maxs *= self->s.scale;
	}

	// Enhanced movement
	self->monsterinfo.drop_height = 384;
	self->monsterinfo.jump_height = 128;
	self->monsterinfo.can_jump = true;

	self->monsterinfo.bonus_flags |= BF_CORRUPTED;

	// Override attack with enhanced version
	self->monsterinfo.attack = berserkerkl_attack;
	self->monsterinfo.dodge = berserkerkl_dodge;
	self->monsterinfo.melee = berserkkl_melee;
	// Power armor
	if (!ED_GetSpawnTemp().was_key_specified("power_armor_type"))
		self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
	if (!ED_GetSpawnTemp().was_key_specified("power_armor_power"))
		self->monsterinfo.power_armor_power = 800;

	// Sound precaching (bombspell sounds precached elsewhere)
	gi.soundindex("weapons/rockfly.wav");

	ApplyMonsterBonusFlags(self);
}