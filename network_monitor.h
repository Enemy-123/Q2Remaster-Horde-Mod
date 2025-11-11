#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <vector>

// Forward declarations to avoid circular dependencies
struct edict_t;

// ============================================================================
// Network Traffic Monitor - Prevents SZ_GetSpace buffer overflow (65535 bytes)
// ============================================================================
// The Quake II engine has a fixed network message buffer limit of 65535 bytes
// (USHRT_MAX). When this limit is exceeded, the game crashes with:
// "SZ_GetSpace: overflow without allowoverflow set with a length of 65530"
//
// This system monitors network traffic and implements throttling to prevent
// the buffer from overflowing, especially during:
// - Asset precaching (models, sounds)
// - Config string updates (monster stats, HUD data)
// - Entity state updates
// ============================================================================

namespace NetworkMonitor {

    // Network message buffer limits (from Quake II engine)
    constexpr size_t ENGINE_BUFFER_LIMIT = 65535;         // Absolute max (USHRT_MAX)
    constexpr size_t SAFE_BUFFER_THRESHOLD = 60000;       // Start throttling at 60KB
    constexpr size_t CRITICAL_BUFFER_THRESHOLD = 63000;   // Emergency throttling at 63KB
    constexpr size_t WARNING_BUFFER_THRESHOLD = 55000;    // First warning at 55KB

    // Estimated sizes for different network message types (in bytes)
    constexpr size_t BYTES_PER_CONFIGSTRING = 128;        // Average config string size
    constexpr size_t BYTES_PER_MODEL_INDEX = 64;          // Model precache entry
    constexpr size_t BYTES_PER_SOUND_INDEX = 48;          // Sound precache entry
    constexpr size_t BYTES_PER_ENTITY_UPDATE = 32;        // Entity state update
    constexpr size_t BYTES_PER_HUD_UPDATE = 256;          // HUD string update

    // Traffic statistics for current frame
    struct FrameTrafficStats {
        size_t total_bytes_estimated = 0;
        uint32_t config_string_updates = 0;
        uint32_t model_precaches = 0;
        uint32_t sound_precaches = 0;
        uint32_t entity_updates = 0;
        uint32_t hud_updates = 0;

        void reset() {
            total_bytes_estimated = 0;
            config_string_updates = 0;
            model_precaches = 0;
            sound_precaches = 0;
            entity_updates = 0;
            hud_updates = 0;
        }

        // Calculate total estimated bytes
        size_t calculate_total() const {
            return (config_string_updates * BYTES_PER_CONFIGSTRING) +
                   (model_precaches * BYTES_PER_MODEL_INDEX) +
                   (sound_precaches * BYTES_PER_SOUND_INDEX) +
                   (entity_updates * BYTES_PER_ENTITY_UPDATE) +
                   (hud_updates * BYTES_PER_HUD_UPDATE);
        }
    };

    // Traffic history for analysis
    struct TrafficHistory {
        static constexpr size_t HISTORY_SIZE = 60;  // Track last 60 frames (~1 second)
        std::array<size_t, HISTORY_SIZE> bytes_per_frame = {};
        size_t current_index = 0;
        size_t max_bytes_seen = 0;
        int64_t last_overflow_warning_ms = 0;  // Time in milliseconds

        void record_frame(size_t bytes) {
            bytes_per_frame[current_index] = bytes;
            current_index = (current_index + 1) % HISTORY_SIZE;
            if (bytes > max_bytes_seen) {
                max_bytes_seen = bytes;
            }
        }

        size_t get_average_bytes() const {
            size_t sum = 0;
            for (size_t bytes : bytes_per_frame) {
                sum += bytes;
            }
            return sum / HISTORY_SIZE;
        }

        size_t get_max_bytes() const {
            size_t max = 0;
            for (size_t bytes : bytes_per_frame) {
                if (bytes > max) max = bytes;
            }
            return max;
        }
    };

    // Main monitor state
    struct NetworkMonitorState {
        FrameTrafficStats current_frame;
        TrafficHistory history;
        bool throttling_active = false;
        int64_t last_warning_time_ms = 0;  // Time in milliseconds
        uint32_t overflow_warnings_count = 0;

        // Throttling state
        uint32_t config_strings_deferred = 0;
        uint32_t precaches_deferred = 0;
    };

    // Global monitor instance (defined in .cpp)
    extern NetworkMonitorState g_network_monitor;

    // ========================================================================
    // Public API
    // ========================================================================

    // Initialize the monitor (call at map start)
    void Init();

    // Reset frame stats (call at beginning of G_RunFrame)
    void ResetFrame();

    // Update history and check thresholds (call at end of G_RunFrame)
    void EndFrame();

    // Record network traffic
    void RecordConfigString(size_t string_length = BYTES_PER_CONFIGSTRING);
    void RecordModelPrecache();
    void RecordSoundPrecache();
    void RecordEntityUpdate();
    void RecordHUDUpdate(size_t string_length = BYTES_PER_HUD_UPDATE);

    // Check if we should throttle network traffic this frame
    [[nodiscard]] bool ShouldThrottle();
    [[nodiscard]] bool ShouldThrottleCritical();

    // Get current frame stats
    [[nodiscard]] const FrameTrafficStats& GetCurrentFrameStats();
    [[nodiscard]] size_t GetEstimatedBytesThisFrame();

    // Print statistics (for debugging)
    void PrintStats();

    // ========================================================================
    // Config String Batching System
    // ========================================================================

    // Batched config string update (queues if throttling active)
    void QueueConfigString(int index, const char* value);

    // Process queued config strings (called when safe to send)
    void ProcessQueuedConfigStrings();

    // ========================================================================
    // HUD Delta Encoding
    // ========================================================================

    // Cache for last HUD state sent to each client
    struct HUDCache {
        std::string wave_text;
        std::string monsters_text;
        std::string items_text;
        int32_t last_wave = -1;
        int32_t last_monsters_remaining = -1;
        int64_t last_update_time_ms = 0;  // Time in milliseconds
    };

    // Check if HUD update is needed (returns false if no change)
    [[nodiscard]] bool NeedHUDUpdate(edict_t* client, const char* new_text);

    // Update HUD cache for client
    void UpdateHUDCache(edict_t* client, const char* text);

} // namespace NetworkMonitor
