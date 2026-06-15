// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// m_move.c -- monster movement

#include "g_local.h"
#include "horde/horde_ids.h"
#include "horde/p_flyer_morph.h"
#include "horde/g_horde_phys.h"

// ============================================================================
// Movement Constants - Extracted for performance and maintainability
// ============================================================================

// Personal space constants for anti-stacking
constexpr float MONSTER_PERSONAL_SPACE_DEFAULT = 48.0f;
constexpr float MONSTER_PERSONAL_SPACE_BOSS = 96.0f;
constexpr float MONSTER_PERSONAL_SPACE_FLYING = 64.0f;
constexpr float MONSTER_PERSONAL_SPACE_SEARCH_MULTIPLIER = 2.0f;

// Repulsion system constants
constexpr float REPULSION_MAX_FORCE = 24.0f;
constexpr float REPULSION_VERTICAL_DAMPING = 0.1f;
constexpr float REPULSION_MULTI_MONSTER_SCALE = 0.7f;
constexpr float REPULSION_STRENGTH_DEFAULT = 0.25f;
constexpr float REPULSION_STRENGTH_BOSS = 0.1f;
constexpr float REPULSION_STRENGTH_COMBAT = 0.5f;
constexpr float REPULSION_STRENGTH_CORRIDOR = 0.1f;       // Reduced from 0.3 to 0.1
constexpr float REPULSION_STRENGTH_TIGHT_CORRIDOR = 0.0f; // New: completely disable in very tight spaces
constexpr float REPULSION_LATERAL_MAX_FRACTION = 0.5f;    // cap sideways nudge to this fraction of the step length (keeps goal dominant)
constexpr float REPULSION_MIN_THRESHOLD_SQ = 1.0f;

// Wall repulsion constants - keeps monsters off walls so squeezed boxes don't park their
// (full-size) model inside geometry. Lateral-only vs. the goal direction, so it never blocks
// pathing through an intentional gap ("unless a nav point is in the way"). Reuses the cardinal
// corridor traces, and unlike monster repulsion it stays ON in tight corridors (centering them).
constexpr float WALL_REPULSION_FORCE = 24.0f;          // per-wall push magnitude at zero distance
constexpr float WALL_REPULSION_STRENGTH = 0.35f;       // how much of the lateral push is applied to the move
constexpr float WALL_REPULSION_STRENGTH_COMBAT = 0.6f; // slightly reduced while engaging so corner-peeking still works
constexpr float WALL_REPULSION_MIN_THRESHOLD_SQ = 0.25f;

// Squeeze gating: a monster only WIDTH-shrinks for a real two-sided gap (doorway/corridor). We
// test this by casting two FORWARD probes at the body's left/right edges - if both flanks are
// walled ahead, the opening is narrower than the body and it must squeeze; a single or diagonal
// wall leaves one flank open, so it slides at full size instead of burying its model. Probing
// forward (not sideways at the center) catches the pinch while the monster is still approaching
// the gap, not only once its center is already between the jambs. This is the extra forward reach
// of those probes, past the body's leading edge.
constexpr float SQUEEZE_PINCH_LOOKAHEAD = 8.0f;
constexpr int REPULSION_MAX_NEARBY_MONSTERS = 8;
constexpr int REPULSION_CROWDING_THRESHOLD = 4;
constexpr gtime_t REPULSION_CACHE_TIME = 150_ms;

// Corridor detection constants
constexpr float CORRIDOR_CHECK_DISTANCE = 64.0f;
constexpr float CORRIDOR_CHECK_DISTANCE_TIGHT = 48.0f;  // New: tighter check for single-file corridors
constexpr int CORRIDOR_BLOCKED_THRESHOLD = 2;
constexpr int CORRIDOR_BLOCKED_TIGHT_THRESHOLD = 3;     // New: all 4 directions blocked = tight corridor

// Combat range constants
constexpr float COMBAT_RANGE_MELEE = 64.0f;
constexpr float COMBAT_RANGE_RANGED = 256.0f;
constexpr float COMBAT_RANGE_MIXED = 128.0f;

// Flying monster constants
constexpr float FLY_MIN_HEIGHT = 40.0f;
constexpr float FLY_CARRIER_MIN_HEIGHT = 104.0f;
constexpr gtime_t FLY_POSITION_UPDATE_MIN = 3_sec;
constexpr gtime_t FLY_POSITION_UPDATE_MAX = 5_sec;
constexpr float FLY_TURN_FACTOR_FAST = 0.45f;
constexpr float FLY_TURN_FACTOR_BASE = 0.84f;
constexpr float FLY_TURN_FACTOR_SPEED_SCALE = 0.08f;
constexpr float FLY_SPEED_BONUS_MULTIPLIER = 1.3f;
constexpr float FLY_ACCEL_BONUS_MULTIPLIER = 1.5f;
constexpr float FLY_PITCH_LERP_SPEED = 4.0f;

// Movement decision constants
constexpr float MOVEMENT_MIN_DELTA = 1.0f;
constexpr float MOVEMENT_MIN_DELTA_THRESHOLD = 10.0f;
constexpr float MOVEMENT_PROGRESS_THRESHOLD = 0.05f;

// Direction search constants
constexpr float DIRECTION_ANGLE_STEP = 45.0f;
constexpr float DIRECTION_YAW_MIN_DIFF = 15.0f;
constexpr float DIRECTION_YAW_MAX_DIFF = 165.0f;
constexpr float DIRECTION_MOMENTUM_VARIATION = 30.0f;

// Timing constants
constexpr gtime_t BUMP_TIME_DELAY = 200_ms;
constexpr gtime_t BUMP_TIME_FALLBACK = 300_ms;
constexpr gtime_t RANDOM_CHANGE_BASE = 100_ms;
constexpr gtime_t RANDOM_CHANGE_MIN = 500_ms;
constexpr gtime_t RANDOM_CHANGE_MAX = 1000_ms;
constexpr gtime_t BAD_MOVE_TIME_PENALTY = 1000_ms;
constexpr gtime_t STUCK_TELEPORT_MIN = 12.0_sec;
constexpr gtime_t STUCK_TELEPORT_MAX = 17.0_sec;
constexpr gtime_t REACT_DAMAGE_MIN = 3_sec;
constexpr gtime_t REACT_DAMAGE_MAX = 5_sec;

// Flying monster wall stuck recovery constants
constexpr gtime_t FLY_WALL_STUCK_THRESHOLD = 1500_ms; // Time before forcing descent
constexpr float FLY_DESCENT_SPEED_MULTIPLIER = 0.6f;  // How fast to descend when stuck

// Ground monster wall-stuck recovery (non-boss). Bosses squeeze their box / push off walls to
// thread tight gaps; normal monsters can't afford that (cost + model burying with many of them),
// so instead they notice they've been grinding the same wall for a while and deliberately turn to
// look another way before trying again. This is how long to keep grinding before forcing that turn.
constexpr gtime_t WALL_STUCK_TURN_THRESHOLD = 600_ms;

// Path progress detection constants - for dynamic recalculation when stuck
constexpr float PATH_PROGRESS_MIN_DISTANCE = 16.0f;    // Must move at least this far toward goal
constexpr gtime_t PATH_PROGRESS_CHECK_TIME = 500_ms;   // Check progress every 500ms
constexpr gtime_t PATH_STUCK_RECALC_TIME = 750_ms;     // Recalculate path after this long without progress
constexpr gtime_t PATH_CACHE_TIME_NORMAL = 2_sec;      // Normal path cache time
constexpr gtime_t PATH_CACHE_TIME_STUCK = 500_ms;      // Reduced cache time when stuck

// this is used for communications out of sv_movestep to say what entity
// is blocking us
// Anti-stacking system for monsters
// These functions prevent monsters from clustering in the same spot
// while still allowing them to pass through each other when necessary

// Get preferred personal space for a monster type
static float GetMonsterPersonalSpace(edict_t* ent)
{
	if (!ent || !(ent->svflags & SVF_MONSTER))
		return MONSTER_PERSONAL_SPACE_DEFAULT;

	// Bosses need more space
	if (ent->monsterinfo.IS_BOSS)
		return MONSTER_PERSONAL_SPACE_BOSS;

	// Flying monsters need more vertical space
	if (ent->flags & FL_FLY)
		return MONSTER_PERSONAL_SPACE_FLYING;

	// Default personal space
	return MONSTER_PERSONAL_SPACE_DEFAULT;
}

// Calculate repulsion vector from nearby monsters
static vec3_t CalculateMonsterRepulsion(edict_t* ent)
{
	vec3_t repulsion = { 0, 0, 0 };

	if (!ent || !(ent->svflags & SVF_MONSTER))
		return repulsion;

	// Don't calculate repulsion too frequently (optimized from 100ms to 150ms)
	if (ent->monsterinfo.last_repulsion_time > level.time - REPULSION_CACHE_TIME)
		return ent->monsterinfo.repulsion_vector;

	ent->monsterinfo.last_repulsion_time = level.time;

	// Get personal space for this monster
	float personal_space = ent->monsterinfo.personal_space;
	if (personal_space <= 0)
		personal_space = GetMonsterPersonalSpace(ent);

	// Search for nearby monsters using spatial grid (O(1) instead of O(n))
	const float search_radius = personal_space * MONSTER_PERSONAL_SPACE_SEARCH_MULTIPLIER;
	const float personal_space_sq = personal_space * personal_space; // Squared for faster comparison
	int nearby_count = 0;

	// Use ProximityGrid for massive performance improvement over findradius
	// This reduces complexity from O(n) to O(1) using spatial partitioning
	std::span<edict_t* const> nearby_entities = HordePhys::g_monster_grid.IsBuilt()
		? HordePhys::g_monster_grid.QueryRadius(ent->s.origin, search_radius)
		: std::span<edict_t* const>{};

	for (edict_t* other : nearby_entities)
	{
		// Skip self, non-monsters, and dead things
		if (other == ent || !other->inuse || !(other->svflags & SVF_MONSTER))
			continue;

		if (other->health <= 0 || other->deadflag)
			continue;

		// Skip repulsion between summoned Stroggs - they can walk through each other
		if (ent->monsterinfo.issummoned && other->monsterinfo.issummoned)
			continue;

		// Calculate distance using squared distance for performance
		vec3_t diff = ent->s.origin - other->s.origin;
		const float dist_sq = diff.lengthSquared();

		// Skip if too far (use squared distance comparison)
		if (dist_sq >= personal_space_sq || dist_sq < REPULSION_MIN_THRESHOLD_SQ)
			continue;

		// Now calculate actual distance only when needed
		const float dist = std::sqrt(dist_sq);

		// Calculate repulsion force - stronger when closer
		// Use C++17/23 std::clamp for cleaner, potentially better optimized code
		float strength = std::clamp((personal_space - dist) / personal_space, 0.0f, 1.0f);

		// Normalize and scale the repulsion vector
		vec3_t push = diff * (1.0f / dist); // Manual normalization (already have dist)
		push *= strength * REPULSION_MAX_FORCE;

		// Reduce vertical repulsion for ground monsters
		if (!(ent->flags & (FL_FLY | FL_SWIM)))
			push.z *= REPULSION_VERTICAL_DAMPING;

		repulsion += push;
		nearby_count++;

		// Limit total monsters considered for performance
		if (nearby_count >= REPULSION_MAX_NEARBY_MONSTERS)
			break;
	}

	// Scale down if too many monsters to prevent excessive spreading
	if (nearby_count > REPULSION_CROWDING_THRESHOLD)
		repulsion *= REPULSION_MULTI_MONSTER_SCALE;

	// Store calculated repulsion
	ent->monsterinfo.repulsion_vector = repulsion;

	return repulsion;
}

// Apply repulsion to movement vector
static void ApplyMonsterRepulsion(edict_t* ent, vec3_t& move, bool in_combat)
{
	if (!ent || !(ent->svflags & SVF_MONSTER))
		return;

	// Don't apply repulsion to bosses as strongly
	float repulsion_strength = ent->monsterinfo.IS_BOSS ? REPULSION_STRENGTH_BOSS : REPULSION_STRENGTH_DEFAULT;

	// Reduce repulsion in combat to maintain aggression
	if (in_combat && ent->enemy && ent->enemy->inuse)
		repulsion_strength *= REPULSION_STRENGTH_COMBAT;

	// Cache corridor detection (and the push-off-walls vector) to reduce expensive trace calls.
	// This runs regardless of nearby-monster repulsion: wall repulsion has to work in empty
	// corners too, so it can't sit behind the monster-repulsion early-out.
	static constexpr gtime_t CORRIDOR_CACHE_TIME = 200_ms;
	const bool needs_corridor_check = (ent->monsterinfo.corridor_check_time <= level.time);

	if (needs_corridor_check)
	{
		// Check if we're in a narrow space (corridor) - optimized with early exits
		const vec3_t& origin = ent->s.origin;
		int blocked_dirs = 0;
		int tight_blocked_dirs = 0;

		// Cardinal probes (line traces instead of bbox traces for performance). Keep each
		// fraction so we can build a wall-centering push from the same casts.
		const float fx_neg = gi.traceline(origin, origin + vec3_t{-CORRIDOR_CHECK_DISTANCE, 0, 0}, ent, MASK_SOLID).fraction;
		const float fx_pos = gi.traceline(origin, origin + vec3_t{ CORRIDOR_CHECK_DISTANCE, 0, 0}, ent, MASK_SOLID).fraction;
		const float fy_neg = gi.traceline(origin, origin + vec3_t{0, -CORRIDOR_CHECK_DISTANCE, 0}, ent, MASK_SOLID).fraction;
		const float fy_pos = gi.traceline(origin, origin + vec3_t{0,  CORRIDOR_CHECK_DISTANCE, 0}, ent, MASK_SOLID).fraction;

		if (fx_neg < 1.0f) blocked_dirs++;
		if (fx_pos < 1.0f) blocked_dirs++;
		if (fy_neg < 1.0f) blocked_dirs++;
		if (fy_pos < 1.0f) blocked_dirs++;

		// Push away from each near wall (closer wall -> stronger push the opposite way). Opposite
		// walls cancel, so a monster centered in a corridor gets ~no push, while an off-center or
		// corner-wedged one is steered toward open space.
		const vec3_t wall_push = {
			((1.0f - fx_neg) - (1.0f - fx_pos)) * WALL_REPULSION_FORCE, // wall on -x pushes +x
			((1.0f - fy_neg) - (1.0f - fy_pos)) * WALL_REPULSION_FORCE,
			0.0f
		};

		// Additional check for VERY tight corridors (single monster width)
		if (blocked_dirs >= CORRIDOR_BLOCKED_THRESHOLD)
		{
			if (gi.traceline(origin, origin + vec3_t{-CORRIDOR_CHECK_DISTANCE_TIGHT, 0, 0}, ent, MASK_SOLID).fraction < 1.0f)
				tight_blocked_dirs++;
			if (gi.traceline(origin, origin + vec3_t{CORRIDOR_CHECK_DISTANCE_TIGHT, 0, 0}, ent, MASK_SOLID).fraction < 1.0f)
				tight_blocked_dirs++;
			if (gi.traceline(origin, origin + vec3_t{0, -CORRIDOR_CHECK_DISTANCE_TIGHT, 0}, ent, MASK_SOLID).fraction < 1.0f)
				tight_blocked_dirs++;
			if (gi.traceline(origin, origin + vec3_t{0, CORRIDOR_CHECK_DISTANCE_TIGHT, 0}, ent, MASK_SOLID).fraction < 1.0f)
				tight_blocked_dirs++;
		}

		// Cache the result
		ent->monsterinfo.corridor_blocked_dirs = blocked_dirs;
		ent->monsterinfo.corridor_tight_blocked_dirs = tight_blocked_dirs;
		ent->monsterinfo.corridor_wall_push = wall_push;
		ent->monsterinfo.corridor_check_time = level.time + CORRIDOR_CACHE_TIME;
	}

	// Goal direction reference, captured before any push is added.
	const vec3_t original_move = move;

	// --- Monster-vs-monster repulsion (lateral, skipped when nothing is nearby) ---
	vec3_t repulsion = CalculateMonsterRepulsion(ent);
	if (repulsion.lengthSquared() >= REPULSION_MIN_THRESHOLD_SQ)
	{
		// PERFORMANCE FIX: Better corridor handling to prevent traffic jams. In very tight spaces
		// (3-4 directions blocked at close range), completely disable lateral repulsion so monsters
		// can file through single-file. Wall repulsion below is intentionally exempt from this.
		if (ent->monsterinfo.corridor_tight_blocked_dirs >= CORRIDOR_BLOCKED_TIGHT_THRESHOLD)
			repulsion_strength = REPULSION_STRENGTH_TIGHT_CORRIDOR;
		else if (ent->monsterinfo.corridor_blocked_dirs >= CORRIDOR_BLOCKED_THRESHOLD)
			repulsion_strength *= REPULSION_STRENGTH_CORRIDOR;

		if (original_move.lengthSquared() > 1.0f)
		{
			// Lateral-only repulsion: keep ONLY the component perpendicular to the goal
			// direction and ADD it, so separation never slows (or speeds) travel along the
			// path. A fast monster behind a slow one keeps full forward speed and, being
			// non-solid in horde, flows past instead of being held back. The sideways nudge
			// is what spreads the pack so they don't stack on one spot.
			const vec3_t move_dir = original_move.normalized();
			vec3_t lateral = repulsion - move_dir * repulsion.dot(move_dir);

			// Keep the goal dominant: cap the sideways nudge to a fraction of the step length.
			const float max_lat = original_move.length() * REPULSION_LATERAL_MAX_FRACTION;
			if (lateral.lengthSquared() > max_lat * max_lat)
				lateral = lateral.normalized() * max_lat;

			move += lateral * repulsion_strength;
		}
		else
		{
			// No goal direction (idle/standing): plain nudge so a pile on one spot still spreads.
			move += repulsion * repulsion_strength;
		}
	}

	// --- Wall repulsion (lateral, bosses only) ---
	// Steers the monster off nearby walls so a squeezed (shrunk-box) monster doesn't park its
	// full-size model inside geometry. Paired with squeeze-to-fit, which is also boss-only now:
	// normal monsters keep full size and instead turn away from walls they grind on (see the
	// wall-stuck handling in SV_movestep), so they don't need this centering nudge. Lateral-only
	// vs. the goal: the along-goal component is stripped, so a path that genuinely runs through a
	// gap is never blocked ("unless a nav point is in the way") - only sideways centering remains.
	// Unlike monster repulsion this stays active in tight corridors, where walls are close.
	const vec3_t wall_push = ent->monsterinfo.corridor_wall_push;
	if (ent->monsterinfo.IS_BOSS && wall_push.lengthSquared() >= WALL_REPULSION_MIN_THRESHOLD_SQ)
	{
		float wall_strength = WALL_REPULSION_STRENGTH;
		if (in_combat && ent->enemy && ent->enemy->inuse)
			wall_strength *= WALL_REPULSION_STRENGTH_COMBAT;

		if (original_move.lengthSquared() > 1.0f)
		{
			const vec3_t move_dir = original_move.normalized();
			vec3_t lateral = wall_push - move_dir * wall_push.dot(move_dir);

			const float max_lat = original_move.length() * REPULSION_LATERAL_MAX_FRACTION;
			if (lateral.lengthSquared() > max_lat * max_lat)
				lateral = lateral.normalized() * max_lat;

			move += lateral * wall_strength;
		}
		else
		{
			// Idle/standing with no goal: apply the full push so a monster wedged into a corner
			// still works its way back out to where its real box fits.
			move += wall_push * wall_strength;
		}
	}
}

// Initialize anti-stacking for a monster
void InitMonsterAntiStack(edict_t* ent)
{
	if (!ent || !(ent->svflags & SVF_MONSTER))
		return;

	ent->monsterinfo.personal_space = GetMonsterPersonalSpace(ent);
	ent->monsterinfo.repulsion_vector = vec3_t{0, 0, 0};
	ent->monsterinfo.formation_slot = -1;
	ent->monsterinfo.formation_update_time = 0_sec;
	ent->monsterinfo.last_repulsion_time = 0_sec;
	ent->monsterinfo.last_valid_move = vec3_t{0, 0, 0};

	// Initialize corridor caching
	ent->monsterinfo.corridor_check_time = 0_sec;
	ent->monsterinfo.corridor_blocked_dirs = 0;
	ent->monsterinfo.corridor_tight_blocked_dirs = 0;
	ent->monsterinfo.corridor_wall_push = vec3_t{0, 0, 0};
	ent->monsterinfo.wall_stuck_time = 0_ms;
	SetMonsterSqueeze(ent, {});

	// Set preferred combat range based on monster type
	if (ent->monsterinfo.melee)
		ent->monsterinfo.preferred_combat_range = COMBAT_RANGE_MELEE;
	else if (ent->monsterinfo.attack)
		ent->monsterinfo.preferred_combat_range = COMBAT_RANGE_RANGED;
	else
		ent->monsterinfo.preferred_combat_range = COMBAT_RANGE_MIXED;

	// Initialize teleport timing fields to prevent immediate teleportation after spawn
	if (g_horde && g_horde->integer)
	{
		ent->monsterinfo.stuck_check_time = level.time + random_time(STUCK_TELEPORT_MIN, STUCK_TELEPORT_MAX);
		ent->monsterinfo.react_to_damage_time = level.time + random_time(REACT_DAMAGE_MIN, REACT_DAMAGE_MAX);
		ent->monsterinfo.last_activity_time = level.time;
		ent->teleport_time = level.time + random_time(8_sec, 12_sec);
		ent->monsterinfo.no_enemy_timeout_start_time = 0_sec;
	}
}

// [Horde] Single point of truth for writing monsterinfo.bbox_squeeze, so the global count of
// currently-squeezed monsters stays accurate. That count is the fast-path gate for
// G_TraceSqueezeAware (g_phys.cpp) - while it's 0, incoming projectile/hitscan traces stay free.
void SetMonsterSqueeze(edict_t* ent, const vec3_t& new_sq)
{
	const vec3_t& old = ent->monsterinfo.bbox_squeeze;
	const bool was = (old[0] != 0.f || old[1] != 0.f || old[2] != 0.f);
	const bool now = (new_sq[0] != 0.f || new_sq[1] != 0.f || new_sq[2] != 0.f);

	if (was && !now)
		g_num_squeezed_monsters--;
	else if (!was && now)
		g_num_squeezed_monsters++;

	ent->monsterinfo.bbox_squeeze = new_sq;
}

// Helper function to determine if triggers should be enabled for this entity
// In horde mode, triggers are disabled for monsters except morphed players
static inline bool ShouldEnableTriggers(edict_t* ent)
{
	return !g_horde->integer || IsMorphed(ent);
}

edict_t* new_bad; // pmm

/*
=============
M_CheckBottom

Returns false if any part of the bottom of the entity is off an edge that
is not a staircase.

=============
*/
bool M_CheckBottom_Fast_Generic(const vec3_t& absmins, const vec3_t& absmaxs, bool ceiling)
{
	vec3_t start;
	start[2] = ceiling ? absmaxs[2] + 1 : absmins[2] - 1;

	// Usar una sola iteración con bit masking para probar las 4 esquinas
	for (int i = 0; i < 4; i++) {
		start[0] = (i & 1) ? absmaxs[0] : absmins[0];
		start[1] = (i & 2) ? absmaxs[1] : absmins[1];
		if (gi.pointcontents(start) != CONTENTS_SOLID)
			return false;
	}
	return true;
}


bool M_CheckBottom_Slow_Generic(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs,
	edict_t* ignore, contents_t mask, bool ceiling, bool allow_any_step_height)
{
	// Precalcular dimensiones para reutilización
	const vec3_t step_quadrant_size = (maxs - mins) * 0.5f;
	const vec3_t half_step_quadrant = step_quadrant_size * 0.5f;
	const vec3_t half_step_quadrant_mins = -half_step_quadrant;

	// Optimizar trazas iniciales
	vec3_t const start = origin;
	vec3_t stop = origin;
	stop.z = ceiling ?
		(origin.z + maxs.z + STEPSIZE * 2) :
		(origin.z + mins.z - STEPSIZE * 2);

	trace_t trace = gi.trace(start, mins, maxs, stop, ignore, mask);

	if (trace.fraction == 1.0f)
		return false;

	if (allow_any_step_height)
		return true;

	const float mid = trace.endpos[2];
	constexpr  float step_threshold = STEPSIZE;
	vec3_t quadrant_start, quadrant_end;

	// Optimizar bucle de verificación de esquinas
	for (int i = 0; i < 4; i++) {
		quadrant_start = start;
		quadrant_start.x += ((i & 1) ? half_step_quadrant.x : -half_step_quadrant.x);
		quadrant_start.y += ((i & 2) ? half_step_quadrant.y : -half_step_quadrant.y);
		quadrant_end = quadrant_start;
		quadrant_end.z = stop.z;

		trace = gi.trace(quadrant_start, half_step_quadrant_mins, half_step_quadrant,
			quadrant_end, ignore, mask);

		const float height_diff = ceiling ?
			trace.endpos[2] - mid :
			mid - trace.endpos[2];

		if (trace.fraction == 1.0f || height_diff > step_threshold)
			return false;
	}

	return true;
}

bool M_CheckBottom(edict_t* ent)
{
	// if all of the points under the corners are solid world, don't bother
	// with the tougher checks

	if (M_CheckBottom_Fast_Generic(ent->s.origin + ent->mins, ent->s.origin + ent->maxs, ent->gravityVector[2] > 0))
		return true; // we got out easy

	contents_t const mask = (ent->svflags & SVF_MONSTER) ? MASK_MONSTERSOLID : (MASK_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER);
	return M_CheckBottom_Slow_Generic(ent->s.origin, ent->mins, ent->maxs, ent, mask, ent->gravityVector[2] > 0, ent->spawnflags.has(SPAWNFLAG_MONSTER_SUPER_STEP));
}

//============
// ROGUE
bool IsBadAhead(edict_t* self, edict_t* bad, const vec3_t& move)
{
	vec3_t dir;
	vec3_t forward;
	float  dp_bad, dp_move;
	vec3_t move_copy;

	move_copy = move;

	dir = bad->s.origin - self->s.origin;
	dir.normalize();
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	dp_bad = forward.dot(dir);

	move_copy.normalize();
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	dp_move = forward.dot(move_copy);

	if ((dp_bad < 0) && (dp_move < 0))
		return true;
	if ((dp_bad > 0) && (dp_move > 0))
		return true;

	return false;
}

[[nodiscard]] vec3_t G_IdealHoverPosition(const edict_t* ent)
{
	constexpr uint32_t SKIP_FLAGS = AI_COMBAT_POINT | AI_SOUND_TARGET | AI_HINT_PATH | AI_PATHING;

	// Early return for default cases
	if ((!ent->enemy && !(ent->monsterinfo.aiflags & AI_MEDIC)) ||
		(ent->monsterinfo.aiflags & SKIP_FLAGS)) {
		return vec3_origin; // Use constant if available
	}

	// Calculate random angle in horizontal plane
	const float theta = frandom(2 * PIf); // theta is already float

	// Calculate vertical angle based on entity type using acosf
	const float phi = ent->monsterinfo.fly_above ? acosf(0.7f + frandom(0.3f)) :
		(ent->monsterinfo.fly_buzzard || (ent->monsterinfo.aiflags & AI_MEDIC)) ? acosf(frandom()) :
		acosf(frandom() * 0.7f); // Use acosf here

	// Calculate direction vector using sinf and cosf
	const vec3_t direction{
		sinf(phi) * cosf(theta), // Use sinf, cosf
		sinf(phi) * sinf(theta), // Use sinf, sinf
		cosf(phi)                // Use cosf
	};

	// Scale direction by random distance within range
	return direction * frandom(ent->monsterinfo.fly_min_distance, ent->monsterinfo.fly_max_distance);
}
inline bool SV_flystep_testvisposition(vec3_t start, vec3_t end, vec3_t starta, vec3_t startb, edict_t* ent)
{
	trace_t tr = gi.traceline(start, end, ent, MASK_SOLID | CONTENTS_MONSTERCLIP);

	if (tr.fraction == 1.0f)
	{
		tr = gi.trace(starta, ent->mins, ent->maxs, startb, ent, MASK_SOLID | CONTENTS_MONSTERCLIP);

		if (tr.fraction == 1.0f)
			return true;
	}

	return false;
}

static bool SV_alternate_flystep(edict_t* ent, vec3_t move, bool relink, edict_t* current_bad)
{
	// swimming monsters just follow their velocity in the air
	if ((ent->flags & FL_SWIM) && ent->waterlevel < WATER_UNDER)
		return true;

	// Recalculate ideal hover position if timer expires, pinned target lost,
	// or enemy acquired but ideal position was never set (prevents face-to-face on spawn)
	if (ent->monsterinfo.fly_position_time <= level.time ||
		(ent->enemy && ent->monsterinfo.fly_pinned && !visible(ent, ent->enemy)) ||
		(ent->enemy && ent->monsterinfo.fly_ideal_position == vec3_origin))
	{
		ent->monsterinfo.fly_pinned = false;
		ent->monsterinfo.fly_position_time = level.time + random_time(FLY_POSITION_UPDATE_MIN, FLY_POSITION_UPDATE_MAX);
		ent->monsterinfo.fly_ideal_position = G_IdealHoverPosition(ent);
	}

	vec3_t towards_origin, towards_velocity = {};
	float current_speed;
	vec3_t dir = ent->velocity.normalized(current_speed);

	// Check for NaN direction (shouldn't happen but good for safety)
	if (std::isnan(dir[0]) || std::isnan(dir[1]) || std::isnan(dir[2]))
	{
//  #if defined(_DEBUG) && defined(_WIN32)
// 		__debugbreak(); // Break in debug builds if NaN occurs
//  #endif
		return false;
	}

	// Determine the target origin based on AI state
	if (ent->monsterinfo.aiflags & AI_PATHING)
		towards_origin = (ent->monsterinfo.nav_path.returnCode == PathReturnCode::TraversalPending) ?
		ent->monsterinfo.nav_path.secondMovePoint : ent->monsterinfo.nav_path.firstMovePoint;
	else if (ent->enemy && !(ent->monsterinfo.aiflags & (AI_COMBAT_POINT | AI_SOUND_TARGET | AI_LOST_SIGHT)))
	{
		towards_origin = ent->enemy->s.origin;
		towards_velocity = ent->enemy->velocity;
	}
	else if (ent->goalentity)
		towards_origin = ent->goalentity->s.origin;
	else // Target likely gone, decelerate
	{
		if (current_speed)
		{
			// Apply base acceleration (negative to decelerate)
			float base_accel = ent->monsterinfo.fly_acceleration;
			// Apply bonus flag acceleration modifier
			if (ent->monsterinfo.bonus_flags != BF_NONE && !ent->monsterinfo.IS_BOSS) { //HORDEBONUS
				base_accel *= FLY_ACCEL_BONUS_MULTIPLIER;
			}

			if (current_speed > 0)
				current_speed = max(0.f, current_speed - base_accel);
			else if (current_speed < 0) // Should generally not happen with flying monsters
				current_speed = min(0.f, current_speed + base_accel);

			ent->velocity = dir * current_speed;
		}
		return true;
	}

	// Calculate the desired position (wanted_pos)
	vec3_t wanted_pos;
	if (ent->monsterinfo.fly_pinned)
		wanted_pos = ent->monsterinfo.fly_ideal_position;
	else if (ent->monsterinfo.aiflags & (AI_PATHING | AI_COMBAT_POINT | AI_SOUND_TARGET | AI_LOST_SIGHT))
		wanted_pos = towards_origin;
	else // Combine target position, velocity prediction, and ideal hover offset
		wanted_pos = (towards_origin + (towards_velocity * 0.5f)) + ent->monsterinfo.fly_ideal_position;

	// Trace to find a reachable point near the wanted position
	trace_t tr = gi.trace(towards_origin, { -8.f, -8.f, -8.f }, { 8.f, 8.f, 8.f }, wanted_pos, ent, MASK_SOLID | CONTENTS_MONSTERCLIP);
	if (!tr.allsolid) // Use the trace endpoint if it's not completely solid
		wanted_pos = tr.endpos;

	// Calculate direction and distance to the wanted position
	float dist_to_wanted;
	vec3_t dest_diff = (wanted_pos - ent->s.origin);
	// Ignore vertical difference if it's within the monster's height (prevents vertical jitter)
	if (dest_diff.z > ent->mins.z && dest_diff.z < ent->maxs.z)
		dest_diff.z = 0;
	vec3_t wanted_dir = dest_diff.normalized(dist_to_wanted);

	// Update ideal yaw towards the target origin (not necessarily the wanted position)
	if (!(ent->monsterinfo.aiflags & AI_MANUAL_STEERING))
		ent->ideal_yaw = vectoyaw((towards_origin - ent->s.origin).normalized());

	// --- Obstacle Avoidance ---
	// Cache trace mask to avoid repeated bitwise operations
	static constexpr contents_t obstacle_mask = MASK_SOLID | CONTENTS_MONSTERCLIP;

	// Check for immediate obstacles in the direction of wanted_dir
	tr = gi.trace(ent->s.origin, ent->mins, ent->maxs, ent->s.origin + (wanted_dir * ent->monsterinfo.fly_acceleration), ent, obstacle_mask);

	// Cache angle vectors to avoid recalculation
	vec3_t aim_fwd, aim_rgt, aim_up;
	vec3_t const yaw_angles = { 0, ent->s.angles.y, 0 };
	AngleVectors(yaw_angles, aim_fwd, aim_rgt, aim_up);

	// If blocked closely, try to navigate around
	if (tr.fraction < 0.25f)
	{
		// Track when we started being blocked
		if (ent->monsterinfo.fly_wall_stuck_time == 0_ms)
			ent->monsterinfo.fly_wall_stuck_time = level.time;

		// Pre-calculate common positions to reduce vector operations
		const vec3_t bottom_pos = ent->s.origin + vec3_t{ 0, 0, ent->mins.z };
		const vec3_t top_pos = ent->s.origin + vec3_t{ 0, 0, ent->maxs.z };
		const vec3_t accel_offset = vec3_t{ 0, 0, ent->monsterinfo.fly_acceleration };

		bool const bottom_visible = SV_flystep_testvisposition(bottom_pos, wanted_pos,
			ent->s.origin, ent->s.origin + vec3_t{ 0, 0, ent->mins.z } - accel_offset, ent);
		bool const top_visible = SV_flystep_testvisposition(top_pos, wanted_pos,
			ent->s.origin, top_pos + accel_offset, ent);

		// Check if we've been stuck for too long - force descent to floor
		bool force_descent = false;
		if (level.time > ent->monsterinfo.fly_wall_stuck_time + FLY_WALL_STUCK_THRESHOLD)
		{
			// Check if there's floor below us
			vec3_t floor_check = ent->s.origin;
			floor_check.z -= 512.f;
			trace_t floor_trace = gi.trace(ent->s.origin, ent->mins, ent->maxs, floor_check, ent, obstacle_mask);

			// If there's floor below and not in sky, descend
			if (floor_trace.fraction < 1.0f &&
				!(floor_trace.surface && (floor_trace.surface->flags & SURF_SKY)))
			{
				force_descent = true;
				// Add strong downward component
				wanted_dir = vec3_t{ wanted_dir.x * 0.3f, wanted_dir.y * 0.3f, -FLY_DESCENT_SPEED_MULTIPLIER };
				wanted_dir.normalize();
			}
		}

		if (!force_descent)
		{
			if (bottom_visible == top_visible) // Blocked horizontally
			{
				// Pre-calculate side positions
				const vec3_t side_offset = aim_fwd.scaled(ent->maxs);
				const vec3_t lateral_offset = aim_rgt.scaled(ent->maxs);

				bool const left_visible = gi.traceline(ent->s.origin + side_offset - lateral_offset, wanted_pos, ent, obstacle_mask).fraction == 1.0f;
				bool const right_visible = gi.traceline(ent->s.origin + side_offset + lateral_offset, wanted_pos, ent, obstacle_mask).fraction == 1.0f;

				if (left_visible != right_visible) // Clear path to one side
				{
					wanted_dir += (right_visible ? aim_rgt : -aim_rgt);
				}
				else // Blocked on both sides, push away from obstacle
				{
					wanted_dir = tr.plane.normal;
					// Also add slight downward bias when stuck on all sides
					wanted_dir.z -= 0.2f;
				}
			}
			else // Blocked vertically
			{
				wanted_dir += (top_visible ? aim_up : -aim_up);
			}
		}
		wanted_dir.normalize(); // Re-normalize after adjustment
	}
	else
	{
		// Not blocked - reset wall stuck timer
		ent->monsterinfo.fly_wall_stuck_time = 0_ms;
	}
	// --- End Obstacle Avoidance ---

	// --- Direction Interpolation ---
	bool const following_paths = ent->monsterinfo.aiflags & (AI_PATHING | AI_COMBAT_POINT | AI_LOST_SIGHT);
	float turn_factor;

	// Determine how quickly the monster can turn towards the wanted direction
	// Faster turning if using thrusters (melee), following paths, or already moving towards the target.
	// Slower turning based on current speed otherwise.
	if (((ent->monsterinfo.fly_thrusters && !ent->monsterinfo.fly_pinned) || following_paths) && dir.dot(wanted_dir) > 0.0f)
		turn_factor = FLY_TURN_FACTOR_FAST; // Faster turn rate
	else
		turn_factor = min(1.f, FLY_TURN_FACTOR_BASE + (FLY_TURN_FACTOR_SPEED_SCALE * (current_speed / ent->monsterinfo.fly_speed))); // Speed dependent turn rate

	vec3_t final_dir = dir ? dir : wanted_dir; // Start with current or wanted direction

	// Check for and handle bad movement directions (e.g., flying into water)
	bool bad_movement_direction = false;
	if (ent->flags & FL_SWIM) // If currently swimming
		bad_movement_direction = !(gi.pointcontents(ent->s.origin + (wanted_dir * current_speed)) & CONTENTS_WATER); // Check if destination is NOT water
	else if ((ent->flags & FL_FLY) && ent->waterlevel < WATER_UNDER) // If flying and not fully submerged
		bad_movement_direction = gi.pointcontents(ent->s.origin + (wanted_dir * current_speed)) & CONTENTS_WATER; // Check if destination IS water

	if (bad_movement_direction)
	{
		if (ent->monsterinfo.fly_recovery_time < level.time) // If recovery timer expired
		{
			// Set a new random recovery direction
			ent->monsterinfo.fly_recovery_dir = vec3_t{ crandom(), crandom(), crandom() }.normalized();
			ent->monsterinfo.fly_recovery_time = level.time + 1_sec;
		}
		wanted_dir = ent->monsterinfo.fly_recovery_dir; // Use recovery direction
	}

	// Interpolate current direction towards wanted direction
	if (dir && turn_factor > 0) // Only interpolate if we have a current direction and can turn
		final_dir = slerp(dir, wanted_dir, 1.0f - turn_factor).normalized();

	// --- Speed Calculation ---
	float base_fly_speed = ent->monsterinfo.fly_speed;
	float base_accel = ent->monsterinfo.fly_acceleration;

	// Apply bonus flag modifiers
	if (ent->monsterinfo.bonus_flags != BF_NONE && !ent->monsterinfo.IS_BOSS) { //HORDEBONUS
		base_fly_speed *= FLY_SPEED_BONUS_MULTIPLIER;
		base_accel *= FLY_ACCEL_BONUS_MULTIPLIER;
	}

	// Slow down as we approach the wanted position
	float speed_factor;
	if (!ent->enemy || (ent->monsterinfo.fly_thrusters && !ent->monsterinfo.fly_pinned) || following_paths)
	{
		// If following paths and moving away from the wanted direction, stop.
		if (following_paths && dir && wanted_dir.dot(dir) < -0.25)
			speed_factor = 0.f;
		else // Otherwise, maintain full speed
			speed_factor = 1.f;
	}
	else // Normal flight towards enemy/goal: scale speed based on distance
	{
		speed_factor = min(1.f, dist_to_wanted / base_fly_speed); // Slow down proportionally
	}

	// Reverse speed factor if moving in a bad direction (e.g., trying to leave water)
	if (bad_movement_direction)
		speed_factor = -speed_factor;

	// Apply extra acceleration if moving away from the destination
	float accel = base_accel;
	if (final_dir.dot(wanted_dir) < 0.25f) // If angle difference is large (> ~75 degrees)
		accel *= 2.0f; // Apply reverse thrusters/brake harder

	// Calculate the desired speed based on distance and state
	float wanted_speed = base_fly_speed * speed_factor;

	// Stop movement if in manual steering mode (e.g., spawning)
	if (ent->monsterinfo.aiflags & AI_MANUAL_STEERING)
		wanted_speed = 0;

	// Adjust current speed towards wanted speed using acceleration
	if (current_speed > wanted_speed)
		current_speed = max(wanted_speed, current_speed - accel);
	else if (current_speed < wanted_speed)
		current_speed = min(wanted_speed, current_speed + accel);

	// Final NaN check for safety
	if (std::isnan(final_dir[0]) || std::isnan(final_dir[1]) || std::isnan(final_dir[2]) ||
		std::isnan(current_speed))
	{
// #if defined(_DEBUG) && defined(_WIN32)
// 		__debugbreak();
// #endif
		return false;
	}

	// Commit the final velocity
	ent->velocity = final_dir * current_speed;

	// Adjust pitch for specific monster types (buzzards, medics) when targeting an enemy
	if (ent->enemy && (ent->monsterinfo.fly_buzzard || (ent->monsterinfo.aiflags & AI_MEDIC)))
	{
		vec3_t d = (ent->s.origin - towards_origin).normalized();
		d = vectoangles(d);
		// Smoothly interpolate pitch towards the negative target pitch angle
		ent->s.angles[PITCH] = LerpAngle(ent->s.angles[PITCH], -d[PITCH], gi.frame_time_s * FLY_PITCH_LERP_SPEED);
	}
	else // Otherwise, keep pitch level
	{
		ent->s.angles[PITCH] = 0;
	}

	return true; // Movement step was successful
}

// flying monsters don't step up
static bool SV_flystep(edict_t* ent, vec3_t move, bool relink, edict_t* current_bad)
{
	if (ent->monsterinfo.aiflags & AI_ALTERNATE_FLY)
	{
		if (SV_alternate_flystep(ent, move, relink, current_bad))
			return true;
	}

	// try the move
	vec3_t const oldorg = ent->s.origin;
	vec3_t neworg = ent->s.origin + move;

	// fixme: move to monsterinfo
	// we want the carrier to stay a certain distance off the ground, to help prevent him
	// from shooting his fliers, who spawn in below him
	const float minheight = (horde::IsMonsterType(ent, horde::MonsterTypeID::CARRIER) ||
	                         horde::IsMonsterType(ent, horde::MonsterTypeID::CARRIER_MINI))
	                        ? FLY_CARRIER_MIN_HEIGHT : FLY_MIN_HEIGHT;
	// try one move with vertical motion, then one without
	for (int i = 0; i < 2; i++)
	{
		vec3_t new_move = move;

		if (i == 0 && ent->enemy)
		{
			if (!ent->goalentity)
				ent->goalentity = ent->enemy;

			vec3_t& goal_position = (ent->monsterinfo.aiflags & AI_PATHING) ? ent->monsterinfo.nav_path.firstMovePoint : ent->goalentity->s.origin;

			float const dz = ent->s.origin[2] - goal_position[2];
			float dist = move.length();

			if (ent->goalentity->client)
			{
				if (dz > minheight)
				{
					//	pmm
					new_move *= 0.5f;
					new_move[2] -= dist;
				}
				if (!((ent->flags & FL_SWIM) && (ent->waterlevel < WATER_WAIST)))
					if (dz < (minheight - 10))
					{
						new_move *= 0.5f;
						new_move[2] += dist;
					}
			}
			else
			{
				// RAFAEL
				//if (strcmp(ent->classname, "monster_fixbot") == 0)
				//{
				//	if (ent->s.frame >= 105 && ent->s.frame <= 120)
				//	{
				//		if (dz > 12)
				//			new_move[2]--;
				//		else if (dz < -12)
				//			new_move[2]++;
				//	}
				//	else if (ent->s.frame >= 31 && ent->s.frame <= 88)
				//	{
				//		if (dz > 12)
				//			new_move[2] -= 12;
				//		else if (dz < -12)
				//			new_move[2] += 12;
				//	}
				//	else
				//	{
				//		if (dz > 12)
				//			new_move[2] -= 8;
				//		else if (dz < -12)
				//			new_move[2] += 8;
				//	}
				//}
				//else
				{
					// RAFAEL
					if (dz > 0)
					{
						new_move *= 0.5f;
						new_move[2] -= min(dist, dz);
					}
					else if (dz < 0)
					{
						new_move *= 0.5f;
						new_move[2] += -max(-dist, dz);
					}
					// RAFAEL
				}
				// RAFAEL
			}
		}

		neworg = ent->s.origin + new_move;

		trace_t trace = gi.trace(ent->s.origin, ent->mins, ent->maxs, neworg, ent, MASK_MONSTERSOLID);

		// fly monsters don't enter water voluntarily
		if (ent->flags & FL_FLY)
		{
			if (!ent->waterlevel)
			{
				vec3_t  const test{ trace.endpos[0], trace.endpos[1], trace.endpos[2] + ent->mins[2] + 1 };
				contents_t const contents = gi.pointcontents(test);
				if (contents & MASK_WATER)
					return false;
			}
		}

		// swim monsters don't exit water voluntarily
		if (ent->flags & FL_SWIM)
		{
			if (ent->waterlevel < WATER_WAIST)
			{
				vec3_t const test{ trace.endpos[0], trace.endpos[1], trace.endpos[2] + ent->mins[2] + 1 };
				contents_t const contents = gi.pointcontents(test);
				if (!(contents & MASK_WATER))
					return false;
			}
		}

		// ROGUE
		if ((trace.fraction == 1) && (!trace.allsolid) && (!trace.startsolid))
			// ROGUE
		{
			ent->s.origin = trace.endpos;
			//=====
			// PGM
			if (!current_bad && CheckForBadArea(ent))
				ent->s.origin = oldorg;
			else
			{
				if (relink)
				{
					gi.linkentity(ent);
					G_TouchTriggers(ent);
				}

				return true;
			}
			// PGM
			//=====
		}

		G_Impact(ent, trace);

		if (!ent->enemy)
			break;
	}

	return false;
}

/*
=============
SV_movestep

Called by monster program code.
The move will be adjusted for slopes and stairs, but if the move isn't
possible, no move is done, false is returned, and
pr_global_struct->trace_normal is set to the normal of the blocking wall
=============
*/
// FIXME since we need to test end position contents here, can we avoid doing
// it again later in catagorize position?
bool SV_movestep(edict_t* ent, vec3_t move, bool relink)
{
	//======
	// PGM
	edict_t* current_bad = nullptr;

	// PMM - who cares about bad areas if you're dead?
	if (ent->health > 0)
	{
		current_bad = CheckForBadArea(ent);
		if (current_bad)
		{
			ent->bad_area = current_bad;

			if (ent->enemy && horde::IsSpecialType(ent->enemy, horde::SpecialEntityTypeID::TESLA_MINE))
			{
				// if the tesla is in front of us, back up...
				if (IsBadAhead(ent, current_bad, move))
					move *= -1;
			}
		}
		else if (ent->bad_area)
		{
			// if we're no longer in a bad area, get back to business.
			ent->bad_area = nullptr;
			if (ent->oldenemy) // && ent->bad_area->owner == ent->enemy)
			{
				ent->enemy = ent->oldenemy;
				ent->goalentity = ent->oldenemy;
				FoundTarget(ent);
			}
		}
	}
	// PGM
	//======

	// Apply anti-stacking repulsion for non-flying monsters
	if (g_horde->integer && (ent->svflags & SVF_MONSTER) && !(ent->flags & (FL_SWIM | FL_FLY)))
	{
		bool in_combat = (ent->enemy && ent->enemy->inuse && ent->enemy->health > 0);
		ApplyMonsterRepulsion(ent, move, in_combat);
	}

	// flying monsters don't step up
	if (ent->flags & (FL_SWIM | FL_FLY))
		return SV_flystep(ent, move, relink, current_bad);

	// try the move
	vec3_t const oldorg = ent->s.origin;

	float stepsize;

	// push down from a step height above the wished position
	if (ent->spawnflags.has(SPAWNFLAG_MONSTER_SUPER_STEP) && ent->health > 0)
		stepsize = 64.f;
	else if (!(ent->monsterinfo.aiflags & AI_NOSTEP))
		stepsize = STEPSIZE;
	else
		stepsize = 1;

	stepsize += 0.75f;

	contents_t mask = (ent->svflags & SVF_MONSTER) ? MASK_MONSTERSOLID : (MASK_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER);

	// Paril: horde
// would be better it as special or monster? on special side it manages it removal, but its a monster. 
// The logic is: if horde is on AND the entity is NOT a sentry gun...
if ((g_horde->integer && !horde::IsSpecialType(ent, horde::SpecialEntityTypeID::SENTRY_GUN)) ||
    (ent->svflags & SVF_PLAYER && EntIsSpectating(ent)))
{
    // Don't remove CONTENTS_MONSTER for morphed players - they need proper collision
    if (!IsMorphed(ent)) {
        mask &= ~CONTENTS_MONSTER;
    }
}

	// [Horde] Squeeze-to-fit: a monster too big for a doorway-sized gap shrinks its collision
	// box just enough to clear the opening - and grows back as the gap allows - recomputed
	// every step so it adapts to the actual wall instead of always collapsing to the minimum.
	// Width shrinks toward g_monster_squeeze (half-width), height toward g_monster_squeeze_height
	// (total). The whole entity is resized + relinked, so all stepping/ground logic stays
	// consistent (this is what makes stairs/crates work). bbox_squeeze stores the per-axis inset
	// so the real size is always recoverable. Trace-based; works for func_door openings and bare
	// door-sized gaps alike. Bosses only: they're big, few, and important enough to justify the
	// per-step traces and the rare model-into-wall clipping. Normal monsters keep full size and
	// instead turn away when they grind a wall too long (see the wedged branch below).
	if (g_horde->integer && (ent->svflags & SVF_MONSTER) && ent->monsterinfo.IS_BOSS &&
		!(ent->flags & (FL_SWIM | FL_FLY)) &&
		(g_monster_squeeze->value > 0.f || g_monster_squeeze_height->value > 0.f))
	{
		const vec3_t sq = ent->monsterinfo.bbox_squeeze;

		// Recover the monster's real (unsqueezed) box from the stored inset.
		vec3_t orig_mins = ent->mins, orig_maxs = ent->maxs;
		orig_mins[0] -= sq[0]; orig_maxs[0] += sq[0];
		orig_mins[1] -= sq[1]; orig_maxs[1] += sq[1];
		orig_maxs[2] += sq[2];

		if ((ent->monsterinfo.aiflags & AI_DUCKED) || ent->health <= 0)
		{
			// Duck / death-shrink own the height; hand the width back and let them manage the rest.
			if (sq[0] != 0.f || sq[1] != 0.f || sq[2] != 0.f)
			{
				ent->mins[0] = orig_mins[0]; ent->maxs[0] = orig_maxs[0];
				ent->mins[1] = orig_mins[1]; ent->maxs[1] = orig_maxs[1];
				SetMonsterSqueeze(ent, {});
				gi.linkentity(ent);
			}
		}
		else
		{
			// Two-stage sizing each step:
			//  (1) Grow target (t_fit): the LARGEST box that isn't startsolid at our current
			//      spot, regardless of where we're heading. This lets a monster that squeezed
			//      into a gap and then stopped grow back the instant it has room, instead of
			//      sitting shrunk with its model clipped into the wall.
			//  (2) Shrink below t_fit ONLY if that box is blocked along the move AND the
			//      smallest allowed box clears the gap meaningfully (a real pinch, not a flat
			//      wall we're merely sliding against).
			vec3_t want_mins = orig_mins, want_maxs = orig_maxs;

			const float wtarget = (g_monster_squeeze->value > 0.f) ? std::max(4.f, g_monster_squeeze->value) : 0.f;
			const float htarget = g_monster_squeeze_height->value;
			const float half_x = (orig_maxs[0] - orig_mins[0]) * 0.5f;
			const float half_y = (orig_maxs[1] - orig_mins[1]) * 0.5f;
			const float total_h = orig_maxs[2] - orig_mins[2];
			const float max_ins_x = (wtarget > 0.f) ? std::max(0.f, half_x - wtarget) : 0.f;
			const float max_ins_y = (wtarget > 0.f) ? std::max(0.f, half_y - wtarget) : 0.f;
			const float max_ins_z = (htarget > 0.f && total_h > htarget) ? std::min(total_h - htarget, std::max(0.f, total_h - 24.f)) : 0.f;

			if (max_ins_x > 0.f || max_ins_y > 0.f || max_ins_z > 0.f)
			{
				// Box for given shrink fractions: txy narrows width (x/y), tz lowers height (z).
				// 0 = full, 1 = minimum footprint. Width and height are independent so a monster can
				// duck a low ceiling without needlessly narrowing, and narrow a doorway without
				// needlessly ducking.
				auto box_at = [&](float txy, float tz, vec3_t& bmins, vec3_t& bmaxs)
				{
					bmins = orig_mins; bmaxs = orig_maxs;
					bmins[0] += max_ins_x * txy; bmaxs[0] -= max_ins_x * txy;
					bmins[1] += max_ins_y * txy; bmaxs[1] -= max_ins_y * txy;
					bmaxs[2] -= max_ins_z * tz;
				};

				vec3_t tmin, tmax;

				// (1) Grow target / in-place fit. Only search when we're currently shrunk and might
				// grow back; a full-size monster is already at t_fit = 0 (no extra trace usually).
				// Uniform here - this is just the least shrink so we don't overlap where we stand.
				float t_fit = 0.f;
				const bool currently_squeezed = (sq[0] != 0.f || sq[1] != 0.f || sq[2] != 0.f);
				if (currently_squeezed && gi.trace(oldorg, orig_mins, orig_maxs, oldorg, ent, mask).startsolid)
				{
					// Full box is wedged here - find the LEAST shrink that fits at this spot.
					float lo = 0.f, hi = 1.f;
					for (int i = 0; i < 5; i++)
					{
						const float midt = (lo + hi) * 0.5f;
						box_at(midt, midt, tmin, tmax);
						if (gi.trace(oldorg, tmin, tmax, oldorg, ent, mask).startsolid)
							lo = midt; // still stuck here - shrink more
						else
							hi = midt; // fits here - try keeping more size
					}
					t_fit = hi;
				}

				// (2) Shrink below t_fit for the move. Width and height are decided separately so
				// each axis only gives up size when that axis is actually what's blocking us.
				float txy_final = t_fit;
				float tz_final = t_fit;
				box_at(t_fit, t_fit, tmin, tmax);
				trace_t fit_move = gi.trace(oldorg, tmin, tmax, oldorg + move, ent, mask);
				if (fit_move.startsolid || fit_move.allsolid || fit_move.fraction < 1.0f)
				{
					const float fit_frac = (fit_move.startsolid || fit_move.allsolid) ? 0.f : fit_move.fraction;

					// WIDTH: detect whether a clean two-sided gap is dead ahead. Cast two FORWARD
					// probes at the body's left/right edges: if BOTH flanks are walled ahead, the
					// opening is narrower than the body - a head-on pinch we must thread. This is now
					// only a HINT (a "fast-path"): when set it lets even a small progress gain trigger
					// the squeeze below. It is NOT required - a gap met at an angle leaves one flank
					// open and won't set it, but the progress fallback below still narrows there.
					// Gating narrowing on the pinch ALONE was the regression: bosses refused to
					// squeeze unless aimed dead-straight at the opening. Probing forward (not sideways
					// at the center) detects the pinch while still approaching the gap, before the
					// center is between the jambs.
					bool lateral_pinched = false;
					{
						vec3_t mdir = { move[0], move[1], 0.f };
						const float mlen = std::sqrt(mdir[0] * mdir[0] + mdir[1] * mdir[1]);
						if (mlen > 0.01f)
						{
							mdir[0] /= mlen; mdir[1] /= mlen;
							const vec3_t perp = { -mdir[1], mdir[0], 0.f };
							const float side = std::abs(perp[0]) * half_x + std::abs(perp[1]) * half_y; // real half-width across travel
							const float fwd  = std::abs(mdir[0]) * half_x + std::abs(mdir[1]) * half_y; // real half-length along travel
							const vec3_t lateral = perp * side;                                          // out to each body edge
							const vec3_t ahead   = mdir * (fwd + mlen + SQUEEZE_PINCH_LOOKAHEAD);         // forward past the leading edge / this step
							const bool left_blocked  = gi.traceline(oldorg + lateral, oldorg + lateral + ahead, ent, mask).fraction < 1.0f;
							const bool right_blocked = gi.traceline(oldorg - lateral, oldorg - lateral + ahead, ent, mask).fraction < 1.0f;
							lateral_pinched = left_blocked && right_blocked;
						}
					}

					if (max_ins_x > 0.f || max_ins_y > 0.f)
					{
						// Does narrowing (at current height) help clear more of the move? Trace the
						// fully-narrow box and compare against the full-width fit. Narrow when it gains:
						//   - pinch fast-path: a clean head-on gap was detected, so even a small gain
						//     (>0.02) counts - we know we have to thread it;
						//   - progress fallback (the original test): require a clear >0.1 gain. This is
						//     what catches angled / asymmetric gaps the head-on probe misses, and is the
						//     behavior that was lost when narrowing got gated on the pinch alone.
						box_at(1.0f, t_fit, tmin, tmax);
						trace_t wmin = gi.trace(oldorg, tmin, tmax, oldorg + move, ent, mask);
						const float min_gain = lateral_pinched ? 0.02f : 0.1f;
						if (!wmin.startsolid && !wmin.allsolid && wmin.fraction > fit_frac + min_gain)
						{
							const float wgoal = wmin.fraction - 0.02f;
							float lo = t_fit, hi = 1.f;
							for (int i = 0; i < 5; i++)
							{
								const float midt = (lo + hi) * 0.5f;
								box_at(midt, t_fit, tmin, tmax);
								trace_t mt = gi.trace(oldorg, tmin, tmax, oldorg + move, ent, mask);
								if (!mt.startsolid && !mt.allsolid && mt.fraction >= wgoal)
									hi = midt; // enough narrowing - keep more width
								else
									lo = midt; // not enough - narrow more
							}
							txy_final = hi;
						}
					}

					// HEIGHT: duck whenever lowering (at the chosen width) clears the way - a low
					// ceiling / overhang needs no side walls, so this is NOT gated on the pinch test.
					if (max_ins_z > 0.f)
					{
						box_at(txy_final, t_fit, tmin, tmax);
						trace_t hcur = gi.trace(oldorg, tmin, tmax, oldorg + move, ent, mask);
						const float hcur_frac = (hcur.startsolid || hcur.allsolid) ? 0.f : hcur.fraction;
						box_at(txy_final, 1.0f, tmin, tmax);
						trace_t hmin = gi.trace(oldorg, tmin, tmax, oldorg + move, ent, mask);
						if (!hmin.startsolid && !hmin.allsolid && hmin.fraction > hcur_frac + 0.1f)
						{
							const float hgoal = hmin.fraction - 0.02f;
							float lo = t_fit, hi = 1.f;
							for (int i = 0; i < 5; i++)
							{
								const float midt = (lo + hi) * 0.5f;
								box_at(txy_final, midt, tmin, tmax);
								trace_t mt = gi.trace(oldorg, tmin, tmax, oldorg + move, ent, mask);
								if (!mt.startsolid && !mt.allsolid && mt.fraction >= hgoal)
									hi = midt; // enough ducking - keep more height
								else
									lo = midt; // not enough - duck more
							}
							tz_final = hi;
						}
					}
				}

				box_at(txy_final, tz_final, want_mins, want_maxs);
			}

			// Apply only if the box actually changed (avoids needless relinks while size is stable).
			const vec3_t new_sq = { orig_maxs[0] - want_maxs[0], orig_maxs[1] - want_maxs[1], orig_maxs[2] - want_maxs[2] };
			if (new_sq[0] != sq[0] || new_sq[1] != sq[1] || new_sq[2] != sq[2])
			{
				ent->mins = want_mins;
				ent->maxs = want_maxs;
				SetMonsterSqueeze(ent, new_sq);
				gi.linkentity(ent);
			}
		}
	}

	vec3_t start_up = oldorg + ent->gravityVector * (-1 * stepsize);

	start_up = gi.trace(oldorg, ent->mins, ent->maxs, start_up, ent, mask).endpos;

	vec3_t const end_up = start_up + move;

	trace_t up_trace = gi.trace(start_up, ent->mins, ent->maxs, end_up, ent, mask);

	if (up_trace.startsolid)
	{
		start_up += ent->gravityVector * (-1 * stepsize);
		up_trace = gi.trace(start_up, ent->mins, ent->maxs, end_up, ent, mask);
	}

	vec3_t const start_fwd = oldorg;
	vec3_t const end_fwd = start_fwd + move;

	trace_t fwd_trace = gi.trace(start_fwd, ent->mins, ent->maxs, end_fwd, ent, mask);

	if (fwd_trace.startsolid)
	{
		start_up += ent->gravityVector * (-1 * stepsize);
		fwd_trace = gi.trace(start_fwd, ent->mins, ent->maxs, end_fwd, ent, mask);
	}

	// pick the one that went farther
	trace_t& chosen_forward = (up_trace.fraction > fwd_trace.fraction) ? up_trace : fwd_trace;

	if (chosen_forward.startsolid || chosen_forward.allsolid)
		return false;

	int32_t steps = 1;
	bool stepped = false;

	if (up_trace.fraction > fwd_trace.fraction)
		steps = 2;

	// step us down
	vec3_t const end = chosen_forward.endpos + (ent->gravityVector * (steps * stepsize));
	trace_t trace = gi.trace(chosen_forward.endpos, ent->mins, ent->maxs, end, ent, mask);

	if (std::abs(ent->s.origin.z - trace.endpos.z) > 8.f)
		stepped = true;

	// Paril: improved the water handling here.
	// monsters are okay with stepping into water
	// up to their waist.
	if (ent->waterlevel <= WATER_WAIST)
	{
		water_level_t end_waterlevel;
		contents_t	  end_watertype;
		M_CatagorizePosition(ent, trace.endpos, end_waterlevel, end_watertype);

		// don't go into deep liquids or
		// slime/lava voluntarily
		if (end_watertype & (CONTENTS_SLIME | CONTENTS_LAVA) ||
			end_waterlevel > WATER_WAIST)
			return false;
	}

	if (trace.fraction == 1)
	{
		// if monster had the ground pulled out, go ahead and fall
		if (ent->flags & FL_PARTIALGROUND)
		{
			ent->s.origin += move;
			if (relink)
			{
				gi.linkentity(ent);
				if (ShouldEnableTriggers(ent))
					G_TouchTriggers(ent);
			}
			ent->groundentity = nullptr;
			
			// Store successful move direction for momentum
			if ((ent->svflags & SVF_MONSTER) && move.lengthSquared() > 1.0f)
				ent->monsterinfo.last_valid_move = move.normalized();
			
			return true;
		}
		// [Paril-KEX] allow dead monsters to "fall" off of edges in their death animation
		else if (!ent->spawnflags.has(SPAWNFLAG_MONSTER_SUPER_STEP) && ent->health > 0)
			return false; // walked off an edge
	}

	// [Paril-KEX] if we didn't move at all (or barely moved), don't count it
	if ((trace.endpos - oldorg).length() < move.length() * MOVEMENT_PROGRESS_THRESHOLD)
	{
		ent->monsterinfo.bad_move_time = level.time + BAD_MOVE_TIME_PENALTY;

		// If we're wedged while squeezed (bosses only), react immediately instead of waiting out
		// the bump cooldown - otherwise a shrunk monster grinds into the wall (model clipping)
		// instead of turning / backing out.
		const vec3_t& bsq = ent->monsterinfo.bbox_squeeze;
		const bool squeezed = (bsq[0] != 0.f || bsq[1] != 0.f || bsq[2] != 0.f);

		// [Horde] Smart wall-stuck escape for normal (non-boss) ground monsters. They don't squeeze
		// or get pushed off walls, so instead they remember how long they've been grinding this
		// wall. Once that passes WALL_STUCK_TURN_THRESHOLD, commit to a deliberate turn: slide fully
		// toward the open side, or - if nose-on with nowhere to slide - just look the other way.
		// We return true so the new heading sticks: SV_StepDirection keeps a turned ideal_yaw on a
		// true return but restores the old one on false, and the caller then won't immediately
		// re-aim at the goal and march us straight back into the same wall. The gentle per-frame
		// slide below still handles short grinds; this only escalates the persistent ones.
		if ((ent->svflags & SVF_MONSTER) && !ent->monsterinfo.IS_BOSS && ent->health > 0 &&
			chosen_forward.fraction < 1.0f)
		{
			if (ent->monsterinfo.wall_stuck_time == 0_ms)
				ent->monsterinfo.wall_stuck_time = level.time; // start of a fresh grind
			else if (level.time - ent->monsterinfo.wall_stuck_time >= WALL_STUCK_TURN_THRESHOLD)
			{
				vec3_t turn_dir = SlideClipVelocity(AngleVectors(vec3_t{ 0.f, ent->ideal_yaw, 0.f }).forward, chosen_forward.plane.normal, 1.0f);
				if (turn_dir.lengthSquared() > 0.1f)
					ent->ideal_yaw = vectoyaw(turn_dir);                // commit fully along the wall
				else
					ent->ideal_yaw = anglemod(ent->ideal_yaw + 180.f);  // dead-on: turn around

				ent->monsterinfo.wall_stuck_time = level.time;          // re-arm; turn again only if still stuck
				ent->monsterinfo.bump_time = level.time + BUMP_TIME_DELAY;
				ent->monsterinfo.random_change_time = level.time + RANDOM_CHANGE_BASE;
				return true;
			}
		}

		// Improved collision recovery with smooth yaw interpolation
		if ((ent->monsterinfo.bump_time < level.time || squeezed) && chosen_forward.fraction < 1.0f)
		{
			// Calculate slide velocity
			vec3_t slide_dir = SlideClipVelocity(AngleVectors(vec3_t{ 0.f, ent->ideal_yaw, 0.f }).forward, chosen_forward.plane.normal, 1.0f);

			// Validate the slide direction
			if (slide_dir.lengthSquared() > 0.1f)
			{
				float new_yaw = vectoyaw(slide_dir);

				// Check if new direction is significantly different and not just oscillating
				float yaw_diff = new_yaw - ent->ideal_yaw;
				// Normalize to -180 to 180 range
				if (yaw_diff > 180.f)
					yaw_diff -= 360.f;
				else if (yaw_diff < -180.f)
					yaw_diff += 360.f;

				float abs_yaw_diff = std::abs(yaw_diff);
				if (abs_yaw_diff > DIRECTION_YAW_MIN_DIFF && abs_yaw_diff < DIRECTION_YAW_MAX_DIFF)
				{
					// PERFORMANCE FIX: Smooth yaw interpolation instead of instant snap
					// Apply 40% of the rotation per frame to eliminate jerky wall-sliding
					// This creates natural-looking turns while maintaining responsiveness
					ent->ideal_yaw += yaw_diff * 0.4f;
					// Normalize back to 0-360 range
					ent->ideal_yaw = anglemod(ent->ideal_yaw);

					ent->monsterinfo.random_change_time = level.time + RANDOM_CHANGE_BASE;
					ent->monsterinfo.bump_time = level.time + BUMP_TIME_DELAY;
					return true;
				}
			}

			// If slide failed, try using last valid move with slight variation
			if (ent->monsterinfo.last_valid_move.lengthSquared() > 0.1f)
			{
				float variation = frandom(-DIRECTION_MOMENTUM_VARIATION, DIRECTION_MOMENTUM_VARIATION);
				ent->ideal_yaw = vectoyaw(ent->monsterinfo.last_valid_move) + variation;
				ent->monsterinfo.bump_time = level.time + BUMP_TIME_FALLBACK;
			}
		}

		return false;
	}

	// Made real forward progress this step, so we're no longer grinding a wall - clear the
	// non-boss wall-stuck timer. (Passing the "barely moved" test above is the truest signal.)
	if (ent->svflags & SVF_MONSTER)
		ent->monsterinfo.wall_stuck_time = 0_ms;

	// check point traces down for dangling corners
	ent->s.origin = trace.endpos;

	// PGM
	//  PMM - don't bother with bad areas if we're dead
	if (ent->health > 0)
	{
		// use AI_BLOCKED to tell the calling layer that we're now mad at a tesla or sentrygun
		new_bad = CheckForBadArea(ent);
		if (!current_bad && new_bad)
		{
			if (new_bad->owner)
			{
				if (horde::IsSpecialType(new_bad->owner, horde::SpecialEntityTypeID::TESLA_MINE) || horde::IsSpecialType(new_bad->owner, horde::SpecialEntityTypeID::SENTRY_GUN))
				{
					if ((!(ent->enemy)) || (!(ent->enemy->inuse)))
					{
						TargetTesla(ent, new_bad->owner);
						ent->monsterinfo.aiflags |= AI_BLOCKED;
					}
					else if (horde::IsSpecialType(ent->enemy, horde::SpecialEntityTypeID::TESLA_MINE) || horde::IsMonsterType(ent->enemy, horde::MonsterTypeID::SENTRYGUN))
					{
						// already targeting a tesla or sentrygun, do nothing
					}
					else if ((ent->enemy) && (ent->enemy->client))
					{
						if (!visible(ent, ent->enemy))
						{
							TargetTesla(ent, new_bad->owner);
							ent->monsterinfo.aiflags |= AI_BLOCKED;
						}
					}
					else
					{
						TargetTesla(ent, new_bad->owner);
						ent->monsterinfo.aiflags |= AI_BLOCKED;
					}
				}
			}

			ent->s.origin = oldorg;
			return false;
		}
	}

	// PGM

	if (!M_CheckBottom(ent))
	{
		if (ent->flags & FL_PARTIALGROUND)
		{ // entity had floor mostly pulled out from underneath it
			// and is trying to correct
			if (relink)
			{
				gi.linkentity(ent);
				if (ShouldEnableTriggers(ent))
					G_TouchTriggers(ent);
			}
			
			// Store successful move direction
			if ((ent->svflags & SVF_MONSTER) && move.lengthSquared() > 1.0f)
				ent->monsterinfo.last_valid_move = move.normalized();
			
			return true;
		}

		// walked off an edge that wasn't a stairway
		ent->s.origin = oldorg;
		return false;
	}

	if (ent->spawnflags.has(SPAWNFLAG_MONSTER_SUPER_STEP) && ent->health > 0)
	{
		if (!ent->groundentity || ent->groundentity->solid == SOLID_BSP)
		{
			if (!(trace.ent->solid == SOLID_BSP))
			{
				// walked off an edge
				ent->s.origin = oldorg;
				M_CheckGround(ent, G_GetClipMask(ent));
				return false;
			}
		}
	}

	// [Paril-KEX]
	M_CheckGround(ent, G_GetClipMask(ent));

	if (!ent->groundentity)
	{
		// walked off an edge
		ent->s.origin = oldorg;
		M_CheckGround(ent, G_GetClipMask(ent));
		return false;
	}

	if (ent->flags & FL_PARTIALGROUND)
	{
		ent->flags &= ~FL_PARTIALGROUND;
	}
	ent->groundentity = trace.ent;
	ent->groundentity_linkcount = trace.ent->linkcount;

	// the move is ok - store successful move direction for momentum
	if ((ent->svflags & SVF_MONSTER) && move.lengthSquared() > 1.0f)
		ent->monsterinfo.last_valid_move = move.normalized();
	
	if (relink)
	{
		gi.linkentity(ent);

		// [Paril-KEX] this is something N64 does to avoid doors opening
		// at the start of a level, which triggers some monsters to spawn.
		if (ShouldEnableTriggers(ent))
			if (!level.is_n64 || level.time > FRAME_TIME_S)
				G_TouchTriggers(ent);
	}

	if (stepped)
		ent->s.renderfx |= RF_STAIR_STEP;

	if (trace.fraction < 1.f)
		G_Impact(ent, trace);

	// **HORDE OPTIMIZATION**: Reset damage timeout only if monster can see enemy or took damage
	if (g_horde->integer && (ent->svflags & SVF_MONSTER) &&
		((ent->enemy && ent->enemy->inuse && visible(ent, ent->enemy, false)) || ent->health < ent->max_health))
	{
		ent->monsterinfo.react_to_damage_time = level.time + random_time(REACT_DAMAGE_MIN, REACT_DAMAGE_MAX);
	}

	return true;
}

// check if a movement would succeed
bool ai_check_move(edict_t* self, float dist)
{
	if (ai_movement_disabled->integer) {
		return false;
	}

	float const yaw_rad = DEG2RAD(self->s.angles[YAW]);
	vec3_t const move = {
		cosf(yaw_rad) * dist,
		sinf(yaw_rad) * dist,
		0
	};

	vec3_t const old_origin = self->s.origin;

	// This is a pure probe - SV_movestep's squeeze logic may resize the box, so save and
	// restore the real box (not just the origin) so a trial move can't leave us shrunk.
	vec3_t const old_mins = self->mins, old_maxs = self->maxs;
	vec3_t const old_squeeze = self->monsterinfo.bbox_squeeze;

	if (!SV_movestep(self, move, false))
	{
		self->mins = old_mins;
		self->maxs = old_maxs;
		SetMonsterSqueeze(self, old_squeeze); // SV_movestep may have re-squeezed; restore counter too
		gi.linkentity(self);
		return false;
	}

	self->s.origin = old_origin;
	self->mins = old_mins;
	self->maxs = old_maxs;
	SetMonsterSqueeze(self, old_squeeze); // SV_movestep may have re-squeezed; restore counter too
	gi.linkentity(self);
	return true;
}

//============================================================================

/*
===============
M_ChangeYaw

===============
*/
void M_ChangeYaw(edict_t* ent)
{
	float ideal;
	float current;
	float move;
	float speed;

	current = anglemod(ent->s.angles[YAW]);
	ideal = ent->ideal_yaw;

	if (current == ideal)
		return;

	move = ideal - current;
	// [Paril-KEX] high tick rate
	speed = ent->yaw_speed / (gi.tick_rate / 10);

	if (ideal > current)
	{
		if (move >= 180)
			move = move - 360;
	}
	else
	{
		if (move <= -180)
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

	ent->s.angles[YAW] = anglemod(current + move);
}

/*
======================
SV_StepDirection
Turns to the movement direction, and walks the current distance if facing it.
Returns true if the movement was successful.
======================
*/
bool SV_StepDirection(edict_t* ent, float yaw, float dist, bool allow_no_turns)
{

	// Store original values for potential rollback
	const vec3_t oldorigin = ent->s.origin;
	const float old_ideal_yaw = ent->ideal_yaw;
	const float old_current_yaw = ent->s.angles[YAW];

	// Update entity yaw
	ent->ideal_yaw = yaw;
	M_ChangeYaw(ent);

	// Calculate movement vector
	const float rad_yaw = DEG2RAD(yaw);
	vec3_t const move = {
		cosf(rad_yaw) * dist,
		sinf(rad_yaw) * dist,
		0
	};

// Special handling for specific monster types
const bool is_widow = (horde::IsMonsterType(ent, horde::MonsterTypeID::WIDOW) ||
    horde::IsMonsterType(ent, horde::MonsterTypeID::WIDOW2) || horde::IsMonsterType(ent, horde::MonsterTypeID::WIDOW1));
	
	// Attempt movement
	if (SV_movestep(ent, move, false))
	{
		// Clear blocked flag on successful movement
		ent->monsterinfo.aiflags &= ~AI_BLOCKED;

		// Safety check for entity still being valid
		if (!ent->inuse)
			return true;

		// Check if entity is facing the right direction
		if (!is_widow && !FacingIdeal(ent))
		{
			// Revert position but maintain rotation if allowed
			ent->s.origin = oldorigin;
			M_CheckGround(ent, G_GetClipMask(ent));
			return allow_no_turns;
		}

		// Update entity state and check triggers
		gi.linkentity(ent);

		if (ShouldEnableTriggers(ent))
		{
			G_TouchTriggers(ent);
		}

		G_TouchProjectiles(ent, oldorigin);
		return true;
	}

	// Movement failed, restore original rotation
	ent->ideal_yaw = old_ideal_yaw;
	ent->s.angles[YAW] = old_current_yaw;

	// Update entity state and check triggers even on failure
	gi.linkentity(ent);

	if (!g_horde->integer || IsMorphed(ent))
	{
		G_TouchTriggers(ent);
	}

	return false;
}
/*
======================
SV_FixCheckBottom

======================
*/
void SV_FixCheckBottom(edict_t* ent)
{
	ent->flags |= FL_PARTIALGROUND;
}

/*
================
SV_NewChaseDir

================
*/
constexpr float DI_NODIR = -1;

bool SV_NewChaseDir(edict_t *actor, vec3_t pos, float dist)
{
	float deltax, deltay;
	float d[3];
	float tdir, olddir, turnaround;

	olddir = anglemod(truncf(actor->ideal_yaw / 45) * 45);
	turnaround = anglemod(olddir - 180);

	deltax = pos[0] - actor->s.origin[0];
	deltay = pos[1] - actor->s.origin[1];
	if (deltax > 10)
		d[1] = 0;
	else if (deltax < -10)
		d[1] = 180;
	else
		d[1] = DI_NODIR;
	if (deltay < -10)
		d[2] = 270;
	else if (deltay > 10)
		d[2] = 90;
	else
		d[2] = DI_NODIR;

	// try direct route
	if (d[1] != DI_NODIR && d[2] != DI_NODIR)
	{
		if (d[1] == 0)
			tdir = d[2] == 90 ? 45.f : 315.f;
		else
			tdir = d[2] == 90 ? 135.f : 215.f;

		if (tdir != turnaround && SV_StepDirection(actor, tdir, dist, false))
			return true;
	}

	// try other directions
	if (brandom() || std::abs(deltay) > std::abs(deltax))
	{
		tdir = d[1];
		d[1] = d[2];
		d[2] = tdir;
	}

	if (d[1] != DI_NODIR && d[1] != turnaround && SV_StepDirection(actor, d[1], dist, false))
		return true;

	if (d[2] != DI_NODIR && d[2] != turnaround && SV_StepDirection(actor, d[2], dist, false))
		return true;

	// ROGUE
	if (actor->monsterinfo.blocked)
	{
		if ((actor->inuse) && (actor->health > 0) && !(actor->monsterinfo.aiflags & AI_TARGET_ANGER))
		{
			// if block "succeeds", the actor will not move or turn.
			if (actor->monsterinfo.blocked(actor, dist))
			{
				actor->monsterinfo.move_block_counter = -2;
				return true;
			}

			// we couldn't step; instead of running endlessly in our current
			// spot, try switching to node navigation temporarily to get to
			// where we need to go.
			if (!(actor->monsterinfo.aiflags & (AI_LOST_SIGHT | AI_COMBAT_POINT | AI_TARGET_ANGER | AI_PATHING | AI_TEMP_MELEE_COMBAT | AI_NO_PATH_FINDING)))
			{
				if (++actor->monsterinfo.move_block_counter > 2)
				{
					actor->monsterinfo.aiflags |= AI_TEMP_MELEE_COMBAT;
					actor->monsterinfo.move_block_change_time = level.time + 3_sec;
					actor->monsterinfo.move_block_counter = 0;
				}
			}
		}
	}
	// ROGUE

	/* there is no direct path to the player, so pick another direction */

	if (olddir != DI_NODIR && SV_StepDirection(actor, olddir, dist, false))
		return true;

	if (brandom()) /*randomly determine direction of search*/
	{
		for (tdir = 0; tdir <= 315; tdir += 45)
			if (tdir != turnaround && SV_StepDirection(actor, tdir, dist, false))
				return true;
	}
	else
	{
		for (tdir = 315; tdir >= 0; tdir -= 45)
			if (tdir != turnaround && SV_StepDirection(actor, tdir, dist, false))
				return true;
	}

	if (turnaround != DI_NODIR && SV_StepDirection(actor, turnaround, dist, false))
		return true;

	// Every direction failed - we're boxed in (typically wedged into a corner). Don't re-roll a
	// brand-new random yaw: doing that every think makes a cornered monster pirouette in place
	// ("moving in circles"). Instead retreat the way we came (turnaround) and lock that heading
	// for a beat via random_change_time, so M_MoveToGoal / M_NavPathToGoal hold ideal_yaw instead
	// of immediately re-aiming at the (still-blocked) goal and walking us straight back in. The
	// monster ends up facing a stable escape direction; if it stays wedged, the wall-stuck timer
	// (SV_movestep) and the teleport failsafe take over rather than a frantic spin.
	actor->ideal_yaw = (turnaround != DI_NODIR) ? turnaround : anglemod(actor->ideal_yaw + 180.f);

	// Add a pause so we don't try-and-fail (or re-search all 8 dirs) every single frame.
	actor->monsterinfo.random_change_time = level.time + random_time(RANDOM_CHANGE_MIN, RANDOM_CHANGE_MAX);
	actor->monsterinfo.bump_time = level.time + BUMP_TIME_FALLBACK;
	actor->monsterinfo.bad_move_time = level.time + BAD_MOVE_TIME_PENALTY;

	// if a bridge was pulled out from underneath a monster, it may not have
	// a valid standing position at all

	if (!M_CheckBottom(actor))
		SV_FixCheckBottom(actor);

	return false;
}
/*
======================
SV_CloseEnough

======================
*/
bool SV_CloseEnough(edict_t* ent, edict_t* goal, float dist)
{
	int i;

	for (i = 0; i < 3; i++)
	{
		if (goal->absmin[i] > ent->absmax[i] + dist)
			return false;
		if (goal->absmax[i] < ent->absmin[i] - dist)
			return false;
	}
	return true;
}

static bool M_NavPathToGoal(edict_t* self, float dist, const vec3_t& goal)
{

	// mark us as *trying* now (nav_pos is valid)
	self->monsterinfo.aiflags |= AI_PATHING;

	vec3_t& path_to = (self->monsterinfo.nav_path.returnCode == PathReturnCode::TraversalPending) ?
		self->monsterinfo.nav_path.secondMovePoint : self->monsterinfo.nav_path.firstMovePoint;

	// Calculate monster's actual dimensions and collision bounds
	const float height = self->maxs[2] - self->mins[2];
	const float width = std::max(self->maxs[0] - self->mins[0], self->maxs[1] - self->mins[1]);
	const float ground_offset = self->mins[2] - PLAYER_MINS[2];

	vec3_t const ground_origin = self->s.origin + vec3_t{ 0.f, 0.f, ground_offset };
	vec3_t const mon_mins = ground_origin + PLAYER_MINS;
	vec3_t const mon_maxs = ground_origin + PLAYER_MAXS;

	// --- Progress-based path recalculation ---
	// Track if monster is actually making progress toward goal
	const vec3_t goal_pos = self->enemy ? self->enemy->s.origin : self->goalentity->s.origin;
	const float current_dist_to_goal = (goal_pos - self->s.origin).length();
	bool force_recalc = false;
	bool is_stuck = false;

	// Check progress every PATH_PROGRESS_CHECK_TIME
	if (self->monsterinfo.path_progress_time <= level.time)
	{
		self->monsterinfo.path_progress_time = level.time + PATH_PROGRESS_CHECK_TIME;

		// Compare current distance to goal with last recorded distance
		if (self->monsterinfo.path_last_goal_dist > 0)
		{
			float progress = self->monsterinfo.path_last_goal_dist - current_dist_to_goal;

			// If we haven't moved significantly closer to goal, we might be stuck
			if (progress < PATH_PROGRESS_MIN_DISTANCE)
			{
				self->monsterinfo.path_no_progress_time += PATH_PROGRESS_CHECK_TIME;

				// If stuck for too long, force path recalculation
				if (self->monsterinfo.path_no_progress_time >= PATH_STUCK_RECALC_TIME)
				{
					force_recalc = true;
					is_stuck = true;
					self->monsterinfo.path_no_progress_time = 0_ms;
				}
			}
			else
			{
				// Making progress, reset stuck counter
				self->monsterinfo.path_no_progress_time = 0_ms;
			}
		}

		self->monsterinfo.path_last_goal_dist = current_dist_to_goal;
	}

	// Check if we need to recalculate path
	const bool path_expired = self->monsterinfo.nav_path_cache_time <= level.time;
	const bool path_intersecting = self->monsterinfo.nav_path.returnCode != PathReturnCode::TraversalPending &&
		boxes_intersect(mon_mins, mon_maxs, path_to, path_to);

	if (path_expired || path_intersecting || force_recalc)
	{
		PathRequest request;

		// Set goal based on enemy or goalentity
		request.goal = goal_pos;
		request.moveDist = dist;
		request.start = self->s.origin;
		request.pathFlags = PathFlags::Walk;

		// Set node search parameters based on actual monster dimensions
		request.nodeSearch.minHeight = -(height * 1.5f); // Extra margin for slopes/stairs
		request.nodeSearch.maxHeight = height * 2;       // Extra height for jumps/drops

		// Debug drawing if enabled
		if (g_debug_monster_paths->integer == 1)
			request.debugging.drawTime = gi.frame_time_s;

		// Special handling for specific monster types
		if (horde::IsMonsterType(self, horde::MonsterTypeID::GUARDIAN) || horde::IsMonsterType(self, horde::MonsterTypeID::PSX_GUARDIAN))
		{
			request.nodeSearch.radius = std::max(2048.f, width * 4);
		}

		// Configure movement capabilities
		if (self->monsterinfo.can_jump || (self->flags & FL_FLY))
		{
			if (self->monsterinfo.jump_height)
			{
				request.pathFlags |= PathFlags::BarrierJump;
				request.traversals.jumpHeight = std::min(self->monsterinfo.jump_height, height * 3);
			}
			if (self->monsterinfo.drop_height)
			{
				request.pathFlags |= PathFlags::WalkOffLedge;
				request.traversals.dropHeight = std::min(self->monsterinfo.drop_height, height * 4);
			}
		}

		// Flying monsters get special treatment
		if (self->flags & FL_FLY)
		{
			const float vertical_limit = std::max(8192.f, height * 8);
			request.nodeSearch.maxHeight = request.nodeSearch.minHeight = vertical_limit;
			request.pathFlags |= PathFlags::LongJump;
		}

		if (!gi.GetPathToGoal(request, self->monsterinfo.nav_path))
		{
			// fatal error, don't bother ever trying nodes
			if (self->monsterinfo.nav_path.returnCode == PathReturnCode::NoNavAvailable)
				self->monsterinfo.aiflags |= AI_NO_PATH_FINDING;
			return false;
		}

		// Use shorter cache time if we were stuck, to recover faster
		self->monsterinfo.nav_path_cache_time = level.time + (is_stuck ? PATH_CACHE_TIME_STUCK : PATH_CACHE_TIME_NORMAL);

		// Reset progress tracking for new path
		self->monsterinfo.path_last_goal_dist = current_dist_to_goal;
	}

	// Store original yaw values for potential restoration
	float yaw;
	const float old_yaw = self->s.angles[YAW];
	const float old_ideal_yaw = self->ideal_yaw;

	// Calculate movement direction with yaw smoothing to prevent diagonal stuttering
	if (self->monsterinfo.random_change_time >= level.time &&
		!(self->monsterinfo.aiflags & AI_ALTERNATE_FLY))
	{
		yaw = self->ideal_yaw;
	}
	else
	{
		vec3_t dir = path_to - self->s.origin;
		dir.normalize();
		float target_yaw = vectoyaw(dir);

		// Smooth yaw transition to prevent jittery diagonal movement
		// Only apply smoothing if we already have a valid ideal_yaw direction
		if (old_ideal_yaw >= 0)
		{
			float yaw_diff = target_yaw - old_ideal_yaw;
			// Normalize to -180 to 180 range
			if (yaw_diff > 180.f)
				yaw_diff -= 360.f;
			else if (yaw_diff < -180.f)
				yaw_diff += 360.f;

			// Only smooth small changes (< 45°) - larger changes should be immediate
			// This prevents stuttering from micro-adjustments while allowing quick turns
			if (std::abs(yaw_diff) < 45.f)
				yaw = anglemod(old_ideal_yaw + yaw_diff * 0.6f);  // 60% blend toward target
			else
				yaw = target_yaw;
		}
		else
		{
			yaw = target_yaw;
		}
	}

	// Try primary movement
	if (!SV_StepDirection(self, yaw, dist, true))
	{
		if (!self->inuse)
			return false;

		// Handle blocked state
		if (self->monsterinfo.blocked && !(self->monsterinfo.aiflags & AI_TARGET_ANGER))
		{
			if ((self->inuse) && (self->health > 0))
			{
				// Restore original yaw for blocked handling
				self->s.angles[YAW] = old_yaw;
				self->ideal_yaw = old_ideal_yaw;
				if (self->monsterinfo.blocked(self, dist))
					return true;
			}
		}

		// Try movement to first point
		if (self->monsterinfo.random_change_time >= level.time)
			yaw = self->ideal_yaw;
		else
		{
			vec3_t dir = self->monsterinfo.nav_path.firstMovePoint - self->s.origin;
			dir.normalize();
			yaw = vectoyaw(dir);
		}

		if (!SV_StepDirection(self, yaw, dist, true))
		{
			// Handle blocked flag
			if (self->monsterinfo.aiflags & AI_BLOCKED)
			{
				self->monsterinfo.aiflags &= ~AI_BLOCKED;
				return true;
			}

			// Try random direction change
			if (self->monsterinfo.random_change_time < level.time && self->inuse)
			{
				self->monsterinfo.random_change_time = level.time + 1500_ms;
				if (SV_NewChaseDir(self, path_to, dist))
					return true;
			}

			// Update blocked counter
			self->monsterinfo.path_blocked_counter += FRAME_TIME_S * 3;

			// Check if we've been blocked too long
			if (self->monsterinfo.path_blocked_counter > 1.5_sec)
				return false;
		}
	}

	return true;
}
/*
=============
M_MoveToPath

Advanced movement code that use the bots pathfinder if allowed and conditions are right.
Feel free to add any other conditions needed.
=============
*/
static bool M_MoveToPath(edict_t* self, float dist)
{
	// 1. Mejorar las condiciones iniciales con comprobaciones más precisas
	if (self->flags & FL_STATIONARY)
		return false;
	else if (self->monsterinfo.aiflags & AI_NO_PATH_FINDING)
		return false;
	else if (self->monsterinfo.path_wait_time > level.time)
		return false;
	else if (!self->enemy || !self->enemy->inuse)  // Añadir check de inuse
		return false;
	else if (self->enemy->client &&
		self->enemy->client->invisible_time > level.time &&
		self->enemy->client->invisibility_fade_time <= level.time)
		return false;
	else if (self->monsterinfo.attack_state >= AS_MISSILE)
		return true;

	combat_style_t style = self->monsterinfo.combat_style;
	if (self->monsterinfo.aiflags & AI_TEMP_MELEE_COMBAT)
		style = COMBAT_MELEE;

	// 2. Mejorar la lógica de visibilidad y rango
	if (visible(self, self->enemy, false)) {
		float const dist_to_enemy = range_to(self, self->enemy);
		float const height_diff = fabs(self->s.origin.z - self->enemy->s.origin.z);
		float const max_step_height = max(self->maxs.z, -self->mins.z);

		if ((self->flags & (FL_SWIM | FL_FLY)) || style == COMBAT_RANGED) {
			return false;  // Mantener comportamiento normal para voladores/nadadores
		}
		else if (style == COMBAT_MELEE) {
			// Ajustar rangos para melee más agresivo
			if (dist_to_enemy > 240.f || height_diff > max_step_height) {
				if (M_NavPathToGoal(self, dist, self->enemy->s.origin)) {
					self->monsterinfo.path_blocked_counter = 0_ms;  // Reset contador al encontrar camino
					return true;
				}
				self->monsterinfo.aiflags &= ~AI_TEMP_MELEE_COMBAT;
			}
			else {
				self->monsterinfo.aiflags &= ~AI_TEMP_MELEE_COMBAT;
				return false;
			}
		}
		else if (style == COMBAT_MIXED) {
			// Ajustar rangos para combate mixto más dinámico
			if (dist_to_enemy > RANGE_NEAR || height_diff > max_step_height * 2.0f) {
				if (M_NavPathToGoal(self, dist, self->enemy->s.origin)) {
					self->monsterinfo.path_blocked_counter = 0_ms;
					return true;
				}
			}
			else {
				return false;
			}
		}
	}
	else {
		// 3. Mejorar el manejo de enemigos no visibles
		if (M_NavPathToGoal(self, dist, self->enemy->s.origin)) {
			self->monsterinfo.path_blocked_counter = 0_ms;
			return true;
		}
	}

	// 4. Mejorar el manejo de errores y bloqueos
	if (!self->inuse)
		return false;

	if (self->monsterinfo.nav_path.returnCode > PathReturnCode::StartPathErrors) {
		// Incrementar tiempo de espera si hay múltiples errores
		self->monsterinfo.path_wait_time = level.time + 10_sec;
		return false;
	}

	// 5. Ajustar el contador de bloqueo para ser más responsivo
	self->monsterinfo.path_blocked_counter += FRAME_TIME_S * 2;  // Reducido de 3 a 2

	if (self->monsterinfo.path_blocked_counter > 3_sec) {  // Reducido de 5 a 3 segundos
		self->monsterinfo.path_blocked_counter = 0_ms;
		self->monsterinfo.path_wait_time = level.time + 3_sec;  // Reducido de 5 a 3 segundos
		return false;
	}

	return true;
}
/*
======================
M_MoveToGoal
======================
*/
void M_MoveToGoal(edict_t* ent, float dist)
{
	if (ai_movement_disabled->integer) {
		if (!FacingIdeal(ent)) {
			M_ChangeYaw(ent);
		} // mal: don't move, but still face toward target
		return;
	}

	edict_t* goal;

	goal = ent->goalentity;

	if (!ent->groundentity && !(ent->flags & (FL_FLY | FL_SWIM)))
		return;
	// ???
	else if (!goal)
		return;

	// [Paril-KEX] try paths if we can't see the enemy
	if (!(ent->monsterinfo.aiflags & AI_COMBAT_POINT) && ent->monsterinfo.attack_state < AS_MISSILE)
	{
		if (M_MoveToPath(ent, dist))
		{
			ent->monsterinfo.path_blocked_counter = max(0_ms, ent->monsterinfo.path_blocked_counter - FRAME_TIME_S);
			return;
		}
	}

	ent->monsterinfo.aiflags &= ~AI_PATHING;

	//if (goal)
	//	gi.Draw_Point(goal->s.origin, 1.f, rgba_red, gi.frame_time_ms, false);

	// [Paril-KEX] dumb hack; in some n64 maps, the corners are way too high and
	// I'm too lazy to fix them individually in maps, so here's a game fix..
	if (!(goal->flags & FL_PARTIALGROUND) && !(ent->flags & (FL_FLY | FL_SWIM)) &&
		goal->classname && (!strcmp(goal->classname, "path_corner") || !strcmp(goal->classname, "point_combat")))
	{
		vec3_t p = goal->s.origin;
		p.z = ent->s.origin.z;

		if (boxes_intersect(ent->absmin, ent->absmax, p, p))
		{
			// mark this so we don't do it again later
			goal->flags |= FL_PARTIALGROUND;

			if (!boxes_intersect(ent->absmin, ent->absmax, goal->s.origin, goal->s.origin))
			{
				// move it if we would have touched it if the corner was lower
				goal->s.origin.z = p.z;
				gi.linkentity(goal);
			}
		}
	}

	// [Paril-KEX] if we have a straight shot to our target, just move
	// straight instead of trying to stick to invisible guide lines
	if ((ent->monsterinfo.bad_move_time <= level.time || (ent->monsterinfo.aiflags & AI_CHARGING)) && goal)
	{
		// Aim at the goal first, then check facing against THAT direction. If we just need
		// to turn (>45 deg), rotate and return without penalty - a needed turn is not a block.
		// (Previously the facing check used the old ideal_yaw, so SV_StepDirection below would
		// reset ideal_yaw to the goal, fail the re-check, and we'd wrongly slap on the 5s
		// bad_move penalty - the cause of lone monsters stuttering when tracking a target.)
		const float goal_yaw = vectoyaw((goal->s.origin - ent->s.origin).normalized());
		ent->ideal_yaw = goal_yaw;
		if (!FacingIdeal(ent))
		{
			M_ChangeYaw(ent);
			return;
		}

		// Monsters are non-solid to each other for movement in horde, so don't let a crowd
		// of them block the line-of-sight test and disable the fast straight path.
		contents_t los_mask = MASK_MONSTERSOLID;
		if (g_horde->integer)
			los_mask &= ~CONTENTS_MONSTER;

		trace_t const tr = gi.traceline(ent->s.origin, goal->s.origin, ent, los_mask);

		if (tr.fraction == 1.0f || tr.ent == goal)
		{
			if (SV_StepDirection(ent, goal_yaw, dist, false))
				return;
		}

		// We're facing the goal with a clear-ish shot but still couldn't step - a real block.
		// Don't try this for a while, *unless* we're going to a path corner.
		if (goal->classname && strcmp(goal->classname, "path_corner") && strcmp(goal->classname, "point_combat"))
		{
			ent->monsterinfo.bad_move_time = level.time + 5_sec;
			ent->monsterinfo.aiflags &= ~AI_CHARGING;
		}
	}

	// bump around...
	// Skip if still in bump time cooldown (prevents stuttering from rapid direction changes)
	if (ent->monsterinfo.bump_time > level.time)
	{
		M_ChangeYaw(ent); // Still face the target while waiting
		return;
	}

	if ((ent->monsterinfo.random_change_time <= level.time // random change time is up
		&& irandom(4) == 1 // random bump around
		&& !(ent->monsterinfo.aiflags & AI_CHARGING) // PMM - charging monsters (AI_CHARGING) don't deflect unless they have to
		&& !((ent->monsterinfo.aiflags & AI_ALTERNATE_FLY) && ent->enemy && !(ent->monsterinfo.aiflags & AI_LOST_SIGHT))) // alternate fly monsters don't do this either unless they have to
		|| !SV_StepDirection(ent, ent->ideal_yaw, dist, ent->monsterinfo.bad_move_time > level.time))
	{
		if (ent->monsterinfo.aiflags & AI_BLOCKED)
		{
			ent->monsterinfo.aiflags &= ~AI_BLOCKED;
			return;
		}
		ent->monsterinfo.random_change_time = level.time + random_time(RANDOM_CHANGE_MIN, RANDOM_CHANGE_MAX);
		SV_NewChaseDir(ent, goal->s.origin, dist);
		ent->monsterinfo.move_block_counter = 0;
	}
	else
		ent->monsterinfo.bad_move_time -= 250_ms;

	//vec3_t dir = AngleVectors({ 0.f, ent->ideal_yaw, 0.f }).forward;
	//gi.Draw_Line(ent->s.origin, ent->s.origin + (dir * 24), rgba_blue, gi.frame_time_ms, false);
}

/*
===============
Global Monster Jump Functions
These provide basic jump capability for monsters without custom jump animations
===============
*/

// Apply custom gravity while airborne - faster fall, normal rise
static void M_MonsterApplyJumpGravity(edict_t* self)
{
    if (self->velocity[2] < 0)
        self->gravity = 2.0f;  // Faster fall
    else
        self->gravity = 1.0f;  // Normal rise

    gi.linkentity(self);
}

// Generic monster jump - works without special animations
void M_MonsterJump(edict_t* self, float forward_vel, float up_vel)
{
    vec3_t forward, up;

    AngleVectors(self->s.angles, forward, nullptr, up);

    // Clear vertical velocity before applying new jump force
    self->velocity[2] = 0;

    self->velocity += (forward * forward_vel);
    self->velocity += (up * up_vel);

    self->groundentity = nullptr; // We are now airborne

    // Apply custom gravity immediately
    M_MonsterApplyJumpGravity(self);
}

// Global blocked callback for monsters without custom jump animations
MONSTERINFO_BLOCKED(M_MonsterBlocked) (edict_t* self, float dist) -> bool
{
    // Check for platforms first
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

/*
===============
M_walkmove
===============
*/
bool M_walkmove(edict_t* ent, float yaw, float dist)
{
	if (ai_movement_disabled->integer) {
		return false;
	}

	vec3_t move;
	// PMM
	bool retval;

	if (!ent->groundentity && !(ent->flags & (FL_FLY | FL_SWIM)))
		return false;

	yaw = DEG2RAD(yaw);

	move[0] = cosf(yaw) * dist;
	move[1] = sinf(yaw) * dist;
	move[2] = 0;

	// PMM
	retval = SV_movestep(ent, move, true);
	ent->monsterinfo.aiflags &= ~AI_BLOCKED;
	return retval;
}