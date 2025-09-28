#include "../shared.h"
#include "../g_local.h"
#include "horde_performance.h"
#include "g_horde_phys.h"
#include "g_horde_benefits.h"

// *************************
// BARREL - Explosive barrels for Horde Mode
// *************************

constexpr float BARREL_BOUNCE_MULTIPLIER = 1.2f;
constexpr float BARREL_MIN_BOUNCE_SPEED = 100.0f;
constexpr float BARREL_BOUNCE_RANDOM = 50.0f;
constexpr float BARREL_VERTICAL_BOOST = 150.0f;
constexpr gtime_t BARREL_BURN_TIME = 800_ms;  // Increased for longer burn effect
constexpr float BARREL_CHAIN_EXPLOSION_RADIUS = 250.0f;
constexpr gtime_t BARREL_CHAIN_DELAY = 100_ms;

// Forward declarations
void barrel_explode(edict_t* self);
void barrel_burn(edict_t* self);
void barrel_remove(edict_t* self);
void barrel_think(edict_t* self);
void remove_barrels(edict_t* ent);

// Remove barrel and clean up player tracking
void barrel_remove(edict_t* self)
{
    if (!self || !self->inuse)
        return;

    self->takedamage = false;

    // Decrement player's barrel count
    if (self->chain && self->chain->client)
    {
        self->chain->client->resp.num_barrels--;

        // If this was a held barrel, clear it
        if (self->chain->client->resp.held_barrel == self)
            self->chain->client->resp.held_barrel = nullptr;

        // Clear this barrel from the tracking array
        for (int i = 0; i < BarrelConstants::MAX_BARRELS_PER_PLAYER; ++i)
        {
            if (self->chain->client->resp.deployed_barrels[i] == self)
            {
                self->chain->client->resp.deployed_barrels[i] = nullptr;
                break;
            }
        }
    }

    // Remove from targetable entities if tracked
    auto& vec = g_targetable_special_entities;
    vec.erase(std::remove(vec.begin(), vec.end(), self), vec.end());

    G_FreeEdict(self);
}

// Pain handler - barrels react to damage
PAIN(barrel_pain)(edict_t* self, edict_t* other, float kick, int damage, const mod_t& mod) -> void
{
    // Store who damaged us for attribution
    if (other && other != self)
        self->enemy = other;
}

// Death handler
DIE(barrel_die)(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod) -> void
{
    // Don't die multiple times
    if (self->deadflag)
        return;

    self->deadflag = true;
    // Don't set activator - always use chain (owner) for damage attribution

    // Big damage = instant explosion, small damage = delayed burn
    if (damage >= 90)
    {
        barrel_explode(self);
    }
    else
    {
        // Start burning with visual effect
        self->timestamp = level.time + BARREL_BURN_TIME;
        self->think = barrel_burn;
        self->nextthink = level.time + FRAME_TIME_S;

        // Stop absorbing damage when burning starts
        self->takedamage = false;
    }
}

// Deal AOE damage while burning to attract monsters
void barrel_burn_damage(edict_t* self)
{
    const float BURN_DAMAGE_RADIUS = 250.0f;
    const float BURN_DAMAGE_PER_SEC = 10.0f;
    const float damage_this_frame = BURN_DAMAGE_PER_SEC * gi.frame_time_s;

    // Find nearby entities
    const auto nearby_entities = HordePhys::g_monster_grid.QueryRadius(self->s.origin, BURN_DAMAGE_RADIUS);

    int monsters_damaged = 0;

    for (auto* ent : nearby_entities)
    {
        // Skip self, other barrels, and team members
        if (ent == self || ent->die == barrel_die || !ent->takedamage)
            continue;

        // Only damage monsters and players not on same team
        if (!(ent->svflags & SVF_MONSTER) && !ent->client)
            continue;

        // Check if it's a monster or enemy player
        if (ent->svflags & SVF_MONSTER)
        {
            float dist = range_to(self, ent);
            if (dist <= BURN_DAMAGE_RADIUS)
            {
                // Deal small damage to attract attention
                // Use chain (owner) if no activator is set
                edict_t* damage_attacker = self->activator ? self->activator : (self->chain ? self->chain : self);
                T_Damage(ent, self, damage_attacker,
                        vec3_origin, ent->s.origin, vec3_origin,
                        damage_this_frame, 0, DAMAGE_NONE, MOD_UNKNOWN);
                monsters_damaged++;
            }
        }
    }

    // Debug: show damage count periodically
    static gtime_t last_debug_time = 0_sec;
    if (monsters_damaged > 0 && level.time - last_debug_time > 1_sec)
    {
        if (self->chain && self->chain->client)
        {
    //        gi.LocClient_Print(self->chain, PRINT_HIGH, "Barrel burning: {} monsters in range\n", monsters_damaged);
        }
        last_debug_time = level.time;
    }
}

// Burning state before explosion
THINK(barrel_burn)(edict_t* self) -> void
{
    // Debug output - simplified format
    if (self->chain && self->chain->client)
    {
        int time_left = (int)((self->timestamp - level.time).seconds());
    //   gi.LocClient_Print(self->chain, PRINT_HIGH, "Barrel burning: {}s until explosion\n", time_left);
    }

    // Check if burn time has expired - if so, explode
    if (level.time >= self->timestamp)
    {
        if (self->chain && self->chain->client)
        //    gi.LocClient_Print(self->chain, PRINT_HIGH, "Barrel exploding NOW!\n");
        barrel_explode(self);
        return;
    }

    // Visual/audio burning effect
    self->s.effects |= EF_BARREL_EXPLODING;
    self->s.sound = gi.soundindex("weapons/bfg__l1a.wav");

    // Deal AOE damage to attract monsters
    barrel_burn_damage(self);

    self->think = barrel_burn;
    self->nextthink = level.time + FRAME_TIME_S;
}

// Find and trigger chain explosions
void barrel_chain_explosions(edict_t* self)
{
    // Find nearby barrels for chain reaction
    const auto nearby_entities = HordePhys::g_monster_grid.QueryRadius(self->s.origin, BARREL_CHAIN_EXPLOSION_RADIUS);

    for (auto* ent : nearby_entities)
    {
        // Check if it's another barrel
        if (ent != self && ent->die == barrel_die && ent->inuse && !ent->deadflag)
        {
            float dist = range_to(self, ent);
            if (dist <= BARREL_CHAIN_EXPLOSION_RADIUS)
            {
                // Delay chain explosions slightly for dramatic effect
                ent->timestamp = level.time + BARREL_CHAIN_DELAY;
                ent->think = barrel_burn;
                ent->nextthink = level.time + 10_hz;
                ent->activator = self->activator;
                ent->deadflag = true;
            }
        }
    }
}

// Main explosion function
THINK(barrel_explode)(edict_t* self) -> void
{
    self->takedamage = false;

    // Scale damage based on horde difficulty
    int damage = BarrelConstants::BARREL_BASE_DAMAGE;
    if (g_horde->integer)
    {
        damage = damage * (1.0f + (current_wave_level * 0.1f));
    }

    // Clean up barrel tracking BEFORE the entity is freed
    if (self->chain && self->chain->client)
    {
        self->chain->client->resp.num_barrels--;

        // If this was a held barrel, clear it
        if (self->chain->client->resp.held_barrel == self)
            self->chain->client->resp.held_barrel = nullptr;

        // Clear this barrel from the tracking array
        for (int i = 0; i < BarrelConstants::MAX_BARRELS_PER_PLAYER; ++i)
        {
            if (self->chain->client->resp.deployed_barrels[i] == self)
            {
                self->chain->client->resp.deployed_barrels[i] = nullptr;
                break;
            }
        }
    }

    // Radius damage - use chain if available, otherwise self
    edict_t* attacker = self->chain ? self->chain : self;
    T_RadiusDamage(self, attacker, (float)damage, nullptr,
                   BarrelConstants::BARREL_EXPLOSION_RADIUS, DAMAGE_NONE, MOD_G_SPLASH);

    // Throw gibs
    ThrowGibs(self, (1.5f * damage / 200.f), {
        { 2, "models/objects/debris1/tris.md2", GIB_METALLIC | GIB_DEBRIS },
        { 4, "models/objects/debris3/tris.md2", GIB_METALLIC | GIB_DEBRIS },
        { 8, "models/objects/debris2/tris.md2", GIB_METALLIC | GIB_DEBRIS }
    });

    // Chain explosions
    barrel_chain_explosions(self);

    // Explosion effect - THIS WILL FREE THE ENTITY!
    if (self->groundentity)
        BecomeExplosion2(self);
    else
        BecomeExplosion1(self);
}

// Touch function for barrels with summoned-style behavior
// Allows owner to push them like summoned strogg
TOUCH(barrel_summoned_touch)(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
    // Only the owner (chain) can push the barrel with special mechanics
    if (!other->client || !self->chain)
        return;

    // Check if this is the owner
    if (other != self->chain)
    {
        // Non-owners get normal collision
        return;
    }

    // Don't push if owner is not on ground or not touching properly
    if (!other->groundentity || !other_touching_self)
        return;

    // Check if player wants to pickup (looking down at barrel)
    if (other->client->v_angle[PITCH] > 45.0f) // Looking down more than 45 degrees
    {
        // Try to pick up the barrel
        if (!other->client->resp.held_barrel && !self->deadflag)
        {
            // Pick it up
            other->client->resp.held_barrel = self;
            self->solid = SOLID_NOT;
            self->svflags |= SVF_NOCLIENT; // Hide the barrel
            gi.linkentity(self);

            gi.sound(other, CHAN_AUTO, gi.soundindex("misc/w_pkup.wav"), 1, ATTN_NORM, 0);
          //  gi.LocClient_Print(other, PRINT_HIGH, "Picked up barrel\n");
        }
        return;
    }

    // Calculate push direction and strength
    vec3_t push_dir;
    float push_speed = 400.0f; // Same as summoned strogg

    // Check if owner is looking up (towards sky/roof)
    if (other->client->v_angle[PITCH] < -45.0f) // Looking up more than 45 degrees
    {
        // Vertical push - launch the barrel upward
        self->velocity[2] = push_speed * 1.5f; // Strong vertical push

        // Add some forward momentum based on view
        vec3_t forward;
        AngleVectors(other->client->v_angle, forward, nullptr, nullptr);
        self->velocity[0] += forward[0] * push_speed * 0.3f;
        self->velocity[1] += forward[1] * push_speed * 0.3f;

        // Make sure barrel is off ground for the jump
        self->groundentity = nullptr;

        // Add rotation for effect
        self->avelocity = {
            (frandom() * 2.0f - 1.0f) * 180,
            (frandom() * 2.0f - 1.0f) * 180,
            (frandom() * 2.0f - 1.0f) * 180
        };
    }
    else
    {
        // Horizontal push based on player's view direction
        vec3_t forward;
        AngleVectors(other->client->v_angle, forward, nullptr, nullptr);

        // Apply velocity directly for immediate push
        self->velocity[0] = forward[0] * push_speed;
        self->velocity[1] = forward[1] * push_speed;
        self->velocity[2] = 100; // Small upward component to help with obstacles

        // Add some rotation for visual effect
        self->avelocity = {
            (frandom() * 2.0f - 1.0f) * 90,
            (frandom() * 2.0f - 1.0f) * 90,
            (frandom() * 2.0f - 1.0f) * 90
        };
    }

    // Update physics
    gi.linkentity(self);
}

void barrel_touch(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self);
// // Touch function for physics interaction
// TOUCH(barrel_touch)(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
// {
//     // Don't interact with non-solid things
//     if ((!other->groundentity) || (other->groundentity == self))
//         return;

//     if (!other_touching_self)
//         return;

//     // Simple push for players and entities
//     if (other->client || (other->svflags & SVF_MONSTER))
//     {
//         // Simple directional push
//         vec3_t push_dir = self->s.origin - other->s.origin;
//         push_dir[2] = 0; // Keep it horizontal
//         push_dir.normalize();

//         // Simple fixed-distance push
//         vec3_t new_pos = self->s.origin + (push_dir * 20);

//         // Check if new position is valid
//         trace_t trace = gi.trace(self->s.origin, self->mins, self->maxs, new_pos, self, MASK_SOLID);
//         if (trace.fraction > 0)
//         {
//             self->s.origin = trace.endpos;
//             gi.linkentity(self);
//         }
//     }
// }

// Simple landing function
TOUCH(barrel_land)(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
    if (!other->inuse)
        return;

    // Only react to world and solid entities
    if (other != world && !(other->solid == SOLID_BSP))
        return;

    // If we hit ground, just settle
    if (tr.plane.normal && tr.plane.normal[2] > 0.7)
    {
        // Stop all movement
        self->velocity = {};
        self->avelocity = {};
        self->movetype = MOVETYPE_STEP;
        self->touch = barrel_summoned_touch;  // Use summoned touch behavior
        self->think = barrel_think;
        self->nextthink = level.time + FRAME_TIME_S;

        // Keep barrel upright
        self->s.angles[0] = 0;
        self->s.angles[2] = 0;

        gi.linkentity(self);

        // Landing sound
         gi.sound(self, CHAN_AUTO, gi.soundindex("tank/thud.wav"), 1, ATTN_NORM, 0);
    }
}

// Touch function for physics interaction
TOUCH(barrel_touch)(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
    float ratio;
    vec3_t v;

    // Don't interact with non-solid things
    if ((!other->groundentity) || (other->groundentity == self))
        return;

    if (!other_touching_self)
        return;

    // Special handling for players pushing barrels
    if ((other->svflags & SVF_PLAYER) && !(other->svflags & SVF_BOT))
    {
        // Apply stronger push force based on player velocity
        vec3_t push_dir = self->s.origin - other->s.origin;
        push_dir.normalize();

        // Use player's velocity magnitude to determine push strength
        float player_speed = other->velocity.length();
        float push_strength = std::max(50.0f, player_speed * 0.5f);

        // Apply push with some upward component if player is moving fast
        vec3_t push_velocity = push_dir * push_strength;
        if (player_speed > 200)
        {
            push_velocity[2] += 50; // Add upward component for running pushes
        }

        self->velocity += push_velocity;

        // Add some rotation for visual effect
        self->avelocity = {
            (frandom() * 2.0f - 1.0f) * 90,
            (frandom() * 2.0f - 1.0f) * 90,
            (frandom() * 2.0f - 1.0f) * 90
        };

        gi.linkentity(self);
        return;
    }

    // Standard push for other entities
    ratio = (float)other->mass / (float)self->mass;
    v = self->s.origin - other->s.origin;
    M_walkmove(self, vectoyaw(v), 20 * ratio * gi.frame_time_s);
}

// Bounce when thrown
TOUCH(barrel_bounce)(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
    if (!other->inuse)
        return;

    // Only bounce off world and solid entities
    if (other != world && !(other->solid == SOLID_BSP))
        return;

    if (tr.plane.normal)
    {
        vec3_t out{};
        float backoff = self->velocity.dot(tr.plane.normal) * BARREL_BOUNCE_MULTIPLIER;

        for (int i = 0; i < 3; i++)
        {
            float change = tr.plane.normal[i] * backoff;
            out[i] = self->velocity[i] - change;
            out[i] += (frandom() * 2.0f - 1.0f) * BARREL_BOUNCE_RANDOM;

            if (fabs(out[i]) < BARREL_MIN_BOUNCE_SPEED && out[i] != 0)
            {
                out[i] = (out[i] < 0 ? -BARREL_MIN_BOUNCE_SPEED : BARREL_MIN_BOUNCE_SPEED);
            }
        }

        // Add vertical boost for ground bounces
        if (tr.plane.normal[2] > 0.7)
        {
            out[2] += BARREL_VERTICAL_BOOST;
        }

        // Minimum velocity check
        if (out.length() < BARREL_MIN_BOUNCE_SPEED)
        {
            // Settle the barrel
            self->velocity = {};
            self->avelocity = {};
            self->movetype = MOVETYPE_STEP;
            self->touch = barrel_touch;
            self->think = barrel_think;
            self->nextthink = level.time + FRAME_TIME_S;
            gi.linkentity(self);
            return;
        }

        self->velocity = out;
        self->avelocity = {
            (frandom() * 2.0f - 1.0f) * 180,
            (frandom() * 2.0f - 1.0f) * 180,
            (frandom() * 2.0f - 1.0f) * 180
        };

        // Bounce sound
      //  gi.sound(self, CHAN_VOICE, gi.soundindex("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
        gi.sound(self, CHAN_AUTO, gi.soundindex("tank/thud.wav"), 1, ATTN_NORM, 0);
    }
}

// Main think function
THINK(barrel_think)(edict_t* self) -> void
{
    // Check lifetime
    if (self->timestamp > 0_sec && level.time > self->timestamp)
    {
        barrel_remove(self);
        return;
    }

    // World effects
    M_CatagorizePosition(self, self->s.origin, self->waterlevel, self->watertype);
    self->flags |= FL_IMMUNE_SLIME;
    M_WorldEffects(self);

    self->think = barrel_think;
    self->nextthink = level.time + FRAME_TIME_S;
}

// Initial setup
THINK(barrel_start)(edict_t* self) -> void
{
    M_droptofloor(self);

    if (self->health <= 0)
    {
        // Trigger the death function with small damage to start burning
        // This ensures proper death handling and burning sequence
        barrel_die(self, self, self, 30, self->s.origin, MOD_UNKNOWN);
        return;
    }

    self->think = barrel_think;
    self->nextthink = level.time + FRAME_TIME_S;
}

// Pick up a barrel
bool barrel_pickup(edict_t* player, edict_t* barrel)
{
    if (!player || !player->client || !barrel || !barrel->inuse)
        return false;

    // Check if already holding a barrel
    if (player->client->resp.held_barrel)
        return false;

    // Check distance
    float dist = range_to(player, barrel);
    if (dist > BarrelConstants::BARREL_PICKUP_RANGE)
        return false;

    // Can't pick up burning barrels
    if (barrel->deadflag || barrel->think == barrel_burn)
        return false;

    // Pick it up
    player->client->resp.held_barrel = barrel;
    barrel->solid = SOLID_NOT;
    barrel->svflags |= SVF_NOCLIENT; // Hide the barrel
    gi.linkentity(barrel);

    gi.sound(player, CHAN_AUTO, gi.soundindex("misc/w_pkup.wav"), 1, ATTN_NORM, 0);
 //   gi.LocClient_Print(player, PRINT_HIGH, "Picked up barrel\n");

    return true;
}

// Drop a held barrel
void barrel_drop(edict_t* player)
{
    if (!player || !player->client || !player->client->resp.held_barrel)
        return;

    edict_t* barrel = player->client->resp.held_barrel;

    // Place barrel in front of player
    vec3_t forward, start;
    AngleVectors(player->s.angles, forward, nullptr, nullptr);
    start = player->s.origin + (forward * 64);
    start[2] += 16; // Lift it up a bit

    barrel->s.origin = start;
    barrel->solid = SOLID_BBOX;
    barrel->svflags &= ~SVF_NOCLIENT; // Make visible again
    barrel->velocity = {};
    barrel->movetype = MOVETYPE_STEP;
    gi.linkentity(barrel);

    player->client->resp.held_barrel = nullptr;
  //  gi.LocClient_Print(player, PRINT_HIGH, "Dropped barrel\n");
}

// Visualize held barrel
void barrel_visualize(edict_t* player)
{
    if (!player || !player->client || !player->client->resp.held_barrel)
        return;

    edict_t* barrel = player->client->resp.held_barrel;

    // Update barrel position to follow player
    vec3_t forward, right, up, offset;
    AngleVectors(player->client->v_angle, forward, right, up);

    // Position barrel in front and slightly to the right
    offset = player->s.origin + (forward * 48) + (right * 12) + (up * -8);
    barrel->s.origin = offset;

    // Make it visible but not solid
    barrel->solid = SOLID_NOT;
    barrel->svflags &= ~SVF_NOCLIENT;
    gi.linkentity(barrel);
}

// Fire/throw a barrel
void fire_barrel(edict_t* self, const vec3_t& start, const vec3_t& aimdir)
{
    // Check barrel limit BEFORE placing a new one
    if (self && self->client)
    {
        if (ClientIsSpectating(self->client)){
         gi.Com_PrintFmt(" Can't do this while spect!\n");
            return;  
               }
               
        if (self->client->pers.health <= 0)
            return;

        // If at limit, remove oldest barrel first
        if (self->client->resp.num_barrels >= BarrelConstants::MAX_BARRELS_PER_PLAYER)
        {
            edict_t* oldest = self->client->resp.deployed_barrels[self->client->resp.oldest_barrel_idx];
            if (oldest && oldest->inuse && oldest->die == barrel_die)
            {
                // Force instant explosion of oldest barrel
               return;  //barrel_explode(oldest);
               gi.Com_PrintFmt(" Can't throw any more Barrels!\n");
            }
            // Don't increment counter yet, barrel_explode will decrement it
        }
    }

    edict_t* barrel;
    vec3_t dir;
    vec3_t forward, right, up;

    dir = vectoangles(aimdir);
    AngleVectors(dir, forward, right, up);

    barrel = G_Spawn();
    barrel->s.origin = start;
    barrel->velocity = aimdir * BarrelConstants::BARREL_THROW_SPEED;

    // Add simple arc to the throw
    const float gravityAdjustment = level.gravity / 800.f;
    barrel->velocity += up * (200 * gravityAdjustment);

    // No random spin - keep it upright
    barrel->avelocity = {};

    // Set up barrel properties
    barrel->movetype = MOVETYPE_BOUNCE;
    barrel->solid = SOLID_BBOX;
    barrel->mins = { -16, -16, 0 };
    barrel->maxs = { 16, 16, 40 };
    barrel->s.modelindex = gi.modelindex("models/objects/barrels/tris.md2");

    barrel->teammaster = self;
    barrel->chain = self;  // Set chain for summoned-style chain->teammastership

    // Set to NOTEAM so barrels can be damaged by anyone
    barrel->ctf_team = CTF_NOTEAM;

    // Set lifetime if deployed
    if (self->client)
    {
        barrel->timestamp = level.time + BarrelConstants::BARREL_LIFETIME;
    }

    barrel->touch = barrel_land;
    barrel->health = BarrelConstants::BARREL_BASE_HEALTH;
    barrel->takedamage = true;
    barrel->pain = barrel_pain;
    barrel->die = barrel_die;
    barrel->dmg = BarrelConstants::BARREL_BASE_DAMAGE;
    barrel->classname = "horde_barrel";
    barrel->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::BARREL);
    barrel->flags |= (FL_DAMAGEABLE | FL_TRAP);
    barrel->clipmask = MASK_PROJECTILE & ~CONTENTS_DEADMONSTER;

    if (self && self->client && !G_ShouldPlayersCollide(true))
        barrel->clipmask &= ~CONTENTS_PLAYER;

    gi.linkentity(barrel);

    // Track the barrel
    if (self->client)
    {
        // Find an empty slot or use the oldest slot
        int slot_to_use = -1;

        // First, try to find an empty slot
        for (int i = 0; i < BarrelConstants::MAX_BARRELS_PER_PLAYER; i++)
        {
            if (!self->client->resp.deployed_barrels[i] || !self->client->resp.deployed_barrels[i]->inuse)
            {
                slot_to_use = i;
                break;
            }
        }

        // If no empty slot, use the oldest barrel index
        if (slot_to_use == -1)
        {
            slot_to_use = self->client->resp.oldest_barrel_idx;
            self->client->resp.oldest_barrel_idx = (self->client->resp.oldest_barrel_idx + 1) % BarrelConstants::MAX_BARRELS_PER_PLAYER;
        }

        // Store the barrel in the slot
        self->client->resp.deployed_barrels[slot_to_use] = barrel;

        // Only increment if we're not at the limit
        if (self->client->resp.num_barrels < BarrelConstants::MAX_BARRELS_PER_PLAYER)
        {
            self->client->resp.num_barrels++;
        }

        // Clear held barrel if throwing
        if (self->client->resp.held_barrel)
            self->client->resp.held_barrel = nullptr;
    }

    // Add to targetable entities
    g_targetable_special_entities.push_back(barrel);
}

// Spawn function override for horde mode
void SP_misc_explobox(edict_t* self)
{
    // Only override in horde mode
    if (!g_horde->integer)
    {
        // Call original function (would need to be implemented or linked)
        return;
    }

    gi.modelindex("models/objects/debris1/tris.md2");
    gi.modelindex("models/objects/debris2/tris.md2");
    gi.modelindex("models/objects/debris3/tris.md2");
    gi.soundindex("weapons/bfg__l1a.wav");

    self->solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;
    self->model = "models/objects/barrels/tris.md2";
    self->s.modelindex = gi.modelindex(self->model);
    self->mins = { -16, -16, 0 };
    self->maxs = { 16, 16, 40 };

    if (!self->mass)
        self->mass = 100;

    // Scale health based on horde difficulty
    if (!self->health)
    {
        self->health = BarrelConstants::BARREL_BASE_HEALTH;
        if (g_horde->integer)
        {
            self->health = self->health * (1.0f + (current_wave_level * 0.05f));
        }
    }

    // Scale damage
    if (!self->dmg)
    {
        self->dmg = BarrelConstants::BARREL_BASE_DAMAGE;
        if (g_horde->integer)
        {
            self->dmg = self->dmg * (1.0f + (current_wave_level * 0.1f));
        }
    }

    self->die = barrel_die;
    self->takedamage = true;
    self->pain = barrel_pain;
    self->classname = "horde_barrel";
    self->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::BARREL);
    self->flags |= FL_TRAP;
    self->touch = barrel_touch;
    self->think = barrel_start;
    self->nextthink = level.time + 20_hz;

    // Map-placed barrels don't have an owner, so no chain
    self->chain = nullptr;

    gi.linkentity(self);
}

// Remove all barrels owned by a player
void remove_barrels(edict_t* ent)
{
    if (!ent || !ent->client)
        return;

    // Iterate through the player's deployed barrels array
    for (int i = 0; i < BarrelConstants::MAX_BARRELS_PER_PLAYER; i++)
    {
        edict_t* barrel = ent->client->resp.deployed_barrels[i];
        if (barrel && barrel->inuse)
        {
            barrel_remove(barrel);
            ent->client->resp.deployed_barrels[i] = nullptr;
        }
    }

    // Clear the held barrel if any
    if (ent->client->resp.held_barrel)
    {
        barrel_remove(ent->client->resp.held_barrel);
        ent->client->resp.held_barrel = nullptr;
    }

    // Reset barrel counters
    ent->client->resp.num_barrels = 0;
    ent->client->resp.oldest_barrel_idx = 0;
}

// Command function for testing barrels
void Cmd_Barrel_f(edict_t* ent)
{
    if (!ent->client)
        return;

    const char* arg = gi.argv(1);

    // "barrel" - throw a barrel
    if (!arg || !*arg || Q_strcasecmp(arg, "throw") == 0)
    {
        // Get aim direction
        vec3_t forward, right, start;
        AngleVectors(ent->client->v_angle, forward, right, nullptr);

        // Start position in front of player
        start = ent->s.origin;
        start[2] += ent->viewheight - 8;
        start = start + (forward * 24);

        // Fire the barrel
        fire_barrel(ent, start, forward);

      //  gi.LocClient_Print(ent, PRINT_HIGH, "Barrel thrown! ({}/{})\n",
       //                   ent->client->resp.num_barrels,
         //                 BarrelConstants::MAX_BARRELS_PER_PLAYER);
    }
    // "barrel pickup" - pickup nearest barrel
    else if (Q_strcasecmp(arg, "pickup") == 0)
    {
        // Find nearest barrel
        edict_t* best = nullptr;
        float best_dist = BarrelConstants::BARREL_PICKUP_RANGE;

        for (int i = 1; i < globals.num_edicts; i++)
        {
            edict_t* check = &g_edicts[i];
            if (!check->inuse)
                continue;
            if (check->die != barrel_die)
                continue;
            if (check->deadflag)
                continue;

            float dist = range_to(ent, check);
            if (dist < best_dist)
            {
                best = check;
                best_dist = dist;
            }
        }

        if (best)
        {
            if (barrel_pickup(ent, best))
            {
                gi.LocClient_Print(ent, PRINT_HIGH, "Barrel picked up!\n");
            }
            else
            {
                gi.LocClient_Print(ent, PRINT_HIGH, "Cannot pick up barrel.\n");
            }
        }
        else
        {
            gi.LocClient_Print(ent, PRINT_HIGH, "No barrel in range.\n");
        }
    }
    // "barrel drop" - drop held barrel
    else if (Q_strcasecmp(arg, "drop") == 0)
    {
        if (ent->client->resp.held_barrel)
        {
            barrel_drop(ent);
            gi.LocClient_Print(ent, PRINT_HIGH, "Barrel dropped.\n");
        }
        else
        {
            gi.LocClient_Print(ent, PRINT_HIGH, "Not holding a barrel.\n");
        }
    }
    // "barrel spawn" - spawn a barrel at player location
    else if (Q_strcasecmp(arg, "spawn") == 0)
    {
        edict_t* barrel = G_Spawn();
        barrel->s.origin = ent->s.origin + vec3_t{0, 0, 24};

        barrel->solid = SOLID_BBOX;
        barrel->movetype = MOVETYPE_STEP;
        barrel->s.modelindex = gi.modelindex("models/objects/barrels/tris.md2");
        barrel->mins = { -16, -16, 0 };
        barrel->maxs = { 16, 16, 40 };
        barrel->mass = 100;
        barrel->health = BarrelConstants::BARREL_BASE_HEALTH;
        barrel->dmg = BarrelConstants::BARREL_BASE_DAMAGE;
        barrel->die = barrel_die;
        barrel->takedamage = true;
        barrel->pain = barrel_pain;
        barrel->classname = "horde_barrel";
        barrel->special_type_id = static_cast<uint8_t>(horde::SpecialEntityTypeID::BARREL);
        barrel->flags |= FL_TRAP;
        barrel->touch = barrel_summoned_touch;  // Use summoned touch behavior
        barrel->think = barrel_start;
        barrel->nextthink = level.time + 20_hz;
        //barrel->chain->teammaster = ent;
        barrel->teammaster = ent;
        barrel->chain = ent;  // Set chain for summoned-style chain->teammastership

        gi.linkentity(barrel);
        gi.LocClient_Print(ent, PRINT_HIGH, "Barrel spawned at your location.\n");
    }
    // "barrel clear" - remove all barrels
    else if (Q_strcasecmp(arg, "clear") == 0)
    {
        int count = 0;
        for (int i = 1; i < globals.num_edicts; i++)
        {
            edict_t* check = &g_edicts[i];
            if (!check->inuse)
                continue;
            if (check->die != barrel_die)
                continue;

            barrel_remove(check);
            count++;
        }
        gi.LocClient_Print(ent, PRINT_HIGH, "Removed {} barrels.\n", count);
    }
    else
    {
        gi.LocClient_Print(ent, PRINT_HIGH, "Usage: barrel [throw|pickup|drop|spawn|clear]\n");
    }
}