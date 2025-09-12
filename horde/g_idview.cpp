#include "../shared.h"
#include <queue>
#include <sstream>
#include <span>
#include <array> // Included for std::array
#include <cmath> // Included for sqrtf

// Assuming gtime_t and other game-specific types are defined in "shared.h"
// Also assuming fmt library is available and configured.

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

const char* FormatEntityInfo_Fast(edict_t* ent) {
    static char info_buffer[256];

    if (!ent || !ent->inuse) {
        info_buffer[0] = '\0';
        return info_buffer;
    }

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

    if (out < end) {
        *out = '\0';
    } else {
        *(end - 1) = '\0';
    }

    return info_buffer;
}

struct IDViewConfig {
	static constexpr gtime_t UPDATE_INTERVAL = 108_ms;
	static constexpr float MAX_DISTANCE = 2048.0f;
	static constexpr float MIN_DOT = 0.98f;
	static constexpr float CLOSE_DISTANCE = 100.0f;
	static constexpr float CLOSE_MIN_DOT = 0.5f;
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
	float distance{ IDViewConfig::MAX_DISTANCE };
};

[[nodiscard]] TargetSearchResult FindBestTarget(edict_t* ent, const vec3_t& forward) noexcept {
    TargetSearchResult result;
    vec3_t const& viewer_pos = ent->s.origin;
    
    edict_t* best_candidate = nullptr;
    float best_score = -1.0f;

    auto checkEntity = [&](edict_t* who) {
        if (!IsValidTarget(ent, who, false)) {
            return;
        }

        vec3_t dir = who->s.origin - viewer_pos;
        float const dist_sq = dir.lengthSquared();

        static constexpr float MAX_DISTANCE_SQ = IDViewConfig::MAX_DISTANCE * IDViewConfig::MAX_DISTANCE;
        if (dist_sq > MAX_DISTANCE_SQ) {
            return;
        }

        dir.normalize();
        float const dot = forward.dot(dir);

        static constexpr float CLOSE_DISTANCE_SQ = IDViewConfig::CLOSE_DISTANCE * IDViewConfig::CLOSE_DISTANCE;
        float const min_dot = (dist_sq < CLOSE_DISTANCE_SQ)
            ? IDViewConfig::CLOSE_MIN_DOT
            : IDViewConfig::MIN_DOT;

        if (dot < min_dot) {
            return;
        }

        float score = (dot * 1000.0f) - sqrtf(dist_sq);
        if (score > best_score) {
            best_score = score;
            best_candidate = who;
        }
    };

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

    if (best_candidate) {
        result.target = best_candidate;
        vec3_t dir = best_candidate->s.origin - viewer_pos;
        result.distance = dir.length();
    }
    
    // 
    return result;
} //


void SetIDView(edict_t* ent) {
	// Throttling: Exit early if the function was called recently for this client,
	// or if the game is in intermission. This is the primary performance control.
	if (level.intermissiontime ||
		level.time - ent->client->resp.lastidtime < IDViewConfig::UPDATE_INTERVAL) {
		return;
	}

	// Update the timestamp for the next throttled call.
	ent->client->resp.lastidtime = level.time;

	// Determine the unique configstring index for this client's HUD.
	int const client_cs = CONFIG_ID_PLAYER_NAME + (ent - g_edicts - 1);

	// Get the player's view direction.
	vec3_t forward;
	AngleVectors(ent->client->v_angle, forward, nullptr, nullptr);

	// Find the best new target in the player's field of view.
	TargetSearchResult result = FindBestTarget(ent, forward);
	edict_t* current_target = result.target;

	// "Sticky Target" logic: If no new target is found, but the previous target is still
	// valid and visible, keep it selected. This improves user experience by reducing flicker.
	if (!current_target && ent->client->idtarget && IsValidTarget(ent, ent->client->idtarget, true)) {
		current_target = ent->client->idtarget;
	}

	// If the final target is different from the one we have cached, update the cache.
	if (ent->client->idtarget != current_target) {
		ent->client->idtarget = current_target;

		// If there's no longer a target, clear the HUD display and we are done.
		if (!current_target) {
			gi.configstring(client_cs, "");
			ent->client->ps.stats[STAT_TARGET_HEALTH_STRING] = 0;
			return;
		}
	}

	// If we have a valid target (either new or sticky), format its info and update the HUD.
	// This block is now executed every throttled frame for a selected target, ensuring
	// that dynamic data like health and armor is kept up-to-date.
	if (current_target) {
		 const char* info = FormatEntityInfo_Fast(current_target);
		if (info && info[0] != '\0') {
			// The game engine is smart enough not to send a network update
			// if the configstring content hasn't changed.
			gi.configstring(client_cs, info);
			ent->client->ps.stats[STAT_TARGET_HEALTH_STRING] = client_cs;
		}
		else {
			ent->client->idtarget = nullptr;
			gi.configstring(client_cs, "");
			ent->client->ps.stats[STAT_TARGET_HEALTH_STRING] = 0;
		}
	}
}