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
	// ROGUE / HORDE MODIFICATION
	// First, determine the actual source of the threat.
	// If we're hit by a laser beam, the real threat is the emitter that owns it.
	edict_t* threat = inflictor;

	// If we're hit by a laser beam, the real threat is the emitter that owns it.
	if (threat && horde::IsSpecialType(threat, horde::SpecialEntityTypeID::LASER_BEAM))
	{
		threat = threat->owner;
	}
	// If the inflictor is a projectile from a sentry gun, the sentry is the real threat.
	else if (threat && threat->owner && horde::IsSpecialType(threat->owner, horde::SpecialEntityTypeID::SENTRY_GUN))
	{
		threat = threat->owner;
	}

	// Now, check if the determined threat is a deployable we should target directly.
	if (threat && (horde::IsSpecialType(threat, horde::SpecialEntityTypeID::TESLA_MINE) ||
		horde::IsSpecialType(threat, horde::SpecialEntityTypeID::SENTRY_GUN) ||
		horde::IsSpecialType(threat, horde::SpecialEntityTypeID::LASER_EMITTER)))
	{
		new_tesla = MarkTeslaArea(targ, threat);

		// Sentry Gun or Laser Emitter logic
		if (horde::IsSpecialType(threat, horde::SpecialEntityTypeID::SENTRY_GUN) || horde::IsSpecialType(threat, horde::SpecialEntityTypeID::LASER_EMITTER))
		{
			// Check if enough time has passed since last sentrygun/emitter targeting
			if (level.time - targ->monsterinfo.last_reacttodamage_target_time > target_cooldown_react)
			{
				// Target the new threat if it's new, random, or we aren't already targeting a similar threat.
				if ((new_tesla || brandom()) && (!targ->enemy ||
					!(horde::IsSpecialType(targ->enemy, horde::SpecialEntityTypeID::SENTRY_GUN) || horde::IsSpecialType(targ->enemy, horde::SpecialEntityTypeID::LASER_EMITTER))))
				{
					TargetTesla(targ, threat);
					targ->monsterinfo.last_reacttodamage_target_time = level.time;
				}
			}
		}
		// Tesla Mine logic
		else if (horde::IsSpecialType(threat, horde::SpecialEntityTypeID::TESLA_MINE))
		{
			// Target the new threat if it's new, random, or we aren't already targeting a tesla.
			if ((new_tesla || brandom()) && (!targ->enemy || !horde::IsSpecialType(targ->enemy, horde::SpecialEntityTypeID::TESLA_MINE)))
				TargetTesla(targ, threat);
		}
		return;
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
			percentHealth = (float)(targ->health) / (float)(targ->max_health);
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

		percentHealth = (float)(targ->health) / (float)(targ->max_health);
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

void AssignMonsterTeam(edict_t* ent) {
	if ((ent->svflags & SVF_MONSTER) && ent->monsterinfo.team != CTF_TEAM1) {
		ent->monsterinfo.team = CTF_TEAM2;
	}
}

// Funciones auxiliares
bool CheckEntityClass(edict_t* ent, const char* className) {
	return ent->classname && !strcmp(ent->classname, className);
}

bool IsLaserEntity(edict_t* ent) {
	return ent->classname &&
		(!strcmp(ent->classname, "emitter") || !strcmp(ent->classname, "laser"));
}

bool CheckTeslaMineTeam(edict_t* mine, edict_t* other) {
	if (!mine->team)
		return false;

	const char* otherTeam;
	if (other->client)
		otherTeam = other->client->resp.ctf_team == CTF_TEAM1 ? TEAM1 : TEAM2;
	else if (other->svflags & SVF_MONSTER)
		otherTeam = other->monsterinfo.team == CTF_TEAM1 ? TEAM1 : TEAM2;
	else
		return false;

	return !strcmp(mine->team, otherTeam);
}

bool CheckTrapTeam(edict_t* trap, edict_t* other) {
	const char* otherTeam = other->client ?
		(other->client->resp.ctf_team == CTF_TEAM1 ? TEAM1 : TEAM2) :
		(other->monsterinfo.team == CTF_TEAM1 ? TEAM1 : TEAM2);

	return !strcmp(trap->team, otherTeam);
}

bool CheckLaserTeam(edict_t* laser, edict_t* other) {
	const char* otherTeam = other->client ?
		(other->client->resp.ctf_team == CTF_TEAM1 ? TEAM1 : TEAM2) :
		(other->team ? other->team : "neutral");

	return !strcmp(laser->team, otherTeam);
}

bool CheckTeamDamage(edict_t* targ, edict_t* attacker) {
	return !g_friendly_fire->integer && OnSameTeam(targ, attacker);
}

bool OnSameTeam(edict_t* ent1, edict_t* ent2)
{
	// Validaciones iniciales
	if (!ent1 || !ent2 || ent1 == ent2)
		return false;

	// Determinar el modo de juego
	enum GameMode {
		MODE_COOPERATIVE,
		MODE_TEAMPLAY,
		MODE_HORDE,
		MODE_OTHER
	};

	GameMode currentMode;

	if (G_IsCooperative())
		currentMode = MODE_COOPERATIVE;
	else if (G_TeamplayEnabled() && !g_horde->integer)
		currentMode = MODE_TEAMPLAY;
	else if (g_horde->integer)
		currentMode = MODE_HORDE;
	else
		currentMode = MODE_OTHER;

	switch (currentMode) {
	case MODE_COOPERATIVE:
		return (ent1->client && ent2->client);

	case MODE_TEAMPLAY:
		if (ent1->client && ent2->client)
			return ent1->client->resp.ctf_team == ent2->client->resp.ctf_team;

		if ((ent1->svflags & SVF_MONSTER) && (ent2->svflags & SVF_MONSTER)) {
			AssignMonsterTeam(ent1);
			AssignMonsterTeam(ent2);
			return ent1->monsterinfo.team == ent2->monsterinfo.team;
		}
		return false;

	case MODE_HORDE:
		// Verificar jugadores
		if (ent1->client && ent2->client)
			return ent1->client->resp.ctf_team == ent2->client->resp.ctf_team;

		// Verificar monstruos
		if ((ent1->svflags & SVF_MONSTER) && (ent2->svflags & SVF_MONSTER)) {
			AssignMonsterTeam(ent1);
			AssignMonsterTeam(ent2);
			return ent1->monsterinfo.team == ent2->monsterinfo.team;
		}

		// Verificar jugador vs monstruo
		if (ent1->client && (ent2->svflags & SVF_MONSTER))
			return ent1->client->resp.ctf_team == ent2->monsterinfo.team;

		if (ent2->client && (ent1->svflags & SVF_MONSTER))
			return ent2->client->resp.ctf_team == ent1->monsterinfo.team;

		// Verificar minas tesla
		if (CheckEntityClass(ent1, "tesla_mine"))
			return CheckTeslaMineTeam(ent1, ent2);

		if (CheckEntityClass(ent2, "tesla_mine"))
			return CheckTeslaMineTeam(ent2, ent1);

		// Verificar trampas
		if (CheckEntityClass(ent1, "food_cube_trap"))
			return CheckTrapTeam(ent1, ent2);

		if (CheckEntityClass(ent2, "food_cube_trap"))
			return CheckTrapTeam(ent2, ent1);

		// Verificar láseres y emisores
		if (IsLaserEntity(ent1))
			return CheckLaserTeam(ent1, ent2);

		if (IsLaserEntity(ent2))
			return CheckLaserTeam(ent2, ent1);

		return false;

	default:
		return false;
	}
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

	// Apply health regeneration with optimized checks and calculations
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
	// Fast path early returns for improved performance
	if (!attacker || !attacker->client || !g_iddmg || !g_iddmg->integer ||
		!attacker->client->pers.iddmg_state || !targ ||
		targ->monsterinfo.invincible_time > level.time) {
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

void HandleVampireEffect(edict_t* attacker, edict_t* targ, int damage) {
	// Fast path early exits with direct boolean checks for optimal branching
	if (!g_vampire || !g_vampire->integer || !attacker || !targ || damage <= 0) {
		return;
	}

	// Additional early return checks to avoid deeper nesting
	if (attacker == targ || OnSameTeam(targ, attacker) ||
		(targ->monsterinfo.invincible_time && targ->monsterinfo.invincible_time > level.time)) {
		return;
	}

	// Check if attacker can use vampire effect before proceeding
	if (!CanUseVampireEffect(attacker)) {
		return;
	}

	// Pre-calculate health stolen once
	float health_stolen = damage / 4.0f;
	//if (isSentrygun) {
	//	health_stolen *= VampireConfig::SENTRY_HEALING_FACTOR;
	//}

	// Only process healing if needed (attacker isn't at max health)
	if (attacker->health < attacker->max_health) {
		// Apply weapon-specific modifiers only if needed
		//if (!isSentrygun) {
		//	health_stolen = calculate_health_stolen(attacker, static_cast<int>(health_stolen));
		//}

		// Use direct clamping with std::min to simplify logic
		const float max_healing = static_cast<float>(VampireConfig::MAX_STORED_HEALING);
		attacker->regen_info.stored_healing = std::min(
			attacker->regen_info.stored_healing + health_stolen,
			max_healing
		);
	}

	// Process sentry healing only in applicable game states
	if ((attacker->svflags & SVF_PLAYER) && current_wave_level >= 10) {
		// Cache constants to avoid repeated access
		const float sentry_factor = VampireConfig::SENTRY_HEALING_FACTOR;
		const float max_stored = static_cast<float>(VampireConfig::MAX_STORED_HEALING);
		const float sentry_heal = health_stolen * sentry_factor;

		// Use traditional loop (replaces std::span)
		std::span entities_view{ g_edicts, globals.num_edicts };
		for (auto& ent : entities_view) {
			// Combined all conditions in a single if statement for better branch prediction
			if (!ent.inuse || ent.health <= 0 || ent.owner != attacker ||
				!ent.classname || horde::IsSpecialType(attacker, horde::SpecialEntityTypeID::SENTRY_GUN)) {
				continue;
			}

			// Single operation to update stored healing with clamping
			ent.regen_info.stored_healing = std::min(
				ent.regen_info.stored_healing + sentry_heal,
				max_stored
			);
		}
	}

	// Only apply armor vampire at level 2 - direct check
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
	const int64_t initial_health = targ->health; // Use int64_t here too for consistency
	// Note: 'take' is still int at this point, needs casting later if used directly with CalculateRealDamage
	// We calculate real_damage later after 'take' is determined.

    if ((targ->svflags & SVF_MONSTER) && targ->monsterinfo.IS_BOSS &&
        (mod.id == MOD_HANDGRENADE || mod.id == MOD_HG_SPLASH) &&
        (inflictor && inflictor->count == 1))
    {
        // It's a cluster grenade from an upgraded prox hitting a boss. Reduce damage by half.
        damage *= 0.2; // You can adjust this multiplier, e.g., 0.3 for 70% reduction.
    }

	if (!targ->takedamage)
		return;

	// Si es daño por agua, intentar teletransportar inmediatamente
	if (mod.id == MOD_WATER && (targ->svflags & SVF_MONSTER) && !targ->monsterinfo.IS_BOSS) {
		CheckAndTeleportStuckMonster(targ);
	}

	// Si el atacante es nulo, usamos el inflictor como atacante
	if (!attacker)
		attacker = inflictor;

	// Si aún así el atacante es nulo, usamos el mundo como atacante
	if (!attacker)
		attacker = world;

	if (attacker && (attacker->svflags & SVF_MONSTER)) {
		UpdatePowerUpTimes(attacker);
		damage *= M_DamageModifier(attacker);
	}

	if (g_instagib->integer && !g_horde->integer && attacker->client && targ->client) {

		// [Kex] always kill no matter what on instagib
		damage = 9999;
	}

	sphere_notified = false; // PGM

	// friendly fire avoidance
	// if enabled you can't hurt teammates (but you can hurt yourself)
	// knockback still occurs
	if ((targ != attacker) && !(dflags & DAMAGE_NO_PROTECTION))
	{
		// mark as friendly fire
		if (OnSameTeam(targ, attacker))
		{
			mod.friendly_fire = true;

			// if we're not a nuke & friendly fire is disabled, just kill the damage
			if (!g_friendly_fire->integer && (mod.id != MOD_TARGET_LASER) /*&& (mod.id != MOD_NUKE)*/) {
				damage = 0;
			}
			else if (attacker->svflags & SVF_MONSTER && targ->svflags & SVF_MONSTER && mod.id == MOD_TARGET_LASER)
			{
				damage = 0; // Monsters don't hurt each other with lasers
			}
		}
	}
	// Q2ETweaks self damage avoidance
	// if enabled you can't hurt yourself
	// knockback still occurs
	if ((targ == attacker) && !(dflags & DAMAGE_NO_PROTECTION))
	{
		// if we're not a nuke & self damage is disabled, just kill the damage
				//if (g_no_self_damage->integer && (mod.id != MOD_TARGET_LASER) && (mod.id != MOD_NUKE) && (mod.id != MOD_TRAP) && (mod.id != MOD_BARREL) && (mod.id != MOD_EXPLOSIVE) && (mod.id != MOD_DOPPLE_EXPLODE))
		if (g_no_self_damage->integer && (mod.id != MOD_TARGET_LASER) /*&& (mod.id != MOD_NUKE)*/ && (mod.id != MOD_BARREL) && (mod.id != MOD_EXPLOSIVE) /*&& (mod.id != MOD_DOPPLE_EXPLODE)*/)
			damage = 0;
	}
	//
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
	} // mal: just for debugging...

	client = targ->client;

	// PMM - defender sphere takes half damage
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

	// bonus damage for surprising a monster
	if (!(dflags & DAMAGE_RADIUS) && (targ->svflags & SVF_MONSTER) && (attacker->client) &&
		(!targ->enemy || targ->monsterinfo.surprise_time == level.time) && (targ->health > 0))
	{
		damage *= 2;
		targ->monsterinfo.surprise_time = level.time;
	}

	// ZOID
	// strength tech
	damage = CTFApplyStrength(attacker, damage);
	// ZOID

	if ((targ->flags & FL_NO_KNOCKBACK) ||
		((targ->flags & FL_ALIVE_KNOCKBACK_ONLY) && (!targ->deadflag || targ->dead_time != level.time)))
		knockback = 0;

	// figure momentum add
	if (!(dflags & DAMAGE_NO_KNOCKBACK))
	{
		if ((knockback) && (targ->movetype != MOVETYPE_NONE) && (targ->movetype != MOVETYPE_BOUNCE) &&
			(targ->movetype != MOVETYPE_PUSH) && (targ->movetype != MOVETYPE_STOP))
		{
			// CORRECTED: The 'dir' vector is not guaranteed to be normalized.
			// It must be normalized here to ensure correct knockback force.
			vec3_t normalized_dir = dir.normalized();
			vec3_t kvel;

			// Skip mass calculation for BFG pull
			if (g_bfgpull->integer && mod.id == MOD_BFG_LASER) {
				kvel = normalized_dir * (500.0f * knockback / 50);
			}
			else {
				float mass;
				if (targ->mass < 50)
					mass = 50;
				else
					mass = (float)targ->mass;

				if (targ->client && attacker == targ)
					kvel = normalized_dir * (1600.0f * knockback / mass);
				else
					kvel = normalized_dir * (500.0f * knockback / mass);
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
	// ROGUE
	if (!(dflags & DAMAGE_NO_PROTECTION) &&
		(((client && client->invincible_time > level.time)) ||
			((targ->svflags & SVF_MONSTER) && targ->monsterinfo.invincible_time > level.time)))
		// ROGUE
	{
		if (targ->pain_debounce_time < level.time)
		{
			gi.sound(targ, CHAN_ITEM, gi.soundindex("items/protect4.wav"), 1, ATTN_NORM, 0);
			targ->pain_debounce_time = level.time + 2_sec;
		}
		take = 0;
		save = damage;
	}

	// Handle Horde Bonus Stuff
	// Simpler and clearer
	if (attacker && attacker->client &&              // 1. Check if attacker and client exist
		(g_horde->integer == 0 ||                    // 2. Either not in horde mode
			!ClientIsSpectating(attacker->client)))      // 3. Or attacker is not spectating 
	{
		HandleAutoHaste(attacker, targ, damage);
		HandleVampireEffect(attacker, targ, damage);
		// Calculate real_damage here, after 'take' has been potentially modified
		const int64_t real_damage = CalculateRealDamage(targ, static_cast<int64_t>(take), initial_health);
		// Cast real_damage back to int if HandleIDDamage expects int (assuming it does based on its definition)
		HandleIDDamage(attacker, targ, static_cast<int>(real_damage));
		// REMOVED: ProcessDamage(targ, attacker, take); // Call removed as function is now redundant/empty
	}

	// ZOID
	// team armor protect
	if (G_TeamplayEnabled() && targ->client && attacker->client &&
		targ->client->resp.ctf_team == attacker->client->resp.ctf_team && targ != attacker &&
		g_teamplay_armor_protect->integer)
	{
		psave = asave = 0;
	}
	else
	{
		// ZOID
		psave = CheckPowerArmor(targ, point, normal, take, dflags);
		take -= psave;

		asave = CheckArmor(targ, point, normal, take, te_sparks, dflags);
		take -= asave;
	}

	// treat cheat/powerup savings the same as armor
	asave += save;

	// ZOID
	// resistance tech
	take = CTFApplyResistance(targ, take);
	// ZOID

	// ZOID
	//CTFCheckHurtCarrier(targ, attacker);
	// ZOID

	// ROGUE - this option will do damage both to the armor and person. originally for DPU rounds
	if (dflags & DAMAGE_DESTROY_ARMOR)
	{
		if (!(targ->flags & FL_GODMODE) && !(dflags & DAMAGE_NO_PROTECTION) &&
			!(client && client->invincible_time > level.time))
		{
			take = damage;
		}
	}
	// ROGUE

	// [Paril-KEX] player hit markers
	if (targ != attacker && attacker && attacker->client && targ->health > 0 &&
		!((targ && targ->svflags & SVF_DEADMONSTER) || (targ->flags & FL_NO_DAMAGE_EFFECTS)) &&
		mod.id != MOD_TARGET_LASER &&
		!(attacker && attacker->movetype == MOVETYPE_NOCLIP) &&
		targ->monsterinfo.invincible_time <= level.time)
	{
		attacker->client->ps.stats[STAT_HIT_MARKER] += take + psave + asave;
	}

	// do the damage
	if (take)
	{
		if (!(targ->flags & FL_NO_DAMAGE_EFFECTS))
		{
			// ROGUE
			if (targ->flags & FL_MECHANICAL)
				SpawnDamage(TE_ELECTRIC_SPARKS, point, normal, take);
			// ROGUE
			else if ((targ->svflags & SVF_MONSTER) || (client))
			{
				// XATRIX
				if (strcmp(targ->classname, "monster_gekk") == 0)
					SpawnDamage(TE_GREENBLOOD, point, normal, take);
				// XATRIX
				// ROGUE
				else if (mod.id == MOD_CHAINFIST)
					SpawnDamage(TE_MOREBLOOD, point, normal, 255);
				// ROGUE
				else
					//  SpawnDamage(TE_BLOOD, point, normal, take);
					SpawnDamage(TE_BLOOD, point, normal, take);
			}
			else
				SpawnDamage(te_sparks, point, normal, take);
		}

		if (!CTFMatchSetup())
			targ->health = targ->health - take;

		// Stygian Vampirism for non-boss, non-friendly monsters
		if (attacker && (attacker->svflags & SVF_MONSTER) && 
			(attacker->monsterinfo.bonus_flags & BF_STYGIAN) && 
			!(attacker->monsterinfo.bonus_flags & BF_FRIENDLY) /*&& 
			!attacker->monsterinfo.IS_BOSS*/ && 
			take > 0 && attacker->health > 0) 
		{
			constexpr float VAMP_FACTOR = 0.3f; // Heal for 30% of damage dealt
			int heal_amount = std::max(1, static_cast<int>(take * VAMP_FACTOR));
			attacker->health = std::min(attacker->health + heal_amount, attacker->max_health);
		}


		if ((targ->flags & FL_IMMORTAL) && targ->health <= 0)
			targ->health = 1;

		// PGM - spheres need to know who to shoot at
		if (client && client->owned_sphere)
		{
			sphere_notified = true;
			if (client->owned_sphere->pain)
				client->owned_sphere->pain(client->owned_sphere, attacker, 0, 0, mod);
		}
		// PGM

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

			// Safety checks for all parameters
			if (!targ || !inflictor || !attacker) {
				return;
			}

			// Initialize monsterinfo if necessary
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

	// PGM - spheres need to know who to shoot at
	if (!sphere_notified)
	{
		if (client && client->owned_sphere)
		{
			sphere_notified = true;
			if (client->owned_sphere->pain)
				client->owned_sphere->pain(client->owned_sphere, attacker, 0, 0, mod);
		}
	}
	// PGM

	if (targ && targ->client) {
		targ->client->last_attacker_time = level.time;
		// Update last_damage only if damage is greater than 0 and not invincible/immune
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

	// add to the damage inflicted on a player this frame
	// the total will be turned into screen blends and view angle kicks
	// at the end of the frame
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
				// for projectile direct hits, use the attacker; otherwise
				// use the inflictor (rocket splash should point to the rocket)
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