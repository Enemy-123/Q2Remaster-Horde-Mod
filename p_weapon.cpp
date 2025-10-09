// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_weapon.c

#include "g_local.h"
#include "horde/g_horde_benefits.h"
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
void weapon_tesla_fire(edict_t* ent, bool held);
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

	// Don't give ammo for start items (weapons given via g_start_items)
	if ((!(ent->spawnflags & SPAWNFLAG_ITEM_DROPPED) || g_horde->integer) &&
	    !(ent->spawnflags & SPAWNFLAG_ITEM_START_ITEM))
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
					given_quantity = static_cast<int>(given_quantity * multiplier);
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
		const bool using_ripper = ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_IONRIPPER;
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
		if (using_blaster || using_glauncher || using_etfrifle || using_proxlauncher || using_ripper) {
			final_multiplier *= 1.4f;
		}
	
		if (using_shotgun) {
			final_multiplier *= 1.5f;
		}
		if (using_tesla) {
			final_multiplier *= 4.0f; // Much faster Tesla deployment
		}

		if (using_trap) {
			final_multiplier *= 4.0f; // Much faster Trap deployment
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
	// Check for menu protection first - prevent any weapon handling while in menu
	if (ent->client->menu_protected) {
		// Keep weapon in ready state but don't process any actions
		if (ent->client->weaponstate == WEAPON_READY) {
			// Maintain idle animation
			if (ent->client->ps.gunframe < FRAME_IDLE_FIRST || ent->client->ps.gunframe > FRAME_IDLE_LAST)
				ent->client->ps.gunframe = FRAME_IDLE_FIRST;
		}
		return READY_NONE; // Cannot fire or change weapons while in menu
	}

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
	int	  damage = g_config.grenade.damage;
	int	  speed;
	float radius;

	radius = static_cast<float>(damage + g_config.grenade.radius_offset);

	// Apply Hand Grenade upgrades
	if (ent && ent->client)
	{
		// Damage upgrade: initial 200 + (level * 10)
		damage += static_cast<int>(ent->client->pers.skills.hg_damage * 10);

		// Range upgrade affects throw distance (GRENADE_MINSPEED/MAXSPEED)
		// Note: Range is handled elsewhere, not in radius calculation

		// Radius damage upgrade: affects explosion radius size (adds 10 per level)
		radius += ent->client->pers.skills.hg_radius_damage * 10.0f;
	}

	if (is_quad)
		damage *= damage_multiplier;

	vec3_t start, dir;
	// Paril: kill sideways angle on grenades
	// limit upwards angle so you don't throw behind you
	P_ProjectSource(ent, { max(-62.5f, ent->client->v_angle[0]), ent->client->v_angle[1], ent->client->v_angle[2] }, { 2, 0, -14 }, start, dir);

	gtime_t const timer = ent->client->grenade_time - level.time;
	speed = static_cast<int>(ent->health <= 0 ? GRENADE_MINSPEED : min(GRENADE_MINSPEED + (GRENADE_TIMER - timer).seconds() * ((GRENADE_MAXSPEED - GRENADE_MINSPEED) / GRENADE_TIMER.seconds()), GRENADE_MAXSPEED));

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

			// Faster deployment for Tesla and Trap weapons
			if (fire == weapon_tesla_fire || fire == weapon_trap_fire)
				grenade_wait_time = 0.01_sec; // Very fast for deployables (10ms)

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
	bool napalm = PlayerHasNapalmGL(ent) || (ent->client && ent->client->pers.skills.gl_bouncy);
	int	  damage = napalm ? g_config.grenadelauncher.damage_napalm : g_config.grenadelauncher.damage_normal;
	float radius = napalm ? g_config.grenadelauncher.radius_napalm : g_config.grenadelauncher.radius_normal;

	// Apply weapon upgrades from player skills
	if (ent && ent->client)
	{
		// Damage upgrade: initial 100 + (level * 6)
		damage += static_cast<int>(ent->client->pers.skills.gl_damage * 6);

		// Radius upgrade: initial 100 + (level * 2.5)
		radius += ent->client->pers.skills.gl_radius * 2.5f;
	}

	if (is_quad)
		damage *= damage_multiplier;

	vec3_t start, dir;
	// Paril: kill sideways angle on grenades
	// limit upwards angle so you don't fire it behind you
	P_ProjectSource(ent, { max(-62.5f, ent->client->v_angle[0]), ent->client->v_angle[1], ent->client->v_angle[2] }, { 8, 0, -8 }, start, dir);

	P_AddWeaponKick(ent, ent->client->v_forward * -2, { -1.f, 0.f, 0.f });

	// Speed upgrade: initial 600 + (level * 30)
	int speed = g_config.grenadelauncher.speed;
	if (ent && ent->client)
		speed += ent->client->pers.skills.gl_range * 30;

	fire_grenade(ent, start, dir, damage, speed, 2.5_sec, radius, (crandom_open() * 10.0f), (200 + crandom_open() * 10.0f), false);

	// Apply silent mode (no muzzle flash)
	bool is_silent = (ent && ent->client && ent->client->pers.skills.gl_silent);
	if (!is_silent)
	{
		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(MZ_GRENADE | is_silenced);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);

		PlayerNoise(ent, start, PNOISE_WEAPON);
	}

	G_RemoveAmmo(ent);

	if (PlayerHasNapalmGL(ent) || (ent->client && ent->client->pers.skills.gl_bouncy))
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

	damage = irandom(g_config.rocket.damage_min, g_config.rocket.damage_max);
	radius_damage = g_config.rocket.radius;
	damage_radius = g_config.rocket.radius;

	// Apply weapon upgrades from player skills
	if (ent && ent->client)
	{
		// Damage upgrade: base + (level * 3.5)
		damage += static_cast<int>(ent->client->pers.skills.rl_damage * 3.5f);
		radius_damage += static_cast<int>(ent->client->pers.skills.rl_damage * 3.5f);

		// Radius upgrade: base + (level * 2.5)
		damage_radius += ent->client->pers.skills.rl_radius * 2.5f;
	}

	if (is_quad)
	{
		damage *= damage_multiplier;
		radius_damage *= damage_multiplier;
	}

	vec3_t start, dir;
	P_ProjectSource(ent, ent->client->v_angle, { 8, 8, -8 }, start, dir);

	// Speed upgrade: base + (level * 28)
	int speed = g_config.rocket.speed;
	if (ent && ent->client)
		speed += ent->client->pers.skills.rl_range * 28;

	fire_rocket(ent, start, dir, damage, speed, damage_radius, radius_damage);

	P_AddWeaponKick(ent, ent->client->v_forward * -2, { -1.f, 0.f, 0.f });

	// Apply silent mode (no muzzle flash)
	bool is_silent = (ent && ent->client && ent->client->pers.skills.rl_silent);
	if (!is_silent)
	{
		// send muzzle flash
		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(MZ_ROCKET | is_silenced);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);

		PlayerNoise(ent, start, PNOISE_WEAPON);
	}

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
	int speed = hyper ? g_config.hyperblaster.speed : g_config.blaster.speed;
	int const bounces = hyper ? g_config.hyperblaster.bounces : g_config.blaster.bounces;

	// Apply speed upgrades
	if (ent && ent->client)
	{
		if (hyper)
			speed += ent->client->pers.skills.hb_range * 40;
		else
			speed += ent->client->pers.skills.bl_range * 40;
	}

	//left hb / right blaster
	!hyper ? fire_blaster(ent, start, dir, damage, speed, effect, hyper ? MOD_HYPERBLASTER : MOD_BLASTER, bounces)
	: fire_blaster_bolt(ent, start, dir, damage, speed, effect, hyper ? MOD_HYPERBLASTER : MOD_BLASTER, bounces);

	// send muzzle flash (check for silent mode)
	bool silent_mode = false;
	if (ent && ent->client)
	{
		silent_mode = hyper ? ent->client->pers.skills.hb_silent : ent->client->pers.skills.bl_silent;
	}

	if (!silent_mode)
	{
		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		if (hyper)
			gi.WriteByte(MZ_HYPERBLASTER | is_silenced);
		else
			gi.WriteByte(MZ_BLASTER | is_silenced);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);
	}

	PlayerNoise(ent, start, PNOISE_WEAPON);
}

void Weapon_Blaster_Fire(edict_t* ent)
{
	// Vortex-style blaster ammo system - check for ammo first
	if (ent->client->blaster_ammo < 1)
	{
		// Play no ammo sound
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1_sec;
		}
		// Don't fire but let the animation continue normally
		return;
	}

	// Consume blaster ammo
	ent->client->blaster_ammo--;

	// reduced damage to balance with Strength Tech (4x multiplier)
	int damage_min = g_config.blaster.damage_min;
	int damage_max = g_config.blaster.damage_max;

	// Apply Blaster upgrades
	if (ent && ent->client)
	{
		// Damage upgrade: min +2 per level, max +5 per level
		damage_min += ent->client->pers.skills.bl_damage * 2;
		damage_max += ent->client->pers.skills.bl_damage * 5;
	}

	int const damage = irandom(damage_min, damage_max);

	// Determine effect based on trails setting
	effects_t effect = EF_BLASTER;
	if (ent && ent->client && ent->client->pers.skills.bl_trails)
		effect = EF_NONE;  // Disable trails

	Blaster_Fire(ent, vec3_origin, damage, false, effect);
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

			// Apply Hyperblaster damage upgrades
			int damage_min = g_config.hyperblaster.damage_min;
			int damage_max = g_config.hyperblaster.damage_max;
			if (ent && ent->client)
			{
				// Damage upgrade varies by config - using the difference between min and max
				float upgrade_per_level = (damage_max - damage_min) / 10.0f;
				damage_min += static_cast<int>(ent->client->pers.skills.hb_damage * upgrade_per_level);
				damage_max += static_cast<int>(ent->client->pers.skills.hb_damage * upgrade_per_level);
			}

			damage = irandom(damage_min, damage_max);

			// Determine effect based on trails setting
			effects_t effect = ((ent->client->ps.gunframe - 6) % 4) == 0 ? EF_HYPERBLASTER : EF_NONE;
			if (ent && ent->client && ent->client->pers.skills.hb_trails)
				effect = EF_NONE;  // Disable trails

			Blaster_Fire(ent, offset, damage, true, effect);
			Weapon_PowerupSound(ent);

			G_RemoveAmmo(ent);

			ent->client->anim_priority = ANIM_ATTACK;
			if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crattak1 - static_cast<int>(frandom() + 0.25f);
				ent->client->anim_end = FRAME_crattak9;
			}
			else
			{
				ent->s.frame = FRAME_attack1 - static_cast<int>(frandom() + 0.25f);
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
constexpr gtime_t TRACER_COOLDOWN = 500_ms;

// Gun offset vectors
constexpr vec3_t GUN_OFFSET = { 0.0f, 8.0f, -8.0f }; // -8 relative to viewheight
constexpr vec3_t TRACER_OFFSET_STANDING = { 0.0f, 10.5f, -11.0f };
constexpr vec3_t TRACER_OFFSET_DUCKED = { 0.0f, 8.0f, -6.0f };

void Fire_TracerBullet(edict_t* ent, int damage, gtime_t cooldown_duration)
{
    if (!ent || !ent->client)
        return;

    // Check if player has tracer bullets from benefits or weapon upgrades
    bool has_traced = PlayerHasTracedBullets(ent);

    // Check weapon-specific tracer upgrades
    if (ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_MACHINEGUN)
        has_traced = has_traced || ent->client->pers.skills.mg_tracers > 0;
    else if (ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_CHAINGUN)
        has_traced = has_traced || ent->client->pers.skills.cg_tracers > 0;

    if (!has_traced || ent->client->resp.lasthbshot > level.time)
        return;

    // Scale tracer damage based on weapon upgrades
    int final_damage = damage;
    if (ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_MACHINEGUN && ent->client->pers.skills.mg_tracers > 0)
    {
        final_damage = ent->client->pers.skills.mg_tracers * g_config.machinegun.tracer_damage_per_level;
    }
    else if (ent->client->pers.weapon && ent->client->pers.weapon->id == IT_WEAPON_CHAINGUN && ent->client->pers.skills.cg_tracers > 0)
    {
        final_damage = ent->client->pers.skills.cg_tracers * g_config.chaingun.tracer_damage_per_level;
    }

    const vec3_t& tracer_offset = (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
        ? TRACER_OFFSET_DUCKED
        : TRACER_OFFSET_STANDING;

    vec3_t tracer_start;
    vec3_t dir;
    P_ProjectSource(ent, ent->client->v_angle, tracer_offset, tracer_start, dir, true);

    fire_blaster2(ent, tracer_start, dir, final_damage, 3150, EF_NONE, false);

    ent->client->resp.lasthbshot = level.time + cooldown_duration;
}



void Machinegun_Fire(edict_t* ent)
{
	if (!ent || !ent->client)
		return;

	int damage = irandom(g_config.machinegun.damage_min, g_config.machinegun.damage_max);
	int kick = g_config.machinegun.kick;

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

	// Apply machinegun damage upgrade (add 1 damage per level)
	damage += ent->client->pers.skills.mg_damage;

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

	// Calculate firing vectors correctly.
	// The offset is relative to the player's eye level. This fixes a bug
	// where viewheight was added twice and the start position was modified
	// after the direction vector was calculated.
	vec3_t start;
	vec3_t forward;
	vec3_t machinegun_offset = { 0.0f, 8.0f, -8.0f - GUN_HEIGHT_ADJUST };
	P_ProjectSource(ent, ent->client->v_angle, machinegun_offset, start, forward, true);

	// Apply spread reduction if upgraded (divide spread by 2)
	int hspread = DEFAULT_BULLET_HSPREAD;
	int vspread = DEFAULT_BULLET_VSPREAD;
	if (ent->client->pers.skills.mg_spread > 0)
	{
		hspread /= 1.5;
		vspread /= 1.5;
	}

	// Fire with lag compensation
	G_LagCompensate(ent, start, forward);
	fire_bullet(ent, start, forward, damage, kick, hspread, vspread, MOD_MACHINEGUN);
	G_UnLagCompensate();

	// Play weapon sound with power-up effects (unless silent mode)
	if (!ent->client->pers.skills.mg_silent)
	{
		Weapon_PowerupSound(ent);
	}

	// Send muzzle flash effect to clients (add MZ_SILENCED flag if silent mode)
	int flash_flags = MZ_MACHINEGUN | is_silenced;
	if (ent->client->pers.skills.mg_silent)
	{
		flash_flags |= MZ_SILENCED;
	}
	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	gi.WriteByte(flash_flags);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	// Generate noise event for AI awareness (unless silent mode)
	if (!ent->client->pers.skills.mg_silent)
	{
		PlayerNoise(ent, start, PNOISE_WEAPON);
	}

	// Remove ammo
	G_RemoveAmmo(ent);

	// Handle tracer logic using the helper function
	Fire_TracerBullet(ent, g_config.machinegun.tracer_damage, gtime_t::from_ms(g_config.machinegun.tracer_cooldown_ms));

	// Configure player animation based on stance
	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED) {
		ent->s.frame = FRAME_crattak1 - static_cast<int>(frandom() + 0.25f);
		ent->client->anim_end = FRAME_crattak9;
	}
	else {
		ent->s.frame = FRAME_attack1 - static_cast<int>(frandom() + 0.25f);
		ent->client->anim_end = FRAME_attack8;
	}
	ent->client->anim_time = 0_ms;
}

void Weapon_Machinegun(edict_t* ent)
{
	constexpr int pause_frames[] = { 23, 45, 0 };

	Weapon_Repeating(ent, 3, 5, 45, 49, pause_frames, Machinegun_Fire);
}

// Constants for Chaingun_Fire visuals and mechanics
constexpr float KICK_BASE_ANGLE = 0.5f;
constexpr float KICK_PER_SHOT = 0.15f;

// Frame numbers critical to Chaingun_Fire's internal logic
constexpr int CHAINGUN_START_FRAME = 5;
constexpr int CHAINGUN_END_FRAME = 21;
constexpr int CHAINGUN_PAUSE_FRAME = 14;
constexpr int CHAINGUN_LOOP_FRAME = 15;
constexpr int CHAINGUN_SPINDOWN_FRAME = 32;
constexpr int CHAINGUN_SOUND_FRAME = 22;
constexpr int CHAINGUN_READY_FRAME = 31;

// Shot logic frames
constexpr int CHAINGUN_SINGLE_SHOT_FRAME = 9;
constexpr int CHAINGUN_DOUBLE_SHOT_FRAME = 14;

// Tracer bullet specifics
constexpr int CG_TRACER_DMG = 20;
constexpr gtime_t CG_TRACER_COOLDOWN = 300_ms;

void Chaingun_Fire(edict_t* ent)
{
	if (!ent || !ent->client)
		return;

	int shots;
	int damage = irandom(g_config.chaingun.damage_min, g_config.chaingun.damage_max);
	int kick = g_config.chaingun.kick;

	// Apply chaingun damage upgrade (add 1 damage per level)
	damage += ent->client->pers.skills.cg_damage;

	// Apply spin upgrade by reducing frame transitions (skip frames for faster spin)
	int frame_skip = ent->client->pers.skills.cg_spin / 2; // Every 2 spin levels = 1 frame skip

	// Handle gun state transitions
	if (ent->client->ps.gunframe > CHAINGUN_READY_FRAME) {
		ent->client->ps.gunframe = CHAINGUN_START_FRAME;
		if (!ent->client->pers.skills.cg_silent)
		{
			gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnu1a.wav"), 1, ATTN_IDLE, 0);
		}
	}
	else if ((ent->client->ps.gunframe == CHAINGUN_PAUSE_FRAME) &&
		!(ent->client->buttons & BUTTON_ATTACK)) {
		ent->client->ps.gunframe = CHAINGUN_SPINDOWN_FRAME;
		ent->client->weapon_sound = 0;
		if (!ent->client->pers.skills.cg_silent)
		{
			gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnd1a.wav"), 1, ATTN_IDLE, 0);
		}
		return;
	}
	else if ((ent->client->ps.gunframe == CHAINGUN_END_FRAME) &&
		(ent->client->buttons & BUTTON_ATTACK) &&
		ent->client->pers.inventory[ent->client->pers.weapon->ammo]) {
		ent->client->ps.gunframe = CHAINGUN_LOOP_FRAME;
	}
	else {
		ent->client->ps.gunframe++;
	}

	if (ent->client->ps.gunframe == CHAINGUN_SOUND_FRAME) {
		ent->client->weapon_sound = 0;
		if (!ent->client->pers.skills.cg_silent)
		{
			gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnd1a.wav"), 1, ATTN_IDLE, 0);
		}
	}

    if (ent->client->ps.gunframe >= CHAINGUN_SPINDOWN_FRAME) {
        ent->client->weapon_sound = 0;
        return;
    }

	if (ent->client->ps.gunframe < CHAINGUN_START_FRAME || ent->client->ps.gunframe > CHAINGUN_END_FRAME) {
		return;
    }

	// Set weapon loop sound (unless silent mode)
	if (!ent->client->pers.skills.cg_silent)
	{
		ent->client->weapon_sound = gi.soundindex("weapons/chngnl1a.wav");
	}
	else
	{
		ent->client->weapon_sound = 0;
	}

	if (ent->client->ps.gunframe <= CHAINGUN_SINGLE_SHOT_FRAME)
		shots = 1;
	else if (ent->client->ps.gunframe <= CHAINGUN_DOUBLE_SHOT_FRAME)
		shots = (ent->client->buttons & BUTTON_ATTACK) ? 2 : 1;
	else
		shots = 3;

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

	if (ent->client->pers.inventory[ent->client->pers.weapon->ammo] < shots) {
		shots = ent->client->pers.inventory[ent->client->pers.weapon->ammo];
	}

	if (!shots) {
		NoAmmoWeaponChange(ent, true);
		ent->client->ps.gunframe = CHAINGUN_SPINDOWN_FRAME;
		ent->client->weapon_sound = 0;
		if (!ent->client->pers.skills.cg_silent)
		{
			gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnd1a.wav"), 1, ATTN_IDLE, 0);
		}
		return;
	}

	if (is_quad) {
		damage *= damage_multiplier;
		kick *= damage_multiplier;
	}

	vec3_t kick_origin{};
	vec3_t kick_angles{};
	for (int i = 0; i < 3; i++) {
		kick_origin[i] = crandom() * KICK_RANDOM_SCALE;
		kick_angles[i] = crandom() * (KICK_BASE_ANGLE + (shots * KICK_PER_SHOT));
	}
	P_AddWeaponKick(ent, kick_origin, kick_angles);

	vec3_t start_pos;
    vec3_t fire_dir;
	P_ProjectSource(ent, ent->client->v_angle, GUN_OFFSET, start_pos, fire_dir, true);

	// Apply spread reduction if upgraded (divide spread by 2)
	int hspread = DEFAULT_BULLET_HSPREAD;
	int vspread = DEFAULT_BULLET_VSPREAD;
	if (ent->client->pers.skills.cg_spread > 0)
	{
		hspread /= 1.5;
		vspread /= 1.5;
	}

	G_LagCompensate(ent, start_pos, fire_dir);
	for (int i = 0; i < shots; i++) {
		fire_bullet(ent, start_pos, fire_dir, damage, kick, hspread, vspread, MOD_CHAINGUN);
	}
	G_UnLagCompensate();

	// Play weapon sound with power-up effects (unless silent mode)
	if (!ent->client->pers.skills.cg_silent)
	{
		Weapon_PowerupSound(ent);
	}

	// Send muzzle flash effect to clients (add MZ_SILENCED flag if silent mode)
	int flash_flags = (MZ_CHAINGUN1 + shots - 1) | is_silenced;
	if (ent->client->pers.skills.cg_silent)
	{
		flash_flags |= MZ_SILENCED;
	}
	gi.WriteByte(svc_muzzleflash);
	gi.WriteEntity(ent);
	gi.WriteByte(flash_flags);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	// Generate noise event for AI awareness (unless silent mode)
	if (!ent->client->pers.skills.cg_silent)
	{
		PlayerNoise(ent, start_pos, PNOISE_WEAPON);
	}
	
	G_RemoveAmmo(ent, shots);

	Fire_TracerBullet(ent, g_config.chaingun.tracer_damage, gtime_t::from_ms(g_config.chaingun.tracer_cooldown_ms));
}

void Weapon_Chaingun(edict_t* ent)
{
    // Standard chaingun animation frame parameters for Weapon_Repeating.
    // These are designed to be compatible with Chaingun_Fire's internal frame logic.
    // FRAME_ACTIVATE_LAST = 4  => FRAME_FIRE_FIRST = 5 (Chaingun_Fire starts its logic)
    // FRAME_FIRE_LAST = 31     => FRAME_IDLE_FIRST = 32 (Chaingun_Fire handles this as end of cycle/spindown)
    // FRAME_IDLE_LAST = 61     (Duration of the idle animation part)
    // FRAME_DEACTIVATE_LAST = 64 (Duration of the weapon lowering animation)
    constexpr int STD_CHAINGUN_ACTIVATE_LAST = 4;
    constexpr int STD_CHAINGUN_FIRE_LAST = 31;
    constexpr int STD_CHAINGUN_IDLE_LAST = 61;
    constexpr int STD_CHAINGUN_DEACTIVATE_LAST = 64;

    constexpr int pause_frames[] = { 38, 43, 51, 61, 0 }; // Standard chaingun idle pause frames

    Weapon_Repeating(ent,
        STD_CHAINGUN_ACTIVATE_LAST,
        STD_CHAINGUN_FIRE_LAST,
        STD_CHAINGUN_IDLE_LAST,
        STD_CHAINGUN_DEACTIVATE_LAST,
        pause_frames,
        Chaingun_Fire);
}
/*
======================================================================

SHOTGUN / SUPERSHOTGUN

======================================================================
*/

void weapon_shotgun_fire(edict_t* ent)
{
	int damage;
	int kick = g_config.shotgun.kick;

	// Check if using energy shells (global benefit or weapon-specific upgrade)
	bool use_energy = PlayerHasEnergyShells(ent) || (ent->client && ent->client->pers.skills.sg_energized);
	damage = !use_energy ? irandom(g_config.shotgun.damage_min, g_config.shotgun.damage_max) : irandom(g_config.shotgun.damage_energy_min, g_config.shotgun.damage_energy_max);

	// Apply damage upgrade (+0.2 per level, max 10 levels = +2.0 damage)
	if (ent->client && ent->client->pers.skills.sg_damage > 0)
	{
		damage += static_cast<int>(ent->client->pers.skills.sg_damage * 0.2f);
	}

	// Apply strike (kick) upgrade (+2 per level for noticeable feedback)
	if (ent->client && ent->client->pers.skills.sg_strike > 0)
	{
		kick += ent->client->pers.skills.sg_strike * 2;
	}

	vec3_t start, dir;
	// Paril: kill sideways angle on hitscan
	P_ProjectSource(ent, ent->client->v_angle, { 0, 0, -8 }, start, dir, true);

	P_AddWeaponKick(ent, ent->client->v_forward * -2, { -2.f, 0.f, 0.f });

	if (is_quad)
	{
		damage *= damage_multiplier;
		kick *= damage_multiplier;
	}

	// Calculate pellet count with fractional tracking (+0.5 per level)
	int base_pellets = G_IsDeathmatch() ? g_config.shotgun.pellet_count_deathmatch : g_config.shotgun.pellet_count_normal;
	int pellet_count = base_pellets;
	if (ent->client && ent->client->pers.skills.sg_pellets > 0)
	{
		pellet_count += static_cast<int>(ent->client->pers.skills.sg_pellets * 0.5f);
	}

	// Apply spread reduction (divide by 1.5f for tighter spread)
	int hspread = 500;
	int vspread = 500;
	if (ent->client && ent->client->pers.skills.sg_spread)
	{
		hspread = static_cast<int>(hspread / 1.5f);
		vspread = static_cast<int>(vspread / 1.5f);
	}

	G_LagCompensate(ent, start, dir);
	fire_shotgun(ent, start, dir, damage, kick, hspread, vspread, pellet_count, MOD_SHOTGUN);
	G_UnLagCompensate();

	// send muzzle flash (suppressed if silent mode enabled)
	bool silent = ent->client && ent->client->pers.skills.sg_silent;
	if (!silent)
	{
		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(MZ_SHOTGUN | is_silenced);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);

		PlayerNoise(ent, start, PNOISE_WEAPON);
	}

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
	int kick = g_config.supershotgun.kick;

	// Check if using energy shells (global benefit or weapon-specific upgrade)
	bool use_energy = PlayerHasEnergyShells(ent) || (ent->client && ent->client->pers.skills.ssg_energized);
	damage = !use_energy ? irandom(g_config.supershotgun.damage_min, g_config.supershotgun.damage_max) : irandom(g_config.supershotgun.damage_energy_min, g_config.supershotgun.damage_energy_max);

	// Apply damage upgrade (+0.4 per level, max 10 levels = +4.0 damage)
	if (ent->client && ent->client->pers.skills.ssg_damage > 0)
	{
		damage += static_cast<int>(ent->client->pers.skills.ssg_damage * 0.4f);
	}

	// Apply strike (kick) upgrade (+2 per level for noticeable feedback)
	if (ent->client && ent->client->pers.skills.ssg_strike > 0)
	{
		kick += ent->client->pers.skills.ssg_strike * 2;
	}

	if (is_quad)
	{
		damage *= damage_multiplier;
		kick *= damage_multiplier;
	}

	// Calculate pellet count with fractional tracking (+0.5 per level)
	int base_pellets = g_config.supershotgun.pellet_count;
	int pellet_count_per_barrel = base_pellets / 2;
	if (ent->client && ent->client->pers.skills.ssg_pellets > 0)
	{
		int additional_pellets = static_cast<int>(ent->client->pers.skills.ssg_pellets * 0.5f);
		pellet_count_per_barrel = (base_pellets + additional_pellets) / 2;
	}

	// Apply spread reduction (divide by 1.5f for tighter spread)
	int hspread = DEFAULT_SHOTGUN_HSPREAD;
	int vspread = DEFAULT_SHOTGUN_VSPREAD;
	if (ent->client && ent->client->pers.skills.ssg_spread)
	{
		hspread = static_cast<int>(hspread / 1.5f);
		vspread = static_cast<int>(vspread / 1.5f);
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
	fire_shotgun(ent, start, dir, damage, kick, hspread, vspread, pellet_count_per_barrel, MOD_SSHOTGUN);
	v[YAW] = ent->client->v_angle[YAW] + 5;
	P_ProjectSource(ent, v, { 0, 0, -8 }, start, dir, true);
	fire_shotgun(ent, start, dir, damage, kick, hspread, vspread, pellet_count_per_barrel, MOD_SSHOTGUN);
	G_UnLagCompensate();
	// DEFAULT_SSHOTGUN_COUNT /2.7
	P_AddWeaponKick(ent, ent->client->v_forward * -2, { -2.f, 0.f, 0.f });

	// send muzzle flash (suppressed if silent mode enabled)
	bool silent = ent->client && ent->client->pers.skills.ssg_silent;
	if (!silent)
	{
		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(MZ_SSHOTGUN | is_silenced);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);

		PlayerNoise(ent, start, PNOISE_WEAPON);
	}

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
		damage = g_config.railgun.damage_horde;
		kick = g_config.railgun.kick;
	}
	else
	{
		damage = g_config.railgun.damage;
		kick = g_config.railgun.kick;
	}

	// Apply railgun damage upgrade
	constexpr int RAILGUN_ADDON_DAMAGE = 8;
	if (ent->client && ent->client->pers.skills.rg_damage > 0)
	{
		damage += RAILGUN_ADDON_DAMAGE * ent->client->pers.skills.rg_damage;
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

	// send muzzle flash (unless silent mode is enabled)
	if (!ent->client || !ent->client->pers.skills.rg_silent)
	{
		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(MZ_RAILGUN | is_silenced);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);

		PlayerNoise(ent, start, PNOISE_WEAPON);
	}

	G_RemoveAmmo(ent);
}

void Weapon_Railgun(edict_t* ent)
{
	constexpr int pause_frames[] = { 56, 0 };
	constexpr int fire_frames[] = { 4, 0 };

	Weapon_Generic(ent, 3, 18, 56, 61, pause_frames, fire_frames, weapon_railgun_fire);
}

/*
======================================================================

20MM CANNON

======================================================================
*/

void weapon_20mm_fire(edict_t* ent)
{
	int damage = g_config.cannon20mm.damage;
	int kick = g_config.cannon20mm.kick;

	// Check if player is grounded or in water
	if (!ent->groundentity && !ent->waterlevel)
	{
		if (ent->client && !(ent->svflags & SVF_MONSTER))
			gi.LocClient_Print(ent, PRINT_HIGH, nullptr, "You must be on the ground or in water to fire the 20mm cannon.\n");
		return;
	}

	if (is_quad)
	{
		damage *= damage_multiplier;
		kick *= damage_multiplier;
	}

	vec3_t start, dir;
	P_ProjectSource(ent, ent->client->v_angle, { 0, 7, -8 }, start, dir, true);
	G_LagCompensate(ent, start, dir);
	fire_20mm(ent, start, dir, damage, kick, g_config.cannon20mm.range, MOD_CANNON);
	G_UnLagCompensate();

	// Apply recoil - push player backward
	vec3_t forward;
	AngleVectors(ent->client->v_angle, forward, nullptr, nullptr);
	ent->velocity -= forward * g_config.cannon20mm.recoil_force;

	// Big visual recoil - weapon kicks up high
	P_AddWeaponKick(ent, ent->client->v_forward * -3, { -4.f, 0.f, 0.f });

	// Play 20mm cannon sound
	gi.positioned_sound(ent->s.origin, ent, CHAN_WEAPON, gi.soundindex("world/lid.wav"), 1, ATTN_NORM, 0);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	G_RemoveAmmo(ent);
}

void Weapon_20mm(edict_t* ent)
{
	constexpr int pause_frames[] = { 56, 0 };
	constexpr int fire_frames[] = { 4, 0 };

	// Vortex frame values: VERY short fire cycle (ends at frame 4) for rapid fire
	Weapon_Generic(ent, 3, 4, 56, 61, pause_frames, fire_frames, weapon_20mm_fire);
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
	float const damage_radius = g_config.bfg.radius;

	if (G_IsDeathmatch())
		damage = g_config.bfg.damage;
	else
		damage = g_config.bfg.damage;

	// Apply BFG damage upgrade
	constexpr float BFG10K_ADDON_DAMAGE = 2.0f;
	if (ent->client && ent->client->pers.skills.bfg_damage > 0)
	{
		damage += static_cast<int>(BFG10K_ADDON_DAMAGE * ent->client->pers.skills.bfg_damage);
	}

	// Handle muzzle flash for standard BFG charge-up (unless silent mode)
	if (!PlayerHasBFGSlide(ent) && ent->client->ps.gunframe == 9)
	{
		if (!ent->client || !ent->client->pers.skills.bfg_silent)
		{
			gi.WriteByte(svc_muzzleflash);
			gi.WriteEntity(ent);
			gi.WriteByte(MZ_BFG | is_silenced);
			gi.multicast(ent->s.origin, MULTICAST_PVS, false);
			PlayerNoise(ent, ent->s.origin, PNOISE_WEAPON);
		}
		return;
	}

	// Check for required ammo
	int const required_ammo = PlayerHasBFGSlide(ent) ? 25 : 50;
	if (ent->client->pers.inventory[ent->client->pers.weapon->ammo] < required_ammo)
		return;

	if (is_quad)
		damage *= damage_multiplier;

	// Calculate BFG speed with upgrade
	constexpr int BFG10K_INITIAL_SPEED = 650;
	constexpr int BFG10K_ADDON_SPEED = 35;
	int bfg_speed = BFG10K_INITIAL_SPEED;
	if (ent->client && ent->client->pers.skills.bfg_range > 0)
	{
		bfg_speed += BFG10K_ADDON_SPEED * ent->client->pers.skills.bfg_range;
	}

	vec3_t start, dir;
	P_ProjectSource(ent, ent->client->v_angle, { 8, 8, -8 }, start, dir);

	// Fire BFG projectile with upgraded speed
	fire_bfg(ent, start, dir, damage, bfg_speed, damage_radius);

	// Apply weapon kick
	P_AddWeaponKick(ent, ent->client->v_forward * -2, { -20.f, 0, crandom() * 8 });
	ent->client->kick.total = DAMAGE_TIME();
	ent->client->kick.time = level.time + ent->client->kick.total;

	// Muzzle flash for the actual firing (unless silent mode)
	if (!ent->client || !ent->client->pers.skills.bfg_silent)
	{
		gi.WriteByte(svc_muzzleflash);
		gi.WriteEntity(ent);
		gi.WriteByte(MZ_BFG2 | is_silenced);
		gi.multicast(ent->s.origin, MULTICAST_PVS, false);

		PlayerNoise(ent, start, PNOISE_WEAPON);
	}

	// Remove the correct amount of ammo in a single, clear call
	G_RemoveAmmo(ent, required_ammo);

	// Advance gunframe
	if (PlayerHasBFGSlide(ent))
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