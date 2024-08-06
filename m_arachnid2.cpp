// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

TANK

==============================================================================
*/

#include "g_local.h"
#include "m_arachnid.h"
#include "m_flash.h"
#include "shared.h"

static cached_soundindex sound_pain;
static cached_soundindex sound_death;
static cached_soundindex sound_sight;

MONSTERINFO_SIGHT(arachnid2_sight) (edict_t* self, edict_t* other) -> void
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

//
// stand
//

mframe_t arachnid2_frames_stand[] = {
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
MMOVE_T(arachnid2_move_stand) = { FRAME_idle1, FRAME_idle13, arachnid2_frames_stand, nullptr };

MONSTERINFO_STAND(arachnid2_stand) (edict_t* self) -> void
{
	M_SetAnimation(self, &arachnid2_move_stand);
}

//
// walk
//

static cached_soundindex sound_step;

void arachnid2_footstep(edict_t* self)
{
	gi.sound(self, CHAN_BODY, sound_step, 0.5f, ATTN_IDLE, 0.0f);
}

mframe_t arachnid2_frames_walk[] = {
	{ ai_walk, 8, arachnid2_footstep },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8, arachnid2_footstep },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 },
	{ ai_walk, 8 }
};
MMOVE_T(arachnid2_move_walk) = { FRAME_walk1, FRAME_walk10, arachnid2_frames_walk, nullptr };

MONSTERINFO_WALK(arachnid2_walk) (edict_t* self) -> void
{
	M_SetAnimation(self, &arachnid2_move_walk);
}

//
// run
//

mframe_t arachnid2_frames_run[] = {
	{ ai_run, 8, arachnid2_footstep },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8, arachnid2_footstep },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 }
};
MMOVE_T(arachnid2_move_run) = { FRAME_walk1, FRAME_walk10, arachnid2_frames_run, nullptr };

MONSTERINFO_RUN(arachnid2_run) (edict_t* self) -> void
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		M_SetAnimation(self, &arachnid2_move_stand);
		return;
	}

	M_SetAnimation(self, &arachnid2_move_run);
}

//
// pain
//

mframe_t arachnid2_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(arachnid2_move_pain1) = { FRAME_pain11, FRAME_pain15, arachnid2_frames_pain1, arachnid2_run };

mframe_t arachnid2_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(arachnid2_move_pain2) = { FRAME_pain21, FRAME_pain26, arachnid2_frames_pain2, arachnid2_run };

PAIN(arachnid2_pain) (edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3_sec;
	gi.sound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain(self, mod))
		return; // no pain anims in nightmare

	float r = frandom();

	if (r < 0.5f)
		M_SetAnimation(self, &arachnid2_move_pain1);
	else
		M_SetAnimation(self, &arachnid2_move_pain2);
}

MONSTERINFO_SETSKIN(arachnid2_setskin) (edict_t* self) -> void
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

static cached_soundindex sound_charge;

void arachnid2_charge_rail(edict_t* self)
{
	if (!self->enemy || !self->enemy->inuse)
		return;

	gi.sound(self, CHAN_WEAPON, sound_charge, 1.f, ATTN_NORM, 0.f);
	self->pos1 = self->enemy->s.origin;
	self->pos1[2] += self->enemy->viewheight;
}

void arachnid2_rail(edict_t* self)
{
	vec3_t start;
	vec3_t dir;
	vec3_t forward, right;
	monster_muzzleflash_id_t id;

	switch (self->s.frame)
	{
	case FRAME_rails6:
	default:
		id = MZ2_ARACHNID_RAIL1;
		break;
	case FRAME_rails10:
		id = MZ2_ARACHNID_RAIL2;
		break;
	case FRAME_rails_up7:
		id = MZ2_ARACHNID_RAIL_UP1;
		break;
	case FRAME_rails_up11:
		id = MZ2_ARACHNID_RAIL_UP2;
		break;
	}

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);

	// calc direction to where we targeted
	dir = self->pos1 - start;
	dir.normalize();

	monster_fire_railgun(self, start, dir, 50, 100, id);
}

static void gm_arachnid_blind_check(edict_t* self)
{
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		vec3_t aim = self->monsterinfo.blind_fire_target - self->s.origin;
		self->ideal_yaw = vectoyaw(aim);
	}
}

void gm_arachnid_rockets(edict_t* self)
{
	vec3_t						start;
	vec3_t						dir;
	vec3_t						forward, right;
	vec3_t						vec;
	monster_muzzleflash_id_t	id;
	int							rocketSpeed;
	vec3_t						target;
	bool						blindfire = self->monsterinfo.aiflags & AI_MANUAL_STEERING;

	switch (self->s.frame)
	{
	case FRAME_rails4:
	default:
		id = MZ2_ARACHNID_RAIL1;
		break;
	case FRAME_rails8:
		id = MZ2_ARACHNID_RAIL2;
		break;
	case FRAME_rails_up4:
		id = MZ2_ARACHNID_RAIL_UP1;
		break;
	case FRAME_rails_up6:
		id = MZ2_ARACHNID_RAIL_UP2;
		break;
	case FRAME_rails_up10:
		id = MZ2_ARACHNID_RAIL_UP1;
		break;
	case FRAME_rails_up12:
		id = MZ2_ARACHNID_RAIL_UP2;
		break;
	}

	AngleVectors(self->s.angles, forward, right, nullptr);
	start = M_ProjectFlashSource(self, monster_flash_offset[id], forward, right);

	rocketSpeed = 800;

	if (blindfire)
		target = self->monsterinfo.blind_fire_target;
	else
		target = self->enemy->s.origin;

	if (blindfire)
	{
		vec = target;
		dir = vec - start;
	}
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

	if ((!blindfire) && ((frandom() < (0.2f + ((3 - skill->integer) * 0.15f)))))
		PredictAim(self, self->enemy, start, rocketSpeed, false, 0, &dir, &vec);

	dir.normalize();
	//copied from m_chick
	trace_t trace{}; // PMM - check target
	trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
	if (blindfire)
	{
		// blindfire has different fail criteria for the trace
		if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
		{
			// RAFAEL
			if (self->s.skinnum > 1)
				monster_fire_heat(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.075f);
			else
				// RAFAEL
				monster_fire_rocket(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1);
		}
		else
		{
			vec = target;
			vec += (right * -10);
			dir = vec - start;
			dir.normalize();
			trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
			if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
			{
				monster_fire_heat(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.075f);
			}
			else
			{
				vec = target;
				vec += (right * 10);
				dir = vec - start;
				dir.normalize();
				trace = gi.traceline(start, vec, self, MASK_PROJECTILE);
				if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
				{
					monster_fire_heat(self, start, dir, 60, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.075f);
				}
			}
		}
	}
	else
	{
		if (trace.fraction > 0.5f || trace.ent->solid != SOLID_BSP)
		{
			monster_fire_heat(self, start, dir, 50, rocketSpeed, MZ2_CHICK_ROCKET_1, 0.095f);
		}
	}
}

mframe_t arachnid2_frames_attack1[] = {
	{ ai_charge, 0, arachnid2_charge_rail },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, arachnid2_rail },
	{ ai_charge, 0, arachnid2_charge_rail },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, arachnid2_rail },
	{ ai_charge },
	//{ ai_charge },
	//{ ai_charge }
};
MMOVE_T(arachnid2_attack1) = { FRAME_rails3, FRAME_rails11, arachnid2_frames_attack1, arachnid2_run };

mframe_t arachnid2_frames_attack_up1[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, arachnid2_charge_rail },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, arachnid2_rail },
	{ ai_charge, 0, arachnid2_charge_rail },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, arachnid2_rail },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
};
MMOVE_T(arachnid2_attack_up1) = { FRAME_rails_up1, FRAME_rails_up16, arachnid2_frames_attack_up1, arachnid2_run };

mframe_t gm_arachnid_frames_attack1[] = {

	//{ ai_charge },
	//{ ai_charge },
	{ ai_charge, 0, gm_arachnid_blind_check },
	{ ai_charge, 0, gm_arachnid_rockets },
	{ ai_charge },
	{ ai_charge, 0, gm_arachnid_rockets },
	{ ai_charge },
	{ ai_charge, 0, gm_arachnid_rockets },
	{ ai_charge },
	//{ ai_charge, 0, gm_arachnid_rockets },
	//{ ai_charge }
};
MMOVE_T(gm_arachnid_attack1) = { FRAME_rails5, FRAME_rails11, gm_arachnid_frames_attack1, arachnid2_run };

mframe_t gm_arachnid_frames_attack_up1[] = {

	//{ ai_charge },
	//{ ai_charge },
	{ ai_charge, 0, gm_arachnid_blind_check },
	{ ai_charge, 0, gm_arachnid_rockets },
	{ ai_charge },
	{ ai_charge, 0, gm_arachnid_rockets },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gm_arachnid_rockets },
	{ ai_charge },
	{ ai_charge, 0, gm_arachnid_rockets },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
};
MMOVE_T(gm_arachnid_attack_up1) = { FRAME_rails_up3, FRAME_rails_up16, gm_arachnid_frames_attack_up1, arachnid2_run };
static cached_soundindex sound_melee, sound_melee_hit;

void arachnid2_melee_charge(edict_t* self)
{
	gi.sound(self, CHAN_WEAPON, sound_melee, 1.f, ATTN_NORM, 0.f);
}

void arachnid2_melee_hit(edict_t* self)
{
	if (self->style == 1)
	{
		if (!fire_hit(self, { MELEE_DISTANCE, 0, 0 }, 30, 50))
			self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
	}
	else
	{
		if (!fire_hit(self, { MELEE_DISTANCE, 0, 0 }, 20, 50))
			self->monsterinfo.melee_debounce_time = level.time + 1000_ms;
	}
}

mframe_t arachnid2_frames_melee[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, arachnid2_melee_charge },
	{ ai_charge },
	{ ai_charge, 0, arachnid2_melee_hit },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, arachnid2_melee_charge },
	{ ai_charge },
	{ ai_charge, 0, arachnid2_melee_hit },
	{ ai_charge }
};
MMOVE_T(arachnid2_melee) = { FRAME_melee_atk1, FRAME_melee_atk12, arachnid2_frames_melee, arachnid2_run };

MONSTERINFO_ATTACK(arachnid2_attack) (edict_t* self) -> void
{
	if (!self->enemy || !self->enemy->inuse)
		return;
	if (self->monsterinfo.melee_debounce_time < level.time && range_to(self, self->enemy) < MELEE_DISTANCE)
		M_SetAnimation(self, &arachnid2_melee);
	else
	{
		if (self->style == 1)
		{
			if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f)
				M_SetAnimation(self, &gm_arachnid_attack_up1);
			else
				M_SetAnimation(self, &gm_arachnid_attack1);
		}
		else
		{
			if ((self->enemy->s.origin[2] - self->s.origin[2]) > 150.f)
				M_SetAnimation(self, &arachnid2_attack_up1);
			else
				M_SetAnimation(self, &arachnid2_attack1);
		}
	}
}

//
// death
//

void arachnid2_dead(edict_t* self)
{
	self->mins = { -16, -16, -24 };
	self->maxs = { 16, 16, -8 };
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0_ms;
	gi.linkentity(self);
}

mframe_t arachnid2_frames_death1[] = {
	{ ai_move, 0 },
	{ ai_move, -1.23f },
	{ ai_move, -1.23f },
	{ ai_move, -1.23f },
	{ ai_move, -1.23f },
	{ ai_move, -1.64f },
	{ ai_move, -1.64f },
	{ ai_move, -2.45f },
	{ ai_move, -8.63f },
	{ ai_move, -4.0f },
	{ ai_move, -4.5f },
	{ ai_move, -6.8f },
	{ ai_move, -8.0f },
	{ ai_move, -5.4f },
	{ ai_move, -3.4f },
	{ ai_move, -1.9f },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
MMOVE_T(arachnid2_move_death) = { FRAME_death1, FRAME_death20, arachnid2_frames_death1, arachnid2_dead };

DIE(arachnid2_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	OnEntityDeath(self);
	// check for gib
	if (M_CheckGib(self, mod))
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		ThrowGibs(self, damage, {
			{ 2, "models/objects/gibs/bone/tris.md2" },
			{ 2, "models/objects/gibs/sm_meat/tris.md2" },
			{ "models/monsters/gunner/gibs/chest.md2", GIB_METALLIC },
			{ "models/monsters/gunner/gibs/garm.md2", GIB_METALLIC | GIB_UPRIGHT },
			{ "models/monsters/gladiatr/gibs/rarm.md2", GIB_METALLIC | GIB_UPRIGHT },
			{ "models/monsters/gunner/gibs/foot.md2", GIB_METALLIC },
			{ "models/monsters/gunner/gibs/head.md2", GIB_METALLIC | GIB_HEAD }
			});
		self->deadflag = true;
		return;
	}

	if (self->deadflag)
		return;

	// regular death
	gi.sound(self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
	self->deadflag = true;
	self->takedamage = true;

	M_SetAnimation(self, &arachnid2_move_death);

	if (self->spawnflags.has(SPAWNFLAG_IS_BOSS) && !self->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
		BossDeathHandler(self);
	}
}
//
// monster_arachnid2
//

/*QUAKED monster_arachnid2 (1 .5 0) (-48 -48 -20) (48 48 48) Ambush Trigger_Spawn Sight
 */
void SP_monster_arachnid2(edict_t* self)
{
	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return;
	}

	sound_step.assign("insane/insane11.wav");
	sound_charge.assign("gladiator/railgun.wav");
	sound_melee.assign("gladiator/melee3.wav");
	sound_melee_hit.assign("gladiator/melee2.wav");
	sound_pain.assign("arachnid/pain.wav");
	sound_death.assign("arachnid/death.wav");
	sound_sight.assign("arachnid/sight.wav");

	self->s.modelindex = gi.modelindex("models/monsters/arachnid/tris.md2");
	self->mins = { -48, -48, -20 };
	self->maxs = { 48, 48, 48 };
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->monsterinfo.scale = MODEL_SCALE;


	 if (!strcmp(self->classname, "monster_arachnid2") && !self->spawnflags.has(SPAWNFLAG_IS_BOSS)) {
		self->s.scale = 0.85f;
		self->health = 1000 * st.health_multiplier;
		self->mins = { -41, -41, -17 };
		self->maxs = { 41, 41, 41 };
		self->gib_health = -200;
	}
	self->mass = 450;

	self->pain = arachnid2_pain;
	self->die = arachnid2_die;
	self->monsterinfo.stand = arachnid2_stand;
	self->monsterinfo.walk = arachnid2_walk;
	self->monsterinfo.run = arachnid2_run;
	self->monsterinfo.attack = arachnid2_attack;
	self->monsterinfo.sight = arachnid2_sight;
	self->monsterinfo.setskin = arachnid2_setskin;

	gi.linkentity(self);

	M_SetAnimation(self, &arachnid2_move_stand);

	walkmonster_start(self);
	ApplyMonsterBonusFlags(self);
}

void SP_monster_gm_arachnid(edict_t* self)
{
	SP_monster_arachnid2(self);

	self->monsterinfo.armor_type = IT_ARMOR_COMBAT;
	self->monsterinfo.armor_power = 500;
	self->style = 1;
	self->health = 1000 * st.health_multiplier;
	self->s.scale = 0.85f;
	self->mins = { -48, -48, -20 };
	self->maxs = { 48, 48, 48 };

	if (!strcmp(self->classname, "monster_gm_arachnid") && self->spawnflags.has(SPAWNFLAG_IS_BOSS) && !self->spawnflags.has(SPAWNFLAG_BOSS_DEATH_HANDLED)) {
		self->health = 2800 + (1.08 * current_wave_number);
		self->mins = { -41, -41, -17 };
		self->maxs = { 41, 41, 41 };
		self->gib_health = -999777;
	}
	ApplyMonsterBonusFlags(self);
}