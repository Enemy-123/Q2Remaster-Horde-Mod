#pragma once

#include <cstdint>
#include <array>
#include <string_view>
#include <boost/container/flat_map.hpp>

namespace horde {

    // Weapon IDs for fast array-based lookups instead of string comparisons
    // This eliminates the performance bottleneck of string hashing in hot paths
    enum class WeaponID : uint8_t {
        MELEE = 0,
        BLASTER = 1,
        BLASTER2 = 2,
        BLASTER_BOLT = 3,
        BLUEBLASTER = 4,
        SHOTGUN = 5,
        MACHINEGUN = 6,
        GRENADE = 7,
        ROCKET = 8,
        HEAT = 9,
        RAILGUN = 10,
        BFG = 11,
        IONRIPPER = 12,
        HYPERBLASTER = 13,
        BOLT = 14,
        TRACKER = 15,
        PLASMA = 16,
        DABEAM = 17,
        HEATBEAM = 18,
        SLAM = 19,
        LIGHTNING = 20,
        FLECHETTE = 21,
        FIREBALL = 22,
        PROBOSCIS = 23,

        MAX_WEAPONS = 24,  // Total number of weapons
        UNKNOWN = 255      // Invalid weapon
    };

    // Class for efficient weapon type lookups
    class WeaponRegistry {
    public:
        // Initialize the registry with all weapon mappings
        static void Initialize();

        // Get weapon ID from weapon name string
        static WeaponID GetWeaponID(const char* weapon_name);

        // Check if a weapon ID is valid
        static bool IsValidWeapon(WeaponID id);

        // For debugging - get weapon name from ID
        static const char* GetWeaponName(WeaponID id);
    };

    // Initialize weapon registry (called as part of InitializeHordeIDs)
    void InitializeWeaponIDs();

} // namespace horde
