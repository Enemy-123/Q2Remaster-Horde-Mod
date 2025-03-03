// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

hover_vanilla

==============================================================================
*/

#include "g_local.h"
#include "m_hover.h"
#include "m_flash.h"
#include "shared.h"

static cached_soundindex sound_pain1;
static cached_soundindex sound_pain2;
static cached_soundindex sound_death1;
static cached_soundindex sound_death2;
static cached_soundindex sound_sight;
static cached_soundindex sound_search1;
static cached_soundindex sound_search2;

// ROGUE
// daedalus_bomber sounds
static cached_soundindex daed_sound_pain1;
static cached_soundindex daed_sound_pain2;
static cached_soundindex daed_sound_death1;
static cached_soundindex daed_sound_death2;
static cached_soundindex daed_sound_sight;
static cached_soundindex daed_sound_search1;
static cached_soundindex daed_sound_search2;
// ROGUE

MONSTERINFO_SIGHT(hover_vanilla_sight) (edict_t* self, edict_t* other) -> void
{
	// PMM - daedalus_bomber sounds
	if (self->mass < 225)
		gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, daed_sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_SEARCH(hover_vanilla_search) (edict_t* self) -> void
{
	// PMM - daedalus_bomber sounds
	if (self->mass < 225)
	{
		if (frandom() < 0.5f)
			gi.sound(self, CHAN_VOICE, sound_search1, 1, ATTN_NORM, 0);
		else
			gi.sound(self, CHAN_VOICE, sound_search2, 1, ATTN_NORM, 0);
	}
	else
	{
		if (frandom() < 0.5f)
			gi.sound(self, CHAN_VOICE, daed_sound_search1, 1, ATTN_NORM, 0);
		else
			gi.sound(self, CHAN_VOICE, daed_sound_search2, 1, ATTN_NORM, 0);
	}
}

void hover_vanilla_run(edict_t* self);
void hover_vanilla_dead(edict_t* self);
void hover_vanilla_attack(edict_t* self);
void hover_vanilla_reattack(edict_t* self);
void hover_vanilla_fire_blaster(edict_t* self);

void daedalus_bomber_reattack(edict_t* self);
void daedalus_bomber_fire_grenades(edict_t* self);

mframe_t hover_vanilla_frames_stand[] = {
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
MMOVE_T(hover_vanilla_move_stand) = { FRAME_stand01, FRAME_stand30, hover_vanilla_frames_stand, nullptr };

mframe_t hover_vanilla_frames_pain3[] = {
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
MMOVE_T(hover_vanilla_move_pain3) = { FRAME_pain301, FRAME_pain309, hover_vanilla_frames_pain3, hover_vanilla_run };

mframe_t hover_vanilla_frames_pain2[] = {
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
MMOVE_T(hover_vanilla_move_pain2) = { FRAME_pain201, FRAME_pain212, hover_vanilla_frames_pain2, hover_vanilla_run };

mframe_t hover_vanilla_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, -8 },
	{ ai_move, -4 },
	{ ai_move, -6 },
	{ ai_move, -4 },
	{ ai_move, -3 },
	{ ai_move, 1 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 3 },
	{ ai_move, 1 },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 3 },
	{ ai_move, 2 },
	{ ai_move, 7 },
	{ ai_move, 1 },
	{ ai_move },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move, 5 },
	{ ai_move, 3 },
	{ ai_move, 4 }
};
MMOVE_T(hover_vanilla_move_pain1) = { FRAME_pain101, FRAME_pain128, hover_vanilla_frames_pain1, hover_vanilla_run };

mframe_t hover_vanilla_frames_walk[] = {
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 }
};
MMOVE_T(hover_vanilla_move_walk) = { FRAME_forwrd01, FRAME_forwrd35, hover_vanilla_frames_walk, nullptr };

mframe_t hover_vanilla_frames_run[] = {
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 }
};
MMOVE_T(hover_vanilla_move_run) = { FRAME_forwrd01, FRAME_forwrd35, hover_vanilla_frames_run, nullptr };

static void hover_vanilla_gib(edict_t* self)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_EXPLOSION1);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PHS, false);

	self->s.skinnum /= 2;

	ThrowGibs(self, 150, {
		{ 2, "models/objects/gibs/sm_meat/tris.md2" },
		{ 2, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
		{ "models/monsters/hover/gibs/chest.md2", GIB_SKINNED },
		{ 2, "models/monsters/hover/gibs/ring.md2", GIB_SKINNED | GIB_METALLIC },
		{ 2, "models/monsters/hover/gibs/foot.md2", GIB_SKINNED },
		{ "models/monsters/hover/gibs/head.md2", GIB_SKINNED | GIB_HEAD },
		});
}

THINK(hover_vanilla_deadthink) (edict_t* self) -> void
{
	if (!self->groundentity && level.time < self->timestamp)
	{
		self->nextthink = level.time + FRAME_TIME_S;
		return;
	}

	hover_vanilla_gib(self);
}

void hover_vanilla_dying(edict_t* self)
{
	if (self->groundentity)
	{
		hover_vanilla_deadthink(self);
		return;
	}

	if (brandom())
		return;

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_PLAIN_EXPLOSION);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PHS, false);

	if (brandom())
		ThrowGibs(self, 120, {
			{ "models/objects/gibs/sm_meat/tris.md2" }
			});
	else
		ThrowGibs(self, 120, {
			{ "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC }
			});
}

mframe_t hover_vanilla_frames_death1[] = {
	{ ai_move },
	{ ai_move, 0.f, hover_vanilla_dying },
	{ ai_move },
	{ ai_move, 0.f, hover_vanilla_dying },
	{ ai_move },
	{ ai_move, 0.f, hover_vanilla_dying },
	{ ai_move, -10, hover_vanilla_dying },
	{ ai_move, 3 },
	{ ai_move, 5, hover_vanilla_dying },
	{ ai_move, 4, hover_vanilla_dying },
	{ ai_move, 7 }
};
MMOVE_T(hover_vanilla_move_death1) = { FRAME_death101, FRAME_death111, hover_vanilla_frames_death1, hover_vanilla_dead };

mframe_t hover_vanilla_frames_start_attack[] = {
	{ ai_charge, 1 },
	{ ai_charge, 1 },
	{ ai_charge, 1 }
};
MMOVE_T(hover_vanilla_move_start_attack) = { FRAME_attak101, FRAME_attak103, hover_vanilla_frames_start_attack, hover_vanilla_attack };

mframe_t hover_vanilla_frames_attack1[] = {
	{ ai_charge, -10, hover_vanilla_fire_blaster },
	{ ai_charge, -10, hover_vanilla_fire_blaster },
	{ ai_charge, 0, hover_vanilla_reattack },
};
MMOVE_T(hover_vanilla_move_attack1) = { FRAME_attak104, FRAME_attak106, hover_vanilla_frames_attack1, nullptr };

mframe_t daedalus_bomber_frames_attack1[] = {
	{ ai_charge, -10, daedalus_bomber_fire_grenades },
	{ ai_charge, -10, daedalus_bomber_fire_grenades },
	{ ai_charge, 0, daedalus_bomber_reattack },
};
MMOVE_T(daedalus_bomber_move_attack1) = { FRAME_attak104, FRAME_attak106, daedalus_bomber_frames_attack1, nullptr };

mframe_t hover_vanilla_frames_end_attack[] = {
	{ ai_charge, 1 },
	{ ai_charge, 1 }
};
MMOVE_T(hover_vanilla_move_end_attack) = { FRAME_attak107, FRAME_attak108, hover_vanilla_frames_end_attack, hover_vanilla_run };

/* PMM - circle strafing code */
#if 0
mframe_t hover_vanilla_frames_start_attack2[] = {
	{ ai_charge, 15 },
	{ ai_charge, 15 },
	{ ai_charge, 15 }
};
MMOVE_T(hover_vanilla_move_start_attack2) = { FRAME_attak101, FRAME_attak103, hover_vanilla_frames_start_attack2, hover_vanilla_attack };
#endif

mframe_t hover_vanilla_frames_attack2[] = {
	{ ai_charge, 10, hover_vanilla_fire_blaster },
	{ ai_charge, 10, hover_vanilla_fire_blaster },
	{ ai_charge, 10, hover_vanilla_reattack },
};
MMOVE_T(hover_vanilla_move_attack2) = { FRAME_attak104, FRAME_attak106, hover_vanilla_frames_attack2, nullptr };

mframe_t daedalus_bomber_frames_attack2[] = {
	{ ai_charge, -10, daedalus_bomber_fire_grenades },
	{ ai_charge, -10, daedalus_bomber_fire_grenades },
	{ ai_charge, 0, daedalus_bomber_reattack },
};
MMOVE_T(daedalus_bomber_move_attack2) = { FRAME_attak104, FRAME_attak106, daedalus_bomber_frames_attack2, nullptr };

#if 0
mframe_t hover_vanilla_frames_end_attack2[] = {
	{ ai_charge, 15 },
	{ ai_charge, 15 }
};
MMOVE_T(hover_vanilla_move_end_attack2) = { FRAME_attak107, FRAME_attak108, hover_vanilla_frames_end_attack2, hover_vanilla_run };
#endif

// end of circle strafe

void hover_vanilla_reattack(edict_t* self)
{
	if (self->enemy->health > 0)
		if (visible(self, self->enemy))
			if (frandom() <= 0.6f)
			{
				if (self->monsterinfo.attack_state == AS_STRAIGHT)
				{
					M_SetAnimation(self, &hover_vanilla_move_attack1);
					return;
				}
				else if (self->monsterinfo.attack_state == AS_SLIDING)
				{
					M_SetAnimation(self, &hover_vanilla_move_attack2);
					return;
				}
				else
					gi.Com_PrintFmt("PRINT: hover_vanilla_reattack: unexpected state {}\n", (int32_t)self->monsterinfo.attack_state);
			}
	M_SetAnimation(self, &hover_vanilla_move_end_attack);
}

void daedalus_bomber_reattack(edict_t* self)
{
	if (self->enemy->health > 0)
		if (visible(self, self->enemy))
			if (frandom() <= 0.4f)
			{
				if (self->monsterinfo.attack_state == AS_STRAIGHT)
				{
					M_SetAnimation(self, &daedalus_bomber_move_attack1);
					return;
				}
				else if (self->monsterinfo.attack_state == AS_SLIDING)
				{
					M_SetAnimation(self, &daedalus_bomber_move_attack2);
					return;
				}
				else
					gi.Com_PrintFmt("hover_reattack: unexpected state {}\n", (int32_t)self->monsterinfo.attack_state);
			}
	M_SetAnimation(self, &hover_vanilla_move_end_attack);
}



void daedalus_bomber_fire_grenades(edict_t* self)
{
	constexpr float MORTAR_SPEED = 1050.f;
	constexpr float GRENADE_SPEED = 760.f;


	vec3_t					 forward, right, up;
	vec3_t					 aim;
	monster_muzzleflash_id_t flash_number = MZ2_GUNCMDR_GRENADE_MORTAR_1;
	float					 spread;
	float					 pitch = 0;
	// PMM
	vec3_t target;
	vec3_t offset{};
	bool blindfire = false;

	if (self->s.frame == FRAME_attak104)
		offset = { 1.7f, 7.0f, 11.3f };
	else if (self->s.frame == FRAME_attak105)
		offset = { 1.7f, -7.0f, 11.3f };


	if (!self->enemy || !self->enemy->inuse) // PGM
		return;

	//	pmm
	// if we're shooting blind and we still can't see our enemy
	if ((blindfire) && (!visible(self, self->enemy)))
	{
		// and we have a valid blind_fire_target
		if (!self->monsterinfo.blind_fire_target)
			return;

		target = self->monsterinfo.blind_fire_target;
	}
	else
		target = self->enemy->s.origin;
	// pmm

	AngleVectors(self->s.angles, forward, right, up); // PGM
	const vec3_t start = G_ProjectSource2(self->s.origin, offset, forward, right, up);
	// PGM
	if (self->enemy && !(flash_number >= MZ2_GUNCMDR_GRENADE_MORTAR_1 && flash_number <= MZ2_GUNCMDR_GRENADE_MORTAR_1))
	{
		float dist;

		aim = target - self->s.origin;
		dist = aim.length();

		// aim up if they're on the same level as me and far away.
		if ((dist > 512) && (aim[2] < 64) && (aim[2] > -64))
		{
			aim[2] += (dist - 512);
		}

		aim.normalize();
		pitch = aim[2];
		if (pitch > 0.4f)
			pitch = 0.4f;
		else if (pitch < -0.5f)
			pitch = -0.5f;

		if ((self->enemy->absmin.z - self->absmax.z) > 16.f && flash_number >= MZ2_GUNCMDR_GRENADE_MORTAR_1 && flash_number <= MZ2_GUNCMDR_GRENADE_MORTAR_3)
			pitch += 0.5f;
	}
	// PGM

	if (flash_number >= MZ2_GUNCMDR_GRENADE_MORTAR_1 && flash_number <= MZ2_GUNCMDR_GRENADE_MORTAR_1)
		pitch -= 0.05f;

	if (!(flash_number >= MZ2_GUNCMDR_GRENADE_MORTAR_1 && flash_number <= MZ2_GUNCMDR_GRENADE_MORTAR_1))
	{
		aim = forward + (right * spread);
		aim += (up * pitch);
		aim.normalize();
	}
	else
	{
		PredictAim(self, self->enemy, start, 800, false, 0.f, &aim, nullptr);
		aim += right * spread;
		aim.normalize();
	}


	{
		// mortar fires farther
		float speed;

		if (flash_number >= MZ2_GUNCMDR_GRENADE_MORTAR_1 && flash_number <= MZ2_GUNCMDR_GRENADE_MORTAR_1)
			speed = MORTAR_SPEED;
		else
			speed = GRENADE_SPEED;

		// try search for best pitch
		PredictAim(self, self->enemy, start, 800, false, 0.f, &aim, nullptr);
		monster_fire_grenade(self, start, aim, 24, speed, flash_number, (crandom_open() * 10.0f), 200.f + (crandom_open() * 10.0f));

	}
}

void hover_vanilla_fire_blaster(edict_t* self)
{
	vec3_t	  start;
	vec3_t	  forward, right;
	vec3_t	  end;
	vec3_t	  dir;
	int		blasterSpeed;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;
	// PGM

	AngleVectors(self->s.angles, forward, right, nullptr);
	vec3_t const o = monster_flash_offset[(self->s.frame & 1) ? MZ2_HOVER_BLASTER_2 : MZ2_HOVER_BLASTER_1];
	start = M_ProjectFlashSource(self, o, forward, right);

	end = self->enemy->s.origin;
	end[2] += self->enemy->viewheight;
	dir = end - start;
	dir.normalize();

	blasterSpeed = 1230;

	// PGM	- daedalus fires blaster2
	PredictAim(self, self->enemy, start, blasterSpeed / 1.5, true, 0.f, &dir, &end);
	monster_fire_blaster(self, start, dir, 12, blasterSpeed, (self->s.frame & 1) ? MZ2_DAEDALUS_BLASTER_2 : MZ2_DAEDALUS_BLASTER, (self->s.frame % 4) ? EF_NONE : EF_BLASTER);
	// PGM
}

MONSTERINFO_STAND(hover_vanilla_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &hover_vanilla_move_stand);
}

MONSTERINFO_RUN(hover_vanilla_run) (edict_t* self) -> void
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &hover_vanilla_move_stand);
	else
		M_SetAnimation(self, &hover_vanilla_move_run);
}

MONSTERINFO_WALK(hover_vanilla_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &hover_vanilla_move_walk);
}

MONSTERINFO_ATTACK(hover_vanilla_start_attack) (edict_t* self) -> void
{
	M_SetAnimation(self, &hover_vanilla_move_start_attack);
}

void hover_vanilla_attack(edict_t* self)
{
	float chance = 0.5f;

	if (self->mass > 150) // the daedalus strafes more
		chance += 0.1f;

	if (frandom() > chance)
	{
		if (strcmp(self->classname, "monster_hover_vanilla") == 0)
			M_SetAnimation(self, &hover_vanilla_move_attack1);
		if (strcmp(self->classname, "monster_daedalus_bomber") == 0)
			M_SetAnimation(self, &daedalus_bomber_move_attack1);
		self->monsterinfo.attack_state = AS_STRAIGHT;
	}
	else // circle strafe
	{
		if (frandom() <= 0.5f) // switch directions
			self->monsterinfo.lefty = !self->monsterinfo.lefty;
		if (strcmp(self->classname, "monster_hover_vanilla") == 0)
			M_SetAnimation(self, &hover_vanilla_move_attack2);
		if (strcmp(self->classname, "monster_daedalus_bomber") == 0)
			M_SetAnimation(self, &daedalus_bomber_move_attack2);

		self->monsterinfo.attack_state = AS_SLIDING;
	}
}
PAIN(hover_vanilla_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;

	float r = frandom();

	//====
	if (r < 0.5f)
	{
		// PMM - daedalus_bomber sounds
		if (self->mass < 225)
			gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
		else
			gi.sound(self, CHAN_VOICE, daed_sound_pain1, 1, ATTN_NORM, 0);
	}
	else
	{
		// PMM - daedalus_bomber sounds
		if (self->mass < 225)
			gi.sound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
		else
			gi.sound(self, CHAN_VOICE, daed_sound_pain2, 1, ATTN_NORM, 0);
	}
	// PGM
	//====

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	r = frandom();

	if (damage <= 25)
	{
		if (r < 0.5f)
			M_SetAnimation(self, &hover_vanilla_move_pain3);
		else
			M_SetAnimation(self, &hover_vanilla_move_pain2);
	}
	else
	{
		//====
		// PGM pain sequence is WAY too long
		if (r < 0.3f)
			M_SetAnimation(self, &hover_vanilla_move_pain1);
		else
			M_SetAnimation(self, &hover_vanilla_move_pain2);
		// PGM
		//====
	}
}

MONSTERINFO_SETSKIN(hover_vanilla_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1; // PGM support for skins 2 & 3.
	else
		self->s.skinnum &= ~1; // PGM support for skins 2 & 3.
}

void hover_vanilla_dead(edict_t* self)
{
	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, -8 };
	self->movetype = MOVETYPE_TOSS;
	self->think = hover_vanilla_deadthink;
	self->nextthink = level.time + FRAME_TIME_S;
	self->timestamp = level.time + 15_sec;
	gi.linkentity(self);
}

DIE(hover_vanilla_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	//OnEntityDeath(self);
	self->s.effects = EF_NONE;
	self->monsterinfo.power_armor_type = IT_NULL;

	if (M_CheckGib(self, mod))
	{
		hover_vanilla_gib(self);
		return;
	}

	if (self->deadflag)
		return;

	// regular death
	// PMM - daedalus_bomber sounds
	if (self->mass < 225)
	{
		if (frandom() < 0.5f)
			gi.sound(self, CHAN_VOICE, sound_death1, 1, ATTN_NORM, 0);
		else
			gi.sound(self, CHAN_VOICE, sound_death2, 1, ATTN_NORM, 0);
	}
	else
	{
		if (frandom() < 0.5f)
			gi.sound(self, CHAN_VOICE, daed_sound_death1, 1, ATTN_NORM, 0);
		else
			gi.sound(self, CHAN_VOICE, daed_sound_death2, 1, ATTN_NORM, 0);
	}
	self->deadflag = true;
	self->takedamage = true;
	M_SetAnimation(self, &hover_vanilla_move_death1);
}

static void hover_vanilla_set_fly_parameters(edict_t* self) {
	if (strcmp(self->classname, "monster_hover_vanilla")) {
		{

			self->monsterinfo.fly_thrusters = false;
			self->monsterinfo.fly_acceleration = 20.f;
			self->monsterinfo.fly_speed = 320.f;
			// Icarus prefers to keep its distance, but flies slower than the flyer.
			// he never pins because of this.
			self->monsterinfo.fly_min_distance = 550.f;
			self->monsterinfo.fly_max_distance = 850.f;
		}
	}
	else
	{
		self->monsterinfo.fly_thrusters = false;
		self->monsterinfo.fly_acceleration = 20.f;
		self->monsterinfo.fly_speed = 170.f;
		// Icarus prefers to keep its distance, but flies slower than the flyer.
		// he never pins because of this.
		self->monsterinfo.fly_min_distance = 250.f;
		self->monsterinfo.fly_max_distance = 450.f;

	}
}
/*QUAKED monster_hover_vanilla (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 */
 /*QUAKED monster_daedalus_bomber (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 This is the improved icarus monster.
 */
void SP_monster_hover_vanilla(edict_t* self)
{
	const spawn_temp_t& st = ED_GetSpawnTemp();

	if (g_horde->integer && current_wave_level <= 18) {
		const bool isDaedalus = !strcmp(self->classname, "monster_daedalus_bomber");
		const float randomChance = frandom();

		constexpr float FIRST_SOUND_CHANCE = 0.12f;
		constexpr float SECOND_SOUND_CHANCE = 0.24f;

		const int currentSound = (randomChance < FIRST_SOUND_CHANCE)
			? (isDaedalus ? daed_sound_search1 : sound_search1)
			: (randomChance < SECOND_SOUND_CHANCE)
			? (isDaedalus ? daed_sound_search2 : sound_search2)
			: 0;  // No sound

		if (currentSound) {
			gi.sound(self, CHAN_VOICE, currentSound, 1, ATTN_NORM, 0);
		}
	}

	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/hover/tris.md2");

	gi.modelindex("models/monsters/hover/gibs/chest.md2");
	gi.modelindex("models/monsters/hover/gibs/foot.md2");
	gi.modelindex("models/monsters/hover/gibs/head.md2");
	gi.modelindex("models/monsters/hover/gibs/ring.md2");

	self->mins = { -24, -24, -24 };
	self->maxs = { 24, 24, 32 };

	self->health = 120 * st.health_multiplier;
	self->gib_health = -100;
	self->mass = 150;
	self->s.scale = 1.0f;

	self->pain = hover_vanilla_pain;
	self->die = hover_vanilla_die;

	self->monsterinfo.stand = hover_vanilla_stand;
	self->monsterinfo.walk = hover_vanilla_walk;
	self->monsterinfo.run = hover_vanilla_run;
	self->monsterinfo.attack = hover_vanilla_start_attack;
	self->monsterinfo.sight = hover_vanilla_sight;
	self->monsterinfo.search = hover_vanilla_search;
	self->monsterinfo.setskin = hover_vanilla_setskin;

	// PGM
	if (strcmp(self->classname, "monster_daedalus_bomber") == 0)
	{
		self->health = 250 * st.health_multiplier;
		self->mass = 350;
		self->yaw_speed = 23;
		if (!st.was_key_specified("power_armor_type"))
			self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = 100;
		// PMM - daedalus_bomber sounds
		self->monsterinfo.engine_sound = gi.soundindex("daedalus/daedidle1.wav");
		daed_sound_pain1.assign("daedalus/daedpain1.wav");
		daed_sound_pain2.assign("daedalus/daedpain2.wav");
		daed_sound_death1.assign("daedalus/daeddeth1.wav");
		daed_sound_death2.assign("daedalus/daeddeth2.wav");
		daed_sound_sight.assign("daedalus/daedsght1.wav");
		daed_sound_search1.assign("daedalus/daedsrch1.wav");
		daed_sound_search2.assign("daedalus/daedsrch2.wav");
		gi.soundindex("tank/tnkatck3.wav");
		// pmm
	}
	else
	{
		self->yaw_speed = 18;
		sound_pain1.assign("hover/hovpain1.wav");
		sound_pain2.assign("hover/hovpain2.wav");
		sound_death1.assign("hover/hovdeth1.wav");
		sound_death2.assign("hover/hovdeth2.wav");
		sound_sight.assign("hover/hovsght1.wav");
		sound_search1.assign("hover/hovsrch1.wav");
		sound_search2.assign("hover/hovsrch2.wav");
		gi.soundindex("hover/hovatck1.wav");

		self->monsterinfo.engine_sound = gi.soundindex("hover/hovidle1.wav");
	}
	// PGM

	gi.linkentity(self);

	M_SetAnimation(self, &hover_vanilla_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	flymonster_start(self);

	// PGM
	if (strcmp(self->classname, "monster_daedalus_bomber") == 0)
		self->s.skinnum = 2;
	// PGM

	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	hover_vanilla_set_fly_parameters(self);

	ApplyMonsterBonusFlags(self);
}