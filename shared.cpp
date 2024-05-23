#include "shared.h"

std::string GetTitleFromFlags(int bonus_flags) {
    std::string title;
    if (bonus_flags & BF_CHAMPION) {
        title += "Champion ";
    }
    if (bonus_flags & BF_CORRUPTED) {
        title += "Corrupted ";
    }
    if (bonus_flags & BF_INVICTUS) {
        title += "Invictus ";
    }
    if (bonus_flags & BF_BERSERKING) {
        title += "Berserking ";
    }
    if (bonus_flags & BF_POSSESSED) { // Corregido aquí
        title += "Possessed "; // Corregido aquí
    }
    if (bonus_flags & BF_STYGIAN) {
        title += "Stygian ";
    }
    return title;
}

// shared.cpp
#include "shared.h"
void ApplyMonsterBonusFlags(edict_t* monster) {
    monster->initial_max_health = monster->health;

    if (monster->monsterinfo.bonus_flags & BF_CHAMPION) {
        monster->s.scale = 1.3f;
        monster->s.effects |= EF_ROCKET | EF_FIREBALL;
        monster->s.renderfx |= RF_SHELL_RED;
        monster->health *= 1.5f;
        monster->monsterinfo.power_armor_power *= 1.5f;
        monster->initial_max_health *= 1.5f; // Incrementar initial_max_health
    }
    if (monster->monsterinfo.bonus_flags & BF_CORRUPTED) {
        monster->s.scale = 1.5f;
        monster->s.effects |= EF_PLASMA | EF_TAGTRAIL;
        monster->health *= 1.4f;
        monster->monsterinfo.power_armor_power *= 1.4f;
        monster->initial_max_health *= 1.4f; // Incrementar initial_max_health
    }
    if (monster->monsterinfo.bonus_flags & BF_INVICTUS) {
        monster->s.effects |= EF_BLUEHYPERBLASTER;
        monster->s.renderfx |= RF_TRANSLUCENT;
        monster->monsterinfo.power_armor_power *= 1.6f;
    }
    if (monster->monsterinfo.bonus_flags & BF_BERSERKING) {
        monster->s.effects |= EF_GIB | EF_FLAG2;
        monster->health *= 1.5f;
        monster->monsterinfo.power_armor_power *= 1.5f;
        monster->initial_max_health *= 1.5f; // Incrementar initial_max_health
    }
    if (monster->monsterinfo.bonus_flags & BF_POSSESSED) { // Corregido aquí
        monster->s.effects |= EF_BARREL_EXPLODING | EF_SPHERETRANS;
        monster->health *= 1.7f;
        monster->monsterinfo.power_armor_power *= 1.7f;
        monster->initial_max_health *= 1.7f; // Incrementar initial_max_health
    }
    if (monster->monsterinfo.bonus_flags & BF_STYGIAN) {
        monster->s.scale = 1.2f;
        monster->s.effects |= EF_TRACKER | EF_FLAG1;
        monster->health *= 1.6f;
        monster->monsterinfo.power_armor_power *= 1.6f;
        monster->initial_max_health *= 1.6f; // Incrementar initial_max_health
    }
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
        { "monster_hover2", "Blaster Icarus" },
        { "monster_fixbot", "Fixbot" },
        { "monster_gekk", "Gekk" },
        { "monster_gunner2", "Gunner" },
        { "monster_gunner", "Improved Gunner" },
        { "monster_medic", "Medic" },
        { "monster_brain", "Brain" },
        { "monster_stalker", "Stalker" },
        { "monster_parasite", "Parasite" },
        { "monster_tank", "Tank" },
        { "monster_tank2", "Vanilla Tank" },
        { "monster_guncmdr2", "Gunner Commander" },
        { "monster_mutant", "Mutant" },
        { "monster_chick", "Iron Maiden" },
        { "monster_chick_heat", "Iron Praetor" },
        { "monster_berserk", "Berserker" },
        { "monster_floater", "Technician" },
        { "monster_hover", "Rocket Icarus" },
        { "monster_daedalus", "Daedalus" },
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
        { "monster_jorg", "Makron" },
        { "monster_gladb", "DarkMatter Gladiator" },
        { "monster_boss2_64", "N64 Hornet" },
        { "monster_boss2kl", "N64 Hornet" },
        { "monster_perrokl", "Infected Parasite" },
        { "monster_guncmdrkl", "Gunner Grenadier" },
        { "monster_shambler", "Shambler" },
        { "monster_shamblerkl", "Shambler" },
        { "monster_makronkl", "Makron" },
        { "monster_widow1", "Widow Apprentice" },
        { "monster_widow", "Widow Emperor" },
        { "monster_widow2", "Widow Creator" },
        { "monster_supertank", "Super-Tank" },
        { "monster_supertankkl", "Super-Tank!" },
        { "monster_boss5", "Super-Tank" },
        { "monster_turret", "TurretGun" },
        { "monster_turretkl", "TurretGun" },
        { "monster_boss2", "Hornet" }
    };

    auto it = name_replacements.find(ent->classname);
    std::string display_name = (it != name_replacements.end()) ? it->second : ent->classname;

    // Añadir el título basado en los flags
    std::string title = GetTitleFromFlags(ent->monsterinfo.bonus_flags);
    return title + display_name;
}

void ApplyBossEffects(edict_t* boss, bool isSmallMap, bool isMediumMap, bool isBigMap, float& health_multiplier, float& power_armor_multiplier) {
    health_multiplier = 1.0f;
    power_armor_multiplier = 1.0f;

    if (boss->monsterinfo.bonus_flags & BF_CHAMPION) {
        boss->s.scale = 1.3f;
        boss->mins *= 1.3f;
        boss->maxs *= 1.3f;
        boss->s.effects |= EF_ROCKET;
        boss->s.renderfx |= RF_SHELL_RED;
        health_multiplier = 1.5f;
        power_armor_multiplier = 1.5f;
    }
    if (boss->monsterinfo.bonus_flags & BF_CORRUPTED) {
        boss->s.scale = 1.5f;
        boss->mins *= 1.5f;
        boss->maxs *= 1.5f;
        boss->s.effects |= EF_FLIES;
    }
}