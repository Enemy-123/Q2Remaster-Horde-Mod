#include "g_horde_phys.h"
#include "../g_local.h"
#include <algorithm> // For std::min/max

namespace HordePhys {

    ProximityGrid g_monster_grid;

    void ProximityGrid::Build(const vec3_t& world_mins, const vec3_t& world_maxs) {
        for (auto& cell : m_cells) {
            cell.clear();
        }

        m_world_mins = world_mins;
        const vec3_t world_size = world_maxs - world_mins;

        // Use a small epsilon to prevent division by zero on flat maps
        constexpr float epsilon = 1.0f;
        m_cell_size = std::max(std::max(world_size.x, world_size.y), epsilon) / static_cast<float>(GRID_DIMENSION);
        
        m_inv_cell_size = 1.0f / m_cell_size;
        m_is_built = true;

        // NEW: Debug print to verify grid dimensions
        if (developer->integer >= 2) {
            gi.Com_PrintFmt("ProximityGrid Built: Mins({:.1f},{:.1f}) Size({:.1f},{:.1f}) CellSize:{:.1f}\n",
                m_world_mins.x, m_world_mins.y, world_size.x, world_size.y, m_cell_size);
        }
    }

    // GetCellIndex and Add remain the same as the previous robust version...
    int ProximityGrid::GetCellIndex(const vec3_t& pos) const {
        if (!m_is_built) return -1;
        int grid_x = static_cast<int>((pos.x - m_world_mins.x) * m_inv_cell_size);
        int grid_y = static_cast<int>((pos.y - m_world_mins.y) * m_inv_cell_size);
        grid_x = std::clamp(grid_x, 0, GRID_DIMENSION - 1);
        grid_y = std::clamp(grid_y, 0, GRID_DIMENSION - 1);
        return grid_y * GRID_DIMENSION + grid_x;
    }

    void ProximityGrid::Add(edict_t* ent) {
        if (!m_is_built || !ent) return;
        int min_idx = GetCellIndex(ent->absmin);
        int max_idx = GetCellIndex(ent->absmax);
        if (min_idx != -1) m_cells[min_idx].add(ent);
        if (max_idx != -1 && max_idx != min_idx) m_cells[max_idx].add(ent);
    }

    // GetPotentialColliders remains the same as the previous robust version...
    std::span<edict_t* const> ProximityGrid::GetPotentialColliders(edict_t* ent) {
        if (!m_is_built || !ent) return {};
        size_t buffer_count = 0;
        int min_x = std::clamp(static_cast<int>((ent->absmin.x - m_world_mins.x) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
        int max_x = std::clamp(static_cast<int>((ent->absmax.x - m_world_mins.x) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
        int min_y = std::clamp(static_cast<int>((ent->absmin.y - m_world_mins.y) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
        int max_y = std::clamp(static_cast<int>((ent->absmax.y - m_world_mins.y) * m_inv_cell_size), 0, GRID_DIMENSION - 1);

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                int cell_idx = y * GRID_DIMENSION + x;
                const auto& cell = m_cells[cell_idx];
                for (size_t i = 0; i < cell.count; ++i) {
                    edict_t* other = cell.monsters[i];
                    if (other != ent) {
                        bool found = false;
                        for (size_t j = 0; j < buffer_count; ++j) if (m_query_buffer[j] == other) { found = true; break; }
                        if (!found && buffer_count < m_query_buffer.size()) m_query_buffer[buffer_count++] = other;
                    }
                }
            }
        }
        return {m_query_buffer.data(), buffer_count};
    }

    // // NEW: Debug drawing function
    // void ProximityGrid::DebugDraw(float duration) {
    //     if (!m_is_built || developer->integer < 2) {
    //         return;
    //     }
    //     for (int y = 0; y < GRID_DIMENSION; ++y) {
    //         for (int x = 0; x < GRID_DIMENSION; ++x) {
    //             vec3_t min_corner = m_world_mins + vec3_t{x * m_cell_size, y * m_cell_size, 0};
    //             vec3_t max_corner = min_corner + vec3_t{m_cell_size, m_cell_size, 128};
    //             gi.Draw_Bounds(min_corner, max_corner, rgba_green, duration, false);
    //         }
    //     }
    // }

} // namespace HordePhys