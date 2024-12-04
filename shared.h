#pragma once

#ifndef SHARED_H
#define SHARED_H

#include "g_local.h"

constexpr int ADRENALINE_HEALTH_BONUS = 5;
#include <string>

// Replace the macro with a constexpr function for better type safety and debugging
[[nodiscard]] constexpr bool IsFirstThreeWaves(int32_t wave_level) noexcept {
    return wave_level <= 3;
}

enum bonus_flags_t : uint32_t {
    BF_NONE = 0,
    BF_CHAMPION = bit_v<0>,      // 1 << 0
    BF_CORRUPTED = bit_v<1>,     // 1 << 1
    BF_RAGEQUITTER = bit_v<2>,   // 1 << 2
    BF_BERSERKING = bit_v<3>,    // 1 << 3
    BF_POSSESSED = bit_v<4>,      // 1 << 4
    BF_STYGIAN = bit_v<5>,        // 1 << 5
    BF_FRIENDLY = bit_v<6>        // 1 << 6
};

MAKE_ENUM_BITFLAGS(bonus_flags_t);

// boss stuff

enum class BossTeleportReason {
    DROWNING,
    TRIGGER_HURT,
//    STUCK
};

bool CheckAndTeleportBoss(edict_t* boss, const BossTeleportReason reason);

bool M_AdjustBlindfireTarget(edict_t* self, const vec3_t& start, const vec3_t& target, const vec3_t& right, vec3_t& out_dir);
void ImprovedSpawnGrow(const vec3_t& position, float start_size, float end_size, edict_t* spawned_entity);
void ClearSpawnArea(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs);
extern void SetHealthBarName(const edict_t* boss);

// Declarar funciones globales name strings
std::string GetDisplayName(const char* classname);
std::string GetDisplayName(const edict_t* ent);

// Mantén el mapa de nombres como una variable externa
extern const std::unordered_map<std::string_view, std::string_view> name_replacements;
std::string GetTitleFromFlags(int bonus_flags);

//DMG & POWERUP
void ApplyMonsterBonusFlags(edict_t* monster);
void ApplyBossEffects(edict_t* boss);
//extern [[nodiscard]] constexpr float M_DamageModifier(edict_t* monster) noexcept;
void UpdatePowerUpTimes(edict_t* monster);

std::string GetPlayerName(const edict_t* player);
// healthbar
extern void SP_target_healthbar(edict_t* self);
extern void use_target_healthbar(edict_t* self, edict_t* other, edict_t* activator);
extern void check_target_healthbar(edict_t* self);

void Monster_MoveSpawn(edict_t* self); 
void ConfigureBossArmor(edict_t* self);
void PushEntitiesAway(const vec3_t& center, int num_waves, float push_radius, float push_strength, float horizontal_push_strength, float vertical_push_strength);
struct MapSize {
    bool isSmallMap = false;
    bool isMediumMap = false;
    bool isBigMap = false;
};

MapSize GetMapSize(const char* mapname);

extern gtime_t GetWaveTimer();
bool TeleportSelf(edict_t* ent);
inline int8_t GetNumHumanPlayers();

// Estructura para almacenar las estad√≠sticas de los jugadores
struct PlayerStats {
    edict_t* player = nullptr;
    int32_t total_damage = 0;
};

void ApplyGradualHealing(edict_t* ent);

void AllowReset() noexcept;

bool CheckAndTeleportStuckMonster(edict_t* self);
bool G_IsClearPath(const edict_t* ignore, contents_t mask, const vec3_t& spot1, const vec3_t& spot2);

extern gtime_t horde_message_end_time;
extern void CheckAndUpdateMenus();
extern void CheckAndResetDisabledSpawnPoints();
extern void CheckAndRestoreMonsterAlpha(edict_t* const ent);

// Estructura para pasar datos adicionales a la función de filtro
struct FilterData {
    const edict_t* ignore_ent;
    int count;
};
//weapon
void SpawnClusterGrenades(edict_t* owner, const vec3_t& origin, int base_damage);

//void UpdateHordeMessage(std::string_view message, gtime_t duration);


#endif // SHARED_H
