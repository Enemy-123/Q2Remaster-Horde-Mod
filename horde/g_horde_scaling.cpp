#include "g_horde_scaling.h"
#include "g_horde.h"
#include "../g_local.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <json/json.h>

// Global scaling configuration
ScalingConfig g_scalingConfig;

// External references
extern cvar_t* g_chaotic;
extern cvar_t* g_insane;
extern cvar_t* g_hardcoop;
extern cvar_t* developer;
extern int16_t current_wave_level;

// External function from g_horde.cpp
extern horde::MapSize GetCurrentMapSize();

// Default scaling parameters (can be overridden by config file)
static void SetDefaultScalingConfig() {
    // Monster Health: Aggressive early growth, plateaus around wave 35
    g_scalingConfig.monster_health = {
        .midpoint = 20.0f,
        .growth_rate = 0.15f,
        .min_value = 1.0f,
        .max_value = 3.5f
    };

    // Monster Armor: Slightly delayed growth compared to health
    g_scalingConfig.monster_armor = {
        .midpoint = 22.0f,
        .growth_rate = 0.12f,
        .min_value = 1.0f,
        .max_value = 2.5f
    };

    // Monster Damage: Moderate growth to maintain challenge
    g_scalingConfig.monster_damage = {
        .midpoint = 18.0f,
        .growth_rate = 0.10f,
        .min_value = 1.0f,
        .max_value = 2.0f
    };

    // Player Weapon Damage: Helps players keep up with monster health
    g_scalingConfig.player_weapon_damage = {
        .midpoint = 15.0f,
        .growth_rate = 0.12f,
        .min_value = 1.0f,
        .max_value = 2.0f
    };

    // Player Ammo Drops: More ammo at higher waves
    g_scalingConfig.player_ammo_drops = {
        .midpoint = 12.0f,
        .growth_rate = 0.20f,
        .min_value = 1.0f,
        .max_value = 1.5f
    };

    // Common Items: Slightly reduced at higher waves
    g_scalingConfig.item_common_drops = {
        .midpoint = 15.0f,
        .growth_rate = 0.15f,
        .min_value = 0.7f,
        .max_value = 1.0f
    };

    // Rare Items: Increase availability at higher waves
    g_scalingConfig.item_rare_drops = {
        .midpoint = 20.0f,
        .growth_rate = 0.18f,
        .min_value = 1.0f,
        .max_value = 2.0f
    };

    // Legendary Items: Significant increase at very high waves
    g_scalingConfig.item_legendary_drops = {
        .midpoint = 25.0f,
        .growth_rate = 0.20f,
        .min_value = 1.0f,
        .max_value = 3.0f
    };

    // Difficulty modifiers
    g_scalingConfig.chaos_modifier = 1.15f;
    g_scalingConfig.insane_modifier = 1.3f;
    g_scalingConfig.map_size_small_modifier = 0.9f;
    g_scalingConfig.map_size_medium_modifier = 1.0f;
    g_scalingConfig.map_size_large_modifier = 1.1f;
}

// Initialize the scaling system
void InitializeScalingSystem() {
    SetDefaultScalingConfig();
    LoadScalingConfig();

    if (developer && developer->integer) {
        gi.Com_Print("=== Horde Scaling System Initialized ===\n");
        PrintScalingCurve("Monster Health", g_scalingConfig.monster_health, 40);
        PrintScalingCurve("Player Weapon Damage", g_scalingConfig.player_weapon_damage, 40);
    }
}

// Load scaling configuration from JSON file
void LoadScalingConfig() {
    // Get base directory from game
    std::string gamedir = gi.cvar("gamedir", "", CVAR_NOFLAGS)->string;
    if (gamedir.empty()) {
        gamedir = "baseq2";
    }

    std::string filename = gamedir + "/config/scaling.json";
    std::ifstream file(filename, std::ifstream::binary);

    if (!file.is_open()) {
        gi.Com_PrintFmt("Scaling: config/scaling.json not found at {}, using defaults\n", filename);
        return;
    }

    // Parse JSON
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;

    if (!Json::parseFromStream(builder, file, &root, &errs)) {
        gi.Com_PrintFmt("Scaling: Failed to parse {}: {}\n", filename, errs);
        file.close();
        return;
    }

    file.close();

    // Read enabled flag
    if (root.isMember("enabled") && root["enabled"].isBool()) {
        g_config.use_sigmoid_scaling = root["enabled"].asBool();
    }

    // Read bosses_only flag
    if (root.isMember("bosses_only") && root["bosses_only"].isBool()) {
        g_config.use_sigmoid_scaling_bosses_only = root["bosses_only"].asBool();
    }

    // Read except_bosses flag
    if (root.isMember("except_bosses") && root["except_bosses"].isBool()) {
        g_config.use_sigmoid_scaling_except_bosses = root["except_bosses"].asBool();
    }

    // Read monster health scaling parameters
    if (root.isMember("monster_scaling") && root["monster_scaling"].isObject()) {
        const Json::Value& monster_scaling = root["monster_scaling"];

        if (monster_scaling.isMember("health") && monster_scaling["health"].isObject()) {
            const Json::Value& health = monster_scaling["health"];

            if (health.isMember("midpoint") && health["midpoint"].isNumeric()) {
                g_scalingConfig.monster_health.midpoint = health["midpoint"].asFloat();
            }
            if (health.isMember("growth_rate") && health["growth_rate"].isNumeric()) {
                g_scalingConfig.monster_health.growth_rate = health["growth_rate"].asFloat();
            }
            if (health.isMember("min_value") && health["min_value"].isNumeric()) {
                g_scalingConfig.monster_health.min_value = health["min_value"].asFloat();
            }
            if (health.isMember("max_value") && health["max_value"].isNumeric()) {
                g_scalingConfig.monster_health.max_value = health["max_value"].asFloat();
            }
        }
    }

    // Read difficulty modifiers
    if (root.isMember("difficulty_modifiers") && root["difficulty_modifiers"].isObject()) {
        const Json::Value& modifiers = root["difficulty_modifiers"];

        if (modifiers.isMember("chaos_modifier") && modifiers["chaos_modifier"].isNumeric()) {
            g_scalingConfig.chaos_modifier = modifiers["chaos_modifier"].asFloat();
        }
        if (modifiers.isMember("insane_modifier") && modifiers["insane_modifier"].isNumeric()) {
            g_scalingConfig.insane_modifier = modifiers["insane_modifier"].asFloat();
        }
        if (modifiers.isMember("map_size_small") && modifiers["map_size_small"].isNumeric()) {
            g_scalingConfig.map_size_small_modifier = modifiers["map_size_small"].asFloat();
        }
        if (modifiers.isMember("map_size_medium") && modifiers["map_size_medium"].isNumeric()) {
            g_scalingConfig.map_size_medium_modifier = modifiers["map_size_medium"].asFloat();
        }
        if (modifiers.isMember("map_size_large") && modifiers["map_size_large"].isNumeric()) {
            g_scalingConfig.map_size_large_modifier = modifiers["map_size_large"].asFloat();
        }
    }

    if (developer && developer->integer) {
        gi.Com_PrintFmt("Scaling: Sigmoid scaling enabled={}, bosses_only={}\n",
            g_config.use_sigmoid_scaling, g_config.use_sigmoid_scaling_bosses_only);
        gi.Com_PrintFmt("Scaling: Health params - midpoint={}, growth={}, min={}, max={}\n",
            g_scalingConfig.monster_health.midpoint, g_scalingConfig.monster_health.growth_rate,
            g_scalingConfig.monster_health.min_value, g_scalingConfig.monster_health.max_value);
    }
}

// Core sigmoid function implementation
float CalculateSigmoid(float current_wave, const SigmoidParams& params) {
    // Sigmoid formula: y = min + (max - min) / (1 + e^(-growth * (x - midpoint)))
    float exponent = -params.growth_rate * (current_wave - params.midpoint);
    float sigmoid = 1.0f / (1.0f + std::exp(exponent));
    return params.min_value + (params.max_value - params.min_value) * sigmoid;
}

// Get monster health scaling for current wave
float GetMonsterHealthScale(int wave_level) {
    float base_scale = CalculateSigmoid(static_cast<float>(wave_level), g_scalingConfig.monster_health);

    // Apply difficulty modifiers
    base_scale *= GetDifficultyModifier();
    base_scale *= GetMapSizeModifier();

    return base_scale;
}

// Get monster armor scaling for current wave
float GetMonsterArmorScale(int wave_level) {
    float base_scale = CalculateSigmoid(static_cast<float>(wave_level), g_scalingConfig.monster_armor);

    // Apply difficulty modifiers
    base_scale *= GetDifficultyModifier();
    base_scale *= GetMapSizeModifier();

    return base_scale;
}

// Get monster damage scaling for current wave
float GetMonsterDamageScale(int wave_level) {
    float base_scale = CalculateSigmoid(static_cast<float>(wave_level), g_scalingConfig.monster_damage);

    // Apply difficulty modifiers
    base_scale *= GetDifficultyModifier();

    // Slightly reduce damage scaling on smaller maps
    if (GetCurrentMapSize().isSmallMap) {
        base_scale *= 0.95f;
    }

    return base_scale;
}

// Get player weapon scaling for current wave
float GetPlayerWeaponScale(int wave_level) {
    float base_scale = CalculateSigmoid(static_cast<float>(wave_level), g_scalingConfig.player_weapon_damage);

    // Reduce player damage scaling if insane mode is active
    if (g_insane && g_insane->integer > 0) {
        base_scale *= 0.9f;
    }

    return base_scale;
}

// Get item drop scaling based on tier (0=common, 1=rare, 2=legendary)
float GetItemDropScale(int wave_level, int item_tier) {
    const SigmoidParams* params = nullptr;

    switch (item_tier) {
        case 0: params = &g_scalingConfig.item_common_drops; break;
        case 1: params = &g_scalingConfig.item_rare_drops; break;
        case 2: params = &g_scalingConfig.item_legendary_drops; break;
        default: return 1.0f;
    }

    return CalculateSigmoid(static_cast<float>(wave_level), *params);
}

// Scale monster health with sigmoid curve
int ScaleMonsterHealth(int base_health, int wave_level, bool is_boss) {
    if (!g_config.use_sigmoid_scaling) {
        if (developer && developer->integer) {
            gi.Com_PrintFmt("ScaleMonsterHealth: Scaling disabled (use_sigmoid_scaling=false)\n");
        }
        return base_health;
    }

    if (g_config.use_sigmoid_scaling_bosses_only && !is_boss) {
        return base_health;
    }

    if (g_config.use_sigmoid_scaling_except_bosses && is_boss) {
        return base_health;
    }

    float scale = GetMonsterHealthScale(wave_level);

    // Bosses get additional scaling
    if (is_boss) {
        scale *= 1.5f;

        // Add some linear scaling for bosses to maintain challenge
        scale += (wave_level - 10) * 0.02f;
    }

    int scaled_health = static_cast<int>(base_health * scale);

    if (developer && developer->integer) {
        gi.Com_PrintFmt("ScaleMonsterHealth: wave={}, base={}, scale={:.2f}, scaled={}, is_boss={}\n",
            wave_level, base_health, scale, scaled_health, is_boss);
    }

    return scaled_health;
}

// Scale monster armor with sigmoid curve
int ScaleMonsterArmor(int base_armor, int wave_level) {
    if (!g_config.use_sigmoid_scaling) {
        return base_armor;
    }

    float scale = GetMonsterArmorScale(wave_level);
    return static_cast<int>(base_armor * scale);
}

// Scale weapon damage with sigmoid curve
int ScaleWeaponDamage(int base_damage, int wave_level, bool is_player_weapon) {
    if (!g_config.use_sigmoid_scaling) {
        return base_damage;
    }

    float scale = is_player_weapon ?
        GetPlayerWeaponScale(wave_level) :
        GetMonsterDamageScale(wave_level);

    return static_cast<int>(base_damage * scale);
}

// Scale item drop weight based on tier
float ScaleItemDropWeight(float base_weight, int wave_level, int item_tier) {
    if (!g_config.use_sigmoid_scaling) {
        return base_weight;
    }

    float scale = GetItemDropScale(wave_level, item_tier);
    return base_weight * scale;
}

// Get map size modifier
float GetMapSizeModifier() {
    const auto& mapSize = GetCurrentMapSize();

    if (mapSize.isSmallMap)
        return g_scalingConfig.map_size_small_modifier;
    else if (mapSize.isBigMap)
        return g_scalingConfig.map_size_large_modifier;
    else
        return g_scalingConfig.map_size_medium_modifier;
}

// Get difficulty modifier based on game settings
float GetDifficultyModifier() {
    float modifier = 1.0f;

    if (g_chaotic && g_chaotic->integer > 0) {
        modifier *= g_scalingConfig.chaos_modifier;
    }

    if (g_insane && g_insane->integer > 0) {
        modifier *= g_scalingConfig.insane_modifier;
    }

    if (g_hardcoop && g_hardcoop->integer > 0) {
        modifier *= 1.05f; // Slight additional difficulty for hardcoop
    }

    return modifier;
}

// Debug function to visualize scaling curves
void PrintScalingCurve(const char* name, const SigmoidParams& params, int max_wave) {
    gi.Com_PrintFmt("\n{} Scaling Curve:\n", name);
    gi.Com_Print("Wave | Scale | Graph\n");
    gi.Com_Print("-----|-------|");
    for (int i = 0; i < 50; i++) gi.Com_Print("-");
    gi.Com_Print("\n");

    for (int wave = 1; wave <= max_wave; wave += 5) {
        float scale = CalculateSigmoid(static_cast<float>(wave), params);
        float normalized = (scale - params.min_value) / (params.max_value - params.min_value);
        int bar_length = static_cast<int>(normalized * 40);

        gi.Com_PrintFmt("{:4d} | {:5.2f} | ", wave, scale);

        for (int i = 0; i < bar_length; i++) {
            gi.Com_Print("#");
        }
        gi.Com_Print("\n");
    }
}