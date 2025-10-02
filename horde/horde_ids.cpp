#include "horde_ids.h"
#include "g_horde.h"
#include <algorithm>

namespace horde {

    static bool s_specialTypeRegistryInitialized = false;
    static bool s_monsterTypeRegistryInitialized = false;

    // Global instances
    MonsterSpawnTimeTracker g_monsterSpawnTracker;
    SpawnPointTimeTracker g_spawnPointTimeTracker;

    // Static variables for the registries
    std::array<MapOriginRegistry::MapOrigin, static_cast<size_t>(MapID::MAX_MAPS)> MapOriginRegistry::s_origins;
    std::array<MapSize, static_cast<size_t>(MapID::MAX_MAPS)> MapOriginRegistry::s_mapSizes;
    bool MapOriginRegistry::s_initialized = false;

    // Use string_view for zero-copy lookups with static string literals
    static std::unordered_map<std::string_view, SpecialEntityTypeID> s_specialTypeMap;
    static std::unordered_map<std::string_view, MonsterTypeID> s_monsterTypeMap;
    static std::unordered_map<std::string_view, MapID> s_mapIDMap;
    // Reverse map of type IDs to classnames - used for debugging
    static std::array<const char*, static_cast<size_t>(MonsterTypeID::MAX_TYPES)> s_typeToClassname;

    // Initialize the entire system
    void InitializeHordeIDs() {
        MonsterTypeRegistry::Initialize();
        MapOriginRegistry::Initialize(); // This will now initialize origins and sizes
        SpecialTypeRegistry::Initialize();

        // Reset the trackers
        g_monsterSpawnTracker.Reset();
        g_spawnPointTimeTracker.Reset();
    }

    //
    // SpecialTypeRegistry implementation
    //

    void SpecialTypeRegistry::Initialize() {
        if (s_specialTypeRegistryInitialized) {
            return;
        }

        s_specialTypeMap.clear();
        s_specialTypeMap.reserve(16); // Reserve space for all special entity types
        s_specialTypeMap = {
            {"tesla_mine",           SpecialEntityTypeID::TESLA_MINE},
            {"food_cube_trap",       SpecialEntityTypeID::FOOD_CUBE_TRAP},
            {"prox_mine",            SpecialEntityTypeID::PROX_MINE},
            {"monster_sentrygun",    SpecialEntityTypeID::SENTRY_GUN},
            {"monster_turret",       SpecialEntityTypeID::TURRET},
            {"nuke",                 SpecialEntityTypeID::NUKE_MINE},
            {"emitter",              SpecialEntityTypeID::LASER_EMITTER},
            {"laser",                SpecialEntityTypeID::LASER_BEAM},
            {"doppleganger",         SpecialEntityTypeID::DOPPLEGANGER},
            {"strogg_summoner_base", SpecialEntityTypeID::STROGG_SUMMONER},
            {"horde_barrel",         SpecialEntityTypeID::BARREL}
        };

        s_specialTypeRegistryInitialized = true;
    }

    SpecialEntityTypeID SpecialTypeRegistry::GetTypeID(const char* classname) {
        if (!classname || !classname[0]) [[unlikely]] {
            return SpecialEntityTypeID::UNKNOWN;
        }
        auto it = s_specialTypeMap.find(classname);
        if (it != s_specialTypeMap.end()) [[likely]] {
            return it->second;
        }
        return SpecialEntityTypeID::UNKNOWN;
    }

    //
    // MonsterTypeRegistry implementation
    //

    void MonsterTypeRegistry::Initialize() {
        if (s_monsterTypeRegistryInitialized) {
            return;
        }

        s_monsterTypeMap.clear();
        s_monsterTypeMap.reserve(128); // Reserve space for all monster types

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
            {"monster_chickkl", MonsterTypeID::CHICKKL},                     // 26

            // Small units
            {"monster_parasite", MonsterTypeID::PARASITE},                   // 27
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

            // Special fog wave bosses
            {"monster_berserkerkl", MonsterTypeID::BERSERKERKL},             // 42
            {"monster_gekkkl", MonsterTypeID::GEKKKL},                       // 43

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
            {"monster_turret", MonsterTypeID::TURRET},                    // 69

            {"monster_sentrygun", MonsterTypeID::SENTRYGUN},                // 70

            {"monster_boss3_stand", MonsterTypeID::BOSS3_STAND}, // 71
            {"misc_eastertank", MonsterTypeID::EASTERTANK}, // 72
            {"misc_easterchick", MonsterTypeID::EASTERCHICK},   // 73
            {"misc_easterchick2", MonsterTypeID::EASTERCHICK2}, // 74
            {"monster_commander_body", MonsterTypeID::COMMANDER_BODY},  // 75
            {"misc_bigviper", MonsterTypeID::BIGVIPER}, // 76
            {"monster_kamikaze", MonsterTypeID::KAMIKAZE}, // 77
            {"monster_arachnid", MonsterTypeID::ARACHNID}, // 78
            {"monster_supertank", MonsterTypeID::SUPERTANK}, // 79
            {"monster_supertankkl", MonsterTypeID::SUPERTANKKL}, // 80
            {"monster_flipper", MonsterTypeID::FLIPPER}, // 80
        };
    
        // Initialize the reverse mapping
        s_typeToClassname.fill(nullptr); // Ensure it's clean first
        for (const auto& [classname, id] : s_monsterTypeMap) {
            size_t index = static_cast<size_t>(id);
            if (index < s_typeToClassname.size()) {
                s_typeToClassname[index] = classname.data();
            }
        }
          s_monsterTypeRegistryInitialized = true;
    }

    MonsterTypeID MonsterTypeRegistry::GetTypeID(const char* classname) {
        if (!classname || !classname[0]) [[unlikely]] {
            return MonsterTypeID::UNKNOWN;
        }

        auto it = s_monsterTypeMap.find(classname);
        if (it != s_monsterTypeMap.end()) [[likely]] {
            return it->second;
        }
        return MonsterTypeID::UNKNOWN;
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
    // Using vector for O(1) access since entity numbers are sequential
    //

    void SpawnPointTimeTracker::EnsureCapacity(int entity_num) {
        if (entity_num >= static_cast<int>(m_lastSpawnTimes.size())) {
            m_lastSpawnTimes.resize(entity_num + 1, 0_sec);
        }
    }

    void SpawnPointTimeTracker::Reset() {
        m_lastSpawnTimes.clear();
    }

    void SpawnPointTimeTracker::SetLastSpawnTime(const edict_t* point, gtime_t time) {
        if (!point) [[unlikely]] return;

        int entity_num = point->s.number;
        EnsureCapacity(entity_num);
        m_lastSpawnTimes[entity_num] = time;
    }

    gtime_t SpawnPointTimeTracker::GetLastSpawnTime(const edict_t* point) const {
        if (!point) [[unlikely]] return 0_sec;

        int entity_num = point->s.number;
        if (entity_num < static_cast<int>(m_lastSpawnTimes.size())) [[likely]] {
            return m_lastSpawnTimes[entity_num];
        }
        return 0_sec;
    }

    //
    // MapOriginRegistry implementation
    //

    void MapOriginRegistry::Initialize() {
        if (s_initialized) {
            return;
        }

        // Initialize the map ID lookup first
        s_mapIDMap.clear();
        s_mapIDMap.reserve(64); // Reserve space for all map IDs
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
            {"xdm4", MapID::XDM4}, {"xdm5", MapID::XDM5}, {"xdm6", MapID::XDM6},

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

        // Clear all origins (note: struct now has bool first, then vec3_t)
        s_origins.fill({ false, vec3_origin });

        // Define the origins using the original data from mapOrigins
        // Q2DM1
        s_origins[static_cast<size_t>(MapID::Q2DM1)] = { true, {1184, 568, 704} };
        // Q2DM2
        s_origins[static_cast<size_t>(MapID::Q2DM2)] = { true, {128, -960, 704} };
        // Q2DM3
        s_origins[static_cast<size_t>(MapID::Q2DM3)] = { true, {192, -136, 72} };
        // Q2DM4
        s_origins[static_cast<size_t>(MapID::Q2DM4)] = { true, {504, 876, 292} };
        // Q2DM5
        s_origins[static_cast<size_t>(MapID::Q2DM5)] = { true, {48, 952, 376} };
        // Q2DM6
        s_origins[static_cast<size_t>(MapID::Q2DM6)] = { true, {496, 1392, -88} };
        // Q2DM7
        s_origins[static_cast<size_t>(MapID::Q2DM7)] = { true, {816, 832, 56} };
        // Q2DM8
        s_origins[static_cast<size_t>(MapID::Q2DM8)] = { true, {112, 1216, 88} };
        // RDM1
        s_origins[static_cast<size_t>(MapID::RDM1)] = { true, {410, 373, 30} };
        // RDM2
        s_origins[static_cast<size_t>(MapID::RDM2)] = { true, {-1104, -483, 370} };
        // RDM4
        s_origins[static_cast<size_t>(MapID::RDM4)] = { true, {-336, 2456, -288} };
        // RDM5
        s_origins[static_cast<size_t>(MapID::RDM5)] = { true, {1088, 592, -568} };
        // RDM6
        s_origins[static_cast<size_t>(MapID::RDM6)] = { true, {712, 1328, 48} };
        // RDM8
        s_origins[static_cast<size_t>(MapID::RDM8)] = { true, {-1516, 976, -156} };
        // RDM9
        s_origins[static_cast<size_t>(MapID::RDM9)] = { true, {-984, -80, 232} };
        // RDM12
        s_origins[static_cast<size_t>(MapID::RDM12)] = { true, {32, -1888, 120} };
        // Q2CTF4
        s_origins[static_cast<size_t>(MapID::Q2CTF4)] = { true, {-2390, 1112, 218} };
        // Q2CTF5
        s_origins[static_cast<size_t>(MapID::Q2CTF5)] = { true, {2432, -960, 168} };
        // RBOSS
        s_origins[static_cast<size_t>(MapID::RBOSS)] = { true, {856, -2080, 32} };
        // NDCTF0
        s_origins[static_cast<size_t>(MapID::NDCTF0)] = { true, {-608, -304, 184} };
        // XDM1
        s_origins[static_cast<size_t>(MapID::XDM1)] = { true, {-312, 600, 144} };
        // XDM2
        s_origins[static_cast<size_t>(MapID::XDM2)] = { true, {-232, 472, 424} };
        // XDM3
        s_origins[static_cast<size_t>(MapID::XDM3)] = { true, {96, -96, 360} };
        // XDM4
        s_origins[static_cast<size_t>(MapID::XDM4)] = { true, {-160, -368, 360} };
        // XDM5
        s_origins[static_cast<size_t>(MapID::XDM5)] = { true, {8, -635, 367} };
        // XDM6
        s_origins[static_cast<size_t>(MapID::XDM6)] = { true, {-1088, -128, 528} };
        // INDUSTRY
        s_origins[static_cast<size_t>(MapID::INDUSTRY)] = { true, {-1009, -545, 79} };
        // MGU3M4
        s_origins[static_cast<size_t>(MapID::MGU3M4)] = { true, {3312, 3344, 864} };
        // MGDM1
        s_origins[static_cast<size_t>(MapID::MGDM1)] = { true, {176, 64, 288} };
        // MGU6TRIAL
        s_origins[static_cast<size_t>(MapID::MGU6TRIAL)] = { true, {-848, 176, 96} };
        // FACT3
        s_origins[static_cast<size_t>(MapID::FACT3)] = { true, {0, -64, 192} };
        // MGU4TRIAL
        s_origins[static_cast<size_t>(MapID::MGU4TRIAL)] = { true, {-960, -528, -328} };
        // MGU6M3
        s_origins[static_cast<size_t>(MapID::MGU6M3)] = { true, {0, 592, 1600} };
        // WASTE2
        s_origins[static_cast<size_t>(MapID::WASTE2)] = { true, {-1152, -288, -40} };
        // Q64/COMM
        s_origins[static_cast<size_t>(MapID::Q64_COMM)] = { true, {1464, -88, -432} };
        // Q64/COMMAND
        s_origins[static_cast<size_t>(MapID::Q64_COMMAND)] = { true, {0, -208, 56} };
        // Q64/DM7
        s_origins[static_cast<size_t>(MapID::Q64_DM7)] = { true, {64, 224, 120} };
        // Q64/DM1
        s_origins[static_cast<size_t>(MapID::Q64_DM1)] = { true, {-192, -320, 80} };
        // Q64/DM2
        s_origins[static_cast<size_t>(MapID::Q64_DM2)] = { true, {840, 80, 96} };
        // Q64/DM3
        s_origins[static_cast<size_t>(MapID::Q64_DM3)] = { true, {488, 392, 64} };
        // Q64/DM4
        s_origins[static_cast<size_t>(MapID::Q64_DM4)] = { true, {176, 272, -24} };
        // Q64/DM6
        s_origins[static_cast<size_t>(MapID::Q64_DM6)] = { true, {-1568, 1680, 144} };
        // Q64/DM8
        s_origins[static_cast<size_t>(MapID::Q64_DM8)] = { true, {-800, 448, 56} };
        // Q64/DM9
        s_origins[static_cast<size_t>(MapID::Q64_DM9)] = { true, {160, 56, 40} };
        // Q64/DM10
        s_origins[static_cast<size_t>(MapID::Q64_DM10)] = { true, {-304, 512, -92} };
        // EC/BASE_EC
        s_origins[static_cast<size_t>(MapID::EC_BASE_EC)] = { true, {-112, 704, 128} };
        // OLD/KMDM3
        s_origins[static_cast<size_t>(MapID::OLD_KMDM3)] = { true, {-480, -572, 144} };
        // TEST/MALS_BARRIER_TEST
        s_origins[static_cast<size_t>(MapID::TEST_MALS_BARRIER_TEST)] = { true, {24, 136, 224} };
        // TEST/SPBOX
        s_origins[static_cast<size_t>(MapID::TEST_SPBOX)] = { true, {112, 192, 168} };
        // TEST/TEST_KAISER
        s_origins[static_cast<size_t>(MapID::TEST_TEST_KAISER)] = { true, {1344, 176, -8} };
        // E3/JAIL_E3
        s_origins[static_cast<size_t>(MapID::E3_JAIL_E3)] = { true, {-572, -1312, 76} };
        // XINTELL
        s_origins[static_cast<size_t>(MapID::XINTELL)] = { true, {2096, -992, 376} };

        // Initialize Map Sizes
        // Default all to Medium
        s_mapSizes.fill({ false, false, true });

        // Define Small Maps using MapID
        const MapID smallMapIDs[] = {
            MapID::Q2DM3, MapID::Q2DM7, MapID::Q2DM2, MapID::Q64_DM10, MapID::TEST_MALS_BARRIER_TEST,
            MapID::Q64_DM9, MapID::Q64_DM7, MapID::Q64_DM2, MapID::TEST_SPBOX,
            MapID::Q64_DM1, MapID::FACT3, MapID::Q2CTF4, MapID::RDM4, MapID::Q64_COMMAND, MapID::MGU3M4,
            MapID::MGU4TRIAL, MapID::MGU6TRIAL, MapID::EC_BASE_EC, MapID::MGDM1, MapID::NDCTF0, MapID::Q64_DM6,
            MapID::Q64_DM8, MapID::Q64_DM4, MapID::Q64_DM3, MapID::INDUSTRY, MapID::E3_JAIL_E3, MapID::Q2DM5
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
            MapID::SEWER64, MapID::BASE64, MapID::CITY64, MapID::RDM1, MapID::RDM2, MapID::XDM5
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
        if (!map_name || !map_name[0]) [[unlikely]] {
            return MapID::UNKNOWN;
        }

        // Make sure the registry is initialized
        if (!s_initialized) [[unlikely]] {
            Initialize();
        }

        auto it = s_mapIDMap.find(map_name);
        if (it != s_mapIDMap.end()) [[likely]] {
            return it->second;
        }
        return MapID::UNKNOWN;
    }

    bool MapOriginRegistry::GetOrigin(const char* map_name, vec3_t& out_origin) {
        return GetOrigin(GetMapID(map_name), out_origin);
    }

    bool MapOriginRegistry::GetOrigin(MapID mapId, vec3_t& out_origin) {
        // Make sure the registry is initialized
        if (!s_initialized) [[unlikely]] {
            Initialize();
        }

        size_t index = static_cast<size_t>(mapId);
        if (index < s_origins.size() && s_origins[index].is_valid) [[likely]] {
            out_origin = s_origins[index].origin;
            return true;
        }
        return false;
    }

    MapSize MapOriginRegistry::GetMapSize(MapID mapId) {
        // Make sure the registry is initialized
        if (!s_initialized) [[unlikely]] {
            Initialize();
        }

        size_t index = static_cast<size_t>(mapId);
        if (index < s_mapSizes.size() && mapId != MapID::UNKNOWN) [[likely]] {
            return s_mapSizes[index];
        }

        // Default to Medium for unknown or out-of-bounds IDs
        return { false, false, true };
    }

    //
    // Fast type checking functions - optimized with branch hints
    //

    bool IsMonsterType(const edict_t* ent, MonsterTypeID type_to_check) {
        if (!ent) [[unlikely]] {
            return false;
        }
        return ent->monsterinfo.monster_type_id == static_cast<uint8_t>(type_to_check);
    }

    bool IsSpecialType(const edict_t* ent, SpecialEntityTypeID type_to_check) {
        if (!ent) [[unlikely]] {
            return false;
        }
        return ent->special_type_id == static_cast<uint8_t>(type_to_check);
    }

} // namespace horde