#pragma once

#include "../g_local.h" // For edict_t, vec3_t
#include <span>        // For std::span

namespace HordePhys {

    // A simple grid cell that holds pointers to monsters.
    struct ProximityGridCell {
        // We use a small, fixed-size array to avoid heap allocations,
        // which is crucial for performance in a per-frame system.
        static constexpr size_t MAX_MONSTERS_PER_CELL = 16;
        std::array<edict_t*, MAX_MONSTERS_PER_CELL> monsters;
        size_t count = 0;

        void clear() {
            count = 0;
        }

        void add(edict_t* ent) {
            if (count < MAX_MONSTERS_PER_CELL) {
                monsters[count++] = ent;
            }
        }
    };

    // The main grid class that manages monster proximity checks.
    class ProximityGrid {
    public:
        // Call this once per frame before running physics.
        // It calculates the grid dimensions based on the world size.
        void Build(const vec3_t& world_mins, const vec3_t& world_maxs);

        // Call this for each active monster to place it in the grid.
        void Add(edict_t* ent);

        // The core query function. Given an entity, it returns a list of
        // other monsters that are in the same or adjacent grid cells.
        // This is the list of potential colliders.
        std::span<edict_t* const> GetPotentialColliders(edict_t* ent);

    private:
        // Using a fixed-size grid. 16x16 = 256 cells is a good starting point.
        static constexpr int GRID_DIMENSION = 16;
        static constexpr int CELL_COUNT = GRID_DIMENSION * GRID_DIMENSION;
        std::array<ProximityGridCell, CELL_COUNT> m_cells;

        // A single, reusable buffer for query results to avoid allocations.
        std::array<edict_t*, MAX_EDICTS> m_query_buffer;

        // Cached grid properties for fast calculations.
        vec3_t m_world_mins;
        float m_cell_size = 0.0f;
        float m_inv_cell_size = 0.0f;
        bool m_is_built = false;

        // Helper to convert a world coordinate to a grid index.
        int GetCellIndex(const vec3_t& pos) const;
    };

    // The single global instance of our grid.
    extern ProximityGrid g_monster_grid;

} // namespace HordePhys