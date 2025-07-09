
// --- START OF FILE g_laser.h ---
#pragma once // Include guard

#include "g_local.h" // Include necessary base headers (for edict_t, vec3_t, gtime_t, etc.)
#include <array>     // For std::array
#include <cstdint>   // For int32_t

// Forward declaration
void G_UpdateActiveLasersForWaveProgression(int current_wave_level_from_game);

// Constants
namespace LaserConstants {
    constexpr int32_t MAX_LASERS = 6;
    constexpr int32_t LASER_COST = 25;
    constexpr int32_t LASER_INITIAL_DAMAGE = 1;
    constexpr int32_t LASER_ADDON_DAMAGE = 4;
    constexpr int32_t LASER_INITIAL_HEALTH = 150;
    constexpr int32_t LASER_ADDON_HEALTH = 120;
    constexpr int32_t MAX_LASER_HEALTH = 2500;
    constexpr gtime_t LASER_SPAWN_DELAY = 1_sec;
    constexpr gtime_t LASER_TIMEOUT_DELAY = 180_sec;
    constexpr gtime_t BLINK_INTERVAL = 500_ms;
    constexpr gtime_t WARNING_TIME = 10_sec;
    constexpr float LASER_NONCLIENT_MOD = 1.0f;
}

// --- PlayerLaserManager Definition ---
class PlayerLaserManager {
private:
    struct LaserEntry {
        edict_t* emitter = nullptr;
        edict_t* beam = nullptr;
        bool active = false;
    };

    std::array<LaserEntry, LaserConstants::MAX_LASERS> lasers;
    int active_count = 0;
    edict_t* owner; // Pointer to the owning player edict

public:
    // Constructor - Takes the owning player edict
    explicit PlayerLaserManager(edict_t* player);

    // Methods
    bool can_add_laser() const;
    void add_laser(edict_t* emitter, edict_t* beam);
    // Mark laser as inactive in the manager
    void remove_laser(const edict_t* entity);
    // Calls laser_die on all active lasers to ensure proper cleanup
    void remove_all_lasers();
    int get_active_count() const;
    edict_t* get_owner() const { return owner; } // Helper if needed

    // Destructor (optional, only if PlayerLaserManager itself owns resources)
    // ~PlayerLaserManager(); // Likely not needed if it just holds pointers
};

// --- Helper Namespace Declaration ---
namespace LaserHelpers {
    // Declaration of the function needed by horde_menu.cpp
    // Retrieves the PlayerLaserManager associated with a player entity.
    // Returns nullptr if the player is invalid or has no manager.
    PlayerLaserManager* get_laser_manager(edict_t* ent);

    // Other helper declarations could go here if needed by multiple files
}

// --- Function Declarations ---
// Functions from g_laser.cpp that need to be called from other files

// Creates a laser for the given entity (player)
void create_laser(edict_t* ent);

// Removes all lasers associated with the given entity (player)
void remove_lasers(edict_t* ent) noexcept;

// The DIE function for laser components (emitter/beam)
// Generally only called internally or by engine, but declare if needed elsewhere
// extern "C" { // Use extern "C" if DIE macro requires C linkage
//     DIE(laser_die)(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void;
// }
// ^-- Declaration might not be strictly necessary if only called internally via function pointer

// --- END OF FILE g_laser.h ---

