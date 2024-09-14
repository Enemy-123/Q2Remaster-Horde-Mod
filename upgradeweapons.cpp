//
//// Estructura para almacenar información de armas
//struct weapon_info_t {
//    const char* name;
//    int max_level;
//    bool has_radius;
//    bool has_range;
//};
//
//// Lista de armas con sus propiedades
//static const weapon_info_t weapons[] = {
//    {"Blaster", 10, false, false},
//    {"Shotgun", 10, false, false},
//    {"Super Shotgun", 10, false, false},
//    {"Machinegun", 10, false, false},
//    {"ETF Rifle", 10, false, false},
//    {"Chaingun", 10, false, false},
//    {"Tesla Grenades", 10, true, true},
//    {"Hand Grenades", 10, true, true},
//    {"Prox Launcher", 10, true, false},
//    {"Grenade Launcher", 10, true, false},
//    {"Rocket Launcher", 10, true, false},
//    {"Ionripper", 10, false, false},
//    {"Hyperblaster", 10, false, false},
//    {"Plasma Beam", 10, false, false},
//    {"Railgun", 10, false, false},
//    {"Phalanx", 10, true, false},
//    {"BFG10K", 10, false, false},
//    {"Disruptor", 10, false, false}
//};
//
//#define MAX_WEAPON_UPGRADES (sizeof(weapons) / sizeof(weapons[0]))
//#define MAX_UPGRADE_MENU_ENTRIES (MAX_WEAPON_UPGRADES + 3) // +3 para título, línea en blanco y "Close"
//
//static pmenu_t upgrade_menu[MAX_UPGRADE_MENU_ENTRIES];
//
//void UpdateUpgradeMenu(edict_t* ent) {
//    int i, count = 0;
//    char buffer[64];
//
//    // Título del menú
//    strcpy(upgrade_menu[count++].text, "*Weapon Upgrade Menu");
//    upgrade_menu[count++].text[0] = 0; // Línea en blanco
//
//    // Generar entradas de menú para cada arma
//    for (i = 0; i < MAX_WEAPON_UPGRADES; i++) {
//        const weapon_info_t* weapon = &weapons[i];
//        int current_level = ent->client->pers.weapon_levels[i]; // Asumiendo que existe esta estructura
//
//        snprintf(buffer, sizeof(buffer), "%s [%d/%d]", weapon->name, current_level, weapon->max_level);
//        strcpy(upgrade_menu[count].text, buffer);
//        upgrade_menu[count].align = PMENU_ALIGN_LEFT;
//        upgrade_menu[count].SelectFunc = WeaponUpgradeHandler;
//        count++;
//
//        // Agregar información adicional si es necesario
//        if (weapon->has_radius || weapon->has_range) {
//            if (weapon->has_radius) {
//                snprintf(buffer, sizeof(buffer), "  Radius: [%d/%d]", ent->client->pers.weapon_radius[i], 10); // Asumiendo max 10
//                strcpy(upgrade_menu[count].text, buffer);
//                upgrade_menu[count].align = PMENU_ALIGN_LEFT;
//                upgrade_menu[count].SelectFunc = WeaponUpgradeHandler;
//                count++;
//            }
//            if (weapon->has_range) {
//                snprintf(buffer, sizeof(buffer), "  Range: [%d/%d]", ent->client->pers.weapon_range[i], 10); // Asumiendo max 10
//                strcpy(upgrade_menu[count].text, buffer);
//                upgrade_menu[count].align = PMENU_ALIGN_LEFT;
//                upgrade_menu[count].SelectFunc = WeaponUpgradeHandler;
//                count++;
//            }
//        }
//    }
//
//    // Opción para cerrar el menú
//    strcpy(upgrade_menu[count].text, "Close");
//    upgrade_menu[count].align = PMENU_ALIGN_LEFT;
//    upgrade_menu[count].SelectFunc = WeaponUpgradeHandler;
//}
//
//void WeaponUpgradeHandler(edict_t* ent, pmenuhnd_t* p) {
//    int selected = p->cur - 2; // -2 para ajustar por el título y la línea en blanco
//
//    if (selected >= 0 && selected < MAX_WEAPON_UPGRADES) {
//        int weapon_index = selected / 3; // Cada arma puede tener hasta 3 líneas (nombre, radio, rango)
//        int upgrade_type = selected % 3; // 0 = damage, 1 = radius, 2 = range
//
//        const weapon_info_t* weapon = &weapons[weapon_index];
//
//        switch (upgrade_type) {
//        case 0: // Upgrade damage
//            if (ent->client->pers.weapon_levels[weapon_index] < weapon->max_level) {
//                ent->client->pers.weapon_levels[weapon_index]++;
//                gi.LocCenter_Print(ent, "Upgraded {} damage to level {}\n", weapon->name, ent->client->pers.weapon_levels[weapon_index]);
//            }
//            else {
//                gi.LocCenter_Print(ent, "{} damage is already at max level\n", weapon->name);
//            }
//            break;
//        case 1: // Upgrade radius (if applicable)
//            if (weapon->has_radius && ent->client->pers.weapon_radius[weapon_index] < 10) {
//                ent->client->pers.weapon_radius[weapon_index]++;
//                gi.LocCenter_Print(ent, "Upgraded {} radius to level {}\n", weapon->name, ent->client->pers.weapon_radius[weapon_index]);
//            }
//            break;
//        case 2: // Upgrade range (if applicable)
//            if (weapon->has_range && ent->client->pers.weapon_range[weapon_index] < 10) {
//                ent->client->pers.weapon_range[weapon_index]++;
//                gi.LocCenter_Print(ent, "Upgraded {} range to level {}\n", weapon->name, ent->client->pers.weapon_range[weapon_index]);
//            }
//            break;
//        }
//
//        // Actualizar y volver a abrir el menú
//        UpdateUpgradeMenu(ent);
//        PMenu_Open(ent, upgrade_menu, -1, sizeof(upgrade_menu) / sizeof(pmenu_t), nullptr, nullptr);
//    }
//    else if (strcmp(upgrade_menu[p->cur].text, "Close") == 0) {
//        PMenu_Close(ent);
//    }
//}
//
//void OpenWeaponUpgradeMenu(edict_t* ent) {
//    UpdateUpgradeMenu(ent);
//    PMenu_Open(ent, upgrade_menu, -1, sizeof(upgrade_menu) / sizeof(pmenu_t), nullptr, nullptr);
//}