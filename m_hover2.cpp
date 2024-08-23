// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

hover2

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
// daedalus2 sounds
static cached_soundindex daed_sound_pain1;
static cached_soundindex daed_sound_pain2;
static cached_soundindex daed_sound_death1;
static cached_soundindex daed_sound_death2;
static cached_soundindex daed_sound_sight;
static cached_soundindex daed_sound_search1;
static cached_soundindex daed_sound_search2;
// ROGUE

MONSTERINFO_SIGHT(hover2_sight) (edict_t* self, edict_t* other) -> void
{
	// PMM - daedalus2 sounds
	if (self->mass < 225)
		gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	else
		gi.sound(self, CHAN_VOICE, daed_sound_sight, 1, ATTN_NORM, 0);
}

MONSTERINFO_SEARCH(hover2_search) (edict_t* self) -> void
{
	// PMM - daedalus2 sounds
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

void hover2_run(edict_t* self);
void hover2_dead(edict_t* self);
void hover2_attack(edict_t* self);
void hover2_reattack(edict_t* self);
void hover2_fire_blaster(edict_t* self);

mframe_t hover2_frames_stand[] = {
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
MMOVE_T(hover2_move_stand) = { FRAME_stand01, FRAME_stand30, hover2_frames_stand, nullptr };

mframe_t hover2_frames_pain3[] = {
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
MMOVE_T(hover2_move_pain3) = { FRAME_pain301, FRAME_pain309, hover2_frames_pain3, hover2_run };

mframe_t hover2_frames_pain2[] = {
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
MMOVE_T(hover2_move_pain2) = { FRAME_pain201, FRAME_pain212, hover2_frames_pain2, hover2_run };

mframe_t hover2_frames_pain1[] = {
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
MMOVE_T(hover2_move_pain1) = { FRAME_pain101, FRAME_pain128, hover2_frames_pain1, hover2_run };

mframe_t hover2_frames_walk[] = {
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
MMOVE_T(hover2_move_walk) = { FRAME_forwrd01, FRAME_forwrd35, hover2_frames_walk, nullptr };

mframe_t hover2_frames_run[] = {
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
MMOVE_T(hover2_move_run) = { FRAME_forwrd01, FRAME_forwrd35, hover2_frames_run, nullptr };

static void hover2_gib(edict_t* self)
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

THINK(hover2_deadthink) (edict_t* self) -> void
{
	if (!self->groundentity && level.time < self->timestamp)
	{
		self->nextthink = level.time + FRAME_TIME_S;
		return;
	}

	hover2_gib(self);
}

void hover2_dying(edict_t* self)
{
	if (self->groundentity)
	{
		hover2_deadthink(self);
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

mframe_t hover2_frames_death1[] = {
	{ ai_move },
	{ ai_move, 0.f, hover2_dying },
	{ ai_move },
	{ ai_move, 0.f, hover2_dying },
	{ ai_move },
	{ ai_move, 0.f, hover2_dying },
	{ ai_move, -10, hover2_dying },
	{ ai_move, 3 },
	{ ai_move, 5, hover2_dying },
	{ ai_move, 4, hover2_dying },
	{ ai_move, 7 }
};
MMOVE_T(hover2_move_death1) = { FRAME_death101, FRAME_death111, hover2_frames_death1, hover2_dead };

mframe_t hover2_frames_start_attack[] = {
	{ ai_charge, 1 },
	{ ai_charge, 1 },
	{ ai_charge, 1 }
};
MMOVE_T(hover2_move_start_attack) = { FRAME_attak101, FRAME_attak103, hover2_frames_start_attack, hover2_attack };

mframe_t hover2_frames_attack1[] = {
	{ ai_charge, -10, hover2_fire_blaster },
	{ ai_charge, -10, hover2_fire_blaster },
	{ ai_charge, 0, hover2_reattack },
};
MMOVE_T(hover2_move_attack1) = { FRAME_attak104, FRAME_attak106, hover2_frames_attack1, nullptr };

mframe_t hover2_frames_end_attack[] = {
	{ ai_charge, 1 },
	{ ai_charge, 1 }
};
MMOVE_T(hover2_move_end_attack) = { FRAME_attak107, FRAME_attak108, hover2_frames_end_attack, hover2_run };

/* PMM - circle strafing code */
#if 0
mframe_t hover2_frames_start_attack2[] = {
	{ ai_charge, 15 },
	{ ai_charge, 15 },
	{ ai_charge, 15 }
};
MMOVE_T(hover2_move_start_attack2) = { FRAME_attak101, FRAME_attak103, hover2_frames_start_attack2, hover2_attack };
#endif

mframe_t hover2_frames_attack2[] = {
	{ ai_charge, 10, hover2_fire_blaster },
	{ ai_charge, 10, hover2_fire_blaster },
	{ ai_charge, 10, hover2_reattack },
};
MMOVE_T(hover2_move_attack2) = { FRAME_attak104, FRAME_attak106, hover2_frames_attack2, nullptr };

#if 0
mframe_t hover2_frames_end_attack2[] = {
	{ ai_charge, 15 },
	{ ai_charge, 15 }
};
MMOVE_T(hover2_move_end_attack2) = { FRAME_attak107, FRAME_attak108, hover2_frames_end_attack2, hover2_run };
#endif

// end of circle strafe

void hover2_reattack(edict_t* self)
{
	if (self->enemy->health > 0)
		if (visible(self, self->enemy))
			if (frandom() <= 0.6f)
			{
				if (self->monsterinfo.attack_state == AS_STRAIGHT)
				{
					M_SetAnimation(self, &hover2_move_attack1);
					return;
				}
				else if (self->monsterinfo.attack_state == AS_SLIDING)
				{
					M_SetAnimation(self, &hover2_move_attack2);
					return;
				}
				else
					gi.Com_PrintFmt("hover2_reattack: unexpected state {}\n", (int32_t)self->monsterinfo.attack_state);
			}
	M_SetAnimation(self, &hover2_move_end_attack);
}
constexpr float MORTAR_SPEED = 1850.f;
constexpr float GRENADE_SPEED = 1600.f;


void hover2_fire_blaster(edict_t* self)
{

	if (strcmp(self->classname, "monster_daedalus2") == 0) {
		vec3_t start;
		vec3_t forward, right, up;
		vec3_t end;
		vec3_t dir{};
		vec3_t aim{};
		float spread = 0.0f;
		float pitch = -3.0f;
		monster_muzzleflash_id_t flash_number;

		if (!self->enemy || !self->enemy->inuse)
			return;

		AngleVectors(self->s.angles, forward, right, up); // Asegúrate de declarar up aquí
		vec3_t o = monster_flash_offset[(self->s.frame & 1) ? MZ2_HOVER_BLASTER_2 : MZ2_HOVER_BLASTER_1];
		start = M_ProjectFlashSource(self, o, forward, right);

		VectorCopy(self->enemy->s.origin, end);
		end[2] += self->enemy->viewheight;
		VectorSubtract(end, start, dir);
		VectorNormalize(dir);

		if (self->mass < 200)
		{
			monster_fire_blaster(self, start, dir, 3, 1000, (self->s.frame & 1) ? MZ2_HOVER_BLASTER_2 : MZ2_HOVER_BLASTER_1, (self->s.frame % 4) ? EF_NONE : EF_HYPERBLASTER);
		}
		else
		{
			// Ajusta la lógica de lanzamiento de granadas para alternar brazos
			flash_number = (self->s.frame & 1) ? MZ2_HOVER_BLASTER_2 : MZ2_HOVER_BLASTER_1;

			VectorCopy(forward, aim);
			VectorMA(aim, spread, right, aim);
			VectorMA(aim, pitch, up, aim);
			VectorNormalize(aim);

			monster_fire_grenade(self, start, aim, 45, (flash_number == MZ2_HOVER_BLASTER_2) ? MORTAR_SPEED : GRENADE_SPEED, flash_number, 10.0f, 7.0f);
		}
	}

	else
	{
		vec3_t	  start;
		vec3_t	  forward, right;
		vec3_t	  end;
		vec3_t	  dir;

		if (!self->enemy || !self->enemy->inuse) // PGM
			return;								 // PGM

		AngleVectors(self->s.angles, forward, right, nullptr);
		vec3_t o = monster_flash_offset[(self->s.frame & 1) ? MZ2_HOVER_BLASTER_2 : MZ2_HOVER_BLASTER_1];
		start = M_ProjectFlashSource(self, o, forward, right);

		end = self->enemy->s.origin;
		end[2] += self->enemy->viewheight;
		dir = end - start;
		dir.normalize();

		// PGM	- daedalus fires blaster2
		if (self->mass < 200)
			monster_fire_blaster(self, start, dir, 1, 1000, (self->s.frame & 1) ? MZ2_HOVER_BLASTER_2 : MZ2_HOVER_BLASTER_1, (self->s.frame % 4) ? EF_NONE : EF_HYPERBLASTER);
		else
			monster_fire_blaster2(self, start, dir, 1, 1000, (self->s.frame & 1) ? MZ2_DAEDALUS_BLASTER_2 : MZ2_DAEDALUS_BLASTER, (self->s.frame % 4) ? EF_NONE : EF_BLASTER);
		// PGM
	}

	
}



MONSTERINFO_STAND(hover2_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &hover2_move_stand);
}

MONSTERINFO_RUN(hover2_run) (edict_t* self) -> void
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		M_SetAnimation(self, &hover2_move_stand);
	else
		M_SetAnimation(self, &hover2_move_run);
}

MONSTERINFO_WALK(hover2_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &hover2_move_walk);
}

MONSTERINFO_ATTACK(hover2_start_attack) (edict_t* self) -> void
{
	M_SetAnimation(self, &hover2_move_start_attack);
}

void hover2_attack(edict_t* self)
{
	float chance = 0.5f;

	if (self->mass > 150) // the daedalus2 strafes more
		chance += 0.1f;

	if (frandom() > chance)
	{
		M_SetAnimation(self, &hover2_move_attack1);
		self->monsterinfo.attack_state = AS_STRAIGHT;
	}
	else // circle strafe
	{
		if (frandom() <= 0.5f) // switch directions
			self->monsterinfo.lefty = !self->monsterinfo.lefty;
		M_SetAnimation(self, &hover2_move_attack2);
		self->monsterinfo.attack_state = AS_SLIDING;
	}
}

PAIN(hover2_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;

	float r = frandom();

	//====
	if (r < 0.5f)
	{
		// PMM - daedalus2 sounds
		if (self->mass < 225)
			gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
		else
			gi.sound(self, CHAN_VOICE, daed_sound_pain1, 1, ATTN_NORM, 0);
	}
	else
	{
		// PMM - daedalus2 sounds
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
			M_SetAnimation(self, &hover2_move_pain3);
		else
			M_SetAnimation(self, &hover2_move_pain2);
	}
	else
	{
		//====
		// PGM pain sequence is WAY too long
		if (r < 0.3f)
			M_SetAnimation(self, &hover2_move_pain1);
		else
			M_SetAnimation(self, &hover2_move_pain2);
		// PGM
		//====
	}
}

MONSTERINFO_SETSKIN(hover2_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1; // PGM support for skins 2 & 3.
	else
		self->s.skinnum &= ~1; // PGM support for skins 2 & 3.
}

void hover2_dead(edict_t* self)
{
	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, -8 };
	self->movetype = MOVETYPE_TOSS;
	self->think = hover2_deadthink;
	self->nextthink = level.time + FRAME_TIME_S;
	self->timestamp = level.time + 15_sec;
	gi.linkentity(self);
}

DIE(hover2_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	OnEntityDeath(self);
	self->s.effects = EF_NONE;
	self->monsterinfo.power_armor_type = IT_NULL;

	if (M_CheckGib(self, mod))
	{
		hover2_gib(self);
		return;
	}

	if (self->deadflag)
		return;

	// regular death
	// PMM - daedalus2 sounds
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
	M_SetAnimation(self, &hover2_move_death1);
}

static void hover2_set_fly_parameters(edict_t* self) {
	if (strcmp(self->classname, "monster_hover2")) {
		{

			self->monsterinfo.fly_thrusters = false;
			self->monsterinfo.fly_acceleration = 20.f;
			self->monsterinfo.fly_speed = 320.f;
			// Icarus prefers to keep its distance, but flies slower than the flyer.
			// he never pins because of this.
			self->monsterinfo.fly_min_distance = 450.f;
			self->monsterinfo.fly_max_distance = 800.f;
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
/*QUAKED monster_hover2 (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 */
 /*QUAKED monster_daedalus2 (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 This is the improved icarus monster.
 */
void SP_monster_hover2(edict_t* self)
{
	if (g_horde->integer && current_wave_number <= 18)
	{
		if (strcmp(self->classname, "monster_daedalus2"))
		{
			float randomsearch = frandom(); // Generar un número aleatorio entre 0 y 1

			if (randomsearch < 0.12f)
				gi.sound(self, CHAN_VOICE, sound_search1, 1, ATTN_NORM, 0);
			else if (randomsearch < 0.24f)
				gi.sound(self, CHAN_VOICE, sound_search2, 1, ATTN_NORM, 0);
			else
				NULL;
		}
		else
		{
			if (!strcmp(self->classname, "monster_daedalus2"))
			{
				float randomsearch = frandom(); // Generar un número aleatorio entre 0 y 1

				if (randomsearch < 0.12f)
					gi.sound(self, CHAN_VOICE, daed_sound_search1, 1, ATTN_NORM, 0);
				else if (randomsearch < 0.24f)
					gi.sound(self, CHAN_VOICE, daed_sound_search2, 1, ATTN_NORM, 0);
				else
					NULL;
			}
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

	self->pain = hover2_pain;
	self->die = hover2_die;

	self->monsterinfo.stand = hover2_stand;
	self->monsterinfo.walk = hover2_walk;
	self->monsterinfo.run = hover2_run;
	self->monsterinfo.attack = hover2_start_attack;
	self->monsterinfo.sight = hover2_sight;
	self->monsterinfo.search = hover2_search;
	self->monsterinfo.setskin = hover2_setskin;

	// PGM
	if (strcmp(self->classname, "monster_daedalus2") == 0)
	{
		self->health = 250 * st.health_multiplier;
		self->mass = 350;
		self->yaw_speed = 23;
		if (!st.was_key_specified("power_armor_type"))
			self->monsterinfo.power_armor_type = IT_ITEM_POWER_SCREEN;
		if (!st.was_key_specified("power_armor_power"))
			self->monsterinfo.power_armor_power = 100;
		// PMM - daedalus2 sounds
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

	M_SetAnimation(self, &hover2_move_stand);
	self->monsterinfo.scale = MODEL_SCALE;

	flymonster_start(self);

	// PGM
	if (strcmp(self->classname, "monster_daedalus2") == 0)
		self->s.skinnum = 2;
	// PGM

	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	hover2_set_fly_parameters(self);

	ApplyMonsterBonusFlags(self);
}