// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// Teleport forward ability ported from Vortex

#include "../g_local.h"
#include "../shared.h"

// Teleport forward constants
constexpr int TELEPORT_MAX_DISTANCE = 512;
constexpr int TELEPORT_STEP_BACK = 8;
constexpr gtime_t TELEPORT_COOLDOWN = 0.5_sec;

// Forward declarations
static bool ValidTeleportSpot(edict_t* ent, const vec3_t& spot);

// Check if a teleport spot is valid (not near sky brushes)
static bool ValidTeleportSpot(edict_t* ent, const vec3_t& spot)
{
    vec3_t start, forward, right;
    trace_t tr;

    AngleVectors(ent->s.angles, forward, right, nullptr);

    // Check above
    start = spot;
    start.z += 128;
    tr = gi.traceline(spot, start, ent, MASK_SOLID);
    if (tr.surface && (tr.surface->flags & SURF_SKY))
        return false;

    // Check left
    start = spot + right * -128;
    tr = gi.traceline(spot, start, ent, MASK_SOLID);
    if (tr.surface && (tr.surface->flags & SURF_SKY))
        return false;

    // Check right
    start = spot + right * 128;
    tr = gi.traceline(spot, start, ent, MASK_SOLID);
    if (tr.surface && (tr.surface->flags & SURF_SKY))
        return false;

    // Check forward
    start = spot + forward * 128;
    tr = gi.traceline(spot, start, ent, MASK_SOLID);
    if (tr.surface && (tr.surface->flags & SURF_SKY))
        return false;

    // Check behind
    start = spot + forward * -128;
    tr = gi.traceline(spot, start, ent, MASK_SOLID);
    if (tr.surface && (tr.surface->flags & SURF_SKY))
        return false;

    return true;
}

// Teleport forward command
void Cmd_TeleportForward_f(edict_t* ent)
{
    int dist = TELEPORT_MAX_DISTANCE;
    vec3_t forward, right, start, end;
    trace_t tr;

    // Basic validation
    if (!ent || !ent->client)
        return;

    if (!ent->inuse || ent->health <= 0)
        return;

    // Check if player is spectating or menu protected
    if (ClientIsSpectating(ent->client))
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Cannot teleport while spectating.\n");
        return;
    }

    if (IsPlayerMenuProtected(ent))
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Cannot teleport while in a menu.\n");
        return;
    }

    // Check cooldown
    if (ent->client->resp.teleport_cooldown > level.time)
    {
        float remaining_seconds = (ent->client->resp.teleport_cooldown - level.time).seconds();
        float remaining_display = std::floor(remaining_seconds * 10.0f) / 10.0f;
        gi.LocClient_Print(ent, PRINT_HIGH, "Teleport on cooldown for {} seconds\n", remaining_display);
        return;
    }

    // Get forward vector from player's view
    AngleVectors(ent->client->v_angle, forward, right, nullptr);

    // Get starting position
    start = ent->s.origin;
    start.z++;

    // Keep trying to teleport until there is no room left
    while (dist > 0)
    {
        end = start + forward * dist;

        // If there is a nearby sky brush, we shouldn't teleport there
        if (!ValidTeleportSpot(ent, end))
        {
            // Try further back
            dist -= TELEPORT_STEP_BACK;
            continue;
        }

        tr = gi.trace(end, ent->mins, ent->maxs, end, ent, MASK_SHOT);

        // Is this a valid position?
        if (!(tr.contents & MASK_SHOT) && !(gi.pointcontents(end) & MASK_SHOT) && !tr.allsolid)
        {
            // Teleport successful!
            ent->s.event = EV_PLAYER_TELEPORT;
            ent->s.origin = end;
            ent->s.old_origin = end;
            ent->velocity = vec3_origin;
            gi.linkentity(ent);

            // Set cooldown
            ent->client->resp.teleport_cooldown = level.time + TELEPORT_COOLDOWN;

            // Visual/audio effect
            gi.WriteByte(svc_temp_entity);
            gi.WriteByte(TE_TELEPORT_EFFECT);
            gi.WritePosition(end);
            gi.multicast(end, MULTICAST_PVS, false);

            return;
        }
        else
        {
            // Try further back
            dist -= TELEPORT_STEP_BACK;
        }
    }

    // Failed to find a valid teleport spot
    gi.LocClient_Print(ent, PRINT_HIGH, "No valid teleport destination found.\n");
}
