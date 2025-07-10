// g_laser.h
#pragma once

#include "g_local.h"
#include <array>
#include <cstdint>

// Forward declaration
class PlayerLaserManager;
void G_UpdateActiveLasersForWaveProgression(int current_wave_level_from_game);

// Constants
namespace LaserConstants {
    constexpr int32_t MAX_LASERS_PER_PLAYER = 6;
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

// Manages the lasers for a single player.
// This object is manually allocated with 'new' and deallocated with 'delete'.
class PlayerLaserManager {
private:
    struct LaserEntry {
        edict_t* emitter = nullptr;
        bool active = false;
    };

    std::array<LaserEntry, LaserConstants::MAX_LASERS_PER_PLAYER> lasers;
    int active_count = 0;
    edict_t* owner; // Pointer to the owning player edict

public:
    explicit PlayerLaserManager(edict_t* player);
    ~PlayerLaserManager(); // Destructor to clean up all lasers on manager deletion.

    bool can_add_laser() const;
    void add_laser(edict_t* emitter);
    void remove_laser(const edict_t* emitter_to_remove);
    void remove_all_lasers();
    int get_active_count() const;
};

// This makes the helper functions visible to other files like g_cmds.cpp
namespace LaserHelpers {
    // Retrieves the PlayerLaserManager associated with a player entity.
    // Returns nullptr if the player is invalid or has no manager.
    PlayerLaserManager* get_laser_manager(edict_t* ent);
}

// Creates a laser for the given entity (player).
void create_laser(edict_t* ent);
void laser_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
void CleanupPlayerLaserManager(edict_t* ent);