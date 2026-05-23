#include "weapon_id.h"
#include <algorithm>
#include <utility>

namespace horde {

    using WeaponNameEntry = std::pair<std::string_view, WeaponID>;

    // Sorted by name for binary search. This gives the same locality benefit as
    // flat_map without runtime construction for a fixed 24-entry table.
    static constexpr std::array<WeaponNameEntry, 24> WEAPON_NAME_TO_ID = {{
        {"bfg",             WeaponID::BFG},
        {"blaster",         WeaponID::BLASTER},
        {"blaster2",        WeaponID::BLASTER2},
        {"blaster_bolt",    WeaponID::BLASTER_BOLT},
        {"blueblaster",     WeaponID::BLUEBLASTER},
        {"bolt",            WeaponID::BOLT},
        {"dabeam",          WeaponID::DABEAM},
        {"fireball",        WeaponID::FIREBALL},
        {"flechette",       WeaponID::FLECHETTE},
        {"grenade",         WeaponID::GRENADE},
        {"heat",            WeaponID::HEAT},
        {"heatbeam",        WeaponID::HEATBEAM},
        {"hyperblaster",    WeaponID::HYPERBLASTER},
        {"ionripper",       WeaponID::IONRIPPER},
        {"lightning",       WeaponID::LIGHTNING},
        {"machinegun",      WeaponID::MACHINEGUN},
        {"melee",           WeaponID::MELEE},
        {"plasma",          WeaponID::PLASMA},
        {"proboscis",       WeaponID::PROBOSCIS},
        {"railgun",         WeaponID::RAILGUN},
        {"rocket",          WeaponID::ROCKET},
        {"shotgun",         WeaponID::SHOTGUN},
        {"slam",            WeaponID::SLAM},
        {"tracker",         WeaponID::TRACKER}
    }};

    // Indexed directly by WeaponID for debug/reverse lookup.
    static constexpr std::array<const char*, static_cast<size_t>(WeaponID::MAX_WEAPONS)> WEAPON_ID_TO_NAME = {{
        "melee",
        "blaster",
        "blaster2",
        "blaster_bolt",
        "blueblaster",
        "shotgun",
        "machinegun",
        "grenade",
        "rocket",
        "heat",
        "railgun",
        "bfg",
        "ionripper",
        "hyperblaster",
        "bolt",
        "tracker",
        "plasma",
        "dabeam",
        "heatbeam",
        "slam",
        "lightning",
        "flechette",
        "fireball",
        "proboscis"
    }};

    static constexpr bool WeaponNameTableIsSorted() {
        for (size_t i = 1; i < WEAPON_NAME_TO_ID.size(); ++i) {
            if (WEAPON_NAME_TO_ID[i - 1].first >= WEAPON_NAME_TO_ID[i].first) {
                return false;
            }
        }
        return true;
    }

    static_assert(WEAPON_NAME_TO_ID.size() == static_cast<size_t>(WeaponID::MAX_WEAPONS));
    static_assert(WEAPON_ID_TO_NAME.size() == static_cast<size_t>(WeaponID::MAX_WEAPONS));
    static_assert(WeaponNameTableIsSorted());

    WeaponID WeaponRegistry::GetWeaponID(const char* weapon_name) {
        if (!weapon_name || !weapon_name[0]) [[unlikely]] {
            return WeaponID::UNKNOWN;
        }

        const std::string_view name{weapon_name};
        const auto it = std::lower_bound(
            WEAPON_NAME_TO_ID.begin(),
            WEAPON_NAME_TO_ID.end(),
            name,
            [](const WeaponNameEntry& entry, std::string_view value) {
                return entry.first < value;
            });

        if (it != WEAPON_NAME_TO_ID.end() && it->first == name) [[likely]] {
            return it->second;
        }
        return WeaponID::UNKNOWN;
    }

    bool WeaponRegistry::IsValidWeapon(WeaponID id) {
        return static_cast<size_t>(id) < WEAPON_ID_TO_NAME.size();
    }

    const char* WeaponRegistry::GetWeaponName(WeaponID id) {
        const size_t index = static_cast<size_t>(id);
        return (index < WEAPON_ID_TO_NAME.size()) ? WEAPON_ID_TO_NAME[index] : nullptr;
    }

} // namespace horde
