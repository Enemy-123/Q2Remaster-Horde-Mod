#pragma once
// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

// ============================================================================
// MONSTER WEAPON DAMAGE/SPEED/RADIUS MACROS
// ============================================================================
// These macros provide a clean, fast interface to the global weapon system.
//
// Usage:
//   int damage = M_ROCKET_DMG(self);
//   int speed = M_ROCKET_SPEED(self);
//   int radius = M_ROCKET_RADIUS(self);
//
// The macros automatically:
//   - Get global base damage/speed/radius
//   - Apply monster-specific damage_scale/speed_scale
//   - Apply wave scaling (including boss bonuses via IS_BOSS flag)
//
// All values scale with:
//   - Global weapon base (from config/monsters.json)
//   - Monster damage_scale (per-monster multiplier)
//   - Wave level (sigmoid scaling)
//   - Boss status (1.5x multiplier if IS_BOSS is true)
// ============================================================================

#include "g_local.h"

// Forward declarations
int GetMonsterWeaponDamage(uint8_t monster_type_id, const char* weapon_name);
int GetMonsterWeaponSpeed(uint8_t monster_type_id, const char* weapon_name);
int GetMonsterWeaponRadius(uint8_t monster_type_id, const char* weapon_name);

// ============================================================================
// MELEE WEAPONS
// ============================================================================
#define M_MELEE_DMG(ent)          GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "melee")
#define M_SLAM_DMG(ent)           GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "slam")
#define M_PROBOSCIS_DMG(ent)      GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "proboscis")

// ============================================================================
// BLASTER VARIANTS
// ============================================================================
#define M_BLASTER_DMG(ent)        GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "blaster")
#define M_BLASTER_SPEED(ent)      GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "blaster")

#define M_BLASTER2_DMG(ent)       GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "blaster2")
#define M_BLASTER2_SPEED(ent)     GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "blaster2")

#define M_BLASTER_BOLT_DMG(ent)   GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "blaster_bolt")
#define M_BLASTER_BOLT_SPEED(ent) GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "blaster_bolt")

#define M_BLUEBLASTER_DMG(ent)    GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "blueblaster")
#define M_BLUEBLASTER_SPEED(ent)  GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "blueblaster")

#define M_HYPERBLASTER_DMG(ent)   GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "hyperblaster")
#define M_HYPERBLASTER_SPEED(ent) GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "hyperblaster")

// ============================================================================
// BULLET WEAPONS (instant hit)
// ============================================================================
#define M_SHOTGUN_DMG(ent)        GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "shotgun")
#define M_MACHINEGUN_DMG(ent)     GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "machinegun")

// ============================================================================
// EXPLOSIVE WEAPONS
// ============================================================================
#define M_GRENADE_DMG(ent)        GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "grenade")
#define M_GRENADE_SPEED(ent)      GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "grenade")
#define M_GRENADE_RADIUS(ent)     GetMonsterWeaponRadius((ent)->monsterinfo.monster_type_id, "grenade")

#define M_ROCKET_DMG(ent)         GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "rocket")
#define M_ROCKET_SPEED(ent)       GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "rocket")
#define M_ROCKET_RADIUS(ent)      GetMonsterWeaponRadius((ent)->monsterinfo.monster_type_id, "rocket")

// ============================================================================
// HEAT SEEKING ROCKETS
// ============================================================================
#define M_HEAT_DMG(ent)           GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "heat")
#define M_HEAT_SPEED(ent)         GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "heat")

// ============================================================================
// RAILGUN
// ============================================================================
#define M_RAILGUN_DMG(ent)        GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "railgun")

// ============================================================================
// BFG
// ============================================================================
#define M_BFG_DMG(ent)            GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "bfg")
#define M_BFG_SPEED(ent)          GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "bfg")
#define M_BFG_RADIUS(ent)         GetMonsterWeaponRadius((ent)->monsterinfo.monster_type_id, "bfg")

// ============================================================================
// XATRIX WEAPONS
// ============================================================================
#define M_IONRIPPER_DMG(ent)      GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "ionripper")
#define M_IONRIPPER_SPEED(ent)    GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "ionripper")

#define M_FLECHETTE_DMG(ent)      GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "flechette")
#define M_FLECHETTE_SPEED(ent)    GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "flechette")

// ============================================================================
// ROGUE WEAPONS
// ============================================================================
#define M_PLASMA_DMG(ent)         GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "plasma")
#define M_PLASMA_SPEED(ent)       GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "plasma")
#define M_PLASMA_RADIUS(ent)      GetMonsterWeaponRadius((ent)->monsterinfo.monster_type_id, "plasma")

#define M_TRACKER_DMG(ent)        GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "tracker")
#define M_TRACKER_SPEED(ent)      GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "tracker")
#define M_TRACKER_RADIUS(ent)     GetMonsterWeaponRadius((ent)->monsterinfo.monster_type_id, "tracker")

#define M_DABEAM_DMG(ent)         GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "dabeam")

#define M_HEATBEAM_DMG(ent)       GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "heatbeam")

// ============================================================================
// SPECIAL WEAPONS
// ============================================================================
#define M_LIGHTNING_DMG(ent)      GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "lightning")

#define M_FIREBALL_DMG(ent)       GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "fireball")
#define M_FIREBALL_SPEED(ent)     GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "fireball")
#define M_FIREBALL_RADIUS(ent)    GetMonsterWeaponRadius((ent)->monsterinfo.monster_type_id, "fireball")

#define M_BOLT_DMG(ent)           GetMonsterWeaponDamage((ent)->monsterinfo.monster_type_id, "bolt")
#define M_BOLT_SPEED(ent)         GetMonsterWeaponSpeed((ent)->monsterinfo.monster_type_id, "bolt")

// ============================================================================
// HARDCODED FALLBACK CONSTANTS (if config not loaded or returns 0)
// ============================================================================
// These are only used as emergency fallbacks and should rarely be needed
// since the global weapon system provides defaults.
constexpr int M_MELEE_DMG_DEFAULT = 10;
constexpr int M_BLASTER_DMG_DEFAULT = 15;
constexpr int M_BLASTER_SPEED_DEFAULT = 1000;
constexpr int M_SHOTGUN_DMG_DEFAULT = 4;
constexpr int M_MACHINEGUN_DMG_DEFAULT = 8;
constexpr int M_GRENADE_DMG_DEFAULT = 50;
constexpr int M_GRENADE_SPEED_DEFAULT = 600;
constexpr int M_ROCKET_DMG_DEFAULT = 100;
constexpr int M_ROCKET_SPEED_DEFAULT = 650;
constexpr int M_RAILGUN_DMG_DEFAULT = 150;
constexpr int M_BFG_DMG_DEFAULT = 500;
constexpr int M_BFG_SPEED_DEFAULT = 400;

// ============================================================================
// HELPER MACROS FOR COMMON PATTERNS
// ============================================================================

// Get damage with optional fallback
#define M_GET_DMG_OR(ent, weapon, fallback) \
    ({ int _dmg = M_##weapon##_DMG(ent); _dmg > 0 ? _dmg : (fallback); })

// Get speed with optional fallback
#define M_GET_SPEED_OR(ent, weapon, fallback) \
    ({ int _spd = M_##weapon##_SPEED(ent); _spd > 0 ? _spd : (fallback); })

// Example usage:
//   int damage = M_GET_DMG_OR(self, ROCKET, 100);  // Returns rocket damage or 100 if not configured

// ============================================================================
// MONSTER HEALTH/ARMOR MACROS
// ============================================================================
// Forward declarations for health/armor functions
int GetMonsterBaseHealth(uint8_t monster_type_id);
int GetMonsterScaledHealth(uint8_t monster_type_id, int wave_level, bool is_boss);
int GetMonsterBaseArmor(uint8_t monster_type_id);
int GetMonsterScaledArmor(uint8_t monster_type_id, int wave_level, bool is_boss);
int GetMonsterBasePowerArmor(uint8_t monster_type_id);
int GetMonsterScaledPowerArmor(uint8_t monster_type_id, int wave_level, bool is_boss);
int32_t GetMonsterPowerArmorType(uint8_t monster_type_id);

extern int16_t current_wave_level;

// Macro patterns:
//   M_<MONSTER>_INITIAL_HEALTH           - Base health from config
//   M_<MONSTER>_ADDON_HEALTH(ent)        - Scaled health (wave + boss bonuses)
//   M_<MONSTER>_INITIAL_ARMOR            - Base armor from config
//   M_<MONSTER>_ADDON_ARMOR(ent)         - Scaled armor (wave + boss bonuses)
//   M_<MONSTER>_POWER_ARMOR_TYPE         - Power armor type (IT_ITEM_POWER_SCREEN, etc.)
//   M_<MONSTER>_POWER_ARMOR              - Base power armor from config
//   M_<MONSTER>_ADDON_POWER_ARMOR(ent)   - Scaled power armor (wave + boss bonuses)

// ============================================================================
// SOLDIERS
// ============================================================================
#define M_SOLDIER_LIGHT_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LIGHT))
#define M_SOLDIER_LIGHT_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LIGHT), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_LIGHT_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LIGHT))
#define M_SOLDIER_LIGHT_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LIGHT), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_LIGHT_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LIGHT))
#define M_SOLDIER_LIGHT_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LIGHT))
#define M_SOLDIER_LIGHT_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LIGHT), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_SOLDIER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER))
#define M_SOLDIER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER))
#define M_SOLDIER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER))
#define M_SOLDIER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER))
#define M_SOLDIER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_SOLDIER_SS_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_SS))
#define M_SOLDIER_SS_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_SS), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_SS_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_SS))
#define M_SOLDIER_SS_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_SS), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_SS_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_SS))
#define M_SOLDIER_SS_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_SS))
#define M_SOLDIER_SS_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_SS), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_SOLDIER_HYPERGUN_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_HYPERGUN))
#define M_SOLDIER_HYPERGUN_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_HYPERGUN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_HYPERGUN_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_HYPERGUN))
#define M_SOLDIER_HYPERGUN_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_HYPERGUN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_HYPERGUN_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_HYPERGUN))
#define M_SOLDIER_HYPERGUN_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_HYPERGUN))
#define M_SOLDIER_HYPERGUN_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_HYPERGUN), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_SOLDIER_RIPPER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_RIPPER))
#define M_SOLDIER_RIPPER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_RIPPER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_RIPPER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_RIPPER))
#define M_SOLDIER_RIPPER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_RIPPER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_RIPPER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_RIPPER))
#define M_SOLDIER_RIPPER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_RIPPER))
#define M_SOLDIER_RIPPER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_RIPPER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_SOLDIER_LASERGUN_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LASERGUN))
#define M_SOLDIER_LASERGUN_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LASERGUN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_LASERGUN_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LASERGUN))
#define M_SOLDIER_LASERGUN_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LASERGUN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SOLDIER_LASERGUN_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LASERGUN))
#define M_SOLDIER_LASERGUN_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LASERGUN))
#define M_SOLDIER_LASERGUN_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SOLDIER_LASERGUN), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// INFANTRY
// ============================================================================
#define M_INFANTRY_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY))
#define M_INFANTRY_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_INFANTRY_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY))
#define M_INFANTRY_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_INFANTRY_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY))
#define M_INFANTRY_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY))
#define M_INFANTRY_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_INFANTRY_VANILLA_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY_VANILLA))
#define M_INFANTRY_VANILLA_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_INFANTRY_VANILLA_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY_VANILLA))
#define M_INFANTRY_VANILLA_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_INFANTRY_VANILLA_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY_VANILLA))
#define M_INFANTRY_VANILLA_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY_VANILLA))
#define M_INFANTRY_VANILLA_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::INFANTRY_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// GUNNERS
// ============================================================================
#define M_GUNNER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER))
#define M_GUNNER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUNNER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER))
#define M_GUNNER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUNNER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER))
#define M_GUNNER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER))
#define M_GUNNER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_GUNNER_VANILLA_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER_VANILLA))
#define M_GUNNER_VANILLA_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUNNER_VANILLA_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER_VANILLA))
#define M_GUNNER_VANILLA_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUNNER_VANILLA_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER_VANILLA))
#define M_GUNNER_VANILLA_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER_VANILLA))
#define M_GUNNER_VANILLA_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNNER_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// MEDICS & COMMANDERS
// ============================================================================
#define M_MEDIC_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC))
#define M_MEDIC_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MEDIC_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC))
#define M_MEDIC_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MEDIC_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC))
#define M_MEDIC_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC))
#define M_MEDIC_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_MEDIC_COMMANDER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC_COMMANDER))
#define M_MEDIC_COMMANDER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC_COMMANDER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MEDIC_COMMANDER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC_COMMANDER))
#define M_MEDIC_COMMANDER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC_COMMANDER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MEDIC_COMMANDER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC_COMMANDER))
#define M_MEDIC_COMMANDER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC_COMMANDER))
#define M_MEDIC_COMMANDER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MEDIC_COMMANDER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_GUNCMDR_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR))
#define M_GUNCMDR_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUNCMDR_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR))
#define M_GUNCMDR_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUNCMDR_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR))
#define M_GUNCMDR_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR))
#define M_GUNCMDR_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_GUNCMDR_VANILLA_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_VANILLA))
#define M_GUNCMDR_VANILLA_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUNCMDR_VANILLA_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_VANILLA))
#define M_GUNCMDR_VANILLA_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUNCMDR_VANILLA_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_VANILLA))
#define M_GUNCMDR_VANILLA_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_VANILLA))
#define M_GUNCMDR_VANILLA_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_GUNCMDR_KL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_KL))
#define M_GUNCMDR_KL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUNCMDR_KL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_KL))
#define M_GUNCMDR_KL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUNCMDR_KL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_KL))
#define M_GUNCMDR_KL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_KL))
#define M_GUNCMDR_KL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUNCMDR_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// GLADIATORS
// ============================================================================
#define M_GLADIATOR_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR))
#define M_GLADIATOR_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GLADIATOR_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR))
#define M_GLADIATOR_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GLADIATOR_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR))
#define M_GLADIATOR_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR))
#define M_GLADIATOR_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_GLADIATOR_B_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_B))
#define M_GLADIATOR_B_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_B), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GLADIATOR_B_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_B))
#define M_GLADIATOR_B_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_B), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GLADIATOR_B_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_B))
#define M_GLADIATOR_B_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_B))
#define M_GLADIATOR_B_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_B), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_GLADIATOR_C_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_C))
#define M_GLADIATOR_C_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_C), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GLADIATOR_C_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_C))
#define M_GLADIATOR_C_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_C), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GLADIATOR_C_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_C))
#define M_GLADIATOR_C_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_C))
#define M_GLADIATOR_C_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GLADIATOR_C), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// TANKS
// ============================================================================
#define M_TANK_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::TANK))
#define M_TANK_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::TANK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_TANK_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK))
#define M_TANK_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_TANK_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::TANK))
#define M_TANK_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK))
#define M_TANK_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_TANK_COMMANDER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::TANK_COMMANDER))
#define M_TANK_COMMANDER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::TANK_COMMANDER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_TANK_COMMANDER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_COMMANDER))
#define M_TANK_COMMANDER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_COMMANDER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_TANK_COMMANDER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::TANK_COMMANDER))
#define M_TANK_COMMANDER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_COMMANDER))
#define M_TANK_COMMANDER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_COMMANDER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_TANK_64_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::TANK_64))
#define M_TANK_64_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::TANK_64), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_TANK_64_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_64))
#define M_TANK_64_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_64), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_TANK_64_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::TANK_64))
#define M_TANK_64_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_64))
#define M_TANK_64_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_64), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_RUNNERTANK_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::RUNNERTANK))
#define M_RUNNERTANK_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::RUNNERTANK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_RUNNERTANK_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::RUNNERTANK))
#define M_RUNNERTANK_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::RUNNERTANK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_RUNNERTANK_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::RUNNERTANK))
#define M_RUNNERTANK_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::RUNNERTANK))
#define M_RUNNERTANK_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::RUNNERTANK), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_TANK_SPAWNER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::TANK_SPAWNER))
#define M_TANK_SPAWNER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::TANK_SPAWNER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_TANK_SPAWNER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_SPAWNER))
#define M_TANK_SPAWNER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_SPAWNER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_TANK_SPAWNER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::TANK_SPAWNER))
#define M_TANK_SPAWNER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_SPAWNER))
#define M_TANK_SPAWNER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::TANK_SPAWNER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_SUPERTANK_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANK))
#define M_SUPERTANK_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SUPERTANK_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANK))
#define M_SUPERTANK_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SUPERTANK_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANK))
#define M_SUPERTANK_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANK))
#define M_SUPERTANK_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANK), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_SUPERTANKKL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANKKL))
#define M_SUPERTANKKL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANKKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SUPERTANKKL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANKKL))
#define M_SUPERTANKKL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANKKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SUPERTANKKL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANKKL))
#define M_SUPERTANKKL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANKKL))
#define M_SUPERTANKKL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SUPERTANKKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// CHICKS
// ============================================================================
#define M_CHICK_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::CHICK))
#define M_CHICK_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::CHICK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_CHICK_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICK))
#define M_CHICK_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_CHICK_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::CHICK))
#define M_CHICK_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICK))
#define M_CHICK_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICK), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_CHICK_HEAT_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::CHICK_HEAT))
#define M_CHICK_HEAT_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::CHICK_HEAT), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_CHICK_HEAT_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICK_HEAT))
#define M_CHICK_HEAT_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICK_HEAT), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_CHICK_HEAT_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::CHICK_HEAT))
#define M_CHICK_HEAT_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICK_HEAT))
#define M_CHICK_HEAT_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICK_HEAT), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_CHICKKL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::CHICKKL))
#define M_CHICKKL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::CHICKKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_CHICKKL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICKKL))
#define M_CHICKKL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICKKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_CHICKKL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::CHICKKL))
#define M_CHICKKL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICKKL))
#define M_CHICKKL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::CHICKKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// MELEE MONSTERS
// ============================================================================
#define M_PARASITE_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::PARASITE))
#define M_PARASITE_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::PARASITE), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_PARASITE_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::PARASITE))
#define M_PARASITE_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::PARASITE), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_PARASITE_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::PARASITE))
#define M_PARASITE_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::PARASITE))
#define M_PARASITE_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::PARASITE), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_BERSERK_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::BERSERK))
#define M_BERSERK_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::BERSERK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BERSERK_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::BERSERK))
#define M_BERSERK_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::BERSERK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BERSERK_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::BERSERK))
#define M_BERSERK_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BERSERK))
#define M_BERSERK_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BERSERK), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_BERSERKERKL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::BERSERKERKL))
#define M_BERSERKERKL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::BERSERKERKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BERSERKERKL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::BERSERKERKL))
#define M_BERSERKERKL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::BERSERKERKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BERSERKERKL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::BERSERKERKL))
#define M_BERSERKERKL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BERSERKERKL))
#define M_BERSERKERKL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BERSERKERKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_MUTANT_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::MUTANT))
#define M_MUTANT_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::MUTANT), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MUTANT_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::MUTANT))
#define M_MUTANT_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::MUTANT), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MUTANT_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::MUTANT))
#define M_MUTANT_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MUTANT))
#define M_MUTANT_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MUTANT), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_REDMUTANT_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::REDMUTANT))
#define M_REDMUTANT_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::REDMUTANT), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_REDMUTANT_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::REDMUTANT))
#define M_REDMUTANT_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::REDMUTANT), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_REDMUTANT_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::REDMUTANT))
#define M_REDMUTANT_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::REDMUTANT))
#define M_REDMUTANT_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::REDMUTANT), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// FLYERS
// ============================================================================
#define M_BRAIN_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::BRAIN))
#define M_BRAIN_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::BRAIN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BRAIN_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::BRAIN))
#define M_BRAIN_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::BRAIN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BRAIN_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::BRAIN))
#define M_BRAIN_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BRAIN))
#define M_BRAIN_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BRAIN), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_FLYER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::FLYER))
#define M_FLYER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::FLYER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FLYER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLYER))
#define M_FLYER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLYER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FLYER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::FLYER))
#define M_FLYER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLYER))
#define M_FLYER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLYER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_HOVER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::HOVER))
#define M_HOVER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::HOVER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_HOVER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::HOVER))
#define M_HOVER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::HOVER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_HOVER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::HOVER))
#define M_HOVER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::HOVER))
#define M_HOVER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::HOVER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_HOVER_VANILLA_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::HOVER_VANILLA))
#define M_HOVER_VANILLA_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::HOVER_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_HOVER_VANILLA_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::HOVER_VANILLA))
#define M_HOVER_VANILLA_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::HOVER_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_HOVER_VANILLA_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::HOVER_VANILLA))
#define M_HOVER_VANILLA_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::HOVER_VANILLA))
#define M_HOVER_VANILLA_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::HOVER_VANILLA), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_DAEDALUS_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS))
#define M_DAEDALUS_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_DAEDALUS_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS))
#define M_DAEDALUS_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_DAEDALUS_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS))
#define M_DAEDALUS_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS))
#define M_DAEDALUS_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_DAEDALUS_BOMBER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS_BOMBER))
#define M_DAEDALUS_BOMBER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS_BOMBER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_DAEDALUS_BOMBER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS_BOMBER))
#define M_DAEDALUS_BOMBER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS_BOMBER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_DAEDALUS_BOMBER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS_BOMBER))
#define M_DAEDALUS_BOMBER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS_BOMBER))
#define M_DAEDALUS_BOMBER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::DAEDALUS_BOMBER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// XATRIX MONSTERS
// ============================================================================
#define M_FIXBOT_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT))
#define M_FIXBOT_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FIXBOT_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT))
#define M_FIXBOT_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FIXBOT_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT))
#define M_FIXBOT_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT))
#define M_FIXBOT_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_FIXBOT_KL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT_KL))
#define M_FIXBOT_KL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FIXBOT_KL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT_KL))
#define M_FIXBOT_KL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FIXBOT_KL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT_KL))
#define M_FIXBOT_KL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT_KL))
#define M_FIXBOT_KL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FIXBOT_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_GEKK_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GEKK))
#define M_GEKK_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GEKK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GEKK_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GEKK))
#define M_GEKK_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GEKK), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GEKK_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GEKK))
#define M_GEKK_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GEKK))
#define M_GEKK_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GEKK), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_GEKKKL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GEKKKL))
#define M_GEKKKL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GEKKKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GEKKKL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GEKKKL))
#define M_GEKKKL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GEKKKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GEKKKL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GEKKKL))
#define M_GEKKKL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GEKKKL))
#define M_GEKKKL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GEKKKL), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// ROGUE MONSTERS
// ============================================================================
#define M_FLOATER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER))
#define M_FLOATER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FLOATER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER))
#define M_FLOATER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FLOATER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER))
#define M_FLOATER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER))
#define M_FLOATER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_FLOATER_TRACKER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER_TRACKER))
#define M_FLOATER_TRACKER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER_TRACKER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FLOATER_TRACKER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER_TRACKER))
#define M_FLOATER_TRACKER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER_TRACKER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FLOATER_TRACKER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER_TRACKER))
#define M_FLOATER_TRACKER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER_TRACKER))
#define M_FLOATER_TRACKER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLOATER_TRACKER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_STALKER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::STALKER))
#define M_STALKER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::STALKER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_STALKER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::STALKER))
#define M_STALKER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::STALKER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_STALKER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::STALKER))
#define M_STALKER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::STALKER))
#define M_STALKER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::STALKER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_CARRIER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER))
#define M_CARRIER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_CARRIER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER))
#define M_CARRIER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_CARRIER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER))
#define M_CARRIER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER))
#define M_CARRIER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_CARRIER_MINI_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER_MINI))
#define M_CARRIER_MINI_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER_MINI), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_CARRIER_MINI_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER_MINI))
#define M_CARRIER_MINI_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER_MINI), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_CARRIER_MINI_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER_MINI))
#define M_CARRIER_MINI_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER_MINI))
#define M_CARRIER_MINI_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::CARRIER_MINI), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_WIDOW_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW))
#define M_WIDOW_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_WIDOW_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW))
#define M_WIDOW_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_WIDOW_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW))
#define M_WIDOW_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW))
#define M_WIDOW_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_WIDOW1_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW1))
#define M_WIDOW1_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW1), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_WIDOW1_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW1))
#define M_WIDOW1_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW1), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_WIDOW1_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW1))
#define M_WIDOW1_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW1))
#define M_WIDOW1_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW1), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_WIDOW2_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW2))
#define M_WIDOW2_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW2), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_WIDOW2_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW2))
#define M_WIDOW2_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW2), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_WIDOW2_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW2))
#define M_WIDOW2_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW2))
#define M_WIDOW2_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::WIDOW2), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// SPIDERS & ARACHNIDS
// ============================================================================
#define M_SPIDER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SPIDER))
#define M_SPIDER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SPIDER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SPIDER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SPIDER))
#define M_SPIDER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SPIDER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SPIDER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SPIDER))
#define M_SPIDER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SPIDER))
#define M_SPIDER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SPIDER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_ARACHNID2_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID2))
#define M_ARACHNID2_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID2), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_ARACHNID2_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID2))
#define M_ARACHNID2_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID2), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_ARACHNID2_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID2))
#define M_ARACHNID2_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID2))
#define M_ARACHNID2_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID2), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_GM_ARACHNID_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GM_ARACHNID))
#define M_GM_ARACHNID_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GM_ARACHNID), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GM_ARACHNID_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GM_ARACHNID))
#define M_GM_ARACHNID_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GM_ARACHNID), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GM_ARACHNID_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GM_ARACHNID))
#define M_GM_ARACHNID_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GM_ARACHNID))
#define M_GM_ARACHNID_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GM_ARACHNID), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_PSX_ARACHNID_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::PSX_ARACHNID))
#define M_PSX_ARACHNID_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::PSX_ARACHNID), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_PSX_ARACHNID_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::PSX_ARACHNID))
#define M_PSX_ARACHNID_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::PSX_ARACHNID), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_PSX_ARACHNID_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::PSX_ARACHNID))
#define M_PSX_ARACHNID_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::PSX_ARACHNID))
#define M_PSX_ARACHNID_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::PSX_ARACHNID), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_ARACHNID_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID))
#define M_ARACHNID_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_ARACHNID_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID))
#define M_ARACHNID_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_ARACHNID_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID))
#define M_ARACHNID_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID))
#define M_ARACHNID_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::ARACHNID), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// SHAMBLERS
// ============================================================================
#define M_SHAMBLER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER))
#define M_SHAMBLER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SHAMBLER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER))
#define M_SHAMBLER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SHAMBLER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER))
#define M_SHAMBLER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER))
#define M_SHAMBLER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_SHAMBLER_SMALL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_SMALL))
#define M_SHAMBLER_SMALL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_SMALL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SHAMBLER_SMALL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_SMALL))
#define M_SHAMBLER_SMALL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_SMALL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SHAMBLER_SMALL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_SMALL))
#define M_SHAMBLER_SMALL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_SMALL))
#define M_SHAMBLER_SMALL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_SMALL), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_SHAMBLER_KL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_KL))
#define M_SHAMBLER_KL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SHAMBLER_KL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_KL))
#define M_SHAMBLER_KL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SHAMBLER_KL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_KL))
#define M_SHAMBLER_KL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_KL))
#define M_SHAMBLER_KL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SHAMBLER_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// BOSSES
// ============================================================================
#define M_BOSS2_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2))
#define M_BOSS2_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BOSS2_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2))
#define M_BOSS2_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BOSS2_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2))
#define M_BOSS2_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2))
#define M_BOSS2_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_BOSS2_64_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_64))
#define M_BOSS2_64_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_64), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BOSS2_64_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_64))
#define M_BOSS2_64_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_64), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BOSS2_64_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_64))
#define M_BOSS2_64_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_64))
#define M_BOSS2_64_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_64), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_BOSS2_MINI_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_MINI))
#define M_BOSS2_MINI_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_MINI), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BOSS2_MINI_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_MINI))
#define M_BOSS2_MINI_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_MINI), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BOSS2_MINI_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_MINI))
#define M_BOSS2_MINI_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_MINI))
#define M_BOSS2_MINI_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_MINI), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_BOSS2_KL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_KL))
#define M_BOSS2_KL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BOSS2_KL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_KL))
#define M_BOSS2_KL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BOSS2_KL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_KL))
#define M_BOSS2_KL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_KL))
#define M_BOSS2_KL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS2_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_MAKRON_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON))
#define M_MAKRON_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MAKRON_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON))
#define M_MAKRON_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MAKRON_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON))
#define M_MAKRON_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON))
#define M_MAKRON_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_MAKRON_KL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON_KL))
#define M_MAKRON_KL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MAKRON_KL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON_KL))
#define M_MAKRON_KL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MAKRON_KL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON_KL))
#define M_MAKRON_KL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON_KL))
#define M_MAKRON_KL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MAKRON_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_JORG_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::JORG))
#define M_JORG_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::JORG), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_JORG_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::JORG))
#define M_JORG_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::JORG), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_JORG_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::JORG))
#define M_JORG_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::JORG))
#define M_JORG_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::JORG), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_JORG_SMALL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::JORG_SMALL))
#define M_JORG_SMALL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::JORG_SMALL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_JORG_SMALL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::JORG_SMALL))
#define M_JORG_SMALL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::JORG_SMALL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_JORG_SMALL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::JORG_SMALL))
#define M_JORG_SMALL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::JORG_SMALL))
#define M_JORG_SMALL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::JORG_SMALL), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_BOSS5_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::BOSS5))
#define M_BOSS5_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::BOSS5), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BOSS5_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS5))
#define M_BOSS5_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS5), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_BOSS5_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::BOSS5))
#define M_BOSS5_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS5))
#define M_BOSS5_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::BOSS5), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_GUARDIAN_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUARDIAN))
#define M_GUARDIAN_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::GUARDIAN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUARDIAN_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUARDIAN))
#define M_GUARDIAN_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUARDIAN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_GUARDIAN_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::GUARDIAN))
#define M_GUARDIAN_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUARDIAN))
#define M_GUARDIAN_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::GUARDIAN), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_PSX_GUARDIAN_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::PSX_GUARDIAN))
#define M_PSX_GUARDIAN_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::PSX_GUARDIAN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_PSX_GUARDIAN_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::PSX_GUARDIAN))
#define M_PSX_GUARDIAN_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::PSX_GUARDIAN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_PSX_GUARDIAN_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::PSX_GUARDIAN))
#define M_PSX_GUARDIAN_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::PSX_GUARDIAN))
#define M_PSX_GUARDIAN_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::PSX_GUARDIAN), current_wave_level, (ent)->monsterinfo.IS_BOSS)

// ============================================================================
// SPECIAL MONSTERS
// ============================================================================
#define M_MISC_INSANE_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::MISC_INSANE))
#define M_MISC_INSANE_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::MISC_INSANE), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MISC_INSANE_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::MISC_INSANE))
#define M_MISC_INSANE_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::MISC_INSANE), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_MISC_INSANE_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::MISC_INSANE))
#define M_MISC_INSANE_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MISC_INSANE))
#define M_MISC_INSANE_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::MISC_INSANE), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_TURRET_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::TURRET))
#define M_TURRET_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::TURRET), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_TURRET_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::TURRET))
#define M_TURRET_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::TURRET), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_TURRET_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::TURRET))
#define M_TURRET_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::TURRET))
#define M_TURRET_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::TURRET), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_SENTRYGUN_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::SENTRYGUN))
#define M_SENTRYGUN_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::SENTRYGUN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SENTRYGUN_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::SENTRYGUN))
#define M_SENTRYGUN_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::SENTRYGUN), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_SENTRYGUN_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::SENTRYGUN))
#define M_SENTRYGUN_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SENTRYGUN))
#define M_SENTRYGUN_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::SENTRYGUN), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_KAMIKAZE_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::KAMIKAZE))
#define M_KAMIKAZE_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::KAMIKAZE), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_KAMIKAZE_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::KAMIKAZE))
#define M_KAMIKAZE_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::KAMIKAZE), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_KAMIKAZE_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::KAMIKAZE))
#define M_KAMIKAZE_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::KAMIKAZE))
#define M_KAMIKAZE_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::KAMIKAZE), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_FLIPPER_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::FLIPPER))
#define M_FLIPPER_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::FLIPPER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FLIPPER_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLIPPER))
#define M_FLIPPER_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLIPPER), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_FLIPPER_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::FLIPPER))
#define M_FLIPPER_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLIPPER))
#define M_FLIPPER_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::FLIPPER), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_JANITOR_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR))
#define M_JANITOR_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_JANITOR_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR))
#define M_JANITOR_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_JANITOR_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR))
#define M_JANITOR_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR))
#define M_JANITOR_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_JANITOR2_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR2))
#define M_JANITOR2_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR2), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_JANITOR2_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR2))
#define M_JANITOR2_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR2), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_JANITOR2_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR2))
#define M_JANITOR2_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR2))
#define M_JANITOR2_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::JANITOR2), current_wave_level, (ent)->monsterinfo.IS_BOSS)

#define M_PERRO_KL_INITIAL_HEALTH           GetMonsterBaseHealth(static_cast<uint8_t>(horde::MonsterTypeID::PERRO_KL))
#define M_PERRO_KL_ADDON_HEALTH(ent)        GetMonsterScaledHealth(static_cast<uint8_t>(horde::MonsterTypeID::PERRO_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_PERRO_KL_INITIAL_ARMOR            GetMonsterBaseArmor(static_cast<uint8_t>(horde::MonsterTypeID::PERRO_KL))
#define M_PERRO_KL_ADDON_ARMOR(ent)         GetMonsterScaledArmor(static_cast<uint8_t>(horde::MonsterTypeID::PERRO_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
#define M_PERRO_KL_POWER_ARMOR_TYPE         GetMonsterPowerArmorType(static_cast<uint8_t>(horde::MonsterTypeID::PERRO_KL))
#define M_PERRO_KL_POWER_ARMOR              GetMonsterBasePowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::PERRO_KL))
#define M_PERRO_KL_ADDON_POWER_ARMOR(ent)   GetMonsterScaledPowerArmor(static_cast<uint8_t>(horde::MonsterTypeID::PERRO_KL), current_wave_level, (ent)->monsterinfo.IS_BOSS)
