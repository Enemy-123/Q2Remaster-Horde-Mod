#include "g_horde_phys.h"
#include "../g_local.h"
#include <algorithm> // For std::min/max

namespace HordePhys {

    ProximityGrid g_monster_grid;

       // =======================================================================
    // REPLACEMENT: ProximityGrid::Build (Definitive Version)
    //
    // This version now performs a multi-sample check for every grid cell.
    // It traces 9 points within each cell to accurately determine if any part
    // of the cell is over playable, solid ground. This creates a much more
    // reliable "walkability map" and is the core of the fix.
    // =======================================================================
    void ProximityGrid::Build(const vec3_t& world_mins, const vec3_t& world_maxs) {
        for (auto& cell : m_cells) {
            cell.clear();
        }
        m_is_cell_walkable.fill(false); // Default all cells to not walkable.

        m_world_mins = world_mins;
        const vec3_t world_size = world_maxs - world_mins;

        constexpr float epsilon = 1.0f;
        m_cell_size = std::max(std::max(world_size.x, world_size.y), epsilon) / static_cast<float>(GRID_DIMENSION);
        
        if (m_cell_size <= 0.0f) {
            m_is_built = false;
            if (developer->integer) gi.Com_PrintFmt("ProximityGrid Build FAILED: Invalid cell size.\n");
            return;
        }
        m_inv_cell_size = 1.0f / m_cell_size;
        m_is_built = true;

        // --- NEW MULTI-SAMPLE GEOMETRY CHECK ---
        int walkable_cell_count = 0;
        for (int y = 0; y < GRID_DIMENSION; ++y) {
            for (int x = 0; x < GRID_DIMENSION; ++x) {
                const int cell_idx = y * GRID_DIMENSION + x;
                
                // We will test a 3x3 grid of points within this cell.
                bool found_walkable_spot = false;
                for (int sub_y = 0; sub_y < 3; ++sub_y) {
                    for (int sub_x = 0; sub_x < 3; ++sub_x) {
                        // Calculate the sample point's position within the cell.
                        vec3_t sample_point = {
                            m_world_mins.x + (x + 0.25f + (sub_x * 0.25f)) * m_cell_size,
                            m_world_mins.y + (y + 0.25f + (sub_y * 0.25f)) * m_cell_size,
                            world_maxs.z // Start the trace from the top of the world.
                        };

                        vec3_t trace_end = sample_point;
                        trace_end.z = world_mins.z; // Trace down to the bottom.

                        trace_t trace = gi.trace(sample_point, vec3_origin, vec3_origin, trace_end, nullptr, MASK_SOLID);

                        // If this sample point hits valid, non-steep ground, the cell is usable.
                        if (trace.fraction < 1.0f && trace.plane.normal.z > 0.7f) {
                            m_is_cell_walkable[cell_idx] = true;
                            found_walkable_spot = true;
                            break; // Found a valid spot, no need to check other samples in this cell.
                        }
                    }
                    if (found_walkable_spot) {
                        break;
                    }
                }

                if (found_walkable_spot) {
                    walkable_cell_count++;
                }
            }
        }
        // --- END NEW LOGIC ---

        if (developer->integer >= 2) {
            gi.Com_PrintFmt("ProximityGrid Built: Mins({:.1f},{:.1f}) Size({:.1f},{:.1f}) CellSize:{:.1f}\n",
                m_world_mins.x, m_world_mins.y, world_size.x, world_size.y, m_cell_size);
            gi.Com_PrintFmt("ProximityGrid Geometry Check: Found {} walkable cells out of {}.\n",
                walkable_cell_count, CELL_COUNT);
        }
    }


    // ... (rest of g_horde_phys.cpp remains the same) ...
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

    std::span<edict_t* const> ProximityGrid::GetPotentialColliders(edict_t* ent) {
        if (!m_is_built || !ent) return {};
        
        static std::array<bool, MAX_EDICTS> already_added;
        already_added.fill(false);

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
                        const int other_idx = other - g_edicts;
                        if (!already_added[other_idx]) {
                            if (buffer_count < m_query_buffer.size()) {
                                m_query_buffer[buffer_count++] = other;
                                already_added[other_idx] = true;
                            }
                        }
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