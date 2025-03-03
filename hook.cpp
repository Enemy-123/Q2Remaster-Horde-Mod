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
cvar_t* hook_speed;
cvar_t* hook_pullspeed;
cvar_t* hook_sky;
cvar_t* hook_maxtime;
cvar_t* hook_damage;
cvar_t* hook_initdamage;
cvar_t* hook_maxdamage;
cvar_t* hook_delay;

void Hook_InitGame(void)
{
	hook_speed = gi.cvar("hook_speed", "900", CVAR_NOFLAGS);
	hook_pullspeed = gi.cvar("hook_pullspeed", "700", CVAR_NOFLAGS);
	hook_sky = gi.cvar("hook_sky", "0", CVAR_NOFLAGS);
	hook_maxtime = gi.cvar("hook_maxtime", "5", CVAR_NOFLAGS);
	hook_damage = gi.cvar("hook_damage", "20", CVAR_NOFLAGS);
	hook_initdamage = gi.cvar("hook_initdamage", "10", CVAR_NOFLAGS);
	hook_maxdamage = gi.cvar("hook_maxdamage", "20", CVAR_NOFLAGS);
	hook_delay = gi.cvar("hook_delay", "0.2", CVAR_NOFLAGS);
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

	// start with NULL pointer checks
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
};

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
	if (self->enemy->client)
	{
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

	if (other->client && other->svflags & SVF_BOT) 		// we hit a bot	
	{
		// if bot, apply the same pull force as brain tongue attack
		AngleVectors(self->s.angles, forward, nullptr, nullptr);
		other->velocity = forward * -1550;  // Apply pulling force to the bot

		// Stop further processing as we already applied the force
		Hook_Reset(self);
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
		gi.Com_Print("Hook_Think: self is null\n");
		return;
	}
	if (!self->owner) {
		gi.Com_Print("Hook_Think: self->owner is null\n");
		G_FreeEdict(self);
		return;
	}
	if (!self->owner->owner) {
		gi.Com_Print("Hook_Think: self->owner->owner is null\n");
		G_FreeEdict(self);
		return;
	}
	if (!self->owner->owner->client) {
		gi.Com_Print("Hook_Think: self->owner->owner->client is null\n");
		G_FreeEdict(self);
		return;
	}

	// put start position into start
	AngleVectors(self->owner->owner->client->v_angle, forward, right, NULL);   // Kyper - Lithium port - remove forward and right?
	offset = vec3_t{ 24, 8, -8 };  // Kyper - Lithium port - changed "ent->viewheight - 8" to "-8" following example in p_weapon.cpp
	P_ProjectSource(self->owner->owner, self->owner->owner->client->v_angle, offset, start, dir);

	// move the two ends
	self->s.origin = start;
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
	self->s.skinnum = 0x30303030;  // cream spawngro

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
	hook->velocity = forward * hook_speed->value;

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
	AngleVectors(ent->client->v_angle, forward, right, NULL);
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