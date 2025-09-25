// Network Overflow Prevention System - Header
#pragma once

#include "g_local.h"

// Core throttling functions
void G_ResetNetworkThrottle();
bool G_CanSendNetworkMessage(edict_t* ent, gtime_t throttle_time = 100_ms);

// Monster optimization
void G_OptimizeMonsterNetworkUpdate(edict_t* monster);

// Tesla effect wrapper with throttling
bool G_SendTeslaEffect(edict_t* self, edict_t* target, vec3_t& start, vec3_t& end);

// Optimized HUD/scoreboard
void G_SendOptimizedScoreboard(edict_t* ent);

// Frame hook
void G_NetworkThrottle_RunFrame();

// PlayFab helpers
void G_GetMinimalRoomMetadata(char* buffer, size_t buffer_size);
int CountActiveClients();

// Message size tracking
class MessageSizeTracker {
private:
    int current_size;
    static constexpr int MAX_SAFE_SIZE = 1200;  // Leave buffer under 1400

public:
    MessageSizeTracker() : current_size(0) {}

    void Reset() { current_size = 0; }

    bool CanAdd(int bytes) const {
        return (current_size + bytes) < MAX_SAFE_SIZE;
    }

    bool Add(int bytes) {
        if (!CanAdd(bytes)) {
            gi.Com_Print("Warning: Message size limit reached, skipping data\n");
            return false;
        }
        current_size += bytes;
        return true;
    }

    int GetCurrentSize() const { return current_size; }
    int GetRemainingSpace() const { return MAX_SAFE_SIZE - current_size; }
};