#include "shared.h"
#include "memory_safety.h"
#include "horde/g_horde.h"
#include "g_config.h"
#include <unordered_map>
#include <algorithm>
#include <span>
#include "horde/horde_ids.h"
#include "horde/g_entity_properties.h"
#include "horde/g_horde_phys.h"
#include "horde/horde_constants.h"
#include <optional>
#include <vector>
#include <string>
#include <string_view>
#include <array>
#include <bit>
#include <boost/container/small_vector.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

// Constants for various game mechanics
namespace {
	// Spawn point selection
	constexpr size_t MAX_SPAWN_ATTEMPTS = 16;
	constexpr size_t STACK_ENTITY_CAPACITY = 32;
	constexpr size_t MAX_PUSHABLE_ENTITIES = 64;

	// Monster bonuses
	constexpr float BONUS_POWER_ARMOR_RATIO = 0.4f;
	constexpr float BONUS_GIB_HEALTH_MULT = 2.8f;
	constexpr int   BONUS_GIB_HEALTH_MIN = -200;

	// Teleportation
	constexpr int   TELEPORT_PM_TIME = 160;
	constexpr float TELEPORT_POSITION_SCALE = 8.0f;
	constexpr float TELEPORT_Z_OFFSET = 10.0f;

	// Entity spawning
	constexpr float SPAWN_CLEAR_SAFE_OFFSET = 26.0f;
	constexpr float MAX_ENTITY_REACH = 128.0f;

	// Boss effects
	constexpr int   BOSS_EFFECT_MIN_INDEX = 1;
	constexpr int   BOSS_EFFECT_MAX_INDEX = 6;
	constexpr int   BOSS_SECONDARY_EFFECTS = 12;
	constexpr float BOSS_EFFECT_SCALE = 0.55f;
	constexpr float BOSS_RANDOM_OFFSET_RANGE = 255.0f;
	constexpr float BOSS_EARTHQUAKE_SPEED = 500.0f;
}

// Global flag to control visual effect when removing deployables
thread_local bool g_use_quiet_deployable_removal = false;

void RemoveEntity(edict_t* ent);

// O(1) swap-and-pop removal from g_targetable_special_entities
// Replaces O(N) erase-remove idiom for better performance
void RemoveEntityFromGlobalList(edict_t* entity) {
	auto& vec = g_targetable_special_entities;
	auto it = std::find(vec.begin(), vec.end(), entity);
	if (it != vec.end()) {
		// Swap with last element and pop (O(1) removal)
		// Order doesn't matter for targeting, so this is safe
		*it = vec.back();
		vec.pop_back();
	}
}

// 1:  spawn point selection using pre-shuffled global list
// Excludes style=1 spawn points (aerial spawns for flying monsters)
[[nodiscard]] static edict_t* SelectRandomClearSpawnPoint() {
	if (g_num_spawn_points == 0) {
		return nullptr;
	}

	// Start from a random index to avoid spawn clustering in multiplayer
	// Previous thread_local approach was broken - it shared state across all players
	const size_t start_index = static_cast<size_t>(irandom(g_num_spawn_points));
	const size_t max_attempts = std::min(MAX_SPAWN_ATTEMPTS, g_num_spawn_points);

	// First pass: look for clear, non-aerial spawn points
	for (size_t i = 0; i < max_attempts; ++i) {
		size_t check_index = (start_index + i) % g_num_spawn_points;
		edict_t* spot = g_spawn_point_list[check_index];

		// Exclude style=1 spawn points (aerial spawns for flying monsters)
		if (spot && spot->inuse && spot->style != 1 && !IsSpawnPointOccupied(spot)) {
			return spot;
		}
	}

	// Fallback: find any non-style=1 spawn point (even if occupied)
	for (size_t i = 0; i < g_num_spawn_points; ++i) {
		size_t check_index = (start_index + i) % g_num_spawn_points;
		edict_t* spot = g_spawn_point_list[check_index];

		if (spot && spot->inuse && spot->style != 1) {
			return spot;
		}
	}

	// Last resort: return any spawn point (should rarely happen)
	size_t fallback_index = static_cast<size_t>(irandom(g_num_spawn_points));
	return g_spawn_point_list[fallback_index];
}

// 2: Compile-time lookup table for bonus effects
struct BonusEffectData {
	effects_t effects = EF_NONE;
	renderfx_t renderfx = RF_NONE;
	float alpha = 1.0f;
	float health_mult = 1.0f;
	float power_armor_mult = 1.0f;
	gtime_t double_time_add = 0_sec;
	gtime_t quad_time_add = 0_sec;
	gtime_t invincible_time_add = 0_sec;
	monster_attack_state_t attack_state = AS_NONE;

	float scale_small = 1.0f;
	float scale_medium = 1.0f;
	float scale_large = 1.0f;
	float health_mult_small = 1.0f;
	float health_mult_medium = 1.0f;
	float health_mult_large = 1.0f;
	float pa_mult_small = 1.0f;
	float pa_mult_medium = 1.0f;
	float pa_mult_large = 1.0f;
};

static constexpr std::array<BonusEffectData, 7> g_bonus_effects_array = {{
	{},
	{
		.effects = EF_ROCKET | EF_FIREBALL, .renderfx = RF_SHELL_RED,
		.double_time_add = 475_sec,
		.scale_small = 1.1f, .scale_medium = 1.25f, .scale_large = 1.5f,
		.health_mult_small = 1.13f, .health_mult_medium = 1.1f, .health_mult_large = 1.35f,
		.pa_mult_small = 1.1f, .pa_mult_medium = 1.08f, .pa_mult_large = 1.05f
	},
	{
		.effects = EF_PLASMA | EF_TAGTRAIL,
		.scale_small = 1.1f, .scale_medium = 1.2f, .scale_large = 1.2f,
		.health_mult_small = 1.24f, .health_mult_medium = 1.02f, .health_mult_large = 1.4f,
		.pa_mult_small = 1.04f, .pa_mult_medium = 1.15f, .pa_mult_large = 1.2f
	},
	{
		.effects = EF_BLUEHYPERBLASTER, .alpha = 0.6f,
		.power_armor_mult = 1.2f, .invincible_time_add = 12_sec
	},
	{
		.alpha = 0.3f,
		.health_mult = 1.1f, .power_armor_mult = 1.2f
	},
	{
		.effects = EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE, .alpha = 0.6f,
		.health_mult = 1.2f, .power_armor_mult = 1.35f
	},
	{
		.effects = EF_TRACKER | EF_FLAG1,
		.scale_small = 1.1f, .scale_medium = 1.3f, .scale_large = 1.2f,
		.health_mult_small = 1.2f, .health_mult_medium = 1.5f, .health_mult_large = 1.5f,
		.pa_mult_small = 1.1f, .pa_mult_medium = 1.1f, .pa_mult_large = 1.1f
	}
}};

// Optimized with bit manipulation for O(1) lookup
constexpr size_t GetBonusEffectIndex(bonus_flags_t flags) {
	if (flags == BF_NONE) return 0;

	// Use countr_zero to find the position of the first set bit
	// This is much faster than an if-chain
	const unsigned int bit_pos = std::countr_zero(static_cast<unsigned int>(flags));

	// Map bit positions to effect indices
	// BF_CHAMPION (bit 0) -> index 1, BF_CORRUPTED (bit 1) -> index 2, etc.
	return (bit_pos < 6) ? bit_pos + 1 : 0;
}

// Map size cache - accessible from both GetMapSize and InvalidateMapSizeCache
namespace {
    std::array<std::optional<horde::MapSize>,
               static_cast<size_t>(horde::MapID::MAX_MAPS)> g_mapsize_cache;
    char g_last_map_for_cache[MAX_QPATH] = "";
}

// 3: Enhanced map size cache with perfect hash
[[nodiscard]] horde::MapSize GetMapSize(const char* mapname) noexcept {
    // If the map has changed, clear cache
    // Use case-insensitive comparison to handle "q2dm1" vs "Q2DM1"
    if (Q_strcasecmp(g_last_map_for_cache, level.mapname) != 0) {
        g_mapsize_cache.fill(std::nullopt);
        Q_strlcpy(g_last_map_for_cache, level.mapname, sizeof(g_last_map_for_cache));
    }

    const horde::MapID mapId = horde::MapOriginRegistry::GetMapID(mapname);
    if (mapId == horde::MapID::UNKNOWN) {
        return {}; 
    }

    const size_t index = static_cast<size_t>(mapId);

    if (g_mapsize_cache[index].has_value()) {
        return g_mapsize_cache[index].value();
    }

    // Check for config override first
    horde::MapSize size;
    if (index < g_config.maps.map_overrides.size())
    {
        const MapOverrideConfig& override_config = g_config.maps.map_overrides[index];
        if (override_config.has_size_override)
        {
            // Convert from bool fields to MapSize struct
            size.isSmallMap = override_config.size_override_is_small;
            size.isBigMap = override_config.size_override_is_big;
            size.isMediumMap = override_config.size_override_is_medium;
            g_mapsize_cache[index] = size;
            return size;
        }
    }

    // No override, use hardcoded map size from registry
    size = horde::MapOriginRegistry::GetMapSize(mapId);
    g_mapsize_cache[index] = size;
    return size;
}

// Invalidate the GetMapSize cache - call when config is reloaded
void InvalidateMapSizeCache() noexcept {
    g_mapsize_cache.fill(std::nullopt);
}

// Returns the map size to use for BOSS POOL selection, which may differ from the
// gameplay map size (GetMapSize) when a map sets a boss_size override. This lets a
// map keep, e.g., medium monster spawning while drawing bosses from the small pool.
// out_isOverride is set true when an explicit boss_size override is active, signalling
// the caller to hard-restrict the candidate pool to this size's list.
[[nodiscard]] horde::MapSize GetBossMapSize(const char* mapname, bool& out_isOverride) noexcept {
    out_isOverride = false;

    const horde::MapID mapId = horde::MapOriginRegistry::GetMapID(mapname);
    if (mapId != horde::MapID::UNKNOWN) {
        const size_t index = static_cast<size_t>(mapId);
        if (index < g_config.maps.map_overrides.size()) {
            const MapOverrideConfig& override_config = g_config.maps.map_overrides[index];
            if (override_config.has_boss_size_override) {
                horde::MapSize size;
                size.isSmallMap = override_config.boss_size_override_is_small;
                size.isBigMap = override_config.boss_size_override_is_big;
                size.isMediumMap = override_config.boss_size_override_is_medium;
                out_isOverride = true;
                return size;
            }
        }
    }

    // No boss-size override: bosses follow the regular gameplay map size.
    return GetMapSize(mapname);
}

// 4: Batch entity removal with memory pooling
void RemovePlayerOwnedEntities(edict_t* player) {
    if (!player || !player->client) {
        return;
    }

    boost::container::small_vector<edict_t*, STACK_ENTITY_CAPACITY> entities_array;

    auto add_entity = [&](edict_t* ent) {
        if (!ent || !ent->inuse) {
            return;
        }

        if (entities_array.size() >= MAX_SAFE_CONTAINER_SIZE) {
            if (developer && developer->integer) {
                gi.Com_PrintFmt("ERROR: Entity collection overflow ({}), cannot add more\n", entities_array.size());
            }
            return;
        }

        if (!safe_push_back(entities_array, ent)) {
            gi.Com_Print("ERROR: Failed to allocate memory for entity collection\n");
        }
    };

    const auto& client = *player->client;

    std::span lasers{client.resp.deployed_lasers};
    for (edict_t* laser : lasers) add_entity(laser);

    std::span sentries{client.resp.deployed_sentries};
    for (edict_t* sentry : sentries) add_entity(sentry);

    std::span teslas{client.resp.deployed_teslas};
    for (edict_t* tesla : teslas) add_entity(tesla);

    std::span traps{client.resp.deployed_traps};
    for (edict_t* trap : traps) add_entity(trap);

    std::span proxs{client.resp.deployed_proxs};
    for (edict_t* prox : proxs) add_entity(prox);

    // Remove summoned Strogg monsters
    RemoveSummonedEntities(player);

    // Find and add all summoned Strogg bases owned by this player from the special entities list
    for (edict_t* special_ent : g_targetable_special_entities) {
        if (special_ent && special_ent->inuse &&
            special_ent->special_type_id == static_cast<uint8_t>(horde::SpecialEntityTypeID::STROGG_SUMMONER) &&
            special_ent->teammaster == player) {
            add_entity(special_ent);
        }
    }

    for (edict_t* ent : entities_array) {
        if (ent && ent->inuse) {
            RemoveEntity(ent);
        }
    }
}

// 5:  defense check with compile-time lookup
[[nodiscard]] bool IsPlayerDefense(const edict_t* ent) {
    if (!ent) return false;

    auto id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);

    // UNKNOWN (255) is the default G_Spawn() gives every non-special entity - benign, not corrupt.
    // Check it BEFORE the range check below (255 >= COUNT would otherwise swallow this normal case).
    if (id == horde::SpecialEntityTypeID::UNKNOWN) return false;

    // Any OTHER out-of-range value (COUNT..254) is genuinely corrupt - reject to prevent UB.
    if (ent->special_type_id >= static_cast<uint8_t>(horde::SpecialEntityTypeID::COUNT)) {
        return false;
    }

    static constexpr uint64_t DEFENSE_MASK =
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::SENTRY_GUN)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::TURRET)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::LASER_EMITTER)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::TESLA_MINE)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::FOOD_CUBE_TRAP)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::STROGG_SUMMONER));

    return (DEFENSE_MASK & (1ULL << static_cast<uint64_t>(id))) != 0;
}

// 6:  removable entity check
[[nodiscard]] bool IsRemovableEntity(const edict_t* ent) {
    if (!ent) return false;

    auto id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);

    // UNKNOWN (255) is the default G_Spawn() gives EVERY non-special entity - it is a benign "not a
    // special entity" sentinel, NOT a corrupt value. Check it BEFORE the range check below: 255 >= COUNT
    // is true, so checking the range first would misreport this normal state as an invalid id. Optionally
    // warn (once per classname per map) when a classname that SHOULD be special forgot to set its id.
    if (id == horde::SpecialEntityTypeID::UNKNOWN) {
        if (developer && developer->integer) {
            thread_local boost::unordered::unordered_flat_set<std::string_view> reported_classnames;
            thread_local char last_map_for_reporting[MAX_QPATH] = "";

            if (Q_strcasecmp(last_map_for_reporting, level.mapname) != 0) {
                reported_classnames.clear();
                Q_strlcpy(last_map_for_reporting, level.mapname, sizeof(last_map_for_reporting));
            }

            if (ent->classname && horde::SpecialTypeRegistry::GetTypeID(ent->classname) != horde::SpecialEntityTypeID::UNKNOWN) {
                if (reported_classnames.find(ent->classname) == reported_classnames.end()) {
                    gi.Com_PrintFmt("VALIDATION WARNING: Entity with classname '{}' is missing its special_type_id. Please set it in its spawn function.\n", ent->classname);
                    reported_classnames.insert(ent->classname);
                }
            }
        }
        return false;
    }

    // Any OTHER out-of-range value (COUNT..254) really is corrupt - keep the error to catch UB.
    if (ent->special_type_id >= static_cast<uint8_t>(horde::SpecialEntityTypeID::COUNT)) {
        if (developer && developer->integer) {
            gi.Com_PrintFmt("ERROR: Entity has invalid special_type_id {} (classname: {})\n",
                ent->special_type_id, ent->classname ? ent->classname : "NULL");
        }
        return false;
    }

    static constexpr uint64_t REMOVABLE_MASK =
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::TESLA_MINE)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::FOOD_CUBE_TRAP)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::LASER_EMITTER)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::DOPPLEGANGER)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::PROX_MINE)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::SENTRY_GUN)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::TURRET)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::NUKE_MINE)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::STROGG_SUMMONER)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::BARREL));

    return (REMOVABLE_MASK & (1ULL << static_cast<uint64_t>(id))) != 0;
}

void RemoveEntity(edict_t* ent) {
    if (!ent || !ent->inuse) return;

    // Check for special entity die handlers FIRST (before summoned monster check)
    // This ensures deployables like sentries use their proper die handlers
    auto id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);
    if (id != horde::SpecialEntityTypeID::UNKNOWN) {
        EntityDieHandler handler = GetDieHandler(id);
        if (handler) {
            // Call the die handler - it will check g_use_quiet_deployable_removal flag
            handler(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
            return;
        }
    }

    // Special handling for summoned monsters (non-deployable)
    if ((ent->svflags & SVF_MONSTER) && ent->monsterinfo.isfriendlyspawn) {
        // Update player tracking for summoned monsters
        RemoveSummonFromPlayerArray(ent);

        // Clean up reference from the base entity if it exists
        if (ent->chain && ent->chain->inuse && ent->chain->teamchain == ent) {
            ent->chain->teamchain = nullptr;
        }
        BecomeTE(ent);
        return;
    }

    BecomeTE(ent);
}

// Cut the remaining time on an expiry field by a random 80-95% (keeps 5-20%).
static void BossReduceRemainingTime(gtime_t& expiry) {
	const gtime_t remaining = expiry - level.time;
	if (remaining <= 0_ms) return;
	expiry = level.time + random_time(remaining * 0.05f, remaining * 0.20f);
}

// Boss "shockwave": hurt a deployable/summon for ~75% of current health instead of deleting it.
void WeakenEntityForBoss(edict_t* ent) {
	if (!ent || !ent->inuse) return;

	const auto id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);

	// Lasers relay through the emitter; the meaningful health is on the beam (emitter->chain).
	edict_t* health_target = ent;
	if (id == horde::SpecialEntityTypeID::LASER_EMITTER && ent->chain && ent->chain->inuse)
		health_target = ent->chain;

	if (health_target->health > 1) {
		// Lasers carry much more health now (base 150, +100/wave), so the boss takes a
		// bigger bite (leave ~1/6) to keep the post-shockwave value in line with the old
		// 1/4 of weaker lasers. Other deployables (sentry/turret) stay at the old ~1/4.
		const bool is_laser = (id == horde::SpecialEntityTypeID::LASER_EMITTER);
		const int divisor = is_laser ? 6 : 4;
		health_target->health = std::max(1, health_target->health / divisor); // never 0
	}

	// Tesla: cut the deployed "expires -> explodes" timer (air_finished) down to 3-8 seconds.
	// air_finished is the real expiry (tesla_think_active); the ID-view HUD reads it too.
	// Apply even while the tesla is still arming (air_finished not set yet, <= level.time).
	// FL_BOSS_SHORTENED stops tesla_activate and the adrenaline refresh (UpdatePlayerTeslaMines)
	// from restoring the full lifetime. Never extend a tesla that already has less time left.
	if (id == horde::SpecialEntityTypeID::TESLA_MINE) {
		const gtime_t new_expiry = level.time + random_time(3_sec, 8_sec);
		if (ent->air_finished <= level.time || ent->air_finished > new_expiry) {
			ent->air_finished = new_expiry;
			ent->wait = new_expiry.seconds();
			ent->flags |= FL_BOSS_SHORTENED;
		}
	}
	// Trap: slash remaining lifetime by 80-95%.
	else if (id == horde::SpecialEntityTypeID::FOOD_CUBE_TRAP)
		BossReduceRemainingTime(ent->timestamp);        // trap expiry field
}

// 7:  power-up time updates
void UpdatePowerUpTimes(edict_t* monster) noexcept {
	if (!monster) return;

	const gtime_t current_time = level.time;
	const bool quad_expired = monster->monsterinfo.quad_time <= current_time;
	const bool double_expired = monster->monsterinfo.double_time <= current_time;

	if (quad_expired && double_expired) {
		monster->monsterinfo.damage_modifier_applied = false;
	}
}

// 8: Simplified damage modifier calculation
float M_DamageModifier(edict_t* monster) noexcept {
	if (!monster) return 1.0f;

	const gtime_t current_time = level.time;
	
	if (horde::IsMonsterType(monster, horde::MonsterTypeID::SENTRYGUN)) {
		if (monster->monsterinfo.quad_time > current_time) return 2.0f;
		if (monster->monsterinfo.double_time > current_time) return 1.5f;
		return 1.0f;
	}

	if (monster->monsterinfo.quad_time > current_time) return 3.0f;
	if (monster->monsterinfo.double_time > current_time) return 2.0f;
	return 1.0f;
}

// 9:  title generation with compile-time strings
// Returns string to be used with GetDisplayName's internal buffer
std::string_view GetTitleFromFlags(bonus_flags_t bonus_flags) noexcept {
	static constexpr struct {
		bonus_flags_t flags;
		std::string_view title;
	} common_titles[] = {
		{BF_CHAMPION, "Champion "},
		{BF_CORRUPTED, "Corrupted "},
		{BF_RAGEQUITTER, "Ragequitter "},
		{BF_GHOSTLY, "Ghostly "},
		{BF_POSSESSED, "Possessed "},
		{BF_STYGIAN, "Stygian "},
		{BF_FRIENDLY, "Friendly "},
		{BF_CHAMPION | BF_CORRUPTED, "Champion Corrupted "},
	};

	if (bonus_flags == BF_NONE) {
		return "";
	}

	for (const auto& entry : common_titles) {
		if (entry.flags == bonus_flags) {
			return entry.title;
		}
	}

	// For complex multi-flag combinations, we need to build dynamically
	// Allocate space for up to 7 flags (max realistic combination)
	static thread_local std::string title_builder;
	title_builder.clear();
	title_builder.reserve(64);

	if (bonus_flags & BF_CHAMPION)    title_builder += "Champion ";
	if (bonus_flags & BF_CORRUPTED)   title_builder += "Corrupted ";
	if (bonus_flags & BF_RAGEQUITTER) title_builder += "Ragequitter ";
	if (bonus_flags & BF_GHOSTLY)     title_builder += "Ghostly ";
	if (bonus_flags & BF_POSSESSED)   title_builder += "Possessed ";
	if (bonus_flags & BF_STYGIAN)     title_builder += "Stygian ";
	if (bonus_flags & BF_FRIENDLY)    title_builder += "Friendly ";

	return title_builder;
}

// 10: Pre-computed display names with perfect hash
static std::array<std::string, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_monsterDisplayNames;
static std::array<std::string, static_cast<size_t>(horde::SpecialEntityTypeID::COUNT)> g_specialDisplayNames;
static bool g_displayNamesInitialized = false;

char* FormatClassname(const char* classname, char* out, const char* end);
static std::string_view GetMonsterNameReplacement(horde::MonsterTypeID typeId) noexcept;

void InitializeDisplayNames() {
    if (g_displayNamesInitialized) return;

    constexpr size_t monster_count = static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES);
    constexpr size_t special_count = static_cast<size_t>(horde::SpecialEntityTypeID::COUNT);
    
    for (size_t i = 0; i < monster_count; ++i) {
        g_monsterDisplayNames[i].reserve(32);
    }
    for (size_t i = 0; i < special_count; ++i) {
        g_specialDisplayNames[i].reserve(24);
    }

    for (size_t i = 0; i < monster_count; ++i) {
        auto typeId = static_cast<horde::MonsterTypeID>(i);
        
        const std::string_view replacement = GetMonsterNameReplacement(typeId);
        if (!replacement.empty()) {
            g_monsterDisplayNames[i] = std::string(replacement);
        } 
        else if (horde::MonsterTypeRegistry::IsValidType(typeId)) {
            const char* classname = horde::MonsterTypeRegistry::GetClassname(typeId);
            char buffer[64];
            char* end = FormatClassname(classname, buffer, buffer + sizeof(buffer));
            g_monsterDisplayNames[i].assign(buffer, end);
        } else {
            g_monsterDisplayNames[i] = "Unknown Monster";
        }
    }

    static constexpr std::pair<horde::SpecialEntityTypeID, const char*> special_mappings[] = {
        {horde::SpecialEntityTypeID::TESLA_MINE, "Tesla Mine"},
        {horde::SpecialEntityTypeID::FOOD_CUBE_TRAP, "Stroggonoff Maker"},
        {horde::SpecialEntityTypeID::TURRET, "Turret"},
        {horde::SpecialEntityTypeID::SENTRY_GUN, "Sentry Gun"},
        {horde::SpecialEntityTypeID::LASER_EMITTER, "Laser Emitter"},
        {horde::SpecialEntityTypeID::DOPPLEGANGER, "Doppleganger"},
        {horde::SpecialEntityTypeID::BARREL, "Explosive Barrel"},
    };
    
    for (size_t i = 0; i < special_count; ++i) {
        g_specialDisplayNames[i] = "Unknown Object";
    }
    
    for (const auto& [typeId, name] : special_mappings) {
        g_specialDisplayNames[static_cast<size_t>(typeId)] = name;
    }

    g_displayNamesInitialized = true;
}

// 11:  display name generation with proper buffer management
// Fixed: No longer has re-entrancy bug from nested static buffer calls
const char* GetDisplayName(const edict_t* ent) {
    constexpr size_t NUM_BUFFERS = 8;
    thread_local char display_name_buffers[NUM_BUFFERS][128];
    thread_local size_t buffer_index = 0;

    buffer_index = (buffer_index + 1) % NUM_BUFFERS;
    char* const current_buffer = display_name_buffers[buffer_index];

    if (!ent) {
        Q_strlcpy(current_buffer, "Unknown", sizeof(display_name_buffers[0]));
        return current_buffer;
    }

    if (!g_displayNamesInitialized) {
        InitializeDisplayNames();
    }

    const char* base_name_ptr = nullptr;
    auto special_id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);
    auto monster_id = static_cast<horde::MonsterTypeID>(ent->monsterinfo.monster_type_id);

    if (special_id != horde::SpecialEntityTypeID::UNKNOWN &&
        static_cast<size_t>(special_id) < g_specialDisplayNames.size()) {
        base_name_ptr = g_specialDisplayNames[static_cast<size_t>(special_id)].c_str();
    } else if ((ent->svflags & SVF_MONSTER) &&
               monster_id != horde::MonsterTypeID::UNKNOWN &&
               static_cast<size_t>(monster_id) < g_monsterDisplayNames.size()) {
        base_name_ptr = g_monsterDisplayNames[static_cast<size_t>(monster_id)].c_str();
    } else {
        base_name_ptr = ent->classname ? ent->classname : "Unknown";
    }

    // CRITICAL FIX: Capture title as string_view BEFORE calling G_Fmt
    // This prevents re-entrancy bug where G_Fmt might call back into GetTitleFromFlags
    std::string_view title = GetTitleFromFlags(ent->monsterinfo.bonus_flags);

    // Now safe to format - title is already captured and won't be overwritten
    auto result = G_Fmt("{}{}", title, base_name_ptr);
    size_t copy_len = std::min(result.size(), sizeof(display_name_buffers[0]) - 1);
    memcpy(current_buffer, result.data(), copy_len);
    current_buffer[copy_len] = '\0';

    return current_buffer;
}

// Get monster display name from MonsterTypeID (for projectiles)
const char* GetMonsterDisplayNameFromTypeID(horde::MonsterTypeID type_id) {
    if (!g_displayNamesInitialized) {
        InitializeDisplayNames();
    }

    if (type_id != horde::MonsterTypeID::UNKNOWN &&
        static_cast<size_t>(type_id) < g_monsterDisplayNames.size()) {
        return g_monsterDisplayNames[static_cast<size_t>(type_id)].c_str();
    }

    return "Unknown Monster";
}

// // Calculate sentry gun health with adrenaline bonus
[[nodiscard]] int CalculateSentryHealth(int base_health, gclient_t* owner_client)
{
	if (!owner_client) return base_health;
	return base_health + (owner_client->pers.adrenaline_count * 10);
}

// Calculate tesla/trap lifetime with adrenaline bonus
[[nodiscard]] gtime_t CalculateDeployableLifetime(gtime_t base_lifetime, gclient_t* owner_client)
{
	if (!owner_client) return base_lifetime;
	int adrenaline_bonus_seconds = owner_client->pers.adrenaline_count * 3;
	return base_lifetime + gtime_t::from_sec(adrenaline_bonus_seconds);
}

// Forward declaration
void ApplyPvMLevelScaling(edict_t* monster);

// 12:  bonus flag application
void ApplyMonsterBonusFlags(edict_t* monster)
{
	if (!monster || !monster->inuse || monster->monsterinfo.IS_BOSS)
		return;

	const spawn_temp_t& st = ED_GetSpawnTemp();

	// Apply level-based scaling (PvM leveling system with level-gap difficulty scaling)
	// Skip this for friendly spawns - they already have their pvm_level set based on the summoner's skill level
	if (!monster->monsterinfo.isfriendlyspawn)
	{
		// When there's a level gap, spawn harder monsters prioritizing lower-level ones for accessibility
		if (g_lowest_player_level < g_highest_player_level)
		{
			// Calculate the level gap between highest and lowest players
			int32_t level_gap = g_highest_player_level - g_lowest_player_level;

			// If gap is significant (>= 3 levels), increase difficulty by spawning more varied-level monsters
			// This results in ~30% harder spawns overall while still prioritizing lower levels
			float higher_level_chance = (level_gap >= 3) ? 0.50f : 0.20f;

			if (frandom() < higher_level_chance)
			{
				// Random level between lowest and highest (inclusive)
				// Prioritizes lower levels to help struggling players
				monster->monsterinfo.pvm_level = irandom(g_lowest_player_level, g_highest_player_level + 1);
			}
			else
			{
				// Use lowest player level to maintain accessibility
				monster->monsterinfo.pvm_level = g_lowest_player_level;
			}
		}
		else
		{
			// All players are same level, use that level
			monster->monsterinfo.pvm_level = g_lowest_player_level;
		}
	}

	// PERFORMANCE OPTIMIZATION: Cache config lookups to avoid hash map lookups in hot path
	// Cache MonsterStatsConfig pointer (eliminates GetMonsterConfig() calls)
	monster->monsterinfo.cached_monster_config = GetMonsterConfig(monster->monsterinfo.monster_type_id);

	// Cache MonsterLevelScaling pointer (eliminates GetMonsterLevelScaling() + string parsing)
	const char* classname = horde::MonsterTypeRegistry::GetClassname(static_cast<horde::MonsterTypeID>(monster->monsterinfo.monster_type_id));
	if (classname && strncmp(classname, "monster_", 8) == 0)
	{
		const char* monster_name = classname + 8; // Skip "monster_" prefix
		monster->monsterinfo.cached_level_scaling = GetMonsterLevelScaling(monster_name);
	}

	// Apply centralized PvM level scaling for ALL monsters
	// This ensures monsters get level-scaled health/armor even if their spawn functions haven't been updated
	ApplyPvMLevelScaling(monster);

	// Use base_health for power armor calculation to avoid double-scaling issues
	// base_health is set before any bonus multipliers are applied
	if (monster->monsterinfo.bonus_flags != BF_NONE && (!(monster->monsterinfo.bonus_flags & BF_FRIENDLY))) {
		// ==================================================================
		// CAMBIO: Añadir una comprobación para no sobreescribir la armadura existente.
		// Solo añade armadura de bonus si el monstruo no tiene ya power armor.
		// ------------------------------------------------------------------
		if (monster->monsterinfo.power_armor_power <= 0)
		{
			if (!st.was_key_specified("power_armor_power"))
				monster->monsterinfo.power_armor_power = static_cast<int>(round(monster->monsterinfo.base_health * BONUS_POWER_ARMOR_RATIO));
			if (!st.was_key_specified("power_armor_type"))
				monster->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
		}
		// ==================================================================
	}

	// Fix armor conflict: if monster has both armor and power_armor, keep only the highest value
	// THIS MUST RUN AFTER BONUS MONSTER POWER ARMOR IS ADDED (above)
	if (monster->monsterinfo.armor_power > 0 && monster->monsterinfo.power_armor_power > 0)
	{
		if (monster->monsterinfo.armor_power > monster->monsterinfo.power_armor_power)
		{
			// Regular armor is higher, remove power armor
			monster->monsterinfo.power_armor_power = 0;
			monster->monsterinfo.power_armor_type = IT_NULL;
		}
		else
		{
			// Power armor is higher or equal, remove regular armor
			monster->monsterinfo.armor_power = 0;
			monster->monsterinfo.armor_type = IT_NULL;
		}
	}

	if (monster->monsterinfo.isfriendlyspawn) {
		monster->monsterinfo.bonus_flags |= BF_FRIENDLY;
		FindMTarget(monster);
		monster->svflags |= SVF_PLAYER;
		monster->s.renderfx &= ~RF_DOT_SHADOW;

		monster->monsterinfo.team = (monster->owner && monster->owner->client)
			? monster->owner->client->resp.ctf_team
			: CTF_NOTEAM;

		gi.linkentity(monster);
	}

	// Check if this monster is spawned by a summoned commander
	// This handles reinforcements from medic_commander, tank_spawner, carrier, widow, etc.
	if (monster->monsterinfo.commander && monster->monsterinfo.commander->inuse) {
		edict_t* commander = monster->monsterinfo.commander;
		// If the commander has a chain (meaning it's summoned), inherit its properties
		InheritSummonedProperties(monster, commander, true);  // true = full setup with collision and team
	}

	monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;
	monster->gib_health = static_cast<int>(round(monster->gib_health * BONUS_GIB_HEALTH_MULT));
	if (monster->gib_health <= BONUS_GIB_HEALTH_MIN)
		monster->gib_health = BONUS_GIB_HEALTH_MIN;

	const size_t effect_index = GetBonusEffectIndex(monster->monsterinfo.bonus_flags);
	if (effect_index > 0) {
		const BonusEffectData& data = g_bonus_effects_array[effect_index];
		
		monster->s.effects |= data.effects;
		monster->s.renderfx |= data.renderfx;
		if (data.alpha > 0.0f) monster->s.alpha = data.alpha;
		
		monster->health = static_cast<int>(round(monster->health * data.health_mult));
		monster->monsterinfo.power_armor_power = static_cast<int>(round(monster->monsterinfo.power_armor_power * data.power_armor_mult));
		monster->monsterinfo.armor_power = static_cast<int>(round(monster->monsterinfo.armor_power * data.power_armor_mult));
		
		if (data.double_time_add > 0_sec)
			monster->monsterinfo.double_time = std::max(level.time, monster->monsterinfo.double_time) + data.double_time_add;
		if (data.quad_time_add > 0_sec)
			monster->monsterinfo.quad_time = std::max(level.time, monster->monsterinfo.quad_time) + data.quad_time_add;
		if (data.invincible_time_add > 0_sec)
			monster->monsterinfo.invincible_time = std::max(level.time, monster->monsterinfo.invincible_time) + data.invincible_time_add;
		if (data.attack_state != AS_NONE)
			monster->monsterinfo.attack_state = data.attack_state;
	}

	// Give BF_CHAMPION monsters a 15% chance to have quad damage (but never friendly units)
	if ((monster->monsterinfo.bonus_flags & BF_CHAMPION) &&
	    !(monster->monsterinfo.bonus_flags & BF_FRIENDLY) &&
	    frandom() <= 0.15f) {
		monster->monsterinfo.quad_time = level.time + 475_sec;
	}

	// Anti-domination escalation (mode 1/3): give non-boss, non-friendly monsters a flat health boost
	// so a stomped wave is genuinely tougher. Bosses never reach here (early return at the top of this
	// function), and friendly summons are excluded. Applied last so it stacks atop any bonus-flag
	// multiplier above; max_health is synced just below.
	if (g_horde_local.domination_flag_active &&
		(g_horde_local.domination_mode == 1 || g_horde_local.domination_mode == 3) &&
		!monster->monsterinfo.isfriendlyspawn &&
		!(monster->monsterinfo.bonus_flags & BF_FRIENDLY))
	{
		monster->health = static_cast<int>(round(monster->health * HordeConstants::DOMINATION_HEALTH_MULT));
	}

	// Update max_health to match the final health value after all bonus multipliers
	// Note: monster_start() sets max_health = health initially, but we need to update it
	// here after ApplyMonsterBonusFlags applies health multipliers
	monster->max_health = monster->health;

	// Update skin after all health scaling is complete to ensure correct appearance
	if (monster->monsterinfo.setskin)
		monster->monsterinfo.setskin(monster);

	monster->s.renderfx |= RF_IR_VISIBLE;

	// Apply bonus monster dodge if this is a bonus monster
	// Override M_MonsterDodge with better implementation, but preserve custom dodges (spider, runnertank, etc)
	if (IsBonusMonster(monster) && (!monster->monsterinfo.dodge || monster->monsterinfo.dodge == M_MonsterDodge)) {
		monster->monsterinfo.dodge = bonus_monster_dodge;
	}

	gi.linkentity(monster);
}

// Apply centralized PvM level scaling for monsters
// This ensures ALL monsters get level-based health/armor scaling
// even if their individual spawn functions haven't been updated yet
void ApplyPvMLevelScaling(edict_t* monster)
{
	if (!monster || !monster->inuse)
		return;

	// Skip bosses - they have special scaling
	if (monster->monsterinfo.IS_BOSS)
		return;

	// Get monster type and classname
	uint8_t type_id = monster->monsterinfo.monster_type_id;
	const char* classname = horde::MonsterTypeRegistry::GetClassname(static_cast<horde::MonsterTypeID>(type_id));
	
	if (!classname || strncmp(classname, "monster_", 8) != 0)
		return;

	// Extract monster name without "monster_" prefix
	const char* monster_name = classname + 8;

	// Try to get level-based scaling config
	const MonsterLevelScaling* level_scaling = GetMonsterLevelScaling(monster_name);
	if (!level_scaling)
		return; // No level scaling config for this monster

	// Get monster config for the health/armor scale multipliers
	const MonsterStatsConfig* config = GetMonsterConfig(type_id);
	if (!config)
		return;

	// Determine which level to use for scaling
	// Use the monster's pvm_level that was assigned in ApplyMonsterBonusFlags
	// This ensures level-gap scaling works correctly (monsters assigned higher levels get scaled properly)
	int scaling_level = monster->monsterinfo.pvm_level;

	// Unified source of truth:
	// Base health comes from monsters.<name>.health and level scaling contributes only addon_health.
	int scaled_health = config->health + (scaling_level * level_scaling->addon_health);
	
	// Cap health to base + 25% to prevent bullet-sponge monsters
	// Bonus flags (BF_CHAMPION, etc.) are applied AFTER this function, so they can exceed this cap
	int health_cap = static_cast<int>(config->health * 1.25f);
	if (scaled_health > health_cap)
		scaled_health = health_cap;
	
	scaled_health = static_cast<int>(scaled_health * config->health_scale);
	monster->health = scaled_health;
	monster->max_health = scaled_health;

	// Apply level-based armor scaling (if monster has regular armor configured)
	if (config->armor_power > 0 && monster->monsterinfo.armor_power > 0)
	{
		int scaled_armor = config->armor_power + (scaling_level * level_scaling->addon_armor);
		// Cap armor to base + 25%
		int armor_cap = static_cast<int>(config->armor_power * 1.25f);
		if (scaled_armor > armor_cap)
			scaled_armor = armor_cap;
		scaled_armor = static_cast<int>(scaled_armor * config->armor_scale);
		monster->monsterinfo.armor_power = scaled_armor;
	}

	// Apply level-based armor scaling to power armor (if monster has power armor configured)
	if (config->power_armor_power > 0 && monster->monsterinfo.power_armor_power > 0)
	{
		int scaled_armor = config->power_armor_power + (scaling_level * level_scaling->addon_power_armor);
		// Cap power armor to base + 25%
		int power_armor_cap = static_cast<int>(config->power_armor_power * 1.25f);
		if (scaled_armor > power_armor_cap)
			scaled_armor = power_armor_cap;
		scaled_armor = static_cast<int>(scaled_armor * config->power_armor_scale);
		monster->monsterinfo.power_armor_power = scaled_armor;
	}
}

// 13:  boss minimum calculations with LUT
static constexpr struct {
	int wave_threshold;
	int health_base;
	int power_armor_base;
} boss_tier_data[] = {
	{25, 16500, 9500},
	{20, 11250, 6750},
	{15, 9300, 3400},
	{10, 6000, 4100},
	{5, 5600, 4100},
	{0, 3750, 1125}
};

static void CalculateBossMinimums(int wave_number, int& health_min, int& power_armor_min) noexcept
{
	size_t tier_index = 0;
	for (size_t i = 0; i < std::size(boss_tier_data); ++i) {
		if (wave_number >= boss_tier_data[i].wave_threshold) {
			tier_index = i;
			break;
		}
	}
	
	const auto& tier = boss_tier_data[tier_index];
	const float random_multiplier = frandom(0.85f, 1.15f);
	
	health_min = std::min(static_cast<int>(tier.health_base * random_multiplier), tier.health_base + 1000);
	power_armor_min = std::min(static_cast<int>(tier.power_armor_base * random_multiplier), tier.power_armor_base + 500);
}

constexpr float REGULAR_ARMOR_FACTOR = 0.75f;

void ConfigureBossArmor(edict_t* self) {
	if (!self || !self->inuse || !self->monsterinfo.IS_BOSS)
		return;

	int health_min, power_armor_min;
	CalculateBossMinimums(current_wave_level, health_min, power_armor_min);

	if (self->monsterinfo.power_armor_power > 0) {
		self->monsterinfo.power_armor_power = std::max(
			self->monsterinfo.power_armor_power, power_armor_min);
	}

	if (self->monsterinfo.armor_power > 0) {
		const int min_regular_armor = static_cast<int>(power_armor_min * REGULAR_ARMOR_FACTOR);
		self->monsterinfo.armor_power = std::max(
			self->monsterinfo.armor_power, min_regular_armor);
	}
}

// Apply ONLY the cosmetic + timed-buff effects of a single bonus flag (effects/renderfx/alpha
// and any double/quad/invincible timers), with NO health/armor scaling. Used by special boss
// encounters that set their own fixed stats but still want each member visually distinct
// (e.g. the Hell Maidens, where each of the three carries a different BF_ modifier).
void ApplyBonusFlagVisuals(edict_t* ent, bonus_flags_t flag)
{
	if (!ent || !ent->inuse)
		return;

	ent->monsterinfo.bonus_flags = flag;

	const size_t effect_index = GetBonusEffectIndex(flag);
	if (effect_index == 0)
		return;

	const BonusEffectData& data = g_bonus_effects_array[effect_index];

	ent->s.effects |= data.effects;
	ent->s.renderfx |= data.renderfx | RF_IR_VISIBLE;
	if (data.alpha > 0.0f)
		ent->s.alpha = data.alpha;

	if (data.double_time_add > 0_sec)
		ent->monsterinfo.double_time = std::max(level.time, ent->monsterinfo.double_time) + data.double_time_add;
	if (data.quad_time_add > 0_sec)
		ent->monsterinfo.quad_time = std::max(level.time, ent->monsterinfo.quad_time) + data.quad_time_add;
	if (data.invincible_time_add > 0_sec)
		ent->monsterinfo.invincible_time = std::max(level.time, ent->monsterinfo.invincible_time) + data.invincible_time_add;
	if (data.attack_state != AS_NONE)
		ent->monsterinfo.attack_state = data.attack_state;

	gi.linkentity(ent);
}

// 14:  boss effects with compile-time lookup
void ApplyBossEffects(edict_t* boss)
{
	if (!boss->monsterinfo.IS_BOSS || boss->monsterinfo.effects_applied)
		return;

	const BossSizeCategory sizeCategory = boss->bossSizeCategory;
	const size_t effect_index = static_cast<size_t>(irandom(BOSS_EFFECT_MIN_INDEX, BOSS_EFFECT_MAX_INDEX));
	boss->monsterinfo.bonus_flags = static_cast<bonus_flags_t>(1 << (effect_index - 1));

	const BonusEffectData& data = g_bonus_effects_array[effect_index];
	
	boss->s.effects |= data.effects;
	boss->s.renderfx |= data.renderfx;
	if (data.alpha > 0.0f) boss->s.alpha = data.alpha;
	boss->monsterinfo.attack_state = data.attack_state;

	if (data.double_time_add > 0_sec)
		boss->monsterinfo.double_time = std::max(level.time, boss->monsterinfo.double_time) + data.double_time_add;
	if (data.quad_time_add > 0_sec)
		boss->monsterinfo.quad_time = std::max(level.time, boss->monsterinfo.quad_time) + data.quad_time_add;
	if (data.invincible_time_add > 0_sec)
		boss->monsterinfo.invincible_time = std::max(level.time, boss->monsterinfo.invincible_time) + data.invincible_time_add;

	float health_multiplier = data.health_mult;
	float power_armor_multiplier = data.power_armor_mult;
	float scale_factor = 1.0f;

	switch (sizeCategory) {
	case BossSizeCategory::Small:
		scale_factor = data.scale_small;
		health_multiplier *= data.health_mult_small;
		power_armor_multiplier *= data.pa_mult_small;
		break;
	case BossSizeCategory::Medium:
		scale_factor = data.scale_medium;
		health_multiplier *= data.health_mult_medium;
		power_armor_multiplier *= data.pa_mult_medium;
		break;
	case BossSizeCategory::Large:
		scale_factor = data.scale_large;
		health_multiplier *= data.health_mult_large;
		power_armor_multiplier *= data.pa_mult_large;
		break;
	}

	if (scale_factor != 1.0f) {
		const float original_mins_z = boss->mins[2];  // Save before additional scaling

		boss->s.scale *= scale_factor;
		boss->mins *= scale_factor;
		boss->maxs *= scale_factor;
		boss->mass *= scale_factor;

		// Adjust origin to prevent floating (uses original mins[2] before this boss scaling)
		// Skip for PSX_GUARDIAN - its model origin is already at feet level, not bbox bottom
		if (!horde::IsMonsterType(boss, horde::MonsterTypeID::PSX_GUARDIAN)) {
			boss->s.origin[2] -= (original_mins_z * scale_factor - original_mins_z);
		}
		gi.linkentity(boss);
	}

	int health_min, power_armor_min;
	CalculateBossMinimums(current_wave_level, health_min, power_armor_min);

	boss->health = std::max(static_cast<int>(boss->health * health_multiplier), health_min);
	boss->max_health = boss->health;

	if (boss->monsterinfo.power_armor_power > 0) {
		boss->monsterinfo.power_armor_power = std::max(
			static_cast<int>(boss->monsterinfo.power_armor_power * power_armor_multiplier),
			power_armor_min
		);
	}

	if (boss->monsterinfo.armor_power > 0) {
		const int min_regular_armor = static_cast<int>(power_armor_min * REGULAR_ARMOR_FACTOR);
		boss->monsterinfo.armor_power = std::max(
			static_cast<int>(boss->monsterinfo.armor_power * power_armor_multiplier),
			min_regular_armor
		);
	}

	static constexpr float size_health_mults[] = {1.0f, 0.8f, 1.0f, 1.2f};
	static constexpr float size_armor_mults[] = {1.0f, 0.9f, 1.0f, 1.2f};

	const size_t size_index = static_cast<size_t>(sizeCategory);
	if (size_index < std::size(size_health_mults)) {
		boss->health = static_cast<int>(boss->health * size_health_mults[size_index]);
		
		if (boss->monsterinfo.power_armor_power > 0)
			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * size_armor_mults[size_index]);
		if (boss->monsterinfo.armor_power > 0)
			boss->monsterinfo.armor_power = static_cast<int>(boss->monsterinfo.armor_power * size_armor_mults[size_index]);
	}

	boss->health = std::max(boss->health, health_min);
	if (boss->monsterinfo.power_armor_power > 0)
		boss->monsterinfo.power_armor_power = std::max(boss->monsterinfo.power_armor_power, power_armor_min);
	if (boss->monsterinfo.armor_power > 0) {
		const int min_regular_armor = static_cast<int>(power_armor_min * REGULAR_ARMOR_FACTOR);
		boss->monsterinfo.armor_power = std::max(boss->monsterinfo.armor_power, min_regular_armor);
	}

	boss->max_health = boss->health;
	boss->monsterinfo.effects_applied = true;
}

// 15:  player name retrieval with caching
const char* GetPlayerName(const edict_t* player) {
    constexpr size_t NUM_BUFFERS = 8;
    thread_local char player_name_buffers[NUM_BUFFERS][MAX_INFO_VALUE];
    thread_local size_t buffer_index = 0;

    thread_local struct {
        const edict_t* last_player = nullptr;
        char cached_name[MAX_INFO_VALUE];
        gtime_t cache_time = 0_sec;
    } name_cache;
    
    static constexpr gtime_t CACHE_DURATION = 1_sec;

    buffer_index = (buffer_index + 1) % NUM_BUFFERS;
    char* const current_buffer = player_name_buffers[buffer_index];
    
    if (!player || !player->client || !player->inuse) { 
        Q_strlcpy(current_buffer, "N/A", sizeof(player_name_buffers[0]));
        return current_buffer;
    }
    
    if (name_cache.last_player == player && 
        level.time - name_cache.cache_time < CACHE_DURATION) {
        Q_strlcpy(current_buffer, name_cache.cached_name, sizeof(player_name_buffers[0]));
        return current_buffer;
    }
    
    size_t written = gi.Info_ValueForKey(player->client->pers.userinfo, "name",
        name_cache.cached_name, sizeof(name_cache.cached_name) - 1);
    
    if (written == 0 || written >= sizeof(name_cache.cached_name)) {
        Q_strlcpy(current_buffer, "N/A", sizeof(player_name_buffers[0]));
        return current_buffer;
    }
    
    name_cache.cached_name[written] = '\0';
    name_cache.last_player = player;
    name_cache.cache_time = level.time;

    Q_strlcpy(current_buffer, name_cache.cached_name, sizeof(player_name_buffers[0]));
    
    return current_buffer;
}

// Forward declarations for external functions
extern void SP_target_earthquake(edict_t* self);
extern constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_SILENT = 1_spawnflag;
extern constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_ONE_SHOT = 8_spawnflag;

// 16:  spawn grow with reduced allocations
void ImprovedSpawnGrow(const vec3_t& position, float start_size, float end_size, edict_t* spawned_entity)
{
	SpawnGrow_Spawn(position, start_size, end_size);

	if (!spawned_entity || !spawned_entity->monsterinfo.IS_BOSS) {
		return;
	}

	const float scaled_start = start_size * BOSS_EFFECT_SCALE;
	const float scaled_end = end_size * BOSS_EFFECT_SCALE;

	for (int i = 0; i < BOSS_SECONDARY_EFFECTS; i++) {
		vec3_t offset;
		offset.x = position.x + crandom() * BOSS_RANDOM_OFFSET_RANGE;
		offset.y = position.y + crandom() * BOSS_RANDOM_OFFSET_RANGE;
		offset.z = position.z + crandom() * BOSS_RANDOM_OFFSET_RANGE;
		
		SpawnGrow_Spawn(offset, scaled_start, scaled_end);
	}

	edict_t* earthquake = G_Spawn();
	if (earthquake) {
		earthquake->classname = "target_earthquake";
		earthquake->spawnflags = brandom()
			? SPAWNFLAGS_EARTHQUAKE_SILENT
			: SPAWNFLAGS_EARTHQUAKE_ONE_SHOT;
		earthquake->speed = BOSS_EARTHQUAKE_SPEED;
		earthquake->count = BOSS_SECONDARY_EFFECTS;

		SP_target_earthquake(earthquake);
		earthquake->use(earthquake, spawned_entity, spawned_entity);
	}
}

// 17:  path checking with early exits
bool G_IsClearPath(const edict_t* ignore, contents_t mask, const vec3_t& spot1, const vec3_t& spot2) {
	if (!is_valid_vector(spot1) || !is_valid_vector(spot2)) {
		return false;
	}
	
	const float dist_sq = (spot2 - spot1).lengthSquared();
	if (dist_sq < 1.0f) {
		return true;
	}
	
	const trace_t tr = gi.traceline(spot1, spot2, ignore, mask);
	return (tr.fraction == 1.0f);
}

// 18:  entity teleportation
void TeleportEntity(edict_t* ent, edict_t* dest) {
	if (!ent || !ent->inuse || !dest || !dest->inuse || !is_valid_vector(dest->s.origin)) {
		return;
	}

	ent->svflags |= SVF_NOCLIENT;
	gi.unlinkentity(ent);

	ent->s.origin = dest->s.origin;
	ent->s.origin.z += TELEPORT_Z_OFFSET;
	ent->velocity = vec3_origin;

	if (ent->client) {
		ent->client->ps.pmove.pm_time = TELEPORT_PM_TIME;
		ent->client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;
		ent->s.event = EV_PLAYER_TELEPORT;

		ent->client->ps.viewangles = dest->s.angles;
		ent->client->v_angle = dest->s.angles;
		ent->s.angles = dest->s.angles;

		for (int i = 0; i < 3; i++) {
			ent->client->ps.pmove.origin[i] = static_cast<int16_t>(ent->s.origin[i] * TELEPORT_POSITION_SCALE);
			float angle_diff = anglemod(dest->s.angles[i] - ent->client->resp.cmd_angles[i]);
			ent->client->ps.pmove.delta_angles[i] = angle_diff;
		}
		ent->client->ps.pmove.velocity = vec3_origin;
	} else {
		ent->s.angles = dest->s.angles;
	}

	ent->svflags &= ~SVF_NOCLIENT;
	gi.linkentity(ent);
	KillBox(ent, false, MOD_TELEFRAG, true);
}

// 19:  entity overlap checking
[[nodiscard]] bool EntitiesOverlap(const edict_t* ent, const vec3_t& area_mins, const vec3_t& area_maxs) {
	if (!ent) return false;

	const vec3_t& origin = ent->s.origin;
	const vec3_t& mins = ent->mins;
	const vec3_t& maxs = ent->maxs;
	
	return boxes_intersect(
		{origin.x + mins.x, origin.y + mins.y, origin.z + mins.z},
		{origin.x + maxs.x, origin.y + maxs.y, origin.z + maxs.z},
		area_mins, area_maxs
	);
}

edict_t* SelectSingleSpawnPoint(edict_t* ent);
// 20:  area clearing with spatial partitioning
void ClearSpawnArea(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs) {
	if (!is_valid_vector(origin) || !is_valid_vector(mins) || !is_valid_vector(maxs)) {
		return;
	}

	const vec3_t safe_offset{SPAWN_CLEAR_SAFE_OFFSET, SPAWN_CLEAR_SAFE_OFFSET, SPAWN_CLEAR_SAFE_OFFSET};
	const vec3_t area_mins = origin + mins - safe_offset;
	const vec3_t area_maxs = origin + maxs + safe_offset;

	// OPTIMIZATION: Use engine's BoxEdicts spatial query instead of findradius
	// BoxEdicts uses BSP tree/spatial partitioning for O(log n) lookup vs O(n) iteration
	// More accurate since we're checking box overlap anyway
	edict_t* touch[MAX_EDICTS];
	int num_touched = gi.BoxEdicts(area_mins, area_maxs, touch, MAX_EDICTS, AREA_SOLID, nullptr, nullptr);

	boost::container::small_vector<edict_t*, STACK_ENTITY_CAPACITY> entities_array;

	for (int i = 0; i < num_touched; i++) {
		edict_t* ent = touch[i];
		if (!ent || !ent->inuse ||
			(ent->svflags & SVF_MONSTER) ||
			ent->solid == SOLID_NOT ||
			ent->solid == SOLID_TRIGGER ||
			!is_valid_vector(ent->s.origin) ||
			!is_valid_vector(ent->mins) ||
			!is_valid_vector(ent->maxs) ||
			!EntitiesOverlap(ent, area_mins, area_maxs)) {
			continue;
		}

		// Safety check: prevent overflow-induced bad_alloc
		if (entities_array.size() >= MAX_SAFE_CONTAINER_SIZE) {
			if (developer && developer->integer) {
				gi.Com_PrintFmt("ERROR: ClearSpawnArea entity overflow ({}), stopping collection\n", entities_array.size());
			}
			break;
		}

		if (!safe_push_back(entities_array, ent)) {
			gi.Com_Print("ERROR: Failed to allocate memory for spawn area entity collection\n");
			break;
		}
	}

	for (edict_t* current_ent : entities_array) {
		if (!current_ent || !current_ent->inuse) continue;

		if (current_ent->client) {
			edict_t* spawn_point = SelectSingleSpawnPoint(current_ent);
			if (spawn_point && spawn_point->inuse) {
				TeleportEntity(current_ent, spawn_point);
			}
		} else {
			RemoveEntity(current_ent);
		}
	}
}

// 21:  entity pushing with spatial coherence
void PushEntitiesAway(const vec3_t& center, int num_waves, float push_radius, float push_strength, 
				  float horizontal_push_strength, float vertical_push_strength)
{
	push_radius = std::max(push_radius, 1.0f);
	const float search_radius = push_radius * 1.5f;

	edict_t* processable_entities[MAX_PUSHABLE_ENTITIES];
	edict_t* removable_entities[MAX_PUSHABLE_ENTITIES];
	size_t processable_count = 0;
	size_t removable_count = 0;

	// First, check for removable special entities (traps, mines, bases, etc.)
	for (edict_t* ent = nullptr; (ent = findradius(ent, center, search_radius)) != nullptr;) {
		if (!ent || !ent->inuse ||
			gi.traceline(center, ent->s.origin, nullptr, MASK_SOLID).fraction < 1.0f) {
			continue;
		}

		if (IsRemovableEntity(ent)) {
			if (removable_count < MAX_PUSHABLE_ENTITIES) {
				removable_entities[removable_count++] = ent;
			}
		} else if (ent->takedamage && !(ent->svflags & SVF_MONSTER) && processable_count < MAX_PUSHABLE_ENTITIES) {
			// Non-monster entities that can be damaged (players, etc.)
			processable_entities[processable_count++] = ent;
		}
	}

	// Then check for monsters using the monster grid (more efficient)
	auto monsters = HordePhys::g_monster_grid.QueryRadius(center, search_radius);
	for (edict_t* monster : monsters) {
		if (!monster || !monster->inuse ||
			gi.traceline(center, monster->s.origin, nullptr, MASK_SOLID).fraction < 1.0f) {
			continue;
		}

		// Summoned monsters get weakened along with the deployables
		if (monster->monsterinfo.isfriendlyspawn) {
			if (removable_count < MAX_PUSHABLE_ENTITIES) {
				removable_entities[removable_count++] = monster;
			}
		} else if (processable_count < MAX_PUSHABLE_ENTITIES) {
			// Regular monsters get pushed/damaged
			processable_entities[processable_count++] = monster;
		}
	}

	// Weaken (instead of destroy) every collected removable entity.
	for (size_t i = 0; i < removable_count; ++i) {
		if (removable_entities[i] && removable_entities[i]->inuse)
			WeakenEntityForBoss(removable_entities[i]);
	}

	for (int wave = 0; wave < num_waves; wave++) {
		const float wave_progress = static_cast<float>(wave) / num_waves;
		const float size = std::max(push_radius * (1.0f - wave_progress * 0.5f), 0.030f);

		SpawnGrow_Spawn(center, size, size * 0.3f);

		if (wave > 0) {
			for (int i = 0; i < 4; i++) {
				vec3_t effect_pos;
				effect_pos.x = center.x + crandom() * size * 0.5f;
				effect_pos.y = center.y + crandom() * size * 0.5f;
				effect_pos.z = center.z + crandom() * size * 0.25f;
				SpawnGrow_Spawn(effect_pos, size * 0.5f, size * 0.15f);
			}
		}

		for (size_t i = 0; i < processable_count; ++i) {
			edict_t* ent = processable_entities[i];
			if (!ent || !ent->inuse || !gi.inPVS(center, ent->s.origin, false)) {
				continue;
			}

			vec3_t push_dir = ent->s.origin - center;
			const float dist = push_dir.length();

			if (dist < 0.01f) {
				push_dir = vec3_t{crandom(), crandom(), 0.1f};
			} else {
				push_dir *= (1.0f / dist);
			}

			const float dist_factor = std::max(0.0f, 1.0f - (dist / search_radius));
			int base_push = (ent->svflags & SVF_MONSTER) ? 800 : 80;
			if (ent->groundentity) base_push *= 2;
			base_push = static_cast<int>(base_push * dist_factor);

			if (ent->client) {
				ent->client->landmark_free_fall = true;
				if (wave == 0) {
					ent->client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;
					ent->client->ps.pmove.pm_time = 100;
				}
			}

			T_Damage(ent, ent, ent, push_dir, ent->s.origin, vec3_origin,
				0, base_push, DAMAGE_RADIUS, MOD_UNKNOWN);

			if (vertical_push_strength > 0 && wave <= 1) {
				const float boost = (wave == 0) ? 100.0f : 75.0f;
				ent->velocity.z += boost;
			}
		}
	}
}

// 22:  string comparison
[[nodiscard]] inline bool string_equals(const char* str1, std::string_view str2) noexcept {
	if (!str1) return false;
	const size_t str1_len = strlen(str1);
	return str1_len == str2.length() &&
		   !Q_strncasecmp(str1, str2.data(), str2.length());
}

// Direct-indexed monster display-name overrides. Empty entries fall back to
// formatted classnames in InitializeDisplayNames().
static constexpr auto monster_name_replacements = []() constexpr {
    std::array<std::string_view, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> names{};
    auto set = [&names](horde::MonsterTypeID id, std::string_view name) constexpr {
        names[static_cast<size_t>(id)] = name;
    };

    set(horde::MonsterTypeID::SOLDIER_LIGHT, "Blaster Guard");
    set(horde::MonsterTypeID::SOLDIER, "SG Guard");
    set(horde::MonsterTypeID::SOLDIER_SS, "SS Guard");
    set(horde::MonsterTypeID::SOLDIER_HYPERGUN, "Hyper Guard");
    set(horde::MonsterTypeID::SOLDIER_LASERGUN, "Laser Guard");
    set(horde::MonsterTypeID::SOLDIER_RIPPER, "Ripper Guard");
    set(horde::MonsterTypeID::INFANTRY_VANILLA, "Infantry");
    set(horde::MonsterTypeID::INFANTRY, "Enforcer");
    set(horde::MonsterTypeID::GUNNER_VANILLA, "Gunner");
    set(horde::MonsterTypeID::GUNNER, "Heavy Gunner");
    set(horde::MonsterTypeID::GUNCMDR_VANILLA, "Gunner Commander");
    set(horde::MonsterTypeID::GUNCMDR, "Gunner Grenadier");
    set(horde::MonsterTypeID::GUNCMDR_KL, "Gunner Commander");
    set(horde::MonsterTypeID::FLYER, "Flyer");
    set(horde::MonsterTypeID::KAMIKAZE, "Kamikaze Flyer");
    set(horde::MonsterTypeID::HOVER_VANILLA, "Blaster Icarus");
    set(horde::MonsterTypeID::HOVER, "Rocket Icarus");
    set(horde::MonsterTypeID::DAEDALUS, "Daedalus");
    set(horde::MonsterTypeID::DAEDALUS_BOMBER, "Bomber Daedalus");
    set(horde::MonsterTypeID::FLOATER, "Technician");
    set(horde::MonsterTypeID::FLOATER_TRACKER, "DarkMatter Technician");
    set(horde::MonsterTypeID::MEDIC, "Medic");
    set(horde::MonsterTypeID::MEDIC_COMMANDER, "Medic Commander");
    set(horde::MonsterTypeID::FIXBOT, "Fixbot");
    set(horde::MonsterTypeID::FIXBOT_KL, "Fixer");
    set(horde::MonsterTypeID::MUTANT, "Mutant");
    set(horde::MonsterTypeID::REDMUTANT, "Raged Mutant");
    set(horde::MonsterTypeID::BERSERK, "Berserker");
    set(horde::MonsterTypeID::BERSERKERKL, "Trespasser Yeller");
    set(horde::MonsterTypeID::GEKK, "Gekk");
    set(horde::MonsterTypeID::GEKKKL, "Inferno Gekk");
    set(horde::MonsterTypeID::PARASITE, "Parasite");
    set(horde::MonsterTypeID::PERRO_KL, "Infected Parasite");
    set(horde::MonsterTypeID::STALKER, "Stalker");
    set(horde::MonsterTypeID::BRAIN, "Brain");
    set(horde::MonsterTypeID::CHICK, "Iron Maiden");
	set(horde::MonsterTypeID::CHICK_HEAT, "Iron Praetor");
	set(horde::MonsterTypeID::CHICKKL, "Hell Maiden");
    set(horde::MonsterTypeID::TANK, "Tank");
    set(horde::MonsterTypeID::TANK_64, "N64 Tank");
    set(horde::MonsterTypeID::TANK_COMMANDER, "Tank Commander");
    set(horde::MonsterTypeID::TANK_SPAWNER, "Spawner Tank");
    set(horde::MonsterTypeID::RUNNERTANK, "BETA Runner Tank");
    set(horde::MonsterTypeID::GLADIATOR, "Gladiator");
    set(horde::MonsterTypeID::GLADIATOR_B, "DarkMatter Gladiator");
    set(horde::MonsterTypeID::GLADIATOR_C, "Plasma Gladiator");
    set(horde::MonsterTypeID::SPIDER, "Plasma Spider");
    set(horde::MonsterTypeID::ARACHNID, "Arachnid");
    set(horde::MonsterTypeID::ARACHNID2, "Arachnid");
    set(horde::MonsterTypeID::PSX_ARACHNID, "Arachnid");
    set(horde::MonsterTypeID::GM_ARACHNID, "Guided-Missile Arachnid");
    set(horde::MonsterTypeID::SHAMBLER, "Shambler");
    set(horde::MonsterTypeID::SHAMBLER_SMALL, "Tiny Shambler!");
    set(horde::MonsterTypeID::SHAMBLER_KL, "Shambler");
    set(horde::MonsterTypeID::BOSS2, "Hornet");
    set(horde::MonsterTypeID::BOSS2_64, "N64 Mini Hornet");
    set(horde::MonsterTypeID::BOSS2_MINI, "Mini Hornet");
    set(horde::MonsterTypeID::BOSS2_KL, "N64 Hornet");
    set(horde::MonsterTypeID::CARRIER, "Carrier");
    set(horde::MonsterTypeID::CARRIER_MINI, "Mini Carrier");
    set(horde::MonsterTypeID::MAKRON, "Makron");
    set(horde::MonsterTypeID::MAKRON_KL, "Makron");
    set(horde::MonsterTypeID::JORG, "Jorg");
    set(horde::MonsterTypeID::JORG_SMALL, "Mini Jorg");
    set(horde::MonsterTypeID::WIDOW, "Widow Battle-Maiden");
    set(horde::MonsterTypeID::WIDOW1, "Black Widow");
    set(horde::MonsterTypeID::WIDOW2, "Widow Creator");
    set(horde::MonsterTypeID::GUARDIAN, "Guardian");
    set(horde::MonsterTypeID::PSX_GUARDIAN, "Guardian");
    set(horde::MonsterTypeID::JANITOR, "Janitor");
    set(horde::MonsterTypeID::JANITOR2, "Mini Guardian");
    set(horde::MonsterTypeID::SUPERTANK, "Super-Tank");
    set(horde::MonsterTypeID::SUPERTANKKL, "Super-Tank");
    set(horde::MonsterTypeID::BOSS5, "Super-Tank");
    set(horde::MonsterTypeID::SENTRYGUN, "Sentry-Gun");
    set(horde::MonsterTypeID::TURRET, "Turret");
    set(horde::MonsterTypeID::MISC_INSANE, "Insane Grunt");
    set(horde::MonsterTypeID::FLIPPER, "Flipper");

    return names;
}();

static std::string_view GetMonsterNameReplacement(horde::MonsterTypeID typeId) noexcept {
    const size_t index = static_cast<size_t>(typeId);
    return (index < monster_name_replacements.size()) ? monster_name_replacements[index] : std::string_view{};
}

// 24:  teleportation with better spawn point selection
bool TeleportSelf(edict_t* ent) {
	if (!ent || !ent->inuse || !ent->client || !ent->solid || ent->deadflag) {
		return false;
	}

	if (ent->client->resp.teleport_cooldown > level.time) {
		float remaining_seconds = (ent->client->resp.teleport_cooldown - level.time).seconds();
		float remaining_display = std::floor(remaining_seconds * 10.0f) / 10.0f;
		gi.LocClient_Print(ent, PRINT_HIGH, "Teleport on cooldown for {} seconds\n", remaining_display);
		return false;
	}

	ent->client->resp.teleport_cooldown = level.time + 3_sec;

	if (g_num_spawn_points == 0) {
		if (developer && developer->integer) {
			//gi.Com_PrintFmt("TeleportSelf WARNING: No valid spawn points found for teleport.\n");
		}
		return false;
	}

	const char* playerName = GetPlayerName(ent);
	bool was_emergency = ent->client->emergency_teleport;

	auto perform_teleport_actions = [&](edict_t* destination_spot) {
		TeleportEntity(ent, destination_spot);

		if (ent->client->owned_sphere) {
			edict_t* sphere = ent->client->owned_sphere;
			sphere->s.origin = ent->s.origin;
			sphere->s.origin.z = ent->absmax.z;
			sphere->s.angles[YAW] = ent->s.angles[YAW];
			gi.linkentity(sphere);
		}

		if (!was_emergency) {
			gi.LocBroadcast_Print(PRINT_HIGH, "{} Teleported Away!\n", playerName);
		}

		ent->client->invincible_time = std::max(level.time, ent->client->invincible_time) + 2_sec;
	};

	edict_t* best_spot = SelectRandomClearSpawnPoint();
	if (best_spot) {
		perform_teleport_actions(best_spot);
	} else {
		// Fallback: try to find any non-style=1 spawn point
		edict_t* fallback_spot = nullptr;
		for (size_t i = 0; i < g_num_spawn_points; ++i) {
			edict_t* spot = g_spawn_point_list[i];
			if (spot && spot->inuse && spot->style != 1) {
				fallback_spot = spot;
				break;
			}
		}
		// Use fallback or last resort any spawn point
		perform_teleport_actions(fallback_spot ? fallback_spot : g_spawn_point_list[0]);
	}

	if (was_emergency) {
		ent->client->emergency_teleport = false;
	}

	return true;
}

// 25:  jump move detection - zero heap allocation with compile-time array
static constexpr std::array<const mmove_t*, 32> g_jump_moves = {
    &berserk_move_jump, &berserk_move_jump2,
    &brain_move_jumpattack, &brain_move_jump, &brain_move_jump2,
    &chick_move_jump, &chick_move_jump2,
    &guncmdr_move_jump, &guncmdr_move_jump2,
    &gunner_move_jump, &gunner_move_jump2,
    &gunner_vanilla_move_jump, &gunner_vanilla_move_jump2,
    &infantry_move_jump, &infantry_move_jump2,
    &mutant_move_jump, &mutant_move_jump_up, &mutant_move_jump_down,
    &parasite_move_jump_up, &parasite_move_jump_down,
    &redmutant_move_jump, &redmutant_move_jump_up, &redmutant_move_jump_down,
    &runnertank_move_jump, &runnertank_move_jump2,
    &soldier_move_jump, &soldier_move_jump2,
    &stalker_move_jump_straightup, &stalker_move_jump_up, &stalker_move_jump_down,
    &gekk_move_jump_up, &gekk_move_jump_down
};

bool IsMonsterJumping(const edict_t* self) {
    if (!self || !self->monsterinfo.active_move) {
        return false;
    }

    const mmove_t* current_move = self->monsterinfo.active_move.pointer();
    // Linear search - with 31 entries, likely faster than binary search due to cache locality
    return std::find(g_jump_moves.begin(), g_jump_moves.end(), current_move) != g_jump_moves.end();
}

bool horde_fog_active = false;  // Tracks if boss fog is currently active

// Tracks which clients had their flashlight forced ON by the fog system, keyed by client
// index (player->client - game.clients). RestoreFog only switches the flashlight back off
// for these players, so anyone who already had it on by choice keeps it. Cleared on the
// rising edge of each fog wave and again as each player is restored.
static std::array<bool, MAX_CLIENTS> fog_forced_flashlight{};

// Turn the flashlight on for fog visibility if it's off, and remember that the fog system
// forced it -- so RestoreFog can switch it back off without disturbing players who already
// had it on by choice. Shared by ApplyFogEffect and the client-spawn fog path
// (SetupPlayerFog), so mid-wave joiners are tracked the same as players present at start.
void ForceFlashlightForFog(edict_t* player)
{
	if (!player || !player->client)
		return;

	if (!(player->flags & FL_FLASHLIGHT))
	{
		P_ToggleFlashlight(player, true);
		fog_forced_flashlight[player->client - game.clients] = true;
	}
}

// Clear all fog/flashlight tracking. Called from ResetGame() so a reset that happens mid
// fog-wave can't leak boss fog or a forced flashlight into the next map.
void ResetFogState() noexcept
{
	horde_fog_active = false;
	fog_forced_flashlight.fill(false);
}

// Boss spawning functions
// Apply a temporary, wave-themed fog to all players for special-wave spawns.
// Colors are chosen per wave type so the two special waves read differently,
// matching the per-wave sound/name theming in ApplySpecialWaveEffects().
// Called every frame while the wave is active, which also covers mid-wave joiners
// (P_ForceFogTransition early-outs once a client has reached the target fog).
void ApplyFogEffect(MonsterWaveType waveType)
{
	// Rising edge: the first frame a fog wave activates, forget any stale "we forced the
	// flashlight" flags from a previous wave so each player's pre-wave state starts clean.
	const bool fog_starting = !horde_fog_active;
	horde_fog_active = true;
	if (fog_starting)
		fog_forced_flashlight.fill(false);

	// Fog values are 0..1: { density, red, green, blue, sky-darken factor }.
	// Gekk ("Inferno Gekk")  -> sickly toxic-green swamp haze (default).
	// Berserk ("Trespasser") -> deep blood/ember red.
	std::array<float, 5> fog = { 0.15f, 0.10f, 0.22f, 0.06f, 0.85f }; // Gekk: toxic green
	if (HasWaveType(waveType, MonsterWaveType::Berserk))
		fog = { 0.15f, 0.34f, 0.05f, 0.10f, 0.85f }; // Berserk: blood red

	// active_players() (NOT _no_spect): spectators must see the boss fog too -- they follow the
	// action and fog is per-client. ForceFlashlightForFog is a no-op for anyone already lit.
	for (auto player : active_players())
	{
		// Quick transition into the wave fog (also drives mid-wave joiners).
		player->client->pers.fog_transition_time = 4000_ms;
		player->client->pers.wanted_fog = fog;

		// Force the flashlight on so players can see through the fog (records that we did,
		// and won't re-toggle a player who already has it on).
		ForceFlashlightForFog(player);
	}
}

// Apply special wave effects (sounds, earthquake) for Gekk/Berserk waves
void ApplySpecialWaveEffects(MonsterWaveType waveType)
{
	bool is_gekk_wave = HasWaveType(waveType, MonsterWaveType::Gekk);
	bool is_berserk_wave = HasWaveType(waveType, MonsterWaveType::Berserk);

	if (!is_gekk_wave && !is_berserk_wave)
		return;

	// Play themed sound based on wave type
	int sound_index = 0;
	if (is_gekk_wave)
	{
		// Random Gekk sound
		sound_index = brandom()
			? gi.soundindex("gek/gek_low.wav")
			: gi.soundindex("gek/gek_amb.wav");
	}
	else // Berserk wave
	{
	sound_index = brandom()
		? sound_index = gi.soundindex("world/radio3.wav")
		: sound_index = gi.soundindex("world/amb20.wav");
	}

	// Play sound globally
	if (sound_index)
	{
		gi.positioned_sound(vec3_origin, world, CHAN_AUTO, sound_index, 1.0f, ATTN_NONE, 0);
	}

	// Create earthquake effect
	edict_t* earthquake = G_Spawn();
	if (earthquake)
	{
		earthquake->classname = "target_earthquake";
		earthquake->spawnflags = brandom()
			? SPAWNFLAGS_EARTHQUAKE_SILENT
			: SPAWNFLAGS_EARTHQUAKE_ONE_SHOT;
		earthquake->speed = 250; // Earthquake severity
		earthquake->count = 6; // Duration in seconds

		SP_target_earthquake(earthquake);
		earthquake->use(earthquake, world, world);

		if (developer && developer->integer)
		{
			gi.Com_PrintFmt("HORDE: Applied {} wave special effects (sound + earthquake)\n",
				is_gekk_wave ? "Inferno Gekk" : "Trespasser");
		}
	}
}

// Restore the map's original fog after the special wave ends, and switch the flashlight
// back off only for players whose flashlight the fog system turned on.
void RestoreFog()
{
	// Mark fog as inactive
	horde_fog_active = false;

	// mgu4trial needs the flashlight on continuously, so never auto-disable it there.
	const bool keep_flashlight_map =
		horde::MapOriginRegistry::GetMapID(level.mapname) == horde::MapID::MGU4TRIAL;

	// active_players() so spectators are transitioned back too (they got the fog above).
	for (auto player : active_players())
	{
		// Slow transition back to the map's original (worldspawn) fog.
		player->client->pers.fog_transition_time = 2000_ms;
		player->client->pers.wanted_fog = {
			world->fog.density,
			world->fog.color[0],
			world->fog.color[1],
			world->fog.color[2],
			world->fog.sky_factor
		};

		// Only undo the flashlight if WE forced it on; players who had it on by choice keep
		// it. Clear the flag either way so it doesn't leak into the next wave.
		const ptrdiff_t cn = player->client - game.clients;
		const bool we_forced_it = fog_forced_flashlight[cn];
		fog_forced_flashlight[cn] = false;

		if (we_forced_it && !keep_flashlight_map && (player->flags & FL_FLASHLIGHT))
		{
			P_ToggleFlashlight(player, false);
		}
	}
}
