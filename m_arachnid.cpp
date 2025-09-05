// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

ARACHNID UNIFIED IMPLEMENTATION

This file contains all arachnid variants:
- Standard arachnid (railgun)
- Arachnid2 (stronger railgun)
- PSX arachnid (smarter AI, can spawn minions)
- Spider (skinned variant of standard arachnid)
- GM arachnid (rocket variant)

==============================================================================
*/

#include "g_local.h"
#include "m_arachnid.h"
#include "m_flash.h"
#include "shared.h"

// Common spawn flags
constexpr spawnflags_t SPAWNFLAG_SPIDER = 8_spawnflag;

// Shared cached sounds for all arachnid types
static cached_soundindex sound_pain;
static cached_soundindex sound_death;
static cached_soundindex sound_sight;
static cached_soundindex sound_step;
static cached_soundindex sound_charge;
static cached_soundindex sound_melee;
static cached_soundindex sound_melee_hit;

// PSX specific sounds
static cached_soundindex sound_spawn;
static cached_soundindex sound_pissed;
// Plasma spider sounds
static cached_soundindex sound_plasma;
static cached_soundindex sound_plasma_hit;

// PSX reinforcement constants
constexpr std::array<reinforcement_def_t, 1> psx_boss_reinforcements_defs = { {
    {horde::MonsterTypeID::TANK_SPAWNER, 2}
} };

// NEW: Compile-time reinforcement definitions for the PSX Arachnid in coop.
constexpr std::array<reinforcement_def_t, 1> psx_coop_reinforcements_defs = { {
    {horde::MonsterTypeID::STALKER, 2}
} };
constexpr int32_t default_monster_slots_base = 5;

// Forward declarations for spider jump/ceiling logic
bool spider_ok_to_transition(edict_t* self);
void spider_jump_transition(edict_t* self);
void spider_jump_wait_land(edict_t* self);
bool spider_do_pounce(edict_t* self, const vec3_t& dest);
void spider_jump_straightup(edict_t* self);
void spider_jump(edict_t* self, blocked_jump_result_t result);
void spider_dodge_jump(edict_t* self);
void spider_run(edict_t* self); // Need forward declaration for animation callbacks
// Forward declarations for base arachnid functions needed by spider
void arachnid_stand(edict_t* self);
void arachnid_run(edict_t* self);

//// Forward declarations for spider dodge/blocked
//MONSTERINFO_BLOCKED(spider_blocked) (edict_t* self, float dist) -> bool;
//MONSTERINFO_DODGE(spider_dodge) (edict_t* self, edict_t* attacker, gtime_t eta, trace_t* tr, bool gravity) -> void;

void spider_stand(edict_t* self); // Need forward declaration for animation callbacks

// Helper macro for ceiling check
inline bool SPIDER_ON_CEILING(edict_t* ent)
{
	return (ent->gravityVector[2] > 0);
}

//==========================================================================================
// SHARED ANIMATIONS AND FUNCTIONS
//==========================================================================================

// Common stand animation
mframe_t arachnid_frames_stand[] = {
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
MMOVE_T(arachnid_move_stand) = { FRAME_idle1, FRAME_idle13, arachnid_frames_stand, nullptr };

// Common pain animations
mframe_t arachnid_frames_pain1[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};

mframe_t arachnid_frames_pain2[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};

// Common death animation
mframe_t arachnid_frames_death1[] = {
    { ai_move, 0 },
    { ai_move, -1.23f },
    { ai_move, -1.23f },
    { ai_move, -1.23f },
    { ai_move, -1.23f },
    { ai_move, -1.64f },
    { ai_move, -1.64f },
    { ai_move, -2.45f },
    { ai_move, -8.63f },
    { ai_move, -4.0f },
    { ai_move, -4.5f },
    { ai_move, -6.8f },
    { ai_move, -8.0f },
    { ai_move, -5.4f },
    { ai_move, -3.4f },
    { ai_move, -1.9f },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};

// Shared utility functions
void arachnid_footstep(edict_t* self)
{
    gi.sound(self, CHAN_BODY, sound_step, 0.5f, ATTN_IDLE, 0.0f);
}

void arachnid_melee_charge(edict_t* self)
{
    gi.sound(self, CHAN_WEAPON, sound_melee, 1.f, ATTN_NORM, 0.f);
}

void arachnid_charge_rail(edict_t* self)
{
    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    gi.sound(self, CHAN_WEAPON, sound_charge, 1.f, ATTN_NORM, 0.f);
    self->pos1 = self->enemy->s.origin;
    self->pos1[2] += self->enemy->viewheight;
}

void arachnid_dead(edict_t* self)
{
    self->mins = { -16, -16, -24 };
    self->maxs = { 16, 16, -8 };
  	monster_dead(self);
}

void arachnid_die_internal(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod, const mmove_t* death_move)
{
    // check for gib
    if (M_CheckGib(self, mod))
    {
        gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

        ThrowGibs(self, damage, {
            { 2, "models/objects/gibs/bone/tris.md2" },
            { 2, "models/objects/gibs/sm_meat/tris.md2" },
            { "models/monsters/gunner/gibs/chest.md2", GIB_METALLIC },
            { "models/monsters/gunner/gibs/garm.md2", GIB_METALLIC | GIB_UPRIGHT },
            { "models/monsters/gladiatr/gibs/rarm.md2", GIB_METALLIC | GIB_UPRIGHT },
            { "models/monsters/gunner/gibs/foot.md2", GIB_METALLIC },
            { "models/monsters/gunner/gibs/head.md2", GIB_METALLIC | GIB_HEAD }
            });
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    // regular death
    gi.sound(self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
    self->deadflag = true;
    self->takedamage = true;

    // Clear AI manual steering flag if it's set (mainly for PSX arachnid)
    self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

    // Ensure spider falls correctly if on ceiling
    if (self->spawnflags.has(SPAWNFLAG_SPIDER)) {
        self->movetype = MOVETYPE_TOSS; // Make sure it falls
        self->s.angles[2] = 0;
        self->gravityVector = { 0, 0, -1 };
    }

    M_SetAnimation(self, death_move);

    if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED) {
        BossDeathHandler(self);
    }
}


//==========================================================================================
// SPIDER JUMPING AND CEILING LOGIC (Adapted from Stalker)
//==========================================================================================

// Placeholder jump animations using run frames
mframe_t spider_frames_jump[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
MMOVE_T(spider_move_jump_up) = { FRAME_walk1, FRAME_walk7, spider_frames_jump, spider_run }; // Placeholder frames
MMOVE_T(spider_move_jump_down) = { FRAME_walk1, FRAME_walk7, spider_frames_jump, spider_run }; // Placeholder frames
MMOVE_T(spider_move_jump_transition) = { FRAME_walk1, FRAME_walk7, spider_frames_jump, spider_run }; // Placeholder frames

// --- spider_ok_to_transition ---
bool spider_ok_to_transition(edict_t* self)
{
	trace_t trace;
	vec3_t	pt, start;
	float	max_dist;
	float	margin;
	float	end_height;

	if (SPIDER_ON_CEILING(self))
	{
		// if we get knocked off the ceiling, always fall downwards
		if (!self->groundentity)
			return true;

		max_dist = -384; // How far down to check for floor
		margin = self->mins[2] - 8;
	}
	else
	{
		max_dist = 384; // How far up to check for ceiling (Increased from 180)
		margin = self->maxs[2] + 4; // Margin adjustment for ceiling check (Added +4 back)
	}

	pt = self->s.origin;
	pt[2] += max_dist;
	trace = gi.trace(self->s.origin, self->mins, self->maxs, pt, self, MASK_MONSTERSOLID);

	if (trace.fraction == 1.0f ||
		!(trace.contents & CONTENTS_SOLID) ||
		(trace.ent != world))
	{
		return false; // No surface found in range
	}

    // Check surface normal
	if (SPIDER_ON_CEILING(self))
	{
		if (trace.plane.normal[2] < 0.9f) // Must be reasonably flat floor
			return false;
	}
	else
	{
		if (trace.plane.normal[2] > -0.9f) // Must be reasonably flat ceiling
			return false;
	}

	end_height = trace.endpos[2];

	// Check the four corners at the transition height to ensure enough space
	pt[0] = self->absmin[0];
	pt[1] = self->absmin[1];
	pt[2] = trace.endpos[2] + margin;
	start = pt;
	start[2] = self->s.origin[2];
	trace = gi.traceline(start, pt, self, MASK_MONSTERSOLID);
	if (trace.fraction == 1.0f || !(trace.contents & CONTENTS_SOLID) || (trace.ent != world))
		return false;
	if (fabsf(end_height + margin - trace.endpos[2]) > 8)
		return false;

	pt[0] = self->absmax[0];
	pt[1] = self->absmin[1];
	start = pt;
	start[2] = self->s.origin[2];
	trace = gi.traceline(start, pt, self, MASK_MONSTERSOLID);
	if (trace.fraction == 1.0f || !(trace.contents & CONTENTS_SOLID) || (trace.ent != world))
		return false;
	if (fabsf(end_height + margin - trace.endpos[2]) > 8)
		return false;

	pt[0] = self->absmax[0];
	pt[1] = self->absmax[1];
	start = pt;
	start[2] = self->s.origin[2];
	trace = gi.traceline(start, pt, self, MASK_MONSTERSOLID);
	if (trace.fraction == 1.0f || !(trace.contents & CONTENTS_SOLID) || (trace.ent != world))
		return false;
	if (fabsf(end_height + margin - trace.endpos[2]) > 8)
		return false;

	pt[0] = self->absmin[0];
	pt[1] = self->absmax[1];
	start = pt;
	start[2] = self->s.origin[2];
	trace = gi.traceline(start, pt, self, MASK_MONSTERSOLID);
	if (trace.fraction == 1.0f || !(trace.contents & CONTENTS_SOLID) || (trace.ent != world))
		return false;
	if (fabsf(end_height + margin - trace.endpos[2]) > 8)
		return false;

	return true;
}

// --- spider_jump_transition ---
void spider_jump_transition(edict_t* self)
{
	if (self->deadflag)
		return;

	if (SPIDER_ON_CEILING(self))
	{
		if (spider_ok_to_transition(self))
		{
			self->gravityVector[2] = -1;
			self->s.angles[2] += 180.0f;
			if (self->s.angles[2] > 360.0f)
				self->s.angles[2] -= 360.0f;
			self->groundentity = nullptr;
		}
	}
	else if (self->groundentity) // make sure we're standing on SOMETHING...
	{
		self->velocity[0] += crandom() * 5;
		self->velocity[1] += crandom() * 5;
		self->velocity[2] += -400 * self->gravityVector[2]; // Jump upwards relative to current gravity
		if (spider_ok_to_transition(self))
		{
			self->gravityVector[2] = 1;
			self->s.angles[2] = 180.0;
			self->groundentity = nullptr;
		}
	}
}

// --- spider_jump_wait_land ---
void spider_jump_wait_land(edict_t* self)
{
	// Optional: Add chance to shoot while in air?
	/*
	if ((frandom() < 0.7f) && (level.time >= self->monsterinfo.attack_finished))
	{
		self->monsterinfo.attack_finished = level.time + 300_ms;
		spider_plasma(self); // Or appropriate attack
	}
	*/

	if (self->groundentity == nullptr)
	{
		self->gravity = 1.3f; // Slightly increased gravity during jump
		self->monsterinfo.nextframe = self->s.frame;

		if (monster_jump_finished(self))
		{
			self->gravity = 1;
			self->monsterinfo.nextframe = self->s.frame + 1;
		}
	}
	else // Landed
	{
		self->gravity = 1;
		self->monsterinfo.nextframe = self->s.frame + 1;

		// THE PROBLEMATIC BLOCK HAS BEEN REMOVED.
		// The physics engine will handle correct placement on the ground/ceiling.
	}
}

// --- spider_jump_up / spider_jump_down (for ledges) ---
void spider_jump_up(edict_t* self)
{
	vec3_t forward, up;
	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 200);
	self->velocity += (up * 450);
}

void spider_jump_down(edict_t* self)
{
	vec3_t forward, up;
	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 100);
	self->velocity += (up * 300);
}

// --- spider_jump (ledge jump animation trigger) ---
void spider_jump(edict_t* self, blocked_jump_result_t result)
{
	if (!self->enemy)
		return;

	// Use placeholder animations
	if (result == blocked_jump_result_t::JUMP_JUMP_UP)
	{
		// Need specific frames/functions for the jump action itself
		spider_jump_up(self);
		M_SetAnimation(self, &spider_move_jump_up);
	}
	else
	{
		spider_jump_down(self);
		M_SetAnimation(self, &spider_move_jump_down);
	}
}
// --- spider_do_pounce ---
bool spider_check_lz(edict_t* self, edict_t* target, const vec3_t& dest)
{
	if (!target || !target->inuse)
		return false;
	if ((gi.pointcontents(dest) & MASK_WATER) || (target->waterlevel))
		return false;
	if (!target->groundentity)
		return false;

	// Basic check under target center
	vec3_t jumpLZ = dest;
	jumpLZ[2] -= 1; // Check slightly below destination
	if (!(gi.pointcontents(jumpLZ) & MASK_SOLID))
		return false;

	return true;
}

bool spider_do_pounce(edict_t* self, const vec3_t& dest)
{
    if (!M_HasValidTarget(self))
    {
        return false; // Stop immediately if the target is invalid.
    }

    vec3_t	dist;
    float	length;
    vec3_t	jumpAngles;
    vec3_t	jumpLZ;
    float	velocity = 400.1f;

    if (SPIDER_ON_CEILING(self))
        return false; // No pouncing from ceiling

    if (!spider_check_lz(self, self->enemy, dest))
        return false;

    dist = dest - self->s.origin;

    jumpAngles = vectoangles(dist);
    if (fabsf(jumpAngles[YAW] - self->s.angles[YAW]) > 45)
        return false; // Not facing target enough

    if (std::isnan(jumpAngles[YAW]))
        return false;

    self->ideal_yaw = jumpAngles[YAW];
    M_ChangeYaw(self);

    length = dist.length();
    if (length > 450)
        return false; // Too far

    jumpLZ = dest;
    vec3_t dir = dist.normalized();

    // Find valid trajectory
    while (velocity <= 800)
    {
        if (M_CalculatePitchToFire(self, jumpLZ, self->s.origin, dir, velocity, 3, false, true))
            break;
        velocity += 200;
    }

    if (velocity > 800)
        return false; // No valid trajectory found

    self->velocity = dir * velocity;
    // Set a jump animation (placeholder)
    M_SetAnimation(self, &spider_move_jump_up);
    return true;
}

// --- spider_jump_straightup (for ceiling transition / dodge) ---
void spider_jump_straightup(edict_t* self)
{
}

// --- spider_blocked ---
MONSTERINFO_BLOCKED(spider_blocked) (edict_t* self, float dist) -> bool
{
	if (!has_valid_enemy(self))
		return false;

	bool onCeiling = SPIDER_ON_CEILING(self);

	if (!onCeiling)
	{
		// Check for ledge jumps first
		if (auto result = blocked_checkjump(self, dist); result != blocked_jump_result_t::NO_JUMP)
		{
			if (result != blocked_jump_result_t::JUMP_TURN)
				spider_jump(self, result); // Use our spider jump function
			return true;
		}

		// Check for platform drops (less relevant for spider?)
		// if (blocked_checkplat(self, dist))
		// 	 return true;

		// Chance to pounce if blocked and enemy visible
		if (visible(self, self->enemy) && frandom() < 0.1f)
		{
			if (spider_do_pounce(self, self->enemy->s.origin))
				return true;
		}

        // MODIFICATION: Increased the chance to transition to the ceiling when blocked on the floor from 30% to 60%.
        if (frandom() < 0.6f && spider_ok_to_transition(self))
        {
            spider_jump_transition(self);
            return true;
        }
	}
	else // Currently on ceiling
	{
		// MODIFICATION: If blocked on the ceiling, give it only a 25% chance to jump down.
		// This makes it more likely to stay on the ceiling and try to find another path,
		// effectively making it stay on the ceiling for longer periods.
		if (frandom() < 0.25f && spider_ok_to_transition(self))
		{
			spider_jump_transition(self);
			return true;
		}
	}

	// Default blocked behavior (turn away)
	return false;
}

// --- spider_dodge ---
MONSTERINFO_DODGE(spider_dodge) (edict_t* self, edict_t* attacker, gtime_t eta, trace_t* tr, bool gravity) -> void
{
	if (!self->groundentity || self->health <= 0)
		return;

	if (!self->enemy)
	{
		self->enemy = attacker;
		FoundTarget(self);
		return;
	}

	// Don't dodge if projectile impact is too soon or too far away
	if ((eta < FRAME_TIME_MS) || (eta > 5_sec))
		return;

    // Cooldown for dodging
	if (self->timestamp > level.time)
		return;
	self->timestamp = level.time + random_time(1_sec, 3_sec); // Shorter cooldown than stalker?

	// Perform the dodge jump (which handles ceiling transition)
	spider_dodge_jump(self);
}


// --- spider_dodge_jump ---
void spider_dodge_jump(edict_t* self)
{
	vec3_t right;
	float side_speed = 280.0f; // Adjust speed as needed

	vec3_t up, forward; // Added forward vector declaration
	float upward_speed = 250.0f; // Adjust upward speed as needed

	// Get the forward, right and up vectors based on current facing angle and gravity
	AngleVectors(self->s.angles, forward, right, up); // Pass 'forward' instead of nullptr

	// Clear existing vertical velocity to ensure consistent jump height
	self->velocity[2] = 0;

	// Add upward velocity (relative to gravity)
	self->velocity -= (up * upward_speed * self->gravityVector[2]); // Use gravityVector for direction

	// Randomly choose left or right strafe and add horizontal velocity
	if (frandom() < 0.5f)
		self->velocity += (right * side_speed); // Strafe right
	else
		self->velocity -= (right * side_speed); // Strafe left

	// Ensure the spider leaves the ground
	self->groundentity = nullptr;

	// Use placeholder jump animation (maybe a dedicated one later?)
	M_SetAnimation(self, &spider_move_jump_up); // Using jump_up animation
}

// --- spider_physics_change ---
MONSTERINFO_PHYSCHANGED(spider_physics_change) (edict_t* self) -> void
{
	if (SPIDER_ON_CEILING(self) && !self->groundentity)
	{
		self->gravityVector[2] = -1;
		self->s.angles[2] += 180.0f;
		if (self->s.angles[2] > 360.0f)
			self->s.angles[2] -= 360.0f;
	}
}

// --- spider_stand / spider_run (needed for animation callbacks) ---
MONSTERINFO_STAND(spider_stand) (edict_t* self) -> void
{
    if (SPIDER_ON_CEILING(self)) {
        self->viewheight = -16;
    } else {
        self->viewheight = 24;
    }
    // Redirect to the standard arachnid stand
    arachnid_stand(self);
}

MONSTERINFO_RUN(spider_run) (edict_t* self) -> void
{
    if (SPIDER_ON_CEILING(self)) {
        self->viewheight = -16;
    } else {
        self->viewheight = 24;
    }
    
    // Redirect to the standard arachnid run
    arachnid_run(self);
}
//==========================================================================================
// STANDARD ARACHNID IMPLEMENTATION 
//==========================================================================================

MONSTERINFO_SIGHT(arachnid_sight) (edict_t* self, edict_t* other) -> void
{
    gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_STAND(arachnid_stand) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid_move_stand);
}

// Walk animation
mframe_t arachnid_frames_walk[] = {
    { ai_walk, 8, arachnid_footstep },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8, arachnid_footstep },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 }
};
MMOVE_T(arachnid_move_walk) = { FRAME_walk1, FRAME_walk10, arachnid_frames_walk, nullptr };

MONSTERINFO_WALK(arachnid_walk) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid_move_walk);
}

// Run animation
mframe_t arachnid_frames_run[] = {
    { ai_run, 13, arachnid_footstep },
    { ai_run, 13 },
    { ai_run, 13 },
    { ai_run, 13 },
    { ai_run, 13 },
    { ai_run, 13, arachnid_footstep },
    { ai_run, 13 },
    { ai_run, 13 },
    { ai_run, 13 },
    { ai_run, 13 }
};
MMOVE_T(arachnid_move_run) = { FRAME_walk1, FRAME_walk10, arachnid_frames_run, nullptr };

MONSTERINFO_RUN(arachnid_run) (edict_t* self) -> void
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
    {
        M_SetAnimation(self, &arachnid_move_stand);
        return;
    }

    M_SetAnimation(self, &arachnid_move_run);
}

// Pain move animations
MMOVE_T(arachnid_move_pain1) = { FRAME_pain11, FRAME_pain15, arachnid_frames_pain1, arachnid_run };
MMOVE_T(arachnid_move_pain2) = { FRAME_pain21, FRAME_pain26, arachnid_frames_pain2, arachnid_run };

PAIN(arachnid_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + 3_sec;
    gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    float const r = frandom();

    if (r < 0.5f)
        M_SetAnimation(self, &arachnid_move_pain1);
    else
        M_SetAnimation(self, &arachnid_move_pain2);
}

// Rail gun firing
void arachnid_rail(edict_t* self)
{

    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    vec3_t start;
    vec3_t dir;
    vec3_t forward, right;
    monster_muzzleflash_id_t id;

    switch (self->s.frame)
    {
    case FRAME_rails3:
    default:
        id = MZ2_ARACHNID_RAIL1;
        break;
    case FRAME_rails7:
        id = MZ2_ARACHNID_RAIL2;
        break;
    case FRAME_rails_up2:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up5:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    case FRAME_rails_up9:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up11:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    }

    AngleVectors(self->s.angles, forward, right, nullptr);
    start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);

    // calc direction to where we targeted
    dir = self->pos1 - start;
    dir.normalize();

    monster_fire_railgun(self, start, dir, self->monsterinfo.IS_BOSS ? 40 : 35, 100, id);
}

// Attack animations
mframe_t arachnid_frames_attack1[] = {
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge, 0, arachnid_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge, 0, arachnid_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge }
};
MMOVE_T(arachnid_attack1) = { FRAME_rails2, FRAME_rails11, arachnid_frames_attack1, arachnid_run };

mframe_t arachnid_frames_attack_up1[] = {
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge, 0, arachnid_rail },
    { ai_charge },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge, 0, arachnid_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge, 0, arachnid_rail },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(arachnid_attack_up1) = { FRAME_rails_up1, FRAME_rails_up13, arachnid_frames_attack_up1, arachnid_run };

// Melee attack
void arachnid_melee_hit(edict_t* self)
{
    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    if (self->enemy) {
        if (!fire_hit(self, { MELEE_DISTANCE, 0, 0 }, 15, 50))
            self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
    }
    else {
        self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
    }
}

mframe_t arachnid_frames_melee[] = {
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge, 0, arachnid_melee_hit },
    { ai_charge, 0, arachnid_melee_hit },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge, 0, arachnid_melee_hit },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_hit },
    { ai_charge }
};
MMOVE_T(arachnid_melee) = { FRAME_melee_atk1, FRAME_melee_atk12, arachnid_frames_melee, arachnid_run };

void spider_charge_plasma(edict_t* self)
{
    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    if (!self->enemy || !self->enemy->inuse)
        return;

    // Play plasma charge sound instead of railgun sound
    gi.sound(self, CHAN_WEAPON, sound_plasma, 1.f, ATTN_NORM, 0.f);

}

// Improved plasma firing with better leading
void spider_plasma(edict_t* self)
{

    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    vec3_t start;
    vec3_t dir;
    vec3_t forward, right, up; // Added 'up' vector
    monster_muzzleflash_id_t id;

    // Choose appropriate muzzle flash based on the animation frame
    switch (self->s.frame)
    {
    case FRAME_rails4:
    default:
        id = MZ2_ARACHNID_RAIL1;
        break;
    case FRAME_rails8:
        id = MZ2_ARACHNID_RAIL2;
        break;
    case FRAME_rails_up7:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up11:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    }


    float constexpr plasma_speed = 900;
   
    AngleVectors(self->s.angles, forward, right, up); // Get vectors first
    start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);
    // Adjust origin slightly forward and upward (adjust as needed)
    start += (forward * 5);
    start += (up * 8);
   
    // Use PredictAim to lead the target based on plasma projectile speed, using the calculated start position
    PredictAim(self, self->enemy, start, plasma_speed, true, 0.0f, nullptr, &self->pos1);

    // calc direction to where we targeted (predicted position)
    dir = self->pos1 - start;
    dir.normalize();

    int damage = 30;
    int radius_damage = 60;

    // Play proper plasma fire sound
    gi.sound(self, CHAN_WEAPON, sound_plasma, 1.f, ATTN_NORM, 0.f);

    // Fire plasma shot
    fire_plasma(self, start, dir, damage, 900, radius_damage, radius_damage);

    // Chance for enhanced shot at higher difficulties
    if (skill->integer >= 2 && frandom() < 0.35f) {
        fire_plasma(self, start, dir, damage * 0.7f, 1100, radius_damage * 0.7f, radius_damage * 0.7f);
    }
}

// Redefined attack frames with plasma shots at appropriate moments
mframe_t spider_frames_attack1[] = {
    { ai_charge, 0, spider_charge_plasma },  // New plasma charge function
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, spider_plasma },         // First plasma shot
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, spider_plasma },         // Second plasma shot
    { ai_charge }
};
MMOVE_T(spider_attack1) = { FRAME_rails1, FRAME_rails9, spider_frames_attack1, arachnid_run };

// Upward attack animations
mframe_t spider_frames_attack_up1[] = {
    { ai_charge, 0, spider_charge_plasma },  // New plasma charge function
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, spider_plasma },         // First plasma shot
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, spider_plasma },         // Second plasma shot
    { ai_charge },
    { ai_charge }
};
MMOVE_T(spider_attack_up1) = { FRAME_rails_up1, FRAME_rails_up13, spider_frames_attack_up1, arachnid_run };
// Attack decision

MONSTERINFO_ATTACK(spider_attack) (edict_t* self) -> void
{
    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    float dist = range_to(self, self->enemy);

    // Melee check first
    if (self->monsterinfo.melee_debounce_time < level.time && dist < MELEE_DISTANCE) {
        M_SetAnimation(self, &arachnid_melee);
        return;
    }

    // MODIFICATION: Increased chance to attempt a jump during combat from 20% to 40%.
    if (self->groundentity && frandom() < 0.4f)
    {
        if (!SPIDER_ON_CEILING(self) && dist > 100 && dist < 400 && frandom() < 0.5f) // Pounce condition
        {
            if (spider_do_pounce(self, self->enemy->s.origin))
                return; // Pounce initiated
        }
        else // Otherwise, try a transition jump
        {
             if (spider_ok_to_transition(self)) {
                 // MODIFICATION: Fixed a bug. It was calling spider_dodge_jump, which is for strafing.
                 // The correct function to flip to/from the ceiling is spider_jump_transition.
                 spider_jump_transition(self);
                 return; // Jump initiated
             }
        }
    }

    // Standard ranged attack logic
    if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f)
        M_SetAnimation(self, &spider_attack_up1);
    else
        M_SetAnimation(self, &spider_attack1);
}


MONSTERINFO_ATTACK(arachnid_attack) (edict_t* self) -> void
{
    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    if (self->monsterinfo.melee_debounce_time < level.time && range_to(self, self->enemy) < MELEE_DISTANCE)
        M_SetAnimation(self, &arachnid_melee);
    else if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f)
        M_SetAnimation(self, &arachnid_attack_up1);
    else
        M_SetAnimation(self, &arachnid_attack1);
}

// Death animation
MMOVE_T(arachnid_move_death) = { FRAME_death1, FRAME_death20, arachnid_frames_death1, arachnid_dead };

DIE(arachnid_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
    arachnid_die_internal(self, inflictor, attacker, damage, point, mod, &arachnid_move_death);
}

//==========================================================================================
// ARACHNID2 IMPLEMENTATION
//==========================================================================================

MONSTERINFO_SIGHT(arachnid2_sight) (edict_t* self, edict_t* other) -> void
{
    gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_STAND(arachnid2_stand) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid_move_stand);
}

// Walk animation
mframe_t arachnid2_frames_walk[] = {
    { ai_walk, 8, arachnid_footstep },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8, arachnid_footstep },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 }
};
MMOVE_T(arachnid2_move_walk) = { FRAME_walk1, FRAME_walk10, arachnid2_frames_walk, nullptr };

MONSTERINFO_WALK(arachnid2_walk) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid2_move_walk);
}

// Run animation
mframe_t arachnid2_frames_run[] = {
    { ai_run, 8, arachnid_footstep },
    { ai_run, 8 },
    { ai_run, 8 },
    { ai_run, 8 },
    { ai_run, 8 },
    { ai_run, 8, arachnid_footstep },
    { ai_run, 8 },
    { ai_run, 8 },
    { ai_run, 8 },
    { ai_run, 8 }
};
MMOVE_T(arachnid2_move_run) = { FRAME_walk1, FRAME_walk10, arachnid2_frames_run, nullptr };

MONSTERINFO_RUN(arachnid2_run) (edict_t* self) -> void
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
    {
        M_SetAnimation(self, &arachnid_move_stand);
        return;
    }

    M_SetAnimation(self, &arachnid2_move_run);
}

// Pain animations
MMOVE_T(arachnid2_move_pain1) = { FRAME_pain11, FRAME_pain15, arachnid_frames_pain1, arachnid2_run };
MMOVE_T(arachnid2_move_pain2) = { FRAME_pain21, FRAME_pain26, arachnid_frames_pain2, arachnid2_run };

PAIN(arachnid2_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + 3_sec;
    gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    float const r = frandom();

    if (r < 0.5f)
        M_SetAnimation(self, &arachnid2_move_pain1);
    else
        M_SetAnimation(self, &arachnid2_move_pain2);
}

// Rail gun for arachnid2
void arachnid2_rail(edict_t* self)
{

    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    vec3_t start;
    vec3_t dir;
    vec3_t forward, right;
    monster_muzzleflash_id_t id;

    switch (self->s.frame)
    {
    case FRAME_rails6:
    default:
        id = MZ2_ARACHNID_RAIL1;
        break;
    case FRAME_rails10:
        id = MZ2_ARACHNID_RAIL2;
        break;
    case FRAME_rails_up7:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up11:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    }

    AngleVectors(self->s.angles, forward, right, nullptr);
    start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);

    // calc direction to where we targeted
    dir = self->pos1 - start;
    dir.normalize();

    monster_fire_railgun(self, start, dir, 50, 100, id);
}

// Attack animations for arachnid2
mframe_t arachnid2_frames_attack1[] = {
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_rail },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_rail },
    { ai_charge }
};
MMOVE_T(arachnid2_attack1) = { FRAME_rails3, FRAME_rails11, arachnid2_frames_attack1, arachnid2_run };

mframe_t arachnid2_frames_attack_up1[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_rail },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
};
MMOVE_T(arachnid2_attack_up1) = { FRAME_rails_up1, FRAME_rails_up16, arachnid2_frames_attack_up1, arachnid2_run };

// Melee attack for arachnid2
void arachnid2_melee_hit(edict_t* self)
{
    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    if (self->style == 1)
    {
        if (!fire_hit(self, { MELEE_DISTANCE, 0, 0 }, 30, 50))
            self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
    }
    else
    {
        if (!fire_hit(self, { MELEE_DISTANCE, 0, 0 }, 20, 50))
            self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
    }
}

mframe_t arachnid2_frames_melee[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_melee_hit },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_melee_hit },
    { ai_charge }
};
MMOVE_T(arachnid2_melee) = { FRAME_melee_atk1, FRAME_melee_atk12, arachnid2_frames_melee, arachnid2_run };

// GM Arachnid (Rocket variant) functionality
static void gm_arachnid_blind_check(edict_t* self)
{
    if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
    {
        vec3_t const aim = self->monsterinfo.blind_fire_target - self->s.origin;
        self->ideal_yaw = vectoyaw(aim);
    }
}

void gm_arachnid_rockets(edict_t* self)
{
    vec3_t                      start;
    vec3_t                      dir;
    vec3_t                      forward, right;
    vec3_t                      vec;
    monster_muzzleflash_id_t    id;
    int                         rocketSpeed;
    vec3_t                      target;
    bool                        const blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;

    // FIX: Add a guard clause. If we are not blind-firing, we MUST have a valid enemy.
    // If the enemy is gone, abort the rocket launch for this frame.
    if (!blindfire && !M_HasValidTarget(self))
    {
        return;
    }

    switch (self->s.frame)
    {
    case FRAME_rails4:
    default:
        id = MZ2_ARACHNID_RAIL1;
        break;
    case FRAME_rails8:
        id = MZ2_ARACHNID_RAIL2;
        break;
    case FRAME_rails_up4:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up6:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    case FRAME_rails_up10:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up12:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    }

    AngleVectors(self->s.angles, forward, right, nullptr);
    start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);

    rocketSpeed = 800;

    if (blindfire)
        target = self->monsterinfo.blind_fire_target;
    else
        target = self->enemy->s.origin;

    if (blindfire)
    {
        vec = target;
        dir = vec - start;
    }
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

    if ((!blindfire) && ((frandom() < (0.2f + ((3 - skill->integer) * 0.15f)))))
        PredictAim(self, self->enemy, start, rocketSpeed, false, 0, &dir, &vec);

    dir.normalize();

    trace_t trace{}; // Check target
    trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
    if (blindfire)
    {
        // blindfire has different fail criteria for the trace
        if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
        {
            // RAFAEL
            if (self->s.skinnum > 1)
                monster_fire_heat(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.075f);
            else
                // RAFAEL
                monster_fire_rocket(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1);
        }
        else
        {
            vec = target;
            vec += (right * -10);
            dir = vec - start;
            dir.normalize();
            trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
            if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
            {
                monster_fire_heat(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.075f);
            }
            else
            {
                vec = target;
                vec += (right * 10);
                dir = vec - start;
                dir.normalize();
                trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
                if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
                {
                    monster_fire_heat(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.075f);
                }
            }
        }
    }
    else
    {
        if (trace.fraction > 0.5f || trace.ent->solid != SOLID_BSP)
        {
            monster_fire_heat(self, start, dir, 50, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.095f);
        }
    }
}

// GM Arachnid attack animations
mframe_t gm_arachnid_frames_attack1[] = {
    { ai_charge, 0, gm_arachnid_blind_check },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge }
};
MMOVE_T(gm_arachnid_attack1) = { FRAME_rails5, FRAME_rails11, gm_arachnid_frames_attack1, arachnid2_run };

mframe_t gm_arachnid_frames_attack_up1[] = {
    { ai_charge, 0, gm_arachnid_blind_check },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(gm_arachnid_attack_up1) = { FRAME_rails_up3, FRAME_rails_up16, gm_arachnid_frames_attack_up1, arachnid2_run };

// Attack decision for arachnid2
MONSTERINFO_ATTACK(arachnid2_attack) (edict_t* self) -> void
{
    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    if (self->monsterinfo.melee_debounce_time < level.time && range_to(self, self->enemy) < MELEE_DISTANCE)
        M_SetAnimation(self, &arachnid2_melee);
    else
    {
        if (self->style == 1)
        {
            if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f)
                M_SetAnimation(self, &gm_arachnid_attack_up1);
            else
                M_SetAnimation(self, &gm_arachnid_attack1);
        }
        else
        {
            if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f)
                M_SetAnimation(self, &arachnid2_attack_up1);
            else
                M_SetAnimation(self, &arachnid2_attack1);
        }
    }
}

// Death for arachnid2
MMOVE_T(arachnid2_move_death) = { FRAME_death1, FRAME_death20, arachnid_frames_death1, arachnid_dead };

DIE(arachnid2_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
    arachnid_die_internal(self, inflictor, attacker, damage, point, mod, &arachnid2_move_death);
}

// Skin management
MONSTERINFO_SETSKIN(arachnid2_setskin) (edict_t* self) -> void
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum |= 1;
    else
        self->s.skinnum &= ~1;
}

//spider skinnum, fixing wounded skin
MONSTERINFO_SETSKIN(spider_setskin) (edict_t* self) -> void
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum |= 1;
    else
        self->s.skinnum &= ~1;
}

//==========================================================================================
// PSX ARACHNID IMPLEMENTATION
//==========================================================================================

MONSTERINFO_SIGHT(arachnid_psx_sight) (edict_t* self, edict_t* other) -> void
{
    gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_STAND(arachnid_psx_stand) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid_move_stand);
}

// Walk animation
mframe_t arachnid_psx_frames_walk[] = {
    { ai_walk, 2, arachnid_footstep },
    { ai_walk, 5 },
    { ai_walk, 12 },
    { ai_walk, 16 },
    { ai_walk, 5 },
    { ai_walk, 8, arachnid_footstep },
    { ai_walk, 8 },
    { ai_walk, 12 },
    { ai_walk, 9 },
    { ai_walk, 5 }
};
MMOVE_T(arachnid_psx_move_walk) = { FRAME_walk1, FRAME_walk10, arachnid_psx_frames_walk, nullptr };

MONSTERINFO_WALK(arachnid_psx_walk) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid_psx_move_walk);
}

// Run animation
mframe_t arachnid_psx_frames_run[] = {
    { ai_run, 2, arachnid_footstep },
    { ai_run, 5 },
    { ai_run, 12 },
    { ai_run, 16 },
    { ai_run, 5 },
    { ai_run, 8, arachnid_footstep },
    { ai_run, 8 },
    { ai_run, 12 },
    { ai_run, 9 },
    { ai_run, 5 }
};
MMOVE_T(arachnid_psx_move_run) = { FRAME_walk1, FRAME_walk10, arachnid_psx_frames_run, nullptr };

MONSTERINFO_RUN(arachnid_psx_run) (edict_t* self) -> void
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
    {
        M_SetAnimation(self, &arachnid_move_stand);
        return;
    }

    M_SetAnimation(self, &arachnid_psx_move_run);
}

// Pain animations
MMOVE_T(arachnid_psx_move_pain1) = { FRAME_pain11, FRAME_pain15, arachnid_frames_pain1, arachnid_psx_run };
MMOVE_T(arachnid_psx_move_pain2) = { FRAME_pain21, FRAME_pain26, arachnid_frames_pain2, arachnid_psx_run };

PAIN(arachnid_psx_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + 3_sec;
    gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

    float const r = frandom();

    if (r < 0.5f)
        M_SetAnimation(self, &arachnid_psx_move_pain1);
    else
        M_SetAnimation(self, &arachnid_psx_move_pain2);
}

// PSX arachnid rail gun
static void arachnid_psx_charge_rail(edict_t* self, monster_muzzleflash_id_t mz)
{
    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    gi.sound(self, CHAN_WEAPON, sound_charge, 1.f, ATTN_NORM, 0.f);

    vec3_t forward, right, start;
    AngleVectors(self->s.angles, forward, right, nullptr);
    start = M_ProjectFlashSource(self, monster_flash_offset[mz], forward, right);

    PredictAim(self, self->enemy, start, 0, false, 0.0f, nullptr, &self->pos1);
}

static void arachnid_psx_charge_rail_left(edict_t* self)
{
    arachnid_psx_charge_rail(self, MZ2_ARACHNID_RAIL1);
}

static void arachnid_psx_charge_rail_right(edict_t* self)
{
    arachnid_psx_charge_rail(self, MZ2_ARACHNID_RAIL2);
}

static void arachnid_psx_charge_rail_up_left(edict_t* self)
{
    arachnid_psx_charge_rail(self, MZ2_ARACHNID_RAIL_UP1);
}

static void arachnid_psx_charge_rail_up_right(edict_t* self)
{
    arachnid_psx_charge_rail(self, MZ2_ARACHNID_RAIL_UP2);
}

void arachnid_psx_rail_real(edict_t* self, monster_muzzleflash_id_t id)
{
    vec3_t start;
    vec3_t dir;
    vec3_t forward, right, up;

    AngleVectors(self->s.angles, forward, right, up);
    start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);
    int dmg = 50;

    if (self->s.frame >= FRAME_melee_in1 && self->s.frame <= FRAME_melee_in16)
    {
        // scan our current direction for players
        std::array<edict_t*, 8> players_scanned{};
        size_t num_players = 0;

        for (auto player : active_players())
        {
            if (!visible(self, player, false))
                continue;

            if (infront_cone(self, player, 0.5f))
            {
                players_scanned[num_players++] = player;

                if (num_players == players_scanned.size())
                    break;
            }
        }

        if (num_players != 0)
        {
            edict_t* chosen = players_scanned[irandom(num_players)];

            PredictAim(self, chosen, start, 0, false, 0.0f, nullptr, &self->pos1);

            dir = (chosen->s.origin - self->s.origin).normalized();

            self->ideal_yaw = vectoyaw(dir);
            self->s.angles[YAW] = self->ideal_yaw;

            dir = (self->pos1 - start).normalized();

            for (int i = 0; i < 3; i++)
                dir[i] += crandom_open() * 0.018f;
            dir = dir.normalized();
        }
        else
        {
            dir = forward;
        }
    }
    else
    {
        // calc direction to where we targeted
        dir = (self->pos1 - start).normalized();
        dmg = 50;
    }

    bool const hit = monster_fire_railgun(self, start, dir, dmg, dmg * 2.0f, id);

    if (dmg == 50)
    {
        if (hit)
            self->count = 0;
        else
            self->count++;
    }
}

void arachnid_psx_rail(edict_t* self)
{
    monster_muzzleflash_id_t id;

    switch (self->s.frame)
    {
    case FRAME_rails4:
    default:
        id = MZ2_ARACHNID_RAIL1;
        break;
    case FRAME_rails8:
        id = MZ2_ARACHNID_RAIL2;
        break;
    case FRAME_rails_up7:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up11:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    }

    arachnid_psx_rail_real(self, id);
}

// PSX arachnid attack
mframe_t arachnid_psx_frames_attack1[] = {
    { ai_charge },
    { ai_charge, 0, arachnid_psx_charge_rail_left },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_rail },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_charge_rail_right },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(arachnid_psx_attack1) = { FRAME_rails1, FRAME_rails11, arachnid_psx_frames_attack1, arachnid_psx_run };

mframe_t arachnid_psx_frames_attack_up1[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_charge_rail_up_left },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_rail },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_charge_rail_up_right },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
};
MMOVE_T(arachnid_psx_attack_up1) = { FRAME_rails_up1, FRAME_rails_up16, arachnid_psx_frames_attack_up1, arachnid_psx_run };

// PSX arachnid melee attack
void arachnid_psx_melee_hit(edict_t* self)
{
    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    if (!fire_hit(self, { MELEE_DISTANCE, 0, 0 }, 15, 50))
    {
        self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
        self->count++;
    }
    else if (self->s.frame == FRAME_melee_atk11 &&
        self->monsterinfo.melee_debounce_time < level.time)
        self->monsterinfo.nextframe = FRAME_melee_atk2;
}

// Melee animations
mframe_t arachnid_psx_frames_melee_out[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(arachnid_psx_melee_out) = { FRAME_melee_out1, FRAME_melee_out3, arachnid_psx_frames_melee_out, arachnid_psx_run };

void arachnid_psx_to_out_melee(edict_t* self)
{
    M_SetAnimation(self, &arachnid_psx_melee_out);
}

mframe_t arachnid_psx_frames_melee[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_melee_hit },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_melee_hit },
    { ai_charge }
};
MMOVE_T(arachnid_psx_melee) = { FRAME_melee_atk1, FRAME_melee_atk12, arachnid_psx_frames_melee, arachnid_psx_to_out_melee };

void arachnid_psx_to_melee(edict_t* self)
{
    M_SetAnimation(self, &arachnid_psx_melee);
}

mframe_t arachnid_psx_frames_melee_in[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(arachnid_psx_melee_in) = { FRAME_melee_in1, FRAME_melee_in3, arachnid_psx_frames_melee_in, arachnid_psx_to_melee };

// Rapid fire rail
static void arachnid_psx_rail_rapid(edict_t* self)
{
    bool const left_shot = self->s.frame == FRAME_melee_in9;
    arachnid_psx_rail_real(self, left_shot ? MZ2_ARACHNID_RAIL1 : MZ2_ARACHNID_RAIL2);
}

mframe_t arachnid_psx_frames_attack3[] = {
    { ai_charge },
    { ai_move, 0, arachnid_psx_rail_rapid },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, arachnid_psx_rail_rapid },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, arachnid_psx_rail_rapid },
    { ai_move },
    { ai_move },
    { ai_charge }
};
MMOVE_T(arachnid_psx_attack3) = { FRAME_melee_in4, FRAME_melee_in16, arachnid_psx_frames_attack3, arachnid_psx_to_out_melee };

static void arachnid_psx_rapid_fire(edict_t* self)
{
    self->count = 0;
    M_SetAnimation(self, &arachnid_psx_attack3);
}

// Spawning function for PSX arachnid (skill 3 only)
static void arachnid_psx_spawn(edict_t* self)
{
    if (skill->integer != 3)
        return;

    static constexpr vec3_t reinforcement_position[] = { { -24.f, 124.f, 0 }, { -24.f, -124.f, 0 } };
    vec3_t f, r, offset, startpoint, spawnpoint;
    int    count;

    AngleVectors(self->s.angles, f, r, nullptr);

    int num_summoned;
    self->monsterinfo.chosen_reinforcements = M_PickReinforcements(self, num_summoned, 2);

    for (count = 0; count < num_summoned; count++)
    {
        offset = reinforcement_position[count];

        if (self->s.scale)
            offset *= self->s.scale;

        startpoint = M_ProjectFlashSource(self, offset, f, r);
        startpoint[2] += 10 * (self->s.scale ? self->s.scale : 1.0f);

        // --- NEW ID-BASED LOGIC ---
        uint8_t def_index = self->monsterinfo.chosen_reinforcements[count];
        if (def_index >= self->monsterinfo.reinforcements.defs.size()) continue;

        const auto& reinforcement_def = self->monsterinfo.reinforcements.defs[def_index];
        horde::MonsterTypeID typeId = reinforcement_def.typeId;
        const char* classname = horde::MonsterTypeRegistry::GetClassname(typeId);
        if (!classname) continue;

        vec3_t mins, maxs;
        GetPredictedScaledBounds(typeId, mins, maxs);
        // --- END NEW LOGIC ---

        if (FindSpawnPoint(startpoint, mins, maxs, spawnpoint, 32))
        {
            if (CheckGroundSpawnPoint(spawnpoint, mins, maxs, 256, -1))
            {
                edict_t* ent = CreateGroundMonster(spawnpoint, self->s.angles, mins, maxs, classname, 256);
                if (!ent) return;

                ent->nextthink = level.time;
                ent->think(ent);

                ent->monsterinfo.aiflags |= AI_SPAWNED_COMMANDER | AI_DO_NOT_COUNT | AI_IGNORE_SHOTS;
                ent->monsterinfo.commander = self;
                ent->monsterinfo.slots_from_commander = reinforcement_def.strength;
                self->monsterinfo.monster_used += reinforcement_def.strength;

                gi.sound(ent, CHAN_BODY, sound_spawn, 1, ATTN_NONE, 0);

                if ((self->enemy->inuse) && (self->enemy->health > 0))
                {
                    ent->enemy = self->enemy;
                    FoundTarget(ent);
                }

                float const radius = (maxs - mins).length() * 0.5f;
                SpawnGrow_Spawn(spawnpoint + (mins + maxs), radius, radius * 2.f);
            }
        }
    }
}

// Taunt animation and spawning
mframe_t arachnid_psx_frames_taunt[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_spawn },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(arachnid_psx_taunt) = { FRAME_melee_pain1, FRAME_melee_pain16, arachnid_psx_frames_taunt, arachnid_psx_rapid_fire };

// PSX arachnid attack decision
MONSTERINFO_ATTACK(arachnid_psx_attack) (edict_t* self) -> void
{
    if (!M_HasValidTarget(self))
    {
        return; // Stop immediately if the target is invalid.
    }

    if (self->monsterinfo.melee_debounce_time < level.time && range_to(self, self->enemy) < MELEE_DISTANCE)
        M_SetAnimation(self, &arachnid_psx_melee_in);
    // annoyed rapid fire attack
    else if (self->enemy->client &&
        self->last_move_time <= level.time &&
        self->count >= 4 &&
        frandom() < (max(self->count / 2.0f, 4.0f) + 1.0f) * 0.2f &&
        (M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL1]) || M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL2])))
    {
        M_SetAnimation(self, &arachnid_psx_taunt);
        gi.sound(self, CHAN_VOICE, sound_pissed, 1.f, 0.25f, 0.f);
        self->count = 0;
        self->pain_debounce_time = level.time + 4.5_sec;
        self->last_move_time = level.time + 10_sec;
    }
    else if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f &&
        (M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL_UP1]) || M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL_UP2])))
        M_SetAnimation(self, &arachnid_psx_attack_up1);
    else if (M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL1]) || M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL2]))
        M_SetAnimation(self, &arachnid_psx_attack1);
}

// PSX arachnid death
MMOVE_T(arachnid_psx_move_death) = { FRAME_death1, FRAME_death20, arachnid_frames_death1, arachnid_dead };

DIE(arachnid_psx_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
    arachnid_die_internal(self, inflictor, attacker, damage, point, mod, &arachnid_psx_move_death);
}

// PSX arachnid skin management
MONSTERINFO_SETSKIN(arachnid_psx_setskin) (edict_t* self) -> void
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

//==========================================================================================
// SPAWN FUNCTIONS
//==========================================================================================

// Helper function to initialize common sound effects
void initialize_arachnid_sounds()
{
    static bool sounds_initialized = false;

    if (!sounds_initialized)
    {
        sound_step.assign("insane/insane11.wav");
        sound_charge.assign("gladiator/railgun.wav");
        sound_melee.assign("gladiator/melee3.wav");
        sound_melee_hit.assign("gladiator/melee2.wav");
        sound_pain.assign("arachnid/pain.wav");
        sound_death.assign("arachnid/death.wav");
        sound_sight.assign("arachnid/sight.wav");

        sounds_initialized = true;
    }
}

/*QUAKED monster_arachnid (1 .5 0) (-48 -48 -20) (48 48 48) Ambush Trigger_Spawn Sight
*/
void SP_monster_arachnid(edict_t* self)
{
    const spawn_temp_t& st = ED_GetSpawnTemp();

    if (!M_AllowSpawn(self)) {
        G_FreeEdict(self);
        return;
    }

    initialize_arachnid_sounds();

    // --- EAGER INITIALIZATION ---
    // Set the ID to the base ARACHNID type.
    if (self->monsterinfo.monster_type_id == MONSTER_TYPE_UNKNOWN) { // Check if it hasn't been set yet
        self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID);
    }

    if (!self->monsterinfo.IS_BOSS)
    {
        self->health = 1000 * st.health_multiplier;
        self->gib_health = -200;
    }

    self->s.modelindex = gi.modelindex("models/monsters/arachnid/tris.md2");
    self->mins = { -48, -48, -20 };
    self->maxs = { 48, 48, 48 };
    self->movetype = MOVETYPE_STEP;
    self->solid = SOLID_BBOX;

    self->monsterinfo.scale = MODEL_SCALE;
    self->mass = 450;

    self->pain = arachnid_pain;
    self->die = arachnid_die;
    self->monsterinfo.stand = arachnid_stand;
    self->monsterinfo.walk = arachnid_walk;
    self->monsterinfo.run = arachnid_run;
    self->monsterinfo.attack = arachnid_attack;
    self->monsterinfo.sight = arachnid_sight;
    self->monsterinfo.setskin = arachnid2_setskin;

    gi.linkentity(self);

    M_SetAnimation(self, &arachnid_move_stand);

    walkmonster_start(self);

    // --- REFACTORED ---
    // This check is now clean and clear. It only applies to the base arachnid
    // when it's spawned as a boss.
    if (horde::IsMonsterType(self, horde::MonsterTypeID::ARACHNID) && self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED) {
        self->health = 3500 + (1.08 * current_wave_level);
        self->gib_health = -99999;
    }
    ApplyMonsterBonusFlags(self);
}

/*QUAKED monster_spider (1 .5 0) (-48 -48 -20) (48 48 48) Ambush Trigger_Spawn Sight
*/
void SP_monster_spider(edict_t* self)
{
    const spawn_temp_t& st = ED_GetSpawnTemp();

    // Initialize plasma sounds
    sound_plasma.assign("weapons/plasshot.wav");
    sound_plasma_hit.assign("weapons/plasma/hit.wav");

    gi.soundindex("weapons/plasma/fire1.wav");
    gi.soundindex("weapons/plasma/hit.wav");

    self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::SPIDER);
    self->spawnflags |= SPAWNFLAG_SPIDER;
    SP_monster_arachnid(self); // Call the base spawner

    // Override functions for spider variant
    self->monsterinfo.stand = spider_stand;
    self->monsterinfo.run = spider_run;
    self->monsterinfo.attack = spider_attack;
    self->monsterinfo.dodge = spider_dodge;
    self->monsterinfo.blocked = spider_blocked;
    self->monsterinfo.physics_change = spider_physics_change;
    self->monsterinfo.can_jump = true;
    self->monsterinfo.jump_height = 68;
    self->monsterinfo.drop_height = 256;
    self->gravityVector = { 0, 0, -1 };

    self->monsterinfo.weapon_sound = gi.soundindex("weapons/phaloop.wav");

    // --- REFACTORED ---
    // The strcmp is no longer needed because we know this is a spider.
    self->s.scale = 0.7f;
    self->health = IsFirstThreeWaves(current_wave_level) ? 350 * st.health_multiplier : 550 * st.health_multiplier;
    self->max_health = self->health;
    self->mins *= self->s.scale;
    self->maxs *= self->s.scale;
    
    ApplyMonsterBonusFlags(self);
}

/*QUAKED monster_arachnid2 (1 .5 0) (-48 -48 -20) (48 48 48) Ambush Trigger_Spawn Sight
*/
void SP_monster_arachnid2(edict_t* self)
{
    const spawn_temp_t& st = ED_GetSpawnTemp();

    if (!M_AllowSpawn(self)) {
        G_FreeEdict(self);
        return;
    }
    if (self->monsterinfo.monster_type_id == MONSTER_TYPE_UNKNOWN)
    self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID2);

    initialize_arachnid_sounds();

    self->s.modelindex = gi.modelindex("models/monsters/arachnid/tris.md2");
    self->mins = { -48, -48, -20 };
    self->maxs = { 48, 48, 48 };
    self->movetype = MOVETYPE_STEP;
    self->solid = SOLID_BBOX;
    self->monsterinfo.scale = MODEL_SCALE;

    // --- REFACTORED ---
    // Use the ID check instead of strcmp.
    if (horde::IsMonsterType(self, horde::MonsterTypeID::ARACHNID2) && !self->monsterinfo.IS_BOSS)
    {
        self->s.scale = 0.85f;
        self->mins *= self->s.scale;
        self->maxs *= self->s.scale;
    }
    self->gib_health = -200;
    self->mass = 450;
    self->health = 1000 * st.health_multiplier;

    self->pain = arachnid2_pain;
    self->die = arachnid2_die;
    self->monsterinfo.stand = arachnid2_stand;
    self->monsterinfo.walk = arachnid2_walk;
    self->monsterinfo.run = arachnid2_run;
    self->monsterinfo.attack = arachnid2_attack;
    self->monsterinfo.sight = arachnid2_sight;
    gi.linkentity(self);

    M_SetAnimation(self, &arachnid_move_stand);

    walkmonster_start(self);
    ApplyMonsterBonusFlags(self);
}

/*QUAKED monster_gm_arachnid (1 .5 0) (-48 -48 -20) (48 48 48) Ambush Trigger_Spawn Sight
*/
void SP_monster_gm_arachnid(edict_t* self)
{
    const spawn_temp_t& st = ED_GetSpawnTemp();

    self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::GM_ARACHNID);
    SP_monster_arachnid2(self); // Calls the base spawner


    self->monsterinfo.armor_type = IT_ARMOR_COMBAT;
    self->monsterinfo.armor_power = 500;
    self->style = 1; // This style flag is used in arachnid2_attack, we can replace that later.
    self->health = 1000 * st.health_multiplier;
    if (g_horde->integer) {
        self->s.scale = 0.85f;
        self->mins *= self->s.scale;
        self->maxs *= self->s.scale;
    }

    // --- REFACTORED ---
    if (horde::IsMonsterType(self, horde::MonsterTypeID::GM_ARACHNID) && 
    self->monsterinfo.IS_BOSS && 
    !self->monsterinfo.BOSS_DEATH_HANDLED) {     
    
    self->health = 2800 + (1.08 * current_wave_level);
    }
    ApplyMonsterBonusFlags(self);
}

/*QUAKED monster_psxarachnid (1 .5 0) (-40 -40 -20) (40 40 48) Ambush Trigger_Spawn Sight
*/
void SP_monster_psxarachnid(edict_t* self)
{
    const spawn_temp_t& st = ED_GetSpawnTemp();

    if (!M_AllowSpawn(self)) {
        G_FreeEdict(self);
        return;
    }

    initialize_arachnid_sounds();
    sound_pissed.assign("guncmdr/gcdrsrch1.wav");
    self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::PSX_ARACHNID);
    sound_spawn.assign("medic_commander/monsterspawn1.wav");

    // --- MODIFIED REINFORCEMENT SETUP ---
    if (!st.was_key_specified("monster_slots"))
        self->monsterinfo.monster_slots = default_monster_slots_base;

    if (self->monsterinfo.monster_slots)
    {
        if (self->monsterinfo.IS_BOSS)
            M_SetupReinforcements(psx_boss_reinforcements_defs, self->monsterinfo.reinforcements);
        else
            M_SetupReinforcements(psx_coop_reinforcements_defs, self->monsterinfo.reinforcements);
    }
    // --- END MODIFICATION ---

    self->s.modelindex = gi.modelindex("models/monsters/arachnid/tris.md2");
    self->mins = { -48, -48, -20 };
    self->maxs = { 48, 48, 48 };
    self->movetype = MOVETYPE_STEP;
    self->solid = SOLID_BBOX;

    if (!st.was_key_specified("power_armor_type") && self->monsterinfo.IS_BOSS)
        self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
    if (!st.was_key_specified("power_armor_power") && self->monsterinfo.IS_BOSS)
        self->monsterinfo.power_armor_power = 2500;

    self->health = 1000 * st.health_multiplier;
    self->gib_health = -200;

    self->monsterinfo.scale = MODEL_SCALE;
    self->mass = 450;

    self->pain = arachnid_psx_pain;
    self->die = arachnid_psx_die;
    self->monsterinfo.stand = arachnid_psx_stand;
    self->monsterinfo.walk = arachnid_psx_walk;
    self->monsterinfo.run = arachnid_psx_run;
    self->monsterinfo.attack = arachnid_psx_attack;
    self->monsterinfo.sight = arachnid_psx_sight;
    self->monsterinfo.setskin = arachnid_psx_setskin;

    self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

    gi.linkentity(self);

    M_SetAnimation(self, &arachnid_move_stand);

    walkmonster_start(self);

    ApplyMonsterBonusFlags(self);
}