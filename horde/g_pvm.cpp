#include "g_pvm.h"
#include "g_character.h"
#include "../g_local.h"

// PvM (Player vs Monster) mode implementation
// - Backpack drops on death with all weapons/ammo
// - Respawn weapon system
// - Character persistence integration
//
// Setup: Set g_pvm 1, then restart the map. Horde mode will be auto-enabled.

// Backpack despawn time
constexpr gtime_t BACKPACK_DESPAWN_TIME = 60_sec;

// Default PvM respawn weapon
constexpr const char* DEFAULT_PVM_WEAPON = "Rocket Launcher";
constexpr int DEFAULT_PVM_ROCKETS = 10;

// Forward declarations
void PVM_BackpackThink(edict_t* backpack);
void PVM_BackpackTouch(edict_t* backpack, edict_t* other, const trace_t& tr, bool other_touching_self);

// Drop backpack on player death
void PVM_DropBackpack(edict_t* player)
{
    if (!player || !player->client)
        return;

    // Create backpack entity
    edict_t* backpack = G_Spawn();
    if (!backpack)
    {
        gi.Com_PrintFmt("PVM: Failed to spawn backpack for {}\n", player->client->pers.netname);
        return;
    }

    // Set basic properties
    backpack->classname = "item_backpack";
    backpack->s.modelindex = gi.modelindex("models/items/pack/tris.md2");
    backpack->s.effects = EF_ROTATE;
    backpack->solid = SOLID_TRIGGER;
    backpack->movetype = MOVETYPE_TOSS;
    backpack->touch = PVM_BackpackTouch;
    backpack->think = PVM_BackpackThink;
    backpack->nextthink = level.time + BACKPACK_DESPAWN_TIME;

    // Set bounds
    backpack->mins = { -15, -15, -15 };
    backpack->maxs = { 15, 15, 15 };

    // Store owner name for display
    backpack->message = G_CopyString(player->client->pers.netname, TAG_GAME);

    // Copy ALL inventory (weapons and ammo)
    backpack->client = player->client; // Temporarily link to access inventory
    for (int i = 0; i < IT_TOTAL; i++)
    {
        // Store in backpack's count field (we'll use a custom storage method)
        // For now, we'll allocate a client structure to store inventory
    }

    // Create a temporary client structure to store inventory
    gclient_t* temp_client = (gclient_t*)gi.TagMalloc(sizeof(gclient_t), TAG_GAME);
    if (temp_client)
    {
        memset(temp_client, 0, sizeof(gclient_t));
        temp_client->pers.inventory = player->client->pers.inventory;
        backpack->client = temp_client;
    }
    else
    {
        gi.Com_PrintFmt("PVM: Failed to allocate backpack storage\n");
        G_FreeEdict(backpack);
        return;
    }

    // Position backpack at player death location
    backpack->s.origin = player->s.origin;
    backpack->s.origin[2] += 16; // Lift it up slightly

    // Give it some velocity for visual effect
    backpack->velocity[0] = crandom() * 100;
    backpack->velocity[1] = crandom() * 100;
    backpack->velocity[2] = 200 + crandom() * 50;

    gi.linkentity(backpack);

    gi.Com_PrintFmt("PVM: Dropped backpack for {}\n", player->client->pers.netname);
}

// Backpack touch handler (pickup)
TOUCH(PVM_BackpackTouch) (edict_t* backpack, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
    if (!other || !other->client)
        return;

    // Don't pick up if dead or spectating
    if (other->health <= 0 || other->client->resp.spectator)
        return;

    if (!backpack->client)
    {
        gi.Com_PrintFmt("PVM: Backpack has no inventory data!\n");
        G_FreeEdict(backpack);
        return;
    }

    // Give all items from backpack to picker
    int weapons_picked = 0;
    int ammo_picked = 0;

    for (int i = 0; i < IT_TOTAL; i++)
    {
        int count = backpack->client->pers.inventory[i];
        if (count > 0)
        {
            const gitem_t* item = GetItemByIndex((item_id_t)i);
            if (!item)
                continue;

            // Add to player's inventory
            other->client->pers.inventory[i] += count;

            // Clamp to max
            if (item->ammo && item->ammo < AMMO_MAX)
            {
                int max_ammo = other->client->pers.max_ammo[item->ammo];
                if (other->client->pers.inventory[i] > max_ammo)
                    other->client->pers.inventory[i] = max_ammo;
                ammo_picked++;
            }
            else if (item->flags & IF_WEAPON)
            {
                weapons_picked++;
            }
        }
    }

    // Play pickup sound
    gi.sound(other, CHAN_AUTO, gi.soundindex("items/pkup.wav"), 1, ATTN_NORM, 0);

    // Notify player
    const char* owner_name = backpack->message ? backpack->message : "someone";
    gi.LocClient_Print(other, PRINT_HIGH, nullptr,
                      "Picked up {}'s backpack ({} weapons, {} ammo types)\n",
                      owner_name, weapons_picked, ammo_picked);

    // Free backpack
    if (backpack->message)
        gi.TagFree((void*)backpack->message);
    if (backpack->client)
        gi.TagFree(backpack->client);

    G_FreeEdict(backpack);
}

// Backpack think (despawn after timeout)
THINK(PVM_BackpackThink) (edict_t* backpack) -> void
{
    if (!backpack)
        return;

    // Despawn effect
    gi.sound(backpack, CHAN_AUTO, gi.soundindex("items/respawn1.wav"), 1, ATTN_IDLE, 0);

    // Free memory
    if (backpack->message)
        gi.TagFree((void*)backpack->message);
    if (backpack->client)
        gi.TagFree(backpack->client);

    G_FreeEdict(backpack);
}

// Give player their respawn weapon (called on spawn)
void PVM_GiveRespawnWeapon(edict_t* player)
{
    if (!player || !player->client)
        return;

    // Get saved respawn weapon from character
    const char* weapon_name = Character_GetRespawnWeapon(player);

    // Try to find the weapon
    gitem_t* weapon = FindItem(weapon_name);

    if (!weapon || !(weapon->flags & IF_WEAPON))
    {
        // Fallback to default
        gi.Com_PrintFmt("PVM: Invalid respawn weapon '{}' for {}, using default\n",
                        weapon_name, player->client->pers.netname);
        weapon = FindItem(DEFAULT_PVM_WEAPON);
    }

    if (!weapon)
    {
        gi.Com_PrintFmt("PVM: ERROR - Could not find default respawn weapon!\n");
        return;
    }

    // Give weapon
    player->client->pers.inventory[weapon->id] = 1;

    // Give ammo
    if (weapon->ammo && weapon->ammo < AMMO_MAX)
    {
        // Special handling for specific weapons
        if (Q_strcasecmp(weapon_name, "Rocket Launcher") == 0)
        {
            player->client->pers.inventory[weapon->ammo] = DEFAULT_PVM_ROCKETS;
        }
        else
        {
            // Give reasonable starting ammo (20% of max)
            int max_ammo = player->client->pers.max_ammo[weapon->ammo];
            player->client->pers.inventory[weapon->ammo] = max_ammo / 5;
            if (player->client->pers.inventory[weapon->ammo] < 10)
                player->client->pers.inventory[weapon->ammo] = 10;
        }
    }

    // Set as current weapon
    player->client->pers.weapon = weapon;
    player->client->pers.selected_item = weapon->id;

    gi.Com_PrintFmt("PVM: Gave {} respawn weapon: {}\n",
                    player->client->pers.netname, weapon_name);
}

// Check if PvM mode is active
bool IsPvMMode()
{
    extern cvar_t* g_pvm;
    return g_pvm && g_pvm->integer;
}
