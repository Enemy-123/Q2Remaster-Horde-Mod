// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "../g_local.h"
#include <sstream>

static const int MAX_MONSTERS_BIG_MAP = 44;
static const int MAX_MONSTERS_MEDIUM_MAP = 18;
static const int MAX_MONSTERS_SMALL_MAP = 15;

int remainingMonsters = 0;
int current_wave_number = 1;
const int BOSS_TO_SPAWN = 1;
cvar_t* g_horde;
enum class horde_state_t
{
	warmup,
	spawning,
	cleanup,
	rest
};

static struct {
	gtime_t         warm_time = 5_sec;
	horde_state_t   state = horde_state_t::warmup;

	gtime_t         monster_spawn_time = 0.5_sec;
	int32_t         num_to_spawn;
	int32_t         level;
} g_horde_local;

bool next_wave_message_sent = false;

void IsMapSize(const std::string& mapname, bool& isSmallMap, bool& isBigMap, bool& isMediumMap) {
    if (isSmallMap) {
        isMediumMap = false;
        isBigMap = false;
    }
    else if (isBigMap) {
        isMediumMap = false;
        isSmallMap = false;
    }
    else {
        isMediumMap = true;
        isSmallMap = false;
        isBigMap = false;
    }
}
bool isMediumMap = true;
bool isSmallMap = false;
bool isBigMap = false;
static void Horde_InitLevel(int32_t lvl)
{
	current_wave_number++;
	g_horde_local.level = lvl;
	g_horde_local.monster_spawn_time = level.time + random_time(0.5_sec, 0.9_sec); // wea debug

	isSmallMap = false;
	isMediumMap = false;
	isBigMap = false;

	IsMapSize(level.mapname, isSmallMap, isBigMap, isMediumMap);

	if (g_horde_local.level == 5) {
		gi.cvar_set("g_vampire", "1");
		//	gi.cvar_set("g_damage_scale", "1.5");
		gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\n\nYou're covered in blood!\n\n\nVampire Ability\nENABLED!\n");

		next_wave_message_sent = false;
	}

	if (g_horde_local.level == 10) {
		//		gi.cvar_set("g_damage_scale", "1.8");
		//		gi.cvar_set("ai_damage_scale", "1.5");
		gi.cvar_set("g_ammoregen", "1");
		gi.sound(world, CHAN_VOICE, gi.soundindex("misc/keyuse.wav"), 1, ATTN_NONE, 0);
		gi.LocBroadcast_Print(PRINT_CENTER, "\n\nAMMO REGEN\n\nENABLED!\n");
	}

	if (g_horde_local.level == 15) {
		gi.LocBroadcast_Print(PRINT_CENTER, "\n\n TIME ACCEL IS RUNNING THROUGHT YOUR VEINS \nEACH KILL WHILE ACCEL\nGIVES 0.5 EXTRA SECONDS!\n");
	}

	if (g_horde_local.level == 20) {
		gi.cvar_set("g_damage_scale", "2.0");
	}

	// Lógica para determinar el tamaño del mapa
	if (!Q_strcasecmp(level.mapname, "q2dm3") ||
		!Q_strcasecmp(level.mapname, "q2dm7") ||
		!Q_strcasecmp(level.mapname, "q2dm2") ||
		!Q_strcasecmp(level.mapname, "q2ctf4") ||
		!Q_strcasecmp(level.mapname, "dm10") ||
		!Q_strcasecmp(level.mapname, "q64/dm10") ||
		!Q_strcasecmp(level.mapname, "q64\\dm10") ||
		!Q_strcasecmp(level.mapname, "q64/dm9") ||
		!Q_strcasecmp(level.mapname, "q64\\dm9") ||
		!Q_strcasecmp(level.mapname, "q64/dm7") ||
		!Q_strcasecmp(level.mapname, "q64\\dm7") ||
		!Q_strcasecmp(level.mapname, "q64/dm2") ||
		!Q_strcasecmp(level.mapname, "q64\\dm2") ||
		!Q_strcasecmp(level.mapname, "fact3") ||
		!Q_strcasecmp(level.mapname, "q2ctf4") ||
		!Q_strcasecmp(level.mapname, "mgu3m4") ||
		!Q_strcasecmp(level.mapname, "mgu4trial") ||
		!Q_strcasecmp(level.mapname, "mgu6trial") ||
		!Q_strcasecmp(level.mapname, "mgdm1")) {
		isSmallMap = true;
		isMediumMap = false;
		isBigMap = false;
	}
	else if
		(!Q_strcasecmp(level.mapname, "q2ctf5") ||
			!Q_strcasecmp(level.mapname, "old/kmdm3") ||
			!Q_strcasecmp(level.mapname, "xdm2") ||
			!Q_strcasecmp(level.mapname, "xdm6")) {
		isBigMap = true;
		isMediumMap = false;
		isSmallMap = false;
	}


	// Declaración de ent fuera del bucle
	edict_t* ent;

	// Ciclo a través de jugadores
	for (uint32_t player = 1; player <= game.maxclients; player++)
	{
		ent = &g_edicts[player];

		// Contar jugadores activos
		int numActiveHPlayers = 0;
		for (auto player : active_players()) {
			numActiveHPlayers++;
		}

		// Ajustar los valores según el tipo de mapa y la cantidad de jugadores activos


		if (isSmallMap) { // Horde Monsters to Spawn
			g_horde_local.num_to_spawn = 8 + (lvl * 1);
			if (g_horde_local.num_to_spawn > MAX_MONSTERS_SMALL_MAP) {
				g_horde_local.num_to_spawn = MAX_MONSTERS_SMALL_MAP;
			}
			if (g_chaotic->integer && !g_insane->integer) {
				g_horde_local.num_to_spawn += numActiveHPlayers; // Aumentar el número de monstruos si chaotic->integer es verdadero
			}
		}
		else if (isBigMap) {
			g_horde_local.num_to_spawn = 22 + (lvl * 2);
			if (g_horde_local.num_to_spawn > MAX_MONSTERS_BIG_MAP) {
				g_horde_local.num_to_spawn = MAX_MONSTERS_BIG_MAP;
			}
			if (g_chaotic->integer && !g_insane->integer) { //  && current_wave_number > 5 
				g_horde_local.num_to_spawn += (numActiveHPlayers + 7); // Aumentar el número de monstruos si chaotic->integer es verdadero
			}
		}
		else {
			g_horde_local.num_to_spawn = 10 + (lvl * 1.2);
			if (g_horde_local.num_to_spawn > MAX_MONSTERS_MEDIUM_MAP) {
				g_horde_local.num_to_spawn = MAX_MONSTERS_MEDIUM_MAP;
			}
			if (g_chaotic->integer && !g_insane->integer) { // && current_wave_number > 7
				g_horde_local.num_to_spawn += (numActiveHPlayers + 3); // Aumentar el número de monstruos si chaotic->integer es verdadero
			}
		}
	}
}
bool G_IsDeathmatch()
{
	return deathmatch->integer && g_horde->integer;
}

bool G_IsCooperative()
{
	return coop->integer;
}

struct weighted_item_t;

using weight_adjust_func_t = void(*)(const weighted_item_t& item, float& weight);

void adjust_weight_health(const weighted_item_t& item, float& weight);
void adjust_weight_weapon(const weighted_item_t& item, float& weight);
void adjust_weight_ammo(const weighted_item_t& item, float& weight);
void adjust_weight_armor(const weighted_item_t& item, float& weight);
void adjust_weight_powerup(const weighted_item_t& item, float& weight);

constexpr struct weighted_item_t {
	const char* classname;
	int32_t					min_level = -1, max_level = -1;
	float					weight = 1.0f;
	weight_adjust_func_t	adjust_weight = nullptr;
} items[] = {
//	{ "item_health_small", -1, 9, 0.26f, adjust_weight_health },
//	{ "item_health", -1, -1, 0.20f, adjust_weight_health },
	{ "item_health_large", -1, -1, 0.25f, adjust_weight_health },
	{ "item_health_mega", -1, -1, 0.09f, adjust_weight_health },
	{ "item_adrenaline", -1, -1, 0.2f, adjust_weight_health },

//	{ "item_armor_shard", -1, 9, 0.26f, adjust_weight_armor },
	{ "item_armor_jacket", -1, 4, 0.35f, adjust_weight_armor },
	{ "item_armor_combat", 6, -1, 0.12f, adjust_weight_armor },
	{ "item_armor_body", 8, -1, 0.1f, adjust_weight_armor },
	{ "item_power_screen", 2, 8, 0.03f, adjust_weight_armor },
	{ "item_power_shield", 9, -1, 0.07f, adjust_weight_armor },

	{ "item_quad", 6, -1, 0.08f, adjust_weight_powerup },
	{ "item_double", 5, -1, 0.11f, adjust_weight_powerup },
	{ "item_quadfire", 4, -1, 0.012f, adjust_weight_powerup },
	{ "item_invulnerability", 4, -1, 0.051f, adjust_weight_powerup },
	{ "item_sphere_defender", -1, -1, 0.1f, adjust_weight_powerup },
	{ "item_sphere_hunter", 9, -1, 0.06f, adjust_weight_powerup },
	{ "item_invisibility", 4, -1, 0.08f, adjust_weight_powerup },
//	{ "item_doppleganger", 6, -1, 0.06f, adjust_weight_powerup },

	{ "weapon_chainfist", -1, 2, 0.12f, adjust_weight_weapon },
	{ "weapon_shotgun", -1, -1, 0.27f, adjust_weight_weapon },
	{ "weapon_supershotgun", 4, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_machinegun", -1, -1, 0.29f, adjust_weight_weapon },
	{ "weapon_etf_rifle", 3, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_boomer", 4, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_chaingun", 5, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_grenadelauncher", 6, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_proxlauncher", 8, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_hyperblaster", 7, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_phalanx", 10, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_rocketlauncher", 6, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_railgun", 9, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_plasmabeam", 7, -1, 0.19f, adjust_weight_weapon },
	{ "weapon_disintegrator", 14, -1, 0.15f, adjust_weight_weapon },
	{ "weapon_bfg", 12, -1, 0.15f, adjust_weight_weapon },

	{ "ammo_trap", 5, -1, 0.15f, adjust_weight_ammo },
	{ "ammo_shells", -1, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_bullets", -1, -1, 0.30f, adjust_weight_ammo },
	{ "ammo_flechettes", 5, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_grenades", -1, -1, 0.35f, adjust_weight_ammo },
	{ "ammo_prox", 7, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_tesla", 4, -1, 0.15f, adjust_weight_ammo },
	{ "ammo_cells", 5, -1, 0.30f, adjust_weight_ammo },
	{ "ammo_magslug", 9, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_slugs", 7, -1, 0.25f, adjust_weight_ammo },
	{ "ammo_disruptor", 12, -1, 0.24f, adjust_weight_ammo },
	{ "ammo_rockets", 7, -1, 0.25f, adjust_weight_ammo },
	{ "item_bandolier", 4, -1, 0.32f, adjust_weight_ammo },
	{ "item_pack", 8, -1, 0.34f, adjust_weight_ammo },
};

void adjust_weight_health(const weighted_item_t& item, float& weight)
{
}

void adjust_weight_weapon(const weighted_item_t& item, float& weight)
{
}

void adjust_weight_ammo(const weighted_item_t& item, float& weight)
{
}

void adjust_weight_armor(const weighted_item_t& item, float& weight)
{
}

void adjust_weight_powerup(const weighted_item_t& item, float& weight)
{
}

constexpr weighted_item_t monsters[] = {
{ "monster_soldier_light", -1, 18, 0.55f },
{ "monster_soldier_ss", -1, 7, 0.55f },
{ "monster_soldier", -1, 4, 0.45f },
{ "monster_soldier_hypergun", 2, 7, 0.55f },
{ "monster_soldier_lasergun", 3, 9, 0.45f },
{ "monster_soldier_ripper", 3, 7, 0.45f },

{ "monster_infantry2", 2, 13, 0.36f },
{ "monster_infantry", 8, -1, 0.36f },

{ "monster_flyer", -1, 19, 0.14f },
{ "monster_hover2", 5, 12, 0.18f },

{ "monster_gekk", 3, 12, 0.22f },

{ "monster_gunner2", 3, 11, 0.35f },
{ "monster_gunner", 8, -1, 0.34f },

{ "monster_medic", 5, 12, 0.1f },
{ "monster_brain", 6, -1, 0.23f }, 
{ "monster_stalker", 4, 11, 0.13f },
{ "monster_parasite", 4, 14, 0.2f },
{ "monster_tank", 14, -1, 0.3f },  
{ "monster_tank2", 5, 13, 0.3f },  
{ "monster_guncmdr2", 6, 10, 0.18f },
{ "monster_mutant", 5, 18, 0.55f },
{ "monster_chick", 6, 18, 0.7f },
{ "monster_chick_heat", 10, -1, 0.7f },
{ "monster_berserk", 8, -1, 0.45f },
{ "monster_floater", 9, 16, 0.13f },
{ "monster_hover", 11, -1, 0.23f }, 
{ "monster_daedalus", 6, -1, 0.008f }, 
{ "monster_medic_commander", 13, -1, 0.18f }, 
{ "monster_tank_commander", 11, 18, 0.15f },
{ "monster_spider", 12, -1, 0.24f },
{ "monster_guncmdr", 11, 22, 0.28f },
{ "monster_gladc", 6, 19, 0.16f }, 
{ "monster_gladiator", 9, -1, 0.24f },
{ "monster_shambler", 17, -1, 0.1f },
{ "monster_floater2", 17, -1, 0.35f },
{ "monster_carrier2", 17, -1, 0.23f },
{ "monster_tank_64", 16, -1, 0.13f },
{ "monster_janitor", 16, -1, 0.18f },
{ "monster_janitor2", 19, -1, 0.12f },
{ "monster_makron", 18, 19, 0.2f },
{ "monster_gladb", 14, -1, 0.75f},
{ "monster_boss2_64", 14, -1, 0.08f },
{ "monster_perrokl", 21, -1, 0.27f },
{ "monster_guncmdrkl", 23, -1, 0.39f },
{ "monster_shamblerkl", 23, -1, 0.39f }
};

struct boss_t {
	const char* classname;
	int32_t min_level; // Not used in this case, but keep it for future flexibility
	int32_t max_level;  // Not used in this case, but keep it for future flexibility
	float weight;
};

constexpr boss_t BOSS[] = {
  {"monster_jorg", -1, -1, 0.7f},
  {"monster_makronkl", -1, -1, 0.07f},
  {"monster_perrokl", -1, -1, 0.07f},
  {"monster_shamblerkl", -1, -1, 0.07f},
  {"monster_guncmdrkl", -1, -1, 0.07f},
  {"monster_boss2kl", -1, -1, 0.07f},
  {"monster_supertankkl", -1, -1, 0.07f},
  {"monster_turretkl", -1, -1, 0.07f},
};

struct picked_item_t {
	const weighted_item_t* item;
	float weight;
};

gitem_t* G_HordePickItem()
{
	// collect valid items
	static std::array<picked_item_t, q_countof(items)> picked_items;
	static size_t num_picked_items;

	num_picked_items = 0;

	float total_weight = 0;

	for (auto& item : items)
	{
		if (item.min_level != -1 && g_horde_local.level < item.min_level)
			continue;
		if (item.max_level != -1 && g_horde_local.level > item.max_level)
			continue;

		float weight = item.weight;

		if (item.adjust_weight)
			item.adjust_weight(item, weight);

		if (weight <= 0)
			continue;

		total_weight += weight;
		picked_items[num_picked_items++] = { &item, total_weight };
	}

	if (!total_weight)
		return nullptr;

	float r = frandom() * total_weight;

	for (size_t i = 0; i < num_picked_items; i++)
		if (r < picked_items[i].weight)
			return FindItemByClassname(picked_items[i].item->classname);

	return nullptr;
}

const char* G_HordePickMonster()
{
	// collect valid monsters
	static std::array<picked_item_t, q_countof(items)> picked_monsters;
	static size_t num_picked_monsters;

	num_picked_monsters = 0;

	float total_weight = 0;

	for (auto& item : monsters)
	{

		if (item.min_level != -1 && g_horde_local.level < item.min_level)
			continue;
		if (item.max_level != -1 && g_horde_local.level > item.max_level)
			continue;

		float weight = item.weight;

		if (item.adjust_weight)
			item.adjust_weight(item, weight);

		if (weight <= 0)
			continue;

		total_weight += weight;
		picked_monsters[num_picked_monsters++] = { &item, total_weight };
	}

	if (!total_weight)
		return nullptr;

	float r = frandom() * total_weight;

	for (size_t i = 0; i < num_picked_monsters; i++)
		if (r < picked_monsters[i].weight)
			return picked_monsters[i].item->classname;

	return nullptr;
}

const char* G_HordePickBOSS()
{
	const char* desired_boss = nullptr;

	if (!Q_strcasecmp(level.mapname, "q2dm1")) {
		desired_boss = brandom() ? "monster_supertankkl" : "monster_boss2kl";
	}
	else if (!Q_strcasecmp(level.mapname, "rdm14")) {
		desired_boss = "monster_makronkl";
	}
	else if (!Q_strcasecmp(level.mapname, "q2dm2")) {
		desired_boss = "monster_boss2kl";
	}
	else if (!Q_strcasecmp(level.mapname, "q2dm8")) {
		float r = frandom();
		desired_boss = (r < 0.333f) ? "monster_shamblerkl" : (r < 0.666f) ? "monster_boss2kl" : "monster_makronkl";
	}
	else if (!Q_strcasecmp(level.mapname, "xdm2")) {
		float r = frandom();
		desired_boss = (r < 0.333f) ? "monster_gunnercmdrkl" : (r < 0.666f) ? "monster_boss2kl" : "monster_makronkl";
	}
	else if (!Q_strcasecmp(level.mapname, "dm7") || !Q_strcasecmp(level.mapname, "q64/dm7") || !Q_strcasecmp(level.mapname, "q64\\dm7") ||
		!Q_strcasecmp(level.mapname, "dm10") || !Q_strcasecmp(level.mapname, "q64/dm10") || !Q_strcasecmp(level.mapname, "q64\\dm10") ||
		!Q_strcasecmp(level.mapname, "dm2") || !Q_strcasecmp(level.mapname, "q64/dm2") || !Q_strcasecmp(level.mapname, "q64\\dm2")) {
		desired_boss = brandom() ? "monster_perrokl" : "monster_guncmdrkl";
	}
	else if (!Q_strcasecmp(level.mapname, "q2ctf5")) {
		float r = frandom();
		desired_boss = (r < 0.333f) ? "monster_supertankkl" : (r < 0.666f) ? "monster_boss2kl" : "monster_makronkl";
	}

	for (const auto& item : BOSS)
	{
		if (!strcmp(item.classname, desired_boss))
		{
			return item.classname;
		}
	}
	return nullptr;
}

void Horde_PreInit()
{
	g_horde = gi.cvar("horde", "0", CVAR_LATCH);

	if (!g_horde->integer)
		return;

	if (!deathmatch->integer || ctf->integer || teamplay->integer || coop->integer)

	gi.Com_Print("Horde mode must be DM.\n");
	gi.cvar_set("deathmatch", "1");
	gi.cvar_set("ctf", "0");
	gi.cvar_set("teamplay", "0");
	gi.cvar_set("coop", "0");
	gi.cvar_set("timelimit", "20");
	gi.cvar_set("fraglimit", "0");

		g_horde = gi.cvar("horde", "0", CVAR_LATCH);

	if (g_horde->integer)
	{
		gi.Com_Print("Horde mode must be DM.\n");
		gi.cvar_set("deathmatch", "1");
		gi.cvar_set("ctf", "0");
		gi.cvar_set("teamplay", "0");
		gi.cvar_set("coop", "0");
		gi.cvar_set("timelimit", "25");
		gi.cvar_set("fraglimit", "0");
		gi.cvar_set("sv_target_id", "1");
		gi.cvar_set("g_dm_instant_items", "1");
		gi.cvar_set("g_disable_player_collision", "1");
//		gi.cvar_set("g_instagib", "1");
//		gi.cvar_set("g_dm_no_self_damage", "1");
//		gi.cvar_set("g_allow_techs", "1");
//		gi.cvar_set("g_use_hook", "1");
//		gi.cvar_set("hook_pullspeed", "1200");
//		gi.cvar_set("hook_speed", "3000");
//		gi.cvar_set("hook_sky", "1");
//		gi.cvar_set("g_no_nukes", "1");
//		gi.cvar_set("g_allow_grapple 1", "1");
//		gi.cvar_set("g_grapple_fly_speed", "3000");
//		gi.cvar_set("g_grapple_pull_speed", "1200");
//		gi.cvar_set("g_dm_no_fall_damage", "1");
//		gi.cvar_set("g_instant_weapon_switch", "1");
//		gi.cvar_set("g_start_items", "item_bandolier 1");

	}
}

void Horde_Init()
{

	// precache all items
	for (auto& item : itemlist)
		PrecacheItem(&item);

	// all monsters too
	for (auto& monster : monsters)
	{
		edict_t* e = G_Spawn();
		e->classname = monster.classname;
		e->monsterinfo.aiflags |= AI_DO_NOT_COUNT; // FIX BUG COUNT MONSTER IN COOP BEING +8
		ED_CallSpawn(e);
		G_FreeEdict(e);
	}

	g_horde_local.warm_time = level.time + 4_sec;
}

static bool Horde_AllMonstersDead()
{
	for (size_t i = 0; i < globals.max_edicts; i++)
	{
		if (!g_edicts[i].inuse)
			continue;
		else if (g_edicts[i].svflags & SVF_MONSTER)
		{
			if (!g_edicts[i].deadflag && g_edicts[i].health > 0)
				return false;
		}
	}
	return true;
}

static void Horde_CleanBodies()
{
	for (size_t i = 0; i < globals.max_edicts; i++)
	{
		if (!g_edicts[i].inuse)
			continue;
		else if (g_edicts[i].svflags & SVF_DEADMONSTER)
		{
			G_FreeEdict(&g_edicts[i]);			
		}
	}
}

void SpawnBossAutomatically()
{
	if ((Q_strcasecmp(level.mapname, "q2dm1") == 0 && current_wave_number % 8 == 0 && current_wave_number != 0) ||
		(Q_strcasecmp(level.mapname, "rdm14") == 0 && current_wave_number % 5 == 0 && current_wave_number != 0) ||
		(Q_strcasecmp(level.mapname, "q2dm2") == 0 && current_wave_number % 5 == 0 && current_wave_number != 0) ||
		(Q_strcasecmp(level.mapname, "q2dm8") == 0 && current_wave_number % 5 == 0 && current_wave_number != 0) ||
		(Q_strcasecmp(level.mapname, "xdm2") == 0 && current_wave_number % 5 == 0 && current_wave_number != 0) ||
		(Q_strcasecmp(level.mapname, "q2ctf5") == 0 && current_wave_number % 5 == 0 && current_wave_number != 0) ||
		((!Q_strcasecmp(level.mapname, "q64/dm10") || !Q_strcasecmp(level.mapname, "q64\\dm10")) && current_wave_number % 5 == 0 && current_wave_number != 0) ||
		((!Q_strcasecmp(level.mapname, "q64/dm7") || !Q_strcasecmp(level.mapname, "q64\\dm7")) && current_wave_number % 5 == 0 && current_wave_number != 0) ||
		((!Q_strcasecmp(level.mapname, "q64/dm2") || !Q_strcasecmp(level.mapname, "q64\\dm2")) && current_wave_number % 5 == 0 && current_wave_number != 0))
	{

		// Solo necesitas un bucle aquí para generar un jefe
		edict_t* boss = G_Spawn(); // Creas un nuevo edict_t solo si es necesario
		if (!boss)
			return;

		// Aquí selecciona el jefe deseado
		const char* desired_boss = G_HordePickBOSS();
		if (!desired_boss) {
			return; // No se pudo encontrar un jefe válido
		}
		boss->classname = desired_boss;
		// origin for monsters
// Define un mapa que mapea los nombres de los mapas a las coordenadas de origen
		std::unordered_map<std::string, std::array<int, 3>> mapOrigins = {
			{"q2dm1", {1184, 568, 704}},
			{"rdm14", {1248, 664, 896}},
			{"q2dm2", {128, -960, 704}},
			{"q2dm8", {112, 1216, 88}},
			{"q2ctf5", {2432, -960, 168}},
			{"xdm2", {-232, 472, 424}},
			{"q64/dm7", {64, 224, 120}},
			{"q64\\dm7", {64, 224, 120}},
			{"q64/dm10", {-304, 512, -92}},
			{"q64\\dm10", {-304, 512, -92}},
			{"q64/dm2", {1328, -256, 272}},
			{"q64\\dm2", {1328, -256, 272}}
		};

		// Busca las coordenadas de origen del mapa actual y asígnalas al jefe
		auto it = mapOrigins.find(level.mapname);
		if (it != mapOrigins.end()) {
			boss->s.origin[0] = it->second[0];
			boss->s.origin[1] = it->second[1];
			boss->s.origin[2] = it->second[2];
		}
		else {
			return; // Si el mapa actual no se encuentra en el mapa, devuelve
		}

		boss->s.angles[0] = 0;
		boss->s.angles[1] = 0;
		boss->s.angles[2] = 0;

		gi.LocBroadcast_Print(PRINT_BROADCAST, "\n\nCHAMPION STROGG SPAWNED");
		boss->maxs *= 1.4;
		boss->mins *= 1.4;
		boss->s.scale = 1.4;
		boss->health *= pow(1.28, current_wave_number);
		boss->gib_health *= 3;
		//	boss->s.renderfx = RF_TRANSLUCENT;
		//	boss->s.effects = EF_FLAG1 | EF_QUAD;

		vec3_t effectPosition = boss->s.origin;
		effectPosition[0] += (boss->s.origin[0] - effectPosition[0]) * (boss->s.scale - 3);
		effectPosition[1] += (boss->s.origin[1] - effectPosition[1]) * (boss->s.scale - 3);
		effectPosition[2] += (boss->s.origin[2] - effectPosition[2]) * (boss->s.scale - 3);

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BOSSTPORT);
		gi.WritePosition(effectPosition);
		gi.multicast(effectPosition, MULTICAST_PHS, false);
		ED_CallSpawn(boss);
	}
}

void ResetGame() {
	g_horde_local.state = horde_state_t::warmup;
	next_wave_message_sent = false; // Reinicia la bandera de mensaje de próxima oleada
	gi.cvar_set("g_chaotic", "0");
	gi.cvar_set("g_insane", "0");
	gi.cvar_set("g_vampire", "0");
	gi.cvar_set("ai_damage_scale", "1");
	gi.cvar_set("g_damage_scale", "1");
	gi.cvar_set("g_ammoregen", "0");
	gi.cvar_set("g_hardcoop", "0");

	}

#include <chrono>

// Define una variable global para almacenar el tiempo de referencia cuando se cumple la condición
std::chrono::steady_clock::time_point condition_start_time;

// Variable para almacenar el valor anterior de remainingMonsters
int previous_remainingMonsters = 0;

// Función para verificar si la condición de remainingMonsters se cumple durante más de x segundos
bool CheckRemainingMonstersCondition(bool isSmallMap, bool isBigMap, bool isMediumMap) {
	int maxMonsters;
	int timeThreshold;

	// Declaración de ent fuera del bucle
	edict_t* ent;

	// Ciclo a través de jugadores
	for (uint32_t player = 1; player <= game.maxclients; player++)
	{
		ent = &g_edicts[player];
		if (!ent->inuse || !ent->client || ent->movetype == MOVETYPE_NOCLIP)
			continue;

		// Contar jugadores activos
		int numActivePlayers = 0;
		for (auto player : active_players()) {
			numActivePlayers++;
		}

		// Ajustar los valores según el tipo de mapa y la cantidad de jugadores activos
		if (numActivePlayers > 5) {
			if (isSmallMap) {
				maxMonsters = 9; // remainingmonsters
				timeThreshold = 9 - numActivePlayers;  // timer in seconds whento get to next wave, activating chaotic or insane,

				if (timeThreshold <= 5) {
					timeThreshold = 6;
				}
			}
			else if (isBigMap) {
				maxMonsters = 27;
				timeThreshold = 20 - numActivePlayers;

				if (timeThreshold <= 12) {
					timeThreshold = 13;
				}
			}
			else {
				maxMonsters = 12;
				timeThreshold = 14 - numActivePlayers;

				if (timeThreshold <= 9) {
					timeThreshold = 9;
				}
			}
		}
		else {
			if (isSmallMap) {   // less than 5 players, smallmap works nice 5/2/24
				maxMonsters = 5;
				timeThreshold = 14; // Ajustar el umbral de tiempo más alto
			}
			else if (isBigMap) { // adjusted day 5/2 8pm
				maxMonsters = 22;
				timeThreshold = 19; // Ajustar el umbral de tiempo más alto
			}
			else {
				maxMonsters = 7;
				timeThreshold = 15; // Ajustar el umbral de tiempo más alto
			}
		}
		// Agregar la condición adicional para timeThreshold
	}
	if (remainingMonsters <= maxMonsters) {
		// Si la condición se cumple por primera vez, actualiza el tiempo de referencia
		if (condition_start_time == std::chrono::steady_clock::time_point()) {
			condition_start_time = std::chrono::steady_clock::now();
		}
		// Verifica si la condición ha estado activa durante más de x segundos
		auto current_time = std::chrono::steady_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::seconds>(current_time - condition_start_time);

		if (duration.count() >= timeThreshold) {
			// Reinicia el tiempo de referencia para la próxima ola
			condition_start_time = std::chrono::steady_clock::time_point();
			return true;
		}
	}
	else {
		// Reinicia el tiempo de referencia si remainingMonsters está aumentando
		if (remainingMonsters > previous_remainingMonsters) {
			condition_start_time = std::chrono::steady_clock::time_point();
		}
	}
	// Actualiza el valor anterior de remainingMonsters
	previous_remainingMonsters = remainingMonsters;
	return false;
}

void Horde_RunFrame()
{
	switch (g_horde_local.state)
	{
	case horde_state_t::warmup:
		if (g_horde_local.warm_time < level.time + 1.5_sec) // lesser seconds is more time before first wave
		{
			remainingMonsters = 0;
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(1);
			current_wave_number = 2;

			// init random
			std::srand(static_cast<unsigned int>(std::time(nullptr)));

			//generate random number
			float r = frandom();

			if (r < 0.333f) // less than 0.333
			{
				gi.sound(world, CHAN_VOICE, gi.soundindex("misc/r_tele3.wav"), 1, ATTN_NONE, 0);
			}
			else if (r < 0.666f) // if less than 0.666
			{
			//	gi.sound(world, CHAN_VOICE, gi.soundindex("world/redforce.wav"), 1, ATTN_NONE, 0);
				gi.sound(world, CHAN_VOICE, gi.soundindex("world/klaxon2.wav"), 1, ATTN_NONE, 0);
			}
			else // same or more than 0.666
			{
				gi.sound(world, CHAN_VOICE, gi.soundindex("misc/tele_up.wav"), 1, ATTN_NONE, 0);
			}
			// Print message according difficult
			if (!g_chaotic->integer)
				gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\n???");
			else
				gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\nHorde Imminent");

			// random sound
			if (!g_chaotic->integer || (g_chaotic->integer && r > 0.666f))
				gi.sound(world, CHAN_VOICE, gi.soundindex("misc/r_tele3.wav"), 1, ATTN_NONE, 0);
			else if (r > 0.333f)
				gi.sound(world, CHAN_VOICE, gi.soundindex("world/incoming.wav"), 1, ATTN_NONE, 0);
			else
				gi.sound(world, CHAN_VOICE, gi.soundindex("misc/tele_up.wav"), 1, ATTN_NONE, 0);
		}
		break;

	case horde_state_t::spawning:
		if (!next_wave_message_sent)
		{
			next_wave_message_sent = true;
		}
		if (g_horde_local.monster_spawn_time <= level.time)
		{

			// Llama a la función para spawnear el jefe solo si es el momento adecuado
			if (g_horde_local.num_to_spawn == BOSS_TO_SPAWN)
			{
				SpawnBossAutomatically();
			}

			edict_t* e = G_Spawn();
			e->classname = G_HordePickMonster();
			select_spawn_result_t result = SelectDeathmatchSpawnPoint(false, true, false);
			if (result.any_valid)
			{

				e->s.origin = result.spot->s.origin;
				e->s.angles = result.spot->s.angles;
				e->item = G_HordePickItem();
				// e->s.renderfx = RF_TRANSLUCENT;
				ED_CallSpawn(e);
				remainingMonsters = level.total_monsters - level.killed_monsters; // Calcula la cantidad de monstruos restantes

				{
					// spawn animation
					vec3_t spawngrow_pos = e->s.origin;
					float start_size = (sqrt(spawngrow_pos[0] * spawngrow_pos[0] + spawngrow_pos[1] * spawngrow_pos[1] + spawngrow_pos[1] * spawngrow_pos[1])) * 0.025f; // scaling
					float end_size = start_size;

					// Generar el Spawngrow con los tamaños calculados
					SpawnGrow_Spawn(spawngrow_pos, start_size, end_size);

				}

				if (!Q_strcasecmp(level.mapname, "mgu4trial")) {
					e->s.renderfx = RF_GLOW;
					e->s.effects = EF_GRENADE_LIGHT;
				}
						
				e->enemy = &g_edicts[1];
				e->gib_health = -280;

				e->monsterinfo.power_armor_power *= pow(1.082, current_wave_number);  // trying with coop health scaling + multiplying power armor by currentwave
			

				HuntTarget(e);
	
				if (current_wave_number >= 13 || g_insane->integer || g_chaotic->integer == 2) {
					g_horde_local.monster_spawn_time = level.time + random_time(1.1_sec, 1.3_sec);
				//	e->health *= pow(1.0085, current_wave_number); // trying with coop health caling again
				}
				else if (!g_insane->integer || !g_chaotic->integer) {
					g_horde_local.monster_spawn_time = level.time + random_time(0.7_sec, 0.9_sec);  // monster spawn speed
				}
				else if (g_chaotic->integer == 1) {
					g_horde_local.monster_spawn_time = level.time + random_time(0.4_sec, 0.7_sec);  // monster spawn speed
				}

				--g_horde_local.num_to_spawn;

				if (!g_horde_local.num_to_spawn)
				{
					{
						std::ostringstream message_stream;
						message_stream << "New Wave Is Here.\n Current Level: " << g_horde_local.level << "\n";
						gi.LocBroadcast_Print(PRINT_CENTER, message_stream.str().c_str());
					}
					g_horde_local.state = horde_state_t::cleanup;
					g_horde_local.monster_spawn_time = level.time + 1_sec;
				}
			}
			else
			{
				remainingMonsters = level.total_monsters - level.killed_monsters;; // Calcula la cantidad de monstruos restantes
				g_horde_local.monster_spawn_time = level.time + 0.5_sec;
			}
		}
		break;

	case horde_state_t::cleanup:

		if (CheckRemainingMonstersCondition(isSmallMap, isBigMap, isMediumMap)) {

			if (current_wave_number >= 15) {
				gi.cvar_set("g_insane", "1");
				gi.cvar_set("g_chaotic", "0");


				// Si se cumple la condición durante más de x segundos, avanza al estado 'rest'
				g_horde_local.state = horde_state_t::rest;
				break;
			}
			else if ((!isSmallMap) || current_wave_number > 6 && current_wave_number <= 14) {
				gi.cvar_set("g_chaotic", "1");
			}
			else
				gi.cvar_set("g_chaotic", "2");

			// Si se cumple la condición durante más de x segundos, avanza al estado 'rest'
			g_horde_local.state = horde_state_t::rest;
			break;
		}

		if (g_horde_local.monster_spawn_time < level.time)
		{
			if (Horde_AllMonstersDead())
			{
				g_horde_local.warm_time = level.time + 5_sec;
				g_horde_local.state = horde_state_t::rest;
				remainingMonsters = 0;

				if (g_chaotic->integer || g_insane->integer) {
					gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\n\n\nHarder Wave Controlled, GG\n");
					gi.sound(world, CHAN_VOICE, gi.soundindex("world/x_light.wav"), 1, ATTN_NONE, 0);
					gi.cvar_set("g_chaotic", "0");
					gi.cvar_set("g_insane", "0");
//					gi.cvar_set("ai_damage_scale", "1.2");
				}
				else if (!g_chaotic->integer) {
					gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\n\n\nWave Defeated, GG !\n");
				}
			}
			else
			remainingMonsters = level.total_monsters + 1 - level.killed_monsters;
			g_horde_local.monster_spawn_time = level.time + 3_sec;
		}
		break;


	case horde_state_t::rest:
		if (g_horde_local.warm_time < level.time)
		{
			if (g_chaotic->integer || g_insane->integer) {
				if (!g_insane->integer) {
					gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\n\n STROGGS STARTING TO PUSH !\n\n\n ");
				  }
				else if (g_insane->integer) {
					gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\n\n**************\n\n\n--STRONGER WAVE COMING--\n\n\n STROGGS STARTING TO PUSH !\n\n\n **************");
				  }

				float r = frandom();

				if (r < 0.167f) // Aproximadamente 16.7% de probabilidad para cada sonido
					gi.sound(world, CHAN_VOICE, gi.soundindex("nav_editor/action_fail.wav"), 1, ATTN_NONE, 0);
				else if (r < 0.333f)
					gi.sound(world, CHAN_VOICE, gi.soundindex("makron/roar1.wav"), 1, ATTN_NONE, 0);
				else if (r < 0.5f)
					gi.sound(world, CHAN_VOICE, gi.soundindex("zortemp/ack.wav"), 1, ATTN_NONE, 0);
				else if (r < 0.667f)
					gi.sound(world, CHAN_VOICE, gi.soundindex("misc/spawn1.wav"), 1, ATTN_NONE, 0);
				else if (r < 0.833f)
					gi.sound(world, CHAN_VOICE, gi.soundindex("makron/voice3.wav"), 1, ATTN_NONE, 0);
				else
					gi.sound(world, CHAN_VOICE, gi.soundindex("world/v_fac3.wav"), 1, ATTN_NONE, 0);
			}
			else if (!g_chaotic->integer || !g_insane->integer) {
				gi.LocBroadcast_Print(PRINT_CENTER, "Loading Next Wave");
				gi.sound(world, CHAN_VOICE, gi.soundindex("world/lite_on1.wav"), 1, ATTN_NONE, 0);
			}
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(g_horde_local.level + 1);
			Horde_CleanBodies();
		}
		break;
	}
}

void HandleResetEvent() {
	ResetGame();

}

