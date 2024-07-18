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
	extern inline void VectorCopy(const vec3_t& src, vec3_t& dest) noexcept;

	constexpr spawnflags_t SPAWNFLAG_TURRET2_BLASTER = 0x0008_spawnflag;
	constexpr spawnflags_t SPAWNFLAG_TURRET2_MACHINEGUN = 0x0010_spawnflag;
	constexpr spawnflags_t SPAWNFLAG_TURRET2_ROCKET = 0x0020_spawnflag;
	constexpr spawnflags_t SPAWNFLAG_TURRET2_HEATBEAM = 0x0040_spawnflag;
	constexpr spawnflags_t SPAWNFLAG_TURRET2_WEAPONCHOICE = SPAWNFLAG_TURRET2_HEATBEAM | SPAWNFLAG_TURRET2_ROCKET | SPAWNFLAG_TURRET2_MACHINEGUN | SPAWNFLAG_TURRET2_BLASTER;
	constexpr spawnflags_t SPAWNFLAG_TURRET2_WALL_UNIT = 0x0080_spawnflag;
	constexpr spawnflags_t SPAWNFLAG_TURRET2_NO_LASERSIGHT = 18_spawnflag_bit;

	bool FindMTarget(edict_t* self) {
		edict_t* ent = nullptr;
		float range = 800.0f; // Rango de búsqueda
		vec3_t dir{};
		float bestDist = range + 1.0f; // Inicializa con un valor mayor al rango
		edict_t* bestTarget = nullptr;

		for (unsigned int i = 0; i < globals.num_edicts; i++) {
			ent = &g_edicts[i];

			// Verifica si la entidad está en uso, es sólida, no es la misma entidad, tiene salud mayor a 0 y no está muerta
			if (!ent->inuse || !ent->solid || ent == self || ent->health <= 0 || ent->deadflag || ent->solid == SOLID_NOT)
				continue;

			// Asegúrate de que no es un jugador
			if (ent->svflags & SVF_PLAYER)
				continue;

			// Solo busca enemigos en el equipo contrario y que sean monstruos
			if (!OnSameTeam(self, ent) && (ent->svflags & SVF_MONSTER)) {
				VectorSubtract(ent->s.origin, self->s.origin, dir);
				float dist = VectorLength(dir);

				// Verifica si la entidad está dentro del rango, es visible y es la más cercana encontrada hasta ahora
				if (dist < range && visible(self, ent) && dist < bestDist) {
					bestDist = dist;
					bestTarget = ent;
				}
			}
		}

		// Si se encontró un objetivo válido, se asigna como enemigo
		if (bestTarget) {
			self->enemy = bestTarget;
			return true;
		}

		return false; // No se encontró objetivo válido
	}

	void turret2Aim(edict_t* self);
	void turret2_ready_gun(edict_t* self);
	void turret2_run(edict_t* self);

	extern const mmove_t turret2_move_fire;
	extern const mmove_t turret2_move_fire_blind;

	static cached_soundindex sound_moved, sound_moving;

	void turret2Aim(edict_t* self)
	{
		vec3_t end, dir;
		vec3_t ang;
		float move, idealPitch, idealYaw, current, speed;
		int orientation;

		// Verifica el estado del enemigo
		bool enemy_valid = (self->enemy && self->enemy != world && self->enemy->inuse && !OnSameTeam(self, self->enemy));

		// Si el enemigo no es válido, busca un nuevo objetivo
		if (!enemy_valid)
		{
			if (!FindMTarget(self))
				return;
		}

		// Actualiza el enemigo válido después de intentar encontrar un nuevo objetivo
		enemy_valid = (self->enemy && self->enemy != world && self->enemy->inuse && !OnSameTeam(self, self->enemy));
		if (!enemy_valid)
			return;

		// Si la torreta está en modo inactivo, prepara el arma, pero no apuntes
		if (self->s.frame < FRAME_active01)
		{
			turret2_ready_gun(self);
			return;
		}
		// Si la torreta aún se está preparando, no apuntes.
		if (self->s.frame < FRAME_run01)
			return;

		// PMM - Apuntado de fuego ciego aquí
		if (self->monsterinfo.active_move == &turret2_move_fire_blind)
		{
			end = self->monsterinfo.blind_fire_target;
			if (self->enemy->s.origin[2] < self->monsterinfo.blind_fire_target[2])
				end[2] += self->enemy->viewheight + 10;
			else
				end[2] += self->enemy->mins[2] - 10;
		}
		else
		{
			end = self->enemy->s.origin;
			if (self->enemy->client)
				end[2] += self->enemy->viewheight;
			else
				end[2] += (self->enemy->mins[2] + self->enemy->maxs[2]) / 2; // Ajusta para enemigos pequeños
		}

		dir = end - self->s.origin;
		ang = vectoangles(dir);

		// Ajuste de pitch y yaw
		idealPitch = ang[PITCH];
		idealYaw = ang[YAW];

		orientation = (int)self->offset[1];
		switch (orientation)
		{
		case -1: // up, pitch: 0 to 90
			if (idealPitch < -90)
				idealPitch += 360;
			if (idealPitch > -5)
				idealPitch = -5;
			break;
		case -2: // down, pitch: -180 to -360
			if (idealPitch > -90)
				idealPitch -= 360;
			if (idealPitch < -355)
				idealPitch = -355;
			else if (idealPitch > -185)
				idealPitch = -185;
			break;
		case 0: // +X, pitch: 0 to -90, -270 to -360 (or 0 to 90)
			if (idealPitch < -180)
				idealPitch += 360;

			if (idealPitch > 85)
				idealPitch = 85;
			else if (idealPitch < -85)
				idealPitch = -85;

			if (idealYaw > 180)
				idealYaw -= 360;
			if (idealYaw > 85)
				idealYaw = 85;
			else if (idealYaw < -85)
				idealYaw = -85;
			break;
		case 90: // +Y, pitch: 0 to 90, -270 to -360 (or 0 to 90)
			if (idealPitch < -180)
				idealPitch += 360;

			if (idealPitch > 85)
				idealPitch = 85;
			else if (idealPitch < -85)
				idealPitch = -85;

			if (idealYaw > 270)
				idealYaw -= 360;
			if (idealYaw > 175)
				idealYaw = 175;
			else if (idealYaw < 5)
				idealYaw = 5;
			break;
		case 180: // -X, pitch: 0 to 90, -270 to -360 (or 0 to 90)
			if (idealPitch < -180)
				idealPitch += 360;

			if (idealPitch > 85)
				idealPitch = 85;
			else if (idealPitch < -85)
				idealPitch = -85;

			if (idealYaw > 265)
				idealYaw = 265;
			else if (idealYaw < 95)
				idealYaw = 95;
			break;
		case 270: // -Y, pitch: 0 to 90, -270 to -360 (or 0 to 90)
			if (idealPitch < -180)
				idealPitch += 360;

			if (idealPitch > 85)
				idealPitch = 85;
			else if (idealPitch < -85)
				idealPitch = -85;

			if (idealYaw < 90)
				idealYaw += 360;
			if (idealYaw > 355)
				idealYaw = 355;
			else if (idealYaw < 185)
				idealYaw = 185;
			break;
		}

		// Ajuste de pitch
		current = self->s.angles[PITCH];
		speed = self->yaw_speed / (gi.tick_rate / 10);

		if (idealPitch != current)
		{
			move = idealPitch - current;

			while (move >= 360)
				move -= 360;
			if (move >= 90)
			{
				move = move - 360;
			}

			while (move <= -360)
				move += 360;
			if (move <= -90)
			{
				move = move + 360;
			}

			if (move > 0)
			{
				if (move > speed)
					move = speed;
			}
			else
			{
				if (move < -speed)
					move = -speed;
			}

			self->s.angles[PITCH] = anglemod(current + move);
		}

		// Ajuste de yaw
		current = self->s.angles[YAW];

		if (idealYaw != current)
		{
			move = idealYaw - current;

			if (move >= 180)
			{
				move = move - 360;
			}

			if (move <= -180)
			{
				move = move + 360;
			}

			if (move > 0)
			{
				if (move > speed)
					move = speed;
			}
			else
			{
				if (move < -speed)
					move = -speed;
			}

			self->s.angles[YAW] = anglemod(current + move);
		}

		if (self->spawnflags.has(SPAWNFLAG_TURRET2_NO_LASERSIGHT) || self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
			return;

		// Paril: improved turrets; draw lasersight
		if (!self->target_ent)
		{
			self->target_ent = G_Spawn();
			self->target_ent->s.modelindex = MODELINDEX_WORLD;
			self->target_ent->s.renderfx = RF_BEAM;
			self->target_ent->s.frame = 1;
			self->target_ent->s.skinnum = 0xd0d1d2d3;
			self->target_ent->classname = "turret2_lasersight";
			self->target_ent->s.effects = EF_BOB;
			self->target_ent->s.origin = self->s.origin;
			self->target_ent->owner = self;  // Establecer el propietario del lasersight
		}

		vec3_t forward;
		AngleVectors(self->s.angles, forward, nullptr, nullptr);
		end = self->s.origin + (forward * 8192);
		trace_t tr = gi.traceline(self->s.origin, end, self, MASK_SOLID);

		float scan_range = 64.f;

		if (visible(self, self->enemy))
			scan_range = 12.f;

		tr.endpos[0] += sinf(level.time.seconds() + self->s.number) * scan_range;
		tr.endpos[1] += cosf((level.time.seconds() - self->s.number) * 3.f) * scan_range;
		tr.endpos[2] += sinf((level.time.seconds() - self->s.number) * 2.5f) * scan_range;

		forward = tr.endpos - self->s.origin;
		forward.normalize();

		end = self->s.origin + (forward * 8192);
		tr = gi.traceline(self->s.origin, end, self, MASK_SOLID);

		self->target_ent->s.old_origin = tr.endpos;
		gi.linkentity(self->target_ent);
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

	MONSTERINFO_RUN(turret2_run) (edict_t* self) -> void
	{
		if (self->s.frame < FRAME_run01)
			turret2_ready_gun(self);
		else
		{
			self->monsterinfo.aiflags |= AI_HIGH_TICK_RATE;
			M_SetAnimation(self, &turret2_move_run);

			if (self->monsterinfo.weapon_sound)
			{
				self->monsterinfo.weapon_sound = 0;
				gi.sound(self, CHAN_WEAPON, sound_moved, 1.0f, ATTN_STATIC, 0.f);
			}
		}
	}

	// **********************
	//  ATTACK
	// **********************

	constexpr int32_t TURRET2_BLASTER_DAMAGE = 12;
	constexpr int32_t TURRET2_BULLET_DAMAGE = 6;
	// unused
	// constexpr int32_t turret2_HEAT_DAMAGE	= 4;
	constexpr gtime_t ROCKET_FIRE_INTERVAL = 2.0_sec; // 2.3 segundos

	void turret2Fire(edict_t* self)
	{
		vec3_t forward;
		vec3_t start, end, dir;
		float dist, chance;
		trace_t trace;
		int rocketSpeed = 0;
		bool damageApplied = false;  // Nueva bandera para prevenir daño duplicado

		turret2Aim(self);

		if (!self->enemy || !self->enemy->inuse || OnSameTeam(self, self->enemy) || self->enemy->deadflag)
		{
			if (!FindMTarget(self))
				return;
		}

		// Reducir retrasos o hacer que el comportamiento de disparo sea más agresivo
		self->monsterinfo.attack_finished = level.time;  // Reducir o eliminar el tiempo de espera

		if (self->monsterinfo.aiflags & AI_LOST_SIGHT)
			end = self->monsterinfo.blind_fire_target;
		else
			end = self->enemy->s.origin;

		dir = end - self->s.origin;
		dir.normalize();
		AngleVectors(self->s.angles, forward, nullptr, nullptr);
		chance = dir.dot(forward);

		if (chance < 0.98f)
			return;

		chance = frandom();

		if (self->spawnflags.has(SPAWNFLAG_TURRET2_ROCKET))
			rocketSpeed = 1650;
		else if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
			rocketSpeed = 1800;

		if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN) || visible(self, self->enemy))
		{
			start = self->s.origin;
			// Aim for the head.
			if (!(self->monsterinfo.aiflags & AI_LOST_SIGHT))
			{
				if ((self->enemy) && (self->enemy->client))
					end[2] += self->enemy->viewheight;
				else
					end[2] += 7;
			}

			dir = end - start;
			dist = dir.length();

			// Check for predictive fire
			// Paril: adjusted to be a bit more fair
			if (!(self->monsterinfo.aiflags & AI_LOST_SIGHT))
			{
				// On harder difficulties, randomly fire directly at enemy
				// more often; makes them more unpredictable

					PredictAim(self, self->enemy, start, 0, true, 0.0f, &dir, nullptr);
			}

			dir.normalize();
			trace = gi.traceline(start, end, self, MASK_PROJECTILE);
			if (trace.ent == self->enemy || trace.ent == world)
			{
				// Disparo de cohetes cada 3 segundos
				if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN))
				{
					gtime_t currentTime = level.time;
					gtime_t rocketFireInterval = ROCKET_FIRE_INTERVAL;
					if (currentTime > self->monsterinfo.last_rocket_fire_time + rocketFireInterval)
					{
						self->monsterinfo.last_rocket_fire_time = currentTime;

						if (dist * trace.fraction > 72 && !damageApplied)
						{
							PredictAim(self, self->enemy, start, (float)rocketSpeed, true, (frandom(3.f - skill->integer) / 3.f) - frandom(0.05f * (3.f - skill->integer)), &dir, nullptr);
							fire_rocket(self->owner, start, dir, 100, 1220, 100, 75); // Pasa el mod_t a monster_fire_rocket
							damageApplied = true;
						}
					}
				}

				constexpr gtime_t PLASMA_FIRE_INTERVAL = 4.0_sec; // Por ejemplo, cada 4 segundos
				if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
				{
					start = self->s.origin;
					dir = end - start;
					dir.normalize();
					trace = gi.traceline(start, end, self, MASK_PROJECTILE);
					if (trace.ent == self->enemy || trace.ent == world)
					{
						if (!damageApplied)
						{
							// Sigue disparando heatbeam
							T_Damage(trace.ent, self, self->owner, dir, trace.endpos, trace.plane.normal, TURRET2_BLASTER_DAMAGE, 0, DAMAGE_ENERGY, MOD_TURRET);
							monster_fire_heatbeam(self, start, forward, vec3_origin, 0, 50, MZ2_TURRET_BLASTER);
							damageApplied = true;
						}

						// Disparo de plasma en intervalos
						gtime_t currentTime = level.time;
						if (currentTime > self->monsterinfo.last_plasma_fire_time + PLASMA_FIRE_INTERVAL)
						{
							self->monsterinfo.last_plasma_fire_time = currentTime;
							fire_plasma(self->owner, start, dir, 100, 1220, 100, 75); // Ajusta los parámetros según sea necesario
						}
					}
				}
			

				else if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN))
				{
					if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME))
					{
						self->monsterinfo.aiflags |= AI_HOLD_FRAME;
						self->monsterinfo.duck_wait_time = level.time + 5_sec + gtime_t::from_sec(frandom(skill->value)); // Reduce el tiempo inicial de espera
						self->monsterinfo.next_duck_time = level.time + gtime_t::from_sec(0.1f); // Reduce el tiempo inicial de espera
						gi.sound(self, CHAN_VOICE, gi.soundindex("weapons/chngnu1a.wav"), 1, ATTN_NORM, 0);
					}
					else
					{
						if (self->monsterinfo.next_duck_time < level.time &&
							self->monsterinfo.melee_debounce_time <= level.time && !damageApplied)
						{
							// Aplica el daño con el mod_t configurado

							T_Damage(trace.ent, self, self->owner, dir, trace.endpos, trace.plane.normal, TURRET2_BULLET_DAMAGE, 0, DAMAGE_NONE, MOD_TURRET);
							monster_fire_bullet(self, start, dir, 0, 0, DEFAULT_BULLET_HSPREAD / 1.8, DEFAULT_BULLET_VSPREAD / 2, MZ2_TURRET_MACHINEGUN);
							self->monsterinfo.melee_debounce_time = level.time + 15_hz;	
							damageApplied = true;
						}

						if (self->monsterinfo.duck_wait_time < level.time)
							self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
					}
				}
				else if (self->spawnflags.has(SPAWNFLAG_TURRET2_ROCKET))
				{
					if (dist * trace.fraction > 72 && !damageApplied)
					{
						// Aplica el daño con el mod_t configurado
						T_Damage(trace.ent, self, self->owner, dir, trace.endpos, trace.plane.normal, 70, 0, DAMAGE_DESTROY_ARMOR, MOD_TURRET);
						fire_rocket(self, start, dir, 70, rocketSpeed, MZ2_TURRET_ROCKET, MOD_TURRET); // Pasa el mod_t a monster_fire_rocket
						damageApplied = true;
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

	// **********************
	//  PAIN
	// **********************

	PAIN(turret2_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
	{
	}

	// **********************
	//  DEATH
	// **********************

	DIE(turret2_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
	{
		vec3_t forward;
		edict_t* base;

		if (self->owner && self->owner->client) {
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


	THINK(turret2_timeout) (edict_t* self) -> void
	{
		self->health -= 3;
		self->monsterinfo.power_armor_power -= 5;

		if (self->monsterinfo.power_armor_power <= 10)
			self->health -= 10;
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

		stationarymonster_start(ent);

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

		self->movetype = MOVETYPE_PUSH;
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
		vec3_t spot1, spot2;
		trace_t tr;

		if (self->enemy->health > 0)
		{
			// Verificar si alguna entidad está en el camino del disparo
			spot1 = self->s.origin;
			spot1[2] += self->viewheight;
			spot2 = self->enemy->s.origin;
			spot2[2] += self->enemy->viewheight;

			tr = gi.traceline(spot1, spot2, self, CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_SLIME | CONTENTS_LAVA | CONTENTS_WINDOW);
			// Si el tr.ent no es el enemigo y no está en el mismo equipo
			if (tr.ent != self->enemy && !OnSameTeam(self, tr.ent))
			{
				// PMM - si no podemos ver nuestro objetivo, y no estamos bloqueados por un monstruo, ir a fuego ciego si está disponible
				if ((!visible(self, self->enemy)))
				{
					if ((self->monsterinfo.blindfire) && (self->monsterinfo.blind_fire_delay <= 1.0_sec))
					{
						tr = gi.traceline(spot1, self->monsterinfo.blind_fire_target, self, CONTENTS_MONSTER);
						if (!(tr.allsolid || tr.startsolid || ((tr.fraction < 1.0f) && (tr.ent != self->enemy) && !OnSameTeam(self, tr.ent))))
						{
							self->monsterinfo.attack_state = AS_BLIND;
							self->monsterinfo.attack_finished = level.time + random_time(400_ms, 0.2_sec); // Reduce el tiempo de espera
							return true;
						}
					}
				}
				return false;
			}
		}

		if (level.time < self->monsterinfo.attack_finished)
			return false;

		gtime_t nexttime = (self->spawnflags.has(SPAWNFLAG_TURRET2_ROCKET)) ? (1.0_sec - (0.2_sec * skill->integer)) :
			(self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER)) ? (1.0_sec - (0.2_sec * skill->integer)) :
			(0.6_sec - (0.1_sec * skill->integer)); // Reduce el tiempo de espera

		self->monsterinfo.attack_state = AS_MISSILE;
		self->monsterinfo.attack_finished = level.time + nexttime;
		return true;
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
	void SP_monster_sentrygun(edict_t* self)
	{
#define playeref self->owner->s.effects;
		self->monsterinfo.last_rocket_fire_time = gtime_t::from_sec(0); // Inicializa el tiempo de último disparo de cohete
		int angle;

			self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
			self->monsterinfo.team = CTF_TEAM1;
			self->s.effects = EF_BOB | EF_GRENADE;
			self->monsterinfo.attack_state = AS_BLIND;

			ApplyMonsterBonusFlags(self);

		if (!M_AllowSpawn(self))
		{
			G_FreeEdict(self);
			return;
		}

		// pre-caches
		sound_moved.assign("gunner/gunidle1.wav");
		sound_moving.assign("turret/moving.wav");
		gi.modelindex("models/objects/debris1/tris.md2");

		self->s.modelindex = gi.modelindex("models/monsters/turret/tris.md2");
		self->mins = { -12, -12, -12 };
		self->maxs = { 12, 12, 12 };
		self->movetype = MOVETYPE_NONE;


		//self->think = G_FreeEdict;
		//self->nextthink = level.time + 2_sec;


		self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
		self->monsterinfo.power_armor_power = 125;
		self->health = 90;
		self->gib_health = -100;
		self->mass = 100;
		self->yaw_speed = 15;
		self->solid = SOLID_BBOX;
		self->svflags = SVF_PLAYER;
		self->flags |= FL_MECHANICAL;
		self->pain = turret2_pain;
		self->die = turret2_die;

		// [Paril-KEX]
		if (!G_ShouldPlayersCollide(true)) {
			self->clipmask &= ~CONTENTS_PLAYER;
		}

		// map designer didn't specify weapon type. set it now.
		if (!self->spawnflags.has(SPAWNFLAG_TURRET2_WEAPONCHOICE) && current_wave_number <= 5)
			self->spawnflags |= SPAWNFLAG_TURRET2_MACHINEGUN;
			
		// map designer didn't specify weapon type. set it now.
		else if (!self->spawnflags.has(SPAWNFLAG_TURRET2_WEAPONCHOICE) && current_wave_number >= 6)
			if (brandom())
			self->spawnflags |= SPAWNFLAG_TURRET2_HEATBEAM;
		else
			self->spawnflags |= SPAWNFLAG_TURRET2_MACHINEGUN;


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
			stationarymonster_start(self);
		}

		if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN))
		{
			gi.soundindex("infantry/infatck1.wav");
			gi.soundindex("weapons/chngnu1a.wav");
			gi.soundindex("weapons/rockfly.wav");
			gi.modelindex("models/objects/rocket/tris.md2");
			gi.soundindex("chick/chkatck2.wav");
			self->s.skinnum = 2;

			self->spawnflags &= ~SPAWNFLAG_TURRET2_WEAPONCHOICE;
			self->spawnflags |= SPAWNFLAG_TURRET2_MACHINEGUN;
		}
		else if (self->spawnflags.has(SPAWNFLAG_TURRET2_ROCKET))
		{
			gi.soundindex("weapons/rockfly.wav");
			gi.modelindex("models/objects/rocket/tris.md2");
			gi.soundindex("chick/chkatck2.wav");
			self->s.skinnum = 2;

			self->spawnflags &= ~SPAWNFLAG_TURRET2_WEAPONCHOICE;
			self->spawnflags |= SPAWNFLAG_TURRET2_ROCKET;
		}
		else
		{
			gi.modelindex("models/objects/laser/tris.md2");
			gi.soundindex("misc/lasfly.wav");
			gi.soundindex("soldier/solatck2.wav");

			self->spawnflags &= ~SPAWNFLAG_TURRET2_WEAPONCHOICE;
			self->spawnflags |= SPAWNFLAG_TURRET2_BLASTER;
		}

		self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
		if (self->spawnflags.has(SPAWNFLAG_TURRET2_BLASTER))
			self->yaw_speed = 12;
		if (self->spawnflags.has(SPAWNFLAG_TURRET2_MACHINEGUN | SPAWNFLAG_TURRET2_BLASTER))
			self->monsterinfo.blindfire = true;

	}