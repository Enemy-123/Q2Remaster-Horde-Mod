#include "../shared.h"
#include "../g_local.h"
#include "horde_performance.h"
#include "g_horde_phys.h"
#include "g_horde_benefits.h"

// Forward declaration
void T_RadiusDamage_TeamSafe(edict_t* inflictor, edict_t* attacker, float damage,
                             edict_t* ignore, float radius, damageflags_t dflags, mod_t mod);

// *************************
// BARREL - Explosive barrels for Horde Mode
// *************************

// Barrel gravity gun cvars
cvar_t* barrel_hold_speed;
cvar_t* barrel_throw_speed;

// Initialize barrel cvars
void Barrel_InitGame(void)
{
	barrel_hold_speed = gi.cvar("barrel_hold_speed", "800", CVAR_NOFLAGS);
	barrel_throw_speed = gi.cvar("barrel_throw_speed", "1200", CVAR_NOFLAGS);
}

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

// Helper function to clean up barrel tracking for a player
static void barrel_cleanup_tracking(edict_t* barrel)
{
    if (!barrel || !barrel->chain || !barrel->chain->client)
        return;

    auto* client = barrel->chain->client;

    // Decrement barrel count with bounds checking
    if (client->resp.num_barrels > 0) {
        client->resp.num_barrels--;
    }

    // Clear held barrel reference if this is it
    if (client->resp.held_barrel == barrel)
        client->resp.held_barrel = nullptr;

    // Clear from deployed barrels array
    for (int i = 0; i < BarrelConstants::MAX_BARRELS_PER_PLAYER; ++i)
    {
        if (client->resp.deployed_barrels[i] == barrel)
        {
            client->resp.deployed_barrels[i] = nullptr;
            break;
        }
    }
}

// Remove barrel and clean up player tracking
void barrel_remove(edict_t* self)
{
    if (!self || !self->inuse)
        return;

    self->takedamage = false;

    // Clean up player tracking
    barrel_cleanup_tracking(self);

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

        // Stop absorbing damage and make it non-blocking
        self->takedamage = false;
        self->clipmask = CONTENTS_NONE;  // Don't clip against anything (allows bullets to pass through)
        gi.linkentity(self);
    }
}

// Deal AOE damage while burning to attract monsters
void barrel_burn_damage(edict_t* self)
{
    if (!self || !self->inuse)
        return;

    constexpr float BURN_DAMAGE_RADIUS = 250.0f;
    constexpr float BURN_DAMAGE_PER_SEC = 10.0f;
    constexpr gtime_t target_cooldown_react = 1.5_sec;
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
                T_Damage(ent, damage_attacker, damage_attacker,
                        vec3_origin, ent->s.origin, vec3_origin,
                        damage_this_frame, 0, DAMAGE_NONE, MOD_UNKNOWN);

                // Make monsters target the barrel directly (like pathfinding "bad area" detection)
                if (level.time - ent->monsterinfo.last_reacttodamage_target_time > target_cooldown_react) {
                    if (!ent->enemy || !horde::IsSpecialType(ent->enemy, horde::SpecialEntityTypeID::BARREL)) {
                        TargetTesla(ent, self);
                        ent->monsterinfo.last_reacttodamage_target_time = level.time;
                    }
                }

                monsters_damaged++;
            }
        }
    }

}

// Burning state before explosion
THINK(barrel_burn)(edict_t* self) -> void
{
    // Check if burn time has expired - if so, explode
    if (level.time >= self->timestamp)
    {
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
    if (!self || !self->inuse)
        return;

    // Find nearby barrels for chain reaction
    const auto nearby_entities = HordePhys::g_monster_grid.QueryRadius(self->s.origin, BARREL_CHAIN_EXPLOSION_RADIUS);

    for (auto* ent : nearby_entities)
    {
        // Check if it's another barrel - early exit for non-barrels
        if (ent == self || !ent->inuse || ent->deadflag || ent->die != barrel_die)
            continue;

        const float dist = range_to(self, ent);
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
    barrel_cleanup_tracking(self);

    // Radius damage - use chain if available, otherwise self
    edict_t* attacker = self->chain ? self->chain : self;
    T_RadiusDamage_TeamSafe(self, attacker, (float)damage, nullptr,
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
        }
        return;
    }

    // Calculate push direction and strength
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

        // DON'T override think/touch if barrel is burning!
        if (self->think != barrel_burn)
        {
            self->movetype = MOVETYPE_STEP;
            self->touch = barrel_summoned_touch;  // Use summoned touch behavior
            self->think = barrel_think;
            self->nextthink = level.time + FRAME_TIME_S;
        }
        // If burning, keep movetype TOSS and let barrel_burn continue

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

            // Only change to MOVETYPE_STEP if NOT burning
            // Burning barrels need to keep MOVETYPE_TOSS to settle on ground properly
            if (self->think != barrel_burn)
            {
                self->movetype = MOVETYPE_STEP;
                self->touch = barrel_touch;
                self->think = barrel_think;
                self->nextthink = level.time + FRAME_TIME_S;
            }
            else
            {
                // Burning barrel landed - ensure it continues to burn
                // Don't change movetype, touch, or think - just ensure nextthink is scheduled
                if (self->nextthink <= level.time)
                {
                    self->nextthink = level.time + FRAME_TIME_S;
                }
            }

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
        gi.sound(self, CHAN_AUTO, gi.soundindex("tank/thud.wav"), 1, ATTN_NORM, 0);
    }
}

// Main think function
THINK(barrel_think)(edict_t* self) -> void
{
    // Don't check lifetime while barrel is being held
    bool is_held = false;
    if (self->chain && self->chain->client && self->chain->client->resp.held_barrel == self)
    {
        is_held = true;
    }

    // Check lifetime only if not being held
    // IMPORTANT: Don't remove burning barrels here - barrel_burn handles its own explosion timing
    if (!is_held && !self->deadflag && self->timestamp > 0_sec && level.time > self->timestamp)
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

    // Store the initial distance from player to barrel (for gravity gun effect)
    vec3_t barrel_vec = barrel->s.origin - player->s.origin;
    barrel->wait = barrel_vec.length();

    // Pick it up
    player->client->resp.held_barrel = barrel;
    barrel->solid = SOLID_TRIGGER; // Use trigger so it doesn't clip but can't get stuck in walls
    barrel->s.alpha = 0.7f; // Make it semi-transparent while held
    barrel->svflags &= ~SVF_NOCLIENT; // Make visible (will be handled by visualize)
    barrel->movetype = MOVETYPE_NOCLIP; // Allow free movement
    gi.linkentity(barrel);

    gi.sound(player, CHAN_AUTO, gi.soundindex("misc/w_pkup.wav"), 1, ATTN_NORM, 0);
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
}

// Gravity gun style barrel holding - update barrel position based on player view
void barrel_visualize(edict_t* player)
{
    if (!player || !player->client || !player->client->resp.held_barrel)
        return;

    edict_t* barrel = player->client->resp.held_barrel;

    // Calculate where the barrel SHOULD be (at stored distance in view direction)
    vec3_t forward;
    AngleVectors(player->client->v_angle, forward, nullptr, nullptr);
    vec3_t target_position = player->s.origin + (forward * barrel->wait);
    target_position[2] += player->viewheight - 8; // Adjust to eye level

    // Calculate current distance from barrel to target
    vec3_t to_target = target_position - barrel->s.origin;
    float current_distance = to_target.length();

    // If barrel is too far away (stuck behind wall or lost), teleport it back
    constexpr float MAX_DISTANCE = 400.0f; // Teleport threshold
    if (current_distance > MAX_DISTANCE)
    {
        // Teleport barrel directly to target position
        barrel->s.origin = target_position;
        gi.linkentity(barrel);
        return;
    }

    // Always trace from current position to target to ensure clear path and stop at walls
    trace_t tr = gi.trace(barrel->s.origin, barrel->mins, barrel->maxs,
                          target_position, barrel, MASK_SOLID);

    // If path is blocked, pull back from the wall more aggressively
    if (tr.fraction < 1.0f)
    {
        // Pull back 16 units from the wall (doubled from 8) to prevent sticking
        vec3_t pullback = tr.plane.normal * 16.0f;
        target_position = tr.endpos + pullback;
    }

    // Calculate direction from barrel's current position to validated target position
    vec3_t pull_dir = target_position - barrel->s.origin;
    float distance_to_target = pull_dir.length();

    // Smoothly move barrel toward target position
    // More aggressive movement to keep up with player
    if (distance_to_target > 1.0f)
    {
        pull_dir = safe_normalized(pull_dir);
        // Increased speed multiplier for faster catch-up
        float move_speed = distance_to_target * barrel_hold_speed->value / 100.0f;
        // Use exponential scaling for distance - farther = faster catch up
        float speed_scale = 0.025f + (distance_to_target / 300.0f); // Base speed + distance factor
        barrel->s.origin = barrel->s.origin + (pull_dir * (move_speed * speed_scale));
    }
    else
    {
        // Close enough, snap to target
        barrel->s.origin = target_position;
    }

    // Make it visible but not solid while held
    barrel->solid = SOLID_TRIGGER; // Use trigger so it doesn't clip but can't get stuck in walls
    barrel->svflags &= ~SVF_NOCLIENT;
    barrel->movetype = MOVETYPE_NOCLIP; // Move freely without collision
    barrel->velocity = {}; // Clear velocity since we're directly setting position
    gi.linkentity(barrel);
}

// Fire/throw a barrel - returns the spawned barrel entity
edict_t* fire_barrel(edict_t* self, const vec3_t& start, const vec3_t& aimdir)
{
    // Check barrel limit BEFORE placing a new one
    if (self && self->client)
    {
        if (ClientIsSpectating(self->client)){
         gi.Com_PrintFmt(" Can't do this while spect!\n");
            return nullptr;
               }

        // Check if player is menu protected
        if (IsPlayerMenuProtected(self)) {
            gi.LocClient_Print(self, PRINT_HIGH, "You cannot use this while in a menu.\n");
            return nullptr;
        }

        if (self->client->pers.health <= 0)
            return nullptr;

        // Check if at limit before deploying
        if (self->client->resp.num_barrels >= BarrelConstants::MAX_BARRELS_PER_PLAYER)
        {
            gi.LocClient_Print(self, PRINT_HIGH, "You have reached the barrel limit.\n");
            return nullptr;
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
        // Find an empty slot in the tracking array
        for (int i = 0; i < BarrelConstants::MAX_BARRELS_PER_PLAYER; i++)
        {
            if (!self->client->resp.deployed_barrels[i] || !self->client->resp.deployed_barrels[i]->inuse)
            {
                self->client->resp.deployed_barrels[i] = barrel;
                break;
            }
        }

        // Increment the count
        self->client->resp.num_barrels++;

        // Clear held barrel if throwing
        if (self->client->resp.held_barrel)
            self->client->resp.held_barrel = nullptr;
    }

    // Add to targetable entities
    g_targetable_special_entities.push_back(barrel);

    return barrel;
}

// Spawn function override for horde mode
// This function is removed - map-placed barrels should always use the g_misc.cpp implementation
// Player-spawned barrels are created via fire_barrel() function called by Cmd_Barrel_f

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

    // Reset barrel counter
    ent->client->resp.num_barrels = 0;
}

// Command function for testing barrels

// Smart barrel action - can be called directly from code without gi.argv
void Barrel_SmartAction(edict_t* ent)
{
    if (!ent->client)
        return;

    // Check if player is menu protected
    if (IsPlayerMenuProtected(ent)) {
        gi.LocClient_Print(ent, PRINT_HIGH, "You cannot use this while in a menu.\n");
        return;
    }

    // If holding a barrel, throw it
    if (ent->client->resp.held_barrel)
    {
        edict_t* barrel = ent->client->resp.held_barrel;

        // Calculate throw direction from player's view
        vec3_t forward;
        AngleVectors(ent->client->v_angle, forward, nullptr, nullptr);

        // Clear any existing velocity and apply throw velocity
        barrel->velocity = forward * barrel_throw_speed->value;

        // Release the barrel
        barrel->s.alpha = 1.0f; // Reset to fully opaque
        barrel->solid = SOLID_BBOX;
        barrel->movetype = MOVETYPE_TOSS;
        gi.linkentity(barrel);

        ent->client->resp.held_barrel = nullptr;

        // Clear the attack button to prevent weapon from firing
        if (ent->client && ent->client->resp.held_barrel && ent->client->latched_buttons & BUTTON_ATTACK)
            ent->client->latched_buttons &= ~BUTTON_ATTACK;

        // gi.LocClient_Print(ent, PRINT_HIGH, "Barrel thrown!\n");
        return;
    }

    // Try to pick up nearest barrel
    edict_t* best = nullptr;
    float best_dist = BarrelConstants::BARREL_PICKUP_RANGE;

    for (int i = 1; i < static_cast<int>(globals.num_edicts); i++)
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

    if (best && barrel_pickup(ent, best))
    {
        // gi.LocClient_Print(ent, PRINT_HIGH, "Barrel picked up!\n");
        return;
    }

    // Otherwise spawn a new barrel and pick it up immediately
    vec3_t forward, start;
    AngleVectors(ent->client->v_angle, forward, nullptr, nullptr);
    start = ent->s.origin;
    start[2] += ent->viewheight - 8;
    start = start + (forward * 64); // Spawn at gravity gun distance
    edict_t* barrel = fire_barrel(ent, start, forward);

    if (barrel && barrel_pickup(ent, barrel))
    {
        // gi.LocClient_Print(ent, PRINT_HIGH, "Barrel spawned and picked up!\n");
    }
}

void Cmd_Barrel_f(edict_t* ent)
{
    if (!ent->client)
        return;

    // Check if player is menu protected
    if (IsPlayerMenuProtected(ent)) {
        gi.LocClient_Print(ent, PRINT_HIGH, "You cannot use this while in a menu.\n");
        return;
    }

    const char* arg = gi.argv(1);

    // "barrel" - smart behavior (throw if holding, pickup if near, spawn otherwise)
    if (!arg || !*arg)
    {
        // If holding a barrel, throw it
        if (ent->client->resp.held_barrel)
        {
            edict_t* barrel = ent->client->resp.held_barrel;

            // Calculate throw direction from player's view
            vec3_t forward;
            AngleVectors(ent->client->v_angle, forward, nullptr, nullptr);

            // Clear any existing velocity and apply throw velocity
            barrel->velocity = forward * barrel_throw_speed->value;

            // Release the barrel
            barrel->s.alpha = 1.0f; // Reset to fully opaque
            barrel->solid = SOLID_BBOX;
            barrel->movetype = MOVETYPE_TOSS;
            gi.linkentity(barrel);

            ent->client->resp.held_barrel = nullptr;

            // Clear the attack button to prevent weapon from firing
            if (ent->client && ent->client->resp.held_barrel && ent->client->latched_buttons & BUTTON_ATTACK)
                ent->client->latched_buttons &= ~BUTTON_ATTACK;

            // gi.LocClient_Print(ent, PRINT_HIGH, "Barrel thrown!\n");
            return;
        }

        // Try to pick up nearest barrel
        edict_t* best = nullptr;
        float best_dist = BarrelConstants::BARREL_PICKUP_RANGE;

        for (int i = 1; i < static_cast<int>(globals.num_edicts); i++)
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

        if (best && barrel_pickup(ent, best))
        {
            // gi.LocClient_Print(ent, PRINT_HIGH, "Barrel picked up!\n");
            return;
        }

        // Otherwise spawn a new barrel and pick it up immediately
        vec3_t forward, start;
        AngleVectors(ent->client->v_angle, forward, nullptr, nullptr);
        start = ent->s.origin;
        start[2] += ent->viewheight - 8;
        start = start + (forward * 64); // Spawn at gravity gun distance
        edict_t* barrel = fire_barrel(ent, start, forward);

        if (barrel && barrel_pickup(ent, barrel))
        {
            // gi.LocClient_Print(ent, PRINT_HIGH, "Barrel spawned and picked up!\n");
        }
    }
    // "barrel throw" - explicit throw command
    else if (Q_strcasecmp(arg, "throw") == 0)
    {
        if (ent->client->resp.held_barrel)
        {
            edict_t* barrel = ent->client->resp.held_barrel;
            vec3_t forward;
            AngleVectors(ent->client->v_angle, forward, nullptr, nullptr);
            barrel->velocity = forward * barrel_throw_speed->value;
            barrel->solid = SOLID_BBOX;
            barrel->movetype = MOVETYPE_TOSS;
            gi.linkentity(barrel);
            ent->client->resp.held_barrel = nullptr;
            // gi.LocClient_Print(ent, PRINT_HIGH, "Barrel thrown!\n");
        }
        else
        {
            // gi.LocClient_Print(ent, PRINT_HIGH, "Not holding a barrel.\n");
        }
    }
    // "barrel pickup" - pickup nearest barrel
    else if (Q_strcasecmp(arg, "pickup") == 0)
    {
        // Find nearest barrel
        edict_t* best = nullptr;
        float best_dist = BarrelConstants::BARREL_PICKUP_RANGE;

        for (int i = 1; i < static_cast<int>(globals.num_edicts); i++)
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
                // gi.LocClient_Print(ent, PRINT_HIGH, "Barrel picked up!\n");
            }
            else
            {
                // gi.LocClient_Print(ent, PRINT_HIGH, "Cannot pick up barrel.\n");
            }
        }
        else
        {
            // gi.LocClient_Print(ent, PRINT_HIGH, "No barrel in range.\n");
        }
    }
    // "barrel drop" - drop held barrel
    else if (Q_strcasecmp(arg, "drop") == 0)
    {
        if (ent->client->resp.held_barrel)
        {
            barrel_drop(ent);
            // gi.LocClient_Print(ent, PRINT_HIGH, "Barrel dropped.\n");
        }
        else
        {
            // gi.LocClient_Print(ent, PRINT_HIGH, "Not holding a barrel.\n");
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
        // gi.LocClient_Print(ent, PRINT_HIGH, "Barrel spawned at your location.\n");
    }
    // "barrel clear" - remove all barrels
    else if (Q_strcasecmp(arg, "clear") == 0)
    {
        int count = 0;
        for (int i = 1; i < static_cast<int>(globals.num_edicts); i++)
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
        // gi.LocClient_Print(ent, PRINT_HIGH, "Usage: barrel [throw|pickup|drop|spawn|clear]\n");
    }
}