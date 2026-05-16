// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_utils.c -- misc utility functions for game module

#include "g_local.h"
#include "memory_safety.h"
#include <boost/container/small_vector.hpp>

// Entity spawning and reuse constants
constexpr gtime_t ENTITY_REUSE_INITIAL_PERIOD = 2_sec;  // Relax replacement policy during initial server startup
constexpr gtime_t ENTITY_REUSE_DELAY = 500_ms;          // Minimum time before reusing freed entity slot

// Death and cleanup timing constants
constexpr gtime_t DEATH_CLEANUP_DELAY = 2_sec;          // Standard cleanup delay for dead entities
constexpr gtime_t FADE_START_DELAY = 4_sec;             // Delay before starting fade-out effect
constexpr gtime_t FADE_DURATION = 3_sec;                // Duration of fade-out animation
constexpr gtime_t FADE_LIFESPAN = 0.5_sec;              // Fast fade lifespan for immediate effects

// Entity cleanup detection
constexpr gtime_t STUCK_ENTITY_THINK_TIMEOUT = 5_sec;   // Time without thinking before considering entity stuck

/*
=============
Helper Functions
=============
*/

// Validates that an entity pointer is non-null and in use
inline bool IsValidEntity(edict_t* ent) {
	return ent && ent->inuse;
}

// Calculates the center point of an entity's bounding box
inline vec3_t CalculateEntityCenter(const edict_t* ent) {
	return ent->s.origin + (ent->mins + ent->maxs) * 0.5f;
}

// Validates that all if/ifgef statements in a layout string have matching endif statements
// Returns true if balanced, false if imbalanced (with debug logging)
bool ValidateLayoutString(const std::string& layout, const char* debug_name) {
	int if_count = 0;
	int endif_count = 0;
	
	size_t pos = 0;
	while (pos < layout.size())
	{
		// Count "if " with word boundary check (must be at start or after space/newline)
		if (pos + 3 <= layout.size() && layout.substr(pos, 3) == "if ")
		{
			if (pos == 0 || layout[pos - 1] == ' ' || layout[pos - 1] == '\n')
			{
				if_count++;
				pos += 3;
				continue;
			}
		}
		
		// Count "ifgef "
		if (pos + 6 <= layout.size() && layout.substr(pos, 6) == "ifgef ")
		{
			if_count++;
			pos += 6;
			continue;
		}
		
		// Count "endif" with word boundary check (must be followed by space/newline/end)
		if (pos + 5 <= layout.size() && layout.substr(pos, 5) == "endif")
		{
			if (pos + 5 >= layout.size() || layout[pos + 5] == ' ' || layout[pos + 5] == '\n')
			{
				endif_count++;
				pos += 5;
				continue;
			}
		}
		
		pos++;
	}
	
	const bool balanced = (if_count == endif_count);
	
	// Log validation results if developer mode is on
	if (!balanced && developer && developer->integer)
	{
		const char* name = debug_name ? debug_name : "Unknown";
		gi.Com_PrintFmt("ERROR: Layout validation failed for '{}'\n", name);
		gi.Com_PrintFmt("  if/ifgef count: {}\n", if_count);
		gi.Com_PrintFmt("  endif count: {}\n", endif_count);
		gi.Com_PrintFmt("  Layout size: {} bytes\n", layout.size());

		// If developer is 2 or higher, log the full layout string for complete debugging
		if (developer->integer >= 2)
		{
			gi.Com_PrintFmt("  Full layout string:\n{}\n", layout);
			gi.Com_PrintFmt("  --- END FULL LAYOUT ---\n");
		}
		else
		{
			// Developer level 1: Log first 500 chars for debugging
			size_t preview_len = std::min(layout.size(), size_t(500));
			std::string preview = layout.substr(0, preview_len);
			if (layout.size() > preview_len)
				preview += "...";
			gi.Com_PrintFmt("  Layout preview: {}\n", preview);
			gi.Com_PrintFmt("  (Set 'developer 2' to see full layout string)\n");
		}
	}
	
	return balanced;
}

// Removes an entity from its team chain cleanly
inline void RemoveFromTeamChain(edict_t* ent) {
	if (!ent->teammaster)
		return;

	// If this entity is part of a chain, cleanly remove it
	if (ent->flags & FL_TEAMSLAVE)
	{
		for (edict_t* master = ent->teammaster; master; master = master->teamchain)
		{
			if (master->teamchain == ent)
			{
				master->teamchain = ent->teamchain;
				break;
			}
		}
	}
	// Remove teammaster and promote next in chain
	else if (ent->flags & FL_TEAMMASTER)
	{
		ent->teammaster->flags &= ~FL_TEAMMASTER;

		edict_t* new_master = ent->teammaster->teamchain;

		if (new_master)
		{
			new_master->flags |= FL_TEAMMASTER;
			new_master->flags &= ~FL_TEAMSLAVE;

			for (edict_t* m = new_master; m; m = m->teamchain)
				m->teammaster = new_master;
		}
	}
}

/*
=============
G_Find

Searches all active entities for the next one that validates the given callback.

Searches beginning at the edict after from, or the beginning if nullptr
nullptr will be returned if the end of the list is reached.
=============
*/
edict_t* G_Find(edict_t* from, std::function<bool(edict_t* e)> matcher)
{
	if (!from)
		from = g_edicts;
	else
		from++;

	for (; from < &g_edicts[globals.num_edicts]; from++)
	{
		if (!from->inuse)
			continue;
		if (matcher(from))
			return from;
	}

	return nullptr;
}

/*
=================
findradius
Returns entities that have origins within a spherical area
findradius (origin, radius)

Notes:
- The 'from' parameter allows for iterative searching
- Returns the next entity found within radius or nullptr when done
-  to use squared distance for performance
=================
*/
edict_t* findradius(edict_t* from, const vec3_t& org, float rad)
{
	// Parameter validation
	if (!is_valid_vector(org) || rad <= 0.0f) {
		return nullptr;
	}

	// Square the radius to avoid expensive sqrt operations
	const float rad_squared = rad * rad;

	// Initialize starting point
	if (!from) {
		from = g_edicts;
	}
	else {
		from++;
	}

	// Ensure we don't exceed the array bounds
	const edict_t* const edicts_end = &g_edicts[globals.num_edicts];

	// Iterate through entities
	for (; from < edicts_end; from++) {
		// Basic entity validation
		if (!from->inuse || from->solid == SOLID_NOT) {
			continue;
		}

		// Calculate vector from search origin to entity center
		const vec3_t entity_center = CalculateEntityCenter(from);
		const vec3_t eorg = org - entity_center;

		// Use squared length comparison to avoid sqrt
		if (eorg.lengthSquared() > rad_squared) {
			continue;
		}

		return from;
	}

	return nullptr;
}

/*
=============
G_PickTarget

Searches all active entities for the next one that holds
the matching string at fieldofs in the structure.

Searches beginning at the edict after from, or the beginning if nullptr
nullptr will be returned if the end of the list is reached.

=============
*/
constexpr size_t MAXCHOICES = 8;

edict_t* G_PickTarget(const char* targetname)
{
	edict_t* ent = nullptr;
	int num_choices = 0;
	edict_t* choice[MAXCHOICES] = { nullptr }; // Initialize array to nullptrs

	if (!targetname)
	{
		return nullptr;
	}

	while (1)
	{
		ent = G_FindByString<&edict_t::targetname>(ent, targetname);
		if (!ent)
			break;
		choice[num_choices++] = ent;
		if (num_choices == MAXCHOICES)
			break;
	}

	if (!num_choices)
	{
		if (developer->integer)
			gi.Com_PrintFmt("G_PickTarget: target '{}' not found\n", targetname);
		return nullptr;
	}

	return choice[irandom(num_choices)];
}

THINK(Think_Delay) (edict_t* ent) -> void
{
	G_UseTargets(ent, ent->activator);
	G_FreeEdict(ent);
}

void G_PrintActivationMessage(edict_t* ent, edict_t* activator, bool coop_global)
{
	//
	// print the message
	//
	if ((ent->message) && !(activator->svflags & SVF_MONSTER))
	{
		if (coop_global && coop->integer)
			gi.LocBroadcast_Print(PRINT_CENTER, "{}", ent->message);
		else
			gi.LocCenter_Print(activator, "{}", ent->message);

		// [Paril-KEX] allow non-noisy centerprints
		if (ent->noise_index >= 0)
		{
			if (ent->noise_index)
				gi.sound(activator, CHAN_AUTO, ent->noise_index, 1, ATTN_NORM, 0);
			else
				gi.sound(activator, CHAN_AUTO, gi.soundindex("misc/talk1.wav"), 1, ATTN_NORM, 0);
		}
	}
}

void G_MonsterKilled(edict_t* self);

/*
==============================
G_UseTargets

the global "activator" should be set to the entity that initiated the firing.

If self.delay is set, a DelayedUse entity will be created that will actually
do the SUB_UseTargets after that many seconds have passed.

Centerprints any self.message to the activator.

Search for (string)targetname in all entities that
match (string)self.target and call their .use function

==============================
*/
void G_UseTargets(edict_t* ent, edict_t* activator)
{
	edict_t* t;

	//
	// check for a delay
	//
	if (ent->delay)
	{
		// create a temp object to fire at a later time
		t = G_Spawn();
		t->classname = "DelayedUse";
		t->nextthink = level.time + gtime_t::from_sec(ent->delay);
		t->think = Think_Delay;
		t->activator = activator;
		if (!activator)
			gi.Com_Print("Think_Delay with no activator\n");
		t->message = ent->message;
		t->target = ent->target;
		t->killtarget = ent->killtarget;
		return;
	}

	//
	// print the message
	//
	G_PrintActivationMessage(ent, activator, true);

	//
	// kill killtargets
	//
	if (ent->killtarget)
	{
		t = nullptr;
		while ((t = G_FindByString<&edict_t::targetname>(t, ent->killtarget)))
		{
			// Remove from team chain if part of one
			RemoveFromTeamChain(t);

			// [Paril-KEX] if we killtarget a monster, clean up properly
			if (t->svflags & SVF_MONSTER)
			{
				if (!t->deadflag && !(t->monsterinfo.aiflags & AI_DO_NOT_COUNT) && !(t->spawnflags & SPAWNFLAG_MONSTER_DEAD))
					G_MonsterKilled(t);
			}

			// PMM
			G_FreeEdict(t);

			if (!ent->inuse)
			{
				gi.Com_Print("entity was removed while using killtargets\n");
				return;
			}
		}
	}

	//
	// fire targets
	//
	if (ent->target)
	{
		// Cache classname comparisons for performance
		const bool is_door = !Q_strcasecmp(ent->classname, "func_door");
		const bool is_door_rotating = !Q_strcasecmp(ent->classname, "func_door_rotating");
		const bool is_door_secret = !Q_strcasecmp(ent->classname, "func_door_secret");
		const bool is_water = !Q_strcasecmp(ent->classname, "func_water");
		const bool is_door_like = is_door || is_door_rotating || is_door_secret || is_water;

		t = nullptr;
		while ((t = G_FindByString<&edict_t::targetname>(t, ent->target)))
		{
			// doors fire area portals in a specific way
			if (is_door_like && !Q_strcasecmp(t->classname, "func_areaportal"))
				continue;

			if (t == ent)
			{
				gi.Com_Print("WARNING: Entity used itself.\n");
			}
			else
			{
				if (t->use)
					t->use(t, ent, activator);
			}
			if (!ent->inuse)
			{
				gi.Com_Print("entity was removed while using targets\n");
				return;
			}
		}
	}
}

constexpr vec3_t VEC_UP = { 0, -1, 0 };
constexpr vec3_t MOVEDIR_UP = { 0, 0, 1 };
constexpr vec3_t VEC_DOWN = { 0, -2, 0 };
constexpr vec3_t MOVEDIR_DOWN = { 0, 0, -1 };

void G_SetMovedir(vec3_t& angles, vec3_t& movedir)
{
	if (angles == VEC_UP)
	{
		movedir = MOVEDIR_UP;
	}
	else if (angles == VEC_DOWN)
	{
		movedir = MOVEDIR_DOWN;
	}
	else
	{
		AngleVectors(angles, movedir, nullptr, nullptr);
	}

	angles = {};
}

char* G_CopyString(const char* in, int32_t tag)
{
	if (!in)
		return nullptr;
	const size_t amt = strlen(in) + 1;
	char* const out = static_cast<char*>(gi.TagMalloc(amt, tag));
	Q_strlcpy(out, in, amt);
	return out;
}

void G_InitEdict(edict_t* e)
{
	// This is the safe way to clear an edict. We must preserve critical pointers
	// and flags that are set *before* this function is called.
	gclient_t* const saved_client = e->client; // Save the client pointer
	const int32_t saved_spawn_count = e->spawn_count; // Preserve spawn count across clears
	const svflags_t saved_svflags = e->svflags; // Save the original flags before they are cleared

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
	memset(e, 0, sizeof(*e)); // Zero out the entire structure
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

	e->client = saved_client; // Restore the client pointer
	e->spawn_count = saved_spawn_count; // Restore the spawn count

	// Re-initialize C++ objects after memset to avoid UB
	e->monsterinfo = monsterinfo_t{}; // Default construct instead of using memset value

	// Restore ONLY the flags that identify the entity as a player or bot.
	// This prevents inheriting dangerous state like SVF_MONSTER or SVF_NOCLIENT from a reused slot.
	e->svflags = saved_svflags & (SVF_PLAYER | SVF_BOT);

	// ROGUE
	// FIXME -
	//   this fixes a bug somewhere that is setting "nextthink" for an entity that has
	//   already been released.  nextthink is being set to FRAME_TIME_S after level.time,
	//   since freetime = nextthink - FRAME_TIME_S
	if (e->nextthink)
		e->nextthink = 0_ms;
	// ROGUE

	e->inuse = true;
	e->sv.init = false;
	e->classname = "noclass";
	e->gravity = 1.0;
	e->s.number = e - g_edicts;

	// PGM - do this before calling the spawn function so it can be overridden.
	e->gravityVector[0] = 0.0;
	e->gravityVector[1] = 0.0;
	e->gravityVector[2] = -1.0;
	// PGM
	e->monsterinfo.monster_type_id = MONSTER_TYPE_UNKNOWN;
	e->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::UNKNOWN);

	//testing new teams feature
    // Set the default team based on whether the entity is a player.
    if (e->svflags & SVF_PLAYER)
    {
        // If the entity is a player, it should start with no team.
        // The game logic will later assign it to CTF_TEAM1 when they join the game.
        e->ctf_team = CTF_NOTEAM;
    }
    else
    {
        // If it's NOT a player (e.g., a monster, an item, a door, a future deployable),
        // default it to the enemy team.
        e->ctf_team = CTF_TEAM2;
    }
}
/*
=================
G_Spawn

Either finds a free edict, or allocates a new one.
This is the CORRECT place to clear the entity data to prevent stale data bugs.
=================
*/

edict_t* G_Spawn()
{
	uint32_t i;
	edict_t* e;

	e = &g_edicts[game.maxclients + 1];
	for (i = game.maxclients + 1; i < globals.num_edicts; i++, e++)
	{
		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if (!e->inuse && (e->freetime < ENTITY_REUSE_INITIAL_PERIOD || level.time - e->freetime > ENTITY_REUSE_DELAY))
		{
			G_InitEdict(e);
			return e;
		}
	}

	if (i == game.maxentities)
		gi.Com_Error("ED_Alloc: no free edicts");

	globals.num_edicts++;
	G_InitEdict(e);
	return e;
}

/*
=================
G_FreeEdict

Marks the edict as free and cleans up dangling pointers to it.
=================
*/
THINK(G_FreeEdict) (edict_t* ed) -> void {
    // Already freed check
    if (!IsValidEntity(ed))
        return;

    // --- Dangling Pointer Cleanup Loop ---
    // PERFORMANCE NOTE: This is an O(n) operation that scans all entities.
    // This is necessary to prevent dangling pointer crashes, but expensive.
    // Consider batching multiple entity frees if performance becomes an issue.
    //
    // We iterate through all other entities and nullify any common pointers
    // that point to the entity we are about to free.
    for (edict_t* other = g_edicts; other < &g_edicts[globals.num_edicts]; other++)
    {
        if (!other->inuse || other == ed)
            continue;

        // Early exit: skip entities that are unlikely to have entity references
        // Items, projectiles, and temporary effects typically don't reference other entities
        const bool is_simple_entity = (other->solid == SOLID_NOT && !other->client &&
                                       !(other->svflags & SVF_MONSTER));
        if (is_simple_entity)
            continue;

        // Nullify common entity pointers
        if (other->owner == ed) other->owner = nullptr;
        if (other->enemy == ed) other->enemy = nullptr;
        if (other->oldenemy == ed) other->oldenemy = nullptr;
        if (other->goalentity == ed) other->goalentity = nullptr;
        if (other->movetarget == ed) other->movetarget = nullptr;
        if (other->target_ent == ed) other->target_ent = nullptr;
        if (other->teammaster == ed) other->teammaster = nullptr;
        if (other->teamchain == ed) other->teamchain = nullptr;
        if (other->chain == ed) other->chain = nullptr; // Used by player_trail

        // Check client-specific pointers
        if (other->client) {
            if (other->client->chase_target == ed) other->client->chase_target = nullptr;
            if (other->client->sight_entity == ed) other->client->sight_entity = nullptr;
            if (other->client->sound_entity == ed) other->client->sound_entity = nullptr;
            if (other->client->sound2_entity == ed) other->client->sound2_entity = nullptr;
            // The player_trail head/tail pointers are handled in PlayerTrail_Destroy,
            // but adding them here provides an extra layer of safety.
            if (other->client->trail_head == ed) other->client->trail_head = nullptr;
            if (other->client->trail_tail == ed) other->client->trail_tail = nullptr;
        }

        // Check monster-specific pointers
        if (other->svflags & SVF_MONSTER) {
            if (other->monsterinfo.commander == ed) other->monsterinfo.commander = nullptr;
            if (other->monsterinfo.healer == ed) other->monsterinfo.healer = nullptr;
            if (other->monsterinfo.badMedic1 == ed) other->monsterinfo.badMedic1 = nullptr;
            if (other->monsterinfo.badMedic2 == ed) other->monsterinfo.badMedic2 = nullptr;
            if (other->monsterinfo.last_player_enemy == ed) other->monsterinfo.last_player_enemy = nullptr;
        }
    }
    // --- END Dangling Pointer Cleanup ---

    // Handle cleanup through OnEntityRemoved
    OnEntityRemoved(ed);

    // Unlink from world
    gi.unlinkentity(ed);

    // Protected entity check
    if ((ed - g_edicts) <= (ptrdiff_t)(game.maxclients + BODY_QUEUE_SIZE)) {
#ifdef _DEBUG
        gi.Com_Print("tried to free special edict\n");
#endif
        return;
    }

    // Unregister from bot system
    gi.Bot_UnRegisterEdict(ed);

    // Preserve and increment spawn count
    int32_t id = ed->spawn_count + 1;

    // CRITICAL: Manually release C++ objects before memset
    // The memset bypasses destructors, causing memory leaks for heap-allocated members
    // See TODO comment at g_local.h:1472-1474
    if (ed->moveinfo.curve_positions.ptr) {
        ed->moveinfo.curve_positions.release();
    }

    // Clear entity data
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
    memset(ed, 0, sizeof(*ed));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

    // Restore essential fields
    ed->s.number = ed - g_edicts;
    ed->classname = "freed";
    ed->freetime = level.time;
    ed->inuse = false;
    ed->spawn_count = id;
    ed->sv.init = false;
}

BoxEdictsResult_t G_TouchTriggers_BoxFilter(edict_t* hit, void*)
{
	if (!hit->touch)
		return BoxEdictsResult_t::Skip;

	return BoxEdictsResult_t::Keep;
}

/*
============
G_TouchTriggers

============
*/
void G_TouchTriggers(edict_t* ent)
{
	int		 i, num;
	static edict_t* touch[MAX_EDICTS];
	edict_t* hit;

	if (!ent) // null check
		return;
	// dead things don't activate triggers!
	if ((ent->client || (ent->svflags & SVF_MONSTER)) && (ent->health <= 0))
		return;

	num = gi.BoxEdicts(ent->absmin, ent->absmax, touch, MAX_EDICTS, AREA_TRIGGERS, G_TouchTriggers_BoxFilter, nullptr);

	// be careful, it is possible to have an entity in this
	// list removed before we get to it (killtriggered)
	for (i = 0; i < num; i++)
	{
		hit = touch[i];
		if (!IsValidEntity(hit))
			continue;
		if (!hit->touch)
			continue;
		hit->touch(hit, ent, null_trace, true);
	}
}

// [Paril-KEX] scan for projectiles between our movement positions
// to see if we need to collide against them
void G_TouchProjectiles(edict_t* ent, vec3_t previous_origin)
{
	struct skipped_projectile
	{
		edict_t* projectile;
		int32_t spawn_count;
	};

	// Static vector to store projectiles we are temporarily ignoring
	static boost::container::small_vector<skipped_projectile, 32> skipped;

	// Clear old entries to prevent accumulation over time
	periodic_cleanup(skipped, MAX_SKIPPED_PROJECTILES, MAX_SKIPPED_PROJECTILES / 2);

	if (!ent)
		return;

	// Trace through all projectiles along movement path
	while (true)
	{
		trace_t tr = gi.trace(previous_origin, ent->mins, ent->maxs, ent->s.origin, ent, ent->clipmask | CONTENTS_PROJECTILE);

		// Stop if no more collisions or invalid trace
		if (tr.fraction == 1.0f || !tr.ent || !(tr.ent->svflags & SVF_PROJECTILE))
			break;

		// Temporarily mark projectile as skipped to avoid re-detecting it
		tr.ent->svflags &= ~SVF_PROJECTILE;

		// Store projectile info for restoration later
		if (!safe_push_back(skipped, skipped_projectile{ tr.ent, tr.ent->spawn_count }, MAX_SKIPPED_PROJECTILES)) {
			// Vector full: drop oldest entry and retry
			if (developer && developer->integer) {
				gi.Com_Print("WARNING: Too many skipped projectiles, dropping oldest\n");
			}
			if (!skipped.empty()) {
				skipped.erase(skipped.begin());
				safe_push_back(skipped, skipped_projectile{ tr.ent, tr.ent->spawn_count }, MAX_SKIPPED_PROJECTILES);
			}
		}

		// Allow friendly fire pass-through in coop
		if (ent->client && tr.ent->owner && tr.ent->owner->client && !G_ShouldPlayersCollide(true))
			continue;

		G_Impact(ent, tr);
	}

	// Restore SVF_PROJECTILE flag for all skipped projectiles that are still valid
	for (auto& skip : skipped) {
		if (IsValidEntity(skip.projectile) && skip.projectile->spawn_count == skip.spawn_count)
			skip.projectile->svflags |= SVF_PROJECTILE;
	}

	skipped.clear();
}

/*
==============================================================================

Kill box

==============================================================================
*/

/*
=================
KillBox

Kills all entities that would touch the proposed new positioning
of ent.
=================
*/

BoxEdictsResult_t KillBox_BoxFilter(edict_t* hit, void*)
{
	if (!hit->solid || !hit->takedamage || hit->solid == SOLID_TRIGGER)
		return BoxEdictsResult_t::Skip;

	return BoxEdictsResult_t::Keep;
}

bool KillBox(edict_t* ent, bool from_spawning, mod_id_t mod, bool bsp_clipping, bool allow_safety)
{
	// don't telefrag as spectator...
	if (ent->movetype == MOVETYPE_NOCLIP)
		return true;

	contents_t mask = CONTENTS_MONSTER | CONTENTS_PLAYER;

	// [Paril-KEX] don't gib other players in coop if we're not colliding
	if (from_spawning && ent->client && coop->integer && !G_ShouldPlayersCollide(false))
		mask &= ~CONTENTS_PLAYER;

	int		 i, num;
	static edict_t* touch[MAX_EDICTS];
	edict_t* hit;

	num = gi.BoxEdicts(ent->absmin, ent->absmax, touch, MAX_EDICTS, AREA_SOLID, KillBox_BoxFilter, nullptr);

	for (i = 0; i < num; i++)
	{
		hit = touch[i];

		if (hit == ent)
			continue;
		else if (!hit->inuse || !hit->takedamage || !hit->solid || hit->solid == SOLID_TRIGGER || hit->solid == SOLID_BSP)
			continue;
		else if (hit->client && !(mask & CONTENTS_PLAYER))
			continue;

		if ((ent->solid == SOLID_BSP || (ent->svflags & SVF_HULL)) && bsp_clipping)
		{
			trace_t clip = gi.clip(ent, hit->s.origin, hit->mins, hit->maxs, hit->s.origin, G_GetClipMask(hit));

			if (clip.fraction == 1.0f)
				continue;
		}

		// [Paril-KEX] don't allow telefragging of friends in coop.
		// the player that is about to be telefragged will have collision
		// disabled until another time.
		if (ent->client && hit->client/* && coop->integer*/) // disabled coop check so horde works
		{
			hit->clipmask &= ~CONTENTS_PLAYER;
			ent->clipmask &= ~CONTENTS_PLAYER;
			continue;
		}

		if (allow_safety && G_FixStuckObject(hit, hit->s.origin) != stuck_result_t::NO_GOOD_POSITION)
			continue;

		T_Damage(hit, ent, ent, vec3_origin, ent->s.origin, vec3_origin, 100000, 0, DAMAGE_NO_PROTECTION, mod);
	}

	return true; // all clear
}

// Modify the CheckAndRestoreMonsterAlpha function to batch updates
void CheckAndRestoreMonsterAlpha(edict_t* const ent) {
	if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER)) {
		return;
	}

	// Skip monsters with bonus flags that intentionally use alpha transparency
	if (ent->monsterinfo.bonus_flags & (BF_GHOSTLY | BF_RAGEQUITTER | BF_POSSESSED)) {
		return;
	}

	// Batch multiple attribute changes before linking
	bool needs_update = false;
	if (ent->health > 0 && !ent->deadflag && ent->s.alpha < 1.0f) {
		ent->s.alpha = 0.0f;
		ent->s.renderfx &= ~RF_TRANSLUCENT;
		ent->takedamage = true;
		needs_update = true;
	}

	// Only link if necessary
	if (needs_update) {
		gi.linkentity(ent);
	}
}

THINK(fade_out_think)(edict_t* self) -> void {
	// If monster is alive, restore its state
	if (self->health > 0 && !self->deadflag) {
		CheckAndRestoreMonsterAlpha(self);
		self->nextthink = level.time + FRAME_TIME_MS;
		self->is_fading_out = false;
		return;
	}

	// Fade complete - free the entity
	if (level.time >= self->timestamp) {
		self->is_fading_out = false;
		G_FreeEdict(self);
		return;
	}

	// Calculate fade factor using the same method as spawngrow
	const float t = 1.f - ((level.time - self->teleport_time).seconds() / self->wait);
	self->s.alpha = t * t; // Use t^2 for smoother fade like spawngrow

	self->nextthink = level.time + FRAME_TIME_MS;
}

void StartFadeOut(edict_t* ent) {
	// Don't start fade out if monster is alive or already fading
	if ((ent->health > 0 && !ent->deadflag) ||
		ent->is_fading_out ||
		(ent->monsterinfo.aiflags & (AI_CLEANUP_FADE | AI_CLEANUP_NORMAL))) {
		return;
	}

	// Configure fade timing
	ent->teleport_time = level.time;
	ent->timestamp = level.time + FADE_LIFESPAN;
	ent->wait = FADE_LIFESPAN.seconds();

	// Configure think function
	ent->think = fade_out_think;
	ent->nextthink = level.time + FRAME_TIME_MS;

	// Mark as fading in progress
	ent->is_fading_out = true;

	// Configure entity state
	ent->solid = SOLID_NOT;
	ent->movetype = MOVETYPE_NONE;
	ent->takedamage = false;
	ent->svflags &= ~SVF_NOCLIENT;
	ent->s.renderfx &= ~RF_DOT_SHADOW;

	// Ensure entity is linked to world
	gi.linkentity(ent);
}

void OnEntityDeath(edict_t* self) noexcept
{
	if (!self || !self->inuse || self->monsterinfo.death_processed || !g_horde->integer) {
		return;
	}

	self->monsterinfo.death_processed = true;

	// --- State Cleanup on Death ---
	if (self->svflags & SVF_MONSTER) {
		self->monsterinfo.bonus_flags = BF_NONE;
		self->monsterinfo.effects_applied = false;
		self->monsterinfo.IS_BOSS = false;
	}

	// --- Setup Post-Death Behavior (Fading, etc.) ---
	// This logic is correct for setting up how the dead body behaves.
	bool apply_horde_fade = (self->svflags & SVF_MONSTER) && g_horde && g_horde->integer;

	if (apply_horde_fade) {
		self->teleport_time = level.time + FADE_START_DELAY;
		self->timestamp = self->teleport_time + FADE_DURATION;
		self->wait = FADE_DURATION.seconds();

		self->monsterinfo.aiflags |= AI_CLEANUP_FADE;
		self->monsterinfo.aiflags &= ~AI_CLEANUP_NORMAL;
		self->s.renderfx &= ~RF_DOT_SHADOW;
	} else {
		self->timestamp = level.time + DEATH_CLEANUP_DELAY;
		if (self->svflags & SVF_MONSTER) {
			self->monsterinfo.aiflags |= AI_CLEANUP_NORMAL;
			self->monsterinfo.aiflags &= ~AI_CLEANUP_FADE;
		}
	}
}

#include "horde/g_laser.h"
// This function is for the FINAL cleanup before an entity is removed from the game.
// This is where you free all associated memory.
void OnEntityRemoved(edict_t* self){
	if (!self) {
		return;}

	// --- Free Savable Memory ---
	self->moveinfo.curve_positions.release();
}

void CleanupInvalidEntities() {
	for (uint32_t i = 0; i < (globals.num_edicts); i++) {
		edict_t* ent = &g_edicts[i];
		if (ent->inuse && ent->svflags & SVF_MONSTER) {
			if (ent->solid == SOLID_NOT && ent->health > 0) {
				gi.Com_PrintFmt("PRINT: Removing Bug/Immortal monster: {}\n", ent->classname);
				ent->health = -1;
				OnEntityDeath(ent);
				G_FreeEdict(ent);
			}
		}
	}
}

void CleanupStuckEntities() {
	// Calculate the starting index AFTER the body queue
	const uint32_t start_index = game.maxclients + static_cast<uint32_t>(BODY_QUEUE_SIZE) + 1U;

	// Iterate through edicts, skipping players AND body queue slots.
	for (uint32_t i = start_index; i < globals.num_edicts; i++) {
		edict_t* ent = &g_edicts[i];

		// Basic validity checks
		if (!IsValidEntity(ent)) {
			continue;
		}

		// Skip active items (can keep this check)
		if (ent->item) {
			continue;
		}

		// --- Conditions for identifying a stuck/lingering entity ---
		if ((ent->solid == SOLID_BSP || ent->solid == SOLID_BBOX) && ent->health <= 0) {
			bool stopped_thinking = (!ent->think || ent->nextthink <= level.time - STUCK_ENTITY_THINK_TIMEOUT);
			bool not_fading = !ent->is_fading_out;
			bool likely_monster = (ent->svflags & (SVF_MONSTER | SVF_DEADMONSTER)) || ent->monsterinfo.was_spawned_by_horde;

			if (stopped_thinking && not_fading && likely_monster) {
				if (developer->integer) {
					gi.Com_PrintFmt("CleanupStuckEntities: Removing stuck entity #{} (Class: {}, Solid: {}, Health: {}, Think: {}, NextThink: {:.2f}, Fading: {})\n",
						static_cast<int>(ent - g_edicts),
						ent->classname ? ent->classname : "null",
						static_cast<int>(ent->solid), // Corrected cast
						ent->health,
						ent->think ? "Yes" : "No",
						ent->nextthink.seconds(),
						ent->is_fading_out ? "Yes" : "No");
				}
				G_FreeEdict(ent);
			}
		}
	}
}

/*
=================
IsPlayerMenuProtected

Helper function to check if a player is menu protected
Returns true if the player is in a menu and protected from damage/actions
=================
*/
bool IsPlayerMenuProtected(edict_t* ent)
{
	// Validate entity and check if it's a player
	if (!ent || !ent->client)
		return false;

	// Return the menu protection status
	return ent->client->menu_protected;
}

/*
=================
SetProjectileAttackerInfo

Sets projectile attacker tracking information for proper kill attribution
=================
*/
void SetProjectileAttackerInfo(edict_t* projectile, edict_t* attacker)
{
	if (!projectile || !attacker)
		return;

	if (attacker->client)
	{
		projectile->projectile_was_player_attacker = true;
		projectile->projectile_attacker_type_id = 0;
		projectile->projectile_attacker_level = 0;
	}
	else if (attacker->svflags & SVF_MONSTER)
	{
		projectile->projectile_was_player_attacker = false;
		projectile->projectile_attacker_type_id = attacker->monsterinfo.monster_type_id;
		projectile->projectile_attacker_level = attacker->monsterinfo.pvm_level;
	}
}

/*
=================
GetRealAttacker

Resolves ownership chains to find the real attacker
Handles: sentry guns -> player, doppelgangers -> player, etc.
=================
*/
edict_t* GetRealAttacker(edict_t* entity)
{
	if (!IsValidEntity(entity))
		return nullptr;

	edict_t* attacker = entity;

	// If entity has an owner, start with that
	if (IsValidEntity(entity->owner))
		attacker = entity->owner;

	// Check for sentry gun ownership chain
	if (attacker && horde::IsSpecialType(attacker, horde::SpecialEntityTypeID::SENTRY_GUN))
	{
		if (IsValidEntity(attacker->owner))
			attacker = attacker->owner;
	}

	// Check for doppelganger ownership chain
	if (attacker && horde::IsSpecialType(attacker, horde::SpecialEntityTypeID::DOPPLEGANGER))
	{
		if (IsValidEntity(attacker->teammaster))
			attacker = attacker->teammaster;
	}

	// If we still don't have a valid attacker, default to the original entity
	if (!IsValidEntity(attacker))
		attacker = entity;

	return attacker;
}

/*
=================
SendMuzzleFlash

Sends standardized muzzle flash effect to clients
=================
*/
void SendMuzzleFlash(edict_t* ent, player_muzzle_t effect)
{
	if (!ent)
		return;

	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	gi.WriteByte(effect);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);
}

/*
=================
ApplyQuadDamage

Applies quad damage multiplier consistently
Returns modified damage and kick values
=================
*/
QuadDamageResult ApplyQuadDamage(int base_damage, int base_kick, edict_t* ent)
{
	QuadDamageResult result{base_damage, base_kick};

	if (!ent)
		return result;

	if (is_quad)
	{
		result.damage *= damage_multiplier;
		result.kick *= damage_multiplier;
	}

	return result;
}
