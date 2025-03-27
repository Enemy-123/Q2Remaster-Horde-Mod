#include "../g_local.h"
#include "../shared.h"
#include "../ctf/p_ctf_menu.h"

void OpenVoteMenu(edict_t* ent);
void VoteMenuHandler(edict_t* ent, pmenuhnd_t* p);
void UpdateVoteMenu();

constexpr size_t MAX_MAPS_PER_PAGE = 11;
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

	// Always close existing menu first
	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	const pmenu_t* menu = (ent->client->resp.ctf_team == CTF_NOTEAM) ?
		tech_menustart : tech_menu;
	const size_t menu_size = (ent->client->resp.ctf_team == CTF_NOTEAM) ?
		sizeof(tech_menustart) / sizeof(pmenu_t) :
		sizeof(tech_menu) / sizeof(pmenu_t);

	PMenu_Open(ent, menu, -1, menu_size, nullptr, nullptr);
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


void OpenHUDMenu(edict_t* ent) {
	// Input validation
	if (!ent || !ent->client) {
		return;
	}

	// Close any existing menu first
	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	// Static allocation for menu entries
	static pmenu_t entries[8];
	memset(entries, 0, sizeof(entries)); // Zero initialize for safety

	// Safe buffer sizes for formatting menu text
	static constexpr size_t TEXT_BUFFER_SIZE = 64;
	char id_text[TEXT_BUFFER_SIZE];
	char dmg_text[TEXT_BUFFER_SIZE];

	// Format option text safely
	snprintf(id_text, sizeof(id_text), "Enable/Disable ID [%s]",
		ent->client->pers.id_state ? "ON" : "OFF");
	snprintf(dmg_text, sizeof(dmg_text), "Enable/Disable ID-DMG [%s]",
		ent->client->pers.iddmg_state ? "ON" : "OFF");

	// Build menu entries with bounds checking
	int count = 0;
	const size_t max_entries = sizeof(entries) / sizeof(entries[0]);

	// Title and spacing
	if (count < max_entries) {
		Q_strlcpy(entries[count].text, "*HUD Options", sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_CENTER;
		entries[count++].SelectFunc = nullptr;
	}

	if (count < max_entries) {
		entries[count].text[0] = '\0';
		entries[count].align = PMENU_ALIGN_CENTER;
		entries[count++].SelectFunc = nullptr;
	}

	// ID Toggle
	if (count < max_entries) {
		Q_strlcpy(entries[count].text, id_text, sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// ID-DMG Toggle
	if (count < max_entries) {
		Q_strlcpy(entries[count].text, dmg_text, sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// Spacing
	if (count < max_entries) {
		entries[count].text[0] = '\0';
		entries[count].align = PMENU_ALIGN_CENTER;
		entries[count++].SelectFunc = nullptr;
	}

	// Back to Horde Menu
	if (count < max_entries) {
		Q_strlcpy(entries[count].text, "Back to Horde Menu", sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// Close
	if (count < max_entries) {
		Q_strlcpy(entries[count].text, "Close", sizeof(entries[count].text));
		entries[count].align = PMENU_ALIGN_LEFT;
		entries[count++].SelectFunc = HUDMenuHandler;
	}

	// Open menu and update UI
	auto p = PMenu_Open(ent, entries, -1, count, nullptr, nullptr);
	if (p) {
		UpdateHUDMenu(ent, p);
	}
}

void UpdateHUDMenu(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p || !p->entries) {
		return;
	}

	// Convert G_Fmt result to std::string explicitly
	std::string id_status = std::string(G_Fmt("Enable/Disable ID [{}]",
		ent->client->pers.id_state ? "ON" : "OFF"));
	std::string dmg_status = std::string(G_Fmt("Enable/Disable ID-DMG [{}]",
		ent->client->pers.iddmg_state ? "ON" : "OFF"));

	// Ensure we don't exceed menu bounds
	if (p->entries && p->entries + 2 && p->entries + 3) {
		PMenu_UpdateEntry(p->entries + 2, id_status.c_str(),
			PMENU_ALIGN_LEFT, HUDMenuHandler);
		PMenu_UpdateEntry(p->entries + 3, dmg_status.c_str(),
			PMENU_ALIGN_LEFT, HUDMenuHandler);
	}

	PMenu_Update(ent);
}

void HUDMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	if (!ent || !ent->client || !p || p->cur < 0) {
		return;
	}

	// Define valid option range
	constexpr int MIN_OPTION = 2;
	constexpr int MAX_OPTION = 6;

	const int option = p->cur;
	if (option < MIN_OPTION || option > MAX_OPTION) {
		PMenu_Close(ent);
		return;
	}

	switch (option) {
	case 2: // Toggle ID
	case 3: { // Toggle ID-DMG
		bool& state = (option == 2) ?
			ent->client->pers.id_state :
			ent->client->pers.iddmg_state;
		state = !state;

		// Use safer string handling
		std::string msg = fmt::format("\n\n\n{} state toggled to {}\n",
			(option == 2) ? "ID" : "ID-DMG",
			state ? "ON" : "OFF");

		gi.LocCenter_Print(ent, msg.c_str());
		UpdateHUDMenu(ent, p);
		break;
	}
	case 5: // Back to Horde Menu
		PMenu_Close(ent);
		if (ent->inuse) { // Extra validation
			OpenHordeMenu(ent);
		}
		break;
	case 6: // Close
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


void OpenMapCategoryMenu(edict_t* ent) {
	if (!ent || !ent->client) {
		return;
	}

	// Always close any existing menu first
	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	// Create a local copy to modify
	static pmenu_t menu_entries[8];
	memcpy(menu_entries, map_category_menu, sizeof(map_category_menu));

	// Set handlers safely
	for (size_t i = 0; i < sizeof(menu_entries) / sizeof(menu_entries[0]); i++) {
		if (i >= 2 && i <= 4) {
			menu_entries[i].SelectFunc = MapCategoryHandler;
		}
		if (i == 6 || i == 7) {
			menu_entries[i].SelectFunc = MapCategoryHandler;
		}
	}

	PMenu_Open(ent, menu_entries, -1, sizeof(menu_entries) / sizeof(menu_entries[0]), nullptr, nullptr);
}

void VoteMenuHandler(edict_t* ent, pmenuhnd_t* p) {
	// Input validation
	if (!ent || !ent->client || !p || p->cur < 0) {
		return;
	}

	const int option = p->cur;
	std::vector<std::string>* current_map_list = nullptr;

	// Get current map list with category check
	if (categorized_maps.current_category.isBigMap) {
		current_map_list = &categorized_maps.big_maps;
	}
	else if (categorized_maps.current_category.isSmallMap) {
		current_map_list = &categorized_maps.small_maps;
	}
	else {
		current_map_list = &categorized_maps.medium_maps;
	}

	// Validate map list
	if (!current_map_list || current_map_list->empty()) {
		PMenu_Close(ent);
		return;
	}

	// Handle map selection (menu options 2 through 2+MAX_MAPS_PER_PAGE)
	if (option >= 2 && option < 2 + MAX_MAPS_PER_PAGE) {
		// Calculate map index with bounds checking
		const size_t page_offset = categorized_maps.current_page * MAX_MAPS_PER_PAGE;
		const size_t map_index = page_offset + (option - 2);

		// Validate map index and menu entry
		if (map_index >= current_map_list->size()) {
			return; // Invalid map index
		}

		if (vote_menu[option].text[0] == '\0') {
			return; // Empty slot
		}

		const std::string& map_name = (*current_map_list)[map_index];

		// Check if it's current map
		if (Q_strcasecmp(map_name.c_str(), level.mapname) == 0) {
			gi.LocClient_Print(ent, PRINT_HIGH, "Can't vote for the current map.\n");
			return;
		}

		// Initiate vote with safe string operations
		Q_strlcpy(ctfgame.elevel, map_name.c_str(), sizeof(ctfgame.elevel));

		char vote_msg[128]; // Safe buffer for vote message
		snprintf(vote_msg, sizeof(vote_msg), "Change map to %s?", map_name.c_str());

		if (CTFBeginElection(ent, ELECT_MAP, vote_msg)) {
			PMenu_Close(ent);
		}
		return;
	}

	// Navigation options - Next, Back, Close
	switch (option) {
	case MAX_MAPS_PER_PAGE + 3: // Next
		if ((categorized_maps.current_page + 1) * MAX_MAPS_PER_PAGE < current_map_list->size()) {
			categorized_maps.current_page++;
			UpdateVoteMenu();

			// Safely recreate menu
			if (ent->client->menu) {
				PMenu_Close(ent);
			}
			PMenu_Open(ent, vote_menu, -1, sizeof(vote_menu) / sizeof(vote_menu[0]), nullptr, nullptr);
		}
		break;

	case MAX_MAPS_PER_PAGE + 4: // Back
		if (ent->client->menu) {
			PMenu_Close(ent);
		}
		OpenMapCategoryMenu(ent);
		break;

	case MAX_MAPS_PER_PAGE + 5: // Close
		PMenu_Close(ent);
		break;
	}
}

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

	size_t start = categorized_maps.current_page * MAX_MAPS_PER_PAGE;
	size_t end = std::min(start + MAX_MAPS_PER_PAGE, current_map_list->size());

	if (start >= current_map_list->size()) {
		categorized_maps.current_page = 0;
		start = 0;
		end = std::min(MAX_MAPS_PER_PAGE, current_map_list->size());
	}

	// Update category title
	const char* category_name = categorized_maps.current_category.isBigMap ? "Big Maps" :
		categorized_maps.current_category.isSmallMap ? "Small Maps" :
		"Medium Maps";

	std::string category_title = "*" + std::string(category_name);
	Q_strlcpy(vote_menu[0].text, category_title.c_str(), sizeof(vote_menu[0].text));

	// Clear existing entries
	for (size_t i = 2; i < 2 + MAX_MAPS_PER_PAGE; i++) {
		vote_menu[i].text[0] = '\0';
		vote_menu[i].SelectFunc = nullptr;
	}

	// Fill map entries
	for (size_t i = 0; i < (end - start); i++) {
		if ((i + 2) < std::size(vote_menu)) {
			Q_strlcpy(vote_menu[i + 2].text, (*current_map_list)[start + i].c_str(),
				sizeof(vote_menu[i + 2].text));
			vote_menu[i + 2].SelectFunc = VoteMenuHandler;
		}
	}

	// Update navigation buttons
	bool has_next_page = (categorized_maps.current_page + 1) * MAX_MAPS_PER_PAGE < current_map_list->size();

	// Next button
	if (has_next_page) {
		Q_strlcpy(vote_menu[MAX_MAPS_PER_PAGE + 3].text, "Next",
			sizeof(vote_menu[MAX_MAPS_PER_PAGE + 3].text));
		vote_menu[MAX_MAPS_PER_PAGE + 3].SelectFunc = VoteMenuHandler;
	}
	else {
		vote_menu[MAX_MAPS_PER_PAGE + 3].text[0] = '\0';
		vote_menu[MAX_MAPS_PER_PAGE + 3].SelectFunc = nullptr;
	}

	// Back button
	Q_strlcpy(vote_menu[MAX_MAPS_PER_PAGE + 4].text, "Back",
		sizeof(vote_menu[MAX_MAPS_PER_PAGE + 4].text));
	vote_menu[MAX_MAPS_PER_PAGE + 4].SelectFunc = VoteMenuHandler;

	// Close button
	Q_strlcpy(vote_menu[MAX_MAPS_PER_PAGE + 5].text, "Close",
		sizeof(vote_menu[MAX_MAPS_PER_PAGE + 5].text));
	vote_menu[MAX_MAPS_PER_PAGE + 5].SelectFunc = VoteMenuHandler;
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


// horde menu

// // Helper function para crear menús de horda dinámicamente
pmenuhnd_t* CreateHordeMenu(edict_t* ent) {
	// Close any existing menu to prevent memory leaks
	if (ent->client->menu) {
		PMenu_Close(ent);
	}

	// Use global static array - more predictable memory usage
	static pmenu_t entries[32];
	const size_t max_entries = sizeof(entries) / sizeof(entries[0]);
	int count = 0;

	// Safety check for array bounds
	auto add_entry = [&](const char* text, int align, SelectFunc_t func = nullptr) {
		if (count < max_entries) {
			Q_strlcpy(entries[count].text, text, sizeof(entries[count].text));
			entries[count].align = align;
			entries[count].SelectFunc = func;
			count++;
		}
		};

	// Add menu entries using the safe function
	add_entry("*Horde Menu", PMENU_ALIGN_CENTER);
	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("Show Inventory", PMENU_ALIGN_LEFT, HordeMenuHandler);
	add_entry("Go Spectator/AFK", PMENU_ALIGN_LEFT, HordeMenuHandler);
	add_entry("", PMENU_ALIGN_CENTER);

	if (ctfgame.election == ELECT_NONE) {
		add_entry("Vote Map", PMENU_ALIGN_LEFT, HordeMenuHandler);
		add_entry("", PMENU_ALIGN_CENTER);
		add_entry("Change Tech", PMENU_ALIGN_LEFT, HordeMenuHandler);
		add_entry("HUD Options", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}
	else {
		add_entry("Vote Yes", PMENU_ALIGN_LEFT, HordeMenuHandler);
		add_entry("Vote No", PMENU_ALIGN_LEFT, HordeMenuHandler);
		add_entry("", PMENU_ALIGN_CENTER);
		add_entry("Change Tech", PMENU_ALIGN_LEFT, HordeMenuHandler);
		add_entry("HUD Options", PMENU_ALIGN_LEFT, HordeMenuHandler);
	}

	add_entry("", PMENU_ALIGN_CENTER);
	add_entry("Close", PMENU_ALIGN_LEFT, HordeMenuHandler);

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

