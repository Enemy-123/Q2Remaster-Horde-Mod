// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

TURRET

==============================================================================
*/

#include "../g_local.h"
#include "m_rogue_turret.h"
#include "../shared.h"
constexpr spawnflags_t SPAWNFLAG_TURRET2_BLASTER = 0x0008_spawnflag;
constexpr spawnflags_t SPAWNFLAG_TURRET2_MACHINEGUN = 0x0010_spawnflag;
constexpr spawnflags_t SPAWNFLAG_TURRET2_ROCKET = 0x0020_spawnflag;
constexpr spawnflags_t SPAWNFLAG_TURRET2_HEATBEAM = 0x0040_spawnflag;
constexpr spawnflags_t SPAWNFLAG_TURRET2_WEAPONCHOICE = SPAWNFLAG_TURRET2_HEATBEAM | SPAWNFLAG_TURRET2_ROCKET | SPAWNFLAG_TURRET2_MACHINEGUN | SPAWNFLAG_TURRET2_BLASTER;
constexpr spawnflags_t SPAWNFLAG_TURRET2_WALL_UNIT = 0x0080_spawnflag;
constexpr spawnflags_t SPAWNFLAG_TURRET2_NO_LASERSIGHT = 18_spawnflag_bit;

void turret2Aim(edict_t* self);
void turret2_ready_gun(edict_t* self);
void turret2_run(edict_t* self);
void TurretSparks(edict_t* self);

extern const mmove_t turret2_move_fire;
extern const mmove_t turret2_move_fire_blind;

static cached_soundindex sound_moved, sound_moving, sound_pew;

// Actualizar la posición del efecto
void UpdateSmokePosition(edict_t* self) {
	if (!self || !self->inuse || !self->target_hint_chain || !self->target_hint_chain->inuse)
		return;

	// Actualizar frame y skin si es necesario
	self->target_hint_chain->s.frame = self->s.frame;
	self->target_hint_chain->s.skinnum = self->s.skinnum;

	// Calcular la nueva posición para el emisor de humo
	vec3_t forward;
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	vec3_t smoke_pos = self->s.origin + (forward * 20.0f);

	// Actualizar posición del emisor
	self->target_hint_chain->s.origin = smoke_pos;
	self->target_hint_chain->s.angles = self->s.angles;

	// Emitir el efecto de humo
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_CHAINFIST_SMOKE);
	gi.WritePosition(smoke_pos);
	gi.multicast(smoke_pos, MULTICAST_PVS, false);

	gi.linkentity(self->target_hint_chain);
}


void turret2Aim(edict_t* self)
{
	// Validaciones iniciales
	if (!self || !self->inuse)
		return;

	// Actualizar la posición del efecto visual
	UpdateSmokePosition(self);

	TurretSparks(self);

	// Verifica el estado del enemigo
	bool enemy_valid = (self->enemy && self->enemy != world &&
		self->enemy->inuse && !OnSameTeam(self, self->enemy));

	if (!enemy_valid) {
		if (!FindMTarget(self))
			return;

		// Revalidar después de buscar nuevo objetivo
		enemy_valid = (self->enemy && self->enemy != world &&
			self->enemy->inuse && !OnSameTeam(self, self->enemy));
		if (!enemy_valid)
			return;
	}

	// Si la torreta está en modo inactivo, prepara el arma sin apuntar
	if (self->s.frame < FRAME_active01) {
		turret2_ready_gun(self);
		return;
	}

	// Si la torreta aún se está preparando, no apuntar
	if (self->s.frame < FRAME_run01)
		return;

	// Calcular punto de destino
	vec3_t end;
	if (self->monsterinfo.active_move == &turret2_move_fire_blind) {
		end = self->monsterinfo.blind_fire_target;
		end[2] += (self->enemy->s.origin[2] < self->monsterinfo.blind_fire_target[2]) ?
			self->enemy->viewheight + 10 :
			self->enemy->mins[2] - 10;
	}
	else {
		end = self->enemy->s.origin;
		end[2] += self->enemy->client ?
			self->enemy->viewheight :
			(self->enemy->mins[2] + self->enemy->maxs[2]) * 0.5f;
	}

	// Calcular dirección y ángulos
	vec3_t dir = end - self->s.origin;
	if (!is_valid_vector(dir)) {
		return;
	}
	dir = safe_normalized(dir);
	vec3_t ang = vectoangles(dir);

	// Ajustar pitch y yaw ideales
	float idealPitch = ang[PITCH];
	float idealYaw = ang[YAW];

	// Procesamiento según orientación
	int orientation = static_cast<int>(self->offset[1]);
	switch (orientation) {
	case -1: // up, pitch: 0 to 90
		if (idealPitch < -90) idealPitch += 360;
		idealPitch = std::clamp(idealPitch, -90.0f, -5.0f);
		break;

	case -2: // down, pitch: -180 to -360
		if (idealPitch > -90) idealPitch -= 360;
		idealPitch = std::clamp(idealPitch, -355.0f, -185.0f);
		break;

	case 0: // +X
		if (idealPitch < -180) idealPitch += 360;
		idealPitch = std::clamp(idealPitch, -85.0f, 85.0f);
		if (idealYaw > 180) idealYaw -= 360;
		idealYaw = std::clamp(idealYaw, -85.0f, 85.0f);
		break;

	case 90: // +Y
		if (idealPitch < -180) idealPitch += 360;
		idealPitch = std::clamp(idealPitch, -85.0f, 85.0f);
		if (idealYaw > 270) idealYaw -= 360;
		idealYaw = std::clamp(idealYaw, 5.0f, 175.0f);
		break;

	case 180: // -X
		if (idealPitch < -180) idealPitch += 360;
		idealPitch = std::clamp(idealPitch, -85.0f, 85.0f);
		idealYaw = std::clamp(idealYaw, 95.0f, 265.0f);
		break;

	case 270: // -Y
		if (idealPitch < -180) idealPitch += 360;
		idealPitch = std::clamp(idealPitch, -85.0f, 85.0f);
		if (idealYaw < 90) idealYaw += 360;
		idealYaw = std::clamp(idealYaw, 185.0f, 355.0f);
		break;
	}

	// Ajustar velocidad base según powerups
	float base_speed = self->yaw_speed / (gi.tick_rate / 10);
	if (self->monsterinfo.quadfire_time > level.time) {
		base_speed *= 1.5f;
	}

	// Ajustar pitch
	float current_pitch = self->s.angles[PITCH];
	float pitch_move = idealPitch - current_pitch;

	// Normalizar movimiento de pitch
	while (std::abs(pitch_move) >= 360) {
		pitch_move += (pitch_move < 0) ? 360 : -360;
	}
	if (std::abs(pitch_move) > 90) {
		pitch_move += (pitch_move < 0) ? 360 : -360;
	}

	pitch_move = std::clamp(pitch_move, -base_speed, base_speed);
	self->s.angles[PITCH] = anglemod(current_pitch + pitch_move);

	// Ajustar yaw
	float current_yaw = self->s.angles[YAW];
	float yaw_move = idealYaw - current_yaw;

	// Normalizar movimiento de yaw
	if (std::abs(yaw_move) >= 180) {
		yaw_move += (yaw_move < 0) ? 360 : -360;
	}

	yaw_move = std::clamp(yaw_move, -base_speed, base_speed);
	self->s.angles[YAW] = anglemod(current_yaw + yaw_move);

	// Manejar laser sight
	if (!self->spawnflags.has(SPAWNFLAG_TURRET2_NO_LASERSIGHT) &&
		!self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
	{
		// Inicializar laser sight si no existe
		if (!self->target_ent) {
			self->target_ent = G_Spawn();
			self->target_ent->s.modelindex = MODELINDEX_WORLD;
			self->target_ent->s.renderfx = RF_BEAM;
			self->target_ent->s.frame = 1;
			self->target_ent->s.skinnum = 0xd0d1d2d3;
			self->target_ent->classname = "turret2_lasersight";
			self->target_ent->s.effects = EF_BOB;
			self->target_ent->s.origin = self->s.origin;
			self->target_ent->owner = self;
		}

		// Calcular punto final del láser
		vec3_t forward;
		AngleVectors(self->s.angles, forward, nullptr, nullptr);

		float scan_range = visible(self, self->enemy) ? 12.f : 64.f;

		vec3_t laser_end = self->s.origin + (forward * 8192);
		trace_t tr = gi.traceline(self->s.origin, laser_end, self, MASK_SOLID);

		// Aplicar efecto de ondulación
		vec3_t wave{
			sinf(level.time.seconds() + self->s.number) * scan_range,
			cosf((level.time.seconds() - self->s.number) * 3.f) * scan_range,
			sinf((level.time.seconds() - self->s.number) * 2.5f) * scan_range
		};

		tr.endpos += wave;

		// Actualizar dirección del láser
		forward = tr.endpos - self->s.origin;
		if (is_valid_vector(forward)) {
			forward.normalize();
			laser_end = self->s.origin + (forward * 8192);
			tr = gi.traceline(self->s.origin, laser_end, self, MASK_SOLID);
			self->target_ent->s.old_origin = tr.endpos;
			gi.linkentity(self->target_ent);
		}
	}
}

MONSTERINFO_SIGHT(turret2_sight) (edict_t* self, edict_t* other) -> void
{
}

MONSTERINFO_SEARCH(turret2_search) (edict_t* self) -> void
{
}

mframe_t turret2_frames_stand[] = {
	{ ai_stand },
	{ ai_stand }
};
MMOVE_T(turret2_move_stand) = { FRAME_stand01, FRAME_stand02, turret2_frames_stand, nullptr };

MONSTERINFO_STAND(turret2_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &turret2_move_stand);
	if (self->target_ent)
	{
		G_FreeEdict(self->target_ent);
		self->target_ent = nullptr;
	}
}

mframe_t turret2_frames_ready_gun[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand },
	{ ai_stand },
	{ ai_stand },

	{ ai_stand }
};
MMOVE_T(turret2_move_ready_gun) = { FRAME_active01, FRAME_run01, turret2_frames_ready_gun, turret2_run };

void turret2_ready_gun(edict_t* self)
{
	if (self->monsterinfo.active_move != &turret2_move_ready_gun)
	{
		M_SetAnimation(self, &turret2_move_ready_gun);
		self->monsterinfo.weapon_sound = sound_moving;
	}
}

mframe_t turret2_frames_seek[] = {
	{ ai_walk, 0, turret2Aim },
	{ ai_walk, 0, turret2Aim }
};
MMOVE_T(turret2_move_seek) = { FRAME_run01, FRAME_run02, turret2_frames_seek, nullptr };

MONSTERINFO_WALK(turret2_walk) (edict_t* self) -> void
{
	if (self->s.frame < FRAME_run01)
		turret2_ready_gun(self);
	else
		M_SetAnimation(self, &turret2_move_seek);
}

mframe_t turret2_frames_run[] = {
	{ ai_run, 0, turret2Aim },
	{ ai_run, 0, turret2Aim }
};
MMOVE_T(turret2_move_run) = { FRAME_run01, FRAME_run02, turret2_frames_run, turret2_run };

void CreateTurretGlowEffect(edict_t* turret);

MONSTERINFO_RUN(turret2_run) (edict_t* self) -> void
{
	CreateTurretGlowEffect(self);

	//if (self->s.frame < FRAME_run01)
	//	turret2_ready_gun(self);
	//else
	{
		self->monsterinfo.aiflags |= AI_HIGH_TICK_RATE;
		M_SetAnimation(self, &turret2_move_run);

		if (self->monsterinfo.weapon_sound)
		{
			self->monsterinfo.weapon_sound = 0;
			gi.sound(self, CHAN_WEAPON, sound_moved, 1.0f, ATTN_STATIC, 0.f);
		}
	}
	TurretSparks(self);
}

//Powerups

void TurretRespondPowerup(edict_t* turret, edict_t* owner) {
	if (!turret || !owner || !owner->client)
		return;

	if (owner->client->quad_time > level.time) {
		turret->monsterinfo.quad_time = owner->client->quad_time;
	}

	if (owner->client->double_time > level.time) {
		turret->monsterinfo.double_time = owner->client->double_time;
	}

	if (owner->client->invincible_time > level.time) {
		turret->monsterinfo.invincible_time = owner->client->invincible_time;
	}
	if (owner->client->quadfire_time > level.time) {
		turret->monsterinfo.quadfire_time = owner->client->quadfire_time;
	}
}


void TurretCheckPowerups(edict_t* turret) {
	if (!turret || !turret->owner || !turret->owner->client)
		return;

	edict_t* owner = turret->owner;

	// Ensure the turret always inherits quad, double, and invincibility from the player
	TurretRespondPowerup(turret, owner);
} // Now, turrets will also inherit invincibility from their owners, just like quad and double.

// **********************
//  ATTACK
// **********************


int32_t TURRET2_BLASTER_DAMAGE = 3;
int32_t TURRET2_BULLET_DAMAGE = 2;


void turret2Fire(edict_t* self)
{
	if (!self || !self->inuse)
		return;

	// Verificar powerups y heredar del dueño
	if (self->owner && self->owner->client) {
		TurretRespondPowerup(self, self->owner);
	}

	// Configurar intervalos de disparo basados en powerups
	const gtime_t ROCKET_FIRE_INTERVAL = self->monsterinfo.quadfire_time > level.time ?
		0.75_sec : 1.5_sec;
	const gtime_t MACHINEGUN_FIRE_RATE = self->monsterinfo.quadfire_time > level.time ?
		9_hz : 15_hz;
	const gtime_t PLASMA_FIRE_INTERVAL = self->monsterinfo.quadfire_time > level.time ?
		0.8_sec : 2_sec;

	// Actualizar orientación
	turret2Aim(self);

	// Validar enemigo
	if (!self->enemy || !self->enemy->inuse ||
		OnSameTeam(self, self->enemy) || self->enemy->deadflag)
	{
		if (!FindMTarget(self))
			return;
	}

	self->monsterinfo.attack_finished = level.time;

	// Determinar punto final
	vec3_t end = (self->monsterinfo.aiflags & AI_LOST_SIGHT) ?
		self->monsterinfo.blind_fire_target :
		self->enemy->s.origin;


	// Configurar velocidad del proyectil
	float projectileSpeed = 0.0f;
	if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
		projectileSpeed = self->monsterinfo.quadfire_time > level.time ? 2000.0f : 1800.0f;
	else if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN))
		projectileSpeed = self->monsterinfo.quadfire_time > level.time ? 1850.0f : 1650.0f;

	// Procesar disparo si hay línea de visión
	if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN) ||
		self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER) ||
		visible(self, self->enemy))
	{
		// Ajustar punto de mira
		if (!(self->monsterinfo.aiflags & AI_LOST_SIGHT)) {
			if (self->enemy->client)
				end[2] += self->enemy->viewheight;
			else
				end[2] += 7;
		}

		// Calcular dirección
		vec3_t start = self->s.origin;
		vec3_t dir = end - start;
		if (!is_valid_vector(dir)) {
			return;
		}
		dir.normalize();

		// Verificar ángulo de disparo
		vec3_t forward;
		AngleVectors(self->s.angles, forward, nullptr, nullptr);
		float chance = dir.dot(forward);
		if (chance < 0.98f)
			return;


		dir = end - start;
		float dist = dir.length();

		// Predicción mejorada con quad
		if (!(self->monsterinfo.aiflags & AI_LOST_SIGHT)) {
			float predictionError = self->monsterinfo.quadfire_time > level.time ?
				0.02f : (frandom(3.f - skill->integer) / 3.f);
			PredictAim(self, self->enemy, start, projectileSpeed, true,
				predictionError, &dir, nullptr);
		}

		// Calcular daño base y modificadores
		const float damageModifier = M_DamageModifier(self);
		const float quadMultiplier = self->monsterinfo.quadfire_time > level.time ?
			1.5f : 1.0f;
		constexpr int baseDamage = 100;
		const int modifiedDamage = static_cast<int>(baseDamage * damageModifier *
			quadMultiplier);

		if (!is_valid_vector(dir)) {
			return;
		}
		dir.normalize();

		// Verificar línea de disparo
		trace_t tr = gi.traceline(start, end, self, MASK_PROJECTILE);
		if (tr.ent == self->enemy || tr.ent == world)
		{
			bool damageApplied = false;

			// Procesar disparo según tipo de arma
			if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN))
			{
				// Sistema de cohetes
				if (level.time > self->monsterinfo.last_rocket_fire_time +
					ROCKET_FIRE_INTERVAL)
				{
					self->monsterinfo.last_rocket_fire_time = level.time;

					if (dist * tr.fraction > 72 && !damageApplied)
					{
						vec3_t rocketDir;
						PredictAim(self, self->enemy, start, projectileSpeed, false,
							self->monsterinfo.quadfire_time > level.time ?
							0.01f : 0.05f,
							&rocketDir, nullptr);

						if (is_valid_vector(rocketDir)) {
							fire_rocket(self->owner, start, rocketDir, modifiedDamage,
								self->monsterinfo.quadfire_time > level.time ?
								1600 : 1420,
								120, modifiedDamage);
							damageApplied = true;
							gi.sound(self, CHAN_VOICE, sound_pew, 1, ATTN_NORM, 0);
						}
					}
				}

				// Sistema de ametralladora
				if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME))
				{
					self->monsterinfo.aiflags |= AI_HOLD_FRAME;
					self->monsterinfo.duck_wait_time = level.time +
						(self->monsterinfo.quadfire_time > level.time ? 3_sec : 5_sec);
					self->monsterinfo.next_duck_time = level.time + 0.1_sec;
					gi.sound(self, CHAN_VOICE, gi.soundindex("weapons/chngnu1a.wav"),
						1, ATTN_NORM, 0);
				}
				else if (self->monsterinfo.next_duck_time < level.time &&
					self->monsterinfo.melee_debounce_time <= level.time &&
					!damageApplied)
				{
					vec3_t bulletDir;
					PredictAim(self, self->enemy, start, 9999, false,
						self->monsterinfo.quadfire_time > level.time ?
						0.01f : 0.03f,
						&bulletDir, nullptr);

					if (is_valid_vector(bulletDir)) {
						// Daño de ametralladora mejorado con quad
						T_Damage(tr.ent, self, self->owner, bulletDir, tr.endpos,
							tr.plane.normal,
							static_cast<int>(TURRET2_BULLET_DAMAGE *
								damageModifier * quadMultiplier),
							static_cast<int>(5 * damageModifier * quadMultiplier),
							DAMAGE_NONE, MOD_TURRET);

						monster_fire_bullet(self, start, bulletDir, 0, 5,
							self->monsterinfo.quadfire_time > level.time ?
							DEFAULT_BULLET_HSPREAD / 2 : DEFAULT_BULLET_HSPREAD,
							self->monsterinfo.quadfire_time > level.time ?
							DEFAULT_BULLET_VSPREAD / 2 : DEFAULT_BULLET_VSPREAD,
							MZ2_TURRET_MACHINEGUN);

						// Velocidad de disparo basada en quad
						self->monsterinfo.melee_debounce_time = level.time +
							MACHINEGUN_FIRE_RATE;
						damageApplied = true;
					}

					if (self->monsterinfo.duck_wait_time < level.time)
						self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
				}
			}
			else if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
			{
				vec3_t offset = { 20.f, 0.f, 0.f };
				// Calcular el punto de inicio usando el offset fijo
				const vec3_t hbturretstart = self->s.origin + (forward * offset[0]);

				vec3_t predictedDir;
				PredictAim(self, self->enemy, hbturretstart, 9999, false,
					self->monsterinfo.quadfire_time > level.time ?
					0.01f : 0.03f,
					&predictedDir, nullptr);

				if (is_valid_vector(predictedDir)) {
					trace_t blasterTrace = gi.traceline(hbturretstart,
						hbturretstart + predictedDir * 8192,
						self, MASK_PROJECTILE);

					if (blasterTrace.ent == self->enemy ||
						blasterTrace.ent == world)
					{
						if (!damageApplied)
						{
							// Daño del blaster mejorado con quad
							T_Damage(blasterTrace.ent, self, self->owner,
								predictedDir, blasterTrace.endpos,
								blasterTrace.plane.normal,
								static_cast<int>(TURRET2_BLASTER_DAMAGE *
									damageModifier * quadMultiplier),
								0, DAMAGE_ENERGY, MOD_TURRET);

							monster_fire_heatbeam(self, hbturretstart, predictedDir,
								vec3_origin,
								self->monsterinfo.quadfire_time >
								level.time ? 2 : 0,
								10, MZ2_WIDOW2_BEAM_SWEEP_1);  // Cambiado a MZ2_WIDOW2_BEAM_SWEEP_1
							damageApplied = true;
						}


						// Sistema de plasma mejorado
						if (level.time > self->monsterinfo.last_plasma_fire_time +
							PLASMA_FIRE_INTERVAL)
						{
							self->monsterinfo.last_plasma_fire_time = level.time;

							fire_plasma(self->owner, hbturretstart, predictedDir,
								static_cast<int>(100 * quadMultiplier),
								self->monsterinfo.quadfire_time > level.time ?
								1450 : 1250,
								120,
								static_cast<int>(100 * quadMultiplier));
							gi.sound(self, CHAN_VOICE, sound_pew, 1, ATTN_NORM, 0);
						}
					}
				}
			}
		}
	}
}

// PMM
void turret2FireBlind(edict_t* self)
{
	vec3_t forward;
	vec3_t start, end, dir;
	float  chance;
	int	   rocketSpeed = 550;

	turret2Aim(self);

	if (!self->enemy || !self->enemy->inuse)
		return;

	dir = self->monsterinfo.blind_fire_target - self->s.origin;
	dir.normalize();
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	chance = dir.dot(forward);
	if (chance < 0.98f)
		return;

	if (self->spawnflags.has(SPAWNFLAG_TURRET2_ROCKET))
		rocketSpeed = 650;
	else if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
		rocketSpeed = 800;
	else
		rocketSpeed = 0;

	start = self->s.origin;
	end = self->monsterinfo.blind_fire_target;

	if (self->enemy->s.origin[2] < self->monsterinfo.blind_fire_target[2])
		end[2] += self->enemy->mins[2] - 10;
	else
		end[2] += self->enemy->mins[2] - 10;

	dir = end - start;

	dir.normalize();

	if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
	{
		// Aplica el daño con el mod_t configurado
		monster_fire_heatbeam(self, start, forward, vec3_origin, 1, 50, MZ2_TURRET_BLASTER);
	}
	else if (self->spawnflags.has(SPAWNFLAG_TURRET2_ROCKET))
		monster_fire_rocket(self, start, dir, 40, rocketSpeed, MZ2_TURRET_ROCKET);
}
// pmm

mframe_t turret2_frames_fire[] = {
	{ ai_run, 0, turret2Aim },
	{ ai_run, 0, turret2Fire },
	{ ai_run, 0, turret2Fire },
	{ ai_run, 0, turret2Fire },
};
MMOVE_T(turret2_move_fire) = { FRAME_pow01, FRAME_pow04, turret2_frames_fire, turret2_run };

// PMM

// the blind frames need to aim first
mframe_t turret2_frames_fire_blind[] = {
	{ ai_run, 0, turret2Aim },
	{ ai_run, 0, turret2FireBlind },
	{ ai_run, 0, turret2FireBlind },
	{ ai_run, 0, turret2FireBlind }

};
MMOVE_T(turret2_move_fire_blind) = { FRAME_pow01, FRAME_pow04, turret2_frames_fire_blind, turret2_run };
// pmm

MONSTERINFO_ATTACK(turret2_attack) (edict_t* self) -> void
{
	if (self->s.frame < FRAME_run01)
	{
		turret2_ready_gun(self);
	}
	else if (self->monsterinfo.attack_state != AS_BLIND)
	{
		M_SetAnimation(self, &turret2_move_fire);
	}
	else
	{
		// No delays or probabilities, directly set the blind fire animation
		if (!self->monsterinfo.blind_fire_target)
			return;

		M_SetAnimation(self, &turret2_move_fire_blind);
	}

	// pmm
}
float entdist(const edict_t* ent1, const edict_t* ent2)
{
	return (ent1->s.origin - ent2->s.origin).length();
}

void TurretSparks(edict_t* self)
{
	if (!self || !self->inuse)
		return;

	if (self->health <= (self->max_health / 3)) {
		if (level.time >= self->monsterinfo.next_duck_time) {
			vec3_t forward, right, up;
			AngleVectors(self->s.angles, forward, right, up);

			// Calculate spark origin using offset
			vec3_t spark_origin = self->s.origin + (forward * 20.0f);

			vec3_t dir;
			if (!self->enemy) {
				dir = { crandom(), crandom(), crandom() };
				dir.normalize();
			}
			else {
				dir = (spark_origin - self->enemy->s.origin).normalized();
			}

			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_SPLASH);
			gi.WriteByte(32);
			gi.WritePosition(spark_origin);
			gi.WriteDir(dir);
			gi.WriteByte(SPLASH_SPARKS);
			gi.multicast(spark_origin, MULTICAST_PVS, false);

			self->monsterinfo.next_duck_time = level.time + random_time(2_sec, 7_sec);
		}
	}
}

// **********************
//  PAIN
// **********************

PAIN(turret2_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;
	gi.sound(self, CHAN_VOICE, gi.soundindex("tank/tnkpain2.wav"), 1, ATTN_NORM, 0);

	// Calculate spark origin with offset
	vec3_t forward, right, up;
	AngleVectors(self->s.angles, forward, right, up);
	vec3_t spark_origin = self->s.origin + (forward * 20.0f);

	// Create spark effect for heavy hits (damage >= 40)
	if (damage >= 40) {
		vec3_t dir = (spark_origin - (other ? other->s.origin : spark_origin)).normalized();

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_SPLASH);
		gi.WriteByte(32);
		gi.WritePosition(spark_origin);
		gi.WriteDir(dir);
		gi.WriteByte(SPLASH_SPARKS);
		gi.multicast(spark_origin, MULTICAST_PVS, false);
	}

	self->enemy = other;

	// Call periodic sparks function
	TurretSparks(self);
}

// **********************
//  DEATH
// **********************

DIE(turret2_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	OnEntityDeath(self);
	vec3_t forward;
	edict_t* base;

	if (self->owner && self->owner->client) {
		gi.Client_Print(self->owner, PRINT_HIGH, "Your sentry gun was destroyed.\n");
		self->owner->client->num_sentries--;
	}

	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	self->s.origin += (forward * 1);

	ThrowGibs(self, 1, {
		{ 2, "models/objects/debris1/tris.md2", GIB_METALLIC | GIB_DEBRIS }
		});

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_BFG_BIGEXPLOSION);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PHS, false);

	if (self->teamchain)
	{
		base = self->teamchain;
		base->solid = SOLID_NOT;
		base->takedamage = false;
		base->movetype = MOVETYPE_NONE;
		base->teammaster = base;
		base->teamchain = nullptr;
		base->flags &= ~FL_TEAMSLAVE;
		base->flags |= FL_TEAMMASTER;
		gi.linkentity(base);

		self->teammaster = self->teamchain = nullptr;
		self->flags &= ~(FL_TEAMSLAVE | FL_TEAMMASTER);
	}

	if (self->target)
	{
		if (self->enemy && self->enemy->inuse)
			G_UseTargets(self, self->enemy);
		else
			G_UseTargets(self, self);
	}

	// Limpiar el efecto de brillo
	if (self->target_hint_chain && self->target_hint_chain->inuse) {
		G_FreeEdict(self->target_hint_chain);
		self->target_hint_chain = nullptr;
	}

	if (self->target_ent)
	{
		G_FreeEdict(self->target_ent);
		self->target_ent = nullptr;
	}

	edict_t* gib = ThrowGib(self, "models/monsters/turret/tris.md2", damage, GIB_SKINNED | GIB_METALLIC | GIB_HEAD | GIB_DEBRIS, self->s.scale);
	gib->s.frame = 14;

	// Si la torreta murió porque su propietario desapareció
	if (!self->owner || !self->owner->inuse)
	{
		self->think = G_FreeEdict;
		self->nextthink = level.time + 0_sec;
	}
}


// **********************
//  WALL SPAWN
// **********************

void turret2_wall_spawn(edict_t* turret)
{
	edict_t* ent;
	int		 angle;

	ent = G_Spawn();
	ent->s.origin = turret->s.origin;
	ent->s.angles = turret->s.angles;

	angle = (int)ent->s.angles[1];
	if (ent->s.angles[0] == 90)
		angle = -1;
	else if (ent->s.angles[0] == 270)
		angle = -2;
	switch (angle)
	{
	case -1:
		ent->mins = { -16, -16, -8 };
		ent->maxs = { 16, 16, 0 };
		break;
	case -2:
		ent->mins = { -16, -16, 0 };
		ent->maxs = { 16, 16, 8 };
		break;
	case 0:
		ent->mins = { -8, -16, -16 };
		ent->maxs = { 0, 16, 16 };
		break;
	case 90:
		ent->mins = { -16, -8, -16 };
		ent->maxs = { 16, 0, 16 };
		break;
	case 180:
		ent->mins = { 0, -16, -16 };
		ent->maxs = { 8, 16, 16 };
		break;
	case 270:
		ent->mins = { -16, 0, -16 };
		ent->maxs = { 16, 8, 16 };
		break;
	}

	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_NOT;

	ent->teammaster = turret;
	turret->flags |= FL_TEAMMASTER;
	turret->teammaster = turret;
	turret->teamchain = ent;
	ent->teamchain = nullptr;
	ent->flags |= FL_TEAMSLAVE;
	ent->owner = turret;

	ent->s.modelindex = gi.modelindex("models/monsters/turretbase/tris.md2");

	gi.linkentity(ent);
}

MOVEINFO_ENDFUNC(turret2_wake) (edict_t* ent) -> void
{
	// the wall section will call this when it stops moving.
	// just return without doing anything. easiest way to have a null function.
	if (ent->flags & FL_TEAMSLAVE)
	{
		ent->s.sound = 0;
		return;
	}

	ent->monsterinfo.stand = turret2_stand;
	ent->monsterinfo.walk = turret2_walk;
	ent->monsterinfo.run = turret2_run;
	ent->monsterinfo.dodge = nullptr;
	ent->monsterinfo.attack = turret2_attack;
	ent->monsterinfo.melee = nullptr;
	ent->monsterinfo.sight = turret2_sight;
	ent->monsterinfo.search = turret2_search;
	M_SetAnimation(ent, &turret2_move_stand);
	ent->takedamage = true;
	ent->movetype = MOVETYPE_NONE;
	// prevent counting twice
	ent->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	gi.linkentity(ent);

	stationarymonster_start(ent, spawn_temp_t::empty);

	if (ent->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN))
	{
		ent->s.skinnum = 1;
	}
	else if (ent->spawnflags.has(SPAWNFLAG_TURRET2_ROCKET))
	{
		ent->s.skinnum = 2;
	}

	// but we do want the death to count
	ent->monsterinfo.aiflags &= ~AI_DO_NOT_COUNT;
}

USE(turret2_activate) (edict_t* self, edict_t* other, edict_t* activator) -> void
{
	vec3_t	 endpos;
	vec3_t	 forward = { 0, 0, 0 };
	edict_t* base;

	self->movetype = MOVETYPE_NONE;
	if (!self->speed)
		self->speed = 15;
	self->moveinfo.speed = self->speed;
	self->moveinfo.accel = self->speed;
	self->moveinfo.decel = self->speed;

	if (self->s.angles[0] == 270)
	{
		forward = { 0, 0, 1 };
	}
	else if (self->s.angles[0] == 90)
	{
		forward = { 0, 0, -1 };
	}
	else if (self->s.angles[1] == 0)
	{
		forward = { 1, 0, 0 };
	}
	else if (self->s.angles[1] == 90)
	{
		forward = { 0, 1, 0 };
	}
	else if (self->s.angles[1] == 180)
	{
		forward = { -1, 0, 0 };
	}
	else if (self->s.angles[1] == 270)
	{
		forward = { 0, -1, 0 };
	}

	// start up the turret
	endpos = self->s.origin + (forward * 32);
	Move_Calc(self, endpos, turret2_wake);

	base = self->teamchain;
	if (base)
	{
		base->movetype = MOVETYPE_PUSH;
		base->speed = self->speed;
		base->moveinfo.speed = base->speed;
		base->moveinfo.accel = base->speed;
		base->moveinfo.decel = base->speed;

		// start up the wall section
		endpos = self->teamchain->s.origin + (forward * 32);
		Move_Calc(self->teamchain, endpos, turret2_wake);

		base->s.sound = sound_moving;
		base->s.loop_attenuation = ATTN_NORM;
	}
}
// PMM

MONSTERINFO_CHECKATTACK(turret2_checkattack) (edict_t* self) -> bool
{
	if (!self->enemy || self->enemy->health <= 0) {
		return false;
	}

	// Punto de origen ajustado para la torreta
	vec3_t spot1 = self->s.origin;
	spot1.z += self->viewheight;

	// Punto de destino ajustado según el scale del enemigo
	vec3_t spot2 = self->enemy->s.origin;
	if (self->enemy->client) {
		spot2.z += self->enemy->viewheight;
	}
	else {
		// Ajuste dinámico basado en el scale del enemigo
		float enemy_height = (self->enemy->maxs[2] - self->enemy->mins[2]) * self->enemy->s.scale;
		spot2.z += enemy_height * 0.5f; // Apuntar al centro de masa
	}

	// Máscara de contenido que ignora otros monstruos para mejor detección
	const contents_t mask = static_cast<contents_t>(CONTENTS_SOLID | CONTENTS_SLIME | CONTENTS_LAVA | CONTENTS_WINDOW) & ~CONTENTS_MONSTER;
	trace_t tr = gi.traceline(spot1, spot2, self, mask);

	// Búsqueda de objetivos mejorada considerando el scale
	if (tr.ent && tr.ent->svflags & SVF_MONSTER && !OnSameTeam(self, tr.ent) && tr.ent->health > 0) {
		vec3_t diff_current = self->enemy->s.origin - self->s.origin;
		vec3_t diff_new = tr.ent->s.origin - self->s.origin;

		// Usar length() en vez de VectorLength
		float dist_current = diff_current.length() / self->enemy->s.scale;
		float dist_new = diff_new.length() / tr.ent->s.scale;

		// Cambiar objetivo si encontramos uno mejor considerando scale
		if (dist_new < dist_current) {
			self->enemy = tr.ent;
			spot2 = tr.ent->s.origin;
			spot2.z += (tr.ent->maxs[2] - tr.ent->mins[2]) * tr.ent->s.scale * 0.5f;
		}
	}

	// Verificación de ataque considerando línea de visión y scale
	if (tr.fraction == 1.0 || tr.ent == self->enemy ||
		(tr.ent && tr.ent->svflags & SVF_MONSTER && !OnSameTeam(self, tr.ent)))
	{
		if (level.time < self->monsterinfo.attack_finished)
			return false;

		// Probabilidades base según el arma
		float chance = self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER) ? 0.9f :
			self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN) ? 0.8f : 0.7f;

		// Ajustar probabilidad según el scale del enemigo
		float scale_factor = self->enemy->s.scale;
		chance += (scale_factor > 1.0f) ? 0.1f : (scale_factor < 1.0f) ? -0.1f : 0.0f;

		// Ajustar según la distancia y scale combinados
		float range = range_to(self, self->enemy) / scale_factor;
		if (range <= RANGE_MELEE) {
			chance = 1.0f;
		}
		else if (range <= RANGE_NEAR) {
			chance += 0.15f;
		}
		else if (range <= RANGE_MID) {
			chance += 0.05f;
		}

		if (frandom() > chance) {
			return false;
		}

		self->monsterinfo.attack_state = AS_MISSILE;
		self->monsterinfo.attack_finished = level.time + 150_ms;
		return true;
	}

	// Fuego ciego mejorado con consideración de scale
	if (self->monsterinfo.blindfire && level.time > self->monsterinfo.blind_fire_delay)
	{
		vec3_t aim_point = self->monsterinfo.last_sighting;

		// Ajustar predicción según scale y velocidad
		if (self->enemy->client || self->enemy->velocity != vec3_origin) {
			// Usar operadores de vec3_t directamente
			aim_point += self->enemy->velocity * (0.2f * self->enemy->s.scale);
		}

		// Variación aleatoria ajustada al scale
		float spread = 100.0f / self->enemy->s.scale;
		vec3_t random_spread{
			crandom() * spread,
			crandom() * spread,
			crandom() * spread
		};
		aim_point += random_spread;

		tr = gi.traceline(spot1, aim_point, self, mask);
		if (tr.fraction >= 0.3f) {
			self->monsterinfo.attack_state = AS_BLIND;
			self->monsterinfo.blind_fire_delay = level.time + 1.5_sec;
			return true;
		}
	}

	return false;
}

// **********************
//  SPAWN
// **********************

/*QUAKED monster_sentrygun (1 .5 0) (-16 -16 -16) (16 16 16) Ambush Trigger_Spawn Sight Blaster MachineGun Rocket Heatbeam WallUnit

The automated defense turret that mounts on walls.
Check the weapon you want it to use: blaster, machinegun, rocket, heatbeam.
Default weapon is blaster.
When activated, wall units move 32 units in the direction they're facing.
*/

THINK(EmitSmokeEffect)(edict_t* ent) -> void {
	if (!ent || !ent->owner || !ent->owner->inuse) {
		G_FreeEdict(ent);
		return;
	}

	// Solo emitir humo con una probabilidad del 40%
	if (frandom() < 0.4f) {
		// Escribir el efecto de humo
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_CHAINFIST_SMOKE);
		gi.WritePosition(ent->s.origin);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);
	}

	// Configurar el próximo think con tiempo aleatorio
	ent->nextthink = level.time + random_time(2_sec, 5_sec);
	ent->think = EmitSmokeEffect;
}

void CreateTurretGlowEffect(edict_t* turret) {
	if (!turret || !turret->inuse)
		return;

	// Eliminar el efecto anterior si existe
	if (turret->target_hint_chain && turret->target_hint_chain->inuse) {
		G_FreeEdict(turret->target_hint_chain);
		turret->target_hint_chain = nullptr;
	}

	edict_t* smoke = G_Spawn();
	if (!smoke)
		return;

	smoke->movetype = MOVETYPE_NONE;
	smoke->solid = SOLID_NOT;
	smoke->s.modelindex = 0;  // No necesitamos modelo para el efecto de humo
	smoke->s.renderfx = RF_FULLBRIGHT;
	smoke->s.effects = EF_BOB;  // Efecto de bobbing
	smoke->owner = turret;
	smoke->classname = "turret_smoke";
	smoke->think = EmitSmokeEffect;
	smoke->nextthink = level.time + random_time(8_sec, 15_sec);  // Inicio retrasado aleatorio

	// Posicionar el emisor de humo usando el nuevo vec3_t
	vec3_t forward;
	AngleVectors(turret->s.angles, forward, nullptr, nullptr);

	// Usar la nueva sintaxis de vec3_t para el posicionamiento
	smoke->s.origin = turret->s.origin + (forward * 20.0f);
	smoke->s.angles = turret->s.angles;

	gi.linkentity(smoke);
	turret->target_hint_chain = smoke;
}
void SP_monster_sentrygun(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	// Al crear la torreta, verificar si el owner tiene power-ups activos
	if (self->owner && self->owner->client) {
		TurretRespondPowerup(self, self->owner);
	}


#define playeref self->owner->s.effects;
	self->monsterinfo.last_rocket_fire_time = gtime_t::from_sec(0); // Inicializa el tiempo de último disparo de cohete
	int angle;

	self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
//	self->monsterinfo.team = CTF_TEAM1;
	self->monsterinfo.attack_state = AS_BLIND;

	//test EF grenade
	self->s.effects = EF_BOB; // Quitar EF_GRENADE de aquí
	self->target_hint_chain = nullptr; // Inicializar el puntero del efecto
	// Crear el efecto visual después de establecer la posición y ángulos
	CreateTurretGlowEffect(self);

	ApplyMonsterBonusFlags(self);

	if (!M_AllowSpawn(self))
	{
		G_FreeEdict(self);
		return;
	}

	// pre-caches
	sound_pew.assign("makron/blaster.wav");
	sound_moved.assign("gunner/gunidle1.wav");
	sound_moving.assign("turret/moving.wav");
	gi.modelindex("models/objects/debris1/tris.md2");

	self->s.modelindex = gi.modelindex("models/monsters/turret/tris.md2");
	self->mins = { -12, -12, -12 };
	self->maxs = { 12, 12, 12 };
	self->movetype = MOVETYPE_NONE;




	self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
	self->monsterinfo.power_armor_power = 100;
	self->health = 80;
	self->gib_health = -100;
	self->mass = 100;
	self->yaw_speed = 16;
	self->solid = SOLID_BBOX;
	self->svflags = SVF_PLAYER;
	self->flags |= FL_MECHANICAL;
	self->pain = turret2_pain;
	self->die = turret2_die;

	//if (self->client && !G_ShouldPlayersCollide(true) || self->owner->client && !G_ShouldPlayersCollide(true)) {
	//	self->clipmask &= ~CONTENTS_PLAYER;
	//}

	//// map designer didn't specify weapon type. set it now.
	//if (!self->spawnflags.has(SPAWNFLAG_TURRET2_WEAPONCHOICE) && current_wave_level <= 5)
	//	self->spawnflags |= SPAWNFLAG_TURRET2_MACHINEGUN;

	// map designer didn't specify weapon type. set it now.
	const float randomValue = frandom();

	// Si el valor aleatorio es menor que 0.3, selecciona HEATBEAM; de lo contrario, MACHINEGUN
	if (!self->spawnflags.has(SPAWNFLAG_TURRET2_WEAPONCHOICE)) {
		if (randomValue < 0.3f) {
			self->spawnflags |= SPAWNFLAG_TURRET2_HEATBEAM;
		}
		else {
			self->spawnflags |= SPAWNFLAG_TURRET2_MACHINEGUN;
		}
	}



	if (self->spawnflags.has(SPAWNFLAG_TURRET2_HEATBEAM))
	{
		self->spawnflags &= ~SPAWNFLAG_TURRET2_HEATBEAM;
		self->spawnflags |= SPAWNFLAG_TURRET2_BLASTER;
	}

	if (!self->spawnflags.has(SPAWNFLAG_TURRET2_WALL_UNIT))
	{
		self->monsterinfo.stand = turret2_stand;
		self->monsterinfo.walk = turret2_walk;
		self->monsterinfo.run = turret2_run;
		self->monsterinfo.dodge = nullptr;
		self->monsterinfo.attack = turret2_attack;
		self->monsterinfo.melee = nullptr;
		self->monsterinfo.sight = turret2_sight;
		self->monsterinfo.search = turret2_search;
		M_SetAnimation(self, &turret2_move_stand);
	}

	// PMM
	self->monsterinfo.checkattack = turret2_checkattack;

	self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
	self->monsterinfo.scale = MODEL_SCALE;
	self->gravity = 0;

	self->offset = self->s.angles;
	angle = (int)self->s.angles[1];
	switch (angle)
	{
	case -1: // up
		self->s.angles[0] = 270;
		self->s.angles[1] = 0;
		self->s.origin[2] += 2;
		break;
	case -2: // down
		self->s.angles[0] = 90;
		self->s.angles[1] = 0;
		self->s.origin[2] -= 2;
		break;
	case 0:
		self->s.origin[0] += 2;
		break;
	case 90:
		self->s.origin[1] += 2;
		break;
	case 180:
		self->s.origin[0] -= 2;
		break;
	case 270:
		self->s.origin[1] -= 2;
		break;
	default:
		break;
	}

	gi.linkentity(self);

	if (self->spawnflags.has(SPAWNFLAG_TURRET2_WALL_UNIT))
	{
		if (!self->targetname)
		{
			G_FreeEdict(self);
			return;
		}

		self->takedamage = false;
		self->use = turret2_activate;
		turret2_wall_spawn(self);
		if (!(self->monsterinfo.aiflags & AI_DO_NOT_COUNT))
		{
			if (g_debug_monster_kills->integer)
				level.monsters_registered[level.total_monsters] = self;
			level.total_monsters++;
		}
	}
	else
	{
		stationarymonster_start(self, spawn_temp_t::empty);
	}

	if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN))
	{
		gi.modelindex("models/objects/laser/tris.md2");
		gi.soundindex("infantry/infatck1.wav");
		gi.soundindex("weapons/chngnu1a.wav");
		gi.soundindex("weapons/rockfly.wav");
		gi.modelindex("models/objects/rocket/tris.md2");
		gi.soundindex("chick/chkatck2.wav");
		gi.soundindex("tank/tnkpain2.wav");
		gi.soundindex("makron/blaster.wav");
		gi.soundindex("gunner/gunidle1.wav");

		self->s.skinnum = 2;

		self->spawnflags &= ~SPAWNFLAG_TURRET2_WEAPONCHOICE;
		self->spawnflags |= SPAWNFLAG_TURRET2_MACHINEGUN;
	}
	else if (self->spawnflags.has(SPAWNFLAG_TURRET2_ROCKET))
	{
		gi.soundindex("weapons/rockfly.wav");
		gi.modelindex("models/objects/rocket/tris.md2");
		gi.soundindex("chick/chkatck2.wav");
		gi.soundindex("tank/tnkpain2.wav");
		gi.soundindex("makron/blaster.wav");
		gi.soundindex("gunner/gunidle1.wav");
		self->s.skinnum = 2;

		self->spawnflags &= ~SPAWNFLAG_TURRET2_WEAPONCHOICE;
		self->spawnflags |= SPAWNFLAG_TURRET2_ROCKET;
	}
	else
	{
		gi.modelindex("models/objects/laser/tris.md2");
		gi.soundindex("misc/lasfly.wav");
		gi.soundindex("soldier/solatck2.wav");
		gi.soundindex("tank/tnkpain2.wav");
		gi.soundindex("makron/blaster.wav");
		gi.soundindex("gunner/gunidle1.wav");

		self->spawnflags &= ~SPAWNFLAG_TURRET2_WEAPONCHOICE;
		self->spawnflags |= SPAWNFLAG_TURRET2_BLASTER;
	}

	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
		self->yaw_speed = 15;
	if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN | SPAWNFLAG_TURRET2_BLASTER))
		self->monsterinfo.blindfire = true;

	if (self->monsterinfo.quadfire_time > level.time) {
		self->yaw_speed *= 2.0f;
	}
}