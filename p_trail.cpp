// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"

/*

PLAYER TRAIL

==============================================================================

This is a two-way list containing the a list of points of where
the player has been recently. It is used by monsters for pursuit.

This is improved from vanilla; now, the list itself is stored in
client data so it can be stored for multiple clients.

chain = next
enemy = prev

The head node will always have a null "chain", the tail node
will always have a null "enemy".
*/

// Number of breadcrumb markers kept per player. Markers are dropped only when the player moves out
// of sight of the last one and are recycled only when this many newer ones exist (no time expiry),
// so a larger value = a longer trail that persists longer -> monsters track a player who broke line
// of sight further back. Each marker is one edict (per player), so this is cheap vs MAX_EDICTS=8192.
// Bump higher for even more persistence; lower if monsters start chasing stale paths. Only affects
// pursuit of PLAYER enemies (trail needs a client); summoned-vs-enemy uses the goalentity refresh.
constexpr size_t TRAIL_LENGTH = 24; // was 8

// A trail node is only safe to touch if its edict slot is still a live player_trail. If the slot
// was freed and reused (e.g. by a corpse / dropped item under heavy entity load), these fail.
static bool PlayerTrail_NodeValid(const edict_t* e)
{
	return e && e->inuse && e->classname && !strcmp(e->classname, "player_trail");
}

// Defensive: if any trail pointer no longer references a live player_trail entity, the chain is
// corrupt (a freed/reused slot). Reset it so we never walk into - or, worse, reposition (via
// PlayerTrail_Add's `trail->s.origin = player pos`) - a reused entity, which is what teleported
// corpses/items onto players. Validates each node BEFORE dereferencing its ->chain, and is bounded
// so a corrupted/cyclic chain can't loop. Orphaned valid nodes (if any) are cleared at level change.
static void PlayerTrail_Sanitize(gclient_t* cl)
{
	if (!cl)
		return;
	size_t n = 0;
	for (edict_t* c = cl->trail_tail; c; )
	{
		if (!PlayerTrail_NodeValid(c) || ++n > TRAIL_LENGTH + 1)
		{
			cl->trail_head = cl->trail_tail = nullptr;
			return;
		}
		c = c->chain;
	}
	if (cl->trail_head && !PlayerTrail_NodeValid(cl->trail_head))
		cl->trail_head = cl->trail_tail = nullptr;
}

// places a new entity at the head of the player trail.
// the tail entity may be moved to the front if the length
// is at the end.
static edict_t* PlayerTrail_Spawn(edict_t* owner)
{
	// Safety check: Ensure owner is a valid player.
	if (!owner || !owner->client)
		return nullptr;

	size_t len = 0;
	for (edict_t* current = owner->client->trail_tail; current; current = current->chain)
		len++;

	edict_t* trail;

	// If the trail is full, recycle the tail node to become the new head.
	if (len >= TRAIL_LENGTH)
	{
		trail = owner->client->trail_tail;

		// Unlink the old tail.
		// The new tail is the next one in the chain.
		owner->client->trail_tail = trail->chain;
		if (owner->client->trail_tail)
			owner->client->trail_tail->enemy = nullptr; // The new tail has no previous node.

		// Clear the old links of the recycled node.
		trail->chain = nullptr;
		trail->enemy = nullptr;
	}
	else
	{
		// If the trail is not full, spawn a new entity for the trail.
		trail = G_Spawn();
		if (!trail) // G_Spawn can fail if edict limit is reached.
			return nullptr;
		trail->classname = "player_trail";
	}

	// Link the new node as the new head of the list.
	trail->enemy = owner->client->trail_head; // New head's 'prev' is the old head.
	if (owner->client->trail_head)
		owner->client->trail_head->chain = trail; // Old head's 'next' is the new head.

	owner->client->trail_head = trail; // The player now points to the new head.

	// If there was no tail, this new node is also the tail.
	if (!owner->client->trail_tail)
		owner->client->trail_tail = trail;

	return trail;
}

// destroys all player trail entities in the map.
// we don't want these to stay around across level loads.
void PlayerTrail_Destroy(edict_t* player)
{
	for (size_t i = 0; i < globals.num_edicts; i++)
		if (g_edicts[i].classname && strcmp(g_edicts[i].classname, "player_trail") == 0)
			if (!player || g_edicts[i].owner == player)
				G_FreeEdict(&g_edicts[i]);

	// This is the critical fix. After freeing the edicts, the pointers
	// in the client struct must be cleared to prevent them from becoming
	// dangling pointers that point to freed memory.
	if (player)
	{
		if (player->client)
			player->client->trail_head = player->client->trail_tail = nullptr;
	}
	else
	{
		// If called globally (e.g., on level change), clear for all clients.
		for (size_t i = 0; i < game.maxclients; i++)
			game.clients[i].trail_head = game.clients[i].trail_tail = nullptr;
	}
}

// check to see if we can add a new player trail spot
// for this player.
void PlayerTrail_Add(edict_t* player)
{
	// Safety check: Ensure player is valid and has a client structure.
	if (!player || !player->client)
		return;

	// Drop any trail pointers that now reference freed/reused edict slots before we touch them
	// (prevents repositioning a reused corpse/item entity onto the player).
	PlayerTrail_Sanitize(player->client);

	// Don't add a new trail marker if the player can still see the last one they dropped.
	// This prevents spamming markers when standing still or moving slowly.
	if (player->client->trail_head && visible(player, player->client->trail_head))
		return;

	// Don't spawn trails under certain conditions.
	if (level.intermissiontime || player->health <= 0 || player->movetype == MOVETYPE_NOCLIP || !player->groundentity)
		return;

	edict_t* trail = PlayerTrail_Spawn(player);
	if (!trail)
		return; // PlayerTrail_Spawn can fail.

	trail->s.origin = player->s.old_origin;
	trail->timestamp = level.time;
	trail->owner = player;
}

edict_t* PlayerTrail_Pick(edict_t* self, bool next)
{
	if (!self || !self->enemy || !self->enemy->client)
		return nullptr;

	// Drop any trail pointers that now reference freed/reused edict slots before we walk them.
	PlayerTrail_Sanitize(self->enemy->client);

	// This is your safety check. It's excellent.
	// With the G_FreeEdict fix, this should only be triggered by actual monsters,
	// but keeping this check is good practice to prevent crashes from other potential bugs.
	if (!self->enemy->client->trail_head)
		return nullptr;

	// Find the first marker in the player's trail that is *newer* than the last one
	// this monster was pursuing. This prevents the monster from going back to an old spot.
	edict_t* marker;
	for (marker = self->enemy->client->trail_head; marker; marker = marker->enemy)
	{
		// self->monsterinfo.trail_time is updated when a monster successfully reaches a trail marker.
		if (marker->timestamp > self->monsterinfo.trail_time)
			break; // Found a marker newer than the one we last pursued.
	}

	// If no new markers are found, there's nothing to pick.
	if (!marker)
		return nullptr;

	if (next)
	{
		// 'next' is true when the monster has reached a trail point and wants the *next* one.
		// We find the marker on the trail we are currently closest to, and then
		// return the one after it. This helps the monster re-sync if it gets off path.
		float closest_dist_sq = std::numeric_limits<float>::infinity();
		edict_t* closest = nullptr;

		// Iterate from the newest marker we can pursue backwards.
		for (edict_t* m2 = marker; m2; m2 = m2->enemy)
		{
			float const len_sq = (m2->s.origin - self->s.origin).lengthSquared();
			if (len_sq < closest_dist_sq)
			{
				closest_dist_sq = len_sq;
				closest = m2;
			}
		}

		// This should not happen if 'marker' was valid, but it's a good safety check.
		if (!closest)
			return nullptr;

		// Return the next marker in the chain (the one that is older).
		// The monster will follow the trail from newest to oldest.
		return closest->chain;
	}
	else
	{
		// 'next' is false when the monster has just lost sight of the player.
		// From the newest available marker, find the first one the monster can see.
		for (; marker; marker = marker->enemy)
		{
			if (visible(self, marker))
				return marker; // Found a visible marker to run to.
		}
	}

	// If we got here, no suitable marker was found.
	return nullptr;
}