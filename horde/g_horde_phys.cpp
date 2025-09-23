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
   // static const vec3_t monster_maxs = {16, 16, 32};

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
    EntityGrid g_entity_grid;

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
                // FIX: Cast the signed 'int' to an unsigned 'size_t' for array indexing.
                const auto& cell = m_cells[static_cast<size_t>(cell_idx)];

                vec3_t cell_min = { m_world_mins.x + x * m_cell_size, m_world_mins.y + y * m_cell_size, draw_z };
                vec3_t cell_max = cell_min + vec3_t{ m_cell_size, m_cell_size, 1.0f };

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
    int ProximityGrid::GetCellIndex(const vec3_t& pos) const
    {
        if (!m_is_built)
            return -1;
        int grid_x = static_cast<int>((pos.x - m_world_mins.x) * m_inv_cell_size);
        int grid_y = static_cast<int>((pos.y - m_world_mins.y) * m_inv_cell_size);
        grid_x = std::clamp(grid_x, 0, GRID_DIMENSION - 1);
        grid_y = std::clamp(grid_y, 0, GRID_DIMENSION - 1);
        return grid_y * GRID_DIMENSION + grid_x;
    }

    void ProximityGrid::Add(edict_t* ent)
    {
        if (!m_is_built || !ent)
            return;
        int min_idx = GetCellIndex(ent->absmin);
        int max_idx = GetCellIndex(ent->absmax);
        // FIX: Cast the signed 'int' to 'size_t' after checking it's not -1.
        if (min_idx != -1)
            m_cells[static_cast<size_t>(min_idx)].add(ent);
        // FIX: Cast the signed 'int' to 'size_t' after checking it's not -1.
        if (max_idx != -1 && max_idx != min_idx)
            m_cells[static_cast<size_t>(max_idx)].add(ent);
    }

    std::span<edict_t* const> ProximityGrid::GetPotentialColliders(edict_t* ent)
    {
        if (!m_is_built || !ent)
            return {};

        // Use the member buffer for tracking, avoiding heap allocation.
        std::fill(m_visited_entities.begin(), m_visited_entities.end(), false);
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
                const auto& cell = m_cells[static_cast<size_t>(cell_idx)];
                for (size_t i = 0; i < cell.count; ++i)
                {
                    edict_t* other = cell.monsters[i];
                    if (other != ent)
                    {
                        const int entity_num = other->s.number;
                        // Check the boolean array instead of inserting into a set
                        if (!m_visited_entities[entity_num])
                        {
                            m_visited_entities[entity_num] = true; // Mark as visited
                            if (buffer_count < m_query_buffer.size())
                            {
                                m_query_buffer[buffer_count++] = other;
                            }
                            else
                            {
                                if (developer->integer) {
                                    gi.Com_PrintFmt("ProximityGrid WARNING: GetPotentialColliders query buffer is full! (Max {})\n", m_query_buffer.size());
                                }
                                goto end_loops;
                            }
                        }
                    }
                }
            }
        }

    end_loops:
        return { m_query_buffer.data(), buffer_count };
    }

    std::span<edict_t* const> ProximityGrid::QueryRadius(const vec3_t& origin, float radius)
    {
        if (!m_is_built) {
            return {};
        }

        // Use the member buffer for tracking, avoiding heap allocation.
        std::fill(m_visited_entities.begin(), m_visited_entities.end(), false);
        size_t buffer_count = 0;

        const float inv_cs = m_inv_cell_size;
        const vec3_t& mins = m_world_mins;

        int min_x = std::clamp(static_cast<int>((origin.x - radius - mins.x) * inv_cs), 0, GRID_DIMENSION - 1);
        int max_x = std::clamp(static_cast<int>((origin.x + radius - mins.x) * inv_cs), 0, GRID_DIMENSION - 1);
        int min_y = std::clamp(static_cast<int>((origin.y - radius - mins.y) * inv_cs), 0, GRID_DIMENSION - 1);
        int max_y = std::clamp(static_cast<int>((origin.y + radius - mins.y) * inv_cs), 0, GRID_DIMENSION - 1);

        for (int y = min_y; y <= max_y; ++y)
        {
            for (int x = min_x; x <= max_x; ++x)
            {
                const int cell_idx = y * GRID_DIMENSION + x;
                const auto& cell = m_cells[static_cast<size_t>(cell_idx)];

                for (size_t i = 0; i < cell.count; ++i)
                {
                    edict_t* other = cell.monsters[i];

                    const int entity_num = other->s.number;
                    // Check the boolean array instead of inserting into a set
                    if (!m_visited_entities[entity_num])
                    {
                        m_visited_entities[entity_num] = true; // Mark as visited
                        if (buffer_count < m_query_buffer.size())
                        {
                            m_query_buffer[buffer_count++] = other;
                        }
                        else
                        {
                            if (developer->integer) {
                                gi.Com_PrintFmt("ProximityGrid WARNING: QueryRadius query buffer is full! (Max {})\n", m_query_buffer.size());
                            }
                            goto end_loops;
                        }
                    }
                }
            }
        }

    end_loops:
        return { m_query_buffer.data(), buffer_count };
    }


void ProximityGrid::Reset()
{
    // This just clears the dynamic monster data from each cell,
    // leaving the m_is_cell_walkable data intact.
    for (auto& cell : m_cells)
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

    // ============================================================================
    // EntityGrid Implementation
    // ============================================================================

    uint32_t EntityGrid::GetEntityType(edict_t* ent) const {
        if (!ent || !ent->classname) return 0;

        uint32_t type = 0;
        const char* classname = ent->classname;

        // Cache common entity types
        if (ent->client) {
            type |= TYPE_PLAYERS;
        }
        else if (ent->svflags & SVF_MONSTER) {
            type |= TYPE_MONSTERS;
        }
        else if (strstr(classname, "item_") || strstr(classname, "weapon_")) {
            type |= TYPE_ITEMS;
        }
        else if (strstr(classname, "rocket") || strstr(classname, "grenade") ||
                 strstr(classname, "bullet") || strstr(classname, "plasma")) {
            type |= TYPE_PROJECTILES;
        }
        else if (strstr(classname, "trigger_") || strstr(classname, "func_")) {
            type |= TYPE_TRIGGERS;
        }

        return type;
    }

    void EntityGrid::AddEntity(edict_t* ent) {
        if (!ent || !ent->inuse) return;

        // Cache entity type
        m_entity_types[ent->s.number] = GetEntityType(ent);

        // Use parent class Add method
        Add(ent);
    }

    void EntityGrid::UpdateEntity(edict_t* ent) {
        if (!ent || !ent->inuse) return;

        // For now, rebuild the grid (could be optimized with delta updates)
        // This is still much faster than findradius for most cases
        Reset();

        // Re-add all entities (in a real implementation, you'd track moving entities)
        for (int i = 1; i < globals.num_edicts; i++) {
            edict_t* e = &g_edicts[i];
            if (e->inuse) {
                AddEntity(e);
            }
        }
    }

    std::span<edict_t* const> EntityGrid::QueryRadiusFiltered(const vec3_t& origin, float radius, uint32_t type_mask) {
        if (!IsBuilt()) {
            return {};
        }

        // Get all entities in radius using parent method
        auto all_entities = QueryRadius(origin, radius);

        // Filter by type if mask is specified
        if (type_mask == TYPE_ALL) {
            return all_entities;
        }

        // Use a static buffer for filtered results to avoid allocations
        static std::array<edict_t*, 512> filtered_buffer;
        size_t filtered_count = 0;

        for (edict_t* ent : all_entities) {
            if (filtered_count >= filtered_buffer.size()) break;

            uint32_t ent_type = m_entity_types[ent->s.number];
            if (ent_type & type_mask) {
                filtered_buffer[filtered_count++] = ent;
            }
        }

        return { filtered_buffer.data(), filtered_count };
    }

} // namespace HordePhys