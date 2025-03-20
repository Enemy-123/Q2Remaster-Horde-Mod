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


// New function for shambler fireball thinking/tracking
THINK(shambler_fireball_think) (edict_t* self) -> void
{
	edict_t* acquire = nullptr;
	float    oldlen = 0;
	float    olddot = 1;

	// Don't acquire a target until a small delay has passed
	// This prevents colliding with the owner at spawn
	if (self->timestamp < level.time || self->oldenemy)
	{
		vec3_t const fwd = AngleVectors(self->s.angles).forward;

		if (self->oldenemy)
		{
			self->enemy = self->oldenemy;
			self->oldenemy = nullptr;
		}

		if (self->enemy)
		{
			acquire = self->enemy;

			if (acquire->health <= 0 || !visible(self, acquire))
			{
				self->enemy = acquire = nullptr;
			}
			else
			{
				float const dist_to_target = (self->s.origin - acquire->s.origin).normalize();
				self->pos1 = ((acquire->s.origin + vec3_t{ 0.f, 0.f, acquire->mins.z }) - self->s.origin).normalized();
			}
		}

		if (!acquire)
		{
			// acquire new target
			edict_t* target = nullptr;

			while ((target = findradius(target, self->s.origin, 1024)) != nullptr)
			{
				// Skip owner
				if (self->owner == target)
					continue;
				if (!target->client)
					continue;
				if (target->health <= 0)
					continue;
				if (!visible(self, target))
					continue;
				// Skip teammates
				if (OnSameTeam(self->owner, target))
					continue;

				float const dist_to_target = (self->s.origin - target->s.origin).normalize();
				vec3_t vec = ((target->s.origin + vec3_t{ 0.f, 0.f, target->mins.z }) - self->s.origin).normalized();

				float const len = vec.length();
				float const dot = vec.dot(fwd);

				// targets that require us to turn less are preferred
				if (dot >= olddot)
					continue;

				if (acquire == nullptr || dot < olddot || len < oldlen)
				{
					acquire = target;
					oldlen = len;
					olddot = dot;
					self->pos1 = vec;
				}
			}
		}
	}

	vec3_t const preferred_dir = self->pos1;

	if (acquire != nullptr)
	{
		if (self->enemy != acquire)
		{
			gi.sound(self, CHAN_WEAPON, sound_fireball, 1.f, 0.25f, 0);
			self->enemy = acquire;
		}
	}
	else
		self->enemy = nullptr;

	float t = self->accel;

	if (self->enemy)
		t *= 0.85f;

	// FIXED: Limit turn rate to prevent too-sharp turns
	float max_turn_rate = 0.15f;
	float turn_amount = fmin(t, max_turn_rate);

	// Smooth turning using slerp function with limited turn rate
	self->movedir = slerp(self->movedir, preferred_dir, turn_amount).normalized();
	self->s.angles = vectoangles(self->movedir);

	if (self->speed < self->yaw_speed)
	{
		self->speed += self->yaw_speed * gi.frame_time_s;
	}

	// FIXED: Add speed limit to prevent excessive velocity
	const float MAX_SPEED = 1000.0f;
	if (self->speed > MAX_SPEED)
		self->speed = MAX_SPEED;

	self->velocity = self->movedir * self->speed;

	// FIXED: Check for potential collisions before next frame
	vec3_t next_pos = self->s.origin + (self->velocity * gi.frame_time_s);
	trace_t trace = gi.traceline(self->s.origin, next_pos, self, MASK_SHOT);
	if (trace.fraction < 1.0f && trace.ent != self->owner)
	{
		// Trigger touch function manually if we're about to hit something
		if (self->touch)
			self->touch(self, trace.ent, trace, false);
		return; // Skip nextthink since we're freeing this entity
	}

	self->nextthink = level.time + FRAME_TIME_MS;

	// Add trail effect for better visibility
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_BLOOD);
	gi.WritePosition(self->s.origin);
	gi.WritePosition(self->s.old_origin);
	gi.multicast(self->s.origin, MULTICAST_PVS, false);
}

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

// Improved fireball spawning - two phases like the Fixbot
void ShamblerCastFireballs(edict_t* self)
{
	if (!self->enemy)
		return;

	vec3_t f, r;
	AngleVectors(self->s.angles, f, r, nullptr);

	const vec3_t left_pos = M_ProjectFlashSource(self, fireball_left_hand[self->s.frame - FRAME_smash01], f, r);
	const vec3_t right_pos = M_ProjectFlashSource(self, fireball_right_hand[self->s.frame - FRAME_smash01], f, r);
	const vec3_t start = (left_pos + right_pos) * 0.5f;

	vec3_t dir;
	vec3_t target;
	const float rocketSpeed = 800;
	const bool blindfire = (self->monsterinfo.aiflags & AI_MANUAL_STEERING) != 0;

	// Target selection logic
	if (blindfire)
	{
		target = self->monsterinfo.blind_fire_target;
		if (!M_AdjustBlindfireTarget(self, start, target, r, dir))
			return;
	}
	else
	{
		// Smart targeting 
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

		// Line of sight check
		trace_t const trace = gi.traceline(start, target, self, MASK_PROJECTILE);
		if (trace.fraction < 0.5f && !blindfire)
			return;
	}

	// Save last known target position for blindfire
	self->monsterinfo.blind_fire_target = target;

	// FIXED: Reduced number of fireballs and spread
	const int num_fireballs = (g_hardcoop->integer || self->monsterinfo.IS_BOSS) ? 3 : 2; // Reduced from 5/3
	const float spread_base = g_hardcoop->integer ? 0.02f : 0.04f; // Reduced spread
	const bool isboss = (self->monsterinfo.IS_BOSS != 0);

	// Central firing position
	vec3_t central_pos = start;

	// Create spread patterns for multiple fireballs
	for (int i = 0; i < num_fireballs; i++)
	{
		vec3_t spread_dir = dir;
		vec3_t spawn_pos = central_pos;

		// Add variety to launch positions based on index
		if (i > 0) {
			float angle = (i * (2 * PIf / num_fireballs));
			float radius = 15.0f; // Reduced radius of spread circle (was 20)

			// Circular pattern around central position
			spawn_pos[0] += cosf(angle) * radius;
			spawn_pos[1] += sinf(angle) * radius;

			// Add directional spread
			float spread = spread_base;
			if (isboss)
				spread *= 0.75f;

			spread_dir[0] += cosf(angle) * spread;
			spread_dir[1] += sinf(angle) * spread;
			spread_dir[2] += (frandom() - 0.5f) * spread;
			spread_dir.normalize();
		}

		edict_t* fireball = G_Spawn();
		if (fireball)
		{
			fireball->s.origin = spawn_pos;
			fireball->s.angles = vectoangles(spread_dir);
			fireball->velocity = spread_dir * rocketSpeed;
			fireball->movetype = MOVETYPE_FLYMISSILE;
			fireball->svflags |= SVF_PROJECTILE;
			fireball->flags |= FL_DODGE;
			fireball->clipmask = MASK_PROJECTILE;
			fireball->solid = SOLID_BBOX;
			fireball->s.effects = EF_FIREBALL;  // Removed EF_TELEPORTER which might cause issues
			fireball->s.renderfx = RF_FULLBRIGHT | RF_GLOW;
			fireball->s.modelindex = gi.modelindex("models/objects/gibs/skull/tris.md2");
			fireball->owner = self;
			fireball->touch = fireball_touch;

			// Setting up tracking behavior
			fireball->think = shambler_fireball_think;
			fireball->nextthink = level.time + 100_ms;

			// Set the initial target if we have one
			if (self->enemy && visible(fireball, self->enemy)) {
				fireball->oldenemy = self->enemy;
				fireball->timestamp = level.time + (isboss ? 0.2_sec : 0.5_sec);
			}

			// Turn rate parameters - FIXED: Reduced turn rates
			float turn_fraction = isboss ? 0.12f : 0.08f; // Reduced from 0.15/0.1
			fireball->accel = turn_fraction;
			fireball->movedir = spread_dir;
			fireball->speed = rocketSpeed;
			fireball->yaw_speed = rocketSpeed; // Reduced from 1.5x

			// Size and damage parameters
			fireball->dmg = irandom(15, 25) * M_DamageModifier(self);
			fireball->radius_dmg = 40 * M_DamageModifier(self);
			fireball->dmg_radius = 100;
			fireball->classname = "shambler_fireball";

			// Scale based on animation
			const float size_factor = static_cast<float>(self->s.frame - FRAME_smash01) /
				static_cast<float>(q_countof(fireball_left_hand));
			fireball->s.scale = 0.5f + (1.0f - 0.5f) * size_factor;

			// Sound effect
			fireball->s.sound = gi.soundindex("weapons/rockfly.wav");

			gi.linkentity(fireball);
		}
	}

	// Main attack sound
	gi.sound(self, CHAN_WEAPON, sound_fireball, 1, ATTN_NORM, 0);

	// Add visual effects at launch point
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_ROCKET_EXPLOSION);
	gi.WritePosition(start);
	gi.multicast(start, MULTICAST_PVS, false);
}
// Enhancement for charging effect - make fireballs visible during charge
void shambler_fireball_update(edict_t* self)
{
	edict_t* fireball_effect_left = self->beam;
	edict_t* fireball_effect_right = self->beam2;

	// Create or update left hand fireball
	if (!fireball_effect_left) {
		fireball_effect_left = G_Spawn();
		self->beam = fireball_effect_left;
		fireball_effect_left->s.effects = EF_FIREBALL;
		fireball_effect_left->s.renderfx = RF_FULLBRIGHT | RF_GLOW;
		fireball_effect_left->movetype = MOVETYPE_NONE;
		fireball_effect_left->solid = SOLID_NOT;
		gi.setmodel(fireball_effect_left, "models/objects/gibs/sm_meat/tris.md2");	}

	// Create or update right hand fireball
	if (!fireball_effect_right) {
		fireball_effect_right = G_Spawn();
		self->beam2 = fireball_effect_right;
		fireball_effect_right->s.effects = EF_FIREBALL;
		fireball_effect_right->s.renderfx = RF_FULLBRIGHT | RF_GLOW;
		fireball_effect_right->movetype = MOVETYPE_NONE;
		fireball_effect_right->solid = SOLID_NOT;
		gi.setmodel(fireball_effect_right, "models/objects/gibs/sm_meat/tris.md2");
	}

	// Clean up if we're past the charging phase
	if (self->s.frame >= FRAME_magic01 + q_countof(lightning_left_hand))
	{
		if (fireball_effect_left) {
			G_FreeEdict(fireball_effect_left);
			self->beam = nullptr;
		}
		if (fireball_effect_right) {
			G_FreeEdict(fireball_effect_right);
			self->beam2 = nullptr;
		}
		return;
	}

	vec3_t f, r;
	AngleVectors(self->s.angles, f, r, nullptr);

	// Calculate positions for both hands
	const vec3_t left_pos = M_ProjectFlashSource(self, fireball_left_hand[self->s.frame - FRAME_smash01], f, r);
	const vec3_t right_pos = M_ProjectFlashSource(self, fireball_right_hand[self->s.frame - FRAME_smash01], f, r);

	// Update fireball effect positions
	fireball_effect_left->s.origin = left_pos;
	fireball_effect_right->s.origin = right_pos;

	// Calculate size based on frame (gradually growing)
	const float size_factor = static_cast<float>(self->s.frame - FRAME_smash01) /
		static_cast<float>(q_countof(fireball_left_hand));
	constexpr float max_size = 1.0f; // Maximum size multiplier
	const float current_size = 0.25f + (max_size - 0.25f) * size_factor;

	// Update size
	fireball_effect_left->s.scale = current_size;
	fireball_effect_right->s.scale = current_size;

	// Add pulsating effect
	const gtime_t current_time = level.time;
	const float pulse = sinf(current_time.seconds<float>() * 10.0f) * 0.2f + 0.8f;

	// Apply pulse to scale
	fireball_effect_left->s.scale *= pulse;
	fireball_effect_right->s.scale *= pulse;

	// Add particle effects for better visibility
	if (frandom() < 0.5f) {
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_WELDING_SPARKS);
		gi.WriteByte(5);  // Particle count
		gi.WritePosition(left_pos);
		gi.WriteDir(vec3_origin);
		gi.WriteByte(0xe0);  // Color
		gi.multicast(left_pos, MULTICAST_PVS, false);

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_WELDING_SPARKS);
		gi.WriteByte(5);
		gi.WritePosition(right_pos);
		gi.WriteDir(vec3_origin);
		gi.WriteByte(0xe0);
		gi.multicast(right_pos, MULTICAST_PVS, false);
	}

	gi.linkentity(fireball_effect_left);
	gi.linkentity(fireball_effect_right);
}

void shambler_windupFire(edict_t* self)
{
	// Sound effect for charging
	gi.sound(self, CHAN_WEAPON, sound_windup, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);

	// Initialize tracking variables
	self->beam = nullptr;  // Left hand fireball
	self->beam2 = nullptr; // Right hand fireball

	// Start the visual fireball charge effect
	shambler_fireball_update(self);

	// Add dramatic light effect
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_TELEPORT_EFFECT);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PVS, false);
}

//
static void shambler_lightning_update(edict_t* self)
{
	edict_t* lightning = self->beam;

	if (!lightning) {
		// Handle error: lightning is nullptr
		return;
	}

	if (self->s.frame >= FRAME_magic01 + q_countof(lightning_left_hand))
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
	if (!self->enemy)
		return;

	vec3_t start;
	vec3_t dir;
	vec3_t forward, right;
	vec3_t const offset = FindShamblerOffset(self);

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, offset, forward, right);

	// calc direction to where we targted
	if (g_hardcoop->integer || current_wave_level >= 22 || self->monsterinfo.IS_BOSS)
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

	fire_bullet(self, start, dir, irandom(8, 12), 15 * M_DamageModifier(self), 0, 0, MOD_TESLA);
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

mframe_t shambler_frames_fireball[] = {
	{ ai_charge, 0, shambler_windupFire },      // First frame - start charge
	{ ai_charge, 0, shambler_fireball_update }, // Charge building up
	{ ai_charge, 0, shambler_fireball_update },
	{ ai_move, 0, shambler_fireball_update },
	{ ai_move, 0, shambler_fireball_update },
	{ ai_move, 0, ShamblerSaveLoc },           // Save target location
	{ ai_move },
	{ ai_charge },
	{ ai_charge },
	{ ai_move, 0, ShamblerCastFireballs },     // First wave of fireballs
	{ ai_move, 0, ShamblerCastFireballs },     // Second wave 
	{ ai_move, 0, ShamblerCastFireballs },     // Third wave
	{ ai_move, 0, ShamblerCastFireballs },     // Fourth wave
};
MMOVE_T(shambler_attack_fireball) = { FRAME_smash01, FRAME_smash12, shambler_frames_fireball, shambler_run };


MONSTERINFO_ATTACK(shambler_attack) (edict_t* self) -> void
{
//	if (!strcmp(self->classname, "monster_shambler_small")) { //fireball got bugged somehow
	M_SetAnimation(self, &shambler_attack_magic);
	return;
}
	//else
	//M_SetAnimation(self,
	//	brandom() ?
	//	&shambler_attack_magic :
	//	&shambler_attack_fireball);
	//return;
//}

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
	if (!self->enemy)
	{
		// char buffer[256];
		// std::snprintf(buffer, sizeof(buffer), "sham_smash10: Error: enemy not properly initialized\n");
		// gi.Com_Print(buffer);
		return;
	}

	ai_charge(self, 0);

	if (!CanDamage(self->enemy, self))
		return;

	vec3_t const aim = { MELEE_DISTANCE, self->mins[0], -4 };
	const bool hit = fire_hit(self, aim, !strcmp(self->classname, "monster_shambler_small") ? 45 : irandom(110, 120), 120); // Slower attack

	if (hit)
		gi.sound(self, CHAN_WEAPON, sound_smack, 1, ATTN_NORM, 0);

	// SpawnMeatSpray(self.origin + v_forward * 16, crandom() * 100 * v_right);
	// SpawnMeatSpray(self.origin + v_forward * 16, crandom() * 100 * v_right);
};

void ShamClaw(edict_t* self)
{
	if (!self->enemy)
	{
		// char buffer[256];
		// std::snprintf(buffer, sizeof(buffer), "ShamClaw: Error: enemy not properly initialized\n");
		// gi.Com_Print(buffer);
		return;
	}

	ai_charge(self, 10);

	if (!CanDamage(self->enemy, self))
		return;

	const vec3_t aim = { MELEE_DISTANCE, self->mins[0], -4 };
	const bool hit = fire_hit(self, aim, !strcmp(self->classname, "monster_shambler_small") ? 30 : irandom(70, 80), 80); // Slower attack

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
};

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
	ai_charge(self, 8);

	if (brandom() && self->enemy && range_to(self, self->enemy) < MELEE_DISTANCE)
		M_SetAnimation(self, &shambler_attack_swingr);
}

void sham_swingr9(edict_t* self)
{
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
	if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED)
		boss_die(self);

	//OnEntityDeath(self);
	if (self->beam)
	{
		G_FreeEdict(self->beam);
		self->beam = nullptr;
	}

	if (self->beam2)
	{
		G_FreeEdict(self->beam2);
		self->beam2 = nullptr;
	}

	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);
		// FIXME: better gibs for shambler, shambler head
		ThrowGibs(self, damage, {
			{ "models/objects/gibs/sm_meat/tris.md2" },
			{ "models/objects/gibs/sm_meat/tris.md2" },
			{ "models/objects/gibs/sm_meat/tris.md2" },
			{ "models/objects/gibs/chest/tris.md2" },
			{ "models/objects/gibs/chest/tris.md2" },
			{ "models/objects/gibs/chest/tris.md2" },
			{ "models/objects/gibs/head2/tris.md2", GIB_HEAD }
			});
		self->deadflag = true;
		return;
	}

	if (self->deadflag)
		return;

	// regular death
	gi.sound(self, CHAN_VOICE, sound_die, 1, self->monsterinfo.IS_BOSS ? ATTN_NONE : ATTN_NORM, 0);
	self->deadflag = true;
	self->takedamage = true;

	M_SetAnimation(self, &shambler_move_death);
}

void SP_monster_shambler(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (!M_AllowSpawn(self)) {
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

	if (!strcmp(self->classname, "monster_shambler")) {
		self->health = 650 * st.health_multiplier;
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
	self->monsterinfo.blocked = nullptr;
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
	SP_monster_shambler(self);
	if (!strcmp(self->classname, "monster_shamblerkl")) {
		self->health = 6500 + (1.08 * current_wave_level);
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

	SP_monster_shambler(self);
	if (!strcmp(self->classname, "monster_shambler_small")) {
		self->health = 350 + st.health_multiplier;
		self->gib_health = -190;
		self->s.scale = 0.6f;
		self->mins *= 0.6f;
		self->maxs *= 0.6f;
	}

		self->yaw_speed = 65;
	ApplyMonsterBonusFlags(self);
}