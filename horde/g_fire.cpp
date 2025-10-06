// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// Fire and Fireball implementation - modernized from Vortex

#include "../g_local.h"
#include "horde_performance.h"
#include "g_horde_phys.h"

// ============================================================================
// CONSTANTS
// ============================================================================

// Fire entity constants
constexpr int FIRE_IGNITE_FRAMES = 3;      // Frames 0-3 for ignition
constexpr int FIRE_BURN_START = 4;         // Frame 4 starts burning loop
constexpr int FIRE_BURN_END = 15;          // Frame 15 ends burning loop

// Fireball constants (loaded from g_config, but defaults here)
constexpr int FIREBALL_DEFAULT_DAMAGE = 45;
constexpr float FIREBALL_DEFAULT_RADIUS = 230.0f;
constexpr int FIREBALL_DEFAULT_SPEED = 600;
constexpr int FIREBALL_DEFAULT_FLAMES = 5;
constexpr int FIREBALL_DEFAULT_FLAME_DAMAGE = 10;
constexpr gtime_t FIREBALL_LIFETIME = 10_sec;

// Fire model paths
constexpr const char* FIRE_MODEL = "models/objects/fire/tris.md2";
constexpr const char* FIREBALL_MODEL = "models/objects/dball/tris.md2";

// Frame definitions for fire animation (flameb1-flameb11)
// These represent the 11 frames of the fire/flame effect
constexpr int FRAME_flameb1 = 0;
constexpr int FRAME_flameb2 = 1;
constexpr int FRAME_flameb3 = 2;
constexpr int FRAME_flameb4 = 3;
constexpr int FRAME_flameb5 = 4;
constexpr int FRAME_flameb6 = 5;
constexpr int FRAME_flameb7 = 6;
constexpr int FRAME_flameb8 = 7;
constexpr int FRAME_flameb9 = 8;
constexpr int FRAME_flameb10 = 9;
constexpr int FRAME_flameb11 = 10;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================



 // Helper function for team-safe radius damage - optimized with spatial grid
void T_RadiusDamage_TeamSafe(edict_t* inflictor, edict_t* attacker, float damage,
                 edict_t* ignore, float radius, damageflags_t dflags, mod_t mod);


// Burning effect functions
void burning_think(edict_t* self);
void apply_burning(edict_t* target, edict_t* attacker, int damage, gtime_t duration);
void remove_burning(edict_t* ent);

// Fire entity functions
void bfire_think(edict_t* self);
void bfire_touch(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self);

// Fireball entity functions
void fire_fireball_think(edict_t* self);
void fire_fireball_touch(edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self);
void fire_fireball_explode(edict_t* self, const trace_t* tr);

// Main fire functions
void ThrowFlame(edict_t* ent, const vec3_t& start, const vec3_t& forward,
                float dist, int speed, int damage, gtime_t ttl);
void fire_fireball(edict_t* self, const vec3_t& start, const vec3_t& aimdir,
                   int damage, float damage_radius, int speed, int flames, int flame_damage);

// ============================================================================
// BURNING EFFECT IMPLEMENTATION
// ============================================================================

// Burning think - applies DOT damage over time
THINK(burning_think)(edict_t* self) -> void
{
    if (!self || !self->inuse)
        return;

    // Check if burning effect should end
    if (!self->enemy || !self->enemy->inuse || self->enemy->health <= 0 ||
        level.time >= self->timestamp)
    {
        gi.unlinkentity(self);
        G_FreeEdict(self);
        return;
    }

    // Check if target is in water - water removes burning
    if (self->enemy->waterlevel > 0)
    {
        // // Visual effect for extinguishing
        // gi.WriteByte(svc_temp_entity);
        // gi.WriteByte(TE_MOREBLOOD);
        // gi.WritePosition(self->enemy->s.origin);
        // gi.multicast(self->enemy->s.origin, MULTICAST_PVS, false);

        gi.unlinkentity(self);
        G_FreeEdict(self);
        return;
    }

    // Apply burning damage
    T_Damage(self->enemy, self, self->owner, vec3_origin, self->enemy->s.origin,
            vec3_origin, self->dmg, 0, DAMAGE_NO_KNOCKBACK, MOD_LAVA);

    // // Visual fire effect
    // gi.WriteByte(svc_temp_entity);
    // gi.WriteByte(TE_FLAME);
    // gi.WritePosition(self->enemy->s.origin);
    // gi.multicast(self->enemy->s.origin, MULTICAST_PVS, false);

    // Continue burning
    self->nextthink = level.time + 850_ms;  // Damage every 0.5 seconds
}

// Apply burning effect to an entity
void apply_burning(edict_t* target, edict_t* attacker, int damage, gtime_t duration)
{
    if (!target || !target->inuse || !target->takedamage)
        return;

    if (ClientIsSpectating(target->client))
        return;

    // Check if target already has a burning effect
    // Find existing burning effect attached to this target
    for (edict_t* e = &g_edicts[1]; e < &g_edicts[globals.num_edicts]; e++)
    {
        if (!e->inuse || !e->classname)
            continue;

        if (strcmp(e->classname, "burning") == 0 && e->enemy == target)
        {
            // Refresh existing burn duration
            e->timestamp = level.time + duration;
            e->dmg = std::max(e->dmg, damage);  // Use higher damage if new burn is stronger
            return;
        }
    }

    // Create new burning effect entity
    edict_t* burn = G_Spawn();
    burn->classname = "burning";
    burn->enemy = target;
    burn->owner = attacker;
    burn->dmg = damage;
    burn->timestamp = level.time + duration;
    burn->think = burning_think;
    burn->nextthink = level.time + 850_ms;
    burn->solid = SOLID_NOT;
    burn->svflags |= SVF_NOCLIENT;

    gi.linkentity(burn);
}

// Remove burning effect from an entity
void remove_burning(edict_t* ent)
{
    if (!ent || !ent->inuse)
        return;

    // Find and remove burning effect
    for (edict_t* e = &g_edicts[1]; e < &g_edicts[globals.num_edicts]; e++)
    {
        if (!e->inuse || !e->classname)
            continue;

        if (strcmp(e->classname, "burning") == 0 && e->enemy == ent)
        {
            gi.unlinkentity(e);
            G_FreeEdict(e);
            return;
        }
    }
}

// ============================================================================
// FIRE ENTITY IMPLEMENTATION
// ============================================================================

THINK(bfire_think)(edict_t* self) -> void
{
    // Animate fire: frames 0-3 are ignition, 4-15 are burning loop
    if (self->s.frame > FIRE_IGNITE_FRAMES)
    {
        // Burning animation loop
        self->s.frame++;
        if (self->s.frame > FIRE_BURN_END)
            self->s.frame = FIRE_BURN_START;
    }
    else
    {
        // Ignition phase - increment to burning
        self->s.frame++;
    }

    // Check if fire should be removed
    if (!self->owner || !self->owner->inuse || self->owner->health <= 0 ||
        level.time >= self->timestamp || self->waterlevel > 0)
    {
        gi.unlinkentity(self);
        G_FreeEdict(self);
        return;
    }

    // Damage entities standing in fire
    // Use distance-based check instead of bounding box
    // Use pain_debounce_time for rate limiting
    if (!self->pain_debounce_time.milliseconds() || level.time >= self->pain_debounce_time)
    {
        constexpr float FIRE_DAMAGE_RADIUS = 24.0f;  // Small radius for tight damage area
        constexpr float FIRE_DAMAGE_RADIUS_SQ = FIRE_DAMAGE_RADIUS * FIRE_DAMAGE_RADIUS;

        // Find all entities near the fire
        for (edict_t* e = &g_edicts[1]; e < &g_edicts[globals.num_edicts]; e++)
        {
            if (!e->inuse || !e->takedamage)
                continue;

            if (e == self)
                continue;

            // Check distance from fire to entity center
            vec3_t entity_center = (e->absmin + e->absmax) * 0.5f;
            float dist_sq = (entity_center - self->s.origin).lengthSquared();

            if (dist_sq > FIRE_DAMAGE_RADIUS_SQ)
                continue;

            // Don't damage owner or teammates
            if (e == self->owner || (self->owner && OnSameTeam(self->owner, e)))
                continue;

            // Apply burning effect for standing in fire
            apply_burning(e, self->owner, self->dmg / 2, 10_sec);

            // Deal minor fire damage (most damage comes from burning DOT)
            T_Damage(e, self, self->owner, vec3_origin, self->s.origin,
                    vec3_origin, self->dmg / 5, 0, DAMAGE_NO_KNOCKBACK, MOD_LAVA);
        }

        // Rate limit damage to once per second
        self->pain_debounce_time = level.time + 1_sec;
    }

    self->nextthink = level.time + FRAME_TIME_MS;
}

TOUCH(bfire_touch)(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
    if (!other || !other->inuse)
        return;

    if (ClientIsSpectating(other->client))
        return;

    // Don't damage owner or teammates
    if (other == self->owner)
        return;

    if (self->owner && OnSameTeam(self->owner, other))
        return;

    // Only damage entities that can take damage
    if (!other->takedamage)
        return;

    // Apply burning DOT effect after contact
    // Burning lasts for 3 seconds and deals damage/2 per tick
    apply_burning(other, self->owner, self->dmg / 2, 3_sec);

    // Deal minor immediate damage (most damage comes from burning DOT)
    vec3_t normal = tr.plane.normal;
    T_Damage(other, self, self->owner, vec3_origin, self->s.origin,
            normal, self->dmg / 5, 0, DAMAGE_NO_KNOCKBACK, MOD_LAVA);
}

void ThrowFlame(edict_t* ent, const vec3_t& start, const vec3_t& forward,
                float dist, int speed, int damage, gtime_t ttl)
{
    if (!ent || !ent->inuse)
        return;

    // Create the fire entity
    edict_t* fire = G_Spawn();
    fire->s.origin = start;
    fire->s.old_origin = start;
    fire->s.angles = vectoangles(forward);
    fire->s.angles[PITCH] = 0; // Always face up

    // Set velocity - toss it forward with random upward component
    fire->velocity = forward * static_cast<float>(speed);

    // Add upward velocity (skip for totems, check would need mtype if available)
    fire->velocity.z += frandom(0.0f, 200.0f);

    fire->movetype = MOVETYPE_TOSS;
    fire->s.effects |= EF_BLASTER;
    fire->owner = ent;
    fire->dmg = damage;
    fire->classname = "fire";
    fire->s.sound = gi.soundindex("weapons/bfg__l1a.wav");
    fire->timestamp = level.time + ttl;
    fire->think = bfire_think;
    fire->touch = bfire_touch;
    fire->nextthink = level.time + FRAME_TIME_MS;
    fire->solid = SOLID_TRIGGER;
    fire->clipmask = MASK_SHOT;
    fire->s.modelindex = gi.modelindex(FIRE_MODEL);
    fire->s.scale = 0.4f;
    
    gi.linkentity(fire);
}

// ============================================================================
// FIREBALL ENTITY IMPLEMENTATION
// ============================================================================

void fire_fireball_explode(edict_t* self, const trace_t* tr)
{
    if (!self)
        return;

    // Don't call this more than once
    if (self->solid == SOLID_NOT)
        return;

    self->solid = SOLID_NOT;

    // Burn targets within explosion radius
    // Use spatial grid for performance if available
    for (edict_t* e = &g_edicts[1]; e < &g_edicts[globals.num_edicts]; e++)
    {
        if (!e->inuse || !e->takedamage)
            continue;

        if (ClientIsSpectating(e->client))
            continue;

        // Check distance
        float dist = (e->s.origin - self->s.origin).length();
        if (dist > self->dmg_radius)
            continue;

        // Skip owner and teammates
        if (e == self->owner || (self->owner && OnSameTeam(self->owner, e)))
            continue;

        // Check line of sight
        if (!CanDamage(e, self))
            continue;

        // Apply burning DOT effect from fireball explosion
        apply_burning(e, self->owner, self->radius_dmg, 7.5_sec);
    }

    // Loop for creating flame entities
    vec3_t forward;
    for (int i = 0; i < self->count; i++)
    {
        if (tr && tr->fraction < 1.0f) // If touching plane, move in direction of plane normal (away from it)
        {
            forward = tr->plane.normal;
        }
        else // Otherwise, move in direction opposite of velocity
        {
            forward = -self->velocity;
            forward = forward.normalized();
        }

        // Randomize aiming vector
        forward.x += 0.5f * crandom();
        forward.y += 0.5f * crandom();
        forward = forward.normalized();

        // Create the flame entities
        ThrowFlame(self->owner, self->s.origin, forward, 0,
                  irandom(50, 150), self->radius_dmg, 4.7_sec);
    }

    // Do radius damage to nearby entities
    T_RadiusDamage_TeamSafe(self, self->owner, (float)self->dmg, self->owner,
                           self->dmg_radius, DAMAGE_NONE, mod_t(MOD_FIREBALL));

    // Create explosion effect
    gi.WriteByte(svc_temp_entity);
    gi.WriteByte(TE_EXPLOSION1);
    gi.WritePosition(self->s.origin);
    gi.multicast(self->s.origin, MULTICAST_PHS, false);

    // Remove fireball entity next frame
    gi.unlinkentity(self);
    self->think = G_FreeEdict;
    self->nextthink = level.time + FRAME_TIME_MS;
}

TOUCH(fire_fireball_touch)(edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self) -> void
{
    // Remove fireball if owner dies or becomes invalid or if we touch a sky brush
    if (!ent->owner || !ent->owner->inuse || ent->owner->health <= 0 ||
        (tr.surface && (tr.surface->flags & SURF_SKY)))
    {
        gi.unlinkentity(ent);
        G_FreeEdict(ent);
        return;
    }

    if (other == ent->owner)
        return;

    // Explode on contact
    fire_fireball_explode(ent, &tr);
}

THINK(fire_fireball_think)(edict_t* self) -> void
{
    // Check lifetime and water
    if (level.time >= self->timestamp || self->waterlevel > 0)
    {
        gi.unlinkentity(self);
        G_FreeEdict(self);
        return;
    }

    // Extra particle effects - use spark effect with red/orange colors
    // Note: This may need adjustment based on available particle system
    // G_DrawSparks(self->s.origin, self->s.origin, 223, 231, 4, 1, 0, 0);

    // Set angles to point in the direction of movement
    self->s.angles = vectoangles(self->velocity);

    // Run model animation (frames 0-3 for fireball projectile)
    self->s.frame++;
    if (self->s.frame > 3)
        self->s.frame = 0;

    self->nextthink = level.time + FRAME_TIME_MS;
}

void fire_fireball(edict_t* self, const vec3_t& start, const vec3_t& aimdir,
                   int damage, float damage_radius, int speed, int flames, int flame_damage)
{
    if (!self || !self->inuse)
        return;

    if (ClientIsSpectating(self->client))
        return;

    // Get aiming angles
    vec3_t dir = vectoangles(aimdir);

    // Get directional vector (already have it in aimdir, but normalize to be safe)
    vec3_t forward = aimdir.normalized();

    // Spawn fireball entity
    edict_t* fireball = G_Spawn();
    fireball->s.origin = start;
    fireball->s.effects |= EF_BLASTER;
    fireball->movetype = MOVETYPE_TOSS;
    fireball->clipmask = MASK_SHOT;
    fireball->solid = SOLID_BBOX;
    fireball->s.modelindex = gi.modelindex(FIREBALL_MODEL);
    fireball->owner = self;
    fireball->touch = fire_fireball_touch;
    fireball->think = fire_fireball_think;
    fireball->dmg_radius = damage_radius;
    fireball->dmg = damage;
    fireball->radius_dmg = flame_damage;
    fireball->count = flames;
    fireball->classname = "fireball";
    fireball->timestamp = level.time + FIREBALL_LIFETIME;
    fireball->s.angles = dir;
    fireball->s.scale = 0.3f;
    fireball->s.effects |= EF_SPHERETRANS;
    gi.linkentity(fireball);

    fireball->nextthink = level.time + FRAME_TIME_MS;

    // Set velocity
    fireball->velocity = forward * static_cast<float>(speed);

    // Push up slightly (skip for bots/monsters with lockon)
    // Note: Check for client vs monster might be needed
    if (self->client)
        fireball->velocity.z += 150.0f;

    // Make it spin/roll
    fireball->avelocity = { 0, 0, 600 };

    // Play a sound
    gi.sound(fireball, CHAN_WEAPON, gi.soundindex("chick/chkatck2.wav"), 1, ATTN_NORM, 0);
}

// ============================================================================
// MONSTER-FRIENDLY WRAPPERS
// ============================================================================

// Cast a fireball suitable for monster use
// This function reads damage values from monsters.json weapon_damage.fireball
// Can be called from monster attack routines (e.g., shambler_fireball_update, BerserkCastFireballs)
void MonsterCastFireball(edict_t* self, const vec3_t& start, const vec3_t& target_pos)
{
    if (!self || !self->inuse)
        return;

    // Check if we have a valid target
    if (!M_HasValidTarget(self))
        return;

    // Calculate direction to target
    vec3_t dir = DirectionTo(start, target_pos);

    // Get damage from monster weapon config
    // Use global config values if available, otherwise use defaults
    int damage = g_config.global_weapon_damage.fireball > 0 ?
                 g_config.global_weapon_damage.fireball : FIREBALL_DEFAULT_DAMAGE;
    float radius = g_config.global_weapon_radius.fireball > 0 ?
                   g_config.global_weapon_radius.fireball : FIREBALL_DEFAULT_RADIUS;
    int speed = g_config.global_weapon_speed.fireball > 0 ?
                g_config.global_weapon_speed.fireball : FIREBALL_DEFAULT_SPEED;
    int flames = FIREBALL_DEFAULT_FLAMES;
    int flame_dmg = FIREBALL_DEFAULT_FLAME_DAMAGE;

    // Fire the fireball
    fire_fireball(self, start, dir, damage, radius, speed, flames, flame_dmg);
}

// Simpler wrapper that calculates start position automatically
void MonsterCastFireballAtEnemy(edict_t* self)
{
    if (!self || !self->inuse || !M_HasValidTarget(self))
        return;

    // Get monster's position for projectile start
    vec3_t f, r;
    AngleVectors(self->s.angles, f, r, nullptr);
    vec3_t start = G_ProjectSource(self->s.origin, { 0.f, 0.f, 24.f }, f, r);

    // Aim at enemy
    vec3_t target = self->enemy->s.origin;
    target.z += self->enemy->viewheight;

    MonsterCastFireball(self, start, target);
}
