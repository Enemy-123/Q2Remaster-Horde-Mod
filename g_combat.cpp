// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_combat.c

#include "g_local.h"
#include "shared.h"


/*
============
CanDamage

Returns true if the inflictor can directly damage the target.  Used for
explosions and melee attacks.
============
*/
bool CanDamage(edict_t* targ, edict_t* inflictor)
{
	vec3_t	dest;
	trace_t trace;

	// bmodels need special checking because their origin is 0,0,0
	vec3_t inflictor_center;

	if (inflictor->linked)
		inflictor_center = (inflictor->absmin + inflictor->absmax) * 0.5f;
	else
		inflictor_center = inflictor->s.origin;

	if (targ->solid == SOLID_BSP)
	{
		dest = closest_point_to_box(inflictor_center, targ->absmin, targ->absmax);

		trace = gi.traceline(inflictor_center, dest, inflictor, MASK_SOLID | CONTENTS_PROJECTILECLIP);
		if (trace.fraction == 1.0f)
			return true;
	}

	vec3_t targ_center;

	if (targ->linked)
		targ_center = (targ->absmin + targ->absmax) * 0.5f;
	else
		targ_center = targ->s.origin;

	// Check center point first
	trace = gi.traceline(inflictor_center, targ_center, inflictor, MASK_SOLID | CONTENTS_PROJECTILECLIP);
	if (trace.fraction == 1.0f)
		return true;

	// Check 4 other points without repeatedly copying targ_center
	vec3_t check_point = targ_center;

	check_point[0] += 15.0f;
	check_point[1] += 15.0f;
	trace = gi.traceline(inflictor_center, check_point, inflictor, MASK_SOLID | CONTENTS_PROJECTILECLIP);
	if (trace.fraction == 1.0f)
		return true;

	check_point[1] -= 30.0f; // From +15 to -15
	trace = gi.traceline(inflictor_center, check_point, inflictor, MASK_SOLID | CONTENTS_PROJECTILECLIP);
	if (trace.fraction == 1.0f)
		return true;

	check_point[0] -= 30.0f; // From +15 to -15
	trace = gi.traceline(inflictor_center, check_point, inflictor, MASK_SOLID | CONTENTS_PROJECTILECLIP);
	if (trace.fraction == 1.0f)
		return true;

	check_point[1] += 30.0f; // From -15 to +15
	trace = gi.traceline(inflictor_center, check_point, inflictor, MASK_SOLID | CONTENTS_PROJECTILECLIP);
	if (trace.fraction == 1.0f)
		return true;


	return false;
}
/*
============
Killed
============
*/
void Killed(edict_t* targ, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, mod_t mod)
{
	if (!targ)  // First check if targ is valid
		return;

	if (targ->health < -999)
		targ->health = -999;

	// [Paril-KEX]
	if ((targ->svflags & SVF_MONSTER) && targ->monsterinfo.aiflags & AI_MEDIC)
	{
		if (targ->enemy && targ->enemy->inuse && (targ->enemy->svflags & SVF_MONSTER)) // god, I hope so
		{
			cleanupHealTarget(targ->enemy);
		}

		// clean up self
		targ->monsterinfo.aiflags &= ~AI_MEDIC;
	}

	targ->enemy = attacker;
	targ->lastMOD = mod;

	// [Paril-KEX] monsters call die in their damage handler
	if (targ->svflags & SVF_MONSTER)
		return;

	if (targ->die)
		targ->die(targ, inflictor, attacker, damage, point, mod);

	// If the entity is freed in its die function, it will no longer be in use.
	// This check prevents a crash on the subsequent line.
	if (!targ->inuse)
		return;

	if (targ->monsterinfo.setskin)
		targ->monsterinfo.setskin(targ);
}
/*
================
SpawnDamage
================
*/
void SpawnDamage(int type, const vec3_t& origin, const vec3_t& normal, int damage)
{
	if (damage > 255)
		damage = 255;
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(type);
	//	gi.WriteByte (damage);
	gi.WritePosition(origin);
	gi.WriteDir(normal);
	gi.multicast(origin, MULTICAST_PVS, false);
}

/*
============
T_Damage

targ		entity that is being damaged
inflictor	entity that is causing the damage
attacker	entity that caused the inflictor to damage targ
	example: targ=monster, inflictor=rocket, attacker=player

dir			direction of the attack
point		point at which the damage is being inflicted
normal		normal vector from that point
damage		amount of damage being inflicted
knockback	force to be applied against targ as a result of the damage

dflags		these flags are used to control how T_Damage works
	DAMAGE_RADIUS			damage was indirect (from a nearby explosion)
	DAMAGE_NO_ARMOR			armor does not protect from this damage
	DAMAGE_ENERGY			damage is from an energy based weapon
	DAMAGE_NO_KNOCKBACK		do not affect velocity, just view angles
	DAMAGE_BULLET			damage is from a bullet (used for ricochets)
	DAMAGE_NO_PROTECTION	kills godmode, armor, everything
============
*/
static int CheckPowerArmor(edict_t* ent, const vec3_t& point, const vec3_t& normal, int damage, damageflags_t dflags)
{
	gclient_t* client;
	int		   save;
	item_id_t  power_armor_type;
	int		   damagePerCell;
	int		   pa_te_type;
	int* power;
	int		   power_used;

	if (ent->health <= 0)
		return 0;

	if (!damage)
		return 0;

	client = ent->client;

	if (dflags & (DAMAGE_NO_ARMOR | DAMAGE_NO_POWER_ARMOR)) // PGM
		return 0;

	if (client)
	{
		power_armor_type = PowerArmorType(ent);
		power = &client->pers.inventory[IT_AMMO_CELLS];
	}
	else if (ent->svflags & SVF_MONSTER)
	{
		power_armor_type = ent->monsterinfo.power_armor_type;
		power = &ent->monsterinfo.power_armor_power;
	}
	else
		return 0;

	if (power_armor_type == IT_NULL)
		return 0;
	if (!*power)
		return 0;

	if (power_armor_type == IT_ITEM_POWER_SCREEN)
	{
		vec3_t vec;
		float  dot;
		vec3_t forward;

		// only works if damage point is in front
		AngleVectors(ent->s.angles, forward, nullptr, nullptr);
		vec = point - ent->s.origin;
		vec.normalize();
		dot = vec.dot(forward);
		if (dot <= 0.3f)
			return 0;

		damagePerCell = 1;
		pa_te_type = TE_SCREEN_SPARKS;
		damage = damage / 3;
	}
	else
	{
		if (ctf->integer)
			damagePerCell = 2; // power armor is weaker in CTF
		else
			damagePerCell = 2;
		pa_te_type = TE_SCREEN_SPARKS;
		damage = (2 * damage) / 3;
	}

	// Paril: fix small amounts of damage not
	// being absorbed
	damage = max(1, damage);

	save = *power * damagePerCell;

	if (!save)
		return 0;

	// [Paril-KEX] energy damage should do more to power armor, not ETF Rifle shots.
	if (dflags & DAMAGE_ENERGY)
		save = max(1, save / 2);

	if (save > damage)
		save = damage;

	// [Paril-KEX] energy damage should do more to power armor, not ETF Rifle shots.
	if (dflags & DAMAGE_ENERGY)
		power_used = (save / damagePerCell) * 2;
	else
		power_used = save / damagePerCell;

	power_used = max(1, power_used);

	SpawnDamage(pa_te_type, point, normal, save);
	ent->powerarmor_time = level.time + 200_ms;

	// Paril: adjustment so that power armor
	// always uses damagePerCell even if it does
	// only a single point of damage
	*power = max(0, *power - max(damagePerCell, power_used));

	// check power armor turn-off states
	if (ent->client)
		G_CheckPowerArmor(ent);
	else if (!*power)
	{
		gi.sound(ent, CHAN_AUTO, gi.soundindex("misc/mon_power2.wav"), 1.f, ATTN_NORM, 0.f);

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_POWER_SPLASH);
		gi.WriteEntity(ent);
		gi.WriteByte((power_armor_type == IT_ITEM_POWER_SCREEN) ? 1 : 0);
		gi.multicast(ent->s.origin, MULTICAST_PHS, false);
	}

	return save;
}

static int CheckArmor(edict_t* ent, const vec3_t& point, const vec3_t& normal, int damage, int te_sparks,
	damageflags_t dflags)
{
	gclient_t* client;
	int		   save;
	item_id_t  index;
	gitem_t* armor;
	int* power;

	if (!damage)
		return 0;

	// ROGUE
	if (dflags & (DAMAGE_NO_ARMOR | DAMAGE_NO_REG_ARMOR))
		// ROGUE
		return 0;

	client = ent->client;
	index = ArmorIndex(ent);

	if (!index)
		return 0;

	armor = GetItemByIndex(index);

	if (dflags & DAMAGE_ENERGY)
		save = (int)ceilf(armor->armor_info->energy_protection * damage);
	else
		save = (int)ceilf(armor->armor_info->normal_protection * damage);

	if (client)
		power = &client->pers.inventory[index];
	else
		power = &ent->monsterinfo.armor_power;

	if (save >= *power)
		save = *power;

	if (!save)
		return 0;

	*power -= save;

	if (!client && !ent->monsterinfo.armor_power)
		ent->monsterinfo.armor_type = IT_NULL;

	SpawnDamage(te_sparks, point, normal, save);

	return save;
}
// In g_monster.c or wherever M_ReactToDamage is located


void M_ReactToDamage(edict_t* targ, edict_t* attacker, edict_t* inflictor)
{
    // pmm
    bool new_tesla;
    static constexpr gtime_t target_cooldown_react = 1.5_sec;

    if (!(attacker->client) && !(attacker->svflags & SVF_MONSTER))
        return;

    //=======
    // ROGUE / HORDE MODIFICATION (REVISED AND CORRECTED)
    //
    // First, determine the tangible "threat source" on the map that the monster should target.
    // This might be different from the 'inflictor' (e.g., a bullet) or the 'attacker' (e.g., a player).
    edict_t* threat_source = nullptr;

    // Case 1: The inflictor is a deployable itself (like a Tesla mine).
    if (inflictor && (horde::IsSpecialType(inflictor, horde::SpecialEntityTypeID::TESLA_MINE) ||
                      horde::IsSpecialType(inflictor, horde::SpecialEntityTypeID::SENTRY_GUN) ||
					  horde::IsSpecialType(inflictor, horde::SpecialEntityTypeID::DOPPLEGANGER) ||
                      horde::IsSpecialType(inflictor, horde::SpecialEntityTypeID::LASER_EMITTER)))
    {
        threat_source = inflictor;
    }
    // Case 2: The inflictor is a part of a deployable (like a laser beam).
    else if (inflictor && horde::IsSpecialType(inflictor, horde::SpecialEntityTypeID::LASER_BEAM))
    {
        threat_source = inflictor->owner; // The threat is the emitter.
    }
    // Case 3: The inflictor is a projectile whose owner is a deployable (e.g., a bullet from a sentry).
    else if (inflictor && inflictor->owner && horde::IsSpecialType(inflictor->owner, horde::SpecialEntityTypeID::SENTRY_GUN))
    {
        threat_source = inflictor->owner; // The threat is the sentry gun.
    }
    // Case 4 (CORRECTED): The inflictor is a projectile/sphere whose owner is a doppelganger.
    else if (inflictor && inflictor->owner && horde::IsSpecialType(inflictor->owner, horde::SpecialEntityTypeID::DOPPLEGANGER))
    {
        // The real threat is the player who owns the doppelganger.
        threat_source = inflictor->owner; 
    }

    // If we identified a deployable as the threat source, react to it.
    if (threat_source && threat_source->inuse)
    {
        // Check if the monster should target this type of deployable.
        if (horde::IsSpecialType(threat_source, horde::SpecialEntityTypeID::TESLA_MINE) ||
            horde::IsSpecialType(threat_source, horde::SpecialEntityTypeID::SENTRY_GUN) ||
			horde::IsSpecialType(threat_source, horde::SpecialEntityTypeID::DOPPLEGANGER) ||
            horde::IsSpecialType(threat_source, horde::SpecialEntityTypeID::LASER_EMITTER))
        {
            new_tesla = MarkTeslaArea(targ, threat_source); // Assuming this function marks the area around any deployable.

            // Sentry Gun or Laser Emitter logic
            if (horde::IsSpecialType(threat_source, horde::SpecialEntityTypeID::SENTRY_GUN) ||
				horde::IsSpecialType(threat_source, horde::SpecialEntityTypeID::DOPPLEGANGER) ||
				horde::IsSpecialType(threat_source, horde::SpecialEntityTypeID::LASER_EMITTER))
            {
                if (level.time - targ->monsterinfo.last_reacttodamage_target_time > target_cooldown_react)
                {
                    if ((new_tesla || brandom()) && (!targ->enemy ||
                        !(horde::IsSpecialType(targ->enemy, horde::SpecialEntityTypeID::SENTRY_GUN) ||
					  	 horde::IsSpecialType(targ->enemy, horde::SpecialEntityTypeID::DOPPLEGANGER) ||
						 horde::IsSpecialType(targ->enemy, horde::SpecialEntityTypeID::LASER_EMITTER))))
                    {
                        TargetTesla(targ, threat_source); // Assuming this function sets the enemy to any deployable.
                        targ->monsterinfo.last_reacttodamage_target_time = level.time;
                    }
                }
            }
            // Tesla Mine logic
            else if (horde::IsSpecialType(threat_source, horde::SpecialEntityTypeID::TESLA_MINE))
            {
                if ((new_tesla || brandom()) && (!targ->enemy || !horde::IsSpecialType(targ->enemy, horde::SpecialEntityTypeID::TESLA_MINE)))
                    TargetTesla(targ, threat_source);
            }
            return; // We've reacted to the deployable, so we are done.
        }
    }
    // ROGUE / HORDE MODIFICATION END
    //=======

	if (attacker == targ || attacker == targ->enemy)
		return;

	// if we are a good guy monster and our attacker is a player
	// or another good guy, do not get mad at them
	if (targ->monsterinfo.aiflags & AI_GOOD_GUY)
	{
		if (attacker->client || (attacker->monsterinfo.aiflags & AI_GOOD_GUY))
			return;
	}

	// PGM
	//  if we're currently mad at something a target_anger made us mad at, ignore
	//  damage
	if (targ->enemy && targ->monsterinfo.aiflags & AI_TARGET_ANGER)
	{
		float percentHealth;

		// make sure whatever we were pissed at is still around.
		if (targ->enemy->inuse)
		{
			percentHealth = static_cast<float>(targ->health) / static_cast<float>(targ->max_health);
			if (targ->enemy->inuse && percentHealth > 0.33f)
				return;
		}

		// remove the target anger flag
		targ->monsterinfo.aiflags &= ~AI_TARGET_ANGER;
	}
	// PGM

	// we recently switched from reacting to damage, don't do it
	if (targ->monsterinfo.react_to_damage_time > level.time)
		return;

	// PMM
	// if we're healing someone, do like above and try to stay with them
	if ((targ->enemy) && (targ->monsterinfo.aiflags & AI_MEDIC))
	{
		float percentHealth;

		percentHealth = static_cast<float>(targ->health) / static_cast<float>(targ->max_health);
		// ignore it some of the time
		if (targ->enemy->inuse && percentHealth > 0.25f)
			return;

		// remove the medic flag
		cleanupHealTarget(targ->enemy);
		targ->monsterinfo.aiflags &= ~AI_MEDIC;
	}
	// PMM

	// we now know that we are not both good guys
	targ->monsterinfo.react_to_damage_time = level.time + random_time(3_sec, 5_sec);

	// if attacker is a client, get mad at them because he's good and we're not
	if (attacker->client)
	{
		targ->monsterinfo.aiflags &= ~AI_SOUND_TARGET;

		// this can only happen in coop (both new and old enemies are clients)
		// only switch if can't see the current enemy
		if (targ->enemy != attacker)
		{
			if (targ->enemy && targ->enemy->client)
			{
				if (visible(targ, targ->enemy))
				{
					targ->oldenemy = attacker;
					return;
				}
				targ->oldenemy = targ->enemy;
			}

			// [Paril-KEX]
			if ((targ->svflags & SVF_MONSTER) && targ->monsterinfo.aiflags & AI_MEDIC)
			{
				if (targ->enemy && targ->enemy->inuse && (targ->enemy->svflags & SVF_MONSTER)) // god, I hope so
				{
					cleanupHealTarget(targ->enemy);
				}

				// clean up self
				targ->monsterinfo.aiflags &= ~AI_MEDIC;
			}

			targ->enemy = attacker;
			if (!(targ->monsterinfo.aiflags & AI_DUCKED))
				FoundTarget(targ);
		}
		return;
	}

	if (attacker->enemy == targ // if they *meant* to shoot us, then shoot back
		// it's the same base (walk/swim/fly) type and both don't ignore shots,
		// get mad at them
		|| (((targ->flags & (FL_FLY | FL_SWIM)) == (attacker->flags & (FL_FLY | FL_SWIM))) &&
			(targ->monsterinfo.monster_type_id != attacker->monsterinfo.monster_type_id) &&
			(!(attacker->monsterinfo.aiflags & AI_IGNORE_SHOTS) ||
				horde::IsSpecialType(attacker, horde::SpecialEntityTypeID::SENTRY_GUN) ||
				horde::IsSpecialType(attacker, horde::SpecialEntityTypeID::DOPPLEGANGER) ||
				horde::IsSpecialType(attacker, horde::SpecialEntityTypeID::LASER_EMITTER)) &&
			!(targ->monsterinfo.aiflags & AI_IGNORE_SHOTS)))
	{
		if (targ->enemy != attacker)
		{
			// [Paril-KEX]
			if ((targ->svflags & SVF_MONSTER) && targ->monsterinfo.aiflags & AI_MEDIC)
			{
				if (targ->enemy && targ->enemy->inuse && (targ->enemy->svflags & SVF_MONSTER)) // god, I hope so
				{
					cleanupHealTarget(targ->enemy);
				}

				// clean up self
				targ->monsterinfo.aiflags &= ~AI_MEDIC;
			}

			if (targ->enemy && targ->enemy->client)
				targ->oldenemy = targ->enemy;
			targ->enemy = attacker;
			if (!(targ->monsterinfo.aiflags & AI_DUCKED))
				FoundTarget(targ);
		}
	}
	// otherwise get mad at whoever they are mad at (help our buddy) unless it is us!
	else if (attacker->enemy && attacker->enemy != targ && targ->enemy != attacker->enemy)
	{
		if (targ->enemy != attacker->enemy)
		{
			// [Paril-KEX]
			if ((targ->svflags & SVF_MONSTER) && targ->monsterinfo.aiflags & AI_MEDIC)
			{
				if (targ->enemy && targ->enemy->inuse && (targ->enemy->svflags & SVF_MONSTER)) // god, I hope so
				{
					cleanupHealTarget(targ->enemy);
				}

				// clean up self
				targ->monsterinfo.aiflags &= ~AI_MEDIC;
			}

			if (targ->enemy && targ->enemy->client)
				targ->oldenemy = targ->enemy;
			targ->enemy = attacker->enemy;
			if (!(targ->monsterinfo.aiflags & AI_DUCKED))
				FoundTarget(targ);
		}
	}
}

// ADD THIS NEW FUNCTION
ctfteam_t GetEntityTeam(const edict_t* ent)
{
    if (!ent || !ent->inuse) {
        return CTF_NOTEAM;
    }

    // 1. Check if it's a player (highest priority)
    if (ent->client) {
        return ent->client->resp.ctf_team;
    }

    // 2. Check if it's a monster (includes Sentry Guns)
    if (ent->svflags & SVF_MONSTER) {
        return (ctfteam_t)ent->monsterinfo.team;
    }
    
    // 3. Check the new unified field for deployables and other entities.
    //    If this is set, it's the definitive team.
    if (ent->ctf_team != CTF_NOTEAM) {
        return ent->ctf_team;
    }

    // 5. If none of the above, it has no team.
    return CTF_NOTEAM;
}

bool IsLaserEntity(edict_t* ent) {
	return
		(horde::IsSpecialType(ent, horde::SpecialEntityTypeID::LASER_BEAM)) || (horde::IsSpecialType(ent, horde::SpecialEntityTypeID::LASER_EMITTER));
}

bool CheckTeamDamage(edict_t* targ, edict_t* attacker) {
	return !g_friendly_fire->integer && OnSameTeam(targ, attacker);
}

bool OnSameTeam(edict_t* ent1, edict_t* ent2)
{
    // Initial validations remain the same
    if (!ent1 || !ent2 || ent1 == ent2)
        return false;

    // Get the team for each entity using our new unified function
    ctfteam_t team1 = GetEntityTeam(ent1);
    ctfteam_t team2 = GetEntityTeam(ent2);

    // If either entity has no team, they can't be on the same team.
    if (team1 == CTF_NOTEAM || team2 == CTF_NOTEAM)
        return false;

    // A single, fast, reliable integer comparison.
    return team1 == team2;
}

#include <span>

static void HandleIDDamage(edict_t* attacker, const edict_t* targ, int real_damage);
void ApplyGradualArmor(edict_t* ent);
// Nueva estructura para manejar la regeneración gradual


namespace VampireConfig {
	constexpr float BASE_MULTIPLIER = 0.8f;
	constexpr float QUAD_DIVISOR = 2.7f;
	constexpr float DOUBLE_DIVISOR = 1.5f;
	constexpr float TECH_STRENGTH_DIVISOR = 1.6f;
	constexpr int MAX_ARMOR = 200;

	// Nuevos parámetros para el sistema de regeneración gradual
	constexpr gtime_t REGEN_INTERVAL = 80_ms;  // Intervalo de regeneración en segundos
	constexpr float SENTRY_HEALING_FACTOR = 0.4f;  // Factor de reducción para sentries
	constexpr int MAX_STORED_HEALING = 35;  // Máximo de curación almacenada

	constexpr float ARMOR_STEAL_RATIO = 0.166f;
	constexpr int MAX_STORED_ARMOR = 25;  // Máximo de armor almacenado
	constexpr float ARMOR_REGEN_AMOUNT = 1.0f;  // Cantidad de armor regenerado por tick
}

void ApplyGradualHealing(edict_t* ent) {
	// Fast-path early returns grouped for better branch prediction
	if (!ent || ent->health <= 0 || level.time < ent->regen_info.next_regen_time)
		return;

	// Apply health regeneration with  checks and calculations
	if (ent->health < ent->max_health && ent->regen_info.stored_healing > 0) {
		// Use direct min operation to simplify logic
		const float heal_amount = std::min(2.0f, ent->regen_info.stored_healing);

		// Calculate new health with single-step clamping
		const int new_health = std::min(ent->health + static_cast<int>(heal_amount), ent->max_health);
		const int actual_healed = new_health - ent->health;

		// Only update if actual healing occurred (avoids unnecessary writes)
		if (actual_healed > 0) {
			ent->health = new_health;
			ent->regen_info.stored_healing -= actual_healed;
		          // Clear remaining stored healing if max health is reached or exceeded
		          if (ent->health >= ent->max_health) {
		              ent->regen_info.stored_healing = 0;
		          }
		}
	}

	// Apply armor regeneration if player - direct condition check
	if (ent->client) {
		ApplyGradualArmor(ent);
	}

	// Set next regeneration time
	ent->regen_info.next_regen_time = level.time + VampireConfig::REGEN_INTERVAL;
}

void ApplyGradualArmor(edict_t* ent) {
	// Grouped early returns for better branch prediction
	if (!ent || !ent->client)
		return;

	// Cache armor index to avoid repeated function calls
	const int index = ArmorIndex(ent);
	if (!index) {
		ent->regen_info.stored_armor = 0;
		return;
	}

	// Use reference for direct access to armor value
	int& current_armor = ent->client->pers.inventory[index];

	// Combined condition check for better branching
	if (current_armor <= 0 || current_armor >= VampireConfig::MAX_ARMOR) {
		ent->regen_info.stored_armor = 0;
		return;
	}

	// Fast path for no regeneration needed
	if (ent->regen_info.stored_armor <= 0 || level.time < ent->regen_info.next_regen_time)
		return;

	// Calculate regeneration with a single min operation
	const float regen_amount = std::min(VampireConfig::ARMOR_REGEN_AMOUNT, ent->regen_info.stored_armor);

	// Direct calculation of new armor with clamping
	const int new_armor = std::min(
		current_armor + static_cast<int>(regen_amount),
		VampireConfig::MAX_ARMOR
	);

	// Only apply changes if needed (avoid unnecessary writes)
	const int actual_added = new_armor - current_armor;
	if (actual_added > 0) {
		current_armor = new_armor;
		ent->regen_info.stored_armor -= actual_added;

		// Fast cleanup if max reached
		if (new_armor >= VampireConfig::MAX_ARMOR)
			ent->regen_info.stored_armor = 0;
	}
}

struct WeaponMultiplier {
	item_id_t weapon_id;
	float multiplier;
};

static constexpr std::array<WeaponMultiplier, 8> WEAPON_MULTIPLIERS = { {
	{IT_WEAPON_SHOTGUN, 1.0f / DEFAULT_SHOTGUN_COUNT},
	{IT_WEAPON_SSHOTGUN, 0.5f},
	{IT_WEAPON_RLAUNCHER, 0.5f},
	{IT_WEAPON_HYPERBLASTER, 0.5f},
	{IT_WEAPON_PHALANX, 0.5f},
	{IT_WEAPON_RAILGUN, 0.5f},
	{IT_WEAPON_IONRIPPER, 1.0f / 3.0f},
	{IT_WEAPON_GLAUNCHER, 0.5f}
} };

static int64_t CalculateRealDamage(const edict_t* targ, int64_t take, int64_t initial_health) {
	if (!targ) return take;
	if (targ->svflags & SVF_DEADMONSTER) return std::min<int64_t>(take, 5);
	if (initial_health <= 0) return std::min<int64_t>(take, 10);

	int64_t real_damage = std::min<int64_t>(take, initial_health);
	if (targ->health <= 0) {
		// Ensure abs() result is cast to int64_t before comparison if targ->gib_health is int
		real_damage += std::min<int64_t>(static_cast<int64_t>(abs(targ->gib_health)), initial_health);
	}
	return real_damage;
}

static void HandleIDDamage(edict_t* attacker, const edict_t* targ, int real_damage) {
	mod_t    mod;

	// Fast path early returns for improved performance
	if (!attacker || !attacker->client || !g_iddmg || !g_iddmg->integer ||
		!attacker->client->pers.iddmg_state || !targ ||
		targ->monsterinfo.invincible_time > level.time || mod.id == MOD_TRAP) {
		return;
	}

	auto& client = *attacker->client;
	const bool should_reset = level.time - attacker->client->lastdmg > 1.65_sec ||
		client.dmg_counter > 99999;

	// Cast to uint64_t to prevent overflow during addition
	client.dmg_counter = should_reset ?
		static_cast<uint64_t>(real_damage) :
		client.dmg_counter + static_cast<uint64_t>(real_damage);

	// For display, cap at 32-bit max if needed
	client.ps.stats[STAT_ID_DAMAGE] = static_cast<int>(std::min<uint64_t>(client.dmg_counter, INT_MAX));
	attacker->client->lastdmg = level.time;

	if ((targ->svflags & SVF_MONSTER) && targ->health >= 1) {
		// Cast to uint64_t to prevent overflow during addition
		client.total_damage += static_cast<uint64_t>(real_damage);
	}
}

static void HandleAutoHaste(edict_t* attacker, const edict_t* targ, int damage) {
	if (!g_autohaste->integer || !attacker || !attacker->client ||
		attacker->client->quadfire_time >= level.time ||
		damage <= 0 || (attacker->health < 1 && targ->health < 1)) {
		return;
	}

	constexpr float DAMAGE_FACTOR = 1.0f / 1150.0f;
	if (frandom() <= damage * DAMAGE_FACTOR) {
		attacker->client->quadfire_time = level.time + 5_sec;
	}
}

int calculate_health_stolen(edict_t* attacker, int base_health_stolen) {
	if (!attacker || !attacker->client || !attacker->client->pers.weapon) {
		return base_health_stolen;
	}

	float multiplier = VampireConfig::BASE_MULTIPLIER;
	const item_id_t weapon_id = attacker->client->pers.weapon->id;

	// Iterate directly over the std::array (replaces std::span)
	auto it = std::find_if(WEAPON_MULTIPLIERS.begin(), WEAPON_MULTIPLIERS.end(),
		[weapon_id](const WeaponMultiplier& wm) { return wm.weapon_id == weapon_id; });

	if (it != WEAPON_MULTIPLIERS.end()) {
		multiplier = it->multiplier;

		if (weapon_id == IT_WEAPON_MACHINEGUN && g_tracedbullets->integer) {
			multiplier = 0.5f;
		}
		else if (weapon_id == IT_WEAPON_GLAUNCHER && g_bouncygl->integer) {
			multiplier *= 0.5f;
		}
	}

	// Aplicar modificadores de poder
	if (attacker->client->quad_time > level.time)
		multiplier /= VampireConfig::QUAD_DIVISOR;
	if (attacker->client->double_time > level.time)
		multiplier /= VampireConfig::DOUBLE_DIVISOR;
	if (attacker->client->pers.inventory[IT_TECH_STRENGTH])
		multiplier /= VampireConfig::TECH_STRENGTH_DIVISOR;

	return std::max(1, static_cast<int>(base_health_stolen * multiplier));
}

//void heal_attacker_sentries(const edict_t* attacker, int health_stolen) noexcept {
//	if (!attacker || current_wave_level < 17) return;
//
//	// Use traditional loop (replaces std::span)
//	std::span entities_view{ g_edicts, globals.num_edicts };
//	for (auto& ent : entities_view) {
//		if (!ent.inuse || ent.health <= 0 || ent.owner != attacker ||
//			strcmp(ent.classname, "monster_sentrygun") != 0) {
//			continue;
//		}
//		ent.health = std::min(ent.health + health_stolen, ent.max_health);
//	}
//}

void apply_armor_vampire(edict_t* attacker, int damage) {
	if (!attacker || !attacker->client)
		return;

	const int index = ArmorIndex(attacker);
	if (!index) {
		attacker->regen_info.stored_armor = 0;
		return;
	}

	int current_armor = attacker->client->pers.inventory[index];

	if (current_armor <= 0) {
		attacker->regen_info.stored_armor = 0;
		return;
	}

	if (current_armor >= VampireConfig::MAX_ARMOR)
		return;

	// Calculamos el armor directamente del daño, sin considerar el health
	const float armor_stolen = VampireConfig::ARMOR_STEAL_RATIO * (damage / 4.0f);

	// Almacenar el armor hasta el límite de almacenamiento
	attacker->regen_info.stored_armor = std::min(
		attacker->regen_info.stored_armor + armor_stolen,
		static_cast<float>(VampireConfig::MAX_STORED_ARMOR)
	);
}


static bool CanUseVampireEffect(const edict_t* attacker) noexcept {  // const and noexcept for better optimization
	// Fast early returns for invalid cases
	if (!attacker || attacker->health <= 0 || attacker->deadflag) {
		return false;
	}

	// Check for sentrygun first (most common special case)
	if (horde::IsSpecialType(attacker, horde::SpecialEntityTypeID::SENTRY_GUN)) {
		return false; // Sentry guns should not benefit from vampire effect when attacking
	}

	// Check if it's a player (not a monster)
	if (!(attacker->svflags & SVF_MONSTER)) {
		return true;  // Players can use vampire
	}

	// Final check for special monsters - using direct bitwise operations
	constexpr uint32_t VALID_BONUS_FLAGS = (BF_STYGIAN | BF_POSSESSED);
	return (attacker->monsterinfo.bonus_flags & VALID_BONUS_FLAGS) &&
		!attacker->monsterinfo.IS_BOSS;
}

// =======================================================================
// HandleVampireEffect
//
// Applies the "vampire" effect, where an attacker gains health by
// dealing damage. This improved version optimizes sentry healing by
// using player-specific tracking arrays instead of a global entity scan.
//
// Parameters:
//  attacker - The entity dealing damage and receiving the healing.
//  targ     - The entity being damaged.
//  damage   - The amount of damage dealt before armor/reductions.
// =======================================================================
void HandleVampireEffect(edict_t* attacker, edict_t* targ, int damage)
{
    // --- 1. Guard Clauses & Early Exits ---
    // These fast checks prevent unnecessary processing and are crucial for performance.

    // Vampire effect is disabled or damage is zero.
    if (!g_vampire || !g_vampire->integer || damage <= 0) {
        return;
    }

    // Invalid entities.
    if (!attacker || !targ) {
        return;
    }

    // Attacker cannot heal from damaging itself or teammates.
    if (attacker == targ || OnSameTeam(targ, attacker)) {
        return;
    }

    // Do not grant health for damaging an invincible target.
    if (targ->monsterinfo.invincible_time > level.time) {
        return;
    }

    // Check if the attacker is eligible to use the vampire effect.
    if (!CanUseVampireEffect(attacker)) {
        return;
    }

    // --- 2. Attacker Self-Healing ---
    // Calculate the base amount of health to be stolen and stored for regeneration.
    float health_stolen = damage / 4.0f;

    // Only store healing if the attacker is not already at max health.
    if (attacker->health < attacker->max_health) {
        // Apply weapon-specific multipliers to adjust the healing amount.
        // This is only done for players, as monsters don't have weapons in the same way.
        if (attacker->client) {
            health_stolen = calculate_health_stolen(attacker, static_cast<int>(health_stolen));
        }

        // Add the stolen health to the attacker's stored regeneration pool,
        // clamping it to the maximum allowed value.
        const float max_healing = static_cast<float>(VampireConfig::MAX_STORED_HEALING);
        attacker->regen_info.stored_healing = std::min(
            attacker->regen_info.stored_healing + health_stolen,
            max_healing
        );
    }

    // --- 3. Sentry Healing () ---
    // If the attacker is a player, heal their deployed sentries as well.
    // This is a critical optimization: we iterate ONLY over the player's sentries,
    // not the entire global entity list.
    if (attacker->client && current_wave_level >= 10)
    {
        // Pre-calculate the healing amount for sentries.
        const float sentry_heal_amount = health_stolen * VampireConfig::SENTRY_HEALING_FACTOR;
        const float max_stored_healing = static_cast<float>(VampireConfig::MAX_STORED_HEALING);

        // Iterate through the player's specific array of deployed sentries.
        for (int i = 0; i < SentryConstants::MAX_SENTRIES_PER_PLAYER; ++i)
        {
            edict_t* sentry = attacker->client->resp.deployed_sentries[i];

            // Safety check: ensure the sentry exists, is in use, and is not dead.
            if (sentry && sentry->inuse && sentry->health > 0)
            {
                // Add healing to the sentry's regeneration pool, clamping to the max.
                sentry->regen_info.stored_healing = std::min(
                    sentry->regen_info.stored_healing + sentry_heal_amount,
                    max_stored_healing
                );
            }
        }
    }

    // --- 4. Armor Vampire Effect ---
    // At higher vampire levels, the attacker can also steal armor.
    if (g_vampire->integer == 2) {
        apply_armor_vampire(attacker, damage);
    }
}

//t_damage
void T_Damage(edict_t* targ, edict_t* inflictor, edict_t* attacker, const vec3_t& dir, const vec3_t& point,
	const vec3_t& normal, int damage, int knockback, damageflags_t dflags, mod_t mod)
{
	gclient_t* client;
	int		   take = 0;
	int		   save;
	int		   asave;
	int		   psave;
	int		   te_sparks;
	bool	   sphere_notified; // PGM

	if ((targ->svflags & SVF_MONSTER) && targ->monsterinfo.IS_BOSS &&
		(mod.id == MOD_HANDGRENADE || mod.id == MOD_HG_SPLASH) &&
		(inflictor && inflictor->count == 1))
	{
		// It's a cluster grenade from an upgraded prox hitting a boss. Reduce damage.
		damage = lroundf(damage * 0.3f); // 30% reduction.
	}

	if (!targ->takedamage)
		return;

	//Teleport if drown
	if (mod.id == MOD_WATER && (targ->svflags & SVF_MONSTER) && !targ->monsterinfo.IS_BOSS) {
		CheckAndTeleportStuckMonster(targ);
	}

	//attacker null, inflictor will be attacker
	if (!attacker)
		attacker = inflictor;

	//then world would be attacker
	if (!attacker)
		attacker = world;

	if (attacker && (attacker->svflags & SVF_MONSTER)) {
		UpdatePowerUpTimes(attacker);
		damage = static_cast<int>(round(damage * M_DamageModifier(attacker)));
	}

	if (g_instagib->integer && !g_horde->integer && attacker->client && targ->client) {
		// [Kex] always kill no matter what on instagib
		damage = 9999;
	}

	sphere_notified = false;

// friendly fire avoidance
// if enabled you can't hurt teammates (but you can hurt yourself)
// knockback still occurs
	if ((targ != attacker) && !(dflags & DAMAGE_NO_PROTECTION))
	{
		if (OnSameTeam(targ, attacker))
		{
			mod.friendly_fire = true;
			// if we're not a nuke & friendly fire is disabled, just kill the damage
			if (!g_friendly_fire->integer && (mod.id != MOD_TARGET_LASER)) {
				damage = 0;
			}
			else if (attacker->svflags & SVF_MONSTER && targ->svflags & SVF_MONSTER && mod.id == MOD_TARGET_LASER)
			{
				damage = 0;
			}
		}
	}

	if ((targ == attacker) && !(dflags & DAMAGE_NO_PROTECTION))
	{
		if (g_no_self_damage->integer && (mod.id != MOD_TARGET_LASER) && (mod.id != MOD_BARREL) && (mod.id != MOD_EXPLOSIVE))
			damage = 0;
	}

// ROGUE
// allow the deathmatch game to change values
	if (G_IsDeathmatch() && gamerules->integer)
	{
		if (DMGame.ChangeDamage)
			damage = DMGame.ChangeDamage(targ, attacker, damage, mod);
		if (DMGame.ChangeKnockback)
			knockback = DMGame.ChangeKnockback(targ, attacker, knockback, mod);

		if (!damage)
			return;
	}

	// easy mode takes half damage
	if (skill->integer == 0 && !G_IsDeathmatch() && targ->client && damage)
	{
		damage /= 2;
		if (!damage)
			damage = 1;
	}

	if ((targ->svflags & SVF_MONSTER) != 0) {
		damage *= ai_damage_scale->integer;
	}
	else {
		damage *= g_damage_scale->integer;
	}

	client = targ->client;

	if (damage && (client) && (client->owned_sphere) && (client->owned_sphere->spawnflags == SPHERE_DEFENDER))
	{
		damage /= 2;
		if (!damage)
			damage = 1;
	}

	if (dflags & DAMAGE_BULLET)
		te_sparks = TE_BULLET_SPARKS;
	else
		te_sparks = TE_SPARKS;

	if (!(dflags & DAMAGE_RADIUS) && (targ->svflags & SVF_MONSTER) && (attacker->client) &&
		(!targ->enemy || targ->monsterinfo.surprise_time == level.time) && (targ->health > 0))
	{
		damage *= 2;
		targ->monsterinfo.surprise_time = level.time;
	}

	// strength tech, laser won't double damage if tech
	if (mod.id != MOD_PLAYER_LASER)
	{
		damage = CTFApplyStrength(attacker, damage);
	}

	if ((targ->flags & FL_NO_KNOCKBACK) ||
		((targ->flags & FL_ALIVE_KNOCKBACK_ONLY) && (!targ->deadflag || targ->dead_time != level.time)))
		knockback = 0;

	if (!(dflags & DAMAGE_NO_KNOCKBACK))
	{
		if ((knockback) && (targ->movetype != MOVETYPE_NONE) && (targ->movetype != MOVETYPE_BOUNCE) &&
			(targ->movetype != MOVETYPE_PUSH) && (targ->movetype != MOVETYPE_STOP))
		{
			vec3_t normalized_dir = dir.normalized();
			vec3_t kvel;

			// Skip mass calculation for BFG pull
			if (g_bfgpull->integer && mod.id == MOD_BFG_LASER) {
				kvel = normalized_dir * (500.0f * knockback / 50);
			}
			else {
				float mass = (targ->mass < 50) ? 50.0f : static_cast<float>(targ->mass);
				float force = (targ->client && attacker == targ) ? 1600.0f : 500.0f;
				kvel = normalized_dir * (force * knockback / mass);
			}
			targ->velocity += kvel;
		}
	}

	take = damage;
	save = 0;

	// check for godmode
	if ((targ->flags & FL_GODMODE) && !(dflags & DAMAGE_NO_PROTECTION))
	{
		take = 0;
		save = damage;
		SpawnDamage(te_sparks, point, normal, save);
	}

	// check for invincibility
	if (!(dflags & DAMAGE_NO_PROTECTION) &&
		(((client && client->invincible_time > level.time)) ||
			((targ->svflags & SVF_MONSTER) && targ->monsterinfo.invincible_time > level.time)))
	{
		if (targ->pain_debounce_time < level.time)
		{
			gi.sound(targ, CHAN_ITEM, gi.soundindex("items/protect4.wav"), 1, ATTN_NORM, 0);
			targ->pain_debounce_time = level.time + 2_sec;
		}
		take = 0;
		save = damage;
	}

	if (G_TeamplayEnabled() && targ->client && attacker->client &&
		targ->client->resp.ctf_team == attacker->client->resp.ctf_team && targ != attacker &&
		g_teamplay_armor_protect->integer)
	{
		psave = asave = 0;
	}
	else
	{
		psave = CheckPowerArmor(targ, point, normal, take, dflags);
		take -= psave;
		asave = CheckArmor(targ, point, normal, take, te_sparks, dflags);
		take -= asave;
	}

	asave += save;
	take = CTFApplyResistance(targ, take);

	if (dflags & DAMAGE_DESTROY_ARMOR)
	{
		if (!(targ->flags & FL_GODMODE) && !(dflags & DAMAGE_NO_PROTECTION) &&
			!(client && client->invincible_time > level.time))
		{
			take = damage;
		}
	}

	// --- Vampire/ID Damage calls here ---
	// Calculate and apply these effects based on the final damage value ('take')
	// *after* all armor, godmode, and invincibility checks have been applied.
	// This ensures accurate damage reporting and balanced health stealing.
	if (attacker && attacker->client &&
		(g_horde->integer == 0 || !ClientIsSpectating(attacker->client)))
	{
		// Use the actual damage dealt ('take') for these calculations.
		HandleAutoHaste(attacker, targ, take);
		HandleVampireEffect(attacker, targ, take);
		HandleIDDamage(attacker, targ, take);
	}
	// --- END FIX ---

	if (targ != attacker && attacker && attacker->client && targ->health > 0 &&
		!((targ && targ->svflags & SVF_DEADMONSTER) || (targ->flags & FL_NO_DAMAGE_EFFECTS)) &&
		mod.id != MOD_TARGET_LASER &&
		!(attacker && attacker->movetype == MOVETYPE_NOCLIP) &&
		targ->monsterinfo.invincible_time <= level.time)
	{
		attacker->client->ps.stats[STAT_HIT_MARKER] += take + psave + asave;
	}

	if (take)
	{
		if (!(targ->flags & FL_NO_DAMAGE_EFFECTS))
		{
			if (targ->flags & FL_MECHANICAL)
				SpawnDamage(TE_ELECTRIC_SPARKS, point, normal, take);
			else if ((targ->svflags & SVF_MONSTER) || (client))
			{
				if (horde::IsMonsterType(targ, horde::MonsterTypeID::GEKK))
					SpawnDamage(TE_GREENBLOOD, point, normal, take);
				else if (mod.id == MOD_CHAINFIST)
					SpawnDamage(TE_MOREBLOOD, point, normal, 255);
				else
					SpawnDamage(TE_BLOOD, point, normal, take);
			}
			else
				SpawnDamage(te_sparks, point, normal, take);
		}

		if (!CTFMatchSetup())
			targ->health = targ->health - take;

		if (attacker && (attacker->svflags & SVF_MONSTER) &&
			(attacker->monsterinfo.bonus_flags & BF_STYGIAN) &&
			!(attacker->monsterinfo.bonus_flags & BF_FRIENDLY) &&
			take > 0 && attacker->health > 0)
		{
			constexpr float VAMP_FACTOR = 0.3f;
			int heal_amount = std::max(1, static_cast<int>(take * VAMP_FACTOR));
			attacker->health = std::min(attacker->health + heal_amount, attacker->max_health);
		}

		if ((targ->flags & FL_IMMORTAL) && targ->health <= 0)
			targ->health = 1;

		if (client && client->owned_sphere)
		{
			sphere_notified = true;
			if (client->owned_sphere->pain)
				client->owned_sphere->pain(client->owned_sphere, attacker, 0, 0, mod);
		}

		if (targ->health <= 0)
		{
			if (!targ) {
				return;
			}

			if ((targ->svflags & SVF_MONSTER) || (client))
			{
				targ->flags |= FL_ALIVE_KNOCKBACK_ONLY;
				targ->dead_time = level.time;
			}

			if (!targ || !inflictor || !attacker) {
				return;
			}

			if (&targ->monsterinfo) {
				targ->monsterinfo.damage_blood += take;
				targ->monsterinfo.damage_attacker = attacker;
				targ->monsterinfo.damage_inflictor = inflictor;
				targ->monsterinfo.damage_from = point;
				targ->monsterinfo.damage_mod = mod;
				targ->monsterinfo.damage_knockback += knockback;
			}

			Killed(targ, inflictor, attacker, take, point, mod);
			return;
		}
	}

	if (!sphere_notified)
	{
		if (client && client->owned_sphere)
		{
			sphere_notified = true;
			if (client->owned_sphere->pain)
				client->owned_sphere->pain(client->owned_sphere, attacker, 0, 0, mod);
		}
	}

	if (targ && targ->client) {
		targ->client->last_attacker_time = level.time;
		if ((take > 0 || psave > 0 || asave > 0) &&
			!(targ->client->invincible_time > level.time) &&
			!(dflags & DAMAGE_NO_PROTECTION)) {
			targ->client->last_damage_time = level.time + COOP_DAMAGE_RESPAWN_TIME;
		}
	}

	if (targ->svflags & SVF_MONSTER)
	{
		if (damage > 0)
		{
			M_ReactToDamage(targ, attacker, inflictor);
			targ->monsterinfo.damage_attacker = attacker;
			targ->monsterinfo.damage_inflictor = inflictor;
			targ->monsterinfo.damage_blood += take;
			targ->monsterinfo.damage_from = point;
			targ->monsterinfo.damage_mod = mod;
			targ->monsterinfo.damage_knockback += knockback;
		}
		if (targ->monsterinfo.setskin)
			targ->monsterinfo.setskin(targ);
	}
	else if (take && targ->pain)
		targ->pain(targ, attacker, (float)knockback, take, mod);

	if (client)
	{
		client->damage_parmor += psave;
		client->damage_armor += asave;
		client->damage_blood += take;
		client->damage_knockback += knockback;
		client->damage_from = point;

		if (!(dflags & DAMAGE_NO_INDICATOR) && inflictor != world && attacker != world && (take || psave || asave))
		{
			damage_indicator_t* indicator = nullptr;
			size_t i;
			for (i = 0; i < client->num_damage_indicators; i++)
			{
				if ((point - client->damage_indicators[i].from).length() < 32.f)
				{
					indicator = &client->damage_indicators[i];
					break;
				}
			}
			if (!indicator && i != MAX_DAMAGE_INDICATORS)
			{
				indicator = &client->damage_indicators[i];
				indicator->from = (dflags & DAMAGE_RADIUS) ? inflictor->s.origin : attacker->s.origin;
				indicator->health = indicator->armor = indicator->power = 0;
				client->num_damage_indicators++;
			}
			if (indicator)
			{
				indicator->health += take;
				indicator->power += psave;
				indicator->armor += asave;
			}
		}
	}
}
/*
============
T_RadiusDamage
============
*/
void T_RadiusDamage(edict_t* inflictor, edict_t* attacker, float damage, edict_t* ignore, float radius, damageflags_t dflags, mod_t mod)
{
	if (!inflictor)
		return;

	if (!attacker)
		attacker = inflictor;

	vec3_t inflictor_center;
	if (inflictor->linked)
		inflictor_center = (inflictor->absmin + inflictor->absmax) * 0.5f;
	else
		inflictor_center = inflictor->s.origin;

	float damage_modifier = 1.0f;
	if (attacker && (attacker->svflags & SVF_MONSTER)) {
		UpdatePowerUpTimes(attacker);
		damage_modifier = M_DamageModifier(attacker);
	}

	edict_t* ent = nullptr;
	while ((ent = findradius(ent, inflictor_center, radius)) != nullptr)
	{
		if (ent == ignore || !ent->takedamage)
			continue;

		// Determine the single point of impact on the entity.
		// This makes all subsequent calculations consistent.
		vec3_t damage_point;
		if (ent->solid == SOLID_BSP && ent->linked) {
			damage_point = closest_point_to_box(inflictor_center, ent->absmin, ent->absmax);
		}
		else {
			// This logic matches the original code's method for finding the center
			// of players/monsters, ensuring identical behavior.
			vec3_t center = ent->mins + ent->maxs;
			center *= 0.5f;
			damage_point = ent->s.origin + center;
		}

		// Vector from explosion center to the entity's impact point
		vec3_t force_vec = damage_point - inflictor_center;
		float dist = force_vec.length();

		float points = damage - 0.5f * dist;

		if (ent == attacker)
			points *= 0.5f;

		if (points <= 0)
			continue;

		if (!CanDamage(ent, inflictor))
			continue;

		// Calculate knockback direction.
		vec3_t dir;
		if (dist > 0.001f)
			dir = force_vec * (1.0f / dist); // Efficient normalization
		else
			dir = vec3_t(0, 0, 1); // Default up direction if at the same spot

		if (ent && inflictor && attacker) {
			const float modified_points = points * damage_modifier;
			T_Damage(
				ent,
				inflictor,
				attacker,
				dir,
				damage_point, // Use the consistent impact point
				dir,
				static_cast<int>(modified_points),
				static_cast<int>(modified_points),
				dflags | DAMAGE_RADIUS,
				mod
			);
		}
	}
}