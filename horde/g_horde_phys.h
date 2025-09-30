#pragma once

#include "../g_local.h" // For edict_t, vec3_t
#include <span>        // For std::span
#include <array>       // For std::array
#include <vector>      // For std::vector
#include <unordered_map> // For std::unordered_map

namespace HordePhys {

    // Helper function to get water level for a position
    water_level_t GetWaterLevelForPosition(const vec3_t& in_point);

    // A simple grid cell that holds pointers to monsters.
    struct ProximityGridCell {
        static constexpr size_t MAX_MONSTERS_PER_CELL = 128; // Increased for coop/sp modes with more entities
        std::array<edict_t*, MAX_MONSTERS_PER_CELL> monsters;
        size_t count = 0;

        void clear() { count = 0; }

        void add(edict_t* ent) {
            if (count < MAX_MONSTERS_PER_CELL) {
                monsters[count++] = ent;
            }
            else {
                if (developer->integer) {
                    gi.Com_PrintFmt("ProximityGrid WARNING: Cell is full! (Max {}). Cannot add entity '{}'.\n",
                        MAX_MONSTERS_PER_CELL,
                        ent->classname ? ent->classname : "unknown");
                }
            }
        }
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
        // Cache for entity types to avoid repeated classname checks
        // Using unordered_map for sparse storage (most entity slots are unused)
        std::unordered_map<int, uint32_t> m_entity_types;

        // Buffer for filtered query results (thread-safe member instead of static)
        std::array<edict_t*, MAX_QUERY_RESULTS> m_filtered_buffer;

        uint32_t GetEntityType(edict_t* ent) const;
    };

    extern EntityGrid g_entity_grid;

} // namespace HordePhys