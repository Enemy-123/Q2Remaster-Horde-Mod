// Network Overflow Prevention System for Quake II Remaster Horde Mod
// This file contains fixes for the 999 ping and server message overflow issues

#include "../g_local.h"
#include "../memory_safety.h"
#include "g_horde.h"
#include "../q_vec3.h"
#include <algorithm>
#include <vector>
#include <string>

// Global network throttling state
struct NetworkThrottle {
    std::vector<gtime_t> last_update_time;  // Heap allocation with safe_reserve
    std::vector<int> client_reliable_bytes_this_frame;  // Per-client tracking
    int message_count_this_frame;
    int reliable_bytes_this_frame;  // Track reliable message buffer usage
    static constexpr int MAX_MESSAGES_PER_FRAME = 100;  // Increased from 10 to reduce lag
    // Keep reasonable limits but not so low that it causes lag
    static constexpr int MAX_RELIABLE_BYTES_PER_FRAME = 4096;  // Increased to reduce lag
    static constexpr int MAX_RELIABLE_BYTES_PER_CLIENT = 3072;  // Increased to reduce lag
    static constexpr gtime_t MIN_UPDATE_INTERVAL = 16_ms;  // Reduced from 50ms for smoother updates

    NetworkThrottle() : message_count_this_frame(0), reliable_bytes_this_frame(0) {
        // Safe allocation of MAX_EDICTS slots - MUST succeed or vectors stay empty
        // Cannot return early from constructor, so we proceed regardless
        if (!safe_reserve(last_update_time, MAX_EDICTS)) {
            gi.Com_Print("ERROR: Failed to allocate network throttle memory\n");
            // Attempt to allocate at least a minimal size to avoid crashes
            try {
                last_update_time.resize(MAX_EDICTS);
            } catch (...) {
                // Last resort: minimal allocation
                try { last_update_time.resize(256); } catch (...) {}
            }
        } else {
            last_update_time.resize(MAX_EDICTS);  // Default constructor initializes to 0
        }

        if (!safe_reserve(client_reliable_bytes_this_frame, MAX_CLIENTS)) {
            gi.Com_Print("ERROR: Failed to allocate client network tracking memory\n");
            // Attempt allocation regardless
            try {
                client_reliable_bytes_this_frame.resize(MAX_CLIENTS, 0);
            } catch (...) {
                // Last resort: minimal allocation
                try { client_reliable_bytes_this_frame.resize(32, 0); } catch (...) {}
            }
        } else {
            client_reliable_bytes_this_frame.resize(MAX_CLIENTS, 0);
        }
    }
};

static NetworkThrottle g_network_throttle;

// Reset per-frame counters
void G_ResetNetworkThrottle() {
    g_network_throttle.message_count_this_frame = 0;
    g_network_throttle.reliable_bytes_this_frame = 0;
    // Reset per-client counters
    std::fill(g_network_throttle.client_reliable_bytes_this_frame.begin(),
              g_network_throttle.client_reliable_bytes_this_frame.end(), 0);
}

// Check if we can send a network message for this entity
bool G_CanSendNetworkMessage(edict_t* ent, gtime_t throttle_time) {
    if (!ent) return false;

    // Safety check: if vectors failed to initialize, allow messages (fail open)
    if (g_network_throttle.last_update_time.empty()) {
        return true;  // Throttling disabled due to init failure
    }

    // Check global frame limit
    if (g_network_throttle.message_count_this_frame >= NetworkThrottle::MAX_MESSAGES_PER_FRAME) {
        return false;
    }

    // Check entity-specific throttling
    int ent_index = ent - g_edicts;
    if (ent_index < 0 || ent_index >= static_cast<int>(MAX_EDICTS)) return false;
    if (ent_index >= static_cast<int>(g_network_throttle.last_update_time.size())) return false;

    gtime_t& last_time = g_network_throttle.last_update_time[ent_index];
    if (level.time - last_time < throttle_time) {
        return false;
    }

    // Update state
    last_time = level.time;
    g_network_throttle.message_count_this_frame++;
    return true;
}

// Check if we can send reliable data (for HUD, configstrings, etc)
// client_num is optional - use -1 for non-client-specific messages
bool G_CanSendReliableData(int byte_size, int client_num = -1) {
    // Safety check: if vectors failed to initialize, allow messages (fail open)
    if (g_network_throttle.client_reliable_bytes_this_frame.empty()) {
        return true;  // Throttling disabled due to init failure
    }

    // Check global limit
    if (g_network_throttle.reliable_bytes_this_frame + byte_size >
        NetworkThrottle::MAX_RELIABLE_BYTES_PER_FRAME) {
        return false;
    }

    // Check per-client limit if applicable
    if (client_num >= 0 && client_num < MAX_CLIENTS) {
        if (client_num < static_cast<int>(g_network_throttle.client_reliable_bytes_this_frame.size())) {
            if (g_network_throttle.client_reliable_bytes_this_frame[client_num] + byte_size >
                NetworkThrottle::MAX_RELIABLE_BYTES_PER_CLIENT) {
                return false;
            }
            g_network_throttle.client_reliable_bytes_this_frame[client_num] += byte_size;
        }
    }

    g_network_throttle.reliable_bytes_this_frame += byte_size;
    return true;
}

// Optimized monster network update
void G_OptimizeMonsterNetworkUpdate(edict_t* monster) {
    if (!monster || !monster->inuse) return;
    if (!(monster->svflags & SVF_MONSTER)) return;

    // Store old position in monsterinfo if not already used
    vec3_t& old_origin = monster->monsterinfo.last_sighting;

    // Check if monster moved significantly
    vec3_t delta = monster->s.origin - old_origin;
    float dist_sq = delta.lengthSquared();

    // Only update if moved more than 16 units or has important effects
    const float MIN_MOVE_DIST_SQ = 16.0f * 16.0f;

    if (dist_sq < MIN_MOVE_DIST_SQ &&
        !(monster->s.effects & (EF_TELEPORTER | EF_FLAG1 | EF_FLAG2)) &&
        !(monster->flags & FL_GODMODE)) {  // Boss might have godmode

        // Skip this frame's network update by not relinking
        return;
    }

    // Update old position
    old_origin = monster->s.origin;

    // Force relink to send update
    gi.linkentity(monster);
}

// Batched message system for HUD updates
class HUDBatcher {
private:
    static constexpr size_t MAX_BATCH_SIZE = 1200;  // Safe under 1400 limit
    std::string batch_buffer;
    edict_t* target_client;
    bool needs_flush;

public:
    HUDBatcher() : target_client(nullptr), needs_flush(false) {
        // Pre-reserve to avoid reallocations
        batch_buffer.reserve(MAX_BATCH_SIZE);
    }

    void SetClient(edict_t* client) {
        if (target_client != client) {
            Flush();
            target_client = client;
        }
    }

    bool AddMessage(std::string_view msg) {
        // Check if we need to flush before adding
        if (batch_buffer.length() + msg.length() > MAX_BATCH_SIZE) {
            Flush();
        }

        // Safe append with size checking
        if (!safe_string_append(batch_buffer, msg, MAX_BATCH_SIZE)) {
            // Buffer full, try flushing and adding again
            Flush();
            if (!safe_string_append(batch_buffer, msg, MAX_BATCH_SIZE)) {
                return false;  // Message too large
            }
        }

        needs_flush = true;
        return true;
    }

    void Flush() {
        if (!target_client || !needs_flush) return;
        if (batch_buffer.empty()) {
            needs_flush = false;
            return;
        }

        // Get client number for per-client tracking
        int client_num = target_client - g_edicts - 1;
        if (client_num < 0 || client_num >= game.maxclients) {
            needs_flush = false;
            return;
        }

        // Check reliable buffer capacity before sending
        int msg_size = static_cast<int>(batch_buffer.length()) + 2;  // +2 for svc_layout byte + string terminator
        if (!G_CanSendReliableData(msg_size, client_num)) {
            // Can't send now, will retry next frame
            return;
        }

        gi.WriteByte(svc_layout);
        gi.WriteString(batch_buffer.c_str());
        // Use unreliable for HUD to reduce reliable-channel pressure
        gi.unicast(target_client, false);

        batch_buffer.clear();
        needs_flush = false;
    }

    void Reset() {
        batch_buffer.clear();
        target_client = nullptr;
        needs_flush = false;
    }
};

static HUDBatcher g_hud_batcher;

// Wrapper for tesla effect with throttling
bool G_SendTeslaEffect(edict_t* self, edict_t* target, vec3_t& start, vec3_t& end) {
    // Check throttling
    if (!G_CanSendNetworkMessage(self, 75_ms)) {
        return false;
    }

    // Send effect
    gi.WriteByte(svc_temp_entity);
    gi.WriteByte(TE_LIGHTNING);
    gi.WriteEntity(self);
    gi.WriteEntity(target);
    gi.WritePosition(start);
    gi.WritePosition(end);

    // Use unreliable for effects
    gi.multicast(start, MULTICAST_PVS, false);

    return true;
}

// Optimized scoreboard that limits data
void G_SendOptimizedScoreboard(edict_t* ent) {
    if (!ent || !ent->client) return;

    constexpr int MAX_SHOWN_PLAYERS = 8;  // Limit to top 8
    constexpr size_t MAX_SCOREBOARD_SIZE = 1024;
    std::string scoreboard;
    scoreboard.reserve(MAX_SCOREBOARD_SIZE);

    // Header
    if (!safe_string_append(scoreboard, "xv 0 yv 32 string2 \"Frags Player\" ", MAX_SCOREBOARD_SIZE)) {
        return;
    }

    // Sort clients by score
    struct PlayerScore {
        edict_t* ent;
        int score;
    };

    std::vector<PlayerScore> scores;
    if (!safe_reserve(scores, game.maxclients)) {
        return;
    }

    for (int i = 0; i < game.maxclients; i++) {
        edict_t* cl = &g_edicts[1 + i];
        if (!cl->inuse || !cl->client) continue;

        if (!safe_push_back(scores, PlayerScore{cl, cl->client->resp.score}, game.maxclients)) {
            break;
        }
    }

    // Sort by score
    std::sort(scores.begin(), scores.end(),
        [](const PlayerScore& a, const PlayerScore& b) {
            return a.score > b.score;
        });

    // Add top players
    int y = 50;
    for (size_t i = 0; i < scores.size() && i < MAX_SHOWN_PLAYERS; i++) {
        const PlayerScore& ps = scores[i];
        std::string_view line = G_Fmt("xv 0 yv {} string \"{}  {}\" ",
            y, ps.score, ps.ent->client->pers.netname);

        if (!safe_string_append(scoreboard, line, MAX_SCOREBOARD_SIZE)) {
            break;  // Scoreboard full
        }
        y += 10;
    }

    // Get client number for per-client tracking
    int client_num = ent - g_edicts - 1;
    if (client_num < 0 || client_num >= game.maxclients) {
        return;
    }

    // Check reliable buffer capacity before sending
    int msg_size = static_cast<int>(scoreboard.length()) + 2;
    if (!G_CanSendReliableData(msg_size, client_num)) {
        return;  // Can't send now, skip this frame
    }

    // Send
    gi.WriteByte(svc_layout);
    gi.WriteString(scoreboard.c_str());
    gi.unicast(ent, true);
}

// Hook into G_RunFrame to reset throttle counters
void G_NetworkThrottle_RunFrame() {
    G_ResetNetworkThrottle();
}

// Count active clients efficiently
int CountActiveClients() {
    int count = 0;
    for (int i = 0; i < game.maxclients; i++) {
        edict_t* ent = &g_edicts[1 + i];
        if (ent->inuse && ent->client && ent->client->pers.connected) {
            count++;
        }
    }
    return count;
}

// Reduce PlayFab metadata size
void G_GetMinimalRoomMetadata(char* buffer, size_t buffer_size) {
    // Only send essential data to prevent 403 errors
    // Use safe formatting instead of snprintf
    char temp_buffer[128];
    G_FmtTo(temp_buffer, "{{\"v\":1,\"w\":{},\"p\":{}}}",
        current_wave_level,
        std::min(CountActiveClients(), 8));  // Cap at 8 to reduce size

    Q_strlcpy(buffer, temp_buffer, buffer_size);
}