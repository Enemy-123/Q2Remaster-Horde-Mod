// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_utils.c -- misc utility functions for game module

#include "g_local.h"

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
- Optimized to use squared distance for performance
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

		// Calculate vector to entity center
		vec3_t eorg;
		for (int j = 0; j < 3; j++) {
			// Calculate entity center by adding half of its bounding box to origin
			const float entity_center_offset = (from->mins[j] + from->maxs[j]) * 0.5f;
			eorg[j] = org[j] - (from->s.origin[j] + entity_center_offset);
		}

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
			// Log error message here if needed
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
			if (t->teammaster)
			{
				// PMM - if this entity is part of a chain, cleanly remove it
				if (t->flags & FL_TEAMSLAVE)
				{
					for (edict_t* master = t->teammaster; master; master = master->teamchain)
					{
						if (master->teamchain == t)
						{
							master->teamchain = t->teamchain;
							break;
						}
					}
				}
				// [Paril-KEX] remove teammaster too
				else if (t->flags & FL_TEAMMASTER)
				{
					t->teammaster->flags &= ~FL_TEAMMASTER;

					edict_t* new_master = t->teammaster->teamchain;

					if (new_master)
					{
						new_master->flags |= FL_TEAMMASTER;
						new_master->flags &= ~FL_TEAMSLAVE;

						for (edict_t* m = new_master; m; m = m->teamchain)
							m->teammaster = new_master;
					}
				}
			}

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
		t = nullptr;
		while ((t = G_FindByString<&edict_t::targetname>(t, ent->target)))
		{
			// doors fire area portals in a specific way
			if (!Q_strcasecmp(t->classname, "func_areaportal") &&
				(!Q_strcasecmp(ent->classname, "func_door") || !Q_strcasecmp(ent->classname, "func_door_rotating")
					|| !Q_strcasecmp(ent->classname, "func_door_secret") || !Q_strcasecmp(ent->classname, "func_water")))
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
    e->monsterinfo.monster_type_id = MONSTER_TYPE_UNKNOWN; // Access it via monsterinfo
    e->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::UNKNOWN); // Access it directly
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
		if (!e->inuse && (e->freetime < 2_sec || level.time - e->freetime > 500_ms))
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
    if (!ed || !ed->inuse) // Added null check for safety
        return;

    // --- NEW: Dangling Pointer Cleanup Loop ---
    // Iterate through all other entities and nullify any common pointers
    // that point to the entity we are about to free.
    for (edict_t* other = g_edicts; other < &g_edicts[globals.num_edicts]; other++)
    {
        if (!other->inuse || other == ed)
            continue;

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
        if (other->monsterinfo.commander == ed) other->monsterinfo.commander = nullptr;
        if (other->monsterinfo.healer == ed) other->monsterinfo.healer = nullptr;
        if (other->monsterinfo.badMedic1 == ed) other->monsterinfo.badMedic1 = nullptr;
        if (other->monsterinfo.badMedic2 == ed) other->monsterinfo.badMedic2 = nullptr;
        if (other->monsterinfo.last_player_enemy == ed) other->monsterinfo.last_player_enemy = nullptr;
    }
    // --- END NEW ---

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

    // Clear entity data
    memset(ed, 0, sizeof(*ed));

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
		if (!hit || !hit->inuse)
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
		int32_t		spawn_count;
	};
	// a bit ugly, but we'll store projectiles we are ignoring here.
	static std::vector<skipped_projectile> skipped;

	if (!ent) 
		return;
	while (true)
	{
		trace_t tr = gi.trace(previous_origin, ent->mins, ent->maxs, ent->s.origin, ent, ent->clipmask | CONTENTS_PROJECTILE);

		if (tr.fraction == 1.0f)
			break;
		if (!tr.ent)
			break;
		if (!(tr.ent->svflags & SVF_PROJECTILE))
			break;

		// always skip this projectile since certain conditions may cause the projectile
		// to not disappear immediately
		tr.ent->svflags &= ~SVF_PROJECTILE;
		skipped.push_back({ tr.ent, tr.ent->spawn_count });

		// if we're both players and it's coop, allow the projectile to "pass" through
		if (ent->client && tr.ent->owner && tr.ent->owner->client && !G_ShouldPlayersCollide(true))
			continue;

		G_Impact(ent, tr);
	}

	for (auto& skip : skipped)
		if (skip.projectile->inuse && skip.projectile->spawn_count == skip.spawn_count)
			skip.projectile->svflags |= SVF_PROJECTILE;

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


// Constante para el tiempo de vida del fade
constexpr gtime_t FADE_LIFESPAN = 0.5_sec;

THINK(fade_out_think)(edict_t* self) -> void {
	// Si el monstruo está vivo, restaurar su estado
	if (self->health > 0 && !self->deadflag) {
		CheckAndRestoreMonsterAlpha(self);
		//	self->think = monster_think;
		self->nextthink = level.time + FRAME_TIME_MS;
		self->is_fading_out = false;  // Usar bool
		return;
	}

	if (level.time >= self->timestamp) {
		self->is_fading_out = false;  // Limpiar el bool antes de liberar
		G_FreeEdict(self);
		return;
	}

	// Calcular el factor de fade usando el mismo método que spawngrow
	const float t = 1.f - ((level.time - self->teleport_time).seconds() / self->wait);
	self->s.alpha = t * t; // Usar t^2 para un fade más suave como spawngrow

	self->nextthink = level.time + FRAME_TIME_MS;
}

void StartFadeOut(edict_t* ent) {
	// No iniciar fade out si el monstruo está vivo o ya está en fade
	if ((ent->health > 0 && !ent->deadflag) ||
		ent->is_fading_out ||
		(ent->monsterinfo.aiflags & (AI_CLEANUP_FADE | AI_CLEANUP_NORMAL))) {
		return;
	}

	// Configurar tiempos
	ent->teleport_time = level.time;
	ent->timestamp = level.time + FADE_LIFESPAN;
	ent->wait = FADE_LIFESPAN.seconds();

	// Configurar pensamiento
	ent->think = fade_out_think;
	ent->nextthink = level.time + FRAME_TIME_MS;

	// Marcar que está en proceso de fade
	ent->is_fading_out = true;

	// Configurar estados
	ent->solid = SOLID_NOT;
	ent->movetype = MOVETYPE_NONE;
	ent->takedamage = false;
	ent->svflags &= ~SVF_NOCLIENT;
	ent->s.renderfx &= ~RF_DOT_SHADOW;

	// Asegurar que la entidad está enlazada
	gi.linkentity(ent);
}

// This function is for when an entity's health reaches zero.
// It sets up the "dead" state.
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
		constexpr gtime_t FADE_START_DELAY = 4_sec;
		constexpr gtime_t FADE_DURATION = 3_sec;

		self->teleport_time = level.time + FADE_START_DELAY;
		self->timestamp = self->teleport_time + FADE_DURATION;
		self->wait = FADE_DURATION.seconds();

		self->monsterinfo.aiflags |= AI_CLEANUP_FADE;
		self->monsterinfo.aiflags &= ~AI_CLEANUP_NORMAL;
		self->s.renderfx &= ~RF_DOT_SHADOW;
	} else {
		self->timestamp = level.time + 2_sec;
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
		return;
	}

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
	for (uint32_t i = start_index; i < globals.num_edicts; i++) { // <-- MODIFIED START INDEX
		edict_t* ent = &g_edicts[i];

		// Basic validity checks
		if (!ent || !ent->inuse) {
			continue;
		}

		// Skip active items (can keep this check)
		if (ent->item) {
			continue;
		}

		// --- Conditions for identifying a stuck/lingering entity ---
		if ((ent->solid == SOLID_BSP || ent->solid == SOLID_BBOX) && ent->health <= 0) {
			bool stopped_thinking = (!ent->think || ent->nextthink <= level.time - 5_sec); // Corrected check
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