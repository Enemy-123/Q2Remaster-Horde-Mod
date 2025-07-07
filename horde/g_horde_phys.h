// In: g_horde_phys.h

#pragma once

#include "../g_local.h" // For edict_t, vec3_t
#include <span>        // For std::span
#include <array>

namespace HordePhys {

    // A simple grid cell that holds pointers to monsters.
    struct ProximityGridCell {
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
    public: // <-- Make sure this is public
        static constexpr int GRID_DIMENSION = 16;
        // --- MOVED TO PUBLIC ---
        static constexpr int CELL_COUNT = GRID_DIMENSION * GRID_DIMENSION;

        // --- PUBLIC API ---
        void Build(const vec3_t& world_mins, const vec3_t& world_maxs);
        void Add(edict_t* ent);
        std::span<edict_t* const> GetPotentialColliders(edict_t* ent);

        int GetCellIndex(const vec3_t& pos) const;

        // --- PUBLIC ACCESSORS ---
        [[nodiscard]] bool IsBuilt() const noexcept { return m_is_built; }
        [[nodiscard]] float GetCellSize() const noexcept { return m_cell_size; }
        [[nodiscard]] const vec3_t& GetWorldMins() const noexcept { return m_world_mins; }
        [[nodiscard]] bool IsCellWalkable(int cell_index) const;

    private:
        // --- PRIVATE DATA ---
        // CELL_COUNT was moved to public
        std::array<ProximityGridCell, CELL_COUNT> m_cells;
        std::array<bool, CELL_COUNT> m_is_cell_walkable;
        
        // This buffer holds the results of a query.
        std::array<edict_t*, MAX_EDICTS> m_query_buffer;

        // +++ NEW: EFFICIENT QUERY TRACKING +++
        // This array stores the last query ID an entity was a part of.
        // This avoids clearing a large boolean array on every call.
        std::array<unsigned int, MAX_EDICTS> m_entity_query_stamps;
        // This ID is incremented for each call to GetPotentialColliders.
        unsigned int m_current_query_id = 0;
        // +++ END NEW +++

        vec3_t m_world_mins;
        float m_cell_size = 0.0f;
        float m_inv_cell_size = 0.0f;
        bool m_is_built = false;
    };

    // The single global instance of our grid.
    extern ProximityGrid g_monster_grid;

} // namespace HordePhys