#include "g_local.h"
#include "shared.h"

constexpr int32_t MAX_LASERS = 6;
constexpr int32_t LASER_COST = 25;
constexpr int32_t LASER_INITIAL_DAMAGE = 50;
constexpr int32_t LASER_ADDON_DAMAGE = 50;
constexpr int32_t LASER_INITIAL_HEALTH = 275;  // DMG before explode
constexpr int32_t LASER_ADDON_HEALTH = 75;     // DMG addon before explode
constexpr gtime_t LASER_SPAWN_DELAY = 1_sec;
constexpr gtime_t LASER_TIMEOUT_DELAY = 150_sec;
constexpr float LASER_NONCLIENT_MOD = 0.25f;    // Reducido para menor desgaste contra objetos
static bool g_laser_timeout_in_progress = false;


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
    // Only decrement the counter if it's not a timeout
    if (!g_laser_timeout_in_progress && self->teammaster && self->teammaster->client)
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
    bool hit_valid_target = false;

    if (!self->owner)
    {
        G_FreeEdict(self);
        return;
    }

    int size = (self->health < 1) ? 0 : (self->health >= 1000) ? 4 : 2;
    self->s.frame = size;

    // Cambiar color basado en la salud del láser
    if (self->health > self->max_health * 0.20f)
    {
        self->s.skinnum = 0xf2f2f0f0; // rojo
    }
    else
    {
        self->s.skinnum = 0xd0d1d2d3; // amarillo
    }

    AngleVectors(self->s.angles, forward, nullptr, nullptr);
    vec3_t start = self->pos1;
    vec3_t end = start + forward * 8192;
    tr = gi.traceline(start, end, self->owner, MASK_SHOT);
    self->s.origin = tr.endpos;
    self->s.old_origin = self->pos1;

    int damage = (size) ? std::min(self->dmg, self->health) : 0;

    if (damage && tr.ent && tr.ent->inuse && tr.ent != self->teammaster)
    {
        // Verificar si el objetivo es válido (monstruo, jugador, o entidad dañable)
        if ((tr.ent->svflags & SVF_MONSTER) || tr.ent->client || tr.ent->takedamage)
        {
            // Verificar si el objetivo está en el mismo equipo
            if (!OnSameTeam(self->teammaster, tr.ent))
            {
                // Aplicar daño incluso si la salud es <= 0, pero no contar como hit_valid_target
                T_Damage(tr.ent, self, self->teammaster, forward, tr.endpos, vec3_origin, damage, 0, DAMAGE_ENERGY, MOD_PLAYER_LASER);

                // Solo contar como hit_valid_target si la salud es > 0
                if (tr.ent->health > 0)
                {
                    hit_valid_target = true;

                    // Reducir la salud del láser solo si golpeó un objetivo válido con salud > 0
                    if (tr.ent->svflags & SVF_MONSTER && (!(tr.ent->spawnflags.has(SPAWNFLAG_IS_BOSS))))
                    {
                        self->health -= damage * 0.4f;  // desgaste aligerado contra monsters
                    }
                    else if (tr.ent->svflags & SVF_MONSTER && tr.ent->spawnflags.has(SPAWNFLAG_IS_BOSS))
                    {
                        self->health -= damage * 0.6f; // ligeramente mayor desgaste contra boss
                    }
                    else
                    {
                        self->health -= damage * 0.25f;  // Desgaste aún menor contra otros objetivos válidos
                    }
                }
            }
        }
    }

    // Si no golpeó un objetivo válido, no reducir la salud
    if (!hit_valid_target)
    {
        // Opcionalmente, puedes agregar un desgaste mínimo aquí si lo deseas
        // self->health -= 0.1f;  // Desgaste mínimo cuando no golpea nada
    }

    // Si la salud llega a cero, explotar
    if (self->health <= 0)
    {
        gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser emitter burned out and exploded.\n");
        laser_die(self, self, self->teammaster, self->dmg, self->s.origin, MOD_PLAYER_LASER);
        return;
    }

    self->nextthink = level.time + FRAME_TIME_MS;
}

void remove_laser(edict_t* self, edict_t* attacker, bool is_timeout);

THINK(emitter_think)(edict_t* self) -> void
{
    // Check if the laser has timed out
    if (level.time >= self->timestamp)
    {
        gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser timed out and was removed.\n");
        remove_laser(self, self->teammaster, true);
        return;
    }

    // flash green when we are about to expire (last 10 seconds)
    if (level.time >= self->timestamp - 10_sec)
    {
        if (level.time.milliseconds() / 500 % 2 == 0)  // Blink every 0.5 seconds
        {
            self->s.renderfx |= RF_SHELL_GREEN;
            self->s.effects |= EF_COLOR_SHELL;
        }
        else
        {
            self->s.renderfx &= ~RF_SHELL_GREEN;
            self->s.effects &= ~EF_COLOR_SHELL;
        }
    }
    else
    {
        self->s.renderfx &= ~RF_SHELL_GREEN;
        self->s.effects &= ~EF_COLOR_SHELL;
    }

    self->nextthink = level.time + FRAME_TIME_MS;
}

void create_laser(edict_t* ent)
{
    vec3_t forward, right, start, end, offset;
    trace_t tr;
    edict_t* grenade;
    edict_t* laser;

    if (ent->movetype != MOVETYPE_WALK) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Need to be Non-Spect to create laser.\n");
        return;
    }

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

    tr = gi.traceline(start, end, ent, MASK_SOLID);

    if (tr.fraction == 1.0f)
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Too far from wall.\n");
        return;
    }

    laser = G_Spawn();
    grenade = G_Spawn();

    // create the laser beam
    laser->dmg = LASER_INITIAL_DAMAGE + LASER_ADDON_DAMAGE;
    laser->health = LASER_INITIAL_HEALTH + (LASER_ADDON_HEALTH * current_wave_number);
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
    laser->flags |= FL_NO_KNOCKBACK;

    // Asignar el equipo al láser
    if (ent->client->resp.ctf_team == CTF_TEAM1) {
        laser->team = TEAM1;
    }
    else if (ent->client->resp.ctf_team == CTF_TEAM2) {
        laser->team = TEAM2;
    }
    else {
        laser->team = "neutral"; // O cualquier valor por defecto que prefieras
    }

    if (laser->health >= 1500)
        laser->health = 1500;

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
    grenade->pain = laser_pain;
    grenade->timestamp = level.time + LASER_TIMEOUT_DELAY;
    laser->flags |= FL_NO_KNOCKBACK;

    grenade->team = laser->team;

    gi.linkentity(grenade);

    ent->client->num_lasers++;
    ent->client->pers.inventory[IT_AMMO_CELLS] -= LASER_COST;

    gi.LocClient_Print(ent, PRINT_HIGH, "Laser built. You have {}/{} lasers.\n", ent->client->num_lasers, MAX_LASERS);
}

void remove_laser(edict_t* self, edict_t* attacker, bool is_timeout)
{
    if (is_timeout)
    {
        g_laser_timeout_in_progress = true;
    }

    if (self->teammaster && self->teammaster->client)
    {
        self->teammaster->client->num_lasers--;
        gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser removed. {}/{} remaining.\n",
            self->teammaster->client->num_lasers, MAX_LASERS);
    }

    laser_die(self, nullptr, attacker, 9999, self->s.origin, MOD_UNKNOWN);

    if (is_timeout)
    {
        g_laser_timeout_in_progress = false;
    }
}

