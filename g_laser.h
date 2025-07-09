// --- START OF FILE g_laser.h ---
#pragma once // Include guard

#include "g_local.h" // Include necessary base headers
#include <array>
#include <cstdint>

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
    // Constructor
    explicit PlayerLaserManager(edict_t* player);

    // Destructor - Ensures all game entities are cleaned up when the manager is destroyed.
    ~PlayerLaserManager();

    // Methods
    bool can_add_laser() const;
    void add_laser(edict_t* emitter, edict_t* beam);
    void remove_laser(const edict_t* entity);
    void remove_all_lasers();
    int get_active_count() const;
    edict_t* get_owner() const { return owner; }
};

// --- Helper Namespace Declaration ---
namespace LaserHelpers {
    PlayerLaserManager* get_laser_manager(edict_t* ent);
}

// --- Function Declarations ---
void create_laser(edict_t* ent);
void remove_lasers(edict_t* ent) noexcept;

// --- END OF FILE g_laser.h ---