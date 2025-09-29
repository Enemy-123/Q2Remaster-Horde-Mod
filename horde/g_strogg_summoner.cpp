#include "../shared.h"
#include "../g_local.h"
#include "../m_player.h"
#include "horde_ids.h"

// ***************************
//  STROGG SUMMONER
// ***************************

// Forward declarations for monster spawning
void SP_monster_soldier(edict_t* self);
void SP_monster_chick(edict_t* self);
void SP_monster_gunner(edict_t* self);
void SP_monster_tank(edict_t* self);
void SP_monster_gladiator(edict_t* self);
void SP_monster_berserk(edict_t* self);
void SP_monster_infantry(edict_t* self);
void SP_monster_brain(edict_t* self);
void SP_monster_spider(edict_t* self);
void SP_monster_shambler_small(edict_t* self);
void SP_monster_medic(edict_t* self);
void SP_monster_daedalus_bomber(edict_t* self);

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
		// Check if the monster is already dead/dying - if so, let it die naturally
		if (!self->teamchain->deadflag)
		{
			// Monster is still alive, kill it without explosion
			// Just damage it normally instead of forcing an explosion
			T_Damage(self->teamchain, self, self, vec3_origin, self->teamchain->s.origin,
					 vec3_origin, 10000, 0, DAMAGE_NONE, MOD_UNKNOWN);
		}
		// If monster is already dead, just let it finish its death animation naturally
	}

	// Clean up the base
 BecomeTE(self);
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
		// Monster is dead, clean up the base silently
		if (self->teammaster && self->teammaster->client)
		{
			gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Summoned Strogg has been destroyed.");
		}
		// Remove from special entities list
		auto& vec = g_targetable_special_entities;
		vec.erase(std::remove(vec.begin(), vec.end(), self), vec.end());

		// Clean up the base without triggering explosion
		self->takedamage = DAMAGE_NONE;
		G_FreeEdict(self);
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
	horde::MonsterTypeID selected_type = horde::MonsterTypeID::UNKNOWN;

	if (monster_type < 20) {
		selected_type = horde::MonsterTypeID::CHICK;
		SP_monster_chick(monster);
	}
	else if (monster_type < 35) {
		selected_type = horde::MonsterTypeID::GUNNER;
		SP_monster_gunner(monster);
	}
	else if (monster_type < 45) {
		selected_type = horde::MonsterTypeID::DAEDALUS_BOMBER;
		SP_monster_tank(monster);
	}
	else if (monster_type < 55) {
		selected_type = horde::MonsterTypeID::MEDIC;
		SP_monster_medic(monster);
	}
	else if (monster_type < 70) {
		selected_type = horde::MonsterTypeID::SHAMBLER_SMALL;
		SP_monster_shambler_small(monster);
	}
	else if (monster_type < 85) {
		selected_type = horde::MonsterTypeID::INFANTRY;
		SP_monster_infantry(monster);
	}
	else if (monster_type < 95) {
		selected_type = horde::MonsterTypeID::SPIDER;
		SP_monster_spider(monster);
	}
	else {
		selected_type = horde::MonsterTypeID::BRAIN;
		SP_monster_brain(monster);
	}

	// Store the monster type ID for later use
	monster->monsterinfo.monster_type_id = static_cast<uint8_t>(selected_type);

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
	// Only set default team if no owner was found
	if (!base->teammaster || !base->teammaster->client) {
		monster->ctf_team = CTF_TEAM1; // Default team only if no owner
	}


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
	// Check if player is menu protected
	if (IsPlayerMenuProtected(ent)) {
		gi.LocClient_Print(ent, PRINT_HIGH, "You cannot use this while in a menu.\n");
		return;
	}

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
	base->flags |= (FL_TRAP);
	base->takedamage = DAMAGE_NONE;
	//base->health = 100;  // Base has some health
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
		gi.LocClient_Print(ent, PRINT_HIGH, "Failed to summon Strogg warrior!\n");
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

	// Message to player - use MonsterTypeRegistry to get proper name
	const char* monster_name = "warrior";
	auto monster_type = static_cast<horde::MonsterTypeID>(monster->monsterinfo.monster_type_id);
	if (monster_type != horde::MonsterTypeID::UNKNOWN) {
		const char* classname = horde::MonsterTypeRegistry::GetClassname(monster_type);
		if (classname && strncmp(classname, "monster_", 8) == 0) {
			monster_name = classname + 8;  // Skip "monster_" prefix
		}
	}
	gi.LocClient_Print(ent, PRINT_HIGH, "Strogg {} summoned! Type 'remove strogg' to dismiss.\n", monster_name);
}

// Replacement for StroggSummonAtPoint - now returns the base instead of the monster
edict_t* StroggSummonAtPoint(edict_t* owner, const vec3_t& spawn_origin, const vec3_t& spawn_angles)
{
	vec3_t forward;
	AngleVectors(spawn_angles, forward, nullptr, nullptr);

	// Create the summoner at the specified point
	fire_strogg_summoner(owner, spawn_origin, forward);

	// Find the base we just created using the special entities list
	for (edict_t* special_ent : g_targetable_special_entities) {
		if (special_ent && special_ent->inuse &&
			special_ent->special_type_id == static_cast<uint8_t>(horde::SpecialEntityTypeID::STROGG_SUMMONER) &&
			special_ent->teammaster == owner) {
			vec3_t diff = special_ent->s.origin - spawn_origin;
			if (diff.length() < 10.0f) { // Recently created at this position
				return special_ent;
			}
		}
	}

	return nullptr; // Return nullptr if failed
}

// Updated Use function that works with the new system
void Use_StroggSummon_Impl(edict_t* ent, gitem_t* item)
{
	// --- 1. Initial validation checks ---
	if (ClientIsSpectating(ent->client)) {
		gi.Client_Print(ent, PRINT_HIGH, "Need to be Non-Spect to summon a Strogg\n");
		return;
	}

	// Check if player already has a summoned Strogg using the special entities list
	int summon_count = 0;
	for (edict_t* special_ent : g_targetable_special_entities) {
		if (special_ent && special_ent->inuse &&
			special_ent->special_type_id == static_cast<uint8_t>(horde::SpecialEntityTypeID::STROGG_SUMMONER) &&
			special_ent->teammaster == ent) {
			summon_count++;
			if (summon_count >= 4) { // Limit to 2 summons per player
				gi.LocClient_Print(ent, PRINT_HIGH, "You already have maximum Strogg summons active!\n");
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

		// Check if spawn succeeded by looking for the base in special entities
		bool spawn_success = false;
		for (edict_t* special_ent : g_targetable_special_entities) {
			if (special_ent && special_ent->inuse &&
				special_ent->special_type_id == static_cast<uint8_t>(horde::SpecialEntityTypeID::STROGG_SUMMONER) &&
				special_ent->teammaster == ent) {
				vec3_t diff = special_ent->s.origin - final_spawn_point;
				if (diff.length() < 10.0f) {
					spawn_success = true;
					break;
				}
			}
		}

		if (spawn_success) {
			ent->client->pers.inventory[item->id]--;
			// Message already printed by fire_strogg_summoner
		} else {
			gi.Client_Print(ent, PRINT_HIGH, "Strogg summoning failed.\n");
		}
	} else {
		gi.Client_Print(ent, PRINT_HIGH, "Cannot find a suitable location to summon Strogg.\n");
	}
}

// Command to remove summoned Strogg
// Helper function to inherit summoned properties from parent/commander to child entity
void InheritSummonedProperties(edict_t* child, edict_t* parent, bool full_setup = true)
{
	if (!child || !parent || !parent->chain || !parent->teammaster)
		return;

	// Inherit core summoned properties
	child->chain = parent->chain;  // Inherit chain to strogg base
	child->teammaster = parent->teammaster;  // Inherit player owner
	child->monsterinfo.issummoned = true;

	// Full setup for spawned reinforcements (not needed for boss transformations)
	if (full_setup) {
		child->touch = strogg_summoned_touch;  // Allow owner to push
		child->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
		child->monsterinfo.bonus_flags |= BF_FRIENDLY;

		// Set team properly based on the player owner
		if (child->teammaster->client) {
			child->ctf_team = child->teammaster->client->resp.ctf_team;
			child->monsterinfo.team = child->teammaster->client->resp.ctf_team;
		}

		// Ensure proper collision for summoned monsters
		child->svflags &= ~SVF_PLAYER;
		child->svflags |= SVF_MONSTER;
		child->solid = SOLID_BBOX;
		child->clipmask = MASK_MONSTERSOLID;

		gi.linkentity(child);
	}
}

// Helper function to remove summoned entities with optional filtering
enum class RemovalFilter {
	ALL_SUMMONS,     // Remove all summoned entities
	STROGG_ONLY      // Remove only strogg bases
};

static int RemoveSummonedEntities(edict_t* owner, RemovalFilter filter)
{
	if (!owner || !owner->client)
		return 0;

	int removed_count = 0;
	std::vector<edict_t*> ents_to_remove;

	if (filter == RemovalFilter::STROGG_ONLY) {
		// Find all strogg_summoner_base entities owned by this player
		for (edict_t* special_ent : g_targetable_special_entities) {
			if (special_ent && special_ent->inuse &&
				special_ent->special_type_id == static_cast<uint8_t>(horde::SpecialEntityTypeID::STROGG_SUMMONER) &&
				special_ent->teammaster == owner) {
				ents_to_remove.push_back(special_ent);
			}
		}
	}
	else { // ALL_SUMMONS
		// Find all summoned monsters (excluding bases, lasers, and barrels)
		for (int i = 1; i < globals.num_edicts; i++) {
			edict_t* check = &g_edicts[i];
			if (check && check->inuse && check->teammaster == owner && check->chain) {
				// Exclude bases, lasers, and barrels from removal using the special type system
				if (!horde::IsSpecialType(check, horde::SpecialEntityTypeID::STROGG_SUMMONER) &&
				    !horde::IsSpecialType(check, horde::SpecialEntityTypeID::LASER_EMITTER) &&
				    !horde::IsSpecialType(check, horde::SpecialEntityTypeID::LASER_BEAM) &&
				    !horde::IsSpecialType(check, horde::SpecialEntityTypeID::BARREL)) {
					ents_to_remove.push_back(check);
				}
			}
		}
		
		// Also remove the strogg bases themselves (they will clean up properly)
		for (edict_t* special_ent : g_targetable_special_entities) {
			if (special_ent && special_ent->inuse &&
				special_ent->special_type_id == static_cast<uint8_t>(horde::SpecialEntityTypeID::STROGG_SUMMONER) &&
				special_ent->teammaster == owner) {
				ents_to_remove.push_back(special_ent);
			}
		}
	}

	// Remove all found entities
	for (edict_t* ent : ents_to_remove) {
		if (ent && ent->inuse) {
			removed_count++;
			
			// Check if it's a strogg base
			if (ent->special_type_id == static_cast<uint8_t>(horde::SpecialEntityTypeID::STROGG_SUMMONER)) {
				strogg_summoner_die(ent, owner, owner, 0, ent->s.origin, MOD_UNKNOWN);
			}
			// Otherwise just remove it normally
			else if (ent->die) {
				ent->die(ent, owner, owner, 0, ent->s.origin, MOD_UNKNOWN);
			} else {
				G_FreeEdict(ent);
			}
		}
	}

	return removed_count;
}

void Cmd_RemoveStrogg_f(edict_t* ent)
{
	if (!ent || !ent->client)
		return;

	int removed_count = RemoveSummonedEntities(ent, RemovalFilter::STROGG_ONLY);

	if (removed_count > 0)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "{} summoned Strogg dismissed.\n", removed_count);
	}
	else
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "No summoned Strogg found.\n");
	}
}

void Cmd_RemoveAllSummons_f(edict_t* ent)
{
	if (!ent || !ent->client)
		return;

	int removed_count = RemoveSummonedEntities(ent, RemovalFilter::ALL_SUMMONS);

	if (removed_count > 0)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "{} strogg entities dismissed.\n", removed_count);
	}
	else
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "No strogg entities found.\n");
	}
}
