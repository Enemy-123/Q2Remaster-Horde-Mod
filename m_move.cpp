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
// Bbox-aware separation: monsters repulse out to the SUM of their horizontal radii (+margin) so two
// of them never come to rest sharing a bounding box (they stay non-solid and can still pass through).
// Fixed personal space alone is too small for big monsters, which is how they end up stacked & stuck.
constexpr float BBOX_SEPARATION_MARGIN = 8.0f;                  // push slightly past bbox contact so they fully clear
constexpr float MONSTER_REPULSION_MAX_NEIGHBOR_RADIUS = 80.0f;  // search reach for the largest likely neighbor bbox

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
constexpr float REPULSION_STRENGTH_PATHING_UNSEEN = 0.3f; // nav-marching with enemy out of sight: mostly follow the path
constexpr gtime_t REPULSION_ENEMY_SEEN_RECENT = 2_sec;    // trail_time staleness that counts as "out of sight"

// --- Hard separation: summoned Stroggs deeply overlapping a DIFFERENT-team monster ---
// Lateral repulsion can't separate two mutual enemies (the push is anti-parallel to the goal, so
// its lateral component is ~0 and gets stripped). This adds a FULL (non-lateral) un-stacking push,
// gated to summoned stroggs only and only at DEEP overlap, so they un-pile yet can still close to
// melee. One-sided (only the strogg accumulates it) -> no symmetric ping-pong, normal crowd untouched.
constexpr float HARD_SEPARATION_DEEP_OVERLAP_FRACTION = 0.5f; // engage only when dist < personal_space * this (or boxes intersect)
constexpr float HARD_SEPARATION_MAX_FORCE = 24.0f; // peak per-neighbor push (matches REPULSION_MAX_FORCE)
constexpr float HARD_SEPARATION_STRENGTH = 1.0f;  // applied fraction (full force so it actually un-stacks)
constexpr float HARD_SEPARATION_STRENGTH_COMBAT = 1.0f;  // no penalty vs a live enemy: the falloff + MIN_FWD_FRACTION clamp keep it closing to melee, so a stuck cluster fighting one player still breaks apart
constexpr float HARD_SEPARATION_MIN_DIST = 4.0f;  // floor in falloff denominator (anti force-spike at near-zero distance)
constexpr float HARD_SEPARATION_STEP_CAP_FRACTION = 2.5f;  // cap total hard push to this * step length; >1 lets a wedged pile shove apart faster than the goal re-converges it

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

// Timing constants
constexpr gtime_t BUMP_TIME_DELAY = 200_ms;
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

// Path cache time (PSX parity): how long a solved path is committed before re-solving.
constexpr gtime_t PATH_CACHE_TIME_NORMAL = 2_sec;
// Re-solve the path once a monster has LANDED at an elevation this far from where the path was
// solved. All horde monsters get SUPER_STEP (stepsize 64), so they often climb a ledge directly
// instead of via the planned route, stranding the cached path on the level below; without this they
// keep aiming at the bypassed lower point until the 2s cache expires. Tunable: lower clears lower
// ledges faster but re-solves more often on stairs (harmless - single-route stairs re-solve to the
// same path).
constexpr float PATH_RESOLVE_Z_DELTA = 32.0f;

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

// TEMP DIAGNOSTIC filter: count live monster neighbors via the engine areagrid, to compare against
// the custom ProximityGrid's result (does the grid actually return the stacked monsters?).
static BoxEdictsResult_t RepulseDbg_BoxFilter(edict_t* hit, void* self_ptr)
{
	if (hit == self_ptr || !(hit->svflags & SVF_MONSTER) || hit->health <= 0 || hit->deadflag)
		return BoxEdictsResult_t::Skip;
	return BoxEdictsResult_t::Keep;
}

// Calculate repulsion vector from nearby monsters
static vec3_t CalculateMonsterRepulsion(edict_t* ent)
{
	vec3_t repulsion = { 0, 0, 0 };
	vec3_t hard_separation = { 0, 0, 0 }; // full (non-lateral) un-stack push, summoned strogg vs different-team only

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

	// Horizontal bounding radius of this monster (largest x/y half-extent). Used so the engagement
	// distance scales with the actual bbox - two monsters should separate until their boxes no longer
	// overlap, not just to a fixed personal space (which is smaller than a big monster's box).
	const float ent_radius = std::max({ ent->maxs.x, -ent->mins.x, ent->maxs.y, -ent->mins.y });

	// Search wide enough to find any monster whose bbox could overlap ours (our radius + the largest
	// likely neighbor radius + margin), floored at the original personal-space search.
	const float search_radius = std::max(personal_space * MONSTER_PERSONAL_SPACE_SEARCH_MULTIPLIER,
	                                      ent_radius + MONSTER_REPULSION_MAX_NEIGHBOR_RADIUS + BBOX_SEPARATION_MARGIN);
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

		//// Skip repulsion between summoned Stroggs - they can walk through each other
		//if (ent->monsterinfo.issummoned && other->monsterinfo.issummoned)
		//	continue;

		// Calculate distance using squared distance for performance
		vec3_t diff = ent->s.origin - other->s.origin;
		const float dist_sq = diff.lengthSquared();

		// Per-pair engagement: keep their bboxes from overlapping. Engage out to the sum of both
		// horizontal radii (+margin) so neither comes to rest inside the other's box, but never below
		// this monster's personal space. Repulsion only nudges the move vector - monsters stay non-solid
		// and can still pass THROUGH each other; they just won't settle stacked on one spot.
		const float other_radius = std::max({ other->maxs.x, -other->mins.x, other->maxs.y, -other->mins.y });
		const float pair_space = std::max(personal_space, ent_radius + other_radius + BBOX_SEPARATION_MARGIN);
		const float pair_space_sq = pair_space * pair_space;

		// Too far: ignore.
		if (dist_sq >= pair_space_sq)
			continue;

		// Separation direction + distance. Near-coincident (dist < ~1) is the WORST stack - exactly the
		// "stuck on the same bbox" case we most need to break - but diff is ~0 so it can't be normalized.
		// The old code early-SKIPPED this case (the divide-by-zero guard), which is why a perfectly
		// stacked pair never separated. Instead, synthesize a deterministic per-entity horizontal escape
		// direction (golden-angle from the entity number) so a stacked group fans out in distinct
		// directions rather than staying piled.
		float dist;
		vec3_t sep_dir;
		if (dist_sq < REPULSION_MIN_THRESHOLD_SQ)
		{
			dist = HARD_SEPARATION_MIN_DIST; // treat as contact for the falloff math
			const float ang = static_cast<float>(ent->s.number) * 2.39996323f; // golden angle (radians)
			sep_dir = { cosf(ang), sinf(ang), 0.0f };
		}
		else
		{
			dist = std::sqrt(dist_sq);
			sep_dir = diff * (1.0f / dist); // unit vector AWAY from the neighbor
		}

		// Calculate repulsion force - stronger when closer
		float strength = std::clamp((pair_space - dist) / pair_space, 0.0f, 1.0f);

		vec3_t push = sep_dir * (strength * REPULSION_MAX_FORCE);

		// Reduce vertical repulsion for ground monsters
		if (!(ent->flags & (FL_FLY | FL_SWIM)))
			push.z *= REPULSION_VERTICAL_DAMPING;

		repulsion += push;
		nearby_count++;

		// --- Hard separation: FULL (non-lateral) un-stack push, DEEP overlap only. Applied in
		// ApplyMonsterRepulsion without the lateral strip, so the along-goal separating component
		// survives - that's what un-stacks monsters whose goals point the same way (or at each other),
		// which plain lateral repulsion can't. Two one-sided cases (one-sided so a pair can't ping-pong):
		//   1. WE are a summoned strogg and the neighbor is a REAL enemy (no BF_FRIENDLY). Naturally
		//      one-sided: the enemy isn't summoned, so only the strogg accumulates the push. Un-stacks
		//      two mutual enemies grinding into each other.
		//   2. Two NORMAL enemy monsters (neither is an ally). A spawn cluster all chasing the SAME
		//      player has near-parallel goals, so lateral repulsion barely separates it; this un-piles
		//      them. Made one-sided via the entity-number tiebreaker (only the lower-numbered one steps
		//      aside). Allies (issummoned OR BF_FRIENDLY) are excluded on BOTH sides so it never shoves
		//      a friendly spawn.
		const bool ent_is_ally   = ent->monsterinfo.issummoned   || (ent->monsterinfo.bonus_flags & BF_FRIENDLY);
		const bool other_is_ally = other->monsterinfo.issummoned || (other->monsterinfo.bonus_flags & BF_FRIENDLY);
		const bool summoned_vs_enemy = ent->monsterinfo.issummoned && !(other->monsterinfo.bonus_flags & BF_FRIENDLY);
		const bool normal_pair = !ent_is_ally && !other_is_ally && (ent->s.number < other->s.number);
		if (summoned_vs_enemy || normal_pair)
		{
			// Normal enemy pairs un-stack across the FULL bbox-aware range (push until their boxes
			// clear). The diagnostic showed they settle at bbox-overlap distance (dist ~24-48), where
			// the old deep-overlap-only gate (pair_space*0.5) never fired - so |hardsep| was always 0
			// and they stayed visibly stacked. The summoned strogg-vs-enemy case keeps the tighter
			// deep-overlap engagement so stroggs still close to melee instead of being held at box range.
			const float deep_dist = normal_pair ? pair_space
			                                    : pair_space * HARD_SEPARATION_DEEP_OVERLAP_FRACTION;
			const bool deeply_overlapping =
				(dist < deep_dist) ||
				boxes_intersect(ent->absmin, ent->absmax, other->absmin, other->absmax);

			if (deeply_overlapping)
			{
				// Falloff: 0 at the deep-overlap edge, ~1 at contact. dist is already floored
				// (coincident pairs use HARD_SEPARATION_MIN_DIST above), so this can't spike.
				const float clamped = std::max(dist, HARD_SEPARATION_MIN_DIST);
				const float hs = std::clamp((deep_dist - clamped) / deep_dist, 0.0f, 1.0f);

				vec3_t hard_push = sep_dir * (hs * HARD_SEPARATION_MAX_FORCE); // unit dir AWAY from the neighbor

				if (!(ent->flags & (FL_FLY | FL_SWIM)))
					hard_push.z *= REPULSION_VERTICAL_DAMPING;

				hard_separation += hard_push;
			}
		}

		// Limit total monsters considered for performance
		if (nearby_count >= REPULSION_MAX_NEARBY_MONSTERS)
			break;
	}

	// Scale down if too many monsters to prevent excessive spreading
	if (nearby_count > REPULSION_CROWDING_THRESHOLD)
		repulsion *= REPULSION_MULTI_MONSTER_SCALE;

	// Store calculated repulsion
	ent->monsterinfo.repulsion_vector = repulsion;
	ent->monsterinfo.hard_separation_vector = hard_separation;

	// TEMP DIAGNOSTIC (developer 2): compare the custom grid's neighbor count against the engine
	// areagrid's, and report whether a push was computed. Interpreting the [REPULSE] lines:
	//   * no lines at all while monsters are visibly stacked -> this function (so SV_movestep) isn't
	//     running for them; they're stacked in a non-walking AI state (fix must run outside movestep).
	//   * grid_nearby=0 but box_nearby>=2 -> the ProximityGrid query is the problem (use BoxEdicts).
	//   * grid_nearby>=2 with hardsep=0 -> the hard-separation gate isn't firing.
	//   * grid_nearby>=2 with hardsep>0 -> push IS computed; the loss is downstream (movement/re-converge).
	if (developer->integer >= 2)
	{
		edict_t* box[16];
		const size_t box_nearby = gi.BoxEdicts(ent->absmin - vec3_t{ 16, 16, 16 }, ent->absmax + vec3_t{ 16, 16, 16 },
			box, 16, AREA_SOLID, RepulseDbg_BoxFilter, ent);
		if (box_nearby >= 2 || nearby_count >= 2)
		{
			static gtime_t s_next_repulse_dbg = 0_sec;
			if (level.time >= s_next_repulse_dbg)
			{
				s_next_repulse_dbg = level.time + 500_ms;
				gi.Com_PrintFmt("[REPULSE] {} grid={} box={} |rep|={} |hardsep|={} corridor={} tight={}\n",
					ent->classname, nearby_count, static_cast<int>(box_nearby),
					static_cast<int>(repulsion.length()), static_cast<int>(hard_separation.length()),
					ent->monsterinfo.corridor_blocked_dirs, ent->monsterinfo.corridor_tight_blocked_dirs);
			}
		}
	}

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

	// Nav-following with the enemy out of sight: mostly trust the path, minimal jostling.
	// Hard separation below is untouched, so packs still un-pile.
	if ((ent->monsterinfo.aiflags & AI_PATHING) &&
		((ent->monsterinfo.aiflags & AI_LOST_SIGHT) ||
		 ent->monsterinfo.trail_time + REPULSION_ENEMY_SEEN_RECENT <= level.time))
		repulsion_strength *= REPULSION_STRENGTH_PATHING_UNSEEN;

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

	// --- Hard separation (FULL push, deep-overlap only) ---
	// Not projected to lateral, so the along-goal separating component (the one the lateral block
	// strips) survives - that's what un-stacks monsters whose goals point the same way (a spawn
	// cluster chasing one player) or back at each other (a summoned strogg vs its enemy). Only
	// non-zero at deep overlap (see CalculateMonsterRepulsion), so it never fires at normal adjacency
	// and never stops a monster from closing to melee.
	const vec3_t hard_sep = ent->monsterinfo.hard_separation_vector;
	if (hard_sep.lengthSquared() >= REPULSION_MIN_THRESHOLD_SQ)
	{
		float hard_strength = HARD_SEPARATION_STRENGTH;

		// Live enemy: scale down so it un-stacks without shoving the strogg back out of melee range.
		if (in_combat && ent->enemy && ent->enemy->inuse)
			hard_strength *= HARD_SEPARATION_STRENGTH_COMBAT;

		// Un-stacking stays mostly ON in corridors: a hard push aimed into a wall is rejected/slid by
		// the step trace (monsters are still solid to walls), so it can't bury them, while the
		// along-corridor component still separates a front-to-back pile. Only damp it in the very
		// tightest single-file spots so a 1-wide corridor still files through instead of jittering.
		if (ent->monsterinfo.corridor_tight_blocked_dirs >= CORRIDOR_BLOCKED_TIGHT_THRESHOLD)
			hard_strength *= 0.5f;

		if (hard_strength > 0.0f)
		{
			vec3_t hard_move = hard_sep * hard_strength;

			if (original_move.lengthSquared() > 1.0f)
			{
				const vec3_t move_dir = original_move.normalized();
				const float step_len = original_move.length();

				// Anti-jitter: cap the total hard push to a fraction of the step length so the
				// strogg can't be flung across the enemy in a single frame.
				const float max_push = step_len * HARD_SEPARATION_STEP_CAP_FRACTION;
				if (hard_move.lengthSquared() > max_push * max_push)
					hard_move = hard_move.normalized() * max_push;

				// Keep the lateral (un-stacking) part in full, but never let the along-goal
				// back-push REVERSE net travel: when a strogg and its enemy point goals straight
				// at each other the full push used to net-cancel the forward step, leaving the
				// strogg running in place. Slow it head-on if we must, but always net at least
				// MIN_FWD_FRACTION of the intended step forward so it keeps closing to melee.
				static constexpr float MIN_FWD_FRACTION = 0.25f;
				const float along = hard_move.dot(move_dir);          // < 0 => pushing backward
				const vec3_t lateral = hard_move - move_dir * along;  // perpendicular part
				const float min_along = -(step_len * (1.0f - MIN_FWD_FRACTION));
				hard_move = lateral + move_dir * std::max(along, min_along);
			}

			move += hard_move;
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

// [Horde] Per-pair selective solidity. Horde monsters are non-solid to each other in SV_movestep
// (spacing is repulsion-based), but summoned player-allies and enemy monsters should act as real
// mutual walls. A single CONTENTS_MONSTER mask bit can't express "solid to enemies but not allies"
// (that made normal monsters jam near an ally), so we enforce the wall PER PAIR here. Returns true
// (step blocked) when:
//   - the step would move us INTO an OPPOSED monster we're not already overlapping (the wall: stop
//     at the surface, i.e. melee range), OR
//   - we're ALREADY lodged inside an opposed monster (e.g. the player shoved us in) and the step
//     does NOT move us away from it. The repulsion can't eject us here - it's capped so it never
//     reverses the pull toward our enemy goal - so the wall forces us out: only separating steps
//     pass, and the caller bumps (SV_NewChaseDir) to a separating heading until we reach the surface.
static bool M_StepHitsOpposedMonster(edict_t* ent, const vec3_t& candidate_origin)
{
	if (!g_horde->integer || !(ent->svflags & SVF_MONSTER))
		return false;

	const vec3_t cmin = candidate_origin + ent->mins;
	const vec3_t cmax = candidate_origin + ent->maxs;

	// Radius generous enough that a neighbor whose box could intersect ours is returned.
	const float ent_width = std::max(ent->maxs[0] - ent->mins[0], ent->maxs[1] - ent->mins[1]);
	const float search_radius = std::max(ent_width, 64.f) + 64.f;
	std::span<edict_t* const> nearby = HordePhys::g_monster_grid.IsBuilt()
		? HordePhys::g_monster_grid.QueryRadius(candidate_origin, search_radius)
		: std::span<edict_t* const>{};

	for (edict_t* other : nearby)
	{
		if (other == ent || !other->inuse || !(other->svflags & SVF_MONSTER))
			continue;
		if (other->health <= 0 || other->deadflag)
			continue;
		if (!M_MonstersOpposed(ent, other))
			continue; // same side -> pass through (no wall)

		if (boxes_intersect(ent->absmin, ent->absmax, other->absmin, other->absmax))
		{
			// Already lodged inside this opposed monster: allow ONLY steps that increase the
			// distance to its center (eject toward the surface); block anything that keeps or
			// deepens the overlap (our goal would otherwise pull us right back in).
			if (DistanceSquared(candidate_origin, other->s.origin) <= DistanceSquared(ent->s.origin, other->s.origin))
				return true;
		}
		else if (boxes_intersect(cmin, cmax, other->absmin, other->absmax))
		{
			// Stepping INTO a new overlap -> hard wall (stop at the surface / melee range).
			return true;
		}
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

	// [Horde] Per-pair wall: opposed monsters (summoned ally <-> enemy) hard-block each other even
	// though horde monsters are otherwise non-solid. If this horizontal step would move us INTO an
	// opposed monster, reject it like a wall block - the caller then bumps via SV_NewChaseDir, so
	// allies route around enemy walls (and stop at their melee target). Same-side pairs pass through.
	if (M_StepHitsOpposedMonster(ent, ent->s.origin + vec3_t{ move[0], move[1], 0.f }))
		return false;

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
    // Don't remove CONTENTS_MONSTER for morphed players - they need proper collision.
    // Horde monsters are non-solid to each other here (spacing is repulsion-based); the ONLY
    // hard collision between monsters is the per-pair opposed wall checked above (summoned ally
    // <-> enemy), so normal same-side monsters keep passing through with a gentle nudge.
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

		// Record when a ground monster started grinding this wall, for the horde teleport failsafe
		// (g_horde.cpp checks wall_stuck_time + GROUND_WALL_STUCK_TELEPORT_TIME). We don't act on it
		// here anymore - the PSX slide / SV_NewChaseDir handle recovery; this just feeds the failsafe
		// so a monster wedged for far too long can still be teleported out.
		if ((ent->svflags & SVF_MONSTER) && !ent->monsterinfo.IS_BOSS && ent->monsterinfo.wall_stuck_time == 0_ms)
			ent->monsterinfo.wall_stuck_time = level.time;

		// A wedged squeezed boss reacts immediately (ignores the bump cooldown) so its shrunk box
		// doesn't grind its full-size model into the wall while it waits out the cooldown.
		const vec3_t& bsq = ent->monsterinfo.bbox_squeeze;
		const bool squeezed = (bsq[0] != 0.f || bsq[1] != 0.f || bsq[2] != 0.f);

		// PSX parity: snap ideal_yaw to slide along the wall we hit and try again next think. (We
		// previously layered a non-boss wall-stuck turn-away, a 40%-smoothed slide and a
		// last_valid_move momentum fallback on top of this - the smoothing mirrored the nav yaw lag
		// we reverted and the yaw-diff window could skip the slide entirely. Harder/boxed-in cases
		// now fall through to SV_NewChaseDir, the same as the original.)
		if ((ent->monsterinfo.bump_time < level.time || squeezed) && chosen_forward.fraction < 1.0f)
		{
			vec3_t dir = SlideClipVelocity(AngleVectors(vec3_t{ 0.f, ent->ideal_yaw, 0.f }).forward, chosen_forward.plane.normal, 1.0f);
			float new_yaw = vectoyaw(dir);

			if (dir.lengthSquared() > 0.1f && ent->ideal_yaw != new_yaw)
			{
				ent->ideal_yaw = new_yaw;
				ent->monsterinfo.random_change_time = level.time + RANDOM_CHANGE_BASE;
				ent->monsterinfo.bump_time = level.time + BUMP_TIME_DELAY;
				return true;
			}
		}

		return false;
	}

	// Made forward progress this step, so we're no longer grinding a wall - clear the grind timer
	// the horde teleport failsafe reads.
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

	// the move is ok
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

	// PSX parity: every direction failed - we're boxed in. Pick a fresh random yaw and let the
	// search retry next think. (We had instead committed to turnaround and locked random_change_time
	// /bump_time/bad_move_time to avoid pirouetting, but those locks starved this very search and
	// the straight-shot, leaving cornered monsters wedged until they gained visual - so we restore
	// the original retry-every-frame random escape.)
	actor->ideal_yaw = frandom(0, 360); // can't move; pick a random yaw...

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

// [Horde] Elevator traversal helpers. The nav mesh plans PathLinkType::Elevator links but leaves
// riding entirely to the game (bots get it engine-side via Bot_MoveToPoint; monsters only receive
// the two move points), so we drive func_plats ourselves via Use_Plat's monster branch - the same
// sanctioned trigger path rogue's blocked_checkplat uses.
static bool M_EntityIsPlat(const edict_t* e)
{
	// prefix match covers func_plat and func_plat2, like rogue/g_rogue_newai.cpp's blocked_checkplat
	return e && e != world && e->classname && !strncmp(e->classname, "func_plat", 9);
}

static bool M_OnPlat(const edict_t* self)
{
	return M_EntityIsPlat(self->groundentity);
}

static bool M_PlatIsMoving(const edict_t* plat)
{
	return plat->moveinfo.state == STATE_UP || plat->moveinfo.state == STATE_DOWN;
}

// Find the plat serving a nav elevator boarding point: xy box containment (bmodel absmin/absmax
// are valid for MOVETYPE_PUSH), z anywhere within the plat's full travel range (pos1 = top,
// pos2 = bottom) plus a margin - the plat may currently be parked at the other end. Ambiguity
// (nested/adjacent plats) resolved by smallest xy distance from box center to the point.
static edict_t* M_FindPlatForPoint(const vec3_t& point)
{
	edict_t* best = nullptr;
	float best_dist_sq = 1e30f;

	for (uint32_t i = 1; i < globals.num_edicts; i++)
	{
		edict_t* e = &g_edicts[i];
		if (!e->inuse || !M_EntityIsPlat(e))
			continue;

		if (point.x < e->absmin.x - 32.f || point.x > e->absmax.x + 32.f ||
			point.y < e->absmin.y - 32.f || point.y > e->absmax.y + 32.f)
			continue;

		const float surf_offset = e->maxs.z; // rider surface offset from origin
		const float z_top = std::max(e->pos1.z, e->pos2.z) + surf_offset;
		const float z_bottom = std::min(e->pos1.z, e->pos2.z) + surf_offset;
		if (point.z < z_bottom - 64.f || point.z > z_top + 64.f)
			continue;

		const float cx = (e->absmin.x + e->absmax.x) * 0.5f - point.x;
		const float cy = (e->absmin.y + e->absmax.y) * 0.5f - point.y;
		const float dist_sq = cx * cx + cy * cy;
		if (dist_sq < best_dist_sq)
		{
			best_dist_sq = dist_sq;
			best = e;
		}
	}

	return best;
}

// [Horde] While deliberately holding still for an elevator (riding it, or waiting for the plat
// to arrive), play the stand animation instead of running in place - but only when there's no
// visible enemy to keep engaging. Reuses the healing-pause mechanism: ai_stand/ai_run freeze
// while healing_pause_time is active, and once it expires ai_stand's HuntTarget fix-up bounces
// the monster back to run - which re-enters M_MoveToGoal and this machine, re-standing here if
// the hold is still in effect. Net effect: standing pose with a 1-2 frame run tick every 400ms.
static void M_PlatHoldStand(edict_t* self)
{
	if (self->monsterinfo.stand && self->enemy && self->enemy->inuse &&
		!(self->monsterinfo.aiflags & AI_SOUND_TARGET) &&
		!visible(self, self->enemy, false))
	{
		self->monsterinfo.stand(self);
		self->monsterinfo.healing_pause_time = level.time + 400_ms;
	}
}

// [Horde] Comrade nav breadcrumbs: positions monsters recently solved paths FROM. Paths can't
// be shared between monsters (a path is specific to its start), but a solve POSITION transfers
// perfectly - it's proven to have a start node AND to be in the component that can route to the
// players. A pack cornered off-mesh/disconnected walks toward the nearest fresh crumb (dropped
// by the one comrade that found a lane) instead of beelining into the same wall, then solves
// from there. Freshness is time-based only to keep crumbs local to the action; the mesh itself
// is static. Stale-map entries are rejected by the c.time <= level.time check (level.time
// restarts per map).
struct nav_crumb_t
{
	vec3_t pos;
	gtime_t time;
};
static constexpr size_t NAV_CRUMB_MAX = 16;
static constexpr gtime_t NAV_CRUMB_LIFETIME = 10_sec;
static constexpr float NAV_CRUMB_RADIUS = 1024.f;
static nav_crumb_t s_nav_crumbs[NAV_CRUMB_MAX];
static size_t s_nav_crumb_head = 0;

static bool NavCrumbFresh(const nav_crumb_t& c)
{
	return c.time > 0_ms && c.time <= level.time && level.time - c.time < NAV_CRUMB_LIFETIME;
}

static void M_RecordNavCrumb(const edict_t* self)
{
	if (self->flags & (FL_FLY | FL_SWIM))
		return; // flyer/swimmer positions don't help ground monsters

	for (nav_crumb_t& c : s_nav_crumbs)
	{
		if (NavCrumbFresh(c) && (c.pos - self->s.origin).lengthSquared() < 128.f * 128.f)
		{
			c.time = level.time; // refresh the nearby crumb instead of duplicating it
			return;
		}
	}

	s_nav_crumbs[s_nav_crumb_head] = { self->s.origin, level.time };
	s_nav_crumb_head = (s_nav_crumb_head + 1) % NAV_CRUMB_MAX;
}

static bool M_FindNavCrumb(const edict_t* self, vec3_t& out)
{
	float best_sq = NAV_CRUMB_RADIUS * NAV_CRUMB_RADIUS;
	bool found = false;

	for (const nav_crumb_t& c : s_nav_crumbs)
	{
		if (!NavCrumbFresh(c))
			continue;
		const float d_sq = (c.pos - self->s.origin).lengthSquared();
		if (d_sq < best_sq)
		{
			best_sq = d_sq;
			out = c.pos;
			found = true;
		}
	}

	return found;
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

	// PSX parity: only recompute the path when the fixed cache expires or when we've reached the
	// current path point - NOT on a "progress" heuristic. The old progress-based force_recalc
	// measured distance-to-GOAL closed per 500ms and flagged "stuck" if it was small; but a monster
	// chasing a fleeing enemy (the goal is the enemy's live origin) closes ~0 distance while moving
	// perfectly, so it constantly re-solved off-schedule - and a fresh solve between two near-equal
	// routes returns a different "lane", which is the stutter/blur during the lane swap. Genuine
	// blocks are handled by SV_NewChaseDir + path_blocked_counter, not by re-solving here.
	const vec3_t goal_pos = self->enemy ? self->enemy->s.origin : self->goalentity->s.origin;

	// [Horde] While the plat under us is actually MOVING, ride it: don't step (we'd walk off
	// mid-shaft), don't re-solve (a mid-shaft origin is off-mesh -> NoStartNode -> bench), and
	// keep path_solve_z pinned so the elevation_changed re-solve below can't fire mid-ride.
	// The hold is bounded: plat_hit_top/plat_hit_bottom flip moveinfo.state to an idle value and
	// this predicate goes false on its own; the (by then expired) 2s cache re-solves from the new
	// floor. Applies to ANY moving plat, not just elevator links - a stale path or a rogue
	// blocked_checkplat boarding benefits identically.
	if (M_OnPlat(self) && M_PlatIsMoving(self->groundentity))
	{
		const vec3_t to_exit = self->monsterinfo.nav_path.secondMovePoint - self->s.origin;
		if (to_exit.x * to_exit.x + to_exit.y * to_exit.y > 1.f)
		{
			self->ideal_yaw = vectoyaw(to_exit);
			M_ChangeYaw(self);
		}
		self->monsterinfo.path_solve_z = self->s.origin.z;
		M_PlatHoldStand(self); // ride in the stand pose when no visible enemy
		return true;
	}

	// Check if we need to recalculate path
	const bool path_expired = self->monsterinfo.nav_path_cache_time <= level.time;
	const bool reached_point = boxes_intersect(mon_mins, mon_maxs, path_to, path_to);
	const bool path_intersecting = self->monsterinfo.nav_path.returnCode != PathReturnCode::TraversalPending && reached_point;

	// [Horde] Jump a BarrierJump traversal a SUPER_STEP monster reached by walking. All horde monsters
	// get SUPER_STEP (stepsize 64), so they walk onto the traversal's near point without triggering the
	// engine's jump - returnCode stays TraversalPending and they sit on the landing (diag: rc=2 link=3,
	// grounded, hdist~0, feet on the point). Re-solving just re-plans the same jump, and handing it to
	// classic movement walks it back OFF the crate (the nav wanted a JUMP, not a walk - confirmed: after
	// a classic hand-off the monster drifted ~189u away, then cycled). So do the jump ourselves toward
	// the goal: the nav only planned a BarrierJump here because jump_height can clear it, so launch up by
	// that height (+margin) and forward. Airborne after (groundentity cleared) -> re-evaluates on
	// landing, climbing crate by crate. Won't cut a real engine jump short: that clears TraversalPending
	// before the monster is ever grounded on the landing.
	// "On the traversal point" - deliberately more lenient than boxes_intersect (reached_point): a
	// TALL monster (e.g. monster_janitor) stands with its feet slightly ABOVE the barrier-jump node,
	// so the point falls just under its player-sized box and boxes_intersect misses - then the jump
	// never fires and it stays stuck (diag: janitor rc=2, hdist~1-3, dz=-32 so the point is below its
	// feet). Fire when we're horizontally on the point and at/above it (reached or overshot the
	// landing), regardless of exact box overlap.
	// LongJump gaps: path_to (= secondMovePoint) is ACROSS the gap and can't be reached by walking,
	// so measure the trigger window against the near edge (firstMovePoint) instead. Elevator links
	// are excluded entirely - a monster at a boarding point must ride, not launch into the shaft.
	const bool gap_link = self->monsterinfo.nav_path.pathLinkType == PathLinkType::LongJump;
	const vec3_t& trav_near = gap_link ? self->monsterinfo.nav_path.firstMovePoint : path_to;
	const float trav_dx = trav_near.x - self->s.origin.x;
	const float trav_dy = trav_near.y - self->s.origin.y;
	const float trav_dz = trav_near.z - self->s.origin.z; // <0 => point is below us (we reached/overshot up)
	const bool on_traversal_point = self->groundentity &&
		self->monsterinfo.nav_path.returnCode == PathReturnCode::TraversalPending &&
		self->monsterinfo.nav_path.pathLinkType != PathLinkType::Elevator &&
		(trav_dx * trav_dx + trav_dy * trav_dy) < (32.f * 32.f) &&
		trav_dz < 24.f && trav_dz > -80.f;
	if (on_traversal_point)
	{
		// Barrier jumps aim at the goal (the barrier sits between us and it); gap jumps aim at the
		// landing point so the monster crosses the gap the nav planned, not a beeline at the enemy.
		const vec3_t& land = self->monsterinfo.nav_path.secondMovePoint;
		const vec3_t jdir = gap_link ? (land - self->s.origin) : (goal_pos - self->s.origin); // vectoyaw uses x/y only
		if (jdir.x * jdir.x + jdir.y * jdir.y > 1.f)
			self->ideal_yaw = self->s.angles[YAW] = vectoyaw(jdir); // face the jump so it clears
		const float jh = std::max(static_cast<float>(self->monsterinfo.jump_height), 32.f);
		float up_vel = std::min(sqrtf(1600.f * (jh + 16.f)), 500.f); // clear ~jump_height (rise uses g=800*1.0)
		// WalkOffLedge traversals are DROPS (diag: gekk leapt up 408 for a dz=-40 ledge) - a
		// small hop off the lip is enough, forward momentum carries it over the edge.
		if (self->monsterinfo.nav_path.pathLinkType == PathLinkType::WalkOffLedge)
			up_vel = std::min(up_vel, 220.f);
		float fwd_vel = 180.f;
		if (gap_link)
		{
			up_vel = std::max(up_vel, 260.f); // gaps need real hang time even for low jumpers
			const float hdx = land.x - self->s.origin.x;
			const float hdy = land.y - self->s.origin.y;
			const float hdist = sqrtf(hdx * hdx + hdy * hdy);
			// Air time under M_MonsterApplyJumpGravity: rise at g=800 (t=up/800), fall at g=1600
			// back to launch level (t=up/(800*sqrt(2))) => ~1.707*up/800. A landing below launch
			// level adds fall time, so this undershoots slightly on downward gaps - the clamp
			// ceiling covers the slack.
			const float air_time = 1.707f * up_vel / 800.f;
			fwd_vel = std::min(std::max(hdist / air_time, 200.f), 480.f);
		}
		if (developer->integer >= 2)
			gi.Com_PrintFmt("[NAV] {} traversal-jump link={} (fwd={:.0f} up={:.0f})\n",
				self->classname, (int32_t)self->monsterinfo.nav_path.pathLinkType, fwd_vel, up_vel);
		M_MonsterJump(self, fwd_vel, up_vel);
		return true;
	}

	// SUPER_STEP / jumps can carry a monster up (or down) to a different level than the cached path
	// was solved for, leaving a stale point it bypassed that boxes_intersect never "reaches" - it
	// then aims back at the old level until the cache expires (the "doesn't clean after getting up"
	// edge-of-platform stick). Once we've LANDED (grounded) a good step away in Z, re-solve from
	// where we actually are. Grounded-gated so it never fires mid-jump; vertical-only so it can't
	// reintroduce horizontal lane-thrash.
	const bool elevation_changed = self->groundentity &&
		fabsf(self->s.origin.z - self->monsterinfo.path_solve_z) > PATH_RESOLVE_Z_DELTA;

	if (path_expired || path_intersecting || elevation_changed)
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

		// Debug drawing if enabled. The engine only draws the path when it's (re)solved - here that's
		// every PATH_CACHE_TIME_NORMAL (2s), or sooner on arrival - so a one-frame drawTime just
		// flickers (worst for a stuck/circling monster, which only re-solves on the 2s cache). Draw
		// for a bit longer than the cache so the route stays continuously visible, like the bbox
		// overlay (which persists by being redrawn every frame).
		if (g_debug_monster_paths->integer == 1)
			request.debugging.drawTime = 2.5f;

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

			// [Horde] Gap jumps for capable ground jumpers (flyers get LongJump below anyway).
			// A gap jump is a jump PLUS a fall, so require both a meaningful jump and drop
			// tolerance - weak or drop-averse monsters shouldn't attempt gaps.
			if (!(self->flags & FL_FLY) && self->monsterinfo.can_jump &&
				self->monsterinfo.jump_height >= 40 && self->monsterinfo.drop_height >= 128)
				request.pathFlags |= PathFlags::LongJump;
		}

		// [Horde] Ground monsters can operate func_plats (Use_Plat's monster branch), so let the
		// nav route them through elevator links; flyers/swimmers path over or around instead.
		// The traversal itself is driven by the elevator machine + ride-hold above.
		if (!(self->flags & (FL_FLY | FL_SWIM)))
			request.pathFlags |= PathFlags::Elevator;

		// Flying monsters get special treatment
		if (self->flags & FL_FLY)
		{
			const float vertical_limit = std::max(8192.f, height * 8);
			request.nodeSearch.maxHeight = request.nodeSearch.minHeight = vertical_limit;
			request.pathFlags |= PathFlags::LongJump;
		}

		if (!gi.GetPathToGoal(request, self->monsterinfo.nav_path))
		{
			const PathReturnCode rc = self->monsterinfo.nav_path.returnCode;

			// fatal error, don't bother ever trying nodes
			if (rc == PathReturnCode::NoNavAvailable)
			{
				self->monsterinfo.aiflags |= AI_NO_PATH_FINDING;
				return false;
			}

			// Request built without Walk/Water flags - a bug in this function, not a map issue
			if (rc == PathReturnCode::MissingWalkOrSwimFlag)
			{
				if (developer->integer)
					gi.Com_PrintFmt("[NAV] {} MissingWalkOrSwimFlag - PathRequest built without Walk/Water flags\n", self->classname);
				self->monsterinfo.aiflags |= AI_NO_PATH_FINDING;
				return false;
			}

			// [Horde] A monster nudged slightly off-mesh (repulsion pressing it into a wall,
			// standing on a crate it climbed) gets NoStartNode with the engine-default node
			// search. Retry once with a widened search before giving up, so a barely-off-mesh
			// monster re-acquires the mesh instead of being benched to classic wall-bumping.
			// The failure code stays in nav_path.returnCode for M_MoveToPath's per-code bench.
			bool solved = false;
			if (rc == PathReturnCode::NoStartNode || rc == PathReturnCode::InvalidStart)
			{
				request.nodeSearch.radius = std::max(request.nodeSearch.radius, std::max(512.f, width * 4.f));
				request.nodeSearch.minHeight = std::max(request.nodeSearch.minHeight, height * 3.f);
				request.nodeSearch.maxHeight = std::max(request.nodeSearch.maxHeight, height * 4.f);
				solved = gi.GetPathToGoal(request, self->monsterinfo.nav_path);
			}
			// [Horde] NoPathFound often means the ENEMY is momentarily somewhere unroutable
			// (mid-jump, off-mesh ledge, sealed pocket) - not that WE are boxed in. Benching a
			// whole pack 10s over it makes them all beeline into the same corner wall on classic
			// movement. Retry once toward the last place we actually saw the enemy; arriving
			// there re-engages the normal lost-sight pursuit. Skip when we're already close to
			// it (a repeat solve to our own position degenerates into running in place) or when
			// it's essentially the same spot as the failed goal.
			else if (rc == PathReturnCode::NoPathFound && self->enemy)
			{
				const vec3_t& ls = self->monsterinfo.last_sighting;
				const vec3_t ls_to_goal = ls - goal_pos;
				const vec3_t ls_to_self = ls - self->s.origin;
				if (ls_to_goal.lengthSquared() > 64.f * 64.f &&
					ls_to_self.lengthSquared() > 64.f * 64.f)
				{
					request.goal = ls;
					solved = gi.GetPathToGoal(request, self->monsterinfo.nav_path);
				}
			}

			if (!solved)
			{
				// Routine chatter: failed solves are common near wave edges - dev>=2, throttled
				if (developer->integer >= 2)
				{
					static gtime_t s_next_fail_dbg = 0_ms;
					if (level.time >= s_next_fail_dbg)
					{
						s_next_fail_dbg = level.time + 500_ms;
						gi.Com_PrintFmt("[NAV] {} solve failed rc={}{}\n", self->classname,
							(int32_t)self->monsterinfo.nav_path.returnCode,
							(rc == PathReturnCode::NoStartNode || rc == PathReturnCode::InvalidStart) ? " (after widened retry)" : "");
					}
				}
				return false;
			}
			// widened retry succeeded - fall through and commit the route as normal
		}

		// Fixed cache time (PSX parity): commit to this route until it expires or we reach it.
		self->monsterinfo.nav_path_cache_time = level.time + PATH_CACHE_TIME_NORMAL;
		self->monsterinfo.path_solve_z = self->s.origin.z; // baseline for the elevation-change re-solve
		M_RecordNavCrumb(self); // this position provably solves - share it with benched comrades
	}

	// TEMP NAV DIAG (developer 2): diagnose the vertical "edge of platform" stick. Throttled, and
	// gated to grounded monsters whose path point is at a different level or whose path is mid-
	// traversal - i.e. the suspect cases. Read the stuck parasite's line:
	//   rc = returnCode (TraversalPending means the nav planned a jump it thinks isn't done yet),
	//   link = pathLinkType (BarrierJump/LongJump/WalkOffLedge/Walk/Elevator),
	//   dz = path_to.z - origin.z (positive = point is ABOVE us, negative = below),
	//   hdist = horizontal distance to the point, dsz = how far our Z drifted since the solve.
	if (developer->integer >= 2 && self->groundentity &&
		(self->monsterinfo.nav_path.returnCode == PathReturnCode::TraversalPending ||
		 fabsf(path_to.z - self->s.origin.z) > 24.f))
	{
		static gtime_t s_next_nav_dbg = 0_ms;
		if (level.time >= s_next_nav_dbg)
		{
			s_next_nav_dbg = level.time + 300_ms;
			const vec3_t to_pt = path_to - self->s.origin;
			gi.Com_PrintFmt("[NAV] {} rc={} link={} npts={} oz={:.0f} ptz={:.0f} dz={:.0f} hdist={:.0f} dsz={:.0f}\n",
				self->classname,
				(int32_t)self->monsterinfo.nav_path.returnCode,
				(int32_t)self->monsterinfo.nav_path.pathLinkType,
				self->monsterinfo.nav_path.numPathPoints,
				self->s.origin.z, path_to.z, to_pt.z,
				sqrtf(to_pt.x * to_pt.x + to_pt.y * to_pt.y),
				self->s.origin.z - self->monsterinfo.path_solve_z);
		}
	}

	// [Horde] Elevator traversal machine. Assumed engine semantics, verified via the [NAV-ELEV]
	// diag: firstMovePoint = boarding point at our end of the shaft, secondMovePoint = exit at
	// the other end. Direction (up vs down) is derived from the exit's Z relative to us - never
	// from which point is "the bottom" - so this is direction-agnostic. It is also stateless: a
	// re-solve that drops the elevator plan makes all of it evaporate; a rider knocked off
	// mid-ride just falls, lands, and re-solves via elevation_changed.
	vec3_t move_point = path_to; // what we actually steer toward this frame
	const bool elevator_link = self->monsterinfo.nav_path.returnCode == PathReturnCode::TraversalPending &&
		self->monsterinfo.nav_path.pathLinkType == PathLinkType::Elevator;
	if (elevator_link)
	{
		const vec3_t& board = self->monsterinfo.nav_path.firstMovePoint;
		const vec3_t& exit_pt = self->monsterinfo.nav_path.secondMovePoint;
		const float exit_dz = exit_pt.z - self->s.origin.z;

		// TEMP NAV DIAG (developer 2): empirically confirm the closed-source elevator traversal
		// layout (boarding vs exit point, plat association) - read alongside the [NAV] diag above.
		if (developer->integer >= 2)
		{
			static gtime_t s_next_elev_dbg = 0_ms;
			if (level.time >= s_next_elev_dbg)
			{
				s_next_elev_dbg = level.time + 300_ms;
				const edict_t* dbg_plat = M_OnPlat(self) ? self->groundentity : M_FindPlatForPoint(board);
				gi.Com_PrintFmt("[NAV-ELEV] {} oz={:.0f} board=({:.0f} {:.0f} {:.0f}) exit=({:.0f} {:.0f} {:.0f}) plat={} state={} topz={:.0f}\n",
					self->classname, self->s.origin.z,
					board.x, board.y, board.z, exit_pt.x, exit_pt.y, exit_pt.z,
					dbg_plat ? dbg_plat->classname : "none",
					dbg_plat ? (int32_t)dbg_plat->moveinfo.state : -1,
					dbg_plat ? dbg_plat->absmax.z : 0.f);
			}
		}

		if (fabsf(exit_dz) <= 64.f) // SUPER_STEP stepsize: we're at the exit's level - ride done
		{
			// force a fresh solve from the new floor next frame; meanwhile walk toward the exit
			self->monsterinfo.nav_path_cache_time = 0_ms;
			move_point = exit_pt;
		}
		else
		{
			move_point = board; // not at exit level: steer at the boarding point, not the exit

			edict_t* plat = M_OnPlat(self) ? self->groundentity : M_FindPlatForPoint(board);

			if (plat && plat->spawnflags.has(SPAWNFLAG_PLAT_NO_MONSTER))
			{
				// Map forbids monsters operating this plat (and Use_Plat's monster branch is
				// bypassed for it - a monster activator would wrongly run the player path).
				// Bench nav briefly and let classic movement take over.
				self->monsterinfo.path_wait_time = level.time + 3_sec;
				return false;
			}

			if (plat && plat->use)
			{
				if (self->groundentity == plat)
				{
					// Standing on the plat. Idle at the wrong end for our exit -> operate it
					// (Use_Plat's monster branch; naturally debounced - it only acts on idle
					// states, and once moving the ride-hold above owns the frames).
					if ((plat->moveinfo.state == STATE_BOTTOM && exit_dz > 0.f) ||
						(plat->moveinfo.state == STATE_TOP && exit_dz < 0.f))
						plat->use(plat, self, self);

					// hold on the plat either way; face the exit for the ride
					const vec3_t to_exit = exit_pt - self->s.origin;
					if (to_exit.x * to_exit.x + to_exit.y * to_exit.y > 1.f)
					{
						self->ideal_yaw = vectoyaw(to_exit);
						M_ChangeYaw(self);
					}
					M_PlatHoldStand(self);
					return true;
				}

				// Not on the plat yet. If it's parked at another level, call it over; while it
				// travels, wait NEAR the boarding point instead of pressing into the shaft (a
				// descending plat crushes pit-standers via plat_blocked; riders on top are safe).
				const bool plat_here = fabsf(plat->absmax.z - board.z) <= 64.f;
				if (!plat_here)
				{
					if (!M_PlatIsMoving(plat))
						plat->use(plat, self, self);

					// Crush safety: plat_go_up spawns a bad_area over the pit (killed again at
					// plat_hit_bottom), so if we're inside it we're standing where the plat will
					// come down. Back out horizontally and wait BESIDE the shaft instead.
					if (edict_t* bad = CheckForBadArea(self))
					{
						vec3_t away = self->s.origin - (bad->absmin + bad->absmax) * 0.5f;
						away.z = 0.f;
						if (away.lengthSquared() < 1.f)
							away = { 1.f, 0.f, 0.f };
						away.normalize();
						move_point = self->s.origin + away * 64.f;
						// fall through: WALK out of the danger area, never stand in it
					}
					else
					{
						const float bdx = board.x - self->s.origin.x;
						const float bdy = board.y - self->s.origin.y;
						if (bdx * bdx + bdy * bdy < 64.f * 64.f)
						{
							// close enough - stand and wait for the ride to arrive
							self->ideal_yaw = vectoyaw(board - self->s.origin);
							M_ChangeYaw(self);
							M_PlatHoldStand(self);
							return true;
						}
						// else keep walking toward the boarding point (move_point already = board)
					}
				}
				// plat parked at our level: keep walking onto it (move_point = board)
			}
			// No plat found (a lift-style mover we don't recognize): fall through and walk
			// toward the boarding point - worst case is pre-elevator behavior.
		}
	}

	// Store original yaw values for potential restoration
	float yaw;
	const float old_yaw = self->s.angles[YAW];
	const float old_ideal_yaw = self->ideal_yaw;

	// Aim straight at the nav waypoint. (We previously blended only 60% toward it for sub-45 deg
	// changes to damp diagonal stutter, but that per-frame lag meant a monster never actually faced
	// the waypoint on a curving path - it cut corners and drifted off the line, arriving at the
	// wrong place when pursuing a lost target. Match the original: head directly at the point.)
	if (self->monsterinfo.random_change_time >= level.time &&
		!(self->monsterinfo.aiflags & AI_ALTERNATE_FLY))
		yaw = self->ideal_yaw;
	else
		yaw = vectoyaw((move_point - self->s.origin).normalized());

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
		}

		// Check if we've been blocked too long (PSX parity: evaluated on any primary-step failure,
		// even when the firstMovePoint retry succeeded, so an accumulated counter still bails)
		if (self->monsterinfo.path_blocked_counter > 1.5_sec)
			return false;
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
	{
		// [Horde] Benched after a failed solve. Falling to classic movement means beelining at
		// the enemy - for a cornered pack that's the same wall for everyone. If a comrade
		// recently solved a path nearby, walk toward its crumb (a position proven on-mesh and
		// in the routable component) and lift the bench on arrival; our own solve then tends
		// to succeed from there. Skipped when the enemy is visible - direct chase is right then.
		if (self->enemy && self->enemy->inuse && !visible(self, self->enemy, false))
		{
			vec3_t crumb;
			if (M_FindNavCrumb(self, crumb))
			{
				const vec3_t to_crumb = crumb - self->s.origin;
				if (to_crumb.lengthSquared() < 64.f * 64.f)
				{
					// At the proven-good spot: shorten the bench to a 1s retry cadence rather
					// than lifting it outright - the crumb may be our OWN recent solve spot
					// (solves fail here for goal-side reasons), and a full lift would re-solve
					// every other frame.
					self->monsterinfo.path_wait_time = std::min(self->monsterinfo.path_wait_time, level.time + 1_sec);
					return false;
				}

				const float cyaw = vectoyaw(to_crumb.normalized());
				if (SV_StepDirection(self, cyaw, dist, true))
					return true;

				if (self->monsterinfo.random_change_time < level.time)
				{
					self->monsterinfo.random_change_time = level.time + 1500_ms;
					if (SV_NewChaseDir(self, crumb, dist))
						return true;
				}
			}
		}
		return false;
	}
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
				// PSX parity: no path_blocked_counter reset here. M_NavPathToGoal returns true even on
				// blocked frames, so resetting on "success" zeroed the counter every frame and both
				// escape hatches (the 1.5s internal bail and the 5s leash below) could never fire -
				// a nav-blocked monster stayed glued to nav forever, grinding the wall until the
				// teleport failsafe. The counter decays gradually in M_MoveToGoal instead.
				if (M_NavPathToGoal(self, dist, self->enemy->s.origin)) {
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
			return true;
		}
	}

	// 4. Mejorar el manejo de errores y bloqueos
	if (!self->inuse)
		return false;

	if (self->monsterinfo.nav_path.returnCode > PathReturnCode::StartPathErrors) {
		// Bench nav proportionally to how permanent the failure is:
		//  - NoStartNode/InvalidStart: WE are momentarily off-mesh (shoved by the horde, on a
		//    crate); the widened retry in M_NavPathToGoal already failed once, but we drift back
		//    on-mesh quickly - short bench.
		//  - NoGoalNode/InvalidGoal: the ENEMY is off-mesh; enemies move, retry moderately.
		//  - NoPathFound and the rest: genuinely disconnected - keep the long bench.
		gtime_t bench;
		switch (self->monsterinfo.nav_path.returnCode) {
		case PathReturnCode::NoStartNode:
		case PathReturnCode::InvalidStart:
			bench = 2_sec;
			break;
		case PathReturnCode::NoGoalNode:
		case PathReturnCode::InvalidGoal:
			bench = 3_sec;
			break;
		default:
			bench = 10_sec;
			break;
		}
		self->monsterinfo.path_wait_time = level.time + bench;
		return false;
	}

	// PSX parity: give the nav path a longer leash before abandoning it (was *2 / 3s, now *3 / 5s)
	// so monsters commit to a route instead of bailing to classic bump movement mid-path.
	self->monsterinfo.path_blocked_counter += FRAME_TIME_S * 3;

	if (self->monsterinfo.path_blocked_counter > 5_sec) {
		self->monsterinfo.path_blocked_counter = 0_ms;
		self->monsterinfo.path_wait_time = level.time + 5_sec;
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

	// bump around... (PSX parity: no bump_time gate here. We previously skipped straight to facing
	// the target while bump_time was active, but SV_movestep refreshes bump_time on nearly every
	// blocked frame, so that gate permanently starved the SV_NewChaseDir cardinal-direction search
	// below - a monster wedged in a corner could never run it and stayed stuck until it gained
	// visual. bump_time now only throttles SV_movestep's own wall-slide, as in the original.)
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