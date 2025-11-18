#include "../shared.h"
#include "../g_local.h"
#include "horde_performance.h"
#include "g_horde_phys.h"
#include "g_horde_benefits.h"

// *************************
// TESLA - 
// *************************

constexpr float TESLA_DAMAGE_RADIUS = 200;
constexpr int32_t TESLA_DAMAGE = 4;
constexpr int32_t TESLA_KNOCKBACK = 8;

constexpr gtime_t TESLA_ACTIVATE_TIME = 1.2_sec;

constexpr int32_t TESLA_EXPLOSION_DAMAGE_MULT = 50; // this is the amount the damage is multiplied by for underwater explosions
constexpr float TESLA_EXPLOSION_RADIUS = 200;

// Constants to adjust bounce behavior
constexpr float TESLA_BOUNCE_MULTIPLIER = 1.35f; // Base bounce multiplier
constexpr float TESLA_MIN_BOUNCE_SPEED = 120.0f; // Minimum speed after a bounce
constexpr float TESLA_BOUNCE_RANDOM = 70.0f;	 // Randomness factor in bounce
constexpr float TESLA_VERTICAL_BOOST = 180.0f;	 // Additional vertical impulse

// Offsets for Tesla positioning
constexpr float TESLA_WALL_OFFSET = 3.0f;		// Offset for walls
constexpr float TESLA_WALL_HEIGHT_ADJUST = 16.0f;  // Height adjustment for wall-mounted teslas
constexpr float TESLA_CEILING_OFFSET = -20.4f;	// Optimized offset for ceilings
constexpr float TESLA_CEILING_FINE_ADJUST = 4.0f;  // Fine adjustment for ceiling positioning
constexpr float TESLA_FLOOR_OFFSET = -12.0f;	// Offset for floor
constexpr float TESLA_ORB_OFFSET = 12.0f;		// Height of normal sphere
constexpr float TESLA_ORB_OFFSET_CEIL = -18.0f; // Height of sphere when on ceiling
constexpr float TESLA_POSITION_OFFSET = 16.0f;  // General positioning offset for tesla body

// Network message limiting
constexpr int MAX_TESLA_MESSAGES_PER_FRAME = 12; // Limit messages per frame to prevent overflow
static int tesla_messages_this_frame = 0;
static gtime_t tesla_message_frame_time = 0_sec;
// Chain Lightning constants
constexpr float CHAIN_LIGHTNING_RANGE = 200.0f;        // Range to find chain targets
constexpr float CHAIN_LIGHTNING_DAMAGE_MULT = 0.5f;    // 50% of tesla damage for chain targets
constexpr int MAX_CHAIN_TARGETS_PER_VICTIM = 3;        // Max chain targets per tesla victim (increased)
constexpr int CHAIN_LIGHTNING_MAX_EFFECTS_PER_FRAME = 30; // Much higher limit for chain effects per frame

void tesla_remove(edict_t *self)
{
	self->takedamage = false;

	if (self->owner && self->owner->client)
	{
		self->owner->client->resp.num_teslas--; // Decrementar el contador de teslas del jugador
	}

	self->owner = self->teammaster; // Going away, set the owner correctly.
	// PGM - grenade explode does damage to self->enemy
	self->enemy = nullptr;

	// play quad sound if quadded and an underwater explosion
	if ((self->dmg_radius) && (self->dmg > (TESLA_DAMAGE * TESLA_EXPLOSION_DAMAGE_MULT)))
		gi.sound(self, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);

	// Check flag to use quiet removal effect
	if (g_use_quiet_deployable_removal) {
		BecomeTE(self);
	} else {
		Grenade_Explode(self);
	}
}

DIE(tesla_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t &point, const mod_t &mod)->void
{
	RemoveEntityFromGlobalList(self);
	tesla_remove(self);
}

void tesla_blow(edict_t *self)
{
	self->dmg *= TESLA_EXPLOSION_DAMAGE_MULT;
	self->dmg_radius = TESLA_EXPLOSION_RADIUS;
	tesla_remove(self);
}

// Function to cache the ray origin calculation
// forward, right, up: pre-computed angle vectors to avoid redundant AngleVectors calls
static vec3_t calculate_tesla_ray_origin(const edict_t *self, const vec3_t &forward, const vec3_t &right, const vec3_t &up)
{
	vec3_t ray_origin = self->s.origin;

	// On wall
	if (fabs(self->s.angles[PITCH]) > 45 && fabs(self->s.angles[PITCH]) < 135)
	{
		// First, move outward from wall
		ray_origin = ray_origin + (forward * TESLA_WALL_OFFSET);

		// Adjust height consistently
		ray_origin = ray_origin + (up * TESLA_WALL_HEIGHT_ADJUST);
	}
	// On ceiling
	else if (fabs(self->s.angles[PITCH]) > 150 || fabs(self->s.angles[PITCH]) < -150)
	{
		ray_origin = ray_origin - (up * TESLA_ORB_OFFSET_CEIL);
		ray_origin = ray_origin - (up * TESLA_CEILING_FINE_ADJUST);
	}
	// On floor
	else
	{
		ray_origin = ray_origin + (up * TESLA_ORB_OFFSET);
	}

	return ray_origin;
}

//  calculation of ray target
vec3_t calculate_tesla_ray_target(const edict_t *self, const edict_t *target)
{
	// Calcular el centro del objetivo
	vec3_t target_center = target->s.origin;

	// Use midpoint of bounding box for better targeting
	for (int i = 0; i < 3; i++)
	{
		target_center[i] += (target->mins[i] + target->maxs[i]) * 0.5f;
	}

	return target_center;
}

// Fast trace function with caching
bool tesla_ray_trace(const edict_t *self, const edict_t *target, trace_t &tr, const vec3_t &forward, const vec3_t &right, const vec3_t &up)
{
	vec3_t const ray_start = calculate_tesla_ray_origin(self, forward, right, up);
	vec3_t const ray_end = calculate_tesla_ray_target(self, target);

	// Perform the trace
	tr = gi.traceline(ray_start, ray_end, self, MASK_PROJECTILE);

	// Check if ray reaches target
	return tr.fraction == 1.0f || tr.ent == target;
}

// Target priority struct
struct tesla_target
{
	edict_t *ent;
	float priority;
	float dist_squared;
	trace_t trace;  // Cache trace result to avoid duplicate ray traces
};

//  target validation
bool is_valid_tesla_target(edict_t *self, edict_t *ent)
{
	if (!ent || !ent->inuse || ent == self ||
		ent->health <= 0 || ent->deadflag ||
		ent->solid == SOLID_NOT)
		return false;

	// Tesla specific checks
	if (ent->monsterinfo.invincible_time > level.time)
		return false;

	// Skip sentries, emitters, nukes
	switch (static_cast<horde::SpecialEntityTypeID>(ent->special_type_id))
	{
	case horde::SpecialEntityTypeID::SENTRY_GUN:
	case horde::SpecialEntityTypeID::LASER_EMITTER:
	case horde::SpecialEntityTypeID::NUKE_MINE:
		return false;
	default:
		break; // Not one of the types, so continue
	}

	// Check for friendly summoned monsters (BF_FRIENDLY flag)
	if (ent->svflags & SVF_MONSTER)
	{
		if (ent->monsterinfo.bonus_flags & BF_FRIENDLY)
			return false;
	}

	// Owner team check for monsters
	if (self->owner && (self->owner->svflags & SVF_MONSTER))
	{
		if (ent == self->owner ||
			(ent->svflags & SVF_MONSTER && OnSameTeam(ent, self->owner)))
			return false;
	}

	return true;
}

// Target priority calculation
float calculate_tesla_priority(edict_t *self, edict_t *target, float dist_squared)
{
	float priority = 1.0f / (dist_squared + 1.0f);

	// Prioritize monsters slightly higher
	if (target->svflags & SVF_MONSTER)
		priority *= 1.2f;

	return priority;
}

// Chain target validation - excludes entities that are already being attacked by the tesla
bool is_valid_chain_target(edict_t *self, edict_t *ent, const tesla_target *tesla_targets, int num_tesla_targets)
{
	// Basic validation first
	if (!is_valid_tesla_target(self, ent))
		return false;

	// Exclude entities that are already being targeted by the tesla
	for (int i = 0; i < num_tesla_targets; i++)
	{
		if (tesla_targets[i].ent == ent)
			return false;
	}

	return true;
}

// Helper for sending tesla effect
bool try_send_tesla_effect(edict_t *self, edict_t *target, const vec3_t &ray_start, const vec3_t &ray_end)
{
	// Reset counter if we're in a new frame
	if (tesla_message_frame_time != level.time)
	{
		tesla_messages_this_frame = 0;
		tesla_message_frame_time = level.time;
	}

	// Check if we've hit the message limit for this frame
	if (tesla_messages_this_frame >= MAX_TESLA_MESSAGES_PER_FRAME)
	{
		return false;
	}

	// Rate limit effects per tesla
	if (level.time < self->monsterinfo.attack_finished)
	{
		return false;
	}

	// Send the effect
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_LIGHTNING);
	gi.WriteEntity(self);
	gi.WriteEntity(target);
	gi.WritePosition(ray_start);
	gi.WritePosition(ray_end);
	gi.multicast(ray_start, MULTICAST_PVS, false);

	// Update counters
	tesla_messages_this_frame++;

	// Increment effect count stored in medicTries (integer field)
	self->monsterinfo.medicTries++;

	// Dynamic rate limiting based on how many effects we've sent
	if (self->monsterinfo.medicTries <= 5)
	{
		self->monsterinfo.attack_finished = level.time + 0_hz; // No limit for first few effects
	}
	else if (self->monsterinfo.medicTries <= 10)
	{
		self->monsterinfo.attack_finished = level.time + 5_hz; // Start limiting after a few
	}
	else
	{
		self->monsterinfo.attack_finished = level.time + 10_hz; // More aggressive limiting
	}

	return true;
}

// Helper for sending chain lightning effects
bool try_send_chain_lightning_effect(edict_t *tesla_source, edict_t *chain_target, const vec3_t &chain_start, const vec3_t &chain_end)
{
	// DEBUGGING: Remove ALL rate limiting to isolate the visibility issue
	
	// Send the chain lightning effect
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_LIGHTNING);
	gi.WriteEntity(tesla_source);   // source entity (the tesla)
	gi.WriteEntity(chain_target);   // destination entity (the chain target)
	gi.WritePosition(chain_start);
	gi.WritePosition(chain_end);
	gi.multicast(chain_start, MULTICAST_PVS, false);

	return true;
}

// Main chain lightning function - spreads lightning from tesla victims to nearby enemies
// nearby_entities: cached entity list from tesla's grid query (avoids duplicate queries)
template<typename EntityContainer>
void tesla_chain_lightning(edict_t *self, const tesla_target *tesla_victims, int num_victims, const vec3_t &cached_ray_origin, const EntityContainer &nearby_entities)
{
	if (!self || num_victims <= 0)
		return;

	// Check if the tesla owner has the Tesla Chain Lightning upgrade
	if (!self->teammaster || !self->teammaster->client)
		return;

	// Only enable chain lightning if bot benefit OR player skill is enabled (like gl_bouncy)
	if (!ClassicPlayerHasBenefitTeslaChainLightning(self->teammaster) && !self->teammaster->client->pers.skills.tesla_chain)
		return;

	// Process each victim that was attacked by the tesla
	for (int victim_idx = 0; victim_idx < num_victims; victim_idx++)
	{
		edict_t *victim = tesla_victims[victim_idx].ent;
		if (!victim || !victim->inuse || victim->health <= 0)
			continue;

		// Reuse the nearby entities from the main tesla query instead of re-querying
		// Only filter for entities near THIS victim
		int chains_from_this_victim = 0;
		const float max_chain_range_squared = CHAIN_LIGHTNING_RANGE * CHAIN_LIGHTNING_RANGE;

		for (auto* potential_chain_target : nearby_entities)
		{
			if (chains_from_this_victim >= MAX_CHAIN_TARGETS_PER_VICTIM)
				break;

			// Must be a monster to be a chain target
			if (!(potential_chain_target->svflags & SVF_MONSTER))
				continue;

			// Validate the chain target (excludes original tesla targets)
			if (!is_valid_chain_target(self, potential_chain_target, tesla_victims, num_victims))
				continue;

			// Distance check from victim to potential chain target
			float dist_squared = DistanceSquared(victim->s.origin, potential_chain_target->s.origin);
			if (dist_squared > max_chain_range_squared)
				continue;

			// Visibility check from victim to chain target
			if (!visible(victim, potential_chain_target))
				continue;

			// Chain lightning should start from victim, not tesla
			vec3_t chain_start = calculate_tesla_ray_target(self, victim);
			vec3_t chain_end = calculate_tesla_ray_target(self, potential_chain_target);

			// Apply chain lightning damage (reduced damage)
			int chain_damage = static_cast<int>(self->dmg * CHAIN_LIGHTNING_DAMAGE_MULT);
			vec3_t chain_dir = chain_end - chain_start;
			chain_dir.normalize();

			T_Damage(potential_chain_target, victim, self->teammaster, chain_dir, chain_end, vec3_origin,
				chain_damage, TESLA_KNOCKBACK / 2, DAMAGE_NO_ARMOR, MOD_TESLA);

			// Send chain lightning visual effect from victim to chain target
			if (try_send_chain_lightning_effect(self, potential_chain_target, chain_start, chain_end))
			{
				chains_from_this_victim++;
			}
		}
	}
}

//  targeting and attack function using the Proximity Grid
THINK(tesla_think_active)(edict_t* self)->void
{
	if (!self)
		return;

	if (!self->teammaster || !self->teammaster->inuse)
	{
		tesla_remove(self);
		return;
	}

	// Check if owner is menu protected - pause tesla effects but keep it alive
	if (IsPlayerMenuProtected(self->teammaster)) {
		// Keep the tesla alive but skip damage/effects
		self->think = tesla_think_active;
		self->nextthink = HordePerf::GetTeslaThinkTimeWithJitter();
		return;
	}

	if (level.time > self->air_finished)
	{
		tesla_remove(self);
		return;
	}

	// Setup positioning and bounds
	vec3_t start = self->s.origin;
	const bool is_on_wall = fabs(self->s.angles[PITCH]) > 45 && fabs(self->s.angles[PITCH]) < 135;

	vec3_t forward, right, up;
	AngleVectors(self->s.angles, forward, right, up);

	if (is_on_wall)
	{
		start = start + (forward * TESLA_POSITION_OFFSET);
	}
	else
	{
		if (self->s.angles[PITCH] > 150 || self->s.angles[PITCH] < -150)
		{
			start = start + (up * -TESLA_POSITION_OFFSET);
		}
		else
		{
			start = start + (up * TESLA_POSITION_OFFSET);
		}
	}

	// Target acquisition with priority
	constexpr int max_targets = 3;
	constexpr float TESLA_SEARCH_RADIUS = TESLA_DAMAGE_RADIUS;
	const float max_range_squared = TESLA_SEARCH_RADIUS * TESLA_SEARCH_RADIUS;

	// Fixed-size array for potential targets (no dynamic allocation)
	constexpr int MAX_POTENTIAL_TARGETS = 10;
	tesla_target potential_targets[MAX_POTENTIAL_TARGETS];
	int num_targets = 0;

	// Cache the ray origin for this frame (pass pre-computed vectors to avoid redundant AngleVectors call)
	vec3_t ray_origin = calculate_tesla_ray_origin(self, forward, right, up);

	// --- THE OPTIMIZATION: Use the Grid instead of a linear scan ---
	// Get a pre-filtered list of nearby entities (monsters, players, projectiles) from the grid.
	const auto nearby_entities = HordePhys::g_monster_grid.QueryRadius(self->s.origin, TESLA_SEARCH_RADIUS);

	for (auto* ent : nearby_entities)
	{
		if (num_targets >= MAX_POTENTIAL_TARGETS)
			break;

		// The Tesla only targets monsters, so we filter out players and projectiles here.
		if (!(ent->svflags & SVF_MONSTER))
			continue;

		if (!is_valid_tesla_target(self, ent))
			continue;

		// The grid gives a square area, so we still need a precise distance check.
		float dist_squared = DistanceSquared(self->s.origin, ent->s.origin);
		if (dist_squared > max_range_squared)
			continue;

		// Quick visibility check before expensive ray trace
		if (!visible(self, ent))
			continue;

		trace_t tr;
		if (!tesla_ray_trace(self, ent, tr, forward, right, up))
			continue;

		// This is a valid target, add it to our list for sorting.
		// Store the trace result to avoid re-tracing during attack phase
		potential_targets[num_targets++] = {
			ent,
			calculate_tesla_priority(self, ent, dist_squared),
			dist_squared,
			tr };
	}
	// --- END OF OPTIMIZATION ---

	// Simple insertion sort (more efficient for small arrays)
	for (int i = 1; i < num_targets; i++)
	{
		tesla_target key = potential_targets[i];
		int j = i - 1;
		while (j >= 0 && potential_targets[j].priority < key.priority)
		{
			potential_targets[j + 1] = potential_targets[j];
			j--;
		}
		potential_targets[j + 1] = key;
	}

	// Attack phase - collect successfully attacked targets for chain lightning
	tesla_target attacked_victims[max_targets];
	int victims_count = 0;

	for (int i = 0; i < num_targets && victims_count < max_targets; i++)
	{
		const auto& target = potential_targets[i];

		// Use cached trace result from target acquisition (no need to trace again)
		const trace_t &tr = target.trace;

		vec3_t ray_end = tr.endpos;
		vec3_t dir = ray_end - ray_origin;
		dir.normalize();

		T_Damage(target.ent, self, self->teammaster, dir, tr.endpos, tr.plane.normal,
			self->dmg, TESLA_KNOCKBACK, DAMAGE_NO_ARMOR, MOD_TESLA);

		// Always add successfully damaged targets to chain lightning victims
		attacked_victims[victims_count] = target;
		victims_count++;

		// Try to send the visual effect independently (best effort)
		try_send_tesla_effect(self, target.ent, ray_origin, ray_end);
	}

	// Chain Lightning Phase - spread lightning from victims to nearby enemies
	// Pass the cached nearby_entities list to avoid duplicate grid queries
	if (victims_count > 0)
	{
		tesla_chain_lightning(self, attacked_victims, victims_count, ray_origin, nearby_entities);
	}

	// Stagger think times to distribute processing load
	if (self->inuse)
	{
		self->think = tesla_think_active;

		// --- PERFORMANCE FIX: Use jittered think time ---
		// This prevents all teslas from thinking on the exact same server frame,
		// which smooths out performance spikes when many are deployed.
		self->nextthink = HordePerf::GetTeslaThinkTimeWithJitter();
		// --- END FIX ---
	}
}
THINK(tesla_activate)(edict_t *self)->void
{
	edict_t *search;

	if (gi.pointcontents(self->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA | CONTENTS_WATER))
	{
		tesla_blow(self);
		return;
	}

	if (G_IsDeathmatch())
	{
		search = nullptr;
		while ((search = findradius(search, self->s.origin, 1.5f * TESLA_DAMAGE_RADIUS)) != nullptr)
		{
			if (search->classname && ((G_IsDeathmatch() && !g_horde->integer && ((!strncmp(search->classname, "info_player_", 12)) || (!strcmp(search->classname, "misc_teleporter_dest")) || (!strncmp(search->classname, "item_flag_", 10))))) &&
				(visible(search, self)))
			{
				BecomeExplosion1(self);
				return;
			}
		}
	}

	if (G_IsDeathmatch() && !g_horde->integer)
		self->owner = nullptr;

	self->think = tesla_think_active;
	self->nextthink = HordePerf::GetTeslaThinkTimeWithJitter();
	// Calculate tesla lifetime with adrenaline bonus
	gtime_t tesla_lifetime = CalculateDeployableLifetime(TeslaConstants::TIME_TO_LIVE, self->teammaster ? self->teammaster->client : nullptr);
	self->air_finished = level.time + tesla_lifetime;

	self->monsterinfo.attack_finished = level.time;
	self->monsterinfo.medicTries = 0;
}

THINK(tesla_think)(edict_t *ent)->void
{
	if (gi.pointcontents(ent->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA))
	{
		tesla_remove(ent);
		return;
	}

	if (!(ent->s.frame))
		gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/teslaopen.wav"), 1, ATTN_NORM, 0);

	ent->s.frame++;
	if (ent->s.frame > 14)
	{
		ent->s.frame = 14;
		ent->think = tesla_activate;
		ent->nextthink = level.time + 10_hz;
	}
	else
	{
		if (ent->s.frame > 9)
		{
			if (ent->s.frame == 10)
			{
				if (ent->owner && ent->owner->client)
				{
					PlayerNoise(ent->owner, ent->s.origin, PNOISE_WEAPON); // PGM
				}
				ent->s.skinnum = 1;
			}
			else if (ent->s.frame == 12)
				ent->s.skinnum = 2;
			else if (ent->s.frame == 14)
				ent->s.skinnum = 3;
		}
		ent->think = tesla_think;
		ent->nextthink = level.time + 15_hz;
	}
}

TOUCH(tesla_lava)(edict_t *ent, edict_t *other, const trace_t &tr, bool other_touching_self)->void
{
	if (!other->inuse)
		return;

	// Only stick to BSP entities (world geometry) or PUSH movers (doors, platforms)
	// Don't stick to monsters, players, or other entities
	bool can_stick = (other->solid == SOLID_BSP) ||
	                 (other->movetype == MOVETYPE_PUSH && !(other->svflags & SVF_MONSTER) && !other->client);

	if (!can_stick)
		return;

	// Bounce logic for non-world entities (bounce off them instead of sticking)
	if (other != world && (other->svflags & SVF_MONSTER || other->client ||
	    (other->movetype != MOVETYPE_PUSH && other->solid == SOLID_BBOX)))
	{
		if (tr.plane.normal)
		{
			vec3_t out{};
			float const backoff = ent->velocity.dot(tr.plane.normal) * TESLA_BOUNCE_MULTIPLIER;
			for (int i = 0; i < 3; i++)
			{
				float change = tr.plane.normal[i] * backoff;
				out[i] = ent->velocity[i] - change;
				out[i] += (frandom() * 2.0f - 1.0f) * TESLA_BOUNCE_RANDOM; // Equivalent to crandom()
				if (fabs(out[i]) < TESLA_MIN_BOUNCE_SPEED && out[i] != 0)
				{
					out[i] = (out[i] < 0 ? -TESLA_MIN_BOUNCE_SPEED : TESLA_MIN_BOUNCE_SPEED);
				}
			}
			if (tr.plane.normal[2] > 0)
			{
				out[2] += TESLA_VERTICAL_BOOST;
			}
			if (out.length() < TESLA_MIN_BOUNCE_SPEED)
			{
				out.normalize();
				out = out * TESLA_MIN_BOUNCE_SPEED;
			}
			ent->velocity = out;
			ent->avelocity = {
				(frandom() * 2.0f - 1.0f) * 240,
				(frandom() * 2.0f - 1.0f) * 240,
				(frandom() * 2.0f - 1.0f) * 240};
			if (ent->velocity.length() > 0 && strcmp(other->classname, "func_train"))
			{
				gi.sound(ent, CHAN_VOICE, gi.soundindex(frandom() > 0.5f ? "weapons/hgrenb1a.wav" : "weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
			}
		}
		return;
	}

	if (tr.plane.normal)
	{
		const float slope = fabs(tr.plane.normal[2]);
		if (slope > 0.85f)
		{
			if (tr.plane.normal[2] > 0)
			{
				// Floor
				ent->s.angles = {};
				ent->mins = {-4, -4, 0};
				ent->maxs = {4, 4, 8};
				ent->s.origin = ent->s.origin + (tr.plane.normal * TESLA_FLOOR_OFFSET);
				ent->s.origin[2] += TESLA_ORB_OFFSET;
			}
			else
			{
				// Ceiling
				ent->s.angles = {180, 0, 0};
				ent->mins = {-4, -4, -8};
				ent->maxs = {4, 4, 0};
				ent->s.origin = ent->s.origin + (tr.plane.normal * TESLA_CEILING_OFFSET);
				ent->s.origin[2] += TESLA_ORB_OFFSET_CEIL;
			}
		}
		else
		{
			vec3_t dir = vectoangles(tr.plane.normal);
			vec3_t forward;
			AngleVectors(dir, &forward, nullptr, nullptr);

			// Detect if it's a flat wall based on the X/Y components of the normal
			bool is_flat_wall = (fabs(tr.plane.normal[0]) > 0.95f || fabs(tr.plane.normal[1]) > 0.95f);

			if (is_flat_wall)
			{
				// Use original behavior for flat walls
				ent->s.angles[PITCH] = dir[PITCH] + 90;
				ent->s.angles[YAW] = dir[YAW];
				ent->s.angles[ROLL] = 0;
				ent->mins = {0, -4, -4};
				ent->maxs = {8, 4, 4};
				ent->s.origin = ent->s.origin + (forward * -TESLA_WALL_OFFSET);
			}
			else
			{
				// Use new behavior for curved/sloped surfaces
				ent->s.angles = dir;
				ent->s.angles[PITCH] += 90;
				ent->mins = {-4, -4, -4};
				ent->maxs = {4, 4, 4};
				ent->s.origin = ent->s.origin + (forward * -TESLA_WALL_OFFSET);
			}
		}
	}

	if (gi.pointcontents(ent->s.origin) & (CONTENTS_LAVA | CONTENTS_SLIME))
	{
		tesla_blow(ent);
		return;
	}

	ent->velocity = {};
	ent->avelocity = {};
	ent->takedamage = true;
	ent->movetype = MOVETYPE_NONE;
	ent->die = tesla_die;
	ent->touch = nullptr;
	ent->solid = SOLID_BBOX;
	ent->think = tesla_think;
	ent->nextthink = level.time;
	gi.linkentity(ent);
}

void fire_tesla(edict_t *self, const vec3_t &start, const vec3_t &aimdir, int tesla_damage_multiplier, int speed)
{
	// Check if player is menu protected
	if (IsPlayerMenuProtected(self)) {
		gi.LocClient_Print(self, PRINT_HIGH, "You cannot use this while in a menu.\n");
		return;
	}

	// O(1) PERFORMANCE: If player is at their tesla limit, remove the oldest one.
	if (self && self->client && self->client->resp.num_teslas >= TeslaConstants::MAX_TESLAS_PER_PLAYER())
	{
		// Get the oldest tesla from our circular buffer.
		edict_t *oldest = self->client->resp.deployed_teslas[self->client->resp.oldest_tesla_idx];

		// Ensure it's a valid, in-use tesla before removing it.
		if (oldest && oldest->inuse && horde::IsSpecialType(oldest, horde::SpecialEntityTypeID::TESLA_MINE))
		{
            // --- ROBUST METHOD: Directly call the die function ---
            // This explicitly triggers the tesla's full cleanup sequence, which calls
            // tesla_remove() to handle the explosion and player count.
            // Use quiet removal effect for oldest tesla auto-replacement
			g_use_quiet_deployable_removal = true;
			tesla_die(oldest, self, self, 0, oldest->s.origin, MOD_UNKNOWN);
			g_use_quiet_deployable_removal = false;
		}
	}

	edict_t *tesla;
	vec3_t dir;
	vec3_t forward, right, up;

	dir = vectoangles(aimdir);
	AngleVectors(dir, forward, right, up);

	tesla = G_Spawn();
	tesla->s.origin = start;
	tesla->velocity = aimdir * speed;

	const float gravityAdjustment = level.gravity / 800.f;

	tesla->velocity += up * (200 + (frandom() * 2.0f - 1.0f) * 10.0f) * gravityAdjustment;
	tesla->velocity += right * ((frandom() * 2.0f - 1.0f) * 10.0f);
	tesla->avelocity = {
		(frandom() * 2.0f - 1.0f) * 90,
		(frandom() * 2.0f - 1.0f) * 90,
		(frandom() * 2.0f - 1.0f) * 120};

	tesla->s.angles = {};
	tesla->movetype = MOVETYPE_BOUNCE;
	tesla->solid = SOLID_BBOX;
	tesla->s.effects |= EF_GRENADE;
	tesla->s.renderfx |= RF_IR_VISIBLE;
	tesla->mins = {-4, -4, 0};
	tesla->maxs = {4, 4, 8};
	tesla->s.modelindex = gi.modelindex("models/weapons/g_tesla/tris.md2");

	tesla->owner = self;
	tesla->teammaster = self;

	// Store attacker info in case owner dies before projectile hits
	if (self) {
		if (self->client) {
			tesla->projectile_was_player_attacker = true;
			tesla->projectile_attacker_type_id = 0;
		} else if (self->svflags & SVF_MONSTER) {
			tesla->projectile_was_player_attacker = false;
			tesla->projectile_attacker_type_id = self->monsterinfo.monster_type_id;
		}
	}

// Team assignment (Refactored)
    if (self->client) {
        tesla->ctf_team = self->client->resp.ctf_team;
    } else {
        tesla->ctf_team = CTF_NOTEAM;
    }

	// Calculate tesla lifetime with adrenaline bonus
	gtime_t tesla_lifetime = CalculateDeployableLifetime(TeslaConstants::TIME_TO_LIVE, self ? self->client : nullptr);
	tesla->wait = (level.time + tesla_lifetime).seconds();
	tesla->think = tesla_think;
	tesla->nextthink = level.time + TESLA_ACTIVATE_TIME;
	tesla->timestamp = level.time;

	tesla->touch = tesla_lava;

	if (G_IsDeathmatch())
		tesla->health = 50;
	else
		tesla->health = 50;

	tesla->takedamage = true;
	tesla->die = tesla_die;
	tesla->dmg = TESLA_DAMAGE * tesla_damage_multiplier;
	tesla->classname = "tesla_mine";
	tesla->special_type_id = static_cast<uint8_t>(horde::SpecialTypeRegistry::GetTypeID(tesla->classname));
	tesla->flags |= (FL_DAMAGEABLE | FL_TRAP);
	tesla->clipmask = (MASK_PROJECTILE | CONTENTS_SLIME | CONTENTS_LAVA) & ~CONTENTS_DEADMONSTER;
	if (self && self->client && !G_ShouldPlayersCollide(true))
		tesla->clipmask &= ~CONTENTS_PLAYER;

	tesla->flags |= FL_MECHANICAL;

    g_targetable_special_entities.push_back(tesla);
	// Initialize effect tracking fields
	tesla->monsterinfo.attack_finished = level.time;
	tesla->monsterinfo.medicTries = 0;

	gi.linkentity(tesla);

	if (self->client)
	{
		// Track the newly deployed tesla.
		self->client->resp.deployed_teslas[self->client->resp.oldest_tesla_idx] = tesla;
		
        // Advance the index for the next "oldest".
		self->client->resp.oldest_tesla_idx = (self->client->resp.oldest_tesla_idx + 1) % TeslaConstants::MAX_TESLAS_PER_PLAYER();

		// Increment the counter.
		self->client->resp.num_teslas++;
	}
}
