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
#include "horde/g_horde_scaling.h"
#include "monster_constants.h"
#include <boost/container/small_vector.hpp>

// Add these prototypes near the top
// bool tesla_check_conversion(edict_t* tesla, edict_t* converter);
// void tesla_convert(edict_t* tesla, edict_t* converter);

constexpr float MEDIC_MIN_DISTANCE = 32;
constexpr float MEDIC_MAX_HEAL_DISTANCE = 400;
constexpr gtime_t MEDIC_TRY_TIME = 3_sec; // Reduced from 10 seconds for more active healing in horde mode

// FIXME -
//
// owner moved to monsterinfo.healer instead
//
// For some reason, the healed monsters are rarely ending up in the floor
//
// 5/15/1998 I think I fixed these, keep an eye on them

void M_SetEffects(edict_t *ent);
bool FindTarget(edict_t *self);
void FoundTarget(edict_t *self);

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

constexpr std::array<reinforcement_def_t, 6> commander_reinforcements_defs = {{{horde::MonsterTypeID::GUNNER_VANILLA, 1}, {horde::MonsterTypeID::GUNNER_VANILLA, 2}, {horde::MonsterTypeID::JANITOR2, 3}, {horde::MonsterTypeID::INFANTRY, 3}, {horde::MonsterTypeID::GUNNER, 4}, {horde::MonsterTypeID::GLADIATOR, 6}}};

// NEW: Compile-time reinforcement definitions for the standard (bonus) Medic.
constexpr std::array<reinforcement_def_t, 3> default_reinforcements_defs = {{{horde::MonsterTypeID::GUNNER_VANILLA, 1}, {horde::MonsterTypeID::GLADIATOR, 1}, {horde::MonsterTypeID::GLADIATOR_B, 1}}};

constexpr int32_t commander_monster_slots_base = 3;
constexpr int32_t default_monster_slots_base = 2;

static const float inverse_log_slots = pow(2, MAX_REINFORCEMENTS);

constexpr std::array<vec3_t, MAX_REINFORCEMENTS> reinforcement_position = {
	vec3_t{80, 0, 0},
	vec3_t{40, 60, 0},
	vec3_t{40, -60, 0},
	vec3_t{0, 80, 0},
	vec3_t{0, -80, 0}};

// filter out the reinforcement indices we can pick given the space we have left
template<typename Container>
static void M_PickValidReinforcements(edict_t *self, int32_t space, Container &output)
{
	output.clear();

	// Use the new std::span from the reinforcement_list_t
	for (uint8_t i = 0; i < self->monsterinfo.reinforcements.defs.size(); i++)
		if (self->monsterinfo.reinforcements.defs[i].strength <= space)
			output.push_back(i);
}

// pick an array of reinforcements to use; note that this does not modify `self`
std::array<uint8_t, MAX_REINFORCEMENTS> M_PickReinforcements(edict_t *self, int32_t &num_chosen, int32_t max_slots)
{
	boost::container::small_vector<uint8_t, 64> available;
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

void M_SetupReinforcements(std::span<const reinforcement_def_t> defs, reinforcement_list_t &list)
{
	list.defs = defs;
}

extern const mmove_t medic_move_stand;
extern const mmove_t medic_move_walk;

void fixHealerEnemy(edict_t *self)
{
	if (self->oldenemy && self->oldenemy->inuse && self->oldenemy->health > 0)
	{
		self->enemy = self->oldenemy;
		self->oldenemy = nullptr;
		HuntTarget(self, true); // Use animate_state=true to trigger run
	}
	else
	{
		self->enemy = self->goalentity = nullptr;
		self->oldenemy = nullptr;
		if (!FindTarget(self))
		{
			// No enemy found, return to patrol/walk mode
			// Force proper animation transition to prevent stuck state
			// if (self->monsterinfo.walk)
			// {
			// 	M_SetAnimation(self, &medic_move_walk); // Fixed: was incorrectly calling stand
			// }
			// else if (self->monsterinfo.stand)
			{
				M_SetAnimation(self, &medic_move_stand);
			}
		}
		// else FindTarget already called FoundTarget which sets run mode
	}
}

void cleanupHeal(edict_t *self)
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

void abortHeal(edict_t *self, bool gib, bool mark)
{
	int hurt;
	constexpr vec3_t pain_normal = {0, 0, 1};

	if (self->enemy && self->enemy->inuse && !self->enemy->client && (self->monsterinfo.aiflags & AI_MEDIC))
	{
		cleanupHealTarget(self->enemy);
		// Clear AI_STAND_GROUND flag if we set it during healing
		self->enemy->monsterinfo.aiflags &= ~AI_STAND_GROUND;

		// gib em!
		if (mark)
		{
			edict_t *medic = self->enemy->monsterinfo.badMedic1;

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

// Check if summoned medic can resurrect (respects player's summon limit)
static bool Medic_CanResurrect(edict_t *medic)
{
	// Non-summoned medics can always resurrect
	if (!medic->monsterinfo.isfriendlyspawn)
		return true;

	// Need valid owner
	if (!medic->teammaster || !medic->teammaster->client)
		return false;

	// Check current summons using player array
	int total_summons = medic->teammaster->client->resp.num_summons;

	return total_summons < SummonConstants::MAX_SUMMONS_PER_PLAYER();
}

bool finishHeal(edict_t *self)
{
	// Initial null check before any operations
	if (!self || !self->enemy)
	{
		return false;
	}

	edict_t *healee = self->enemy;

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
	if (isBodyque)
	{
		if (!healee->s.origin || !healee->s.angles)
		{
			abortHeal(self, false, false);
			return false;
		}

		vec3_t const position = healee->s.origin;
		vec3_t angles = healee->s.angles;
		angles[PITCH] = 0;
		angles[ROLL] = 0;

		edict_t *insane = G_Spawn();
		if (!insane)
		{
			abortHeal(self, false, false);
			return false;
		}

		insane->s.origin = position;
		insane->s.angles = angles;
		insane->classname = (frandom() > 0.6f) ? "monster_chick_heat" : "monster_brain";

		if (g_horde->integer)
		{
			insane->item = brandom() ? G_HordePickItem() : nullptr;
		}

		spawn_temp_t st{};
		ED_CallSpawn(insane, st);

		if (!insane->inuse)
		{
			G_FreeEdict(insane);
			abortHeal(self, false, false);
			return false;
		}

		// Clean up original healee
		if (healee)
		{
			gi.sound(healee, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);
			ThrowGibs(healee, 50, {{2, "models/objects/gibs/bone/tris.md2"}, {4, "models/objects/gibs/sm_meat/tris.md2"}, {"models/objects/gibs/head2/tris.md2", GIB_HEAD}});

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
	if (insaneDead)
	{
		vec3_t const position = healee->s.origin;
		vec3_t angles = healee->s.angles;
		angles[PITCH] = 0;
		angles[ROLL] = 0;

		edict_t *insane = G_Spawn();
		if (!insane)
		{
			abortHeal(self, false, false);
			return false;
		}

		insane->s.origin = position;
		insane->s.angles = angles;
		insane->classname = "monster_soldier_lasergun";

		if (g_horde->integer)
		{
			insane->item = brandom() ? G_HordePickItem() : nullptr;
		}

		spawn_temp_t st{};
		ED_CallSpawn(insane, st);

		if (!insane->inuse)
		{
			G_FreeEdict(insane);
			abortHeal(self, false, false);
			return false;
		}

		// Clean up original healee
		if (healee)
		{
			gi.sound(healee, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);
			ThrowGibs(healee, 50, {{2, "models/objects/gibs/bone/tris.md2"}, {4, "models/objects/gibs/sm_meat/tris.md2"}, {"models/objects/gibs/head2/tris.md2", GIB_HEAD}});

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
	if (!healee || !healee->inuse)
	{
		abortHeal(self, false, false);
		return false;
	}

	// Check spatial constraints
	vec3_t maxs = healee->maxs;
	maxs[2] += 48;
	trace_t const tr = gi.trace(healee->s.origin, healee->mins, maxs, healee->s.origin, healee, MASK_MONSTERSOLID);

	if (tr.startsolid || tr.allsolid || tr.ent != world)
	{
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
	healee->health = old_max_health / 3;							   // Resurrect with 1/3 health
	healee->monsterinfo.power_armor_power = old_power_armor_power / 3; // Also 1/3 armor
	healee->monsterinfo.max_power_armor_power = old_power_armor_power;
	healee->monsterinfo.power_armor_type = healee->monsterinfo.initial_power_armor_type = old_power_armor_type;
	healee->monsterinfo.base_health = old_base_health;
	healee->monsterinfo.health_scaling = old_health_scaling;

	// Apply visual updates
	if (healee->monsterinfo.setskin)
	{
		healee->monsterinfo.setskin(healee);
	}

	// Initialize AI state
	if (healee->think)
	{
		healee->nextthink = level.time;
		healee->think(healee);
	}

	// Update AI flags
	healee->monsterinfo.aiflags &= ~AI_RESURRECTING;
	healee->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT;
	healee->s.effects &= ~EF_FLIES;
	healee->monsterinfo.healer = nullptr;

	// Set revived entity's team to match the medic's team
	healee->ctf_team = self->ctf_team;
	if (healee->svflags & SVF_MONSTER)
	{
		healee->monsterinfo.team = static_cast<uint8_t>(self->ctf_team);

		// If the medic is summoned, the resurrected monster should inherit summoner properties
		if (self->monsterinfo.isfriendlyspawn && self->teammaster)
		{
			// Safety check - summoned medic MUST have a chain (player reference)
			if (!self->chain || !self->chain->client)
			{
				// This shouldn't happen, but if it does, abort resurrection
				abortHeal(self, false, false);
				return false;
			}

			// Check if player has room for another summon
			if (self->chain->client->resp.num_summons >= SummonConstants::MAX_SUMMONS_PER_PLAYER())
			{
				// At max summons, abort resurrection
				abortHeal(self, false, false);
				return false;
			}

			// Inherit summoned properties from the medic
			healee->monsterinfo.isfriendlyspawn = true;
			healee->monsterinfo.issummoned = true; // Part of Strogg summoner system
			healee->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
			healee->monsterinfo.bonus_flags |= BF_FRIENDLY;

			// Set chain and teammaster references (direct to player)
			healee->chain = self->chain;		   // Direct reference to player
			healee->teammaster = self->teammaster; // Point to player owner
			healee->touch = strogg_summoned_touch; // Always set touch to allow owner to push

			// Inherit PvM level from the summoned medic
			// This ensures revived monsters scale with the player's monster_summon skill
			healee->monsterinfo.pvm_level = self->monsterinfo.pvm_level;

			// Initialize upkeep timer for revived monster (1 cube per second asynchronously)
			healee->monsterinfo.upkeep_time = level.time + 1_sec;

			// Ensure proper collision for summoned monsters
			healee->svflags &= ~SVF_PLAYER;
			healee->svflags |= SVF_MONSTER;
			healee->solid = SOLID_BBOX;
			healee->clipmask = MASK_MONSTERSOLID;

			// Re-link entity to ensure touch function and collision are applied
			gi.linkentity(healee);

			// Add to player's tracking array
			bool added = false;
			for (int i = 0; i < SummonConstants::MAX_SUMMONS_ARRAY_SIZE; i++)
			{
				if (!self->chain->client->resp.deployed_summons[i] || !self->chain->client->resp.deployed_summons[i]->inuse)
				{
					self->chain->client->resp.deployed_summons[i] = healee;
					added = true;
					break;
				}
			}

			if (added)
			{
				self->chain->client->resp.num_summons++;
			}
		}
	}

	// Handle targeting - for horde mode, immediately find appropriate target
	if (g_horde->integer)
	{
		// Clear old enemies
		healee->enemy = nullptr;
		healee->oldenemy = nullptr;

		// For summoned/friendly monsters, immediately look for monster targets
		if (healee->monsterinfo.isfriendlyspawn || (healee->monsterinfo.bonus_flags & BF_FRIENDLY))
		{
			// Use FindMTarget to find monster enemies
			if (!FindMTarget(healee))
			{
				// **FIX: Don't set pausetime - let natural AI handle it**
				// **OLD:** healee->monsterinfo.pausetime = level.time + 0.5_sec;
				if (healee->monsterinfo.stand)
				{
					healee->monsterinfo.stand(healee);
				}
			}
			// If FindMTarget found something, it will have called FoundTarget
		}
		else
		{
			// Regular horde monsters should target players
			// **FIX: Don't set pausetime - let natural AI handle it**
			// **OLD:** healee->monsterinfo.pausetime = level.time + 0.5_sec;
			if (healee->monsterinfo.stand)
			{
				healee->monsterinfo.stand(healee);
			}
		}
	}
	else
	{
		// Non-horde behavior
		edict_t *new_enemy = self->enemy;
		healee->oldenemy = nullptr;
		healee->enemy = new_enemy;

		if (new_enemy && healee->inuse)
		{
			FoundTarget(healee);
		}
		else
		{
			healee->enemy = nullptr;
			if (healee->inuse && !FindTarget(healee))
			{
				if (healee->inuse)
				{
					// **FIX: Much shorter pausetime to prevent loops**
					// **OLD:** healee->monsterinfo.pausetime = level.time + 1_sec;
					healee->monsterinfo.pausetime = level.time + 0.1_sec;

					if (healee->monsterinfo.stand)
					{
						healee->monsterinfo.stand(healee);
					}
				}
			}
		}
	}

	// Update final state
	healee->monsterinfo.react_to_damage_time = level.time;
	healee->monsterinfo.was_stuck = false;

	// Mark that resurrection completed successfully
	if (self && self->inuse && healee && healee->health > 0)
	{
		self->monsterinfo.last_resurrection_time = level.time;

		// Notify owner for summoned medics
		if (self->monsterinfo.isfriendlyspawn && self->teammaster && self->teammaster->client)
		{
			// Count current Strogg summons (both spawned and revived)
			int summon_count = self->teammaster->client->resp.num_summons;

			// Get monster name
			const char *monster_name = "monster";
			auto monster_type = static_cast<horde::MonsterTypeID>(healee->monsterinfo.monster_type_id);
			if (monster_type != horde::MonsterTypeID::UNKNOWN)
			{
				const char *classname = horde::MonsterTypeRegistry::GetClassname(monster_type);
				if (classname && strncmp(classname, "monster_", 8) == 0)
				{
					monster_name = classname + 8; // Skip "monster_" prefix
				}
			}

			// Notify the owner with total summon count
			gi.LocClient_Print(self->teammaster, PRINT_HIGH,
							   "Medic resurrected {}! ({}/{})\n",
							   monster_name, summon_count, SummonConstants::MAX_SUMMONS_PER_PLAYER());
		}
	}

	// Don't call cleanupHeal() - we want to keep the resurrected monster as enemy to continue healing
	// cleanupHeal(self);
	return true;
}

bool canReach(edict_t *self, edict_t *other)
{
	vec3_t spot1;
	vec3_t spot2;
	trace_t trace;

	spot1 = self->s.origin;
	spot1[2] += self->viewheight;
	spot2 = other->s.origin;
	spot2[2] += other->viewheight;
	trace = gi.traceline(spot1, spot2, self, MASK_PROJECTILE | MASK_WATER);
	return trace.fraction == 1.0f || trace.ent == other;
}

edict_t *healFindMonster(edict_t *self, float radius)
{
	edict_t *ent = nullptr;
	edict_t *best_dead = nullptr;
	edict_t *best_injured_teammate = nullptr;

	while ((ent = findradius(ent, self->s.origin, radius)) != nullptr)
	{
		if (ent == self)
			continue;
		// Check for monsters, bodyque entities, and players
		if (!(ent->svflags & SVF_MONSTER) && strcmp(ent->classname, "bodyque") != 0 && !ent->client)
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
		// Skip player_noise entities but allow actual players
		if (!strcmp(ent->classname, "player_noise"))
			continue;

		// Check for injured entities (alive but hurt)
		if (ent->health > 0 && ent->health < ent->max_health)
		{
			// Determine if this is a valid heal target
			bool can_heal = false;

			if (g_horde->integer)
			{
				// In horde mode, friendly medics heal players and friendly monsters
				if (self->monsterinfo.isfriendlyspawn || (self->monsterinfo.bonus_flags & BF_FRIENDLY))
				{
					// Heal players on the same team as the medic
					// FIX: Removed incorrect SVF_PLAYER check - players are identified by ent->client
					if (ent && ent->client && GetEntityTeam(ent) == GetEntityTeam(self))
						can_heal = true;
					// Heal friendly monsters
					else if ((ent->svflags & SVF_MONSTER) && OnSameTeam(self, ent))
						can_heal = true;
				}
				else
				{
					// Enemy medics only heal enemy monsters
					if ((ent->svflags & SVF_MONSTER) && OnSameTeam(self, ent))
						can_heal = true;
				}
			}
			else
			{
				// Non-horde mode - use OnSameTeam
				can_heal = OnSameTeam(self, ent);
			}

			if (can_heal)
			{
				if (!best_injured_teammate || ent->health < best_injured_teammate->health)
				{
					best_injured_teammate = ent;
				}
			}
			continue; // Don't consider alive entities for revival
		}

		// Dead entity handling (for revival) - revive ANY dead corpse
		if (ent->health > 0)
			continue;
		if ((ent->nextthink) && (ent->think != monster_dead_think))
			continue;

		// Skip gibbed monsters - they cannot be resurrected
		if (ent->gib_health && ent->health < ent->gib_health)
			continue;

		// For dead entities, pick the best one regardless of team (we'll assign them to our team)
		if (!best_dead || ent->max_health > best_dead->max_health)
		{
			best_dead = ent;
		}
	}

	// Priority order:
	// 1. Dead corpses (always prioritize resurrection - they'll join our team)
	// 2. Injured teammates (for healing)
	if (best_dead)
		return best_dead;

	return best_injured_teammate;
}

bool M_NeedRegen(edict_t *target);
// Check for healing opportunities during movement
void medic_check_heal(edict_t *self)
{
	// **CORE FIX: Respect resurrection cooldown**
	// This closes the loophole that allowed walking medics to re-trigger the loop.
	if (level.time - self->monsterinfo.last_resurrection_time < 1_sec)
		return;

	// Don't check if already healing
	if (self->monsterinfo.aiflags & AI_MEDIC)
		return;

	// Be more aggressive about healing - check even if we have an enemy
	// but prioritize based on distance and threat level
	bool has_active_enemy = (self->enemy && self->enemy->inuse && self->enemy->health > 0);
	float enemy_distance = has_active_enemy ? realrange(self, self->enemy) : 999999.0f;

	// Look for healing opportunities with larger radius
	float radius = MEDIC_MAX_HEAL_DISTANCE * 1.5f; // Increased search radius
	edict_t *ent = healFindMonster(self, radius);

	if (ent)
	{
		// Check if this is a high-priority target
		bool is_dead = (ent->health <= 0);
		bool is_critical = (ent->health > 0 && ent->health < ent->max_health * 0.3f); // Less than 30% health
		bool is_hurt = (ent->health > 0 && ent->health < ent->max_health * 0.75f);	  // Less than 75% health

		// Check if it's a player that needs healing
		bool is_player = (ent->client != nullptr);

		// Calculate distances
		float heal_distance = realrange(self, ent);

		// Be more selective about interrupting current activity
		bool should_heal = false;

		if (g_horde->integer)
		{
			// More aggressive healing in horde mode
			if (is_dead && heal_distance < MEDIC_MAX_HEAL_DISTANCE)
			{
				// Always prioritize resurrection if in range
				should_heal = true;
			}
			else if (is_player && (is_hurt || M_NeedRegen(ent)) && heal_distance < MEDIC_MAX_HEAL_DISTANCE)
			{
				// Always prioritize healing players
				should_heal = true;
			}
			else if (is_critical && heal_distance < MEDIC_MAX_HEAL_DISTANCE * 0.75f)
			{
				// Heal critically injured allies if close
				should_heal = true;
			}
			else if (!has_active_enemy && is_hurt && heal_distance < MEDIC_MAX_HEAL_DISTANCE)
			{
				// If no enemy, heal any hurt ally in range
				should_heal = true;
			}
			else if (has_active_enemy && enemy_distance > MEDIC_MAX_HEAL_DISTANCE * 2.0f &&
					 (is_dead || is_critical) && heal_distance < MEDIC_MAX_HEAL_DISTANCE)
			{
				// Enemy is far, safe to heal
				should_heal = true;
			}
		}
		else
		{
			// Normal mode: be more aggressive about healing
			if (!has_active_enemy)
			{
				// No enemy, always heal if needed
				should_heal = true;
			}
			else if (is_dead && enemy_distance > MEDIC_MAX_HEAL_DISTANCE * 1.5f)
			{
				// Enemy is somewhat far, safe to revive
				should_heal = true;
			}
			else if (is_critical && enemy_distance > MEDIC_MAX_HEAL_DISTANCE)
			{
				// Heal critical allies if enemy isn't too close
				should_heal = true;
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

edict_t *medic_FindDeadMonster(edict_t *self)
{
	// **CORE FIX: Respect resurrection cooldown**
	// If we recently completed a resurrection, don't look for corpses yet.
	if (level.time - self->monsterinfo.last_resurrection_time < 0.3_sec)
		return nullptr;

	float radius;

	if (!g_horde->integer && self->monsterinfo.react_to_damage_time > level.time)
		return nullptr;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		radius = MEDIC_MAX_HEAL_DISTANCE;
	else
		radius = 1024;

	edict_t *best = healFindMonster(self, radius);

	if (best)
		self->timestamp = level.time + MEDIC_TRY_TIME;

	return best;
}

MONSTERINFO_IDLE(medic_idle)(edict_t *self)->void
{
	// **FIX: Remove the pausetime forcing during cooldown**
	// The cooldown is already handled in medic_FindDeadMonster
	// **OLD BUGGY CODE REMOVED:**
	// if (level.time - self->monsterinfo.last_resurrection_time < 1_sec)
	// {
	//     self->monsterinfo.pausetime = level.time + 1.0_sec;
	//     return;
	// }

	// PMM - commander sounds
	if (self->mass == 400)
		gi.sound(self, CHAN_VOICE, sound_idle1, 1, ATTN_IDLE, 0);
	else
		gi.sound(self, CHAN_VOICE, commander_sound_idle1, 1, ATTN_IDLE, 0);

	// Only look for healing targets if we don't have an active threat
	if (!self->enemy || !self->enemy->inuse || self->enemy->health <= 0)
	{
		edict_t *ent = medic_FindDeadMonster(self);
		if (ent && realrange(self, ent) < MEDIC_MAX_HEAL_DISTANCE * 0.75f)
		{
			// Only switch if corpse is close
			self->oldenemy = self->enemy;
			self->enemy = ent;
			self->enemy->monsterinfo.healer = self;
			self->monsterinfo.aiflags |= AI_MEDIC;
			FoundTarget(self);
		}
	}
}

MONSTERINFO_SEARCH(medic_search)(edict_t *self)->void
{
	// PMM - commander sounds
	if (self->mass == 400)
		gi.sound(self, CHAN_VOICE, sound_search, 1, ATTN_IDLE, 0);
	else
		gi.sound(self, CHAN_VOICE, commander_sound_search, 1, ATTN_IDLE, 0);

	// Only look for healing targets if we don't have an active threat
	if (!self->enemy || !self->enemy->inuse || self->enemy->health <= 0)
	{
		edict_t *ent = medic_FindDeadMonster(self);
		if (ent && realrange(self, ent) < MEDIC_MAX_HEAL_DISTANCE * 0.75f)
		{
			// Only switch if corpse is close
			self->oldenemy = self->enemy;
			self->enemy = ent;
			self->enemy->monsterinfo.healer = self;
			self->monsterinfo.aiflags |= AI_MEDIC;
			FoundTarget(self);
		}
	}
}

MONSTERINFO_SIGHT(medic_sight)(edict_t *self, edict_t *other)->void
{
	// PMM - commander sounds
	if (self->mass == 400)
		gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, commander_sound_sight, 1, ATTN_NORM, 0);
}

mframe_t medic_frames_stand[] = {
	{ai_stand, 0, medic_idle},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand},
	{ai_stand, 0, medic_check_heal},
	{ai_stand, 2, medic_check_heal},
};
MMOVE_T(medic_move_stand) = {FRAME_wait1, FRAME_wait90, medic_frames_stand, nullptr};

MONSTERINFO_STAND(medic_stand)(edict_t *self)->void
{
	// Debug output for friendly medics
	// if (self->monsterinfo.bonus_flags & BF_FRIENDLY)
	// {
	// 	gi.Com_PrintFmt("MEDIC_STAND: pausetime={:.1f} enemy={} oldenemy={} AI_MEDIC={} AI_RESURRECTING={}\n",
	// 		(self->monsterinfo.pausetime - level.time).seconds(),
	// 		self->enemy ? self->enemy->classname : "null",
	// 		self->oldenemy ? self->oldenemy->classname : "null",
	// 		(self->monsterinfo.aiflags & AI_MEDIC) ? "yes" : "no",
	// 		(self->enemy && (self->enemy->monsterinfo.aiflags & AI_RESURRECTING)) ? "yes" : "no");
	// }
	M_SetAnimation(self, &medic_move_stand);
}

mframe_t medic_frames_walk[] = {
	{ai_walk, 6.2f},
	{ai_walk, 18.1f, monster_footstep},
	{ai_walk, 1},
	{ai_walk, 9},
	{ai_walk, 10},
	{ai_walk, 9},
	{ai_walk, 11},
	{ai_walk, 11.6f, monster_footstep},
	{ai_walk, 2},
	{ai_walk, 9.9f},
	{ai_walk, 14},
	{ai_walk, 9.3f}};
MMOVE_T(medic_move_walk) = {FRAME_walk1, FRAME_walk12, medic_frames_walk, nullptr};

MONSTERINFO_WALK(medic_walk)(edict_t *self)->void
{

	// Debug output for friendly medics
	// if (self->monsterinfo.bonus_flags & BF_FRIENDLY)
	// {
	// 	gi.Com_PrintFmt("MEDIC_walk: pausetime={:.1f} enemy={} oldenemy={} AI_MEDIC={} AI_RESURRECTING={}\n",
	// 		(self->monsterinfo.pausetime - level.time).seconds(),
	// 		self->enemy ? self->enemy->classname : "null",
	// 		self->oldenemy ? self->oldenemy->classname : "null",
	// 		(self->monsterinfo.aiflags & AI_MEDIC) ? "yes" : "no",
	// 		(self->enemy && (self->enemy->monsterinfo.aiflags & AI_RESURRECTING)) ? "yes" : "no");
	// }

	// Don't look for healing if we have an active enemy threat
	if (self->enemy && self->enemy->inuse && self->enemy->health > 0)
	{
		// We have an active threat - focus on combat
		M_SetAnimation(self, &medic_move_walk);
		return;
	}

	// // Only check for healing when not in combat
	// if (!(self->monsterinfo.aiflags & AI_MEDIC))
	// {
	// 	edict_t* ent = medic_FindDeadMonster(self);
	// 	if (ent)
	// 	{
	// 		// Only switch to healing if it's a good target
	// 		bool is_teammate = OnSameTeam(self, ent);
	// 		if ((ent->health > 0 && is_teammate && ent->health < ent->max_health * 0.5f) || // Only heal if < 50% health
	// 		    (ent->health <= 0 && realrange(self, ent) < MEDIC_MAX_HEAL_DISTANCE * 0.75f)) // Only revive if close
	// 		{
	// 			self->oldenemy = self->enemy;
	// 			self->enemy = ent;
	// 			self->enemy->monsterinfo.healer = self;
	// 			self->monsterinfo.aiflags |= AI_MEDIC;
	// 			FoundTarget(self);
	// 			return;
	// 		}
	// 	}
	// }

	M_SetAnimation(self, &medic_move_walk);
}

mframe_t medic_frames_run[] = {
	{ai_run, 18},
	{ai_run, 22.5f, monster_footstep},
	{ai_run, 25.4f, monster_done_dodge},
	{ai_run, 23.4f, monster_footstep},
	{ai_run, 24},
	{ai_run, 35.6f}};
MMOVE_T(medic_move_run) = {FRAME_run1, FRAME_run6, medic_frames_run, nullptr};

MONSTERINFO_RUN(medic_run)(edict_t *self)->void
{

	// Debug output for friendly medics
	// if (self->monsterinfo.bonus_flags & BF_FRIENDLY)
	// {
	// 	gi.Com_PrintFmt("MEDIC_RUN: pausetime={:.1f} enemy={} oldenemy={} AI_MEDIC={} AI_RESURRECTING={}\n",
	// 		(self->monsterinfo.pausetime - level.time).seconds(),
	// 		self->enemy ? self->enemy->classname : "null",
	// 		self->oldenemy ? self->oldenemy->classname : "null",
	// 		(self->monsterinfo.aiflags & AI_MEDIC) ? "yes" : "no",
	// 		(self->enemy && (self->enemy->monsterinfo.aiflags & AI_RESURRECTING)) ? "yes" : "no");
	// }

	monster_done_dodge(self);

	// FIX: If we have no enemy and we're not healing, go back to stand
	// This prevents the stand->run->stand cycling when medic has no target
	if (!self->enemy && !(self->monsterinfo.aiflags & AI_MEDIC))
	{
		// Set a pausetime to prevent immediate cycling back to run
		self->monsterinfo.pausetime = level.time + 1.0_sec;
		M_SetAnimation(self, &medic_move_stand);
		return;
	}

	// Priority 1: Deal with active threats first
	if (self->enemy && self->enemy->inuse && self->enemy->health > 0)
	{
		// We have an active enemy - check if it's actually attacking us
		float enemy_distance = realrange(self, self->enemy);

		// If enemy is close and threatening, focus on combat
		if (enemy_distance < MEDIC_MAX_HEAL_DISTANCE * 1.5f)
		{
			// Enemy is close - focus on combat, not healing
			if (self->monsterinfo.aiflags & AI_STAND_GROUND)
				M_SetAnimation(self, &medic_move_stand);
			else
				M_SetAnimation(self, &medic_move_run);
			return;
		}
	}

	// Priority 2: Only look for healing when safe or no threats
	if (!(self->monsterinfo.aiflags & AI_MEDIC))
	{
		edict_t *ent = medic_FindDeadMonster(self);
		if (ent)
		{
			// Dead monster found - only resurrect if safe
			if (ent->health <= 0)
			{
				// Only resurrect if we have no enemy or enemy is far away
				if (!self->enemy || !self->enemy->inuse ||
					realrange(self, self->enemy) > MEDIC_MAX_HEAL_DISTANCE * 2.0f)
				{
					// Also check distance to corpse
					if (realrange(self, ent) < MEDIC_MAX_HEAL_DISTANCE * 0.75f)
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
			}
			// Injured teammate - heal if conditions are right
			else if (OnSameTeam(self, ent))
			{
				float heal_distance = realrange(self, ent);
				edict_t *current_enemy = self->enemy;

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
	// If stuck in medic mode trying to resurrect but at limit, clear the flag
	else
	{
		if (self->enemy && self->enemy->health <= 0 && !Medic_CanResurrect(self))
		{
			self->monsterinfo.aiflags &= ~AI_MEDIC;
			self->enemy = self->oldenemy;
			self->oldenemy = nullptr;
		}
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &medic_move_stand);
	else
		M_SetAnimation(self, &medic_move_run);
}

mframe_t medic_frames_pain1[] = {
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move}};
MMOVE_T(medic_move_pain1) = {FRAME_paina2, FRAME_paina6, medic_frames_pain1, medic_run};

mframe_t medic_frames_pain2[] = {
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move, 0, monster_footstep},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move, 0, monster_footstep}};
MMOVE_T(medic_move_pain2) = {FRAME_painb2, FRAME_painb13, medic_frames_pain2, medic_run};

PAIN(medic_pain)(edict_t *self, edict_t *other, float kick, int damage, const mod_t &mod)->void
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

MONSTERINFO_SETSKIN(medic_setskin)(edict_t *self)->void
{
	if ((self->health < (self->max_health / 2)))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

void medic_fire_blaster_bolt(edict_t *self)
{
	if (!M_HasEnemy(self))
	{
		return; // Stop immediately if the enemy is invalid.
	}

	vec3_t start;
	vec3_t forward, right;
	vec3_t end;
	vec3_t dir;
	int damage = M_BOLT_DMG(self);
	monster_muzzleflash_id_t mz;

	mz = static_cast<monster_muzzleflash_id_t>(((self->mass > 400) ? MZ2_MEDIC_HYPERBLASTER2_1 : MZ2_MEDIC_HYPERBLASTER1_1));

	AngleVectors(self->s.angles, forward, right, nullptr);
	const vec3_t &offset = monster_flash_offset[mz];
	start = M_ProjectFlashSource(self, offset, forward, right);

	if (!M_HasValidTarget(self))
		return;

	end = self->enemy->s.origin;
	end[2] += self->enemy->viewheight;
	dir = end - start;
	dir.normalize();

	int speed = M_BLASTER_BOLT_SPEED(self);
	monster_fire_blaster_bolt(self, start, dir, damage > 0 ? damage : 30, speed > 0 ? speed : 1150, mz, EF_BLUEHYPERBLASTER);
}

void medic_fire_blaster(edict_t *self)
{
	if (!M_HasEnemy(self))
	{
		return; // Stop immediately if the enemy is invalid.
	}

	vec3_t start;
	vec3_t forward, right;
	vec3_t end;
	vec3_t dir;
	effects_t effect;
	monster_muzzleflash_id_t mz;

	if ((self->s.frame == FRAME_attack9) || (self->s.frame == FRAME_attack12))
	{
		effect = EF_BLASTER;
		mz = (self->mass > 400) ? MZ2_MEDIC_BLASTER_2 : MZ2_MEDIC_BLASTER_1;
	}
	else
	{
		effect = (self->s.frame % 4) ? EF_NONE : EF_HYPERBLASTER;
		mz = static_cast<monster_muzzleflash_id_t>(((self->mass > 400) ? MZ2_MEDIC_HYPERBLASTER2_1 : MZ2_MEDIC_HYPERBLASTER1_1) + (self->s.frame - FRAME_attack19));
	}

	AngleVectors(self->s.angles, forward, right, nullptr);
	const vec3_t &offset = monster_flash_offset[mz];
	start = M_ProjectFlashSource(self, offset, forward, right);

	if (!M_HasValidTarget(self))
		return;

	end = self->enemy->s.origin;
	end[2] += self->enemy->viewheight;
	dir = end - start;
	dir.normalize();

	// Determine the actual target. If the enemy is a laser beam,
	// the real target for this animation check is its owner (the emitter).
	edict_t *target = self->enemy;
	if (horde::IsSpecialType(target, horde::SpecialEntityTypeID::LASER_BEAM))
	{
		target = target->owner;
	}

	// medic commander shoots blaster2
	if (self->mass > 400)
	{
		int damage = M_GET_DMG_OR(self, BLASTER2, 18);

		// Check if the resolved target is a deployable that warrants a special attack animation.
		// This also fixes a bug where it was checking 'self' instead of 'self->enemy' for the sentry gun.
		if (target && (horde::IsSpecialType(target, horde::SpecialEntityTypeID::TESLA_MINE) ||
				   horde::IsSpecialType(target, horde::SpecialEntityTypeID::SENTRY_GUN) ||
				   horde::IsSpecialType(target, horde::SpecialEntityTypeID::FOOD_CUBE_TRAP) ||
				   horde::IsSpecialType(target, horde::SpecialEntityTypeID::LASER_EMITTER)))
		{
			damage *= 1.5f;
		}

		int speed = M_BLASTER2_SPEED(self);
		monster_fire_blaster2(self, start, dir, damage, speed > 0 ? speed : 1000, mz, effect);
	}
	else
	{
		int damage = M_GET_DMG_OR(self, BLASTER, 15);

		// Check if the resolved target is a deployable that warrants a special attack animation.
		// This also fixes a bug where it was checking 'self' instead of 'self->enemy' for the sentry gun.
		if (target && (horde::IsSpecialType(target, horde::SpecialEntityTypeID::TESLA_MINE) ||
				   horde::IsSpecialType(target, horde::SpecialEntityTypeID::SENTRY_GUN) ||
				   horde::IsSpecialType(target, horde::SpecialEntityTypeID::FOOD_CUBE_TRAP) ||
				   horde::IsSpecialType(target, horde::SpecialEntityTypeID::LASER_EMITTER)))
		{
			damage *= 1.5f;
		}

		int speed = M_BLASTER_SPEED(self);
		monster_fire_blaster(self, start, dir, damage, speed > 0 ? speed : 1000, mz, effect);
	}
}

void medic_dead(edict_t *self)
{
	self->mins = {-16, -16, -24};
	self->maxs = {16, 16, -8};
	monster_dead(self);
}

static void medic_shrink(edict_t *self)
{
	self->maxs[2] = -2;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t medic_frames_death[] = {
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move, -18.f, monster_footstep},
	{ai_move, -10.f, medic_shrink},
	{ai_move, -6.f},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move, 0, monster_footstep},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move}};
MMOVE_T(medic_move_death) = {FRAME_death2, FRAME_death30, medic_frames_death, medic_dead};

DIE(medic_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t &point, const mod_t &mod)->void
{
	// OnEntityDeath(self);
	//  if we had a pending patient, he was already freed up in Killed

	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		ThrowGibs(self, damage, {{2, "models/objects/gibs/bone/tris.md2"}, {"models/objects/gibs/sm_meat/tris.md2"}, {"models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC}, {"models/monsters/medic/gibs/chest.md2", GIB_SKINNED}, {2, "models/monsters/medic/gibs/leg.md2", GIB_SKINNED | GIB_UPRIGHT}, {"models/monsters/medic/gibs/hook.md2", GIB_SKINNED | GIB_UPRIGHT}, {"models/monsters/medic/gibs/gun.md2", GIB_SKINNED | GIB_UPRIGHT}, {"models/monsters/medic/gibs/head.md2", GIB_SKINNED | GIB_HEAD}});

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
	{ai_move, -1},
	{ai_move, -1, monster_duck_down},
	{ai_move, -1, monster_duck_hold},
	{ai_move, -1},
	{ai_move, -1},
	{ai_move, -1}, // PMM - duck up used to be here
	{ai_move, -1},
	{ai_move, -1},
	{ai_move, -1},
	{ai_move, -1},
	{ai_move, -1},
	{ai_move, -1},
	{ai_move, -1, monster_duck_up}};
MMOVE_T(medic_move_duck) = {FRAME_duck2, FRAME_duck14, medic_frames_duck, medic_run};

// PMM -- moved dodge code to after attack code so I can reference attack frames

mframe_t medic_frames_attackHyperBlaster[] = {
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge, 0, medic_fire_blaster},
	{ai_charge},
	{ai_charge},
	// [Paril-KEX] end on 36 as intended
	{ai_charge, 2.f}, // 33
	{ai_charge, 3.f, monster_footstep},
};
MMOVE_T(medic_move_attackHyperBlaster) = {FRAME_attack15, FRAME_attack34, medic_frames_attackHyperBlaster, medic_run};

static void medic_quick_attack(edict_t *self)
{
	if (frandom() < 0.5f)
	{
		M_SetAnimation(self, &medic_move_attackHyperBlaster, false);
		self->monsterinfo.nextframe = FRAME_attack16;
	}
}

void medic_continue(edict_t *self)
{
	// Validar que self es válido
	if (!self)
	{
		gi.Com_PrintFmt("Error: medic_continue - null self");
		return;
	}

	// Validar que enemy existe y está vivo
	if (!self->enemy || !self->enemy->inuse || self->enemy->health <= 0)
	{
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
	{ai_charge, 5},
	{ai_charge, 3},
	{ai_charge, 2},
	{ai_charge, 0, medic_quick_attack},
	{ai_charge, 0, monster_footstep},
	{ai_charge},
	{ai_charge, 0, medic_fire_blaster_bolt},
	{ai_charge},
	{ai_charge},
	{ai_charge, 0, medic_fire_blaster_bolt},
	{ai_charge},
	{ai_charge, 0, medic_continue} // Change to medic_continue... Else, go to frame 32
};
MMOVE_T(medic_move_attackBlaster) = {FRAME_attack3, FRAME_attack14, medic_frames_attackBlaster, medic_run};

void medic_hook_launch(edict_t *self)
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
	{45.0f, -9.2f, 15.5f},
	{48.4f, -9.7f, 15.2f},
	{47.8f, -9.8f, 15.8f},
	{47.3f, -9.3f, 14.3f},
	{45.4f, -10.1f, 13.1f},
	{41.9f, -12.7f, 12.0f},
	{37.8f, -15.8f, 11.2f},
	{34.3f, -18.4f, 10.7f},
	{32.7f, -19.7f, 10.4f},
	{32.7f, -19.7f, 10.4f}};

// This helper function checks if a target needs health or armor.
bool M_NeedRegen(edict_t *target)
{
	if (!target || !target->inuse)
		return false;

	// Check if target needs health.
	if (target->health > 0 && target->health < target->max_health)
		return true;

	// Check if monster needs armor (must match healing logic structure).
	if (target->svflags & SVF_MONSTER)
	{
		// Check power armor first (if monster has power armor system)
		if (target->monsterinfo.max_power_armor_power > 0)
		{
			if (target->monsterinfo.power_armor_power < target->monsterinfo.max_power_armor_power)
				return true;
		}
		// Check regular armor (only if monster actually has armor_type set)
		else if (target->monsterinfo.armor_type != IT_NULL && target->monsterinfo.armor_power < 200)
		{
			return true;
		}
	}
	// Check if player needs armor.
	else if (target->client)
	{
		int armor_index = ArmorIndex(target);
		if (armor_index != IT_NULL)
		{
			// Player has armor, check if it's below healing cap
			int current_armor = target->client->pers.inventory[armor_index];
			// Use same max as healing code (150) for consistency
			int max_armor = 150;
			if (current_armor < max_armor)
				return true;
		}
		else
		{
			// Player has no armor at all, so they need it.
			return true;
		}
	}

	return false;
}

// This is the final, complete version of the function.
// It includes the correct animation loop, pause, sound, resurrection,
// the fix for the "won't stop healing" bug, and the effective healing rates.
void medic_cable_attack(edict_t *self)
{
	// --- FRAME 51: PAUSE & LOOP LOGIC ---
	if (self->s.frame == FRAME_attack51)
	{
		if (self->monsterinfo.aiflags & AI_HOLD_FRAME)
		{
			// FIX: Check if target still needs healing EVERY FRAME during hold
			if (!self->enemy || !self->enemy->inuse || !M_NeedRegen(self->enemy) ||
				!visible(self, self->enemy) || realrange(self, self->enemy) > MEDIC_MAX_HEAL_DISTANCE)
			{
				cleanupHeal(self);
				self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
				self->monsterinfo.nextframe = FRAME_attack54;
				return;
			}

			if (level.time >= self->monsterinfo.attack_finished)
			{
				self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
				self->monsterinfo.nextframe = FRAME_attack43;
			}
		}
		else
		{
			if (!self->enemy || !self->enemy->inuse || !M_NeedRegen(self->enemy) ||
				!visible(self, self->enemy) || realrange(self, self->enemy) > MEDIC_MAX_HEAL_DISTANCE)
			{
				cleanupHeal(self);
				self->monsterinfo.nextframe = FRAME_attack54;
			}
			else
			{
				self->monsterinfo.aiflags |= AI_HOLD_FRAME;
				self->monsterinfo.attack_finished = level.time + 200_ms; // This controls the pause duration.
			}
		}
	}

	// --- GENERIC HEALING LOGIC (Runs on all frames) ---
	if (!self->enemy || !self->enemy->inuse)
	{
		cleanupHeal(self);
		self->monsterinfo.nextframe = FRAME_attack54;
		return;
	}

	if (realrange(self, self->enemy) <= MEDIC_MAX_HEAL_DISTANCE && visible(self, self->enemy))
	{
		vec3_t offset, start, forward, right;
		trace_t tr;

		AngleVectors(self->s.angles, forward, right, nullptr);
		int cable_index = min((int)(self->s.frame - FRAME_attack42), 9);
		offset = medic_cable_offsets[cable_index];
		start = M_ProjectFlashSource(self, offset, forward, right);

		tr = gi.traceline(start, self->enemy->s.origin, self, MASK_SOLID);
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_MEDIC_CABLE_ATTACK);
		gi.WriteShort(self - g_edicts);
		gi.WritePosition(start);
		gi.WritePosition(tr.endpos);
		gi.multicast(self->s.origin, MULTICAST_PVS, false);

		if (level.time >= self->monsterinfo.checkattack_time)
		{
			gi.sound(self->enemy, CHAN_AUTO, sound_hook_heal, 1, ATTN_NORM, 0);
			self->monsterinfo.checkattack_time = level.time + 1_sec;
		}

		if (self->enemy->health <= 0)
		{
			if (self->s.frame == FRAME_attack43)
			{
				if (Medic_CanResurrect(self))
				{
					finishHeal(self);
					self->monsterinfo.nextframe = FRAME_attack43;
				}
				else
				{
					abortHeal(self, false, false);
				}
			}
		}
		else
		{
			// --- CORRECTED HEALING AMOUNT ---
			bool is_friendly = (self->monsterinfo.aiflags & AI_GOOD_GUY) != 0;
			int heal_amount = is_friendly ? 6 : 4; // Using your more effective values.

			self->enemy->health = min((int)self->enemy->health + heal_amount, (int)self->enemy->max_health);

			if (self->enemy->svflags & SVF_MONSTER)
			{
				// Heal power armor if monster has it
				if (self->enemy->monsterinfo.max_power_armor_power > 0)
				{
					self->enemy->monsterinfo.power_armor_power = min(self->enemy->monsterinfo.power_armor_power + heal_amount / 2, self->enemy->monsterinfo.max_power_armor_power);
				}
				// Heal regular armor only if monster actually has armor_type set
				else if (self->enemy->monsterinfo.armor_type != IT_NULL && self->enemy->monsterinfo.armor_power < 200)
				{
					self->enemy->monsterinfo.armor_power = min(self->enemy->monsterinfo.armor_power + heal_amount / 2, 200);
				}
			}
			else if (self->enemy->client)
			{
				// Heal player's armor - adapts to whichever armor type they currently have
				int armor_index = ArmorIndex(self->enemy);
				if (armor_index == IT_NULL)
				{
					// Player has no armor - give them jacket armor to start
					self->enemy->client->pers.inventory[IT_ARMOR_JACKET] = heal_amount / 2;
				}
				else
				{
					// Player has armor - heal whichever type they have (jacket/combat/body)
					// Use consistent max like CTF regen, regardless of armor type
					int current_armor = self->enemy->client->pers.inventory[armor_index];
					int max_armor = 150; // Reasonable healing cap for all armor types
					if (current_armor < max_armor)
					{
						self->enemy->client->pers.inventory[armor_index] = min(current_armor + heal_amount / 2, max_armor);
					}
				}
			}
		}

		// Check if target is fully healed - stop immediately on ANY frame (like old version)
		if (!M_NeedRegen(self->enemy))
		{
			cleanupHeal(self);
			self->monsterinfo.nextframe = FRAME_attack54;
			return;
		}
	}
	else
	{
		cleanupHeal(self);
		self->monsterinfo.nextframe = FRAME_attack54;
	}
}

void medic_delay(edict_t *self)
{
	self->monsterinfo.attack_finished = level.time + gtime_t::from_sec(frandom() + 1.0f);
}

// In m_medic.c
void medic_finish_and_hunt(edict_t *self)
{
	// Clear medic-specific flags now that the action is complete.
	self->monsterinfo.aiflags &= ~AI_MEDIC;
	self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);

	// PRIORITY 1: RETURN TO COMBAT
	if (self->oldenemy && self->oldenemy->inuse && self->oldenemy->health > 0)
	{
		self->enemy = self->oldenemy;
		self->oldenemy = nullptr;
		HuntTarget(self, true);
		return;
	}

	// PRIORITY 2: FIND A NEW ENEMY
	bool found_target = false;
	if (g_horde->integer && (self->monsterinfo.isfriendlyspawn || (self->monsterinfo.bonus_flags & BF_FRIENDLY)))
	{
		found_target = FindMTarget(self);
	}
	if (!found_target)
	{
		found_target = FindTarget(self);
	}

	if (found_target)
	{
		return; // FoundTarget already set proper state
	}

	// PRIORITY 3: NO ENEMIES FOUND, GO IDLE
	self->enemy = nullptr;
	self->oldenemy = nullptr;

	// **FIX: Set a SHORT pausetime to prevent immediate stand->run cycling**
	// This gives the medic a moment to stabilize before ai_stand kicks in
	self->monsterinfo.pausetime = level.time + 0.5_sec;

	// Transition to stand - the cooldown in medic_FindDeadMonster will prevent loops
	M_SetAnimation(self, &medic_move_stand);
}

void medic_hook_retract(edict_t *self)
{
	if (self->mass == 400)
		gi.sound(self, CHAN_WEAPON, sound_hook_retract, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_WEAPON, sound_hook_retract, 1, ATTN_NORM, 0);

	self->monsterinfo.aiflags &= ~AI_MEDIC;
	fixHealerEnemy(self);
}

// Search for nearby enemies (used to interrupt healing if threat appears)
bool mymedic_findenemy(edict_t *self)
{
	edict_t *target = nullptr;
	float search_radius = 1024.0f;

	while ((target = findradius(target, self->s.origin, search_radius)) != nullptr)
	{
		// Check if this is a valid enemy target
		if (target == self)
			continue;
		if (!target->inuse || !target->takedamage)
			continue;
		if (target->health <= 0)
			continue;
		if (OnSameTeam(self, target))
			continue;
		if (!visible(self, target))
			continue;

		self->enemy = target;
		return true;
	}
	return false;
}

void medic_heal_end(edict_t *self);

// Modified animation frames to support healing loop
mframe_t medic_frames_attackCable[] = {
	{ai_charge, -5.f},					  // 33
	{ai_charge, -6.f},					  // 34
	{ai_charge, -5.f},					  // 35
	{ai_charge, -6.f},					  // 36
	{ai_charge, -4.7f},					  // 37
	{ai_charge, -5.f},					  // 38
	{ai_charge, -6.f},					  // 39
	{ai_charge, -4.f},					  // 40
	{ai_charge, 0, monster_footstep},	  // 41
	{ai_charge, 0, medic_hook_launch},	  // 42 - launch cable
	{ai_charge, 0, medic_cable_attack},	  // 43 - start of healing loop
	{ai_charge, 0, medic_cable_attack},	  // 44
	{ai_charge, 0, medic_cable_attack},	  // 45
	{ai_charge, 0, medic_cable_attack},	  // 46
	{ai_charge, 0, medic_cable_attack},	  // 47
	{ai_charge, 0, medic_cable_attack},	  // 48
	{ai_charge, 0, medic_cable_attack},	  // 49
	{ai_charge, 0, medic_cable_attack},	  // 50
// VVV CHANGE THIS LINE VVV
	{ai_charge, 0, medic_cable_attack},   // 51 - NOW CALLS THE MAIN FUNCTION
// ^^^ CHANGE THIS LINE ^^^
	{ai_charge, 0, medic_cable_attack},	  // 52
	{ai_charge, 0, nullptr},			  // 53
	{ai_charge, 0, medic_hook_retract},	  // 54 - retract cable
	{ai_charge, 0, nullptr},			  // 55
	{ai_charge, 0, nullptr},			  // 56
	{ai_charge, 0, nullptr},			  // 57
	{ai_charge, 0, nullptr},			  // 58
	{ai_charge, 0, nullptr},			  // 59
	{ai_charge, 0, medic_delay}			  // 60
};
MMOVE_T(medic_move_attackCable) = {FRAME_attack33, FRAME_attack60, medic_frames_attackCable, medic_heal_end};

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
// MMOVE_T(medic_move_attackCable) = { FRAME_attack37, FRAME_attack55, medic_frames_attackCable, medic_run };

// Called at the end of cable attack - decides whether to continue healing or stop
void medic_heal_end(edict_t *self)
{
	// Stop healing if target died, is fully healed, or out of range while standing ground
	if (!self->enemy || !self->enemy->inuse || self->enemy->health <= 0 || !M_NeedRegen(self->enemy) ||
		((self->monsterinfo.aiflags & AI_STAND_GROUND) && (realrange(self, self->enemy) > MEDIC_MAX_HEAL_DISTANCE)))
	{
		self->enemy = nullptr;
		self->monsterinfo.aiflags &= ~AI_MEDIC;
		M_SetAnimation(self, &medic_move_stand);
		return;
	}

	// Continue healing if target is still in range and there are no enemies around
	if (OnSameTeam(self, self->enemy) && (realrange(self, self->enemy) <= MEDIC_MAX_HEAL_DISTANCE) &&
		!mymedic_findenemy(self))
	{
		M_SetAnimation(self, &medic_move_attackCable);
	}
	else
	{
		// Enemy appeared or target moved - stop healing
		self->monsterinfo.aiflags &= ~AI_MEDIC;
		medic_run(self);
	}
}

void medic_start_spawn(edict_t *self)
{
	gi.sound(self, CHAN_WEAPON, commander_sound_spawn, 1, ATTN_NORM, 0);
	self->monsterinfo.nextframe = FRAME_attack48;
}

void medic_determine_spawn(edict_t *self)
{
	vec3_t f, r, offset, startpoint, spawnpoint;
	int count;
	int num_success = 0;

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
		if (def_index >= self->monsterinfo.reinforcements.defs.size())
			continue;

		const auto &reinforcement_def = self->monsterinfo.reinforcements.defs[def_index];
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
			if (self->s.scale)
				offset *= self->s.scale;
			offset[0] *= -1.0f;
			offset[1] *= -1.0f;
			startpoint = M_ProjectFlashSource(self, offset, f, r);
			startpoint[2] += 10;

			// --- START OF FIX (Second Loop) ---
			uint8_t def_index = self->monsterinfo.chosen_reinforcements[count];
			if (def_index >= self->monsterinfo.reinforcements.defs.size())
				continue;

			const auto &reinforcement_def = self->monsterinfo.reinforcements.defs[def_index];
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

void medic_spawngrows(edict_t *self)
{
	vec3_t f, r, offset, startpoint, spawnpoint;
	int count;
	int num_summoned = 0;
	int num_success = 0;
	float current_yaw;

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

	for (size_t i = 0; i < MAX_REINFORCEMENTS; i++)
	{
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
		if (def_index >= self->monsterinfo.reinforcements.defs.size())
			continue;

		const auto &reinforcement_def = self->monsterinfo.reinforcements.defs[def_index];
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

void medic_finish_spawn(edict_t *self)
{
	edict_t *ent;
	vec3_t f, r, offset, startpoint, spawnpoint;
	size_t num_summoned = 0;
	edict_t *designated_enemy;

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
		if (self->monsterinfo.chosen_reinforcements[i] == 255)
		{
			break;
		}
		num_summoned++;
	}

	for (size_t count_idx = 0; count_idx < num_summoned; count_idx++)
	{
		uint8_t def_index = self->monsterinfo.chosen_reinforcements[count_idx];
		if (def_index >= self->monsterinfo.reinforcements.defs.size())
		{
			continue;
		}

		const auto &reinforcement_def = self->monsterinfo.reinforcements.defs[def_index];
		horde::MonsterTypeID typeId = reinforcement_def.typeId;

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
				ent = CreateGroundMonster(spawnpoint, self->s.angles, mins, maxs, typeId, 256);
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

		if (g_horde && g_horde->integer)
		{
			// Increment global spawn counter
			level.global_spawned_count++;
			if (brandom())
			{
				ent->item = G_HordePickItem();
			}
			else
			{
				ent->item = nullptr;
			}
		}
		else
		{
			ent->item = nullptr;
		}

		ApplyMonsterBonusFlags(ent);

		// FIX: Use a temporary variable for coop target selection to avoid overwriting
		// the validated 'designated_enemy' unless a valid coop target is found.
		edict_t *final_target = designated_enemy;
		if (coop && coop->integer)
		{
			edict_t *coop_target = PickCoopTarget(ent);
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
	{ai_charge, 2}, // 33
	{ai_charge, 3},
	{ai_charge, 5},
	{ai_charge, 4.4f}, // 36
	{ai_charge, 4.7f},
	{ai_charge, 5},
	{ai_charge, 6},
	{ai_charge, 4}, // 40
	{ai_charge, 0, monster_footstep},
	{ai_move, 0, medic_start_spawn}, // 42
	{ai_move},						 // 43 -- 43 through 47 are skipped
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move},
	{ai_move, 0, medic_determine_spawn}, // 48
	{ai_charge, 0, medic_spawngrows},	 // 49
	{ai_move},							 // 50
	{ai_move},							 // 51
	{ai_move, -15, medic_finish_spawn},	 // 52
	{ai_move, -1.5f},
	{ai_move, -1.2f},
	{ai_move, -3, monster_footstep}};
MMOVE_T(medic_move_callReinforcements) = {FRAME_attack33, FRAME_attack55, medic_frames_callReinforcements, medic_run};

//// Add this function to check if a tesla mine can be converted
// bool tesla_check_conversion(edict_t* tesla, edict_t* converter)
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
// }
//
//// Add this function to convert a tesla mine to the medic's team
// void tesla_convert(edict_t* tesla, edict_t* converter)
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
// }

MONSTERINFO_ATTACK(medic_attack)(edict_t *self)->void
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
		if ((self->mass > 400) && (r > 0.8f) && M_CanSpawnMore(self))
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
		if ((self->mass > 400) && (r > 0.2f) && (enemy_range > RANGE_MELEE) && M_CanSpawnMore(self))
			M_SetAnimation(self, &medic_move_callReinforcements);
		else
			M_SetAnimation(self, &medic_move_attackBlaster);
	}
}

MONSTERINFO_CHECKATTACK(medic_checkattack)(edict_t *self)->bool
{
	// --- BLOCK 1: OPPORTUNISTIC HEALING (WHEN IN COMBAT MODE) ---
	// If we are not currently in medic mode, check if we should switch.
	if (!(self->monsterinfo.aiflags & AI_MEDIC))
	{
		// Only check for healing targets if there is no immediate combat threat.
		bool has_active_threat = false;
		if (self->enemy && self->enemy->inuse && self->enemy->health > 0)
		{
			if (realrange(self, self->enemy) < MEDIC_MAX_HEAL_DISTANCE * 1.5f)
			{
				has_active_threat = true;
			}
		}

		if (!has_active_threat)
		{
			// For summoned medics, check if summon limit is reached before trying to resurrect
			if (!Medic_CanResurrect(self))
			{
				// Skip resurrection entirely - medic will continue normal behavior
				// No need to notify player repeatedly
				return M_CheckAttack(self);
			}

			edict_t *dead = medic_FindDeadMonster(self);
			// We only care about corpses here. Living allies are handled by medic_check_heal.
			if (dead && dead->health <= 0 && realrange(self, dead) < MEDIC_MAX_HEAL_DISTANCE * 0.75f)
			{
				// Found a corpse and it's safe to revive. Switch to medic mode.
				self->oldenemy = self->enemy;
				self->enemy = dead;
				self->enemy->monsterinfo.healer = self;
				self->monsterinfo.aiflags |= AI_MEDIC;
				self->timestamp = level.time + MEDIC_TRY_TIME;

				// **REFINED LOGIC**: Instead of calling medic_attack() directly,
				// we set the attack state and return true. This lets ai_run handle
				// turning towards the target before firing the heal beam, which is cleaner.
				self->monsterinfo.attack_state = AS_MISSILE;
				return true;
			}
		}
	}

	// --- BLOCK 2: ACTIVE HEALING (WHEN IN MEDIC MODE) ---
	// If we are already in medic mode, decide if we are in range to heal.
	if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		// If our patient disappears, abort the healing process.
		if (!self->enemy || !self->enemy->inuse)
		{
			abortHeal(self, false, false);
			return false;
		}

		// Timeout logic to prevent getting stuck on unreachable targets.
		if (self->timestamp < level.time)
		{
			abortHeal(self, false, true);
			return false;
		}

		// Check if we are in range to use the healing cable.
		if (realrange(self, self->enemy) < MEDIC_MAX_HEAL_DISTANCE)
		{
			// We are in range. Signal to start the healing attack.
			self->monsterinfo.attack_state = AS_MISSILE;
			return true;
		}
		else
		{
			// We are too far away. Signal to keep running towards the target.
			self->monsterinfo.attack_state = AS_STRAIGHT;
			return false;
		}
	}

	// --- BLOCK 3: STANDARD COMBAT LOGIC ---
	// If we are not in medic mode and found no one to heal, use default combat checks.
	if (self->enemy && self->enemy->client && !visible(self, self->enemy) && M_CanSpawnMore(self))
	{
		self->monsterinfo.attack_state = AS_BLIND;
		return true;
	}

	if (self->monsterinfo.monster_slots && (frandom() < 0.8f) && M_CanSpawnMore(self) && (M_SlotsLeft(self) > self->monsterinfo.monster_slots * 0.8f) && (realrange(self, self->enemy) > 150))
	{
		self->monsterinfo.aiflags |= AI_BLOCKED;
		self->monsterinfo.attack_state = AS_MISSILE;
		return true;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.attack_state = AS_MISSILE;
		return true;
	}

	// Fall back to the generic monster attack checker.
	return M_CheckAttack(self);
}

void MedicCommanderCache()
{
	gi.modelindex("models/items/spawngro3/tris.md2");
}

MONSTERINFO_DUCK(medic_duck)(edict_t *self, gtime_t eta)->bool
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

MONSTERINFO_SIDESTEP(medic_sidestep)(edict_t *self)->bool
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
MONSTERINFO_BLOCKED(medic_blocked)(edict_t *self, float dist)->bool
{
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
// PGM
//===========

/*QUAKED monster_medic_commander (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 */
/*QUAKED monster_medic (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
model="models/monsters/medic/tris.md2"
*/
void SP_monster_medic(edict_t *self)
{
	const spawn_temp_t &st = ED_GetSpawnTemp();

	if (self->monsterinfo.monster_type_id == MONSTER_TYPE_UNKNOWN)
	{ // Check if it hasn't been set yet
		self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::MEDIC);
	}

	if (!M_AllowSpawn(self))
	{
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

	self->mins = {-24, -24, -24};
	self->maxs = {24, 24, 32};

	// PMM
	if (horde::IsMonsterType(self, horde::MonsterTypeID::MEDIC_COMMANDER))
	{
		if (g_horde && g_horde->integer && current_wave_level > 0) {
			self->health = M_MEDIC_COMMANDER_ADDON_HEALTH(self);
		} else {
			self->health = static_cast<int>(M_MEDIC_COMMANDER_INITIAL_HEALTH * st.health_multiplier);
		}
		self->gib_health = -130;
		self->mass = 600;
		self->yaw_speed = 40; // default is 20
		MedicCommanderCache();
	}
	else
	{
		// PMM
		if (g_horde && g_horde->integer && current_wave_level > 0) {
			self->health = M_MEDIC_ADDON_HEALTH(self);
		} else {
			self->health = static_cast<int>(M_MEDIC_INITIAL_HEALTH * st.health_multiplier);
		}
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

	// Initialize resurrection tracking
	self->monsterinfo.last_resurrection_time = 0_ms;

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

	// Disable reinforcement spawning for summoned medics to prevent cascading spawns
	if (self->monsterinfo.isfriendlyspawn)
	{
		self->monsterinfo.monster_slots = 0;
		self->monsterinfo.monster_used = 0;
	}

	ApplyMonsterBonusFlags(self);
}

/*QUAKED monster_medic_commander (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 */
void SP_monster_medic_commander(edict_t *self)
{
	// Set mass to indicate commander variant
	self->mass = 600;
	self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeID::MEDIC_COMMANDER);
	SP_monster_medic(self);
}
