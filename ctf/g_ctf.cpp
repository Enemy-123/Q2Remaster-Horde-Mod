// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "../g_local.h"
#include "../m_player.h"

#include <assert.h>

enum match_t
{
	MATCH_NONE,
	MATCH_SETUP,
	MATCH_PREGAME,
	MATCH_GAME,
	MATCH_POST
};

enum elect_t
{
	ELECT_NONE,
	ELECT_MAP,
	ELECT_TIME // Add this for time extension vote
};

struct ctfgame_t
{
	int		team1, team2;
	int		total1, total2; // these are only set when going into intermission except in teamplay
	gtime_t last_flag_capture;
	int		last_capture_team;

	match_t match;	   // match state
	gtime_t matchtime; // time for match start/end (depends on state)
	int		lasttime;  // last time update, explicitly truncated to seconds
	bool	countdown; // has audio countdown started?

	elect_t	 election;	 // election type
	edict_t* etarget;	 // for admin election, who's being elected
	char	 elevel[32]; // for map election, target level
	int		 evotes;	 // votes so far
	int		 needvotes;	 // votes needed
	gtime_t	 electtime;	 // remaining time until election times out
	char	 emsg[256];	 // election name
	int		 warnactive; // true if stat string 30 is active

	ghost_t ghosts[MAX_CLIENTS]; // ghost codes
};

ctfgame_t ctfgame;

cvar_t* ctf;
cvar_t* teamplay;
cvar_t* g_teamplay_force_join;

// [Paril-KEX]
bool G_TeamplayEnabled()
{
	return ctf->integer || teamplay->integer || g_horde->integer;
}

// [Paril-KEX]
void G_AdjustTeamScore(ctfteam_t team, int32_t offset)
{
	if (team == CTF_TEAM1)
		ctfgame.total1 += offset;
	else if (team == CTF_TEAM2)
		ctfgame.total2 += offset;
}

cvar_t* competition;
cvar_t* matchlock;
cvar_t* electpercentage;
cvar_t* matchtime;
cvar_t* matchsetuptime;
cvar_t* matchstarttime;
cvar_t* admin_password;
cvar_t* allow_admin;
cvar_t* warp_list;
cvar_t* warn_unbalanced;

// Index for various CTF pics, this saves us from calling gi.imageindex
// all the time and saves a few CPU cycles since we don't have to do
// a bunch of string compares all the time.
// These are set in CTFPrecache() called from worldspawn
int imageindex_i_ctf1;
int imageindex_i_ctf2;
int imageindex_i_ctf1d;
int imageindex_i_ctf2d;
int imageindex_i_ctf1t;
int imageindex_i_ctf2t;
int imageindex_i_ctfj;
int imageindex_sbfctf1;
int imageindex_sbfctf2;
int imageindex_ctfsb1;
int imageindex_ctfsb2;
int imageindex_strogg;
int imageindex_human;
int imageindex_victory;
int modelindex_flag1, modelindex_flag2; // [Paril-KEX]

constexpr item_id_t tech_ids[] = { IT_TECH_RESISTANCE, IT_TECH_STRENGTH, IT_TECH_HASTE, IT_TECH_REGENERATION };

/*--------------------------------------------------------------------------*/

#ifndef KEX_Q2_GAME
/*
=================
findradius

Returns entities that have origins within a spherical area

findradius (origin, radius)
=================
*/
static edict_t* loc_findradius(edict_t* from, const vec3_t& org, float rad)
{
	vec3_t eorg;
	int	   j;

	if (!from)
		from = g_edicts;
	else
		from++;
	for (; from < &g_edicts[globals.num_edicts]; from++)
	{
		if (!from->inuse)
			continue;
		for (j = 0; j < 3; j++)
			eorg[j] = org[j] - (from->s.origin[j] + (from->mins[j] + from->maxs[j]) * 0.5f);
		if (eorg.length() > rad)
			continue;
		return from;
	}

	return nullptr;
}
#endif

static void loc_buildboxpoints(vec3_t(&p)[8], const vec3_t& org, const vec3_t& mins, const vec3_t& maxs)
{
	p[0] = org + mins;
	p[1] = p[0];
	p[1][0] -= mins[0];
	p[2] = p[0];
	p[2][1] -= mins[1];
	p[3] = p[0];
	p[3][0] -= mins[0];
	p[3][1] -= mins[1];
	p[4] = org + maxs;
	p[5] = p[4];
	p[5][0] -= maxs[0];
	p[6] = p[0];
	p[6][1] -= maxs[1];
	p[7] = p[0];
	p[7][0] -= maxs[0];
	p[7][1] -= maxs[1];
}

static bool loc_CanSee(edict_t* targ, edict_t* inflictor)
{
	trace_t trace;
	vec3_t	targpoints[8];
	int		i;
	vec3_t	viewpoint;

	// bmodels need special checking because their origin is 0,0,0
	if (targ->movetype == MOVETYPE_PUSH)
		return false; // bmodels not supported

	loc_buildboxpoints(targpoints, targ->s.origin, targ->mins, targ->maxs);

	viewpoint = inflictor->s.origin;
	viewpoint[2] += inflictor->viewheight;

	for (i = 0; i < 8; i++)
	{
		trace = gi.traceline(viewpoint, targpoints[i], inflictor, MASK_SOLID);
		if (trace.fraction == 1.0f)
			return true;
	}

	return false;
}
#

/*--------------------------------------------------------------------------*/

void CTFSpawn()
{
	memset(&ctfgame, 0, sizeof(ctfgame));
	CTFSetupTechSpawn();

	if (competition->integer > 1)
	{
		ctfgame.match = MATCH_SETUP;
		ctfgame.matchtime = level.time + gtime_t::from_min(matchsetuptime->value);
	}
}

void CTFInit()
{
	ctf = gi.cvar("ctf", "0", CVAR_SERVERINFO | CVAR_LATCH);
	competition = gi.cvar("competition", "0", CVAR_SERVERINFO);
	matchlock = gi.cvar("matchlock", "1", CVAR_SERVERINFO);
	electpercentage = gi.cvar("electpercentage", "66", CVAR_NOFLAGS);
	matchtime = gi.cvar("matchtime", "20", CVAR_SERVERINFO);
	matchsetuptime = gi.cvar("matchsetuptime", "10", CVAR_NOFLAGS);
	matchstarttime = gi.cvar("matchstarttime", "20", CVAR_NOFLAGS);
	admin_password = gi.cvar("admin_password", "", CVAR_NOFLAGS);
	allow_admin = gi.cvar("allow_admin", "1", CVAR_NOFLAGS);
	warp_list = gi.cvar("warp_list", "q2ctf1 q2ctf2 q2ctf3 q2ctf4 q2ctf5", CVAR_NOFLAGS);
	warn_unbalanced = gi.cvar("warn_unbalanced", "0", CVAR_NOFLAGS);
}

/*
 * Precache CTF items
 */

void CTFPrecache()
{
	imageindex_i_ctf1 = gi.imageindex("i_ctf1");
	imageindex_i_ctf2 = gi.imageindex("i_ctf2");
	imageindex_i_ctf1d = gi.imageindex("i_ctf1d");
	imageindex_i_ctf2d = gi.imageindex("i_ctf2d");
	imageindex_i_ctf1t = gi.imageindex("i_ctf1t");
	imageindex_i_ctf2t = gi.imageindex("i_ctf2t");
	imageindex_i_ctfj = gi.imageindex("i_ctfj");
	//imageindex_sbfctf1 = gi.imageindex("m_cursor14");
	imageindex_sbfctf1 = gi.imageindex("sbfctf1");
	imageindex_sbfctf2 = gi.imageindex("sbfctf2");
	modelindex_flag1 = gi.modelindex("players/male/flag1.md2");
	modelindex_flag2 = gi.modelindex("players/male/flag2.md2");

	imageindex_ctfsb1 = gi.imageindex("tag4");
	imageindex_ctfsb2 = gi.imageindex("tag5");
	imageindex_strogg = gi.imageindex("ach/ACH_eou7_on");

	// Precache all possible images
	int const precache_ach_eou7_on = gi.imageindex("ach/ACH_eou7_on");
	int const precache_ach_eou5_on = gi.imageindex("ach/ACH_eou5_on");
	int const precache_ach_xatrix_on = gi.imageindex("ach/ACH_xatrix_on");
	int const precache_ach_rogue_on = gi.imageindex("ach/ACH_rogue_on");
	int const precache_ach_eou3_on = gi.imageindex("ach/ACH_eou3_on");

	// Assign a random image to imageindex_human
	float const randomValue = frandom();
	if (randomValue < 0.2)
	{
		imageindex_human = precache_ach_eou7_on;
	}
	else if (randomValue < 0.4)
	{
		imageindex_human = precache_ach_eou5_on;
	}
	else if (randomValue < 0.6)
	{
		imageindex_human = precache_ach_xatrix_on;
	}
	else if (randomValue < 0.8)
	{
		imageindex_human = precache_ach_rogue_on;
	}
	else
	{
		imageindex_human = precache_ach_eou3_on;
	}

	PrecacheItem(GetItemByIndex(IT_WEAPON_GRAPPLE));
}

/*--------------------------------------------------------------------------*/

const char* CTFTeamName(int team)
{
	switch (team)
	{
	case CTF_TEAM1:
		return "Stroggicide Squad";
	case CTF_TEAM2:
		return "BLUE";
	case CTF_NOTEAM:
		return "SPECTATOR/AFK";
	}
	return "UNKNOWN"; // Hanzo pointed out this was spelled wrong as "UKNOWN"
}

const char* CTFOtherTeamName(int team)
{
	switch (team)
	{
	case CTF_TEAM1:
		return "BLUE";
	case CTF_TEAM2:
		return "RED";
	}
	return "UNKNOWN"; // Hanzo pointed out this was spelled wrong as "UKNOWN"
}

int CTFOtherTeam(int team)
{
	switch (team)
	{
	case CTF_TEAM1:
		return CTF_TEAM2;
	case CTF_TEAM2:
		return CTF_TEAM1;
	}
	return -1; // invalid value
}

/*--------------------------------------------------------------------------*/

float PlayersRangeFromSpot(edict_t* spot);
bool  SpawnPointClear(edict_t* spot);
void CTFAssignSkin(edict_t* ent, const char* s)
{
	int	  const playernum = ent - g_edicts - 1;
	std::string_view t(s);

	if (size_t const i = t.find_first_of('/'); i != std::string_view::npos)
		t = t.substr(0, i + 1);
	else
		t = "male/";

	ent->client->resp.ctf_team;
	t = G_Fmt("{}\\{}\\default", ent->client->pers.netname, s);


	gi.configstring(CS_PLAYERSKINS + playernum, t.data());

	//	gi.LocClient_Print(ent, PRINT_HIGH, "$g_assigned_team", ent->client->pers.netname);
}

void CTFAssignTeam(gclient_t* who)
{
	edict_t* player;
	uint32_t team1count = 0, team2count = 0;

	who->resp.ctf_state = 0;

	if (!g_teamplay_force_join->integer && !(g_edicts[1 + (who - game.clients)].svflags & SVF_BOT))
	{
		who->resp.ctf_team = CTF_NOTEAM;
		return;
	}

	for (uint32_t i = 1; i <= game.maxclients; i++)
	{
		player = &g_edicts[i];

		if (!player->inuse || player->client == who)
			continue;

		switch (player->client->resp.ctf_team)
		{
		case CTF_TEAM1:
			team1count++;
			break;
		case CTF_TEAM2:
			team2count++;
			break;
		default:
			break;
		}
	}

	who->resp.ctf_team = CTF_TEAM1;

}

/*
================
SelectCTFSpawnPoint

go to a ctf point, but NOT the two points closest
to other players
================
*/
edict_t* SelectCTFSpawnPoint(edict_t* ent, bool force_spawn)
{
	if (ent->client->resp.ctf_state)
	{
		select_spawn_result_t result = SelectDeathmatchSpawnPoint(g_dm_spawn_farthest->integer, force_spawn, false);
		if (result.any_valid)
			return result.spot;
	}

	const char* cname;
	bool use_ground_spawns = false;

	auto count_ground_spawns = []() -> int32_t {
		int32_t count = 0;
		for (size_t i = 0; i < globals.num_edicts; i++) {
			const auto& e = g_edicts[i];
			if (e.inuse && strcmp(e.classname, "info_player_deathmatch") == 0 && e.style != 1) {
				count++;
			}
		}
		return count;
		};

	switch (ent->client->resp.ctf_team)
	{
	case CTF_TEAM1:
		if (strcmp(level.mapname, "mgu4trial") == 0) {
			cname = "info_player_start";
		}
		else if (current_wave_level == 0 && count_ground_spawns() > 0) {
			cname = "info_player_deathmatch";
			use_ground_spawns = true;
		}
		else {
			cname = "info_player_start";
		}
		break;
	case CTF_TEAM2:
		cname = "info_player_team2";
		break;
	default:
	{
		select_spawn_result_t result = SelectDeathmatchSpawnPoint(g_dm_spawn_farthest->integer, force_spawn, true);
		if (result.any_valid)
			return result.spot;
		gi.Com_Error("can't find suitable spectator spawn point");
		return nullptr;
	}
	}

	static std::vector<edict_t*> spawn_points;
	edict_t* spot = nullptr;
	spawn_points.clear();

	while ((spot = G_FindByString<&edict_t::classname>(spot, cname)) != nullptr)
	{
		if (!use_ground_spawns || (use_ground_spawns && spot->style != 1)) {
			spawn_points.push_back(spot);
		}
	}

	if (spawn_points.empty())
	{
		select_spawn_result_t result = SelectDeathmatchSpawnPoint(g_dm_spawn_farthest->integer, force_spawn, true);
		if (!result.any_valid)
			gi.Com_Error("can't find suitable CTF spawn point");
		return result.spot;
	}

	std::shuffle(spawn_points.begin(), spawn_points.end(), mt_rand);
	for (auto& point : spawn_points)
		if (SpawnPointClear(point))
			return point;

	if (force_spawn)
		return random_element(spawn_points);

	return nullptr;
}

/*------------------------------------------------------------------------*/
/*
CTFFragBonuses

Calculate the bonuses for flag defense, flag carrier defense, etc.
Note that bonuses are not cumaltive.  You get one, they are in importance
order.
*/
void CTFFragBonuses(edict_t* targ, edict_t* inflictor, edict_t* attacker)
{
	edict_t* ent;
	item_id_t	flag_item, enemy_flag_item;
	int			otherteam;
	edict_t* flag, * carrier = nullptr;
	const char* c;
	vec3_t		v1, v2;

	if (targ->client && attacker->client)
	{
		if (attacker->client->resp.ghost)
			if (attacker != targ)
				attacker->client->resp.ghost->kills++;
		if (targ->client->resp.ghost)
			targ->client->resp.ghost->deaths++;
	}

	// no bonus for fragging yourself
	if (!targ->client || !attacker->client || targ == attacker)
		return;

	otherteam = CTFOtherTeam(targ->client->resp.ctf_team);
	if (otherteam < 0)
		return; // whoever died isn't on a team

	// same team, if the flag at base, check to he has the enemy flag
	if (targ->client->resp.ctf_team == CTF_TEAM1)
	{
		flag_item = IT_FLAG1;
		enemy_flag_item = IT_FLAG2;
	}
	else
	{
		flag_item = IT_FLAG2;
		enemy_flag_item = IT_FLAG1;
	}

	// did the attacker frag the flag carrier?
	if (targ->client->pers.inventory[enemy_flag_item])
	{
		attacker->client->resp.ctf_lastfraggedcarrier = level.time;
		attacker->client->resp.score += CTF_FRAG_CARRIER_BONUS;
		gi.LocClient_Print(attacker, PRINT_MEDIUM, "$g_bonus_enemy_carrier",
			CTF_FRAG_CARRIER_BONUS);

		// the target had the flag, clear the hurt carrier
		// field on the other team
		for (uint32_t i = 1; i <= game.maxclients; i++)
		{
			ent = g_edicts + i;
			if (ent->inuse && ent->client->resp.ctf_team == otherteam)
				ent->client->resp.ctf_lasthurtcarrier = 0_ms;
		}
		return;
	}

	if (targ->client->resp.ctf_lasthurtcarrier &&
		level.time - targ->client->resp.ctf_lasthurtcarrier < CTF_CARRIER_DANGER_PROTECT_TIMEOUT &&
		!attacker->client->pers.inventory[flag_item])
	{
		// attacker is on the same team as the flag carrier and
		// fragged a guy who hurt our flag carrier
		attacker->client->resp.score += CTF_CARRIER_DANGER_PROTECT_BONUS;
		gi.LocBroadcast_Print(PRINT_MEDIUM, "$g_bonus_flag_defense",
			attacker->client->pers.netname,
			CTFTeamName(attacker->client->resp.ctf_team));
		if (attacker->client->resp.ghost)
			attacker->client->resp.ghost->carrierdef++;
		return;
	}

	// flag and flag carrier area defense bonuses

	// we have to find the flag and carrier entities

	// find the flag
	switch (attacker->client->resp.ctf_team)
	{
	case CTF_TEAM1:
		c = "item_flag_team1";
		break;
	case CTF_TEAM2:
		c = "item_flag_team2";
		break;
	default:
		return;
	}

	flag = nullptr;
	while ((flag = G_FindByString<&edict_t::classname>(flag, c)) != nullptr)
	{
		if (!(flag->spawnflags & SPAWNFLAG_ITEM_DROPPED))
			break;
	}

	if (!flag)
		return; // can't find attacker's flag

	// find attacker's team's flag carrier
	for (uint32_t i = 1; i <= game.maxclients; i++)
	{
		carrier = g_edicts + i;
		if (carrier->inuse &&
			carrier->client->pers.inventory[flag_item])
			break;
		carrier = nullptr;
	}

	// ok we have the attackers flag and a pointer to the carrier

	// check to see if we are defending the base's flag
	v1 = targ->s.origin - flag->s.origin;
	v2 = attacker->s.origin - flag->s.origin;

	if ((v1.length() < CTF_TARGET_PROTECT_RADIUS ||
		v2.length() < CTF_TARGET_PROTECT_RADIUS ||
		loc_CanSee(flag, targ) || loc_CanSee(flag, attacker)) &&
		attacker->client->resp.ctf_team != targ->client->resp.ctf_team)
	{
		// we defended the base flag
		attacker->client->resp.score += CTF_FLAG_DEFENSE_BONUS;
		if (flag->solid == SOLID_NOT)
			gi.LocBroadcast_Print(PRINT_MEDIUM, "$g_bonus_defend_base",
				attacker->client->pers.netname,
				CTFTeamName(attacker->client->resp.ctf_team));
		else
			gi.LocBroadcast_Print(PRINT_MEDIUM, "$g_bonus_defend_flag",
				attacker->client->pers.netname,
				CTFTeamName(attacker->client->resp.ctf_team));
		if (attacker->client->resp.ghost)
			attacker->client->resp.ghost->basedef++;
		return;
	}

	if (carrier && carrier != attacker)
	{
		v1 = targ->s.origin - carrier->s.origin;
		v2 = attacker->s.origin - carrier->s.origin;

		if (v1.length() < CTF_ATTACKER_PROTECT_RADIUS ||
			v2.length() < CTF_ATTACKER_PROTECT_RADIUS ||
			loc_CanSee(carrier, targ) || loc_CanSee(carrier, attacker))
		{
			attacker->client->resp.score += CTF_CARRIER_PROTECT_BONUS;
			gi.LocBroadcast_Print(PRINT_MEDIUM, "$g_bonus_defend_carrier",
				attacker->client->pers.netname,
				CTFTeamName(attacker->client->resp.ctf_team));
			if (attacker->client->resp.ghost)
				attacker->client->resp.ghost->carrierdef++;
			return;
		}
	}
}

void CTFCheckHurtCarrier(edict_t* targ, edict_t* attacker)
{
	item_id_t flag_item;

	if (!targ->client || !attacker->client)
		return;

	if (targ->client->resp.ctf_team == CTF_TEAM1)
		flag_item = IT_FLAG2;
	else
		flag_item = IT_FLAG1;

	if (targ->client->pers.inventory[flag_item] &&
		targ->client->resp.ctf_team != attacker->client->resp.ctf_team)
		attacker->client->resp.ctf_lasthurtcarrier = level.time;
}

/*------------------------------------------------------------------------*/

void CTFResetFlag(int ctf_team)
{
	const char* c;
	edict_t* ent;

	switch (ctf_team)
	{
	case CTF_TEAM1:
		c = "item_flag_team1";
		break;
	case CTF_TEAM2:
		c = "item_flag_team2";
		break;
	default:
		return;
	}

	ent = nullptr;
	while ((ent = G_FindByString<&edict_t::classname>(ent, c)) != nullptr)
	{
		if (ent->spawnflags.has(SPAWNFLAG_ITEM_DROPPED))
			G_FreeEdict(ent);
		else
		{
			ent->svflags &= ~SVF_NOCLIENT;
			ent->solid = SOLID_TRIGGER;
			gi.linkentity(ent);
			ent->s.event = EV_ITEM_RESPAWN;
		}
	}
}

void CTFResetFlags()
{
	CTFResetFlag(CTF_TEAM1);
	CTFResetFlag(CTF_TEAM2);
}

bool CTFPickup_Flag(edict_t* ent, edict_t* other)
{
	int		  ctf_team;
	edict_t* player;
	item_id_t flag_item, enemy_flag_item;

	// figure out what team this flag is
	if (ent->item->id == IT_FLAG1)
		ctf_team = CTF_TEAM1;
	else if (ent->item->id == IT_FLAG2)
		ctf_team = CTF_TEAM2;
	else
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Don't know what team the flag is on.\n");
		return false;
	}

	// same team, if the flag at base, check to he has the enemy flag
	if (ctf_team == CTF_TEAM1)
	{
		flag_item = IT_FLAG1;
		enemy_flag_item = IT_FLAG2;
	}
	else
	{
		flag_item = IT_FLAG2;
		enemy_flag_item = IT_FLAG1;
	}

	if (ctf_team == other->client->resp.ctf_team)
	{

		if (!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED))
		{
			// the flag is at home base.  if the player has the enemy
			// flag, he's just won!

			if (other->client->pers.inventory[enemy_flag_item])
			{
				gi.LocBroadcast_Print(PRINT_HIGH, "$g_flag_captured",
					other->client->pers.netname, CTFOtherTeamName(ctf_team));
				other->client->pers.inventory[enemy_flag_item] = 0;

				ctfgame.last_flag_capture = level.time;
				ctfgame.last_capture_team = ctf_team;
				if (ctf_team == CTF_TEAM1)
					ctfgame.team1++;
				else
					ctfgame.team2++;

				gi.sound(ent, CHAN_RELIABLE | CHAN_NO_PHS_ADD | CHAN_AUX, gi.soundindex("ctf/flagcap.wav"), 1, ATTN_NONE, 0);

				// other gets another 10 frag bonus
				other->client->resp.score += CTF_CAPTURE_BONUS;
				if (other->client->resp.ghost)
					other->client->resp.ghost->caps++;

				// Ok, let's do the player loop, hand out the bonuses
				for (uint32_t i = 1; i <= game.maxclients; i++)
				{
					player = &g_edicts[i];
					if (!player->inuse)
						continue;

					if (player->client->resp.ctf_team != other->client->resp.ctf_team)
						player->client->resp.ctf_lasthurtcarrier = -5_sec;
					else if (player->client->resp.ctf_team == other->client->resp.ctf_team)
					{
						if (player != other)
							player->client->resp.score += CTF_TEAM_BONUS;
						// award extra points for capture assists
						if (player->client->resp.ctf_lastreturnedflag && player->client->resp.ctf_lastreturnedflag + CTF_RETURN_FLAG_ASSIST_TIMEOUT > level.time)
						{
							gi.LocBroadcast_Print(PRINT_HIGH, "$g_bonus_assist_return", player->client->pers.netname);
							player->client->resp.score += CTF_RETURN_FLAG_ASSIST_BONUS;
						}
						if (player->client->resp.ctf_lastfraggedcarrier && player->client->resp.ctf_lastfraggedcarrier + CTF_FRAG_CARRIER_ASSIST_TIMEOUT > level.time)
						{
							gi.LocBroadcast_Print(PRINT_HIGH, "$g_bonus_assist_frag_carrier", player->client->pers.netname);
							player->client->resp.score += CTF_FRAG_CARRIER_ASSIST_BONUS;
						}
					}
				}

				CTFResetFlags();
				return false;
			}
			return false; // its at home base already
		}
		// hey, its not home.  return it by teleporting it back
		gi.LocBroadcast_Print(PRINT_HIGH, "$g_returned_flag",
			other->client->pers.netname, CTFTeamName(ctf_team));
		other->client->resp.score += CTF_RECOVERY_BONUS;
		other->client->resp.ctf_lastreturnedflag = level.time;
		gi.sound(ent, CHAN_RELIABLE | CHAN_NO_PHS_ADD | CHAN_AUX, gi.soundindex("ctf/flagret.wav"), 1, ATTN_NONE, 0);
		// CTFResetFlag will remove this entity!  We must return false
		CTFResetFlag(ctf_team);
		return false;
	}

	// hey, its not our flag, pick it up
	gi.LocBroadcast_Print(PRINT_HIGH, "$g_got_flag",
		other->client->pers.netname, CTFTeamName(ctf_team));
	other->client->resp.score += CTF_FLAG_BONUS;

	other->client->pers.inventory[flag_item] = 1;
	other->client->resp.ctf_flagsince = level.time;

	// pick up the flag
	// if it's not a dropped flag, we just make is disappear
	// if it's dropped, it will be removed by the pickup caller
	if (!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED))
	{
		ent->flags |= FL_RESPAWN;
		ent->svflags |= SVF_NOCLIENT;
		ent->solid = SOLID_NOT;
	}
	return true;
}

TOUCH(CTFDropFlagTouch) (edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	// owner (who dropped us) can't touch for two secs
	if (other == ent->owner &&
		ent->nextthink - level.time > CTF_AUTO_FLAG_RETURN_TIMEOUT - 2_sec)
		return;

	Touch_Item(ent, other, tr, other_touching_self);
}

THINK(CTFDropFlagThink) (edict_t* ent) -> void
{
	// auto return the flag
	// reset flag will remove ourselves
	if (ent->item->id == IT_FLAG1)
	{
		CTFResetFlag(CTF_TEAM1);
		gi.LocBroadcast_Print(PRINT_HIGH, "$g_flag_returned",
			CTFTeamName(CTF_TEAM1));
	}
	else if (ent->item->id == IT_FLAG2)
	{
		CTFResetFlag(CTF_TEAM2);
		gi.LocBroadcast_Print(PRINT_HIGH, "$g_flag_returned",
			CTFTeamName(CTF_TEAM2));
	}

	gi.sound(ent, CHAN_RELIABLE | CHAN_NO_PHS_ADD | CHAN_AUX, gi.soundindex("ctf/flagret.wav"), 1, ATTN_NONE, 0);
}

// Called from PlayerDie, to drop the flag from a dying player
void CTFDeadDropFlag(edict_t* self)
{
	edict_t* dropped = nullptr;

	if (self->client->pers.inventory[IT_FLAG1])
	{
		dropped = Drop_Item(self, GetItemByIndex(IT_FLAG1));
		self->client->pers.inventory[IT_FLAG1] = 0;
		gi.LocBroadcast_Print(PRINT_HIGH, "$g_lost_flag",
			self->client->pers.netname, CTFTeamName(CTF_TEAM1));
	}
	else if (self->client->pers.inventory[IT_FLAG2])
	{
		dropped = Drop_Item(self, GetItemByIndex(IT_FLAG2));
		self->client->pers.inventory[IT_FLAG2] = 0;
		gi.LocBroadcast_Print(PRINT_HIGH, "$g_lost_flag",
			self->client->pers.netname, CTFTeamName(CTF_TEAM2));
	}

	if (dropped)
	{
		dropped->think = CTFDropFlagThink;
		dropped->nextthink = level.time + CTF_AUTO_FLAG_RETURN_TIMEOUT;
		dropped->touch = CTFDropFlagTouch;
	}
}

void CTFDrop_Flag(edict_t* ent, gitem_t* item)
{
	if (brandom())
		gi.LocClient_Print(ent, PRINT_HIGH, "$g_lusers_drop_flags");
	else
		gi.LocClient_Print(ent, PRINT_HIGH, "$g_winners_drop_flags");
}

THINK(CTFFlagThink) (edict_t* ent) -> void
{
	if (ent->solid != SOLID_NOT)
		ent->s.frame = 173 + (((ent->s.frame - 173) + 1) % 16);
	ent->nextthink = level.time + 10_hz;
}

THINK(CTFFlagSetup) (edict_t* ent) -> void
{
	trace_t tr;
	vec3_t	dest;

	ent->mins = { -15, -15, -15 };
	ent->maxs = { 15, 15, 15 };

	if (ent->model)
		gi.setmodel(ent, ent->model);
	else
		gi.setmodel(ent, ent->item->world_model);
	ent->solid = SOLID_TRIGGER;
	ent->movetype = MOVETYPE_TOSS;
	ent->touch = Touch_Item;
	ent->s.frame = 173;

	dest = ent->s.origin + vec3_t{ 0, 0, -128 };

	tr = gi.trace(ent->s.origin, ent->mins, ent->maxs, dest, ent, MASK_SOLID);
	if (tr.startsolid)
	{
		gi.Com_PrintFmt("PRINT: CTFFlagSetup: {} startsolid at {}\n", ent->classname, ent->s.origin);
		G_FreeEdict(ent);
		return;
	}

	ent->s.origin = tr.endpos;

	gi.linkentity(ent);

	ent->nextthink = level.time + 10_hz;
	ent->think = CTFFlagThink;
}

void CTFEffects(edict_t* player)
{
	player->s.effects &= ~(EF_FLAG1 | EF_FLAG2);
	if (player->health > 0)
	{
		if (player->client->pers.inventory[IT_FLAG1])
		{
			player->s.effects |= EF_FLAG1;
		}
		if (player->client->pers.inventory[IT_FLAG2])
		{
			player->s.effects |= EF_FLAG2;
		}
	}

	if (player->client->pers.inventory[IT_FLAG1])
		player->s.modelindex3 = modelindex_flag1;
	else if (player->client->pers.inventory[IT_FLAG2])
		player->s.modelindex3 = modelindex_flag2;
	else
		player->s.modelindex3 = 0;
}

// called when we enter the intermission
void CTFCalcScores()
{
	ctfgame.total1 = ctfgame.total2 = 0;
	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		if (!g_edicts[i + 1].inuse)
			continue;
		if (game.clients[i].resp.ctf_team == CTF_TEAM1)
			ctfgame.total1 += game.clients[i].resp.score;
	}
}

// [Paril-KEX] end game rankings
void CTFCalcRankings(std::array<uint32_t, MAX_CLIENTS>& player_ranks)
{
	// we're all winners.. or losers. whatever
	if (ctfgame.total1 == ctfgame.total2)
	{
		player_ranks.fill(1);
		return;
	}

	ctfteam_t const winning_team = (ctfgame.total1 > ctfgame.total2) ? CTF_TEAM1 : CTF_TEAM2;

	for (const auto* const player : active_players())
		if (player->client->pers.spawned && player->client->resp.ctf_team != CTF_NOTEAM)
			player_ranks[player->s.number - 1] = player->client->resp.ctf_team == winning_team ? 1 : 2;
}

void CheckEndTDMLevel()
{
	if (ctfgame.total1 >= fraglimit->integer || ctfgame.total2 >= fraglimit->integer)
	{
		gi.LocBroadcast_Print(PRINT_HIGH, "$g_fraglimit_hit");
		EndDMLevel();
	}
}

void CTFID_f(edict_t* ent)
{
	if (ent->client->pers.id_state)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Disabling player identication display.\n");
		ent->client->pers.id_state = false;
	}
	else
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Activating player identication display.\n");
		ent->client->pers.id_state = true;
	}
}

void DMGID_f(edict_t* ent)
{
	if (ent->client->pers.iddmg_state)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Disabling damage identication display.\n");
		ent->client->pers.iddmg_state = false;
	}
	else
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Activating damage identication display.\n");
		ent->client->pers.iddmg_state = true;
	}
}

//void BFGMODE_f(edict_t* ent)
//{
//	if (ent->client->resp.slidebfg)
//	{
//		gi.LocClient_Print(ent, PRINT_HIGH, "BFG Mode switched to PULL.\n");
//		ent->client->resp.slidebfg = false;
//	}
//	else
//	{
//		gi.LocClient_Print(ent, PRINT_HIGH, "BFG MODE switched to SLIDE.\n");
//		ent->client->resp.slidebfg = true;
//	}
//}

#include "../shared.h"
#include <queue>
#include <sstream>
#include <span>

constexpr gtime_t TESLA_TIME_TO_LIVE = gtime_t::from_sec(60);

// Lista de prefijos válidos para IsValidClassname
static constexpr std::array<std::string_view, 6> ALLOWED_PREFIXES = { {
	"monster_",
	"misc_insane",
	"tesla_mine",
	"food_cube_trap",
	"emitter",
	"doppleganger"
} };

[[nodiscard]] inline std::string GetDisplayName(const char* classname) {
	if (!classname) return "Unknown";
	const auto it = name_replacements.find(classname);
	return std::string(it != name_replacements.end() ? it->second : classname);
}

[[nodiscard]] inline std::string FormatClassname(const std::string& classname) {
	std::string result;
	result.reserve(classname.length());

	std::stringstream ss(classname);
	std::string segment;
	bool first_word = true;

	while (std::getline(ss, segment, '_')) {
		if (!first_word) result += ' ';
		if (!segment.empty()) {
			segment[0] = toupper(segment[0]);
			result += segment;
		}
		first_word = false;
	}

	return result;
}

bool IsValidClassname(const char* classname) noexcept {
	if (!classname) return false;
	for (const auto prefix : ALLOWED_PREFIXES) {
		if (strncmp(classname, prefix.data(), prefix.length()) == 0) {
			return true;
		}
	}
	return false;
}

bool IsValidTarget(edict_t* ent, edict_t* other, bool vis) {
	if (!other || !other->inuse || !other->takedamage || other->solid == SOLID_NOT) {
		return false;
	}

	if (other == ent || other->svflags & SVF_DEADMONSTER) {
		return false;
	}

	if (vis && ent && !visible(ent, other)) {
		return false;
	}

	return other->client || IsValidClassname(other->classname);
}

int GetArmorInfo(edict_t* ent) {
	if (ent->svflags & SVF_MONSTER) {
		return ent->monsterinfo.power_armor_power;
	}

	if (!ent->client) {
		return 0;
	}

	int const index = ArmorIndex(ent);
	return (index != IT_NULL) ? ent->client->pers.inventory[index] : 0;
}

template<typename Duration>
int GetRemainingTime(gtime_t current_time, gtime_t end_time) {
	return std::max(0, static_cast<int>((end_time - current_time).template seconds<float>()));
}

enum class EntityType {
	Player,
	Monster,
	Other
};

[[nodiscard]] inline EntityType GetEntityType(const edict_t* ent) {
	if (!ent) return EntityType::Other;
	if (ent->client) return EntityType::Player;
	if (ent->svflags & SVF_MONSTER) return EntityType::Monster;
	return EntityType::Other;
}

[[nodiscard]] inline std::string FormatEntityInfo(edict_t* ent) {
	if (!ent || !ent->inuse) {
		return {};
	}

	std::string info;
	info.reserve(256);

	const EntityType type = GetEntityType(ent);

	switch (type) {
	case EntityType::Monster: {
		// Get the title first for monsters
		std::string title = GetTitleFromFlags(ent->monsterinfo.bonus_flags);
		std::string name = GetDisplayName(ent->classname ? ent->classname : "Unknown Monster");

		fmt::format_to(std::back_inserter(info), "{}{}\nH: {}",
			title, FormatClassname(name), ent->health);

		if (ent->monsterinfo.armor_power >= 1) {
			fmt::format_to(std::back_inserter(info), " A: {}", ent->monsterinfo.armor_power);
		}

		if (ent->monsterinfo.power_armor_power >= 1) {
			fmt::format_to(std::back_inserter(info), " PA: {}", ent->monsterinfo.power_armor_power);
		}

		struct PowerupInfo {
			gtime_t time;
			const char* label;
		};

		std::array<PowerupInfo, 4> powerups{ {
			{ent->monsterinfo.quad_time, "Quad"},
			{ent->monsterinfo.double_time, "Double"},
			{ent->monsterinfo.invincible_time, "Invuln"},
			{ent->monsterinfo.quadfire_time, "Accel"}
		} };

		for (const auto& powerup : powerups) {
			if (powerup.time > level.time) {
				fmt::format_to(std::back_inserter(info), "\n{}: {}s",
					powerup.label, GetRemainingTime<float>(level.time, powerup.time));
			}
		}
		break;
	}

	case EntityType::Player: {
		std::string playerName = GetPlayerName(ent);
	//	if (playerName.empty()) return {};

		fmt::format_to(std::back_inserter(info), "{}\nH: {}", playerName, ent->health);

		if (int armor = GetArmorInfo(ent); armor > 0) {
			fmt::format_to(std::back_inserter(info), " A: {}", armor);
		}
		break;
	}

	case EntityType::Other: {
		if (!ent->classname) return {};
		// Use the char* overload for other entities
		std::string name = GetDisplayName(ent->classname);
		if (name.empty()) return {};

		fmt::format_to(std::back_inserter(info), "{}\nH: {}", name, ent->health);

		if (strcmp(ent->classname, "tesla_mine") == 0 ||
			strcmp(ent->classname, "food_cube_trap") == 0) {

			gtime_t const time_active = level.time - ent->timestamp;
			gtime_t const time_remaining = (strcmp(ent->classname, "tesla_mine") == 0)
				? TESLA_TIME_TO_LIVE - time_active
				: -time_active;

			fmt::format_to(std::back_inserter(info), " T: {}s",
				GetRemainingTime<float>(gtime_t{}, time_remaining));
		}
		else if (strcmp(ent->classname, "emitter") == 0 &&
			ent->owner && ent->owner->inuse) {
			fmt::format_to(std::back_inserter(info), " DMG: {}", ent->owner->health);
			gtime_t const time_active = level.time - ent->owner->timestamp;
			gtime_t const time_remaining = ent->timestamp - time_active;
			fmt::format_to(std::back_inserter(info), " T: {}s",
				GetRemainingTime<float>(gtime_t{}, time_remaining));
		}
		break;
	}
	}

	return info;
}

class EntityInfoManager {
public:
	static constexpr size_t MAX_ENTITY_INFOS = ENTITY_INFO_COUNT;
	static constexpr size_t MAX_STRING_LENGTH = 256;
	static constexpr gtime_t UPDATE_INTERVAL = 107_ms;
	static constexpr gtime_t STALE_THRESHOLD = 3000_ms;

private:
	struct EntityInfo {
		std::array<char, MAX_STRING_LENGTH> data{};
		uint16_t length{ 0 };
		int32_t config_string_id{ -1 };
		gtime_t last_update{ 0_ms };

		bool needsUpdate(std::string_view newInfo, gtime_t currentTime) const noexcept {
			// Only update if the content actually changed AND enough time has passed
			bool content_changed = length != newInfo.length() ||
				std::memcmp(data.data(), newInfo.data(), newInfo.length()) != 0;

			return content_changed &&
				(currentTime - last_update > UPDATE_INTERVAL);
		}

		void update(std::string_view newInfo, gtime_t currentTime) noexcept {
			std::memcpy(data.data(), newInfo.data(), newInfo.length());
			length = static_cast<uint16_t>(newInfo.length());
			data[newInfo.length()] = '\0';
			last_update = currentTime;
		}
	};

	std::array<EntityInfo, MAX_ENTITY_INFOS> m_entities;
	std::vector<int> m_entityToSlot;
	std::vector<int> m_freeSlots;
	uint16_t m_activeCount{ 0 };

public:
	EntityInfoManager() noexcept {
		m_freeSlots.reserve(MAX_ENTITY_INFOS);
		for (int i = 0; i < MAX_ENTITY_INFOS; ++i) {
			m_freeSlots.push_back(i);
			// Use explicit offset from CONFIG_ENTITY_INFO_START
			m_entities[i].config_string_id = CONFIG_ENTITY_INFO_START + i;
		}
		m_entityToSlot.resize(MAX_EDICTS, -1);
	}

	bool updateEntityInfo(int entityIndex, std::string_view info) noexcept {
		// Add bounds check for entity index
		if (entityIndex < 0 || entityIndex >= MAX_EDICTS) {
			return false;
		}

		if (info.length() >= MAX_STRING_LENGTH) {
			return false;
		}

		int slotIndex = m_entityToSlot[entityIndex];

		// If no slot is assigned
		if (slotIndex == -1) {
			if (m_freeSlots.empty()) {
				// You could also optionally call cleanupStaleEntries() here
				// but be mindful of the potential performance impact.
				return false;
			}

			slotIndex = m_freeSlots.back();
			m_freeSlots.pop_back();
			m_entityToSlot[entityIndex] = slotIndex;
			m_activeCount++;
		}

		// Update only if necessary
		auto& entity = m_entities[slotIndex];
		if (entity.needsUpdate(info, level.time)) {
			entity.update(info, level.time);
			gi.configstring(entity.config_string_id, entity.data.data());
		}

		return true;
	}

	static constexpr bool isValidConfigStringId(int32_t id) noexcept {
		return id >= CONFIG_ENTITY_INFO_START &&
			id < (CONFIG_ENTITY_INFO_START + MAX_ENTITY_INFOS);
	}

	void removeEntityInfo(int entityIndex) noexcept {
		if (entityIndex < 0 || entityIndex >= MAX_EDICTS)
			return;

		int const slotIndex = m_entityToSlot[entityIndex];
		if (slotIndex == -1)
			return; // Entity not found or already removed

		if (slotIndex < 0 || slotIndex >= MAX_ENTITY_INFOS)
			return;

		auto& entity = m_entities[slotIndex];

		if (isValidConfigStringId(entity.config_string_id)) {
			gi.configstring(entity.config_string_id, "");
		}

		entity.length = 0;
		entity.data[0] = '\0';
		entity.last_update = 0_ms;

		m_freeSlots.push_back(slotIndex);
		m_entityToSlot[entityIndex] = -1; // Mark slot as free		m_activeCount--;
	}

	[[nodiscard]] int getConfigStringIndex(int entityIndex) const noexcept {
		if (entityIndex < 0 || entityIndex >= MAX_EDICTS) {
			return -1;
		}

		int const slotIndex = m_entityToSlot[entityIndex];
		if (slotIndex != -1) {
			// Add an assertion for extra safety during development
			assert(slotIndex >= 0 && slotIndex < MAX_ENTITY_INFOS && "Slot index out of bounds!");
			if (slotIndex >= 0 && slotIndex < MAX_ENTITY_INFOS)
				return m_entities[slotIndex].config_string_id;
		}
		return -1;
	}

	[[nodiscard]] bool hasEntityInfo(int entityIndex) const noexcept {
		if (entityIndex < 0 || entityIndex >= MAX_EDICTS)
			return false;
		return m_entityToSlot[entityIndex] != -1;
	}

	[[nodiscard]] size_t getActiveCount() const noexcept {
		return m_activeCount;
	}

	[[nodiscard]] size_t getAvailableSlots() const noexcept {
		return m_freeSlots.size();
	}

	void cleanupStaleEntries() noexcept {
		for (int entityIndex = 0; entityIndex < MAX_EDICTS; ++entityIndex) {
			int const slotIndex = m_entityToSlot[entityIndex];
			if (slotIndex != -1) {
				if (level.time - m_entities[slotIndex].last_update > STALE_THRESHOLD) {
					removeEntityInfo(entityIndex);
				}
			}
		}
	}
};

inline EntityInfoManager g_entityInfoManager;
struct CTFIDViewConfig {
	static constexpr gtime_t UPDATE_INTERVAL = 107_ms;
	static constexpr float MAX_DISTANCE = 2048.0f;
	static constexpr float MIN_DOT = 0.98f;
	static constexpr float CLOSE_DISTANCE = 100.0f;
	static constexpr float CLOSE_MIN_DOT = 0.5f;
};

[[nodiscard]] bool IsInFieldOfView(const vec3_t& viewer_pos, const vec3_t& viewer_forward,
	const vec3_t& target_pos, float min_dot, float max_distance) noexcept {
	vec3_t dir = target_pos - viewer_pos;
	float const dist = dir.normalize();

	return dist < max_distance && viewer_forward.dot(dir) > min_dot;
}

[[nodiscard]] bool CanSeeTarget(const edict_t* viewer, const vec3_t& start,
	const edict_t* target, const vec3_t& end) noexcept {
	trace_t const tr = gi.traceline(start, end, viewer, MASK_SOLID);
	return tr.fraction == 1.0f || tr.ent == target;
}

struct TargetSearchResult {
	edict_t* target{ nullptr };
	float distance{ CTFIDViewConfig::MAX_DISTANCE };
};

[[nodiscard]] TargetSearchResult FindBestTarget(edict_t* ent, const vec3_t& forward) noexcept {
	TargetSearchResult result;
	vec3_t const& viewer_pos = ent->s.origin;

	// Check players first
	for (edict_t* who : active_players()) {
		if (!IsValidTarget(ent, who, false)) continue;

		vec3_t dir = who->s.origin - viewer_pos;
		float const dist = dir.normalize();

		float const min_dot = (dist < CTFIDViewConfig::CLOSE_DISTANCE)
			? CTFIDViewConfig::CLOSE_MIN_DOT
			: CTFIDViewConfig::MIN_DOT;

		if (dist >= result.distance || forward.dot(dir) <= min_dot) continue;
		if (!CanSeeTarget(ent, viewer_pos, who, who->s.origin)) continue;

		result.distance = dist;
		result.target = who;
	}

	// Then check monsters
	for (edict_t* who : active_monsters()) {
		if (!IsValidTarget(ent, who, false)) continue;

		vec3_t dir = who->s.origin - viewer_pos;
		float const dist = dir.normalize();

		float const min_dot = (dist < CTFIDViewConfig::CLOSE_DISTANCE)
			? CTFIDViewConfig::CLOSE_MIN_DOT
			: CTFIDViewConfig::MIN_DOT;

		if (dist >= result.distance || forward.dot(dir) <= min_dot) continue;
		if (!CanSeeTarget(ent, viewer_pos, who, who->s.origin)) continue;

		result.distance = dist;
		result.target = who;
	}

	// Finally check other valid entities (tesla mines, traps, etc)
	for (edict_t* who = g_edicts + 1; who < g_edicts + globals.num_edicts; who++) {
		// Skip players and monsters as we already checked them
		if (who->client || (who->svflags & SVF_MONSTER)) continue;

		// Check if it's a valid other entity
		if (!IsValidTarget(ent, who, false)) continue;

		vec3_t dir = who->s.origin - viewer_pos;
		float const dist = dir.normalize();

		float const min_dot = (dist < CTFIDViewConfig::CLOSE_DISTANCE)
			? CTFIDViewConfig::CLOSE_MIN_DOT
			: CTFIDViewConfig::MIN_DOT;

		if (dist >= result.distance || forward.dot(dir) <= min_dot) continue;
		if (!CanSeeTarget(ent, viewer_pos, who, who->s.origin)) continue;

		result.distance = dist;
		result.target = who;
	}

	return result;
}

void CTFSetIDView(edict_t* ent) {
	static bool is_processing = false;

	if (is_processing || level.intermissiontime ||
		level.time - ent->client->resp.lastidtime < CTFIDViewConfig::UPDATE_INTERVAL) {
		return;
	}

	is_processing = true;
	struct ScopeGuard {
		~ScopeGuard() { is_processing = false; }
	} guard;

	ent->client->resp.lastidtime = level.time;
	// Clear only the target health string stat
	ent->client->ps.stats[STAT_TARGET_HEALTH_STRING] = 0;

	vec3_t forward;
	AngleVectors(ent->client->v_angle, forward, nullptr, nullptr);

	TargetSearchResult result = FindBestTarget(ent, forward);
	edict_t* best = result.target;

	if (!best && ent->client->idtarget && IsValidTarget(ent, ent->client->idtarget, true)) {
		best = ent->client->idtarget;
	}

	if (best) {
		ent->client->idtarget = best;
		int const entity_index = best - g_edicts;

		// Use EntityInfoManager for all entities (players and monsters)
		std::string info_string = FormatEntityInfo(best);
		if (g_entityInfoManager.updateEntityInfo(entity_index, info_string)) {
			int const config_string_id = g_entityInfoManager.getConfigStringIndex(entity_index);
			if (config_string_id != -1) {
				ent->client->ps.stats[STAT_TARGET_HEALTH_STRING] = config_string_id;
			}
		}
		return;
	}

	ent->client->idtarget = nullptr;
}

void OnEntityDeath(edict_t* self) noexcept
{
	// Early exit checks
	if (!self || !self->inuse || self->monsterinfo.death_processed) {
		return;
	}

	if (self->is_fading_out)
		return;

	self->monsterinfo.death_processed = true;

	// Clear boss status if needed
	if (self->monsterinfo.IS_BOSS) {
		self->monsterinfo.IS_BOSS = false;
	}

	// Mark for EntityInfoManager cleanup
	int32_t const entity_index = static_cast<int32_t>(self - g_edicts);
	if (static_cast<uint32_t>(entity_index) < MAX_EDICTS) {
		g_entityInfoManager.removeEntityInfo(entity_index);
	}

	// Setup cleanup timing and flags
	if (g_horde->integer) {
		constexpr gtime_t FADE_START_DELAY = 4_sec;
		constexpr gtime_t FADE_DURATION = 3_sec;
		self->teleport_time = level.time + FADE_START_DELAY;
		self->timestamp = level.time + FADE_START_DELAY + FADE_DURATION;
		self->wait = FADE_DURATION.seconds();
		self->monsterinfo.aiflags |= AI_CLEANUP_FADE;
		self->s.renderfx &= ~RF_DOT_SHADOW;
	}
	else {
		self->timestamp = level.time + 2_sec;
		self->monsterinfo.aiflags |= AI_CLEANUP_NORMAL;
	}
}// OnEntityRemoved now simply calls OnEntityDeath
inline void OnEntityRemoved(edict_t* self) noexcept {
	OnEntityDeath(self);
}

// CleanupInvalidEntities - remains largely the same
void CleanupInvalidEntities() {
	for (uint32_t i = 0; i < (globals.num_edicts); i++) {
		edict_t* ent = &g_edicts[i];
		if (ent->inuse && ent->svflags & SVF_MONSTER) {
			if (ent->solid == SOLID_NOT && ent->health > 0) {
				gi.Com_PrintFmt("PRINT: Removing Bug/Immortal monster: {}\n", ent->classname);
				ent->health = -1;
				OnEntityDeath(ent);
				G_FreeEdict(ent);
			}
		}
	}
}

void SetCTFStats(edict_t* ent)
{
	uint32_t i;
	int		 p1, p2;
	edict_t* e;

	//if (ctfgame.match > MATCH_NONE)
	//	ent->client->ps.stats[STAT_CTF_MATCH] = CONFIG_CTF_MATCH;     //unused so it works on horde hud 
	//else
//		ent->client->ps.stats[STAT_CTF_MATCH] = 0;

	//if (ctfgame.warnactive)
	//	ent->client->ps.stats[STAT_CTF_TEAMINFO] = CONFIG_CTF_TEAMINFO;
	//else
	//	ent->client->ps.stats[STAT_CTF_TEAMINFO] = 0;

	// ghosting
	if (ent->client->resp.ghost)
	{
		ent->client->resp.ghost->score = ent->client->resp.score;
		Q_strlcpy(ent->client->resp.ghost->netname, ent->client->pers.netname, sizeof(ent->client->resp.ghost->netname));
		ent->client->resp.ghost->number = ent->s.number;
	}

	// logo headers for the frag display
	if (level.intermissiontime)
	{
		ent->client->ps.stats[STAT_CTF_TEAM1_HEADER] = imageindex_human;

	//	ent->client->ps.stats[STAT_CTF_ID_VIEW] = 0;
	//	ent->client->ps.stats[STAT_CTF_ID_VIEW] = 0;

		ent->client->ps.stats[STAT_CTF_TEAM2_HEADER] = imageindex_strogg;

	}
	else
		ent->client->ps.stats[STAT_CTF_TEAM1_HEADER] = imageindex_ctfsb1;

	ent->client->ps.stats[STAT_CTF_TEAM2_HEADER] = imageindex_ctfsb2;


	bool const blink = 0;

	// if during intermission, we must blink the team header of the winning team
/*	if (level.intermissiontime && blink)
	{
		// blink half second
		// note that ctfgame.total[12] is set when we go to intermission
		if (ctfgame.team1 > ctfgame.team2)
			ent->client->ps.stats[STAT_CTF_TEAM1_HEADER] = 0;
		else if (ctfgame.team2 > ctfgame.team1)
			ent->client->ps.stats[STAT_CTF_TEAM2_HEADER] = 0;
		else if (ctfgame.total1 > ctfgame.total2) // frag tie breaker
			ent->client->ps.stats[STAT_CTF_TEAM1_HEADER] = 0;
		else if (ctfgame.total2 > ctfgame.total1)
			ent->client->ps.stats[STAT_CTF_TEAM2_HEADER] = 0;
		else
		{ // tie game!
			ent->client->ps.stats[STAT_CTF_TEAM1_HEADER] = 0;
			ent->client->ps.stats[STAT_CTF_TEAM2_HEADER] = 0;
		}
	}*/

	// tech icon
	i = 0;
	ent->client->ps.stats[STAT_CTF_TECH] = 0;
	for (; i < q_countof(tech_ids); i++)
	{
		if (ent->client->pers.inventory[tech_ids[i]])
		{
			ent->client->ps.stats[STAT_CTF_TECH] = gi.imageindex(GetItemByIndex(tech_ids[i])->icon);
			break;
		}
	}

	if (ctf->integer)
	{
		// figure out what icon to display for team logos
		// three states:
		//   flag at base
		//   flag taken
		//   flag dropped
		p1 = imageindex_i_ctf1;
		e = G_FindByString<&edict_t::classname>(nullptr, "item_flag_team1");
		if (e != nullptr)
		{
			if (e->solid == SOLID_NOT)
			{
				// not at base
				// check if on player
				p1 = imageindex_i_ctf1d; // default to dropped
				for (i = 1; i <= game.maxclients; i++)
					if (g_edicts[i].inuse &&
						g_edicts[i].client->pers.inventory[IT_FLAG1])
					{
						// enemy has it
						p1 = imageindex_i_ctf1t;
						break;
					}

				// [Paril-KEX] make sure there is a dropped version on the map somewhere
				if (p1 == imageindex_i_ctf1d)
				{
					e = G_FindByString<&edict_t::classname>(e, "item_flag_team1");

					if (e == nullptr)
					{
						CTFResetFlag(CTF_TEAM1);
						gi.LocBroadcast_Print(PRINT_HIGH, "$g_flag_returned",
							CTFTeamName(CTF_TEAM1));
						gi.sound(ent, CHAN_RELIABLE | CHAN_NO_PHS_ADD | CHAN_AUX, gi.soundindex("ctf/flagret.wav"), 1, ATTN_NONE, 0);
					}
				}
			}
			else if (e->spawnflags.has(SPAWNFLAG_ITEM_DROPPED))
				p1 = imageindex_i_ctf1d; // must be dropped
		}
		p2 = imageindex_i_ctf2;
		e = G_FindByString<&edict_t::classname>(nullptr, "item_flag_team2");
		if (e != nullptr)
		{
			if (e->solid == SOLID_NOT)
			{
				// not at base
				// check if on player
				p2 = imageindex_i_ctf2d; // default to dropped
				for (i = 1; i <= game.maxclients; i++)
					if (g_edicts[i].inuse &&
						g_edicts[i].client->pers.inventory[IT_FLAG2])
					{
						// enemy has it
						p2 = imageindex_i_ctf2t;
						break;
					}

				// [Paril-KEX] make sure there is a dropped version on the map somewhere
				if (p2 == imageindex_i_ctf2d)
				{
					e = G_FindByString<&edict_t::classname>(e, "item_flag_team2");

					if (e == nullptr)
					{
						CTFResetFlag(CTF_TEAM2);
						gi.LocBroadcast_Print(PRINT_HIGH, "$g_flag_returned",
							CTFTeamName(CTF_TEAM2));
						gi.sound(ent, CHAN_RELIABLE | CHAN_NO_PHS_ADD | CHAN_AUX, gi.soundindex("ctf/flagret.wav"), 1, ATTN_NONE, 0);
					}
				}
			}
			else if (e->spawnflags.has(SPAWNFLAG_ITEM_DROPPED))
				p2 = imageindex_i_ctf2d; // must be dropped
		}

		ent->client->ps.stats[STAT_CTF_TEAM1_PIC] = p1;
		ent->client->ps.stats[STAT_CTF_TEAM2_PIC] = p2;

		if (ctfgame.last_flag_capture && level.time - ctfgame.last_flag_capture < 5_sec)
		{
			if (ctfgame.last_capture_team == CTF_TEAM1)
				if (blink)
					ent->client->ps.stats[STAT_CTF_TEAM1_PIC] = p1;
				else
					ent->client->ps.stats[STAT_CTF_TEAM1_PIC] = 0;
			else if (blink)
				ent->client->ps.stats[STAT_CTF_TEAM2_PIC] = p2;
			else
				ent->client->ps.stats[STAT_CTF_TEAM2_PIC] = 0;
		}

		//	ent->client->ps.stats[STAT_CTF_TEAM1_CAPS] = STAT_FRAGS;
		//	ent->client->ps.stats[STAT_CTF_TEAM2_CAPS] = STAT_CTF_MATCH;

		ent->client->ps.stats[STAT_CTF_FLAG_PIC] = 0;
		if (ent->client->resp.ctf_team == CTF_TEAM1 &&
			ent->client->pers.inventory[IT_FLAG2] &&
			(blink))
			ent->client->ps.stats[STAT_CTF_FLAG_PIC] = imageindex_i_ctf2;

		else if (ent->client->resp.ctf_team == CTF_TEAM2 &&
			ent->client->pers.inventory[IT_FLAG1] &&
			(blink))
			ent->client->ps.stats[STAT_CTF_FLAG_PIC] = imageindex_i_ctf1;
	}
	else
	{
		ent->client->ps.stats[STAT_CTF_TEAM1_PIC] = imageindex_i_ctf1;
		ent->client->ps.stats[STAT_CTF_TEAM2_PIC] = imageindex_i_ctf2;

		//	ent->client->ps.stats[STAT_CTF_TEAM1_CAPS] = STAT_FRAGS;
	//		ent->client->ps.stats[STAT_CTF_TEAM2_CAPS] = STAT_CTF_MATCH;
	}

	ent->client->ps.stats[STAT_CTF_JOINED_TEAM1_PIC] = 0;
	ent->client->ps.stats[STAT_CTF_JOINED_TEAM2_PIC] = 0;
	if (ent->client->resp.ctf_team == CTF_TEAM1)
		ent->client->ps.stats[STAT_CTF_JOINED_TEAM1_PIC] = imageindex_i_ctfj;
	else if (ent->client->resp.ctf_team == CTF_TEAM2)
		ent->client->ps.stats[STAT_CTF_JOINED_TEAM2_PIC] = imageindex_i_ctfj;



}

/*------------------------------------------------------------------------*/

/*QUAKED info_player_team1 (1 0 0) (-16 -16 -24) (16 16 32)
potential team1 spawning position for ctf games
*/
void SP_info_player_team1(edict_t* self)
{
}

/*QUAKED info_player_team2 (0 0 1) (-16 -16 -24) (16 16 32)
potential team2 spawning position for ctf games
*/
void SP_info_player_team2(edict_t* self)
{
}

/*------------------------------------------------------------------------*/
/* GRAPPLE																  */
/*------------------------------------------------------------------------*/

// ent is player
void CTFPlayerResetGrapple(edict_t* ent)
{
	if (ent->client && ent->client->ctf_grapple)
		CTFResetGrapple(ent->client->ctf_grapple);
}

// self is grapple, not player
void CTFResetGrapple(edict_t* self)
{
	if (!self->owner->client->ctf_grapple)
		return;

	gi.sound(self->owner, CHAN_WEAPON, gi.soundindex("weapons/grapple/grreset.wav"), self->owner->client->silencer_shots ? 0.2f : 1.0f, ATTN_NORM, 0);

	gclient_t* cl;
	cl = self->owner->client;
	cl->ctf_grapple = nullptr;
	cl->ctf_grapplereleasetime = level.time + 1_sec;
	cl->ctf_grapplestate = CTF_GRAPPLE_STATE_FLY; // we're firing, not on hook
	self->owner->flags &= ~FL_NO_KNOCKBACK;
	G_FreeEdict(self);
}

TOUCH(CTFGrappleTouch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	float volume = 1.0;

	if (other == self->owner)
		return;

	if (self->owner->client->ctf_grapplestate != CTF_GRAPPLE_STATE_FLY)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY))
	{
		CTFResetGrapple(self);
		return;
	}

	self->velocity = {};

	PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (self->dmg)
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, tr.plane.normal, self->dmg, 1, DAMAGE_NONE, MOD_GRAPPLE);
		CTFResetGrapple(self);
		return;
	}

	self->owner->client->ctf_grapplestate = CTF_GRAPPLE_STATE_PULL; // we're on hook
	self->enemy = other;

	self->solid = SOLID_NOT;

	if (self->owner->client->silencer_shots)
		volume = 0.2f;

	gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/grapple/grhit.wav"), volume, ATTN_NORM, 0);
	self->s.sound = gi.soundindex("weapons/grapple/grpull.wav");

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_SPARKS);
	gi.WritePosition(self->s.origin);
	gi.WriteDir(tr.plane.normal);
	gi.multicast(self->s.origin, MULTICAST_PVS, false);
}

// draw beam between grapple and self
void CTFGrappleDrawCable(edict_t* self)
{
	if (self->owner->client->ctf_grapplestate == CTF_GRAPPLE_STATE_HANG)
		return;

	vec3_t start, dir;
	P_ProjectSource(self->owner, self->owner->client->v_angle, { 7, 2, -9 }, start, dir);

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_GRAPPLE_CABLE_2);
	gi.WriteEntity(self->owner);
	gi.WritePosition(start);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PVS, false);
}

void SV_AddGravity(edict_t* ent);

// pull the player toward the grapple
void CTFGrapplePull(edict_t* self)
{
	vec3_t hookdir, v;
	float  vlen;

	if (self->owner->client->pers.weapon && self->owner->client->pers.weapon->id == IT_WEAPON_GRAPPLE &&
		!(self->owner->client->newweapon || ((self->owner->client->latched_buttons | self->owner->client->buttons) & BUTTON_HOLSTER)) &&
		self->owner->client->weaponstate != WEAPON_FIRING &&
		self->owner->client->weaponstate != WEAPON_ACTIVATING)
	{
		if (!self->owner->client->newweapon)
			self->owner->client->newweapon = self->owner->client->pers.weapon;

		CTFResetGrapple(self);
		return;
	}

	if (self->enemy)
	{
		if (self->enemy->solid == SOLID_NOT)
		{
			CTFResetGrapple(self);
			return;
		}
		if (self->enemy->solid == SOLID_BBOX)
		{
			v = self->enemy->size * 0.5f;
			v += self->enemy->s.origin;
			self->s.origin = v + self->enemy->mins;
			gi.linkentity(self);
		}
		else
			self->velocity = self->enemy->velocity;

		if (self->enemy->deadflag)
		{ // he died
			CTFResetGrapple(self);
			return;
		}
	}

	CTFGrappleDrawCable(self);

	if (self->owner->client->ctf_grapplestate > CTF_GRAPPLE_STATE_FLY)
	{
		// pull player toward grapple
		vec3_t forward, up;

		AngleVectors(self->owner->client->v_angle, forward, nullptr, up);
		v = self->owner->s.origin;
		v[2] += self->owner->viewheight;
		hookdir = self->s.origin - v;

		vlen = hookdir.length();

		if (self->owner->client->ctf_grapplestate == CTF_GRAPPLE_STATE_PULL &&
			vlen < 64)
		{
			self->owner->client->ctf_grapplestate = CTF_GRAPPLE_STATE_HANG;
			self->s.sound = gi.soundindex("weapons/grapple/grhang.wav");
		}

		hookdir.normalize();
		hookdir = hookdir * g_grapple_pull_speed->value;
		self->owner->velocity = hookdir;
		self->owner->flags |= FL_NO_KNOCKBACK;
		SV_AddGravity(self->owner);
	}
}

DIE(grapple_die) (edict_t* self, edict_t* other, edict_t* inflictor, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	if (mod.id == MOD_CRUSH)
		CTFResetGrapple(self);
}

bool CTFFireGrapple(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed, effects_t effect)
{
	if (!self || !self->client) {
		return false;
	}

	edict_t* grapple;
	trace_t  tr;
	vec3_t   const normalized = dir.normalized();

	grapple = G_Spawn();
	grapple->s.origin = start;
	grapple->s.old_origin = start;
	grapple->s.angles = vectoangles(normalized);
	grapple->velocity = normalized * speed;
	grapple->movetype = MOVETYPE_FLYMISSILE;
	grapple->clipmask = MASK_PROJECTILE;

	// [Paril-KEX]
	if (!G_ShouldPlayersCollide(true)) {
		grapple->clipmask &= ~CONTENTS_PLAYER;
	}

	grapple->solid = SOLID_BBOX;
	grapple->s.effects |= effect;
	grapple->s.modelindex = gi.modelindex("models/weapons/grapple/hook/tris.md2");
	grapple->owner = self;
	grapple->touch = CTFGrappleTouch;
	grapple->dmg = damage;
	grapple->flags |= FL_NO_KNOCKBACK | FL_NO_DAMAGE_EFFECTS;
	grapple->takedamage = true;
	grapple->die = grapple_die;

	self->client->ctf_grapple = grapple;
	self->client->ctf_grapplestate = CTF_GRAPPLE_STATE_FLY; // we're firing, not on hook

	gi.linkentity(grapple);

	tr = gi.traceline(self->s.origin, grapple->s.origin, grapple, grapple->clipmask);
	if (tr.fraction < 1.0f) {
		grapple->s.origin = tr.endpos + (tr.plane.normal * 1.f);
		grapple->touch(grapple, tr.ent, tr, false);
		return false;
	}

	grapple->s.sound = gi.soundindex("weapons/grapple/grfly.wav");

	return true;
}


void CTFGrappleFire(edict_t* ent, const vec3_t& g_offset, int damage, effects_t effect)
{
	float volume = 1.0;

	if (ent->client->ctf_grapplestate > CTF_GRAPPLE_STATE_FLY)
		return; // it's already out

	vec3_t start, dir;
	P_ProjectSource(ent, ent->client->v_angle, vec3_t{ 24, 8, -8 + 2 } + g_offset, start, dir);

	if (ent->client->silencer_shots)
		volume = 0.2f;

	if (CTFFireGrapple(ent, start, dir, damage, g_grapple_fly_speed->value, effect))
		gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/grapple/grfire.wav"), volume, ATTN_NORM, 0);

	PlayerNoise(ent, start, PNOISE_WEAPON);
}

void CTFWeapon_Grapple_Fire(edict_t* ent)
{
	CTFGrappleFire(ent, vec3_origin, g_grapple_damage->integer, EF_NONE);
}

void CTFWeapon_Grapple(edict_t* ent)
{
	constexpr int pause_frames[] = { 10, 18, 27, 0 };
	constexpr int fire_frames[] = { 6, 0 };
	int			  prevstate;

	// if the the attack button is still down, stay in the firing frame
	if ((ent->client->buttons & (BUTTON_ATTACK | BUTTON_HOLSTER)) &&
		ent->client->weaponstate == WEAPON_FIRING &&
		ent->client->ctf_grapple)
		ent->client->ps.gunframe = 6;

	if (!(ent->client->buttons & (BUTTON_ATTACK | BUTTON_HOLSTER)) &&
		ent->client->ctf_grapple)
	{
		CTFResetGrapple(ent->client->ctf_grapple);
		if (ent->client->weaponstate == WEAPON_FIRING)
			ent->client->weaponstate = WEAPON_READY;
	}

	if ((ent->client->newweapon || ((ent->client->latched_buttons | ent->client->buttons) & BUTTON_HOLSTER)) &&
		ent->client->ctf_grapplestate > CTF_GRAPPLE_STATE_FLY &&
		ent->client->weaponstate == WEAPON_FIRING)
	{
		// he wants to change weapons while grappled
		if (!ent->client->newweapon)
			ent->client->newweapon = ent->client->pers.weapon;
		ent->client->weaponstate = WEAPON_DROPPING;
		ent->client->ps.gunframe = 32;
	}

	prevstate = ent->client->weaponstate;
	Weapon_Generic(ent, 5, 10, 31, 36, pause_frames, fire_frames,
		CTFWeapon_Grapple_Fire);

	// if the the attack button is still down, stay in the firing frame
	if ((ent->client->buttons & (BUTTON_ATTACK | BUTTON_HOLSTER)) &&
		ent->client->weaponstate == WEAPON_FIRING &&
		ent->client->ctf_grapple)
		ent->client->ps.gunframe = 6;

	// if we just switched back to grapple, immediately go to fire frame
	if (prevstate == WEAPON_ACTIVATING &&
		ent->client->weaponstate == WEAPON_READY &&
		ent->client->ctf_grapplestate > CTF_GRAPPLE_STATE_FLY)
	{
		if (!(ent->client->buttons & (BUTTON_ATTACK | BUTTON_HOLSTER)))
			ent->client->ps.gunframe = 6;
		else
			ent->client->ps.gunframe = 5;
		ent->client->weaponstate = WEAPON_FIRING;
	}
}

void CTFDirtyTeamMenu()
{
	for (auto player : active_players())
		if (player->client->menu)
		{
			player->client->menudirty = true;
			player->client->menutime = level.time;
		}
}

void CTFTeam_f(edict_t* ent)
{
	if (!G_TeamplayEnabled())
		return;

	const char* t;
	ctfteam_t	  desired_team;

	t = gi.args();
	if (!*t)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "$g_you_are_on_team",
			CTFTeamName(ent->client->resp.ctf_team));
		return;
	}

	if (ctfgame.match > MATCH_SETUP)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "$g_cant_change_teams");
		return;
	}

	// [Paril-KEX] with force-join, don't allow us to switch
	// using this command.
	if (g_teamplay_force_join->integer)
	{
		if (!(ent->svflags & SVF_BOT))
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "$g_cant_change_teams");
			return;
		}
	}

	if (Q_strcasecmp(t, "red") == 0)
		desired_team = CTF_TEAM1;
	else if (Q_strcasecmp(t, "blue") == 0)
		desired_team = CTF_TEAM2;
	else
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "$g_unknown_team", t);
		return;
	}

	if (ent->client->resp.ctf_team == desired_team)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "$g_already_on_team",
			CTFTeamName(ent->client->resp.ctf_team));
		return;
	}

	////
	ent->svflags = SVF_NONE;
	ent->flags &= ~FL_GODMODE;
	ent->client->resp.ctf_team = desired_team;
	ent->client->resp.ctf_state = 0;
	char value[MAX_INFO_VALUE] = { 0 };
	gi.Info_ValueForKey(ent->client->pers.userinfo, "skin", value, sizeof(value));
	CTFAssignSkin(ent, value);

	// if anybody has a menu open, update it immediately
	CTFDirtyTeamMenu();

	if (ent->solid == SOLID_NOT)
	{
		// spectator
		PutClientInServer(ent);

		G_PostRespawn(ent);

		gi.LocBroadcast_Print(PRINT_HIGH, "$g_joined_team",
			ent->client->pers.netname, CTFTeamName(desired_team));
		return;
	}

	ent->health = 0;
	player_die(ent, ent, ent, 100000, vec3_origin, { MOD_SUICIDE, true });

	// don't even bother waiting for death frames
	ent->deadflag = true;
	respawn(ent);

	ent->client->resp.score = 0;

	gi.LocBroadcast_Print(PRINT_HIGH, "$g_changed_team",
		ent->client->pers.netname, CTFTeamName(desired_team));
}
#include <string>
#include <vector>
#include <algorithm>
#include "../horde/g_horde_benefits.h"

constexpr size_t MAX_CTF_STAT_LENGTH = 1024;

std::string GetActiveBonusesString() {
	const std::vector<std::pair<const char*, const char*>> bonus_mappings = {
		{"vampire upgraded", "Health & Armor Vampirism"},
		{"vampire", "Health Vampirism"},
		{"ammo regen", "Ammo Regen"},
		{"start armor", "Starting Armor"},
		{"auto haste", "Auto-Haste"},
		{"Cluster Prox Grenades", "Upgraded Prox Launcher"},
		{"Traced-Piercing Bullets", "Traced-Energy Bullets"},
		{"Napalm-Grenade Launcher", "Napalm-Grenade Launcher"},
		{"BFG Grav-Pull Lasers", "BFG Grav-Pull Lasers"},
		//{"Improved Chaingun", "Improved Chaingun"},
		{"Piercing Plasma", "Piercing Plasma-Beam"}
	};

	std::vector<std::string> active_bonuses;

	// Check if "vampire upgraded" is obtained first
	bool has_vampire_upgraded = false;
	for (size_t i = 0; i < MAX_BENEFITS; i++) {
		if (std::strcmp(BENEFITS[i].name, "vampire upgraded") == 0) {
			has_vampire_upgraded = has_benefit(i);
			break;
		}
	}

	for (size_t i = 0; i < MAX_BENEFITS; i++) {
		// Skip "vampire" if "vampire upgraded" is already obtained
		if (std::strcmp(BENEFITS[i].name, "vampire") == 0 && has_vampire_upgraded) {
			continue;
		}

		if (has_benefit(i)) {
			// Encontrar el texto del bonus correspondiente
			for (const auto& [benefit_name, bonus_text] : bonus_mappings) {
				if (std::strcmp(BENEFITS[i].name, benefit_name) == 0) {
					active_bonuses.push_back(bonus_text);
					break;
				}
			}
		}
	}

	if (active_bonuses.empty()) {
		return ""; // Return an empty string if there are no active bonuses
	}

	std::string result = "* ";
	for (size_t i = 0; i < active_bonuses.size(); ++i) {
		result += active_bonuses[i];
		if (i < active_bonuses.size() - 1) {
			result += "\n* ";
		}
	}
	return result;
}

struct PlayerScore {
	unsigned int index;
	int score;
	int ping;
	bool is_dead;  // Cambiado de has_flag a is_dead
};

void CTFScoreboardMessage(edict_t* ent, edict_t* killer) {
	std::vector<PlayerScore> team_players;
	std::vector<PlayerScore> spectators;
	int total_score = 0;

	// Sort players
	for (unsigned int i = 0; i < game.maxclients; i++) {
		const edict_t* const cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse)
			continue;
		const gclient_t* const cl = &game.clients[i];
		PlayerScore const player = {
			i,
			cl->resp.score,
			std::min(cl->ping, 999),
			(cl_ent->deadflag != false)  // Usando deadflag en lugar de inventory
		};

		if (cl->resp.ctf_team == CTF_TEAM1) {
			team_players.push_back(player);
			total_score += player.score;
		}
		else if (cl->resp.ctf_team == CTF_NOTEAM) {
			spectators.push_back(player);
		}
	}

	std::sort(team_players.begin(), team_players.end(),
		[](const PlayerScore& a, const PlayerScore& b) { return a.score > b.score; });

	std::string layout;

	// Header
	if (g_horde->integer) {
		layout += fmt::format("if 0 xv -20 yv -10 loc_string2 1 \"Wave Number: {}          Stroggs Remaining: {}\" endif \n",
			last_wave_number, level.total_monsters - level.killed_monsters);
	}

	if (timelimit->value) {
		layout += fmt::format("if 0 xv 340 yv -33 time_limit {} endif \n",
			gi.ServerFrame() + ((gtime_t::from_min(timelimit->value) - level.time)).milliseconds() / gi.frame_time_ms);
	}

	// Team score
	if (!level.intermissiontime) {
		layout += fmt::format("if 25 xv -90 yv 10 dogtag endif \n");
		std::string activeBonuses = GetActiveBonusesString();
		if (!activeBonuses.empty()) {
			layout += fmt::format("if 0 xv 208 yv 8 string \"{}\" endif \n", activeBonuses);
		}
	}
	else {
		layout += fmt::format("if 25 xv -90 yv 10 dogtag endif "
			"if 25 xv 205 yv 8 pic 25 endif "
			"if 0 xv 70 yv -20 num 2 19 endif \n",
			total_score, team_players.size());
	}

	// Player list con indicador de muerte
	for (size_t i = 0; i < team_players.size() && i < 16; ++i) {
		const auto& player = team_players[i];
		int y = 42 + i * 8;

		// Primero añadimos el indicador de muerte si corresponde
		if (player.is_dead) {
			layout += fmt::format("if 0 xv -135 yv {} string \"[Dead]\" endif ", y);
		}

		// Luego añadimos la información normal del jugador
		layout += fmt::format("if 0 ctf -90 {} {} {} {} \"\" endif \n",
			y, player.index, player.score, player.ping);
	}

	// Spectators
	if (layout.size() < MAX_CTF_STAT_LENGTH - 50 && !spectators.empty()) {
		int y = 42 + (std::min<size_t>(team_players.size(), 16) + 2) * 8;
		layout += fmt::format("if 0 xv -90 yv {} loc_string2 0 \"Spectators & AFK\" endif \n", y);
		y += 8;
		for (const auto& spec : spectators) {
			layout += fmt::format("if 0 ctf -90 {} {} {} {} \"\" endif \n",
				y, spec.index, spec.score, spec.ping);
			y += 8;
		}
	}

	// Footer
	if (!level.intermissiontime) {
		layout += fmt::format("if 0 xv 0 yb -55 cstring2 \"{}\" endif \n",
			ent->client->resp.ctf_team != CTF_TEAM1
			? "Use Inventory <KEY> to toggle Horde Menu."
			: "Use Horde Menu on Powerup Wheel or press Inventory <KEY> to toggle Horde Menu.");
	}
	else {
		layout += fmt::format("ifgef {} yb -48 xv 0 loc_cstring2 0 \"{}\" endif \n",
			level.intermission_server_frame + (5_sec).frames(),
			brandom() ? "MAKE THEM PAY !!!" : "THEY WILL REGRET THIS !!!");
	}

	gi.WriteByte(svc_layout);
	gi.WriteString(layout.c_str());
}
/*------------------------------------------------------------------------*/
/* TECH																	  */
/*------------------------------------------------------------------------*/

void CTFHasTech(edict_t* who)
{
	// Check if the message has been shown less than twice and the current wave number is less or equal to 5
	if (who->client->ctf_lasttechmsg_count < 2)
	{
		gi.LocCenter_Print(who, "\n\nTechs Are Now Being Saved After Death.\nYou Can Use Your *Drop Tech* Key \nOr\n Swap them on Horde Menu! Open Horde Menu (TURTLE) on POWERUP WHEEL\n");

		// Increment the message count
		who->client->ctf_lasttechmsg_count++;
	}
}

gitem_t* CTFWhat_Tech(edict_t* ent)
{
	int i;

	i = 0;
	for (; i < q_countof(tech_ids); i++)
	{
		if (ent->client->pers.inventory[tech_ids[i]])
		{
			return GetItemByIndex(tech_ids[i]);
		}
	}
	return nullptr;
}

bool CTFPickup_Tech(edict_t* ent, edict_t* other)
{
	int i;

	i = 0;
	for (; i < q_countof(tech_ids); i++)
	{
		if (other->client->pers.inventory[tech_ids[i]])
		{
			CTFHasTech(other);
			return false; // has this one
		}
	}

	// client only gets one tech
	other->client->pers.inventory[ent->item->id]++;
	other->client->ctf_regentime = level.time;
	return true;
}

static void SpawnTech(gitem_t* item, edict_t* spot);

static edict_t* FindTechSpawn() {
	edict_t* spot = nullptr;
	constexpr vec3_t mins{};  // Constructor por defecto inicializa a {0,0,0}
	constexpr vec3_t maxs{};

	// Intentar encontrar un punto de generación válido varias veces
	for (int attempts = 0; attempts < 10; attempts++) {
		spot = SelectDeathmatchSpawnPoint(false, true, true).spot;
		if (!spot) continue;

		// Usar el origen del spot para start y ajustar end hacia abajo
		vec3_t const start = spot->s.origin;
		vec3_t const end = spot->s.origin + vec3_t{ 0, 0, -128 };

		// Realizar el trace para verificar que el punto está sobre suelo sólido
		trace_t const tr = gi.trace(start, mins, maxs, end, spot, MASK_SOLID);

		if (tr.fraction < 1.0 && !tr.startsolid && !tr.allsolid) {
			return spot;
		}
	}

	return nullptr;
}
THINK(TechThink) (edict_t* tech) -> void
{
	if (tech == nullptr)
	{
		gi.Com_PrintFmt("PRINT: TechThink: Invalid tech entity\n");
		return;
	}

	edict_t* spot = FindTechSpawn();
	if (spot != nullptr)
	{
		if (tech->item != nullptr)
		{
			SpawnTech(tech->item, spot);
			G_FreeEdict(tech);
		}
		else
		{
			gi.Com_PrintFmt("PRINT: TechThink: Tech entity has no item\n");
		}
	}
	else
	{
		tech->nextthink = level.time + CTF_TECH_TIMEOUT;
		tech->think = TechThink;
	}
}

static THINK(Tech_Make_Touchable) (edict_t* tech) -> void {
	tech->touch = Touch_Item;
	tech->nextthink = level.time + CTF_TECH_TIMEOUT;
	tech->think = TechThink;
}

void CTFDrop_Tech(edict_t* ent, gitem_t* item)
{
	// Eliminar el tech item del inventario del jugador
	ent->client->pers.inventory[item->id] = 0;

	// Reiniciar el estado de todos los tech items del mismo tipo
	for (unsigned int i = 0; i < game.maxentities; i++)
	{
		edict_t* tech = &g_edicts[i];
		if (tech->inuse && tech->item == item)
		{
			tech->svflags &= ~SVF_NOCLIENT;
			tech->solid = SOLID_TRIGGER;
			tech->movetype = MOVETYPE_TOSS;
			tech->touch = Touch_Item;
			tech->nextthink = level.time + CTF_TECH_TIMEOUT;
			tech->think = TechThink;
			// Reiniciar el registro de quién ha recogido el item
			tech->item_picked_up_by.reset();
			gi.linkentity(tech);
		}
	}
}


void CTFDeadDropTech(edict_t* ent)
{
	int i;

	for (i = 0; i < q_countof(tech_ids); i++)
	{
		if (ent->client->pers.inventory[tech_ids[i]])
		{
			ent->client->pers.inventory[tech_ids[i]] = 1;
		}
	}
}

static void SpawnTech(gitem_t* item, edict_t* spot)
{
	if (item == nullptr || spot == nullptr)
	{
		gi.Com_PrintFmt("PRINT: SpawnTech: Invalid item or spot\n");
		return;
	}

	edict_t* ent = G_Spawn();
	if (ent == nullptr)
	{
		gi.Com_PrintFmt("PRINT: SpawnTech: Failed to spawn entity\n");
		return;
	}

	ent->classname = item->classname;
	ent->item = item;
	ent->spawnflags = SPAWNFLAG_ITEM_DROPPED;
	ent->s.effects = item->world_model_flags;
	ent->s.renderfx = RF_GLOW | RF_NO_LOD;
	ent->mins = { -15, -15, -15 };
	ent->maxs = { 15, 15, 15 };

	if (ent->item->world_model)
	{
		gi.setmodel(ent, ent->item->world_model);
	}
	else
	{
		gi.Com_PrintFmt("PRINT: SpawnTech: Item has no world model\n");
	}

	ent->solid = SOLID_TRIGGER;
	ent->movetype = MOVETYPE_TOSS;
	ent->touch = Touch_Item;
	ent->owner = ent;

	vec3_t const angles = { 0, (float)irandom(360), 0 };
	vec3_t forward, right;
	AngleVectors(angles, forward, right, nullptr);

	ent->s.origin = spot->s.origin;
	ent->s.origin[2] += 16;
	ent->velocity = forward * 100;
	ent->velocity[2] = 300;

	ent->nextthink = level.time + CTF_TECH_TIMEOUT;
	ent->think = TechThink;

	gi.linkentity(ent);
}

THINK(SpawnTechs) (edict_t* ent) -> void
{
	edict_t* spot;
	int i;

	i = 0;
	for (; i < q_countof(tech_ids); i++)
	{
		if ((spot = FindTechSpawn()) != nullptr)
			SpawnTech(GetItemByIndex(tech_ids[i]), spot);
	}
	if (ent)
		G_FreeEdict(ent);
}

// frees the passed edict!
void CTFRespawnTech(edict_t* ent)
{
	edict_t* spot;

	if ((spot = FindTechSpawn()) != nullptr)
		SpawnTech(ent->item, spot);
	G_FreeEdict(ent);
}

void CTFSetupTechSpawn()
{
	edict_t* ent;
	bool techs_allowed;

	// [Paril-KEX]
	if (!strcmp(g_allow_techs->string, "auto"))
		techs_allowed = !!ctf->integer || !!g_horde->integer;
	else
		techs_allowed = !!g_allow_techs->integer || !!g_horde->integer;

	if (!techs_allowed)
		return;

	ent = G_Spawn();
	ent->nextthink = level.time + 2_sec;
	ent->think = SpawnTechs;
}

void CTFResetTech()
{
	edict_t* ent;
	uint32_t i;

	for (ent = g_edicts + 1, i = 1; i < globals.num_edicts; i++, ent++)
	{
		if (ent->inuse)
			if (ent->item && (ent->item->flags & IF_TECH))
				G_FreeEdict(ent);
	}
	SpawnTechs(nullptr);
}


int CTFApplyResistance(edict_t* ent, int dmg)
{
	float volume = 1.0;

	if (ent->client && ent->client->silencer_shots)
		volume = 0.2f;

	if (dmg && ent->client && ent->client->pers.inventory[IT_TECH_RESISTANCE])
	{
		// make noise
		gi.sound(ent, CHAN_AUX, gi.soundindex("ctf/tech1.wav"), volume, ATTN_NORM, 0);
		return dmg / 2;
	}
	return dmg;
}

int CTFApplyStrength(edict_t* ent, int dmg) {
	if (ent == nullptr) {
		// Este error sí debería ser raro y vale la pena registrarlo
		gi.Com_PrintFmt("PRINT: CTFApplyStrength: Error - ent is null\n");
		return dmg;
	}

	if (ent->client && dmg && ent->client->pers.inventory[IT_TECH_STRENGTH]) {
		return (ent->client->resp.spree >= 10 || current_wave_level >= 15) ? dmg * 2.0f : dmg * 1.5f;
	}

	return dmg;
}
bool CTFApplyStrengthSound(edict_t* ent)
{
	float volume = 1.0;

	if (ent->client && ent->client->silencer_shots)
		volume = 0.2f;

	if (ent->client &&
		ent->client->pers.inventory[IT_TECH_STRENGTH])
	{
		if (ent->client->ctf_techsndtime < level.time && (!(ent->svflags & SVF_BOT)))
		{
			ent->client->ctf_techsndtime = level.time + 1_sec;
			if (ent->client->quad_time > level.time)
				gi.sound(ent, CHAN_AUX, gi.soundindex("ctf/tech2x.wav"), volume, ATTN_NORM, 0);
			else
				gi.sound(ent, CHAN_AUX, gi.soundindex("ctf/tech2.wav"), volume, ATTN_NORM, 0);
		}
		return true;
	}
	return false;
}

bool CTFApplyHaste(edict_t* ent)
{
	if (ent->client &&
		ent->client->pers.inventory[IT_TECH_HASTE])
		return true;
	return false;
}

void CTFApplyHasteSound(edict_t* ent)
{
	float volume = 1.0;

	if (ent->client && ent->client->silencer_shots)
		volume = 0.2f;

	if (ent->client &&
		ent->client->pers.inventory[IT_TECH_HASTE] &&
		ent->client->ctf_techsndtime < level.time)
	{
		ent->client->ctf_techsndtime = level.time + 1_sec;
		gi.sound(ent, CHAN_AUX, gi.soundindex("ctf/tech3.wav"), volume, ATTN_NORM, 0);
	}
}

void CTFApplyRegeneration(edict_t* ent)
{
	bool	   noise = false;
	gclient_t* client;
	int		   index;
	float	   volume = 1.0;

	client = ent->client;
	if (!client)
		return;

	if (client && ent->deadflag)
		return;

	if (ent->client->silencer_shots)
		volume = 0.2f;

	if (client->pers.inventory[IT_TECH_REGENERATION])
	{
		if (client->ctf_regentime < level.time)
		{
			client->ctf_regentime = level.time;
			if (ent->health < 100)
			{
				ent->health += 5;
				if (ent->health > 100)
					ent->health = 100;
				client->ctf_regentime += 500_ms;
				noise = true;
			}
			index = ArmorIndex(ent);
			if (index && client->pers.inventory[index] < 100)
			{
				client->pers.inventory[index] += 5;
				if (client->pers.inventory[index] > 100)
					client->pers.inventory[index] = 100;
				client->ctf_regentime += 500_ms;
				noise = true;
			}
		}
		if (noise && ent->client->ctf_techsndtime < level.time)
		{
			ent->client->ctf_techsndtime = level.time + 1_sec;
			gi.sound(ent, CHAN_AUX, gi.soundindex("ctf/tech4.wav"), volume, ATTN_NORM, 0);
		}
	}
}

bool CTFHasRegeneration(edict_t* ent)
{
	if (ent->client &&
		ent->client->pers.inventory[IT_TECH_REGENERATION])
		return true;
	return false;
}

void CTFSay_Team(edict_t* who, const char* msg_in)
{
	edict_t* cl_ent;
	char outmsg[256];

	if (CheckFlood(who))
		return;

	Q_strlcpy(outmsg, msg_in, sizeof(outmsg));

	char* msg = outmsg;

	if (*msg == '\"')
	{
		msg[strlen(msg) - 1] = 0;
		msg++;
	}

	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse)
			continue;
		if (cl_ent->client->resp.ctf_team == who->client->resp.ctf_team)
			gi.LocClient_Print(cl_ent, PRINT_CHAT, "({}): {}\n",
				who->client->pers.netname, msg);
	}
}

/*-----------------------------------------------------------------------*/
/*QUAKED misc_ctf_banner (1 .5 0) (-4 -64 0) (4 64 248) TEAM2
The origin is the bottom of the banner.
The banner is 248 tall.
*/
THINK(misc_ctf_banner_think) (edict_t* ent) -> void
{
	ent->s.frame = (ent->s.frame + 1) % 16;
	ent->nextthink = level.time + 10_hz;
}

constexpr spawnflags_t SPAWNFLAG_CTF_BANNER_BLUE = 1_spawnflag;

void SP_misc_ctf_banner(edict_t* ent)
{
	ent->movetype = MOVETYPE_NONE;
	ent->solid = SOLID_NOT;
	ent->s.modelindex = gi.modelindex("models/ctf/banner/tris.md2");
	if (ent->spawnflags.has(SPAWNFLAG_CTF_BANNER_BLUE)) // team2
		ent->s.skinnum = 1;

	ent->s.frame = irandom(16);
	gi.linkentity(ent);

	ent->think = misc_ctf_banner_think;
	ent->nextthink = level.time + 10_hz;
}

/*QUAKED misc_ctf_small_banner (1 .5 0) (-4 -32 0) (4 32 124) TEAM2
The origin is the bottom of the banner.
The banner is 124 tall.
*/
void SP_misc_ctf_small_banner(edict_t* ent)
{
	ent->movetype = MOVETYPE_NONE;
	ent->solid = SOLID_NOT;
	ent->s.modelindex = gi.modelindex("models/ctf/banner/small.md2");
	if (ent->spawnflags.has(SPAWNFLAG_CTF_BANNER_BLUE)) // team2
		ent->s.skinnum = 1;

	ent->s.frame = irandom(16);
	gi.linkentity(ent);

	ent->think = misc_ctf_banner_think;
	ent->nextthink = level.time + 10_hz;
}
void CTFWinElection();
/*-----------------------------------------------------------------------*/

static void SetGameName(pmenu_t* p)
{
	if (ctf->integer)
		Q_strlcpy(p->text, "$g_pc_3wctf", sizeof(p->text));
	else
		Q_strlcpy(p->text, "Horde MOD BETA v0.0089\n\n\n\n\n\n\n\n\nDiscord:\nEnemy0416", sizeof(p->text));
}

static void SetLevelName(pmenu_t* p)
{
	static char levelname[33];

	levelname[0] = '*';
	if (g_edicts[0].message)
		Q_strlcpy(levelname + 1, g_edicts[0].message, sizeof(levelname) - 1);
	else
		Q_strlcpy(levelname + 1, level.mapname, sizeof(levelname) - 1);
	levelname[sizeof(levelname) - 1] = 0;
	Q_strlcpy(p->text, levelname, sizeof(p->text));
}

/*-----------------------------------------------------------------------*/
//#include <fmt/core.h>
// Constantes del sistema de votación
struct VoteConstants {
	static constexpr size_t MAX_VOTE_INFO_LENGTH = 75;
	static constexpr size_t MAX_VOTE_MAP_LENGTH = 128;
	static constexpr size_t MAX_VOTE_MSG_LENGTH = 256;
	static constexpr gtime_t VOTE_DURATION = 25_sec;
};

// Helper function para verificar longitud de strings de votación
[[nodiscard]] inline static bool IsValidVoteString(const char* str, size_t max_length) {
	return str && strlen(str) < max_length - 1;
}


bool CTFBeginElection(edict_t* ent, elect_t type, const char* msg);
bool CTFCheckTimeExtensionVote()
{
	if (!timelimit->value || ctfgame.election != ELECT_NONE)
		return false;

	const gtime_t time_remaining = gtime_t::from_min(timelimit->value) - level.time;
	const int mins_remaining = time_remaining.seconds<int>() / 60;

	// Start vote at 5 minutes remaining
	if (mins_remaining == 5)
	{
		// Find first active human player to initiate vote
		for (auto player : active_players())
		{
			if (player->client && !(player->svflags & SVF_BOT))
			{
				return CTFBeginElection(player, ELECT_TIME, "Extend map time by 30 minutes?");
			}
		}
	}

	return false;
}

bool CTFBeginElection(edict_t* ent, elect_t type, const char* msg) {
	// Initial validations
	if (!ent || !ent->client || !msg) {
		return false;
	}

	// Special case for time extension vote - bypass some restrictions
	const bool is_time_vote = (type == ELECT_TIME);

	if (!is_time_vote) {
		//if (electpercentage->value == 0) {
		//	gi.LocClient_Print(ent, PRINT_HIGH, "Elections are disabled, only an admin can process this action.\n");
		//	return false;
		//}
		if (ent->client->resp.ctf_team == CTF_NOTEAM) {
			gi.LocClient_Print(ent, PRINT_HIGH, "You have to be in the game to start a vote.\n");
			return false;
		}
	}

	if (ctfgame.election != ELECT_NONE) {
		gi.LocClient_Print(ent, PRINT_HIGH, "Election already in progress.\n");
		return false;
	}

	// Count players and clear votes
	int count = 0;
	ctfgame.evotes = 0;
	for (auto player : active_players()) {
		if (player->client) {
			player->client->resp.voted = false;
			if (!(player->svflags & SVF_BOT) &&
				(player->client->resp.ctf_team == CTF_TEAM1)) {
				count++;
			}
		}
	}

	// Election setup
	ctfgame.etarget = ent;
	ctfgame.election = type;
	ctfgame.needvotes = static_cast<int>((count * electpercentage->value) / 100);
	ctfgame.electtime = level.time + (is_time_vote ? 30_sec : VoteConstants::VOTE_DURATION);

	// Message preparation
	if (IsValidVoteString(msg, sizeof(ctfgame.emsg))) {
		Q_strlcpy(ctfgame.emsg, msg, sizeof(ctfgame.emsg));
	}
	else {
		Q_strlcpy(ctfgame.emsg, is_time_vote ? "Extend map time" : "Vote in progress",
			sizeof(ctfgame.emsg));
	}

	// HUD update and messages
	UpdateVoteHUD();

	// For time extension in single player, directly extend time
	if (is_time_vote && count == 1) {
		gi.cvar_set("timelimit", G_Fmt("{}", timelimit->value + 30).data());
		gi.LocBroadcast_Print(PRINT_HIGH, "Time extended by 30 minutes.\n");
		ctfgame.election = ELECT_NONE; // Reset election state
		return false;
	}

	// Broadcast messages
	if (is_time_vote) {
		gi.LocBroadcast_Print(PRINT_HIGH, "Vote to extend map time by 30 minutes\n");
	}
	else {
		gi.LocBroadcast_Print(PRINT_HIGH,
			"Use Horde Menu (TURTLE) on POWERUP WHEEL to vote, or type YES/NO in console.\n");
	}

	gi.LocBroadcast_Print(PRINT_HIGH,
		fmt::format("Votes: {}  Needed: {}\n", ctfgame.evotes, ctfgame.needvotes).c_str());

	// Auto-aprobación para un solo jugador
	if (count == 1) {
		ctfgame.evotes = ctfgame.needvotes;
		gi.LocBroadcast_Print(PRINT_CHAT, "Election approved automatically as there are no other (human) players logged.\n");
		CTFWinElection();
	}

	return true;
}
void UpdateVoteHUD() {
	static constexpr size_t MAX_VOTE_STRING = 128;

	if (ctfgame.election != ELECT_NONE) {
		// Format with bounds checking
		std::string vote_info = fmt::format("{} Time left: {}s\n",
			ctfgame.emsg,
			static_cast<int>((ctfgame.electtime - level.time).seconds()));

		if (vote_info.length() >= MAX_VOTE_STRING) {
			vote_info.resize(MAX_VOTE_STRING - 1);
			vote_info += "...";
		}

		gi.configstring(CONFIG_VOTE_INFO, vote_info.c_str());
		ClearHordeMessage();

		for (auto player : active_players()) {
			if (player->client) {
				player->client->ps.stats[STAT_VOTESTRING] = CONFIG_VOTE_INFO;
			}
		}
	}
	else {
		gi.configstring(CONFIG_VOTE_INFO, "");
		for (auto player : active_players()) {
			if (player->client) {
				player->client->ps.stats[STAT_VOTESTRING] = 0;
			}
		}
	}
}

void DoRespawn(edict_t* ent);

void CTFResetAllPlayers()
{
	uint32_t i;
	edict_t* ent;

	for (i = 1; i <= game.maxclients; i++)
	{
		ent = g_edicts + i;
		if (!ent->inuse)
			continue;

		if (ent->client->menu)
			PMenu_Close(ent);

		CTFPlayerResetGrapple(ent);
		CTFDeadDropFlag(ent);
		CTFDeadDropTech(ent);

		ent->client->resp.ctf_team = CTF_NOTEAM;
		ent->client->resp.ready = false;

		ent->svflags = SVF_NONE;
		ent->flags &= ~FL_GODMODE;
		PutClientInServer(ent);
	}

	// reset the level
	CTFResetTech();
	CTFResetFlags();

	for (ent = g_edicts + 1, i = 1; i < globals.num_edicts; i++, ent++)
	{
		if (ent->inuse && !ent->client)
		{
			if (ent->solid == SOLID_NOT && ent->think == DoRespawn &&
				ent->nextthink >= level.time)
			{
				ent->nextthink = 0_ms;
				DoRespawn(ent);
			}
		}
	}
	if (ctfgame.match == MATCH_SETUP)
		ctfgame.matchtime = level.time + gtime_t::from_min(matchsetuptime->value);
}

void CTFAssignGhost(edict_t* ent)
{
	//int ghost, i;

	//for (ghost = 0; ghost < MAX_CLIENTS; ghost++)
	//	if (!ctfgame.ghosts[ghost].code)
	//		break;
	//if (ghost == MAX_CLIENTS)
	//	return;
	//ctfgame.ghosts[ghost].team = ent->client->resp.ctf_team;
	//ctfgame.ghosts[ghost].score = 0;
	//for (;;)
	//{
	//	ctfgame.ghosts[ghost].code = irandom(10000, 100000);
	//	for (i = 0; i < MAX_CLIENTS; i++)
	//		if (i != ghost && ctfgame.ghosts[i].code == ctfgame.ghosts[ghost].code)
	//			break;
	//	if (i == MAX_CLIENTS)
	//		break;
	//}
	//ctfgame.ghosts[ghost].ent = ent;
	//Q_strlcpy(ctfgame.ghosts[ghost].netname, ent->client->pers.netname, sizeof(ctfgame.ghosts[ghost].netname));
	//ent->client->resp.ghost = ctfgame.ghosts + ghost;
	//gi.LocClient_Print(ent, PRINT_CHAT, "Your ghost code is **** {} ****\n", ctfgame.ghosts[ghost].code);
	//gi.LocClient_Print(ent, PRINT_HIGH, "If you lose connection, you can rejoin with your score intact by typing \"ghost {}\".\n",
	//	ctfgame.ghosts[ghost].code);
}

// start a match
void CTFStartMatch()
{
	edict_t* ent;

	ctfgame.match = MATCH_GAME;
	ctfgame.matchtime = level.time + gtime_t::from_min(matchtime->value);
	ctfgame.countdown = false;

	ctfgame.team1 = ctfgame.team2 = 0;

	memset(ctfgame.ghosts, 0, sizeof(ctfgame.ghosts));

	for (uint32_t i = 1; i <= game.maxclients; i++)
	{
		ent = g_edicts + i;
		if (!ent->inuse)
			continue;
		if (ent->svflags & SVF_BOT)
			continue;

		ent->client->resp.score = 0;
		ent->client->resp.ctf_state = 0;
		ent->client->resp.ghost = nullptr;

		gi.LocCenter_Print(ent, "******************\n\nMATCH HAS STARTED!\n\n******************");

		if (ent->client->resp.ctf_team != CTF_NOTEAM)
		{
			// make up a ghost code
			CTFAssignGhost(ent);
			CTFPlayerResetGrapple(ent);
			ent->svflags = SVF_NOCLIENT;
			ent->flags &= ~FL_GODMODE;

			ent->client->respawn_time = level.time + random_time(1_sec, 4_sec);
			ent->client->ps.pmove.pm_type = PM_DEAD;
			ent->client->anim_priority = ANIM_DEATH;
			ent->s.frame = FRAME_death308 - 1;
			ent->client->anim_end = FRAME_death308;
			ent->deadflag = true;
			ent->movetype = MOVETYPE_NOCLIP;
			ent->client->ps.gunindex = 0;
			ent->client->ps.gunskin = 0;
			gi.linkentity(ent);
		}
	}
}

void CTFEndMatch()
{
	//ctfgame.match = MATCH_POST;
	//gi.LocBroadcast_Print(PRINT_CHAT, "MATCH COMPLETED!\n");

	//CTFCalcScores();

	//gi.LocBroadcast_Print(PRINT_HIGH, "RED TEAM:  {} captures, {} points\n",
	//	ctfgame.team1, ctfgame.total1);
	//gi.LocBroadcast_Print(PRINT_HIGH, "BLUE TEAM:  {} captures, {} points\n",
	//	ctfgame.team2, ctfgame.total2);

	//if (ctfgame.team1 > ctfgame.team2)
	//	gi.LocBroadcast_Print(PRINT_CHAT, "$g_ctf_red_wins_caps",
	//		ctfgame.team1 - ctfgame.team2);
	//else if (ctfgame.team2 > ctfgame.team1)
	//	gi.LocBroadcast_Print(PRINT_CHAT, "$g_ctf_blue_wins_caps",
	//		ctfgame.team2 - ctfgame.team1);
	//else if (ctfgame.total1 > ctfgame.total2) // frag tie breaker
	//	gi.LocBroadcast_Print(PRINT_CHAT, "$g_ctf_red_wins_points",
	//		ctfgame.total1 - ctfgame.total2);
	//else if (ctfgame.total2 > ctfgame.total1)
	//	gi.LocBroadcast_Print(PRINT_CHAT, "$g_ctf_blue_wins_points",
	//		ctfgame.total2 - ctfgame.total1);
	//else
	//	gi.LocBroadcast_Print(PRINT_CHAT, "$g_ctf_tie_game");

	//EndDMLevel();
}

bool CTFNextMap()
{
	if (ctfgame.match == MATCH_POST)
	{
		ctfgame.match = MATCH_SETUP;
		CTFResetAllPlayers();
		return true;
	}
	return false;
}

void CTFWinElection() {
	edict_t* CreateTargetChangeLevel(const char* map);
	switch (ctfgame.election) {
	case ELECT_MAP:
		gi.LocBroadcast_Print(PRINT_HIGH, "vote succeeded! Changing level to {}.\n",
			ctfgame.elevel);
		if (g_horde->integer) {
			HandleResetEvent();
			for (unsigned int i = 0; i < game.maxclients; i++) {
				edict_t* ent = g_edicts + 1 + i;
				if (ent->inuse && ent->client) {
					InitClientPt(ent, ent->client);
				}
			}
			BeginIntermission(CreateTargetChangeLevel(ctfgame.elevel));
			// Limpiar el nivel después de usarlo
			ctfgame.elevel[0] = '\0';
		}
		break;
	case ELECT_TIME:
		// Extend the timelimit by 30 minutes
		gi.cvar_set("timelimit", G_Fmt("{}", timelimit->value + 30).data());
		gi.LocBroadcast_Print(PRINT_HIGH, "Vote succeeded! Time extended by 30 minutes.\n");
		break;
	default:
		break;
	}
	// Resetear el estado de la elección
	ctfgame.election = ELECT_NONE;
	ctfgame.electtime = 0_sec;
	// Llamar a UpdateVoteHUD para limpiar el configstring y el voted_map de los jugadores
	UpdateVoteHUD();
}

void CTFVoteYes(edict_t* ent)
{
	if (ctfgame.election == ELECT_NONE)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "No election is in progress.\n");
		return;
	}
	if (ent->client->resp.voted)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "You already voted.\n");
		return;
	}
	if (ctfgame.etarget == ent)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "You can't vote for yourself.\n");
		return;
	}

	ent->client->resp.voted = true;

	ctfgame.evotes++;
	if (ctfgame.evotes == ctfgame.needvotes)
	{
		// the election has been won
		CTFWinElection();
		return;
	}
	gi.LocBroadcast_Print(PRINT_HIGH, "{}\n", ctfgame.emsg);
	gi.LocBroadcast_Print(PRINT_CHAT, "Votes: {}  Needed: {}  Time left: {}s\n", ctfgame.evotes, ctfgame.needvotes,
		(ctfgame.electtime - level.time).seconds<int>());
}

void CTFVoteNo(edict_t* ent)
{
	if (ctfgame.election == ELECT_NONE)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "No election is in progress.\n");
		return;
	}
	if (ent->client->resp.voted)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "You already voted.\n");
		return;
	}
	if (ctfgame.etarget == ent)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "You can't vote for yourself.\n");
		return;
	}

	ent->client->resp.voted = true;

	gi.LocBroadcast_Print(PRINT_HIGH, "{}\n", ctfgame.emsg);
	gi.LocBroadcast_Print(PRINT_CHAT, "Votes: {}  Needed: {}  Time left: {}s\n", ctfgame.evotes, ctfgame.needvotes,
		(ctfgame.electtime - level.time).seconds<int>());
}

void CTFReady(edict_t* ent)
{
	uint32_t i, j;
	edict_t* e;
	uint32_t t1, t2;

	if (ent->client->resp.ctf_team == CTF_NOTEAM)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Pick a team first (hit <TAB> for menu)\n");
		return;
	}

	if (ctfgame.match != MATCH_SETUP)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "A match is not being setup.\n");
		return;
	}

	if (ent->client->resp.ready)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "You have already commited.\n");
		return;
	}

	ent->client->resp.ready = true;
	gi.LocBroadcast_Print(PRINT_HIGH, "{} is ready.\n", ent->client->pers.netname);

	t1 = t2 = 0;
	for (j = 0, i = 1; i <= game.maxclients; i++)
	{
		e = g_edicts + i;
		if (!e->inuse)
			continue;
		if (e->client->resp.ctf_team != CTF_NOTEAM && !e->client->resp.ready)
			j++;
		if (e->client->resp.ctf_team == CTF_TEAM1)
			t1++;
		else if (e->client->resp.ctf_team == CTF_TEAM2)
			t2++;
	}
	if (!j && t1 && t2)
	{
		// everyone has commited
		gi.LocBroadcast_Print(PRINT_CHAT, "All players have committed.  Match starting\n");
		ctfgame.match = MATCH_PREGAME;
		ctfgame.matchtime = level.time + gtime_t::from_sec(matchstarttime->value);
		ctfgame.countdown = false;
		gi.positioned_sound(world->s.origin, world, CHAN_AUTO | CHAN_RELIABLE, gi.soundindex("misc/talk1.wav"), 1, ATTN_NONE, 0);
	}
}

void CTFNotReady(edict_t* ent)
{
	if (ent->client->resp.ctf_team == CTF_NOTEAM)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Pick a team first (hit <TAB> for menu)\n");
		return;
	}

	if (ctfgame.match != MATCH_SETUP && ctfgame.match != MATCH_PREGAME)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "A match is not being setup.\n");
		return;
	}

	if (!ent->client->resp.ready)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "You haven't commited.\n");
		return;
	}

	ent->client->resp.ready = false;
	gi.LocBroadcast_Print(PRINT_HIGH, "{} is no longer ready.\n", ent->client->pers.netname);

	if (ctfgame.match == MATCH_PREGAME)
	{
		gi.LocBroadcast_Print(PRINT_CHAT, "Match halted.\n");
		ctfgame.match = MATCH_SETUP;
		ctfgame.matchtime = level.time + gtime_t::from_min(matchsetuptime->value);
	}
}

void CTFGhost(edict_t* ent)
{
	int i;
	int n;

	if (gi.argc() < 2)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Usage:  ghost <code>\n");
		return;
	}

	if (ent->client->resp.ctf_team != CTF_NOTEAM)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "You are already in the game.\n");
		return;
	}
	if (ctfgame.match != MATCH_GAME)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "No match is in progress.\n");
		return;
	}

	n = atoi(gi.argv(1));

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		if (ctfgame.ghosts[i].code && ctfgame.ghosts[i].code == n)
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "Ghost code accepted, your position has been reinstated.\n");
			ctfgame.ghosts[i].ent->client->resp.ghost = nullptr;
			ent->client->resp.ctf_team = ctfgame.ghosts[i].team;
			ent->client->resp.ghost = ctfgame.ghosts + i;
			ent->client->resp.score = ctfgame.ghosts[i].score;
			ent->client->resp.ctf_state = 0;
			ctfgame.ghosts[i].ent = ent;
			ent->svflags = SVF_NONE;
			ent->flags &= ~FL_GODMODE;
			PutClientInServer(ent);
			gi.LocBroadcast_Print(PRINT_HIGH, "{} has been reinstated to {} team.\n",
				ent->client->pers.netname, CTFTeamName(ent->client->resp.ctf_team));
			return;
		}
	}
	gi.LocClient_Print(ent, PRINT_HIGH, "Invalid ghost code.\n");
}


bool CTFMatchSetup()
{
	if (ctfgame.match == MATCH_SETUP || ctfgame.match == MATCH_PREGAME)
		return true;
	return false;
}

bool CTFMatchOn()
{
	if (ctfgame.match == MATCH_GAME)
		return true;
	return false;
}


/*-----------------------------------------------------------------------*/

void RemoveTech(edict_t* ent) {
	// Recorrer los TECHS específicos
	for (int i = 0; i < sizeof(tech_ids) / sizeof(tech_ids[0]); i++) {
		int const tech_index = tech_ids[i];
		if (ent->client->pers.inventory[tech_index] > 0) {
			// Eliminar el TECH item del inventario del jugador
			ent->client->pers.inventory[tech_index] = 0;

			// Reiniciar el estado de todos los TECH items del mismo tipo
			for (unsigned int j = 0; j < game.maxentities; j++) {
				edict_t* tech = &g_edicts[j];
				if (tech->inuse && tech->item && tech->item->id == tech_index) {
					tech->svflags &= ~SVF_NOCLIENT;
					tech->solid = SOLID_TRIGGER;
					tech->movetype = MOVETYPE_TOSS;
					tech->touch = Touch_Item;
					tech->nextthink = level.time + CTF_TECH_TIMEOUT;
					tech->think = TechThink;
					// Reiniciar el registro de quién ha recogido el item
					tech->item_picked_up_by.reset();
					gi.linkentity(tech);
				}
			}
		}
	}
}

void OpenVoteMenu(edict_t* ent);
void VoteMenuHandler(edict_t* ent, pmenuhnd_t* p);
void UpdateVoteMenu();

constexpr size_t MAX_MAPS_PER_PAGE = 11;
constexpr size_t MAX_MAP_ENTRIES = 64;
constexpr size_t MAX_TECH_OPTIONS = 4;

// Definir el número correcto de elementos en el array `vote_menu`
static pmenu_t vote_menu[MAX_MAPS_PER_PAGE + 5 + 1] = {
	{ "*Map Voting Menu", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "Next", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "Previous", PMENU_ALIGN_LEFT, VoteMenuHandler },
	{ "Close", PMENU_ALIGN_LEFT, VoteMenuHandler }
};

void ShowInventory(edict_t* ent) {

	if (ent->svflags & SVF_BOT)
		return;

	int i;
	gclient_t* cl = ent->client;

	cl->showinventory = true;

	gi.WriteByte(svc_inventory);
	for (i = 0; i < IT_TOTAL; i++) {
		gi.WriteShort(cl->pers.inventory[i]);
	}
	for (; i < MAX_ITEMS; i++) {
		gi.WriteShort(0);
	}
	gi.unicast(ent, true);
}
void OpenHordeMenu(edict_t* ent);
void CTFJoinTeam1(edict_t* ent, pmenuhnd_t* p);
void CTFJoinTeam2(edict_t* ent, pmenuhnd_t* p);
void CTFReturnToMain(edict_t* ent, pmenuhnd_t* p);
void CTFChaseCam(edict_t* ent, pmenuhnd_t* p);
void CTFJoinTeam(edict_t* ent, ctfteam_t desired_team);

void OpenHordeMenu(edict_t* ent)
{
	CreateHordeMenu(ent);
}


//Tech Menus
void OpenTechMenu(edict_t* ent);
void TechMenuHandler(edict_t* ent, pmenuhnd_t* p);

// Definir los TECHS disponibles
static const char* tech_names[] = {
	"Strength",
	"Haste",
	"Regeneration",
	"Resistance"
};

static pmenu_t tech_menu[6] = {
	{ "*Tech Menu", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "Strength", PMENU_ALIGN_LEFT, TechMenuHandler },
	{ "Haste", PMENU_ALIGN_LEFT, TechMenuHandler },
	{ "Regeneration", PMENU_ALIGN_LEFT, TechMenuHandler },
	{ "Resistance", PMENU_ALIGN_LEFT, TechMenuHandler }
};

static pmenu_t tech_menustart[18] = {
	{ "*Tech Menu", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "Select a TECH:", PMENU_ALIGN_LEFT, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "Strength", PMENU_ALIGN_LEFT, TechMenuHandler },
	{ "Haste", PMENU_ALIGN_LEFT, TechMenuHandler },
	{ "Regeneration", PMENU_ALIGN_LEFT, TechMenuHandler },
	{ "Resistance", PMENU_ALIGN_LEFT, TechMenuHandler },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "You can change it later", PMENU_ALIGN_LEFT, nullptr },
	{ "On Horde Menu", PMENU_ALIGN_CENTER, nullptr },
};

void OpenTechMenu(edict_t* ent) {
	if (!ent || !ent->client) {
		return;
	}

	const pmenu_t* menu = (ent->client->resp.ctf_team == CTF_NOTEAM) ?
		tech_menustart : tech_menu;
	const size_t menu_size = (ent->client->resp.ctf_team == CTF_NOTEAM) ?
		sizeof(tech_menustart) : sizeof(tech_menu);

	PMenu_Open(ent, menu, -1, menu_size / sizeof(pmenu_t), nullptr, nullptr);
}

void TechMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	int option;

	// Determinar si estamos usando el menú para CTF_NOTEAM o el menú regular
	if (ent->client->resp.ctf_team == CTF_NOTEAM) {
		option = p->cur - 4; // Ajustar para el menú CTF_NOTEAM (2 líneas extra al principio)
	}
	else {
		option = p->cur - 2; // Ajuste original para el menú regular
	}

	if (option >= 0 && option < sizeof(tech_names) / sizeof(tech_names[0])) {
		// Eliminar TECHS anteriores
		RemoveTech(ent);

		// Mapear el índice de la opción al índice correcto en tech_ids
		int tech_index = -1;
		if (strcmp(tech_names[option], "Strength") == 0) {
			tech_index = IT_TECH_STRENGTH;
		}
		else if (strcmp(tech_names[option], "Haste") == 0) {
			tech_index = IT_TECH_HASTE;
		}
		else if (strcmp(tech_names[option], "Regeneration") == 0) {
			tech_index = IT_TECH_REGENERATION;
		}
		else if (strcmp(tech_names[option], "Resistance") == 0) {
			tech_index = IT_TECH_RESISTANCE;
		}

		// Añadir el nuevo TECH seleccionado
		if (tech_index != -1) {
			ent->client->pers.inventory[tech_index] = 1;
			gi.LocCenter_Print(ent, "\n\n\n\nSelected Tech: {}\n", tech_names[option]);

			// Ejecutar el sonido correspondiente al TECH seleccionado
			switch (tech_index) {
			case IT_TECH_HASTE:
				ent->client->resp.ctf_team == CTF_NOTEAM ? CTFJoinTeam(ent, CTF_TEAM1), CTFApplyHasteSound(ent) : CTFApplyHasteSound(ent);
				break;
			case IT_TECH_STRENGTH:
				ent->client->resp.ctf_team == CTF_NOTEAM ? CTFJoinTeam(ent, CTF_TEAM1), CTFApplyStrengthSound(ent) : CTFApplyStrengthSound(ent);
				break;
			case IT_TECH_REGENERATION:
				ent->client->resp.ctf_team == CTF_NOTEAM ? CTFJoinTeam(ent, CTF_TEAM1), CTFApplyRegeneration(ent) : CTFApplyRegeneration(ent);
				break;
			case IT_TECH_RESISTANCE:
				ent->client->resp.ctf_team == CTF_NOTEAM ? CTFJoinTeam(ent, CTF_TEAM1), CTFApplyResistance(ent, 0) : CTFApplyResistance(ent, 0);
				break;
			}
		}
	}
	PMenu_Close(ent); // Cerrar el menú de TECHS
}

// HUD Menu forward declarations
void UpdateHUDMenu(edict_t* ent, pmenuhnd_t* p);
void HUDMenuHandler(edict_t* ent, pmenuhnd_t* p);

// Menu structure definition
static pmenu_t hud_menu[] = {
	{ "*HUD Options", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_LEFT, HUDMenuHandler },  // ID Toggle
	{ "", PMENU_ALIGN_LEFT, HUDMenuHandler },  // ID-DMG Toggle
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "Back to Horde Menu", PMENU_ALIGN_LEFT, HUDMenuHandler },
	{ "Close", PMENU_ALIGN_LEFT, HUDMenuHandler }
};

void OpenHUDMenu(edict_t* ent) {
	if (!ent || !ent->client) {
		return;
	}

	auto p = PMenu_Open(ent, hud_menu, -1, sizeof(hud_menu) / sizeof(pmenu_t), nullptr, nullptr);
	if (p) {
		UpdateHUDMenu(ent, p);
	}
}

void UpdateHUDMenu(edict_t* ent, pmenuhnd_t* p) {
	// Update ID toggle entry
	PMenu_UpdateEntry(p->entries + 2,
		G_Fmt("Enable/Disable ID [{}]", ent->client->pers.id_state ? "ON" : "OFF").data(),
		PMENU_ALIGN_LEFT,
		HUDMenuHandler);

	// Update ID-DMG toggle entry
	PMenu_UpdateEntry(p->entries + 3,
		G_Fmt("Enable/Disable ID-DMG [{}]", ent->client->pers.iddmg_state ? "ON" : "OFF").data(),
		PMENU_ALIGN_LEFT,
		HUDMenuHandler);

	PMenu_Update(ent);
}

void HUDMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p) {
		return;
	}

	const int option = p->cur;

	switch (option) {
	case 2: // Toggle ID
	case 3: { // Toggle ID-DMG
		bool& state = (option == 2) ? ent->client->pers.id_state : ent->client->pers.iddmg_state;
		state = !state;
		gi.LocCenter_Print(ent, "\n\n\n{} state toggled to {}\n",
			(option == 2) ? "ID" : "ID-DMG",
			state ? "ON" : "OFF");
		UpdateHUDMenu(ent, p);
		break;
	}
	case 5: // Back to Horde Menu
		PMenu_Close(ent);
		OpenHordeMenu(ent);
		break;
	case 6: // Close
		PMenu_Close(ent);
		break;
	default:
		gi.Com_PrintFmt("Invalid menu option {} for player {}\n",
			option, ent->client->pers.netname);
		PMenu_Close(ent);
		break;
	}
}

void CheckAndUpdateMenus() {
	for (auto const player : active_players()) {
		if (!player->client || !player->client->menu) {
			continue;
		}

		// Verificar si el jugador está en el menú HUD comparando con el primer elemento
		bool const isInHUDMenu = (player->client->menu->entries[0].text == std::string("*HUD Options"));

		if (isInHUDMenu) {
			UpdateHUDMenu(player, player->client->menu);
			gi.unicast(player, true);
		}
	}
}

// Saving VOTE INFO

//maplist menu

static pmenu_t map_category_menu[] = {
	{ "*Map Category Selection", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "Big Maps", PMENU_ALIGN_LEFT, nullptr },
	{ "Medium Maps", PMENU_ALIGN_LEFT, nullptr },
	{ "Small Maps", PMENU_ALIGN_LEFT, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "Back to Horde Menu", PMENU_ALIGN_LEFT, nullptr },
	{ "Close", PMENU_ALIGN_LEFT, nullptr }
};

struct map_lists_t {
	std::vector<std::string> big_maps;
	std::vector<std::string> medium_maps;
	std::vector<std::string> small_maps;
	size_t current_page = 0;
	MapSize current_category = MapSize{};
};

static map_lists_t categorized_maps;
static int current_page = 0;

// Función para categorizar los mapas
void CategorizeMapList() {
	categorized_maps.big_maps.clear();
	categorized_maps.medium_maps.clear();
	categorized_maps.small_maps.clear();

	const char* mlist = g_map_list->string;
	char* token;

	while (*(token = COM_Parse(&mlist)) != '\0') {
		const char* map_name = token;
		MapSize const mapSize = GetMapSize(map_name);

		if (mapSize.isBigMap) {
			categorized_maps.big_maps.push_back(map_name);
		}
		else if (mapSize.isSmallMap) {
			categorized_maps.small_maps.push_back(map_name);
		}
		else {
			categorized_maps.medium_maps.push_back(map_name);
		}
	}
}

// Handler para el menú de categorías
void MapCategoryHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p) {
		return;
	}

	const int option = p->cur;

	switch (option) {
	case 2: // Big Maps
		categorized_maps.current_category = MapSize{ false, false, true };
		categorized_maps.current_page = 0;
		UpdateVoteMenu();
		PMenu_Close(ent);
		PMenu_Open(ent, vote_menu, -1, sizeof(vote_menu) / sizeof(pmenu_t), nullptr, nullptr);
		break;

	case 3: // Medium Maps
		categorized_maps.current_category = MapSize{ false, true, false };
		categorized_maps.current_page = 0;
		UpdateVoteMenu();
		PMenu_Close(ent);
		PMenu_Open(ent, vote_menu, -1, sizeof(vote_menu) / sizeof(pmenu_t), nullptr, nullptr);
		break;

	case 4: // Small Maps
		categorized_maps.current_category = MapSize{ true, false, false };
		categorized_maps.current_page = 0;
		UpdateVoteMenu();
		PMenu_Close(ent);
		PMenu_Open(ent, vote_menu, -1, sizeof(vote_menu) / sizeof(pmenu_t), nullptr, nullptr);
		break;

	case 6: // Back to Horde Menu
		PMenu_Close(ent);
		OpenHordeMenu(ent);
		break;

	case 7: // Close
		PMenu_Close(ent);
		break;
	}
}




//vote menu stuff

//void LoadMapList() {
//	const char* mlist = g_map_list->string;
//	char* token;
//
//	map_list.num_maps = 0;
//	while (*(token = COM_Parse(&mlist)) != '\0' && map_list.num_maps < 64) {
//		Q_strlcpy(map_list.maps[map_list.num_maps], token, sizeof(map_list.maps[map_list.num_maps]));
//		map_list.num_maps++;
//	}
//}

std::string format_string(std::string_view fmt, auto&&... args) {
	return fmt::format(fmt, std::forward<decltype(args)>(args)...);
}

// Modificar UpdateVoteMenu para usar format_string en lugar de va
void UpdateVoteMenu() {
	std::vector<std::string>* current_map_list = nullptr;

	if (categorized_maps.current_category.isBigMap) {
		current_map_list = &categorized_maps.big_maps;
	}
	else if (categorized_maps.current_category.isSmallMap) {
		current_map_list = &categorized_maps.small_maps;
	}
	else {
		current_map_list = &categorized_maps.medium_maps;
	}

	if (!current_map_list || current_map_list->empty()) {
		return;
	}

	int start = categorized_maps.current_page * MAX_MAPS_PER_PAGE;
	int end = start + MAX_MAPS_PER_PAGE;
	if (end > current_map_list->size()) end = current_map_list->size();

	// Actualizar título según la categoría
	const char* category_name = categorized_maps.current_category.isBigMap ? "Big Maps" :
		categorized_maps.current_category.isSmallMap ? "Small Maps" : "Medium Maps";
	std::string category_title = "*" + std::string(category_name);
	Q_strlcpy(vote_menu[0].text, category_title.c_str(), sizeof(vote_menu[0].text));

	for (int i = 2; i < 2 + MAX_MAPS_PER_PAGE; i++) {
		if (start < end) {
			Q_strlcpy(vote_menu[i].text, (*current_map_list)[start].c_str(), sizeof(vote_menu[i].text));
			vote_menu[i].SelectFunc = VoteMenuHandler;
			start++;
		}
		else {
			vote_menu[i].text[0] = '\0';
			vote_menu[i].SelectFunc = nullptr;
		}
	}

	// Línea vacía entre los mapas y las opciones de menú
	Q_strlcpy(vote_menu[MAX_MAPS_PER_PAGE + 2].text, "", sizeof(vote_menu[MAX_MAPS_PER_PAGE + 2].text));
	vote_menu[MAX_MAPS_PER_PAGE + 2].SelectFunc = nullptr;

	// Actualizar opciones de navegación
	if ((categorized_maps.current_page + 1) * MAX_MAPS_PER_PAGE < current_map_list->size()) {
		Q_strlcpy(vote_menu[MAX_MAPS_PER_PAGE + 3].text, "Next", sizeof(vote_menu[MAX_MAPS_PER_PAGE + 3].text));
		vote_menu[MAX_MAPS_PER_PAGE + 3].SelectFunc = VoteMenuHandler;
	}
	else {
		vote_menu[MAX_MAPS_PER_PAGE + 3].text[0] = '\0';
		vote_menu[MAX_MAPS_PER_PAGE + 3].SelectFunc = nullptr;
	}

	Q_strlcpy(vote_menu[MAX_MAPS_PER_PAGE + 4].text, "Back", sizeof(vote_menu[MAX_MAPS_PER_PAGE + 4].text));
	vote_menu[MAX_MAPS_PER_PAGE + 4].SelectFunc = VoteMenuHandler;

	Q_strlcpy(vote_menu[MAX_MAPS_PER_PAGE + 5].text, "Close", sizeof(vote_menu[MAX_MAPS_PER_PAGE + 5].text));
	vote_menu[MAX_MAPS_PER_PAGE + 5].SelectFunc = VoteMenuHandler;
}


void VoteMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p) {
		return;
	}

	const int option = p->cur;
	std::vector<std::string>* current_map_list = nullptr;

	if (categorized_maps.current_category.isBigMap) {
		current_map_list = &categorized_maps.big_maps;
	}
	else if (categorized_maps.current_category.isSmallMap) {
		current_map_list = &categorized_maps.small_maps;
	}
	else {
		current_map_list = &categorized_maps.medium_maps;
	}

	if (!current_map_list) {
		PMenu_Close(ent);
		return;
	}

	// Manejo de selección de mapa
	if (option >= 2 && option < 2 + MAX_MAPS_PER_PAGE) {
		const char* voted_map = vote_menu[option].text;
		if (!voted_map[0]) {
			return;
		}

		if (Q_strcasecmp(voted_map, level.mapname) == 0) {
			gi.LocClient_Print(ent, PRINT_HIGH, "This map is already in progress. Please choose a different one.\n");
			return;
		}

		gi.LocCenter_Print(ent, "You voted for Map: {}\n", voted_map);
		Q_strlcpy(ent->client->voted_map, voted_map, sizeof(ent->client->voted_map));
		CTFWarp(ent, voted_map);
		PMenu_Close(ent);
		return;
	}

	// Manejo de navegación
	if (option == MAX_MAPS_PER_PAGE + 3 && // Next
		(categorized_maps.current_page + 1) * MAX_MAPS_PER_PAGE < current_map_list->size()) {
		categorized_maps.current_page++;
		UpdateVoteMenu();
		PMenu_Close(ent);
		PMenu_Open(ent, vote_menu, -1, sizeof(vote_menu) / sizeof(pmenu_t), nullptr, nullptr);
	}
	else if (option == MAX_MAPS_PER_PAGE + 4) { // Back
		PMenu_Close(ent);
		OpenVoteMenu(ent); // Volver al menú de categorías
	}
	else if (option == MAX_MAPS_PER_PAGE + 5) { // Close
		PMenu_Close(ent);
	}
}

void OpenVoteMenu(edict_t* ent) {
	CategorizeMapList(); // Categorizar mapas al abrir el menú
	categorized_maps.current_page = 0;

	// Configurar handlers para el menú de categorías
	for (size_t i = 2; i <= 4; i++) {
		map_category_menu[i].SelectFunc = MapCategoryHandler;
	}
	map_category_menu[6].SelectFunc = MapCategoryHandler;
	map_category_menu[7].SelectFunc = MapCategoryHandler;

	PMenu_Open(ent, map_category_menu, -1, sizeof(map_category_menu) / sizeof(pmenu_t), nullptr, nullptr);
}

void HordeMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	const int option = p->cur;

	// Cierra el menú sólo si es necesario al final de la ejecución del caso
	bool shouldCloseMenu = true;

	if (ctfgame.election == ELECT_NONE) {
		// Opciones cuando no hay votación en progreso
		switch (option) {
		case 2: // Show Inventory
			ShowInventory(ent);
			break;
		case 3: // Go Spectator/AFK
			CTFObserver(ent);
			break;
		case 5: // Vote Map
			OpenVoteMenu(ent);
			shouldCloseMenu = false; // El menú se volverá a abrir en OpenVoteMenu
			break;
		case 7: // Change Tech
			OpenTechMenu(ent);
			shouldCloseMenu = false; // El menú se volverá a abrir en OpenTechMenu
			break;
		case 8: // HUD Options
			OpenHUDMenu(ent);
			shouldCloseMenu = false; // El menú se volverá a abrir en OpenHUDMenu
			break;
		case 11: // Close menu
			break;
		}
	}
	else {
		// Opciones cuando hay votación en progreso
		switch (option) {
		case 2: // Show Inventory
			ShowInventory(ent);
			break;
		case 3: // Go Spectator/AFK
			CTFObserver(ent);
			break;
		case 5: // Vote Yes
			CTFVoteYes(ent);
			break;
		case 6: // Vote No
			CTFVoteNo(ent);
			break;
		case 8: // Change Tech
			OpenTechMenu(ent);
			shouldCloseMenu = false; // El menú se volverá a abrir en OpenTechMenu
			break;
		case 9: // HUD Options
			OpenHUDMenu(ent);
			shouldCloseMenu = false; // El menú se volverá a abrir en OpenHUDMenu
			break;
		case 10: // Close menu
			break;
		}
	}

	if (shouldCloseMenu) {
		PMenu_Close(ent);
	}
}


//vote in progress menu

static const pmenu_t vote_in_progress_menu[] = {
	{ "*Vote In Progress", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr }, // Línea en blanco
	{ "Vote Yes", PMENU_ALIGN_LEFT, HordeMenuHandler },
	{ "Vote No", PMENU_ALIGN_LEFT, HordeMenuHandler },
	{ "", PMENU_ALIGN_CENTER, nullptr }, // Línea en blanco
	{ "Close", PMENU_ALIGN_LEFT, HordeMenuHandler }
};

// horde menu

// // Helper function para crear menús de horda dinámicamente
pmenuhnd_t* CreateHordeMenu(edict_t* ent)
{
	static pmenu_t entries[32];  // Tamaño máximo razonable para el menú
	int count = 0;

	// Menú base
	entries[count++] = { "*Horde Menu", PMENU_ALIGN_CENTER, nullptr };
	entries[count++] = { "", PMENU_ALIGN_CENTER, nullptr };
	entries[count++] = { "Show Inventory", PMENU_ALIGN_LEFT, HordeMenuHandler };
	entries[count++] = { "Go Spectator/AFK", PMENU_ALIGN_LEFT, HordeMenuHandler };
	entries[count++] = { "", PMENU_ALIGN_CENTER, nullptr };

	if (ctfgame.election == ELECT_NONE)
	{
		// Opciones normales
		entries[count++] = { "Vote Map", PMENU_ALIGN_LEFT, HordeMenuHandler };
		entries[count++] = { "", PMENU_ALIGN_CENTER, nullptr };
		entries[count++] = { "Change Tech", PMENU_ALIGN_LEFT, HordeMenuHandler };
		entries[count++] = { "HUD Options", PMENU_ALIGN_LEFT, HordeMenuHandler };
	}
	else
	{
		// Opciones durante votación
		entries[count++] = { "Vote Yes", PMENU_ALIGN_LEFT, HordeMenuHandler };
		entries[count++] = { "Vote No", PMENU_ALIGN_LEFT, HordeMenuHandler };
		entries[count++] = { "", PMENU_ALIGN_CENTER, nullptr };
		entries[count++] = { "Change Tech", PMENU_ALIGN_LEFT, HordeMenuHandler };
		entries[count++] = { "HUD Options", PMENU_ALIGN_LEFT, HordeMenuHandler };
	}

	entries[count++] = { "", PMENU_ALIGN_CENTER, nullptr };
	entries[count++] = { "Close", PMENU_ALIGN_LEFT, HordeMenuHandler };

	return PMenu_Open(ent, entries, -1, count, nullptr, nullptr);
}



pmenuhnd_t* CreateHUDMenu(edict_t* ent)
{
	// Uso de constexpr para tamaños fijos
	static constexpr size_t MAX_ENTRIES = 8;
	static constexpr size_t TEXT_BUFFER_SIZE = 64;
	static pmenu_t entries[MAX_ENTRIES];

	int count = 0;

	// Struct helper para reducir repetición
	struct MenuEntry {
		const char* text;
		int align;
		SelectFunc_t func;
	};

	// Helper para añadir entradas
	auto add_entry = [&](const char* text, int align, SelectFunc_t func = nullptr) {
		if (count < MAX_ENTRIES) {
			Q_strlcpy(entries[count].text, text, sizeof(entries[count].text));
			entries[count].align = align;
			entries[count].SelectFunc = func;
			count++;
		}
		};

	// Título y espaciado
	add_entry("*HUD Options", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Buffer para los textos formateados
	char id_text[TEXT_BUFFER_SIZE];
	char dmg_text[TEXT_BUFFER_SIZE];

	// Los snprintf están bien aquí
	snprintf(id_text, sizeof(id_text), "Enable/Disable ID [%s]",
		ent->client->pers.id_state ? "ON" : "OFF");
	snprintf(dmg_text, sizeof(dmg_text), "Enable/Disable ID-DMG [%s]",
		ent->client->pers.iddmg_state ? "ON" : "OFF");

	// Opciones del menú
	add_entry(id_text, PMENU_ALIGN_LEFT, HUDMenuHandler);
	add_entry(dmg_text, PMENU_ALIGN_LEFT, HUDMenuHandler);
	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("Back to Horde Menu", PMENU_ALIGN_LEFT, HUDMenuHandler);
	add_entry("Close", PMENU_ALIGN_LEFT, HUDMenuHandler);

	return PMenu_Open(ent, entries, -1, count, nullptr, nullptr);
}

//TEAMS MENU & STUFF

static constexpr int jmenu_level = 1;
static constexpr int jmenu_match = 2;
static constexpr int jmenu_horde = 4;
//static const int jmenu_blue = 7;
static constexpr int jmenu_chase = 8;
static constexpr int OriginalModBy = 14;

const pmenu_t joinmenu[] = {
	{ "*$g_pc_3wctf", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "$g_pc_join_red_team", PMENU_ALIGN_LEFT, CTFJoinTeam1 },
	{ "", PMENU_ALIGN_LEFT, nullptr },
	{ "", PMENU_ALIGN_LEFT, nullptr },
	{ "", PMENU_ALIGN_LEFT, nullptr },
	{ "$g_pc_chase_camera", PMENU_ALIGN_LEFT, CTFChaseCam },
	{ "", PMENU_ALIGN_LEFT, nullptr },
	{ "", PMENU_ALIGN_LEFT, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_LEFT, nullptr },
	{ "", PMENU_ALIGN_LEFT, nullptr },
	{ "", PMENU_ALIGN_LEFT, nullptr },
};

const pmenu_t nochasemenu[] = {
	{ "Horde MOD BETA", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "$g_pc_no_chase", PMENU_ALIGN_LEFT, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "$g_pc_return", PMENU_ALIGN_LEFT, CTFReturnToMain }
};

void CTFJoinTeam(edict_t* ent, ctfteam_t desired_team)
{
	PMenu_Close(ent);
	ent->svflags &= ~SVF_NOCLIENT;
	ent->client->resp.ctf_team = desired_team;
	ent->client->resp.ctf_state = 0;
	char const value[MAX_INFO_VALUE] = { 0 };
	//	gi.Info_ValueForKey(ent->client->pers.userinfo, "skin", value, sizeof(value));
	//	CTFAssignSkin(ent, value);

		// assign a ghost if we are in match mode
	if (ctfgame.match == MATCH_GAME)
	{
		if (ent->client->resp.ghost)
			ent->client->resp.ghost->code = 0;
		ent->client->resp.ghost = nullptr;
		CTFAssignGhost(ent);
	}

	PutClientInServer(ent);

	G_PostRespawn(ent);

	gi.LocBroadcast_Print(PRINT_HIGH, "$g_joined_team",
		ent->client->pers.netname, CTFTeamName(desired_team));

	if (ctfgame.match == MATCH_SETUP)
	{
		gi.LocCenter_Print(ent, "Type \"ready\" in console to ready up.\n");
	}

	// if anybody has a menu open, update it immediately
	CTFDirtyTeamMenu();
}

void CTFJoinTeam1(edict_t* ent, pmenuhnd_t* p)
{
	//CTFJoinTeam(ent, CTF_TEAM1);
	OpenTechMenu(ent);

}

void CTFJoinTeam2(edict_t* ent, pmenuhnd_t* p)
{
	CTFJoinTeam(ent, CTF_TEAM2);
}

static void CTFNoChaseCamUpdate(edict_t* ent)
{
	pmenu_t* entries = ent->client->menu->entries;

	SetGameName(&entries[0]);
	SetLevelName(&entries[jmenu_level]);
}

void CTFChaseCam(edict_t* ent, pmenuhnd_t* p)
{
	edict_t* e;

	CTFJoinTeam(ent, CTF_NOTEAM);

	if (ent->client->chase_target)
	{
		ent->client->chase_target = nullptr;
		ent->client->ps.pmove.pm_flags &= ~(PMF_NO_POSITIONAL_PREDICTION | PMF_NO_ANGULAR_PREDICTION);
		PMenu_Close(ent);
		return;
	}

	for (uint32_t i = 1; i <= game.maxclients; i++)
	{
		e = g_edicts + i;
		if (e->inuse && e->solid != SOLID_NOT)
		{
			ent->client->chase_target = e;
			PMenu_Close(ent);
			ent->client->update_chase = true;
			return;
		}
	}

	PMenu_Close(ent);
	PMenu_Open(ent, nochasemenu, -1, sizeof(nochasemenu) / sizeof(pmenu_t), nullptr, CTFNoChaseCamUpdate);
}





void CTFReturnToMain(edict_t* ent, pmenuhnd_t* p)
{
	PMenu_Close(ent);
	CTFOpenJoinMenu(ent);
}

void CTFRequestMatch(edict_t* ent, pmenuhnd_t* p)
{
	//PMenu_Close(ent);

	//CTFBeginElection(ent, ELECT_MATCH, G_Fmt("{} has requested to switch to competition mode.\n",
	//	ent->client->pers.netname).data());
}

void DeathmatchScoreboard(edict_t* ent);

void CTFShowScores(edict_t* ent, pmenu_t* p)
{
	PMenu_Close(ent);

	ent->client->showscores = true;
	ent->client->showinventory = false;
	DeathmatchScoreboard(ent);
}

void CTFUpdateJoinMenu(edict_t* ent)
{
	pmenu_t* entries = ent->client->menu->entries;

	SetGameName(entries);

	//if (ctfgame.match >= MATCH_PREGAME && matchlock->integer)
	//{
	//	Q_strlcpy(entries[jmenu_horde].text, "MATCH IS LOCKED", sizeof(entries[jmenu_horde].text));
	//	entries[jmenu_horde].SelectFunc = nullptr;
	//	//		Q_strlcpy(entries[jmenu_blue].text, "  (entry is not permitted)", sizeof(entries[jmenu_blue].text));
	//	//		entries[jmenu_blue].SelectFunc = nullptr;
	//}
	//else
	//{
	//	if (ctfgame.match >= MATCH_PREGAME)
	//	{
	//		Q_strlcpy(entries[jmenu_horde].text, "Join Red MATCH Team", sizeof(entries[jmenu_horde].text));
	//		//		Q_strlcpy(entries[jmenu_blue].text, "Join Blue MATCH Team", sizeof(entries[jmenu_blue].text));
	//	}
	//	else
		//{
	Q_strlcpy(entries[jmenu_horde].text, "Join and Fight the HORDE!", sizeof(entries[jmenu_horde].text));
	//			Q_strlcpy(entries[jmenu_blue].text, "$g_pc_join_blue_team", sizeof(entries[jmenu_blue].text));
//	}
	entries[jmenu_horde].SelectFunc = CTFJoinTeam1;
	//		entries[jmenu_blue].SelectFunc = CTFJoinTeam2;
//}

	entries[OriginalModBy].text[0] = '\0';
	entries[OriginalModBy].SelectFunc = nullptr;
	if (g_horde->integer)
	{
		Q_strlcpy(entries[OriginalModBy].text, "Original Mod by Paril.\nModified by Enemy.", sizeof(entries[OriginalModBy].text));
	}

	//// KEX_FIXME: what's this for?
	//if (g_teamplay_force_join->string && *g_teamplay_force_join->string)
	//{
	//	if (Q_strcasecmp(g_teamplay_force_join->string, "red") == 0)
	//	{
	//		//			entries[jmenu_blue].text[0] = '\0';
	//		//			entries[jmenu_blue].SelectFunc = nullptr;
	//	}
	//	else if (Q_strcasecmp(g_teamplay_force_join->string, "blue") == 0)
	//	{
	//		entries[jmenu_horde].text[0] = '\0';
	//		entries[jmenu_horde].SelectFunc = nullptr;
	//	}
	//}

	if (ent->client->chase_target)
		Q_strlcpy(entries[jmenu_chase].text, "$g_pc_leave_chase_camera", sizeof(entries[jmenu_chase].text));
	else
		Q_strlcpy(entries[jmenu_chase].text, "Go Spectator", sizeof(entries[jmenu_chase].text));

	SetLevelName(entries + jmenu_level);

	uint32_t num1 = 0, num2 = 0;
	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		if (!g_edicts[i + 1].inuse)
			continue;
		if (game.clients[i].resp.ctf_team == CTF_TEAM1)
			num1++;
		//else if (game.clients[i].resp.ctf_team == CTF_TEAM2)
		//	num2++;
	}

	//switch (ctfgame.match)
	//{
	//case MATCH_NONE:
	//	entries[jmenu_match].text[0] = '\0';
	//	break;

	//case MATCH_SETUP:
	//	Q_strlcpy(entries[jmenu_match].text, "*MATCH SETUP IN PROGRESS", sizeof(entries[jmenu_match].text));
	//	break;

	//case MATCH_PREGAME:
	//	Q_strlcpy(entries[jmenu_match].text, "*MATCH STARTING", sizeof(entries[jmenu_match].text));
	//	break;

	//case MATCH_GAME:
	//	Q_strlcpy(entries[jmenu_match].text, "*MATCH IN PROGRESS", sizeof(entries[jmenu_match].text));
	//	break;

	//default:
	//	break;
	//}

	if (*entries[jmenu_horde].text)
	{

		Q_strlcpy(entries[jmenu_horde + 1].text, "$g_pc_playercount", sizeof(entries[jmenu_horde + 1].text));
		G_FmtTo(entries[jmenu_horde + 1].text_arg1, "{}", num1);
	}
	else
	{
		entries[jmenu_horde + 1].text[0] = '\0';
		entries[jmenu_horde + 1].text_arg1[0] = '\0';
	}
	//	if (*entries[jmenu_blue].text)
	{
		//		Q_strlcpy(entries[jmenu_blue + 1].text, "$g_pc_playercount", sizeof(entries[jmenu_blue + 1].text));
		//		G_FmtTo(entries[jmenu_blue + 1].text_arg1, "{}", num2);
		//	}
		//	else
		//	{
			//	entries[jmenu_blue + 1].text[0] = '\0';
			//	entries[jmenu_blue + 1].text_arg1[0] = '\0';
	}


}

void CTFOpenJoinMenu(edict_t* ent)
{
	uint32_t num1 = 0, num2 = 0;
	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		if (!g_edicts[i + 1].inuse)
			continue;
		if (game.clients[i].resp.ctf_team == CTF_TEAM1)
			num1++;
		else if (game.clients[i].resp.ctf_team == CTF_TEAM2)
			num2++;
	}

	int team;

	if (num1 > num2)
		team = CTF_TEAM1;
	else if (num2 > num1)
		team = CTF_TEAM2;
	team = brandom() ? CTF_TEAM1 : CTF_TEAM2;

	// Cerrar cualquier menú abierto antes de abrir uno nuevo
	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	PMenu_Open(ent, joinmenu, team, sizeof(joinmenu) / sizeof(pmenu_t), nullptr, CTFUpdateJoinMenu);
}

bool CTFStartClient(edict_t* ent)
{
	if (!G_TeamplayEnabled())
		return false;

	if (ent->client->resp.ctf_team != CTF_NOTEAM)
		return false;

	if ((!(ent->svflags & SVF_BOT) && !g_teamplay_force_join->integer) || ctfgame.match >= MATCH_SETUP)
	{
		// Cerrar cualquier menú abierto antes de hacer cambios en el estado del jugador
		if (ent->client->menu) {
			PMenu_Close(ent);
		}

		// start as 'observer'
		ent->movetype = MOVETYPE_NOCLIP;
		ent->solid = SOLID_NOT;
		ent->svflags |= SVF_NOCLIENT;
		ent->client->resp.ctf_team = CTF_NOTEAM;
		ent->client->resp.spectator = true;
		ent->client->ps.gunindex = 0;
		ent->client->ps.gunskin = 0;
		gi.linkentity(ent);

		CTFOpenJoinMenu(ent);
		return true;
	}
	return false;
}

void RemoveAllTechItems(edict_t* ent)
{
	// Verify player and client validity
	if (!ent || !ent->client)
		return;

	// Use the known safe upper bound directly
	const int SAFE_MAX_ITEMS = 85; // Based on the warning message indicating valid range 0-84

	// Iterate through the inventory up to our known safe limit
	for (int i = 0; i < SAFE_MAX_ITEMS; i++)
	{
		const gitem_t* const item = &itemlist[i];

		// Skip if item is null or inventory slot is empty
		if (!item || ent->client->pers.inventory[i] <= 0)
			continue;

		// Check item flags
		const bool isTechItem = (item->flags & IF_TECH) != 0;
		const bool isArmorItem = (item->flags & IF_ARMOR) != 0;
		const bool isPowerupItem = (item->flags & IF_POWERUP) != 0;

		// Early continue if item doesn't match any category
		if (!isTechItem && !isArmorItem && !isPowerupItem)
			continue;


		// Remove item from player's inventory
		ent->client->pers.inventory[i] = 0;

		// Reset all entities of this item type
		for (unsigned int j = 0; j < game.maxentities; j++)
		{
			edict_t* tech = &g_edicts[j];

			// Skip invalid or unused entities
			if (!tech || !tech->inuse || tech->item != item)
				continue;

			// Reset entity state
			tech->svflags &= ~SVF_NOCLIENT;
			tech->solid = SOLID_TRIGGER;
			tech->movetype = MOVETYPE_TOSS;
			tech->touch = Touch_Item;
			tech->nextthink = level.time + CTF_TECH_TIMEOUT;
			tech->think = TechThink;

			// Reset item pickup tracking
			tech->item_picked_up_by.reset();

			// Update entity in game world
			gi.linkentity(tech);
		}
	}
}
void CTFObserver(edict_t* ent)
{
	if (!G_TeamplayEnabled() || g_teamplay_force_join->integer)
		return;

	// start as 'observer'
	if (ent->movetype == MOVETYPE_NOCLIP)
		CTFPlayerResetGrapple(ent);

	CTFDeadDropFlag(ent);
	RemoveAllTechItems(ent); // Llamar a la función para eliminar los tech items

	ent->deadflag = false;
	ent->movetype = MOVETYPE_NOCLIP;
	ent->solid = SOLID_NOT;
	ent->svflags |= SVF_NOCLIENT;
	PutClientInServer(ent);
	ent->client->resp.ctf_team = CTF_NOTEAM;
	CTFJoinTeam(ent, CTF_NOTEAM);

	ent->client->resp.spree = 0;
	ent->client->ps.gunindex = 0;
	ent->client->ps.gunskin = 0;

	// Remove all entities owned by the player
	RemovePlayerOwnedEntities(ent);

	// Cerrar el menú si está abierto
	if (ent->client->menu) {
		PMenu_Close(ent);
	}
}

bool CTFInMatch()
{
	if (ctfgame.match > MATCH_NONE)
		return true;
	return false;
}
bool CTFCheckRules()
{
	int t;
	uint32_t i;
	char text[64];
	edict_t* ent;

	if (ctfgame.election != ELECT_NONE && ctfgame.electtime <= level.time)
	{
		gi.LocBroadcast_Print(PRINT_CHAT, "Election timed out and has been cancelled.\n");
		ctfgame.election = ELECT_NONE;

		// Resetear el estado de votación de todos los jugadores
		for (auto const* ent : active_players()) {
			if (ent->inuse && ent->client)
			{
				ent->client->resp.voted = false;
			}
		}
	}

	// Resto de la función sigue igual...
	if (ctfgame.match != MATCH_NONE)
	{
		t = (ctfgame.matchtime - level.time).seconds<int>();

		// no team warnings in match mode
		ctfgame.warnactive = 0;

		if (t <= 0)
		{ // time ended on something
			switch (ctfgame.match)
			{
			case MATCH_SETUP:
				// go back to normal mode
				if (competition->integer < 3)
				{
					ctfgame.match = MATCH_NONE;
					gi.cvar_set("competition", "1");
					CTFResetAllPlayers();
				}
				else
				{
					// reset the time
					ctfgame.matchtime = level.time + gtime_t::from_min(matchsetuptime->value);
				}
				return false;

			case MATCH_PREGAME:
				// match started!
				CTFStartMatch();
				gi.positioned_sound(world->s.origin, world, CHAN_AUTO | CHAN_RELIABLE, gi.soundindex("misc/tele_up.wav"), 1, ATTN_NONE, 0);
				return false;

			case MATCH_GAME:
				// match ended!
				CTFEndMatch();
				gi.positioned_sound(world->s.origin, world, CHAN_AUTO | CHAN_RELIABLE, gi.soundindex("misc/bigtele.wav"), 1, ATTN_NONE, 0);
				return false;

			default:
				break;
			}
		}

		if (t == ctfgame.lasttime)
			return false;

		ctfgame.lasttime = t;

		switch (ctfgame.match)
		{
			//case MATCH_SETUP:
			//	for (j = 0, i = 1; i <= game.maxclients; i++)
			//	{
			//		ent = g_edicts + i;
			//		if (!ent->inuse)
			//			continue;
			//		if (ent->client->resp.ctf_team != CTF_NOTEAM &&
			//			!ent->client->resp.ready)
			//			j++;
			//	}

			//	if (competition->integer < 3)
			//		G_FmtTo(text, "{:02}:{:02} SETUP: {} not ready", t / 60, t % 60, j);
			//	else
			//		G_FmtTo(text, "SETUP: {} not ready", j);

			//	//		gi.configstring(CONFIG_CTF_MATCH, text);
			//	break;

		case MATCH_PREGAME:
			G_FmtTo(text, "{:02}:{:02} UNTIL START", t / 60, t % 60);
			//		gi.configstring(CONFIG_CTF_MATCH, text);

			if (t <= 10 && !ctfgame.countdown)
			{
				ctfgame.countdown = true;
				gi.positioned_sound(world->s.origin, world, CHAN_AUTO | CHAN_RELIABLE, gi.soundindex("world/10_0.wav"), 1, ATTN_NONE, 0);
			}
			break;

			//case MATCH_GAME:
			//	G_FmtTo(text, "{:02}:{:02} MATCH", t / 60, t % 60);
			//	//		gi.configstring(CONFIG_CTF_MATCH, text);
			//	if (t <= 10 && !ctfgame.countdown)
			//	{
			//		ctfgame.countdown = true;
			//		gi.positioned_sound(world->s.origin, world, CHAN_AUTO | CHAN_RELIABLE, gi.soundindex("world/10_0.wav"), 1, ATTN_NONE, 0);
			//	}
			//	break;

		default:
			break;
		}
		return false;
	}
	else
	{
		int team1 = 0, team2 = 0;

		if (level.time == gtime_t::from_sec(ctfgame.lasttime))
			return false;
		ctfgame.lasttime = level.time.seconds<int>();
		// this is only done in non-match (public) mode

		if (warn_unbalanced->integer)
		{
			// count up the team totals
			for (i = 1; i <= game.maxclients; i++)
			{
				ent = g_edicts + i;
				if (!ent->inuse)
					continue;
				if (ent->client->resp.ctf_team == CTF_TEAM1)
					team1++;
				else if (ent->client->resp.ctf_team == CTF_TEAM2)
					team2++;
			}

			if (team1 - team2 >= 2 && team2 >= 2)
			{
				if (ctfgame.warnactive != CTF_TEAM1)
				{
					ctfgame.warnactive = CTF_TEAM1;
					//			gi.configstring(CONFIG_CTF_TEAMINFO, "WARNING: Red has too many players");
				}
			}
			else if (team2 - team1 >= 2 && team1 >= 2)
			{
				if (ctfgame.warnactive != CTF_TEAM2)
				{
					ctfgame.warnactive = CTF_TEAM2;
					//			gi.configstring(CONFIG_CTF_TEAMINFO, "WARNING: Blue has too many players");
				}
			}
			else
				ctfgame.warnactive = 0;
		}
		else
			ctfgame.warnactive = 0;
	}

	if (capturelimit->integer &&
		(ctfgame.team1 >= capturelimit->integer ||
			ctfgame.team2 >= capturelimit->integer))
	{
		gi.LocBroadcast_Print(PRINT_HIGH, "$g_capturelimit_hit");
		return true;
	}
	return false;
}


/*--------------------------------------------------------------------------
 * just here to help old map conversions
 *--------------------------------------------------------------------------*/

TOUCH(old_teleporter_touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	edict_t* dest;
	vec3_t	 forward;

	if (!other->client)
		return;
	dest = G_PickTarget(self->target);
	if (!dest)
	{
		gi.Com_Print("Couldn't find destination\n");
		return;
	}

	// ZOID
	CTFPlayerResetGrapple(other);
	// ZOID

	// unlink to make sure it can't possibly interfere with KillBox
	gi.unlinkentity(other);

	other->s.origin = dest->s.origin;
	other->s.old_origin = dest->s.origin;
	//	other->s.origin[2] += 10;

	// clear the velocity and hold them in place briefly
	other->velocity = {};
	other->client->ps.pmove.pm_time = 160; // hold time
	other->client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;

	// draw the teleport splash at source and on the player
	self->enemy->s.event = EV_PLAYER_TELEPORT;
	other->s.event = EV_PLAYER_TELEPORT;

	// set angles
	other->client->ps.pmove.delta_angles = dest->s.angles - other->client->resp.cmd_angles;

	other->s.angles[PITCH] = 0;
	other->s.angles[YAW] = dest->s.angles[YAW];
	other->s.angles[ROLL] = 0;
	other->client->ps.viewangles = dest->s.angles;
	other->client->v_angle = dest->s.angles;

	// give a little forward velocity
	AngleVectors(other->client->v_angle, forward, nullptr, nullptr);
	other->velocity = forward * 200;

	gi.linkentity(other);

	// kill anything at the destination
	if (!KillBox(other, true))
	{
	}

	// [Paril-KEX] move sphere, if we own it
	if (other->client->owned_sphere)
	{
		edict_t* sphere = other->client->owned_sphere;
		sphere->s.origin = other->s.origin;
		sphere->s.origin[2] = other->absmax[2];
		sphere->s.angles[YAW] = other->s.angles[YAW];
		gi.linkentity(sphere);
	}
}

/*QUAKED trigger_ctf_teleport (0.5 0.5 0.5) ?
Players touching this will be teleported
*/
void SP_trigger_ctf_teleport(edict_t* ent)
{
	edict_t* s;
	int		 i;

	if (!ent->target)
	{
		gi.Com_Print("teleporter without a target.\n");
		G_FreeEdict(ent);
		return;
	}

	ent->svflags |= SVF_NOCLIENT;
	ent->solid = SOLID_TRIGGER;
	ent->touch = old_teleporter_touch;
	gi.setmodel(ent, ent->model);
	gi.linkentity(ent);

	// noise maker and splash effect dude
	s = G_Spawn();
	ent->enemy = s;
	for (i = 0; i < 3; i++)
		s->s.origin[i] = ent->mins[i] + (ent->maxs[i] - ent->mins[i]) / 2;
	s->s.sound = gi.soundindex("world/hum1.wav");
	gi.linkentity(s);
}

/*QUAKED info_ctf_teleport_destination (0.5 0.5 0.5) (-16 -16 -24) (16 16 32)
Point trigger_teleports at these.
*/
void SP_info_ctf_teleport_destination(edict_t* ent)
{
	ent->s.origin[2] += 16;
}

/*----------------------------------------------------------------------------------*/
/* ADMIN */

struct admin_settings_t
{
	int	 matchlen;
	int	 matchsetuplen;
	int	 matchstartlen;
	bool weaponsstay;
	bool instantitems;
	bool quaddrop;
	bool instantweap;
	bool matchlock;
};

void CTFAdmin_UpdateSettings(edict_t* ent, pmenuhnd_t* setmenu);
void CTFOpenAdminMenu(edict_t* ent);

void CTFAdmin_SettingsApply(edict_t* ent, pmenuhnd_t* p)
{
	admin_settings_t* settings = (admin_settings_t*)p->arg;

	if (settings->matchlen != matchtime->value)
	{
		gi.LocBroadcast_Print(PRINT_HIGH, "{} changed the match length to {} minutes.\n",
			ent->client->pers.netname, settings->matchlen);
		if (ctfgame.match == MATCH_GAME)
		{
			// in the middle of a match, change it on the fly
			ctfgame.matchtime = (ctfgame.matchtime - gtime_t::from_min(matchtime->value)) + gtime_t::from_min(settings->matchlen);
		}
		;
		gi.cvar_set("matchtime", G_Fmt("{}", settings->matchlen).data());
	}

	if (settings->matchsetuplen != matchsetuptime->value)
	{
		gi.LocBroadcast_Print(PRINT_HIGH, "{} changed the match setup time to {} minutes.\n",
			ent->client->pers.netname, settings->matchsetuplen);
		if (ctfgame.match == MATCH_SETUP)
		{
			// in the middle of a match, change it on the fly
			ctfgame.matchtime = (ctfgame.matchtime - gtime_t::from_min(matchsetuptime->value)) + gtime_t::from_min(settings->matchsetuplen);
		}
		;
		gi.cvar_set("matchsetuptime", G_Fmt("{}", settings->matchsetuplen).data());
	}

	if (settings->matchstartlen != matchstarttime->value)
	{
		gi.LocBroadcast_Print(PRINT_HIGH, "{} changed the match start time to {} seconds.\n",
			ent->client->pers.netname, settings->matchstartlen);
		if (ctfgame.match == MATCH_PREGAME)
		{
			// in the middle of a match, change it on the fly
			ctfgame.matchtime = (ctfgame.matchtime - gtime_t::from_sec(matchstarttime->value)) + gtime_t::from_sec(settings->matchstartlen);
		}
		gi.cvar_set("matchstarttime", G_Fmt("{}", settings->matchstartlen).data());
	}

	if (settings->weaponsstay != !!g_dm_weapons_stay->integer)
	{
		gi.LocBroadcast_Print(PRINT_HIGH, "{} turned {} weapons stay.\n",
			ent->client->pers.netname, settings->weaponsstay ? "on" : "off");
		gi.cvar_set("g_dm_weapons_stay", settings->weaponsstay ? "1" : "0");
	}

	if (settings->instantitems != !!g_dm_instant_items->integer)
	{
		gi.LocBroadcast_Print(PRINT_HIGH, "{} turned {} instant items.\n",
			ent->client->pers.netname, settings->instantitems ? "on" : "off");
		gi.cvar_set("g_dm_instant_items", settings->instantitems ? "1" : "0");
	}

	if (settings->quaddrop != (bool)!g_dm_no_quad_drop->integer)
	{
		gi.LocBroadcast_Print(PRINT_HIGH, "{} turned {} quad drop.\n",
			ent->client->pers.netname, settings->quaddrop ? "on" : "off");
		gi.cvar_set("g_dm_no_quad_drop", !settings->quaddrop ? "1" : "0");
	}

	if (settings->instantweap != !!g_instant_weapon_switch->integer)
	{
		gi.LocBroadcast_Print(PRINT_HIGH, "{} turned {} instant weapons.\n",
			ent->client->pers.netname, settings->instantweap ? "on" : "off");
		gi.cvar_set("g_instant_weapon_switch", settings->instantweap ? "1" : "0");
	}

	if (settings->matchlock != !!matchlock->integer)
	{
		gi.LocBroadcast_Print(PRINT_HIGH, "{} turned {} match lock.\n",
			ent->client->pers.netname, settings->matchlock ? "on" : "off");
		gi.cvar_set("matchlock", settings->matchlock ? "1" : "0");
	}

	PMenu_Close(ent);
	CTFOpenAdminMenu(ent);
}

void CTFAdmin_SettingsCancel(edict_t* ent, pmenuhnd_t* p)
{
	PMenu_Close(ent);
	CTFOpenAdminMenu(ent);
}

void CTFAdmin_ChangeMatchLen(edict_t* ent, pmenuhnd_t* p)
{
	admin_settings_t* settings = (admin_settings_t*)p->arg;

	settings->matchlen = (settings->matchlen % 60) + 5;
	if (settings->matchlen < 5)
		settings->matchlen = 5;

	CTFAdmin_UpdateSettings(ent, p);
}

void CTFAdmin_ChangeMatchSetupLen(edict_t* ent, pmenuhnd_t* p)
{
	admin_settings_t* settings = (admin_settings_t*)p->arg;

	settings->matchsetuplen = (settings->matchsetuplen % 60) + 5;
	if (settings->matchsetuplen < 5)
		settings->matchsetuplen = 5;

	CTFAdmin_UpdateSettings(ent, p);
}

void CTFAdmin_ChangeMatchStartLen(edict_t* ent, pmenuhnd_t* p)
{
	admin_settings_t* settings = (admin_settings_t*)p->arg;

	settings->matchstartlen = (settings->matchstartlen % 600) + 10;
	if (settings->matchstartlen < 20)
		settings->matchstartlen = 20;

	CTFAdmin_UpdateSettings(ent, p);
}

void CTFAdmin_ChangeWeapStay(edict_t* ent, pmenuhnd_t* p)
{
	admin_settings_t* settings = (admin_settings_t*)p->arg;

	settings->weaponsstay = !settings->weaponsstay;
	CTFAdmin_UpdateSettings(ent, p);
}

void CTFAdmin_ChangeInstantItems(edict_t* ent, pmenuhnd_t* p)
{
	admin_settings_t* settings = (admin_settings_t*)p->arg;

	settings->instantitems = !settings->instantitems;
	CTFAdmin_UpdateSettings(ent, p);
}

void CTFAdmin_ChangeQuadDrop(edict_t* ent, pmenuhnd_t* p)
{
	admin_settings_t* settings = (admin_settings_t*)p->arg;

	settings->quaddrop = !settings->quaddrop;
	CTFAdmin_UpdateSettings(ent, p);
}

void CTFAdmin_ChangeInstantWeap(edict_t* ent, pmenuhnd_t* p)
{
	admin_settings_t* settings = (admin_settings_t*)p->arg;

	settings->instantweap = !settings->instantweap;
	CTFAdmin_UpdateSettings(ent, p);
}

void CTFAdmin_ChangeMatchLock(edict_t* ent, pmenuhnd_t* p)
{
	admin_settings_t* settings = (admin_settings_t*)p->arg;

	settings->matchlock = !settings->matchlock;
	CTFAdmin_UpdateSettings(ent, p);
}

void CTFAdmin_UpdateSettings(edict_t* ent, pmenuhnd_t* setmenu)
{
	int				  i = 2;
	admin_settings_t* settings = (admin_settings_t*)setmenu->arg;

	PMenu_UpdateEntry(setmenu->entries + i, G_Fmt("Match Len:       {:2} mins", settings->matchlen).data(), PMENU_ALIGN_LEFT, CTFAdmin_ChangeMatchLen);
	i++;

	PMenu_UpdateEntry(setmenu->entries + i, G_Fmt("Match Setup Len: {:2} mins", settings->matchsetuplen).data(), PMENU_ALIGN_LEFT, CTFAdmin_ChangeMatchSetupLen);
	i++;

	PMenu_UpdateEntry(setmenu->entries + i, G_Fmt("Match Start Len: {:2} secs", settings->matchstartlen).data(), PMENU_ALIGN_LEFT, CTFAdmin_ChangeMatchStartLen);
	i++;

	PMenu_UpdateEntry(setmenu->entries + i, G_Fmt("Weapons Stay:    {}", settings->weaponsstay ? "Yes" : "No").data(), PMENU_ALIGN_LEFT, CTFAdmin_ChangeWeapStay);
	i++;

	PMenu_UpdateEntry(setmenu->entries + i, G_Fmt("Instant Items:   {}", settings->instantitems ? "Yes" : "No").data(), PMENU_ALIGN_LEFT, CTFAdmin_ChangeInstantItems);
	i++;

	PMenu_UpdateEntry(setmenu->entries + i, G_Fmt("Quad Drop:       {}", settings->quaddrop ? "Yes" : "No").data(), PMENU_ALIGN_LEFT, CTFAdmin_ChangeQuadDrop);
	i++;

	PMenu_UpdateEntry(setmenu->entries + i, G_Fmt("Instant Weapons: {}", settings->instantweap ? "Yes" : "No").data(), PMENU_ALIGN_LEFT, CTFAdmin_ChangeInstantWeap);
	i++;

	PMenu_UpdateEntry(setmenu->entries + i, G_Fmt("Match Lock:      {}", settings->matchlock ? "Yes" : "No").data(), PMENU_ALIGN_LEFT, CTFAdmin_ChangeMatchLock);
	i++;

	PMenu_Update(ent);
}

const pmenu_t def_setmenu[] = {
	{ "*Settings Menu", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_LEFT, nullptr }, // int matchlen;
	{ "", PMENU_ALIGN_LEFT, nullptr }, // int matchsetuplen;
	{ "", PMENU_ALIGN_LEFT, nullptr }, // int matchstartlen;
	{ "", PMENU_ALIGN_LEFT, nullptr }, // bool weaponsstay;
	{ "", PMENU_ALIGN_LEFT, nullptr }, // bool instantitems;
	{ "", PMENU_ALIGN_LEFT, nullptr }, // bool quaddrop;
	{ "", PMENU_ALIGN_LEFT, nullptr }, // bool instantweap;
	{ "", PMENU_ALIGN_LEFT, nullptr }, // bool matchlock;
	{ "", PMENU_ALIGN_LEFT, nullptr },
	{ "Apply", PMENU_ALIGN_LEFT, CTFAdmin_SettingsApply },
	{ "Cancel", PMENU_ALIGN_LEFT, CTFAdmin_SettingsCancel }
};

void CTFAdmin_Settings(edict_t* ent, pmenuhnd_t* p)
{
	admin_settings_t* settings;
	pmenuhnd_t* menu;

	PMenu_Close(ent);

	settings = (admin_settings_t*)gi.TagMalloc(sizeof(*settings), TAG_LEVEL);

	settings->matchlen = matchtime->integer;
	settings->matchsetuplen = matchsetuptime->integer;
	settings->matchstartlen = matchstarttime->integer;
	settings->weaponsstay = g_dm_weapons_stay->integer;
	settings->instantitems = g_dm_instant_items->integer;
	settings->quaddrop = !g_dm_no_quad_drop->integer;
	settings->instantweap = g_instant_weapon_switch->integer != 0;
	settings->matchlock = matchlock->integer != 0;

	menu = PMenu_Open(ent, def_setmenu, -1, sizeof(def_setmenu) / sizeof(pmenu_t), settings, nullptr);
	CTFAdmin_UpdateSettings(ent, menu);
}

void CTFAdmin_MatchSet(edict_t* ent, pmenuhnd_t* p)
{
	PMenu_Close(ent);

	if (ctfgame.match == MATCH_SETUP)
	{
		gi.LocBroadcast_Print(PRINT_CHAT, "Match has been forced to start.\n");
		ctfgame.match = MATCH_PREGAME;
		ctfgame.matchtime = level.time + gtime_t::from_sec(matchstarttime->value);
		gi.positioned_sound(world->s.origin, world, CHAN_AUTO | CHAN_RELIABLE, gi.soundindex("misc/talk1.wav"), 1, ATTN_NONE, 0);
		ctfgame.countdown = false;
	}
	else if (ctfgame.match == MATCH_GAME)
	{
		gi.LocBroadcast_Print(PRINT_CHAT, "Match has been forced to terminate.\n");
		ctfgame.match = MATCH_SETUP;
		ctfgame.matchtime = level.time + gtime_t::from_min(matchsetuptime->value);
		CTFResetAllPlayers();
	}
}

void CTFAdmin_MatchMode(edict_t* ent, pmenuhnd_t* p)
{
	PMenu_Close(ent);

	if (ctfgame.match != MATCH_SETUP)
	{
		if (competition->integer < 3)
			gi.cvar_set("competition", "2");
		ctfgame.match = MATCH_SETUP;
		CTFResetAllPlayers();
	}
}

void CTFAdmin_Reset(edict_t* ent, pmenuhnd_t* p)
{
	PMenu_Close(ent);

	// go back to normal mode
	gi.LocBroadcast_Print(PRINT_CHAT, "Match mode has been terminated, reseting to normal game.\n");
	ctfgame.match = MATCH_NONE;
	gi.cvar_set("competition", "1");
	CTFResetAllPlayers();
}

void CTFAdmin_Cancel(edict_t* ent, pmenuhnd_t* p)
{
	PMenu_Close(ent);
}

pmenu_t adminmenu[] = {
	{ "*Administration Menu", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr }, // blank
	{ "Settings", PMENU_ALIGN_LEFT, CTFAdmin_Settings },
	{ "", PMENU_ALIGN_LEFT, nullptr },
	{ "", PMENU_ALIGN_LEFT, nullptr },
	{ "Cancel", PMENU_ALIGN_LEFT, CTFAdmin_Cancel },
	{ "", PMENU_ALIGN_CENTER, nullptr },
};

void CTFOpenAdminMenu(edict_t* ent)
{
	adminmenu[3].text[0] = '\0';
	adminmenu[3].SelectFunc = nullptr;
	adminmenu[4].text[0] = '\0';
	adminmenu[4].SelectFunc = nullptr;
	if (ctfgame.match == MATCH_SETUP)
	{
		Q_strlcpy(adminmenu[3].text, "Force start match", sizeof(adminmenu[3].text));
		adminmenu[3].SelectFunc = CTFAdmin_MatchSet;
		Q_strlcpy(adminmenu[4].text, "Reset to pickup mode", sizeof(adminmenu[4].text));
		adminmenu[4].SelectFunc = CTFAdmin_Reset;
	}
	else if (ctfgame.match == MATCH_GAME || ctfgame.match == MATCH_PREGAME)
	{
		Q_strlcpy(adminmenu[3].text, "Cancel match", sizeof(adminmenu[3].text));
		adminmenu[3].SelectFunc = CTFAdmin_MatchSet;
	}
	else if (ctfgame.match == MATCH_NONE && competition->integer)
	{
		Q_strlcpy(adminmenu[3].text, "Switch to match mode", sizeof(adminmenu[3].text));
		adminmenu[3].SelectFunc = CTFAdmin_MatchMode;
	}

	//	if (ent->client->menu)
	//		PMenu_Close(ent->client->menu);

	PMenu_Open(ent, adminmenu, -1, sizeof(adminmenu) / sizeof(pmenu_t), nullptr, nullptr);
}

void CTFAdmin(edict_t* ent)
{
	//	if (!allow_admin->integer)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Administration is disabled\n");
		return;
	}

	//if (gi.argc() > 1 && admin_password->string && *admin_password->string &&
	//	!ent->client->resp.admin && strcmp(admin_password->string, gi.argv(1)) == 0)
	//{
	//	ent->client->resp.admin = true;
	//	gi.LocBroadcast_Print(PRINT_HIGH, "{} has become an admin.\n", ent->client->pers.netname);
	//	gi.LocClient_Print(ent, PRINT_HIGH, "Type 'admin' to access the adminstration menu.\n");
	//}

	//if (!ent->client->resp.admin)
	//{
	//	CTFBeginElection(ent, ELECT_ADMIN, G_Fmt("{} has requested admin rights.\n",
	//		ent->client->pers.netname).data());
	//	return;
	//}

	//if (ent->client->menu)
	//	PMenu_Close(ent);

	//CTFOpenAdminMenu(ent);
}

/*----------------------------------------------------------------*/

void CTFStats(edict_t* ent)
{
	if (!G_TeamplayEnabled())
		return;

	ghost_t* g;
	static std::string text;
	edict_t* e2;

	text.clear();

	if (ctfgame.match == MATCH_SETUP)
	{
		for (uint32_t i = 1; i <= game.maxclients; i++)
		{
			e2 = g_edicts + i;
			if (!e2->inuse)
				continue;
			if (!e2->client->resp.ready && e2->client->resp.ctf_team != CTF_NOTEAM)
			{
				std::string_view const str = G_Fmt("{} is not ready.\n", e2->client->pers.netname);

				if (text.length() + str.length() < MAX_CTF_STAT_LENGTH - 50)
					text += str;
			}
		}
	}

	uint32_t i;
	for (i = 0, g = ctfgame.ghosts; i < MAX_CLIENTS; i++, g++)
		if (g->ent)
			break;

	if (i == MAX_CLIENTS)
	{
		if (!text.length())
			text = "No statistics available.\n";

		gi.Client_Print(ent, PRINT_HIGH, text.c_str());
		return;
	}

	text += "  #|Name            |Score|Kills|Death|BasDf|CarDf|Effcy|\n";

	for (i = 0, g = ctfgame.ghosts; i < MAX_CLIENTS; i++, g++)
	{
		if (!*g->netname)
			continue;

		int32_t e;

		if (g->deaths + g->kills == 0)
			e = 50;
		else
			e = g->kills * 100 / (g->kills + g->deaths);
		std::string_view const str = G_Fmt("{:3}|{:<16.16}|{:5}|{:5}|{:5}|{:5}|{:5}|{:4}%|\n",
			g->number,
			g->netname,
			g->score,
			g->kills,
			g->deaths,
			g->basedef,
			g->carrierdef,
			e);

		if (text.length() + str.length() > MAX_CTF_STAT_LENGTH - 50)
		{
			text += "And more...\n";
			break;
		}

		text += str;
	}

	gi.Client_Print(ent, PRINT_HIGH, text.c_str());
}

void CTFPlayerList(edict_t* ent)
{
	static std::string text;
	edict_t* e2;

	// number, name, connect time, ping, score, admin
	text.clear();

	for (uint32_t i = 1; i <= game.maxclients; i++)
	{
		e2 = g_edicts + i;
		if (!e2->inuse)
			continue;

		std::string_view const str = G_Fmt("{:3} {:<16.16} {:02}:{:02} {:4} {:3}{}{}\n",
			i,
			e2->client->pers.netname,
			(level.time - e2->client->resp.entertime).milliseconds() / 60000,
			((level.time - e2->client->resp.entertime).milliseconds() % 60000) / 1000,
			e2->client->ping,
			e2->client->resp.score,
			(ctfgame.match == MATCH_SETUP || ctfgame.match == MATCH_PREGAME) ? (e2->client->resp.ready ? " (ready)" : " (notready)") : "",
			e2->client->resp.admin ? " (admin)" : "");

		if (text.length() + str.length() > MAX_CTF_STAT_LENGTH - 50)
		{
			text += "And more...\n";
			break;

		}

		text += str;
	}

	gi.Client_Print(ent, PRINT_HIGH, text.data());
}

void CTFWarp(edict_t* ent, const char* map_name)
{
	// Function to print available maps list
	auto PrintMapList = [](edict_t* ent) {
		const char* mlist = g_map_list->string;
		char* token;
		std::string formatted_list;
		formatted_list.reserve(8192); // Pre-reserve space but allocate on heap

		int map_index = 1;
		while (*(token = COM_Parse(&mlist)) != '\0')
		{
			// Use string formatting with less stack usage
			formatted_list += (map_index == 1 ? "" : "-");
			formatted_list += std::to_string(map_index);
			formatted_list += ": ";
			formatted_list += token;
			formatted_list += "\n";

			map_index++;
		}

		gi.LocClient_Print(ent, PRINT_HIGH, "Available levels are:\n{}\n", formatted_list.c_str());
		};

	// If no map name provided, just print the list
	if (!map_name || !*map_name)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Choose a level to vote to. You can now use (optional) just number!\n");
		PrintMapList(ent);
		return;
	}

	// Find the requested map
	const char* mlist = g_map_list->string;
	char* token;
	const int vote_index = atoi(map_name);
	int current_index = 1;
	bool found_map = false;

	while (*(token = COM_Parse(&mlist)) != '\0')
	{
		if (current_index == vote_index || Q_strcasecmp(token, map_name) == 0)
		{
			found_map = true;
			break;
		}
		current_index++;
	}

	// Handle map not found
	if (!found_map)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Unknown HORDE level.\n");
		PrintMapList(ent);
		return;
	}

	// Check if trying to vote for current map
	if (Q_strcasecmp(token, level.mapname) == 0)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Cannot vote for the current map.\n");
		return;
	}

	// Process the vote
	char playerName[32];
	gi.Info_ValueForKey(ent->client->pers.userinfo, "name", playerName, sizeof(playerName));
	Q_strlcpy(ctfgame.elevel, token, sizeof(ctfgame.elevel));

	std::string voteMessage = G_Fmt("{} has requested a vote for level {}.\nUse Horde Menu (TURTLE) on POWERUP WHEEL to vote YES/NO.\n",
		playerName, token).data();

	if (CTFBeginElection(ent, ELECT_MAP, voteMessage.c_str()))
	{
		if (ent->client->menu) {
			PMenu_Close(ent);
		}
	}
}

void CTFBoot(edict_t* ent)
{
	edict_t* targ;

	if (!ent->client->resp.admin)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "You are not an admin.\n");
		return;
	}

	if (gi.argc() < 2)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Who do you want to kick?\n");
		return;
	}

	if (*gi.argv(1) < '0' && *gi.argv(1) > '9')
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Specify the player number to kick.\n");
		return;
	}

	uint32_t i = strtoul(gi.argv(1), nullptr, 10);
	if (i < 1 || i > game.maxclients)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Invalid player number.\n");
		return;
	}

	targ = g_edicts + i;
	if (!targ->inuse)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "That player number is not connected.\n");
		return;
	}

	gi.AddCommandString(G_Fmt("kick {}\n", i - 1).data());
}

void CTFSetPowerUpEffect(edict_t* ent, effects_t def)
{

	ent->s.effects |= def;
}