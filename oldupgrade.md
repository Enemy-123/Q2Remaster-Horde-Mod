/ --- BENEFITS MENU SYSTEM ---

// Open Abilities Menu
void OpenAbilitiesMenu(edict_t* ent) {
    // Set menu protection
    if (ent && ent->client) {
        ent->client->menu_protected = true;
        ent->client->menu_protection_start = level.time;
    }
    CreateAbilitiesMenu(ent);
}

// Abilities Menu Handler
void AbilitiesMenuHandler(edict_t* ent, pmenuhnd_t* p) {
    if (!ent || !ent->client || !p) return;

    pmenu_t* item = &p->entries[p->cur];
    if (!item->SelectFunc) return;

    // Handle benefit purchase
    if (strncmp(item->text_arg1, "ability_", 8) == 0) {
        const char* benefit_name = item->text_arg1 + 8; // Skip "ability_" prefix

        // Find benefit by name
        for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS; ++i) {
            if (g_benefitsData.categories[i] != BenefitCategory::ABILITY) continue;

            if (strcmp(g_benefitsData.names[i], benefit_name) == 0) {
                BenefitID benefit_id = static_cast<BenefitID>(i);
                int32_t cost = 1; // Default cost

                if (PlayerPurchaseBenefit(ent, benefit_id, cost)) {
                    // Refresh menu to show updated state
                    PMenu_Close(ent);
                    OpenAbilitiesMenu(ent);
                }
                return;
            }
        }
    }

    // Handle special menu actions
    if (strcmp(item->text_arg1, "back_to_main") == 0) {
        PMenu_Close(ent);
        OpenUpgradeMenu(ent);
    }
}

// Create Abilities Menu
pmenuhnd_t* CreateAbilitiesMenu(edict_t* ent) {
    if (!ent || !ent->client) return nullptr;

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
    for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS && menu_index < 25; ++i) {
        if (g_benefitsData.categories[i] != BenefitCategory::ABILITY) continue;

        BenefitID benefit_id = static_cast<BenefitID>(i);
        bool owned = PlayerHasBenefit(ent, benefit_id);

        // Skip if already owned - cleaner menu
        if (owned) {
            continue;
        }

        // Check prerequisites
        auto prereq = g_benefitsData.prerequisites[i];
        bool prereq_met = (prereq == BenefitID::NONE) || PlayerHasBenefit(ent, prereq);

        // Don't show if prerequisite not met - cleaner menu
        if (!prereq_met) {
            continue;
        }

        bool can_afford = ent->client->pers.ability_points >= 1;

        // Available to purchase
        G_FmtTo(abilities_menu[menu_index].text,
                 "{} {} (1 pt)", can_afford ? ">" : " ", g_benefitsData.names[i]);
        abilities_menu[menu_index].align = PMENU_ALIGN_LEFT;
        if (can_afford) {
            abilities_menu[menu_index].SelectFunc = AbilitiesMenuHandler;
            snprintf(abilities_menu[menu_index].text_arg1, sizeof(abilities_menu[menu_index].text_arg1),
                     "ability_%s", g_benefitsData.names[i]);
        } else {
            abilities_menu[menu_index].SelectFunc = nullptr;
        }
        menu_index++;
        has_available = true;
    }

    if (!has_available) {
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
void OpenWeaponsMenu(edict_t* ent) {
    // Set menu protection
    if (ent && ent->client) {
        ent->client->menu_protected = true;
        ent->client->menu_protection_start = level.time;
    }
    CreateWeaponsMenu(ent);
}

// Weapons Menu Handler
void WeaponsMenuHandler(edict_t* ent, pmenuhnd_t* p) {
    if (!ent || !ent->client || !p) return;

    pmenu_t* item = &p->entries[p->cur];
    if (!item->SelectFunc) return;

    // Handle benefit purchase
    if (strncmp(item->text_arg1, "weapon_", 7) == 0) {
        const char* benefit_name = item->text_arg1 + 7; // Skip "weapon_" prefix

        // Find benefit by name
        for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS; ++i) {
            if (g_benefitsData.categories[i] != BenefitCategory::WEAPON) continue;

            if (strcmp(g_benefitsData.names[i], benefit_name) == 0) {
                BenefitID benefit_id = static_cast<BenefitID>(i);

                // Set costs based on benefit type
                int32_t cost = 1; // Default cost
                if (benefit_id == BenefitID::BFG_GRAV_PULL || benefit_id == BenefitID::CLUSTER_PROX) {
                    cost = 3;
                }

                if (PlayerPurchaseBenefit(ent, benefit_id, cost)) {
                    // Refresh menu to show updated state
                    PMenu_Close(ent);
                    OpenWeaponsMenu(ent);
                }
                return;
            }
        }
    }

    // Handle special menu actions
    if (strcmp(item->text_arg1, "back_to_main") == 0) {
        PMenu_Close(ent);
        OpenUpgradeMenu(ent);
    }
}

// Create Weapons Menu
pmenuhnd_t* CreateWeaponsMenu(edict_t* ent) {
    if (!ent || !ent->client) return nullptr;

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
    for (size_t i = 0; i < BenefitsDataSoA::NUM_BENEFITS && menu_index < 25; ++i) {
        if (g_benefitsData.categories[i] != BenefitCategory::WEAPON) continue;

        BenefitID benefit_id = static_cast<BenefitID>(i);
        bool owned = PlayerHasBenefit(ent, benefit_id);

        // Skip if already owned - cleaner menu
        if (owned) {
            continue;
        }

        // Set costs based on benefit type
        int32_t cost = 1; // Default cost
        if (benefit_id == BenefitID::BFG_GRAV_PULL || benefit_id == BenefitID::CLUSTER_PROX) {
            cost = 3;
        }

        // Check prerequisites
        auto prereq = g_benefitsData.prerequisites[i];
        bool prereq_met = (prereq == BenefitID::NONE) || PlayerHasBenefit(ent, prereq);

        // Don't show if prerequisite not met - cleaner menu
        if (!prereq_met) {
            continue;
        }

        bool can_afford = ent->client->pers.weapon_points >= cost;

        // Available to purchase
        G_FmtTo(weapons_menu[menu_index].text,
                 "{} {} ({} pt{})", can_afford ? ">" : " ", g_benefitsData.names[i], cost, cost > 1 ? "s" : "");
        weapons_menu[menu_index].align = PMENU_ALIGN_LEFT;
        if (can_afford) {
            weapons_menu[menu_index].SelectFunc = WeaponsMenuHandler;
            snprintf(weapons_menu[menu_index].text_arg1, sizeof(weapons_menu[menu_index].text_arg1),
                     "weapon_%s", g_benefitsData.names[i]);
        } else {
            weapons_menu[menu_index].SelectFunc = nullptr;
        }
        menu_index++;
        has_available = true;
    }

    if (!has_available) {
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

void OpenUpgradeMenu(edict_t* ent) {
    // Set menu protection for upgrade menu
    if (ent && ent->client) {
        ent->client->menu_protected = true;
        ent->client->menu_protection_start = level.time;
    }
    CreateUpgradeMenu(ent);
}

void UpgradeMenuHandler(edict_t* ent, pmenuhnd_t* p) {
    if (!ent || !ent->client || !p) return;

    pmenu_t* item = &p->entries[p->cur];
    if (!item->SelectFunc) return;

    // Handle menu navigation
    if (strcmp(item->text_arg1, "abilities_shop") == 0) {
        PMenu_Close(ent);
        OpenAbilitiesMenu(ent); // This already sets protection
    } else if (strcmp(item->text_arg1, "weapon_upgrades") == 0) {
        PMenu_Close(ent);
        OpenWeaponsMenu(ent); // This already sets protection
    } else if (strcmp(item->text_arg1, "restore_points") == 0) {
        // Preserve admin-given bonus points
        int32_t admin_bonus_ability = ent->client->pers.admin_bonus_ability_points;
        int32_t admin_bonus_weapon = ent->client->pers.admin_bonus_weapon_points;

        PlayerRestoreAllPoints(ent);

        // Re-add admin bonus points after restore
        ent->client->pers.ability_points += admin_bonus_ability;
        ent->client->pers.weapon_points += admin_bonus_weapon;

        if (admin_bonus_ability > 0 || admin_bonus_weapon > 0) {
            gi.LocClient_Print(ent, PRINT_HIGH, "Points restored (preserved {} admin ability and {} admin weapon bonus)\n",
                              admin_bonus_ability, admin_bonus_weapon);
        }

        PMenu_Close(ent);
        OpenUpgradeMenu(ent); // This already sets protection
    } else if (strcmp(item->text_arg1, "toggle_auto_buy_abilities") == 0) {
        bool was_enabled = ent->client->pers.auto_buy_abilities;
        ent->client->pers.auto_buy_abilities = !ent->client->pers.auto_buy_abilities;

        // If disabling auto-buy for the first time, offer refund
        if (was_enabled && !ent->client->pers.auto_buy_abilities &&
            !ent->client->pers.has_manually_disabled_auto_buy) {
            PlayerRefundAutoPurchasedBenefits(ent);
        } else {
            gi.LocClient_Print(ent, PRINT_HIGH, "Auto-buy abilities: {}\n",
                      ent->client->pers.auto_buy_abilities ? "ON" : "OFF");
        }
        PMenu_Close(ent);
        OpenUpgradeMenu(ent); // This already sets protection
    } else if (strcmp(item->text_arg1, "toggle_auto_buy_weapons") == 0) {
        bool was_enabled = ent->client->pers.auto_buy_weapons;
        ent->client->pers.auto_buy_weapons = !ent->client->pers.auto_buy_weapons;

        // If disabling auto-buy for the first time, offer refund
        if (was_enabled && !ent->client->pers.auto_buy_weapons &&
            !ent->client->pers.has_manually_disabled_auto_buy) {
            PlayerRefundAutoPurchasedBenefits(ent);
        } else {
            gi.LocClient_Print(ent, PRINT_HIGH, "Auto-buy weapons: {}\n",
                      ent->client->pers.auto_buy_weapons ? "ON" : "OFF");
        }
        PMenu_Close(ent);
        OpenUpgradeMenu(ent); // This already sets protection
    } else if (strcmp(item->text_arg1, "back_to_main") == 0) {
        PMenu_Close(ent);
        OpenHordeMenu(ent); // This already sets protection
    }
}

pmenuhnd_t* CreateUpgradeMenu(edict_t* ent) {
    if (!ent || !ent->client) return nullptr;

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
