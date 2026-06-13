#pragma once

#include "../g_local.h" // For edict_t, vec3_t
#include <span>        // For std::span
#include <array>       // For std::array
#include <vector>      // For std::vector
#include <boost/container/small_vector.hpp>  // For small_vector optimization

namespace HordePhys {

    // Helper function to get water level for a position
    water_level_t GetWaterLevelForPosition(const vec3_t& in_point);

    // Per-entity grid tracking data (stored here instead of on edict_t to reduce entity footprint)
    struct EntityGridTracking {
        int8_t cells[4] = {-1, -1, -1, -1};
        uint8_t cell_count = 0;
    };

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

        // Access grid tracking data for an entity (indexed by entity number)
        EntityGridTracking& GetTracking(edict_t* ent) { return m_tracking[ent->s.number]; }
        const EntityGridTracking& GetTracking(const edict_t* ent) const { return m_tracking[ent->s.number]; }

    protected:
        std::array<ProximityGridCell, CELL_COUNT> m_cells;
        std::array<edict_t*, MAX_QUERY_RESULTS> m_query_buffer;

        // Per-entity tracking: which cells each entity occupies (replaces edict_t::grid_cells/grid_cell_count)
        std::array<EntityGridTracking, MAX_EDICTS> m_tracking{};

        // Query ID system for efficient duplicate detection across cells
        std::array<uint32_t, MAX_EDICTS> m_last_query_ids;
        uint32_t m_current_query_id = 0;

        vec3_t m_world_mins;
        float m_cell_size = 0.0f;
        float m_inv_cell_size = 0.0f;
        bool m_is_built = false;

    private:
        // Helper method for common query logic
        template<typename FilterFunc>
        std::span<edict_t* const> QueryCellRange(int min_x, int max_x, int min_y, int max_y, FilterFunc&& filter);
    };;

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

        // Get/set cached entity type (stored here instead of on edict_t)
        uint32_t GetCachedEntityType(const edict_t* ent) const { return m_cached_types[ent->s.number]; }
        void SetCachedEntityType(edict_t* ent, uint32_t type) { m_cached_types[ent->s.number] = type; }

    private:
        // Buffer for filtered query results (thread-safe member instead of static)
        std::array<edict_t*, MAX_QUERY_RESULTS> m_filtered_buffer;

        // Per-entity cached type flags (replaces edict_t::cached_entity_type)
        std::array<uint32_t, MAX_EDICTS> m_cached_types{};

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
        static constexpr size_t MAX_COOLDOWN_POSITIONS = 32;  // Ring buffer size for cooldown tracking

        // Generate the spawn grid for the current map
        // Scans the entire map and validates spawn positions
        bool Generate(const vec3_t& world_mins, const vec3_t& world_maxs, bool force_regenerate = false);

        // Get a random validated spawn position from the grid
        bool GetRandomPosition(vec3_t& out_pos) const;

        // Get a random position within distance range from a point
        bool GetRandomPositionNear(const vec3_t& center, float min_dist, float max_dist, vec3_t& out_pos) const;

        // Get a tactical spawn position (not visible to players, with distance checks)
        // min_dist_from_players: Minimum distance from any player
        // prefer_out_of_visibility: If true, strongly prefer positions not visible to players
        // Returns false if no suitable position found after max_attempts
        bool GetTacticalSpawnPosition(vec3_t& out_pos, float min_dist_from_players = 512.0f, int max_attempts = 20, bool prefer_out_of_visibility = false) const;

        // Mark a grid position as recently used (adds cooldown)
        void MarkPositionUsed(const vec3_t& pos);

        // Check if a position is too close to a recently used position (on cooldown)
        bool IsPositionOnCooldown(const vec3_t& pos, float cooldown_radius) const;

        // Clear all cooldowns (call on map change/wave reset)
        void ClearCooldowns();

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
        // Fixed-capacity grid nodes - no heap allocation, m_node_count tracks active entries.
        std::array<vec3_t, MAX_GRID_NODES> m_grid_nodes{};
        int m_node_count = 0;
        vec3_t m_world_mins{};
        vec3_t m_world_maxs{};
        vec3_t m_grid_size{};  // Size per grid cell

        // Cooldown tracking - ring buffer of recently used positions
        mutable std::array<vec3_t, MAX_COOLDOWN_POSITIONS> m_cooldown_positions{};
        mutable std::array<gtime_t, MAX_COOLDOWN_POSITIONS> m_cooldown_expiry{};
        mutable size_t m_cooldown_write_index = 0;

        // Helper functions for grid generation (ported from Vortex)
        bool ValidateSpawnPosition(const vec3_t& pos, const vec3_t& mins, const vec3_t& maxs) const;
        bool CheckBottom(const vec3_t& pos, const vec3_t& boxmin, const vec3_t& boxmax) const;
        bool IsNearbyGridNode(const vec3_t& pos, int current_count, float min_distance = 129.0f) const;

        // Tactical spawning helper - checks if position is visible to any active player
        bool IsVisibleToPlayers(const vec3_t& pos) const;

        // Grid coordinate conversion (maps world coords to grid indices using actual map bounds)
        vec3_t GridToWorld(int x, int y, int z) const;
        void WorldToGrid(const vec3_t& world_pos, int& out_x, int& out_y, int& out_z) const;
    };

    extern SpawnGrid g_spawn_grid;

} // namespace HordePhys
