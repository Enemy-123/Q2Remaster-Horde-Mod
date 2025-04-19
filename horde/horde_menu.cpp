// --- START OF FILE horde_menu.cpp ---

#include "../g_local.h"  // Includes edict_t, gclient_t, gi, level, etc.
#include "../shared.h"  // For MAX_ITEMS, etc.
#include "../ctf/p_ctf_menu.h" // Menu system definitions and functions
#include "../ctf/g_ctf.h"      // For CTF functions like CTFObserver, CTFJoinTeam, CTFBeginElection, etc.
#include "g_horde.h"    // For GetMapSize
#include "g_horde_benefits.h"
#include "../laser.h"

constexpr const char* HORDE_MOD_VERSION_STRING = "Horde MOD BETA v0.0095";

// Forward Declarations from this file
void OpenVoteMenu(edict_t* ent);
void VoteMenuHandler(edict_t* ent, pmenuhnd_t* p);
void UpdateVoteMenu();
void ShowInventory(edict_t* ent);
void OpenHordeMenu(edict_t* ent);
void HordeMenuHandler(edict_t* ent, pmenuhnd_t* p);
pmenuhnd_t* CreateHordeMenu(edict_t* ent);
void OpenTechMenu(edict_t* ent);
void TechMenuHandler(edict_t* ent, pmenuhnd_t* p);
void OpenHUDMenu(edict_t* ent);
void UpdateHUDMenu(edict_t* ent, pmenuhnd_t* p);
void HUDMenuHandler(edict_t* ent, pmenuhnd_t* p);
void CheckAndUpdateMenus();
void OpenMapCategoryMenu(edict_t* ent);
void MapCategoryHandler(edict_t* ent, pmenuhnd_t* p);
void CategorizeMapList();
pmenuhnd_t* CreateHUDMenu(edict_t* ent);
void OpenMiscMenu(edict_t* ent); // Forward declare Misc menu functions
void MiscMenuHandler(edict_t* ent, pmenuhnd_t* p);

// Helper to get sentry type name
static const char* GetSentryTypeName(sentrytype_t type) {
	switch (type) {
	case SENTRY_HEATBEAM:    return "Beam/Phalanx";
	case SENTRY_MACHINEGUN:  return "Mg/Rocket";
	case SENTRY_FLECHETTE:   return "Flech/Grenade";
	case SENTRY_RANDOM:      // fallthrough
	default:                 return "Random";
	}
}

//--------------------------------
static void SetGameName(pmenu_t* p);
static void SetLevelName(pmenu_t* p);

void HordeJoinTeam(edict_t* ent, pmenuhnd_t* p); // Handler for Join Horde
void GoChaseCam(edict_t* ent, pmenuhnd_t* p);  // Handler for Chase Cam/Spectator

// Updated joinmenu with more spacing before Join/Spectate options
const pmenu_t joinmenu[] = {
	{ "*PLACEHOLDER*", PMENU_ALIGN_CENTER, nullptr },        // 0: Title (Set by SetGameName)
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 1: Blank Separator
	{ "*PLACEHOLDER*", PMENU_ALIGN_CENTER, nullptr },        // 2: Level Name (Set by SetLevelName)
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 3: Blank Separator
	// --- Add more blank entries for spacing ---
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 4: Blank
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 5: Blank
	// --- End extra spacing ---
	{ "Join and Fight the HORDE!", PMENU_ALIGN_LEFT, HordeJoinTeam }, // 6: Join Horde (Now lower)
	{ "", PMENU_ALIGN_LEFT, nullptr },                      // 7: Player Count (filled dynamically)
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 8: Blank Separator
	// --- Add more blank entries for spacing ---
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 9: Blank
	// --- End extra spacing ---
	{ "Go Spectator", PMENU_ALIGN_LEFT, GoChaseCam },     // 10: Go Spectator / Leave Chase (Now lower)
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 11: Blank Separator
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 12: Blank (Spacing)
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 13: Blank (Spacing)
	{ "Discord: Enemy0416", PMENU_ALIGN_CENTER, nullptr },    // 14: Discord Info
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 15: Blank Separator
	{ "", PMENU_ALIGN_LEFT, nullptr }                       // 16: Credits (filled dynamically)
};

// Recalculate size
constexpr size_t JOINMENU_SIZE = sizeof(joinmenu) / sizeof(pmenu_t); // Should be 17 now

// Update indices based on the NEW joinmenu structure
constexpr size_t JOINMENU_TITLE_IDX = 0;
constexpr size_t JOINMENU_LEVELNAME_IDX = 2;
constexpr size_t JOINMENU_JOIN_HORDE_IDX = 6; // New index
constexpr size_t JOINMENU_JOIN_HORDE_COUNT_IDX = 7; // New index
constexpr size_t JOINMENU_CHASECAM_IDX = 10; // New index
constexpr size_t JOINMENU_DISCORD_IDX = 14; // New index
constexpr size_t JOINMENU_CREDITS_IDX = 16; // New index

// Re-verify assertions
static_assert(JOINMENU_CREDITS_IDX < JOINMENU_SIZE, "JOINMENU_CREDITS_IDX is out of bounds for joinmenu");
static_assert(JOINMENU_DISCORD_IDX < JOINMENU_SIZE, "JOINMENU_DISCORD_IDX is out of bounds for joinmenu");
static_assert(JOINMENU_CHASECAM_IDX < JOINMENU_SIZE, "JOINMENU_CHASECAM_IDX is out of bounds for joinmenu");
static_assert(JOINMENU_JOIN_HORDE_COUNT_IDX < JOINMENU_SIZE, "JOINMENU_JOIN_HORDE_COUNT_IDX is out of bounds for joinmenu");

void HordeOpenJoinMenu(edict_t* ent)
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

	PMenu_Open(ent, joinmenu, team, sizeof(joinmenu) / sizeof(pmenu_t), nullptr, HordeUpdateJoinMenu);
}


void HordeUpdateJoinMenu(edict_t* ent)
{
	// --- Safety Checks ---
	if (!ent || !ent->client || !ent->client->menu || !ent->client->menu->entries)
	{
		gi.Com_Print("Warning: HordeUpdateJoinMenu called with invalid ent/client/menu.\n");
		return;
	}
	// Check if the menu size matches what we expect
	if (ent->client->menu->num != JOINMENU_SIZE) {
		gi.Com_PrintFmt("Warning: HordeUpdateJoinMenu - menu size mismatch (expected {}, got {}).\n", JOINMENU_SIZE, ent->client->menu->num);
		// Optionally close the menu or return to prevent potential crashes
		// PMenu_Close(ent);
		return;
	}


	pmenu_t* entries = ent->client->menu->entries;

	// --- Update Static/Common Entries ---
	SetGameName(&entries[JOINMENU_TITLE_IDX]);       // Update Game Title
	SetLevelName(&entries[JOINMENU_LEVELNAME_IDX]);  // Update Level Name

	// --- Horde Specific Logic ---
	if (g_horde->integer) // Check if Horde mode is active
	{
		// Set "Join Horde" option text and handler
		Q_strlcpy(entries[JOINMENU_JOIN_HORDE_IDX].text, "Join and Fight the HORDE!", sizeof(entries[JOINMENU_JOIN_HORDE_IDX].text));
		entries[JOINMENU_JOIN_HORDE_IDX].SelectFunc = HordeJoinTeam;

		// Set Credits text
		Q_strlcpy(entries[JOINMENU_CREDITS_IDX].text, "Original Mod by Paril.\nModified by Enemy.", sizeof(entries[JOINMENU_CREDITS_IDX].text));
		entries[JOINMENU_CREDITS_IDX].SelectFunc = nullptr;

		// Set Discord Text (ensure it's visible)
		Q_strlcpy(entries[JOINMENU_DISCORD_IDX].text, "Discord: Enemy0416", sizeof(entries[JOINMENU_DISCORD_IDX].text));
		entries[JOINMENU_DISCORD_IDX].SelectFunc = nullptr;


		// --- Update Player Count (Optimized) ---
		uint32_t horde_player_count = 0;
		for (const auto* player_ent : active_players()) {
			if (player_ent->client->resp.ctf_team == CTF_TEAM1) {
				horde_player_count++;
			}
		}

		// Update the player count display entry
		Q_strlcpy(entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text, "$g_pc_playercount", sizeof(entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text));
		// *** FIX: Removed the sizeof argument from G_FmtTo ***
		G_FmtTo(entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text_arg1, "{}", horde_player_count);

	}
	else // Not Horde mode
	{
		// Disable/Clear Horde-specific entries
		Q_strlcpy(entries[JOINMENU_JOIN_HORDE_IDX].text, "(Horde Mode Disabled)", sizeof(entries[JOINMENU_JOIN_HORDE_IDX].text));
		entries[JOINMENU_JOIN_HORDE_IDX].SelectFunc = nullptr;
		entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text[0] = '\0';
		entries[JOINMENU_JOIN_HORDE_COUNT_IDX].text_arg1[0] = '\0';
		entries[JOINMENU_CREDITS_IDX].text[0] = '\0';
		entries[JOINMENU_CREDITS_IDX].SelectFunc = nullptr;
		// Clear Discord info if not in Horde mode
		entries[JOINMENU_DISCORD_IDX].text[0] = '\0';
		entries[JOINMENU_DISCORD_IDX].SelectFunc = nullptr;
	}


	// --- Update Chase Cam / Spectator Option ---
	const char* chase_text = ent->client->chase_target ?
		"$g_pc_leave_chase_camera" :
		"Go Spectator";
	Q_strlcpy(entries[JOINMENU_CHASECAM_IDX].text, chase_text, sizeof(entries[JOINMENU_CHASECAM_IDX].text));
	entries[JOINMENU_CHASECAM_IDX].SelectFunc = GoChaseCam;

	// Ensure other blank entries remain blank (already handled by array definition)
}

// Keep the SetGameName and SetLevelName definitions as they were in the previous correct version.
// Definition of SetGameName
static void SetGameName(pmenu_t* p)
{
	// Safety check
	if (!p) return;

	if (ctf->integer) // Check if CTF mode is active
		Q_strlcpy(p->text, "$g_pc_3wctf", sizeof(p->text)); // Use localized CTF name
	else // Assume Horde or other modes
		// Use the constexpr string defined in g_horde.h
		Q_strlcpy(p->text, HORDE_MOD_VERSION_STRING, sizeof(p->text));
}


// Definition of SetLevelName
static void SetLevelName(pmenu_t* p)
{
	// Safety check
	if (!p) return;

	static char levelname[sizeof(pmenu_t::text)]; // Use size of destination buffer

	levelname[0] = '*'; // Prefix with '*' for centered title look
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
	{ "*Map Voting Menu", PMENU_ALIGN_CENTER, nullptr }, // Title (updated by category)
	{ "", PMENU_ALIGN_CENTER, nullptr },                 // Blank separator
	// --- Map entries start here (index 2 to 2 + MAX_MAPS_PER_PAGE - 1) ---
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
	// --- Map entries end here ---
	{ "", PMENU_ALIGN_CENTER, nullptr },                 // Blank separator (index 2 + MAX_MAPS_PER_PAGE)
	{ "Next", PMENU_ALIGN_LEFT, VoteMenuHandler },       // Navigation (index 3 + MAX_MAPS_PER_PAGE)
	{ "Back", PMENU_ALIGN_LEFT, VoteMenuHandler },       // Navigation (index 4 + MAX_MAPS_PER_PAGE)
	{ "Close", PMENU_ALIGN_LEFT, VoteMenuHandler }       // Close (index 5 + MAX_MAPS_PER_PAGE)
};

// --- Map List Categorization ---

struct map_lists_t {
	std::vector<std::string> big_maps;
	std::vector<std::string> medium_maps;
	std::vector<std::string> small_maps;
	size_t current_page = 0;
	horde::MapSize current_category = { false, true, false }; // Default to medium
};

static map_lists_t categorized_maps;

// Function to categorize the maps based on g_map_list cvar
void CategorizeMapList() {
	categorized_maps.big_maps.clear();
	categorized_maps.medium_maps.clear();
	categorized_maps.small_maps.clear();

	const char* mlist = g_map_list->string;
	if (!mlist) return; // Safety check

	char* token;
	// Use a local copy to avoid modifying the cvar string directly
	std::string mlist_copy = mlist;
	const char* mlist_ptr = mlist_copy.c_str();


	while (*(token = COM_Parse(&mlist_ptr)) != '\0') {
		// Check if token is empty, can happen with consecutive spaces
		if (token[0] == '\0') continue;

		const char* map_name = token;
		horde::MapSize const mapSize = GetMapSize(map_name); // Assuming GetMapSize is safe

		if (mapSize.isBigMap) {
			categorized_maps.big_maps.push_back(map_name);
		}
		else if (mapSize.isSmallMap) {
			categorized_maps.small_maps.push_back(map_name);
		}
		else { // isMediumMap or unknown defaults to medium
			categorized_maps.medium_maps.push_back(map_name);
		}
	}
}


// --- Map Category Selection Menu ---

// Add a placeholder for the current map name
static pmenu_t map_category_menu[] = {
	{ "Map Category Selection", PMENU_ALIGN_CENTER, nullptr }, // 0: Title
	{ "", PMENU_ALIGN_CENTER, nullptr },                       // 1: Blank Separator
	{ "*PLACEHOLDER_MAP*", PMENU_ALIGN_CENTER, nullptr },      // 2: Current Map (Set Dynamically)
	{ "", PMENU_ALIGN_CENTER, nullptr },                       // 3: Blank Separator
	{ "Big Maps", PMENU_ALIGN_LEFT, MapCategoryHandler },      // 4: Big Maps (was 2)
	{ "Medium Maps", PMENU_ALIGN_LEFT, MapCategoryHandler },   // 5: Medium Maps (was 3)
	{ "Small Maps", PMENU_ALIGN_LEFT, MapCategoryHandler },    // 6: Small Maps (was 4)
	{ "", PMENU_ALIGN_CENTER, nullptr },                       // 7: Blank Separator (was 5)
	{ "Back to Horde Menu", PMENU_ALIGN_LEFT, MapCategoryHandler }, // 8: Back (was 6)
	{ "Close", PMENU_ALIGN_LEFT, MapCategoryHandler }          // 9: Close (was 7)
};
// Update size
constexpr size_t MAP_CATEGORY_MENU_SIZE = sizeof(map_category_menu) / sizeof(pmenu_t); // Now 10

// Define indices for clarity (optional but helpful)
//constexpr size_t MAP_CAT_MENU_TITLE_IDX = 0;
constexpr size_t MAP_CAT_MENU_CURRENT_MAP_IDX = 2;
constexpr size_t MAP_CAT_MENU_BIG_IDX = 4;
constexpr size_t MAP_CAT_MENU_MEDIUM_IDX = 5;
constexpr size_t MAP_CAT_MENU_SMALL_IDX = 6;
constexpr size_t MAP_CAT_MENU_BACK_IDX = 8;
constexpr size_t MAP_CAT_MENU_CLOSE_IDX = 9;

// Handler for the map category selection menu
void MapCategoryHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p) {
		return;
	}

	const int option = p->cur;

	PMenu_Close(ent); // Close the category menu first

	// Use the new indices defined above (or hardcode the updated numbers)
	switch (option) {
	case MAP_CAT_MENU_BIG_IDX: // Big Maps (Now 4)
		// *** FIX: Set category correctly for Big Maps ***
		categorized_maps.current_category = horde::MapSize{ false, true, false }; // {isSmall=false, isBig=true, isMedium=false}
		categorized_maps.current_page = 0;
		UpdateVoteMenu(); // Update vote menu data based on new category
		PMenu_Open(ent, vote_menu, -1, VOTE_MENU_SIZE, nullptr, nullptr); // Open the map list
		break;

	case MAP_CAT_MENU_MEDIUM_IDX: // Medium Maps (Now 5)
		// *** FIX: Set category correctly for Medium Maps ***
		categorized_maps.current_category = horde::MapSize{ false, false, true }; // {isSmall=false, isBig=false, isMedium=true}
		categorized_maps.current_page = 0;
		UpdateVoteMenu();
		PMenu_Open(ent, vote_menu, -1, VOTE_MENU_SIZE, nullptr, nullptr);
		break;

	case MAP_CAT_MENU_SMALL_IDX: // Small Maps (Now 6)
		// This one was already correct
		categorized_maps.current_category = horde::MapSize{ true, false, false }; // {isSmall=true, isBig=false, isMedium=false}
		categorized_maps.current_page = 0;
		UpdateVoteMenu();
		PMenu_Open(ent, vote_menu, -1, VOTE_MENU_SIZE, nullptr, nullptr);
		break;

	case MAP_CAT_MENU_BACK_IDX: // Back to Horde Menu (Now 8)
		OpenHordeMenu(ent); // Open the main menu
		break;

	case MAP_CAT_MENU_CLOSE_IDX: // Close (Now 9)
		// Menu already closed at the start
		break;

	default:
		// Ignore selections on title, separators, or current map display
		// Menu already closed
		break;
	}
}// Opens the map category selection menu
// Opens the map category selection menu
void OpenMapCategoryMenu(edict_t* ent) {
	if (!ent || !ent->client) {
		return;
	}

	// Close any existing menu first
	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	// --- Dynamically set the current map name ---
	char current_map_text[sizeof(pmenu_t::text)]; // Use size of destination buffer
	if (level.mapname && *level.mapname) {
		snprintf(current_map_text, sizeof(current_map_text), "Current: %s", level.mapname);
	}
	else {
		snprintf(current_map_text, sizeof(current_map_text), "Current: Unknown*");
	}
	// Ensure null termination just in case snprintf fills the buffer completely
	current_map_text[sizeof(current_map_text) - 1] = '\0';

	// Copy the formatted string into the static menu array entry
	// Use the defined index for safety
	Q_strlcpy(map_category_menu[MAP_CAT_MENU_CURRENT_MAP_IDX].text,
		current_map_text,
		sizeof(map_category_menu[MAP_CAT_MENU_CURRENT_MAP_IDX].text));
	// Ensure this entry is not selectable
	map_category_menu[MAP_CAT_MENU_CURRENT_MAP_IDX].SelectFunc = nullptr;
	map_category_menu[MAP_CAT_MENU_CURRENT_MAP_IDX].align = PMENU_ALIGN_CENTER;
	// --- End dynamic map name setting ---


	// Open the category menu (using the updated static array)
	PMenu_Open(ent, map_category_menu, -1, MAP_CATEGORY_MENU_SIZE, nullptr, nullptr);
}

// --- Map Voting Logic ---

// Opens the initial map category selection menu
void OpenVoteMenu(edict_t* ent) {
	if (!ent || !ent->client) {
		return;
	}
	CategorizeMapList(); // Ensure maps are categorized before opening the menu
	OpenMapCategoryMenu(ent); // Start with category selection
}

// Handler for map selection and navigation within the vote menu
void VoteMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	// Input validation
	if (!ent || !ent->client || !p || p->cur < 0) {
		if (ent && ent->client && ent->client->menu) PMenu_Close(ent);
		return;
	}

	const size_t option = p->cur;
	std::vector<std::string>* current_map_list = nullptr;

	// Get current map list based on category
	if (categorized_maps.current_category.isBigMap) {
		current_map_list = &categorized_maps.big_maps;
	}
	else if (categorized_maps.current_category.isSmallMap) {
		current_map_list = &categorized_maps.small_maps;
	}
	else { // Medium or default
		current_map_list = &categorized_maps.medium_maps;
	}

	// Validate map list
	if (!current_map_list || current_map_list->empty()) {
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
	if (option >= map_section_start && option < map_section_end) {
		// Calculate map index based on current page and selection
		const size_t page_offset = categorized_maps.current_page * MAX_MAPS_PER_PAGE;
		const size_t map_index = page_offset + (option - map_section_start);

		// Validate map index and menu entry text
		if (map_index >= current_map_list->size() || vote_menu[option].text[0] == '\0') {
			// Invalid selection (out of bounds or empty slot), do nothing or maybe close menu?
			return;
		}

		const std::string& map_name = (*current_map_list)[map_index];

		// Check if it's the current map
		if (level.mapname && Q_strcasecmp(map_name.c_str(), level.mapname) == 0) {
			gi.LocClient_Print(ent, PRINT_HIGH, "Can't vote for the current map.\n");
			return; // Stay in the menu
		}

		// Initiate vote
		Q_strlcpy(ctfgame.elevel, map_name.c_str(), sizeof(ctfgame.elevel));

		char vote_msg[128]; // Safe buffer for vote message
		snprintf(vote_msg, sizeof(vote_msg), "Change map to %s?", map_name.c_str());

		// Close menu *before* starting election if successful
		if (CTFBeginElection(ent, ELECT_MAP, vote_msg)) {
			PMenu_Close(ent);
		}
		// If election failed (e.g., already in progress), menu remains open
		return;
	}

	// Handle navigation buttons
	PMenu_Close(ent); // Close current menu before navigating

	if (option == next_button_index) { // Next Page
		// Check if there is a next page
		if ((categorized_maps.current_page + 1) * MAX_MAPS_PER_PAGE < current_map_list->size()) {
			categorized_maps.current_page++;
			UpdateVoteMenu(); // Update the menu data
			PMenu_Open(ent, vote_menu, -1, VOTE_MENU_SIZE, nullptr, nullptr); // Reopen the updated vote menu
		}
		else {
			// No next page, maybe reopen the current one? Or handle differently?
			// For now, just re-open the current (last) page
			UpdateVoteMenu();
			PMenu_Open(ent, vote_menu, -1, VOTE_MENU_SIZE, nullptr, nullptr);
		}
	}
	else if (option == back_button_index) { // Back to Categories
		OpenMapCategoryMenu(ent); // Go back to category selection
	}
	else if (option == close_button_index) { // Close
		// Menu already closed
	}
	else {
		// Invalid option outside map list and nav buttons
		// Menu already closed
	}
}

// Updates the contents of the static vote_menu array based on the current category and page
void UpdateVoteMenu() {
	std::vector<std::string>* current_map_list = nullptr;

	// Determine current map list and category name
	const char* category_name;
	if (categorized_maps.current_category.isBigMap) {
		current_map_list = &categorized_maps.big_maps;
		category_name = "Big Maps";
	}
	else if (categorized_maps.current_category.isSmallMap) {
		current_map_list = &categorized_maps.small_maps;
		category_name = "Small Maps";
	}
	else { // Medium or default
		current_map_list = &categorized_maps.medium_maps;
		category_name = "Medium Maps";
	}

	// Safety check for empty list
	if (!current_map_list) return; // Should not happen if CategorizeMapList was called

	// Update category title (index 0)
	char category_title[64];
	snprintf(category_title, sizeof(category_title), "%s", category_name);
	Q_strlcpy(vote_menu[0].text, category_title, sizeof(vote_menu[0].text));

	// --- Calculate map indices for the current page ---
	size_t const num_maps = current_map_list->size();
	size_t start_index = categorized_maps.current_page * MAX_MAPS_PER_PAGE;

	// Handle potential invalid page (e.g., if map list changed)
	if (start_index >= num_maps && num_maps > 0) {
		categorized_maps.current_page = (num_maps - 1) / MAX_MAPS_PER_PAGE;
		start_index = categorized_maps.current_page * MAX_MAPS_PER_PAGE;
	}
	else if (num_maps == 0) {
		start_index = 0; // No maps, start index is 0
	}

	size_t end_index = std::min(start_index + MAX_MAPS_PER_PAGE, num_maps);

	// --- Clear/Fill map entries (indices 2 to 2 + MAX_MAPS_PER_PAGE - 1) ---
	size_t const map_section_start = 2;
	for (size_t i = 0; i < MAX_MAPS_PER_PAGE; ++i) {
		size_t const current_map_idx = start_index + i;
		size_t const menu_idx = map_section_start + i;

		if (current_map_idx < end_index) {
			// Fill entry
			Q_strlcpy(vote_menu[menu_idx].text, (*current_map_list)[current_map_idx].c_str(), sizeof(vote_menu[menu_idx].text));
			vote_menu[menu_idx].SelectFunc = VoteMenuHandler;
		}
		else {
			// Clear entry
			vote_menu[menu_idx].text[0] = '\0';
			vote_menu[menu_idx].SelectFunc = nullptr;
		}
	}

	// --- Update navigation buttons ---
	size_t const nav_button_start_index = map_section_start + MAX_MAPS_PER_PAGE + 1; // Index after maps and blank

	// "Next" button (index nav_button_start_index)
	bool const has_next_page = (start_index + MAX_MAPS_PER_PAGE) < num_maps;
	if (has_next_page) {
		Q_strlcpy(vote_menu[nav_button_start_index].text, "Next", sizeof(vote_menu[nav_button_start_index].text));
		vote_menu[nav_button_start_index].SelectFunc = VoteMenuHandler;
	}
	else {
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

void ShowInventory(edict_t* ent) {
	// Basic validation
	if (!ent || !ent->client || (ent->svflags & SVF_BOT))
		return;

	gclient_t* cl = ent->client;
	cl->showinventory = true; // Should this be set before sending? Maybe not needed if client handles it.

	gi.WriteByte(svc_inventory);

	// Write current inventory counts up to IT_TOTAL
	for (int i = 0; i < IT_TOTAL; i++) {
		// Ensure index is valid for pers.inventory
		if (i < 0 || i >= std::size(cl->pers.inventory)) {
			gi.WriteShort(0); // Write 0 for invalid indices
			continue;
		}
		gi.WriteShort(cl->pers.inventory[i]);
	}

	// Pad the rest up to MAX_ITEMS with zeros
	// Use a safer loop condition based on IT_TOTAL and MAX_ITEMS
	for (int i = IT_TOTAL; i < MAX_ITEMS; i++) {
		gi.WriteShort(0);
	}

	gi.unicast(ent, true);
}

// === Tech Menu ===

// Define the TECHS available
static const char* tech_names[] = {
	"Strength",
	"Haste",
	"Regeneration",
	"Resistance"
};
constexpr size_t NUM_TECHS = sizeof(tech_names) / sizeof(tech_names[0]);

// Menu definition for when player is already in a team
static pmenu_t tech_menu[] = {
	{ "*Tech Selection", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "Strength", PMENU_ALIGN_LEFT, TechMenuHandler },
	{ "Haste", PMENU_ALIGN_LEFT, TechMenuHandler },
	{ "Regeneration", PMENU_ALIGN_LEFT, TechMenuHandler },
	{ "Resistance", PMENU_ALIGN_LEFT, TechMenuHandler }
};
constexpr size_t TECH_MENU_SIZE = sizeof(tech_menu) / sizeof(pmenu_t);

// Menu definition for when player is joining (CTF_NOTEAM)
static pmenu_t tech_menustart[] = {
	{ "*Tech Selection", PMENU_ALIGN_CENTER, nullptr },          // 0
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 1
	{ "Select a TECH:", PMENU_ALIGN_LEFT, nullptr },          // 2
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 3
	{ "Strength", PMENU_ALIGN_LEFT, TechMenuHandler },      // 4
	{ "Haste", PMENU_ALIGN_LEFT, TechMenuHandler },          // 5
	{ "Regeneration", PMENU_ALIGN_LEFT, TechMenuHandler },  // 6
	{ "Resistance", PMENU_ALIGN_LEFT, TechMenuHandler },    // 7
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 8
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 9
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 10
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 11
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 12
	{ "", PMENU_ALIGN_CENTER, nullptr },                      // 13
	{ "You can change it later", PMENU_ALIGN_LEFT, nullptr }, // 14
	{ "On Horde Menu", PMENU_ALIGN_CENTER, nullptr },         // 15
};
constexpr size_t TECH_MENU_START_SIZE = sizeof(tech_menustart) / sizeof(pmenu_t);

// Opens the appropriate tech menu
void OpenTechMenu(edict_t* ent) {
	if (!ent || !ent->client) {
		return;
	}

	// Always close existing menu first
	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	// Select menu based on team status
	const bool is_joining = (ent->client->resp.ctf_team == CTF_NOTEAM);
	const pmenu_t* menu_to_open = is_joining ? tech_menustart : tech_menu;
	const size_t menu_size = is_joining ? TECH_MENU_START_SIZE : TECH_MENU_SIZE;

	PMenu_Open(ent, menu_to_open, -1, menu_size, nullptr, nullptr);
}

// Handles selection in the tech menus
void TechMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p) {
		return;
	}

	const bool is_joining = (ent->client->resp.ctf_team == CTF_NOTEAM);
	const int tech_option_offset = is_joining ? 4 : 2; // Starting index of tech options
	const int option = p->cur - tech_option_offset;

	// Validate selected option index
	if (option >= 0 && option < static_cast<int>(NUM_TECHS)) {
		// Remove existing TECHS
		RemoveTech(ent); // Assuming RemoveTech handles removing *all* techs

		// Map the selected option text to the correct internal item ID
		int tech_index = -1;
		const char* selected_tech_name = tech_names[option];

		if (strcmp(selected_tech_name, "Strength") == 0) {
			tech_index = IT_TECH_STRENGTH;
		}
		else if (strcmp(selected_tech_name, "Haste") == 0) {
			tech_index = IT_TECH_HASTE;
		}
		else if (strcmp(selected_tech_name, "Regeneration") == 0) {
			tech_index = IT_TECH_REGENERATION;
		}
		else if (strcmp(selected_tech_name, "Resistance") == 0) {
			tech_index = IT_TECH_RESISTANCE;
		}

		// If a valid tech was selected
		if (tech_index != -1) {
			// Give the player the tech
			ent->client->pers.inventory[tech_index] = 1;
			gi.LocCenter_Print(ent, "\n\n\n\nSelected Tech: {}\n", selected_tech_name);

			// Join team if player was joining, and apply tech effect/sound
			if (is_joining) {
				CTFJoinTeam(ent, CTF_TEAM1); // Automatically join team 1
			}

			// Apply sound/effect based on the tech chosen
			switch (tech_index) {
			case IT_TECH_HASTE:       CTFApplyHasteSound(ent); break;
			case IT_TECH_STRENGTH:    CTFApplyStrengthSound(ent); break;
			case IT_TECH_REGENERATION: CTFApplyRegeneration(ent); break;
			case IT_TECH_RESISTANCE:  CTFApplyResistance(ent, 0); break; // ApplyResistance might need a value? Assuming 0 is base.
			}
		}
	}

	PMenu_Close(ent); // Close the tech menu after selection
}


// === Misc Options Menu ===

// Handler for cycling Sentry Gun choice
void HordeMenu_SentryChoice(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client) {
		return;
	}

	// Cycle to the next choice
	int current_choice = static_cast<int>(ent->client->pers.sentry_gun_choice);
	current_choice = (current_choice + 1) % SENTRY_TYPE_COUNT;
	ent->client->pers.sentry_gun_choice = static_cast<sentrytype_t>(current_choice);
	ent->client->resp.sentry_gun_choice = static_cast<sentrytype_t>(current_choice); // Persist choice

	// Inform the player
	gi.LocCenter_Print(ent, "Sentrygun Type set to: {}\n", GetSentryTypeName(ent->client->pers.sentry_gun_choice));

	// Update the menu display immediately by reopening THE MISC MENU
	// PMenu_Close is handled by OpenMiscMenu
	OpenMiscMenu(ent); // Reopen the Misc menu to show the updated choice
}

// Handler for the Misc submenu
void MiscMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p || p->cur < 0 || p->cur >= p->num) {
		if (ent && ent->client && ent->client->menu) PMenu_Close(ent);
		return;
	}

	const pmenu_t* selected_entry = &p->entries[p->cur];
	const char* selected_text = selected_entry->text;
	bool shouldCloseMenu = true; // Default to closing the menu

	// Use strncmp for options that might have counts appended or dynamic text
	if (strncmp(selected_text, "Remove Lasers", strlen("Remove Lasers")) == 0) {
		Cmd_RemoveLaser_f(ent);
		// Message handled internally, menu should close.
	}
	else if (strncmp(selected_text, "Remove Sentry Gun", strlen("Remove Sentry Gun")) == 0) {
		Cmd_RemoveSentry_f(ent);
		// Message handled internally, menu should close.
	}
	// **** Check Sentry Type selection ****
	else if (strncmp(selected_text, "Sentry Type:", strlen("Sentry Type:")) == 0) {
		HordeMenu_SentryChoice(ent, p); // Call the dedicated handler
		shouldCloseMenu = false; // Don't close, HordeMenu_SentryChoice will reopen Misc Menu
	}
	// **** END Check ****
	else if (strcmp(selected_text, "Back") == 0) {
		PMenu_Close(ent);
		OpenHordeMenu(ent); // Go back to the main menu
		shouldCloseMenu = false; // Already handled closing/opening
	}
	else if (strcmp(selected_text, "Close") == 0) {
		// No specific action, default close behavior is fine.
	}
	else {
		// Clicked on title or separator - don't close
		shouldCloseMenu = false;
	}

	// Close the menu if required and if it still exists
	if (shouldCloseMenu && ent->client && ent->client->menu) {
		PMenu_Close(ent);
	}
}

// Creates and opens the Misc submenu
void OpenMiscMenu(edict_t* ent) {
	if (!ent || !ent->client) {
		return;
	}

	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	// Increase buffer size slightly for the new entry + potential separator
	static pmenu_t entries[12]; // Increased size should be safe
	memset(entries, 0, sizeof(entries));
	int count = 0;

	// Helper lambda remains the same
	auto add_entry = [&](const char* text, int align, SelectFunc_t func = nullptr) {
		if (count < static_cast<int>(std::size(entries))) {
			Q_strlcpy(entries[count].text, text, sizeof(entries[count].text));
			entries[count].align = align;
			entries[count].SelectFunc = func;
			count++;
		}
		else {
			gi.Com_Print("Warning: OpenMiscMenu exceeded static entry buffer size.\n");
		}
		};

	add_entry("*Misc Options*", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER); // Separator

	// --- ALWAYS Add Sentry Gun Choice FIRST ---
	char sentry_choice_text[64];
	snprintf(sentry_choice_text, sizeof(sentry_choice_text), "Sentry Type: [%s]",
		GetSentryTypeName(ent->client->pers.sentry_gun_choice));
	// Use MiscMenuHandler here, it will call HordeMenu_SentryChoice internally
	add_entry(sentry_choice_text, PMENU_ALIGN_LEFT, MiscMenuHandler);
	// --- END ALWAYS ---

	// --- Conditional Remove Options ---
	int laser_count = 0;
	if (auto* manager = LaserHelpers::get_laser_manager(ent)) {
		laser_count = manager->get_active_count();
	}
	if (laser_count > 0) {
		char remove_laser_text[64];
		snprintf(remove_laser_text, sizeof(remove_laser_text), "Remove Lasers (%d)", laser_count);
		add_entry(remove_laser_text, PMENU_ALIGN_LEFT, MiscMenuHandler);
	}

	int sentry_count = ent->client->num_sentries;
	if (sentry_count > 0) {
		char remove_sentry_text[64];
		snprintf(remove_sentry_text, sizeof(remove_sentry_text), "Remove Sentry Gun (%d)", sentry_count);
		add_entry(remove_sentry_text, PMENU_ALIGN_LEFT, MiscMenuHandler);
	}
	// --- END Conditional Remove Options ---

	add_entry("", PMENU_ALIGN_CENTER); // Separator
	add_entry("Back", PMENU_ALIGN_LEFT, MiscMenuHandler);
	add_entry("Close", PMENU_ALIGN_LEFT, MiscMenuHandler);

	PMenu_Open(ent, entries, -1, count, nullptr, nullptr); // Use the dynamic count
}

// === HUD Options Menu ===

constexpr size_t HUD_MENU_MAX_ENTRIES = 8;
constexpr size_t HUD_TEXT_BUFFER_SIZE = 64;

// Opens the HUD options menu
void OpenHUDMenu(edict_t* ent) {
	if (!ent || !ent->client) {
		return;
	}

	// Close any existing menu first
	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	// Use CreateHUDMenu to generate and open the menu
	CreateHUDMenu(ent);
}

// Creates and returns the HUD menu handle (used by OpenHUDMenu and HUDMenuHandler)
pmenuhnd_t* CreateHUDMenu(edict_t* ent) {
	// Static allocation for menu entries
	static pmenu_t entries[HUD_MENU_MAX_ENTRIES];
	memset(entries, 0, sizeof(entries)); // Zero initialize for safety

	// Safe buffer sizes for formatting menu text
	char id_text[HUD_TEXT_BUFFER_SIZE];
	char dmg_text[HUD_TEXT_BUFFER_SIZE];

	// Format option text safely using snprintf
	snprintf(id_text, sizeof(id_text), "Enable/Disable ID [%s]",
		ent->client->pers.id_state ? "ON" : "OFF");
	snprintf(dmg_text, sizeof(dmg_text), "Enable/Disable ID-DMG [%s]",
		ent->client->pers.iddmg_state ? "ON" : "OFF");

	// Build menu entries with bounds checking
	size_t count = 0;

	// Title and spacing
	if (count < HUD_MENU_MAX_ENTRIES) {
		Q_strlcpy(entries[count].text, "*HUD Options", sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_CENTER;
		entries[count++].SelectFunc = nullptr;
	}
	if (count < HUD_MENU_MAX_ENTRIES) {
		entries[count].text[0] = '\0'; // Blank separator
		entries[count].align = PMENU_ALIGN_CENTER;
		entries[count++].SelectFunc = nullptr;
	}

	// ID Toggle
	if (count < HUD_MENU_MAX_ENTRIES) {
		Q_strlcpy(entries[count].text, id_text, sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// ID-DMG Toggle
	if (count < HUD_MENU_MAX_ENTRIES) {
		Q_strlcpy(entries[count].text, dmg_text, sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// Spacing
	if (count < HUD_MENU_MAX_ENTRIES) {
		entries[count].text[0] = '\0';
		entries[count].align = PMENU_ALIGN_CENTER;
		entries[count++].SelectFunc = nullptr;
	}

	// Back to Horde Menu
	if (count < HUD_MENU_MAX_ENTRIES) {
		Q_strlcpy(entries[count].text, "Back to Horde Menu", sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// Close
	if (count < HUD_MENU_MAX_ENTRIES) {
		Q_strlcpy(entries[count].text, "Close", sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// Open menu and return handle
	return PMenu_Open(ent, entries, -1, count, nullptr, nullptr);
}

// Updates the dynamic text in the HUD menu (ON/OFF status)
void UpdateHUDMenu(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p || !p->entries || p->num < 4) { // Need at least 4 entries for title, blank, ID, IDDMG
		return;
	}

	// Safe buffers for status text
	char id_status_text[HUD_TEXT_BUFFER_SIZE];
	char dmg_status_text[HUD_TEXT_BUFFER_SIZE];

	// Format status text safely
	snprintf(id_status_text, sizeof(id_status_text), "Enable/Disable ID [%s]",
		ent->client->pers.id_state ? "ON" : "OFF");
	snprintf(dmg_status_text, sizeof(dmg_status_text), "Enable/Disable ID-DMG [%s]",
		ent->client->pers.iddmg_state ? "ON" : "OFF");

	// Update the menu entries (assuming indices 2 and 3 are ID and ID-DMG toggles)
	PMenu_UpdateEntry(p->entries + 2, id_status_text, PMENU_ALIGN_LEFT, HUDMenuHandler);
	PMenu_UpdateEntry(p->entries + 3, dmg_status_text, PMENU_ALIGN_LEFT, HUDMenuHandler);

	// Force the menu to redraw on the client
	PMenu_Update(ent);
}

// Handles selections in the HUD options menu
void HUDMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p || p->cur < 0) {
		if (ent && ent->client && ent->client->menu) PMenu_Close(ent);
		return;
	}

	// Define valid option range based on the created menu structure
	constexpr int ID_TOGGLE_OPTION = 2;
	constexpr int IDDMG_TOGGLE_OPTION = 3;
	constexpr int BACK_OPTION = 5;
	constexpr int CLOSE_OPTION = 6;

	const int option = p->cur;

	switch (option) {
	case ID_TOGGLE_OPTION: // Toggle ID
	case IDDMG_TOGGLE_OPTION: { // Toggle ID-DMG
		bool& state_to_toggle = (option == ID_TOGGLE_OPTION) ?
			ent->client->pers.id_state :
			ent->client->pers.iddmg_state;
		state_to_toggle = !state_to_toggle;

		// Use snprintf for safe message formatting
		char msg_buffer[128];
		snprintf(msg_buffer, sizeof(msg_buffer), "\n\n\n%s state toggled to %s\n",
			(option == ID_TOGGLE_OPTION) ? "ID" : "ID-DMG",
			state_to_toggle ? "ON" : "OFF");

		gi.LocCenter_Print(ent, msg_buffer);
		UpdateHUDMenu(ent, p); // Update the menu display without closing it
		break;
	}
	case BACK_OPTION: // Back to Horde Menu
		PMenu_Close(ent);
		if (ent->inuse) { // Extra safety check
			OpenHordeMenu(ent);
		}
		break;
	case CLOSE_OPTION: // Close
		PMenu_Close(ent);
		break;
	default: // Invalid option
		PMenu_Close(ent);
		break;
	}
}

// Periodically checks if players have the HUD menu open and updates it
void CheckAndUpdateMenus() {
	for (auto const* player : active_players()) { // Use range-based for loop
		// Basic validation
		if (!player || !player->client || !player->client->menu || !player->client->menu->entries) {
			continue;
		}

		// Check if the player is in the HUD menu by comparing the title text
		// Using strcmp for C-style string comparison
		if (strcmp(player->client->menu->entries[0].text, "*HUD Options") == 0) {
			UpdateHUDMenu(const_cast<edict_t*>(player), player->client->menu); // Pass non-const edict_t
			// gi.unicast(const_cast<edict_t*>(player), true); // Update might already handle this
		}
		// Add checks for other dynamic menus if needed
	}
}


// === Main Horde Menu ===

// Handles selections in the main Horde menu
void HordeMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p || !p->entries || p->cur < 0 || p->cur >= p->num) return; // Added bounds check for p->cur

	const int option = p->cur; // Keep option index if needed elsewhere, but don't rely on it for branching here
	const pmenu_t* selected_entry = &p->entries[option]; // Get the selected entry struct
	bool shouldCloseMenu = true;

	// Use text comparison for actions where index might vary
	const char* selected_text = selected_entry->text;

	// --- Action Handling based on Text ---

	// Check for "Go Spectator/AFK"
	if (strcmp(selected_text, "Go Spectator/AFK") == 0) {
		CTFObserver(ent);
		shouldCloseMenu = false; // CTFObserver might handle closing
	}
	// Check for "Misc Options"
	else if (strcmp(selected_text, "Misc Options") == 0) {
		OpenMiscMenu(ent);
		shouldCloseMenu = false;
	}
	// Vote Map
	else if (ctfgame.election == ELECT_NONE && strcmp(selected_text, "Vote Map") == 0) {
		OpenVoteMenu(ent);
		shouldCloseMenu = false;
	}
	// Vote Yes
	else if (ctfgame.election != ELECT_NONE && strcmp(selected_text, "Vote Yes") == 0) {
		CTFVoteYes(ent);
		// Keep shouldCloseMenu = true (default)
	}
	// Vote No
	else if (ctfgame.election != ELECT_NONE && strcmp(selected_text, "Vote No") == 0) {
		CTFVoteNo(ent);
		// Keep shouldCloseMenu = true (default)
	}
	// HUD Options
	else if (strcmp(selected_text, "HUD Options") == 0) {
		OpenHUDMenu(ent);
		shouldCloseMenu = false;
	}
	// Swap Tech
	else if (strcmp(selected_text, "Swap Tech") == 0) {
		OpenTechMenu(ent);
		shouldCloseMenu = false;
	}
	// Show Inventory
	else if (strcmp(selected_text, "Show Inventory") == 0) {
		ShowInventory(ent);
		// Decide if menu should close:
		shouldCloseMenu = false; // Keep menu open after showing inventory
	}
	// Close Button
	else if (strcmp(selected_text, "Close") == 0) {
		// No action needed, handled by shouldCloseMenu = true (default)
	}
	// --- Default Case (Clicked on non-actionable item like title, blank, vote question) ---
	else {
		shouldCloseMenu = false; // Don't close menu for non-actionable items
	}


	// Close the menu if required and if it still exists
	if (shouldCloseMenu && ent->client && ent->client->menu) { // Add ent->client check
		PMenu_Close(ent);
	}
}

// Creates and opens the main Horde menu
// Forward declaration for the handler function if not already visible
void HordeMenuHandler(edict_t* ent, pmenuhnd_t* p);

// Creates and opens the main Horde menu
pmenuhnd_t* CreateHordeMenu(edict_t* ent) {
	if (!ent || !ent->client) {
		return nullptr;
	}

	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	// Increased size slightly to accommodate the potential new entry + separators
	static pmenu_t entries[20]; // Buffer size should be sufficient
	memset(entries, 0, sizeof(entries));
	int count = 0;

	// Helper lambda remains the same
	auto add_entry = [&](const char* text, int align, SelectFunc_t func = nullptr) {
		if (count < static_cast<int>(std::size(entries))) {
			Q_strlcpy(entries[count].text, text, sizeof(entries[count].text));
			entries[count].align = align;
			entries[count].SelectFunc = func;
			count++;
		}
		else {
			gi.Com_Print("Warning: CreateHordeMenu exceeded static entry buffer size.\n");
		}
		};
	// 1. Title & Separator
	add_entry(HORDE_MOD_VERSION_STRING, PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER); // Blank after title

	// 2. Go Spectator
	add_entry("Go Spectator/AFK", PMENU_ALIGN_LEFT, HordeMenuHandler);
	add_entry("", PMENU_ALIGN_CENTER); // Separator after Spectator

	// 3. Vote Section
	if (ctfgame.election == ELECT_NONE) {
		add_entry("Vote Map", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}
	else {
		char vote_question[64];
		snprintf(vote_question, sizeof(vote_question), "Vote: %s", ctfgame.emsg);
		vote_question[sizeof(vote_question) - 1] = '\0'; // Ensure null termination

		add_entry(vote_question, PMENU_ALIGN_CENTER, nullptr); // Display question
		add_entry("Vote Yes", PMENU_ALIGN_LEFT, HordeMenuHandler);
		add_entry("Vote No", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}	add_entry("", PMENU_ALIGN_CENTER); // Separator after Vote section

	// 4. Misc Options <<-- REMOVE THE IF CONDITION HERE
	// int laser_count = 0; // No longer need these counts here
	// if (auto* manager = LaserHelpers::get_laser_manager(ent)) {
	//     laser_count = manager->get_active_count();
	// }
	// int sentry_count = ent->client->num_sentries;
	// if (laser_count > 0 || sentry_count > 0) { // <-- REMOVE THIS IF
	add_entry("Misc Options", PMENU_ALIGN_LEFT, HordeMenuHandler); // <-- ALWAYS ADD THIS
	// } // <-- REMOVE THIS ENDING BRACE

	// 5. HUD Options
	add_entry("HUD Options", PMENU_ALIGN_LEFT, HordeMenuHandler);

	// 6. Change Tech
	add_entry("Swap Tech", PMENU_ALIGN_LEFT, HordeMenuHandler);

	// 7. Show Inventory
	add_entry("Show Inventory", PMENU_ALIGN_LEFT, HordeMenuHandler);

	// Optional blank separators before close
	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);
	// ...

	// Last Entry: Close
	add_entry("Close", PMENU_ALIGN_LEFT, HordeMenuHandler);

	// Open the menu
	return PMenu_Open(ent, entries, -1, count, nullptr, nullptr);
}

// Entry point to open the main Horde menu
void OpenHordeMenu(edict_t* ent) {
	if (!ent || !ent->client) return;
	CreateHordeMenu(ent); // Create and open the menu
}


/////////////////////////////////////////////
		//////SCOREBOARD//////////
/////////////////////////////////////////////


// Make sure we're using the correct fmt namespace for format
//namespace fmt_game = fmt;

// Constants

constexpr size_t MAX_PLAYERS_TO_DISPLAY = 16;
constexpr int PLAYER_Y_START = 42;
constexpr int PLAYER_Y_SPACING = 8;
constexpr int LAYOUT_SAFETY_MARGIN = 50;

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

	std::string str() const {
		return buffer;
	}

	size_t size() const {
		return buffer.size();
	}
};


/**
 * @struct BonusMapping
 * @brief Maps internal benefit names to display text
 */
struct BonusMapping {
	std::string_view benefit_name;
	std::string_view display_text;
};

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
	std::vector<PlayerScore> team_players;
	std::vector<PlayerScore> spectators;
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

	void addHeader() {
		// Wave and enemies information
		if (g_horde->integer) {
			layout_builder.append(fmt::format(
				"if 0 xv -20 yv -10 loc_string2 1 \"Wave Number: {}          Stroggs Remaining: {}\" endif \n",
				last_wave_number, level.total_monsters - level.killed_monsters));
		}

		// Time limit
		if (timelimit->value) {
			layout_builder.append(fmt::format(
				"if 0 xv 340 yv -33 time_limit {} endif \n",
				gi.ServerFrame() + ((gtime_t::from_min(timelimit->value) - level.time)).milliseconds() / gi.frame_time_ms));
		}
	}

	void addTeamScore() {
		if (!level.intermissiontime) {
			// Normal game display
			layout_builder.append("if 25 xv -90 yv 10 dogtag endif \n");

			// Active bonuses
			std::string activeBonuses = GetActiveBonusesString();
			if (!activeBonuses.empty()) {
				layout_builder.append(fmt::format(
					"if 0 xv 208 yv 8 string \"{}\" endif \n", activeBonuses));
			}
		}
		else {
			// Intermission display
			layout_builder.append(fmt::format(
				"if 25 xv -90 yv 10 dogtag endif "
				"if 25 xv 205 yv 8 pic 25 endif "
				"if 0 xv 70 yv -20 num 2 19 endif \n",
				total_score, team_players.size()));
		}
	}

	void addPlayerList() {
		for (size_t i = 0; i < std::min(team_players.size(), MAX_PLAYERS_TO_DISPLAY); ++i) {
			const auto& player = team_players[i];
			int y = PLAYER_Y_START + i * PLAYER_Y_SPACING;

			// Add death indicator if player is dead
			if (player.is_dead) {
				layout_builder.append(fmt::format(
					"if 0 xv -135 yv {} string \"[Dead]\" endif ", y));
			}

			// Add player information
			layout_builder.append(fmt::format(
				"if 0 ctf -90 {} {} {} {} \"\" endif \n",
				y, player.index, player.score, player.ping));
		}
	}

	void addSpectators() {
		// Only add spectators if there's enough space and there are spectators
		if (layout_builder.size() < MAX_CTF_STAT_LENGTH - LAYOUT_SAFETY_MARGIN && !spectators.empty()) {
			// Calculate vertical position after team players
			int y = PLAYER_Y_START + (std::min(team_players.size(), MAX_PLAYERS_TO_DISPLAY) + 2) * PLAYER_Y_SPACING;

			// Add spectator header
			layout_builder.append(fmt::format(
				"if 0 xv -90 yv {} loc_string2 0 \"Spectators & AFK\" endif \n", y));
			y += PLAYER_Y_SPACING;

			// Add each spectator
			for (const auto& spec : spectators) {
				layout_builder.append(fmt::format(
					"if 0 ctf -90 {} {} {} {} \"\" endif \n",
					y, spec.index, spec.score, spec.ping));
				y += PLAYER_Y_SPACING;
			}
		}
	}

	void addFooter() {
		if (!level.intermissiontime) {
			// Standard help text based on player's team
			const char* help_text = (ent->client->resp.ctf_team != CTF_TEAM1)
				? "Use Inventory <KEY> to toggle Horde Menu."
				: "Use Horde Menu on Powerup Wheel or press Inventory <KEY> to toggle Horde Menu.";

			layout_builder.append(fmt::format(
				"if 0 xv 0 yb -55 cstring2 \"{}\" endif \n", help_text));
		}
		else {
			// Intermission message
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
void HordeScoreboardMessage(edict_t* ent, edict_t* killer) {
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
		final_layout.resize(MAX_CTF_STAT_LENGTH - 1);
	}

	// Send to client
	gi.WriteByte(svc_layout);
	gi.WriteString(final_layout.c_str());
}

	//HORDE BONUS STRINGS
/**
 * Gets a formatted string of all active player bonuses
 * return Formatted string with all active bonuses
 */
std::string GetActiveBonusesString() {
	// Define mappings from internal names to display names
	static const std::array<BonusMapping, 11> bonus_mappings = { {
		{"vampire upgraded", "Health & Armor Vampirism"},
		{"vampire", "Health Vampirism"},
		{"ammo regen", "Ammo Regen"},
		{"start armor", "Starting Armor"},
		{"auto haste", "Auto-Haste"},
		{"Cluster Prox Grenades", "Upgraded Prox Launcher"},
		{"Traced-Piercing Bullets", "Traced-Energy Bullets"},
		{"Napalm-Grenade Launcher", "Napalm-Grenade Launcher"},
		{"BFG Grav-Pull Lasers", "BFG Grav-Pull Lasers"},
		{"Piercing Plasma", "Piercing Plasma-Beam"},
		{"Energy Shells", "Energy Shells"}
	} };

	// Create bonus name to index lookup for faster lookups
	static const auto benefit_name_to_index = []() {
		std::unordered_map<std::string_view, size_t> result;
		for (size_t i = 0; i < MAX_BENEFITS; i++) {
			result.emplace(BENEFITS[i].name, i);
		}
		return result;
		}();

	std::vector<std::string_view> active_bonuses;
	active_bonuses.reserve(bonus_mappings.size()); // Reserve space to avoid reallocation

	// Check if "vampire upgraded" is active
	bool has_vampire_upgraded = false;
	auto vampire_upgraded_it = benefit_name_to_index.find("vampire upgraded");
	if (vampire_upgraded_it != benefit_name_to_index.end()) {
		has_vampire_upgraded = has_benefit(vampire_upgraded_it->second);
	}

	// Loop through all benefits to find active ones
	for (const auto& [benefit_name, display_text] : bonus_mappings) {
		// Skip "vampire" if "vampire upgraded" is already active
		if (benefit_name == "vampire" && has_vampire_upgraded) {
			continue;
		}

		// Check if the benefit is active
		auto it = benefit_name_to_index.find(benefit_name);
		if (it != benefit_name_to_index.end() && has_benefit(it->second)) {
			active_bonuses.push_back(display_text);
		}
	}

	// Return early if no bonuses are active
	if (active_bonuses.empty()) {
		return "";
	}

	// Create formatted result string
	StringBuilder result(active_bonuses.size() * 30); // Estimate average length

	for (size_t i = 0; i < active_bonuses.size(); ++i) {
		result.append("* ");
		result.append(active_bonuses[i]);

		if (i < active_bonuses.size() - 1) {
			result.append("\n");
		}
	}

	return result.str();
}

// --- END OF FILE horde_menu.cpp ---
