// --- START OF FILE horde_menu.cpp ---

#include "../g_local.h"		   // Includes edict_t, gclient_t, gi, level, etc.
#include "../shared.h"		   // For MAX_ITEMS, etc.
#include "../memory_safety.h"  // For safe memory operations
#include "../ctf/p_ctf_menu.h" // Menu system definitions and functions
#include "../ctf/g_ctf.h"	   // For CTF functions like CTFObserver, CTFJoinTeam, CTFBeginElection, etc.
#include "g_horde.h"		   // For GetMapSize
#include "g_horde_benefits.h"
#include "g_laser.h"
#include "p_flyer_morph.h" // For IsMorphed, RestoreMorphed, Cmd_PlayerToFlyer_f
#include "p_brain_morph.h" // For Cmd_PlayerToBrain_f

// Declaration for P_GetLobbyUserNum (defined in p_client.cpp)
extern unsigned int P_GetLobbyUserNum(const edict_t *player);

// Declaration for Cmd_Help_f (defined in p_hud.cpp)
extern void Cmd_Help_f(edict_t *ent);

// Forward Declarations from this file
void OpenVoteMenu(edict_t *ent);
void VoteMenuHandler(edict_t *ent, pmenuhnd_t *p);
void UpdateVoteMenu();
void ShowInventory(edict_t *ent);
void OpenHordeMenu(edict_t *ent) noexcept;
void HordeMenuHandler(edict_t *ent, pmenuhnd_t *p);
pmenuhnd_t *CreateHordeMenu(edict_t *ent);
void OpenTechMenu(edict_t *ent);
void TechMenuHandler(edict_t *ent, pmenuhnd_t *p);
void OpenHUDMenu(edict_t *ent);
void UpdateHUDMenu(edict_t *ent, pmenuhnd_t *p);
void HUDMenuHandler(edict_t *ent, pmenuhnd_t *p);
void CheckAndUpdateMenus();
void OpenMapCategoryMenu(edict_t *ent);
void MapCategoryHandler(edict_t *ent, pmenuhnd_t *p);
void CategorizeMapList();
pmenuhnd_t *CreateHUDMenu(edict_t *ent);
void OpenMiscMenu(edict_t *ent, int cursor_position = -1); // Forward declare Misc menu functions
void MiscMenuHandler(edict_t *ent, pmenuhnd_t *p);
void OpenRespawnWeaponMenu(edict_t *ent); // Forward declare Respawn Weapon menu
void RespawnWeaponMenuHandler(edict_t *ent, pmenuhnd_t *p);
void OpenAdminMenu(edict_t *ent); // Forward declare Admin menu functions
void AdminMenuHandler(edict_t *ent, pmenuhnd_t *p);

// Upgrade menu functions
void OpenUpgradeMenu(edict_t *ent);
void UpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);
pmenuhnd_t *CreateUpgradeMenu(edict_t *ent);

// Benefits menu functions
void OpenAbilitiesMenu(edict_t *ent);
void AbilitiesMenuHandler(edict_t *ent, pmenuhnd_t *p);
void OpenWeaponsMenu(edict_t *ent);
void WeaponsMenuHandler(edict_t *ent, pmenuhnd_t *p);
pmenuhnd_t *CreateAbilitiesMenu(edict_t *ent);
pmenuhnd_t *CreateWeaponsMenu(edict_t *ent);

// Helper to get sentry type name
static const char *GetSentryTypeName(sentrytype_t type)
{
	switch (type)
	{
	case SENTRY_HEATBEAM:
		return "Beam/Phalanx";
	case SENTRY_MACHINEGUN:
		return "Mg/Rocket";
	case SENTRY_FLECHETTE:
		return "Flech/Grenade";
	case SENTRY_RANDOM: // fallthrough
	default:
		return "Random";
	}
}

// Helper to get BFG mode name
static const char *GetBFGModeName(BFGMode mode)
{
	switch (mode)
	{
	case BFGMode::NORMAL:
		return "Normal";
	case BFGMode::SLIDE:
		return "Slide";
	case BFGMode::GRAV_PULL:
		return "Slide+Pull";
	default:
		return "Normal";
	}
}

static const char *GetSpecialWaveName(int type)
{
	switch (type)
	{
	case SPECIAL_WAVE_BARRELS:
		return "Barrel";
	case SPECIAL_WAVE_HOOK:
		return "Hook";
	case SPECIAL_WAVE_BOMBSPELL_FORWARD:
		return "Bombspell";
	case SPECIAL_WAVE_BOMBSPELL_AREA:
		return "Bombspell Area";
	case SPECIAL_WAVE_TELEPORT_FWD:
		return "Teleport Fwd";
	case SPECIAL_WAVE_FIREBALL:
		return "Fireball";
	default:
		return "None";
	}
}

static const char *GetMorphTypeName(int type)
{
	switch (type)
	{
	case 0:
		return "Brain";
	case 1:
		return "Flyer";
	default:
		return "Brain";
	}
}

//--------------------------------
static void SetGameName(pmenu_t *p);
static void SetLevelName(pmenu_t *p);

void HordeJoinTeam(edict_t *ent, pmenuhnd_t *p);		 // Handler for Join Horde
void GoChaseCam(edict_t *ent, pmenuhnd_t *p);			 // Handler for Chase Cam/Spectator
void OpenAdminFromJoinMenu(edict_t *ent, pmenuhnd_t *p); // Handler for Admin Menu from Join Menu

// Simplified joinmenu with reduced spacing
const pmenu_t joinmenu[] = {
	{"*PLACEHOLDER*", PMENU_ALIGN_CENTER, nullptr, ""},					// 0: Title (Set by SetGameName)
	{"", PMENU_ALIGN_CENTER, nullptr, ""},								// 1: Blank Separator
	{"*PLACEHOLDER*", PMENU_ALIGN_CENTER, nullptr, ""},					// 2: Level Name (Set by SetLevelName)
	{"", PMENU_ALIGN_CENTER, nullptr, ""},								// 3: Blank Separator
	{"Join and Fight the HORDE!", PMENU_ALIGN_LEFT, HordeJoinTeam, ""}, // 4: Join Horde
	{"", PMENU_ALIGN_LEFT, nullptr, ""},								// 5: Player Count
	{"", PMENU_ALIGN_CENTER, nullptr, ""},								// 6: Blank Separator
	{"Go Spectator", PMENU_ALIGN_LEFT, GoChaseCam, ""},					// 7: Go Spectator
	{"", PMENU_ALIGN_CENTER, nullptr, ""},								// 8: Blank Separator
	{"[HOST] Admin Menu", PMENU_ALIGN_LEFT, nullptr, ""},				// 9: Host Admin Menu (visibility controlled in update)
	{"", PMENU_ALIGN_CENTER, nullptr, ""},								// 10: Blank Separator
	{"", PMENU_ALIGN_CENTER, nullptr, ""},								// 11: More spacing
	{"", PMENU_ALIGN_CENTER, nullptr, ""},								// 12: More spacing
	{"", PMENU_ALIGN_CENTER, nullptr, ""},								// 13: More spacing
	{"", PMENU_ALIGN_CENTER, nullptr, ""},								// 14: More spacing
	{"Discord: Enemy0416", PMENU_ALIGN_LEFT, nullptr, ""},				// 15: Discord Info
	{"Original Mod by Paril", PMENU_ALIGN_LEFT, nullptr, ""},			// 16: Original Mod Credit
	{"Modified by Enemy", PMENU_ALIGN_LEFT, nullptr, ""}				// 17: Modified Credit

};

// Recalculate size
constexpr size_t JOINMENU_SIZE = sizeof(joinmenu) / sizeof(pmenu_t); // Should be 18 now

// Update indices based on the simplified joinmenu structure
constexpr size_t JOINMENU_TITLE_IDX = 0;
constexpr size_t JOINMENU_LEVELNAME_IDX = 2;
constexpr size_t JOINMENU_JOIN_HORDE_IDX = 4;		// Updated index
constexpr size_t JOINMENU_JOIN_HORDE_COUNT_IDX = 5; // Updated index
constexpr size_t JOINMENU_CHASECAM_IDX = 7;			// Updated index
constexpr size_t JOINMENU_ADMIN_IDX = 9;			// Host Admin Menu
constexpr size_t JOINMENU_DISCORD_IDX = 15;			// Discord at bottom
constexpr size_t JOINMENU_ORIGINAL_CREDIT_IDX = 16; // Original Mod Credit
constexpr size_t JOINMENU_MODIFIED_CREDIT_IDX = 17; // Modified Credit

// Re-verify assertions
static_assert(JOINMENU_ORIGINAL_CREDIT_IDX < JOINMENU_SIZE, "JOINMENU_ORIGINAL_CREDIT_IDX is out of bounds for joinmenu");
static_assert(JOINMENU_MODIFIED_CREDIT_IDX < JOINMENU_SIZE, "JOINMENU_MODIFIED_CREDIT_IDX is out of bounds for joinmenu");
static_assert(JOINMENU_DISCORD_IDX < JOINMENU_SIZE, "JOINMENU_DISCORD_IDX is out of bounds for joinmenu");
static_assert(JOINMENU_CHASECAM_IDX < JOINMENU_SIZE, "JOINMENU_CHASECAM_IDX is out of bounds for joinmenu");
static_assert(JOINMENU_JOIN_HORDE_COUNT_IDX < JOINMENU_SIZE, "JOINMENU_JOIN_HORDE_COUNT_IDX is out of bounds for joinmenu");

void HordeOpenJoinMenu(edict_t *ent)
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
	if (ent->client->menu)
	{
		PMenu_Close(ent);
	}

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	PMenu_Open(ent, joinmenu, team, sizeof(joinmenu) / sizeof(pmenu_t), nullptr, HordeUpdateJoinMenu);
}

void HordeUpdateJoinMenu(edict_t *ent)
{
	// --- Safety Checks ---
	if (!ent || !ent->client || !ent->client->menu || !ent->client->menu->entries)
	{
		gi.Com_Print("Warning: HordeUpdateJoinMenu called with invalid ent/client/menu.\n");
		return;
	}
	// Check if the menu size matches what we expect
	if (ent->client->menu->num != JOINMENU_SIZE)
	{
		gi.Com_PrintFmt("Warning: HordeUpdateJoinMenu - menu size mismatch (expected {}, got {}).", JOINMENU_SIZE, ent->client->menu->num);
		return;
	}

	pmenu_t *entries = ent->client->menu->entries;

	// --- Update Static/Common Entries ---
	SetGameName(&entries[JOINMENU_TITLE_IDX]);		// Update Game Title
	SetLevelName(&entries[JOINMENU_LEVELNAME_IDX]); // Update Level Name

	// --- Horde/Coop/PvM Specific Logic ---
	if (g_horde->integer || pvm->integer || G_IsCooperative() || coop->integer || !deathmatch->integer) // Check if Horde mode, PvM, Coop, or single player is active
	{
		// Set appropriate join text based on mode
		const char *join_text;
		if (g_horde->integer && !pvm->integer)
			join_text = "Join and Fight the HORDE!";
		else if (pvm->integer)
			join_text = "Join PvM (Player vs Monster)";
		else if (G_IsCooperative() || coop->integer)
			join_text = "Join Cooperative Game";
		else
			join_text = "Start Single Player";

		Q_strlcpy(entries[JOINMENU_JOIN_HORDE_IDX].text, join_text, sizeof(entries[JOINMENU_JOIN_HORDE_IDX].text));
		entries[JOINMENU_JOIN_HORDE_IDX].SelectFunc = HordeJoinTeam;

		// Set Discord Text (ensure it's visible)
		Q_strlcpy(entries[JOINMENU_DISCORD_IDX].text, "Discord: Enemy0416", sizeof(entries[JOINMENU_DISCORD_IDX].text));
		entries[JOINMENU_DISCORD_IDX].SelectFunc = nullptr;

		// Set Original Credit text
		Q_strlcpy(entries[JOINMENU_ORIGINAL_CREDIT_IDX].text, "Original Mod by Paril", sizeof(entries[JOINMENU_ORIGINAL_CREDIT_IDX].text));
		entries[JOINMENU_ORIGINAL_CREDIT_IDX].SelectFunc = nullptr;

		// Set Modified Credit text
		Q_strlcpy(entries[JOINMENU_MODIFIED_CREDIT_IDX].text, "Modified by Enemy", sizeof(entries[JOINMENU_MODIFIED_CREDIT_IDX].text));
		entries[JOINMENU_MODIFIED_CREDIT_IDX].SelectFunc = nullptr;

		// --- Update Player Count () ---
		uint32_t horde_player_count = 0;
		for (const auto *player_ent : active_players())
		{
			if (player_ent->client->resp.ctf_team == CTF_TEAM1)
			{
				horde_player_count++;
			}
		}

		// Update the player count display entry
		Q_strlcpy(entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text, "$g_pc_playercount", sizeof(entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text));
		// Safely format player count into the 64-byte text_arg1 buffer
		// text_arg1 is 64 bytes, sufficient for any reasonable player count
		G_FmtTo(entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text_arg1, "{}", horde_player_count);
	}
	else // Not Horde or Coop mode
	{
		// Disable/Clear Horde-specific entries
		Q_strlcpy(entries[JOINMENU_JOIN_HORDE_IDX].text, "(Horde/Coop Mode Disabled)", sizeof(entries[JOINMENU_JOIN_HORDE_IDX].text));
		entries[JOINMENU_JOIN_HORDE_IDX].SelectFunc = nullptr;
		entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text[0] = '\0';
		entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text_arg1[0] = '\0';
		// Clear Discord info if not in Horde mode
		entries[JOINMENU_DISCORD_IDX].text[0] = '\0';
		entries[JOINMENU_DISCORD_IDX].SelectFunc = nullptr;
		// Clear credit entries if not in Horde mode
		entries[JOINMENU_ORIGINAL_CREDIT_IDX].text[0] = '\0';
		entries[JOINMENU_ORIGINAL_CREDIT_IDX].SelectFunc = nullptr;
		entries[JOINMENU_MODIFIED_CREDIT_IDX].text[0] = '\0';
		entries[JOINMENU_MODIFIED_CREDIT_IDX].SelectFunc = nullptr;
	}

	// --- Update Chase Cam / Spectator Option ---
	const char *chase_text = ent->client->chase_target ? "$g_pc_leave_chase_camera" : "Go Spectator";
	Q_strlcpy(entries[JOINMENU_CHASECAM_IDX].text, chase_text, sizeof(entries[JOINMENU_CHASECAM_IDX].text));
	entries[JOINMENU_CHASECAM_IDX].SelectFunc = GoChaseCam;

	// --- Update Admin Menu Option (Host Only) ---
	unsigned int playerNum = P_GetLobbyUserNum(ent);
	if (playerNum == 0)
	{
		// Player is the host - show admin menu option
		Q_strlcpy(entries[JOINMENU_ADMIN_IDX].text, "[HOST] Admin Menu", sizeof(entries[JOINMENU_ADMIN_IDX].text));
		entries[JOINMENU_ADMIN_IDX].SelectFunc = OpenAdminFromJoinMenu;
	}
	else
	{
		// Player is not the host - hide admin menu option
		entries[JOINMENU_ADMIN_IDX].text[0] = '\0';
		entries[JOINMENU_ADMIN_IDX].SelectFunc = nullptr;
	}
}

// Handler for opening Admin Menu from Join Menu
void OpenAdminFromJoinMenu(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client)
	{
		return;
	}

	// Check if player is the host
	unsigned int playerNum = P_GetLobbyUserNum(ent);
	if (playerNum != 0)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Only the host can access the admin menu.\n");
		return;
	}

	// Close the join menu and open the admin menu
	PMenu_Close(ent);
	OpenAdminMenu(ent);
}

// Keep the SetGameName and SetLevelName definitions as they were in the previous correct version.
// Definition of SetGameName
static void SetGameName(pmenu_t *p)
{
	// Safety check
	if (!p)
		return;

	if (ctf->integer)										// Check if CTF mode is active
		Q_strlcpy(p->text, "$g_pc_3wctf", sizeof(p->text)); // Use localized CTF name
	else													// Assume Horde or other modes
		// Use the constexpr string defined in g_horde.h
		Q_strlcpy(p->text, HORDE_MOD_VERSION_STRING, sizeof(p->text));
}

// Definition of SetLevelName
static void SetLevelName(pmenu_t *p)
{
	// Safety check
	if (!p)
		return;

	static char levelname[sizeof(pmenu_t::text)]; // Use size of destination buffer

	levelname[0] = '*';								 // Prefix with '*' for centered title look
	if (g_edicts[0].message && *g_edicts[0].message) // Check if worldspawn has a message (level title)
		Q_strlcpy(levelname + 1, g_edicts[0].message, sizeof(levelname) - 1);
	else if (level.mapname && *level.mapname) // Fallback to map filename
		Q_strlcpy(levelname + 1, level.mapname, sizeof(levelname) - 1);
	else
		Q_strlcpy(levelname + 1, "Unknown Level", sizeof(levelname) - 1); // Final fallback

	// levelname is already null-terminated by Q_strlcpy or initialization
	Q_strlcpy(p->text, levelname, sizeof(p->text));
}

// === Map Voting Menu ===

constexpr size_t MAX_MAPS_PER_PAGE = 11;
constexpr size_t VOTE_MENU_SIZE = MAX_MAPS_PER_PAGE + 6; // Title, blank, N maps, blank, Next, Back, Close

// vote_menu is dynamically updated by UpdateVoteMenu
static pmenu_t vote_menu[VOTE_MENU_SIZE] = {
	{"*Map Voting Menu", PMENU_ALIGN_CENTER, nullptr, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"Next", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"Back", PMENU_ALIGN_LEFT, VoteMenuHandler, ""},
	{"Close", PMENU_ALIGN_LEFT, VoteMenuHandler, ""}};
// --- Map List Categorization ---

struct map_lists_t
{
	std::vector<std::string> big_maps;
	std::vector<std::string> medium_maps;
	std::vector<std::string> small_maps;
	size_t current_page = 0;
	horde::MapSize current_category = {false, true, false}; // Default to medium
};

static map_lists_t categorized_maps;

// Respawn weapon selection menu
constexpr size_t RESPAWN_WEAPON_MENU_SIZE = 14;
static pmenu_t respawn_weapon_menu[RESPAWN_WEAPON_MENU_SIZE];

// Function to categorize the maps based on g_map_list cvar
void CategorizeMapList()
{
	categorized_maps.big_maps.clear();
	categorized_maps.medium_maps.clear();
	categorized_maps.small_maps.clear();

	const char *mlist = g_map_list->string;
	if (!mlist)
		return; // Safety check

	char *token;
	// Use a local copy to avoid modifying the cvar string directly
	std::string mlist_copy = mlist;
	const char *mlist_ptr = mlist_copy.c_str();

	while (*(token = COM_Parse(&mlist_ptr)) != '\0')
	{
		// Check if token is empty, can happen with consecutive spaces
		if (token[0] == '\0')
			continue;

		const char *map_name = token;

		// Categorize based on map size only
		horde::MapSize const mapSize = GetMapSize(map_name);

		if (mapSize.isBigMap)
		{
			if (!safe_push_back(categorized_maps.big_maps, std::string(map_name), MAX_SAFE_CONTAINER_SIZE))
			{
				gi.Com_Print("WARNING: Too many big maps\n");
			}
		}
		else if (mapSize.isSmallMap)
		{
			if (!safe_push_back(categorized_maps.small_maps, std::string(map_name), MAX_SAFE_CONTAINER_SIZE))
			{
				gi.Com_Print("WARNING: Too many small maps\n");
			}
		}
		else
		{ // isMediumMap or unknown defaults to medium
			if (!safe_push_back(categorized_maps.medium_maps, std::string(map_name), MAX_SAFE_CONTAINER_SIZE))
			{
				gi.Com_Print("WARNING: Too many medium maps\n");
			}
		}
	}
}

// --- Map Category Selection Menu ---

// Add a placeholder for the current map name
static pmenu_t map_category_menu[12]; // Dynamic menu, will be filled in OpenMapCategoryMenu
// Menu indices are now dynamic, determined by the text content in MapCategoryHandler

// Forward declaration for cooperative campaign menu
void OpenCooperativeCampaignMenu(edict_t *ent);
void CooperativeCampaignHandler(edict_t *ent, pmenuhnd_t *p);

// Cooperative campaign selection menu
static pmenu_t coop_campaign_menu[10];

void OpenCooperativeCampaignMenu(edict_t *ent)
{
	if (!ent || !ent->client)
	{
		return;
	}

	if (ent->client->menu)
	{
		PMenu_Close(ent);
	}

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	// Build the campaign menu
	memset(coop_campaign_menu, 0, sizeof(coop_campaign_menu));
	int idx = 0;

	// Title
	Q_strlcpy(coop_campaign_menu[idx].text, "Select Cooperative Campaign", sizeof(coop_campaign_menu[idx].text));
	coop_campaign_menu[idx].align = PMENU_ALIGN_CENTER;
	coop_campaign_menu[idx].SelectFunc = nullptr;
	idx++;

	// Blank line
	coop_campaign_menu[idx].text[0] = '\0';
	coop_campaign_menu[idx].align = PMENU_ALIGN_CENTER;
	coop_campaign_menu[idx].SelectFunc = nullptr;
	idx++;

	// Campaign options
	Q_strlcpy(coop_campaign_menu[idx].text, "Quake 2", sizeof(coop_campaign_menu[idx].text));
	coop_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	coop_campaign_menu[idx].SelectFunc = CooperativeCampaignHandler;
	idx++;

	Q_strlcpy(coop_campaign_menu[idx].text, "Call of the Machine", sizeof(coop_campaign_menu[idx].text));
	coop_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	coop_campaign_menu[idx].SelectFunc = CooperativeCampaignHandler;
	idx++;

	Q_strlcpy(coop_campaign_menu[idx].text, "The Reckoning", sizeof(coop_campaign_menu[idx].text));
	coop_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	coop_campaign_menu[idx].SelectFunc = CooperativeCampaignHandler;
	idx++;

	Q_strlcpy(coop_campaign_menu[idx].text, "Ground Zero", sizeof(coop_campaign_menu[idx].text));
	coop_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	coop_campaign_menu[idx].SelectFunc = CooperativeCampaignHandler;
	idx++;

	Q_strlcpy(coop_campaign_menu[idx].text, "Quake 2 N64", sizeof(coop_campaign_menu[idx].text));
	coop_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	coop_campaign_menu[idx].SelectFunc = CooperativeCampaignHandler;
	idx++;

	// Blank line
	coop_campaign_menu[idx].text[0] = '\0';
	coop_campaign_menu[idx].align = PMENU_ALIGN_CENTER;
	coop_campaign_menu[idx].SelectFunc = nullptr;
	idx++;

	// Back option
	Q_strlcpy(coop_campaign_menu[idx].text, "Back", sizeof(coop_campaign_menu[idx].text));
	coop_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	coop_campaign_menu[idx].SelectFunc = CooperativeCampaignHandler;
	idx++;

	PMenu_Open(ent, coop_campaign_menu, -1, idx, nullptr, nullptr);
}

void CooperativeCampaignHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p)
	{
		return;
	}

	const char *selected_text = p->entries[p->cur].text;

	PMenu_Close(ent);

	if (strcmp(selected_text, "Quake 2") == 0)
	{
		Q_strlcpy(ctfgame.elevel, "coop_quake2", sizeof(ctfgame.elevel));
		CTFBeginElection(ent, ELECT_COOP, "Switch to Cooperative: Quake 2?");
	}
	else if (strcmp(selected_text, "Call of the Machine") == 0)
	{
		Q_strlcpy(ctfgame.elevel, "coop_mg2", sizeof(ctfgame.elevel));
		CTFBeginElection(ent, ELECT_COOP, "Switch to Cooperative: Call of the Machine?");
	}
	else if (strcmp(selected_text, "The Reckoning") == 0)
	{
		Q_strlcpy(ctfgame.elevel, "coop_xatrix", sizeof(ctfgame.elevel));
		CTFBeginElection(ent, ELECT_COOP, "Switch to Cooperative: The Reckoning?");
	}
	else if (strcmp(selected_text, "Ground Zero") == 0)
	{
		Q_strlcpy(ctfgame.elevel, "coop_rogue", sizeof(ctfgame.elevel));
		CTFBeginElection(ent, ELECT_COOP, "Switch to Cooperative: Ground Zero?");
	}
	else if (strcmp(selected_text, "Quake 2 N64") == 0)
	{
		Q_strlcpy(ctfgame.elevel, "coop_n64", sizeof(ctfgame.elevel));
		CTFBeginElection(ent, ELECT_COOP, "Switch to Cooperative: Quake 2 N64?");
	}
	else if (strcmp(selected_text, "Back") == 0)
	{
		OpenMapCategoryMenu(ent);
	}
}

// Handler for the map category selection menu
void MapCategoryHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p)
	{
		return;
	}

	const char *selected_text = p->entries[p->cur].text;

	PMenu_Close(ent); // Close the category menu first

	if (strcmp(selected_text, "Small Maps") == 0)
	{
		categorized_maps.current_category = horde::MapSize{true, false, false};
		categorized_maps.current_page = 0;
		UpdateVoteMenu();
		PMenu_Open(ent, vote_menu, -1, VOTE_MENU_SIZE, nullptr, nullptr);
		// Maintain menu protection
		ent->client->menu_protected = true;
		ent->client->menu_protection_start = level.time;
	}
	else if (strcmp(selected_text, "Medium Maps") == 0)
	{
		categorized_maps.current_category = horde::MapSize{false, false, true};
		categorized_maps.current_page = 0;
		UpdateVoteMenu();
		PMenu_Open(ent, vote_menu, -1, VOTE_MENU_SIZE, nullptr, nullptr);
		// Maintain menu protection
		ent->client->menu_protected = true;
		ent->client->menu_protection_start = level.time;
	}
	else if (strcmp(selected_text, "Big Maps") == 0)
	{
		categorized_maps.current_category = horde::MapSize{false, true, false};
		categorized_maps.current_page = 0;
		UpdateVoteMenu();
		PMenu_Open(ent, vote_menu, -1, VOTE_MENU_SIZE, nullptr, nullptr);
		// Maintain menu protection
		ent->client->menu_protected = true;
		ent->client->menu_protection_start = level.time;
	}
	else if (strcmp(selected_text, "Vote Cooperative Mode (Beta)") == 0)
	{
		// Open the cooperative campaign selection menu
		OpenCooperativeCampaignMenu(ent);
	}
	else if (strcmp(selected_text, "Vote Horde Mode") == 0)
	{
		// Start vote to switch to horde mode
		Q_strlcpy(ctfgame.elevel, "horde_mode", sizeof(ctfgame.elevel));
		CTFBeginElection(ent, ELECT_COOP, "Switch to Horde Mode?");
	}
	else if (strcmp(selected_text, "Extend Time") == 0)
	{
		// Start manual vote to extend map time
		ctfgame.automatic_vote = false; // Mark as manual vote
		CTFBeginElection(ent, ELECT_TIME, "Extend map time by 30 minutes?");
	}
	else if (strcmp(selected_text, "Back to Horde Menu") == 0)
	{
		OpenHordeMenu(ent);
	}
	// else Close or unrecognized - just close (already done)
} // Opens the map category selection menu
void OpenMapCategoryMenu(edict_t *ent)
{
	if (!ent || !ent->client)
	{
		return;
	}

	// Close any existing menu first
	if (ent->client->menu)
	{
		PMenu_Close(ent);
	}

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	// Build the menu dynamically
	memset(map_category_menu, 0, sizeof(map_category_menu));
	int idx = 0;

	// Title
	Q_strlcpy(map_category_menu[idx].text, "Map Category Selection", sizeof(map_category_menu[idx].text));
	map_category_menu[idx].align = PMENU_ALIGN_CENTER;
	map_category_menu[idx].SelectFunc = nullptr;
	idx++;

	// Blank line
	map_category_menu[idx].text[0] = '\0';
	map_category_menu[idx].align = PMENU_ALIGN_CENTER;
	map_category_menu[idx].SelectFunc = nullptr;
	idx++;

	// Current map
	if (level.mapname && *level.mapname)
	{
		G_FmtTo(map_category_menu[idx].text, "Current: {}", level.mapname);
	}
	else
	{
		Q_strlcpy(map_category_menu[idx].text, "Current: Unknown", sizeof(map_category_menu[idx].text));
	}
	map_category_menu[idx].align = PMENU_ALIGN_CENTER;
	map_category_menu[idx].SelectFunc = nullptr;
	idx++;

	// Blank line
	map_category_menu[idx].text[0] = '\0';
	map_category_menu[idx].align = PMENU_ALIGN_CENTER;
	map_category_menu[idx].SelectFunc = nullptr;
	idx++;

	// Map categories or mode vote options
	if (g_horde->integer || pvm->integer)
	{
		// In horde/PvM mode - show map categories and cooperative vote
		Q_strlcpy(map_category_menu[idx].text, "Small Maps", sizeof(map_category_menu[idx].text));
		map_category_menu[idx].align = PMENU_ALIGN_LEFT;
		map_category_menu[idx].SelectFunc = MapCategoryHandler;
		idx++;

		Q_strlcpy(map_category_menu[idx].text, "Medium Maps", sizeof(map_category_menu[idx].text));
		map_category_menu[idx].align = PMENU_ALIGN_LEFT;
		map_category_menu[idx].SelectFunc = MapCategoryHandler;
		idx++;

		Q_strlcpy(map_category_menu[idx].text, "Big Maps", sizeof(map_category_menu[idx].text));
		map_category_menu[idx].align = PMENU_ALIGN_LEFT;
		map_category_menu[idx].SelectFunc = MapCategoryHandler;
		idx++;

		Q_strlcpy(map_category_menu[idx].text, "Vote Cooperative Mode (Beta)", sizeof(map_category_menu[idx].text));
		map_category_menu[idx].align = PMENU_ALIGN_LEFT;
		map_category_menu[idx].SelectFunc = MapCategoryHandler;
		idx++;

		// Add Extend Time option
		Q_strlcpy(map_category_menu[idx].text, "Extend Time", sizeof(map_category_menu[idx].text));
		map_category_menu[idx].align = PMENU_ALIGN_LEFT;
		map_category_menu[idx].SelectFunc = MapCategoryHandler;
		idx++;
	}
	else if (G_IsCooperative() || coop->integer)
	{
		// In cooperative mode - show option to vote for horde mode
		Q_strlcpy(map_category_menu[idx].text, "Vote Horde Mode", sizeof(map_category_menu[idx].text));
		map_category_menu[idx].align = PMENU_ALIGN_LEFT;
		map_category_menu[idx].SelectFunc = MapCategoryHandler;
		idx++;
	}

	// Blank line
	map_category_menu[idx].text[0] = '\0';
	map_category_menu[idx].align = PMENU_ALIGN_CENTER;
	map_category_menu[idx].SelectFunc = nullptr;
	idx++;

	// Back and Close
	Q_strlcpy(map_category_menu[idx].text, "Back to Horde Menu", sizeof(map_category_menu[idx].text));
	map_category_menu[idx].align = PMENU_ALIGN_LEFT;
	map_category_menu[idx].SelectFunc = MapCategoryHandler;
	idx++;

	Q_strlcpy(map_category_menu[idx].text, "Close", sizeof(map_category_menu[idx].text));
	map_category_menu[idx].align = PMENU_ALIGN_LEFT;
	map_category_menu[idx].SelectFunc = MapCategoryHandler;
	idx++;

	// Open the menu
	PMenu_Open(ent, map_category_menu, -1, idx, nullptr, nullptr);
}

// --- Map Voting Logic ---

// Opens the initial map category selection menu
void OpenVoteMenu(edict_t *ent)
{
	if (!ent || !ent->client)
	{
		return;
	}
	CategorizeMapList();	  // Ensure maps are categorized before opening the menu
	OpenMapCategoryMenu(ent); // Start with category selection - this sets menu protection
}

// Handler for map selection and navigation within the vote menu
void VoteMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	// Input validation
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	const size_t option = p->cur;
	std::vector<std::string> *current_map_list = nullptr;

	// Get current map list based on category
	if (categorized_maps.current_category.isBigMap)
	{
		current_map_list = &categorized_maps.big_maps;
	}
	else if (categorized_maps.current_category.isSmallMap)
	{
		current_map_list = &categorized_maps.small_maps;
	}
	else
	{ // Medium or default
		current_map_list = &categorized_maps.medium_maps;
	}

	// Validate map list
	if (!current_map_list || current_map_list->empty())
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "No maps available in this category.\n");
		PMenu_Close(ent);
		OpenMapCategoryMenu(ent); // Go back to category selection
		return;
	}

	const size_t map_section_start = 2;
	const size_t map_section_end = map_section_start + MAX_MAPS_PER_PAGE;
	const size_t next_button_index = map_section_end + 1;
	const size_t back_button_index = map_section_end + 2;
	const size_t close_button_index = map_section_end + 3;

	// Handle map selection (options within the map list part of the menu)
	if (option >= map_section_start && option < map_section_end)
	{
		// Calculate map index based on current page and selection
		const size_t page_offset = categorized_maps.current_page * MAX_MAPS_PER_PAGE;
		const size_t map_index = page_offset + (option - map_section_start);

		// Validate map index and menu entry text
		if (map_index >= current_map_list->size() || vote_menu[option].text[0] == '\0')
		{
			// Invalid selection (out of bounds or empty slot), do nothing or maybe close menu?
			return;
		}

		const std::string &map_name = (*current_map_list)[map_index];

		// Check if it's the current map
		if (level.mapname && Q_strcasecmp(map_name.c_str(), level.mapname) == 0)
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "Can't vote for the current map.\n");
			return; // Stay in the menu
		}

		// Initiate vote
		Q_strlcpy(ctfgame.elevel, map_name.c_str(), sizeof(ctfgame.elevel));

		char vote_msg[128]; // Safe buffer for vote message
		snprintf(vote_msg, sizeof(vote_msg), "Change map to %s?", map_name.c_str());

		// Close menu *before* starting election if successful
		if (CTFBeginElection(ent, ELECT_MAP, vote_msg))
		{
			PMenu_Close(ent);
		}
		// If election failed (e.g., already in progress), menu remains open
		return;
	}

	// Handle navigation buttons
	PMenu_Close(ent); // Close current menu before navigating

	if (option == next_button_index)
	{ // Next Page
		// Check if there is a next page
		if ((categorized_maps.current_page + 1) * MAX_MAPS_PER_PAGE < current_map_list->size())
		{
			categorized_maps.current_page++;
			UpdateVoteMenu();												  // Update the menu data
			PMenu_Open(ent, vote_menu, -1, VOTE_MENU_SIZE, nullptr, nullptr); // Reopen the updated vote menu
			// Maintain menu protection
			ent->client->menu_protected = true;
			ent->client->menu_protection_start = level.time;
		}
		else
		{
			// No next page, maybe reopen the current one? Or handle differently?
			// For now, just re-open the current (last) page
			UpdateVoteMenu();
			PMenu_Open(ent, vote_menu, -1, VOTE_MENU_SIZE, nullptr, nullptr);
			// Maintain menu protection
			ent->client->menu_protected = true;
			ent->client->menu_protection_start = level.time;
		}
	}
	else if (option == back_button_index)
	{							  // Back to Categories
		OpenMapCategoryMenu(ent); // Go back to category selection - this already sets protection
	}
	else if (option == close_button_index)
	{	// Close
		// Menu already closed
	}
	else
	{
		// Invalid option outside map list and nav buttons
		// Menu already closed
	}
}

// Updates the contents of the static vote_menu array based on the current category and page
void UpdateVoteMenu()
{
	std::vector<std::string> *current_map_list = nullptr;

	// Determine current map list and category name
	const char *category_name;
	if (categorized_maps.current_category.isBigMap)
	{
		current_map_list = &categorized_maps.big_maps;
		category_name = "Big Maps";
	}
	else if (categorized_maps.current_category.isSmallMap)
	{
		current_map_list = &categorized_maps.small_maps;
		category_name = "Small Maps";
	}
	else
	{ // Medium or default
		current_map_list = &categorized_maps.medium_maps;
		category_name = "Medium Maps";
	}

	// Safety check for empty list
	if (!current_map_list)
		return;

	// Update category title (index 0) using G_FmtTo
	G_FmtTo(vote_menu[0].text, "{}", category_name);

	// --- Calculate map indices for the current page ---
	size_t const num_maps = current_map_list->size();
	size_t start_index = categorized_maps.current_page * MAX_MAPS_PER_PAGE;

	// Handle potential invalid page (e.g., if map list changed)
	if (start_index >= num_maps && num_maps > 0)
	{
		categorized_maps.current_page = (num_maps - 1) / MAX_MAPS_PER_PAGE;
		start_index = categorized_maps.current_page * MAX_MAPS_PER_PAGE;
	}
	else if (num_maps == 0)
	{
		start_index = 0; // No maps, start index is 0
	}

	size_t end_index = std::min(start_index + MAX_MAPS_PER_PAGE, num_maps);

	// --- Clear/Fill map entries (indices 2 to 2 + MAX_MAPS_PER_PAGE - 1) ---
	size_t const map_section_start = 2;
	for (size_t i = 0; i < MAX_MAPS_PER_PAGE; ++i)
	{
		size_t const current_map_idx = start_index + i;
		size_t const menu_idx = map_section_start + i;

		if (current_map_idx < end_index)
		{
			// Fill entry
			Q_strlcpy(vote_menu[menu_idx].text, (*current_map_list)[current_map_idx].c_str(), sizeof(vote_menu[menu_idx].text));
			vote_menu[menu_idx].SelectFunc = VoteMenuHandler;
		}
		else
		{
			// Clear entry
			vote_menu[menu_idx].text[0] = '\0';
			vote_menu[menu_idx].SelectFunc = nullptr;
		}
	}

	// --- Update navigation buttons ---
	size_t const nav_button_start_index = map_section_start + MAX_MAPS_PER_PAGE + 1; // Index after maps and blank

	// "Next" button (index nav_button_start_index)
	bool const has_next_page = (start_index + MAX_MAPS_PER_PAGE) < num_maps;
	if (has_next_page)
	{
		Q_strlcpy(vote_menu[nav_button_start_index].text, "Next", sizeof(vote_menu[nav_button_start_index].text));
		vote_menu[nav_button_start_index].SelectFunc = VoteMenuHandler;
	}
	else
	{
		vote_menu[nav_button_start_index].text[0] = '\0'; // Disable "Next"
		vote_menu[nav_button_start_index].SelectFunc = nullptr;
	}

	// "Back" button (index nav_button_start_index + 1)
	Q_strlcpy(vote_menu[nav_button_start_index + 1].text, "Back", sizeof(vote_menu[nav_button_start_index + 1].text));
	vote_menu[nav_button_start_index + 1].SelectFunc = VoteMenuHandler;

	// "Close" button (index nav_button_start_index + 2)
	Q_strlcpy(vote_menu[nav_button_start_index + 2].text, "Close", sizeof(vote_menu[nav_button_start_index + 2].text));
	vote_menu[nav_button_start_index + 2].SelectFunc = VoteMenuHandler;
}

// === Inventory Display ===

void ShowInventory(edict_t *ent)
{
	// Basic validation
	if (!ent || !ent->client || (ent->svflags & SVF_BOT))
		return;

	gclient_t *cl = ent->client;
	cl->showinventory = true;

	gi.WriteByte(svc_inventory);

	// --- FIX 1: Use size_t for the loop to match container size type ---
	// Write current inventory counts up to IT_TOTAL
	for (size_t i = 0; i < IT_TOTAL; i++)
	{
		// Ensure index is valid for pers.inventory. The i < 0 check is no longer needed with size_t.
		if (i >= std::size(cl->pers.inventory))
		{
			gi.WriteShort(0); // Write 0 for out-of-bounds indices
			continue;
		}
		gi.WriteShort(cl->pers.inventory[i]);
	}

	// --- FIX 2: Use size_t for the loop to match MAX_ITEMS type ---
	// Pad the rest up to MAX_ITEMS with zeros
	for (size_t i = IT_TOTAL; i < MAX_ITEMS; i++)
	{
		gi.WriteShort(0);
	}

	gi.unicast(ent, true);
}

// === Tech Menu ===

// Define the TECHS available
static const char *tech_names[] = {
	"Strength",
	"Haste",
	"Regeneration",
	"Resistance"};
constexpr size_t NUM_TECHS = sizeof(tech_names) / sizeof(tech_names[0]);

// Menu definition for when player is already in a team
static pmenu_t tech_menu[] = {
	{"*Tech Selection", PMENU_ALIGN_CENTER, nullptr, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"Strength", PMENU_ALIGN_LEFT, TechMenuHandler, ""},
	{"Haste", PMENU_ALIGN_LEFT, TechMenuHandler, ""},
	{"Regeneration", PMENU_ALIGN_LEFT, TechMenuHandler, ""},
	{"Resistance", PMENU_ALIGN_LEFT, TechMenuHandler, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"Back", PMENU_ALIGN_LEFT, TechMenuHandler, ""}};
constexpr size_t TECH_MENU_SIZE = sizeof(tech_menu) / sizeof(pmenu_t);

// Menu definition for when player is joining (CTF_NOTEAM)
static pmenu_t tech_menustart[] = {
	{"*Tech Selection", PMENU_ALIGN_CENTER, nullptr, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"Select a TECH:", PMENU_ALIGN_LEFT, nullptr, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"Strength", PMENU_ALIGN_LEFT, TechMenuHandler, ""},
	{"Haste", PMENU_ALIGN_LEFT, TechMenuHandler, ""},
	{"Regeneration", PMENU_ALIGN_LEFT, TechMenuHandler, ""},
	{"Resistance", PMENU_ALIGN_LEFT, TechMenuHandler, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"You can change it later", PMENU_ALIGN_LEFT, nullptr, ""},
	{"On Horde Menu", PMENU_ALIGN_CENTER, nullptr, ""},
	{"", PMENU_ALIGN_CENTER, nullptr, ""},
	{"Back", PMENU_ALIGN_LEFT, TechMenuHandler, ""}};
constexpr size_t TECH_MENU_START_SIZE = sizeof(tech_menustart) / sizeof(pmenu_t);

// Opens the appropriate tech menu
void OpenTechMenu(edict_t *ent)
{
	if (!ent || !ent->client)
	{
		return;
	}

	// Always close existing menu first
	if (ent->client->menu)
	{
		PMenu_Close(ent);
	}

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	// Select menu based on team status
	const bool is_joining = (ent->client->resp.ctf_team == CTF_NOTEAM);
	const pmenu_t *menu_to_open = is_joining ? tech_menustart : tech_menu;
	const size_t menu_size = is_joining ? TECH_MENU_START_SIZE : TECH_MENU_SIZE;

	PMenu_Open(ent, menu_to_open, -1, menu_size, nullptr, nullptr);
}

// Handles selection in the tech menus
void TechMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p)
	{
		return;
	}

	// Check if "Back" was selected
	const char *selected_text = p->entries[p->cur].text;
	if (strcmp(selected_text, "Back") == 0)
	{
		PMenu_Close(ent);
		OpenHordeMenu(ent); // Return to main menu with protection
		return;
	}

	const bool is_joining = (ent->client->resp.ctf_team == CTF_NOTEAM);
	const int tech_option_offset = is_joining ? 4 : 2; // Starting index of tech options
	const int option = p->cur - tech_option_offset;

	// Validate selected option index
	if (option >= 0 && option < static_cast<int>(NUM_TECHS))
	{
		// Remove existing TECHS
		RemoveTech(ent); // Assuming RemoveTech handles removing *all* techs

		// Map the selected option text to the correct internal item ID
		int tech_index = -1;
		const char *selected_tech_name = tech_names[option];

		if (strcmp(selected_tech_name, "Strength") == 0)
		{
			tech_index = IT_TECH_STRENGTH;
		}
		else if (strcmp(selected_tech_name, "Haste") == 0)
		{
			tech_index = IT_TECH_HASTE;
		}
		else if (strcmp(selected_tech_name, "Regeneration") == 0)
		{
			tech_index = IT_TECH_REGENERATION;
		}
		else if (strcmp(selected_tech_name, "Resistance") == 0)
		{
			tech_index = IT_TECH_RESISTANCE;
		}

		// If a valid tech was selected
		if (tech_index != -1)
		{
			// Give the player the tech
		//	gi.Com_PrintFmt("PRINT: TechMenu: Giving {} tech {} via menu\n", ent->client->pers.netname, selected_tech_name);
			ent->client->pers.inventory[tech_index] = 1;
			gi.LocCenter_Print(ent, "\n\n\n\nSelected Tech: {}\n", selected_tech_name);

			// Join team if player was joining, and apply tech effect/sound
			if (is_joining)
			{
				CTFJoinTeam(ent, CTF_TEAM1); // Automatically join team 1
			}

			// Apply sound/effect based on the tech chosen
			switch (tech_index)
			{
			case IT_TECH_HASTE:
				CTFApplyHasteSound(ent);
				break;
			case IT_TECH_STRENGTH:
				CTFApplyStrengthSound(ent);
				break;
			case IT_TECH_REGENERATION:
				CTFApplyRegeneration(ent);
				break;
			case IT_TECH_RESISTANCE:
				CTFApplyResistance(ent, 0);
				break; // ApplyResistance might need a value? Assuming 0 is base.
			}
		}
	}

	PMenu_Close(ent); // Close the tech menu after selection
}

// === Misc Options Menu ===

// Handler for cycling Sentry Gun choice
void HordeMenu_SentryChoice(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client)
	{
		return;
	}

	// Cycle to the next choice
	int current_choice = static_cast<int>(ent->client->pers.sentry_gun_choice);
	current_choice = (current_choice + 1) % SENTRY_TYPE_COUNT;
	ent->client->pers.sentry_gun_choice = static_cast<sentrytype_t>(current_choice);
	ent->client->resp.sentry_gun_choice = static_cast<sentrytype_t>(current_choice); // Persist choice

	// Inform the player

	gi.LocCenter_Print(ent, "\n\n\nSentrygun Type set to: {}\n", GetSentryTypeName(ent->client->pers.sentry_gun_choice));
	// Update the menu display immediately by reopening THE MISC MENU
	// PMenu_Close is handled by OpenMiscMenu
	OpenMiscMenu(ent, p->cur); // Reopen the Misc menu to show the updated choice with cursor preserved
}

// Handler for cycling BFG mode
void HordeMenu_BFGMode(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client)
	{
		return;
	}

	// Check what modes are available
	bool has_slide = PlayerHasBenefit(ent, BenefitID::BFG_SLIDE);
	bool has_pull = PlayerHasBenefit(ent, BenefitID::BFG_GRAV_PULL);

	// If no upgrades, can't change mode
	if (!has_slide && !has_pull)
	{
		gi.LocCenter_Print(ent, "\n\n\nNo BFG upgrades available!\n");
		return;
	}

	// Cycle through available modes
	BFGMode current_mode = ent->client->pers.bfg_mode;
	BFGMode new_mode = BFGMode::NORMAL;

	if (current_mode == BFGMode::NORMAL)
	{
		if (has_slide)
		{
			new_mode = BFGMode::SLIDE;
		}
		else if (has_pull)
		{
			new_mode = BFGMode::GRAV_PULL;
		}
	}
	else if (current_mode == BFGMode::SLIDE)
	{
		if (has_pull)
		{
			new_mode = BFGMode::GRAV_PULL;
		}
		else
		{
			new_mode = BFGMode::NORMAL;
		}
	}
	else
	{ // GRAV_PULL
		new_mode = BFGMode::NORMAL;
	}

	ent->client->pers.bfg_mode = new_mode;
	gi.LocCenter_Print(ent, "\n\n\nBFG Mode set to: {}\n", GetBFGModeName(new_mode));
	OpenMiscMenu(ent, p->cur); // Reopen to show updated mode with cursor preserved
}

void HordeMenu_SpecialWave(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client)
	{
		return;
	}

	// Cycle through the special wave types (1=Barrels, 2=Hook, 3=Bombspell, 4=Bombspell Area, 5=Teleport Fwd, 6=Fireball)
	int current_type = g_special_key->integer;
	int new_type = (current_type % 6) + 1; // Cycle 1->2->3->4->5->6->1

	// Update the cvar
	gi.cvar_forceset("g_special_key", G_Fmt("{}", new_type).data());

	gi.LocCenter_Print(ent, "\n\n\nSpecial key [L] set to: {}\n", GetSpecialWaveName(new_type));
	OpenMiscMenu(ent, p->cur); // Reopen to show updated choice with cursor preserved
}

void HordeMenu_StroggPreference(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client)
	{
		return;
	}

	// Cycle morph preference (0=Brain, 1=Flyer)
	ent->client->pers.morph_preference = (ent->client->pers.morph_preference + 1) % 2;

	gi.LocCenter_Print(ent, "\n\n\nStrogg preference set to: {}\n", GetMorphTypeName(ent->client->pers.morph_preference));
	OpenMiscMenu(ent, p->cur); // Reopen to show updated choice with cursor preserved
}

void HordeMenu_StroggificationCommand(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client)
	{
		return;
	}

	// Check if player is morphed
	if (IsMorphed(ent))
	{
		// Unmorph back to human
		RestoreMorphed(ent);
		gi.LocCenter_Print(ent, "\n\n\nTransformed back to human form!\n");
		OpenMiscMenu(ent, p->cur); // Reopen to show updated option with cursor preserved
		return;
	}

	// Transform based on preference (0=Brain, 1=Flyer)
	if (ent->client->pers.morph_preference == 0)
	{
		Cmd_PlayerToBrain_f(ent);
	}
	else
	{
		Cmd_PlayerToFlyer_f(ent);
	}

	OpenMiscMenu(ent, p->cur); // Reopen to show updated option with cursor preserved
}

// Handler for the Misc submenu
void MiscMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0 || p->cur >= p->num)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	const pmenu_t *selected_entry = &p->entries[p->cur];
	const char *selected_text = selected_entry->text;
	bool shouldCloseMenu = true; // Default to closing the menu

	// Use strncmp for options that might have counts appended or dynamic text
	if (strncmp(selected_text, "Remove Stroggs", strlen("Remove Stroggs")) == 0)
	{
		// Remove Strogg summoner bases (which will also kill their spawned monsters)
		Cmd_RemoveStrogg_f(ent);
		// Message handled internally, menu should close.
	}
	else if (strncmp(selected_text, "Remove Lasers", strlen("Remove Lasers")) == 0)
	{
		Cmd_RemoveLaser_f(ent);
		// Message handled internally, menu should close.
	}
	else if (strncmp(selected_text, "Remove Sentry Gun", strlen("Remove Sentry Gun")) == 0)
	{
		Cmd_RemoveSentry_f(ent);
		// Message handled internally, menu should close.
	}
	else if (strncmp(selected_text, "Remove Barrels", strlen("Remove Barrels")) == 0)
	{
		Cmd_RemoveBarrel_f(ent);
		// Message handled internally, menu should close.
	}
	// **** Check Special Wave selection ****
	else if (strncmp(selected_text, "Special key [L]", strlen("Special key [L]")) == 0)
	{
		HordeMenu_SpecialWave(ent, p); // Call the dedicated handler
		shouldCloseMenu = false;	   // Don't close, HordeMenu_SpecialWave will reopen Misc Menu
	}
	// **** Check Sentry Type selection ****
	else if (strncmp(selected_text, "Sentry Type:", strlen("Sentry Type:")) == 0)
	{
		HordeMenu_SentryChoice(ent, p); // Call the dedicated handler
		shouldCloseMenu = false;		// Don't close, HordeMenu_SentryChoice will reopen Misc Menu
	}
	// **** Check BFG Mode selection ****
	else if (strncmp(selected_text, "BFG Mode:", strlen("BFG Mode:")) == 0)
	{
		HordeMenu_BFGMode(ent, p); // Call the dedicated handler
		shouldCloseMenu = false;   // Don't close, HordeMenu_BFGMode will reopen Misc Menu
	}
	// **** Check Beta: Strogg preference selection ****
	else if (strncmp(selected_text, "[Beta] Strogg", strlen("[Beta] Strogg")) == 0)
	{
		HordeMenu_StroggPreference(ent, p); // Call the preference handler
		shouldCloseMenu = false;			// Don't close, will reopen Misc Menu
	}
	// **** Check Stroggification command (morph/unmorph) ****
	else if (strcmp(selected_text, "Stroggificate me!") == 0 ||
			 strcmp(selected_text, "I hate stroggs!") == 0)
	{
		HordeMenu_StroggificationCommand(ent, p); // Call the morph command handler
		shouldCloseMenu = false;				  // Don't close, will reopen Misc Menu
	}
	// **** END Check ****
	else if (strcmp(selected_text, "Back") == 0)
	{
		PMenu_Close(ent);
		OpenHordeMenu(ent);		 // Go back to the main menu
		shouldCloseMenu = false; // Already handled closing/opening
	}
	else if (strcmp(selected_text, "Close") == 0)
	{
		// No specific action, default close behavior is fine.
	}
	else
	{
		// Clicked on title or separator - don't close
		shouldCloseMenu = false;
	}

	// Close the menu if required and if it still exists
	if (shouldCloseMenu && ent->client && ent->client->menu)
	{
		PMenu_Close(ent);
	}
}

// Handler for respawn weapon selection menu
void RespawnWeaponMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	const size_t option = p->cur;
	const char *selected_text = respawn_weapon_menu[option].text;

	if (!selected_text || !selected_text[0])
	{
		return;
	}

	// Handle "Back to Main Menu"
	if (strcmp(selected_text, "Back to Main Menu") == 0)
	{
		PMenu_Close(ent);
		OpenHordeMenu(ent);
		return;
	}

	// Handle "Current:" line (skip)
	if (strncmp(selected_text, "Current:", 8) == 0)
	{
		return;
	}

	// Otherwise, it's a weapon selection
	Character_SetRespawnWeapon(ent, selected_text);
	gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Respawn weapon set to: {}\n", selected_text);

	// Reopen the menu to show updated selection
	PMenu_Close(ent);
	OpenRespawnWeaponMenu(ent);
}

// Creates and opens the respawn weapon selection menu
void OpenRespawnWeaponMenu(edict_t *ent)
{
	if (!ent || !ent->client)
	{
		return;
	}

	if (ent->client->menu)
	{
		PMenu_Close(ent);
	}

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	// Clear menu
	memset(respawn_weapon_menu, 0, sizeof(respawn_weapon_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr)
	{
		if (count < RESPAWN_WEAPON_MENU_SIZE)
		{
			Q_strlcpy(respawn_weapon_menu[count].text, text, sizeof(respawn_weapon_menu[count].text));
			respawn_weapon_menu[count].align = align;
			respawn_weapon_menu[count].SelectFunc = func;
			count++;
		}
	};

	// Title
	add_entry("*Set Respawn Weapon*", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current respawn weapon
	char current_weapon_display[64];
	const char *current_weapon = Character_GetRespawnWeapon(ent);
	snprintf(current_weapon_display, sizeof(current_weapon_display), "Current: %s", current_weapon);
	add_entry(current_weapon_display, PMENU_ALIGN_LEFT);
	add_entry("", PMENU_ALIGN_CENTER);

	// Weapon options
	add_entry("Blaster", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	add_entry("Shotgun", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	add_entry("Super Shotgun", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	add_entry("Machinegun", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	add_entry("Chaingun", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	add_entry("Grenade Launcher", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	add_entry("Rocket Launcher", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	add_entry("HyperBlaster", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	add_entry("Railgun", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	add_entry("BFG10K", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);

	// Back option
	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("Back to Main Menu", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);

	PMenu_Open(ent, respawn_weapon_menu, -1, count, nullptr, nullptr);
}

// Creates and opens the Misc submenu
void OpenMiscMenu(edict_t *ent, int cursor_position)
{
	if (!ent || !ent->client)
	{
		return;
	}

	if (ent->client->menu)
	{
		PMenu_Close(ent);
	}

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	static pmenu_t entries[15]; // Increased from 14 to 15 for extra strogg option
	memset(entries, 0, sizeof(entries));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr)
	{
		if (count < static_cast<int>(std::size(entries)))
		{
			Q_strlcpy(entries[count].text, text, sizeof(entries[count].text));
			entries[count].align = align;
			entries[count].SelectFunc = func;
			count++;
		}
		else
		{
			gi.Com_Print("Warning: OpenMiscMenu exceeded static entry buffer size.\n");
		}
	};

	add_entry("*Misc Options*", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER); // Separator

	// --- Special Wave Selection ---
	add_entry(G_Fmt("Special key [L]: [{}]", GetSpecialWaveName(g_special_key->integer)).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);

	// --- Sentry Gun Choice ---
	add_entry(G_Fmt("Sentry Type: [{}]", GetSentryTypeName(ent->client->pers.sentry_gun_choice)).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);

	// --- BFG Mode Selection (only if player has BFG upgrades) ---
	bool has_bfg_upgrades = PlayerHasBenefit(ent, BenefitID::BFG_SLIDE) || PlayerHasBenefit(ent, BenefitID::BFG_GRAV_PULL);
	if (has_bfg_upgrades)
	{
		add_entry(G_Fmt("BFG Mode: [{}]", GetBFGModeName(ent->client->pers.bfg_mode)).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	// --- Conditional Remove Options (MODIFIED) ---

	// Count actual summoned monsters (excluding bases, lasers, and barrels)
	// This includes: strogg monsters, medic-revived entities, and any other summoned creatures
	int strogg_count = 0;

	// Count all summoned entities (excluding lasers, barrels, and strogg bases)
	for (int i = 1; i < static_cast<int>(globals.num_edicts); i++)
	{
		edict_t *check = &g_edicts[i];
		if (check && check->inuse && check->teammaster == ent && check->chain)
		{
			// Exclude bases, lasers, and barrels from this count using the special type system
			if (!horde::IsSpecialType(check, horde::SpecialEntityTypeID::STROGG_SUMMONER) &&
				!horde::IsSpecialType(check, horde::SpecialEntityTypeID::LASER_EMITTER) &&
				!horde::IsSpecialType(check, horde::SpecialEntityTypeID::LASER_BEAM) &&
				!horde::IsSpecialType(check, horde::SpecialEntityTypeID::BARREL))
			{
				strogg_count++;
			}
		}
	}

	if (strogg_count > 0)
	{
		add_entry(G_Fmt("Remove Stroggs ({})", strogg_count).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	// Get the laser count directly from the client's respawn data.
	int laser_count = ent->client->resp.num_lasers;
	if (laser_count > 0)
	{
		// Use G_Fmt to format the menu entry with the current count.
		add_entry(G_Fmt("Remove Lasers ({})", laser_count).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	// --- Sentry Gun Removal (Unchanged) ---
	int sentry_count = ent->client->resp.num_sentries;
	if (sentry_count > 0)
	{
		add_entry(G_Fmt("Remove Sentry Gun ({})", sentry_count).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	// --- Barrel Removal ---
	int barrel_count = ent->client->resp.num_barrels;
	if (barrel_count > 0)
	{
		add_entry(G_Fmt("Remove Barrels ({}/{})", barrel_count, BarrelConstants::MAX_BARRELS_PER_PLAYER()).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	add_entry("", PMENU_ALIGN_CENTER); // Separator

	// --- STROGG OPTIONS (Prominent placement near bottom) ---
	// --- Beta: Strogg Preference Selection (always visible) ---
	add_entry(G_Fmt("[Beta] Strogg [{}]", GetMorphTypeName(ent->client->pers.morph_preference)).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);

	// --- Stroggification Command (morph/unmorph) ---
	if (IsMorphed(ent))
	{
		add_entry("I hate stroggs!", PMENU_ALIGN_LEFT, MiscMenuHandler);
	}
	else
	{
		add_entry("Stroggificate me!", PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	add_entry("", PMENU_ALIGN_CENTER); // Separator
	add_entry("Back", PMENU_ALIGN_LEFT, MiscMenuHandler);
	add_entry("Close", PMENU_ALIGN_LEFT, MiscMenuHandler);

	PMenu_Open(ent, entries, cursor_position, count, nullptr, nullptr);
}

// === HUD Options Menu ===

constexpr size_t HUD_MENU_MAX_ENTRIES = 8;
constexpr size_t HUD_TEXT_BUFFER_SIZE = 64;

// Opens the HUD options menu
void OpenHUDMenu(edict_t *ent)
{
	if (!ent || !ent->client)
	{
		return;
	}

	// Close any existing menu first
	if (ent->client->menu)
	{
		PMenu_Close(ent);
	}

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	// Use CreateHUDMenu to generate and open the menu
	CreateHUDMenu(ent);
}

// Creates and returns the HUD menu handle (used by OpenHUDMenu and HUDMenuHandler)
pmenuhnd_t *CreateHUDMenu(edict_t *ent)
{
	static pmenu_t entries[HUD_MENU_MAX_ENTRIES];
	memset(entries, 0, sizeof(entries));

	size_t count = 0;

	// Title and spacing
	if (count < HUD_MENU_MAX_ENTRIES)
	{
		Q_strlcpy(entries[count].text, "*HUD Options", sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_CENTER;
		entries[count++].SelectFunc = nullptr;
	}
	if (count < HUD_MENU_MAX_ENTRIES)
	{
		entries[count].text[0] = '\0'; // Blank separator
		entries[count].align = PMENU_ALIGN_CENTER;
		entries[count++].SelectFunc = nullptr;
	}

	// ID Toggle using G_FmtTo
	if (count < HUD_MENU_MAX_ENTRIES)
	{
		G_FmtTo(entries[count].text, "Enable/Disable ID [{}]", ent->client->pers.id_state ? "ON" : "OFF");
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// ID-DMG Toggle using G_FmtTo
	if (count < HUD_MENU_MAX_ENTRIES)
	{
		G_FmtTo(entries[count].text, "Enable/Disable ID-DMG [{}]", ent->client->pers.iddmg_state ? "ON" : "OFF");
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// Spacing
	if (count < HUD_MENU_MAX_ENTRIES)
	{
		entries[count].text[0] = '\0';
		entries[count].align = PMENU_ALIGN_CENTER;
		entries[count++].SelectFunc = nullptr;
	}

	// Back to Horde Menu
	if (count < HUD_MENU_MAX_ENTRIES)
	{
		Q_strlcpy(entries[count].text, "Back to Horde Menu", sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// Close
	if (count < HUD_MENU_MAX_ENTRIES)
	{
		Q_strlcpy(entries[count].text, "Close", sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	return PMenu_Open(ent, entries, -1, count, nullptr, nullptr);
}

// Updates the dynamic text in the HUD menu (ON/OFF status)
void UpdateHUDMenu(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || !p->entries || p->num < 4)
	{
		return;
	}

	// Update the menu entries using G_Fmt (PMenu_UpdateEntry takes const char*)
	PMenu_UpdateEntry(p->entries + 2, G_Fmt("Enable/Disable ID [{}]", ent->client->pers.id_state ? "ON" : "OFF").data(), PMENU_ALIGN_LEFT, HUDMenuHandler);
	PMenu_UpdateEntry(p->entries + 3, G_Fmt("Enable/Disable ID-DMG [{}]", ent->client->pers.iddmg_state ? "ON" : "OFF").data(), PMENU_ALIGN_LEFT, HUDMenuHandler);

	PMenu_Update(ent);
}

// Handles selections in the HUD options menu
void HUDMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	constexpr int ID_TOGGLE_OPTION = 2;
	constexpr int IDDMG_TOGGLE_OPTION = 3;
	constexpr int BACK_OPTION = 5;
	constexpr int CLOSE_OPTION = 6;

	const int option = p->cur;

	switch (option)
	{
	case ID_TOGGLE_OPTION:
	case IDDMG_TOGGLE_OPTION:
	{
		bool &state_to_toggle = (option == ID_TOGGLE_OPTION) ? ent->client->pers.id_state : ent->client->pers.iddmg_state;
		state_to_toggle = !state_to_toggle;

		// Use G_Fmt for the message
		gi.LocCenter_Print(ent, G_Fmt("\n\n\n{} state toggled to {}\n",
									  (option == ID_TOGGLE_OPTION) ? "ID" : "ID-DMG",
									  state_to_toggle ? "ON" : "OFF")
									.data());

		UpdateHUDMenu(ent, p);
		break;
	}
	case BACK_OPTION:
		PMenu_Close(ent);
		if (ent->inuse)
		{
			OpenHordeMenu(ent);
		}
		break;
	case CLOSE_OPTION:
		PMenu_Close(ent);
		break;
	default:
		PMenu_Close(ent);
		break;
	}
}

// Periodically checks if players have the HUD menu open and updates it
void CheckAndUpdateMenus()
{
	for (auto const *player : active_players())
	{ // Use range-based for loop
		// Basic validation
		if (!player || !player->client || !player->client->menu || !player->client->menu->entries)
		{
			continue;
		}

		// Check if the player is in the HUD menu by comparing the title text
		// Using strcmp for C-style string comparison
		if (strcmp(player->client->menu->entries[0].text, "*HUD Options") == 0)
		{
			UpdateHUDMenu(const_cast<edict_t *>(player), player->client->menu); // Pass non-const edict_t
																				// gi.unicast(const_cast<edict_t*>(player), true); // Update might already handle this
		}
		// Add checks for other dynamic menus if needed
	}
}

// === Admin Menu ===

void OpenAdminMenu(edict_t *ent)
{
	if (!ent || !ent->client)
	{
		return;
	}

	// Check if player is the host (player 0)
	unsigned int playerNum = P_GetLobbyUserNum(ent);
	if (playerNum != 0)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Only the host can access the admin menu.\n");
		return;
	}

	if (ent->client->menu)
	{
		PMenu_Close(ent);
	}

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	static pmenu_t admin_menu[15];
	memset(admin_menu, 0, sizeof(admin_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr)
	{
		if (count < static_cast<int>(std::size(admin_menu)))
		{
			Q_strlcpy(admin_menu[count].text, text, sizeof(admin_menu[count].text));
			admin_menu[count].align = align;
			admin_menu[count].SelectFunc = func;
			count++;
		}
	};

	add_entry("*Admin Menu*", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("Add 5 Ability Points (All)", PMENU_ALIGN_LEFT, AdminMenuHandler);
	add_entry("Add 5 Weapon Points (All)", PMENU_ALIGN_LEFT, AdminMenuHandler);
	add_entry("Add 10 Points (All)", PMENU_ALIGN_LEFT, AdminMenuHandler);
	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("Give All Weapons", PMENU_ALIGN_LEFT, AdminMenuHandler);
	// God mode commented out - can break game balance
	// add_entry("Give God Mode (All)", PMENU_ALIGN_LEFT, AdminMenuHandler);
	add_entry("Heal All Players", PMENU_ALIGN_LEFT, AdminMenuHandler);
	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("Skip to Next Wave", PMENU_ALIGN_LEFT, AdminMenuHandler);
	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("Back", PMENU_ALIGN_LEFT, AdminMenuHandler);
	add_entry("Close", PMENU_ALIGN_LEFT, AdminMenuHandler);

	PMenu_Open(ent, admin_menu, -1, count, nullptr, nullptr);
}

void AdminMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	// Verify still the host
	unsigned int playerNum = P_GetLobbyUserNum(ent);
	if (playerNum != 0)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Admin privileges lost.\n");
		PMenu_Close(ent);
		return;
	}

	const pmenu_t *selected = &p->entries[p->cur];
	const char *text = selected->text;
	bool shouldClose = false; // Keep menu open by default

	if (strcmp(text, "Add 5 Ability Points (All)") == 0)
	{
		const char *adminName = GetPlayerName(ent);
		for (auto player : active_players())
		{
			if (player->client && (player->client->resp.ctf_team == CTF_TEAM1 ||
								   G_IsCooperative() || coop->integer || !deathmatch->integer))
			{
				player->client->pers.ability_points += 5;
				player->client->pers.admin_bonus_ability_points += 5; // Track admin-given points
				gi.LocClient_Print(player, PRINT_HIGH, "{} granted you 5 ability points!\n", adminName);
			}
		}
		gi.LocClient_Print(ent, PRINT_HIGH, "Gave 5 ability points to all players.\n");
	}
	else if (strcmp(text, "Add 5 Weapon Points (All)") == 0)
	{
		const char *adminName = GetPlayerName(ent);
		for (auto player : active_players())
		{
			if (player->client && (player->client->resp.ctf_team == CTF_TEAM1 ||
								   G_IsCooperative() || coop->integer || !deathmatch->integer))
			{
				player->client->pers.weapon_points += 5;
				player->client->pers.admin_bonus_weapon_points += 5; // Track admin-given points
				gi.LocClient_Print(player, PRINT_HIGH, "{} granted you 5 weapon points!\n", adminName);
			}
		}
		gi.LocClient_Print(ent, PRINT_HIGH, "Gave 5 weapon points to all players.\n");
	}
	else if (strcmp(text, "Add 10 Points (All)") == 0)
	{
		const char *adminName = GetPlayerName(ent);
		for (auto player : active_players())
		{
			if (player->client && (player->client->resp.ctf_team == CTF_TEAM1 ||
								   G_IsCooperative() || coop->integer || !deathmatch->integer))
			{
				player->client->pers.ability_points += 10;
				player->client->pers.weapon_points += 10;
				player->client->pers.admin_bonus_ability_points += 10; // Track admin-given points
				player->client->pers.admin_bonus_weapon_points += 10;  // Track admin-given points
				gi.LocClient_Print(player, PRINT_HIGH, "{} granted you 10 ability and weapon points!\n", adminName);
			}
		}
		gi.LocClient_Print(ent, PRINT_HIGH, "Gave 10 points of each type to all players.\n");
	}
	else if (strcmp(text, "Give All Weapons") == 0)
	{
		const char *adminName = GetPlayerName(ent);
		for (auto player : active_players())
		{
			if (player->client && (player->client->resp.ctf_team == CTF_TEAM1 ||
								   G_IsCooperative() || coop->integer || !deathmatch->integer))
			{
				// Give all weapons
				for (size_t i = IT_WEAPON_SHOTGUN; i <= IT_WEAPON_BFG; i++)
				{
					player->client->pers.inventory[i] = 1;
				}
				// Give ammo
				player->client->pers.inventory[IT_AMMO_SHELLS] = 200;
				player->client->pers.inventory[IT_AMMO_BULLETS] = 300;
				player->client->pers.inventory[IT_AMMO_GRENADES] = 100;
				player->client->pers.inventory[IT_AMMO_ROCKETS] = 100;
				player->client->pers.inventory[IT_AMMO_CELLS] = 300;
				player->client->pers.inventory[IT_AMMO_SLUGS] = 100;
				gi.LocClient_Print(player, PRINT_HIGH, "{} gave you all weapons!\n", adminName);
			}
		}
		gi.LocClient_Print(ent, PRINT_HIGH, "Gave all weapons to all players.\n");
	}
	// God mode option removed - can break game balance
	/*else if (strcmp(text, "Give God Mode (All)") == 0) {
		const char* adminName = GetPlayerName(ent);
		for (auto player : active_players()) {
			if (player->client && player->client->resp.ctf_team == CTF_TEAM1) {
				player->flags |= FL_GODMODE;
				gi.LocClient_Print(player, PRINT_HIGH, "{} enabled god mode for you!\n", adminName);
			}
		}
		gi.LocClient_Print(ent, PRINT_HIGH, "Enabled god mode for all players.\n");
	}*/
	else if (strcmp(text, "Heal All Players") == 0)
	{
		const char *adminName = GetPlayerName(ent);
		for (auto player : active_players())
		{
			if (player->client && (player->client->resp.ctf_team == CTF_TEAM1 ||
								   G_IsCooperative() || coop->integer || !deathmatch->integer))
			{
				player->health = player->max_health;
				player->client->pers.inventory[IT_ARMOR_BODY] = 200;
				gi.LocClient_Print(player, PRINT_HIGH, "{} healed you!\n", adminName);
			}
		}
		gi.LocClient_Print(ent, PRINT_HIGH, "Healed all players.\n");
	}
	else if (strcmp(text, "Skip to Next Wave") == 0)
	{
		if (g_horde->integer || pvm->integer)
		{
			// Kill all AI to trigger next wave
			extern void Cmd_Kill_AI_f(edict_t * ent);
			Cmd_Kill_AI_f(ent);
			gi.LocClient_Print(ent, PRINT_HIGH, "Killed all AI - advancing to next wave.\n");
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "This only works in Horde/PvM mode.\n");
		}
	}
	else if (strcmp(text, "Back") == 0)
	{
		PMenu_Close(ent);
		OpenHordeMenu(ent);
		shouldClose = false;
	}
	else if (strcmp(text, "Close") == 0)
	{
		shouldClose = true; // Actually close on Close option
	}

	if (shouldClose && ent->client && ent->client->menu)
	{
		PMenu_Close(ent);
	}
}

// === Main Horde Menu ===

// Handles selections in the main Horde menu
void HordeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || !p->entries || p->cur < 0 || p->cur >= p->num)
		return; // Added bounds check for p->cur

	const int option = p->cur;							 // Keep option index if needed elsewhere, but don't rely on it for branching here
	const pmenu_t *selected_entry = &p->entries[option]; // Get the selected entry struct
	bool shouldCloseMenu = true;

	// Use text comparison for actions where index might vary
	const char *selected_text = selected_entry->text;

	// --- Action Handling based on Text ---

	// Check for Show Objectives (coop only)
	if (strcmp(selected_text, "Show Objectives") == 0)
	{
		PMenu_Close(ent); // Close menu first
		Cmd_Help_f(ent);  // Show objectives screen
		return;			  // Exit early since we already closed the menu
	}
	// Check for Admin Menu (host only)
	if (strcmp(selected_text, "[HOST] Admin Menu") == 0)
	{
		OpenAdminMenu(ent);
		shouldCloseMenu = false;
	}
	// Check for "Go Spectator/AFK"
	if (strcmp(selected_text, "Go Spectator/AFK") == 0)
	{
		CTFObserver(ent);
		shouldCloseMenu = false; // CTFObserver might handle closing
	}
	// Check for "Upgrade Menu"
	else if (strncmp(selected_text, "Upgrade Menu", 12) == 0)
	{
		shouldCloseMenu = true; // Close main menu first
		PMenu_Close(ent);		// Close now before opening upgrade menu
		OpenUpgradeMenu(ent);	// This will set protection and open new menu
		return;					// Exit early since we already closed
	}
	// Check for "Misc Options"
	else if (strcmp(selected_text, "Misc Options") == 0)
	{
		OpenMiscMenu(ent);
		shouldCloseMenu = false;
	}
	// Vote Map
	else if (ctfgame.election == ELECT_NONE && strcmp(selected_text, "Vote Map") == 0)
	{
		OpenVoteMenu(ent);
		shouldCloseMenu = false;
	}
	// Vote Yes
	else if (ctfgame.election != ELECT_NONE && strcmp(selected_text, "Vote Yes") == 0)
	{
		CTFVoteYes(ent);
		shouldCloseMenu = false; // Keep menu open after voting
	}
	// Vote No
	else if (ctfgame.election != ELECT_NONE && strcmp(selected_text, "Vote No") == 0)
	{
		CTFVoteNo(ent);
		shouldCloseMenu = false; // Keep menu open after voting
	}
	// HUD Options
	else if (strcmp(selected_text, "HUD Options") == 0)
	{
		OpenHUDMenu(ent);
		shouldCloseMenu = false;
	}
	// Set Respawn Weapon
	else if (strcmp(selected_text, "Set Respawn Weapon") == 0)
	{
		OpenRespawnWeaponMenu(ent);
		shouldCloseMenu = false;
	}
	// Swap Tech
	else if (strcmp(selected_text, "Swap Tech") == 0)
	{
		OpenTechMenu(ent);
		shouldCloseMenu = false;
	}
	// Show Inventory
	else if (strcmp(selected_text, "Show Inventory") == 0)
	{
		PMenu_Close(ent); // Close menu first to prevent overlay
		ShowInventory(ent);
		return; // Exit early since we already closed the menu
	}
	// Close Button
	else if (strcmp(selected_text, "Close") == 0)
	{
		// No action needed, handled by shouldCloseMenu = true (default)
	}
	// --- Default Case (Clicked on non-actionable item like title, blank, vote question) ---
	else
	{
		shouldCloseMenu = false; // Don't close menu for non-actionable items
	}

	// Close the menu if required and if it still exists
	if (shouldCloseMenu && ent->client && ent->client->menu)
	{ // Add ent->client check
		PMenu_Close(ent);
	}
}

// Creates and opens the main Horde menu
// Forward declaration for the handler function if not already visible
void HordeMenuHandler(edict_t *ent, pmenuhnd_t *p);

// Creates and opens the main Horde menu
pmenuhnd_t *CreateHordeMenu(edict_t *ent)
{
	if (!ent || !ent->client)
	{
		return nullptr;
	}

	if (ent->client->menu)
	{
		PMenu_Close(ent);
	}

	static pmenu_t entries[20];
	memset(entries, 0, sizeof(entries));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr)
	{
		if (count < static_cast<int>(std::size(entries)))
		{
			Q_strlcpy(entries[count].text, text, sizeof(entries[count].text));
			entries[count].align = align;
			entries[count].SelectFunc = func;
			count++;
		}
		else
		{
			gi.Com_Print("Warning: CreateHordeMenu exceeded static entry buffer size.\n");
		}
	};

	add_entry(HORDE_MOD_VERSION_STRING, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Add Show Objectives option if in coop mode (first option after joining)
	if (coop->integer)
	{
		add_entry("Show Objectives", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}

	// Add Admin Menu option if player is host
	unsigned int playerNum = P_GetLobbyUserNum(ent);
	if (playerNum == 0)
	{
		add_entry("[HOST] Admin Menu", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}

	add_entry("Go Spectator/AFK", PMENU_ALIGN_LEFT, HordeMenuHandler);

	if (ctfgame.election == ELECT_NONE)
	{
		add_entry("Vote Map", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}
	else
	{
		// Use G_Fmt for the vote question
		add_entry(G_Fmt("Vote: {}", ctfgame.emsg).data(), PMENU_ALIGN_CENTER, nullptr);
		add_entry("Vote Yes", PMENU_ALIGN_LEFT, HordeMenuHandler);
		add_entry("Vote No", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}
	add_entry("", PMENU_ALIGN_CENTER);

	// Add Upgrade Menu with total points display
	int total_points = ent->client->pers.ability_points + ent->client->pers.weapon_points;
	// Use string_view directly, no need for std::string allocation
	auto upgrade_text = G_Fmt("Upgrade Menu (Points: {})", total_points);
	add_entry(upgrade_text.data(), PMENU_ALIGN_LEFT, HordeMenuHandler);

	add_entry("Misc Options", PMENU_ALIGN_LEFT, HordeMenuHandler);
	add_entry("HUD Options", PMENU_ALIGN_LEFT, HordeMenuHandler);

	if (pvm->integer)
	{
		add_entry("Set Respawn Weapon", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}

	if (g_horde && !pvm->integer)
	{
		add_entry("Swap Tech", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}

	add_entry("Show Inventory", PMENU_ALIGN_LEFT, HordeMenuHandler);

	add_entry("", PMENU_ALIGN_CENTER);

	add_entry("Close", PMENU_ALIGN_LEFT, HordeMenuHandler);

	return PMenu_Open(ent, entries, -1, count, nullptr, nullptr);
}

// Entry point to open the main Horde menu
void OpenHordeMenu(edict_t *ent) noexcept
{
	if (!ent || !ent->client)
		return;

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	CreateHordeMenu(ent); // Create and open the menu
}

/////////////////////////////////////////////
//////SCOREBOARD//////////
/////////////////////////////////////////////

// Make sure we're using the correct fmt namespace for format
// namespace fmt_game = fmt;

// Constants

constexpr size_t MAX_PLAYERS_TO_DISPLAY = 16;
constexpr int PLAYER_Y_START = 42;
constexpr int PLAYER_Y_SPACING = 8;
constexpr int LAYOUT_SAFETY_MARGIN = 50;

/**
 *  StringBuilder
 * Helper class for efficient string concatenation
 */
class StringBuilder
{
private:
	std::string buffer;
	size_t max_size;

public:
	explicit StringBuilder(size_t reserved_size = 256)
	{
		// Clamp reserved size to prevent overflow
		reserved_size = std::min(reserved_size, MAX_STRING_BUILD_SIZE);
		max_size = MAX_STRING_BUILD_SIZE;
		try
		{
			buffer.reserve(reserved_size);
		}
		catch (const std::bad_alloc &)
		{
			gi.Com_Print("WARNING: Failed to reserve string builder memory\n");
		}
	}

	StringBuilder &append(std::string_view text)
	{
		// Use safe append with size checking
		if (!safe_string_append(buffer, text, max_size))
		{
			if (developer && developer->integer)
			{
				gi.Com_Print("WARNING: StringBuilder reached max size, truncating\n");
			}
		}
		return *this;
	}

	std::string str() const
	{
		return buffer;
	}

	size_t size() const
	{
		return buffer.size();
	}
};

/**
 * PlayerScore
 * Contains player score information for the scoreboard
 */
struct PlayerScore
{
	unsigned int index;
	int score;
	int ping;
	bool is_dead;

	// Sort players by score in descending order
	bool operator>(const PlayerScore &other) const
	{
		return score > other.score;
	}
};

/**
 * ScoreboardLayout
 * Handles scoreboard layout generation
 */
class ScoreboardLayout {
private:
	StringBuilder layout_builder;
	const edict_t* ent;
	std::vector<PlayerScore> team_players;
	std::vector<PlayerScore> spectators;
	int total_score;

	static constexpr size_t MAX_SPECTATORS_TO_DISPLAY = 8;

public:
	ScoreboardLayout(edict_t* player_ent, size_t reserve_size = MAX_CTF_STAT_LENGTH)
		: layout_builder(reserve_size), ent(player_ent), total_score(0) {
	}

	void collectPlayers() {
		for (unsigned int i = 0; i < game.maxclients; i++) {
			const edict_t* const cl_ent = g_edicts + 1 + i;
			if (!cl_ent->inuse)
				continue;

			const gclient_t* const cl = &game.clients[i];

			PlayerScore player = {
				i,
				cl->resp.score,
				std::clamp(cl->ping, 0, 999),
				(cl_ent->deadflag != 0)
			};

			if (cl->resp.ctf_team == CTF_TEAM1) {
				if (!safe_push_back(team_players, player, MAX_SAFE_CONTAINER_SIZE)) {
					gi.Com_Print("WARNING: Too many team players for scoreboard\n");
				} else {
					total_score += player.score;
				}
			}
			else if (cl->resp.ctf_team == CTF_NOTEAM) {
				if (!safe_push_back(spectators, player, MAX_SAFE_CONTAINER_SIZE)) {
					gi.Com_Print("WARNING: Too many spectators for scoreboard\n");
				}
			}
		}
		std::sort(team_players.begin(), team_players.end(), std::greater<>());
	}

	void addHeader() {
    if (g_horde->integer || pvm->integer) {
		// string2 is better than loc_string2 here it seems
        // Element 1: Wave Number (aligned left)
        layout_builder.append(fmt::format(
            "if 0 xv -130 yv -5 string2 \"Wave: {}\" endif \n",
            last_wave_number));

        // Element 2: Stroggs Remaining (aligned further to the right)
        layout_builder.append(fmt::format(
            "if 0 xv -40 yv -5 string2 \"Stroggs: {}\" endif \n",
            GetStroggsNum()));
    }

    // Time limit remains the same
    if (timelimit->value) {
        layout_builder.append(fmt::format(
            "if 0 xv 340 yv -33 time_limit {} endif \n",
            gi.ServerFrame() + ((gtime_t::from_min(timelimit->value) - level.time)).milliseconds() / gi.frame_time_ms));
    }
}

	// THIS IS THE REVISED, SIMPLER FUNCTION
	void addTeamScore() {
		if (!level.intermissiontime) {
			layout_builder.append("if 25 xv -135 yv 3 dogtag endif \n");

			// Get the new, safely-limited active bonuses string
			std::string activeBonuses = GetPlayerActiveBonusesString(const_cast<edict_t*>(ent));
			if (!activeBonuses.empty()) {
                // No more truncation needed here. The string is already safe.
				layout_builder.append(fmt::format(
					"if 0 xv 208 yv 8 string \"{}\" endif \n", activeBonuses));
			}
		}
		else {
			layout_builder.append(fmt::format(
				"if 25 xv -130 yv 3 dogtag endif "
				"if 25 xv 205 yv 8 pic 25 endif "
				"if 0 xv 70 yv -20 num 0 {} endif \n", // Used num instead of 19 for score
				total_score));
		}
	}

void addPlayerList() {
	// Add column headers. The X coordinates here will be the same for the data below.
	int header_y = PLAYER_Y_START - PLAYER_Y_SPACING; // Position headers just above the first player
	layout_builder.append(fmt::format(
		"if 0 xv -130 yv {} string2 \"Name\" xv 70 yv {} string2 \"Score\" xv 120 yv {} string2 \"Ping\" endif \n",
		header_y, header_y, header_y));

	// Loop through players and display their info
	for (size_t i = 0; i < std::min(team_players.size(), MAX_PLAYERS_TO_DISPLAY); ++i) {
		const auto& player = team_players[i];
		edict_t* player_ent = g_edicts + 1 + player.index;
		int y = PLAYER_Y_START + i * PLAYER_Y_SPACING;

		// --- [DEAD] Indicator ---
		// Draw this separately to the left so it doesn't affect name alignment.
		if (player.is_dead) {
			layout_builder.append(fmt::format(
				"if 0 xv -175 yv {} string \"[DEAD]\" endif \n", y));
		}

		// --- Player Data (Manual Placement) ---
		// We now draw each piece of data in its correct column to match the headers.
		const char* player_name = GetPlayerName(player_ent);
		std::string score_str = fmt::format("{}", player.score);
		std::string ping_str = fmt::format("{}", player.ping);

		// This single command places each string at a specific coordinate.
		layout_builder.append(fmt::format(
			// Column 1: Name (starts at x=-90)
			"if 0 xv -130 yv {} string \"{}\" "
			// Column 2: Score (starts at x=70)
			"xv 70 yv {} string \"{}\" "
			// Column 3: Ping (starts at x=120)
			"xv 120 yv {} string \"{}\" endif \n",
			y, player_name,
			y, score_str,
			y, ping_str));
	}
}
void addSpectators() {
    if (layout_builder.size() < MAX_CTF_STAT_LENGTH - LAYOUT_SAFETY_MARGIN && !spectators.empty()) {
        int y = PLAYER_Y_START + (std::min(team_players.size(), MAX_PLAYERS_TO_DISPLAY) + 2) * PLAYER_Y_SPACING;

        // --- OPTIMIZATION ---
        // Switched from the complex 'loc_string2' to the simpler 'string2'.
        // This is more direct and saves a couple of bytes.
        layout_builder.append(fmt::format(
            "if 0 xv -90 yv {} string2 \"Spectators & AFK\" endif \n", y));
        y += PLAYER_Y_SPACING;

        size_t spectators_to_display = std::min(spectators.size(), MAX_SPECTATORS_TO_DISPLAY);
        for (size_t i = 0; i < spectators_to_display; ++i) {
            const auto& spec = spectators[i];
            if (layout_builder.size() >= MAX_CTF_STAT_LENGTH - LAYOUT_SAFETY_MARGIN) {
                break;
            }
            // This 'ctf' command is already highly efficient. No changes needed.
            layout_builder.append(fmt::format(
                "if 0 ctf -90 {} {} {:5} {} \"\" endif \n",
                y, spec.index, spec.score, spec.ping));
            y += PLAYER_Y_SPACING;
        }

        if (spectators.size() > spectators_to_display) {
            // This 'string' command is also efficient. No changes needed.
            layout_builder.append(fmt::format(
                "if 0 xv -90 yv {} string \"... and {} more\" endif \n",
                y, spectators.size() - spectators_to_display));
        }
    }
}

	void addFooter() {
		if (!level.intermissiontime) {
			const char* help_text = (ent->client->resp.ctf_team != CTF_TEAM1)
				? "Use Inventory <KEY> to toggle Horde Menu."
				: "Use Horde Menu on Powerup Wheel or press Inventory <KEY> to toggle Horde Menu.";
			layout_builder.append(fmt::format(
				"if 0 xv 0 yb -55 cstring2 \"{}\" endif \n", help_text));
		}
		else {
			const char* message = brandom()
				? "MAKE THEM PAY !!!"
				: "THEY WILL REGRET THIS !!!";
			layout_builder.append(fmt::format(
				"ifgef {} yb -48 xv 0 loc_cstring2 0 \"{}\" endif \n",
				level.intermission_server_frame + (5_sec).frames(),
				message));
		}
	}

	std::string build() {
		return layout_builder.str();
	}
};

/**
 * @brief Displays the CTF/Horde scoreboard for a player
 * @param ent The player entity to display the scoreboard for
 * @param killer The entity that killed the player (if any)
 */
void HordeScoreboardMessage(edict_t *ent, edict_t *killer)
{
	// Create scoreboard layout generator
	ScoreboardLayout layout(ent);

	// Collect and sort players
	layout.collectPlayers();

	// Build the layout in sections
	layout.addHeader();
	layout.addTeamScore();
	layout.addPlayerList();
	layout.addSpectators();
	layout.addFooter();

	// Get final layout string
	std::string final_layout = layout.build();

	// Ensure we don't exceed layout size limits
	if (final_layout.size() >= MAX_CTF_STAT_LENGTH)
	{
		// Safe resize with exception handling
		try
		{
			final_layout.resize(MAX_CTF_STAT_LENGTH - 1);
		}
		catch (const std::bad_alloc &)
		{
			gi.Com_Print("ERROR: Failed to resize scoreboard layout\n");
			final_layout = "ERROR: Memory allocation failed";
		}
	}

	// Send to client
	gi.WriteByte(svc_layout);
	gi.WriteString(final_layout.c_str());
}

// --- BENEFITS MENU SYSTEM ---

// Open Abilities Menu
void OpenAbilitiesMenu(edict_t *ent)
{
	// Set menu protection
	if (ent && ent->client)
	{
		ent->client->menu_protected = true;
		ent->client->menu_protection_start = level.time;
	}
	CreateAbilitiesMenu(ent);
}

// Abilities Menu Handler
void AbilitiesMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p)
		return;

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	// Handle benefit purchase
	if (strncmp(item->text_arg1, "ability_", 8) == 0)
	{
		const char *benefit_name = item->text_arg1 + 8; // Skip "ability_" prefix

		// Find benefit by name
		for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS; ++i)
		{
			if (g_benefitsData.categories[i] != BenefitCategory::ABILITY)
				continue;

			if (strcmp(g_benefitsData.names[i], benefit_name) == 0)
			{
				BenefitID benefit_id = static_cast<BenefitID>(i);
				int32_t cost = 1; // Default cost

				if (PlayerPurchaseBenefit(ent, benefit_id, cost))
				{
					// Refresh menu to show updated state
					PMenu_Close(ent);
					OpenAbilitiesMenu(ent);
				}
				return;
			}
		}
	}

	// Handle special menu actions
	if (strcmp(item->text_arg1, "back_to_main") == 0)
	{
		PMenu_Close(ent);
		OpenUpgradeMenu(ent);
	}
}

// Create Abilities Menu
pmenuhnd_t *CreateAbilitiesMenu(edict_t *ent)
{
	if (!ent || !ent->client)
		return nullptr;

	static pmenu_t abilities_menu[32];
	memset(abilities_menu, 0, sizeof(abilities_menu));
	int menu_index = 0;

	// Header
	Q_strlcpy(abilities_menu[menu_index].text, "=== ABILITIES SHOP ===", sizeof(abilities_menu[menu_index].text));
	abilities_menu[menu_index].align = PMENU_ALIGN_CENTER;
	abilities_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Points display
	G_FmtTo(abilities_menu[menu_index].text, "Points Available: {}", ent->client->pers.ability_points);
	abilities_menu[menu_index].align = PMENU_ALIGN_CENTER;
	abilities_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Separator
	Q_strlcpy(abilities_menu[menu_index].text, "---", sizeof(abilities_menu[menu_index].text));
	abilities_menu[menu_index].align = PMENU_ALIGN_CENTER;
	abilities_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// List ability benefits (only show available ones)
	bool has_available = false;
	for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS && menu_index < 25; ++i)
	{
		if (g_benefitsData.categories[i] != BenefitCategory::ABILITY)
			continue;

		BenefitID benefit_id = static_cast<BenefitID>(i);
		bool owned = PlayerHasBenefit(ent, benefit_id);

		// Skip if already owned - cleaner menu
		if (owned)
		{
			continue;
		}

		// Check prerequisites
		auto prereq = g_benefitsData.prerequisites[i];
		bool prereq_met = (prereq == BenefitID::NONE) || PlayerHasBenefit(ent, prereq);

		// Don't show if prerequisite not met - cleaner menu
		if (!prereq_met)
		{
			continue;
		}

		bool can_afford = ent->client->pers.ability_points >= 1;

		// Available to purchase
		G_FmtTo(abilities_menu[menu_index].text,
				"{} {} (1 pt)", can_afford ? ">" : " ", g_benefitsData.names[i]);
		abilities_menu[menu_index].align = PMENU_ALIGN_LEFT;
		if (can_afford)
		{
			abilities_menu[menu_index].SelectFunc = AbilitiesMenuHandler;
			snprintf(abilities_menu[menu_index].text_arg1, sizeof(abilities_menu[menu_index].text_arg1),
					 "ability_%s", g_benefitsData.names[i]);
		}
		else
		{
			abilities_menu[menu_index].SelectFunc = nullptr;
		}
		menu_index++;
		has_available = true;
	}

	if (!has_available)
	{
		Q_strlcpy(abilities_menu[menu_index].text, "All abilities purchased!", sizeof(abilities_menu[menu_index].text));
		abilities_menu[menu_index].align = PMENU_ALIGN_CENTER;
		abilities_menu[menu_index].SelectFunc = nullptr;
		menu_index++;
	}

	// Separator before back option
	Q_strlcpy(abilities_menu[menu_index].text, "---", sizeof(abilities_menu[menu_index].text));
	abilities_menu[menu_index].align = PMENU_ALIGN_CENTER;
	abilities_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Back to main menu
	Q_strlcpy(abilities_menu[menu_index].text, "< Back", sizeof(abilities_menu[menu_index].text));
	abilities_menu[menu_index].align = PMENU_ALIGN_LEFT;
	abilities_menu[menu_index].SelectFunc = AbilitiesMenuHandler;
	Q_strlcpy(abilities_menu[menu_index].text_arg1, "back_to_main", sizeof(abilities_menu[menu_index].text_arg1));
	menu_index++;

	return PMenu_Open(ent, abilities_menu, 0, menu_index, nullptr, nullptr);
}

// Open Weapons Menu
void OpenWeaponsMenu(edict_t *ent)
{
	// Set menu protection
	if (ent && ent->client)
	{
		ent->client->menu_protected = true;
		ent->client->menu_protection_start = level.time;
	}
	CreateWeaponsMenu(ent);
}

// Weapons Menu Handler
void WeaponsMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p)
		return;

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	// Handle benefit purchase
	if (strncmp(item->text_arg1, "weapon_", 7) == 0)
	{
		const char *benefit_name = item->text_arg1 + 7; // Skip "weapon_" prefix

		// Find benefit by name
		for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS; ++i)
		{
			if (g_benefitsData.categories[i] != BenefitCategory::WEAPON)
				continue;

			if (strcmp(g_benefitsData.names[i], benefit_name) == 0)
			{
				BenefitID benefit_id = static_cast<BenefitID>(i);

				// Set costs based on benefit type
				int32_t cost = 1; // Default cost
				if (benefit_id == BenefitID::BFG_GRAV_PULL || benefit_id == BenefitID::CLUSTER_PROX)
				{
					cost = 3;
				}

				if (PlayerPurchaseBenefit(ent, benefit_id, cost))
				{
					// Refresh menu to show updated state
					PMenu_Close(ent);
					OpenWeaponsMenu(ent);
				}
				return;
			}
		}
	}

	// Handle special menu actions
	if (strcmp(item->text_arg1, "back_to_main") == 0)
	{
		PMenu_Close(ent);
		OpenUpgradeMenu(ent);
	}
}

// Create Weapons Menu
pmenuhnd_t *CreateWeaponsMenu(edict_t *ent)
{
	if (!ent || !ent->client)
		return nullptr;

	static pmenu_t weapons_menu[32];
	memset(weapons_menu, 0, sizeof(weapons_menu));
	int menu_index = 0;

	// Header
	Q_strlcpy(weapons_menu[menu_index].text, "=== WEAPON UPGRADES ===", sizeof(weapons_menu[menu_index].text));
	weapons_menu[menu_index].align = PMENU_ALIGN_CENTER;
	weapons_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Points display
	G_FmtTo(weapons_menu[menu_index].text, "Points Available: {}", ent->client->pers.weapon_points);
	weapons_menu[menu_index].align = PMENU_ALIGN_CENTER;
	weapons_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Separator
	Q_strlcpy(weapons_menu[menu_index].text, "---", sizeof(weapons_menu[menu_index].text));
	weapons_menu[menu_index].align = PMENU_ALIGN_CENTER;
	weapons_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// List weapon benefits (only show available ones)
	bool has_available = false;
	for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS && menu_index < 25; ++i)
	{
		if (g_benefitsData.categories[i] != BenefitCategory::WEAPON)
			continue;

		BenefitID benefit_id = static_cast<BenefitID>(i);
		bool owned = PlayerHasBenefit(ent, benefit_id);

		// Skip if already owned - cleaner menu
		if (owned)
		{
			continue;
		}

		// Set costs based on benefit type
		int32_t cost = 1; // Default cost
		if (benefit_id == BenefitID::BFG_GRAV_PULL || benefit_id == BenefitID::CLUSTER_PROX)
		{
			cost = 3;
		}

		// Check prerequisites
		auto prereq = g_benefitsData.prerequisites[i];
		bool prereq_met = (prereq == BenefitID::NONE) || PlayerHasBenefit(ent, prereq);

		// Don't show if prerequisite not met - cleaner menu
		if (!prereq_met)
		{
			continue;
		}

		bool can_afford = ent->client->pers.weapon_points >= cost;

		// Available to purchase
		G_FmtTo(weapons_menu[menu_index].text,
				"{} {} ({} pt{})", can_afford ? ">" : " ", g_benefitsData.names[i], cost, cost > 1 ? "s" : "");
		weapons_menu[menu_index].align = PMENU_ALIGN_LEFT;
		if (can_afford)
		{
			weapons_menu[menu_index].SelectFunc = WeaponsMenuHandler;
			snprintf(weapons_menu[menu_index].text_arg1, sizeof(weapons_menu[menu_index].text_arg1),
					 "weapon_%s", g_benefitsData.names[i]);
		}
		else
		{
			weapons_menu[menu_index].SelectFunc = nullptr;
		}
		menu_index++;
		has_available = true;
	}

	if (!has_available)
	{
		Q_strlcpy(weapons_menu[menu_index].text, "All weapons purchased!", sizeof(weapons_menu[menu_index].text));
		weapons_menu[menu_index].align = PMENU_ALIGN_CENTER;
		weapons_menu[menu_index].SelectFunc = nullptr;
		menu_index++;
	}

	// Separator before back option
	Q_strlcpy(weapons_menu[menu_index].text, "---", sizeof(weapons_menu[menu_index].text));
	weapons_menu[menu_index].align = PMENU_ALIGN_CENTER;
	weapons_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Back to main menu
	Q_strlcpy(weapons_menu[menu_index].text, "< Back", sizeof(weapons_menu[menu_index].text));
	weapons_menu[menu_index].align = PMENU_ALIGN_LEFT;
	weapons_menu[menu_index].SelectFunc = WeaponsMenuHandler;
	Q_strlcpy(weapons_menu[menu_index].text_arg1, "back_to_main", sizeof(weapons_menu[menu_index].text_arg1));
	menu_index++;

	return PMenu_Open(ent, weapons_menu, 0, menu_index, nullptr, nullptr);
}

// =================
// Upgrade Menu System
// =================

void OpenUpgradeMenu(edict_t *ent)
{
	// Set menu protection for upgrade menu
	if (ent && ent->client)
	{
		ent->client->menu_protected = true;
		ent->client->menu_protection_start = level.time;
	}
	CreateUpgradeMenu(ent);
}

void UpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p)
		return;

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	// Handle menu navigation
	if (strcmp(item->text_arg1, "abilities_shop") == 0)
	{
		PMenu_Close(ent);
		OpenAbilitiesMenu(ent); // This already sets protection
	}
	else if (strcmp(item->text_arg1, "weapon_upgrades") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponsMenu(ent); // This already sets protection
	}
	else if (strcmp(item->text_arg1, "restore_points") == 0)
	{
		// Preserve admin-given bonus points
		int32_t admin_bonus_ability = ent->client->pers.admin_bonus_ability_points;
		int32_t admin_bonus_weapon = ent->client->pers.admin_bonus_weapon_points;

		PlayerRestoreAllPoints(ent);

		// Re-add admin bonus points after restore
		ent->client->pers.ability_points += admin_bonus_ability;
		ent->client->pers.weapon_points += admin_bonus_weapon;

		if (admin_bonus_ability > 0 || admin_bonus_weapon > 0)
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "Points restored (preserved {} admin ability and {} admin weapon bonus)\n",
							   admin_bonus_ability, admin_bonus_weapon);
		}

		PMenu_Close(ent);
		OpenUpgradeMenu(ent); // This already sets protection
	}
	else if (strcmp(item->text_arg1, "toggle_auto_buy_abilities") == 0)
	{
		bool was_enabled = ent->client->pers.auto_buy_abilities;
		ent->client->pers.auto_buy_abilities = !ent->client->pers.auto_buy_abilities;

		// If disabling auto-buy for the first time, offer refund
		if (was_enabled && !ent->client->pers.auto_buy_abilities &&
			!ent->client->pers.has_manually_disabled_auto_buy)
		{
			PlayerRefundAutoPurchasedBenefits(ent);
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "Auto-buy abilities: {}\n",
							   ent->client->pers.auto_buy_abilities ? "ON" : "OFF");
		}
		PMenu_Close(ent);
		OpenUpgradeMenu(ent); // This already sets protection
	}
	else if (strcmp(item->text_arg1, "toggle_auto_buy_weapons") == 0)
	{
		bool was_enabled = ent->client->pers.auto_buy_weapons;
		ent->client->pers.auto_buy_weapons = !ent->client->pers.auto_buy_weapons;

		// If disabling auto-buy for the first time, offer refund
		if (was_enabled && !ent->client->pers.auto_buy_weapons &&
			!ent->client->pers.has_manually_disabled_auto_buy)
		{
			PlayerRefundAutoPurchasedBenefits(ent);
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "Auto-buy weapons: {}\n",
							   ent->client->pers.auto_buy_weapons ? "ON" : "OFF");
		}
		PMenu_Close(ent);
		OpenUpgradeMenu(ent); // This already sets protection
	}
	else if (strcmp(item->text_arg1, "back_to_main") == 0)
	{
		PMenu_Close(ent);
		OpenHordeMenu(ent); // This already sets protection
	}
}

pmenuhnd_t *CreateUpgradeMenu(edict_t *ent)
{
	if (!ent || !ent->client)
		return nullptr;

	static pmenu_t upgrade_menu[64];
	memset(upgrade_menu, 0, sizeof(upgrade_menu));
	int menu_index = 0;

	// Title
	Q_strlcpy(upgrade_menu[menu_index].text, "=== UPGRADE MENU ===", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Points display
	G_FmtTo(upgrade_menu[menu_index].text, "Ability Points: {}", ent->client->pers.ability_points);
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	G_FmtTo(upgrade_menu[menu_index].text, "Weapon Points: {}", ent->client->pers.weapon_points);
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "---", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Menu options
	Q_strlcpy(upgrade_menu[menu_index].text, "> Abilities Shop", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
	upgrade_menu[menu_index].SelectFunc = UpgradeMenuHandler;
	Q_strlcpy(upgrade_menu[menu_index].text_arg1, "abilities_shop", sizeof(upgrade_menu[menu_index].text_arg1));
	menu_index++;

	Q_strlcpy(upgrade_menu[menu_index].text, "> Weapon Upgrades", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
	upgrade_menu[menu_index].SelectFunc = UpgradeMenuHandler;
	Q_strlcpy(upgrade_menu[menu_index].text_arg1, "weapon_upgrades", sizeof(upgrade_menu[menu_index].text_arg1));
	menu_index++;

	// Always show restore option - helps late-joining players and those who need to reset
	Q_strlcpy(upgrade_menu[menu_index].text, "> Restore All Points", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
	upgrade_menu[menu_index].SelectFunc = UpgradeMenuHandler;
	Q_strlcpy(upgrade_menu[menu_index].text_arg1, "restore_points", sizeof(upgrade_menu[menu_index].text_arg1));
	menu_index++;

	// Auto-buy toggles
	G_FmtTo(upgrade_menu[menu_index].text, "> Auto-buy Abilities: {}", ent->client->pers.auto_buy_abilities ? "ON" : "OFF");
	upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
	upgrade_menu[menu_index].SelectFunc = UpgradeMenuHandler;
	Q_strlcpy(upgrade_menu[menu_index].text_arg1, "toggle_auto_buy_abilities", sizeof(upgrade_menu[menu_index].text_arg1));
	menu_index++;

	G_FmtTo(upgrade_menu[menu_index].text, "> Auto-buy Weapons: {}", ent->client->pers.auto_buy_weapons ? "ON" : "OFF");
	upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
	upgrade_menu[menu_index].SelectFunc = UpgradeMenuHandler;
	Q_strlcpy(upgrade_menu[menu_index].text_arg1, "toggle_auto_buy_weapons", sizeof(upgrade_menu[menu_index].text_arg1));
	menu_index++;

	// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "---", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Back to main menu
	Q_strlcpy(upgrade_menu[menu_index].text, "< Back", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
	upgrade_menu[menu_index].SelectFunc = UpgradeMenuHandler;
	Q_strlcpy(upgrade_menu[menu_index].text_arg1, "back_to_main", sizeof(upgrade_menu[menu_index].text_arg1));
	menu_index++;

	return PMenu_Open(ent, upgrade_menu, 0, menu_index, nullptr, nullptr);
}

// --- END OF FILE horde_menu.cpp ---
