// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"
#include "m_player.h"
#include "bots/bot_includes.h"
#include "shared.h"
#include "horde/g_laser.h"
#include "horde/g_horde_benefits.h"
#include "horde/g_horde.h"
#include "horde/horde_ids.h"
#include "horde/p_flyer_morph.h"
#include "horde/p_brain_morph.h"
#include "horde/g_pvm_menu.h"
#include "horde/g_upgrades.h"

void SP_misc_teleporter_dest(edict_t* ent);

// External functions for inventory (from g_cmds.cpp)
extern void Cmd_InvUse_f(edict_t* ent);
extern void SelectNextItem(edict_t* ent, item_flags_t itflags, bool menu);
extern void SelectPrevItem(edict_t* ent, item_flags_t itflags);

// Boss fog tracking and flashlight (horde_fog_active now in shared.h)
extern void P_ToggleFlashlight(edict_t* ent, bool state);

// ========================================================================
// HORDE MODE CONSTANTS
// ========================================================================
namespace HordeConstants {
	// Wave thresholds for weapon unlocks
	constexpr int8_t WAVE_BASIC_WEAPONS = 4;
	constexpr int8_t WAVE_ADVANCED_WEAPONS = 13;
	constexpr int8_t WAVE_HIGH_AMMO_CAPS = 25;

	// Point awards per wave milestone
	constexpr int8_t ABILITY_POINT_WAVE_INTERVAL = 4;
	constexpr int8_t WEAPON_POINT_WAVE_INTERVAL = 8;

	// Ammo capacity constants - Basic level
	namespace BasicAmmo {
		constexpr int16_t BULLETS = 250;
		constexpr int16_t SHELLS = 100;
		constexpr int16_t CELLS = 250;
		constexpr int16_t FLECHETTES = 250;
		constexpr int16_t GRENADES = 50;
		constexpr int16_t ROCKETS = 50;
		constexpr int16_t SLUGS = 50;
		constexpr int16_t MAGSLUG = 50;
		constexpr int16_t DISRUPTOR = 12;
		constexpr int16_t TESLA = 7;
		constexpr int16_t PROX = 50;
		constexpr int16_t TRAP = 7;
	}

	// Ammo capacity constants - High level (wave 25+)
	namespace HighAmmo {
		constexpr int16_t BULLETS = 400;
		constexpr int16_t SHELLS = 175;
		constexpr int16_t CELLS = 400;
		constexpr int16_t FLECHETTES = 400;
		constexpr int16_t GRENADES = 125;
		constexpr int16_t ROCKETS = 100;
		constexpr int16_t SLUGS = 75;
		constexpr int16_t MAGSLUG = 125;
		constexpr int16_t DISRUPTOR = 35;
		constexpr int16_t TESLA = 14;
		constexpr int16_t PROX = 125;
		constexpr int16_t TRAP = 12;
	}

	// Starting ammo for late-joiners - Basic loadout
	namespace StartingAmmoBasic {
		constexpr int16_t BULLETS = 100;
		constexpr int16_t SHELLS = 20;
		constexpr int16_t FLECHETTES = 50;
		constexpr int16_t PROX = 10;
	}

	// Starting ammo for late-joiners - Advanced loadout
	namespace StartingAmmoAdvanced {
		constexpr int16_t GRENADES = 10;
		constexpr int16_t ROCKETS = 10;
	}

	// Early wave starter ammo (waves 1-3)
	namespace EarlyWaveAmmo {
		constexpr int16_t BULLETS = 50;
		constexpr int16_t SHELLS = 10;
	}
}
// ========================================================================
// PLAYER CONSTANTS
// ========================================================================
namespace PlayerConstants {
	constexpr int DEFAULT_HEALTH = 100;
	constexpr int DEFAULT_MAX_HEALTH = 100;
	constexpr int DEFAULT_BLASTER_AMMO = 25;
	constexpr int DEFAULT_VIEWHEIGHT = 22;
	constexpr int DEFAULT_MASS = 200;
	constexpr gtime_t AIR_TIME = 12_sec;
	constexpr gtime_t RESPAWN_TIMEOUT = 3_sec;
	constexpr gtime_t RESPAWN_INVULNERABILITY = 2_sec;
	
	// Spawn point safety distances
	constexpr float SPAWN_PLAYER_MIN_DISTANCE = 32.0f;
	constexpr float LAVA_SAFETY_MARGIN = 64.0f;
	constexpr int MAX_LAVA_SPAWN_POINTS = 64;
}

// Helper function for password validation
inline bool ValidatePassword(const char* password_cvar, const char* user_password) noexcept
{
	// Returns true if validation FAILS (password required but wrong/missing)
	if (!password_cvar || !*password_cvar)
		return false; // No password set
	
	if (strcmp(password_cvar, "none") == 0)
		return false; // Password explicitly disabled
	
	return strcmp(password_cvar, user_password) != 0; // True if passwords don't match (validation failed)
}

THINK(info_player_start_drop) (edict_t* self) -> void
{
	// allow them to drop
	self->solid = SOLID_TRIGGER;
	self->movetype = MOVETYPE_TOSS;
	self->mins = PLAYER_MINS;
	self->maxs = PLAYER_MAXS;
	gi.linkentity(self);
}

constexpr spawnflags_t SPAWNFLAG_SPAWN_RIDE = 1_spawnflag;

/*QUAKED info_player_start (1 0 0) (-16 -16 -24) (16 16 32)
The normal starting point for a level.
*/
void SP_info_player_start(edict_t* self)
{
	// fix stuck spawn points
	if (gi.trace(self->s.origin, PLAYER_MINS, PLAYER_MAXS, self->s.origin, self, MASK_SOLID).startsolid)
		G_FixStuckObject(self, self->s.origin);

	// [Paril-KEX] on n64, since these can spawn riding elevators,
	// allow them to "ride" the elevators so respawning works
	if (level.is_n64 || level.is_psx || self->spawnflags.has(SPAWNFLAG_SPAWN_RIDE))
	{
		self->think = info_player_start_drop;
		self->nextthink = level.time + FRAME_TIME_S;
	}

	if (pm_config.physics_flags & PHYSICS_PSX_SCALE)
		self->s.origin[2] -= PLAYER_MINS[2] - (PLAYER_MINS[2] * PSX_PHYSICS_SCALAR);
}

/*QUAKED info_player_deathmatch (1 0 1) (-16 -16 -24) (16 16 32)
potential spawning position for deathmatch games
*/
void SP_info_player_deathmatch(edict_t* self)
{
	if ((!g_horde->integer && !pvm->integer) || !deathmatch->integer)
	{
		G_FreeEdict(self);
		return;
	}
	if (g_dm_spawns->integer)
		SP_misc_teleporter_dest(self);
}

/*QUAKED info_player_coop (1 0 1) (-16 -16 -24) (16 16 32)
potential spawning position for coop games
*/
void SP_info_player_coop(edict_t* self)
{
	if (!G_IsCooperative() && !g_horde->integer && !pvm->integer)
	{
		G_FreeEdict(self);
		return;
	}

	SP_info_player_start(self);
}

/*QUAKED info_player_coop_lava (1 0 1) (-16 -16 -24) (16 16 32)
potential spawning position for coop games on rmine2 where lava level
needs to be checked
*/
void SP_info_player_coop_lava(edict_t* self)
{
	if (!G_IsCooperative())
	{
		G_FreeEdict(self);
		return;
	}

	// fix stuck spawn points
	if (gi.trace(self->s.origin, PLAYER_MINS, PLAYER_MAXS, self->s.origin, self, MASK_SOLID).startsolid)
		G_FixStuckObject(self, self->s.origin);
}

/*QUAKED info_player_intermission (1 0 1) (-16 -16 -24) (16 16 32)
The deathmatch intermission point will be at one of these
Use 'angles' instead of 'angle', so you can set pitch or roll as well as yaw.  'pitch yaw roll'
*/
void SP_info_player_intermission(edict_t* ent)
{
}

// [Paril-KEX] whether instanced items should be used or not
bool P_UseCoopInstancedItems() noexcept
{
	if (IsPvMMode())
	return false;
	// squad respawn forces instanced items on, since we don't
	// want players to need to backtrack just to get their stuff.
	return g_coop_instanced_items->integer || g_coop_squad_respawn->integer;
}

//=======================================================================


// ========================================================================
// OBITUARY MESSAGE LOOKUP TABLES
// ========================================================================

namespace ObituaryMessages {
	// Self-kill messages (suicide)
	inline const char* GetSelfKillMessage(mod_id_t mod_id)
	{
		switch (mod_id)
		{
		case MOD_HELD_GRENADE:   return "$g_mod_self_held_grenade";
		case MOD_HG_SPLASH:
		case MOD_G_SPLASH:       return "$g_mod_self_grenade_splash";
		case MOD_R_SPLASH:       return "$g_mod_self_rocket_splash";
		case MOD_BFG_BLAST:      return "$g_mod_self_bfg_blast";
		case MOD_TRAP:           return "$g_mod_self_trap";
		case MOD_DOPPLE_EXPLODE: return "$g_mod_self_dopple_explode";
		default:                 return "{0} becomes bored with life\n";
		}
	}

	// Player vs Player kill messages
	inline const char* GetPlayerKillMessage(mod_id_t mod_id)
	{
		switch (mod_id)
		{
		case MOD_BLASTER:          return "$g_mod_kill_blaster";
		case MOD_SHOTGUN:          return "$g_mod_kill_shotgun";
		case MOD_SSHOTGUN:         return "$g_mod_kill_sshotgun";
		case MOD_MACHINEGUN:       return "$g_mod_kill_machinegun";
		case MOD_CHAINGUN:         return "$g_mod_kill_chaingun";
		case MOD_GRENADE:          return "$g_mod_kill_grenade";
		case MOD_G_SPLASH:         return "$g_mod_kill_grenade_splash";
		case MOD_ROCKET:           return "$g_mod_kill_rocket";
		case MOD_R_SPLASH:         return "$g_mod_kill_rocket_splash";
		case MOD_HYPERBLASTER:     return "$g_mod_kill_hyperblaster";
		case MOD_RAILGUN:          return "$g_mod_kill_railgun";
		case MOD_BFG_LASER:        return "$g_mod_kill_bfg_laser";
		case MOD_BFG_BLAST:        return "$g_mod_kill_bfg_blast";
		case MOD_BFG_EFFECT:       return "$g_mod_kill_bfg_effect";
		case MOD_HANDGRENADE:      return "$g_mod_kill_handgrenade";
		case MOD_HG_SPLASH:        return "$g_mod_kill_handgrenade_splash";
		case MOD_HELD_GRENADE:     return "$g_mod_kill_held_grenade";
		case MOD_TELEFRAG:
		case MOD_TELEFRAG_SPAWN:   return "$g_mod_kill_telefrag";
		case MOD_RIPPER:           return "$g_mod_kill_ripper";
		case MOD_PHALANX:          return "$g_mod_kill_phalanx";
		case MOD_TRAP:             return "$g_mod_kill_trap";
		case MOD_CHAINFIST:        return "$g_mod_kill_chainfist";
		case MOD_DISINTEGRATOR:    return "$g_mod_kill_disintegrator";
		case MOD_ETF_RIFLE:        return "$g_mod_kill_etf_rifle";
		case MOD_HEATBEAM:         return "$g_mod_kill_heatbeam";
		case MOD_TESLA:            return "$g_mod_kill_tesla";
		case MOD_PROX:             return "$g_mod_kill_prox";
		case MOD_NUKE:             return "$g_mod_kill_nuke";
		case MOD_VENGEANCE_SPHERE: return "$g_mod_kill_vengeance_sphere";
		case MOD_DEFENDER_SPHERE:  return "$g_mod_kill_defender_sphere";
		case MOD_HUNTER_SPHERE:    return "$g_mod_kill_hunter_sphere";
		case MOD_TRACKER:          return "$g_mod_kill_tracker";
		case MOD_DOPPLE_EXPLODE:   return "$g_mod_kill_dopple_explode";
		case MOD_DOPPLE_VENGEANCE: return "$g_mod_kill_dopple_vengeance";
		case MOD_DOPPLE_HUNTER:    return "$g_mod_kill_dopple_hunter";
		case MOD_GRAPPLE:          return "$g_mod_kill_grapple";
		case MOD_HOOK:             return "{0} was disemboweled by {1}'s hook.\n";
		default:                   return "$g_mod_kill_generic";
		}
	}

	// Monster kill messages
	inline const char* GetMonsterKillMessage(mod_id_t mod_id)
	{
		switch (mod_id)
		{
		case MOD_BLASTER:        return brandom() ? "{0} was humiliated by a {1}\n" : "{0} was blasted by a {1}\n";
		case MOD_SHOTGUN:        return "{0}'s face was impacted by a {1}\n";
		case MOD_SSHOTGUN:       return brandom() ? "{0} was blown to pieces by a {1}\n" : "{0}'s ribs were shattered by a {1}\n";
		case MOD_MACHINEGUN:     return "{0} was shredded by a {1}\n";
		case MOD_CHAINGUN:       return "{0}'s torso was removed by a {1}\n";
		case MOD_GRENADE:        return "{0} was gibbed by a {1}'s grenade\n";
		case MOD_G_SPLASH:       return "{0} was splattered all over by a {1}\n";
		case MOD_ROCKET:         return "{0} ate the rocket of a {1}\n";
		case MOD_FIREBALL:       return "{0} was reduced to ashes by a {1}\n";
		case MOD_R_SPLASH:       return "{0} was blown up by a {1}\n";
		case MOD_HYPERBLASTER:   return "{0} was blasted by a {1}\n";
		case MOD_RAILGUN:        return "{0} was railed by a {1}\n";
		case MOD_BFG_LASER:      return "{0} ate the lights of a {1}'s BFG Lasers\n";
		case MOD_BFG_BLAST:      return "{0} was disintegrated by a {1}\n";
		case MOD_BFG_EFFECT:     return "{0} couldn't escape the apocalyptic fury of a {1}'s BFG\n";
		case MOD_HANDGRENADE:    return "{0} was blown up by a {1}\n";
		case MOD_HG_SPLASH:      return "{0} was splashed by a {1}\n";
		case MOD_HELD_GRENADE:   return "{0} was blown up by a {1}\n";
		case MOD_RIPPER:         return "{0} got ionripped by a {1}\n";
		case MOD_TARGET_LASER:   return "{0} was laser-cooked by a {1}\n";
		case MOD_TESLA:          return "{0} got a shocking end thanks to a {1}\n";
		case MOD_TELEFRAG:
		case MOD_TELEFRAG_SPAWN: return "{0} was telefragged by a {1}\n";
		case MOD_BRAINTENTACLE:  return brandom() ? "{0} got a slimy end from a {1}'s tentacles. Gross!\n" : "{0} tastes finger lickin' good to a {1}\n";
		case MOD_GEKK:           return "{0} was spat to death by a {1}. Yuck!\n";
		case MOD_TANK_PUNCH:     return "{0} was pulverized by a {1}\n";
		case MOD_PHALANX:        return "{0} was melted by a {1}\n";
		case MOD_TRACKER:        return "{0} was tracked down by a {1}\n";
		case MOD_BLUEBLASTER:    return "{0} was blasted by a {1}\n";
		case MOD_BURN:			 return "{0} was burned alive by a {1}\n";
		default:                 return brandom() ? "{0} was killed insanely by a {1}\n" : "{0} was killed by a {1}\n";
		}
	}

	// World/environmental kill messages
	inline const char* GetWorldKillMessage(mod_id_t mod_id)
	{
		switch (mod_id)
		{
		case MOD_SUICIDE:        return "{0} becomes bored with life\n";
		case MOD_FALLING:        return "{0} made a leap of faith\n";
		case MOD_CRUSH:          return "{0} suffers from claustrophobia\n";
		case MOD_WATER:          return "{0} forgot to breathe\n";
		case MOD_SLIME:          return "$g_mod_generic_slime";
		case MOD_LAVA:           return "{0} joins the lava gods\n";
		case MOD_EXPLOSIVE:
		case MOD_BARREL:         return "$g_mod_generic_explosive";
		case MOD_EXIT:           return "$g_mod_generic_exit";
		case MOD_TARGET_LASER:   return "{0} saw the light!\n";
		case MOD_TARGET_BLASTER: return "$g_mod_generic_blaster";
		case MOD_BOMB:
		case MOD_SPLASH:
		case MOD_TRIGGER_HURT:   return "$g_mod_generic_hurt";
		default:                 return "{0} died.\n";
		}
	}

	// Get monster display name from attacker (strips "monster_" prefix)
	inline const char* GetMonsterDisplayName(edict_t* attacker)
	{
		const char* name = GetDisplayName(attacker);
		if (name && strncmp(name, "monster_", 8) == 0)
			return name + 8;
		return name ? name : "monster";
	}

	// Get monster name from projectile type ID
	inline const char* GetMonsterNameFromProjectile(edict_t* inflictor)
	{
		const char* classname = horde::MonsterTypeRegistry::GetClassname(
			static_cast<horde::MonsterTypeID>(inflictor->projectile_attacker_type_id));
		
		if (classname && strncmp(classname, "monster_", 8) == 0)
			return classname + 8;
		return classname ? classname : "monster";
	}
}

// ========================================================================
// OBITUARY HELPER FUNCTIONS
// ========================================================================

// Handle self-kill (suicide) obituary
static void HandleSelfKillObituary(edict_t* self, const mod_t& mod)
{
	const char* message = ObituaryMessages::GetSelfKillMessage(mod.id);
	gi.LocBroadcast_Print(PRINT_MEDIUM, message, self->client->pers.netname);

	// Apply score penalty in deathmatch
	if (deathmatch->integer && !mod.no_point_loss)
	{
		if (gamerules->integer && DMGame.Score)
			DMGame.Score(self, self, -1, mod);
		else
		{
			self->client->resp.score--;
			if (teamplay->integer)
				G_AdjustTeamScore(self->client->resp.ctf_team, -1);
		}
	}

	self->enemy = nullptr;
}

// Handle player vs player kill obituary
static void HandlePlayerKillObituary(edict_t* self, edict_t* attacker, const mod_t& mod)
{
	const char* message = ObituaryMessages::GetPlayerKillMessage(mod.id);
	gi.LocBroadcast_Print(PRINT_MEDIUM, message, self->client->pers.netname, attacker->client->pers.netname);
	self->enemy = attacker;
}

// Handle monster kill obituary (living monster)
static void HandleMonsterKillObituary(edict_t* self, edict_t* attacker, const mod_t& mod)
{
	const char* monster_name = ObituaryMessages::GetMonsterDisplayName(attacker);
	const char* message = ObituaryMessages::GetMonsterKillMessage(mod.id);
	gi.LocBroadcast_Print(PRINT_MEDIUM, message, self->client->pers.netname, monster_name);
	self->enemy = attacker;
}

// Handle projectile from dead monster
static void HandleDeadMonsterProjectileObituary(edict_t* self, edict_t* inflictor, const mod_t& mod)
{
	const char* monster_name = ObituaryMessages::GetMonsterNameFromProjectile(inflictor);
	const char* message = ObituaryMessages::GetMonsterKillMessage(mod.id);
	gi.LocBroadcast_Print(PRINT_MEDIUM, message, self->client->pers.netname, monster_name);
	self->enemy = nullptr;
}

// Handle world/environmental kills
static void HandleWorldKillObituary(edict_t* self, edict_t* attacker, const mod_t& mod)
{
	const char* message = ObituaryMessages::GetWorldKillMessage(mod.id);
	gi.LocBroadcast_Print(PRINT_MEDIUM, message, self->client->pers.netname);

	// Apply score penalty in deathmatch
	if (deathmatch->integer && !mod.no_point_loss)
	{
		if (gamerules->integer && DMGame.Score)
			DMGame.Score(self, self, -1, mod);
		else
		{
			self->client->resp.score--;
			if (teamplay->integer && self->client->resp.ctf_team != CTF_NOTEAM)
				G_AdjustTeamScore(self->client->resp.ctf_team, -1);
		}
	}

	// Set enemy if attacker is valid (not world, not self)
	self->enemy = (attacker && attacker != world && attacker != self) ? attacker : nullptr;
}

void ClientObituary(edict_t* self, edict_t* inflictor, edict_t* attacker, mod_t mod)
{
	// Validate inputs
	if (!self || !self->client)
	{
		gi.Com_Print("ClientObituary: Error - null self or self->client\n");
		return;
	}

	// Set friendly fire flag
	if ((G_IsCooperative() && attacker && attacker->client) || deathmatch->integer)
		mod.friendly_fire = true;

	// --- 1. Handle Self-Kills (Suicide) ---
	if (attacker == self)
	{
		HandleSelfKillObituary(self, mod);
		return;
	}

	// --- 2. Handle Player vs Player Kills ---
	if (attacker && attacker->client)
	{
		HandlePlayerKillObituary(self, attacker, mod);
		return;
	}

	// --- 3. Handle Projectiles from Dead Monsters ---
	if (inflictor && (inflictor->svflags & SVF_PROJECTILE) &&
		!inflictor->projectile_was_player_attacker && 
		inflictor->projectile_attacker_type_id &&
		(!attacker || attacker == world || attacker == inflictor ||
		 (attacker && !(attacker->svflags & SVF_MONSTER) && !attacker->client)))
	{
		HandleDeadMonsterProjectileObituary(self, inflictor, mod);
		return;
	}

	// --- 4. Handle Living Monster Kills ---
	if (attacker && (attacker->svflags & SVF_MONSTER))
	{
		HandleMonsterKillObituary(self, attacker, mod);
		return;
	}

	// --- 5. Handle World/Environmental Kills ---
	HandleWorldKillObituary(self, attacker, mod);
}

void TossClientWeapon(edict_t* self)
{
	gitem_t* item;
	edict_t* drop;
	bool	 quad;
	// RAFAEL
	bool quadfire;
	// RAFAEL
	float spread;

	if (!deathmatch->integer)
		return;

	item = self->client->pers.weapon;
	// PvM mode: disable instagib
	if (item && g_instagib->integer && !IsPvMMode())
		item = nullptr;
	if (item && !self->client->pers.inventory[self->client->pers.weapon->ammo])
		item = nullptr;
	if (item && !item->drop)
		item = nullptr;

	if (g_dm_no_quad_drop->integer)
		quad = false;
	else
		quad = (self->client->quad_time > (level.time + 1_sec));

	// RAFAEL
	if (g_dm_no_quadfire_drop->integer)
		quadfire = false;
	else
		quadfire = (self->client->quadfire_time > (level.time + 1_sec));
	// RAFAEL

	if (item && quad)
		spread = 22.5;
	// RAFAEL
	else if (item && quadfire)
		spread = 12.5;
	// RAFAEL
	else
		spread = 0.0;

	if (item)
	{
		self->client->v_angle[YAW] -= spread;
		drop = Drop_Item(self, item);
		self->client->v_angle[YAW] += spread;
		drop->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
		drop->spawnflags &= ~SPAWNFLAG_ITEM_DROPPED;
		drop->svflags &= ~SVF_INSTANCED;
	}

	if (quad)
	{
		self->client->v_angle[YAW] += spread;
		drop = Drop_Item(self, GetItemByIndex(IT_ITEM_QUAD));
		self->client->v_angle[YAW] -= spread;
		drop->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
		drop->spawnflags &= ~SPAWNFLAG_ITEM_DROPPED;
		drop->svflags &= ~SVF_INSTANCED;

		drop->touch = Touch_Item;
		drop->nextthink = self->client->quad_time;
		drop->think = G_FreeEdict;
	}

	// RAFAEL
	if (quadfire)
	{
		self->client->v_angle[YAW] += spread;
		drop = Drop_Item(self, GetItemByIndex(IT_ITEM_QUADFIRE));
		self->client->v_angle[YAW] -= spread;
		drop->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
		drop->spawnflags &= ~SPAWNFLAG_ITEM_DROPPED;
		drop->svflags &= ~SVF_INSTANCED;

		drop->touch = Touch_Item;
		drop->nextthink = self->client->quadfire_time;
		drop->think = G_FreeEdict;
	}
	// RAFAEL
}

/*
==================
LookAtKiller
==================
*/
void LookAtKiller(edict_t* self, edict_t* inflictor, edict_t* attacker)
{
	vec3_t dir;

	if (attacker && attacker != world && attacker != self)
	{
		dir = attacker->s.origin - self->s.origin;
	}
	else if (inflictor && inflictor != world && inflictor != self)
	{
		dir = inflictor->s.origin - self->s.origin;
	}
	else
	{
		self->client->killer_yaw = self->s.angles[YAW];
		return;
	}
	// PMM - fixed to correct for pitch of 0
	if (dir[0])
		self->client->killer_yaw = 180 / PIf * atan2f(dir[1], dir[0]);
	else if (dir[1] > 0)
		self->client->killer_yaw = 90;
	else if (dir[1] < 0)
		self->client->killer_yaw = 270;
	else
		self->client->killer_yaw = 0;
}

/*
==================
player_die
==================
*/
DIE(player_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	PlayerTrail_Destroy(self);

	// Clear morph state if player was morphed
	if (IsMorphed(self)) {
		RestoreMorphed(self);
	}

	self->avelocity = {};

	self->takedamage = true;
	self->movetype = MOVETYPE_TOSS;

	self->s.modelindex2 = 0; // remove linked weapon model
	// ZOID
	self->s.modelindex3 = 0; // remove linked ctf flag
	// ZOID

	self->s.angles[0] = 0;
	self->s.angles[2] = 0;

	self->s.sound = 0;
	self->client->weapon_sound = 0;

	self->maxs[2] = -8;

	//	self->solid = SOLID_NOT;
	self->svflags |= SVF_DEADMONSTER;

	if (!self->deadflag)
	{
		self->client->respawn_time = (level.time + 1_sec);
		if (deathmatch->integer && g_dm_force_respawn_time->integer) {
			self->client->respawn_time = (level.time + gtime_t::from_sec(g_dm_force_respawn_time->value));
		}

		LookAtKiller(self, inflictor, attacker);
		self->client->ps.pmove.pm_type = PM_DEAD;
		ClientObituary(self, inflictor, attacker, mod);

		CTFFragBonuses(self, inflictor, attacker);
		// Kyper - Lithium Port
		Hook_PlayerDie(attacker, self);
		// ZOID
		// PvM mode: drop backpack with all items, otherwise toss weapon
		if (IsPvMMode())
			PVM_DropBackpack(self);
		else
			TossClientWeapon(self);
		// ZOID
		CTFPlayerResetGrapple(self);
		CTFDeadDropFlag(self);
		CTFDeadDropTech(self);
		// ZOID
		if (deathmatch->integer && !self->client->showscores)
			Cmd_Help_f(self); // show scores

		if ((deathmatch->integer && g_horde->integer && !P_UseCoopInstancedItems()) || (G_IsCooperative() && !P_UseCoopInstancedItems()))
		{
			// clear inventory
			// this is kind of ugly, but it's how we want to handle keys in coop
			for (int n = 0; n < IT_TOTAL; n++)
			{
				if (G_IsCooperative() && (itemlist[n].flags & IF_KEY))
					self->client->resp.coop_respawn.inventory[n] = self->client->pers.inventory[n];
				self->client->pers.inventory[n] = 0;
			}
		}
	}

	if (gamerules->integer) // if we're in a dm game, alert the game
	{
		if (DMGame.PlayerDeath)
			DMGame.PlayerDeath(self, inflictor, attacker);
	}

	// remove powerups
	self->client->quad_time = 0_ms;
	self->client->invincible_time = 0_ms;
	self->client->breather_time = 0_ms;
	self->client->enviro_time = 0_ms;
	self->client->invisible_time = 0_ms;
	self->flags &= ~FL_POWER_ARMOR;

	// clear inventory
//	if (G_TeamplayEnabled())					// fixing no weapons loadout
//		self->client->pers.inventory.fill(0);

	// RAFAEL
	self->client->quadfire_time = 0_ms;
	// RAFAEL

	//==============
	// ROGUE stuff
	self->client->double_time = 0_ms;

	// if there's a sphere around, let it know the player died.
	// vengeance and hunter will die if they're not attacking,
	// defender should always die
	if (self->client && self->client->owned_sphere) {
		if (self->client->owned_sphere->die) {
			self->client->owned_sphere->die(self->client->owned_sphere, self, self, 0, vec3_origin, mod);
		}
		else {
			G_FreeEdict(self->client->owned_sphere);
		}
		self->client->owned_sphere = nullptr;
	}
	// if we've been killed by the tracker or laser, GIB!
	if (mod.id == MOD_TRACKER || mod.id == MOD_PLAYER_LASER)
	{
		self->health = -400;
		damage = 9999;
	}

	// make sure no trackers are still hurting us.
	if (self->client->tracker_pain_time) // || self->monsterinfo.tracker_pain_time)
	{
		RemoveAttackingPainDaemons(self);
	}

	// if we got obliterated by the nuke, don't gib
	if ((self->health < -80) && (mod.id == MOD_NUKE))
		self->flags |= FL_NOGIB;

	// ROGUE
	//==============

	if (self->health < -40)
	{
		// PMM
		// don't toss gibs if we got vaped by the nuke
		if (!(self->flags & FL_NOGIB))
		{
			// pmm
			// gib
			gi.sound(self, CHAN_BODY, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

			// more meaty gibs for your dollar!
			if (deathmatch->integer && (self->health < -80))
				ThrowGibs(self, damage, { { 4, "models/objects/gibs/sm_meat/tris.md2" } });

			ThrowGibs(self, damage, { { 4, "models/objects/gibs/sm_meat/tris.md2" } });
			// PMM
		}
		self->flags &= ~FL_NOGIB;
		// pmm

		// Don't create skull if being crushed - prevent infinite explosion loop
		if (mod.id != MOD_CRUSH)
			ThrowClientHead(self, damage);
		// ZOID
		self->client->anim_priority = ANIM_DEATH;
		self->client->anim_end = 0;
		// ZOID
		self->takedamage = false;
	}
	else
	{ // normal death
		if (!self->deadflag)
		{
			// start a death animation
			self->client->anim_priority = ANIM_DEATH;
			if (self->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				self->s.frame = FRAME_crdeath1 - 1;
				self->client->anim_end = FRAME_crdeath5;
			}
			else
			{
				switch (irandom(3))
				{
				case 0:
					self->s.frame = FRAME_death101 - 1;
					self->client->anim_end = FRAME_death106;
					break;
				case 1:
					self->s.frame = FRAME_death201 - 1;
					self->client->anim_end = FRAME_death206;
					break;
				case 2:
					self->s.frame = FRAME_death301 - 1;
					self->client->anim_end = FRAME_death308;
					break;
				}
			}
			static constexpr const char* death_sounds[] = {
				"*death1.wav",
				"*death2.wav",
				"*death3.wav",
				"*death4.wav"
			};
			gi.sound(self, CHAN_VOICE, gi.soundindex(random_element(death_sounds)), 1, ATTN_NORM, 0);
			self->client->anim_time = 0_ms;
		}
	}

	// Death logic
	if (!self->deadflag)
	{
		if (g_horde->integer)
		{
			if (g_coop_squad_respawn->integer || g_coop_enable_lives->integer)
			{
				if (g_coop_enable_lives->integer && self->client->pers.lives > 0)
				{
					self->client->pers.lives--;
					self->client->resp.coop_respawn.lives--;
				}
				bool allPlayersDead = true;
				for (auto const* player : active_players_no_spect())
				{
					if (player->health > 0 || (!level.deadly_kill_box && g_coop_enable_lives->integer && player->client->pers.lives > 0))
					{
						allPlayersDead = false;
						break;
					}
				}
				if (allPlayersDead)
				{
					// Horde mode
					for (auto player : active_players_no_spect())
						gi.LocCenter_Print(player, "$g_coop_lose");
					AllowReset();
					gi.cvar_set("timelimit", "0.01");
				}
				// Set respawn time if level restart is not scheduled
				if (!level.coop_level_restart_time)
					self->client->respawn_time = level.time + 2_sec;
			}
		}
		else if (G_IsCooperative() && (g_coop_squad_respawn->integer || (G_IsCooperative() && g_coop_enable_lives->integer)))
		{
			if (g_coop_enable_lives->integer && self->client->pers.lives)
			{
				self->client->pers.lives--;
				self->client->resp.coop_respawn.lives--;
			}
			bool allPlayersDead = true;
			for (auto const* player : active_players())
				if (player->health > 0 || (!level.deadly_kill_box && g_coop_enable_lives->integer && player->client->pers.lives > 0))
				{
					allPlayersDead = false;
					break;
				}
			if (allPlayersDead) // allow respawns for telefrags and weird shit
			{
				level.coop_level_restart_time = level.time + 5_sec;
				for (const auto player : active_players())
					gi.LocCenter_Print(player, "$g_coop_lose");
			}

			// in 3 seconds, attempt a respawn or put us into
			// spectator mode
			if (!level.coop_level_restart_time)
				self->client->respawn_time = level.time + 3_sec;
		}
	}
	self->deadflag = true;

	// Save character data on death (PvM & Horde)
	if ((g_horde && g_horde->integer) || (pvm && pvm->integer))
		Character_Save(self);

	// Don't link if being crushed - prevent re-crush
	if (mod.id == MOD_CRUSH)
	{
		self->svflags = SVF_NOCLIENT;
		self->takedamage = false;
		self->solid = SOLID_NOT;
		self->movetype = MOVETYPE_NOCLIP;
		self->die = nullptr;
		gi.unlinkentity(self);
	}
	else
		gi.linkentity(self);

	// Remove all entities owned by the player (only for Horde/PvM)
	if (g_horde->integer || pvm->integer)
	{
		RemovePlayerOwnedEntities(self);
	}
}
//=======================================================================

#include <string>
#include <sstream>

// [Paril-KEX]
static void Player_GiveStartItems(edict_t* ent, const char* ptr)
{
	if (!ent || !ptr)
		return;

	char token_copy[MAX_TOKEN_CHARS];
	const char* token;

	while (*(token = COM_ParseEx(&ptr, ";")))
	{
		Q_strlcpy(token_copy, token, sizeof(token_copy));
		const char* ptr_copy = token_copy;
		const char* item_name = COM_Parse(&ptr_copy);

		// Early validation of item_name
		if (!item_name || !*item_name)
		{
			gi.Com_ErrorFmt("Empty item name in g_start_item\n");
			continue;
		}

		gitem_t* item = FindItemByClassname(item_name);

		// Comprehensive null check for item and its pickup function
		if (!item)
		{
			gi.Com_ErrorFmt("Invalid g_start_item entry: {}\n", item_name);
			continue;
		}

		if (!item->pickup)
		{
			gi.Com_ErrorFmt("Item {} has no pickup function\n", item_name);
			continue;
		}

		int32_t count = 1;
		if (*ptr_copy)
			count = atoi(COM_Parse(&ptr_copy));

		if (count == 0)
		{
			ent->client->pers.inventory[item->id] = 0;
			continue;
		}

		edict_t* dummy = G_Spawn();
		if (!dummy)
		{
			gi.Com_ErrorFmt("Failed to spawn dummy entity for item {}\n", item_name);
			continue;
		}

		dummy->item = item;
		dummy->count = count;
		dummy->spawnflags |= SPAWNFLAG_ITEM_DROPPED | SPAWNFLAG_ITEM_START_ITEM;
		item->pickup(dummy, ent);
		G_FreeEdict(dummy);
	}
}

constexpr item_id_t tech_ids[] = { IT_TECH_RESISTANCE, IT_TECH_STRENGTH, IT_TECH_HASTE, IT_TECH_REGENERATION };
bool IsTechItem(int item_id) noexcept;
void InitClientPt(const edict_t* ent, gclient_t* client)
{
	// backup userinfo and states
	char userinfo[MAX_INFO_STRING];
	bool saved_id_state = client->pers.id_state;
	bool saved_iddmg_state = client->pers.iddmg_state;
	sentrytype_t saved_sentrygun_state = client->pers.sentry_gun_choice;
	Q_strlcpy(userinfo, client->pers.userinfo, sizeof(userinfo));

	// For Horde mode, check score to maybe reset team
	if (g_horde->integer) {
		if (!(ent->svflags & SVF_BOT) && client->resp.score <= 2) {
			client->resp.ctf_team = CTF_NOTEAM;
		}
	}

	// Clear inventory except blaster and tech items
	for (size_t i = 0; i < IT_TOTAL; i++) {
		// Note: You might need to cast IT_WEAPON_BLASTER if it's also an int
		if (i != static_cast<size_t>(IT_WEAPON_BLASTER) && !IsTechItem(i)) {
			client->pers.inventory[i] = 0;
		}
	}

	// Restore userinfo and states
	Q_strlcpy(client->pers.userinfo, userinfo, sizeof(client->pers.userinfo));
	client->pers.id_state = saved_id_state;
	client->pers.iddmg_state = saved_iddmg_state;
	client->pers.sentry_gun_choice = saved_sentrygun_state;

	// Reset health values
		client->pers.health = 100;
		client->pers.max_health = 100;
}
// Helper function to verify if an item is a TECH item
bool IsTechItem(int item_id) noexcept
{
	for (const auto& tech_id : tech_ids) {
		if (item_id == tech_id) {
			return true;
		}
	}
	return false;
}

/*
==============
InitClientPersistant

This is only called when the game first initializes in single player,
but is called after each death and level change in deathmatch
==============
*/
// Calculate maximum health based on current wave level
int CalculateWaveBasedMaxHealth(int base_max_health, gclient_t* client = nullptr) noexcept
{
	if (!g_horde->integer && !pvm->integer)
		return max(100, base_max_health);

	// PvM mode: Don't apply wave-based scaling, vitality handles health instead
	if (IsPvMMode())
		return base_max_health;

	// Calculate health based on wave tier (optimized lookup)
	int calculated_max_health = base_max_health;
	
	if (current_wave_level >= 30)
		calculated_max_health = 250;
	else if (current_wave_level >= 25)
		calculated_max_health = 225;
	else if (current_wave_level >= 20)
		calculated_max_health = 200;
	else if (current_wave_level >= 15)
		calculated_max_health = 175;
	else if (current_wave_level >= 10)
		calculated_max_health = 150;
	else if (current_wave_level >= 5)
		calculated_max_health = 125;
	else
		calculated_max_health = 100;
	
	calculated_max_health = max(calculated_max_health, base_max_health);

	// Add adrenaline bonus if client provided
	if (client)
		calculated_max_health += (client->pers.adrenaline_count * ADRENALINE_HEALTH_BONUS);

	return calculated_max_health;
}

// Updates g_start_items based on current wave for dynamic loadouts
void Horde_UpdateStartItemsForWave(int32_t wave)
{
	std::string loadout;

	if (wave >= HordeConstants::WAVE_BASIC_WEAPONS)
	{
		// Basic weapons (wave 4+) - weapons don't auto-give ammo thanks to SPAWNFLAG_ITEM_START_ITEM
		loadout = "weapon_blaster 1;weapon_chainfist 1;weapon_shotgun 1;weapon_supershotgun 1;"
		          "weapon_machinegun 1;weapon_etf_rifle 1;weapon_proxlauncher 1;";

		// Starting ammo for basic weapons
		loadout += "ammo_bullets 2;ammo_shells 2;ammo_flechettes 2;ammo_prox 1;";

		if (wave >= HordeConstants::WAVE_ADVANCED_WEAPONS)
		{
			// Advanced weapons (wave 13+)
			loadout += "weapon_chaingun 1;weapon_grenadelauncher 1;weapon_rocketlauncher 1;";

			// Starting ammo for advanced weapons
			loadout += "ammo_grenades 1;ammo_rockets 1;";
		}
	}
	else if (wave >= 1)
	{
		// Early waves (1-3): Minimal loadout
		loadout = "weapon_blaster 1;ammo_bullets 1;ammo_shells 1;";
	}
	else
	{
		// Wave 0 (default/reset): Blaster only
		loadout = "weapon_blaster 1";
	}

	// Update the cvar
	gi.cvar_set("g_start_items", loadout.c_str());

	if (developer->integer)
	{
		gi.Com_PrintFmt("Horde: Updated g_start_items for wave {}\n", wave);
	}
}

void Horde_InitClientPersistant(edict_t* ent, gclient_t* client)
{

	const bool is_late_joiner = !client->pers.received_late_join_ammo;
	const int wave = current_wave_level;

	//
	// BOT AUTO-BUY SYSTEM
	//
	// Bots always use auto-buy for the old benefits system
	if (ent->svflags & SVF_BOT) {
		client->pers.auto_buy_abilities = true;
		client->pers.auto_buy_weapons = true;
		client->pers.has_manually_disabled_auto_buy = false;
	}

	//
	// LATE JOINER BENEFITS
	//
	if (is_late_joiner)
	{
		// Enable auto-buy by default for late joiners too
		client->pers.auto_buy_abilities = true;
		client->pers.auto_buy_weapons = true;
		client->pers.has_manually_disabled_auto_buy = false;

		// Calculate bonus points based on wave progress
		client->pers.ability_points = (wave >= HordeConstants::ABILITY_POINT_WAVE_INTERVAL) 
			? (wave / HordeConstants::ABILITY_POINT_WAVE_INTERVAL) : 0;
		
		client->pers.weapon_points = (wave >= HordeConstants::WEAPON_POINT_WAVE_INTERVAL)
			? (wave / HordeConstants::WEAPON_POINT_WAVE_INTERVAL) : 0;

		// Notify if points awarded
		if (client->pers.ability_points > 0 || client->pers.weapon_points > 0)
		{
			gi.LocClient_Print(ent, PRINT_HIGH,
				"Late join bonus: {} ability points, {} weapon points awarded based on current wave!\n",
				client->pers.ability_points, client->pers.weapon_points);

			// Trigger auto-buy immediately for late joiner bots only
			// (Human players use the upgrade menu system instead)
			if (ent->svflags & SVF_BOT) {
				CheckBotAutoBuy(ent);
			}
		}
	}

	// PvM Mode: Give respawn weapon only, then return
	if (IsPvMMode())
	{
		// Set base health (100) - vitality bonus will be added by PvM_ApplyStatBonuses
		client->pers.max_health = client->resp.max_health = ent->max_health = 100;

		// Set max ammo values (required for weapon to work properly)
		client->pers.max_ammo[AMMO_BULLETS] = 200;
		client->pers.max_ammo[AMMO_SHELLS] = 100;
		client->pers.max_ammo[AMMO_CELLS] = 200;
		client->pers.max_ammo[AMMO_GRENADES] = 50;
		client->pers.max_ammo[AMMO_ROCKETS] = 50;
		client->pers.max_ammo[AMMO_SLUGS] = 50;
		client->pers.max_ammo[AMMO_MAGSLUG] = 50;
		client->pers.max_ammo[AMMO_FLECHETTES] = 200;
		client->pers.max_ammo[AMMO_DISRUPTOR] = 12;
		client->pers.max_ammo[AMMO_PROX] = 50;
		client->pers.max_ammo[AMMO_TESLA] = 50;
		client->pers.max_ammo[AMMO_TRAP] = 5;

		// Apply PvM stat bonuses (Max Ammo, Vitality from PvM levels)
		PvM_ApplyStatBonuses(ent);

		// Apply skill-based upgrades (vitality, max ammo from skill points)
		ApplySkillBonuses(ent);

		// Set health to max after all bonuses are applied
		client->pers.health = client->pers.max_health;

		// Clear inventory (they only get respawn weapon)
		client->pers.inventory.fill(0);

		// Give respawn weapon and ammo (handled by PVM_GiveRespawnWeapon)
		PVM_GiveRespawnWeapon(ent);

		// Always give blaster as backup weapon
		client->pers.inventory[IT_WEAPON_BLASTER] = 1;
		client->pers.inventory[IT_ITEM_MENU] = 1;
		client->pers.inventory[IT_ITEM_FLASHLIGHT] = 1;

		return;
	}

	// Cache wave level for multiple checks

	const bool is_high_wave = wave >= HordeConstants::WAVE_HIGH_AMMO_CAPS;

	//
	// HEALTH INITIALIZATION
	//
	const int saved_adrenaline = client->pers.adrenaline_count;
	const int new_max_health = CalculateWaveBasedMaxHealth(100, client);
	client->pers.max_health = client->resp.max_health = ent->max_health = new_max_health;
	client->pers.health = new_max_health;
	client->pers.adrenaline_count = saved_adrenaline;



	//
	// AMMO CAP INITIALIZATION
	//
	// Use constexpr arrays for better cache locality
	using AmmoConfig = struct { int bullets, shells, cells, flechettes, grenades, rockets, slugs, magslug, disruptor, tesla, prox, trap; };
	
	constexpr AmmoConfig basic_ammo = {
		HordeConstants::BasicAmmo::BULLETS, HordeConstants::BasicAmmo::SHELLS,
		HordeConstants::BasicAmmo::CELLS, HordeConstants::BasicAmmo::FLECHETTES,
		HordeConstants::BasicAmmo::GRENADES, HordeConstants::BasicAmmo::ROCKETS,
		HordeConstants::BasicAmmo::SLUGS, HordeConstants::BasicAmmo::MAGSLUG,
		HordeConstants::BasicAmmo::DISRUPTOR, HordeConstants::BasicAmmo::TESLA,
		HordeConstants::BasicAmmo::PROX, HordeConstants::BasicAmmo::TRAP
	};
	
	constexpr AmmoConfig high_ammo = {
		HordeConstants::HighAmmo::BULLETS, HordeConstants::HighAmmo::SHELLS,
		HordeConstants::HighAmmo::CELLS, HordeConstants::HighAmmo::FLECHETTES,
		HordeConstants::HighAmmo::GRENADES, HordeConstants::HighAmmo::ROCKETS,
		HordeConstants::HighAmmo::SLUGS, HordeConstants::HighAmmo::MAGSLUG,
		HordeConstants::HighAmmo::DISRUPTOR, HordeConstants::HighAmmo::TESLA,
		HordeConstants::HighAmmo::PROX, HordeConstants::HighAmmo::TRAP
	};
	
	const AmmoConfig& ammo = is_high_wave ? high_ammo : basic_ammo;
	
	client->pers.max_ammo[AMMO_BULLETS] = ammo.bullets;
	client->pers.max_ammo[AMMO_SHELLS] = ammo.shells;
	client->pers.max_ammo[AMMO_CELLS] = ammo.cells;
	client->pers.max_ammo[AMMO_FLECHETTES] = ammo.flechettes;
	client->pers.max_ammo[AMMO_GRENADES] = ammo.grenades;
	client->pers.max_ammo[AMMO_ROCKETS] = ammo.rockets;
	client->pers.max_ammo[AMMO_SLUGS] = ammo.slugs;
	client->pers.max_ammo[AMMO_MAGSLUG] = ammo.magslug;
	client->pers.max_ammo[AMMO_DISRUPTOR] = ammo.disruptor;
	client->pers.max_ammo[AMMO_TESLA] = ammo.tesla;
	client->pers.max_ammo[AMMO_PROX] = ammo.prox;
	client->pers.max_ammo[AMMO_TRAP] = ammo.trap;

	// Apply skill bonuses (vitality, max ammo)
	ApplySkillBonuses(ent);

	//
	// HORDE-SPECIFIC ITEMS
	//
	client->pers.inventory[IT_ITEM_MENU] = 1;
	client->pers.inventory[IT_ITEM_FLASHLIGHT] = 1;

	//
	// BOT TECH INITIALIZATION
	//
	if ((ent->svflags & SVF_BOT) && ent->client->resp.ctf_team != CTF_NOTEAM)
	{
		bool bot_has_tech = false;
		for (size_t i = 0; i < IT_TOTAL && !bot_has_tech; i++)
		{
			if ((itemlist[i].flags & IF_TECH) && client->pers.inventory[i] > 0)
				bot_has_tech = true;
		}

		if (!bot_has_tech)
			client->pers.inventory[IT_TECH_STRENGTH] = 1;
	}

	// NOTE: Weapon and ammo loadouts are now handled via g_start_items,
	// which is dynamically updated each wave by Horde_UpdateStartItemsForWave()
	// and applied in InitClientPersistant via Player_GiveStartItems()
}



// Modified InitClientPersistant
void InitClientPersistant(edict_t* ent, gclient_t* client)
{
	// Cache cvar checks
	const bool is_deathmatch = deathmatch->integer != 0;
	const bool is_coop = coop->integer != 0;
	const bool is_horde = g_horde && g_horde->integer != 0;

	// Backup & restore userinfo
	char userinfo[MAX_INFO_STRING];
	Q_strlcpy(userinfo, client->pers.userinfo, sizeof(userinfo));
	ClientUserinfoChanged(ent, userinfo);

	//
	// HEALTH INITIALIZATION (Default - Horde mode will override if active)
	//
	client->pers.health = 100;
	client->pers.max_health = 100;
	ent->max_health = 100;

	//
	// SKILLS INITIALIZATION (New unified upgrade system)
	//
	// Initialize skills structure to zero (only on first connect)
	// Skills persist across respawns via client->pers
	if (client->pers.pvm_level == 0)
	{
		client->pers.skills = player_skills_t(); // Zero-initialize all skills
		client->pers.skill_points = 0;
		client->pers.pvm_level = 0; // Start at level 0
		client->pers.pvm_xp = 0;
	}

	//
	// BLASTER AMMO INITIALIZATION (Vortex-style)
	//
	client->blaster_ammo = 25;
	client->blaster_regen_time = level.time;

	//
	// ARMOR INITIALIZATION (Clear existing)
	//
	client->pers.inventory[IT_ARMOR_BODY] =
		client->pers.inventory[IT_ARMOR_COMBAT] =
		client->pers.inventory[IT_ARMOR_JACKET] =
		client->pers.inventory[IT_ARMOR_SHARD] = 0;

	//
	// INVENTORY & AMMO INITIALIZATION
	//
	bool taken_loadout = false;

	// Check for coop inheritance
	if (G_IsCooperative())
	{
		for (auto player : active_players())
		{
			if (player == ent || !player->client->pers.spawned ||
				player->client->resp.spectator || player->movetype == MOVETYPE_NOCLIP)
				continue;

			client->pers.inventory = player->client->pers.inventory;
			client->pers.max_ammo = player->client->pers.max_ammo;
			client->pers.power_cubes = player->client->pers.power_cubes;
			taken_loadout = true;
			break;
		}
	}

	if (!taken_loadout)
	{
		// Base ammo initialization (Default values)
		client->pers.max_ammo.fill(50);
		client->pers.max_ammo[AMMO_BULLETS] = 200;
		client->pers.max_ammo[AMMO_SHELLS] = 100;
		client->pers.max_ammo[AMMO_CELLS] = 200;
		client->pers.max_ammo[AMMO_FLECHETTES] = 200;
		client->pers.max_ammo[AMMO_GRENADES] = 50;
		client->pers.max_ammo[AMMO_ROCKETS] = 50;
		client->pers.max_ammo[AMMO_SLUGS] = 50;
		client->pers.max_ammo[AMMO_MAGSLUG] = 50;
		client->pers.max_ammo[AMMO_DISRUPTOR] = 12;
		client->pers.max_ammo[AMMO_TESLA] = 5;
		client->pers.max_ammo[AMMO_PROX] = 50;
		client->pers.max_ammo[AMMO_TRAP] = 5;

		// Give blaster in deathmatch (non-Horde deathmatch only)
		if (is_deathmatch && !is_horde)
			client->pers.inventory[IT_WEAPON_BLASTER] = 1;

		// Process start items (skip in PvM mode)
		if (!IsPvMMode())
		{
			if (g_start_items && *g_start_items->string)
				Player_GiveStartItems(ent, g_start_items->string);
			if (level.start_items && *level.start_items)
				Player_GiveStartItems(ent, level.start_items);
		}

		G_CheckPowerArmor(ent);

		// Standard items for non-deathmatch/coop
		if (!is_deathmatch || is_coop)
		{
			if (!is_horde)
				client->pers.inventory[IT_ITEM_FLASHLIGHT] = 1;
			client->pers.inventory[IT_ITEM_COMPASS] = 1;
		}

		//
		// HORDE-SPECIFIC INITIALIZATION
		//
		if (is_horde)
		{
			// Load character data BEFORE initializing (needed for respawn weapon)
			Character_Load(ent);

			Horde_InitClientPersistant(ent, client);
		}

		// Handle grapple
		const bool give_grapple = (g_allow_grapple && strcmp(g_allow_grapple->string, "auto") == 0) ?
			(ctf && ctf->integer && !level.no_grapple) :
			(g_allow_grapple && g_allow_grapple->integer);

		if (give_grapple)
			client->pers.inventory[IT_WEAPON_GRAPPLE] = 1;
	}

	//
	// WEAPON SELECTION
	// Try last weapon, fallback to NoAmmoWeaponChange, then Blaster
	//
	if (client->pers.lastweapon && client->pers.inventory[client->pers.lastweapon->id] > 0)
	{
		client->pers.weapon = client->pers.lastweapon;
		client->pers.selected_item = client->pers.lastweapon->id;
	}
	else
	{
		NoAmmoWeaponChange(ent, false);
		if (client->newweapon)
		{
			client->pers.weapon = client->newweapon;
			client->pers.selected_item = client->newweapon->id;
		}
		else
		{
			// Fallback to Blaster
			gitem_t* blasterItem = FindItem("Blaster");
			if (blasterItem)
			{
				client->pers.selected_item = blasterItem->id;
				if (client->pers.inventory[blasterItem->id] <= 0)
					client->pers.inventory[blasterItem->id] = 1;
				client->pers.weapon = blasterItem;
			}
			else
			{
				client->pers.weapon = nullptr;
				client->pers.selected_item = IT_NULL;
			}
		}
	}

	//
	// FINAL SETUP
	//
	if (G_IsCooperative() && g_coop_enable_lives && g_coop_enable_lives->integer)
		client->pers.lives = g_coop_num_lives ? g_coop_num_lives->integer + 1 : 1;

	if (ent->client->pers.autoshield >= AUTO_SHIELD_AUTO)
		client->pers.savedFlags |= FL_WANTS_POWER_ARMOR;

	// Apply Start Armor skill - 10 armor per level
	if (client->pers.skills.start_armor > 0)
	{
		client->pers.inventory[IT_ARMOR_BODY] = client->pers.skills.start_armor * 10;
	}

	client->pers.connected = true;
	client->pers.spawned = true;
}

void InitClientResp(gclient_t* client)
{
	const ctfteam_t ctf_team = client->resp.ctf_team;
	const bool id_state = client->pers.id_state;      // just save current state
	const bool iddmg_state = client->pers.iddmg_state; // just save current state

	client->resp = {};

	client->resp.ctf_team = ctf_team;
	client->pers.id_state = id_state;
	client->pers.iddmg_state = iddmg_state;
	client->resp.sentry_gun_choice = client->pers.sentry_gun_choice;

	client->resp.entertime = level.time;
	client->resp.coop_respawn = client->pers;

	// Initialize camera mode flags
	client->use_eyecam = false;
	client->auto_eyecam = true;
}

/*
==================
SaveClientData

Some information that should be persistant, like health,
is still stored in the edict structure, so it needs to
be mirrored out to the client structure before all the
edicts are wiped.
==================
*/
void SaveClientData()
{
	const bool is_coop = coop->integer != 0; // Cache cvar check
	
	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		edict_t* ent = &g_edicts[1 + i];
		if (!ent->inuse)
			continue;
		
		gclient_t* client = &game.clients[i];
		client->pers.health = ent->health;
		client->pers.max_health = ent->max_health;
		client->pers.savedFlags = (ent->flags & (FL_FLASHLIGHT | FL_GODMODE | FL_NOTARGET | FL_POWER_ARMOR | FL_WANTS_POWER_ARMOR));
		
		if (is_coop)
		{
			client->pers.score = ent->client->resp.score;
			client->pers.id_state = ent->client->pers.id_state;
			client->pers.iddmg_state = ent->client->pers.iddmg_state;
			client->pers.sentry_gun_choice = ent->client->pers.sentry_gun_choice;
		}
	}
}
void FetchClientEntData(edict_t* ent)
{
	ent->health = ent->client->pers.health;
	ent->max_health = ent->client->pers.max_health;
	ent->flags |= ent->client->pers.savedFlags;
	if (coop->integer)
		ent->client->resp.score = ent->client->pers.score;
}


/*
=======================================================================

  SelectSpawnPoint

=======================================================================
*/

/*
================
PlayersRangeFromSpot

Returns the distance to the nearest player from the given spot
================
*/
float PlayersRangeFromSpot(edict_t* spot)
{
	float best_distance = 9999999.0f;

	for (uint32_t n = 1; n <= game.maxclients; n++)
	{
		edict_t* player = &g_edicts[n];

		// Early exits for invalid players
		if (!player->inuse || !player->solid || player->health <= 0)
			continue;

		// Calculate distance (squared distance is faster, but we need actual for sorting)
		vec3_t delta = spot->s.origin - player->s.origin;
		float distance = delta.length();

		if (distance < best_distance)
			best_distance = distance;
	}

	return best_distance;
}

bool SpawnPointClear(edict_t* spot)
{
	vec3_t p = spot->s.origin + vec3_t{ 0, 0, 9.f };
	return !gi.trace(p, PLAYER_MINS, PLAYER_MAXS, p, spot, CONTENTS_PLAYER | CONTENTS_MONSTER).startsolid;
}

select_spawn_result_t SelectDeathmatchSpawnPoint(bool farthest, bool force_spawn, bool fallback_to_ctf_or_start)
{
	struct spawn_point_t
	{
		edict_t* point;
		float dist;
	};

	static std::vector<spawn_point_t> spawn_points;
	spawn_points.clear();

	// Cache classname strings for faster comparison
	static constexpr const char* dm_spawn_class = "info_player_deathmatch";
	static constexpr const char* team1_spawn_class = "info_player_team1";
	static constexpr const char* team2_spawn_class = "info_player_team2";
	static constexpr const char* start_spawn_class = "info_player_start";

	// Gather all potential spawn points in a single pass over the edict list
	for (uint32_t i = 1; i < globals.num_edicts; i++)
	{
		edict_t* spot = &g_edicts[i];
		if (!spot->inuse || !spot->classname)
			continue;

		const char* classname = spot->classname;
		bool is_valid_spawn = (strcmp(classname, dm_spawn_class) == 0);

		if (!is_valid_spawn && fallback_to_ctf_or_start)
		{
			is_valid_spawn = (strcmp(classname, team1_spawn_class) == 0 || 
			                  strcmp(classname, team2_spawn_class) == 0);
		}

		if (is_valid_spawn)
			spawn_points.push_back({ spot, PlayersRangeFromSpot(spot) });
	}

	// If still no points after the main loop, try the absolute fallback
	if (spawn_points.empty() && fallback_to_ctf_or_start)
	{
		edict_t* spot = G_FindByString<&edict_t::classname>(nullptr, start_spawn_class);
		if (spot)
			spawn_points.push_back({ spot, PlayersRangeFromSpot(spot) });
	}

	// No points at all
	if (spawn_points.empty())
		return { nullptr, false };

	// If there's only one spawn point, that's the one
	if (spawn_points.size() == 1)
	{
		if (force_spawn || SpawnPointClear(spawn_points[0].point))
			return { spawn_points[0].point, true };
		return { nullptr, true };
	}

	// Sort by distance ascending (closest players at top of list)
	std::sort(spawn_points.begin(), spawn_points.end(), 
		[](const spawn_point_t& a, const spawn_point_t& b) { return a.dist < b.dist; });

	// Farthest spawn selection
	if (farthest)
	{
		for (int32_t i = static_cast<int32_t>(spawn_points.size()) - 1; i >= 0; --i)
		{
			if (SpawnPointClear(spawn_points[i].point))
				return { spawn_points[i].point, true };
		}
	}
	else
	{
		// Random spawn selection (avoid the 2 closest to any player)
		if (spawn_points.size() > 2)
			std::shuffle(spawn_points.begin() + 2, spawn_points.end(), mt_rand);

		// Pick first clear spawn from shuffled list
		if (spawn_points.size() > 2)
		{
			for (auto it = spawn_points.begin() + 2; it != spawn_points.end(); ++it)
			{
				if (SpawnPointClear(it->point))
					return { it->point, true };
			}
		}

		// No clear spawns in safe zone, try the 2 closest
		if (SpawnPointClear(spawn_points[1].point))
			return { spawn_points[1].point, true };
		else if (SpawnPointClear(spawn_points[0].point))
			return { spawn_points[0].point, true };
	}

	// All checks failed but we must spawn - pick random
	if (force_spawn)
		return { random_element(spawn_points).point, true };

	return { nullptr, true };
}

//===============
// ROGUE
edict_t* SelectLavaCoopSpawnPoint(edict_t* ent)
{
	constexpr float LAVA_SAFETY_MARGIN = 64.0f;
	constexpr float PLAYER_MIN_DISTANCE = 32.0f;
	constexpr int MAX_SPAWN_POINTS = 64;
	
	edict_t* spawnPoints[MAX_SPAWN_POINTS];
	int numPoints = 0;
	
	// Find the highest lava level
	float lavatop = -99999.0f;
	edict_t* lava = nullptr;
	
	while ((lava = G_FindByString<&edict_t::classname>(lava, "func_water")))
	{
		if (lava->spawnflags.has(SPAWNFLAG_WATER_SMART))
		{
			vec3_t center = (lava->absmax + lava->absmin) * 0.5f;
			if ((gi.pointcontents(center) & MASK_WATER) && lava->absmax[2] > lavatop)
				lavatop = lava->absmax[2];
		}
	}

	// No lava found
	if (lavatop < -99000.0f)
		return nullptr;

	// Add safety margin
	lavatop += LAVA_SAFETY_MARGIN;

	// Collect all valid lava spawn points
	edict_t* spot = nullptr;
	while ((spot = G_FindByString<&edict_t::classname>(spot, "info_player_coop_lava")) && numPoints < MAX_SPAWN_POINTS)
	{
		spawnPoints[numPoints++] = spot;
	}

	// Find the lowest safe spawn point with sufficient player distance
	edict_t* best_spawn = nullptr;
	float lowest_safe_height = 999999.0f;
	
	for (int i = 0; i < numPoints; i++)
	{
		edict_t* spawn = spawnPoints[i];
		
		// Skip if below lava or too close to players
		if (spawn->s.origin[2] < lavatop || PlayersRangeFromSpot(spawn) <= PLAYER_MIN_DISTANCE)
			continue;
		
		// Pick lowest safe spot
		if (spawn->s.origin[2] < lowest_safe_height)
		{
			lowest_safe_height = spawn->s.origin[2];
			best_spawn = spawn;
		}
	}

	return best_spawn;
}
// ROGUE
//===============

// [Paril-KEX]
edict_t* SelectSingleSpawnPoint(edict_t* ent)
{
	edict_t* spot = nullptr;

	while ((spot = G_FindByString<&edict_t::classname>(spot, "info_player_start")) != nullptr)
	{
		if (!game.spawnpoint[0] && !spot->targetname)
			break;

		if (!game.spawnpoint[0] || !spot->targetname)
			continue;

		if (Q_strcasecmp(game.spawnpoint, spot->targetname) == 0)
			break;
	}

	if (!spot)
	{
		// there wasn't a matching targeted spawnpoint, use one that has no targetname
		while ((spot = G_FindByString<&edict_t::classname>(spot, "info_player_start")) != nullptr)
			if (!spot->targetname)
				return spot;
	}

	// none at all, so just pick any
	if (!spot)
		return G_FindByString<&edict_t::classname>(spot, "info_player_start");

	return spot;
}


// [Paril-KEX]
static edict_t* G_UnsafeSpawnPosition(vec3_t spot, bool check_players)
{
	contents_t mask = MASK_PLAYERSOLID;

	if (!check_players)
		mask &= ~CONTENTS_PLAYER;

	trace_t tr = gi.trace(spot, PLAYER_MINS, PLAYER_MAXS, spot, nullptr, mask);

	// sometimes the spot is too close to the ground, give it a bit of slack
	if (tr.startsolid && !tr.ent->client)
	{
		spot[2] += 1;
		tr = gi.trace(spot, PLAYER_MINS, PLAYER_MAXS, spot, nullptr, mask);
	}

	// no idea why this happens in some maps..
	if (tr.startsolid && !tr.ent->client)
	{
		// try a nudge
		if (G_FixStuckObject_Generic(spot, PLAYER_MINS, PLAYER_MAXS, [mask](const vec3_t& start, const vec3_t& mins, const vec3_t& maxs, const vec3_t& end) {
			return gi.trace(start, mins, maxs, end, nullptr, mask);
			}) == stuck_result_t::NO_GOOD_POSITION)
			return tr.ent; // what do we do here...?

		trace_t tr = gi.trace(spot, PLAYER_MINS, PLAYER_MAXS, spot, nullptr, mask);

		if (tr.startsolid && !tr.ent->client)
			return tr.ent; // what do we do here...?
	}

	if (tr.fraction == 1.f)
		return nullptr;
	else if (check_players && tr.ent && tr.ent->client)
		return tr.ent;

	return nullptr;
}

edict_t* SelectCoopSpawnPoint(edict_t* ent, bool force_spawn, bool check_players)
{
	edict_t* spot = nullptr;
	const char* target;

	// ROGUE
	//  rogue hack, but not too gross...
	if (!Q_strcasecmp(level.mapname, "rmine2"))
		return SelectLavaCoopSpawnPoint(ent);
	// ROGUE

	// try the main spawn point first
	spot = SelectSingleSpawnPoint(ent);

	if (spot && !G_UnsafeSpawnPosition(spot->s.origin, check_players))
		return spot;

	spot = nullptr;

	// assume there are four coop spots at each spawnpoint
	int32_t num_valid_spots = 0;

	while (1)
	{
		spot = G_FindByString<&edict_t::classname>(spot, "info_player_coop");
		if (!spot)
			break; // we didn't have enough...

		target = spot->targetname;
		if (!target)
			target = "";
		if (Q_strcasecmp(game.spawnpoint, target) == 0)
		{ // this is a coop spawn point for one of the clients here
			num_valid_spots++;

			if (!G_UnsafeSpawnPosition(spot->s.origin, check_players))
				return spot; // this is it
		}
	}

	bool use_targetname = true;

	// if we didn't find any spots, map is probably set up wrong.
	// use empty targetname ones.
	if (!num_valid_spots)
	{
		use_targetname = false;

		while (1)
		{
			spot = G_FindByString<&edict_t::classname>(spot, "info_player_coop");
			if (!spot)
				break; // we didn't have enough...

			target = spot->targetname;
			if (!target)
			{
				// this is a coop spawn point for one of the clients here
				num_valid_spots++;

				if (!G_UnsafeSpawnPosition(spot->s.origin, check_players))
					return spot; // this is it
			}
		}
	}

	// if player collision is disabled, just pick a random spot
	if (!g_coop_player_collision->integer)
	{
		spot = nullptr;

		num_valid_spots = irandom(num_valid_spots);

		while (1)
		{
			spot = G_FindByString<&edict_t::classname>(spot, "info_player_coop");

			if (!spot)
				break; // we didn't have enough...

			target = spot->targetname;
			if (use_targetname && !target)
				target = "";
			if (use_targetname ? (Q_strcasecmp(game.spawnpoint, target) == 0) : !target)
			{ // this is a coop spawn point for one of the clients here
				num_valid_spots++;

				if (!num_valid_spots)
					return spot;

				--num_valid_spots;
			}
		}

		// if this fails, just fall through to some other spawn.
	}

	// no safe spots..?
	if (force_spawn || !g_coop_player_collision->integer)
		return SelectSingleSpawnPoint(spot);

	return nullptr;
}

bool TryLandmarkSpawn(edict_t* ent, vec3_t& origin, vec3_t& angles)
{
	// if transitioning from another level with a landmark seamless transition
	// just set the location here
	if (!ent->client->landmark_name || !strlen(ent->client->landmark_name))
	{
		return false;
	}

	edict_t* landmark = G_PickTarget(ent->client->landmark_name);
	if (!landmark)
	{
		return false;
	}

	vec3_t old_origin = origin;
	vec3_t spot_origin = origin;
	origin = ent->client->landmark_rel_pos;

	// rotate our relative landmark into our new landmark's frame of reference
	origin = RotatePointAroundVector({ 1, 0, 0 }, origin, landmark->s.angles[0]);
	origin = RotatePointAroundVector({ 0, 1, 0 }, origin, landmark->s.angles[2]);
	origin = RotatePointAroundVector({ 0, 0, 1 }, origin, landmark->s.angles[1]);

	origin += landmark->s.origin;

	angles = ent->client->oldviewangles + landmark->s.angles;

	if (landmark->spawnflags.has(SPAWNFLAG_LANDMARK_KEEP_Z))
		origin[2] = spot_origin[2];

	// sometimes, landmark spawns can cause slight inconsistencies in collision;
	// we'll do a bit of tracing to make sure the bbox is clear
	if (G_FixStuckObject_Generic(origin, PLAYER_MINS, PLAYER_MAXS, [ent](const vec3_t& start, const vec3_t& mins, const vec3_t& maxs, const vec3_t& end) {
		return gi.trace(start, mins, maxs, end, ent, MASK_PLAYERSOLID & ~CONTENTS_PLAYER);
		}) == stuck_result_t::NO_GOOD_POSITION)
	{
		origin = old_origin;
		return false;
	}

	ent->s.origin = origin;

	// rotate the velocity that we grabbed from the map
	if (ent->velocity)
	{
		ent->velocity = RotatePointAroundVector({ 1, 0, 0 }, ent->velocity, landmark->s.angles[0]);
		ent->velocity = RotatePointAroundVector({ 0, 1, 0 }, ent->velocity, landmark->s.angles[2]);
		ent->velocity = RotatePointAroundVector({ 0, 0, 1 }, ent->velocity, landmark->s.angles[1]);
	}

	return true;
}

/*
===========
SelectSpawnPoint

Chooses a player start, deathmatch start, coop start, etc
============
*/
bool SelectSpawnPoint(edict_t* ent, vec3_t& origin, vec3_t& angles, bool force_spawn, bool& landmark)
{
	edict_t* spot = nullptr;

	// DM spots are simple
	if (deathmatch->integer)
	{
		if (G_TeamplayEnabled())
			spot = SelectCTFSpawnPoint(ent, force_spawn);
		else
		{
			select_spawn_result_t result = SelectDeathmatchSpawnPoint(g_dm_spawn_farthest->integer, force_spawn, true);

			if (!result.any_valid)
				gi.Com_Error("no valid spawn points found");

			spot = result.spot;
		}

		if (spot)
		{
			origin = spot->s.origin + vec3_t{ 0, 0, static_cast<float>(g_dm_spawns->integer ? 9 : 1) };
			angles = spot->s.angles;

			return true;
		}

		return false;
	}

	if (G_IsCooperative())
	{
		spot = SelectCoopSpawnPoint(ent, force_spawn, true);

		if (!spot)
			spot = SelectCoopSpawnPoint(ent, force_spawn, false);

		// no open spot yet
		if (!spot)
		{
			// in worst case scenario in coop during intermission, just spawn us at intermission
			// spot. this only happens for a single frame, and won't break
			// anything if they come back.
			if (level.intermissiontime)
			{
				origin = level.intermission_origin;
				angles = level.intermission_angle;
				return true;
			}

			return false;
		}
	}
	else
	{
		spot = SelectSingleSpawnPoint(ent);

		// in SP, just put us at the origin if spawn fails
		if (!spot)
		{
			gi.Com_PrintFmt("PRINT: Couldn't find spawn point {}\n", game.spawnpoint);

			origin = { 0, 0, 0 };
			angles = { 0, 0, 0 };

			return true;
		}
	}

	// spot should always be non-null here

	origin = spot->s.origin;
	angles = spot->s.angles;

	// check landmark
	// Paril: client check
	if (ent->client && TryLandmarkSpawn(ent, origin, angles))
		landmark = true;

	return true;
}

//======================================================================

void InitBodyQue()
{
	edict_t* ent;

	level.body_que = 0;
	// FIX: Use size_t for the loop counter to match BODY_QUEUE_SIZE.
	for (size_t i = 0; i < BODY_QUEUE_SIZE; i++)
	{
		ent = G_Spawn();
		ent->classname = "bodyque";
	}
}

DIE(body_die) (edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
	// Don't create skull if being crushed - prevent infinite explosion loop
	if (mod.id == MOD_CRUSH)
	{
		// prevent explosion singularities
		self->svflags = SVF_NOCLIENT;
		self->takedamage = false;
		self->solid = SOLID_NOT;
		self->movetype = MOVETYPE_NOCLIP;
		self->die = nullptr;
		gi.unlinkentity(self);
		return;
	}

	if (self->s.modelindex == MODELINDEX_PLAYER &&
		self->health < self->gib_health)
	{
		gi.sound(self, CHAN_BODY, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);
		ThrowGibs(self, damage, { { 4, "models/objects/gibs/sm_meat/tris.md2" } });
		self->s.origin[2] -= 48;
		ThrowClientHead(self, damage);
	}
}

void CopyToBodyQue(edict_t* ent)
{
	// if we were completely removed, don't bother with a body
	if (!ent->s.modelindex)
		return;

	edict_t* body;

	// grab a body que and cycle to the next one
	body = &g_edicts[game.maxclients + level.body_que + 1];
	level.body_que = (level.body_que + 1) % BODY_QUEUE_SIZE;

	// FIXME: send an effect on the removed body

	gi.unlinkentity(ent);

	gi.unlinkentity(body);
	body->s = ent->s;
	body->s.number = body - g_edicts;
	body->s.skinnum = ent->s.skinnum & 0xFF; // only copy the client #
	body->s.effects = EF_NONE;
	body->s.renderfx = RF_NONE;

	body->svflags = ent->svflags;
	body->absmin = ent->absmin;
	body->absmax = ent->absmax;
	body->size = ent->size;
	body->solid = ent->solid;
	body->clipmask = ent->clipmask;
	body->owner = ent->owner;
	body->movetype = ent->movetype;
	body->health = ent->health;
	body->gib_health = ent->gib_health;
	body->s.event = EV_OTHER_TELEPORT;
	body->velocity = ent->velocity;
	body->avelocity = ent->avelocity;
	body->groundentity = ent->groundentity;
	body->groundentity_linkcount = ent->groundentity_linkcount;

	if (ent->takedamage)
	{
		body->mins = ent->mins;
		body->maxs = ent->maxs;
	}
	else
		body->mins = body->maxs = {};

	body->die = body_die;
	body->takedamage = true;

	// Don't link if body would be crushed immediately
	trace_t tr = gi.trace(body->s.origin, body->mins, body->maxs, body->s.origin, body, MASK_SOLID);
	if (!tr.startsolid)
		gi.linkentity(body);
	else
	{
		// Body would spawn in solid - just disable it
		body->solid = SOLID_NOT;
		body->takedamage = false;
		body->s.modelindex = 0;
	}
}

void G_PostRespawn(edict_t* self)
{
	if (self->svflags & SVF_NOCLIENT)
		return;

	// add a teleportation effect
	self->s.event = EV_PLAYER_TELEPORT;

	// hold in place briefly
	self->client->ps.pmove.pm_flags = PMF_TIME_TELEPORT;
	self->client->ps.pmove.pm_time = 112;

	self->client->respawn_time = level.time;

	if (g_horde->integer || pvm->integer)
		self->client->invincible_time = max(level.time, self->client->invincible_time) + 2_sec;    // RESPAWN INVULNERABILITY EACH RESPAWN EVERY MODE
}

void respawn(edict_t* self)
{
	if (deathmatch->integer || coop->integer)
	{
		// spectators don't leave bodies
		if (!self->client->resp.spectator)
			CopyToBodyQue(self);
		self->svflags &= ~SVF_NOCLIENT;
		PutClientInServer(self);

		G_PostRespawn(self);
		self->client->resp.spree = 0;
		return;
	}

	// restart the entire server
	gi.AddCommandString("menu_loadgame\n");
}

/*
 * only called when pers.spectator changes
 * note that resp.spectator should be the opposite of pers.spectator here
 */
void spectator_respawn(edict_t* ent)
{
	uint32_t i, numspec;

	// if the user wants to become a spectator, make sure he doesn't
	// exceed max_spectators

	if (ent->client->pers.spectator)
	{
		char value[MAX_INFO_VALUE] = { 0 };
		gi.Info_ValueForKey(ent->client->pers.userinfo, "spectator", value, sizeof(value));

		if (ValidatePassword(spectator_password->string, value))
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "Spectator password incorrect.\n");
			ent->client->pers.spectator = false;
			gi.WriteByte(svc_stufftext);
			gi.WriteString("spectator 0\n");
			gi.unicast(ent, true);
			return;
		}

		// count spectators
		for (i = 1, numspec = 0; i <= game.maxclients; i++)
			if (g_edicts[i].inuse && g_edicts[i].client->pers.spectator)
				numspec++;

		if (numspec >= (uint32_t)maxspectators->integer)
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "Server spectator limit is full.");
			ent->client->pers.spectator = false;
			// reset his spectator var
			gi.WriteByte(svc_stufftext);
			gi.WriteString("spectator 0\n");
			gi.unicast(ent, true);
			return;
		}
	}
	else
	{
		// he was a spectator and wants to join the game
		// he must have the right password
		char value[MAX_INFO_VALUE] = { 0 };
		gi.Info_ValueForKey(ent->client->pers.userinfo, "password", value, sizeof(value));

		if (ValidatePassword(password->string, value))
		{
			gi.LocClient_Print(ent, PRINT_HIGH, "Password incorrect.\n");
			ent->client->pers.spectator = true;
			gi.WriteByte(svc_stufftext);
			gi.WriteString("spectator 1\n");
			gi.unicast(ent, true);
			return;
		}
	}

	// clear score on respawn
	ent->client->resp.score = ent->client->pers.score = 0;

	// move us to no team
	ent->client->resp.ctf_team = CTF_NOTEAM;

	// change spectator mode
	ent->client->resp.spectator = ent->client->pers.spectator;

	ent->svflags &= ~SVF_NOCLIENT;
	PutClientInServer(ent);

	// add a teleportation effect
	if (!ent->client->pers.spectator)
	{
		// send effect
		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(MZ_LOGIN);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);

		// hold in place briefly
		ent->client->ps.pmove.pm_flags = PMF_TIME_TELEPORT;
		ent->client->ps.pmove.pm_time = 112;
	}

	ent->client->respawn_time = level.time;

	if (ent->client->pers.spectator)
		gi.LocBroadcast_Print(PRINT_HIGH, "$g_observing", ent->client->pers.netname);
	else
		gi.LocBroadcast_Print(PRINT_HIGH, "$g_joined_game", ent->client->pers.netname);
}

//==============================================================

// [Paril-KEX]
// skinnum was historically used to pack data
// so we're going to build onto that.
void P_AssignClientSkinnum(edict_t* ent) noexcept
{
	if (ent->s.modelindex != 255)
		return;

	player_skinnum_t packed;

	packed.client_num = ent->client - game.clients;
	if (ent->client->pers.weapon)
		packed.vwep_index = ent->client->pers.weapon->vwep_index - level.vwep_offset + 1;
	else
		packed.vwep_index = 0;
	packed.viewheight = ent->client->ps.viewoffset.z + ent->client->ps.pmove.viewheight;

	if (G_IsCooperative())
		packed.team_index = 1; // all players are teamed in coop
	else if (G_TeamplayEnabled())
		packed.team_index = ent->client->resp.ctf_team;
	else
		packed.team_index = 0;

	if (ent->deadflag)
		packed.poi_icon = 1;
	else
		packed.poi_icon = 0;

	ent->s.skinnum = packed.skinnum;
}

// [Paril-KEX] send player level POI
void P_SendLevelPOI(edict_t* ent)
{
	if (!level.valid_poi)
		return;

	gi.WriteByte(svc_poi);
	gi.WriteShort(POI_OBJECTIVE);
	gi.WriteShort(10000);
	gi.WritePosition(ent->client->help_poi_location);
	gi.WriteShort(ent->client->help_poi_image);
	gi.WriteByte(208);
	gi.WriteByte(POI_FLAG_NONE);
	gi.unicast(ent, true);
}

// [Paril-KEX] force the fog transition on the given player,
// optionally instantaneously (ignore any transition time)
void P_ForceFogTransition(edict_t* ent, bool instant)
{
	// sanity check; if we're not changing the values, don't bother
	if (ent->client->fog == ent->client->pers.wanted_fog &&
		ent->client->heightfog == ent->client->pers.wanted_heightfog)
		return;

	svc_fog_data_t fog{};

	// check regular fog
	if (ent->client->pers.wanted_fog[0] != ent->client->fog[0] ||
		ent->client->pers.wanted_fog[4] != ent->client->fog[4])
	{
		fog.bits |= svc_fog_data_t::BIT_DENSITY;
		fog.density = ent->client->pers.wanted_fog[0];
		fog.skyfactor = ent->client->pers.wanted_fog[4] * 255.f;
	}
	if (ent->client->pers.wanted_fog[1] != ent->client->fog[1])
	{
		fog.bits |= svc_fog_data_t::BIT_R;
		fog.red = ent->client->pers.wanted_fog[1] * 255.f;
	}
	if (ent->client->pers.wanted_fog[2] != ent->client->fog[2])
	{
		fog.bits |= svc_fog_data_t::BIT_G;
		fog.green = ent->client->pers.wanted_fog[2] * 255.f;
	}
	if (ent->client->pers.wanted_fog[3] != ent->client->fog[3])
	{
		fog.bits |= svc_fog_data_t::BIT_B;
		fog.blue = ent->client->pers.wanted_fog[3] * 255.f;
	}

	if (!instant && ent->client->pers.fog_transition_time)
	{
		fog.bits |= svc_fog_data_t::BIT_TIME;
		fog.time = clamp(ent->client->pers.fog_transition_time.milliseconds(), (int64_t)0, (int64_t)std::numeric_limits<uint16_t>::max());
	}

	// check heightfog stuff
	auto& hf = ent->client->heightfog;
	const auto& wanted_hf = ent->client->pers.wanted_heightfog;

	if (hf.falloff != wanted_hf.falloff)
	{
		fog.bits |= svc_fog_data_t::BIT_HEIGHTFOG_FALLOFF;
		if (!wanted_hf.falloff)
			fog.hf_falloff = 0;
		else
			fog.hf_falloff = wanted_hf.falloff;
	}
	if (hf.density != wanted_hf.density)
	{
		fog.bits |= svc_fog_data_t::BIT_HEIGHTFOG_DENSITY;

		if (!wanted_hf.density)
			fog.hf_density = 0;
		else
			fog.hf_density = wanted_hf.density;
	}

	if (hf.start[0] != wanted_hf.start[0])
	{
		fog.bits |= svc_fog_data_t::BIT_HEIGHTFOG_START_R;
		fog.hf_start_r = wanted_hf.start[0] * 255.f;
	}
	if (hf.start[1] != wanted_hf.start[1])
	{
		fog.bits |= svc_fog_data_t::BIT_HEIGHTFOG_START_G;
		fog.hf_start_g = wanted_hf.start[1] * 255.f;
	}
	if (hf.start[2] != wanted_hf.start[2])
	{
		fog.bits |= svc_fog_data_t::BIT_HEIGHTFOG_START_B;
		fog.hf_start_b = wanted_hf.start[2] * 255.f;
	}
	if (hf.start[3] != wanted_hf.start[3])
	{
		fog.bits |= svc_fog_data_t::BIT_HEIGHTFOG_START_DIST;
		fog.hf_start_dist = wanted_hf.start[3];
	}

	if (hf.end[0] != wanted_hf.end[0])
	{
		fog.bits |= svc_fog_data_t::BIT_HEIGHTFOG_END_R;
		fog.hf_end_r = wanted_hf.end[0] * 255.f;
	}
	if (hf.end[1] != wanted_hf.end[1])
	{
		fog.bits |= svc_fog_data_t::BIT_HEIGHTFOG_END_G;
		fog.hf_end_g = wanted_hf.end[1] * 255.f;
	}
	if (hf.end[2] != wanted_hf.end[2])
	{
		fog.bits |= svc_fog_data_t::BIT_HEIGHTFOG_END_B;
		fog.hf_end_b = wanted_hf.end[2] * 255.f;
	}
	if (hf.end[3] != wanted_hf.end[3])
	{
		fog.bits |= svc_fog_data_t::BIT_HEIGHTFOG_END_DIST;
		fog.hf_end_dist = wanted_hf.end[3];
	}

	if (fog.bits & 0xFF00)
		fog.bits |= svc_fog_data_t::BIT_MORE_BITS;

	gi.WriteByte(svc_fog);

	if (fog.bits & svc_fog_data_t::BIT_MORE_BITS)
		gi.WriteShort(fog.bits);
	else
		gi.WriteByte(fog.bits);

	if (fog.bits & svc_fog_data_t::BIT_DENSITY)
	{
		gi.WriteFloat(fog.density);
		gi.WriteByte(fog.skyfactor);
	}
	if (fog.bits & svc_fog_data_t::BIT_R)
		gi.WriteByte(fog.red);
	if (fog.bits & svc_fog_data_t::BIT_G)
		gi.WriteByte(fog.green);
	if (fog.bits & svc_fog_data_t::BIT_B)
		gi.WriteByte(fog.blue);
	if (fog.bits & svc_fog_data_t::BIT_TIME)
		gi.WriteShort(fog.time);

	if (fog.bits & svc_fog_data_t::BIT_HEIGHTFOG_FALLOFF)
		gi.WriteFloat(fog.hf_falloff);
	if (fog.bits & svc_fog_data_t::BIT_HEIGHTFOG_DENSITY)
		gi.WriteFloat(fog.hf_density);

	if (fog.bits & svc_fog_data_t::BIT_HEIGHTFOG_START_R)
		gi.WriteByte(fog.hf_start_r);
	if (fog.bits & svc_fog_data_t::BIT_HEIGHTFOG_START_G)
		gi.WriteByte(fog.hf_start_g);
	if (fog.bits & svc_fog_data_t::BIT_HEIGHTFOG_START_B)
		gi.WriteByte(fog.hf_start_b);
	if (fog.bits & svc_fog_data_t::BIT_HEIGHTFOG_START_DIST)
		gi.WriteLong(fog.hf_start_dist);

	if (fog.bits & svc_fog_data_t::BIT_HEIGHTFOG_END_R)
		gi.WriteByte(fog.hf_end_r);
	if (fog.bits & svc_fog_data_t::BIT_HEIGHTFOG_END_G)
		gi.WriteByte(fog.hf_end_g);
	if (fog.bits & svc_fog_data_t::BIT_HEIGHTFOG_END_B)
		gi.WriteByte(fog.hf_end_b);
	if (fog.bits & svc_fog_data_t::BIT_HEIGHTFOG_END_DIST)
		gi.WriteLong(fog.hf_end_dist);

	gi.unicast(ent, true);

	ent->client->fog = ent->client->pers.wanted_fog;
	hf = wanted_hf;
}

// [Paril-KEX] ugly global to handle squad respawn origin
static bool use_squad_respawn = false;
static bool spawn_from_begin = false;
static vec3_t squad_respawn_position, squad_respawn_angles;

inline void PutClientOnSpawnPoint(edict_t* ent, const vec3_t& spawn_origin, const vec3_t& spawn_angles)
{
	gclient_t* client = ent->client;

	client->ps.pmove.origin = spawn_origin;

	ent->s.origin = spawn_origin;
	if (!use_squad_respawn)
		ent->s.origin[2] += 1; // make sure off ground
	ent->s.old_origin = ent->s.origin;

	// set the delta angle
	client->ps.pmove.delta_angles = spawn_angles - client->resp.cmd_angles;

	ent->s.angles = spawn_angles;
	ent->s.angles[PITCH] /= 3;

	client->ps.viewangles = ent->s.angles;
	client->v_angle = ent->s.angles;

	AngleVectors(client->v_angle, client->v_forward, nullptr, nullptr);
}

/*
===========
PutClientInServer

Called when a player connects to a server or respawns in
a deathmatch.
===========
*/
// ========================================================================
// PUT CLIENT IN SERVER HELPER FUNCTIONS
// ========================================================================

// Find and setup intermission spawn point for awaiting respawn
static void SetupRespawnLimboState(edict_t* ent)
{
	gclient_t* client = ent->client;
	
	// Find intermission point if not already cached
	if (!level.respawn_intermission)
	{
		edict_t* pt = G_FindByString<&edict_t::classname>(nullptr, "info_player_intermission");
		
		if (!pt)
		{
			// Fallback: try player start or deathmatch spawn
			pt = G_FindByString<&edict_t::classname>(nullptr, "info_player_start");
			if (!pt)
				pt = G_FindByString<&edict_t::classname>(nullptr, "info_player_deathmatch");
		}
		else
		{
			// Choose random intermission spot (1-4)
			int32_t random_spot = irandom(4);
			while (random_spot--)
			{
				edict_t* next = G_FindByString<&edict_t::classname>(pt, "info_player_intermission");
				if (!next)
					break;
				pt = next;
			}
		}

		// Cache the intermission location
		if (pt)
		{
			level.intermission_origin = pt->s.origin;
			level.intermission_angle = pt->s.angles;
		}
		else
		{
			level.intermission_origin = vec3_origin;
			level.intermission_angle = vec3_origin;
		}
		level.respawn_intermission = true;
	}

	// Place player at intermission point in frozen state
	ent->s.origin = level.intermission_origin;
	client->ps.pmove.origin = level.intermission_origin;
	client->ps.viewangles = level.intermission_angle;
	client->awaiting_respawn = true;
	client->ps.pmove.pm_type = PM_FREEZE;
	client->ps.rdflags = RDF_NONE;
	ent->deadflag = false;
	ent->solid = SOLID_NOT;
	ent->movetype = MOVETYPE_NOCLIP;
	ent->s.modelindex = 0;
	ent->svflags |= SVF_NOCLIENT;
	client->ps.team_id = client->resp.ctf_team;
	gi.linkentity(ent);
}

// Initialize entity defaults for spawning player
static void InitializeEntityDefaults(edict_t* ent, int index)
{
	ent->groundentity = nullptr;
	ent->client = &game.clients[index];
	ent->takedamage = true;
	ent->movetype = MOVETYPE_WALK;
	ent->viewheight = 22;
	ent->inuse = true;
	ent->classname = "player";
	ent->mass = 200;
	ent->solid = SOLID_BBOX;
	ent->deadflag = false;
	ent->air_finished = level.time + 12_sec;
	ent->clipmask = MASK_PLAYERSOLID;
	ent->model = "players/male/tris.md2";
	ent->die = player_die;
	ent->waterlevel = WATER_NONE;
	ent->watertype = CONTENTS_NONE;
	ent->flags &= ~(FL_NO_KNOCKBACK | FL_ALIVE_KNOCKBACK_ONLY | FL_NO_DAMAGE_EFFECTS | FL_SAM_RAIMI);
	ent->svflags &= ~SVF_DEADMONSTER;
	ent->svflags |= SVF_PLAYER;
	ent->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::UNKNOWN);
	ent->s.sound = 0;
	ent->client->weapon_sound = 0;
	ent->mins = PLAYER_MINS;
	ent->maxs = PLAYER_MAXS;
}

// Setup player state defaults
static void InitializePlayerState(edict_t* ent)
{
	gclient_t* client = ent->client;
	
	memset(&client->ps, 0, sizeof(client->ps));
	
	char val[MAX_INFO_VALUE];
	gi.Info_ValueForKey(client->pers.userinfo, "fov", val, sizeof(val));
	client->ps.fov = clamp(static_cast<float>(atoi(val)), 1.f, 160.f);
	client->ps.pmove.viewheight = ent->viewheight;
	client->ps.team_id = client->resp.ctf_team;

	if (!G_ShouldPlayersCollide(false))
		ent->clipmask &= ~CONTENTS_PLAYER;

	if (client->pers.weapon)
		client->ps.gunindex = gi.modelindex(client->pers.weapon->view_model);
	else
		client->ps.gunindex = 0;
	client->ps.gunskin = 0;

	ent->s.effects = EF_NONE;
	ent->s.modelindex = MODELINDEX_PLAYER;
	ent->s.modelindex2 = MODELINDEX_PLAYER;
	ent->s.frame = 0;
	
	P_AssignClientSkinnum(ent);
}

// Setup world fog for player
static void SetupPlayerFog(edict_t* ent)
{
	// Check if boss fog is active
	if (horde_fog_active)
	{
		// Apply boss fog settings
		constexpr float BOSS_FOG_DENSITY = 0.11f;
		constexpr float BOSS_FOG_RED = 0.05f;
		constexpr float BOSS_FOG_GREEN = 0.02f;
		constexpr float BOSS_FOG_BLUE = 0.08f;

		ent->client->pers.wanted_fog = {
			BOSS_FOG_DENSITY,
			BOSS_FOG_RED,
			BOSS_FOG_GREEN,
			BOSS_FOG_BLUE,
			0.0f // Darken sky
		};

		// Enable flashlight if not already on
		if (!(ent->flags & FL_FLASHLIGHT))
		{
			P_ToggleFlashlight(ent, true);
		}
	}
	else
	{
		// Apply world fog settings
		ent->client->pers.wanted_fog = {
			world->fog.density,
			world->fog.color[0],
			world->fog.color[1],
			world->fog.color[2],
			world->fog.sky_factor
		};
	}

	ent->client->pers.wanted_heightfog = {
		{ world->heightfog.start_color[0], world->heightfog.start_color[1], world->heightfog.start_color[2], world->heightfog.start_dist },
		{ world->heightfog.end_color[0], world->heightfog.end_color[1], world->heightfog.end_color[2], world->heightfog.end_dist },
		world->heightfog.falloff,
		world->heightfog.density
	};
	P_ForceFogTransition(ent, true);
}

void PutClientInServer(edict_t* ent)
{
	// Cache frequently used values
	const bool is_horde = g_horde && g_horde->integer;
	const bool is_deathmatch = deathmatch->integer != 0;
	const bool is_coop = G_IsCooperative();
	
	if (is_horde)
		VerifyAndAdjustBots();

	gclient_t* client = ent->client;
	const int index = ent - g_edicts - 1;
	vec3_t spawn_origin, spawn_angles;
	bool valid_spawn = false;
	bool is_landmark = false;

	// Handle velocity preservation for landmark spawns
	if (client->landmark_name)
		ent->velocity = client->oldvelocity;
	else
		ent->velocity = {};

	// Find spawn point
	const bool force_spawn = client->awaiting_respawn && level.time > client->respawn_timeout;

	if (use_squad_respawn)
	{
		spawn_origin = squad_respawn_position;
		spawn_angles = squad_respawn_angles;
		valid_spawn = true;
	}
	else if (gamerules->integer && DMGame.SelectSpawnPoint)
	{
		valid_spawn = DMGame.SelectSpawnPoint(ent, spawn_origin, spawn_angles, force_spawn);
	}
	else
	{
		valid_spawn = SelectSpawnPoint(ent, spawn_origin, spawn_angles, force_spawn, is_landmark);
	}

	// No valid spawn - put in limbo
	if (!valid_spawn)
	{
		if (!client->awaiting_respawn)
		{
			char userinfo[MAX_INFO_STRING];
			memcpy(userinfo, client->pers.userinfo, sizeof(userinfo));
			ClientUserinfoChanged(ent, userinfo);
			client->respawn_timeout = level.time + 3_sec;
		}

		SetupRespawnLimboState(ent);
		return;
	}

	// Update respawn state
	client->resp.ctf_state++;
	const bool was_waiting_for_respawn = client->awaiting_respawn;
	
	if (client->awaiting_respawn)
		ent->svflags &= ~SVF_NOCLIENT;
	
	client->awaiting_respawn = false;
	client->respawn_timeout = 0_ms;

	// Backup social ID
	char social_id[MAX_INFO_VALUE];
	Q_strlcpy(social_id, ent->client->pers.social_id, sizeof(social_id));

	// Handle client data reset based on game mode
	client_respawn_t resp = {};

	if (is_deathmatch)
	{
		client->resp.inactivity_time = 0_sec;
		client->resp.inactivity_warning = false;
		client->resp.inactive = false;
		client->pers.health = 0;
		resp = client->resp;
	}
	else
	{
		char userinfo[MAX_INFO_STRING];
		memcpy(userinfo, client->pers.userinfo, sizeof(userinfo));

		if (is_coop || (is_deathmatch && is_horde))
		{
			resp = client->resp;

			if (!P_UseCoopInstancedItems())
			{
				resp.coop_respawn.game_help1changed = client->pers.game_help1changed;
				resp.coop_respawn.game_help2changed = client->pers.game_help2changed;
				resp.coop_respawn.helpchanged = client->pers.helpchanged;
				// PvM: Don't overwrite pers with old coop_respawn (it has pre-vitality health values)
				// InitClientPersistant will handle health properly with vitality bonuses
				if (!IsPvMMode())
					client->pers = resp.coop_respawn;
			}
			else if (!client->pers.weapon)
			{
				client->pers.weapon = client->pers.lastweapon;
			}
		}

		ClientUserinfoChanged(ent, userinfo);

		if (is_coop && resp.score > client->pers.score)
			client->pers.score = resp.score;
		else if (!is_coop)
			memset(&resp, 0, sizeof(resp));
	}

	// Clear client data while preserving persistant info and camera settings
	client_persistant_t saved = client->pers;

	// DEBUG: Log pvm_level before and after save/restore
	// if (is_horde && level.intermissiontime)
	// {
	// 	gi.Com_PrintFmt("DEBUG PutClientInServer: Player {} before memset - pvm_level: {}, pvm_xp: {}\n",
	// 		ent - g_edicts - 1, client->pers.pvm_level, client->pers.pvm_xp);
	// }

	bool saved_auto_eyecam = client->auto_eyecam;
	bool saved_use_eyecam = client->use_eyecam;
	memset(client, 0, sizeof(*client));
	client->pers = saved;
	client->resp = resp;
	client->auto_eyecam = saved_auto_eyecam;
	client->use_eyecam = saved_use_eyecam;
	client->pers.sentry_gun_choice = client->resp.sentry_gun_choice;

	// DEBUG: Log pvm_level after restore
	// if (is_horde && level.intermissiontime)
	// {
	// 	gi.Com_PrintFmt("DEBUG PutClientInServer: Player {} after restore - pvm_level: {}, pvm_xp: {}, pers.health: {}\n",
	// 		ent - g_edicts - 1, client->pers.pvm_level, client->pers.pvm_xp, client->pers.health);
	// }

	// Initialize persistant data if needed
	if (client->pers.health <= 0)
	{
		// if (is_horde && level.intermissiontime)
		// {
		// 	gi.Com_PrintFmt("DEBUG PutClientInServer: Player {} calling InitClientPersistant (health <= 0)\n",
		// 		ent - g_edicts - 1);
		// }
		InitClientPersistant(ent, client);

		// if (is_horde && level.intermissiontime)
		// {
		// 	gi.Com_PrintFmt("DEBUG PutClientInServer: Player {} after InitClientPersistant - pvm_level: {}, pvm_xp: {}\n",
		// 		ent - g_edicts - 1, client->pers.pvm_level, client->pers.pvm_xp);
		// }
	}

	// Restore social ID
	Q_strlcpy(ent->client->pers.social_id, social_id, sizeof(social_id));
	ent->client->pers.connected = true;

	// Clear server flags
	globals.server_flags &= ~SERVER_FLAG_SLOW_TIME;

	// Copy client data to entity
	FetchClientEntData(ent);

	// Initialize entity defaults
	InitializeEntityDefaults(ent, index);
	
	// Initialize player state
	InitializePlayerState(ent);

	// Position player at spawn point
	PutClientOnSpawnPoint(ent, spawn_origin, spawn_angles);

	// Setup fog
	SetupPlayerFog(ent);

	// CTF initialization
	if (CTFStartClient(ent))
		return;

	// Spawn as spectator (check both pers and resp, but allow joining real teams)
	if ((client->pers.spectator || client->resp.spectator) &&
	    client->resp.ctf_team == CTF_NOTEAM)
	{
		client->chase_target = nullptr;
		client->resp.spectator = true;
		client->auto_eyecam = true;  // Initialize camera mode for spectators
		client->use_eyecam = false;
		ent->movetype = MOVETYPE_NOCLIP;
		ent->solid = SOLID_NOT;
		ent->svflags |= SVF_NOCLIENT;
		ent->client->ps.gunindex = 0;
		ent->client->ps.gunskin = 0;
		gi.linkentity(ent);
		return;
	}

	client->resp.spectator = false;

	// Handle landmark spawn collision detection
	if (spawn_from_begin && (is_coop || (is_deathmatch && is_horde)))
	{
		edict_t* collision = G_UnsafeSpawnPosition(ent->s.origin, true);
		if (collision)
		{
			gi.linkentity(ent);
			if (collision->client)
			{
				bool lm = false;
				SelectSpawnPoint(collision, spawn_origin, spawn_angles, true, lm);
				PutClientOnSpawnPoint(collision, spawn_origin, spawn_angles);
			}
		}
		ent->client->landmark_free_fall = true;
	}

	gi.linkentity(ent);
	KillBox(ent, true, MOD_TELEFRAG_SPAWN);

	// Level-specific hacks
	if (horde::MapOriginRegistry::GetMapID(level.mapname) == horde::MapID::RBOSS && !is_deathmatch)
		client->pers.inventory[IT_KEY_NUKE] = 1;

	// Equip weapon
	client->newweapon = client->pers.weapon;
	ChangeWeapon(ent);

	// Post-respawn effects
	if (was_waiting_for_respawn)
		G_PostRespawn(ent);
}

/*
=====================
ClientBeginDeathmatch

A client has just connected to the server in
deathmatch mode, so clear everything out before starting them.
=====================
*/
void ClientBeginDeathmatch(edict_t* ent)
{
	G_InitEdict(ent);

	// make sure we have a known default
	ent->svflags |= SVF_PLAYER;

	InitClientResp(ent->client);

	// ZOID
	if (G_TeamplayEnabled() && ent->client->resp.ctf_team < CTF_TEAM1)
		CTFAssignTeam(ent->client);
	// ZOID

	// PGM
	if (gamerules->integer && DMGame.ClientBegin)
	{
		DMGame.ClientBegin(ent);
	}
	// PGM

	// locate ent at a spawn point
	PutClientInServer(ent);

	if (level.intermissiontime)
	{
		MoveClientToIntermission(ent);
	}
	else
	{
		if (!(ent->svflags & SVF_NOCLIENT))
		{
			// send effect
			gi.WriteByte(svc_muzzleflash);
			gi.WriteEntity(ent);
			gi.WriteByte(MZ_LOGIN);
			gi.multicast(ent->s.origin, MULTICAST_PVS, false);
		}
	}

	gi.LocBroadcast_Print(PRINT_HIGH, "$g_entered_game", ent->client->pers.netname);

	// make sure all view stuff is valid
	ClientEndServerFrame(ent);
}

static void G_SetLevelEntry()
{
	if (deathmatch->integer)
		return;
	// map is a hub map, so we shouldn't bother tracking any of this.
	// the next map will pick up as the start.
	else if (level.hub_map)
		return;

	level_entry_t* found_entry = nullptr;
	int32_t highest_order = 0;

	for (size_t i = 0; i < MAX_LEVELS_PER_UNIT; i++)
	{
		level_entry_t* entry = &game.level_entries[i];

		highest_order = max(highest_order, entry->visit_order);

		if (!strcmp(entry->map_name, level.mapname) || !*entry->map_name)
		{
			found_entry = entry;
			break;
		}
	}

	if (!found_entry)
	{
		gi.Com_PrintFmt("PRINT: WARNING: more than {} maps in unit, can't track the rest\n", MAX_LEVELS_PER_UNIT);
		return;
	}

	level.entry = found_entry;
	Q_strlcpy(level.entry->map_name, level.mapname, sizeof(level.entry->map_name));

	// we're visiting this map for the first time, so
	// mark it in our order as being recent
	if (!*level.entry->pretty_name)
	{
		Q_strlcpy(level.entry->pretty_name, level.level_name, sizeof(level.entry->pretty_name));
		level.entry->visit_order = highest_order + 1;

		// give all of the clients an extra life back
		if (g_coop_enable_lives->integer)
			for (size_t i = 0; i < game.maxclients; i++)
				game.clients[i].pers.lives = min(g_coop_num_lives->integer + 1, game.clients[i].pers.lives + 1);
	}

	// scan for all new maps we can go to, for secret levels
	edict_t* changelevel = nullptr;
	while ((changelevel = G_FindByString<&edict_t::classname>(changelevel, "target_changelevel")))
	{
		if (!changelevel->map || !*changelevel->map)
			continue;

		// next unit map, don't count it
		if (strchr(changelevel->map, '*'))
			continue;

		const char* level = strchr(changelevel->map, '+');

		if (level)
			level++;
		else
			level = changelevel->map;

		// don't include end screen levels
		if (strstr(level, ".cin") || strstr(level, ".pcx"))
			continue;

		size_t level_length;

		const char* spawnpoint = strchr(level, '$');

		if (spawnpoint)
			level_length = spawnpoint - level;
		else
			level_length = strlen(level);

		// make an entry for this level that we may or may not visit
		level_entry_t* found_entry = nullptr;

		for (size_t i = 0; i < MAX_LEVELS_PER_UNIT; i++)
		{
			level_entry_t* entry = &game.level_entries[i];

			if (!*entry->map_name || !strncmp(entry->map_name, level, level_length))
			{
				found_entry = entry;
				break;
			}
		}

		if (!found_entry)
		{
			gi.Com_PrintFmt("PRINT: WARNING: more than {} maps in unit, can't track the rest\n", MAX_LEVELS_PER_UNIT);
			return;
		}

		Q_strlcpy(found_entry->map_name, level, min(level_length + 1, sizeof(found_entry->map_name)));
	}
}

/*
===========
ClientBegin

called when a client has finished connecting, and is ready
to be placed into the game.  This will happen every level load.
============
*/
void ClientBegin(edict_t* ent)
{
	ent->client = game.clients + (ent - g_edicts - 1);
	ent->client->awaiting_respawn = false;
	ent->client->respawn_timeout = 0_ms;

	// [Paril-KEX] we're always connected by this point...
	ent->client->pers.connected = true;

	// Clean up any invalid morph state from previous sessions
	if (IsMorphed(ent)) {
		RestoreMorphed(ent);
	}
	// Clear any stale flyer data
	ClearFlyerData(ent);

	// Horde mode: Begin staged asset loading for connections
	if (g_horde->integer) {
		// DISABLED: Test if monster exclusion alone prevents crashes
		// horde::AssetManager::Get().BeginClientLoading(ent);

		// Give client a grace period before spawning (longer for late waves)
		if (current_wave_level >= 20) {
			ent->client->respawn_timeout = level.time + 3000_ms;
			gi.LocClient_Print(ent, PRINT_HIGH,
				"Late-wave connection detected. Loading assets...");
		} else {
			ent->client->respawn_timeout = level.time + 1000_ms;
		}
	}

	if (deathmatch->integer)
	{
		ClientBeginDeathmatch(ent);
		return;
	}

	// [Paril-KEX] set enter time now, so we can send messages slightly
	// after somebody first joins
	ent->client->resp.entertime = level.time;
	ent->client->pers.spawned = true;

	// if there is already a body waiting for us (a loadgame), just
	// take it, otherwise spawn one from scratch
	if (ent->inuse)
	{
		// the client has cleared the client side viewangles upon
		// connecting to the server, which is different than the
		// state when the game is saved, so we need to compensate
		// with deltaangles
		ent->client->ps.pmove.delta_angles = ent->client->ps.viewangles;
	}
	else
	{
		// a spawn point will completely reinitialize the entity
		// except for the persistant data that was initialized at
		// ClientConnect() time
		G_InitEdict(ent);
		ent->classname = "player";
		InitClientResp(ent->client);
		spawn_from_begin = true;
		PutClientInServer(ent);
		spawn_from_begin = false;
	}

	// make sure we have a known default
	ent->svflags |= SVF_PLAYER;

	if (level.intermissiontime)
	{
		MoveClientToIntermission(ent);
	}
	else
	{
		// send effect if in a multiplayer game
		if (game.maxclients > 1 && !(ent->svflags & SVF_NOCLIENT))
			gi.LocBroadcast_Print(PRINT_HIGH, "$g_entered_game", ent->client->pers.netname);
	}

	level.coop_scale_players++;
	G_Monster_CheckCoopHealthScaling();

	// make sure all view stuff is valid
	ClientEndServerFrame(ent);

	// [Paril-KEX] send them goal, if needed
	G_PlayerNotifyGoal(ent);

	// [Paril-KEX] we're going to set this here just to be certain
	// that the level entry timer only starts when a player is actually
	// *in* the level
	G_SetLevelEntry();
}

/*
================
P_GetLobbyUserNum
================
*/
unsigned int P_GetLobbyUserNum(const edict_t* player) {
	unsigned int playerNum = 0;
	if (player > g_edicts && player < g_edicts + MAX_EDICTS) {
		playerNum = (player - g_edicts) - 1;
		if (playerNum >= MAX_CLIENTS) {
			playerNum = 0;
		}
	}
	return playerNum;
}

/*
================
G_EncodedPlayerName

Gets a token version of the players "name" to be decoded on the client.
================
*/
std::string G_EncodedPlayerName(edict_t* player)
{
	unsigned int const playernum = P_GetLobbyUserNum(player);
	return std::string("##P") + std::to_string(playernum);
}

/*
===========
ClientUserInfoChanged

called whenever the player updates a userinfo variable.
============
*/
void ClientUserinfoChanged(edict_t* ent, const char* userinfo)
{
	// set name
	if (!gi.Info_ValueForKey(userinfo, "name", ent->client->pers.netname, sizeof(ent->client->pers.netname)))
		Q_strlcpy(ent->client->pers.netname, "badinfo", sizeof(ent->client->pers.netname));

	// set spectator
	char val[MAX_INFO_VALUE] = { 0 };
	gi.Info_ValueForKey(userinfo, "spectator", val, sizeof(val));

	// spectators are only supported in deathmatch
	if (deathmatch->integer && !G_TeamplayEnabled() && *val && strcmp(val, "0"))
		ent->client->pers.spectator = true;
	else
		ent->client->pers.spectator = false;

	// set skin
	if (!gi.Info_ValueForKey(userinfo, "skin", val, sizeof(val)))
		Q_strlcpy(val, "male/grunt", sizeof(val));

	int const playernum = ent - g_edicts - 1;

	// combine name and skin into a configstring
	// ZOID
	if (G_TeamplayEnabled())
		CTFAssignSkin(ent, val);
	else
	{
		// set dogtag
		char dogtag[MAX_INFO_VALUE] = { 0 };
		gi.Info_ValueForKey(userinfo, "dogtag", dogtag, sizeof(dogtag));

		// ZOID
		gi.configstring(CS_PLAYERSKINS + playernum, G_Fmt("{}\\{}\\{}", ent->client->pers.netname, val, dogtag).data());
	}

	// ZOID
	//  set player name field (used in id_state view)
	gi.configstring(CONFIG_ID_PLAYER_NAME + playernum, ent->client->pers.netname);
	// ZOID

	// [Kex] netname is used for a couple of other things, so we update this after those.
	if ((ent->svflags & SVF_BOT) == 0) {
		Q_strlcpy(ent->client->pers.netname, G_EncodedPlayerName(ent).c_str(), sizeof(ent->client->pers.netname));
	}

	// fov
	gi.Info_ValueForKey(userinfo, "fov", val, sizeof(val));
	ent->client->ps.fov = clamp((float)atoi(val), 1.f, 160.f);

	// handedness
	if (gi.Info_ValueForKey(userinfo, "hand", val, sizeof(val)))
	{
		ent->client->pers.hand = static_cast<handedness_t>(clamp(atoi(val), (int32_t)RIGHT_HANDED, (int32_t)CENTER_HANDED));
	}
	else
	{
		ent->client->pers.hand = RIGHT_HANDED;
	}

	// [Paril-KEX] auto-switch
	if (gi.Info_ValueForKey(userinfo, "autoswitch", val, sizeof(val)))
	{
		ent->client->pers.autoswitch = static_cast<auto_switch_t>(clamp(atoi(val), (int32_t)auto_switch_t::SMART, (int32_t)auto_switch_t::NEVER));
	}
	else
	{
		ent->client->pers.autoswitch = auto_switch_t::SMART;
	}

	if (gi.Info_ValueForKey(userinfo, "autoshield", val, sizeof(val)))
	{
		ent->client->pers.autoshield = atoi(val);
	}
	else
	{
		ent->client->pers.autoshield = -1;
	}

	// [Paril-KEX] wants bob
	if (gi.Info_ValueForKey(userinfo, "bobskip", val, sizeof(val)))
	{
		ent->client->pers.bob_skip = val[0] == '1';
	}
	else
	{
		ent->client->pers.bob_skip = false;
	}

	// save off the userinfo in case we want to check something later
	Q_strlcpy(ent->client->pers.userinfo, userinfo, sizeof(ent->client->pers.userinfo));
}

inline bool IsSlotIgnored(edict_t* slot, edict_t** ignore, size_t num_ignore)
{
	for (size_t i = 0; i < num_ignore; i++)
		if (slot == ignore[i])
			return true;

	return false;
}

inline edict_t* ClientChooseSlot_Any(edict_t** ignore, size_t num_ignore)
{
	for (size_t i = 0; i < game.maxclients; i++)
		if (!IsSlotIgnored(globals.edicts + i + 1, ignore, num_ignore) && !game.clients[i].pers.connected)
			return globals.edicts + i + 1;

	return nullptr;
}

inline edict_t* ClientChooseSlot_Coop(const char* userinfo, const char* social_id, bool isBot, edict_t** ignore, size_t num_ignore)
{
	char name[MAX_INFO_VALUE] = { 0 };
	gi.Info_ValueForKey(userinfo, "name", name, sizeof(name));

	// the host should always occupy slot 0, some systems rely on this
	// (CHECK: is this true? is it just bots?)
	{
		size_t num_players = 0;

		for (size_t i = 0; i < game.maxclients; i++)
			if (IsSlotIgnored(globals.edicts + i + 1, ignore, num_ignore) || game.clients[i].pers.connected)
				num_players++;

		if (!num_players)
		{
			gi.Com_PrintFmt("PRINT: coop slot {} is host {}+{}\n", 1, name, social_id);
			return globals.edicts + 1;
		}
	}

	// grab matches from players that we have connected
	using match_type_t = int32_t;
	enum {
		MATCH_USERNAME,
		MATCH_SOCIAL,
		MATCH_BOTH,

		MATCH_TYPES
	};

	struct {
		edict_t* slot = nullptr;
		size_t total = 0;
	} matches[MATCH_TYPES];

	for (size_t i = 0; i < game.maxclients; i++)
	{
		if (IsSlotIgnored(globals.edicts + i + 1, ignore, num_ignore) || game.clients[i].pers.connected)
			continue;

		char check_name[MAX_INFO_VALUE] = { 0 };
		gi.Info_ValueForKey(game.clients[i].pers.userinfo, "name", check_name, sizeof(check_name));

		bool const username_match = game.clients[i].pers.userinfo[0] &&
			!strcmp(check_name, name);

		bool const social_match = social_id && game.clients[i].pers.social_id[0] &&
			!strcmp(game.clients[i].pers.social_id, social_id);

		match_type_t type = (match_type_t)0;

		if (username_match)
			type |= MATCH_USERNAME;
		if (social_match)
			type |= MATCH_SOCIAL;

		if (!type)
			continue;

		matches[type].slot = globals.edicts + i + 1;
		matches[type].total++;
	}

	// pick matches in descending order, only if the total matches
	// is 1 in the particular set; this will prefer to pick
	// social+username matches first, then social, then username last.
	for (int32_t i = 2; i >= 0; i--)
	{
		if (matches[i].total == 1)
		{
			gi.Com_PrintFmt("PRINT: coop slot {} restored for {}+{}\n", (ptrdiff_t)(matches[i].slot - globals.edicts), name, social_id);

			// spawn us a ghost now since we're gonna spawn eventually
			if (!matches[i].slot->inuse)
			{
				matches[i].slot->s.modelindex = MODELINDEX_PLAYER;
				matches[i].slot->solid = SOLID_BBOX;

				G_InitEdict(matches[i].slot);
				matches[i].slot->classname = "player";
				InitClientResp(matches[i].slot->client);
				spawn_from_begin = true;
				PutClientInServer(matches[i].slot);
				spawn_from_begin = false;

				// make sure we have a known default
				matches[i].slot->svflags |= SVF_PLAYER;

				matches[i].slot->sv.init = false;
				matches[i].slot->classname = "player";
				matches[i].slot->client->pers.connected = true;
				matches[i].slot->client->pers.spawned = true;
				P_AssignClientSkinnum(matches[i].slot);
				gi.linkentity(matches[i].slot);
			}

			return matches[i].slot;
		}
	}

	// in the case where we can't find a match, we're probably a new
	// player, so pick a slot that hasn't been occupied yet
	for (size_t i = 0; i < game.maxclients; i++)
		if (!IsSlotIgnored(globals.edicts + i + 1, ignore, num_ignore) && !game.clients[i].pers.userinfo[0])
		{
			gi.Com_PrintFmt("coop slot {} issuing new for {}+{}\n", i + 1, name, social_id);
			return globals.edicts + i + 1;
		}

	// all slots have some player data in them, we're forced to replace one.
	edict_t* any_slot = ClientChooseSlot_Any(ignore, num_ignore);

	gi.Com_PrintFmt("coop slot {} any slot for {}+{}\n", !any_slot ? -1 : (ptrdiff_t)(any_slot - globals.edicts), name, social_id);

	return any_slot;
}

// [Paril-KEX] for coop, we want to try to ensure that players will always get their
// proper slot back when they connect.
edict_t* ClientChooseSlot(const char* userinfo, const char* social_id, bool isBot, edict_t** ignore, size_t num_ignore, bool cinematic)
{
	// coop and non-bots is the only thing that we need to do special behavior on
	if ((!cinematic && G_IsCooperative() && !isBot)/* || (!cinematic && g_horde->integer && !isBot)*/)
		return ClientChooseSlot_Coop(userinfo, social_id, isBot, ignore, num_ignore);

	// just find any free slot
	return ClientChooseSlot_Any(ignore, num_ignore);
}

/*
===========
ClientConnect

Called when a player begins connecting to the server.
The game can refuse entrance to a client by returning false.
If the client is allowed, the connection process will continue
and eventually get to ClientBegin()
Changing levels will NOT cause this to be called again, but
loadgames will.
============
*/
bool ClientConnect(edict_t* ent, char* userinfo, const char* social_id, bool isBot)
{
	// check to see if they are on the banned IP list
#if 0
	value = Info_ValueForKey(userinfo, "ip");
	if (SV_FilterPacket(value))
	{
		Info_SetValueForKey(userinfo, "rejmsg", "Banned.");
		return false;
	}
#endif

	// check for a spectator
	char value[MAX_INFO_VALUE] = { 0 };
	gi.Info_ValueForKey(userinfo, "spectator", value, sizeof(value));

	if (G_IsDeathmatch() && *value && strcmp(value, "0"))
	{
		uint32_t i, numspec;

		if (ValidatePassword(spectator_password->string, value))
		{
			gi.Info_SetValueForKey(userinfo, "rejmsg", "Spectator password required or incorrect.");
			return false;
		}

		// count spectators
		for (i = numspec = 0; i < game.maxclients; i++)
			if (g_edicts[i + 1].inuse && g_edicts[i + 1].client->pers.spectator)
				numspec++;

		if (numspec >= (uint32_t)maxspectators->integer)
		{
			gi.Info_SetValueForKey(userinfo, "rejmsg", "Server spectator limit is full.");
			return false;
		}
	}
	else
	{
		// check for a password ( if not a bot! )
		gi.Info_ValueForKey(userinfo, "password", value, sizeof(value));
		if (!isBot && ValidatePassword(password->string, value))
		{
			gi.Info_SetValueForKey(userinfo, "rejmsg", "Password required or incorrect.");
			return false;
		}
	}

	// they can connect
	ent->client = game.clients + (ent - g_edicts - 1);

	// set up userinfo early
	ClientUserinfoChanged(ent, userinfo);

	// if there is already a body waiting for us (a loadgame), just
	// take it, otherwise spawn one from scratch
	if (ent->inuse == false)
	{
		// clear the respawning variables
		// ZOID -- force team join
		ent->client->resp.ctf_team = CTF_NOTEAM;
		ent->client->pers.id_state = true; // here we set ID or IDDMG enabled or not by default
		ent->client->pers.iddmg_state = g_horde->integer ? true : false;
		// ZOID
		InitClientResp(ent->client);
		if (!game.autosaved || !ent->client->pers.weapon)
			InitClientPersistant(ent, ent->client);
	}
	// make sure we start with known default(s)
	ent->svflags = SVF_PLAYER;
	if (isBot) {
		ent->svflags |= SVF_BOT;
	}

	Q_strlcpy(ent->client->pers.social_id, social_id, sizeof(ent->client->pers.social_id));

	if (game.maxclients > 1)
	{
		// [Paril-KEX] fetch name because now netname is kinda unsuitable
		gi.Info_ValueForKey(userinfo, "name", value, sizeof(value));
		gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_player_connected", value);
	}

	ent->client->pers.connected = true;

	// [Paril-KEX] force a state update
	ent->sv.init = false;
	return true;
}

/*
===========
ClientDisconnect

Called when a player drops from the server.
Will not be called between levels.
============
*/
void ClientDisconnect(edict_t* ent)
{
	if (!ent || !ent->client) // Added null check for ent
		return;

	if (g_horde && g_horde->integer)
	VerifyAndAdjustBots();

	// Clean up flyer morph state if morphed
	if (IsMorphed(ent)) {
		RestoreMorphed(ent);
	}
	// Clear flyer data from static map to prevent stale pointer issues
	ClearFlyerData(ent);

	// --- Existing Cleanup Logic ---
	//CleanupPlayerLaserManager(ent);
	RemovePlayerOwnedEntities(ent); // This handles the laser edicts via laser_die

	// ZOID
	CTFDeadDropFlag(ent);
	CTFDeadDropTech(ent);
	// ZOID

	PlayerTrail_Destroy(ent);

	//============
	// ROGUE
	// make sure no trackers are still hurting us.
	if (ent->client->tracker_pain_time > 0_ms) // Check against 0_ms
		RemoveAttackingPainDaemons(ent);

	if (ent->client->owned_sphere)
	{
		if (ent->client->owned_sphere->inuse)
			G_FreeEdict(ent->client->owned_sphere);
		ent->client->owned_sphere = nullptr;
	}

	if (gamerules && gamerules->integer) // Added null check for gamerules cvar
	{
		if (DMGame.PlayerDisconnect)
			DMGame.PlayerDisconnect(ent);
	}
	// ROGUE
	//============

	// --- Remaining Disconnect Logic ---
	// send effect
	if (!(ent->svflags & (SVF_NOCLIENT | SVF_BOT)))
	{
		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(MZ_LOGOUT);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);
	}

	gi.unlinkentity(ent);
	ent->s.modelindex = 0;
	ent->solid = SOLID_NOT;
	ent->inuse = false;
	ent->sv.init = false; // Mark for re-initialization if reused
	ent->classname = "disconnected";
	// Clear potentially sensitive parts of the client struct AFTER deleting managed objects
	if (ent->client) { // Double check client pointer exists before accessing members
		ent->client->pers.connected = false;
		ent->client->pers.spawned = false;
		// Other client fields are usually zeroed out by the engine or game logic
		// when the slot is reused, but explicitly nulling pointers is safest.
		ent->client->chase_target = nullptr;
		ent->client->menu = nullptr;
		ent->client->ctf_grapple = nullptr;
		// Note: owned_sphere was already nulled above.
	}
	ent->timestamp = level.time + 1_sec; // Used elsewhere? Seems short for a disconnect timestamp.


	// update active scoreboards
	if (deathmatch && deathmatch->integer) { // Added null check
		for (auto player : active_players()) {
			// Added null check for player and player->client
			if (player && player->client && player->client->showscores) {
				player->client->menutime = level.time;
			}
		}
	}
}

/*
=================
ClientIsSpectating
=================
*/
bool ClientIsSpectating(const gclient_t* cl) noexcept {
	if (!cl) return false;

	return cl->resp.ctf_team == CTF_NOTEAM;
}

/*
=================
EntIsSpectating
=================
*/
bool EntIsSpectating(const edict_t* ent) noexcept
{
	if (!ent || !ent->client)
		return true;



	return (G_TeamplayEnabled() && ent->client->resp.ctf_team == CTF_NOTEAM) ||
		ent->client->resp.spectator ||
		ent->client->pers.spectator ||
		ent->movetype == MOVETYPE_NOCLIP;
}

//==============================================================

trace_t SV_PM_Clip(const vec3_t& start, const vec3_t* mins, const vec3_t* maxs, const vec3_t& end, contents_t mask)
{
	return gi.game_import_t::clip(world, start, mins, maxs, end, mask);
}

bool G_ShouldPlayersCollide(bool weaponry)
{
	if (g_disable_player_collision->integer)
		return false; // only for debugging.

	// always collide on dm
	if (!coop->integer || !g_horde->integer)
		return true;

	// weaponry collides if friendly fire is enabled
	if (weaponry && g_friendly_fire->integer)
		return true;

	// check collision cvar
	return g_coop_player_collision->integer;
}

/*
=================
P_FallingDamage

Paril-KEX: this is moved here and now reacts directly
to ClientThink rather than being delayed.
=================
*/
void P_FallingDamage(edict_t* ent, const pmove_t& pm)
{
	int	   damage;
	vec3_t dir;

	// dead stuff can't crater
	if (ent->health <= 0 || ent->deadflag)
		return;

	if (ent->s.modelindex != MODELINDEX_PLAYER)
		return; // not in the player model

	if (ent->movetype == MOVETYPE_NOCLIP)
		return;

	// never take falling damage if completely underwater
	if (pm.waterlevel == WATER_UNDER)
		return;

	// ZOID
	//  never take damage if just release grapple or on grapple
	if (ent->client->ctf_grapplereleasetime >= level.time ||
		(ent->client->ctf_grapple &&
			ent->client->ctf_grapplestate > CTF_GRAPPLE_STATE_FLY))
		return;

	// Ignore falling damage if the player has the hook out or if the hook release time is within the last 3 seconds
	if (ent->client && (ent->client->hook_out || ent->client->hook_release_time + 1.0 >= level.time.seconds())) {
		return;
	}

	// ZOID

	float delta = pm.impact_delta;

	if (pm_config.physics_flags & PHYSICS_PSX_SCALE)
		delta = delta * delta * 0.000078f;
	else
		delta = delta * delta * 0.0001f;


	if (pm.waterlevel == WATER_WAIST)
		delta *= 0.25f;
	if (pm.waterlevel == WATER_FEET)
		delta *= 0.5f;

	if (delta < 1)
		return;

	// restart footstep timer
	ent->client->bobtime = 0;

	if (ent->client->landmark_free_fall)
	{
		delta = min(30.f, delta);
		ent->client->landmark_free_fall = false;
		ent->client->landmark_noise_time = level.time + 100_ms;
	}

	if (delta < 15)
	{
		if (!(pm.s.pm_flags & PMF_ON_LADDER))
			ent->s.event = EV_FOOTSTEP;
		return;
	}

	ent->client->fall_value = delta * 0.5f;
	if (ent->client->fall_value > 40)
		ent->client->fall_value = 40;
	ent->client->fall_time = level.time + FALL_TIME();

	if (delta > 30)
	{
		if (delta >= 55)
			ent->s.event = EV_FALLFAR;
		else
			ent->s.event = EV_FALL;

		ent->pain_debounce_time = level.time + FRAME_TIME_S; // no normal pain sound
		damage = std::max(static_cast<int>((delta - 30) / 2), 1);

		if (pm_config.physics_flags & PHYSICS_PSX_SCALE)
			damage = std::min(4, damage);

		dir = { 0, 0, 1 };

		if (!deathmatch->integer || !g_dm_no_fall_damage->integer)
			T_Damage(ent, world, world, dir, ent->s.origin, vec3_origin, damage, 0, DAMAGE_NONE, MOD_FALLING);
	}
	ent->s.event = EV_FALLSHORT;

	// Paril: falling damage noises alert monsters
	if (ent->health)
		PlayerNoise(ent, pm.s.origin, PNOISE_SELF);
}

// Constantes
constexpr gtime_t BOT_INACTIVITY_DURATION = 20_sec;  // Duración específica para bots
constexpr gtime_t MIN_INACTIVITY_DURATION = 15_sec;
constexpr gtime_t DEFAULT_INACTIVITY_DURATION = 45_sec;
constexpr gtime_t WARNING_TIME = 5_sec;
constexpr gtime_t MENU_PROTECTED_INACTIVITY_DURATION = 90_sec;  // Extended time when in menus

// Enumeración para estados de actividad
enum class ActivityState {
	Active,
	Warning,
	Inactive
};

// Funciones auxiliares
static void WarnInactivePlayer(edict_t* ent) {
	gi.LocClient_Print(ent, PRINT_CENTER, "5 seconds to go AFK!\n");
	gi.local_sound(ent, CHAN_AUTO, gi.soundindex("world/brick_wall_break1.wav"), 1, ATTN_NONE, 0);
}

static void HandleInactivePlayer(edict_t* ent) {
	// Check if already inactive or no client
	if (!ent || !ent->client || ent->client->resp.inactive) {
		return;
	}

	gi.LocClient_Print(ent, PRINT_CENTER, "\n\n\n\n\nYou have deserted the war against stroggs!\n UNACCEPTABLE\n");

	// Force player into spectator mode
	// Ensure CTFObserver correctly sets spectator flags (e.g., resp.spectator = true)
	// and potentially changes movetype/solid state.
	CTFObserver(ent);

	ent->client->resp.inactive = true; // Mark as inactive


	// Additional cleanup that might happen when going spectator:
	ent->client->ps.gunindex = 0; // Hide weapon model
	ent->client->ps.gunskin = 0;
	// Ensure movetype/solid are appropriate for spectator
	ent->movetype = MOVETYPE_NOCLIP;
	ent->solid = SOLID_NOT;
	ent->svflags |= SVF_NOCLIENT; // Might already be set by CTFObserver
	gi.linkentity(ent); // Relink after changes
}


static bool IsPlayerActive(const edict_t* ent) {
	return (ent->client->latched_buttons & BUTTON_ANY) ||
		ent->client->old_origin != ent->s.origin ||
		ent->client->old_angles != ent->client->v_angle;
}

static bool IsBotStuckAtOrigin(const edict_t* ent) {
	return (ent->svflags & SVF_BOT) &&
		ent->client->old_origin == ent->s.origin;
}

static bool BotIsOnLava(const edict_t* ent)
{

	if (ent->flags & FL_INWATER && ((ent->watertype & CONTENTS_SLIME || ent->watertype & CONTENTS_LAVA) || (ent->air_finished == level.time))) {
		return true;
	}

	return false;
}

static bool ClientInactivityTimer(edict_t* ent) {
	// Verificación de precondiciones
	if (!ent || !ent->client) {
		gi.Com_PrintFmt("PRINT: Error: Invalid entity or client in ClientInactivityTimer\n");
		return false;
	}

	// Casos especiales donde no se aplica la inactividad
	if (level.intermissiontime ||
		ClientIsSpectating(ent->client) ||
		ent->client->resp.ctf_team == CTF_NOTEAM) {
		return true;
	}

	// Manejo especial para bots
	if (ent->svflags & SVF_BOT) {
		// Inicialización del temporizador de inactividad para bots
		if (!ent->client->resp.inactivity_time) {
			ent->client->resp.inactivity_time = level.time + BOT_INACTIVITY_DURATION;
			ent->client->old_origin = ent->s.origin;
			return true;
		}

		if (BotIsOnLava(ent)) {
		ent->client->resp.teleport_cooldown = level.time;
		ent->client->emergency_teleport = true;
		TeleportSelf(ent);
	}

		// Si el bot está atascado en su origen
		if (IsBotStuckAtOrigin(ent)) {
			if (level.time > ent->client->resp.inactivity_time) {
				// Teletransportar al bot
				TeleportSelf(ent);
				// Reiniciar el temporizador
				ent->client->resp.inactivity_time = level.time + BOT_INACTIVITY_DURATION;
				return true;
			}
		}
		else {
			// Si el bot se mueve, reiniciar el temporizador
			ent->client->resp.inactivity_time = level.time + BOT_INACTIVITY_DURATION;
		}

		// Actualizar la posición antigua del bot
		ent->client->old_origin = ent->s.origin;
		return true;
	}

	// Código original para jugadores humanos
	const gtime_t inactivity_duration = std::max(DEFAULT_INACTIVITY_DURATION, MIN_INACTIVITY_DURATION);

	// Check if player is menu protected - give them extra time (once)
	if (ent->client->menu_protected && !ent->client->resp.inactivity_time) {
		ent->client->resp.inactivity_time = level.time + MENU_PROTECTED_INACTIVITY_DURATION;
		ent->client->resp.inactivity_warning = false;
		return true;
	}

	if (!ent->client->resp.inactivity_time) {
		ent->client->resp.inactivity_time = level.time + inactivity_duration;
		ent->client->resp.inactivity_warning = false;
		ent->client->resp.inactive = false;
		return true;
	}

	if (IsPlayerActive(ent)) {
		ent->client->resp.inactivity_time = level.time + inactivity_duration;
		ent->client->resp.inactivity_warning = false;
		ent->client->resp.inactive = false;
	}
	else {
		const gtime_t current_time = level.time;
		if (current_time > ent->client->resp.inactivity_time) {
			HandleInactivePlayer(ent);
			return false;
		}
		if (current_time > ent->client->resp.inactivity_time - WARNING_TIME && !ent->client->resp.inactivity_warning) {
			ent->client->resp.inactivity_warning = true;
			WarnInactivePlayer(ent);
		}
	}

	if (ent->client->resp.inactivity_warning && IsPlayerActive(ent)) {
		ent->client->resp.inactivity_warning = false;
	}

	ent->client->old_origin = ent->s.origin;
	ent->client->old_angles = ent->client->v_angle;
	return true;
}

void CheckClientsInactivity() {
	if (!G_TeamplayEnabled() || g_teamplay_force_join->integer)
		return;

	auto should_check_inactivity = [](edict_t* player) {
		return player->inuse && player->client &&
			player->client->resp.ctf_team != CTF_NOTEAM &&
			!ClientIsSpectating(player->client) &&
			!(player->svflags & SVF_BOT);
		};

	for (auto player : active_players()) {
		if (should_check_inactivity(player)) {
			ClientInactivityTimer(player);
		}
	}
}

void UpdateClientHealth(edict_t* ent, gclient_t* client)
{
	if ((!g_horde->integer && !pvm->integer) || client->resp.spectator || !client->pers.spawned ||
		ent->health <= 0 || ent->deadflag)
		return;

	// PvM mode: Skip wave-based health updates, vitality handles max health
	if (IsPvMMode())
	{
		// client->resp.max_health = ent->max_health;
		// client->pers.max_health = ent->max_health;
		// ent->health = ent->max_health;

		return;
	}

	const int new_max_health = CalculateWaveBasedMaxHealth(100, client);

	if (new_max_health != client->pers.max_health)
	{
		// FIX: Replaced C-style casts with static_cast for type safety and clarity.
		const float health_ratio = static_cast<float>(ent->health) / static_cast<float>(client->pers.max_health);
		client->pers.max_health = new_max_health;
		client->resp.max_health = new_max_health;
		ent->max_health = new_max_health;
		// FIX: Replaced C-style cast with static_cast.
		int new_health = static_cast<int>(health_ratio * new_max_health);
		new_health = min(new_health, new_max_health);
		ent->health = new_health;
		gi.linkentity(ent);
	}
}

void UpdateIRTracking(edict_t* ent, gclient_t* client)
{
	if (!developer->integer)
		return;
	// Si no está activo el IR o el cliente no es válido, salir
	if (!client || !client->ir_tracking_active || client->ir_time <= level.time)
	{
		if (client)
			client->ir_tracking_active = false;
		return;
	}

	// Solo buscar monstruos cada X frames para mejorar rendimiento
	client->ir_frame_count = (client->ir_frame_count + 1) % 3;
	if (client->ir_frame_count != 0)
		return;

	static uint32_t ir_dupe_key = 0;  // Clave única para el IR

	// Usar el iterador de monstruos activos
	for (const auto* const monster : active_monsters())
	{
		// Solo dibujar si es un monstruo "contable"
		if (!(monster->monsterinfo.aiflags & AI_DO_NOT_COUNT))
		{
			gi.unicast(ent, false, ir_dupe_key);  // Primero indicamos que el mensaje es solo para este cliente
			gi.Draw_Bounds(monster->absmin, monster->absmax, rgba_red, 0.1f, false);
		}
	}

	ir_dupe_key++;  // Increment key for next frame
}

// Handle menu movement input
bool HandleMenuMovement(edict_t* ent, usercmd_t* menu_ucmd)
{
	// Handle inventory display separately - inventory takes priority over menu
	if (ent->client->showinventory)
	{
		// Handle inventory navigation with forwardmove
		const int32_t inv_sign = menu_ucmd->forwardmove > 0 ? 1 : menu_ucmd->forwardmove < 0 ? -1 : 0;
		if (ent->client->menu_sign != inv_sign)
		{
			ent->client->menu_sign = inv_sign;
			if (inv_sign > 0)
			{
				SelectNextItem(ent, IF_ANY, false); // false = don't handle menu
				return true;
			}
			else if (inv_sign < 0)
			{
				SelectPrevItem(ent, IF_ANY);
				return true;
			}
		}

		// BUTTON_ATTACK selects inventory item (like BUTTON_USE)
		if ((menu_ucmd->buttons & BUTTON_ATTACK) && !ent->client->inmenu)
		{
			// Call inventory use function
			Cmd_InvUse_f(ent);
			ent->client->inmenu = true;
			menu_ucmd->buttons &= ~BUTTON_ATTACK;
			return true;
		}

		// BUTTON_JUMP closes inventory
		if ((menu_ucmd->buttons & BUTTON_JUMP) && !(ent->client->ps.pmove.pm_flags & PMF_JUMP_HELD))
		{
			ent->client->ps.pmove.pm_flags |= PMF_JUMP_HELD;
			ent->client->showinventory = false;
			menu_ucmd->buttons &= ~BUTTON_JUMP;
			return true;
		}

		if (!(menu_ucmd->buttons & BUTTON_ATTACK))
		{
			ent->client->inmenu = false;
		}

		if (!(menu_ucmd->buttons & BUTTON_JUMP))
		{
			ent->client->ps.pmove.pm_flags &= ~PMF_JUMP_HELD;
		}
		return true; // Return true to indicate inventory handled input - don't process menu
	}

	if (!ent->client->menu || ent->svflags & SVF_BOT)
		return false;

	const int32_t menu_sign = menu_ucmd->forwardmove > 0 ? 1 : menu_ucmd->forwardmove < 0 ? -1 : 0;
	if (ent->client->menu_sign != menu_sign)
	{
		ent->client->menu_sign = menu_sign;
		if (menu_sign > 0)
		{
			PMenu_Prev(ent);
			return true;
		}
		else if (menu_sign < 0)
		{
			PMenu_Next(ent);
			return true;
		}
	}

	// BUTTON_ATTACK selects menu item
	if ((menu_ucmd->buttons & BUTTON_ATTACK) && !ent->client->inmenu)
	{
		PMenu_Select(ent);
		ent->client->inmenu = true;
		menu_ucmd->buttons &= ~BUTTON_ATTACK;
		return true;
	}

	// BUTTON_JUMP for back navigation or menu exit
	if ((menu_ucmd->buttons & BUTTON_JUMP) && !(ent->client->ps.pmove.pm_flags & PMF_JUMP_HELD))
	{
		ent->client->ps.pmove.pm_flags |= PMF_JUMP_HELD;

		// Check if there's a "Back" option in current menu
		bool found_back = false;
		if (ent->client->menu)
		{
			pmenuhnd_t* hnd = ent->client->menu;
			for (int i = 0; i < hnd->num; i++)
			{
				if (hnd->entries[i].text[0] &&
				    (strstr(hnd->entries[i].text, "Back") ||
				     strstr(hnd->entries[i].text, "Close")))
				{
					// Select the back/close option
					hnd->cur = i;
					PMenu_Select(ent);
					found_back = true;
					break;
				}
			}
		}

		// If no back option found, close the menu
		if (!found_back)
		{
			PMenu_Close(ent);
		}

		menu_ucmd->buttons &= ~BUTTON_JUMP;
		return true;
	}

	if (!(menu_ucmd->buttons & BUTTON_ATTACK))
	{
		ent->client->inmenu = false;
	}

	if (!(menu_ucmd->buttons & BUTTON_JUMP))
	{
		ent->client->ps.pmove.pm_flags &= ~PMF_JUMP_HELD;
	}
	return false;
}

void ClientThink(edict_t* ent, usercmd_t* ucmd)
{
	gclient_t* client;
	edict_t* other;
	uint32_t i;
	pmove_t pm;

	client = ent->client;

	level.current_entity = ent;

	// Store if player wants to throw barrel BEFORE we modify buttons
	bool barrel_throw_requested = false;
	if (client->resp.held_barrel && client->resp.held_barrel->inuse && (ucmd->buttons & BUTTON_ATTACK))
	{
		barrel_throw_requested = true;
		// Clear the button immediately so it doesn't get latched
		ucmd->buttons &= ~BUTTON_ATTACK;
	}
	// Block attack button if holding barrel (but not throwing)
	else if (client->resp.held_barrel && client->resp.held_barrel->inuse)
	{
		ucmd->buttons &= ~BUTTON_ATTACK;
	}

	// [Paril-KEX] pass buttons through even if we are in intermission or chasing.
	client->oldbuttons = client->buttons;
	client->buttons = ucmd->buttons;
	client->latched_buttons |= client->buttons & ~client->oldbuttons;
	client->cmd = *ucmd;

	// check for inactivity timer
	if (!ClientInactivityTimer(ent))
		return;

	if (client->hook_on && ent->client->hook)
		Hook_Service(client->hook);

	// Update held barrel position (gravity gun style)
	if (client->resp.held_barrel && client->resp.held_barrel->inuse)
	{
		extern void barrel_visualize(edict_t* player);
		barrel_visualize(ent);

		// Throw barrel if attack was pressed (using flag set earlier)
		if (barrel_throw_requested)
		{
			extern cvar_t* barrel_throw_speed;
			edict_t* barrel = client->resp.held_barrel;

			// Calculate throw direction from player's view
			vec3_t forward;
			AngleVectors(client->v_angle, forward, nullptr, nullptr);

			// Clear any existing velocity and apply throw velocity
			barrel->velocity = forward * barrel_throw_speed->value;

			// Release the barrel
			barrel->s.alpha = 1.0f; // Reset to fully opaque
			barrel->solid = SOLID_BBOX;
			barrel->movetype = MOVETYPE_TOSS;
			gi.linkentity(barrel);

			client->resp.held_barrel = nullptr;

			//gi.LocClient_Print(ent, PRINT_HIGH, "Barrel thrown!\n");
			// Continue with normal processing after throwing
		}
		else
		{
			// Holding barrel - block weapons like menu_protected does
			// Clear weapon fire state
			client->weapon_fire_buffered = false;
			client->latched_buttons &= ~BUTTON_ATTACK;
			if (client->weaponstate != WEAPON_READY)
				client->weaponstate = WEAPON_READY;

			// Don't process weapon firing while holding barrel
			// Continue with movement but skip weapon processing
		}
	}

	// Handle menu protection - block attack/jump but allow menu navigation
	if (client->menu_protected && (client->menu || client->showinventory))
	{
		// Let menu handle the input
		if (HandleMenuMovement(ent, ucmd))
		{
			// Menu handled it, clear combat buttons to prevent game actions
			ucmd->buttons &= ~(BUTTON_ATTACK | BUTTON_JUMP);
		}
		// Still block the buttons even if menu didn't handle them
		else
		{
			ucmd->buttons &= ~(BUTTON_ATTACK | BUTTON_JUMP);
		}

		// Wait for player to land before freezing to prevent awkward mid-air freeze
		bool should_freeze = ent->groundentity != nullptr;

		if (should_freeze)
		{
			// Freeze player to prevent looking stuck in air or running in place
			client->ps.pmove.pm_type = PM_FREEZE;
			ent->velocity = vec3_origin; // Stop all movement
			gi.linkentity(ent); // Update entity state

			// Don't process normal movement/combat while in menu or inventory
			return;
		}
		// If not settled yet, block buttons but allow movement to continue until landing
		// (fall through to normal pmove processing)
	}

	// Check for intermission or awaiting respawn
	if (level.intermissiontime || ent->client->awaiting_respawn)
	{
		client->ps.pmove.pm_type = PM_FREEZE;

		bool n64_sp = false;

		if (level.intermissiontime)
		{
			n64_sp = !G_IsDeathmatch() && level.is_n64;

			// can exit intermission after five seconds
			// with any key, but not if in the n64 single player mode
			if (level.changemap && (!n64_sp || level.level_intermission_set) && level.time > level.intermissiontime + 5_sec && (ucmd->buttons & BUTTON_ANY))
				level.exitintermission = true;
		}

		if (!n64_sp)
			client->ps.pmove.viewheight = ent->viewheight = 22;
		else
			client->ps.pmove.viewheight = ent->viewheight = 0;
		ent->movetype = MOVETYPE_NOCLIP;

		// Close any open menu during intermission or respawn wait
		if (ent->client->menu) {
			PMenu_Close(ent);
		}
		return;
	}

	if (ent->client->chase_target)
	{
		client->resp.cmd_angles = ucmd->angles;
		ent->movetype = MOVETYPE_NOCLIP;
	}
	else
	{
		// Handle menu movement if the menu is open
		if (ent->client->menu)
		{
			// Just pass the original ucmd directly
			if (HandleMenuMovement(ent, ucmd))
			{
				return;
			}
		}

		// NO EARLY RETURN FOR MORPHED PLAYERS - they need pmove!

		memset(&pm, 0, sizeof(pm));

		// Check if the player is morphed and set appropriate physics
		if (IsMorphed(ent))
		{
			auto* data = GetMorphData(ent);
			if (data && data->morph_type == MORPH_FLYER) {
				// Flyer uses flying physics
				client->ps.pmove.pm_type = PM_SPECTATOR; // Use free-flight physics
				client->ps.pmove.gravity = 0; // No gravity
				client->ps.pmove.pm_flags &= ~PMF_NO_GROUND_SEEK;
			} else {
				// Brain and other morphs use normal ground movement
				client->ps.pmove.pm_type = PM_NORMAL;
				// Use level.gravity so sv_gravity cvar is respected
				client->ps.pmove.gravity = (short)(level.gravity * ent->gravity);
			}
		}
		else // This is the original logic for a normal player
		{
			if (ent->movetype == MOVETYPE_NOCLIP)
			{
				if (ent->client->awaiting_respawn)
					client->ps.pmove.pm_type = PM_FREEZE;
				else if (ent->client->resp.spectator || (G_TeamplayEnabled() && ent->client->resp.ctf_team == CTF_NOTEAM))
					client->ps.pmove.pm_type = PM_SPECTATOR;
				else
					client->ps.pmove.pm_type = PM_NOCLIP;
			}
			else if (ent->s.modelindex != MODELINDEX_PLAYER)
				client->ps.pmove.pm_type = PM_GIB;
			else if (ent->deadflag)
				client->ps.pmove.pm_type = PM_DEAD;
			else if (ent->client->ctf_grapplestate >= CTF_GRAPPLE_STATE_PULL)
				client->ps.pmove.pm_type = PM_GRAPPLE;
			else
				client->ps.pmove.pm_type = PM_NORMAL;

			// [Paril-KEX] trigger_gravity support
			if (ent->no_gravity_time > level.time)
			{
				client->ps.pmove.gravity = 0;
				client->ps.pmove.pm_flags |= PMF_NO_GROUND_SEEK;
			}
			else
			{
				client->ps.pmove.gravity = (short)(level.gravity * ent->gravity);
				client->ps.pmove.pm_flags &= ~PMF_NO_GROUND_SEEK;
			}
		}

		// CRITICAL FIX: Add the player collision handling that was missing!
		// [Paril-KEX]
		// Morphed players should never collide with other players (causes awful lag)
		if (!G_ShouldPlayersCollide(false) ||
			IsMorphed(ent) ||
			(deathmatch->integer && g_horde->integer && !(ent->clipmask & CONTENTS_PLAYER))) // if player collision is on and we're temporarily ghostly...
			client->ps.pmove.pm_flags |= PMF_IGNORE_PLAYER_COLLISION;
		else
			client->ps.pmove.pm_flags &= ~PMF_IGNORE_PLAYER_COLLISION;

		pm.s = client->ps.pmove;
		pm.s.origin = ent->s.origin;
		pm.s.velocity = ent->velocity;
		
		if (memcmp(&client->old_pmove, &pm.s, sizeof(pm.s)))
			pm.snapinitial = true;
		
		pm.cmd = *ucmd;
		pm.player = ent;
		pm.trace = gi.game_import_t::trace;
		pm.clip = SV_PM_Clip;
		pm.pointcontents = gi.pointcontents;
		pm.viewoffset = ent->client->ps.viewoffset;

		// Perform a pmove. THIS NOW RUNS FOR ALL PLAYERS INCLUDING MORPHED ONES!
		Pmove(&pm);

		if (pm.groundentity && ent->groundentity)
		{
			float const stepsize = fabs(ent->s.origin[2] - pm.s.origin[2]);
			if (stepsize > 4.f && stepsize < STEPSIZE)
			{
				ent->s.renderfx |= RF_STAIR_STEP;
				ent->client->step_frame = gi.ServerFrame() + 1;
			}
		}

		P_FallingDamage(ent, pm);

		if (ent->client->landmark_free_fall && pm.groundentity)
		{
			ent->client->landmark_free_fall = false;
			ent->client->landmark_noise_time = level.time + 100_ms;
		}

		// [Paril-KEX] save old position for G_TouchProjectiles
		vec3_t const old_origin = ent->s.origin;
		ent->s.origin = pm.s.origin;
		ent->velocity = pm.s.velocity;

		// [Paril-KEX] if we stepped onto/off of a ladder, reset the last ladder pos
		if ((pm.s.pm_flags & PMF_ON_LADDER) != (client->ps.pmove.pm_flags & PMF_ON_LADDER))
		{
			client->last_ladder_pos = ent->s.origin;
			if (pm.s.pm_flags & PMF_ON_LADDER)
			{
				if (!G_IsDeathmatch() && client->last_ladder_sound < level.time)
				{
					ent->s.event = EV_LADDER_STEP;
					client->last_ladder_sound = level.time + LADDER_SOUND_TIME;
				}
			}
		}

		// Save results of pmove
		client->ps.pmove = pm.s;
		client->old_pmove = pm.s;
		ent->mins = pm.mins;
		ent->maxs = pm.maxs;

		if (!ent->client->menu)
			client->resp.cmd_angles = ucmd->angles;

		if (pm.jump_sound && !(pm.s.pm_flags & PMF_ON_LADDER))
		{
			// Check if we're a brain morph and use brain jump sound
			if (IsMorphed(ent)) {
				auto* data = GetMorphData(ent);
				if (data && data->morph_type == MORPH_BRAIN) {
					// Use brain sight sound for jump (like the monster does)
					gi.sound(ent, CHAN_VOICE, gi.soundindex("brain/brnsght1.wav"), 1, ATTN_NORM, 0);
				} else {
					// Other morphs use normal jump sound for now
					gi.sound(ent, CHAN_VOICE, gi.soundindex("*jump1.wav"), 1, ATTN_NORM, 0);
				}
			} else {
				// Normal player jump sound
				gi.sound(ent, CHAN_VOICE, gi.soundindex("*jump1.wav"), 1, ATTN_NORM, 0);
			}
		}

		// ROGUE sam raimi cam support
		if (ent->flags & FL_SAM_RAIMI)
			ent->viewheight = 8;
		else
			ent->viewheight = (int)pm.s.viewheight;

		ent->waterlevel = pm.waterlevel;
		ent->watertype = pm.watertype;
		ent->groundentity = pm.groundentity;
		if (pm.groundentity)
			ent->groundentity_linkcount = pm.groundentity->linkcount;

		if (ent->deadflag)
		{
			client->ps.viewangles[ROLL] = 40;
			client->ps.viewangles[PITCH] = -15;
			client->ps.viewangles[YAW] = client->killer_yaw;
		}
		else
		{
			// Always update view angles and forward vector for proper attack origin calculation
			client->v_angle = pm.viewangles;
			AngleVectors(client->v_angle, client->v_forward, nullptr, nullptr);

			// Only update ps.viewangles when menu is not open to keep view stable during menu
			if (!ent->client->menu) {
				client->ps.viewangles = pm.viewangles;
			}

			// After angles are updated, run morph-specific logic
			// for attacks, model rotation, and animations.
			if (IsMorphed(ent))
			{
				// Sync the visual model's horizontal rotation with the player's view
				ent->s.angles[YAW] = client->v_angle[YAW];

				// Call the appropriate morph logic function based on type
				auto* data = GetMorphData(ent);
				if (data) {
					switch (data->morph_type) {
						case MORPH_FLYER:
							RunFlyerFrames(ent, *ucmd);
							ApplyFlyerPhysics(ent);
							break;
						case MORPH_BRAIN:
							RunBrainFrames(ent, *ucmd);
							// Brain uses normal physics, no special physics needed
							break;
						default:
							break;
					}
				}
			}
		}

		// ZOID
		if (client->ctf_grapple)
			CTFGrapplePull(client->ctf_grapple);
		// ZOID

		gi.linkentity(ent);

		// Don't reset gravity for morphed players - they manage their own gravity
		if (!IsMorphed(ent))
			ent->gravity = 1.0;

		// horde updating client health
		UpdateClientHealth(ent, client);
		UpdateIRTracking(ent, client);

		if (ent->movetype != MOVETYPE_NOCLIP)
		{
			G_TouchTriggers(ent);
			G_TouchProjectiles(ent, old_origin);
		}

		// Touch other objects
		for (i = 0; i < pm.touch.num; i++)
		{
			trace_t& tr = pm.touch.traces[i];
			other = tr.ent;
			if (other->touch)
				other->touch(other, ent, tr, true);
		}
	}

	// Fire weapon from final position if needed
	if (client->latched_buttons & BUTTON_ATTACK)
	{
		if (client->resp.spectator)
		{
			client->latched_buttons = BUTTON_NONE;
			if (client->chase_target)
			{
				// Simple toggle: auto-adaptive -> freecam
				client->ps.gunindex = 0;
				client->ps.gunskin = 0;
				client->chase_target = nullptr;
				client->use_eyecam = false;
				client->ps.pmove.pm_flags &= ~(PMF_NO_POSITIONAL_PREDICTION | PMF_NO_ANGULAR_PREDICTION);
			}
			else
			{
				// Enter adaptive camera mode from freecam
				GetChaseTarget(ent);
				client->auto_eyecam = true;
				client->use_eyecam = false;
			}
		}
		else if (!ent->client->weapon_thunk && !IsMorphed(ent))  // Morphed players handle attacks differently
		{
			if (ent->client->weaponstate == WEAPON_READY)
			{
				ent->client->weapon_fire_buffered = true;
				if (ent->client->weapon_fire_finished <= level.time)
				{
					ent->client->weapon_thunk = true;
					Think_Weapon(ent);
				}
			}
		}
	}

	if (client->resp.spectator || (G_TeamplayEnabled() && ent->client->resp.ctf_team == CTF_NOTEAM))
	{
		usercmd_t spec_menu_ucmd = *ucmd;
		if (!HandleMenuMovement(ent, &spec_menu_ucmd))
		{
			if (spec_menu_ucmd.buttons & BUTTON_JUMP)
			{
				if (!(client->ps.pmove.pm_flags & PMF_JUMP_HELD))
				{
					client->ps.pmove.pm_flags |= PMF_JUMP_HELD;
					if (client->chase_target)
						ChaseNext(ent);
					else
						GetChaseTarget(ent);
				}
			}
			else
				client->ps.pmove.pm_flags &= ~PMF_JUMP_HELD;
		}
		*ucmd = spec_menu_ucmd;
	}

	// Update chase cam if being followed
	for (i = 1; i <= game.maxclients; i++)
	{
		other = g_edicts + i;
		if (other->inuse && other->client->chase_target == ent)
			UpdateChaseCam(other);
	}
}

//static inline bool G_MonstersSearchingFor(const edict_t* player)
//{
//	for (auto const* ent : active_monsters())
//	{
//		// check for *any* player target
//		if (player == nullptr && ent->enemy && !ent->enemy->client)
//			continue;
//		// they're not targeting us, so who cares
//		else if (player != nullptr && ent->enemy != player)
//			continue;
//
//		// they lost sight of us
//		if ((ent->monsterinfo.aiflags & AI_LOST_SIGHT) && level.time > ent->monsterinfo.trail_time + 5_sec)
//			continue;
//
//		// no sir
//		return true;
//	}
//
//	// yes sir
//	return false;
//}

// [Paril-KEX] from the given player, find a good spot to
// spawn a player

// Spawnflags for trigger_hurt
constexpr spawnflags_t SPAWNFLAG_HURT_START_OFF = 1_spawnflag;
constexpr spawnflags_t SPAWNFLAG_HURT_NO_PLAYERS = 32_spawnflag;
constexpr spawnflags_t SPAWNFLAG_HURT_CLIPPED = 128_spawnflag;

// Helper struct and function for the filter
struct HurtFilterData {
    bool found;
};

static BoxEdictsResult_t HurtFilter(edict_t* ent, void* data) {
    auto* filterData = static_cast<HurtFilterData*>(data);

    if (strcmp(ent->classname, "trigger_hurt") == 0 &&
        !(ent->spawnflags.has(SPAWNFLAG_HURT_START_OFF) && ent->solid == SOLID_NOT) &&
        !ent->spawnflags.has(SPAWNFLAG_HURT_NO_PLAYERS))
    {
        filterData->found = true;
        return BoxEdictsResult_t::End; // Stop searching
    }
    return BoxEdictsResult_t::Skip;
}

// Rewritten function
bool IsInsideTriggerHurt(const vec3_t& point) {
    HurtFilterData filterData = { false };
    // Use a small box around the point to check for triggers
    gi.BoxEdicts(point, point, nullptr, 0, AREA_TRIGGERS, HurtFilter, &filterData);
    return filterData.found;
}

static BoxEdictsResult_t MonsterOnlyFilter(edict_t* ent, void* data) {
	FilterData* filter_data = static_cast<FilterData*>(data);
	if (ent == filter_data->ignore_ent)
		return BoxEdictsResult_t::Skip;

	if (ent->svflags & SVF_MONSTER) {
		filter_data->count++;
		return BoxEdictsResult_t::End;
	}
	return BoxEdictsResult_t::Skip;
}

bool HasMonsterAtPoint(const vec3_t& point) {
	const vec3_t spawn_mins = point + PLAYER_MINS;
	const vec3_t spawn_maxs = point + PLAYER_MAXS;
	FilterData filter_data = { nullptr, 0 };
	gi.BoxEdicts(spawn_mins, spawn_maxs, nullptr, 0, AREA_SOLID, MonsterOnlyFilter, &filter_data);
	return filter_data.count > 0;
}

// [Paril-KEX] from the given player, find a good spot to spawn a player
inline bool G_FindRespawnSpot(edict_t* player, vec3_t& spot) // Using spot reference is fine
{
	// --- Initial player state check ---
	// Sanity check: make sure the player themselves has room.
	// Use a more specific mask if needed, MASK_PLAYERSOLID might be better?
	// Using MASK_SOLID as original.
	trace_t tr = gi.trace(player->s.origin, PLAYER_MINS, PLAYER_MAXS, player->s.origin, player, MASK_SOLID);
	if (tr.startsolid || tr.allsolid) {
		return false; // Player is stuck, can't find a spot relative to them
	}

	// --- Constants ---
	// Use std::array for compile-time array
	static constexpr std::array<float, 5> yaw_spread = { 0.f, 90.f, 45.f, -45.f, -90.f };
	static constexpr float BACK_DISTANCE = 128.0f;
	static constexpr float UP_DISTANCE = 128.0f;
	static constexpr float DOWN_DISTANCE_MULTIPLIER = 4.0f; // How far down to trace
	static constexpr float PLAYER_VIEWHEIGHT = 22.0f;
	static constexpr float MIN_SLOPE_Z = 0.7f; // Minimum Z normal for valid ground
	static constexpr float MAX_STEP_HEIGHT = STEPSIZE * 4.0f; // Max Z difference allowed

	// Mask for finding the spawn spot (avoid solid, hazards, player clips)
	static constexpr contents_t SPAWN_SEARCH_MASK = MASK_SOLID | CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_PLAYERCLIP;
	// Mask for final visibility checks (don't need to check playerclip etc.)
	static constexpr contents_t VISIBILITY_MASK = MASK_SOLID;

	// --- Loop through candidate directions ---
	for (const float yaw : yaw_spread) // Use const float& or float
	{
		vec3_t angles = { 0.f, (player->s.angles[YAW] + 180.f) + yaw, 0.f }; // Ensure .f for floats

		// --- Trace Up ---
		vec3_t start = player->s.origin;
		vec3_t end = start + vec3_t{ 0.f, 0.f, UP_DISTANCE };
		tr = gi.trace(start, PLAYER_MINS, PLAYER_MAXS, end, player, SPAWN_SEARCH_MASK);

		// Check if stuck or hit hazard going up
		if (tr.startsolid || tr.allsolid || (tr.contents & (CONTENTS_LAVA | CONTENTS_SLIME))) {
			continue; // Try next direction
		}

		// --- Trace Back ---
		vec3_t fwd;
		AngleVectors(angles, fwd, nullptr, nullptr); // Calculate forward vector for this yaw

		start = tr.endpos; // Start from where the upward trace ended
		end = start + fwd * BACK_DISTANCE;
		tr = gi.trace(start, PLAYER_MINS, PLAYER_MAXS, end, player, SPAWN_SEARCH_MASK);

		// Check if stuck or hit hazard going back
		if (tr.startsolid || tr.allsolid || (tr.contents & (CONTENTS_LAVA | CONTENTS_SLIME))) {
			continue; // Try next direction
		}

		// --- Trace Down to find ground ---
		start = tr.endpos; // Start from where the backward trace ended
		end = start - vec3_t{ 0.f, 0.f, UP_DISTANCE * DOWN_DISTANCE_MULTIPLIER }; // Trace down further
		tr = gi.trace(start, PLAYER_MINS, PLAYER_MAXS, end, player, SPAWN_SEARCH_MASK);

		// --- Validate Ground Spot (Perform cheap checks first!) ---

		// 1. Check if trace failed badly (stuck, floating, hazard content hit)
		if (tr.startsolid || tr.allsolid || (tr.contents & (CONTENTS_LAVA | CONTENTS_SLIME)) || tr.fraction == 1.0f) {
			continue; // Bad spot: stuck, floating, or hit hazard directly
		}

		// 2. Check if landed on something other than the world (e.g., another entity)
		// Allow landing on world or solid func_ entities maybe? Check original intent.
		// Original code checked `tr.ent != world`. Assuming that's correct.
		if (tr.ent != world) {
			continue; // Landed on an entity, not ground
		}

		// 3. Check slope (Cheap: uses last trace result)
		if (tr.plane.normal.z < MIN_SLOPE_Z) {
			continue; // Too steep
		}

		// 4. Check Z distance (Cheap)
		const float z_diff = std::fabs(player->s.origin[2] - tr.endpos[2]); // Use std::fabs
		if (z_diff > MAX_STEP_HEIGHT) {
			continue; // Too high or too low difference
		}

		// --- More Expensive Checks ---

		// 5. Check for liquid at head height (Moderately expensive)
		vec3_t head_check_pos = tr.endpos + vec3_t{ 0.f, 0.f, PLAYER_VIEWHEIGHT };
		if (gi.pointcontents(head_check_pos) & MASK_WATER) { // MASK_WATER includes LAVA, SLIME too
			continue; // Head would be underwater/in hazard
		}

		// 6. Check if inside trigger_hurt (Potentially expensive)
		if (IsInsideTriggerHurt(tr.endpos)) {
			continue;
		}

		// 7. Check for monster collision (Potentially expensive)
		if (HasMonsterAtPoint(tr.endpos)) {
			continue;
		}

		// --- Final Visibility Check (Most Expensive, only if needed) ---
		// Only check visibility if the height difference is significant (> 1 step)
		if (z_diff > STEPSIZE)
		{
			// Check visibility from player origin to spot origin
			trace_t vis_tr = gi.traceline(player->s.origin, tr.endpos, player, VISIBILITY_MASK);
			if (vis_tr.fraction != 1.0f) {
				continue; // Path blocked
			}

			// Check visibility from player head to spot head
			vec3_t player_head = player->s.origin + vec3_t{ 0.f, 0.f, PLAYER_VIEWHEIGHT };
			vec3_t spot_head = tr.endpos + vec3_t{ 0.f, 0.f, PLAYER_VIEWHEIGHT };
			vis_tr = gi.traceline(player_head, spot_head, player, VISIBILITY_MASK);
			if (vis_tr.fraction != 1.0f) {
				continue; // Head path blocked
			}
		}

		// --- Found a valid spot! ---
		spot = tr.endpos; // Assign the valid spot
		return true;
	}

	// Looped through all directions, no suitable spot found
	return false;
}

struct PlayerStatus {
	edict_t* player;
	coop_respawn_t state;
	vec3_t spot;
};

// This is the recommended final version.
// It is simple, reliable, and highly performant due to caching the main bottleneck.

inline std::tuple<edict_t*, vec3_t> G_FindSquadRespawnTarget() {
	// --- Constants ---
	static constexpr auto BAD_AREA_TIMEOUT = 3_sec;
	static constexpr auto ZERO_TIME = 0_ms;
	static constexpr auto MAX_INT64_VAL = std::numeric_limits<int64_t>::max();

	// --- Data Structures () ---
    // Use a static C-style array to avoid vector overhead entirely.
	static std::array<std::pair<edict_t*, vec3_t>, MAX_CLIENTS> candidates_arr;
	size_t num_candidates = 0; // Reset count each frame.

	const bool is_horde_mode = g_horde->integer != 0;
	gtime_t min_combat_time_left = gtime_t::from_ms(MAX_INT64_VAL);
	gtime_t min_bad_area_time_left = BAD_AREA_TIMEOUT;
	bool player_in_combat = false;
	bool player_in_bad_area = false;
	bool force_respawn_possible = false;

	// --- UI Message State Caching ---
	static bool last_frame_in_combat = false;
	static bool last_frame_in_bad_area = false;
	static gtime_t last_displayed_combat_time = ZERO_TIME;
	static gtime_t last_displayed_bad_area_time = ZERO_TIME;
	static char cached_combat_message[128] = "";
	static char cached_bad_area_message[128] = "";

	// --- Single Processing Loop (Runs Every Frame) ---
	auto process_single_player = [&](edict_t* player) {
		if (!player || !player->client) [[unlikely]] return;

		if (player->deadflag) [[unlikely]] {
			player->client->coop_respawn_state = COOP_RESPAWN_WAITING;
			return;
		}

		gtime_t combat_time_for_player = player->client->last_damage_time - level.time;
		if (combat_time_for_player > ZERO_TIME) [[likely]] {
			player->client->coop_respawn_state = COOP_RESPAWN_IN_COMBAT;
			player_in_combat = true;
			min_combat_time_left = std::min(min_combat_time_left, combat_time_for_player);
			player->client->time_in_bad_area = ZERO_TIME;
			return;
		}

		// --- Caching Logic for G_FindRespawnSpot ---
		vec3_t spot;
		bool is_valid_spot = false;
		static constexpr auto CACHE_DURATION = 250_ms;
		static constexpr float CACHE_INVALIDATE_DISTANCE_SQ = 64.0f * 64.0f;

		gtime_t cache_expiry_time = player->client->squad_spot_cache_time + CACHE_DURATION;
		vec3_t dist_vec = player->s.origin - player->client->squad_spot_cached_at_pos;
		float dist_sq = dist_vec.x * dist_vec.x + dist_vec.y * dist_vec.y + dist_vec.z * dist_vec.z;

		if (player->client->squad_spot_cache_time > 0_ms && level.time < cache_expiry_time && dist_sq < CACHE_INVALIDATE_DISTANCE_SQ) {
			spot = player->client->cached_squad_spot;
			is_valid_spot = true;
		} else {
			is_valid_spot = G_FindRespawnSpot(player, spot);
			if (is_valid_spot) {
				player->client->cached_squad_spot = spot;
				player->client->squad_spot_cache_time = level.time;
				player->client->squad_spot_cached_at_pos = player->s.origin;
			} else {
				player->client->squad_spot_cache_time = 0_ms;
			}
		}
		// --- End of Caching Logic ---

		bool is_currently_in_bad_area = (player->groundentity != world || player->waterlevel >= WATER_UNDER);

		if (is_currently_in_bad_area || !is_valid_spot) [[unlikely]] {
			player->client->coop_respawn_state = is_currently_in_bad_area ? COOP_RESPAWN_BAD_AREA : COOP_RESPAWN_BLOCKED;
			player->client->time_in_bad_area += FRAME_TIME_MS;
			gtime_t time_left_for_player = std::max(ZERO_TIME, BAD_AREA_TIMEOUT - player->client->time_in_bad_area);
			min_bad_area_time_left = std::min(min_bad_area_time_left, time_left_for_player);
			player_in_bad_area = true;
			if (time_left_for_player <= ZERO_TIME) {
				force_respawn_possible = true;
			}
		} else {
			player->client->coop_respawn_state = COOP_RESPAWN_NONE;
			player->client->time_in_bad_area = ZERO_TIME;
            if (num_candidates < candidates_arr.size()) {
                candidates_arr[num_candidates] = {player, spot};
                num_candidates++;
            }
		}
	};

	// Execute the loop
	if (is_horde_mode) {
		for (auto player_ent : active_players_no_spect()) { process_single_player(player_ent); }
	} else {
		for (auto player_ent : active_players()) { process_single_player(player_ent); }
	}

	// --- Update UI Messages (Runs Every Frame) ---
	bool update_combat_msg = false;
	bool update_bad_area_msg = false;

	if (player_in_combat != last_frame_in_combat || (player_in_combat && min_combat_time_left != last_displayed_combat_time)) {
		update_combat_msg = true;
		last_displayed_combat_time = min_combat_time_left;
	}
	if (!player_in_bad_area && last_frame_in_bad_area) {
		update_bad_area_msg = true;
	} else if (player_in_bad_area != last_frame_in_bad_area || (player_in_bad_area && min_bad_area_time_left != last_displayed_bad_area_time)) {
		update_bad_area_msg = true;
		last_displayed_bad_area_time = min_bad_area_time_left;
	}

	// Inside G_FindSquadRespawnTarget...

if (update_combat_msg) {
    // G_Fmt returns a view into a static buffer. Get a direct pointer to it.
    const char* new_msg_ptr = player_in_combat
        ? G_Fmt("In Combat! Reviving in: {:.1f}s", min_combat_time_left.seconds<float>()).data()
        : "";

    // Compare C-strings directly. This is faster than std::string comparison.
    if (strcmp(new_msg_ptr, cached_combat_message) != 0) {
        // Copy the new message into our static char[] cache. No heap allocation.
        Q_strlcpy(cached_combat_message, new_msg_ptr, sizeof(cached_combat_message));
        gi.configstring(CONFIG_COOP_RESPAWN_STRING + 0, new_msg_ptr);
    }
}

if (update_bad_area_msg) {
    // The double-buffer in G_Fmt allows this second call to be safe.
    const char* new_msg_ptr = (player_in_bad_area && min_bad_area_time_left > ZERO_TIME)
        ? G_Fmt("Bad/Blocked Area! Forcing Respawn in: {:.1f}s", min_bad_area_time_left.seconds<float>()).data()
        : "";

    if (strcmp(new_msg_ptr, cached_bad_area_message) != 0) {
        Q_strlcpy(cached_bad_area_message, new_msg_ptr, sizeof(cached_bad_area_message));
        gi.configstring(CONFIG_COOP_RESPAWN_STRING + 1, new_msg_ptr);
    }
}

	last_frame_in_combat = player_in_combat;
	last_frame_in_bad_area = player_in_bad_area;

	// --- Select Respawn Target ---
	if (num_candidates > 0) {
		size_t index = static_cast<size_t>(irandom(static_cast<int32_t>(num_candidates)));
		return { candidates_arr[index].first, candidates_arr[index].second };
	}

	// --- Handle Forced Respawn ---
	if (force_respawn_possible) {
		auto check_forced_respawn = [&](edict_t* player) -> std::tuple<edict_t*, vec3_t> {
			if (player && player->client &&
			   (player->client->coop_respawn_state == COOP_RESPAWN_BAD_AREA || player->client->coop_respawn_state == COOP_RESPAWN_BLOCKED) &&
			   (BAD_AREA_TIMEOUT - player->client->time_in_bad_area <= ZERO_TIME)) {
				if (edict_t* spawn_point = SelectSingleSpawnPoint(player)) {
					player->client->coop_respawn_state = COOP_RESPAWN_NONE;
					return { player, spawn_point->s.origin };
				}
			}
			return { nullptr, vec3_t{} };
		};

		if (is_horde_mode) {
			for (auto p_ent : active_players_no_spect()) {
				auto res = check_forced_respawn(p_ent);
				if (std::get<0>(res) != nullptr) return res;
			}
		} else {
			for (auto p_ent : active_players()) {
				auto res = check_forced_respawn(p_ent);
				if (std::get<0>(res) != nullptr) return res;
			}
		}
	}

	// --- Mark remaining players as waiting ---
	auto update_to_waiting = [](edict_t* player) {
		if (player && player->client && !player->deadflag && player->client->coop_respawn_state == COOP_RESPAWN_NONE) {
			player->client->coop_respawn_state = COOP_RESPAWN_WAITING;
		}
	};
	if (is_horde_mode) {
		for (auto p_ent : active_players_no_spect()) { update_to_waiting(p_ent); }
	} else {
		for (auto p_ent : active_players()) { update_to_waiting(p_ent); }
	}

	return { nullptr, vec3_t{} };
}

enum respawn_state_t
{
	RESPAWN_NONE,     // invalid state
	RESPAWN_SPECTATE, // move to spectator
	RESPAWN_SQUAD,    // move to good squad point
	RESPAWN_START     // move to start of map
};

// [Paril-KEX] return false to fall back to click-to-respawn behavior.
// note that this is only called if they are allowed to respawn (not
// restarting the level due to all being dead)
static bool G_CoopRespawn(edict_t* ent)
{
	// don't do this in non-coop
	//  if (!G_IsCooperative())
	//      return false;
	// if we don't have squad or lives, it doesn't matter
	if (!g_coop_squad_respawn->integer && !g_coop_enable_lives->integer)
		return false;

	respawn_state_t state = RESPAWN_NONE;

	// first pass: if we have no lives left, just move to spectator
	if (g_coop_enable_lives->integer)
	{
		if (ent->client->pers.lives == 0)
		{
			state = RESPAWN_SPECTATE;
			ent->client->coop_respawn_state = COOP_RESPAWN_NO_LIVES;
		}
	}

	// second pass: check for where to spawn
	if (state == RESPAWN_NONE)
	{
		// if squad respawn, don't respawn until we can find a good player to spawn on.
		if (g_coop_squad_respawn->integer)
		{
			bool allDead = true;

			for (auto const* player : active_players())
			{
				if (player->health > 0)
				{
					allDead = false;
					break;
				}
			}

			// all dead, so if we ever get here we have lives enabled;
			// we should just respawn at the start of the level
			if (allDead)
				state = RESPAWN_START;
			else
			{
				auto [good_player, good_spot] = G_FindSquadRespawnTarget();

				if (good_player)
				{
					state = RESPAWN_SQUAD;

					squad_respawn_position = good_spot;

					// // If the respawning player is human (not a bot), add some offset
					// // to avoid spawning at exactly the same position
					// if (!(ent->svflags & SVF_BOT))
					// {
					// 	// Try to find a valid offset position for human players
					// 	vec3_t test_pos;
					// 	trace_t trace;
					// 	bool found_valid_offset = false;

					// 	// Try up to 8 different angles to find a valid spawn position
					// 	for (int attempts = 0; attempts < 8; attempts++)
					// 	{
					// 		float angle = frandom() * PIf * 2.0f;
					// 		float distance = 32.0f + frandom() * 32.0f; // 32-64 units away

					// 		test_pos = squad_respawn_position;
					// 		test_pos[0] += cos(angle) * distance;
					// 		test_pos[1] += sin(angle) * distance;

					// 		// Check if the new position is valid (not in solid)
					// 		trace = gi.trace(squad_respawn_position, PLAYER_MINS, PLAYER_MAXS,
					// 						test_pos, ent, MASK_PLAYERSOLID);

					// 		// Check if path is clear and destination is not stuck
					// 		if (trace.fraction == 1.0f && !trace.allsolid && !trace.startsolid)
					// 		{
					// 			// Additional check: make sure the player won't be stuck at the new position
					// 			trace_t pos_check = gi.trace(test_pos, PLAYER_MINS, PLAYER_MAXS,
					// 										 test_pos, ent, MASK_PLAYERSOLID);

					// 			if (!pos_check.startsolid && !pos_check.allsolid)
					// 			{
					// 				// Valid position found
					// 				squad_respawn_position = test_pos;
					// 				found_valid_offset = true;
					// 				break;
					// 			}
					// 		}
					// 	}

					// 	// If no valid offset found, use original position (better than spawning in a wall)
					// 	// The original position was already validated by G_FindRespawnSpot
					// }
					// // Bots spawn at the exact same position (no offset)
					
					squad_respawn_angles = good_player->s.angles;
					squad_respawn_angles[2] = 0;

					use_squad_respawn = true;
				}
				else
				{
					state = RESPAWN_SPECTATE;
				}
			}
		}
		else
			state = RESPAWN_START;
	}

	if (state == RESPAWN_SQUAD || state == RESPAWN_START)
	{
		// give us our max health back since it will reset
		// to pers.health; in instanced items we'd lose the items
		// we touched so we always want to respawn with our max.
		if (P_UseCoopInstancedItems())
			ent->client->pers.health = ent->client->pers.max_health = ent->max_health;

		respawn(ent);

		ent->client->latched_buttons = BUTTON_NONE;
		use_squad_respawn = false;
	}
	else if (state == RESPAWN_SPECTATE)
	{
		if (!ent->client->coop_respawn_state)
			ent->client->coop_respawn_state = COOP_RESPAWN_WAITING;

		if (!ent->client->resp.spectator)
		{
			// move us to spectate just so we don't have to twiddle
			// our thumbs forever
			CopyToBodyQue(ent);
			ent->client->resp.spectator = true;
			ent->solid = SOLID_NOT;
			ent->takedamage = false;
			ent->s.modelindex = 0;
			ent->svflags |= SVF_NOCLIENT;
			ent->client->ps.damage_blend[3] = ent->client->ps.screen_blend[3] = 0;
			ent->client->ps.rdflags = RDF_NONE;
			ent->movetype = MOVETYPE_NOCLIP;
			// TODO: check if anything else needs to be reset
			gi.linkentity(ent);
			GetChaseTarget(ent);
		}
	}

	return true;
}




/*
==============
ClientBeginServerFrame

This will be called once for each server frame, before running
any other entities in the world.
==============
*/
void ClientBeginServerFrame(edict_t* ent)
{
	gclient_t* client;
	int		   buttonMask;

	if (gi.ServerFrame() != ent->client->step_frame)
		ent->s.renderfx &= ~RF_STAIR_STEP;

	if (level.intermissiontime)
		return;

	client = ent->client;

	if (client->awaiting_respawn)
	{
		if ((level.time.milliseconds() % 500) == 0)
			PutClientInServer(ent);
		return;
	}

	if ((ent->svflags & SVF_BOT) != 0) {
		Bot_BeginFrame(ent);
	}

	if (G_IsDeathmatch() && !G_TeamplayEnabled() &&
		client->pers.spectator != client->resp.spectator &&
		(level.time - client->respawn_time) >= 5_sec)
	{
		spectator_respawn(ent);
		return;
	}

	// run weapon animations if it hasn't been done by a ucmd_t
	if (!client->weapon_thunk && !client->resp.spectator && !IsMorphed(ent))
		Think_Weapon(ent);
	else
		client->weapon_thunk = false;

	if (ent->deadflag)
	{
		// don't respawn if level is waiting to restart
		if (level.time > client->respawn_time && !level.coop_level_restart_time)
		{
			// check for coop handling
			if (!G_CoopRespawn(ent))
			{
				// in deathmatch, only wait for attack button
				if (deathmatch->integer)
					buttonMask = BUTTON_ATTACK;
				else
					buttonMask = -1;

				if ((client->latched_buttons & buttonMask) ||
					(deathmatch->integer && g_dm_force_respawn->integer))
				{
					respawn(ent);
					client->latched_buttons = BUTTON_NONE;
				}
			}
		}
		return;
	}

	// add player trail so monsters can follow
	if (!deathmatch->integer || g_horde->integer || pvm->integer || G_IsCooperative())
		PlayerTrail_Add(ent);

	client->latched_buttons = BUTTON_NONE;
}
/*
==============
RemoveAttackingPainDaemons

This is called to clean up the pain daemons that the disruptor attaches
to clients to damage them.
==============
*/
void RemoveAttackingPainDaemons(edict_t* self)
{
	edict_t* tracker;

	tracker = G_FindByString<&edict_t::classname>(nullptr, "pain daemon");
	while (tracker)
	{
		if (tracker->enemy == self)
			G_FreeEdict(tracker);
		tracker = G_FindByString<&edict_t::classname>(tracker, "pain daemon");
	}

	if (self->client)
		self->client->tracker_pain_time = 0_ms;
}
