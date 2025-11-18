// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

TANK AND TANK SPAWNER
All tank variants unified in a single file

==============================================================================
*/

#include "g_local.h"
#include "m_tank.h"
#include "m_flash.h"
#include "shared.h"
#include "horde/g_horde_scaling.h"
#include "monster_constants.h"
#include <algorithm>
#include <numeric>
#include <boost/container/flat_map.hpp>
#include <boost/container/small_vector.hpp>

// Forward declarations for all variants
// Standard tank functions
void tank_refire_rocket(edict_t* self);
void tank_doattack_rocket(edict_t* self);
void tank_reattack_blaster(edict_t* self);
void tank_reattack_grenades(edict_t* self);

// Tank spawner functions
void tank_vanilla_refire_rocket(edict_t* self);
void tank_vanilla_doattack_rocket(edict_t* self);
void tank_vanilla_reattack_blaster(edict_t* self);

// Shared sound indices
static cached_soundindex sound_thud;
static cached_soundindex sound_pain, sound_pain2;
static cached_soundindex sound_idle;
static cached_soundindex sound_die;
static cached_soundindex sound_step;
static cached_soundindex sound_sight;
static cached_soundindex sound_windup;
static cached_soundindex sound_strike;
static cached_soundindex sound_grenade;
static cached_soundindex sound_spawn_commander;

// Shared constants
constexpr spawnflags_t SPAWNFLAG_TANK_COMMANDER_GUARDIAN = 8_spawnflag;
constexpr spawnflags_t SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING = 16_spawnflag;
constexpr int32_t MONSTER_MAX_SLOTS = 6;
constexpr float MORTAR_SPEED = 1850.f;
constexpr float GRENADE_SPEED = 1600.f;

// Teleportation and spawn cache constants
constexpr int32_t NUM_TELEPORT_DIRECTIONS = 8;
constexpr int32_t NUM_SPAWN_DIRECTIONS = 16; // More directions for spawning
constexpr gtime_t TELEPORT_CACHE_DURATION = 150_ms;
constexpr std::array<vec3_t, NUM_TELEPORT_DIRECTIONS> teleport_directions = {{
	{1.0f, 0.0f, 0.0f},         // 0°
	{0.707f, 0.707f, 0.0f},     // 45°
	{0.0f, 1.0f, 0.0f},         // 90°
	{-0.707f, 0.707f, 0.0f},    // 135°
	{-1.0f, 0.0f, 0.0f},        // 180°
	{-0.707f, -0.707f, 0.0f},   // 225°
	{0.0f, -1.0f, 0.0f},        // 270°
	{0.707f, -0.707f, 0.0f}     // 315°
}};

// Pre-calculated spawn directions (16 directions for better coverage)
constexpr std::array<vec3_t, NUM_SPAWN_DIRECTIONS> spawn_directions = {{
	{1.0f, 0.0f, 0.0f},         // 0°
	{0.924f, 0.383f, 0.0f},     // 22.5°
	{0.707f, 0.707f, 0.0f},     // 45°
	{0.383f, 0.924f, 0.0f},     // 67.5°
	{0.0f, 1.0f, 0.0f},         // 90°
	{-0.383f, 0.924f, 0.0f},    // 112.5°
	{-0.707f, 0.707f, 0.0f},    // 135°
	{-0.924f, 0.383f, 0.0f},    // 157.5°
	{-1.0f, 0.0f, 0.0f},        // 180°
	{-0.924f, -0.383f, 0.0f},   // 202.5°
	{-0.707f, -0.707f, 0.0f},   // 225°
	{-0.383f, -0.924f, 0.0f},   // 247.5°
	{0.0f, -1.0f, 0.0f},        // 270°
	{0.383f, -0.924f, 0.0f},    // 292.5°
	{0.707f, -0.707f, 0.0f},    // 315°
	{0.924f, -0.383f, 0.0f}     // 337.5°
}};

// Teleportation cache structures
struct TeleportPositionCache {
	vec3_t position;
	gtime_t validation_time;
	bool is_valid;

	TeleportPositionCache() : position{0, 0, 0}, validation_time(0_sec), is_valid(false) {}
};

struct TeleportTargetCache {
	edict_t* target;
	vec3_t last_target_origin;
	gtime_t last_update_time;
	std::array<TeleportPositionCache, NUM_TELEPORT_DIRECTIONS> positions;

	TeleportTargetCache() : target(nullptr), last_target_origin{0, 0, 0}, last_update_time(0_sec) {}

	bool IsStale(edict_t* current_target) const {
		return target != current_target ||
		       level.time - last_update_time > TELEPORT_CACHE_DURATION ||
		       last_target_origin != current_target->s.origin;
	}

	void Update(edict_t* new_target) {
		target = new_target;
		last_target_origin = new_target->s.origin;
		last_update_time = level.time;
		// Reset all position validations
		for (auto& pos : positions) {
			pos.is_valid = false;
			pos.validation_time = 0_sec;
		}
	}
};

// Static cache for tank teleportation
static boost::container::flat_map<int, TeleportTargetCache> g_teleport_cache;  // C++23 - integer keys for better cache locality

// Cache management functions
static TeleportTargetCache& GetOrCreateTargetCache(edict_t* target) {
	// Prevent unbounded growth in long Horde matches
	if (g_teleport_cache.size() > 256) {
		g_teleport_cache.clear();
	}

	auto& cache = g_teleport_cache[target->s.number];
	if (cache.IsStale(target)) {
		cache.Update(target);
	}
	return cache;
}

static bool ValidateTeleportPosition(edict_t* self, const vec3_t& position, edict_t* target) {
	// Check if position is valid using the same logic as original function
	trace_t tr = gi.trace(position, self->mins, self->maxs, position, nullptr, MASK_MONSTERSOLID);
	return !(tr.contents & MASK_MONSTERSOLID);
}

// Reset teleport cache on game reset (called from ResetGame)
void ResetTankTeleportCache() {
	g_teleport_cache.clear();
}

// Tank spawner constants
constexpr spawnflags_t SPAWNFLAG_tank_vanilla_COMMANDER_GUARDIAN = 8_spawnflag;
constexpr spawnflags_t SPAWNFLAG_tank_vanilla_COMMANDER_HEAT_SEEKING = 16_spawnflag;
constexpr int32_t TANK_VANILLA_MAX_REINFORCEMENTS = 5;
constexpr std::array<vec3_t, TANK_VANILLA_MAX_REINFORCEMENTS> tank_vanilla_reinforcement_position = {
	vec3_t { 80, 0, 0 },
	vec3_t { 40, 60, 0 },
	vec3_t { 40, -60, 0 },
	vec3_t { 0, 80, 0 },
	vec3_t { 0, -80, 0 }
};

// Reinforcement configuration
constexpr std::array<reinforcement_def_t, 7> tank_vanilla_special_reinforcements_defs = { {
	{horde::MonsterTypeID::CHICK_HEAT, 1}, {horde::MonsterTypeID::CHICK_HEAT, 1},
	{horde::MonsterTypeID::GUNCMDR, 1},    {horde::MonsterTypeID::SPIDER, 1},
	{horde::MonsterTypeID::SPIDER, 1},     {horde::MonsterTypeID::BRAIN, 1},
	{horde::MonsterTypeID::JANITOR, 1}
} };

constexpr std::array<reinforcement_def_t, 7> tank_vanilla_hard_reinforcements_defs = { {
	{horde::MonsterTypeID::SOLDIER, 1},       {horde::MonsterTypeID::SOLDIER_SS, 1},
	{horde::MonsterTypeID::SOLDIER, 1},       {horde::MonsterTypeID::SOLDIER_SS, 1},
	{horde::MonsterTypeID::SOLDIER_LIGHT, 1}, {horde::MonsterTypeID::SOLDIER_SS, 1},
	{horde::MonsterTypeID::SOLDIER_SS, 1}
} };

constexpr std::array<reinforcement_def_t, 7> tank_vanilla_insane_reinforcements_defs = { {
	{horde::MonsterTypeID::CHICK, 1},          {horde::MonsterTypeID::CHICK, 1},
	{horde::MonsterTypeID::BERSERK, 1},        {horde::MonsterTypeID::BERSERK, 1},
	{horde::MonsterTypeID::GUNNER, 1},         {horde::MonsterTypeID::GUNNER_VANILLA, 1},
	{horde::MonsterTypeID::GUNNER_VANILLA, 1}
} };

//////////////////////////////////////////////////////////////////////////////
//                        SHARED UTILITY FUNCTIONS                          //
//////////////////////////////////////////////////////////////////////////////


// Teleport near target function for commander - optimized with cache
[[nodiscard]] bool TeleportNearTarget(edict_t* self, edict_t* target, float dist, bool effect)
{
	// Get or create cache for this target
	TeleportTargetCache& cache = GetOrCreateTargetCache(target);

	// Try cached positions first, then validate new ones
	for (int i = 0; i < NUM_TELEPORT_DIRECTIONS; i++)
	{
		TeleportPositionCache& pos_cache = cache.positions[i];

		// Check if we have a recent valid cached result
		if (pos_cache.is_valid &&
		    level.time - pos_cache.validation_time < TELEPORT_CACHE_DURATION)
		{
			if (ValidateTeleportPosition(self, pos_cache.position, target))
			{
				if (effect)
				{
					// Teleport effect at both positions
					gi.WriteByte(svc_temp_entity);
					gi.WriteByte(TE_BFG_BIGEXPLOSION);
					gi.WritePosition(self->s.origin);
					gi.multicast(self->s.origin, MULTICAST_PVS, false);

					gi.unlinkentity(self);

					gi.WriteByte(svc_temp_entity);
					gi.WriteByte(TE_BOSSTPORT);
					gi.WritePosition(pos_cache.position);
					gi.multicast(pos_cache.position, MULTICAST_PVS, false);
				}

				self->s.origin = pos_cache.position;
				gi.linkentity(self);
				return true;
			}
			else
			{
				// Position is no longer valid, mark as invalid
				pos_cache.is_valid = false;
			}
		}

		// Calculate new position using pre-computed direction vectors
		const vec3_t& forward = teleport_directions[i];
		vec3_t end = target->s.origin + (forward * (target->maxs[0] + self->maxs[0] + dist));

		// First trace to check path
		trace_t tr = gi.traceline(target->s.origin, end, target, MASK_MONSTERSOLID);

		// Store trace endpoint for floor check
		vec3_t start = tr.endpos;
		end = tr.endpos;
		end.z -= fabsf(self->mins[2]) + 32.0f;

		// Trace to floor
		tr = gi.traceline(start, end, nullptr, MASK_MONSTERSOLID);

		// Don't teleport off ledges unless we can fly
		if (tr.fraction == 1.0f && !(self->flags & FL_FLY))
		{
			pos_cache.is_valid = false;
			pos_cache.validation_time = level.time;
			continue;
		}

		// Set up final position
		start = tr.endpos;
		start.z += fabsf(self->mins[2]) + 1.0f;

		// Check if position is valid
		tr = gi.trace(start, self->mins, self->maxs, start, nullptr, MASK_MONSTERSOLID);
		if (!(tr.contents & MASK_MONSTERSOLID))
		{
			// Cache the successful position
			pos_cache.position = start;
			pos_cache.is_valid = true;
			pos_cache.validation_time = level.time;

			if (effect)
			{
				// Teleport effect at both positions
				gi.WriteByte(svc_temp_entity);
				gi.WriteByte(TE_BFG_BIGEXPLOSION);
				gi.WritePosition(self->s.origin);
				gi.multicast(self->s.origin, MULTICAST_PVS, false);

				gi.unlinkentity(self);

				gi.WriteByte(svc_temp_entity);
				gi.WriteByte(TE_BOSSTPORT);
				gi.WritePosition(start);
				gi.multicast(start, MULTICAST_PVS, false);
			}

			self->s.origin = start;
			gi.linkentity(self);
			return true;
		}
		else
		{
			// Cache the failed position to avoid recalculating it soon
			pos_cache.position = start;
			pos_cache.is_valid = false;
			pos_cache.validation_time = level.time;
		}
	}
	return false;
}

// Blindfire target adjustment - regular tank version
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

// Blindfire target adjustment - spawner tank version
bool M_AdjustBlindfireTarget2(edict_t* self, const vec3_t& start, const vec3_t& target, const vec3_t& right, vec3_t& out_dir)
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
	vec3_t const left_target = target + (right * -20);
	trace = gi.traceline(start, left_target, self, MASK_PROJECTILE);

	if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
	{
		out_dir = left_target - start;
		out_dir.normalize();
		return true;
	}

	// ok, that failed.  try to the right
	vec3_t const right_target = target + (right * 20);
	trace = gi.traceline(start, right_target, self, MASK_PROJECTILE);
	if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
	{
		out_dir = right_target - start;
		out_dir.normalize();
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////
//                        REGULAR TANK IMPLEMENTATION                       //
//////////////////////////////////////////////////////////////////////////////

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

MONSTERINFO_RUN(tank_run) (edict_t* self) -> void
{
	self->monsterinfo.aiflags &= ~AI_BRUTAL;

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

//
// attacks
//

void TankGrenades(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	int damage = GetMonsterWeaponDamage(self->monsterinfo.monster_type_id, horde::WeaponID::GRENADE);
	if (damage <= 0) damage = 50;

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

	vec3_t const start = M_ProjectFlashSource(self, offset, forward, right);
	const bool is_mortar = (self->s.frame == FRAME_attak110);

	// Get configured grenade speed or use defaults
	int config_speed = GetMonsterWeaponSpeed(self->monsterinfo.monster_type_id, horde::WeaponID::GRENADE);
	const float speed = config_speed > 0 ? static_cast<float>(config_speed) : (is_mortar ? MORTAR_SPEED : GRENADE_SPEED);
	vec3_t aim, aimpoint;

	const float dist = range_to(self, self->enemy);

	// Para distancias cortas, usar solo PredictAim
	if (dist < 400 && !is_mortar)  // No aplicar a disparos de mortero
	{
		PredictAim(self, self->enemy, start, speed, true, 0, &aim, &aimpoint);
		aim += right * (crandom() * 0.02f);  // Pequeño ajuste aleatorio
		aim.normalize();
		// FIX: Round all float arguments to integers
		monster_fire_grenade(self, start, aim, damage, lroundf(speed), MZ2_UNUSED_0,
			lroundf(crandom_open() * 10.0f), lroundf(200.f + (crandom_open() * 10.0f)));
	}
	// Para distancias largas o mortero, mantener la lógica original
	else
	{
		PredictAim(self, self->enemy, start, speed, true, 0, &aim, &aimpoint);
		aim += right * 0.05f;
		aim.normalize();

		if (M_CalculatePitchToFire(self, aimpoint, start, aim, speed, 2.5f, is_mortar))
			// FIX: Round all float arguments to integers
			monster_fire_grenade(self, start, aim, damage, lroundf(speed), MZ2_UNUSED_0,
				lroundf(crandom_open() * 10.0f), lroundf(frandom() * 10.f));
		else
			// FIX: Round all float arguments to integers
			monster_fire_grenade(self, start, aim, damage, lroundf(speed), MZ2_UNUSED_0,
				lroundf(crandom_open() * 10.0f), lroundf(200.f + (crandom_open() * 10.0f)));
	}

	gi.sound(self, CHAN_WEAPON, sound_grenade, 1, ATTN_NORM, 0);
}

void TankBlaster(edict_t* self)
{
	// Basic enemy check - blindfire logic needs to execute
	if (!M_HasEnemy(self))
		return;

	int damage = GetMonsterWeaponDamage(self->monsterinfo.monster_type_id, horde::WeaponID::BLASTER);
	if (damage <= 0) damage = 30;

	vec3_t forward, right;
	vec3_t dir;
	monster_muzzleflash_id_t flash_number;

	const bool blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;

	if (self->s.frame == FRAME_attak110)
		flash_number = MZ2_TANK_BLASTER_1;
	else if (self->s.frame == FRAME_attak113)
		flash_number = MZ2_TANK_BLASTER_2;
	else // (self->s.frame == FRAME_attak116)
		flash_number = MZ2_TANK_BLASTER_3;

	AngleVectors(self->s.angles, forward, right, nullptr);
	const vec3_t start = G_ProjectSource(self->s.origin, monster_flash_offset[flash_number], forward, right);

	// Base offsets
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

	// Ajustar offset según el scale
	bullet_offset = bullet_offset * self->s.scale;

	const vec3_t bullet_start = G_ProjectSource(self->s.origin, bullet_offset, forward, right);

	if (blindfire) {
		vec3_t const target = self->monsterinfo.blind_fire_target;
		if (!M_AdjustBlindfireTarget(self, start, target, right, dir))
			return;
	}
	else
	{
		// Not blindfiring - need fully valid target
		if (!M_HasValidTarget(self))
			return;
		PredictAim(self, self->enemy, start, 0, false, 0.f, &dir, nullptr);
	}

	const bool isBoss =
    (horde::IsMonsterType(self, horde::MonsterTypeID::TANK_64) && self->monsterinfo.IS_BOSS) ||
    (g_hardcoop->integer && horde::IsMonsterType(self, horde::MonsterTypeID::TANK_64));

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

		int lightning_damage = M_GET_DMG_OR(self, LIGHTNING, 15);
		fire_bullet(self, bullet_start, dir, lightning_damage, 18, 0, 0, MOD_TESLA);
	}
	else {
		monster_fire_blaster2(self, start, dir, damage,
			M_GET_SPEED_OR(self, BLASTER2, 950), flash_number, EF_BLASTER);
	}
}

void TankStrike(edict_t* self)
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
	int damage = GetMonsterWeaponDamage(self->monsterinfo.monster_type_id, horde::WeaponID::SLAM);
	if (damage <= 0) damage = self->monsterinfo.IS_BOSS ? 175 : 75;
	void T_SlamRadiusDamage(vec3_t point, edict_t * inflictor, edict_t * attacker, float damage, float kick, edict_t * ignore, float radius, mod_t mod);
	// Daño radial
	T_SlamRadiusDamage(tr.endpos, self, self, damage, 450.f, self, 185, MOD_TANK_PUNCH);
}

void TankRocket(edict_t* self)
{
	// Basic enemy check - blindfire logic needs to execute
	if (!M_HasEnemy(self))
		return;

	// Get damage and speed from global weapon system
	int damage = M_GET_DMG_OR(self, ROCKET, 50);
	int rocketSpeed;

    vec3_t                   forward, right;
    vec3_t                   start;
    vec3_t                   dir;
    vec3_t                   vec;
    monster_muzzleflash_id_t flash_number;
    vec3_t target;
    trace_t trace;

    const bool blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;
	const bool useHeat = self->spawnflags.has(SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING) || self->monsterinfo.IS_BOSS;

    // Determine flash_number based on frame
    if (self->s.frame == FRAME_attak322 || (self->s.frame == FRAME_attak324))
        flash_number = MZ2_TANK_ROCKET_1;
    else if (self->s.frame == FRAME_attak327)
        flash_number = MZ2_TANK_ROCKET_2;
    else // (self->s.frame == FRAME_attak330)
        flash_number = MZ2_TANK_ROCKET_3;

    AngleVectors(self->s.angles, forward, right, nullptr);
    start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

    // Determine rocket speed - use heat or rocket based on flags
    int config_speed = useHeat ? M_HEAT_SPEED(self) : M_ROCKET_SPEED(self);

    if (config_speed > 0)
        rocketSpeed = config_speed;
    else if (self->speed)
        rocketSpeed = lroundf(self->speed);
    else if (self->spawnflags.has(SPAWNFLAG_TANK_COMMANDER_GUARDIAN))
        rocketSpeed = 600;
    else
        rocketSpeed = 480;

    // Determine target position
    if (blindfire)
    {
        // --- Check added for blind_fire_target ---
        if (!self->monsterinfo.blind_fire_target) // Ensure target exists
             return;
        target = self->monsterinfo.blind_fire_target;
        // --- End Check ---
    }
    else
    {
        // Not blindfiring - need fully valid target
        if (!M_HasValidTarget(self))
            return;
        target = self->enemy->s.origin;
    }

    // Calculate initial direction
    if (blindfire)
    {
        vec = target;
        dir = vec - start;
    }
    // --- self->enemy is guaranteed valid here ---
    else if (frandom() < 0.66f || (start[2] < self->enemy->absmin[2]))
    {
        vec = self->enemy->s.origin;
        vec[2] += self->enemy->viewheight; // Access viewheight safely
        dir = vec - start;
    }
    else
    {
        vec = self->enemy->s.origin;
        vec[2] = self->enemy->absmin[2] + 1; // Access absmin safely
        dir = vec - start;
    }


    // Lead target if not blindfiring
    // --- self->enemy is guaranteed valid here ---
    if ((!blindfire) && ((frandom() < (0.2f + ((3 - skill->integer) * 0.15f)))))
        PredictAim(self, self->enemy, start, rocketSpeed, false, 0, &dir, &vec);


    dir.normalize(); // Normalize direction after potential prediction

    // Perform fire check (trace) and fire projectile
    // Fire rocket/heat based on flags
    if (blindfire)
    {
        if (M_AdjustBlindfireTarget(self, start, vec, right, dir))
        {
            if (useHeat)
                monster_fire_heat(self, start, dir, damage, rocketSpeed, flash_number, lroundf(self->accel));
            else {
				int speed = horde::IsMonsterType(self, horde::MonsterTypeID::TANK_COMMANDER) ?
					lroundf(rocketSpeed * 1.5f) : rocketSpeed;
                monster_fire_rocket(self, start, dir, damage, speed, flash_number);
			}
        }
    }
    else
    {
        trace = gi.traceline(start, vec, self, MASK_PROJECTILE);

        if (trace.fraction > 0.5f || trace.ent->solid != SOLID_BSP)
        {
            if (useHeat)
                monster_fire_heat(self, start, dir, damage, rocketSpeed, flash_number, lroundf(self->accel));
            else {
				int speed = horde::IsMonsterType(self, horde::MonsterTypeID::TANK_COMMANDER) ?
					lroundf(rocketSpeed * 1.5f) : rocketSpeed;
                monster_fire_rocket(self, start, dir, damage, speed, flash_number);
			}
        }
    }
}

void TankMachineGun(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return;
	}

	// Determine if using flechette (commander/boss) or machinegun
	bool useFlechette = horde::IsMonsterType(self, horde::MonsterTypeID::TANK_COMMANDER) ||
		self->spawnflags.has(SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING) ||
		self->monsterinfo.IS_BOSS;

	int damage = useFlechette ? M_GET_DMG_OR(self, FLECHETTE, 6) : M_GET_DMG_OR(self, MACHINEGUN, 6);

	// Aumenta velocidad de giro
	vec3_t dir = self->enemy->s.origin - self->s.origin;
	self->yaw_speed = 45;
	self->ideal_yaw = vectoyaw(dir);
	M_ChangeYaw(self);

	monster_muzzleflash_id_t const flash_number = static_cast<monster_muzzleflash_id_t>(MZ2_TANK_MACHINEGUN_1 + (self->s.frame - FRAME_attak406));
	vec3_t forward, right;
	AngleVectors(self->s.angles, forward, right, nullptr);
	vec3_t const start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

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

	if (useFlechette) {
		int speed = M_GET_SPEED_OR(self, FLECHETTE, self->monsterinfo.IS_BOSS ? 1150 : 700);

		monster_fire_flechette(self, start, forward, damage, speed, flash_number);

		vec3_t right_offset;
		AngleVectors(vectoangles(forward), nullptr, &right_offset, nullptr);
		vec3_t forward_right = forward + (right_offset * 0.05f);
		forward_right.normalize();
		monster_fire_flechette(self, start, forward_right, damage, speed, flash_number);
	}
	else {
		monster_fire_bullet(self, start, forward, damage, 8,
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

void commander_punch(edict_t* self);

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

mframe_t tank_frames_commander_punch[] =
{
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, commander_punch},  // FRAME_attak225 - Añadir footstep aquí
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
MMOVE_T(tank_move_commander_punch) = { FRAME_attak224, FRAME_attak235, tank_frames_commander_punch, tank_run };


void commander_punch(edict_t* self)
{
	if (!self->enemy || !self->enemy->inuse)
		return;

	//if we standing, no teleport, why? why not
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		M_SetAnimation(self, &tank_move_commander_punch);
		self->monsterinfo.attack_finished = level.time + 0.5_sec;
		return;
	}

	// Intentar teleport
	if (!TeleportNearTarget(self, self->enemy, 16.0f, true))
		return;  //if teleport fails, we exit without doing a punch

	// if we reached this part, teleport was succesful
	constexpr gtime_t TELEPORT_COOLDOWN = 3_sec;
	self->monsterinfo.spawn_cooldown = level.time + TELEPORT_COOLDOWN;
	M_SetAnimation(self, &tank_move_commander_punch);
	self->monsterinfo.attack_finished = level.time + 0.5_sec;
}

//
// animation frames
//

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


void tank_reattack_blaster(edict_t* self)
{
	if (!self || !self->enemy || !self->enemy->inuse)
	{
		M_SetAnimation(self, &tank_move_attack_post_blast);
		return;
	}

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
	if (!self || !self->enemy || !self->enemy->inuse)
	{
		M_SetAnimation(self, &tank_move_attack_post_blast);
		return;
	}

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

void tank_refire_rocket(edict_t* self)
{
	// Add a guard clause to ensure the enemy is valid before checking its state.
	// If the enemy is gone, the tank should finish its attack animation.
	if (!self || !self->enemy || !self->enemy->inuse)
	{
		M_SetAnimation(self, &tank_move_attack_post_rocket);
		return;
	}
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
	//OnEntityDeath(self);
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

MONSTERINFO_BLOCKED(tank_blocked) (edict_t* self, float dist) -> bool
{
	if (blocked_checkplat(self, dist))
		return true;

	// Check if we can jump
	if (auto const result = blocked_checkjump(self, dist); result != blocked_jump_result_t::NO_JUMP)
	{
		if (result != blocked_jump_result_t::JUMP_TURN)
		{
			// Reduced forward velocity to prevent excessive jump distance
			M_MonsterJump(self, 180.0f, 250.0f);
		}
		return true;
	}

	return false;
}

MONSTERINFO_ATTACK(tank_attack) (edict_t* self) -> void
{
	monster_done_dodge(self);

	// Use helper function for consistent enemy validation
	if (!M_HasEnemy(self))
		return;

	vec3_t vec;
	float  range{};
	float  r;
	float chance;

	// --- NEW LOGIC: Determine target type once at the top ---
	// First, resolve the true target (in case it's a laser beam)
	edict_t* target = self->enemy;
	if (horde::IsSpecialType(target, horde::SpecialEntityTypeID::LASER_BEAM))
	{
		target = target->owner;
	}

	// Now, check if the resolved target is a deployable we shouldn't use the machine gun on.
	bool is_restricted_target = false;
	if (target) {
		is_restricted_target = horde::IsSpecialType(target, horde::SpecialEntityTypeID::TESLA_MINE) ||
							   horde::IsSpecialType(target, horde::SpecialEntityTypeID::SENTRY_GUN) ||
							   horde::IsSpecialType(target, horde::SpecialEntityTypeID::FOOD_CUBE_TRAP) ||
							   horde::IsSpecialType(target, horde::SpecialEntityTypeID::LASER_EMITTER);
	}
	// --- END NEW LOGIC ---

	if (self->enemy->client && self->monsterinfo.IS_BOSS && self->s.skinnum == 2 && frandom() < 0.6f) {
		// Verificar cooldown de teleport
		if (level.time < self->monsterinfo.spawn_cooldown)
			return;

		commander_punch(self);
		M_SetAnimation(self, &tank_move_punch);
	}

	if (range_to(self, self->enemy) <= RANGE_MELEE * 2.0f)
	{
		// Ataque melee (punch)
		M_SetAnimation(self, &tank_move_punch);
		self->monsterinfo.attack_finished = level.time + 0.6_sec;
		return;
	}

	if (self->monsterinfo.attack_state == AS_BLIND)
	{
		// ... (blind fire logic remains the same)
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
			if (self->s.skinnum == 2) {
				const float attack_choice = frandom();
				if (attack_choice <= 0.7f) {
					M_SetAnimation(self, &tank_move_attack_grenade);
				}
				else {
					M_SetAnimation(self, &tank_move_attack_blast);
				}
			}
			else {
				M_SetAnimation(self, &tank_move_attack_blast);
			}
			self->monsterinfo.nextframe = FRAME_attak108;
		}

		self->monsterinfo.attack_finished = level.time + random_time(3_sec, 5_sec);
		self->pain_debounce_time = level.time + 5_sec; // no pain for a while
		return;
	}

	vec = self->enemy->s.origin - self->s.origin;
	range = vec.length();

	r = frandom();

	if (range <= 125)
	{
		// Use our pre-calculated boolean to decide if machine gun is allowed
		const bool can_machinegun = !is_restricted_target && M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_MACHINEGUN_5]);

		if (can_machinegun && r < 0.5f)
			M_SetAnimation(self, &tank_move_attack_chain);
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1])) {
			if (self->s.skinnum == 2) {
				const float attack_choice = frandom();
				if (attack_choice <= 0.7f) {
					M_SetAnimation(self, &tank_move_attack_grenade);
				}
				else {
					M_SetAnimation(self, &tank_move_attack_blast);
				}
			}
			else {
				M_SetAnimation(self, &tank_move_attack_blast);
			}
		}
	}
	else if (range <= 250)
	{
		// Use our pre-calculated boolean again
		const bool can_machinegun = !is_restricted_target && M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_MACHINEGUN_5]);

		if (can_machinegun && r < 0.25f)
			M_SetAnimation(self, &tank_move_attack_chain);
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1])) {
			if (self->s.skinnum == 2) {
				const float attack_choice = frandom();
				if (attack_choice <= 0.5f) {
					M_SetAnimation(self, &tank_move_attack_grenade);
				}
				else {
					M_SetAnimation(self, &tank_move_attack_blast);
				}
			}
			else {
				M_SetAnimation(self, &tank_move_attack_blast);
			}
		}
	}
	else
	{
		// And use it a final time for long range
		const bool can_machinegun = !is_restricted_target && M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_MACHINEGUN_5]);
		const bool can_rocket = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_ROCKET_1]);

		if (can_machinegun && r < 0.33f)
			M_SetAnimation(self, &tank_move_attack_chain);
		else if (can_rocket && r < 0.66f)
		{
			M_SetAnimation(self, &tank_move_attack_pre_rocket);
			self->pain_debounce_time = level.time + 5_sec; // no pain for a while
		}
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1])) {
			if (self->s.skinnum == 2) {
				const float attack_choice = frandom();
				if (attack_choice <= 0.5f) {
					M_SetAnimation(self, &tank_move_attack_grenade);
				}
				else {
					M_SetAnimation(self, &tank_move_attack_blast);
				}
			}
			else {
				M_SetAnimation(self, &tank_move_attack_blast);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
//                        SPAWNER TANK IMPLEMENTATION                       //
//////////////////////////////////////////////////////////////////////////////

//
// misc
//

MONSTERINFO_SIGHT(tank_vanilla_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

void tank_vanilla_footstep(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_step, 1, ATTN_NORM, 0);
}

void tank_vanilla_thud(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_thud, 1, ATTN_NORM, 0);
}

void tank_vanilla_windup(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_windup, 1, ATTN_NORM, 0);
}

MONSTERINFO_IDLE(tank_vanilla_idle) (edict_t* self) -> void
{
	gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

//
// stand
//

mframe_t tank_vanilla_frames_stand[] = {
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
MMOVE_T(tank_vanilla_move_stand) = { FRAME_stand01, FRAME_stand30, tank_vanilla_frames_stand, nullptr };

MONSTERINFO_STAND(tank_vanilla_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &tank_vanilla_move_stand);
}

//
// walk
//

void tank_vanilla_walk(edict_t* self);

mframe_t tank_vanilla_frames_walk[] = {
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 3 },
	{ ai_walk, 2 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 4 },
	{ ai_walk, 4, tank_vanilla_footstep },
	{ ai_walk, 3 },
	{ ai_walk, 5 },
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 7 },
	{ ai_walk, 7 },
	{ ai_walk, 6 },
	{ ai_walk, 6, tank_vanilla_footstep }
};
MMOVE_T(tank_vanilla_move_walk) = { FRAME_walk05, FRAME_walk20, tank_vanilla_frames_walk, nullptr };

MONSTERINFO_WALK(tank_vanilla_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &tank_vanilla_move_walk);
}

//
// run
//

void tank_vanilla_run(edict_t* self);

mframe_t tank_vanilla_frames_start_run[] = {
	{ ai_run },
	{ ai_run, 6 },
	{ ai_run, 6 },
	{ ai_run, 11, tank_vanilla_footstep }
};
MMOVE_T(tank_vanilla_move_start_run) = { FRAME_walk01, FRAME_walk04, tank_vanilla_frames_start_run, tank_vanilla_run };

mframe_t tank_vanilla_frames_run[] = {
	{ ai_run, 4 },
	{ ai_run, 5 },
	{ ai_run, 3 },
	{ ai_run, 2 },
	{ ai_run, 5 },
	{ ai_run, 5 },
	{ ai_run, 4 },
	{ ai_run, 4, tank_vanilla_footstep },
	{ ai_run, 3 },
	{ ai_run, 5 },
	{ ai_run, 4 },
	{ ai_run, 5 },
	{ ai_run, 7 },
	{ ai_run, 7 },
	{ ai_run, 6 },
	{ ai_run, 6, tank_vanilla_footstep }
};
MMOVE_T(tank_vanilla_move_run) = { FRAME_walk05, FRAME_walk20, tank_vanilla_frames_run, nullptr };

MONSTERINFO_RUN(tank_vanilla_run) (edict_t* self) -> void
{
	if (self->enemy && self->enemy->client)
		self->monsterinfo.aiflags &= ~AI_BRUTAL;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		M_SetAnimation(self, &tank_vanilla_move_stand);
		return;
	}

	if (self->monsterinfo.active_move == &tank_vanilla_move_walk ||
		self->monsterinfo.active_move == &tank_vanilla_move_start_run)
	{
		M_SetAnimation(self, &tank_vanilla_move_run);
	}
	else
	{
		M_SetAnimation(self, &tank_vanilla_move_start_run);
	}
}

//
// pain
//

mframe_t tank_vanilla_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(tank_vanilla_move_pain1) = { FRAME_pain101, FRAME_pain104, tank_vanilla_frames_pain1, tank_vanilla_run };

mframe_t tank_vanilla_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(tank_vanilla_move_pain2) = { FRAME_pain201, FRAME_pain205, tank_vanilla_frames_pain2, tank_vanilla_run };

mframe_t tank_vanilla_frames_pain3[] = {
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
	{ ai_move, 0, tank_vanilla_footstep }
};
MMOVE_T(tank_vanilla_move_pain3) = { FRAME_pain301, FRAME_pain316, tank_vanilla_frames_pain3, tank_vanilla_run };

PAIN(tank_vanilla_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
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

		// don't go into pain while attacking/spawning
		if ((self->s.frame >= FRAME_attak301) && (self->s.frame <= FRAME_attak330))
			return;
		if ((self->s.frame >= FRAME_attak101) && (self->s.frame <= FRAME_attak116))
			return;
		if ((self->s.frame >= FRAME_attak223) && (self->s.frame <= FRAME_attak231)) //spawning
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
		M_SetAnimation(self, &tank_vanilla_move_pain1);
	else if (damage <= 60)
		M_SetAnimation(self, &tank_vanilla_move_pain2);
	else
		M_SetAnimation(self, &tank_vanilla_move_pain3);
}

MONSTERINFO_SETSKIN(tank_vanilla_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

//
// attacks
//

void tank_vanillaBlaster(edict_t* self)
{
	int damage = GetMonsterWeaponDamage(self->monsterinfo.monster_type_id, horde::WeaponID::BLASTER);
	if (damage <= 0) damage = 30;

	vec3_t					 forward, right;
	vec3_t					 start;
	vec3_t					 dir;
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
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	// pmm - blindfire support
	vec3_t target;

	// PMM
	if (blindfire)
	{
		target = self->monsterinfo.blind_fire_target;

		if (!M_AdjustBlindfireTarget2(self, start, target, right, dir))
			return;
	}
	else
		PredictAim(self, self->enemy, start, 0, false, 0.f, &dir, nullptr);
	// pmm

	monster_fire_blaster(self, start, dir, damage,
		M_GET_SPEED_OR(self, BLASTER, 1230), flash_number, EF_BLASTER);
}

void tank_vanillaStrike(edict_t* self)
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
	T_SlamRadiusDamage(tr.endpos, self, self, M_GET_DMG_OR(self, SLAM, 75), 450.f, self, 165, MOD_TANK_PUNCH);

	// Check if we have slots left to spawn monsters
	if (self->monsterinfo.monster_used <= 3)
		return;
}

void tank_vanillaRocket(edict_t* self)
{
	// Basic enemy check - blindfire logic needs to execute
	if (!M_HasEnemy(self))
		return;

	bool useHeat = self->spawnflags.has(SPAWNFLAG_tank_vanilla_COMMANDER_HEAT_SEEKING);
	int damage = useHeat ? M_GET_DMG_OR(self, HEAT, 50) : M_GET_DMG_OR(self, ROCKET, 50);

	vec3_t					 forward, right;
	vec3_t					 start;
	vec3_t					 dir;
	vec3_t					 vec;
	monster_muzzleflash_id_t flash_number;
	int						 rocketSpeed;
	vec3_t target;

	bool   const blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;

	if (self->s.frame == FRAME_attak324)
		flash_number = MZ2_TANK_ROCKET_1;
	else if (self->s.frame == FRAME_attak327)
		flash_number = MZ2_TANK_ROCKET_2;
	else // (self->s.frame == FRAME_attak330)
		flash_number = MZ2_TANK_ROCKET_3;

	AngleVectors(self->s.angles, forward, right, nullptr);

	// [Paril-KEX] scale
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	// Get rocket speed from config
	int config_speed = useHeat ? M_HEAT_SPEED(self) : M_ROCKET_SPEED(self);

	if (config_speed > 0)
		rocketSpeed = config_speed;
	else if (self->speed)
		rocketSpeed = self->speed;
	else if (useHeat)
		rocketSpeed = 500;
	else
		rocketSpeed = 850;

	// PMM
	if (blindfire)
		target = self->monsterinfo.blind_fire_target;
	else
	{
		// Not blindfiring - need fully valid target
		if (!M_HasValidTarget(self))
			return;
		target = self->enemy->s.origin;
	}
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
		if (M_AdjustBlindfireTarget2(self, start, vec, right, dir))
		{
			if (self->spawnflags.has(SPAWNFLAG_tank_vanilla_COMMANDER_HEAT_SEEKING))
				monster_fire_heat(self, start, dir, damage, rocketSpeed, flash_number, self->accel);
			else
				monster_fire_rocket(self, start, dir, damage, rocketSpeed, flash_number);
		}
	}
	else
	{
		trace_t const trace = gi.traceline(start, vec, self, MASK_PROJECTILE);

		if (trace.fraction > 0.5f || trace.ent->solid != SOLID_BSP)
		{
			if (self->spawnflags.has(SPAWNFLAG_tank_vanilla_COMMANDER_HEAT_SEEKING))
				monster_fire_heat(self, start, dir, damage, rocketSpeed, flash_number, self->accel);
			else
				monster_fire_rocket(self, start, dir, damage, rocketSpeed, flash_number);
		}
	}
}

void tank_vanillaMachineGun(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	int damage = GetMonsterWeaponDamage(self->monsterinfo.monster_type_id, horde::WeaponID::MACHINEGUN);
	if (damage <= 0) damage = 6;

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

	monster_fire_blaster_bolt(self, start, forward, damage,
		M_GET_SPEED_OR(self, BLASTER_BOLT, 1150), flash_number, EF_BLUEHYPERBLASTER);
}

static void tank_vanilla_blind_check(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		vec3_t const aim = self->monsterinfo.blind_fire_target - self->s.origin;
		self->ideal_yaw = vectoyaw(aim);
	}
}

mframe_t tank_vanilla_frames_attack_blast[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, -1 },
	{ ai_charge, -2 },
	{ ai_charge, -1 },
	{ ai_charge, -1, tank_vanilla_blind_check },
	{ ai_charge },
	{ ai_charge, 0, tank_vanillaBlaster }, // 10
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, tank_vanillaBlaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, tank_vanillaBlaster } // 16
};
MMOVE_T(tank_vanilla_move_attack_blast) = { FRAME_attak101, FRAME_attak116, tank_vanilla_frames_attack_blast, tank_vanilla_reattack_blaster };

mframe_t tank_vanilla_frames_reattack_blast[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, tank_vanillaBlaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, tank_vanillaBlaster } // 16
};
MMOVE_T(tank_vanilla_move_reattack_blast) = { FRAME_attak111, FRAME_attak116, tank_vanilla_frames_reattack_blast, tank_vanilla_reattack_blaster };

mframe_t tank_vanilla_frames_attack_post_blast[] = {
	{ ai_move }, // 17
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 3 },
	{ ai_move, 2 },
	{ ai_move, -2, tank_vanilla_footstep } // 22
};
MMOVE_T(tank_vanilla_move_attack_post_blast) = { FRAME_attak117, FRAME_attak122, tank_vanilla_frames_attack_post_blast, tank_vanilla_run };

void tank_vanilla_reattack_blaster(edict_t* self)
{
	if (!self || !self->enemy || !self->enemy->inuse)
	{
		M_SetAnimation(self, &tank_vanilla_move_attack_post_blast);
		return;
	}

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		M_SetAnimation(self, &tank_vanilla_move_attack_post_blast);
		return;
	}

	if (visible(self, self->enemy))
		if (self->enemy->health > 0)
			if (frandom() <= 0.6f)
			{
				M_SetAnimation(self, &tank_vanilla_move_reattack_blast);
				return;
			}
	M_SetAnimation(self, &tank_vanilla_move_attack_post_blast);
}

void tank_vanilla_poststrike(edict_t* self)
{
	self->enemy = nullptr;
	// [Paril-KEX]
	self->monsterinfo.pausetime = HOLD_FOREVER;
	self->monsterinfo.stand(self);
}

mframe_t tank_vanilla_frames_attack_strike[] = {
	{ ai_move, 3 },
	{ ai_move, 2 },
	{ ai_move, 2 },
	{ ai_move, 1 },
	{ ai_move, 6 },
	{ ai_move, 7 },
	{ ai_move, 9, tank_vanilla_footstep },
	{ ai_move, 2 },
	{ ai_move, 1 },
	{ ai_move, 2 },
	{ ai_move, 2, tank_vanilla_footstep },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -2 },
	{ ai_move, -2 },
	{ ai_move, 0, tank_vanilla_windup },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, tank_vanillaStrike },
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
	{ ai_move, -2, tank_vanilla_footstep }
};
MMOVE_T(tank_vanilla_move_attack_strike) = { FRAME_attak201, FRAME_attak238, tank_vanilla_frames_attack_strike, tank_vanilla_poststrike };

mframe_t tank_vanilla_frames_attack_pre_rocket[] = {
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
	{ ai_charge, 7, tank_vanilla_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, // 20

	{ ai_charge, -3 }
};
MMOVE_T(tank_vanilla_move_attack_pre_rocket) = { FRAME_attak301, FRAME_attak321, tank_vanilla_frames_attack_pre_rocket, tank_vanilla_doattack_rocket };

mframe_t tank_vanilla_frames_attack_fire_rocket[] = {
	{ ai_charge, -3, tank_vanilla_blind_check }, // Loop Start	22
	{ ai_charge },
	{ ai_charge, 0, tank_vanillaRocket }, // 24
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, tank_vanillaRocket },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, -1, tank_vanillaRocket } // 30	Loop End
};
MMOVE_T(tank_vanilla_move_attack_fire_rocket) = { FRAME_attak322, FRAME_attak330, tank_vanilla_frames_attack_fire_rocket, tank_vanilla_refire_rocket };

mframe_t tank_vanilla_frames_attack_post_rocket[] = {
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
	{ ai_charge, -1, tank_vanilla_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, // 50

	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(tank_vanilla_move_attack_post_rocket) = { FRAME_attak331, FRAME_attak353, tank_vanilla_frames_attack_post_rocket, tank_vanilla_run };

mframe_t tank_vanilla_frames_attack_chain[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ nullptr, 0, tank_vanillaMachineGun },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(tank_vanilla_move_attack_chain) = { FRAME_attak401, FRAME_attak429, tank_vanilla_frames_attack_chain, tank_vanilla_run };

mframe_t tank_vanilla_frames_punch[] =
{
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, tank_vanillaStrike},  // FRAME_attak225
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
MMOVE_T(tank_vanilla_move_punch) = { FRAME_attak224, FRAME_attak235, tank_vanilla_frames_punch, tank_vanilla_run };

void tank_vanilla_refire_rocket(edict_t* self)
{
	if (!self || !self->enemy || !self->enemy->inuse)
	{
		M_SetAnimation(self, &tank_vanilla_move_attack_post_rocket);
		return;
	}

	// PMM - blindfire cleanup
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		M_SetAnimation(self, &tank_vanilla_move_attack_post_rocket);
		return;
	}
	// pmm

	if (self->enemy->health > 0)
		if (visible(self, self->enemy))
			if (frandom() <= 0.7f)
			{
				M_SetAnimation(self, &tank_vanilla_move_attack_fire_rocket);
				return;
			}
	M_SetAnimation(self, &tank_vanilla_move_attack_post_rocket);
}

void tank_vanilla_doattack_rocket(edict_t* self)
{
	M_SetAnimation(self, &tank_vanilla_move_attack_fire_rocket);
}

// Monster spawning functionality for spawner tank
void Monster_MoveSpawn(edict_t* self) {
	// FIX: Add a guard clause to ensure the target is still valid before spawning.
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	// Basic validation
	if (!self || self->health <= 0 || self->deadflag ||
		self->monsterinfo.monster_slots <= 0) {
		return;
	}

	// Check global spawner limit for horde mode
	if (g_horde->integer) {
		if (level.global_spawned_count >= level.global_spawner_limit) {
			return; // Don't spawn if we've reached the global limit
		}
	}

	int const available_slots = self->monsterinfo.monster_slots - self->monsterinfo.monster_used;
	if (available_slots <= 0)
		return;

	// Constants
	constexpr float RADIUS_MIN = 100.0f;
	constexpr float RADIUS_MAX = 175.0f;
	constexpr float HEIGHT_OFFSET = 8.0f;
	constexpr int MAX_ATTEMPTS = 8;

	// Función auxiliar para verificar si una posición es válida
	auto trySpawnPosition = [&](const vec3_t& spawn_origin, const vec3_t& spawn_angles) -> bool {
		for (float const height_test : {0.0f, HEIGHT_OFFSET, -HEIGHT_OFFSET}) {
			vec3_t test_origin = spawn_origin;
			test_origin.z += height_test;

			trace_t const trace = gi.traceline(self->s.origin, test_origin, self, MASK_SOLID);
			if (trace.fraction < 1.0f)
				continue;

			// --- MODIFIED ID-BASED LOGIC ---
			// 1. Find all possible reinforcements that fit in the available slots.
			boost::container::small_vector<uint8_t, 32> available_indices;
			for (uint8_t i = 0; i < self->monsterinfo.reinforcements.defs.size(); ++i) {
				if (self->monsterinfo.reinforcements.defs[i].strength <= available_slots) {
					// Check if precached in horde mode - filter before adding to available list
					horde::MonsterTypeID typeId = self->monsterinfo.reinforcements.defs[i].typeId;
					if (g_horde->integer && !IsMonsterTypePrecached(typeId)) {
						continue; // Skip non-precached monsters
					}
					available_indices.push_back(i);
				}
			}
			if (available_indices.empty()) {
				return false; // Can't spawn anything
			}

			// 2. Pick one randomly from the valid options.
			uint8_t chosen_index = random_element(available_indices);
			const auto& reinf_def = self->monsterinfo.reinforcements.defs[chosen_index];
			horde::MonsterTypeID typeId = reinf_def.typeId;

			const char* classname = horde::MonsterTypeRegistry::GetClassname(typeId);
			if (!classname) continue;

			// 3. Get its properties instantly from the global data array.
			vec3_t mins, maxs;
			GetPredictedScaledBounds(typeId, mins, maxs);
			// --- END MODIFIED LOGIC ---

			// Use CreateGroundMonster for proper spawning
			edict_t* monster = CreateGroundMonster(test_origin, spawn_angles, mins, maxs, typeId, 64);
			if (!monster) {
				return false;
			}

			// Set additional flags and properties
			monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
			monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_SPAWNED_COMMANDER;
			monster->monsterinfo.commander = self;
			monster->monsterinfo.bonus_flags = self->monsterinfo.bonus_flags;
			monster->owner = self;

			// Update slot count
			self->monsterinfo.monster_used += reinf_def.strength;

			if (self->enemy && self->enemy->inuse && self->enemy->health > 0) {
				monster->enemy = self->enemy;
				FoundTarget(monster);
			}

			if (g_horde->integer)
				monster->item = brandom() ? G_HordePickItem() : nullptr;
			ApplyMonsterBonusFlags(monster);

			// Visual effects
			float const size = (monster->maxs - monster->mins).length() * 0.5f;
			SpawnGrow_Spawn(test_origin, size * 2.0f, size * 0.5f);

			// Sound effect
			gi.sound(self, CHAN_WEAPON, gi.soundindex("medic_commander/monsterspawn1.wav"), 1, ATTN_NORM, 0);

			// Increment global spawn counter
			if (g_horde->integer) {
				level.global_spawned_count++;
			}

			return true;
		}
		return false;
	};

	// First attempt the predefined positions
	for (const auto& pos : tank_vanilla_reinforcement_position) {
		vec3_t const spawn_origin = self->s.origin + pos;
		vec3_t spawn_angles = self->s.angles;
		spawn_angles[YAW] = atan2f(pos.y, pos.x) * (180.0f / PIf);

		if (trySpawnPosition(spawn_origin, spawn_angles))
			return;
	}

	// If predefined positions fail, try alternative positions using pre-calculated directions
	static std::array<int, NUM_SPAWN_DIRECTIONS> direction_indices;
	static bool indices_initialized = false;

	// Initialize shuffled indices once
	if (!indices_initialized) {
		std::iota(direction_indices.begin(), direction_indices.end(), 0);
		indices_initialized = true;
	}

	// Shuffle direction order for variety
	std::shuffle(direction_indices.begin(), direction_indices.end(), mt_rand);

	for (int attempt = 0; attempt < MAX_ATTEMPTS && attempt < NUM_SPAWN_DIRECTIONS; attempt++) {
		const int direction_idx = direction_indices[attempt];
		const vec3_t& direction = spawn_directions[direction_idx];

		float const radius = frandom(RADIUS_MIN, RADIUS_MAX);
		vec3_t const offset{
			direction.x * radius,
			direction.y * radius,
			HEIGHT_OFFSET
		};

		vec3_t const spawn_origin = self->s.origin + offset;
		vec3_t spawn_angles = self->s.angles;
		// Convert direction back to angle for spawn orientation
		spawn_angles[YAW] = atan2f(direction.y, direction.x) * (180.0f / PIf);

		if (trySpawnPosition(spawn_origin, spawn_angles))
			return;
	}
}

void tank_vanilla_spawn_finished(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_spawn_commander, 1, ATTN_NONE, 0);
	tank_vanilla_run(self);
}

mframe_t tank_frames_spawn[] =
{
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, tank_vanillaStrike},  // FRAME_attak225 - Añadir footstep aquí
	{ai_charge, 0, Monster_MoveSpawn},  // FRAME_attak226 - Engendrar monstruo aquí
	{ai_charge, 0, Monster_MoveSpawn},
	{ai_charge, -2, Monster_MoveSpawn}, // FRAME_attak229
	{ai_charge, -2, Monster_MoveSpawn},  // FRAME_attak229
	{ai_charge, -2, Monster_MoveSpawn} ,  // FRAME_attak229
	{ai_charge, 0, nullptr },
	{ai_charge, -2, Monster_MoveSpawn},
	{ai_charge, 0, nullptr},
	{ai_charge, -2, Monster_MoveSpawn},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr}
};
// Actualiza la definición de tank_move_spawn para usar la nueva función
MMOVE_T(tank_move_spawn) = { FRAME_attak221, FRAME_attak238, tank_frames_spawn, tank_vanilla_spawn_finished };

// Spawner tank attack patterns
MONSTERINFO_ATTACK(tank_vanilla_attack) (edict_t* self) -> void
{
	monster_done_dodge(self);

	// Use helper function for consistent enemy validation
	if (!M_HasEnemy(self)) {
		return;
	}

	const int to_enemy = range_to(self, self->enemy);
	const float range = to_enemy;

	if (self && self->enemy && range <= RANGE_MELEE * 2)
	{
		M_SetAnimation(self, &tank_vanilla_move_punch);
		return;
	}

	constexpr float CLOSE_RANGE = 125.0f;
	constexpr float MID_RANGE = 250.0f;
	constexpr gtime_t PAIN_IMMUNITY = 5_sec;
	constexpr gtime_t SPAWN_COOLDOWN = 10_sec;

	// Handle monster spawning logic
	const bool can_spawn = M_CanSpawnMore(self);
	const bool has_clear_path = G_IsClearPath(self, CONTENTS_SOLID, self->s.origin, self->enemy->s.origin);
	const bool spawn_cooldown_ready = level.time >= self->monsterinfo.spawn_cooldown;  // Nueva verificación

	if (can_spawn && has_clear_path && spawn_cooldown_ready &&
		(visible(self, self->enemy) && infront(self, self->enemy)))
	{
		M_SetAnimation(self, &tank_move_spawn);
		self->monsterinfo.attack_finished = level.time + 0.2_sec;
		self->monsterinfo.spawn_cooldown = level.time + SPAWN_COOLDOWN;  // Actualiza el tiempo de cooldown
		return;
	}

	if (self->enemy->health <= 0) {
		self->monsterinfo.aiflags &= ~AI_BRUTAL;
		return;
	}

	// Handle blind fire state
	if (self->monsterinfo.attack_state == AS_BLIND) {
		float chance;
		if (self->monsterinfo.blind_fire_delay < 1_sec)
			chance = 1.0f;
		else if (self->monsterinfo.blind_fire_delay < 7.5_sec)
			chance = 0.4f;
		else
			chance = 0.1f;

		if (frandom() > chance || !self->monsterinfo.blind_fire_target)
			return;

		self->monsterinfo.blind_fire_delay += 5.2_sec + random_time(3_sec);

		const bool rocket_visible = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_ROCKET_1]);
		const bool blaster_visible = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]);

		if (!rocket_visible && !blaster_visible)
			return;

		const bool use_rocket = (rocket_visible && blaster_visible) ? brandom() : rocket_visible;

		self->monsterinfo.aiflags |= AI_MANUAL_STEERING;

		if (use_rocket)
			M_SetAnimation(self, &tank_vanilla_move_attack_fire_rocket);
		else {
			M_SetAnimation(self, &tank_vanilla_move_attack_blast);
			self->monsterinfo.nextframe = FRAME_attak108;
		}

		self->monsterinfo.attack_finished = level.time + random_time(3_sec, 5_sec);
		self->pain_debounce_time = level.time + PAIN_IMMUNITY;
		return;
	}

	// Normal attack selection based on range
	const float r = frandom();
	const bool can_machinegun = !horde::IsSpecialType(self->enemy, horde::SpecialEntityTypeID::TESLA_MINE) && M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_MACHINEGUN_5]);

	if (range <= CLOSE_RANGE) {
		if (can_machinegun && r < 0.5f)
			M_SetAnimation(self, &tank_vanilla_move_attack_chain);
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]))
			M_SetAnimation(self, &tank_vanilla_move_attack_blast);
	}
	else if (range <= MID_RANGE) {
		if (can_machinegun && r < 0.25f)
			M_SetAnimation(self, &tank_vanilla_move_attack_chain);
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]))
			M_SetAnimation(self, &tank_vanilla_move_attack_blast);
	}
	else {
		const bool can_rocket = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_ROCKET_1]);

		if (can_machinegun && r < 0.33f)
			M_SetAnimation(self, &tank_vanilla_move_attack_chain);
		else if (can_rocket && r < 0.66f) {
			M_SetAnimation(self, &tank_vanilla_move_attack_pre_rocket);
			self->pain_debounce_time = level.time + PAIN_IMMUNITY;
		}
		else if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]))
			M_SetAnimation(self, &tank_vanilla_move_attack_blast);
	}
}

// Death handling for the spawner tank
void tank_vanilla_dead(edict_t* self)
{
	self->mins = { -16, -16, -16 };
	self->maxs = { 16, 16, -0 };
	monster_dead(self);
}

static void tank_vanilla_shrink(edict_t* self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t tank_vanilla_frames_death1[] = {
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
	{ ai_move, -7, tank_vanilla_shrink },
	{ ai_move, -15, tank_vanilla_thud },
	{ ai_move, -5 },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(tank_vanilla_move_death) = { FRAME_death101, FRAME_death132, tank_vanilla_frames_death1, tank_vanilla_dead };

// Define a structure for finding commander-spawned monsters
struct commander_spawned_monsters_filter_t {
	edict_t* commander = nullptr; // Initialize to avoid undefined behavior

	// Add default constructor
	commander_spawned_monsters_filter_t() = default;

	// Constructor with parameter
	commander_spawned_monsters_filter_t(edict_t* _commander) : commander(_commander) {}

	inline bool operator()(edict_t* ent) const {
		// Add null check since we now allow default construction
		if (!commander)
			return false;

		return (ent->inuse &&
			ent->owner == commander &&
			(ent->monsterinfo.aiflags & AI_SPAWNED_COMMANDER));
	}
};

// Helper function just for this case
inline entity_iterable_t<commander_spawned_monsters_filter_t> find_commander_spawns(edict_t* commander, uint32_t start_index) {
	// Create a global filter that will persist during iteration
	static commander_spawned_monsters_filter_t filter;

	// Set the commander for this iteration
	filter.commander = commander;

	// Return the iterable
	return entity_iterable_t<commander_spawned_monsters_filter_t>(start_index);
}

DIE(tank_vanilla_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	// Find and kill spawned monsters more efficiently
	uint32_t monster_start_index = game.maxclients + static_cast<uint32_t>(BODY_QUEUE_SIZE) + 1;

	// Kill spawned monsters using AI_SPAWNED_COMMANDER flag filter
	int killed_count = 0;
	for (auto ent : find_commander_spawns(self, monster_start_index)) {
		ent->health = -999;
		ent->die(ent, self, self, 999, vec3_origin, mod);
		killed_count++;
	}

	// Reset used slots
	self->monsterinfo.monster_used = 0;

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

	M_SetAnimation(self, &tank_vanilla_move_death);
}

MONSTERINFO_BLOCKED(tank_vanilla_blocked) (edict_t* self, float dist) -> bool
{
	if (blocked_checkplat(self, dist))
		return true;

	// Check if we can jump
	if (auto const result = blocked_checkjump(self, dist); result != blocked_jump_result_t::NO_JUMP)
	{
		if (result != blocked_jump_result_t::JUMP_TURN)
		{
			// Reduced forward velocity to prevent excessive jump distance
			M_MonsterJump(self, 180.0f, 250.0f);
		}
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////
//                           ENTITY SPAWN FUNCTIONS                         //
//////////////////////////////////////////////////////////////////////////////

/*QUAKED monster_tank (1 .5 0) (-32 -32 -16) (32 32 72) Ambush Trigger_Spawn Sight
model="models/monsters/tank/tris.md2"
*/
void SP_monster_tank(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	// Set a default ID if one hasn't been set by a more specific spawner.
	if (self->monsterinfo.monster_type_id == MONSTER_TYPE_UNKNOWN) {
		self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::TANK);
}	if (g_horde->integer && brandom()) {
		gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_NORM, 0);
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
	sound_spawn_commander.assign("medic_commander/monsterspawn1.wav");

	gi.soundindex("tank/tnkatck1.wav");
	gi.soundindex("tank/tnkatk2a.wav");
	gi.soundindex("tank/tnkatk2b.wav");
	gi.soundindex("tank/tnkatk2c.wav");
	gi.soundindex("tank/tnkatk2d.wav");
	gi.soundindex("tank/tnkatk2e.wav");
	gi.soundindex("tank/tnkatck3.wav");


		// Power armor configuration
	if (!st.was_key_specified("power_armor_type") && M_TANK_POWER_ARMOR_TYPE != IT_NULL) {
		self->monsterinfo.power_armor_type = static_cast<item_id_t>(M_TANK_COMMANDER_POWER_ARMOR_TYPE);
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = M_TANK_ADDON_POWER_ARMOR(self);
	}

	// Regular armor configuration
	if (!st.was_key_specified("armor_type") && M_TANK_INITIAL_ARMOR > 0) {
		self->monsterinfo.armor_type = IT_ARMOR_COMBAT;
		if (!st.was_key_specified("armor_power"))
			self->monsterinfo.armor_power = M_TANK_ADDON_ARMOR(self);
	}

	// --- REFACTORED: Use the monster ID to determine stats and appearance ---
	bool is_boss = self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED;

	if (horde::IsMonsterType(self, horde::MonsterTypeID::TANK_COMMANDER))
	{
		int base_health = M_TANK_COMMANDER_INITIAL_HEALTH;
		float health_scale = 1.0f;

		if (g_horde && g_horde->integer && current_wave_level > 0) {
			self->health = GetScaledHealth(base_health, health_scale, current_wave_level, is_boss);
		} else {
			self->health = static_cast<int>(base_health * health_scale * st.health_multiplier);
		}
		self->gib_health = -225;
		self->count = 1;
		sound_pain2.assign("tank/pain.wav");
        self->s.skinnum = 2; // Set commander skin
	}
	else if (horde::IsMonsterType(self, horde::MonsterTypeID::TANK_64))
	{
		int base_health = M_TANK_64_INITIAL_HEALTH;
		float health_scale = 1.0f;
		self->accel = 0.075f;

		if (g_horde->integer && current_wave_level > 0) {
			self->health = GetScaledHealth(base_health, health_scale, current_wave_level, is_boss);

			if (is_boss) {
				if (!st.was_key_specified("armor_type") && M_TANK_INITIAL_ARMOR > 0) {
					self->monsterinfo.armor_power = M_TANK_ADDON_ARMOR(self);
				}
				if (!st.was_key_specified("power_armor_type") && M_TANK_POWER_ARMOR_TYPE != IT_NULL) {
					self->monsterinfo.power_armor_type = static_cast<item_id_t>(M_TANK_POWER_ARMOR_TYPE);
					self->monsterinfo.power_armor_power = M_TANK_ADDON_POWER_ARMOR(self);
				}
			}
		} else if (G_IsCooperative()) {
			self->health = 1000;
		} else {
			self->health = static_cast<int>(base_health * health_scale * st.health_multiplier);
		}
		self->gib_health = -250;
		if (self->monsterinfo.bonus_flags & BF_GHOSTLY)
			self->accel *= 0.1f;
	}
	else // Default case for base TANK
	{
		int base_health = M_TANK_INITIAL_HEALTH;
		float health_scale = 1.0f;

		if (g_horde && g_horde->integer && current_wave_level > 0) {
			self->health = GetScaledHealth(base_health, health_scale, current_wave_level, is_boss);
		} else {
			self->health = static_cast<int>(base_health * health_scale * st.health_multiplier);
		}
		self->gib_health = -200;
		sound_pain.assign("tank/tnkpain2.wav");
	}

	self->monsterinfo.scale = MODEL_SCALE;
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
	self->monsterinfo.blocked = tank_blocked;
	self->monsterinfo.setskin = tank_setskin;

	gi.linkentity(self);
	M_SetAnimation(self, &tank_move_stand);
	walkmonster_start(self);

	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	self->monsterinfo.blindfire = true;

	ApplyMonsterBonusFlags(self);
}

/*QUAKED monster_tank_spawner (1 .5 0) (-32 -32 -16) (32 32 72) Ambush Trigger_Spawn Sight
*/
void SP_monster_tank_spawner(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::TANK_SPAWNER);	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	if (!st.was_key_specified("monster_slots"))
		self->monsterinfo.monster_slots = MONSTER_MAX_SLOTS;
	self->monsterinfo.monster_used = 0;

	// --- MODIFIED REINFORCEMENT SETUP ---
	bool is_special_case = (current_wave_level >= 10 &&
		self->monsterinfo.bonus_flags != BF_NONE &&
		!(self->monsterinfo.bonus_flags & BF_FRIENDLY));

	if (is_special_case)
		M_SetupReinforcements(tank_vanilla_special_reinforcements_defs, self->monsterinfo.reinforcements);
	else if (current_wave_level >= 25)
		M_SetupReinforcements(tank_vanilla_insane_reinforcements_defs, self->monsterinfo.reinforcements);
	else
		M_SetupReinforcements(tank_vanilla_hard_reinforcements_defs, self->monsterinfo.reinforcements);

	self->s.modelindex = gi.modelindex("models/monsters/tank/tris.md2");
	self->mins = { -32, -32, -16 };
	self->maxs = { 32, 32, 64 };
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	// Pre-cargar modelos de gibs
	gi.modelindex("models/monsters/tank/gibs/barm.md2");
	gi.modelindex("models/monsters/tank/gibs/head.md2");
	gi.modelindex("models/monsters/tank/gibs/chest.md2");
	gi.modelindex("models/monsters/tank/gibs/foot.md2");
	gi.modelindex("models/monsters/tank/gibs/thigh.md2");

	// Asignar sonidos
	sound_thud.assign("tank/tnkdeth2.wav");
	sound_idle.assign("tank/tnkidle1.wav");
	sound_die.assign("tank/death.wav");
	sound_step.assign("tank/step.wav");
	sound_windup.assign("tank/tnkatck4.wav");
	sound_strike.assign("tank/tnkatck5.wav");
	sound_sight.assign("tank/sight1.wav");
	sound_spawn_commander.assign("medic_commander/monsterspawn1.wav"); // Asignar el nuevo sonido

	gi.soundindex("tank/tnkatck1.wav");
	gi.soundindex("tank/tnkatk2a.wav");
	gi.soundindex("tank/tnkatk2b.wav");
	gi.soundindex("tank/tnkatk2c.wav");
	gi.soundindex("tank/tnkatk2d.wav");
	gi.soundindex("tank/tnkatk2e.wav");
	gi.soundindex("tank/tnkatck3.wav");


    // --- REFACTORED ---
    // The strcmp is removed as this function only spawns one type.
	bool is_boss = self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED;
	int base_health = M_TANK_SPAWNER_INITIAL_HEALTH;
	float health_scale = 1.0f;

	if (self->spawnflags.has(SPAWNFLAG_tank_vanilla_COMMANDER_GUARDIAN))
	{
		if (!self->s.scale)
			self->s.scale = 1.5f;
		base_health = M_TANK_SPAWNER_INITIAL_HEALTH;
	}

	// Apply health scaling
	if (g_horde && g_horde->integer && current_wave_level > 0) {
		self->health = GetScaledHealth(base_health, health_scale, current_wave_level, is_boss);
	} else {
		self->health = static_cast<int>(base_health * health_scale * st.health_multiplier);
	}

	// Apply armor scaling
	if (!st.was_key_specified("armor_type") && M_TANK_SPAWNER_INITIAL_ARMOR > 0) {
		self->monsterinfo.armor_type = IT_ARMOR_COMBAT;
		if (!st.was_key_specified("armor_power"))
			self->monsterinfo.armor_power = M_TANK_SPAWNER_ADDON_ARMOR(self);
	}

	// Apply power armor scaling
	if (!st.was_key_specified("power_armor_type") && M_TANK_SPAWNER_POWER_ARMOR_TYPE != IT_NULL) {
		self->monsterinfo.power_armor_type = static_cast<item_id_t>(M_TANK_SPAWNER_POWER_ARMOR_TYPE);
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = M_TANK_SPAWNER_ADDON_POWER_ARMOR(self);
	}

	self->gib_health = -225;
	self->count = 1;
	sound_pain2.assign("tank/pain.wav");

	self->monsterinfo.scale = MODEL_SCALE;

	self->mass = 500;
	self->pain = tank_vanilla_pain;
	self->die = tank_vanilla_die;
	self->monsterinfo.stand = tank_vanilla_stand;
	self->monsterinfo.walk = tank_vanilla_walk;
	self->monsterinfo.run = tank_vanilla_run;
	self->monsterinfo.dodge = nullptr;
	self->monsterinfo.attack = tank_vanilla_attack;
	self->monsterinfo.melee = nullptr;
	self->monsterinfo.sight = tank_vanilla_sight;
	self->monsterinfo.idle = tank_vanilla_idle;
	self->monsterinfo.blocked = tank_vanilla_blocked;
	self->monsterinfo.setskin = tank_vanilla_setskin;

	gi.linkentity(self);
	M_SetAnimation(self, &tank_vanilla_move_stand);
	walkmonster_start(self);

	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	self->monsterinfo.blindfire = true;

	ApplyMonsterBonusFlags(self);
}

void Use_Boss3(edict_t* ent, edict_t* other, edict_t* activator);

THINK(Think_Tank_Stand) (edict_t* ent) -> void
{
	if (ent->s.frame == FRAME_stand30)
		ent->s.frame = FRAME_stand01;
	else
		ent->s.frame++;
	ent->nextthink = level.time + 10_hz;
}

THINK(Think_tank_vanillaStand) (edict_t* ent) -> void
{
	if (ent->s.frame == FRAME_stand30)
		ent->s.frame = FRAME_stand01;
	else
		ent->s.frame++;
	ent->nextthink = level.time + 10_hz;
}

/*QUAKED monster_tank_stand (1 .5 0) (-32 -32 0) (32 32 90)
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
	// Removed manual (mins/maxs)  scaling - monster_start() handles it automatically
	self->use = Use_Boss3;
	self->think = Think_Tank_Stand;
	self->nextthink = level.time + 10_hz;
	gi.linkentity(self);
}

/*QUAKED monster_tank_spawner_stand (1 .5 0) (-32 -32 0) (32 32 90)
*/
void SP_monster_tank_spawner_stand(edict_t* self)
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
	// Removed manual (mins/maxs)  scaling - monster_start() handles it automatically
	self->use = Use_Boss3;
	self->think = Think_tank_vanillaStand;
	self->nextthink = level.time + 10_hz;
	gi.linkentity(self);
}

// N64 tank
void SP_monster_tank_64(edict_t* self)
{
	const spawn_temp_t &st = ED_GetSpawnTemp();

    // --- EAGER INITIALIZATION ---
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::TANK_64);	brandom() ?
		self->spawnflags |= SPAWNFLAG_TANK_COMMANDER_GUARDIAN :
		self->spawnflags |= SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING;
	
    SP_monster_tank(self); // Call base spawner

	bool is_boss = self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED;
	int base_health = M_TANK_64_INITIAL_HEALTH;
	float health_scale = 1.0f;

	// Apply health scaling
	if (g_horde && g_horde->integer && current_wave_level > 0) {
		self->health = GetScaledHealth(base_health, health_scale, current_wave_level, is_boss);
	} else {
		self->health = static_cast<int>(base_health * health_scale * st.health_multiplier);
	}

	// Apply armor scaling
	if (!st.was_key_specified("armor_type") && M_TANK_64_INITIAL_ARMOR > 0) {
		self->monsterinfo.armor_type = IT_ARMOR_COMBAT;
		if (!st.was_key_specified("armor_power"))
			self->monsterinfo.armor_power = M_TANK_64_ADDON_ARMOR(self);
	}

	// Apply power armor scaling
	if (!st.was_key_specified("power_armor_type") && M_TANK_64_POWER_ARMOR_TYPE != IT_NULL) {
		self->monsterinfo.power_armor_type = static_cast<item_id_t>(M_TANK_64_POWER_ARMOR_TYPE);
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = M_TANK_64_ADDON_POWER_ARMOR(self);
	}

	self->s.skinnum = 2;

	ApplyMonsterBonusFlags(self);
}

// --- NEWLY ADDED SPAWN FUNCTION ---
/*QUAKED monster_tank_commander (1 .5 0) (-32 -32 -16) (32 32 72) Ambush Trigger_Spawn Sight Guardian HeatSeeking
 */
void SP_monster_tank_commander(edict_t* self)
{
	const spawn_temp_t &st = ED_GetSpawnTemp();

    // --- EAGER INITIALIZATION ---
    // Set the specific ID *before* calling the base spawner.
    self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::TANK_COMMANDER);    // Call the base spawner. It will now use the ID we just set to apply
    // the correct health, sounds, and skin.
    SP_monster_tank(self);

	// Power armor configuration
	if (!st.was_key_specified("power_armor_type") && M_TANK_COMMANDER_POWER_ARMOR_TYPE != IT_NULL) {
		self->monsterinfo.power_armor_type = static_cast<item_id_t>(M_TANK_COMMANDER_POWER_ARMOR_TYPE);
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = M_TANK_COMMANDER_ADDON_POWER_ARMOR(self);
	}

	// Regular armor configuration
	if (!st.was_key_specified("armor_type") && M_TANK_COMMANDER_INITIAL_ARMOR > 0) {
		self->monsterinfo.armor_type = IT_ARMOR_COMBAT;
		if (!st.was_key_specified("armor_power"))
			self->monsterinfo.armor_power = M_TANK_COMMANDER_ADDON_ARMOR(self);
	}

	int base_health = M_TANK_COMMANDER_INITIAL_HEALTH;
	if (g_horde && g_horde->integer && current_wave_level > 0) {
		bool is_boss = self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED;
		self->health = ScaleMonsterHealth(base_health, current_wave_level, is_boss);
	} else {
		self->health = base_health * st.health_multiplier;
	}
}