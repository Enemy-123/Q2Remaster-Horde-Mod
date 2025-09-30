#pragma once

#include "../g_local.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

namespace horde {

enum class AssetType {
    MODEL,
    SOUND,
    IMAGE
};

struct AssetInfo {
    std::string name;
    int32_t index;
    AssetType type;
    std::chrono::steady_clock::time_point last_used;
    uint32_t use_count;
    bool is_core_asset;  // Core assets are never unloaded
};

class AssetManager {
private:
    // Maps asset name to AssetInfo
    std::unordered_map<std::string, AssetInfo> m_models;
    std::unordered_map<std::string, AssetInfo> m_sounds;
    std::unordered_map<std::string, AssetInfo> m_images;

    // Track assets that need to be sent to connecting clients
    struct PendingClientLoad {
        edict_t* client;
        std::vector<std::pair<std::string, AssetType>> pending_assets;  // Store name+type, not pointers
        size_t current_batch;
        gtime_t next_batch_time;
        bool is_loading;
    };

    std::vector<PendingClientLoad> m_pending_clients;

    // Cached sorted asset list for faster client loading
    struct SortedAssetCache {
        std::vector<std::pair<std::string, AssetType>> sorted_list;
        size_t asset_count_snapshot;
        bool needs_rebuild;
    } m_sorted_cache;

    // Configuration
    static constexpr size_t BATCH_SIZE = 50;  // Assets per batch
    static constexpr int32_t BATCH_DELAY_MS = 100;  // Delay between batches
    static constexpr size_t MAX_UNUSED_TIME_SECONDS = 300;  // 5 minutes

    // Statistics
    struct {
        uint32_t total_models;
        uint32_t total_sounds;
        uint32_t total_images;
        uint32_t duplicate_prevented;
        uint32_t assets_cleaned;
    } m_stats;

    // Helper functions
    std::unordered_map<std::string, AssetInfo>* GetAssetMap(AssetType type);
    const std::unordered_map<std::string, AssetInfo>* GetAssetMap(AssetType type) const;
    bool IsAssetStillNeeded(const AssetInfo& info) const;
    void SendAssetBatch(PendingClientLoad& client_load);
    void RebuildSortedCache();
    void InvalidateSortedCache();

    // Template helper to reduce code duplication
    template<typename IndexFunc>
    int32_t RegisterAssetInternal(const char* name, bool is_core, AssetType type,
                                   std::unordered_map<std::string, AssetInfo>& map,
                                   uint32_t& stat_counter, size_t limit,
                                   const char* type_name, IndexFunc index_func);

public:
    AssetManager();
    ~AssetManager();

    // Singleton pattern
    static AssetManager& Get();

    // Asset registration with deduplication
    int32_t RegisterModel(const char* name, bool is_core = false);
    int32_t RegisterSound(const char* name, bool is_core = false);
    int32_t RegisterImage(const char* name, bool is_core = false);

    // Find existing asset index (returns 0 if not found)
    int32_t FindModel(const char* name) const;
    int32_t FindSound(const char* name) const;
    int32_t FindImage(const char* name) const;

    // Client connection handling
    void BeginClientLoading(edict_t* client);
    void ProcessClientLoading();  // Called each frame
    bool IsClientLoading(edict_t* client) const;
    void AbortClientLoading(edict_t* client);

    // Asset cleanup (called between waves)
    void CleanupUnusedAssets();
    void MarkAssetUsed(const char* name, AssetType type);

    // Mark core assets that should never be cleaned
    void MarkCoreAssets();

    // Debug and statistics
    void PrintStats() const;
    void DumpAssetList(AssetType type) const;
    size_t GetAssetCount(AssetType type) const;
    size_t GetTotalAssetCount() const;

    // Reset (for level changes)
    void Reset();
};

// Convenience macros for asset registration
#define REGISTER_MODEL(name) horde::AssetManager::Get().RegisterModel(name)
#define REGISTER_SOUND(name) horde::AssetManager::Get().RegisterSound(name)
#define REGISTER_IMAGE(name) horde::AssetManager::Get().RegisterImage(name)

// Cached asset indices for horde mode
struct HordeAssets {
    // Commonly used models
    cached_modelindex barrel_model;
    cached_modelindex tesla_model;
    cached_modelindex trap_model;
    cached_modelindex grenade_model;
    cached_modelindex rocket_model;
    cached_modelindex dopple_base_model;

    // Commonly used sounds
    cached_soundindex spawn_sound;
    cached_soundindex teleport_sound;
    cached_soundindex wave_complete_sound;
    cached_soundindex incoming_sound;
    cached_soundindex pickup_sound;

    // Initialize all cached assets
    void Init();
    void Reset();
};

extern HordeAssets g_horde_assets;

} // namespace horde