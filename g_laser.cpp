#include "g_local.h"
#include "shared.h"
#include <array>
#include <unordered_map>

// Forward declarations
void laser_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
class PlayerLaserManager;

// Parte 2: LaserManagerHolder (reemplaza la versión anterior)
class LaserManagerHolder {
public:
    PlayerLaserManager* manager_ptr;
    LaserManagerHolder();
    ~LaserManagerHolder();
};

// Constants
namespace LaserConstants {
    constexpr int32_t MAX_LASERS = 6;
    constexpr int32_t LASER_COST = 25;
    constexpr int32_t LASER_INITIAL_DAMAGE = 1;
    constexpr int32_t LASER_ADDON_DAMAGE = 4;
    constexpr int32_t LASER_INITIAL_HEALTH = 125;
    constexpr int32_t LASER_ADDON_HEALTH = 100;
    constexpr int32_t MAX_LASER_HEALTH = 1750;
    constexpr gtime_t LASER_SPAWN_DELAY = 1_sec;
    constexpr gtime_t LASER_TIMEOUT_DELAY = 150_sec;
    constexpr gtime_t TRACE_UPDATE_INTERVAL = 50_ms;
    constexpr gtime_t BLINK_INTERVAL = 500_ms;
    constexpr gtime_t WARNING_TIME = 10_sec;
    constexpr float LASER_NONCLIENT_MOD = 1.0f;   // Aumentado para mejor daño PvE
}

// Estructuras de apoyo
struct LaserState {
    vec3_t last_trace_start;
    vec3_t last_trace_end;
    gtime_t last_trace_time = 0_ms;
    bool needs_retrace = true;
};

struct EmitterState {
    bool is_warning_phase = false;
    bool is_blink_on = false;
    gtime_t last_blink_time = 0_ms;
};

// Sistema de gestión de láseres por jugador
class PlayerLaserManager {
private:
    struct LaserEntry {
        edict_t* emitter = nullptr;
        edict_t* beam = nullptr;
        bool active = false;
    };

    std::array<LaserEntry, LaserConstants::MAX_LASERS> lasers;
    int active_count = 0;
    edict_t* owner;

public:
    explicit PlayerLaserManager(edict_t* player) : owner(player) {}

    bool can_add_laser() const {
        return active_count < LaserConstants::MAX_LASERS;
    }

    void add_laser(edict_t* emitter, edict_t* beam) {
        if (!can_add_laser()) return;

        for (auto& entry : lasers) {
            if (!entry.active) {
                entry.emitter = emitter;
                entry.beam = beam;
                entry.active = true;
                active_count++;
                break;
            }
        }
    }

    void remove_laser(edict_t* entity) {
        for (auto& entry : lasers) {
            if (entry.active && (entry.emitter == entity || entry.beam == entity)) {
                entry.active = false;
                entry.emitter = nullptr;
                entry.beam = nullptr;
                active_count--;
                break;
            }
        }
    }

    void remove_all_lasers() {
        for (auto& entry : lasers) {
            if (entry.active) {
                if (entry.emitter) {
                    laser_die(entry.emitter, nullptr, owner, 9999, vec3_origin, MOD_UNKNOWN);
                }
                entry.active = false;
                entry.emitter = nullptr;
                entry.beam = nullptr;
            }
        }
        active_count = 0;
    }

    int get_active_count() const {
        return active_count;
    }
};

LaserManagerHolder::LaserManagerHolder() : manager_ptr(nullptr) {}

LaserManagerHolder::~LaserManagerHolder() {
    delete manager_ptr;
}


namespace LaserHelpers {
    bool is_valid_target(const edict_t* ent) {
        return ent && ent->inuse &&
            ((ent->svflags & SVF_MONSTER) || ent->client || ent->takedamage);
    }

    bool is_same_team(const edict_t* ent1, const edict_t* ent2) {
        return ent1 && ent2 && ent1->team && ent2->team &&
            strcmp(ent1->team, ent2->team) == 0;
    }

    float calculate_damage_multiplier(const edict_t* target) {
        if (!target) return 0.0f;

        if (target->svflags & SVF_MONSTER) {
            if (target->monsterinfo.invincible_time > level.time) {
                return 0.0f;
            }
            return target->spawnflags.has(SPAWNFLAG_IS_BOSS) ? 1.25f : 1.0f;
        }
        return LaserConstants::LASER_NONCLIENT_MOD;
    }

    void update_visual_state(edict_t* ent, bool warning_state, bool blink_state) {
        if (warning_state && blink_state) {
            ent->s.renderfx |= RF_SHELL_GREEN;
            ent->s.effects |= EF_COLOR_SHELL;
        }
        else {
            ent->s.renderfx &= ~RF_SHELL_GREEN;
            ent->s.effects &= ~EF_COLOR_SHELL;
        }
    }

    PlayerLaserManager* get_laser_manager(edict_t* ent) {
        if (!ent || !ent->client) return nullptr;
        auto* holder = reinterpret_cast<LaserManagerHolder*>(ent->client->laser_manager);
        return holder ? static_cast<PlayerLaserManager*>(holder->manager_ptr) : nullptr;
    }

    // Función helper para obtener el ángulo entre dos vectores
    [[nodiscard]] float get_angle_between_vectors(const vec3_t& v1, const vec3_t& v2) {
        if (!is_valid_vector(v1) || !is_valid_vector(v2)) {
            return 0.0f;
        }

        vec3_t normalized_v1 = safe_normalized(v1);
        vec3_t normalized_v2 = safe_normalized(v2);

        float dot = normalized_v1.dot(normalized_v2);
        return acosf(std::clamp(dot, -1.0f, 1.0f)) * (180.0f / PIf);
    }

    // Función helper para verificar si un vector está dentro de un rango angular
    [[nodiscard]] bool is_vector_within_angle(const vec3_t& vec, const vec3_t& reference, float max_angle) {
        return get_angle_between_vectors(vec, reference) <= max_angle;
    }
}

// Funciones principales optimizadas
void laser_remove(edict_t* self) {
    if (!self) return;

    self->think = BecomeExplosion1;
    self->nextthink = level.time + FRAME_TIME_MS;

    if (self->owner && self->owner->inuse) {
        self->owner->think = G_FreeEdict;
        self->owner->nextthink = level.time + FRAME_TIME_MS;
    }

    if (self->teammaster && self->teammaster->inuse && self->teammaster->client) {
        if (auto* manager = LaserHelpers::get_laser_manager(self->teammaster)) {
            manager->remove_laser(self);
            gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser destroyed. {}/{} remaining.\n",
                manager->get_active_count(), LaserConstants::MAX_LASERS);
        }
    }
}

DIE(laser_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void {
    if (!self) return;

    OnEntityDeath(self);

    // Asegurarse de que el manager actualice el contador
    if (self->teammaster && self->teammaster->client) {
        if (auto* manager = LaserHelpers::get_laser_manager(self->teammaster)) {
            manager->remove_laser(self);
        }
    }

    if (self->classname && strcmp(self->classname, "emitter") == 0) {
        if (self->owner)
            G_FreeEdict(self->owner);
        BecomeExplosion1(self);
    }
    else {
        if (self->owner)
            BecomeExplosion1(self->owner);
        G_FreeEdict(self);
    }
}

THINK(laser_beam_think)(edict_t* self) -> void {
    if (!self || !self->owner) {
        G_FreeEdict(self);
        return;
    }

    static std::unordered_map<const edict_t*, LaserState> laser_states;
    auto& state = laser_states[self];

    // Simplify size calculation to match original behavior
    const int size = (self->health < 1) ? 0 : (self->health >= 1000) ? 4 : 2;
    self->s.frame = size;
    self->s.skinnum = (self->health > self->max_health * 0.20f) ? 0xf2f2f0f0 : 0xd0d1d2d3;

    if (state.needs_retrace || level.time >= state.last_trace_time + LaserConstants::TRACE_UPDATE_INTERVAL) {
        // Fix 1: Usar normalización segura para el vector forward
        vec3_t forward;
        AngleVectors(self->s.angles, &forward, nullptr, nullptr);
        forward = safe_normalized(forward);

        // Fix 2: Asegurar que los puntos de inicio y fin son válidos
        state.last_trace_start = self->pos1;
        if (!is_valid_vector(state.last_trace_start)) {
            state.last_trace_start = self->s.old_origin;
        }

        // Fix 3: Calcular el punto final usando distancia fija
        constexpr float LASER_LENGTH = 8192.0f;
        state.last_trace_end = state.last_trace_start + forward * LASER_LENGTH;

        // Fix 4: Validar el vector resultante
        if (!is_valid_vector(state.last_trace_end)) {
            state.last_trace_end = state.last_trace_start + vec3_t{ LASER_LENGTH, 0, 0 };
        }

        // Realizar el trace con los vectores validados
        trace_t tr = gi.traceline(state.last_trace_start, state.last_trace_end, self->owner, MASK_SHOT);

        // Fix 5: Asegurar que el punto final del trace es válido
        if (is_valid_vector(tr.endpos)) {
            self->s.origin = tr.endpos;
        }
        else {
            self->s.origin = state.last_trace_start;
        }

        // Original damage calculation with fixes
        const int damage = (size) ? std::min(self->dmg, self->health) : 0;

        if (damage && tr.ent && tr.ent->inuse && tr.ent != self->teammaster) {
            if (LaserHelpers::is_valid_target(tr.ent) && !LaserHelpers::is_same_team(self->teammaster, tr.ent)) {
                // Fix 6: Usar el vector forward normalizado para el daño
                T_Damage(tr.ent, self, self->teammaster, forward, tr.endpos, vec3_origin,
                    damage, 0, DAMAGE_ENERGY, MOD_PLAYER_LASER);

                // Aplicar desgaste solo si el objetivo tiene salud positiva
                if (tr.ent->health > 0) {
                    float damageMult = LaserHelpers::calculate_damage_multiplier(tr.ent);
                    self->health -= damage * damageMult;
                }
            }
        }

        state.last_trace_time = level.time;
        state.needs_retrace = false;
    }

    // Fix 7: Mantener consistente el punto de origen del láser
    self->s.old_origin = self->pos1;

    // Health check and cleanup
    if (self->health <= 0) {
        if (self->teammaster && self->teammaster->inuse && self->teammaster->client) {
            if (auto* manager = LaserHelpers::get_laser_manager(self->teammaster)) {
                manager->remove_laser(self);
                gi.LocClient_Print(self->teammaster, PRINT_HIGH,
                    "Laser emitter burned out and exploded. {}/{} remaining.\n",
                    manager->get_active_count(), LaserConstants::MAX_LASERS);
            }
        }
        laser_die(self, self, self->teammaster, self->dmg, self->s.origin, MOD_PLAYER_LASER);
        laser_states.erase(self);
        return;
    }

    self->nextthink = level.time + FRAME_TIME_MS;
}

THINK(emitter_think)(edict_t* self) -> void {
    if (!self) return;

    static std::unordered_map<const edict_t*, EmitterState> emitter_states;
    auto& state = emitter_states[self];

    if (level.time >= self->timestamp) {
        gi.LocClient_Print(self->teammaster, PRINT_HIGH, "Laser timed out and was removed.\n");
        laser_die(self, nullptr, self->teammaster, 9999, self->s.origin, MOD_UNKNOWN);
        emitter_states.erase(self);
        return;
    }

    bool should_warn = level.time >= self->timestamp - LaserConstants::WARNING_TIME;
    if (should_warn != state.is_warning_phase) {
        state.is_warning_phase = should_warn;
        state.last_blink_time = 0_ms;
    }

    if (state.is_warning_phase && level.time >= state.last_blink_time + LaserConstants::BLINK_INTERVAL) {
        state.is_blink_on = !state.is_blink_on;
        state.last_blink_time = level.time;
    }

    LaserHelpers::update_visual_state(self, state.is_warning_phase, state.is_blink_on);
    self->nextthink = level.time + FRAME_TIME_MS;
}

void create_laser(edict_t* ent) {
    if (!ent || !ent->client) return;

    // Validaciones iniciales (modo horde, movetype, etc...)
    if (!g_horde->integer) {
        gi.Client_Print(ent, PRINT_HIGH, "Need to be on Horde Mode to spawn a laser\n");
        return;
    }

    if (ent->movetype != MOVETYPE_WALK) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Need to be Non-Spect to create laser.\n");
        return;
    }

    // Inicialización del manager
    if (!ent->client->laser_manager) {
        auto* holder = new LaserManagerHolder();
        holder->manager_ptr = new PlayerLaserManager(ent);
        ent->client->laser_manager = reinterpret_cast<void*>(holder);
    }

    auto* manager = LaserHelpers::get_laser_manager(ent);
    if (!manager || !manager->can_add_laser()) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Can't build any more lasers.\n");
        return;
    }

    if (ent->client->pers.inventory[IT_AMMO_CELLS] < LaserConstants::LASER_COST) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Not enough cells to create a laser.\n");
        return;
    }

    // Fix 8: Calcular vectores de dirección de forma segura
    vec3_t forward, right;
    auto [fwd, rgt, _] = AngleVectors(ent->client->v_angle);
    forward = safe_normalized(fwd);
    right = safe_normalized(rgt);

    // Fix 9: Ajustar el offset y calcular posiciones de forma segura
    vec3_t offset{ 0.0f, 8.0f, static_cast<float>(ent->viewheight) - 8.0f };
    vec3_t start = G_ProjectSource(ent->s.origin, offset, forward, right);
    vec3_t end = start + forward * 64;

    if (!is_valid_vector(start) || !is_valid_vector(end)) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Invalid position for laser placement.\n");
        return;
    }

    trace_t tr = gi.traceline(start, end, ent, MASK_SOLID);

    if (tr.fraction == 1.0f) {
        gi.LocClient_Print(ent, PRINT_HIGH, "Too far from wall.\n");
        return;
    }

    edict_t* laser = G_Spawn();
    edict_t* grenade = G_Spawn();

    // Configurar láser
    laser->dmg = LaserConstants::LASER_INITIAL_DAMAGE +
        (LaserConstants::LASER_ADDON_DAMAGE * (current_wave_level));
    laser->health = std::min(LaserConstants::LASER_INITIAL_HEALTH +
        (LaserConstants::LASER_ADDON_HEALTH * (current_wave_level - 1)),
        LaserConstants::MAX_LASER_HEALTH);
    laser->max_health = laser->health;
    laser->gib_health = -100;
    laser->mass = 50;
    laser->movetype = MOVETYPE_NONE;
    laser->solid = SOLID_NOT;
    laser->s.renderfx = RF_BEAM | RF_TRANSLUCENT;
    laser->s.modelindex = 1;
    laser->s.sound = gi.soundindex("world/laser.wav");
    laser->classname = "laser";
    laser->teammaster = ent;
    laser->owner = grenade;
    laser->s.skinnum = 0xf2f2f0f0;
    laser->think = laser_beam_think;
    laser->nextthink = level.time + LaserConstants::LASER_SPAWN_DELAY;
    laser->s.origin = ent->s.origin;
    laser->s.old_origin = tr.endpos;
    laser->pos1 = tr.endpos;
    laser->s.angles = vectoangles(tr.plane.normal);
    laser->takedamage = false;
    laser->die = laser_die;
    laser->flags |= FL_NO_KNOCKBACK;

    // Establecer equipo basado en CTF
    if (ent->client->resp.ctf_team == CTF_TEAM1) {
        laser->team = TEAM1;
    }
    else if (ent->client->resp.ctf_team == CTF_TEAM2) {
        laser->team = TEAM2;
    }
    else {
        laser->team = "neutral";
    }

    // Configurar granada (emisor)
    grenade->s.origin = tr.endpos;
    grenade->s.angles = vectoangles(tr.plane.normal);
    grenade->movetype = MOVETYPE_NONE;
    grenade->clipmask = MASK_SHOT;
    grenade->solid = SOLID_BBOX;
    grenade->mins = vec3_t{ -3, -3, 0 };
    grenade->maxs = vec3_t{ 3, 3, 6 };
    grenade->takedamage = true;
    grenade->health = 100;
    grenade->gib_health = -50;
    grenade->mass = 25;
    grenade->s.modelindex = gi.modelindex("models/objects/grenade2/tris.md2");
    grenade->teammaster = ent;
    grenade->owner = laser;
    grenade->classname = "emitter";
    grenade->nextthink = level.time + FRAME_TIME_MS;
    grenade->think = emitter_think;
    grenade->die = laser_die;
    grenade->svflags = SVF_BOT;
    //    grenade->pain = laser_pain;
    grenade->timestamp = level.time + LaserConstants::LASER_TIMEOUT_DELAY;
    grenade->flags |= FL_NO_KNOCKBACK;
    grenade->team = laser->team;

    // Enlazar entidades
    gi.linkentity(laser);
    gi.linkentity(grenade);

    // Actualizar inventario y contador del jugador
    ent->client->pers.inventory[IT_AMMO_CELLS] -= LaserConstants::LASER_COST;

    // Obtener el manager y registrar el nuevo láser
    if (auto* current_manager = LaserHelpers::get_laser_manager(ent)) {
        current_manager->add_laser(grenade, laser);
        gi.LocClient_Print(ent, PRINT_HIGH, "Laser built. You have {}/{} lasers.\n",
            current_manager->get_active_count(), LaserConstants::MAX_LASERS);
    }
}
    void remove_lasers(edict_t * ent) noexcept {
        if (!ent) return;

        // Usar el manager para remover todos los láseres
        if (auto* manager = LaserHelpers::get_laser_manager(ent)) {
            manager->remove_all_lasers();
        }
    }