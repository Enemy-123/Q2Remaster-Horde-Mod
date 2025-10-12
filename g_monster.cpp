// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"
#include "horde/g_horde_benefits.h"
#include "horde/g_pvm_menu.h"
#include "bots/bot_includes.h"
#include "shared.h"
#include "monster_constants.h"
#include <algorithm>

//================
// M_CanSpawnMore
// Check if a spawner monster can spawn more monsters
// Takes into account both local slots and global limit
//================
bool M_CanSpawnMore(edict_t* spawner)
{
	// Check if spawner has slots available
	if (M_SlotsLeft(spawner) <= 0)
		return false;

	// Check global limit in horde mode
	if (g_horde->integer) {
		if (level.global_spawned_count >= level.global_spawner_limit)
			return false;
	}

	return true;
}

//
// monster weapons
//
void monster_muzzleflash(edict_t* self, const vec3_t& start, monster_muzzleflash_id_t id)
{
	// CRITICAL: Check entity is valid before writing to prevent misplaced muzzleflashes
	if (!self || !self->inuse)
		return;

	if (id <= 255)
		gi.WriteByte(svc_muzzleflash2);
	else
		gi.WriteByte(svc_muzzleflash3);

	gi.WriteEntity(self);

	if (id <= 255)
		gi.WriteByte(id);
	else
		gi.WriteShort(id);

	gi.multicast(start, MULTICAST_PHS, false);
}

void monster_fire_bullet(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int kick, int hspread,
	int vspread, monster_muzzleflash_id_t flashtype)
{	

	fire_bullet(self, start, dir, damage, kick, hspread, vspread, MOD_CHAINGUN);
	monster_muzzleflash(self, start, flashtype);
}
void monster_fire_energy_bullet(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int kick, int hspread,
	int vspread, monster_muzzleflash_id_t flashtype)
{	

	fire_energy_bullet(self, start, dir, damage, kick, hspread, vspread, MOD_CHAINGUN);
	monster_muzzleflash(self, start, flashtype);
}

void monster_fire_shotgun(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int kick, int hspread,
	int vspread, int count, monster_muzzleflash_id_t flashtype)
{
	fire_shotgun(self, start, aimdir, damage, kick, hspread, vspread, count, MOD_SSHOTGUN);
	monster_muzzleflash(self, start, flashtype);
}

edict_t* monster_fire_blaster(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed,
	monster_muzzleflash_id_t flashtype, effects_t effect)
{
	edict_t* e = fire_blaster(self, start, dir, damage, speed, effect, MOD_BLASTER);
	monster_muzzleflash(self, start, flashtype);
	return e;
}

edict_t* monster_fire_blaster_bolt(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed,
	monster_muzzleflash_id_t flashtype, effects_t effect, int bounces)
{
	// Call fire_blaster_bolt instead of fire_blaster
	// We'll use the default 2 bounces as established in monster_fire_blaster_bolt in g_local.h
	edict_t* e = fire_blaster_bolt(self, start, dir, damage, speed, effect, MOD_BLASTER, bounces);

	// Add the muzzle flash effect, keeping it consistent with monster_fire_blaster
	monster_muzzleflash(self, start, flashtype);

	return e;
}

void monster_fire_flechette(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed,
	monster_muzzleflash_id_t flashtype)
{

	fire_flechette(self, start, dir, damage, speed, damage / 2);
	monster_muzzleflash(self, start, flashtype);
}

void monster_fire_grenade(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int speed,
	monster_muzzleflash_id_t flashtype, float right_adjust, float up_adjust)
{

	fire_grenade(self, start, aimdir, damage, speed, 2.5_sec, damage + 40.f, right_adjust, up_adjust, true);
	monster_muzzleflash(self, start, flashtype);
}

void monster_fire_rocket(edict_t* self, const vec3_t& start, const vec3_t& dir, int damage, int speed,
	monster_muzzleflash_id_t flashtype)
{

	fire_rocket(self, start, dir, damage, speed, static_cast<float>(damage) + 20, damage);
	monster_muzzleflash(self, start, flashtype);
}

bool monster_fire_railgun(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int kick,
	monster_muzzleflash_id_t flashtype)
{
	if (gi.pointcontents(start) & MASK_SOLID)
		return false;


	bool hit = fire_rail(self, start, aimdir, damage, kick);

	monster_muzzleflash(self, start, flashtype);

	return hit;
}


void monster_fire_bfg(edict_t* self, const vec3_t& start, const vec3_t& aimdir, int damage, int speed, int kick,
	float damage_radius, monster_muzzleflash_id_t flashtype)
{

	fire_bfg(self, start, aimdir, damage, speed, damage_radius);
	monster_muzzleflash(self, start, flashtype);
}

// [Paril-KEX]
vec3_t M_ProjectFlashSource(edict_t* self, const vec3_t& offset, const vec3_t& forward, const vec3_t& right)
{
	return G_ProjectSource(self->s.origin, self->s.scale ? (offset * self->s.scale) : offset, forward, right);
}


// [Paril-KEX] check if shots fired from the given offset
// might be blocked by something
bool M_CheckClearShot(edict_t* self, const vec3_t& offset, vec3_t& start)
{
	if (!M_HasValidTarget(self))
	{
		return false; // Stop immediately if the target is invalid.
	}

	vec3_t f, r;

	vec3_t real_angles = { self->s.angles[0], self->ideal_yaw, 0.f };

	AngleVectors(real_angles, f, r, nullptr);
	start = M_ProjectFlashSource(self, offset, f, r);

	vec3_t target;

	bool is_blind = self->monsterinfo.attack_state == AS_BLIND || (self->monsterinfo.aiflags & (AI_MANUAL_STEERING | AI_LOST_SIGHT));

	if (is_blind)
		target = self->monsterinfo.blind_fire_target;
	else
	{
		// Additional safety check before accessing enemy
		if (!self->enemy || !self->enemy->inuse)
			return false;
		target = self->enemy->s.origin + vec3_t{ 0, 0, (float)self->enemy->viewheight };
	}

	trace_t tr = gi.traceline(start, target, self, MASK_PROJECTILE & ~CONTENTS_DEADMONSTER);

	if ((tr.ent && tr.ent == self->enemy) || (tr.ent && tr.ent->client) || (tr.fraction > 0.8f && !tr.startsolid))
		return true;

	if (!is_blind)
	{
		// Additional safety check before accessing enemy again
		if (!self->enemy || !self->enemy->inuse)
			return false;
		target = self->enemy->s.origin;

		trace_t tr = gi.traceline(start, target, self, MASK_PROJECTILE & ~CONTENTS_DEADMONSTER);

		if ((tr.ent && tr.ent == self->enemy) || (tr.ent && tr.ent->client) || (tr.fraction > 0.8f && !tr.startsolid))
			return true;
	}

	return false;
}

bool M_CheckClearShot(edict_t* self, const vec3_t& offset)
{
	vec3_t start;
	return M_CheckClearShot(self, offset, start);
}

void M_CheckGround(edict_t* ent, contents_t mask)
{
	vec3_t	point;
	trace_t trace;

	// [Paril-KEX]
	if (ent->no_gravity_time > level.time)
		return;

	if (ent->flags & (FL_SWIM | FL_FLY))
		return;

	if ((ent->velocity[2] * ent->gravityVector[2]) < -100) // PGM
	{
		ent->groundentity = nullptr;
		return;
	}

	// if the hull point one-quarter unit down is solid the entity is on ground
	point[0] = ent->s.origin[0];
	point[1] = ent->s.origin[1];
	point[2] = ent->s.origin[2] + (0.25f * ent->gravityVector[2]); // PGM

	trace = gi.trace(ent->s.origin, ent->mins, ent->maxs, point, ent, mask);

	// check steepness
	// PGM
	if (ent->gravityVector[2] < 0) // normal gravity
	{
		if (trace.plane.normal[2] < 0.7f && !trace.startsolid)
		{
			ent->groundentity = nullptr;
			return;
		}
	}
	else // inverted gravity
	{
		if (trace.plane.normal[2] > -0.7f && !trace.startsolid)
		{
			ent->groundentity = nullptr;
			return;
		}
	}
	// PGM

	if (!trace.startsolid && !trace.allsolid)
	{
		ent->s.origin = trace.endpos;
		ent->groundentity = trace.ent;
		ent->groundentity_linkcount = trace.ent->linkcount;
		ent->velocity[2] = 0;
	}
}

void M_CatagorizePosition(edict_t* self, const vec3_t& in_point, water_level_t& waterlevel, contents_t& watertype)
{
	vec3_t	   point;
	contents_t cont;

	//
	// get waterlevel
	//
	point[0] = in_point[0];
	point[1] = in_point[1];
	if (self->gravityVector[2] > 0)
		point[2] = in_point[2] + self->maxs[2] - 1;
	else
		point[2] = in_point[2] + self->mins[2] + 1;
	cont = gi.pointcontents(point);

	if (!(cont & MASK_WATER))
	{
		waterlevel = WATER_NONE;
		watertype = CONTENTS_NONE;
		return;
	}

	watertype = cont;
	waterlevel = WATER_FEET;
	point[2] += 26;
	cont = gi.pointcontents(point);
	if (!(cont & MASK_WATER))
		return;

	waterlevel = WATER_WAIST;
	point[2] += 22;
	cont = gi.pointcontents(point);
	if (cont & MASK_WATER)
		waterlevel = WATER_UNDER;
}

bool M_ShouldReactToPain(edict_t* self, const mod_t& mod)
{
	if (!self || !self->inuse)
		return false;

	// Don't react if ducking or at a combat point (specific AI states)
	if (self->monsterinfo.aiflags & (AI_DUCKED | AI_COMBAT_POINT))
		return false;

	// Our summoned monsters are better
	if (self->monsterinfo.bonus_flags & BF_FRIENDLY)
		return false;

	// Horde mode has its own reaction rules
	if (g_horde && g_horde->integer) {
		bool const is_special_weapon = (mod.id == MOD_CHAINFIST || mod.id == MOD_TESLA || mod.id == MOD_TURRET);
		bool const is_early_wave_normal_monster = (current_wave_level <= 10 && !self->monsterinfo.bonus_flags && !self->monsterinfo.IS_BOSS);
		bool const is_boss = self->monsterinfo.IS_BOSS;
		return (is_special_weapon || is_early_wave_normal_monster || is_boss);
	}

	// React to special weapons regardless of skill, or to any weapon if skill is low.
	bool const is_special_weapon = (mod.id == MOD_CHAINFIST || mod.id == MOD_TESLA);
	return is_special_weapon || (skill->integer < 3);
}

void M_WorldEffects(edict_t* ent)
{
	int dmg;
	if (ent->health > 0)
	{
		bool take_drown_damage = false;
		if (!(ent->flags & FL_SWIM))
		{
			if (ent->waterlevel < WATER_UNDER)
				ent->air_finished = level.time + 6_sec;
			else if (ent->air_finished < level.time)
			{
				if (g_horde->integer) {
					if (ent->monsterinfo.IS_BOSS) {
						if (CheckAndTeleportBoss(ent, BossTeleportReason::DROWNING)) {
							ent->air_finished = level.time + 6_sec;
						}
						else {
							take_drown_damage = true;
						}
					}
					else if (ent->svflags & SVF_MONSTER) {
						if (CheckAndTeleportStuckMonster(ent)) {
							ent->air_finished = level.time + 6_sec;
						}
						else {
							take_drown_damage = true;
						}
					}
				}
				else {
					take_drown_damage = true;
				}
			}
		}
		else
		{
			if (ent->waterlevel > WATER_NONE)
				ent->air_finished = level.time + 6_sec;
			else if (ent->air_finished < level.time)
				take_drown_damage = true;
		}

		// Aplicar daño por ahogamiento si corresponde
		if (take_drown_damage && ent->pain_debounce_time < level.time)
		{
	// Drowning damage increases over time.
	dmg = 2 + static_cast<int>(2 * floorf((level.time - ent->air_finished).seconds()));
	if (dmg > 15) {
		// After a certain point, drowning becomes rapidly fatal.
		dmg = 120; 
	}
			T_Damage(ent, world, world, vec3_origin, ent->s.origin, vec3_origin, dmg, 0, DAMAGE_NO_ARMOR,
				MOD_WATER);
			ent->pain_debounce_time = level.time + 1_sec;
		}
	}

	if (ent->waterlevel == WATER_NONE)
	{
		if (ent->flags & FL_INWATER)
		{
			gi.sound(ent, CHAN_BODY, gi.soundindex("player/watr_out.wav"), 1, ATTN_NORM, 0);
			ent->flags &= ~FL_INWATER;
		}
	}
	else
	{
		if ((ent->watertype & CONTENTS_LAVA) && !(ent->flags & FL_IMMUNE_LAVA))
		{
			if (ent->damage_debounce_time < level.time)
			{
				ent->damage_debounce_time = level.time + 100_ms;
				T_Damage(ent, world, world, vec3_origin, ent->s.origin, vec3_origin, 10 * ent->waterlevel, 0, DAMAGE_NONE,
					MOD_LAVA);
			}
		}
		if ((ent->watertype & CONTENTS_SLIME) && !(ent->flags & FL_IMMUNE_SLIME))
		{
			if (ent->damage_debounce_time < level.time)
			{
				ent->damage_debounce_time = level.time + 100_ms;
				T_Damage(ent, world, world, vec3_origin, ent->s.origin, vec3_origin, 4 * ent->waterlevel, 0, DAMAGE_NONE,
					MOD_SLIME);
			}
		}

		if (!(ent->flags & FL_INWATER))
		{
			if (ent->watertype & CONTENTS_LAVA)
			{
				if ((ent->svflags & SVF_MONSTER) && ent->health > 0)
				{
					if (frandom() <= 0.5f)
						gi.sound(ent, CHAN_BODY, gi.soundindex("player/lava1.wav"), 1, ATTN_NORM, 0);
					else
						gi.sound(ent, CHAN_BODY, gi.soundindex("player/lava2.wav"), 1, ATTN_NORM, 0);
				}
				else
					gi.sound(ent, CHAN_BODY, gi.soundindex("player/watr_in.wav"), 1, ATTN_NORM, 0);
			}
			else if (ent->watertype & CONTENTS_SLIME)
				gi.sound(ent, CHAN_BODY, gi.soundindex("player/watr_in.wav"), 1, ATTN_NORM, 0);
			else if (ent->watertype & CONTENTS_WATER)
				gi.sound(ent, CHAN_BODY, gi.soundindex("player/watr_in.wav"), 1, ATTN_NORM, 0);

			ent->flags |= FL_INWATER;
			ent->damage_debounce_time = 0_ms;
		}
	}
}

bool M_droptofloor_generic(vec3_t& origin, const vec3_t& mins, const vec3_t& maxs, bool ceiling, edict_t* ignore, contents_t mask, bool allow_partial)
{
	vec3_t	end;
	trace_t trace;

	// PGM
	// Check if starting inside solid
	trace_t initial_trace = gi.trace(origin, mins, maxs, origin, ignore, mask);
	if (initial_trace.startsolid)
	{
		// Try nudging slightly away from solid
		origin[2] += (ceiling ? -2.0f : 2.0f);
		// Re-check if still inside solid after nudge
		initial_trace = gi.trace(origin, mins, maxs, origin, ignore, mask);
		if (initial_trace.startsolid) {
			// Still stuck after nudge, drop failed
			return false;
		}
	}

	if (!ceiling)
	{
		end = origin;
		end[2] -= 256;
	}
	else
	{
		end = origin;
		end[2] += 256;
	}
	// PGM

	trace = gi.trace(origin, mins, maxs, end, ignore, mask);

	if (trace.fraction == 1 || trace.allsolid || (!allow_partial && trace.startsolid))
		return false;

	origin = trace.endpos;

	return true;
}

bool M_droptofloor(edict_t* ent)
{
	contents_t mask = G_GetClipMask(ent);

	if (!ent->spawnflags.has(SPAWNFLAG_MONSTER_NO_DROP))
	{
		if (!M_droptofloor_generic(ent->s.origin, ent->mins, ent->maxs, ent->gravityVector[2] > 0, ent, mask, true))
			return false;
	}
	else
	{
		if (gi.trace(ent->s.origin, ent->mins, ent->maxs, ent->s.origin, ent, mask).startsolid)
			return false;
	}

	gi.linkentity(ent);
	M_CheckGround(ent, mask);
	M_CatagorizePosition(ent, ent->s.origin, ent->waterlevel, ent->watertype);

	return true;
}

// Basic enemy existence check - use at START of attack functions to allow blindfire logic to execute
// Does NOT check health or visibility - those checks come AFTER blindfire validation
bool M_HasEnemy(edict_t* self)
{
	return self && self->inuse && self->enemy && self->enemy->inuse;
}

// Full target validation - use AFTER blindfire checks, for direct (non-blind) attacks
// Checks enemy health and is suitable for attacks that need a fully valid, living target
bool M_HasValidTarget(edict_t* self)
{
	if (!self || !self->inuse || !self->enemy || !self->enemy->inuse || self->enemy->health <= 0)
	{
		return false;
	}

	// Check for menu protected or fully invisible players
	if (self->enemy->client)
	{
		// Menu protected or fully invisible (fade complete) - treat as invalid target
		if (self->enemy->client->menu_protected ||
			(self->enemy->client->invisible_time > level.time &&
			 self->enemy->client->invisibility_fade_time <= level.time))
		{
			return false;
		}
	}

	return true;
}

// M_SetEffects - Based on original Q2 code with optimizations
// Only updates entity state when effects actually change to reduce network traffic
void M_SetEffects(edict_t* ent)
{
	// Calculate new effects state without modifying entity yet
	effects_t new_effects = ent->s.effects & ~(EF_COLOR_SHELL | EF_POWERSCREEN | EF_DOUBLE | EF_QUAD | EF_PENT | EF_FLIES | EF_DUALFIRE |
		EF_ROCKET | EF_FIREBALL | EF_PLASMA | EF_TAGTRAIL | EF_BLUEHYPERBLASTER |
		EF_GIB | EF_FLAG2 | EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE | EF_TRACKER | EF_FLAG1);
	renderfx_t new_renderfx = ent->s.renderfx & ~(RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_IR_VISIBLE);
	int32_t new_sound = 0;
	int32_t new_loop_attn = 0;
	int current_bonus_flags = 0; // Declare before goto to avoid compiler error

	// Gibbed entities have no effects
	if (ent->s.renderfx & RF_LOW_PRIORITY)
		goto apply_changes;

	// Weapon/engine sounds (alive only)
	if (ent->monsterinfo.weapon_sound && ent->health > 0) {
		new_sound = ent->monsterinfo.weapon_sound;
		new_loop_attn = ATTN_NORM;
	}
	else if (ent->monsterinfo.engine_sound) {
		new_sound = ent->monsterinfo.engine_sound;
		new_loop_attn = ATTN_NORM;
	}

	// Resurrection effect (takes precedence)
	if (ent->monsterinfo.aiflags & AI_RESURRECTING) {
		new_effects |= EF_COLOR_SHELL;
		new_renderfx |= RF_SHELL_RED;
		goto apply_changes;
	}

	// Add shadow for alive monsters
	new_renderfx |= RF_DOT_SHADOW;

	// Dead monsters get no powerup effects
	if (ent->health <= 0)
		goto apply_changes;

	// Bonus flag visuals (mutually exclusive)
	current_bonus_flags = ent->monsterinfo.bonus_flags;
	if (current_bonus_flags & BF_CHAMPION) {
		new_effects |= EF_ROCKET | EF_FIREBALL;
		new_renderfx |= RF_IR_VISIBLE;
	}
	else if (current_bonus_flags & BF_CORRUPTED) {
		new_effects |= EF_PLASMA | EF_TAGTRAIL;
		new_renderfx |= RF_IR_VISIBLE;
	}
	else if (current_bonus_flags & BF_RAGEQUITTER) {
		new_effects |= EF_BLUEHYPERBLASTER;
		new_renderfx |= RF_IR_VISIBLE;
	}
	else if (current_bonus_flags & BF_GHOSTLY) {
		// Alpha is set in ApplyMonsterBonusFlags, no additional effects needed
		new_renderfx |= RF_IR_VISIBLE;
	}
	else if (current_bonus_flags & BF_POSSESSED) {
		new_effects |= EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE;
		new_renderfx |= RF_IR_VISIBLE;
	}
	else if (current_bonus_flags & BF_STYGIAN) {
		new_effects |= EF_TRACKER | EF_FLAG1;
		new_renderfx |= RF_IR_VISIBLE;
	}

	// Friendly units get quad damage visual effect (not mutually exclusive)
	if (current_bonus_flags & BF_FRIENDLY) {
		new_effects |= EF_QUAD;
	}

	// Power armor
	if (ent->powerarmor_time > level.time) {
		if (ent->monsterinfo.power_armor_type == IT_ITEM_POWER_SCREEN) {
			new_effects |= EF_POWERSCREEN;
		}
		else if (ent->monsterinfo.power_armor_type == IT_ITEM_POWER_SHIELD) {
			new_effects |= EF_COLOR_SHELL;
			new_renderfx |= RF_SHELL_GREEN;
		}
	}

	// Powerups (only when expiring/blinking)
	if (ent->monsterinfo.quad_time > level.time && G_PowerUpExpiring(ent->monsterinfo.quad_time))
		new_effects |= EF_QUAD;
	if (ent->monsterinfo.double_time > level.time && G_PowerUpExpiring(ent->monsterinfo.double_time))
		new_effects |= EF_DOUBLE;
	if (ent->monsterinfo.invincible_time > level.time && G_PowerUpExpiring(ent->monsterinfo.invincible_time))
		new_effects |= EF_PENT;
	if (ent->monsterinfo.quadfire_time > level.time && G_PowerUpExpiring(ent->monsterinfo.quadfire_time))
		new_effects |= EF_DUALFIRE;

	// Monster command selection blinking (green shell)
	// Use time-based blinking instead of frame-based for smoother effect
	if (ent->monsterinfo.selected_time > level.time) {
		// Blink every 200ms using integer division
		int64_t time_ms = level.time.milliseconds();
		if ((time_ms / 200) & 1) {
			new_effects |= EF_COLOR_SHELL;
			new_renderfx |= RF_SHELL_GREEN;
		}
	}

apply_changes:
	// Only update entity state if something changed (reduces network traffic)
	if (ent->monsterinfo.cached_effects != new_effects ||
		ent->monsterinfo.cached_renderfx != new_renderfx ||
		ent->monsterinfo.cached_sound != new_sound) {

		ent->s.effects = new_effects;
		ent->s.renderfx = new_renderfx;
		ent->s.sound = new_sound;
		ent->s.loop_attenuation = new_loop_attn;

		// Update cache
		ent->monsterinfo.cached_effects = new_effects;
		ent->monsterinfo.cached_renderfx = new_renderfx;
		ent->monsterinfo.cached_sound = new_sound;
	}
}

bool M_AllowSpawn(edict_t* self) {
	if (G_IsDeathmatch() && !ai_allow_dm_spawn->integer) {
		return false;
	}
	return true;
}

void M_SetAnimation(edict_t* self, const save_mmove_t& move, bool instant)
{
	// [Paril-KEX] free the beams if we switch animations.
	if (self->beam)
	{
		G_FreeEdict(self->beam);
		self->beam = nullptr;
	}

	if (self->beam2)
	{
		G_FreeEdict(self->beam2);
		self->beam2 = nullptr;
	}

	// instant switches will cause active_move to change on the next frame
	if (instant)
	{
		self->monsterinfo.active_move = move;
		self->monsterinfo.next_move = nullptr;
		return;
	}

	// these wait until the frame is ready to be finished
	self->monsterinfo.next_move = move;
}

void LogFrameError(edict_t* self, const mmove_t* move)
{
	gi.Com_PrintFmt("Frame Error: {} frame {} not in range {}-{}\n",
		self->classname,
		self->s.frame,
		move->firstframe,
		move->lastframe);
}
void M_MoveFrame(edict_t* self)
{
	const mmove_t* move = self->monsterinfo.active_move.pointer();

	// [Paril-KEX] high tick rate adjustments;
	// monsters still only step frames and run thinkfunc's at
	// 10hz, but will run aifuncs at full speed with
	// distance spread over 10hz

	// HORDE MOD: Speed up thinking for blocked monsters
	// When monsters are blocked or struggling with movement, make them think faster
	// to reconsider their options more quickly (especially jumping)
	if ((self->monsterinfo.aiflags & AI_BLOCKED) ||
	    (self->monsterinfo.bad_move_time > level.time) ||
	    (self->monsterinfo.bump_time > level.time))
	{
		// Think at 20hz (50ms) when blocked to make jump decisions faster
		self->nextthink = level.time + 50_ms;
	}
	else
	{
		self->nextthink = level.time + FRAME_TIME_S;
	}

	// time to run next 10hz move yet?
	bool run_frame = self->monsterinfo.next_move_time <= level.time;

	// we asked nicely to switch frames when the timer ran up
	if (run_frame && self->monsterinfo.next_move.pointer() && self->monsterinfo.active_move != self->monsterinfo.next_move)
	{
		M_SetAnimation(self, self->monsterinfo.next_move, true);
		move = self->monsterinfo.active_move.pointer();
	}

	if (!move)
		return;

	// no, but maybe we were explicitly forced into another move (pain,
	// death, etc)
	if (!run_frame)
		run_frame = (self->s.frame < move->firstframe || self->s.frame > move->lastframe);

	if (run_frame)
	{
		// [Paril-KEX] allow next_move and nextframe to work properly after an endfunc
		bool explicit_frame = false;

		if ((self->monsterinfo.nextframe) && (self->monsterinfo.nextframe >= move->firstframe) &&
			(self->monsterinfo.nextframe <= move->lastframe))
		{
			self->s.frame = self->monsterinfo.nextframe;
			self->monsterinfo.nextframe = 0;
		}
		else
		{
			if (self->s.frame == move->lastframe)
			{
				if (move->endfunc)
				{
					move->endfunc(self);

					if (self->monsterinfo.next_move)
					{
						M_SetAnimation(self, self->monsterinfo.next_move, true);

						if (self->monsterinfo.nextframe)
						{
							self->s.frame = self->monsterinfo.nextframe;
							self->monsterinfo.nextframe = 0;
							explicit_frame = true;
						}
					}

					// regrab move, endfunc is very likely to change it
					move = self->monsterinfo.active_move.pointer();

					// check for death
					if (self->svflags & SVF_DEADMONSTER)
						return;

					// check if endfunc cleared the move
					if (!move)
						return;
				}
			}

			if (self->s.frame < move->firstframe || self->s.frame > move->lastframe)
			{
				self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
				self->s.frame = move->firstframe;
			}
			else if (!explicit_frame)
			{
				if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME))
				{
					self->s.frame++;
					if (self->s.frame > move->lastframe)
						self->s.frame = move->firstframe;
				}
			}
		}

		if (self->monsterinfo.aiflags & AI_HIGH_TICK_RATE)
			self->monsterinfo.next_move_time = level.time;
		else
			self->monsterinfo.next_move_time = level.time + 10_hz;

		if ((self->monsterinfo.nextframe) && !((self->monsterinfo.nextframe >= move->firstframe) &&
			(self->monsterinfo.nextframe <= move->lastframe)))
			self->monsterinfo.nextframe = 0;
	}

	// NB: frame thinkfunc can be called on the same frame
	// as the animation changing

	int32_t index = self->s.frame - move->firstframe;
	if (move->frame[index].aifunc)
	{
		if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME))
		{
			float dist = move->frame[index].dist * self->monsterinfo.scale;
			dist /= gi.tick_rate / 10;
			move->frame[index].aifunc(self, dist);
		}
		else
			move->frame[index].aifunc(self, 0);
	}

	if (run_frame && move->frame[index].thinkfunc)
		move->frame[index].thinkfunc(self);

	if (move->frame[index].lerp_frame != -1)
	{
		self->s.renderfx |= RF_OLD_FRAME_LERP;
		self->s.old_frame = move->frame[index].lerp_frame;
	}
}

// Helper function to award kill credit and bonuses to a player
static void AwardKillToPlayer(edict_t* player)
{
	if (!player || !player->client)
		return;

	if (G_IsCooperative() || g_horde->integer)
	{
		player->client->resp.score++;
		player->client->resp.spree++;

		// Increment powerup time if player has autohaste bonus
		if (ClassicPlayerHasBenefitAutoHaste(player))
		{
			if (player->client->quadfire_time > level.time)
				player->client->quadfire_time += 0.75_sec;
			if (player->client->double_time > level.time)
				player->client->double_time += 0.5_sec;
			if (player->client->quad_time > level.time)
				player->client->quad_time += 0.5_sec;
		}
	}
}

void G_MonsterKilled(edict_t* self)
{
    if (!(self->monsterinfo.aiflags & AI_DO_NOT_COUNT))
    {
        level.killed_monsters++;
    }

	// Award kill to the player or the owner of the entity that got the kill
    if (self->enemy && self->enemy->client)
    {
        AwardKillToPlayer(self->enemy);

		// Award PvM XP if in PvM mode
		if (pvm->integer)
		{
			// Base XP per monster kill (can be made configurable later)
			constexpr int32_t BASE_MONSTER_XP = 8;
			PvM_AwardExperience(self->enemy, BASE_MONSTER_XP);
		}
    }
    else if (self->enemy && self->enemy->owner && self->enemy->owner->client)
    {
        AwardKillToPlayer(self->enemy->owner);

		// Award PvM XP if in PvM mode
		if (pvm->integer)
		{
			constexpr int32_t BASE_MONSTER_XP = 3;
			PvM_AwardExperience(self->enemy->owner, BASE_MONSTER_XP);
		}
    }
	
	// Debugging: Track monster kills if enabled
	if (g_debug_monster_kills->integer)
	{
		bool found = false;

		// Find the monster in the registered list and mark it as killed (null pointer)
		for (auto& ent : level.monsters_registered)
		{
			if (ent == self)
			{
				ent = nullptr;
				found = true;
				break;
			}
		}

		// If the monster wasn't found in the registered list, it might indicate an issue
		if (!found)
		{
// #if defined(_DEBUG) && defined(KEX_PLATFORM_WINPC)
// 			__debugbreak(); // Trigger debugger breakpoint in debug builds on Windows
// #endif
			// Print a message to the center of the screen for the first player
			gi.Center_Print(&g_edicts[1], "found missing monster?");
		}

		// Check if all counted monsters have been killed
		if (!(self->monsterinfo.aiflags & AI_DO_NOT_COUNT) && level.killed_monsters == level.total_monsters)
		{
			gi.Center_Print(&g_edicts[1], "all monsters dead");
		}
	}
}

void M_ProcessPain(edict_t* e)
{
	// No damage was processed, so there's nothing to do.
	if (!e->monsterinfo.damage_blood)
		return;

	// --- Handle Lethal Damage (Death) ---
	if (e->health <= 0)
	{
		// ROGUE: Cleanup for medic-type monsters
		if (e->monsterinfo.aiflags & AI_MEDIC)
		{
			if (e->enemy && e->enemy->inuse && (e->enemy->svflags & SVF_MONSTER))
			{
				cleanupHealTarget(e->enemy);
			}
			// Clean up self
			e->monsterinfo.aiflags &= ~AI_MEDIC;
		}
		// ROGUE

		bool dead_commander_check = false;

		if (!e->deadflag)
		{
			e->enemy = e->monsterinfo.damage_attacker;

			// ROGUE: Free up a slot for spawned monsters if it was spawned by a commander
			if ((e->monsterinfo.aiflags & AI_SPAWNED_COMMANDER) && !(e->monsterinfo.aiflags & AI_SPAWNED_NEEDS_GIB))
				dead_commander_check = true;

			// Always call G_MonsterKilled on death, unless it was spawned dead.
			// G_MonsterKilled handles the AI_DO_NOT_COUNT logic for level stats.
			if (!(e->spawnflags & SPAWNFLAG_MONSTER_DEAD))
				G_MonsterKilled(e);

			e->touch = nullptr;
			monster_death_use(e); // This function fires deathtarget and healthtarget on death
		}

		// Call the monster's specific death function (e.g., animations, sounds)
		e->die(e, e->monsterinfo.damage_inflictor, e->monsterinfo.damage_attacker, e->monsterinfo.damage_blood, e->monsterinfo.damage_from, e->monsterinfo.damage_mod);

		// [Paril-KEX] Medic commander only gets his slots back after the monster is gibbed, since we can revive them
		if (e->health <= e->gib_health)
		{
			if ((e->monsterinfo.aiflags & AI_SPAWNED_COMMANDER) && (e->monsterinfo.aiflags & AI_SPAWNED_NEEDS_GIB))
				dead_commander_check = true;
		}

		if (dead_commander_check)
		{
			edict_t*& commander = e->monsterinfo.commander;
			if (commander && commander->inuse) {
				commander->monsterinfo.monster_used = max(0, commander->monsterinfo.monster_used - e->monsterinfo.slots_from_commander);
				// Decrement global spawn counter for horde mode
				if (g_horde->integer) {
					level.global_spawned_count = max(0, level.global_spawned_count - 1);
				}
			}
			commander = nullptr;
		}

		// [Paril-KEX] Fix for monsters getting stuck in the last frame of death animation
		if (e->inuse && e->health > e->gib_health && e->s.frame == e->monsterinfo.active_move->lastframe)
		{
			e->s.frame -= irandom(1, 3);
			// Optional: Add slight angle jitter for visual variety on ground death
			if (e->groundentity && e->movetype == MOVETYPE_TOSS && !(e->flags & FL_STATIONARY))
				e->s.angles[1] += brandom() ? 4.5f : -4.5f;
		}
	}
	// --- Handle Non-Lethal Damage (Pain) ---
	else
	{
		// Call the monster's specific pain function
		e->pain(e, e->monsterinfo.damage_attacker, (float)e->monsterinfo.damage_knockback, e->monsterinfo.damage_blood, e->monsterinfo.damage_mod);
	}

	// --- Post-Damage Processing (for both pain and death paths) ---

	// Check if the entity is still in use after pain/death processing
	if (!e->inuse)
		return;

	// Apply skin changes if necessary (e.g., for damage skins)
	if (e->monsterinfo.setskin)
		e->monsterinfo.setskin(e);

	// Reset damage tracking variables for the next frame
	e->monsterinfo.damage_blood = 0;
	e->monsterinfo.damage_knockback = 0;
	e->monsterinfo.damage_attacker = e->monsterinfo.damage_inflictor = nullptr;

	// *** IMPROVEMENT IMPLEMENTED HERE ***
	// Fire healthtarget only if the monster survived the damage and has a healthtarget.
	// This prevents a double-trigger on death, as monster_death_use() already handles it.
	if (e->health > 0 && e->healthtarget)
	{
		const char* original_target = e->target; // Store original target
		e->target = e->healthtarget;             // Temporarily set health target
		G_UseTargets(e, e->enemy);               // Fire health target
		e->target = original_target;             // Restore original target
	}
}

//
// Monster utility functions
//
THINK(monster_dead_think) (edict_t* self) -> void
{
	// Check for spawning state deaths - only when in horde mode
	if (g_horde && g_horde->integer) {
		HandleSpawnPhaseAggression(self);
	}
	// flies
	if ((self->monsterinfo.aiflags & AI_STINKY) && !(self->monsterinfo.aiflags & AI_STUNK))
	{
		if (!self->fly_sound_debounce_time)
			self->fly_sound_debounce_time = level.time + random_time(5_sec, 15_sec);
		else if (self->fly_sound_debounce_time < level.time)
		{
			if (!self->s.sound)
			{
				self->s.effects |= EF_FLIES;
				self->s.sound = gi.soundindex("infantry/inflies1.wav");
				self->fly_sound_debounce_time = level.time + 60_sec;
			}
			else
			{
				self->s.effects &= ~EF_FLIES;
				self->s.sound = 0;
				self->monsterinfo.aiflags |= AI_STUNK;
			}
		}
	}

// Advance death animation until the last frame.
	if (self->s.frame < self->monsterinfo.active_move->lastframe)
	{
		self->s.frame++;
		self->nextthink = level.time + 10_hz;
		return;
	}

	// Pin to the last frame to ensure end-of-animation logic triggers correctly.
	self->s.frame = self->monsterinfo.active_move->lastframe;


	// If we reached the last frame and death hasn't been processed
	if (!self->monsterinfo.death_processed)
	{
		OnEntityDeath(self);  // Process death when animation completes
		self->nextthink = level.time + 10_hz;
		return;
	}

	// If we're here, animation is done and death is processed
	// Check if we have fade effect active
	if ((self->monsterinfo.aiflags & AI_CLEANUP_FADE) &&
		self->teleport_time && level.time >= self->teleport_time)
	{
		float const progress = (level.time - self->teleport_time).seconds() / self->wait;
		float const alpha = 1.0f - progress;
		self->s.alpha = std::max(0.1f, alpha);
	}

	// Check for cleanup time
	if (self->timestamp && level.time >= self->timestamp)
	{
		// For summoned monsters, use BecomeTE for cleaner removal
		if (self->monsterinfo.isfriendlyspawn) {
			// Clean up reference from the base entity if it exists
			if (self->chain && self->chain->inuse && self->chain->teamchain == self) {
				self->chain->teamchain = nullptr;
			}
			BecomeTE(self);
		} else {
			G_FreeEdict(self);
		}
		return;
	}

	self->nextthink = level.time + 10_hz;
}

void monster_dead(edict_t* self)
{
	self->think = monster_dead_think;
	self->nextthink = level.time + 10_hz;
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->monsterinfo.damage_blood = 0;
	self->fly_sound_debounce_time = 0_ms;
	self->monsterinfo.aiflags &= ~AI_STUNK;

	// Decrement global spawner count if this was a spawned monster
	if (g_horde->integer && (self->monsterinfo.aiflags & AI_SPAWNED_COMMANDER)) {
		level.global_spawned_count = std::max(0, level.global_spawned_count - 1);
	}

	// Remove from player's summon tracking array if this was a summoned monster
	if (self->monsterinfo.issummoned && self->chain && self->chain->client) {
		RemoveSummonFromPlayerArray(self);
	}

	if (g_horde->integer) {
		boss_die(self);
	}
	gi.linkentity(self);
}

/*
=============
infront

returns 1 if the entity is in front (in sight) of self
=============
*/
static bool projectile_infront(edict_t* self, edict_t* other)
{
	vec3_t vec;
	float  dot;
	vec3_t forward;

	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	vec = other->s.origin - self->s.origin;
	vec.normalize();
	dot = vec.dot(forward);
	return dot > 0.35f;
}

#include "horde/g_horde_phys.h"
// The new,  M_CheckDodge function
static void M_CheckDodge(edict_t* self)
{
    if (!self || !self->inuse || !self->monsterinfo.dodge) {
        return;
    }

    // we recently made a valid dodge, don't try again for a bit
    if (self->monsterinfo.dodge_time > level.time) {
        return;
    }

    for (auto* ent : active_projectiles())
    {
        // 1. Check distance first (fast elimination)
        static constexpr float MAX_DODGE_DIST_SQ = 512.0f * 512.0f;
        if (DistanceSquared(self->s.origin, ent->s.origin) > MAX_DODGE_DIST_SQ) {
            continue;
        }

        // 2. Is it moving?
        if (ent->velocity.lengthSquared() < VECTOR_LENGTH_SQ_EPSILON) {
            continue;
        }

        // 3. Is it in front of us?
        if (!projectile_infront(self, ent)) {
            continue;
        }

        // 4. Trace its path to see if it will hit us
        vec3_t trace_start = ent->s.origin;
        vec3_t trace_end = ent->s.origin + ent->velocity;
        trace_t tr = gi.trace(trace_start, ent->mins, ent->maxs, trace_end, ent, ent->clipmask);

        if (tr.ent == self)
        {
            // It's going to hit! Calculate ETA and call the dodge function.
            vec3_t v = tr.endpos - trace_start;
            float vel_len = ent->velocity.length();
            gtime_t eta = 0_sec;

            if (vel_len > 0.0f) {
                eta = gtime_t::from_sec(v.length() / vel_len);
            }

            self->monsterinfo.dodge(self, ent->owner, eta, &tr, (ent->movetype == MOVETYPE_BOUNCE || ent->movetype == MOVETYPE_TOSS));
            
            // We've initiated a dodge, no need to check other projectiles this frame.
            return;
        }
    }
}

// Global dodge function for bonus monsters
MONSTERINFO_DODGE(bonus_monster_dodge) (edict_t* self, edict_t* attacker, gtime_t eta, trace_t* tr, bool gravity) -> void
{
	// Basic checks
	if (!self->groundentity || self->health <= 0)
		return;

	// Set enemy if we don't have one
	if (!self->enemy && attacker)
	{
		self->enemy = attacker;
		FoundTarget(self);
		return;
	}

	// Check dodge cooldown using timestamp
	if (self->timestamp > level.time)
		return;

	// Don't dodge if projectile impact is too soon or too far away
	if (eta < FRAME_TIME_MS || eta > 3_sec)
		return;

	// Don't dodge if attacker is invalid
	if (!attacker)
		return;

	// Calculate dodge direction based on attacker position
	vec3_t dodge_dir;

	// Get our right vector for lateral dodge
	vec3_t right;
	AngleVectors(self->s.angles, nullptr, right, nullptr);

	// Decide dodge direction - prefer moving away from attacker
	vec3_t to_attacker = (attacker->s.origin - self->s.origin).normalized();
	float side_dot = to_attacker.dot(right);

	// Dodge perpendicular to attack direction, away from attacker
	if (side_dot > 0)
		dodge_dir = right * -1.0f; // Dodge left
	else
		dodge_dir = right; // Dodge right

	// Add some forward/backward component based on distance
	vec3_t forward;
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	float dist = (self->s.origin - attacker->s.origin).length();

	if (dist < 350.0f) {
		// Close range - dodge backward
		dodge_dir += forward * -0.35f;
	} else if (dist > 700.0f) {
		// Long range - dodge forward to close distance
		dodge_dir += forward * 0.25f;
	}

	dodge_dir = dodge_dir.normalized();

	// Calculate dodge speed based on urgency (eta)
	float base_dodge_speed = 320.0f;
	float eta_seconds = eta.seconds();
	float urgency_multiplier = std::clamp(2.0f - eta_seconds, 1.0f, 2.5f);
	float dodge_speed = base_dodge_speed * urgency_multiplier;

	// Apply dodge velocity
	vec3_t dodge_velocity = dodge_dir * dodge_speed;
	
	// Preserve some vertical momentum but replace horizontal
	dodge_velocity.z = self->velocity.z * 0.5f;
	self->velocity = dodge_velocity;

	// Set cooldown using timestamp
	self->timestamp = level.time + random_time(0.4_sec, 1.3_sec);

	// Also set pausetime for movement consistency
	self->monsterinfo.pausetime = level.time + random_time(0.3_sec, 0.7_sec);

	// Update lefty for consistency with sidestep
	self->monsterinfo.lefty = (side_dot > 0) ? 1 : 0;

	// Mark that we're dodging
	monster_done_dodge(self);
}

static bool CheckPathVisibility(const vec3_t& start, const vec3_t& end)
{
	trace_t tr = gi.traceline(start, end, nullptr, MASK_SOLID | CONTENTS_PROJECTILECLIP | CONTENTS_MONSTERCLIP | CONTENTS_PLAYERCLIP);

	bool valid = tr.fraction == 1.0f;

	if (!valid)
	{
		// try raising some of the points
		bool can_raise_start = false, can_raise_end = false;
		vec3_t raised_start = start + vec3_t{ 0.f, 0.f, 16.f };
		vec3_t raised_end = end + vec3_t{ 0.f, 0.f, 16.f };

		if (gi.traceline(start, raised_start, nullptr, MASK_SOLID | CONTENTS_PROJECTILECLIP | CONTENTS_MONSTERCLIP | CONTENTS_PLAYERCLIP).fraction == 1.0f)
			can_raise_start = true;

		if (gi.traceline(end, raised_end, nullptr, MASK_SOLID | CONTENTS_PROJECTILECLIP | CONTENTS_MONSTERCLIP | CONTENTS_PLAYERCLIP).fraction == 1.0f)
			can_raise_end = true;

		// try raised start -> end
		if (can_raise_start)
		{
			tr = gi.traceline(raised_start, end, nullptr, MASK_SOLID | CONTENTS_PROJECTILECLIP | CONTENTS_MONSTERCLIP | CONTENTS_PLAYERCLIP);

			if (tr.fraction == 1.0f)
				return true;
		}

		// try start -> raised end
		if (can_raise_end)
		{
			tr = gi.traceline(start, raised_end, nullptr, MASK_SOLID | CONTENTS_PROJECTILECLIP | CONTENTS_MONSTERCLIP | CONTENTS_PLAYERCLIP);

			if (tr.fraction == 1.0f)
				return true;
		}

		// try both raised
		if (can_raise_start && can_raise_end)
		{
			tr = gi.traceline(raised_start, raised_end, nullptr, MASK_SOLID | CONTENTS_PROJECTILECLIP | CONTENTS_MONSTERCLIP | CONTENTS_PLAYERCLIP);

			if (tr.fraction == 1.0f)
				return true;
		}

		//gi.Draw_Line(start, end, rgba_red, 0.1f, false);
	}

	return valid;
}

THINK(monster_think) (edict_t* self) -> void
{
    // Check if the monster's ID is uninitialized (using UNKNOWN as the sentinel value).
    // This catches monsters spawned without an explicit ID set in their SP_... function.
    if (self->monsterinfo.monster_type_id == static_cast<uint8_t>(horde::MonsterTypeID::UNKNOWN)) 
    {
        // Attempt to assign the ID based on its classname.
        self->monsterinfo.monster_type_id = static_cast<uint8_t>(horde::MonsterTypeRegistry::GetTypeID(self->classname));

        // Check the result of the assignment.
        if (self->monsterinfo.monster_type_id != static_cast<uint8_t>(horde::MonsterTypeID::UNKNOWN))
        {
            // SUCCESS: We found a valid ID. Log this for the developer.
            if (developer->integer) {
                gi.Com_PrintFmt(
                    "LAZY-INIT: Assigned monster ID for #{} (classname '{}') to {}\n",
                    static_cast<int>(self - g_edicts), 
                    self->classname, 
                    self->monsterinfo.monster_type_id
                );
            }
        }
        else
        {
            // FAILURE: The classname is not in the registry. This is a critical warning.
            // We log this regardless of the 'developer' cvar because it's an error.
            gi.Com_PrintFmt("WARNING: UNKNOWN MONSTER - Classname '{}' (#{}) has no registered MonsterTypeID.\n", self->classname, static_cast<int>(self - g_edicts)); // FIX: self.classname -> self->classname
        }
    }

	// Check horde stuck monster
	if (g_horde->integer)
	{
		CheckAndTeleportStuckMonster(self);
	}

	// [Paril-KEX] monster sniff testing; if we can make an unobstructed path to the player, murder ourselves.
	if (g_debug_monster_kills->integer)
	{
		if (g_edicts[1].inuse)
		{
			trace_t enemy_trace = gi.traceline(self->s.origin, g_edicts[1].s.origin, self, MASK_SHOT);

			if (enemy_trace.fraction < 1.0f && enemy_trace.ent == &g_edicts[1])
			{
				T_Damage(self, &g_edicts[1], &g_edicts[1], { 0, 0, -1 }, self->s.origin, { 0, 0, -1 }, 9999, 9999, DAMAGE_NO_PROTECTION, MOD_BFG_BLAST);
			}
			else
			{
				static vec3_t points[64];

				if (self->disintegrator_time <= level.time)
				{
					PathRequest request;
					request.goal = g_edicts[1].s.origin;
					request.moveDist = 4.0f;
					request.nodeSearch.ignoreNodeFlags = true;
					request.nodeSearch.radius = 9999;
					request.pathFlags = PathFlags::All;
					request.start = self->s.origin;
					request.traversals.dropHeight = 9999;
					request.traversals.jumpHeight = 9999;
					request.pathPoints.array = points;
					request.pathPoints.count = q_countof(points);

					PathInfo info;

					if (gi.GetPathToGoal(request, info))
					{
						if (info.returnCode != PathReturnCode::NoStartNode &&
							info.returnCode != PathReturnCode::NoGoalNode &&
							info.returnCode != PathReturnCode::NoPathFound &&
							info.returnCode != PathReturnCode::NoNavAvailable &&
							info.numPathPoints >= 0 &&
							static_cast<size_t>(info.numPathPoints) < q_countof(points))
						{
							const size_t num_points = info.numPathPoints;

							if (CheckPathVisibility(g_edicts[1].s.origin + vec3_t{ 0.f, 0.f, g_edicts[1].mins.z }, points[num_points - 1]) &&
								CheckPathVisibility(self->s.origin + vec3_t{ 0.f, 0.f, self->mins.z }, points[0]))
							{
								size_t i = 0;

								for (; i < num_points - 1; i++)
								{
									if (!CheckPathVisibility(points[i], points[i + 1]))
										break;
								}

								if (i == num_points - 1)
								{
									T_Damage(self, &g_edicts[1], &g_edicts[1], { 0, 0, 1 }, self->s.origin, { 0, 0, 1 }, 9999, 9999, DAMAGE_NO_PROTECTION, MOD_BFG_BLAST);
								}
								else
								{
									self->disintegrator_time = level.time + 500_ms;
								}
							}
							else
							{
								self->disintegrator_time = level.time + 500_ms;
							}
						}
						else
						{
							self->disintegrator_time = level.time + 1_sec;
						}
					}
				}
			}

			if (!self->deadflag && !(self->monsterinfo.aiflags & AI_DO_NOT_COUNT))
			{
				gi.Draw_Bounds(self->absmin, self->absmax, rgba_red, gi.frame_time_s, false);
			}
		}
	}

	self->s.renderfx &= ~(RF_STAIR_STEP | RF_OLD_FRAME_LERP);

	M_ProcessPain(self);

	// pain/die above may have freed the entity
	if (!self->inuse || self->think != monster_think)
	{
		return;
	}

	// Monster upkeep system - drain 1 cube per second per summoned monster (asynchronous)
	// Each monster has its own timer, so they drain independently based on spawn time
	if (g_horde->integer && self->monsterinfo.issummoned && self->chain && self->chain->client)
	{
		// Check if it's time to drain a cube (1 second interval)
		if (level.time >= self->monsterinfo.upkeep_time)
		{
			edict_t* owner = self->chain;

			// Try to drain 1 cube from owner
			if (owner->client->pers.horde_power_cubes > 0)
			{
				owner->client->pers.horde_power_cubes -= 1;
			}
			else
			{
				// Owner can't pay upkeep - remove this monster
				gi.LocClient_Print(owner, PRINT_HIGH, "Couldn't keep up the cost - Removing summon!\n");
				T_Damage(self, world, world, vec3_origin, self->s.origin,
					vec3_origin, 99999, 0, DAMAGE_NO_PROTECTION, MOD_TELEFRAG);
				return; // Monster is being removed, exit early
			}

			// Set next upkeep time (1 second from now)
			self->monsterinfo.upkeep_time = level.time + 1_sec;
		}
	}

	// Stygian/Friendly Health Regeneration
	//  check order for fast early-out. The timer check is first as it fails most often.
	if (level.time >= self->monsterinfo.next_regen_time &&
		self->health > 0 &&
		(self->monsterinfo.bonus_flags & (BF_STYGIAN | BF_FRIENDLY)) &&
		self->health < self->max_health &&
		!self->monsterinfo.IS_BOSS)
	{
		// Regenerate 8% of max health per interval, minimum 1 HP
		const int REGEN_AMOUNT = std::max(1, static_cast<int>(self->max_health * 0.08f));
		constexpr gtime_t REGEN_INTERVAL = 5_sec; // Regenerate every 5 seconds

		self->health = std::min(self->health + REGEN_AMOUNT, self->max_health);
		self->monsterinfo.next_regen_time = level.time + REGEN_INTERVAL;
	}

	if (self->hackflags & HACKFLAG_ATTACK_PLAYER)
	{
		if (!self->enemy && g_edicts[1].inuse) // FIX: self.enemy -> self->enemy
		{
			self->enemy = &g_edicts[1];
			FoundTarget(self);
		}
	}

	if (self->health > 0 && self->monsterinfo.dodge && !(globals.server_flags & SERVER_FLAG_LOADING))
	{
		M_CheckDodge(self);
	}

	M_MoveFrame(self);
	if (self->linkcount != self->monsterinfo.linkcount)
	{
		self->monsterinfo.linkcount = self->linkcount;
		M_CheckGround(self, G_GetClipMask(self));
	}
	M_CatagorizePosition(self, self->s.origin, self->waterlevel, self->watertype);
	M_WorldEffects(self);
	M_SetEffects(self);

	// Update skin based on health (needed for regeneration, healing, etc.)
	if (self->monsterinfo.setskin)
		self->monsterinfo.setskin(self);
}

/*
================
monster_use

Using a monster makes it angry at the current activator
================
*/
USE(monster_use) (edict_t* self, edict_t* other, edict_t* activator) -> void
{
	if (self->enemy)
		return;
	if (self->health <= 0)
		return;
	if (!activator)
		return;
	if (activator->flags & FL_NOTARGET)
		return;
	if (!(activator->client) && !(activator->monsterinfo.aiflags & AI_GOOD_GUY))
		return;
	if (activator->flags & FL_DISGUISED) // PGM
		return;							 // PGM

	// delay reaction so if the monster is teleported, its sound is still heard
	self->enemy = activator;
	FoundTarget(self);
}

void monster_start_go(edict_t* self);

THINK(monster_triggered_spawn) (edict_t* self) -> void
{
	self->s.origin[2] += 1;

	self->solid = SOLID_BBOX;
	self->movetype = MOVETYPE_STEP;
	self->svflags &= ~SVF_NOCLIENT;
	self->air_finished = level.time + 12_sec;
	gi.linkentity(self);

	KillBox(self, false);

	monster_start_go(self);

	// RAFAEL
	//if (strcmp(self->classname, "monster_fixbot") == 0)
	//{
	//	if (self->spawnflags.has(SPAWNFLAG_FIXBOT_LANDING | SPAWNFLAG_FIXBOT_TAKEOFF | SPAWNFLAG_FIXBOT_FIXIT))
	//	{
	//		self->enemy = nullptr;
	//		return;
	//	}
	//}
	// RAFAEL

	if (self->enemy && !(self->spawnflags & SPAWNFLAG_MONSTER_AMBUSH) && !(self->enemy->flags & FL_NOTARGET) && !(self->monsterinfo.aiflags & AI_GOOD_GUY))
	{
		// ROGUE
		if (!(self->enemy->flags & FL_DISGUISED))
			// ROGUE
			FoundTarget(self);
		// ROGUE
		else // PMM - just in case, make sure to clear the enemy so FindTarget doesn't get confused
			self->enemy = nullptr;
		// ROGUE
	}
	else
	{
		self->enemy = nullptr;
	}
}

USE(monster_triggered_spawn_use) (edict_t* self, edict_t* other, edict_t* activator) -> void
{
	// we have a one frame delay here so we don't telefrag the guy who activated us
	self->think = monster_triggered_spawn;
	self->nextthink = level.time + FRAME_TIME_S;
	if (activator && activator->client && !(self->hackflags & HACKFLAG_END_CUTSCENE))
		self->enemy = activator;
	self->use = monster_use;

	if (self->spawnflags.has(SPAWNFLAG_MONSTER_SCENIC))
	{
		M_droptofloor(self);

		self->nextthink = 0_ms;
		self->think(self);

		if (self->spawnflags.has(SPAWNFLAG_MONSTER_AMBUSH))
			monster_use(self, other, activator);

		for (int i = 0; i < 30; i++)
		{
			self->think(self);
			self->monsterinfo.next_move_time = 0_ms;
		}
	}
}

THINK(monster_triggered_think) (edict_t* self) -> void
{
	if (!(self->monsterinfo.aiflags & AI_DO_NOT_COUNT))
		gi.Draw_Bounds(self->absmin, self->absmax, rgba_blue, gi.frame_time_s, false);

	self->nextthink = level.time + 1_ms;
}

void monster_triggered_start(edict_t* self)
{
	self->solid = SOLID_NOT;
	self->movetype = MOVETYPE_NONE;
	self->svflags |= SVF_NOCLIENT;
	self->nextthink = 0_ms;
	self->use = monster_triggered_spawn_use;

	// line to exclude trigger-spawned monsters from count in Horde mode
	if (g_horde->integer)
		self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	if (g_debug_monster_kills->integer)
	{
		self->think = monster_triggered_think;
		self->nextthink = level.time + 1_ms;
	}

	if (!self->targetname ||
		(G_FindByString<&edict_t::target>(nullptr, self->targetname) == nullptr &&
			G_FindByString<&edict_t::pathtarget>(nullptr, self->targetname) == nullptr &&
			G_FindByString<&edict_t::deathtarget>(nullptr, self->targetname) == nullptr &&
			G_FindByString<&edict_t::itemtarget>(nullptr, self->targetname) == nullptr &&
			G_FindByString<&edict_t::healthtarget>(nullptr, self->targetname) == nullptr &&
			G_FindByString<&edict_t::combattarget>(nullptr, self->targetname) == nullptr))
	{
		gi.Com_PrintFmt("{}: is trigger spawned, but has no targetname or no entity to spawn it\n", *self);
	}
}

/*
================
monster_death_use

When a monster dies, it fires all of its targets with the current
enemy as activator.
================
*/
void monster_death_use(edict_t* self)
{
	self->flags &= ~(FL_FLY | FL_SWIM);
	self->monsterinfo.aiflags &= AI_DEATH_MASK;

	if (self->item)
	{
		edict_t* dropped = Drop_Item(self, self->item);

		if (self->itemtarget)
		{
			dropped->target = self->itemtarget;
			self->itemtarget = nullptr;
		}

		self->item = nullptr;
	}

	if (self->deathtarget)
		self->target = self->deathtarget;

	if (self->target)
		G_UseTargets(self, self->enemy);

	// [Paril-KEX] fire health target
	if (self->healthtarget)
	{
		const char* target = self->target;
		self->target = self->healthtarget;
		G_UseTargets(self, self->enemy);
		self->target = target;
	}
}

// [Paril-KEX] adjust the monster's health from how
// many active players we have
void G_Monster_ScaleCoopHealth(edict_t* self)
{
	// No escalar si es un jefe
	if (self->monsterinfo.IS_BOSS)
		return;;

	// No escalar si es una entidad amigable (como sentry guns)
	if (self->monsterinfo.bonus_flags & BF_FRIENDLY)
		return;;

	// already scaled
	if (self->monsterinfo.health_scaling >= level.coop_scale_players)
		return;

	// this is just to fix monsters that change health after spawning...
	// looking at you, soldiers
	if (!self->monsterinfo.base_health)
		self->monsterinfo.base_health = self->max_health;

	const int32_t delta = level.coop_scale_players - self->monsterinfo.health_scaling;
	const int32_t additional_health = delta * (int32_t)(self->monsterinfo.base_health * level.coop_health_scaling);

	self->health = max(1, self->health + additional_health);
	self->max_health += additional_health;

	self->monsterinfo.health_scaling = level.coop_scale_players;
}

struct monster_filter_t
{
	inline bool operator()(edict_t* self) const
	{
		return self->inuse && (self->flags & FL_COOP_HEALTH_SCALE) && self->health > 0;
	}
};

// check all active monsters' scaling
void G_Monster_CheckCoopHealthScaling()
{
	
	for (auto monster : entity_iterable_t<monster_filter_t>())
	{
		// No escalar si es un jefe
		if (monster->monsterinfo.IS_BOSS)
			continue;

		// No escalar si es una entidad amigable (como sentry guns)
		if (monster->monsterinfo.bonus_flags & BF_FRIENDLY)
			continue;

		// Aplicar el escalado
		G_Monster_ScaleCoopHealth(monster);
	}
}

//============================================================================
constexpr spawnflags_t SPAWNFLAG_MONSTER_FUBAR = 4_spawnflag;

bool monster_start(edict_t* self, const spawn_temp_t& st)
{
	if (!M_AllowSpawn(self)) {
		G_FreeEdict(self);
		return false;
	}

	if (g_horde && g_horde->integer && (!(self->monsterinfo.isfriendlyspawn))) {
	    if (self->monsterinfo.team == CTF_NOTEAM)
    {
        // If no team is set, assign it to the default enemy team.
        self->monsterinfo.team = CTF_TEAM2;
    }
}

	//     if (g_horde && g_horde->integer && !self->monsterinfo.was_spawned_by_horde)
    // {
    //     self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
    // }

	if (self->spawnflags.has(SPAWNFLAG_MONSTER_SCENIC))
		self->monsterinfo.aiflags |= AI_GOOD_GUY;

	// [Paril-KEX] n64
	if (self->hackflags & (HACKFLAG_END_CUTSCENE | HACKFLAG_ATTACK_PLAYER))
		self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	if (self->spawnflags.has(SPAWNFLAG_MONSTER_FUBAR) && !(self->monsterinfo.aiflags & AI_GOOD_GUY))
	{
		self->spawnflags &= ~SPAWNFLAG_MONSTER_FUBAR;
		self->spawnflags |= SPAWNFLAG_MONSTER_AMBUSH;
	}

	// [Paril-KEX] simplify other checks
	if (self->monsterinfo.aiflags & AI_GOOD_GUY)
		self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	// player's summoned monsters won't have count // Horde
	if (self->monsterinfo.isfriendlyspawn)
		self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	// ROGUE
	if (!(self->monsterinfo.aiflags & AI_DO_NOT_COUNT) && !self->spawnflags.has(SPAWNFLAG_MONSTER_DEAD))
	{
		if (g_debug_monster_kills->integer)
			level.monsters_registered[level.total_monsters] = self;
		// ROGUE
		level.total_monsters++;
	}

	self->nextthink = level.time + FRAME_TIME_S;
	self->svflags |= SVF_MONSTER;
	self->takedamage = true;
	self->air_finished = level.time + 6_sec;
	self->use = monster_use;
	self->max_health = self->health;
	self->clipmask = MASK_MONSTERSOLID;
	self->deadflag = false;
	self->svflags &= ~SVF_DEADMONSTER;
	self->flags &= ~FL_ALIVE_KNOCKBACK_ONLY;
	self->flags |= FL_COOP_HEALTH_SCALE;
	self->s.old_origin = self->s.origin;
	self->monsterinfo.initial_power_armor_type = self->monsterinfo.power_armor_type;
	self->monsterinfo.max_power_armor_power = self->monsterinfo.power_armor_power;

	if (!self->monsterinfo.checkattack)
		self->monsterinfo.checkattack = M_CheckAttack;

	// Apply monster type specific scale from centralized data if not already set
	if (!self->s.scale && self->monsterinfo.monster_type_id != MONSTER_TYPE_UNKNOWN)
	{
		const size_t index = static_cast<size_t>(self->monsterinfo.monster_type_id);
		if (index < g_monsterData.MONSTER_ARRAY_SIZE)
		{
			const float type_scale = g_monsterData.s_scales[index];
			if (type_scale > 0.0f)
				self->s.scale = type_scale;
		}
	}

	// Override with debug scale if set
	if (ai_model_scale->value > 0) {
		self->s.scale = ai_model_scale->value;
	}

	if (self->s.scale)
	{
		const float original_mins_z = self->mins[2];  // Save unscaled value for height adjustment

		self->monsterinfo.scale *= self->s.scale;
		self->mins *= self->s.scale;
		self->maxs *= self->s.scale;
		self->mass *= self->s.scale;

		// Adjust origin to prevent floating (uses original unscaled mins[2])
		// When scaling up, scaled mins extends further down, so raise origin to compensate
		self->s.origin[2] -= (original_mins_z * self->s.scale - original_mins_z);
	}

	if (pm_config.physics_flags & PHYSICS_PSX_SCALE)
		self->s.origin[2] -= self->mins[2] - (self->mins[2] * PSX_PHYSICS_SCALAR);

	// set combat style if unset
	if (self->monsterinfo.combat_style == COMBAT_UNKNOWN)
	{
		if (!self->monsterinfo.attack && self->monsterinfo.melee)
			self->monsterinfo.combat_style = COMBAT_MELEE;
		else
			self->monsterinfo.combat_style = COMBAT_MIXED;
	}

	if (st.item)
	{
		self->item = FindItemByClassname(st.item);
		if (!self->item)
			gi.Com_PrintFmt("{}: bad item: {}\n", *self, st.item);
	}

	// randomize what frame they start on
	if (self->monsterinfo.active_move)
		self->s.frame =
		irandom(self->monsterinfo.active_move->firstframe, self->monsterinfo.active_move->lastframe + 1);

	// PMM - get this so I don't have to do it in all of the monsters
	self->monsterinfo.base_height = self->maxs[2];

	// Paril: monsters' old default viewheight (25)
	// is all messed up for certain monsters. Calculate
	// from maxs to make a bit more sense.
	if (!self->viewheight)
		self->viewheight = static_cast<int>(self->maxs[2] - 8.f);

	// PMM - clear these
	self->monsterinfo.quad_time = 0_ms;
	self->monsterinfo.double_time = 0_ms;
	self->monsterinfo.invincible_time = 0_ms;

	// set base health & set base scaling to 1 player
	self->monsterinfo.base_health = self->health;
	self->monsterinfo.health_scaling = 1;

	// [Paril-KEX] co-op health scale
	G_Monster_ScaleCoopHealth(self);

	// set vision cone
	if (!st.was_key_specified("vision_cone"))
	{
		self->vision_cone = -2.0f; // special value to use old algorithm
	}

	return true;
}

stuck_result_t G_FixStuckObject(edict_t *self, vec3_t check)
{
	contents_t mask = G_GetClipMask(self);
	stuck_result_t result = G_FixStuckObject_Generic(check, self->mins, self->maxs, [self, mask] (const vec3_t &start, const vec3_t &mins, const vec3_t &maxs, const vec3_t &end) {
		return gi.trace(start, mins, maxs, end, self, mask);
	});

	if (result == stuck_result_t::NO_GOOD_POSITION)
		return result;

	self->s.origin = check;

	if (result == stuck_result_t::FIXED && developer->integer)
		gi.Com_PrintFmt("fixed stuck {}\n", *self);

	return result;
}

bool Horde_AttemptToUnstickMonster(edict_t* self);

void monster_start_go(edict_t *self)
{
	// Initialize anti-stacking system for horde mode
	if (g_horde->integer && (self->svflags & SVF_MONSTER))
	{
		InitMonsterAntiStack(self);
	}

	// Global jump properties for all monsters (unless already set)
	if (self->monsterinfo.drop_height == 0)
		self->monsterinfo.drop_height = 256;
	if (self->monsterinfo.jump_height == 0)
		self->monsterinfo.jump_height = 48;  // Standard jump height for all monsters
	if (!self->monsterinfo.can_jump)
		self->monsterinfo.can_jump = true;

	// Auto-setup armor from config if not manually set
	const spawn_temp_t& st = ED_GetSpawnTemp();
	if (!st.was_key_specified("armor_type") && self->monsterinfo.armor_type == IT_NULL) {
		item_id_t config_armor_type = M_ARMOR_TYPE(self);
		if (config_armor_type != IT_NULL) {
			self->monsterinfo.armor_type = config_armor_type;
			if (!st.was_key_specified("armor_power")) {
				self->monsterinfo.armor_power = M_ADDON_ARMOR(self);
			}
		}
	}

	// Auto-setup power armor from config if not manually set
	if (!st.was_key_specified("power_armor_type") && self->monsterinfo.power_armor_type == IT_NULL) {
		item_id_t config_power_armor_type = M_POWER_ARMOR_TYPE(self);
		if (config_power_armor_type != IT_NULL) {
			self->monsterinfo.power_armor_type = config_power_armor_type;
			if (!st.was_key_specified("power_armor_power")) {
				self->monsterinfo.power_armor_power = M_ADDON_POWER_ARMOR(self);
			}
		}
	}

	// Paril: moved here so this applies to swim/fly monsters too
	if (!(self->flags & FL_STATIONARY))
	{
		const vec3_t check = self->s.origin;

		// [Paril-KEX] different nudge method; see if any of the bbox sides are clear,
		// if so we can see how much headroom we have in that direction and shift us.
		// most of the monsters stuck in solids will only be stuck on one side, which
		// conveniently leaves only one side not in a solid; this won't fix monsters
		// stuck in a corner though.
		bool is_stuck = false;

		if ((self->monsterinfo.aiflags & AI_GOOD_GUY) || (self->flags & (FL_FLY | FL_SWIM)))
			is_stuck = gi.trace(self->s.origin, self->mins, self->maxs, self->s.origin, self, MASK_MONSTERSOLID).startsolid;
		else
			is_stuck = !M_droptofloor(self) || !M_walkmove(self, 0, 0);

		if (is_stuck)
		{
			if (G_FixStuckObject(self, check) != stuck_result_t::NO_GOOD_POSITION)
			{
				if (self->monsterinfo.aiflags & AI_GOOD_GUY)
					is_stuck = gi.trace(self->s.origin, self->mins, self->maxs, self->s.origin, self, MASK_MONSTERSOLID).startsolid;
				else if (!(self->flags & (FL_FLY | FL_SWIM)))
					M_droptofloor(self);
				is_stuck = false;
			}
		}

		// last ditch effort: brute force
		if (is_stuck)
		{
			// Paril: try nudging them out. this fixes monsters stuck
			// in very shallow slopes.
			constexpr const int32_t adjust[] = { 0, -1, 1, -2, 2, -4, 4, -8, 8 };
			bool					walked = false;

			for (int32_t y = 0; !walked && y < 3; y++)
				for (int32_t x = 0; !walked && x < 3; x++)
					for (int32_t z = 0; !walked && z < 3; z++)
					{
						self->s.origin[0] = check[0] + adjust[x];
						self->s.origin[1] = check[1] + adjust[y];
						self->s.origin[2] = check[2] + adjust[z];
						
						if (self->monsterinfo.aiflags & AI_GOOD_GUY)
						{
							is_stuck = gi.trace(self->s.origin, self->mins, self->maxs, self->s.origin, self, MASK_MONSTERSOLID).startsolid;

							if (!is_stuck)
								walked = true;
						}
						else if (!(self->flags & (FL_FLY | FL_SWIM)))
						{
							M_droptofloor(self);
							walked = M_walkmove(self, 0, 0);
						}
					}
		}

		if (is_stuck)
        {
            // MODIFICATION: If in Horde mode, attempt a more robust fix.
            if (g_horde && g_horde->integer)
            {
                if (Horde_AttemptToUnstickMonster(self))
                {
                    // The monster was successfully relocated.
                    is_stuck = false;
                }
            }
            // END MODIFICATION

            // If still stuck (not horde, or horde fix failed), print the warning.
            if (is_stuck)
            {
			    gi.Com_PrintFmt("WARNING: {} stuck in solid (calling emergencyspawn)\n", *self);
            }
        }
	}

	vec3_t v;

	if (self->health <= 0)
		return;

	self->s.old_origin = self->s.origin;

	// check for target to combat_point and change to combattarget
	if (self->target)
	{
		bool	 notcombat;
		bool	 fixup;
		edict_t *target;

		target = nullptr;
		notcombat = false;
		fixup = false;
		while ((target = G_FindByString<&edict_t::targetname>(target, self->target)) != nullptr)
		{
			if (strcmp(target->classname, "point_combat") == 0)
			{
				self->combattarget = self->target;
				fixup = true;
			}
			else
			{
				notcombat = true;
			}
		}
		if (notcombat && self->combattarget)
			gi.Com_PrintFmt("{}: has target with mixed types\n", *self);
		if (fixup)
			self->target = nullptr;
	}

	// validate combattarget
	if (self->combattarget)
	{
		edict_t *target;

		target = nullptr;
		while ((target = G_FindByString<&edict_t::targetname>(target, self->combattarget)) != nullptr)
		{
			if (strcmp(target->classname, "point_combat") != 0)
			{
				gi.Com_PrintFmt("{} has a bad combattarget {} ({})\n", *self, self->combattarget, *target);
			}
		}
	}

	// allow spawning dead
	bool spawn_dead = self->spawnflags.has(SPAWNFLAG_MONSTER_DEAD);

	if (self->target)
	{
		self->goalentity = self->movetarget = G_PickTarget(self->target);
		if (!self->movetarget)
		{
			gi.Com_PrintFmt("{}: can't find target {}\n", *self, self->target);
			self->target = nullptr;
			self->monsterinfo.pausetime = HOLD_FOREVER;
			if (!spawn_dead)
				self->monsterinfo.stand(self);
		}
		else if (strcmp(self->movetarget->classname, "path_corner") == 0)
		{
			v = self->goalentity->s.origin - self->s.origin;
			self->ideal_yaw = self->s.angles[YAW] = vectoyaw(v);
			if (!spawn_dead)
				self->monsterinfo.walk(self);
			self->target = nullptr;
		}
		else
		{
			self->goalentity = self->movetarget = nullptr;
			self->monsterinfo.pausetime = HOLD_FOREVER;
			if (!spawn_dead)
				self->monsterinfo.stand(self);
		}
	}
	else
	{
		self->monsterinfo.pausetime = HOLD_FOREVER;
		if (!spawn_dead)
			self->monsterinfo.stand(self);
	}
	
	if (spawn_dead)
	{
		// to spawn dead, we'll mimick them dying naturally
		self->health = 0;

		vec3_t f = self->s.origin;

		if (self->die)
			self->die(self, self, self, 0, vec3_origin, MOD_SUICIDE);

		if (!self->inuse)
			return;

		if (self->monsterinfo.setskin)
			self->monsterinfo.setskin(self);

		self->monsterinfo.aiflags |= AI_SPAWNED_DEAD;

		auto move = self->monsterinfo.active_move.pointer();

		for (int i = move->firstframe; i < move->lastframe; i++)
		{
			self->s.frame = i;

			if (move->frame[i - move->firstframe].thinkfunc)
				move->frame[i - move->firstframe].thinkfunc(self);

			if (!self->inuse)
				return;
		}

		if (move->endfunc)
			move->endfunc(self);

		if (!self->inuse)
			return;

		if (self->monsterinfo.start_frame) {
			self->s.frame = self->monsterinfo.start_frame;
		} else {
			self->s.frame = move->lastframe;
		}

		self->s.origin = f;
		gi.linkentity(self);

		self->monsterinfo.aiflags &= ~AI_SPAWNED_DEAD;
	}
	else
	{
		self->think = monster_think;
		self->nextthink = level.time + FRAME_TIME_S;
		self->monsterinfo.aiflags |= AI_SPAWNED_ALIVE;
	}
}

THINK(walkmonster_start_go) (edict_t* self) -> void
{
	if (!self->yaw_speed)
		self->yaw_speed = 20;

	if (self->spawnflags.has(SPAWNFLAG_MONSTER_TRIGGER_SPAWN))
		monster_triggered_start(self);
	else
		monster_start_go(self);
}

void walkmonster_start(edict_t* self)
{
	// Add this check before monster_start is called
	if (g_horde->integer && self->spawnflags.has(SPAWNFLAG_MONSTER_TRIGGER_SPAWN))
		self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	self->think = walkmonster_start_go;
	monster_start(self, ED_GetSpawnTemp());
}

THINK(flymonster_start_go) (edict_t* self) -> void
{
	if (!self->yaw_speed)
		self->yaw_speed = 30;

	if (self->spawnflags.has(SPAWNFLAG_MONSTER_TRIGGER_SPAWN))
		monster_triggered_start(self);
	else
		monster_start_go(self);
}

void flymonster_start(edict_t* self)
{
	// Add this check before monster_start is called
	if (g_horde->integer && self->spawnflags.has(SPAWNFLAG_MONSTER_TRIGGER_SPAWN))
		self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	self->flags |= FL_FLY;
	self->think = flymonster_start_go;
	monster_start(self, ED_GetSpawnTemp());
}

THINK(swimmonster_start_go) (edict_t* self) -> void
{
	if (!self->yaw_speed)
		self->yaw_speed = 30;

	if (self->spawnflags.has(SPAWNFLAG_MONSTER_TRIGGER_SPAWN))
		monster_triggered_start(self);
	else
		monster_start_go(self);
}

void swimmonster_start(edict_t* self)
{
	// Add this check before monster_start is called
	if (g_horde->integer && self->spawnflags.has(SPAWNFLAG_MONSTER_TRIGGER_SPAWN))
		self->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	self->flags |= FL_SWIM;
	self->think = swimmonster_start_go;
	monster_start(self, ED_GetSpawnTemp());
}

USE(trigger_health_relay_use) (edict_t* self, edict_t* other, edict_t* activator) -> void
{
	const float percent_health = clamp(static_cast<float>(other->health) / static_cast<float>(other->max_health), 0.f, 1.f);

	// not ready to trigger yet
	if (percent_health > self->speed)
		return;

	// fire!
	G_UseTargets(self, activator);

	// kill self
	G_FreeEdict(self);
}

/*QUAKED trigger_health_relay (1.0 1.0 0.0) (-8 -8 -8) (8 8 8)
Special type of relay that fires when a linked object is reduced
beyond a certain amount of health.

It will only fire once, and free itself afterwards.
*/
void SP_trigger_health_relay(edict_t* self)
{
	if (!self->targetname)
	{
		gi.Com_PrintFmt("{} missing targetname\n", *self);
		G_FreeEdict(self);
		return;
	}

	if (self->speed < 0 || self->speed > 100)
	{
		gi.Com_PrintFmt("{} has bad \"speed\" (health percentage); must be between 0 and 100, inclusive\n", *self);
		G_FreeEdict(self);
		return;
	}

	self->svflags |= SVF_NOCLIENT;
	self->use = trigger_health_relay_use;
}
