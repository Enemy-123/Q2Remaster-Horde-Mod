// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.h"
#include "../shared.h"

// ================
// PMM
bool Pickup_Nuke(edict_t* ent, edict_t* other)
{
	int quantity;

	quantity = other->client->pers.inventory[ent->item->id];

	if (quantity >= 1)
		return false;

	if (G_IsCooperative() && !P_UseCoopInstancedItems() && (ent->item->flags & IF_STAY_COOP) && (quantity > 0))
		return false;

	other->client->pers.inventory[ent->item->id]++;

	if (G_IsDeathmatch())
	{
		if (!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED))
			SetRespawn(ent, gtime_t::from_sec(ent->item->quantity));
	}

	return true;
}

// ================
// PGM
void Use_IR(edict_t* ent, gitem_t* item)
{
	ent->client->pers.inventory[item->id]--;
	ent->client->ir_time = max(level.time, ent->client->ir_time) + 25_sec;
	ent->client->ir_tracking_active = true;  // Activar el tracking
	gi.sound(ent, CHAN_ITEM, gi.soundindex("misc/ir_start.wav"), 1, ATTN_NORM, 0);
}

void Use_Double(edict_t* ent, gitem_t* item)
{
	ent->client->pers.inventory[item->id]--;

	ent->client->double_time = max(level.time, ent->client->double_time) + 30_sec;

	gi.sound(ent, CHAN_ITEM, gi.soundindex("misc/ddamage1.wav"), 1, ATTN_NORM, 0);
}

void Use_Nuke(edict_t* ent, gitem_t* item)
{
	vec3_t forward, right, start;
	int	   speed;

	ent->client->pers.inventory[item->id]--;

	AngleVectors(ent->client->v_angle, forward, right, nullptr);

	start = ent->s.origin;
	speed = 100;
	fire_nuke(ent, start, forward, speed);
}

// Variable estática para rastrear el último uso del teleport por bots
static gtime_t last_bot_teleport_time = 0_sec;
constexpr gtime_t BOT_TELEPORT_COOLDOWN = 15_sec;  // Cooldown en segundos

static bool TryBotTeleport(const edict_t* ent)
{
	// Si es un bot (SVF_BOT flag)
	if (ent->svflags & SVF_BOT)
	{
		// Verificar si ha pasado suficiente tiempo desde el último uso
		if ((level.time - last_bot_teleport_time) < BOT_TELEPORT_COOLDOWN)
		{
			// No ha pasado suficiente tiempo - mostrar tiempo restante
			//float const remaining = (BOT_TELEPORT_COOLDOWN - (level.time - last_bot_teleport_time)).seconds();
//			gi.Client_Print(ent, PRINT_HIGH, "Bot teleport on cooldown. Please wait %.1f seconds.\n", remaining);
			return false;
		}

		// Actualizar el tiempo del último uso
		last_bot_teleport_time = level.time;
	}

	return true;
}

void Use_TeleportSelf(edict_t* ent, gitem_t* item)
{
	if (!g_horde->integer)
	{
		gi.Client_Print(ent, PRINT_HIGH, "Need to be on Horde Mode to Teleport Away\n");
		return;
	}

	if (ent->client && ent->health <= 0) // do nothing if dead
		return;


	if (ClientIsSpectating(ent->client)) {
		gi.Client_Print(ent, PRINT_HIGH, "Need to be Non-Spect to Teleport\n");
		return;
	}

	// Verificar el cooldown para bots antes de permitir el teleport
	if (!TryBotTeleport(ent))
	{
		return;
	}

	// Solo consume el item si el teleport fue exitoso
	if (TeleportSelf(ent)) {
		ent->client->pers.inventory[item->id]--;
	}
}

// ***************************
//  SPAWN TURRET LOGIC
// ***************************


// Variable estática para rastrear el último uso de sentry por bots
static gtime_t last_bot_sentry_time = 0_sec;
constexpr gtime_t BOT_SENTRY_COOLDOWN = 5_sec;

static bool TryBotSentry(const edict_t* ent)
{
	// Si es un bot (SVF_BOT flag)
	if (ent->svflags & SVF_BOT)
	{
		// Verificar si ha pasado suficiente tiempo desde el último uso
		if ((level.time - last_bot_sentry_time) < BOT_SENTRY_COOLDOWN)
		{
			return false;
		}
		// Actualizar el tiempo del último uso
		last_bot_sentry_time = level.time;
	}
	return true;
}

// This helper function remains the same. It's clean and does one job well.
static edict_t* SpawnSentryAtPoint(edict_t* owner, const vec3_t& spawn_origin, const vec3_t& spawn_angles)
{
    edict_t* turret = G_Spawn();
    if (!turret)
        return nullptr; // Return null on failure

    turret->monsterinfo.issummoned = true;
    turret->classname = "monster_sentrygun";
    turret->s.origin = spawn_origin;
    turret->s.angles = spawn_angles;
    turret->owner = owner;
    turret->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

    ApplyMonsterBonusFlags(turret);
    ED_CallSpawn(turret);

    if (turret->inuse) {
        SpawnGrow_Spawn(spawn_origin, 24.f, 48.f);
        return turret; // Return the new turret on success!
    }

    // If ED_CallSpawn failed, turret is not inuse
    G_FreeEdict(turret);
    return nullptr; // Return null on failure
}

// StroggSummonAtPoint removed - now using fire_strogg_summoner from g_strogg_summoner.cpp

// The new, all-in-one function to use the Sentry Gun item.
void Use_SentryGun(edict_t* ent, gitem_t* item)
{
	// --- 1. Initial validation checks ---
	// if (!g_horde->integer) {
	// 	gi.Client_Print(ent, PRINT_HIGH, "Need to be on Horde Mode to spawn a Sentry-Gun\n");
	// 	return;
	// }
	if (ClientIsSpectating(ent->client)) {
		gi.Client_Print(ent, PRINT_HIGH, "Need to be Non-Spect to spawn a Sentry-Gun\n");
		return;
	}
	if (!TryBotSentry(ent)) {
		return;
	}
	if (ent->svflags & SVF_BOT) {
		if (ent->client->resp.num_sentries >= 1) {
			return;
		}
	} else {
		if (ent->client->resp.num_sentries >= SentryConstants::MAX_SENTRIES_PER_PLAYER) {
			gi.Client_Print(ent, PRINT_HIGH, "You have reached the sentry gun limit.\n");
			return;
		}
	}

	// --- 2. Define placement constants (from old fire_sentrygun) ---
	constexpr vec3_t SENTRY_MINS = { -12, -12, -12 };
	constexpr vec3_t SENTRY_MAXS = { 12, 12, 12 };
	constexpr int MAX_ATTEMPTS = 16;
	constexpr float MIN_DEPLOY_HEIGHT = 20.0f; // Min height above player's feet

	// --- 3. Create a lambda to check if a spawn position is valid ---
	// This lambda encapsulates all the checks from the old `trySpawnPosition`.
	auto is_valid_spawn_location = [&](const vec3_t& pos) -> bool {
		vec3_t test_pos = pos;

		// Ensure it's not spawning below the player's feet
		if (test_pos.z - ent->s.origin.z < MIN_DEPLOY_HEIGHT) {
			return false;
		}

		// Check line of sight from player to the spawn point
		trace_t const trace = gi.traceline(ent->s.origin, test_pos, ent, MASK_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER);
		if (trace.fraction < 1.0f) {
			return false;
		}

		// Check if the final spot has enough room
		if (!CheckSpawnPoint(test_pos, SENTRY_MINS, SENTRY_MAXS)) {
			return false;
		}

		// All checks passed
		return true;
	};

	// --- 4. Placement Logic ---
	vec3_t forward;
	AngleVectors(ent->client->v_angle, forward, nullptr, nullptr);
	vec3_t const base_spawn_angles = { 0, ent->client->v_angle[YAW], 0 };

	// Generate random distance and height for placement
	float const distance = irandom(40.f, 125.f);
	float const height = irandom(50.f, 125.f);

	// Calculate the initial desired point
	vec3_t desired_point = ent->s.origin + (forward * distance);
	desired_point.z += height;

	vec3_t final_spawn_point;
	vec3_t final_spawn_angles = base_spawn_angles;
	bool found_spot = false;

	// ** Primary Attempt **
	if (is_valid_spawn_location(desired_point)) {
		final_spawn_point = desired_point;
		found_spot = true;
	}

	// ** Fallback Attempts (if primary failed) **
	if (!found_spot) {
		constexpr float RADIUS_MIN = 40.0f;
		constexpr float RADIUS_MAX = 100.0f;

		for (int i = 0; i < MAX_ATTEMPTS; i++) {
			float const random_angle_rad = frandom(2.0f * PIf);
			float const radius = frandom(RADIUS_MIN, RADIUS_MAX);
			
			vec3_t const offset = {
				cosf(random_angle_rad) * radius,
				sinf(random_angle_rad) * radius,
				0 // Height is already part of desired_point
			};

			vec3_t const test_point = desired_point + offset;

			if (is_valid_spawn_location(test_point)) {
				final_spawn_point = test_point;
				// Optional: Adjust the sentry's angle to face away from the center
				// final_spawn_angles[YAW] = vectoangles(offset)[YAW];
				found_spot = true;
				break; // Exit the loop once a spot is found
			}
		}
	}

   // --- 5. Final Action: Spawn or Fail ---
    if (found_spot) {
        // Call our improved helper function
        edict_t* turret = SpawnSentryAtPoint(ent, final_spawn_point, final_spawn_angles);

        if (turret) { // Check if the spawn was successful
            // The sentry was spawned successfully, now track it.
            for (int i = 0; i < SentryConstants::MAX_SENTRIES_PER_PLAYER; ++i) {
                if (ent->client->resp.deployed_sentries[i] == nullptr) {
                    ent->client->resp.deployed_sentries[i] = turret;
                    break;
                }
            }

            ent->client->pers.inventory[item->id]--;
            ent->client->resp.num_sentries++;
            gi.LocClient_Print(ent, PRINT_HIGH, "Sentry gun deployed. You have {}/{} sentry guns.\n",
                ent->client->resp.num_sentries, SentryConstants::MAX_SENTRIES_PER_PLAYER);
        } else {
            gi.Client_Print(ent, PRINT_HIGH, "Sentry deployment failed.\n");
        }
    } else {
        gi.Client_Print(ent, PRINT_HIGH, "Cannot find a suitable location to deploy sentry gun.\n");
    }
}

void Use_StroggSummon(edict_t* ent, gitem_t* item)
{
	// Forward to the new implementation in g_strogg_summoner.cpp
	extern void Use_StroggSummon_Impl(edict_t* ent, gitem_t* item);
	Use_StroggSummon_Impl(ent, item);
}

bool Pickup_SentryGun(edict_t* ent, edict_t* other)
{
	int quantity;

	// if (!G_IsDeathmatch()) // item is DM only
	// 	return false;
	quantity = other->client->pers.inventory[ent->item->id];
	if (quantity >= 3) // FIXME - apply max to sentryguns
		return false;

	other->client->pers.inventory[ent->item->id]++;

	if (!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED))
		SetRespawn(ent, gtime_t::from_sec(ent->item->quantity));

	return true;
}

bool Pickup_Teleport(edict_t* ent, edict_t* other)
{
	int quantity;

	if (!G_IsDeathmatch()) // item is DM only
		return false;
	quantity = other->client->pers.inventory[ent->item->id];
	if (quantity >= 3)
		return false;

	other->client->pers.inventory[ent->item->id]++;

	if (!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED))
		SetRespawn(ent, gtime_t::from_sec(ent->item->quantity));

	return true;
}

void Use_Doppleganger(edict_t* ent, gitem_t* item)
{
	vec3_t forward, right;
	vec3_t createPt, spawnPt;
	vec3_t ang;

	ang[PITCH] = 0;
	ang[YAW] = ent->client->v_angle[YAW];
	ang[ROLL] = 0;
	AngleVectors(ang, forward, right, nullptr);

	createPt = ent->s.origin + (forward * 48);

	if (!FindSpawnPoint(createPt, ent->mins, ent->maxs, spawnPt, 32))
		return;

	if (!CheckGroundSpawnPoint(spawnPt, ent->mins, ent->maxs, 64, -1))
		return;

	ent->client->pers.inventory[item->id]--;

	SpawnGrow_Spawn(spawnPt, 24.f, 48.f);
	fire_doppleganger(ent, spawnPt, forward);
}

bool Pickup_Doppleganger(edict_t* ent, edict_t* other)
{
	int quantity;

	if (!deathmatch->integer) // item is DM only
		return false;

	quantity = other->client->pers.inventory[ent->item->id];
	if (quantity >= 1) // FIXME - apply max to dopplegangers
		return false;

	other->client->pers.inventory[ent->item->id]++;

	if (!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED))
		SetRespawn(ent, gtime_t::from_sec(ent->item->quantity));

	return true;
}

bool Pickup_Sphere(edict_t* ent, edict_t* other)
{
	int quantity;

	if (other->client && other->client->owned_sphere)
	{
		//		gi.LocClient_Print(other, PRINT_HIGH, "$g_only_one_sphere_customer");
		return false;
	}

	quantity = other->client->pers.inventory[ent->item->id];
	if ((skill->integer == 1 && quantity >= 2) || (skill->integer >= 2 && quantity >= 1))
		return false;

	if ((G_IsCooperative()) && !P_UseCoopInstancedItems() && (ent->item->flags & IF_STAY_COOP) && (quantity > 0))
		return false;

	other->client->pers.inventory[ent->item->id]++;

	if (G_IsDeathmatch())
	{
		if (!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED))
			SetRespawn(ent, gtime_t::from_sec(ent->item->quantity));
		if (g_dm_instant_items->integer)
		{
			// PGM
			if (ent->item->use)
				ent->item->use(other, ent->item);
			else
				gi.Com_Print("Powerup has no use function!\n");
			// PGM
		}
	}

	return true;
}

void Use_Defender(edict_t* ent, gitem_t* item)
{
	if (ent->client && ent->client->owned_sphere)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "$g_only_one_sphere_time");
		return;
	}

	ent->client->pers.inventory[item->id]--;

	Defender_Launch(ent);
}

void Use_Hunter(edict_t* ent, gitem_t* item)
{
	if (ent->client && ent->client->owned_sphere)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "$g_only_one_sphere_time");
		return;
	}

	ent->client->pers.inventory[item->id]--;

	Hunter_Launch(ent);
}

void Use_Vengeance(edict_t* ent, gitem_t* item)
{
	if (ent->client && ent->client->owned_sphere)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "$g_only_one_sphere_time");
		return;
	}

	ent->client->pers.inventory[item->id]--;

	Vengeance_Launch(ent);
}

// PGM
// ================

//=================
// Item_TriggeredSpawn - create the item marked for spawn creation
//=================
USE(Item_TriggeredSpawn) (edict_t* self, edict_t* other, edict_t* activator) -> void
{
	self->svflags &= ~SVF_NOCLIENT;
	self->use = nullptr;

	if (self->spawnflags.has(SPAWNFLAG_ITEM_TOSS_SPAWN))
	{
		self->movetype = MOVETYPE_TOSS;
		vec3_t forward, right;

		AngleVectors(self->s.angles, forward, right, nullptr);
		self->s.origin = self->s.origin;
		self->s.origin[2] += 16;
		self->velocity = forward * 100;
		self->velocity[2] = 300;
	}

	if (!self->spawnflags.has(SPAWNFLAG_ITEM_NO_DROP))
	{
		if (self->item->id != IT_KEY_POWER_CUBE && self->item->id != IT_KEY_EXPLOSIVE_CHARGES) // leave them be on key_power_cube..
			self->spawnflags &= SPAWNFLAG_ITEM_NO_TOUCH;
	}
	else
		self->spawnflags &= ~SPAWNFLAG_ITEM_TRIGGER_SPAWN;

	droptofloor(self);
}

//=================
// SetTriggeredSpawn - set up an item to spawn in later.
//=================
void SetTriggeredSpawn(edict_t* ent)
{
	// don't do anything on key_power_cubes.
	if (ent->item->id == IT_KEY_POWER_CUBE || ent->item->id == IT_KEY_EXPLOSIVE_CHARGES)
		return;

	ent->think = nullptr;
	ent->nextthink = 0_ms;
	ent->use = Item_TriggeredSpawn;
	ent->svflags |= SVF_NOCLIENT;
	ent->solid = SOLID_NOT;
}