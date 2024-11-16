// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

GUARDIAN

==============================================================================
*/

#include "g_local.h"
#include "m_guardian.h"
#include "m_flash.h"
#include "shared.h"

constexpr spawnflags_t SPAWNFLAG_GUARDIAN_JANITOR = 8_spawnflag;
//
// stand
//

mframe_t guardian_frames_stand[] = {
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
MMOVE_T(guardian_move_stand) = { FRAME_idle1, FRAME_idle52, guardian_frames_stand, nullptr };

MONSTERINFO_STAND(guardian_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &guardian_move_stand);
}

//
// walk
//

static cached_soundindex sound_step;

void guardian_footstep(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_step, 1.f, ATTN_NORM, 0.0f);
}

mframe_t guardian_frames_walk[] = {
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8, guardian_footstep },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8, guardian_footstep },
	{ ai_walk, 8 }
};
MMOVE_T(guardian_move_walk) = { FRAME_walk1, FRAME_walk19, guardian_frames_walk, nullptr };

MONSTERINFO_WALK(guardian_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &guardian_move_walk);
}

//
// run
//

mframe_t guardian_frames_run[] = {
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8, guardian_footstep },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8, guardian_footstep },
	{ ai_run, 8 }
};
MMOVE_T(guardian_move_run) = { FRAME_walk1, FRAME_walk19, guardian_frames_run, nullptr };

MONSTERINFO_RUN(guardian_run) (edict_t* self) -> void
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		M_SetAnimation(self, &guardian_move_stand);
		return;
	}

	M_SetAnimation(self, &guardian_move_run);
}

//
// pain
//

mframe_t guardian_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(guardian_move_pain1) = { FRAME_pain1_1, FRAME_pain1_8, guardian_frames_pain1, guardian_run };

PAIN(guardian_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (mod.id != MOD_CHAINFIST && damage <= 10)
		return;

	if (level.time < self->pain_debounce_time)
		return;

	if (mod.id != MOD_CHAINFIST && damage <= 75)
		if (frandom() > 0.2f)
			return;

	// don't go into pain while attacking
	if ((self->s.frame >= FRAME_atk1_spin1) && (self->s.frame <= FRAME_atk1_spin15))
		return;
	if ((self->s.frame >= FRAME_atk2_fire1) && (self->s.frame <= FRAME_atk2_fire4))
		return;
	if ((self->s.frame >= FRAME_kick_in1) && (self->s.frame <= FRAME_kick_in13))
		return;

	self->pain_debounce_time = level.time + 3_sec;
	//gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	M_SetAnimation(self, &guardian_move_pain1);
	self->monsterinfo.weapon_sound = 0;
}

mframe_t guardian_frames_atk1_out[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(guardian_atk1_out) = { FRAME_atk1_out1, FRAME_atk1_out3, guardian_frames_atk1_out, guardian_run };

void guardian_atk1_finish(edict_t* self)
{
	M_SetAnimation(self, &guardian_atk1_out);
	self->monsterinfo.weapon_sound = 0;
}

static cached_soundindex sound_charge;
static cached_soundindex sound_spin_loop;

void guardian_atk1_charge(edict_t* self)
{
	self->monsterinfo.weapon_sound = sound_spin_loop;

	if (!strcmp(self->classname, "monster_guardian"))
	gi.sound(self, CHAN_WEAPON, sound_charge, 1.f, ATTN_NORM, 0.f);
}

void guardian_fire_blaster(edict_t* self)
{
	vec3_t forward, right, target;
	vec3_t start;
	monster_muzzleflash_id_t id;
	AngleVectors(self->s.angles, forward, right, nullptr);

	if (!strcmp(self->classname, "monster_janitor2")) {
		id = static_cast<monster_muzzleflash_id_t>(MZ2_SOLDIER_RIPPER_1);
		// Aplicar el offset escalado
		vec3_t offset = { 88.f * self->s.scale, 50.f * self->s.scale, 60.f * self->s.scale };
		start = self->s.origin + (forward * offset[0]) + (right * offset[1]);
		start.z += offset[2];
	}
	else {
		id = MZ2_GUARDIAN_BLASTER;
		start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);
	}

	target = self->enemy->s.origin;
	target[2] += self->enemy->viewheight;
	for (int i = 0; i < 3; i++)
		target[i] += crandom_open() * 5.f;
	forward = target - start;
	forward.normalize();

	if (!strcmp(self->classname, "monster_guardian"))
	{
		monster_fire_blaster(self, start, forward, 18, 1800, id, (self->s.frame % 4) ? EF_QUAD : EF_HYPERBLASTER);
	}
	else if (!strcmp(self->classname, "monster_janitor2"))
	{
		// Usar Ionripper para janitor2
		monster_fire_ionripper(self, start, forward, 25, 1600, id, EF_IONRIPPER);
	}

	if (self->enemy && self->enemy->health > 0 &&
		self->s.frame == FRAME_atk1_spin12 && self->timestamp > level.time && visible(self, self->enemy))
		self->monsterinfo.nextframe = FRAME_atk1_spin5;
}

mframe_t guardian_frames_atk1_spin[] = {
	{ ai_charge, 0, guardian_atk1_charge },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
	{ ai_charge, 0, guardian_fire_blaster },
};
MMOVE_T(guardian_move_atk1_spin) = { FRAME_atk1_spin1, FRAME_atk1_spin15, guardian_frames_atk1_spin, guardian_atk1_finish };

void guardian_atk1(edict_t* self)
{
	M_SetAnimation(self, &guardian_move_atk1_spin);
	self->timestamp = level.time + 650_ms + random_time(1.5_sec);
}

mframe_t guardian_frames_atk1_in[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(guardian_move_atk1_in) = { FRAME_atk1_in1, FRAME_atk1_in3, guardian_frames_atk1_in, guardian_atk1 };

mframe_t guardian_frames_atk2_out[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, guardian_footstep },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(guardian_move_atk2_out) = { FRAME_atk2_out1, FRAME_atk2_out7, guardian_frames_atk2_out, guardian_run };

void guardian_atk2_out(edict_t* self)
{
	M_SetAnimation(self, &guardian_move_atk2_out);
}

static cached_soundindex sound_laser;

constexpr vec3_t laser_positions[] = {
	{ 125.0f, -70.f, 60.f },
	{ 112.0f, -62.f, 60.f }
};

PRETHINK(guardian_fire_update) (edict_t* laser) -> void
{
	edict_t* self = laser->owner;
	vec3_t forward, right, target;
	vec3_t start;

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, laser_positions[1 - (self->s.frame & 1)], forward, right);

	if (self->enemy) {
		target = self->enemy->s.origin + self->enemy->mins;
		for (int i = 0; i < 3; i++)
			target[i] += frandom() * self->enemy->size[i];
	}
	else {
		// Default target if there's no enemy
		target = start + forward * 100; // Adjust the distance as needed
	}

	forward = target - start;
	forward.normalize();
	laser->s.origin = start;
	laser->movedir = forward;
	gi.linkentity(laser);
	dabeam_update(laser, false);
}

constexpr float GRENADE_SPEED = 1200;

// Función para lanzar granadas alternando entre dos posiciones
static void guardian_grenade(edict_t* self)
{
	vec3_t start{};
	vec3_t forward{}, right{}, up{};
	vec3_t aim{};
	const monster_muzzleflash_id_t flash_number = MZ2_GUNNER_GRENADE2_4;
	const float speed = GRENADE_SPEED;

	if (!self->enemy || !self->enemy->inuse)
		return;

	AngleVectors(self->s.angles, forward, right, up);

	// Seleccionar la posición de la granada basada en el contador
	int pos_index = self->count % 2; // Alterna entre 0 y 1
	start = M_ProjectFlashSource(self, laser_positions[1 - (self->s.frame & 1)], forward, right);

	// Incrementar el contador para la próxima granada
	self->count++;

	// Predecir la posición del objetivo
	float time_to_target = (self->enemy->s.origin - start).length() / speed;
	vec3_t predicted_pos = self->enemy->s.origin + (self->enemy->velocity * time_to_target);

	aim = predicted_pos - start;
	const float dist = aim.length();

	// Ajustar la puntería basada en la distancia
	if (dist > 200)
	{
		float vertical_adjust = (dist - 200) * 0.0010f;
		aim[2] += vertical_adjust;
	}

	// Reducir la dispersión aleatoria
	aim[0] += crandom_open() * 0.03f;
	aim[1] += crandom_open() * 0.03f;
	aim[2] += crandom_open() * 0.03f;
	aim.normalize();

	// Ajustar el pitch ligeramente hacia abajo
	const float pitch_adjust = -0.15f - (dist * 0.00015f);
	aim += up * pitch_adjust;
	aim.normalize();

	// Calcular el mejor pitch para disparar
	if (M_CalculatePitchToFire(self, predicted_pos, start, aim, speed, 1.5f, false))
	{
		aim[2] += crandom_open() * 0.01f;
		aim.normalize();
	}

	// Compensar la velocidad ascendente en fire_grenade2
	float gravityAdjustment = level.gravity / 800.f;
	float downwardAdjustment = -200.0f * gravityAdjustment / speed;
	aim[2] += downwardAdjustment;
	aim.normalize();

	// Disparar la granada
	fire_grenade2(self, start, aim, 40, speed, 2.5_sec, 80, false);
	gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/hgrent1a.wav"), 1, ATTN_NORM, 0);
}

void guardian_laser_fire(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_laser, 1.f, ATTN_NORM, 0.f);
	monster_fire_dabeam(self, self->monsterinfo.power_armor_power = !strcmp(self->classname, "monster_guardian") ? 25 : 5, self->s.frame & 1, guardian_fire_update);
}

// Nueva función para manejar ataques basados en el tipo de entidad
void guardian_fire_attack(edict_t* self)
{
	if (!self->enemy || !self->enemy->inuse)
		return;

	if (strcmp(self->classname, "monster_janitor2") == 0) {
		// Ataque con granadas para janitor2
		guardian_grenade(self);
	}
	else if (strcmp(self->classname, "monster_guardian") == 0) {
		// Ataque con láser para guardian
		guardian_laser_fire(self);
	}
}


// Actualización de los frames de atk2_fire para utilizar guardian_fire_attack
mframe_t guardian_frames_atk2_fire[] = {
	{ ai_charge, 0, guardian_fire_attack },
	{ ai_charge, 0, guardian_fire_attack },
	{ ai_charge, 0, guardian_fire_attack },
	{ ai_charge, 0, guardian_fire_attack }
};
MMOVE_T(guardian_move_atk2_fire) = { FRAME_atk2_fire1, FRAME_atk2_fire4, guardian_frames_atk2_fire, guardian_atk2_out };

void guardian_atk2(edict_t* self)
{
	M_SetAnimation(self, &guardian_move_atk2_fire);
}

mframe_t guardian_frames_atk2_in[] = {
	{ ai_charge, 0, guardian_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, guardian_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, guardian_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(guardian_move_atk2_in) = { FRAME_atk2_in1, FRAME_atk2_in12, guardian_frames_atk2_in, guardian_atk2 };

void guardian_kick(edict_t* self)
{
	// Verificar si self->enemy está correctamente inicializado
	if (self->enemy) {
		if (!fire_hit(self, { MELEE_DISTANCE, 0, -80 }, !strcmp(self->classname, "monster_guardian") ? 85 : 30, 700))
			self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
	}
	else {
		//char buffer[256];
		//std::snprintf(buffer, sizeof(buffer), "guardian_kick: Error: enemy not properly initialized\n");
		//gi.Com_Print(buffer);

		// Manejar el caso donde self->enemy no está inicializado
		self->monsterinfo.melee_debounce_time = level.time + 1000_ms; // Ajustar según sea necesario
	}
}

mframe_t guardian_frames_kick[] = {
	{ ai_charge },
	{ ai_charge, 0, guardian_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, guardian_kick },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, guardian_footstep },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(guardian_move_kick) = { FRAME_kick_in1, FRAME_kick_in13, guardian_frames_kick, guardian_run };

MONSTERINFO_ATTACK(guardian_attack) (edict_t* self) -> void
{
	if (!self->enemy || !self->enemy->inuse)
		return;

	const float r = range_to(self, self->enemy);

	if (r > RANGE_NEAR)
		M_SetAnimation(self, &guardian_move_atk2_in);
	else if (
		(self->monsterinfo.melee_debounce_time < level.time && r < 120.f && !strcmp(self->classname, "monster_guardian")) ||
		(self->monsterinfo.melee_debounce_time < level.time && r < 120.f && !strcmp(self->classname, "monster_janitor2") && r <= RANGE_MELEE)
		)
		M_SetAnimation(self, &guardian_move_kick);
	else
		M_SetAnimation(self, &guardian_move_atk1_in);
}

//
// death
//

void guardian_explode(edict_t* self)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_EXPLOSION1_BIG);
	gi.WritePosition((self->s.origin + self->mins) + vec3_t{ frandom() * self->size[0], frandom() * self->size[1], frandom() * self->size[2] });
	gi.multicast(self->s.origin, MULTICAST_ALL, false);
}

constexpr const char* gibs[] = {
	"models/monsters/guardian/gib1.md2",
	"models/monsters/guardian/gib2.md2",
	"models/monsters/guardian/gib3.md2",
	"models/monsters/guardian/gib4.md2",
	"models/monsters/guardian/gib5.md2",
	"models/monsters/guardian/gib6.md2",
	"models/monsters/guardian/gib7.md2"
};

void guardian_dead(edict_t* self)
{
	for (int i = 0; i < 3; i++)
		guardian_explode(self);

	ThrowGibs(self, 125, {
		{ 2, "models/objects/gibs/sm_meat/tris.md2" },
		{ 4, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
		{ 2, gibs[0], GIB_METALLIC },
		{ 2, gibs[1], GIB_METALLIC },
		{ 2, gibs[2], GIB_METALLIC },
		{ 2, gibs[3], GIB_METALLIC },
		{ 2, gibs[4], GIB_METALLIC },
		{ 2, gibs[5], GIB_METALLIC },
		{ gibs[6], GIB_METALLIC | GIB_HEAD }
		});
}

mframe_t guardian_frames_death1boss[FRAME_death26 - FRAME_death1 + 1] = {
	{ ai_move, 0, BossExplode },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(guardian_move_deathboss) = { FRAME_death1, FRAME_death26, guardian_frames_death1boss, guardian_dead };

mframe_t guardian_frames_death1[FRAME_death11 - FRAME_death1 + 1] = {
	{ ai_move, 0, BossExplode },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(guardian_move_death) = { FRAME_death1, FRAME_death11, guardian_frames_death1, guardian_dead };

DIE(guardian_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	OnEntityDeath(self);
	// regular death
	//gi.sound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	self->monsterinfo.weapon_sound = 0;
	self->deadflag = true;
	self->takedamage = false;

	if (!strcmp(self->classname, "monster_guardian") || strcmp(self->classname, "monster_janitor2")) {
		M_SetAnimation(self, &guardian_move_deathboss);
	}
	else {
		M_SetAnimation(self, &guardian_move_death);
	}
}

//
// monster_tank
//

/*QUAKED monster_guardian (1 .5 0) (-96 -96 -66) (96 96 62) Ambush Trigger_Spawn Sight
 */
void SP_monster_guardian(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	sound_step.assign("zortemp/step.wav");
	sound_charge.assign("weapons/hyprbu1a.wav");
	sound_spin_loop.assign("weapons/hyprbl1a.wav");
	sound_laser.assign("weapons/laser2.wav");

	for (auto& gib : gibs)
		gi.modelindex(gib);

	self->s.modelindex = gi.modelindex("models/monsters/guardian/tris.md2");
	self->mins = { -96, -96, -66 };
	self->maxs = { 96, 96, 62 };
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	self->health = 6500 + (1.08 * current_wave_level);
	self->gib_health = -200;

	// Inicializar el contador para alternar posiciones de granadas
	self->count = 0;

	if (!st.was_key_specified("power_armor_type"))
		self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
	if (!st.was_key_specified("power_armor_power"))
		self->monsterinfo.power_armor_power = !strcmp(self->classname, "monster_guardian") ? 550 : 385;

	self->monsterinfo.scale = MODEL_SCALE;

	self->mass = 850;

	self->pain = guardian_pain;
	self->die = guardian_die;
	self->monsterinfo.stand = guardian_stand;
	self->monsterinfo.walk = guardian_walk;
	self->monsterinfo.run = guardian_run;
	self->monsterinfo.attack = guardian_attack;

	gi.linkentity(self);

	M_SetAnimation(self, &guardian_move_stand);

	walkmonster_start(self);

	ApplyMonsterBonusFlags(self);
}

void SP_monster_janitor2(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	self->spawnflags |= SPAWNFLAG_GUARDIAN_JANITOR;
	SP_monster_guardian(self);
	self->s.skinnum = 2;
	if (!self->s.scale)
		self->s.scale = 0.4f;
	self->health = 600 * st.health_multiplier;

	self->mins = { -18, -18, -24 };
	self->maxs = { 18, 18, 30 };

	ApplyMonsterBonusFlags(self);
}