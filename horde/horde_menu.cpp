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
#include <boost/container/small_vector.hpp>

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
void OpenProxUpgradeMenu(edict_t *ent, int cursor_pos = -1);   // Forward declare Prox Upgrade submenu
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

// Bonus Management menu functions (Classic Mode)
void OpenBonusManagementMenu(edict_t *ent, int cursor_position = -1);
void BonusManagementMenuHandler(edict_t *ent, pmenuhnd_t *p);
void OpenBonusAbilitiesMenu(edict_t *ent, int cursor_position = -1);
void BonusAbilitiesMenuHandler(edict_t *ent, pmenuhnd_t *p);
void OpenBonusWeaponsMenu(edict_t *ent, int cursor_position = -1);
void BonusWeaponsMenuHandler(edict_t *ent, pmenuhnd_t *p);

// Helper to get sentry type name
static const char *GetSentryTypeName(sentrytype_t type)
{
	switch (type)
	{
	case SENTRY_HEATBEAM:
		return "Beams";
	case SENTRY_MACHINEGUN:
		return "Bullets";
	case SENTRY_FLECHETTE:
		return "Flechettes";
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

static bool HasOtherHumanClients(const edict_t *ent)
{
	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		const edict_t *player = &g_edicts[i + 1];

		if (player == ent || !player->inuse || !player->client || (player->svflags & SVF_BOT))
		{
			continue;
		}

		return true;
	}

	return false;
}

static bool CanShowSinglePlayerCampaignMenu(const edict_t *ent)
{
	if (!ent || !ent->client || (ent->svflags & SVF_BOT))
	{
		return false;
	}

	const bool is_host = P_GetLobbyUserNum(ent) == 0;
	const bool is_coop_mode = G_IsCooperative() || coop->integer;
	const bool is_horde_mode = g_horde && g_horde->integer;

	return is_host && (is_coop_mode || (is_horde_mode && !HasOtherHumanClients(ent)));
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
	if (!ent || !ent->client)
	{
		return;
	}

	if (!deathmatch->integer && !coop->integer && !G_TeamplayEnabled())
	{
		OpenHordeMenu(ent);
		return;
	}

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
	if (g_horde->integer || pvm->integer || G_IsCooperative() || coop->integer)
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
			join_text = "Join Game";

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
	else if (*level.mapname) // Fallback to map filename
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
	// Using small_vector to avoid heap allocation for typical installations (most have < 16 maps per category)
	boost::container::small_vector<std::string, 16> big_maps;
	boost::container::small_vector<std::string, 16> medium_maps;
	boost::container::small_vector<std::string, 16> small_maps;
	size_t current_page = 0;
	horde::MapSize current_category = {false, true, false}; // Default to medium
};

static map_lists_t categorized_maps;

// Respawn weapon selection menu
constexpr size_t RESPAWN_WEAPON_MENU_SIZE = 18; // Title, blank, current weapon, blank, 10 weapons, blank, Next, Previous, Back;
static pmenu_t respawn_weapon_menu[RESPAWN_WEAPON_MENU_SIZE];
static size_t respawn_weapon_current_page = 0;

// Abilities menu pagination
static size_t abilities_current_page = 0;

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

// --- Mode Selection Menu (for voting) ---
static pmenu_t mode_selection_menu[8]; // Menu for choosing Horde or PvM mode before map selection

// Forward declarations for mode selection
void OpenModeSelectionMenu(edict_t *ent);
void ModeSelectionHandler(edict_t *ent, pmenuhnd_t *p);
// Menu indices are now dynamic, determined by the text content in MapCategoryHandler

// Forward declaration for cooperative campaign menu
void OpenCooperativeCampaignMenu(edict_t *ent);
void CooperativeCampaignHandler(edict_t *ent, pmenuhnd_t *p);

// Forward declaration for true single-player campaign menu
void OpenSinglePlayerCampaignMenu(edict_t *ent);
void SinglePlayerCampaignHandler(edict_t *ent, pmenuhnd_t *p);

// Cooperative campaign selection menu
static pmenu_t coop_campaign_menu[10];
static pmenu_t sp_campaign_menu[10];

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
	for (auto& e : coop_campaign_menu) e = {};
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

void OpenSinglePlayerCampaignMenu(edict_t *ent)
{
	if (!ent || !ent->client)
	{
		return;
	}

	if (ent->client->menu)
	{
		PMenu_Close(ent);
	}

	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	for (auto& e : sp_campaign_menu) e = {};
	int idx = 0;

	Q_strlcpy(sp_campaign_menu[idx].text, "Select Single Player Campaign", sizeof(sp_campaign_menu[idx].text));
	sp_campaign_menu[idx].align = PMENU_ALIGN_CENTER;
	sp_campaign_menu[idx].SelectFunc = nullptr;
	idx++;

	sp_campaign_menu[idx].text[0] = '\0';
	sp_campaign_menu[idx].align = PMENU_ALIGN_CENTER;
	sp_campaign_menu[idx].SelectFunc = nullptr;
	idx++;

	Q_strlcpy(sp_campaign_menu[idx].text, "Quake 2", sizeof(sp_campaign_menu[idx].text));
	sp_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	sp_campaign_menu[idx].SelectFunc = SinglePlayerCampaignHandler;
	idx++;

	Q_strlcpy(sp_campaign_menu[idx].text, "Call of the Machine", sizeof(sp_campaign_menu[idx].text));
	sp_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	sp_campaign_menu[idx].SelectFunc = SinglePlayerCampaignHandler;
	idx++;

	Q_strlcpy(sp_campaign_menu[idx].text, "The Reckoning", sizeof(sp_campaign_menu[idx].text));
	sp_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	sp_campaign_menu[idx].SelectFunc = SinglePlayerCampaignHandler;
	idx++;

	Q_strlcpy(sp_campaign_menu[idx].text, "Ground Zero", sizeof(sp_campaign_menu[idx].text));
	sp_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	sp_campaign_menu[idx].SelectFunc = SinglePlayerCampaignHandler;
	idx++;

	Q_strlcpy(sp_campaign_menu[idx].text, "Quake 2 N64", sizeof(sp_campaign_menu[idx].text));
	sp_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	sp_campaign_menu[idx].SelectFunc = SinglePlayerCampaignHandler;
	idx++;

	sp_campaign_menu[idx].text[0] = '\0';
	sp_campaign_menu[idx].align = PMENU_ALIGN_CENTER;
	sp_campaign_menu[idx].SelectFunc = nullptr;
	idx++;

	Q_strlcpy(sp_campaign_menu[idx].text, "Back", sizeof(sp_campaign_menu[idx].text));
	sp_campaign_menu[idx].align = PMENU_ALIGN_LEFT;
	sp_campaign_menu[idx].SelectFunc = SinglePlayerCampaignHandler;
	idx++;

	PMenu_Open(ent, sp_campaign_menu, -1, idx, nullptr, nullptr);
}

static void StartSinglePlayerCampaign(edict_t *ent, const char *campaign_name, const char *start_map)
{
	if (!ent || !ent->client || !start_map || !*start_map)
	{
		return;
	}

	char sp_map[64];
	snprintf(sp_map, sizeof(sp_map), "*sp:%s", start_map);

	gi.LocBroadcast_Print(PRINT_HIGH, "Starting Single Player: {}...\n", campaign_name);
	BeginIntermission(CreateTargetChangeLevel(sp_map));
}

void SinglePlayerCampaignHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p)
	{
		return;
	}

	const char *selected_text = p->entries[p->cur].text;

	PMenu_Close(ent);

	if (strcmp(selected_text, "Quake 2") == 0)
	{
		StartSinglePlayerCampaign(ent, "Quake 2", "base1");
	}
	else if (strcmp(selected_text, "Call of the Machine") == 0)
	{
		StartSinglePlayerCampaign(ent, "Call of the Machine", "mguhub");
	}
	else if (strcmp(selected_text, "The Reckoning") == 0)
	{
		StartSinglePlayerCampaign(ent, "The Reckoning", "xswamp");
	}
	else if (strcmp(selected_text, "Ground Zero") == 0)
	{
		StartSinglePlayerCampaign(ent, "Ground Zero", "rmine1");
	}
	else if (strcmp(selected_text, "Quake 2 N64") == 0)
	{
		StartSinglePlayerCampaign(ent, "Quake 2 N64", "q64/outpost");
	}
	else if (strcmp(selected_text, "Back") == 0)
	{
		OpenHordeMenu(ent);
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
	else if (strcmp(selected_text, "Vote PvM Mode") == 0)
	{
		// Start vote to switch to PvM mode
		CTFBeginElection(ent, ELECT_PVM, "Switch to PvM Mode? (Disables g_instagib)");
	}
	else if (strcmp(selected_text, "Vote Horde Mode") == 0)
	{
		// Start vote to switch to Horde mode
		CTFBeginElection(ent, ELECT_HORDE, "Switch to Horde Mode? (Enables g_instagib)");
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
	for (auto& e : map_category_menu) e = {};
	int idx = 0;

	// Check if we're in mode voting flow (user has selected a mode)
	bool in_mode_vote_flow = (ent->client->pending_mode_vote != 0);

	// Title
	if (in_mode_vote_flow)
	{
		Q_strlcpy(map_category_menu[idx].text, "Select Map Category", sizeof(map_category_menu[idx].text));
	}
	else
	{
		Q_strlcpy(map_category_menu[idx].text, "Map Category Selection", sizeof(map_category_menu[idx].text));
	}
	map_category_menu[idx].align = PMENU_ALIGN_CENTER;
	map_category_menu[idx].SelectFunc = nullptr;
	idx++;

	// Blank line
	map_category_menu[idx].text[0] = '\0';
	map_category_menu[idx].align = PMENU_ALIGN_CENTER;
	map_category_menu[idx].SelectFunc = nullptr;
	idx++;

	// Current map
	if (*level.mapname)
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

	// Map categories - always show these
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

	// If NOT in mode vote flow, show mode switching votes and extend time
	if (!in_mode_vote_flow)
	{
		// Show mode switching votes based on current mode
		if (g_horde->integer && !pvm->integer)
		{
			// In Horde mode - show PvM vote option
			Q_strlcpy(map_category_menu[idx].text, "Vote PvM Mode", sizeof(map_category_menu[idx].text));
			map_category_menu[idx].align = PMENU_ALIGN_LEFT;
			map_category_menu[idx].SelectFunc = MapCategoryHandler;
			idx++;
		}
		else if (pvm->integer)
		{
			// In PvM mode - show Horde vote option
			Q_strlcpy(map_category_menu[idx].text, "Vote Horde Mode", sizeof(map_category_menu[idx].text));
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

		// Add Extend Time option
		Q_strlcpy(map_category_menu[idx].text, "Extend Time", sizeof(map_category_menu[idx].text));
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

// --- Mode Selection Menu (for voting on mode + map) ---

void ModeSelectionHandler(edict_t *ent, pmenuhnd_t *p)
{
	if (!ent || !ent->client || !p)
	{
		return;
	}

	const char *selected_text = p->entries[p->cur].text;

	PMenu_Close(ent); // Close the mode selection menu

	if (strcmp(selected_text, "Vote Horde Mode") == 0)
	{
		// Store that we want to vote for Horde mode
		ent->client->pending_mode_vote = 1;
		// Categorize maps before opening map menu
		CategorizeMapList();
		// Now open the map category menu to select which map
		OpenMapCategoryMenu(ent);
	}
	else if (strcmp(selected_text, "Vote PvM Mode") == 0)
	{
		// Store that we want to vote for PvM mode
		ent->client->pending_mode_vote = 2;
		// Categorize maps before opening map menu
		CategorizeMapList();
		// Now open the map category menu to select which map
		OpenMapCategoryMenu(ent);
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
}

void OpenModeSelectionMenu(edict_t *ent)
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

	// Clear any pending mode vote
	ent->client->pending_mode_vote = 0;

	// Build the menu
	for (auto& e : mode_selection_menu) e = {};
	int idx = 0;

	// Title
	Q_strlcpy(mode_selection_menu[idx].text, "Vote Mode & Map", sizeof(mode_selection_menu[idx].text));
	mode_selection_menu[idx].align = PMENU_ALIGN_CENTER;
	mode_selection_menu[idx].SelectFunc = nullptr;
	idx++;

	// Blank line
	mode_selection_menu[idx].text[0] = '\0';
	mode_selection_menu[idx].align = PMENU_ALIGN_CENTER;
	mode_selection_menu[idx].SelectFunc = nullptr;
	idx++;

	// Show both mode options
	Q_strlcpy(mode_selection_menu[idx].text, "Vote Horde Mode", sizeof(mode_selection_menu[idx].text));
	mode_selection_menu[idx].align = PMENU_ALIGN_LEFT;
	mode_selection_menu[idx].SelectFunc = ModeSelectionHandler;
	idx++;

	Q_strlcpy(mode_selection_menu[idx].text, "Vote PvM Mode", sizeof(mode_selection_menu[idx].text));
	mode_selection_menu[idx].align = PMENU_ALIGN_LEFT;
	mode_selection_menu[idx].SelectFunc = ModeSelectionHandler;
	idx++;

	// Add Extend Time option
	Q_strlcpy(mode_selection_menu[idx].text, "Extend Time", sizeof(mode_selection_menu[idx].text));
	mode_selection_menu[idx].align = PMENU_ALIGN_LEFT;
	mode_selection_menu[idx].SelectFunc = ModeSelectionHandler;
	idx++;

	// Blank line
	mode_selection_menu[idx].text[0] = '\0';
	mode_selection_menu[idx].align = PMENU_ALIGN_CENTER;
	mode_selection_menu[idx].SelectFunc = nullptr;
	idx++;

	// Back and Close
	Q_strlcpy(mode_selection_menu[idx].text, "Back to Horde Menu", sizeof(mode_selection_menu[idx].text));
	mode_selection_menu[idx].align = PMENU_ALIGN_LEFT;
	mode_selection_menu[idx].SelectFunc = ModeSelectionHandler;
	idx++;

	Q_strlcpy(mode_selection_menu[idx].text, "Close", sizeof(mode_selection_menu[idx].text));
	mode_selection_menu[idx].align = PMENU_ALIGN_LEFT;
	mode_selection_menu[idx].SelectFunc = ModeSelectionHandler;
	idx++;

	// Open the menu
	PMenu_Open(ent, mode_selection_menu, -1, idx, nullptr, nullptr);
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
	boost::container::small_vector<std::string, 16> *current_map_list = nullptr;

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

		// Check if it's the current map (but only if not voting for a mode change)
		if (ent->client->pending_mode_vote == 0 && Q_strcasecmp(map_name.c_str(), level.mapname) == 0)
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "Can't vote for the current map.\n");
			return; // Stay in the menu
		}

		// Check if we're voting for a mode+map combination
		if (ent->client->pending_mode_vote != 0)
		{
			// Store mode+map in elevel as "mode:map" format
			char combined[64];
			const char *mode_name = (ent->client->pending_mode_vote == 1) ? "horde" : "pvm";
			snprintf(combined, sizeof(combined), "%s:%s", mode_name, map_name.c_str());
			Q_strlcpy(ctfgame.elevel, combined, sizeof(ctfgame.elevel));

			// Create vote message with mode info
			char vote_msg[128];
			const char *mode_display = (ent->client->pending_mode_vote == 1) ? "Horde" : "PvM";
			snprintf(vote_msg, sizeof(vote_msg), "Change to %s Mode on %s?", mode_display, map_name.c_str());

			// Clear pending mode vote before starting election
			ent->client->pending_mode_vote = 0;

			// Use ELECT_MAP for mode+map voting (we'll handle it in CTFWinElection)
			if (CTFBeginElection(ent, ELECT_MAP, vote_msg))
			{
				PMenu_Close(ent);
			}
		}
		else
		{
			// Regular map vote (no mode change)
			Q_strlcpy(ctfgame.elevel, map_name.c_str(), sizeof(ctfgame.elevel));

			char vote_msg[128];
			snprintf(vote_msg, sizeof(vote_msg), "Change map to %s?", map_name.c_str());

			// Close menu *before* starting election if successful
			if (CTFBeginElection(ent, ELECT_MAP, vote_msg))
			{
				PMenu_Close(ent);
			}
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
	boost::container::small_vector<std::string, 16> *current_map_list = nullptr;

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

	BFGMode current_mode = ent->client->pers.bfg_mode;
	BFGMode new_mode = BFGMode::NORMAL;

	// Classic Mode (vortex=0): Allow cycling through available modes
	if (g_vortex->integer == 0)
	{
		// Check if player has BFG Pull benefit
		bool has_pull = ClassicPlayerHasBenefit(ent, BenefitID::BFG_GRAV_PULL);

		// Cycle through modes: Normal -> Slide -> Pull (if unlocked) -> Normal
		if (current_mode == BFGMode::NORMAL)
		{
			new_mode = BFGMode::SLIDE;
		}
		else if (current_mode == BFGMode::SLIDE)
		{
			// Only allow Pull mode if player has the benefit
			if (has_pull)
			{
				new_mode = BFGMode::GRAV_PULL;
			}
			else
			{
				new_mode = BFGMode::NORMAL;
			}
		}
		else // GRAV_PULL
		{
			new_mode = BFGMode::NORMAL;
		}
	}
	else
	{
		// RPG Mode (vortex=1): Check for benefits/skills
		bool has_slide = ClassicPlayerHasBenefit(ent, BenefitID::BFG_SLIDE);
		bool has_pull = ClassicPlayerHasBenefit(ent, BenefitID::BFG_GRAV_PULL);

		// If no upgrades, can't change mode
		if (!has_slide && !has_pull)
		{
			gi.LocCenter_Print(ent, "\n\n\nNo BFG upgrades available!\n");
			return;
		}

		// Cycle through available modes based on what player has
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
	if (strncmp(selected_text, "  Remove Stroggs", strlen("  Remove Stroggs")) == 0)
	{
		// Remove Strogg summoner bases (which will also kill their spawned monsters)
		Cmd_RemoveStrogg_f(ent);
		// Message handled internally, menu should close.
	}
	else if (strncmp(selected_text, "  Remove Lasers", strlen("  Remove Lasers")) == 0)
	{
		Cmd_RemoveLaser_f(ent);
		// Message handled internally, menu should close.
	}
	else if (strncmp(selected_text, "  Remove Sentry Gun", strlen("  Remove Sentry Gun")) == 0)
	{
		Cmd_RemoveSentry_f(ent);
		// Message handled internally, menu should close.
	}
	else if (strncmp(selected_text, "  Remove Barrels", strlen("  Remove Barrels")) == 0)
	{
		Cmd_RemoveBarrel_f(ent);
		// Message handled internally, menu should close.
	}
	// **** Check Special Wave selection ****
	else if (strstr(selected_text, "Special key [L]") != nullptr)
	{
		HordeMenu_SpecialWave(ent, p); // Call the dedicated handler
		shouldCloseMenu = false;	   // Don't close, HordeMenu_SpecialWave will reopen Misc Menu
	}
	// **** Check Sentry Type selection ****
	else if (strstr(selected_text, "Sentry Type") != nullptr)
	{
		HordeMenu_SentryChoice(ent, p); // Call the dedicated handler
		shouldCloseMenu = false;		// Don't close, HordeMenu_SentryChoice will reopen Misc Menu
	}
	// **** Check BFG Behavior selection ****
	else if (strstr(selected_text, "BFG Behavior") != nullptr)
	{
		HordeMenu_BFGMode(ent, p); // Call the dedicated handler
		shouldCloseMenu = false;   // Don't close, HordeMenu_BFGMode will reopen Misc Menu
	}
	// **** Check Beta: Strogg preference selection ****
	else if (strstr(selected_text, "[Beta] Strogg") != nullptr)
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

	// Handle "Back" or "Back to Main Menu"
	if (strcmp(selected_text, "Back") == 0 || strcmp(selected_text, "Back to Main Menu") == 0)
	{
		respawn_weapon_current_page = 0; // Reset to first page
		PMenu_Close(ent);
		OpenHordeMenu(ent);
		return;
	}

	// Handle "Next"
	if (strcmp(selected_text, "Next") == 0)
	{
		respawn_weapon_current_page++;
		PMenu_Close(ent);
		OpenRespawnWeaponMenu(ent);
		return;
	}

	// Handle "< Previous"
	if (strcmp(selected_text, "Previous") == 0)
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
	for (auto& e : respawn_weapon_menu) e = {};
	int count = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr)
	{
		if (count < static_cast<int>(RESPAWN_WEAPON_MENU_SIZE))
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
		add_entry("Next", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);
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

	// Back to main menu (always show, with "Back" label)
	add_entry("Back", PMENU_ALIGN_LEFT, RespawnWeaponMenuHandler);

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

	static pmenu_t entries[16]; // Increased from 15 to 16 for BFG mode option
	for (auto& e : entries) e = {};
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

	char buffer[128];

	// --- Special Wave Selection ---
	MenuFormatItemWithCustomNoNumber(buffer, sizeof(buffer), "Special key [L]", GetSpecialWaveName(g_special_key->integer));
	add_entry(buffer, PMENU_ALIGN_LEFT, MiscMenuHandler);

	// --- Sentry Gun Choice ---
	MenuFormatItemWithCustomNoNumber(buffer, sizeof(buffer), "Sentry Type", GetSentryTypeName(ent->client->pers.sentry_gun_choice));
	add_entry(buffer, PMENU_ALIGN_LEFT, MiscMenuHandler);

	// --- BFG Mode Selection (Classic Mode only) ---
	if (g_vortex->integer == 0) {
		MenuFormatItemWithCustomNoNumber(buffer, sizeof(buffer), "BFG Behavior", GetBFGModeName(ent->client->pers.bfg_mode));
		add_entry(buffer, PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	// --- Beta: Strogg Preference Selection ---
	MenuFormatItemWithCustomNoNumber(buffer, sizeof(buffer), "[Beta] Strogg", GetMorphTypeName(ent->client->pers.morph_preference));
	add_entry(buffer, PMENU_ALIGN_LEFT, MiscMenuHandler);

	// --- Stroggification Command (morph/unmorph) ---
	if (IsMorphed(ent))
	{
		add_entry("  I hate stroggs!", PMENU_ALIGN_LEFT, MiscMenuHandler);
	}
	else
	{
		add_entry("  Stroggificate me!", PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	add_entry("", PMENU_ALIGN_CENTER); // Separator

	// --- Conditional Remove Options ---

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
		add_entry(G_Fmt("  Remove Stroggs ({})", strogg_count).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	// Get the laser count directly from the client's respawn data.
	int laser_count = ent->client->resp.num_lasers;
	if (laser_count > 0)
	{
		// Use G_Fmt to format the menu entry with the current count.
		add_entry(G_Fmt("  Remove Lasers ({})", laser_count).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	// --- Sentry Gun Removal ---
	int sentry_count = ent->client->resp.num_sentries;
	if (sentry_count > 0)
	{
		add_entry(G_Fmt("  Remove Sentry Gun ({})", sentry_count).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	// --- Barrel Removal ---
	int barrel_count = ent->client->resp.num_barrels;
	if (barrel_count > 0)
	{
		add_entry(G_Fmt("  Remove Barrels ({}/{})", barrel_count, BarrelConstants::MAX_BARRELS_PER_PLAYER()).data(), PMENU_ALIGN_LEFT, MiscMenuHandler);
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
	for (auto& e : entries) e = {};

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

	// ID Toggle using MenuFormatItemWithCustom
	int item_num = 1;
	char status_buffer[16];
	if (count < HUD_MENU_MAX_ENTRIES)
	{
		snprintf(status_buffer, sizeof(status_buffer), "[%s]", ent->client->pers.id_state ? "ON" : "OFF");
		MenuFormatItemWithCustom(entries[count].text, sizeof(entries[count].text), item_num++, "Enable/Disable ID", status_buffer);
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// ID-DMG Toggle using MenuFormatItemWithCustom
	if (count < HUD_MENU_MAX_ENTRIES)
	{
		snprintf(status_buffer, sizeof(status_buffer), "[%s]", ent->client->pers.iddmg_state ? "ON" : "OFF");
		MenuFormatItemWithCustom(entries[count].text, sizeof(entries[count].text), item_num++, "Enable/Disable ID-DMG", status_buffer);
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
	for (auto& e : admin_menu) e = {};
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
		gi.LocClient_Print(ent, PRINT_HIGH, "Gave 5 bonus ability points to all players.\n");
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
		gi.LocClient_Print(ent, PRINT_HIGH, "Gave 5 bonus weapon points to all players.\n");
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
		gi.LocClient_Print(ent, PRINT_HIGH, "Gave 10 bonus points of each type to all players.\n");
	}
	else if (strcmp(text, "Give All Weapons (All)") == 0)
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
	else if (strcmp(text, "Heal All Players (All)") == 0)
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
	if (strcmp(selected_text, "Single Player") == 0)
	{
		if (!CanShowSinglePlayerCampaignMenu(ent))
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "Single Player is only available to the host in coop, or in solo Horde.\n");
			return;
		}

		OpenSinglePlayerCampaignMenu(ent);
		return;
	}
	// Check for "Go Spectator/AFK"
	if (strcmp(selected_text, "Go Spectator/AFK") == 0)
	{
		CTFObserver(ent);
		shouldCloseMenu = false; // CTFObserver might handle closing
	}
	// Check for "Upgrade Menu" (with or without highlighting)
	else if (strncmp(selected_text, "Upgrade Menu", 12) == 0 || strncmp(selected_text, "*Upgrade Menu", 13) == 0)
	{
		shouldCloseMenu = true; // Close main menu first
		PMenu_Close(ent);		// Close now before opening upgrade menu
		OpenUpgradeMenu(ent);	// This will set protection and open new menu
		return;					// Exit early since we already closed
	}
	// Check for "Bonus Management" (with or without highlighting)
	else if (strncmp(selected_text, "Bonus Management", 16) == 0 || strncmp(selected_text, "*Bonus Management", 17) == 0)
	{
		shouldCloseMenu = true; // Close main menu first
		PMenu_Close(ent);		// Close now before opening bonus menu
		OpenBonusManagementMenu(ent); // This will set protection and open new menu
		return;					// Exit early since we already closed
	}
	// Check for "Misc Options"
	else if (strcmp(selected_text, "Misc Options") == 0)
	{
		OpenMiscMenu(ent);
		shouldCloseMenu = false;
	}
	// Vote Mode/Map
	else if (ctfgame.election == ELECT_NONE && strcmp(selected_text, "Vote Mode/Map") == 0)
	{
		OpenModeSelectionMenu(ent);
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
	else if (strcmp(selected_text, "*Set Respawn Weapon") == 0)
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
	for (auto& e : entries) e = {};
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

	const bool is_true_single_player =
		!deathmatch->integer && !coop->integer &&
		(!g_horde || !g_horde->integer) &&
		(!pvm || !pvm->integer);

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

		if (CanShowSinglePlayerCampaignMenu(ent))
		{
			add_entry("Single Player", PMENU_ALIGN_LEFT, HordeMenuHandler);
		}
	}

	if (!is_true_single_player)
	{
		add_entry("Go Spectator/AFK", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}

	if (ctfgame.election == ELECT_NONE)
	{
		add_entry("Vote Mode/Map", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}
	else
	{
		// Use G_Fmt for the vote question
		add_entry(G_Fmt("Vote: {}", ctfgame.emsg).data(), PMENU_ALIGN_CENTER, nullptr);
		add_entry("Vote Yes", PMENU_ALIGN_LEFT, HordeMenuHandler);
		add_entry("Vote No", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}
	add_entry("", PMENU_ALIGN_CENTER);

	// Show Upgrade Menu in RPG Mode, Bonus Management in Classic Mode
	if (g_vortex->integer != 0)
	{
		// Highlight Upgrade Menu if player has available skill points
		int total_points = ent->client->pers.ability_points + ent->client->pers.weapon_points;
		if (total_points >= 1)
		{
			add_entry("*Upgrade Menu", PMENU_ALIGN_LEFT, HordeMenuHandler);
		}
		else
		{
			add_entry("Upgrade Menu", PMENU_ALIGN_LEFT, HordeMenuHandler);
		}
	}
	else
	{
		// Classic Mode: Show Bonus Management
		int total_points = ent->client->pers.ability_points + ent->client->pers.weapon_points;
		if (total_points >= 1)
		{
			add_entry("*Bonus Management", PMENU_ALIGN_LEFT, HordeMenuHandler);
		}
		else
		{
			add_entry("Bonus Management", PMENU_ALIGN_LEFT, HordeMenuHandler);
		}
	}

	add_entry("Misc Options", PMENU_ALIGN_LEFT, HordeMenuHandler);
	add_entry("HUD Options", PMENU_ALIGN_LEFT, HordeMenuHandler);

	// Show Set Respawn Weapon in PvM mode (horde+pvm or pvm alone, but not horde alone)
	if (pvm->integer)
	{
		add_entry("*Set Respawn Weapon", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}

	// Only show Character Info in RPG Mode (vortex enabled)
	if (pvm->integer && g_vortex->integer != 0)
	{
		add_entry("Character Info", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}

	if (g_horde && g_horde->integer && !pvm->integer)
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
			for (auto& e : detail_menu) e = {};
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
				Q_strlcpy(detail_menu[menu_index].text, "Upgrade", sizeof(detail_menu[menu_index].text));
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
			Q_strlcpy(detail_menu[menu_index].text, "Back", sizeof(detail_menu[menu_index].text));
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
	for (auto& e : detail_menu) e = {};
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
		Q_strlcpy(detail_menu[menu_index].text, "Upgrade", sizeof(detail_menu[menu_index].text));
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
	Q_strlcpy(detail_menu[menu_index].text, "Back", sizeof(detail_menu[menu_index].text));
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

	// Handle pagination
	if (strcmp(item->text_arg1, "next_page") == 0)
	{
		abilities_current_page++;
		PMenu_Close(ent);
		OpenAbilitiesMenu(ent);
		return;
	}

	if (strcmp(item->text_arg1, "prev_page") == 0)
	{
		if (abilities_current_page > 0)
			abilities_current_page--;
		PMenu_Close(ent);
		OpenAbilitiesMenu(ent);
		return;
	}

	// Handle back navigation
	if (strcmp(item->text_arg1, "back_to_upgrade") == 0)
	{
		abilities_current_page = 0; // Reset to first page when returning
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

// Create Abilities Menu - New skill-based system with pagination
pmenuhnd_t *CreateAbilitiesMenu(edict_t *ent)
{
	if (!ent || !ent->client)
		return nullptr;

	static pmenu_t abilities_menu[32];
	for (auto& e : abilities_menu) e = {};
	int menu_index = 0;

	auto add_entry = [&](const char *text, int align, SelectFunc_t func = nullptr, const char *arg1 = nullptr)
	{
		if (menu_index < static_cast<int>(std::size(abilities_menu)))
		{
			Q_strlcpy(abilities_menu[menu_index].text, text, sizeof(abilities_menu[menu_index].text));
			abilities_menu[menu_index].align = align;
			abilities_menu[menu_index].SelectFunc = func;
			if (arg1)
				Q_strlcpy(abilities_menu[menu_index].text_arg1, arg1, sizeof(abilities_menu[menu_index].text_arg1));
			menu_index++;
		}
	};

	// Get all ability upgrades
	const UpgradeDefinition* defs = GetUpgradeDefinitions();
	size_t def_count = GetUpgradeDefinitionCount();

	// Count total abilities
	size_t total_abilities = 0;
	for (size_t i = 0; i < def_count; ++i)
	{
		if (defs[i].category == UpgradeCategory::ABILITY)
			total_abilities++;
	}

	// Pagination settings (8 items per page)
	constexpr size_t abilities_per_page = 8;
	size_t total_pages = (total_abilities + abilities_per_page - 1) / abilities_per_page;

	// Wrap page number
	if (abilities_current_page >= total_pages && total_pages > 0)
		abilities_current_page = 0;

	// Calculate ability range for current page
	size_t start_index = abilities_current_page * abilities_per_page;
	size_t end_index = std::min(start_index + abilities_per_page, total_abilities);

	// Header
	add_entry("Upgrade Ability Menu", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_LEFT);
	// Display abilities for current page
	size_t ability_counter = 0;
	int item_number = static_cast<int>(start_index) + 1;  // Continue numbering across pages
	bool has_abilities = false;

	for (size_t i = 0; i < def_count; ++i)
	{
		if (defs[i].category != UpgradeCategory::ABILITY)
			continue;

		// Check if this ability is in the current page range
		if (ability_counter >= start_index && ability_counter < end_index)
		{
			int8_t current_level = GetSkillLevel(ent, defs[i].id);
			int8_t max_level = defs[i].max_level;

			char item_text[64];

		// Always use progress indicator [X/Y] for all abilities
        MenuFormatItemWithProgress(item_text, sizeof(item_text), item_number, defs[i].name, current_level, max_level);

			add_entry(item_text, PMENU_ALIGN_LEFT, AbilitiesMenuHandler, defs[i].id);
			item_number++;
			has_abilities = true;
		}

		ability_counter++;
	}

	if (!has_abilities)
	{
		add_entry("No abilities available", PMENU_ALIGN_CENTER);
	}

	// Points display with green highlighting
	char points_text[64];
	G_FmtTo(points_text, "*You have: {} points", ent->client->pers.skill_points);
	add_entry("", PMENU_ALIGN_LEFT);
	add_entry("", PMENU_ALIGN_LEFT);
	add_entry("", PMENU_ALIGN_LEFT);
	add_entry("", PMENU_ALIGN_LEFT);

	// Navigation buttons (before reset)
	// Previous page button (only if not on first page)
	if (abilities_current_page > 0)
	{
		add_entry("Previous Page", PMENU_ALIGN_LEFT, AbilitiesMenuHandler, "prev_page");
	}

	// Next page button (only if not on last page)
	if (abilities_current_page < total_pages - 1)
	{
		add_entry("Next Page", PMENU_ALIGN_LEFT, AbilitiesMenuHandler, "next_page");
	}


	// Back to upgrade menu (always visible)
	add_entry("Back to Main Menu", PMENU_ALIGN_LEFT, AbilitiesMenuHandler, "back_to_upgrade");
	// Reset all skills option (moved after navigation)
	add_entry("Reset All Skills (Free)", PMENU_ALIGN_LEFT, AbilitiesMenuHandler, "reset_skills");

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
	for (auto& e : weapons_menu) e = {};
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
		bool owned = ClassicPlayerHasBenefit(ent, benefit_id);

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
		bool prereq_met = (prereq == BenefitID::NONE) || ClassicPlayerHasBenefit(ent, prereq);

		// Don't show if prerequisite not met - cleaner menu
		if (!prereq_met)
		{
			continue;
		}

		bool can_afford = ent->client->pers.weapon_points >= cost;

		// Available to purchase
		MenuFormatItemWithCost(weapons_menu[menu_index].text,
		                       sizeof(weapons_menu[menu_index].text),
		                       can_afford, g_benefitsData.names[i], cost);
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
	Q_strlcpy(weapons_menu[menu_index].text, "Back", sizeof(weapons_menu[menu_index].text));
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
	// Menu is now available in both Classic Mode (vortex=0) and RPG Mode (vortex=1)
	// In Classic Mode, abilities show the benefit system
	// In RPG Mode, abilities show the skill system

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
	for (auto& e : upgrade_menu) e = {};
	int menu_index = 0;

	// Check vortex mode to determine menu type
	bool is_classic_mode = (g_vortex->integer == 0);

	// Title - different for Classic vs RPG mode
	if (is_classic_mode)
	{
		Q_strlcpy(upgrade_menu[menu_index].text, "*=== BONUS MANAGEMENT ===", sizeof(upgrade_menu[menu_index].text));
	}
	else
	{
		Q_strlcpy(upgrade_menu[menu_index].text, "*=== UPGRADES ===", sizeof(upgrade_menu[menu_index].text));
	}
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Points display - different for Classic vs RPG mode
	if (is_classic_mode)
	{
		// Classic Mode: Show ability and weapon points from benefit system
		G_FmtTo(upgrade_menu[menu_index].text, "Ability Points: {}", ent->client->pers.ability_points);
		upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
		upgrade_menu[menu_index].SelectFunc = nullptr;
		menu_index++;

		G_FmtTo(upgrade_menu[menu_index].text, "Weapon Points: {}", ent->client->pers.weapon_points);
		upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
		upgrade_menu[menu_index].SelectFunc = nullptr;
		menu_index++;
	}
	else
	{
		// RPG Mode: Show skill points
		G_FmtTo(upgrade_menu[menu_index].text, "Skill Points: {}", ent->client->pers.skill_points);
		upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
		upgrade_menu[menu_index].SelectFunc = nullptr;
		menu_index++;

		G_FmtTo(upgrade_menu[menu_index].text, "Weapon Points: {}", ent->client->pers.weapon_points);
		upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
		upgrade_menu[menu_index].SelectFunc = nullptr;
		menu_index++;
	}

	// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Menu options
	Q_strlcpy(upgrade_menu[menu_index].text, "Abilities", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
	upgrade_menu[menu_index].SelectFunc = UpgradeMenuHandler;
	Q_strlcpy(upgrade_menu[menu_index].text_arg1, "abilities_shop", sizeof(upgrade_menu[menu_index].text_arg1));
	menu_index++;

	// Weapon upgrades
	Q_strlcpy(upgrade_menu[menu_index].text, "Weapons", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
	upgrade_menu[menu_index].SelectFunc = UpgradeMenuHandler;
	Q_strlcpy(upgrade_menu[menu_index].text_arg1, "weapons_shop", sizeof(upgrade_menu[menu_index].text_arg1));
	menu_index++;

	// In Classic Mode, show auto-buy status and toggle option
	if (is_classic_mode)
	{
		// Auto-buy status display
		const char* auto_buy_status = ent->client->pers.auto_buy_benefit_bot ? "ON" : "OFF";
		G_FmtTo(upgrade_menu[menu_index].text, "Auto-Buy: {}", auto_buy_status);
		upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
		upgrade_menu[menu_index].SelectFunc = nullptr;
		menu_index++;
	}
	else
	{
		// Placeholder for future talents (RPG Mode only)
		Q_strlcpy(upgrade_menu[menu_index].text, "Talents (Coming Soon)", sizeof(upgrade_menu[menu_index].text));
		upgrade_menu[menu_index].align = PMENU_ALIGN_LEFT;
		upgrade_menu[menu_index].SelectFunc = nullptr;
		menu_index++;
	}

	// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;
		// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;
		// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;
		// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;
		// Separator
	// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;
		// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;
		// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;
		// Separator
	Q_strlcpy(upgrade_menu[menu_index].text, "", sizeof(upgrade_menu[menu_index].text));
	upgrade_menu[menu_index].align = PMENU_ALIGN_CENTER;
	upgrade_menu[menu_index].SelectFunc = nullptr;
	menu_index++;

	// Back to main menu
	Q_strlcpy(upgrade_menu[menu_index].text, "Back", sizeof(upgrade_menu[menu_index].text));
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

	// Block weapon upgrade menu when vortex is 0 (Classic Mode)
	if (g_vortex->integer == 0)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Weapon upgrade menu is disabled in Classic Mode (vortex 0)\n");
		return;
	}

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	for (auto& e : weapon_upgrade_menu) e = {};
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
	snprintf(title, sizeof(title), "Weapon upgrading (%zu/%zu)", weapon_upgrade_current_page + 1, total_pages);
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

	if (weapon_upgrade_current_page < total_pages - 1)
	{
		add_entry("Next", PMENU_ALIGN_LEFT, WeaponUpgradeMenuHandler, "next_page");
	}

	if (weapon_upgrade_current_page > 0)
	{
		add_entry("< Previous", PMENU_ALIGN_LEFT, WeaponUpgradeMenuHandler, "prev_page");
	}

	add_entry("Reset All Weapon (Free)", PMENU_ALIGN_LEFT, WeaponUpgradeMenuHandler, "reset_weapons");
	add_entry("Back to Upgrades", PMENU_ALIGN_LEFT, WeaponUpgradeMenuHandler, "back_to_upgrades");

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
		// Note: tesla_chain is a skill talent, not a weapon upgrade, so it's not reset here

		refunded_points += skills.trap_damage + skills.trap_range + skills.trap_radius;
		skills.trap_damage = skills.trap_range = skills.trap_radius = 0;

		refunded_points += skills.pl_damage + skills.pl_range + skills.pl_radius;
		skills.pl_damage = skills.pl_range = skills.pl_radius = 0;
		skills.pl_trails = skills.pl_silent = skills.pl_improved_traps = false;

		// Refund the points
		ent->client->pers.weapon_points += refunded_points;

		// Save the reset to disk
		Character_Save(ent);

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

	for (auto& e : rl_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.rl_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "rl_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Speed", ent->client->pers.skills.rl_speed, 10);
	add_entry(status, PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "rl_speed");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Radius", ent->client->pers.skills.rl_radius, 10);
	add_entry(status, PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "rl_radius");

	const char *trails_status = ent->client->pers.skills.rl_trails ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "No Trails", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "rl_trails");

	const char *silent_status = ent->client->pers.skills.rl_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "rl_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, RLUpgradeMenuHandler, "back_to_weapons");

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
		OpenRLUpgradeMenu(ent, p->cur);
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
		OpenRLUpgradeMenu(ent, p->cur);
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
		OpenRLUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "rl_trails") == 0)
	{
		ent->client->pers.skills.rl_trails = !ent->client->pers.skills.rl_trails;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Rocket Launcher Trails: {}\n", ent->client->pers.skills.rl_trails ? "DISABLED" : "ENABLED");
		PMenu_Close(ent);
		OpenRLUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "rl_silent") == 0)
	{
		ent->client->pers.skills.rl_silent = !ent->client->pers.skills.rl_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Rocket Launcher Silent Mode: {}\n", ent->client->pers.skills.rl_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenRLUpgradeMenu(ent, p->cur);
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

	for (auto& e : gl_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.gl_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Range", ent->client->pers.skills.gl_range, 10);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_range");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Radius", ent->client->pers.skills.gl_radius, 10);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_radius");

	const char *trails_status = ent->client->pers.skills.gl_trails ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "No Trails", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_trails");

	const char *silent_status = ent->client->pers.skills.gl_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_silent");

	const char *bouncy_status = ent->client->pers.skills.gl_bouncy ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Bouncy Grenades", bouncy_status);
	add_entry(status, PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "gl_bouncy");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, GLUpgradeMenuHandler, "back_to_weapons");

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

	for (auto& e : mg_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.mg_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "mg_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Pierce", ent->client->pers.skills.mg_pierce, 10);
	add_entry(status, PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "mg_pierce");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Tracers", ent->client->pers.skills.mg_tracers, 10);
	add_entry(status, PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "mg_tracers");

	const char *spread_status = ent->client->pers.skills.mg_spread ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Reduced Spread", spread_status);
	add_entry(status, PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "mg_spread");

	const char *silent_status = ent->client->pers.skills.mg_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "mg_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, MGUpgradeMenuHandler, "back_to_weapons");

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
		OpenMGUpgradeMenu(ent, p->cur);
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
		OpenMGUpgradeMenu(ent, p->cur);
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
		OpenMGUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "mg_spread") == 0)
	{
		ent->client->pers.skills.mg_spread = !ent->client->pers.skills.mg_spread;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Machinegun Reduced Spread: {}\n", ent->client->pers.skills.mg_spread ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenMGUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "mg_silent") == 0)
	{
		ent->client->pers.skills.mg_silent = !ent->client->pers.skills.mg_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Machinegun Silent Mode: {}\n", ent->client->pers.skills.mg_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenMGUpgradeMenu(ent, p->cur);
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

	for (auto& e : cg_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.cg_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "cg_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Spin", ent->client->pers.skills.cg_spin, 10);
	add_entry(status, PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "cg_spin");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Tracers", ent->client->pers.skills.cg_tracers, 10);
	add_entry(status, PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "cg_tracers");

	const char *spread_status = ent->client->pers.skills.cg_spread ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Reduced Spread", spread_status);
	add_entry(status, PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "cg_spread");

	const char *silent_status = ent->client->pers.skills.cg_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "cg_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, CGUpgradeMenuHandler, "back_to_weapons");

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

	for (auto& e : sg_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.sg_damage, 10);
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
		OpenSGUpgradeMenu(ent, p->cur);
	}, "sg_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Strike", ent->client->pers.skills.sg_strike, 10);
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
		OpenSGUpgradeMenu(ent, p->cur);
	}, "sg_strike");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Pellets", ent->client->pers.skills.sg_pellets, 10);
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
		OpenSGUpgradeMenu(ent, p->cur);
	}, "sg_pellets");

	const char *spread_status = ent->client->pers.skills.sg_spread ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Reduced Spread: %s", spread_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.sg_spread = !ent->client->pers.skills.sg_spread;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Reduced Spread: {}\n", ent->client->pers.skills.sg_spread ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSGUpgradeMenu(ent, p->cur);
	}, "sg_spread");

	const char *silent_status = ent->client->pers.skills.sg_silent ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Silent Mode: %s", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.sg_silent = !ent->client->pers.skills.sg_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Silent Mode: {}\n", ent->client->pers.skills.sg_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSGUpgradeMenu(ent, p->cur);
	}, "sg_silent");

	const char *energized_status = ent->client->pers.skills.sg_energized ? "ON" : "OFF";
	snprintf(status, sizeof(status), "Energized Shells: %s", energized_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.sg_energized = !ent->client->pers.skills.sg_energized;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Shotgun Energized Shells: {}\n", ent->client->pers.skills.sg_energized ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSGUpgradeMenu(ent, p->cur);
	}, "sg_energized");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
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

	for (auto& e : ssg_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.ssg_damage, 10);
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
		OpenSSGUpgradeMenu(ent, p->cur);
	}, "ssg_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Strike", ent->client->pers.skills.ssg_strike, 10);
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
		OpenSSGUpgradeMenu(ent, p->cur);
	}, "ssg_strike");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Pellets", ent->client->pers.skills.ssg_pellets, 10);
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
		OpenSSGUpgradeMenu(ent, p->cur);
	}, "ssg_pellets");

	const char *spread_status = ent->client->pers.skills.ssg_spread ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Reduced Spread", spread_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.ssg_spread = !ent->client->pers.skills.ssg_spread;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Reduced Spread: {}\n", ent->client->pers.skills.ssg_spread ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSSGUpgradeMenu(ent, p->cur);
	}, "ssg_spread");

	const char *silent_status = ent->client->pers.skills.ssg_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.ssg_silent = !ent->client->pers.skills.ssg_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Silent Mode: {}\n", ent->client->pers.skills.ssg_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSSGUpgradeMenu(ent, p->cur);
	}, "ssg_silent");

	const char *energized_status = ent->client->pers.skills.ssg_energized ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Energized Shells", energized_status);
	add_entry(status, PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
		ent->client->pers.skills.ssg_energized = !ent->client->pers.skills.ssg_energized;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Super Shotgun Energized Shells: {}\n", ent->client->pers.skills.ssg_energized ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenSSGUpgradeMenu(ent, p->cur);
	}, "ssg_energized");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, [](edict_t *ent, pmenuhnd_t *p) {
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

	for (auto& e : hg_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.hg_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, HGUpgradeMenuHandler, "hg_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Range", ent->client->pers.skills.hg_range, 10);
	add_entry(status, PMENU_ALIGN_LEFT, HGUpgradeMenuHandler, "hg_range");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Radius Damage", ent->client->pers.skills.hg_radius_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, HGUpgradeMenuHandler, "hg_radius_damage");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, HGUpgradeMenuHandler, "back_to_weapons");

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

void OpenProxUpgradeMenu(edict_t *ent, int cursor_pos)
{
	if (!ent || !ent->client)
		return;

	if (ent->client->menu)
		PMenu_Close(ent);

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	for (auto& e : prox_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.pl_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Range", ent->client->pers.skills.pl_range, 10);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_range");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Radius", ent->client->pers.skills.pl_radius, 10);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_radius");

	const char *trails_status = ent->client->pers.skills.pl_trails ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "No Trails", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_trails");

	const char *silent_status = ent->client->pers.skills.pl_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_silent");

	const char *improved_status = ent->client->pers.skills.pl_improved_traps ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Improved Traps", improved_status);
	add_entry(status, PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "pl_improved_traps");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, ProxUpgradeMenuHandler, "back_to_weapons");

	PMenu_Open(ent, prox_upgrade_menu, cursor_pos, count, nullptr, nullptr);
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
		OpenProxUpgradeMenu(ent, p->cur);
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
		OpenProxUpgradeMenu(ent, p->cur);
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
		OpenProxUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "pl_trails") == 0)
	{
		ent->client->pers.skills.pl_trails = !ent->client->pers.skills.pl_trails;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Trails: {}\n", ent->client->pers.skills.pl_trails ? "DISABLED" : "ENABLED");
		PMenu_Close(ent);
		OpenProxUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "pl_silent") == 0)
	{
		ent->client->pers.skills.pl_silent = !ent->client->pers.skills.pl_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Silent Mode: {}\n", ent->client->pers.skills.pl_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenProxUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "pl_improved_traps") == 0)
	{
		ent->client->pers.skills.pl_improved_traps = !ent->client->pers.skills.pl_improved_traps;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "Prox Launcher Improved Traps: {}\n", ent->client->pers.skills.pl_improved_traps ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenProxUpgradeMenu(ent, p->cur);
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

	for (auto& e : blaster_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.bl_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, BlasterUpgradeMenuHandler, "bl_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Speed", ent->client->pers.skills.bl_speed, 10);
	add_entry(status, PMENU_ALIGN_LEFT, BlasterUpgradeMenuHandler, "bl_speed");

	const char *trails_status = ent->client->pers.skills.bl_trails ? "DISABLED" : "ENABLED";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Trails", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, BlasterUpgradeMenuHandler, "bl_trails");

	const char *silent_status = ent->client->pers.skills.bl_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, BlasterUpgradeMenuHandler, "bl_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, BlasterUpgradeMenuHandler, "back_to_weapons");

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

	for (auto& e : hyperblaster_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.hb_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, HyperblasterUpgradeMenuHandler, "hb_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Speed", ent->client->pers.skills.hb_speed, 10);
	add_entry(status, PMENU_ALIGN_LEFT, HyperblasterUpgradeMenuHandler, "hb_speed");

	const char *trails_status = ent->client->pers.skills.hb_trails ? "DISABLED" : "ENABLED";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Trails", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, HyperblasterUpgradeMenuHandler, "hb_trails");

	const char *silent_status = ent->client->pers.skills.hb_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, HyperblasterUpgradeMenuHandler, "hb_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, HyperblasterUpgradeMenuHandler, "back_to_weapons");

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

	for (auto& e : etf_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.etf_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, ETFUpgradeMenuHandler, "etf_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Speed", ent->client->pers.skills.etf_speed, 10);
	add_entry(status, PMENU_ALIGN_LEFT, ETFUpgradeMenuHandler, "etf_speed");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Kick", ent->client->pers.skills.etf_kick, 10);
	add_entry(status, PMENU_ALIGN_LEFT, ETFUpgradeMenuHandler, "etf_kick");

	const char *silent_status = ent->client->pers.skills.etf_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, ETFUpgradeMenuHandler, "etf_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, ETFUpgradeMenuHandler, "back_to_weapons");

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
		OpenETFUpgradeMenu(ent, p->cur);
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
		OpenETFUpgradeMenu(ent, p->cur);
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
		OpenETFUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "etf_silent") == 0)
	{
		ent->client->pers.skills.etf_silent = !ent->client->pers.skills.etf_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "ETF Rifle Silent Mode: {}\n", ent->client->pers.skills.etf_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenETFUpgradeMenu(ent, p->cur);
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

	for (auto& e : ionripper_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.ir_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, IonRipperUpgradeMenuHandler, "ir_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Speed", ent->client->pers.skills.ir_speed, 10);
	add_entry(status, PMENU_ALIGN_LEFT, IonRipperUpgradeMenuHandler, "ir_speed");

	const char *trails_status = ent->client->pers.skills.ir_trails ? "OFF" : "ON";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Trails", trails_status);
	add_entry(status, PMENU_ALIGN_LEFT, IonRipperUpgradeMenuHandler, "ir_trails");

	const char *silent_status = ent->client->pers.skills.ir_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, IonRipperUpgradeMenuHandler, "ir_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, IonRipperUpgradeMenuHandler, "back_to_weapons");

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

	for (auto& e : railgun_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.rg_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, RailgunUpgradeMenuHandler, "rg_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Burn", ent->client->pers.skills.rg_burn, 10);
	add_entry(status, PMENU_ALIGN_LEFT, RailgunUpgradeMenuHandler, "rg_burn");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Pierce", ent->client->pers.skills.rg_pierce, 10);
	add_entry(status, PMENU_ALIGN_LEFT, RailgunUpgradeMenuHandler, "rg_pierce");

	const char *silent_status = ent->client->pers.skills.rg_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, RailgunUpgradeMenuHandler, "rg_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, RailgunUpgradeMenuHandler, "back_to_weapons");

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

	for (auto& e : bfg_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.bfg_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "bfg_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Speed", ent->client->pers.skills.bfg_speed, 10);
	add_entry(status, PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "bfg_speed");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Duration", ent->client->pers.skills.bfg_duration, 10);
	add_entry(status, PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "bfg_duration");

	// BFG Mode display
	const char *mode_str = "Normal";
	if (ent->client->pers.bfg_mode == BFGMode::SLIDE)
		mode_str = "Slide";
	else if (ent->client->pers.bfg_mode == BFGMode::GRAV_PULL)
		mode_str = "Pull";

	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Mode", mode_str);
	add_entry(status, PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "bfg_mode");

	const char *silent_status = ent->client->pers.skills.bfg_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "bfg_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, BFGUpgradeMenuHandler, "back_to_weapons");

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
		OpenBFGUpgradeMenu(ent, p->cur);
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
		OpenBFGUpgradeMenu(ent, p->cur);
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
		OpenBFGUpgradeMenu(ent, p->cur);
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
		OpenBFGUpgradeMenu(ent, p->cur);
	}
	else if (strcmp(arg, "bfg_silent") == 0)
	{
		ent->client->pers.skills.bfg_silent = !ent->client->pers.skills.bfg_silent;
		gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "BFG10K Silent Mode: {}\n", ent->client->pers.skills.bfg_silent ? "ON" : "OFF");
		PMenu_Close(ent);
		OpenBFGUpgradeMenu(ent, p->cur);
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

	// Clear menu
	for (auto& e : cannon20mm_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.cannon20mm_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, ETGUpgradeMenuHandler, "cannon20mm_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Range", ent->client->pers.skills.cannon20mm_range, 10);
	add_entry(status, PMENU_ALIGN_LEFT, ETGUpgradeMenuHandler, "cannon20mm_range");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Recoil Reduction", ent->client->pers.skills.cannon20mm_recoil, 10);
	add_entry(status, PMENU_ALIGN_LEFT, ETGUpgradeMenuHandler, "cannon20mm_recoil");

	const char *silent_status = ent->client->pers.skills.cannon20mm_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, ETGUpgradeMenuHandler, "cannon20mm_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, ETGUpgradeMenuHandler, "back_to_weapons");

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

	for (auto& e : plasmabeam_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.pb_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, PlasmabeamUpgradeMenuHandler, "pb_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Burn", ent->client->pers.skills.pb_burn, 10);
	add_entry(status, PMENU_ALIGN_LEFT, PlasmabeamUpgradeMenuHandler, "pb_burn");

	snprintf(status, sizeof(status), "%d. Pierce Level: %d/10 (%.0f%% chance)", item_num++, ent->client->pers.skills.pb_pierce, ent->client->pers.skills.pb_pierce * 4.0f);
	add_entry(status, PMENU_ALIGN_LEFT, PlasmabeamUpgradeMenuHandler, "pb_pierce");

	const char *silent_status = ent->client->pers.skills.pb_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, PlasmabeamUpgradeMenuHandler, "pb_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, PlasmabeamUpgradeMenuHandler, "back_to_weapons");

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
//namespace fmt_game = fmt;

// Constants

constexpr size_t MAX_PLAYERS_TO_DISPLAY = 14;
constexpr int PLAYER_Y_START = 42;
constexpr int PLAYER_Y_SPACING = 8;
constexpr int LAYOUT_SAFETY_MARGIN = 50;
static constexpr size_t MAX_BONUS_STRING_LENGTH = 400; 

/**
 *  StringBuilder
 * Helper class for efficient string concatenation
 */
class StringBuilder {
private:
	std::string buffer;

public:
	explicit StringBuilder(size_t reserved_size = 256) {
		buffer.reserve(reserved_size);
	}

	StringBuilder& append(std::string_view text) {
		buffer.append(text);
		return *this;
	}

	bool append_checked(std::string_view text, size_t reserve_margin = 0) {
		if (text.empty())
			return true;

		const size_t limit = MAX_CTF_STAT_LENGTH;
		const size_t safe_limit = (limit > (reserve_margin + 1)) ? (limit - reserve_margin - 1) : 0;

		if (buffer.size() + text.size() > safe_limit)
			return false;

		buffer.append(text);
		return true;
	}

	std::string str() const {
		return buffer;
	}

	size_t size() const {
		return buffer.size();
	}
};

/**
 * SanitizeLayoutText
 * Returns a copy of `text` safe to embed inside a quoted layout `string "..."`
 * token. The client-side layout parser has no concept of escaped quotes, so a
 * stray '"' or '\\' in a player name or bonus string would terminate the token
 * early and corrupt the rest of the layout (silent client crash). Control chars
 * (< 0x20, including the raw newlines used to separate active bonuses) are
 * collapsed to spaces. High-bit bytes (>= 0x80) are preserved so colored/special
 * name glyphs still render.
 */
static std::string SanitizeLayoutText(std::string_view text) {
	std::string out;
	out.reserve(text.size());
	for (unsigned char c : text) {
		if (c == '"' || c == '\\' || c < 0x20)
			out += ' ';
		else
			out += static_cast<char>(c);
	}
	return out;
}


/**
 * PlayerScore
 * Contains player score information for the scoreboard
 */
struct PlayerScore {
	unsigned int index;
	int score;
	int ping;
	bool is_dead;

	// Sort players by score in descending order
	bool operator>(const PlayerScore& other) const {
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
	// Increased capacity to handle typical server sizes (avoid heap allocation)
	boost::container::small_vector<PlayerScore, 32> team_players;  // Typical servers: 16-32 players
	boost::container::small_vector<PlayerScore, 16> spectators;    // Typical spectators: <16
	int total_score;

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

			// Create player score entry
			PlayerScore player = {
				i,
				cl->resp.score,
				std::clamp(cl->ping, 0, 999),
				(cl_ent->deadflag != 0)  // Using deadflag to determine if player is dead
			};

			// Sort into appropriate team
			if (cl->resp.ctf_team == CTF_TEAM1) {
				team_players.push_back(player);
				total_score += player.score;
			}
			else if (cl->resp.ctf_team == CTF_NOTEAM) {
				spectators.push_back(player);
			}
		}

		// Sort team players by score
		std::sort(team_players.begin(), team_players.end(), std::greater<>());
	}

	void addHeader()
	{
		if (g_horde->integer || pvm->integer)
		{
			// string2 is better than loc_string2 here it seems
			// Element 1: Wave Number (aligned left)
			const std::string wave_line = fmt::format(
				"xv -140 yv -5 string2 \"Wave: {}\" \n",
				last_wave_number);
			if (!layout_builder.append_checked(wave_line, LAYOUT_SAFETY_MARGIN))
				return;

			// Element 2: Stroggs Remaining (aligned further to the right)
			const std::string strogg_line = fmt::format(
				"xv -40 yv -5 string2 \"Stroggs: {}\" \n",
				GetStroggsNum());
			if (!layout_builder.append_checked(strogg_line, LAYOUT_SAFETY_MARGIN))
				return;
		}

		// Time limit remains the same
		if (timelimit->value)
		{
			const std::string time_line = fmt::format(
				"xv 340 yv -33 time_limit {} \n",
				gi.ServerFrame() + ((gtime_t::from_min(timelimit->value) - level.time)).milliseconds() / gi.frame_time_ms);
			(void)layout_builder.append_checked(time_line, LAYOUT_SAFETY_MARGIN);
		}
	}


		void addTeamScore()
	{
		const char *horde_dogtag_path = "/tags/etqw_strogg.png";
		// Display Strogg team icon
		const std::string icon_line = fmt::format(
			"xv -140 yv 3 picn {} \n", horde_dogtag_path);
		if (!layout_builder.append_checked(icon_line, LAYOUT_SAFETY_MARGIN))
			return;

		if (!level.intermissiontime)
		{

			// Active bonuses (per-player) with length check
			std::string activeBonuses = GetPlayerActiveBonusesString(const_cast<edict_t*>(ent));
			if (!activeBonuses.empty()) {
	//			Truncate if too long to prevent overflow
				if (activeBonuses.length() > MAX_BONUS_STRING_LENGTH) {
	//				Safe resize with bounds check
					try {
						activeBonuses.resize(MAX_BONUS_STRING_LENGTH);
						activeBonuses += "...";
					} catch (const std::bad_alloc&) {
						gi.Com_Print("WARNING: Failed to resize bonus string\n");
						activeBonuses = "Error";
					}
				}
				// Safety net: strip any quote/backslash/control chars before embedding
				activeBonuses = SanitizeLayoutText(activeBonuses);
				const std::string bonus_line = fmt::format(
					"xv 208 yv 8 string \"{}\" \n", activeBonuses);
				(void)layout_builder.append_checked(bonus_line, LAYOUT_SAFETY_MARGIN);
			}
		}
		else
		{
			// Intermission screen - display Strogg team icon (uses stat 26 = STAT_CTF_TEAM2_HEADER, right side)
			(void)layout_builder.append_checked("if 26 xv 208 yv 8 pic 25 endif \n", LAYOUT_SAFETY_MARGIN);
		}
	}

	void addPlayerList() {




		int header_y = PLAYER_Y_START - PLAYER_Y_SPACING;
				if (g_vortex->integer)
		{
			const std::string header_line = fmt::format(
				"yv {} xv -140 string2 \"Name\" xv 70 string2 \"Score\" xv 120 string2 \"Lv\" xv 160 string2 \"Ping\" \n",
				header_y);
			if (!layout_builder.append_checked(header_line, LAYOUT_SAFETY_MARGIN))
				return;
		}
		else
		{
			const std::string header_line = fmt::format(
				"yv {} xv -140 string2 \"Name\" xv 70 string2 \"Score\" xv 120 string2 \"Ping\" \n",
				header_y);
			if (!layout_builder.append_checked(header_line, LAYOUT_SAFETY_MARGIN))
				return;
		}
		bool truncated = false;
		for (size_t i = 0; i < std::min(team_players.size(), MAX_PLAYERS_TO_DISPLAY); ++i) {
			const auto& player = team_players[i];
		edict_t *player_ent = g_edicts + 1 + player.index;
		const char *player_name = GetPlayerName(player_ent);
			int y = PLAYER_Y_START + i * PLAYER_Y_SPACING;

			// Add death indicator if player is dead
			if (player.is_dead) {
				const std::string dead_line = fmt::format(
					"xv -185 yv {} string \"[Dead]\" ", y);
				if (!layout_builder.append_checked(dead_line, LAYOUT_SAFETY_MARGIN)) {
					truncated = true;
					break;
				}
			}

			// Add player information (truncate name to prevent overflow)
				std::string display_name = player_name;
				if (display_name.length() > 20) {
					display_name.resize(17);
					display_name += "...";
				}
				display_name = SanitizeLayoutText(display_name);
				const std::string player_line = fmt::format(
					"yv {} xv -140 string \"{}\" xv 70 string \"{}\"  xv 120 string \"{}\" \n",
					y, display_name, player.score, player.ping);
				if (!layout_builder.append_checked(player_line, LAYOUT_SAFETY_MARGIN)) {
					truncated = true;
					break;
				}
		}
		if (truncated) {
			const int y = PLAYER_Y_START + static_cast<int>(std::min(team_players.size(), MAX_PLAYERS_TO_DISPLAY)) * PLAYER_Y_SPACING;
			const std::string more_line = fmt::format(
				"xv -90 yv {} string2 \"And more...\" \n", y);
			(void)layout_builder.append_checked(more_line, LAYOUT_SAFETY_MARGIN);
		}
	}

	void addSpectators() {
		// Only add spectators if there's enough space and there are spectators
		if (layout_builder.size() < MAX_CTF_STAT_LENGTH - LAYOUT_SAFETY_MARGIN && !spectators.empty()) {
			// Calculate vertical position after team players
			int y = PLAYER_Y_START + (std::min(team_players.size(), MAX_PLAYERS_TO_DISPLAY) + 2) * PLAYER_Y_SPACING;

			// Add spectator header
			const std::string spec_header = fmt::format(
				"xv -90 yv {} loc_string2 0 \"Spectators & AFK\" \n", y);
			if (!layout_builder.append_checked(spec_header, LAYOUT_SAFETY_MARGIN))
				return;
			y += PLAYER_Y_SPACING;

			// Add each spectator

			for (const auto& spec : spectators) {

				edict_t *spec_ent = g_edicts + 1 + spec.index;
				const char *spec_name = GetPlayerName(spec_ent);

				// Truncate spectator names to prevent overflow
				std::string display_spec_name = spec_name;
				if (display_spec_name.length() > 20) {
					display_spec_name.resize(17);
					display_spec_name += "...";
				}
				display_spec_name = SanitizeLayoutText(display_spec_name);

				// Optimized format: Name, Score, Ping (spectators don't have levels)
				if (!g_vortex->integer)
				{
					const std::string spec_line = fmt::format(
					"yv {} xv -140 string2 \"{}\" xv 70 string2 \"{}\" xv 120 string2 \"{}\" \n",
					y, display_spec_name, spec.score, spec.ping);
					if (!layout_builder.append_checked(spec_line, LAYOUT_SAFETY_MARGIN))
						break;
				}
				else	{
				const std::string spec_line = fmt::format(
					"yv {} xv -140 string2 \"{}\" xv 70 string2 \"{}\" xv 160 string2 \"{}\" \n",
					y, display_spec_name, spec.score, spec.ping);
				if (!layout_builder.append_checked(spec_line, LAYOUT_SAFETY_MARGIN))
					break;
				}

				y += PLAYER_Y_SPACING;
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

			// Check if we have enough space for the help text (reserve ~150 bytes)
			if (layout_builder.size() < MAX_CTF_STAT_LENGTH - 150) {
				const std::string help_line = fmt::format(
					"xv 0 yb -55 cstring2 \"{}\" \n", help_text);
				(void)layout_builder.append_checked(help_line, LAYOUT_SAFETY_MARGIN);
			}
		}
		else
		{
			// This block runs during the intermission.
			const char *message = brandom()
									  ? "MAKE THEM PAY!"
									  : "THEY WILL REGRET THIS!";

			// Only add intermission message if we have enough space to avoid truncation
			// The ifgef block needs ~100 chars, so reserve 150 to be safe
			if (layout_builder.size() < MAX_CTF_STAT_LENGTH - 150) {
				// It will display the message after a 5-second delay.
				const std::string intermission_line = fmt::format(
					"ifgef {} yb -48 xv 0 loc_cstring2 0 \"{}\" endif \n",
					level.intermission_server_frame + (5_sec).frames(),
					message);
				(void)layout_builder.append_checked(intermission_line, LAYOUT_SAFETY_MARGIN);
			}
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
void HordeScoreboardMessage(edict_t* ent, edict_t* killer) {
	// Belt-and-suspenders: every current caller passes a live client, but the
	// addFooter()/addTeamScore() helpers dereference ent->client directly.
	if (!ent || !ent->client)
		return;

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
	if (final_layout.size() >= MAX_CTF_STAT_LENGTH) {
		if (developer && developer->integer)
			gi.Com_PrintFmt("ERROR: Scoreboard layout exceeded size limit ({} >= {}), not sending\n",
							final_layout.size(), MAX_CTF_STAT_LENGTH);
		return;
	}

	// Validate layout before sending to client
	if (!final_layout.empty() && !ValidateLayoutString(final_layout, "HordeScoreboardMessage")) {
		if (developer && developer->integer)
			gi.Com_Print("ERROR: HordeScoreboardMessage layout failed validation, not sending\n");
		return;
	}

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

	for (auto& e : chainfist_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.cf_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, ChainfistUpgradeMenuHandler, "cf_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Range", ent->client->pers.skills.cf_range, 10);
	add_entry(status, PMENU_ALIGN_LEFT, ChainfistUpgradeMenuHandler, "cf_range");

	const char *silent_status = ent->client->pers.skills.cf_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, ChainfistUpgradeMenuHandler, "cf_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, ChainfistUpgradeMenuHandler, "back_to_weapons");

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

	for (auto& e : tesla_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.tesla_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, TeslaUpgradeMenuHandler, "tesla_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Range", ent->client->pers.skills.tesla_range, 10);
	add_entry(status, PMENU_ALIGN_LEFT, TeslaUpgradeMenuHandler, "tesla_range");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Radius", ent->client->pers.skills.tesla_radius, 10);
	add_entry(status, PMENU_ALIGN_LEFT, TeslaUpgradeMenuHandler, "tesla_radius");

	const char *chain_status = ent->client->pers.skills.tesla_chain ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Chain Lightning", chain_status);
	add_entry(status, PMENU_ALIGN_LEFT, TeslaUpgradeMenuHandler, "tesla_chain");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, TeslaUpgradeMenuHandler, "back_to_weapons");

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

	for (auto& e : trap_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.trap_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, TrapUpgradeMenuHandler, "trap_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Range", ent->client->pers.skills.trap_range, 10);
	add_entry(status, PMENU_ALIGN_LEFT, TrapUpgradeMenuHandler, "trap_range");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Radius", ent->client->pers.skills.trap_radius, 10);
	add_entry(status, PMENU_ALIGN_LEFT, TrapUpgradeMenuHandler, "trap_radius");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, TrapUpgradeMenuHandler, "back_to_weapons");

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

	for (auto& e : phalanx_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.phalanx_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, PhalanxUpgradeMenuHandler, "phalanx_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Speed", ent->client->pers.skills.phalanx_speed, 10);
	add_entry(status, PMENU_ALIGN_LEFT, PhalanxUpgradeMenuHandler, "phalanx_speed");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Radius", ent->client->pers.skills.phalanx_radius, 10);
	add_entry(status, PMENU_ALIGN_LEFT, PhalanxUpgradeMenuHandler, "phalanx_radius");

	const char *silent_status = ent->client->pers.skills.phalanx_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, PhalanxUpgradeMenuHandler, "phalanx_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, PhalanxUpgradeMenuHandler, "back_to_weapons");

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

	for (auto& e : disruptor_upgrade_menu) e = {};
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

	// Display current upgrade levels using MenuFormatItemWithProgress
	char status[128];
	int item_num = 1;

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Damage", ent->client->pers.skills.disruptor_damage, 10);
	add_entry(status, PMENU_ALIGN_LEFT, DisruptorUpgradeMenuHandler, "disruptor_damage");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Speed", ent->client->pers.skills.disruptor_speed, 10);
	add_entry(status, PMENU_ALIGN_LEFT, DisruptorUpgradeMenuHandler, "disruptor_speed");

	MenuFormatItemWithProgress(status, sizeof(status), item_num++, "Duration", ent->client->pers.skills.disruptor_duration, 10);
	add_entry(status, PMENU_ALIGN_LEFT, DisruptorUpgradeMenuHandler, "disruptor_duration");

	const char *silent_status = ent->client->pers.skills.disruptor_silent ? "ON" : "OFF";
	MenuFormatItemWithCustom(status, sizeof(status), item_num++, "Silent Mode", silent_status);
	add_entry(status, PMENU_ALIGN_LEFT, DisruptorUpgradeMenuHandler, "disruptor_silent");

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("---", PMENU_ALIGN_CENTER);
	add_entry("Back to Weapons", PMENU_ALIGN_LEFT, DisruptorUpgradeMenuHandler, "back_to_weapons");

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

// ========================================================================
// BONUS MANAGEMENT MENU SYSTEM (Classic Mode - Non-Vortex)
// ========================================================================

// Helper: Get the cost of a benefit
static int32_t GetBenefitCost(BenefitID benefit_id) {
	// Special cases with higher costs
	if (benefit_id == BenefitID::BFG_GRAV_PULL || benefit_id == BenefitID::CLUSTER_PROX) {
		return 3;
	}
	// Default cost for all other benefits
	return 1;
}

// Helper: Check if any purchased benefits depend on this one
static bool HasDependentBenefits(edict_t* player, BenefitID benefit_id) {
	if (!player || !player->client) return false;

	for (size_t i = 0; i < g_benefitsData.NUM_BENEFITS; ++i) {
		BenefitID other_benefit = static_cast<BenefitID>(i);

		// Skip if not purchased
		if (!ClassicPlayerHasPurchasedBenefit(player, other_benefit)) continue;

		// Check if this benefit is a prerequisite for the other
		auto prereq = g_benefitsData.prerequisites[i];
		if (prereq == benefit_id) {
			return true; // Found a dependent benefit
		}
	}

	return false;
}

// Helper: Refund a benefit (remove purchase, deactivate, return points)
static bool RefundBenefit(edict_t* player, BenefitID benefit_id) {
	if (!player || !player->client) return false;

	// Check if player actually owns it
	if (!ClassicPlayerHasPurchasedBenefit(player, benefit_id)) {
		return false;
	}

	// Check if any other benefits depend on this one
	if (HasDependentBenefits(player, benefit_id)) {
		gi.LocClient_Print(player, PRINT_HIGH, "Cannot refund: Other benefits depend on this!\n");
		return false;
	}

	// Get the cost to refund
	int32_t refund_amount = GetBenefitCost(benefit_id);

	// Determine which point pool to refund to
	BenefitCategory category = g_benefitsData.categories[static_cast<size_t>(benefit_id)];

	// Deactivate the benefit first
	BotDeactivateBenefit(player, benefit_id);

	// Remove from purchased mask
	uint8_t bit_pos = static_cast<uint8_t>(benefit_id);
	player->client->pers.purchased_benefits_mask &= ~(1u << bit_pos);

	// Refund the points
	if (category == BenefitCategory::ABILITY) {
		player->client->pers.ability_points += refund_amount;
	} else if (category == BenefitCategory::WEAPON) {
		player->client->pers.weapon_points += refund_amount;
	}

	// Show message
	const char* benefit_name = g_benefitsData.names[static_cast<size_t>(benefit_id)];
	gi.LocClient_Print(player, PRINT_HIGH, "{} refunded ({} pt{})\n",
		benefit_name, refund_amount, refund_amount > 1 ? "s" : "");

	return true;
}

// ========================================================================
// BONUS ABILITIES MENU (Classic Mode)
// ========================================================================

void BonusAbilitiesMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p || p->cur < 0 || p->cur >= p->num) {
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	const pmenu_t* selected_entry = &p->entries[p->cur];
	const char* arg = selected_entry->text_arg1;

	if (!arg || arg[0] == '\0') {
		// No action - title or separator
		return;
	}

	// Handle refund for owned benefits
	if (strncmp(arg, "refund_", 7) == 0) {
		const char* benefit_name = arg + 7; // Skip "refund_" prefix

		// Find benefit by name
		for (size_t i = 0; i < g_benefitsData.NUM_BENEFITS; ++i) {
			if (g_benefitsData.categories[i] != BenefitCategory::ABILITY) continue;

			if (strcmp(g_benefitsData.names[i], benefit_name) == 0) {
				BenefitID benefit_id = static_cast<BenefitID>(i);

				// Attempt to refund the benefit
				RefundBenefit(ent, benefit_id);

				// Reopen menu to show updated state
				PMenu_Close(ent);
				OpenBonusAbilitiesMenu(ent, p->cur);
				return;
			}
		}
	}

	// Handle purchase of new benefits
	if (strncmp(arg, "purchase_", 9) == 0) {
		const char* benefit_name = arg + 9; // Skip "purchase_" prefix

		// Find benefit by name
		for (size_t i = 0; i < g_benefitsData.NUM_BENEFITS; ++i) {
			if (g_benefitsData.categories[i] != BenefitCategory::ABILITY) continue;

			if (strcmp(g_benefitsData.names[i], benefit_name) == 0) {
				BenefitID benefit_id = static_cast<BenefitID>(i);
				int32_t cost = 1; // All abilities cost 1 point

				if (BotPurchaseBenefit(ent, benefit_id, cost)) {
					// Success message already shown by BotPurchaseBenefit
				}
				
				// Reopen menu to show updated state
				PMenu_Close(ent);
				OpenBonusAbilitiesMenu(ent, p->cur);
				return;
			}
		}
	}

	// Handle back action
	if (strcmp(arg, "back") == 0) {
		PMenu_Close(ent);
		OpenBonusManagementMenu(ent);
	}
}

void OpenBonusAbilitiesMenu(edict_t* ent, int cursor_position) {
	if (!ent || !ent->client) return;

	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	static pmenu_t entries[64];
	for (auto& e : entries) e = {};
	int count = 0;

	auto add_entry = [&](const char* text, int align, SelectFunc_t func = nullptr, const char* text_arg = nullptr) {
		if (count < static_cast<int>(std::size(entries))) {
			Q_strlcpy(entries[count].text, text, sizeof(entries[count].text));
			entries[count].align = align;
			entries[count].SelectFunc = func;
			if (text_arg) {
				Q_strlcpy(entries[count].text_arg1, text_arg, sizeof(entries[count].text_arg1));
			}
			count++;
		}
	};

	// Title
	add_entry("=== ABILITIES ===", PMENU_ALIGN_CENTER);
	
	// Points display
	char points_buffer[64];
	G_FmtTo(points_buffer, "Points: {}", ent->client->pers.ability_points);
	add_entry(points_buffer, PMENU_ALIGN_CENTER);
	
	// Separator
	add_entry("", PMENU_ALIGN_CENTER);

	// Show owned abilities first (with refund option)
	bool has_owned = false;
	for (size_t i = 0; i < g_benefitsData.NUM_BENEFITS && count < 55; ++i) {
		if (g_benefitsData.categories[i] != BenefitCategory::ABILITY) continue;

		BenefitID benefit_id = static_cast<BenefitID>(i);

		// Check if player has purchased this benefit
		if (ClassicPlayerHasPurchasedBenefit(ent, benefit_id)) {
			has_owned = true;
			int32_t refund_cost = GetBenefitCost(benefit_id);

			char buffer[128];
			G_FmtTo(buffer, "  > Refund: {} ({} pt{})", g_benefitsData.names[i], refund_cost, refund_cost > 1 ? "s" : "");

			char arg_buffer[64];
			snprintf(arg_buffer, sizeof(arg_buffer), "refund_%s", g_benefitsData.names[i]);

			add_entry(buffer, PMENU_ALIGN_LEFT, BonusAbilitiesMenuHandler, arg_buffer);
		}
	}

	if (has_owned) {
		add_entry("---", PMENU_ALIGN_CENTER);
	}

	// Show available abilities to purchase
	bool has_available = false;
	for (size_t i = 0; i < g_benefitsData.NUM_BENEFITS && count < 60; ++i) {
		if (g_benefitsData.categories[i] != BenefitCategory::ABILITY) continue;

		BenefitID benefit_id = static_cast<BenefitID>(i);
		
		// Skip if already purchased
		if (ClassicPlayerHasPurchasedBenefit(ent, benefit_id)) {
			continue;
		}

		// Check prerequisites
		auto prereq = g_benefitsData.prerequisites[i];
		bool prereq_met = (prereq == BenefitID::NONE) || ClassicPlayerHasPurchasedBenefit(ent, prereq);
		
		if (!prereq_met) {
			continue;
		}

		has_available = true;
		bool can_afford = ent->client->pers.ability_points >= 1;
		
		char buffer[128];
		G_FmtTo(buffer, "  {} {} (1 pt)", can_afford ? ">" : " ", g_benefitsData.names[i]);
		
		if (can_afford) {
			char arg_buffer[64];
			snprintf(arg_buffer, sizeof(arg_buffer), "purchase_%s", g_benefitsData.names[i]);
			add_entry(buffer, PMENU_ALIGN_LEFT, BonusAbilitiesMenuHandler, arg_buffer);
		} else {
			add_entry(buffer, PMENU_ALIGN_LEFT);
		}
	}

	if (!has_available && !has_owned) {
		add_entry("No abilities available", PMENU_ALIGN_CENTER);
	}

	// Separator
	add_entry("", PMENU_ALIGN_CENTER);
	
	// Back option
	add_entry("< Back", PMENU_ALIGN_LEFT, BonusAbilitiesMenuHandler, "back");

	PMenu_Open(ent, entries, cursor_position, count, nullptr, nullptr);
}

// ========================================================================
// BONUS WEAPONS MENU (Classic Mode)
// ========================================================================

void BonusWeaponsMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p || p->cur < 0 || p->cur >= p->num) {
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	const pmenu_t* selected_entry = &p->entries[p->cur];
	const char* arg = selected_entry->text_arg1;

	if (!arg || arg[0] == '\0') {
		// No action - title or separator
		return;
	}

	// Handle refund for owned benefits
	if (strncmp(arg, "refund_", 7) == 0) {
		const char* benefit_name = arg + 7; // Skip "refund_" prefix

		// Find benefit by name
		for (size_t i = 0; i < g_benefitsData.NUM_BENEFITS; ++i) {
			if (g_benefitsData.categories[i] != BenefitCategory::WEAPON) continue;

			if (strcmp(g_benefitsData.names[i], benefit_name) == 0) {
				BenefitID benefit_id = static_cast<BenefitID>(i);

				// Attempt to refund the benefit
				RefundBenefit(ent, benefit_id);

				// Reopen menu to show updated state
				PMenu_Close(ent);
				OpenBonusWeaponsMenu(ent, p->cur);
				return;
			}
		}
	}

	// Handle purchase of new benefits
	if (strncmp(arg, "purchase_", 9) == 0) {
		const char* benefit_name = arg + 9; // Skip "purchase_" prefix

		// Find benefit by name
		for (size_t i = 0; i < g_benefitsData.NUM_BENEFITS; ++i) {
			if (g_benefitsData.categories[i] != BenefitCategory::WEAPON) continue;

			if (strcmp(g_benefitsData.names[i], benefit_name) == 0) {
				BenefitID benefit_id = static_cast<BenefitID>(i);
				
				// Set cost based on benefit type
				int32_t cost = 1;
				if (benefit_id == BenefitID::BFG_GRAV_PULL || benefit_id == BenefitID::CLUSTER_PROX) {
					cost = 3;
				}

				if (BotPurchaseBenefit(ent, benefit_id, cost)) {
					// Success message already shown by BotPurchaseBenefit
				}
				
				// Reopen menu to show updated state
				PMenu_Close(ent);
				OpenBonusWeaponsMenu(ent, p->cur);
				return;
			}
		}
	}

	// Handle back action
	if (strcmp(arg, "back") == 0) {
		PMenu_Close(ent);
		OpenBonusManagementMenu(ent);
	}
}

void OpenBonusWeaponsMenu(edict_t* ent, int cursor_position) {
	if (!ent || !ent->client) return;

	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	static pmenu_t entries[64];
	for (auto& e : entries) e = {};
	int count = 0;

	auto add_entry = [&](const char* text, int align, SelectFunc_t func = nullptr, const char* text_arg = nullptr) {
		if (count < static_cast<int>(std::size(entries))) {
			Q_strlcpy(entries[count].text, text, sizeof(entries[count].text));
			entries[count].align = align;
			entries[count].SelectFunc = func;
			if (text_arg) {
				Q_strlcpy(entries[count].text_arg1, text_arg, sizeof(entries[count].text_arg1));
			}
			count++;
		}
	};

	// Title
	add_entry("=== WEAPON UPGRADES ===", PMENU_ALIGN_CENTER);
	
	// Points display
	char points_buffer[64];
	G_FmtTo(points_buffer, "Points: {}", ent->client->pers.weapon_points);
	add_entry(points_buffer, PMENU_ALIGN_CENTER);
	
	// Separator
	add_entry("", PMENU_ALIGN_CENTER);

	// Show owned weapons first (with refund option)
	bool has_owned = false;
	for (size_t i = 0; i < g_benefitsData.NUM_BENEFITS && count < 55; ++i) {
		if (g_benefitsData.categories[i] != BenefitCategory::WEAPON) continue;

		BenefitID benefit_id = static_cast<BenefitID>(i);

		// Check if player has purchased this benefit
		if (ClassicPlayerHasPurchasedBenefit(ent, benefit_id)) {
			has_owned = true;
			int32_t refund_cost = GetBenefitCost(benefit_id);

			char buffer[128];
			G_FmtTo(buffer, "  > Refund: {} ({} pt{})", g_benefitsData.names[i], refund_cost, refund_cost > 1 ? "s" : "");

			char arg_buffer[64];
			snprintf(arg_buffer, sizeof(arg_buffer), "refund_%s", g_benefitsData.names[i]);

			add_entry(buffer, PMENU_ALIGN_LEFT, BonusWeaponsMenuHandler, arg_buffer);
		}
	}

	if (has_owned) {
		add_entry("---", PMENU_ALIGN_CENTER);
	}

	// Show available weapons to purchase
	bool has_available = false;
	for (size_t i = 0; i < g_benefitsData.NUM_BENEFITS && count < 60; ++i) {
		if (g_benefitsData.categories[i] != BenefitCategory::WEAPON) continue;

		BenefitID benefit_id = static_cast<BenefitID>(i);
		
		// Skip if already purchased
		if (ClassicPlayerHasPurchasedBenefit(ent, benefit_id)) {
			continue;
		}

		// Check prerequisites
		auto prereq = g_benefitsData.prerequisites[i];
		bool prereq_met = (prereq == BenefitID::NONE) || ClassicPlayerHasPurchasedBenefit(ent, prereq);
		
		if (!prereq_met) {
			continue;
		}

		has_available = true;
		
		// Set cost
		int32_t cost = 1;
		if (benefit_id == BenefitID::BFG_GRAV_PULL || benefit_id == BenefitID::CLUSTER_PROX) {
			cost = 3;
		}
		
		bool can_afford = ent->client->pers.weapon_points >= cost;
		
		char buffer[128];
		G_FmtTo(buffer, "  {} {} ({} pt{})", can_afford ? ">" : " ", g_benefitsData.names[i], cost, cost > 1 ? "s" : "");
		
		if (can_afford) {
			char arg_buffer[64];
			snprintf(arg_buffer, sizeof(arg_buffer), "purchase_%s", g_benefitsData.names[i]);
			add_entry(buffer, PMENU_ALIGN_LEFT, BonusWeaponsMenuHandler, arg_buffer);
		} else {
			add_entry(buffer, PMENU_ALIGN_LEFT);
		}
	}

	if (!has_available && !has_owned) {
		add_entry("No weapons available", PMENU_ALIGN_CENTER);
	}

	// Separator
	add_entry("", PMENU_ALIGN_CENTER);
	
	// Back option
	add_entry("< Back", PMENU_ALIGN_LEFT, BonusWeaponsMenuHandler, "back");

	PMenu_Open(ent, entries, cursor_position, count, nullptr, nullptr);
}

// ========================================================================
// BONUS MANAGEMENT MAIN MENU (Classic Mode)
// ========================================================================

void BonusManagementMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p || p->cur < 0 || p->cur >= p->num) {
		if (ent && ent->client && ent->client->menu)
			PMenu_Close(ent);
		return;
	}

	const pmenu_t* selected_entry = &p->entries[p->cur];
	const char* arg = selected_entry->text_arg1;

	if (!arg || arg[0] == '\0') {
		// No action - title or separator
		return;
	}

	if (strcmp(arg, "abilities") == 0) {
		PMenu_Close(ent);
		OpenBonusAbilitiesMenu(ent);
	}
	else if (strcmp(arg, "weapons") == 0) {
		PMenu_Close(ent);
		OpenBonusWeaponsMenu(ent);
	}
	else if (strcmp(arg, "reset") == 0) {
		BotRestoreAllBonusPoints(ent);
		gi.LocClient_Print(ent, PRINT_HIGH, "All bonus points restored!\n");
		PMenu_Close(ent);
		OpenBonusManagementMenu(ent);
	}
	else if (strcmp(arg, "toggle_auto_abilities") == 0) {
		bool was_enabled = ent->client->pers.auto_buy_benefit_bot;
		ent->client->pers.auto_buy_benefit_bot = !ent->client->pers.auto_buy_benefit_bot;

		// If disabling auto-buy for the first time, offer refund
		if (was_enabled && !ent->client->pers.auto_buy_benefit_bot &&
			!ent->client->pers.bot_has_manually_disabled_auto_buy) {
			BotRefundAutoPurchasedBenefits(ent);
		} else {
			gi.LocClient_Print(ent, PRINT_HIGH, "Auto-buy abilities: {}\n",
					  ent->client->pers.auto_buy_benefit_bot ? "ON" : "OFF");
		}
		PMenu_Close(ent);
		OpenBonusManagementMenu(ent);
	}
	else if (strcmp(arg, "toggle_auto_weapons") == 0) {
		bool was_enabled = ent->client->pers.auto_buy_benefit_weapons_bot;
		ent->client->pers.auto_buy_benefit_weapons_bot = !ent->client->pers.auto_buy_benefit_weapons_bot;

		// If disabling auto-buy for the first time, offer refund
		if (was_enabled && !ent->client->pers.auto_buy_benefit_weapons_bot &&
			!ent->client->pers.bot_has_manually_disabled_auto_buy) {
			BotRefundAutoPurchasedBenefits(ent);
		} else {
			gi.LocClient_Print(ent, PRINT_HIGH, "Auto-buy weapons: {}\n",
					  ent->client->pers.auto_buy_benefit_weapons_bot ? "ON" : "OFF");
		}
		PMenu_Close(ent);
		OpenBonusManagementMenu(ent);
	}
	else if (strcmp(arg, "back") == 0) {
		PMenu_Close(ent);
		OpenHordeMenu(ent);
	}
}

void OpenBonusManagementMenu(edict_t* ent, int cursor_position) {
	if (!ent || !ent->client) return;

	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	// Set menu protection
	ent->client->menu_protected = true;
	ent->client->menu_protection_start = level.time;

	static pmenu_t entries[32];
	for (auto& e : entries) e = {};
	int count = 0;

	auto add_entry = [&](const char* text, int align, SelectFunc_t func = nullptr, const char* text_arg = nullptr) {
		if (count < static_cast<int>(std::size(entries))) {
			Q_strlcpy(entries[count].text, text, sizeof(entries[count].text));
			entries[count].align = align;
			entries[count].SelectFunc = func;
			if (text_arg) {
				Q_strlcpy(entries[count].text_arg1, text_arg, sizeof(entries[count].text_arg1));
			}
			count++;
		}
	};

	// Title
	add_entry("=== BONUS MANAGEMENT ===", PMENU_ALIGN_CENTER);
	
	// Points display
	char buffer[128];
	G_FmtTo(buffer, "Ability Points: {}", ent->client->pers.ability_points);
	add_entry(buffer, PMENU_ALIGN_CENTER);
	
	G_FmtTo(buffer, "Weapon Points: {}", ent->client->pers.weapon_points);
	add_entry(buffer, PMENU_ALIGN_CENTER);
	
	// Separator
	add_entry("", PMENU_ALIGN_CENTER);

	// Menu options
	add_entry("> Abilities", PMENU_ALIGN_LEFT, BonusManagementMenuHandler, "abilities");
	add_entry("> Weapon Upgrades", PMENU_ALIGN_LEFT, BonusManagementMenuHandler, "weapons");
	add_entry("> Reset All Points", PMENU_ALIGN_LEFT, BonusManagementMenuHandler, "reset");
	
	// Auto-buy toggles
	G_FmtTo(buffer, "> Auto-buy Abilities: {}", ent->client->pers.auto_buy_benefit_bot ? "ON" : "OFF");
	add_entry(buffer, PMENU_ALIGN_LEFT, BonusManagementMenuHandler, "toggle_auto_abilities");

	G_FmtTo(buffer, "> Auto-buy Weapons: {}", ent->client->pers.auto_buy_benefit_weapons_bot ? "ON" : "OFF");
	add_entry(buffer, PMENU_ALIGN_LEFT, BonusManagementMenuHandler, "toggle_auto_weapons");
	
	// Separator
	add_entry("", PMENU_ALIGN_CENTER);
	
	// Back option
	add_entry("< Back", PMENU_ALIGN_LEFT, BonusManagementMenuHandler, "back");

	PMenu_Open(ent, entries, cursor_position, count, nullptr, nullptr);
}
