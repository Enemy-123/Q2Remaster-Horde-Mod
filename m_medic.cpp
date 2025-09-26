// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

MEDIC

==============================================================================
*/

#include "g_local.h"
#include "m_medic.h"
#include "m_flash.h"
#include "shared.h"


// Add these prototypes near the top
//bool tesla_check_conversion(edict_t* tesla, edict_t* converter);
//void tesla_convert(edict_t* tesla, edict_t* converter);

constexpr float MEDIC_MIN_DISTANCE = 32;
constexpr float MEDIC_MAX_HEAL_DISTANCE = 400;
constexpr gtime_t MEDIC_TRY_TIME = 3_sec;  // Reduced from 10 seconds for more active healing in horde mode

// FIXME -
//
// owner moved to monsterinfo.healer instead
//
// For some reason, the healed monsters are rarely ending up in the floor
//
// 5/15/1998 I think I fixed these, keep an eye on them

void M_SetEffects(edict_t* ent);
bool FindTarget(edict_t* self);
void FoundTarget(edict_t* self);

static cached_soundindex sound_idle1;
static cached_soundindex sound_pain1;
static cached_soundindex sound_pain2;
static cached_soundindex sound_die;
static cached_soundindex sound_sight;
static cached_soundindex sound_search;
static cached_soundindex sound_hook_launch;
static cached_soundindex sound_hook_hit;
static cached_soundindex sound_hook_heal;
static cached_soundindex sound_hook_retract;

// PMM - commander sounds
static cached_soundindex commander_sound_idle1;
static cached_soundindex commander_sound_pain1;
static cached_soundindex commander_sound_pain2;
static cached_soundindex commander_sound_die;
static cached_soundindex commander_sound_sight;
static cached_soundindex commander_sound_search;
static cached_soundindex commander_sound_hook_launch;
static cached_soundindex commander_sound_hook_hit;
static cached_soundindex commander_sound_hook_heal;
static cached_soundindex commander_sound_hook_retract;
static cached_soundindex commander_sound_spawn;

constexpr std::array<reinforcement_def_t, 6> commander_reinforcements_defs = { {
	{horde::MonsterTypeID::GUNNER_VANILLA, 1}, {horde::MonsterTypeID::GUNNER_VANILLA, 2},
	{horde::MonsterTypeID::JANITOR2, 3},       {horde::MonsterTypeID::INFANTRY, 3},
	{horde::MonsterTypeID::GUNNER, 4},         {horde::MonsterTypeID::GLADIATOR, 6}
} };

// NEW: Compile-time reinforcement definitions for the standard (bonus) Medic.
constexpr std::array<reinforcement_def_t, 3> default_reinforcements_defs = { {
	{horde::MonsterTypeID::GUNNER_VANILLA, 1}, {horde::MonsterTypeID::GLADIATOR, 1},
	{horde::MonsterTypeID::GLADIATOR_B, 1}
} };


constexpr int32_t commander_monster_slots_base = 3;
constexpr int32_t default_monster_slots_base = 2;

static const float inverse_log_slots = pow(2, MAX_REINFORCEMENTS);

constexpr std::array<vec3_t, MAX_REINFORCEMENTS> reinforcement_position = {
	vec3_t { 80, 0, 0 },
	vec3_t { 40, 60, 0 },
	vec3_t { 40, -60, 0 },
	vec3_t { 0, 80, 0 },
	vec3_t { 0, -80, 0 }
};

// filter out the reinforcement indices we can pick given the space we have left
static void M_PickValidReinforcements(edict_t* self, int32_t space, std::vector<uint8_t>& output)
{
	output.clear();

	// Use the new std::span from the reinforcement_list_t
	for (uint8_t i = 0; i < self->monsterinfo.reinforcements.defs.size(); i++)
		if (self->monsterinfo.reinforcements.defs[i].strength <= space)
			output.push_back(i);
}

// pick an array of reinforcements to use; note that this does not modify `self`
std::array<uint8_t, MAX_REINFORCEMENTS> M_PickReinforcements(edict_t* self, int32_t& num_chosen, int32_t max_slots)
{
	static std::vector<uint8_t> available;
	std::array<uint8_t, MAX_REINFORCEMENTS> chosen;
	chosen.fill(255);

	int32_t const num_slots = max(1, (int32_t)log2(frandom(inverse_log_slots)));
	int32_t remaining = self->monsterinfo.monster_slots - self->monsterinfo.monster_used;

	for (num_chosen = 0; num_chosen < num_slots; num_chosen++)
	{
		if ((max_slots && num_chosen == max_slots) || !remaining)
			break;

		M_PickValidReinforcements(self, remaining, available);

		if (!available.size())
			break;

		chosen[num_chosen] = random_element(available);
		// Use the new defs span to get the strength
		remaining -= self->monsterinfo.reinforcements.defs[chosen[num_chosen]].strength;
	}

	return chosen;
}

void M_SetupReinforcements(std::span<const reinforcement_def_t> defs, reinforcement_list_t& list)
{
	list.defs = defs;
}

void fixHealerEnemy(edict_t* self)
{
	if (self->oldenemy && self->oldenemy->inuse && self->oldenemy->health > 0)
	{
		self->enemy = self->oldenemy;
		self->oldenemy = nullptr;
		HuntTarget(self, true);  // Use animate_state=true to trigger run
	}
	else
	{
		self->enemy = self->goalentity = nullptr;
		self->oldenemy = nullptr;
		if (!FindTarget(self))
		{
			// No enemy found, return to patrol/walk mode
			if (self->monsterinfo.walk)
				self->monsterinfo.stand(self);
			else if (self->monsterinfo.stand)
				self->monsterinfo.stand(self);
		}
		// else FindTarget already called FoundTarget which sets run mode
	}
}

void cleanupHeal(edict_t* self)
{
	// clean up target, if we have one and it's legit
	if (self->enemy && self->enemy->inuse && !self->enemy->client && (self->monsterinfo.aiflags & AI_MEDIC))
	{
		cleanupHealTarget(self->enemy);
		// Clear AI_STAND_GROUND flag if we set it during healing
		self->enemy->monsterinfo.aiflags &= ~AI_STAND_GROUND;
	}

	fixHealerEnemy(self);
}

void abortHeal(edict_t* self, bool gib, bool mark)
{
	int				 hurt;
	constexpr vec3_t pain_normal = { 0, 0, 1 };

	if (self->enemy && self->enemy->inuse && !self->enemy->client && (self->monsterinfo.aiflags & AI_MEDIC))
	{
		cleanupHealTarget(self->enemy);
		// Clear AI_STAND_GROUND flag if we set it during healing
		self->enemy->monsterinfo.aiflags &= ~AI_STAND_GROUND;

	// gib em!
	if (mark)
	{
		edict_t* medic = self->enemy->monsterinfo.badMedic1;

		if (medic && medic->inuse &&
			(horde::IsMonsterType(medic, horde::MonsterTypeID::MEDIC) ||
			horde::IsMonsterType(medic, horde::MonsterTypeID::MEDIC_COMMANDER)))
		{
			self->enemy->monsterinfo.badMedic2 = self;
		}
			else
			{
				self->enemy->monsterinfo.badMedic1 = self;
			}
		}

		if (gib)
		{
			// [Paril-KEX] health added in case of weird edge case
			// with fixbot "healing" the corpses
			if (self->enemy->gib_health)
				hurt = -self->enemy->gib_health + max(0, self->enemy->health);
			else
				hurt = 500;

			T_Damage(self->enemy, self, self, vec3_origin, self->enemy->s.origin,
				pain_normal, hurt, 0, DAMAGE_NONE, MOD_UNKNOWN);
		}

		cleanupHeal(self);
	}

	self->monsterinfo.aiflags &= ~AI_MEDIC;
	self->monsterinfo.medicTries = 0;
}

bool finishHeal(edict_t* self)
{
	// Initial null check before any operations
	if (!self || !self->enemy) {
		return false;
	}

	edict_t* healee = self->enemy;

	// Initialize healee state
	healee->spawnflags = SPAWNFLAG_NONE;
	healee->monsterinfo.aiflags &= AI_RESPAWN_MASK;
	healee->target = nullptr;
	healee->targetname = nullptr;
	healee->combattarget = nullptr;
	healee->deathtarget = nullptr;
	healee->healthtarget = nullptr;
	healee->itemtarget = nullptr;
	healee->monsterinfo.healer = self;

	const bool isBodyque = healee->classname && !strcmp(healee->classname, "bodyque");
	const bool insaneDead = healee && horde::IsMonsterType(healee, horde::MonsterTypeID::MISC_INSANE);

	// Handle bodyque resurrection
	if (isBodyque) {
		if (!healee->s.origin || !healee->s.angles) {
			abortHeal(self, false, false);
			return false;
		}

		vec3_t const position = healee->s.origin;
		vec3_t angles = healee->s.angles;
		angles[PITCH] = 0;
		angles[ROLL] = 0;

		edict_t* insane = G_Spawn();
		if (!insane) {
			abortHeal(self, false, false);
			return false;
		}

		insane->s.origin = position;
		insane->s.angles = angles;
		insane->classname = (frandom() > 0.6f) ? "misc_insane" : "monster_soldier_lasergun";

		if (g_horde->integer) {
			insane->item = brandom() ? G_HordePickItem() : nullptr;
		}

		spawn_temp_t st{};
		ED_CallSpawn(insane, st);

		if (!insane->inuse) {
			G_FreeEdict(insane);
			abortHeal(self, false, false);
			return false;
		}

		// Clean up original healee
		if (healee) {
			gi.sound(healee, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);
			ThrowGibs(healee, 50, {
				{ 2, "models/objects/gibs/bone/tris.md2" },
				{ 4, "models/objects/gibs/sm_meat/tris.md2" },
				{ "models/objects/gibs/head2/tris.md2", GIB_HEAD }
				});

			healee->s.modelindex = 0;
			healee->solid = SOLID_NOT;
			healee->takedamage = false;
			healee->svflags |= SVF_NOCLIENT;
			healee->deadflag = true;
			G_FreeEdict(healee);
		}

		self->enemy = healee = insane;
	}

	// Handle insane resurrection
	if (insaneDead) {
		vec3_t const position = healee->s.origin;
		vec3_t angles = healee->s.angles;
		angles[PITCH] = 0;
		angles[ROLL] = 0;

		edict_t* insane = G_Spawn();
		if (!insane) {
			abortHeal(self, false, false);
			return false;
		}

		insane->s.origin = position;
		insane->s.angles = angles;
		insane->classname = "monster_soldier_lasergun";

		if (g_horde->integer) {
			insane->item = brandom() ? G_HordePickItem() : nullptr;
		}

		spawn_temp_t st{};
		ED_CallSpawn(insane, st);

		if (!insane->inuse) {
			G_FreeEdict(insane);
			abortHeal(self, false, false);
			return false;
		}

		// Clean up original healee
		if (healee) {
			gi.sound(healee, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);
			ThrowGibs(healee, 50, {
				{ 2, "models/objects/gibs/bone/tris.md2" },
				{ 4, "models/objects/gibs/sm_meat/tris.md2" },
				{ "models/objects/gibs/head2/tris.md2", GIB_HEAD }
				});

			healee->s.modelindex = 0;
			healee->solid = SOLID_NOT;
			healee->takedamage = false;
			healee->svflags |= SVF_NOCLIENT;
			healee->deadflag = true;
			G_FreeEdict(healee);
		}

		self->enemy = healee = insane;
	}

	// Verify healee after potential transformations
	if (!healee || !healee->inuse) {
		abortHeal(self, false, false);
		return false;
	}

	// Check spatial constraints
	vec3_t maxs = healee->maxs;
	maxs[2] += 48;
	trace_t const tr = gi.trace(healee->s.origin, healee->mins, maxs, healee->s.origin, healee, MASK_MONSTERSOLID);

	if (tr.startsolid || tr.allsolid || tr.ent != world) {
		abortHeal(self, true, false);
		return false;
	}

	healee->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT;

	// Backup health and armor state
	int32_t const old_max_health = healee->max_health;
	item_id_t const old_power_armor_type = healee->monsterinfo.initial_power_armor_type;
	int32_t const old_power_armor_power = healee->monsterinfo.max_power_armor_power;
	int32_t const old_base_health = healee->monsterinfo.base_health;
	int32_t const old_health_scaling = healee->monsterinfo.health_scaling;
	auto const reinforcements = healee->monsterinfo.reinforcements;
	int32_t const slots_from_commander = healee->monsterinfo.slots_from_commander;
	int32_t const monster_slots = healee->monsterinfo.monster_slots;
	int32_t const monster_used = healee->monsterinfo.monster_used;
	int32_t const old_gib_health = healee->gib_health;

	// Respawn with preserved state
	spawn_temp_t st{};
	st.keys_specified.emplace("reinforcements");
	st.reinforcements = "";
	ED_CallSpawn(healee, st);

	// Restore preserved state
	healee->monsterinfo.slots_from_commander = slots_from_commander;
	healee->monsterinfo.reinforcements = reinforcements;
	healee->monsterinfo.monster_slots = monster_slots;
	healee->monsterinfo.monster_used = monster_used;
	healee->gib_health = old_gib_health / 2;
	healee->max_health = old_max_health;
	healee->health = old_max_health / 3;  // Resurrect with 1/3 health
	healee->monsterinfo.power_armor_power = old_power_armor_power / 3;  // Also 1/3 armor
	healee->monsterinfo.max_power_armor_power = old_power_armor_power;
	healee->monsterinfo.power_armor_type = healee->monsterinfo.initial_power_armor_type = old_power_armor_type;
	healee->monsterinfo.base_health = old_base_health;
	healee->monsterinfo.health_scaling = old_health_scaling;

	// Apply visual updates
	if (healee->monsterinfo.setskin) {
		healee->monsterinfo.setskin(healee);
	}

	// Initialize AI state
	if (healee->think) {
		healee->nextthink = level.time;
		healee->think(healee);
	}

	// Update AI flags
	healee->monsterinfo.aiflags &= ~AI_RESURRECTING;
	healee->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT;
	healee->s.effects &= ~EF_FLIES;
	healee->monsterinfo.healer = nullptr;

		// Set revived monster's team to match the medic's team
	healee->ctf_team = self->ctf_team;

	// Handle targeting - for horde mode, just stand initially
	if (g_horde->integer) {
		// In horde mode, make resurrected monster stand briefly
		healee->enemy = nullptr;
		healee->oldenemy = nullptr;
		healee->monsterinfo.pausetime = level.time + 1_sec; // Stand for 1 second before engaging
		if (healee->monsterinfo.stand) {
			healee->monsterinfo.stand(healee);
		}
	}
	else {
		// Non-horde behavior
		edict_t* new_enemy = self->enemy;
		healee->oldenemy = nullptr;
		healee->enemy = new_enemy;

		if (new_enemy && healee->inuse) {
			FoundTarget(healee);
		}
		else {
			healee->enemy = nullptr;
			if (healee->inuse && !FindTarget(healee)) {
				if (healee->inuse) {
					healee->monsterinfo.pausetime = HOLD_FOREVER;
					if (healee->monsterinfo.stand) {
						healee->monsterinfo.stand(healee);
					}
				}
			}
		}
	}

	// Update final state
	healee->monsterinfo.react_to_damage_time = level.time;
	healee->monsterinfo.was_stuck = false;

	cleanupHeal(self);
	return true;
}

bool canReach(edict_t* self, edict_t* other)
{
	vec3_t	spot1;
	vec3_t	spot2;
	trace_t trace;

	spot1 = self->s.origin;
	spot1[2] += self->viewheight;
	spot2 = other->s.origin;
	spot2[2] += other->viewheight;
	trace = gi.traceline(spot1, spot2, self, MASK_PROJECTILE | MASK_WATER);
	return trace.fraction == 1.0f || trace.ent == other;
}

edict_t* healFindMonster(edict_t* self, float radius)
{
	edict_t* ent = nullptr;
	edict_t* best_dead = nullptr;
	edict_t* best_injured_teammate = nullptr;

	while ((ent = findradius(ent, self->s.origin, radius)) != nullptr)
	{
		if (ent == self)
			continue;
		// Check for both monsters and bodyque entities
		if (!(ent->svflags & SVF_MONSTER) && strcmp(ent->classname, "bodyque") != 0)
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

		// FIXME - there's got to be a better way ..
		// make sure we don't spawn people right on top of us
		if (realrange(self, ent) <= MEDIC_MIN_DISTANCE)
			continue;
		if (!visible(self, ent))
			continue;
		if (!strncmp(ent->classname, "player", 6)) // stop it from trying to heal player_noise entities
			continue;

		// Check for injured teammates (alive but hurt)
		if (ent->health > 0 && ent->health < ent->max_health)
		{
			// Store injured teammates for later consideration
			if (OnSameTeam(self, ent))
			{
				if (!best_injured_teammate || ent->health < best_injured_teammate->health)
				{
					best_injured_teammate = ent;
				}
			}
			continue; // Don't consider alive entities for revival
		}

		// Dead entity handling (for revival) - revive ANY dead monster, not just teammates
		if (ent->health > 0)
			continue;
		if ((ent->nextthink) && (ent->think != monster_dead_think))
			continue;

		// For dead monsters, pick the best one regardless of team
		if (!best_dead || ent->max_health > best_dead->max_health)
		{
			best_dead = ent;
		}
	}

	// Priority order for horde mode:
	// 1. Dead monsters (always prioritize resurrection)
	// 2. Injured teammates (for healing)
	if (best_dead)
		return best_dead;

	return best_injured_teammate;
}

// Check for healing opportunities during movement
void medic_check_heal(edict_t* self)
{
	// Don't check if already healing
	if (self->monsterinfo.aiflags & AI_MEDIC)
		return;

	// Look for healing opportunities
	float radius = MEDIC_MAX_HEAL_DISTANCE;
	edict_t* ent = healFindMonster(self, radius);

	if (ent)
	{
		// Check if this is a high-priority target (injured teammate or dead monster)
		bool is_teammate = OnSameTeam(self, ent);
		bool is_injured = (ent->health > 0 && ent->health < ent->max_health);
		bool is_dead = (ent->health <= 0);

		// Calculate distances
		float heal_distance = realrange(self, ent);

		// In horde mode, be more aggressive about healing/reviving
		bool should_heal = false;

		if (g_horde->integer)
		{
			// Horde mode: prioritize healing/resurrection more
			if (is_dead)
			{
				should_heal = true;  // Always try to revive dead monsters
			}
			else if (is_injured && is_teammate && heal_distance < MEDIC_MAX_HEAL_DISTANCE)
			{
				should_heal = true;  // Heal injured teammates in range
			}
		}
		else
		{
			// Normal mode: consider enemy distance
			float enemy_distance = self->enemy ? realrange(self, self->enemy) : 9999.0f;

			if (is_injured && is_teammate && heal_distance < MEDIC_MAX_HEAL_DISTANCE * 0.5f)
			{
				should_heal = true;  // Always heal nearby injured teammates
			}
			else if (is_dead && (enemy_distance > MEDIC_MAX_HEAL_DISTANCE * 1.5f || !self->enemy))
			{
				should_heal = true;  // Revive if safe
			}
		}

		if (should_heal)
		{
			// Switch to healing mode
			self->oldenemy = self->enemy;
			self->enemy = ent;
			self->enemy->monsterinfo.healer = self;
			self->monsterinfo.aiflags |= AI_MEDIC;
			self->timestamp = level.time + MEDIC_TRY_TIME; // Set timer for attempt
			FoundTarget(self);

			// Force immediate action
			if (self->monsterinfo.run)
				self->monsterinfo.run(self);
		}
	}
}

edict_t* medic_FindDeadMonster(edict_t* self)
{
	float	radius;

	// In horde mode, always look for dead monsters to revive
	// regardless of recent damage
	if (!g_horde->integer && self->monsterinfo.react_to_damage_time > level.time)
		return nullptr;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		radius = MEDIC_MAX_HEAL_DISTANCE;
	else
		radius = 1024;  // Large search radius for dead monsters

	edict_t* best = healFindMonster(self, radius);

	if (best)
		self->timestamp = level.time + MEDIC_TRY_TIME;

	return best;
}

MONSTERINFO_IDLE(medic_idle) (edict_t* self) -> void
{
	edict_t* ent;

	// PMM - commander sounds
	if (self->mass == 400)
		gi.sound(self, CHAN_VOICE, sound_idle1, 1, ATTN_IDLE, 0);
	else
		gi.sound(self, CHAN_VOICE, commander_sound_idle1, 1, ATTN_IDLE, 0);

	if (!self->oldenemy)
	{
		ent = medic_FindDeadMonster(self);
		if (ent)
		{
			self->oldenemy = self->enemy;
			self->enemy = ent;
			self->enemy->monsterinfo.healer = self;
			self->monsterinfo.aiflags |= AI_MEDIC;
			FoundTarget(self);
		}
	}
}

MONSTERINFO_SEARCH(medic_search) (edict_t* self) -> void
{
	edict_t* ent;

	// PMM - commander sounds
	if (self->mass == 400)
		gi.sound(self, CHAN_VOICE, sound_search, 1, ATTN_IDLE, 0);
	else
		gi.sound(self, CHAN_VOICE, commander_sound_search, 1, ATTN_IDLE, 0);

	if (!self->oldenemy)
	{
		ent = medic_FindDeadMonster(self);
		if (ent)
		{
			self->oldenemy = self->enemy;
			self->enemy = ent;
			self->enemy->monsterinfo.healer = self;
			self->monsterinfo.aiflags |= AI_MEDIC;
			FoundTarget(self);
		}
	}
}

MONSTERINFO_SIGHT(medic_sight) (edict_t* self, edict_t* other) -> void
{
	// PMM - commander sounds
	if (self->mass == 400)
		gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, commander_sound_sight, 1, ATTN_NORM, 0);
}

mframe_t medic_frames_stand[] = {
	{ ai_stand, 0, medic_idle },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 2, medic_check_heal}, 
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 2, medic_check_heal}, 
};
MMOVE_T(medic_move_stand) = { FRAME_wait1, FRAME_wait90, medic_frames_stand, nullptr };

MONSTERINFO_STAND(medic_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &medic_move_stand);
}

mframe_t medic_frames_walk[] = {
	{ ai_walk, 6.2f },
	{ ai_walk, 18.1f, monster_footstep },
	{ ai_walk, 1 },
	{ ai_walk, 9 },
	{ ai_walk, 10 },
	{ ai_walk, 9 },
	{ ai_walk, 11 },
	{ ai_walk, 11.6f, monster_footstep },
	{ ai_walk, 2 },
	{ ai_walk, 9.9f },
	{ ai_walk, 14 },
	{ ai_walk, 9.3f }
};
MMOVE_T(medic_move_walk) = { FRAME_walk1, FRAME_walk12, medic_frames_walk, nullptr };

MONSTERINFO_WALK(medic_walk) (edict_t* self) -> void
{
	// Check for healing opportunities even while walking
	if (!(self->monsterinfo.aiflags & AI_MEDIC))
	{
		edict_t* ent = medic_FindDeadMonster(self);
		if (ent)
		{
			// Prioritize injured teammates
			bool is_teammate = OnSameTeam(self, ent);
			if ((ent->health > 0 && is_teammate) || ent->health <= 0)
			{
				self->oldenemy = self->enemy;
				self->enemy = ent;
				self->enemy->monsterinfo.healer = self;
				self->monsterinfo.aiflags |= AI_MEDIC;
				FoundTarget(self);
				return;
			}
		}
	}

	M_SetAnimation(self, &medic_move_walk);
}

mframe_t medic_frames_run[] = {
	{ ai_run, 18, medic_check_heal },
	{ ai_run, 22.5f, monster_footstep },
	{ ai_run, 25.4f, monster_done_dodge },
	{ ai_run, 23.4f, monster_footstep },
	{ ai_run, 24, medic_check_heal },
	{ ai_run, 35.6f }
};
MMOVE_T(medic_move_run) = { FRAME_run1, FRAME_run6, medic_frames_run, nullptr };

MONSTERINFO_RUN(medic_run) (edict_t* self) -> void
{
	monster_done_dodge(self);

	// Only look for healing targets if not already in medic mode
	if (!(self->monsterinfo.aiflags & AI_MEDIC))
	{
		edict_t* ent;

		// Check for dead monsters to resurrect
		ent = medic_FindDeadMonster(self);
		if (ent)
		{
			// Dead monster found - prioritize resurrection over everything in horde mode
			if (ent->health <= 0)
			{
				// In horde mode, immediately drop everything to resurrect
				if (g_horde->integer || !self->enemy ||
				    realrange(self, self->enemy) > MEDIC_MAX_HEAL_DISTANCE * 1.5f)
				{
					self->oldenemy = self->enemy;
					self->enemy = ent;
					self->enemy->monsterinfo.healer = self;
					self->monsterinfo.aiflags |= AI_MEDIC;
					self->timestamp = level.time + MEDIC_TRY_TIME;
					FoundTarget(self);
					return;
				}
			}
			// Injured teammate - heal if conditions are right
			else if (OnSameTeam(self, ent))
			{
				float heal_distance = realrange(self, ent);
				edict_t* current_enemy = self->enemy;

				// Prioritize healing teammates if they're close or no immediate threat
				if (heal_distance < MEDIC_MAX_HEAL_DISTANCE ||
				    !current_enemy ||
				    realrange(self, current_enemy) > MEDIC_MAX_HEAL_DISTANCE * 1.5f)
				{
					self->oldenemy = self->enemy;
					self->enemy = ent;
					self->enemy->monsterinfo.healer = self;
					self->monsterinfo.aiflags |= AI_MEDIC;
					FoundTarget(self);
					return;
				}
			}
		}
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &medic_move_stand);
	else
		M_SetAnimation(self, &medic_move_run);
}

mframe_t medic_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(medic_move_pain1) = { FRAME_paina2, FRAME_paina6, medic_frames_pain1, medic_run };

mframe_t medic_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep }
};
MMOVE_T(medic_move_pain2) = { FRAME_painb2, FRAME_painb13, medic_frames_pain2, medic_run };

PAIN(medic_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	monster_done_dodge(self);

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;

	float const r = frandom();

	if (self->mass > 400)
	{
		if (damage < 35)
		{
			gi.sound(self, CHAN_VOICE, commander_sound_pain1, 1, ATTN_NORM, 0);

			if (mod.id != MOD_CHAINFIST)
				return;
		}

		gi.sound(self, CHAN_VOICE, commander_sound_pain2, 1, ATTN_NORM, 0);
	}
	else if (r < 0.5f)
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	// if we're healing someone, we ignore pain
	if (mod.id != MOD_CHAINFIST && (self->monsterinfo.aiflags & AI_MEDIC))
		return;

	if (self->mass > 400)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

		if (r < (min((static_cast<float>(damage) * 0.005f), 0.5f))) // no more than 50% chance of big pain
			M_SetAnimation(self, &medic_move_pain2);
		else
			M_SetAnimation(self, &medic_move_pain1);
	}
	else if (r < 0.5f)
		M_SetAnimation(self, &medic_move_pain1);
	else
		M_SetAnimation(self, &medic_move_pain2);

	// PMM - clear duck flag
	if (self->monsterinfo.aiflags & AI_DUCKED)
		monster_duck_up(self);

	abortHeal(self, false, false);
}

MONSTERINFO_SETSKIN(medic_setskin) (edict_t* self) -> void
{
	if ((self->health < (self->max_health / 2)))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

void medic_fire_blaster_bolt(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t start;
	vec3_t forward, right;
	vec3_t end;
	vec3_t dir;
	int damage = 30;
	monster_muzzleflash_id_t mz;

	mz = static_cast<monster_muzzleflash_id_t>(((self->mass > 400) ? MZ2_MEDIC_HYPERBLASTER2_1 : MZ2_MEDIC_HYPERBLASTER1_1));

	AngleVectors(self->s.angles, forward, right, nullptr);
	const vec3_t& offset = monster_flash_offset[mz];
	start = M_ProjectFlashSource(self, offset, forward, right);
	end = self->enemy->s.origin;
	end[2] += self->enemy->viewheight;
	dir = end - start;
	dir.normalize();

	monster_fire_blaster_bolt(self, start, dir, damage, 1150, mz, EF_BLUEHYPERBLASTER);
}

void medic_fire_blaster(edict_t* self)
{
	if (!M_HasValidTarget(self))
	{
		return; // Stop immediately if the target is invalid.
	}

	vec3_t	  start;
	vec3_t	  forward, right;
	vec3_t	  end;
	vec3_t	  dir;
	effects_t effect;
	int		  damage = 2;
	monster_muzzleflash_id_t mz;

	if ((self->s.frame == FRAME_attack9) || (self->s.frame == FRAME_attack12))
	{
		effect = EF_BLASTER;
		damage = 6;
		mz = (self->mass > 400) ? MZ2_MEDIC_BLASTER_2 : MZ2_MEDIC_BLASTER_1;
	}
	else
	{
		effect = (self->s.frame % 4) ? EF_NONE : EF_HYPERBLASTER;
		mz = static_cast<monster_muzzleflash_id_t>(((self->mass > 400) ? MZ2_MEDIC_HYPERBLASTER2_1 : MZ2_MEDIC_HYPERBLASTER1_1) + (self->s.frame - FRAME_attack19));
	}

	AngleVectors(self->s.angles, forward, right, nullptr);
	const vec3_t& offset = monster_flash_offset[mz];
	start = M_ProjectFlashSource(self, offset, forward, right);

	end = self->enemy->s.origin;
	end[2] += self->enemy->viewheight;
	dir = end - start;
	dir.normalize();

	        // Determine the actual target. If the enemy is a laser beam,
        // the real target for this animation check is its owner (the emitter).
        edict_t* target = self->enemy;
        if (horde::IsSpecialType(target, horde::SpecialEntityTypeID::LASER_BEAM))
        {
            target = target->owner;
        }

        // Check if the resolved target is a deployable that warrants a special attack animation.
        // This also fixes a bug where it was checking 'self' instead of 'self->enemy' for the sentry gun.
        if (target && (horde::IsSpecialType(target, horde::SpecialEntityTypeID::TESLA_MINE) ||
                       horde::IsSpecialType(target, horde::SpecialEntityTypeID::SENTRY_GUN) ||
                       horde::IsSpecialType(target, horde::SpecialEntityTypeID::LASER_EMITTER)))
        {
		damage *= 1.5f;
		}
	// medic commander shoots blaster2
	if (self->mass > 400)
		monster_fire_blaster2(self, start, dir, damage, 1000, mz, effect);
	else
	{
		monster_fire_blaster(self, start, dir, damage, 1000, mz, effect);
	}
}

void medic_dead(edict_t* self)
{
	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, -8 };
	monster_dead(self);
}

static void medic_shrink(edict_t* self)
{
	self->maxs[2] = -2;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t medic_frames_death[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -18.f, monster_footstep },
	{ ai_move, -10.f, medic_shrink },
	{ ai_move, -6.f },
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
	{ ai_move, 0, monster_footstep },
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
MMOVE_T(medic_move_death) = { FRAME_death2, FRAME_death30, medic_frames_death, medic_dead };

DIE(medic_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	// if we had a pending patient, he was already freed up in Killed

	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		ThrowGibs(self, damage, {
			{ 2, "models/objects/gibs/bone/tris.md2" },
			{ "models/objects/gibs/sm_meat/tris.md2" },
			{ "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
			{ "models/monsters/medic/gibs/chest.md2", GIB_SKINNED },
			{ 2, "models/monsters/medic/gibs/leg.md2", GIB_SKINNED | GIB_UPRIGHT },
			{ "models/monsters/medic/gibs/hook.md2", GIB_SKINNED | GIB_UPRIGHT },
			{ "models/monsters/medic/gibs/gun.md2", GIB_SKINNED | GIB_UPRIGHT },
			{ "models/monsters/medic/gibs/head.md2", GIB_SKINNED | GIB_HEAD }
			});

		self->deadflag = true;
		return;
	}

	if (self->deadflag)
		return;

	// regular death
	//	PMM
	if (self->mass == 400)
		gi.sound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, commander_sound_die, 1, ATTN_NORM, 0);
	//
	self->deadflag = true;
	self->takedamage = true;

	M_SetAnimation(self, &medic_move_death);
}

mframe_t medic_frames_duck[] = {
	{ ai_move, -1 },
	{ ai_move, -1, monster_duck_down },
	{ ai_move, -1, monster_duck_hold },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 }, // PMM - duck up used to be here
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1, monster_duck_up }
};
MMOVE_T(medic_move_duck) = { FRAME_duck2, FRAME_duck14, medic_frames_duck, medic_run };

// PMM -- moved dodge code to after attack code so I can reference attack frames

mframe_t medic_frames_attackHyperBlaster[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge },
	{ ai_charge },
	// [Paril-KEX] end on 36 as intended
	{ ai_charge, 2.f }, // 33
	{ ai_charge, 3.f, monster_footstep },
};
MMOVE_T(medic_move_attackHyperBlaster) = { FRAME_attack15, FRAME_attack34, medic_frames_attackHyperBlaster, medic_run };

static void medic_quick_attack(edict_t* self)
{
	if (frandom() < 0.5f)
	{
		M_SetAnimation(self, &medic_move_attackHyperBlaster, false);
		self->monsterinfo.nextframe = FRAME_attack16;
	}
}

void medic_continue(edict_t* self)
{
	// Validar que self es válido
	if (!self) {
		gi.Com_PrintFmt("Error: medic_continue - null self");
		return;
	}

	// Validar que enemy existe y está vivo
	if (!self->enemy || !self->enemy->inuse || self->enemy->health <= 0) {
		return;
	}

	// Check if we should continue attacking
	if (visible(self, self->enemy))
	{
		// In horde mode, be less aggressive with continuous attacks
		float continue_chance = g_horde->integer ? 0.5f : 0.75f;

		if (frandom() <= continue_chance)
		{
			// Continue attacking with HyperBlaster
			M_SetAnimation(self, &medic_move_attackHyperBlaster, false);
		}
		else
		{
			// Stop attacking and return to run
			M_SetAnimation(self, &medic_move_run);
		}
	}
	else
	{
		// No visibility, stop attacking and chase
		M_SetAnimation(self, &medic_move_run);
	}
}

mframe_t medic_frames_attackBlaster[] = {
	{ ai_charge, 5 },
	{ ai_charge, 3 },
	{ ai_charge, 2 },
	{ ai_charge, 0, medic_quick_attack },
	{ ai_charge, 0, monster_footstep },
	{ ai_charge },
	{ ai_charge, 0, medic_fire_blaster_bolt },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, medic_fire_blaster_bolt },
	{ ai_charge },
	{ ai_charge, 0, medic_continue } // Change to medic_continue... Else, go to frame 32
};
MMOVE_T(medic_move_attackBlaster) = { FRAME_attack3, FRAME_attack14, medic_frames_attackBlaster, medic_run };

void medic_hook_launch(edict_t* self)
{
	// PMM - commander sounds
	if (self->mass == 400)
		gi.sound(self, CHAN_WEAPON, sound_hook_launch, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_WEAPON, commander_sound_hook_launch, 1, ATTN_NORM, 0);

	// Ensure we're committed to the healing/resurrection
	if (self->monsterinfo.aiflags & AI_MEDIC && self->enemy)
	{
		// Lock the medic into this action
		self->monsterinfo.pausetime = level.time + 2_sec;
	}
}

constexpr vec3_t medic_cable_offsets[] = {
	{ 45.0f, -9.2f, 15.5f },
	{ 48.4f, -9.7f, 15.2f },
	{ 47.8f, -9.8f, 15.8f },
	{ 47.3f, -9.3f, 14.3f },
	{ 45.4f, -10.1f, 13.1f },
	{ 41.9f, -12.7f, 12.0f },
	{ 37.8f, -15.8f, 11.2f },
	{ 34.3f, -18.4f, 10.7f },
	{ 32.7f, -19.7f, 10.4f },
	{ 32.7f, -19.7f, 10.4f }
};

// Add this helper function to check if a target needs healing
bool M_NeedRegen(edict_t* target)
{
    if (!target || !target->inuse)
        return false;
    
    // Check if target is damaged
    if (target->health > 0 && target->health < target->max_health)
        return true;
    
    // Check if armor needs repair
    if (target->monsterinfo.power_armor_power < target->monsterinfo.max_power_armor_power)
        return true;
    
    return false;
}

// Modified cable attack function with healing loop
void medic_cable_attack(edict_t* self)
{
    vec3_t   offset, start, end, forward, right, dir;
    trace_t  tr;
    float    distance;
    uint32_t damage;

    // Make sure we are in a good frame for our next decision
    if (self->s.frame < FRAME_attack42 || self->s.frame > FRAME_attack50)
        return;

    // Reconsider target every few frames.
    if (self->s.frame == FRAME_attack43)
    {
        // Re-evaluate healing target
        if (!self->enemy || !self->enemy->inuse)
            return;
    }

    // We have an active cable attack - check if in range
    distance = self->enemy ? realrange(self, self->enemy) : 9999.0f;
    if (self->enemy && distance <= MEDIC_MAX_HEAL_DISTANCE && visible(self, self->enemy))
    {
        // Play attack sound
        if (self->s.frame == FRAME_attack43 || self->s.frame == FRAME_attack50)
            gi.sound(self, CHAN_WEAPON, sound_hook_launch, 1, ATTN_NORM, 0);

        // Calculate cable origin
        AngleVectors(self->s.angles, forward, right, nullptr);
        offset = medic_cable_offsets[self->s.frame - FRAME_attack42];
        start = M_ProjectFlashSource(self, offset, forward, right);

        // Trace to the enemy
        tr = gi.traceline(start, self->enemy->s.origin, self, MASK_SOLID);

        // Draw cable (make wider and more visible for healing)
        if (M_NeedRegen(self->enemy))
            gi.WriteByte(svc_temp_entity);
        else
            gi.WriteByte(svc_temp_entity);

        gi.WriteByte(TE_MEDIC_CABLE_ATTACK);
        gi.WriteShort(self - g_edicts);
        gi.WritePosition(start);
        gi.WritePosition(tr.endpos);
        gi.multicast(self->s.origin, MULTICAST_PVS, false);

        // Apply healing effect
        if (M_NeedRegen(self->enemy))
        {
            // Play healing sound effect
            if (self->s.frame == FRAME_attack43)
                gi.sound(self->enemy, CHAN_AUTO, sound_hook_heal, 1, ATTN_NORM, 0);
        }

        // Damage or healing  logic
        if (self->enemy->health <= 0 && g_horde->integer && self->enemy->svflags & SVF_DEADMONSTER)
        {
            // Resurrect corpse in horde mode
            if (self->s.frame == FRAME_attack43)
            {
                // Start resurrection - mark as resurrecting but keep as dead for now
                self->enemy->monsterinfo.aiflags |= AI_RESURRECTING;
                self->enemy->monsterinfo.attack_finished = level.time + 4_sec; // resurrection duration
                // Keep health at 0 and dead flags until resurrection completes
                // This prevents shadow flickering and other visual issues
            }
        }
        else if (M_NeedRegen(self->enemy))
        {
            // Mark that this monster is being healed
            // Regular healing logic
            // apply healing instead of damage
            bool    is_friendly = (self->monsterinfo.aiflags & AI_GOOD_GUY) != 0;
            int     heal_amount = is_friendly ? 30 : 8; // boosted heal vs normal

            self->enemy->health = min((int)self->enemy->health + heal_amount, (int)self->enemy->max_health);

            // If monster has power armor, heal that too
        if (self->enemy->monsterinfo.power_armor_power < self->enemy->monsterinfo.max_power_armor_power)
        {
            self->enemy->monsterinfo.power_armor_power += heal_amount / 2;
            if (self->enemy->monsterinfo.power_armor_power > self->enemy->monsterinfo.max_power_armor_power)
                self->enemy->monsterinfo.power_armor_power = self->enemy->monsterinfo.max_power_armor_power;
        }
        
        // Hold monster in place while healing using dedicated healing pause
        if (self->enemy->svflags & SVF_MONSTER)
        {
            self->enemy->monsterinfo.healing_pause_time = level.time + 0.5_sec;  // Pause while being healed
            self->enemy->monsterinfo.healer = self;  // Set the healer reference
        }
        
        // Check if fully healed
        if (!M_NeedRegen(self->enemy) && self->s.frame >= FRAME_attack48)
        {
            cleanupHeal(self);
            self->monsterinfo.nextframe = FRAME_attack52;
        }
        // Loop healing animation if still needs healing
        else if (M_NeedRegen(self->enemy) && self->s.frame >= FRAME_attack48)
        {
            self->monsterinfo.nextframe = FRAME_attack44; // Loop back to healing frames
        }
    }
    }
    else
    {
        // No more enemy? Abort
        cleanupHeal(self);
    }

    // Check for resurrecti on completion (horde mode)
    if (g_horde->integer && self->enemy && (self->enemy->monsterinfo.aiflags & AI_RESURRECTING))
    {
        // Continue resurrection animation
        if (self->s.frame == FRAME_attack44)
        {
            self->enemy->monsterinfo.healing_pause_time = level.time + 3_sec;  // Keep corpse in place during resurrection
            self->enemy->monsterinfo.healer = self;  // Maintain healer reference
        }
        
        // Check if resurrection is complete
        if (level.time >= self->enemy->monsterinfo.attack_finished)
        {
            // Resurrection complete
            finishHeal(self);
            self->monsterinfo.nextframe = FRAME_attack52;
        }
        // Keep looping resurrection animation
        else if (self->s.frame >= FRAME_attack48)
        {
            self->monsterinfo.nextframe = FRAME_attack44; // Loop back
        }
    }
    
    // End of attack?
    if (self->s.frame == FRAME_attack50)
    {
        // If our enemy is no longer valid, or out of reach, abort.
        if (!self->enemy || distance > MEDIC_MAX_HEAL_DISTANCE || !visible(self, self->enemy) || !M_NeedRegen(self->enemy))
        {
            cleanupHeal(self);
            if (distance > 190.f && visible(self, self->enemy) && self->health > 0 && infront(self, self->enemy))
                self->monsterinfo.nextframe = FRAME_attack41;
        }
        else // our enemy is still good to go, reset and keep going.
        {
            // continue!
            if (self->enemy && self->enemy->health <= 0 && g_horde->integer && self->enemy->svflags & SVF_DEADMONSTER)
                return; // Keep going for resurrection
                
            if (M_NeedRegen(self->enemy))
                self->monsterinfo.nextframe = FRAME_attack42;
        }
    }
}

// Add continue function to check if healing should continue
void medic_cable_continue(edict_t* self)
{
    if (!self->enemy || !self->enemy->inuse)
    {
        abortHeal(self, false, false);
        return;
    }

    // If resurrecting a corpse, continue to frame 50 for finishHeal
    if (self->enemy->health <= 0)
    {
        // Continue resurrection to frame 50 where finishHeal is called
        self->monsterinfo.nextframe = FRAME_attack50;
        return;
    }

    float dist = (self->s.origin - self->enemy->s.origin).length();

    // Continue healing if target still needs it and is in range
    if (M_NeedRegen(self->enemy) && dist <= MEDIC_MAX_HEAL_DISTANCE)
    {
        // Loop back to healing frames
        self->monsterinfo.nextframe = FRAME_attack42;
    }
    else
    {
        // Done healing, retract cable
        self->monsterinfo.nextframe = FRAME_attack52;
    }
}

void medic_hook_retract(edict_t* self)
{
	if (self->mass == 400)
		gi.sound(self, CHAN_WEAPON, sound_hook_retract, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_WEAPON, sound_hook_retract, 1, ATTN_NORM, 0);

	self->monsterinfo.aiflags &= ~AI_MEDIC;
	fixHealerEnemy(self);
}

// Modified animation frames to support healing loop
mframe_t medic_frames_attackCable[] = {
	{ ai_charge, -5.f },
    { ai_charge, -6.f },
	{ ai_charge, -5.f },
    { ai_charge, -6.f },
    { ai_charge, -4.7f }, // 37
    { ai_charge, -5.f },
    { ai_charge, -6.f },
    { ai_charge, -4.f }, // 40
    { ai_charge, 0, monster_footstep },
    { ai_move, 0, medic_hook_launch }, // 42
    { ai_move, 0, medic_cable_attack }, // 43
    { ai_move, 0, medic_cable_attack }, // 44 - healing frame start
    { ai_move, 0, medic_cable_attack },
    { ai_move, 0, medic_cable_attack },
    { ai_move, 0, medic_cable_attack },
    { ai_move, 0, medic_cable_attack }, // 48 - healing frame end

    { ai_move, 0, medic_cable_attack }, // 50
	{ ai_move, 0, medic_cable_continue }, // 51 - check if should continue
    { ai_move, 0, medic_cable_attack }, // 52

    { ai_move, -1.5f },
	{ ai_move, 0, medic_hook_retract }, // 53
	{ ai_move, -1.5f },
	{ ai_move, -1.5f },
	{ ai_move, -1.5f },
	{ ai_move, -1.5f },
	{ ai_move, -1.5f },
    { ai_move, -1.2f, monster_footstep },
    { ai_move, -3.f }
};
MMOVE_T(medic_move_attackCable) = { FRAME_attack33, FRAME_attack60, medic_frames_attackCable, medic_run };

// mframe_t medic_frames_attackCable[] = {
// 	// ROGUE - negated 36-40 so he scoots back from his target a little
// 	// ROGUE - switched 33-36 to ai_charge
// 	// ROGUE - changed frame 52 to 60 to compensate for changes in 36-40
// 	// [Paril-KEX] started on 36 as they intended
// 	{ ai_charge, -4.7f }, // 37
// 	{ ai_charge, -5.f },
// 	{ ai_charge, -6.f },
// 	{ ai_charge, -4.f }, // 40
// 	{ ai_charge, 0, monster_footstep },
// 	{ ai_move, 0, medic_hook_launch },	// 42
// 	{ ai_move, 0, medic_cable_attack }, // 43
// 	{ ai_move, 0, medic_cable_attack },
// 	{ ai_move, 0, medic_cable_attack },
// 	{ ai_move, 0, medic_cable_attack },
// 	{ ai_move, 0, medic_cable_attack },
// 	{ ai_move, 0, medic_cable_attack },
// 	{ ai_move, 0, medic_cable_attack },
// 	{ ai_move, 0, medic_cable_attack },
// 	{ ai_move, 0, medic_cable_attack }, // 51
// 	{ ai_move, 0, medic_hook_retract }, // 52
// 	{ ai_move, -1.5f },
// 	{ ai_move, -1.2f, monster_footstep },
// 	{ ai_move, -3.f }
// };
//MMOVE_T(medic_move_attackCable) = { FRAME_attack37, FRAME_attack55, medic_frames_attackCable, medic_run };

void medic_start_spawn(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, commander_sound_spawn, 1, ATTN_NORM, 0);
	self->monsterinfo.nextframe = FRAME_attack48;
}

void medic_determine_spawn(edict_t* self)
{
	vec3_t f, r, offset, startpoint, spawnpoint;
	int    count;
	int    num_success = 0;

	AngleVectors(self->s.angles, f, r, nullptr);

	int num_summoned;
	self->monsterinfo.chosen_reinforcements = M_PickReinforcements(self, num_summoned);

	for (count = 0; count < num_summoned; count++)
	{
		offset = reinforcement_position[count];

		if (self->s.scale)
			offset *= self->s.scale;

		startpoint = M_ProjectFlashSource(self, offset, f, r);
		startpoint[2] += 10 * (self->s.scale ? self->s.scale : 1.0f);

		
		uint8_t def_index = self->monsterinfo.chosen_reinforcements[count];
		if (def_index >= self->monsterinfo.reinforcements.defs.size()) continue;

		const auto& reinforcement_def = self->monsterinfo.reinforcements.defs[def_index];
		horde::MonsterTypeID typeId = reinforcement_def.typeId;
		vec3_t mins, maxs;
		GetPredictedScaledBounds(typeId, mins, maxs);
		

		if (FindSpawnPoint(startpoint, mins, maxs, spawnpoint, 32))
		{
			if (CheckGroundSpawnPoint(spawnpoint, mins, maxs, 256, -1))
			{
				num_success++;
				count = num_summoned; // Found a spot, we're done here
			}
		}
	}

	if (num_success == 0)
	{
		for (count = 0; count < num_summoned; count++)
		{
			offset = reinforcement_position[count];
			if (self->s.scale) offset *= self->s.scale;
			offset[0] *= -1.0f;
			offset[1] *= -1.0f;
			startpoint = M_ProjectFlashSource(self, offset, f, r);
			startpoint[2] += 10;

			// --- START OF FIX (Second Loop) ---
			uint8_t def_index = self->monsterinfo.chosen_reinforcements[count];
			if (def_index >= self->monsterinfo.reinforcements.defs.size()) continue;

			const auto& reinforcement_def = self->monsterinfo.reinforcements.defs[def_index];
			horde::MonsterTypeID typeId = reinforcement_def.typeId;
			vec3_t mins, maxs;
			GetPredictedScaledBounds(typeId, mins, maxs);
			

			if (FindSpawnPoint(startpoint, mins, maxs, spawnpoint, 32))
			{
				if (CheckGroundSpawnPoint(spawnpoint, mins, maxs, 256, -1))
				{
					num_success++;
					count = num_summoned; // Found a spot, we're done here
				}
			}
		}

		if (num_success)
		{
			self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
			self->ideal_yaw = anglemod(self->s.angles[YAW]) + 180;
			if (self->ideal_yaw > 360.0f)
				self->ideal_yaw -= 360.0f;
		}
	}

	if (num_success == 0)
		self->monsterinfo.nextframe = FRAME_attack53;
}

void medic_spawngrows(edict_t* self)
{
	vec3_t f, r, offset, startpoint, spawnpoint;
	int    count;
	int    num_summoned = 0;
	int    num_success = 0;
	float  current_yaw;

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		current_yaw = anglemod(self->s.angles[YAW]);
		if (fabsf(current_yaw - self->ideal_yaw) > 0.1f)
		{
			self->monsterinfo.aiflags |= AI_HOLD_FRAME;
			return;
		}
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
	}

	AngleVectors(self->s.angles, f, r, nullptr);

	for (size_t i = 0; i < MAX_REINFORCEMENTS; i++) {
		if (self->monsterinfo.chosen_reinforcements[i] == 255)
			break;
		num_summoned++;
	}

	for (count = 0; count < num_summoned; count++)
	{
		offset = reinforcement_position[count];
		startpoint = M_ProjectFlashSource(self, offset, f, r);
		startpoint[2] += 10 * (self->s.scale ? self->s.scale : 1.0f);

		
		uint8_t def_index = self->monsterinfo.chosen_reinforcements[count];
		if (def_index >= self->monsterinfo.reinforcements.defs.size()) continue;

		const auto& reinforcement_def = self->monsterinfo.reinforcements.defs[def_index];
		horde::MonsterTypeID typeId = reinforcement_def.typeId;
		vec3_t mins, maxs;
		GetPredictedScaledBounds(typeId, mins, maxs);
		

		if (FindSpawnPoint(startpoint, mins, maxs, spawnpoint, 32))
		{
			if (CheckGroundSpawnPoint(spawnpoint, mins, maxs, 256, -1))
			{
				num_success++;
				// Don't show SpawnGrow here - only show it when monster is actually created
				// float const radius = (maxs - mins).length() * 0.5f;
				// SpawnGrow_Spawn(spawnpoint + (mins + maxs), radius, radius * 2.f);
			}
		}
	}

	if (num_success == 0)
		self->monsterinfo.nextframe = FRAME_attack53;
}

void medic_finish_spawn(edict_t* self)
{
	edict_t* ent;
	vec3_t   f, r, offset, startpoint, spawnpoint;
	size_t   num_summoned = 0;
	edict_t* designated_enemy;

	// FIX: Determine the initial target safely. If the medic is healing, the target is
	// the 'oldenemy'. Otherwise, it's the current 'enemy'. We must validate this pointer
	// before proceeding, as it could have become invalid during the spawn animation.
	if (self->monsterinfo.aiflags & AI_MEDIC)
		designated_enemy = self->oldenemy;
	else
		designated_enemy = self->enemy;

	// If the designated enemy is no longer valid, we can't assign it to the minions.
	// Set it to nullptr so they acquire targets on their own.
	if (!designated_enemy || !designated_enemy->inuse || designated_enemy->health <= 0)
	{
		designated_enemy = nullptr;
	}
	// END FIX

	AngleVectors(self->s.angles, f, r, nullptr);

	for (size_t i = 0; i < MAX_REINFORCEMENTS; i++)
	{
		if (self->monsterinfo.chosen_reinforcements[i] == 255) {
			break;
		}
		num_summoned++;
	}

	for (size_t count_idx = 0; count_idx < num_summoned; count_idx++)
	{
		uint8_t def_index = self->monsterinfo.chosen_reinforcements[count_idx];
		if (def_index >= self->monsterinfo.reinforcements.defs.size()) {
			continue;
		}

		const auto& reinforcement_def = self->monsterinfo.reinforcements.defs[def_index];
		horde::MonsterTypeID typeId = reinforcement_def.typeId;
		const char* classname = horde::MonsterTypeRegistry::GetClassname(typeId);
		if (!classname) {
			continue;
		}

		vec3_t mins, maxs;
		GetPredictedScaledBounds(typeId, mins, maxs);

		offset = reinforcement_position[count_idx];
		startpoint = M_ProjectFlashSource(self, offset, f, r);
		startpoint[2] += 10 * (self->s.scale ? self->s.scale : 1.0f);

		ent = nullptr;
		if (FindSpawnPoint(startpoint, mins, maxs, spawnpoint, 32))
		{
			if (CheckGroundSpawnPoint(spawnpoint, mins, maxs, 256, -1))
			{
				ent = CreateGroundMonster(spawnpoint, self->s.angles, mins, maxs, classname, 256);
			}
		}

		if (!ent)
			continue;

		// Show SpawnGrow effect now that monster is actually created
		float const radius = (maxs - mins).length() * 0.5f;
		SpawnGrow_Spawn(ent->s.origin, radius, radius * 2.f);

		if (ent->think)
		{
			ent->nextthink = level.time;
			ent->think(ent);
		}

		ent->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT | AI_SPAWNED_COMMANDER | AI_SPAWNED_NEEDS_GIB;
		ent->monsterinfo.commander = self;
		ent->monsterinfo.slots_from_commander = reinforcement_def.strength;
		self->monsterinfo.monster_used += reinforcement_def.strength;

		if (g_horde && g_horde->integer) {
			if (brandom()) {
				ent->item = G_HordePickItem();
			}
			else {
				ent->item = nullptr;
			}
		}
		else {
			ent->item = nullptr;
		}

		ApplyMonsterBonusFlags(ent);

		// FIX: Use a temporary variable for coop target selection to avoid overwriting
		// the validated 'designated_enemy' unless a valid coop target is found.
		edict_t* final_target = designated_enemy;
		if (coop && coop->integer)
		{
			edict_t* coop_target = PickCoopTarget(ent);
			if (coop_target && coop_target != self->enemy)
			{
				final_target = coop_target;
			}
		}
		// END FIX

		if (final_target) // This check is now sufficient as the pointer has been validated
		{
			ent->enemy = final_target;
			FoundTarget(ent);
		}
		else
		{
			ent->enemy = nullptr;
			if (ent->monsterinfo.stand)
				ent->monsterinfo.stand(ent);
		}
	}
}

mframe_t medic_frames_callReinforcements[] = {
	// ROGUE - 33-36 now ai_charge
	{ ai_charge, 2 }, // 33
	{ ai_charge, 3 },
	{ ai_charge, 5 },
	{ ai_charge, 4.4f }, // 36
	{ ai_charge, 4.7f },
	{ ai_charge, 5 },
	{ ai_charge, 6 },
	{ ai_charge, 4 }, // 40
	{ ai_charge, 0, monster_footstep },
	{ ai_move, 0, medic_start_spawn }, // 42
	{ ai_move },					   // 43 -- 43 through 47 are skipped
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, medic_determine_spawn }, // 48
	{ ai_charge, 0, medic_spawngrows },	   // 49
	{ ai_move },						   // 50
	{ ai_move },						   // 51
	{ ai_move, -15, medic_finish_spawn },  // 52
	{ ai_move, -1.5f },
	{ ai_move, -1.2f },
	{ ai_move, -3, monster_footstep }
};
MMOVE_T(medic_move_callReinforcements) = { FRAME_attack33, FRAME_attack55, medic_frames_callReinforcements, medic_run };



//// Add this function to check if a tesla mine can be converted
//bool tesla_check_conversion(edict_t* tesla, edict_t* converter)
//{
//	// Must be active tesla mine
//	if (!tesla || !tesla->inuse || strcmp(tesla->classname, "tesla_mine") != 0)
//		return false;
//
//	// Must be on different teams
//	if (tesla->team == converter->team)
//		return false;
//
//	// Must be in range and visible
//	if (!visible(converter, tesla))
//		return false;
//
//	float dist = (tesla->s.origin - converter->s.origin).length();
//	if (dist > MEDIC_MAX_HEAL_DISTANCE)
//		return false;
//
//	return true;
//}
//
//// Add this function to convert a tesla mine to the medic's team
//void tesla_convert(edict_t* tesla, edict_t* converter)
//{
//	// Change team
//	tesla->team = converter->team;
//
//	// Update visuals to match new team
//	tesla->s.effects &= ~(EF_COLOR_SHELL);
//	tesla->s.renderfx &= ~(RF_SHELL_RED | RF_SHELL_BLUE);
//	tesla->s.effects |= EF_COLOR_SHELL;
//	tesla->s.renderfx |= (converter->team == TEAM1) ? RF_SHELL_RED : RF_SHELL_BLUE;
//
//	// Reset owner
//	tesla->owner = converter;
//}


MONSTERINFO_ATTACK(medic_attack) (edict_t *self) -> void
{
	monster_done_dodge(self);

	float enemy_range = range_to(self, self->enemy);

	// signal from checkattack to spawn
	if (self->monsterinfo.aiflags & AI_BLOCKED)
	{
		M_SetAnimation(self, &medic_move_callReinforcements);
		self->monsterinfo.aiflags &= ~AI_BLOCKED;
	}

	float r = frandom();
	if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		if ((self->mass > 400) && (r > 0.8f) && M_SlotsLeft(self))
			M_SetAnimation(self, &medic_move_callReinforcements);
		else
			M_SetAnimation(self, &medic_move_attackCable);
	}
	else
	{
		if (self->monsterinfo.attack_state == AS_BLIND)
		{
			M_SetAnimation(self, &medic_move_callReinforcements);
			return;
		}
		if ((self->mass > 400) && (r > 0.2f) && (enemy_range > RANGE_MELEE) && M_SlotsLeft(self))
			M_SetAnimation(self, &medic_move_callReinforcements);
		else
			M_SetAnimation(self, &medic_move_attackBlaster);
	}
}

MONSTERINFO_CHECKATTACK(medic_checkattack) (edict_t* self) -> bool
{
	// In horde mode, check for dead monsters to resurrect even during combat
	if (g_horde->integer && !(self->monsterinfo.aiflags & AI_MEDIC))
	{
		edict_t* dead = medic_FindDeadMonster(self);
		if (dead && dead->health <= 0)
		{
			// Found a dead monster - switch to resurrection immediately
			self->oldenemy = self->enemy;
			self->enemy = dead;
			self->enemy->monsterinfo.healer = self;
			self->monsterinfo.aiflags |= AI_MEDIC;
			self->timestamp = level.time + MEDIC_TRY_TIME;
			medic_attack(self);
			return true;
		}
	}

	if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		// if our target went away
		if ((!self->enemy) || (!self->enemy->inuse))
		{
			abortHeal(self, false, false);
			return false;
		}

		// For resurrection, be more patient
		if (self->enemy->health <= 0)
		{
			// Don't timeout resurrections as quickly
			if (self->timestamp < level.time - 5_sec)  // Give more time for resurrection
			{
				abortHeal(self, false, true);
				self->timestamp = 0_ms;
				return false;
			}
		}
		else
		{
			// Normal timeout for healing living targets
			if (self->timestamp < level.time)
			{
				abortHeal(self, false, true);
				self->timestamp = 0_ms;
				return false;
			}
		}

		if (realrange(self, self->enemy) < MEDIC_MAX_HEAL_DISTANCE + 10)
		{
			medic_attack(self);
			return true;
		}
		else
		{
			self->monsterinfo.attack_state = AS_STRAIGHT;
			return false;
		}
	}

	if (self->enemy->client && !visible(self, self->enemy) && M_SlotsLeft(self))
	{
		self->monsterinfo.attack_state = AS_BLIND;
		return true;
	}

	// give a LARGE bias to spawning things when we have room
	// use AI_BLOCKED as a signal to attack to spawn
	if (self->monsterinfo.monster_slots && (frandom() < 0.8f) && (M_SlotsLeft(self) > self->monsterinfo.monster_slots * 0.8f) && (realrange(self, self->enemy) > 150))
	{
		self->monsterinfo.aiflags |= AI_BLOCKED;
		self->monsterinfo.attack_state = AS_MISSILE;
		return true;
	}

	// ROGUE
	// since his idle animation looks kinda bad in combat, always attack
	// when he's on a combat point
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.attack_state = AS_MISSILE;
		return true;
	}

	return M_CheckAttack(self);
}

void MedicCommanderCache()
{
	gi.modelindex("models/items/spawngro3/tris.md2");
}

MONSTERINFO_DUCK(medic_duck) (edict_t* self, gtime_t eta) -> bool
{
	//	don't dodge if you're healing
	if (self->monsterinfo.aiflags & AI_MEDIC)
		return false;

	if ((self->monsterinfo.active_move == &medic_move_attackHyperBlaster) ||
		(self->monsterinfo.active_move == &medic_move_attackCable) ||
		(self->monsterinfo.active_move == &medic_move_attackBlaster) ||
		(self->monsterinfo.active_move == &medic_move_callReinforcements))
	{
		// he ignores skill
		self->monsterinfo.unduck(self);
		return false;
	}

	M_SetAnimation(self, &medic_move_duck);

	return true;
}

MONSTERINFO_SIDESTEP(medic_sidestep) (edict_t* self) -> bool
{
	if ((self->monsterinfo.active_move == &medic_move_attackHyperBlaster) ||
		(self->monsterinfo.active_move == &medic_move_attackCable) ||
		(self->monsterinfo.active_move == &medic_move_attackBlaster) ||
		(self->monsterinfo.active_move == &medic_move_callReinforcements))
	{
		// if we're shooting, don't dodge
		return false;
	}

	if (self->monsterinfo.active_move != &medic_move_run)
		M_SetAnimation(self, &medic_move_run);

	return true;
}

//===========
// PGM
MONSTERINFO_BLOCKED(medic_blocked) (edict_t* self, float dist) -> bool
{
	if (blocked_checkplat(self, dist))
		return true;

	return false;
}
// PGM
//===========

/*QUAKED monster_medic_commander (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 */
 /*QUAKED monster_medic (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 model="models/monsters/medic/tris.md2"
 */
void SP_monster_medic(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

    if (self->monsterinfo.monster_type_id == MONSTER_TYPE_UNKNOWN) { // Check if it hasn't been set yet
        self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::MEDIC);
    }

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/medic/tris.md2");

	gi.modelindex("models/monsters/medic/gibs/chest.md2");
	gi.modelindex("models/monsters/medic/gibs/gun.md2");
	gi.modelindex("models/monsters/medic/gibs/head.md2");
	gi.modelindex("models/monsters/medic/gibs/hook.md2");
	gi.modelindex("models/monsters/medic/gibs/leg.md2");

	self->mins = { -24, -24, -24 };
	self->maxs = { 24, 24, 32 };

	// PMM
	if (horde::IsMonsterType(self, horde::MonsterTypeID::MEDIC_COMMANDER))
	{
		self->health = 600 * st.health_multiplier;
		self->gib_health = -130;
		self->mass = 600;
		self->yaw_speed = 40; // default is 20
		MedicCommanderCache();
	}
	else
	{
		// PMM
		self->health = 300 * st.health_multiplier;
		self->gib_health = -130;
		self->mass = 400;
	}

	self->pain = medic_pain;
	self->die = medic_die;

	self->monsterinfo.stand = medic_stand;
	self->monsterinfo.walk = medic_walk;
	self->monsterinfo.run = medic_run;
	// pmm
	self->monsterinfo.dodge = M_MonsterDodge;
	self->monsterinfo.duck = medic_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = medic_sidestep;
	self->monsterinfo.blocked = medic_blocked;
	// pmm
	self->monsterinfo.attack = medic_attack;
	self->monsterinfo.melee = nullptr;
	self->monsterinfo.sight = medic_sight;
	self->monsterinfo.idle = medic_idle;
	self->monsterinfo.search = medic_search;
	self->monsterinfo.checkattack = medic_checkattack;
	self->monsterinfo.setskin = medic_setskin;

	gi.linkentity(self);

	M_SetAnimation(self, &medic_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start(self);

	// PMM
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	if (self->mass > 400)
	{
		self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::MEDIC_COMMANDER);
    
		self->s.skinnum = 2;

		// commander sounds
		commander_sound_idle1.assign("medic_commander/medidle.wav");
		commander_sound_pain1.assign("medic_commander/medpain1.wav");
		commander_sound_pain2.assign("medic_commander/medpain2.wav");
		commander_sound_die.assign("medic_commander/meddeth.wav");
		commander_sound_sight.assign("medic_commander/medsght.wav");
		commander_sound_search.assign("medic_commander/medsrch.wav");
		commander_sound_hook_launch.assign("medic_commander/medatck2c.wav");
		commander_sound_hook_hit.assign("medic_commander/medatck3a.wav");
		commander_sound_hook_heal.assign("medic_commander/medatck4a.wav");
		commander_sound_hook_retract.assign("medic_commander/medatck5a.wav");
		commander_sound_spawn.assign("medic_commander/monsterspawn1.wav");
		gi.soundindex("tank/tnkatck3.wav");

		// --- MODIFIED REINFORCEMENT SETUP ---
		if (!st.was_key_specified("monster_slots"))
			self->monsterinfo.monster_slots = commander_monster_slots_base;

		if (self->monsterinfo.monster_slots)
		{
			if (skill->integer)
				self->monsterinfo.monster_slots += floor(self->monsterinfo.monster_slots * (skill->value / 2.f));

			// Pass the constexpr array directly to the setup function
			M_SetupReinforcements(commander_reinforcements_defs, self->monsterinfo.reinforcements);
		}
		// --- END MODIFICATION ---
	}
	else
	{
		sound_idle1.assign("medic/idle.wav");
		sound_pain1.assign("medic/medpain1.wav");
		sound_pain2.assign("medic/medpain2.wav");
		sound_die.assign("medic/meddeth1.wav");
		sound_sight.assign("medic/medsght1.wav");
		sound_search.assign("medic/medsrch1.wav");
		sound_hook_launch.assign("medic/medatck2.wav");
		sound_hook_hit.assign("medic/medatck3.wav");
		sound_hook_heal.assign("medic/medatck4.wav");
		sound_hook_retract.assign("medic/medatck5.wav");
		gi.soundindex("medic/medatck1.wav");

		self->s.skinnum = 0;

		// // --- MODIFIED REINFORCEMENT SETUP for bonus medics ---
		// if (self->monsterinfo.bonus_flags != BF_NONE) {
		// 	if (!st.was_key_specified("monster_slots"))
		// 		self->monsterinfo.monster_slots = default_monster_slots_base;

		// 	if (self->monsterinfo.monster_slots)
		// 	{
		// 		if (skill->integer)
		// 			self->monsterinfo.monster_slots += floor(self->monsterinfo.monster_slots * (skill->value / 2.f));

		// 		// Pass the constexpr array directly to the setup function
		// 		M_SetupReinforcements(default_reinforcements_defs, self->monsterinfo.reinforcements);
		// 	}
		// }
			}
	// pmm

	ApplyMonsterBonusFlags(self);
}
