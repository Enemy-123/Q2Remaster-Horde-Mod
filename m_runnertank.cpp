// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

runnertank

==============================================================================
*/

#include "g_local.h"

#include "m_runnertank.h"

#include "m_flash.h"
#include "shared.h"

void runnertank_refire_rocket(edict_t* self);
void runnetank_doattack_rocket(edict_t* self);
void runnertank_reattack_blaster(edict_t* self);
bool runnertank_check_wall(edict_t* self, float dist);

static cached_soundindex sound_thud;
static cached_soundindex sound_pain, sound_pain2;
static cached_soundindex sound_idle;
static cached_soundindex sound_die;
static cached_soundindex sound_step;
static cached_soundindex sound_sight;
static cached_soundindex sound_windup;
static cached_soundindex sound_strike;

constexpr spawnflags_t SPAWNFLAG_runnertank_COMMANDER_GUARDIAN = 8_spawnflag;
constexpr spawnflags_t SPAWNFLAG_runnertank_COMMANDER_HEAT_SEEKING = 16_spawnflag;




//
// misc
//

MONSTERINFO_SIGHT(runnertank_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

void runnertank_footstep(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_step, 1, ATTN_NORM, 0);
}

void runnertank_thud(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_thud, 1, ATTN_NORM, 0);
}

void runnertank_windup(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_windup, 1, ATTN_NORM, 0);
}

MONSTERINFO_IDLE(runnertank_idle) (edict_t* self) -> void
{
	gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

//
// stand
//

mframe_t runnertank_frames_stand[] = {
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
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }
};
MMOVE_T(runnertank_move_stand) = { FRAME_stand01, FRAME_stand30, runnertank_frames_stand, nullptr };

MONSTERINFO_STAND(runnertank_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &runnertank_move_stand);
}

//
// walk
//

void runnertank_walk(edict_t* self);

#if 0
mframe_t runnertank_frames_start_walk[] = {
	{ ai_walk },
	{ ai_walk, 6 },
	{ ai_walk, 6 },
	{ ai_walk, 11, runnertank_footstep }
};
MMOVE_T(runnertank_move_start_walk) = { FRAME_walk01, FRAME_walk04, runnertank_frames_start_walk, runnertank_walk };
#endif

// Animación de caminata corregida
mframe_t runnertank_frames_walk[] = {
	{ ai_walk, 4 }, { ai_walk, 5 }, { ai_walk, 3 }, { ai_walk, 2 },
	{ ai_walk, 5 }, { ai_walk, 5 }, { ai_walk, 4 }, { ai_walk, 4, runnertank_footstep },
	{ ai_walk, 3 }, { ai_walk, 5 }, { ai_walk, 4 }, { ai_walk, 5 },
	{ ai_walk, 7 }, { ai_walk, 7 }, { ai_walk, 6 }, { ai_walk, 6, runnertank_footstep },
	{ ai_walk, 4 }, { ai_walk, 5 }, { ai_walk, 3 }, { ai_walk, 2 },
	{ ai_walk, 5 }, { ai_walk, 5 }, { ai_walk, 4 }, { ai_walk, 4, runnertank_footstep },
	{ ai_walk, 3 }, { ai_walk, 5 }, { ai_walk, 4 }, { ai_walk, 5 },
	{ ai_walk, 7 }, { ai_walk, 7 }, { ai_walk, 6 }, { ai_walk, 6, runnertank_footstep },
	{ ai_walk, 4 }, { ai_walk, 5 }, { ai_walk, 3 }, { ai_walk, 2 },
	{ ai_walk, 4 }, { ai_walk, 5 }
};
MMOVE_T(runnertank_move_walk) = { FRAME_walk01, FRAME_walk38, runnertank_frames_walk, runnertank_walk };

#if 0
mframe_t runnertank_frames_stop_walk[] = {
	{ ai_walk, 3 },
	{ ai_walk, 3 },
	{ ai_walk, 2 },
	{ ai_walk, 2 },
	{ ai_walk, 4, runnertank_footstep }
};
MMOVE_T(runnertank_move_stop_walk) = { FRAME_walk21, FRAME_walk25, runnertank_frames_stop_walk, runnertank_stand };
#endif
void runnertank_run(edict_t* self);

// Nuevas animaciones para inicio y fin de caminata/carrera
mframe_t runnertank_frames_start_walk[] = {
	{ ai_walk, 0 }, { ai_walk, 3 }, { ai_walk, 3 }, { ai_walk, 3 }
};
MMOVE_T(runnertank_move_start_walk) = { FRAME_walk01, FRAME_walk04, runnertank_frames_start_walk, runnertank_walk };

mframe_t runnertank_frames_stop_walk[] = {
	{ ai_walk, 3 }, { ai_walk, 3 }, { ai_walk, 2 }, { ai_walk, 2 },
	{ ai_walk, 0, runnertank_footstep }
};
MMOVE_T(runnertank_move_stop_walk) = { FRAME_walk34, FRAME_walk38, runnertank_frames_stop_walk, runnertank_stand };

void runnertank_walk_to_run(edict_t* self);
void runnertank_stop_run_to_attack(edict_t* self);

mframe_t runnertank_frames_start_run[] = {
	{ ai_run, 8 }, { ai_run, 8 }, { ai_run, 8 }, { ai_run, 8, runnertank_footstep }
};
MMOVE_T(runnertank_move_start_run) = { FRAME_walk01, FRAME_walk04, runnertank_frames_start_run, runnertank_walk_to_run };


// Ajustar la función de caminata para una transición más suave
MONSTERINFO_WALK(runnertank_walk) (edict_t* self) -> void
{
	if (self->monsterinfo.active_move != &runnertank_move_walk)
	{
		M_SetAnimation(self, &runnertank_move_start_walk);
	}
	else
	{
		M_SetAnimation(self, &runnertank_move_walk);
	}
}


void runnertank_unstuck(edict_t* self)
{
	static int stuck_count = 0;
	if (VectorLength(self->velocity) < 10)
	{
		stuck_count++;
		if (stuck_count > 5)
		{
			// Intenta moverse en una dirección aleatoria
			self->ideal_yaw = frandom() * 360;
			M_ChangeYaw(self);
			stuck_count = 0;
		}
	}
	else
	{
		stuck_count = 0;
	}
}

//
// run
//



//mframe_t runnertank_frames_start_run[] = {
//	{ ai_run },
//	{ ai_run, 6 },
//	{ ai_run, 6 },
//	{ ai_run, 11, runnertank_footstep }
//};
//MMOVE_T(runnertank_move_start_run) = { FRAME_walk01, FRAME_walk04, runnertank_frames_start_run, runnertank_run };
//
// Actualizar la animación de carrera
mframe_t runnertank_frames_run[] = {
	{ ai_run, 18, runnertank_footstep },
	{ ai_run, 18, nullptr },
	{ ai_run, 18, nullptr },
	{ ai_run, 18, [](edict_t* self) { runnertank_check_wall(self, 32); } },
	{ ai_run, 18, nullptr },
	{ ai_run, 18, nullptr },
	{ ai_run, 18, runnertank_footstep },
	{ ai_run, 18, nullptr },
	{ ai_run, 18, nullptr },
	{ ai_run, 18, [](edict_t* self) { runnertank_check_wall(self, 32); } }
};
MMOVE_T(runnertank_move_run) = { FRAME_run01, FRAME_run10, runnertank_frames_run, nullptr };


mframe_t runnertank_frames_stop_run[] = {
	{ ai_run, 3 },
	{ ai_run, 3 },
	{ ai_run, 2 },
	{ ai_run, 2 },
	{ ai_run, 4, runnertank_footstep }
};
MMOVE_T(runnertank_move_stop_run) = { FRAME_walk21, FRAME_walk25, runnertank_frames_stop_run, runnertank_stop_run_to_attack };

// Función para manejar la transición de caminata a carrera
void runnertank_walk_to_run(edict_t* self)
{
	if (self->enemy && range_to(self, self->enemy) > RANGE_NEAR)
	{
		M_SetAnimation(self, &runnertank_move_run);
		self->monsterinfo.aiflags |= AI_CHARGING;
	}
	else
	{
		M_SetAnimation(self, &runnertank_move_walk);
	}
}



bool runnertank_enemy_visible(edict_t* self)
{
	return self->enemy && visible(self, self->enemy);
}
MONSTERINFO_RUN(runnertank_run) (edict_t* self) -> void
{
	if (self->enemy && self->enemy->client)
		self->monsterinfo.aiflags |= AI_BRUTAL;
	else
		self->monsterinfo.aiflags &= ~AI_BRUTAL;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		M_SetAnimation(self, &runnertank_move_stand);
		return;
	}

	const float range = self->enemy ? range_to(self, self->enemy) : 0;

	// Decidir si debe detenerse y atacar
	if (range <= RANGE_NEAR && self->monsterinfo.aiflags & AI_CHARGING)
	{
		// Probabilidad de detenerse aumenta a medida que se acerca al enemigo
		float stop_chance = 1.0f - (range / RANGE_NEAR);
		if (frandom() < stop_chance)
		{
			M_SetAnimation(self, &runnertank_move_stop_run);
			self->monsterinfo.aiflags &= ~AI_CHARGING;
			return;
		}
	}

	// Implementar carrera y strafing
	if (self->monsterinfo.aiflags & AI_CHARGING)
	{
		M_SetAnimation(self, &runnertank_move_run);
		// Aumentar la velocidad durante la carga
		VectorScale(self->velocity, 1.5f, self->velocity);
	}
	else
	{
		M_SetAnimation(self, &runnertank_move_run);
	}

	// Implementar carrera y strafing
	if (self->monsterinfo.aiflags & AI_CHARGING)
	{
		M_SetAnimation(self, &runnertank_move_run);
		// Aumentar la velocidad durante la carga
		VectorScale(self->velocity, 1.5f, self->velocity);
	}
	else
	{
		M_SetAnimation(self, &runnertank_move_run);
	}

	// Implementar strafing mejorado
	if (self->enemy && visible(self, self->enemy))
	{
		if (range <= RANGE_MID)
		{
			float strafe_chance = 0.5f;  // 50% de probabilidad base de hacer strafe
			// Aumentar la probabilidad de strafing si el enemigo está disparando
			if (self->enemy->client && (self->enemy->client->buttons & BUTTON_ATTACK))
				strafe_chance += 0.2f;
			// Aumentar la probabilidad de strafing si el runnertank tiene poca salud
			if (self->health < self->max_health * 0.5f)
				strafe_chance += 0.2f;
			if (frandom() < strafe_chance)
			{
				// Decidir aleatoriamente si strafear a la izquierda o derecha
				if (frandom() < 0.5)
					self->monsterinfo.lefty = 0;  // Strafe derecha
				else
					self->monsterinfo.lefty = 1;  // Strafe izquierda
				// Aplicar el movimiento de strafe
				vec3_t right, strafe_vel;
				AngleVectors(self->s.angles, nullptr, right, nullptr);
				// Aumentar significativamente la velocidad de strafe
				const float strafe_speed = 300 + (frandom() * 200);  // Velocidad de strafe variable y aumentada
				if (self->monsterinfo.lefty)
					VectorScale(right, -strafe_speed, strafe_vel);  // Strafe izquierda
				else
					VectorScale(right, strafe_speed, strafe_vel);   // Strafe derecha
				// Combinar el movimiento de avance con el strafe
				VectorAdd(self->velocity, strafe_vel, self->velocity);
				// Ajustar la duración del strafe
				self->monsterinfo.pausetime = level.time + random_time(0.75_sec, 2_sec);
			}
		}
	}

	// Volver a la dirección original después del strafe
	if (level.time > self->monsterinfo.pausetime)
	{
		self->monsterinfo.lefty = 0;
		VectorSet(self->velocity, 0, 0, self->velocity[2]);  // Mantener solo la velocidad vertical
	}
}

//
// pain
//

mframe_t runnertank_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(runnertank_move_pain1) = { FRAME_pain201, FRAME_pain204, runnertank_frames_pain1, runnertank_run };

mframe_t runnertank_frames_pain3[] = {
	{ ai_move, -7 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move, 3 },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, runnertank_footstep }
};
MMOVE_T(runnertank_move_pain3) = { FRAME_pain301, FRAME_pain316, runnertank_frames_pain3, runnertank_run };

PAIN(runnertank_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (mod.id != MOD_CHAINFIST && damage <= 10)
		return;

	if (level.time < self->pain_debounce_time)
		return;

	if (mod.id != MOD_CHAINFIST)
	{
		if (damage <= 30)
			if (frandom() > 0.2f)
				return;

		// don't go into pain while attacking
		if ((self->s.frame >= FRAME_attak301) && (self->s.frame <= FRAME_attak330))
			return;
		if ((self->s.frame >= FRAME_attak101) && (self->s.frame <= FRAME_attak116))
			return;
	}

	self->pain_debounce_time = level.time + 3_sec;

	if (self->count)
		gi.sound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	// PMM - blindfire cleanup
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
	// pmm

	if (damage <= 50)
		M_SetAnimation(self, &runnertank_move_pain1);
	else
		M_SetAnimation(self, &runnertank_move_pain3);
}

MONSTERINFO_SETSKIN(runnertank_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

// [Paril-KEX]
bool M_AdjustBlindfireTarget(edict_t* self, const vec3_t& start, const vec3_t& target, const vec3_t& right, vec3_t& out_dir);

//
// attacks
//
void runnertankRail(edict_t* self)
{
	vec3_t forward, right;
	vec3_t start;
	vec3_t dir;
	monster_muzzleflash_id_t flash_number;

	if (!self->enemy || !self->enemy->inuse || !infront(self, self->enemy) || !visible(self, self->enemy))
		return;

	bool blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;

	// Mantenemos los mismos flash numbers
	if (self->s.frame == FRAME_attak110)
		flash_number = MZ2_TANK_BLASTER_1;
	else if (self->s.frame == FRAME_attak113)
		flash_number = MZ2_TANK_BLASTER_2;
	else // (self->s.frame == FRAME_attak116)
		flash_number = MZ2_TANK_BLASTER_3;

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	vec3_t target;
	if (blindfire)
	{
		target = self->monsterinfo.blind_fire_target;
		if (!M_AdjustBlindfireTarget(self, start, target, right, dir))
			return;
	}
	else
	{
		// Usamos PredictAim para seguir la trayectoria del enemigo
		PredictAim(self, self->enemy, start, 0, false, 0.2f, &dir, nullptr);
	}

	// Cambiamos monster_fire_blaster por monster_fire_railgun
	monster_fire_railgun(self, start, dir, 45, 100, flash_number);
}
void runnertankStrike(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_strike, 1, ATTN_NORM, 0);

	{
		gi.sound(self, CHAN_WEAPON, sound_strike, 1, ATTN_NORM, 0);


		// Efecto visual similar al berserker
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BERSERK_SLAM);

		vec3_t f, r, start;
		AngleVectors(self->s.angles, f, r, nullptr);
		start = M_ProjectFlashSource(self, { 20.f, -14.3f, -21.f }, f, r);
		trace_t tr = gi.traceline(self->s.origin, start, self, MASK_SOLID);

		gi.WritePosition(tr.endpos);
		gi.WriteDir({ 0.f, 0.f, 1.f });
		gi.multicast(tr.endpos, MULTICAST_PHS, false);
		void T_SlamRadiusDamage(vec3_t point, edict_t * inflictor, edict_t * attacker, float damage, float kick, edict_t * ignore, float radius, mod_t mod);
		// Daño radial
		T_SlamRadiusDamage(tr.endpos, self, self, 75, 450.f, self, 165, MOD_UNKNOWN);

	}
}

mframe_t tank_frames_punch_attack[] =
{
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, nullptr},
	{ai_charge, 0, runnertankStrike},  // FRAME_attak225 - Añadir footstep aquí
	{ai_charge, 0, nullptr},  // FRAME_attak226 - Engendrar monstruo aquí
	{ai_charge, -1, nullptr},
	{ai_charge, -2, nullptr}   // FRAME_attak229
};
MMOVE_T(tank_move_punch_attack) = { FRAME_attak222, FRAME_attak229, tank_frames_punch_attack, runnertank_run };


void runnertankRocket(edict_t* self)
{
	vec3_t					 forward, right;
	vec3_t					 start;
	vec3_t					 dir;
	vec3_t					 vec;
	monster_muzzleflash_id_t flash_number;
	int						 rocketSpeed; // PGM
	// pmm - blindfire support
	vec3_t target;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	bool   blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;

	if (self->s.frame == FRAME_attak324)
		flash_number = MZ2_TANK_ROCKET_1;
	else if (self->s.frame == FRAME_attak327)
		flash_number = MZ2_TANK_ROCKET_2;
	else // (self->s.frame == FRAME_attak330)
		flash_number = MZ2_TANK_ROCKET_3;

	AngleVectors(self->s.angles, forward, right, nullptr);

	// [Paril-KEX] scale
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	if (self->speed)
		rocketSpeed = self->speed;
	else if (self->spawnflags.has(SPAWNFLAG_runnertank_COMMANDER_HEAT_SEEKING))
		rocketSpeed = 500;
	else
		rocketSpeed = 650;

	// PMM
	if (blindfire)
		target = self->monsterinfo.blind_fire_target;
	else
		target = self->enemy->s.origin;
	// pmm

	// PGM
	//  PMM - blindfire shooting
	if (blindfire)
	{
		vec = target;
		dir = vec - start;
	}
	// pmm
	// don't shoot at feet if they're above me.
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
	// PGM

	//======
	// PMM - lead target  (not when blindfiring)
	// 20, 35, 50, 65 chance of leading
	if ((!blindfire) && ((frandom() < (0.2f + ((3 - skill->integer) * 0.15f)))))
		PredictAim(self, self->enemy, start, rocketSpeed, false, 0, &dir, &vec);
	// PMM - lead target
	//======

	dir.normalize();

	// pmm blindfire doesn't check target (done in checkattack)
	// paranoia, make sure we're not shooting a target right next to us
	if (blindfire)
	{
		// blindfire has different fail criteria for the trace
		if (M_AdjustBlindfireTarget(self, start, vec, right, dir))
		{
			if (self->spawnflags.has(SPAWNFLAG_runnertank_COMMANDER_HEAT_SEEKING))
				monster_fire_heat(self, start, dir, 50, rocketSpeed, flash_number, self->accel);
			else
				monster_fire_rocket(self, start, dir, 50, rocketSpeed, flash_number);
		}
	}
	else
	{
		trace_t trace = gi.traceline(start, vec, self, MASK_PROJECTILE);

		if (trace.fraction > 0.5f || trace.ent->solid != SOLID_BSP)
		{
			if (self->spawnflags.has(SPAWNFLAG_runnertank_COMMANDER_HEAT_SEEKING))
				monster_fire_heat(self, start, dir, 50, rocketSpeed, flash_number, self->accel);
			else
				monster_fire_rocket(self, start, dir, 50, rocketSpeed, flash_number);
		}
	}
}
void runnertankPlasmaGun(edict_t* self)
{
	bool blindfire = false;
	vec3_t start;
	vec3_t dir{};
	vec3_t forward, right, up;
	monster_muzzleflash_id_t flash_number;

	if (!self->enemy || !self->enemy->inuse || self->enemy && !visible(self, self->enemy))
		return;

	flash_number = static_cast<monster_muzzleflash_id_t>(MZ2_TANK_MACHINEGUN_1 + (self->s.frame - FRAME_attak406));
	AngleVectors(self->s.angles, forward, right, up);
	start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

	// Calcular la dirección base hacia el enemigo
	vec3_t target = self->enemy->s.origin;
	target[2] += self->enemy->viewheight;
	VectorSubtract(target, start, dir);
	VectorNormalize(dir);

	// Calcular el ángulo de dispersión basado en la animación del tanque
	float fanAngle;
	if (self->s.frame <= FRAME_attak415)
		fanAngle = -20 + (self->s.frame - FRAME_attak406) * 4; // Abanico de -20 a 20 grados
	else
		fanAngle = 20 - (self->s.frame - FRAME_attak416) * 4; // Abanico de 20 a -20 grados

	// Rotar el vector de dirección para crear el efecto de abanico
	float sinAngle = sin(DEG2RAD(fanAngle));
	float cosAngle = cos(DEG2RAD(fanAngle));
	float newX = forward[0] * cosAngle - right[0] * sinAngle;
	float newY = forward[1] * cosAngle - right[1] * sinAngle;
	float newZ = forward[2] * cosAngle - right[2] * sinAngle;
	VectorSet(dir, newX, newY, newZ);
	VectorNormalize(dir);

	// Añadir una pequeña dispersión aleatoria
	float spread = 0.02f;
	dir[0] += crandom() * spread;
	dir[1] += crandom() * spread;
	dir[2] += crandom() * spread;
	VectorNormalize(dir);

	if (blindfire)
	{
		target = self->monsterinfo.blind_fire_target;
		if (!M_AdjustBlindfireTarget(self, start, target, right, dir))
			return;
	}
	else
	{
		// Usamos PredictAim para seguir la trayectoria del enemigo
		PredictAim(self, self->enemy, start, 700, false, 0.2f, &dir, nullptr);
	}

	// Disparar el plasma con la velocidad correcta
	fire_plasma(self, start, dir, 35, 700, 40, 40);

	// Guardar la posición del objetivo para el próximo disparo
	VectorCopy(self->enemy->s.origin, self->pos1);
	self->pos1[2] += self->enemy->viewheight;
}
static void runnertank_blind_check(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		vec3_t aim = self->monsterinfo.blind_fire_target - self->s.origin;
		self->ideal_yaw = vectoyaw(aim);
	}
}

mframe_t runnertank_frames_attack_blast[] = {
    { ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
    { ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
    { ai_charge }, { ai_charge, 0, runnertankRail },
    { ai_charge }, { ai_charge }, { ai_charge, 0, runnertankRail },
    { ai_charge }, { ai_charge }, { ai_charge, 0, runnertankRail },
    { ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
    { ai_charge }, { ai_charge }
};
MMOVE_T(runnertank_move_attack_blast) = { FRAME_attak101, FRAME_attak122, runnertank_frames_attack_blast, runnertank_reattack_blaster };

mframe_t runnertank_frames_reattack_blast[] = {
	{ ai_charge },
	{ ai_charge, 0, runnertankRail },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, runnertankRail },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, runnertankRail } // 16
};
MMOVE_T(runnertank_move_reattack_blast) = { FRAME_attak111, FRAME_attak122, runnertank_frames_reattack_blast, runnertank_reattack_blaster };

mframe_t runnertank_frames_attack_post_blast[] = {
	{ ai_move }, // 17
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 3 },
	{ ai_move, 2 },
	{ ai_move, -2, runnertank_footstep } // 22
};
MMOVE_T(runnertank_move_attack_post_blast) = { FRAME_attak117, FRAME_attak122, runnertank_frames_attack_post_blast, runnertank_run };

void runnertank_reattack_blaster(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		M_SetAnimation(self, &runnertank_move_attack_post_blast);
		return;
	}

	if (visible(self, self->enemy))
		if (self->enemy->health > 0)
			if (frandom() <= 0.6f)
			{
				M_SetAnimation(self, &runnertank_move_reattack_blast);
				return;
			}
	M_SetAnimation(self, &runnertank_move_attack_post_blast);
}
void runnertank_doattack_rocket(edict_t* self);

void runnertank_poststrike(edict_t* self)
{
	self->enemy = nullptr;
	// [Paril-KEX]
	self->monsterinfo.pausetime = HOLD_FOREVER;
	self->monsterinfo.stand(self);
}

mframe_t runnertank_frames_attack_strike[] = {
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge, 0, runnertankStrike }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }
};
MMOVE_T(runnertank_move_attack_strike) = { FRAME_attak201, FRAME_attak238, runnertank_frames_attack_strike, runnertank_poststrike };

mframe_t runnertank_frames_attack_pre_rocket[] = {
	{ ai_charge },	{ ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge, 0, runnertankRocket }, { ai_charge }, { ai_charge, 0, runnertankRocket }, { ai_charge },
	{ ai_charge, 0, runnertankRocket }, { ai_charge }, { ai_charge }, { ai_charge, 0, runnertankRocket },
	{ ai_charge }, { ai_charge }, { ai_charge, 0, runnertankRocket }, { ai_charge, 0, runnertankRocket },
	{ ai_charge }
};
MMOVE_T(runnertank_move_attack_pre_rocket) = { FRAME_attak303, FRAME_attak321, runnertank_frames_attack_pre_rocket, runnertank_doattack_rocket };

mframe_t runnertank_frames_attack_fire_rocket[] = {
	{ ai_charge }, { ai_charge }, { ai_charge, 0, runnertankRocket },
	{ ai_charge }, { ai_charge }, { ai_charge, 0, runnertankRocket },
	{ ai_charge }, { ai_charge }, { ai_charge },
	{ ai_charge }, { ai_charge }, { ai_charge }, { ai_charge }, { ai_charge }
};
MMOVE_T(runnertank_move_attack_fire_rocket) = { FRAME_attak322, FRAME_attak335, runnertank_frames_attack_fire_rocket, runnertank_refire_rocket };

mframe_t runnertank_frames_attack_post_rocket[] = {

	{ ai_charge, -9 },
	{ ai_charge, -8 },
	{ ai_charge, -7 },
	{ ai_charge, -1 },
	{ ai_charge, -1, runnertank_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, // 50
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(runnertank_move_attack_post_rocket) = { FRAME_attak326, FRAME_attak335, runnertank_frames_attack_post_rocket, runnertank_run };


void runnertank_refire_rocket(edict_t* self)
{
	// PMM - blindfire cleanup
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		M_SetAnimation(self, &runnertank_move_attack_post_rocket);
		return;
	}
	// pmm

	if (self->enemy->health > 0)
		if (visible(self, self->enemy))
			if (frandom() <= 0.4f)
			{
				M_SetAnimation(self, &runnertank_move_attack_fire_rocket);
				return;
			}
	M_SetAnimation(self, &runnertank_move_attack_post_rocket);
}

void runnertank_doattack_rocket(edict_t* self)
{
	M_SetAnimation(self, &runnertank_move_attack_fire_rocket);
}


mframe_t runnertank_frames_attack_chain[] = {
	{ ai_charge },
	{ ai_charge }, 
	{ nullptr, 0, runnertankPlasmaGun },
	{ nullptr, 0, runnertankPlasmaGun },
	{ nullptr, 0, runnertankPlasmaGun },
	{ nullptr, 0, runnertankPlasmaGun },
	{ nullptr, 0, runnertankPlasmaGun },
	{ nullptr, 0, runnertankPlasmaGun },
	{ nullptr, 0, runnertankPlasmaGun },
	{ nullptr, 0, runnertankPlasmaGun },
	{ nullptr, 0, runnertankPlasmaGun },
	{ ai_charge }
};
MMOVE_T(runnertank_move_attack_chain) = { FRAME_attak404, FRAME_attak415, runnertank_frames_attack_chain, runnertank_run };

void runnertank_stop_run_to_attack(edict_t* self)
{
	if (self->enemy && range_to(self, self->enemy) <= RANGE_NEAR)
	{
		M_SetAnimation(self, &runnertank_move_attack_pre_rocket);
		self->monsterinfo.attack_finished = level.time + 2_sec;
	}
	else
	{
		M_SetAnimation(self, &runnertank_move_run);
	}
}

MONSTERINFO_ATTACK(runnertank_attack) (edict_t* self) -> void
{
	if (!self->enemy || !self->enemy->inuse)
		return;

	const float range = range_to(self, self->enemy);

	// Ajustar la frecuencia de ataque
	if (level.time < self->monsterinfo.attack_finished)
		return;

	if (range <= MELEE_DISTANCE * 2)
	{
		// Ataque melee (punch)
		M_SetAnimation(self, &tank_move_punch_attack);
		self->monsterinfo.attack_finished = level.time + 0.2_sec;
		return;
	}
	else if (range <= RANGE_NEAR && range >= MELEE_DISTANCE * 2.00 && infront(self, self->enemy))
	{
		// En rango melee, decide aleatoriamente entre cohetes y cadena
		if (brandom())
		{
			M_SetAnimation(self, &runnertank_move_attack_pre_rocket);
			self->pain_debounce_time = level.time + 3_sec;
			self->monsterinfo.attack_finished = level.time + 4_sec;
		}
		else
		{
			M_SetAnimation(self, &runnertank_move_attack_chain);
			self->monsterinfo.attack_finished = level.time + 3_sec;
		}
	}
	else if (range <= RANGE_NEAR)
	{
		// En rango cercano, decide entre rail gun, cadena o cohetes
		const float random_choice = frandom();
		const bool can_rail = M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]);
		if (can_rail && random_choice < 0.4)
		{
			M_SetAnimation(self, &runnertank_move_run);
			self->monsterinfo.attack_finished = level.time + 4_sec;
		}
		else if (random_choice < 0.7)
		{
			M_SetAnimation(self, &runnertank_move_attack_chain);
			self->monsterinfo.attack_finished = level.time + 3_sec;
		}
		else
		{
			M_SetAnimation(self, &runnertank_move_attack_pre_rocket);
			self->pain_debounce_time = level.time + 3_sec;
			self->monsterinfo.attack_finished = level.time + 4_sec;
		}
	}
	else if (range <= RANGE_MID)
	{
		// En rango medio, prefiere rail gun o cohetes
		if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]) && frandom() < 0.6)
		{
			M_SetAnimation(self, &runnertank_move_attack_blast);
			self->monsterinfo.attack_finished = level.time + 2_sec;
		}
		else
		{
			M_SetAnimation(self, &runnertank_move_attack_pre_rocket);
			self->pain_debounce_time = level.time + 3_sec;
			self->monsterinfo.attack_finished = level.time + 4_sec;
		}
	}
	else
	{
		// En rango lejano, prefiere rail gun o cohetes
		if (M_CheckClearShot(self, monster_flash_offset[MZ2_TANK_BLASTER_1]) && frandom() < 0.6)
		{
			M_SetAnimation(self, &runnertank_move_attack_blast);
			self->monsterinfo.attack_finished = level.time + 2_sec;
		}
	}

	// Añadir una pausa entre decisiones de ataque
	self->monsterinfo.pausetime = level.time + 1.5_sec;
}
//
// death
//

void runnertank_dead(edict_t* self)
{
	self->mins = { -16, -16, -16 };
	self->maxs = { 16, 16, -0 };
	monster_dead(self);
}

static void runnertank_shrink(edict_t* self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t runnertank_frames_death1[] = {
	{ ai_move, -7 },
	{ ai_move, -2 },
	{ ai_move, -2 },
	{ ai_move, 1 },
	{ ai_move, 3 },
	{ ai_move, 6 },
	{ ai_move, 1 },
	{ ai_move, 1 },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -2 },
	{ ai_move },
	{ ai_move },
	{ ai_move, -3 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -4 },
	{ ai_move, -6 },
	{ ai_move, -4 },
	{ ai_move, -5 },
	{ ai_move, -7, runnertank_shrink },
	{ ai_move, -15, runnertank_thud },
	{ ai_move, -5 },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(runnertank_move_death) = { FRAME_death01, FRAME_death32, runnertank_frames_death1, runnertank_dead };

DIE(runnertank_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		ThrowGibs(self, damage, {
			{ "models/objects/gibs/sm_meat/tris.md2" },
			{ 3, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
			{ "models/objects/gibs/gear/tris.md2", GIB_METALLIC },
			{ 2, "models/monsters/tank/gibs/foot.md2", GIB_SKINNED | GIB_METALLIC },
			{ 2, "models/monsters/tank/gibs/thigh.md2", GIB_SKINNED | GIB_METALLIC },
			{ "models/monsters/tank/gibs/chest.md2", GIB_SKINNED },
			{ "models/monsters/tank/gibs/head.md2", GIB_HEAD | GIB_SKINNED }
			});

		if (!self->style)
			ThrowGib(self, "models/monsters/tank/gibs/barm.md2", damage, GIB_SKINNED | GIB_UPRIGHT, self->s.scale);

		self->deadflag = true;
		return;
	}

	if (self->deadflag)
		return;

	// [Paril-KEX] dropped arm
	if (!self->style)
	{
		self->style = 1;

		auto [fwd, rgt, up] = AngleVectors(self->s.angles);

		edict_t* arm_gib = ThrowGib(self, "models/monsters/tank/gibs/barm.md2", damage, GIB_SKINNED | GIB_UPRIGHT, self->s.scale);

		if (!arm_gib) {
			return;
		}

		arm_gib->s.origin = self->s.origin + (rgt * -16.f) + (up * 23.f);
		arm_gib->s.old_origin = arm_gib->s.origin;
		arm_gib->avelocity = { crandom() * 15.f, crandom() * 15.f, 180.f };
		arm_gib->velocity = (up * 100.f) + (rgt * -120.f);
		arm_gib->s.angles = self->s.angles;
		arm_gib->s.angles[2] = -90.f;
		arm_gib->s.skinnum /= 2;
		gi.linkentity(arm_gib);
	}

	// regular death
	gi.sound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	self->deadflag = true;
	self->takedamage = true;

	M_SetAnimation(self, &runnertank_move_death);
}
void runnertank_jump_now(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 130);
	self->velocity += (up * 300);
}

void runnertank_jump2_now(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 250);
	self->velocity += (up * 400);
}

void runnertank_jump_wait_land(edict_t* self)
{
	if (self->groundentity == nullptr)
	{
		self->monsterinfo.nextframe = self->s.frame;

		if (monster_jump_finished(self))
			self->monsterinfo.nextframe = self->s.frame + 1;
	}
	else
		self->monsterinfo.nextframe = self->s.frame + 1;
}

mframe_t runnertank_frames_jump[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, runnertank_jump_now },
	{ ai_move },
	{ ai_move, 0, runnertank_jump_wait_land },
	{ ai_move }
};
MMOVE_T(runnertank_move_jump) = { FRAME_run01, FRAME_run07, runnertank_frames_jump, runnertank_run };

mframe_t runnertank_frames_jump2[] = {
	{ ai_move, -6 },
	{ ai_move, -4 },
	{ ai_move, -5 },
	{ ai_move, 0, runnertank_jump2_now },
	{ ai_move },
	{ ai_move, 0, runnertank_jump_wait_land },
	{ ai_move }
};
MMOVE_T(runnertank_move_jump2) = { FRAME_run01, FRAME_run07, runnertank_frames_jump2, runnertank_run };

void runnertank_jump(edict_t* self, blocked_jump_result_t result)
{
	if (!self->enemy)
		return;

	monster_done_dodge(self);

	if (result == blocked_jump_result_t::JUMP_JUMP_UP)
		M_SetAnimation(self, &runnertank_move_jump2);
	else
		M_SetAnimation(self, &runnertank_move_jump);
}
//===========
// PGM
bool runnertank_check_wall(edict_t* self, float dist)
{
	vec3_t forward, right, up;
	vec3_t check_point{}, wall_normal;
	trace_t tr;

	AngleVectors(self->s.angles, forward, right, up);
	VectorMA(self->s.origin, dist + 10, forward, check_point);

	tr = gi.trace(self->s.origin, self->mins, self->maxs, check_point, self, MASK_MONSTERSOLID);

	if (tr.fraction < 1.0) {
		VectorCopy(tr.plane.normal, wall_normal);

		// Calcular el producto punto manualmente
		float dot = forward[0] * wall_normal[0] + forward[1] * wall_normal[1] + forward[2] * wall_normal[2];

		float turn_factor = fabs(dot);
		float max_turn = 90.0f; // Aumentado de 45.0f a 90.0f
		float turn_angle = max_turn * turn_factor;

		if (dot < 0) {
			self->ideal_yaw = anglemod(self->s.angles[YAW] + turn_angle);
		}
		else {
			self->ideal_yaw = anglemod(self->s.angles[YAW] - turn_angle);
		}

		// Usar M_ChangeYaw en lugar de ai_turn para un giro más suave
		M_ChangeYaw(self);

		// Reducir la velocidad del monstruo de manera más gradual
		VectorScale(self->velocity, 0.8f, self->velocity);

		return true;
	}

	return false;
}

MONSTERINFO_BLOCKED(runnertank_blocked) (edict_t* self, float dist) -> bool
{
    if (self->monsterinfo.can_jump)
    {
        if (const auto result = blocked_checkjump(self, dist); result != blocked_jump_result_t::NO_JUMP)
        {
            runnertank_jump(self, result);
            return true;
        }
    }

    if (blocked_checkplat(self, dist))
        return true;

    // Intenta cambiar de dirección si está bloqueado por una pared
    if (runnertank_check_wall(self, dist))
    {
        // Añadir un giro adicional usando ai_turn
        ai_turn(self, 0);
        return true;
    }

    return false;
}
// PGM
// 
//===========

//
// monster_runnertank
//

MONSTERINFO_SIDESTEP(runnertank_sidestep) (edict_t* self) -> bool
{
	// No hacer sidestep si estamos en medio de un ataque
	if ((self->monsterinfo.active_move == &runnertank_move_attack_blast) ||
		(self->monsterinfo.active_move == &runnertank_move_attack_pre_rocket) ||
		(self->monsterinfo.active_move == &runnertank_move_attack_fire_rocket))
	{
		return false;
	}

	// Si no estamos corriendo, cambiar a la animación de carrera
	if (self->monsterinfo.active_move != &runnertank_move_run)
		M_SetAnimation(self, &runnertank_move_run);

	// Iniciar un nuevo movimiento de strafe
	self->monsterinfo.lefty = (frandom() < 0.5f) ? 0 : 1;

	// Aplicar un impulso lateral más fuerte
	vec3_t right, strafe_vel;
	AngleVectors(self->s.angles, nullptr, right, nullptr);
	float strafe_speed = 300 + (frandom() * 200);  // Consistente con runnertank_run

	if (self->monsterinfo.lefty)
		VectorScale(right, -strafe_speed, strafe_vel);
	else
		VectorScale(right, strafe_speed, strafe_vel);

	VectorAdd(self->velocity, strafe_vel, self->velocity);

	self->monsterinfo.pausetime = level.time + random_time(0.75_sec, 2_sec);

	return true;
}

/*QUAKED monster_runnertank (1 .5 0) (-32 -32 -16) (32 32 72) Ambush Trigger_Spawn Sight
model="models/monsters/runnertank/tris.md2"
*/
/*QUAKED monster_runnertank_commander (1 .5 0) (-32 -32 -16) (32 32 72) Ambush Trigger_Spawn Sight Guardian HeatSeeking
 */
void SP_monster_runnertank(edict_t* self)
{
	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	self->s.modelindex = gi.modelindex("models/vault/monsters/tank/tris.md2");
	self->mins = { -32, -32, -16 };
	self->maxs = { 32, 32, 64 };
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	gi.modelindex("models/monsters/tank/gibs/barm.md2");
	gi.modelindex("models/monsters/tank/gibs/head.md2");
	gi.modelindex("models/monsters/tank/gibs/chest.md2");
	gi.modelindex("models/monsters/tank/gibs/foot.md2");
	gi.modelindex("models/monsters/tank/gibs/thigh.md2");

	sound_thud.assign("tank/tnkdeth2.wav");
	sound_idle.assign("tank/tnkidle1.wav");
	sound_die.assign("tank/death.wav");
	sound_step.assign("tank/step.wav");
	sound_windup.assign("tank/tnkatck4.wav");
	sound_strike.assign("tank/tnkatck5.wav");
	sound_sight.assign("tank/sight1.wav");

	gi.soundindex("tank/tnkatck1.wav");
	gi.soundindex("tank/tnkatk2a.wav");
	gi.soundindex("tank/tnkatk2b.wav");
	gi.soundindex("tank/tnkatk2c.wav");
	gi.soundindex("tank/tnkatk2d.wav");
	gi.soundindex("tank/tnkatk2e.wav");
	gi.soundindex("tank/tnkatck3.wav");

	if (strcmp(self->classname, "monster_runnertank_commander") == 0)
	{
		self->health = 1000 * st.health_multiplier;
		self->gib_health = -225;
		self->count = 1;
		sound_pain2.assign("tank/pain.wav");
	}
	else
	{
		self->health = 750 * st.health_multiplier;
		self->gib_health = -200;
		sound_pain.assign("tank/tnkpain2.wav");
	}

	self->monsterinfo.scale = MODEL_SCALE;

	// [Paril-KEX] N64 runnertank commander is a chonky boy
	if (self->spawnflags.has(SPAWNFLAG_runnertank_COMMANDER_GUARDIAN))
	{
		if (!self->s.scale)
			self->s.scale = 1.5f;
		self->health = 1500 * st.health_multiplier;
	}

	// heat seekingness
	if (!self->accel)
		self->accel = 0.075f;

	self->mass = 500;

	self->pain = runnertank_pain;
	self->die = runnertank_die;
	self->monsterinfo.stand = runnertank_stand;
	self->monsterinfo.walk = runnertank_walk;
	self->monsterinfo.run = runnertank_run;
	self->monsterinfo.sidestep = runnertank_sidestep;
	self->monsterinfo.dodge = nullptr;
	self->monsterinfo.attack = runnertank_attack;
	self->monsterinfo.melee = nullptr;
	self->monsterinfo.sight = runnertank_sight;
	self->monsterinfo.idle = runnertank_idle;
	self->monsterinfo.blocked = runnertank_blocked; // PGM
	self->monsterinfo.setskin = runnertank_setskin;
	self->yaw_speed *= 2;
	gi.linkentity(self);

	M_SetAnimation(self, &runnertank_move_stand);

	walkmonster_start(self);

	// PMM
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	self->monsterinfo.blindfire = true;
	// pmm
	if (strcmp(self->classname, "monster_runnertank_commander") == 0)
		self->s.skinnum = 2;

	self->monsterinfo.can_jump = true;
	self->monsterinfo.drop_height = 256;
	self->monsterinfo.jump_height = 68;
	ApplyMonsterBonusFlags(self);
}

void Use_Boss3(edict_t* ent, edict_t* other, edict_t* activator);

THINK(Think_runnertankStand) (edict_t* ent) -> void
{
	if (ent->s.frame == FRAME_stand30)
		ent->s.frame = FRAME_stand01;
	else
		ent->s.frame++;
	ent->nextthink = level.time + 10_hz;
}

/*QUAKED monster_runnertank_stand (1 .5 0) (-32 -32 0) (32 32 90)

Just stands and cycles in one place until targeted, then teleports away.
N64 edition!
*/
void SP_monster_runnertank_stand(edict_t* self)
{
	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->model = "models/vault/monsters/tank/tris.md2";
	self->s.modelindex = gi.modelindex(self->model);
	self->s.frame = FRAME_stand01;
	self->s.skinnum = 2;

	gi.soundindex("misc/bigtele.wav");

	self->mins = { -32, -32, -16 };
	self->maxs = { 32, 32, 64 };

	if (!self->s.scale)
		self->s.scale = 1.5f;

	self->mins *= self->s.scale;
	self->maxs *= self->s.scale;

	self->use = Use_Boss3;
	self->think = Think_runnertankStand;
	self->nextthink = level.time + 10_hz;
	gi.linkentity(self);
}