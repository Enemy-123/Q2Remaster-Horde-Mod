#include "g_horde_phys.h"
#include "../g_local.h"
#include <algorithm> // For std::min/max
#include <set>

// // NEW HELPER FUNCTION
// // Gets the water level for a raw position, simulating a standard monster's bounding box.
// // This is essential for checking potential spawn points before a monster exists there.
water_level_t GetWaterLevelForPosition(const vec3_t& in_point)
{
    // Use a standard medium-sized monster bounding box for the check.
    static const vec3_t monster_mins = {-16, -16, -24};
    static const vec3_t monster_maxs = {16, 16, 32};

    vec3_t point;
    contents_t cont;

    // Check at the monster's "feet"
    point = in_point;
    point[2] += monster_mins[2] + 1;
    cont = gi.pointcontents(point);

    if (!(cont & MASK_WATER)) {
        return WATER_NONE;
    }

    // Check at "waist" height
    point[2] += 26;
    cont = gi.pointcontents(point);
    if (!(cont & MASK_WATER)) {
        return WATER_FEET;
    }

    // Check at "head" height
    point[2] += 22;
    cont = gi.pointcontents(point);
    if (!(cont & MASK_WATER)) {
        return WATER_WAIST;
    }

    return WATER_UNDER;
}

// to fix, add a debug void
namespace HordePhys
{

    ProximityGrid g_monster_grid;

    // // NEW: Fully corrected debug function for your specific engine.
    // void ProximityGrid::DebugDraw()
    // {
    //     if (developer->integer < 2)
    //     {
    //         return;
    //     }

    //     if (!m_is_built)
    //     {
    //         return;
    //     }

    //     // Sanity check: If cell size is zero or invalid, we can't draw.
    //     if (m_cell_size <= 0.0f)
    //     {
    //         static bool printed_size_error = false;
    //         if (!printed_size_error)
    //         {
    //             gi.Com_PrintFmt("ProximityGrid::DebugDraw: SKIPPING, cell size is invalid ({}).\n", m_cell_size);
    //             printed_size_error = true;
    //         }
    //         return;
    //     }

    //     int drawn_count = 0;

    //     for (int y = 0; y < GRID_DIMENSION; ++y)
    //     {
    //         for (int x = 0; x < GRID_DIMENSION; ++x)
    //         {
    //             const int cell_idx = y * GRID_DIMENSION + x;
    //             if (m_is_cell_walkable[cell_idx])
    //             {
    //                 // Get the stored ground height for this specific cell
    //                 const float ground_z = m_cell_ground_z[cell_idx];

    //                 // Draw the box right on the ground
    //                 vec3_t cell_min = {m_world_mins.x + x * m_cell_size, m_world_mins.y + y * m_cell_size, ground_z};
    //                 vec3_t cell_max = cell_min + vec3_t{m_cell_size, m_cell_size, 1.0f}; // Make it 1 unit tall

    //                 gi.Draw_Bounds(cell_min, cell_max, rgba_green, FRAME_TIME_S.seconds<float>() + 0.01f, false);
    //                 drawn_count++;
    //             }
    //         }
    //     }

    //     // Print a confirmation to the console, but only occasionally.
    //     if (FRAME_TIME_MS)
    //     {
    //         gi.Com_PrintFmt("ProximityGrid::DebugDraw: Visualizing {} walkable cells.\n", drawn_count);
    //     }
    // }

    void ProximityGrid::DebugDraw()
{
    if (developer->integer < 2 || !m_is_built)
    {
        return;
    }

    // Draw the grid structure in the air so it's easy to see.
    // You can find a good Z height or just use the middle of the map.
    float draw_z = (m_world_mins.z + 4096) / 2.0f; // Example Z

    for (int y = 0; y < GRID_DIMENSION; ++y)
    {
        for (int x = 0; x < GRID_DIMENSION; ++x)
        {
            const int cell_idx = y * GRID_DIMENSION + x;
            const auto& cell = m_cells[cell_idx];

            vec3_t cell_min = {m_world_mins.x + x * m_cell_size, m_world_mins.y + y * m_cell_size, draw_z};
            vec3_t cell_max = cell_min + vec3_t{m_cell_size, m_cell_size, 1.0f};

            // Color the cell based on how full it is.
            // Green = empty, Yellow = some, Red = full.
            rgba_t color = rgba_green;
            if (cell.count > 0) {
                color = rgba_yellow;
            }
            if (cell.count >= ProximityGridCell::MAX_MONSTERS_PER_CELL) {
                color = rgba_red;
            }

            gi.Draw_Bounds(cell_min, cell_max, color, FRAME_TIME_S.seconds<float>() + 0.01f, false);
        }
    }
}

// =======================================================================
// FINAL, BULLETPROOF VERSION: ProximityGrid::Build
//
// This version adds the final layer of robustness: SEED VALIDATION.
// It ensures that the flood-fill only starts from entities that are
// actually on or very near a walkable surface found in the raw scan.
// This prevents bad seeds (like floating path_corners) from creating
// invalid "island" cells.
// =======================================================================
// void ProximityGrid::Build(const vec3_t& world_mins, const vec3_t& world_maxs) {
//     // --- INITIALIZATION (same as before) ---
//     for (auto& cell : m_cells) {
//         cell.clear();
//     }
//     m_is_cell_walkable.fill(false);
//     m_cell_ground_z.fill(0.0f);
//     m_is_cell_verified.fill(false);

//     m_world_mins = world_mins;
//     const vec3_t world_size = world_maxs - world_mins;
//     constexpr float epsilon = 1.0f;
//     m_cell_size = std::max(std::max(world_size.x, world_size.y), epsilon) / static_cast<float>(GRID_DIMENSION);

//     if (m_cell_size <= 0.0f) {
//         m_is_built = false;
//         if (developer->integer) gi.Com_PrintFmt("ProximityGrid Build FAILED: Invalid cell size.\n");
//         return;
//     }
//     m_inv_cell_size = 1.0f / m_cell_size;
//     m_is_built = true;

//     // --- STEP 1: RAW GEOMETRY SCAN (same as before) ---
//     // ... (The iterative trace logic is perfect, no changes needed here) ...
//     if (developer->integer >= 2) gi.Com_PrintFmt("ProximityGrid: Performing iterative geometry scan...\n");
//     int raw_walkable_count = 0;
//     for (int y = 0; y < GRID_DIMENSION; ++y) {
//         for (int x = 0; x < GRID_DIMENSION; ++x) {
//             const int cell_idx = y * GRID_DIMENSION + x;
//             std::set<float> floor_z_values;
//             for (int sub_y = 0; sub_y < 3; ++sub_y) {
//                 for (int sub_x = 0; sub_x < 3; ++sub_x) {
//                     vec3_t sample_xy = { m_world_mins.x + (x + 0.25f + (sub_x * 0.25f)) * m_cell_size, m_world_mins.y + (y + 0.25f + (sub_y * 0.25f)) * m_cell_size, 0 };
//                     float current_z = world_maxs.z;
//                     while (current_z > world_mins.z) {
//                         vec3_t trace_start = sample_xy; trace_start.z = current_z;
//                         vec3_t trace_end = sample_xy; trace_end.z = world_mins.z;
//                         trace_t trace = gi.trace(trace_start, vec3_origin, vec3_origin, trace_end, nullptr, MASK_SOLID);
//                         if (trace.fraction >= 1.0f) break;

//                                                 if (trace.plane.normal.z > 0.7f) {
//                             // Usamos nuestra nueva función para comprobar el punto de impacto.
//                             if (GetWaterLevelForPosition(trace.endpos) < WATER_WAIST) {
//                                 floor_z_values.insert(trace.endpos.z);
//                             }
//                         }

//                         current_z = trace.endpos.z - 1.0f;
//                     }
//                 }
//             }
//             if (!floor_z_values.empty()) {
//                 m_is_cell_walkable[cell_idx] = true;
//                 m_cell_ground_z[cell_idx] = *floor_z_values.begin();
//                 raw_walkable_count++;
//             }
//         }
//     }
//     if (developer->integer >= 2) gi.Com_PrintFmt("ProximityGrid Raw Scan: Found {} potential walkable cells.\n", raw_walkable_count);


//     // --- STEP 2: GATHER AND VALIDATE SEED POINTS ---
//     std::vector<vec3_t> seed_points;
//     // Max height an entity can be above the grid's floor to be a valid seed.
//     // Generous enough for items, strict enough to reject flying path_corners.
//     constexpr float MAX_SEED_HEIGHT_ABOVE_GROUND = 96.0f; 
//     int potential_seed_count = 0;

//     for (int i = 1; i < globals.num_edicts; ++i) {
//         edict_t* ent = &g_edicts[i];
//         if (!ent->inuse || !ent->classname) continue;

//         if (!Q_strncasecmp(ent->classname, "info_player", 11) ||
//             !Q_strncasecmp(ent->classname, "item_", 5) ||
//             !Q_strncasecmp(ent->classname, "weapon_", 7) ||
//             !Q_strncasecmp(ent->classname, "ammo_", 5) ||
//             !Q_strncasecmp(ent->classname, "misc_teleporter", 15) ||
//             !Q_strncasecmp(ent->classname, "path_corner", 11))
//         {
//             potential_seed_count++;
            
//             // --- SEED VALIDATION ---
//             int seed_idx = GetCellIndex(ent->s.origin);

//             // 1. Is the seed in a cell that has a floor at all?
//             if (seed_idx == -1 || !m_is_cell_walkable[seed_idx]) {
//                 continue; // Discard seed: it's over a void.
//             }

//             // 2. Is the seed's height close to the floor we found in its cell?
//             const float ground_z = m_cell_ground_z[seed_idx];
//             if (fabs(ent->s.origin.z - ground_z) > MAX_SEED_HEIGHT_ABOVE_GROUND) {
//                 continue; // Discard seed: it's too high above the ground (e.g., for a flying monster).
//             }

//             // If we get here, the seed is valid.
//             seed_points.push_back(ent->s.origin);
//         }
//     }
//     if (developer->integer >= 2) {
//         gi.Com_PrintFmt("ProximityGrid: Found {} potential seeds, validated {}.\n", potential_seed_count, seed_points.size());
//     }


//     // --- STEP 3: 3D-AWARE FLOOD FILL VERIFICATION (same as before) ---
//     // ... (The 3D-aware flood fill is perfect, no changes needed here) ...
//     std::vector<int> queue;
//     queue.reserve(CELL_COUNT);
//     constexpr float MAX_STEP_HEIGHT = 48.0f;
//     for (const auto& point : seed_points) {
//         int seed_idx = GetCellIndex(point);
//         if (seed_idx != -1 && m_is_cell_walkable[seed_idx] && !m_is_cell_verified[seed_idx]) {
//             queue.clear();
//             queue.push_back(seed_idx);
//             m_is_cell_verified[seed_idx] = true;
//             size_t head = 0;
//             while (head < queue.size()) {
//                 int current_idx = queue[head++];
//                 const float current_z = m_cell_ground_z[current_idx];
//                 int current_x = current_idx % GRID_DIMENSION;
//                 int current_y = current_idx / GRID_DIMENSION;
//                 const int neighbors[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
//                 for (const auto& offset : neighbors) {
//                     int neighbor_x = current_x + offset[0];
//                     int neighbor_y = current_y + offset[1];
//                     if (neighbor_x >= 0 && neighbor_x < GRID_DIMENSION && neighbor_y >= 0 && neighbor_y < GRID_DIMENSION) {
//                         int neighbor_idx = neighbor_y * GRID_DIMENSION + neighbor_x;
//                         if (m_is_cell_walkable[neighbor_idx] && !m_is_cell_verified[neighbor_idx]) {
//                             const float neighbor_z = m_cell_ground_z[neighbor_idx];
//                             if (fabs(current_z - neighbor_z) < MAX_STEP_HEIGHT) {
//                                 m_is_cell_verified[neighbor_idx] = true;
//                                 queue.push_back(neighbor_idx);
//                             }
//                         }
//                     }
//                 }
//             }
//         }
//     }

//     // --- STEP 4: FINALIZE THE GRID (same as before) ---
//     // ... (This logic is perfect, no changes needed) ...
//     int final_walkable_count = 0;
//     for (int i = 0; i < CELL_COUNT; ++i) {
//         if (m_is_cell_walkable[i] && !m_is_cell_verified[i]) {
//             m_is_cell_walkable[i] = false;
//         }
//         if (m_is_cell_walkable[i]) {
//             final_walkable_count++;
//         }
//     }
//     if (developer->integer >= 2) {
//         gi.Com_PrintFmt("ProximityGrid Geometry Check: Found {} final walkable cells out of {}.\n", final_walkable_count, CELL_COUNT);
//     }
// }

void ProximityGrid::Build(const vec3_t& world_mins, const vec3_t& world_maxs) {
    // Clear any old data.
    Reset(); 
    m_is_built = false;

    m_world_mins = world_mins;
    const vec3_t world_size = world_maxs - world_mins;
    constexpr float epsilon = 1.0f;

    // Calculate cell size based on the largest world dimension.
    m_cell_size = std::max(std::max(world_size.x, world_size.y), epsilon) / static_cast<float>(GRID_DIMENSION);

    if (m_cell_size <= 0.0f) {
        if (developer->integer) gi.Com_PrintFmt("ProximityGrid Build FAILED: Invalid cell size.\n");
        return;
    }

    m_inv_cell_size = 1.0f / m_cell_size;
    m_is_built = true;

    if (developer->integer >= 2) {
        gi.Com_PrintFmt("ProximityGrid built successfully. Cell size: %.2f\n", m_cell_size);
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