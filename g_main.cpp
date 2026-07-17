// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"
#include "bots/bot_includes.h"
#include "memory_safety.h"

CHECK_GCLIENT_INTEGRITY;
CHECK_EDICT_INTEGRITY;

// AddressSanitizer default options, baked into the DLL so they apply regardless of
// how the game is launched (F5, Steam, or manual + attach) without depending on the
// ASAN_OPTIONS env var. detect_container_overflow=0 is required because jsoncpp.lib is
// linked without ASan container annotations (see _DISABLE_*_ANNOTATION in game.vcxproj);
// with the runtime check left on, ASan misreads std::format's internal _Fmt_buffer and
// crashes (verify_double_ended_contiguous_container). A real ASAN_OPTIONS env var still
// overrides these per-key.
#ifdef __SANITIZE_ADDRESS__
extern "C" __declspec(dllexport) const char *__asan_default_options()
{
	return "detect_container_overflow=0:halt_on_error=1:abort_on_error=0";
}
#endif

std::mt19937 mt_rand;

game_locals_t  game;
level_locals_t level;

local_game_import_t  gi;

/*static*/ char local_game_import_t::print_buffer[0x10000];

/*static*/ std::array<char[MAX_INFO_STRING], MAX_LOCALIZATION_ARGS> local_game_import_t::buffers;
/*static*/ std::array<const char*, MAX_LOCALIZATION_ARGS> local_game_import_t::buffer_ptrs;

game_export_t  globals;

cached_modelindex		sm_meat_index;
cached_soundindex		snd_fry;

edict_t* g_edicts;

cvar_t* developer;
cvar_t* deathmatch;
cvar_t* coop;
cvar_t* skill;
cvar_t* fraglimit;
cvar_t* timelimit;
// ZOID
cvar_t* capturelimit;
cvar_t* g_quick_weapon_switch;
cvar_t* g_instant_weapon_switch;
// ZOID
cvar_t* password;
cvar_t* spectator_password;
cvar_t* needpass;
cvar_t* maxclients;
cvar_t* maxspectators;
static cvar_t* maxentities;
cvar_t* g_select_empty;
cvar_t* sv_dedicated;

cvar_t* filterban;

cvar_t* sv_maxvelocity;
cvar_t* sv_gravity;

cvar_t* g_skipViewModifiers;

cvar_t* sv_rollspeed;
cvar_t* sv_rollangle;
cvar_t* gun_x;
cvar_t* gun_y;
cvar_t* gun_z;

cvar_t* run_pitch;
cvar_t* run_roll;
cvar_t* bob_up;
cvar_t* bob_pitch;
cvar_t* bob_roll;

cvar_t* sv_cheats;

cvar_t* g_debug_monster_paths;
cvar_t* g_debug_monster_kills;
cvar_t* g_debug_poi;

cvar_t* bot_debug_follow_actor;
cvar_t* bot_debug_move_to_point;

cvar_t* flood_msgs;
cvar_t* flood_persecond;
cvar_t* flood_waitdelay;

cvar_t* sv_stopspeed; // PGM	 (this was a define in g_phys.c)

cvar_t* g_strict_saves;

// ROGUE cvars
cvar_t* gamerules;
cvar_t* huntercam;
cvar_t* g_dm_strong_mines;
cvar_t* g_dm_random_items;
// ROGUE

// [Kex]
cvar_t* g_instagib;
cvar_t* g_coop_player_collision;
cvar_t* g_coop_squad_respawn;
cvar_t* g_coop_enable_lives;
cvar_t* g_coop_num_lives;
cvar_t* g_coop_damage_respawn_time;
cvar_t* g_coop_bad_area_time;
cvar_t* g_coop_instanced_items;
cvar_t* g_allow_grapple;
cvar_t* g_grapple_fly_speed;
cvar_t* g_grapple_pull_speed;
cvar_t* g_grapple_damage;
cvar_t* g_coop_health_scaling;
cvar_t* g_weapon_respawn_time;

// Horde"flags"
cvar_t* g_easymonsters;
cvar_t* g_iddmg;
//cvar_t* g_autohaste;
cvar_t* dm_monsters;
//cvar_t* g_upgradeproxs;
//cvar_t* g_piercingbeam;
cvar_t* g_dm_spawns;
cvar_t* sv_eyecam;
cvar_t* sv_target_id;
cvar_t* g_no_self_damage;
//dm"flags"
cvar_t* g_no_health;
cvar_t* g_no_items;
cvar_t* g_dm_weapons_stay;
cvar_t* g_dm_no_fall_damage;
cvar_t* g_dm_instant_items;
cvar_t* g_dm_same_level;
cvar_t* g_friendly_fire;
cvar_t* g_dm_force_respawn;
cvar_t* g_dm_force_respawn_time;
cvar_t* g_dm_spawn_farthest;
cvar_t* g_no_armor;
cvar_t* g_dm_allow_exit;
cvar_t* g_infinite_ammo;
cvar_t* g_dm_no_quad_drop;
cvar_t* g_dm_no_quadfire_drop;
cvar_t* g_no_mines;
cvar_t* g_dm_no_stack_double;
cvar_t* g_no_nukes;
cvar_t* g_no_spheres;
cvar_t* g_teamplay_armor_protect;
cvar_t* g_allow_techs;
cvar_t* g_start_items;
cvar_t* g_map_list;
cvar_t* g_map_list_shuffle;
cvar_t* g_lag_compensation;

cvar_t* g_speedstuff;
cvar_t* g_mover_debug;

cvar_t* sv_airaccelerate;
cvar_t* g_damage_scale;
cvar_t* g_disable_player_collision;
cvar_t* ai_damage_scale;
cvar_t* ai_model_scale;
cvar_t* ai_allow_dm_spawn;
cvar_t* ai_movement_disabled;
cvar_t* g_monster_squeeze;        // target half-WIDTH a blocked monster shrinks its box down to, to fit a tight gap (0 = off; smaller = squeezes tighter)
cvar_t* g_monster_squeeze_height; // target total HEIGHT a blocked monster shrinks its box down to (0 = don't shrink height)

cvar_t* g_nolag; // Network optimization: convert gibs to temp entities

//HORDE STUFF
cvar_t* g_horde_profiler;
cvar_t* g_horde_tactical_spawn;
cvar_t* g_vortex;
cvar_t* g_spectator_teleport;
cvar_t* pvm; // PvM (Player vs Monster) mode

cvar_t* g_use_hook;
cvar_t* g_hook_wave;
cvar_t* g_special_key;
cvar_t* g_loadent;
cvar_t* g_chaotic;
cvar_t* g_insane;
cvar_t* g_swap_coop_monsters;
//cvar_t* g_ammoregen;
cvar_t* sv_wave_timer;
//cvar_t* g_tracedbullets;
//cvar_t* g_energyshells;
//cvar_t* g_bouncygl;
//cvar_t* g_bfgpull;
//cvar_t* g_bfgslide;
cvar_t* g_startarmor;
//cvar_t* g_vampire;

static cvar_t* g_frames_per_frame;

void SpawnEntities(const char* mapname, const char* entities, const char* spawnpoint);
void ClientThink(edict_t* ent, usercmd_t* cmd);
edict_t* ClientChooseSlot(const char* userinfo, const char* social_id, bool isBot, edict_t** ignore, size_t num_ignore, bool cinematic);
bool  ClientConnect(edict_t* ent, char* userinfo, const char* social_id, bool isBot);
char* WriteGameJson(bool autosave, size_t* out_size);
void  ReadGameJson(const char* jsonString);
char* WriteLevelJson(bool transition, size_t* out_size);
void  ReadLevelJson(const char* jsonString);
bool  G_CanSave();
void ClientDisconnect(edict_t* ent);
void ClientBegin(edict_t* ent);
void ClientCommand(edict_t* ent);
void G_RunFrame(bool main_loop);
void G_PrepFrame();
void InitSave();

#include <chrono>
#include "shared.h"
#include <span>

/*
============
PreInitGame

This will be called when the dll is first loaded, which
only happens when a new game is started or a save game
is loaded.
============
*/
void PreInitGame()
{
	developer = gi.cvar("developer", "0", CVAR_NOFLAGS);
	maxclients = gi.cvar("maxclients", G_Fmt("{}", MAX_SPLIT_PLAYERS).data(), CVAR_SERVERINFO | CVAR_LATCH);
	deathmatch = gi.cvar("deathmatch", "0", CVAR_LATCH);
	coop = gi.cvar("coop", "0", CVAR_LATCH);
	teamplay = gi.cvar("teamplay", "0", CVAR_LATCH);

	// ZOID
	CTFInit();
	// ZOID

		// Paril
	Horde_PreInit();

	// ZOID
	// This gamemode only supports deathmatch
	if (ctf->integer)
	{
		if (!deathmatch->integer)
		{
			gi.Com_Print("Forcing deathmatch.\n");
			gi.cvar_set("deathmatch", "1");
		}
		// force coop off
		if (coop->integer)
			gi.cvar_set("coop", "0");
		// force tdm off
		if (teamplay->integer)
			gi.cvar_set("teamplay", "0");
	}
	if (teamplay->integer)
	{
		if (!deathmatch->integer)
		{
			gi.Com_Print("Forcing deathmatch.\n");
			gi.cvar_set("deathmatch", "1");
		}
		// force coop off
		if (coop->integer)
			gi.cvar_set("coop", "0");
	}
	// ZOID
}

/*
============
InitGame

Called after PreInitGame when the game has set up cvars.
============
*/
void InitGame()
{
	gi.Com_Print("==== InitGame ====\n");

	// Load configuration from JSON
	cvar_t* gamedir = gi.cvar("game", "", CVAR_NOFLAGS);
	std::string basedir = gi.cvar("basedir", "", CVAR_NOFLAGS)->string;
	if (basedir.empty())
		basedir = ".";
	if (basedir.back() != '/' && basedir.back() != '\\')
		basedir += "/";
	if (gamedir && gamedir->string[0])
		basedir += std::string(gamedir->string) + "/";
	else
		basedir += "baseq2/";

	// IMPORTANT: Initialize monster type registry BEFORE loading configs
	// Config_LoadMonsters needs the registry to look up monster type IDs
	horde::InitializeHordeIDs();

	Config_Load(basedir.c_str());

	// Kyper - Lithium port
	g_use_hook = gi.cvar("g_use_hook", "1", CVAR_NOFLAGS);
	g_hook_wave = gi.cvar("g_hook_wave", "0", CVAR_NOFLAGS);
	g_special_key = gi.cvar("g_special_key", "1", CVAR_NOFLAGS);  // Default: 1=Barrels
	gi.cvar_set("hook_pullspeed", "1200");
	gi.cvar_set("hook_speed", "3000");
	gi.cvar_set("hook_sky", "1");

	Hook_InitGame();
	Barrel_InitGame();

	InitSave();

	// seed RNG
	mt_rand.seed((uint32_t)std::chrono::system_clock::now().time_since_epoch().count());

	gun_x = gi.cvar("gun_x", "0", CVAR_NOFLAGS);
	gun_y = gi.cvar("gun_y", "0", CVAR_NOFLAGS);
	gun_z = gi.cvar("gun_z", "0", CVAR_NOFLAGS);

	// FIXME: sv_ prefix is wrong for these
	sv_rollspeed = gi.cvar("sv_rollspeed", "200", CVAR_NOFLAGS);
	sv_rollangle = gi.cvar("sv_rollangle", "2", CVAR_NOFLAGS);
	sv_maxvelocity = gi.cvar("sv_maxvelocity", "4000", CVAR_NOFLAGS);
	sv_gravity = gi.cvar("sv_gravity", "800", CVAR_NOFLAGS);

	g_skipViewModifiers = gi.cvar("g_skipViewModifiers", "0", CVAR_NOSET);

	sv_stopspeed = gi.cvar("sv_stopspeed", "100", CVAR_NOFLAGS); // PGM - was #define in g_phys.c

	// ROGUE
	huntercam = gi.cvar("huntercam", "1", CVAR_SERVERINFO | CVAR_LATCH);
	g_dm_strong_mines = gi.cvar("g_dm_strong_mines", "0", CVAR_NOFLAGS);
	g_dm_random_items = gi.cvar("g_dm_random_items", "0", CVAR_NOFLAGS);
	// ROGUE

	// [Kex] Instagib
	g_instagib = gi.cvar("g_instagib", "0", CVAR_NOFLAGS);

	sv_eyecam = gi.cvar("sv_eyecam", "1", CVAR_NOFLAGS);
	g_no_self_damage = gi.cvar("g_no_self_damage", "1", CVAR_NOFLAGS);
	sv_target_id = gi.cvar("sv_target_id", "0", CVAR_NOFLAGS);

	// [Paril-KEX]
	g_coop_player_collision = gi.cvar("g_coop_player_collision", "0", CVAR_LATCH);
	g_coop_squad_respawn = gi.cvar("g_coop_squad_respawn", "1", CVAR_LATCH);
	g_coop_enable_lives = gi.cvar("g_coop_enable_lives", "0", CVAR_LATCH);
	g_coop_num_lives = gi.cvar("g_coop_num_lives", "2", CVAR_LATCH);
	// squad respawn timers in seconds; -1 = unset (fall back to lua config, then compiled default)
	g_coop_damage_respawn_time = gi.cvar("g_coop_damage_respawn_time", "-1", CVAR_NOFLAGS);
	g_coop_bad_area_time = gi.cvar("g_coop_bad_area_time", "-1", CVAR_NOFLAGS);
	g_coop_instanced_items = gi.cvar("g_coop_instanced_items", "1", CVAR_LATCH);
	g_allow_grapple = gi.cvar("g_allow_grapple", "auto", CVAR_NOFLAGS);
	// Initialize grapple cvars from g_config values (loaded from JSON)
	g_grapple_fly_speed = gi.cvar("g_grapple_fly_speed", G_Fmt("{}", g_config.grapple.fly_speed).data(), CVAR_NOFLAGS);
	g_grapple_pull_speed = gi.cvar("g_grapple_pull_speed", G_Fmt("{}", g_config.grapple.pull_speed).data(), CVAR_NOFLAGS);
	g_grapple_damage = gi.cvar("g_grapple_damage", G_Fmt("{}", g_config.grapple.damage).data(), CVAR_NOFLAGS);

	g_debug_monster_paths = gi.cvar("g_debug_monster_paths", "0", CVAR_NOFLAGS);
	g_debug_monster_kills = gi.cvar("g_debug_monster_kills", "0", CVAR_LATCH);
	g_debug_poi = gi.cvar("g_debug_poi", "0", CVAR_NOFLAGS);

	bot_debug_follow_actor = gi.cvar("bot_debug_follow_actor", "0", CVAR_NOFLAGS);
	bot_debug_move_to_point = gi.cvar("bot_debug_move_to_point", "0", CVAR_NOFLAGS);

	g_horde->integer
		? g_coop_squad_respawn = gi.cvar("g_coop_squad_respawn", "1", CVAR_LATCH)
		: g_coop_squad_respawn = gi.cvar("g_coop_squad_respawn", "0", CVAR_LATCH);

	// noset vars
	sv_dedicated = gi.cvar("dedicated", "0", CVAR_NOSET);

	// latched vars
	sv_cheats = gi.cvar("cheats",
#if defined(_DEBUG)
		"1"
#else
		"0"
#endif
		, CVAR_SERVERINFO | CVAR_LATCH);
	gi.cvar("gamename", GAMEVERSION, CVAR_SERVERINFO | CVAR_LATCH);

	maxspectators = gi.cvar("maxspectators", "4", CVAR_SERVERINFO);
	skill = gi.cvar("skill", "1", CVAR_LATCH);
	maxentities = gi.cvar("maxentities", G_Fmt("{}", MAX_EDICTS).data(), CVAR_LATCH);
	gamerules = gi.cvar("gamerules", "0", CVAR_LATCH); // PGM

	// change anytime vars
	fraglimit = gi.cvar("fraglimit", "0", CVAR_SERVERINFO);
	timelimit = gi.cvar("timelimit", "0", CVAR_SERVERINFO);
	// ZOID
	capturelimit = gi.cvar("capturelimit", "0", CVAR_SERVERINFO);
	g_quick_weapon_switch = gi.cvar("g_quick_weapon_switch", "1", CVAR_LATCH);
	g_instant_weapon_switch = gi.cvar("g_instant_weapon_switch", "0", CVAR_LATCH);
	// ZOID
	password = gi.cvar("password", "", CVAR_USERINFO);
	spectator_password = gi.cvar("spectator_password", "", CVAR_USERINFO);
	needpass = gi.cvar("needpass", "0", CVAR_SERVERINFO);
	filterban = gi.cvar("filterban", "1", CVAR_NOFLAGS);

	g_select_empty = gi.cvar("g_select_empty", "0", CVAR_ARCHIVE);

	run_pitch = gi.cvar("run_pitch", "0.002", CVAR_NOFLAGS);
	run_roll = gi.cvar("run_roll", "0.005", CVAR_NOFLAGS);
	bob_up = gi.cvar("bob_up", "0.005", CVAR_NOFLAGS);
	bob_pitch = gi.cvar("bob_pitch", "0.002", CVAR_NOFLAGS);
	bob_roll = gi.cvar("bob_roll", "0.002", CVAR_NOFLAGS);

	// flood control
	flood_msgs = gi.cvar("flood_msgs", "4", CVAR_NOFLAGS);
	flood_persecond = gi.cvar("flood_persecond", "4", CVAR_NOFLAGS);
	flood_waitdelay = gi.cvar("flood_waitdelay", "10", CVAR_NOFLAGS);

	g_strict_saves = gi.cvar("g_strict_saves", "1", CVAR_NOFLAGS);

	sv_airaccelerate = gi.cvar("sv_airaccelerate", "0", CVAR_NOFLAGS);

	g_damage_scale = gi.cvar("g_damage_scale", "1", CVAR_NOFLAGS);
	g_disable_player_collision = gi.cvar("g_disable_player_collision", "0", CVAR_NOFLAGS);
	ai_damage_scale = gi.cvar("ai_damage_scale", "1", CVAR_NOFLAGS);
	ai_model_scale = gi.cvar("ai_model_scale", "0", CVAR_NOFLAGS);
	ai_allow_dm_spawn = gi.cvar("ai_allow_dm_spawn", "1", CVAR_NOFLAGS);
	ai_movement_disabled = gi.cvar("ai_movement_disabled", "0", CVAR_NOFLAGS);
	g_monster_squeeze = gi.cvar("g_monster_squeeze", "8", CVAR_NOFLAGS);
	g_monster_squeeze_height = gi.cvar("g_monster_squeeze_height", "56", CVAR_NOFLAGS);

	g_nolag = gi.cvar("g_nolag", "0", CVAR_NOFLAGS);

	g_frames_per_frame = gi.cvar("g_frames_per_frame", "1", CVAR_NOFLAGS);

	g_coop_health_scaling = gi.cvar("g_coop_health_scaling", "0", CVAR_LATCH);
	g_weapon_respawn_time = gi.cvar("g_weapon_respawn_time", "30", CVAR_NOFLAGS);

	// dm "flags"
	g_no_health = gi.cvar("g_no_health", "0", CVAR_NOFLAGS);
	g_no_items = gi.cvar("g_no_items", "0", CVAR_NOFLAGS);
	g_dm_weapons_stay = gi.cvar("g_dm_weapons_stay", "0", CVAR_NOFLAGS);
	g_dm_no_fall_damage = gi.cvar("g_dm_no_fall_damage", "0", CVAR_NOFLAGS);
	g_dm_instant_items = gi.cvar("g_dm_instant_items", "1", CVAR_NOFLAGS);
	g_dm_same_level = gi.cvar("g_dm_same_level", "0", CVAR_NOFLAGS);
	g_friendly_fire = gi.cvar("g_friendly_fire", "0", CVAR_NOFLAGS);
	g_dm_force_respawn = gi.cvar("g_dm_force_respawn", "0", CVAR_NOFLAGS);
	g_dm_force_respawn_time = gi.cvar("g_dm_force_respawn_time", "0", CVAR_NOFLAGS);
	g_dm_spawn_farthest = gi.cvar("g_dm_spawn_farthest", "1", CVAR_NOFLAGS);
	g_no_armor = gi.cvar("g_no_armor", "0", CVAR_NOFLAGS);
	g_dm_allow_exit = gi.cvar("g_dm_allow_exit", "0", CVAR_NOFLAGS);
	g_infinite_ammo = gi.cvar("g_infinite_ammo", "0", CVAR_LATCH);
	g_dm_no_quad_drop = gi.cvar("g_dm_no_quad_drop", "0", CVAR_NOFLAGS);
	g_dm_no_quadfire_drop = gi.cvar("g_dm_no_quadfire_drop", "0", CVAR_NOFLAGS);
	g_no_mines = gi.cvar("g_no_mines", "0", CVAR_NOFLAGS);
	g_dm_no_stack_double = gi.cvar("g_dm_no_stack_double", "0", CVAR_NOFLAGS);
	g_no_nukes = gi.cvar("g_no_nukes", "0", CVAR_NOFLAGS);
	g_no_spheres = gi.cvar("g_no_spheres", "0", CVAR_NOFLAGS);
	g_teamplay_force_join = gi.cvar("g_teamplay_force_join", "0", CVAR_NOFLAGS);
	g_teamplay_armor_protect = gi.cvar("g_teamplay_armor_protect", "0", CVAR_NOFLAGS);
	g_allow_techs = gi.cvar("g_allow_techs", "auto", CVAR_NOFLAGS);

	g_horde_profiler = gi.cvar("g_horde_profiler", "0", CVAR_NOFLAGS);
	g_horde_tactical_spawn = gi.cvar("g_horde_tactical_spawn", "0", CVAR_NOFLAGS);
	g_vortex = gi.cvar("vortex", "0", CVAR_SERVERINFO | CVAR_LATCH);
	g_spectator_teleport = gi.cvar("g_spectator_teleport", "1", CVAR_NOFLAGS); // Allow spectators to use teleporters
	pvm = gi.cvar("pvm", "0", CVAR_NOFLAGS);

	g_loadent = gi.cvar("g_loadent", "1", CVAR_LATCH);
	g_chaotic = gi.cvar("g_chaotic", "0", CVAR_NOFLAGS);
	g_insane = gi.cvar("g_insane", "0", CVAR_NOFLAGS);
	g_swap_coop_monsters = gi.cvar("g_swap_coop_monsters", "1", CVAR_NOFLAGS);
	//g_ammoregen = gi.cvar("g_ammoregen", "0", CVAR_NOFLAGS);
	sv_wave_timer = gi.cvar("sv_wave_timer", "1", CVAR_NOFLAGS);
	//g_tracedbullets = gi.cvar("g_tracedbullets", "1", CVAR_NOFLAGS);
	//g_energyshells = gi.cvar("g_energyshells", "0", CVAR_NOFLAGS);
	//g_bouncygl = gi.cvar("g_bouncygl", "0", CVAR_NOFLAGS);
	//g_bfgpull = gi.cvar("g_bfgpull", "0", CVAR_NOFLAGS);
	// g_bfgslide = gi.cvar("g_bfgslide", "1", CVAR_NOFLAGS);
	g_startarmor = gi.cvar("g_startarmor", "0", CVAR_NOFLAGS);
	// g_upgradeproxs = gi.cvar("g_upgradeproxs", "0", CVAR_NOFLAGS);
	// g_piercingbeam = gi.cvar("g_piercingbeam", "0", CVAR_NOFLAGS);
	g_dm_spawns = gi.cvar("g_dm_spawns", "1", CVAR_NOFLAGS);
	//g_vampire = gi.cvar("g_vampire", "0", CVAR_NOFLAGS);
	//g_autohaste = gi.cvar("g_autohaste", "0", CVAR_NOFLAGS);
	//g_easymonsters = gi.cvar("g_easymonsters", "0", CVAR_NOFLAGS);
	g_iddmg = gi.cvar("g_iddmg", "1", CVAR_NOFLAGS);

	g_speedstuff = gi.cvar("g_speedstuff", "1.0f", CVAR_NOFLAGS);
	//g_mover_debug = gi.cvar("g_mover_debug", "0", CVAR_NOFLAGS);

	g_start_items = gi.cvar("g_start_items", "", CVAR_NOFLAGS);
	g_map_list = gi.cvar("g_map_list", "", CVAR_NOFLAGS);
	g_map_list_shuffle = gi.cvar("g_map_list_shuffle", "0", CVAR_NOFLAGS);
	g_lag_compensation = gi.cvar("g_lag_compensation", "1", CVAR_NOFLAGS);

	// items
	InitItems();

	game = {};

	// initialize all entities for this game
	game.maxentities = maxentities->integer;
	gi.Com_PrintFmt("sizeof(edict_t) = {} bytes, total edicts allocation = {} KB\n",
		sizeof(edict_t), (game.maxentities * sizeof(edict_t)) / 1024);
	g_edicts = (edict_t*)gi.TagMalloc(game.maxentities * sizeof(g_edicts[0]), TAG_GAME);
	globals.edicts = g_edicts;
	globals.max_edicts = game.maxentities;

	// initialize all clients for this game
	game.maxclients = maxclients->integer;
	game.clients = (gclient_t*)gi.TagMalloc(game.maxclients * sizeof(game.clients[0]), TAG_GAME);
	globals.num_edicts = game.maxclients + 1;

	//======
	// ROGUE
	if (gamerules->integer)
		InitGameRules(); // if there are game rules to set up, do so now.
	// ROGUE
	//======

	// how far back we should support lag origins for
	game.max_lag_origins = 20 * (0.1f / gi.frame_time_s);
	game.lag_origins = (vec3_t*)gi.TagMalloc(game.maxclients * sizeof(vec3_t) * game.max_lag_origins, TAG_GAME);
}

//===================================================================

void ShutdownGame()
{
	gi.Com_Print("==== ShutdownGame ====\n");

	gi.FreeTags(TAG_LEVEL);
	gi.FreeTags(TAG_GAME);
	
	// Reset pointers to prevent dangling pointer issues
	g_edicts = nullptr;
}

static void* G_GetExtension(const char* name)
{
	return nullptr;
}

const shadow_light_data_t* GetShadowLightData(int32_t entity_number);

gtime_t FRAME_TIME_S;
gtime_t FRAME_TIME_MS;

/*
=================
GetGameAPI

Returns a pointer to the structure with all entry points
and global variables
=================
*/
Q2GAME_API game_export_t* GetGameAPI(game_import_t* import)
{
	gi = *import;

	FRAME_TIME_S = FRAME_TIME_MS = gtime_t::from_ms(gi.frame_time_ms);

	globals.apiversion = GAME_API_VERSION;
	globals.PreInit = PreInitGame;
	globals.Init = InitGame;
	globals.Shutdown = ShutdownGame;
	globals.SpawnEntities = SpawnEntities;

	globals.WriteGameJson = WriteGameJson;
	globals.ReadGameJson = ReadGameJson;
	globals.WriteLevelJson = WriteLevelJson;
	globals.ReadLevelJson = ReadLevelJson;
	globals.CanSave = G_CanSave;

	globals.Pmove = Pmove;

	globals.GetExtension = G_GetExtension;

	globals.ClientChooseSlot = ClientChooseSlot;
	globals.ClientThink = ClientThink;
	globals.ClientConnect = ClientConnect;
	globals.ClientUserinfoChanged = ClientUserinfoChanged;
	globals.ClientDisconnect = ClientDisconnect;
	globals.ClientBegin = ClientBegin;
	globals.ClientCommand = ClientCommand;

	globals.RunFrame = G_RunFrame;
	globals.PrepFrame = G_PrepFrame;

	globals.ServerCommand = ServerCommand;
	globals.Bot_SetWeapon = Bot_SetWeapon;
	globals.Bot_TriggerEdict = Bot_TriggerEdict;
	globals.Bot_GetItemID = Bot_GetItemID;
	globals.Bot_UseItem = Bot_UseItem;
	globals.Edict_ForceLookAtPoint = Edict_ForceLookAtPoint;
	globals.Bot_PickedUpItem = Bot_PickedUpItem;

	globals.Entity_IsVisibleToPlayer = Entity_IsVisibleToPlayer;
	globals.GetShadowLightData = GetShadowLightData;

	globals.edict_size = sizeof(edict_t);

	return &globals;
}
//======================================================================

/*
=================
ClientEndServerFrames
=================
*/
void ClientEndServerFrames()
{
	// calc the player views now that all pushing
	// and damage has been added
	for (auto player : active_players())
	{
		ClientEndServerFrame(player);
	}
}

/*
=================
CreateTargetChangeLevel

Returns the created target changelevel
=================
*/
edict_t* CreateTargetChangeLevel(const char* map)
{
	edict_t* ent;

	ent = G_Spawn();
	ent->classname = "target_changelevel";
	Q_strlcpy(level.nextmap, map, sizeof(level.nextmap));
	ent->map = level.nextmap;
	return ent;
}

static void QueueSinglePlayerModeSettings()
{
	gi.AddCommandString(
		"bot_pause 1; bot_minClients -1; "
		"ctf 0; teamplay 0; pvm 0; horde 0; coop 0; deathmatch 0; "
		"g_instagib 0; g_dm_spawns 0; g_dm_spawn_farthest 0; "
		"g_use_hook 0; g_hook_wave 0; g_allow_grapple 0; g_allow_techs 0; "
		"g_loadent 0; dm_monsters 0; g_swap_coop_monsters 0; vortex 0; "
		"g_disable_player_collision 0; g_damage_scale 1; ai_damage_scale 1; ai_allow_dm_spawn 0; "
		"timelimit 0; fraglimit 0; capturelimit 0; maxspectators 0; "
		"set g_start_items \"\"; set cheats 0 s; maxclients 1; kexmultiplayer maxplayers 1\n");
}

/*
=================
EndDMLevel

The timelimit or fraglimit has been exceeded
=================
*/

// --- HELPER FUNCTIONS (add these to the top of the file or a utility header) ---

// Trim leading and trailing whitespace from a string_view
inline std::string_view trim_whitespace(std::string_view str)
{
    const char* whitespace = " \t\r\n";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string_view::npos)
        return {}; // String is all whitespace

    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

inline boost::container::small_vector<std::string_view, 32> split_string_view(std::string_view str, char delimiter = ' ')
{
    // Using small_vector to avoid heap allocation for typical map lists (< 32 entries)
    boost::container::small_vector<std::string_view, 32> result;
    if (!safe_reserve(result, 32)) {  // Pre-allocate reasonable size for map lists
        return result;
    }
    size_t start = 0;
    size_t end = 0;
    while ((start = str.find_first_not_of(delimiter, end)) != std::string_view::npos) {
        end = str.find(delimiter, start);
        std::string_view token = str.substr(start, end - start);

        // Trim whitespace from the token
        token = trim_whitespace(token);

        // Skip empty tokens (from consecutive delimiters or whitespace-only tokens)
        if (token.empty()) {
            continue;
        }

        if (!safe_push_back(result, token, 256)) {
            break;  // Stop if we hit size limit
        }
    }
    return result;
}

template<typename Container>
inline std::string join_string_views(const Container& views, const char* separator = " ")
{
    if (views.empty()) return "";
    size_t total_size = (views.size() - 1) * strlen(separator);
    for (const auto& v : views) total_size += v.length();

    // Clamp total size to reasonable limit for cvar strings
    const size_t MAX_CVAR_STRING = 8192;
    if (total_size > MAX_CVAR_STRING) {
        total_size = MAX_CVAR_STRING;
    }

    std::string result;
    try {
        result.reserve(total_size);
    } catch (...) {
        return "";  // Allocation failed
    }

    if (!safe_string_append(result, views[0], MAX_CVAR_STRING)) {
        return result;
    }

    for (size_t i = 1; i < views.size(); ++i) {
        if (!safe_string_append(result, separator, MAX_CVAR_STRING)) {
            break;
        }
        if (!safe_string_append(result, views[i], MAX_CVAR_STRING)) {
            break;
        }
    }
    return result;
}

// Validate and sanitize a map name before changing levels
// Returns true if the map name is valid, false otherwise
inline bool ValidateMapName(const char* map_name, char* sanitized_buffer, size_t buffer_size)
{
    if (!map_name || !sanitized_buffer || buffer_size == 0)
        return false;

    // Trim leading/trailing whitespace
    std::string_view map_view = trim_whitespace(map_name);

    // Check for empty map name
    if (map_view.empty()) {
        gi.Com_PrintFmt("WARNING: Empty map name detected in map list! Skipping map change.\n");
        return false;
    }

    // Check for spaces within the map name (after trimming) - this indicates corruption
    if (map_view.find(' ') != std::string_view::npos) {
        gi.Com_PrintFmt("ERROR: Map name '{}' contains a SPACE character! This indicates corruption in g_map_list.\n", map_name);
        gi.Com_PrintFmt("       Map names should never have spaces. Check your config file for formatting errors.\n");
        return false;
    }

    // Check for suspicious characters or corruption
    if (map_view.find_first_of("\r\n\t") != std::string_view::npos) {
        gi.Com_PrintFmt("WARNING: Map name '{}' contains invalid characters! Sanitizing...\n", map_name);
        // Remove the invalid characters
        size_t write_pos = 0;
        for (char c : map_view) {
            if (c != '\r' && c != '\n' && c != '\t' && write_pos < buffer_size - 1) {
                sanitized_buffer[write_pos++] = c;
            }
        }
        sanitized_buffer[write_pos] = '\0';
        map_view = std::string_view(sanitized_buffer, write_pos);
    }

    // Check for leading/trailing spaces that weren't trimmed (shouldn't happen but defensive)
    if (map_name[0] == ' ' || map_name[strlen(map_name) - 1] == ' ') {
        gi.Com_PrintFmt("WARNING: Map name '{}' has leading/trailing spaces! Corruption detected in g_map_list.\n", map_name);
    }

    // Copy the sanitized name to buffer
    size_t len = std::min(map_view.length(), buffer_size - 1);
    memcpy(sanitized_buffer, map_view.data(), len);
    sanitized_buffer[len] = '\0';

    // Check for very short or suspicious map names
    if (len < 2) {
        gi.Com_PrintFmt("WARNING: Map name '{}' is suspiciously short! Possible corruption in g_map_list.\n", sanitized_buffer);
        return false;
    }

    return true;
}

void EndDMLevel()
{
    edict_t* ent;

    // Priority 1: Stay on the same level if the cvar is set.
    if (g_dm_same_level->integer)
    {
        BeginIntermission(CreateTargetChangeLevel(level.mapname));
        return;
    }

    // Priority 2: Use a forced map if one is set.
    if (*level.forcemap)
    {
        BeginIntermission(CreateTargetChangeLevel(level.forcemap));
        return;
    }

    // Priority 3: Process the map list.
    if (*g_map_list->string)
    {
        // ---  SHUFFLE LOGIC ---
        if (g_map_list_shuffle->integer)
        {
            // Use the allocation-free split to get views of the map names.
            auto values = split_string_view(g_map_list->string);

            // If there's only one map, just restart it.
            if (values.size() <= 1) {
                BeginIntermission(CreateTargetChangeLevel(level.mapname));
                return;
            }

            // Shuffle the list.
            std::shuffle(values.begin(), values.end(), mt_rand);

            // Ensure the new first map isn't the same as the current one.
            if (values[0] == level.mapname) {
                std::swap(values[0], values.back());
            }

            // --- CRITICAL FIX: Copy the next map name BEFORE modifying the cvar ---
            // The string_views in 'values' point to g_map_list->string, so we MUST
            // copy the next map name before cvar_forceset invalidates that memory!
            char next_map_buffer[MAX_QPATH];
            char validated_map_buffer[MAX_QPATH];
            std::string_view next_map_sv = values[0];

            // Safely copy the string_view's content into our buffer.
            size_t len = std::min(next_map_sv.length(), sizeof(next_map_buffer) - 1);
            memcpy(next_map_buffer, next_map_sv.data(), len);
            next_map_buffer[len] = '\0'; // Manually null-terminate.

            // Re-join the list into a new string to update the cvar.
            // This one allocation is acceptable to rebuild the list.
            std::string new_map_list = join_string_views(values);
            gi.cvar_forceset("g_map_list", new_map_list.c_str());

            // Validate the map name before changing levels
            if (!ValidateMapName(next_map_buffer, validated_map_buffer, sizeof(validated_map_buffer))) {
                gi.Com_PrintFmt("ERROR: Invalid map name '{}' in shuffled map list! Falling back to current map.\n", next_map_buffer);
                BeginIntermission(CreateTargetChangeLevel(level.mapname));
                return;
            }

            // Start the intermission with the validated map name.
            BeginIntermission(CreateTargetChangeLevel(validated_map_buffer));
            return; // We are done.
        }
        // --- ORIGINAL NON-SHUFFLE LOGIC (FALLBACK) ---
        else
        {
            const char* str = g_map_list->string;
            char first_map[MAX_QPATH]{ 0 };
            char validated_map[MAX_QPATH];
            char* map;

            while (true)
            {
                map = COM_ParseEx(&str, " ");
                if (!*map)
                    break; // End of list

                if (Q_strcasecmp(map, level.mapname) == 0)
                {
                    // Found current map, go to the next one.
                    map = COM_ParseEx(&str, " ");
                    if (*map) // If there is a next map
                    {
                        // Validate before changing
                        if (ValidateMapName(map, validated_map, sizeof(validated_map))) {
                            BeginIntermission(CreateTargetChangeLevel(validated_map));
                            return;
                        } else {
                            gi.Com_PrintFmt("ERROR: Invalid next map '{}' in map list! Falling back to current map.\n", map);
                            BeginIntermission(CreateTargetChangeLevel(level.mapname));
                            return;
                        }
                    }
                    // If no next map, we'll break and loop to the first_map.
                    break;
                }
                // Store the first map in the list in case we need to loop.
                if (!first_map[0])
                    Q_strlcpy(first_map, map, sizeof(first_map));
            }

            // If we reached the end of the list, loop back to the first map.
            if (first_map[0])
            {
                // Validate before changing
                if (ValidateMapName(first_map, validated_map, sizeof(validated_map))) {
                    BeginIntermission(CreateTargetChangeLevel(validated_map));
                    return;
                } else {
                    gi.Com_PrintFmt("ERROR: Invalid first map '{}' in map list! Restarting current map.\n", first_map);
                    BeginIntermission(CreateTargetChangeLevel(level.mapname));
                    return;
                }
            }
        }
    }

    // Priority 4: Use a nextmap set by a trigger.
    if (level.nextmap[0])
    {
        BeginIntermission(CreateTargetChangeLevel(level.nextmap));
        return;
    }

    // Priority 5: Find a target_changelevel entity in the map.
    ent = G_FindByString<&edict_t::classname>(nullptr, "target_changelevel");
    if (ent)
    {
        BeginIntermission(ent);
        return;
    }

    // Final Fallback: If all else fails, just restart the current level.
    BeginIntermission(CreateTargetChangeLevel(level.mapname));
}

/*
=================
CheckNeedPass
=================
*/
void CheckNeedPass()
{
	int need;
	static int32_t password_modified, spectator_password_modified;

	// if password or spectator_password has changed, update needpass
	// as needed
	if (Cvar_WasModified(password, password_modified) || Cvar_WasModified(spectator_password, spectator_password_modified))
	{
		need = 0;

		if (*password->string && Q_strcasecmp(password->string, "none"))
			need |= 1;
		if (*spectator_password->string && Q_strcasecmp(spectator_password->string, "none"))
			need |= 2;

		gi.cvar_set("needpass", G_Fmt("{}", need).data());
	}
}

/*
=================
CheckDMRules
=================
*/
void CheckDMRules()
{
	gclient_t* cl;

	if (level.intermissiontime)
		return;

	// Paril
	if (g_horde->integer)
	{
		Horde_RunFrame();

		CTFCheckTimeExtensionVote(); // add more timelimit vote
		// Check if time is up
		if (timelimit->value && level.time >= gtime_t::from_min(timelimit->value))
		{
			gi.LocBroadcast_Print(PRINT_HIGH, "$g_timelimit_hit");
			// Reset game state once (avoid redundant calls)
			gi.cvar_set("timelimit", "60");
			HandleResetEvent();
			EndDMLevel();

			// Notify and reinitialize each client
			for (int i = 0; i < maxclients->integer; i++)
			{
				edict_t* ent = g_edicts + 1 + i;
				if (ent->inuse && ent->client)
				{
				IsPvMMode() ? gi.LocCenter_Print(ent, "PvM Mode is being reset.") : gi.LocCenter_Print(ent, "Horde Mode is being reset.");
					InitClientPt(ent, ent->client);
				}
			}
			return;
		}
	}

	if (!deathmatch->integer)
		return;

	// ZOID
	if (ctf->integer && CTFCheckRules())
	{
		EndDMLevel();
		return;
	}
	if (CTFInMatch())
		return; // no checking in match mode
	// ZOID

	if (CTFCheckRules())
	{
		return;
	}
//=======
// ROGUE
	if (gamerules->integer && DMGame.CheckDMRules)
	{
		if (DMGame.CheckDMRules())
			return;
	}
	// ROGUE
	//=======

	if (timelimit->value)
	{
		if (level.time >= gtime_t::from_min(timelimit->value))
		{
			gi.LocBroadcast_Print(PRINT_HIGH, "$g_timelimit_hit");
			EndDMLevel();
			return;
		}
	}

	if (fraglimit->integer)
	{
		// [Paril-KEX]
		if (teamplay->integer)
		{
			CheckEndTDMLevel();
			return;
		}

		for (uint32_t i = 0; i < game.maxclients; i++)
		{
			cl = game.clients + i;
			if (!g_edicts[i + 1].inuse)
				continue;

			if (cl->resp.score >= fraglimit->integer)
			{
				gi.LocBroadcast_Print(PRINT_HIGH, "$g_fraglimit_hit");
				EndDMLevel();
				return;
			}
		}
	}
}

/*
=============
ExitLevel
=============
*/
void ExitLevel()
{
	// [Paril-KEX] N64 fade
	if (level.intermission_fade)
	{
		level.intermission_fade_time = level.time + 1.3_sec;
		level.intermission_fading = true;
		return;
	}

	ClientEndServerFrames();

	level.exitintermission = 0;
	level.intermissiontime = 0_ms;

	// [Paril-KEX] support for intermission completely wiping players
	// back to default stuff
	if (level.intermission_clear)
	{
		level.intermission_clear = false;

		for (uint32_t i = 0; i < game.maxclients; i++)
		{
			// [Kex] Maintain user info to keep the player skin. 
			char userinfo[MAX_INFO_STRING];
			memcpy(userinfo, game.clients[i].pers.userinfo, sizeof(userinfo));

			game.clients[i].pers = client_persistant_t{};
			game.clients[i].resp.coop_respawn = client_persistant_t{};
			g_edicts[i + 1].health = 0; // this should trip the power armor, etc to reset as well

			memcpy(game.clients[i].pers.userinfo, userinfo, sizeof(userinfo));
			memcpy(game.clients[i].resp.coop_respawn.userinfo, userinfo, sizeof(userinfo));
		}
	}

	// [Paril-KEX] end of unit, so clear level trackers
	if (level.intermission_eou)
	{
		game.level_entries = {};

		// give all players their lives back
		if (g_coop_enable_lives->integer)
			for (auto player : active_players())
				player->client->pers.lives = g_coop_num_lives->integer + 1;
	}

	if (CTFNextMap())
		return;

	if (level.changemap == nullptr)
	{
		gi.Com_Error("Got null changemap when trying to exit level. Was a trigger_changelevel configured correctly?");
		return;
	}

	// for N64 mainly, but if we're directly changing to "victorXXX.pcx" then
	// end game
	size_t start_offset = (level.changemap[0] == '*' ? 1 : 0);

	// Check for true single-player mode switch
	if (start_offset && !Q_strncasecmp(level.changemap + start_offset, "sp:", 3)) {
		const char* map = level.changemap + start_offset + 3;

		QueueSinglePlayerModeSettings();

		extern ctfgame_t ctfgame;
		ctfgame.elevel[0] = '\0';

		gi.AddCommandString(G_Fmt("map \"{}\"\n", map).data());
	}
	// Check for cooperative mode switch
	else if (start_offset && !Q_strncasecmp(level.changemap + start_offset, "coop:", 5)) {
		// Extract the actual map name after "coop:"
		const char* map = level.changemap + start_offset + 5;

		// Apply cooperative settings first
		gi.AddCommandString("bot_pause 1; skill 3; g_dm_spawns 0; g_use_hook 0; g_instagib 0; pvm 0; horde 0; coop 1; deathmatch 0; g_allow_grapple 0; g_coop_squad_respawn 1; g_allow_techs 0; g_coop_num_lives 7; set cheats 0 s; g_coop_health_scaling 0.23; timelimit 0; maxclients 7; kexmultiplayer maxplayers 7\n");

		// Clear the election level after successful cooperative switch
		extern ctfgame_t ctfgame;
		ctfgame.elevel[0] = '\0';

		// Then change to the map
		gi.AddCommandString(G_Fmt("map \"{}\"\n", map).data());
	}
	// Check for horde mode switch
	else if (start_offset && !Q_strncasecmp(level.changemap + start_offset, "horde:", 6)) {
		// Extract the actual map name after "horde:"
		const char* map = level.changemap + start_offset + 6;

		// Apply horde mode settings first (any horde value >= 1 gets Horde 2 behavior now)
		gi.AddCommandString(G_Fmt("horde {}; coop 0; deathmatch 1; g_allow_techs 1; timelimit 0; g_dm_spawn_farthest 0\n",
			g_horde->integer >= 1 ? g_horde->integer : 1).data()); //; maxclients 32; kexmultiplayer maxplayers 32

		// Clear the election level after successful horde switch
		extern ctfgame_t ctfgame;
		ctfgame.elevel[0] = '\0';

		// Then change to the map
		gi.AddCommandString(G_Fmt("map \"{}\"\n", map).data());
	}
	// Check for victory screen
	else if (strlen(level.changemap) > (6 + start_offset) &&
		!Q_strncasecmp(level.changemap + start_offset, "victor", 6) &&
		!Q_strncasecmp(level.changemap + strlen(level.changemap) - 4, ".pcx", 4))
		gi.AddCommandString(G_Fmt("endgame \"{}\"\n", level.changemap + start_offset).data());
	else
		gi.AddCommandString(G_Fmt("gamemap \"{}\"\n", level.changemap).data());

	level.changemap = nullptr;
}

static void G_CheckCvars()
{
	if (Cvar_WasModified(sv_airaccelerate, game.airacceleration_modified))
	{
		// [Paril-KEX] air accel handled by game DLL now, and allow
		// it to be changed in sp/coop
		gi.configstring(CS_AIRACCEL, G_Fmt("{}", sv_airaccelerate->integer).data());
		pm_config.airaccel = sv_airaccelerate->integer;
	}

	if (Cvar_WasModified(sv_gravity, game.gravity_modified))
		level.gravity = sv_gravity->value;
}

static bool G_AnyDeadPlayersWithoutLives()
{
	for (auto const player : active_players())
		if (player->health <= 0 && !player->client->pers.lives)
			return true;

	return false;
}

static gtime_t g_bot_overlap_cooldown = 0_ms;

void G_CheckBotOverlap(void)
{
    // If the global cooldown is active, do nothing this frame.
    // This correctly enforces the 5-second pause between unsticking events.
    if (level.time < g_bot_overlap_cooldown)
    {
        return;
    }

    // If the cooldown is not active, search for the FIRST overlapping pair to resolve.
    for (uint32_t i = 1; i <= game.maxclients; i++)
    {
        edict_t* bot1 = &g_edicts[i];

        // --- Validate bot1 ---
        if (!bot1->inuse || !(bot1->svflags & SVF_BOT) || !bot1->client || bot1->deadflag || bot1->solid == SOLID_NOT)
            continue;

        for (uint32_t j = i + 1; j <= game.maxclients; j++)
        {
            edict_t* bot2 = &g_edicts[j];

            // --- Validate bot2 ---
            if (!bot2->inuse || !(bot2->svflags & SVF_BOT) || !bot2->client || bot2->deadflag || bot2->solid == SOLID_NOT)
                continue;

            // --- The Overlap Check ---
            if (boxes_intersect(bot1->absmin, bot1->absmax, bot2->absmin, bot2->absmax))
            {
                // Set the emergency flag for a silent teleport
                bot1->client->emergency_teleport = true;

                // *** THE FIX IS HERE ***
                // Attempt to teleport the bot AND check if it was successful.
                if (TeleportSelf(bot1))
                {
                    // SUCCESS: A bot was actually teleported.
                    // Now, activate the global cooldown.
                    g_bot_overlap_cooldown = level.time + 5_sec;

                    // Return immediately to ensure only ONE unsticking event
                    // happens per 5-second cycle. This prevents teleport storms.
                    return;
                }
                // If TeleportSelf(bot1) returned false, we do nothing. The loop will
                // continue, trying to find another pair of stuck bots to resolve
                // in this same frame.
            }
        }
    }
}
/*
================
G_RunFrame

Advances the world by 0.1 seconds
================
*/

#include "profiler.h"
#include "horde/g_horde_phys.h"

/*
================
UpdateProximityGrids

Updates the spatial grid system for efficient proximity queries.
The grid system is essential for Tesla coils, traps, and other proximity-based mechanics.
Works in all game modes.
================
*/
static void UpdateProximityGrids()
{
    PROFILE_SCOPE("BuildProximityGrid");

    // The grid's world bounds are calculated only ONCE per map load.
    // This is a heavy operation that should not be done every frame.
    static std::string last_map_for_grid;
    if (last_map_for_grid != level.mapname)
    {
        vec3_t world_mins{}, world_maxs{};
        ClearBounds(world_mins, world_maxs);
        // We use the efficient 'monster_spawn_points' iterator here.
        for (auto* sp : monster_spawn_points()) {
            AddPointToBounds(sp->s.origin, world_mins, world_maxs);
        }
        world_mins -= vec3_t{512, 512, 512};
        world_maxs += vec3_t{512, 512, 512};

        HordePhys::g_monster_grid.Build(world_mins, world_maxs);
        HordePhys::g_entity_grid.Build(world_mins, world_maxs);
        last_map_for_grid = level.mapname;
    }

    // The per-frame update is very fast. It clears the previous
    // frame's data and then uses the efficient iterators to add only the
    // relevant entities (monsters, players, projectiles) to the grid.
    HordePhys::g_monster_grid.Reset();
    HordePhys::g_entity_grid.Reset();

    for (auto* monster : active_monsters()) {
        HordePhys::g_monster_grid.Add(monster);
        HordePhys::g_entity_grid.AddEntity(monster);
    }
    for (auto *player : active_players_no_spect()) {
        if (player && player->inuse && player->health > 0 && !EntIsSpectating(player)) {
            HordePhys::g_monster_grid.Add(player);
            HordePhys::g_entity_grid.AddEntity(player);
        }
    }
    for (auto* proj : active_projectiles()) {
        HordePhys::g_monster_grid.Add(proj);
        HordePhys::g_entity_grid.AddEntity(proj);
    }

    // Add other damageable entities more efficiently
    // Start after maxclients + BODY_QUEUE_SIZE to skip body queue
    const uint32_t start_idx = game.maxclients + static_cast<uint32_t>(BODY_QUEUE_SIZE) + 1;

    // Only scan entities that are likely to be damageable and relevant
    for (uint32_t i = start_idx; i < globals.num_edicts; i++) {
        edict_t* ent = &g_edicts[i];
        if (!ent->inuse || !ent->takedamage)
            continue;

        // Skip entities already added (monsters, players, projectiles)
        if (ent->svflags & SVF_MONSTER)
            continue;
        if (ent->client)
            continue;
        if (ent->svflags & SVF_PROJECTILE)
            continue;

        // Add other damageable entities (barrels, breakables, etc.)
        HordePhys::g_entity_grid.AddEntity(ent);
    }

    if (developer->integer >= 2) {
       HordePhys::g_monster_grid.DebugDraw();
    }
}

/*
================
ProcessHordePerFrameLogic

Processes horde-mode specific logic that runs every frame.
Includes bot overlap detection, player scaling, monster management, and cleanup.
================
*/
template<typename MonstersT, typename PlayersT>
static void ProcessHordePerFrameLogic(const MonstersT& monsters, const PlayersT& players)
{
    // DISABLED: Test if monster exclusion alone prevents crashes
    // horde::AssetManager::Get().ProcessClientLoading();

    // Check for and resolve any bot-on-bot overlaps.
    G_CheckBotOverlap();

    // Cache map size - only update if map changes
    static std::string last_mapname;
    static horde::MapSize cached_mapSize;
    if (last_mapname != level.mapname) {
        cached_mapSize = GetMapSize(level.mapname);
        last_mapname = level.mapname;
    }

    // Cache human player count - only recalculate periodically
    static int cached_human_player_count = 0;
    static gtime_t last_player_count_check = 0_ms;
    const gtime_t PLAYER_COUNT_CHECK_INTERVAL = 500_ms;

    if ((level.time - last_player_count_check) >= PLAYER_COUNT_CHECK_INTERVAL) {
        cached_human_player_count = GetNumHumanPlayers();
        last_player_count_check = level.time;
    }

    // Player scale configuration (sigmoid scaling retired; traditional coop scaling always applies)
    level.coop_scale_players = 2 + cached_human_player_count;
    G_Monster_CheckCoopHealthScaling();

    // Update deployables based on adrenaline changes (cached for performance)
    G_UpdateAdrenalineBasedDeployables(current_wave_level);

    // Time-slicing monster checks.
    // Instead of checking every monster for being stuck every frame, we process
    // a small batch. This spreads the CPU load over multiple frames.
    constexpr uint32_t BATCH_SIZE = 32;
    uint32_t processed = 0;
    for (auto ent : monsters) {
        if (processed >= BATCH_SIZE) break;
        CheckAndRestoreMonsterAlpha(ent);
        if (!ent->monsterinfo.IS_BOSS)
            CheckAndTeleportStuckMonster(ent);
        processed++;
    }

    // Other cleanup and state management functions.
    CleanupInvalidEntities();
    CleanupStuckEntities();
    CheckAndResetDisabledSpawnPoints();
    if (horde_message_end_time > 0_sec && level.time >= horde_message_end_time) {
        ClearHordeMessage();
    }
}

/*
================
UpdateIntermissionState

Handles intermission timing, fading, and exit logic.
Returns true if the frame should exit early (during fade or exit).
================
*/
template<typename PlayersT>
static bool UpdateIntermissionState(const PlayersT& players)
{
    // Handle intermission timing
    if (level.intermissiontime && g_horde->integer) {
        level.intermission_fade = true;
        constexpr gtime_t INTERMISSION_DURATION = 30_sec;

        if (level.intermissiontime == level.time) {
            // First time entering intermission
            gi.Com_PrintFmt("PRINT: Intermission started. Auto-exit scheduled in 30 seconds.\n");
        }

        const gtime_t time_elapsed = level.time - level.intermissiontime;
        const gtime_t time_remaining = INTERMISSION_DURATION - time_elapsed;

        if (time_remaining == 0_ms) {
            // Time to exit intermission
            gi.Com_PrintFmt("PRINT: Auto-exiting intermission after 30 seconds.\n");
            level.exitintermission = true;
        }
        else if (time_remaining.seconds() < 30 && time_remaining.milliseconds() % 5000 == 0) {
            // Print remaining time every 5 seconds in last 30 seconds
            gi.Com_PrintFmt("PRINT: Intermission time remaining: {:.0f} seconds\n", time_remaining.seconds());
        }
    }

    // Handle intermission fading
    if (level.intermission_fading) {
        if (level.intermission_fade_time > level.time) {
            const float alpha = clamp(1.0f - (level.intermission_fade_time - level.time - 300_ms).seconds(), 0.f, 1.f);

            for (auto player : players)
                player->client->ps.screen_blend = { 0, 0, 0, alpha };
        }
        else {
            level.intermission_fade = level.intermission_fading = false;
            ExitLevel();
        }

        level.in_frame = false;
        return true;  // Exit frame early
    }

    // Exit intermissions
    if (level.exitintermission) {
        ExitLevel();
        level.in_frame = false;
        return true;  // Exit frame early
    }

    // Reload map on restart
    if (level.coop_level_restart_time > 0_ms && level.time > level.coop_level_restart_time) {
        ClientEndServerFrames();
        gi.AddCommandString("restart_level\n");
    }

    return false;  // Continue with normal frame processing
}

/*
================
G_RunFrame_

Main game frame function. This is the main game frame function. It is called every server frame.
The structure of this function is highly optimized for performance.
================
*/
inline void G_RunFrame_(bool main_loop)
{
    auto monsters = active_monsters();
    auto players = active_players();

    // Profiler and Horde-specific setup.
    if (g_horde_profiler) {
        g_profiler_enabled = g_horde_profiler->integer != 0;
    } else {
        g_profiler_enabled = false;
    }
    Profiler_ResetFrame();

    // Update proximity grid system (works in all game modes)
    UpdateProximityGrids();

    // Menu updates (works in all game modes)
    CheckAndUpdateMenus();

    // Horde-specific per-frame logic
    if (g_horde->integer) {
        ProcessHordePerFrameLogic(monsters, players);
    }

    // --- GENERAL FRAME LOGIC ---
    level.in_frame = true;
    G_CheckCvars();
    Bot_UpdateDebug();
    level.time += FRAME_TIME_MS;

    // Handle intermission state (returns true if frame should exit early)
    if (UpdateIntermissionState(players)) {
        return;
    }

    // Handle coop respawn states - move conditional outside of loop for better branching
    bool check_coop_respawn = (G_IsCooperative() && (g_coop_enable_lives->integer || g_coop_squad_respawn->integer)) ||
        (G_IsDeathmatch() && g_horde->integer && (g_coop_enable_lives->integer || g_coop_squad_respawn->integer));

    if (check_coop_respawn) {
        for (auto player : players) {
            if (player->client->respawn_time >= level.time)
                player->client->coop_respawn_state = COOP_RESPAWN_WAITING;
            else if (g_coop_enable_lives->integer && player->health <= 0 && player->client->pers.lives == 0)
                player->client->coop_respawn_state = COOP_RESPAWN_NO_LIVES;
            else if (g_coop_enable_lives->integer && G_AnyDeadPlayersWithoutLives())
                player->client->coop_respawn_state = COOP_RESPAWN_NO_LIVES;
            else
                player->client->coop_respawn_state = COOP_RESPAWN_NONE;
        }
    }

    // This is the most efficient way to process every active entity in the game.
    // It iterates through the g_edicts array only ONCE per frame.
    edict_t* ent = &g_edicts[0];
    for (uint32_t i = 0; i < globals.num_edicts; i++, ent++) {
        // The most basic optimization: skip empty entity slots.
        if (!ent->inuse) {
            // housekeeping for disconnected player slots.
            if (i > 0 && i <= game.maxclients) {
                if (ent->timestamp && level.time < ent->timestamp) {
                    const int32_t playernum = ent - g_edicts - 1;
                    gi.configstring(CS_PLAYERSKINS + playernum, "");
                    ent->timestamp = 0_sec;
                }
            }
            continue;
        }

        // Set the global pointer to the current entity.
        level.current_entity = ent;

        ApplyGradualHealing(ent);
        if (!(ent->s.renderfx & RF_BEAM))
            ent->s.old_origin = ent->s.origin;

        if ((ent->groundentity) && (ent->groundentity->linkcount != ent->groundentity_linkcount)) {
            contents_t mask = G_GetClipMask(ent);

            if (!(ent->flags & (FL_SWIM | FL_FLY)) && (ent->svflags & SVF_MONSTER)) {
                ent->groundentity = nullptr;
                M_CheckGround(ent, mask);
            }
            else {
                // Check if still on ground
                trace_t tr = gi.trace(ent->s.origin, ent->mins, ent->maxs, ent->s.origin + ent->gravityVector, ent, mask);

                if (tr.startsolid || tr.allsolid || tr.ent != ent->groundentity)
                    ent->groundentity = nullptr;
                else
                    ent->groundentity_linkcount = ent->groundentity->linkcount;
            }
	}

        Entity_UpdateState(ent);

        // Branch to either client-specific logic or general entity logic.
        if (i > 0 && i <= game.maxclients) {
            ClientBeginServerFrame(ent);
        } else {
            G_RunEntity(ent);
        }

        // Process monster pain immediately after its main logic has run.
        // G_RunEntity above may have freed this monster this frame, so re-check inuse
        // before processing deferred pain on a dead edict.
        if (ent->inuse && (ent->svflags & SVF_MONSTER)) {
            M_ProcessPain(ent);
        }
    }

    // Game rules checks
    CheckDMRules();
    CheckNeedPass();

    if (check_coop_respawn) {
        // Check if all players are now alive
        bool reset_coop_respawn = true;
        for (auto const player : players) {
            if (player->health <= 0) {
                reset_coop_respawn = false;
                break;
            }
        }

        // Reset respawn states if all players alive
        if (reset_coop_respawn) {
            for (auto const player : players)
                player->client->coop_respawn_state = COOP_RESPAWN_NONE;
        }
    }


    ClientEndServerFrames();

    if (level.entry && !level.intermissiontime && g_edicts[1].inuse && g_edicts[1].client->pers.connected)
        level.entry->time += FRAME_TIME_S;

    level.in_frame = false;
    Profiler_RunFrame_End();
}

inline bool G_AnyPlayerSpawned()
{
	for (auto const player : active_players())
		if (player->client && player->client->pers.spawned)
			return true;

	return false;
}

void G_RunFrame(bool main_loop)
{
	//if (main_loop && !G_AnyPlayerSpawned())
	//	return;

	for (int32_t i = 0; i < g_frames_per_frame->integer; i++)
		G_RunFrame_(main_loop);

	// match details.. only bother if there's at least 1 player in-game
	// and not already end of game
	if (G_AnyPlayerSpawned() && !level.intermissiontime)
	{
		constexpr gtime_t MATCH_REPORT_TIME = 45_sec;

		if (level.time - level.next_match_report > MATCH_REPORT_TIME)
		{
			level.next_match_report = level.time + MATCH_REPORT_TIME;
			G_ReportMatchDetails(false);
		}
	}
}

/*
================
G_PrepFrame

This has to be done before the world logic, because
player processing happens outside RunFrame
================
*/
void G_PrepFrame()
{
	for (uint32_t i = 0; i < globals.num_edicts; i++)
		g_edicts[i].s.event = EV_NONE;

	for (auto player : active_players())
		player->client->ps.stats[STAT_HIT_MARKER] = 0;

	globals.server_flags &= ~SERVER_FLAG_INTERMISSION;

	if (level.intermissiontime) {
		globals.server_flags |= SERVER_FLAG_INTERMISSION;
	}
}
