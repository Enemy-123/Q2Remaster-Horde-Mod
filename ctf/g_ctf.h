// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#pragma once

#include "../g_local.h" // Include base types like edict_t, gclient_t, gtime_t, etc. BEFORE they are used here.

constexpr const char* CTF_VERSION_STRING = "1.52";

// --- Enums Moved from g_ctf.cpp ---

enum ctfteam_t
{
	CTF_NOTEAM,
	CTF_TEAM1,
	CTF_TEAM2
};

enum ctfgrapplestate_t
{
	CTF_GRAPPLE_STATE_FLY,
	CTF_GRAPPLE_STATE_PULL,
	CTF_GRAPPLE_STATE_HANG
};

// Moved from g_ctf.cpp for horde_menu.cpp
enum elect_t
{
	ELECT_NONE,
	ELECT_MAP,
	ELECT_TIME // Added for time extension vote in g_ctf.cpp
};

// Moved from g_ctf.cpp for CTFObserver logic
enum match_t
{
	MATCH_NONE,
	MATCH_SETUP,
	MATCH_PREGAME,
	MATCH_GAME,
	MATCH_POST
};


// --- Structs Moved/Defined Here ---

// Moved from g_ctf.cpp for horde_menu.cpp
struct ghost_t
{
	char netname[MAX_NETNAME];
	int	 number;

	// stats
	int deaths;
	int kills;
	int caps;
	int basedef;
	int carrierdef;

	int		 code;	// ghost code
	ctfteam_t		 team;	// team
	int		 score; // frags at time of disconnect
	edict_t* ent;
};


// Moved from g_ctf.cpp for horde_menu.cpp (Needed for extern ctfgame)
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

// --- Extern Declarations ---

// Declare ctfgame as extern (definition is in g_ctf.cpp)
extern ctfgame_t ctfgame;

// CVars used elsewhere
extern cvar_t* ctf;
extern cvar_t* g_teamplay_force_join;
extern cvar_t* teamplay;
extern cvar_t* capturelimit; // Used in CTFCheckRules

// --- Constants ---

constexpr const char* CTF_TEAM1_SKIN = "ctf_r";
constexpr const char* CTF_TEAM2_SKIN = "ctf_b";

constexpr int32_t CTF_CAPTURE_BONUS = 15;
constexpr int32_t CTF_TEAM_BONUS = 10;
constexpr int32_t CTF_RECOVERY_BONUS = 1;
constexpr int32_t CTF_FLAG_BONUS = 0;
constexpr int32_t CTF_FRAG_CARRIER_BONUS = 2;
constexpr gtime_t CTF_FLAG_RETURN_TIME = 40_sec;

constexpr int32_t CTF_CARRIER_DANGER_PROTECT_BONUS = 2;
constexpr int32_t CTF_CARRIER_PROTECT_BONUS = 1;
constexpr int32_t CTF_FLAG_DEFENSE_BONUS = 1;
constexpr int32_t CTF_RETURN_FLAG_ASSIST_BONUS = 1;
constexpr int32_t CTF_FRAG_CARRIER_ASSIST_BONUS = 2;

constexpr float CTF_TARGET_PROTECT_RADIUS = 400;
constexpr float CTF_ATTACKER_PROTECT_RADIUS = 400;

constexpr gtime_t CTF_CARRIER_DANGER_PROTECT_TIMEOUT = 8_sec;
constexpr gtime_t CTF_FRAG_CARRIER_ASSIST_TIMEOUT = 10_sec;
constexpr gtime_t CTF_RETURN_FLAG_ASSIST_TIMEOUT = 10_sec;

constexpr gtime_t CTF_AUTO_FLAG_RETURN_TIMEOUT = 30_sec;

constexpr gtime_t CTF_TECH_TIMEOUT = 45_sec; // Also used by horde_menu

constexpr int32_t CTF_DEFAULT_GRAPPLE_SPEED = 650;
constexpr float	  CTF_DEFAULT_GRAPPLE_PULL_SPEED = 650;

// --- Function Declarations ---

void CTFInit();
void CTFSpawn();
void CTFPrecache();
bool G_TeamplayEnabled();
void G_AdjustTeamScore(ctfteam_t team, int32_t offset);

void SP_info_player_team1(edict_t* self);
void SP_info_player_team2(edict_t* self);

const char* CTFTeamName(int team);
const char* CTFOtherTeamName(int team);
void		CTFAssignSkin(edict_t* ent, const char* s);
void		CTFAssignTeam(gclient_t* who);
edict_t* SelectCTFSpawnPoint(edict_t* ent, bool force_spawn);
bool		CTFPickup_Flag(edict_t* ent, edict_t* other);
void		CTFDrop_Flag(edict_t* ent, gitem_t* item);
void		CTFEffects(edict_t* player);
void		CTFCalcScores();
void		CTFCalcRankings(std::array<uint32_t, MAX_CLIENTS>& player_ranks);
void		CheckEndTDMLevel();
void		SetCTFStats(edict_t* ent);
void		CTFDeadDropFlag(edict_t* self);
void		CTFScoreboardMessage(edict_t* ent, edict_t* killer);
void		CTFTeam_f(edict_t* ent);
void		CTFID_f(edict_t* ent);
void		DMGID_f(edict_t* ent);
#ifndef KEX_Q2_GAME
void		CTFSay_Team(edict_t* who, const char* msg);
#endif
void		CTFFlagSetup(edict_t* ent);
void		CTFResetFlag(int ctf_team);
void		CTFFragBonuses(edict_t* targ, edict_t* inflictor, edict_t* attacker);
void		CTFCheckHurtCarrier(edict_t* targ, edict_t* attacker);
void        CTFDirtyTeamMenu();

// GRAPPLE
void CTFWeapon_Grapple(edict_t* ent);
void CTFPlayerResetGrapple(edict_t* ent);
void CTFGrapplePull(edict_t* self);
void CTFResetGrapple(edict_t* self);

// TECH
gitem_t* CTFWhat_Tech(edict_t* ent);
bool	 CTFPickup_Tech(edict_t* ent, edict_t* other);
void	 CTFDrop_Tech(edict_t* ent, gitem_t* item);
void	 CTFDeadDropTech(edict_t* ent);
void	 CTFSetupTechSpawn();
int		 CTFApplyResistance(edict_t* ent, int dmg); // Also needed for horde_menu Tech handler sound
int		 CTFApplyStrength(edict_t* ent, int dmg);
bool	 CTFApplyStrengthSound(edict_t* ent);       // Also needed for horde_menu Tech handler sound
bool	 CTFApplyHaste(edict_t* ent);
void	 CTFApplyHasteSound(edict_t* ent);          // Also needed for horde_menu Tech handler sound
void	 CTFApplyRegeneration(edict_t* ent);        // Also needed for horde_menu Tech handler sound
bool	 CTFHasRegeneration(edict_t* ent);
void	 CTFRespawnTech(edict_t* ent);
void	 CTFResetTech();

// --- Declarations specifically needed for horde_menu.cpp ---
//void ShowInventory(edict_t* ent);                   // For MainMenu_Handler
void CTFJoinTeam(edict_t* ent, ctfteam_t desired_team); // For TechMenu_Handler
bool CTFBeginElection(edict_t* ent, elect_t type, const char* msg); // For MapVote_Handler
void RemoveAllTechItems(edict_t* ent);              // For TechMenu_Handler
// CTFObserver, CTFVoteYes, CTFVoteNo are already declared below

// --- Other CTF Functions (Mostly keep as is) ---
void RemoveTech(edict_t* ent);
void OpenTechMenu(edict_t* ent);
void HordeOpenJoinMenu(edict_t* ent);
void HordeUpdateJoinMenu(edict_t* ent);
bool CTFStartClient(edict_t* ent);
void CTFVoteYes(edict_t* ent); // Needed for MainMenu_Handler
void CTFVoteNo(edict_t* ent);  // Needed for MainMenu_Handler
void CTFReady(edict_t* ent);
void CTFNotReady(edict_t* ent);
bool CTFNextMap();
bool CTFMatchSetup();
bool CTFMatchOn();
void CTFGhost(edict_t* ent);
void CTFAdmin(edict_t* ent);
bool CTFInMatch();
void CTFStats(edict_t* ent);
void CTFWarp(edict_t* ent, const char* map_name);
void CTFBoot(edict_t* ent);
void CTFPlayerList(edict_t* ent);

bool CTFCheckRules();

void SP_misc_ctf_banner(edict_t* ent);
void SP_misc_ctf_small_banner(edict_t* ent);

void UpdateChaseCam(edict_t* ent);
void ChaseNext(edict_t* ent);
void ChasePrev(edict_t* ent);

void CTFObserver(edict_t* ent); // Needed for MainMenu_Handler

void SP_trigger_teleport(edict_t* ent);
void SP_info_teleport_destination(edict_t* ent);

void CTFSetPowerUpEffect(edict_t* ent, effects_t def);