#pragma once

#include "../g_local.h" // For edict_t, vec3_t
#include <span>        // For std::span
#include <array>       // For std::array
#include <vector>      // For std::vector
#include <unordered_map> // For std::unordered_map
#include <boost/container/small_vector.hpp> // For small_vector optimization

namespace HordePhys {

    // Helper function to get water level for a position
    water_level_t GetWaterLevelForPosition(const vec3_t& in_point);

    // A simple grid cell that holds pointers to monsters.
    struct ProximityGridCell {
        // Using small_vector for inline storage - avoids heap allocation for typical case
        // Stores 32 pointers inline (256 bytes), only allocates heap if cell exceeds 32 entities
        // This eliminates heap fragmentation and improves cache locality
        static constexpr size_t TYPICAL_ENTITIES_PER_CELL = 32;
        boost::container::small_vector<edict_t*, TYPICAL_ENTITIES_PER_CELL> monsters;

        ProximityGridCell() = default;  // No need for reserve() - storage is inline

        void clear() {
            monsters.clear();
            // Inline storage is preserved, no deallocation
        }

        void add(edict_t* ent) {
            monsters.push_back(ent);
        }

        size_t count() const { return monsters.size(); }
    };

    // The main grid class that manages monster proximity checks.
    class ProximityGrid {
    public:
        static constexpr int GRID_DIMENSION = 16;
        static constexpr int CELL_COUNT = GRID_DIMENSION * GRID_DIMENSION;
        static constexpr size_t MAX_QUERY_RESULTS = 512;

        void Reset();
        void DebugDraw();
        void Build(const vec3_t& world_mins, const vec3_t& world_maxs);
        void Add(edict_t* ent);
        void Remove(edict_t* ent);
        std::span<edict_t* const> GetPotentialColliders(edict_t* ent);
        int GetCellIndex(const vec3_t& pos) const;

        std::span<edict_t* const> QueryRadius(const vec3_t& origin, const float radius);

        [[nodiscard]] bool IsBuilt() const noexcept { return m_is_built; }
        [[nodiscard]] float GetCellSize() const noexcept { return m_cell_size; }
        [[nodiscard]] const vec3_t& GetWorldMins() const noexcept { return m_world_mins; }

    protected:
        std::array<ProximityGridCell, CELL_COUNT> m_cells;
        std::array<edict_t*, MAX_QUERY_RESULTS> m_query_buffer;

        // --- MODIFICATION: Reusable buffer to avoid heap allocations in queries ---
        std::array<bool, MAX_EDICTS> m_visited_entities;
        // --- END MODIFICATION ---

        vec3_t m_world_mins;
        float m_cell_size = 0.0f;
        float m_inv_cell_size = 0.0f;
        bool m_is_built = false;

    private:
        // Helper method for common query logic
        template<typename FilterFunc>
        std::span<edict_t* const> QueryCellRange(int min_x, int max_x, int min_y, int max_y, FilterFunc&& filter);
    };

    extern ProximityGrid g_monster_grid;

    // General entity grid for all entity types (not just monsters)
    class EntityGrid : public ProximityGrid {
    public:
        // Add entity with type filtering
        void AddEntity(edict_t* ent);

        // Query entities by type flags
        std::span<edict_t* const> QueryRadiusFiltered(const vec3_t& origin, const float radius, const uint32_t type_mask = 0xFFFFFFFF);

        // Update entity position (for moving entities)
        void UpdateEntity(edict_t* ent);

        // Entity type masks for filtering
        enum EntityTypeMask : uint32_t {
            TYPE_PLAYERS    = 1 << 0,
            TYPE_MONSTERS   = 1 << 1,
            TYPE_ITEMS      = 1 << 2,
            TYPE_PROJECTILES = 1 << 3,
            TYPE_TRIGGERS   = 1 << 4,
            TYPE_ALL        = 0xFFFFFFFF
        };

    private:
        // Buffer for filtered query results (thread-safe member instead of static)
        std::array<edict_t*, MAX_QUERY_RESULTS> m_filtered_buffer;

        uint32_t GetEntityType(edict_t* ent) const;
    };

    extern EntityGrid g_entity_grid;

    // =======================================================================
    // Spawn Grid System - Pre-validated spawn positions across the map
    // Ported from Vortex mod's grid system to prevent out-of-map spawns
    // =======================================================================

    class SpawnGrid {
    public:
        static constexpr int MAX_GRID_NODES = 10000;
        static constexpr int GRID_DIMENSION = 64;   // 64x64x64 grid cells (adaptive to map size)
        static constexpr int GRID_SPACING = 16;     // 16 units between grid points

        // Generate the spawn grid for the current map
        // Scans the entire map and validates spawn positions
        bool Generate(const vec3_t& world_mins, const vec3_t& world_maxs, bool force_regenerate = false);

        // Get a random validated spawn position from the grid
        bool GetRandomPosition(vec3_t& out_pos) const;

        // Get a random position within distance range from a point
        bool GetRandomPositionNear(const vec3_t& center, float min_dist, float max_dist, vec3_t& out_pos) const;

        // Get number of grid nodes generated
        [[nodiscard]] int GetNodeCount() const noexcept { return m_node_count; }

        // Check if grid has been generated
        [[nodiscard]] bool IsGenerated() const noexcept { return m_node_count > 0; }

        // Check if a position is within the playable map boundaries
        [[nodiscard]] bool IsPositionInBounds(const vec3_t& pos, float tolerance = 128.0f) const noexcept;

        // Clear the grid
        void Clear();

        // Save/load grid to disk for faster map loads
        bool SaveToDisk(const char* mapname) const;
        bool LoadFromDisk(const char* mapname);

    private:
        std::vector<vec3_t> m_grid_nodes;
        int m_node_count = 0;
        vec3_t m_world_mins{};
        vec3_t m_world_maxs{};
        vec3_t m_grid_size{};  // Size per grid cell

        // Helper functions for grid generation (ported from Vortex)
        bool ValidateSpawnPosition(const vec3_t& pos, const vec3_t& mins, const vec3_t& maxs) const;
        bool CheckBottom(const vec3_t& pos, const vec3_t& boxmin, const vec3_t& boxmax) const;
        bool IsNearbyGridNode(const vec3_t& pos, int current_count, float min_distance = 129.0f) const;

        // Grid coordinate conversion (maps world coords to grid indices using actual map bounds)
        vec3_t GridToWorld(int x, int y, int z) const;
        void WorldToGrid(const vec3_t& world_pos, int& out_x, int& out_y, int& out_z) const;
    };

    extern SpawnGrid g_spawn_grid;

} // namespace HordePhys