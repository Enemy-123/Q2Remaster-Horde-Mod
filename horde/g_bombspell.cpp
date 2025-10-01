// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// Bombspell implementation ported from Vortex

#include "../g_local.h"
#include "horde_performance.h"
#include "g_horde_phys.h"

// Bomb spell constants
constexpr float CARPETBOMB_INITIAL_DAMAGE = 60;  // Reduced from 100
constexpr float CARPETBOMB_ADDON_DAMAGE = 10;    // Reduced from 20
constexpr float CARPETBOMB_DAMAGE_RADIUS = 150;
constexpr gtime_t CARPETBOMB_DURATION = 5_sec;
constexpr float CARPETBOMB_MAX_HEIGHT = 256;
constexpr float CARPETBOMB_ROOF_BUFFER = 32;
constexpr float CARPETBOMB_STEP_SIZE = 96;  // Reduced from 128 to slow down forward movement
constexpr float CARPETBOMB_CARPET_WIDTH = 200;

constexpr float BOMBAREA_WIDTH = 300;
constexpr float BOMBAREA_FLOOR_HEIGHT = 256;
constexpr gtime_t BOMBAREA_DURATION = 10_sec;
constexpr gtime_t BOMBAREA_STARTUP_DELAY = 1500_ms;  // Increased from 1s to 1.5s
constexpr float MAX_BOMB_RANGE = 1024;

constexpr float BOMBPERSON_RANGE = 1024;
constexpr float BOMBPERSON_WIDTH = 100;
constexpr gtime_t BOMBPERSON_DURATION = 10_sec;

constexpr float COST_FOR_BOMB = 50;
constexpr gtime_t DELAY_BOMB = 2_sec;

constexpr int CEILING_PITCH = 270;  // Looking up
constexpr int FLOOR_PITCH = 90;     // Looking down

// Forward declarations
void spawn_grenades(edict_t* ent, const vec3_t& origin, gtime_t time, int damage, int num);
void T_RadiusDamage_TeamSafe(edict_t* inflictor, edict_t* attacker, float damage,
                             edict_t* ignore, float radius, damageflags_t dflags, mod_t mod);

// Helper function for team-safe radius damage - optimized with spatial grid
void T_RadiusDamage_TeamSafe(edict_t* inflictor, edict_t* attacker, float damage,
                             edict_t* ignore, float radius, damageflags_t dflags, mod_t mod)
{
    if (!inflictor)
        return;

    // Use spatial grid for performance - same as main T_RadiusDamage
    auto nearby_entities = HordePhys::g_entity_grid.QueryRadiusFiltered(
        inflictor->s.origin, radius, HordePhys::EntityGrid::TYPE_ALL);

    for (edict_t* ent : nearby_entities)
    {
        if (ent == ignore || !ent->takedamage)
            continue;
        if (ent == attacker)  // Don't damage self
            continue;
        if (attacker && OnSameTeam(ent, attacker))  // Don't damage teammates
            continue;

        // Calculate distance and damage
        vec3_t center = ent->mins + ent->maxs;
        center = ent->s.origin + (center * 0.5f);
        float dist = sqrtf(HordePerf::g_distance_cache.GetDistanceSquared(center, inflictor->s.origin));

        // Reject entities whose actual impact point is outside the explosion radius
        if (dist > radius)
            continue;

        float points = damage - 0.5f * dist;

        if (points > 0 && CanDamage(ent, inflictor))
        {
            vec3_t dir = (center - inflictor->s.origin).normalized();
            T_Damage(ent, inflictor, attacker, dir, inflictor->s.origin, vec3_origin,
                    (int)points, (int)points, dflags, mod);
        }
    }
}

// Helper function to spawn grenades
void spawn_grenades(edict_t* ent, const vec3_t& origin, gtime_t time, int damage, int num)
{
    if (!ent || num <= 0 || damage <= 0)
        return;

    vec3_t start = origin;

    // Spawn grenades that fall downward
    for (int i = 0; i < num; i++)
    {
        // Start with straight down direction
        vec3_t dir = { 0, 0, -1 };

        // Add slight random horizontal spread for variety
        dir.x = crandom() * 0.05f;  // Very slight horizontal variation
        dir.y = crandom() * 0.05f;
        dir = dir.normalized();

        // Adjust starting position slightly for multiple grenades
        vec3_t spawn_pos = start;
        if (num > 1) {
            spawn_pos.x += crandom() * 10.0f;
            spawn_pos.y += crandom() * 10.0f;
        }

        // Fire grenade with minimal initial velocity (let gravity do the work)
        // Low speed and high up_adjust creates an arc that falls naturally
        fire_grenade(ent, spawn_pos, dir, damage, 200, time, CARPETBOMB_DAMAGE_RADIUS,
                    crandom() * 5.0f,  // Small random horizontal adjustment
                    50.0f + crandom() * 20.0f,  // Small upward velocity for arc
                    false);
    }
}

// Carpet bomb think function
THINK(carpetbomb_think)(edict_t* self) -> void
{
    float ceil_height;
    bool failed = false;
    vec3_t forward, right, start, end;
    trace_t tr, tr1;

    // Check owner validity - support both players and monsters
    if (!self->owner || !self->owner->inuse || self->owner->health <= 0 || level.time >= self->timestamp)
    {
        G_FreeEdict(self);
        return;
    }

    // Additional checks for player owners only
    if (self->owner->client && (IsPlayerMenuProtected(self->owner) || ClientIsSpectating(self->owner->client)))
    {
        G_FreeEdict(self);
        return;
    }

    // Save current position before modifications
    vec3_t saved_origin = self->s.origin;

    // Move forward
    AngleVectors(self->s.angles, &forward, &right, nullptr);
    vec3_t move_dist = forward * frandom(CARPETBOMB_DAMAGE_RADIUS / 2, CARPETBOMB_DAMAGE_RADIUS + 1);
    start = saved_origin + move_dist;

    // Trace horizontally and vertically to check movement
    tr = gi.traceline(saved_origin, start, self, MASK_SOLID);
    end = start;
    start.z += 1;
    end.z -= 8192;
    tr1 = gi.traceline(start, end, self, MASK_SOLID);
    start.z -= 1;

    // Check if we need to adjust height
    if (tr.fraction < 1 || start.z != tr1.endpos.z)
    {
        // Get current ceiling height
        end = start;
        end.z += 8192;
        tr = gi.traceline(saved_origin, end, self, MASK_SOLID);
        ceil_height = tr.endpos.z;

        // Push down from above desired position
        start.z += CARPETBOMB_STEP_SIZE;
        if (start.z > ceil_height)
            start.z = ceil_height;

        end = start;
        end.z -= 8192;
        tr = gi.traceline(start, end, self, MASK_SOLID);

        // Don't go through walls
        if (tr.allsolid)
            failed = true;

        // Try a bit lower
        if (tr.startsolid)
        {
            start.z -= CARPETBOMB_STEP_SIZE;
            tr = gi.traceline(start, end, self, MASK_SOLID);
            if (tr.startsolid || tr.allsolid)
            {
                // For monster owners, be more lenient
                if (self->count != 1)
                    failed = true;
            }
        }

        // Don't go into water if we aren't already submerged (skip for monsters)
        if (self->count != 1)
        {
            vec3_t water_check = tr.endpos;
            water_check.z += 8;
            if (!self->waterlevel && (gi.pointcontents(water_check) & MASK_WATER))
                failed = true;
        }
    }

    // Use calculated position for effects
    vec3_t effect_pos = tr.endpos;
    start = tr.endpos;

    // Spawn explosions on either side
    AngleVectors(self->s.angles, nullptr, &right, nullptr);
    vec3_t side_offset = right * (crandom() * frandom(CARPETBOMB_CARPET_WIDTH / 4, CARPETBOMB_CARPET_WIDTH / 2));
    end = effect_pos + side_offset;

    // Make sure path is wide enough
    tr = gi.traceline(effect_pos, end, self, MASK_SHOT);
    vec3_t explosion_pos = tr.endpos;
    explosion_pos.z += 32;

    // Make sure the caster can see this spot (skip for monster owners)
    bool is_monster_owner = (self->count == 1);
    if (!is_monster_owner)
    {
        trace_t vis_tr = gi.traceline(self->move_origin, explosion_pos, self, MASK_SOLID);
        if (vis_tr.fraction < 1.0f)
            failed = true;
    }

    // Make sure bombspell is in a valid location
    if ((gi.pointcontents(explosion_pos) & CONTENTS_SOLID) || failed)
    {
        // For monster owners, don't fail - just continue without explosion
        if (is_monster_owner)
        {
            self->s.origin = start;
            self->nextthink = level.time + FRAME_TIME_MS * 2;  // Doubled to slow down movement
            gi.linkentity(self);
            return;
        }
        else
        {
            G_FreeEdict(self);
            return;
        }
    }

    // Create temporary entity for explosion to avoid modifying self during damage
    vec3_t temp_origin = self->s.origin;
    self->s.origin = explosion_pos;

    // Use team-safe radius damage to avoid hitting owner and teammates
    T_RadiusDamage_TeamSafe(self, self->owner, (float)self->dmg, self->owner,
                           self->dmg_radius, DAMAGE_NONE, mod_t(MOD_BOMBS));

    // Write explosion effects
    gi.WriteByte(svc_temp_entity);
    gi.WriteByte(TE_EXPLOSION1);
    gi.WritePosition(explosion_pos);
    gi.multicast(explosion_pos, MULTICAST_PVS, false);

    // Restore entity to starting position for next think
    self->s.origin = start;
    self->nextthink = level.time + FRAME_TIME_MS * 2;  // Doubled to slow down movement

    gi.linkentity(self);
}

// Carpet bomb function
void CarpetBomb(edict_t* ent)
{
    vec3_t forward;

    if (!ent || !ent->client || IsPlayerMenuProtected(ent) || ClientIsSpectating(ent->client))
        return;

    // Check cooldown
    if (ent->client->resp.bombspell_forward_cooldown > level.time) {
        float remaining_seconds = (ent->client->resp.bombspell_forward_cooldown - level.time).seconds();
        float remaining_display = std::floor(remaining_seconds * 10.0f) / 10.0f;
        gi.LocClient_Print(ent, PRINT_HIGH, "Bombspell on cooldown for {} seconds\n", remaining_display);
        return;
    }

    // Deduct cost
    // Note: power_cube_index would need to be defined elsewhere in the mod
    // ent->client->pers.inventory[power_cube_index] -= COST_FOR_BOMB * cost_mult;

    // Create bombspell entity
    edict_t* spell = G_Spawn();
    spell->think = carpetbomb_think;
    spell->nextthink = level.time + FRAME_TIME_MS;
    spell->s.origin = ent->s.origin;
    spell->move_origin = ent->s.origin;  // Save caster's origin for visibility checks
    spell->dmg = CARPETBOMB_INITIAL_DAMAGE + CARPETBOMB_ADDON_DAMAGE;
    spell->dmg_radius = CARPETBOMB_DAMAGE_RADIUS;
    spell->timestamp = level.time + CARPETBOMB_DURATION;  // Use timestamp instead of delay
    spell->owner = ent;
    spell->mins = vec3_origin;
    spell->maxs = vec3_origin;
    spell->solid = SOLID_NOT;
    spell->svflags |= SVF_NOCLIENT | SVF_PROJECTILE;  // Mark as projectile to avoid save issues
    spell->classname = "bombspell";  // Set classname for identification

    // Set carpet direction
    AngleVectors(ent->client->v_angle, &forward, nullptr, nullptr);
    spell->s.angles = vectoangles(forward);

    gi.linkentity(spell);

    // Play sound effect if available
    //gi.sound(ent, CHAN_ITEM, gi.soundindex("abilities/carpetbomb.wav"), 1, ATTN_NORM, 0);

    // Set cooldown
    ent->client->resp.bombspell_forward_cooldown = level.time + 1500_ms;
}

// Bomb area think function
THINK(bombarea_think)(edict_t* self) -> void
{
    float thinktime, bombtime;
    vec3_t start, spawn_pos;

    // Check owner validity - support both players and monsters
    if (!self->owner || !self->owner->inuse || self->owner->health <= 0 || level.time >= self->timestamp)
    {
        G_FreeEdict(self);
        return;
    }

    // Additional checks for player owners only
    if (self->owner->client && (IsPlayerMenuProtected(self->owner) || ClientIsSpectating(self->owner->client)))
    {
        G_FreeEdict(self);
        return;
    }

    // Calculate time remaining and think time
    float time_remaining = (self->timestamp - level.time).seconds();
    thinktime = 0.3f + 0.2f * frandom();  // Slower interval (was 0.15-0.25, now 0.3-0.5)

    // Start from the area center
    start = self->s.origin;

    // Spread randomly around the area
    start.x += frandom(-BOMBAREA_WIDTH / 2, BOMBAREA_WIDTH / 2);
    start.y += frandom(-BOMBAREA_WIDTH / 2, BOMBAREA_WIDTH / 2);

    // Set spawn position above the target area
    spawn_pos = start;

    // Check what kind of surface we're bombing
    bool is_ceiling = (self->s.angles[PITCH] > 225 && self->s.angles[PITCH] < 315);

    if (is_ceiling)
    {
        spawn_pos.z -= 50;  // Spawn slightly below ceiling
    }
    else  // Floor bombing
    {
        spawn_pos.z += 200 + frandom(50, 150);  // Spawn 200-350 units above floor
    }

    // Spawn fewer grenades per wave for slower start (reduced from 2-3 to 1-2)
    bombtime = 0.5f + 2.0f * frandom();
    int grenade_count = irandom(1, 2);
    spawn_grenades(self->owner, spawn_pos, gtime_t::from_sec(bombtime), self->dmg, grenade_count);

    self->nextthink = level.time + gtime_t::from_sec(thinktime);
}

// Bomb area function
void BombArea(edict_t* ent)//, float skill_mult, float cost_mult)
{
    vec3_t angles, offset;
    vec3_t forward, right, start, end;
    trace_t tr;
    int cost = COST_FOR_BOMB;// * cost_mult;

    if (!ent || !ent->client || IsPlayerMenuProtected(ent) || ClientIsSpectating(ent->client))
        return;

    // Check cooldown
    if (ent->client->resp.bombspell_area_cooldown > level.time) {
        float remaining_seconds = (ent->client->resp.bombspell_area_cooldown - level.time).seconds();
        float remaining_display = std::floor(remaining_seconds * 10.0f) / 10.0f;
        gi.LocClient_Print(ent, PRINT_HIGH, "Bombspell area on cooldown for {} seconds\n", remaining_display);
        return;
    }

    AngleVectors(ent->client->v_angle, &forward, &right, nullptr);
    offset = { 0, 7, (float)(ent->viewheight - 8) };
    P_ProjectSource(ent, ent->client->v_angle, offset, start, forward);
    end = start + forward * MAX_BOMB_RANGE;
    tr = gi.traceline(start, end, ent, MASK_SOLID);

    // Check if we're looking at a floor or ceiling
    angles = vectoangles(tr.plane.normal);

    // Normal pointing up = floor, normal pointing down = ceiling
    bool is_floor = (tr.plane.normal.z > 0.7f);   // Normal pointing up
    bool is_ceiling = (tr.plane.normal.z < -0.7f); // Normal pointing down

    // Check if aiming at sky (looking up but didn't hit a ceiling)
    float pitch = ent->client->v_angle[PITCH];
    bool aiming_at_sky = (pitch < -45.0f && !is_ceiling && tr.fraction >= 1.0f);

    if (aiming_at_sky)
    {
        // Place bomb below visible sky - trace down from high point
        vec3_t sky_start = ent->s.origin;
        sky_start.z += 2048;  // Start high above player
        vec3_t sky_end = ent->s.origin;
        sky_end.z += 64;  // End just above player head
        trace_t sky_tr = gi.traceline(sky_start, sky_end, ent, MASK_SOLID);

        if (sky_tr.fraction < 1.0f)
        {
            // Found ceiling, place bomb there
            tr.endpos = sky_tr.endpos;
            angles = vectoangles(sky_tr.plane.normal);
            is_ceiling = true;
        }
        else
        {
            // No ceiling found, place at a reasonable height above player
            tr.endpos = ent->s.origin;
            tr.endpos.z += 512;
            angles[PITCH] = 270;  // Point down
            is_ceiling = true;
        }
    }
    else if (!is_floor && !is_ceiling)
    {
        gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "You must look at a ceiling or floor to cast this spell.\n");
        return;
    }

    edict_t* bomb = G_Spawn();
    bomb->solid = SOLID_NOT;
    bomb->svflags |= SVF_NOCLIENT | SVF_PROJECTILE;  // Mark as projectile to avoid save issues
    bomb->velocity = vec3_origin;
    bomb->mins = vec3_origin;
    bomb->maxs = vec3_origin;
    bomb->owner = ent;
    bomb->timestamp = level.time + BOMBAREA_DURATION + BOMBAREA_STARTUP_DELAY;  // Use timestamp instead of delay
    bomb->nextthink = level.time + BOMBAREA_STARTUP_DELAY;
    bomb->dmg = CARPETBOMB_INITIAL_DAMAGE + CARPETBOMB_ADDON_DAMAGE;// * skill_mult;
    bomb->think = bombarea_think;
    bomb->s.angles = angles;
    bomb->s.origin = tr.endpos;
    bomb->classname = "bombspell";  // Set classname for identification

    gi.linkentity(bomb);

    //gi.sound(ent, CHAN_ITEM, gi.soundindex("abilities/timebomb.wav"), 1, ATTN_NORM, 0);

    // Set cooldown
    ent->client->resp.bombspell_area_cooldown = level.time + 2500_ms;
}

// Bomb person think function
THINK(bombperson_think)(edict_t* self) -> void
{
    int height, max_height;
    float bombtime, thinktime;
    vec3_t start;
    trace_t tr;

    // Bomb self-terminates if the enemy dies or owner teleports away
    if (!self->owner || !self->owner->inuse || !self->owner->client ||
        !self->enemy || !self->enemy->inuse || self->enemy->health <= 0 ||
        level.time >= self->timestamp ||
        IsPlayerMenuProtected(self->owner) || ClientIsSpectating(self->owner->client))
    {
        // Remove curse from enemy if applicable
        G_FreeEdict(self);
        return;
    }

    // Calculate drop rate based on remaining time
    float time_remaining = (self->timestamp - level.time).seconds();
    bombtime = time_remaining - 2.0f;  // Leave 2 seconds buffer
    if (bombtime < 0)
        bombtime = 0;
    thinktime = 0.25f * (bombtime + 1.0f);

    // Update position to follow enemy, but don't modify self yet
    vec3_t target_pos = self->enemy->s.origin;

    // Get random drop height
    max_height = 250 - (20 * 1);  // Simplified skill level calculation
    if (max_height < 150)
        max_height = 150;
    height = frandom(50, max_height) + (int)self->enemy->maxs.z;

    // Drop bombs above target
    start = target_pos;
    start.z += height;
    tr = gi.traceline(target_pos, start, self->owner, MASK_SHOT);
    
    // Validate trace result
    if (tr.fraction == 0 || tr.allsolid)
    {
        // Skip this bomb if we can't place it
        self->nextthink = level.time + gtime_t::from_sec(thinktime);
        return;
    }
    
    start = tr.endpos;
    start.z--;

    // Spread randomly around target
    start.x += (BOMBPERSON_WIDTH / 2) * crandom();
    start.y += (BOMBPERSON_WIDTH / 2) * crandom();
    
    spawn_grenades(self->owner, start, gtime_t::from_sec(0.5f + 2.0f * frandom()), self->dmg, 1);
    
    // Now update self position to follow enemy
    self->s.origin = target_pos;
    self->nextthink = level.time + gtime_t::from_sec(thinktime);
}

// Bomb person function
void BombPerson(edict_t* target, edict_t* owner)//, float skill_mult)
{
    if (!target || !owner || IsPlayerMenuProtected(owner) ||
        (owner->client && ClientIsSpectating(owner->client)))
        return;

    //gi.sound(target, CHAN_ITEM, gi.soundindex("abilities/meteorlaunch.wav"), 1, ATTN_NORM, 0);

    if (target->client && !(target->svflags & SVF_MONSTER))
        gi.LocClient_Print(target, PRINT_HIGH, nullptr, "SOMEONE SET UP US THE BOMB!!\n");

    edict_t* bomb = G_Spawn();
    bomb->solid = SOLID_NOT;
    bomb->svflags |= SVF_NOCLIENT | SVF_PROJECTILE;  // Mark as projectile to avoid save issues
    bomb->velocity = vec3_origin;
    bomb->mins = vec3_origin;
    bomb->maxs = vec3_origin;
    bomb->owner = owner;
    bomb->enemy = target;
    bomb->timestamp = level.time + BOMBPERSON_DURATION;  // Use timestamp instead of delay
    bomb->dmg = CARPETBOMB_INITIAL_DAMAGE + CARPETBOMB_ADDON_DAMAGE;// * skill_mult;
    bomb->nextthink = level.time + 1_sec;
    bomb->think = bombperson_think;
    bomb->s.origin = target->s.origin;
    bomb->classname = "bombspell";  // Set classname for identification
    bomb->flags |= FL_TEAMSLAVE;  // Mark as a slave entity that shouldn't be saved

    gi.linkentity(bomb);
}

// Main command handler for bomb player
void Cmd_BombPlayer(edict_t* ent)
{
    int cost = COST_FOR_BOMB;// * cost_mult;
    vec3_t forward, right, start, end, offset;
    trace_t tr;

    if (!ent || !ent->client || IsPlayerMenuProtected(ent) || ClientIsSpectating(ent->client))
        return;

    // Check for ability delay
    // if (ent->client->ability_delay > level.time)
    //     return;

    // ent->client->ability_delay = level.time + DELAY_BOMB;

    // Write a nice effect so everyone knows we've cast a spell
    gi.WriteByte(svc_temp_entity);
    gi.WriteByte(TE_TELEPORT_EFFECT);
    gi.WritePosition(ent->s.origin);
    gi.multicast(ent->s.origin, MULTICAST_PVS, false);

    // ent->lastsound = level.framenum; // Not available in Q2 Remaster

    // Check command arguments for bomb type
    const char* args = gi.args();

    // Bomb forward (carpet bomb)
    if (strstr(args, "forward"))
    {
        CarpetBomb(ent);// skill_mult, cost_mult);
        return;
    }

    // Bomb area
    if (strstr(args, "area"))
    {
        BombArea(ent);// skill_mult, cost_mult);
        return;
    }

    // Default: bomb a person (targeted bomb)
    // ent->client->pers.inventory[power_cube_index] -= cost;

    AngleVectors(ent->client->v_angle, &forward, &right, nullptr);
    ent->client->kick_origin = forward * -3;
    offset = { 0, 7, (float)(ent->viewheight - 8) };
    P_ProjectSource(ent, ent->client->v_angle, offset, start, forward);
    end = start + forward * BOMBPERSON_RANGE;
    tr = gi.traceline(start, end, ent, MASK_SHOT);

    // Check if we hit a valid target
    if (tr.ent && tr.ent->takedamage && tr.ent != ent)
    {
        BombPerson(tr.ent, ent);//, skill_mult);
    }
}