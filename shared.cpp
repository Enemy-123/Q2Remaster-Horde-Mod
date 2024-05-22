// shared.cpp

#include "shared.h"

std::string GetTitleFromFlags(int bonus_flags) {
    std::string title;
    if (bonus_flags & BF_CHAMPION) {
        title += "Champion ";
    }
    if (bonus_flags & BF_PLAGUED) {
        title += "Plagued ";
    }
    if (bonus_flags & BF_INVICTUS) {
        title += "Invictus ";
    }
    if (bonus_flags & BF_BERSERKER) {
        title += "Bloodthirsty ";
    }
    if (bonus_flags & BF_POSESSED) {
        title += "Posessed ";
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
        { "monster_jorg", "Jorg" },
        { "monster_gladb", "DarkMatter Gladiator" },
        { "monster_boss2_64", "N64 Hornet" },
        { "monster_boss2kl", "N64 Hornet" },
        { "monster_perrokl", "Infected Parasite" },
        { "monster_guncmdrkl", "Enraged Gunner Grenadier" },
        { "monster_shamblerkl", "Stygian Shambler" },
        { "monster_makronkl", "Ghostly Makron" },
        { "monster_widow1", "Widow Apprentice" },
        { "monster_widow", "Widow Emperor" },
        { "monster_widow2", "Widow Creator" },
        { "monster_supertank", "Super-Tank" },
        { "monster_supertankkl", "Super-Tank!" },
        { "monster_boss5", "Super-Tank" },
        { "monster_turret", "TurretGun" },
        { "monster_turretkl", "TurretGun" },
        { "monster_boss2", "Hornet" }
        // Add other replacements as needed
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
        boss->s.effects |= EF_ROCKET;
        boss->s.renderfx |= RF_SHELL_RED;
        health_multiplier = 1.5f;
        power_armor_multiplier = 1.5f;
    }
    if (boss->monsterinfo.bonus_flags & BF_PLAGUED) {
        boss->s.scale = 1.5f;
        boss->s.effects |= EF_FLIES;
        health_multiplier = 1.4f;
        power_armor_multiplier = 1.4f;
    }
    if (boss->monsterinfo.bonus_flags & BF_INVICTUS) {
        boss->s.scale = 1.0f;
        boss->s.effects |= EF_BLUEHYPERBLASTER;
        boss->s.renderfx |= RF_TRANSLUCENT;
        health_multiplier = 1.0f;
        power_armor_multiplier = 1.6f;
    }
    if (boss->monsterinfo.bonus_flags & BF_BERSERKER) {
        boss->s.scale = 1.4f;
        boss->s.effects |= EF_GIB | EF_FIREBALL;
        health_multiplier = 1.3f;
        power_armor_multiplier = 1.3f;
    }
    if (boss->monsterinfo.bonus_flags & BF_POSESSED) {
        boss->s.scale = 1.8f;
        boss->s.effects |= EF_BARREL_EXPLODING | EF_SPHERETRANS;
        health_multiplier = 1.7f;
        power_armor_multiplier = 1.7f;
    }

    if (isSmallMap) {
        boss->s.scale *= 0.8f;
    }
    else if (isMediumMap) {
        boss->s.scale *= 1.2f;
    }
    else if (isBigMap) {
        boss->s.scale *= 1.4f;
    }
}
