#include "g_pvm.h"
#include "g_character.h"
#include "../g_local.h"
#include "horde_monster_data.h"
#include <vector>
#include <algorithm>

// PvM (Player vs Monster) mode implementation
// - Backpack drops on death with all weapons/ammo
// - Respawn weapon system
// - Character persistence integration
//
// Setup: Set pvm 1, then restart the map. Horde mode will be auto-enabled.

// Backpack despawn time
constexpr gtime_t BACKPACK_DESPAWN_TIME = 60_sec;

// Default PvM respawn weapon
constexpr const char *DEFAULT_PVM_WEAPON = "Rocket Launcher";
constexpr int DEFAULT_PVM_ROCKETS = 10;

// Forward declarations
extern const char *GetPlayerName(const edict_t *player);
void PVM_BackpackThink(edict_t *backpack);
void PVM_BackpackTouch(edict_t *backpack, edict_t *other, const trace_t &tr, bool other_touching_self);

// Drop backpack on player death
void PVM_DropBackpack(edict_t *player)
{
    if (!player || !player->client)
        return;

    // Create backpack entity
    edict_t *backpack = G_Spawn();
    if (!backpack)
    {
        //       gi.Com_PrintFmt("PVM: Failed to spawn backpack for {}\n", player->client->pers.netname);
        return;
    }

    // Set basic properties
    backpack->classname = "item_backpack";
    backpack->s.modelindex = gi.modelindex("models/items/pack/tris.md2");
    backpack->s.effects = EF_GIB; // Only gib effect, no rotate/bob
    backpack->solid = SOLID_TRIGGER;
    backpack->movetype = MOVETYPE_TOSS;
    backpack->touch = PVM_BackpackTouch;
    backpack->think = PVM_BackpackThink;
    backpack->nextthink = level.time + BACKPACK_DESPAWN_TIME;

    // Set bounds
    backpack->mins = {-15, -15, -15};
    backpack->maxs = {15, 15, 15};

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
    gclient_t *temp_client = (gclient_t *)gi.TagMalloc(sizeof(gclient_t), TAG_GAME);
    if (temp_client)
    {
        *temp_client = {};
        temp_client->pers.inventory = player->client->pers.inventory;
        backpack->client = temp_client;
    }
    else
    {
        //     gi.Com_PrintFmt("PVM: Failed to allocate backpack storage\n");
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

    //   gi.Com_PrintFmt("PVM: Dropped backpack for {}\n", player->client->pers.netname);
}

// Backpack touch handler (pickup)
TOUCH(PVM_BackpackTouch)(edict_t *backpack, edict_t *other, const trace_t &tr, bool other_touching_self)->void
{
    if (!other || !other->client)
        return;

    // Don't pick up if dead or spectating
    if (other->health <= 0 || other->client->resp.spectator)
        return;

    if (!backpack->client)
    {
        //      gi.Com_PrintFmt("PVM: Backpack has no inventory data!\n");
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
            const gitem_t *item = GetItemByIndex((item_id_t)i);
            if (!item)
                continue;

            // Skip techs - they should stay in the world as pickups
            if (item->flags & IF_TECH || item->flags & IF_ARMOR) //||  item->flags & IF_POWERUP)
            {
                //    gi.Com_PrintFmt("PVM: Backpack skipping tech {}\n", item->classname);
                continue;
            }

            // Add to player's inventory
            other->client->pers.inventory[i] += count;

            // Clamp to max
            if (item->ammo && item->ammo < IT_TOTAL)
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
    // const char *owner_name = backpack->message ? backpack->message : "someone";
    // gi.LocClient_Print(other, PRINT_HIGH, nullptr,
    //                    "Picked up {}'s backpack ({} weapons, {} ammo types)\n",
    //                    owner_name, weapons_picked, ammo_picked);

    // Free backpack
    if (backpack->message)
        gi.TagFree((void *)backpack->message);
    if (backpack->client)
        gi.TagFree(backpack->client);

    G_FreeEdict(backpack);
}

// Backpack think (despawn after timeout)
THINK(PVM_BackpackThink)(edict_t *backpack)->void
{
    if (!backpack)
        return;

    // Despawn effect
    gi.sound(backpack, CHAN_AUTO, gi.soundindex("items/respawn1.wav"), 1, ATTN_IDLE, 0);

    // Free memory
    if (backpack->message)
        gi.TagFree((void *)backpack->message);
    if (backpack->client)
        gi.TagFree(backpack->client);

    G_FreeEdict(backpack);
}

// Give player their respawn weapon (called on spawn)
void PVM_GiveRespawnWeapon(edict_t *player)
{
    if (!player || !player->client)
        return;

    // Get saved respawn weapon from character
    const char *weapon_name = Character_GetRespawnWeapon(player);

    // Try to find the weapon
    gitem_t *weapon = FindItem(weapon_name);

    if (!weapon || !(weapon->flags & IF_WEAPON))
    {
        // Fallback to default
        //      gi.Com_PrintFmt("PVM: Invalid respawn weapon '{}' for {}, using default\n",
        //                     weapon_name, player->client->pers.netname);
        weapon = FindItem(DEFAULT_PVM_WEAPON);
    }

    if (!weapon)
    {
        //        gi.Com_PrintFmt("PVM: ERROR - Could not find default respawn weapon!\n");
        return;
    }

    // Give weapon
    player->client->pers.inventory[weapon->id] = 1;

    // Give ammo (weapon->ammo is item_id, need to get the ammo tag)
    if (weapon->ammo)
    {
        gitem_t *ammo_item = GetItemByIndex(weapon->ammo);
        if (ammo_item && ammo_item->tag >= AMMO_BULLETS && ammo_item->tag < AMMO_MAX)
        {
            ammo_t ammo_type = (ammo_t)ammo_item->tag;

            // Special handling for specific weapons
            if (Q_strcasecmp(weapon_name, "Rocket Launcher") == 0)
            {
                player->client->pers.inventory[weapon->ammo] = DEFAULT_PVM_ROCKETS;
            }
            else
            {
                // Give reasonable starting ammo (20% of max)
                int max_ammo = player->client->pers.max_ammo[ammo_type];
                player->client->pers.inventory[weapon->ammo] = max_ammo / 5;
                if (player->client->pers.inventory[weapon->ammo] < 10)
                    player->client->pers.inventory[weapon->ammo] = 10;
            }

            //   gi.Com_PrintFmt("PVM: Gave {} {} ammo to {}\n",
            //      player->client->pers.inventory[weapon->ammo], ammo_item->pickup_name, GetPlayerName(player));
        }
    }

    // Set as current weapon
    player->client->pers.weapon = weapon;
    player->client->pers.selected_item = weapon->id;

    // gi.Com_PrintFmt("PVM: Gave {} respawn weapon: {}\n",
    //                  GetPlayerName(player), weapon_name);
}

// Check if PvM mode is active
bool IsPvMMode()
{
    extern cvar_t *pvm;
    return pvm && pvm->integer;
}

// Check if a monster type is valid for PvM mode
// PvM mode uses monsters from wave 8+ to keep precaching reasonable
bool PVM_IsValidMonster(int minWave)
{
    return minWave >= PVM_MIN_WAVE;
}


// Storage for randomly selected monsters for this map

// Monsters excluded from PVM random selection
// Add monster types here that should never appear in PVM mode
// Heap optimization: Changed from std::vector to std::array (compile-time, zero heap allocation)
static constexpr std::array<horde::MonsterTypeID, 20> g_pvm_excluded_monsters = {
    // Example exclusions (uncomment to exclude):
    horde::MonsterTypeID::JANITOR2,        // Special fog wave boss
    horde::MonsterTypeID::JANITOR,        // Special fog wave boss
    horde::MonsterTypeID::TANK_COMMANDER,        // Special fog wave boss
    horde::MonsterTypeID::TANK_64,        // Special fog wave boss
    horde::MonsterTypeID::GEKKKL,             // Special fog wave boss
    horde::MonsterTypeID::FLOATER_TRACKER,     // way too hateable
    horde::MonsterTypeID::MEDIC_COMMANDER,     // way too op
    horde::MonsterTypeID::DAEDALUS_BOMBER,     // way too op
    horde::MonsterTypeID::CARRIER_MINI, // way too op
    horde::MonsterTypeID::CARRIER, // way too op
    horde::MonsterTypeID::BOSS2_MINI, // way too op
    horde::MonsterTypeID::BOSS2_64,     // way too op
    horde::MonsterTypeID::BOSS2,        // way too op
    horde::MonsterTypeID::REDMUTANT,    // way too op
    horde::MonsterTypeID::PERRO_KL,     // way too op
    horde::MonsterTypeID::SHAMBLER_KL,  // way too op
    horde::MonsterTypeID::GUNCMDR_KL,   // way too op
    horde::MonsterTypeID::MAKRON_KL,    // way too op
    //horde::MonsterTypeID::TANK_SPAWNER, // way too op
    horde::MonsterTypeID::FIXBOT_KL,    // way too op
    horde::MonsterTypeID::BOSS2_KL,     // way too op
};

// Check if a monster type is excluded from PVM
bool PVM_IsMonsterExcluded(horde::MonsterTypeID typeId)
{
    return std::find(g_pvm_excluded_monsters.begin(), g_pvm_excluded_monsters.end(), typeId) != g_pvm_excluded_monsters.end();
}

// Heap optimization: Changed from std::vector to std::array (compile-time, zero heap allocation)
static std::array<horde::MonsterTypeID, PVM_RANDOM_MONSTER_COUNT> g_pvm_random_monsters;
static int g_pvm_random_monster_count = 0;

// Initialize random monster selection for the current map
void PVM_InitRandomMonsters()
{
    g_pvm_random_monster_count = 0;

    if (!IsPvMMode())
        return;

    // Collect all valid monsters (wave 8+, not excluded)
    std::vector<horde::MonsterTypeID> valid_monsters;
    for (size_t i = 0; i < MONSTER_DATA_COUNT; i++)
    {
        const auto& monster = monsterTypes[i];
        if (PVM_IsValidMonster(monster.minWave) && !PVM_IsMonsterExcluded(monster.typeId))
        {
            valid_monsters.push_back(monster.typeId);
        }
    }

    // If we have fewer than PVM_RANDOM_MONSTER_COUNT monsters, use all of them
    if (valid_monsters.size() <= PVM_RANDOM_MONSTER_COUNT)
    {
        g_pvm_random_monster_count = static_cast<int>(valid_monsters.size());
        std::copy(valid_monsters.begin(), valid_monsters.end(), g_pvm_random_monsters.begin());
        // gi.Com_PrintFmt("PVM: Using all {} available monsters (wave {}+)\n",
        //                valid_monsters.size(), PVM_MIN_WAVE);
        return;
    }

    // Randomly select PVM_RANDOM_MONSTER_COUNT monsters
    // Use Fisher-Yates shuffle for first N elements
    std::vector<horde::MonsterTypeID> shuffled = valid_monsters;
    for (int i = 0; i < PVM_RANDOM_MONSTER_COUNT; i++)
    {
        int j = i + (rand() % (shuffled.size() - i));
        std::swap(shuffled[i], shuffled[j]);
    }

    // Copy first PVM_RANDOM_MONSTER_COUNT elements to array
    g_pvm_random_monster_count = PVM_RANDOM_MONSTER_COUNT;
    std::copy(shuffled.begin(), shuffled.begin() + PVM_RANDOM_MONSTER_COUNT, g_pvm_random_monsters.begin());

    // gi.Com_PrintFmt("PVM: Selected {} random monsters for this map\n", PVM_RANDOM_MONSTER_COUNT);
}

// Get the count of randomly selected monsters for this map
int PVM_GetRandomMonsterCount()
{
    if (!IsPvMMode())
        return 0;
    return g_pvm_random_monster_count;
}

// Get the list of randomly selected monsters for this map
const horde::MonsterTypeID* PVM_GetRandomMonsters()
{
    if (!IsPvMMode() || g_pvm_random_monster_count == 0)
        return nullptr;

    return g_pvm_random_monsters.data();
}
