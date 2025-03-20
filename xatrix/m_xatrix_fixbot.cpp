// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
	fixbot.c
*/

#include "../g_local.h"
#include "m_xatrix_fixbot.h"
#include "../m_flash.h"
#include "../shared.h"

void fixbot_spawn_turret(edict_t* self);
void fixbot_spawn_check(edict_t* self);
void fixbot_start_spawn(edict_t* self);
void fixbot_prep_spawn(edict_t* self);

static edict_t* g_spawn_target = nullptr;
static vec3_t g_spawn_position = {};
static bool g_is_spawning = false;

static cached_soundindex sound_spawn;

bool infront(edict_t* self, edict_t* other);
bool FindTarget(edict_t* self);

static cached_soundindex sound_pain1;
static cached_soundindex sound_die;
static cached_soundindex sound_weld1;
static cached_soundindex sound_weld2;
static cached_soundindex sound_weld3;
static cached_soundindex sound_gun;
static cached_soundindex sound_pew;

void fixbot_run(edict_t* self);
void fixbot_attack(edict_t* self);
void fixbot_dead(edict_t* self);
void fixbot_fire_blaster(edict_t* self);
void fixbot_fire_welder(edict_t* self);

void use_scanner(edict_t* self);
void change_to_roam(edict_t* self);
void fly_vertical(edict_t* self);

void fixbot_stand(edict_t* self);

extern const mmove_t fixbot_move_forward;
extern const mmove_t fixbot_move_stand;
extern const mmove_t fixbot_move_stand2;
extern const mmove_t fixbot_move_roamgoal;

extern const mmove_t fixbot_move_weld_start;
extern const mmove_t fixbot_move_weld;
extern const mmove_t fixbot_move_weld_end;
extern const mmove_t fixbot_move_takeoff;
extern const mmove_t fixbot_move_landing;
extern const mmove_t fixbot_move_turn;
extern const mmove_t fixbot_move_spawn;
void roam_goal(edict_t* self);


constexpr const char* fixbot_reinforcements = "monster_turret 1";
constexpr int32_t fixbot_monster_slots_base = 6;


bool find_turret_spawn_position(edict_t* self, vec3_t& position, vec3_t& direction)
{
	vec3_t forward, right, up;
	vec3_t start, end;
	trace_t tr;

	// Create ray from fixbot position
	AngleVectors(self->s.angles, forward, right, up);
	start = self->s.origin;
	end = start + (forward * 1024);  // Check up to 1024 units ahead

	// Trace to find a wall
	tr = gi.traceline(start, end, self, MASK_SOLID);

	// If we hit something that's not a monster or player
	if (tr.fraction < 1.0 && !(tr.ent->svflags & SVF_MONSTER) && !tr.ent->client)
	{
		// Found a suitable wall
		direction = tr.plane.normal;
		direction.normalize();
		position = tr.endpos + (direction * 8);  // Offset from wall
		return true;
	}

	return false;
}

PRETHINK(fixbot_spawn_laser_update) (edict_t* laser) -> void
{
	// Validate input
	if (!laser || !laser->inuse)
		return;

	edict_t* self = laser->owner;

	// Owner check - critical for safety
	if (!self || !self->inuse) {
		if (laser->inuse) {
			G_FreeEdict(laser);
		}
		return;
	}

	// Start position
	vec3_t start, dir;
	AngleVectors(self->s.angles, dir, nullptr, nullptr);
	start = self->s.origin + (dir * 16);

	// If we have a spawn position, aim at it
	if (g_is_spawning && !g_spawn_position.equals(vec3_origin)) {
		dir = g_spawn_position - start;
		dir.normalize();
	}

	laser->s.origin = start;
	laser->movedir = dir;
	gi.linkentity(laser);
	dabeam_update(laser, true);
}

void fixbot_fire_spawn_laser(edict_t* self)
{
	// Safety check
	if (!self || !self->inuse)
		return;

	// Only proceed if we're in spawning mode
	if (!g_is_spawning)
		return;

	// Fire the laser beam effect with safety check
	monster_fire_dabeam(self, -1, false, fixbot_spawn_laser_update);

	// Add some particle effects at the target location for better visuals
	if (!g_spawn_position.equals(vec3_origin))
	{
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_WELDING_SPARKS);
		gi.WriteByte(5);
		gi.WritePosition(g_spawn_position);
		gi.WriteDir(vec3_origin);
		gi.WriteByte(0xe0);
		gi.multicast(g_spawn_position, MULTICAST_PVS, false);
	}
}

void fixbot_start_spawn(edict_t* self)
{
	// Create visual charge effect
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_WELDING_SPARKS);
	gi.WriteByte(15); // More sparks for better effect
	gi.WritePosition(self->s.origin);
	gi.WriteDir(vec3_origin);
	gi.WriteByte(0xe0);
	gi.multicast(self->s.origin, MULTICAST_PVS, false);

	// Add a glow effect to the fixbot while spawning
	self->s.effects |= EF_HYPERBLASTER;

	// Make sure the fixbot still aims at the spawn position
	if (g_is_spawning && !g_spawn_position.equals(vec3_origin))
	{
		vec3_t dir = g_spawn_position - self->s.origin;
		self->ideal_yaw = vectoyaw(dir);
		M_ChangeYaw(self);
	}
}


// New function to spawn a turret at a specific position
// Spawn a turret at a specific position with proper safety checks
void spawn_turret_at_position(edict_t* self, const vec3_t& position)
{
	// Validate caller
	if (!self || !self->inuse)
	{
		gi.Com_PrintFmt("spawn_turret_at_position: invalid caller\n");
		return;
	}

	vec3_t dir;
	edict_t* ent;

	// Calculate direction vector (from position to fixbot)
	dir = self->s.origin - position;
	if (dir.normalize() < 0.01f)  // Check if normalization worked
	{
		// If positions are too close, use a default direction
		dir = { 0, 0, 1 };
	}

	// Create the turret entity
	ent = G_Spawn();
	if (!ent)
	{
		gi.Com_PrintFmt("spawn_turret_at_position: failed to spawn entity\n");
		return;
	}

	// Set basic properties - CRITICAL: Initialize enemy to nullptr
	ent->enemy = nullptr;
	ent->goalentity = nullptr;
	ent->movetarget = nullptr;
	ent->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
	ent->classname = "monster_turret";
	ent->owner = self;

	// Position the turret
	ent->s.origin = position;

	// Orient the turret to face away from the wall
	ent->s.angles = vectoangles(dir);

	// Finalize the turret
	ent->monsterinfo.aiflags |= AI_SPAWNED_COMMANDER;
	ent->monsterinfo.commander = self;
	 bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// Sound and visual effects - but check sound_spawn is valid first
	if (sound_spawn)
		gi.sound(self, CHAN_AUTO, sound_spawn, 1,
			isboss
	? ATTN_NONE : ATTN_NORM, 0);

	// Visual effect for spawning
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_TELEPORT_EFFECT);
	gi.WritePosition(ent->s.origin);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	// Add to monster count
	if (self->monsterinfo.monster_slots)
		self->monsterinfo.monster_used += 1;

	// Before spawn, add some flags that might prevent instant targeting
	ent->svflags |= SVF_NOCLIENT;  // Make temporarily invisible to prevent instant targeting

	// Use ED_CallSpawn to properly initialize the turret
	ED_CallSpawn(ent);

	// Post-spawn safety checks
	if (ent->inuse)
	{
		// Make sure enemy is NULL even after initialization
		ent->enemy = nullptr;
		ent->monsterinfo.search_time = level.time + 2_sec;  // Give it time before searching
		ent->svflags &= ~SVF_NOCLIENT;  // Make the turret visible again

		// Consider adding a delay before turret becomes active
		// ent->nextthink = level.time + 1_sec;
	}
	else
	{
		gi.Com_PrintFmt("spawn_turret_at_position: turret initialization failed\n");
	}
}

void fixbot_prep_spawn(edict_t* self)
{
	// Visual effect to indicate spawning start
	gi.sound(self, CHAN_WEAPON, sound_weld1, 1, ATTN_NORM, 0);
	self->monsterinfo.aiflags |= AI_MANUAL_STEERING;

	// Find where to spawn the turret
	vec3_t spawn_pos, spawn_dir;
	if (find_turret_spawn_position(self, spawn_pos, spawn_dir))
	{
		// Store globally for the laser aiming - THIS WAS MISSING
		g_spawn_position = spawn_pos;
		g_is_spawning = true;  // THIS WAS MISSING

		// Make the fixbot look at the spawn position
		vec3_t dir = spawn_pos - self->s.origin;
		self->ideal_yaw = vectoyaw(dir);
		M_ChangeYaw(self);
	}
}

// Update the spawn check function
void fixbot_spawn_check(edict_t* self)
{
	// Safety check first
	if (!self || !self->inuse)
		return;


	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// Only spawn if we have slots available and is boss and is actually in spawning mode
	if (isboss &&
		self->monsterinfo.monster_slots &&
		self->monsterinfo.monster_slots > self->monsterinfo.monster_used &&
		g_is_spawning) {

		// Validate spawn position
		if (!g_spawn_position.equals(vec3_origin)) {
			spawn_turret_at_position(self, g_spawn_position);
		}
	}

	// Reset spawning state
	g_is_spawning = false;
	g_spawn_position = vec3_origin;

	// Clear manual steering flag
	if (self && self->inuse)
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

	// Turn off any spawn visual effects
	if (self && self->inuse)
		self->s.effects &= ~EF_HYPERBLASTER;
}

void fixbot_spawn_turret(edict_t* self)
{
	vec3_t forward, right, up;
	vec3_t start, end, dir;
	trace_t tr;
	edict_t* ent;

	// Create ray from fixbot position
	AngleVectors(self->s.angles, forward, right, up);
	start = self->s.origin;
	end = start + (forward * 1024);  // Check up to 1024 units ahead

	// Trace to find a wall
	tr = gi.traceline(start, end, self, MASK_SOLID);

	// If we hit something that's not a monster or player
	if (tr.fraction < 1.0 && !(tr.ent->svflags & SVF_MONSTER) && !tr.ent->client) {
		// Found a suitable wall, spawn a turret
		ent = G_Spawn();
		if (!ent)
			return;

		ent->classname = "monster_turret";

		// Position the turret at the impact point, slightly offset from wall
		dir = tr.plane.normal;
		dir.normalize();
		ent->s.origin = tr.endpos + (dir * 8);  // Offset from wall

		// Orient the turret to face away from the wall
		ent->s.angles = vectoangles(dir);

		// Finalize the turret
		ent->monsterinfo.aiflags |= AI_SPAWNED_COMMANDER;
		ent->monsterinfo.commander = self;

		bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

		// Initialize the turret
		gi.sound(self, CHAN_AUTO, sound_spawn, 1, isboss ? ATTN_NONE : ATTN_NORM, 0);

		// Visual effect for spawning
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_TELEPORT_EFFECT);
		gi.WritePosition(ent->s.origin);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);

		// Add to monster count
		if (self->monsterinfo.monster_slots)
			self->monsterinfo.monster_used += 1;

		// Use ED_CallSpawn to properly initialize the turret
		ED_CallSpawn(ent);
	}
}

mframe_t fixbot_frames_spawn[] = {
    { ai_move, 0, fixbot_prep_spawn },  // Find spawn position and start aiming
    { ai_move, 0, fixbot_fire_spawn_laser }, // Start the laser
    { ai_move, 0, fixbot_fire_spawn_laser },
    { ai_move, 0, fixbot_fire_spawn_laser },
    { ai_move, 0, fixbot_fire_spawn_laser }, // Continue laser for ~2 seconds
    { ai_move, 0, fixbot_fire_spawn_laser },
    { ai_move, 0, fixbot_fire_spawn_laser },
    { ai_move, 0, fixbot_fire_spawn_laser },
    { ai_move, 0, fixbot_spawn_check },   // Now spawn the turret
    { ai_move, 0 }                      // End spawn sequence
};
MMOVE_T(fixbot_move_spawn) = { FRAME_weldstart_01, FRAME_weldstart_10, fixbot_frames_spawn, fixbot_run };

// [Paril-KEX] clean up bot goals if we get interrupted
THINK(bot_goal_check) (edict_t* self) -> void
{
	if (!self->owner || !self->owner->inuse || self->owner->goalentity != self)
	{
		G_FreeEdict(self);
		return;
	}

	self->nextthink = level.time + 1_ms;
}

void ED_CallSpawn(edict_t* ent);

edict_t* fixbot_FindDeadMonster(edict_t* self)
{
	edict_t* ent = nullptr;
	edict_t* best = nullptr;

	while ((ent = findradius(ent, self->s.origin, 1024)) != nullptr)
	{
		if (ent == self)
			continue;
		if (!(ent->svflags & SVF_MONSTER))
			continue;
		if (ent->monsterinfo.aiflags & AI_GOOD_GUY)
			continue;
		// check to make sure we haven't bailed on this guy already
		if ((ent->monsterinfo.badMedic1 == self) || (ent->monsterinfo.badMedic2 == self))
			continue;
		if (ent->monsterinfo.healer)
			// FIXME - this is correcting a bug that is somewhere else
			// if the healer is a monster, and it's in medic mode .. continue .. otherwise
			//   we will override the healer, if it passes all the other tests
			if ((ent->monsterinfo.healer->inuse) && (ent->monsterinfo.healer->health > 0) &&
				(ent->monsterinfo.healer->svflags & SVF_MONSTER) && (ent->monsterinfo.healer->monsterinfo.aiflags & AI_MEDIC))
				continue;
		if (ent->health > 0)
			continue;
		if ((ent->nextthink) && (ent->think != monster_dead_think))
			continue;
		if (!visible(self, ent))
			continue;
		if (!best)
		{
			best = ent;
			continue;
		}
		if (ent->max_health <= best->max_health)
			continue;
		best = ent;
	}

	return best;
}

// Improve flying parameters
static void fixbot_set_attack_fly_parameters(edict_t* self)
{
	self->monsterinfo.fly_thrusters = false;
	self->monsterinfo.fly_acceleration = 25.f;   // Was 20, now 25
	self->monsterinfo.fly_speed = 240.f;         // Was 120, now 240
	self->monsterinfo.fly_min_distance = 250.f;  // Was 300, now 250
	self->monsterinfo.fly_max_distance = 800.f;  // Was 900, now 800
}


edict_t* fixbot_FindLiveEnemy(edict_t* self) {
	// Early validation check
	if (!self || !self->inuse)
		return nullptr;

	// Variable declaration with safe distance limits
	edict_t* ent = nullptr;
	edict_t* best = nullptr;
	float best_dist_squared = 1500 * 1500;  // Use squared distance for better performance

	// Get self origin once outside the loop
	vec3_t self_origin = self->s.origin;

	while ((ent = findradius(ent, self_origin, 1500)) != nullptr) {
		// Skip invalid entities and self
		if (!ent->inuse || ent == self)
			continue;

		// Skip dead entities
		if (ent->health <= 0 || ent->deadflag)
			continue;

		// Skip teammates and summoned units if we're summoned
		if (OnSameTeam(self, ent) || (self->monsterinfo.issummoned && ent->monsterinfo.issummoned))
			continue;

		// For performance, do expensive checks on potentially valid targets only
		if (!(ent->client || (ent->svflags & SVF_MONSTER)))
			continue;

		// Skip invisible players
		if (ent->client && ent->client->invisible_time > level.time &&
			ent->client->invisibility_fade_time <= level.time)
			continue;

		// Check distance first (cheaper than visibility check)
		float dist_squared = DistanceSquared(self_origin, ent->s.origin);
		if (dist_squared >= best_dist_squared)
			continue;

		// Visibility check - most expensive, do last
		if (!visible(self, ent))
			continue;

		// We found a better candidate
		best = ent;
		best_dist_squared = dist_squared;
	}

	return best;
}

// More aggressive search behavior
int fixbot_search(edict_t* self)
{
	edict_t* ent;
	extern void fixbot_start_attack(edict_t * self);

	// More frequently search for enemies
	if (!self->enemy || (self->enemy && self->enemy->health <= 0) ||
		(self->enemy && !visible(self, self->enemy)) ||
		(self->monsterinfo.aiflags & AI_STAND_GROUND))
	{
		ent = fixbot_FindLiveEnemy(self);
		if (ent) {
			self->enemy = ent;
			fixbot_set_attack_fly_parameters(self);
			fixbot_start_attack(self);
			return 1;  // Enemy found and attack initiated
		}
	}
	return 0;  // No enemy found or already had an enemy
}

void landing_goal(edict_t* self)
{
	trace_t	 tr;
	vec3_t	 forward, right, up;
	vec3_t	 end;
	edict_t* ent;

	ent = G_Spawn();
	ent->classname = "bot_goal";
	ent->solid = SOLID_BBOX;
	ent->owner = self;
	ent->think = bot_goal_check;
	gi.linkentity(ent);

	ent->mins = { -32, -32, -24 };
	ent->maxs = { 32, 32, 24 };

	AngleVectors(self->s.angles, forward, right, up);
	end = self->s.origin + (forward * 32);
	end = self->s.origin + (up * -8096);

	tr = gi.trace(self->s.origin, ent->mins, ent->maxs, end, self, MASK_MONSTERSOLID);

	ent->s.origin = tr.endpos;

	self->goalentity = self->enemy = ent;
	M_SetAnimation(self, &fixbot_move_landing);
}

void takeoff_goal(edict_t* self)
{
	trace_t	 tr;
	vec3_t	 forward, right, up;
	vec3_t	 end;
	edict_t* ent;

	ent = G_Spawn();
	ent->classname = "bot_goal";
	ent->solid = SOLID_BBOX;
	ent->owner = self;
	ent->think = bot_goal_check;
	gi.linkentity(ent);

	ent->mins = { -32, -32, -24 };
	ent->maxs = { 32, 32, 24 };

	AngleVectors(self->s.angles, forward, right, up);
	end = self->s.origin + (forward * 32);
	end = self->s.origin + (up * 128);

	tr = gi.trace(self->s.origin, ent->mins, ent->maxs, end, self, MASK_MONSTERSOLID);

	ent->s.origin = tr.endpos;

	self->goalentity = self->enemy = ent;
	M_SetAnimation(self, &fixbot_move_takeoff);
}

void change_to_roam(edict_t* self)
{
	if (self->enemy)
		return;

	if (fixbot_search(self))
		return;

	M_SetAnimation(self, &fixbot_move_roamgoal);

	if (self->spawnflags.has(SPAWNFLAG_FIXBOT_LANDING))
	{
		landing_goal(self);
		M_SetAnimation(self, &fixbot_move_landing);
		self->spawnflags &= ~SPAWNFLAG_FIXBOT_LANDING;
		self->spawnflags = SPAWNFLAG_FIXBOT_WORKING;
	}
	if (self->spawnflags.has(SPAWNFLAG_FIXBOT_TAKEOFF))
	{
		takeoff_goal(self);
		M_SetAnimation(self, &fixbot_move_takeoff);
		self->spawnflags &= ~SPAWNFLAG_FIXBOT_TAKEOFF;
		self->spawnflags = SPAWNFLAG_FIXBOT_WORKING;
	}
	if (self->spawnflags.has(SPAWNFLAG_FIXBOT_FIXIT))
	{
		M_SetAnimation(self, &fixbot_move_roamgoal);
		self->spawnflags &= ~SPAWNFLAG_FIXBOT_FIXIT;
		self->spawnflags = SPAWNFLAG_FIXBOT_WORKING;
	}
	if (!self->spawnflags)
	{
		M_SetAnimation(self, &fixbot_move_stand2);
	}
}

void roam_goal(edict_t* self)
{

	trace_t	 tr;
	vec3_t	 forward, right, up;
	vec3_t	 end;
	edict_t* ent;
	vec3_t	 dang;
	float	 len, oldlen;
	int		 i;
	vec3_t	 vec;
	vec3_t	 whichvec{};

	ent = G_Spawn();
	ent->classname = "bot_goal";
	ent->solid = SOLID_BBOX;
	ent->owner = self;
	ent->think = bot_goal_check;
	ent->nextthink = level.time + 1_ms;
	gi.linkentity(ent);

	oldlen = 0;

	for (i = 0; i < 12; i++)
	{

		dang = self->s.angles;

		if (i < 6)
			dang[YAW] += 30 * i;
		else
			dang[YAW] -= 30 * (i - 6);

		AngleVectors(dang, forward, right, up);
		end = self->s.origin + (forward * 8192);

		tr = gi.traceline(self->s.origin, end, self, MASK_PROJECTILE);

		vec = self->s.origin - tr.endpos;
		len = vec.normalize();

		if (len > oldlen)
		{
			oldlen = len;
			whichvec = tr.endpos;
		}
	}

	ent->s.origin = whichvec;
	self->goalentity = self->enemy = ent;

	M_SetAnimation(self, &fixbot_move_turn);
}

void use_scanner(edict_t* self)
{

	if (self->enemy)
		return;

	edict_t* ent = nullptr;

	float  radius = 1024;
	vec3_t vec;

	float len;

	while ((ent = findradius(ent, self->s.origin, radius)) != nullptr)
	{
		if (ent->health >= 100)
		{
			if (strcmp(ent->classname, "object_repair") == 0)
			{
				if (visible(self, ent))
				{
					// remove the old one
					if (strcmp(self->goalentity->classname, "bot_goal") == 0)
					{
						self->goalentity->nextthink = level.time + 100_ms;
						self->goalentity->think = G_FreeEdict;
					}

					self->goalentity = self->enemy = ent;

					vec = self->s.origin - self->goalentity->s.origin;
					len = vec.normalize();

					fixbot_set_attack_fly_parameters(self);

					if (len < 32)
					{
						M_SetAnimation(self, &fixbot_move_weld_start);
						return;
					}
					return;
				}
			}
		}
	}

	if (!self->goalentity)
	{
		M_SetAnimation(self, &fixbot_move_stand);
		return;
	}

	vec = self->s.origin - self->goalentity->s.origin;
	len = vec.length();

	if (len < 32)
	{
		if (strcmp(self->goalentity->classname, "object_repair") == 0)
		{
			M_SetAnimation(self, &fixbot_move_weld_start);
		}
		else
		{
			self->goalentity->nextthink = level.time + 100_ms;
			self->goalentity->think = G_FreeEdict;
			M_SetAnimation(self, &fixbot_move_stand);
		}
		return;
	}

	vec = self->s.origin - self->s.old_origin;
	len = vec.length();

	/*
	  bot is stuck get new goalentity
	*/
	if (len == 0)
	{
		if (strcmp(self->goalentity->classname, "object_repair") == 0)
		{
			M_SetAnimation(self, &fixbot_move_stand);
		}
		else
		{
			self->goalentity->nextthink = level.time + 100_ms;
			self->goalentity->think = G_FreeEdict;
			M_SetAnimation(self, &fixbot_move_stand);
		}
	}
}

/*
	when the bot has found a landing pad
	it will proceed to its goalentity
	just above the landing pad and
	decend translated along the z the current
	frames are at 10fps
*/
void blastoff(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int kick, int te_impact, int hspread, int vspread)
{
	trace_t	   tr;
	vec3_t	   dir;
	vec3_t	   forward, right, up;
	vec3_t	   end;
	float	   r;
	float	   u;
	vec3_t	   water_start;
	bool	   water = false;
	contents_t content_mask = MASK_PROJECTILE | MASK_WATER;

	hspread += (self->s.frame - FRAME_takeoff_01);
	vspread += (self->s.frame - FRAME_takeoff_01);

	tr = gi.traceline(self->s.origin, start, self, MASK_PROJECTILE);
	if (!(tr.fraction < 1.0f))
	{
		dir = vectoangles(aimdir);
		AngleVectors(dir, forward, right, up);

		r = crandom() * hspread;
		u = crandom() * vspread;
		end = start + (forward * 8192);
		end += (right * r);
		end += (up * u);

		if (gi.pointcontents(start) & MASK_WATER)
		{
			water = true;
			water_start = start;
			content_mask &= ~MASK_WATER;
		}

		tr = gi.traceline(start, end, self, content_mask);

		// see if we hit water
		if (tr.contents & MASK_WATER)
		{
			int color;

			water = true;
			water_start = tr.endpos;

			if (start != tr.endpos)
			{
				if (tr.contents & CONTENTS_WATER)
				{
					if (strcmp(tr.surface->name, "*brwater") == 0)
						color = SPLASH_BROWN_WATER;
					else
						color = SPLASH_BLUE_WATER;
				}
				else if (tr.contents & CONTENTS_SLIME)
					color = SPLASH_SLIME;
				else if (tr.contents & CONTENTS_LAVA)
					color = SPLASH_LAVA;
				else
					color = SPLASH_UNKNOWN;

				if (color != SPLASH_UNKNOWN)
				{
					gi.WriteByte(svc_temp_entity);
					gi.WriteByte(TE_SPLASH);
					gi.WriteByte(8);
					gi.WritePosition(tr.endpos);
					gi.WriteDir(tr.plane.normal);
					gi.WriteByte(color);
					gi.multicast(tr.endpos, MULTICAST_PVS, false);
				}

				// change bullet's course when it enters water
				dir = end - start;
				dir = vectoangles(dir);
				AngleVectors(dir, forward, right, up);
				r = crandom() * hspread * 2;
				u = crandom() * vspread * 2;
				end = water_start + (forward * 8192);
				end += (right * r);
				end += (up * u);
			}

			// re-trace ignoring water this time
			tr = gi.traceline(water_start, end, self, MASK_PROJECTILE);
		}
	}

	// send gun puff / flash
	if (!((tr.surface) && (tr.surface->flags & SURF_SKY)))
	{
		if (tr.fraction < 1.0f)
		{
			if (tr.ent->takedamage)
			{
				T_Damage(tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_BULLET, MOD_BLASTOFF);
			}
			else
			{
				if (!(tr.surface->flags & SURF_SKY))
				{
					gi.WriteByte(svc_temp_entity);
					gi.WriteByte(te_impact);
					gi.WritePosition(tr.endpos);
					gi.WriteDir(tr.plane.normal);
					gi.multicast(tr.endpos, MULTICAST_PVS, false);

					if (self->client)
						PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
				}
			}
		}
	}

	// if went through water, determine where the end and make a bubble trail
	if (water)
	{
		vec3_t pos;

		dir = tr.endpos - water_start;
		dir.normalize();
		pos = tr.endpos + (dir * -2);
		if (gi.pointcontents(pos) & MASK_WATER)
			tr.endpos = pos;
		else
			tr = gi.traceline(pos, water_start, tr.ent, MASK_WATER);

		pos = water_start + tr.endpos;
		pos *= 0.5f;

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BUBBLETRAIL);
		gi.WritePosition(water_start);
		gi.WritePosition(tr.endpos);
		gi.multicast(pos, MULTICAST_PVS, false);
	}
}

void fly_vertical(edict_t* self)
{

	int	   i;
	vec3_t v;
	vec3_t forward, right, up;
	vec3_t start;
	vec3_t tempvec;

	v = self->goalentity->s.origin - self->s.origin;
	self->ideal_yaw = vectoyaw(v);
	M_ChangeYaw(self);

	if (self->s.frame == FRAME_landing_58 || self->s.frame == FRAME_takeoff_16)
	{
		self->goalentity->nextthink = level.time + 100_ms;
		self->goalentity->think = G_FreeEdict;
		M_SetAnimation(self, &fixbot_move_stand);
		self->goalentity = self->enemy = nullptr;
	}

	// kick up some particles
	tempvec = self->s.angles;
	tempvec[PITCH] += 90;

	AngleVectors(tempvec, forward, right, up);
	start = self->s.origin;

	for (i = 0; i < 10; i++)
		blastoff(self, start, forward, 2, 1, TE_SHOTGUN, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD);

	// needs sound
}

void fly_vertical2(edict_t* self)
{
	vec3_t v;
	float  len;

	v = self->goalentity->s.origin - self->s.origin;
	len = v.length();
	self->ideal_yaw = vectoyaw(v);
	M_ChangeYaw(self);

	if (len < 32)
	{
		self->goalentity->nextthink = level.time + 100_ms;
		self->goalentity->think = G_FreeEdict;
		M_SetAnimation(self, &fixbot_move_stand);
	}

	// needs sound
}

mframe_t fixbot_frames_landing[] = {
	{ ai_move },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },

	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },

	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },

	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },

	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },

	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 }
};
MMOVE_T(fixbot_move_landing) = { FRAME_landing_01, FRAME_landing_58, fixbot_frames_landing, nullptr };

/*
	generic ambient stand
*/
mframe_t fixbot_frames_stand[] = {
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
	{ ai_move, 0, change_to_roam }

};
MMOVE_T(fixbot_move_stand) = { FRAME_ambient_01, FRAME_ambient_19, fixbot_frames_stand, nullptr };

mframe_t fixbot_frames_stand2[] = {
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
	{ ai_stand, 0, change_to_roam }
};
MMOVE_T(fixbot_move_stand2) = { FRAME_ambient_01, FRAME_ambient_19, fixbot_frames_stand2, nullptr };

#if 0
/*
	will need the pickup offset for the front pincers
	object will need to stop forward of the object
	and take the object with it ( this may require a variant of liftoff and landing )
*/
mframe_t fixbot_frames_pickup[] = {
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
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }

};
MMOVE_T(fixbot_move_pickup) = { FRAME_pickup_01, FRAME_pickup_27, fixbot_frames_pickup, nullptr };
#endif

/*
	generic frame to move bot
*/
mframe_t fixbot_frames_roamgoal[] = {
	{ ai_move, 0, roam_goal }
};
MMOVE_T(fixbot_move_roamgoal) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_roamgoal, nullptr };

void ai_facing(edict_t* self, float dist)
{
	{
		fixbot_stand(self);
		return;
	}

	vec3_t v;

	if (infront(self, self->goalentity))
		M_SetAnimation(self, &fixbot_move_forward);
	else
	{
		v = self->goalentity->s.origin - self->s.origin;
		self->ideal_yaw = vectoyaw(v);
		M_ChangeYaw(self);
	}
};

mframe_t fixbot_frames_turn[] = {
	{ ai_facing }
};
MMOVE_T(fixbot_move_turn) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_turn, nullptr };

void go_roam(edict_t* self)
{
}

/*
	takeoff
*/
mframe_t fixbot_frames_takeoff[] = {
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },

	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical }
};
MMOVE_T(fixbot_move_takeoff) = { FRAME_takeoff_01, FRAME_takeoff_16, fixbot_frames_takeoff, nullptr };

/* findout what this is */
mframe_t fixbot_frames_paina[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(fixbot_move_paina) = { FRAME_paina_01, FRAME_paina_06, fixbot_frames_paina, fixbot_run };

/* findout what this is */
mframe_t fixbot_frames_painb[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(fixbot_move_painb) = { FRAME_painb_01, FRAME_painb_08, fixbot_frames_painb, fixbot_run };

/*
	backup from pain
	call a generic painsound
	some spark effects
*/
mframe_t fixbot_frames_pain3[] = {
	{ ai_move, -1 }
};
MMOVE_T(fixbot_move_pain3) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_pain3, fixbot_run };

#if 0
/*
	bot has compleated landing
	and is now on the grownd
	( may need second land if the bot is releasing jib into jib vat )
*/
mframe_t fixbot_frames_land[] = {
	{ ai_move }
};
MMOVE_T(fixbot_move_land) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_land, nullptr };
#endif

void M_MoveToGoal(edict_t* ent, float dist);

void ai_movetogoal(edict_t* self, float dist)
{
	M_MoveToGoal(self, dist);
}
/*

*/
mframe_t fixbot_frames_forward[] = {
	{ ai_movetogoal, 5, use_scanner }
};
MMOVE_T(fixbot_move_forward) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_forward, nullptr };

/*

*/
mframe_t fixbot_frames_walk[] = {
	{ ai_walk, 5 }
};
MMOVE_T(fixbot_move_walk) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_walk, nullptr };

/*

*/
mframe_t fixbot_frames_run[] = {
	{ ai_run, 10 }
};
MMOVE_T(fixbot_move_run) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_run, nullptr };

#if 0
/*
	raf
	note to self
	they could have a timer that will cause
	the bot to explode on countdown
*/
mframe_t fixbot_frames_death1[] = {
	{ ai_move }
};
MMOVE_T(fixbot_move_death1) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_death1, fixbot_dead };

//
mframe_t fixbot_frames_backward[] = {
	{ ai_move }
};
MMOVE_T(fixbot_move_backward) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_backward, nullptr };
#endif

//
mframe_t fixbot_frames_start_attack[] = {
	{ ai_charge }
};
MMOVE_T(fixbot_move_start_attack) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_start_attack, fixbot_attack };

#if 0
/*
	TBD:
	need to get laser attack anim
	attack with the laser blast
*/
mframe_t fixbot_frames_attack1[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, -10, fixbot_fire_blaster }
};
MMOVE_T(fixbot_move_attack1) = { FRAME_shoot_01, FRAME_shoot_06, fixbot_frames_attack1, nullptr };
#endif

PRETHINK(fixbot_laser_update) (edict_t* laser) -> void
{
	edict_t* self = laser->owner;

	vec3_t start, dir;
	AngleVectors(self->s.angles, dir, nullptr, nullptr);
	start = self->s.origin + (dir * 16);

	if (self->enemy && self->health > 0)
	{
		vec3_t point;
		point = (self->enemy->absmin + self->enemy->absmax) * 0.5f;
		if (self->monsterinfo.aiflags & AI_MEDIC)
			point[0] += sinf(level.time.seconds()) * 8;
		dir = point - self->s.origin;
		dir.normalize();
	}

	laser->s.origin = start;
	laser->movedir = dir;
	gi.linkentity(laser);
	dabeam_update(laser, true);
}

void abortHeal(edict_t* self, bool gib, bool mark);

void fixbot_fire_laser(edict_t* self)
{
	//// critter dun got blown up while bein' fixed
	//if (!self->enemy || !self->enemy->inuse || self->enemy->health <= self->enemy->gib_health)
	//{
	//	M_SetAnimation(self, &fixbot_move_stand);
	//	self->monsterinfo.aiflags &= ~AI_MEDIC;
	//	return;
	//}

	//monster_fire_dabeam(self, -1, false, fixbot_laser_update);

	//if (self->enemy->health > (self->enemy->mass / 10))
	//{
	//	vec3_t maxs;
	//	self->enemy->spawnflags = SPAWNFLAG_NONE;
	//	self->enemy->monsterinfo.aiflags &= AI_STINKY;
	//	self->enemy->target = nullptr;
	//	self->enemy->targetname = nullptr;
	//	self->enemy->combattarget = nullptr;
	//	self->enemy->deathtarget = nullptr;
	//	self->enemy->healthtarget = nullptr;
	//	self->enemy->itemtarget = nullptr;
	//	self->enemy->monsterinfo.healer = self;

	//	maxs = self->enemy->maxs;
	//	maxs[2] += 48; // compensate for change when they die

	//	trace_t tr = gi.trace(self->enemy->s.origin, self->enemy->mins, maxs, self->enemy->s.origin, self->enemy, MASK_MONSTERSOLID);
	//	if (tr.startsolid || tr.allsolid)
	//	{
	//		abortHeal(self, false, true);
	//		return;
	//	}
	//	else if (tr.ent != world)
	//	{
	//		abortHeal(self, false, true);
	//		return;
	//	}
	//	else
	//	{
	//		self->enemy->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT;

	//		// backup & restore health stuff, because of multipliers
	//		int32_t old_max_health = self->enemy->max_health;
	//		item_id_t old_power_armor_type = self->enemy->monsterinfo.initial_power_armor_type;
	//		int32_t old_power_armor_power = self->enemy->monsterinfo.max_power_armor_power;
	//		int32_t old_base_health = self->enemy->monsterinfo.base_health;
	//		int32_t old_health_scaling = self->enemy->monsterinfo.health_scaling;
	//		auto reinforcements = self->enemy->monsterinfo.reinforcements;
	//		int32_t monster_slots = self->enemy->monsterinfo.monster_slots;
	//		int32_t monster_used = self->enemy->monsterinfo.monster_used;
	//		int32_t old_gib_health = self->enemy->gib_health;

	//		st = {};
	//		st.keys_specified.emplace("reinforcements");
	//		st.reinforcements = "";

	//		ED_CallSpawn(self->enemy);

	//		self->enemy->monsterinfo.reinforcements = reinforcements;
	//		self->enemy->monsterinfo.monster_slots = monster_slots;
	//		self->enemy->monsterinfo.monster_used = monster_used;

	//		self->enemy->gib_health = old_gib_health / 2;
	//		self->enemy->health = self->enemy->max_health = old_max_health;
	//		self->enemy->monsterinfo.power_armor_power = self->enemy->monsterinfo.max_power_armor_power = old_power_armor_power;
	//		self->enemy->monsterinfo.power_armor_type = self->enemy->monsterinfo.initial_power_armor_type = old_power_armor_type;
	//		self->enemy->monsterinfo.base_health = old_base_health;
	//		self->enemy->monsterinfo.health_scaling = old_health_scaling;

	//		if (self->enemy->monsterinfo.setskin)
	//			self->enemy->monsterinfo.setskin(self->enemy);

	//		if (self->enemy->think)
	//		{
	//			self->enemy->nextthink = level.time;
	//			self->enemy->think(self->enemy);
	//		}
	//		self->enemy->monsterinfo.aiflags &= ~AI_RESURRECTING;
	//		self->enemy->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT;
	//		// turn off flies
	//		self->enemy->s.effects &= ~EF_FLIES;
	//		self->enemy->monsterinfo.healer = nullptr;

	//		// clean up target, if we have one and it's legit
	//		if (self->enemy && self->enemy->inuse)
	//		{
	//			cleanupHealTarget(self->enemy);

	//			if ((self->oldenemy) && (self->oldenemy->inuse) && (self->oldenemy->health > 0))
	//			{
	//				self->enemy->enemy = self->oldenemy;
	//				FoundTarget(self->enemy);
	//			}
	//			else
	//			{
	//				self->enemy->enemy = nullptr;
	//				if (!FindTarget(self->enemy))
	//				{
	//					// no valid enemy, so stop acting
	//					self->enemy->monsterinfo.pausetime = HOLD_FOREVER;
	//					self->enemy->monsterinfo.stand(self->enemy);
	//				}
	//				self->enemy = nullptr;
	//				self->oldenemy = nullptr;
	//				if (!FindTarget(self))
	//				{
	//					// no valid enemy, so stop acting
	//					self->monsterinfo.pausetime = HOLD_FOREVER;
	//					self->monsterinfo.stand(self);
	//					return;
	//				}
	//			}
	//		}
	//	}

	//	M_SetAnimation(self, &fixbot_move_stand);
	//}
	//else
	//	self->enemy->monsterinfo.aiflags |= AI_RESURRECTING;
}

mframe_t fixbot_frames_laserattack[] = {
	{ ai_charge, 0, fixbot_fire_laser },
	{ ai_charge, 0, fixbot_fire_laser },
	{ ai_charge, 0, fixbot_fire_laser },
	{ ai_charge, 0, fixbot_fire_laser },
	{ ai_charge, 0, fixbot_fire_laser },
	{ ai_charge, 0, fixbot_fire_laser }
};
MMOVE_T(fixbot_move_laserattack) = { FRAME_shoot_01, FRAME_shoot_06, fixbot_frames_laserattack, nullptr };

/*
	need to get forward translation data
	for the charge attack
*/

static inline vec3_t heat_fixbot_get_dist_vec(const edict_t* heat, const edict_t* target, float dist_to_target)
{
	return (((target->s.origin + vec3_t{ 0.f, 0.f, target->mins.z }) + (target->velocity * (clamp(dist_to_target / 500.f, 0.f, 1.f)) * 0.5f)) - heat->s.origin).normalized();
}

THINK(heat_fixbot_think) (edict_t* self) -> void
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

			if (acquire->health <= 0 ||
				!visible(self, acquire))
			{
				self->enemy = acquire = nullptr;
			}
			else 
			{
				float const dist_to_target = (self->s.origin - acquire->s.origin).normalize();
				self->pos1 = heat_fixbot_get_dist_vec(self, acquire, dist_to_target);
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
				vec3_t vec = heat_fixbot_get_dist_vec(self, target, dist_to_target);

				float const len = vec.normalize();
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
			gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/railgr1a.wav"), 1.f, 0.25f, 0);
			self->enemy = acquire;
		}
	}
	else
		self->enemy = nullptr;

	float t = self->accel;

	if (self->enemy)
		t *= 0.85f;

	self->movedir = slerp(self->movedir, preferred_dir, t).normalized();
	self->s.angles = vectoangles(self->movedir);

	if (self->speed < self->yaw_speed)
	{
		self->speed += self->yaw_speed * gi.frame_time_s;
	}

	self->velocity = self->movedir * self->speed;
	self->nextthink = level.time + FRAME_TIME_MS;
}

DIE(fixbot_heat_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	BecomeExplosion1(self);
}
void plasma_touch(edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self);

// RAFAEL
void fire_fixbot_heat(edict_t* self, const vec3_t& start, const vec3_t& dir, const vec3_t& rest_dir, int damage, int speed, float damage_radius, int radius_damage, float turn_fraction)
{
	edict_t* heat;

	heat = G_Spawn();
	heat->s.origin = start;
	heat->movedir = dir;
	heat->s.angles = vectoangles(dir);
	heat->velocity = dir * speed;
	heat->movetype = MOVETYPE_FLYMISSILE;
	heat->clipmask = MASK_PROJECTILE;
	heat->flags |= FL_DAMAGEABLE;
	heat->solid = SOLID_BBOX;
	heat->s.effects |= EF_PLASMA | EF_ANIM_ALLFAST;
	heat->s.modelindex = gi.modelindex("sprites/s_photon.sp2");
	heat->s.scale = 0.75f;
	heat->owner = self;
	heat->touch = plasma_touch;
	heat->speed = speed / 1.45;
	heat->yaw_speed = speed * 2.4;
	heat->accel = turn_fraction;
	heat->pos1 = rest_dir;
	heat->mins = { -5, -5, -5 };
	heat->maxs = { 5, 5, 5 };
	heat->health = 50;
	heat->takedamage = true;
	heat->die = fixbot_heat_die;

	heat->nextthink = level.time + 0.20_sec;
	heat->think = heat_fixbot_think;

	heat->dmg = damage;
	heat->radius_dmg = radius_damage;
	heat->dmg_radius = damage_radius;
	heat->s.sound = gi.soundindex("weapons/rockfly.wav");

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	if (visible(heat, self->enemy))
	{
		heat->oldenemy = self->enemy;
		heat->timestamp = level.time + (isboss ? 0.3_sec : 0.6_sec);
		gi.sound(heat, CHAN_WEAPON, gi.soundindex("weapons/railgr1a.wav"), 1.f, 0.25f, 0);
	}

	gi.linkentity(heat);
}

// Psx Guardian heat attack, but using plasmas

static void fixbot_fire_plasma(edict_t* self, float offset)
{
	vec3_t forward, right, up;
	vec3_t start;

	AngleVectors(self->s.angles, forward, right, up);

	// Move the starting position further away from the fixbot to prevent collision
	start = self->s.origin;
	start += forward * 25.0f;  // Move forward away from fixbot's body (was -8.0f)
	start += right * offset;
	start += up * 50.f;

	// Base direction - 45 degrees up
	vec3_t dir;
	dir = forward + (up * 0.5f);
	dir.normalize();

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// Base parameters
	float speed = isboss ? irandom(300, 450) : irandom(200, 350);
	float turn_fraction = isboss ? 0.18f : 0.12f;

	if (isboss) {
	// Increase separation between projectiles to avoid collision
		vec3_t start1 = start + (right * 25.0f) + (up * 15.0f);  // up-right
		vec3_t start2 = start;                                   // center 
		vec3_t start3 = start - (right * 25.0f) + (up * 15.0f);  // up-left
		vec3_t start4 = start + (right * 30.0f) - (up * 15.0f);  // down-right
		vec3_t start5 = start - (right * 30.0f) - (up * 15.0f);  // down-left

		// Spread direction vectors
		vec3_t dir1 = dir + (right * 0.1f) + (up * 0.05f);
		dir1.normalize();

		vec3_t dir2 = dir;  // Center direction unchanged

		vec3_t dir3 = dir - (right * 0.1f) + (up * 0.05f);
		dir3.normalize();

		vec3_t dir4 = dir + (right * 0.15f) - (up * 0.05f);
		dir4.normalize();

		vec3_t dir5 = dir - (right * 0.15f) - (up * 0.05f);
		dir5.normalize();

		// Fire all five plasma projectiles from different positions
		// IMPORTANT: Add self as "ignore entity" parameter to heat fire function
		fire_fixbot_heat(self, start1, dir1, forward, 20, speed, 150, 35, turn_fraction);
		fire_fixbot_heat(self, start2, dir2, forward, 20, speed, 150, 35, turn_fraction);
		fire_fixbot_heat(self, start3, dir3, forward, 20, speed, 150, 35, turn_fraction);
		fire_fixbot_heat(self, start4, dir4, forward, 20, speed, 150, 35, turn_fraction);
		fire_fixbot_heat(self, start5, dir5, forward, 20, speed, 150, 35, turn_fraction);
	}
	else {
		// Regular fixbot - fire single plasma
		fire_fixbot_heat(self, start, dir, forward, 20, speed, 150, 35, turn_fraction);
	}

	// Play sound once regardless of how many projectiles we fired
	gi.sound(self, CHAN_WEAPON, sound_pew, 1.f, 0.5f, 0.0f);
}

void fixbot_reattack(edict_t* self)
{
	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// if our enemy is still valid, then continue firing
	if (self->enemy && !level.intermissiontime && !self->enemy->deadflag) {
		// Boss has higher chance to continue attack
		float reattack_chance = isboss ? 0.9f : 0.8f;

		if (frandom() < reattack_chance) {
			fixbot_fire_plasma(self, 8.0f);
			self->monsterinfo.nextframe = FRAME_charging_27;
			return;
		}
	}

	// end attack
	self->monsterinfo.attack_finished = level.time + (isboss ? 0.5_sec : 1.0_sec);
}

mframe_t fixbot_frames_attack2[] = {
	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge },
	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge },
	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge },

	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge, -10 },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge, -10 },
	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge },
	{ ai_charge, -10 },
	{ ai_charge, 0, fixbot_fire_blaster },

	{ ai_charge },
	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge },
	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge, 0, fixbot_fire_blaster },

	{ ai_charge, 0, fixbot_reattack }
};
MMOVE_T(fixbot_move_attack2) = { FRAME_charging_01, FRAME_charging_31, fixbot_frames_attack2, fixbot_run };

void weldstate(edict_t* self)
{
	if (self->s.frame == FRAME_weldstart_10)
		M_SetAnimation(self, &fixbot_move_weld);
	else if (self->goalentity && self->s.frame == FRAME_weldmiddle_07)
	{
		if (self->goalentity->health <= 0)
		{
			self->enemy->owner = nullptr;
			M_SetAnimation(self, &fixbot_move_weld_end);
		}
		else
			self->goalentity->health -= 10;
	}
	else
	{
		self->goalentity = self->enemy = nullptr;
		M_SetAnimation(self, &fixbot_move_stand);
	}
}

void ai_move2(edict_t* self, float dist)
{
	if (!self->goalentity)
	{
		fixbot_stand(self);
		return;
	}

	vec3_t v;

	M_walkmove(self, self->s.angles[YAW], dist);

	v = self->goalentity->s.origin - self->s.origin;
	self->ideal_yaw = vectoyaw(v);
	M_ChangeYaw(self);
};

mframe_t fixbot_frames_weld_start[] = {
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0, weldstate }
};
MMOVE_T(fixbot_move_weld_start) = { FRAME_weldstart_01, FRAME_weldstart_10, fixbot_frames_weld_start, nullptr };

mframe_t fixbot_frames_weld[] = {
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, weldstate }
};
MMOVE_T(fixbot_move_weld) = { FRAME_weldmiddle_01, FRAME_weldmiddle_07, fixbot_frames_weld, nullptr };

mframe_t fixbot_frames_weld_end[] = {
	{ ai_move2, -2 },
	{ ai_move2, -2 },
	{ ai_move2, -2 },
	{ ai_move2, -2 },
	{ ai_move2, -2 },
	{ ai_move2, -2 },
	{ ai_move2, -2, weldstate }
};
MMOVE_T(fixbot_move_weld_end) = { FRAME_weldend_01, FRAME_weldend_07, fixbot_frames_weld_end, nullptr };

void fixbot_fire_welder(edict_t* self)
{
	vec3_t start;
	vec3_t forward, right, up;
	vec3_t end;
	vec3_t dir;
	vec3_t vec;
	float  r;

	if (!self->enemy)
		return;

	vec[0] = 24.0;
	vec[1] = -0.8f;
	vec[2] = -10.0;

	AngleVectors(self->s.angles, forward, right, up);
	start = M_ProjectFlashSource(self, vec, forward, right);

	end = self->enemy->s.origin;

	dir = end - start;

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_WELDING_SPARKS);
	gi.WriteByte(10);
	gi.WritePosition(start);
	gi.WriteDir(vec3_origin);
	gi.WriteByte(irandom(0xe0, 0xe8));
	gi.multicast(self->s.origin, MULTICAST_PVS, false);

	if (frandom() > 0.8f)
	{
		r = frandom();

		if (r < 0.33f)
			gi.sound(self, CHAN_VOICE, sound_weld1, 1, ATTN_IDLE, 0);
		else if (r < 0.66f)
			gi.sound(self, CHAN_VOICE, sound_weld2, 1, ATTN_IDLE, 0);
		else
			gi.sound(self, CHAN_VOICE, sound_weld3, 1, ATTN_IDLE, 0);
	}
}

void fixbot_fire_blaster(edict_t* self)
{
	vec3_t start;
	vec3_t forward, right, up;
	vec3_t end;
	vec3_t dir;

	if (!visible(self, self->enemy))
	{
		M_SetAnimation(self, &fixbot_move_run);
	}

	AngleVectors(self->s.angles, forward, right, up);
	start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_HOVER_BLASTER_1], forward, right);

	end = self->enemy->s.origin;
	end[2] += self->enemy->viewheight;
	dir = end - start;
	dir.normalize();

	monster_fire_blaster_bolt(self, start, dir, 7, 1000, MZ2_HOVER_BLASTER_1, EF_NONE, 0);
	// save for aiming the shot
	self->pos1 = self->enemy->s.origin;
	self->pos1[2] += self->enemy->viewheight;
}

MONSTERINFO_STAND(fixbot_stand) (edict_t* self) -> void
{
	if (self->enemy && self->enemy->health > 0 && visible(self, self->enemy) && !OnSameTeam(self, self->enemy)) {
		// If finds a valid enemy, start attack
		self->monsterinfo.run(self);
	}
	else {
		// Clear invalid enemy
		self->enemy = nullptr;
		M_SetAnimation(self, &fixbot_move_stand);
	}
}

MONSTERINFO_RUN(fixbot_run) (edict_t* self) -> void
{
	// Always set the run animation, don't conditionally check for enemy
	M_SetAnimation(self, &fixbot_move_run);

	// Still check for enemies, but don't make animation dependent on it
	if (!self->enemy || !visible(self, self->enemy)) {
		fixbot_search(self);
	}
}

MONSTERINFO_WALK(fixbot_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &fixbot_move_walk);
}

void fixbot_start_attack(edict_t* self)
{
	if (self->enemy) { 
		M_SetAnimation(self, &fixbot_move_start_attack);
	}
	else {
		self->enemy = nullptr; // Si el enemigo no es un jugador, cancelar el ataque
	}
}

MONSTERINFO_ATTACK(fixbot_attack) (edict_t* self) -> void
{
	// Add strafing behavior similar to hover
	if (frandom() > 0.4f) // 60% chance to strafe
	{
		// Start strafing movement
		if (frandom() <= 0.5f) // Randomly choose direction
			self->monsterinfo.lefty = !self->monsterinfo.lefty;

		// Set our attack state for reference
		self->monsterinfo.attack_state = AS_SLIDING;
	}
	else
	{
		// Standard forward attack
		self->monsterinfo.attack_state = AS_STRAIGHT;
	}

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// If this is a boss, sometimes choose to spawn turrets
	if (isboss && frandom() < 0.45f && M_SlotsLeft(self) > 0) {
		M_SetAnimation(self, &fixbot_move_spawn);
		return;
	}

	// Otherwise, go to attack2 which has the plasma firing
	M_SetAnimation(self, &fixbot_move_attack2);
}



PAIN(fixbot_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (level.time < self->pain_debounce_time)
		return;

	fixbot_set_attack_fly_parameters(self);
	self->pain_debounce_time = level.time + 3_sec;
	gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);

	if (damage <= 10)
		M_SetAnimation(self, &fixbot_move_pain3);
	else if (damage <= 25)
		M_SetAnimation(self, &fixbot_move_painb);
	else
		M_SetAnimation(self, &fixbot_move_paina);

	//abortHeal(self, false, false);
}

void fixbot_dead(edict_t* self)
{
	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, -8 };
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0_ms;
	gi.linkentity(self);
}

DIE(fixbot_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	ThrowGibs(self, damage, {

	{ 1, "models/objects/gibs/sm_metal/tris.md2", GIB_ACID },
	{ 1, "models/objects/gibs/gear/tris.md2", GIB_METALLIC },
		});
	gi.sound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	BecomeExplosion1(self);
	// shards
}

/*QUAKED monster_fixbot (1 .5 0) (-32 -32 -24) (32 32 24) Ambush Trigger_Spawn Fixit Takeoff Landing
 */
void SP_monster_fixbot(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	sound_pain1.assign("daedalus/daedpain1.wav");
	sound_die.assign("daedalus/daeddeth1.wav");
	sound_pew.assign("makron/blaster.wav");
	sound_weld1.assign("misc/welder1.wav");
	sound_weld2.assign("misc/welder2.wav");
	sound_weld3.assign("misc/welder3.wav");

	self->s.modelindex = gi.modelindex("models/monsters/fixbot/tris.md2");

	self->mins = { -16, -16, -12 };
	self->maxs = { 16, 16, 12 };

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	self->health = 225 * st.health_multiplier;
	self->mass = 150;
	self->s.scale = 1.4f;

	self->pain = fixbot_pain;
	self->die = fixbot_die;

	self->monsterinfo.stand = fixbot_stand;
	self->monsterinfo.walk = fixbot_walk;
	self->monsterinfo.run = fixbot_run;
	self->monsterinfo.attack = fixbot_attack;

	flymonster_start(self);

	gi.linkentity(self);

	M_SetAnimation(self, &fixbot_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;


	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	fixbot_set_attack_fly_parameters(self);

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// Setup reinforcement system if it's a boss
	if (isboss && !st.was_key_specified("monster_slots")) {
		self->monsterinfo.monster_slots = fixbot_monster_slots_base;
	}
		if (skill->integer)
			self->monsterinfo.monster_slots += floor(self->monsterinfo.monster_slots * (skill->value / 2.f));

		if (!st.was_key_specified("reinforcements"))
			M_SetupReinforcements(fixbot_reinforcements, self->monsterinfo.reinforcements);

	ApplyMonsterBonusFlags(self);
}

void SP_monster_fixbotkl(edict_t* self) {
	SP_monster_fixbot(self);

	self->max_health = 7500;
	self->health = self->max_health;
	self->s.scale = 3.4f;
	self->mins *= 3.4f;
	self->maxs *= 3.4f;

}
