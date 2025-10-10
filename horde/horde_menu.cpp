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
#include "g_pvm_menu.h"    // For PvM stats menu
#include "g_upgrades.h"    // For new skill/upgrade system
#include "g_character.h"   // For Character_Save
#include "menu_helpers.h"  // For menu formatting helpers

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
void OpenWeaponUpgradeMenu(edict_t *ent); // Forward declare Weapon Upgrade menu
void OpenMGUpgradeMenu(edict_t *ent, int cursor_pos = -1);     // Forward declare MG Upgrade submenu
void OpenCGUpgradeMenu(edict_t *ent, int cursor_pos = -1);     // Forward declare CG Upgrade submenu (already has cursor support)
void OpenSGUpgradeMenu(edict_t *ent, int cursor_pos = -1);     // Forward declare SG Upgrade submenu
void OpenSSGUpgradeMenu(edict_t *ent, int cursor_pos = -1);    // Forward declare SSG Upgrade submenu
void OpenGLUpgradeMenu(edict_t *ent, int cursor_pos = -1);     // Forward declare GL Upgrade submenu
void OpenRLUpgradeMenu(edict_t *ent, int cursor_pos = -1);     // Forward declare RL Upgrade submenu
void OpenProxUpgradeMenu(edict_t *ent);   // Forward declare Prox Upgrade submenu
void OpenPlasmabeamUpgradeMenu(edict_t *ent, int cursor_pos = -1); // Forward declare Plasmabeam Upgrade submenu
void OpenPhalanxUpgradeMenu(edict_t *ent, int cursor_pos = -1);    // Forward declare Phalanx Upgrade submenu
void OpenDisruptorUpgradeMenu(edict_t *ent, int cursor_pos = -1);  // Forward declare Disruptor Upgrade submenu
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
constexpr size_t RESPAWN_WEAPON_MENU_SIZE = 18; // Title, blank, current weapon, blank, 10 weapons, blank, Next, Previous, Back;
static pmenu_t respawn_weapon_menu[RESPAWN_WEAPON_MENU_SIZE];
static size_t respawn_weapon_current_page = 0;

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
	{ // Close
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

	// Save to character file
	Character_Save(ent);

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
	bool has_slide = BotHasBenefit(ent, BenefitID::BFG_SLIDE);
	bool has_pull = BotHasBenefit(ent, BenefitID::BFG_GRAV_PULL);

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

	// Save to character file
	Character_Save(ent);

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

	// Handle "< Back" or "Back to Main Menu"
	if (strcmp(selected_text, "< Back") == 0 || strcmp(selected_text, "Back to Main Menu") == 0)
	{
		respawn_weapon_current_page = 0; // Reset to first page
		PMenu_Close(ent);
		OpenHordeMenu(ent);
		return;
	}

	// Handle "Next >"
	if (strcmp(selected_text, "Next >") == 0)
	{
		respawn_weapon_current_page++;
		PMenu_Close(ent);
		OpenRespawnWeaponMenu(ent);
		return;
	}

	// Handle "< Previous"
	if (strcmp(selected_text, "< Previous") == 0)
	{
		if (respawn_weapon_current_page > 0)
		{
			respawn_weapon_current_page--;
		}
		PMenu_Close(ent);
		OpenRespawnWeaponMenu(ent);
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

	// Reopen the menu to show updated selection (stay on same page)
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

	// All weapons across both pages
	static const char* all_weapons[] = {
		// Page 0: Base weapons (12 weapons)
		"Chainfist",
		"Blaster",
		"Shotgun",
		"Super Shotgun",
		"Machinegun",
		"Chaingun",
		"Grenade Launcher",
		"Grenades",
		"Rocket Launcher",
		"HyperBlaster",
		"Railgun",
		"BFG10K",
		// Page 1: Expansion weapons (8 weapons)
		"20mm Cannon",
		"ETF Rifle",
		"Prox Launcher",
		"Plasma Beam",
		"Ionripper",
		"Phalanx",
		"Disruptor",
		"Tesla",
		"Trap"
	};
	constexpr size_t total_weapons = sizeof(all_weapons) / sizeof(all_weapons[0]);
	constexpr size_t weapons_per_page = 12;
	constexpr size_t total_pages = (total_weapons + weapons_per_page - 1) / weapons_per_page;

	// Validate and clamp current page
	if (respawn_weapon_current_page >= total_pages)
	{
		respawn_weapon_current_page = 0;
	}

	// Calculate weapon range for current page
	size_t start_index = respawn_weapon_current_page * weapons_per_page;
	size_t end_index = std::min(start_index + weapons_per_page, total_weapons);

	// Title
	add_entry("*Set Respawn Weapon*", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current respawn weapon
	char current_weapon_display[64];
	const char *current_weapon = Character_GetRespawnWeapon(ent);
	snprintf(current_weapon_display, sizeof(current_weapon_display), "Current: %s", current_weapon);
	add_entry(current_weapon_display, PMENU_ALIGN_LEFT);
	add_entry("", PMENU_ALIGN_CENTER);

	// Weapon options for current page
	for (size_t i = start_index; i < end_index; ++i)
	{
		add_entry(all_weapons[i], PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	}

	// Fill remaining weapon slots if we have fewer than 10 weapons on this page
	for (size_t i = end_index - start_index; i < weapons_per_page; ++i)
	{
		add_entry("", PMENU_ALIGN_CENTER);
	}

	// Navigation section
	add_entry("", PMENU_ALIGN_CENTER);

	// Next button (only if not on last page)
	if (respawn_weapon_current_page < total_pages - 1)
	{
		add_entry("Next >", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	}
	else
	{
		add_entry("", PMENU_ALIGN_CENTER);
	}

	// Previous button (only if not on first page)
	if (respawn_weapon_current_page > 0)
	{
		add_entry("< Previous", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
	}
	else
	{
		add_entry("", PMENU_ALIGN_CENTER);
	}

	// Back to main menu (always show, with "< Back" label)
	add_entry("< Back", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);

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
	bool has_bfg_upgrades = BotHasBenefit(ent, BenefitID::BFG_SLIDE) || BotHasBenefit(ent, BenefitID::BFG_GRAV_PULL);
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
	add_entry("Add 5 Ability Points (Bots)", PMENU_ALIGN_LEFT, AdminMenuHandler);
	add_entry("Add 5 Weapon Points (Bots)", PMENU_ALIGN_LEFT, AdminMenuHandler);
	add_entry("Add 10 Points (Bots)", PMENU_ALIGN_LEFT, AdminMenuHandler);
	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("Give All Weapons (All)", PMENU_ALIGN_LEFT, AdminMenuHandler);
	// God mode commented out - can break game balance
	// add_entry("Give God Mode (All)", PMENU_ALIGN_LEFT, AdminMenuHandler);
	add_entry("Heal All Players (All)", PMENU_ALIGN_LEFT, AdminMenuHandler);
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
			if (player->client && (player->client->resp.ctf_team == CTF_TEAM1 || player->svflags & SVF_BOT ||
								   G_IsCooperative() || coop->integer || !deathmatch->integer))
			{
				player->client->pers.ability_points += 5;
				player->client->pers.admin_bonus_ability_points += 5; // Track admin-given points
				gi.LocClient_Print(player, PRINT_HIGH, "{} granted you 5 ability points!\n", adminName);
			}
		}
		gi.LocClient_Print(ent, PRINT_HIGH, "Gave 5 bonus ability points to all bots.\n");
	}
	else if (strcmp(text, "Add 5 Weapon Points (All)") == 0)
	{
		const char *adminName = GetPlayerName(ent);
		for (auto player : active_players())
		{
			if (player->client && (player->client->resp.ctf_team == CTF_TEAM1 ||  player->svflags & SVF_BOT ||
								   G_IsCooperative() || coop->integer || !deathmatch->integer))
			{
				player->client->pers.weapon_points += 5;
				player->client->pers.admin_bonus_weapon_points += 5; // Track admin-given points
				gi.LocClient_Print(player, PRINT_HIGH, "{} granted you 5 weapon points!\n", adminName);
			}
		}
		gi.LocClient_Print(ent, PRINT_HIGH, "Gave 5 bonus weapon points to all bots.\n");
	}
	else if (strcmp(text, "Add 10 Points (All)") == 0)
	{
		const char *adminName = GetPlayerName(ent);
		for (auto player : active_players())
		{
			if (player->client && (player->client->resp.ctf_team == CTF_TEAM1 ||  player->svflags & SVF_BOT ||
								   G_IsCooperative() || coop->integer || !deathmatch->integer))
			{
				player->client->pers.ability_points += 10;
				player->client->pers.weapon_points += 10;
				player->client->pers.admin_bonus_ability_points += 10; // Track admin-given points
				player->client->pers.admin_bonus_weapon_points += 10;  // Track admin-given points
				gi.LocClient_Print(player, PRINT_HIGH, "{} granted you 10 ability and weapon points!\n", adminName);
			}
		}
		gi.LocClient_Print(ent, PRINT_HIGH, "Gave 10 bonus points of each type to all bots.\n");
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
		respawn_weapon_current_page = 0; // Reset to first page when opening from main menu
		OpenRespawnWeaponMenu(ent);
		shouldCloseMenu = false;
	}
	// Character Info
	else if (strstr(selected_text, "Character Info"))
	{
		PvM_OpenStatsMenu(ent);
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
		// Add Character Info menu
		add_entry("Character Info", PMENU_ALIGN_LEFT, HordeMenuHandler);
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

// Ability Detail Menu Handler
void AbilityDetailMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p)
		return;

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	// Handle upgrade purchase
	if (strncmp(item->text_arg1, "upgrade_", 8) == 0)
	{
		const char *upgrade_id = item->text_arg1 + 8; // Skip "upgrade_" prefix

		if (UpgradeSkill(ent, upgrade_id))
		{
			Character_Save(ent);  // Save the upgrade to disk
			gi.LocClient_Print(ent, PRINT_HIGH, "Upgraded {}!\n",
				FindUpgradeByID(upgrade_id)->name);

			// Refresh detail menu to show updated state
			PMenu_Close(ent);

			// Re-open detail menu with same ability
			if (ent && ent->client)
			{
				ent->client->menu_protected = true;
				ent->client->menu_protection_start = level.time;
			}

			// Create detail menu for the same ability
			static pmenu_t detail_menu[32];
			memset(detail_menu, 0, sizeof(detail_menu));
			int menu_index = 0;

			const UpgradeDefinition* def = FindUpgradeByID(upgrade_id);
			if (!def) return;

			int8_t current_level = GetSkillLevel(ent, upgrade_id);
			bool can_upgrade = CanUpgrade(ent, upgrade_id);

			// Title
			G_FmtTo(detail_menu[menu_index].text, "=== {} ===", def->name);
			detail_menu[menu_index].align = PMENU_ALIGN_CENTER;
			detail_menu[menu_index].SelectFunc = nullptr;
			menu_index++;

			// Level info
			G_FmtTo(detail_menu[menu_index].text, "Level: {}/{}", current_level, def->max_level);
			detail_menu[menu_index].align = PMENU_ALIGN_CENTER;
			detail_menu[menu_index].SelectFunc = nullptr;
			menu_index++;

			// Cost
			G_FmtTo(detail_menu[menu_index].text, "Cost: {} pts", def->cost_per_level);
			detail_menu[menu_index].align = PMENU_ALIGN_CENTER;
			detail_menu[menu_index].SelectFunc = nullptr;
			menu_index++;

			// Separator
			Q_strlcpy(detail_menu[menu_index].text, "---", sizeof(detail_menu[menu_index].text));
			detail_menu[menu_index].align = PMENU_ALIGN_CENTER;
			detail_menu[menu_index].SelectFunc = nullptr;
			menu_index++;

			// Description (split by \n)
			char desc_copy[512];
			Q_strlcpy(desc_copy, def->description, sizeof(desc_copy));
			char *line = strtok(desc_copy, "\n");
			while (line && menu_index < 28)
			{
				Q_strlcpy(detail_menu[menu_index].text, line, sizeof(detail_menu[menu_index].text));
				detail_menu[menu_index].align = PMENU_ALIGN_LEFT;
				detail_menu[menu_index].SelectFunc = nullptr;
				menu_index++;
				line = strtok(nullptr, "\n");
			}

			// Separator
			Q_strlcpy(detail_menu[menu_index].text, "---", sizeof(detail_menu[menu_index].text));
			detail_menu[menu_index].align = PMENU_ALIGN_CENTER;
			detail_menu[menu_index].SelectFunc = nullptr;
			menu_index++;

			// Upgrade button
			if (can_upgrade)
			{
				Q_strlcpy(detail_menu[menu_index].text, "> Upgrade", sizeof(detail_menu[menu_index].text));
				detail_menu[menu_index].align = PMENU_ALIGN_LEFT;
				detail_menu[menu_index].SelectFunc = AbilityDetailMenuHandler;
				snprintf(detail_menu[menu_index].text_arg1, sizeof(detail_menu[menu_index].text_arg1), "upgrade_%s", upgrade_id);
			}
			else
			{
				if (current_level >= def->max_level)
					Q_strlcpy(detail_menu[menu_index].text, "  [MAX LEVEL]", sizeof(detail_menu[menu_index].text));
				else
					Q_strlcpy(detail_menu[menu_index].text, "  [INSUFFICIENT POINTS]", sizeof(detail_menu[menu_index].text));
				detail_menu[menu_index].align = PMENU_ALIGN_LEFT;
				detail_menu[menu_index].SelectFunc = nullptr;
			}
			menu_index++;

			// Back button
			Q_strlcpy(detail_menu[menu_index].text, "< Back", sizeof(detail_menu[menu_index].text));
			detail_menu[menu_index].align = PMENU_ALIGN_LEFT;
			detail_menu[menu_index].SelectFunc = AbilityDetailMenuHandler;
			Q_strlcpy(detail_menu[menu_index].text_arg1, "back_to_abilities", sizeof(detail_menu[menu_index].text_arg1));
			menu_index++;

			PMenu_Open(ent, detail_menu, 0, menu_index, nullptr, nullptr);
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "Cannot upgrade this ability\n");
		}
	}
	else if (strcmp(item->text_arg1, "back_to_abilities") == 0)
	{
		PMenu_Close(ent);
		OpenAbilitiesMenu(ent);
	}
}

// Create Ability Detail Menu
pmenuhnd_t *CreateAbilityDetailMenu(edict_t *ent, const char* upgrade_id)
{
	if (!ent || !ent->client || !upgrade_id)
		return nullptr;

	const UpgradeDefinition* def = FindUpgradeByID(upgrade_id);
	if (!def)
		return nullptr;

	static pmenu_t detail_menu[32];
	memset(detail_menu, 0, sizeof(detail_menu));
	int menu_index = 0;

	int8_t current_level = GetSkillLevel(ent, upgrade_id);
	bool can_upgrade = CanUpgrade(ent, upgrade_id);

	// Title
	G_FmtTo(detail_menu[menu_index].text, "=== {} ===", def->name);
	detail_menu[menu_index].align = PMENU_ALIGN_CENTER;
	detail_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Level info
	G_FmtTo(detail_menu[menu_index].text, "Level: {}/{}", current_level, def->max_level);
	detail_menu[menu_index].align = PMENU_ALIGN_CENTER;
	detail_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Cost
	G_FmtTo(detail_menu[menu_index].text, "Cost: {} pts", def->cost_per_level);
	detail_menu[menu_index].align = PMENU_ALIGN_CENTER;
	detail_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Separator
	Q_strlcpy(detail_menu[menu_index].text, "---", sizeof(detail_menu[menu_index].text));
	detail_menu[menu_index].align = PMENU_ALIGN_CENTER;
	detail_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Description (split by \n)
	char desc_copy[512];
	Q_strlcpy(desc_copy, def->description, sizeof(desc_copy));
	char *line = strtok(desc_copy, "\n");
	while (line && menu_index < 28)
	{
		Q_strlcpy(detail_menu[menu_index].text, line, sizeof(detail_menu[menu_index].text));
		detail_menu[menu_index].align = PMENU_ALIGN_LEFT;
		detail_menu[menu_index].SelectFunc = nullptr;
		menu_index++;
		line = strtok(nullptr, "\n");
	}

	// Separator
	Q_strlcpy(detail_menu[menu_index].text, "---", sizeof(detail_menu[menu_index].text));
	detail_menu[menu_index].align = PMENU_ALIGN_CENTER;
	detail_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Upgrade button
	if (can_upgrade)
	{
		Q_strlcpy(detail_menu[menu_index].text, "> Upgrade", sizeof(detail_menu[menu_index].text));
		detail_menu[menu_index].align = PMENU_ALIGN_LEFT;
		detail_menu[menu_index].SelectFunc = AbilityDetailMenuHandler;
		snprintf(detail_menu[menu_index].text_arg1, sizeof(detail_menu[menu_index].text_arg1), "upgrade_%s", upgrade_id);
	}
	else
	{
		if (current_level >= def->max_level)
			Q_strlcpy(detail_menu[menu_index].text, "  [MAX LEVEL]", sizeof(detail_menu[menu_index].text));
		else
			Q_strlcpy(detail_menu[menu_index].text, "  [INSUFFICIENT POINTS]", sizeof(detail_menu[menu_index].text));
		detail_menu[menu_index].align = PMENU_ALIGN_LEFT;
		detail_menu[menu_index].SelectFunc = nullptr;
	}
	menu_index++;

	// Back button
	Q_strlcpy(detail_menu[menu_index].text, "< Back", sizeof(detail_menu[menu_index].text));
	detail_menu[menu_index].align = PMENU_ALIGN_LEFT;
	detail_menu[menu_index].SelectFunc = AbilityDetailMenuHandler;
	Q_strlcpy(detail_menu[menu_index].text_arg1, "back_to_abilities", sizeof(detail_menu[menu_index].text_arg1));
	menu_index++;

	return PMenu_Open(ent, detail_menu, 0, menu_index, nullptr, nullptr);
}

// Abilities Menu Handler
void AbilitiesMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p)
		return;

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	// Handle back navigation
	if (strcmp(item->text_arg1, "back_to_upgrade") == 0)
	{
		PMenu_Close(ent);
		OpenUpgradeMenu(ent);
		return;
	}

	// Handle reset skills
	if (strcmp(item->text_arg1, "reset_skills") == 0)
	{
		ResetAllSkills(ent);
		Character_Save(ent);  // Save the reset to disk
		PMenu_Close(ent);
		OpenAbilitiesMenu(ent);
		return;
	}

	// Open detail menu for selected ability (upgrade_id is directly in text_arg1)
	const char *upgrade_id = item->text_arg1;
	if (upgrade_id && upgrade_id[0] != '\0')
	{
		PMenu_Close(ent);

		// Set menu protection
		if (ent && ent->client)
		{
			ent->client->menu_protected = true;
			ent->client->menu_protection_start = level.time;
		}

		CreateAbilityDetailMenu(ent, upgrade_id);
	}
}

// Create Abilities Menu - New skill-based system
pmenuhnd_t *CreateAbilitiesMenu(edict_t *ent)
{
	if (!ent || !ent->client)
		return nullptr;

	static pmenu_t abilities_menu[32];
	memset(abilities_menu, 0, sizeof(abilities_menu));
	int menu_index = 0;

	// Header
	Q_strlcpy(abilities_menu[menu_index].text, "Upgrade Ability Menu", sizeof(abilities_menu[menu_index].text));
	abilities_menu[menu_index].align = PMENU_ALIGN_CENTER;
	abilities_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Separator
	Q_strlcpy(abilities_menu[menu_index].text, "---", sizeof(abilities_menu[menu_index].text));
	abilities_menu[menu_index].align = PMENU_ALIGN_CENTER;
	abilities_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// List upgradeable abilities
	const UpgradeDefinition* defs = GetUpgradeDefinitions();
	size_t def_count = GetUpgradeDefinitionCount();

	bool has_abilities = false;
	int item_number = 1;  // Start numbering from 1
	for (size_t i = 0; i < def_count && menu_index < 25; ++i)
	{
		if (defs[i].category != UpgradeCategory::ABILITY)
			continue;

		int8_t current_level = GetSkillLevel(ent, defs[i].id);
		int8_t max_level = defs[i].max_level;

		// Show ability with current level
		if (max_level == 1)
		{
			// Boolean ability (owned or not)
			if (current_level > 0)
			{
				MenuFormatItemWithOwned(abilities_menu[menu_index].text,
				                        sizeof(abilities_menu[menu_index].text),
				                        item_number, defs[i].name);
				abilities_menu[menu_index].SelectFunc = AbilitiesMenuHandler; // Allow viewing details
			}
			else
			{
				MenuFormatItemWithCost(abilities_menu[menu_index].text,
				                       sizeof(abilities_menu[menu_index].text),
				                       item_number, defs[i].name, defs[i].cost_per_level);
				abilities_menu[menu_index].SelectFunc = AbilitiesMenuHandler; // Allow viewing details
			}
		}
		else
		{
			// Multi-level ability - use progress indicator [X/Y]
			MenuFormatItemWithProgress(abilities_menu[menu_index].text,
			                           sizeof(abilities_menu[menu_index].text),
			                           item_number, defs[i].name, current_level, max_level);
			abilities_menu[menu_index].SelectFunc = AbilitiesMenuHandler; // Always allow selection to view details
		}

		Q_strlcpy(abilities_menu[menu_index].text_arg1, defs[i].id, sizeof(abilities_menu[menu_index].text_arg1));
		abilities_menu[menu_index].align = PMENU_ALIGN_RIGHT;
		menu_index++;
		item_number++;
		has_abilities = true;
	}

	if (!has_abilities)
	{
		Q_strlcpy(abilities_menu[menu_index].text, "No abilities available", sizeof(abilities_menu[menu_index].text));
		abilities_menu[menu_index].align = PMENU_ALIGN_CENTER;
		abilities_menu[menu_index].SelectFunc = nullptr;
		menu_index++;
	}

	// Separator before back option
	Q_strlcpy(abilities_menu[menu_index].text, "---", sizeof(abilities_menu[menu_index].text));
	abilities_menu[menu_index].align = PMENU_ALIGN_CENTER;
	abilities_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

		// Points display
	G_FmtTo(abilities_menu[menu_index].text, "You have: {} points to upgrade", ent->client->pers.skill_points);
	abilities_menu[menu_index].align = PMENU_ALIGN_CENTER;
	abilities_menu[menu_index].SelectFunc = nullptr;
	menu_index++;
	
	// Reset all skills option
	Q_strlcpy(abilities_menu[menu_index].text, "Reset All Skills (Free)", sizeof(abilities_menu[menu_index].text));
	abilities_menu[menu_index].align = PMENU_ALIGN_LEFT;
	abilities_menu[menu_index].SelectFunc = AbilitiesMenuHandler;
	Q_strlcpy(abilities_menu[menu_index].text_arg1, "reset_skills", sizeof(abilities_menu[menu_index].text_arg1));
	menu_index++;

	// Back to upgrade menu
	Q_strlcpy(abilities_menu[menu_index].text, "< Back", sizeof(abilities_menu[menu_index].text));
	abilities_menu[menu_index].align = PMENU_ALIGN_LEFT;
	abilities_menu[menu_index].SelectFunc = AbilitiesMenuHandler;
	Q_strlcpy(abilities_menu[menu_index].text_arg1, "back_to_upgrade", sizeof(abilities_menu[menu_index].text_arg1));
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
		for (size_t i = 0; i < BotsBonusesSoA::NUM_BOTSBONUS; ++i)
		{
			if (g_BotsBonuses.categories[i] != BenefitCategory::WEAPON)
				continue;

			if (strcmp(g_BotsBonuses.names[i], benefit_name) == 0)
			{
				BenefitID benefit_id = static_cast<BenefitID>(i);

				// Set costs based on benefit type
				int32_t cost = 1; // Default cost
				if (benefit_id == BenefitID::BFG_GRAV_PULL || benefit_id == BenefitID::CLUSTER_PROX)
				{
					cost = 3;
				}

				if (BotPurchaseBenefit(ent, benefit_id, cost))
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
	for (size_t i = 0; i < BotsBonusesSoA::NUM_BOTSBONUS && menu_index < 25; ++i)
	{
		if (g_BotsBonuses.categories[i] != BenefitCategory::WEAPON)
			continue;

		BenefitID benefit_id = static_cast<BenefitID>(i);
		bool owned = BotHasBenefit(ent, benefit_id);

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
		auto prereq = g_BotsBonuses.prerequisites[i];
		bool prereq_met = (prereq == BenefitID::NONE) || BotHasBenefit(ent, prereq);

		// Don't show if prerequisite not met - cleaner menu
		if (!prereq_met)
		{
			continue;
		}

		bool can_afford = ent->client->pers.weapon_points >= cost;

		// Available to purchase
		MenuFormatItemWithCost(weapons_menu[menu_index].text,
		                       sizeof(weapons_menu[menu_index].text),
		                       can_afford, g_BotsBonuses.names[i], cost);
		weapons_menu[menu_index].align = PMENU_ALIGN_LEFT;
		if (can_afford)
		{
			weapons_menu[menu_index].SelectFunc = WeaponsMenuHandler;
			snprintf(weapons_menu[menu_index].text_arg1, sizeof(weapons_menu[menu_index].text_arg1),
					 "weapon_%s", g_BotsBonuses.names[i]);
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
	else if (strcmp(item->text_arg1, "weapons_shop") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
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
	Q_strlcpy(upgrade_menu[menu_index].text, "=== UPGRADES ===", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Points display
	G_FmtTo(upgrade_menu[menu_index].text, "Skill Points: {}", ent->client->pers.skill_points);
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "---", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Menu options
	Q_strlcpy(upgrade_menu[menu_index].text, "> Abilities", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
	upgrade_menu[menu_index].SelectFunc = UpgradeMenuHandler;
	Q_strlcpy(upgrade_menu[menu_index].text_arg1, "abilities_shop", sizeof(upgrade_menu[menu_index].text_arg1));
	menu_index++;

	// Weapon upgrades
	Q_strlcpy(upgrade_menu[menu_index].text, "> Weapons", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
	upgrade_menu[menu_index].SelectFunc = UpgradeMenuHandler;
	Q_strlcpy(upgrade_menu[menu_index].text_arg1, "weapons_shop", sizeof(upgrade_menu[menu_index].text_arg1));
	menu_index++;

	// Placeholder for future talents
	Q_strlcpy(upgrade_menu[menu_index].text, "  Talents (Coming Soon)", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
	upgrade_menu[menu_index].SelectFunc = nullptr;
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

/////////////////////////////////////////////
// WEAPON UPGRADE MENU
/////////////////////////////////////////////

static pmenu_t weapon_upgrade_menu[32];
static size_t weapon_upgrade_current_page = 0;

void WeaponUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenWeaponUpgradeMenu(edict_t *ent)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(weapon_upgrade_menu, 0, sizeof(weapon_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(weapon_upgrade_menu[count].text, text, sizeof(weapon_upgrade_menu[count].text));
			weapon_upgrade_menu[count].align = align;
			weapon_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(weapon_upgrade_menu[count].text_arg1, arg, sizeof(weapon_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// All weapons (name, handler_arg)
	struct WeaponEntry {
		const char* name;
		const char* arg;
	};

	static const WeaponEntry all_weapons[] = {
		// Page 0: Base weapons (12 weapons)
		{"> Chainfist", "chainfist"},
		{"> Blaster", "blaster"},
		{"> Shotgun", "shotgun"},
		{"> Super Shotgun", "supershotgun"},
		{"> Machinegun", "machinegun"},
		{"> Chaingun", "chaingun"},
		{"> Grenade Launcher", "grenade_launcher"},
		{"> Hand Grenades", "hand_grenades"},
		{"> Rocket Launcher", "rocket_launcher"},
		{"> Hyperblaster", "hyperblaster"},
		{"> Railgun", "railgun"},
		{"> BFG10K", "bfg10k"},
		// Page 1: Expansion weapons (8 weapons)
		{"> 20mm Cannon", "20mm_cannon"},
		{"> ETF Rifle", "etf_rifle"},
		{"> Prox Launcher", "prox_launcher"},
		{"> Plasma Beam", "plasmabeam"},
		{"> Ion Ripper", "ion_ripper"},
		{"> Phalanx", "phalanx"},
		{"> Disruptor", "disruptor"},
		{"> Tesla", "tesla"},
		{"> Trap", "trap"}
	};
	constexpr size_t total_weapons = sizeof(all_weapons) / sizeof(all_weapons[0]);
	constexpr size_t weapons_per_page = 12;
	constexpr size_t total_pages = (total_weapons + weapons_per_page - 1) / weapons_per_page;

	// Ensure current page is valid
	if (weapon_upgrade_current_page >= total_pages)
		weapon_upgrade_current_page = 0;

	// Title with page indicator
	char title[64];
	snprintf(title, sizeof(title), "=== WEAPON UPGRADES (Page %zu/%zu) ===", weapon_upgrade_current_page + 1, total_pages);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Add weapons for current page
	size_t start_index = weapon_upgrade_current_page * weapons_per_page;
	size_t end_index = std::min(start_index + weapons_per_page, total_weapons);

	for (size_t i = start_index; i < end_index; i++)
	{
		add_entry(all_weapons[i].name, PMENU_ALIGN_LEFT, WeaponUpgradeMenuHandler, all_weapons[i].arg);
	}

	// Navigation and exit
	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);

	if (weapon_upgrade_current_page < total_pages - 1)
	{
		add_entry("Next >", PMENU_ALIGN_LEFT, WeaponUpgradeMenuHandler, "next_page");
	}

	if (weapon_upgrade_current_page > 0)
	{
		add_entry("< Previous", PMENU_ALIGN_LEFT, WeaponUpgradeMenuHandler, "prev_page");
	}

	add_entry("Reset All Weapon Upgrades (Free)", PMENU_ALIGN_LEFT, WeaponUpgradeMenuHandler, "reset_weapons");
	add_entry("< Back to Upgrades", PMENU_ALIGN_LEFT, WeaponUpgradeMenuHandler, "back_to_upgrades");

	PMenu_Open(ent, weapon_upgrade_menu, -1, count, nullptr, nullptr);
}

// Forward declarations for new weapon menus
void OpenChainfistUpgradeMenu(edict_t *ent, int cursor_pos = -1);
void OpenBlasterUpgradeMenu(edict_t *ent, int cursor_pos = -1);
void OpenHyperblasterUpgradeMenu(edict_t *ent, int cursor_pos = -1);
void OpenHGUpgradeMenu(edict_t *ent, int cursor_pos = -1);
void OpenETFUpgradeMenu(edict_t *ent, int cursor_pos = -1);
void OpenIonRipperUpgradeMenu(edict_t *ent, int cursor_pos = -1);
void OpenRailgunUpgradeMenu(edict_t *ent, int cursor_pos = -1);
void OpenBFGUpgradeMenu(edict_t *ent, int cursor_pos = -1);
void Open20mmCannonUpgradeMenu(edict_t *ent, int cursor_pos = -1);
void OpenTeslaUpgradeMenu(edict_t *ent, int cursor_pos = -1);
void OpenTrapUpgradeMenu(edict_t *ent, int cursor_pos = -1);

void WeaponUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	// Handle page navigation
	if (strcmp(arg, "next_page") == 0)
	{
		weapon_upgrade_current_page++;
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
		return;
	}
	else if (strcmp(arg, "prev_page") == 0)
	{
		if (weapon_upgrade_current_page > 0)
			weapon_upgrade_current_page--;
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
		return;
	}

	// Handle weapon selection
	if (strcmp(arg, "chainfist") == 0)
	{
		PMenu_Close(ent);
		OpenChainfistUpgradeMenu(ent);
	}
	else if (strcmp(arg, "blaster") == 0)
	{
		PMenu_Close(ent);
		OpenBlasterUpgradeMenu(ent);
	}
	else if (strcmp(arg, "hyperblaster") == 0)
	{
		PMenu_Close(ent);
		OpenHyperblasterUpgradeMenu(ent);
	}
	else if (strcmp(arg, "shotgun") == 0)
	{
		PMenu_Close(ent);
		OpenSGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "supershotgun") == 0)
	{
		PMenu_Close(ent);
		OpenSSGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "machinegun") == 0)
	{
		PMenu_Close(ent);
		OpenMGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "chaingun") == 0)
	{
		PMenu_Close(ent);
		OpenCGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "rocket_launcher") == 0)
	{
		PMenu_Close(ent);
		OpenRLUpgradeMenu(ent);
	}
	else if (strcmp(arg, "grenade_launcher") == 0)
	{
		PMenu_Close(ent);
		OpenGLUpgradeMenu(ent);
	}
	else if (strcmp(arg, "hand_grenades") == 0)
	{
		PMenu_Close(ent);
		OpenHGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "prox_launcher") == 0)
	{
		PMenu_Close(ent);
		OpenProxUpgradeMenu(ent);
	}
	else if (strcmp(arg, "etf_rifle") == 0)
	{
		PMenu_Close(ent);
		OpenETFUpgradeMenu(ent);
	}
	else if (strcmp(arg, "ion_ripper") == 0)
	{
		PMenu_Close(ent);
		OpenIonRipperUpgradeMenu(ent);
	}
	else if (strcmp(arg, "plasmabeam") == 0)
	{
		PMenu_Close(ent);
		OpenPlasmabeamUpgradeMenu(ent);
	}
	else if (strcmp(arg, "railgun") == 0)
	{
		PMenu_Close(ent);
		OpenRailgunUpgradeMenu(ent);
	}
	else if (strcmp(arg, "bfg10k") == 0)
	{
		PMenu_Close(ent);
		OpenBFGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "20mm_cannon") == 0)
	{
		PMenu_Close(ent);
		Open20mmCannonUpgradeMenu(ent);
	}
	else if (strcmp(arg, "phalanx") == 0)
	{
		PMenu_Close(ent);
		OpenPhalanxUpgradeMenu(ent);
	}
	else if (strcmp(arg, "disruptor") == 0)
	{
		PMenu_Close(ent);
		OpenDisruptorUpgradeMenu(ent);
	}
	else if (strcmp(arg, "tesla") == 0)
	{
		PMenu_Close(ent);
		OpenTeslaUpgradeMenu(ent);
	}
	else if (strcmp(arg, "trap") == 0)
	{
		PMenu_Close(ent);
		OpenTrapUpgradeMenu(ent);
	}
	else if (strcmp(arg, "reset_weapons") == 0)
	{
		// Reset all weapon skill upgrades and refund points
		auto& skills = ent->client->pers.skills;
		int32_t refunded_points = 0;

		// Count and reset all weapon skills
		refunded_points += skills.mg_damage + skills.mg_pierce + skills.mg_tracers;
		skills.mg_damage = skills.mg_pierce = skills.mg_tracers = 0;
		skills.mg_spread = skills.mg_silent = false;

		refunded_points += skills.cg_damage + skills.cg_spin + skills.cg_tracers;
		skills.cg_damage = skills.cg_spin = skills.cg_tracers = 0;
		skills.cg_spread = skills.cg_silent = false;

		refunded_points += skills.sg_damage + skills.sg_strike + skills.sg_pellets;
		skills.sg_damage = skills.sg_strike = skills.sg_pellets = 0;
		skills.sg_spread = skills.sg_silent = skills.sg_energized = false;

		refunded_points += skills.ssg_damage + skills.ssg_strike + skills.ssg_pellets;
		skills.ssg_damage = skills.ssg_strike = skills.ssg_pellets = 0;
		skills.ssg_spread = skills.ssg_silent = skills.ssg_energized = false;

		refunded_points += skills.rl_damage + skills.rl_radius + skills.rl_speed;
		skills.rl_damage = skills.rl_radius = skills.rl_speed = 0;
		skills.rl_trails = skills.rl_silent = false;

		refunded_points += skills.gl_damage + skills.gl_range + skills.gl_radius;
		skills.gl_damage = skills.gl_range = skills.gl_radius = 0;
		skills.gl_trails = skills.gl_silent = skills.gl_bouncy = false;

		refunded_points += skills.hg_damage + skills.hg_range + skills.hg_radius_damage;
		skills.hg_damage = skills.hg_range = skills.hg_radius_damage = 0;

		refunded_points += skills.hb_damage + skills.hb_speed;
		skills.hb_damage = skills.hb_speed = 0;
		skills.hb_trails = skills.hb_silent = false;

		refunded_points += skills.rg_damage + skills.rg_burn + skills.rg_pierce;
		skills.rg_damage = skills.rg_burn = skills.rg_pierce = 0;
		skills.rg_trails = skills.rg_silent = false;

		refunded_points += skills.bfg_damage + skills.bfg_speed + skills.bfg_duration;
		skills.bfg_damage = skills.bfg_speed = skills.bfg_duration = 0;
		skills.bfg_silent = false;

		refunded_points += skills.cf_damage + skills.cf_range;
		skills.cf_damage = skills.cf_range = 0;
		skills.cf_silent = false;

		refunded_points += skills.etf_damage + skills.etf_speed + skills.etf_kick;
		skills.etf_damage = skills.etf_speed = skills.etf_kick = 0;
		skills.etf_silent = false;

		refunded_points += skills.ir_damage + skills.ir_speed;
		skills.ir_damage = skills.ir_speed = 0;
		skills.ir_silent = false;

		refunded_points += skills.pb_damage + skills.pb_burn + skills.pb_pierce;
		skills.pb_damage = skills.pb_burn = skills.pb_pierce = 0;
		skills.pb_silent = false;

		refunded_points += skills.phalanx_damage + skills.phalanx_radius + skills.phalanx_speed;
		skills.phalanx_damage = skills.phalanx_radius = skills.phalanx_speed = 0;
		skills.phalanx_silent = false;

		refunded_points += skills.disruptor_damage + skills.disruptor_speed + skills.disruptor_duration;
		skills.disruptor_damage = skills.disruptor_speed = skills.disruptor_duration = 0;
		skills.disruptor_silent = false;

		refunded_points += skills.tesla_damage + skills.tesla_range + skills.tesla_radius;
		skills.tesla_damage = skills.tesla_range = skills.tesla_radius = 0;
		skills.tesla_chain = false;

		refunded_points += skills.trap_damage + skills.trap_range + skills.trap_radius;
		skills.trap_damage = skills.trap_range = skills.trap_radius = 0;

		refunded_points += skills.pl_damage + skills.pl_range + skills.pl_radius;
		skills.pl_damage = skills.pl_range = skills.pl_radius = 0;
		skills.pl_trails = skills.pl_silent = skills.pl_improved_traps = false;

		// Refund the points
		ent->client->pers.weapon_points += refunded_points;

		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "All weapon upgrades reset! {} weapon points refunded.\n", refunded_points);

		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
	else if (strcmp(arg, "back_to_upgrades") == 0)
	{
		weapon_upgrade_current_page = 0; // Reset to first page
		PMenu_Close(ent);
		OpenUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// ROCKET LAUNCHER UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t rl_upgrade_menu[32];

void RLUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenRLUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(rl_upgrade_menu, 0, sizeof(rl_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(rl_upgrade_menu[count].text, text, sizeof(rl_upgrade_menu[count].text));
			rl_upgrade_menu[count].align = align;
			rl_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(rl_upgrade_menu[count].text_arg1, arg, sizeof(rl_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.rl_damage +
	                       ent->client->pers.skills.rl_speed +
	                       ent->client->pers.skills.rl_radius;
	int max_upgrades = 30;
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== ROCKET LAUNCHER (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.rl_damage);
	add_entry(status, PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "rl_damage");

	snprintf(status, sizeof(status), "Speed %d [10]", ent->client->pers.skills.rl_speed);
	add_entry(status, PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "rl_speed");

	snprintf(status, sizeof(status), "Radius %d [10]", ent->client->pers.skills.rl_radius);
	add_entry(status, PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "rl_radius");

	const char *trails_status = ent->client->pers.skills.rl_trails ? "ON" : "OFF";
	snprintf(status, sizeof(status), "No Trails: %s", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "rl_trails");

	const char *silent_status = ent->client->pers.skills.rl_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "rl_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, rl_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void RLUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "rl_damage") == 0)
	{
		if (ent->client->pers.skills.rl_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.rl_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Rocket Launcher Damage increased to level {}!\n", ent->client->pers.skills.rl_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Rocket Launcher Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenRLUpgradeMenu(ent);
	}
	else if (strcmp(arg, "rl_speed") == 0)
	{
		if (ent->client->pers.skills.rl_speed < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.rl_speed++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Rocket Launcher Speed increased to level {}!\n", ent->client->pers.skills.rl_speed);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Rocket Launcher Speed is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenRLUpgradeMenu(ent);
	}
	else if (strcmp(arg, "rl_radius") == 0)
	{
		if (ent->client->pers.skills.rl_radius < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.rl_radius++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Rocket Launcher Radius Damage increased to level {}!\n", ent->client->pers.skills.rl_radius);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Rocket Launcher Radius is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenRLUpgradeMenu(ent);
	}
	else if (strcmp(arg, "rl_trails") == 0)
	{
		ent->client->pers.skills.rl_trails = !ent->client->pers.skills.rl_trails;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Rocket Launcher Trails: {}\n", ent->client->pers.skills.rl_trails ? "DISABLED" : "ENABLED");
		PMenu_Close(ent);
		OpenRLUpgradeMenu(ent);
	}
	else if (strcmp(arg, "rl_silent") == 0)
	{
		ent->client->pers.skills.rl_silent = !ent->client->pers.skills.rl_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Rocket Launcher Silent Mode: {}\n", ent->client->pers.skills.rl_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenRLUpgradeMenu(ent);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// GRENADE LAUNCHER UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t gl_upgrade_menu[32];

void GLUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenGLUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(gl_upgrade_menu, 0, sizeof(gl_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(gl_upgrade_menu[count].text, text, sizeof(gl_upgrade_menu[count].text));
			gl_upgrade_menu[count].align = align;
			gl_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(gl_upgrade_menu[count].text_arg1, arg, sizeof(gl_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.gl_damage +
	                       ent->client->pers.skills.gl_range +
	                       ent->client->pers.skills.gl_radius;
	int max_upgrades = 30; // 3 stats * 10 max each
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== GRENADE LAUNCHER (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.gl_damage);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_damage");

	snprintf(status, sizeof(status), "Range %d [10]", ent->client->pers.skills.gl_range);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_range");

	snprintf(status, sizeof(status), "Radius %d [10]", ent->client->pers.skills.gl_radius);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_radius");

	const char *trails_status = ent->client->pers.skills.gl_trails ? "ON" : "OFF";
	snprintf(status, sizeof(status), "No Trails: %s", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_trails");

	const char *silent_status = ent->client->pers.skills.gl_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_silent");

	const char *bouncy_status = ent->client->pers.skills.gl_bouncy ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Bouncy Grenades: %s", bouncy_status);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_bouncy");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, gl_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void GLUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	int cursor = p->cur; // Save cursor position for reopening

	if (strcmp(arg, "gl_damage") == 0)
	{
		if (ent->client->pers.skills.gl_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.gl_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Grenade Launcher Damage increased to level {}!\n", ent->client->pers.skills.gl_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Grenade Launcher Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenGLUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "gl_range") == 0)
	{
		if (ent->client->pers.skills.gl_range < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.gl_range++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Grenade Launcher Range increased to level {}!\n", ent->client->pers.skills.gl_range);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Grenade Launcher Speed is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenGLUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "gl_radius") == 0)
	{
		if (ent->client->pers.skills.gl_radius < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.gl_radius++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Grenade Launcher Radius Damage increased to level {}!\n", ent->client->pers.skills.gl_radius);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Grenade Launcher Radius is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenGLUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "gl_trails") == 0)
	{
		ent->client->pers.skills.gl_trails = !ent->client->pers.skills.gl_trails;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Grenade Launcher Trails: {}\n", ent->client->pers.skills.gl_trails ? "DISABLED" : "ENABLED");
		PMenu_Close(ent);
		OpenGLUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "gl_silent") == 0)
	{
		ent->client->pers.skills.gl_silent = !ent->client->pers.skills.gl_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Grenade Launcher Silent Mode: {}\n", ent->client->pers.skills.gl_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenGLUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "gl_bouncy") == 0)
	{
		ent->client->pers.skills.gl_bouncy = !ent->client->pers.skills.gl_bouncy;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Grenade Launcher Bouncy Grenades: {}\n", ent->client->pers.skills.gl_bouncy ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenGLUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// MACHINEGUN UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t mg_upgrade_menu[32];

void MGUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenMGUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(mg_upgrade_menu, 0, sizeof(mg_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(mg_upgrade_menu[count].text, text, sizeof(mg_upgrade_menu[count].text));
			mg_upgrade_menu[count].align = align;
			mg_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(mg_upgrade_menu[count].text_arg1, arg, sizeof(mg_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.mg_damage +
	                       ent->client->pers.skills.mg_pierce +
	                       ent->client->pers.skills.mg_tracers +
	                       ent->client->pers.skills.mg_spread;
	int max_upgrades = 31;
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== MACHINEGUN (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.mg_damage);
	add_entry(status, PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "mg_damage");

	snprintf(status, sizeof(status), "Pierce %d [10]", ent->client->pers.skills.mg_pierce);
	add_entry(status, PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "mg_pierce");

	snprintf(status, sizeof(status), "Tracers %d [10]", ent->client->pers.skills.mg_tracers);
	add_entry(status, PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "mg_tracers");

	const char *spread_status = ent->client->pers.skills.mg_spread ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Reduced Spread: %s", spread_status);
	add_entry(status, PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "mg_spread");

	const char *silent_status = ent->client->pers.skills.mg_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "mg_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, mg_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void MGUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "mg_damage") == 0)
	{
		if (ent->client->pers.skills.mg_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.mg_damage++;
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Machinegun Damage increased to level {}!\n", ent->client->pers.skills.mg_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Machinegun Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenMGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "mg_pierce") == 0)
	{
		if (ent->client->pers.skills.mg_pierce < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.mg_pierce++;
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Machinegun Pierce increased to level {}!\n", ent->client->pers.skills.mg_pierce);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Machinegun Pierce is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenMGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "mg_tracers") == 0)
	{
		if (ent->client->pers.skills.mg_tracers < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.mg_tracers++;
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Machinegun Tracers increased to level {}!\n", ent->client->pers.skills.mg_tracers);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Machinegun Tracers is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenMGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "mg_spread") == 0)
	{
		ent->client->pers.skills.mg_spread = !ent->client->pers.skills.mg_spread;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Machinegun Reduced Spread: {}\n", ent->client->pers.skills.mg_spread ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenMGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "mg_silent") == 0)
	{
		ent->client->pers.skills.mg_silent = !ent->client->pers.skills.mg_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Machinegun Silent Mode: {}\n", ent->client->pers.skills.mg_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenMGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// CHAINGUN UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t cg_upgrade_menu[32];
static pmenu_t sg_upgrade_menu[32];
static pmenu_t ssg_upgrade_menu[32];

void CGUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenCGUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(cg_upgrade_menu, 0, sizeof(cg_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(cg_upgrade_menu[count].text, text, sizeof(cg_upgrade_menu[count].text));
			cg_upgrade_menu[count].align = align;
			cg_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(cg_upgrade_menu[count].text_arg1, arg, sizeof(cg_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.cg_damage +
	                       ent->client->pers.skills.cg_spin +
	                       ent->client->pers.skills.cg_tracers +
	                       ent->client->pers.skills.cg_spread;
	int max_upgrades = 31;
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== CHAINGUN (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.cg_damage);
	add_entry(status, PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "cg_damage");

	snprintf(status, sizeof(status), "Spin %d [10]", ent->client->pers.skills.cg_spin);
	add_entry(status, PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "cg_spin");

	snprintf(status, sizeof(status), "Tracers %d [10]", ent->client->pers.skills.cg_tracers);
	add_entry(status, PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "cg_tracers");

	const char *spread_status = ent->client->pers.skills.cg_spread ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Reduced Spread: %s", spread_status);
	add_entry(status, PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "cg_spread");

	const char *silent_status = ent->client->pers.skills.cg_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "cg_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, cg_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void CGUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "cg_damage") == 0)
	{
		if (ent->client->pers.skills.cg_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.cg_damage++;
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chaingun Damage increased to level {}!\n", ent->client->pers.skills.cg_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chaingun Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenCGUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "cg_spin") == 0)
	{
		if (ent->client->pers.skills.cg_spin < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.cg_spin++;
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chaingun Spin increased to level {}!\n", ent->client->pers.skills.cg_spin);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chaingun Spin is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenCGUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "cg_tracers") == 0)
	{
		if (ent->client->pers.skills.cg_tracers < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.cg_tracers++;
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chaingun Tracers increased to level {}!\n", ent->client->pers.skills.cg_tracers);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chaingun Tracers is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenCGUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "cg_spread") == 0)
	{
		ent->client->pers.skills.cg_spread = !ent->client->pers.skills.cg_spread;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chaingun Reduced Spread: {}\n", ent->client->pers.skills.cg_spread ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenCGUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "cg_silent") == 0)
	{
		ent->client->pers.skills.cg_silent = !ent->client->pers.skills.cg_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chaingun Silent Mode: {}\n", ent->client->pers.skills.cg_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenCGUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// SHOTGUN UPGRADE SUBMENU
/////////////////////////////////////////////

void OpenSGUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(sg_upgrade_menu, 0, sizeof(sg_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(sg_upgrade_menu[count].text, text, sizeof(sg_upgrade_menu[count].text));
			sg_upgrade_menu[count].align = align;
			sg_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(sg_upgrade_menu[count].text_arg1, arg, sizeof(sg_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.sg_damage +
	                       ent->client->pers.skills.sg_strike +
	                       ent->client->pers.skills.sg_pellets;
	int max_upgrades = 30;
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== SHOTGUN (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.sg_damage);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		if (ent->client->pers.skills.sg_damage < 10) {
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.sg_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Damage increased to level {}!\n", ent->client->pers.skills.sg_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		} else {
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenSGUpgradeMenu(ent);
	}, "sg_damage");

	snprintf(status, sizeof(status), "Strike %d [10]", ent->client->pers.skills.sg_strike);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		if (ent->client->pers.skills.sg_strike < 10) {
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.sg_strike++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Strike increased to level {}!\n", ent->client->pers.skills.sg_strike);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		} else {
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Strike is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenSGUpgradeMenu(ent);
	}, "sg_strike");

	snprintf(status, sizeof(status), "Pellets %d [10]", ent->client->pers.skills.sg_pellets);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		if (ent->client->pers.skills.sg_pellets < 10) {
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.sg_pellets++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Pellets increased to level {}!\n", ent->client->pers.skills.sg_pellets);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		} else {
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Pellets is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenSGUpgradeMenu(ent);
	}, "sg_pellets");

	const char *spread_status = ent->client->pers.skills.sg_spread ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Reduced Spread: %s", spread_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.sg_spread = !ent->client->pers.skills.sg_spread;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Reduced Spread: {}\n", ent->client->pers.skills.sg_spread ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSGUpgradeMenu(ent);
	}, "sg_spread");

	const char *silent_status = ent->client->pers.skills.sg_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.sg_silent = !ent->client->pers.skills.sg_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Silent Mode: {}\n", ent->client->pers.skills.sg_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSGUpgradeMenu(ent);
	}, "sg_silent");

	const char *energized_status = ent->client->pers.skills.sg_energized ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Energized Shells: %s", energized_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.sg_energized = !ent->client->pers.skills.sg_energized;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Energized Shells: {}\n", ent->client->pers.skills.sg_energized ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSGUpgradeMenu(ent);
	}, "sg_energized");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}, "back_to_weapons");

	PMenu_Open(ent, sg_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

/////////////////////////////////////////////
// SUPER SHOTGUN UPGRADE SUBMENU
/////////////////////////////////////////////

void OpenSSGUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(ssg_upgrade_menu, 0, sizeof(ssg_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(ssg_upgrade_menu[count].text, text, sizeof(ssg_upgrade_menu[count].text));
			ssg_upgrade_menu[count].align = align;
			ssg_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(ssg_upgrade_menu[count].text_arg1, arg, sizeof(ssg_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.ssg_damage +
	                       ent->client->pers.skills.ssg_strike +
	                       ent->client->pers.skills.ssg_pellets;
	int max_upgrades = 30;
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== SUPER SHOTGUN (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.ssg_damage);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		if (ent->client->pers.skills.ssg_damage < 10) {
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.ssg_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Damage increased to level {}!\n", ent->client->pers.skills.ssg_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		} else {
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenSSGUpgradeMenu(ent);
	}, "ssg_damage");

	snprintf(status, sizeof(status), "Strike %d [10]", ent->client->pers.skills.ssg_strike);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		if (ent->client->pers.skills.ssg_strike < 10) {
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.ssg_strike++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Strike increased to level {}!\n", ent->client->pers.skills.ssg_strike);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		} else {
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Strike is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenSSGUpgradeMenu(ent);
	}, "ssg_strike");

	snprintf(status, sizeof(status), "Pellets %d [10]", ent->client->pers.skills.ssg_pellets);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		if (ent->client->pers.skills.ssg_pellets < 10) {
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.ssg_pellets++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Pellets increased to level {}!\n", ent->client->pers.skills.ssg_pellets);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		} else {
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Pellets is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenSSGUpgradeMenu(ent);
	}, "ssg_pellets");

	const char *spread_status = ent->client->pers.skills.ssg_spread ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Reduced Spread: %s", spread_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.ssg_spread = !ent->client->pers.skills.ssg_spread;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Reduced Spread: {}\n", ent->client->pers.skills.ssg_spread ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSSGUpgradeMenu(ent);
	}, "ssg_spread");

	const char *silent_status = ent->client->pers.skills.ssg_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.ssg_silent = !ent->client->pers.skills.ssg_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Silent Mode: {}\n", ent->client->pers.skills.ssg_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSSGUpgradeMenu(ent);
	}, "ssg_silent");

	const char *energized_status = ent->client->pers.skills.ssg_energized ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Energized Shells: %s", energized_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.ssg_energized = !ent->client->pers.skills.ssg_energized;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Energized Shells: {}\n", ent->client->pers.skills.ssg_energized ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSSGUpgradeMenu(ent);
	}, "ssg_energized");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}, "back_to_weapons");

	PMenu_Open(ent, ssg_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

/////////////////////////////////////////////
// HAND GRENADE UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t hg_upgrade_menu[32];

void HGUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenHGUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(hg_upgrade_menu, 0, sizeof(hg_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(hg_upgrade_menu[count].text, text, sizeof(hg_upgrade_menu[count].text));
			hg_upgrade_menu[count].align = align;
			hg_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(hg_upgrade_menu[count].text_arg1, arg, sizeof(hg_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.hg_damage +
	                       ent->client->pers.skills.hg_range +
	                       ent->client->pers.skills.hg_radius_damage;
	int max_upgrades = 30; // 3 stats * 10 max each
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== HAND GRENADES (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.hg_damage);
	add_entry(status, PMENU_ALIGN_LEFT, HGUpgradeMenuHandler, "hg_damage");

	snprintf(status, sizeof(status), "Range %d [10]", ent->client->pers.skills.hg_range);
	add_entry(status, PMENU_ALIGN_LEFT, HGUpgradeMenuHandler, "hg_range");

	snprintf(status, sizeof(status), "Radius Damage %d [10]", ent->client->pers.skills.hg_radius_damage);
	add_entry(status, PMENU_ALIGN_LEFT, HGUpgradeMenuHandler, "hg_radius_damage");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, HGUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, hg_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void HGUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	int cursor = p->cur; // Save cursor position for reopening

	if (strcmp(arg, "hg_damage") == 0)
	{
		if (ent->client->pers.skills.hg_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.hg_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hand Grenade Damage increased to level {}!\n", ent->client->pers.skills.hg_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hand Grenade Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenHGUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "hg_range") == 0)
	{
		if (ent->client->pers.skills.hg_range < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.hg_range++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hand Grenade Range increased to level {}!\n", ent->client->pers.skills.hg_range);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hand Grenade Range is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenHGUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "hg_radius_damage") == 0)
	{
		if (ent->client->pers.skills.hg_radius_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.hg_radius_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hand Grenade Radius Damage increased to level {}!\n", ent->client->pers.skills.hg_radius_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hand Grenade Radius Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenHGUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// PROX LAUNCHER UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t prox_upgrade_menu[32];

void ProxUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenProxUpgradeMenu(edict_t *ent)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(prox_upgrade_menu, 0, sizeof(prox_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(prox_upgrade_menu[count].text, text, sizeof(prox_upgrade_menu[count].text));
			prox_upgrade_menu[count].align = align;
			prox_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(prox_upgrade_menu[count].text_arg1, arg, sizeof(prox_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.pl_damage +
	                       ent->client->pers.skills.pl_range +
	                       ent->client->pers.skills.pl_radius;
	int max_upgrades = 30;
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== PROX LAUNCHER (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.pl_damage);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_damage");

	snprintf(status, sizeof(status), "Range %d [10]", ent->client->pers.skills.pl_range);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_range");

	snprintf(status, sizeof(status), "Radius %d [10]", ent->client->pers.skills.pl_radius);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_radius");

	const char *trails_status = ent->client->pers.skills.pl_trails ? "ON" : "OFF";
	snprintf(status, sizeof(status), "No Trails: %s", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_trails");

	const char *silent_status = ent->client->pers.skills.pl_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_silent");

	const char *improved_status = ent->client->pers.skills.pl_improved_traps ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Improved Traps: %s", improved_status);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_improved_traps");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, prox_upgrade_menu, -1, count, nullptr, nullptr);
}

void ProxUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "pl_damage") == 0)
	{
		if (ent->client->pers.skills.pl_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.pl_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Damage increased to level {}!\n", ent->client->pers.skills.pl_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenProxUpgradeMenu(ent);
	}
	else if (strcmp(arg, "pl_range") == 0)
	{
		if (ent->client->pers.skills.pl_range < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.pl_range++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Range increased to level {}!\n", ent->client->pers.skills.pl_range);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Range is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenProxUpgradeMenu(ent);
	}
	else if (strcmp(arg, "pl_radius") == 0)
	{
		if (ent->client->pers.skills.pl_radius < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.pl_radius++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Radius Damage increased to level {}!\n", ent->client->pers.skills.pl_radius);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Radius is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenProxUpgradeMenu(ent);
	}
	else if (strcmp(arg, "pl_trails") == 0)
	{
		ent->client->pers.skills.pl_trails = !ent->client->pers.skills.pl_trails;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Trails: {}\n", ent->client->pers.skills.pl_trails ? "DISABLED" : "ENABLED");
		PMenu_Close(ent);
		OpenProxUpgradeMenu(ent);
	}
	else if (strcmp(arg, "pl_silent") == 0)
	{
		ent->client->pers.skills.pl_silent = !ent->client->pers.skills.pl_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Silent Mode: {}\n", ent->client->pers.skills.pl_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenProxUpgradeMenu(ent);
	}
	else if (strcmp(arg, "pl_improved_traps") == 0)
	{
		ent->client->pers.skills.pl_improved_traps = !ent->client->pers.skills.pl_improved_traps;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Improved Traps: {}\n", ent->client->pers.skills.pl_improved_traps ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenProxUpgradeMenu(ent);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// BLASTER UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t blaster_upgrade_menu[32];

void BlasterUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenBlasterUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(blaster_upgrade_menu, 0, sizeof(blaster_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(blaster_upgrade_menu[count].text, text, sizeof(blaster_upgrade_menu[count].text));
			blaster_upgrade_menu[count].align = align;
			blaster_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(blaster_upgrade_menu[count].text_arg1, arg, sizeof(blaster_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	add_entry("=== BLASTER ===", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.bl_damage);
	add_entry(status, PMENU_ALIGN_LEFT, BlasterUpgradeMenuHandler, "bl_damage");

	snprintf(status, sizeof(status), "Speed %d [10]", ent->client->pers.skills.bl_speed);
	add_entry(status, PMENU_ALIGN_LEFT, BlasterUpgradeMenuHandler, "bl_speed");

	const char *trails_status = ent->client->pers.skills.bl_trails ? "DISABLED" : "ENABLED";
	snprintf(status, sizeof(status), "Trails: %s", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, BlasterUpgradeMenuHandler, "bl_trails");

	const char *silent_status = ent->client->pers.skills.bl_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, BlasterUpgradeMenuHandler, "bl_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, BlasterUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, blaster_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void BlasterUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "bl_damage") == 0)
	{
		if (ent->client->pers.skills.bl_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.bl_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Blaster Damage increased to level {}!\n", ent->client->pers.skills.bl_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Blaster Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenBlasterUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "bl_speed") == 0)
	{
		if (ent->client->pers.skills.bl_speed < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.bl_speed++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Blaster Speed increased to level {}!\n", ent->client->pers.skills.bl_speed);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Blaster Speed is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenBlasterUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "bl_trails") == 0)
	{
		ent->client->pers.skills.bl_trails = !ent->client->pers.skills.bl_trails;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Blaster Trails: {}\n", ent->client->pers.skills.bl_trails ? "DISABLED" : "ENABLED");
		PMenu_Close(ent);
		OpenBlasterUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "bl_silent") == 0)
	{
		ent->client->pers.skills.bl_silent = !ent->client->pers.skills.bl_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Blaster Silent Mode: {}\n", ent->client->pers.skills.bl_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenBlasterUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// HYPERBLASTER UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t hyperblaster_upgrade_menu[32];

void HyperblasterUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenHyperblasterUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(hyperblaster_upgrade_menu, 0, sizeof(hyperblaster_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(hyperblaster_upgrade_menu[count].text, text, sizeof(hyperblaster_upgrade_menu[count].text));
			hyperblaster_upgrade_menu[count].align = align;
			hyperblaster_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(hyperblaster_upgrade_menu[count].text_arg1, arg, sizeof(hyperblaster_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	add_entry("=== HYPERBLASTER ===", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.hb_damage);
	add_entry(status, PMENU_ALIGN_LEFT, HyperblasterUpgradeMenuHandler, "hb_damage");

	snprintf(status, sizeof(status), "Speed %d [10]", ent->client->pers.skills.hb_speed);
	add_entry(status, PMENU_ALIGN_LEFT, HyperblasterUpgradeMenuHandler, "hb_speed");

	const char *trails_status = ent->client->pers.skills.hb_trails ? "DISABLED" : "ENABLED";
	snprintf(status, sizeof(status), "Trails: %s", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, HyperblasterUpgradeMenuHandler, "hb_trails");

	const char *silent_status = ent->client->pers.skills.hb_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, HyperblasterUpgradeMenuHandler, "hb_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, HyperblasterUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, hyperblaster_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void HyperblasterUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "hb_damage") == 0)
	{
		if (ent->client->pers.skills.hb_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.hb_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hyperblaster Damage increased to level {}!\n", ent->client->pers.skills.hb_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hyperblaster Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenHyperblasterUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "hb_speed") == 0)
	{
		if (ent->client->pers.skills.hb_speed < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.hb_speed++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hyperblaster Speed increased to level {}!\n", ent->client->pers.skills.hb_speed);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hyperblaster Speed is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenHyperblasterUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "hb_trails") == 0)
	{
		ent->client->pers.skills.hb_trails = !ent->client->pers.skills.hb_trails;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hyperblaster Trails: {}\n", ent->client->pers.skills.hb_trails ? "DISABLED" : "ENABLED");
		PMenu_Close(ent);
		OpenHyperblasterUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "hb_silent") == 0)
	{
		ent->client->pers.skills.hb_silent = !ent->client->pers.skills.hb_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Hyperblaster Silent Mode: {}\n", ent->client->pers.skills.hb_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenHyperblasterUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// ETF RIFLE UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t etf_upgrade_menu[32];

void ETFUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenETFUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(etf_upgrade_menu, 0, sizeof(etf_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(etf_upgrade_menu[count].text, text, sizeof(etf_upgrade_menu[count].text));
			etf_upgrade_menu[count].align = align;
			etf_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(etf_upgrade_menu[count].text_arg1, arg, sizeof(etf_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	add_entry("=== ETF RIFLE ===", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.etf_damage);
	add_entry(status, PMENU_ALIGN_LEFT, ETFUpgradeMenuHandler, "etf_damage");

	snprintf(status, sizeof(status), "Speed %d [10]", ent->client->pers.skills.etf_speed);
	add_entry(status, PMENU_ALIGN_LEFT, ETFUpgradeMenuHandler, "etf_speed");

	snprintf(status, sizeof(status), "Kick %d [10]", ent->client->pers.skills.etf_kick);
	add_entry(status, PMENU_ALIGN_LEFT, ETFUpgradeMenuHandler, "etf_kick");

	const char *silent_status = ent->client->pers.skills.etf_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, ETFUpgradeMenuHandler, "etf_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, ETFUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, etf_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void ETFUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "etf_damage") == 0)
	{
		if (ent->client->pers.skills.etf_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.etf_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "ETF Rifle Damage increased to level {}!\n", ent->client->pers.skills.etf_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "ETF Rifle Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenETFUpgradeMenu(ent);
	}
	else if (strcmp(arg, "etf_speed") == 0)
	{
		if (ent->client->pers.skills.etf_speed < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.etf_speed++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "ETF Rifle Speed increased to level {}!\n", ent->client->pers.skills.etf_speed);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "ETF Rifle Speed is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenETFUpgradeMenu(ent);
	}
	else if (strcmp(arg, "etf_kick") == 0)
	{
		if (ent->client->pers.skills.etf_kick < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.etf_kick++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "ETF Rifle Kick increased to level {}!\n", ent->client->pers.skills.etf_kick);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "ETF Rifle Kick is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenETFUpgradeMenu(ent);
	}
	else if (strcmp(arg, "etf_silent") == 0)
	{
		ent->client->pers.skills.etf_silent = !ent->client->pers.skills.etf_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "ETF Rifle Silent Mode: {}\n", ent->client->pers.skills.etf_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenETFUpgradeMenu(ent);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// ION RIPPER UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t ionripper_upgrade_menu[32];

void IonRipperUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenIonRipperUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(ionripper_upgrade_menu, 0, sizeof(ionripper_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(ionripper_upgrade_menu[count].text, text, sizeof(ionripper_upgrade_menu[count].text));
			ionripper_upgrade_menu[count].align = align;
			ionripper_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(ionripper_upgrade_menu[count].text_arg1, arg, sizeof(ionripper_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	add_entry("=== ION RIPPER ===", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.ir_damage);
	add_entry(status, PMENU_ALIGN_LEFT, IonRipperUpgradeMenuHandler, "ir_damage");

	snprintf(status, sizeof(status), "Speed %d [10]", ent->client->pers.skills.ir_speed);
	add_entry(status, PMENU_ALIGN_LEFT, IonRipperUpgradeMenuHandler, "ir_speed");

	const char *trails_status = ent->client->pers.skills.ir_trails ? "OFF" : "ON";
	snprintf(status, sizeof(status), "Trails: %s", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, IonRipperUpgradeMenuHandler, "ir_trails");

	const char *silent_status = ent->client->pers.skills.ir_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, IonRipperUpgradeMenuHandler, "ir_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, IonRipperUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, ionripper_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void IonRipperUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "ir_damage") == 0)
	{
		if (ent->client->pers.skills.ir_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.ir_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Ion Ripper Damage increased to level {}!\n", ent->client->pers.skills.ir_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Ion Ripper Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenIonRipperUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "ir_speed") == 0)
	{
		if (ent->client->pers.skills.ir_speed < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.ir_speed++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Ion Ripper Speed increased to level {}!\n", ent->client->pers.skills.ir_speed);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Ion Ripper Speed is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenIonRipperUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "ir_trails") == 0)
	{
		ent->client->pers.skills.ir_trails = !ent->client->pers.skills.ir_trails;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Ion Ripper Trails: {}\n", ent->client->pers.skills.ir_trails ? "OFF" : "ON");
		PMenu_Close(ent);
		OpenIonRipperUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "ir_silent") == 0)
	{
		ent->client->pers.skills.ir_silent = !ent->client->pers.skills.ir_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Ion Ripper Silent Mode: {}\n", ent->client->pers.skills.ir_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenIonRipperUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// RAILGUN UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t railgun_upgrade_menu[32];

void RailgunUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenRailgunUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(railgun_upgrade_menu, 0, sizeof(railgun_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(railgun_upgrade_menu[count].text, text, sizeof(railgun_upgrade_menu[count].text));
			railgun_upgrade_menu[count].align = align;
			railgun_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(railgun_upgrade_menu[count].text_arg1, arg, sizeof(railgun_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	add_entry("=== RAILGUN ===", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.rg_damage);
	add_entry(status, PMENU_ALIGN_LEFT, RailgunUpgradeMenuHandler, "rg_damage");

	snprintf(status, sizeof(status), "Burn %d [10]", ent->client->pers.skills.rg_burn);
	add_entry(status, PMENU_ALIGN_LEFT, RailgunUpgradeMenuHandler, "rg_burn");

	snprintf(status, sizeof(status), "Pierce %d [10]", ent->client->pers.skills.rg_pierce);
	add_entry(status, PMENU_ALIGN_LEFT, RailgunUpgradeMenuHandler, "rg_pierce");

	const char *silent_status = ent->client->pers.skills.rg_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, RailgunUpgradeMenuHandler, "rg_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, RailgunUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, railgun_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void RailgunUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "rg_damage") == 0)
	{
		if (ent->client->pers.skills.rg_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.rg_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Railgun Damage increased to level {}!\n", ent->client->pers.skills.rg_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Railgun Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenRailgunUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "rg_burn") == 0)
	{
		if (ent->client->pers.skills.rg_burn < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.rg_burn++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Railgun Burn increased to level {}!\n", ent->client->pers.skills.rg_burn);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Railgun Burn is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenRailgunUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "rg_pierce") == 0)
	{
		if (ent->client->pers.skills.rg_pierce < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.rg_pierce++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Railgun Pierce increased to level {}!\n", ent->client->pers.skills.rg_pierce);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Railgun Pierce is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenRailgunUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "rg_silent") == 0)
	{
		ent->client->pers.skills.rg_silent = !ent->client->pers.skills.rg_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Railgun Silent Mode: {}\n", ent->client->pers.skills.rg_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenRailgunUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// BFG10K UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t bfg_upgrade_menu[32];

void BFGUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenBFGUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(bfg_upgrade_menu, 0, sizeof(bfg_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(bfg_upgrade_menu[count].text, text, sizeof(bfg_upgrade_menu[count].text));
			bfg_upgrade_menu[count].align = align;
			bfg_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(bfg_upgrade_menu[count].text_arg1, arg, sizeof(bfg_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	add_entry("=== BFG10K ===", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.bfg_damage);
	add_entry(status, PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "bfg_damage");

	snprintf(status, sizeof(status), "Speed %d [10]", ent->client->pers.skills.bfg_speed);
	add_entry(status, PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "bfg_speed");

	snprintf(status, sizeof(status), "Duration %d [10]", ent->client->pers.skills.bfg_duration);
	add_entry(status, PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "bfg_duration");

	// BFG Mode display
	const char *mode_str = "Normal";
	if (ent->client->pers.bfg_mode == BFGMode::SLIDE)
		mode_str = "Slide";
	else if (ent->client->pers.bfg_mode == BFGMode::GRAV_PULL)
		mode_str = "Pull";

	snprintf(status, sizeof(status), "Mode: %s", mode_str);
	add_entry(status, PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "bfg_mode");

	const char *silent_status = ent->client->pers.skills.bfg_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "bfg_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, bfg_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void BFGUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "bfg_damage") == 0)
	{
		if (ent->client->pers.skills.bfg_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.bfg_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "BFG10K Damage increased to level {}!\n", ent->client->pers.skills.bfg_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "BFG10K Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenBFGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "bfg_speed") == 0)
	{
		if (ent->client->pers.skills.bfg_speed < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.bfg_speed++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "BFG10K Speed increased to level {}!\n", ent->client->pers.skills.bfg_speed);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "BFG10K Speed is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenBFGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "bfg_duration") == 0)
	{
		if (ent->client->pers.skills.bfg_duration < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.bfg_duration++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "BFG10K Duration increased to level {}!\n", ent->client->pers.skills.bfg_duration);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "BFG10K Duration is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenBFGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "bfg_mode") == 0)
	{
		// Cycle through modes: Normal -> Slide -> Pull -> Normal
		if (ent->client->pers.bfg_mode == BFGMode::NORMAL)
		{
			ent->client->pers.bfg_mode = BFGMode::SLIDE;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "BFG10K Mode: Slide\n");
		}
		else if (ent->client->pers.bfg_mode == BFGMode::SLIDE)
		{
			ent->client->pers.bfg_mode = BFGMode::GRAV_PULL;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "BFG10K Mode: Pull\n");
		}
		else
		{
			ent->client->pers.bfg_mode = BFGMode::NORMAL;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "BFG10K Mode: Normal\n");
		}
		PMenu_Close(ent);
		OpenBFGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "bfg_silent") == 0)
	{
		ent->client->pers.skills.bfg_silent = !ent->client->pers.skills.bfg_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "BFG10K Silent Mode: {}\n", ent->client->pers.skills.bfg_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenBFGUpgradeMenu(ent);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// 20MM CANNON (ETG) UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t cannon20mm_upgrade_menu[32];

void ETGUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void Open20mmCannonUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(cannon20mm_upgrade_menu, 0, sizeof(cannon20mm_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(cannon20mm_upgrade_menu[count].text, text, sizeof(cannon20mm_upgrade_menu[count].text));
			cannon20mm_upgrade_menu[count].align = align;
			cannon20mm_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(cannon20mm_upgrade_menu[count].text_arg1, arg, sizeof(cannon20mm_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	add_entry("=== 20MM CANNON ===", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.cannon20mm_damage);
	add_entry(status, PMENU_ALIGN_LEFT, ETGUpgradeMenuHandler, "cannon20mm_damage");

	snprintf(status, sizeof(status), "Range %d [10]", ent->client->pers.skills.cannon20mm_range);
	add_entry(status, PMENU_ALIGN_LEFT, ETGUpgradeMenuHandler, "cannon20mm_range");

	snprintf(status, sizeof(status), "Recoil Reduction %d [10]", ent->client->pers.skills.cannon20mm_recoil);
	add_entry(status, PMENU_ALIGN_LEFT, ETGUpgradeMenuHandler, "cannon20mm_recoil");

	const char *silent_status = ent->client->pers.skills.cannon20mm_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, ETGUpgradeMenuHandler, "cannon20mm_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, ETGUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, cannon20mm_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void ETGUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "cannon20mm_damage") == 0)
	{
		if (ent->client->pers.skills.cannon20mm_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.cannon20mm_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "20mm Cannon Damage increased to level {}!\n", ent->client->pers.skills.cannon20mm_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "20mm Cannon Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		Open20mmCannonUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "cannon20mm_range") == 0)
	{
		if (ent->client->pers.skills.cannon20mm_range < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.cannon20mm_range++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "20mm Cannon Range increased to level {}!\n", ent->client->pers.skills.cannon20mm_range);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "20mm Cannon Range is already at maximum level!\n");
		}
		PMenu_Close(ent);
		Open20mmCannonUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "cannon20mm_recoil") == 0)
	{
		if (ent->client->pers.skills.cannon20mm_recoil < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.cannon20mm_recoil++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "20mm Cannon Recoil Reduction increased to level {}!\n", ent->client->pers.skills.cannon20mm_recoil);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "20mm Cannon Recoil Reduction is already at maximum level!\n");
		}
		PMenu_Close(ent);
		Open20mmCannonUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "cannon20mm_silent") == 0)
	{
		ent->client->pers.skills.cannon20mm_silent = !ent->client->pers.skills.cannon20mm_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "20mm Cannon Silent Mode: {}\n", ent->client->pers.skills.cannon20mm_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		Open20mmCannonUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// PLASMABEAM UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t plasmabeam_upgrade_menu[32];

void PlasmabeamUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenPlasmabeamUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(plasmabeam_upgrade_menu, 0, sizeof(plasmabeam_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(plasmabeam_upgrade_menu[count].text, text, sizeof(plasmabeam_upgrade_menu[count].text));
			plasmabeam_upgrade_menu[count].align = align;
			plasmabeam_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(plasmabeam_upgrade_menu[count].text_arg1, arg, sizeof(plasmabeam_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	add_entry("=== PLASMABEAM ===", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.pb_damage);
	add_entry(status, PMENU_ALIGN_LEFT, PlasmabeamUpgradeMenuHandler, "pb_damage");

	snprintf(status, sizeof(status), "Burn %d [10]", ent->client->pers.skills.pb_burn);
	add_entry(status, PMENU_ALIGN_LEFT, PlasmabeamUpgradeMenuHandler, "pb_burn");

	snprintf(status, sizeof(status), "Pierce Level: %d/10 (%.0f%% chance)", ent->client->pers.skills.pb_pierce, ent->client->pers.skills.pb_pierce * 4.0f);
	add_entry(status, PMENU_ALIGN_LEFT, PlasmabeamUpgradeMenuHandler, "pb_pierce");

	const char *silent_status = ent->client->pers.skills.pb_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, PlasmabeamUpgradeMenuHandler, "pb_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, PlasmabeamUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, plasmabeam_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void PlasmabeamUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	if (strcmp(arg, "pb_damage") == 0)
	{
		if (ent->client->pers.skills.pb_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.pb_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Plasmabeam Damage increased to level {}!\n", ent->client->pers.skills.pb_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Plasmabeam Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenPlasmabeamUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "pb_burn") == 0)
	{
		if (ent->client->pers.skills.pb_burn < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.pb_burn++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Plasmabeam Burn increased to level {}!\n", ent->client->pers.skills.pb_burn);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Plasmabeam Burn is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenPlasmabeamUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "pb_pierce") == 0)
	{
		if (ent->client->pers.skills.pb_pierce < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.pb_pierce++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Plasmabeam Pierce increased to level {} ({}% chance)!\n", ent->client->pers.skills.pb_pierce, ent->client->pers.skills.pb_pierce * 4);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Plasmabeam Pierce is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenPlasmabeamUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "pb_silent") == 0)
	{
		ent->client->pers.skills.pb_silent = !ent->client->pers.skills.pb_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Plasmabeam Silent Mode: {}\n", ent->client->pers.skills.pb_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenPlasmabeamUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
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
class ScoreboardLayout
{
private:
	StringBuilder layout_builder;
	const edict_t *ent;
	std::vector<PlayerScore> team_players;
	std::vector<PlayerScore> spectators;
	int total_score;

	static constexpr size_t MAX_SPECTATORS_TO_DISPLAY = 8;

public:
	ScoreboardLayout(edict_t *player_ent, size_t reserve_size = MAX_CTF_STAT_LENGTH)
		: layout_builder(reserve_size), ent(player_ent), total_score(0)
	{
	}

	void collectPlayers()
	{
		for (unsigned int i = 0; i < game.maxclients; i++)
		{
			const edict_t *const cl_ent = g_edicts + 1 + i;
			if (!cl_ent->inuse)
				continue;

			const gclient_t *const cl = &game.clients[i];

			PlayerScore player = {
				i,
				cl->resp.score,
				std::clamp(cl->ping, 0, 999),
				(cl_ent->deadflag != 0)};

			if (cl->resp.ctf_team == CTF_TEAM1)
			{
				if (!safe_push_back(team_players, player, MAX_SAFE_CONTAINER_SIZE))
				{
					gi.Com_Print("WARNING: Too many team players for scoreboard\n");
				}
				else
				{
					total_score += player.score;
				}
			}
			else if (cl->resp.ctf_team == CTF_NOTEAM)
			{
				if (!safe_push_back(spectators, player, MAX_SAFE_CONTAINER_SIZE))
				{
					gi.Com_Print("WARNING: Too many spectators for scoreboard\n");
				}
			}
		}
		std::sort(team_players.begin(), team_players.end(), std::greater<>());
	}

	void addHeader()
	{
		if (g_horde->integer || pvm->integer)
		{
			// string2 is better than loc_string2 here it seems
			// Element 1: Wave Number (aligned left)
			layout_builder.append(fmt::format(
				"if 0 xv -140 yv -5 string2 \"Wave: {}\" endif \n",
				last_wave_number));

			// Element 2: Stroggs Remaining (aligned further to the right)
			layout_builder.append(fmt::format(
				"if 0 xv -40 yv -5 string2 \"Stroggs: {}\" endif \n",
				GetStroggsNum()));
		}

		// Time limit remains the same
		if (timelimit->value)
		{
			layout_builder.append(fmt::format(
				"if 0 xv 340 yv -33 time_limit {} endif \n",
				gi.ServerFrame() + ((gtime_t::from_min(timelimit->value) - level.time)).milliseconds() / gi.frame_time_ms));
		}
	}

	void addTeamScore()
	{
		// Define the path to your custom horde dogtag
		const char *horde_dogtag_path = "/tags/etqw_strogg.png"; // No file extension needed

		if (!level.intermissiontime)
		{
			// Use 'picn' to draw the specific horde dogtag image
			layout_builder.append(fmt::format(
				"if 25 xv -140 yv 3 picn {} endif \n", horde_dogtag_path));

			// Get the new, safely-limited active bonuses string
			//std::string activeBonuses = GetPlayerActiveBonusesString(const_cast<edict_t *>(ent));
			// if (!activeBonuses.empty())
			// {
			// 	layout_builder.append(fmt::format(
			// 		"if 0 xv 208 yv 8 string \"{}\" endif \n", activeBonuses));
			// }
		}
		else
		{
			// Intermission screen - split into separate appends for better error isolation
			// Each if/endif pair is self-contained and easier to debug
			layout_builder.append(fmt::format(
				"if 25 xv -140 yv 3 picn {} endif \n", horde_dogtag_path));
			layout_builder.append("if 25 xv 205 yv 3 pic 25 endif \n");
			layout_builder.append(fmt::format(
				"if 0 xv 70 yv -20 num 0 {} endif \n", total_score));
		}
	}

	void addPlayerList()
	{
		// Add column headers. The X coordinates here will be the same for the data below.
		int header_y = PLAYER_Y_START - PLAYER_Y_SPACING; // Position headers just above the first player
		layout_builder.append(fmt::format(
			"if 0 xv -140 yv {} string2 \"Name\" xv 70 yv {} string2 \"Score\" xv 120 yv {} string2 \"Lv\" xv 160 yv {} string2 \"Png\" endif \n",
			header_y, header_y, header_y, header_y));

		// Loop through players and display their info
		for (size_t i = 0; i < std::min(team_players.size(), MAX_PLAYERS_TO_DISPLAY); ++i)
		{
			// **FIX:** Add a safety check to prevent string overflow. 200 is a safe margin for one line.
			if (layout_builder.size() >= MAX_CTF_STAT_LENGTH - 200)
			{
				break;
			}

			const auto &player = team_players[i];
			edict_t *player_ent = g_edicts + 1 + player.index;
			int y = PLAYER_Y_START + i * PLAYER_Y_SPACING;

			// **FIX:** Build the content for the player row first, then wrap it in a single if...endif.
			std::string player_line_content;

			if (player.is_dead)
			{
				player_line_content += fmt::format("xv -175 yv {} string \"[RIP]\" ", y);
			}

			const char *player_name = GetPlayerName(player_ent);
			std::string score_str = fmt::format("{}", player.score);
			int32_t player_level = player_ent->client->pers.pvm_level;

			// // DEBUG: Print level info when building scoreboard
			// if (level.intermissiontime)
			// {
			// 	gi.Com_PrintFmt("DEBUG Scoreboard addPlayerList: Player {} ({}), index={}, pvm_level={}, pvm_xp={}\n",
			// 		player_name, i, player.index, player_level, player_ent->client->pers.pvm_xp);
			// }

			std::string level_str = fmt::format("{}", player_level);
			std::string ping_str = fmt::format("{}", player.ping);

			player_line_content += fmt::format(
				"xv -140 yv {} string \"{}\" "
				"xv 70 yv {} string \"{}\" "
				"xv 120 yv {} string \"{}\" "
				"xv 160 yv {} string \"{}\"",
				y, player_name, y, score_str, y, level_str, y, ping_str);

			layout_builder.append(fmt::format("if 0 {} endif \n", player_line_content));
		}
	}

	void addSpectators()
	{
		// Only add spectators if there's enough buffer space and there are spectators to show.
		if (layout_builder.size() < MAX_CTF_STAT_LENGTH - LAYOUT_SAFETY_MARGIN && !spectators.empty())
		{
			// Calculate the starting Y position for the spectator list, leaving a gap after the player list.
			int y = PLAYER_Y_START + (std::min(team_players.size(), MAX_PLAYERS_TO_DISPLAY) + 1) * PLAYER_Y_SPACING;

			// Add the "Spectators & AFK" header, aligning it with the "Name" column for consistency.
			layout_builder.append(fmt::format(
				"if 0 xv -90 yv {} string2 \"Spectators & AFK\" endif \n", y));
			y += PLAYER_Y_SPACING; // Add a bit more space after the header

			// Loop through the spectators to display, up to the defined maximum.
			size_t spectators_to_display = std::min(spectators.size(), MAX_SPECTATORS_TO_DISPLAY);
			for (size_t i = 0; i < spectators_to_display; ++i)
			{
				// **FIX:** Use a larger, safer margin for the check.
				if (layout_builder.size() >= MAX_CTF_STAT_LENGTH - 200)
				{
					break;
				}

				const auto &spec = spectators[i];
				edict_t *spec_ent = g_edicts + 1 + spec.index;
				const char *spec_name = GetPlayerName(spec_ent);
				std::string score_str = fmt::format("{}", spec.score);
				std::string ping_str = fmt::format("{}", spec.ping);

				// **FIX:** Consolidate all drawing commands into a single if...endif block.
				std::string spectator_line_content = fmt::format(
					"xv -140 yv {} string2 \"{}\" "
					"xv 70 yv {} string2 \"{}\" "
					"xv 160 yv {} string2 \"{}\"",
					y, spec_name, y, score_str, y, ping_str);

				layout_builder.append(fmt::format("if 0 {} endif \n", spectator_line_content));

				y += PLAYER_Y_SPACING;
			}

			// If there are more spectators than we can display, add the "... and X more" message.
			if (spectators.size() > spectators_to_display)
			{
				layout_builder.append(fmt::format(
					"if 0 xv -140 yv {} string2 \"... and {} more\" endif \n",
					y, spectators.size() - spectators_to_display));
			}
		}
	}

	void addFooter()
	{
		if (!level.intermissiontime)
		{
			const char *help_text = (ent->client->resp.ctf_team != CTF_TEAM1)
										? "Use Inventory <KEY> to toggle Horde Menu."
										: "Use Horde Menu on Powerup Wheel or press Inventory <KEY> to toggle Horde Menu.";
			layout_builder.append(fmt::format(
				"if 0 xv 0 yb -55 cstring2 \"{}\" endif \n", help_text));
		}
		else
		{
			// This block runs during the intermission.
			const char *message = brandom()
									  ? "MAKE THEM PAY !!!"
									  : "THEY WILL REGRET THIS !!!";

			// It will display the message after a 5-second delay.
			layout_builder.append(fmt::format(
				"ifgef {} yb -48 xv 0 loc_cstring2 0 \"{}\" endif \n",
				level.intermission_server_frame + (5_sec).frames(),
				message));
		}
	}

	std::string build()
	{
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

	//	gi.Com_PrintFmt("--- BEGIN SCOREBOARD LAYOUT ---\n{}\n--- END SCOREBOARD LAYOUT ---\n", final_layout.c_str());

	// Send to client
	gi.WriteByte(svc_layout);
	gi.WriteString(final_layout.c_str());
}

// --- END OF FILE horde_menu.cpp ---

/////////////////////////////////////////////
// CHAINFIST UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t chainfist_upgrade_menu[32];

void ChainfistUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenChainfistUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(chainfist_upgrade_menu, 0, sizeof(chainfist_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(chainfist_upgrade_menu[count].text, text, sizeof(chainfist_upgrade_menu[count].text));
			chainfist_upgrade_menu[count].align = align;
			chainfist_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(chainfist_upgrade_menu[count].text_arg1, arg, sizeof(chainfist_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.cf_damage +
	                       ent->client->pers.skills.cf_range;
	int max_upgrades = 20; // 2 stats * 10 max each
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== CHAINFIST (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.cf_damage);
	add_entry(status, PMENU_ALIGN_LEFT, ChainfistUpgradeMenuHandler, "cf_damage");

	snprintf(status, sizeof(status), "Range %d [10]", ent->client->pers.skills.cf_range);
	add_entry(status, PMENU_ALIGN_LEFT, ChainfistUpgradeMenuHandler, "cf_range");

	const char *silent_status = ent->client->pers.skills.cf_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, ChainfistUpgradeMenuHandler, "cf_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, ChainfistUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, chainfist_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void ChainfistUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	int cursor = p->cur; // Save cursor position for reopening

	if (strcmp(arg, "cf_damage") == 0)
	{
		if (ent->client->pers.skills.cf_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.cf_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chainfist Damage increased to level {}!\n", ent->client->pers.skills.cf_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chainfist Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenChainfistUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "cf_range") == 0)
	{
		if (ent->client->pers.skills.cf_range < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.cf_range++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chainfist Range increased to level {}!\n", ent->client->pers.skills.cf_range);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chainfist Range is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenChainfistUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "cf_silent") == 0)
	{
		ent->client->pers.skills.cf_silent = !ent->client->pers.skills.cf_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Chainfist Silent Mode: {}\n", ent->client->pers.skills.cf_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenChainfistUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// TESLA UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t tesla_upgrade_menu[32];

void TeslaUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenTeslaUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(tesla_upgrade_menu, 0, sizeof(tesla_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(tesla_upgrade_menu[count].text, text, sizeof(tesla_upgrade_menu[count].text));
			tesla_upgrade_menu[count].align = align;
			tesla_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(tesla_upgrade_menu[count].text_arg1, arg, sizeof(tesla_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.tesla_damage +
	                       ent->client->pers.skills.tesla_range +
	                       ent->client->pers.skills.tesla_radius;
	int max_upgrades = 30; // 3 stats * 10 max each
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== TESLA (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.tesla_damage);
	add_entry(status, PMENU_ALIGN_LEFT, TeslaUpgradeMenuHandler, "tesla_damage");

	snprintf(status, sizeof(status), "Range %d [10]", ent->client->pers.skills.tesla_range);
	add_entry(status, PMENU_ALIGN_LEFT, TeslaUpgradeMenuHandler, "tesla_range");

	snprintf(status, sizeof(status), "Radius %d [10]", ent->client->pers.skills.tesla_radius);
	add_entry(status, PMENU_ALIGN_LEFT, TeslaUpgradeMenuHandler, "tesla_radius");

	const char *chain_status = ent->client->pers.skills.tesla_chain ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Chain Lightning: %s", chain_status);
	add_entry(status, PMENU_ALIGN_LEFT, TeslaUpgradeMenuHandler, "tesla_chain");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, TeslaUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, tesla_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void TeslaUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	int cursor = p->cur; // Save cursor position for reopening

	if (strcmp(arg, "tesla_damage") == 0)
	{
		if (ent->client->pers.skills.tesla_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.tesla_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Tesla Damage increased to level {}!\n", ent->client->pers.skills.tesla_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Tesla Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenTeslaUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "tesla_range") == 0)
	{
		if (ent->client->pers.skills.tesla_range < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.tesla_range++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Tesla Range increased to level {}!\n", ent->client->pers.skills.tesla_range);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Tesla Range is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenTeslaUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "tesla_radius") == 0)
	{
		if (ent->client->pers.skills.tesla_radius < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.tesla_radius++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Tesla Radius increased to level {}!\n", ent->client->pers.skills.tesla_radius);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Tesla Radius is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenTeslaUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "tesla_chain") == 0)
	{
		ent->client->pers.skills.tesla_chain = !ent->client->pers.skills.tesla_chain;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Tesla Chain Lightning: {}\n", ent->client->pers.skills.tesla_chain ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenTeslaUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// TRAP UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t trap_upgrade_menu[32];

void TrapUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenTrapUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(trap_upgrade_menu, 0, sizeof(trap_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(trap_upgrade_menu[count].text, text, sizeof(trap_upgrade_menu[count].text));
			trap_upgrade_menu[count].align = align;
			trap_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(trap_upgrade_menu[count].text_arg1, arg, sizeof(trap_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.trap_damage +
	                       ent->client->pers.skills.trap_range +
	                       ent->client->pers.skills.trap_radius;
	int max_upgrades = 30; // 3 stats * 10 max each
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== TRAP (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.trap_damage);
	add_entry(status, PMENU_ALIGN_LEFT, TrapUpgradeMenuHandler, "trap_damage");

	snprintf(status, sizeof(status), "Range %d [10]", ent->client->pers.skills.trap_range);
	add_entry(status, PMENU_ALIGN_LEFT, TrapUpgradeMenuHandler, "trap_range");

	snprintf(status, sizeof(status), "Radius %d [10]", ent->client->pers.skills.trap_radius);
	add_entry(status, PMENU_ALIGN_LEFT, TrapUpgradeMenuHandler, "trap_radius");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, TrapUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, trap_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void TrapUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	int cursor = p->cur; // Save cursor position for reopening

	if (strcmp(arg, "trap_damage") == 0)
	{
		if (ent->client->pers.skills.trap_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.trap_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Trap Damage increased to level {}!\n", ent->client->pers.skills.trap_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Trap Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenTrapUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "trap_range") == 0)
	{
		if (ent->client->pers.skills.trap_range < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.trap_range++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Trap Range increased to level {}!\n", ent->client->pers.skills.trap_range);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Trap Range is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenTrapUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "trap_radius") == 0)
	{
		if (ent->client->pers.skills.trap_radius < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.trap_radius++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Trap Radius increased to level {}!\n", ent->client->pers.skills.trap_radius);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Trap Radius is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenTrapUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// PHALANX UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t phalanx_upgrade_menu[32];

void PhalanxUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenPhalanxUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(phalanx_upgrade_menu, 0, sizeof(phalanx_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(phalanx_upgrade_menu[count].text, text, sizeof(phalanx_upgrade_menu[count].text));
			phalanx_upgrade_menu[count].align = align;
			phalanx_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(phalanx_upgrade_menu[count].text_arg1, arg, sizeof(phalanx_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.phalanx_damage +
	                       ent->client->pers.skills.phalanx_speed +
	                       ent->client->pers.skills.phalanx_radius;
	int max_upgrades = 30; // 3 stats * 10 max each
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== PHALANX (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.phalanx_damage);
	add_entry(status, PMENU_ALIGN_LEFT, PhalanxUpgradeMenuHandler, "phalanx_damage");

	snprintf(status, sizeof(status), "Speed %d [10]", ent->client->pers.skills.phalanx_speed);
	add_entry(status, PMENU_ALIGN_LEFT, PhalanxUpgradeMenuHandler, "phalanx_speed");

	snprintf(status, sizeof(status), "Radius %d [10]", ent->client->pers.skills.phalanx_radius);
	add_entry(status, PMENU_ALIGN_LEFT, PhalanxUpgradeMenuHandler, "phalanx_radius");

	const char *silent_status = ent->client->pers.skills.phalanx_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, PhalanxUpgradeMenuHandler, "phalanx_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, PhalanxUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, phalanx_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void PhalanxUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	int cursor = p->cur; // Save cursor position for reopening

	if (strcmp(arg, "phalanx_damage") == 0)
	{
		if (ent->client->pers.skills.phalanx_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.phalanx_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Phalanx Damage increased to level {}!\n", ent->client->pers.skills.phalanx_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Phalanx Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenPhalanxUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "phalanx_speed") == 0)
	{
		if (ent->client->pers.skills.phalanx_speed < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.phalanx_speed++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Phalanx Speed increased to level {}!\n", ent->client->pers.skills.phalanx_speed);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Phalanx Speed is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenPhalanxUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "phalanx_radius") == 0)
	{
		if (ent->client->pers.skills.phalanx_radius < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.phalanx_radius++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Phalanx Radius increased to level {}!\n", ent->client->pers.skills.phalanx_radius);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Phalanx Radius is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenPhalanxUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "phalanx_silent") == 0)
	{
		ent->client->pers.skills.phalanx_silent = !ent->client->pers.skills.phalanx_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Phalanx Silent Mode: {}\n", ent->client->pers.skills.phalanx_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenPhalanxUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}

/////////////////////////////////////////////
// DISRUPTOR UPGRADE SUBMENU
/////////////////////////////////////////////

static pmenu_t disruptor_upgrade_menu[32];

void DisruptorUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p);

void OpenDisruptorUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	memset(disruptor_upgrade_menu, 0, sizeof(disruptor_upgrade_menu));
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg = nullptr)
	{
		if (count < 32)
		{
			Q_strlcpy(disruptor_upgrade_menu[count].text, text, sizeof(disruptor_upgrade_menu[count].text));
			disruptor_upgrade_menu[count].align = align;
			disruptor_upgrade_menu[count].SelectFunc = func;
			if (arg)
				Q_strlcpy(disruptor_upgrade_menu[count].text_arg1, arg, sizeof(disruptor_upgrade_menu[count].text_arg1));
			count++;
		}
	};

	// Calculate upgrade percentage
	int current_upgrades = ent->client->pers.skills.disruptor_damage +
	                       ent->client->pers.skills.disruptor_speed +
	                       ent->client->pers.skills.disruptor_duration;
	int max_upgrades = 30; // 3 stats * 10 max each
	int percentage = (current_upgrades * 100) / max_upgrades;

	// Title with percentage
	char title[64];
	snprintf(title, sizeof(title), "=== DISRUPTOR (%d%%) ===", percentage);
	add_entry(title, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);

	// Display current upgrade levels
	char status[128];
	snprintf(status, sizeof(status), "Damage %d [10]", ent->client->pers.skills.disruptor_damage);
	add_entry(status, PMENU_ALIGN_LEFT, DisruptorUpgradeMenuHandler, "disruptor_damage");

	snprintf(status, sizeof(status), "Speed %d [10]", ent->client->pers.skills.disruptor_speed);
	add_entry(status, PMENU_ALIGN_LEFT, DisruptorUpgradeMenuHandler, "disruptor_speed");

	snprintf(status, sizeof(status), "Duration %d [10]", ent->client->pers.skills.disruptor_duration);
	add_entry(status, PMENU_ALIGN_LEFT, DisruptorUpgradeMenuHandler, "disruptor_duration");

	const char *silent_status = ent->client->pers.skills.disruptor_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, DisruptorUpgradeMenuHandler, "disruptor_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("< Back to Weapons", PMENU_ALIGN_LEFT, DisruptorUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, disruptor_upgrade_menu, cursor_pos, count, nullptr, nullptr);
}

void DisruptorUpgradeMenuHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p || p->cur < 0)
	{
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	pmenu_t *item = &p->entries[p->cur];
	if (!item->SelectFunc)
		return;

	const char *arg = item->text_arg1;

	int cursor = p->cur; // Save cursor position for reopening

	if (strcmp(arg, "disruptor_damage") == 0)
	{
		if (ent->client->pers.skills.disruptor_damage < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.disruptor_damage++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Disruptor Damage increased to level {}!\n", ent->client->pers.skills.disruptor_damage);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Disruptor Damage is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenDisruptorUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "disruptor_speed") == 0)
	{
		if (ent->client->pers.skills.disruptor_speed < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.disruptor_speed++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Disruptor Speed increased to level {}!\n", ent->client->pers.skills.disruptor_speed);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Disruptor Speed is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenDisruptorUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "disruptor_duration") == 0)
	{
		if (ent->client->pers.skills.disruptor_duration < 10)
		{
			if (ent->client->pers.weapon_points >= 1)
			{
				ent->client->pers.weapon_points--;
				ent->client->pers.skills.disruptor_duration++;
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Disruptor Duration increased to level {}!\n", ent->client->pers.skills.disruptor_duration);
			}
			else
			{
				gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Not enough weapon points!\n");
			}
		}
		else
		{
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Disruptor Duration is already at maximum level!\n");
		}
		PMenu_Close(ent);
		OpenDisruptorUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "disruptor_silent") == 0)
	{
		ent->client->pers.skills.disruptor_silent = !ent->client->pers.skills.disruptor_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Disruptor Silent Mode: {}\n", ent->client->pers.skills.disruptor_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenDisruptorUpgradeMenu(ent, cursor);
	}
	else if (strcmp(arg, "back_to_weapons") == 0)
	{
		PMenu_Close(ent);
		OpenWeaponUpgradeMenu(ent);
	}
}
