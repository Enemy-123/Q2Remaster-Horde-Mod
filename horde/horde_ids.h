#pragma once

#include "../g_local.h" 
#include <array>
#include <string_view>
#include <unordered_map>

struct edict_t;

namespace horde {

    // IDs for monster types - allows array-based lookup instead of string-based
    enum class MonsterTypeID : uint8_t {
        SOLDIER_LIGHT = 0,
        SOLDIER = 1,
        SOLDIER_SS = 2,
        INFANTRY = 3,
        INFANTRY_VANILLA = 4,
        GUNNER = 5,
        GUNNER_VANILLA = 6,
        MEDIC = 7,
        GLADIATOR = 8,
        GLADIATOR_B = 9,
        GLADIATOR_C = 10,
        TANK = 11,
        TANK_COMMANDER = 12,
        TANK_64 = 13,
        RUNNERTANK = 14,
        CHICK = 15,
        CHICK_HEAT = 16,
        PARASITE = 17,
        BRAIN = 18,
        FLYER = 19,
        HOVER = 20,
        HOVER_VANILLA = 21,
        MUTANT = 22,
        REDMUTANT = 23,
        BERSERK = 24,
        FIXBOT = 25,
        FIXBOT_KL = 26,
        FLOATER = 27,
        FLOATER_TRACKER = 28,
        STALKER = 29,
        GEKK = 30,
        SPIDER = 31,
        ARACHNID2 = 32,
        GM_ARACHNID = 33,
        PSX_ARACHNID = 34,
        SHAMBLER = 35,
        SHAMBLER_SMALL = 36,
        SHAMBLER_KL = 37,
        JANITOR = 38,
        JANITOR2 = 39,
        MEDIC_COMMANDER = 40,
        GUNCMDR = 41,
        GUNCMDR_VANILLA = 42,
        GUNCMDR_KL = 43,
        TANK_SPAWNER = 44,
        PERRO_KL = 45,
        SOLDIER_HYPERGUN = 46,
        SOLDIER_RIPPER = 47,
        SOLDIER_LASERGUN = 48,
        DAEDALUS = 49,
        DAEDALUS_BOMBER = 50,

        // Boss monsters
        BOSS2 = 51,
        BOSS2_64 = 52,
        BOSS2_MINI = 53,
        BOSS2_KL = 54,
        CARRIER = 55,
        CARRIER_MINI = 56,
        MAKRON = 57,
        MAKRON_KL = 58,
        JORG = 59,
        JORG_SMALL = 60,
        WIDOW = 61,
        WIDOW1 = 62,
        WIDOW2 = 63,
        BOSS5 = 64,
        GUARDIAN = 65,
        PSX_GUARDIAN = 66,

        // Add other monster types if needed

        MISC_INSANE = 68,
        TURRET = 69,

        SENTRYGUN = 70,

        BOSS3_STAND = 71,
        EASTERTANK = 72,
        EASTERCHICK = 73,
        EASTERCHICK2 = 74,
        COMMANDER_BODY = 75,
        BIGVIPER = 76,
        ARACHNID = 77,
        KAMIKAZE = 78,

        SUPERTANK = 79,
        SUPERTANKKL = 80,

        FLIPPER = 81,

        MAX_TYPES = 128,  // Set this to a value higher than the largest ID
        UNKNOWN = 255
    };

    // IDs for special, non-monster entities
enum class SpecialEntityTypeID : uint8_t {
    TESLA_MINE,      // 0
    FOOD_CUBE_TRAP,  // 1
    PROX_MINE,       // 2
    TURRET,          // 3
    SENTRY_GUN,      // 4
    NUKE_MINE,       // 5
    LASER_EMITTER,   // 6
    LASER_BEAM,      // 7
    DOPPLEGANGER,    // 8
    STROGG_SUMMONER, // 9
    COUNT,           // 10 - The total number of special entity types
    UNKNOWN = 255
};

// Class for efficient special entity type lookups
class SpecialTypeRegistry {
public:
    // Initialize the registry with all special entity mappings
    static void Initialize();

    // Get special entity type ID from classname
    static SpecialEntityTypeID GetTypeID(const char* classname);
};


    // Maps for known game maps - allows array-based lookup
    enum class MapID : uint16_t {
        // Standard Q2 maps
        Q2DM1 = 0,
        Q2DM2 = 1,
        Q2DM3 = 2,
        Q2DM4 = 3,
        Q2DM5 = 4,
        Q2DM6 = 5,
        Q2DM7 = 6,
        Q2DM8 = 7,

        // Q2CTF maps
        Q2CTF4 = 8,
        Q2CTF5 = 9,

        // RDM maps
        RDM4 = 10,
        RDM5 = 11,
        RDM6 = 12,
        RDM8 = 13,
        RDM9 = 14,
        RDM12 = 15,

        // XDM maps
        XDM1 = 16,
        XDM2 = 17,
        XDM3 = 18,
        XDM4 = 19,
        XDM6 = 20,

        // Q64 maps
        Q64_DM1 = 21,
        Q64_DM2 = 22,
        Q64_DM3 = 23,
        Q64_DM4 = 24,
        Q64_DM6 = 25,
        Q64_DM7 = 26,
        Q64_DM8 = 27,
        Q64_DM9 = 28,
        Q64_DM10 = 29,
        Q64_COMMAND = 30,
        Q64_COMM = 31,

        // MGU maps
        MGU3M4 = 32,
        MGU4TRIAL = 33,
        MGU6M3 = 34,
        MGU6TRIAL = 35,
        MGDM1 = 36,

        // Special maps
        RBOSS = 37,
        INDUSTRY = 38,
        FACT3 = 39,
        WASTE2 = 40,
        NDCTF0 = 41,
        XINTELL = 42,
        SEWER64 = 43,
        BASE64 = 44,
        CITY64 = 45,

        // EC maps
        EC_BASE_EC = 46,

        // Old maps
        OLD_KMDM3 = 47,

        // E3 maps
        E3_JAIL_E3 = 48,

        // Test maps
        TEST_MALS_BARRIER_TEST = 49,
        TEST_SPBOX = 50,
        TEST_TEST_KAISER = 51,

        //rdm maps

        RDM1 = 52,
        RDM2 = 53,
        XDM5 = 54,

        MAX_MAPS = 64,  // Set this to a value higher than the largest ID
        UNKNOWN = 0xFFFF
    };

    // Structure to hold map size classification
    struct MapSize {
        bool isSmallMap = false;
        bool isBigMap = false;
        bool isMediumMap = true; // Default to medium
    };

    // Class for efficient monster type lookups
    class MonsterTypeRegistry {
    public:
        // Initialize the registry with all monster mappings
        static void Initialize();

        // Get monster type ID from classname
        static MonsterTypeID GetTypeID(const char* classname);

        // Check if a monster type exists
        static bool IsValidType(MonsterTypeID id);

        // For debugging - get classname from ID
        static const char* GetClassname(MonsterTypeID id);
    };

    //  replacement for lastMonsterSpawnTime
    class MonsterSpawnTimeTracker {
    public:
        // Reset all spawn times
        void Reset();

        // Set spawn time for a monster type
        void SetLastSpawnTime(const char* classname, gtime_t time);
        void SetLastSpawnTime(MonsterTypeID typeId, gtime_t time);

        // Get last spawn time for a monster type
        gtime_t GetLastSpawnTime(const char* classname) const;
        gtime_t GetLastSpawnTime(MonsterTypeID typeId) const;

    private:
        std::array<gtime_t, static_cast<size_t>(MonsterTypeID::MAX_TYPES)> m_lastSpawnTimes{};
    };

    // Class for efficient map origin and size lookups
    class MapOriginRegistry {
    public:
        // Initialize with predefined origins and sizes for all maps
        static void Initialize();

        // Get map ID from name
        static MapID GetMapID(const char* map_name);

        // Get origin for a map (returns true if successful)
        static bool GetOrigin(const char* map_name, vec3_t& out_origin);
        static bool GetOrigin(MapID mapId, vec3_t& out_origin);

        // Get map size from ID
        static MapSize GetMapSize(MapID mapId);

    private:
        struct MapOrigin {
            vec3_t origin;
            bool is_valid;
        };

        static std::array<MapOrigin, static_cast<size_t>(MapID::MAX_MAPS)> s_origins;
        static std::array<MapSize, static_cast<size_t>(MapID::MAX_MAPS)> s_mapSizes;
        static bool s_initialized;
    };

    //  replacement for spawn point time tracking
    // This is intended to integrate with existing SpawnPointDataArray
    class SpawnPointTimeTracker {
    public:
        // Reset all spawn times
        void Reset();

        // Set spawn time for a point
        void SetLastSpawnTime(const edict_t* point, gtime_t time);

        // Get last spawn time for a point
        gtime_t GetLastSpawnTime(const edict_t* point) const;

    private:
         std::unordered_map<int, gtime_t> m_lastSpawnTimes;
    };

    // Create global instances
    extern MonsterSpawnTimeTracker g_monsterSpawnTracker;
    extern SpawnPointTimeTracker g_spawnPointTimeTracker;

    // Initialization function - call once at startup
    void InitializeHordeIDs();

    bool IsMonsterType(const edict_t* ent, horde::MonsterTypeID type_to_check);
    bool IsSpecialType(const edict_t* ent, horde::SpecialEntityTypeID type_to_check);
} // namespace horde

