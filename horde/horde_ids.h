#pragma once

#include <array>
#include <string_view>
#include <unordered_map>

// Forward declarations to break circular dependencies
struct edict_t;
struct gtime_t;

namespace horde {

    // IMPORTANT: MapSize must be defined BEFORE including g_local.h
    // because g_local.h includes g_horde.h which uses horde::MapSize
    struct MapSize {
        bool isSmallMap = false;
        bool isBigMap = false;
        bool isMediumMap = true; // Default to medium
    };

} // namespace horde

// Now include g_local.h AFTER MapSize is defined
#include "../g_local.h"

namespace horde {

    // Compile-time string hashing using FNV-1a algorithm
    // Can be used for fast string comparisons in constexpr contexts
    constexpr uint32_t fnv1a_hash(std::string_view str) noexcept {
        constexpr uint32_t FNV_PRIME = 0x01000193;
        constexpr uint32_t FNV_OFFSET = 0x811C9DC5;

        uint32_t hash = FNV_OFFSET;
        for (char c : str) {
            hash ^= static_cast<uint32_t>(c);
            hash *= FNV_PRIME;
        }
        return hash;
    }

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
        CHICKKL = 17,
        PARASITE = 18,
        BRAIN = 19,
        FLYER = 20,
        HOVER = 21,
        HOVER_VANILLA = 22,
        MUTANT = 23,
        REDMUTANT = 24,
        BERSERK = 25,
        FIXBOT = 26,
        FIXBOT_KL = 27,
        FLOATER = 28,
        FLOATER_TRACKER = 29,
        STALKER = 30,
        GEKK = 31,
        SPIDER = 32,
        ARACHNID2 = 33,
        GM_ARACHNID = 34,
        PSX_ARACHNID = 35,
        SHAMBLER = 36,
        SHAMBLER_SMALL = 37,
        SHAMBLER_KL = 38,
        JANITOR = 39,
        JANITOR2 = 40,
        MEDIC_COMMANDER = 41,
        GUNCMDR = 42,
        GUNCMDR_VANILLA = 43,
        GUNCMDR_KL = 44,
        TANK_SPAWNER = 45,
        PERRO_KL = 46,
        SOLDIER_HYPERGUN = 47,
        SOLDIER_RIPPER = 48,
        SOLDIER_LASERGUN = 49,
        DAEDALUS = 50,
        DAEDALUS_BOMBER = 51,

        // Boss monsters
        BOSS2 = 52,
        BOSS2_64 = 53,
        BOSS2_MINI = 54,
        BOSS2_KL = 55,
        CARRIER = 56,
        CARRIER_MINI = 57,
        MAKRON = 58,
        MAKRON_KL = 59,
        JORG = 60,
        JORG_SMALL = 61,
        WIDOW = 62,
        WIDOW1 = 63,
        WIDOW2 = 64,
        BOSS5 = 65,
        GUARDIAN = 66,
        PSX_GUARDIAN = 67,

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

        // Special fog wave bosses
        BERSERKERKL = 82,
        GEKKKL = 83,

        MAX_TYPES = 128,  // Set this to a value higher than the largest ID
        UNKNOWN = 255
    };

    // Monster Pack system to reduce memory usage by dividing monsters into groups
    enum class MonsterPack : uint8_t {
        PACK_CLASSIC_STROGG = 0,  // Classic Quake 2 enemies (soldiers, tanks, gunners, etc.)
        PACK_MUTANT_SPECIAL = 1,  // Mutants, flyers, special units
        PACK_ELITE_ARACHNID = 2,  // Elite variants, arachnids, late-game enemies
        MAX_PACKS = 3,
        PACK_NONE = 255           // For monsters that should always be available (bosses)
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
    MORPHED_PLAYER,  // 10 - For players morphed into monsters
    BARREL,          // 11 - Explosive barrels
    COUNT,           // 12 - The total number of special entity types
    UNKNOWN = 255
};;

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
        RDM10 = 15,
        RDM12 = 16,

        // XDM maps
        XDM1 = 17,
        XDM2 = 18,
        XDM3 = 19,
        XDM4 = 20,
        XDM6 = 21,

        // Q64 maps
        Q64_DM1 = 22,
        Q64_DM2 = 23,
        Q64_DM3 = 24,
        Q64_DM4 = 25,
        Q64_DM6 = 26,
        Q64_DM7 = 27,
        Q64_DM8 = 28,
        Q64_DM9 = 29,
        Q64_DM10 = 30,
        Q64_COMMAND = 31,
        Q64_COMM = 32,

        // MGU maps
        MGU3M4 = 33,
        MGU4TRIAL = 34,
        MGU6M3 = 35,
        MGU6TRIAL = 36,
        MGDM1 = 37,

        // Special maps
        RBOSS = 38,
        INDUSTRY = 39,
        FACT3 = 40,
        WASTE2 = 41,
        NDCTF0 = 42,
        XINTELL = 43,
        SEWER64 = 44,
        BASE64 = 45,
        CITY64 = 46,

        // EC maps
        EC_BASE_EC = 47,

        // Old maps
        OLD_KMDM3 = 48,

        // E3 maps
        E3_JAIL_E3 = 49,

        // Test maps
        TEST_MALS_BARRIER_TEST = 50,
        TEST_SPBOX = 51,
        TEST_TEST_KAISER = 52,

        //rdm maps

        RDM1 = 53,
        RDM2 = 54,
        RDM7 = 55,
        XDM5 = 56,

        RDM11 = 57,
        RDM14 = 58,

        MAX_MAPS = 64,  // Set this to a value higher than the largest ID
        UNKNOWN = 0xFFFF
    };

    // NOTE: MapSize has been moved to the top of this file to break circular dependency

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
        // Improved struct packing: bool first to minimize padding
        struct MapOrigin {
            bool is_valid;
            vec3_t origin;
        };

        static std::array<MapOrigin, static_cast<size_t>(MapID::MAX_MAPS)> s_origins;
        static std::array<MapSize, static_cast<size_t>(MapID::MAX_MAPS)> s_mapSizes;
        static bool s_initialized;
    };

    //  replacement for spawn point time tracking
    // This is intended to integrate with existing SpawnPointDataArray
    // Uses vector for O(1) access since entity numbers are sequential
    class SpawnPointTimeTracker {
    public:
        // Reset all spawn times
        void Reset();

        // Set spawn time for a point
        void SetLastSpawnTime(const edict_t* point, gtime_t time);

        // Get last spawn time for a point
        gtime_t GetLastSpawnTime(const edict_t* point) const;

    private:
        std::vector<gtime_t> m_lastSpawnTimes;

        // Ensure the vector is large enough for the given entity number
        void EnsureCapacity(int entity_num);
    };

    // Create global instances
    extern MonsterSpawnTimeTracker g_monsterSpawnTracker;
    extern SpawnPointTimeTracker g_spawnPointTimeTracker;

    // Initialization function - call once at startup
    void InitializeHordeIDs();

    // Fast type checking functions - defined in cpp to avoid incomplete type issues
    bool IsMonsterType(const edict_t* ent, MonsterTypeID type_to_check);
    bool IsSpecialType(const edict_t* ent, SpecialEntityTypeID type_to_check);
} // namespace horde

