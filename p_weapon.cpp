// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_weapon.c

#include "g_local.h"
#include "m_player.h"

bool is_quad;
// RAFAEL
bool is_quadfire;
// RAFAEL
player_muzzle_t is_silenced;

// PGM
byte damage_multiplier;
// PGM

void weapon_grenade_fire(edict_t* ent, bool held);
// RAFAEL
void weapon_trap_fire(edict_t* ent, bool held);
// RAFAEL

//========
// [Kex]
bool G_CheckInfiniteAmmo(gitem_t* item)
{
	if (item->flags & IF_NO_INFINITE_AMMO)
		return false;

	return g_infinite_ammo->integer; // || (G_IsDeathmatch() && g_instagib->integer);
}

//========
// ROGUE
byte P_DamageModifier(edict_t* ent)
{
	is_quad = 0;
	damage_multiplier = 1;

	if (ent->client->quad_time > level.time)
	{
		damage_multiplier *= 4;
		is_quad = 1;

		// if we're quad and DF_NO_STACK_DOUBLE is on, return now.
		if (g_dm_no_stack_double->integer)
			return damage_multiplier;
	}

	if (ent->client->double_time > level.time)
	{
		damage_multiplier *= 2;
		is_quad = 1;
	}

	return damage_multiplier;
}
// ROGUE
//========

// [Paril-KEX] kicks in vanilla take place over 2 10hz server
// frames; this is to mimic that visual behavior on any tickrate.
inline float P_CurrentKickFactor(edict_t* ent)
{
	if (ent->client->kick.time < level.time)
		return 0.f;

	float const f = (ent->client->kick.time - level.time).seconds() / ent->client->kick.total.seconds();
	return f;
}

// [Paril-KEX]
vec3_t P_CurrentKickAngles(edict_t* ent)
{
	return ent->client->kick.angles * P_CurrentKickFactor(ent);
}

vec3_t P_CurrentKickOrigin(edict_t* ent)
{
	return ent->client->kick.origin * P_CurrentKickFactor(ent);
}

void P_AddWeaponKick(edict_t* ent, const vec3_t& origin, const vec3_t& angles)
{
	ent->client->kick.origin = origin;
	ent->client->kick.angles = angles;
	ent->client->kick.total = 200_ms;
	ent->client->kick.time = level.time + ent->client->kick.total;
}
void P_ProjectSource(edict_t* ent, const vec3_t& angles, vec3_t distance, vec3_t& result_start, vec3_t& result_dir, bool adjust_for_pierce)
{
	if (ent->client->pers.hand == LEFT_HANDED)
		distance[1] *= -1;
	else if (ent->client->pers.hand == CENTER_HANDED)
		distance[1] = 0;

	vec3_t forward, right, up;
	vec3_t const eye_position = (ent->s.origin + vec3_t{ 0, 0, (float)ent->viewheight });

	AngleVectors(angles, forward, right, up);

	result_start = G_ProjectSource2(eye_position, distance, forward, right, up);

	vec3_t	   end = eye_position + forward * 8192;
	contents_t mask = MASK_PROJECTILE & ~CONTENTS_DEADMONSTER;

	// [Paril-KEX]
	if (!G_ShouldPlayersCollide(true))
		mask &= ~CONTENTS_PLAYER;

	trace_t  const tr = gi.traceline(eye_position, end, ent, mask);

	// if the point was damageable, use raw forward
	// so railgun pierces properly
	if ((tr.startsolid || adjust_for_pierce) && tr.ent->takedamage)
	{
		result_dir = forward;
		return;
	}

	end = tr.endpos;
	result_dir = (end - result_start).normalized();

#if 0
	// correction for blocked shots.
	// disabled because it looks weird.
	trace_t eye_tr = gi.traceline(result_start, result_start + (result_dir * tr.fraction * 8192.f), ent, mask);

	if ((eye_tr.endpos - tr.endpos).length() > 32.f)
	{
		result_start = eye_position;
		result_dir = (end - result_start).normalized();
		return;
}
#endif
}

/*
===============
PlayerNoise

Each player can have two noise objects associated with it:
a personal noise (jumping, pain, weapon firing), and a weapon
target noise (bullet wall impacts)

Monsters that don't directly see the player can move
to a noise in hopes of seeing the player from there.
===============
*/
void PlayerNoise(edict_t* who, const vec3_t& where, player_noise_t type)
{
	edict_t* noise;

	// Check if who->client is null
	if (!who->client)
		return;

	if (type == PNOISE_WEAPON)
	{
		if (who->client->silencer_shots)
			who->client->invisibility_fade_time = level.time + (INVISIBILITY_TIME / 5);
		else
			who->client->invisibility_fade_time = level.time + INVISIBILITY_TIME;

		if (who->client->silencer_shots)
		{
			who->client->silencer_shots--;
			return;
		}
	}

	// if (G_IsDeathmatch())  // hordenoise hordehearing  MONSTERS PLAYERS HEARING HERE,disabled to give hearing to monsters!
	// return;

	if (who->flags & FL_NOTARGET)
		return;

	if (type == PNOISE_SELF &&
		(who->client->landmark_free_fall || who->client->landmark_noise_time >= level.time))
		return;

	// ROGUE
	if (who->flags & FL_DISGUISED)
	{
		if (type == PNOISE_WEAPON)
		{
			level.disguise_violator = who;
			level.disguise_violation_time = level.time + 500_ms;
		}
		else
			return;
	}
	// ROGUE

	if (!who->mynoise)
	{
		noise = G_Spawn();
		noise->classname = "player_noise";
		noise->mins = { -8, -8, -8 };
		noise->maxs = { 8, 8, 8 };
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise = noise;

		noise = G_Spawn();
		noise->classname = "player_noise";
		noise->mins = { -8, -8, -8 };
		noise->maxs = { 8, 8, 8 };
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise2 = noise;
	}

	if (type == PNOISE_SELF || type == PNOISE_WEAPON)
	{
		noise = who->mynoise;
		who->client->sound_entity = noise;
		who->client->sound_entity_time = level.time;
	}
	else // type == PNOISE_IMPACT
	{
		noise = who->mynoise2;
		who->client->sound2_entity = noise;
		who->client->sound2_entity_time = level.time;
	}

	noise->s.origin = where;
	noise->absmin = where - noise->maxs;
	noise->absmax = where + noise->maxs;
	noise->teleport_time = level.time;
	gi.linkentity(noise);
}

inline bool G_WeaponShouldStay()
{
	if (G_IsDeathmatch())
		return !P_UseCoopInstancedItems(); // somehow works for horde, probably
		//return g_dm_weapons_stay->integer;
	else if (!G_IsDeathmatch())
		return !P_UseCoopInstancedItems();

	return false;
}

void G_CheckAutoSwitch(edict_t* ent, gitem_t* item, bool is_new);

bool Pickup_Weapon(edict_t* ent, edict_t* other)
{
	item_id_t index;
	gitem_t* ammo;
	index = ent->item->id;
	if (G_WeaponShouldStay() && other->client->pers.inventory[index])
	{
		if (!(ent->spawnflags & (SPAWNFLAG_ITEM_DROPPED | SPAWNFLAG_ITEM_DROPPED_PLAYER)))
			return false;
	}
	bool const is_new = !other->client->pers.inventory[index];
	other->client->pers.inventory[index]++;

	if (!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED) || g_horde->integer)
	{
		if (ent->item->ammo != IT_NULL)
		{
			ammo = GetItemByIndex(ent->item->ammo);
			if (G_CheckInfiniteAmmo(ammo))
			{
				Add_Ammo(other, ammo, 1000);
			}

			else
			{
				int given_quantity = ammo->quantity;

				if (level.is_psx && deathmatch->integer)
					given_quantity *= 2;

				if (g_horde->integer)
				{
					// Usar frandom para obtener un valor entre 0.7 y 1.8
					float const multiplier = frandom(0.7f, 1.8f);
					given_quantity = (int)(given_quantity * multiplier);
				}

				given_quantity = std::max(1, given_quantity);
				Add_Ammo(other, ammo, given_quantity);
			}
		}
		else
		{
			//gi.Com_PrintFmt("Warning: Weapon has no ammo type assigned\n");
		}

		if (!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED_PLAYER & SPAWNFLAG_ITEM_DROPPED))
		{
			if (G_IsDeathmatch())
			{
				if (g_dm_weapons_stay->integer)
					ent->flags |= FL_RESPAWN;
			}
			if (G_IsCooperative())
				ent->flags |= FL_RESPAWN;
		}
	}
	G_CheckAutoSwitch(other, ent->item, is_new);
	return true;
}

static void Weapon_RunThink(edict_t* ent)
{
	// call active weapon think routine
	if (!ent->client->pers.weapon->weaponthink)
		return;

	P_DamageModifier(ent);
	// RAFAEL
	is_quadfire = (ent->client->quadfire_time > level.time);
	// RAFAEL
	if (ent->client->silencer_shots)
		is_silenced = MZ_SILENCED;
	else
		is_silenced = MZ_NONE;
	ent->client->pers.weapon->weaponthink(ent);
}

/*
===============
ChangeWeapon

The old weapon has been dropped all the way, so make the new one
current
===============
*/
void ChangeWeapon(edict_t* ent)
{
	// [Paril-KEX]
	if (ent->health > 0 && !g_instant_weapon_switch->integer && ((ent->client->latched_buttons | ent->client->buttons) & BUTTON_HOLSTER))
		return;

	if (ent->client->grenade_time)
	{
		// force a weapon think to drop the held grenade
		ent->client->weapon_sound = 0;
		Weapon_RunThink(ent);
		ent->client->grenade_time = 0_ms;
	}

	if (ent->client->pers.weapon)
	{
		ent->client->pers.lastweapon = ent->client->pers.weapon;

		if (ent->client->newweapon && ent->client->newweapon != ent->client->pers.weapon)
			gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/change.wav"), 1, ATTN_NORM, 0);
	}

	ent->client->pers.weapon = ent->client->newweapon;
	ent->client->newweapon = nullptr;
	ent->client->machinegun_shots = 0;

	// set visible model
	if (ent->s.modelindex == MODELINDEX_PLAYER)
		P_AssignClientSkinnum(ent);

	if (!ent->client->pers.weapon)
	{ // dead
		ent->client->ps.gunindex = 0;
		ent->client->ps.gunskin = 0;
		return;
	}

	ent->client->weaponstate = WEAPON_ACTIVATING;
	ent->client->ps.gunframe = 0;
	ent->client->ps.gunindex = gi.modelindex(ent->client->pers.weapon->view_model);
	ent->client->ps.gunskin = 0;
	ent->client->weapon_sound = 0;

	ent->client->anim_priority = ANIM_PAIN;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crpain1;
		ent->client->anim_end = FRAME_crpain4;
	}
	else
	{
		ent->s.frame = FRAME_pain301;
		ent->client->anim_end = FRAME_pain304;
	}
	ent->client->anim_time = 0_ms;

	// for instantweap, run think immediately
	// to set up correct start frame
	if (g_instant_weapon_switch->integer)
		Weapon_RunThink(ent);
}

/*
=================
NoAmmoWeaponChange
=================
*/
void NoAmmoWeaponChange(edict_t* ent, bool sound)
{
	if (sound)
	{
		if (level.time >= ent->client->empty_click_sound)
		{
			gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->client->empty_click_sound = level.time + 1_sec;
		}
	}

	constexpr item_id_t no_ammo_order[] = {
		IT_WEAPON_DISRUPTOR,
		IT_WEAPON_RAILGUN,
		IT_WEAPON_PLASMABEAM,
		IT_WEAPON_IONRIPPER,
		IT_WEAPON_HYPERBLASTER,
		IT_WEAPON_ETF_RIFLE,
		IT_WEAPON_CHAINGUN,
		IT_WEAPON_MACHINEGUN,
		IT_WEAPON_SSHOTGUN,
		IT_WEAPON_SHOTGUN,
		IT_WEAPON_PHALANX,
		IT_WEAPON_RLAUNCHER,
		IT_WEAPON_GLAUNCHER,
		IT_WEAPON_PROXLAUNCHER,
		IT_WEAPON_CHAINFIST,
		IT_WEAPON_BLASTER
	};

	for (size_t i = 0; i < q_countof(no_ammo_order); i++)
	{
		gitem_t* item = GetItemByIndex(no_ammo_order[i]);

		if (!item)
			gi.Com_ErrorFmt("Invalid no ammo weapon switch weapon {}\n", (int32_t)no_ammo_order[i]);

		if (!ent->client->pers.inventory[item->id])
			continue;

		if (item->ammo && ent->client->pers.inventory[item->ammo] < item->quantity)
			continue;

		ent->client->newweapon = item;
		return;
	}
}

void G_RemoveAmmo(edict_t* ent, int32_t quantity)
{
	if (G_CheckInfiniteAmmo(ent->client->pers.weapon))
		return;

	bool const pre_warning = ent->client->pers.inventory[ent->client->pers.weapon->ammo] <=
		ent->client->pers.weapon->quantity_warn;

	ent->client->pers.inventory[ent->client->pers.weapon->ammo] -= quantity;

	bool const post_warning = ent->client->pers.inventory[ent->client->pers.weapon->ammo] <=
		ent->client->pers.weapon->quantity_warn;

	if (!pre_warning && post_warning)
		gi.local_sound(ent, CHAN_AUTO, gi.soundindex("weapons/lowammo.wav"), 1, ATTN_NORM, 0);

	if (ent->client->pers.weapon->ammo == IT_AMMO_CELLS)
		G_CheckPowerArmor(ent);
}

void G_RemoveAmmo(edict_t* ent)
{
	G_RemoveAmmo(ent, ent->client->pers.weapon->quantity);
}

inline gtime_t Weapon_AnimationTime(edict_t* ent)
{
	// Base animation rate
	if (g_quick_weapon_switch->integer && (gi.tick_rate >= 20) &&
		(ent->client->weaponstate == WEAPON_ACTIVATING || ent->client->weaponstate == WEAPON_DROPPING))
		ent->client->ps.gunrate = 20;
	else
		ent->client->ps.gunrate = 10;

	if (ent->client->ps.gunframe != 0 && (!(ent->client->pers.weapon->flags & IF_NO_HASTE) || ent->client->weaponstate != WEAPON_FIRING))
	{
		const bool using_blaster = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_BLASTER;
		const bool using_shotgun = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_SHOTGUN;
		//const bool using_sshotgun = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_SSHOTGUN;
		const bool using_glauncher = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_GLAUNCHER;
		const bool using_proxlauncher = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_PROXLAUNCHER;
		const bool using_etfrifle = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_ETF_RIFLE;
		//const bool using_machinegun = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_MACHINEGUN;
		//const bool using_chaingun = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_CHAINGUN;
		//const bool using_hyperblaster = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_HYPERBLASTER;
		//const bool using_ripper = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_IONRIPPER;
		//const bool using_rail = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_RAILGUN;
		//const bool using_rocketl = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_RLAUNCHER;
		const bool using_trap = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_AMMO_TRAP;
		const bool using_tesla = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_AMMO_TESLA;

		float final_multiplier = 1.0f;

		// Apply quad fire multiplier
		if (is_quadfire)
			final_multiplier *= 2.0f;

		// Apply haste multiplier
		if (CTFApplyHaste(ent))
			final_multiplier *= 2.0f;

		// Weapon-specific multipliers
		//if (using_blaster) {
		//	final_multiplier *= 1.4f;
		//}
		if (using_blaster || using_glauncher || using_etfrifle || using_proxlauncher) {
			final_multiplier *= 1.4f;
		}
		if (using_shotgun || using_tesla) {
			final_multiplier *= 1.5f;
		}

		if (using_trap) {
			final_multiplier *= 2.0f;
		}

		//// Special handling for chaingun (improved)
		//if (using_chaingun) {
		//	// Limit chaingun to maximum 2x speed regardless of other multipliers
		//	final_multiplier = min(final_multiplier, 2.0f);
		//}

		// Apply final multiplier
		ent->client->ps.gunrate *= final_multiplier;
	}

	// Network optimization
	if (ent->client->ps.gunrate == 10)
	{
		ent->client->ps.gunrate = 0;
		return 100_ms;
	}

	return gtime_t::from_ms((1.f / ent->client->ps.gunrate) * 1000);
}

/*
=================
Think_Weapon

Called by ClientBeginServerFrame and ClientThink
=================
*/
void Think_Weapon(edict_t* ent)
{
	if (ent->client->resp.spectator)
		return;

	// if just died, put the weapon away
	if (ent->health < 1)
	{
		ent->client->newweapon = nullptr;
		ChangeWeapon(ent);
	}

	if (!ent->client->pers.weapon)
	{
		if (ent->client->newweapon)
			ChangeWeapon(ent);
		return;
	}

	// call active weapon think routine
	Weapon_RunThink(ent);

	// check remainder from haste; on 100ms/50ms server frames we may have
	// 'run next frame in' times that we can't possibly catch up to,
	// so we have to run them now.
	if (33_ms < FRAME_TIME_MS)
	{
		gtime_t const relative_time = Weapon_AnimationTime(ent);

		if (relative_time < FRAME_TIME_MS)
		{
			// check how many we can't run before the next server tick
			gtime_t const next_frame = level.time + FRAME_TIME_S;
			int64_t remaining_ms = (next_frame - ent->client->weapon_think_time).milliseconds();

			while (remaining_ms > 0)
			{
				ent->client->weapon_think_time -= relative_time;
				ent->client->weapon_fire_finished -= relative_time;
				Weapon_RunThink(ent);
				remaining_ms -= relative_time.milliseconds();
			}
		}
	}
}

enum weap_switch_t
{
	WEAP_SWITCH_ALREADY_USING,
	WEAP_SWITCH_NO_WEAPON,
	WEAP_SWITCH_NO_AMMO,
	WEAP_SWITCH_NOT_ENOUGH_AMMO,
	WEAP_SWITCH_VALID
};

weap_switch_t Weapon_AttemptSwitch(edict_t* ent, gitem_t* item, bool silent)
{
	if (ent->client->pers.weapon == item)
		return WEAP_SWITCH_ALREADY_USING;
	else if (!ent->client->pers.inventory[item->id])
		return WEAP_SWITCH_NO_WEAPON;

	if (item->ammo && !g_select_empty->integer && !(item->flags & IF_AMMO))
	{
		gitem_t* ammo_item = GetItemByIndex(item->ammo);

		if (!ent->client->pers.inventory[item->ammo])
		{
			if (!silent)
				gi.LocClient_Print(ent, PRINT_HIGH, "$g_no_ammo", ammo_item->pickup_name, item->pickup_name_definite);
			return WEAP_SWITCH_NO_AMMO;
		}
		else if (ent->client->pers.inventory[item->ammo] < item->quantity)
		{
			if (!silent)
				gi.LocClient_Print(ent, PRINT_HIGH, "$g_not_enough_ammo", ammo_item->pickup_name, item->pickup_name_definite);
			return WEAP_SWITCH_NOT_ENOUGH_AMMO;
		}
	}

	return WEAP_SWITCH_VALID;
}

inline bool Weapon_IsPartOfChain(const gitem_t* item, const gitem_t* other)
{
	return other && other->chain && item->chain && other->chain == item->chain;
}

/*
================
Use_Weapon

Make the weapon ready if there is ammo
================
*/
void Use_Weapon(edict_t* ent, gitem_t* item)
{
	gitem_t* wanted, * root;
	weap_switch_t result = WEAP_SWITCH_NO_WEAPON;

	// if we're switching to a weapon in this chain already,
	// start from the weapon after this one in the chain
	if (!ent->client->no_weapon_chains && Weapon_IsPartOfChain(item, ent->client->newweapon))
	{
		root = ent->client->newweapon;
		wanted = root->chain_next;
	}
	// if we're already holding a weapon in this chain,
	// start from the weapon after that one
	else if (!ent->client->no_weapon_chains && Weapon_IsPartOfChain(item, ent->client->pers.weapon))
	{
		root = ent->client->pers.weapon;
		wanted = root->chain_next;
	}
	// start from beginning of chain (if any)
	else
		wanted = root = item;

	while (true)
	{
		// try the weapon currently in the chain
		if ((result = Weapon_AttemptSwitch(ent, wanted, false)) == WEAP_SWITCH_VALID)
			break;

		// no chains
		if (!wanted->chain_next || ent->client->no_weapon_chains)
			break;

		wanted = wanted->chain_next;

		// we wrapped back to the root item
		if (wanted == root)
			break;
	}

	if (result == WEAP_SWITCH_VALID)
		ent->client->newweapon = wanted; // change to this weapon when down
	else if ((result = Weapon_AttemptSwitch(ent, wanted, true)) == WEAP_SWITCH_NO_WEAPON && wanted != ent->client->pers.weapon && wanted != ent->client->newweapon)
		gi.LocClient_Print(ent, PRINT_HIGH, "$g_out_of_item", wanted->pickup_name);
}

/*
================
Drop_Weapon
================
*/
void Drop_Weapon(edict_t* ent, gitem_t* item)
{
	// [Paril-KEX]
	if (G_IsDeathmatch() && g_dm_weapons_stay->integer)
		return;

	item_id_t const index = item->id;
	// see if we're already using it
	if (((item == ent->client->pers.weapon) || (item == ent->client->newweapon)) && (ent->client->pers.inventory[index] == 1))
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "$g_cant_drop_weapon");
		return;
	}

	edict_t* drop = Drop_Item(ent, item);
	drop->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
	drop->svflags &= ~SVF_INSTANCED;
	ent->client->pers.inventory[index]--;
}

void Weapon_PowerupSound(edict_t* ent)
{
	if (!CTFApplyStrengthSound(ent))
	{
		if (ent->client->quad_time > level.time && ent->client->double_time > level.time)
			gi.sound(ent, CHAN_ITEM, gi.soundindex("ctf/tech2x.wav"), 1, ATTN_NORM, 0);
		else if (ent->client->quad_time > level.time)
			gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);
		else if (ent->client->double_time > level.time)
			gi.sound(ent, CHAN_ITEM, gi.soundindex("misc/ddamage3.wav"), 1, ATTN_NORM, 0);
		else if (ent->client->quadfire_time > level.time
			&& ent->client->ctf_techsndtime < level.time)
		{
			ent->client->ctf_techsndtime = level.time + 1_sec;
			gi.sound(ent, CHAN_ITEM, gi.soundindex("ctf/tech3.wav"), 1, ATTN_NORM, 0);
		}
	}

	CTFApplyHasteSound(ent);
}

inline bool Weapon_CanAnimate(edict_t* ent)
{
	// VWep animations screw up corpses
	return !ent->deadflag && ent->s.modelindex == MODELINDEX_PLAYER;
}

// [Paril-KEX] called when finished to set time until
// we're allowed to switch to fire again
inline void Weapon_SetFinished(edict_t* ent)
{
	ent->client->weapon_fire_finished = level.time + Weapon_AnimationTime(ent);
}

inline bool Weapon_HandleDropping(edict_t* ent, int FRAME_DEACTIVATE_LAST)
{
	if (ent->client->weaponstate == WEAPON_DROPPING)
	{
		if (ent->client->weapon_think_time <= level.time)
		{
			if (ent->client->ps.gunframe == FRAME_DEACTIVATE_LAST)
			{
				ChangeWeapon(ent);
				return true;
			}
			else if ((FRAME_DEACTIVATE_LAST - ent->client->ps.gunframe) == 4)
			{
				ent->client->anim_priority = ANIM_ATTACK | ANIM_REVERSED;
				if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
				{
					ent->s.frame = FRAME_crpain4 + 1;
					ent->client->anim_end = FRAME_crpain1;
				}
				else
				{
					ent->s.frame = FRAME_pain304 + 1;
					ent->client->anim_end = FRAME_pain301;
				}
				ent->client->anim_time = 0_ms;
			}

			ent->client->ps.gunframe++;
			ent->client->weapon_think_time = level.time + Weapon_AnimationTime(ent);
		}
		return true;
	}

	return false;
}

inline bool Weapon_HandleActivating(edict_t* ent, int FRAME_ACTIVATE_LAST, int FRAME_IDLE_FIRST)
{
	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		if (ent->client->weapon_think_time <= level.time || g_instant_weapon_switch->integer)
		{
			ent->client->weapon_think_time = level.time + Weapon_AnimationTime(ent);

			if (ent->client->ps.gunframe == FRAME_ACTIVATE_LAST || g_instant_weapon_switch->integer)
			{
				ent->client->weaponstate = WEAPON_READY;
				ent->client->ps.gunframe = FRAME_IDLE_FIRST;
				ent->client->weapon_fire_buffered = false;
				if (!g_instant_weapon_switch->integer)
					Weapon_SetFinished(ent);
				else
					ent->client->weapon_fire_finished = 0_ms;
				return true;
			}

			ent->client->ps.gunframe++;
			return true;
		}
	}

	return false;
}

inline bool Weapon_HandleNewWeapon(edict_t* ent, int FRAME_DEACTIVATE_FIRST, int FRAME_DEACTIVATE_LAST)
{
	bool is_holstering = false;

	if (!g_instant_weapon_switch->integer)
		is_holstering = ((ent->client->latched_buttons | ent->client->buttons) & BUTTON_HOLSTER);

	if ((ent->client->newweapon || is_holstering) && (ent->client->weaponstate != WEAPON_FIRING))
	{
		if (g_instant_weapon_switch->integer || ent->client->weapon_think_time <= level.time)
		{
			if (!ent->client->newweapon)
				ent->client->newweapon = ent->client->pers.weapon;

			ent->client->weaponstate = WEAPON_DROPPING;

			if (g_instant_weapon_switch->integer)
			{
				ChangeWeapon(ent);
				return true;
			}

			ent->client->ps.gunframe = FRAME_DEACTIVATE_FIRST;

			if ((FRAME_DEACTIVATE_LAST - FRAME_DEACTIVATE_FIRST) < 4)
			{
				ent->client->anim_priority = ANIM_ATTACK | ANIM_REVERSED;
				if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
				{
					ent->s.frame = FRAME_crpain4 + 1;
					ent->client->anim_end = FRAME_crpain1;
				}
				else
				{
					ent->s.frame = FRAME_pain304 + 1;
					ent->client->anim_end = FRAME_pain301;
				}
				ent->client->anim_time = 0_ms;
			}

			ent->client->weapon_think_time = level.time + Weapon_AnimationTime(ent);
		}
		return true;
	}

	return false;
}

enum weapon_ready_state_t
{
	READY_NONE,
	READY_CHANGING,
	READY_FIRING
};

inline weapon_ready_state_t Weapon_HandleReady(edict_t* ent, int FRAME_FIRE_FIRST, int FRAME_IDLE_FIRST, int FRAME_IDLE_LAST, const int* pause_frames)
{
	if (ent->client->weaponstate == WEAPON_READY)
	{
		bool const request_firing = ent->client->weapon_fire_buffered || ((ent->client->latched_buttons | ent->client->buttons) & BUTTON_ATTACK);

		if (request_firing && ent->client->weapon_fire_finished <= level.time)
		{
			ent->client->latched_buttons &= ~BUTTON_ATTACK;
			ent->client->weapon_think_time = level.time;

			if ((!ent->client->pers.weapon->ammo) ||
				(ent->client->pers.inventory[ent->client->pers.weapon->ammo] >= ent->client->pers.weapon->quantity))
			{
				ent->client->weaponstate = WEAPON_FIRING;
				ent->client->last_firing_time = level.time + COOP_DAMAGE_FIRING_TIME;
				return READY_FIRING;
			}
			else
			{
				NoAmmoWeaponChange(ent, true);
				return READY_CHANGING;
			}
		}
		else if (ent->client->weapon_think_time <= level.time)
		{
			ent->client->weapon_think_time = level.time + Weapon_AnimationTime(ent);

			if (ent->client->ps.gunframe == FRAME_IDLE_LAST)
			{
				ent->client->ps.gunframe = FRAME_IDLE_FIRST;
				return READY_CHANGING;
			}

			if (pause_frames)
				for (int n = 0; pause_frames[n]; n++)
					if (ent->client->ps.gunframe == pause_frames[n])
						if (irandom(16))
							return READY_CHANGING;

			ent->client->ps.gunframe++;
			return READY_CHANGING;
		}
	}

	return READY_NONE;
}

inline void Weapon_HandleFiring(edict_t* ent, int32_t FRAME_IDLE_FIRST, std::function<void()> fire_handler)
{
	Weapon_SetFinished(ent);

	if (ent->client->weapon_fire_buffered)
	{
		ent->client->buttons |= BUTTON_ATTACK;
		ent->client->weapon_fire_buffered = false;
	}

	fire_handler();

	if (ent->client->ps.gunframe == FRAME_IDLE_FIRST)
	{
		ent->client->weaponstate = WEAPON_READY;
		ent->client->weapon_fire_buffered = false;
	}

	ent->client->weapon_think_time = level.time + Weapon_AnimationTime(ent);
}

void Weapon_Generic(edict_t* ent, int FRAME_ACTIVATE_LAST, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_DEACTIVATE_LAST, const int* pause_frames, const int* fire_frames, void (*fire)(edict_t* ent))
{
	int const FRAME_FIRE_FIRST = (FRAME_ACTIVATE_LAST + 1);
	int const FRAME_IDLE_FIRST = (FRAME_FIRE_LAST + 1);
	int const FRAME_DEACTIVATE_FIRST = (FRAME_IDLE_LAST + 1);

	if (!Weapon_CanAnimate(ent))
		return;

	if (Weapon_HandleDropping(ent, FRAME_DEACTIVATE_LAST))
		return;
	else if (Weapon_HandleActivating(ent, FRAME_ACTIVATE_LAST, FRAME_IDLE_FIRST))
		return;
	else if (Weapon_HandleNewWeapon(ent, FRAME_DEACTIVATE_FIRST, FRAME_DEACTIVATE_LAST))
		return;
	else if (auto const state = Weapon_HandleReady(ent, FRAME_FIRE_FIRST, FRAME_IDLE_FIRST, FRAME_IDLE_LAST, pause_frames))
	{
		if (state == READY_FIRING)
		{
			ent->client->ps.gunframe = FRAME_FIRE_FIRST;
			ent->client->weapon_fire_buffered = false;

			if (ent->client->weapon_thunk)
				ent->client->weapon_think_time += FRAME_TIME_S;

			ent->client->weapon_think_time += Weapon_AnimationTime(ent);
			Weapon_SetFinished(ent);

			for (int n = 0; fire_frames[n]; n++)
			{
				if (ent->client->ps.gunframe == fire_frames[n])
				{
					Weapon_PowerupSound(ent);
					fire(ent);
					break;
				}
			}

			// start the animation
			ent->client->anim_priority = ANIM_ATTACK;
			if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crattak1 - 1;
				ent->client->anim_end = FRAME_crattak9;
			}
			else
			{
				ent->s.frame = FRAME_attack1 - 1;
				ent->client->anim_end = FRAME_attack8;
			}
			ent->client->anim_time = 0_ms;
		}

		return;
	}

	if (ent->client->weaponstate == WEAPON_FIRING && ent->client->weapon_think_time <= level.time)
	{
		ent->client->last_firing_time = level.time + COOP_DAMAGE_FIRING_TIME;
		ent->client->ps.gunframe++;
		Weapon_HandleFiring(ent, FRAME_IDLE_FIRST, [&]() {
			for (int n = 0; fire_frames[n]; n++)
			{
				if (ent->client->ps.gunframe == fire_frames[n])
				{
					Weapon_PowerupSound(ent);
					fire(ent);
					break;
				}
			}
			});
	}
}

void Weapon_Repeating(edict_t* ent, int FRAME_ACTIVATE_LAST, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_DEACTIVATE_LAST, const int* pause_frames, void (*fire)(edict_t* ent))
{
	int const FRAME_FIRE_FIRST = (FRAME_ACTIVATE_LAST + 1);
	int const FRAME_IDLE_FIRST = (FRAME_FIRE_LAST + 1);
	int const FRAME_DEACTIVATE_FIRST = (FRAME_IDLE_LAST + 1);

	if (!Weapon_CanAnimate(ent))
		return;

	if (Weapon_HandleDropping(ent, FRAME_DEACTIVATE_LAST))
		return;
	else if (Weapon_HandleActivating(ent, FRAME_ACTIVATE_LAST, FRAME_IDLE_FIRST))
		return;
	else if (Weapon_HandleNewWeapon(ent, FRAME_DEACTIVATE_FIRST, FRAME_DEACTIVATE_LAST))
		return;
	else if (Weapon_HandleReady(ent, FRAME_FIRE_FIRST, FRAME_IDLE_FIRST, FRAME_IDLE_LAST, pause_frames) == READY_CHANGING)
		return;

	if (ent->client->weaponstate == WEAPON_FIRING && ent->client->weapon_think_time <= level.time)
	{
		ent->client->last_firing_time = level.time + COOP_DAMAGE_FIRING_TIME;
		Weapon_HandleFiring(ent, FRAME_IDLE_FIRST, [&]() { fire(ent); });

		if (ent->client->weapon_thunk)
			ent->client->weapon_think_time += FRAME_TIME_S;
	}
}

/*
======================================================================

GRENADE

======================================================================
*/

void weapon_grenade_fire(edict_t* ent, bool held)
{
	int	  damage = 125;
	int	  speed;
	float radius;

	radius = (float)(damage + 40);
	if (is_quad)
		damage *= damage_multiplier;

	vec3_t start, dir;
	// Paril: kill sideways angle on grenades
	// limit upwards angle so you don't throw behind you
	P_ProjectSource(ent, { max(-62.5f, ent->client->v_angle[0]), ent->client->v_angle[1], ent->client->v_angle[2] }, { 2, 0, -14 }, start, dir);

	gtime_t const timer = ent->client->grenade_time - level.time;
	speed = (int)(ent->health <= 0 ? GRENADE_MINSPEED : min(GRENADE_MINSPEED + (GRENADE_TIMER - timer).seconds() * ((GRENADE_MAXSPEED - GRENADE_MINSPEED) / GRENADE_TIMER.seconds()), GRENADE_MAXSPEED));

	ent->client->grenade_time = 0_ms;

	fire_grenade2(ent, start, dir, damage, speed, timer, radius, held);

	G_RemoveAmmo(ent, 1);
}

void Throw_Generic(edict_t* ent, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_PRIME_SOUND,
	const char* prime_sound,
	int FRAME_THROW_HOLD, int FRAME_THROW_FIRE, const int* pause_frames, int EXPLODE,
	const char* primed_sound,
	void (*fire)(edict_t* ent, bool held), bool extra_idle_frame)
{
	// when we die, just toss what we had in our hands.
	if (ent->health <= 0)
	{
		fire(ent, true);
		return;
	}

	int n;
	int const FRAME_IDLE_FIRST = (FRAME_FIRE_LAST + 1);

	if (ent->client->newweapon && (ent->client->weaponstate == WEAPON_READY))
	{
		if (ent->client->weapon_think_time <= level.time)
		{
			ChangeWeapon(ent);
			ent->client->weapon_think_time = level.time + Weapon_AnimationTime(ent);
		}
		return;
	}

	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		if (ent->client->weapon_think_time <= level.time)
		{
			ent->client->weaponstate = WEAPON_READY;
			if (!extra_idle_frame)
				ent->client->ps.gunframe = FRAME_IDLE_FIRST;
			else
				ent->client->ps.gunframe = FRAME_IDLE_LAST + 1;
			ent->client->weapon_think_time = level.time + Weapon_AnimationTime(ent);
			Weapon_SetFinished(ent);
		}
		return;
	}

	if (ent->client->weaponstate == WEAPON_READY)
	{
		bool const request_firing = ent->client->weapon_fire_buffered || ((ent->client->latched_buttons | ent->client->buttons) & BUTTON_ATTACK);

		if (request_firing && ent->client->weapon_fire_finished <= level.time)
		{
			ent->client->latched_buttons &= ~BUTTON_ATTACK;

			if (ent->client->pers.inventory[ent->client->pers.weapon->ammo])
			{
				ent->client->ps.gunframe = 1;
				ent->client->weaponstate = WEAPON_FIRING;
				ent->client->grenade_time = 0_ms;
				ent->client->weapon_think_time = level.time + Weapon_AnimationTime(ent);
			}
			else
				NoAmmoWeaponChange(ent, true);
			return;
		}
		else if (ent->client->weapon_think_time <= level.time)
		{
			ent->client->weapon_think_time = level.time + Weapon_AnimationTime(ent);

			if (ent->client->ps.gunframe >= FRAME_IDLE_LAST)
			{
				ent->client->ps.gunframe = FRAME_IDLE_FIRST;
				return;
			}

			if (pause_frames)
			{
				for (n = 0; pause_frames[n]; n++)
				{
					if (ent->client->ps.gunframe == pause_frames[n])
					{
						if (irandom(16))
							return;
					}
				}
			}

			ent->client->ps.gunframe++;
		}
		return;
	}

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		ent->client->last_firing_time = level.time + COOP_DAMAGE_FIRING_TIME;

		if (ent->client->weapon_think_time <= level.time)
		{
			if (prime_sound && ent->client->ps.gunframe == FRAME_PRIME_SOUND)
				gi.sound(ent, CHAN_WEAPON, gi.soundindex(prime_sound), 1, ATTN_NORM, 0);

			// [Paril-KEX] dualfire/time accel
			gtime_t grenade_wait_time = 0.1_sec;

			if (CTFApplyHaste(ent))
				grenade_wait_time *= 0.1f;
			if (is_quadfire)
				grenade_wait_time *= 0.1f;

			if (ent->client->ps.gunframe == FRAME_THROW_HOLD)
			{
				if (!ent->client->grenade_time && !ent->client->grenade_finished_time)
					ent->client->grenade_time = level.time + GRENADE_TIMER + 200_ms;

				if (primed_sound && !ent->client->grenade_blew_up)
					ent->client->weapon_sound = gi.soundindex(primed_sound);

				// they waited too long, detonate it in their hand
				if (EXPLODE && !ent->client->grenade_blew_up && level.time >= ent->client->grenade_time)
				{
					Weapon_PowerupSound(ent);
					ent->client->weapon_sound = 0;
					fire(ent, true);
					ent->client->grenade_blew_up = true;

					ent->client->grenade_finished_time = level.time + grenade_wait_time;
				}

				if (ent->client->buttons & BUTTON_ATTACK)
				{
					ent->client->weapon_think_time = level.time + 1_ms;
					return;
				}

				if (ent->client->grenade_blew_up)
				{
					if (level.time >= ent->client->grenade_finished_time)
					{
						ent->client->ps.gunframe = FRAME_FIRE_LAST;
						ent->client->grenade_blew_up = false;
						ent->client->weapon_think_time = level.time + Weapon_AnimationTime(ent);
					}
					else
					{
						return;
					}
				}
				else
				{
					ent->client->ps.gunframe++;

					Weapon_PowerupSound(ent);
					ent->client->weapon_sound = 0;
					fire(ent, false);

					if (!EXPLODE || !ent->client->grenade_blew_up)
						ent->client->grenade_finished_time = level.time + grenade_wait_time;

					if (!ent->deadflag && ent->s.modelindex == MODELINDEX_PLAYER && ent->health > 0) // VWep animations screw up corpses
					{
						if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
						{
							ent->client->anim_priority = ANIM_ATTACK;
							ent->s.frame = FRAME_crattak1 - 1;
							ent->client->anim_end = FRAME_crattak3;
						}
						else
						{
							ent->client->anim_priority = ANIM_ATTACK | ANIM_REVERSED;
							ent->s.frame = FRAME_wave08;
							ent->client->anim_end = FRAME_wave01;
						}
						ent->client->anim_time = 0_ms;
					}
				}
			}

			ent->client->weapon_think_time = level.time + Weapon_AnimationTime(ent);

			if ((ent->client->ps.gunframe == FRAME_FIRE_LAST) && (level.time < ent->client->grenade_finished_time))
				return;

			ent->client->ps.gunframe++;

			if (ent->client->ps.gunframe == FRAME_IDLE_FIRST)
			{
				ent->client->grenade_finished_time = 0_ms;
				ent->client->weaponstate = WEAPON_READY;
				ent->client->weapon_fire_buffered = false;
				Weapon_SetFinished(ent);

				if (extra_idle_frame)
					ent->client->ps.gunframe = FRAME_IDLE_LAST + 1;

				// Paril: if we ran out of the throwable, switch
				// so we don't appear to be holding one that we
				// can't throw
				if (!ent->client->pers.inventory[ent->client->pers.weapon->ammo])
				{
					NoAmmoWeaponChange(ent, false);
					ChangeWeapon(ent);
				}
			}
		}
	}
}

void Weapon_Grenade(edict_t* ent)
{
	constexpr int pause_frames[] = { 29, 34, 39, 48, 0 };

	Throw_Generic(ent, 15, 48, 5, "weapons/hgrena1b.wav", 11, 12, pause_frames, true, "weapons/hgrenc1b.wav", weapon_grenade_fire, true);

	// [Paril-KEX] skip the duped frame
	if (ent->client->ps.gunframe == 1)
		ent->client->ps.gunframe = 2;
}

/*
======================================================================

GRENADE LAUNCHER

======================================================================
*/

void weapon_grenadelauncher_fire(edict_t* ent)
{
	int	  damage = g_bouncygl->integer ? 95 : 115;
	float radius;

	radius = (float)(damage + 40);
	if (is_quad)
		damage *= damage_multiplier;

	vec3_t start, dir;
	// Paril: kill sideways angle on grenades
	// limit upwards angle so you don't fire it behind you
	P_ProjectSource(ent, { max(-62.5f, ent->client->v_angle[0]), ent->client->v_angle[1], ent->client->v_angle[2] }, { 8, 0, -8 }, start, dir);

	P_AddWeaponKick(ent, ent->client->v_forward * -2, { -1.f, 0.f, 0.f });

	fire_grenade(ent, start, dir, damage, 1200, 2.5_sec, radius, (crandom_open() * 10.0f), (200 + crandom_open() * 10.0f), false);

	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	gi.WriteByte(MZ_GRENADE | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	G_RemoveAmmo(ent);

	if (g_bouncygl->integer)
		G_RemoveAmmo(ent);
}

void Weapon_GrenadeLauncher(edict_t* ent)
{
	constexpr int pause_frames[] = { 34, 51, 59, 0 };
	constexpr int fire_frames[] = { 6, 0 };

	Weapon_Generic(ent, 5, 16, 59, 64, pause_frames, fire_frames, weapon_grenadelauncher_fire);
}

/*
======================================================================

ROCKET

======================================================================
*/

void Weapon_RocketLauncher_Fire(edict_t* ent)
{
	int	  damage;
	float damage_radius;
	int	  radius_damage;

	damage = irandom(100, 120);
	radius_damage = 125;
	damage_radius = 125;
	if (is_quad)
	{
		damage *= damage_multiplier;
		radius_damage *= damage_multiplier;
	}

	vec3_t start, dir;
	P_ProjectSource(ent, ent->client->v_angle, { 8, 8, -8 }, start, dir);
	fire_rocket(ent, start, dir, damage, 1230, damage_radius, radius_damage);

	P_AddWeaponKick(ent, ent->client->v_forward * -2, { -1.f, 0.f, 0.f });

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	gi.WriteByte(MZ_ROCKET | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	G_RemoveAmmo(ent);
}

void Weapon_RocketLauncher(edict_t* ent)
{
	constexpr int pause_frames[] = { 25, 33, 42, 50, 0 };
	constexpr int fire_frames[] = { 5, 0 };

	Weapon_Generic(ent, 4, 12, 50, 54, pause_frames, fire_frames, Weapon_RocketLauncher_Fire);
}

/*
======================================================================

BLASTER / HYPERBLASTER

======================================================================
*/

void Blaster_Fire(edict_t* ent, const vec3_t& g_offset, int damage, bool hyper, effects_t effect)
{
	if (is_quad)
		damage *= damage_multiplier;

	vec3_t start, dir;
	P_ProjectSource(ent, ent->client->v_angle, vec3_t{ 24, 8, -8 } + g_offset, start, dir);

	if (hyper)
		P_AddWeaponKick(ent, ent->client->v_forward * -2, { crandom() * 0.7f, crandom() * 0.7f, crandom() * 0.7f });
	else
		P_AddWeaponKick(ent, ent->client->v_forward * -2, { -1.f, 0.f, 0.f });

	// let the regular blaster projectiles travel a bit faster because it is a completely useless gun
	int const speed = hyper ? 1700 : 1300;
	//left hb / right blaster
	!hyper ? fire_blaster(ent, start, dir, damage, speed, effect, hyper ? MOD_HYPERBLASTER : MOD_BLASTER, 5)
	: fire_blaster_bolt(ent, start, dir, damage, speed, effect, hyper ? MOD_HYPERBLASTER : MOD_BLASTER, 3);

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	if (hyper)
		gi.WriteByte(MZ_HYPERBLASTER | is_silenced);
	else
		gi.WriteByte(MZ_BLASTER | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	PlayerNoise(ent, start, PNOISE_WEAPON);
}

void Weapon_Blaster_Fire(edict_t* ent)
{
	// give the blaster 15 across the board instead of just in dm
	int const damage = irandom(14, 18);
	Blaster_Fire(ent, vec3_origin, damage, false, EF_BLASTER);
}

void Weapon_Blaster(edict_t* ent)
{
	constexpr int pause_frames[] = { 19, 32, 0 };
	constexpr int fire_frames[] = { 5, 0 };

	Weapon_Generic(ent, 4, 8, 52, 55, pause_frames, fire_frames, Weapon_Blaster_Fire);
}

void Weapon_HyperBlaster_Fire(edict_t* ent)
{
	float	  rotation;
	vec3_t	  offset;
	int		  damage;

	// start on frame 6
	if (ent->client->ps.gunframe > 20)
		ent->client->ps.gunframe = 6;
	else
		ent->client->ps.gunframe++;

	// if we reached end of loop, have ammo & holding attack, reset loop
	// otherwise play wind down
	if (ent->client->ps.gunframe == 12)
	{
		if (ent->client->pers.inventory[ent->client->pers.weapon->ammo] && (ent->client->buttons & BUTTON_ATTACK))
			ent->client->ps.gunframe = 6;
		else
			gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/hyprbd1a.wav"), 1, ATTN_NORM, 0);
	}

	// play weapon sound for firing loop
	if (ent->client->ps.gunframe >= 6 && ent->client->ps.gunframe <= 11)
		ent->client->weapon_sound = gi.soundindex("weapons/hyprbl1a.wav");
	else
		ent->client->weapon_sound = 0;

	// fire frames
	const bool request_firing = ent->client->weapon_fire_buffered || (ent->client->buttons & BUTTON_ATTACK);

	if (request_firing)
	{
		if (ent->client->ps.gunframe >= 6 && ent->client->ps.gunframe <= 11)
		{
			ent->client->weapon_fire_buffered = false;

			if (!ent->client->pers.inventory[ent->client->pers.weapon->ammo])
			{
				NoAmmoWeaponChange(ent, true);
				return;
			}

			rotation = (ent->client->ps.gunframe - 5) * 2 * PIf / 6;
			offset[0] = -4 * sinf(rotation);
			offset[2] = 0;
			offset[1] = 4 * cosf(rotation);

			if (G_IsDeathmatch())
				damage = 10;
			else
				damage = 10;
			Blaster_Fire(ent, offset, damage, true, ((ent->client->ps.gunframe - 6) % 4) == 0 ? EF_HYPERBLASTER : EF_NONE);
			Weapon_PowerupSound(ent);

			G_RemoveAmmo(ent);

			ent->client->anim_priority = ANIM_ATTACK;
			if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crattak1 - (int)(frandom() + 0.25f);
				ent->client->anim_end = FRAME_crattak9;
			}
			else
			{
				ent->s.frame = FRAME_attack1 - (int)(frandom() + 0.25f);
				ent->client->anim_end = FRAME_attack8;
			}
			ent->client->anim_time = 0_ms;
		}
	}
}

void Weapon_HyperBlaster(edict_t* ent)
{
	constexpr int pause_frames[] = { 0 };

	Weapon_Repeating(ent, 5, 20, 49, 53, pause_frames, Weapon_HyperBlaster_Fire);
}

/*
======================================================================

MACHINEGUN / CHAINGUN

======================================================================
*/
// Constants for better readability and maintainability
constexpr float KICK_RANDOM_SCALE = 0.35f;
constexpr float KICK_ANGLE_SCALE = 0.7f;
constexpr float GUN_HEIGHT_ADJUST = 5.0f;
constexpr int MACHINEGUN_FRAME_FIRE1 = 4;
constexpr int MACHINEGUN_FRAME_FIRE2 = 5;
constexpr int MACHINEGUN_FRAME_IDLE = 6;
constexpr int TRACER_DAMAGE = 40;
constexpr gtime_t TRACER_COOLDOWN = 500_ms;

// Gun offset vectors
constexpr vec3_t GUN_OFFSET = { 0.0f, 8.0f, -8.0f }; // -8 relative to viewheight
constexpr vec3_t TRACER_OFFSET_STANDING = { 0.0f, 10.5f, -11.0f };
constexpr vec3_t TRACER_OFFSET_DUCKED = { 0.0f, 8.0f, -6.0f };

void Machinegun_Fire(edict_t* ent)
{
	if (!ent || !ent->client)
		return;

	int damage = irandom(7, 10);
	int kick = 2;

	// Handle not firing
	if (!(ent->client->buttons & BUTTON_ATTACK)) {
		ent->client->machinegun_shots = 0;
		ent->client->ps.gunframe = MACHINEGUN_FRAME_IDLE;
		return;
	}

	// Toggle between firing frames
	ent->client->ps.gunframe = (ent->client->ps.gunframe == MACHINEGUN_FRAME_FIRE1)
		? MACHINEGUN_FRAME_FIRE2
		: MACHINEGUN_FRAME_FIRE1;

	// Check ammo
	if (ent->client->pers.inventory[ent->client->pers.weapon->ammo] < 1) {
		ent->client->ps.gunframe = MACHINEGUN_FRAME_IDLE;
		NoAmmoWeaponChange(ent, true);
		return;
	}

	// Apply quad damage modifier if active
	if (is_quad) {
		damage *= damage_multiplier;
		kick *= damage_multiplier;
	}

	// Initialize kick vectors
	vec3_t kick_origin{};
	vec3_t kick_angles{};
	for (int i = 0; i < 3; i++) {
		kick_origin[i] = crandom() * KICK_RANDOM_SCALE;
		kick_angles[i] = crandom() * KICK_ANGLE_SCALE;
	}
	P_AddWeaponKick(ent, kick_origin, kick_angles);

	// Calculate firing vectors
	vec3_t start;
	auto [forward, right, up] = AngleVectors(ent->client->v_angle);

	// Calculate gun position with viewheight adjustment
	vec3_t offset = GUN_OFFSET;
	offset[2] += ent->viewheight; // Adjust for player's view height

	P_ProjectSource(ent, ent->client->v_angle, offset, start, forward, true);
	start[2] -= GUN_HEIGHT_ADJUST;  // Additional height adjustment

	// Fire with lag compensation
	G_LagCompensate(ent, start, forward);
	fire_bullet(ent, start, forward, damage, kick,
		DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_MACHINEGUN);
	G_UnLagCompensate();

	// Play weapon sound with power-up effects
	Weapon_PowerupSound(ent);

	// Send muzzle flash effect to clients
	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	gi.WriteByte(MZ_MACHINEGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	// Generate noise event for AI awareness
	PlayerNoise(ent, start, PNOISE_WEAPON);

	// Remove ammo
	G_RemoveAmmo(ent);

	// Handle tracer logic - only if enabled and cooldown has elapsed
	if (g_tracedbullets->integer && ent->lasthbshot <= level.time) {
		// Select appropriate offset based on stance
		const vec3_t& tracer_offset = (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			? TRACER_OFFSET_DUCKED
			: TRACER_OFFSET_STANDING;

		// Calculate tracer starting position
		vec3_t tracer_start = start;
		for (int i = 0; i < 3; i++) {
			tracer_start[i] += tracer_offset[i];
		}

		// Calculate direction vector
		vec3_t dir;
		P_ProjectSource(ent, ent->client->v_angle, tracer_offset, tracer_start, dir);

		// Fire tracer (could add randomization back if desired)
		// if (frandom() < 0.3f) {
		fire_blaster2(ent, tracer_start, dir, TRACER_DAMAGE, 3150, EF_NONE, false);
		// }

		// Reset cooldown
		ent->lasthbshot = level.time + TRACER_COOLDOWN;
	}

	// Configure player animation based on stance
	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED) {
		ent->s.frame = FRAME_crattak1 - (int)(frandom() + 0.25f);
		ent->client->anim_end = FRAME_crattak9;
	}
	else {
		ent->s.frame = FRAME_attack1 - (int)(frandom() + 0.25f);
		ent->client->anim_end = FRAME_attack8;
	}
	ent->client->anim_time = 0_ms;
}
void Weapon_Machinegun(edict_t* ent)
{
	constexpr int pause_frames[] = { 23, 45, 0 };

	Weapon_Repeating(ent, 3, 5, 45, 49, pause_frames, Machinegun_Fire);
}

void chaingun_smoke(edict_t* ent)
{
	vec3_t tempVec, dir;
	// Aumentamos el primer valor (forward) para mover el humo más adelante
	// Opciones de offset:
	// Original: { 8, 8, -4 }  // cerca del arma
	// Opción 1: { 20, 8, -4 } // un poco más adelante
	// Opción 2: { 32, 8, -4 } // bastante más adelante
	// Opción 3: { 48, 8, -4 } // muy adelante

	P_ProjectSource(ent, ent->client->v_angle, { 20, 8, -4 }, tempVec, dir);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_CHAINFIST_SMOKE);
	gi.WritePosition(tempVec);
	gi.unicast(ent, 0);
}

constexpr int SPINUP_FRAMES = 5;      // Number of frames for spin-up
constexpr int SPINDOWN_FRAMES = 2;    // Number of frames for spin-down
// Constants for better readability and maintainability
constexpr float KICK_BASE_ANGLE = 0.5f;
constexpr float KICK_PER_SHOT = 0.15f;
constexpr int CHAINGUN_START_FRAME = 5;
constexpr int CHAINGUN_END_FRAME = 21;
constexpr int CHAINGUN_PAUSE_FRAME = 14;
constexpr int CHAINGUN_LOOP_FRAME = 15;
constexpr int CHAINGUN_SPINDOWN_FRAME = 32;
constexpr int CHAINGUN_SOUND_FRAME = 22;
constexpr int CHAINGUN_READY_FRAME = 31;
constexpr int CHAINGUN_SINGLE_SHOT_FRAME = 9;
constexpr int CHAINGUN_DOUBLE_SHOT_FRAME = 14;
constexpr int CG_TRACER_DMG = 20;
constexpr gtime_t CG_TRACER_COOLDOWN = 200_ms;

void Chaingun_Fire(edict_t* ent)
{
	if (!ent || !ent->client)
		return;

	int shots;
	int damage = irandom(5, 9);
	int kick = 2;

	// Handle gun state transitions
	if (ent->client->ps.gunframe > CHAINGUN_READY_FRAME) {
		// Restart the firing sequence
		ent->client->ps.gunframe = CHAINGUN_START_FRAME;
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnu1a.wav"), 1, ATTN_IDLE, 0);
	}
	else if ((ent->client->ps.gunframe == CHAINGUN_PAUSE_FRAME) &&
		!(ent->client->buttons & BUTTON_ATTACK)) {
		// Player released trigger during firing - start spin down
		ent->client->ps.gunframe = CHAINGUN_SPINDOWN_FRAME;
		ent->client->weapon_sound = 0;
		return;
	}
	else if ((ent->client->ps.gunframe == CHAINGUN_END_FRAME) &&
		(ent->client->buttons & BUTTON_ATTACK) &&
		ent->client->pers.inventory[ent->client->pers.weapon->ammo]) {
		// Player still holding trigger at end of sequence - loop back
		ent->client->ps.gunframe = CHAINGUN_LOOP_FRAME;
	}
	else {
		// Normal frame advancement
		ent->client->ps.gunframe++;
	}

	// Handle spindown sound
	if (ent->client->ps.gunframe == CHAINGUN_SOUND_FRAME) {
		ent->client->weapon_sound = 0;
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnd1a.wav"), 1, ATTN_IDLE, 0);
	}

	// Only fire when in active frames
	if (ent->client->ps.gunframe < CHAINGUN_START_FRAME ||
		ent->client->ps.gunframe > CHAINGUN_END_FRAME)
		return;

	// Set firing sound
	ent->client->weapon_sound = gi.soundindex("weapons/chngnl1a.wav");

	// Determine number of shots based on frame
	if (ent->client->ps.gunframe <= CHAINGUN_SINGLE_SHOT_FRAME)
		shots = 1;
	else if (ent->client->ps.gunframe <= CHAINGUN_DOUBLE_SHOT_FRAME)
		shots = (ent->client->buttons & BUTTON_ATTACK) ? 2 : 1;
	else
		shots = 3;

	// Configure player animation based on stance
	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED) {
		ent->s.frame = FRAME_crattak1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_crattak9;
	}
	else {
		ent->s.frame = FRAME_attack1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_attack8;
	}
	ent->client->anim_time = 0_ms;

	// Check ammo
	if (ent->client->pers.inventory[ent->client->pers.weapon->ammo] < shots) {
		shots = ent->client->pers.inventory[ent->client->pers.weapon->ammo];
	}

	if (!shots) {
		NoAmmoWeaponChange(ent, true);
		return;
	}

	// Apply quad damage modifier if active
	if (is_quad) {
		damage *= damage_multiplier;
		kick *= damage_multiplier;
	}

	// Initialize kick vectors - more kick for more shots
	vec3_t kick_origin{};
	vec3_t kick_angles{};
	for (int i = 0; i < 3; i++) {
		kick_origin[i] = crandom() * KICK_RANDOM_SCALE;
		kick_angles[i] = crandom() * (KICK_BASE_ANGLE + (shots * KICK_PER_SHOT));
	}
	P_AddWeaponKick(ent, kick_origin, kick_angles);

	// Calculate firing vectors
	vec3_t start;
	auto [forward, right, up] = AngleVectors(ent->client->v_angle);

	// Calculate gun position with viewheight adjustment
	vec3_t offset = GUN_OFFSET;
	offset[2] += ent->viewheight; // Adjust for player's view height

	P_ProjectSource(ent, ent->client->v_angle, offset, start, forward, true);
	start[2] -= GUN_HEIGHT_ADJUST;  // Additional height adjustment

	// Fire with lag compensation
	G_LagCompensate(ent, start, forward);
	for (int i = 0; i < shots; i++) {
		fire_bullet(ent, start, forward, damage, kick,
			DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_CHAINGUN);
	}
	G_UnLagCompensate();

	// Play weapon sound with power-up effects
	Weapon_PowerupSound(ent);

	// Send muzzle flash effect to clients - intensity based on shot count
	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	gi.WriteByte((MZ_CHAINGUN1 + shots - 1) | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	// Generate noise event for AI awareness
	PlayerNoise(ent, start, PNOISE_WEAPON);

	// Remove ammo based on shot count
	G_RemoveAmmo(ent, shots);

	// Handle tracer logic - only if enabled and cooldown has elapsed
	if (g_tracedbullets->integer && ent->lasthbshot <= level.time) {
		// Select appropriate offset based on stance
		const vec3_t& tracer_offset = (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			? TRACER_OFFSET_DUCKED
			: TRACER_OFFSET_STANDING;

		// Calculate tracer starting position
		vec3_t tracer_start = start;
		for (int i = 0; i < 3; i++) {
			tracer_start[i] += tracer_offset[i];
		}

		// Calculate direction vector
		vec3_t dir;
		P_ProjectSource(ent, ent->client->v_angle, tracer_offset, tracer_start, dir, true);

		// Fire tracer
		fire_blaster2(ent, tracer_start, dir, CG_TRACER_DMG, 3150, EF_NONE, false);

		// Reset cooldown
		ent->lasthbshot = level.time + CG_TRACER_COOLDOWN;
	}
}

void Weapon_Chaingun(edict_t* ent)
{
	if (g_improvedchaingun->integer) {
		constexpr int pause_frames[] = { 0 };
		Weapon_Repeating(ent, 1, SPINUP_FRAMES + SPINDOWN_FRAMES,
			SPINUP_FRAMES + SPINDOWN_FRAMES + 1,
			SPINUP_FRAMES + SPINDOWN_FRAMES + 1,
			pause_frames, Chaingun_Fire);
	}
	else {
		constexpr int pause_frames[] = { 38, 43, 51, 61, 0 };
		Weapon_Repeating(ent, 4, 31, 61, 64, pause_frames, Chaingun_Fire);
	}
}
/*
======================================================================

SHOTGUN / SUPERSHOTGUN

======================================================================
*/

void weapon_shotgun_fire(edict_t* ent)
{
	int damage;
	int kick = 8;

	damage = irandom(3, 5);
	vec3_t start, dir;
	// Paril: kill sideways angle on hitscan
	P_ProjectSource(ent, ent->client->v_angle, { 0, 0, -8 }, start, dir, true);

	P_AddWeaponKick(ent, ent->client->v_forward * -2, { -2.f, 0.f, 0.f });

	if (is_quad)
	{
		damage *= damage_multiplier;
		kick *= damage_multiplier;
	}

	G_LagCompensate(ent, start, dir);
	if (G_IsDeathmatch())
		fire_shotgun(ent, start, dir, damage, kick, 500, 500, DEFAULT_DEATHMATCH_SHOTGUN_COUNT, MOD_SHOTGUN);
	else
		fire_shotgun(ent, start, dir, damage, kick, 500, 500, DEFAULT_SHOTGUN_COUNT, MOD_SHOTGUN);
	G_UnLagCompensate();

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	gi.WriteByte(MZ_SHOTGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	G_RemoveAmmo(ent);
}

void Weapon_Shotgun(edict_t* ent)
{
	constexpr int pause_frames[] = { 22, 28, 34, 0 };
	constexpr int fire_frames[] = { 8, 0 };

	Weapon_Generic(ent, 7, 18, 36, 39, pause_frames, fire_frames, weapon_shotgun_fire);
}

void weapon_supershotgun_fire(edict_t* ent)
{
	int damage;
	int kick = 17;

	damage = irandom(7, 10);
	//	damage = irandom(8, 13); (vanilla 6)

	if (is_quad)
	{
		damage *= damage_multiplier;
		kick *= damage_multiplier;
	}

	vec3_t start, dir;
	// Paril: kill sideways angle on hitscan
	P_ProjectSource(ent, ent->client->v_angle, { 0, 0, -8 }, start, dir);
	G_LagCompensate(ent, start, dir);
	vec3_t v;
	v[PITCH] = ent->client->v_angle[PITCH];
	v[YAW] = ent->client->v_angle[YAW] - 5;
	v[ROLL] = ent->client->v_angle[ROLL];
	// Paril: kill sideways angle on hitscan
	P_ProjectSource(ent, v, { 0, 0, -8 }, start, dir, true);
	fire_shotgun(ent, start, dir, damage, kick, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD, DEFAULT_SSHOTGUN_COUNT / 2, MOD_SSHOTGUN);
	v[YAW] = ent->client->v_angle[YAW] + 5;
	P_ProjectSource(ent, v, { 0, 0, -8 }, start, dir, true);
	fire_shotgun(ent, start, dir, damage, kick, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD, DEFAULT_SSHOTGUN_COUNT / 2, MOD_SSHOTGUN);
	G_UnLagCompensate();
	// DEFAULT_SSHOTGUN_COUNT /2.7
	P_AddWeaponKick(ent, ent->client->v_forward * -2, { -2.f, 0.f, 0.f });

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	gi.WriteByte(MZ_SSHOTGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	G_RemoveAmmo(ent);
}

void Weapon_SuperShotgun(edict_t* ent)
{
	constexpr int pause_frames[] = { 29, 42, 57, 0 };
	constexpr int fire_frames[] = { 7, 0 };

	Weapon_Generic(ent, 6, 17, 57, 61, pause_frames, fire_frames, weapon_supershotgun_fire);
}

/*
======================================================================

RAILGUN

======================================================================
*/

void weapon_railgun_fire(edict_t* ent)
{
	int damage, kick;

	if (G_IsDeathmatch() && g_horde->integer)
	{
		damage = 225;
		kick = 285;
	}
	else
	{
		damage = 150;
		kick = 285;
	}

	if (is_quad)
	{
		damage *= damage_multiplier;
		kick *= damage_multiplier;
	}

	vec3_t start, dir;
	P_ProjectSource(ent, ent->client->v_angle, { 0, 7, -8 }, start, dir, true);
	G_LagCompensate(ent, start, dir);
	fire_rail(ent, start, dir, damage, kick);
	G_UnLagCompensate();

	P_AddWeaponKick(ent, ent->client->v_forward * -3, { -3.f, 0.f, 0.f });

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	gi.WriteByte(MZ_RAILGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	G_RemoveAmmo(ent);
}

void Weapon_Railgun(edict_t* ent)
{
	constexpr int pause_frames[] = { 56, 0 };
	constexpr int fire_frames[] = { 4, 0 };

	Weapon_Generic(ent, 3, 18, 56, 61, pause_frames, fire_frames, weapon_railgun_fire);
}



float P_CurrentBFGKickFactor(edict_t* ent)
{
	if (ent->client->kick.time < level.time)
		return 0.f;
	float const f = (ent->client->kick.time - level.time).seconds() / ent->client->kick.total.seconds();
	// Add easing function for smoother kick
	return sinf((1.0f - f) * (PIf * 0.5f));
}

void P_ApplyContinuousKick(edict_t* ent, float dt)
{
	if (ent->client->kick.time >= level.time)
	{
		float const factor = P_CurrentBFGKickFactor(ent);
		ent->client->kick_origin = ent->client->kick.origin * factor;
		ent->client->v_dmg_roll = ent->client->kick.angles[ROLL] * factor;
		ent->client->v_dmg_pitch = ent->client->kick.angles[PITCH] * factor;
	}
	else
	{
		ent->client->kick_origin = vec3_origin;
		ent->client->v_dmg_roll = 0;
		ent->client->v_dmg_pitch = 0;
	}
}

void weapon_bfg_fire(edict_t* ent)
{
	int   damage;
	float const damage_radius = 1000;

	if (G_IsDeathmatch())
		damage = 700;
	else
		damage = 700;

	// Handle muzzle flash for pull mode
	if (!g_bfgslide->integer && ent->client->ps.gunframe == 9)
	{
		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(MZ_BFG | is_silenced);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);
		PlayerNoise(ent, ent->s.origin, PNOISE_WEAPON);
		return;
	}

	// Check ammo
	int const required_ammo = g_bfgslide->integer ? 25 : 50;
	if (ent->client->pers.inventory[ent->client->pers.weapon->ammo] < required_ammo)
		return;

	if (is_quad)
		damage *= damage_multiplier;

	vec3_t start, dir;
	P_ProjectSource(ent, ent->client->v_angle, { 8, 8, -8 }, start, dir);

	// Fire BFG 
	fire_bfg(ent, start, dir, damage, 600, damage_radius);

	// Apply weapon kick
	P_AddWeaponKick(ent, ent->client->v_forward * -2, { -20.f, 0, crandom() * 8 });
	ent->client->kick.total = DAMAGE_TIME();
	ent->client->kick.time = level.time + ent->client->kick.total;

	// Muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	gi.WriteByte(MZ_BFG2 | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	// Remove ammo
	if (g_bfgslide->integer) {
		G_RemoveAmmo(ent);
	}
	else
	{
		G_RemoveAmmo(ent);
		G_RemoveAmmo(ent);
	}

	// Advance gunframe
	if (g_bfgslide->integer)
		ent->client->ps.gunframe = 17;
	else
		ent->client->ps.gunframe++;
}


void Weapon_BFG(edict_t* ent)
{
	constexpr int pause_frames[] = { 39, 45, 50, 55, 0 };
	constexpr int fire_frames[] = { 9, 17, 0 };

	Weapon_Generic(ent, 8, 32, 54, 58, pause_frames, fire_frames, weapon_bfg_fire);
}

//======================================================================

void weapon_disint_fire(edict_t* self)
{
	vec3_t start, dir;
	P_ProjectSource(self, self->client->v_angle, { 24, 8, -8 }, start, dir);

	P_AddWeaponKick(self, self->client->v_forward * -2, { -1.f, 0.f, 0.f });

	fire_disintegrator(self, start, dir, 1200);

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(self);
	gi.WriteByte(MZ_BLASTER2);
	gi.multicast(self->s.origin, MULTICAST_PVS, false);

	PlayerNoise(self, start, PNOISE_WEAPON);

	G_RemoveAmmo(self);
}

void Weapon_Beta_Disintegrator(edict_t* ent)
{
	constexpr int pause_frames[] = { 30, 37, 45, 0 };
	constexpr int fire_frames[] = { 17, 0 };

	Weapon_Generic(ent, 16, 23, 46, 50, pause_frames, fire_frames, weapon_disint_fire);
}