#include "../shared.h"
#include "../g_local.h"

// *************************
// TESLA - 
// *************************

constexpr float TESLA_DAMAGE_RADIUS = 200;
constexpr int32_t TESLA_DAMAGE = 4;
constexpr int32_t TESLA_KNOCKBACK = 8;

constexpr gtime_t TESLA_ACTIVATE_TIME = 1.2_sec;

constexpr int32_t TESLA_EXPLOSION_DAMAGE_MULT = 50; // this is the amount the damage is multiplied by for underwater explosions
constexpr float TESLA_EXPLOSION_RADIUS = 200;

// Constantes para ajustar el comportamiento del rebote
constexpr float TESLA_BOUNCE_MULTIPLIER = 1.35f; // Multiplicador base del rebote
constexpr float TESLA_MIN_BOUNCE_SPEED = 120.0f; // Velocidad mínima después de un rebote
constexpr float TESLA_BOUNCE_RANDOM = 70.0f;	 // Factor de aleatoriedad en el rebote
constexpr float TESLA_VERTICAL_BOOST = 180.0f;	 // Impulso vertical adicional

// Offsets for Tesla positioning
constexpr float TESLA_WALL_OFFSET = 3.0f;		// Offset para paredes
constexpr float TESLA_CEILING_OFFSET = -20.4f;	// Offset optimizado para techos
constexpr float TESLA_FLOOR_OFFSET = -12.0f;	// Offset para suelo
constexpr float TESLA_ORB_OFFSET = 12.0f;		// Altura de la esfera normal
constexpr float TESLA_ORB_OFFSET_CEIL = -18.0f; // Altura de la esfera cuando está en techo

// Network message limiting
constexpr int MAX_TESLA_MESSAGES_PER_FRAME = 12; // Limit messages per frame to prevent overflow
static int tesla_messages_this_frame = 0;
static gtime_t tesla_message_frame_time = 0_sec;

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

	// Remove from effect tracking - THIS LINE IS REMOVED
	// tesla_effect_states.erase(self);

	Grenade_Explode(self);
}

DIE(tesla_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t &point, const mod_t &mod)->void
{
	auto& vec = g_targetable_special_entities;
    vec.erase(std::remove(vec.begin(), vec.end(), self), vec.end());
	// OnEntityDeath(self);
	tesla_remove(self);
}

void tesla_blow(edict_t *self)
{
	self->dmg *= TESLA_EXPLOSION_DAMAGE_MULT;
	self->dmg_radius = TESLA_EXPLOSION_RADIUS;
	tesla_remove(self);
}

TOUCH(tesla_zap)(edict_t *self, edict_t *other, const trace_t &tr, bool other_touching_self)->void
{
	// Ignorar el contacto si el otro es un jugador
	if (other->client)
	{
		return;
	}

	// Código existente para manejar el contacto aquí
}

// Function to cache the ray origin calculation
static vec3_t calculate_tesla_ray_origin(const edict_t *self)
{
	vec3_t ray_origin = self->s.origin;
	vec3_t forward, right, up;
	AngleVectors(self->s.angles, forward, right, up);

	// En pared
	if (fabs(self->s.angles[PITCH]) > 45 && fabs(self->s.angles[PITCH]) < 135)
	{
		// Primero, mover hacia afuera de la pared
		ray_origin = ray_origin + (forward * TESLA_WALL_OFFSET);

		// Ajustar la altura de manera consistente
		ray_origin = ray_origin + (up * 16.0f);
	}
	// En techo
	else if (fabs(self->s.angles[PITCH]) > 150 || fabs(self->s.angles[PITCH]) < -150)
	{
		ray_origin = ray_origin - (up * TESLA_ORB_OFFSET_CEIL);
		ray_origin = ray_origin - (up * 4.0f);
	}
	// En suelo
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
bool tesla_ray_trace(const edict_t *self, const edict_t *target, trace_t &tr)
{
	vec3_t const ray_start = calculate_tesla_ray_origin(self);
	vec3_t const ray_end = calculate_tesla_ray_target(self, target);

	// Perform the trace
	tr = gi.traceline(ray_start, ray_end, self, MASK_PROJECTILE);

	// Check if ray reaches target
	return tr.fraction == 1.0f || tr.ent == target;
}

// Target priority struct
struct TeslaTarget
{
	edict_t *ent;
	float priority;
	float dist_squared;
};

//  target validation
bool IsValidTeslaTarget(edict_t *self, edict_t *ent)
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
float CalculateTeslaPriority(edict_t *self, edict_t *target, float dist_squared)
{
	float priority = 1.0f / (dist_squared + 1.0f);

	// Prioritize monsters slightly higher
	if (target->svflags & SVF_MONSTER)
		priority *= 1.2f;

	return priority;
}

// Helper for sending tesla effect
bool TrySendTeslaEffect(edict_t *self, edict_t *target, const vec3_t &ray_start, const vec3_t &ray_end)
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

#include "../horde/g_horde_phys.h"
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
		start = start + (forward * 16);
	}
	else
	{
		if (self->s.angles[PITCH] > 150 || self->s.angles[PITCH] < -150)
		{
			start = start + (up * -16);
		}
		else
		{
			start = start + (up * 16);
		}
	}

	// Target acquisition with priority
	constexpr int max_targets = 3;
	constexpr float TESLA_SEARCH_RADIUS = TESLA_DAMAGE_RADIUS;
	const float max_range_squared = TESLA_SEARCH_RADIUS * TESLA_SEARCH_RADIUS;

	// Fixed-size array for potential targets (no dynamic allocation)
	constexpr int MAX_POTENTIAL_TARGETS = 10;
	TeslaTarget potential_targets[MAX_POTENTIAL_TARGETS];
	int num_targets = 0;

	// Cache the ray origin for this frame
	vec3_t ray_origin = calculate_tesla_ray_origin(self);

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

		if (!IsValidTeslaTarget(self, ent))
			continue;

		// The grid gives a square area, so we still need a precise distance check.
		float dist_squared = DistanceSquared(self->s.origin, ent->s.origin);
		if (dist_squared > max_range_squared)
			continue;

		// Quick visibility check before expensive ray trace
		if (!visible(self, ent))
			continue;

		trace_t tr;
		if (!tesla_ray_trace(self, ent, tr))
			continue;

		// This is a valid target, add it to our list for sorting.
		potential_targets[num_targets++] = {
			ent,
			CalculateTeslaPriority(self, ent, dist_squared),
			dist_squared };
	}
	// --- END OF OPTIMIZATION ---

	// Simple insertion sort (more efficient for small arrays)
	for (int i = 1; i < num_targets; i++)
	{
		TeslaTarget key = potential_targets[i];
		int j = i - 1;
		while (j >= 0 && potential_targets[j].priority < key.priority)
		{
			potential_targets[j + 1] = potential_targets[j];
			j--;
		}
		potential_targets[j + 1] = key;
	}

	// Attack phase
	int targets_attacked = 0;
	for (int i = 0; i < num_targets && targets_attacked < max_targets; i++)
	{
		const auto& target = potential_targets[i];

		trace_t tr;
		if (tesla_ray_trace(self, target.ent, tr))
		{
			vec3_t ray_end = tr.endpos;
			vec3_t dir = ray_end - ray_origin;
			dir.normalize();

			T_Damage(target.ent, self, self->teammaster, dir, tr.endpos, tr.plane.normal,
				self->dmg, TESLA_KNOCKBACK, DAMAGE_NO_ARMOR, MOD_TESLA);

			// Try to send the visual effect, respecting rate limits
			if (TrySendTeslaEffect(self, target.ent, ray_origin, ray_end))
			{
				targets_attacked++;
			}
		}
	}

	// Stagger think times to distribute processing load
	if (self->inuse)
	{
		self->think = tesla_think_active;

		// --- PERFORMANCE FIX: Use randomized think frequency ---
		// This prevents all teslas from thinking on the exact same server frame,
		// which smooths out performance spikes when many are deployed. The frequency
		// will vary between 8 and 12 Hz.
		int const random_frequency_hz = 8 + irandom(5); // Result is 8, 9, 10, 11, or 12.
		self->nextthink = level.time + gtime_t::from_hz(random_frequency_hz);
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
	self->nextthink = level.time + FRAME_TIME_S + gtime_t::from_sec(frandom() * 0.1f);
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
	if (!other->inuse || !(other->solid == SOLID_BSP || other->movetype == MOVETYPE_PUSH))
		return;

	// Bounce logic for non-world entities
	if (other != world && (other->movetype != MOVETYPE_PUSH || other->svflags & SVF_MONSTER || other->client || strcmp(other->classname, "func_train") == 0))
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
				// Suelo
				ent->s.angles = {};
				ent->mins = {-4, -4, 0};
				ent->maxs = {4, 4, 8};
				ent->s.origin = ent->s.origin + (tr.plane.normal * TESLA_FLOOR_OFFSET);
				ent->s.origin[2] += TESLA_ORB_OFFSET;
			}
			else
			{
				// Techo
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

			// Detectar si es una pared "plana" basándonos en los componentes X/Y de la normal
			bool is_flat_wall = (fabs(tr.plane.normal[0]) > 0.95f || fabs(tr.plane.normal[1]) > 0.95f);

			if (is_flat_wall)
			{
				// Usar el comportamiento original para paredes planas
				ent->s.angles[PITCH] = dir[PITCH] + 90;
				ent->s.angles[YAW] = dir[YAW];
				ent->s.angles[ROLL] = 0;
				ent->mins = {0, -4, -4};
				ent->maxs = {8, 4, 4};
				ent->s.origin = ent->s.origin + (forward * -TESLA_WALL_OFFSET);
			}
			else
			{
				// Usar el comportamiento nuevo para superficies curvas/inclinadas
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

// Función para contar y manejar el número de teslas de un jugador
void check_player_tesla_limit(edict_t *self)
{
}

void fire_tesla(edict_t *self, const vec3_t &start, const vec3_t &aimdir, int tesla_damage_multiplier, int speed)
{
	// O(1) PERFORMANCE: If player is at their tesla limit, remove the oldest one.
	if (self && self->client && self->client->resp.num_teslas >= TeslaConstants::MAX_TESLAS_PER_PLAYER)
	{
		// Get the oldest tesla from our circular buffer.
		edict_t *oldest = self->client->resp.deployed_teslas[self->client->resp.oldest_tesla_idx];

		// Ensure it's a valid, in-use tesla before removing it.
		if (oldest && oldest->inuse && horde::IsSpecialType(oldest, horde::SpecialEntityTypeID::TESLA_MINE))
		{
            // --- ROBUST METHOD: Directly call the die function ---
            // This explicitly triggers the tesla's full cleanup sequence, which calls
            // tesla_remove() to handle the explosion and player count.
			tesla_die(oldest, self, self, 0, oldest->s.origin, MOD_UNKNOWN);
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

	// horde::IsSpecialType(tesla, horde::SpecialEntityTypeID::TESLA_MINE);
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
		self->client->resp.oldest_tesla_idx = (self->client->resp.oldest_tesla_idx + 1) % TeslaConstants::MAX_TESLAS_PER_PLAYER;

		// Increment the counter.
		self->client->resp.num_teslas++;
	}
}
