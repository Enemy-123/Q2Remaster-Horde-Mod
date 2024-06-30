#include "shared.h"
#include <unordered_map>
void RemovePlayerOwnedEntities(edict_t* player)
{
    edict_t* ent;
    extern void turret_die(edict_t * self, edict_t * inflictor, edict_t * attacker, int damage, const vec3_t & point, const mod_t & mod);
    extern void prox_die(edict_t * self, edict_t * inflictor, edict_t * attacker, int damage, const vec3_t & point, const mod_t & mod);
    extern void tesla_die(edict_t * self, edict_t * inflictor, edict_t * attacker, int damage, const vec3_t & point, const mod_t & mod);
    extern void trap_die(edict_t * self, edict_t * inflictor, edict_t * attacker, int damage, const vec3_t & point, const mod_t & mod);

    for (unsigned int i = 0; i < globals.num_edicts; i++)
    {
        ent = &g_edicts[i];

        if (!ent->inuse)
            continue;

        // Check if the owner is the player or the turret owned by the player
        if (ent->owner == player || (ent->owner && ent->owner->owner == player))
        {

       //     if (ent->owner->s.effects & EF_QUAD || ent->owner && ent->owner->owner->s.effects & EF_QUAD)

 
            if (!strcmp(ent->classname, "tesla_mine") ||
                !strcmp(ent->classname, "food_cube_trap") ||
                !strcmp(ent->classname, "prox_mine") ||
                !strcmp(ent->classname, "monster_sentrygun"))
            {
                // Call appropriate die function
                if (!strcmp(ent->classname, "monster_sentrygun"))
                {
                    if (ent->health > 0)
                    {
                        ent->health = -1;
                        turret_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
                    }
                }
                else if (!strcmp(ent->classname, "tesla_mine"))
                {
                    tesla_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
                }
                else if (!strcmp(ent->classname, "prox_mine"))
                {
                    prox_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
                }
                else if (!strcmp(ent->classname, "food_cube_trap"))
                {
                    trap_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
                }
                else
                {
                    // Use freeEdict to remove the entity
                    // G_FreeEdict(ent);
                    // Or use the function to create an explosion if appropriate
                    BecomeExplosion1(ent);
                }
            }
        }
    }
}

void UpdatePowerUpTimes(edict_t* monster) {
    if (monster->monsterinfo.quad_time <= level.time) {
        monster->monsterinfo.damage_modifier_applied = false;
    }

    if (monster->monsterinfo.double_time <= level.time) {
        monster->monsterinfo.damage_modifier_applied = false;
    }
}

float M_DamageModifier(edict_t* monster) {
    // Verificar si el modificador ya se ha aplicado
    if (monster->monsterinfo.damage_modifier_applied) {
        return 1.0f; // No aplicar modificador adicional
    }

    float damageModifier = 1.0f; // Inicializa el multiplicador a 1.0

    // Comprueba si el monstruo tiene activado el efecto de quad
    if (monster->monsterinfo.quad_time > level.time) {
        damageModifier *= 4.0f;
    }

    // Comprueba si el monstruo tiene activado el efecto de double
    if (monster->monsterinfo.double_time > level.time) {
        // Aplicar double sólo si quad no está activo
        if (monster->monsterinfo.quad_time <= level.time) {
            damageModifier *= 2.0f;
        }
    }

    // Limitar el daño máximo si ambos, quad y double, están activos
    // El máximo es 4.0f ya que es el máximo con quad y no esta la intencion de tener ambos en uso actualmente
    if (damageModifier > 4.0f) {
        damageModifier = 4.0f;
    }

    // Si sólo está activo double, el máximo es 2.0f
    else if (damageModifier > 2.0f && monster->monsterinfo.quad_time <= level.time) {
        damageModifier = 2.0f;
    }

    // Establecer la bandera indicando que el modificador ha sido aplicado
    monster->monsterinfo.damage_modifier_applied = true;

    return damageModifier;
}


std::string GetTitleFromFlags(int bonus_flags) {
    std::string title;
    if (bonus_flags & BF_CHAMPION) {
        title += "Champion ";
    }
    if (bonus_flags & BF_CORRUPTED) {
        title += "Corrupted ";
    }
    if (bonus_flags & BF_RAGEQUITTER) {
        title += "Ragequitter ";
    }
    if (bonus_flags & BF_BERSERKING) {
        title += "Berserking ";
    }
    if (bonus_flags & BF_POSSESSED) {
        title += "Possessed ";
    }
    if (bonus_flags & BF_STYGIAN) {
        title += "Stygian ";
    }
    return title;
}

std::string GetDisplayName(edict_t* ent) {
    static const std::unordered_map<std::string, std::string> name_replacements = {
        { "monster_soldier_light", "Light Soldier" },
        { "monster_soldier_ss", "SS Soldier" },
        { "monster_soldier", "Soldier" },
        { "monster_soldier_hypergun", "Hyper Soldier" },
        { "monster_soldier_lasergun", "Laser Soldier" },
        { "monster_soldier_ripper", "Ripper Soldier" },
        { "monster_infantry2", "Infantry" },
        { "monster_infantry", "Enforcer" },
        { "monster_flyer", "Flyer" },
        { "monster_kamikaze", "Kamikaze" },
        { "monster_hover2", "Blaster Icarus" },
        { "monster_fixbot", "Fixbot" },
        { "monster_gekk", "Gekk" },
        { "monster_flipper", "Flipper" },
        { "monster_gunner2", "Gunner" },
        { "monster_gunner", "Heavy Gunner" },
        { "monster_medic", "Medic" },
        { "monster_brain", "Brain" },
        { "monster_stalker", "Stalker" },
        { "monster_parasite", "Parasite" },
        { "monster_tank", "Tank" },
        { "monster_tank2", "Vanilla Tank" },
        { "monster_guncmdr2", "Gunner Commander" },
        { "monster_mutant", "Mutant" },
        { "monster_redmutant", "Raged Mutant" },
        { "monster_chick", "Iron Maiden" },
        { "monster_chick_heat", "Iron Praetor" },
        { "monster_berserk", "Berserker" },
        { "monster_floater", "Technician" },
        { "monster_hover", "Rocket Icarus" },
        { "monster_daedalus", "Daedalus" },
        { "monster_daedalus2", "Bombardier Hover" },
        { "monster_medic_commander", "Medic Commander" },
        { "monster_tank_commander", "Tank Commander" },
        { "monster_spider", "Arachnid" },
        { "monster_arachnid", "Arachnid" },
        { "monster_guncmdr", "Grenadier Commander" },
        { "monster_gladc", "Plasma Gladiator" },
        { "monster_gladiator", "Gladiator" },
        { "monster_shambler", "Shambler" },
        { "monster_floater2", "DarkMatter Technician" },
        { "monster_carrier2", "Mini Carrier" },
        { "monster_carrier", "Carrier" },
        { "monster_tank_64", "N64 Tank" },
        { "monster_janitor", "Janitor" },
        { "monster_janitor2", "Mini Guardian" },
        { "monster_guardian", "Guardian" },
        { "monster_makron", "Makron" },
        { "monster_jorg", "Jorg" },
        { "monster_gladb", "DarkMatter Gladiator" },
        { "monster_boss2_64", "N64 Hornet" },
        { "monster_boss2kl", "N64 Hornet" },
        { "monster_perrokl", "Infected Parasite" },
        { "monster_guncmdrkl", "Gunner Grenadier" },
        { "monster_shambler", "Shambler" },
        { "monster_shamblerkl", "Shambler" },
        { "monster_makronkl", "Makron" },
        { "monster_widow1", "Widow Apprentice" },
        { "monster_widow", "Widow Matriarch" },
        { "monster_widow2", "Widow Creator" },
        { "monster_supertank", "Super-Tank" },
        { "monster_supertankkl", "Super-Tank" },
        { "monster_boss5", "Super-Tank" },
        { "monster_sentrygun", "Friendly Sentry-Gun" },
        { "monster_turret", "TurretGun" },
        { "monster_turretkl", "TurretGun" },
        { "monster_boss2", "Hornet" },
        { "misc_insane", "Insa9ne Grunt" },
        { "food_cube_trap", "Stroggonoff Maker\n" },
        { "tesla_mine", " Tesla Mine\n" },
        { "prox_mine", " Prox'Nade\n" }
    };

    auto it = name_replacements.find(ent->classname);
    std::string display_name = (it != name_replacements.end()) ? it->second : ent->classname;

    // Añadir el título basado en los flags
    std::string title = GetTitleFromFlags(ent->monsterinfo.bonus_flags);
    return title + display_name;
}

void ApplyMonsterBonusFlags(edict_t* monster) {
    monster->initial_max_health = monster->health;

    if (monster->monsterinfo.bonus_flags & BF_CHAMPION) {
        monster->s.effects |= EF_ROCKET | EF_FIREBALL;
        monster->s.renderfx |= RF_SHELL_RED;
        monster->health *= 2.0f;
        monster->monsterinfo.power_armor_power *= 1.5f;
        monster->initial_max_health = monster->health; // Incrementar initial_max_health
        monster->monsterinfo.double_time = max(level.time, monster->monsterinfo.double_time) + 125_sec;
    }
    if (monster->monsterinfo.bonus_flags & BF_CORRUPTED) {
        monster->s.effects |= EF_PLASMA | EF_TAGTRAIL;
        monster->health *= 1.5f;
        monster->monsterinfo.power_armor_power *= 1.4f;
        monster->initial_max_health = monster->health; // Incrementar initial_max_health
    }
    if (monster->monsterinfo.bonus_flags & BF_RAGEQUITTER) {
        monster->s.effects |= EF_BLUEHYPERBLASTER;
        monster->s.renderfx |= RF_TRANSLUCENT;
        monster->monsterinfo.power_armor_power *= 4.0f;
        monster->monsterinfo.invincible_time = max(level.time, monster->monsterinfo.invincible_time) + 15_sec;
    }
    if (monster->monsterinfo.bonus_flags & BF_BERSERKING) {
        monster->s.effects |= EF_GIB | EF_FLAG2;
        monster->health *= 1.5f;
        monster->monsterinfo.power_armor_power *= 1.5f;
        monster->initial_max_health = monster->health; // Incrementar initial_max_health
        monster->monsterinfo.quad_time = max(level.time, monster->monsterinfo.quad_time) + 120_sec;
    }
    if (monster->monsterinfo.bonus_flags & BF_POSSESSED) {
        monster->s.effects = EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE;

        monster->s.renderfx |= RF_TRANSLUCENT;
        monster->health *= 1.7f;
        monster->monsterinfo.power_armor_power *= 1.7f;
        monster->initial_max_health = monster->health; // Incrementar initial_max_health
        monster->monsterinfo.attack_state = AS_BLIND;
    }
    if (monster->monsterinfo.bonus_flags & BF_STYGIAN) {
        monster->s.effects |= EF_TRACKER | EF_FLAG1;
        monster->health *= 1.6f;
        monster->monsterinfo.power_armor_power *= 1.6f;
        monster->initial_max_health = monster->health; // Incrementar initial_max_health
        monster->monsterinfo.attack_state = AS_BLIND;
    }
}
//monster->monsterinfo.attack_state = AS_BLIND;
void ApplyBossEffects(edict_t* boss, bool isSmallMap, bool isMediumMap, bool isBigMap, float& health_multiplier, float& power_armor_multiplier) {
    // Resetea multiplicadores a valores predeterminados
    health_multiplier = 1.0f;
    power_armor_multiplier = 1.0f;

    // Aplicar efectos según los flags de bonificación
    if (boss->monsterinfo.bonus_flags & BF_CHAMPION) {
        boss->s.scale = 1.3f;
        boss->mins *= 1.3f;
        boss->maxs *= 1.3f;
        boss->s.effects |= EF_ROCKET;
        boss->s.renderfx |= RF_SHELL_RED;
        health_multiplier *= 1.5f;
        power_armor_multiplier *= 1.5f;
    }
    if (boss->monsterinfo.bonus_flags & BF_CORRUPTED) {
        boss->s.scale = 1.5f;
        boss->mins *= 1.5f;
        boss->maxs *= 1.5f;
        boss->s.effects |= EF_FLIES;
        health_multiplier *= 1.4f;
        power_armor_multiplier *= 1.4f;
    }
    if (boss->monsterinfo.bonus_flags & BF_RAGEQUITTER) {
        boss->s.effects |= EF_BLUEHYPERBLASTER;
        boss->s.renderfx |= RF_TRANSLUCENT;
        power_armor_multiplier *= 1.6f;
    }
    if (boss->monsterinfo.bonus_flags & BF_BERSERKING) {
        boss->s.effects |= EF_GIB | EF_FLAG2;
        health_multiplier *= 1.5f;
        power_armor_multiplier *= 1.5f;
    }
    if (boss->monsterinfo.bonus_flags & BF_POSSESSED) {
        boss->s.effects |= EF_BARREL_EXPLODING;
        boss->s.renderfx |= RF_TRANSLUCENT;
        health_multiplier *= 1.7f;
        power_armor_multiplier *= 1.7f;
    }
    if (boss->monsterinfo.bonus_flags & BF_STYGIAN) {
        boss->s.scale = 1.2f;
        boss->mins *= 1.2f;
        boss->maxs *= 1.2f;
        boss->s.effects |= EF_TRACKER | EF_FLAG1;
        health_multiplier *= 1.6f;
        power_armor_multiplier *= 1.6f;
    }

    // Ajustar la salud y armadura de acuerdo a los multiplicadores
    boss->health *= health_multiplier;
    boss->monsterinfo.power_armor_power *= power_armor_multiplier;

    // Aplicar ajustes adicionales dependiendo del tamaño del mapa
    if (isSmallMap) {
        boss->health *= 0.8f;
        boss->monsterinfo.power_armor_power *= 0.8f;
    }
    else if (isBigMap) {
        boss->health *= 1.2f;
        boss->monsterinfo.power_armor_power *= 1.2f;
    }
}

void SetMonsterHealth(edict_t* monster, int base_health, int current_wave_number) {
    // Determinar la salud mínima en función del número de oleadas
    int health_min = 5000; // Valor por defecto

    if (current_wave_number >= 25 && current_wave_number <= 200) {
        health_min = 18000;
    }
    else if (current_wave_number >= 20 && current_wave_number <= 24) {
        health_min = 15000;
    }
    else if (current_wave_number >= 15 && current_wave_number <= 19) {
        health_min = 12000;
    }
    else if (current_wave_number >= 10 && current_wave_number <= 14) {
        health_min = 10000;
    }
    else if (current_wave_number >= 5 && current_wave_number <= 9) {
        health_min = 8000;
    }
    else if (current_wave_number >= 1 && current_wave_number <= 4) {
        health_min = 5000;
    }

    // Ajustar la salud del monstruo
    if (monster->spawnflags.has(SPAWNFLAG_IS_BOSS)) {
        monster->health = base_health;
        monster->max_health = base_health;
    }
    else {
        monster->health = base_health * 4;
        monster->max_health = base_health * 4;
    }

    // Asegurar que la salud no sea inferior al mínimo
    monster->health = std::max(monster->health, health_min);
    monster->initial_max_health = monster->health; // Asegúrate de que initial_max_health se establece correctamente
}
