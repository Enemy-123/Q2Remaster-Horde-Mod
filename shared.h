#pragma once
// shared.h

#ifndef SHARED_H
#define SHARED_H

#include "g_local.h" // Asegúrate de incluir los encabezados necesarios

std::string GetDisplayName(edict_t* ent);
std::string GetTitleFromFlags(int bonus_flags);
void ApplyBossEffects(edict_t* boss, bool isSmallMap, bool isMediumMap, bool isBigMap, float& health_multiplier, float& power_armor_multiplier);

// Declarar funciones externas
extern void SP_target_healthbar(edict_t* self);
extern void use_target_healthbar(edict_t* self, edict_t* other, edict_t* activator);
extern void check_target_healthbar(edict_t* self);


#endif // SHARED_H
