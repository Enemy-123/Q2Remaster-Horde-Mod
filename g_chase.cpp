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

	// Auto eyecam: determine if we should temporarily switch to first-person
	bool force_eyecam = false;
	if (ent->client->auto_eyecam)
	{
		// Pre-calculate third-person camera position to check space
		vec3_t test_start, test_angles, test_forward, test_goal;

		test_start = targ->s.origin;
		if (targ->viewheight)
			test_start[2] += targ->viewheight;
		else
			test_start[2] = targ->absmax[2] - 8;
		test_start[2] += 16;

		test_angles = ent->client->resp.cmd_angles;
		if (test_angles[PITCH] > 56)
			test_angles[PITCH] = 56;
		AngleVectors(test_angles, test_forward, nullptr, nullptr);
		test_forward.normalize();
		test_start = test_start + (test_forward * (targ->mins[1] - 64));

		if (!targ->groundentity)
			test_start[2] += 16;

		trace_t test_trace = gi.traceline(targ->s.origin, test_start, targ, MASK_SOLID);
		test_goal = test_trace.endpos;

		// Calculate distance between camera and player
		vec3_t player_view = targ->s.origin;
		if (targ->viewheight)
			player_view[2] += targ->viewheight;
		else
			player_view[2] = targ->absmax[2] - 8;

		float cam_distance = (test_goal - player_view).length();

		// If camera is forced too close, switch to eyecam
		// Threshold: 40 units = very cramped space
		if (cam_distance < 40.0f)
			force_eyecam = true;
	}

	// Q2Eaks eyecam handling
	if (sv_eyecam->integer && (ent->client->use_eyecam || force_eyecam))
	{
		// Hide the chased player's model completely in first-person
		targ->svflags |= SVF_NOCLIENT;

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

		// Position camera at target's origin for true first-person view
		// Don't add viewheight here - we're copying ps.viewoffset which handles it
		angles = targ->client->v_angle;
		AngleVectors(angles, forward, right, nullptr);
		forward.normalize();

		// Use target's origin directly (viewoffset from ps handles eye position)
		goal = ownerv;
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

		// Always clear weapon model in third-person chasecam
		ent->client->ps.gunindex = 0;
		ent->client->ps.gunskin = 0;

		// Always show player model when in third-person
		targ->svflags &= ~SVF_NOCLIENT;
	}

	if (targ->deadflag)
		ent->client->ps.pmove.pm_type = PM_DEAD;
	else
	{
		// Eyecam: freeze position but allow mouse rotation for dynamic escape
		// Always use PM_SPECTATOR to allow mouse input (even in forced eyecam)
		ent->client->ps.pmove.pm_type = PM_SPECTATOR;
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
		if (sv_eyecam->integer && (ent->client->use_eyecam || force_eyecam))
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

	// Always allow mouse movement for dynamic eyecam escape (Vortex-style)
	// - Display: shows target's view in eyecam, spectator's view in third-person
	// - Input: spectator can always move mouse (even during forced eyecam)
	// - Next frame: auto_eyecam recalculates with new cmd_angles, may find clearance
	ent->client->ps.pmove.pm_flags |= PMF_NO_POSITIONAL_PREDICTION;
	ent->client->ps.pmove.pm_flags &= ~PMF_NO_ANGULAR_PREDICTION;

	// In eyecam mode: display target's view but allow spectator's mouse to update cmd_angles
	// This enables dynamic escape: spectator rotates mouse → next frame finds clearance → pops to third-person
	if (sv_eyecam->integer && (ent->client->use_eyecam || force_eyecam))
	{
		// Sync delta_angles to target so displayed view matches target
		ent->client->ps.pmove.delta_angles = targ->client->v_angle - ent->client->resp.cmd_angles;
	}
	else
	{
		// Third-person: delta_angles = 0 so view follows mouse directly
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