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
    std::array<float, MAX_VALUE> sqrt_values{};
    std::array<float, MAX_VALUE> squared_values{};
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
        std::array<edict_t*, MAX_SPAWNS_PER_CELL> spawn_points{};
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

        for (int dx = -cell_radius; dx <= cell_radius; ++dx) {
            for (int dy = -cell_radius; dy <= cell_radius; ++dy) {
                uint32_t key = (static_cast<uint32_t>(center_x + dx) << 16) |
                              (static_cast<uint32_t>(center_y + dy) & 0xFFFF);

                auto it = grid_cells.find(key);
                if (it != grid_cells.end()) {
                    const Cell& cell = it->second;
                    for (size_t i = 0; i < cell.count; ++i) {
                        edict_t* spawn = cell.spawn_points[i];
                        if (!spawn || !spawn->inuse) continue;

                        vec3_t diff = spawn->s.origin - pos;
                        if (diff.lengthSquared() <= radius_sq) {
                            result.push_back(spawn);
                        }
                    }
                }
            }
        }

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
    vec3_t mins = vec3_origin;
    vec3_t maxs = vec3_origin;
    float spawn_radius = 0.0f;
    bool can_fly = false;
    bool is_boss = false;
    bool is_special = false;
    int health = 0;
    int armor = 0;
    bool valid = false;
};

class MonsterTypeCache {
    static constexpr size_t MAX_MONSTER_TYPES = 256;
    std::array<MonsterTypeProperties, MAX_MONSTER_TYPES> cache{};
    std::array<bool, MAX_MONSTER_TYPES> cached{};

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
        edict_t* ent = nullptr;
        vec3_t new_pos = vec3_origin;
    };

    std::array<Update, MAX_BATCH> pending_updates{};
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
        vec3_t pos1 = vec3_origin;
        vec3_t pos2 = vec3_origin;
        float distance_sq = 0.0f;
        gtime_t timestamp = 0_sec;
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

// ============================================================================
// Visibility Cache - Reduces expensive line-of-sight checks
// ============================================================================
class VisibilityCache {
	struct CacheEntry {
		int entity_id1 = 0;
		int entity_id2 = 0;
		bool visible = false;
		gtime_t timestamp = 0_sec;
	};

	static constexpr size_t CACHE_SIZE = 256;
	static constexpr gtime_t CACHE_LIFETIME = 200_ms; // Shorter than distance cache

	std::array<CacheEntry, CACHE_SIZE> cache;

	static uint32_t HashEntities(int id1, int id2) {
		// Ensure consistent hashing regardless of order
		if (id1 > id2) std::swap(id1, id2);
		return static_cast<uint32_t>(id1) ^ (static_cast<uint32_t>(id2) << 16);
	}

public:
	bool CheckVisibility(edict_t* ent1, edict_t* ent2, std::function<bool(edict_t*, edict_t*)> visibility_fn, bool& cached) {
		if (!ent1 || !ent2) {
			cached = false;
			return false;
		}

		int id1 = ent1->s.number;
		int id2 = ent2->s.number;
		uint32_t hash = HashEntities(id1, id2);
		size_t idx = hash % CACHE_SIZE;

		// Normalize IDs for consistent lookup
		if (id1 > id2) std::swap(id1, id2);

		// Check if cached
		if (cache[idx].timestamp + CACHE_LIFETIME > level.time &&
		    cache[idx].entity_id1 == id1 &&
		    cache[idx].entity_id2 == id2) {
			cached = true;
			return cache[idx].visible;
		}

		// Calculate and cache
		bool visible = visibility_fn(ent1, ent2);

		cache[idx].entity_id1 = id1;
		cache[idx].entity_id2 = id2;
		cache[idx].visible = visible;
		cache[idx].timestamp = level.time;
		cached = false;

		return visible;
	}

	void Clear() {
		for (auto& entry : cache) {
			entry.timestamp = 0_ms;
		}
	}

	void InvalidateEntity(int entity_id) {
		// Invalidate all entries involving this entity
		for (auto& entry : cache) {
			if (entry.entity_id1 == entity_id || entry.entity_id2 == entity_id) {
				entry.timestamp = 0_ms;
			}
		}
	}
};

inline VisibilityCache g_visibility_cache;

// ============================================================================
// Common Distance Thresholds Cache
// ============================================================================
namespace CommonDistances {
	// Pre-computed squared distances for common thresholds
	constexpr float SQ_100 = 100.0f * 100.0f;   // 10000
	constexpr float SQ_200 = 200.0f * 200.0f;   // 40000
	constexpr float SQ_265 = 265.0f * 265.0f;   // 70225
	constexpr float SQ_300 = 300.0f * 300.0f;   // 90000
	constexpr float SQ_400 = 400.0f * 400.0f;   // 160000
	constexpr float SQ_500 = 500.0f * 500.0f;   // 250000
	constexpr float SQ_600 = 600.0f * 600.0f;   // 360000
	constexpr float SQ_1000 = 1000.0f * 1000.0f; // 1000000

	// Helper functions for common checks
	[[nodiscard]] constexpr bool IsInMeleeRange(float dist_sq) {
		return dist_sq < SQ_100;
	}

	[[nodiscard]] constexpr bool IsInShortRange(float dist_sq) {
		return dist_sq < SQ_265;
	}

	[[nodiscard]] constexpr bool IsInMediumRange(float dist_sq) {
		return dist_sq < SQ_600;
	}

	[[nodiscard]] constexpr bool IsInLongRange(float dist_sq) {
		return dist_sq < SQ_1000;
	}
}

} // namespace HordePerf