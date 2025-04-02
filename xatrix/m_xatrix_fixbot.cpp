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
// Start Horde includes
// End Horde includes


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

// Data structure for the turret spawn filter - ADDED position
struct TurretSpawnFilterData {
	edict_t* self;        // The fixbot trying to spawn
	vec3_t         check_pos;   // The center position of the box being checked
	float          min_distance_sq; // Minimum squared distance allowed to players/monsters
	bool           blocked;       // Output: True if blocked, false otherwise
};

// Filter function for BoxEdicts to check turret spawn validity
static BoxEdictsResult_t TurretSpawnBoxFilter(edict_t* ent, void* data) {
	TurretSpawnFilterData* filter_data = static_cast<TurretSpawnFilterData*>(data);
	edict_t* self = filter_data->self;

	if (ent == self || ent == self->owner) { // Ignore self/owner
		return BoxEdictsResult_t::Skip;
	}

	// Check 1: Is it a truly impassable solid entity? (BSP or specific bbox types)
	// Allow passing through triggers and non-solid entities.
	if (ent->solid == SOLID_BSP || ent->solid == SOLID_BBOX)
	{
		// Check if it's a player/live monster - handle distance below
		bool is_player = (ent->client && ent->inuse);
		bool is_live_monster = ((ent->svflags & SVF_MONSTER) && ent->inuse && !ent->deadflag);

		if (!is_player && !is_live_monster) {
			// It's some other solid object we probably shouldn't spawn inside
			// unless it's something specifically allowed (e.g., func_pushable)
			// Make sure classname exists before comparing
			if (!ent->classname || strstr(ent->classname, "func_") != 0) {
				filter_data->blocked = true;
				if (developer->integer > 1) gi.Com_PrintFmt("TurretSpawnBoxFilter: Blocked by solid entity {}\n", ent->classname ? ent->classname : "?");
				return BoxEdictsResult_t::End;
			}
		}
	}

	// Check 2: Is it a player or live monster *too close* to the check position?
	if ((ent->client && ent->inuse) || ((ent->svflags & SVF_MONSTER) && ent->inuse && !ent->deadflag)) {
		vec3_t check_center = ent->s.origin + (ent->mins + ent->maxs) * 0.5f;
		// Use filter_data->check_pos (center of the box being checked)
		if ((filter_data->check_pos - check_center).lengthSquared() < filter_data->min_distance_sq) {
			filter_data->blocked = true;
			if (developer->integer > 1) gi.Com_PrintFmt("TurretSpawnBoxFilter: Blocked by proximity to {}\n", ent->classname ? ent->classname : "?");
			return BoxEdictsResult_t::End;
		}
	}

	// Check 3: Is it another turret or player defense? (Prevent stacking)
	// Make sure classname exists before comparing
	if (ent->inuse && ent->classname && (strstr(ent->classname, "monster_") == 0 || IsPlayerDefense(ent))) {
		filter_data->blocked = true;
		if (developer->integer > 1) gi.Com_PrintFmt("TurretSpawnBoxFilter: Blocked by existing turret/defense\n");
		return BoxEdictsResult_t::End;
	}

	// Check 4: Is it water/slime/lava? (Turrets likely shouldn't be placed in liquids)
	// Check the content at the center of the box
	if (gi.pointcontents(filter_data->check_pos) & MASK_WATER) {
		filter_data->blocked = true;
		if (developer->integer > 1) gi.Com_PrintFmt("TurretSpawnBoxFilter: Blocked by liquid\n");
		return BoxEdictsResult_t::End;
	}


	return BoxEdictsResult_t::Skip; // Entity is not blocking
}
// --- Ensure TurretSpawnFilterData and TurretSpawnBoxFilter are defined before this ---

bool find_turret_spawn_position(edict_t* self, vec3_t& position, vec3_t& direction)
{
	if (!self || !self->inuse) {
		position = vec3_origin;
		direction = vec3_origin;
		return false;
	}

	// --- Data for tracking the best position found so far IN THIS SEARCH ---
	struct BestPositionData {
		vec3_t pos = vec3_origin;
		vec3_t dir = vec3_origin; // Surface normal
		float distance = -1.0f;
		bool valid = false;
		bool in_front = false;
	} best_position; // Initialize fresh for each call

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);
	float trace_distance = isboss ? 1000.0f : 700.0f;
	vec3_t start = self->s.origin;
	vec3_t initial_forward;
	AngleVectors(self->s.angles, initial_forward, nullptr, nullptr); // Get current facing

	// --- Loop through different angles/attempts ---
	const int max_attempts = 12;
	for (int attempt = 0; attempt < max_attempts; ++attempt) {
		vec3_t current_forward;
		vec3_t angles = self->s.angles; // Start with current angles

		// --- Angle Variation Logic ---
		if (attempt == 0) {
			current_forward = initial_forward; // Use initial facing for first attempt
		}
		else {
			// Modify angles based on attempt number
			if (attempt <= 6) { // Check horizontal +/- 90 degrees first
				angles[YAW] += (attempt - 1) * 30.0f - 75.0f; // Scan +/- 75 degrees
				angles[PITCH] = -15.0f; // Slightly downward bias
			}
			else { // Check wider horizontal and some vertical variation
				angles[YAW] += (attempt - 7) * 60.0f - 150.0f; // Scan +/- 150 degrees
				angles[PITCH] += (frandom() - 0.5f) * 30.0f - 15.0f; // +/- 15 degree pitch variation around -15
			}
			// Normalize angles just in case
			if (angles[YAW] > 360) angles[YAW] -= 360;
			if (angles[YAW] < 0) angles[YAW] += 360;
			if (angles[PITCH] > 360) angles[PITCH] -= 360;
			if (angles[PITCH] < 0) angles[PITCH] += 360;

			AngleVectors(angles, current_forward, nullptr, nullptr); // Update forward vector
		}
		// --- End Angle Variation ---

		vec3_t end = start + (current_forward * trace_distance);
		trace_t tr = gi.traceline(start, end, self, MASK_SOLID); // Trace only against world solid

		if (tr.fraction >= 1.0) { // Didn't hit anything solid in range
			continue;
		}

		// Check if hit entity is suitable (world, func_wall, etc.)
		if (tr.ent != world && !(tr.ent->classname && strstr(tr.ent->classname, "func_") == 0)) {
			// Hit something unsuitable (e.g., another monster, player, invalid func_)
			continue;
		}

		vec3_t surface_normal = tr.plane.normal;
		if (fabs(surface_normal[2]) > 0.9f) { // Too steep floor/ceiling? Adjust normal slightly
			surface_normal[2] = (surface_normal[2] > 0) ? 0.7f : -0.7f;
			surface_normal.normalize();
		}
		vec3_t candidate_pos = tr.endpos + (surface_normal * 16.0f); // Position slightly off the wall

		// --- Use BoxEdicts for further validation ---
		// Use turret specific bounds, adjust as needed
		const vec3_t turret_mins = { -12, -12, -12 };
		const vec3_t turret_maxs = { 12, 12, 12 };
		TurretSpawnFilterData filter_data;
		filter_data.self = self;
		filter_data.check_pos = candidate_pos; // Center of the box check
		filter_data.min_distance_sq = isboss ? (112.0f * 112.0f) : (144.0f * 144.0f);
		filter_data.blocked = false;

		vec3_t box_mins = candidate_pos + turret_mins;
		vec3_t box_maxs = candidate_pos + turret_maxs;

		gi.BoxEdicts(box_mins, box_maxs, nullptr, 0, AREA_SOLID, TurretSpawnBoxFilter, &filter_data);

		if (filter_data.blocked) { // Blocked by proximity, other defense, liquid, etc.
			if (developer->integer > 1) gi.Com_PrintFmt("find_turret_spawn_position (Attempt {}): Candidate {} blocked by BoxEdicts.\n", attempt, candidate_pos);
			continue;
		}

		// --- BoxEdicts check passed ---
		// Check path from fixbot to candidate position
		if (!G_IsClearPath(self, MASK_SOLID, self->s.origin, candidate_pos)) {
			if (developer->integer > 1) gi.Com_PrintFmt("find_turret_spawn_position (Attempt {}): Path to candidate {} blocked.\n", attempt, candidate_pos);
			continue;
		}

		// --- Path Clear - Evaluate Position Quality ---
		vec3_t to_pos_vec = candidate_pos - self->s.origin;
		float dist = to_pos_vec.length();

		if (dist <= 16.0f) { // Too close to self
			continue;
		}
		to_pos_vec.normalize(); // Normalize after length check

		bool pos_in_front = (to_pos_vec.dot(initial_forward) > 0.1f); // Check against initial facing

		bool is_better = false;
		if (!best_position.valid) {
			is_better = true; // First valid position is the best so far
		}
		else {
			// Prefer positions in front
			if (pos_in_front && !best_position.in_front) {
				is_better = true;
			}
			else if (pos_in_front == best_position.in_front) {
				// If front/back status is same, prefer closer positions within reason
				if (dist < best_position.distance) {
					is_better = true;
				}
				// Consider slightly further positions if current best is very close
				else if (dist < best_position.distance * 1.2f && best_position.distance < trace_distance * 0.3f) {
					is_better = true;
				}
			}
		}

		if (is_better) {
			best_position.pos = candidate_pos;
			best_position.dir = surface_normal;
			best_position.distance = dist;
			best_position.valid = true;
			best_position.in_front = pos_in_front;
			if (developer->integer > 1) gi.Com_PrintFmt("find_turret_spawn_position (Attempt {}): Found new best candidate pos {} (dist {:.1f}, front {})\n", attempt, candidate_pos, dist, pos_in_front);
		}
	} // --- End attempt loop ---

		// --- Final Decision ---
	if (best_position.valid) {
	

		// --- REPLACEMENT LOGIC ---
		// Directly use the best position found by the loop, assuming in-loop checks were sufficient.
		position = best_position.pos;
		direction = best_position.dir;
		if (developer->integer > 0) gi.Com_PrintFmt("find_turret_spawn_position: SUCCESS using best pos {} after all attempts (simplified validation).\n", position);
		return true;

	}

	// Fallback: No suitable position found
	position = vec3_origin;
	direction = vec3_origin;
	if (developer->integer > 0) {
		gi.Com_PrintFmt("find_turret_spawn_position: Failed to find ANY valid position after all attempts.\n");
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
	if (!self || !self->inuse || position.equals(vec3_origin)) {
		if (developer->integer > 0) gi.Com_PrintFmt("spawn_turret_at_position: Invalid input (self/pos invalid).\n");
		return;
	}

	// *** REMOVED FINAL VALIDATION CALL HERE ***
	// Validation is now assumed to be handled correctly by the caller (find_turret_spawn_position)

	// --- Spawning proceeds ---
	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);
	vec3_t dir;
	edict_t* ent;

	// Determine direction (same logic as before)
	if (self->enemy && self->enemy->inuse) {
		dir = self->enemy->s.origin - position;
	}
	else {
		dir = position - self->s.origin;
	}
	if (dir.lengthSquared() <= 0.01f) {
		dir = { 1.0f, 0.0f, 0.0f };
	}
	else {
		dir.normalize();
	}

	// Create entity
	ent = G_Spawn();
	if (!ent) {
		if (developer->integer > 0) gi.Com_PrintFmt("spawn_turret_at_position: G_Spawn failed.\n");
		return;
	}

	// Setup properties (same as before)
	ent->classname = "monster_turret";
	ent->s.origin = position;
	ent->s.angles = vectoangles(dir);
	ent->owner = self;
	ent->monsterinfo.commander = self;
	ent->monsterinfo.team = self->monsterinfo.team;
	ent->monsterinfo.aiflags |= AI_DO_NOT_COUNT | AI_SPAWNED_COMMANDER | AI_IGNORE_SHOTS;

	// Sound & Visuals (same as before)
	if (sound_spawn)
		gi.sound(self, CHAN_AUTO, sound_spawn, 1, isboss ? ATTN_NONE : ATTN_NORM, 0);
	float size = 38.0f;
	SpawnGrow_Spawn(position, size * 2.0f, size * 0.5f);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_TELEPORT_EFFECT);
	gi.WritePosition(position);
	gi.multicast(position, MULTICAST_PVS, false);

	// Update count (same as before)
	if (self->monsterinfo.monster_slots) {
		self->monsterinfo.monster_used += 1;
	}

	// Finalize spawn (same as before)
	ED_CallSpawn(ent);
	if (!ent->inuse) {
		if (developer->integer > 0) gi.Com_PrintFmt("spawn_turret_at_position: ED_CallSpawn failed for turret.\n");
		if (self->monsterinfo.monster_slots) {
			self->monsterinfo.monster_used = std::max(0, self->monsterinfo.monster_used - 1);
		}
		return;
	}

	// Post-spawn setup (same as before)
	if (self->enemy && self->enemy->inuse && self->enemy->health > 0) {
		ent->enemy = self->enemy;
		FoundTarget(ent);
	}
	ent->monsterinfo.search_time = level.time + (isboss ? 4.5_sec : 3.5_sec);
	ent->pain_debounce_time = level.time + (isboss ? 2.5_sec : 1.5_sec);

	if (developer->integer > 0) {
		gi.Com_PrintFmt("spawn_turret_at_position: Successfully spawned turret at {}.\n", position);
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
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING; // Ensure flag is off initially

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// First, check if this fixbot is allowed to spawn at all
	bool can_spawn = false;
	if (isboss && self->monsterinfo.monster_slots > self->monsterinfo.monster_used) {
		can_spawn = true;
	}
	else if (!isboss && frandom() < 0.35f) { // Slightly increased chance
		can_spawn = true;
	}

	if (!can_spawn) {
		M_SetAnimation(self, &fixbot_move_run);
		return;
	}

	// Find a valid spawn position (includes BoxEdicts and final ValidateSpawnPosition check)
	vec3_t spawn_pos = {};
	vec3_t spawn_dir = {}; // Surface normal from trace inside find function
	if (find_turret_spawn_position(self, spawn_pos, spawn_dir)) // Pass attempt 0 to reset static best_position
	{
		// --- Position Found and Validated ---
		gi.sound(self, CHAN_WEAPON, sound_weld1, 1, ATTN_NORM, 0);
		self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
		self->monsterinfo.blind_fire_target = spawn_pos;

		// Start turning towards the target smoothly
		vec3_t dir = spawn_pos - self->s.origin;
		if (dir.lengthSquared() > 0.01f) { // Check squared length
			self->ideal_yaw = vectoyaw(dir);
			// M_ChangeYaw(self); // Let laser function handle smooth turning
		}
		if (developer->integer > 0) {
			gi.Com_PrintFmt("fixbot_prep_spawn: Found valid target pos {}. Starting laser.\n", spawn_pos);
		}
		// Animation proceeds to call fixbot_fire_spawn_laser
	}
	else {
		// --- No valid position found ---
		if (developer->integer > 0) {
			gi.Com_PrintFmt("fixbot_prep_spawn: Failed to find valid turret spawn position. Aborting spawn.\n");
		}
		M_SetAnimation(self, &fixbot_move_run);
	}
}

// Update the spawn check function
void fixbot_spawn_check(edict_t* self) {
	// This function is called at the end of the laser animation

	bool spawned_successfully = false; // Flag to track success

	// Check if we are still in the spawning state and have a target position
	if ((self->monsterinfo.aiflags & AI_MANUAL_STEERING) &&
		!self->monsterinfo.blind_fire_target.equals(vec3_origin))
	{
		// The position was already validated by find_turret_spawn_position
		// Perform the actual spawn
		spawn_turret_at_position(self, self->monsterinfo.blind_fire_target);
		spawned_successfully = true; // Assume success if we called spawn

		// Add boss-specific effect after successful spawn attempt (Check classname again for safety)
		if (spawned_successfully && strcmp(self->classname, "monster_fixbotkl") == 0) { // Check flag and classname
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_BFG_EXPLOSION);
			gi.WritePosition(self->monsterinfo.blind_fire_target);
			gi.multicast(self->monsterinfo.blind_fire_target, MULTICAST_PVS, false);
		}
	}
	else {
		if (developer->integer > 0) {
			gi.Com_PrintFmt("fixbot_spawn_check: Not in spawn state or no target position. Spawn aborted.\n");
		}
	}

	// Reset spawning state regardless of success/failure
	self->monsterinfo.blind_fire_target = vec3_origin;
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
	self->s.effects &= ~(EF_HYPERBLASTER | EF_PLASMA); // Clear effects

	// *** ADDED FIX ***
	// Explicitly clear the enemy pointer after the spawn attempt.
	// This forces the fixbot to find a new target when it transitions
	// back to the run state, avoiding potential use-after-free if
	// the spawning process invalidated the old enemy pointer.
	self->enemy = nullptr;
	self->goalentity = nullptr; // Also clear goalentity just in case
	// *** END ADDED FIX ***
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
	// If we already have a valid enemy, just start running
	if (self->enemy && self->enemy->inuse && self->enemy->health > 0) {
		fixbot_run(self); // Transition to run state
		return;
	}

	// Try to find a target using the standard function
	if (FindTarget(self)) {
		fixbot_run(self); // Found a target, transition to run state
		return;
	}

	// --- No enemy found, proceed with roam/landing/takeoff/stand logic ---

	// Existing logic for roam/landing/takeoff/stand:
	if (self->spawnflags.has(SPAWNFLAG_FIXBOT_LANDING))
	{
		landing_goal(self);
		M_SetAnimation(self, &fixbot_move_landing);
		self->spawnflags &= ~SPAWNFLAG_FIXBOT_LANDING;
		self->spawnflags |= SPAWNFLAG_FIXBOT_WORKING; // Use a flag to indicate it's busy?
	}
	else if (self->spawnflags.has(SPAWNFLAG_FIXBOT_TAKEOFF))
	{
		takeoff_goal(self);
		M_SetAnimation(self, &fixbot_move_takeoff);
		self->spawnflags &= ~SPAWNFLAG_FIXBOT_TAKEOFF;
		self->spawnflags |= SPAWNFLAG_FIXBOT_WORKING; // Use a flag to indicate it's busy?
	}
	else if (self->spawnflags.has(SPAWNFLAG_FIXBOT_FIXIT)) // Assuming FIXIT implies roam
	{
		M_SetAnimation(self, &fixbot_move_roamgoal); // Set roam goal animation
		self->spawnflags &= ~SPAWNFLAG_FIXBOT_FIXIT;
		self->spawnflags |= SPAWNFLAG_FIXBOT_WORKING; // Use a flag to indicate it's busy?
	}
	else if (!self->spawnflags) // If no specific action flags are set
	{
		// Default to standing if no roam/landing/takeoff needed
		M_SetAnimation(self, &fixbot_move_stand2);
	}
	else // Has some other SPAWNFLAG_FIXBOT_WORKING or similar? Go to stand.
	{
		M_SetAnimation(self, &fixbot_move_stand2);
	}

	// Note: The original code set fixbot_move_roamgoal first, then potentially overwrote it.
	// This version checks specific actions first, then defaults to stand if none apply
	// or if SPAWNFLAG_FIXBOT_WORKING is set without a specific sub-task.
	// Consider if SPAWNFLAG_FIXBOT_WORKING should always lead to roam or stand.
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
	vec3_t	   water_start{}; // Initialize to zero
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
				// Check surface exists before checking flags
				if (tr.surface && !(tr.surface->flags & SURF_SKY))
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

	// Safety check for goalentity
	if (!self->goalentity || !self->goalentity->inuse) {
		M_SetAnimation(self, &fixbot_move_stand);
		self->enemy = nullptr; // Clear enemy too if goal is gone
		return;
	}


	v = self->goalentity->s.origin - self->s.origin;
	self->ideal_yaw = vectoyaw(v);
	M_ChangeYaw(self);

	if (self->s.frame == FRAME_landing_58 || self->s.frame == FRAME_takeoff_16)
	{
		// Safely free the goal entity if it's a bot_goal
		if (self->goalentity && self->goalentity->classname && strcmp(self->goalentity->classname, "bot_goal") == 0) {
			// self->goalentity->nextthink = level.time + 100_ms; // Not needed if freeing immediately
			// self->goalentity->think = G_FreeEdict;
			G_FreeEdict(self->goalentity); // Free it now
		}
		M_SetAnimation(self, &fixbot_move_stand);
		self->goalentity = self->enemy = nullptr;
		return; // Exit after setting animation and clearing goal
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

	// Safety check for goalentity
	if (!self->goalentity || !self->goalentity->inuse) {
		M_SetAnimation(self, &fixbot_move_stand);
		self->enemy = nullptr; // Clear enemy too if goal is gone
		return;
	}

	v = self->goalentity->s.origin - self->s.origin;
	len = v.length();
	self->ideal_yaw = vectoyaw(v);
	M_ChangeYaw(self);

	if (len < 32)
	{
		// Safely free the goal entity if it's a bot_goal
		if (self->goalentity && self->goalentity->classname && strcmp(self->goalentity->classname, "bot_goal") == 0) {
			// self->goalentity->nextthink = level.time + 100_ms; // Not needed if freeing immediately
			// self->goalentity->think = G_FreeEdict;
			G_FreeEdict(self->goalentity); // Free it now
		}
		M_SetAnimation(self, &fixbot_move_stand);
		self->goalentity = self->enemy = nullptr; // Clear goal/enemy after setting stand
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
	{ ai_move, 1, change_to_roam },
	{ ai_move },
	{ ai_move },
	{ ai_move, 1, change_to_roam },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 1, change_to_roam },

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 1, change_to_roam },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 1, change_to_roam },
	{ ai_move }

};
MMOVE_T(fixbot_move_stand) = { FRAME_ambient_01, FRAME_ambient_19, fixbot_frames_stand, fixbot_run };

mframe_t fixbot_frames_stand2[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, change_to_roam },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
{ ai_stand, 0, change_to_roam },	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand },
{ ai_stand, 0, change_to_roam },	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
{ ai_stand, 0, change_to_roam },	{ ai_stand },
	{ ai_stand }
};
MMOVE_T(fixbot_move_stand2) = { FRAME_ambient_01, FRAME_ambient_19, fixbot_frames_stand2, fixbot_run };

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

	// Add safety check for owner
	if (!self || !self->inuse) {
		if (laser->inuse) G_FreeEdict(laser);
		return;
	}

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
		if (dir.lengthSquared() > 0.01f) dir.normalize(); // Safety normalize
	}

	laser->s.origin = start;
	laser->movedir = dir;
	gi.linkentity(laser);
	dabeam_update(laser, true);
}

void abortHeal(edict_t* self, bool gib, bool mark);

//mframe_t fixbot_frames_laserattack[] = {
//	{ ai_charge, 0, fixbot_fire_laser },
//	{ ai_charge, 0, fixbot_fire_laser },
//	{ ai_charge, 0, fixbot_fire_laser },
//	{ ai_charge, 0, fixbot_fire_laser },
//	{ ai_charge, 0, fixbot_fire_laser },
//	{ ai_charge, 0, fixbot_fire_laser }
//};
//MMOVE_T(fixbot_move_laserattack) = { FRAME_shoot_01, FRAME_shoot_06, fixbot_frames_laserattack, nullptr };

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
				// Ensure normalization happens after length check
				vec3_t dir_to_target = self->s.origin - acquire->s.origin;
				float const dist_to_target = dir_to_target.normalize(); // normalize() returns length
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

				vec3_t dir_to_target = self->s.origin - target->s.origin;
				float const dist_to_target = dir_to_target.normalize(); // normalize() returns length
				vec3_t vec = heat_fixbot_get_dist_vec(self, target, dist_to_target);

				// vec is already normalized by heat_fixbot_get_dist_vec
				// float const len = vec.normalize(); // Don't normalize again
				float const len = dist_to_target; // Use distance directly
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
			if (len > 0.1f) vec.normalize(); // Safety normalize
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
	if (!plasma) return; // Safety check

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

	// Safety check for self
	if (!self || !self->inuse) return false;

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
	if (!has_sky_above && self->enemy && self->enemy->inuse) { // Added enemy inuse check
		// Check if enemy is below us
		bool enemy_below = (self->enemy->s.origin[2] < self->s.origin[2]);

		if (enemy_below) {
			// Setup the start position
			start = self->s.origin;
			start += forward * 25.0f; // Move forward away from fixbot's body
			start += up * 50.f;

			// Use ionripper instead of plasma
			bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

			// Determine direction to enemy
			vec3_t dir_to_enemy = self->enemy->s.origin - start;
			if (dir_to_enemy.lengthSquared() <= 0.01f) dir_to_enemy = forward; // Use forward if too close
			else dir_to_enemy.normalize();

			// Boss fires triple spread of ionrippers
			if (isboss) {
				vec3_t dir1 = dir_to_enemy + (right * 0.12f); dir1.normalize();
				vec3_t dir4 = dir_to_enemy + (right * 0.16f); dir4.normalize();
				vec3_t dir2 = dir_to_enemy; // Already normalized
				vec3_t dir3 = dir_to_enemy - (right * 0.12f); dir3.normalize();
				vec3_t dir5 = dir_to_enemy - (right * 0.16f); dir5.normalize();

				monster_fire_ionripper(self, start, dir1, 20, 750, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir2, 20, 750, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir3, 20, 750, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir4, 20, 750, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir5, 20, 750, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
			}
			else {
				vec3_t dir1 = dir_to_enemy + (right * 0.12f); dir1.normalize();
				vec3_t dir2 = dir_to_enemy; // Already normalized
				vec3_t dir3 = dir_to_enemy - (right * 0.12f); dir3.normalize();
				monster_fire_ionripper(self, start, dir1, 12, 650, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir2, 12, 650, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
				monster_fire_ionripper(self, start, dir3, 12, 650, MZ2_HOVER_BLASTER_1, EF_IONRIPPER);
			}
			gi.sound(self, CHAN_WEAPON, sound_pew, 1, ATTN_NORM, 0); // Play sound for ionripper
			return false; // Didn't use plasma
		}
	}

	// If we got here, use plasma if sky is visible, otherwise don't fire
	if (!has_sky_above) {
		return false; // Don't fire plasma when no sky
	}

	// Move the starting position further away from the fixbot
	start = self->s.origin;
	start += forward * 25.0f;
	start += right * offset; // Apply original offset logic if sky is clear
	start += up * 50.f;

	// Base direction - use upward trajectory if sky is clear
	vec3_t dir = forward + (up * 0.5f); // 45 degree upward if sky clear
	dir.normalize();

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// Base parameters
	float speed = isboss ? irandom(450, 600) : irandom(350, 450);
	float turn_fraction = isboss ? 0.085f : 0.062f;

	if (isboss) {
		vec3_t start1 = start + (right * 25.0f) + (up * 15.0f);
		vec3_t start2 = start;
		vec3_t start3 = start - (right * 25.0f) + (up * 15.0f);

		vec3_t dir1 = dir + (right * 0.1f) + (up * 0.05f); dir1.normalize();
		vec3_t dir2 = dir; // Already normalized
		vec3_t dir3 = dir - (right * 0.1f) + (up * 0.05f); dir3.normalize();

		fire_fixbot_plasma(self, start1, dir1, 20, speed, 150, 35, turn_fraction);
		fire_fixbot_plasma(self, start2, dir2, 20, speed, 150, 35, turn_fraction);
		fire_fixbot_plasma(self, start3, dir3, 20, speed, 150, 35, turn_fraction);
	}
	else {
		fire_fixbot_plasma(self, start, dir, 20, speed, 150, 35, turn_fraction);
	}

	gi.sound(self, CHAN_WEAPON, sound_pew, 1.f, 0.5f, 0.0f);
	return true; // Successfully fired plasma
}


void fixbot_reattack(edict_t* self)
{
	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);
	bool enemy_valid = (self->enemy && self->enemy->inuse && !level.intermissiontime && !self->enemy->deadflag);

	// Explicitly check visibility here too, as the logic depends on it
	if (enemy_valid && !visible(self, self->enemy)) {
		enemy_valid = false; // Treat non-visible enemy as invalid for re-attack decision
	}

	// if our enemy is still valid AND visible, then consider continuing firing
	if (enemy_valid) {
		float reattack_chance = isboss ? 0.9f : 0.8f;

		if (frandom() < reattack_chance) {
			// Use minimal offset for more direct shots
			// fixbot_fire_plasma handles ionripper fallback
			// It returns true if plasma was used, false otherwise (or if enemy invalid).
			if (fixbot_fire_plasma(self, 0.0f)) {
				// Plasma fired successfully, loop back in attack animation
				self->monsterinfo.nextframe = FRAME_charging_27;
				return; // Stay in attack state
			}
			// If fixbot_fire_plasma returned false (used ionripper, couldn't fire, or enemy invalid),
			// fall through to end the attack sequence.
		}
		// Else: Random chance failed, fall through to end the attack sequence.
	}
	// Else: Enemy was invalid or not visible at the start of the function.

	// --- End attack sequence ---
	// Set attack cooldown timer
	self->monsterinfo.attack_finished = level.time + (isboss ? 0.5_sec : 1.0_sec);

	// *** CHANGE: Explicitly call fixbot_run to transition state ***
	// This ensures we leave the attack animation cleanly.
	M_SetAnimation(self, &fixbot_move_run);
	// *** END CHANGE ***

	// Remove the old lines which just set nextframe and M_SetAnimation,
	// as fixbot_run() handles setting the correct animation now.
	// self->monsterinfo.nextframe = 0; // Clear any pending frame override (handled by fixbot_run)
	// M_SetAnimation(self, &fixbot_move_run); // (handled by fixbot_run)
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
			// Safely clear owner if it exists
			if (self->enemy && self->enemy->owner == self) { // Check if enemy is the goal and owned by self
				self->enemy->owner = nullptr;
			}
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

	if (!self->enemy || !self->enemy->inuse) { // Add inuse check
		M_SetAnimation(self, &fixbot_move_stand); // Go back to stand if enemy gone
		return;
	}

	vec[0] = 24.0;
	vec[1] = -0.8f;
	vec[2] = -10.0;

	AngleVectors(self->s.angles, forward, right, up);
	start = M_ProjectFlashSource(self, vec, forward, right);

	end = self->enemy->s.origin;

	dir = end - start; // Calculate direction for sparks if needed, though not used here

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

	// Add enemy validity check
	// Check visibility as well, as ai_charge might rely on a visible enemy implicitly
	if (!self->enemy || !self->enemy->inuse || !visible(self, self->enemy))
	{
		// *** CHANGE: Immediately transition out of attack state ***
		M_SetAnimation(self, &fixbot_move_run);
		return;           // Stop executing this frame's action
		// *** END CHANGE ***
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

	// Plasma/Ionripper chance (only for non-boss, as boss handles spawns)
	if (!isboss && frandom() < 0.080f) // Reduced chance slightly from 0.080
	{
		// Try plasma first, if no sky use ionripper instead
		// fixbot_fire_plasma now handles the ionripper fallback
		// Note: fixbot_fire_plasma itself contains enemy checks. If the enemy becomes
		// invalid between the start of this function and here, fixbot_fire_plasma
		// should handle it gracefully by returning false and not firing.
		fixbot_fire_plasma(self, 0.0f);
	}
}
MONSTERINFO_STAND(fixbot_stand) (edict_t* self) -> void
{
	// Added more robust enemy check
	if (self->enemy && self->enemy->inuse && self->enemy->health > 0 && visible(self, self->enemy) && !OnSameTeam(self, self->enemy)) {
		self->monsterinfo.run(self);
	}
	else {
		self->enemy = nullptr; // Clear invalid enemy
		M_SetAnimation(self, &fixbot_move_stand);
	}
}

MONSTERINFO_RUN(fixbot_run) (edict_t* self) -> void
{
	// The standard ai_run function handles enemy checks and finding new targets.
	// We just need to make sure the correct 'run' animation is playing.

	// Ensure the run animation is set if not already correct
	// (ai_run might change it based on enemy state, but this ensures
	// the base run animation is the default for this state)
	{
		if (self->monsterinfo.aiflags & AI_STAND_GROUND)
			M_SetAnimation(self, &fixbot_move_stand);
		else
			M_SetAnimation(self, &fixbot_move_run);
	}
	// Let the frame's ai_run call handle the rest (movement, calling ai_checkattack, etc.)
}

MONSTERINFO_WALK(fixbot_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &fixbot_move_walk);
}

void fixbot_start_attack(edict_t* self)
{
	// Added enemy validity check
	if (self->enemy && self->enemy->inuse && self->enemy->health > 0) {
		M_SetAnimation(self, &fixbot_move_start_attack);
	}
	else {
		self->enemy = nullptr; // Clear invalid enemy
		M_SetAnimation(self, &fixbot_move_run);
	}
}


MONSTERINFO_ATTACK(fixbot_attack) (edict_t* self) -> void
{
	// *** ADDED: Set attack flight parameters when starting the attack sequence ***
	fixbot_set_attack_fly_parameters(self);
	// *** END ADDED ***

	bool isboss = (strcmp(self->classname, "monster_fixbotkl") == 0);

	// ONLY boss fixbots should spawn turrets periodically
	if (isboss) {
		int slots_left = 0;
		if (self->monsterinfo.monster_slots) {
			slots_left = self->monsterinfo.monster_slots - self->monsterinfo.monster_used;
		}
		float spawn_chance = slots_left > 0 ? 0.4f : 0.0f; // Boss always tries if slots available

		// Increased spawn chance for boss if slots available
		if (frandom() < spawn_chance) {
			M_SetAnimation(self, &fixbot_move_spawn);
			return; // Start spawn animation
		}
	}

	// Add strafing behavior similar to hover
	if (frandom() > 0.4f) // 60% chance to strafe
	{
		self->monsterinfo.lefty = (frandom() <= 0.5f); // Randomly choose direction
		self->monsterinfo.attack_state = AS_SLIDING;
	}
	else
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
	}

	// Regular attack (blaster/plasma/ionripper sequence)
	M_SetAnimation(self, &fixbot_move_attack2);
}

PAIN(fixbot_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (level.time < self->pain_debounce_time)
		return;

	// Don't interrupt critical sequences like spawning
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING) {
		return;
	}

	fixbot_set_attack_fly_parameters(self);
	self->pain_debounce_time = level.time + 3_sec;
	gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);

	if (damage <= 10)
		M_SetAnimation(self, &fixbot_move_pain3);
	else if (damage <= 25)
		M_SetAnimation(self, &fixbot_move_painb);
	else
		M_SetAnimation(self, &fixbot_move_paina);

	//abortHeal(self, false, false); // Likely related to unused medic code
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

	// Handle boss death logic if applicable ( Horde mode integration )
	if (g_horde && g_horde->integer && self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED)
		boss_die(self);

	// Find and destroy all spawned turrets
	for (auto ent : active_monsters()) { // Use helper iterator
		if (ent->inuse && ent->owner == self && ent->classname && // Added null check for classname
			(strcmp(ent->classname, "monster_turret") == 0)) {
			// Create explosion effect at turret location
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_BFG_BIGEXPLOSION);
			gi.WritePosition(ent->s.origin);
			gi.multicast(ent->s.origin, MULTICAST_PHS, false);

			// Kill the turret forcefully
			// ent->health = -999; // Setting health low might not trigger desired death effect
			// BecomeExplosion1(ent); // This might be better
			T_Damage(ent, self, self, vec3_origin, ent->s.origin, vec3_origin, 99999, 100, DAMAGE_NO_PROTECTION, MOD_UNKNOWN); // Deal massive damage
		}
	}

	// Reset used slots if any
	if (self->monsterinfo.monster_slots)
		self->monsterinfo.monster_used = 0;

	// Call generic monster death handler (Horde mode integration)
	OnEntityDeath(self);

	ThrowGibs(self, damage, {
	{ 1, "models/objects/gibs/sm_metal/tris.md2", GIB_ACID },
	{ 1, "models/objects/gibs/gear/tris.md2", GIB_METALLIC },
		});
	gi.sound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	BecomeExplosion1(self); // Make the fixbot itself explode
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

	self->health = 180 * st.health_multiplier;
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
	// Apply skill-based slot increase
	if (skill->integer > 0) { // Check skill->integer directly
		self->monsterinfo.monster_slots += floorf(self->monsterinfo.monster_slots * (skill->value / 2.f)); // Use floorf
	}

	if (!st.was_key_specified("reinforcements"))
		M_SetupReinforcements(fixbot_reinforcements, self->monsterinfo.reinforcements);

	ApplyMonsterBonusFlags(self); // Apply bonuses like Champion, etc.
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
	self->mass = 400;
	// Scale mins/maxs correctly AFTER initial values are set
	self->mins *= self->s.scale / 1.4f; // Scale relative to base fixbot scale
	self->maxs *= self->s.scale / 1.4f; // Scale relative to base fixbot scale

	// Ensure monsterinfo scale reflects final model scale for AI calculations
	self->monsterinfo.scale = self->s.scale;

	// Re-link after changing size
	gi.linkentity(self);
}