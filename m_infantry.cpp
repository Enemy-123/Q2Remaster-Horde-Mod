// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

INFANTRY BLASTER2

==============================================================================
*/

#include "g_local.h"
#include "m_infantry.h"
#include "m_flash.h"
#include "shared.h"

void InfantryMachineGun(edict_t* self);

static cached_soundindex sound_pain1;
static cached_soundindex sound_pain2;
static cached_soundindex sound_die1;
static cached_soundindex sound_die2;

static cached_soundindex sound_gunshot;
static cached_soundindex sound_weapon_cock;
static cached_soundindex sound_punch_swing;
static cached_soundindex sound_punch_hit;
static cached_soundindex sound_sight;
static cached_soundindex sound_search;
static cached_soundindex sound_idle;
static cached_soundindex sound_handgrenade;

// range at which we'll try to initiate a run-attack to close distance
constexpr float RANGE_RUN_ATTACK = RANGE_NEAR * 0.75f;

mframe_t infantry_frames_stand[] = {
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
MMOVE_T(infantry_move_stand) = { FRAME_stand50, FRAME_stand71, infantry_frames_stand, nullptr };

MONSTERINFO_STAND(infantry_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &infantry_move_stand);
}

mframe_t infantry_frames_fidget[] = {
	{ ai_stand, 1 },
	{ ai_stand },
	{ ai_stand, 1 },
	{ ai_stand, 3 },
	{ ai_stand, 6 },
	{ ai_stand, 3, monster_footstep },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 1 },
	{ ai_stand },
	{ ai_stand, -1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 1 },
	{ ai_stand },
	{ ai_stand, -2 },
	{ ai_stand, 1 },
	{ ai_stand, 1 },
	{ ai_stand, 1 },
	{ ai_stand, -1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, -1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, -1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, -1 },
	{ ai_stand, -1 },
	{ ai_stand },
	{ ai_stand, -3 },
	{ ai_stand, -2 },
	{ ai_stand, -3 },
	{ ai_stand, -3, monster_footstep },
	{ ai_stand, -2 }
};
MMOVE_T(infantry_move_fidget) = { FRAME_stand01, FRAME_stand49, infantry_frames_fidget, infantry_stand };

MONSTERINFO_IDLE(infantry_fidget) (edict_t* self) -> void
{
	if (self->enemy)
		return;

	M_SetAnimation(self, &infantry_move_fidget);
	gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

mframe_t infantry_frames_walk[] = {
	{ ai_walk, 5, monster_footstep },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 6, monster_footstep },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 5 }
};
MMOVE_T(infantry_move_walk) = { FRAME_walk03, FRAME_walk14, infantry_frames_walk, nullptr };

MONSTERINFO_WALK(infantry_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &infantry_move_walk);
}

mframe_t infantry_frames_run[] = {
	{ ai_run, 10 },
	{ ai_run, 15, monster_footstep },
	{ ai_run, 5 },
	{ ai_run, 7, monster_done_dodge },
	{ ai_run, 18 },
	{ ai_run, 20, monster_footstep },
	{ ai_run, 2 },
	{ ai_run, 6 }
};
MMOVE_T(infantry_move_run) = { FRAME_run01, FRAME_run08, infantry_frames_run, nullptr };

MONSTERINFO_RUN(infantry_run) (edict_t* self) -> void
{
	monster_done_dodge(self);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &infantry_move_stand);
	else
		M_SetAnimation(self, &infantry_move_run);
}

mframe_t infantry_frames_pain1[] = {
	{ ai_move, -3 },
	{ ai_move, -2 },
	{ ai_move, -1 },
	{ ai_move, -2 },
	{ ai_move, -1, monster_footstep },
	{ ai_move, 1 },
	{ ai_move, -1 },
	{ ai_move, 1 },
	{ ai_move, 6 },
	{ ai_move, 2, monster_footstep }
};
MMOVE_T(infantry_move_pain1) = { FRAME_pain101, FRAME_pain110, infantry_frames_pain1, infantry_run };

mframe_t infantry_frames_pain2[] = {
	{ ai_move, -3 },
	{ ai_move, -3 },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, -2, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 5 },
	{ ai_move, 2, monster_footstep }
};
MMOVE_T(infantry_move_pain2) = { FRAME_pain201, FRAME_pain210, infantry_frames_pain2, infantry_run };

extern const mmove_t infantry_move_jump;
extern const mmove_t infantry_move_jump2;

PAIN(infantry_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	int n;

	// allow turret to pain
	if ((self->monsterinfo.active_move == &infantry_move_jump ||
		self->monsterinfo.active_move == &infantry_move_jump2) && self->think == monster_think)
		return;

	if ((self->s.frame >= FRAME_attak201) && (self->s.frame <= FRAME_attak208)) //grenade attack
		return;

	monster_done_dodge(self);

	if (level.time < self->pain_debounce_time)
	{
		if (self->think == monster_think && frandom() < 0.33f)
			self->monsterinfo.dodge(self, other, FRAME_TIME_S, nullptr, false);

		return;
	}

	self->pain_debounce_time = level.time + 3_sec;

	n = brandom();

	if (n == 0)
		gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	if (self->think != monster_think)
		return;

	if (!M_ShouldReactToPain(self, mod))
	{
		if (self->think == monster_think && frandom() < 0.33f)
			self->monsterinfo.dodge(self, other, FRAME_TIME_S, nullptr, false);

		return; // no pain anims in nightmare
	}

	if (n == 0)
		M_SetAnimation(self, &infantry_move_pain1);
	else
		M_SetAnimation(self, &infantry_move_pain2);

	// PMM - clear duck flag
	if (self->monsterinfo.aiflags & AI_DUCKED)
		monster_duck_up(self);
}

MONSTERINFO_SETSKIN(infantry_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
	//		self->s.skinnum = gi.imageindex("models/vault/monsters/infantry/camo.pcx");
}

constexpr vec3_t aimangles[] = {
	{ 0.0f, 5.0f, 0.0f },
	{ 10.0f, 15.0f, 0.0f },
	{ 20.0f, 25.0f, 0.0f },
	{ 25.0f, 35.0f, 0.0f },
	{ 30.0f, 40.0f, 0.0f },
	{ 30.0f, 45.0f, 0.0f },
	{ 25.0f, 50.0f, 0.0f },
	{ 20.0f, 40.0f, 0.0f },
	{ 15.0f, 35.0f, 0.0f },
	{ 40.0f, 35.0f, 0.0f },
	{ 70.0f, 35.0f, 0.0f },
	{ 90.0f, 35.0f, 0.0f }
};

void InfantryMachineGun(edict_t* self)
{
	vec3_t start, forward, right, up, dir, end;

	if (!has_valid_enemy(self))
		return;

	// Si está en secuencia de muerte y tiene salud > 0
	if (self->health <= 0 && self->s.frame >= FRAME_death211 && self->s.frame <= FRAME_death225)
	{
		// Usar los offsets precisos del machinegun durante la muerte
		monster_muzzleflash_id_t const flash_number = static_cast<monster_muzzleflash_id_t>(MZ2_INFANTRY_MACHINEGUN_2 + (self->s.frame - FRAME_death211));

		// Obtener vectores de dirección
		AngleVectors(self->s.angles, forward, right, nullptr);

		// Usar los offsets precisos del machinegun
		start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

		// Calcular dirección usando los ángulos predefinidos
		vec3_t const vec = self->s.angles - aimangles[flash_number - MZ2_INFANTRY_MACHINEGUN_2];
		AngleVectors(vec, forward, nullptr, nullptr);

		// Disparar usando el efecto y sonido del hyperblaster
		monster_fire_blaster2(self, start, forward, 6, 1150, MZ2_MEDIC_HYPERBLASTER1_5, EF_BLASTER);
	}
	else
	{
		// Comportamiento normal cuando no está muriendo
		vec3_t  const offset = { 26.6f, 6.1f, 10.1f };
		AngleVectors(self->s.angles, forward, right, up);
		start = G_ProjectSource2(self->s.origin, offset, forward, right, up);

		dir = self->enemy->s.origin - start;

		if (frandom() < 0.3f)
			PredictAim(self, self->enemy, start, 1075, true, 0, &dir, &end);
		else
			end = self->enemy->s.origin;

		trace_t const trace = gi.traceline(start, end, self, MASK_PROJECTILE);

		if (trace.ent == self->enemy || trace.ent == world)
		{
			dir.normalize();
			monster_fire_blaster2(self, start, dir, 6, 1150, MZ2_MEDIC_HYPERBLASTER1_5, EF_BLASTER);
		}
	}
}

MONSTERINFO_SIGHT(infantry_sight) (edict_t* self, edict_t* other) -> void
{
	if (brandom())
		gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
}

void infantry_dead(edict_t* self)
{

	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, -8 };
	monster_dead(self);
}

static void infantry_shrink(edict_t* self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

mframe_t infantry_frames_death1[] = {
	{ ai_move, -4, nullptr, FRAME_death102 },
	{ ai_move },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, -4, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -1, monster_footstep },
	{ ai_move, 3 },
	{ ai_move, 1 },
	{ ai_move, 1 },
	{ ai_move, -2 },
	{ ai_move, 2 },
	{ ai_move, 2 },
	{ ai_move, 9, [](edict_t* self) { infantry_shrink(self); monster_footstep(self); } },
	{ ai_move, 9 },
	{ ai_move, 5, monster_footstep },
	{ ai_move, -3 },
	{ ai_move, -3 }
};
MMOVE_T(infantry_move_death1) = { FRAME_death101, FRAME_death120, infantry_frames_death1, infantry_dead };

// Off with his head
mframe_t infantry_frames_death2[] = {
	{ ai_move, 0, nullptr, FRAME_death202 },
	{ ai_move, 1 },
	{ ai_move, 5 },
	{ ai_move, -1 },
	{ ai_move },
	{ ai_move, 1, monster_footstep },
	{ ai_move, 1, monster_footstep },
	{ ai_move, 4 },
	{ ai_move, 3 },
	{ ai_move },
	{ ai_move, -2, InfantryMachineGun },
	{ ai_move, -2, InfantryMachineGun },
	{ ai_move, -3, InfantryMachineGun },
	{ ai_move, -1, InfantryMachineGun },
	{ ai_move, -2, InfantryMachineGun },
	{ ai_move, 0, InfantryMachineGun },
	{ ai_move, 2, InfantryMachineGun },
	{ ai_move, 2, InfantryMachineGun },
	{ ai_move, 3, InfantryMachineGun },
	{ ai_move, -10, InfantryMachineGun },
	{ ai_move, -7, InfantryMachineGun },
	{ ai_move, -8, InfantryMachineGun },
	{ ai_move, -6, [](edict_t* self) { infantry_shrink(self); monster_footstep(self); } },
	{ ai_move, 4 },
	{ ai_move }
};
MMOVE_T(infantry_move_death2) = { FRAME_death201, FRAME_death225, infantry_frames_death2, infantry_dead };

mframe_t infantry_frames_death3[] = {
	{ ai_move, 0 },
	{ ai_move },
	{ ai_move, 0, [](edict_t* self) { infantry_shrink(self); monster_footstep(self); } },
	{ ai_move, -6 },
	{ ai_move, -11, [](edict_t* self) { self->monsterinfo.nextframe = FRAME_death307; } },
	{ ai_move, -3 },
	{ ai_move, -11 },
	{ ai_move, 0, monster_footstep },
	{ ai_move }
};
MMOVE_T(infantry_move_death3) = { FRAME_death301, FRAME_death309, infantry_frames_death3, infantry_dead };

DIE(infantry_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	int n;

	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		const char* head_gib = (self->monsterinfo.active_move != &infantry_move_death3) ? "models/objects/gibs/sm_meat/tris.md2" : "models/monsters/infantry/gibs/head.md2";

		self->s.skinnum /= 2;

		ThrowGibs(self, damage, {
			{ "models/objects/gibs/bone/tris.md2" },
			{ 3, "models/objects/gibs/sm_meat/tris.md2" },
			{ "models/monsters/infantry/gibs/chest.md2", GIB_SKINNED },
			{ "models/monsters/infantry/gibs/gun.md2", GIB_SKINNED | GIB_UPRIGHT },
			{ 2, "models/monsters/infantry/gibs/foot.md2", GIB_SKINNED },
			{ 2, "models/monsters/infantry/gibs/arm.md2", GIB_SKINNED },
			{ head_gib, GIB_HEAD | GIB_SKINNED }
			});
		self->deadflag = true;
		return;
	}

	if (self->deadflag)
		return;

	// regular death
	self->deadflag = true;
	self->takedamage = true;

	n = irandom(3);

	if (n == 0)
	{
		M_SetAnimation(self, &infantry_move_death1);
		gi.sound(self, CHAN_VOICE, sound_die2, 1, ATTN_NORM, 0);
	}
	else if (n == 1)
	{
		M_SetAnimation(self, &infantry_move_death2);
		gi.sound(self, CHAN_VOICE, sound_die1, 1, ATTN_NORM, 0);
	}
	else
	{
		M_SetAnimation(self, &infantry_move_death3);
		gi.sound(self, CHAN_VOICE, sound_die2, 1, ATTN_NORM, 0);
	}

	// don't always pop a head gib, it gets old
	if (n != 2 && frandom() <= 0.25f)
	{
		edict_t* head = ThrowGib(self, "models/monsters/infantry/gibs/head.md2", damage, GIB_NONE, self->s.scale);

		if (head)
		{
			head->s.angles = self->s.angles;
			head->s.origin = self->s.origin + vec3_t{ 0, 0, 32.f };
			vec3_t  const headDir = (self->s.origin - inflictor->s.origin);
			head->velocity = headDir / headDir.length() * 100.0f;
			head->velocity[2] = 200.0f;
			head->avelocity *= 0.15f;
			head->s.skinnum = 0;
			gi.linkentity(head);
		}
	}
}
mframe_t infantry_frames_duck[] = {
	{ ai_move, -2, monster_duck_down },
	{ ai_move, -5, monster_duck_hold },
	{ ai_move, 3 },
	{ ai_move, 4, monster_duck_up },
	{ ai_move }
};
MMOVE_T(infantry_move_duck) = { FRAME_duck01, FRAME_duck05, infantry_frames_duck, infantry_run };

// PMM - dodge code moved below so I can see the attack frames

extern const mmove_t infantry_move_attack4;

void infantry_set_firetime(edict_t* self)
{
	self->monsterinfo.fire_wait = level.time + random_time(0.7_sec, 2_sec);

	if (!(self->monsterinfo.aiflags & AI_STAND_GROUND) && self->enemy && range_to(self, self->enemy) >= RANGE_RUN_ATTACK && ai_check_move(self, 8.0f))
		M_SetAnimation(self, &infantry_move_attack4, false);
}

void infantry_cock_gun(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_weapon_cock, 1, ATTN_NORM, 0);

	// gun cocked
	self->count = 1;
}

void infantry_fire(edict_t* self);

// cock-less attack, used if he has already cocked his gun
mframe_t infantry_frames_attack1[] = {
	{ ai_charge },
	{ ai_charge, 6, [](edict_t* self) { infantry_set_firetime(self); monster_footstep(self); } },
	{ ai_charge, 0, infantry_fire },
	{ ai_charge },
	{ ai_charge, 1 },
	{ ai_charge, -7 },
	{ ai_charge, -6, [](edict_t* self) { self->monsterinfo.nextframe = FRAME_attak114; monster_footstep(self); } },
	// dead frames start
	{ ai_charge, -1 },
	{ ai_charge, 0, infantry_cock_gun },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	// dead frames end
	{ ai_charge, -1 },
	{ ai_charge, -1 }
};
MMOVE_T(infantry_move_attack1) = { FRAME_attak101, FRAME_attak115, infantry_frames_attack1, infantry_run };

// old animation, full cock + shoot
mframe_t infantry_frames_attack3[] = {
	{ ai_charge, 4,  nullptr },
	{ ai_charge, -1, nullptr },
	{ ai_charge, -1, nullptr },
	{ ai_charge, 0,  infantry_cock_gun },
	{ ai_charge, -1, nullptr },
	{ ai_charge, 1,  nullptr },
	{ ai_charge, 1,  nullptr },
	{ ai_charge, 2,  nullptr },
	{ ai_charge, -2, nullptr },
	{ ai_charge, -3, [](edict_t* self) { infantry_set_firetime(self); monster_footstep(self); }  },
	{ ai_charge, 1,  infantry_fire },
	{ ai_charge, 5,  nullptr },
	{ ai_charge, -1, nullptr },
	{ ai_charge, -2, nullptr },
	{ ai_charge, -3, nullptr },
};
MMOVE_T(infantry_move_attack3) = { FRAME_attak301, FRAME_attak315, infantry_frames_attack3, infantry_run };

// even older animation, full cock + shoot
mframe_t infantry_frames_attack5[] = {
	// skipped frames
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, nullptr },

	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, monster_footstep },
	{ ai_charge, 0, infantry_cock_gun },
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, [](edict_t* self) { self->monsterinfo.nextframe = self->s.frame + 1; } },
	{ ai_charge, 0, nullptr }, // skipped frame
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, infantry_set_firetime },
	{ ai_charge, 0, infantry_fire },

	// skipped frames
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, nullptr },

	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, nullptr },
	{ ai_charge, 0, monster_footstep }
};
MMOVE_T(infantry_move_attack5) = { FRAME_attak401, FRAME_attak423, infantry_frames_attack5, infantry_run };

extern const mmove_t infantry_move_attack4;

void infantry_fire(edict_t* self)
{
	InfantryMachineGun(self);

	// we fired, so we must cock again before firing
	self->count = 1;

	// check if we ran out of firing time
	if (self->monsterinfo.active_move == &infantry_move_attack4)
	{
		if (level.time >= self->monsterinfo.fire_wait)
		{
			monster_done_dodge(self);
			M_SetAnimation(self, &infantry_move_attack1, false);
			self->monsterinfo.nextframe = FRAME_attak114;
		}
		// got close to an edge
		else if (!ai_check_move(self, 8.0f))
		{
			M_SetAnimation(self, &infantry_move_attack1, false);
			self->monsterinfo.nextframe = FRAME_attak103;
			monster_done_dodge(self);
			self->monsterinfo.attack_state = AS_STRAIGHT;
		}
	}
	else if ((self->s.frame >= FRAME_attak101 && self->s.frame <= FRAME_attak115) ||
		(self->s.frame >= FRAME_attak301 && self->s.frame <= FRAME_attak315) ||
		(self->s.frame >= FRAME_attak401 && self->s.frame <= FRAME_attak424))
	{
		if (level.time >= self->monsterinfo.fire_wait)
		{
			self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

			if (self->s.frame == FRAME_attak416)
				self->monsterinfo.nextframe = FRAME_attak420;
		}
		else
			self->monsterinfo.aiflags |= AI_HOLD_FRAME;
	}
}

void infantry_swing(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_punch_swing, 1, ATTN_NORM, 0);
}

void infantry_smack(edict_t* self)
{
	vec3_t const aim = { MELEE_DISTANCE, 0, 0 };
	// Verificar si self->enemy está correctamente inicializado
	if (self->enemy) {
		// Llamar a fire_hit solo si self->enemy está inicializado
		if (fire_hit(self, aim, irandom(5, 10), 50))
			gi.sound(self, CHAN_WEAPON, sound_punch_hit, 1, ATTN_NORM, 0);
		else
			self->monsterinfo.melee_debounce_time = level.time + 1.5_sec;
	}
	else {
		//char buffer[256];
		//std::snprintf(buffer, sizeof(buffer), "infantry_smack: Error: enemy not properly initialized\n");
		//gi.Com_Print(buffer);

		// Manejar el caso donde self->enemy no está inicializado
		self->monsterinfo.melee_debounce_time = level.time + 1.5_sec; // Puedes ajustar esto según sea necesario//
	}
}


mframe_t infantry_frames_attack2[] = {
	{ ai_charge, 3 },
	{ ai_charge, 6 },
	{ ai_charge, 0, infantry_swing },
	{ ai_charge, 8, monster_footstep },
	{ ai_charge, 5 },
	{ ai_charge, 8, infantry_smack },
	{ ai_charge, 8, infantry_smack },
	{ ai_charge, 3 }
};
MMOVE_T(infantry_move_attack2) = { FRAME_attak201, FRAME_attak208, infantry_frames_attack2, infantry_run };

static void infantry_grenade(edict_t* self)
{
	constexpr float MORTAR_SPEED = 1050.f;
	constexpr float GRENADE_SPEED = 760.f;

	vec3_t start{};
	vec3_t forward{}, right{}, up{};
	vec3_t aim{};
	const vec3_t offset = { 24, 10, 10 };
	constexpr float speed = GRENADE_SPEED;

	if (!self->enemy || !self->enemy->inuse)
		return;

	AngleVectors(self->s.angles, forward, right, up);
	start = G_ProjectSource2(self->s.origin, offset, forward, right, up);

	// Predict target position
	const float dist = range_to(self, self->enemy);

	// Para distancias cortas, usar PredictAim
	if (dist < 400) // Ajusta este valor según necesites
	{
		PredictAim(self, self->enemy, start, speed, false, 0.f, &aim, nullptr);

		// Pequeño ajuste aleatorio y compensación de gravedad para corta distancia
		aim += right * (crandom() * 0.02f);
		aim += up * (crandom() * 0.02f - 0.01f); // Ligero ajuste hacia abajo
		aim.normalize();
	}
	// Para distancias más largas, usar CalculatePitch
	else
	{
		const vec3_t predicted_pos = self->enemy->s.origin + (self->enemy->velocity * (dist / speed));
		aim = predicted_pos - start;
		aim.normalize();

		if (M_CalculatePitchToFire(self, predicted_pos, start, aim, speed, 1.5f, false))
		{
			aim[2] += crandom_open() * 0.01f;
			aim.normalize();
		}
	}

	fire_grenade2(self, start, aim, 40, speed, 2.5_sec, 80, false);
	gi.sound(self, CHAN_VOICE, sound_handgrenade, 1, ATTN_NORM, 0);
}

mframe_t infantry_frames_grenade[] = {
	{ ai_charge, 3 },
	{ ai_charge, 6 },
	{ ai_charge, 5 },
	{ ai_charge, 0, infantry_swing },
	{ ai_charge, 8 },
	{ ai_charge, 8, infantry_grenade },
	{ ai_charge, 8, monster_footstep },
	{ ai_charge, 3 }
};
MMOVE_T(infantry_move_grenade) = { FRAME_attak201, FRAME_attak208, infantry_frames_grenade, infantry_run };

// [Paril-KEX] run-attack, inspired by q2test
void infantry_attack4_refire(edict_t* self)
{
	// ran out of firing time
	if (level.time >= self->monsterinfo.fire_wait)
	{
		monster_done_dodge(self);
		M_SetAnimation(self, &infantry_move_attack1, false);
		self->monsterinfo.nextframe = FRAME_attak114;
	}
	// we got too close, or we can't move forward, switch us back to regular attack
	else if ((self->monsterinfo.aiflags & AI_STAND_GROUND) || (self->enemy && (range_to(self, self->enemy) < RANGE_RUN_ATTACK || !ai_check_move(self, 8.0f))))
	{
		M_SetAnimation(self, &infantry_move_attack1, false);
		self->monsterinfo.nextframe = FRAME_attak103;
		monster_done_dodge(self);
		self->monsterinfo.attack_state = AS_STRAIGHT;
	}
	else
		self->monsterinfo.nextframe = FRAME_run201;

	infantry_fire(self);
}

mframe_t infantry_frames_attack4[] = {
	{ ai_charge, 16, infantry_fire },
	{ ai_charge, 16, [](edict_t* self) { monster_footstep(self); infantry_fire(self); } },
	{ ai_charge, 13, infantry_fire },
	{ ai_charge, 10, infantry_fire },
	{ ai_charge, 16, infantry_fire },
	{ ai_charge, 16, [](edict_t* self) { monster_footstep(self); infantry_fire(self); } },
	{ ai_charge, 16, infantry_fire },
	{ ai_charge, 16, infantry_attack4_refire }
};
MMOVE_T(infantry_move_attack4) = { FRAME_run201, FRAME_run208, infantry_frames_attack4, infantry_run, 0.5f };

MONSTERINFO_ATTACK(infantry_attack) (edict_t* self) -> void
{
	monster_done_dodge(self);

	float  const r = range_to(self, self->enemy);

	if (r <= RANGE_MELEE && self->monsterinfo.melee_debounce_time <= level.time)
	{
		M_SetAnimation(self, &infantry_move_attack2);
	}
	else if (r > RANGE_MELEE && frandom() <= 0.35f)
	{
		// 35% chance to throw a grenade when enemy is beyond melee range
		M_SetAnimation(self, &infantry_move_grenade);
	}
	else if (M_CheckClearShot(self, monster_flash_offset[MZ2_INFANTRY_MACHINEGUN_1]))
	{
		if (self->count)
			M_SetAnimation(self, &infantry_move_attack1);
		else
		{
			M_SetAnimation(self, frandom() <= 0.1f ? &infantry_move_attack5 : &infantry_move_attack3);
			self->monsterinfo.nextframe = FRAME_attak405;
		}
	}
}

//===========
// PGM
void infantry_jump_now(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 100);
	self->velocity += (up * 300);
}

void infantry_jump2_now(edict_t* self)
{
	vec3_t forward, up;

	AngleVectors(self->s.angles, forward, nullptr, up);
	self->velocity += (forward * 150);
	self->velocity += (up * 400);
}

void infantry_jump_wait_land(edict_t* self)
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

mframe_t infantry_frames_jump[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, infantry_jump_now },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, infantry_jump_wait_land },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(infantry_move_jump) = { FRAME_jump01, FRAME_jump10, infantry_frames_jump, infantry_run };

mframe_t infantry_frames_jump2[] = {
	{ ai_move, -8 },
	{ ai_move, -4 },
	{ ai_move, -4 },
	{ ai_move, 0, infantry_jump2_now },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, infantry_jump_wait_land },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(infantry_move_jump2) = { FRAME_jump01, FRAME_jump10, infantry_frames_jump2, infantry_run };

void infantry_jump(edict_t* self, blocked_jump_result_t result)
{
	if (!self->enemy)
		return;

	monster_done_dodge(self);

	if (result == blocked_jump_result_t::JUMP_JUMP_UP)
		M_SetAnimation(self, &infantry_move_jump2);
	else
		M_SetAnimation(self, &infantry_move_jump);
}

MONSTERINFO_BLOCKED(infantry_blocked) (edict_t* self, float dist) -> bool
{
	if (auto  const result = blocked_checkjump(self, dist); result != blocked_jump_result_t::NO_JUMP)
	{
		if (result != blocked_jump_result_t::JUMP_TURN)
			infantry_jump(self, result);
		return true;
	}

	if (blocked_checkplat(self, dist))
		return true;

	return false;
}

MONSTERINFO_DUCK(infantry_duck) (edict_t* self, gtime_t eta) -> bool
{
	// if we're jumping, don't dodge
	if ((self->monsterinfo.active_move == &infantry_move_jump) ||
		(self->monsterinfo.active_move == &infantry_move_jump2))
	{
		return false;
	}

	// don't duck during our firing or melee frames
	if (self->s.frame == FRAME_attak103 ||
		self->s.frame == FRAME_attak315 ||
		(self->monsterinfo.active_move == &infantry_move_attack2))
	{
		self->monsterinfo.unduck(self);
		return false;
	}

	M_SetAnimation(self, &infantry_move_duck);

	return true;
}

MONSTERINFO_SIDESTEP(infantry_sidestep) (edict_t* self) -> bool
{
	// if we're jumping, don't dodge
	if ((self->monsterinfo.active_move == &infantry_move_jump) ||
		(self->monsterinfo.active_move == &infantry_move_jump2))
	{
		return false;
	}

	if (self->monsterinfo.active_move == &infantry_move_run)
		return true;

	// Don't sidestep if we're already sidestepping, and def not unless we're actually shooting
	// or if we already cocked
	if (self->monsterinfo.active_move != &infantry_move_attack4 &&
		self->monsterinfo.next_move != &infantry_move_attack4 &&
		((self->s.frame == FRAME_attak103 ||
			self->s.frame == FRAME_attak311 ||
			self->s.frame == FRAME_attak416) &&
			!self->count))
	{
		// give us a fire time boost so we don't end up firing for 1 frame
		self->monsterinfo.fire_wait += random_time(300_ms, 600_ms);

		M_SetAnimation(self, &infantry_move_attack4, false);
	}

	return true;
}

void InfantryPrecache()
{
	sound_pain1.assign("infantry/infpain1.wav");
	sound_pain2.assign("infantry/infpain2.wav");
	sound_die1.assign("infantry/infdeth1.wav");
	sound_die2.assign("infantry/infdeth2.wav");

	//sound_gunshot.assign("infantry/infatck1.wav");
	sound_gunshot.assign("weapons/hyprbf1a.wav");
	//	sound_weapon_cock.assign("infantry/infatck3.wav");
	sound_weapon_cock.assign("weapons/hyprbu1a.wav");
	sound_punch_swing.assign("infantry/infatck2.wav");
	sound_punch_hit.assign("infantry/melee2.wav");

	sound_sight.assign("infantry/infsght1.wav");
	sound_search.assign("infantry/infsrch1.wav");
	sound_idle.assign("infantry/infidle1.wav");
	sound_handgrenade.assign("weapons/hgrent1a.wav");
}

constexpr spawnflags_t SPAWNFLAG_INFANTRY_NOJUMPING = 8_spawnflag;

/*QUAKED monster_infantry (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight NoJumping
 */
void SP_monster_infantry(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	InfantryPrecache();

	self->monsterinfo.aiflags |= AI_STINKY;

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/infantry/tris.md2");

	gi.modelindex("models/monsters/infantry/gibs/head.md2");
	gi.modelindex("models/monsters/infantry/gibs/chest.md2");
	gi.modelindex("models/monsters/infantry/gibs/gun.md2");
	gi.modelindex("models/monsters/infantry/gibs/arm.md2");
	gi.modelindex("models/monsters/infantry/gibs/foot.md2");

	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, 32 };

	if (!g_horde->integer) {
		self->health = 100 * st.health_multiplier;
	}

	if (g_horde->integer) {

		self->s.scale = 1.2f;

		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = 85;
		if (!st.was_key_specified("power_armor_type"))
			self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;

		self->health = 150 * st.health_multiplier;
	}

	self->health = 100 * st.health_multiplier;

	self->gib_health = -65;
	self->mass = 200;

	self->pain = infantry_pain;
	self->die = infantry_die;
	self->monsterinfo.combat_style = COMBAT_MIXED;

	self->monsterinfo.stand = infantry_stand;
	self->monsterinfo.walk = infantry_walk;
	self->monsterinfo.run = infantry_run;
	// pmm
	self->monsterinfo.dodge = M_MonsterDodge;
	self->monsterinfo.duck = infantry_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = infantry_sidestep;
	self->monsterinfo.blocked = infantry_blocked;
	// pmm
	self->monsterinfo.attack = infantry_attack;
	self->monsterinfo.melee = nullptr;
	self->monsterinfo.sight = infantry_sight;
	self->monsterinfo.idle = infantry_fidget;
	self->monsterinfo.setskin = infantry_setskin;

	gi.linkentity(self);

	M_SetAnimation(self, &infantry_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;
	self->monsterinfo.can_jump = !self->spawnflags.has(SPAWNFLAG_INFANTRY_NOJUMPING);
	self->monsterinfo.drop_height = 192;
	self->monsterinfo.jump_height = 40;

	//	self->s.renderfx |= RF_CUSTOMSKIN;
	//	self->s.skinnum = gi.imageindex("models/vault/monsters/infantry/camo.pcx");

	walkmonster_start(self);

	ApplyMonsterBonusFlags(self);
}
