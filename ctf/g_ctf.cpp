// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "../g_local.h"
#include "../m_player.h"

#include <assert.h>

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



std::string FormatClassname(const std::string& classname) {
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
	for (const auto& prefix : ALLOWED_PREFIXES) { 
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
std::string GetDisplayName(const char* classname);

template<typename Duration>
int GetRemainingTime(gtime_t current_time, gtime_t end_time) {
	return std::max(0, static_cast<int>((end_time - current_time).template seconds<float>()));
}

enum class EntityType {
	Player,
	Monster,
	Other
};

EntityType GetEntityType(const edict_t* ent) {
	if (!ent) return EntityType::Other;
	if (ent->client) return EntityType::Player;
	if (ent->svflags & SVF_MONSTER) return EntityType::Monster;
	return EntityType::Other;
}

std::string FormatEntityInfo(edict_t* ent) {
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
		const std::string  playerName = GetPlayerName(ent);
		info = fmt::format("{}\nH: {}", playerName, ent->health);

		int armor_value = GetArmorInfo(ent);
		if (armor_value > 0) {
			info += fmt::format(" A: {}", armor_value);
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

// Configuration constants
struct CTFIDViewConfig {
	static constexpr gtime_t UPDATE_INTERVAL = 108_ms;
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

	// Process entities in priority order: players -> monsters -> other entities
	auto checkEntity = [&](edict_t* who) {
		if (!IsValidTarget(ent, who, false)) return;

		// Calculate direction and distance
		vec3_t dir = who->s.origin - viewer_pos;
		float const dist = dir.normalize();

		// Early return if this entity is farther than our current best
		if (dist >= result.distance) return;

		// Check angle requirements
		float const min_dot = (dist < CTFIDViewConfig::CLOSE_DISTANCE)
			? CTFIDViewConfig::CLOSE_MIN_DOT
			: CTFIDViewConfig::MIN_DOT;
		if (forward.dot(dir) <= min_dot) return;

		// Line of sight check
		if (!CanSeeTarget(ent, viewer_pos, who, who->s.origin)) return;

		// Update result
		result.distance = dist;
		result.target = who;
		};

	// Check in priority order with early exits for close targets
	for (edict_t* who : active_players()) {
		checkEntity(who);
		if (result.distance <= CTFIDViewConfig::CLOSE_DISTANCE) return result;
	}

	for (edict_t* who : active_monsters()) {
		checkEntity(who);
		if (result.distance <= CTFIDViewConfig::CLOSE_DISTANCE) return result;
	}

	for (edict_t* who = g_edicts + 1; who < g_edicts + globals.num_edicts; who++) {
		if (who->client || (who->svflags & SVF_MONSTER)) continue;
		checkEntity(who);
		if (result.distance <= CTFIDViewConfig::CLOSE_DISTANCE) return result;
	}

	return result;
}

void CTFSetIDView(edict_t* ent) {
	static bool is_processing = false;

	if (is_processing || level.intermissiontime ||
		level.time - ent->client->resp.lastidtime < CTFIDViewConfig::UPDATE_INTERVAL) {
		return;
	}

	// Use RAII to ensure is_processing is reset
	is_processing = true;
	struct ScopeGuard {
		~ScopeGuard() { is_processing = false; }
	} guard;

	ent->client->resp.lastidtime = level.time;

	// Get unique configstring index for this client
	int const client_cs = CONFIG_CTF_PLAYER_NAME + (ent - g_edicts - 1);

	vec3_t forward;
	AngleVectors(ent->client->v_angle, forward, nullptr, nullptr);

	// Find best target
	TargetSearchResult result = FindBestTarget(ent, forward);
	edict_t* best = result.target;

	// Check if we should keep the previous target
	if (!best && ent->client->idtarget && IsValidTarget(ent, ent->client->idtarget, true)) {
		best = ent->client->idtarget;
	}

	// Only clear stats if we're actually changing targets
	if (ent->client->idtarget != best) {
		gi.configstring(client_cs, "");
		ent->client->ps.stats[STAT_TARGET_HEALTH_STRING] = 0;
		ent->client->idtarget = best;
	}

	if (best) {
		std::string info = FormatEntityInfo(best);
		if (!info.empty()) {
			gi.configstring(client_cs, info.c_str());
			ent->client->ps.stats[STAT_TARGET_HEALTH_STRING] = client_cs;
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
#include <array>
#include <string_view>
#include <unordered_map>


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
	for (uint32_t i = 0; i < game.maxentities; i++)
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
	// Safety check
	if (!p) return;

	if (ctf->integer) // Check if CTF mode is active
		Q_strlcpy(p->text, "$g_pc_3wctf", sizeof(p->text)); // Use localized CTF name
	else // Assume Horde or other modes
		Q_strlcpy(p->text, "*Horde MOD BETA v0.0094*", sizeof(p->text)); // Horde title (added '*' for centering style)
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
	//static constexpr size_t MAX_VOTE_STRING = 128;

	if (ctfgame.election != ELECT_NONE) {
		// Format with bounds checking
		std::string vote_info = fmt::format("{} Time left: {}s\n",
			ctfgame.emsg,
			static_cast<int>((ctfgame.electtime - level.time).seconds()));

		//if (vote_info.length() >= MAX_VOTE_STRING) {
		//	vote_info.resize(MAX_VOTE_STRING - 1);
		//	vote_info += "...";
		//}

		gi.configstring(CONFIG_VOTE_INFO, vote_info.c_str());
		//ClearHordeMessage();

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
			for (uint32_t i = 0; i < game.maxclients; i++) {
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
			for (uint32_t j = 0; j < game.maxentities; j++) {
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

void HordeJoinTeam(edict_t* ent, pmenuhnd_t* p);
void CTFJoinTeam2(edict_t* ent, pmenuhnd_t* p);
void CTFReturnToMain(edict_t* ent, pmenuhnd_t* p);
void GoChaseCam(edict_t* ent, pmenuhnd_t* p);
void CTFJoinTeam(edict_t* ent, ctfteam_t desired_team);
//TEAMS MENU & STUFF

static constexpr int jmenu_level = 1;
static constexpr int jmenu_match = 2;
static constexpr int jmenu_horde = 4;
//static const int jmenu_blue = 7;
static constexpr int jmenu_chase = 8;
static constexpr int OriginalModBy = 14;
//
//const pmenu_t joinmenu[] = {
//	{ "*PLACEHOLDER*", PMENU_ALIGN_CENTER, nullptr },        // 0: Title (Set by SetGameName)
//	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 1: Blank Separator
//	{ "*PLACEHOLDER*", PMENU_ALIGN_CENTER, nullptr },        // 2: Level Name (Set by SetLevelName)
//	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 3: Blank Separator
//	{ "Join and Fight the HORDE!", PMENU_ALIGN_LEFT, HordeJoinTeam }, // 4: Join Horde (Moved Up)
//	{ "", PMENU_ALIGN_LEFT, nullptr },                      // 5: Player Count (filled dynamically)
//	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 6: Blank Separator
//	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 7: Blank (Spacing)
//	{ "Go Spectator", PMENU_ALIGN_LEFT, GoChaseCam },     // 8: Go Spectator / Leave Chase (Moved Up)
//	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 9: Blank Separator
//	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 10: Blank (Spacing)
//	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 11: Blank (Spacing)
//	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 12: Blank (Spacing)
//	{ "Discord: Enemy0416", PMENU_ALIGN_CENTER, nullptr },    // 13: Discord Info
//	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 14: Blank Separator
//	{ "", PMENU_ALIGN_LEFT, nullptr }                       // 15: Credits (filled dynamically)
//};
//
//// Recalculate size
//constexpr size_t JOINMENU_SIZE = sizeof(joinmenu) / sizeof(pmenu_t); // Should be 16 now
//
//
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

void HordeJoinTeam(edict_t* ent, pmenuhnd_t* p)
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

void GoChaseCam(edict_t* ent, pmenuhnd_t* p)
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
	HordeOpenJoinMenu(ent);
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

//void HordeUpdateJoinMenu(edict_t* ent)
//{
//	// --- Safety Checks ---
//	if (!ent || !ent->client || !ent->client->menu || !ent->client->menu->entries)
//	{
//		gi.Com_Print("Warning: HordeUpdateJoinMenu called with invalid ent/client/menu.\n");
//		return;
//	}
//	// Check if the menu size matches what we expect
//	if (ent->client->menu->num != JOINMENU_SIZE) {
//		gi.Com_PrintFmt("Warning: HordeUpdateJoinMenu - menu size mismatch (expected {}, got {}).\n", JOINMENU_SIZE, ent->client->menu->num);
//		// Optionally close the menu or return to prevent potential crashes
//		// PMenu_Close(ent);
//		return;
//	}
//
//
//	pmenu_t* entries = ent->client->menu->entries;
//
//	// --- Update Static/Common Entries ---
//	SetGameName(&entries[JOINMENU_TITLE_IDX]);       // Update Game Title
//	SetLevelName(&entries[JOINMENU_LEVELNAME_IDX]);  // Update Level Name
//
//	// --- Horde Specific Logic ---
//	if (g_horde->integer) // Check if Horde mode is active
//	{
//		// Set "Join Horde" option text and handler
//		Q_strlcpy(entries[JOINMENU_JOIN_HORDE_IDX].text, "Join and Fight the HORDE!", sizeof(entries[JOINMENU_JOIN_HORDE_IDX].text));
//		entries[JOINMENU_JOIN_HORDE_IDX].SelectFunc = HordeJoinTeam;
//
//		// Set Credits text
//		Q_strlcpy(entries[JOINMENU_CREDITS_IDX].text, "Original Mod by Paril.\nModified by Enemy.", sizeof(entries[JOINMENU_CREDITS_IDX].text));
//		entries[JOINMENU_CREDITS_IDX].SelectFunc = nullptr;
//
//		// Set Discord Text (already defined in joinmenu array, but ensure it's visible)
//		// No action needed if it's static in the array definition. Ensure it's not cleared below.
//
//
//		// --- Update Player Count (Optimized) ---
//		uint32_t horde_player_count = 0;
//		for (const auto* player_ent : active_players()) {
//			if (player_ent->client->resp.ctf_team == CTF_TEAM1) {
//				horde_player_count++;
//			}
//		}
//
//		// Update the player count display entry
//		Q_strlcpy(entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text, "$g_pc_playercount", sizeof(entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text));
//		G_FmtTo(entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text_arg1, sizeof(entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text_arg1), "{}", horde_player_count);
//
//	}
//	else // Not Horde mode
//	{
//		// Disable/Clear Horde-specific entries
//		Q_strlcpy(entries[JOINMENU_JOIN_HORDE_IDX].text, "(Horde Mode Disabled)", sizeof(entries[JOINMENU_JOIN_HORDE_IDX].text));
//		entries[JOINMENU_JOIN_HORDE_IDX].SelectFunc = nullptr;
//		entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text[0] = '\0';
//		entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text_arg1[0] = '\0';
//		entries[JOINMENU_CREDITS_IDX].text[0] = '\0';
//		entries[JOINMENU_CREDITS_IDX].SelectFunc = nullptr;
//		// Clear Discord info if not in Horde mode
//		entries[JOINMENU_DISCORD_IDX].text[0] = '\0';
//		entries[JOINMENU_DISCORD_IDX].SelectFunc = nullptr;
//	}
//
//
//	// --- Update Chase Cam / Spectator Option ---
//	const char* chase_text = ent->client->chase_target ?
//		"$g_pc_leave_chase_camera" :
//		"Go Spectator";
//	Q_strlcpy(entries[JOINMENU_CHASECAM_IDX].text, chase_text, sizeof(entries[JOINMENU_CHASECAM_IDX].text));
//	entries[JOINMENU_CHASECAM_IDX].SelectFunc = GoChaseCam;
//
//	// Ensure other blank entries remain blank (already handled by array definition)
//}
//
//void HordeOpenJoinMenu(edict_t* ent)
//{
//	uint32_t num1 = 0, num2 = 0;
//	for (uint32_t i = 0; i < game.maxclients; i++)
//	{
//		if (!g_edicts[i + 1].inuse)
//			continue;
//		if (game.clients[i].resp.ctf_team == CTF_TEAM1)
//			num1++;
//		else if (game.clients[i].resp.ctf_team == CTF_TEAM2)
//			num2++;
//	}
//
//	int team;
//
//	if (num1 > num2)
//		team = CTF_TEAM1;
//	else if (num2 > num1)
//		team = CTF_TEAM2;
//	team = brandom() ? CTF_TEAM1 : CTF_TEAM2;
//
//	// Cerrar cualquier menú abierto antes de abrir uno nuevo
//	if (ent->client->menu) {
//		PMenu_Close(ent);
//	}
//
//	PMenu_Open(ent, joinmenu, team, sizeof(joinmenu) / sizeof(pmenu_t), nullptr, HordeUpdateJoinMenu);
//}


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

		HordeOpenJoinMenu(ent);
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
		for (uint32_t j = 0; j < game.maxentities; j++)
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