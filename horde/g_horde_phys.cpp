// In: g_horde_phys.cpp

#include "g_horde_phys.h"
#include "../g_local.h"
#include <algorithm>    // For std::clamp
#include <string>
#include <vector>
#define WIN32_LEAN_AND_MEAN // Also good practice
#include <windows.h>
#include <filesystem>
#include <fstream>

namespace HordePhys {

    // The single global instance of our grid.
    ProximityGrid g_monster_grid;

    //======================================================================
    // --- CACHING HELPER FUNCTIONS ---
    //======================================================================

    // Helper to get the path to our game's module (the DLL)
    // Returns an empty path on failure.
    std::filesystem::path GetModulePath() {
        std::array<char, MAX_PATH> modulePath{};
        HMODULE hModule = nullptr;

        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&GetModulePath), &hModule)) {
            gi.Com_PrintFmt("ProximityGrid Cache: Error obtaining module handle.\n");
            return {};
        }

        if (DWORD result = GetModuleFileNameA(hModule, modulePath.data(), MAX_PATH); result == 0 || result == MAX_PATH) {
            gi.Com_PrintFmt("ProximityGrid Cache: Error obtaining module path.\n");
            return {};
        }
        return {modulePath.data()};
    }

    // Helper to get the full path for a grid cache file.
    // Example: C:\MyGame\MyMod\maps\grd\q2dm1.grd
    std::filesystem::path GetGridCachePath(std::string_view mapname) {
        auto modulePath = GetModulePath();
        if (modulePath.empty()) {
            return {};
        }
        return modulePath.parent_path() / "maps" / "grd" / fmt::format("{}.grd", mapname);
    }

    // Saves the walkable cell data to a cache file.
    bool SaveGridToFile(std::string_view mapname, const std::array<bool, ProximityGrid::CELL_COUNT>& walkable_data) {
        namespace fs = std::filesystem;

        auto cache_path = GetGridCachePath(mapname);
        if (cache_path.empty()) {
            return false; // Error already printed
        }

        try {
            // Ensure the "maps/grd" directory exists.
            fs::create_directories(cache_path.parent_path());

            FILE* fp = fopen(cache_path.string().c_str(), "wb");
            if (!fp) {
                gi.Com_PrintFmt("ProximityGrid Cache: Failed to open for writing: {}\n", cache_path.string());
                return false;
            }

            struct FileGuard {
                FILE* fp;
                ~FileGuard() { if (fp) fclose(fp); }
            } guard(fp);

            // Convert bool array to a compact byte array for consistent file size.
            std::array<uint8_t, ProximityGrid::CELL_COUNT> buffer;
            for (size_t i = 0; i < ProximityGrid::CELL_COUNT; ++i) {
                buffer[i] = walkable_data[i] ? 1 : 0;
            }

            if (fwrite(buffer.data(), sizeof(uint8_t), ProximityGrid::CELL_COUNT, fp) != ProximityGrid::CELL_COUNT) {
                gi.Com_PrintFmt("ProximityGrid Cache: Error writing to file: {}\n", cache_path.string());
                return false;
            }

            gi.Com_PrintFmt("ProximityGrid Cache: Successfully saved to {}.\n", cache_path.string());
            return true;

        } catch (const fs::filesystem_error& e) {
            gi.Com_PrintFmt("ProximityGrid Cache: Filesystem error: {}\n", e.what());
            return false;
        }
    }

    // Loads the walkable cell data from a cache file.
    // Returns true on success, false if the file doesn't exist or is invalid.
    bool LoadGridFromFile(std::string_view mapname, std::array<bool, ProximityGrid::CELL_COUNT>& out_walkable_data) {
        auto cache_path = GetGridCachePath(mapname);
        if (cache_path.empty()) {
            return false; // Error already printed
        }

        FILE* fp = fopen(cache_path.string().c_str(), "rb");
        if (!fp) {
            // This is not an error, it just means the cache doesn't exist yet.
            return false;
        }

        struct FileGuard {
            FILE* fp;
            ~FileGuard() { if (fp) fclose(fp); }
        } guard(fp);

        // Validate file size to ensure it matches our grid dimensions.
        fseek(fp, 0, SEEK_END);
        long file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (file_size != ProximityGrid::CELL_COUNT) {
            gi.Com_PrintFmt("ProximityGrid Cache: File is corrupt or outdated (size mismatch). Deleting: {}\n", cache_path.string());
            fclose(fp); // Close before deleting
            guard.fp = nullptr;
            std::error_code ec;
            std::filesystem::remove(cache_path, ec);
            return false;
        }

        // Read the compact byte data.
        std::array<uint8_t, ProximityGrid::CELL_COUNT> buffer;
        if (fread(buffer.data(), sizeof(uint8_t), ProximityGrid::CELL_COUNT, fp) != ProximityGrid::CELL_COUNT) {
            gi.Com_PrintFmt("ProximityGrid Cache: Error reading from file: {}\n", cache_path.string());
            return false;
        }

        // Convert byte data back to the boolean array.
        for (size_t i = 0; i < ProximityGrid::CELL_COUNT; ++i) {
            out_walkable_data[i] = (buffer[i] == 1);
        }

        return true;
    }

    //======================================================================
    // --- ProximityGrid Member Functions ---
    //======================================================================

    void ProximityGrid::Build(const vec3_t& world_mins, const vec3_t& world_maxs) {
        // --- Standard Grid Initialization ---
        for (auto& cell : m_cells) {
            cell.clear();
        }
        m_is_cell_walkable.fill(false);
        m_entity_query_stamps.fill(0);
        m_current_query_id = 1;

        m_world_mins = world_mins;
        const vec3_t world_size = world_maxs - world_mins;
        
        // These need to be set regardless of cache hit or miss
        m_cell_size = std::max(std::max(world_size.x, world_size.y), 1.0f) / static_cast<float>(GRID_DIMENSION);
        m_inv_cell_size = 1.0f / m_cell_size;

        // --- ATTEMPT TO LOAD FROM CACHE ---
        if (LoadGridFromFile(level.mapname, m_is_cell_walkable)) {
            m_is_built = true;
            gi.Com_PrintFmt("ProximityGrid: Successfully loaded walkable data from cache for {}.\n", level.mapname);
            return; // We're done!
        }

        // --- CACHE MISS: PERFORM EXPENSIVE BUILD ---
        gi.Com_PrintFmt("ProximityGrid: No cache found. Starting expensive build process for {}...\n", level.mapname);

        constexpr float sample_step_xy = 32.0f;
        constexpr float sample_step_z = 16.0f;
        constexpr float player_height = 56.0f;
        constexpr float player_crouch_height = 32.0f;
        const vec3_t player_mins = {-16, -16, 0};
        const vec3_t player_maxs = {16, 16, player_crouch_height};
        constexpr contents_t walkable_mask = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP;

        int walkable_cell_count = 0;
        std::array<float, CELL_COUNT> ground_z_map;
        ground_z_map.fill(world_mins.z);

        int num_x_steps = static_cast<int>(world_size.x / sample_step_xy);
        int num_y_steps = static_cast<int>(world_size.y / sample_step_xy);
        int num_z_steps = static_cast<int>(world_size.z / sample_step_z);

        for (int ix = 0; ix < num_x_steps; ++ix) {
            for (int iy = 0; iy < num_y_steps; ++iy) {
                for (int iz = num_z_steps - 1; iz >= 0; --iz) {
                    vec3_t sample_point = {
                        m_world_mins.x + (ix + 0.5f) * sample_step_xy,
                        m_world_mins.y + (iy + 0.5f) * sample_step_xy,
                        m_world_mins.z + (iz + 0.5f) * sample_step_z
                    };

                    if (gi.pointcontents(sample_point) & MASK_OPAQUE) continue;

                    vec3_t trace_end = sample_point;
                    trace_end.z = world_mins.z;
                    trace_t floor_trace = gi.trace(sample_point, vec3_origin, vec3_origin, trace_end, nullptr, walkable_mask);

                    if (floor_trace.fraction == 1.0f || (floor_trace.surface && (floor_trace.surface->flags & SURF_SKY))) continue;
                    if (floor_trace.plane.normal.z < 0.7f) continue;

                    iz = static_cast<int>((floor_trace.endpos.z - m_world_mins.z) / sample_step_z);

                    vec3_t headroom_start = floor_trace.endpos;
                    headroom_start.z += player_height;
                    trace_t headroom_trace = gi.trace(headroom_start, player_mins, player_maxs, floor_trace.endpos, nullptr, walkable_mask);

                    if (headroom_trace.startsolid || headroom_trace.allsolid || headroom_trace.fraction < 1.0f) continue;
                    
                    const vec3_t& valid_pos = floor_trace.endpos;
                    int cell_idx = GetCellIndex(valid_pos);

                    if (cell_idx != -1 && !m_is_cell_walkable[cell_idx]) {
                        m_is_cell_walkable[cell_idx] = true;
                        ground_z_map[cell_idx] = valid_pos.z;
                        walkable_cell_count++;
                    }
                }
            }
        }

        m_is_built = true;

        // --- SAVE THE NEWLY GENERATED DATA TO CACHE ---
        SaveGridToFile(level.mapname, m_is_cell_walkable);

        // --- DEBUG VISUALIZATION ---
        if (developer->integer) {
            gi.Com_PrintFmt("ProximityGrid Built: Found {} walkable cells out of {}.\n", walkable_cell_count, CELL_COUNT);
            if (developer->integer >= 2) {
                for (int y = 0; y < GRID_DIMENSION; ++y) {
                    for (int x = 0; x < GRID_DIMENSION; ++x) {
                        const int cell_idx = y * GRID_DIMENSION + x;
                        vec3_t cell_min = { m_world_mins.x + x * m_cell_size, m_world_mins.y + y * m_cell_size, 0 };
                        vec3_t cell_max = cell_min + vec3_t{m_cell_size, m_cell_size, 1.0f};
                        if (m_is_cell_walkable[cell_idx]) {
                            cell_min.z = ground_z_map[cell_idx];
                            cell_max.z = ground_z_map[cell_idx] + 1.0f;
                            gi.Draw_Bounds(cell_min, cell_max, rgba_green, 60, false);
                        } else {
                            cell_min.z = m_world_mins.z;
                            cell_max.z = m_world_mins.z + 1.0f;
                            gi.Draw_Bounds(cell_min, cell_max, rgba_red, 60, false);
                        }
                    }
                }
            }
        }
    }

    // =======================================================================
    // REPLACEMENT: ProximityGrid::GetPotentialColliders (Optimized)
    //
    // This is the highly optimized version. Instead of clearing a large
    // boolean array on every call, it uses the "Query ID" technique.
    // 1. A global query ID for the grid is incremented on each call.
    // 2. An entity is only added if its "stamp" doesn't match the current ID.
    // 3. When an entity is added, its stamp is updated to the current ID.
    // This is dramatically faster than the previous method.
    // =======================================================================
    std::span<edict_t* const> ProximityGrid::GetPotentialColliders(edict_t* ent) {
        if (!m_is_built || !ent) {
            return {};
        }

        // --- The Query ID Optimization ---
        // 1. Increment the ID for this specific query. This invalidates all
        //    stamps from the previous call without an expensive array clear.
        m_current_query_id++;

        // 2. Handle the astronomically rare case of the ID wrapping around to 0.
        //    If it does, we must perform a one-time reset.
        if (m_current_query_id == 0) {
            m_entity_query_stamps.fill(0);
            m_current_query_id = 1;
        }
        // --- End Optimization ---

        size_t buffer_count = 0;
        const int min_x = std::clamp(static_cast<int>((ent->absmin.x - m_world_mins.x) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
        const int max_x = std::clamp(static_cast<int>((ent->absmax.x - m_world_mins.x) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
        const int min_y = std::clamp(static_cast<int>((ent->absmin.y - m_world_mins.y) * m_inv_cell_size), 0, GRID_DIMENSION - 1);
        const int max_y = std::clamp(static_cast<int>((ent->absmax.y - m_world_mins.y) * m_inv_cell_size), 0, GRID_DIMENSION - 1);

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                const int cell_idx = y * GRID_DIMENSION + x;
                const auto& cell = m_cells[cell_idx];
                for (size_t i = 0; i < cell.count; ++i) {
                    edict_t* other = cell.monsters[i];
                    if (other != ent) {
                        const int other_idx = other - g_edicts;

                        // 3. Check if the entity has been stamped for THIS query.
                        if (m_entity_query_stamps[other_idx] != m_current_query_id) {
                            if (buffer_count < m_query_buffer.size()) {
                                m_query_buffer[buffer_count++] = other;
                                // 4. Stamp the entity with the current query ID.
                                m_entity_query_stamps[other_idx] = m_current_query_id;
                            }
                        }
                    }
                }
            }
        }
        return {m_query_buffer.data(), buffer_count};
    }

    // ... (rest of your g_horde_phys.cpp functions like Add, GetCellIndex, etc.)
    
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
    
    bool ProximityGrid::IsCellWalkable(int cell_index) const {
        if (cell_index < 0 || cell_index >= CELL_COUNT) return false;
        return m_is_cell_walkable[cell_index];
    }

} // namespace HordePhys