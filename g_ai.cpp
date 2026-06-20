// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_ai.c

#include "g_local.h"
#include <cfloat>
#include "horde/horde_performance.h"
#include "shared.h"
bool FindTarget(edict_t* self);
bool FindEnhancedTarget(edict_t* self);
bool ai_checkattack(edict_t* self, float dist);
trap_state_t* GetTrapState(const edict_t* ent);  // Forward declaration for trap cooldown check

bool    enemy_vis;
bool    enemy_infront;
float   enemy_yaw;

// ROGUE
constexpr float MAX_SIDESTEP = 8.0f;
// ROGUE

// Horde mode constants
constexpr float HORDE_BONUS_SPEED_MULTIPLIER = 1.6f;  // 60% faster movement for bonus-flagged monsters

// Inline helper functions for fast classname checks
// These avoid expensive strcmp calls in hot paths
[[nodiscard]] inline bool IsPlayerNoise(const edict_t* ent) {
	return ent && ent->classname && ent->classname[0] == 'p' && strcmp(ent->classname, "player_noise") == 0;
}

[[nodiscard]] inline bool IsPointCombat(const edict_t* ent) {
	return ent && ent->classname && ent->classname[0] == 'p' && strcmp(ent->classname, "point_combat") == 0;
}

[[nodiscard]] inline bool IsFollowPoint(const edict_t* ent) {
	return ent && ent->classname && ent->classname[0] == 'f' && strcmp(ent->classname, "follow_point") == 0;
}

[[nodiscard]] inline bool IsCommandEntity(const edict_t* ent) {
	return IsPointCombat(ent) || IsFollowPoint(ent);
}

// Clear dead/invalid command references while preserving point/follow command entities.
static inline void ValidateCommandEntity(edict_t*& ent) {
	if (!ent)
		return;

	if (!ent->inuse) {
		ent = nullptr;
		return;
	}

	if (ent->health <= 0 && ent->classname && !IsCommandEntity(ent))
		ent = nullptr;
}

//============================================================================
// Horde target distribution helpers
//
// These give hostile monsters a "nearest-biased random with light
// load-balancing" target choice instead of everyone locking onto the same
// (or merely the geometrically closest) player. Closest players are still
// the most likely pick, but monsters fan out across multiple players/bots
// instead of dogpiling one.
//============================================================================

// Upper bound for the per-client attacker tally. Client edicts are numbered
// 1..maxclients, so this comfortably covers any sane server.
static constexpr size_t HORDE_MAX_CLIENT_SLOTS = 256;

// Returns a table (indexed by client entity number) of how many hostile
// monsters are currently targeting each player. Rebuilt at most once per
// server frame and shared by every caller, so the cost is O(monsters) per
// frame total rather than per monster.
static const int* GetPlayerAttackerCounts()
{
	static int counts[HORDE_MAX_CLIENT_SLOTS];
	static gtime_t last_build = -1_ms;

	if (last_build != level.time)
	{
		last_build = level.time;
		memset(counts, 0, sizeof(counts));

		for (edict_t* m : active_monsters())
		{
			// Friendly spawns hunt monsters, not players, so they don't
			// contribute to player aggro pressure.
			if (m->monsterinfo.isfriendlyspawn)
				continue;

			edict_t* e = m->enemy;
			if (e && e->inuse && e->client)
			{
				const int idx = e->s.number;
				if (idx >= 1 && idx < (int)HORDE_MAX_CLIENT_SLOTS)
					counts[idx]++;
			}
		}
	}

	return counts;
}

// Weight a candidate player for selection: inverse-square distance (closer =
// higher) divided by current attacker load (busier player = lower). Always
// returns a strictly positive value so every reachable player keeps a chance.
static float HordeTargetWeight(edict_t* self, const edict_t* client, const int* attacker_counts)
{
	const float dist_sq = DistanceSquared(self->s.origin, client->s.origin);
	float weight = 1000000.0f / (dist_sq + 1.0f);

	const int idx = client->s.number;
	const int attackers = (idx >= 1 && idx < (int)HORDE_MAX_CLIENT_SLOTS) ? attacker_counts[idx] : 0;

	return weight / (1.0f + (float)attackers);
}

//============================================================================

/*
=================
AI_GetSightClient

For a given monster, check active players to see
who we can see. We don't care who we see, as long
as it's something we can shoot.
=================
*/
edict_t* AI_GetSightClient(edict_t* self)
{
	if (level.intermissiontime)
		return nullptr;

	// Use static buffer with reasonable maximum to avoid heap allocation
	constexpr size_t MAX_PLAYERS = 256;
	static thread_local edict_t* visible_players_buffer[MAX_PLAYERS];
	edict_t** visible_players = visible_players_buffer;
	size_t num_visible = 0;

	// Clamp to buffer size for safety
	const size_t max_clients = (game.maxclients < MAX_PLAYERS) ? game.maxclients : MAX_PLAYERS;

	// Cache values used in the loop
	const vec3_t& self_absmin = self->absmin;
	const vec3_t& self_absmax = self->absmax;

	for (auto player : active_players())
	{
		// Prevent buffer overflow
		if (num_visible >= max_clients)
			break;

		// Early out conditions grouped
		if (player->health <= 0 ||
			player->deadflag ||
			!player->solid ||
			player->flags & (FL_NOTARGET | FL_DISGUISED | FL_GODMODE))
			continue;

		// Cache box intersection
		bool touching = boxes_intersect(self_absmin, self_absmax,
			player->absmin, player->absmax);

		// Early exit if not touching and not in view
		if (!touching &&
			(!(self->monsterinfo.aiflags & AI_THIRD_EYE) &&
			 !infront(self, player)))
		{
			continue;
		}

		// OPTIMIZATION: PVS (Potentially Visible Set) check before expensive trace
		// This is a cheap engine lookup that avoids raycast if areas can't see each other
		if (!gi.inPVS(self->s.origin, player->s.origin, true)) [[unlikely]]
			continue;

		// Expensive visibility trace (last resort)
		if (!visible(self, player))
			continue;

		visible_players[num_visible++] = player;
	}

	// Choose among the visible players.
	edict_t* chosen_player = nullptr;
	if (num_visible == 1)
	{
		chosen_player = visible_players[0];
	}
	else if (num_visible > 1)
	{
		// Horde: nearest-biased weighted random with load-balancing so a
		// crowd of monsters doesn't all snap onto the same player on sight.
		if (g_horde->integer)
		{
			const int* attacker_counts = GetPlayerAttackerCounts();
			float weights[MAX_PLAYERS];
			float total_weight = 0.0f;

			for (size_t i = 0; i < num_visible; i++)
			{
				weights[i] = HordeTargetWeight(self, visible_players[i], attacker_counts);
				total_weight += weights[i];
			}

			float r = frandom() * total_weight;
			for (size_t i = 0; i < num_visible; i++)
			{
				r -= weights[i];
				if (r <= 0.0f)
				{
					chosen_player = visible_players[i];
					break;
				}
			}

			// Floating-point guard: always end up with a valid pick.
			if (!chosen_player)
				chosen_player = visible_players[num_visible - 1];
		}
		else
		{
			// Vanilla coop behavior: uniform random visible player.
			chosen_player = visible_players[irandom(num_visible)];
		}
	}

	return chosen_player;
}
//============================================================================

/*
=============
ai_move

Move the specified distance at current facing.
This replaces the QC functions: ai_forward, ai_back, ai_pain, and ai_painforward
==============
*/
void ai_move(edict_t* self, float dist)
{
	M_walkmove(self, self->s.angles[YAW], dist);
}

/*
=============
ai_stand

Used for standing around and looking for players
Distance is for slight position adjustments needed by the animations
==============
*/
/*
=============
FindNearestValidPlayer

Helper function to find the nearest valid player target for horde mode.
Returns nullptr if no valid player found.
=============
*/
static edict_t* FindNearestValidPlayer(edict_t* self)
{
	// Performance optimization: Use cached result if still valid
	// This reduces O(n*m) iterations where n=monsters, m=players
	// Cache is valid for 500ms (0.5 seconds) to balance responsiveness with performance
	if (self->monsterinfo.next_target_search_time > level.time)
	{
		// Validate cached player is still targetable
		if (self->monsterinfo.cached_nearest_player &&
			self->monsterinfo.cached_nearest_player->inuse &&
			self->monsterinfo.cached_nearest_player->health > 0 &&
			!EntIsSpectating(self->monsterinfo.cached_nearest_player))
		{
			if (self->monsterinfo.cached_nearest_player->client)
			{
				// Recheck invisible/protected status (can change quickly)
				if (self->monsterinfo.cached_nearest_player->client->invisible_time > level.time &&
					self->monsterinfo.cached_nearest_player->client->invisibility_fade_time <= level.time)
				{
					// Player became invisible, invalidate cache
					self->monsterinfo.cached_nearest_player = nullptr;
					self->monsterinfo.next_target_search_time = 0_ms;
				}
				else if (self->monsterinfo.cached_nearest_player->client->menu_protected)
				{
					// Player became menu-protected, invalidate cache
					self->monsterinfo.cached_nearest_player = nullptr;
					self->monsterinfo.next_target_search_time = 0_ms;
				}
				else
				{
					return self->monsterinfo.cached_nearest_player;
				}
			}
			else
			{
				return self->monsterinfo.cached_nearest_player;
			}
		}
		else
		{
			// Cached player invalid, clear cache
			self->monsterinfo.cached_nearest_player = nullptr;
			self->monsterinfo.next_target_search_time = 0_ms;
		}
	}

	// Cache expired or invalid, perform expensive search.
	//
	// Instead of a pure "closest player wins" pick (which made every monster
	// in a cluster lock onto the same player, and whose old early-out break
	// could even return a non-nearest player), gather every reachable player
	// and do a nearest-biased weighted random draw with load-balancing. The
	// closest/least-pressured player is still the most likely target, but the
	// crowd naturally fans out across players and bots.

	// Distance culling: skip players that are extremely far away.
	constexpr float MAX_SEARCH_DISTANCE = 4096.0f; // 4096 units
	constexpr float MAX_SEARCH_DISTANCE_SQ = MAX_SEARCH_DISTANCE * MAX_SEARCH_DISTANCE;

	edict_t* candidates[HORDE_MAX_CLIENT_SLOTS];
	float    weights[HORDE_MAX_CLIENT_SLOTS];
	size_t   num_candidates = 0;
	float    total_weight = 0.0f;

	const int* attacker_counts = GetPlayerAttackerCounts();

	// Gather every player that's alive, not a spectator, and targetable.
	for (auto client : active_players_no_spect())
	{
		if (num_candidates >= HORDE_MAX_CLIENT_SLOTS)
			break;

		if (client->client) {
			// Skip fully invisible players
			if (client->client->invisible_time > level.time &&
				client->client->invisibility_fade_time <= level.time) {
				continue;
			}
			// Skip menu-protected players
			if (client->client->menu_protected) {
				continue;
			}
			// Skip spectators (redundant check but kept for safety)
			if (EntIsSpectating(client)) {
				continue;
			}
		}
		if (client->inuse && client->health > 0)
		{
			const float dist_squared = DistanceSquared(self->s.origin, client->s.origin);

			// Distance culling: Skip if beyond max search distance
			if (dist_squared > MAX_SEARCH_DISTANCE_SQ) {
				continue;
			}

			const float weight = HordeTargetWeight(self, client, attacker_counts);
			candidates[num_candidates] = client;
			weights[num_candidates] = weight;
			total_weight += weight;
			num_candidates++;
		}
	}

	// Weighted random draw among reachable players.
	edict_t* chosen_player = nullptr;
	if (num_candidates == 1)
	{
		chosen_player = candidates[0];
	}
	else if (num_candidates > 1)
	{
		float r = frandom() * total_weight;
		for (size_t i = 0; i < num_candidates; i++)
		{
			r -= weights[i];
			if (r <= 0.0f)
			{
				chosen_player = candidates[i];
				break;
			}
		}

		// Floating-point guard: always end up with a valid pick.
		if (!chosen_player)
			chosen_player = candidates[num_candidates - 1];
	}

	// Cache the result for 500ms (0.5 seconds). This both saves the repeated
	// search and keeps the monster committed to its weighted choice instead of
	// re-rolling (and flip-flopping) every frame.
	self->monsterinfo.cached_nearest_player = chosen_player;
	self->monsterinfo.next_target_search_time = level.time + 500_ms;

	return chosen_player;
}

void ai_stand(edict_t* self, float dist)
{
	vec3_t v;
	// ROGUE
	bool retval;
	// ROGUE

	// Check if monster is paused for healing
	if (self->monsterinfo.healing_pause_time > level.time)
	{
		// Still being healed, just change yaw if needed but don't move
		if (self->enemy)
		{
			v = self->enemy->s.origin - self->s.origin;
			self->ideal_yaw = vectoyaw(v);
			M_ChangeYaw(self);
		}
		return;
	}

	if (dist || (self->monsterinfo.aiflags & AI_ALTERNATE_FLY))
		M_walkmove(self, self->s.angles[YAW], dist);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		// [Paril-KEX] check if we've been pushed out of our point_combat
		if (!(self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND) &&
			IsPointCombat(self->movetarget))
		{
			if (!boxes_intersect(self->absmin, self->absmax, self->movetarget->absmin, self->movetarget->absmax))
			{
				self->monsterinfo.aiflags &= ~AI_STAND_GROUND;
				self->monsterinfo.aiflags |= AI_COMBAT_POINT;
				self->goalentity = self->movetarget;
				self->monsterinfo.run(self);
				return;
			}
		}

		if (self->enemy && !IsPlayerNoise(self->enemy))
		{
			v = self->enemy->s.origin - self->s.origin;
			self->ideal_yaw = vectoyaw(v);
			if (!FacingIdeal(self) && (self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND))
			{
				self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
				self->monsterinfo.run(self);
			}
			// ROGUE
			if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
				// ROGUE
				M_ChangeYaw(self);
			// find out if we're going to be shooting
			retval = ai_checkattack(self, 0);
			// record sightings of player
			if ((self->enemy) && (self->enemy->inuse))
			{
				if (visible(self, self->enemy))
				{
					self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;
					self->monsterinfo.last_sighting = self->monsterinfo.saved_goal = self->enemy->s.origin;
					self->monsterinfo.blind_fire_target = self->monsterinfo.last_sighting + (self->enemy->velocity * -0.1f);
					self->monsterinfo.trail_time = level.time;
					self->monsterinfo.blind_fire_delay = 0_ms;
				}
				else
				{
					if (FindTarget(self))
						return;

					self->monsterinfo.aiflags |= AI_LOST_SIGHT;
				}

				// Paril: fixes rare cases of a stand ground monster being stuck
				// aiming at a sound target that they can still see
				if ((self->monsterinfo.aiflags & AI_SOUND_TARGET) && !retval)
				{
					if (FindTarget(self))
						return;
				}
			}
			// check retval to make sure we're not blindfiring
			else if (!retval)
			{
				FindTarget(self);
				return;
			}
			// ROGUE
		}
		else
			FindTarget(self);
		return;
	}

	// Paril: this fixes a bug somewhere else that sometimes causes
	// a monster to be given an enemy without ever calling HuntTarget.
	if (self->enemy && !(self->monsterinfo.aiflags & AI_SOUND_TARGET))
	{
		HuntTarget(self);
		return;
	}

	if (FindTarget(self))
		return;
		
	// *** START OF THE FIX ***
	// We move the idle function call to be BEFORE the pausetime check.
	// This is the most important change. It allows a monster's specific idle
	// function (like our corrected medic_idle) to take control and reset the
	// pausetime, preventing the unwanted stand->walk transition.
	if (!(self->spawnflags & SPAWNFLAG_MONSTER_AMBUSH) && (self->monsterinfo.idle) &&
		(level.time > self->monsterinfo.idle_time))
	{
		if (self->monsterinfo.idle_time)
		{
			self->monsterinfo.idle(self);
			self->monsterinfo.idle_time = level.time + random_time(15_sec, 30_sec);
		}
		else
		{
			self->monsterinfo.idle_time = level.time + random_time(15_sec);
		}
	}
	// *** END OF THE FIX ***

	if (level.time > self->monsterinfo.pausetime)
	{
		if (g_horde->integer && self->monsterinfo.isfriendlyspawn)
		{
			self->monsterinfo.run(self);
			return;
		}

		// Horde: rather than idly walking the patrol beat, a hostile monster
		// whose pause expired should acquire the nearest reachable player and
		// RUN at them. Only fall back to walking if nobody is in range.
		if (g_horde->integer)
		{
			edict_t* nearest_player = FindNearestValidPlayer(self);
			if (nearest_player)
			{
				self->enemy = nearest_player;
				FoundTarget(self);
				return;
			}
		}

		self->monsterinfo.walk(self);
		return;
	}


	// HORDESTAND: Check if we're in horde mode and the monster has no enemy
	if (g_horde->integer)
	{
		// Only summoned monsters use FindMTarget for targeting
		if (self->monsterinfo.isfriendlyspawn) {
			// Don't interfere with medics that are healing/resurrecting
			if (self->monsterinfo.aiflags & AI_MEDIC)
			{
				// Medic is busy healing/resurrecting, don't look for new targets
				// Just validate the current healing target is still valid
				if (!self->enemy || !self->enemy->inuse)
				{
					// Healing target lost, clear medic mode
					self->monsterinfo.aiflags &= ~AI_MEDIC;
					self->enemy = nullptr;
				}
				// Keep processing movement/animations; just skip FindMTarget below.
			}

		// Clean up dead/invalid entity references for summoned monsters
		// BUT preserve command entities (combat points, follow points)
		ValidateCommandEntity(self->enemy);
		ValidateCommandEntity(self->goalentity);
		ValidateCommandEntity(self->movetarget);


		// Allow monsters on patrol/orders to scan for enemies, but preserve their orders
		if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
		{
			// Only scan for enemies if we don't have a real enemy yet
			if (!self->enemy || IsCommandEntity(self->enemy))
			{
				// Find enemies while patrolling - spot1/spot2 preserved for resume
				if (!(self->monsterinfo.aiflags & AI_MEDIC))
					FindMTarget(self);
			}
			return;
		}

			if (!self->enemy ||
				(self->enemy->client && !self->enemy->monsterinfo.isfriendlyspawn)) { // If current enemy is a player, forget it
				self->enemy = nullptr;
			}
			if (!(self->monsterinfo.aiflags & AI_MEDIC))
				FindMTarget(self);
		}
		else
		{
			edict_t* nearest_player = FindNearestValidPlayer(self);
			if (nearest_player)
			{
				self->enemy = nearest_player;
				FoundTarget(self);
				return;
			}
		}
	}
}

/*
=============
ai_walk

The monster is walking it's beat
=============
*/
void ai_walk(edict_t* self, float dist)
{
	// Check if monster is paused for healing
	if (self->monsterinfo.healing_pause_time > level.time)
	{
		// Still being healed, don't move
		return;
	}

	edict_t* temp_goal = nullptr;

	if (!self->goalentity && (self->monsterinfo.aiflags & AI_GOOD_GUY))
	{
		vec3_t fwd;
		AngleVectors(self->s.angles, fwd, nullptr, nullptr);

		temp_goal = G_Spawn();
		temp_goal->s.origin = self->s.origin + fwd * 64;
		self->goalentity = temp_goal;
	}

	M_MoveToGoal(self, dist);

	if (temp_goal)
	{
		G_FreeEdict(temp_goal);
		self->goalentity = nullptr;
	}

	// check for noticing a player
	if (FindTarget(self))
		return;

	if ((self->monsterinfo.search) && (level.time > self->monsterinfo.idle_time))
	{
		if (self->monsterinfo.idle_time)
		{
			self->monsterinfo.search(self);
			self->monsterinfo.idle_time = level.time + random_time(15_sec, 30_sec);
		}
		else
		{
			self->monsterinfo.idle_time = level.time + random_time(15_sec);
		}
	}

	// HORDEWALK: Add horde mode logic similar to ai_stand
		if (g_horde->integer && !level.intermissiontime)
	{
		// Only summoned monsters use FindMTarget for targeting
		if (self->monsterinfo.isfriendlyspawn) {
		// Clean up dead/invalid entity references for summoned monsters
		// BUT preserve command entities (combat points, follow points)
		ValidateCommandEntity(self->enemy);
		ValidateCommandEntity(self->goalentity);
		ValidateCommandEntity(self->movetarget);


		// Allow monsters on patrol/orders to scan for enemies, but preserve their orders
		if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
		{
			// Only scan for enemies if we don't have a real enemy yet
			if (!self->enemy || IsCommandEntity(self->enemy))
			{
				// Find enemies while patrolling - spot1/spot2 preserved for resume
				FindMTarget(self);
			}
			return;
		}

			if (!self->enemy ||
				(self->enemy->client && !self->enemy->monsterinfo.isfriendlyspawn)) { // If current enemy is a player, forget it
				self->enemy = nullptr;
			}
			FindMTarget(self);
		}
		else
		{
			edict_t* nearest_player = FindNearestValidPlayer(self);
			if (nearest_player)
			{
				self->enemy = nearest_player;
				FoundTarget(self);
				return;
			}
		}
	}
}

/*
=============
ai_charge

Turns towards target and advances
Use this call with a distance of 0 to replace ai_face
==============
*/

// Returns true when the straight line of fire from self to target is physically obstructed by
// another body (a teammate, or another monster) instead of being clear. Unlike visible(), which
// only traces world geometry, this includes entities — so anyone standing in the way counts as
// blocked. Used so a monster re-targets the closest visible enemy (often the blocker itself)
// rather than staring and dry-firing through whoever is in front of it.
static bool M_AttackLaneBlocked(edict_t* self, edict_t* target)
{
	if (!self || !target || !target->inuse)
		return false;

	vec3_t start = self->s.origin;
	start.z += (self->viewheight != 0 ? self->viewheight : (self->maxs.z > 4 ? self->maxs.z - 4 : 0));

	// Mirror M_CheckClearShot's clear-shot test at the enemy's head and feet; the lane only counts
	// as blocked when BOTH aim points are obstructed by something that isn't our target. Hitting a
	// client (player) counts as clear since players are valid enemies, not a wasted shot.
	for (const float z : { static_cast<float>(target->viewheight), 0.f })
	{
		vec3_t end = target->s.origin;
		end.z += z;

		const trace_t tr = gi.traceline(start, end, self, MASK_PROJECTILE & ~CONTENTS_DEADMONSTER);

		if (tr.ent == target || (tr.ent && tr.ent->client) || tr.fraction > 0.8f)
			return false; // clear enough to at least one aim point
	}

	return true;
}

static bool M_HandleStalledAttackRetarget(edict_t* self, float dist)
{
	static constexpr gtime_t LOS_STALL_RETARGET_TIME = 500_ms;
	static constexpr float MELEE_GRACE_RANGE = RANGE_MELEE * 2.5f;

	if (!self || !self->enemy || !self->enemy->inuse)
	{
		if (self)
			self->monsterinfo.no_los_attack_time = 0_ms;
		return false;
	}

	// Only monitor in-place ranged attack flow; moving charge frames should continue normally.
	if (dist != 0.f || !self->monsterinfo.attack)
	{
		self->monsterinfo.no_los_attack_time = 0_ms;
		return false;
	}

	// Keep intentional blindfire behavior intact.
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.no_los_attack_time = 0_ms;
		return false;
	}

	// Don't interfere with medic corpse resurrection logic.
	if ((self->monsterinfo.aiflags & AI_MEDIC) && self->enemy->health <= 0)
	{
		self->monsterinfo.no_los_attack_time = 0_ms;
		return false;
	}

	// If the current enemy is dead/invalid, switch *immediately* — no melee grace, no LOS grace.
	// Dry-firing the attack animation at a corpse while another enemy is in view is exactly the
	// case we're fixing here.
	const bool enemy_invalid = (self->enemy->health <= 0 || self->enemy->deadflag);

	if (!enemy_invalid)
	{
		// If we're in melee range, let close-combat behavior proceed.
		if (range_to(self, self->enemy) <= MELEE_GRACE_RANGE)
		{
			self->monsterinfo.no_los_attack_time = 0_ms;
			return false;
		}

		// A clear line of sight only counts as "not stalled" when nothing is standing in our line of
		// fire. If a body (teammate or another enemy) blocks the shot, fall through and let the
		// retarget timer run so we switch to the closest visible enemy or reposition.
		if (visible(self, self->enemy, false) && !M_AttackLaneBlocked(self, self->enemy))
		{
			self->monsterinfo.no_los_attack_time = 0_ms;
			return false;
		}

		if (self->monsterinfo.no_los_attack_time == 0_ms)
		{
			self->monsterinfo.no_los_attack_time = level.time;
			return false;
		}

		if (level.time < self->monsterinfo.no_los_attack_time + LOS_STALL_RETARGET_TIME)
			return false;
	}

	edict_t* const previous_enemy = self->enemy;
	// Use the faction-correct picker: summoned/friendly units hunt monsters via FindMTarget,
	// horde monsters use the grid-based FindEnhancedTarget, everything else falls back to FindTarget.
	// All of these filter by OnSameTeam, so this can never cause same-team infighting.
	const bool found_target =
		self->monsterinfo.isfriendlyspawn ? FindMTarget(self) :
		g_horde->integer                  ? FindEnhancedTarget(self) :
		                                     FindTarget(self);

	// Throttle retries while still stuck.
	self->monsterinfo.no_los_attack_time = level.time;

	// Found another visible, valid enemy: keep the current attack animation and just turn to face
	// the new target, so the remaining attack frames fire at it instead of being wasted.
	if (found_target && self->enemy && self->enemy != previous_enemy)
	{
		self->goalentity = self->enemy;

		// Snap our aim toward the new enemy now; ai_charge keeps rotating us onto it each frame.
		const vec3_t dir = self->enemy->s.origin - self->s.origin;
		self->ideal_yaw = vectoyaw(dir);
		M_ChangeYaw(self);

		self->monsterinfo.no_los_attack_time = 0_ms;
		// Let ai_charge continue this frame; the frame's fire thinkfunc now targets the new enemy.
		return false;
	}

	// No better target available: still stop dry-firing and return to movement/stand logic.
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
		self->monsterinfo.nextframe = 0;
		self->monsterinfo.next_move_time = level.time;
		if (self->monsterinfo.stand)
			self->monsterinfo.stand(self);
	}
	else if (self->monsterinfo.run)
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
		self->monsterinfo.nextframe = 0;
		self->monsterinfo.next_move_time = level.time;
		self->monsterinfo.run(self);
	}

	return true;
}

void ai_charge(edict_t* self, float dist)
{

	if (!self || !self->inuse || !self->enemy || !self->enemy->inuse)
	{
		if (self)
			self->monsterinfo.no_los_attack_time = 0_ms;
		return;
	}

	if (M_HandleStalledAttackRetarget(self, dist))
		return;

	vec3_t v;
	float ofs;

	// PMM - made AI_MANUAL_STEERING affect things differently here .. they turn, but
	// don't set the ideal_yaw

	// This is put in there so monsters won't move towards the origin after killing
	// a tesla. This could be problematic, so keep an eye on it.
	// (Initial check already present and confirmed)

	// PMM - save blindfire target
	// --- Check added before accessing enemy members ---
	if (self->enemy && self->enemy->inuse && visible(self, self->enemy))
		self->monsterinfo.blind_fire_target = self->enemy->s.origin + (self->enemy->velocity * -0.1f);
	// --- End Check ---

	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
	{
		// --- Check added before accessing enemy origin ---
		if (self->enemy && self->enemy->inuse)
		{
			v = self->enemy->s.origin - self->s.origin;
			self->ideal_yaw = vectoyaw(v);
		}
		// --- End Check ---
	}

	M_ChangeYaw(self);

	if (dist || (self->monsterinfo.aiflags & AI_ALTERNATE_FLY))
	{
		if (self->monsterinfo.aiflags & AI_CHARGING)
		{
			M_MoveToGoal(self, dist);
			return;
		}
		// circle strafe support
		if (self->monsterinfo.attack_state == AS_SLIDING)
		{
			// if we're fighting a tesla, NEVER circle strafe
			// --- Check added before accessing enemy classname ---
			if (self->enemy && self->enemy->inuse && horde::IsSpecialType(self->enemy, horde::SpecialEntityTypeID::TESLA_MINE))
				ofs = 0;
			// --- End Check ---
			else if (self->monsterinfo.lefty)
				ofs = 90;
			else
				ofs = -90;

			dist *= self->monsterinfo.active_move->sidestep_scale;

			if (M_walkmove(self, self->ideal_yaw + ofs, dist))
				return;

			self->monsterinfo.lefty = !self->monsterinfo.lefty;
			M_walkmove(self, self->ideal_yaw - ofs, dist);
		}
		else
			M_walkmove(self, self->s.angles[YAW], dist);
	}

	// [Paril-KEX] if our enemy is literally right next to us, give
	// us more rotational speed so we don't get circled
	// --- Check added before accessing self->enemy for range_to ---
	static constexpr float RANGE_MELEE_EXTENDED = RANGE_MELEE * 2.5f;
	if (self->enemy && self->enemy->inuse && range_to(self, self->enemy) <= RANGE_MELEE_EXTENDED)
		M_ChangeYaw(self);
	// --- End Check ---
}
/*
=============
ai_turn

don't move, but turn towards ideal_yaw
Distance is for slight position adjustments needed by the animations
=============
*/
void ai_turn(edict_t* self, float dist)
{
	if (dist || (self->monsterinfo.aiflags & AI_ALTERNATE_FLY))
		M_walkmove(self, self->s.angles[YAW], dist);

	if (FindTarget(self))
		return;

	// ROGUE
	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
		// ROGUE
		M_ChangeYaw(self);
}

/*

.enemy
Will be world if not currently angry at anyone.

.movetarget
The next path spot to walk toward.  If .enemy, ignore .movetarget.
When an enemy is killed, the monster will try to return to it's path.

.hunt_time
Set to time + something when the player is in sight, but movement straight for
him is blocked.  This causes the monster to use wall following code for
movement direction instead of sighting on the player.

.ideal_yaw
A yaw angle of the intended direction, which will be turned towards at up
to 45 deg / state.  If the enemy is in view and hunt_time is not active,
this will be the exact line towards the enemy.

.pausetime
A monster will leave it's stand state and head towards it's .movetarget when
time > .pausetime.

walkmove(angle, speed) primitive is all or nothing
*/

/*
=============
range_to

returns the distance of an entity relative to self.
in general, the results determine how an AI reacts:
melee	melee range, will become hostile even if back is turned
near	visibility and infront, or visibility and show hostile
mid	    infront and show hostile
> mid	only triggered by damage
=============
*/
#include <cassert>
#include <cmath> 
// Constantes estáticas para mejorar rendimiento
static constexpr float MAX_RANGE = 1000.0f;
static constexpr float MAX_RANGE_SQUARED = MAX_RANGE * MAX_RANGE;
static constexpr float PRIORITY_ATTACKER_BONUS = 0.5f;

// Función range_to simplificada y más segura
float range_to(edict_t* self, const edict_t* other) {
	if (!self || !other)
		return MAX_RANGE;

	return distance_between_boxes(
		self->absmin, self->absmax,
		other->absmin, other->absmax);
}

// Función de comprobación de invisibilidad optimizada
inline bool IsInvisible(edict_t* ent) {
	if (!ent->client)
		return false;
	return ent->client->invisible_time > level.time &&
		ent->client->invisibility_fade_time <= level.time;
}

// Función de verificación de objetivo válido optimizada
inline bool IsValidTarget(edict_t* self, edict_t* ent) {
	if (!ent || !ent->inuse || ent == self ||
		ent->health <= 0 || ent->deadflag ||
		ent->solid == SOLID_NOT ||
		!(ent->svflags & SVF_MONSTER))
		return false;

	const gtime_t current_time = level.time;

	if (ent->client) {
		if (ent->client->invincible_time > current_time || IsInvisible(ent))
			return false;
	}

	return !OnSameTeam(self, ent) &&
		ent->monsterinfo.invincible_time <= current_time;
}

// Helper: IsValidTarget - Tailored for FindMTarget's needs (non-player, non-summoned monster)
static inline bool IsValidMonsterTargetForSummon(edict_t* self, edict_t* ent, gtime_t current_time) {
    // Basic validity checks
    if (!ent || !ent->inuse || ent == self || ent->client || ent->monsterinfo.isfriendlyspawn)
        return false;

    // Health and state checks
    if (ent->health <= 0 || ent->deadflag || ent->solid == SOLID_NOT)
        return false;

    // Team and invincibility checks
    if (OnSameTeam(self, ent) || ent->monsterinfo.invincible_time > current_time)
        return false;

    // Don't target prox mines
    if (horde::IsSpecialType(ent, horde::SpecialEntityTypeID::PROX_MINE))
        return false;

    return true;
}

// Helper: Calculate a score for a potential target
static float CalculateTargetPriority(edict_t* self, edict_t* target, float dist_squared) {
    // Pure distance-based targeting - closest enemy is always highest priority
    return 1000000.0f / (dist_squared + 1.0f);
}

#include "horde/g_horde_phys.h" 

bool FindMTarget(edict_t* self) {
	if (!self || !self->inuse || !self->monsterinfo.isfriendlyspawn || level.intermissiontime) {
		return false;
	}

	// --- Check if owner is menu protected ---
	// For strogg summoned monsters (have chain->teammaster)
	if (self->chain && self->chain->teammaster) {
		if (IsPlayerMenuProtected(self->chain->teammaster)) {
			self->enemy = nullptr;  // Clear enemy so they stop attacking
			return false;
		}
	}
	// For sentry guns (use owner field)
	else if (self->owner && self->owner->client) {
		if (IsPlayerMenuProtected(self->owner)) {
			self->enemy = nullptr;  // Clear enemy so they stop attacking
			return false;
		}
	}
	// For other summoned entities (have direct teammaster)
	else if (self->teammaster) {
		if (IsPlayerMenuProtected(self->teammaster)) {
			self->enemy = nullptr;  // Clear enemy so they stop attacking
			return false;
		}
	}

	// --- Clear Invalid Current Target ---
	if (self->enemy && !IsValidMonsterTargetForSummon(self, self->enemy, level.time)) {
		self->enemy = nullptr;
	}

	// If we have a valid, visible enemy, and it's not time to re-evaluate, stick with it.
	if (self->enemy && self->monsterinfo.react_to_damage_time > level.time && visible(self, self->enemy, false)) {
		return true;
	}

	// --- Find the Best Target ---
	edict_t* best_target = nullptr;
	float highest_priority = -1.0f;
	const vec3_t& self_origin = self->s.origin;
	const gtime_t current_time = level.time;

	// Consider the current enemy as a baseline candidate
	if (self->enemy) {
		if (visible(self, self->enemy, false)) {
			float dist_sq = DistanceSquared(self_origin, self->enemy->s.origin);
			highest_priority = CalculateTargetPriority(self, self->enemy, dist_sq);
			best_target = self->enemy;
		}
	}

	// --- Adjust range for sentry guns in fog ---
	float query_range = MAX_RANGE;
	float query_range_squared = MAX_RANGE_SQUARED;
	
	if (horde_fog_active && horde::IsMonsterType(self, horde::MonsterTypeID::SENTRYGUN)) {
		query_range = 400.0f;  // Reduced range for sentry guns in fog
		query_range_squared = query_range * query_range;
	}

	// --- THE OPTIMIZATION ---
	// Get potential targets from the grid instead of iterating all active monsters.
	const auto potential_targets = HordePhys::g_monster_grid.QueryRadius(self_origin, query_range);

	// --- Iterate Through POTENTIAL Monsters to Find a Better Target ---
	for (auto ent : potential_targets) { // <<<< KEY CHANGE HERE
		// --- Fast Rejection Checks ---
		if (!IsValidMonsterTargetForSummon(self, ent, current_time)) {
			continue;
		}

		// --- Distance Check (Math - Fast) ---
		// The grid gives us a square search area, so we still need a precise distance check.
		float dist_squared = DistanceSquared(self_origin, ent->s.origin);
		if (dist_squared > query_range_squared) {
			continue;
		}

		// --- Priority Calculation (Math - Fast) ---
		// Calculate BEFORE visibility check - if priority is too low, skip expensive trace
		float current_priority = CalculateTargetPriority(self, ent, dist_squared);
		if (current_priority <= highest_priority) {
			continue;  // Not better than what we have, skip expensive checks
		}

		// --- PVS Check (Engine Lookup - Fast) ---
		// Potentially Visible Set check - avoids raycast if areas can't see each other
		if (!gi.inPVS(self_origin, ent->s.origin, true)) [[unlikely]] {
			continue;
		}

		// --- Visibility Trace (Raycast - Expensive, Last Resort) ---
		if (!visible(self, ent, false)) {
			continue;
		}

		// --- Update Best Target ---
		highest_priority = current_priority;
		best_target = ent;
	}

	// --- Finalize Target ---
	if (best_target) {
		if (self->enemy != best_target) {
			self->enemy = best_target;
			self->monsterinfo.react_to_damage_time = current_time + 0.5_sec;
		}
		return true;
	}

	self->enemy = nullptr;
	return false;
}
/*
=============
visible

returns 1 if the entity is visible to self, even if not infront ()
=============
*/
bool visible(edict_t* self, edict_t* other, bool through_glass)
{
	// --- ADD THIS CHECK ---
	// If the 'other' entity pointer is null or the entity is not in use, it cannot be visible.
	if (!other || !other->inuse)
		return false;
	// --- END CHECK ---

	// never visible if flagged
	if (other->flags & FL_NOVISIBLE) // Now this access is safe
		return false;

	// Handle client-specific visibility (players, potentially bots)
	if (other->client)
	{
		// Always visible in rtest (special mode?)
		if (self->hackflags & HACKFLAG_ATTACK_PLAYER)
			return self->inuse; // Check self is valid too

		// Fix intermission / basic validity
		if (!other->solid) // No need for !other->inuse check here, covered above
			return false;

		// --- Menu Protection Check ---
		// Menu-protected players are completely invisible to monsters
		if (other->client->menu_protected)
			return false;

		// --- Invisibility Check ---
		if (other->client->invisible_time > level.time)
		{
			// Fully invisible (fade time expired or hasn't started)
			if (other->client->invisibility_fade_time <= level.time)
				return false;

			// --- Partially Invisible Handling ---
			bool self_is_monster = ((self->svflags & SVF_MONSTER) != 0);

			// If the viewer is NOT a monster, apply randomness
			if (!self_is_monster)
			{
				if (frandom() > other->s.alpha)
					return false; // Random chance failed for non-monster viewer
			}
			// If self_is_monster is true, we skip the frandom() check
			// --- End Partially Invisible Handling ---

		} // End invisibility check
	} // End client checks

	// --- Standard Line of Sight Trace ---
	vec3_t  spot1;
	vec3_t  spot2;
	trace_t trace;

	// Calculate eye positions (handle zero viewheight)
	spot1 = self->s.origin;
	spot1.z += (self->viewheight != 0 ? self->viewheight : (self->maxs.z > 4 ? self->maxs.z - 4 : 0));

	spot2 = other->s.origin;
	spot2.z += (other->viewheight != 0 ? other->viewheight : (other->maxs.z > 4 ? other->maxs.z - 4 : 0));

	// Determine trace mask
	contents_t mask = MASK_OPAQUE | CONTENTS_PROJECTILECLIP;
	if (!through_glass)
		mask |= CONTENTS_WINDOW;

	// Perform the trace
	trace = gi.trace(spot1, vec3_origin, vec3_origin, spot2, self, mask);

	// Check trace result
	return trace.fraction == 1.0f || trace.ent == other;
}
/*
=============
infront_cone

returns 1 if the entity is in front (in sight) of self
=============
*/
bool infront_cone(edict_t* self, edict_t* other, float cone)
{
	
		if (!self || !other)
		return false;

	vec3_t forward;

	AngleVectors(self->s.angles, forward, nullptr, nullptr);

	vec3_t vec = (other->s.origin - self->s.origin).normalized();

	return vec.dot(forward) > cone;
}

/*
=============
infront

returns 1 if the entity is in front (in sight) of self
=============
*/
bool infront(edict_t* self, edict_t* other)
{
	float cone = self->vision_cone;

	if (self->vision_cone < -1.0f)
	{
		// [Paril-KEX] if we're an ambush monster, reduce our cone of
		// vision to not ruin surprises, unless we already had an enemy.
		if (self->spawnflags.has(SPAWNFLAG_MONSTER_AMBUSH) && !self->monsterinfo.trail_time && !self->enemy)
			cone = 0.15f;
		else
			cone = -0.30f;
	}

	return infront_cone(self, other, cone);
}

//============================================================================

void HuntTarget(edict_t* self, bool animate_state)
{
	vec3_t vec;

	// --- Robust Safety Check ---
	if (!self || !self->inuse) {
		return; // Self check first
	}
	if (!self->enemy || !self->enemy->inuse)
	{
		// Log if needed:
		// if (developer->integer > 0) gi.Com_PrintFmt("HuntTarget: {} ({}) found invalid enemy pointer.\n", self->classname ? self->classname : "unknown", self->s.number);
		self->enemy = nullptr;
		self->goalentity = nullptr;
		if (self->monsterinfo.stand) {
			if (!FindTarget(self)) {
				self->monsterinfo.stand(self);
			}
		}
		return; // Prevent crash
	}
	// --- End Robust Safety Check ---

	// --- Original Logic ---
	self->goalentity = self->enemy;
	if (animate_state)
	{
		if (self->monsterinfo.aiflags & AI_STAND_GROUND)
			self->monsterinfo.stand(self);
		else
			self->monsterinfo.run(self);
	}
    // --- ADD EXTRA CHECK ---
    // Double-check enemy validity immediately before use, in case it became invalid
    // between the initial check and this point (e.g., use-after-free).
    if (!self->enemy || !self->enemy->inuse) {
        // Log if needed: gi.Com_PrintFmt("HuntTarget: Enemy became invalid between check and use for {} ({})\n", self->classname ? self->classname : "unknown", self->s.number);
        self->enemy = nullptr;
        self->goalentity = nullptr;
        // Attempt to recover state
        if (self->monsterinfo.stand) {
            if (!FindTarget(self)) { // Try finding a new target
                self->monsterinfo.stand(self); // If none, go back to standing
            }
        }
        return; // Prevent crash
    }
    // --- END EXTRA CHECK ---
	vec = self->enemy->s.origin - self->s.origin; // Access should now be safer
	self->ideal_yaw = vectoyaw(vec);
	// --- End Original Logic ---
}

void FoundTarget(edict_t* self)
{
	// --- Initial Checks ---
	if (!self || !self->inuse) return;

	// Check if enemy is valid *before* using it further
	if (!self->enemy || !self->enemy->inuse) {
		// Enemy became invalid between FindTarget setting it and now.
		// Clear pointer and maybe try finding again or go to stand.
		self->enemy = nullptr;
		if (self->monsterinfo.stand) {
			if (!FindTarget(self)) { // Try finding a new target immediately
				self->monsterinfo.stand(self); // If none found, default to stand
			}
		}
		return;
	}
	// --- End Initial Checks ---

	// Check if we are a summoned unit and the enemy is a player
	// EXCEPTION: If AI_MEDIC flag is set, allow targeting players for healing
	if (!(self->monsterinfo.aiflags & AI_MEDIC)) {
		if ((self->monsterinfo.isfriendlyspawn && !self->enemy) || (self->monsterinfo.isfriendlyspawn && self->enemy && self->enemy->client)) {
			self->enemy = nullptr;
			if (FindMTarget(self))
				return;
			self->monsterinfo.stand(self);
			return;
		}
	}

	// --- Player Noise Check ---
	// If the found enemy is a temporary sound entity, handle it differently.
	// Do not call HuntTarget for it, as it might be freed immediately.
	// Let ai_run handle moving towards the sound goal based on the AI_SOUND_TARGET flag.
	if (IsPlayerNoise(self->enemy))
	{
		self->monsterinfo.aiflags |= AI_SOUND_TARGET; // Ensure the flag is set

		// Set goalentity for movement functions (like M_MoveToGoal in ai_run)
		self->goalentity = self->enemy;

		// Trigger the appropriate movement state (run or stand)
		if (self->monsterinfo.aiflags & AI_STAND_GROUND) {
			self->monsterinfo.stand(self); // Stay standing, but face the sound eventually
		}
		else {
			self->monsterinfo.run(self); // Start running towards the sound
		}
		// DO NOT proceed to HuntTarget or the rest of FoundTarget for player_noise
		return;
	}
	// --- End Player Noise Check ---


	// --- Standard FoundTarget Logic (for valid players/monsters) ---
	// let other monsters see this monster for a while
	if (self->enemy->client)
	{
		// ROGUE
		if (self->enemy->flags & FL_DISGUISED)
			self->enemy->flags &= ~FL_DISGUISED;
		// ROGUE

		self->enemy->client->sight_entity = self;
		self->enemy->client->sight_entity_time = level.time;

		self->enemy->show_hostile = level.time + 1_sec; // wake up other monsters
	}

	if (!self->monsterinfo.trail_time)
		self->monsterinfo.attack_finished = level.time + 600_ms;
	self->monsterinfo.attack_finished += skill->integer == 0 ? 400_ms : skill->integer == 1 ? 200_ms : 0_ms;
	self->monsterinfo.last_sighting = self->monsterinfo.saved_goal = self->enemy->s.origin;
	self->monsterinfo.trail_time = level.time;
	self->monsterinfo.blind_fire_target = self->monsterinfo.last_sighting + (self->enemy->velocity * -0.1f);
	self->monsterinfo.blind_fire_delay = 0_ms;
	self->monsterinfo.fly_position_time = 0_ms;
	self->monsterinfo.aiflags &= ~AI_THIRD_EYE;

	// --- Call HuntTarget (or handle combattarget) ---
	if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
		return; // Already handled above if needed? Re-check original logic flow.

	// Final check before HuntTarget call
	if (!self->enemy || !self->enemy->inuse) return;

	if (!self->combattarget)
	{
		HuntTarget(self); // Call HuntTarget
		return;
	}

	// --- Combat Target Logic ---
	self->goalentity = self->movetarget = G_PickTarget(self->combattarget);
	if (!self->movetarget)
	{
		self->goalentity = self->movetarget = self->enemy;
		// Final check before HuntTarget call
		if (!self->enemy || !self->enemy->inuse) return;
		HuntTarget(self); // Call HuntTarget
		gi.Com_PrintFmt("{}: combattarget {} not found\n", *self, self->combattarget);
		return;
	}
	self->combattarget = nullptr;
	self->monsterinfo.aiflags |= AI_COMBAT_POINT;
	self->monsterinfo.pausetime = 0_ms;
	self->monsterinfo.run(self); // Run towards combat target
	// Note: HuntTarget is NOT called here in the original logic if combattarget is found.
}

// [Paril-KEX] monsters that were alerted by players will
// be temporarily stored on player entities, so we can
// check them & get mad at them even around corners
static edict_t* AI_GetMonsterAlertedByPlayers(edict_t* self)
{
	for (auto player : active_players())
	{
		// dead
		if (player->health <= 0 || player->deadflag || !player->solid)
			continue;

		// we didn't alert any other monster, or it wasn't recently
		if (!player->client->sight_entity || !(player->client->sight_entity_time >= (level.time - FRAME_TIME_S)))
			continue;

		// if we can't see the monster, don't bother
		if (!visible(self, player->client->sight_entity))
			continue;

		// probably good
		return player->client->sight_entity;
	}

	return nullptr;
}

// [Paril-KEX] per-player sounds
static edict_t* AI_GetSoundClient(edict_t* self, bool direct)
{
	edict_t* best_sound = nullptr;
	float best_distance = std::numeric_limits<float>::max();

	for (auto player : active_players())
	{
		// dead
		if (player->health <= 0 || player->deadflag || !player->solid)
			continue;

		edict_t* sound = direct ? player->client->sound_entity : player->client->sound2_entity;

		if (!sound)
			continue;

		// too late
		gtime_t& time = direct ? player->client->sound_entity_time : player->client->sound2_entity_time;

		if (!(time >= (level.time - FRAME_TIME_S)))
			continue;

		// prefer the closest one we heard
		float dist_sq = (sound->s.origin - self->s.origin).lengthSquared();

		if (!best_sound || dist_sq < best_distance)
		{
			best_distance = dist_sq;
			best_sound = sound;
		}
	}

	return best_sound;
}

bool G_MonsterSourceVisible(edict_t* self, edict_t* client)
{
	// First, verify if client or client->client are null
	if (!client || !client->client)
	{
		return false;
	}

	// If we are a summoned unit and the target is a player, it's not visible
	if (self->monsterinfo.isfriendlyspawn && client->client)
		return false;

	// Check if the player is a spectator or has no team in CTF mode
	if (EntIsSpectating(client))
	{
		return false;
	}

	// this is where we would check invisibility
	const float r = range_to(self, client);
	if (r > RANGE_MID)
		return false;
	// Paril: revised so that monsters can be woken up
	// by players 'seen' and attacked at by other monsters
	// if they are close enough. they don't have to be visible.
	const bool is_visible =
		((r <= RANGE_NEAR && client->show_hostile >= level.time && !(self->spawnflags & SPAWNFLAG_MONSTER_AMBUSH)) ||
			(visible(self, client) && (r <= RANGE_MELEE || (self->monsterinfo.aiflags & AI_THIRD_EYE) || infront(self, client))));
	return is_visible;
}

// Helper function for clarity
static inline bool IsValidTargetType(edict_t* ent) {
    if (ent->client) return true;
    if (ent->svflags & SVF_MONSTER) return true;
    if (horde::IsSpecialType(ent, horde::SpecialEntityTypeID::DOPPLEGANGER)) return true;
    return false;
}

bool FindEnhancedTarget(edict_t* self) {
    if (level.intermissiontime)
        return false;

    if (self->monsterinfo.isfriendlyspawn) {
        // Allow monsters on patrol/orders to scan for enemies
        if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
        {
            if (!self->enemy || IsCommandEntity(self->enemy))
            {
                return FindMTarget(self);
            }
            return false;
        }
        return FindMTarget(self);
    }

    edict_t* best_target = nullptr;
    float best_dist_sq = MAX_RANGE_SQUARED;

    // --- THE OPTIMIZATION ---
    // Use the correct grid name: g_monster_grid
    const auto potential_targets = HordePhys::g_monster_grid.QueryRadius(self->s.origin, MAX_RANGE);

    // Loop through the much smaller list of potential targets
    for (auto ent : potential_targets)
    {
        // --- All the filtering logic below is copied directly from your original function ---
        if (!ent->inuse || ent == self || ent->health <= 0 || ent->deadflag) {
            continue;
        }
        if (OnSameTeam(self, ent) || !IsValidTargetType(ent)) {
            continue;
        }

        if (ent->client) {
            if (IsInvisible(ent)) { continue; }
            if (EntIsSpectating(ent)) { continue; }
        }

        float dist_squared = DistanceSquared(self->s.origin, ent->s.origin);
        if (dist_squared > best_dist_sq) {
            continue;
        }

        if (visible(self, ent, false)) {
            best_dist_sq = dist_squared;
            best_target = ent;
        }
    }

    if (best_target) {
        self->enemy = best_target;
        return true;
    }

    return false;
}
/*
===========
FindTarget

Self is currently not attacking anything, so try to find a target

Returns TRUE if an enemy was sighted

When a player fires a missile, the point of impact becomes a fakeplayer so
that monsters that see the impact will respond as if they had seen the
player.

To avoid spending too much time, only a single client (or fakeclient) is
checked each frame.  This means multi player games will have slightly
slower noticing monsters.
============
*/
bool FindTarget(edict_t* self)
{
	if (level.intermissiontime)
		return false;

	// Don't interrupt medics that are healing/resurrecting
	if (self->monsterinfo.aiflags & AI_MEDIC)
		return false;

	// First check if it's a summoned unit that has a player as enemy
	// EXCEPTION: If AI_MEDIC flag is set, allow targeting players for healing
	if (!(self->monsterinfo.aiflags & AI_MEDIC)) {
		if ((self->monsterinfo.isfriendlyspawn && !self->enemy) ||
		    (self->monsterinfo.isfriendlyspawn && self->enemy && self->enemy->client)) {
			self->enemy = nullptr;
			return FindMTarget(self);
		}
	}

	edict_t* client = nullptr;
	bool     heardit;
	bool     ignore_sight_sound = false;

	// [Paril-KEX] if we're in a level transition, don't worry about enemies
	if (globals.server_flags & SERVER_FLAG_LOADING)
		return false;

	// N64 cutscene behavior
	if (self->hackflags & HACKFLAG_END_CUTSCENE)
		return false;

	if (self->monsterinfo.aiflags & AI_GOOD_GUY)
	{
		if (self->goalentity && self->goalentity->inuse && self->goalentity->classname)
		{
			if (strcmp(self->goalentity->classname, "target_actor") == 0)
				return false;
		}

		// FIXME look for monsters?
		return false;
	}

	// if we're going to a combat point, just proceed
	if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
		return false;

	// if the first spawnflag bit is set, the monster will only wake up on
	// really seeing the player, not another monster getting angry or hearing
	// something

	// revised behavior so they will wake up if they "see" a player make a noise
	// but not weapon impact/explosion noises
	heardit = false;

	// Paril: revised so that monsters will first try to consider
	// the current sight client immediately if they can see it.
	// this fixes them dancing in front of you if you fire every frame.
	if ((client = AI_GetSightClient(self)))
	{
		if (client == self->enemy)
		{
			return false;
		}
	}
	// check indirect sources
	if (!client)
	{
		// check monsters that were alerted by players; we can only be alerted if we
		// can see them
		if (!(self->spawnflags & SPAWNFLAG_MONSTER_AMBUSH) && (client = AI_GetMonsterAlertedByPlayers(self)))
		{
			// KEX_FIXME: when does this happen? 
			// [Paril-KEX] adjusted to clear the client
			// so we can try other things
			if (client->enemy == self->enemy ||
				!G_MonsterSourceVisible(self, client))
				client = nullptr;
		}
		// ROGUE

		if (client == nullptr)
		{
			if (level.disguise_violation_time > level.time)
			{
				client = level.disguise_violator;
			}
			// ROGUE
			else if ((client = AI_GetSoundClient(self, true)))
			{
				heardit = true;
			}
			else
			{
				// Horde behavior: before we've had confirmed visual contact, allow
				// impact/explosion noises to override fallback player targeting so
				// monsters investigate heard shots/explosions.
				const bool can_investigate_impact_noise =
					!(self->spawnflags & SPAWNFLAG_MONSTER_AMBUSH) &&
					(!self->enemy ||
					 (g_horde->integer && !self->monsterinfo.isfriendlyspawn && !self->monsterinfo.had_visibility));

				if (can_investigate_impact_noise && (client = AI_GetSoundClient(self, false)))
				{
					heardit = true;
				}
			}
		}
	}

	// Early cooldown check - prevent processing if already on cooldown (prevents animation looping)
	if ((g_horde->integer && heardit && !self->monsterinfo.isfriendlyspawn) ||
		(g_horde->integer && heardit && !self->monsterinfo.issummoned))
	{
		if (self->monsterinfo.lastnoisecooldown > level.time)
		{
			return false; // Still on cooldown from previous sound
		}
	}

	if (!client)
	{
		if (g_horde->integer)
		{
			// Use the same improved logic for all, including sentry gun
			return FindEnhancedTarget(self);
		}
		return false; // No targets found
	}

	// if the entity went away, forget it
	if (!client->inuse)
		return false;

	if (client == self->enemy)
	{
		bool skip_found = true;

		// [Paril-KEX] slight special behavior if we are currently going to a sound
		// and we hear a new one; because player noises are re-used, this can leave
		// us with the "same" enemy even though it's a different noise.
		if (heardit && (self->monsterinfo.aiflags & AI_SOUND_TARGET))
		{
			const     vec3_t temp = client->s.origin - self->s.origin;
			self->ideal_yaw = vectoyaw(temp);

			if (!FacingIdeal(self))
				skip_found = false;
			else if (!SV_CloseEnough(self, client, 8.f))
				skip_found = false;

			if (!skip_found && (self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND))
			{
				self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
			}
		}

		if (skip_found)
			return true; // JDC false;
	}

	// ROGUE - hintpath coop fix
	bool const uses_hint_path = (self->monsterinfo.aiflags & AI_HINT_PATH);

	// Check the mode-specific conditions
	bool const mode_condition = G_IsCooperative() || (G_IsDeathmatch() && g_horde->integer);

	// Combine: If it uses hint paths AND (it's Coop OR it's Deathmatch Horde)
	if ( uses_hint_path && mode_condition ) {
		heardit = false;
	}
	// ROGUE

	if (client->svflags & SVF_MONSTER)
	{
		if (!client->enemy)
			return false;
		if (client->enemy->flags & FL_NOTARGET)
			return false;
	}
	else if (heardit)
	{
		// pgm - a little more paranoia won't hurt....
		if ((client->owner) && (client->owner->flags & FL_NOTARGET))
			return false;
	}
	else if (!client->client)
		return false;

	if (!heardit)
	{
		// this is where we would check invisibility
		float r = range_to(self, client);

		if (r > RANGE_MID)
			return false;

		// Paril: revised so that monsters can be woken up
		// by players 'seen' and attacked at by other monsters
		// if they are close enough. they don't have to be visible.
		bool is_visible =
			((r <= RANGE_NEAR && client->show_hostile >= level.time && !(self->spawnflags & SPAWNFLAG_MONSTER_AMBUSH)) ||
				(visible(self, client) && (r <= RANGE_MELEE || (self->monsterinfo.aiflags & AI_THIRD_EYE) || infront(self, client))));

		if (!is_visible)
			return false;

		self->enemy = client;

		if (!IsPlayerNoise(self->enemy))
		{
			self->monsterinfo.aiflags &= ~AI_SOUND_TARGET;

			if (!self->enemy->client)
			{
				self->enemy = self->enemy->enemy;
				if (!self->enemy->client)
				{
					self->enemy = nullptr;
					return false;
				}
			}
		}

		if (self->enemy->client &&
			((self->enemy->client->invisible_time > level.time && self->enemy->client->invisibility_fade_time <= level.time) ||
			 self->enemy->client->menu_protected))
		{
			self->enemy = nullptr;
			return false;
		}

		if (self->monsterinfo.close_sight_tripped)
			ignore_sight_sound = true;
		else
			self->monsterinfo.close_sight_tripped = true;
	}
	else // heardit
	{
		vec3_t temp;

		if (self->spawnflags.has(SPAWNFLAG_MONSTER_AMBUSH))
		{
			if (!visible(self, client))
				return false;
		}
		else
		{
			if (!gi.inPHS(self->s.origin, client->s.origin, true))
				return false;
		}

		temp = client->s.origin - self->s.origin;

		if ((client->s.origin - self->s.origin).lengthSquared() > 1000000) // too far to hear (1000^2)
			return false;

		// check area portals - if they are different and not connected then we can't hear it
		if (client->areanum != self->areanum)
			if (!gi.AreasConnected(self->areanum, client->areanum))
				return false;

		self->ideal_yaw = vectoyaw(temp);
		// ROGUE
		if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
			// ROGUE
			M_ChangeYaw(self);

		// hunt the sound for a bit; hopefully find the real player
		self->monsterinfo.aiflags |= AI_SOUND_TARGET;
		self->enemy = client;
	}

	//
	// got one
	//
	// ROGUE - if we got an enemy, we need to bail out of hint paths, so take over here
	if (self->monsterinfo.aiflags & AI_HINT_PATH)
		hintpath_stop(self);  // this calls foundtarget for us
	else
		FoundTarget(self);

	// ROGUE
	if (!(self->monsterinfo.aiflags & AI_SOUND_TARGET) && (self->monsterinfo.sight) &&
		// Paril: adjust to prevent monsters getting stuck in sight loops
		!ignore_sight_sound)
		self->monsterinfo.sight(self, self->enemy);

	// Late cooldown set - only set cooldown on successful target acquisition
	if ((g_horde->integer && heardit && !self->monsterinfo.isfriendlyspawn) ||
		(g_horde->integer && heardit && !self->monsterinfo.issummoned))
	{
		self->monsterinfo.lastnoisecooldown = level.time + 3.5_sec; //hordehear cooldown, without the cooldown, monsters will be looping on each noise they hear and will be looking very awful changing animations in each frame
	}

	return true;
}

//=============================================================================

/*
============
FacingIdeal

============
*/
bool FacingIdeal(edict_t* self)
{
    float delta = anglemod(self->s.angles[YAW] - self->ideal_yaw);

    // Path-following uses relaxed threshold (20°) to prevent stuttering during diagonal movement
    // The tight 5° threshold caused monsters to constantly revert position while turning
    if (self->monsterinfo.aiflags & AI_PATHING)
        return delta <= 20 || delta >= 340;

    return delta <= 45 || delta >= 315;
}

//=============================================================================

// [Paril-KEX] split this out so we can use it for the other bosses
bool M_CheckAttack_Base(edict_t* self, float stand_ground_chance, float melee_chance, float near_chance, float mid_chance, float far_chance, float strafe_scalar)
{
	vec3_t  spot1, spot2;
	float   chance;
	trace_t tr;

	//// Validar enemigo
	if (!self->enemy || !self->enemy->inuse ||
		OnSameTeam(self, self->enemy) || self->enemy->deadflag ||
		(self->monsterinfo.isfriendlyspawn && self->enemy->monsterinfo.isfriendlyspawn))
	{
		return false;
	}
	if (self->enemy->flags & FL_NOVISIBLE)
		return false;

	if (self->enemy && self->enemy->health > 0)
	{
		if (self->enemy->client)
		{
			// Check for invisibility or menu protection
			if (self->enemy->client->invisible_time > level.time ||
				self->enemy->client->menu_protected)
			{
				// can't see us at all after this time
				if (self->enemy->client->invisibility_fade_time <= level.time)
					return false;
			}
		}

		spot1 = self->s.origin;
		spot1[2] += self->viewheight;
		// see if any entities are in the way of the shot
		if (!self->enemy->client || self->enemy->solid)
		{
			spot2 = self->enemy->s.origin;
			spot2[2] += self->enemy->viewheight;

			tr = gi.traceline(spot1, spot2, self,
				MASK_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER | CONTENTS_SLIME | CONTENTS_LAVA | CONTENTS_PROJECTILECLIP);
			// Paril: horde
			if (tr.startsolid)
				return false;
		}
		else
		{
			tr.ent = world;
			tr.fraction = 0;
		}

		// do we have a clear shot?
		if (!(self->hackflags & HACKFLAG_ATTACK_PLAYER) && tr.ent != self->enemy && !(tr.ent->svflags & SVF_PLAYER))
		{
			// ROGUE - we want them to go ahead and shoot at info_notnulls if they can.
			if (self->enemy->solid != SOLID_NOT || tr.fraction < 1.0f) // PGM
			{
				// PMM - if we can't see our target, and we're not blocked by a monster, go into blind fire if available
				// Paril - *and* we have at least seen them once
				if (!(tr.ent->svflags & SVF_MONSTER) && !visible(self, self->enemy) && self->monsterinfo.had_visibility)
				{
					if (self->monsterinfo.blindfire && (self->monsterinfo.blind_fire_delay <= 20_sec))
					{
						if (level.time < self->monsterinfo.attack_finished)
						{
							// ROGUE
							return false;
						}
						// ROGUE
						if (level.time < (self->monsterinfo.trail_time + self->monsterinfo.blind_fire_delay))
						{
							// wait for our time
							return false;
						}
						else
						{
							// make sure we're not going to shoot a monster
							tr = gi.traceline(spot1, self->monsterinfo.blind_fire_target, self,
								CONTENTS_MONSTER | CONTENTS_PROJECTILECLIP); // Paril: horde);

							if (tr.allsolid || tr.startsolid || ((tr.fraction < 1.0f) && (tr.ent != self->enemy)))
								return false;

							self->monsterinfo.attack_state = AS_BLIND;
							return true;
						}
					}
				}
				// pmm
				return false;
			}
		}
	}
	// ROGUE

	float enemy_range = range_to(self, self->enemy);

	// Don't allow attacks if enemy is behind us (prevents backwards shooting)
	// Allow blindfire (AI_MANUAL_STEERING) and bosses to override this check
	if (!enemy_infront && !self->monsterinfo.IS_BOSS && !(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
	{
		return false;
	}

	// melee attack
	if (enemy_range <= RANGE_MELEE)
	{
		if (self->monsterinfo.melee && self->monsterinfo.melee_debounce_time <= level.time)
			self->monsterinfo.attack_state = AS_MELEE;
		else
			self->monsterinfo.attack_state = AS_MISSILE;
		return true;
	}

	// if we were in melee just before this but we're too far away, get out of melee state now
	if (self->monsterinfo.attack_state == AS_MELEE && self->monsterinfo.melee_debounce_time > level.time)
		self->monsterinfo.attack_state = AS_MISSILE;

	// missile attack
	if (!self->monsterinfo.attack)
	{
		// ROGUE - fix for melee only monsters & strafing
		self->monsterinfo.attack_state = AS_STRAIGHT;
		// ROGUE
		return false;
	}

	if (level.time < self->monsterinfo.attack_finished)
		return false;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		chance = stand_ground_chance;
	}
	else if (enemy_range <= RANGE_MELEE)
	{
		chance = melee_chance;
	}
	else if (enemy_range <= RANGE_NEAR)
	{
		chance = near_chance;
	}
	else if (enemy_range <= RANGE_MID)
	{
		chance = mid_chance;
	}
	else
	{
		chance = far_chance;
	}

	// PGM - go ahead and shoot every time if it's a info_notnull
	if ((!self->enemy->client && self->enemy->solid == SOLID_NOT) || (frandom() < chance))
	{
		self->monsterinfo.attack_state = AS_MISSILE;
		self->monsterinfo.attack_finished = level.time;
		return true;
	}

	// ROGUE -daedalus should strafe more .. this can be done here or in a customized
	// check_attack code for the hover.
	if (self->flags & FL_FLY)
	{
		if (self->monsterinfo.strafe_check_time <= level.time)
		{
			// originally, just 0.3
			float strafe_chance;

			if (horde::IsMonsterType(self, horde::MonsterTypeID::DAEDALUS) || 
			horde::IsMonsterType(self, horde::MonsterTypeID::DAEDALUS_BOMBER))
				strafe_chance = 0.8f;
			else
				strafe_chance = 0.6f;

			// if enemy is tesla, never strafe
			if ((self->enemy) && horde::IsSpecialType(self->enemy, horde::SpecialEntityTypeID::TESLA_MINE))
			{ // Added braces
				strafe_chance = 0;
			} // End of if block
			else
			{ // Added braces - THIS MAKES THE SCOPE CLEAR
				strafe_chance *= strafe_scalar;
			} // End of else block
		
			// Apply bonus evasion (higher strafe chance)
			// Now this IF is clearly separate and its indentation is no longer misleading
			if (self->monsterinfo.bonus_flags != BF_NONE && !(self->monsterinfo.bonus_flags & BF_FRIENDLY) && !self->monsterinfo.IS_BOSS) {
				strafe_chance = std::min(strafe_chance * 1.5f, 0.95f); // Increase chance by 50%, cap at 95%
			}
		
			// The rest of the logic follows, now clearly separate
			if (strafe_chance > 0) // Using > 0 for float comparison clarity
			{
				monster_attack_state_t new_state = AS_STRAIGHT;
		
				if (frandom() < strafe_chance)
					new_state = AS_SLIDING;
		
				if (new_state != self->monsterinfo.attack_state)
				{
					self->monsterinfo.strafe_check_time = level.time + random_time(1_sec, 3_sec);
					self->monsterinfo.attack_state = new_state;
				}
			}
		}
	}
	// do we want the monsters strafing?
	// [Paril-KEX] no, we don't
	// [Paril-KEX] if we're pathing, don't immediately reset us to
	// straight; this allows us to turn to fire and not jerk back and
	// forth.
	else if (!(self->monsterinfo.aiflags & AI_PATHING))
		self->monsterinfo.attack_state = AS_STRAIGHT;
	// ROGUE

	return false;
}

MONSTERINFO_CHECKATTACK(M_CheckAttack) (edict_t* self) -> bool
{
	return M_CheckAttack_Base(self, 0.7f, 0.4f, 0.25f, 0.06f, 0.f, 1.0f);
}

/*
=============
ai_run_melee

Turn and close until within an angle to launch a melee attack
=============
*/
void ai_run_melee(edict_t* self)
{
	self->ideal_yaw = enemy_yaw;
	// ROGUE
	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
		// ROGUE
		M_ChangeYaw(self);

	if (FacingIdeal(self))
	{
		self->monsterinfo.melee(self);
		self->monsterinfo.attack_state = AS_STRAIGHT;
	}
}

/*
=============
ai_run_missile

Turn in place until within an angle to launch a missile attack
=============
*/
void ai_run_missile(edict_t* self)
{
	self->ideal_yaw = enemy_yaw;
	// ROGUE
	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
		// ROGUE
		M_ChangeYaw(self);

	// Special-case medics resurrecting corpses: don't require facing when on top of target.
	if ((self->monsterinfo.aiflags & AI_MEDIC) && self->enemy && self->enemy->health <= 0)
	{
		if (self->monsterinfo.attack)
		{
			self->monsterinfo.attack(self);
			self->monsterinfo.attack_finished = level.time + random_time(1_sec, 2_sec);
		}
		return;
	}

	if (FacingIdeal(self))
	{
		if (self->monsterinfo.attack)
		{
			self->monsterinfo.attack(self);
			// Apply bonus aggression (faster attacks)
			gtime_t attack_cooldown_min = 1.0_sec;
			gtime_t attack_cooldown_max = 2.0_sec;
			if (self->monsterinfo.bonus_flags != BF_NONE && !(self->monsterinfo.bonus_flags & BF_FRIENDLY) && !self->monsterinfo.IS_BOSS) {
				attack_cooldown_min = 0.4_sec; // Faster min cooldown
				attack_cooldown_max = 0.7_sec; // Faster max cooldown
			}
			self->monsterinfo.attack_finished = level.time + random_time(attack_cooldown_min, attack_cooldown_max);
		}

		// ROGUE
		if ((self->monsterinfo.attack_state == AS_MISSILE) || (self->monsterinfo.attack_state == AS_BLIND))
			// ROGUE
			self->monsterinfo.attack_state = AS_STRAIGHT;
	}
}

/*
=============
ai_run_slide

Strafe sideways, but stay at aproximately the same range
=============
*/
// ROGUE
void ai_run_slide(edict_t* self, float distance)
{
	float ofs;
	float angle;

	self->ideal_yaw = enemy_yaw;

	angle = 90;

	if (self->monsterinfo.lefty)
		ofs = angle;
	else
		ofs = -angle;

	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
		M_ChangeYaw(self);

	// PMM - clamp maximum sideways move for non flyers to make them look less jerky
	if (!(self->flags & FL_FLY)) {
		const float max_sidestep_per_frame = gi.frame_time_ms > 0
			? (MAX_SIDESTEP * 10.0f / (float)gi.frame_time_ms)
			: MAX_SIDESTEP;
		distance = std::min(distance, max_sidestep_per_frame);
	}
	if (M_walkmove(self, self->ideal_yaw + ofs, distance))
		return;
	// PMM - if we're dodging, give up on it and go straight
	if (self->monsterinfo.aiflags & AI_DODGING)
	{
		monster_done_dodge(self);
		// by setting as_straight, caller will know to try straight move
		self->monsterinfo.attack_state = AS_STRAIGHT;
		return;
	}

	self->monsterinfo.lefty = !self->monsterinfo.lefty;
	if (M_walkmove(self, self->ideal_yaw - ofs, distance))
		return;
	// PMM - if we're dodging, give up on it and go straight
	if (self->monsterinfo.aiflags & AI_DODGING)
		monster_done_dodge(self);

	// PMM - the move failed, so signal the caller (ai_run) to try going straight
	self->monsterinfo.attack_state = AS_STRAIGHT;

	if (!self->enemy && self->monsterinfo.isfriendlyspawn && !(self->monsterinfo.aiflags & AI_COMBAT_POINT))
		FindMTarget(self);
}
// ROGUE

/*
=============
ai_checkattack

Decides if we're going to attack or do something else
used by ai_run and ai_stand
=============
*/
bool ai_checkattack(edict_t* self, float dist)
{

		if (!self || !self->inuse || !self->enemy || !self->enemy->inuse)
	{
		// If the enemy is invalid, we cannot and should not attack.
		// Try to find a new target or go back to standing.
		if (FindTarget(self))
		{
			// Found a new target, but don't attack this frame.
			return false;
		}
		if (self->monsterinfo.stand)
		{
			self->monsterinfo.stand(self);
		}
		return false;
	}

	if (level.intermissiontime)
		return false;
	vec3_t temp;
	bool   hesDeadJim;
	bool   retval; // Stores the result of the monster-specific checkattack

	if (self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND)
		self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);

	// This causes monsters to run blindly to the combat point w/o firing
	if (self->goalentity)
	{
		if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
		{
			// If we have an enemy and they are far away while we're moving to a combat point, don't attack yet.
			// Need null check for self->enemy before accessing it.
			if (self->enemy && self->enemy->inuse && range_to(self, self->enemy) > 100.f)
				return false;
		}

		if (self->monsterinfo.aiflags & AI_SOUND_TARGET)
		{
			// If the sound target hasn't been updated recently, stop pursuing it.
			// Need null check for self->enemy before accessing teleport_time.
			if (!self->enemy || !self->enemy->inuse || (level.time - self->enemy->teleport_time) > 5_sec)
			{
				if (self->goalentity == self->enemy) // If our goal was the sound target
				{
					if (self->movetarget) // Revert to path target if available
						self->goalentity = self->movetarget;
					else
						self->goalentity = nullptr;
				}
				self->monsterinfo.aiflags &= ~AI_SOUND_TARGET;
				self->enemy = nullptr; // Clear the sound target as enemy
			}
			else // Sound target is still fresh
			{
				self->enemy->show_hostile = level.time + 1_sec; // Keep alerting others
				return false; // Don't attack a sound target directly
			}
		}
	}

	enemy_vis = false; // Reset global flag

	// --- See if the current enemy is dead or invalid ---
	hesDeadJim = false;
	if (!self->enemy || !self->enemy->inuse) // Check pointer and inuse flag
	{
		hesDeadJim = true;
	}
	else if (self->monsterinfo.aiflags & AI_FORGET_ENEMY) // Flagged to forget
	{
		self->monsterinfo.aiflags &= ~AI_FORGET_ENEMY;
		hesDeadJim = true;
	}
	else if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		// In MEDIC mode, allow corpses (health <= 0) unless gibbed; otherwise require need regen.
		if (self->enemy->health <= 0)
		{
			if (self->enemy->gib_health && self->enemy->health < self->enemy->gib_health)
				hesDeadJim = true; // Gibbed corpse; can't revive.
		}
		else if (!M_NeedRegen(self->enemy))
			hesDeadJim = true; // Target fully recovered (health and armor).
		// Otherwise, hesDeadJim remains false, and we proceed.
	}
	else // Standard checks
	{
		if (!(self->monsterinfo.aiflags & AI_BRUTAL)) // Non-brutal monsters stop attacking dead things
		{
			if (self->enemy->health <= 0)
				hesDeadJim = true;
		}

		// Check for fully invisible or menu-protected players when pursuing
		// Ensure self->enemy->client exists before accessing it
		if (!hesDeadJim && self->enemy->client &&
			((self->enemy->client->invisible_time > level.time &&
			  self->enemy->client->invisibility_fade_time <= level.time) ||
			 self->enemy->client->menu_protected) &&
			(self->monsterinfo.aiflags & AI_PURSUE_NEXT)) // Only if actively pursuing
		{
			hesDeadJim = true;
		}
	}

	// --- Handle dead/invalid enemy ---
	if (hesDeadJim && !(self->hackflags & HACKFLAG_ATTACK_PLAYER))
	{
		self->monsterinfo.aiflags &= ~AI_MEDIC; // Clear medic flag if target invalid
		self->enemy = nullptr;                 // Clear enemy pointer
		self->goalentity = nullptr;            // Clear goal entity
		self->monsterinfo.close_sight_tripped = false;

		// Try to target old enemy if available and alive
		if (self->oldenemy && self->oldenemy->inuse && self->oldenemy->health > 0)
		{
			self->enemy = self->oldenemy;
			self->oldenemy = nullptr;
			HuntTarget(self); // Re-engage old enemy
		}
		// Try last player enemy (Rogue feature)
		else if (self->monsterinfo.last_player_enemy && self->monsterinfo.last_player_enemy->inuse && self->monsterinfo.last_player_enemy->health > 0)
		{
			self->enemy = self->monsterinfo.last_player_enemy;
			self->oldenemy = nullptr;
			self->monsterinfo.last_player_enemy = nullptr;
			HuntTarget(self); // Re-engage last player enemy
		}
		else // No other enemy available
		{
			// Before going idle, try to acquire a fresh visible enemy so the monster keeps hunting
			// instead of standing around after its target dies (mirrors the entry check above).
			if (FindTarget(self))
				return false; // Found a new target; don't attack this frame.

			if (self->movetarget && !(self->monsterinfo.aiflags & AI_STAND_GROUND)) // If has path target and not standing ground
			{
				self->goalentity = self->movetarget;
				self->monsterinfo.walk(self); // Resume walking path
			}
			else // Otherwise, stand ground or pause
			{
				self->monsterinfo.pausetime = HOLD_FOREVER; // Pause indefinitely
				self->monsterinfo.stand(self);           // Go to stand state

				// Clear temporary stand ground flags if set
				if (self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND)
					self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
			}
			// We handled the situation (enemy gone), return true to indicate action was taken (state change)
			return true;
		}
		// If we switched to an old/last enemy, fall through to process them below.
	}

	// --- *** ROBUST ENEMY CHECK *** ---
	// After the hesDeadJim block, double-check if the enemy pointer is *still* valid before proceeding.
	// This catches cases where the enemy might have been freed externally (like kicking a bot)
	// between the initial check and now.
	if (!self->enemy || !self->enemy->inuse)
	{
		// Enemy became invalid. Treat as if dead/lost.
		self->enemy = nullptr;
		self->goalentity = nullptr;
		self->monsterinfo.close_sight_tripped = false;

		// Go back to standing or walking state (similar logic to hesDeadJim block)
		if (self->movetarget && !(self->monsterinfo.aiflags & AI_STAND_GROUND))
		{
			self->goalentity = self->movetarget;
			if (self->monsterinfo.walk) // Check if walk function exists
				self->monsterinfo.walk(self);
			else if (self->monsterinfo.run) // Fallback to run? Or stand? Stand is safer.
				self->monsterinfo.stand(self);
		}
		else
		{
			self->monsterinfo.pausetime = HOLD_FOREVER;
			if (self->monsterinfo.stand) // Check if stand function exists
				self->monsterinfo.stand(self);
			if (self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND)
				self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
		}
		// Signaled a state change due to invalid enemy
		return true;
	}
	// --- *** END ROBUST CHECK *** ---

	// --- Enemy is currently valid, proceed with checks ---

	// Check knowledge of enemy
	enemy_vis = visible(self, self->enemy); // This call should now be safe
	if (enemy_vis)
	{
		// Store if we had clear line of sight (ignoring glass)
		self->monsterinfo.had_visibility = visible(self, self->enemy, false);
		// Make enemy hostile to wake up other monsters
		self->enemy->show_hostile = level.time + 1_sec;
		// Reset search timer, update last sighting
		self->monsterinfo.search_time = level.time + 5_sec;
		self->monsterinfo.last_sighting = self->monsterinfo.saved_goal = self->enemy->s.origin;

		// If we just regained sight
		if (self->monsterinfo.aiflags & AI_LOST_SIGHT)
		{
			self->monsterinfo.aiflags &= ~AI_LOST_SIGHT; // Clear lost sight flag

			// Reset temporary melee combat flag if enough time passed
			if (self->monsterinfo.move_block_change_time < level.time)
				self->monsterinfo.aiflags &= ~AI_TEMP_MELEE_COMBAT;

			// Reset checkattack timer quickly after regaining sight
			self->monsterinfo.checkattack_time = self->monsterinfo.isfriendlyspawn ? (level.time + 30_ms) : (level.time + random_time(50_ms, 200_ms));
		}
		// Update trail time and blind fire target
		self->monsterinfo.trail_time = level.time;
		self->monsterinfo.blind_fire_target = self->monsterinfo.last_sighting + (self->enemy->velocity * -0.1f);
		self->monsterinfo.blind_fire_delay = 0_ms;
	}

	// --- Calculate relative position/angle ---
	enemy_infront = infront(self, self->enemy); // This call should now be safe
	temp = self->enemy->s.origin - self->s.origin; // This access should now be safe
	enemy_yaw = vectoyaw(temp); // This call should now be safe

	// --- Call monster-specific attack checker ---
	// This is where the monster decides IF it wants to attack based on range, chance, state, etc.
	retval = false; // Default to not attacking this frame

	if (self->monsterinfo.checkattack_time <= level.time)
	{
		self->monsterinfo.checkattack_time = level.time + 0.07_sec; // Throttle checkattack calls

		// If our line of fire to the current enemy is obstructed by another body (a teammate or a
		// closer enemy), don't stand there attacking through it. Re-target the closest visible enemy
		// (often the blocker itself) so we engage who we can actually hit; otherwise keep this enemy
		// and let ai_run reposition us for a clear shot.
		if (enemy_vis && range_to(self, self->enemy) > RANGE_MELEE &&
			!(self->monsterinfo.aiflags & (AI_MEDIC | AI_MANUAL_STEERING)) &&
			M_AttackLaneBlocked(self, self->enemy))
		{
			edict_t* const blocked_enemy = self->enemy;
			const bool found_clear =
				self->monsterinfo.isfriendlyspawn ? FindMTarget(self) :
				g_horde->integer                  ? FindEnhancedTarget(self) :
				                                     FindTarget(self);

			if (found_clear && self->enemy && self->enemy != blocked_enemy)
			{
				HuntTarget(self); // engage the clearer/closer enemy
				return false;     // don't attack the blocked enemy this frame
			}

			if (!self->enemy)
				self->enemy = blocked_enemy; // finder cleared it; keep current and reposition
		}

		if (self->monsterinfo.checkattack) // Ensure the function pointer is valid
			retval = self->monsterinfo.checkattack(self);
	}

	// --- Handle attack execution based on checkattack result and current state ---
	if (retval || self->monsterinfo.attack_state >= AS_MISSILE)
	{
		// If checkattack returned true (wants to attack) OR already in an attack state (missile, melee, blind)...

		// Execute Missile Attack
		if (self->monsterinfo.attack_state == AS_MISSILE)
		{
			// Ensure enemy is still valid before running missile logic
			if (self->enemy && self->enemy->inuse) {
				ai_run_missile(self); // Turn and fire missile
				return true;          // Attack action performed
			}
			else { // Enemy became invalid just before firing
				return false; // Don't attack
			}
		}
		// Execute Melee Attack
		if (self->monsterinfo.attack_state == AS_MELEE)
		{
			// Ensure enemy is still valid before running melee logic
			if (self->enemy && self->enemy->inuse) {
				ai_run_melee(self); // Turn and perform melee
				return true;        // Attack action performed
			}
			else { // Enemy became invalid just before melee
				return false; // Don't attack
			}
		}
		// Execute Blind Fire Attack
		if (self->monsterinfo.attack_state == AS_BLIND)
		{
			// Ensure enemy is still valid (though we might be firing at last known pos)
			// ai_run_missile handles the actual firing logic, which might use blind_fire_target
			if (self->enemy && self->enemy->inuse) { // Still need a conceptual enemy for state
				ai_run_missile(self);
				return true; // Attack action performed
			}
			else { // Conceptual enemy gone
				self->monsterinfo.attack_state = AS_STRAIGHT; // Reset state if enemy truly gone
				return false; // Don't attack
			}
		}

		// If we reached here, checkattack returned true, but we weren't already in an attack state.
		// We generally only proceed to attack if the enemy is currently visible.
		if (!enemy_vis)
			return false; // Can't initiate attack if not visible right now

		// Fall through if retval was true but state wasn't AS_MISSILE/MELEE/BLIND
		// This implies checkattack set the state, but ai_run_missile/melee wasn't called yet.
		// The return true/false below handles whether an attack *will* happen.
	}

	// Return the result from the monster-specific checkattack function.
	// This indicates if an attack *was initiated* or decided upon during this check.
	return retval;
}

/*
=============
ai_run

The monster has an enemy it is trying to kill
=============
*/
void ai_run(edict_t* self, float dist)
{
	// =======================================================================
	// --- FIXED AND RESTRUCTURED STATE CHECKS ---
	// =======================================================================

	// Check if monster is paused for healing
	if (self->monsterinfo.healing_pause_time > level.time)
	{
		// Still being healed, just face enemy if we have one
		if (M_HasEnemy(self))
		{
			vec3_t v = self->enemy->s.origin - self->s.origin;
			self->ideal_yaw = vectoyaw(v);
			M_ChangeYaw(self);
		}
		return;
	}

	// Clear enemy if it's a trap in cooldown (similar to menu protection)
	if (self->enemy && horde::IsSpecialType(self->enemy, horde::SpecialEntityTypeID::FOOD_CUBE_TRAP))
	{
		trap_state_t* trap_state = GetTrapState(self->enemy);
		if (trap_state && trap_state->in_cooldown)
		{
			self->enemy = nullptr;  // Forget the trap during cooldown
		}
	}

	// 1. Handle terminal state: Intermission
	if (level.intermissiontime)
	{
		if (self->monsterinfo.walk) self->monsterinfo.walk(self);
		return; // Exit immediately.
	}

	// 2. Handle special case: Summoned monster targeting logic
	if (self->monsterinfo.isfriendlyspawn)
	{
		// Don't interfere with medics that are healing/resurrecting
		if (self->monsterinfo.aiflags & AI_MEDIC)
		{
			// Let the medic continue its healing/resurrection
			if (!M_HasEnemy(self))
			{
				// Target lost, clear medic mode
				self->monsterinfo.aiflags &= ~AI_MEDIC;
				if (!FindMTarget(self))
				{
					if (self->monsterinfo.stand) self->monsterinfo.stand(self);
					return;
				}
			}
			// Otherwise continue with current healing target - don't call FindMTarget
		}
		// Don't interfere with monsters following combat point commands
		else if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
		{
			// Monster is following orders, handled by AI_COMBAT_POINT code below
		}
		else if (!M_HasEnemy(self) || self->enemy->client)
		{
			self->enemy = nullptr;
			if (!FindMTarget(self))
			{
				if (self->monsterinfo.stand) self->monsterinfo.stand(self);
				return;
			}
		}
	}
	// 3. Handle general case: Regular monster targeting logic
	else
	{
		// Don't override medics that are healing/resurrecting
		if (self->monsterinfo.aiflags & AI_MEDIC)
		{
			// Medic is busy healing/resurrecting, don't interrupt
			if (!M_HasEnemy(self))
			{
				// Target lost, clear medic mode
				self->monsterinfo.aiflags &= ~AI_MEDIC;
				if (!FindTarget(self))
				{
					if (self->monsterinfo.stand) self->monsterinfo.stand(self);
					return;
				}
			}
			// Otherwise continue with current healing target
		}
		else if (!M_HasEnemy(self))
		{
			if (!FindTarget(self))
			{
				// Horde: don't drop back to standing just because we lost line
				// of sight. Re-acquire the nearest reachable player and keep
				// running; only stand if there's genuinely nobody in range.
				edict_t* nearest_player = g_horde->integer ? FindNearestValidPlayer(self) : nullptr;
				if (nearest_player)
				{
					self->enemy = nearest_player;
					FoundTarget(self);
				}
				else
				{
					if (self->monsterinfo.stand) self->monsterinfo.stand(self);
					return;
				}
			}
		}
	}

	// 4. Final safety net: At this point, we should have a valid enemy.
	// Exception: AI_COMBAT_POINT monsters can move without an enemy
	if (!M_HasEnemy(self) && !(self->monsterinfo.aiflags & AI_COMBAT_POINT))
	{
		if (self->monsterinfo.stand) self->monsterinfo.stand(self);
		return;
	}
	
	// =======================================================================
	// --- ORIGINAL AI LOGIC CONTINUES (NOW GUARANTEED TO HAVE A VALID ENEMY) ---
	// =======================================================================

	vec3_t   v;
	edict_t* tempgoal;
	edict_t* save;
	bool     newEnemy;
	edict_t* marker;
	float    d1, d2;
	trace_t  tr;
	vec3_t   v_forward, v_right;
	float    left, center, right;
	vec3_t   left_target, right_target;
	// ROGUE
	bool     retval;
	bool     alreadyMoved = false;
	bool     gotcha = false;
	edict_t* realEnemy;
	// ROGUE

	// if we're going to a combat point, just proceed
	if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
	{
		// If monster acquired a real enemy (not the combat point itself), break from orders to fight
		if (self->enemy && self->enemy != self->goalentity && self->enemy != self->movetarget)
		{
			// Check if enemy is a valid combat target (has health)
			if (self->enemy->health > 0 && !self->enemy->client)
			{
				// Temporarily clear AI_COMBAT_POINT to allow normal combat
				// Keep spot1/spot2/leader so we can resume patrol after combat
				self->monsterinfo.aiflags &= ~AI_COMBAT_POINT;
				self->goalentity = self->enemy;
				self->movetarget = nullptr;
				return;
			}
		}

		// Continue following combat point
		ai_checkattack(self, dist);
		M_MoveToGoal(self, dist);

		if (self->movetarget)
		{
			// nb: this is done from the centroid and not viewheight on purpose;
			trace_t tr = gi.trace((self->absmax + self->absmin) * 0.5f, { -2.f, -2.f, -2.f }, { 2.f, 2.f, 2.f }, self->movetarget->s.origin, self, CONTENTS_SOLID);

			// [Paril-KEX] special case: if we're stand ground & knocked way too far away
			// from our path_corner, or we can't see it any more, assume all
			// is lost.
			if ((self->monsterinfo.aiflags & AI_REACHED_HOLD_COMBAT) && (((self->movetarget->s.origin - closest_point_to_box(self->movetarget->s.origin, self->absmin, self->absmax)).lengthSquared() > 25600.f) // 160^2
				|| (tr.fraction < 1.0f && tr.plane.normal.z <= 0.7f))) // if we hit a climbable, ignore this result
			{
				self->monsterinfo.aiflags &= ~AI_COMBAT_POINT;
				self->movetarget = nullptr;
				self->target = nullptr;
				self->goalentity = self->enemy;
			}
			else
				return;
		}
		else
			return;
	}

	// PMM
	if ((self->monsterinfo.aiflags & AI_DUCKED) && self->monsterinfo.unduck)
		self->monsterinfo.unduck(self);

	//==========
	// PGM
	// if we're currently looking for a hint path
	if (self->monsterinfo.aiflags & AI_HINT_PATH)
	{
		// determine direction to our destination hintpath.
		M_MoveToGoal(self, dist);
		if (!self->inuse)
			return;

		// first off, make sure we're looking for the player, not a noise he made
		if (self->enemy)
		{
			if (self->enemy->inuse)
			{
				if (!IsPlayerNoise(self->enemy))
					realEnemy = self->enemy;
				else if (self->enemy->owner)
					realEnemy = self->enemy->owner;
				else // uh oh, can't figure out enemy, bail
				{
					self->enemy = nullptr;
					hintpath_stop(self);
					return;
				}
			}
			else
			{
				self->enemy = nullptr;
				hintpath_stop(self);
				return;
			}
		}
		else
		{
			hintpath_stop(self);
			return;
		}

		if (G_IsCooperative() || (G_IsDeathmatch() && g_horde->integer))
		{
			// if we're in coop, check my real enemy first .. if I SEE him, set gotcha to true
			if (self->enemy && visible(self, realEnemy))
				gotcha = true;
			else // otherwise, let FindTarget bump us out of hint paths, if appropriate
				FindTarget(self);
		}
		else
		{
			if (self->enemy && visible(self, realEnemy))
				gotcha = true;
		}

		// if we see the player, stop following hintpaths.
		if (gotcha)
			// disconnect from hintpaths and start looking normally for players.
			hintpath_stop(self);

		return;
	}
	// PGM
	//==========

	if (self->monsterinfo.aiflags & AI_SOUND_TARGET)
	{
		// PMM - paranoia checking
		if (self->enemy)
			v = self->s.origin - self->enemy->s.origin;

		bool touching_noise = SV_CloseEnough(self, self->enemy, dist * (gi.tick_rate / 10));

		if ((!self->enemy || !self->enemy->inuse) || (touching_noise && FacingIdeal(self)))
			// pmm
		{
			self->monsterinfo.aiflags |= (AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
			self->s.angles[YAW] = self->ideal_yaw;
			self->monsterinfo.stand(self);
			self->monsterinfo.close_sight_tripped = false;
			return;
		}

		// if we're close to the goal, just turn
		if (touching_noise)
			M_ChangeYaw(self);
		else
			M_MoveToGoal(self, dist);

		// ROGUE - prevent double moves for sound_targets
		alreadyMoved = true;

		if (!self->inuse)
			return; // PGM - g_touchtrigger free problem
		// ROGUE

		if (!FindTarget(self))
			return;
	}

	// Apply bonus speed multiplier for bonus-flagged monsters
	if (self->monsterinfo.bonus_flags != BF_NONE && !(self->monsterinfo.bonus_flags & BF_FRIENDLY) && !self->monsterinfo.IS_BOSS) {
		dist *= HORDE_BONUS_SPEED_MULTIPLIER;
	}

	// PMM -- moved ai_checkattack up here so the monsters can attack while strafing or charging

	// PMM -- if we're dodging, make sure to keep the attack_state AS_SLIDING
	retval = ai_checkattack(self, dist);

	// PMM - don't strafe if we can't see our enemy
	if ((!enemy_vis) && (self->monsterinfo.attack_state == AS_SLIDING))
		self->monsterinfo.attack_state = AS_STRAIGHT;
	// unless we're dodging (dodging out of view looks smart)
	if (self->monsterinfo.aiflags & AI_DODGING)
		self->monsterinfo.attack_state = AS_SLIDING;
	// pmm

	if (self->monsterinfo.attack_state == AS_SLIDING)
	{
		// PMM - protect against double moves
		if (!alreadyMoved)
			ai_run_slide(self, dist);
		// PMM
		// we're using attack_state as the return value out of ai_run_slide to indicate whether or not the
		// move succeeded.  If the move succeeded, and we're still sliding, we're done in here (since we've
		// had our chance to shoot in ai_checkattack, and have moved).
		// if the move failed, our state is as_straight, and it will be taken care of below
		if ((!retval) && (self->monsterinfo.attack_state == AS_SLIDING))
			return;
	}
	else if (self->monsterinfo.aiflags & AI_CHARGING)
	{
		self->ideal_yaw = enemy_yaw;
		if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
			M_ChangeYaw(self);
	}
	if (retval)
	{
		// PMM - is this useful?  Monsters attacking usually call the ai_charge routine..
		// the only monster this affects should be the soldier
		if ((dist || (self->monsterinfo.aiflags & AI_ALTERNATE_FLY)) && (!alreadyMoved) && (self->monsterinfo.attack_state == AS_STRAIGHT) &&
			(!(self->monsterinfo.aiflags & AI_STAND_GROUND)))
		{
			M_MoveToGoal(self, dist);
		}
		if (M_HasValidTarget(self) && enemy_vis)
		{
			if (self->monsterinfo.aiflags & AI_LOST_SIGHT)
			{
				self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;

				if (self->monsterinfo.move_block_change_time < level.time)
					self->monsterinfo.aiflags &= ~AI_TEMP_MELEE_COMBAT;
			}
			self->monsterinfo.last_sighting = self->monsterinfo.saved_goal = self->enemy->s.origin;
			self->monsterinfo.trail_time = level.time;
			// PMM
			self->monsterinfo.blind_fire_target = self->monsterinfo.last_sighting + (self->enemy->velocity * -0.1f);
			self->monsterinfo.blind_fire_delay = 0_ms;
			// pmm
		}
		return;
	}
	// PMM

	// PGM - added a little paranoia checking here... 9/22/98
	if (M_HasValidTarget(self) && enemy_vis)
	{
		// PMM - check for alreadyMoved
		if (!alreadyMoved)
			M_MoveToGoal(self, dist);
		if (!self->inuse)
			return; // PGM - g_touchtrigger free problem

		if (self->monsterinfo.aiflags & AI_LOST_SIGHT)
		{
			self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;

			if (self->monsterinfo.move_block_change_time < level.time)
				self->monsterinfo.aiflags &= ~AI_TEMP_MELEE_COMBAT;
		}
		self->monsterinfo.last_sighting = self->monsterinfo.saved_goal = self->enemy->s.origin;
		self->monsterinfo.trail_time = level.time;
		// PMM
		self->monsterinfo.blind_fire_target = self->monsterinfo.last_sighting + (self->enemy->velocity * -0.1f);
		self->monsterinfo.blind_fire_delay = 0_ms;
		// pmm

		// [Paril-KEX] if our enemy is literally right next to us, give
		// us more rotational speed so we don't get circled
		static constexpr float RANGE_MELEE_EXTENDED = RANGE_MELEE * 2.5f;
		if (range_to(self, self->enemy) <= RANGE_MELEE_EXTENDED)
			M_ChangeYaw(self);

		return;
	}

	//=======
	// PGM
	// if we've been looking (unsuccessfully) for the player for 10 seconds
	// PMM - reduced to 5, makes them much nastier
	if ((self->monsterinfo.trail_time + 5_sec) <= level.time)
	{
		// and we haven't checked for valid hint paths in the last 10 seconds
		if ((self->monsterinfo.last_hint_time + 10_sec) <= level.time)
		{
			// check for hint_paths.
			self->monsterinfo.last_hint_time = level.time;
			if (monsterlost_checkhint(self))
				return;
		}
	}
	// PGM
	//=======

	// PMM - moved down here to allow monsters to get on hint paths
	// coop will change to another enemy if visible
	if (G_IsCooperative() || (G_IsDeathmatch() && g_horde->integer))
		FindTarget(self);
	// pmm

	if ((self->monsterinfo.search_time) && (level.time > (self->monsterinfo.search_time + 20_sec)))
	{
		// PMM - double move protection
		if (!alreadyMoved)
			M_MoveToGoal(self, dist);
		self->monsterinfo.search_time = 0_ms;
		return;
	}

	save = self->goalentity;
	tempgoal = G_Spawn();
	self->goalentity = tempgoal;

	newEnemy = false;

	if (!(self->monsterinfo.aiflags & AI_LOST_SIGHT))
	{
		// just lost sight of the player, decide where to go first
		self->monsterinfo.aiflags |= (AI_LOST_SIGHT | AI_PURSUIT_LAST_SEEN);
		self->monsterinfo.aiflags &= ~(AI_PURSUE_NEXT | AI_PURSUE_TEMP);
		newEnemy = true;

		// immediately try paths
		self->monsterinfo.path_blocked_counter = 0_ms;
		self->monsterinfo.path_wait_time = 0_ms;
	}

	if (self->monsterinfo.aiflags & AI_PURSUE_NEXT)
	{
		self->monsterinfo.aiflags &= ~AI_PURSUE_NEXT;

		// give ourself more time since we got this far
		self->monsterinfo.search_time = level.time + 5_sec;

		if (self->monsterinfo.aiflags & AI_PURSUE_TEMP)
		{
			self->monsterinfo.aiflags &= ~AI_PURSUE_TEMP;
			marker = nullptr;
			self->monsterinfo.last_sighting = self->monsterinfo.saved_goal;
			newEnemy = true;
		}
		else if (self->monsterinfo.aiflags & AI_PURSUIT_LAST_SEEN)
		{
			self->monsterinfo.aiflags &= ~AI_PURSUIT_LAST_SEEN;
			marker = PlayerTrail_Pick(self, false);
		}
		else
		{
			marker = PlayerTrail_Pick(self, true);
		}

		if (marker)
		{
			self->monsterinfo.last_sighting = marker->s.origin;
			self->monsterinfo.trail_time = marker->timestamp;
			self->s.angles[YAW] = self->ideal_yaw = marker->s.angles[YAW];

			newEnemy = true;
		}
	}

	if (!(self->monsterinfo.aiflags & AI_PATHING) &&
		boxes_intersect(self->monsterinfo.last_sighting, self->monsterinfo.last_sighting, self->s.origin + self->mins, self->s.origin + self->maxs))
	{
		self->monsterinfo.aiflags |= AI_PURSUE_NEXT;
		dist = std::min(dist, std::sqrt((self->monsterinfo.last_sighting - self->s.origin).lengthSquared()));
		// [Paril-KEX] this helps them navigate corners when two next pursuits
		// are really close together
		self->monsterinfo.random_change_time = level.time + 10_hz;
	}

	self->goalentity->s.origin = self->monsterinfo.last_sighting;

	if (newEnemy)
	{
		tr =
			gi.trace(self->s.origin, self->mins, self->maxs, self->monsterinfo.last_sighting, self, MASK_PLAYERSOLID);
		if (tr.fraction < 1)
		{
			v = self->goalentity->s.origin - self->s.origin;
			d1 = std::sqrt(v.lengthSquared());
			center = tr.fraction;
			d2 = d1 * ((center + 1) / 2);
			float backup_yaw = self->s.angles.y;
			self->s.angles[YAW] = self->ideal_yaw = vectoyaw(v);
			AngleVectors(self->s.angles, v_forward, v_right, nullptr);

			v = { d2, -16, 0 };
			left_target = G_ProjectSource(self->s.origin, v, v_forward, v_right);
			tr = gi.trace(self->s.origin, self->mins, self->maxs, left_target, self, MASK_PLAYERSOLID);
			left = tr.fraction;

			v = { d2, 16, 0 };
			right_target = G_ProjectSource(self->s.origin, v, v_forward, v_right);
			tr = gi.trace(self->s.origin, self->mins, self->maxs, right_target, self, MASK_PLAYERSOLID);
			right = tr.fraction;

			center = (d1 * center) / d2;
			if (left >= center && left > right)
			{
				if (left < 1)
				{
					v = { d2 * left * 0.5f, -16, 0 };
					left_target = G_ProjectSource(self->s.origin, v, v_forward, v_right);
				}
				self->monsterinfo.saved_goal = self->monsterinfo.last_sighting;
				self->monsterinfo.aiflags |= AI_PURSUE_TEMP;
				self->goalentity->s.origin = left_target;
				self->monsterinfo.last_sighting = left_target;
				v = self->goalentity->s.origin - self->s.origin;
				self->ideal_yaw = vectoyaw(v);
			}
			else if (right >= center && right > left)
			{
				if (right < 1)
				{
					v = { d2 * right * 0.5f, 16, 0 };
					right_target = G_ProjectSource(self->s.origin, v, v_forward, v_right);
				}
				self->monsterinfo.saved_goal = self->monsterinfo.last_sighting;
				self->monsterinfo.aiflags |= AI_PURSUE_TEMP;
				self->goalentity->s.origin = right_target;
				self->monsterinfo.last_sighting = right_target;
				v = self->goalentity->s.origin - self->s.origin;
				self->ideal_yaw = vectoyaw(v);
			}
			self->s.angles[YAW] = backup_yaw;
		}
	}

	M_MoveToGoal(self, dist);

	G_FreeEdict(tempgoal);

	if (!self->inuse)
		return; // PGM - g_touchtrigger free problem

	if (self)
		self->goalentity = save;
}
