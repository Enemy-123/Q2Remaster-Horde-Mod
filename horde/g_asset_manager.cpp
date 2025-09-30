#include "g_asset_manager.h"
#include "../g_local.h"
#include <algorithm>

namespace horde {

// Global instance
HordeAssets g_horde_assets;

AssetManager::AssetManager() {
    m_stats = {};
    m_sorted_cache.asset_count_snapshot = 0;
    m_sorted_cache.needs_rebuild = true;
}

AssetManager::~AssetManager() {
}

AssetManager& AssetManager::Get() {
    static AssetManager instance;  // Thread-safe in C++11+, no leak
    return instance;
}

std::unordered_map<std::string, AssetInfo>* AssetManager::GetAssetMap(AssetType type) {
    switch (type) {
        case AssetType::MODEL: return &m_models;
        case AssetType::SOUND: return &m_sounds;
        case AssetType::IMAGE: return &m_images;
        default: return nullptr;
    }
}

const std::unordered_map<std::string, AssetInfo>* AssetManager::GetAssetMap(AssetType type) const {
    switch (type) {
        case AssetType::MODEL: return &m_models;
        case AssetType::SOUND: return &m_sounds;
        case AssetType::IMAGE: return &m_images;
        default: return nullptr;
    }
}

template<typename IndexFunc>
int32_t AssetManager::RegisterAssetInternal(const char* name, bool is_core, AssetType type,
                                            std::unordered_map<std::string, AssetInfo>& map,
                                            uint32_t& stat_counter, size_t limit,
                                            const char* type_name, IndexFunc index_func) {
    if (!name || !*name) return 0;

    // Check if already registered using try_emplace (avoids duplicate lookup)
    auto [it, inserted] = map.try_emplace(name);

    if (!inserted) {
        // Already exists - update stats and return existing index
        it->second.use_count++;
        it->second.last_used = std::chrono::steady_clock::now();
        m_stats.duplicate_prevented++;
        return it->second.index;
    }

    // New asset - call the engine's index function
    int32_t index = index_func(name);

    // Initialize the new AssetInfo in-place
    AssetInfo& info = it->second;
    info.name = name;
    info.index = index;
    info.type = type;
    info.last_used = std::chrono::steady_clock::now();
    info.use_count = 1;
    info.is_core_asset = is_core;

    stat_counter++;
    InvalidateSortedCache();  // New asset added, cache needs rebuild

    // Check if we're approaching limits and warn
    if (map.size() > limit * 0.9) {
        gi.Com_PrintFmt("WARNING: Approaching {} limit ({}/{})\n",
                        type_name, map.size(), limit);
    }

    return index;
}

int32_t AssetManager::RegisterModel(const char* name, bool is_core) {
    return RegisterAssetInternal(name, is_core, AssetType::MODEL, m_models,
                                 m_stats.total_models, MAX_MODELS, "model",
                                 [](const char* n) { return gi.modelindex(n); });
}

int32_t AssetManager::RegisterSound(const char* name, bool is_core) {
    return RegisterAssetInternal(name, is_core, AssetType::SOUND, m_sounds,
                                 m_stats.total_sounds, MAX_SOUNDS, "sound",
                                 [](const char* n) { return gi.soundindex(n); });
}

int32_t AssetManager::RegisterImage(const char* name, bool is_core) {
    return RegisterAssetInternal(name, is_core, AssetType::IMAGE, m_images,
                                 m_stats.total_images, MAX_IMAGES, "image",
                                 [](const char* n) { return gi.imageindex(n); });
}

int32_t AssetManager::FindModel(const char* name) const {
    if (!name) return 0;
    auto it = m_models.find(name);
    return (it != m_models.end()) ? it->second.index : 0;
}

int32_t AssetManager::FindSound(const char* name) const {
    if (!name) return 0;
    auto it = m_sounds.find(name);
    return (it != m_sounds.end()) ? it->second.index : 0;
}

int32_t AssetManager::FindImage(const char* name) const {
    if (!name) return 0;
    auto it = m_images.find(name);
    return (it != m_images.end()) ? it->second.index : 0;
}

void AssetManager::InvalidateSortedCache() {
    m_sorted_cache.needs_rebuild = true;
}

void AssetManager::RebuildSortedCache() {
    m_sorted_cache.sorted_list.clear();

    // Pre-allocate to avoid reallocations
    size_t total_count = m_models.size() + m_sounds.size() + m_images.size();
    m_sorted_cache.sorted_list.reserve(total_count);

    // Collect all assets with their names and types
    for (const auto& [name, info] : m_models) {
        m_sorted_cache.sorted_list.emplace_back(name, AssetType::MODEL);
    }
    for (const auto& [name, info] : m_sounds) {
        m_sorted_cache.sorted_list.emplace_back(name, AssetType::SOUND);
    }
    for (const auto& [name, info] : m_images) {
        m_sorted_cache.sorted_list.emplace_back(name, AssetType::IMAGE);
    }

    // Sort by usage frequency (most used first)
    std::sort(m_sorted_cache.sorted_list.begin(), m_sorted_cache.sorted_list.end(),
        [this](const std::pair<std::string, AssetType>& a,
               const std::pair<std::string, AssetType>& b) {
            // Look up asset info
            const auto* map_a = GetAssetMap(a.second);
            const auto* map_b = GetAssetMap(b.second);

            auto it_a = map_a->find(a.first);
            auto it_b = map_b->find(b.first);

            if (it_a == map_a->end() || it_b == map_b->end()) return false;

            const AssetInfo& info_a = it_a->second;
            const AssetInfo& info_b = it_b->second;

            // Core assets first
            if (info_a.is_core_asset != info_b.is_core_asset) {
                return info_a.is_core_asset;
            }
            // Then by usage count
            return info_a.use_count > info_b.use_count;
        });

    m_sorted_cache.asset_count_snapshot = total_count;
    m_sorted_cache.needs_rebuild = false;
}

void AssetManager::BeginClientLoading(edict_t* client) {
    if (!client || !client->client) return;

    // Remove any existing pending load for this client
    AbortClientLoading(client);

    // Rebuild cache if needed
    if (m_sorted_cache.needs_rebuild) {
        RebuildSortedCache();
    }

    PendingClientLoad load;
    load.client = client;
    load.current_batch = 0;
    load.next_batch_time = level.time + 100_ms;  // Small initial delay
    load.is_loading = true;

    // Copy the pre-sorted asset list
    load.pending_assets = m_sorted_cache.sorted_list;

    m_pending_clients.push_back(load);

    // Send initial message to client
    gi.LocClient_Print(client, PRINT_CENTER, "Loading assets: {} total",
                       (int)load.pending_assets.size());
}

void AssetManager::ProcessClientLoading() {
    auto now = level.time;

    for (auto& load : m_pending_clients) {
        if (!load.is_loading || !load.client || !load.client->inuse) {
            load.is_loading = false;
            continue;
        }

        if (now >= load.next_batch_time) {
            SendAssetBatch(load);
        }
    }

    // Remove completed/aborted loads
    m_pending_clients.erase(
        std::remove_if(m_pending_clients.begin(), m_pending_clients.end(),
            [](const PendingClientLoad& load) {
                return !load.is_loading;
            }),
        m_pending_clients.end()
    );
}

void AssetManager::SendAssetBatch(PendingClientLoad& client_load) {
    if (!client_load.client || !client_load.client->inuse) {
        client_load.is_loading = false;
        return;
    }

    size_t start_idx = client_load.current_batch * BATCH_SIZE;
    size_t end_idx = std::min(start_idx + BATCH_SIZE, client_load.pending_assets.size());

    if (start_idx >= client_load.pending_assets.size()) {
        // All assets sent
        client_load.is_loading = false;
        gi.LocClient_Print(client_load.client, PRINT_CENTER, "Loading complete!");
        return;
    }

    // Note: The actual configstrings are already set from previous precaching.
    // This batching is purely for pacing the client's processing to prevent
    // overwhelming it with all assets at once, which would cause bad_alloc crashes.
    // The delay between batches gives the client time to process each chunk.

    // Update progress
    client_load.current_batch++;
    client_load.next_batch_time = level.time + gtime_t::from_ms(BATCH_DELAY_MS);

    // Show progress
    int progress = (int)((float)end_idx / client_load.pending_assets.size() * 100.0f);
    gi.LocClient_Print(client_load.client, PRINT_CENTER,
                       "Loading assets: {}%", progress);
}

bool AssetManager::IsClientLoading(edict_t* client) const {
    for (const auto& load : m_pending_clients) {
        if (load.client == client && load.is_loading) {
            return true;
        }
    }
    return false;
}

void AssetManager::AbortClientLoading(edict_t* client) {
    for (auto& load : m_pending_clients) {
        if (load.client == client) {
            load.is_loading = false;
        }
    }
}

bool AssetManager::IsAssetStillNeeded(const AssetInfo& info) const {
    // Core assets are always needed
    if (info.is_core_asset) return true;

    // Check if used recently
    auto now = std::chrono::steady_clock::now();
    auto time_since_use = std::chrono::duration_cast<std::chrono::seconds>(
        now - info.last_used).count();

    return time_since_use < MAX_UNUSED_TIME_SECONDS;
}

void AssetManager::CleanupUnusedAssets() {
    // Note: In Quake 2, we can't actually "unload" precached assets
    // But we can track what's unused for debugging and optimization
    uint32_t cleaned = 0;

    auto cleanup_map = [&cleaned, this](std::unordered_map<std::string, AssetInfo>& map) {
        for (auto it = map.begin(); it != map.end(); ) {
            if (!IsAssetStillNeeded(it->second)) {
                // Mark as cleaned (we can't actually remove from configstrings)
                cleaned++;
                it->second.use_count = 0;  // Reset use count
            }
            ++it;
        }
    };

    cleanup_map(m_models);
    cleanup_map(m_sounds);
    cleanup_map(m_images);

    if (cleaned > 0) {
        m_stats.assets_cleaned += cleaned;
        gi.Com_PrintFmt("Asset cleanup: marked {} assets as unused\n", cleaned);
    }
}

void AssetManager::MarkAssetUsed(const char* name, AssetType type) {
    if (!name) return;

    auto map = GetAssetMap(type);
    if (!map) return;

    auto it = map->find(name);
    if (it != map->end()) {
        it->second.last_used = std::chrono::steady_clock::now();
        it->second.use_count++;
    }
}

void AssetManager::MarkCoreAssets() {
    // Mark essential horde mode assets as core
    const char* core_models[] = {
        "models/objects/barrels/tris.md2",
        "models/weapons/g_tesla/tris.md2",
        "models/weapons/z_trap/tris.md2",
        "models/objects/grenade2/tris.md2",
        "models/objects/rocket/tris.md2",
        nullptr
    };

    const char* core_sounds[] = {
        "misc/tele_up.wav",
        "misc/r_tele3.wav",
        "medic_commander/monsterspawn1.wav",
        "misc/w_pkup.wav",
        "tank/thud.wav",
        nullptr
    };

    for (int i = 0; core_models[i]; i++) {
        auto it = m_models.find(core_models[i]);
        if (it != m_models.end()) {
            it->second.is_core_asset = true;
        }
    }

    for (int i = 0; core_sounds[i]; i++) {
        auto it = m_sounds.find(core_sounds[i]);
        if (it != m_sounds.end()) {
            it->second.is_core_asset = true;
        }
    }
}

void AssetManager::PrintStats() const {
    gi.Com_Print("=== Asset Manager Statistics ===\n");
    gi.Com_PrintFmt("Models: {} / {} (limit: {})\n",
                    m_models.size(), m_stats.total_models, MAX_MODELS);
    gi.Com_PrintFmt("Sounds: {} / {} (limit: {})\n",
                    m_sounds.size(), m_stats.total_sounds, MAX_SOUNDS);
    gi.Com_PrintFmt("Images: {} / {} (limit: {})\n",
                    m_images.size(), m_stats.total_images, MAX_IMAGES);
    gi.Com_PrintFmt("Duplicates prevented: {}\n", m_stats.duplicate_prevented);
    gi.Com_PrintFmt("Assets cleaned: {}\n", m_stats.assets_cleaned);
    gi.Com_PrintFmt("Clients loading: {}\n", m_pending_clients.size());
}

void AssetManager::DumpAssetList(AssetType type) const {
    const auto* map = GetAssetMap(type);
    if (!map) return;

    const char* type_name =
        (type == AssetType::MODEL) ? "Models" :
        (type == AssetType::SOUND) ? "Sounds" : "Images";

    gi.Com_PrintFmt("=== {} List ===\n", type_name);

    // Create sorted list for display
    std::vector<const AssetInfo*> sorted_assets;
    for (const auto& [name, info] : *map) {
        sorted_assets.push_back(&info);
    }

    std::sort(sorted_assets.begin(), sorted_assets.end(),
        [](const AssetInfo* a, const AssetInfo* b) {
            return a->use_count > b->use_count;
        });

    int count = 0;
    for (const auto* info : sorted_assets) {
        gi.Com_PrintFmt("[{}] {} (uses: {}, core: {})\n",
                        info->index, info->name.c_str(),
                        info->use_count, info->is_core_asset ? "Y" : "N");

        if (++count >= 50) {
            gi.Com_PrintFmt("... and {} more\n", sorted_assets.size() - 50);
            break;
        }
    }
}

size_t AssetManager::GetAssetCount(AssetType type) const {
    switch (type) {
        case AssetType::MODEL: return m_models.size();
        case AssetType::SOUND: return m_sounds.size();
        case AssetType::IMAGE: return m_images.size();
        default: return 0;
    }
}

size_t AssetManager::GetTotalAssetCount() const {
    return m_models.size() + m_sounds.size() + m_images.size();
}

void AssetManager::Reset() {
    m_models.clear();
    m_sounds.clear();
    m_images.clear();
    m_pending_clients.clear();
    m_sorted_cache.sorted_list.clear();
    m_sorted_cache.asset_count_snapshot = 0;
    m_sorted_cache.needs_rebuild = true;
    m_stats = {};
}

// HordeAssets implementation
void HordeAssets::Init() {
    // Initialize commonly used model indices
    barrel_model.assign("models/objects/barrels/tris.md2");
    tesla_model.assign("models/weapons/g_tesla/tris.md2");
    trap_model.assign("models/weapons/z_trap/tris.md2");
    grenade_model.assign("models/objects/grenade2/tris.md2");
    rocket_model.assign("models/objects/rocket/tris.md2");
    dopple_base_model.assign("models/objects/dopplebase/tris.md2");

    // Initialize commonly used sound indices
    spawn_sound.assign("medic_commander/monsterspawn1.wav");
    teleport_sound.assign("misc/tele_up.wav");
    wave_complete_sound.assign("misc/r_tele3.wav");
    incoming_sound.assign("misc/talk1.wav");
    pickup_sound.assign("misc/w_pkup.wav");

    // Mark these as core assets
    AssetManager::Get().MarkCoreAssets();
}

void HordeAssets::Reset() {
    barrel_model.clear();
    tesla_model.clear();
    trap_model.clear();
    grenade_model.clear();
    rocket_model.clear();
    dopple_base_model.clear();

    spawn_sound.clear();
    teleport_sound.clear();
    wave_complete_sound.clear();
    incoming_sound.clear();
    pickup_sound.clear();
}

} // namespace horde