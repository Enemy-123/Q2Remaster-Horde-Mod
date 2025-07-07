#include "g_horde_phys.h"
#include "../g_local.h"
#include <algorithm> // For std::min/max
#include <set>

// to fix, add a debug void
namespace HordePhys
{

    ProximityGrid g_monster_grid;

    // NEW: Fully corrected debug function for your specific engine.
    void ProximityGrid::DebugDraw()
    {
        if (developer->integer < 2)
        {
            return;
        }

        if (!m_is_built)
        {
            return;
        }

        // Sanity check: If cell size is zero or invalid, we can't draw.
        if (m_cell_size <= 0.0f)
        {
            static bool printed_size_error = false;
            if (!printed_size_error)
            {
                gi.Com_PrintFmt("ProximityGrid::DebugDraw: SKIPPING, cell size is invalid ({}).\n", m_cell_size);
                printed_size_error = true;
            }
            return;
        }

        int drawn_count = 0;

        for (int y = 0; y < GRID_DIMENSION; ++y)
        {
            for (int x = 0; x < GRID_DIMENSION; ++x)
            {
                const int cell_idx = y * GRID_DIMENSION + x;
                if (m_is_cell_walkable[cell_idx])
                {
                    // Get the stored ground height for this specific cell
                    const float ground_z = m_cell_ground_z[cell_idx];

                    // Draw the box right on the ground
                    vec3_t cell_min = {m_world_mins.x + x * m_cell_size, m_world_mins.y + y * m_cell_size, ground_z};
                    vec3_t cell_max = cell_min + vec3_t{m_cell_size, m_cell_size, 1.0f}; // Make it 1 unit tall

                    gi.Draw_Bounds(cell_min, cell_max, rgba_green, FRAME_TIME_S.seconds<float>() + 0.01f, false);
                    drawn_count++;
                }
            }
        }

        // Print a confirmation to the console, but only occasionally.
        if (FRAME_TIME_MS)
        {
            gi.Com_PrintFmt("ProximityGrid::DebugDraw: Visualizing {} walkable cells.\n", drawn_count);
        }
    }

    // =======================================================================
    // DEFINITIVE VERSION: ProximityGrid::Build
    //
    // This function now uses a four-step process for maximum accuracy:
    // 1. RAW SCAN: Traces downwards in every cell to find all potential
    //    walkable surfaces, storing the lowest valid point.
    // 2. SEEDING: Gathers known "good" locations from map entities
    //    (player spawns, items, etc.).
    // 3. VERIFICATION: Performs a flood-fill starting from the seed points
    //    to find all walkable cells connected to the main playable area.
    // 4. FINALIZATION: Prunes any "island" cells that were found in the
    //    raw scan but were not reached by the flood-fill.
    // =======================================================================
    void ProximityGrid::Build(const vec3_t &world_mins, const vec3_t &world_maxs)
    {
        // --- INITIALIZATION ---
        for (auto &cell : m_cells)
        {
            cell.clear();
        }
        m_is_cell_walkable.fill(false);
        m_cell_ground_z.fill(0.0f);
        m_is_cell_verified.fill(false);

        m_world_mins = world_mins;
        const vec3_t world_size = world_maxs - world_mins;

        constexpr float epsilon = 1.0f;
        m_cell_size = std::max(std::max(world_size.x, world_size.y), epsilon) / static_cast<float>(GRID_DIMENSION);

        if (m_cell_size <= 0.0f)
        {
            m_is_built = false;
            if (developer->integer)
                gi.Com_PrintFmt("ProximityGrid Build FAILED: Invalid cell size.\n");
            return;
        }
        m_inv_cell_size = 1.0f / m_cell_size;
        m_is_built = true;

        // --- STEP 1: RAW GEOMETRY SCAN (with Iterative Tracing) ---
        if (developer->integer >= 2)
            gi.Com_PrintFmt("ProximityGrid: Performing iterative geometry scan...\n");
        int raw_walkable_count = 0;
        for (int y = 0; y < GRID_DIMENSION; ++y)
        {
            for (int x = 0; x < GRID_DIMENSION; ++x)
            {
                const int cell_idx = y * GRID_DIMENSION + x;

                // Use a set to store all unique floor heights found in this cell.
                std::set<float> floor_z_values;

                // For each of the 9 sample points in the cell...
                for (int sub_y = 0; sub_y < 3; ++sub_y)
                {
                    for (int sub_x = 0; sub_x < 3; ++sub_x)
                    {
                        vec3_t sample_xy = {
                            m_world_mins.x + (x + 0.25f + (sub_x * 0.25f)) * m_cell_size,
                            m_world_mins.y + (y + 0.25f + (sub_y * 0.25f)) * m_cell_size,
                            0 // Z will be set in the loop
                        };

                        // --- ITERATIVE TRACE LOOP ---
                        // Start from the top of the world.
                        float current_z = world_maxs.z;
                        while (current_z > world_mins.z)
                        {
                            vec3_t trace_start = sample_xy;
                            trace_start.z = current_z;

                            vec3_t trace_end = sample_xy;
                            trace_end.z = world_mins.z;

                            trace_t trace = gi.trace(trace_start, vec3_origin, vec3_origin, trace_end, nullptr, MASK_SOLID);

                            // If we didn't hit anything, we're done with this column.
                            if (trace.fraction >= 1.0f)
                            {
                                break;
                            }

                            // If we hit a valid, walkable floor...
                            if (trace.plane.normal.z > 0.7f)
                            {
                                // ...add its height to our set of floors.
                                floor_z_values.insert(trace.endpos.z);
                            }

                            // Prepare for the next iteration by starting just below the surface we just hit.
                            current_z = trace.endpos.z - 1.0f;
                        }
                    }
                }

                // If we found any valid floors in this cell...
                if (!floor_z_values.empty())
                {
                    m_is_cell_walkable[cell_idx] = true;
                    // Store the LOWEST floor height found. The set is sorted, so we take the first element.
                    m_cell_ground_z[cell_idx] = *floor_z_values.begin();
                    raw_walkable_count++;
                }
            }
        }
        if (developer->integer >= 2)
            gi.Com_PrintFmt("ProximityGrid Raw Scan: Found {} potential walkable cells.\n", raw_walkable_count);

        // --- STEP 2: GATHER SEED POINTS ---
        std::vector<vec3_t> seed_points;
        for (int i = 1; i < globals.num_edicts; ++i)
        {
            edict_t *ent = &g_edicts[i];
            if (!ent->inuse || !ent->classname)
                continue;

            // Use a case-insensitive prefix comparison for robustness.
            // Q_strncasecmp is the ideal function if it exists in your engine.
            // If not, try _strnicmp (Windows) or strncasecmp (Linux).
            if (!Q_strncasecmp(ent->classname, "info_player", 11) ||
                !Q_strncasecmp(ent->classname, "item_", 5) ||
                !Q_strncasecmp(ent->classname, "weapon_", 7) ||
                !Q_strncasecmp(ent->classname, "ammo_", 5) ||
                !Q_strncasecmp(ent->classname, "misc_teleporter", 15) ||
                !Q_strncasecmp(ent->classname, "path_corner", 11))
            {
                seed_points.push_back(ent->s.origin);
            }
        }
        if (developer->integer >= 2)
            gi.Com_PrintFmt("ProximityGrid: Found {} seed points for verification.\n", seed_points.size());

        // --- STEP 3: FLOOD FILL VERIFICATION ---
        std::vector<int> queue;
        queue.reserve(CELL_COUNT);

        for (const auto &point : seed_points)
        {
            int seed_idx = GetCellIndex(point);
            if (seed_idx != -1 && m_is_cell_walkable[seed_idx] && !m_is_cell_verified[seed_idx])
            {
                queue.clear();
                queue.push_back(seed_idx);
                m_is_cell_verified[seed_idx] = true;

                size_t head = 0;
                while (head < queue.size())
                {
                    int current_idx = queue[head++];
                    int current_x = current_idx % GRID_DIMENSION;
                    int current_y = current_idx / GRID_DIMENSION;

                    const int neighbors[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
                    for (const auto &offset : neighbors)
                    {
                        int neighbor_x = current_x + offset[0];
                        int neighbor_y = current_y + offset[1];

                        if (neighbor_x >= 0 && neighbor_x < GRID_DIMENSION && neighbor_y >= 0 && neighbor_y < GRID_DIMENSION)
                        {
                            int neighbor_idx = neighbor_y * GRID_DIMENSION + neighbor_x;
                            if (m_is_cell_walkable[neighbor_idx] && !m_is_cell_verified[neighbor_idx])
                            {
                                m_is_cell_verified[neighbor_idx] = true;
                                queue.push_back(neighbor_idx);
                            }
                        }
                    }
                }
            }
        }

        // --- STEP 4: FINALIZE THE GRID ---
        int final_walkable_count = 0;
        for (int i = 0; i < CELL_COUNT; ++i)
        {
            if (m_is_cell_walkable[i] && !m_is_cell_verified[i])
            {
                m_is_cell_walkable[i] = false;
            }
            if (m_is_cell_walkable[i])
            {
                final_walkable_count++;
            }
        }

        // --- FINAL DEBUG OUTPUT ---
        if (developer->integer >= 2)
        {
            gi.Com_PrintFmt("ProximityGrid Built: Mins({:.1f},{:.1f}) Size({:.1f},{:.1f}) CellSize:{:.1f}\n",
                            m_world_mins.x, m_world_mins.y, world_size.x, world_size.y, m_cell_size);
            gi.Com_PrintFmt("ProximityGrid Geometry Check: Found {} final walkable cells out of {}.\n",
                            final_walkable_count, CELL_COUNT);
        }
    }

// ... (rest of g_horde_phys.cpp remains the same) ...
int ProximityGrid::GetCellIndex(const vec3_t &pos) const
{
    if (!m_is_built)
        return -1;
    int grid_x = static_cast<int>((pos.x - m_world_mins.x) * m_inv_cell_size);
    int grid_y = static_cast<int>((pos.y - m_world_mins.y) * m_inv_cell_size);
    grid_x = std::clamp(grid_x, 0, GRID_DIMENSION - 1);
    grid_y = std::clamp(grid_y, 0, GRID_DIMENSION - 1);
    return grid_y * GRID_DIMENSION + grid_x;
}

void ProximityGrid::Add(edict_t *ent)
{
    if (!m_is_built || !ent)
        return;
    int min_idx = GetCellIndex(ent->absmin);
    int max_idx = GetCellIndex(ent->absmax);
    if (min_idx != -1)
        m_cells[min_idx].add(ent);
    if (max_idx != -1 && max_idx != min_idx)
        m_cells[max_idx].add(ent);
}

std::span<edict_t *const> ProximityGrid::GetPotentialColliders(edict_t *ent)
{
    if (!m_is_built || !ent)
        return {};

    static std::array<bool, MAX_EDICTS> already_added;
    already_added.fill(false);

    size_t buffer_count = 0;
    int min_x = std::clamp(static_cast<int>((ent->absmin.x - m_world_mins.x) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
    int max_x = std::clamp(static_cast<int>((ent->absmax.x - m_world_mins.x) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
    int min_y = std::clamp(static_cast<int>((ent->absmin.y - m_world_mins.y) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
    int max_y = std::clamp(static_cast<int>((ent->absmax.y - m_world_mins.y) * m_inv_cell_size), 0, GRID_DIMENSION - 1);

    for (int y = min_y; y <= max_y; ++y)
    {
        for (int x = min_x; x <= max_x; ++x)
        {
            int cell_idx = y * GRID_DIMENSION + x;
            const auto &cell = m_cells[cell_idx];
            for (size_t i = 0; i < cell.count; ++i)
            {
                edict_t *other = cell.monsters[i];
                if (other != ent)
                {
                    const int other_idx = other - g_edicts;
                    if (!already_added[other_idx])
                    {
                        if (buffer_count < m_query_buffer.size())
                        {
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

void ProximityGrid::Reset()
{
    // This just clears the dynamic monster data from each cell,
    // leaving the m_is_cell_walkable data intact.
    for (auto &cell : m_cells)
    {
        cell.clear();
    }
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