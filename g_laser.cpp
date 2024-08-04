#include "g_local.h"

constexpr int32_t MAX_LASERS = 6;
constexpr int32_t LASER_COST = 25;
constexpr int32_t LASER_INITIAL_DAMAGE = 150;
constexpr int32_t LASER_ADDON_DAMAGE = 20;
constexpr int32_t LASER_INITIAL_HEALTH = 500;
constexpr int32_t LASER_ADDON_HEALTH = 250;
constexpr gtime_t LASER_SPAWN_DELAY = 1_sec;
constexpr gtime_t LASER_TIMEOUT_DELAY = 120_sec;
constexpr float LASER_NONCLIENT_MOD = 0.5f;

void laser_remove(edict_t* self)
{
    // remove emitter/grenade
    self->think = BecomeExplosion1;
    self->nextthink = level.time + FRAME_TIME_MS;

    // remove laser beam
    if (self->owner && self->owner->inuse)
    {
        self->owner->think = G_FreeEdict;
        self->owner->nextthink = level.time + FRAME_TIME_MS;
    }

    // decrement laser counter
    if (self->teammaster && self->teammaster->inuse && self->teammaster->client)
    {
        self->teammaster->client->num_lasers--;
        gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser destroyed. {}/{} remaining.\n",
            self->teammaster->client->num_lasers, MAX_LASERS);
    }
}



DIE(laser_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
    // Decrement laser counter for the owner
    if (self->teammaster && self->teammaster->client)
    {
        self->teammaster->client->num_lasers--;
        gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser destroyed. {}/{} remaining.\n",
            self->teammaster->client->num_lasers, MAX_LASERS);
    }

    // Remove both the emitter and the beam
    if (self->classname && strcmp(self->classname, "emitter") == 0)
    {
        if (self->owner)
            G_FreeEdict(self->owner);  // Free the laser beam
        BecomeExplosion1(self);  // Explode the emitter
    }
    else
    {
        if (self->owner)
            BecomeExplosion1(self->owner);  // Explode the emitter
        G_FreeEdict(self);  // Free the laser beam
    }
}

PAIN(laser_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
    // Implementación básica de dolor
    if (self->health < self->max_health / 2)
    {
        // Cambiar el color a amarillo cuando está dañado
        self->s.skinnum = 0xd0d1d2d3;  // amarillo
    }

    // Llamar a laser_die si la salud llega a cero o menos
    if (self->health <= 0)
    {
        laser_die(self, other, other, damage, self->s.origin, mod);
    }
}

THINK(laser_beam_think)(edict_t* self) -> void
{
    vec3_t forward;
    trace_t tr;
    bool nonclient = false;

    // can't have a laser beam without an emitter!
    if (!self->owner)
    {
        G_FreeEdict(self);
        return;
    }

    // set beam diameter
    int size = (self->health < 1) ? 0 : (self->health >= 1000) ? 4 : 2;
    self->s.frame = size;

    // set beam color
    self->s.skinnum = 0xf2f2f0f0; // red color

    // trace from beam emitter out as far as we can go
    AngleVectors(self->s.angles, forward, nullptr, nullptr);
    vec3_t start = self->pos1;
    vec3_t end = start + forward * 8192;
    tr = gi.traceline(start, end, self->owner, MASK_SHOT);
    self->s.origin = tr.endpos;
    self->s.old_origin = self->pos1;

    // set maximum damage output
    int damage = (size) ? std::min(self->dmg, self->health) : 0;

    // what is in laser's path?
    if (damage && tr.ent && tr.ent->inuse && tr.ent != self->teammaster)
    {
        if (tr.ent->client)
        {
            // remove lasers near spawn positions
            if (tr.ent->client->respawn_time - 1.5_sec > level.time)
            {
                gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser touched respawning player, so it was removed. ({}/{} remain)\n",
                    self->teammaster->client->num_lasers, MAX_LASERS);
                laser_remove(self->owner);
                return;
            }
        }
        else
            nonclient = true; // target is a non-client

        // deal damage to anything in the beam's path
        T_Damage(tr.ent, self, self->teammaster, forward, tr.endpos, vec3_origin, damage, 0, DAMAGE_ENERGY, MOD_PLAYER_LASER);
    }
    else
        damage = 0; // emitter is either burned out or hit nothing valid

    // emitter burns out slowly even when idle
    if (size && !damage && !(self->s.frame % 10))
    {
        damage = std::max(1, static_cast<int>(0.008333f * self->max_health));
    }

    // reduce maximum damage counter
    if (nonclient)
        self->health -= static_cast<int>(LASER_NONCLIENT_MOD * damage);
    else
        self->health -= damage;

    // if the counter reaches 0, then shut-down
    if (damage && self->health < 1)
    {
        self->health = 0;
        gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser emitter burned out.\n");
    }

    self->nextthink = level.time + FRAME_TIME_MS;
}


THINK(emitter_think)(edict_t* self) -> void
{
    // flash green when we are about to expire
    if (self->owner->health < (0.1f * self->owner->max_health))
    {
        if (self->s.frame & 8)
        {
            self->s.renderfx |= RF_SHELL_LITE_GREEN;
            self->s.effects |= EF_COLOR_SHELL;
        }
        else
        {
            self->s.renderfx &= ~RF_SHELL_LITE_GREEN;
            self->s.effects &= ~EF_COLOR_SHELL;
        }
    }
    else
        self->s.renderfx &= ~RF_SHELL_LITE_GREEN;
    self->s.effects &= ~EF_COLOR_SHELL;

    self->nextthink = level.time + FRAME_TIME_MS;
}


void create_laser(edict_t* ent)
{
    vec3_t forward, right, start, end, offset;
    trace_t tr;
    edict_t* grenade;
    edict_t* laser;

    if (ent->client->num_lasers >= MAX_LASERS)
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Can't build any more lasers.\n");
        return;
    }

    if (ent->client->pers.inventory[IT_AMMO_CELLS] < LASER_COST)
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Not enough cells to create a laser.\n");
        return;
    }

    // get starting position and forward vector
    AngleVectors(ent->client->v_angle, forward, right, nullptr);
    VectorSet(offset, 0, 8, ent->viewheight - 8);
    start = G_ProjectSource(ent->s.origin, offset, forward, right);
    // get end position
    end = start + forward * 64;

    tr = gi.traceline(start, end, ent, MASK_SHOT);

    if (tr.fraction == 1.0f)
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Too far from wall.\n");
        return;
    }

    laser = G_Spawn();
    grenade = G_Spawn();

    // create the laser beam
    laser->dmg = LASER_INITIAL_DAMAGE + LASER_ADDON_DAMAGE;
    laser->health = LASER_INITIAL_HEALTH + LASER_ADDON_HEALTH;
    laser->max_health = laser->health;
    laser->gib_health = -100;
    laser->mass = 50;
    laser->movetype = MOVETYPE_NONE;
    laser->solid = SOLID_NOT;
    laser->s.renderfx = RF_BEAM | RF_TRANSLUCENT;
    laser->s.modelindex = 1; // must be non-zero
    laser->s.sound = gi.soundindex("world/laser.wav");
    laser->classname = "laser";
    laser->teammaster = ent; // link to player
    laser->owner = grenade; // link to grenade
    laser->s.skinnum = 0xf2f2f0f0; // red beam color
    laser->think = laser_beam_think;
    laser->nextthink = level.time + LASER_SPAWN_DELAY;
    laser->s.origin = ent->s.origin;
    laser->s.old_origin = tr.endpos;
    laser->pos1 = tr.endpos; // beam origin
    laser->s.angles = vectoangles(tr.plane.normal);
    laser->takedamage = false;
    laser->die = laser_die;
    laser->pain = laser_pain;
    laser->monsterinfo.team = CTF_TEAM1;
    laser->flags |= FL_NO_KNOCKBACK;
    gi.linkentity(laser);

    // create the laser emitter (grenade)
    grenade->s.origin = tr.endpos;
    grenade->s.angles = vectoangles(tr.plane.normal);
    grenade->movetype = MOVETYPE_NONE;
    grenade->clipmask = MASK_SHOT;
    grenade->solid = SOLID_BBOX;
    VectorSet(grenade->mins, -3, -3, 0);
    VectorSet(grenade->maxs, 3, 3, 6);
    grenade->takedamage = true;
    grenade->health = 100;
    grenade->gib_health = -50;
    grenade->mass = 25;
    grenade->s.modelindex = gi.modelindex("models/objects/grenade2/tris.md2");
    grenade->teammaster = ent; // link to player
    grenade->owner = laser; // link to laser
    grenade->classname = "emitter";
    grenade->nextthink = level.time + FRAME_TIME_MS;
    grenade->think = emitter_think;
    grenade->die = laser_die;
    grenade->svflags = SVF_BOT;
    grenade->monsterinfo.team = CTF_TEAM1;
    grenade->pain = laser_pain;
    laser->flags |= FL_NO_KNOCKBACK;

    gi.linkentity(grenade);

    ent->client->num_lasers++;
    ent->client->pers.inventory[IT_AMMO_CELLS] -= LASER_COST;

    gi.LocClient_Print(ent, PRINT_HIGH, "Laser built. You have {}/{} lasers.\n", ent->client->num_lasers, MAX_LASERS);
}

void remove_lasers(edict_t* ent)
{
    edict_t* e = nullptr;
    while ((e = G_Find(e, [](edict_t* e) { return strcmp(e->classname, "emitter") == 0; })) != nullptr)
    {
        if (e && (e->teammaster == ent))
        {
            laser_die(e, nullptr, ent, 9999, vec3_origin, MOD_UNKNOWN);
        }
    }

    // reset laser counter
    ent->client->num_lasers = 0;
}

