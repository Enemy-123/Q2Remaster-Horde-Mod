// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"

void UpdateChaseCam(edict_t* ent)
{
	vec3_t	 o, ownerv, goal;
	edict_t* targ;
	vec3_t	 forward, right;
	trace_t	 trace;
	vec3_t	 oldgoal;
	vec3_t	 angles;

	// is our chase target gone?
	if (!ent->client->chase_target->inuse || ent->client->chase_target->client->resp.spectator)
	{
		edict_t* old = ent->client->chase_target;
		ChaseNext(ent);
		if (ent->client->chase_target == old)
		{
			ent->client->chase_target = nullptr;
			ent->client->ps.pmove.pm_flags &= ~(PMF_NO_POSITIONAL_PREDICTION | PMF_NO_ANGULAR_PREDICTION);
			return;
		}
	}

	targ = ent->client->chase_target;

	ownerv = targ->s.origin;
	oldgoal = ent->s.origin;

	// Q2Eaks eyecam handling
	if (sv_eyecam->integer && ent->client->use_eyecam)
	{
		// mark the chased player as instanced so we can disable their model's visibility
		targ->svflags |= SVF_INSTANCED;

		// copy everything from ps but pmove, pov, stats, and team_id
		ent->client->ps.viewangles = targ->client->ps.viewangles;
		ent->client->ps.viewoffset = targ->client->ps.viewoffset;
		ent->client->ps.kick_angles = targ->client->ps.kick_angles;
		ent->client->ps.gunangles = targ->client->ps.gunangles;
		ent->client->ps.gunoffset = targ->client->ps.gunoffset;
		ent->client->ps.gunindex = targ->client->ps.gunindex;
		ent->client->ps.gunskin = targ->client->ps.gunskin;
		ent->client->ps.gunframe = targ->client->ps.gunframe;
		ent->client->ps.gunrate = targ->client->ps.gunrate;
		ent->client->ps.screen_blend = targ->client->ps.screen_blend;
		ent->client->ps.damage_blend = targ->client->ps.damage_blend;
		ent->client->ps.rdflags = targ->client->ps.rdflags;

		// do pmove stuff so view looks right, but not pm_flags
		ent->client->ps.pmove.origin = targ->client->ps.pmove.origin;
		ent->client->ps.pmove.velocity = targ->client->ps.pmove.velocity;
		ent->client->ps.pmove.pm_time = targ->client->ps.pmove.pm_time;
		ent->client->ps.pmove.gravity = targ->client->ps.pmove.gravity;
		ent->client->ps.pmove.delta_angles = targ->client->ps.pmove.delta_angles;
		ent->client->ps.pmove.viewheight = targ->client->ps.pmove.viewheight;

		ent->client->pers.hand = ent->client->chase_target->client->pers.hand;
		ent->client->pers.weapon = ent->client->chase_target->client->pers.weapon;

		// unadjusted view and origin handling
		angles = targ->client->v_angle;
		AngleVectors(angles, forward, right, nullptr);
		forward.normalize();
		o = ownerv;
		trace = gi.traceline(ownerv, o, targ, MASK_SOLID);
		goal = trace.endpos;
	}
	// vanilla chasecam code with Vortex-style pivot system
	else
	{
		vec3_t pivot, start;

		// Set up starting position at target's viewheight
		start = targ->s.origin;
		if (targ->viewheight)
			start[2] += targ->viewheight;
		else
			start[2] = targ->absmax[2] - 8;

		start[2] += 16; // Put camera a bit above target

		// Store pivot point for angle calculation
		pivot = start;

		// Get the spectator's requested viewing angles (from mouse input)
		// This is KEY: use spectator's cmd_angles, NOT target's angles!
		angles = ent->client->resp.cmd_angles;
		if (angles[PITCH] > 56)
			angles[PITCH] = 56;
		AngleVectors(angles, forward, right, nullptr);
		forward.normalize();

		// Position camera behind target (using pivot system from Vortex)
		// Vortex uses: VectorMA(start, (targ->mins[1]-64), forward, start)
		// Remaster equivalent: start = start + (forward * (targ->mins[1] - 64))
		start = start + (forward * (targ->mins[1] - 64));

		// jump animation lifts
		if (!targ->groundentity)
			start[2] += 16;

		// Primary trace from target origin to camera position
		trace = gi.traceline(targ->s.origin, start, targ, MASK_SOLID);
		goal = trace.endpos;

		// If we hit a wall, pull back using barrel_visualize technique
		if (trace.fraction < 1)
		{
			vec3_t pullback = trace.plane.normal * 12.0f;
			goal = goal + pullback;
		}

		// Pad for floors and ceilings (multi-trace collision)
		o = goal;
		o[2] += 6;
		trace = gi.traceline(goal, o, targ, MASK_SOLID);
		if (trace.fraction < 1)
		{
			goal = trace.endpos;
			goal[2] -= 6;
		}

		o = goal;
		o[2] -= 6;
		trace = gi.traceline(goal, o, targ, MASK_SOLID);
		if (trace.fraction < 1)
		{
			goal = trace.endpos;
			goal[2] += 6;
		}

		// Note: We keep 'angles' as the spectator's cmd_angles throughout
		// This allows the spectator to freely rotate the camera with mouse
		// The pivot system only affects camera POSITION, not view direction
	}

	if (targ->deadflag)
		ent->client->ps.pmove.pm_type = PM_DEAD;
	else
	{
		// Eyecam: freeze all input. Vanilla: allow mouse input with PM_SPECTATOR
		if (sv_eyecam->integer && ent->client->use_eyecam)
			ent->client->ps.pmove.pm_type = PM_FREEZE;
		else
			ent->client->ps.pmove.pm_type = PM_SPECTATOR;  // ✅ Allows mouse input!
	}

	ent->s.origin = goal;

	if (targ->deadflag)
	{
		ent->client->ps.viewangles[ROLL] = 40;
		ent->client->ps.viewangles[PITCH] = -15;
		ent->client->ps.viewangles[YAW] = targ->client->killer_yaw;
	}
	else
	{
		// Eyecam mode: use target's view angles directly
		if (sv_eyecam->integer && ent->client->use_eyecam)
		{
			ent->client->ps.viewangles = targ->client->v_angle;
			ent->client->v_angle = targ->client->v_angle;
		}
		else
		{
			// Vanilla chasecam: use spectator's commanded angles for free rotation
			// angles = cmd_angles from line 90, so spectator controls view with mouse
			ent->client->ps.viewangles = angles;
			ent->client->v_angle = angles;
		}
		AngleVectors(ent->client->v_angle, ent->client->v_forward, nullptr, nullptr);
	}

	ent->viewheight = 0;

	// For free camera rotation: allow angular prediction, block positional only
	// Eyecam mode locks both position and angles to target
	if (sv_eyecam->integer && ent->client->use_eyecam)
	{
		ent->client->ps.pmove.pm_flags |= PMF_NO_POSITIONAL_PREDICTION | PMF_NO_ANGULAR_PREDICTION;
		ent->client->ps.pmove.delta_angles = targ->client->v_angle - ent->client->resp.cmd_angles;
	}
	else
	{
		// Vanilla chasecam: spectator controls view angles with mouse
		ent->client->ps.pmove.pm_flags |= PMF_NO_POSITIONAL_PREDICTION;
		ent->client->ps.pmove.pm_flags &= ~PMF_NO_ANGULAR_PREDICTION;
		// Delta angles = 0 allows free rotation (viewangles = cmd_angles + 0)
		ent->client->ps.pmove.delta_angles = vec3_origin;
	}

	gi.linkentity(ent);
}

void ChaseNext(edict_t* ent)
{
	ptrdiff_t i;
	edict_t* e;

	if (!ent->client->chase_target)
		return;

	i = ent->client->chase_target - g_edicts;
	do
	{
		i++;
		if (i > game.maxclients)
			i = 1;
		e = g_edicts + i;
		if (!e->inuse)
			continue;
		if (!e->client->resp.spectator)
			break;
	} while (e != ent->client->chase_target);

	ent->client->chase_target = e;
	ent->client->update_chase = true;
}

void ChasePrev(edict_t* ent)
{
	int		 i;
	edict_t* e;

	if (!ent->client->chase_target)
		return;

	i = ent->client->chase_target - g_edicts;
	do
	{
		i--;
		if (i < 1)
			i = game.maxclients;
		e = g_edicts + i;
		if (!e->inuse)
			continue;
		if (!e->client->resp.spectator)
			break;
	} while (e != ent->client->chase_target);

	ent->client->chase_target = e;
	ent->client->update_chase = true;
}

void GetChaseTarget(edict_t* ent)
{
	uint32_t i;
	edict_t* other;

	for (i = 1; i <= game.maxclients; i++)
	{
		other = g_edicts + i;
		if (other->inuse && !other->client->resp.spectator)
		{
			ent->client->chase_target = other;
			ent->client->update_chase = true;
			UpdateChaseCam(ent);
			return;
		}
	}

	if (ent->client->chase_msg_time <= level.time)
	{
		gi.LocCenter_Print(ent, "$g_no_players_chase");
		ent->client->chase_msg_time = level.time + 5_sec;
	}
}