#pragma once

#ifndef SHARED_H
#define SHARED_H

#include "g_local.h"
#include <string>

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
// Declarar funciones globales name strings
std::string GetDisplayName(edict_t* ent);
std::string GetTitleFromFlags(int bonus_flags);
//DMG & POWERUP
void ApplyMonsterBonusFlags(edict_t* monster);
void ApplyBossEffects(edict_t* boss, bool isSmallMap, bool isMediumMap, bool isBigMap, float& health_multiplier, float& power_armor_multiplier);
extern float M_DamageModifier(edict_t* monster);
extern std::string GetPlayerName(edict_t* player);
// Declarar funciones externas para el healthbar
extern void SP_target_healthbar(edict_t* self);
extern void use_target_healthbar(edict_t* self, edict_t* other, edict_t* activator);
extern void check_target_healthbar(edict_t* self);
extern void SetMonsterHealth(edict_t* monster, int base_health, int current_wave_number);
extern void UpdatePowerUpTimes(edict_t* monster);
void Boss_SpawnMonster(edict_t* self);
void PushEntitiesAway(const vec3_t& center, int num_waves, int wave_interval_ms, float push_radius, float push_strength, float horizontal_push_strength, float vertical_push_strength);
//strogg ship
//extern edict_t* CreatePathCornerOnSkySurface(edict_t* reference);
//extern edict_t* CreatePathCornerAbovePlayer(edict_t* player);
//extern float PlayersRangeFromSpot(edict_t* spot);
//extern void MoveMonsterToPlayer(edict_t* monster);

struct MapSize {
    bool isSmallMap = false;
    bool isMediumMap = false;
    bool isBigMap = false;
};

MapSize GetMapSize(const std::string& mapname)  ;


// Estructura para almacenar las estadísticas de los jugadores
struct PlayerStats {
    edict_t* player;
    int total_damage;
};




#endif // SHARED_H