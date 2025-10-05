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
