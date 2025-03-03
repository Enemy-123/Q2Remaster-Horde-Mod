// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_newdm.c
// pmack
// june 1998

#include "../g_local.h"
#include "../m_player.h"
#include "../shared.h"

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
		float const rnd = frandom();

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
		float const rnd = frandom();

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

	item_flags_t const myflags = GetSubstituteItemFlags(ent->item->id);

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

	item_id_t const id = FindSubstituteItem(ent);

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
//  DOPPLEGANGER
// ***************************

edict_t* Sphere_Spawn(edict_t* owner, spawnflags_t spawnflags);

DIE(doppleganger_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	edict_t* sphere;
	float	 dist;
	vec3_t	 dir;

	if ((self->enemy) && (self->enemy != self->teammaster))
	{
		dir = self->enemy->s.origin - self->s.origin;
		dist = dir.length();

		if (dist > 80.f)
		{
			if (dist > 768)
			{
				sphere = Sphere_Spawn(self, SPHERE_HUNTER | SPHERE_DOPPLEGANGER);
				sphere->pain(sphere, attacker, 0, 0, mod);
			}
			else
			{
				sphere = Sphere_Spawn(self, SPHERE_VENGEANCE | SPHERE_DOPPLEGANGER);
				sphere->pain(sphere, attacker, 0, 0, mod);
			}
		}
	}

	self->takedamage = DAMAGE_NONE;

	// [Paril-KEX]
	T_RadiusDamage(self, self->teammaster, 160.f, self, 140.f, DAMAGE_NONE, MOD_DOPPLE_EXPLODE);

	if (self->teamchain)
		BecomeExplosion1(self->teamchain);
	BecomeExplosion1(self);
}

PAIN(doppleganger_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	self->enemy = other;
}

THINK(doppleganger_timeout) (edict_t* self) -> void
{
	doppleganger_die(self, self, self, 9999, self->s.origin, MOD_UNKNOWN);
}

THINK(body_think) (edict_t* self) -> void
{
	float r;

	if (fabsf(self->ideal_yaw - anglemod(self->s.angles[YAW])) < 2)
	{
		if (self->timestamp < level.time)
		{
			r = frandom();
			if (r < 0.10f)
			{
				self->ideal_yaw = frandom(350.0f);
				self->timestamp = level.time + 1_sec;
			}
		}
	}
	else
		M_ChangeYaw(self);

	if (self->teleport_time <= level.time)
	{
		self->s.frame++;
		if (self->s.frame > FRAME_stand40)
			self->s.frame = FRAME_stand01;

		self->teleport_time = level.time + 10_hz;
	}

	self->nextthink = level.time + FRAME_TIME_MS;
}

void fire_doppleganger(edict_t* ent, const vec3_t& start, const vec3_t& aimdir)
{
	edict_t* base;
	edict_t* body;
	vec3_t	 dir;
	vec3_t	 forward, right, up;
	int		 number;

	dir = vectoangles(aimdir);
	AngleVectors(dir, forward, right, up);

	base = G_Spawn();
	base->s.origin = start;
	base->s.angles = dir;
	base->movetype = MOVETYPE_TOSS;
	base->solid = SOLID_BBOX;
	base->s.renderfx |= RF_IR_VISIBLE;
	base->s.angles[PITCH] = 0;
	base->mins = { -16, -16, -24 };
	base->maxs = { 16, 16, 32 };
	base->s.modelindex = gi.modelindex("models/objects/dopplebase/tris.md2");
	base->s.alpha = 0.1f;
	base->teammaster = ent;
	base->flags |= (FL_DAMAGEABLE | FL_TRAP);
	base->takedamage = true;
	base->health = 30;
	base->pain = doppleganger_pain;
	base->die = doppleganger_die;

	base->nextthink = level.time + 30_sec;
	base->think = doppleganger_timeout;

	base->classname = "doppleganger";

	base->monsterinfo.issummoned = true;

	gi.linkentity(base);

	body = G_Spawn();
	number = body->s.number;
	body->s = ent->s;
	body->s.sound = 0;
	body->s.event = EV_NONE;
	body->s.number = number;
	body->yaw_speed = 30;
	body->ideal_yaw = 0;
	body->s.origin = start;
	body->s.origin[2] += 8;
	body->teleport_time = level.time + 10_hz;
	body->think = body_think;
	body->nextthink = level.time + FRAME_TIME_MS;
	body->monsterinfo.issummoned = true;
	gi.linkentity(body);

	base->teamchain = body;
	body->teammaster = base;

	// [Paril-KEX]
	body->owner = ent;
	gi.sound(body, CHAN_AUTO, gi.soundindex("medic_commander/monsterspawn1.wav"), 1.f, ATTN_NORM, 0.f);
}

// ***************************
//  SPAWN TURRET LOGIC
// ***************************

// Función principal para colocar la torreta
bool fire_sentrygun(edict_t* ent, const vec3_t& start, const vec3_t& aimdir, float distance, float height)
{
	if (!ent || !ent->client)
		return false;

	// Constantes
	constexpr vec3_t MINS = { -16, -16, -24 };
	constexpr vec3_t MAXS = { 16, 16, 32 };
	constexpr float HEIGHT_OFFSET = 8.0f;
	constexpr int MAX_ATTEMPTS = 8;
	constexpr float MIN_HEIGHT = 50.0f;  // Altura mínima requerida

	// Asegurarnos que la altura no sea menor que el mínimo
	height = max(height, MIN_HEIGHT);

	// Función auxiliar para verificar si una posición es válida
	auto trySpawnPosition = [&](const vec3_t& spawn_origin, const vec3_t& spawn_angles) -> bool {
		// Verificar espacio libre en diferentes alturas
		for (float const height_test : {0.0f, HEIGHT_OFFSET, -HEIGHT_OFFSET}) {
			vec3_t test_origin = spawn_origin;
			test_origin.z += height_test;

			// Asegurarnos que respetamos la altura mínima
			if (test_origin.z - ent->s.origin.z < MIN_HEIGHT)
				continue;

			// Trazar línea desde el jugador hasta la posición de spawn
			trace_t const trace = gi.traceline(ent->s.origin, test_origin, ent, MASK_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER);
			if (trace.fraction < 1.0f)
				continue;

			// Verificar espacio suficiente
			if (!CheckSpawnPoint(test_origin, MINS, MAXS))
				continue;

			// Si llegamos aquí, encontramos una posición válida
			edict_t* turret = G_Spawn();
			if (!turret)
				return false;

			turret->monsterinfo.issummoned = true;
			//turret->classname = "monster_hover";
			turret->classname = "monster_sentrygun";
			turret->s.origin = test_origin;
			turret->s.angles = spawn_angles;
			turret->owner = ent;
			turret->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

			ApplyMonsterBonusFlags(turret);
			ED_CallSpawn(turret);

			if (turret->inuse) {
				// Efecto visual de spawn
				SpawnGrow_Spawn(test_origin, 24.f, 48.f);
				return true;
			}

			G_FreeEdict(turret);
			return false;
		}
		return false;
		};

	// Calcular posición final intentada
	vec3_t angles = vectoangles(aimdir);
	angles = vec3_t{ 0, angles.y, 0 };  // Solo preservamos el yaw
	vec3_t end = start + (aimdir * distance);
	end.z += height;  // Aplicar la altura deseada

	// Primer intento en la posición deseada
	if (trySpawnPosition(end, angles))
		return true;

	// Si falla, intentar posiciones alternativas en un radio
	constexpr float RADIUS_MIN = 50.0f;
	constexpr float RADIUS_MAX = 100.0f;

	for (int i = 0; i < MAX_ATTEMPTS; i++) {
		float spawn_angle = frandom(2.0f * PIf);
		float const radius = frandom(RADIUS_MIN, RADIUS_MAX);
		vec3_t const offset{
			cosf(spawn_angle) * radius,
			sinf(spawn_angle) * radius,
			0  // No añadimos altura extra aquí ya que end ya tiene la altura correcta
		};

		vec3_t const spawn_origin = end + offset;
		vec3_t spawn_angles = angles;
		spawn_angles[YAW] += spawn_angle * (180.0f / PIf);

		if (trySpawnPosition(spawn_origin, spawn_angles))
			return true;
	}

	gi.Client_Print(ent, PRINT_HIGH, "Cannot place turret here.\n");
	return false;
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