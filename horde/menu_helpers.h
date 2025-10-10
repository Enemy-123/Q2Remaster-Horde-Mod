// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#pragma once

#include "../g_local.h"
#include <cstring>

// Menu alignment configuration
constexpr int MENU_NAME_WIDTH = 24;    // Width for "Name:" after padding (pushes brackets to right)
constexpr int MENU_TEXT_MAX = 64;      // Max text buffer size for menu items

// Utility function to pad a string with spaces to a target length (like Vortex)
inline void padRight(char* str, int target_length) {
	for (int i = strlen(str); i < target_length; ++i)
		str[i] = ' ';
	str[target_length] = '\0';
}

// Simple sprintf-based formatting for menu items with aligned indicators

// Format a menu item with numbered prefix and progress indicator
// Example: "  1. Vampirism:      [ 3/10]" (right-aligned with left padding)
// Uses %2d formatting for perfect 2-digit alignment
inline void MenuFormatItemWithProgress(char* buffer, size_t buffer_size,
                                       int item_number, const char* name,
                                       int current, int max)
{
	char padded_name[MENU_TEXT_MAX];
	snprintf(padded_name, sizeof(padded_name), "%s:", name);
	padRight(padded_name, MENU_NAME_WIDTH);
	snprintf(buffer, buffer_size, "  %d. %s [%2d/%2d]", item_number, padded_name, current, max);
}

// Format a menu item with numbered prefix and [OWNED] indicator
// Example: "  1. Vampirism:      [OWNED]" (right-aligned with left padding)
inline void MenuFormatItemWithOwned(char* buffer, size_t buffer_size,
                                    int item_number, const char* name)
{
	char padded_name[MENU_TEXT_MAX];
	snprintf(padded_name, sizeof(padded_name), "%s:", name);
	padRight(padded_name, MENU_NAME_WIDTH);
	snprintf(buffer, buffer_size, "  %d. %s [OWNED]", item_number, padded_name);
}

// Format a menu item with numbered prefix and cost in points
// Example: "  1. Speed Boost:    ( 2pts)" (right-aligned with left padding)
inline void MenuFormatItemWithCost(char* buffer, size_t buffer_size,
                                   int item_number, const char* name,
                                   int cost)
{
	char padded_name[MENU_TEXT_MAX];
	snprintf(padded_name, sizeof(padded_name), "%s:", name);
	padRight(padded_name, MENU_NAME_WIDTH);
	snprintf(buffer, buffer_size, "  %d. %s (%2dpt%s)", item_number, padded_name, cost, cost > 1 ? "s" : "");
}

// Format a menu item with numbered prefix and custom text
// Example: "  1. Item Name:      [CUSTOM]" (right-aligned with left padding)
inline void MenuFormatItemWithCustom(char* buffer, size_t buffer_size,
                                     int item_number, const char* name,
                                     const char* right_text)
{
	char padded_name[MENU_TEXT_MAX];
	snprintf(padded_name, sizeof(padded_name), "%s:", name);
	padRight(padded_name, MENU_NAME_WIDTH);
	snprintf(buffer, buffer_size, "  %d. %s %s", item_number, padded_name, right_text);
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
