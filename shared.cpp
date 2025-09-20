#include "shared.h"
#include "horde/g_horde.h"
#include <unordered_map>
#include <algorithm>
#include <span>
#include "horde/horde_ids.h"
#include "horde/g_entity_properties.h"
#include <optional>
#include <vector>
#include <string>
#include <string_view>
#include <array>
#include <bit>
#include <unordered_set>

void RemoveEntity(edict_t* ent);

// 1:  spawn point selection using pre-shuffled global list
[[nodiscard]] static edict_t* SelectRandomClearSpawnPoint() {
	if (g_num_spawn_points == 0) {
		return nullptr;
	}

	thread_local size_t last_checked_index = 0;
	
	const size_t max_attempts = std::min(static_cast<size_t>(16), g_num_spawn_points);
	
	for (size_t i = 0; i < max_attempts; ++i) {
		size_t check_index = (last_checked_index + i) % g_num_spawn_points;
		edict_t* spot = g_spawn_point_list[check_index];

		if (spot && spot->inuse && !IsSpawnPointOccupied(spot)) {
			last_checked_index = check_index + 1;
			return spot;
		}
	}

	last_checked_index = static_cast<size_t>(irandom(g_num_spawn_points));
	return g_spawn_point_list[last_checked_index];
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
		.effects = EF_GIB | EF_FLAG2, .health_mult = 0.92f, .power_armor_mult = 1.32f,
		.quad_time_add = 475_sec, .attack_state = AS_BLIND
	},
	{
		.effects = EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE, .alpha = 0.6f,
		.health_mult = 1.2f, .power_armor_mult = 1.35f, .attack_state = AS_BLIND
	},
	{
		.effects = EF_TRACKER | EF_FLAG1, .attack_state = AS_BLIND,
		.scale_small = 1.1f, .scale_medium = 1.3f, .scale_large = 1.2f,
		.health_mult_small = 1.2f, .health_mult_medium = 1.5f, .health_mult_large = 1.5f,
		.pa_mult_small = 1.1f, .pa_mult_medium = 1.1f, .pa_mult_large = 1.1f
	}
}};

constexpr size_t GetBonusEffectIndex(bonus_flags_t flags) {
	if (flags == BF_NONE) return 0;
	if (flags & BF_CHAMPION) return 1;
	if (flags & BF_CORRUPTED) return 2;
	if (flags & BF_RAGEQUITTER) return 3;
	if (flags & BF_BERSERKING) return 4;
	if (flags & BF_POSSESSED) return 5;
	if (flags & BF_STYGIAN) return 6;
	return 0;
}

// 3: Enhanced map size cache with perfect hash
[[nodiscard]] horde::MapSize GetMapSize(const char* mapname) noexcept {
    static std::array<std::optional<horde::MapSize>,
                      static_cast<size_t>(horde::MapID::MAX_MAPS)> s_cache;
    static char s_last_map_for_cache[MAX_QPATH] = "";

    // If the map has changed, clear cache and reset spawn point tracking
    if (strcmp(s_last_map_for_cache, level.mapname) != 0) {
        s_cache.fill(std::nullopt);
        Q_strlcpy(s_last_map_for_cache, level.mapname, sizeof(s_last_map_for_cache));
        
        // Reset spawn point selection state for new map
        // This would need to be implemented as a separate function
        // ResetSpawnPointSelection(); 
    }

    const horde::MapID mapId = horde::MapOriginRegistry::GetMapID(mapname);
    if (mapId == horde::MapID::UNKNOWN) {
        return {}; 
    }

    const size_t index = static_cast<size_t>(mapId);

    if (s_cache[index].has_value()) {
        return s_cache[index].value();
    }

    const horde::MapSize size = horde::MapOriginRegistry::GetMapSize(mapId);
    s_cache[index] = size;
    return size;
}

// 4: Batch entity removal with memory pooling
void RemovePlayerOwnedEntities(edict_t* player) {
    if (!player || !player->client) {
        return;
    }

    constexpr size_t STACK_CAPACITY = 32;
    edict_t* stack_entities[STACK_CAPACITY];
    std::vector<edict_t*> heap_entities;
    
    edict_t** entities_array = stack_entities;
    size_t entity_count = 0;
    size_t capacity = STACK_CAPACITY;

    auto add_entity = [&](edict_t* ent) {
        if (ent && ent->inuse) {
            if (entity_count >= capacity) {
                if (entities_array == stack_entities) {
                    // Resize first, then get pointer
                    heap_entities.resize(capacity * 2);
                    heap_entities.assign(stack_entities, stack_entities + entity_count);
                    entities_array = heap_entities.data();
                    capacity = heap_entities.size();
                } else {
                    // Already using heap, just resize
                    heap_entities.resize(capacity * 2);
                    entities_array = heap_entities.data();
                    capacity = heap_entities.size();
                }
            }
            entities_array[entity_count++] = ent;
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

    for (size_t i = 0; i < entity_count; ++i) {
        edict_t* ent = entities_array[i];
        if (ent && ent->inuse) {
            RemoveEntity(ent);
        }
    }
}

// 5:  defense check with compile-time lookup
bool IsPlayerDefense(const edict_t* ent) {
    if (!ent) return false;
    
    auto id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);
    if (id == horde::SpecialEntityTypeID::UNKNOWN) return false;
    
    static constexpr uint64_t DEFENSE_MASK = 
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::SENTRY_GUN)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::TURRET)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::LASER_EMITTER)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::TESLA_MINE)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::FOOD_CUBE_TRAP));
    
    return (DEFENSE_MASK & (1ULL << static_cast<uint64_t>(id))) != 0;
}

// 6:  removable entity check
bool IsRemovableEntity(const edict_t* ent) {
    if (!ent) return false;

    auto id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);
    if (id == horde::SpecialEntityTypeID::UNKNOWN) {
        if (developer->integer) {
            thread_local std::unordered_set<std::string_view> reported_classnames;
            thread_local char last_map_for_reporting[MAX_QPATH] = "";
            
            if (strcmp(last_map_for_reporting, level.mapname) != 0) {
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

    static constexpr uint64_t REMOVABLE_MASK = 
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::TESLA_MINE)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::FOOD_CUBE_TRAP)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::LASER_EMITTER)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::DOPPLEGANGER)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::PROX_MINE)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::SENTRY_GUN)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::TURRET)) |
        (1ULL << static_cast<uint64_t>(horde::SpecialEntityTypeID::NUKE_MINE));
    
    return (REMOVABLE_MASK & (1ULL << static_cast<uint64_t>(id))) != 0;
}

void RemoveEntity(edict_t* ent) {
    if (!ent || !ent->inuse) return;
    auto id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);
    if (id != horde::SpecialEntityTypeID::UNKNOWN) {
        EntityDieHandler handler = GetDieHandler(id);
        if (handler) {
            handler(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
            return;
        }
    }
    BecomeExplosion1(ent);
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

	if (monster->monsterinfo.quad_time > current_time) return 4.0f;
	if (monster->monsterinfo.double_time > current_time) return 2.0f;
	return 1.0f;
}

// 9:  title generation with compile-time strings
const char* GetTitleFromFlags(bonus_flags_t bonus_flags) noexcept {
	static constexpr struct {
		bonus_flags_t flags;
		const char* title;
	} common_titles[] = {
		{BF_CHAMPION, "Champion "},
		{BF_CORRUPTED, "Corrupted "},
		{BF_RAGEQUITTER, "Ragequitter "},
		{BF_BERSERKING, "Berserking "},
		{BF_POSSESSED, "Possessed "},
		{BF_STYGIAN, "Stygian "},
		{BF_FRIENDLY, "Friendly "},
		{BF_CHAMPION | BF_CORRUPTED, "Champion Corrupted "},
		{BF_BERSERKING | BF_POSSESSED, "Berserking Possessed "},
	};

	if (bonus_flags == BF_NONE) {
		return "";
	}

	for (const auto& entry : common_titles) {
		if (entry.flags == bonus_flags) {
			return entry.title;
		}
	}

	thread_local char title_buffer[64];
	char* ptr = title_buffer;
	const char* const end = title_buffer + sizeof(title_buffer);

	auto append_title = [&](const char* text, size_t len_without_null) {
		if (ptr + len_without_null < end) {
			memcpy(ptr, text, len_without_null);
			ptr += len_without_null;
		}
	};

	if (bonus_flags & BF_CHAMPION)   append_title("Champion ", 9);
	if (bonus_flags & BF_CORRUPTED)  append_title("Corrupted ", 10);
	if (bonus_flags & BF_RAGEQUITTER) append_title("Ragequitter ", 12);
	if (bonus_flags & BF_BERSERKING) append_title("Berserking ", 11);
	if (bonus_flags & BF_POSSESSED)  append_title("Possessed ", 10);
	if (bonus_flags & BF_STYGIAN)    append_title("Stygian ", 8);
	if (bonus_flags & BF_FRIENDLY)   append_title("Friendly ", 9);

	*ptr = '\0';
	return title_buffer;
}

// 10: Pre-computed display names with perfect hash
static std::array<std::string, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_monsterDisplayNames;
static std::array<std::string, static_cast<size_t>(horde::SpecialEntityTypeID::COUNT)> g_specialDisplayNames;
static bool g_displayNamesInitialized = false;

std::string FormatClassname(const std::string& classname);

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
        
        auto it = monster_name_replacements.find(typeId);
        if (it != monster_name_replacements.end()) {
            g_monsterDisplayNames[i] = std::string(it->second);
        } 
        else if (horde::MonsterTypeRegistry::IsValidType(typeId)) {
            const char* classname = horde::MonsterTypeRegistry::GetClassname(typeId);
            g_monsterDisplayNames[i] = FormatClassname(classname);
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
    };
    
    for (size_t i = 0; i < special_count; ++i) {
        g_specialDisplayNames[i] = "Unknown Object";
    }
    
    for (const auto& [typeId, name] : special_mappings) {
        g_specialDisplayNames[static_cast<size_t>(typeId)] = name;
    }

    g_displayNamesInitialized = true;
}

// 11:  display name generation using G_Fmt pattern
const char* GetDisplayName(const edict_t* ent) {
    if (!ent) {
        return "Unknown";
    }

    if (!g_displayNamesInitialized) {
        InitializeDisplayNames();
    }

    const char* base_name_ptr = nullptr;
    auto special_id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);
    auto monster_id = static_cast<horde::MonsterTypeID>(ent->monsterinfo.monster_type_id);

    if (special_id != horde::SpecialEntityTypeID::UNKNOWN) {
        base_name_ptr = g_specialDisplayNames[static_cast<size_t>(special_id)].c_str();
    } else if (ent->svflags & SVF_MONSTER && monster_id != horde::MonsterTypeID::UNKNOWN) {
        base_name_ptr = g_monsterDisplayNames[static_cast<size_t>(monster_id)].c_str();
    } else {
        base_name_ptr = ent->classname ? ent->classname : "Unknown";
    }

    const char* title_ptr = GetTitleFromFlags(ent->monsterinfo.bonus_flags);

    return G_Fmt("{}{}", title_ptr, base_name_ptr).data();
}

// 12:  bonus flag application
void ApplyMonsterBonusFlags(edict_t* monster)
{
	if (!monster || !monster->inuse || monster->monsterinfo.IS_BOSS)
		return;

	const spawn_temp_t& st = ED_GetSpawnTemp();
	
	if (monster->monsterinfo.bonus_flags != BF_NONE && (!(monster->monsterinfo.bonus_flags & BF_FRIENDLY))) {
		if (!st.was_key_specified("power_armor_power"))
			monster->monsterinfo.power_armor_power = static_cast<int>(round(monster->max_health * 0.4f));
		if (!st.was_key_specified("power_armor_type"))
			monster->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
	}

	if (monster->monsterinfo.issummoned) {
		monster->monsterinfo.bonus_flags |= BF_FRIENDLY;
		FindMTarget(monster);
		monster->svflags |= SVF_PLAYER;
		monster->s.renderfx &= ~RF_DOT_SHADOW;

		monster->monsterinfo.team = (monster->owner && monster->owner->client) 
			? monster->owner->client->resp.ctf_team 
			: CTF_NOTEAM;

		gi.linkentity(monster);
	}

	monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;
	monster->gib_health = static_cast<int>(round(monster->gib_health * 2.8f));
	if (monster->gib_health <= -200)
		monster->gib_health = -200;

	const size_t effect_index = GetBonusEffectIndex(monster->monsterinfo.bonus_flags);
	if (effect_index > 0) {
		const BonusEffectData& data = g_bonus_effects_array[effect_index];
		
		monster->s.effects |= data.effects;
		monster->s.renderfx |= data.renderfx;
		if (data.alpha != 1.0f) monster->s.alpha = data.alpha;
		
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

	monster->max_health = monster->health;
	monster->s.renderfx |= RF_IR_VISIBLE;
	gi.linkentity(monster);
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

// 14:  boss effects with compile-time lookup
void ApplyBossEffects(edict_t* boss)
{
	if (!boss->monsterinfo.IS_BOSS || boss->monsterinfo.effects_applied)
		return;

	const BossSizeCategory sizeCategory = boss->bossSizeCategory;
	const size_t effect_index = static_cast<size_t>(irandom(1, 7));
	boss->monsterinfo.bonus_flags = static_cast<bonus_flags_t>(1 << (effect_index - 1));

	const BonusEffectData& data = g_bonus_effects_array[effect_index];
	
	boss->s.effects |= data.effects;
	boss->s.renderfx |= data.renderfx;
	boss->s.alpha = data.alpha;
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
		boss->s.scale *= scale_factor;
		boss->mass *= scale_factor;
		float height_offset = -(boss->mins[2]);
		boss->s.origin[2] += height_offset;
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
    thread_local struct {
        const edict_t* last_player = nullptr;
        char cached_name[MAX_INFO_VALUE];
        gtime_t cache_time = 0_sec;
    } name_cache;
    
    static constexpr gtime_t CACHE_DURATION = 1_sec;
    
    if (!player || !player->client || !player->inuse) { 
        return "N/A";
    }
    
    if (name_cache.last_player == player && 
        level.time - name_cache.cache_time < CACHE_DURATION) {
        return name_cache.cached_name;
    }
    
    size_t written = gi.Info_ValueForKey(player->client->pers.userinfo, "name",
        name_cache.cached_name, sizeof(name_cache.cached_name) - 1);
    
    if (written == 0 || written >= sizeof(name_cache.cached_name)) {
        return "N/A";
    }
    
    name_cache.cached_name[written] = '\0';
    name_cache.last_player = player;
    name_cache.cache_time = level.time;
    
    return name_cache.cached_name;
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

	constexpr int NUM_SECONDARY_EFFECTS = 12;
	constexpr float RANDOM_OFFSET_RANGE = 255.0f;
	constexpr float EFFECT_SCALE = 0.55f;
	const float scaled_start = start_size * EFFECT_SCALE;
	const float scaled_end = end_size * EFFECT_SCALE;

	for (int i = 0; i < NUM_SECONDARY_EFFECTS; i++) {
		vec3_t offset;
		offset.x = position.x + crandom() * RANDOM_OFFSET_RANGE;
		offset.y = position.y + crandom() * RANDOM_OFFSET_RANGE;
		offset.z = position.z + crandom() * RANDOM_OFFSET_RANGE;
		
		SpawnGrow_Spawn(offset, scaled_start, scaled_end);
	}

	edict_t* earthquake = G_Spawn();
	if (earthquake) {
		earthquake->classname = "target_earthquake";
		earthquake->spawnflags = brandom() 
			? SPAWNFLAGS_EARTHQUAKE_SILENT 
			: SPAWNFLAGS_EARTHQUAKE_ONE_SHOT;
		earthquake->speed = 500.0f;
		earthquake->count = 12;

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
	ent->s.origin.z += 10;
	ent->velocity = vec3_origin;

	if (ent->client) {
		ent->client->ps.pmove.pm_time = 160;
		ent->client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;
		ent->s.event = EV_PLAYER_TELEPORT;

		ent->client->ps.viewangles = dest->s.angles;
		ent->client->v_angle = dest->s.angles;
		ent->s.angles = dest->s.angles;

		for (int i = 0; i < 3; i++) {
			ent->client->ps.pmove.origin[i] = static_cast<int16_t>(ent->s.origin[i] * 8.0f);
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
bool EntitiesOverlap(const edict_t* ent, const vec3_t& area_mins, const vec3_t& area_maxs) {
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

	constexpr vec3_t safe_offset{26.0f, 26.0f, 26.0f};
	const vec3_t area_mins = origin + mins - safe_offset;
	const vec3_t area_maxs = origin + maxs + safe_offset;

	const vec3_t half_size = (area_maxs - area_mins) * 0.5f;
	const float radius_to_contain_box = half_size.length();
	constexpr float MAX_ENTITY_REACH = 128.0f;
	const float safe_radius = radius_to_contain_box + MAX_ENTITY_REACH;

	constexpr size_t MAX_STACK_ENTITIES = 32;
	edict_t* stack_entities[MAX_STACK_ENTITIES];
	std::vector<edict_t*> heap_entities;
	
	edict_t** entities_array = stack_entities;
	size_t entity_count = 0;
	size_t capacity = MAX_STACK_ENTITIES;

	edict_t* ent = nullptr;
	while ((ent = findradius(ent, origin, safe_radius)) != nullptr) {
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

		if (entity_count >= capacity) {
			if (entities_array == stack_entities) {
				heap_entities.reserve(capacity * 2);
				heap_entities.assign(stack_entities, stack_entities + entity_count);
				entities_array = heap_entities.data();
				capacity *= 2;
				heap_entities.resize(capacity);
			}
		}
		entities_array[entity_count++] = ent;
	}

	for (size_t i = 0; i < entity_count; ++i) {
		edict_t* current_ent = entities_array[i];
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

	constexpr size_t MAX_ENTITIES = 64;
	edict_t* processable_entities[MAX_ENTITIES];
	edict_t* removable_entities[MAX_ENTITIES];
	size_t processable_count = 0;
	size_t removable_count = 0;

	for (edict_t* ent = nullptr; (ent = findradius(ent, center, search_radius)) != nullptr;) {
		if (!ent || !ent->inuse || 
			gi.traceline(center, ent->s.origin, nullptr, MASK_SOLID).fraction < 1.0f) {
			continue;
		}

		if (IsRemovableEntity(ent)) {
			if (removable_count < MAX_ENTITIES) {
				removable_entities[removable_count++] = ent;
			}
		} else if (ent->takedamage && processable_count < MAX_ENTITIES) {
			processable_entities[processable_count++] = ent;
		}
	}

	for (size_t i = 0; i < removable_count; ++i) {
		if (removable_entities[i] && removable_entities[i]->inuse) {
			RemoveEntity(removable_entities[i]);
		}
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
[[nodiscard]] constexpr bool string_equals(const char* str1, std::string_view str2) noexcept {
	if (!str1) return false;
	const size_t str1_len = strlen(str1);
	return str1_len == str2.length() && 
		   !Q_strncasecmp(str1, str2.data(), str2.length());
}

// 23:  monster name replacements with perfect hash
const std::unordered_map<horde::MonsterTypeID, std::string_view> monster_name_replacements = {
    {horde::MonsterTypeID::SOLDIER_LIGHT, "Blaster Guard"},
    {horde::MonsterTypeID::SOLDIER, "SG Guard"},
    {horde::MonsterTypeID::SOLDIER_SS, "SS Guard"},
    {horde::MonsterTypeID::SOLDIER_HYPERGUN, "Hyper Guard"},
    {horde::MonsterTypeID::SOLDIER_LASERGUN, "Laser Guard"},
    {horde::MonsterTypeID::SOLDIER_RIPPER, "Ripper Guard"},
    {horde::MonsterTypeID::INFANTRY_VANILLA, "Infantry"},
    {horde::MonsterTypeID::INFANTRY, "Enforcer"},
    {horde::MonsterTypeID::GUNNER_VANILLA, "Gunner"},
    {horde::MonsterTypeID::GUNNER, "Heavy Gunner"},
    {horde::MonsterTypeID::GUNCMDR_VANILLA, "Gunner Commander"},
    {horde::MonsterTypeID::GUNCMDR, "Gunner Grenadier"},
    {horde::MonsterTypeID::GUNCMDR_KL, "Gunner Commander"},
    {horde::MonsterTypeID::FLYER, "Flyer"},
    {horde::MonsterTypeID::KAMIKAZE, "Kamikaze Flyer"},
    {horde::MonsterTypeID::HOVER_VANILLA, "Blaster Icarus"},
    {horde::MonsterTypeID::HOVER, "Rocket Icarus"},
    {horde::MonsterTypeID::DAEDALUS, "Daedalus"},
    {horde::MonsterTypeID::DAEDALUS_BOMBER, "Bomber Daedalus"},
    {horde::MonsterTypeID::FLOATER, "Technician"},
    {horde::MonsterTypeID::FLOATER_TRACKER, "DarkMatter Technician"},
    {horde::MonsterTypeID::MEDIC, "Medic"},
    {horde::MonsterTypeID::MEDIC_COMMANDER, "Medic Commander"},
    {horde::MonsterTypeID::FIXBOT, "Fixbot"},
    {horde::MonsterTypeID::FIXBOT_KL, "Fixer"},
    {horde::MonsterTypeID::MUTANT, "Mutant"},
    {horde::MonsterTypeID::REDMUTANT, "Raged Mutant"},
    {horde::MonsterTypeID::BERSERK, "Berserker"},
    {horde::MonsterTypeID::GEKK, "Gekk"},
    {horde::MonsterTypeID::PARASITE, "Parasite"},
    {horde::MonsterTypeID::PERRO_KL, "Infected Parasite"},
    {horde::MonsterTypeID::STALKER, "Stalker"},
    {horde::MonsterTypeID::BRAIN, "Brain"},
    {horde::MonsterTypeID::CHICK, "Iron Maiden"},
    {horde::MonsterTypeID::CHICK_HEAT, "Iron Praetor"},
    {horde::MonsterTypeID::TANK, "Tank"},
    {horde::MonsterTypeID::TANK_64, "N64 Tank"},
    {horde::MonsterTypeID::TANK_COMMANDER, "Tank Commander"},
    {horde::MonsterTypeID::TANK_SPAWNER, "Spawner Tank"},
    {horde::MonsterTypeID::RUNNERTANK, "BETA Runner Tank"},
    {horde::MonsterTypeID::GLADIATOR, "Gladiator"},
    {horde::MonsterTypeID::GLADIATOR_B, "DarkMatter Gladiator"},
    {horde::MonsterTypeID::GLADIATOR_C, "Plasma Gladiator"},
    {horde::MonsterTypeID::SPIDER, "Plasma Spider"},
    {horde::MonsterTypeID::ARACHNID, "Arachnid"},
    {horde::MonsterTypeID::ARACHNID2, "Arachnid"},
    {horde::MonsterTypeID::PSX_ARACHNID, "Arachnid"},
    {horde::MonsterTypeID::GM_ARACHNID, "Guided-Missile Arachnid"},
    {horde::MonsterTypeID::SHAMBLER, "Shambler"},
    {horde::MonsterTypeID::SHAMBLER_SMALL, "Tiny Shambler!"},
    {horde::MonsterTypeID::SHAMBLER_KL, "Shambler"},
    {horde::MonsterTypeID::BOSS2, "Hornet"},
    {horde::MonsterTypeID::BOSS2_64, "N64 Mini Hornet"},
    {horde::MonsterTypeID::BOSS2_MINI, "Mini Hornet"},
    {horde::MonsterTypeID::BOSS2_KL, "N64 Hornet"},
    {horde::MonsterTypeID::CARRIER, "Carrier"},
    {horde::MonsterTypeID::CARRIER_MINI, "Mini Carrier"},
    {horde::MonsterTypeID::MAKRON, "Makron"},
    {horde::MonsterTypeID::MAKRON_KL, "Makron"},
    {horde::MonsterTypeID::JORG, "Jorg"},
    {horde::MonsterTypeID::JORG_SMALL, "Mini Jorg"},
    {horde::MonsterTypeID::WIDOW, "Widow Battle-Maiden"},
    {horde::MonsterTypeID::WIDOW1, "Black Widow"},
    {horde::MonsterTypeID::WIDOW2, "Widow Creator"},
    {horde::MonsterTypeID::GUARDIAN, "Guardian"},
    {horde::MonsterTypeID::PSX_GUARDIAN, "Guardian"},
    {horde::MonsterTypeID::JANITOR, "Janitor"},
    {horde::MonsterTypeID::JANITOR2, "Mini Guardian"},
    {horde::MonsterTypeID::SUPERTANK, "Super-Tank"},
    {horde::MonsterTypeID::SUPERTANKKL, "Super-Tank"},
    {horde::MonsterTypeID::BOSS5, "Super-Tank"},
    {horde::MonsterTypeID::SENTRYGUN, "Sentry-Gun"},
    {horde::MonsterTypeID::TURRET, "Turret"},
    {horde::MonsterTypeID::MISC_INSANE, "Insane Grunt"},
	{horde::MonsterTypeID::FLIPPER, "Flipper"}
};

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
		if (developer->integer) {
			gi.Com_PrintFmt("TeleportSelf WARNING: No valid spawn points found for teleport.\n");
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
		size_t random_index = static_cast<size_t>(irandom(g_num_spawn_points));
		perform_teleport_actions(g_spawn_point_list[random_index]);
	}

	if (was_emergency) {
		ent->client->emergency_teleport = false;
	}

	return true;
}

// 25:  jump move detection with compile-time initialization
static std::vector<const mmove_t*> g_jump_moves;

void InitializeMonsterMoveSets() {
    if (!g_jump_moves.empty()) {
        return;
    }

    g_jump_moves.reserve(32);
    
    const mmove_t* jump_moves[] = {
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

    g_jump_moves.assign(std::begin(jump_moves), std::end(jump_moves));
    std::sort(g_jump_moves.begin(), g_jump_moves.end());
}

bool IsMonsterJumping(const edict_t* self) {
    if (!self || !self->monsterinfo.active_move) {
        return false;
    }

    const mmove_t* current_move = self->monsterinfo.active_move.pointer();
    return std::binary_search(g_jump_moves.cbegin(), g_jump_moves.cend(), current_move);
}