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
        // --- MOVED TO PUBLIC ---
        // This constant is now needed by the emergency spawn logic.
        static constexpr int GRID_DIMENSION = 16;

        // --- PUBLIC API ---
        void Reset();
        void DebugDraw();
        void Build(const vec3_t& world_mins, const vec3_t& world_maxs);
        void Add(edict_t* ent);
        std::span<edict_t* const> GetPotentialColliders(edict_t* ent);

        // --- MOVED TO PUBLIC ---
        // Helper to convert a world coordinate to a grid index.
        // Needed by the emergency spawn logic.
        int GetCellIndex(const vec3_t& pos) const;

        // --- NEW PUBLIC ACCESSORS ---
        // These are required by the new FindEmergencySpawnPositionViaGridSearch function.
        [[nodiscard]] bool IsBuilt() const noexcept { return m_is_built; }
        [[nodiscard]] float GetCellSize() const noexcept { return m_cell_size; }
        [[nodiscard]] const vec3_t& GetWorldMins() const noexcept { return m_world_mins; }
        
        // +++ NEW +++
        // This accessor lets the spawn logic check if a grid cell is in the playable area.
        [[nodiscard]] bool IsCellWalkable(int cell_index) const {
            if (cell_index < 0 || cell_index >= CELL_COUNT) return false;
            return m_is_cell_walkable[cell_index];
        }

    private:
        // --- PRIVATE DATA ---
        static constexpr int CELL_COUNT = GRID_DIMENSION * GRID_DIMENSION;
        std::array<ProximityGridCell, CELL_COUNT> m_cells;
        
        // +++ NEW +++
        // This array stores whether a cell is over solid ground or over a void.
        std::array<bool, CELL_COUNT> m_is_cell_walkable;

        std::array<edict_t*, MAX_EDICTS> m_query_buffer;

        vec3_t m_world_mins;
        float m_cell_size = 0.0f;
        float m_inv_cell_size = 0.0f;
        bool m_is_built = false;
    };

    // The single global instance of our grid.
    extern ProximityGrid g_monster_grid;

} // namespace HordePhys