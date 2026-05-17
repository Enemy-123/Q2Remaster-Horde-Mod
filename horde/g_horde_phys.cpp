#include "g_horde_phys.h"
#include "horde_constants.h"  // For HordeConstants
#include "../g_local.h"
#include "../memory_safety.h" // For FileGuard
#include <algorithm> // For std::min/max
#include <filesystem> // For path operations
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h> // For GetModuleFileName, MAX_PATH
    #include <direct.h> // For _mkdir on Windows
#else
    #include <sys/stat.h> // For mkdir on Unix
#endif

namespace HordePhys
{
    // Helper function to get DLL directory path
    static bool GetDLLDirectory(std::filesystem::path& out_path) {
        #ifdef _WIN32
            std::array<char, MAX_PATH> modulePath{};

            HMODULE hModule = nullptr;
            if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&GetDLLDirectory), // Use this function's address
                &hModule)) {
                return false;
            }

            if (GetModuleFileNameA(hModule, modulePath.data(), MAX_PATH) == 0) {
                return false;
            }

            out_path = std::filesystem::path(modulePath.data()).parent_path();
            return true;
        #else
            return false; // Not implemented for non-Windows
        #endif
    }

    ProximityGrid g_monster_grid;
    EntityGrid g_entity_grid;
    SpawnGrid g_spawn_grid;

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
                // Green = empty, Yellow = some, Red = very dense.
                rgba_t color = rgba_green;
                size_t cell_count = cell.count();
                if (cell_count > 0) {
                    color = rgba_yellow;
                }
                if (cell_count >= 64) { // Red for very dense cells
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

        // Initialize Query ID system
        m_current_query_id = 0;
        std::fill(m_last_query_ids.begin(), m_last_query_ids.end(), 0);

        if (developer->integer >= 2) {
            gi.Com_PrintFmt("ProximityGrid built successfully. Cell size: {:.2f}\n", m_cell_size);
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

        // Skip non-solid entities (gibs, debris, etc.) - they don't need spatial queries
        if (ent->solid == SOLID_NOT || ent->solid == SOLID_TRIGGER)
            return;

        // Clear previous grid cell tracking
        ent->grid_cell_count = 0;

        const int min_idx = GetCellIndex(ent->absmin);
        const int max_idx = GetCellIndex(ent->absmax);

        // Add to min cell and track it
        if (min_idx != -1) {
            m_cells[static_cast<size_t>(min_idx)].add(ent);
            ent->grid_cells[ent->grid_cell_count++] = static_cast<int8_t>(min_idx);
        }

        // Add to max cell if different and track it
        if (max_idx != -1 && max_idx != min_idx) {
            m_cells[static_cast<size_t>(max_idx)].add(ent);
            ent->grid_cells[ent->grid_cell_count++] = static_cast<int8_t>(max_idx);
        }
    }

    void ProximityGrid::Remove(edict_t* ent)
    {
        if (!m_is_built || !ent)
            return;

        // Optimized: Use tracked cell indices instead of scanning all 256 cells
        // This reduces Remove from O(256*128) to O(k) where k is typically 1-4 cells
        for (uint8_t i = 0; i < ent->grid_cell_count; ++i)
        {
            int8_t cell_idx = ent->grid_cells[i];
            if (cell_idx < 0 || cell_idx >= CELL_COUNT)
                continue;

            auto& cell = m_cells[static_cast<size_t>(cell_idx)];
            auto& monsters = cell.monsters;

            // Find and remove the entity from this cell
            for (size_t j = 0; j < monsters.size(); ++j)
            {
                if (monsters[j] == ent)
                {
                    // Swap with last element and pop (faster than erase)
                    monsters[j] = monsters.back();
                    monsters.pop_back();
                    break; // Entity appears only once per cell
                }
            }
        }

        // Clear the tracking data
        ent->grid_cell_count = 0;
        std::fill(std::begin(ent->grid_cells), std::end(ent->grid_cells), static_cast<int8_t>(-1));
    }

    // Helper method to query a range of cells with a filter function
    template<typename FilterFunc>
    std::span<edict_t* const> ProximityGrid::QueryCellRange(const int min_x, const int max_x, const int min_y, const int max_y, FilterFunc&& filter)
    {
        // Increment query ID. If it wraps (very rare), reset the array.
        m_current_query_id++;
        if (m_current_query_id == 0)
        {
            std::fill(m_last_query_ids.begin(), m_last_query_ids.end(), 0);
            m_current_query_id = 1;
        }

        size_t buffer_count = 0;

        for (int y = min_y; y <= max_y; ++y)
        {
            for (int x = min_x; x <= max_x; ++x)
            {
                const int cell_idx = y * GRID_DIMENSION + x;
                const auto& cell = m_cells[static_cast<size_t>(cell_idx)];
                const auto& monsters = cell.monsters;

                for (size_t i = 0; i < monsters.size(); ++i)
                {
                    edict_t* other = monsters[i];

                    // Apply filter function
                    if (!filter(other)) {
                        continue;
                    }

                    const int entity_num = other->s.number;

                    // Query ID approach: Check if last query ID matches current
                    if (m_last_query_ids[entity_num] == m_current_query_id) {
                        continue; // Already visited this query
                    }
                    m_last_query_ids[entity_num] = m_current_query_id;

                    if (buffer_count < m_query_buffer.size())
                    {
                        m_query_buffer[buffer_count++] = other;
                    }
                    else
                    {
                        if (developer->integer) {
                            gi.Com_PrintFmt("ProximityGrid WARNING: Query buffer is full! (Max {})\n", m_query_buffer.size());
                        }
                        return { m_query_buffer.data(), buffer_count };
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

        // Use cached entity type if available, otherwise compute and cache it
        if (ent->cached_entity_type == 0) {
            ent->cached_entity_type = GetEntityType(ent);
        }

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

            // Use cached entity type instead of map lookup
            uint32_t ent_type = ent->cached_entity_type;
            if (ent_type & type_mask) {
                m_filtered_buffer[filtered_count++] = ent;
            }
        }

        return { m_filtered_buffer.data(), filtered_count };
    }

    // =======================================================================
    // SpawnGrid Implementation - Ported from Vortex mod
    // =======================================================================

    // Helper: Check if floor beneath position is solid and walkable
    bool SpawnGrid::CheckBottom(const vec3_t& pos, const vec3_t& boxmin, const vec3_t& boxmax) const {
        vec3_t mins, maxs, start, stop;
        trace_t trace;

        mins = pos + boxmin;
        maxs = pos + boxmax;

        // OPTIMIZED: Check if all corners are solid (fast path) - reduced from 8 to 4 units for step-up tolerance
        start[2] = mins[2] - 4.0f;
        for (int x = 0; x <= 1; x++) {
            for (int y = 0; y <= 1; y++) {
                start[0] = x ? maxs[0] : mins[0];
                start[1] = y ? maxs[1] : mins[1];
                if (gi.pointcontents(start) != CONTENTS_SOLID)
                    goto realcheck;
            }
        }
        return true;  // All corners solid, good enough

    realcheck:
        // Check midpoint
        start[2] = mins[2];
        start[0] = stop[0] = (mins[0] + maxs[0]) * 0.5f;
        start[1] = stop[1] = (mins[1] + maxs[1]) * 0.5f;
        stop[2] = start[2] - 36.0f;  // 2*18 units down

        trace = gi.trace(start, vec3_origin, vec3_origin, stop, nullptr, MASK_PLAYERSOLID);
        if (trace.fraction == 1.0f)
            return false;

        float mid = trace.endpos[2];
        float bottom = mid;

        // OPTIMIZED: Corners must be within 24 units of midpoint (was 18) - allows more uneven terrain
        for (int x = 0; x <= 1; x++) {
            for (int y = 0; y <= 1; y++) {
                start[0] = stop[0] = x ? maxs[0] : mins[0];
                start[1] = stop[1] = y ? maxs[1] : mins[1];

                trace = gi.trace(start, vec3_origin, vec3_origin, stop, nullptr, MASK_PLAYERSOLID);

                if (trace.fraction != 1.0f && trace.endpos[2] > bottom)
                    bottom = trace.endpos[2];
                if (trace.fraction == 1.0f || mid - trace.endpos[2] > 24.0f)
                    return false;
            }
        }

        return true;
    }

    // Helper: Check if position is too close to existing grid nodes
    bool SpawnGrid::IsNearbyGridNode(const vec3_t& pos, int current_count, float min_distance) const {
        const float min_dist_sq = min_distance * min_distance;

        for (int i = 0; i < current_count; i++) {
            const float dist_sq = (m_grid_nodes[i] - pos).lengthSquared();
            if (dist_sq < min_dist_sq)
                return true;
        }
        return false;
    }

    // Helper: Validate a potential spawn position
    bool SpawnGrid::ValidateSpawnPosition(const vec3_t& pos, const vec3_t& mins, const vec3_t& maxs) const {
        // Check point contents
        const contents_t cont = gi.pointcontents(pos);
        if (cont & MASK_OPAQUE)
            return false;

        // Check for hazards
        if (cont & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WINDOW))
            return false;

        // Check if bbox fits (includes checking for clip brushes)
        const trace_t tr = gi.trace(pos, mins, maxs, pos, nullptr, MASK_WALK_NAV_SOLID);
        if (tr.startsolid || tr.allsolid || tr.fraction != 1.0f)
            return false;

        // Check if sky is too close above (reject positions near ceiling/sky)
        vec3_t sky_check_start = pos;
        sky_check_start.z += maxs.z - mins.z; // Start from head height
        vec3_t sky_check_end = sky_check_start;
        sky_check_end.z += 128.0f; // Check 128 units up

        trace_t sky_trace = gi.trace(sky_check_start, mins, maxs, sky_check_end, nullptr, MASK_SOLID);
        if (sky_trace.surface && (sky_trace.surface->flags & SURF_SKY))
            return false; // Too close to sky - reject position

        // Check for sufficient open space (reject enclosed boxes/tight spaces)
        // Test 4 horizontal directions at chest height
        const float test_height = (maxs.z - mins.z) * 0.5f;
        constexpr float min_clearance = 96.0f; // Minimum clearance in each direction

        vec3_t test_center = pos;
        test_center.z += test_height;

        // Check forward, back, left, right - static to avoid per-call construction
        static const vec3_t directions[4] = {
            {min_clearance, 0, 0},
            {-min_clearance, 0, 0},
            {0, min_clearance, 0},
            {0, -min_clearance, 0}
        };

        int blocked_directions = 0;
        for (int i = 0; i < 4; i++) {
            vec3_t test_end = test_center + directions[i];
            trace_t space_trace = gi.traceline(test_center, test_end, nullptr, MASK_SOLID);
            if (space_trace.fraction < 0.8f) { // Less than 80% clearance
                blocked_directions++;
            }
        }

        // Reject if 3 or 4 directions are blocked (enclosed space)
        if (blocked_directions >= 3)
            return false;

        // Check ground reachability (reject isolated high positions)
        // Trace down to see if we can reach significantly lower ground
        vec3_t down_start = pos;
        down_start.z -= mins.z; // Start from feet
        vec3_t down_end = down_start;
        down_end.z -= 512.0f; // Check 512 units down

        trace_t down_trace = gi.traceline(down_start, down_end, nullptr, MASK_SOLID);

        // If we're on a high platform (>256 units above lower ground), check for accessibility
        const float height_above_lower = down_trace.fraction * 512.0f;
        if (height_above_lower > 256.0f) {
            // High position - check if there's a ceiling above (trapped on roof)
            vec3_t ceiling_check = pos;
            ceiling_check.z += maxs.z - mins.z;
            vec3_t ceiling_end = ceiling_check;
            ceiling_end.z += 64.0f;

            trace_t ceiling_trace = gi.traceline(ceiling_check, ceiling_end, nullptr, MASK_SOLID);

            // If there's a ceiling close above AND we're very high up, likely an unreachable roof
            if (ceiling_trace.fraction < 0.9f) {
                return false; // Enclosed high position - reject
            }
        }

        return true;
    }

    // Clear the grid
    void SpawnGrid::Clear() {
        m_grid_nodes.clear();
        m_node_count = 0;
        ClearCooldowns();
    }

    // Mark a position as recently used (adds cooldown)
    void SpawnGrid::MarkPositionUsed(const vec3_t& pos) {
        m_cooldown_positions[m_cooldown_write_index] = pos;
        m_cooldown_expiry[m_cooldown_write_index] = level.time + HordeConstants::GRID_POSITION_COOLDOWN;
        m_cooldown_write_index = (m_cooldown_write_index + 1) % MAX_COOLDOWN_POSITIONS;
    }

    // Check if position is too close to a recently used position
    bool SpawnGrid::IsPositionOnCooldown(const vec3_t& pos, float cooldown_radius) const {
        const float radius_sq = cooldown_radius * cooldown_radius;
        const gtime_t current_time = level.time;

        for (size_t i = 0; i < MAX_COOLDOWN_POSITIONS; ++i) {
            if (m_cooldown_expiry[i] > current_time) {
                const float dist_sq = (pos - m_cooldown_positions[i]).lengthSquared();
                if (dist_sq < radius_sq) {
                    return true;
                }
            }
        }
        return false;
    }

    // Clear all cooldowns
    void SpawnGrid::ClearCooldowns() {
        m_cooldown_positions.fill(vec3_origin);
        m_cooldown_expiry.fill(0_sec);
        m_cooldown_write_index = 0;
    }

    // Grid coordinate conversion helpers
    vec3_t SpawnGrid::GridToWorld(int x, int y, int z) const {
        return vec3_t{
            m_world_mins.x + (x + 0.5f) * m_grid_size.x,
            m_world_mins.y + (y + 0.5f) * m_grid_size.y,
            m_world_mins.z + (z + 0.5f) * m_grid_size.z
        };
    }

    void SpawnGrid::WorldToGrid(const vec3_t& world_pos, int& out_x, int& out_y, int& out_z) const {
        out_x = static_cast<int>((world_pos.x - m_world_mins.x) / m_grid_size.x);
        out_y = static_cast<int>((world_pos.y - m_world_mins.y) / m_grid_size.y);
        out_z = static_cast<int>((world_pos.z - m_world_mins.z) / m_grid_size.z);

        out_x = std::clamp(out_x, 0, GRID_DIMENSION - 1);
        out_y = std::clamp(out_y, 0, GRID_DIMENSION - 1);
        out_z = std::clamp(out_z, 0, GRID_DIMENSION - 1);
    }

    // Generate the spawn grid by scanning the entire map
    // Based on Vortex's CreateGrid() function
    bool SpawnGrid::Generate(const vec3_t& world_mins, const vec3_t& world_maxs, bool force_regenerate) {
        if (!force_regenerate && LoadFromDisk(level.mapname)) {
            if (developer->integer > 1)
                gi.Com_PrintFmt("Spawn grid loaded from disk ({} nodes).\n", m_node_count);
            return true;
        }

        Clear();
        // Note: static_vector has fixed capacity - no reserve() needed

        // Store world bounds
        m_world_mins = world_mins;
        m_world_maxs = world_maxs;

        // Calculate grid cell size
        const vec3_t world_span = world_maxs - world_mins;
        m_grid_size = world_span / static_cast<float>(GRID_DIMENSION);

        if (developer->integer > 1) {
            gi.Com_PrintFmt("Spawn grid world bounds: mins={} maxs={}\n", world_mins, world_maxs);
            gi.Com_PrintFmt("Spawn grid cell size: {}\n", m_grid_size);
        }

        // Use TWO different bounding boxes like Vortex:
        // Point trace to find ground, then box for clearance validation
        const vec3_t trace_mins = {0, 0, 0};      // Point trace (no bbox)
        const vec3_t trace_maxs = {0, 0, 0};
        const vec3_t spawn_mins = {-16, -16, 0};  // Spawn box for clearance
        const vec3_t spawn_maxs = {16, 16, 0};
        int generated_count = 0;

        // Debug counters
        int tested_positions = 0;
        int failed_pointcontents = 0, failed_func_entity = 0, failed_hazards = 0;
        int failed_slope = 0, failed_clearance = 0, failed_final_trace = 0;
        int failed_checkbottom = 0, failed_nearby = 0, failed_sky = 0;

        if (developer->integer > 1)
            gi.Com_PrintFmt("Generating spawn grid for map {}...\n", level.mapname);

        // --- SPATIAL HASH FOR O(1) NEARBY LOOKUPS ---
        // Use 64-unit cells (2x min_distance of 32) for efficient nearby checks
        constexpr float HASH_CELL_SIZE = 64.0f;
        constexpr float INV_HASH_CELL_SIZE = 1.0f / HASH_CELL_SIZE;
        constexpr int HASH_TABLE_SIZE = 4096; // Power of 2 for fast modulo
        
        // Hash table: each bucket contains indices into m_grid_nodes
        std::array<boost::container::small_vector<int, 8>, HASH_TABLE_SIZE> spatial_hash;
        
        auto hash_pos = [&](const vec3_t& pos) -> int {
            const int hx = static_cast<int>((pos.x - world_mins.x) * INV_HASH_CELL_SIZE);
            const int hy = static_cast<int>((pos.y - world_mins.y) * INV_HASH_CELL_SIZE);
            // Simple hash combining x and y
            return ((hx * 73856093) ^ (hy * 19349663)) & (HASH_TABLE_SIZE - 1);
        };
        
        auto is_nearby_fast = [&](const vec3_t& pos, float min_distance) -> bool {
            const float min_dist_sq = min_distance * min_distance;
            const int hx = static_cast<int>((pos.x - world_mins.x) * INV_HASH_CELL_SIZE);
            const int hy = static_cast<int>((pos.y - world_mins.y) * INV_HASH_CELL_SIZE);
            
            // Check 3x3 neighborhood of cells
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const int cell_hash = (((hx + dx) * 73856093) ^ ((hy + dy) * 19349663)) & (HASH_TABLE_SIZE - 1);
                    for (int idx : spatial_hash[cell_hash]) {
                        const float dist_sq = (m_grid_nodes[idx] - pos).lengthSquared();
                        if (dist_sq < min_dist_sq)
                            return true;
                    }
                }
            }
            return false;
        };
        // --- END SPATIAL HASH ---

        // Scan the entire map in a 3D grid pattern
        for (int x = 0; x < GRID_DIMENSION; x++) {
            for (int y = 0; y < GRID_DIMENSION; y++) {
                for (int z = GRID_DIMENSION - 1; z >= 0; z--) {
                    vec3_t test_pos = GridToWorld(x, y, z);
                    tested_positions++;

                    // Skip if in solid/lava/slime/window/ladder/clips
                    if (gi.pointcontents(test_pos) & (MASK_OPAQUE | CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP)) {
                        failed_pointcontents++;
                        z--;
                        continue;
                    }

                    // Trace down to find ground using point trace
                    vec3_t endpt = test_pos;
                    endpt[2] = m_world_mins.z - 512.0f;  // Trace below world bounds

                    trace_t tr1 = gi.trace(test_pos, trace_mins, trace_maxs, endpt, nullptr, MASK_WALK_NAV_SOLID);

                    // Set z to ground level for next iteration
                    int gx, gy, gz;
                    WorldToGrid(tr1.endpos, gx, gy, gz);
                    z = gz;

                    // Skip if hit func entity (doors, platforms, etc) - exclude world entity
                    if (tr1.ent && tr1.ent != &g_edicts[0] && tr1.ent->movetype != MOVETYPE_NONE) {
                        failed_func_entity++;
                        continue;
                    }

                    // Skip hazards
                    if (tr1.contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WINDOW)) {
                        failed_hazards++;
                        continue;
                    }

                    // REDUCED: Skip VERY non-walkable slopes (was 0.7, now 0.5 = ~60 degrees)
                    if (tr1.plane.normal[2] < 0.5f) {
                        failed_slope++;
                        continue;
                    }

                    // REDUCED: Test vertical clearance (was 32 units, now 24 for crouched monster)
                    vec3_t clearance_start = tr1.endpos;
                    vec3_t clearance_end = clearance_start;
                    clearance_end[2] += 24.0f;  // Reduced from 32

                    trace_t tr2 = gi.trace(clearance_end, spawn_mins, spawn_maxs, clearance_start, nullptr, MASK_WALK_NAV_SOLID);

                    // Skip if not enough clearance
                    if (tr2.startsolid || tr2.allsolid) {
                        failed_clearance++;
                        continue;
                    }

                    // Final position check
                    vec3_t final_pos = tr2.endpos;
                    final_pos[2] += 24.0f;  // Reduced from 32

                    trace_t tr3 = gi.trace(final_pos, spawn_mins, spawn_maxs, final_pos, nullptr, MASK_WALK_NAV_SOLID);
                    if (tr3.fraction != 1.0f || tr3.startsolid || tr3.allsolid) {
                        failed_final_trace++;
                        continue;
                    }

                    // Check floor is solid
                    if (!CheckBottom(final_pos, spawn_mins, spawn_maxs)) {
                        failed_checkbottom++;
                        continue;
                    }

                    // Check if sky is too close above using bbox trace (reject roof spawns)
                    vec3_t sky_check_start = final_pos;
                    sky_check_start.z += spawn_maxs.z - spawn_mins.z; // Start from top of bbox
                    vec3_t sky_check_end = sky_check_start;
                    sky_check_end.z += 128.0f; // Check 128 units up

                    trace_t sky_trace = gi.trace(sky_check_start, spawn_mins, spawn_maxs, sky_check_end, nullptr, MASK_SOLID);
                    if (sky_trace.surface && (sky_trace.surface->flags & SURF_SKY)) {
                        failed_sky++;
                        continue; // Too close to sky - reject position
                    }

                    // OPTIMIZED: Use spatial hash for O(1) nearby check instead of O(n) scan
                    if (is_nearby_fast(final_pos, 32.0f)) {
                        failed_nearby++;
                        continue;
                    }

                    // Valid spawn position found!
                    const int new_idx = static_cast<int>(m_grid_nodes.size());
                    m_grid_nodes.push_back(final_pos);
                    
                    // Add to spatial hash for future nearby checks
                    spatial_hash[hash_pos(final_pos)].push_back(new_idx);
                    
                    generated_count++;

                    // Progress indicator
                    if (developer->integer > 1 && generated_count % 500 == 0) {
                        gi.Com_PrintFmt("  ... {} nodes found\n", generated_count);
                    }

                    if (generated_count >= MAX_GRID_NODES) {
                        if (developer->integer > 1)
                            gi.Com_PrintFmt("  ... reached MAX_GRID_NODES limit\n");
                        goto done;
                    }
                }
            }
        }

    done:
        m_node_count = generated_count;

        // DEBUG: Print validation failure statistics
        if (developer->integer > 1) {
            gi.Com_PrintFmt("Grid generation stats:\n");
            gi.Com_PrintFmt("  Tested positions: {}\n", tested_positions);
            gi.Com_PrintFmt("  Failed pointcontents: {}\n", failed_pointcontents);
            gi.Com_PrintFmt("  Failed func_entity: {}\n", failed_func_entity);
            gi.Com_PrintFmt("  Failed hazards: {}\n", failed_hazards);
            gi.Com_PrintFmt("  Failed slope: {}\n", failed_slope);
            gi.Com_PrintFmt("  Failed clearance: {}\n", failed_clearance);
            gi.Com_PrintFmt("  Failed final_trace: {}\n", failed_final_trace);
            gi.Com_PrintFmt("  Failed checkbottom: {}\n", failed_checkbottom);
            gi.Com_PrintFmt("  Failed sky: {}\n", failed_sky);
            gi.Com_PrintFmt("  Failed nearby: {}\n", failed_nearby);
            gi.Com_PrintFmt("Spawn grid generation complete: {} valid nodes.\n", m_node_count);
        }

        if (m_node_count > 0) {
            SaveToDisk(level.mapname);
        }

        return m_node_count > 0;
    }

    // Get a random position from the grid
    bool SpawnGrid::GetRandomPosition(vec3_t& out_pos) const {
        if (m_node_count < 1)
            return false;

        const int index = irandom(m_node_count);
        out_pos = m_grid_nodes[index];
        return true;
    }

    // Get a random position near a point
    bool SpawnGrid::GetRandomPositionNear(const vec3_t& center, float min_dist, float max_dist, vec3_t& out_pos) const {
        if (m_node_count < 1)
            return false;

        const float min_dist_sq = min_dist * min_dist;
        const float max_dist_sq = max_dist * max_dist;

        // Try up to 100 random positions
        for (int attempt = 0; attempt < 100; attempt++) {
            const int index = irandom(m_node_count);
            const vec3_t& candidate = m_grid_nodes[index];

            const float dist_sq = (candidate - center).lengthSquared();

            if (dist_sq >= min_dist_sq && dist_sq <= max_dist_sq) {
                out_pos = candidate;
                return true;
            }
        }

        // Fallback: return any random position
        return GetRandomPosition(out_pos);
    }

    // Check if a spawn position is in the Potentially Visible Set (PVS) of any active player
    // Uses gi.inPVS which checks if the position could be visible from any angle
    // This is broader than direct line-of-sight - catches positions that could be seen by turning around
    bool SpawnGrid::IsVisibleToPlayers(const vec3_t& pos) const {
        // Iterate through all clients
        for (int i = 1; i <= maxclients->integer; i++) {
            edict_t* player = &g_edicts[i];

            // Skip invalid/dead players
            if (!player->inuse || player->health <= 0)
                continue;

            // PVS check - returns true if pos could be visible from player's area from any angle
            // The 'false' parameter means don't check through portals (stricter visibility check)
            if (gi.inPVS(pos, player->s.origin, false))
                return true;  // Position is in PVS of this player
        }

        return false;  // Not in PVS of any player
    }

    // Get a tactical spawn position (distance + visibility + cooldown checks)
    // Features are ENABLED BY DEFAULT:
    // - Cooldowns: Always applied to prevent repeated spawns in same area
    // - Distance checks: Always applied (min distance from players)
    // - Visibility checks: Applied when prefer_out_of_visibility=true (25% chance from caller)
    // The g_horde_tactical_spawn cvar can ENHANCE behavior (mode 2 = always check visibility)
    bool SpawnGrid::GetTacticalSpawnPosition(vec3_t& out_pos, float min_dist_from_players, int max_attempts, bool prefer_out_of_visibility) const {
        if (m_node_count < 1)
            return false;

        // Get tactical spawn mode from cvar (can enhance default behavior)
        const int tactical_mode = g_horde_tactical_spawn->integer;
        const float min_dist_sq = min_dist_from_players * min_dist_from_players;

        // Determine what checks to apply:
        // - Cooldowns: ALWAYS applied (default behavior)
        // - Distance checks: ALWAYS applied (default behavior)
        // - Visibility checks: Applied when prefer_out_of_visibility=true OR tactical_mode >= 2
        const bool require_out_of_visibility = prefer_out_of_visibility || (tactical_mode >= 2);

        int attempts = 0;
        while (attempts < max_attempts) {
            attempts++;

            // Get random candidate position
            const int idx = irandom(m_node_count);
            const vec3_t& candidate = m_grid_nodes[idx];

            // ALWAYS check cooldowns (default behavior - prevents spawn clustering)
            if (IsPositionOnCooldown(candidate, HordeConstants::GRID_COOLDOWN_RADIUS)) {
                continue;
            }

            // ALWAYS check distance to players (default behavior - gives reaction time)
            bool too_close = false;
            for (int i = 1; i <= maxclients->integer; i++) {
                edict_t* player = &g_edicts[i];

                if (!player->inuse || player->health <= 0)
                    continue;

                const float dist_sq = (player->s.origin - candidate).lengthSquared();

                if (dist_sq < min_dist_sq) {
                    too_close = true;
                    break;
                }
            }

            if (too_close)
                continue;

            // Check visibility if required (25% chance from caller OR tactical_mode >= 2)
            if (require_out_of_visibility) {
                if (IsVisibleToPlayers(candidate)) {
                    continue;  // Position is visible, try another
                }
            }

            // Found valid position
            out_pos = candidate;
            return true;
        }

        // Fallback: couldn't find position meeting all criteria
        // Try one more pass with just cooldown check (skip distance/visibility for emergency)
        if (developer->integer > 1) {
            gi.Com_PrintFmt("GetTacticalSpawnPosition: No optimal position found after {} attempts, trying relaxed fallback\n", max_attempts);
        }

        for (int i = 0; i < max_attempts / 2; i++) {
            const int idx = irandom(m_node_count);
            const vec3_t& candidate = m_grid_nodes[idx];

            // Only check cooldown in fallback
            if (!IsPositionOnCooldown(candidate, HordeConstants::GRID_COOLDOWN_RADIUS)) {
                out_pos = candidate;
                return true;
            }
        }

        // Final fallback: pure random (no cooldown check)
        return GetRandomPosition(out_pos);
    }

    // Check if a position is within the playable map boundaries
    bool SpawnGrid::IsPositionInBounds(const vec3_t& pos, float tolerance) const noexcept {
        if (!IsGenerated())
            return true;  // If no grid generated, assume position is valid

        // Check if position is within world bounds (with tolerance for edge cases)
        return (pos.x >= m_world_mins.x - tolerance && pos.x <= m_world_maxs.x + tolerance &&
                pos.y >= m_world_mins.y - tolerance && pos.y <= m_world_maxs.y + tolerance &&
                pos.z >= m_world_mins.z - tolerance && pos.z <= m_world_maxs.z + tolerance);
    }

    // Save grid to disk
    bool SpawnGrid::SaveToDisk(const char* mapname) const {
        namespace fs = std::filesystem;

        if (m_node_count < 1)
            return false;

        try {
            // Get DLL directory
            fs::path dll_dir;
            if (!GetDLLDirectory(dll_dir)) {
                gi.Com_PrintFmt("Failed to get DLL directory for spawn grid save\n");
                return false;
            }

            // Extract basename from mapname (e.g., "q64/dm3" -> "dm3")
            std::string_view map_basename = mapname;
            if (size_t last_slash = map_basename.find_last_of("/\\"); last_slash != std::string_view::npos) {
                map_basename = map_basename.substr(last_slash + 1);
            }

            // Build path: {DLL_DIR}/maps/grd/{mapname}.grd
            fs::path grd_dir = dll_dir / "maps" / "grd";
            fs::path grid_file = grd_dir / fmt::format("{}.grd", map_basename);

            // Create directories if needed
            fs::create_directories(grd_dir);

            FILE* fp = fopen(grid_file.string().c_str(), "wb");
            if (!fp) {
                gi.Com_PrintFmt("Failed to save spawn grid to {}\n", grid_file.string());
                return false;
            }
            FileGuard guard(fp);  // RAII: auto-closes on scope exit or exception

            // Write header
            const int32_t version = 1;
            fwrite(&version, sizeof(int32_t), 1, fp);
            fwrite(&m_node_count, sizeof(int32_t), 1, fp);

            // Write node positions
            fwrite(m_grid_nodes.data(), sizeof(vec3_t), m_node_count, fp);

            if (developer->integer > 1)
                gi.Com_PrintFmt("Spawn grid saved to {}\n", grid_file.string());
            return true;

        } catch (const std::exception& e) {
            gi.Com_PrintFmt("Exception saving spawn grid: {}\n", e.what());
            return false;
        }
    }

    // Load grid from disk
    bool SpawnGrid::LoadFromDisk(const char* mapname) {
        namespace fs = std::filesystem;

        try {
            // Get DLL directory
            fs::path dll_dir;
            if (!GetDLLDirectory(dll_dir)) {
                return false; // Silent fail for load
            }

            // Extract basename from mapname (e.g., "q64/dm3" -> "dm3")
            std::string_view map_basename = mapname;
            if (size_t last_slash = map_basename.find_last_of("/\\"); last_slash != std::string_view::npos) {
                map_basename = map_basename.substr(last_slash + 1);
            }

            // Build path: {DLL_DIR}/maps/grd/{mapname}.grd
            fs::path grid_file = dll_dir / "maps" / "grd" / fmt::format("{}.grd", map_basename);

            FILE* fp = fopen(grid_file.string().c_str(), "rb");
            if (!fp)
                return false;
            FileGuard guard(fp);  // RAII: auto-closes on scope exit or exception

            // Read header
            int32_t version = 0;
            int32_t node_count = 0;
            fread(&version, sizeof(int32_t), 1, fp);
            fread(&node_count, sizeof(int32_t), 1, fp);

            if (version != 1 || node_count < 1 || node_count > MAX_GRID_NODES) {
                gi.Com_PrintFmt("Invalid spawn grid file: {}\n", grid_file.string());
                return false;
            }

            // Read nodes
            Clear();
            m_grid_nodes.resize(node_count);
            fread(m_grid_nodes.data(), sizeof(vec3_t), node_count, fp);
            m_node_count = node_count;

            return true;

        } catch (const std::exception&) {
            return false; // Silent fail for load
        }
    }

} // namespace HordePhys
