// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// Bombspell implementation ported from Vortex

#include "../g_local.h"
#include "horde_performance.h"
#include "g_horde_phys.h"

// Bomb spell constants
// NOTE: Player bombspell values are now loaded from the active player Lua config via g_config.bomb_spell
// Monster-only constants below (CARPETSLAM) remain hardcoded
constexpr float CARPETBOMB_ROOF_BUFFER = 32;

// Carpet slam constants (BerserkerKL variant - uses slam attacks)
constexpr float CARPETSLAM_DAMAGE = 35;  // Higher than regular slam (15)
constexpr float CARPETSLAM_KICK = 150.f;  // Higher knockback than regular slam (300)
constexpr float CARPETSLAM_RADIUS = 180;  // Slightly larger than regular slam (165)
constexpr gtime_t CARPETSLAM_DURATION = 4_sec;  // Shorter than carpetbomb
constexpr float CARPETSLAM_STEP_SIZE = 120;  // Faster forward movement

constexpr float BOMBAREA_WIDTH = 300;
constexpr float BOMBAREA_FLOOR_HEIGHT = 256;
constexpr gtime_t BOMBAREA_DURATION = 10_sec;
constexpr gtime_t BOMBAREA_STARTUP_DELAY = 1500_ms;  // Increased from 1s to 1.5s
constexpr float MAX_BOMB_RANGE = 1524;

constexpr float BOMBPERSON_RANGE = 1024;
constexpr float BOMBPERSON_WIDTH = 100;
constexpr gtime_t BOMBPERSON_DURATION = 10_sec;

constexpr float COST_FOR_BOMB = 50;
constexpr gtime_t DELAY_BOMB = 2_sec;

constexpr int CEILING_PITCH = 270;  // Looking up
constexpr int FLOOR_PITCH = 90;     // Looking down

// Forward declarations
void spawn_grenades(edict_t* ent, const vec3_t& origin, gtime_t time, int damage, int num);
void Grenade_Touch(edict_t* ent, edict_t* other, const trace_t& tr, bool other_touching_self);
void T_RadiusDamage_TeamSafe(edict_t* inflictor, edict_t* attacker, float damage,
                             edict_t* ignore, float radius, damageflags_t dflags, mod_t mod);
void T_SlamRadiusDamage(vec3_t point, edict_t* inflictor, edict_t* attacker, float damage,
                        float kick, edict_t* ignore, float radius, mod_t mod);

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
        float dist = sqrtf((inflictor->s.origin - center).lengthSquared());

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
// Dedicated vertical-drop dropper (mirrors the original Vortex spawn_grenades): each grenade
// falls straight down with the EF_GRENADE trail and explodes on contact or after its fuse.
// Callers spread the spawn origin themselves, so no per-grenade horizontal jitter here.
void spawn_grenades(edict_t* ent, const vec3_t& origin, gtime_t time, int damage, int num)
{
    if (!ent || num <= 0 || damage <= 0)
        return;

    for (int i = 0; i < num; i++)
    {
        edict_t* grenade = G_Spawn();
        if (!grenade)
            return;

        grenade->owner        = ent;
        grenade->s.origin     = origin;
        grenade->velocity     = { 0, 0, -300 };  // straight down, like Vortex
        grenade->avelocity    = { 200.f + crandom() * 10.f, 200.f + crandom() * 10.f, 200.f + crandom() * 10.f };
        grenade->movetype     = MOVETYPE_BOUNCE;
        grenade->clipmask     = MASK_SHOT;
        grenade->solid        = SOLID_BBOX;
        grenade->s.effects   |= EF_GRENADE;  // falling-grenade trail (the key visual)
        grenade->svflags     |= SVF_PROJECTILE;
        grenade->mins         = { -8, -8, -8 };
        grenade->maxs         = {  8,  8,  8 };
        grenade->s.modelindex = gi.modelindex("models/objects/grenade/tris.md2");
        grenade->dmg          = damage;
        grenade->dmg_radius   = g_config.bomb_spell.damage_radius;  // keep Horde's radius
        grenade->classname    = "grenade";
        grenade->touch        = Grenade_Touch;
        grenade->think        = Grenade_Explode;  // both read ent->dmg / ent->dmg_radius
        grenade->nextthink    = level.time + time;  // fuse passed in by caller

        // Keep attacker info so damage is attributed even if the owner dies first
        SetProjectileAttackerInfo(grenade, ent);

        gi.linkentity(grenade);
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
    vec3_t move_dist = forward * frandom(g_config.bomb_spell.damage_radius / 2, g_config.bomb_spell.damage_radius + 1);
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
        start.z += g_config.bomb_spell.step_size;
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
            start.z -= g_config.bomb_spell.step_size;
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
    vec3_t side_offset = right * (crandom() * frandom(g_config.bomb_spell.carpet_width / 4, g_config.bomb_spell.carpet_width / 2));
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
            self->nextthink = level.time + FRAME_TIME_MS;  // Vortex sweep speed (single frame per step)
            gi.linkentity(self);
            return;
        }
        else
        {
            G_FreeEdict(self);
            return;
        }
    }

    // Save original position before temporarily moving to explosion point
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
    self->nextthink = level.time + FRAME_TIME_MS;  // Vortex sweep speed (single frame per step)

    gi.linkentity(self);
}

// Carpet slam think function (BerserkerKL variant - uses slam attacks instead of explosions)
THINK(carpetslam_think)(edict_t* self) -> void
{
    float ceil_height;
    bool failed = false;
    vec3_t forward, right, start, end;
    trace_t tr, tr1;

    // Check owner validity - only for monsters
    // Also check if spell duration has expired
    if (!self->owner || !self->owner->inuse || self->owner->health <= 0 || level.time >= self->timestamp)
    {
        G_FreeEdict(self);
        return;
    }

    // Save current position before modifications
    vec3_t saved_origin = self->s.origin;

    // Move forward
    AngleVectors(self->s.angles, &forward, &right, nullptr);
    vec3_t move_dist = forward * CARPETSLAM_STEP_SIZE;
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
        start.z += CARPETSLAM_STEP_SIZE;
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
            start.z -= CARPETSLAM_STEP_SIZE;
            tr = gi.traceline(start, end, self, MASK_SOLID);
            if (tr.startsolid || tr.allsolid)
            {
                // For monsters, be more lenient - just move forward
                failed = false;
            }
        }
    }

    // Use calculated position for slam effect
    vec3_t slam_pos = tr.endpos;
    start = tr.endpos;

    // Make sure slam is in a valid location
    if ((gi.pointcontents(slam_pos) & CONTENTS_SOLID) || failed)
    {
        // Just continue without slam
        self->s.origin = start;
        self->nextthink = level.time + FRAME_TIME_MS * 2;
        gi.linkentity(self);
        return;
    }

    // Create slam effect at this position
    // Use TE_BERSERK_SLAM visual effect
    gi.WriteByte(svc_temp_entity);
    gi.WriteByte(TE_BERSERK_SLAM);
    gi.WritePosition(slam_pos);
    gi.WriteDir({ 0.f, 0.f, 1.f });
    gi.multicast(slam_pos, MULTICAST_PHS, false);

    // Play slam sounds
    gi.positioned_sound(slam_pos, self, CHAN_AUTO, gi.soundindex("mutant/thud1.wav"), 1, ATTN_NORM, 0);
    gi.positioned_sound(slam_pos, self, CHAN_VOICE, gi.soundindex("world/explod2.wav"), 0.75f, ATTN_NORM, 0);

    // Save original position before temporarily moving to slam point
    self->s.origin = slam_pos;

    // Use T_SlamRadiusDamage for the slam effect with upward knockback
    T_SlamRadiusDamage(slam_pos, self, self->owner, CARPETSLAM_DAMAGE, CARPETSLAM_KICK,
                       self->owner, CARPETSLAM_RADIUS, MOD_UNKNOWN);

    // Restore entity to starting position for next think
    self->s.origin = start;
    self->nextthink = level.time + FRAME_TIME_MS * 2;

    gi.linkentity(self);
}

// Carpet bomb function
void CarpetBomb(edict_t* ent)
{
    vec3_t forward;

    if (!ent || !ent->client || IsPlayerMenuProtected(ent) || ClientIsSpectating(ent->client))
        return;

    // Get player's bombspell skill level (default to 5 in Classic Mode)
    int8_t bombspell_level = 5;

    // Only in RPG Mode (vortex=1), check skills and power cubes
    if (g_vortex->integer != 0)
    {
        bombspell_level = ent->client->pers.skills.bombspell;

        // Check if player has bombspell skill
        if (bombspell_level == 0) {
            gi.LocClient_Print(ent, PRINT_HIGH, "You need to upgrade the BombSpell skill first!\n");
            return;
        }

        // Check power cube cost
        const int cost = 25;
        if (ent->client->pers.horde_power_cubes < cost) {
            gi.LocClient_Print(ent, PRINT_HIGH, "Not enough power cubes! Need {} cubes to cast bombspell.\n", cost);
            return;
        }

        // Deduct power cubes
        ent->client->pers.horde_power_cubes -= cost;
    }

    // Check cooldown
    if (ent->client->resp.bombspell_forward_cooldown > level.time) {
        float remaining_seconds = (ent->client->resp.bombspell_forward_cooldown - level.time).seconds();
        float remaining_display = std::floor(remaining_seconds * 10.0f) / 10.0f;
        gi.LocClient_Print(ent, PRINT_HIGH, "Bombspell on cooldown for {} seconds\n", remaining_display);
        return;
    }

    // Calculate damage based on skill level
    int damage = g_config.bomb_spell.initial_damage + (bombspell_level * g_config.bomb_spell.addon_damage);

    // Create bombspell entity
    edict_t* spell = G_Spawn();
    spell->think = carpetbomb_think;
    spell->nextthink = level.time + FRAME_TIME_MS;
    spell->s.origin = ent->s.origin;
    spell->move_origin = ent->s.origin;  // Save caster's origin for visibility checks
    spell->dmg = damage;
    spell->dmg_radius = g_config.bomb_spell.damage_radius;
    spell->timestamp = level.time + gtime_t::from_sec(g_config.bomb_spell.duration_sec);  // Use timestamp instead of delay
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
    ent->client->resp.bombspell_forward_cooldown = level.time + gtime_t::from_ms(g_config.bomb_spell.forward_cooldown_ms);
}

// Bomb area think function
THINK(bombarea_think)(edict_t* self) -> void
{
    float bombtime;
    vec3_t start;

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

    // Match Vortex: 1 grenade per think, spawned on the surface via a horizontal trace so
    // grenades never appear through walls. The emitter is already elevated to ceiling height
    // for floor targets (see BombArea), so grenades fall from above onto the area below.
    bool is_ceiling = (self->s.angles[PITCH] > 225 && self->s.angles[PITCH] < 315);

    start = self->s.origin;
    start.x += frandom(0, BOMBAREA_WIDTH / 2) * crandom();  // Vortex-style center-biased spread
    start.y += frandom(0, BOMBAREA_WIDTH / 2) * crandom();

    trace_t tr = gi.traceline(self->s.origin, start, self, MASK_SHOT);

    bombtime = is_ceiling ? (1.0f + 2.0f * frandom()) : (0.5f + 2.0f * frandom());
    spawn_grenades(self->owner, tr.endpos, gtime_t::from_sec(bombtime), self->dmg, 1);

    self->nextthink = level.time + 200_ms;  // Vortex steady patter (~0.2s)
}

// Bomb area function
void BombArea(edict_t* ent)//, float skill_mult, float cost_mult)
{
    vec3_t angles, offset;
    vec3_t forward, right, start, end;
    trace_t tr;

    if (!ent || !ent->client || IsPlayerMenuProtected(ent) || ClientIsSpectating(ent->client))
        return;

    // Get player's bombspell skill level (default to 5 in Classic Mode)
    int8_t bombspell_level = 5;

    // Only in RPG Mode (vortex=1), check skills and power cubes
    if (g_vortex->integer != 0)
    {
        bombspell_level = ent->client->pers.skills.bombspell;

        // Check if player has bombspell skill
        if (bombspell_level == 0) {
            gi.LocClient_Print(ent, PRINT_HIGH, "You need to upgrade the BombSpell skill first!\n");
            return;
        }

        // Check power cube cost
        const int cubes_cost = 25;
        if (ent->client->pers.horde_power_cubes < cubes_cost) {
            gi.LocClient_Print(ent, PRINT_HIGH, "Not enough power cubes! Need {} cubes to cast bombspell area.\n", cubes_cost);
            return;
        }

        // Deduct power cubes
        ent->client->pers.horde_power_cubes -= cubes_cost;
    }

    // Check cooldown
    if (ent->client->resp.bombspell_area_cooldown > level.time) {
        float remaining_seconds = (ent->client->resp.bombspell_area_cooldown - level.time).seconds();
        float remaining_display = std::floor(remaining_seconds * 10.0f) / 10.0f;
        gi.LocClient_Print(ent, PRINT_HIGH, "Bombspell area on cooldown for {} seconds\n", remaining_display);
        return;
    }

    // Calculate damage based on skill level
    int damage = g_config.bomb_spell.initial_damage + (bombspell_level * g_config.bomb_spell.addon_damage);

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
    bomb->dmg = damage;
    bomb->think = bombarea_think;
    bomb->s.angles = angles;

    // Elevate the emitter so grenades rain down from above (Vortex behavior). For a floor
    // target, lift toward ceiling height (up to BOMBAREA_FLOOR_HEIGHT); for a ceiling target
    // keep it at the ceiling so grenades drop straight down from it.
    vec3_t emitter_origin = tr.endpos;
    if (!is_ceiling)
    {
        vec3_t up_end = tr.endpos;
        up_end.z += BOMBAREA_FLOOR_HEIGHT;
        trace_t up_tr = gi.traceline(tr.endpos, up_end, ent, MASK_SOLID);
        emitter_origin = up_tr.endpos;
    }
    bomb->s.origin = emitter_origin;
    bomb->classname = "bombspell";  // Set classname for identification

    gi.linkentity(bomb);

    //gi.sound(ent, CHAN_ITEM, gi.soundindex("abilities/timebomb.wav"), 1, ATTN_NORM, 0);

    // Set cooldown
    ent->client->resp.bombspell_area_cooldown = level.time + gtime_t::from_ms(g_config.bomb_spell.area_cooldown_ms);
}

// Bomb person think function
THINK(bombperson_think)(edict_t* self) -> void
{
    int height, max_height;
    float thinktime;
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

    // Calculate drop rate (Vortex cadence): ramps from ~0.5s at cast down to a steady 0.25s
    // after the first ~2 seconds, so bombs start raining promptly instead of slowly.
    gtime_t bombtime = (self->timestamp - BOMBPERSON_DURATION) + 2_sec;  // max rate 2s after cast
    if (bombtime < level.time)
        bombtime = level.time;
    thinktime = 0.25f * ((bombtime + 1_sec) - level.time).seconds();
    if (thinktime < 0.25f)
        thinktime = 0.25f;

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
void BombPerson(edict_t* target, edict_t* owner, int damage)
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
    bomb->dmg = damage;
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

    // Get player's bombspell skill level (default to 5 in Classic Mode)
    int8_t bombspell_level = 5;

    // Only in RPG Mode (vortex=1), check skills and power cubes
    if (g_vortex->integer != 0)
    {
        bombspell_level = ent->client->pers.skills.bombspell;

        // Check if player has bombspell skill
        if (bombspell_level == 0) {
            gi.LocClient_Print(ent, PRINT_HIGH, "You need to upgrade the BombSpell skill first!\n");
            return;
        }

        // Check power cube cost
        const int person_cost = 25;
        if (ent->client->pers.horde_power_cubes < person_cost) {
            gi.LocClient_Print(ent, PRINT_HIGH, "Not enough power cubes! Need {} cubes to cast bombspell.\n", person_cost);
            return;
        }

        // Deduct power cubes
        ent->client->pers.horde_power_cubes -= person_cost;
    }

    // Calculate damage based on skill level
    int damage = g_config.bomb_spell.initial_damage + (bombspell_level * g_config.bomb_spell.addon_damage);

    AngleVectors(ent->client->v_angle, &forward, &right, nullptr);
    ent->client->kick_origin = forward * -3;
    offset = { 0, 7, (float)(ent->viewheight - 8) };
    P_ProjectSource(ent, ent->client->v_angle, offset, start, forward);
    end = start + forward * BOMBPERSON_RANGE;
    tr = gi.traceline(start, end, ent, MASK_SHOT);

    // Check if we hit a valid target
    if (tr.ent && tr.ent->takedamage && tr.ent != ent)
    {
        BombPerson(tr.ent, ent, damage);
    }
}
