#include "../g_local.h"
#include "g_asset_manager.h"

// Helper functions to register assets through AssetManager
// These are called from cached_assetindex::assign() when in horde mode

int32_t HordeRegisterModel(const char* name) {
    return horde::AssetManager::Get().RegisterModel(name);
}

int32_t HordeRegisterSound(const char* name) {
    return horde::AssetManager::Get().RegisterSound(name);
}

int32_t HordeRegisterImage(const char* name) {
    return horde::AssetManager::Get().RegisterImage(name);
}

// Also override direct gi.modelindex/soundindex/imageindex calls when in horde mode
// This is for non-cached calls throughout the codebase
namespace {
    struct AssetInterceptor {
        AssetInterceptor() {
            // This runs on game initialization
            // We'll need to hook into the gi functions if possible
        }
    };

    // Static initialization ensures this runs early
    static AssetInterceptor interceptor;
}