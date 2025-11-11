// network_monitor.cpp

#include "network_monitor.h"
#include "g_local.h"
#include <vector>
#include <string>
#include <boost/unordered/unordered_flat_map.hpp>

namespace NetworkMonitor {

    // ========================================================================
    // Global State
    // ========================================================================

    NetworkMonitorState g_network_monitor;

    // Config string queue for batching
    struct QueuedConfigString {
        int index;
        std::string value;
    };
    static std::vector<QueuedConfigString> g_config_string_queue;

    // HUD caches per client (indexed by client number)
    static boost::unordered::unordered_flat_map<int, HUDCache> g_hud_caches;

    // ========================================================================
    // Implementation
    // ========================================================================

    void Init() {
        g_network_monitor = NetworkMonitorState{};
        g_config_string_queue.clear();
        g_hud_caches.clear();

        if (developer && developer->integer) {
            gi.Com_Print("Network Monitor: Initialized\n");
        }
    }

    void ResetFrame() {
        g_network_monitor.current_frame.reset();
    }

    void EndFrame() {
        // Calculate total bytes for this frame
        size_t total_bytes = g_network_monitor.current_frame.calculate_total();
        g_network_monitor.current_frame.total_bytes_estimated = total_bytes;

        // Record in history
        g_network_monitor.history.record_frame(total_bytes);

        // Check thresholds and issue warnings
        int64_t current_time_ms = level.time.milliseconds();
        if (total_bytes >= CRITICAL_BUFFER_THRESHOLD) {
            // Critical threshold - always warn
            if (current_time_ms > g_network_monitor.last_warning_time_ms + 1000) {
                gi.Com_PrintFmt("CRITICAL: Network buffer usage at {} bytes (limit: {})\n",
                    total_bytes, ENGINE_BUFFER_LIMIT);
                gi.Com_PrintFmt("  Config strings: {}, Models: {}, Sounds: {}, HUD: {}\n",
                    g_network_monitor.current_frame.config_string_updates,
                    g_network_monitor.current_frame.model_precaches,
                    g_network_monitor.current_frame.sound_precaches,
                    g_network_monitor.current_frame.hud_updates);
                g_network_monitor.last_warning_time_ms = current_time_ms;
                g_network_monitor.overflow_warnings_count++;
            }
            g_network_monitor.throttling_active = true;
        } else if (total_bytes >= SAFE_BUFFER_THRESHOLD) {
            // Safe threshold - throttle but only warn occasionally
            if (current_time_ms > g_network_monitor.last_warning_time_ms + 5000) {
                if (developer && developer->integer) {
                    gi.Com_PrintFmt("WARNING: Network buffer usage at {} bytes (throttling active)\n",
                        total_bytes);
                }
                g_network_monitor.last_warning_time_ms = current_time_ms;
            }
            g_network_monitor.throttling_active = true;
        } else if (total_bytes >= WARNING_BUFFER_THRESHOLD) {
            // Warning threshold - just log in developer mode
            if (developer && developer->integer >= 2) {
                if (current_time_ms > g_network_monitor.last_warning_time_ms + 10000) {
                    gi.Com_PrintFmt("INFO: Network buffer usage at {} bytes\n", total_bytes);
                    g_network_monitor.last_warning_time_ms = current_time_ms;
                }
            }
            g_network_monitor.throttling_active = false;
        } else {
            // Below warning threshold - normal operation
            g_network_monitor.throttling_active = false;
        }

        // Process queued config strings if we have budget
        if (!g_network_monitor.throttling_active && !g_config_string_queue.empty()) {
            ProcessQueuedConfigStrings();
        }
    }

    void RecordConfigString(size_t string_length) {
        g_network_monitor.current_frame.config_string_updates++;
        g_network_monitor.current_frame.total_bytes_estimated += string_length;
    }

    void RecordModelPrecache() {
        g_network_monitor.current_frame.model_precaches++;
        g_network_monitor.current_frame.total_bytes_estimated += BYTES_PER_MODEL_INDEX;
    }

    void RecordSoundPrecache() {
        g_network_monitor.current_frame.sound_precaches++;
        g_network_monitor.current_frame.total_bytes_estimated += BYTES_PER_SOUND_INDEX;
    }

    void RecordEntityUpdate() {
        g_network_monitor.current_frame.entity_updates++;
        g_network_monitor.current_frame.total_bytes_estimated += BYTES_PER_ENTITY_UPDATE;
    }

    void RecordHUDUpdate(size_t string_length) {
        g_network_monitor.current_frame.hud_updates++;
        g_network_monitor.current_frame.total_bytes_estimated += string_length;
    }

    bool ShouldThrottle() {
        return g_network_monitor.throttling_active ||
               g_network_monitor.current_frame.total_bytes_estimated >= SAFE_BUFFER_THRESHOLD;
    }

    bool ShouldThrottleCritical() {
        return g_network_monitor.current_frame.total_bytes_estimated >= CRITICAL_BUFFER_THRESHOLD;
    }

    const FrameTrafficStats& GetCurrentFrameStats() {
        return g_network_monitor.current_frame;
    }

    size_t GetEstimatedBytesThisFrame() {
        return g_network_monitor.current_frame.calculate_total();
    }

    void PrintStats() {
        const auto& stats = g_network_monitor.current_frame;
        const auto& history = g_network_monitor.history;

        gi.Com_Print("\n=== Network Monitor Statistics ===\n");
        gi.Com_PrintFmt("Current Frame: {} bytes (estimated)\n", stats.total_bytes_estimated);
        gi.Com_PrintFmt("  Config Strings: {} ({} bytes)\n",
            stats.config_string_updates,
            stats.config_string_updates * BYTES_PER_CONFIGSTRING);
        gi.Com_PrintFmt("  Model Precaches: {} ({} bytes)\n",
            stats.model_precaches,
            stats.model_precaches * BYTES_PER_MODEL_INDEX);
        gi.Com_PrintFmt("  Sound Precaches: {} ({} bytes)\n",
            stats.sound_precaches,
            stats.sound_precaches * BYTES_PER_SOUND_INDEX);
        gi.Com_PrintFmt("  HUD Updates: {} ({} bytes)\n",
            stats.hud_updates,
            stats.hud_updates * BYTES_PER_HUD_UPDATE);
        gi.Com_PrintFmt("\nHistory (last 60 frames):\n");
        gi.Com_PrintFmt("  Average: {} bytes/frame\n", history.get_average_bytes());
        gi.Com_PrintFmt("  Maximum: {} bytes/frame\n", history.get_max_bytes());
        gi.Com_PrintFmt("  Peak Ever: {} bytes/frame\n", history.max_bytes_seen);
        gi.Com_PrintFmt("\nThrottling: {}\n", g_network_monitor.throttling_active ? "ACTIVE" : "Inactive");
        gi.Com_PrintFmt("Overflow Warnings: {}\n", g_network_monitor.overflow_warnings_count);
        gi.Com_PrintFmt("Queued Config Strings: {}\n", g_config_string_queue.size());
        gi.Com_Print("==================================\n");
    }

    // ========================================================================
    // Config String Batching
    // ========================================================================

    void QueueConfigString(int index, const char* value) {
        // If not throttling, send immediately
        if (!ShouldThrottle()) {
            gi.configstring(index, value);
            RecordConfigString(value ? strlen(value) : 0);
            return;
        }

        // Queue for later
        QueuedConfigString queued;
        queued.index = index;
        queued.value = value ? value : "";
        g_config_string_queue.push_back(queued);
        g_network_monitor.config_strings_deferred++;

        if (developer && developer->integer >= 2) {
            gi.Com_PrintFmt("Network Monitor: Queued config string {} (queue size: {})\n",
                index, g_config_string_queue.size());
        }
    }

    void ProcessQueuedConfigStrings() {
        if (g_config_string_queue.empty()) {
            return;
        }

        // Process up to 10 config strings per frame when not throttling
        constexpr size_t MAX_PER_FRAME = 10;
        size_t processed = 0;

        while (!g_config_string_queue.empty() && processed < MAX_PER_FRAME) {
            const auto& queued = g_config_string_queue.front();

            // Check if we still have budget
            if (GetEstimatedBytesThisFrame() + BYTES_PER_CONFIGSTRING >= SAFE_BUFFER_THRESHOLD) {
                break;
            }

            // Send the config string
            gi.configstring(queued.index, queued.value.c_str());
            RecordConfigString(queued.value.length());

            // Remove from queue
            g_config_string_queue.erase(g_config_string_queue.begin());
            processed++;
        }

        if (processed > 0 && developer && developer->integer >= 2) {
            gi.Com_PrintFmt("Network Monitor: Processed {} queued config strings ({} remaining)\n",
                processed, g_config_string_queue.size());
        }
    }

    // ========================================================================
    // HUD Delta Encoding
    // ========================================================================

    bool NeedHUDUpdate(edict_t* client, const char* new_text) {
        if (!client || !client->client) {
            return false;
        }

        int client_num = client - g_edicts - 1;
        if (client_num < 0 || client_num >= game.maxclients) {
            return false;
        }

        // Get or create cache for this client
        auto& cache = g_hud_caches[client_num];

        // Always update if it's been more than 2 seconds (force refresh)
        int64_t current_time_ms = level.time.milliseconds();
        if (current_time_ms > cache.last_update_time_ms + 2000) {
            return true;
        }

        // Check if text actually changed
        if (new_text && cache.wave_text != new_text) {
            return true;
        }

        // No update needed
        return false;
    }

    void UpdateHUDCache(edict_t* client, const char* text) {
        if (!client || !client->client) {
            return;
        }

        int client_num = client - g_edicts - 1;
        if (client_num < 0 || client_num >= game.maxclients) {
            return;
        }

        auto& cache = g_hud_caches[client_num];
        cache.wave_text = text ? text : "";
        cache.last_update_time_ms = level.time.milliseconds();
    }

} // namespace NetworkMonitor
