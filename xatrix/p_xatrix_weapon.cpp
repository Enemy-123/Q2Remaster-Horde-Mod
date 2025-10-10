// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "../g_local.h"

// RAFAEL
/*
	RipperGun
*/

void weapon_ionripper_fire(edict_t* ent)
{
	vec3_t tempang;
	int	   damage;

	if (G_IsDeathmatch())
		// tone down for deathmatch
		damage = g_config.ionripper.damage;
	else
		damage = g_config.ionripper.damage;

	// Apply Ion Ripper damage upgrade: +2.5 per level
	if (ent && ent->client)
	{
		damage += static_cast<int>(ent->client->pers.skills.ir_damage * 2.5f);
	}

	if (is_quad)
		damage *= damage_multiplier;

	tempang = ent->client->v_angle;
	tempang[YAW] += crandom();

	vec3_t start, dir;
	P_ProjectSource(ent, tempang, { 16, 7, -8 }, start, dir);

	P_AddWeaponKick(ent, ent->client->v_forward * -3, { -3.f, 0.f, 0.f });

	// Apply Ion Ripper speed upgrade: +40 per level
	int speed = 900;
	if (ent && ent->client)
	{
		speed += ent->client->pers.skills.ir_range * 40;
	}

	// Determine effect based on trails setting
	// Note: EF_IONRIPPER is required for proper bouncing behavior
	effects_t effect = EF_IONRIPPER;
	if (ent && ent->client && ent->client->pers.skills.ir_trails)
		effect = EF_NONE;  // Disable trails

	fire_ionripper(ent, start, dir, damage, speed, effect);

	// send muzzle flash (check for silent mode)
	bool silent_mode = false;
	if (ent && ent->client)
	{
		silent_mode = ent->client->pers.skills.ir_silent;
	}

	if (!silent_mode)
	{
		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(MZ_IONRIPPER | is_silenced);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);
	}

	PlayerNoise(ent, start, PNOISE_WEAPON);

	G_RemoveAmmo(ent);
}

void Weapon_Ionripper(edict_t* ent)
{
	constexpr int pause_frames[] = { 36, 0 };
	constexpr int fire_frames[] = { 6, 0 };

	Weapon_Generic(ent, 5, 7, 36, 39, pause_frames, fire_frames, weapon_ionripper_fire);
}

void weapon_phalanx_fire(edict_t* ent)
{
    vec3_t v;
    int    damage;
    float  damage_radius;
    int    radius_damage;

    damage = irandom(g_config.phalanx.damage_min, g_config.phalanx.damage_max);
    radius_damage = g_config.phalanx.radius_damage;
    damage_radius = g_config.phalanx.damage_radius;
    if (is_quad)
    {
        damage *= damage_multiplier;
        radius_damage *= damage_multiplier;
    }
    vec3_t dir;
    vec3_t start;

    bool quad_shot_mode = frandom() < 0.20f;

    if (quad_shot_mode)
    {
        // Consumir munición para los 4 disparos - como el modo normal usa 2 plasmas,
        // duplicamos el consumo para el modo quad (4 plasmas)
        G_RemoveAmmo(ent);
        G_RemoveAmmo(ent);

        struct PhalanxShot {
            float yaw_offset;
            float pitch_offset;
            int speed;
            int muzzle_effect;
        };

        const PhalanxShot shots[] = {
            { -1.5f, 0.0f, 1075, MZ_PHALANX },
            { 1.5f, 0.0f, 985, MZ_PHALANX2 },
            { 0.0f, 1.0f, 1150, MZ_PHALANX3 },
            { 0.0f, -1.0f, 920, MZ_PHALANX4 }
        };

        for (int i = 0; i < 4; i++)
        {
            const PhalanxShot& current_shot = shots[i];
            v[PITCH] = ent->client->v_angle[PITCH] + current_shot.pitch_offset;
            v[YAW] = ent->client->v_angle[YAW] + current_shot.yaw_offset;
            v[ROLL] = ent->client->v_angle[ROLL];
            P_ProjectSource(ent, v, { 0, 8, -8 }, start, dir);

            fire_plasma(ent, start, dir, damage, current_shot.speed, damage_radius, radius_damage);

            gi.WriteByte(svc_muzzleflash);
            gi.WriteEntity(ent);
            gi.WriteByte(current_shot.muzzle_effect | is_silenced);
            gi.multicast(ent->s.origin, MULTICAST_PVS, false);
        }
    }
    else
    {
        // Comportamiento original - dispara 2 plasmas
        if (ent->client->ps.gunframe == 8)
        {
            v[PITCH] = ent->client->v_angle[PITCH];
            v[YAW] = ent->client->v_angle[YAW] - 1.5f;
            v[ROLL] = ent->client->v_angle[ROLL];
            P_ProjectSource(ent, v, { 0, 8, -8 }, start, dir);
            radius_damage = 30;
            damage_radius = g_config.phalanx.damage_radius;
            fire_plasma(ent, start, dir, damage, 1075, damage_radius, radius_damage);
            gi.WriteByte(svc_muzzleflash);
            gi.WriteEntity(ent);
            gi.WriteByte(MZ_PHALANX2 | is_silenced);
            gi.multicast(ent->s.origin, MULTICAST_PVS, false);
            G_RemoveAmmo(ent);  // Consume munición para el primer plasma
        }
        else
        {
            v[PITCH] = ent->client->v_angle[PITCH];
            v[YAW] = ent->client->v_angle[YAW] + 1.5f;
            v[ROLL] = ent->client->v_angle[ROLL];
            P_ProjectSource(ent, v, { 0, 8, -8 }, start, dir);
            fire_plasma(ent, start, dir, damage, 985, damage_radius, radius_damage);
            gi.WriteByte(svc_muzzleflash);
            gi.WriteEntity(ent);
            gi.WriteByte(MZ_PHALANX | is_silenced);
            gi.multicast(ent->s.origin, MULTICAST_PVS, false);
            PlayerNoise(ent, start, PNOISE_WEAPON);
            G_RemoveAmmo(ent);  // Consume munición para el segundo plasma
        }
    }

    P_AddWeaponKick(ent, ent->client->v_forward * -2, { -2.f, 0.f, 0.f });
}

void Weapon_Phalanx(edict_t* ent)
{
    constexpr int pause_frames[] = { 29, 42, 55, 0 };
    constexpr int fire_frames[] = { 7, 8, 0 };
    Weapon_Generic(ent, 5, 20, 58, 63, pause_frames, fire_frames, weapon_phalanx_fire);
}
/*
======================================================================

TRAP

======================================================================
*/

constexpr gtime_t TRAP_TIMER = 5_sec;
constexpr float TRAP_MINSPEED = 500.f;
constexpr float TRAP_MAXSPEED = 900.f;

void weapon_trap_fire(edict_t* ent, bool held)
{
	vec3_t start, dir;
	// Paril: kill sideways angle on grenades
	// limit upwards angle so you don't throw behind you
	P_ProjectSource(ent, { max(-62.5f, ent->client->v_angle[0]), ent->client->v_angle[1], ent->client->v_angle[2] }, { 8, 0, -8 }, start, dir);

	// Calculate throw speed with range upgrade
	float minspeed = TRAP_MINSPEED;
	float maxspeed = TRAP_MAXSPEED;
	if (ent && ent->client)
	{
		// Range upgrade adds 30 per level to both min and max speed
		float range_bonus = ent->client->pers.skills.trap_range * 30.0f;
		minspeed += range_bonus;
		maxspeed += range_bonus;
	}

	gtime_t timer = ent->client->grenade_time - level.time;
	int speed = static_cast<int>(ent->health <= 0 ? minspeed : min(minspeed + (TRAP_TIMER - timer).seconds() * ((maxspeed - minspeed) / TRAP_TIMER.seconds()), maxspeed));

	ent->client->grenade_time = 0_ms;

	fire_trap(ent, start, dir, speed);

	G_RemoveAmmo(ent, 1);
}

void Weapon_Trap(edict_t* ent)
{
	constexpr int pause_frames[] = { 29, 34, 39, 48, 0 };

	Throw_Generic(ent, 15, 48, 5, "weapons/trapcock.wav", 11, 12, pause_frames, false, "weapons/traploop.wav", weapon_trap_fire, false);
}