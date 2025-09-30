#include "../shared.h"
#include "../g_local.h"
#include "../m_player.h"
#include "horde_ids.h"
#include "g_horde.h"

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

// Cleanup function to remove a summoned monster from player's tracking array
void RemoveSummonFromPlayerArray(edict_t* monster)
{
	if (!monster || !monster->chain || !monster->chain->client)
		return;

	edict_t* player = monster->chain;

	// Find and remove this monster from the player's tracking array
	for (int i = 0; i < MAX_STROGG_SUMMONS; i++) {
		if (player->client->resp.deployed_summons[i] == monster) {
			player->client->resp.deployed_summons[i] = nullptr;
			player->client->resp.num_summons--;
			break;
		}
	}
}

// Touch function for summoned Strogg - allows owner to push them
TOUCH(strogg_summoned_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	// Only the owner (summoner) can push the monster
	// chain now points directly to the player
	if (!other->client || !self->chain || !self->chain->client)
		return;

	if (other != self->chain)
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

// Helper function to spawn a random Strogg monster
static edict_t* spawn_strogg_monster(edict_t* player, const vec3_t& origin, const vec3_t& angles)
{
	edict_t* monster = G_Spawn();

	if (!monster)
		return nullptr;

	// Set spawn position
	monster->s.origin = origin;
	monster->s.angles = angles;

	// Mark as summoned BEFORE calling spawn functions
	monster->monsterinfo.isfriendlyspawn = true;
	monster->monsterinfo.issummoned = true; // Part of Strogg summoner system

	// Set AI flags
	monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	// Randomly select a Strogg monster type (weighted for variety)
	int monster_type = irandom(100);
	horde::MonsterTypeID selected_type = horde::MonsterTypeID::UNKNOWN;

	if (monster_type < 13) {
		selected_type = horde::MonsterTypeID::CHICK;
		SP_monster_chick(monster);
	}
	else if (monster_type < 26) {
		selected_type = horde::MonsterTypeID::GUNNER;
		SP_monster_gunner(monster);
	}
	else if (monster_type < 39) {
		selected_type = horde::MonsterTypeID::DAEDALUS_BOMBER;
		SP_monster_daedalus_bomber(monster);
	}
	else if (monster_type < 52) {
		selected_type = horde::MonsterTypeID::SPIDER;
		SP_monster_spider(monster);
	}
	else if (monster_type < 65) {
		selected_type = horde::MonsterTypeID::SHAMBLER_SMALL;
		SP_monster_shambler_small(monster);
	}
	else if (monster_type < 71) {
		selected_type = horde::MonsterTypeID::INFANTRY;
		SP_monster_infantry(monster);
	}
	else if (monster_type < 91) {
		selected_type = horde::MonsterTypeID::MEDIC;
		SP_monster_medic(monster);
	}
	else {
		selected_type = horde::MonsterTypeID::BRAIN;
		SP_monster_brain(monster);
	}

	// Store the monster type ID for later use
	monster->monsterinfo.monster_type_id = static_cast<uint8_t>(selected_type);

	// Add to horde precache pool - summoned monsters unlock that type for regular horde spawning
	if (g_horde->integer && selected_type != horde::MonsterTypeID::UNKNOWN) {
		g_precached_monster_types_flags[static_cast<size_t>(selected_type)] = true;
		g_precached_monsters_this_map.insert(selected_type);

		// Unlock model family members (free precache for variants sharing the same model)
		UnlockModelFamilyMembers(selected_type, current_wave_level);
	}

	// Verify spawn succeeded
	if (!monster->inuse) {
		G_FreeEdict(monster);
		return nullptr;
	}

	// Set the summoner reference - direct player reference
	monster->teammaster = player;
	monster->chain = player; // Direct reference to player (no fake base entity)

	// Team assignment
	if (player && player->client) {
		monster->ctf_team = player->client->resp.ctf_team;
		monster->monsterinfo.team = player->client->resp.ctf_team;
	} else {
		// Default team only if no owner
		monster->ctf_team = CTF_TEAM1;
	}

	// Apply friendly flag but do NOT set owner
	// This maintains monster's independence and solidity
	monster->monsterinfo.bonus_flags |= BF_FRIENDLY;

	// Important: Do NOT set monster->owner = player
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

	if (!ent->client) {
		return;
	}

	vec3_t dir = vectoangles(aimdir);
	dir[PITCH] = 0; // Level out pitch

	// Spawn the actual Strogg monster directly (no base entity)
	edict_t* monster = spawn_strogg_monster(ent, start, dir);

	if (!monster)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Failed to summon Strogg warrior!\n");
		return;
	}

	// Add monster to player's tracking array
	bool added = false;
	for (int i = 0; i < MAX_STROGG_SUMMONS; i++) {
		if (!ent->client->resp.deployed_summons[i] || !ent->client->resp.deployed_summons[i]->inuse) {
			ent->client->resp.deployed_summons[i] = monster;
			added = true;
			break;
		}
	}

	if (added) {
		ent->client->resp.num_summons++;
	}

	// Spawn effect
	SpawnGrow_Spawn(start, 24.f, 48.f);

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
	gi.LocClient_Print(ent, PRINT_HIGH, "Strogg {} summoned! ({}/{})\n",
		monster_name, ent->client->resp.num_summons, MAX_STROGG_SUMMONS);
}

// Replacement for StroggSummonAtPoint - returns the monster
edict_t* StroggSummonAtPoint(edict_t* owner, const vec3_t& spawn_origin, const vec3_t& spawn_angles)
{
	if (!owner || !owner->client) {
		return nullptr;
	}

	vec3_t forward;
	AngleVectors(spawn_angles, forward, nullptr, nullptr);

	// Store the count before summoning
	int prev_count = owner->client->resp.num_summons;

	// Create the summoner at the specified point
	fire_strogg_summoner(owner, spawn_origin, forward);

	// Check if a new summon was added
	if (owner->client->resp.num_summons > prev_count) {
		// Find the most recently added summon (the one we just created)
		for (int i = MAX_STROGG_SUMMONS - 1; i >= 0; i--) {
			edict_t* summon = owner->client->resp.deployed_summons[i];
			if (summon && summon->inuse) {
				vec3_t diff = summon->s.origin - spawn_origin;
				if (diff.length() < 64.0f) { // Recently created at this position
					return summon;
				}
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

	// Check if player already has maximum summons using the player array
	if (ent->client->resp.num_summons >= MAX_STROGG_SUMMONS) {
		gi.LocClient_Print(ent, PRINT_HIGH, "You already have maximum Strogg summons active!\n");
		return;
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
		// Store count before spawning
		int prev_count = ent->client->resp.num_summons;

		// Use our new summoner system
		fire_strogg_summoner(ent, final_spawn_point, forward);

		// Check if spawn succeeded by checking if count increased
		if (ent->client->resp.num_summons > prev_count) {
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
	// chain now points directly to the player (no base entity)
	if (!child || !parent || !parent->chain || !parent->chain->client)
		return;

	// Inherit core summoned properties
	child->chain = parent->chain;  // Inherit direct player reference
	child->teammaster = parent->chain;  // chain IS the player now
	child->monsterinfo.isfriendlyspawn = true;
	child->monsterinfo.issummoned = true; // Part of Strogg summoner system

	// Full setup for spawned reinforcements (not needed for boss transformations)
	if (full_setup) {
		child->touch = strogg_summoned_touch;  // Allow owner to push
		child->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
		child->monsterinfo.bonus_flags |= BF_FRIENDLY;

		// Set team properly based on the player owner
		if (child->chain->client) {
			child->ctf_team = child->chain->client->resp.ctf_team;
			child->monsterinfo.team = child->chain->client->resp.ctf_team;
		}

		// Ensure proper collision for summoned monsters
		child->svflags &= ~SVF_PLAYER;
		child->svflags |= SVF_MONSTER;
		child->solid = SOLID_BBOX;
		child->clipmask = MASK_MONSTERSOLID;

		gi.linkentity(child);
	}
}

// Helper function to remove summoned Strogg monsters using player array
int RemoveSummonedEntities(edict_t* owner)
{
	if (!owner || !owner->client)
		return 0;

	int removed_count = 0;

	// Collect all summons first (to avoid modifying array while iterating)
	edict_t* summons_to_kill[MAX_STROGG_SUMMONS] = {nullptr};
	for (int i = 0; i < MAX_STROGG_SUMMONS; i++) {
		edict_t* summon = owner->client->resp.deployed_summons[i];
		if (summon && summon->inuse) {
			summons_to_kill[i] = summon;
			removed_count++;
		}
	}

	// Now clear the array BEFORE killing (to prevent die() from trying to remove again)
	for (int i = 0; i < MAX_STROGG_SUMMONS; i++) {
		owner->client->resp.deployed_summons[i] = nullptr;
	}
	owner->client->resp.num_summons = 0;

	// Kill all collected summons
	for (int i = 0; i < MAX_STROGG_SUMMONS; i++) {
		if (summons_to_kill[i]) {
			edict_t* summon = summons_to_kill[i];
			// Use T_Damage for immediate gibbing instead of die()
			T_Damage(summon, world, world, vec3_origin, summon->s.origin,
					 vec3_origin, 99999, 0, DAMAGE_NO_PROTECTION, MOD_TELEFRAG);
		}
	}

	return removed_count;
}

void Cmd_RemoveStrogg_f(edict_t* ent)
{
	if (!ent || !ent->client)
		return;

	int removed_count = RemoveSummonedEntities(ent);

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

	int removed_count = RemoveSummonedEntities(ent);

	if (removed_count > 0)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "{} summoned Strogg dismissed.\n", removed_count);
	}
	else
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "No summoned Strogg found.\n");
	}
}
