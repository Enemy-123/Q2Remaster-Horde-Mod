// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// m_move.c -- monster movement

#include "g_local.h"

// this is used for communications out of sv_movestep to say what entity
// is blocking us
edict_t* new_bad; // pmm

/*
=============
M_CheckBottom

Returns false if any part of the bottom of the entity is off an edge that
is not a staircase.

=============
*/
bool M_CheckBottom_Fast_Generic(const vec3_t& absmins, const vec3_t& absmaxs, bool ceiling)
{
	vec3_t start;
	start[2] = ceiling ? absmaxs[2] + 1 : absmins[2] - 1;

	// Usar una sola iteración con bit masking para probar las 4 esquinas
	for (int i = 0; i < 4; i++) {
		start[0] = (i & 1) ? absmaxs[0] : absmins[0];
		start[1] = (i & 2) ? absmaxs[1] : absmins[1];
		if (gi.pointcontents(start) != CONTENTS_SOLID)
			return false;
	}
	return true;
}


bool M_CheckBottom_Slow_Generic(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs,
	edict_t* ignore, contents_t mask, bool ceiling, bool allow_any_step_height)
{
	// Precalcular dimensiones para reutilización
	const vec3_t step_quadrant_size = (maxs - mins) * 0.5f;
	const vec3_t half_step_quadrant = step_quadrant_size * 0.5f;
	const vec3_t half_step_quadrant_mins = -half_step_quadrant;

	// Optimizar trazas iniciales
	vec3_t const start = origin;
	vec3_t stop = origin;
	stop.z = ceiling ?
		(origin.z + maxs.z + STEPSIZE * 2) :
		(origin.z + mins.z - STEPSIZE * 2);

	trace_t trace = gi.trace(start, mins, maxs, stop, ignore, mask);

	if (trace.fraction == 1.0f)
		return false;

	if (allow_any_step_height)
		return true;

	const float mid = trace.endpos[2];
	constexpr  float step_threshold = STEPSIZE;
	vec3_t quadrant_start, quadrant_end;

	// Optimizar bucle de verificación de esquinas
	for (int i = 0; i < 4; i++) {
		quadrant_start = start;
		quadrant_start.x += ((i & 1) ? half_step_quadrant.x : -half_step_quadrant.x);
		quadrant_start.y += ((i & 2) ? half_step_quadrant.y : -half_step_quadrant.y);
		quadrant_end = quadrant_start;
		quadrant_end.z = stop.z;

		trace = gi.trace(quadrant_start, half_step_quadrant_mins, half_step_quadrant,
			quadrant_end, ignore, mask);

		const float height_diff = ceiling ?
			trace.endpos[2] - mid :
			mid - trace.endpos[2];

		if (trace.fraction == 1.0f || height_diff > step_threshold)
			return false;
	}

	return true;
}

bool M_CheckBottom(edict_t* ent)
{
	// if all of the points under the corners are solid world, don't bother
	// with the tougher checks

	if (M_CheckBottom_Fast_Generic(ent->s.origin + ent->mins, ent->s.origin + ent->maxs, ent->gravityVector[2] > 0))
		return true; // we got out easy

	contents_t const mask = (ent->svflags & SVF_MONSTER) ? MASK_MONSTERSOLID : (MASK_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER);
	return M_CheckBottom_Slow_Generic(ent->s.origin, ent->mins, ent->maxs, ent, mask, ent->gravityVector[2] > 0, ent->spawnflags.has(SPAWNFLAG_MONSTER_SUPER_STEP));
}

//============
// ROGUE
bool IsBadAhead(edict_t* self, edict_t* bad, const vec3_t& move)
{
	vec3_t dir;
	vec3_t forward;
	float  dp_bad, dp_move;
	vec3_t move_copy;

	move_copy = move;

	dir = bad->s.origin - self->s.origin;
	dir.normalize();
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	dp_bad = forward.dot(dir);

	move_copy.normalize();
	AngleVectors(self->s.angles, forward, nullptr, nullptr);
	dp_move = forward.dot(move_copy);

	if ((dp_bad < 0) && (dp_move < 0))
		return true;
	if ((dp_bad > 0) && (dp_move > 0))
		return true;

	return false;
}

[[nodiscard]] vec3_t G_IdealHoverPosition(const edict_t* ent)
{
	constexpr uint32_t SKIP_FLAGS = AI_COMBAT_POINT | AI_SOUND_TARGET | AI_HINT_PATH | AI_PATHING;

	// Early return for default cases
	if ((!ent->enemy && !(ent->monsterinfo.aiflags & AI_MEDIC)) ||
		(ent->monsterinfo.aiflags & SKIP_FLAGS)) {
		return vec3_origin;
	}

	// Calculate random angle in horizontal plane
	const float theta = frandom(2 * PIf);

	// Calculate vertical angle based on entity type
	const float phi = ent->monsterinfo.fly_above ? acos(0.7f + frandom(0.3f)) :
		(ent->monsterinfo.fly_buzzard || (ent->monsterinfo.aiflags & AI_MEDIC)) ? acos(frandom()) :
		acos(frandom() * 0.7f);

	// Calculate direction vector
	const vec3_t direction{
		sin(phi) * cos(theta),
		sin(phi) * sin(theta),
		cos(phi)
	};

	// Scale direction by random distance within range
	return direction * frandom(ent->monsterinfo.fly_min_distance, ent->monsterinfo.fly_max_distance);
}

inline bool SV_flystep_testvisposition(vec3_t start, vec3_t end, vec3_t starta, vec3_t startb, edict_t* ent)
{
	trace_t tr = gi.traceline(start, end, ent, MASK_SOLID | CONTENTS_MONSTERCLIP);

	if (tr.fraction == 1.0f)
	{
		tr = gi.trace(starta, ent->mins, ent->maxs, startb, ent, MASK_SOLID | CONTENTS_MONSTERCLIP);

		if (tr.fraction == 1.0f)
			return true;
	}

	return false;
}

static bool SV_alternate_flystep(edict_t* ent, vec3_t move, bool relink, edict_t* current_bad)
{
	// swimming monsters just follow their velocity in the air
	if ((ent->flags & FL_SWIM) && ent->waterlevel < WATER_UNDER)
		return true;

	if (ent->monsterinfo.fly_position_time <= level.time ||
		(ent->enemy && ent->monsterinfo.fly_pinned && !visible(ent, ent->enemy)))
	{
		ent->monsterinfo.fly_pinned = false;
		ent->monsterinfo.fly_position_time = level.time + random_time(3_sec, 5_sec);
		ent->monsterinfo.fly_ideal_position = G_IdealHoverPosition(ent);
	}

	vec3_t towards_origin, towards_velocity = {};

	float current_speed;
	vec3_t dir = ent->velocity.normalized(current_speed);

	// FIXME
	if (isnan(dir[0]) || isnan(dir[1]) || isnan(dir[2]))
	{
#if defined(_DEBUG) && defined(_WIN32)
		__debugbreak();
#endif
		return false;
	}

	if (ent->monsterinfo.aiflags & AI_PATHING)
		towards_origin = (ent->monsterinfo.nav_path.returnCode == PathReturnCode::TraversalPending) ?
		ent->monsterinfo.nav_path.secondMovePoint : ent->monsterinfo.nav_path.firstMovePoint;
	else if (ent->enemy && !(ent->monsterinfo.aiflags & (AI_COMBAT_POINT | AI_SOUND_TARGET | AI_LOST_SIGHT)))
	{
		towards_origin = ent->enemy->s.origin;
		towards_velocity = ent->enemy->velocity;
	}
	else if (ent->goalentity)
		towards_origin = ent->goalentity->s.origin;
	else // what we're going towards probably died or something
	{
		// change speed
		if (current_speed)
		{
			if (current_speed > 0)
				current_speed = max(0.f, current_speed - ent->monsterinfo.fly_acceleration);
			else if (current_speed < 0)
				current_speed = min(0.f, current_speed + ent->monsterinfo.fly_acceleration);

			ent->velocity = dir * current_speed;
		}

		return true;
	}

	vec3_t wanted_pos;

	if (ent->monsterinfo.fly_pinned)
		wanted_pos = ent->monsterinfo.fly_ideal_position;
	else if (ent->monsterinfo.aiflags & (AI_PATHING | AI_COMBAT_POINT | AI_SOUND_TARGET | AI_LOST_SIGHT))
		wanted_pos = towards_origin;
	else
		wanted_pos = (towards_origin + (towards_velocity * 0.5f)) + ent->monsterinfo.fly_ideal_position;

	//gi.Draw_Point(wanted_pos, 8.0f, rgba_red, gi.frame_time_s, true);

	// find a place we can fit in from here
	trace_t tr = gi.trace(towards_origin, { -8.f, -8.f, -8.f }, { 8.f, 8.f, 8.f }, wanted_pos, ent, MASK_SOLID | CONTENTS_MONSTERCLIP);

	if (!tr.allsolid)
		wanted_pos = tr.endpos;

	float dist_to_wanted;
	vec3_t dest_diff = (wanted_pos - ent->s.origin);

	if (dest_diff.z > ent->mins.z && dest_diff.z < ent->maxs.z)
		dest_diff.z = 0;

	vec3_t wanted_dir = dest_diff.normalized(dist_to_wanted);

	if (!(ent->monsterinfo.aiflags & AI_MANUAL_STEERING))
		ent->ideal_yaw = vectoyaw((towards_origin - ent->s.origin).normalized());

	// check if we're blocked from moving this way from where we are
	tr = gi.trace(ent->s.origin, ent->mins, ent->maxs, ent->s.origin + (wanted_dir * ent->monsterinfo.fly_acceleration), ent, MASK_SOLID | CONTENTS_MONSTERCLIP);

	vec3_t aim_fwd, aim_rgt, aim_up;
	vec3_t const yaw_angles = { 0, ent->s.angles.y, 0 };

	AngleVectors(yaw_angles, aim_fwd, aim_rgt, aim_up);

	// it's a fairly close block, so we may want to shift more dramatically
	if (tr.fraction < 0.25f)
	{
		bool const bottom_visible = SV_flystep_testvisposition(ent->s.origin + vec3_t{ 0, 0, ent->mins.z }, wanted_pos,
			ent->s.origin, ent->s.origin + vec3_t{ 0, 0, ent->mins.z - ent->monsterinfo.fly_acceleration }, ent);
		bool const top_visible = SV_flystep_testvisposition(ent->s.origin + vec3_t{ 0, 0, ent->maxs.z }, wanted_pos,
			ent->s.origin, ent->s.origin + vec3_t{ 0, 0, ent->maxs.z + ent->monsterinfo.fly_acceleration }, ent);

		// top & bottom are same, so we need to try right/left
		if (bottom_visible == top_visible)
		{
			bool const left_visible = gi.traceline(ent->s.origin + aim_fwd.scaled(ent->maxs) - aim_rgt.scaled(ent->maxs), wanted_pos, ent, MASK_SOLID | CONTENTS_MONSTERCLIP).fraction == 1.0f;
			bool const right_visible = gi.traceline(ent->s.origin + aim_fwd.scaled(ent->maxs) + aim_rgt.scaled(ent->maxs), wanted_pos, ent, MASK_SOLID | CONTENTS_MONSTERCLIP).fraction == 1.0f;

			if (left_visible != right_visible)
			{
				if (right_visible)
					wanted_dir += aim_rgt;
				else
					wanted_dir -= aim_rgt;
			}
			else
				// we're probably stuck, push us directly away
				wanted_dir = tr.plane.normal;
		}
		else
		{
			if (top_visible)
				wanted_dir += aim_up;
			else
				wanted_dir -= aim_up;
		}

		wanted_dir.normalize();
	}

	// the closer we are to zero, the more we can change dir.
	// if we're pushed past our max speed we shouldn't
	// turn at all.
	bool const following_paths = ent->monsterinfo.aiflags & (AI_PATHING | AI_COMBAT_POINT | AI_LOST_SIGHT);
	float turn_factor;

	if (((ent->monsterinfo.fly_thrusters && !ent->monsterinfo.fly_pinned) || following_paths) && dir.dot(wanted_dir) > 0.0f)
		turn_factor = 0.45f;
	else
		turn_factor = min(1.f, 0.84f + (0.08f * (current_speed / ent->monsterinfo.fly_speed)));

	vec3_t final_dir = dir ? dir : wanted_dir;

	// FIXME
	if (isnan(final_dir[0]) || isnan(final_dir[1]) || isnan(final_dir[2]))
	{
#if defined(_DEBUG) && defined(_WIN32)
		__debugbreak();
#endif
		return false;
	}

	// swimming monsters don't exit water voluntarily, and
	// flying monsters don't enter water voluntarily (but will
	// try to leave it)
	bool bad_movement_direction = false;

	//if (!(ent->monsterinfo.aiflags & AI_COMBAT_POINT))
	{
		if (ent->flags & FL_SWIM)
			bad_movement_direction = !(gi.pointcontents(ent->s.origin + (wanted_dir * current_speed)) & CONTENTS_WATER);
		else if ((ent->flags & FL_FLY) && ent->waterlevel < WATER_UNDER)
			bad_movement_direction = gi.pointcontents(ent->s.origin + (wanted_dir * current_speed)) & CONTENTS_WATER;
	}

	if (bad_movement_direction)
	{
		if (ent->monsterinfo.fly_recovery_time < level.time)
		{
			ent->monsterinfo.fly_recovery_dir = vec3_t{ crandom(), crandom(), crandom() }.normalized();
			ent->monsterinfo.fly_recovery_time = level.time + 1_sec;
		}

		wanted_dir = ent->monsterinfo.fly_recovery_dir;
	}

	if (dir && turn_factor > 0)
		final_dir = slerp(dir, wanted_dir, 1.0f - turn_factor).normalized();

	// the closer we are to the wanted position, we want to slow
	// down so we don't fly past it.
	float speed_factor;

	//gi.Draw_Ray(ent->s.origin, aim_fwd, 16.0f, 8.0f, rgba_green, gi.frame_time_s, true);
	//gi.Draw_Ray(ent->s.origin, final_dir, 16.0f, 8.0f, rgba_blue, gi.frame_time_s, true);
	if (!ent->enemy || (ent->monsterinfo.fly_thrusters && !ent->monsterinfo.fly_pinned) || following_paths)
	{
		// Paril: only do this correction if we are following paths. we want to move backwards
		// away from players.
		if (following_paths && dir && wanted_dir.dot(dir) < -0.25)
			speed_factor = 0.f;
		else
			speed_factor = 1.f;
	}
	else
		speed_factor = min(1.f, dist_to_wanted / ent->monsterinfo.fly_speed);

	if (bad_movement_direction)
		speed_factor = -speed_factor;

	float accel = ent->monsterinfo.fly_acceleration;

	// if we're flying away from our destination, apply reverse thrusters
	if (final_dir.dot(wanted_dir) < 0.25f)
		accel *= 2.0f;

	float wanted_speed = ent->monsterinfo.fly_speed * speed_factor;

	if (ent->monsterinfo.aiflags & AI_MANUAL_STEERING)
		wanted_speed = 0;

	// change speed
	if (current_speed > wanted_speed)
		current_speed = max(wanted_speed, current_speed - accel);
	else if (current_speed < wanted_speed)
		current_speed = min(wanted_speed, current_speed + accel);

	// FIXME
	if (isnan(final_dir[0]) || isnan(final_dir[1]) || isnan(final_dir[2]) ||
		isnan(current_speed))
	{
#if defined(_DEBUG) && defined(_WIN32)
		__debugbreak();
#endif
		return false;
	}

	// commit
	ent->velocity = final_dir * current_speed;

	// for buzzards, set their pitch
	if (ent->enemy && (ent->monsterinfo.fly_buzzard || (ent->monsterinfo.aiflags & AI_MEDIC)))
	{
		vec3_t d = (ent->s.origin - towards_origin).normalized();
		d = vectoangles(d);
		ent->s.angles[PITCH] = LerpAngle(ent->s.angles[PITCH], -d[PITCH], gi.frame_time_s * 4.0f);
	}
	else
		ent->s.angles[PITCH] = 0;

	return true;
}

// flying monsters don't step up
static bool SV_flystep(edict_t* ent, vec3_t move, bool relink, edict_t* current_bad)
{
	if (ent->monsterinfo.aiflags & AI_ALTERNATE_FLY)
	{
		if (SV_alternate_flystep(ent, move, relink, current_bad))
			return true;
	}

	// try the move
	vec3_t const oldorg = ent->s.origin;
	vec3_t neworg = ent->s.origin + move;

	// fixme: move to monsterinfo
	// we want the carrier to stay a certain distance off the ground, to help prevent him
	// from shooting his fliers, who spawn in below him
	float minheight;

	if (!strcmp(ent->classname, "monster_carrier") || !strcmp(ent->classname, "monster_carrier_mini"))
		minheight = 104;
	else
		minheight = 40;

	// try one move with vertical motion, then one without
	for (int i = 0; i < 2; i++)
	{
		vec3_t new_move = move;

		if (i == 0 && ent->enemy)
		{
			if (!ent->goalentity)
				ent->goalentity = ent->enemy;

			vec3_t& goal_position = (ent->monsterinfo.aiflags & AI_PATHING) ? ent->monsterinfo.nav_path.firstMovePoint : ent->goalentity->s.origin;

			float const dz = ent->s.origin[2] - goal_position[2];
			float dist = move.length();

			if (ent->goalentity->client)
			{
				if (dz > minheight)
				{
					//	pmm
					new_move *= 0.5f;
					new_move[2] -= dist;
				}
				if (!((ent->flags & FL_SWIM) && (ent->waterlevel < WATER_WAIST)))
					if (dz < (minheight - 10))
					{
						new_move *= 0.5f;
						new_move[2] += dist;
					}
			}
			else
			{
				// RAFAEL
				//if (strcmp(ent->classname, "monster_fixbot") == 0)
				//{
				//	if (ent->s.frame >= 105 && ent->s.frame <= 120)
				//	{
				//		if (dz > 12)
				//			new_move[2]--;
				//		else if (dz < -12)
				//			new_move[2]++;
				//	}
				//	else if (ent->s.frame >= 31 && ent->s.frame <= 88)
				//	{
				//		if (dz > 12)
				//			new_move[2] -= 12;
				//		else if (dz < -12)
				//			new_move[2] += 12;
				//	}
				//	else
				//	{
				//		if (dz > 12)
				//			new_move[2] -= 8;
				//		else if (dz < -12)
				//			new_move[2] += 8;
				//	}
				//}
				//else
				{
					// RAFAEL
					if (dz > 0)
					{
						new_move *= 0.5f;
						new_move[2] -= min(dist, dz);
					}
					else if (dz < 0)
					{
						new_move *= 0.5f;
						new_move[2] += -max(-dist, dz);
					}
					// RAFAEL
				}
				// RAFAEL
			}
		}

		neworg = ent->s.origin + new_move;

		trace_t trace = gi.trace(ent->s.origin, ent->mins, ent->maxs, neworg, ent, MASK_MONSTERSOLID);

		// fly monsters don't enter water voluntarily
		if (ent->flags & FL_FLY)
		{
			if (!ent->waterlevel)
			{
				vec3_t  const test{ trace.endpos[0], trace.endpos[1], trace.endpos[2] + ent->mins[2] + 1 };
				contents_t const contents = gi.pointcontents(test);
				if (contents & MASK_WATER)
					return false;
			}
		}

		// swim monsters don't exit water voluntarily
		if (ent->flags & FL_SWIM)
		{
			if (ent->waterlevel < WATER_WAIST)
			{
				vec3_t const test{ trace.endpos[0], trace.endpos[1], trace.endpos[2] + ent->mins[2] + 1 };
				contents_t const contents = gi.pointcontents(test);
				if (!(contents & MASK_WATER))
					return false;
			}
		}

		// ROGUE
		if ((trace.fraction == 1) && (!trace.allsolid) && (!trace.startsolid))
			// ROGUE
		{
			ent->s.origin = trace.endpos;
			//=====
			// PGM
			if (!current_bad && CheckForBadArea(ent))
				ent->s.origin = oldorg;
			else
			{
				if (relink)
				{
					gi.linkentity(ent);
					G_TouchTriggers(ent);
				}

				return true;
			}
			// PGM
			//=====
		}

		G_Impact(ent, trace);

		if (!ent->enemy)
			break;
	}

	return false;
}

/*
=============
SV_movestep

Called by monster program code.
The move will be adjusted for slopes and stairs, but if the move isn't
possible, no move is done, false is returned, and
pr_global_struct->trace_normal is set to the normal of the blocking wall
=============
*/
// FIXME since we need to test end position contents here, can we avoid doing
// it again later in catagorize position?
bool SV_movestep(edict_t* ent, vec3_t move, bool relink)
{
	//======
	// PGM
	edict_t* current_bad = nullptr;

	// PMM - who cares about bad areas if you're dead?
	if (ent->health > 0)
	{
		current_bad = CheckForBadArea(ent);
		if (current_bad)
		{
			ent->bad_area = current_bad;

			if (ent->enemy && !strcmp(ent->enemy->classname, "tesla_mine"))
			{
				// if the tesla is in front of us, back up...
				if (IsBadAhead(ent, current_bad, move))
					move *= -1;
			}
		}
		else if (ent->bad_area)
		{
			// if we're no longer in a bad area, get back to business.
			ent->bad_area = nullptr;
			if (ent->oldenemy) // && ent->bad_area->owner == ent->enemy)
			{
				ent->enemy = ent->oldenemy;
				ent->goalentity = ent->oldenemy;
				FoundTarget(ent);
			}
		}
	}
	// PGM
	//======

	// flying monsters don't step up
	if (ent->flags & (FL_SWIM | FL_FLY))
		return SV_flystep(ent, move, relink, current_bad);

	// try the move
	vec3_t const oldorg = ent->s.origin;

	float stepsize;

	// push down from a step height above the wished position
	if (ent->spawnflags.has(SPAWNFLAG_MONSTER_SUPER_STEP) && ent->health > 0)
		stepsize = 64.f;
	else if (!(ent->monsterinfo.aiflags & AI_NOSTEP))
		stepsize = STEPSIZE;
	else
		stepsize = 1;

	stepsize += 0.75f;

	contents_t mask = (ent->svflags & SVF_MONSTER) ? MASK_MONSTERSOLID : (MASK_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER);

	// Paril: horde
	if ((g_horde->integer && strcmp(ent->classname, "monster_sentrygun")) || (ent->svflags & SVF_PLAYER && EntIsSpectating(ent)))
		mask &= ~CONTENTS_MONSTER;

	vec3_t start_up = oldorg + ent->gravityVector * (-1 * stepsize);

	start_up = gi.trace(oldorg, ent->mins, ent->maxs, start_up, ent, mask).endpos;

	vec3_t const end_up = start_up + move;

	trace_t up_trace = gi.trace(start_up, ent->mins, ent->maxs, end_up, ent, mask);

	if (up_trace.startsolid)
	{
		start_up += ent->gravityVector * (-1 * stepsize);
		up_trace = gi.trace(start_up, ent->mins, ent->maxs, end_up, ent, mask);
	}

	vec3_t const start_fwd = oldorg;
	vec3_t const end_fwd = start_fwd + move;

	trace_t fwd_trace = gi.trace(start_fwd, ent->mins, ent->maxs, end_fwd, ent, mask);

	if (fwd_trace.startsolid)
	{
		start_up += ent->gravityVector * (-1 * stepsize);
		fwd_trace = gi.trace(start_fwd, ent->mins, ent->maxs, end_fwd, ent, mask);
	}

	// pick the one that went farther
	trace_t& chosen_forward = (up_trace.fraction > fwd_trace.fraction) ? up_trace : fwd_trace;

	if (chosen_forward.startsolid || chosen_forward.allsolid)
		return false;

	int32_t steps = 1;
	bool stepped = false;

	if (up_trace.fraction > fwd_trace.fraction)
		steps = 2;

	// step us down
	vec3_t const end = chosen_forward.endpos + (ent->gravityVector * (steps * stepsize));
	trace_t trace = gi.trace(chosen_forward.endpos, ent->mins, ent->maxs, end, ent, mask);

	if (fabsf(ent->s.origin.z - trace.endpos.z) > 8.f)
		stepped = true;

	// Paril: improved the water handling here.
	// monsters are okay with stepping into water
	// up to their waist.
	if (ent->waterlevel <= WATER_WAIST)
	{
		water_level_t end_waterlevel;
		contents_t	  end_watertype;
		M_CatagorizePosition(ent, trace.endpos, end_waterlevel, end_watertype);

		// don't go into deep liquids or
		// slime/lava voluntarily
		if (end_watertype & (CONTENTS_SLIME | CONTENTS_LAVA) ||
			end_waterlevel > WATER_WAIST)
			return false;
	}

	if (trace.fraction == 1)
	{
		// if monster had the ground pulled out, go ahead and fall
		if (ent->flags & FL_PARTIALGROUND)
		{
			ent->s.origin += move;
			if (relink)
			{
				gi.linkentity(ent);
				if (!g_horde->integer) // Paril
				G_TouchTriggers(ent);
			}
			ent->groundentity = nullptr;
			return true;
		}
		// [Paril-KEX] allow dead monsters to "fall" off of edges in their death animation
		else if (!ent->spawnflags.has(SPAWNFLAG_MONSTER_SUPER_STEP) && ent->health > 0)
			return false; // walked off an edge
	}

	// [Paril-KEX] if we didn't move at all (or barely moved), don't count it
	if ((trace.endpos - oldorg).length() < move.length() * 0.05f)
	{
		ent->monsterinfo.bad_move_time = level.time + 1000_ms;

		if (ent->monsterinfo.bump_time < level.time && chosen_forward.fraction < 1.0f)
		{
			// adjust ideal_yaw to move against the object we hit and try again
			vec3_t const dir = SlideClipVelocity(AngleVectors(vec3_t{ 0.f, ent->ideal_yaw, 0.f }).forward, chosen_forward.plane.normal, 1.0f);
			float const new_yaw = vectoyaw(dir);

			if (dir.lengthSquared() > 0.1f && ent->ideal_yaw != new_yaw)
			{
				ent->ideal_yaw = new_yaw;
				ent->monsterinfo.random_change_time = level.time + 100_ms;
				ent->monsterinfo.bump_time = level.time + 200_ms;
				return true;
			}
		}

		return false;
	}

	// check point traces down for dangling corners
	ent->s.origin = trace.endpos;

	// PGM
	//  PMM - don't bother with bad areas if we're dead
	if (ent->health > 0)
	{
		// use AI_BLOCKED to tell the calling layer that we're now mad at a tesla or sentrygun
		new_bad = CheckForBadArea(ent);
		if (!current_bad && new_bad)
		{
			if (new_bad->owner)
			{
				if (!strcmp(new_bad->owner->classname, "tesla_mine") || !strcmp(new_bad->owner->classname, "monster_sentrygun"))
				{
					if ((!(ent->enemy)) || (!(ent->enemy->inuse)))
					{
						TargetTesla(ent, new_bad->owner);
						ent->monsterinfo.aiflags |= AI_BLOCKED;
					}
					else if (!strcmp(ent->enemy->classname, "tesla_mine") || !strcmp(ent->enemy->classname, "monster_sentrygun"))
					{
						// already targeting a tesla or sentrygun, do nothing
					}
					else if ((ent->enemy) && (ent->enemy->client))
					{
						if (!visible(ent, ent->enemy))
						{
							TargetTesla(ent, new_bad->owner);
							ent->monsterinfo.aiflags |= AI_BLOCKED;
						}
					}
					else
					{
						TargetTesla(ent, new_bad->owner);
						ent->monsterinfo.aiflags |= AI_BLOCKED;
					}
				}
			}

			ent->s.origin = oldorg;
			return false;
		}
	}

	// PGM

	if (!M_CheckBottom(ent))
	{
		if (ent->flags & FL_PARTIALGROUND)
		{ // entity had floor mostly pulled out from underneath it
			// and is trying to correct
			if (relink)
			{
				gi.linkentity(ent);
				if (!g_horde->integer) // Paril
				G_TouchTriggers(ent);
			}
			return true;
		}

		// walked off an edge that wasn't a stairway
		ent->s.origin = oldorg;
		return false;
	}

	if (ent->spawnflags.has(SPAWNFLAG_MONSTER_SUPER_STEP) && ent->health > 0)
	{
		if (!ent->groundentity || ent->groundentity->solid == SOLID_BSP)
		{
			if (!(trace.ent->solid == SOLID_BSP))
			{
				// walked off an edge
				ent->s.origin = oldorg;
				M_CheckGround(ent, G_GetClipMask(ent));
				return false;
			}
		}
	}

	// [Paril-KEX]
	M_CheckGround(ent, G_GetClipMask(ent));

	if (!ent->groundentity)
	{
		// walked off an edge
		ent->s.origin = oldorg;
		M_CheckGround(ent, G_GetClipMask(ent));
		return false;
	}

	if (ent->flags & FL_PARTIALGROUND)
	{
		ent->flags &= ~FL_PARTIALGROUND;
	}
	ent->groundentity = trace.ent;
	ent->groundentity_linkcount = trace.ent->linkcount;

	// the move is ok
	if (relink)
	{
		gi.linkentity(ent);

		// [Paril-KEX] this is something N64 does to avoid doors opening
		// at the start of a level, which triggers some monsters to spawn.
		if (!g_horde->integer) // Paril
		if (!level.is_n64 || level.time > FRAME_TIME_S)
			G_TouchTriggers(ent);
	}

	if (stepped)
		ent->s.renderfx |= RF_STAIR_STEP;

	if (trace.fraction < 1.f)
		G_Impact(ent, trace);

	return true;
}

// check if a movement would succeed
bool ai_check_move(edict_t* self, float dist)
{
	if (ai_movement_disabled->integer) {
		return false;
	}

	float const yaw = self->s.angles[YAW] * PIf * 2 / 360;
	vec3_t const move = {
		cosf(yaw) * dist,
		sinf(yaw) * dist,
		0
	};

	vec3_t const old_origin = self->s.origin;

	if (!SV_movestep(self, move, false))
		return false;

	self->s.origin = old_origin;
	gi.linkentity(self);
	return true;
}

//============================================================================

/*
===============
M_ChangeYaw

===============
*/
void M_ChangeYaw(edict_t* ent)
{
	float ideal;
	float current;
	float move;
	float speed;

	current = anglemod(ent->s.angles[YAW]);
	ideal = ent->ideal_yaw;

	if (current == ideal)
		return;

	move = ideal - current;
	// [Paril-KEX] high tick rate
	speed = ent->yaw_speed / (gi.tick_rate / 10);

	if (ideal > current)
	{
		if (move >= 180)
			move = move - 360;
	}
	else
	{
		if (move <= -180)
			move = move + 360;
	}
	if (move > 0)
	{
		if (move > speed)
			move = speed;
	}
	else
	{
		if (move < -speed)
			move = -speed;
	}

	ent->s.angles[YAW] = anglemod(current + move);
}

/*
======================
SV_StepDirection
Turns to the movement direction, and walks the current distance if facing it.
Returns true if the movement was successful.
======================
*/
bool SV_StepDirection(edict_t* ent, float yaw, float dist, bool allow_no_turns)
{
	if (!ent || !ent->inuse || dist <= 0)
		return false;

	// Store original values for potential rollback
	const vec3_t oldorigin = ent->s.origin;
	const float old_ideal_yaw = ent->ideal_yaw;
	const float old_current_yaw = ent->s.angles[YAW];

	// Update entity yaw
	ent->ideal_yaw = yaw;
	M_ChangeYaw(ent);

	// Calculate movement vector
	const float rad_yaw = DEG2RAD(yaw);
	vec3_t const move = {
		cosf(rad_yaw) * dist,
		sinf(rad_yaw) * dist,
		0
	};

	// Special handling for specific monster types
	const bool is_widow = !strncmp(ent->classname, "monster_widow", 13) ||
		!strncmp(ent->classname, "monster_widow1", 13);

	// Attempt movement
	if (SV_movestep(ent, move, false))
	{
		// Clear blocked flag on successful movement
		ent->monsterinfo.aiflags &= ~AI_BLOCKED;

		// Safety check for entity still being valid
		if (!ent->inuse)
			return true;

		// Check if entity is facing the right direction
		if (!is_widow && !FacingIdeal(ent))
		{
			// Revert position but maintain rotation if allowed
			ent->s.origin = oldorigin;
			M_CheckGround(ent, G_GetClipMask(ent));
			return allow_no_turns;
		}

		// Update entity state and check triggers
		gi.linkentity(ent);

		if (!g_horde->integer)
		{
			G_TouchTriggers(ent);
		}

		G_TouchProjectiles(ent, oldorigin);
		return true;
	}

	// Movement failed, restore original rotation
	ent->ideal_yaw = old_ideal_yaw;
	ent->s.angles[YAW] = old_current_yaw;

	// Update entity state and check triggers even on failure
	gi.linkentity(ent);

	if (!g_horde->integer)
	{
		G_TouchTriggers(ent);
	}

	return false;
}
/*
======================
SV_FixCheckBottom

======================
*/
void SV_FixCheckBottom(edict_t* ent)
{
	ent->flags |= FL_PARTIALGROUND;
}

/*
================
SV_NewChaseDir

================
*/
constexpr float DI_NODIR = -1;

/*
================
SV_NewChaseDir

Enhanced pathfinding that maintains stability while improving
movement options and intelligence.
================
*/
bool SV_NewChaseDir(edict_t* actor, vec3_t pos, float dist)
{
	if (!actor || !actor->inuse || dist <= 0)
		return false;

	// Calculate current direction and turnaround
	const float olddir = anglemod(truncf(actor->ideal_yaw / 45) * 45);
	const float turnaround = anglemod(olddir - 180);

	// Calculate delta to target
	const vec3_t delta = pos - actor->s.origin;
	const float dx = delta.x;
	const float dy = delta.y;

	// Determine primary movement directions with 8-way movement
	const float tdir_x = (dx > 10.0f) ? 0.0f : (dx < -10.0f) ? 180.0f : DI_NODIR;
	const float tdir_y = (dy < -10.0f) ? 270.0f : (dy > 10.0f) ? 90.0f : DI_NODIR;

	// Try diagonal movement first for more natural paths
	if (tdir_x != DI_NODIR && tdir_y != DI_NODIR)
	{
		const float diagonal_dir = (tdir_x == 0.0f) ?
			(tdir_y == 90.0f ? 45.0f : 315.0f) :
			(tdir_y == 90.0f ? 135.0f : 225.0f);

		if (diagonal_dir != turnaround && SV_StepDirection(actor, diagonal_dir, dist, false))
			return true;
	}

	// Determine movement priority based on situation
	const bool try_vertical_first = (brandom() || fabsf(dy) > fabsf(dx));
	const float primary_dir = try_vertical_first ? tdir_y : tdir_x;
	const float secondary_dir = try_vertical_first ? tdir_x : tdir_y;

	// Try primary direction
	if (primary_dir != DI_NODIR && primary_dir != turnaround)
		if (SV_StepDirection(actor, primary_dir, dist, false))
			return true;

	// Try secondary direction
	if (secondary_dir != DI_NODIR && secondary_dir != turnaround)
		if (SV_StepDirection(actor, secondary_dir, dist, false))
			return true;

	// Handle specific monster blocked behavior
	if (actor->monsterinfo.blocked && actor->health > 0 &&
		!(actor->monsterinfo.aiflags & AI_TARGET_ANGER))
	{
		// Try blocked behavior first
		if (actor->monsterinfo.blocked(actor, dist))
		{
			actor->monsterinfo.move_block_counter = -2;
			return true;
		}

		// Consider switching to node navigation
		const bool can_use_pathing = !(actor->monsterinfo.aiflags &
			(AI_LOST_SIGHT | AI_COMBAT_POINT | AI_TARGET_ANGER |
				AI_PATHING | AI_TEMP_MELEE_COMBAT | AI_NO_PATH_FINDING));

		if (can_use_pathing && ++actor->monsterinfo.move_block_counter > 2)
		{
			actor->monsterinfo.aiflags |= AI_TEMP_MELEE_COMBAT;
			actor->monsterinfo.move_block_change_time = level.time + 3_sec;
			actor->monsterinfo.move_block_counter = 0;
		}
	}

	// Try previous direction
	if (olddir != DI_NODIR && SV_StepDirection(actor, olddir, dist, false))
		return true;

	// Try alternating between clockwise and counter-clockwise search
	const bool search_clockwise = brandom();
	const float angle_start = search_clockwise ? 0.0f : 315.0f;
	const float angle_end = search_clockwise ? 315.0f : 0.0f;
	const float angle_step = search_clockwise ? 45.0f : -45.0f;

	for (float test_dir = angle_start;
		search_clockwise ? (test_dir <= angle_end) : (test_dir >= angle_end);
		test_dir += angle_step)
	{
		if (test_dir != turnaround && SV_StepDirection(actor, test_dir, dist, false))
			return true;
	}

	// Last resort: try turning around
	if (turnaround != DI_NODIR && SV_StepDirection(actor, turnaround, dist, false))
		return true;

	// If all movement attempts failed, try a random direction
	actor->ideal_yaw = frandom(0, 360);

	// Check and fix ground position if needed
	if (!M_CheckBottom(actor))
		SV_FixCheckBottom(actor);

	return false;
}
/*
======================
SV_CloseEnough

======================
*/
bool SV_CloseEnough(edict_t* ent, edict_t* goal, float dist)
{
	// Verifica si ent o goal son nulos
	if (!ent || !goal)
		return false;
	int i;

	for (i = 0; i < 3; i++)
	{
		if (goal->absmin[i] > ent->absmax[i] + dist)
			return false;
		if (goal->absmax[i] < ent->absmin[i] - dist)
			return false;
	}
	return true;
}

static bool M_NavPathToGoal(edict_t* self, float dist, const vec3_t& goal)
{
	if (!self || dist <= 0)
		return false;

	// mark us as *trying* now (nav_pos is valid)
	self->monsterinfo.aiflags |= AI_PATHING;

	vec3_t& path_to = (self->monsterinfo.nav_path.returnCode == PathReturnCode::TraversalPending) ?
		self->monsterinfo.nav_path.secondMovePoint : self->monsterinfo.nav_path.firstMovePoint;

	// Calculate monster's actual dimensions and collision bounds
	const float height = self->maxs[2] - self->mins[2];
	const float width = std::max(self->maxs[0] - self->mins[0], self->maxs[1] - self->mins[1]);
	const float ground_offset = self->mins[2] - PLAYER_MINS[2];

	vec3_t const ground_origin = self->s.origin + vec3_t{ 0.f, 0.f, ground_offset };
	vec3_t const mon_mins = ground_origin + PLAYER_MINS;
	vec3_t const mon_maxs = ground_origin + PLAYER_MAXS;

	// Check if we need to recalculate path
	const bool path_expired = self->monsterinfo.nav_path_cache_time <= level.time;
	const bool path_intersecting = self->monsterinfo.nav_path.returnCode != PathReturnCode::TraversalPending &&
		boxes_intersect(mon_mins, mon_maxs, path_to, path_to);

	if (path_expired || path_intersecting)
	{
		PathRequest request;

		// Set goal based on enemy or goalentity
		request.goal = self->enemy ? self->enemy->s.origin : self->goalentity->s.origin;
		request.moveDist = dist;
		request.start = self->s.origin;
		request.pathFlags = PathFlags::Walk;

		// Set node search parameters based on actual monster dimensions
		request.nodeSearch.minHeight = -(height * 1.5f); // Extra margin for slopes/stairs
		request.nodeSearch.maxHeight = height * 2;       // Extra height for jumps/drops

		// Debug drawing if enabled
		if (g_debug_monster_paths->integer == 1)
			request.debugging.drawTime = gi.frame_time_s;

		// Special handling for specific monster types
		// Consider using monster dimensions for radius calculation
		if (!strcmp(self->classname, "monster_guardian") || !strcmp(self->classname, "monster_psxguardian"))
		{
			request.nodeSearch.radius = std::max(2048.f, width * 4); // Use width to inform radius
		}

		// Configure movement capabilities
		if (self->monsterinfo.can_jump || (self->flags & FL_FLY))
		{
			if (self->monsterinfo.jump_height)
			{
				request.pathFlags |= PathFlags::BarrierJump;
				// Scale jump height based on monster size
				request.traversals.jumpHeight = std::min(self->monsterinfo.jump_height, height * 3);
			}
			if (self->monsterinfo.drop_height)
			{
				request.pathFlags |= PathFlags::WalkOffLedge;
				// Scale drop height based on monster size
				request.traversals.dropHeight = std::min(self->monsterinfo.drop_height, height * 4);
			}
		}

		// Flying monsters get special treatment
		if (self->flags & FL_FLY)
		{
			// Use monster's actual height for vertical limits
			const float vertical_limit = std::max(8192.f, height * 8);
			request.nodeSearch.maxHeight = request.nodeSearch.minHeight = vertical_limit;
			request.pathFlags |= PathFlags::LongJump;
		}

		if (!gi.GetPathToGoal(request, self->monsterinfo.nav_path))
		{
			// fatal error, don't bother ever trying nodes
			if (self->monsterinfo.nav_path.returnCode == PathReturnCode::NoNavAvailable)
				self->monsterinfo.aiflags |= AI_NO_PATH_FINDING;
			return false;
		}

		self->monsterinfo.nav_path_cache_time = level.time + 2_sec;
	}

	// Store original yaw values for potential restoration
	float yaw;
	const float old_yaw = self->s.angles[YAW];
	const float old_ideal_yaw = self->ideal_yaw;

	// Calculate movement direction
	if (self->monsterinfo.random_change_time >= level.time &&
		!(self->monsterinfo.aiflags & AI_ALTERNATE_FLY))
	{
		yaw = self->ideal_yaw;
	}
	else
	{
		vec3_t dir = path_to - self->s.origin;
		dir.normalize();
		yaw = vectoyaw(dir);
	}

	// Try primary movement
	if (!SV_StepDirection(self, yaw, dist, true))
	{
		if (!self->inuse)
			return false;

		// Handle blocked state
		if (self->monsterinfo.blocked && !(self->monsterinfo.aiflags & AI_TARGET_ANGER))
		{
			if ((self->inuse) && (self->health > 0))
			{
				// Restore original yaw for blocked handling
				self->s.angles[YAW] = old_yaw;
				self->ideal_yaw = old_ideal_yaw;
				if (self->monsterinfo.blocked(self, dist))
					return true;
			}
		}

		// Try movement to first point
		if (self->monsterinfo.random_change_time >= level.time)
			yaw = self->ideal_yaw;
		else
		{
			vec3_t dir = self->monsterinfo.nav_path.firstMovePoint - self->s.origin;
			dir.normalize();
			yaw = vectoyaw(dir);
		}

		if (!SV_StepDirection(self, yaw, dist, true))
		{
			// Handle blocked flag
			if (self->monsterinfo.aiflags & AI_BLOCKED)
			{
				self->monsterinfo.aiflags &= ~AI_BLOCKED;
				return true;
			}

			// Try random direction change
			if (self->monsterinfo.random_change_time < level.time && self->inuse)
			{
				self->monsterinfo.random_change_time = level.time + 1500_ms;
				if (SV_NewChaseDir(self, path_to, dist))
					return true;
			}

			// Update blocked counter
			self->monsterinfo.path_blocked_counter += FRAME_TIME_S * 3;

			// Check if we've been blocked too long
			if (self->monsterinfo.path_blocked_counter > 1.5_sec)
				return false;
		}
	}

	return true;
}
/*
=============
M_MoveToPath

Advanced movement code that use the bots pathfinder if allowed and conditions are right.
Feel free to add any other conditions needed.
=============
*/
static bool M_MoveToPath(edict_t* self, float dist)
{
	// 1. Mejorar las condiciones iniciales con comprobaciones más precisas
	if (self->flags & FL_STATIONARY)
		return false;
	else if (self->monsterinfo.aiflags & AI_NO_PATH_FINDING)
		return false;
	else if (self->monsterinfo.path_wait_time > level.time)
		return false;
	else if (!self->enemy || !self->enemy->inuse)  // Añadir check de inuse
		return false;
	else if (self->enemy->client &&
		self->enemy->client->invisible_time > level.time &&
		self->enemy->client->invisibility_fade_time <= level.time)
		return false;
	else if (self->monsterinfo.attack_state >= AS_MISSILE)
		return true;

	combat_style_t style = self->monsterinfo.combat_style;
	if (self->monsterinfo.aiflags & AI_TEMP_MELEE_COMBAT)
		style = COMBAT_MELEE;

	// 2. Mejorar la lógica de visibilidad y rango
	if (visible(self, self->enemy, false)) {
		float const dist_to_enemy = range_to(self, self->enemy);
		float const height_diff = fabs(self->s.origin.z - self->enemy->s.origin.z);
		float const max_step_height = max(self->maxs.z, -self->mins.z);

		if ((self->flags & (FL_SWIM | FL_FLY)) || style == COMBAT_RANGED) {
			return false;  // Mantener comportamiento normal para voladores/nadadores
		}
		else if (style == COMBAT_MELEE) {
			// Ajustar rangos para melee más agresivo
			if (dist_to_enemy > 240.f || height_diff > max_step_height) {
				if (M_NavPathToGoal(self, dist, self->enemy->s.origin)) {
					self->monsterinfo.path_blocked_counter = 0_ms;  // Reset contador al encontrar camino
					return true;
				}
				self->monsterinfo.aiflags &= ~AI_TEMP_MELEE_COMBAT;
			}
			else {
				self->monsterinfo.aiflags &= ~AI_TEMP_MELEE_COMBAT;
				return false;
			}
		}
		else if (style == COMBAT_MIXED) {
			// Ajustar rangos para combate mixto más dinámico
			if (dist_to_enemy > RANGE_NEAR || height_diff > max_step_height * 2.0f) {
				if (M_NavPathToGoal(self, dist, self->enemy->s.origin)) {
					self->monsterinfo.path_blocked_counter = 0_ms;
					return true;
				}
			}
			else {
				return false;
			}
		}
	}
	else {
		// 3. Mejorar el manejo de enemigos no visibles
		if (M_NavPathToGoal(self, dist, self->enemy->s.origin)) {
			self->monsterinfo.path_blocked_counter = 0_ms;
			return true;
		}
	}

	// 4. Mejorar el manejo de errores y bloqueos
	if (!self->inuse)
		return false;

	if (self->monsterinfo.nav_path.returnCode > PathReturnCode::StartPathErrors) {
		// Incrementar tiempo de espera si hay múltiples errores
		self->monsterinfo.path_wait_time = level.time + 10_sec;
		return false;
	}

	// 5. Ajustar el contador de bloqueo para ser más responsivo
	self->monsterinfo.path_blocked_counter += FRAME_TIME_S * 2;  // Reducido de 3 a 2

	if (self->monsterinfo.path_blocked_counter > 3_sec) {  // Reducido de 5 a 3 segundos
		self->monsterinfo.path_blocked_counter = 0_ms;
		self->monsterinfo.path_wait_time = level.time + 3_sec;  // Reducido de 5 a 3 segundos
		return false;
	}

	return true;
}
/*
======================
M_MoveToGoal
======================
*/
void M_MoveToGoal(edict_t* ent, float dist)
{
	if (ai_movement_disabled->integer) {
		if (!FacingIdeal(ent)) {
			M_ChangeYaw(ent);
		} // mal: don't move, but still face toward target
		return;
	}

	edict_t* goal;

	goal = ent->goalentity;

	if (!ent->groundentity && !(ent->flags & (FL_FLY | FL_SWIM)))
		return;
	// ???
	else if (!goal)
		return;

	// [Paril-KEX] try paths if we can't see the enemy
	if (!(ent->monsterinfo.aiflags & AI_COMBAT_POINT) && ent->monsterinfo.attack_state < AS_MISSILE)
	{
		if (M_MoveToPath(ent, dist))
		{
			ent->monsterinfo.path_blocked_counter = max(0_ms, ent->monsterinfo.path_blocked_counter - FRAME_TIME_S);
			return;
		}
	}

	ent->monsterinfo.aiflags &= ~AI_PATHING;

	//if (goal)
	//	gi.Draw_Point(goal->s.origin, 1.f, rgba_red, gi.frame_time_ms, false);

	// [Paril-KEX] dumb hack; in some n64 maps, the corners are way too high and
	// I'm too lazy to fix them individually in maps, so here's a game fix..
	if (!(goal->flags & FL_PARTIALGROUND) && !(ent->flags & (FL_FLY | FL_SWIM)) &&
		goal->classname && (!strcmp(goal->classname, "path_corner") || !strcmp(goal->classname, "point_combat")))
	{
		vec3_t p = goal->s.origin;
		p.z = ent->s.origin.z;

		if (boxes_intersect(ent->absmin, ent->absmax, p, p))
		{
			// mark this so we don't do it again later
			goal->flags |= FL_PARTIALGROUND;

			if (!boxes_intersect(ent->absmin, ent->absmax, goal->s.origin, goal->s.origin))
			{
				// move it if we would have touched it if the corner was lower
				goal->s.origin.z = p.z;
				gi.linkentity(goal);
			}
		}
	}

	// [Paril-KEX] if we have a straight shot to our target, just move
	// straight instead of trying to stick to invisible guide lines
	if ((ent->monsterinfo.bad_move_time <= level.time || (ent->monsterinfo.aiflags & AI_CHARGING)) && goal)
	{
		if (!FacingIdeal(ent))
		{
			M_ChangeYaw(ent);
			return;
		}

		trace_t const tr = gi.traceline(ent->s.origin, goal->s.origin, ent, MASK_MONSTERSOLID);

		if (tr.fraction == 1.0f || tr.ent == goal)
		{
			if (SV_StepDirection(ent, vectoyaw((goal->s.origin - ent->s.origin).normalized()), dist, false))
				return;
		}

		// we didn't make a step, so don't try this for a while
		// *unless* we're going to a path corner
		if (goal->classname && strcmp(goal->classname, "path_corner") && strcmp(goal->classname, "point_combat"))
		{
			ent->monsterinfo.bad_move_time = level.time + 5_sec;
			ent->monsterinfo.aiflags &= ~AI_CHARGING;
		}
	}

	// bump around...
	if ((ent->monsterinfo.random_change_time <= level.time // random change time is up
		&& irandom(4) == 1 // random bump around
		&& !(ent->monsterinfo.aiflags & AI_CHARGING) // PMM - charging monsters (AI_CHARGING) don't deflect unless they have to
		&& !((ent->monsterinfo.aiflags & AI_ALTERNATE_FLY) && ent->enemy && !(ent->monsterinfo.aiflags & AI_LOST_SIGHT))) // alternate fly monsters don't do this either unless they have to
		|| !SV_StepDirection(ent, ent->ideal_yaw, dist, ent->monsterinfo.bad_move_time > level.time))
	{
		if (ent->monsterinfo.aiflags & AI_BLOCKED)
		{
			ent->monsterinfo.aiflags &= ~AI_BLOCKED;
			return;
		}
		ent->monsterinfo.random_change_time = level.time + random_time(500_ms, 1000_ms);
		SV_NewChaseDir(ent, goal->s.origin, dist);
		ent->monsterinfo.move_block_counter = 0;
	}
	else
		ent->monsterinfo.bad_move_time -= 250_ms;

	//vec3_t dir = AngleVectors({ 0.f, ent->ideal_yaw, 0.f }).forward;
	//gi.Draw_Line(ent->s.origin, ent->s.origin + (dir * 24), rgba_blue, gi.frame_time_ms, false);
}

/*
===============
M_walkmove
===============
*/
bool M_walkmove(edict_t* ent, float yaw, float dist)
{
	if (ai_movement_disabled->integer) {
		return false;
	}

	vec3_t move;
	// PMM
	bool retval;

	if (!ent->groundentity && !(ent->flags & (FL_FLY | FL_SWIM)))
		return false;

	yaw = yaw * PIf * 2 / 360;

	move[0] = cosf(yaw) * dist;
	move[1] = sinf(yaw) * dist;
	move[2] = 0;

	// PMM
	retval = SV_movestep(ent, move, true);
	ent->monsterinfo.aiflags &= ~AI_BLOCKED;
	return retval;
}
