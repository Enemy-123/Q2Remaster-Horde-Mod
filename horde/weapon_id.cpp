#include "weapon_id.h"

namespace horde {

    static bool s_weaponRegistryInitialized = false;

    // Use flat_map for cache-friendly lookups with excellent locality
    // O(log 24) ≈ 5 comparisons, but on contiguous sorted array = faster than hashing
    static boost::container::flat_map<std::string_view, WeaponID> s_weaponIDMap;

    // Reverse map of weapon IDs to names - used for debugging
    static std::array<const char*, static_cast<size_t>(WeaponID::MAX_WEAPONS)> s_weaponIDToName;

    //
    // WeaponRegistry implementation
    //

    void WeaponRegistry::Initialize() {
        if (s_weaponRegistryInitialized) {
            return;
        }

        // Map all weapon names to their IDs
        // flat_map will automatically keep these sorted for O(log n) binary search
        s_weaponIDMap = {
            {"melee",           WeaponID::MELEE},
            {"blaster",         WeaponID::BLASTER},
            {"blaster2",        WeaponID::BLASTER2},
            {"blaster_bolt",    WeaponID::BLASTER_BOLT},
            {"blueblaster",     WeaponID::BLUEBLASTER},
            {"shotgun",         WeaponID::SHOTGUN},
            {"machinegun",      WeaponID::MACHINEGUN},
            {"grenade",         WeaponID::GRENADE},
            {"rocket",          WeaponID::ROCKET},
            {"heat",            WeaponID::HEAT},
            {"railgun",         WeaponID::RAILGUN},
            {"bfg",             WeaponID::BFG},
            {"ionripper",       WeaponID::IONRIPPER},
            {"hyperblaster",    WeaponID::HYPERBLASTER},
            {"bolt",            WeaponID::BOLT},
            {"tracker",         WeaponID::TRACKER},
            {"plasma",          WeaponID::PLASMA},
            {"dabeam",          WeaponID::DABEAM},
            {"heatbeam",        WeaponID::HEATBEAM},
            {"slam",            WeaponID::SLAM},
            {"lightning",       WeaponID::LIGHTNING},
            {"flechette",       WeaponID::FLECHETTE},
            {"fireball",        WeaponID::FIREBALL},
            {"proboscis",       WeaponID::PROBOSCIS}
        };

        // Initialize the reverse mapping
        s_weaponIDToName.fill(nullptr); // Ensure it's clean first
        for (const auto& [weapon_name, id] : s_weaponIDMap) {
            size_t index = static_cast<size_t>(id);
            if (index < s_weaponIDToName.size()) {
                s_weaponIDToName[index] = weapon_name.data();
            }
        }

        s_weaponRegistryInitialized = true;
    }

    WeaponID WeaponRegistry::GetWeaponID(const char* weapon_name) {
        if (!weapon_name || !weapon_name[0]) [[unlikely]] {
            return WeaponID::UNKNOWN;
        }

        auto it = s_weaponIDMap.find(weapon_name);
        if (it != s_weaponIDMap.end()) [[likely]] {
            return it->second;
        }
        return WeaponID::UNKNOWN;
    }

    bool WeaponRegistry::IsValidWeapon(WeaponID id) {
        size_t index = static_cast<size_t>(id);
        return index < static_cast<size_t>(WeaponID::MAX_WEAPONS) && s_weaponIDToName[index] != nullptr;
    }

    const char* WeaponRegistry::GetWeaponName(WeaponID id) {
        size_t index = static_cast<size_t>(id);
        return (index < static_cast<size_t>(WeaponID::MAX_WEAPONS)) ? s_weaponIDToName[index] : nullptr;
    }

    //
    // Initialization function
    //

    void InitializeWeaponIDs() {
        WeaponRegistry::Initialize();
    }

} // namespace horde
