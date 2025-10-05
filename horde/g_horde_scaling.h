#pragma once

#include "../shared.h"
#include <algorithm>
#include <cmath>

// Sigmoid curve parameters for different scaling types
struct SigmoidParams {
    float midpoint;      // Wave where 50% of max scaling is reached
    float growth_rate;   // How steep the curve is (0.1 = gradual, 0.3 = steep)
    float min_value;     // Minimum multiplier
    float max_value;     // Maximum multiplier
};

// Scaling configuration for all game elements
struct ScalingConfig {
    // Monster scaling
    SigmoidParams monster_health;
    SigmoidParams monster_armor;
    SigmoidParams monster_damage;

    // Player scaling
    SigmoidParams player_weapon_damage;
    SigmoidParams player_ammo_drops;

    // Item scaling
    SigmoidParams item_common_drops;
    SigmoidParams item_rare_drops;
    SigmoidParams item_legendary_drops;

    // Difficulty modifiers
    float chaos_modifier;
    float insane_modifier;
    float map_size_small_modifier;
    float map_size_medium_modifier;
    float map_size_large_modifier;
};

// Global scaling configuration
extern ScalingConfig g_scalingConfig;

// Initialize scaling system from config
void InitializeScalingSystem();
void LoadScalingConfig();

// Core sigmoid function
// Returns a value between min and max based on sigmoid curve
float CalculateSigmoid(float current_wave, const SigmoidParams& params);

// Specific scaling functions
float GetMonsterHealthScale(int wave_level);
float GetMonsterArmorScale(int wave_level);
float GetMonsterDamageScale(int wave_level);
float GetPlayerWeaponScale(int wave_level);
float GetItemDropScale(int wave_level, int item_tier);

// Apply scaling to specific values
int ScaleMonsterHealth(int base_health, int wave_level, bool is_boss = false);
int ScaleMonsterArmor(int base_armor, int wave_level);
int ScaleWeaponDamage(int base_damage, int wave_level, bool is_player_weapon = true);
float ScaleItemDropWeight(float base_weight, int wave_level, int item_tier);

// Difficulty curve visualization (for debugging)
void PrintScalingCurve(const char* name, const SigmoidParams& params, int max_wave = 50);

// Map size and difficulty adjustments
float GetMapSizeModifier();
float GetDifficultyModifier();