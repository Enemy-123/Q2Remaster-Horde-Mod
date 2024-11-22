#pragma once

#ifndef SHARED_H
#define SHARED_H

#include "g_local.h"

constexpr int ADRENALINE_HEALTH_BONUS = 5;
#include <string>
#define first3waves current_wave_level <= 3
// Define los flags de bonus
#define BF_CHAMPION   0x00000001
#define BF_CORRUPTED  0x00000002
#define BF_RAGEQUITTER 0x00000004
#define BF_BERSERKING 0x00000008
#define BF_POSSESSED  0x00000010
#define BF_STYGIAN    0x00000020

// boss stuff
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

std::string GetPlayerName(edict_t* player);
// healthbar
extern void SP_target_healthbar(edict_t* self);
extern void use_target_healthbar(edict_t* self, edict_t* other, edict_t* activator);
extern void check_target_healthbar(edict_t* self);

void Monster_MoveSpawn(edict_t* self); 
void PushEntitiesAway(const vec3_t& center, int num_waves, float push_radius, float push_strength, float horizontal_push_strength, float vertical_push_strength);
struct MapSize {
    bool isSmallMap = false;
    bool isMediumMap = false;
    bool isBigMap = false;
};

MapSize GetMapSize(const std::string& mapname) ;

extern gtime_t GetWaveTimer();
bool TeleportSelf(edict_t* ent);
inline int8_t GetNumHumanPlayers();

// Estructura para almacenar las estad√≠sticas de los jugadores
struct PlayerStats {
    edict_t* player = nullptr;
    int32_t total_damage = 0;
};

void AllowReset();

bool CheckAndTeleportStuckMonster(edict_t* self);
bool G_IsClearPath(const edict_t* ignore, contents_t mask, const vec3_t& spot1, const vec3_t& spot2);

extern gtime_t horde_message_end_time;
void CheckAndUpdateMenus();
void CheckAndResetDisabledSpawnPoints();
void CheckAndRestoreMonsterAlpha(edict_t* const ent);

//void UpdateHordeMessage(std::string_view message, gtime_t duration);


#endif // SHARED_H
