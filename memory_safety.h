#pragma once

#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <limits>
#include <new>

// Critical limits to prevent overflow crashes
// 65535 is the uint16_t max - a common overflow point
constexpr size_t MAX_SAFE_CONTAINER_SIZE = 65535;
constexpr size_t MAX_SAFE_RESERVE_SIZE = 32768;  // Half of 65535 for safety margin
constexpr size_t MAX_STRING_BUILD_SIZE = 16384;
constexpr size_t MAX_SPAWN_POINTS = 64;  // Most Q2 maps have far fewer info_player_deathmatch entities
constexpr size_t MAX_ENTITIES_PER_FRAME = 4096;
constexpr size_t MAX_PROFILER_HISTORY = 1000;
constexpr size_t MAX_SKIPPED_PROJECTILES = 256;

// Safe allocation wrapper for vectors
template<typename T>
inline bool safe_reserve(std::vector<T>& vec, size_t new_capacity) {
    try {
        // Prevent overflow - check against both size_t max and our safety limit
        if (new_capacity > MAX_SAFE_CONTAINER_SIZE) {
            if (developer && developer->integer) {
                gi.Com_PrintFmt("WARNING: Attempted to reserve {} elements, clamping to {}\n",
                    new_capacity, MAX_SAFE_CONTAINER_SIZE);
            }
            new_capacity = MAX_SAFE_CONTAINER_SIZE;
        }

        // Check for potential multiplication overflow
        size_t bytes_needed = new_capacity * sizeof(T);
        if (bytes_needed / sizeof(T) != new_capacity) {
            // Overflow would occur
            return false;
        }

        vec.reserve(new_capacity);
        return true;
    } catch (const std::bad_alloc&) {
        if (developer && developer->integer) {
            gi.Com_Print("ERROR: Failed to allocate memory for vector reserve\n");
        }
        return false;
    } catch (...) {
        return false;
    }
}

// Safe resize for vectors with bounds checking
template<typename T>
inline bool safe_resize(std::vector<T>& vec, size_t new_size, size_t max_size = MAX_SAFE_CONTAINER_SIZE) {
    try {
        // Prevent overflow - especially important for uint16_t indices
        if (new_size > max_size) {
            if (developer && developer->integer) {
                gi.Com_PrintFmt("WARNING: Attempted to resize to {} elements, clamping to {}\n",
                    new_size, max_size);
            }
            new_size = max_size;
        }

        // Check for multiplication overflow
        size_t bytes_needed = new_size * sizeof(T);
        if (bytes_needed / sizeof(T) != new_size) {
            return false;
        }

        vec.resize(new_size);
        return true;
    } catch (const std::bad_alloc&) {
        if (developer && developer->integer) {
            gi.Com_Print("ERROR: Failed to allocate memory for vector resize\n");
        }
        return false;
    } catch (...) {
        return false;
    }
}

// Safe push_back with size limit checking (copy version)
template<typename T>
inline bool safe_push_back(std::vector<T>& vec, const T& value, size_t max_size = MAX_SAFE_CONTAINER_SIZE) {
    try {
        if (vec.size() >= max_size) {
            if (developer && developer->integer) {
                static int overflow_count = 0;
                if (overflow_count++ < 10) {  // Limit spam
                    gi.Com_PrintFmt("WARNING: Container size limit {} reached, not adding element\n", max_size);
                }
            }
            return false;
        }

        vec.push_back(value);
        return true;
    } catch (const std::bad_alloc&) {
        if (developer && developer->integer) {
            gi.Com_Print("ERROR: Failed to allocate memory for push_back\n");
        }
        return false;
    } catch (...) {
        return false;
    }
}

// Safe push_back with size limit checking (move version - for performance)
template<typename T>
inline bool safe_push_back(std::vector<T>& vec, T&& value, size_t max_size = MAX_SAFE_CONTAINER_SIZE) {
    try {
        if (vec.size() >= max_size) {
            if (developer && developer->integer) {
                static int overflow_count = 0;
                if (overflow_count++ < 10) {
                    gi.Com_PrintFmt("WARNING: Container size limit {} reached, not adding element\n", max_size);
                }
            }
            return false;
        }

        vec.push_back(std::move(value));
        return true;
    } catch (const std::bad_alloc&) {
        if (developer && developer->integer) {
            gi.Com_Print("ERROR: Failed to allocate memory for push_back\n");
        }
        return false;
    } catch (...) {
        return false;
    }
}

// Unchecked push_back for hot paths where size is pre-validated
template<typename T>
inline bool unsafe_push_back(std::vector<T>& vec, T&& value) {
    try {
        vec.push_back(std::forward<T>(value));
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (...) {
        return false;
    }
}

// Safe emplace_back with explicit size limit
template<typename T, typename... Args>
inline bool safe_emplace_back_limit(std::vector<T>& vec, size_t max_size, Args&&... args) {
    try {
        if (vec.size() >= max_size) {
            if (developer && developer->integer) {
                static int overflow_count = 0;
                if (overflow_count++ < 10) {
                    gi.Com_PrintFmt("WARNING: Container size limit {} reached, not emplacing element\n", max_size);
                }
            }
            return false;
        }

        vec.emplace_back(std::forward<Args>(args)...);
        return true;
    } catch (const std::bad_alloc&) {
        if (developer && developer->integer) {
            gi.Com_Print("ERROR: Failed to allocate memory for emplace_back\n");
        }
        return false;
    } catch (...) {
        return false;
    }
}

// Safe emplace_back with default size limit
template<typename T, typename... Args>
inline bool safe_emplace_back(std::vector<T>& vec, Args&&... args) {
    return safe_emplace_back_limit(vec, MAX_SAFE_CONTAINER_SIZE, std::forward<Args>(args)...);
}

// Unchecked emplace_back for hot paths
template<typename T, typename... Args>
inline bool unsafe_emplace_back(std::vector<T>& vec, Args&&... args) {
    try {
        vec.emplace_back(std::forward<Args>(args)...);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (...) {
        return false;
    }
}

// Safe exponential growth with maximum limit
inline size_t safe_grow_capacity(size_t current_capacity, size_t min_needed) {
    // Prevent overflow when doubling
    const size_t max_double = MAX_SAFE_CONTAINER_SIZE / 2;

    if (current_capacity > max_double) {
        // Can't double safely, grow linearly or hit max
        size_t new_capacity = current_capacity + 1024;
        if (new_capacity < current_capacity) {
            // Overflow occurred
            return MAX_SAFE_CONTAINER_SIZE;
        }
        return std::min(new_capacity, MAX_SAFE_CONTAINER_SIZE);
    }

    // Safe to double
    size_t new_capacity = current_capacity * 2;
    if (new_capacity < min_needed) {
        new_capacity = min_needed;
    }

    return std::min(new_capacity, MAX_SAFE_CONTAINER_SIZE);
}

// Periodic cleanup helper for accumulating containers
template<typename Container>
inline void periodic_cleanup(Container& container, size_t max_size, size_t cleanup_threshold) {
    if (container.size() > cleanup_threshold) {
        if (container.size() > max_size) {
            // Emergency cleanup
            if constexpr (std::is_same_v<Container, std::vector<typename Container::value_type>>) {
                // For vectors: keep newest elements, move them to front
                size_t keep_count = max_size / 2;  // Keep half after cleanup
                size_t remove_count = container.size() - keep_count;

                // Move newest elements to beginning (more efficient than erase)
                if (keep_count > 0) {
                    std::move(container.begin() + remove_count, container.end(), container.begin());
                }
                container.resize(keep_count);

                // Shrink capacity if way oversized
                if (container.capacity() > max_size * 2) {
                    container.shrink_to_fit();
                }
            } else {
                // For other containers, clear if too large
                container.clear();
            }

            if (developer && developer->integer) {
                gi.Com_PrintFmt("WARNING: Container exceeded max size ({}), performed emergency cleanup\n",
                                container.size());
            }
        }
    }
}

// Safe insert for maps with size limit checking
template<typename Map, typename Key, typename Value>
inline bool safe_map_insert(Map& map, const Key& key, const Value& value, size_t max_size = MAX_SAFE_CONTAINER_SIZE) {
    try {
        if (map.size() >= max_size) {
            if (developer && developer->integer) {
                static int overflow_count = 0;
                if (overflow_count++ < 10) {
                    gi.Com_PrintFmt("WARNING: Map size limit {} reached, not inserting\n", max_size);
                }
            }
            return false;
        }

        map.insert({key, value});
        return true;
    } catch (const std::bad_alloc&) {
        if (developer && developer->integer) {
            gi.Com_Print("ERROR: Failed to allocate memory for map insert\n");
        }
        return false;
    } catch (...) {
        return false;
    }
}

// Safe emplace for maps with size limit checking
template<typename Map, typename... Args>
inline bool safe_map_emplace(Map& map, size_t max_size, Args&&... args) {
    try {
        if (map.size() >= max_size) {
            if (developer && developer->integer) {
                static int overflow_count = 0;
                if (overflow_count++ < 10) {
                    gi.Com_PrintFmt("WARNING: Map size limit {} reached, not emplacing\n", max_size);
                }
            }
            return false;
        }

        map.emplace(std::forward<Args>(args)...);
        return true;
    } catch (const std::bad_alloc&) {
        if (developer && developer->integer) {
            gi.Com_Print("ERROR: Failed to allocate memory for map emplace\n");
        }
        return false;
    } catch (...) {
        return false;
    }
}

// Safe operator[] for maps (may insert default value)
template<typename Map, typename Key>
inline typename Map::mapped_type* safe_map_access(Map& map, const Key& key, size_t max_size = MAX_SAFE_CONTAINER_SIZE) {
    try {
        // Check if key exists
        auto it = map.find(key);
        if (it != map.end()) {
            return &it->second;
        }

        // Key doesn't exist, check size before inserting
        if (map.size() >= max_size) {
            if (developer && developer->integer) {
                static int overflow_count = 0;
                if (overflow_count++ < 10) {
                    gi.Com_PrintFmt("WARNING: Map size limit {} reached, cannot access new key\n", max_size);
                }
            }
            return nullptr;
        }

        // Safe to insert
        return &(map[key]);
    } catch (const std::bad_alloc&) {
        if (developer && developer->integer) {
            gi.Com_Print("ERROR: Failed to allocate memory for map access\n");
        }
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

// Safe string append with size checking
inline bool safe_string_append(std::string& str, const std::string_view& to_append, size_t max_size = MAX_STRING_BUILD_SIZE) {
    if (str.size() + to_append.size() > max_size) {
        if (developer && developer->integer) {
            gi.Com_PrintFmt("WARNING: String would exceed max size {}, truncating\n", max_size);
        }
        size_t space_left = max_size > str.size() ? max_size - str.size() : 0;
        if (space_left > 0) {
            str.append(to_append.data(), space_left);
        }
        return false;
    }

    try {
        str.append(to_append);
        return true;
    } catch (const std::bad_alloc&) {
        if (developer && developer->integer) {
            gi.Com_Print("ERROR: Failed to allocate memory for string append\n");
        }
        return false;
    }
}

// RAII wrapper for FILE* to prevent file handle leaks
// Automatically closes file on scope exit (including exceptions)
struct FileGuard {
    FILE* fp;

    explicit FileGuard(FILE* f) : fp(f) {}

    ~FileGuard() {
        if (fp) {
            fclose(fp);
        }
    }

    // Non-copyable, non-movable
    FileGuard(const FileGuard&) = delete;
    FileGuard& operator=(const FileGuard&) = delete;
    FileGuard(FileGuard&&) = delete;
    FileGuard& operator=(FileGuard&&) = delete;

    // Allow boolean check for validity
    explicit operator bool() const { return fp != nullptr; }

    // Allow usage as FILE* in function calls
    operator FILE*() const { return fp; }
};