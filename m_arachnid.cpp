// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

ARACHNID UNIFIED IMPLEMENTATION

This file contains all arachnid variants:
- Standard arachnid (railgun)
- Arachnid2 (stronger railgun)
- PSX arachnid (smarter AI, can spawn minions)
- Spider (skinned variant of standard arachnid)
- GM arachnid (rocket variant)

==============================================================================
*/

#include "g_local.h"
#include "m_arachnid.h"
#include "m_flash.h"
#include "shared.h"

// Common spawn flags
constexpr spawnflags_t SPAWNFLAG_SPIDER = 8_spawnflag;

// Shared cached sounds for all arachnid types
static cached_soundindex sound_pain;
static cached_soundindex sound_death;
static cached_soundindex sound_sight;
static cached_soundindex sound_step;
static cached_soundindex sound_charge;
static cached_soundindex sound_melee;
static cached_soundindex sound_melee_hit;

// PSX specific sounds
static cached_soundindex sound_spawn;
static cached_soundindex sound_pissed;
// Plasma spider sounds
static cached_soundindex sound_plasma;
static cached_soundindex sound_plasma_hit;

// PSX reinforcement constants
constexpr const char* default_boss_reinforcements = "monster_tank_spawner 2";
constexpr const char* coop_reinforcements = "monster_stalker 2";
constexpr int32_t default_monster_slots_base = 5;

//==========================================================================================
// SHARED ANIMATIONS AND FUNCTIONS
//==========================================================================================

// Common stand animation
mframe_t arachnid_frames_stand[] = {
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand }
};
MMOVE_T(arachnid_move_stand) = { FRAME_idle1, FRAME_idle13, arachnid_frames_stand, nullptr };

// Common pain animations
mframe_t arachnid_frames_pain1[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};

mframe_t arachnid_frames_pain2[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};

// Common death animation
mframe_t arachnid_frames_death1[] = {
    { ai_move, 0 },
    { ai_move, -1.23f },
    { ai_move, -1.23f },
    { ai_move, -1.23f },
    { ai_move, -1.23f },
    { ai_move, -1.64f },
    { ai_move, -1.64f },
    { ai_move, -2.45f },
    { ai_move, -8.63f },
    { ai_move, -4.0f },
    { ai_move, -4.5f },
    { ai_move, -6.8f },
    { ai_move, -8.0f },
    { ai_move, -5.4f },
    { ai_move, -3.4f },
    { ai_move, -1.9f },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};

// Shared utility functions
void arachnid_footstep(edict_t* self)
{
    gi.sound(self, CHAN_BODY, sound_step, 0.5f, ATTN_IDLE, 0.0f);
}

void arachnid_melee_charge(edict_t* self)
{
    gi.sound(self, CHAN_WEAPON, sound_melee, 1.f, ATTN_NORM, 0.f);
}

void arachnid_charge_rail(edict_t* self)
{
    if (!self->enemy || !self->enemy->inuse)
        return;

    gi.sound(self, CHAN_WEAPON, sound_charge, 1.f, ATTN_NORM, 0.f);
    self->pos1 = self->enemy->s.origin;
    self->pos1[2] += self->enemy->viewheight;
}

void arachnid_dead(edict_t* self)
{
    self->mins = { -16, -16, -24 };
    self->maxs = { 16, 16, -8 };
    self->movetype = MOVETYPE_TOSS;
    self->svflags |= SVF_DEADMONSTER;
    self->nextthink = 0_ms;
    gi.linkentity(self);
}

// Shared die function
void arachnid_die_internal(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod, const mmove_t* death_move)
{
    // check for gib
    if (M_CheckGib(self, mod))
    {
        gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

        ThrowGibs(self, damage, {
            { 2, "models/objects/gibs/bone/tris.md2" },
            { 2, "models/objects/gibs/sm_meat/tris.md2" },
            { "models/monsters/gunner/gibs/chest.md2", GIB_METALLIC },
            { "models/monsters/gunner/gibs/garm.md2", GIB_METALLIC | GIB_UPRIGHT },
            { "models/monsters/gladiatr/gibs/rarm.md2", GIB_METALLIC | GIB_UPRIGHT },
            { "models/monsters/gunner/gibs/foot.md2", GIB_METALLIC },
            { "models/monsters/gunner/gibs/head.md2", GIB_METALLIC | GIB_HEAD }
            });
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    // regular death
    gi.sound(self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
    self->deadflag = true;
    self->takedamage = true;

    // Clear AI manual steering flag if it's set (mainly for PSX arachnid)
    self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

    M_SetAnimation(self, death_move);

    if (self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED) {
        BossDeathHandler(self);
    }
}

//==========================================================================================
// STANDARD ARACHNID IMPLEMENTATION 
//==========================================================================================

MONSTERINFO_SIGHT(arachnid_sight) (edict_t* self, edict_t* other) -> void
{
    gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_STAND(arachnid_stand) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid_move_stand);
}

// Walk animation
mframe_t arachnid_frames_walk[] = {
    { ai_walk, 8, arachnid_footstep },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8, arachnid_footstep },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 }
};
MMOVE_T(arachnid_move_walk) = { FRAME_walk1, FRAME_walk10, arachnid_frames_walk, nullptr };

MONSTERINFO_WALK(arachnid_walk) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid_move_walk);
}

// Run animation
mframe_t arachnid_frames_run[] = {
    { ai_run, 13, arachnid_footstep },
    { ai_run, 13 },
    { ai_run, 13 },
    { ai_run, 13 },
    { ai_run, 13 },
    { ai_run, 13, arachnid_footstep },
    { ai_run, 13 },
    { ai_run, 13 },
    { ai_run, 13 },
    { ai_run, 13 }
};
MMOVE_T(arachnid_move_run) = { FRAME_walk1, FRAME_walk10, arachnid_frames_run, nullptr };

MONSTERINFO_RUN(arachnid_run) (edict_t* self) -> void
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
    {
        M_SetAnimation(self, &arachnid_move_stand);
        return;
    }

    M_SetAnimation(self, &arachnid_move_run);
}

// Pain move animations
MMOVE_T(arachnid_move_pain1) = { FRAME_pain11, FRAME_pain15, arachnid_frames_pain1, arachnid_run };
MMOVE_T(arachnid_move_pain2) = { FRAME_pain21, FRAME_pain26, arachnid_frames_pain2, arachnid_run };

PAIN(arachnid_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + 3_sec;
    gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    float const r = frandom();

    if (r < 0.5f)
        M_SetAnimation(self, &arachnid_move_pain1);
    else
        M_SetAnimation(self, &arachnid_move_pain2);
}

// Rail gun firing
void arachnid_rail(edict_t* self)
{
    vec3_t start;
    vec3_t dir;
    vec3_t forward, right;
    monster_muzzleflash_id_t id;

    switch (self->s.frame)
    {
    case FRAME_rails3:
    default:
        id = MZ2_ARACHNID_RAIL1;
        break;
    case FRAME_rails7:
        id = MZ2_ARACHNID_RAIL2;
        break;
    case FRAME_rails_up2:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up5:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    case FRAME_rails_up9:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up11:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    }

    AngleVectors(self->s.angles, forward, right, nullptr);
    start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);

    // calc direction to where we targeted
    dir = self->pos1 - start;
    dir.normalize();

    monster_fire_railgun(self, start, dir, self->monsterinfo.IS_BOSS ? 40 : 35, 100, id);
}

// Attack animations
mframe_t arachnid_frames_attack1[] = {
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge, 0, arachnid_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge, 0, arachnid_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge }
};
MMOVE_T(arachnid_attack1) = { FRAME_rails2, FRAME_rails11, arachnid_frames_attack1, arachnid_run };

mframe_t arachnid_frames_attack_up1[] = {
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge, 0, arachnid_rail },
    { ai_charge },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge, 0, arachnid_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge, 0, arachnid_rail },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(arachnid_attack_up1) = { FRAME_rails_up1, FRAME_rails_up13, arachnid_frames_attack_up1, arachnid_run };

// Melee attack
void arachnid_melee_hit(edict_t* self)
{
    if (self->enemy) {
        if (!fire_hit(self, { MELEE_DISTANCE, 0, 0 }, 15, 50))
            self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
    }
    else {
        self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
    }
}

mframe_t arachnid_frames_melee[] = {
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge, 0, arachnid_melee_hit },
    { ai_charge, 0, arachnid_melee_hit },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge, 0, arachnid_melee_hit },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_hit },
    { ai_charge }
};
MMOVE_T(arachnid_melee) = { FRAME_melee_atk1, FRAME_melee_atk12, arachnid_frames_melee, arachnid_run };

void spider_charge_plasma(edict_t* self)
{
    if (!self->enemy || !self->enemy->inuse)
        return;

    // Play plasma charge sound instead of railgun sound
    gi.sound(self, CHAN_WEAPON, sound_plasma, 1.f, ATTN_NORM, 0.f);

}

// Improved plasma firing with better leading
void spider_plasma(edict_t* self)
{
    vec3_t start;
    vec3_t dir;
    vec3_t forward, right;
    monster_muzzleflash_id_t id;

    // Choose appropriate muzzle flash based on the animation frame
    switch (self->s.frame)
    {
    case FRAME_rails4:
    default:
        id = MZ2_ARACHNID_RAIL1;
        break;
    case FRAME_rails8:
        id = MZ2_ARACHNID_RAIL2;
        break;
    case FRAME_rails_up7:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up11:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    }


    // Use PredictAim to lead the target based on plasma projectile speed
    float constexpr plasma_speed = 900;
    PredictAim(self, self->enemy, start, plasma_speed, true, 0.0f, nullptr, &self->pos1);

    AngleVectors(self->s.angles, forward, right, nullptr);
    start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);

    // calc direction to where we targeted (predicted position)
    dir = self->pos1 - start;
    dir.normalize();

    int damage = 30;
    int radius_damage = 60;

    // Play proper plasma fire sound
    gi.sound(self, CHAN_WEAPON, sound_plasma, 1.f, ATTN_NORM, 0.f);

    // Fire plasma shot
    fire_plasma(self, start, dir, damage, 900, radius_damage, radius_damage);

    // Chance for enhanced shot at higher difficulties
    if (skill->integer >= 2 && frandom() < 0.35f) {
        fire_plasma(self, start, dir, damage * 0.7f, 1100, radius_damage * 0.7f, radius_damage * 0.7f);
    }
}

// Redefined attack frames with plasma shots at appropriate moments
mframe_t spider_frames_attack1[] = {
    { ai_charge, 0, spider_charge_plasma },  // New plasma charge function
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, spider_plasma },         // First plasma shot
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, spider_plasma },         // Second plasma shot
    { ai_charge }
};
MMOVE_T(spider_attack1) = { FRAME_rails1, FRAME_rails9, spider_frames_attack1, arachnid_run };

// Upward attack animations
mframe_t spider_frames_attack_up1[] = {
    { ai_charge, 0, spider_charge_plasma },  // New plasma charge function
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, spider_plasma },         // First plasma shot
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, spider_plasma },         // Second plasma shot
    { ai_charge },
    { ai_charge }
};
MMOVE_T(spider_attack_up1) = { FRAME_rails_up1, FRAME_rails_up13, spider_frames_attack_up1, arachnid_run };
// Attack decision

MONSTERINFO_ATTACK(spider_attack) (edict_t* self) -> void
{
    if (!self->enemy || !self->enemy->inuse)
        return;

    if (self->monsterinfo.melee_debounce_time < level.time && range_to(self, self->enemy) < MELEE_DISTANCE)
        M_SetAnimation(self, &arachnid_melee);
    else if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f)
        M_SetAnimation(self, &spider_attack_up1);
    else
        M_SetAnimation(self, &spider_attack1);
}

MONSTERINFO_ATTACK(arachnid_attack) (edict_t* self) -> void
{
    if (!self->enemy || !self->enemy->inuse)
        return;

    if (self->monsterinfo.melee_debounce_time < level.time && range_to(self, self->enemy) < MELEE_DISTANCE)
        M_SetAnimation(self, &arachnid_melee);
    else if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f)
        M_SetAnimation(self, &arachnid_attack_up1);
    else
        M_SetAnimation(self, &arachnid_attack1);
}

// Death animation
MMOVE_T(arachnid_move_death) = { FRAME_death1, FRAME_death20, arachnid_frames_death1, arachnid_dead };

DIE(arachnid_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
    arachnid_die_internal(self, inflictor, attacker, damage, point, mod, &arachnid_move_death);
}

//==========================================================================================
// ARACHNID2 IMPLEMENTATION
//==========================================================================================

MONSTERINFO_SIGHT(arachnid2_sight) (edict_t* self, edict_t* other) -> void
{
    gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_STAND(arachnid2_stand) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid_move_stand);
}

// Walk animation
mframe_t arachnid2_frames_walk[] = {
    { ai_walk, 8, arachnid_footstep },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8, arachnid_footstep },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 }
};
MMOVE_T(arachnid2_move_walk) = { FRAME_walk1, FRAME_walk10, arachnid2_frames_walk, nullptr };

MONSTERINFO_WALK(arachnid2_walk) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid2_move_walk);
}

// Run animation
mframe_t arachnid2_frames_run[] = {
    { ai_run, 8, arachnid_footstep },
    { ai_run, 8 },
    { ai_run, 8 },
    { ai_run, 8 },
    { ai_run, 8 },
    { ai_run, 8, arachnid_footstep },
    { ai_run, 8 },
    { ai_run, 8 },
    { ai_run, 8 },
    { ai_run, 8 }
};
MMOVE_T(arachnid2_move_run) = { FRAME_walk1, FRAME_walk10, arachnid2_frames_run, nullptr };

MONSTERINFO_RUN(arachnid2_run) (edict_t* self) -> void
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
    {
        M_SetAnimation(self, &arachnid_move_stand);
        return;
    }

    M_SetAnimation(self, &arachnid2_move_run);
}

// Pain animations
MMOVE_T(arachnid2_move_pain1) = { FRAME_pain11, FRAME_pain15, arachnid_frames_pain1, arachnid2_run };
MMOVE_T(arachnid2_move_pain2) = { FRAME_pain21, FRAME_pain26, arachnid_frames_pain2, arachnid2_run };

PAIN(arachnid2_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + 3_sec;
    gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    float const r = frandom();

    if (r < 0.5f)
        M_SetAnimation(self, &arachnid2_move_pain1);
    else
        M_SetAnimation(self, &arachnid2_move_pain2);
}

// Rail gun for arachnid2
void arachnid2_rail(edict_t* self)
{
    vec3_t start;
    vec3_t dir;
    vec3_t forward, right;
    monster_muzzleflash_id_t id;

    switch (self->s.frame)
    {
    case FRAME_rails6:
    default:
        id = MZ2_ARACHNID_RAIL1;
        break;
    case FRAME_rails10:
        id = MZ2_ARACHNID_RAIL2;
        break;
    case FRAME_rails_up7:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up11:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    }

    AngleVectors(self->s.angles, forward, right, nullptr);
    start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);

    // calc direction to where we targeted
    dir = self->pos1 - start;
    dir.normalize();

    monster_fire_railgun(self, start, dir, 50, 100, id);
}

// Attack animations for arachnid2
mframe_t arachnid2_frames_attack1[] = {
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_rail },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_rail },
    { ai_charge }
};
MMOVE_T(arachnid2_attack1) = { FRAME_rails3, FRAME_rails11, arachnid2_frames_attack1, arachnid2_run };

mframe_t arachnid2_frames_attack_up1[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_rail },
    { ai_charge, 0, arachnid_charge_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
};
MMOVE_T(arachnid2_attack_up1) = { FRAME_rails_up1, FRAME_rails_up16, arachnid2_frames_attack_up1, arachnid2_run };

// Melee attack for arachnid2
void arachnid2_melee_hit(edict_t* self)
{
    if (self->style == 1)
    {
        if (!fire_hit(self, { MELEE_DISTANCE, 0, 0 }, 30, 50))
            self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
    }
    else
    {
        if (!fire_hit(self, { MELEE_DISTANCE, 0, 0 }, 20, 50))
            self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
    }
}

mframe_t arachnid2_frames_melee[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_melee_hit },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge },
    { ai_charge, 0, arachnid2_melee_hit },
    { ai_charge }
};
MMOVE_T(arachnid2_melee) = { FRAME_melee_atk1, FRAME_melee_atk12, arachnid2_frames_melee, arachnid2_run };

// GM Arachnid (Rocket variant) functionality
static void gm_arachnid_blind_check(edict_t* self)
{
    if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
    {
        vec3_t const aim = self->monsterinfo.blind_fire_target - self->s.origin;
        self->ideal_yaw = vectoyaw(aim);
    }
}

void gm_arachnid_rockets(edict_t* self)
{
    vec3_t                      start;
    vec3_t                      dir;
    vec3_t                      forward, right;
    vec3_t                      vec;
    monster_muzzleflash_id_t    id;
    int                         rocketSpeed;
    vec3_t                      target;
    bool                        const blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;

    switch (self->s.frame)
    {
    case FRAME_rails4:
    default:
        id = MZ2_ARACHNID_RAIL1;
        break;
    case FRAME_rails8:
        id = MZ2_ARACHNID_RAIL2;
        break;
    case FRAME_rails_up4:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up6:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    case FRAME_rails_up10:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up12:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    }

    AngleVectors(self->s.angles, forward, right, nullptr);
    start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);

    rocketSpeed = 800;

    if (blindfire)
        target = self->monsterinfo.blind_fire_target;
    else
        target = self->enemy->s.origin;

    if (blindfire)
    {
        vec = target;
        dir = vec - start;
    }
    else if (frandom() < 0.66f || (start[2] < self->enemy->absmin[2]))
    {
        vec = self->enemy->s.origin;
        vec[2] += self->enemy->viewheight;
        dir = vec - start;
    }
    else
    {
        vec = self->enemy->s.origin;
        vec[2] = self->enemy->absmin[2] + 1;
        dir = vec - start;
    }

    if ((!blindfire) && ((frandom() < (0.2f + ((3 - skill->integer) * 0.15f)))))
        PredictAim(self, self->enemy, start, rocketSpeed, false, 0, &dir, &vec);

    dir.normalize();

    trace_t trace{}; // Check target
    trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
    if (blindfire)
    {
        // blindfire has different fail criteria for the trace
        if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
        {
            // RAFAEL
            if (self->s.skinnum > 1)
                monster_fire_heat(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.075f);
            else
                // RAFAEL
                monster_fire_rocket(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1);
        }
        else
        {
            vec = target;
            vec += (right * -10);
            dir = vec - start;
            dir.normalize();
            trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
            if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
            {
                monster_fire_heat(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.075f);
            }
            else
            {
                vec = target;
                vec += (right * 10);
                dir = vec - start;
                dir.normalize();
                trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
                if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
                {
                    monster_fire_heat(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.075f);
                }
            }
        }
    }
    else
    {
        if (trace.fraction > 0.5f || trace.ent->solid != SOLID_BSP)
        {
            monster_fire_heat(self, start, dir, 50, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.095f);
        }
    }
}

// GM Arachnid attack animations
mframe_t gm_arachnid_frames_attack1[] = {
    { ai_charge, 0, gm_arachnid_blind_check },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge }
};
MMOVE_T(gm_arachnid_attack1) = { FRAME_rails5, FRAME_rails11, gm_arachnid_frames_attack1, arachnid2_run };

mframe_t gm_arachnid_frames_attack_up1[] = {
    { ai_charge, 0, gm_arachnid_blind_check },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge, 0, gm_arachnid_rockets },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(gm_arachnid_attack_up1) = { FRAME_rails_up3, FRAME_rails_up16, gm_arachnid_frames_attack_up1, arachnid2_run };

// Attack decision for arachnid2
MONSTERINFO_ATTACK(arachnid2_attack) (edict_t* self) -> void
{
    if (!self->enemy || !self->enemy->inuse)
        return;
    if (self->monsterinfo.melee_debounce_time < level.time && range_to(self, self->enemy) < MELEE_DISTANCE)
        M_SetAnimation(self, &arachnid2_melee);
    else
    {
        if (self->style == 1)
        {
            if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f)
                M_SetAnimation(self, &gm_arachnid_attack_up1);
            else
                M_SetAnimation(self, &gm_arachnid_attack1);
        }
        else
        {
            if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f)
                M_SetAnimation(self, &arachnid2_attack_up1);
            else
                M_SetAnimation(self, &arachnid2_attack1);
        }
    }
}

// Death for arachnid2
MMOVE_T(arachnid2_move_death) = { FRAME_death1, FRAME_death20, arachnid_frames_death1, arachnid_dead };

DIE(arachnid2_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
    arachnid_die_internal(self, inflictor, attacker, damage, point, mod, &arachnid2_move_death);
}

// Skin management
MONSTERINFO_SETSKIN(arachnid2_setskin) (edict_t* self) -> void
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum |= 1;
    else
        self->s.skinnum &= ~1;
}

//spider skinnum, fixing wounded skin
MONSTERINFO_SETSKIN(spider_setskin) (edict_t* self) -> void
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum |= 1;
    else
        self->s.skinnum &= ~1;
}

//==========================================================================================
// PSX ARACHNID IMPLEMENTATION
//==========================================================================================

MONSTERINFO_SIGHT(arachnid_psx_sight) (edict_t* self, edict_t* other) -> void
{
    gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_STAND(arachnid_psx_stand) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid_move_stand);
}

// Walk animation
mframe_t arachnid_psx_frames_walk[] = {
    { ai_walk, 2, arachnid_footstep },
    { ai_walk, 5 },
    { ai_walk, 12 },
    { ai_walk, 16 },
    { ai_walk, 5 },
    { ai_walk, 8, arachnid_footstep },
    { ai_walk, 8 },
    { ai_walk, 12 },
    { ai_walk, 9 },
    { ai_walk, 5 }
};
MMOVE_T(arachnid_psx_move_walk) = { FRAME_walk1, FRAME_walk10, arachnid_psx_frames_walk, nullptr };

MONSTERINFO_WALK(arachnid_psx_walk) (edict_t* self) -> void
{
    M_SetAnimation(self, &arachnid_psx_move_walk);
}

// Run animation
mframe_t arachnid_psx_frames_run[] = {
    { ai_run, 2, arachnid_footstep },
    { ai_run, 5 },
    { ai_run, 12 },
    { ai_run, 16 },
    { ai_run, 5 },
    { ai_run, 8, arachnid_footstep },
    { ai_run, 8 },
    { ai_run, 12 },
    { ai_run, 9 },
    { ai_run, 5 }
};
MMOVE_T(arachnid_psx_move_run) = { FRAME_walk1, FRAME_walk10, arachnid_psx_frames_run, nullptr };

MONSTERINFO_RUN(arachnid_psx_run) (edict_t* self) -> void
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
    {
        M_SetAnimation(self, &arachnid_move_stand);
        return;
    }

    M_SetAnimation(self, &arachnid_psx_move_run);
}

// Pain animations
MMOVE_T(arachnid_psx_move_pain1) = { FRAME_pain11, FRAME_pain15, arachnid_frames_pain1, arachnid_psx_run };
MMOVE_T(arachnid_psx_move_pain2) = { FRAME_pain21, FRAME_pain26, arachnid_frames_pain2, arachnid_psx_run };

PAIN(arachnid_psx_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + 3_sec;
    gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

    float const r = frandom();

    if (r < 0.5f)
        M_SetAnimation(self, &arachnid_psx_move_pain1);
    else
        M_SetAnimation(self, &arachnid_psx_move_pain2);
}

// PSX arachnid rail gun
static void arachnid_psx_charge_rail(edict_t* self, monster_muzzleflash_id_t mz)
{
    if (!self->enemy || !self->enemy->inuse)
        return;

    gi.sound(self, CHAN_WEAPON, sound_charge, 1.f, ATTN_NORM, 0.f);

    vec3_t forward, right, start;
    AngleVectors(self->s.angles, forward, right, nullptr);
    start = M_ProjectFlashSource(self, monster_flash_offset[mz], forward, right);

    PredictAim(self, self->enemy, start, 0, false, 0.0f, nullptr, &self->pos1);
}

static void arachnid_psx_charge_rail_left(edict_t* self)
{
    arachnid_psx_charge_rail(self, MZ2_ARACHNID_RAIL1);
}

static void arachnid_psx_charge_rail_right(edict_t* self)
{
    arachnid_psx_charge_rail(self, MZ2_ARACHNID_RAIL2);
}

static void arachnid_psx_charge_rail_up_left(edict_t* self)
{
    arachnid_psx_charge_rail(self, MZ2_ARACHNID_RAIL_UP1);
}

static void arachnid_psx_charge_rail_up_right(edict_t* self)
{
    arachnid_psx_charge_rail(self, MZ2_ARACHNID_RAIL_UP2);
}

void arachnid_psx_rail_real(edict_t* self, monster_muzzleflash_id_t id)
{
    vec3_t start;
    vec3_t dir;
    vec3_t forward, right, up;

    AngleVectors(self->s.angles, forward, right, up);
    start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);
    int dmg = 50;

    if (self->s.frame >= FRAME_melee_in1 && self->s.frame <= FRAME_melee_in16)
    {
        // scan our current direction for players
        std::array<edict_t*, 8> players_scanned{};
        size_t num_players = 0;

        for (auto player : active_players())
        {
            if (!visible(self, player, false))
                continue;

            if (infront_cone(self, player, 0.5f))
            {
                players_scanned[num_players++] = player;

                if (num_players == players_scanned.size())
                    break;
            }
        }

        if (num_players != 0)
        {
            edict_t* chosen = players_scanned[irandom(num_players)];

            PredictAim(self, chosen, start, 0, false, 0.0f, nullptr, &self->pos1);

            dir = (chosen->s.origin - self->s.origin).normalized();

            self->ideal_yaw = vectoyaw(dir);
            self->s.angles[YAW] = self->ideal_yaw;

            dir = (self->pos1 - start).normalized();

            for (int i = 0; i < 3; i++)
                dir[i] += crandom_open() * 0.018f;
            dir = dir.normalized();
        }
        else
        {
            dir = forward;
        }
    }
    else
    {
        // calc direction to where we targeted
        dir = (self->pos1 - start).normalized();
        dmg = 50;
    }

    bool const hit = monster_fire_railgun(self, start, dir, dmg, dmg * 2.0f, id);

    if (dmg == 50)
    {
        if (hit)
            self->count = 0;
        else
            self->count++;
    }
}

void arachnid_psx_rail(edict_t* self)
{
    monster_muzzleflash_id_t id;

    switch (self->s.frame)
    {
    case FRAME_rails4:
    default:
        id = MZ2_ARACHNID_RAIL1;
        break;
    case FRAME_rails8:
        id = MZ2_ARACHNID_RAIL2;
        break;
    case FRAME_rails_up7:
        id = MZ2_ARACHNID_RAIL_UP1;
        break;
    case FRAME_rails_up11:
        id = MZ2_ARACHNID_RAIL_UP2;
        break;
    }

    arachnid_psx_rail_real(self, id);
}

// PSX arachnid attack
mframe_t arachnid_psx_frames_attack1[] = {
    { ai_charge },
    { ai_charge, 0, arachnid_psx_charge_rail_left },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_rail },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_charge_rail_right },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(arachnid_psx_attack1) = { FRAME_rails1, FRAME_rails11, arachnid_psx_frames_attack1, arachnid_psx_run };

mframe_t arachnid_psx_frames_attack_up1[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_charge_rail_up_left },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_rail },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_charge_rail_up_right },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
};
MMOVE_T(arachnid_psx_attack_up1) = { FRAME_rails_up1, FRAME_rails_up16, arachnid_psx_frames_attack_up1, arachnid_psx_run };

// PSX arachnid melee attack
void arachnid_psx_melee_hit(edict_t* self)
{
    if (!fire_hit(self, { MELEE_DISTANCE, 0, 0 }, 15, 50))
    {
        self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
        self->count++;
    }
    else if (self->s.frame == FRAME_melee_atk11 &&
        self->monsterinfo.melee_debounce_time < level.time)
        self->monsterinfo.nextframe = FRAME_melee_atk2;
}

// Melee animations
mframe_t arachnid_psx_frames_melee_out[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(arachnid_psx_melee_out) = { FRAME_melee_out1, FRAME_melee_out3, arachnid_psx_frames_melee_out, arachnid_psx_run };

void arachnid_psx_to_out_melee(edict_t* self)
{
    M_SetAnimation(self, &arachnid_psx_melee_out);
}

mframe_t arachnid_psx_frames_melee[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_melee_hit },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_melee_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_melee_hit },
    { ai_charge }
};
MMOVE_T(arachnid_psx_melee) = { FRAME_melee_atk1, FRAME_melee_atk12, arachnid_psx_frames_melee, arachnid_psx_to_out_melee };

void arachnid_psx_to_melee(edict_t* self)
{
    M_SetAnimation(self, &arachnid_psx_melee);
}

mframe_t arachnid_psx_frames_melee_in[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(arachnid_psx_melee_in) = { FRAME_melee_in1, FRAME_melee_in3, arachnid_psx_frames_melee_in, arachnid_psx_to_melee };

// Rapid fire rail
static void arachnid_psx_rail_rapid(edict_t* self)
{
    bool const left_shot = self->s.frame == FRAME_melee_in9;
    arachnid_psx_rail_real(self, left_shot ? MZ2_ARACHNID_RAIL1 : MZ2_ARACHNID_RAIL2);
}

mframe_t arachnid_psx_frames_attack3[] = {
    { ai_charge },
    { ai_move, 0, arachnid_psx_rail_rapid },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, arachnid_psx_rail_rapid },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, arachnid_psx_rail_rapid },
    { ai_move },
    { ai_move },
    { ai_charge }
};
MMOVE_T(arachnid_psx_attack3) = { FRAME_melee_in4, FRAME_melee_in16, arachnid_psx_frames_attack3, arachnid_psx_to_out_melee };

static void arachnid_psx_rapid_fire(edict_t* self)
{
    self->count = 0;
    M_SetAnimation(self, &arachnid_psx_attack3);
}

// Spawning function for PSX arachnid (skill 3 only)
static void arachnid_psx_spawn(edict_t* self)
{
    if (skill->integer != 3)
        return;

    static constexpr vec3_t reinforcement_position[] = { { -24.f, 124.f, 0 }, { -24.f, -124.f, 0 } };
    vec3_t f, r, offset, startpoint, spawnpoint;
    int    count;

    AngleVectors(self->s.angles, f, r, nullptr);

    int num_summoned;
    self->monsterinfo.chosen_reinforcements = M_PickReinforcements(self, num_summoned, 2);

    for (count = 0; count < num_summoned; count++)
    {
        offset = reinforcement_position[count];

        if (self->s.scale)
            offset *= self->s.scale;

        startpoint = M_ProjectFlashSource(self, offset, f, r);
        // a little off the ground
        startpoint[2] += 10 * (self->s.scale ? self->s.scale : 1.0f);

        auto& reinforcement = self->monsterinfo.reinforcements.reinforcements[self->monsterinfo.chosen_reinforcements[count]];

        if (FindSpawnPoint(startpoint, reinforcement.mins, reinforcement.maxs, spawnpoint, 32))
        {
            if (CheckGroundSpawnPoint(spawnpoint, reinforcement.mins, reinforcement.maxs, 256, -1))
            {
                edict_t* ent = CreateGroundMonster(spawnpoint, self->s.angles, reinforcement.mins, reinforcement.maxs, reinforcement.classname, 256);

                if (!ent)
                    return;

                ent->nextthink = level.time;
                ent->think(ent);

                ent->monsterinfo.aiflags |= AI_SPAWNED_COMMANDER | AI_DO_NOT_COUNT | AI_IGNORE_SHOTS;
                ent->monsterinfo.commander = self;
                ent->monsterinfo.slots_from_commander = reinforcement.strength;
                self->monsterinfo.monster_used += reinforcement.strength;

                gi.sound(ent, CHAN_BODY, sound_spawn, 1, ATTN_NONE, 0);

                if ((self->enemy->inuse) && (self->enemy->health > 0))
                {
                    ent->enemy = self->enemy;
                    FoundTarget(ent);
                }

                float const radius = (reinforcement.maxs - reinforcement.mins).length() * 0.5f;
                SpawnGrow_Spawn(spawnpoint + (reinforcement.mins + reinforcement.maxs), radius, radius * 2.f);
            }
        }
    }
}

// Taunt animation and spawning
mframe_t arachnid_psx_frames_taunt[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, arachnid_psx_spawn },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
MMOVE_T(arachnid_psx_taunt) = { FRAME_melee_pain1, FRAME_melee_pain16, arachnid_psx_frames_taunt, arachnid_psx_rapid_fire };

// PSX arachnid attack decision
MONSTERINFO_ATTACK(arachnid_psx_attack) (edict_t* self) -> void
{
    if (!self->enemy || !self->enemy->inuse)
        return;

    if (self->monsterinfo.melee_debounce_time < level.time && range_to(self, self->enemy) < MELEE_DISTANCE)
        M_SetAnimation(self, &arachnid_psx_melee_in);
    // annoyed rapid fire attack
    else if (self->enemy->client &&
        self->last_move_time <= level.time &&
        self->count >= 4 &&
        frandom() < (max(self->count / 2.0f, 4.0f) + 1.0f) * 0.2f &&
        (M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL1]) || M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL2])))
    {
        M_SetAnimation(self, &arachnid_psx_taunt);
        gi.sound(self, CHAN_VOICE, sound_pissed, 1.f, 0.25f, 0.f);
        self->count = 0;
        self->pain_debounce_time = level.time + 4.5_sec;
        self->last_move_time = level.time + 10_sec;
    }
    else if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f &&
        (M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL_UP1]) || M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL_UP2])))
        M_SetAnimation(self, &arachnid_psx_attack_up1);
    else if (M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL1]) || M_CheckClearShot(self, monster_flash_offset[MZ2_ARACHNID_RAIL2]))
        M_SetAnimation(self, &arachnid_psx_attack1);
}

// PSX arachnid death
MMOVE_T(arachnid_psx_move_death) = { FRAME_death1, FRAME_death20, arachnid_frames_death1, arachnid_dead };

DIE(arachnid_psx_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
    arachnid_die_internal(self, inflictor, attacker, damage, point, mod, &arachnid_psx_move_death);
}

// PSX arachnid skin management
MONSTERINFO_SETSKIN(arachnid_psx_setskin) (edict_t* self) -> void
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

//==========================================================================================
// SPAWN FUNCTIONS
//==========================================================================================

// Helper function to initialize common sound effects
void initialize_arachnid_sounds()
{
    static bool sounds_initialized = false;

    if (!sounds_initialized)
    {
        sound_step.assign("insane/insane11.wav");
        sound_charge.assign("gladiator/railgun.wav");
        sound_melee.assign("gladiator/melee3.wav");
        sound_melee_hit.assign("gladiator/melee2.wav");
        sound_pain.assign("arachnid/pain.wav");
        sound_death.assign("arachnid/death.wav");
        sound_sight.assign("arachnid/sight.wav");

        sounds_initialized = true;
    }
}

/*QUAKED monster_arachnid (1 .5 0) (-48 -48 -20) (48 48 48) Ambush Trigger_Spawn Sight
*/
void SP_monster_arachnid(edict_t* self)
{
    const spawn_temp_t& st = ED_GetSpawnTemp();

    if (!M_AllowSpawn(self)) {
        G_FreeEdict(self);
        return;
    }

    initialize_arachnid_sounds();

    self->s.modelindex = gi.modelindex("models/monsters/arachnid/tris.md2");
    self->mins = { -48, -48, -20 };
    self->maxs = { 48, 48, 48 };
    self->movetype = MOVETYPE_STEP;
    self->solid = SOLID_BBOX;

    self->monsterinfo.scale = MODEL_SCALE;
    self->mass = 450;

    self->pain = arachnid_pain;
    self->die = arachnid_die;
    self->monsterinfo.stand = arachnid_stand;
    self->monsterinfo.walk = arachnid_walk;
    self->monsterinfo.run = arachnid_run;
    self->monsterinfo.attack = arachnid_attack;
    self->monsterinfo.sight = arachnid_sight;

    gi.linkentity(self);

    M_SetAnimation(self, &arachnid_move_stand);

    walkmonster_start(self);

    if (!strcmp(self->classname, "monster_arachnid") && self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED) {
        self->health = 3500 + (1.08 * current_wave_level);
        self->gib_health = -99999;
    }
    else if (!strcmp(self->classname, "monster_arachnid") && !self->monsterinfo.IS_BOSS)
    {
        self->health = 1000 * st.health_multiplier;
        self->gib_health = -200;
    }
    ApplyMonsterBonusFlags(self);
}

/*QUAKED monster_spider (1 .5 0) (-48 -48 -20) (48 48 48) Ambush Trigger_Spawn Sight
*/
void SP_monster_spider(edict_t* self)
{
    const spawn_temp_t& st = ED_GetSpawnTemp();

    // Initialize plasma sounds
    sound_plasma.assign("weapons/plasshot.wav");
    sound_plasma_hit.assign("weapons/plasma/hit.wav");

    gi.soundindex("weapons/plasma/fire1.wav");  // Plasma firing sound
    gi.soundindex("weapons/plasma/hit.wav");    // Plasma impact sound

    self->spawnflags |= SPAWNFLAG_SPIDER;
    SP_monster_arachnid(self);

    // Override the attack function with our plasma variant
    self->monsterinfo.attack = spider_attack;

    // Add plasma weapon visuals
    //self->s.effects = EF_PLASMA;
    self->monsterinfo.weapon_sound = gi.soundindex("weapons/phaloop.wav");

    self->s.skinnum = 1;
    if (!strcmp(self->classname, "monster_spider")) {
        self->s.scale = 0.7f;
        self->health = IsFirstThreeWaves(current_wave_level) ? 350 * st.health_multiplier : 650 * st.health_multiplier;
        self->max_health = self->health;
        self->mins = { -41, -41, -17 };
        self->maxs = { 41, 41, 41 };
    }
    ApplyMonsterBonusFlags(self);
}

/*QUAKED monster_arachnid2 (1 .5 0) (-48 -48 -20) (48 48 48) Ambush Trigger_Spawn Sight
*/
void SP_monster_arachnid2(edict_t* self)
{
    const spawn_temp_t& st = ED_GetSpawnTemp();

    if (!M_AllowSpawn(self)) {
        G_FreeEdict(self);
        return;
    }

    initialize_arachnid_sounds();

    self->s.modelindex = gi.modelindex("models/monsters/arachnid/tris.md2");
    self->mins = { -48, -48, -20 };
    self->maxs = { 48, 48, 48 };
    self->movetype = MOVETYPE_STEP;
    self->solid = SOLID_BBOX;
    self->monsterinfo.scale = MODEL_SCALE;

    if (!strcmp(self->classname, "monster_arachnid2") && !self->monsterinfo.IS_BOSS)
    {
        self->s.scale = 0.85f;
        self->mins = { -41, -41, -17 };
        self->maxs = { 41, 41, 41 };
    }
    self->gib_health = -200;
    self->mass = 450;
    self->health = 1000 * st.health_multiplier;

    self->pain = arachnid2_pain;
    self->die = arachnid2_die;
    self->monsterinfo.stand = arachnid2_stand;
    self->monsterinfo.walk = arachnid2_walk;
    self->monsterinfo.run = arachnid2_run;
    self->monsterinfo.attack = arachnid2_attack;
    self->monsterinfo.sight = arachnid2_sight;
    self->monsterinfo.setskin = arachnid2_setskin;

    gi.linkentity(self);

    M_SetAnimation(self, &arachnid_move_stand);

    walkmonster_start(self);
    ApplyMonsterBonusFlags(self);
}

/*QUAKED monster_gm_arachnid (1 .5 0) (-48 -48 -20) (48 48 48) Ambush Trigger_Spawn Sight
*/
void SP_monster_gm_arachnid(edict_t* self)
{
    const spawn_temp_t& st = ED_GetSpawnTemp();

    SP_monster_arachnid2(self);

    self->monsterinfo.armor_type = IT_ARMOR_COMBAT;
    self->monsterinfo.armor_power = 500;
    self->style = 1;
    self->health = 1000 * st.health_multiplier;
    if (g_horde->integer) {
        self->s.scale = 0.85f;
        self->mins = { -48, -48, -20 };
        self->maxs = { 48, 48, 48 };
    }

    if (!strcmp(self->classname, "monster_gm_arachnid") && self->monsterinfo.IS_BOSS && !self->monsterinfo.BOSS_DEATH_HANDLED) {
        self->health = 2800 + (1.08 * current_wave_level);
        self->mins = { -41, -41, -17 };
        self->maxs = { 41, 41, 41 };
    }
    ApplyMonsterBonusFlags(self);
}

/*QUAKED monster_psxarachnid (1 .5 0) (-40 -40 -20) (40 40 48) Ambush Trigger_Spawn Sight
*/
void SP_monster_psxarachnid(edict_t* self)
{
    const spawn_temp_t& st = ED_GetSpawnTemp();

    if (!M_AllowSpawn(self)) {
        G_FreeEdict(self);
        return;
    }

    initialize_arachnid_sounds();
    sound_pissed.assign("guncmdr/gcdrsrch1.wav");

    const char* reinforcements = nullptr;

    sound_spawn.assign("medic_commander/monsterspawn1.wav");
    if (self->monsterinfo.IS_BOSS)
        reinforcements = default_boss_reinforcements;
    else
        reinforcements = coop_reinforcements;

    if (!st.was_key_specified("monster_slots"))
        self->monsterinfo.monster_slots = default_monster_slots_base;
    if (st.was_key_specified("reinforcements"))
        reinforcements = st.reinforcements;
    if (self->monsterinfo.monster_slots && reinforcements && *reinforcements)
        M_SetupReinforcements(reinforcements, self->monsterinfo.reinforcements);

    self->s.modelindex = gi.modelindex("models/monsters/arachnid/tris.md2");
    self->mins = { -40, -40, -20 };
    self->maxs = { 40, 40, 48 };
    self->movetype = MOVETYPE_STEP;
    self->solid = SOLID_BBOX;

    if (!st.was_key_specified("power_armor_type") && self->monsterinfo.IS_BOSS)
        self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
    if (!st.was_key_specified("power_armor_power") && self->monsterinfo.IS_BOSS)
        self->monsterinfo.power_armor_power = 2500;

    self->health = 1000 * st.health_multiplier;
    self->gib_health = -200;

    self->monsterinfo.scale = MODEL_SCALE;
    self->mass = 450;

    self->pain = arachnid_psx_pain;
    self->die = arachnid_psx_die;
    self->monsterinfo.stand = arachnid_psx_stand;
    self->monsterinfo.walk = arachnid_psx_walk;
    self->monsterinfo.run = arachnid_psx_run;
    self->monsterinfo.attack = arachnid_psx_attack;
    self->monsterinfo.sight = arachnid_psx_sight;
    self->monsterinfo.setskin = arachnid_psx_setskin;

    self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

    gi.linkentity(self);

    M_SetAnimation(self, &arachnid_move_stand);

    walkmonster_start(self);

    ApplyMonsterBonusFlags(self);
}