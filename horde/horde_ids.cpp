#include "horde_ids.h"
#include "g_horde.h" // Include g_horde.h for g_edicts if needed, or adjust include path
#include <algorithm>

namespace horde {

    // Global instances
    MonsterSpawnTimeTracker g_monsterSpawnTracker;
    SpawnPointTimeTracker g_spawnPointTimeTracker;

    // Static variables for the registries
    std::array<MapOriginRegistry::MapOrigin, static_cast<size_t>(MapID::MAX_MAPS)> MapOriginRegistry::s_origins;
    std::array<MapSize, static_cast<size_t>(MapID::MAX_MAPS)> MapOriginRegistry::s_mapSizes;
    bool MapOriginRegistry::s_initialized = false;

    // Map of classnames to type IDs - used during initialization
    static std::unordered_map<std::string_view, MonsterTypeID> s_monsterTypeMap;

    // Reverse map of type IDs to classnames - used for debugging
    static std::array<const char*, static_cast<size_t>(MonsterTypeID::MAX_TYPES)> s_typeToClassname;

    // Map of map names to MapIDs - used during initialization
    static std::unordered_map<std::string_view, MapID> s_mapIDMap;


    // Initialize the entire system
    void InitializeHordeIDs() {
        MonsterTypeRegistry::Initialize();
        MapOriginRegistry::Initialize(); // This will now initialize origins and sizes

        // Reset the trackers
        g_monsterSpawnTracker.Reset();
        g_spawnPointTimeTracker.Reset();
    }

    //
    // MonsterTypeRegistry implementation
    //

   void MonsterTypeRegistry::Initialize() {
        // Clear the maps
        s_monsterTypeMap.clear();

        // Fill in all monster type mappings
        s_monsterTypeMap = {
            // Basic infantry
            {"monster_soldier_light", MonsterTypeID::SOLDIER_LIGHT},       // 1
            {"monster_soldier", MonsterTypeID::SOLDIER},                     // 2
            {"monster_soldier_ss", MonsterTypeID::SOLDIER_SS},               // 3
            {"monster_soldier_hypergun", MonsterTypeID::SOLDIER_HYPERGUN},   // 4
            {"monster_soldier_ripper", MonsterTypeID::SOLDIER_RIPPER},       // 5
            {"monster_soldier_lasergun", MonsterTypeID::SOLDIER_LASERGUN},   // 6

            // Infantry units
            {"monster_infantry", MonsterTypeID::INFANTRY},                   // 7
            {"monster_infantry_vanilla", MonsterTypeID::INFANTRY_VANILLA},   // 8

            // Gunner types
            {"monster_gunner", MonsterTypeID::GUNNER},                       // 9
            {"monster_gunner_vanilla", MonsterTypeID::GUNNER_VANILLA},       // 10
            {"monster_guncmdr", MonsterTypeID::GUNCMDR},                     // 11
            {"monster_guncmdr_vanilla", MonsterTypeID::GUNCMDR_VANILLA},     // 12
            {"monster_guncmdrkl", MonsterTypeID::GUNCMDR_KL},                // 13

            // Support units
            {"monster_medic", MonsterTypeID::MEDIC},                         // 14
            {"monster_medic_commander", MonsterTypeID::MEDIC_COMMANDER},     // 15

            // Gladiator variants
            {"monster_gladiator", MonsterTypeID::GLADIATOR},                 // 16
            {"monster_gladb", MonsterTypeID::GLADIATOR_B},                   // 17
            {"monster_gladc", MonsterTypeID::GLADIATOR_C},                   // 18

            // Tank variants
            {"monster_tank", MonsterTypeID::TANK},                           // 19
            {"monster_tank_commander", MonsterTypeID::TANK_COMMANDER},       // 20
            {"monster_tank_64", MonsterTypeID::TANK_64},                     // 21
            {"monster_tank_spawner", MonsterTypeID::TANK_SPAWNER},           // 22
            {"monster_runnertank", MonsterTypeID::RUNNERTANK},               // 23

            // Chick variants
            {"monster_chick", MonsterTypeID::CHICK},                         // 24
            {"monster_chick_heat", MonsterTypeID::CHICK_HEAT},               // 25

            // Small units
            {"monster_parasite", MonsterTypeID::PARASITE},                   // 26
            {"monster_brain", MonsterTypeID::BRAIN},                         // 27
            {"monster_stalker", MonsterTypeID::STALKER},                     // 28

            // Flying units
            {"monster_flyer", MonsterTypeID::FLYER},                         // 29
            {"monster_hover", MonsterTypeID::HOVER},                         // 30
            {"monster_hover_vanilla", MonsterTypeID::HOVER_VANILLA},         // 31
            {"monster_floater", MonsterTypeID::FLOATER},                     // 32
            {"monster_floater_tracker", MonsterTypeID::FLOATER_TRACKER},     // 33
            {"monster_fixbot", MonsterTypeID::FIXBOT},                       // 34
            {"monster_fixbotkl", MonsterTypeID::FIXBOT_KL},                  // 35
            {"monster_daedalus", MonsterTypeID::DAEDALUS},                   // 36
            {"monster_daedalus_bomber", MonsterTypeID::DAEDALUS_BOMBER},     // 37

            // Mutant types
            {"monster_mutant", MonsterTypeID::MUTANT},                       // 38
            {"monster_redmutant", MonsterTypeID::REDMUTANT},                 // 39
            {"monster_berserk", MonsterTypeID::BERSERK},                     // 40
            {"monster_gekk", MonsterTypeID::GEKK},                           // 41

            // Arachnid types
            {"monster_spider", MonsterTypeID::SPIDER},                       // 42
            {"monster_arachnid2", MonsterTypeID::ARACHNID2},                 // 43
            {"monster_gm_arachnid", MonsterTypeID::GM_ARACHNID},             // 44
            {"monster_psxarachnid", MonsterTypeID::PSX_ARACHNID},            // 45

            // Shambler types
            {"monster_shambler", MonsterTypeID::SHAMBLER},                   // 46
            {"monster_shambler_small", MonsterTypeID::SHAMBLER_SMALL},       // 47
            {"monster_shamblerkl", MonsterTypeID::SHAMBLER_KL},              // 48

            // Special units
            {"monster_janitor", MonsterTypeID::JANITOR},                     // 49
            {"monster_janitor2", MonsterTypeID::JANITOR2},                   // 50
            {"monster_perrokl", MonsterTypeID::PERRO_KL},                    // 51

            // Boss units
            {"monster_boss2", MonsterTypeID::BOSS2},                         // 52
            {"monster_boss2_64", MonsterTypeID::BOSS2_64},                   // 53
            {"monster_boss2_mini", MonsterTypeID::BOSS2_MINI},               // 54
            {"monster_boss2kl", MonsterTypeID::BOSS2_KL},                    // 55
            {"monster_carrier", MonsterTypeID::CARRIER},                     // 56
            {"monster_carrier_mini", MonsterTypeID::CARRIER_MINI},           // 57
            {"monster_makron", MonsterTypeID::MAKRON},                       // 58
            {"monster_makronkl", MonsterTypeID::MAKRON_KL},                  // 59
            {"monster_jorg", MonsterTypeID::JORG},                           // 60
            {"monster_jorg_small", MonsterTypeID::JORG_SMALL},               // 61
            {"monster_widow", MonsterTypeID::WIDOW},                         // 62
            {"monster_widow1", MonsterTypeID::WIDOW1},                       // 63
            {"monster_widow2", MonsterTypeID::WIDOW2},                       // 64
            {"monster_boss5", MonsterTypeID::BOSS5},                         // 65
            {"monster_guardian", MonsterTypeID::GUARDIAN},                   // 66
            {"monster_psxguardian", MonsterTypeID::PSX_GUARDIAN},            // 67

             {"misc_insane", MonsterTypeID::MISC_INSANE},                     // 68
            {"monster_turret", MonsterTypeID::TURRET}    ,                    // 69

            {"monster_sentrygun", MonsterTypeID::SENTRYGUN}                    // 70
        };
    
        // Initialize the reverse mapping
        s_typeToClassname.fill(nullptr); // Ensure it's clean first
        for (const auto& [classname, id] : s_monsterTypeMap) {
            size_t index = static_cast<size_t>(id);
            if (index < s_typeToClassname.size()) {
                s_typeToClassname[index] = classname.data();
            }
        }
    }

    MonsterTypeID MonsterTypeRegistry::GetTypeID(const char* classname) {
        if (!classname || !classname[0]) {
            return MonsterTypeID::UNKNOWN;
        }

        auto it = s_monsterTypeMap.find(classname);
        return (it != s_monsterTypeMap.end()) ? it->second : MonsterTypeID::UNKNOWN;
    }

    bool MonsterTypeRegistry::IsValidType(MonsterTypeID id) {
        size_t index = static_cast<size_t>(id);
        return index < static_cast<size_t>(MonsterTypeID::MAX_TYPES) && s_typeToClassname[index] != nullptr;
    }

    const char* MonsterTypeRegistry::GetClassname(MonsterTypeID id) {
        size_t index = static_cast<size_t>(id);
        return (index < static_cast<size_t>(MonsterTypeID::MAX_TYPES)) ? s_typeToClassname[index] : nullptr;
    }

    //
    // MonsterSpawnTimeTracker implementation
    //

    void MonsterSpawnTimeTracker::Reset() {
        m_lastSpawnTimes.fill(0_sec);
    }

    void MonsterSpawnTimeTracker::SetLastSpawnTime(const char* classname, gtime_t time) {
        SetLastSpawnTime(MonsterTypeRegistry::GetTypeID(classname), time);
    }

    void MonsterSpawnTimeTracker::SetLastSpawnTime(MonsterTypeID typeId, gtime_t time) {
        size_t index = static_cast<size_t>(typeId);
        if (index < m_lastSpawnTimes.size()) {
            m_lastSpawnTimes[index] = time;
        }
    }

    gtime_t MonsterSpawnTimeTracker::GetLastSpawnTime(const char* classname) const {
        return GetLastSpawnTime(MonsterTypeRegistry::GetTypeID(classname));
    }

    gtime_t MonsterSpawnTimeTracker::GetLastSpawnTime(MonsterTypeID typeId) const {
        size_t index = static_cast<size_t>(typeId);
        return (index < m_lastSpawnTimes.size()) ? m_lastSpawnTimes[index] : 0_sec;
    }

    //
    // SpawnPointTimeTracker implementation
    //

    void SpawnPointTimeTracker::Reset() {
        m_lastSpawnTimes.fill(0_sec);
    }

    void SpawnPointTimeTracker::SetLastSpawnTime(const edict_t* point, gtime_t time) {
        if (!point) {
            return;
        }

        // Calculate index safely relative to g_edicts
        // Ensure g_edicts is accessible here (might need adjustment based on your project structure)
        size_t index = point - g_edicts;
        if (index < m_lastSpawnTimes.size()) {
            m_lastSpawnTimes[index] = time;
        }
    }

    gtime_t SpawnPointTimeTracker::GetLastSpawnTime(const edict_t* point) const {
        if (!point) {
            return 0_sec;
        }

        size_t index = point - g_edicts;
        return (index < m_lastSpawnTimes.size()) ? m_lastSpawnTimes[index] : 0_sec;
    }

    //
    // MapOriginRegistry implementation
    //

    void MapOriginRegistry::Initialize() {
        if (s_initialized) {
            return;
        }

        // Initialize the map ID lookup first
        s_mapIDMap = {
            // Standard Q2 maps
            {"q2dm1", MapID::Q2DM1}, {"q2dm2", MapID::Q2DM2}, {"q2dm3", MapID::Q2DM3},
            {"q2dm4", MapID::Q2DM4}, {"q2dm5", MapID::Q2DM5}, {"q2dm6", MapID::Q2DM6},
            {"q2dm7", MapID::Q2DM7}, {"q2dm8", MapID::Q2DM8},

            // Q2CTF maps
            {"q2ctf4", MapID::Q2CTF4}, {"q2ctf5", MapID::Q2CTF5},

            // RDM maps
            {"rdm2", MapID::RDM2},  {"rdm1", MapID::RDM1},
            {"rdm4", MapID::RDM4}, {"rdm5", MapID::RDM5}, {"rdm6", MapID::RDM6},
            {"rdm8", MapID::RDM8}, {"rdm9", MapID::RDM9}, {"rdm12", MapID::RDM12},

            // XDM maps
            {"xdm1", MapID::XDM1}, {"xdm2", MapID::XDM2}, {"xdm3", MapID::XDM3},
            {"xdm4", MapID::XDM4}, {"xdm6", MapID::XDM6},

            // Q64 maps
            {"q64/dm1", MapID::Q64_DM1}, {"q64/dm2", MapID::Q64_DM2}, {"q64/dm3", MapID::Q64_DM3},
            {"q64/dm4", MapID::Q64_DM4}, {"q64/dm6", MapID::Q64_DM6}, {"q64/dm7", MapID::Q64_DM7},
            {"q64/dm8", MapID::Q64_DM8}, {"q64/dm9", MapID::Q64_DM9}, {"q64/dm10", MapID::Q64_DM10},
            {"q64/command", MapID::Q64_COMMAND}, {"q64/comm", MapID::Q64_COMM},

            // MGU maps
            {"mgu3m4", MapID::MGU3M4}, {"mgu4trial", MapID::MGU4TRIAL}, {"mgu6m3", MapID::MGU6M3},
            {"mgu6trial", MapID::MGU6TRIAL}, {"mgdm1", MapID::MGDM1},

            // Special maps
            {"rboss", MapID::RBOSS}, {"industry", MapID::INDUSTRY}, {"fact3", MapID::FACT3},
            {"waste2", MapID::WASTE2}, {"ndctf0", MapID::NDCTF0}, {"xintell", MapID::XINTELL},
            {"sewer64", MapID::SEWER64}, {"base64", MapID::BASE64}, {"city64", MapID::CITY64},

            // EC maps
            {"ec/base_ec", MapID::EC_BASE_EC},

            // Old maps
            {"old/kmdm3", MapID::OLD_KMDM3},

            // E3 maps
            {"e3/jail_e3", MapID::E3_JAIL_E3},

            // Test maps
            {"test/mals_barrier_test", MapID::TEST_MALS_BARRIER_TEST},
            {"test/spbox", MapID::TEST_SPBOX}, {"test/test_kaiser", MapID::TEST_TEST_KAISER}
        };

        // Clear all origins
        s_origins.fill({ vec3_origin, false });

        // Define the origins using the original data from mapOrigins
        // Q2DM1
        s_origins[static_cast<size_t>(MapID::Q2DM1)] = { {vec3_t{1184, 568, 704}}, true };
        // Q2DM2
        s_origins[static_cast<size_t>(MapID::Q2DM2)] = { {vec3_t{128, -960, 704}}, true };
        // Q2DM3
        s_origins[static_cast<size_t>(MapID::Q2DM3)] = { {vec3_t{192, -136, 72}}, true };
        // Q2DM4
        s_origins[static_cast<size_t>(MapID::Q2DM4)] = { {vec3_t{504, 876, 292}}, true };
        // Q2DM5
        s_origins[static_cast<size_t>(MapID::Q2DM5)] = { {vec3_t{48, 952, 376}}, true };
        // Q2DM6
        s_origins[static_cast<size_t>(MapID::Q2DM6)] = { {vec3_t{496, 1392, -88}}, true };
        // Q2DM7
        s_origins[static_cast<size_t>(MapID::Q2DM7)] = { {vec3_t{816, 832, 56}}, true };
        // Q2DM8
        s_origins[static_cast<size_t>(MapID::Q2DM8)] = { {vec3_t{112, 1216, 88}}, true };
        // RDM4
        s_origins[static_cast<size_t>(MapID::RDM4)] = { {vec3_t{-336, 2456, -288}}, true };
        // RDM5
        s_origins[static_cast<size_t>(MapID::RDM5)] = { {vec3_t{1088, 592, -568}}, true };
        // RDM6
        s_origins[static_cast<size_t>(MapID::RDM6)] = { {vec3_t{712, 1328, 48}}, true };
        // RDM8
        s_origins[static_cast<size_t>(MapID::RDM8)] = { {vec3_t{-1516, 976, -156}}, true };
        // RDM9
        s_origins[static_cast<size_t>(MapID::RDM9)] = { {vec3_t{-984, -80, 232}}, true };
        // RDM12
        s_origins[static_cast<size_t>(MapID::RDM12)] = { {vec3_t{32, -1888, 120}}, true };
        // Q2CTF4
        s_origins[static_cast<size_t>(MapID::Q2CTF4)] = { {vec3_t{-2390, 1112, 218}}, true };
        // Q2CTF5
        s_origins[static_cast<size_t>(MapID::Q2CTF5)] = { {vec3_t{2432, -960, 168}}, true };
        // RBOSS
        s_origins[static_cast<size_t>(MapID::RBOSS)] = { {vec3_t{856, -2080, 32}}, true };
        // NDCTF0
        s_origins[static_cast<size_t>(MapID::NDCTF0)] = { {vec3_t{-608, -304, 184}}, true };
        // XDM1
        s_origins[static_cast<size_t>(MapID::XDM1)] = { {vec3_t{-312, 600, 144}}, true };
        // XDM2
        s_origins[static_cast<size_t>(MapID::XDM2)] = { {vec3_t{-232, 472, 424}}, true };
        // XDM3
        s_origins[static_cast<size_t>(MapID::XDM3)] = { {vec3_t{96, -96, 360}}, true };
        // XDM4
        s_origins[static_cast<size_t>(MapID::XDM4)] = { {vec3_t{-160, -368, 360}}, true };
        // XDM6
        s_origins[static_cast<size_t>(MapID::XDM6)] = { {vec3_t{-1088, -128, 528}}, true };
        // INDUSTRY
        s_origins[static_cast<size_t>(MapID::INDUSTRY)] = { {vec3_t{-1009, -545, 79}}, true };
        // MGU3M4
        s_origins[static_cast<size_t>(MapID::MGU3M4)] = { {vec3_t{3312, 3344, 864}}, true };
        // MGDM1
        s_origins[static_cast<size_t>(MapID::MGDM1)] = { {vec3_t{176, 64, 288}}, true };
        // MGU6TRIAL
        s_origins[static_cast<size_t>(MapID::MGU6TRIAL)] = { {vec3_t{-848, 176, 96}}, true };
        // FACT3
        s_origins[static_cast<size_t>(MapID::FACT3)] = { {vec3_t{0, -64, 192}}, true };
        // MGU4TRIAL
        s_origins[static_cast<size_t>(MapID::MGU4TRIAL)] = { {vec3_t{-960, -528, -328}}, true };
        // MGU6M3
        s_origins[static_cast<size_t>(MapID::MGU6M3)] = { {vec3_t{0, 592, 1600}}, true };
        // WASTE2
        s_origins[static_cast<size_t>(MapID::WASTE2)] = { {vec3_t{-1152, -288, -40}}, true };
        // Q64/COMM
        s_origins[static_cast<size_t>(MapID::Q64_COMM)] = { {vec3_t{1464, -88, -432}}, true };
        // Q64/COMMAND
        s_origins[static_cast<size_t>(MapID::Q64_COMMAND)] = { {vec3_t{0, -208, 56}}, true };
        // Q64/DM7
        s_origins[static_cast<size_t>(MapID::Q64_DM7)] = { {vec3_t{64, 224, 120}}, true };
        // Q64/DM1
        s_origins[static_cast<size_t>(MapID::Q64_DM1)] = { {vec3_t{-192, -320, 80}}, true };
        // Q64/DM2
        s_origins[static_cast<size_t>(MapID::Q64_DM2)] = { {vec3_t{840, 80, 96}}, true };
        // Q64/DM3
        s_origins[static_cast<size_t>(MapID::Q64_DM3)] = { {vec3_t{488, 392, 64}}, true };
        // Q64/DM4
        s_origins[static_cast<size_t>(MapID::Q64_DM4)] = { {vec3_t{176, 272, -24}}, true };
        // Q64/DM6
        s_origins[static_cast<size_t>(MapID::Q64_DM6)] = { {vec3_t{-1568, 1680, 144}}, true };
        // Q64/DM8
        s_origins[static_cast<size_t>(MapID::Q64_DM8)] = { {vec3_t{-800, 448, 56}}, true };
        // Q64/DM9
        s_origins[static_cast<size_t>(MapID::Q64_DM9)] = { {vec3_t{160, 56, 40}}, true };
        // Q64/DM10
        s_origins[static_cast<size_t>(MapID::Q64_DM10)] = { {vec3_t{-304, 512, -92}}, true };
        // EC/BASE_EC
        s_origins[static_cast<size_t>(MapID::EC_BASE_EC)] = { {vec3_t{-112, 704, 128}}, true };
        // OLD/KMDM3
        s_origins[static_cast<size_t>(MapID::OLD_KMDM3)] = { {vec3_t{-480, -572, 144}}, true };
        // TEST/MALS_BARRIER_TEST
        s_origins[static_cast<size_t>(MapID::TEST_MALS_BARRIER_TEST)] = { {vec3_t{24, 136, 224}}, true };
        // TEST/SPBOX
        s_origins[static_cast<size_t>(MapID::TEST_SPBOX)] = { {vec3_t{112, 192, 168}}, true };
        // TEST/TEST_KAISER
        s_origins[static_cast<size_t>(MapID::TEST_TEST_KAISER)] = { {vec3_t{1344, 176, -8}}, true };
        // E3/JAIL_E3
        s_origins[static_cast<size_t>(MapID::E3_JAIL_E3)] = { {vec3_t{-572, -1312, 76}}, true };
        // XINTELL
        s_origins[static_cast<size_t>(MapID::XINTELL)] = { {vec3_t{2096, -992, 376}}, true };

        // Initialize Map Sizes
        // Default all to Medium
        s_mapSizes.fill({ false, false, true });

        // Define Small Maps using MapID
        const MapID smallMapIDs[] = {
            MapID::Q2DM3, MapID::Q2DM7, MapID::Q2DM2, MapID::Q64_DM10, MapID::TEST_MALS_BARRIER_TEST,
            MapID::Q64_DM9, MapID::Q64_DM7, MapID::Q64_DM2, MapID::TEST_SPBOX,
            MapID::Q64_DM1, MapID::FACT3, MapID::Q2CTF4, MapID::RDM4, MapID::Q64_COMMAND, MapID::MGU3M4,
            MapID::MGU4TRIAL, MapID::MGU6TRIAL, MapID::EC_BASE_EC, MapID::MGDM1, MapID::NDCTF0, MapID::Q64_DM6,
            MapID::Q64_DM8, MapID::Q64_DM4, MapID::Q64_DM3, MapID::INDUSTRY, MapID::E3_JAIL_E3
        };
        for (MapID id : smallMapIDs) {
            size_t index = static_cast<size_t>(id);
            if (index < s_mapSizes.size()) {
                s_mapSizes[index] = { true, false, false }; // isSmall=true, isBig=false, isMedium=false
            }
        }

        // Define Big Maps using MapID
        const MapID bigMapIDs[] = {
            MapID::Q2CTF5, MapID::OLD_KMDM3, MapID::XDM2, MapID::XDM4, MapID::XDM6, MapID::XDM3, MapID::RDM6,
            MapID::RDM8, MapID::XDM1, MapID::WASTE2, MapID::RDM5, MapID::RDM9, MapID::RDM12, MapID::XINTELL,
            MapID::SEWER64, MapID::BASE64, MapID::CITY64, MapID::RDM1, MapID::RDM2
        };
        for (MapID id : bigMapIDs) {
            size_t index = static_cast<size_t>(id);
            if (index < s_mapSizes.size()) {
                s_mapSizes[index] = { false, true, false }; // isSmall=false, isBig=true, isMedium=false
            }
        }
        // All other maps remain Medium by default.

        s_initialized = true;
    }

    MapID MapOriginRegistry::GetMapID(const char* map_name) {
        if (!map_name || !map_name[0]) {
            return MapID::UNKNOWN;
        }

        // Make sure the registry is initialized
        if (!s_initialized) {
            Initialize(); // Should be safe to call multiple times due to s_initialized check
        }

        auto it = s_mapIDMap.find(map_name);
        return (it != s_mapIDMap.end()) ? it->second : MapID::UNKNOWN;
    }

    bool MapOriginRegistry::GetOrigin(const char* map_name, vec3_t& out_origin) {
        return GetOrigin(GetMapID(map_name), out_origin);
    }

    bool MapOriginRegistry::GetOrigin(MapID mapId, vec3_t& out_origin) {
        // Make sure the registry is initialized
        if (!s_initialized) {
            Initialize();
        }

        size_t index = static_cast<size_t>(mapId);
        if (index >= s_origins.size() || !s_origins[index].is_valid) {
            return false;
        }

        out_origin = s_origins[index].origin;
        return true;
    }

    MapSize MapOriginRegistry::GetMapSize(MapID mapId) {
        // Make sure the registry is initialized
        if (!s_initialized) {
            Initialize();
        }

        size_t index = static_cast<size_t>(mapId);
        // Return the size from the array, or default Medium if ID is invalid/out of bounds
        if (index < s_mapSizes.size() && mapId != MapID::UNKNOWN) {
            return s_mapSizes[index];
        }

        // Default to Medium for unknown or out-of-bounds IDs
        return { false, false, true };
    }

} // namespace horde