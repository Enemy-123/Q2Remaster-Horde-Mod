// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

SHAMBLER

==============================================================================
*/

#include "g_local.h"
#include "m_shambler.h"
#include "m_flash.h"
#include "shared.h"
#include "monster_constants.h"

static cached_soundindex sound_pain;
static cached_soundindex sound_idle;
static cached_soundindex sound_die;
static cached_soundindex sound_sight;
static cached_soundindex sound_windup;
static cached_soundindex sound_melee1;
static cached_soundindex sound_melee2;
static cached_soundindex sound_smack;
static cached_soundindex sound_boom;
static cached_soundindex sound_fireball;

//
// misc
//

MONSTERINFO_SIGHT(shambler_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
}

constexpr vec3_t lightning_left_hand[] = {
	{ 44, 36, 25 },
	{ 10, 44, 57 },
	{ -1, 40, 70 },
	{ -10, 34, 75 },
	{ 7.4f, 24, 89 }
};

constexpr vec3_t lightning_right_hand[] = {
	{ 28, -38, 25 },
	{ 31, -7, 70 },
	{ 20, 0, 80 },
	{ 16, 1.2f, 81 },
	{ 27, -11, 83 }
};

constexpr float FIREBALL_HAND_Z_OFFSET = 20.0f;
void shambler_windupFire(edict_t* self);


constexpr vec3_t fireball_left_hand[] = {
	{ 44, 36, 45 },    // Aumentado Z de 25 a 45
	{ 10, 44, 77 },    // Aumentado Z de 57 a 77
	{ -1, 40, 90 },    // Aumentado Z de 70 a 90
	{ -10, 34, 95 },   // Aumentado Z de 75 a 95
	{ 7.4f, 24, 109 }  // Aumentado Z de 89 a 109
};

constexpr vec3_t fireball_right_hand[] = {
	{ 28, -38, 45 },    // Aumentado Z de 25 a 45
	{ 31, -7, 90 },     // Aumentado Z de 70 a 90
	{ 20, 0, 100 },     // Aumentado Z de 80 a 100
	{ 16, 1.2f, 101 },  // Aumentado Z de 81 a 101
	{ 27, -11, 103 }    // Aumentado Z de 83 a 103
};

static void shambler_fireball_update(edict_t* self)
{
	// Use self->beam2 for the fireball charging effect
	edict_t* fireball_effect = self->beam2;
	if (!fireball_effect) {
		// Create new fireball effect
		fireball_effect = G_Spawn();
		self->beam2 = fireball_effect; // Assign to beam2
		fireball_effect->s.effects = EF_FIREBALL | EF_BARREL_EXPLODING; // EF_BARREL_EXPLODING is unusual, ensure it's intended
		fireball_effect->s.renderfx = RF_GLOW;
		fireball_effect->movetype = MOVETYPE_NONE;
		fireball_effect->solid = SOLID_NOT;
		gi.setmodel(fireball_effect, "models/objects/gibs/sm_meat/tris.md2"); // Meat model for charging
	}

	vec3_t f, r;
	AngleVectors(self->s.angles, f, r, nullptr);

	// Calculate positions for both hands, deriving from lightning positions + Z offset
	// Indexing based on FRAME_smash01
	int frame_index = self->s.frame - FRAME_smash01;

	// Safety check: if we're not in the smash animation anymore, clean up
	if (frame_index < 0 || frame_index >= static_cast<int>(q_countof(lightning_left_hand)))
	{
		G_FreeEdict(fireball_effect);
		self->beam2 = nullptr;
		return;
	}

	vec3_t temp_left_hand_pos = lightning_left_hand[frame_index];
	temp_left_hand_pos[2] += FIREBALL_HAND_Z_OFFSET;
	const vec3_t left_pos = M_ProjectFlashSource(self, temp_left_hand_pos, f, r);

	vec3_t temp_right_hand_pos = lightning_right_hand[frame_index];
	temp_right_hand_pos[2] += FIREBALL_HAND_Z_OFFSET;
	const vec3_t right_pos = M_ProjectFlashSource(self, temp_right_hand_pos, f, r);

	// Calculate the midpoint between hands for the fireball effect
	const vec3_t midpoint = (left_pos + right_pos) * 0.5f;

	// Update fireball effect position
	fireball_effect->s.origin = midpoint;

	// Calculate size based on frame, using lightning_left_hand count
	const float size_factor = static_cast<float>(frame_index) /
		static_cast<float>(q_countof(lightning_left_hand) -1); // -1 because index is 0 to N-1, count is N
	constexpr float max_size = 1.5f; // Maximum size multiplier
	const float current_size = 0.1f + (max_size - 0.1f) * size_factor;

	// Update size
	fireball_effect->s.scale = current_size;

	// Add pulsating effect using gtime_t
	const gtime_t current_time = level.time;
	const float pulse = sinf(current_time.seconds<float>() * 0.01f * PIf) * 0.2f + 0.8f; // Consider making 0.01f a named const or relate to game speed

	// Apply pulse to scale, and track the monster's own scale so the charge ball
	// matches a resized/boss shambler (the midpoint already scales via M_ProjectFlashSource).
	fireball_effect->s.scale *= pulse * (self->s.scale ? self->s.scale : 1.0f);

	// Add SpawnGrow-like effect
	if (frandom() < 0.3f) { // 30% chance each frame to spawn a particle
		SpawnGrow_Spawn(midpoint, 10.0f, 1.0f);
	}

	gi.linkentity(fireball_effect);
}

void shambler_windupFire(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_windup, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
	// Ensure self->beam2 is null so shambler_fireball_update creates/reinitializes the effect
	if (self->beam2) { // If there's an old one for some reason, free it
	    G_FreeEdict(self->beam2);
	}
	self->beam2 = nullptr;
	shambler_fireball_update(self); // Initial update will create the beam2 entity
}

//
static void shambler_lightning_update(edict_t* self)
{
	edict_t* lightning = self->beam;

	if (!lightning) {
		// Handle error: lightning is nullptr
		return;
	}

	// Safety check: if we're not in the magic attack animation anymore, clean up
	if (self->s.frame < FRAME_magic01 || self->s.frame >= FRAME_magic01 + static_cast<int>(q_countof(lightning_left_hand)))
	{
		G_FreeEdict(lightning);
		self->beam = nullptr;
		return;
	}

	vec3_t f, r;
	AngleVectors(self->s.angles, f, r, nullptr);
	lightning->s.origin = M_ProjectFlashSource(self, lightning_left_hand[self->s.frame - FRAME_magic01], f, r);
	lightning->s.old_origin = M_ProjectFlashSource(self, lightning_right_hand[self->s.frame - FRAME_magic01], f, r);
	gi.linkentity(lightning);
}

void shambler_windup(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_windup, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);

	edict_t* lightning = self->beam = G_Spawn();
	lightning->s.modelindex = gi.modelindex("models/proj/lightning/tris.md2");
	lightning->s.renderfx |= RF_BEAM;
	lightning->owner = self;
	shambler_lightning_update(self); // Call shambler_lightning_update after lightning is initialized
}

MONSTERINFO_IDLE(shambler_idle) (edict_t* self) -> void
{
	gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

void shambler_maybe_idle(edict_t* self)
{
	if (frandom() > 0.8)
		gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

//
// stand
//

mframe_t shambler_frames_stand[] = {
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
MMOVE_T(shambler_move_stand) = { FRAME_stand01, FRAME_stand17, shambler_frames_stand, nullptr };

MONSTERINFO_STAND(shambler_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &shambler_move_stand);
}

//
// walk
//

void shambler_walk(edict_t* self);

mframe_t shambler_frames_walk[] = {
	{ ai_walk, 10 }, // FIXME: add footsteps?
	{ ai_walk, 9 },
	{ ai_walk, 9 },
	{ ai_walk, 5 },
	{ ai_walk, 6 },
	{ ai_walk, 12 },
	{ ai_walk, 8 },
	{ ai_walk, 3 },
	{ ai_walk, 13 },
	{ ai_walk, 9 },
	{ ai_walk, 7, shambler_maybe_idle },
	{ ai_walk, 5 },
};
MMOVE_T(shambler_move_walk) = { FRAME_walk01, FRAME_walk12, shambler_frames_walk, nullptr };

MONSTERINFO_WALK(shambler_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &shambler_move_walk);
}

//
// run
//

void shambler_run(edict_t* self);

mframe_t shambler_frames_run[] = {
	{ ai_run, 20 }, // FIXME: add footsteps?
	{ ai_run, 24 },
	{ ai_run, 20 },
	{ ai_run, 20 },
	{ ai_run, 24 },
	{ ai_run, 20, shambler_maybe_idle },
};
MMOVE_T(shambler_move_run) = { FRAME_run01, FRAME_run06, shambler_frames_run, nullptr };

MONSTERINFO_RUN(shambler_run) (edict_t* self) -> void
{
	if (self->enemy && self->enemy->client)
		self->monsterinfo.aiflags |= AI_BRUTAL;
	else
		self->monsterinfo.aiflags &= ~AI_BRUTAL;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		M_SetAnimation(self, &shambler_move_stand);
		return;
	}

	M_SetAnimation(self, &shambler_move_run);
}

//
// pain
//

// FIXME: needs halved explosion damage

static void shambler_cleanup_effects(edict_t* self)
{
	// Clean up lightning beam
	if (self->beam)
	{
		self->beam->s.modelindex = 0;
		self->beam->s.renderfx = RF_NONE;
		self->beam->solid = SOLID_NOT;
		gi.unlinkentity(self->beam);
		G_FreeEdict(self->beam);
		self->beam = nullptr;
	}

	// Clean up fireball charging effect
	if (self->beam2)
	{
		self->beam2->s.modelindex = 0;
		self->beam2->s.effects = EF_NONE;
		self->beam2->s.renderfx = RF_NONE;
		self->beam2->solid = SOLID_NOT;
		gi.unlinkentity(self->beam2);
		G_FreeEdict(self->beam2);
		self->beam2 = nullptr;
	}
}

mframe_t shambler_frames_pain[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
};
MMOVE_T(shambler_move_pain) = { FRAME_pain01, FRAME_pain06, shambler_frames_pain, shambler_run };

PAIN(shambler_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (level.time < self->timestamp)
		return;

	self->timestamp = level.time + 1.5_sec;
	gi.sound(self, CHAN_AUTO, sound_pain, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);

	if (mod.id != MOD_CHAINFIST && damage <= 30 && frandom() > 0.2f)
		return;

	// If hard or nightmare, don't go into pain while attacking
	if (skill->integer >= 2)
	{
		if ((self->s.frame >= FRAME_smash01) && (self->s.frame <= FRAME_smash12))
			return;

		if ((self->s.frame >= FRAME_swingl01) && (self->s.frame <= FRAME_swingl09))
			return;

		if ((self->s.frame >= FRAME_swingr01) && (self->s.frame <= FRAME_swingr09))
			return;
	}

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 2_sec;

	// Clean up any active attack effects (lightning beam, fireball charging)
	shambler_cleanup_effects(self);

	M_SetAnimation(self, &shambler_move_pain);
}

MONSTERINFO_SETSKIN(shambler_setskin) (edict_t* self) -> void
{
	// FIXME: create pain skin?
	//if (self->health < (self->max_health / 2))
	//	self->s.skinnum |= 1;
	//else
	//	self->s.skinnum &= ~1;
}

//
// attacks
//

/*
void() sham_magic3     =[      $magic3,       sham_magic4    ] {
	ai_face();
	self.nextthink = self.nextthink + 0.2;
	local entity o;

	self.effects = self.effects | EF_MUZZLEFLASH;
	ai_face();
	self.owner = spawn();
	o = self.owner;
	setmodel (o, "progs/s_light.mdl");
	setorigin (o, self.origin);
	o.angles = self.angles;
	o.nextthink = time + 0.7;
	o.think = SUB_Remove;
};
*/

void ShamblerSaveLoc(edict_t* self)
{

	if (!M_HasValidTarget(self))
	{
		return; // Can't at a non-existent or dead target.
	}

	self->pos1 = self->enemy->s.origin; // save for aiming the shot
	self->pos1[2] += self->enemy->viewheight;
	self->monsterinfo.nextframe = FRAME_magic09;

	gi.sound(self, CHAN_WEAPON, sound_boom, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
	shambler_lightning_update(self);
}

constexpr spawnflags_t SPAWNFLAG_SHAMBLER_PRECISE = 1_spawnflag;

vec3_t FindShamblerOffset(edict_t* self)
{
	vec3_t offset = { 0, 0, 48.f };

	for (int i = 0; i < 8; i++)
	{
		if (M_CheckClearShot(self, offset))
			return offset;

		offset.z -= 4.f;
	}

	return { 0, 0, 48.f };
}

void ShamblerCastLightning(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t start;
	vec3_t dir;
	vec3_t forward, right;
	vec3_t const offset = FindShamblerOffset(self);

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, offset, forward, right);

	// calc direction to where we targted
	if (g_swap_coop_monsters->integer || current_wave_level >= 22 || self->monsterinfo.IS_BOSS)
	{
		PredictAim(self, self->enemy, start, 0, false, 0.f, &dir, nullptr);
	}
	else
		PredictAim(self, self->enemy, start, 0, false, self->spawnflags.has(SPAWNFLAG_SHAMBLER_PRECISE) ? 0.f : 0.1f, &dir, nullptr);

	vec3_t const end = start + (dir * 8192);
	trace_t const tr = gi.traceline(start, end, self, MASK_PROJECTILE | CONTENTS_SLIME | CONTENTS_LAVA);

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_LIGHTNING);
	gi.WriteEntity(self);	// source entity
	gi.WriteEntity(world); // destination entity
	gi.WritePosition(start);
	gi.WritePosition(tr.endpos);
	gi.multicast(start, MULTICAST_PVS, false);

	int damage = M_GET_DMG_OR(self, LIGHTNING, 10);
	fire_bullet(self, start, dir, damage, 15 * M_DamageModifier(self), 0, 0, MOD_TESLA);
}



mframe_t shambler_frames_magic[] = {
	{ ai_charge, 0, shambler_windup },
	{ ai_charge, 0, shambler_lightning_update },
	{ ai_charge, 0, shambler_lightning_update },
	{ ai_move, 0, shambler_lightning_update },
	{ ai_move, 0, shambler_lightning_update },
	{ ai_move, 0, ShamblerSaveLoc},
	{ ai_move },
	{ ai_charge },
	{ ai_move, 0, ShamblerCastLightning },
	{ ai_move, 0, ShamblerCastLightning },
	{ ai_move, 0, ShamblerCastLightning },
	{ ai_move },
};

MMOVE_T(shambler_attack_magic) = { FRAME_magic01, FRAME_magic12, shambler_frames_magic, shambler_run };

// Like FindShamblerOffset, but for the two-handed fireball cast: returns the midpoint
// between the shambler's hands, stepped down until it clears solid so fireballs never
// spawn inside a low ceiling. Hand offsets are projected through M_ProjectFlashSource so
// the result already respects self->s.scale.
static vec3_t FindShamblerFireballOrigin(edict_t* self, const vec3_t& forward, const vec3_t& right)
{
	constexpr int charge_end_index = q_countof(lightning_left_hand) - 1;

	const float scale = self->s.scale ? self->s.scale : 1.0f;

	// reference point near the shambler's chest to trace from
	vec3_t chest = self->s.origin;
	chest[2] += 32.f * scale;

	vec3_t mid;
	trace_t tr;

	for (int i = 0; i < 8; i++)
	{
		const float z_off = FIREBALL_HAND_Z_OFFSET - (i * 8.f);

		vec3_t left = lightning_left_hand[charge_end_index];
		left[2] += z_off;
		vec3_t right_hand = lightning_right_hand[charge_end_index];
		right_hand[2] += z_off;

		mid = (M_ProjectFlashSource(self, left, forward, right) +
			M_ProjectFlashSource(self, right_hand, forward, right)) * 0.5f;

		tr = gi.traceline(chest, mid, self, MASK_SOLID);
		if (!tr.startsolid && tr.fraction == 1.0f)
			return mid;
	}

	// Still blocked at every height: use the last clear point along the trace,
	// nudged back toward the shambler so the projectile spawns in open space.
	vec3_t back = mid - chest;
	back.normalize();
	return tr.endpos - (back * 8.f);
}

void ShamblerCastFireballs(edict_t* self)
{
	if (!M_HasEnemy(self))
	{
		return; // Can't at a non-existent enemy.
	}

	vec3_t f, r;
	AngleVectors(self->s.angles, f, r, nullptr);

	// Ceiling-aware firing origin: like FindShamblerOffset, step the hand midpoint down
	// until it clears solid so fireballs never spawn inside a low ceiling.
	const vec3_t start = FindShamblerFireballOrigin(self, f, r);

	vec3_t dir;
	vec3_t target;
	const float rocketSpeed = 1200;
	const bool blindfire = (self->monsterinfo.aiflags & AI_MANUAL_STEERING) != 0;

	// Si estamos en modo blindfire, usar el target guardado
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
		// Smart targeting como el tank
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

		// Lead shot con probabilidad basada en dificultad
		if (frandom() <= 0.2f + ((3 - skill->integer) * 0.15f))
			PredictAim(self, self->enemy, start, rocketSpeed, false, 0, &dir, &target);
		else
		{
			dir = target - start;
			dir.normalize();
		}

		// Check de línea de visión
		trace_t const trace = gi.traceline(start, target, self, MASK_PROJECTILE);
		if (trace.fraction < 0.5f && !blindfire) // Original was < 1.0f, but 0.5f might be too strict. Reverted to < 0.5f as per original.
			return;
	}

	// Guardar última posición conocida para blindfire
	self->monsterinfo.blind_fire_target = target;

	// Fireballs fall under gravity (MOVETYPE_TOSS) like grenades. Scale speed to range (floor =
	// rocketSpeed so they stay fast/threatening, only boosting for distant targets) and solve the
	// launch pitch so they land on the target instead of arcing short. dir keeps its yaw/lead.
	const float speed = M_BallisticSpeedForTarget(start, target, rocketSpeed, 1600.f);
	M_CalculatePitchToFire(self, target, start, dir, speed, 2.5f, false);

	// Lanzar fireballs
	// Use the new fire_fireball function that spawns flames on explosion
	const int num_fireballs = (g_swap_coop_monsters->integer || self->monsterinfo.IS_BOSS) ? 3 : 1;
	const float spread_base = g_swap_coop_monsters->integer ? 0.03f : 0.06f;

	// Get damage values
	int base_damage = M_GET_DMG_OR(self, FIREBALL, 30);
	float damage_radius = 120.0f;
	int flames = 5;  // Number of flame entities spawned on explosion
	int flame_damage = base_damage / 3;  // Flame damage is 1/3 of fireball damage

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

		// Use new fire_fireball function - it spawns flames on explosion!
		fire_fireball(self, start, spread_dir, base_damage, damage_radius, static_cast<int>(speed), flames, flame_damage);
	}

	gi.sound(self, CHAN_WEAPON, sound_fireball, 1, ATTN_NORM, 0);
}

mframe_t shambler_frames_fireball[] = {
	{ ai_charge, 0, shambler_windupFire },
	{ ai_charge, 0, shambler_fireball_update },
	{ ai_charge, 0, shambler_fireball_update },
	{ ai_move, 0, shambler_fireball_update },
	{ ai_move, 0, shambler_fireball_update },
	{ ai_move, 0, ShamblerSaveLoc},
	{ ai_move },
	{ ai_charge },
	{ ai_move, 0, ShamblerCastFireballs },
	{ ai_move, 0, ShamblerCastFireballs },
	{ ai_move, 0, ShamblerCastFireballs },
	{ ai_move, 0, ShamblerCastFireballs },
};
MMOVE_T(shambler_attack_fireball) = { FRAME_smash01, FRAME_smash12, shambler_frames_fireball, shambler_run };

MONSTERINFO_ATTACK(shambler_attack) (edict_t* self) -> void
{
	monster_done_dodge(self);

	if (horde::IsMonsterType(self, horde::MonsterTypeID::SHAMBLER_SMALL)) {
		M_SetAnimation(self, &shambler_attack_magic);
		return;
	}
	else
		M_SetAnimation(self,
			brandom() ?
			&shambler_attack_magic :
			&shambler_attack_fireball);
	return;
}
//
// melee
//

void shambler_melee1(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_melee1, 1, ATTN_NORM, 0);
}

void shambler_melee2(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_melee2, 1, ATTN_NORM, 0);
}

void sham_swingl9(edict_t* self);
void sham_swingr9(edict_t* self);

void sham_smash10(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	ai_charge(self, 0);

	if (!CanDamage(self->enemy, self))
		return;

	int damage = M_MELEE_DMG(self);
	if (damage <= 0) damage = (horde::IsMonsterType(self, horde::MonsterTypeID::SHAMBLER_SMALL)) ? 45 : 115;

	vec3_t const aim = { MELEE_DISTANCE, self->mins[0], -4 };
	const bool hit = fire_hit(self, aim, damage, 120); // Slower attack

	if (hit)
		gi.sound(self, CHAN_WEAPON, sound_smack, 1, ATTN_NORM, 0);

	// SpawnMeatSpray(self.origin + v_forward * 16, crandom() * 100 * v_right);
	// SpawnMeatSpray(self.origin + v_forward * 16, crandom() * 100 * v_right);
}

void ShamClaw(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	ai_charge(self, 10);

	if (!CanDamage(self->enemy, self))
		return;

	int damage = M_MELEE_DMG(self);
	if (damage <= 0) damage = (horde::IsMonsterType(self, horde::MonsterTypeID::SHAMBLER_SMALL)) ? 45 : 115;
	damage = damage * 65 / 100; // Normal attack is ~65% of heavy attack

	const vec3_t aim = { MELEE_DISTANCE, self->mins[0], -4 };
	const bool hit = fire_hit(self, aim, damage, 80); // Slower attack

	if (hit)
		gi.sound(self, CHAN_WEAPON, sound_smack, 1, ATTN_NORM, 0);

	// 250 if left, -250 if right
	/*
	if (side)
	{
		makevectorsfixed(self.angles);
		SpawnMeatSpray(self.origin + v_forward * 16, side * v_right);
	}
	*/
}

mframe_t shambler_frames_smash[] = {
	{ ai_charge, 2, shambler_melee1 },
	{ ai_charge, 6 },
	{ ai_charge, 6 },
	{ ai_charge, 5 },
	{ ai_charge, 4 },
	{ ai_charge, 1 },
	{ ai_charge, 0 },
	{ ai_charge, 0 },
	{ ai_charge, 0 },
	{ ai_charge, 0, sham_smash10 },
	{ ai_charge, 5 },
	{ ai_charge, 4 },
};

MMOVE_T(shambler_attack_smash) = { FRAME_smash01, FRAME_smash12, shambler_frames_smash, shambler_run };

mframe_t shambler_frames_swingl[] = {
	{ ai_charge, 5, shambler_melee1 },
	{ ai_charge, 3 },
	{ ai_charge, 7 },
	{ ai_charge, 3 },
	{ ai_charge, 7 },
	{ ai_charge, 9 },
	{ ai_charge, 5, ShamClaw },
	{ ai_charge, 4 },
	{ ai_charge, 8, sham_swingl9 },
};

MMOVE_T(shambler_attack_swingl) = { FRAME_swingl01, FRAME_swingl09, shambler_frames_swingl, shambler_run };

mframe_t shambler_frames_swingr[] = {
	{ ai_charge, 1, shambler_melee2 },
	{ ai_charge, 8 },
	{ ai_charge, 14 },
	{ ai_charge, 7 },
	{ ai_charge, 3 },
	{ ai_charge, 6 },
	{ ai_charge, 6, ShamClaw },
	{ ai_charge, 3 },
	{ ai_charge, 8, sham_swingr9 },
};

MMOVE_T(shambler_attack_swingr) = { FRAME_swingr01, FRAME_swingr09, shambler_frames_swingr, shambler_run };

void sham_swingl9(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	ai_charge(self, 8);

	if (brandom() && self->enemy && range_to(self, self->enemy) < MELEE_DISTANCE)
		M_SetAnimation(self, &shambler_attack_swingr);
}

void sham_swingr9(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	ai_charge(self, 1);
	ai_charge(self, 10);

	if (brandom() && self->enemy && range_to(self, self->enemy) < MELEE_DISTANCE)
		M_SetAnimation(self, &shambler_attack_swingl);
}

MONSTERINFO_MELEE(shambler_melee) (edict_t* self) -> void
{
	float  const chance = frandom();
	if (chance > 0.6 || self->health == 600)
		M_SetAnimation(self, &shambler_attack_smash);
	else if (chance > 0.3)
		M_SetAnimation(self, &shambler_attack_swingl);
	else
		M_SetAnimation(self, &shambler_attack_swingr);
}

//
// death
//

void shambler_dead(edict_t* self)
{
	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, -0 };
	monster_dead(self);
}

static void shambler_shrink(edict_t* self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t shambler_frames_death[] = {
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0, shambler_shrink },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 }, // FIXME: thud?
};
MMOVE_T(shambler_move_death) = { FRAME_death01, FRAME_death11, shambler_frames_death, shambler_dead };

DIE(shambler_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	// 1. Always clean up visual effect entities first
	shambler_cleanup_effects(self);

	// 2. Handle boss-specific death logic (if any, before it's fully "dead")
	if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED)
	{
		boss_die(self);
	}

	// 3. Check for gibbing. This can happen to a living monster OR one already in its death animation.
	//    M_CheckGib needs to be able to process the incoming 'damage' for this to work.
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);
		ThrowGibs(self, damage, {
			{ "models/objects/gibs/sm_meat/tris.md2" },
			{ "models/objects/gibs/sm_meat/tris.md2" },
			{ "models/objects/gibs/sm_meat/tris.md2" },
			{ "models/objects/gibs/chest/tris.md2" },
			{ "models/objects/gibs/chest/tris.md2" },
			{ "models/objects/gibs/chest/tris.md2" },
			{ "models/objects/gibs/head2/tris.md2", GIB_HEAD }
		});

		// If it gibs, it's definitely dead.
		// Call shambler_dead to finalize it as a corpse.
		// shambler_dead() calls monster_dead(), which MUST set deadflag = true,
		// takedamage = false, make non-solid, etc.
		shambler_dead(self);
		return; // Gibbed, nothing more to do.
	}

	// 4. If not gibbed by this current hit:
	//    a. If deadflag is already true, it means it was already in a death animation
	//       from a *previous* non-gibbing hit. Let that animation continue.
	//       Since takedamage is true (or was true when this state was entered),
	//       M_CheckGib above would have had a chance to gib it.
	if (self->deadflag)
	{
		return;
	}

	//    b. If deadflag is false, this is the hit that initiates a new, non-gib death.
	//       Start the regular death sequence.
	gi.sound(self, CHAN_VOICE, sound_die, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
	self->deadflag = true;      // Mark as dying.
	
	// CRITICAL CHANGE: Keep takedamage = true so M_CheckGib can work on subsequent hits.
	// Pain reactions will be suppressed by the new check in shambler_pain().
	self->takedamage = true;    

	M_SetAnimation(self, &shambler_move_death); // Play the death animation.
                                                // The animation's last frame calls shambler_dead,
                                                // which calls monster_dead() to finalize the corpse state
                                                // (including setting takedamage = false).
}

void SP_monster_shambler(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

    if (self->monsterinfo.monster_type_id == MONSTER_TYPE_UNKNOWN) { // Check if it hasn't been set yet
        self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER);
    }	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	self->s.modelindex = gi.modelindex("models/monsters/shambler/tris.md2");
	self->mins = { -32, -32, -24 };
	self->maxs = { 32, 32, 64 };
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	gi.modelindex("models/proj/lightning/tris.md2");
	sound_pain.assign("shambler/shurt2.wav");
	sound_idle.assign("shambler/sidle.wav");
	sound_die.assign("shambler/sdeath.wav");
	sound_windup.assign("shambler/sattck1.wav");
	sound_melee1.assign("shambler/melee1.wav");
	sound_melee2.assign("shambler/melee2.wav");
	sound_sight.assign("shambler/ssight.wav");
	sound_smack.assign("shambler/smack.wav");
	sound_boom.assign("shambler/sboom.wav");
	sound_fireball.assign("weapons/rocklx1a.wav");

	if (horde::IsMonsterType(self, horde::MonsterTypeID::SHAMBLER)) {
		self->health = M_SHAMBLER_INITIAL_HEALTH * st.health_multiplier;
		self->gib_health = -190;
	}

	self->mass = 500;

	self->pain = shambler_pain;
	self->die = shambler_die;
	self->monsterinfo.stand = shambler_stand;
	self->monsterinfo.walk = shambler_walk;
	self->monsterinfo.run = shambler_run;
	self->monsterinfo.dodge = nullptr;
	self->monsterinfo.attack = shambler_attack;
	self->monsterinfo.melee = shambler_melee;
	self->monsterinfo.sight = shambler_sight;
	self->monsterinfo.idle = shambler_idle;
	self->monsterinfo.blocked = M_MonsterBlocked;
	self->monsterinfo.setskin = shambler_setskin;

	gi.linkentity(self);

	if (self->spawnflags.has(SPAWNFLAG_SHAMBLER_PRECISE))
		self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	M_SetAnimation(self, &shambler_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start(self);
	ApplyMonsterBonusFlags(self);
}

//HORDE BOSS
void SP_monster_shamblerkl(edict_t* self)
{
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_KL);	SP_monster_shambler(self);
	if (horde::IsMonsterType(self, horde::MonsterTypeID::SHAMBLER_KL)) {
		int base_health = M_SHAMBLER_KL_INITIAL_HEALTH;
		self->health = base_health;
		self->gib_health = -190;
	}
	if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED) {
		self->gib_health = -3500;
	}
	self->yaw_speed = 65;
	//	self->s.renderfx = RF_TRANSLUCENT;
	//	self->s.effects = EF_FLAG1;
	ApplyMonsterBonusFlags(self);
}

void SP_monster_shambler_small(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();
self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_SMALL);	SP_monster_shambler(self);
	if (horde::IsMonsterType(self, horde::MonsterTypeID::SHAMBLER_SMALL)) {
		self->health = M_SHAMBLER_SMALL_INITIAL_HEALTH + st.health_multiplier;
		self->gib_health = -190;
		self->s.scale = 0.6f;
		// Removed manual (mins/maxs)  scaling - monster_start() handles it automatically
	}

	self->yaw_speed = 65;
	ApplyMonsterBonusFlags(self);
}