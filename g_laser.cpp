#include "g_local.h"

constexpr int32_t MAX_LASERS = 6;
constexpr int32_t LASER_COST = 50;
constexpr int32_t LASER_DAMAGE = 100;
constexpr int32_t LASER_HEALTH = 500;
constexpr gtime_t LASER_TIMEOUT = 30_sec;

THINK(laser_think) (edict_t* self) -> void
{
    vec3_t start, end{}, forward;
    trace_t tr;

    if (!self->owner || !self->owner->inuse || self->owner->health <= 0)
    {
        G_FreeEdict(self);
        return;
    }

    AngleVectors(self->s.angles, forward, nullptr, nullptr);
    VectorCopy(self->s.origin, start);
    VectorMA(start, 8192, forward, end);

    tr = gi.traceline(start, end, self, MASK_SHOT);

    if (tr.fraction < 1.0f)
    {
        VectorCopy(tr.endpos, self->s.old_origin);

        if (tr.ent->takedamage && tr.ent != self->owner)
        {
            T_Damage(tr.ent, self, self->owner, forward, tr.endpos, vec3_origin,
                self->dmg, 1, DAMAGE_ENERGY, MOD_TARGET_LASER);
        }
    }
    else
    {
        VectorCopy(end, self->s.old_origin);
    }

    self->nextthink = level.time + FRAME_TIME_MS;
}

DIE(laser_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
    if (self->owner)
        self->owner->client->num_lasers--;
    G_FreeEdict(self);
}

void fire_laser(edict_t* ent, const vec3_t& start, const vec3_t& aimdir)
{
    edict_t* laser;
    vec3_t dir;

    dir = vectoangles(aimdir);

    laser = G_Spawn();
    laser->classname = "player_laser";
    laser->s.origin = start;
    laser->s.angles = dir;
    laser->movetype = MOVETYPE_NONE;
    laser->solid = SOLID_BBOX;
    laser->s.renderfx = RF_BEAM | RF_TRANSLUCENT;
    laser->s.modelindex = 1;  // must be non-zero
    laser->s.frame = 2;
    laser->s.skinnum = 0xf0f0f0f0;  // red color
    laser->owner = ent;
    laser->dmg = LASER_DAMAGE;
    laser->think = laser_think;
    laser->nextthink = level.time + FRAME_TIME_MS;
    laser->svflags |= SVF_NOCLIENT;
    laser->flags |= FL_DAMAGEABLE;
    laser->takedamage = true;
    laser->health = LASER_HEALTH;
    laser->die = laser_die;

    VectorSet(laser->mins, -8, -8, -8);
    VectorSet(laser->maxs, 8, 8, 8);

    gi.linkentity(laser);

    gi.sound(laser, CHAN_AUTO, gi.soundindex("weapons/laser_hum.wav"), 1, ATTN_NORM, 0);

    ent->client->num_lasers++;
}

void create_laser(edict_t* ent)
{
    vec3_t forward, right, up, start;

    if (ent->client->num_lasers >= MAX_LASERS)
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Maximum number of lasers reached.\n");
        return;
    }

    if (ent->client->pers.inventory[IT_AMMO_CELLS] < LASER_COST)
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Not enough cells to create a laser.\n");
        return;
    }

    AngleVectors(ent->client->v_angle, forward, right, up);
    VectorSet(start, ent->s.origin[0], ent->s.origin[1], ent->s.origin[2] + ent->viewheight);
    VectorMA(start, 24, forward, start);

    fire_laser(ent, start, forward);

    ent->client->pers.inventory[IT_AMMO_CELLS] -= LASER_COST;

    G_CheckPowerArmor(ent);  // Check power armor status after using cells

    gi.LocClient_Print(ent, PRINT_HIGH, "Laser created {}/{}.\n", ent->client->num_lasers, MAX_LASERS);
}

void remove_lasers(edict_t* ent)
{
    edict_t* e = nullptr;
    while ((e = G_Find(e, [](edict_t* e) { return strcmp(e->classname, "player_laser") == 0; })) != nullptr)
    {
        if (e && e->owner == ent)
        {
            G_FreeEdict(e);
            ent->client->num_lasers--;
        }
    }
}
//
//void Cmd_Laser_f(edict_t* ent)
//{
//    const char* cmd = gi.argv(1);
//
//    if (Q_strcasecmp(cmd, "create") == 0)
//    {
//        create_laser(ent);
//    }
//    else if (Q_strcasecmp(cmd, "remove") == 0)
//    {
//        remove_lasers(ent);
//        gi.LocClient_Print(ent, PRINT_HIGH, "All lasers removed.\n");
//    }
//    else
//    {
//        gi.LocClient_Print(ent, PRINT_HIGH, "Usage: laser <create|remove>\n");
//    }
//}