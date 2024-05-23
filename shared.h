#pragma once

#ifndef SHARED_H
#define SHARED_H

#include "g_local.h"

// Define los flags de bonus
#define BF_CHAMPION   0x00000001
#define BF_CORRUPTED    0x00000002
#define BF_INVICTUS   0x00000004
#define BF_BERSERKING  0x00000008
#define BF_POSSESSED   0x00000010
#define BF_STYGIAN    0x00000020
// boss stuff
constexpr spawnflags_t SPAWNFLAG_IS_BOSS = spawnflags_t(0x00000025); // Is monster a boss?
constexpr spawnflags_t SPAWNFLAG_BOSS_DEATH_HANDLED = spawnflags_t(0x80000000); // is dead?


// Declarar funciones globales
std::string GetDisplayName(edict_t* ent);
std::string GetTitleFromFlags(int bonus_flags);
void ApplyMonsterBonusFlags(edict_t* monster);
void ApplyBossEffects(edict_t* boss, bool isSmallMap, bool isMediumMap, bool isBigMap, float& health_multiplier, float& power_armor_multiplier);

// Declarar funciones externas para el healthbar
extern void SP_target_healthbar(edict_t* self);
extern void use_target_healthbar(edict_t* self, edict_t* other, edict_t* activator);
extern void check_target_healthbar(edict_t* self);
extern void SetMonsterHealth(edict_t* monster, int base_health, int current_wave_number);
#endif // SHARED_H
