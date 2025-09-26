// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_combat.c

#include "../g_local.h"

void M_SetEffects(edict_t *self);

/*
ROGUE
clean up heal targets for medic
*/
void cleanupHealTarget(edict_t *ent)
{
	ent->monsterinfo.healer = nullptr;
	ent->monsterinfo.healing_pause_time = 0_ms;  // Clear healing pause
	ent->takedamage = true;
	ent->monsterinfo.aiflags &= ~AI_RESURRECTING;
	M_SetEffects(ent);
}

namespace NukeDamageConstants {
    constexpr float KillZoneMultiplier = 2.0f;
    constexpr float MaxDamagePoints = 10000.0f;
    // Using gtime_t literals from your g_local.h
    constexpr gtime_t NukeEffectTimeDirectHit = 2.0_sec;
    constexpr gtime_t NukeEffectTimeObstructedClose = 1.5_sec;
    constexpr gtime_t NukeEffectTimeObstructedFar = 1.0_sec;
    constexpr float NukeEffectObstructedCloseDist = 2048.0f;
    constexpr float EntityCenterOffsetFactor = 0.5f;
}

/*
============
T_RadiusNukeDamage

Like T_RadiusDamage, but ignores walls for direct damage (skips CanDamage check for the damage part).
- Up to KILLZONE radius, do MaxDamagePoints.
- After that, do damage linearly out to KILLZONE2 radius using 'damage_falloff_base'.
- Also applies a "nuke_time" screen effect to all clients, duration based on LoS and distance.
============
*/
void T_RadiusNukeDamage(edict_t *inflictor, edict_t *attacker, float damage_falloff_base, edict_t *ignore, float radius, mod_t mod)
{
    if (!inflictor) {
        gi.Com_PrintFmt("T_RadiusNukeDamage: Null inflictor\n");
        return;
    }
    if (!attacker) {
        // Attacker might be world in some cases, but for a nuke, it's usually an entity.
        // If attacker can legitimately be null, this check might need adjustment or removal.
        gi.Com_PrintFmt("T_RadiusNukeDamage: Null attacker\n");
        return;
    }
    if (radius <= 0.0f) {
        // Potentially log this if it's unexpected, or just return silently.
        // gi.Com_PrintFmt("T_RadiusNukeDamage: Zero or negative radius ({})\n", radius);
        return;
    }
    if (level.intermissiontime) {
        return;
    }

    edict_t *ent = nullptr;
    vec3_t   v_diff_to_center;
    vec3_t   ent_aabb_center;
    vec3_t   dir_to_ent;
    float    dist_to_center;
    float    points;

    const float killzone1_radius = radius;
    const float killzone2_radius = radius * NukeDamageConstants::KillZoneMultiplier;
    const vec3_t& inflictor_origin = inflictor->s.origin; // s.origin is vec3_t

    // --- First Pass: Apply Damage to entities within killzone2_radius ---
    // findradius takes: edict_t* from, const vec3_t& org, float rad
    while ((ent = findradius(ent, inflictor_origin, killzone2_radius)) != nullptr)
    {
        if (ent == ignore)
            continue;
        if (!ent->inuse || !ent->takedamage)
            continue;
        // Check if the entity is a client, monster, or explicitly damageable
        if (!(ent->client || (ent->svflags & SVF_MONSTER) || (ent->flags & FL_DAMAGEABLE)))
            continue;

        // Calculate entity's AABB center
        // ent->mins and ent->maxs are vec3_t
        ent_aabb_center = ent->s.origin + (ent->mins + ent->maxs) * NukeDamageConstants::EntityCenterOffsetFactor;
        v_diff_to_center = ent_aabb_center - inflictor_origin;
        dist_to_center = v_diff_to_center.length();

        points = 0.0f;
        if (dist_to_center <= killzone1_radius)
        {
            if (ent->client) {
                ent->flags |= FL_NOGIB; // Prevent gibbing for direct max damage hits on players
            }
            points = NukeDamageConstants::MaxDamagePoints;
        }
        else if (dist_to_center <= killzone2_radius) // Damage falloff between killzone1 and killzone2
        {
            if (killzone1_radius > 0.0f) { // Avoid division by zero
                 points = (damage_falloff_base / killzone1_radius) * (killzone2_radius - dist_to_center);
            } else {
                 points = damage_falloff_base; // Or some other sensible default
            }
        }
        // No damage if dist_to_center > killzone2_radius (findradius should handle this, but good for clarity)

        if (points > 0.0f)
        {
            if (ent->client) {
                // Mark client for nuke screen effect, direct hit gets longest duration
                // level.time is gtime_t, NukeEffectTimeDirectHit is gtime_t
                ent->client->nuke_time = level.time + NukeDamageConstants::NukeEffectTimeDirectHit;
            }

            // Direction for knockback (from inflictor towards entity)
            dir_to_ent = ent->s.origin - inflictor_origin;
            dir_to_ent = safe_normalized(dir_to_ent); // Uses your vec3_t::safe_normalized

            // T_Damage(edict_t* targ, edict_t* inflictor, edict_t* attacker,
            //          const vec3_t& dir, const vec3_t& point, const vec3_t& normal,
            //          int damage, int knockback, damageflags_t dflags, mod_t mod)
            // For normal, vec3_origin is often used for radius damage where a specific impact normal isn't clear.
            T_Damage(ent, inflictor, attacker, dir_to_ent, inflictor_origin, vec3_origin,
                     static_cast<int>(points), static_cast<int>(points), DAMAGE_RADIUS, mod);
        }
    }

    // --- Second Pass: Apply nuke screen effect to ALL clients based on LoS and distance ---
    trace_t tr; // trace_t is standard
    float   dist_to_client_origin;

    // Iterate through client slots using game.maxclients
    for (uint32_t i = 0; i < game.maxclients; ++i) // Use uint32_t to match game.maxclients type if necessary
    {
        edict_t* client_ent = &g_edicts[i + 1]; // g_edicts[0] is worldspawn

        if (!client_ent->inuse || !client_ent->client)
            continue;

        // If client was directly hit by damage phase, nuke_time is already set to max.
        // This check ensures we don't override it with a potentially shorter LoS-based time.
        // Comparing gtime_t objects directly.
        if (client_ent->client->nuke_time == (level.time + NukeDamageConstants::NukeEffectTimeDirectHit))
            continue;

        // gi.traceline(const vec3_t& start, const vec3_t& end, const edict_t* passent, contents_t contentmask)
        tr = gi.traceline(inflictor_origin, client_ent->s.origin, inflictor, MASK_SOLID);

        gtime_t effect_duration_to_set = 0_ms; // Initialize with gtime_t literal

        if (tr.fraction == 1.0f) // Clear LoS
        {
            effect_duration_to_set = NukeDamageConstants::NukeEffectTimeDirectHit;
        }
        else // Obstructed
        {
            dist_to_client_origin = (client_ent->s.origin - inflictor_origin).length();
            if (dist_to_client_origin < NukeDamageConstants::NukeEffectObstructedCloseDist) {
                effect_duration_to_set = NukeDamageConstants::NukeEffectTimeObstructedClose;
            } else {
                effect_duration_to_set = NukeDamageConstants::NukeEffectTimeObstructedFar;
            }
        }
        
        // Set nuke_time, ensuring we don't reduce an existing effect
        // level.time and client_ent->client->nuke_time are gtime_t
        if (effect_duration_to_set > 0_ms) { // Compare with gtime_t literal
             client_ent->client->nuke_time = std::max(client_ent->client->nuke_time, level.time + effect_duration_to_set);
        }
    }
}

/*
============
T_RadiusClassDamage

Like T_RadiusDamage, but ignores anything with classname=ignoreClass
============
*/
namespace ClassDamageConstants {
    constexpr float SelfDamageMultiplier = 0.5f;
    constexpr float DistanceDamageFactor = 0.5f; // From: damage - 0.5f * v.length()
}

void T_RadiusClassDamage(edict_t *inflictor, edict_t *attacker, float damage,
                         const char *ignoreClass, float radius, mod_t mod)
{
    if (!inflictor || !attacker) {
        gi.Com_PrintFmt("T_RadiusClassDamage: Null inflictor or attacker\n");
        return;
    }
    if (!ignoreClass || !*ignoreClass) {
        gi.Com_PrintFmt("T_RadiusClassDamage: Null or empty ignoreClass\n");
        return;
    }
    if (radius <= 0.0f) { // Use 0.0f for float comparison
        return;
    }
    if (level.intermissiontime) {
        return;
    }

    edict_t *ent = nullptr;
    vec3_t   v_diff;
    vec3_t   ent_center_pos;
    vec3_t   dir_to_ent;
    float    dist;
    float    points;

    const vec3_t& inflictor_origin = inflictor->s.origin;

    while ((ent = findradius(ent, inflictor_origin, radius)) != nullptr)
    {
        if (!ent->inuse || !ent->takedamage) {
            continue;
        }
        // ent->classname is char*. strcmp is fine.
        if (ent->classname && strcmp(ent->classname, ignoreClass) == 0) {
            continue;
        }

        ent_center_pos = ent->s.origin + (ent->mins + ent->maxs) * 0.5f;
        v_diff = ent_center_pos - inflictor_origin;
        dist = v_diff.length(); // Uses sqrt internally

        // findradius checks against AABB, so center might be > radius.
        // Damage formula depends on this distance, so ensure it's within effective radius.
        if (dist > radius) {
            continue;
        }

        points = damage - (ClassDamageConstants::DistanceDamageFactor * dist);

        if (ent == attacker) {
            points *= ClassDamageConstants::SelfDamageMultiplier;
        }

        if (points > 0.0f) { // Use 0.0f for float comparison
            // CanDamage might involve traces or other checks.
            // Assuming inflictor is the direct cause/projectile for CanDamage checks.
            if (CanDamage(ent, inflictor)) {
                dir_to_ent = ent->s.origin - inflictor_origin;
                T_Damage(ent, inflictor, attacker, dir_to_ent, inflictor_origin, vec3_origin,
                         static_cast<int>(points), static_cast<int>(points), DAMAGE_RADIUS, mod);
            }
        }
    }
}

// ROGUE
// ********************