#pragma once

#include "../g_local.h" // For edict_t, vec3_t
#include <span>        // For std::span
#include <array>       // For std::array
#include <vector>      // For std::vector

namespace HordePhys {

    // A simple grid cell that holds pointers to monsters.
    struct ProximityGridCell {
        static constexpr size_t MAX_MONSTERS_PER_CELL = 64; // This is a good, safe value.
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

        void Reset();
        void DebugDraw();
        void Build(const vec3_t& world_mins, const vec3_t& world_maxs);
        void Add(edict_t* ent);
        std::span<edict_t* const> GetPotentialColliders(edict_t* ent);
        int GetCellIndex(const vec3_t& pos) const;

        std::span<edict_t* const> QueryRadius(const vec3_t& origin, float radius);

        [[nodiscard]] bool IsBuilt() const noexcept { return m_is_built; }
        [[nodiscard]] float GetCellSize() const noexcept { return m_cell_size; }
        [[nodiscard]] const vec3_t& GetWorldMins() const noexcept { return m_world_mins; }

    private:
        std::array<ProximityGridCell, CELL_COUNT> m_cells;

        static constexpr size_t MAX_QUERY_RESULTS = 512;
        std::array<edict_t*, MAX_QUERY_RESULTS> m_query_buffer;

        // --- MODIFICATION: Reusable buffer to avoid heap allocations in queries ---
        std::array<bool, MAX_EDICTS> m_visited_entities;
        // --- END MODIFICATION ---

        vec3_t m_world_mins;
        float m_cell_size = 0.0f;
        float m_inv_cell_size = 0.0f;
        bool m_is_built = false;
    };

    extern ProximityGrid g_monster_grid;

} // namespace HordePhys