// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_newdm.c
// pmack
// june 1998

#include "../g_local.h"
#include "../m_player.h"

dm_game_rt DMGame;

//=================
//=================
constexpr item_flags_t IF_TYPE_MASK = (IF_WEAPON | IF_AMMO | IF_POWERUP | IF_ARMOR | IF_KEY);

bool Pickup_Health(edict_t* ent, edict_t* other);
bool Pickup_Armor(edict_t* ent, edict_t* other);
bool Pickup_PowerArmor(edict_t* ent, edict_t* other);

inline item_flags_t GetSubstituteItemFlags(item_id_t id)
{
	const gitem_t* item = GetItemByIndex(id);

	// we want to stay within the item class
	item_flags_t flags = item->flags & IF_TYPE_MASK;

	if ((flags & (IF_WEAPON | IF_AMMO)) == (IF_WEAPON | IF_AMMO))
		flags = IF_AMMO;
	// Adrenaline and Mega Health count as powerup
	else if (id == IT_ITEM_ADRENALINE || id == IT_HEALTH_MEGA)
		flags = IF_POWERUP;

	return flags;
}

inline item_id_t FindSubstituteItem(edict_t* ent)
{
	// never replace flags
	if (ent->item->id == IT_FLAG1 || ent->item->id == IT_FLAG2 || ent->item->id == IT_ITEM_TAG_TOKEN)
		return IT_NULL;

	// stimpack/shard randomizes
	if (ent->item->id == IT_HEALTH_SMALL ||
		ent->item->id == IT_ARMOR_SHARD)
		return brandom() ? IT_HEALTH_SMALL : IT_ARMOR_SHARD;

	// health is special case
	if (ent->item->id == IT_HEALTH_MEDIUM ||
		ent->item->id == IT_HEALTH_LARGE)
	{
		float rnd = frandom();

		if (rnd < 0.6f)
			return IT_HEALTH_MEDIUM;
		else
			return IT_HEALTH_LARGE;
	}
	// armor is also special case
	else if (ent->item->id == IT_ARMOR_JACKET ||
		ent->item->id == IT_ARMOR_COMBAT ||
		ent->item->id == IT_ARMOR_BODY ||
		ent->item->id == IT_ITEM_POWER_SCREEN ||
		ent->item->id == IT_ITEM_POWER_SHIELD)
	{
		float rnd = frandom();

		if (rnd < 0.4f)
			return IT_ARMOR_JACKET;
		else if (rnd < 0.6f)
			return IT_ARMOR_COMBAT;
		else if (rnd < 0.8f)
			return IT_ARMOR_BODY;
		else if (rnd < 0.9f)
			return IT_ITEM_POWER_SCREEN;
		else
			return IT_ITEM_POWER_SHIELD;
	}

	item_flags_t myflags = GetSubstituteItemFlags(ent->item->id);

	std::array<item_id_t, MAX_ITEMS> possible_items;
	size_t possible_item_count = 0;

	// gather matching items
	for (item_id_t i = static_cast<item_id_t>(IT_NULL + 1); i < IT_TOTAL; i = static_cast<item_id_t>(static_cast<int32_t>(i) + 1))
	{
		const gitem_t* it = GetItemByIndex(i);
		item_flags_t itflags = it->flags;

		if (!itflags || (itflags & (IF_NOT_GIVEABLE | IF_TECH | IF_NOT_RANDOM)) || !it->pickup || !it->world_model)
			continue;

		// don't respawn spheres if they're dmflag disabled.
		if (g_no_spheres->integer)
		{
			if (i == IT_ITEM_SPHERE_VENGEANCE ||
				i == IT_ITEM_SPHERE_HUNTER ||
				i == IT_ITEM_SPHERE_DEFENDER)
			{
				continue;
			}
		}

		if (g_no_nukes->integer && i == IT_AMMO_NUKE)
			continue;

		if (g_no_mines->integer &&
			(i == IT_AMMO_PROX || i == IT_AMMO_TESLA || i == IT_AMMO_TRAP || i == IT_WEAPON_PROXLAUNCHER))
			continue;

		itflags = GetSubstituteItemFlags(i);

		if ((itflags & IF_TYPE_MASK) == (myflags & IF_TYPE_MASK))
			possible_items[possible_item_count++] = i;
	}

	if (!possible_item_count)
		return IT_NULL;

	return possible_items[irandom(possible_item_count)];
}

//=================
//=================
item_id_t DoRandomRespawn(edict_t* ent)
{
	if (!ent->item)
		return IT_NULL; // why

	item_id_t id = FindSubstituteItem(ent);

	if (id == IT_NULL)
		return IT_NULL;

	return id;
}

//=================
//=================
void PrecacheForRandomRespawn()
{
	gitem_t* it;
	int		 i;
	int		 itflags;

	it = itemlist;
	for (i = 0; i < IT_TOTAL; i++, it++)
	{
		itflags = it->flags;

		if (!itflags || (itflags & (IF_NOT_GIVEABLE | IF_TECH | IF_NOT_RANDOM)) || !it->pickup || !it->world_model)
			continue;

		PrecacheItem(it);
	}
}
// ***************************
//  SPAWN TURRET LOGIC
// ***************************

// Función principal para colocar la torreta
bool fire_sentrygun(edict_t* ent, const vec3_t& start, const vec3_t& aimdir, float distance, float height)
{
	if (!ent || !ent->client)
		return false;

	const vec3_t mins{ -16, -16, -24 };
	const vec3_t maxs{ 16, 16, 32 };

	// Calcular posición final
	auto dir = vectoangles(aimdir);
	vec3_t end = start + (aimdir * distance);

	// Verificar espacio para la torreta
	trace_t tr = gi.trace(start, mins, maxs, end, ent, CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER);
	vec3_t new_start = tr.endpos;
	new_start.z += height;

	tr = gi.trace(new_start, mins, maxs, new_start, nullptr, CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER);
	if (tr.startsolid || tr.allsolid) {
		gi.Client_Print(ent, PRINT_HIGH, "Cannot place turret here.\n");
		return false;
	}

	// Crear y configurar la torreta
	edict_t* turret = G_Spawn();
	turret->classname = "monster_sentrygun";
	turret->s.origin = new_start;
	turret->s.angles = dir;
	turret->owner = ent;

	// Configurar equipo
	if (ent->client->resp.ctf_team == CTF_TEAM1)
		turret->monsterinfo.team = CTF_TEAM1;
	else if (ent->client->resp.ctf_team == CTF_TEAM2)
		turret->monsterinfo.team = CTF_TEAM2;

	// Spawner usando la lógica existente de monster_sentrygun
	ED_CallSpawn(turret);


	// Efecto visual de spawn
	SpawnGrow_Spawn(turret->s.origin, 24.f, 48.f);

	return true;
}

void Tag_GameInit();
void Tag_PostInitSetup();
void Tag_PlayerDeath(edict_t* targ, edict_t* inflictor, edict_t* attacker);
void Tag_Score(edict_t* attacker, edict_t* victim, int scoreChange, const mod_t& mod);
void Tag_PlayerEffects(edict_t* ent);
void Tag_DogTag(edict_t* ent, edict_t* killer, const char** pic);
void Tag_PlayerDisconnect(edict_t* ent);
int	 Tag_ChangeDamage(edict_t* targ, edict_t* attacker, int damage, mod_t mod);

void DBall_GameInit();
void DBall_ClientBegin(edict_t* ent);
bool DBall_SelectSpawnPoint(edict_t* ent, vec3_t& origin, vec3_t& angles, bool force_spawn);
int	 DBall_ChangeKnockback(edict_t* targ, edict_t* attacker, int knockback, mod_t mod);
int	 DBall_ChangeDamage(edict_t* targ, edict_t* attacker, int damage, mod_t mod);
void DBall_PostInitSetup();
int	 DBall_CheckDMRules();

// ****************************
// General DM Stuff
// ****************************

void InitGameRules()
{
	// clear out the game rule structure before we start
	memset(&DMGame, 0, sizeof(dm_game_rt));

	if (gamerules->integer)
	{
		switch (gamerules->integer)
		{
		case RDM_TAG:
			DMGame.GameInit = Tag_GameInit;
			DMGame.PostInitSetup = Tag_PostInitSetup;
			DMGame.PlayerDeath = Tag_PlayerDeath;
			DMGame.Score = Tag_Score;
			DMGame.PlayerEffects = Tag_PlayerEffects;
			DMGame.DogTag = Tag_DogTag;
			DMGame.PlayerDisconnect = Tag_PlayerDisconnect;
			DMGame.ChangeDamage = Tag_ChangeDamage;
			break;
		case RDM_DEATHBALL:
			DMGame.GameInit = DBall_GameInit;
			DMGame.ChangeKnockback = DBall_ChangeKnockback;
			DMGame.ChangeDamage = DBall_ChangeDamage;
			DMGame.ClientBegin = DBall_ClientBegin;
			DMGame.SelectSpawnPoint = DBall_SelectSpawnPoint;
			DMGame.PostInitSetup = DBall_PostInitSetup;
			DMGame.CheckDMRules = DBall_CheckDMRules;
			break;
			// reset gamerules if it's not a valid number
		default:
			gi.cvar_forceset("gamerules", "0");
			break;
		}
	}

	// if we're set up to play, initialize the game as needed.
	if (DMGame.GameInit)
		DMGame.GameInit();
}