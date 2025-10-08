// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"
#include "g_statusbar.h"
#include "shared.h"
#include "horde/p_flyer_morph.h"

/*
======================================================================

INTERMISSION

======================================================================
*/

void DeathmatchScoreboard(edict_t* ent);

void MoveClientToIntermission(edict_t* ent)
{
	// [Paril-KEX]
	if (ent->client->ps.pmove.pm_type != PM_FREEZE)
		ent->s.event = EV_OTHER_TELEPORT;
	// This initial setting of showscores is overridden later, so it can be removed.
	// if (deathmatch->integer)
	//	ent->client->showscores = true; 
	ent->s.origin = level.intermission_origin;
	ent->client->ps.pmove.origin = level.intermission_origin;
	ent->client->ps.viewangles = level.intermission_angle;
	ent->client->ps.pmove.pm_type = PM_FREEZE;
	ent->client->ps.gunindex = 0;
	ent->client->ps.gunskin = 0;
	ent->client->ps.damage_blend[3] = ent->client->ps.screen_blend[3] = 0;
	ent->client->ps.rdflags = RDF_NONE;

	// clean up powerup info
	ent->client->quad_time = 0_ms;
	ent->client->invincible_time = 0_ms;
	ent->client->breather_time = 0_ms;
	ent->client->enviro_time = 0_ms;
	ent->client->invisible_time = 0_ms;
	ent->client->grenade_blew_up = false;
	ent->client->grenade_time = 0_ms;

	ent->client->showhelp = false;
	ent->client->showscores = false; // Set to false, will be set to true later if deathmatch

	globals.server_flags &= ~SERVER_FLAG_SLOW_TIME;

	// RAFAEL
	ent->client->quadfire_time = 0_ms;
	// RAFAEL
	// ROGUE
	ent->client->ir_time = 0_ms;
	ent->client->nuke_time = 0_ms;
	ent->client->double_time = 0_ms;
	ent->client->tracker_pain_time = 0_ms;
	// ROGUE

	ent->viewheight = 0;
	ent->s.modelindex = 0; // Only one assignment needed
	ent->s.modelindex2 = 0;
	ent->s.modelindex3 = 0;
	// ent->s.modelindex = 0; // Redundant
	ent->s.effects = EF_NONE;
	ent->s.sound = 0;
	ent->solid = SOLID_NOT;
	ent->movetype = MOVETYPE_NOCLIP;

	gi.linkentity(ent);

	// add the layout

	if (deathmatch->integer)
	{
		DeathmatchScoreboard(ent);
		ent->client->showscores = true;
	}
}

// [Paril-KEX] update the level entry for end-of-unit screen
void G_UpdateLevelEntry()
{
	if (!level.entry)
		return;

	level.entry->found_secrets = level.found_secrets;
	level.entry->total_secrets = level.total_secrets;
	level.entry->killed_monsters = level.killed_monsters;
	level.entry->total_monsters = level.total_monsters;
}

inline void G_EndOfUnitEntry(std::string& layout, int y, const level_entry_t& entry)
{
	layout += G_Fmt("yv {} ", y);

	// we didn't visit this level, so print it as an unknown entry
	if (!*entry.pretty_name)
	{
		layout += "table_row 1 ??? ";
		return;
	}

	layout += G_Fmt("table_row 4 \"{}\" ", entry.pretty_name);
	layout += G_Fmt("{}/{} ", entry.killed_monsters, entry.total_monsters);
	layout += G_Fmt("{}/{} ", entry.found_secrets, entry.total_secrets);

	int32_t minutes = entry.time.milliseconds() / 60000;
	int32_t seconds = (entry.time.milliseconds() / 1000) % 60;
	int32_t milliseconds = entry.time.milliseconds() % 1000;

	layout += G_Fmt("{:02}:{:02}:{:03} ", minutes, seconds, milliseconds);
}

void G_EndOfUnitMessage()
{
	// [Paril-KEX] update game level entry
	G_UpdateLevelEntry();

	std::string layout;
	layout.reserve(2048);  // Pre-allocate for end-of-unit layout

	// sort entries
	std::sort(game.level_entries.begin(), game.level_entries.end(), [](const level_entry_t& a, const level_entry_t& b) {
		// This logic is a bit dense but functional. A comment could clarify priorities.
		// Priority: 1. visit_order (if non-zero), 2. pretty_name exists, 3. others
		int32_t const a_order = a.visit_order ? a.visit_order : (*a.pretty_name ? (MAX_LEVELS_PER_UNIT + 1) : (MAX_LEVELS_PER_UNIT + 2));
		int32_t const b_order = b.visit_order ? b.visit_order : (*b.pretty_name ? (MAX_LEVELS_PER_UNIT + 1) : (MAX_LEVELS_PER_UNIT + 2));

		return a_order < b_order;
		});

	layout += "start_table 4 $m_eou_level $m_eou_kills $m_eou_secrets $m_eou_time ";

	int y = 16;
	level_entry_t totals{};
	int32_t num_rows = 0;

	for (auto& entry : game.level_entries)
	{
		if (!*entry.map_name) // Stop if map_name is empty (end of valid entries)
			break;

		G_EndOfUnitEntry(layout, y, entry);

		y += 8;

		totals.found_secrets += entry.found_secrets;
		totals.killed_monsters += entry.killed_monsters;
		totals.time += entry.time;
		totals.total_monsters += entry.total_monsters;
		totals.total_secrets += entry.total_secrets;

		if (entry.visit_order) // Count actual visited levels for totals display condition
			num_rows++;
	}

	y += 8;

	// make this a space so it prints totals
	if (num_rows > 1) // Only show totals if more than one level was visited
	{
		layout += "table_row 0 "; // empty row to separate totals
		totals.pretty_name[0] = ' '; // Hack to make G_EndOfUnitEntry format totals; consider a dedicated totals row format
		G_EndOfUnitEntry(layout, y, totals);
	}

	layout += "xv 160 yt 0 draw_table ";

	layout += G_Fmt("ifgef {} yb -48 xv 0 loc_cstring2 0 \"$m_eou_press_button\" endif ",
		(level.intermission_server_frame + (5_sec).frames()));

	gi.WriteByte(svc_layout);
	gi.WriteString(layout.c_str());
	gi.multicast(vec3_origin, MULTICAST_ALL, true);

	for (auto player : active_players())
		player->client->showeou = true;
}

// data is binary now.
// u8 num_teams
// u8 num_players
// [ repeat num_teams:
//   string team_name
// ]
// [ repeat num_players:
//   u8 client_index
//   s32 score
//   u8 ranking
//   (if num_teams > 0)
//     u8 team
// ]
void G_ReportMatchDetails(bool is_end)
{
	static std::array<uint32_t, MAX_CLIENTS> player_ranks; // Static is fine as it's used as a temporary buffer, zeroed each call.

	player_ranks = {}; // Zero out ranks

	// CTF/TDM is simple
	if (ctf->integer || teamplay->integer)
	{
		CTFCalcRankings(player_ranks);

		gi.WriteByte(2); // num_teams
		gi.WriteString("Stroggicide Squad"); // team 0 name
		gi.WriteString("BLUE TEAM");         // team 1 name
	}
	else // Deathmatch
	{
		// sort players by score, then match everybody to
		// the current highest score downwards until we run out of players.
		static std::array<edict_t*, MAX_CLIENTS> sorted_players_buffer; // Static buffer for sorting
		size_t num_active_players = 0;

		for (auto player : active_players())
		{
			if (num_active_players < MAX_CLIENTS) // Ensure we don't overflow buffer
				sorted_players_buffer[num_active_players++] = player;
			else
				break; // Should not happen if MAX_CLIENTS is sane
		}
		
		std::sort(sorted_players_buffer.begin(), sorted_players_buffer.begin() + num_active_players, 
			[](const edict_t* a, const edict_t* b) { 
				return b->client->resp.score < a->client->resp.score; // Higher score first
			});

		int32_t current_score = INT_MIN; // Use a very low initial score
		int32_t current_rank = 0;

		for (size_t i = 0; i < num_active_players; i++)
		{
			if (!current_rank || sorted_players_buffer[i]->client->resp.score != current_score)
			{
				current_rank++;
				current_score = sorted_players_buffer[i]->client->resp.score;
			}
			// Ensure player index is within bounds for player_ranks
			size_t player_idx = sorted_players_buffer[i]->s.number - 1;
			if (player_idx < MAX_CLIENTS)
				player_ranks[player_idx] = current_rank;
		}

		gi.WriteByte(0); // num_teams for non-team modes
	}

	uint8_t num_players_to_report = 0; // Renamed for clarity

	// First pass to count reportable players (if protocol requires count first)
	for (const auto* const player : active_players())
	{
		if (player->client->pers.spawned && !player->client->resp.spectator)
		{
			if (G_TeamplayEnabled() && player->client->resp.ctf_team == CTF_NOTEAM)
				continue;
			num_players_to_report++;
		}
	}

	gi.WriteByte(num_players_to_report);

	// Second pass to write player data
	for (const auto* const player : active_players())
	{
		if (player->client->pers.spawned && !player->client->resp.spectator)
		{
			if (G_TeamplayEnabled() && player->client->resp.ctf_team == CTF_NOTEAM)
				continue;
			
			size_t player_idx = player->s.number - 1;
			gi.WriteByte(static_cast<uint8_t>(player_idx)); // client_index
			gi.WriteLong(player->client->resp.score); // score
			
			if(player_idx < MAX_CLIENTS)
				gi.WriteByte(static_cast<uint8_t>(player_ranks[player_idx])); // ranking
			else
				gi.WriteByte(0); // Default rank if out of bounds (should not happen)


			if (G_TeamplayEnabled())
				gi.WriteByte(player->client->resp.ctf_team == CTF_TEAM1 ? 0 : 1); // team index
		}
	}

	gi.ReportMatchDetails_Multicast(is_end);
}

void BeginIntermission(edict_t* targ)
{
	edict_t* ent, * client;

	if (level.intermissiontime)
		return; // already activated

	// ZOID
	if (ctf->integer)
		CTFCalcScores();
	// ZOID

	game.autosaved = false;

	level.intermissiontime = level.time;

	// Restore any morphed players before level transition
	if (coop->integer)
	{
		for (uint32_t i = 0; i < game.maxclients; i++)
		{
			client = g_edicts + 1 + i;
			if (!client->inuse)
				continue;
			if (IsMorphed(client))
				RestoreMorphed(client);
		}
	}

	// respawn any dead clients
	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		client = g_edicts + 1 + i;
		if (!client->inuse)
			continue;
		if (client->health <= 0)
		{
			// give us our max health back since it will reset
			// to pers.health; in instanced items we'd lose the items
			// we touched so we always want to respawn with our max.
			if (P_UseCoopInstancedItems())
				client->client->pers.health = client->client->pers.max_health = client->max_health;

			respawn(client);
		}
	}

	level.intermission_server_frame = gi.ServerFrame();
	level.changemap = targ->map;
	level.intermission_clear = targ->spawnflags.has(SPAWNFLAG_CHANGELEVEL_CLEAR_INVENTORY);
	level.intermission_eou = false;
	level.intermission_fade = targ->spawnflags.has(SPAWNFLAG_CHANGELEVEL_FADE_OUT);

	// destroy all player trails
	PlayerTrail_Destroy(nullptr);

	// [Paril-KEX] update game level entry
	G_UpdateLevelEntry();

	if (strstr(level.changemap, "*"))
	{
		if (G_IsCooperative())
		{
			for (uint32_t i = 0; i < game.maxclients; i++)
			{
				client = g_edicts + 1 + i;
				if (!client->inuse)
					continue;
				// strip players of all keys between units
				for (uint32_t n = 0; n < IT_TOTAL; n++)
					if (itemlist[n].flags & IF_KEY)
						client->client->pers.inventory[n] = 0;
			}
		}

		if (level.achievement && level.achievement[0])
		{
			gi.WriteByte(svc_achievement);
			gi.WriteString(level.achievement);
			gi.multicast(vec3_origin, MULTICAST_ALL, true);
		}

		level.intermission_eou = true;

		// "no end of unit" maps handle intermission differently
		if (!targ->spawnflags.has(SPAWNFLAG_CHANGELEVEL_NO_END_OF_UNIT))
			G_EndOfUnitMessage();
		else if (targ->spawnflags.has(SPAWNFLAG_CHANGELEVEL_IMMEDIATE_LEAVE) && !G_IsDeathmatch()) 
		{
			// Need to call this now
			G_ReportMatchDetails(true);
			level.exitintermission = 1; // go immediately to the next level
			return;
		}
	}
	else
	{
		if (!G_IsDeathmatch()) 
		{
			level.exitintermission = 1; // go immediately to the next level
			return;
		}
	}

	// Call while intermission is running
	G_ReportMatchDetails(true);

	level.exitintermission = 0;

	if (!level.level_intermission_set)
	{
		// find an intermission spot
		ent = G_FindByString<&edict_t::classname>(nullptr, "info_player_intermission");

		//if (!ent)
		//{ // the map creator forgot to put in an intermission point...
		//	ent = G_FindByString<&edict_t::classname>(nullptr, "info_player_deathmatch");
		//	if (!ent)
		//		ent = G_FindByString<&edict_t::classname>(nullptr, "info_player_start");
		//}

		if (!ent)
		{ // the map creator forgot to put in an intermission point...
			ent = G_FindByString<&edict_t::classname>(nullptr, "info_player_start");
			if (!ent)
				ent = G_FindByString<&edict_t::classname>(nullptr, "info_player_deathmatch");
		}
		else
		{ // choose one of four spots
			int32_t i = irandom(4);
			while (i--)
			{
				ent = G_FindByString<&edict_t::classname>(ent, "info_player_intermission");
				if (!ent) // wrap around the list
					ent = G_FindByString<&edict_t::classname>(ent, "info_player_intermission");
			}
		}

		level.intermission_origin = ent->s.origin;
		level.intermission_angle = ent->s.angles;
	}

	// move all clients to the intermission point
	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		client = g_edicts + 1 + i;
		if (!client->inuse)
			continue;
		MoveClientToIntermission(client);
	}
}

constexpr size_t MAX_SCOREBOARD_SIZE = 1024;
constexpr size_t SCOREBOARD_ENTRY_RESERVE_SIZE = 128; 

/*
==================
DeathmatchScoreboardMessage

==================
*/
void DeathmatchScoreboardMessage(edict_t* ent, edict_t* killer)
{
	std::string entry_buffer;
	std::string layout_string_buffer; // Renamed to avoid conflict with 'string' from <string>
	
	entry_buffer.reserve(SCOREBOARD_ENTRY_RESERVE_SIZE); 
	layout_string_buffer.reserve(MAX_SCOREBOARD_SIZE);

	int x, y;
	gclient_t* cl;
	// edict_t* cl_ent; // Will get this from PlayerScoreInfo
	const char* tag;

	// ZOID
	if (G_TeamplayEnabled())
	{
		HordeScoreboardMessage(ent, killer);
		return;
	}
	// ZOID

	struct PlayerScoreInfo {
		int client_idx;
		int score;
		int ping;
		gtime_t entertime;
		edict_t* entity; 
	};

	std::vector<PlayerScoreInfo> sorted_players_info;
	sorted_players_info.reserve(game.maxclients);

	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		edict_t* current_cl_ent = g_edicts + 1 + i;
		if (!current_cl_ent->inuse || game.clients[i].resp.spectator)
			continue;
		
		sorted_players_info.push_back({
			(int)i, 
			game.clients[i].resp.score,
			game.clients[i].ping,
			game.clients[i].resp.entertime,
			current_cl_ent
		});
	}

	std::sort(sorted_players_info.begin(), sorted_players_info.end(), 
		[](const PlayerScoreInfo& a, const PlayerScoreInfo& b) {
		if (a.score != b.score) {
			return a.score > b.score; // Higher score first
		}
		return a.client_idx < b.client_idx; // Tie-break by client index
	});

	size_t players_to_display_count = std::min(sorted_players_info.size(), (size_t)16);

	for (size_t i = 0; i < players_to_display_count; i++)
	{
		const auto& p_info = sorted_players_info[i];
		cl = &game.clients[p_info.client_idx]; 

		x = (i >= 8) ? 130 : -72; 
		y = 0 + 32 * (i % 8);    

		tag = nullptr; 

		if (gamerules->integer)
		{
			if (DMGame.DogTag)
				DMGame.DogTag(p_info.entity, killer, &tag);
		}

		entry_buffer.clear(); 
		if (tag)
		{
			fmt::format_to(std::back_inserter(entry_buffer), FMT_STRING("xv {} yv {} picn {} "), x + 32, y, tag);
		}
		else
		{
			fmt::format_to(std::back_inserter(entry_buffer), FMT_STRING("xv {} yv {} dogtag {} "), x + 32, y, p_info.client_idx);
		}
		
		if (layout_string_buffer.length() + entry_buffer.length() > MAX_SCOREBOARD_SIZE)
			break;
		layout_string_buffer += entry_buffer;
		
		entry_buffer.clear();
		fmt::format_to(std::back_inserter(entry_buffer),
			FMT_STRING("client {} {} {} {} {} {} "),
			x, y, p_info.client_idx, cl->resp.score, cl->ping, (int32_t)(level.time - cl->resp.entertime).minutes());

		if (layout_string_buffer.length() + entry_buffer.length() > MAX_SCOREBOARD_SIZE)
			break;
		layout_string_buffer += entry_buffer;
	}

	// Add remaining info with overflow protection
	if (fraglimit->integer && layout_string_buffer.length() < MAX_SCOREBOARD_SIZE - 100)
	{
		fmt::format_to(std::back_inserter(layout_string_buffer), FMT_STRING("xv -20 yv -10 loc_string2 1 $g_score_frags \"{}\" "), fraglimit->integer);
	}
	if (timelimit->value && !level.intermissiontime && layout_string_buffer.length() < MAX_SCOREBOARD_SIZE - 100)
	{
		fmt::format_to(std::back_inserter(layout_string_buffer), FMT_STRING("xv 340 yv -10 time_limit {} "), gi.ServerFrame() + ((gtime_t::from_min(timelimit->value) - level.time)).milliseconds() / gi.frame_time_ms);
	}

	if (level.intermissiontime && layout_string_buffer.length() < MAX_SCOREBOARD_SIZE - 100)
		fmt::format_to(std::back_inserter(layout_string_buffer), FMT_STRING("ifgef {} yb -48 xv 0 loc_cstring2 0 \"$m_eou_press_button\" endif "), (level.intermission_server_frame + (5_sec).frames()));

	// Final safety check before sending
	if (layout_string_buffer.length() >= MAX_SCOREBOARD_SIZE)
	{
		layout_string_buffer.resize(MAX_SCOREBOARD_SIZE - 1);
		if (developer && developer->integer)
			gi.Com_Print("WARNING: Scoreboard truncated to prevent overflow\n");
	}

	gi.WriteByte(svc_layout);
	gi.WriteString(layout_string_buffer.c_str());
}
/*
==================
DeathmatchScoreboard

Draw instead of help message.
Note that it isn't that hard to overflow the 1400 byte message limit!
==================
*/
void DeathmatchScoreboard(edict_t* ent)
{
	DeathmatchScoreboardMessage(ent, ent->enemy);
	gi.unicast(ent, true);
	ent->client->menutime = level.time + 3_sec;
}

/*
==================
Cmd_Score_f

Display the scoreboard
==================
*/
void Cmd_Score_f(edict_t* ent)
{
	if (level.intermissiontime)
		return;

	ent->client->showinventory = false;
	ent->client->showhelp = false;

	globals.server_flags &= ~SERVER_FLAG_SLOW_TIME;

	// ZOID
	if (ent->client->menu)
		PMenu_Close(ent); 
	// ZOID

	if (!G_IsDeathmatch() && !G_IsCooperative())
		return;

	if (ent->client->showscores)
	{
		ent->client->showscores = false;
		ent->client->update_chase = true;
		return;
	}

	ent->client->showscores = true;
	DeathmatchScoreboard(ent);
}

/*
==================
HelpComputer

Draw help computer.
==================
*/
void HelpComputer(edict_t* ent)
{
	const char* sk;

	if (skill->integer == 0)
		sk = "$m_easy";
	else if (skill->integer == 1)
		sk = "$m_medium";
	else if (skill->integer == 2)
		sk = "$m_hard";
	else
		sk = "$m_nightmare";

	// send the layout

	std::string helpString;
	helpString.reserve(2048);  // Pre-allocate to avoid reallocations during concatenation
	helpString += G_Fmt(
		"xv 32 yv 8 picn help "		   // background
		"xv 0 yv 25 cstring2 \"{}\" ",  // level name
		level.level_name);

	if (level.is_n64)
	{
		helpString += G_Fmt("xv 0 yv 54 loc_cstring 1 \"{{}}\" \"{}\" ",  // help 1
			game.helpmessage1);
	}
	else
	{
		const char* first_message = game.helpmessage1;
		const char* first_title = level.primary_objective_title;

		const char* second_message = game.helpmessage2;
		const char* second_title = level.secondary_objective_title;

		if (pm_config.physics_flags & PHYSICS_PSX_SCALE)
		{
			std::swap(first_message, second_message);
			std::swap(first_title, second_title);
		}

		int y = 54;
		if (strlen(first_message))
		{
			helpString += G_Fmt("xv 0 yv {} loc_cstring2 0 \"{}\" "  // title
				"xv 0 yv {} loc_cstring 0 \"{}\" ",
				y,
				first_title,
				y + 11,
				first_message);

			y += 58;
		}

		if (strlen(second_message))
		{
			helpString += G_Fmt("xv 0 yv {} loc_cstring2 0 \"{}\" "  // title
				"xv 0 yv {} loc_cstring 0 \"{}\" ",
				y,
				second_title,
				y + 11,
				second_message);
		}

	}

	helpString += G_Fmt("xv 55 yv 164 loc_string2 0 \"{}\" "
		"xv 265 yv 164 loc_rstring2 1 \"{{}}: {}/{}\" \"$g_pc_goals\" "
		"xv 55 yv 172 loc_string2 1 \"{{}}: {}/{}\" \"$g_pc_kills\" "
		"xv 265 yv 172 loc_rstring2 1 \"{{}}: {}/{}\" \"$g_pc_secrets\" ",
		sk,
		level.found_goals, level.total_goals,
		level.killed_monsters, level.total_monsters,
		level.found_secrets, level.total_secrets);

	gi.WriteByte(svc_layout);
	gi.WriteString(helpString.c_str());
	gi.unicast(ent, true);
}

/*
==================
Cmd_Help_f

Display the current help message
==================
*/
void Cmd_Help_f(edict_t* ent)
{
	// this is for backwards compatability
	if (G_IsDeathmatch())
	{
		Cmd_Score_f(ent);
		return;
	}

	if (level.intermissiontime)
		return;

	ent->client->showinventory = false;
	ent->client->showscores = false;

	if (ent->client->showhelp &&
		(ent->client->pers.game_help1changed == game.help1changed ||
			ent->client->pers.game_help2changed == game.help2changed))
	{
		ent->client->showhelp = false;
		globals.server_flags &= ~SERVER_FLAG_SLOW_TIME;
		return;
	}

	ent->client->showhelp = true;
	ent->client->pers.helpchanged = 0;
	globals.server_flags |= SERVER_FLAG_SLOW_TIME;
	HelpComputer(ent);
}

//=======================================================================


// [Paril-KEX] for stats we want to always be set in coop
// even if we're spectating
void G_SetCoopStats(edict_t* ent) {
	if (G_IsDeathmatch() && g_coop_enable_lives->integer)
		ent->client->ps.stats[STAT_LIVES] = ent->client->pers.lives + 1;
	else
		ent->client->ps.stats[STAT_LIVES] = 0;

	if (G_IsDeathmatch() && !level.intermissiontime) {
		ent->client->ps.stats[STAT_WAVE_NUMBER] = current_wave_level;
	}

	ent->client->ps.stats[STAT_FRAGS] = ent->client->resp.score;
	UpdateVoteHUD();

	//if (G_IsDeathmatch() && level.intermissiontime) {
	//	ent->client->ps.stats[STAT_WAVE_NUMBER] = last_wave_number;
	//}

	ent->client->ps.stats[STAT_REMAINING_MONSTERS] = GetStroggsNum();

	// stat for text on what we're doing for respawn
	if (ent->client->coop_respawn_state)
		ent->client->ps.stats[STAT_COOP_RESPAWN] = CONFIG_COOP_RESPAWN_STRING + (ent->client->coop_respawn_state - COOP_RESPAWN_IN_COMBAT);
	else
		ent->client->ps.stats[STAT_COOP_RESPAWN] = 0;

	// Game timer with horde mode support
	if (sv_wave_timer->integer)
	{
		int t{};

		// Horde mode timer
		if (g_horde->integer) {
			// Get wave timer and convert to seconds as int
			const gtime_t waveTime = GetWaveTimer();
			t = waveTime.seconds<int>();
		}
		// Only update if the time has changed
		if (t != ent->client->last_wave_timer_horde_update)
		{
			ent->client->last_wave_timer_horde_update = t;
			char game_timer[64];
			// Ensure t is not negative
			t = std::max(0, t);
			G_FmtTo(game_timer, "{:02}:{:02}", t / 60, t % 60);
			ent->client->ps.stats[STAT_GAME_TIMER] = HORDE_WAVE_TIMER;
			gi.configstring(HORDE_WAVE_TIMER, game_timer);
		}
	}
}
// Función de utilidad para convertir spawnflags_t a int de forma segura
inline int SafeConvertSpawnflags(const spawnflags_t& flags) {
	return static_cast<int>(static_cast<uint32_t>(flags));
}
struct powerup_info_t
{
	item_id_t item;
	gtime_t gclient_t::* time_ptr = nullptr;
	int32_t gclient_t::* count_ptr = nullptr;
} powerup_table[] = {
	{ IT_ITEM_QUAD, &gclient_t::quad_time },
	{ IT_ITEM_QUADFIRE, &gclient_t::quadfire_time },
	{ IT_ITEM_DOUBLE, &gclient_t::double_time },
	{ IT_ITEM_INVULNERABILITY, &gclient_t::invincible_time },
	{ IT_ITEM_INVISIBILITY, &gclient_t::invisible_time },
	{ IT_ITEM_ENVIROSUIT, &gclient_t::enviro_time },
	{ IT_ITEM_REBREATHER, &gclient_t::breather_time },
	{ IT_ITEM_IR_GOGGLES, &gclient_t::ir_time },
	{ IT_ITEM_SILENCER, nullptr, &gclient_t::silencer_shots }
};

/*
===============
G_SetStats
===============
*/
void G_SetStats(edict_t* ent)
{
	gitem_t* item;
	item_id_t index;
	int		  cells = 0;
	item_id_t power_armor_type;
	unsigned int invIndex;

	//
	// health
	//
	if (ent->s.renderfx & RF_USE_DISGUISE)
		ent->client->ps.stats[STAT_HEALTH_ICON] = level.disguise_icon;
	else
		ent->client->ps.stats[STAT_HEALTH_ICON] = level.pic_health;
	ent->client->ps.stats[STAT_HEALTH] = ent->health;

	if (ctfgame.election != ELECT_NONE) { 
		ent->client->ps.stats[STAT_VOTESTRING] = CONFIG_VOTE_INFO;
	}
	else {
		ent->client->ps.stats[STAT_VOTESTRING] = 0;
	}

	// Horde Status
	if (g_horde->integer && gi.get_configstring(CONFIG_HORDEMSG)[0] != '\0') {
		ent->client->ps.stats[STAT_HORDEMSG] = CONFIG_HORDEMSG;
	}
	else {
		ent->client->ps.stats[STAT_HORDEMSG] = 0;
	}
	//
	// weapons
	//
	uint32_t weaponbits = 0;

	for (invIndex = IT_WEAPON_GRAPPLE; invIndex <= IT_WEAPON_DISRUPTOR; invIndex++)
	{
		if (ent->client->pers.inventory[invIndex])
		{
			weaponbits |= 1 << GetItemByIndex((item_id_t)invIndex)->weapon_wheel_index;
		}
	}

	ent->client->ps.stats[STAT_WEAPONS_OWNED_1] = (weaponbits & 0xFFFF);
	ent->client->ps.stats[STAT_WEAPONS_OWNED_2] = (weaponbits >> 16);

	ent->client->ps.stats[STAT_ACTIVE_WHEEL_WEAPON] = (ent->client->newweapon ? ent->client->newweapon->weapon_wheel_index :
		ent->client->pers.weapon ? ent->client->pers.weapon->weapon_wheel_index :
		-1);
	ent->client->ps.stats[STAT_ACTIVE_WEAPON] = ent->client->pers.weapon ? ent->client->pers.weapon->weapon_wheel_index : -1;

	//
	// ammo
	//
	ent->client->ps.stats[STAT_AMMO_ICON] = 0;
	ent->client->ps.stats[STAT_AMMO] = 0;

	// Special case for blaster - show Vortex-style ammo counter
	if (ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_BLASTER)
	{
		// Use cells icon for blaster ammo display (energy weapon)
		//ent->client->ps.stats[STAT_AMMO_ICON] = gi.imageindex("a_blaster");
		ent->client->ps.stats[STAT_AMMO_ICON] = gi.imageindex("a_cells");
		ent->client->ps.stats[STAT_AMMO] = ent->client->blaster_ammo;
	}
	else if (ent->client->pers.weapon && ent->client->pers.weapon->ammo)
	{
		item = GetItemByIndex(ent->client->pers.weapon->ammo);

		if (!G_CheckInfiniteAmmo(item))
		{
			ent->client->ps.stats[STAT_AMMO_ICON] = gi.imageindex(item->icon);
			ent->client->ps.stats[STAT_AMMO] = ent->client->pers.inventory[ent->client->pers.weapon->ammo];
		}
	}

	memset(&ent->client->ps.stats[STAT_AMMO_INFO_START], 0, sizeof(uint16_t) * NUM_AMMO_STATS);
	for (unsigned int ammoIndex = AMMO_BULLETS; ammoIndex < AMMO_MAX; ++ammoIndex)
	{
		gitem_t* ammo = GetItemByAmmo((ammo_t)ammoIndex);
		uint16_t const val = G_CheckInfiniteAmmo(ammo) ? AMMO_VALUE_INFINITE : clamp(ent->client->pers.inventory[ammo->id], 0, AMMO_VALUE_INFINITE - 1);
		G_SetAmmoStat((uint16_t*)&ent->client->ps.stats[STAT_AMMO_INFO_START], ammo->ammo_wheel_index, val);
	}

	//
	// armor
	//
	power_armor_type = PowerArmorType(ent);
	if (power_armor_type)
		cells = ent->client->pers.inventory[IT_AMMO_CELLS];

	index = ArmorIndex(ent);
	if (power_armor_type && (!index || (level.time.milliseconds() % 3000) < 1500))
	{ 
		ent->client->ps.stats[STAT_ARMOR_ICON] = power_armor_type == IT_ITEM_POWER_SHIELD ? gi.imageindex("i_powershield") : gi.imageindex("i_powerscreen");
		ent->client->ps.stats[STAT_ARMOR] = cells;
	}
	else if (index)
	{
		item = GetItemByIndex(index);
		ent->client->ps.stats[STAT_ARMOR_ICON] = gi.imageindex(item->icon);
		ent->client->ps.stats[STAT_ARMOR] = ent->client->pers.inventory[index];
	}
	else
	{
		ent->client->ps.stats[STAT_ARMOR_ICON] = 0;
		ent->client->ps.stats[STAT_ARMOR] = 0;
	}

	//
	// pickup message
	//
	if (level.time > ent->client->pickup_msg_time)
	{
		ent->client->ps.stats[STAT_PICKUP_ICON] = 0;
		ent->client->ps.stats[STAT_PICKUP_STRING] = 0;
	}

	// owned powerups
	memset(&ent->client->ps.stats[STAT_POWERUP_INFO_START], 0, sizeof(uint16_t) * NUM_POWERUP_STATS);
	for (unsigned int powerupIndex = POWERUP_SCREEN; powerupIndex < POWERUP_MAX; ++powerupIndex)
	{
		gitem_t* powerup = GetItemByPowerup((powerup_t)powerupIndex);
		uint16_t val;

		switch (powerup->id)
		{
		case IT_ITEM_POWER_SCREEN:
		case IT_ITEM_POWER_SHIELD:
			if (!ent->client->pers.inventory[powerup->id])
				val = 0;
			else if (ent->flags & FL_POWER_ARMOR)
				val = 2;
			else
				val = 1;
			break;
		case IT_ITEM_FLASHLIGHT:
			if (!ent->client->pers.inventory[powerup->id])
				val = 0;
			else if (ent->flags & FL_FLASHLIGHT)
				val = 2;
			else
				val = 1;
			break;
		default:
			val = clamp(ent->client->pers.inventory[powerup->id], 0, 3);
			break;
		}

		G_SetPowerupStat((uint16_t*)&ent->client->ps.stats[STAT_POWERUP_INFO_START],
			powerup->powerup_wheel_index, val);
	}

	ent->client->ps.stats[STAT_TIMER_ICON] = 0;
	ent->client->ps.stats[STAT_TIMER] = 0;

	struct active_powerup_t {
		const powerup_info_t* info; // Can be null if it's a sphere
		int16_t timer_value;
		const char* icon;
		bool is_sphere;
	};

	// Use static thread_local to avoid per-frame heap allocations
	static thread_local std::vector<active_powerup_t> active_powerups;
	active_powerups.clear();
	if (active_powerups.capacity() < 8)
		active_powerups.reserve(8);

	for (auto& powerup_entry : powerup_table) // Iterate through the global powerup_table
	{
		const auto* const powerup_time = powerup_entry.time_ptr ? &(ent->client->*(powerup_entry.time_ptr)) : nullptr;
		const auto* const powerup_count = powerup_entry.count_ptr ? &(ent->client->*(powerup_entry.count_ptr)) : nullptr;

		if ((powerup_time && *powerup_time > level.time) ||
			(powerup_count && *powerup_count > 0))
		{
			active_powerup_t ap = {};
			ap.info = &powerup_entry;
            ap.is_sphere = false;

			if (powerup_entry.count_ptr)
				ap.timer_value = (ent->client->*(powerup_entry.count_ptr));
			else
				ap.timer_value = static_cast<int16_t>(ceil((ent->client->*(powerup_entry.time_ptr) - level.time).seconds()));

			const gitem_t* const item_def = GetItemByIndex(powerup_entry.item);
			if (item_def)
				ap.icon = item_def->icon;
            else
                ap.icon = "i_fixme"; // Fallback icon

			active_powerups.push_back(ap);
		}
	}

	if (ent->client->owned_sphere)
	{
		active_powerup_t sphere_ap = {};
        sphere_ap.info = nullptr; // Not from powerup_table
		sphere_ap.is_sphere = true;
		sphere_ap.timer_value = static_cast<int16_t>(ceil(ent->client->owned_sphere->wait - level.time.seconds()));

		if (ent->client->owned_sphere->spawnflags.has(SPHERE_DEFENDER))
			sphere_ap.icon = "p_defender";
		else if (ent->client->owned_sphere->spawnflags.has(SPHERE_HUNTER))
			sphere_ap.icon = "p_hunter";
		else if (ent->client->owned_sphere->spawnflags.has(SPHERE_VENGEANCE))
			sphere_ap.icon = "p_vengeance";
		else {
			sphere_ap.icon = "i_fixme";
			// gi.Com_PrintFmt("Warning: Unknown sphere spawnflags {}\n",
			//	SafeConvertSpawnflags(ent->client->owned_sphere->spawnflags));
		}
		active_powerups.push_back(sphere_ap);
	}

	if (!active_powerups.empty())
	{
		std::sort(active_powerups.begin(), active_powerups.end(),
			[](const active_powerup_t& a, const active_powerup_t& b) {
				return a.timer_value < b.timer_value;
			});

		const bool should_alternate = ((level.time.milliseconds() % 3000) < 1500);
		active_powerup_t* display_powerup = nullptr;

		if (active_powerups.size() > 1)
		{
			display_powerup = should_alternate ? &active_powerups[1] : &active_powerups[0];
		}
		else
		{
			display_powerup = &active_powerups[0];
		}

		if (display_powerup && display_powerup->icon)
		{
			ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex(display_powerup->icon);
			ent->client->ps.stats[STAT_TIMER] = display_powerup->timer_value;
		}
	}
	// PGM (End of powerup timer modification)

	//
	// selected item
	//
	ent->client->ps.stats[STAT_SELECTED_ITEM] = ent->client->pers.selected_item;

	if (ent->client->pers.selected_item == IT_NULL)
		ent->client->ps.stats[STAT_SELECTED_ICON] = 0;
	else
	{
		ent->client->ps.stats[STAT_SELECTED_ICON] = gi.imageindex(itemlist[ent->client->pers.selected_item].icon);

		if (ent->client->pers.selected_item_time < level.time)
			ent->client->ps.stats[STAT_SELECTED_ITEM_NAME] = 0;
	}

	//
	// layouts
	//
	ent->client->ps.stats[STAT_LAYOUTS] = 0;

	if (G_IsDeathmatch())
	{
		if (ent->client->pers.health <= 0 || level.intermissiontime || ent->client->showscores)
			ent->client->ps.stats[STAT_LAYOUTS] |= LAYOUTS_LAYOUT;
		if (ent->client->showinventory && ent->client->pers.health > 0)
			ent->client->ps.stats[STAT_LAYOUTS] |= LAYOUTS_INVENTORY;
	}
	else
	{
		if (ent->client->showscores || ent->client->showhelp || ent->client->showeou)
			ent->client->ps.stats[STAT_LAYOUTS] |= LAYOUTS_LAYOUT;
		if (ent->client->showinventory && ent->client->pers.health > 0)
			ent->client->ps.stats[STAT_LAYOUTS] |= LAYOUTS_INVENTORY;

		if (ent->client->showhelp)
			ent->client->ps.stats[STAT_LAYOUTS] |= LAYOUTS_HELP;
	}

	if (level.intermissiontime || ent->client->awaiting_respawn)
	{
		if (ent->client->awaiting_respawn || (level.intermission_eou || level.is_n64 || (G_IsDeathmatch() && level.intermissiontime)))
			ent->client->ps.stats[STAT_LAYOUTS] |= LAYOUTS_HIDE_HUD;

		if (level.intermission_eou || level.is_n64 || (G_IsDeathmatch() && level.intermissiontime))
			ent->client->ps.stats[STAT_LAYOUTS] |= LAYOUTS_INTERMISSION;
	}

	if (level.story_active)
		ent->client->ps.stats[STAT_LAYOUTS] |= LAYOUTS_HIDE_CROSSHAIR;
	else
		ent->client->ps.stats[STAT_LAYOUTS] &= ~LAYOUTS_HIDE_CROSSHAIR;

	// [Paril-KEX] key display
	if (!G_IsDeathmatch())
	{
		int32_t key_offset = 0;
		player_stat_t stat = STAT_KEY_A;

		ent->client->ps.stats[STAT_KEY_A] =
			ent->client->ps.stats[STAT_KEY_B] =
			ent->client->ps.stats[STAT_KEY_C] = 0;

		// Use static thread_local to avoid per-frame heap allocations
		static thread_local std::vector<item_id_t> keys_held;
		keys_held.clear();
		if (keys_held.capacity() < 10)
			keys_held.reserve(10);

		for (size_t i = 0; i < IT_TOTAL; ++i) // Iterate using index for global C array
		{
            const gitem_t& current_item = itemlist[i];
			if (!(current_item.flags & IF_KEY))
				continue;
			else if (!ent->client->pers.inventory[current_item.id])
				continue;

			keys_held.push_back(current_item.id);
		}

		if (!keys_held.empty()) // Proceed only if keys are held
		{
			if (keys_held.size() > 3)
				key_offset = (int32_t)(level.time.seconds() / 5);

			for (size_t i = 0; i < std::min(keys_held.size(), (size_t)3); i++, stat = (player_stat_t)(stat + 1))
			{
				size_t key_index_to_display = (i + key_offset) % keys_held.size();
				ent->client->ps.stats[stat] = gi.imageindex(GetItemByIndex(keys_held[key_index_to_display])->icon);
			}
		}
	}

	//
	// frags
	//
	ent->client->ps.stats[STAT_FRAGS] = ent->client->resp.score;
	ent->client->ps.stats[STAT_SPREE] = ent->client->resp.spree;

	//
	// help icon / current weapon if not shown
	//
	if (ent->client->pers.helpchanged >= 1 && ent->client->pers.helpchanged <= 2 && (level.time.milliseconds() % 1000) < 500) 
		ent->client->ps.stats[STAT_HELPICON] = gi.imageindex("i_help");
	else if ((ent->client->pers.hand == CENTER_HANDED) && ent->client->pers.weapon)
		ent->client->ps.stats[STAT_HELPICON] = gi.imageindex(ent->client->pers.weapon->icon);
	else
		ent->client->ps.stats[STAT_HELPICON] = 0;

	ent->client->ps.stats[STAT_SPECTATOR] = 0;

	for (size_t i = 0; i < MAX_HEALTH_BARS; i++)
	{
		byte* health_byte = reinterpret_cast<byte*>(&ent->client->ps.stats[STAT_HEALTH_BARS]) + i;

		if (!level.health_bar_entities[i])
			*health_byte = 0;
		else if (level.health_bar_entities[i]->timestamp)
		{
			if (level.health_bar_entities[i]->timestamp < level.time)
			{
				level.health_bar_entities[i] = nullptr;
				*health_byte = 0;
				continue;
			}
			*health_byte = 0b10000000;
		}
		else
		{
            // --- FIX: Add a null check for the 'enemy' pointer ---
			if (!level.health_bar_entities[i]->enemy || !level.health_bar_entities[i]->enemy->inuse || level.health_bar_entities[i]->enemy->health <= 0)
			{
				if (level.health_bar_entities[i]->enemy && level.health_bar_entities[i]->enemy->monsterinfo.aiflags & AI_DOUBLE_TROUBLE)
				{
					*health_byte = 0b10000000;
					continue;
				}

				if (level.health_bar_entities[i]->delay)
				{
					level.health_bar_entities[i]->timestamp = level.time + gtime_t::from_sec(level.health_bar_entities[i]->delay);
					*health_byte = 0b10000000;
				}
				else
				{
					level.health_bar_entities[i] = nullptr;
					*health_byte = 0;
				}
				continue;
			}
			else if (level.health_bar_entities[i]->spawnflags.has(SPAWNFLAG_HEALTHBAR_PVS_ONLY) && !gi.inPVS(ent->s.origin, level.health_bar_entities[i]->enemy->s.origin, true))
			{
				*health_byte = 0;
				continue;
			}

			float health_remaining = ((float)level.health_bar_entities[i]->enemy->health) / level.health_bar_entities[i]->enemy->max_health;
			*health_byte = ((byte)(health_remaining * 0b01111111)) | 0b10000000;
		}
	}
     void SetIDView(edict_t * ent); // Declaration moved up for clarity
	
	if (ent->client->pers.id_state && (ent->svflags & SVF_PLAYER)) // Removed !(ent->svflags & SVF_BOT) as per original
			SetIDView(ent);
	else
	{
		ent->client->ps.stats[STAT_TARGET_HEALTH_STRING] = 0;
	}

	// Network optimization: Only update STAT_ID_DAMAGE if value actually changed
	int new_damage_stat = 0;
	if (level.time <= ent->client->lastdmg + 1.55_sec && g_iddmg->integer &&
	    ent->client->pers.iddmg_state && (ent->svflags & SVF_PLAYER) && !(ent->svflags & SVF_BOT)) {
		new_damage_stat = static_cast<int>(std::min<uint64_t>(ent->client->dmg_counter, INT_MAX));
	}

	// Only update if changed (prevents network spam)
	if (ent->client->ps.stats[STAT_ID_DAMAGE] != new_damage_stat) {
		ent->client->ps.stats[STAT_ID_DAMAGE] = new_damage_stat;
	}


	// ZOID
	SetCTFStats(ent);
	// ZOID
}

/*
===============
G_CheckChaseStats
===============
*/
void G_CheckChaseStats(edict_t* ent)
{
	gclient_t* cl;

	for (uint32_t i = 1; i <= game.maxclients; i++)
	{
		cl = g_edicts[i].client;
		if (!g_edicts[i].inuse || cl->chase_target != ent)
			continue;
		cl->ps.stats = ent->client->ps.stats;
		G_SetSpectatorStats(g_edicts + i);
	}
}

/*
===============
G_SetSpectatorStats
===============
*/
void G_SetSpectatorStats(edict_t* ent)
{
	gclient_t* cl = ent->client;

	if (!cl->chase_target)
		G_SetStats(ent);

	cl->ps.stats[STAT_SPECTATOR] = 1;

	// layouts are independant in spectator
	cl->ps.stats[STAT_LAYOUTS] = 0;
	if (cl->pers.health <= 0 || level.intermissiontime || cl->showscores)
		cl->ps.stats[STAT_LAYOUTS] |= LAYOUTS_LAYOUT;
	if (cl->showinventory && cl->pers.health > 0)
		cl->ps.stats[STAT_LAYOUTS] |= LAYOUTS_INVENTORY;

	if (cl->chase_target && cl->chase_target->inuse)
		// Q2Eaks fix bugged chasecam name showing name\model/skin\tag
		//cl->ps.stats[STAT_CHASE] = CS_PLAYERSKINS +
		//						   (cl->chase_target - g_edicts) - 1;
		cl->ps.stats[STAT_CHASE] = cl->chase_target - g_edicts;
	else
		cl->ps.stats[STAT_CHASE] = 0;
}
