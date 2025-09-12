#include "../shared.h"
#include <queue>
#include <sstream>
#include <span>

constexpr gtime_t TESLA_TIME_TO_LIVE = gtime_t::from_sec(60);

std::string FormatClassname(const std::string& classname) {
	std::string result;
	result.reserve(classname.length());

	std::stringstream ss(classname);
	std::string segment;
	bool first_word = true;

	while (std::getline(ss, segment, '_')) {
		if (!first_word) result += ' ';
		if (!segment.empty()) {
			segment[0] = toupper(segment[0]);
			result += segment;
		}
		first_word = false;
	}

	return result;
}


bool IsValidTarget(edict_t* ent, edict_t* other, bool check_visibility) {
	if (!other || !other->inuse || !other->takedamage || other->solid == SOLID_NOT) {
		return false;
	}
	if (other == ent || (other->svflags & SVF_DEADMONSTER)) {
		return false;
	}
	if (check_visibility && ent && !visible(ent, other)) {
		return false;
	}

    // A valid target is a player, a known monster, or a known special entity.
    return (other->client ||
            other->monsterinfo.monster_type_id != static_cast<uint8_t>(horde::MonsterTypeID::UNKNOWN) ||
            other->special_type_id != static_cast<uint8_t>(horde::SpecialEntityTypeID::UNKNOWN));
}

int GetArmorInfo(edict_t* ent) {
	if (ent->svflags & SVF_MONSTER) {
		return ent->monsterinfo.power_armor_power;
	}

	if (!ent->client) {
		return 0;
	}

	int const index = ArmorIndex(ent);
	return (index != IT_NULL) ? ent->client->pers.inventory[index] : 0;
}


template<typename Duration>
int GetRemainingTime(gtime_t current_time, gtime_t end_time) {
	return std::max(0, static_cast<int>((end_time - current_time).template seconds<float>()));
}

enum class EntityType {
	Player,
	Monster,
	Other
};

EntityType GetEntityType(const edict_t* ent) {
	if (!ent) return EntityType::Other;
	if (ent->client) return EntityType::Player;
	if (ent->svflags & SVF_MONSTER) return EntityType::Monster;
	return EntityType::Other;
}
// // This function now uses the fully refactored ID-based display name system.
// Corrected version of the function
const char* FormatEntityInfo_Fast(edict_t* ent) {
    static char info_buffer[256];

    if (!ent || !ent->inuse) {
        info_buffer[0] = '\0';
        return info_buffer;
    }

    // Use a pointer to the start of the buffer as our output iterator
    char* out = info_buffer;
    char* const end = info_buffer + sizeof(info_buffer);

    const EntityType type = GetEntityType(ent);

    switch (type) {
    case EntityType::Monster: {
        const char* full_name = GetDisplayName_Fast(ent);
        
        
        out = fmt::format_to_n(out, static_cast<size_t>(end - out), "{}\nH: {}", full_name, ent->health).out;

        if (ent->monsterinfo.armor_power >= 1) {
            
            out = fmt::format_to_n(out, static_cast<size_t>(end - out), " A: {}", ent->monsterinfo.armor_power).out;
        }
        if (ent->monsterinfo.power_armor_power >= 1) {
            
            out = fmt::format_to_n(out, static_cast<size_t>(end - out), " PA: {}", ent->monsterinfo.power_armor_power).out;
        }

        struct PowerupInfo { gtime_t time; const char* label; };
        std::array<PowerupInfo, 4> powerups{{
            {ent->monsterinfo.quad_time, "Quad"},
            {ent->monsterinfo.double_time, "Double"},
            {ent->monsterinfo.invincible_time, "Invuln"},
            {ent->monsterinfo.quadfire_time, "Accel"}
        }};
        for (const auto& powerup : powerups) {
            if (powerup.time > level.time) {
                
                out = fmt::format_to_n(out, static_cast<size_t>(end - out), "\n{}: {}s", powerup.label, GetRemainingTime<float>(level.time, powerup.time)).out;
            }
        }
        break;
    }

    case EntityType::Player: {
        const char* playerName = GetPlayerName_Fast(ent);
        
        out = fmt::format_to_n(out, static_cast<size_t>(end - out), "{}\nH: {}", playerName, ent->health).out;

        int armor_value = GetArmorInfo(ent);
        if (armor_value > 0) {
            
            out = fmt::format_to_n(out, static_cast<size_t>(end - out), " A: {}", armor_value).out;
        }
        break;
    }

    case EntityType::Other: {
        auto special_id = static_cast<horde::SpecialEntityTypeID>(ent->special_type_id);
        if (special_id == horde::SpecialEntityTypeID::UNKNOWN) {
            info_buffer[0] = '\0';
            return info_buffer;
        }

        edict_t* stats_source = ent;
        if (special_id == horde::SpecialEntityTypeID::DOPPLEGANGER && ent->teammaster && ent->teammaster->inuse) {
            stats_source = ent;
        }

        const char* name = GetDisplayName_Fast(ent);
        
        switch (special_id) {
            case horde::SpecialEntityTypeID::TESLA_MINE: {
                gtime_t const time_active = level.time - stats_source->timestamp;
                gtime_t const time_remaining = TESLA_TIME_TO_LIVE - time_active;
                
                out = fmt::format_to_n(out, static_cast<size_t>(end - out), "{}\nH: {} T: {}s", name, stats_source->health, GetRemainingTime<float>(gtime_t{}, time_remaining)).out;
                break;
            }
            case horde::SpecialEntityTypeID::FOOD_CUBE_TRAP: {
                gtime_t time_remaining = (stats_source->timestamp > level.time) ? (stats_source->timestamp - level.time) : 0_sec;
                
                out = fmt::format_to_n(out, static_cast<size_t>(end - out), "{}\nH: {} T: {}s", name, stats_source->health, GetRemainingTime<float>(gtime_t{}, time_remaining)).out;
                break;
            }
            case horde::SpecialEntityTypeID::LASER_EMITTER: {
                edict_t* beam = stats_source->chain;
                int health_to_display = (beam && beam->inuse) ? beam->health : 0;
                
                out = fmt::format_to_n(out, static_cast<size_t>(end - out), "{}\nH: {}", name, health_to_display).out;
                if (beam && beam->inuse) {
                    
                    out = fmt::format_to_n(out, static_cast<size_t>(end - out), " DMG: {}", beam->dmg).out;
                    gtime_t time_remaining = (stats_source->timestamp > level.time) ? (stats_source->timestamp - level.time) : 0_sec;
                    
                    out = fmt::format_to_n(out, static_cast<size_t>(end - out), " T: {}s", GetRemainingTime<float>(gtime_t{}, time_remaining)).out;
                }
                break;
            }
            case horde::SpecialEntityTypeID::DOPPLEGANGER: {
                gtime_t const time_remaining = stats_source->nextthink - level.time;
                
                out = fmt::format_to_n(out, static_cast<size_t>(end - out), "{}\nH: {} T: {}s", name, stats_source->health, GetRemainingTime<float>(gtime_t{}, time_remaining)).out;
                break;
            }
            default:
                
                out = fmt::format_to_n(out, static_cast<size_t>(end - out), "{}\nH: {}", name, stats_source->health).out;
                break;
        }
        break;
    }
    }

    // Null-terminate the buffer safely.
    if (out < end) {
        *out = '\0';
    } else {
        *(end - 1) = '\0'; // Ensure termination even if buffer was completely filled
    }

    return info_buffer;
}

struct IDViewConfig {
	static constexpr gtime_t UPDATE_INTERVAL = 108_ms; // How often to run the check (about 9 times/sec)
	static constexpr float MAX_DISTANCE = 2048.0f;     // Max distance to identify a target
	static constexpr float MIN_DOT = 0.98f;            // How close to the crosshair a target must be (higher is stricter)
	static constexpr float CLOSE_DISTANCE = 100.0f;    // A closer distance for a wider check angle
	static constexpr float CLOSE_MIN_DOT = 0.5f;       // A wider angle for very close targets
};

[[nodiscard]] bool IsInFieldOfView(const vec3_t& viewer_pos, const vec3_t& viewer_forward,
	const vec3_t& target_pos, float min_dot, float max_distance) noexcept {
	vec3_t dir = target_pos - viewer_pos;
	float const dist = dir.normalize();
	return dist < max_distance && viewer_forward.dot(dir) > min_dot;
}

[[nodiscard]] bool CanSeeTarget(const edict_t* viewer, const vec3_t& start,
	const edict_t* target, const vec3_t& end) noexcept {
	trace_t const tr = gi.traceline(start, end, viewer, MASK_SOLID);
	return tr.fraction == 1.0f || tr.ent == target;
}

struct TargetSearchResult {
	edict_t* target{ nullptr };
	float distance::MAX_DISTANCE };
};

// Optimized Linear Scan
[[nodiscard]] TargetSearchResult FindBestTarget(edict_t* ent, const vec3_t& forward) noexcept {
    TargetSearchResult result;
    vec3_t const& viewer_pos = ent->s.origin;
    
    edict_t* best_candidate = nullptr;
    float best_score = -1.0f; // Use a score instead of just distance

    auto checkEntity = [&](edict_t* who) {
        if (!IsValidTarget(ent, who, false)) { // The 'false' skips the visibility check in IsValidTarget
            return;
        }

        vec3_t dir = who->s.origin - viewer_pos;
        float const dist_sq = dir.lengthSquared(); // Use squared distance to avoid sqrt

        // Use a generous max distance check to quickly discard far away entities
        static constexpr float MAX_DISTANCE_SQ ::MAX_DISTANCE ::MAX_DISTANCE;
        if (dist_sq > MAX_DISTANCE_SQ) {
            return;
        }

        dir.normalize(); // Normalize only after distance check
        float const dot = forward.dot(dir);

        // Determine the minimum required dot product based on distance
        static constexpr float CLOSE_DISTANCE_SQ ::CLOSE_DISTANCE ::CLOSE_DISTANCE;
        float const min_dot = (dist_sq < CLOSE_DISTANCE_SQ)
            ::CLOSE_MIN_DOT
            ::MIN_DOT;

        if (dot < min_dot) {
            return;
        }

        // Calculate a score. Higher dot product is better, lower distance is better.
        // We prioritize dot product heavily.
        float score = (dot * 1000.0f) - sqrtf(dist_sq); // sqrt is slow, but we only do it for valid candidates
        if (score > best_score) {
            best_score = score;
            best_candidate = who;
        }
    };

    // --- The same iteration logic as before ---
    for (edict_t* who : active_players()) {
        checkEntity(who);
    }
    for (edict_t* who : active_monsters()) {
        checkEntity(who);
    }
    for (edict_t* who = g_edicts + 1; who < g_edicts + globals.num_edicts; who++) {
        if (who->client || (who->svflags & SVF_MONSTER)) continue;
        checkEntity(who);
    }

 