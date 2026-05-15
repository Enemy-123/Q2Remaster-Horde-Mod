// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"
#include "horde/g_character.h"
#include "shared.h"

void Svcmd_Test_f()
{
	gi.LocClient_Print(nullptr, PRINT_HIGH, "Svcmd_Test_f()\n");
}

/*
==============================================================================

PACKET FILTERING


You can add or remove addresses from the filter list with:

addip <ip>
removeip <ip>

The ip address is specified in dot format, and any unspecified digits will match any value, so you can specify an entire
class C network with "addip 192.246.40".

Removeip will only remove an address specified exactly the same way.  You cannot addip a subnet, then removeip a single
host.

listip
Prints the current list of filters.

writeip
Dumps "addip <ip>" commands to listip.cfg so it can be execed at a later date.  The filter lists are not saved and
restored by default, because I beleive it would cause too much confusion.

filterban <0 or 1>

If 1 (the default), then ip addresses matching the current list will be prohibited from entering the game.  This is the
default setting.

If 0, then only addresses matching the list will be allowed.  This lets you easily set up a private game, or a game that
only allows players from your local network.


==============================================================================
*/

struct ipfilter_t
{
	unsigned mask;
	unsigned compare;
};

constexpr size_t MAX_IPFILTERS = 1024;

ipfilter_t ipfilters[MAX_IPFILTERS];
int		   numipfilters;

/*
=================
StringToFilter
=================
*/
static bool StringToFilter(const char *s, ipfilter_t *f)
{
	char num[128];
	int	 i, j;
	byte b[4];
	byte m[4];

	for (i = 0; i < 4; i++)
	{
		b[i] = 0;
		m[i] = 0;
	}

	for (i = 0; i < 4; i++)
	{
		if (*s < '0' || *s > '9')
		{
			gi.LocClient_Print(nullptr, PRINT_HIGH, "Bad filter address: {}\n", s);
			return false;
		}

		j = 0;
		while (*s >= '0' && *s <= '9')
		{
			num[j++] = *s++;
		}
		num[j] = 0;
		b[i] = atoi(num);
		if (b[i] != 0)
			m[i] = 255;

		if (!*s)
			break;
		s++;
	}

	f->mask = *(unsigned *) m;
	f->compare = *(unsigned *) b;

	return true;
}

/*
=================
SV_FilterPacket
=================
*/
bool SV_FilterPacket(const char *from)
{
	int		 i;
	unsigned in;
	byte	 m[4];
	const char	 *p;

	i = 0;
	p = from;
	while (*p && i < 4)
	{
		m[i] = 0;
		while (*p >= '0' && *p <= '9')
		{
			m[i] = m[i] * 10 + (*p - '0');
			p++;
		}
		if (!*p || *p == ':')
			break;
		i++;
		p++;
	}

	in = *(unsigned *) m;

	for (i = 0; i < numipfilters; i++)
		if ((in & ipfilters[i].mask) == ipfilters[i].compare)
			return filterban->integer;

	return !filterban->integer;
}

/*
=================
SV_AddIP_f
=================
*/
void SVCmd_AddIP_f()
{
	int i;

	if (gi.argc() < 3)
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Usage:  addip <ip-mask>\n");
		return;
	}

	for (i = 0; i < numipfilters; i++)
		if (ipfilters[i].compare == 0xffffffff)
			break; // free spot
	if (i == numipfilters)
	{
		if (numipfilters == MAX_IPFILTERS)
		{
			gi.LocClient_Print(nullptr, PRINT_HIGH, "IP filter list is full\n");
			return;
		}
		numipfilters++;
	}

	if (!StringToFilter(gi.argv(2), &ipfilters[i]))
		ipfilters[i].compare = 0xffffffff;
}

/*
=================
SV_RemoveIP_f
=================
*/
void SVCmd_RemoveIP_f()
{
	ipfilter_t f;
	int		   i, j;

	if (gi.argc() < 3)
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Usage:  sv removeip <ip-mask>\n");
		return;
	}

	if (!StringToFilter(gi.argv(2), &f))
		return;

	for (i = 0; i < numipfilters; i++)
		if (ipfilters[i].mask == f.mask && ipfilters[i].compare == f.compare)
		{
			for (j = i + 1; j < numipfilters; j++)
				ipfilters[j - 1] = ipfilters[j];
			numipfilters--;
			gi.LocClient_Print(nullptr, PRINT_HIGH, "Removed.\n");
			return;
		}
	gi.LocClient_Print(nullptr, PRINT_HIGH, "Didn't find {}.\n", gi.argv(2));
}

/*
=================
SV_ListIP_f
=================
*/
void SVCmd_ListIP_f()
{
	int	 i;
	byte b[4];

	gi.LocClient_Print(nullptr, PRINT_HIGH, "Filter list:\n");
	for (i = 0; i < numipfilters; i++)
	{
		*(unsigned *) b = ipfilters[i].compare;
		gi.LocClient_Print(nullptr, PRINT_HIGH, "{}.{}.{}.{}\n", b[0], b[1], b[2], b[3]);
	}
}

// [Paril-KEX]
void SVCmd_NextMap_f()
{
	gi.LocBroadcast_Print(PRINT_HIGH, "$g_map_ended_by_server");
	EndDMLevel();
}

/*
=================
SV_WriteIP_f
=================
*/
void SVCmd_WriteIP_f(void)
{
	// KEX_FIXME: Sys_FOpen isn't available atm, just commenting this out since i don't think we even need this functionality - sponge
	/*
	FILE* f;

	byte	b[4];
	int		i;
	cvar_t* game;

	game = gi.cvar("game", "", 0);

	std::string name;
	if (!*game->string)
		name = std::string(GAMEVERSION) + "/listip.cfg";
	else
		name = std::string(game->string) + "/listip.cfg";

	gi.LocClient_Print(nullptr, PRINT_HIGH, "Writing {}.\n", name.c_str());

	f = Sys_FOpen(name.c_str(), "wb");
	if (!f)
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Couldn't open {}\n", name.c_str());
		return;
	}

	fprintf(f, "set filterban %d\n", filterban->integer);

	for (i = 0; i < numipfilters; i++)
	{
		*(unsigned*)b = ipfilters[i].compare;
		fprintf(f, "sv addip %i.%i.%i.%i\n", b[0], b[1], b[2], b[3]);
	}

	fclose(f);
	*/
}

// REMOVED: AssetManager debug commands - no longer needed
// void SVCmd_AssetStats_f()
// void SVCmd_AssetList_f()
// void SVCmd_AssetCleanup_f()

void SVCmd_ReloadConfig_f()
{
	Config_Reload();
}

/*
=================
SVCmd_TacticalSpawns_f
Admin command: sv tacticspawns <0|1|2>
Controls tactical spawn behavior for monsters
0 = Off (standard random spawning)
1 = Basic (distance checks only)
2 = Full tactical (distance + visibility checks to prevent pop-in)
=================
*/
void SVCmd_TacticalSpawns_f()
{
	if (gi.argc() < 3)
	{
		const char* mode_str = "Off";
		if (g_horde_tactical_spawn->integer == 1)
			mode_str = "Basic (distance only)";
		else if (g_horde_tactical_spawn->integer == 2)
			mode_str = "Full (distance + visibility)";

		gi.LocClient_Print(nullptr, PRINT_HIGH,
			"Usage: sv tacticspawns <0|1|2>\n"
			"Current mode: {} ({})\n"
			"  0 = Off (standard random spawning)\n"
			"  1 = Basic (distance checks only)\n"
			"  2 = Full tactical (distance + visibility checks)\n",
			g_horde_tactical_spawn->integer, mode_str);
		return;
	}

	int mode = atoi(gi.argv(2));

	if (mode < 0 || mode > 2)
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Invalid mode {}. Must be 0, 1, or 2\n", mode);
		return;
	}

	gi.cvar_set("g_horde_tactical_spawn", G_Fmt("{}", mode).data());

	const char* mode_str = "Off";
	if (mode == 1)
		mode_str = "Basic (distance only)";
	else if (mode == 2)
		mode_str = "Full (distance + visibility)";

	gi.LocBroadcast_Print(PRINT_HIGH, "Tactical spawning set to: {} ({})\n", mode, mode_str);
}

/*
=================
ServerCommand

ServerCommand will be called when an "sv" command is issued.
The game can issue gi.argc() / gi.argv() commands to get the rest
of the parameters
=================
*/
/*
=================
FindPlayerByName
Helper function to find a player by name (partial match)
Uses GetPlayerName for proper name retrieval from userinfo
=================
*/
static edict_t* FindPlayerByName(const char* name)
{
	if (!name || !*name)
		return nullptr;

	// Try exact match first
	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		edict_t* player = g_edicts + 1 + i;
		if (!player->inuse || !player->client)
			continue;

		const char* player_name = GetPlayerName(player);
		if (Q_strcasecmp(player_name, name) == 0)
			return player;
	}

	// Try partial match (case-insensitive substring)
	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		edict_t* player = g_edicts + 1 + i;
		if (!player->inuse || !player->client)
			continue;

		const char* player_name = GetPlayerName(player);

		// Convert both strings to lowercase and check substring
		char lower_player_name[MAX_INFO_VALUE];
		char lower_search_name[MAX_INFO_VALUE];
		Q_strlcpy(lower_player_name, player_name, sizeof(lower_player_name));
		Q_strlcpy(lower_search_name, name, sizeof(lower_search_name));

		for (char* p = lower_player_name; *p; p++)
			*p = tolower(*p);
		for (char* p = lower_search_name; *p; p++)
			*p = tolower(*p);

		if (strstr(lower_player_name, lower_search_name))
			return player;
	}

	return nullptr;
}

/*
=================
SVCmd_AddAbilityPoints_f
Admin command: sv addabilitypoints <playername> <quantity>
Adds ability/skill points to a player
=================
*/
void SVCmd_AddAbilityPoints_f()
{
	if (gi.argc() < 4)
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Usage: sv addabilitypoints <playername> <quantity>\n");
		return;
	}

	const char* player_name = gi.argv(2);
	int quantity = atoi(gi.argv(3));

	edict_t* player = FindPlayerByName(player_name);
	if (!player)
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Player '{}' not found\n", player_name);
		return;
	}

	player->client->pers.skill_points += quantity;
	
	gi.LocClient_Print(nullptr, PRINT_HIGH, "Added {} skill points to {}\n", 
		quantity, player->client->pers.netname);
	gi.LocClient_Print(player, PRINT_HIGH, "Admin granted you {} skill points!\n", quantity);
	
	// Save character data
	Character_Save(player);
}

/*
=================
SVCmd_AddWeaponPoints_f
Admin command: sv addweaponpoints <playername> <quantity>
Adds weapon points to a player
=================
*/
void SVCmd_AddWeaponPoints_f()
{
	if (gi.argc() < 4)
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Usage: sv addweaponpoints <playername> <quantity>\n");
		return;
	}

	const char* player_name = gi.argv(2);
	int quantity = atoi(gi.argv(3));

	edict_t* player = FindPlayerByName(player_name);
	if (!player)
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Player '{}' not found\n", player_name);
		return;
	}

	player->client->pers.weapon_points += quantity;
	
	gi.LocClient_Print(nullptr, PRINT_HIGH, "Added {} weapon points to {}\n", 
		quantity, player->client->pers.netname);
	gi.LocClient_Print(player, PRINT_HIGH, "Admin granted you {} weapon points!\n", quantity);
	
	// Save character data
	Character_Save(player);
}

/*
=================
SVCmd_ResetPlayer_f
Admin command: sv reset <playername>
Deletes the player's SQLite character row and resets all progress
=================
*/
void SVCmd_ResetPlayer_f()
{
	if (gi.argc() < 3)
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Usage: sv reset <playername>\n");
		return;
	}

	const char* player_name = gi.argv(2);

	edict_t* player = FindPlayerByName(player_name);
	if (!player)
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Player '{}' not found\n", player_name);
		return;
	}

	if (Character_Reset(player))
	{
		std::string db_path = Character_GetFilePath(player);
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Character row for '{}' deleted successfully from {}\n",
			player->client->pers.netname,
			db_path.c_str());

		// Reset all character data in memory
		player->client->pers.pvm_level = 0;
		player->client->pers.pvm_xp = 0;
		player->client->pers.pvm_stat_points = 0;
		player->client->pers.pvm_max_ammo_level = 0;
		player->client->pers.pvm_vitality_level = 0;
		player->client->pers.skill_points = 0;
		player->client->pers.weapon_points = 0;
		player->client->pers.horde_power_cubes = 0;
		player->client->pers.skills = {};
		Q_strlcpy(player->client->pers.respawn_weapon_name,
			"Rocket Launcher",
			sizeof(player->client->pers.respawn_weapon_name));

		gi.LocClient_Print(player, PRINT_HIGH, "Your character has been reset by an admin!\n");

		// Kick player to force reconnect and reload
		gi.AddCommandString(G_Fmt("kick \"{}\"\n", player->client->pers.netname).data());
	}
	else
	{
		std::string db_path = Character_GetFilePath(player);
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Failed to delete character row for '{}' from {}\n",
			player->client->pers.netname,
			db_path.c_str());
	}
}

void ServerCommand()
{
	const char *cmd;

	cmd = gi.argv(1);
	if (Q_strcasecmp(cmd, "test") == 0)
		Svcmd_Test_f();
	else if (Q_strcasecmp(cmd, "addip") == 0)
		SVCmd_AddIP_f();
	else if (Q_strcasecmp(cmd, "removeip") == 0)
		SVCmd_RemoveIP_f();
	else if (Q_strcasecmp(cmd, "listip") == 0)
		SVCmd_ListIP_f();
	else if (Q_strcasecmp(cmd, "writeip") == 0)
		SVCmd_WriteIP_f();
	else if (Q_strcasecmp(cmd, "nextmap") == 0)
		SVCmd_NextMap_f();
	else if (Q_strcasecmp(cmd, "reload_config") == 0)
		SVCmd_ReloadConfig_f();
	else if (Q_strcasecmp(cmd, "addabilitypoints") == 0)
		SVCmd_AddAbilityPoints_f();
	else if (Q_strcasecmp(cmd, "addweaponpoints") == 0)
		SVCmd_AddWeaponPoints_f();
	else if (Q_strcasecmp(cmd, "reset") == 0)
		SVCmd_ResetPlayer_f();
	else if (Q_strcasecmp(cmd, "tacticspawns") == 0)
		SVCmd_TacticalSpawns_f();
	// REMOVED: Asset manager commands (assetstats, assetlist, assetcleanup)
	else
		gi.LocClient_Print(nullptr, PRINT_HIGH, "Unknown server command \"{}\"\n", cmd);
}
