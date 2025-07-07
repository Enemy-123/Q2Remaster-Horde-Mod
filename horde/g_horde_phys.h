#pragma once

#include "../g_local.h" // For edict_t, vec3_t
#include <span>        // For std::span
#include <array>       // For std::array
#include <vector>      // For std::vector

namespace HordePhys {

    // A simple grid cell that holds pointers to monsters.
    struct ProximityGridCell {
        static constexpr size_t MAX_MONSTERS_PER_CELL = 16;
        std::array<edict_t*, MAX_MONSTERS_PER_CELL> monsters;
        size_t count = 0;

        void clear() { count = 0; }
        void add(edict_t* ent) {
            if (count < MAX_MONSTERS_PER_CELL) {
                monsters[count++] = ent;
            }
        }
    };

    // The main grid class that manages monster proximity checks.
    class ProximityGrid {
    public:
        static constexpr int GRID_DIMENSION = 16;
        static constexpr int CELL_COUNT = GRID_DIMENSION * GRID_DIMENSION;

        void Reset();
        void DebugDraw();
        void Build(const vec3_t& world_mins, const vec3_t& world_maxs);
        void Add(edict_t* ent);
        std::span<edict_t* const> GetPotentialColliders(edict_t* ent);
        int GetCellIndex(const vec3_t& pos) const;

        [[nodiscard]] bool IsBuilt() const noexcept { return m_is_built; }
        [[nodiscard]] float GetCellSize() const noexcept { return m_cell_size; }
        [[nodiscard]] const vec3_t& GetWorldMins() const noexcept { return m_world_mins; }
        
        // [[nodiscard]] bool IsCellWalkable(int cell_index) const {
        //     if (cell_index < 0 || cell_index >= CELL_COUNT) return false;
        //     return m_is_cell_walkable[cell_index];
        // }

    private:
        std::array<ProximityGridCell, CELL_COUNT> m_cells;
        //std::array<bool, CELL_COUNT> m_is_cell_walkable;
        //std::array<float, CELL_COUNT> m_cell_ground_z;
        //std::array<bool, CELL_COUNT> m_is_cell_verified; // Internal helper for Build()

        std::array<edict_t*, MAX_EDICTS> m_query_buffer;

        vec3_t m_world_mins;
        float m_cell_size = 0.0f;
        float m_inv_cell_size = 0.0f;
        bool m_is_built = false;
    };

    extern ProximityGrid g_monster_grid;

} // namespace HordePhys