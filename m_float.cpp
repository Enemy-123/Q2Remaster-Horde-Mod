// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

floater_tracker

==============================================================================
*/

#include "g_local.h"
#include "m_float.h"
#include "m_flash.h"
#include "shared.h"

static cached_soundindex sound_attack2;
static cached_soundindex sound_attack3;
static cached_soundindex sound_death1;
static cached_soundindex sound_idle;
static cached_soundindex sound_pain1;
static cached_soundindex sound_pain2;
static cached_soundindex sound_sight;
static cached_soundindex sound_tracker;

MONSTERINFO_SIGHT(floater_tracker_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_IDLE(floater_tracker_idle) (edict_t* self) -> void
{
	gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

void floater_tracker_dead(edict_t* self);
void floater_tracker_run(edict_t* self);
void floater_tracker_wham(edict_t* self);
void floater_tracker_zap(edict_t* self);

void floater_tracker_fire_blaster(edict_t* self)
{
	vec3_t start;
	vec3_t dir;
	float len;

	// Definimos el offset personalizado
	constexpr vec3_t custom_offset = { 32.5f, -0.8f, 10.f };

	// Obtenemos los vectores usando la estructura
	angle_vectors_t const vectors = AngleVectors(self->s.angles);

	// Usamos G_ProjectSourceWithOffset con vec3_origin
	start = G_ProjectSourceWithOffset(self->s.origin, custom_offset, vectors.forward, vectors.right, vectors.up, vec3_origin);

	dir = self->pos1 - self->enemy->s.origin;
	len = dir.length();

	if (len < 30)
	{
		// calc direction to where we targeted
		dir = self->pos1 - start;
		dir.normalize();
		monster_fire_tracker(self, start, dir, 13, 950, self->enemy, MZ2_UNUSED_0);
	}
	else
	{
		PredictAim(self, self->enemy, start, 1200, true, 0, &dir, nullptr);
		monster_fire_tracker(self, start, dir, 13, 860, nullptr, MZ2_UNUSED_0);
	}
}
mframe_t floater_tracker_frames_stand1[] = {
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
MMOVE_T(floater_tracker_move_stand1) = { FRAME_stand101, FRAME_stand152, floater_tracker_frames_stand1, nullptr };

mframe_t floater_tracker_frames_stand2[] = {
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
MMOVE_T(floater_tracker_move_stand2) = { FRAME_stand201, FRAME_stand252, floater_tracker_frames_stand2, nullptr };

mframe_t floater_tracker_frames_pop[] = {
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{}
};
MMOVE_T(floater_tracker_move_pop) = { FRAME_actvat05, FRAME_actvat31, floater_tracker_frames_pop, floater_tracker_run };

mframe_t floater_tracker_frames_disguise[] = {
	{ ai_stand }
};
MMOVE_T(floater_tracker_move_disguise) = { FRAME_actvat01, FRAME_actvat01, floater_tracker_frames_disguise, nullptr };

MONSTERINFO_STAND(floater_tracker_stand) (edict_t* self) -> void
{
	if (self->monsterinfo.active_move == &floater_tracker_move_disguise)
		M_SetAnimation(self, &floater_tracker_move_disguise);
	else if (frandom() <= 0.5f)
		M_SetAnimation(self, &floater_tracker_move_stand1);
	else
		M_SetAnimation(self, &floater_tracker_move_stand2);
}

mframe_t floater_tracker_frames_attack1[] = {
	{ ai_charge }, // Blaster attack
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, floater_tracker_fire_blaster }, // BOOM (0, -25.8, 32.5)	-- LOOP Starts
	{ ai_charge, 0, floater_tracker_fire_blaster },
	{ ai_charge, 0, floater_tracker_fire_blaster },
	{ ai_charge, 0, floater_tracker_fire_blaster },
	{ ai_charge, 0, floater_tracker_fire_blaster },
	{ ai_charge, 0, floater_tracker_fire_blaster },
	{ ai_charge, 0, floater_tracker_fire_blaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge } //							-- LOOP Ends
};
MMOVE_T(floater_tracker_move_attack1) = { FRAME_attak101, FRAME_attak114, floater_tracker_frames_attack1, floater_tracker_run };

// PMM - circle strafe frames
mframe_t floater_tracker_frames_attack1a[] = {
	{ ai_charge, 10 }, // Blaster attack
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10, floater_tracker_fire_blaster }, // BOOM (0, -25.8, 32.5)	-- LOOP Starts
	{ ai_charge, 10, floater_tracker_fire_blaster },
	{ ai_charge, 10, floater_tracker_fire_blaster },
	{ ai_charge, 10, floater_tracker_fire_blaster },
	{ ai_charge, 10, floater_tracker_fire_blaster },
	{ ai_charge, 10, floater_tracker_fire_blaster },
	{ ai_charge, 10, floater_tracker_fire_blaster },
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10 } //							-- LOOP Ends
};
MMOVE_T(floater_tracker_move_attack1a) = { FRAME_attak101, FRAME_attak114, floater_tracker_frames_attack1a, floater_tracker_run };
// pmm

mframe_t floater_tracker_frames_attack2[] = {
	{ ai_charge }, // Claws
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, floater_tracker_wham }, // WHAM (0, -45, 29.6)		-- LOOP Starts
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, //							-- LOOP Ends
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(floater_tracker_move_attack2) = { FRAME_attak201, FRAME_attak225, floater_tracker_frames_attack2, floater_tracker_run };

mframe_t floater_tracker_frames_attack3[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, floater_tracker_zap }, //								-- LOOP Starts
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, //								-- LOOP Ends
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
MMOVE_T(floater_tracker_move_attack3) = { FRAME_attak301, FRAME_attak334, floater_tracker_frames_attack3, floater_tracker_run };

#if 0
mframe_t floater_tracker_frames_death[] = {
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
MMOVE_T(floater_tracker_move_death) = { FRAME_death01, FRAME_death13, floater_tracker_frames_death, floater_tracker_dead };
#endif

mframe_t floater_tracker_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(floater_tracker_move_pain1) = { FRAME_pain101, FRAME_pain107, floater_tracker_frames_pain1, floater_tracker_run };

mframe_t floater_tracker_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(floater_tracker_move_pain2) = { FRAME_pain201, FRAME_pain208, floater_tracker_frames_pain2, floater_tracker_run };

#if 0
mframe_t floater_tracker_frames_pain3[] = {
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
MMOVE_T(floater_tracker_move_pain3) = { FRAME_pain301, FRAME_pain312, floater_tracker_frames_pain3, floater_tracker_run };
#endif

mframe_t floater_tracker_frames_walk[] = {
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 }
};
MMOVE_T(floater_tracker_move_walk) = { FRAME_stand101, FRAME_stand152, floater_tracker_frames_walk, nullptr };

mframe_t floater_tracker_frames_run[] = {
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 }
};
MMOVE_T(floater_tracker_move_run) = { FRAME_stand101, FRAME_stand152, floater_tracker_frames_run, nullptr };

MONSTERINFO_RUN(floater_tracker_run) (edict_t* self) -> void
{
	if (self->monsterinfo.active_move == &floater_tracker_move_disguise)
		M_SetAnimation(self, &floater_tracker_move_pop);
	else if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &floater_tracker_move_stand1);
	else
		M_SetAnimation(self, &floater_tracker_move_run);
}

MONSTERINFO_WALK(floater_tracker_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &floater_tracker_move_walk);
}

void floater_tracker_wham(edict_t* self)
{
	constexpr vec3_t aim = { MELEE_DISTANCE, 0, 0 };
	gi.sound(self, CHAN_WEAPON, sound_attack3, 1, ATTN_NORM, 0);

	// Verificar si self->enemy está correctamente inicializado
	if (self->enemy) {
		if (!fire_hit(self, aim, irandom(5, 11), -50))
			self->monsterinfo.melee_debounce_time = level.time + 3_sec;
	}
	else {
		//char buffer[256];
		//std::snprintf(buffer, sizeof(buffer), "floater_wham: Error: enemy not properly initialized\n");
		//gi.Com_Print(buffer);

		// Manejar el caso donde self->enemy no está inicializado
		self->monsterinfo.melee_debounce_time = level.time + 3_sec; // Ajustar según sea necesario
	}
}


void floater_tracker_zap(edict_t* self)
{
	vec3_t forward, right;
	vec3_t origin;
	vec3_t dir;
	vec3_t offset;

	dir = self->enemy->s.origin - self->s.origin;

	AngleVectors(self->s.angles, forward, right, nullptr);
	// FIXME use a flash and replace these two lines with the commented one
	offset = { 18.5f, -0.9f, 10 };
	origin = M_ProjectFlashSource(self, offset, forward, right);

	gi.sound(self, CHAN_WEAPON, sound_attack2, 1, ATTN_NORM, 0);

	// FIXME use the flash, Luke
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_SPLASH);
	gi.WriteByte(32);
	gi.WritePosition(origin);
	gi.WriteDir(dir);
	gi.WriteByte(SPLASH_SPARKS);
	gi.multicast(origin, MULTICAST_PVS, false);

	T_Damage(self->enemy, self, self, dir, self->enemy->s.origin, vec3_origin, irandom(5, 11), -10, DAMAGE_ENERGY, MOD_UNKNOWN);
}

MONSTERINFO_ATTACK(floater_tracker_attack) (edict_t* self) -> void
{
	float const chance = 0.5f;

	if (frandom() > chance)
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		M_SetAnimation(self, &floater_tracker_move_attack1);
	}
	else // circle strafe
	{
		if (frandom() <= 0.5f) // switch directions
			self->monsterinfo.lefty = !self->monsterinfo.lefty;
		self->monsterinfo.attack_state = AS_SLIDING;
		M_SetAnimation(self, &floater_tracker_move_attack1a);
	}

	gi.sound(self, CHAN_WEAPON, sound_tracker, 1, ATTN_NORM, 0);
}

MONSTERINFO_MELEE(floater_tracker_melee) (edict_t* self) -> void
{
	if (frandom() < 0.5f)
		M_SetAnimation(self, &floater_tracker_move_attack3);
	else
		M_SetAnimation(self, &floater_tracker_move_attack2);
}

PAIN(floater_tracker_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	int n;

	if (level.time < self->pain_debounce_time)
		return;

	// no pain anims if poppin'
	if (self->monsterinfo.active_move == &floater_tracker_move_disguise ||
		self->monsterinfo.active_move == &floater_tracker_move_pop)
		return;

	n = irandom(3);
	if (n == 0)
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	self->pain_debounce_time = level.time + 3_sec;

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	if (n == 0)
		M_SetAnimation(self, &floater_tracker_move_pain1);
	else
		M_SetAnimation(self, &floater_tracker_move_pain2);
}

MONSTERINFO_SETSKIN(floater_tracker_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

void floater_tracker_dead(edict_t* self)
{
	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, -8 };
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0_ms;
	gi.linkentity(self);
}

DIE(floater_tracker_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	gi.sound(self, CHAN_VOICE, sound_death1, 1, ATTN_NORM, 0);

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_EXPLOSION1);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PHS, false);

	self->s.skinnum /= 2;

	ThrowGibs(self, 55, {
		{ 2, "models/objects/gibs/sm_metal/tris.md2" },
		{ 3, "models/objects/gibs/sm_meat/tris.md2" },
		{ "models/monsters/float/gibs/piece.md2", GIB_SKINNED },
		{ "models/monsters/float/gibs/gun.md2", GIB_SKINNED },
		{ "models/monsters/float/gibs/base.md2", GIB_SKINNED },
		{ "models/monsters/float/gibs/jar.md2", GIB_SKINNED | GIB_HEAD }
		});

	if (frandom() < 0.5f)
		SpawnClusterGrenades(self, self->s.origin, 125);
}

static void float_set_fly_parameters(edict_t* self)
{
	self->monsterinfo.fly_thrusters = false;
	self->monsterinfo.fly_acceleration = 12.f;
	self->monsterinfo.fly_speed = 200.f;
	// Technician gets in closer because he has two melee attacks
	self->monsterinfo.fly_min_distance = 20.f;
	self->monsterinfo.fly_max_distance = 280.f;
}

constexpr spawnflags_t SPAWNFLAG_floater_tracker_DISGUISE = 8_spawnflag;

/*QUAKED monster_floater_tracker (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight Disguise
 */
void SP_monster_floater_tracker(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (g_horde->integer && current_wave_level <= 18)
	{
		const float randomsearch = frandom(); // Generar un número aleatorio entre 0 y 1

		if (randomsearch < 0.12f)
			gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_NORM, 0);
	}

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	sound_attack2.assign("floater/fltatck2.wav");
	sound_attack3.assign("floater/fltatck3.wav");
	sound_death1.assign("floater/fltdeth1.wav");
	sound_idle.assign("floater/fltidle1.wav");
	sound_pain1.assign("floater/fltpain1.wav");
	sound_pain2.assign("floater/fltpain2.wav");
	sound_sight.assign("floater/fltsght1.wav");
	sound_sight.assign("floater/fltsght1.wav");
	sound_tracker.assign("weapons/disrupt.wav");

	gi.soundindex("floater/fltatck1.wav");

	self->monsterinfo.engine_sound = gi.soundindex("floater/fltsrch1.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/float/tris.md2");

	gi.modelindex("models/monsters/float/gibs/base.md2");
	gi.modelindex("models/monsters/float/gibs/gun.md2");
	gi.modelindex("models/monsters/float/gibs/jar.md2");
	gi.modelindex("models/monsters/float/gibs/piece.md2");

	self->mins = { -24, -24, -24 };
	self->maxs = { 24, 24, 48 };

	if (!st.was_key_specified("power_armor_type"))
		self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
	if (!st.was_key_specified("power_armor_power"))
		self->monsterinfo.power_armor_power = 180;

	self->health = 230 * st.health_multiplier;
	self->s.effects = EF_BARREL_EXPLODING;
	self->gib_health = -80;
	self->mass = 300;
	//self->s.scale = 1.2f;
	self->pain = floater_tracker_pain;
	self->die = floater_tracker_die;

	self->monsterinfo.stand = floater_tracker_stand;
	self->monsterinfo.walk = floater_tracker_walk;
	self->monsterinfo.run = floater_tracker_run;
	self->monsterinfo.attack = floater_tracker_attack;
	self->monsterinfo.melee = floater_tracker_melee;
	self->monsterinfo.sight = floater_tracker_sight;
	self->monsterinfo.idle = floater_tracker_idle;
	self->monsterinfo.setskin = floater_tracker_setskin;

	gi.linkentity(self);

	if (self->spawnflags.has(SPAWNFLAG_floater_tracker_DISGUISE))
		M_SetAnimation(self, &floater_tracker_move_disguise);
	else if (frandom() <= 0.5f)
		M_SetAnimation(self, &floater_tracker_move_stand1);
	else
		M_SetAnimation(self, &floater_tracker_move_stand2);

	self->monsterinfo.scale = MODEL_SCALE;

	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	float_set_fly_parameters(self);

	flymonster_start(self);

	ApplyMonsterBonusFlags(self);
}
