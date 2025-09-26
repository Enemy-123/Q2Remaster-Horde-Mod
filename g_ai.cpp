// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_ai.c

#include "g_local.h"
#include <cfloat>
#include "horde/horde_performance.h"
bool FindTarget(edict_t* self);
bool ai_checkattack(edict_t* self, float dist);

bool    enemy_vis;
bool    enemy_infront;
float   enemy_yaw;

// ROGUE
constexpr float MAX_SIDESTEP = 8.0f;
// ROGUE

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

	// Usar stack array en lugar de malloc para pequeñas cantidades
	constexpr size_t MAX_STACK_CLIENTS = 32;
	edict_t* stack_players[MAX_STACK_CLIENTS] = {};
	edict_t** visible_players = stack_players;
	size_t num_visible = 0;

	// Solo alocar dinámicamente si es necesario
	if (game.maxclients > MAX_STACK_CLIENTS) {
		visible_players = (edict_t**)malloc(sizeof(edict_t*) * game.maxclients);
		if (!visible_players)
			return nullptr;
	}

	// Cache de valores usados en el loop
	//const vec3_t& self_origin = self->s.origin;
	const vec3_t& self_absmin = self->absmin;
	const vec3_t& self_absmax = self->absmax;

	for (auto player : active_players())
	{
		// Early out conditions agrupados
		if (player->health <= 0 ||
			player->deadflag ||
			!player->solid ||
			player->flags & (FL_NOTARGET | FL_DISGUISED))
			continue;

		// Cache intersección de cajas
		bool touching = boxes_intersect(self_absmin, self_absmax,
			player->absmin, player->absmax);

			if ( (!touching && 
				(!(self->monsterinfo.aiflags & AI_THIRD_EYE) &&
				 !infront(self, player))
			   ) ||
			   !visible(self, player) )
		  {
			  continue;
		  }

		visible_players[num_visible++] = player;
	}

	// Seleccionar jugador aleatorio de los visibles
	edict_t* chosen_player = nullptr;
	if (num_visible > 0)
		chosen_player = visible_players[irandom(num_visible)];

	// Liberar memoria si fue alocada dinámicamente
	if (visible_players != stack_players)
		free(visible_players);

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
void ai_stand(edict_t* self, float dist)
{
	vec3_t v;
	// ROGUE
	bool retval;
	// ROGUE

	if (dist || (self->monsterinfo.aiflags & AI_ALTERNATE_FLY))
		M_walkmove(self, self->s.angles[YAW], dist);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		// [Paril-KEX] check if we've been pushed out of our point_combat
		if (!(self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND) &&
			self->movetarget && self->movetarget->classname && !strcmp(self->movetarget->classname, "point_combat"))
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

		if (self->enemy && !(self->enemy->classname && !strcmp(self->enemy->classname, "player_noise")))
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

	if (level.time > self->monsterinfo.pausetime)
	{
		self->monsterinfo.walk(self);
		return;
	}

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
	// HORDESTAND: Verifica si estamos en modo horda y el monstruo no tiene un enemigo
	if (g_horde->integer)
	{
		// Solo la sentrygun utilizará FindMTarget para buscar un objetivo
		if (self->monsterinfo.issummoned) {
			if (!self->enemy ||
				(self->enemy->client && !self->enemy->monsterinfo.issummoned)) { // Si el enemigo actual es un player, olvidarlo
				self->enemy = nullptr;
			}
			FindMTarget(self);
		}
		else
		{
			edict_t* nearest_player = nullptr;
			float nearest_distance_sq = FLT_MAX;

			// Encuentra el jugador más cercano que esté vivo y no sea espectador
			for (auto client : active_players_no_spect())
			{
				if (client->client) {
					if (client->client->invisible_time > level.time &&
						client->client->invisibility_fade_time <= level.time) {
						continue; // Saltar jugadores completamente invisibles
					}
					if (EntIsSpectating(client)) {
						continue; // Saltar espectadores
					}
				}
				if (client->inuse && client->health > 0)
				{
					const float dist_squared = DistanceSquared(self->s.origin, client->s.origin);
					if (dist_squared < nearest_distance_sq)
					{
						nearest_player = client;
						nearest_distance_sq = dist_squared;
					}
				}
			}
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
}

/*
=============
ai_charge

Turns towards target and advances
Use this call with a distance of 0 to replace ai_face
==============
*/

void ai_charge(edict_t* self, float dist)
{

	if (!self || !self->inuse || !self->enemy || !self->enemy->inuse)
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
    if (!ent || !ent->inuse || ent == self || ent->client || ent->monsterinfo.issummoned)
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
    float priority = 1000000.0f / (dist_squared + 1.0f); // Closer is much better

    // Big bonus for the entity that last damaged us
    if (target == self->monsterinfo.damage_attacker) {
        priority *= 2.0f;
    }

    // Bonus for targets with more health (bigger threats)
    priority += target->health;

    return priority;
}

#include "horde/g_horde_phys.h" 

bool FindMTarget(edict_t* self) {
	if (!self || !self->inuse || !self->monsterinfo.issummoned || level.intermissiontime) {
		return false;
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

	// --- THE OPTIMIZATION ---
	// Get potential targets from the grid instead of iterating all active monsters.
	const auto potential_targets = HordePhys::g_monster_grid.QueryRadius(self_origin, MAX_RANGE);

	// --- Iterate Through POTENTIAL Monsters to Find a Better Target ---
	for (auto ent : potential_targets) { // <<<< KEY CHANGE HERE
		// --- Fast Rejection Checks ---
		if (!IsValidMonsterTargetForSummon(self, ent, current_time)) {
			continue;
		}

		// --- Distance Check ---
		// The grid gives us a square search area, so we still need a precise distance check.
		float dist_squared = DistanceSquared(self_origin, ent->s.origin);
		if (dist_squared > MAX_RANGE_SQUARED) {
			continue;
		}

		// --- Visibility Check (most expensive) ---
		if (!visible(self, ent, false)) {
			continue;
		}

		// --- Priority Calculation ---
		float current_priority = CalculateTargetPriority(self, ent, dist_squared);

		// --- Update Best Target ---
		if (current_priority > highest_priority) {
			highest_priority = current_priority;
			best_target = ent;
		}
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

	// Verificar si somos una unidad invocada y el enemigo es un player
	if ((self->monsterinfo.issummoned && !self->enemy) || (self->monsterinfo.issummoned && self->enemy && self->enemy->client)) {
		self->enemy = nullptr;
		if (FindMTarget(self))
			return;
		self->monsterinfo.stand(self);
		return;
	}

	// --- Player Noise Check ---
	// If the found enemy is a temporary sound entity, handle it differently.
	// Do not call HuntTarget for it, as it might be freed immediately.
	// Let ai_run handle moving towards the sound goal based on the AI_SOUND_TARGET flag.
	if (self->enemy->classname && strcmp(self->enemy->classname, "player_noise") == 0)
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

		// prefer the closest one we heard - use distance cache for performance
		float dist_sq = HordePerf::g_distance_cache.GetDistanceSquared(self->s.origin, sound->s.origin);

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
	// Primero, verificamos si client o client->client son nulos
	if (!client || !client->client)
	{
		return false;
	}

	// Si somos una unidad invocada y el objetivo es un player, no es visible
	if (self->monsterinfo.issummoned && client->client)
		return false;

	// Verificamos si el jugador es espectador o no tiene equipo en modo CTF
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

    if (self->monsterinfo.issummoned) {
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

	// Primero verificamos si es una unidad invocada que tiene un player como enemigo
	if ((self->monsterinfo.issummoned && !self->enemy) || (self->monsterinfo.issummoned && self->enemy && self->enemy->client)) {
		self->enemy = nullptr;
		return FindMTarget(self);
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
			else if (!(self->enemy) && !(self->spawnflags & SPAWNFLAG_MONSTER_AMBUSH) &&
				(client = AI_GetSoundClient(self, false)))
			{
				heardit = true;
			}
		}
	}

	// Apply cooldown logic only for horde mode
	if (g_horde->integer && heardit && !self->monsterinfo.issummoned)
	{
		if (self->monsterinfo.lastnoisecooldown > level.time)
		{
			return false;
		}
		self->monsterinfo.lastnoisecooldown = level.time + 3.5_sec; //hordehear cooldown
	}

	if (!client)
	{
		if (g_horde->integer)
		{
			// Usar la misma lógica mejorada para todos, incluyendo sentrygun
			return FindEnhancedTarget(self);
		}
		return false; // No se encontraron objetivos
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

		if (strcmp(self->enemy->classname, "player_noise") != 0)
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

		if (self->enemy->client && self->enemy->client->invisible_time > level.time && self->enemy->client->invisibility_fade_time <= level.time)
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

		if (HordePerf::g_distance_cache.GetDistanceSquared(client->s.origin, self->s.origin) > 1000000) // too far to hear (1000^2)
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
    
    if (self->monsterinfo.aiflags & AI_PATHING)
        return delta <= 5 || delta >= 355;
        
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
		(self->monsterinfo.issummoned && self->enemy->monsterinfo.issummoned))
	{
		return false;
	}
	if (self->enemy->flags & FL_NOVISIBLE)
		return false;

	if (self->enemy && self->enemy->health > 0)
	{
		if (self->enemy->client)
		{
			if (self->enemy->client->invisible_time > level.time)
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
		static const float max_sidestep_per_frame = MAX_SIDESTEP * 10.0f / (float)gi.frame_time_ms;
		distance = min(distance, max_sidestep_per_frame);
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

	if (!self->enemy && self->monsterinfo.issummoned)
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
	else if (self->monsterinfo.aiflags & AI_MEDIC) // Medic checking target validity
	{
		// Medic should stop if target is no longer valid
		if (!self->enemy->inuse)
			hesDeadJim = true;
		// For resurrection: stop if corpse was resurrected (has monster_think)
		else if (self->enemy->health <= 0)
			hesDeadJim = true;
		// For healing: stop if living target is fully healed or dead
		else if (self->enemy->health > 0 && (self->enemy->health >= self->enemy->max_health || self->enemy->deadflag))
			hesDeadJim = true;
	}
	else // Standard checks
	{
		if (!(self->monsterinfo.aiflags & AI_BRUTAL)) // Non-brutal monsters stop attacking dead things
		{
			if (self->enemy->health <= 0)
				hesDeadJim = true;
		}

		// Check for fully invisible players when pursuing
		// Ensure self->enemy->client exists before accessing it
		if (!hesDeadJim && self->enemy->client &&
			self->enemy->client->invisible_time > level.time &&
			self->enemy->client->invisibility_fade_time <= level.time &&
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
			self->monsterinfo.checkattack_time = self->monsterinfo.issummoned ? (level.time + 30_ms) : (level.time + random_time(50_ms, 200_ms));
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

	// 1. Handle terminal state: Intermission
	if (level.intermissiontime)
	{
		if (self->monsterinfo.walk) self->monsterinfo.walk(self);
		return; // Exit immediately.
	}

	// 2. Handle special case: Summoned monster targeting logic
	if (self->monsterinfo.issummoned)
	{
		if (!self->enemy || !self->enemy->inuse || self->enemy->client)
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
			if (!self->enemy || !self->enemy->inuse)
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
		else if (!self->enemy || !self->enemy->inuse)
		{
			if (!FindTarget(self))
			{
				if (self->monsterinfo.stand) self->monsterinfo.stand(self);
				return;
			}
		}
	}

	// 4. Final safety net: At this point, we should have a valid enemy.
	if (!self->enemy || !self->enemy->inuse)
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
		ai_checkattack(self, dist);
		M_MoveToGoal(self, dist);

		if (self->movetarget)
		{
			// nb: this is done from the centroid and not viewheight on purpose;
			trace_t tr = gi.trace((self->absmax + self->absmin) * 0.5f, { -2.f, -2.f, -2.f }, { 2.f, 2.f, 2.f }, self->movetarget->s.origin, self, CONTENTS_SOLID);

			// [Paril-KEX] special case: if we're stand ground & knocked way too far away
			// from our path_corner, or we can't see it any more, assume all
			// is lost.
			if ((self->monsterinfo.aiflags & AI_REACHED_HOLD_COMBAT) && ((HordePerf::g_distance_cache.GetDistanceSquared(closest_point_to_box(self->movetarget->s.origin, self->absmin, self->absmax), self->movetarget->s.origin) > 25600.f) // 160^2
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
				if (strcmp(self->enemy->classname, "player_noise") != 0)
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

	// Apply bonus speed multiplier ( HORDEBONUS FLAGGED MONSTER )
	if (self->monsterinfo.bonus_flags != BF_NONE && !(self->monsterinfo.bonus_flags & BF_FRIENDLY) && !self->monsterinfo.IS_BOSS) {
		dist *= 1.6f; // Example: 60% faster
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
		if ((self->enemy) && (self->enemy->inuse) && (enemy_vis))
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
	if ((self->enemy) && (self->enemy->inuse) && (enemy_vis))
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
		dist = min(dist, sqrtf(HordePerf::g_distance_cache.GetDistanceSquared(self->s.origin, self->monsterinfo.last_sighting)));
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
			d1 = sqrtf(HordePerf::g_distance_cache.GetDistanceSquared(self->goalentity->s.origin, self->s.origin));
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