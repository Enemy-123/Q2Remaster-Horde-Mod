#ifndef G_SPAWN_H
#define G_SPAWN_H

#include "g_local.h"

// Declaración de funciones
void UpdateHUD(statusbar_t& sb, edict_t* ent);
void G_InitStatusbar(statusbar_t& sb);
void SP_worldspawn(edict_t* ent);
void SpawnEntities(const char* mapname, const char* entities, const char* spawnpoint);
void G_PrecacheInventoryItems(void);
void G_FindTeams(void);
void G_FixTeams(void);
const char* ED_ParseEdict(const char* data, edict_t* ent);
void ED_ParseField(const char* key, const char* value, edict_t* ent);
char* ED_NewString(const char* string);
void ED_CallSpawn(edict_t* ent);

#endif // G_SPAWN_H
