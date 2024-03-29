// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "../g_local.h"
#include <sstream>




cvar_t* g_horde;

enum class horde_state_t
{
	warmup,
	spawning,
	cleanup,
	rest
};

static struct {
	gtime_t			warm_time = 15_sec;
	horde_state_t	state = horde_state_t::warmup;

	gtime_t			monster_spawn_time;
	int32_t			num_to_spawn;
	int32_t			level;
} g_horde_local;
bool next_wave_message_sent = false;
static const int MAX_MONSTERS_PER_WAVE = 35;
static void Horde_InitLevel(int32_t lvl)
{
	g_horde_local.level = lvl;
	g_horde_local.num_to_spawn = 16 + (lvl * 2);

	// limiting max monsters
	if (g_horde_local.num_to_spawn > MAX_MONSTERS_PER_WAVE)
	{
		g_horde_local.num_to_spawn = MAX_MONSTERS_PER_WAVE;
	}

	g_horde_local.monster_spawn_time = level.time + random_time(1_sec, 3_sec);
}

bool G_IsDeathmatch()
{
	return deathmatch->integer && !g_horde->integer;
}

bool G_IsCooperative()
{
	return coop->integer || g_horde->integer;
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
	{ "item_health_small", -1, -1, 0.35f, adjust_weight_health },
	{ "item_health", -1, -1, 0.20f, adjust_weight_health },
	{ "item_health_large", -1, -1, 0.15f, adjust_weight_health },
	{ "item_health_mega", -1, -1, 0.03f, adjust_weight_health },

	{ "item_armor_shard", -1, -1, 0.35f, adjust_weight_armor },
	{ "item_armor_jacket", 4, 4, 0.25f, adjust_weight_armor },
	{ "item_armor_combat", 6, -1, 0.12f, adjust_weight_armor },
	{ "item_armor_body", 8, -1, 0.10f, adjust_weight_armor },

	{ "item_quad", -1, -1, 0.04f, adjust_weight_powerup },
	{ "item_quadfire", -1, -1, 0.05f, adjust_weight_powerup },
	{ "item_invulnerability", -1, -1, 0.02f, adjust_weight_powerup },

	{ "weapon_blaster", -1, 2, 0.1f, adjust_weight_weapon },
	{ "weapon_shotgun", 2, 3, 0.18f, adjust_weight_weapon },
	{ "weapon_boomer", 4, 9, 0.15f, adjust_weight_weapon },
	{ "weapon_supershotgun", 3, 6, 0.15f, adjust_weight_weapon },
	{ "weapon_machinegun", 3, 5, .15f, adjust_weight_weapon },
	{ "weapon_chaingun", 6, 9, 0.15f, adjust_weight_weapon },
	{ "weapon_grenadelauncher", 6, 7, 0.15f, adjust_weight_weapon },
	{ "weapon_hyperblaster", 4, 6, 0.15f, adjust_weight_weapon },
	{ "weapon_phalanx", 6, 9, 0.15f, adjust_weight_weapon },
	{ "weapon_disintegrator", 5, 8, 0.15f, adjust_weight_weapon },
	{ "weapon_rocketlauncher", 3, 6, 0.10f, adjust_weight_weapon },
	{ "weapon_railgun", 3, 8, 0.15f, adjust_weight_weapon },
	{ "weapon_plasmabeam", 4, 7, 0.10f, adjust_weight_weapon },


	{ "ammo_shells", 2, -1, 0.45f, adjust_weight_ammo },
	{ "ammo_bullets", 2, -1, 0.45f, adjust_weight_ammo },
	{ "ammo_grenades", 2, -1, 0.45f, adjust_weight_ammo },
	{ "ammo_cells", 2, -1, 0.45f, adjust_weight_ammo },
	{ "ammo_magslug", 3, -1, 0.45f, adjust_weight_ammo },
	{ "ammo_slugs", 3, -1, 0.45f, adjust_weight_ammo },
	{ "ammo_disruptor", 4, -1, 0.45f, adjust_weight_ammo },
	{ "ammo_rockets", 3, -1, 0.45f, adjust_weight_ammo },
	{ "item_pack", 2, -1, 0.22f, adjust_weight_ammo },

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
	{ "monster_soldier_light", -1, 3, 0.75f },
	{ "monster_soldier", -1, 3, 0.45f },
	{ "monster_soldier_hypergun", -1, 4, 0.85f },
	{ "monster_stalker", 2, -1, 0.25f },
	{ "monster_gekk", 3, 6, 0.50f },
	{ "monster_parasite", 5, 6, 0.50f },
	{ "monster_brain", 4, 11, 0.30f },
	{ "monster_soldier_lasergun", 2, 6, 0.90f },
	{ "monster_soldier_ripper", 2, 5, 0.85f },
	{ "monster_infantry", 3, 6, 0.90f },
	{ "monster_gunner", 3, 7, 0.80f },
	{ "monster_tank", 4, 9, 0.85f },
	{ "monster_chick", 5, 8, 0.92f },
	{ "monster_guncmdr", 5, -1, 1.1f },
	{ "monster_gladiator", 5, 10, 1.1f },
	{ "monster_chick_heat", 9, -1, 0.63f },
	{ "monster_tank_commander", 6, 9, 0.65f },
	{ "monster_mutant", 3, -1, 0.75f },
	{ "monster_tank", 6, -1, 0.45f },
	{ "monster_janitor2", 8, -1, 0.15f },
	{ "monster_gladb", 8, -1, 1.2f },
	{ "monster_janitor", 5, -1, 0.25f },
	{ "monster_hover", 9, -1, 1.35f },
	{ "monster_flyer", -1, 8, 0.75f },
	{ "monster_floater", 2, 6, 0.85f },
	{ "monster_makron", 9, -1, 0.2f },
	{ "monster_boss2_64", 9, -1, 0.4f },
	{ "monster_carrier2", 9, -1, 0.08f },
	{ "monster_berserk", 4, -1, 1.15f },
	{ "monster_spider", 9, -1, 0.34f },
	{ "monster_tank_64", 10, -1, 0.45f },
	{ "monster_medic_commander", 7, -1, 0.07f },
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

void Horde_PreInit()
{
	g_horde = gi.cvar("horde", "0", CVAR_LATCH);

	if (!g_horde->integer)
		return;

	if (!G_IsDeathmatch() || ctf->integer || teamplay->integer || G_IsCooperative())
	{
		gi.Com_Print("Horde mode must be DM.\n");
		gi.cvar_set("deathmatch", "1");
		gi.cvar_set("ctf", "0");
		gi.cvar_set("teamplay", "0");
		gi.cvar_set("coop", "0");
		gi.cvar_set("timelimit", "0");
		gi.cvar_set("fraglimit", "0");

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
		ED_CallSpawn(e);
		G_FreeEdict(e);
	}

	g_horde_local.warm_time = level.time + 10_sec;
}

// gi.cvar_set("kill_ai", "");

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
		else if (g_edicts[i].svflags & SVF_MONSTER)
		{
			if (g_edicts[i].health <= 0 || g_edicts[i].deadflag || (g_edicts[i].svflags & SVF_DEADMONSTER))
				G_FreeEdict(&g_edicts[i]);
		}
	}
}



void Horde_RunFrame()
{
	switch (g_horde_local.state)
	{
	case horde_state_t::warmup:
		if (g_horde_local.warm_time < level.time + 5_sec)
		{
			gi.LocBroadcast_Print(PRINT_CENTER, "???\n");
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(1);

			if (!coop->value)
				gi.sound(world, CHAN_VOICE, gi.soundindex("world/redforce.wav"), 1, ATTN_NONE, 0);
		}
		break;



	case horde_state_t::spawning:
		if (!next_wave_message_sent)
		{
			next_wave_message_sent = true;
		}

		if (g_horde_local.monster_spawn_time <= level.time)
		{
			edict_t* e = G_Spawn();
			e->classname = G_HordePickMonster();
			select_spawn_result_t result = SelectDeathmatchSpawnPoint(false, true, false);

			if (result.any_valid)
			{
				e->s.origin = result.spot->s.origin;
				e->s.angles = result.spot->s.angles;
				e->item = G_HordePickItem();
				ED_CallSpawn(e);
				g_horde_local.monster_spawn_time = level.time + random_time(0.6_sec, 1.8_sec);

				e->enemy = &g_edicts[1];
				FoundTarget(e);

				--g_horde_local.num_to_spawn;

				if (!g_horde_local.num_to_spawn)
				{
					{
						std::ostringstream message_stream;
						message_stream << "New Wave Is Here.\n Current Level: " << g_horde_local.level << "\n";
						gi.LocBroadcast_Print(PRINT_CENTER, message_stream.str().c_str());
					}
					g_horde_local.state = horde_state_t::cleanup;
					g_horde_local.monster_spawn_time = level.time + 3_sec;
				}
			}
			else
			{
				g_horde_local.monster_spawn_time = level.time + 1.5_sec;
			}
		}
		break;


	case horde_state_t::cleanup:
		if (g_horde_local.monster_spawn_time < level.time)
		{
			if (Horde_AllMonstersDead())
			{
				gi.LocBroadcast_Print(PRINT_CENTER, "Wave Defeated, GG !");

				g_horde_local.warm_time = level.time + 7_sec;
				g_horde_local.state = horde_state_t::rest;
			}
			else
				g_horde_local.monster_spawn_time = level.time + 3_sec;
		}
		break;


	case horde_state_t::rest:
		if (g_horde_local.warm_time < level.time)
		{
			gi.LocBroadcast_Print(PRINT_CENTER, "Loading Next Wave");
			g_horde_local.state = horde_state_t::spawning;
			Horde_InitLevel(g_horde_local.level + 1);
			Horde_CleanBodies();
			gi.sound(world, CHAN_VOICE, gi.soundindex("world/lite_on1.wav"), 1, ATTN_NONE, 0);
		}
		break;
	}
}
