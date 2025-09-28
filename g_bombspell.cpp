// // Copyright (c) ZeniMax Media Inc.
// // Licensed under the GNU General Public License 2.0.
// // Bombspell implementation ported from Vortex

// #include "g_local.h"

// // Bomb spell constants
// constexpr float CARPETBOMB_INITIAL_DAMAGE = 100;
// constexpr float CARPETBOMB_ADDON_DAMAGE = 20;
// constexpr float CARPETBOMB_DAMAGE_RADIUS = 150;
// constexpr gtime_t CARPETBOMB_DURATION = 5_sec;
// constexpr float CARPETBOMB_MAX_HEIGHT = 256;
// constexpr float CARPETBOMB_ROOF_BUFFER = 32;
// constexpr float CARPETBOMB_STEP_SIZE = 128;
// constexpr float CARPETBOMB_CARPET_WIDTH = 200;

// constexpr float BOMBAREA_WIDTH = 300;
// constexpr float BOMBAREA_FLOOR_HEIGHT = 256;
// constexpr gtime_t BOMBAREA_DURATION = 10_sec;
// constexpr gtime_t BOMBAREA_STARTUP_DELAY = 1_sec;
// constexpr float MAX_BOMB_RANGE = 1024;

// constexpr float BOMBPERSON_RANGE = 1024;
// constexpr float BOMBPERSON_WIDTH = 100;
// constexpr gtime_t BOMBPERSON_DURATION = 10_sec;

// constexpr float COST_FOR_BOMB = 50;
// constexpr gtime_t DELAY_BOMB = 2_sec;

// constexpr int CEILING_PITCH = 90;
// constexpr int FLOOR_PITCH = 270;

// // Forward declarations
// void spawn_grenades(edict_t* ent, const vec3_t& origin, gtime_t time, int damage, int num);

// // Helper function to spawn grenades
// void spawn_grenades(edict_t* ent, const vec3_t& origin, gtime_t time, int damage, int num)
// {
//     vec3_t start = origin;
//     vec3_t dir = { 0, 0, 1 };  // Default upward direction

//     // Randomize direction slightly for each grenade
//     for (int i = 0; i < num; i++)
//     {
//         vec3_t rand_dir = dir;
//         rand_dir.x += crandom() * 0.1f;
//         rand_dir.y += crandom() * 0.1f;
//         rand_dir = rand_dir.normalized();

//         // Use existing grenade spawning function from g_weapon.cpp
//         fire_grenade(ent, start, rand_dir, damage, 600, time, CARPETBOMB_DAMAGE_RADIUS,
//                     crandom() * 10.0f, 200.0f + crandom() * 10.0f, false);
//     }
// }

// // Carpet bomb think function
// void carpetbomb_think(edict_t* self)
// {
//     float ceil_height;
//     bool failed = false;
//     vec3_t forward, right, start, end;
//     trace_t tr, tr1;

//     if (!self->owner || !self->owner->inuse || !self->owner->client ||
//         self->owner->health <= 0 || level.time > gtime_t::from_ms(self->delay))
//     {
//         G_FreeEdict(self);
//         return;
//     }

//     // Move forward
//     AngleVectors(self->s.angles, &forward, &right, nullptr);
//     vec3_t move_dist = forward * frandom(CARPETBOMB_DAMAGE_RADIUS / 2, CARPETBOMB_DAMAGE_RADIUS + 1);
//     start = self->s.origin + move_dist;

//     tr = gi.traceline(self->s.origin, start, self, MASK_SOLID);
//     end = start;
//     start.z += 1;
//     end.z -= 8192;
//     tr1 = gi.traceline(start, end, self, MASK_SOLID);
//     start.z -= 1;

//     if (tr.fraction < 1 || start.z != tr1.endpos.z)
//     {
//         // Get current ceiling height
//         end = start;
//         end.z += 8192;
//         tr = gi.traceline(self->s.origin, end, self, MASK_SOLID);
//         ceil_height = tr.endpos.z;

//         // Push down from above desired position
//         start.z += CARPETBOMB_STEP_SIZE;
//         if (start.z > ceil_height)
//             start.z = ceil_height;

//         end = start;
//         end.z -= 8192;
//         tr = gi.traceline(start, end, self, MASK_SOLID);

//         // Don't go through walls
//         if (tr.allsolid)
//             failed = true;

//         // Try a bit lower
//         if (tr.startsolid)
//         {
//             start.z -= CARPETBOMB_STEP_SIZE;
//             tr = gi.traceline(start, end, self, MASK_SOLID);
//             if (tr.startsolid || tr.allsolid)
//                 failed = true;
//         }

//         // Don't go into water if we aren't already submerged
//         vec3_t water_check = tr.endpos;
//         water_check.z += 8;
//         if (!self->waterlevel && (gi.pointcontents(water_check) & MASK_WATER))
//             failed = true;
//     }

//     // Save position
//     self->s.origin = tr.endpos;
//     start = tr.endpos;

//     // Spawn explosions on either side
//     AngleVectors(self->s.angles, nullptr, &right, nullptr);
//     vec3_t side_offset = right * (crandom() * frandom(CARPETBOMB_CARPET_WIDTH / 4, CARPETBOMB_CARPET_WIDTH / 2));
//     end = self->s.origin + side_offset;

//     // Make sure path is wide enough
//     tr = gi.traceline(self->s.origin, end, self, MASK_SHOT);
//     self->s.origin = tr.endpos;
//     self->s.origin.z += 32;

//     // Make sure the caster can see this spot
//     trace_t vis_tr = gi.traceline(self->move_origin, self->s.origin, self, MASK_SOLID);
//     if (vis_tr.fraction < 1.0f)
//         failed = true;

//     // Make sure bombspell is in a valid location
//     if ((gi.pointcontents(self->s.origin) & CONTENTS_SOLID) || failed)
//     {
//         G_FreeEdict(self);
//         return;
//     }

//     T_RadiusDamage(self, self->owner, (float)self->dmg, nullptr, self->dmg_radius,
//                    DAMAGE_NONE, mod_t(MOD_BOMBS));

//     // Write explosion effects
//     gi.WriteByte(svc_temp_entity);
//     gi.WriteByte(TE_EXPLOSION1);
//     gi.WritePosition(self->s.origin);
//     gi.multicast(self->s.origin, MULTICAST_PVS, false);

//     self->s.origin = start;  // Retrieve starting position
//     self->nextthink = level.time + FRAME_TIME_MS;

//     gi.linkentity(self);
// }

// // Carpet bomb function
// void CarpetBomb(edict_t* ent)
// {
//     vec3_t forward, right, start, end, offset;
//     trace_t tr;

//     if (!ent || !ent->client)
//         return;

//     // Deduct cost
//     // Note: power_cube_index would need to be defined elsewhere in the mod
//     // ent->client->pers.inventory[power_cube_index] -= COST_FOR_BOMB * cost_mult;

//     // Create bombspell entity
//     edict_t* spell = G_Spawn();
//     spell->think = carpetbomb_think;
//     spell->nextthink = level.time + FRAME_TIME_MS;
//     spell->owner = ent;
//     spell->svflags |= SVF_NOCLIENT;
//     spell->solid = SOLID_NOT;
//     spell->dmg = CARPETBOMB_INITIAL_DAMAGE + CARPETBOMB_ADDON_DAMAGE;// * skill_mult;
//     spell->dmg_radius = CARPETBOMB_DAMAGE_RADIUS;
//     spell->delay = (level.time + CARPETBOMB_DURATION).milliseconds();
//     spell->s.angles = ent->s.angles;
//     spell->s.angles[PITCH] = 0;
//     spell->s.angles[ROLL] = 0;
//     gi.linkentity(spell);

//     // Store player position (used for visibility check later)
//     spell->move_origin = ent->s.origin;

//     // Get bombspell starting position
//     AngleVectors(ent->s.angles, &forward, &right, nullptr);
//     offset = { 0, 7, (float)(ent->viewheight - 8) };
//     P_ProjectSource(ent, ent->client->v_angle, offset, start, forward);
//     start = start + forward * (1 + CARPETBOMB_DAMAGE_RADIUS / 2);
//     end = start;
//     end.z = -8192;
//     tr = gi.traceline(start, end, ent, MASK_SOLID);
//     spell->s.origin = tr.endpos;
// }

// // Bomb area think function
// void bombarea_think(edict_t* self)
// {
//     float thinktime, bombtime;
//     vec3_t start;
//     trace_t tr;

//     if (!self->owner || !self->owner->inuse || !self->owner->client ||
//         self->owner->health <= 0 || level.time > gtime_t::from_ms(self->delay))
//     {
//         G_FreeEdict(self);
//         return;
//     }

//     start = self->s.origin;

//     thinktime = 0.2f * ((self->delay - 6000) - level.time.milliseconds()) / 1000.0f;
//     if (thinktime < 0.2f)
//         thinktime = 0.2f;

//     // Spread randomly around target
//     start.x += frandom(0, BOMBAREA_WIDTH / 2) * crandom();
//     start.y += frandom(0, BOMBAREA_WIDTH / 2) * crandom();
//     tr = gi.traceline(self->s.origin, start, self, MASK_SHOT);

//     if (self->s.angles[PITCH] == 90)
//         bombtime = 1.0f + 2.0f * frandom();
//     else
//         bombtime = 0.5f + 2.0f * frandom();

//     spawn_grenades(self->owner, tr.endpos, gtime_t::from_sec(bombtime), self->dmg, 1);
//     self->nextthink = level.time + gtime_t::from_sec(thinktime);
// }

// // Bomb area function
// void BombArea(edict_t* ent)//, float skill_mult, float cost_mult)
// {
//     vec3_t angles, offset;
//     vec3_t forward, right, start, end;
//     trace_t tr;
//     int cost = COST_FOR_BOMB;// * cost_mult;

//     if (!ent || !ent->client)
//         return;

//     AngleVectors(ent->client->v_angle, &forward, &right, nullptr);
//     offset = { 0, 7, (float)(ent->viewheight - 8) };
//     P_ProjectSource(ent, ent->client->v_angle, offset, start, forward);
//     end = start + forward * MAX_BOMB_RANGE;
//     tr = gi.traceline(start, end, ent, MASK_SOLID);

//     // Make sure this is a floor
//     angles = vectoangles(tr.plane.normal);

//     if (angles[PITCH] == FLOOR_PITCH)
//     {
//         start = tr.endpos;
//         end = tr.endpos;
//         end.z += BOMBAREA_FLOOR_HEIGHT;
//         tr = gi.traceline(start, end, ent, MASK_SOLID);
//     }
//     else if (angles[PITCH] != CEILING_PITCH)
//     {
//         gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "You must look at a ceiling or floor to cast this spell.\n");
//         return;
//     }

//     edict_t* bomb = G_Spawn();
//     bomb->solid = SOLID_NOT;
//     bomb->svflags |= SVF_NOCLIENT;
//     bomb->velocity = vec3_origin;
//     bomb->mins = vec3_origin;
//     bomb->maxs = vec3_origin;
//     bomb->owner = ent;
//     bomb->delay = (level.time + BOMBAREA_DURATION + BOMBAREA_STARTUP_DELAY).milliseconds();
//     bomb->nextthink = level.time + BOMBAREA_STARTUP_DELAY;
//     bomb->dmg = CARPETBOMB_INITIAL_DAMAGE + CARPETBOMB_ADDON_DAMAGE;// * skill_mult;
//     bomb->think = bombarea_think;
//     bomb->s.origin = tr.endpos;
//     bomb->s.old_origin = tr.endpos;
//     bomb->s.angles = angles;
//     gi.linkentity(bomb);

//     //gi.sound(bomb, CHAN_WEAPON, gi.soundindex("abilities/meteorlaunch_short.wav"), 1, ATTN_NORM, 0);
//     // ent->client->pers.inventory[power_cube_index] -= cost;
//     // ent->client->ability_delay = level.time + (DELAY_BOMB * cost_mult);
// }

// // Bomb person think function
// void bombperson_think(edict_t* self)
// {
//     int height, max_height;
//     float bombtime, thinktime;
//     vec3_t start;
//     trace_t tr;

//     // Calculate drop rate
//     bombtime = (self->delay - 8000) / 1000.0f;
//     if (bombtime < level.time.seconds())
//         bombtime = level.time.seconds();
//     thinktime = level.time.seconds() + 0.25f * ((bombtime + 1.0f) - level.time.seconds());

//     // Bomb self-terminates if the enemy dies or owner teleports away
//     if (!self->owner || !self->owner->inuse || !self->owner->client ||
//         !self->enemy || !self->enemy->inuse || self->enemy->health <= 0 ||
//         level.time > gtime_t::from_ms(self->delay))
//     {
//         // Remove curse from enemy if applicable
//         G_FreeEdict(self);
//         return;
//     }

//     self->s.origin = self->enemy->s.origin;

//     // Get random drop height
//     max_height = 250 - (20 * 1);  // Simplified skill level calculation
//     if (max_height < 150)
//         max_height = 150;
//     height = frandom(50, max_height) + (int)self->enemy->maxs.z;

//     // Drop bombs above target
//     start = self->s.origin;
//     start.z += height;
//     tr = gi.traceline(self->s.origin, start, self->owner, MASK_SHOT);
//     start = tr.endpos;
//     start.z--;

//     // Spread randomly around target
//     start.x += (BOMBPERSON_WIDTH / 2) * crandom();
//     start.y += (BOMBPERSON_WIDTH / 2) * crandom();
//     spawn_grenades(self->owner, start, gtime_t::from_sec(0.5f + 2.0f * frandom()), self->dmg, 1);
//     self->nextthink = level.time + gtime_t::from_sec(thinktime - level.time.seconds());
// }

// // Bomb person function
// void BombPerson(edict_t* target, edict_t* owner)//, float skill_mult)
// {
//     if (!target || !owner)
//         return;

//     //gi.sound(target, CHAN_ITEM, gi.soundindex("abilities/meteorlaunch.wav"), 1, ATTN_NORM, 0);

//     if (target->client && !(target->svflags & SVF_MONSTER))
//         gi.LocClient_Print(target, PRINT_HIGH, nullptr, "SOMEONE SET UP US THE BOMB!!\n");

//     edict_t* bomb = G_Spawn();
//     bomb->solid = SOLID_NOT;
//     bomb->svflags |= SVF_NOCLIENT;
//     bomb->velocity = vec3_origin;
//     bomb->mins = vec3_origin;
//     bomb->maxs = vec3_origin;
//     bomb->owner = owner;
//     bomb->enemy = target;
//     bomb->delay = (level.time + BOMBPERSON_DURATION).milliseconds();
//     bomb->dmg = CARPETBOMB_INITIAL_DAMAGE + CARPETBOMB_ADDON_DAMAGE;// * skill_mult;
//     bomb->nextthink = level.time + 1_sec;
//     bomb->think = bombperson_think;
//     bomb->s.origin = target->s.origin;
//     gi.linkentity(bomb);
// }

// // Main command handler for bomb player
// void Cmd_BombPlayer(edict_t* ent)
// {
//     int cost = COST_FOR_BOMB;// * cost_mult;
//     vec3_t forward, right, start, end, offset;
//     trace_t tr;

//     if (!ent || !ent->client)
//         return;

//     // Check for ability delay
//     // if (ent->client->ability_delay > level.time)
//     //     return;

//     // ent->client->ability_delay = level.time + DELAY_BOMB;

//     // Write a nice effect so everyone knows we've cast a spell
//     gi.WriteByte(svc_temp_entity);
//     gi.WriteByte(TE_TELEPORT_EFFECT);
//     gi.WritePosition(ent->s.origin);
//     gi.multicast(ent->s.origin, MULTICAST_PVS, false);

//     // ent->lastsound = level.framenum; // Not available in Q2 Remaster

//     // Check command arguments for bomb type
//     const char* args = gi.args();

//     // Bomb forward (carpet bomb)
//     if (strstr(args, "forward"))
//     {
//         CarpetBomb(ent);// skill_mult, cost_mult);
//         return;
//     }

//     // Bomb area
//     if (strstr(args, "area"))
//     {
//         BombArea(ent);// skill_mult, cost_mult);
//         return;
//     }

//     // Default: bomb a person (targeted bomb)
//     // ent->client->pers.inventory[power_cube_index] -= cost;

//     AngleVectors(ent->client->v_angle, &forward, &right, nullptr);
//     ent->client->kick_origin = forward * -3;
//     offset = { 0, 7, (float)(ent->viewheight - 8) };
//     P_ProjectSource(ent, ent->client->v_angle, offset, start, forward);
//     end = start + forward * BOMBPERSON_RANGE;
//     tr = gi.traceline(start, end, ent, MASK_SHOT);

//     // Check if we hit a valid target
//     if (tr.ent && tr.ent->takedamage && tr.ent != ent)
//     {
//         BombPerson(tr.ent, ent);//, skill_mult);
//     }
// }