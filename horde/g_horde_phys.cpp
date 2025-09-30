#include "g_horde_phys.h"
#include "../g_local.h"
#include <algorithm> // For std::min/max
#include <unordered_map>

namespace HordePhys
{

    ProximityGrid g_monster_grid;
    EntityGrid g_entity_grid;

    // Gets the water level for a raw position, simulating a standard monster's bounding box.
    // This is essential for checking potential spawn points before a monster exists there.
    water_level_t GetWaterLevelForPosition(const vec3_t& in_point)
    {
        // Use a standard medium-sized monster bounding box for the check.
        static constexpr vec3_t monster_mins = {-16, -16, -24};

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
        const int min_idx = GetCellIndex(ent->absmin);
        const int max_idx = GetCellIndex(ent->absmax);
        // FIX: Cast the signed 'int' to 'size_t' after checking it's not -1.
        if (min_idx != -1)
            m_cells[static_cast<size_t>(min_idx)].add(ent);
        // FIX: Cast the signed 'int' to 'size_t' after checking it's not -1.
        if (max_idx != -1 && max_idx != min_idx)
            m_cells[static_cast<size_t>(max_idx)].add(ent);
    }

    void ProximityGrid::Remove(edict_t* ent)
    {
        if (!m_is_built || !ent)
            return;

        // Iterate through all cells and remove the entity
        // This is O(n) but better than rebuilding the entire grid
        for (auto& cell : m_cells)
        {
            for (size_t i = 0; i < cell.count; ++i)
            {
                if (cell.monsters[i] == ent)
                {
                    // Remove by swapping with last element
                    cell.monsters[i] = cell.monsters[cell.count - 1];
                    --cell.count;
                    // Don't break - entity might be in multiple cells
                    // But we need to check this index again since we swapped
                    --i;
                }
            }
        }
    }

    // Helper method to query a range of cells with a filter function
    template<typename FilterFunc>
    std::span<edict_t* const> ProximityGrid::QueryCellRange(const int min_x, const int max_x, const int min_y, const int max_y, FilterFunc&& filter)
    {
        std::fill(m_visited_entities.begin(), m_visited_entities.end(), false);
        size_t buffer_count = 0;

        bool buffer_full = false;
        for (int y = min_y; y <= max_y && !buffer_full; ++y)
        {
            for (int x = min_x; x <= max_x && !buffer_full; ++x)
            {
                const int cell_idx = y * GRID_DIMENSION + x;
                const auto& cell = m_cells[static_cast<size_t>(cell_idx)];

                for (size_t i = 0; i < cell.count && !buffer_full; ++i)
                {
                    edict_t* other = cell.monsters[i];

                    // Apply filter function
                    if (!filter(other)) {
                        continue;
                    }

                    const int entity_num = other->s.number;
                    if (!m_visited_entities[entity_num])
                    {
                        m_visited_entities[entity_num] = true;
                        if (buffer_count < m_query_buffer.size())
                        {
                            m_query_buffer[buffer_count++] = other;
                        }
                        else
                        {
                            if (developer->integer) {
                                gi.Com_PrintFmt("ProximityGrid WARNING: Query buffer is full! (Max {})\n", m_query_buffer.size());
                            }
                            buffer_full = true;
                        }
                    }
                }
            }
        }

        return { m_query_buffer.data(), buffer_count };
    }

    std::span<edict_t* const> ProximityGrid::GetPotentialColliders(edict_t* ent)
    {
        if (!m_is_built || !ent)
            return {};

        const int min_x = std::clamp(static_cast<int>((ent->absmin.x - m_world_mins.x) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
        const int max_x = std::clamp(static_cast<int>((ent->absmax.x - m_world_mins.x) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
        const int min_y = std::clamp(static_cast<int>((ent->absmin.y - m_world_mins.y) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
        const int max_y = std::clamp(static_cast<int>((ent->absmax.y - m_world_mins.y) * m_inv_cell_size), 0, GRID_DIMENSION - 1);

        // Filter out the querying entity itself
        return QueryCellRange(min_x, max_x, min_y, max_y, [ent](edict_t* other) {
            return other != ent;
        });
    }

    std::span<edict_t* const> ProximityGrid::QueryRadius(const vec3_t& origin, const float radius)
    {
        if (!m_is_built) {
            return {};
        }

        const float inv_cs = m_inv_cell_size;
        const vec3_t& mins = m_world_mins;

        const int min_x = std::clamp(static_cast<int>((origin.x - radius - mins.x) * inv_cs), 0, GRID_DIMENSION - 1);
        const int max_x = std::clamp(static_cast<int>((origin.x + radius - mins.x) * inv_cs), 0, GRID_DIMENSION - 1);
        const int min_y = std::clamp(static_cast<int>((origin.y - radius - mins.y) * inv_cs), 0, GRID_DIMENSION - 1);
        const int max_y = std::clamp(static_cast<int>((origin.y + radius - mins.y) * inv_cs), 0, GRID_DIMENSION - 1);

        // Filter by actual distance to return circle, not just bounding square
        const float radius_sq = radius * radius;
        return QueryCellRange(min_x, max_x, min_y, max_y, [&origin, radius_sq](edict_t* ent) {
            // Calculate distance from entity origin to query origin
            const vec3_t delta = ent->s.origin - origin;
            const float dist_sq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
            return dist_sq <= radius_sq;
        });
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

    // ============================================================================
    // EntityGrid Implementation
    // ============================================================================

    uint32_t EntityGrid::GetEntityType(edict_t* ent) const {
        if (!ent || !ent->classname) return 0;

        uint32_t type = 0;
        const char* classname = ent->classname;

        // Fast checks using flags first
        if (ent->client) {
            type |= TYPE_PLAYERS;
        }
        if (ent->svflags & SVF_MONSTER) {
            type |= TYPE_MONSTERS;
        }

        // Optimize string matching using prefix checks (faster than strstr)
        // Check first few characters to avoid full string scans
        const char first_char = classname[0];

        switch (first_char) {
            case 'i': // item_
                if (classname[1] == 't' && classname[2] == 'e' && classname[3] == 'm' && classname[4] == '_') {
                    type |= TYPE_ITEMS;
                }
                break;

            case 'w': // weapon_
                if (classname[1] == 'e' && classname[2] == 'a' && classname[3] == 'p' && classname[4] == 'o' && classname[5] == 'n' && classname[6] == '_') {
                    type |= TYPE_ITEMS;
                }
                break;

            case 't': // trigger_
                if (classname[1] == 'r' && classname[2] == 'i' && classname[3] == 'g' && classname[4] == 'g' && classname[5] == 'e' && classname[6] == 'r' && classname[7] == '_') {
                    type |= TYPE_TRIGGERS;
                }
                break;

            case 'f': // func_
                if (classname[1] == 'u' && classname[2] == 'n' && classname[3] == 'c' && classname[4] == '_') {
                    type |= TYPE_TRIGGERS;
                }
                break;

            case 'r': // rocket
                if (strstr(classname, "rocket")) {
                    type |= TYPE_PROJECTILES;
                }
                break;

            case 'g': // grenade
                if (strstr(classname, "grenade")) {
                    type |= TYPE_PROJECTILES;
                }
                break;

            case 'b': // bullet
                if (strstr(classname, "bullet")) {
                    type |= TYPE_PROJECTILES;
                }
                break;

            case 'p': // plasma
                if (strstr(classname, "plasma")) {
                    type |= TYPE_PROJECTILES;
                }
                break;
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

        // Remove entity from old position and add to new position
        // Much more efficient than rebuilding the entire grid
        Remove(ent);
        AddEntity(ent);
    }

    std::span<edict_t* const> EntityGrid::QueryRadiusFiltered(const vec3_t& origin, const float radius, const uint32_t type_mask) {
        if (!IsBuilt()) {
            return {};
        }

        // Get all entities in radius using parent method
        auto all_entities = QueryRadius(origin, radius);

        // Filter by type if mask is specified
        if (type_mask == TYPE_ALL) {
            return all_entities;
        }

        // Use member buffer for filtered results (thread-safe)
        size_t filtered_count = 0;

        for (edict_t* ent : all_entities) {
            if (filtered_count >= m_filtered_buffer.size()) break;

            auto it = m_entity_types.find(ent->s.number);
            uint32_t ent_type = (it != m_entity_types.end()) ? it->second : 0;
            if (ent_type & type_mask) {
                m_filtered_buffer[filtered_count++] = ent;
            }
        }

        return { m_filtered_buffer.data(), filtered_count };
    }

} // namespace HordePhys