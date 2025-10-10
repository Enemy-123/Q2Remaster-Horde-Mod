// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#pragma once

#include "../g_local.h"
#include <cstring>

// Menu alignment configuration
constexpr int MENU_TEXT_MAX = 64;      // Max text buffer size for menu items

// Format a menu item with numbered prefix and progress indicator
// Uses tab separator (\t) between name and indicator for fixed-column rendering
// Example: "  1. Vampire:\t[03/10]"
// The renderer in p_ctf_menu.cpp splits on \t and renders each part at fixed X positions
inline void MenuFormatItemWithProgress(char* buffer, size_t buffer_size,
                                       int item_number, const char* name,
                                       int current, int max)
{
    // Use tab character to separate left and right columns for fixed-position rendering
    // Format: "  1. Vampire:\t[00/10]"
    snprintf(buffer, buffer_size, "  %d. %s:\t[%02d/%02d]",
             item_number,
             name,
             current,
             max);
}
// Format a menu item with numbered prefix and [OWNED] indicator
// Uses tab separator for fixed-column rendering
// Example: "  1. Vampire:\t[OWNED]"
inline void MenuFormatItemWithOwned(char* buffer, size_t buffer_size,
                                    int item_number, const char* name)
{
	// Use tab character to separate left and right columns
	snprintf(buffer, buffer_size, "  %d. %s:\t[OWNED]", item_number, name);
}

// Format a menu item with numbered prefix and cost in points
// Uses tab separator for fixed-column rendering
// Example: "  1. Speed Boost:\t(2pts)"
inline void MenuFormatItemWithCost(char* buffer, size_t buffer_size,
                                   int item_number, const char* name,
                                   int cost)
{
	// Use tab character to separate left and right columns
	snprintf(buffer, buffer_size, "  %d. %s:\t(%dpt%s)", item_number, name, cost, cost > 1 ? "s" : "");
}

// Format a menu item with numbered prefix and custom text
// Uses tab separator for fixed-column rendering
// Example: "  1. Item Name:\t[CUSTOM]"
inline void MenuFormatItemWithCustom(char* buffer, size_t buffer_size,
                                     int item_number, const char* name,
                                     const char* right_text)
{
	// Use tab character to separate left and right columns
	snprintf(buffer, buffer_size, "  %d. %s:\t%s", item_number, name, right_text);
}

// Macro versions for convenience (use the inline functions above)
#define MENU_ITEM_WITH_PROGRESS(buffer, item_number, name, current, max) \
	MenuFormatItemWithProgress(buffer, sizeof(buffer), item_number, name, current, max)

#define MENU_ITEM_WITH_OWNED(buffer, item_number, name) \
	MenuFormatItemWithOwned(buffer, sizeof(buffer), item_number, name)

#define MENU_ITEM_WITH_COST(buffer, item_number, name, cost) \
	MenuFormatItemWithCost(buffer, sizeof(buffer), item_number, name, cost)

#define MENU_ITEM_WITH_CUSTOM(buffer, item_number, name, right_text) \
	MenuFormatItemWithCustom(buffer, sizeof(buffer), item_number, name, right_text)
