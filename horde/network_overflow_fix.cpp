// Network Overflow Prevention System for Quake II Remaster Horde Mod
// This file contains fixes for the 999 ping and server message overflow issues

#include "g_local.h"
#include "g_horde.h"
#include "q_vec3.h"
#include <algorithm>
#include <vector>
#include <string>

// Global network throttling state
struct NetworkThrottle {
    gtime_t last_update_time[MAX_EDICTS];
    int message_count_this_frame;
    static constexpr int MAX_MESSAGES_PER_FRAME = 10;
    static constexpr gtime_t MIN_UPDATE_INTERVAL = 50_ms;
};

static NetworkThrottle g_network_throttle;

// Reset per-frame counters
void G_ResetNetworkThrottle() {
    g_network_throttle.message_count_this_frame = 0;
}

// Check if we can send a network message for this entity
bool G_CanSendNetworkMessage(edict_t* ent, gtime_t throttle_time = 100_ms) {
    if (!ent) return false;

    // Check global frame limit
    if (g_network_throttle.message_count_this_frame >= NetworkThrottle::MAX_MESSAGES_PER_FRAME) {
        return false;
    }

    // Check entity-specific throttling
    int ent_index = ent - g_edicts;
    if (ent_index < 0 || ent_index >= MAX_EDICTS) return false;

    gtime_t& last_time = g_network_throttle.last_update_time[ent_index];
    if (level.time - last_time < throttle_time) {
        return false;
    }

    // Update state
    last_time = level.time;
    g_network_throttle.message_count_this_frame++;
    return true;
}

// Optimized monster network update
void G_OptimizeMonsterNetworkUpdate(edict_t* monster) {
    if (!monster || !monster->inuse || !monster->client) return;

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
    static constexpr int MAX_BATCH_SIZE = 1200;  // Safe under 1400 limit
    std::string batch_buffer;
    edict_t* target_client;
    bool needs_flush;

public:
    HUDBatcher() : target_client(nullptr), needs_flush(false) {}

    void SetClient(edict_t* client) {
        if (target_client != client) {
            Flush();
            target_client = client;
        }
    }

    bool AddMessage(const std::string& msg) {
        if (batch_buffer.length() + msg.length() > MAX_BATCH_SIZE) {
            Flush();
        }
        batch_buffer += msg;
        needs_flush = true;
        return true;
    }

    void Flush() {
        if (!needs_flush || !target_client) return;

        gi.WriteByte(svc_layout);
        gi.WriteString(batch_buffer.c_str());
        gi.unicast(target_client, true);

        batch_buffer.clear();
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

    const int MAX_SHOWN_PLAYERS = 8;  // Limit to top 8
    std::string scoreboard;

    // Header
    scoreboard += "xv 0 yv 32 string2 \"Frags Player\" ";

    // Sort clients by score
    struct PlayerScore {
        edict_t* ent;
        int score;
    };

    std::vector<PlayerScore> scores;
    for (int i = 0; i < game.maxclients; i++) {
        edict_t* cl = &g_edicts[1 + i];
        if (!cl->inuse || !cl->client) continue;

        scores.push_back({cl, cl->client->resp.score});
    }

    // Sort by score
    std::sort(scores.begin(), scores.end(),
        [](const PlayerScore& a, const PlayerScore& b) {
            return a.score > b.score;
        });

    // Add top players
    int y = 50;
    for (size_t i = 0; i < scores.size() && i < MAX_SHOWN_PLAYERS; i++) {
        PlayerScore& ps = scores[i];
        scoreboard += G_Fmt("xv 0 yv {} string \"{}  {}\" ",
            y, ps.score, ps.ent->client->pers.netname);
        y += 10;
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
    snprintf(buffer, buffer_size,
        "{\"v\":1,\"w\":%d,\"p\":%d}",
        current_wave_level,
        std::min(CountActiveClients(), 8));  // Cap at 8 to reduce size
}