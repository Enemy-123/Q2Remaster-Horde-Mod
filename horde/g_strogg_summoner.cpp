#include "../shared.h"
#include "../g_local.h"
#include "../m_player.h"
#include "horde_ids.h"

// ***************************
//  STROGG SUMMONER
// ***************************

// Forward declarations for monster spawning
void SP_monster_soldier(edict_t* self);
void SP_monster_gunner(edict_t* self);
void SP_monster_tank(edict_t* self);
void SP_monster_gladiator(edict_t* self);
void SP_monster_berserk(edict_t* self);
void SP_monster_infantry(edict_t* self);
void SP_monster_brain(edict_t* self);
void SP_monster_medic(edict_t* self);

// Touch function for summoned Strogg - allows owner to push them
TOUCH(strogg_summoned_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	// Only the owner (summoner) can push the monster
	if (!other->client || !self->chain || !self->chain->teammaster)
		return;

	if (other != self->chain->teammaster)
		return;

	// Don't push if owner is not on ground or not touching properly
	if (!other->groundentity || !other_touching_self)
		return;

	// Calculate push direction and strength
	vec3_t push_dir;
	float push_speed = 400.0f; // Faster than barrel push (20 -> 400)

	// Check if owner is looking up (towards sky/roof)
	if (other->client->v_angle[PITCH] < -45.0f) // Looking up more than 45 degrees
	{
		// Vertical push - launch the monster upward
		push_dir = { 0, 0, 1 }; // Straight up
		self->velocity[2] = push_speed * 1.5f; // Strong vertical push

		// Add some forward momentum based on view
		vec3_t forward;
		AngleVectors(other->client->v_angle, forward, nullptr, nullptr);
		self->velocity[0] += forward[0] * push_speed * 0.3f;
		self->velocity[1] += forward[1] * push_speed * 0.3f;

		// Make sure monster is off ground for the jump
		self->groundentity = nullptr;
	}
	else
	{
		// Horizontal push based on player's view direction
		vec3_t forward;
		AngleVectors(other->client->v_angle, forward, nullptr, nullptr);

		// Apply velocity directly for immediate push
		self->velocity[0] = forward[0] * push_speed;
		self->velocity[1] = forward[1] * push_speed;
		self->velocity[2] = 100; // Small upward component to help with obstacles

		// Also use walkmove for ground-based movement
		M_walkmove(self, other->client->v_angle[YAW], push_speed * gi.frame_time_s);
	}

	// Update physics
	gi.linkentity(self);
}

DIE(strogg_summoner_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	// Remove from special entities list
	auto& vec = g_targetable_special_entities;
	vec.erase(std::remove(vec.begin(), vec.end(), self), vec.end());

	self->takedamage = DAMAGE_NONE;

	// Clean up the summoned monster if it still exists
	if (self->teamchain && self->teamchain->inuse)
	{
		// Small explosion effect at monster location
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_EXPLOSION1);
		gi.WritePosition(self->teamchain->s.origin);
		gi.multicast(self->teamchain->s.origin, MULTICAST_PHS, false);

		// Kill the summoned monster
		T_Damage(self->teamchain, self, self, vec3_origin, self->teamchain->s.origin,
				 vec3_origin, 10000, 0, DAMAGE_NONE, MOD_UNKNOWN);
	}

	// Clean up the base
	G_FreeEdict(self);
}

PAIN(strogg_summoner_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	// Base takes damage but doesn't react visually
	// The damage will eventually kill it
}

THINK(strogg_summoner_timeout) (edict_t* self) -> void
{
	// Timeout - remove the summoned monster
	if (self->teammaster && self->teammaster->client)
	{
		gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Summoned Strogg expired.");
	}
	strogg_summoner_die(self, self, self, 9999, self->s.origin, MOD_UNKNOWN);
}

THINK(strogg_base_think) (edict_t* self) -> void
{
	// Keep the base updated
	self->nextthink = level.time + FRAME_TIME_MS;

	// Check if our monster is still alive
	if (!self->teamchain || !self->teamchain->inuse || self->teamchain->deadflag)
	{
		// Monster is dead, remove the base
		if (self->teammaster && self->teammaster->client)
		{
			gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Summoned Strogg has been destroyed.");
		}
		strogg_summoner_die(self, self, self, 0, self->s.origin, MOD_UNKNOWN);
	}
}

// Helper function to spawn a random Strogg monster
static edict_t* spawn_strogg_monster(edict_t* base, const vec3_t& origin, const vec3_t& angles)
{
	edict_t* monster = G_Spawn();

	if (!monster)
		return nullptr;

	// Set spawn position
	monster->s.origin = origin;
	monster->s.angles = angles;

	// Mark as summoned BEFORE calling spawn functions
	monster->monsterinfo.issummoned = true;

	// Set AI flags
	monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	// Randomly select a Strogg monster type (weighted for variety)
	int monster_type = irandom(100);

	if (monster_type < 20) {
		monster->classname = "monster_soldier";
		SP_monster_soldier(monster);
	}
	else if (monster_type < 35) {
		monster->classname = "monster_gunner";
		SP_monster_gunner(monster);
	}
	else if (monster_type < 45) {
		monster->classname = "monster_tank";
		SP_monster_tank(monster);
	}
	else if (monster_type < 55) {
		monster->classname = "monster_gladiator";
		SP_monster_gladiator(monster);
	}
	else if (monster_type < 70) {
		monster->classname = "monster_berserk";
		SP_monster_berserk(monster);
	}
	else if (monster_type < 85) {
		monster->classname = "monster_infantry";
		SP_monster_infantry(monster);
	}
	else if (monster_type < 95) {
		monster->classname = "monster_medic";
		SP_monster_medic(monster);
	}
	else {
		monster->classname = "monster_brain";
		SP_monster_brain(monster);
	}

	// Verify spawn succeeded
	if (!monster->inuse) {
		G_FreeEdict(monster);
		return nullptr;
	}

	// Set the summoner reference (NOT owner to maintain independence)
	monster->teammaster = base->teammaster; // Reference to the player

	// Store base reference for cleanup
	monster->chain = base;

	// Team assignment
	if (base->teammaster && base->teammaster->client) {
		monster->ctf_team = base->teammaster->client->resp.ctf_team;
		monster->monsterinfo.team = base->teammaster->client->resp.ctf_team;
	}

	// Apply friendly flag but do NOT set owner
	// This maintains monster's independence and solidity
	monster->monsterinfo.bonus_flags |= BF_FRIENDLY;

	// Important: Do NOT set monster->owner = base->teammaster
	// We want the monster to remain independent with its own collision

	// FIX: Remove SVF_PLAYER flag that was set by ApplyMonsterBonusFlags
	// This flag makes monsters non-solid/passable, we need them solid
	monster->svflags &= ~SVF_PLAYER;
	monster->svflags |= SVF_MONSTER;
	monster->solid = SOLID_BBOX;
	monster->clipmask = MASK_MONSTERSOLID;

	// Set touch function to allow owner to push the monster
	monster->touch = strogg_summoned_touch;

	gi.linkentity(monster);

	return monster;
}

// Main function to fire/create a Strogg summoner
void fire_strogg_summoner(edict_t* ent, const vec3_t& start, const vec3_t& aimdir)
{
	edict_t* base;
	edict_t* monster;
	vec3_t	 dir;
	vec3_t	 spawn_origin;

	// Get angles from aim direction
	dir = vectoangles(aimdir);
	dir[PITCH] = 0; // Level out pitch

	// Create the invisible base (similar to doppelganger but invisible)
	base = G_Spawn();
	base->s.origin = start;
	base->s.angles = dir;
	base->movetype = MOVETYPE_NONE;  // Base doesn't move
	base->solid = SOLID_NOT;          // Base is not solid
	base->s.renderfx |= RF_IR_VISIBLE;
	base->mins = { -8, -8, -8 };
	base->maxs = { 8, 8, 8 };
	base->s.modelindex = 0;  // No model - completely invisible
	base->teammaster = ent;  // Reference to the player who summoned
	base->flags |= (FL_DAMAGEABLE | FL_TRAP);
	base->takedamage = true;
	base->health = 100;  // Base has some health
	base->pain = strogg_summoner_pain;
	base->die = strogg_summoner_die;

	// Optional: Set a timeout for the summon (5 minutes)
	// Comment out these lines for permanent summons until death
	base->nextthink = level.time + 300_sec;
	base->think = strogg_summoner_timeout;

	base->classname = "strogg_summoner_base";
	base->monsterinfo.issummoned = true;

	// Register with special entities
	base->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::STROGG_SUMMONER);
	g_targetable_special_entities.push_back(base);

	gi.linkentity(base);

	// Use the provided spawn position
	spawn_origin = start;

	// Spawn the actual Strogg monster
	monster = spawn_strogg_monster(base, spawn_origin, dir);

	if (!monster)
	{
		// Failed to spawn monster, clean up base
		gi.LocClient_Print(ent, PRINT_HIGH, "Failed to summon Strogg warrior!");
		G_FreeEdict(base);
		return;
	}

	// Link base and monster
	base->teamchain = monster;

	// Start base thinking to monitor monster
	base->think = strogg_base_think;
	base->nextthink = level.time + FRAME_TIME_MS;

	// Spawn effect
	SpawnGrow_Spawn(spawn_origin, 24.f, 48.f);

	// Sound effect
	gi.sound(ent, CHAN_AUTO, gi.soundindex("medic_commander/monsterspawn1.wav"), 1.f, ATTN_NORM, 0.f);

	// Message to player
	gi.LocClient_Print(ent, PRINT_HIGH, "Strogg {} summoned! Type 'remove strogg' to dismiss.", monster->classname + 8); // Skip "monster_" prefix
}

// Replacement for StroggSummonAtPoint - now returns the base instead of the monster
edict_t* StroggSummonAtPoint(edict_t* owner, const vec3_t& spawn_origin, const vec3_t& spawn_angles)
{
	vec3_t forward;
	AngleVectors(spawn_angles, forward, nullptr, nullptr);

	// Create the summoner at the specified point
	fire_strogg_summoner(owner, spawn_origin, forward);

	// Find the base we just created
	edict_t* base = nullptr;
	edict_t* current = nullptr;

	while ((current = G_FindByString<&edict_t::classname>(current, "strogg_summoner_base")) != nullptr)
	{
		if (current->teammaster == owner)
		{
			vec3_t diff = current->s.origin - spawn_origin;
			if (diff.length() < 10.0f) // Recently created at this position
			{
				base = current;
				break;
			}
		}
	}

	return base; // Return the base (or nullptr if failed)
}

// Updated Use function that works with the new system
void Use_StroggSummon_Impl(edict_t* ent, gitem_t* item)
{
	// --- 1. Initial validation checks ---
	if (ClientIsSpectating(ent->client)) {
		gi.Client_Print(ent, PRINT_HIGH, "Need to be Non-Spect to summon a Strogg\\n");
		return;
	}

	// Check if player already has a summoned Strogg
	edict_t* existing = nullptr;
	int summon_count = 0;
	while ((existing = G_FindByString<&edict_t::classname>(existing, "strogg_summoner_base")) != nullptr)
	{
		if (existing->teammaster == ent)
		{
			summon_count++;
			if (summon_count >= 2) // Limit to 2 summons per player
			{
				gi.LocClient_Print(ent, PRINT_HIGH, "You already have maximum Strogg summons active!");
				return;
			}
		}
	}

	// --- 2. Define placement constants ---
	constexpr vec3_t STROGG_MINS = { -16, -16, -24 };
	constexpr vec3_t STROGG_MAXS = { 16, 16, 32 };
	constexpr int MAX_ATTEMPTS = 16;
	constexpr float MIN_DEPLOY_HEIGHT = 20.0f;

	// --- 3. Create a lambda to check if a spawn position is valid ---
	auto is_valid_spawn_location = [&](const vec3_t& pos) -> bool {
		vec3_t test_pos = pos;

		// Ensure it's not spawning below the player's feet
		if (test_pos.z - ent->s.origin.z < MIN_DEPLOY_HEIGHT) {
			return false;
		}

		// Check line of sight from player to the spawn point
		trace_t const trace = gi.traceline(ent->s.origin, test_pos, ent, MASK_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER);
		if (trace.fraction < 1.0f) {
			return false;
		}

		// Check if the final spot has enough room
		if (!CheckSpawnPoint(test_pos, STROGG_MINS, STROGG_MAXS)) {
			return false;
		}

		// All checks passed
		return true;
	};

	// --- 4. Placement Logic ---
	vec3_t forward;
	AngleVectors(ent->client->v_angle, forward, nullptr, nullptr);
	vec3_t const base_spawn_angles = { 0, ent->client->v_angle[YAW], 0 };

	// Generate random distance and height for placement
	float const distance = irandom(40.f, 125.f);
	float const height = irandom(50.f, 125.f);

	// Calculate the initial desired point
	vec3_t desired_point = ent->s.origin + (forward * distance);
	desired_point.z += height;

	vec3_t final_spawn_point;
	bool found_spot = false;

	// ** Primary Attempt **
	if (is_valid_spawn_location(desired_point)) {
		final_spawn_point = desired_point;
		found_spot = true;
	}

	// ** Fallback Attempts (if primary failed) **
	if (!found_spot) {
		constexpr float RADIUS_MIN = 40.0f;
		constexpr float RADIUS_MAX = 100.0f;

		for (int i = 0; i < MAX_ATTEMPTS; i++) {
			float const random_angle_rad = frandom(2.0f * PIf);
			float const radius = frandom(RADIUS_MIN, RADIUS_MAX);

			vec3_t const offset = {
				cosf(random_angle_rad) * radius,
				sinf(random_angle_rad) * radius,
				0
			};

			vec3_t const test_point = desired_point + offset;

			if (is_valid_spawn_location(test_point)) {
				final_spawn_point = test_point;
				found_spot = true;
				break;
			}
		}
	}

	// --- 5. Final Action: Spawn or Fail ---
	if (found_spot) {
		// Use our new summoner system
		fire_strogg_summoner(ent, final_spawn_point, forward);

		// Check if spawn succeeded by looking for the base
		existing = nullptr;
		bool spawn_success = false;
		while ((existing = G_FindByString<&edict_t::classname>(existing, "strogg_summoner_base")) != nullptr)
		{
			if (existing->teammaster == ent)
			{
				vec3_t diff = existing->s.origin - final_spawn_point;
				if (diff.length() < 10.0f)
				{
					spawn_success = true;
					break;
				}
			}
		}

		if (spawn_success) {
			ent->client->pers.inventory[item->id]--;
			// Message already printed by fire_strogg_summoner
		} else {
			gi.Client_Print(ent, PRINT_HIGH, "Strogg summoning failed.\\n");
		}
	} else {
		gi.Client_Print(ent, PRINT_HIGH, "Cannot find a suitable location to summon Strogg.\\n");
	}
}

// Command to remove summoned Strogg
void Cmd_RemoveStrogg_f(edict_t* ent)
{
	if (!ent || !ent->client)
		return;

	edict_t* current = nullptr;
	bool found = false;
	int removed_count = 0;

	// Find all strogg_summoner_base entities owned by this player
	while ((current = G_FindByString<&edict_t::classname>(current, "strogg_summoner_base")) != nullptr)
	{
		if (current->teammaster == ent)
		{
			// Store next before removing
			edict_t* next = G_FindByString<&edict_t::classname>(current, "strogg_summoner_base");

			// Found a summoned Strogg belonging to this player
			found = true;
			removed_count++;
			strogg_summoner_die(current, ent, ent, 0, current->s.origin, MOD_UNKNOWN);

			current = next;
			if (!current) break;
		}
	}

	if (found)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "{} summoned Strogg dismissed.", removed_count);
	}
	else
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "No summoned Strogg found.");
	}
}