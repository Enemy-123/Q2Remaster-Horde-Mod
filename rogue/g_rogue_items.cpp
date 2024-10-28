// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.h"

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

	ent->client->ir_time = max(level.time, ent->client->ir_time) + 60_sec;

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

constexpr int MAX_SENTRIES = 3;

void Use_sentrygun(edict_t* ent, gitem_t* item)
{
	// Constantes
	static constexpr float MIN_FORWARD_DISTANCE = 22.0f;
	static constexpr float MAX_FORWARD_DISTANCE = 155.0f;
	static constexpr float MIN_HEIGHT = 50.0f;
	static constexpr float MAX_HEIGHT = 155.0f;

	if (!g_horde->integer)
	{
		gi.Client_Print(ent, PRINT_HIGH, "Need to be on Horde Mode to spawn a Sentry-Gun\n");
		return;
	}

	if (ClientIsSpectating(ent->client)) {
		gi.Client_Print(ent, PRINT_HIGH, "Need to be Non-Spect to spawn a Sentry-Gun\n");
		return;
	}
	// Comprueba si el jugador puede colocar una nueva torreta
	if (ent->svflags & SVF_BOT) {
		if (ent->client->num_sentries >= 1) {
			return;
		}
	}
	else {
		if (ent->client->num_sentries >= MAX_SENTRIES) {
			gi.Client_Print(ent, PRINT_HIGH, "You have reached the sentry gun limit.\n");
			return;
		}
	}


	// Cálculo de vectores de dirección
	vec3_t forward, right;
	vec3_t createPt, spawnPt;
	vec3_t ang = {
		ent->client->v_angle[PITCH],
		ent->client->v_angle[YAW],
		0  // Roll siempre en 0
	};

	AngleVectors(ang, forward, right, nullptr);

	// Versión corregida del cálculo de distancia
	float forwardDistance = static_cast<float>(irandom(
		static_cast<int32_t>(MIN_FORWARD_DISTANCE),
		static_cast<int32_t>(MAX_FORWARD_DISTANCE)
	));
	forwardDistance = std::max(MIN_FORWARD_DISTANCE, forwardDistance);

	// Versión corregida del cálculo de altura
	float height = static_cast<float>(irandom(
		static_cast<int32_t>(MIN_HEIGHT),
		static_cast<int32_t>(MAX_HEIGHT)
	));
	height = std::max(MIN_HEIGHT, height);


	// Cálculo del punto de spawn
	createPt = ent->s.origin + (forward * forwardDistance);

	// Verificación de punto de spawn válido con mejor mensaje de error
	if (!FindSpawnPoint(createPt, ent->mins, ent->maxs, spawnPt, true)) {
		gi.Client_Print(ent, PRINT_HIGH, "Cannot deploy sentry gun here. Try a different location.\n");
		return;
	}

	// Intento de spawn con manejo de errores
	if (fire_sentrygun(ent, spawnPt, forward, forwardDistance, height)) {
		// Actualización de inventario y contador
		ent->client->pers.inventory[item->id]--;
		ent->client->num_sentries++;
		// Nuevo mensaje después de construir la torreta
		gi.LocClient_Print(ent, PRINT_HIGH, "Sentry gun spawned. You have {}/{} sentry guns.\n",
			ent->client->num_sentries, MAX_SENTRIES);


	}
	else {
		gi.Client_Print(ent, PRINT_HIGH, "Failed to deploy sentry gun. Please try again.\n");
	}
}


bool Pickup_sentrygun(edict_t* ent, edict_t* other)
{
	int quantity;

	if (!G_IsDeathmatch()) // item is DM only
		return false;
	quantity = other->client->pers.inventory[ent->item->id];
	if (quantity >= 3) // FIXME - apply max to sentryguns
		return false;

	other->client->pers.inventory[ent->item->id]++;

	if (!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED))
		SetRespawn(ent, gtime_t::from_sec(ent->item->quantity));

	return true;
}

//void Use_Doppleganger(edict_t* ent, gitem_t* item)
//{
//	vec3_t forward, right;
//	vec3_t createPt, spawnPt;
//	vec3_t ang;
//
//	ang[PITCH] = 0;
//	ang[YAW] = ent->client->v_angle[YAW];
//	ang[ROLL] = 0;
//	AngleVectors(ang, forward, right, nullptr);
//
//	createPt = ent->s.origin + (forward * 48);
//
//	if (!FindSpawnPoint(createPt, ent->mins, ent->maxs, spawnPt, 32))
//		return;
//
//	if (!CheckGroundSpawnPoint(spawnPt, ent->mins, ent->maxs, 64, -1))
//		return;
//
//	ent->client->pers.inventory[item->id]--;
//
//	SpawnGrow_Spawn(spawnPt, 24.f, 48.f);
//	fire_doppleganger(ent, spawnPt, forward);
//}
//
//bool Pickup_Doppleganger(edict_t* ent, edict_t* other)
//{
//	int quantity;
//
//	if (!deathmatch->integer) // item is DM only
//		return false;
//
//	quantity = other->client->pers.inventory[ent->item->id];
//	if (quantity >= 1) // FIXME - apply max to dopplegangers
//		return false;
//
//	other->client->pers.inventory[ent->item->id]++;
//
//	if (!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED))
//		SetRespawn(ent, gtime_t::from_sec(ent->item->quantity));
//
//	return true;
//}

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