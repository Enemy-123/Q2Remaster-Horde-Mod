// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
	fixbot.c
*/

// Debug flag - set to 1 to spawn turrets at player position
#define DEBUG_SPAWN_AT_PLAYER 0

#include "../g_local.h"
#include "m_xatrix_fixbot.h"
#include "../m_flash.h"
#include "../shared.h"

void fixbot_spawn_turret(edict_t* self);
void fixbot_spawn_check(edict_t* self);
void fixbot_start_spawn(edict_t* self);
void fixbot_prep_spawn(edict_t* self);

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

// This makes the fixbot gradually turn to face the spawn position over multiple frames
void fixbot_face_position(edict_t* self, const vec3_t& target_pos)
{
	// Calculate desired direction
	vec3_t dir = target_pos - self->s.origin;
	float desired_yaw = vectoyaw(dir);

	// Set the ideal yaw for the fixbot to turn toward
	self->ideal_yaw = desired_yaw;

	// Calculate the difference between current and desired yaw
	float delta = self->s.angles[YAW] - self->ideal_yaw;
	if (delta > 180)
		delta -= 360;
	if (delta < -180)
		delta += 360;

	// For large turns, apply a smooth transition (max 10 degrees per frame)
	float turn_factor = 10.0f;
	if (fabs(delta) > turn_factor) {
		self->s.angles[YAW] -= (delta > 0) ? turn_factor : -turn_factor;
	}
	else {
		// For small adjustments, smoothly interpolate
		self->s.angles[YAW] = self->ideal_yaw;
	}

	// Normalize the angle
	if (self->s.angles[YAW] > 360)
		self->s.angles[YAW] -= 360;
	if (self->s.angles[YAW] < 0)
		self->s.angles[YAW] += 360;
}

bool find_turret_spawn_position(edict_t* self, vec3_t& position, vec3_t& direction, int attempt = 0)
{
	vec3_t forward, right, up;
	vec3_t start, end;
	trace_t tr;
	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// Non-static structure for tracking best position
	struct {
		vec3_t pos;
		vec3_t dir;
		float distance;
		bool valid;
		bool in_front;
	} best_position{};

	// Reset on first attempt
	if (attempt == 0) {
		best_position.valid = false;
		best_position.distance = 0;
		best_position.in_front = false;
	}

	// Validate self
	if (!self || !self->inuse) {
		position = vec3_origin;
		direction = vec3_origin;
		return false;
	}

	// Create ray from fixbot position
	AngleVectors(self->s.angles, forward, right, up);
	start = self->s.origin;

	// If this is a retry attempt, try different directions
	if (attempt > 0) {
		// Create more varied search directions
		vec3_t angles = vectoangles(forward);

		// First 4 attempts use angles in front of the fixbot
		if (attempt <= 4) {
			// First attempts prioritize forward positions with small variations
			angles[YAW] += (attempt - 1) * 30.0f - 45.0f; // -45, -15, +15, +45 degrees
			angles[PITCH] = -15.0f; // Slight downward angle
		}
		else {
			// Later attempts use full circular search
			angles[YAW] += (attempt - 4) * 45.0f; // Try positions all around
			angles[PITCH] += (frandom() - 0.5f) * 20.0f - 15.0f; // Various pitches, mostly down
		}

		AngleVectors(angles, forward, nullptr, nullptr);
	}

	// Use longer trace for farther positions
	float trace_distance = isboss ? 1000.0f : 700.0f;
	end = start + (forward * trace_distance);

	// Trace against world but exclude monsters and players
	tr = gi.traceline(start, end, self, MASK_SOLID & ~(CONTENTS_MONSTER | CONTENTS_PLAYER));

	if (tr.fraction < 1.0) {
		// We hit something solid (hopefully a wall)

		// Check if the hit entity is valid for spawning (not a player or monster)
		if (!(tr.ent->svflags & SVF_MONSTER) && !tr.ent->client) {
			// Calculate the normal for the surface we hit
			direction = tr.plane.normal;

			// Make sure direction is not perfectly vertical
			if (fabs(direction[2]) > 0.85f) {
				direction[2] = 0.6f;
				direction.normalize();
			}

			// Position a bit off the wall/floor for better placement
			position = tr.endpos + (tr.plane.normal * 16.0f);

			// Check if there's enough space for the turret
			vec3_t mins = { -16, -16, -24 };
			vec3_t maxs = { 16, 16, 24 };

			// First use CheckSpawnPoint for basic validation
			if (CheckSpawnPoint(position, mins, maxs)) {
				// Check for player proximity 
				bool player_too_close = false;
				float min_player_dist = isboss ? 96.0f : 128.0f;

				// Use the iterable for player checks
				for (auto player : active_players_no_spect()) {
					// Check current position
					float dist = (position - player->s.origin).length();
					if (dist < min_player_dist) {
						player_too_close = true;
						break;
					}

					// Check predicted future position based on velocity
					vec3_t predicted_pos = player->s.origin + (player->velocity * 0.5f);
					float predicted_dist = (position - predicted_pos).length();
					if (predicted_dist < min_player_dist) {
						player_too_close = true;
						break;
					}
				}

				if (player_too_close) {
					return false;
				}

				// Then do more rigorous checks for entity overlap
				bool entity_overlap = false;
				edict_t* ent = nullptr;
				while ((ent = findradius(ent, position, 48.0f)) != nullptr) {
					if (ent == self) continue;

					vec3_t ent_mins = position + mins;
					vec3_t ent_maxs = position + maxs;

					if (EntitiesOverlap(ent, ent_mins, ent_maxs)) {
						entity_overlap = true;
						break;
					}
				}

				if (!entity_overlap) {
					// Safely compute vector from self to position
					vec3_t to_pos = position - self->s.origin;
					if (to_pos.length() > 0.1f) {
						to_pos.normalize();
						bool pos_in_front = (to_pos.dot(forward) > 0.0f);

						// Check clear path back to fixbot
						bool clear_path = G_IsClearPath(self, MASK_SOLID, self->s.origin, position);

						if (clear_path) {
							// Calculate distance from fixbot to determine if this is the best position
							float dist = (position - self->s.origin).length();

							// Prefer positions in front with good distance
							bool better_position = false;

							if (!best_position.valid) {
								better_position = true;
							}
							else if (pos_in_front && !best_position.in_front) {
								better_position = true;
							}
							else if ((pos_in_front == best_position.in_front) && dist > best_position.distance) {
								better_position = true;
							}

							if (better_position) {
								best_position.pos = position;
								best_position.dir = direction;
								best_position.distance = dist;
								best_position.valid = true;
								best_position.in_front = pos_in_front;
							}

							// If this is an excellent position in front, use it immediately
							if (pos_in_front && dist > trace_distance * 0.5f) {
								// Final player proximity check
								if (!player_too_close) {
									// Pre-clear the area
									PushEntitiesAway(position, 1, 80.0f, 100.0f, 100.0f, 50.0f);
									return true;
								}
							}
						}
					}
				}
			}
		}
	}

	// Try more positions if we haven't found a good one yet
	if (attempt < 12) {
		return find_turret_spawn_position(self, position, direction, attempt + 1);
	}

	// If we've tried all attempts and found at least one valid position, use the best one
	if (best_position.valid) {
		position = best_position.pos;
		direction = best_position.dir;

		// Final check for player proximity
		bool player_too_close = false;
		float min_player_dist = isboss ? 96.0f : 128.0f;

		for (auto player : active_players_no_spect()) {
			float dist = (position - player->s.origin).length();
			if (dist < min_player_dist) {
				player_too_close = true;
				break;
			}
		}

		if (!player_too_close) {
			// Pre-clear the area
			PushEntitiesAway(position, 1, 80.0f, 100.0f, 100.0f, 50.0f);
			return true;
		}
	}

	// Last resort fallback
	position = self->s.origin + (forward * (isboss ? 350.0f : 250.0f));
	direction = forward;
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
	if ((self->monsterinfo.aiflags & AI_MANUAL_STEERING) &&
		!self->monsterinfo.blind_fire_target.equals(vec3_origin)) {

		// Get direction vector to spawn position
		vec3_t spawn_dir = self->monsterinfo.blind_fire_target - start;
		if (spawn_dir.length() > 0.1f) {
			spawn_dir.normalize();
			dir = spawn_dir;
		}

		// Adjust laser color based on whether it's a boss
		bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);
		if (isboss) {
			// Boss gets a more intense beam
			laser->s.skinnum = 0xf0f0f0f0; // Brighter, more intense beam
		}
	}

	laser->s.origin = start;
	laser->movedir = dir;
	gi.linkentity(laser);
	// Restore original call
	dabeam_update(laser, true);

	// Add particle effects at the target spawn position
	if ((self->monsterinfo.aiflags & AI_MANUAL_STEERING) &&
		!self->monsterinfo.blind_fire_target.equals(vec3_origin))
	{
		// Enhanced particle effects at the target location
		bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);
		int particle_chance = isboss ? 5 : 7; // More frequent effects for boss

		// Occasional sparks at the target location
		if (frandom() * 10 > particle_chance) {
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_WELDING_SPARKS);
			gi.WriteByte(isboss ? 10 : 5); // More particles for boss
			gi.WritePosition(self->monsterinfo.blind_fire_target);
			gi.WriteDir(vec3_origin);
			gi.WriteByte(0xe0);
			gi.multicast(self->monsterinfo.blind_fire_target, MULTICAST_PVS, false);
		}
	}
}

void fixbot_fire_spawn_laser(edict_t* self)
{
	// Safety check
	if (!self || !self->inuse)
		return;

	// Only proceed if we're in spawning mode
	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
		return;

	// Continue turning smoothly toward the spawn position
	if (!self->monsterinfo.blind_fire_target.equals(vec3_origin)) {
		// Calculate direction to target - safely
		vec3_t dir = self->monsterinfo.blind_fire_target - self->s.origin;
		float dist = dir.length();

		if (dist > 0.1f) {
			float desired_yaw = vectoyaw(dir);

			// Set ideal yaw
			self->ideal_yaw = desired_yaw;

			// Calculate delta angle
			float delta = self->s.angles[YAW] - desired_yaw;
			if (delta > 180)
				delta -= 360;
			if (delta < -180)
				delta += 360;

			// Smooth turn - maximum 5 degrees per frame for more natural movement
			float turn_speed = 5.0f;
			if (fabs(delta) > turn_speed) {
				self->s.angles[YAW] -= (delta > 0) ? turn_speed : -turn_speed;
			}
			else {
				self->s.angles[YAW] = desired_yaw;
			}

			// Normalize angle
			while (self->s.angles[YAW] > 360)
				self->s.angles[YAW] -= 360;
			while (self->s.angles[YAW] < 0)
				self->s.angles[YAW] += 360;
		}
	}

	// Fire the laser beam effect with safety check
	monster_fire_dabeam(self, -1, false, fixbot_spawn_laser_update);

	// Add particle effects at the target location
	if (!self->monsterinfo.blind_fire_target.equals(vec3_origin))
	{
		// More impressive effects for boss
		bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);
		int num_sparks = isboss ? 20 : 8; // Increased particles

		// Create welding sparks at the target position
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_WELDING_SPARKS);
		gi.WriteByte(num_sparks);
		gi.WritePosition(self->monsterinfo.blind_fire_target);
		gi.WriteDir(vec3_origin);
		gi.WriteByte(isboss ? 0xf0 : 0xe0); // Brighter color for boss
		gi.multicast(self->monsterinfo.blind_fire_target, MULTICAST_PVS, false);

		// Add a teleport effect occasionally to hint at the upcoming spawn
		if (frandom() > 0.75f) { // More frequent teleport effect
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_TELEPORT_EFFECT);
			gi.WritePosition(self->monsterinfo.blind_fire_target);
			gi.multicast(self->monsterinfo.blind_fire_target, MULTICAST_PVS, false);
		}
	}
}

void fixbot_start_spawn(edict_t* self)
{
	// Validate self
	if (!self || !self->inuse)
		return;

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// Create visual charge effect
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_WELDING_SPARKS);
	gi.WriteByte(isboss ? 25 : 15); // More sparks for boss
	gi.WritePosition(self->s.origin);
	gi.WriteDir(vec3_origin);
	gi.WriteByte(isboss ? 0xf0 : 0xe0); // Brighter color for boss
	gi.multicast(self->s.origin, MULTICAST_PVS, false);

	// Add additional particle effects for bosses
	if (isboss) {
		// Add energy flash
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_SCREEN_SPARKS);
		gi.WritePosition(self->s.origin);
		gi.multicast(self->s.origin, MULTICAST_PVS, false);
	}

	// Add a glow effect to the fixbot while spawning
	self->s.effects |= EF_HYPERBLASTER;

	// For boss, add additional effect
	if (isboss) {
		self->s.effects |= EF_PLASMA;
	}

	// Make sure the fixbot still aims at the spawn position
	if ((self->monsterinfo.aiflags & AI_MANUAL_STEERING) &&
		!self->monsterinfo.blind_fire_target.equals(vec3_origin))
	{
		vec3_t dir = self->monsterinfo.blind_fire_target - self->s.origin;
		float dist = dir.length();

		if (dist > 0.1f) {
			self->ideal_yaw = vectoyaw(dir);
			M_ChangeYaw(self);
		}
	}
}

// New function to spawn a turret at a specific position
// Spawn a turret at a specific position with proper safety checks
void spawn_turret_at_position(edict_t* self, const vec3_t& position)
{
	// Validate inputs
	if (!self || !self->inuse || position.equals(vec3_origin))
		return;

	// Final spawn position check
	vec3_t mins = { -16, -16, -24 };
	vec3_t maxs = { 16, 16, 24 };

	// Check for player proximity one last time
	bool player_too_close = false;
	float min_player_dist = 96.0f;

	for (auto player : active_players_no_spect()) {
		// Check current position
		float dist = (position - player->s.origin).length();
		if (dist < min_player_dist) {
			player_too_close = true;
			break;
		}

		// Check predicted position
		vec3_t predicted_pos = player->s.origin + (player->velocity * 0.5f);
		float predicted_dist = (position - predicted_pos).length();
		if (predicted_dist < min_player_dist) {
			player_too_close = true;
			break;
		}
	}

	if (player_too_close) {
		// Abort spawning - optional effect to show failure
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_TELEPORT_EFFECT);
		gi.WritePosition(position);
		gi.multicast(position, MULTICAST_PVS, false);
		return;
	}

	// Push entities away forcefully one more time
	PushEntitiesAway(position, 1, 80.0f, 100.0f, 100.0f, 50.0f);

	// Use CheckSpawnPoint for consistent validation
	if (!CheckSpawnPoint(position, mins, maxs))
		return;

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);
	vec3_t dir;
	edict_t* ent;

	// Determine best direction for the turret to face
	if (self->enemy && self->enemy->inuse) {
		// Face toward enemy if possible
		dir = self->enemy->s.origin - position;
		float len = dir.length();
		if (len > 0.1f)
			dir *= (1.0f / len);
		else
			dir = { 1.0f, 0.0f, 0.0f }; // Default direction if calculation fails
	}
	else {
		// No enemy, face away from fixbot
		dir = position - self->s.origin;
		float len = dir.length();
		if (len > 0.1f)
			dir *= (1.0f / len);
		else
			dir = { 1.0f, 0.0f, 0.0f }; // Default direction if calculation fails
	}

	// Create the turret entity
	ent = G_Spawn();
	if (!ent)
		return;

	// Set basic properties
	ent->enemy = nullptr;
	ent->goalentity = nullptr;
	ent->movetarget = nullptr;
	ent->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
	ent->classname = "monster_turret";
	ent->owner = self;

	// team relationship
	ent->monsterinfo.team = self->monsterinfo.team;  // Inherit team from spawner
	ent->monsterinfo.aiflags |= AI_SPAWNED_COMMANDER;
	ent->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	// Position and orient the turret
	ent->s.origin = position;
	ent->s.angles = vectoangles(dir);

	// Finalize the turret
	ent->monsterinfo.commander = self;

	// Sound and visual effects
	if (sound_spawn)
		gi.sound(self, CHAN_AUTO, sound_spawn, 1, isboss ? ATTN_NONE : ATTN_NORM, 0);

	// Enhanced visual effect - same style as tank spawner uses
	float size = 38.0f;
	SpawnGrow_Spawn(position, size * 2.0f, size * 0.5f);

	// Additional teleport effect
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_TELEPORT_EFFECT);
	gi.WritePosition(position);
	gi.multicast(position, MULTICAST_PVS, false);

	// Add to monster count
	if (self->monsterinfo.monster_slots) {
		self->monsterinfo.monster_used += 1;
	}

	// Use ED_CallSpawn to properly initialize the turret
	ED_CallSpawn(ent);

	// Post-spawn safety checks
	if (ent->inuse) {
		if (self->enemy && self->enemy->inuse && self->enemy->health > 0) {
			ent->enemy = self->enemy;
			FoundTarget(ent);  // Activates turret against current target immediately
		}
		// Give it more time before searching
		ent->monsterinfo.search_time = level.time + (isboss ? 4_sec : 3_sec);

		// Add a brief invulnerability period after spawn
		ent->pain_debounce_time = level.time + (isboss ? 2_sec : 1_sec);
	}
}

mframe_t fixbot_frames_run[] = {
	{ ai_run, 10 }
};
MMOVE_T(fixbot_move_run) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_run, nullptr };


void fixbot_prep_spawn(edict_t* self)
{
	// Reset any previous spawn data
	self->monsterinfo.blind_fire_target = vec3_origin;

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// First, check if this fixbot is allowed to spawn at all
	bool can_spawn = false;

	// Boss fixbots can spawn if they have slots available
	if (isboss && self->monsterinfo.monster_slots &&
		self->monsterinfo.monster_slots > self->monsterinfo.monster_used) {
		can_spawn = true;
	}
	// Regular fixbots spawn with probability
	else if (!isboss && frandom() < 0.30f) {
		can_spawn = true;
	}

	// If we can't spawn, don't bother with effects or continuing
	if (!can_spawn) {
		// Skip the spawning sequence entirely
		M_SetAnimation(self, &fixbot_move_run);
		return;
	}

	// Find where to spawn the turret
	vec3_t spawn_pos = {};
	vec3_t spawn_dir = {};
	if (find_turret_spawn_position(self, spawn_pos, spawn_dir))
	{
		// Verify the distance requirement - don't spawn too close
		vec3_t dist_vec = spawn_pos - self->s.origin;
		float distance = dist_vec.length();

		if (distance <= (isboss ? 80.0f : 100.0f)) {
			// Too close, try to find a better position
			bool found_better = false;

			// Try a few different angles to find a good position
			for (int attempt = 2; attempt <= 5; attempt++) {
				if (find_turret_spawn_position(self, spawn_pos, spawn_dir, attempt)) {
					dist_vec = spawn_pos - self->s.origin;
					distance = dist_vec.length();

					if (distance > (isboss ? 80.0f : 100.0f)) {
						found_better = true;
						break;
					}
				}
			}

			// If we still couldn't find a valid position, abort spawning
			if (!found_better && !isboss) {
				// Skip the spawning sequence entirely for non-boss fixbots
				M_SetAnimation(self, &fixbot_move_run);
				return;
			}
		}

		// We've found a valid spawn position, so let's proceed with effects

		// Visual effect to indicate spawning start
		gi.sound(self, CHAN_WEAPON, sound_weld1, 1, ATTN_NORM, 0);
		self->monsterinfo.aiflags |= AI_MANUAL_STEERING;

		// Store in monsterinfo for the laser aiming
		self->monsterinfo.blind_fire_target = spawn_pos;

		// Set the direction we want to face, but don't force immediate turn
		// This will be handled smoothly in fixbot_fire_spawn_laser over multiple frames
		vec3_t dir = spawn_pos - self->s.origin;
		if (dir.length() > 0.1f) {
			self->ideal_yaw = vectoyaw(dir);

			// Only make a slight initial turn to start the rotation
			// The rest will happen smoothly during the laser firing
			float delta = self->s.angles[YAW] - self->ideal_yaw;
			if (delta > 180) delta -= 360;
			if (delta < -180) delta += 360;

			// Apply a small initial turn to start the animation
			if (fabs(delta) > 15.0f) {
				// Start turning toward target
				self->s.angles[YAW] -= (delta > 0) ? 15.0f : -15.0f;
			}
		}
	}
	else {
		// Couldn't find any spawn position, abort spawning
		M_SetAnimation(self, &fixbot_move_run);
	}
}

// Update the spawn check function
void fixbot_spawn_check(edict_t* self)
{
	// Safety check first
	if (!self || !self->inuse)
		return;

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

#if DEBUG_SPAWN_AT_PLAYER
	// DEBUG MODE implementation removed for clarity
#else
	// Determine if we can spawn a turret
	bool can_spawn = false;

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING) {
		// Boss fixbots can spawn turrets if they have slots available
		if (isboss && self->monsterinfo.monster_slots &&
			self->monsterinfo.monster_slots > self->monsterinfo.monster_used) {
			can_spawn = true;
		}
		// Regular fixbots can occasionally spawn turrets too, but at a lower rate
		else if (!isboss && frandom() < 0.30f) { // Increased chance to 30% for regular fixbots
			can_spawn = true;
		}
	}

	if (can_spawn) {
		bool spawn_success = false;
		int max_attempts = isboss ? 5 : 3;

		// Try multiple attempts with different positions if needed
		for (int attempt = 0; attempt < max_attempts && !spawn_success; attempt++) {
			// Check if we already have a target position
			if (!self->monsterinfo.blind_fire_target.equals(vec3_origin)) {
				// Validate existing target position 
				vec3_t mins = { -16, -16, -24 };
				vec3_t maxs = { 16, 16, 24 };
				bool is_safe = CheckSpawnPoint(self->monsterinfo.blind_fire_target, mins, maxs);

				// Check player proximity using our iterable
				for (auto player : active_players_no_spect()) {
					float dist = (self->monsterinfo.blind_fire_target - player->s.origin).length();
					if (dist < 96.0f) {
						is_safe = false;
						break;
					}

					// Also check predicted player position
					vec3_t predicted_pos = player->s.origin + (player->velocity * 0.5f);
					float predicted_dist = (self->monsterinfo.blind_fire_target - predicted_pos).length();
					if (predicted_dist < 128.0f) {
						is_safe = false;
						break;
					}
				}

				// Verify distance from fixbot
				vec3_t dist_vec = self->monsterinfo.blind_fire_target - self->s.origin;
				float distance = dist_vec.length();

				if (is_safe && distance > (isboss ? 80.0f : 100.0f)) {
					// Position is valid, spawn the turret
					spawn_turret_at_position(self, self->monsterinfo.blind_fire_target);
					spawn_success = true;
				}
			}

			// If we haven't spawned yet, try finding a new position
			if (!spawn_success) {
				vec3_t spawn_pos, spawn_dir;
				// Pass attempt number to get varied positions
				if (find_turret_spawn_position(self, spawn_pos, spawn_dir, attempt)) {
					spawn_turret_at_position(self, spawn_pos);
					spawn_success = true;

					// Add special effect for boss spawns
					if (isboss) {
						gi.WriteByte(svc_temp_entity);
						gi.WriteByte(TE_BFG_EXPLOSION);
						gi.WritePosition(spawn_pos);
						gi.multicast(spawn_pos, MULTICAST_PVS, false);
					}
				}
			}
		}

		// If boss still couldn't spawn after all attempts, try one last desperate attempt
		if (!spawn_success && isboss) {
			// Find any valid position, even if not ideal
			vec3_t spawn_pos{}, spawn_dir{};

			// Search in cardinal directions around the fixbot
			for (int dir = 0; dir < 4; dir++) {
				vec3_t angles = { 0, dir * 90.0f, 0 };
				vec3_t forward;
				AngleVectors(angles, forward, nullptr, nullptr);

				vec3_t test_pos = self->s.origin + (forward * 250.0f);

				// Simple check for open space
				vec3_t mins = { -16, -16, -24 };
				vec3_t maxs = { 16, 16, 24 };
				if (CheckSpawnPoint(test_pos, mins, maxs)) {
					// Check player proximity
					bool player_too_close = false;
					for (auto player : active_players_no_spect()) {
						if ((test_pos - player->s.origin).length() < 128.0f) {
							player_too_close = true;
							break;
						}
					}

					if (!player_too_close) {
						spawn_turret_at_position(self, test_pos);
						break;
					}
				}
			}
		}
	}
#endif

	// Reset spawning state
	self->monsterinfo.blind_fire_target = vec3_origin;
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
	self->s.effects &= ~EF_HYPERBLASTER;
	self->s.effects &= ~EF_PLASMA; // Also clear plasma effect if present
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
//	{ ai_move, 0, fixbot_fire_spawn_laser }, // Start the laser
	{ ai_move, 0, fixbot_fire_spawn_laser },
//	{ ai_move, 0, fixbot_fire_spawn_laser },
	{ ai_move, 0, fixbot_fire_spawn_laser }, // Continue laser for ~2 seconds
	//{ ai_move, 0, fixbot_fire_spawn_laser },
	{ ai_move, 0, fixbot_fire_spawn_laser },
	//{ ai_move, 0, fixbot_fire_spawn_laser },
	{ ai_move, 0, fixbot_spawn_check },   // Now spawn the turret
	{ ai_move, 0 }                      // End spawn sequence
};
MMOVE_T(fixbot_move_spawn) = { FRAME_weldstart_01, FRAME_weldstart_06, fixbot_frames_spawn, fixbot_run };

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


// Custom enemy finder removed in favor of standard FindTarget and infront

// More aggressive search behavior
int fixbot_search(edict_t* self)
{
	extern void fixbot_start_attack(edict_t * self);

	// Use standard FindTarget instead of custom enemy finder
	if (!self->enemy || (self->enemy && self->enemy->health <= 0) ||
		(self->enemy && !visible(self, self->enemy)))
	{
		// Standard FindTarget behavior
		if (FindTarget(self))
		{
			// Only proceed if enemy is in front of the fixbot
			if (infront(self, self->enemy))
			{
				fixbot_set_attack_fly_parameters(self);
				fixbot_start_attack(self);
				return 1;  // Enemy found and attack initiated
			}
		}
	}
	else if (self->enemy && visible(self, self->enemy) && infront(self, self->enemy))
	{
		// Already has a valid enemy that's visible and in front
		fixbot_set_attack_fly_parameters(self);
		fixbot_start_attack(self);
		return 1;
	}

	return 0;  // No suitable enemy found
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

// RAFAEL
THINK(plasma_fixbot_think) (edict_t* self) -> void
{
	edict_t* acquire = nullptr;
	float oldlen = 0;
	float olddot = 1;

	// Check if we're still in the initial upward flight phase
	if (self->timestamp > level.time)
	{
		// Detect if this was launched with a sky-based trajectory
		// by checking the upward component
		float upward_bias = self->movedir[2];

		// For sky-based trajectories (high upward component),
		// add more upward motion
		vec3_t upward;
		if (upward_bias > 0.3f) {
			// High upward trajectory - add significant upward component
			upward = { 0, 0, 0.1f };
		}
		else {
			// Low upward trajectory - minimal adjustment
			upward = { 0, 0, 0.02f };
		}

		self->movedir = (self->movedir + upward).normalized();
		self->s.angles = vectoangles(self->movedir);
		self->velocity = self->movedir * self->speed;

		// Add plasma trail effects during upward flight
		if (frandom() > 0.5)
		{
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_BLASTER);
			gi.WritePosition(self->s.origin);
			gi.WriteDir(vec3_origin);
			//gi.WriteByte(0xE0); // Light blue color
			gi.multicast(self->s.origin, MULTICAST_PVS, false);
		}

		self->nextthink = level.time + FRAME_TIME_MS;
		return;
	}

	// If we've passed the initial flight phase or have a target already
	vec3_t const fwd = AngleVectors(self->s.angles).forward;

	if (self->oldenemy)
	{
		self->enemy = self->oldenemy;
		self->oldenemy = nullptr;
	}

	// Check if current enemy is still valid
	if (self->enemy)
	{
		acquire = self->enemy;

		if (acquire->health <= 0 || !visible(self, acquire))
		{
			self->enemy = acquire = nullptr;
		}
	}

	if (!acquire)
	{
		// Acquire new target
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

			vec3_t vec = self->s.origin - target->s.origin;
			float len = vec.length();
			vec.normalize();
			float dot = vec.dot(fwd);

			// Targets that require less turning are preferred
			if (dot >= olddot)
				continue;

			if (acquire == nullptr || dot < olddot || len < oldlen)
			{
				acquire = target;
				oldlen = len;
				olddot = dot;
			}
		}
	}

	if (acquire != nullptr)
	{
		vec3_t vec = (acquire->s.origin - self->s.origin).normalized();
		float t = self->accel;

		self->movedir = slerp(self->movedir, vec, t).normalized();
		self->s.angles = vectoangles(self->movedir);

		if (self->enemy != acquire)
		{
			gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/railgr1a.wav"), 1.f, 0.25f, 0);
			self->enemy = acquire;
		}
	}

	// Update velocity based on new direction
	self->velocity = self->movedir * self->speed;

	// Add plasma trail effects
	//if (frandom() > 0.7)
	//{
	//	gi.WriteByte(svc_temp_entity);
	//	gi.WriteByte(TE_BLASTER);
	//	gi.WritePosition(self->s.origin);
	//	gi.WriteDir(vec3_origin);
	//	gi.WriteByte(irandom(0xe0));
	//	gi.multicast(self->s.origin, MULTICAST_PVS, false);
	//}

	self->nextthink = level.time + FRAME_TIME_MS;
}

// Plasma touch function
void plasma_touch(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self);

void fire_fixbot_plasma(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage,
	int speed, float damage_radius, int radius_damage, float turn_fraction)
{
	edict_t* plasma;

	plasma = G_Spawn();
	plasma->s.origin = start;
	plasma->movedir = dir;
	plasma->s.angles = vectoangles(dir);
	plasma->velocity = dir * speed;
	plasma->movetype = MOVETYPE_FLYMISSILE;
	plasma->clipmask = MASK_PROJECTILE;
	plasma->svflags |= SVF_PROJECTILE;
	plasma->solid = SOLID_BBOX;
	plasma->s.effects |= EF_PLASMA | EF_ANIM_ALLFAST;
	plasma->s.modelindex = gi.modelindex("sprites/s_photon.sp2");
	plasma->s.scale = 0.75f;
	plasma->owner = self;
	plasma->touch = plasma_touch;
	plasma->speed = speed;
	plasma->accel = turn_fraction;
	plasma->mins = { -5, -5, -5 };
	plasma->maxs = { 5, 5, 5 };

	plasma->nextthink = level.time + 0.1_sec;
	plasma->think = plasma_fixbot_think;

	plasma->dmg = damage;
	plasma->radius_dmg = radius_damage;
	plasma->dmg_radius = damage_radius;
	plasma->s.sound = gi.soundindex("weapons/rockfly.wav");

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// Set timestamp for initial upward flight phase
	plasma->timestamp = level.time + (isboss ? 0.7_sec : 1.0_sec);

	if (self->enemy && visible(plasma, self->enemy))
	{
		plasma->oldenemy = self->enemy;
		gi.sound(plasma, CHAN_WEAPON, gi.soundindex("weapons/railgr1a.wav"), 1.f, 0.25f, 0);
	}

	gi.linkentity(plasma);
}

// Fixed version of fixbot_fire_plasma function
bool fixbot_fire_plasma(edict_t* self, float offset)
{
	vec3_t forward, right, up;
	vec3_t start;

	AngleVectors(self->s.angles, forward, right, up);

	// Check for open sky above the fixbot
	bool has_sky_above = false;
	{
		vec3_t sky_start = self->s.origin;
		sky_start.z += 20.0f; // Start a bit above the fixbot
		vec3_t sky_end = sky_start;
		sky_end.z += 2000.0f; // Check far upward

		trace_t sky_tr = gi.traceline(sky_start, sky_end, self, MASK_SOLID);

		// Check if we hit sky
		has_sky_above = (sky_tr.surface && (sky_tr.surface->flags & SURF_SKY));
	}

	// If no sky above and player is below, use ionripper instead
	if (!has_sky_above && self->enemy) {
		// Check if enemy is below us
		bool enemy_below = (self->enemy->s.origin[2] < self->s.origin[2]);

		if (enemy_below) {
			// Setup the start position
			start = self->s.origin;
			start += forward * 25.0f; // Move forward away from fixbot's body
			start += up * 50.f;

			// Use ionripper instead of plasma
			bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

			// Fire ionripper projectile
		//	gi.sound(self, CHAN_WEAPON, sound_pew, 1, ATTN_NORM, 0);

			// Determine direction to enemy
			vec3_t dir_to_enemy = self->enemy->s.origin - start;
			dir_to_enemy.normalize();

			// Boss fires triple spread of ionrippers
			if (isboss) {
				// Spread patterns for boss
				vec3_t dir1 = dir_to_enemy + (right * 0.12f);
				vec3_t dir4 = dir_to_enemy + (right * 0.16f);
				vec3_t dir2 = dir_to_enemy;
				vec3_t dir3 = dir_to_enemy - (right * 0.12f);
				vec3_t dir5 = dir_to_enemy - (right * 0.16f);
				dir1.normalize(); dir3.normalize();

				// Fire triple shot
				monster_fire_ionripper(self, start, dir1, 20, 750, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir2, 20, 750, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir3, 20, 750, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir4, 20, 750, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir5, 20, 750, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
			}
			else {
				// Spread patterns for boss
				vec3_t dir1 = dir_to_enemy + (right * 0.12f);
				vec3_t dir2 = dir_to_enemy;
				vec3_t dir3 = dir_to_enemy - (right * 0.12f);
				dir1.normalize(); dir3.normalize();
				monster_fire_ionripper(self, start, dir1, 12, 650, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir2, 12, 650, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir3, 12, 650, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);

			}

			return false; // Didn't use plasma
		}
	}

	// If we got here, use plasma if sky is visible, otherwise don't fire
	if (!has_sky_above) {
		return false; // Don't fire plasma when no sky
	}

	// Move the starting position further away from the fixbot to prevent collision
	start = self->s.origin;
	start += forward * 25.0f; // Move forward away from fixbot's body

	// Adjust offset based on sky presence
	if (has_sky_above) {
		// With open sky, we can use normal offset for arcing shots
		start += right * offset;
	}
	else {
		// Without sky, use minimal offset for direct shots
		start += right * (offset * 0.2f);
	}

	start += up * 50.f;

	// Base direction - adjust based on sky presence
	vec3_t dir;
	if (has_sky_above) {
		// With open sky, use normal 45 degree upward trajectory
		dir = forward + (up * 0.5f);
	}
	else {
		// Without sky, shoot more directly forward
		dir = forward + (up * 0.1f); // Just slight upward angle
	}
	dir.normalize();

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// Base parameters - adjust speed based on sky presence
	float speed;
	if (has_sky_above) {
		// With sky above, use faster projectiles for arcing shots
		speed = isboss ? irandom(450, 600) : irandom(350, 450);
	}
	else {
		// Without sky, use moderate speed for direct shots
		speed = isboss ? irandom(350, 450) : irandom(250, 350);
	}
	float turn_fraction = isboss ? 0.085f : 0.062f;

	// If there's no sky, increase turning ability for better targeting
	if (!has_sky_above) {
		turn_fraction *= 1.5f;
	}

	if (isboss) {
		// Increase separation between projectiles to avoid collision
		vec3_t start1 = start + (right * 25.0f) + (up * 15.0f);  // up-right
		vec3_t start2 = start;                                   // center 
		vec3_t start3 = start - (right * 25.0f) + (up * 15.0f);  // up-left

		// Spread direction vectors - adjust based on sky
		vec3_t dir1, dir2, dir3;

		if (!has_sky_above) {
			// Without sky, spread horizontally more than vertically
			dir1 = dir + (right * 0.15f) + (up * 0.02f);
			dir2 = dir;  // Center direction unchanged
			dir3 = dir - (right * 0.15f) + (up * 0.02f);
		}
		else {
			// With sky, use normal spreading pattern with vertical component
			dir1 = dir + (right * 0.1f) + (up * 0.05f);
			dir2 = dir;  // Center direction unchanged
			dir3 = dir - (right * 0.1f) + (up * 0.05f);
		}

		dir1.normalize();
		dir3.normalize();

		// Fire plasma projectiles with revised function
		fire_fixbot_plasma(self, start1, dir1, 20, speed, 150, 35, turn_fraction);
		fire_fixbot_plasma(self, start2, dir2, 20, speed, 150, 35, turn_fraction);
		fire_fixbot_plasma(self, start3, dir3, 20, speed, 150, 35, turn_fraction);
	}
	else {
		// Regular fixbot - fire single plasma
		fire_fixbot_plasma(self, start, dir, 20, speed, 150, 35, turn_fraction);
	}

	// Play sound once regardless of how many projectiles we fired
	gi.sound(self, CHAN_WEAPON, sound_pew, 1.f, 0.5f, 0.0f);

	// Successfully fired plasma
	return true;
}


void fixbot_reattack(edict_t* self)
{
	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// if our enemy is still valid, then continue firing
	if (self->enemy && !level.intermissiontime && !self->enemy->deadflag) {
		// Boss has higher chance to continue attack
		float reattack_chance = isboss ? 0.9f : 0.8f;

		if (frandom() < reattack_chance) {
			// Use minimal offset for more direct shots
			// If plasma fails (no sky), try ionripper
			if (fixbot_fire_plasma(self, 0.0f)) {
				self->monsterinfo.nextframe = FRAME_charging_27;
				return;
			}
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

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);


	monster_fire_blaster_bolt(self, start, dir, 7, 1000, MZ2_HOVER_BLASTER_1, EF_NONE, isboss ? 3 : 0);
	// save for aiming the shot
	self->pos1 = self->enemy->s.origin;
	self->pos1[2] += self->enemy->viewheight;

	if (frandom() < 0.080f && !isboss)
	{
		// Try plasma first, if no sky use blaster instead
		if (!fixbot_fire_plasma(self, 0.0f)) {
			// If plasma failed (no sky), fire an extra blaster shot
			monster_fire_blaster_bolt(self, start, dir, 7, 1000, MZ2_HOVER_BLASTER_1, EF_NONE, 0);
			gi.sound(self, CHAN_WEAPON, sound_pew, 1, ATTN_NORM, 0);
		}
	}
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
	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// Boss fixbots should spawn turrets periodically
	if (isboss) {
		// Check if we have monster slots available
		int slots_left = 0;
		if (self->monsterinfo.monster_slots) {
			slots_left = self->monsterinfo.monster_slots - self->monsterinfo.monster_used;
		}

		// Higher chance to spawn when we have more slots available
		float spawn_chance = slots_left > 0 ? 0.7f : 0.0f;

		if (frandom() < spawn_chance) {
			//gi.Com_PrintFmt("FixbotKL choosing to spawn turret\n");
			M_SetAnimation(self, &fixbot_move_spawn);
			return;
		}
	}

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

	// Regular attack with plasma
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

	if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED)
		boss_die(self);

	// Find and destroy all spawned turrets
	for (auto ent : active_monsters()) {
		if (ent->inuse &&
			(ent->owner == self || ent->monsterinfo.commander == self) &&
			(!strcmp(ent->classname, "monster_turret"))) {
			// Create explosion effect at turret location
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_BFG_BIGEXPLOSION);
			gi.WritePosition(ent->s.origin);
			gi.multicast(ent->s.origin, MULTICAST_PHS, false);

			// Kill the turret
			ent->health = -999;
			BecomeExplosion1(ent);
		}
	}

	// Reset used slots if any
	if (self->monsterinfo.monster_slots)
		self->monsterinfo.monster_used = 0;

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
	// Set wider vision cone for the fixbot
	self->vision_cone = 0.6f;  // Wider than default (-0.30) for better awareness

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
	sound_spawn.assign("infantry/inflies1.wav"); // Set spawn sound

	// Make sure the sounds are precached
	gi.soundindex("daedalus/daedpain1.wav");
	gi.soundindex("daedalus/daeddeth1.wav");
	gi.soundindex("makron/blaster.wav");
	gi.soundindex("misc/welder1.wav");
	gi.soundindex("misc/welder2.wav");
	gi.soundindex("misc/welder3.wav");
	gi.soundindex("infantry/inflies1.wav");

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

	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (!st.was_key_specified("power_armor_type"))
		self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
	if (!st.was_key_specified("power_armor_power"))
		self->monsterinfo.power_armor_power = 5000;
	self->max_health = 7500;
	self->health = self->max_health;
	self->s.scale = 2.6f;
	self->mins *= 3.4f;
	self->maxs *= 3.4f;

}
