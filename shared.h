#pragma once

#include "g_local.h"
#include <string>
#include "horde/horde_ids.h"
#include "horde/horde_ids.h"

constexpr int ADRENALINE_HEALTH_BONUS = 5;
constexpr float VECTOR_LENGTH_SQ_EPSILON = 0.0001f * 0.0001f;

void InitializeMonsterMoveSets();
// Replace the macro with a constexpr function for better type safety and debugging
[[nodiscard]] constexpr bool IsFirstThreeWaves(int32_t wave_level) noexcept {
    return wave_level <= 3;
}

// boss stuff

enum class BossTeleportReason {
    DROWNING,
    TRIGGER_HURT,
//    STUCK
};
void InitializeMonsterMoveSets();
bool M_AdjustBlindfireTarget(edict_t* self, const vec3_t& start, const vec3_t& target, const vec3_t& right, vec3_t& out_dir);
bool CheckAndTeleportBoss(edict_t* boss, const BossTeleportReason reason);
void ImprovedSpawnGrow(const vec3_t& position, float start_size, float end_size, edict_t* spawned_entity);
void ClearSpawnArea(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs);
extern void SetHealthBarName(const edict_t* boss);

// Mantén el mapa de nombres como una variable externa
extern const std::unordered_map<horde::MonsterTypeID, std::string_view> monster_name_replacements;
std::string GetTitleFromFlags(unsigned int bonus_flags);

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
extern void HandleSpawnPhaseAggression(edict_t* monster);
extern void Monster_MoveSpawn(edict_t* self); 
extern void ConfigureBossArmor(edict_t* self);
extern void PushEntitiesAway(const vec3_t& center, int num_waves, float push_radius, float push_strength, float horizontal_push_strength, float vertical_push_strength);
extern horde::MapSize GetMapSize(const char* mapname);
extern gtime_t GetWaveTimer();
extern int32_t GetStroggsNum() noexcept;
extern inline bool IsBossWave() noexcept;

bool TeleportSelf(edict_t* ent);
//extern int8_t GetNumHumanPlayers();

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

extern bool CTFCheckTimeExtensionVote();
extern void ClearHordeMessage();
bool IsPlayerDefense(const edict_t* ent);
extern void StartFadeOut(edict_t* ent);
extern bool IsMonsterJumping(const edict_t* self);

extern bool Horde_TeleportMonster(edict_t* self, const vec3_t& destination_origin, const vec3_t& destination_angles, bool play_effects, bool force_despite_visibility);

extern std::string GetPlayerName(const edict_t* player);
extern std::string GetDisplayName(const edict_t* ent);
extern std::string GetTitleFromFlags(unsigned int bonus_flags);
extern void VerifyAndAdjustBots();