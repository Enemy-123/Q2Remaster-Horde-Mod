/*============================================================================

	This file is part of Lithium II Mod for Quake II
	Copyright (C) 1997, 1998, 1999, 2010 Matthew A. Ayres

	Lithium II Mod is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Lithium II Mod is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Lithium II Mod.  If not, see <http://www.gnu.org/licenses/>.

	Quake II is a trademark of Id Software, Inc.

	Code by Matt "WhiteFang" Ayres, matt@lithium.com

============================================================================*/

// Offhand laser grappling hook
//
// Code originally from Orange 2 Mod

#include "g_local.h"
#include "shared.h"
#include "g_config.h"

// Legacy cvars - kept for console/config compatibility but values now loaded from g_config
cvar_t* hook_speed;
cvar_t* hook_pullspeed;
cvar_t* hook_sky;
cvar_t* hook_maxtime;
cvar_t* hook_damage;
cvar_t* hook_initdamage;
cvar_t* hook_maxdamage;
cvar_t* hook_delay;
cvar_t* hook_bot_chain_speed;
cvar_t* hook_bot_throw_speed;

// Helper function to check if an entity can be chained by the hook
// Returns true for bots, player's own summoned monsters, or player's sentries
bool Hook_CanChainEntity(edict_t* entity, edict_t* player)
{
	if (!entity || !player || !player->client)
		return false;

	// Bots can always be chained
	if (entity->client && (entity->svflags & SVF_BOT))
		return true;

	// Check if it's a summoned monster that belongs to this player
	if (entity->monsterinfo.issummoned)
	{
		// Check if the player owns this summon via deployed_summons array
		for (int i = 0; i < MAX_STROGG_SUMMONS_ARRAY_SIZE; i++)
		{
			if (player->client->resp.deployed_summons[i] == entity)
				return true;
		}

		// Also check via teammaster/chain (medic resurrections use this)
		if (entity->teammaster == player || entity->chain == player)
			return true;

		// Allow anyone to chain bot-owned summons (prevents obstruction)
		if (entity->chain && entity->chain->client && (entity->chain->svflags & SVF_BOT))
			return true;
	}

	// Check if it's a sentry gun that belongs to this player
	if (horde::IsSpecialType(entity, horde::SpecialEntityTypeID::SENTRY_GUN))
	{
		for (int i = 0; i < SentryConstants::MAX_SENTRIES_PER_PLAYER(); i++)
		{
			if (player->client->resp.deployed_sentries[i] == entity)
				return true;
		}

		// Allow anyone to chain bot-owned sentries (prevents obstruction)
		// Sentries track their owner via the owner field
		if (entity->owner && entity->owner->client && (entity->owner->svflags & SVF_BOT))
			return true;
	}

	return false;
}

// Configuration for auto-targeting chainable entities
namespace HookAutoTarget {
	constexpr float MAX_DISTANCE = 2048.0f;      // Maximum targeting range
	constexpr float MAX_DISTANCE_SQ = MAX_DISTANCE * MAX_DISTANCE;
	constexpr float MIN_DOT = 0.9f;              // Narrow cone (must aim closely)
	constexpr float CLOSE_DISTANCE = 512.0f;     // Distance for relaxed targeting
	constexpr float CLOSE_DISTANCE_SQ = CLOSE_DISTANCE * CLOSE_DISTANCE;
	constexpr float CLOSE_MIN_DOT = 0.7f;        // Wider cone when close
	constexpr float SCORING_DOT_WEIGHT = 1000.0f; // Weight for aim accuracy in scoring
}

// Finds the best chainable entity that the player is aiming at
// Returns nullptr if no valid target found
// Similar to FindBestTarget from g_idview.cpp but specifically for hook targets
edict_t* Hook_FindChainableInView(edict_t* player)
{
	if (!player || !player->client)
		return nullptr;

	vec3_t forward;
	AngleVectors(player->client->v_angle, forward, nullptr, nullptr);
	vec3_t const& viewer_pos = player->s.origin;

	edict_t* best_entity = nullptr;
	float best_score = -999999.0f;

	// Check all active players (bots)
	for (edict_t* target : active_players())
	{
		if (!Hook_CanChainEntity(target, player))
			continue;

		vec3_t dir = target->s.origin - viewer_pos;
		float const dist_sq = dir.lengthSquared();

		if (dist_sq > HookAutoTarget::MAX_DISTANCE_SQ)
			continue;

		dir.normalize();
		float const dot = forward.dot(dir);

		// Determine minimum dot product based on distance
		float const min_dot = (dist_sq < HookAutoTarget::CLOSE_DISTANCE_SQ)
			? HookAutoTarget::CLOSE_MIN_DOT
			: HookAutoTarget::MIN_DOT;

		if (dot < min_dot)
			continue;

		// Score: higher dot = better aim, lower distance = closer
		float score = (dot * HookAutoTarget::SCORING_DOT_WEIGHT) - dist_sq;
		if (score > best_score)
		{
			best_score = score;
			best_entity = target;
		}
	}

	// Check all active monsters (summoned ones)
	for (edict_t* target : active_monsters())
	{
		if (!Hook_CanChainEntity(target, player))
			continue;

		vec3_t dir = target->s.origin - viewer_pos;
		float const dist_sq = dir.lengthSquared();

		if (dist_sq > HookAutoTarget::MAX_DISTANCE_SQ)
			continue;

		dir.normalize();
		float const dot = forward.dot(dir);

		float const min_dot = (dist_sq < HookAutoTarget::CLOSE_DISTANCE_SQ)
			? HookAutoTarget::CLOSE_MIN_DOT
			: HookAutoTarget::MIN_DOT;

		if (dot < min_dot)
			continue;

		float score = (dot * HookAutoTarget::SCORING_DOT_WEIGHT) - dist_sq;
		if (score > best_score)
		{
			best_score = score;
			best_entity = target;
		}
	}

	// Check special entities (sentrygun)
	for (edict_t* target : g_targetable_special_entities)
	{
		if (!Hook_CanChainEntity(target, player))
			continue;

		vec3_t dir = target->s.origin - viewer_pos;
		float const dist_sq = dir.lengthSquared();

		if (dist_sq > HookAutoTarget::MAX_DISTANCE_SQ)
			continue;

		dir.normalize();
		float const dot = forward.dot(dir);

		float const min_dot = (dist_sq < HookAutoTarget::CLOSE_DISTANCE_SQ)
			? HookAutoTarget::CLOSE_MIN_DOT
			: HookAutoTarget::MIN_DOT;

		if (dot < min_dot)
			continue;

		float score = (dot * HookAutoTarget::SCORING_DOT_WEIGHT) - dist_sq;
		if (score > best_score)
		{
			best_score = score;
			best_entity = target;
		}
	}

	// Final visibility check
	if (best_entity)
	{
		trace_t const tr = gi.traceline(viewer_pos, best_entity->s.origin, player, MASK_SOLID);
		if (tr.fraction == 1.0f || tr.ent == best_entity)
		{
			return best_entity;
		}
	}

	return nullptr;
}

void Hook_InitGame(void)
{
	// Initialize cvars from g_config values (loaded from JSON)
	hook_speed = gi.cvar("hook_speed", G_Fmt("{}", g_config.hook.speed).data(), CVAR_NOFLAGS);
	hook_pullspeed = gi.cvar("hook_pullspeed", G_Fmt("{}", g_config.hook.pull_speed).data(), CVAR_NOFLAGS);
	hook_sky = gi.cvar("hook_sky", g_config.hook.allow_sky_attach ? "1" : "0", CVAR_NOFLAGS);
	hook_maxtime = gi.cvar("hook_maxtime", G_Fmt("{}", g_config.hook.max_time_sec).data(), CVAR_NOFLAGS);
	hook_damage = gi.cvar("hook_damage", G_Fmt("{}", g_config.hook.damage).data(), CVAR_NOFLAGS);
	hook_initdamage = gi.cvar("hook_initdamage", G_Fmt("{}", g_config.hook.init_damage).data(), CVAR_NOFLAGS);
	hook_maxdamage = gi.cvar("hook_maxdamage", G_Fmt("{}", g_config.hook.max_damage).data(), CVAR_NOFLAGS);
	hook_delay = gi.cvar("hook_delay", G_Fmt("{}", g_config.hook.delay_sec).data(), CVAR_NOFLAGS);
	hook_bot_chain_speed = gi.cvar("hook_bot_chain_speed", G_Fmt("{}", g_config.hook.bot_chain_speed).data(), CVAR_NOFLAGS);
	hook_bot_throw_speed = gi.cvar("hook_bot_throw_speed", G_Fmt("{}", g_config.hook.bot_throw_speed).data(), CVAR_NOFLAGS);

	gi.AddCommandString("alias +hook hook\n");
	gi.AddCommandString("alias -hook unhook\n");

	//unrelated to hook but convenient to add here
	gi.AddCommandString("alias coopp \"bot_pause 1; skill 3; g_dm_spawns 0; g_use_hook 0; g_instagib 0; horde 0; coop 1; deathmatch 0; g_allow_grapple 0; g_coop_squad_respawn 1; g_allow_techs 0; g_coop_num_lives 7; set cheats 0 s; g_coop_health_scaling 0.23; g_allow_techs 0; timelimit 0; maxclients 7; kexmultiplayer maxplayers 7\n");
}

void Hook_PlayerDie(edict_t* attacker, edict_t* self)
{
	Hook_Reset(self->client->hook);
}

// reset the hook.  pull all entities out of the world and reset
// the clients weapon state
void Hook_Reset(edict_t* rhook)
{
	if (!rhook)
		return;

	// If we were holding a sentry, restore it to normal state
	if (rhook->enemy && rhook->enemy->inuse &&
	    horde::IsSpecialType(rhook->enemy, horde::SpecialEntityTypeID::SENTRY_GUN))
	{
		edict_t* sentry = rhook->enemy;

		// Restore normal sentry physics
		sentry->movetype = MOVETYPE_NONE;
		sentry->solid = SOLID_BBOX;
		sentry->velocity = vec3_origin;

		// Restore base entity visibility if it exists
		if (sentry->teamchain && sentry->teamchain->inuse)
		{
			sentry->teamchain->solid = SOLID_NOT;
			sentry->teamchain->svflags &= ~SVF_NOCLIENT;
			gi.linkentity(sentry->teamchain);
		}

		gi.linkentity(sentry);
	}

	// start with nullptr pointer checks
	if (rhook->owner && rhook->owner->client)
	{
		// client's hook is no longer out
		rhook->owner->client->hook_out = false;
		rhook->owner->client->hook_on = false;
		rhook->owner->client->hook = nullptr;
		rhook->owner->client->hook_toggle = false;
		rhook->owner->client->hook_release_time = level.time.seconds();
		//	   rhook->owner->client->ps.pmove.pm_flags &= ~PMF_NO_PREDICTION;
	}

	// this should always be true and free the laser beam
	if (rhook->laser)
		G_FreeEdict(rhook->laser);

	// delete ourself
	G_FreeEdict(rhook);
}

// resets the hook if it needs to be
bool Hook_Check(edict_t* self)
{
	if (!self || !self->owner || !self->owner->client) {
		Hook_Reset(self);
		return true;
	}

	// drop the hook if either party dies/leaves the game/etc.
	if ((!self->enemy->inuse) || (!self->owner->inuse) ||
		(self->enemy->client && self->enemy->health <= 0) ||
		(self->owner->health <= 0))
	{
		Hook_Reset(self);
		return true;
	}

	// Special handling for chained bots/summons - throw them on button release
	if (self->enemy && Hook_CanChainEntity(self->enemy, self->owner) &&
		(self->owner->client->latched_buttons & BUTTON_ATTACK))
	{
		// Calculate throw direction from player's view
		vec3_t forward;
		AngleVectors(self->owner->client->v_angle, forward, nullptr, nullptr);

		// Throw the entity with high velocity
		self->enemy->velocity = forward * hook_bot_throw_speed->value;

		// Reset hook
		Hook_Reset(self);
		return true;
	}

	// drop the hook if player lets go of button
	// and has the hook as current weapon
	if (((self->owner->client->latched_buttons & BUTTON_ATTACK)
		&& self->owner->client->pers.weapon && // Agregar esta verificaciÃ³n
		(strcmp(self->owner->client->pers.weapon->pickup_name, "Hook") == 0))) // DEBUGGER POINTED THIS LINE!
	{
		Hook_Reset(self);
		return true;
	}

	// Return false if none of the conditions are met
	return false;
}

void Hook_Service(edict_t* self)
{
	// if hook should be dropped, just return
	if (Hook_Check(self))
		return;

	// Don't pull the player if they have a bot or summoned monster chained
	if (self->enemy && Hook_CanChainEntity(self->enemy, self->owner))
		return;

	// give the client some velocity ...
	vec3_t hook_dir;
	if (self->enemy->client)
		hook_dir = self->enemy->s.origin - self->owner->s.origin;
	else
		hook_dir = self->s.origin - self->owner->s.origin;

	// Normalize safely and apply velocity
	hook_dir = safe_normalized(hook_dir);
	self->owner->velocity = hook_dir * hook_pullspeed->value;

	//  SV_AddGravity(self->owner);
}

// keeps the invisible hook entity on hook->enemy (can be world or an entity)
THINK(Hook_Track) (edict_t* self) -> void
{
	vec3_t normal;

	// if hook should be dropped, just return
	if (Hook_Check(self))
		return;

	// bring the pAiN!
	if (self->enemy->client || self->enemy->monsterinfo.issummoned ||
	    horde::IsSpecialType(self->enemy, horde::SpecialEntityTypeID::SENTRY_GUN))
	{
		// Special handling for chained bots/summons/sentries - gravity gun style
		if (Hook_CanChainEntity(self->enemy, self->owner))
		{
			// Check if entity is behind a wall or too far from player
			float distance_to_player = (self->enemy->s.origin - self->owner->s.origin).length();
			trace_t vis_trace = gi.traceline(self->owner->s.origin, self->enemy->s.origin, self->enemy, MASK_SOLID);
			bool is_blocked = (vis_trace.fraction < 1.0f && vis_trace.ent != self->enemy);
			bool is_too_far = (distance_to_player > 800.0f);

			// Teleport entity closer if it's stuck behind a wall or too far
			if (is_blocked || is_too_far)
			{
				vec3_t forward;
				AngleVectors(self->owner->client->v_angle, forward, nullptr, nullptr);
				vec3_t teleport_pos = self->owner->s.origin + (forward * 200.0f);

				// Try to find a valid teleport position
				trace_t ground_trace = gi.trace(teleport_pos, self->enemy->mins, self->enemy->maxs,
				                                 teleport_pos - vec3_t{0, 0, 128}, self->enemy, MASK_SOLID);

				if (ground_trace.fraction < 1.0f)
				{
					self->enemy->s.origin = ground_trace.endpos;
					self->enemy->velocity = vec3_origin;
					gi.linkentity(self->enemy);
				}
			}

			// Check if this is a sentry gun
			if (horde::IsSpecialType(self->enemy, horde::SpecialEntityTypeID::SENTRY_GUN))
			{
				// Use sentry-specific visualization (velocity-based like bots)
				sentry_hookplacement(self->enemy, self->owner, self->wait);
			}
			else
			{
				// Bot/summon handling - use velocity-based movement
				// Calculate where the bot SHOULD be (at stored distance in view direction)
				vec3_t forward;
				AngleVectors(self->owner->client->v_angle, forward, nullptr, nullptr);
				vec3_t target_position = self->owner->s.origin + (forward * self->wait);

				// Calculate direction from bot's current position to target position
				vec3_t pull_dir = target_position - self->enemy->s.origin;
				float distance_to_target = pull_dir.length();

				// Apply velocity proportional to distance (stronger pull when further from target)
				// This creates smooth "spring-like" dragging that responds to mouse movement speed
				if (distance_to_target > 1.0f)
				{
					pull_dir = safe_normalized(pull_dir);
					self->enemy->velocity = pull_dir * (distance_to_target * hook_bot_chain_speed->value / 100.0f);
				}
			}

			// Update hook position to follow the chained entity
			gi.unlinkentity(self);
			self->s.origin = self->enemy->s.origin;
			gi.linkentity(self);
		}
		else
		{
			// Normal client handling - damage over time
			// move the hook along with the player.  It's invisible, but
			// we need this to make the sound come from the right spot

			if (self->owner->client->hook_damage >= hook_maxdamage->value)
			{
				Hook_Reset(self);
				return;
			}

			gi.unlinkentity(self);
			self->s.origin = self->enemy->s.origin;
			gi.linkentity(self);

			normal = self->enemy->s.origin - self->owner->s.origin;

			T_Damage(self->enemy, self, self->owner, vec3_origin, self->enemy->s.origin, normal, hook_damage->value, 0, DAMAGE_NO_KNOCKBACK, MOD_HOOK);

			self->owner->client->hook_damage += hook_damage->value;
		}
	}
	else
	{
		// If the hook is not attached to the player, constantly copy
		// copy the target's velocity. Velocity copying DOES NOT work properly
		// for a hooked client. 
		self->velocity = self->enemy->velocity;

		if (hook_maxtime->value && level.time - self->owner->hook_time > gtime_t::from_sec(hook_maxtime->value))
		{
			Hook_Reset(self);
			return;
		}
	}

	self->nextthink = level.time + 100_ms;
}

// the hook has hit something.  what could it be?
TOUCH(Hook_Touch) (edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
	vec3_t dir, normal, forward;

	// ignore hitting the person who launched us
	if (other == self->owner)
		return;

	if (!self->owner || !self->owner->client)
		return;

	// ignore hitting items/projectiles/etc.
	if (other->solid == SOLID_NOT || other->solid == SOLID_TRIGGER || other->movetype == MOVETYPE_FLYMISSILE)
		return;

	if (tr.surface && (tr.surface->flags & SURF_SKY) && !hook_sky->value)
	{
		Hook_Reset(self);
		return;
	}

	// Check if we hit a chainable entity (bot or player's summoned monster)
	if (Hook_CanChainEntity(other, self->owner))
	{
		// Attach hook to entity instead of instant pull
		self->enemy = other;

		// Store the initial distance from player to entity (for gravity gun effect)
		vec3_t hook_vec = other->s.origin - self->owner->s.origin;
		self->wait = hook_vec.length();

		// Start tracking the entity
		self->think = Hook_Track;
		self->nextthink = level.time + 100_ms;

		// Hook is now on
		self->owner->client->hook_on = true;
		self->solid = SOLID_NOT;

		// Play attachment sound
		gi.positioned_sound(self->s.origin, self, CHAN_WEAPON, gi.soundindex("flyer/Flyatck2.wav"), 1, ATTN_NORM, 0);

		self->owner->hook_time = level.time;
		return;
	}
	else if (other->client)
	{
		if (OnSameTeam(other, self->owner) && other->svflags & ~SVF_BOT)

		// we hit an enemy, so do a bit of damage
		dir = self->owner->s.origin - other->s.origin;
		normal = other->s.origin - self->owner->s.origin;

		if (self->owner->client->hook_damage >= hook_maxdamage->value) {
			Hook_Reset(self);
			return;
		}

		if (hook_maxdamage->value >= hook_initdamage->value)
			T_Damage(other, self, self->owner, dir, self->s.origin, normal, hook_initdamage->value, hook_initdamage->value, DAMAGE_NONE, MOD_HOOK);

		self->owner->client->hook_damage += hook_initdamage->value;
	}
	else     // we hit something that's not a player
	{
		// if we can hurt it, then do a bit of damage
		if (other->takedamage) {
			dir = self->owner->s.origin - other->s.origin;
			normal = other->s.origin - self->owner->s.origin;
			T_Damage(other, self, self->owner, dir, self->s.origin, normal, hook_damage->value, hook_damage->value, DAMAGE_NONE, MOD_UNKNOWN);

			self->owner->client->hook_damage += hook_initdamage->value;
		}
		// stop moving
		self->velocity = vec3_origin;

		gi.positioned_sound(self->s.origin, self, CHAN_WEAPON, gi.soundindex("flyer/Flyatck2.wav"), 1, ATTN_NORM, 0);
	}

	// remember who/what we hit, must be set before Hook_Check() is called
	self->enemy = other;

	// if hook should be dropped, just return
	if (Hook_Check(self))
		return;

	// we are now anchored
	self->owner->client->hook_on = true;

	// keep up with that thing
	self->think = Hook_Track;
	self->nextthink = level.time + 100_ms;

	self->solid = SOLID_NOT;

	self->owner->hook_time = level.time;
}

// move the two ends of the laser beam to the proper positions
THINK(Hook_Think) (edict_t* self) -> void
{
	vec3_t forward, right, offset, start;
	vec3_t dir;   // Kyper - Lithium port - remove forward and right?

	if (!self) {
		gi.Com_Print("Hook_Think: self is nullptr\n");
		return;
	}
	if (!self->owner) {
		gi.Com_Print("Hook_Think: self->owner is nullptr\n");
		G_FreeEdict(self);
		return;
	}
	if (!self->owner->owner) {
		gi.Com_Print("Hook_Think: self->owner->owner is nullptr\n");
		G_FreeEdict(self);
		return;
	}
	if (!self->owner->owner->client) {
		gi.Com_Print("Hook_Think: self->owner->owner->client is nullptr\n");
		G_FreeEdict(self);
		return;
	}

	// put start position into start
	AngleVectors(self->owner->owner->client->v_angle, forward, right, nullptr);   // Kyper - Lithium port - remove forward and right?
	offset = vec3_t{ 24, 8, -8 };  // Kyper - Lithium port - changed "ent->viewheight - 8" to "-8" following example in p_weapon.cpp
	P_ProjectSource(self->owner->owner, self->owner->owner->client->v_angle, offset, start, dir);

	// move the two ends
	self->s.origin = start;

	// If hook is attached to a chainable entity, point beam at the entity instead of the hook
	if (self->owner->enemy && Hook_CanChainEntity(self->owner->enemy, self->owner->owner))
		self->s.old_origin = self->owner->enemy->s.origin;
	else
		self->s.old_origin = self->owner->s.origin;

	gi.linkentity(self);

	// set up to go again
	self->nextthink = level.time + FRAME_TIME_S;
}

// create a laser and return a pointer to it
edict_t* Hook_Start(edict_t* ent)
{
	edict_t* self;

	self = G_Spawn();
	self->movetype = MOVETYPE_NONE;
	self->solid = SOLID_NOT;
	self->s.renderfx |= RF_BEAM | RF_TRANSLUCENT;
	self->s.modelindex = 1;			// must be non-zero
	self->owner = ent;

	// set the beam diameter
	self->s.frame = 4;

	// set the color
	//self->s.skinnum = 0xf0f0f0f0;  // red
	//self->s.skinnum = 0xd0d1d2d3;  // green
	//self->s.skinnum = 0xe0e1e2e3;  // orange
	//self->s.skinnum = 0xdcdddedf;  // yellow
	//self->s.skinnum = 0xa0a1a2a3; // purple
	// self->s.skinnum = 0xb0b1b2b3;  // gray-white-?
	//self->s.skinnum = 0xD0D0D0D0;  // laser bfg 
	//self->s.skinnum = 0x30303030;  // cream spawngro
	self->s.skinnum = 0xd0d1d2d3; // shiny green

	self->think = Hook_Think;

	self->mins = vec3_t{ -8, -8, -8 };
	self->maxs = vec3_t{ 8, 8, 8 };
	gi.linkentity(self);

	self->spawnflags |= SPAWNFLAG_LASER_ZAP | SPAWNFLAG_LASER_ON;
	self->svflags &= ~SVF_NOCLIENT;
	Hook_Think(self);

	return self;
}

// creates the invisible hook entity and sends it on its way
// attaches a laser to it
void Hook_Fire(edict_t* owner, vec3_t start, vec3_t forward) {
	// Check if player is menu protected
	if (IsPlayerMenuProtected(owner)) {
		gi.LocClient_Print(owner, PRINT_HIGH, "You cannot use this while in a menu.\n");
		return;
	}

	edict_t* hook;
	trace_t tr;

	hook = G_Spawn();
	hook->movetype = MOVETYPE_FLYMISSILE;
	hook->solid = SOLID_BBOX;
	hook->clipmask = MASK_SHOT;
	hook->owner = owner;			// this hook belongs to me
	owner->client->hook = hook;		// this is my hook
	hook->classname = "hook";		// this is a hook

	hook->s.angles = vectoangles(forward);

	// Check if aiming at a chainable entity - if so, use super fast hook speed for instant grab
	trace_t aim_trace = gi.traceline(start, start + (forward * 8192.0f), owner, MASK_SHOT);
	bool aiming_at_chainable = (aim_trace.ent && Hook_CanChainEntity(aim_trace.ent, owner));

	hook->velocity = forward * (aiming_at_chainable ? 8000.0f : hook_speed->value);

	hook->touch = Hook_Touch;

	hook->think = G_FreeEdict;
	hook->nextthink = level.time + 5_sec;

	gi.setmodel(hook, "");

	hook->s.origin = start;
	hook->s.old_origin = hook->s.origin;

	hook->mins = { 0, 0, 0 };
	hook->maxs = { 0, 0, 0 };

	// start up the laser
	hook->laser = Hook_Start(hook);

	// put it in the world
	gi.linkentity(hook);

	// from id's code.   // Kyper - Lithium port - now from the remaster code!
	tr = gi.traceline(owner->s.origin, hook->s.origin, hook, MASK_SHOT);
	if (tr.fraction < 1.0f)
	{
		hook->s.origin = hook->s.origin + (forward * -10);
		hook->touch(hook, tr.ent, tr, false);
	}
}

// a call has been made to fire the hook
void Weapon_Hook_Fire(edict_t* ent)
{
	if (level.intermissiontime)
		return;

	// Check if player is menu protected
	if (IsPlayerMenuProtected(ent)) {
		gi.LocClient_Print(ent, PRINT_HIGH, "You cannot use this while in a menu.\n");
		return;
	}

	const bool using_grapple = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_GRAPPLE;
	if  (using_grapple)
	return; // Don't allow hook if grapple is equipped
	
	vec3_t forward, right;
	vec3_t start;
	vec3_t offset;
	vec3_t dir;      // Kyper - Lithium port - replaces forward?

	// Kyper - Lithium port - added an alternate "toggle" method
	// trying to be console friendly!
	if (ent->client->hook_out && !ent->client->hook_toggle)
	{
		return;
	}
	else if (ent->client->hook_out && ent->client->hook_toggle)
	{
		Hook_Reset(ent->client->hook);
		return;
	}

	// don't allow the client to fire the hook too rapidly
	if (level.time < gtime_t::from_sec(ent->client->last_hook_time) + gtime_t::from_sec(hook_delay->value))
		return;

	ent->client->last_hook_time = level.time.seconds();

	ent->client->hook_out = true;
	ent->client->hook_damage = 0;

	// calculate start position and forward direction
	AngleVectors(ent->client->v_angle, forward, right, nullptr);
	offset = vec3_t{ 24, 8, -8 };    // Kyper - Lithium port - changed "ent->viewheight - 8" to "-8" following example in p_weapon.cpp

	P_ProjectSource(ent, ent->client->v_angle, offset, start, dir);

	// kick back??
	P_AddWeaponKick(ent, ent->client->v_forward * -2, { -1.f, 0.f, 0.f });

	// actually launch the hook off
	Hook_Fire(ent, start, dir);

	gi.sound(ent, CHAN_WEAPON, gi.soundindex("flyer/Flyatck3.wav"), 1, ATTN_NORM, 0);

	PlayerNoise(ent, start, PNOISE_WEAPON);
}

// boring service routine
void Weapon_Hook(edict_t* ent)
{
	static int pause_frames[] = { 19, 32, 0 };
	static int fire_frames[] = { 5, 0 };

	Weapon_Generic(ent, 4, 8, 52, 55, pause_frames, fire_frames, Weapon_Hook_Fire);
}