#pragma once

#include <array>
#include <vector>
#include <unordered_map>
#include <bitset>
#include <cmath>
#include <algorithm>
#include <random>
#include <chrono>
#include "../g_local.h"
#include "../q_vec3.h"

namespace HordePerf {

// ============================================================================
// Fast Math Cache
// ============================================================================
template<int MAX_VALUE = 251>
class FastMathCache {
    std::array<float, MAX_VALUE> sqrt_values;
    std::array<float, MAX_VALUE> squared_values;
    bool initialized = false;

public:
    void Initialize() {
        if (initialized) return;
        for (int i = 0; i < MAX_VALUE; ++i) {
            sqrt_values[i] = std::sqrt(static_cast<float>(i));
            squared_values[i] = static_cast<float>(i * i);
        }
        initialized = true;
    }

    float GetSqrt(int value) {
        if (value < 0 || value >= MAX_VALUE)
            return std::sqrt(static_cast<float>(value));
        if (!initialized) Initialize();
        return sqrt_values[value];
    }

    float GetSquared(int value) {
        if (value < 0 || value >= MAX_VALUE)
            return static_cast<float>(value * value);
        if (!initialized) Initialize();
        return squared_values[value];
    }
};

// ============================================================================
// Spawn Point Spatial Index (Optimized Hash-Grid Implementation)
// ============================================================================
class SpawnPointSpatialIndex {
    static constexpr int CELL_SIZE = 512;
    static constexpr int CELL_SHIFT = 9; // log2(512)

    struct Cell {
        static constexpr size_t MAX_SPAWNS_PER_CELL = 16;
        std::array<edict_t*, MAX_SPAWNS_PER_CELL> spawn_points;
        size_t count = 0;

        void clear() { count = 0; }

        void add(edict_t* spawn) {
            if (count < MAX_SPAWNS_PER_CELL) {
                spawn_points[count++] = spawn;
            }
        }
    };

    std::unordered_map<uint32_t, Cell> grid_cells;
    std::vector<edict_t*> all_spawn_points;

    static uint32_t GetCellKey(const vec3_t& pos) {
        int x = static_cast<int>(pos.x) >> CELL_SHIFT;
        int y = static_cast<int>(pos.y) >> CELL_SHIFT;
        return (static_cast<uint32_t>(x) << 16) | (static_cast<uint32_t>(y) & 0xFFFF);
    }

public:
    void AddSpawnPoint(edict_t* spawn_point) {
        if (!spawn_point) return;

        all_spawn_points.push_back(spawn_point);
        uint32_t key = GetCellKey(spawn_point->s.origin);
        grid_cells[key].add(spawn_point);
    }

    void Clear() {
        grid_cells.clear();
        all_spawn_points.clear();
    }

    std::vector<edict_t*> GetNearbySpawnPoints(const vec3_t& pos, float radius) {
        std::vector<edict_t*> result;
        result.reserve(32); // Reserve space for typical nearby spawn count

        int cell_radius = static_cast<int>(radius / CELL_SIZE) + 1;
        int center_x = static_cast<int>(pos.x) >> CELL_SHIFT;
        int center_y = static_cast<int>(pos.y) >> CELL_SHIFT;
        float radius_sq = radius * radius;

        gi.Com_PrintFmt("GetNearbySpawnPoints: Searching at ({:.0f}, {:.0f}, {:.0f}) with radius {:.0f}\n",
            pos.x, pos.y, pos.z, radius);
        gi.Com_PrintFmt("  Grid cells to check: center=({},{}), cell_radius={}\n",
            center_x, center_y, cell_radius);
        gi.Com_PrintFmt("  Total spawn points in index: {}\n", all_spawn_points.size());
        gi.Com_PrintFmt("  Grid cells in map: {}\n", grid_cells.size());

        int cells_checked = 0;
        int cells_with_points = 0;

        for (int dx = -cell_radius; dx <= cell_radius; ++dx) {
            for (int dy = -cell_radius; dy <= cell_radius; ++dy) {
                uint32_t key = (static_cast<uint32_t>(center_x + dx) << 16) |
                              (static_cast<uint32_t>(center_y + dy) & 0xFFFF);

                cells_checked++;
                auto it = grid_cells.find(key);
                if (it != grid_cells.end()) {
                    cells_with_points++;
                    const Cell& cell = it->second;
                    gi.Com_PrintFmt("    Cell ({},{}) has {} spawn points\n", center_x + dx, center_y + dy, cell.count);
                    for (size_t i = 0; i < cell.count; ++i) {
                        edict_t* spawn = cell.spawn_points[i];
                        if (!spawn) {
                            gi.Com_PrintFmt("      Point {} is NULL\n", i);
                            continue;
                        }
                        if (!spawn->inuse) {
                            gi.Com_PrintFmt("      Point {} not inuse\n", i);
                            continue;
                        }

                        vec3_t diff = spawn->s.origin - pos;
                        float dist_sq = diff.lengthSquared();
                        if (dist_sq <= radius_sq) {
                            result.push_back(spawn);
                            gi.Com_PrintFmt("      Added spawn point at ({:.0f}, {:.0f}, {:.0f}), distance: {:.0f}\n",
                                spawn->s.origin.x, spawn->s.origin.y, spawn->s.origin.z, sqrt(dist_sq));
                        } else {
                            gi.Com_PrintFmt("      Spawn point at ({:.0f}, {:.0f}, {:.0f}) too far: {:.0f} > {:.0f}\n",
                                spawn->s.origin.x, spawn->s.origin.y, spawn->s.origin.z, sqrt(dist_sq), radius);
                        }
                    }
                }
            }
        }

        gi.Com_PrintFmt("  Cells checked: {}, cells with points: {}\n", cells_checked, cells_with_points);
        gi.Com_PrintFmt("  Nearby spawn points found: {}\n", result.size());

        return result;
    }

    const std::vector<edict_t*>& GetAllSpawnPoints() const {
        return all_spawn_points;
    }
};

// ============================================================================
// Monster Type Property Cache
// ============================================================================
struct MonsterTypeProperties {
    vec3_t mins;
    vec3_t maxs;
    float spawn_radius;
    bool can_fly;
    bool is_boss;
    bool is_special;
    int health;
    int armor;
    bool valid;
};

class MonsterTypeCache {
    static constexpr size_t MAX_MONSTER_TYPES = 256;
    std::array<MonsterTypeProperties, MAX_MONSTER_TYPES> cache;
    std::array<bool, MAX_MONSTER_TYPES> cached;

public:
    MonsterTypeCache() {
        cached.fill(false);
    }

    const MonsterTypeProperties* GetProperties(int type_id) {
        if (type_id < 0 || type_id >= MAX_MONSTER_TYPES)
            return nullptr;

        if (!cached[type_id])
            return nullptr;

        return &cache[type_id];
    }

    void CacheProperties(int type_id, const MonsterTypeProperties& props) {
        if (type_id < 0 || type_id >= MAX_MONSTER_TYPES)
            return;

        cache[type_id] = props;
        cached[type_id] = true;
    }

    void Clear() {
        cached.fill(false);
    }
};

// ============================================================================
// Fixed Pool Allocator for Temporary Objects
// ============================================================================
template<typename T, size_t N>
class FixedPool {
    std::array<T, N> pool;
    std::bitset<N> used;
    size_t search_hint = 0;

public:
    T* allocate() {
        // Start search from hint position
        for (size_t i = search_hint; i < N; ++i) {
            if (!used[i]) {
                used[i] = true;
                search_hint = (i + 1) % N;
                return &pool[i];
            }
        }
        // Wrap around
        for (size_t i = 0; i < search_hint; ++i) {
            if (!used[i]) {
                used[i] = true;
                search_hint = (i + 1) % N;
                return &pool[i];
            }
        }
        return nullptr;
    }

    void deallocate(T* ptr) {
        if (!ptr) return;
        size_t idx = ptr - pool.data();
        if (idx < N) {
            used[idx] = false;
            new (ptr) T(); // Reset to default state
        }
    }

    void clear() {
        used.reset();
        search_hint = 0;
    }

    size_t available() const {
        return N - used.count();
    }
};

// ============================================================================
// Batch Grid Updater
// ============================================================================
class BatchedGridUpdater {
    static constexpr size_t MAX_BATCH = 64;

    struct Update {
        edict_t* ent;
        vec3_t new_pos;
    };

    std::array<Update, MAX_BATCH> pending_updates;
    size_t update_count = 0;

public:
    void QueueUpdate(edict_t* ent, const vec3_t& new_pos) {
        if (update_count < MAX_BATCH) {
            pending_updates[update_count].ent = ent;
            pending_updates[update_count].new_pos = new_pos;
            update_count++;
        } else {
            // Force flush if full
            FlushUpdates();
            QueueUpdate(ent, new_pos);
        }
    }

    void FlushUpdates() {
        if (update_count == 0) return;

        // Process all updates in batch
        for (size_t i = 0; i < update_count; ++i) {
            // Call your actual grid update function here
            // HordePhys::g_monster_grid.Update(pending_updates[i].ent, pending_updates[i].new_pos);
        }

        update_count = 0;
    }

    size_t GetPendingCount() const {
        return update_count;
    }
};

// ============================================================================
// Global Instances
// ============================================================================
inline FastMathCache<> g_fast_math;
inline SpawnPointSpatialIndex g_spawn_spatial_index;
inline MonsterTypeCache g_monster_type_cache;
inline BatchedGridUpdater g_grid_updater;

// ============================================================================
// Tesla Think Optimization
// ============================================================================
inline gtime_t GetTeslaThinkTimeWithJitter() {
    // Add jitter to prevent all Teslas thinking on same frame
    // Using irandom for -20 to 20 ms jitter
    int jitter_ms = irandom(41) - 20; // irandom(41) gives 0-40, subtract 20 for -20 to 20
    return level.time + 100_ms + gtime_t::from_ms(jitter_ms);
}

// ============================================================================
// Distance Cache for Common Calculations
// ============================================================================
class DistanceCache {
    struct CacheEntry {
        vec3_t pos1;
        vec3_t pos2;
        float distance_sq;
        gtime_t timestamp;
    };

    static constexpr size_t CACHE_SIZE = 256;
    static constexpr gtime_t CACHE_LIFETIME = 500_ms;

    std::array<CacheEntry, CACHE_SIZE> cache;
    size_t current_index = 0;

    static uint32_t HashPositions(const vec3_t& p1, const vec3_t& p2) {
        // Simple hash combining position components
        uint32_t h1 = static_cast<uint32_t>(p1.x) ^ (static_cast<uint32_t>(p1.y) << 11) ^
                      (static_cast<uint32_t>(p1.z) << 22);
        uint32_t h2 = static_cast<uint32_t>(p2.x) ^ (static_cast<uint32_t>(p2.y) << 11) ^
                      (static_cast<uint32_t>(p2.z) << 22);
        return h1 ^ h2;
    }

public:
    float GetDistanceSquared(const vec3_t& pos1, const vec3_t& pos2) {
        uint32_t hash = HashPositions(pos1, pos2);
        size_t idx = hash % CACHE_SIZE;

        // Check if cached
        if (cache[idx].timestamp + CACHE_LIFETIME > level.time &&
            cache[idx].pos1 == pos1 &&
            cache[idx].pos2 == pos2) {
            return cache[idx].distance_sq;
        }

        // Calculate and cache
        vec3_t diff = pos2 - pos1;
        float dist_sq = diff.lengthSquared();

        cache[idx].pos1 = pos1;
        cache[idx].pos2 = pos2;
        cache[idx].distance_sq = dist_sq;
        cache[idx].timestamp = level.time;

        return dist_sq;
    }

    void Clear() {
        for (auto& entry : cache) {
            entry.timestamp = 0_ms;
        }
    }
};

inline DistanceCache g_distance_cache;

} // namespace HordePerf