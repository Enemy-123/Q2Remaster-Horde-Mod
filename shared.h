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
constexpr spawnflags_t SPAWNFLAG_IS_BOSS = spawnflags_t(0x00000025); // Is monster a boss?
constexpr spawnflags_t SPAWNFLAG_BOSS_DEATH_HANDLED = spawnflags_t(0x80000000); // is dead?
void ImprovedSpawnGrow(const vec3_t& position, float start_size, float end_size, edict_t* spawned_entity);

void ClearSpawnArea(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs);

// Declarar funciones globales name strings
std::string GetDisplayName(const char* classname);
std::string GetDisplayName(const edict_t* ent);
std::string GetDisplayName(const std::string& classname);

// Mantén el mapa de nombres como una variable externa
extern const std::unordered_map<std::string_view, std::string_view> name_replacements;

std::string GetTitleFromFlags(int bonus_flags);
//DMG & POWERUP
void ApplyMonsterBonusFlags(edict_t* monster);
void ApplyBossEffects(edict_t* boss);
extern float M_DamageModifier(edict_t* monster);
extern std::string GetPlayerName(edict_t* player);
// Declarar funciones externas para el healthbar
extern void SP_target_healthbar(edict_t* self);
extern void use_target_healthbar(edict_t* self, edict_t* other, edict_t* activator);
extern void check_target_healthbar(edict_t* self);
extern void UpdatePowerUpTimes(edict_t* monster);
extern bool G_IsClearPath(const edict_t* ignore, contents_t mask, const vec3_t& spot1, const vec3_t& spot2);
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

// Estructura para almacenar las estad√≠sticas de los jugadores
struct PlayerStats {
    edict_t* player = nullptr;
    int32_t total_damage = 0;
};
#endif // SHARED_H
