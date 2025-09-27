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
inline bool safe_resize(std::vector<T>& vec, size_t new_size) {
    try {
        // Prevent overflow - especially important for uint16_t indices
        if (new_size > MAX_SAFE_CONTAINER_SIZE) {
            if (developer && developer->integer) {
                gi.Com_PrintFmt("WARNING: Attempted to resize to {} elements, clamping to {}\n",
                    new_size, MAX_SAFE_CONTAINER_SIZE);
            }
            new_size = MAX_SAFE_CONTAINER_SIZE;
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

// Safe push_back with size limit checking
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

// Safe emplace_back with size limit checking
template<typename T, typename... Args>
inline bool safe_emplace_back(std::vector<T>& vec, size_t max_size, Args&&... args) {
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
            // Emergency cleanup - remove oldest entries
            if constexpr (std::is_same_v<Container, std::vector<typename Container::value_type>>) {
                container.erase(container.begin(), container.begin() + (container.size() - max_size));
            } else {
                // For other containers, clear if too large
                container.clear();
            }

            if (developer && developer->integer) {
                gi.Com_Print("WARNING: Container exceeded max size, performing emergency cleanup\n");
            }
        }
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