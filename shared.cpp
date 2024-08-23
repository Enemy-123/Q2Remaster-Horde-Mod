#include "shared.h"
#include "horde/g_horde.h"
#include <unordered_map>
#include <algorithm>  // For std::max

#include "g_local.h"

bool IsRemovableEntity(edict_t* ent);
void RemoveEntity(edict_t* ent);

	void turret_die(edict_t * self, edict_t * inflictor, edict_t * attacker, int damage, const vec3_t & point, const mod_t & mod);
	void prox_die(edict_t * self, edict_t * inflictor, edict_t * attacker, int damage, const vec3_t & point, const mod_t & mod);
	void tesla_die(edict_t * self, edict_t * inflictor, edict_t * attacker, int damage, const vec3_t & point, const mod_t & mod);
	void trap_die(edict_t * self, edict_t * inflictor, edict_t * attacker, int damage, const vec3_t & point, const mod_t & mod);
	void laser_die(edict_t * self, edict_t * inflictor, edict_t * attacker, int damage, const vec3_t & point, const mod_t & mod);
	bool hasEntities = false;

	void RemovePlayerOwnedEntities(edict_t * player)
	{
		if (!player)
			return;

		for (unsigned int i = 0; i < globals.num_edicts; i++)
		{
			edict_t* ent = &g_edicts[i];
			if (!ent->inuse)
				continue;

			bool shouldRemove = (ent->owner == player ||
				(ent->owner && ent->owner->owner == player) ||
				ent->teammaster == player ||
				(ent->teammaster && ent->teammaster->teammaster == player));

			if (shouldRemove)
			{
				OnEntityDeath(ent);

				if (IsRemovableEntity(ent))
				{
					RemoveEntity(ent);
				}
			}
		}

		// Reset the player's laser counter
		if (player->client)
		{
			player->client->num_lasers = 0;
		}
	}

	bool IsRemovableEntity(edict_t * ent)
	{
		static const std::unordered_set<std::string> removableEntities = {
			"tesla_mine", "food_cube_trap", "prox_mine", "monster_sentrygun",
			"emitter", "laser"
		};

		return removableEntities.find(ent->classname) != removableEntities.end();
	}

	void RemoveEntity(edict_t * ent)
	{
		if (!strcmp(ent->classname, "monster_sentrygun") && ent->health > 0)
		{
			ent->health = -1;
			turret_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
		}
		else if (!strcmp(ent->classname, "tesla_mine"))
		{
			tesla_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
		}
		else if (!strcmp(ent->classname, "prox_mine"))
		{
			prox_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
		}
		else if (!strcmp(ent->classname, "food_cube_trap"))
		{
			trap_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
		}
		else if (!strcmp(ent->classname, "emitter") || !strcmp(ent->classname, "laser"))
		{
			laser_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
		}
		else
		{
			BecomeExplosion1(ent);
		}
	}


void UpdatePowerUpTimes(edict_t* monster) {

	if (monster->monsterinfo.quad_time <= level.time) {
		monster->monsterinfo.damage_modifier_applied = false;
	}

	if (monster->monsterinfo.double_time <= level.time)
	{
		monster->monsterinfo.damage_modifier_applied = false;
	}
}

float M_DamageModifier(edict_t* monster)
{
	if (monster->monsterinfo.damage_modifier_applied)
	{
		return 1.0f; // No additional modifier
	}

	float damageModifier = 1.0f;

	if (monster->monsterinfo.quad_time > level.time)
	{
		damageModifier *= 4.0f;
	}

	if (monster->monsterinfo.double_time > level.time)
	{
		if (monster->monsterinfo.quad_time <= level.time)
		{
			damageModifier *= 2.0f;
		}
	}

	if (damageModifier > 4.0f)
	{
		damageModifier = 4.0f;
	}
	else if (damageModifier > 2.0f && monster->monsterinfo.quad_time <= level.time)
	{
		damageModifier = 2.0f;
	}

	monster->monsterinfo.damage_modifier_applied = true;

	return damageModifier;
}

std::string GetTitleFromFlags(int bonus_flags)
{
	std::string title;
	if (bonus_flags & BF_CHAMPION) { title += "Champion "; }
	if (bonus_flags & BF_CORRUPTED) { title += "Corrupted "; }
	if (bonus_flags & BF_RAGEQUITTER) { title += "Ragequitter "; }
	if (bonus_flags & BF_BERSERKING) { title += "Berserking "; }
	if (bonus_flags & BF_POSSESSED) { title += "Possessed "; }
	if (bonus_flags & BF_STYGIAN) { title += "Stygian "; }
	return title;
}

std::string GetDisplayName(edict_t* ent)
{
	static const std::unordered_map<std::string, std::string> name_replacements = {
		{ "monster_soldier_light", "Light Soldier" },
		{ "monster_soldier_ss", "SS Soldier" },
		{ "monster_soldier", "Soldier" },
		{ "monster_soldier_hypergun", "Hyper Soldier" },
		{ "monster_soldier_lasergun", "Laser Soldier" },
		{ "monster_soldier_ripper", "Ripper Soldier" },
		{ "monster_infantry2", "Infantry" },
		{ "monster_infantry", "Enforcer" },
		{ "monster_flyer", "Flyer" },
		{ "monster_kamikaze", "Kamikaze" },
		{ "monster_hover2", "Blaster Icarus" },
		{ "monster_fixbot", "Fixbot" },
		{ "monster_gekk", "Gekk" },
		{ "monster_flipper", "Flipper" },
		{ "monster_gunner2", "Gunner" },
		{ "monster_gunner", "Heavy Gunner" },
		{ "monster_medic", "Medic" },
		{ "monster_brain", "Brain" },
		{ "monster_stalker", "Stalker" },
		{ "monster_parasite", "Parasite" },
		{ "monster_tank", "Tank" },
		{ "monster_tank2", "Vanilla Tank" },
		{ "monster_runnertank", "BETA Runner Tank" },
		{ "monster_guncmdr2", "Gunner Commander" },
		{ "monster_mutant", "Mutant" },
		{ "monster_redmutant", "Raged Mutant" },
		{ "monster_chick", "Iron Maiden" },
		{ "monster_chick_heat", "Iron Praetor" },
		{ "monster_berserk", "Berserker" },
		{ "monster_floater", "Technician" },
		{ "monster_hover", "Rocket Icarus" },
		{ "monster_daedalus", "Daedalus" },
		{ "monster_daedalus2", "Bombardier Hover" },
		{ "monster_medic_commander", "Medic Commander" },
		{ "monster_tank_commander", "Tank Commander" },
		{ "monster_spider", "Arachnid" },
		{ "monster_arachnid", "Arachnid" },
		{ "monster_guncmdr", "Grenadier Commander" },
		{ "monster_gladc", "Plasma Gladiator" },
		{ "monster_gladiator", "Gladiator" },
		{ "monster_shambler", "Shambler" },
		{ "monster_floater2", "DarkMatter Technician" },
		{ "monster_carrier2", "Mini Carrier" },
		{ "monster_carrier", "Carrier" },
		{ "monster_tank_64", "N64 Tank" },
		{ "monster_janitor", "Janitor" },
		{ "monster_janitor2", "Mini Guardian" },
		{ "monster_guardian", "Guardian" },
		{ "monster_makron", "Makron" },
		{ "monster_jorg", "Jorg" },
		{ "monster_gladb", "DarkMatter Gladiator" },
		{ "monster_boss2_64", "N64 Hornet" },
		{ "monster_boss2kl", "N64 Hornet" },
		{ "monster_boss2", "Hornet" },
		{ "monster_perrokl", "Infected Parasite" },
		{ "monster_guncmdrkl", "Gunner Grenadier" },
		{ "monster_shambler", "Shambler" },
		{ "monster_shamblerkl", "Shambler" },
		{ "monster_makronkl", "Makron" },
		{ "monster_widow1", "Widow Apprentice" },
		{ "monster_widow", "Widow Matriarch" },
		{ "monster_widow2", "Widow Creator" },
		{ "monster_supertank", "Super-Tank" },
		{ "monster_supertankkl", "Super-Tank" },
		{ "monster_boss5", "Super-Tank" },
		{ "monster_sentrygun", "Friendly Sentry-Gun" },
		{ "monster_turret", "TurretGun" },
		{ "monster_turretkl", "TurretGun" },
		{ "monster_gnorta", "Gnorta" },
		{ "monster_shocker", "Shocker" },
		{ "monster_arachnid2", "Arachnid" },
		{ "monster_gm_arachnid", "Guided-Missile Arachnid" },
		{ "misc_insane", "Insane Grunt" },
		{ "food_cube_trap", "Stroggonoff Maker\n" },
		{ "tesla_mine", " Tesla Mine\n" },
		{ "emitter", "Laser Emitter\n" }
	};

	const auto it = name_replacements.find(ent->classname);
	std::string display_name = (it != name_replacements.end()) ? it->second : ent->classname;

	std::string title = GetTitleFromFlags(ent->monsterinfo.bonus_flags);
	return title + display_name;
}
void ApplyMonsterBonusFlags(edict_t* monster)
{

	if (monster->spawnflags.has(SPAWNFLAG_IS_BOSS))
	{
		// Si es un jefe, llamamos a ApplyBossEffects
		float health_multiplier, power_armor_multiplier;
		const auto mapSize = GetMapSize(level.mapname);
		ApplyBossEffects(monster, mapSize.isSmallMap, mapSize.isMediumMap, mapSize.isBigMap, health_multiplier, power_armor_multiplier);
		return;
	}

	monster->initial_max_health = monster->health;

	if (monster->monsterinfo.bonus_flags & BF_CHAMPION)
	{
		monster->s.effects |= EF_ROCKET | EF_FIREBALL;
		monster->s.renderfx |= RF_SHELL_RED;
		monster->health *= 2.0f;
		monster->monsterinfo.power_armor_power *= 1.25f;
		monster->monsterinfo.double_time = std::max(level.time, monster->monsterinfo.double_time) + 175_sec;
	}
	if (monster->monsterinfo.bonus_flags & BF_CORRUPTED)
	{
		monster->s.effects |= EF_PLASMA | EF_TAGTRAIL;
		monster->health *= 1.5f;
		monster->monsterinfo.power_armor_power *= 1.4f;
	}
	if (monster->monsterinfo.bonus_flags & BF_RAGEQUITTER) {
		monster->s.effects |= EF_BLUEHYPERBLASTER;
		monster->s.renderfx |= RF_TRANSLUCENT;
		monster->monsterinfo.power_armor_power *= 4.0f;
		monster->monsterinfo.invincible_time = max(level.time, monster->monsterinfo.invincible_time) + 12_sec;
	}
	if (monster->monsterinfo.bonus_flags & BF_BERSERKING) {
		monster->s.effects |= EF_GIB | EF_FLAG2;
		monster->health *= 1.5f;
		monster->monsterinfo.power_armor_power *= 1.3f;
		monster->monsterinfo.quad_time = max(level.time, monster->monsterinfo.quad_time) + 175_sec;
		monster->monsterinfo.attack_state = AS_BLIND;
	}
	if (monster->monsterinfo.bonus_flags & BF_POSSESSED) {
		monster->s.effects |= EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE;
		monster->s.alpha = 0.5f;
		monster->health *= 1.7f;
		monster->monsterinfo.power_armor_power *= 1.7f;
		monster->monsterinfo.attack_state = AS_BLIND;
	}
	if (monster->monsterinfo.bonus_flags & BF_STYGIAN) {
		monster->s.effects |= EF_TRACKER | EF_FLAG1;
		monster->health *= 1.5f;
		monster->monsterinfo.power_armor_power *= 1.1f;
		monster->monsterinfo.attack_state = AS_BLIND;
	}

	monster->initial_max_health = monster->health;
}

void ApplyBossEffects(edict_t* boss, bool isSmallMap, bool isMediumMap, bool isBigMap, float& health_multiplier, float& power_armor_multiplier)
{
	if (!boss->spawnflags.has(SPAWNFLAG_IS_BOSS))
		return;

	health_multiplier = 1.0f;
	power_armor_multiplier = 1.0f;

	// Apply bonus flags effects
	if (boss->monsterinfo.bonus_flags & BF_CHAMPION) {
		boss->s.scale = 1.3f;
		boss->mins *= 1.3f;
		boss->maxs *= 1.3f;
		boss->s.effects |= EF_ROCKET | EF_FIREBALL;
		boss->s.renderfx |= RF_SHELL_RED;
		health_multiplier *= 1.5f;
		power_armor_multiplier *= 1.25f;
		boss->monsterinfo.double_time = std::max(level.time, boss->monsterinfo.double_time) + 175_sec;
	}
	if (boss->monsterinfo.bonus_flags & BF_CORRUPTED) {
		boss->s.scale = 1.5f;
		boss->mins *= 1.5f;
		boss->maxs *= 1.5f;
		boss->s.effects |= EF_PLASMA | EF_TAGTRAIL;
		health_multiplier *= 1.4f;
		power_armor_multiplier *= 1.4f;
	}
	if (boss->monsterinfo.bonus_flags & BF_RAGEQUITTER) {
		boss->s.effects |= EF_BLUEHYPERBLASTER;
		boss->s.renderfx |= RF_TRANSLUCENT;
		power_armor_multiplier *= 1.4f;
		boss->monsterinfo.invincible_time = max(level.time, boss->monsterinfo.invincible_time) + 12_sec;
	}
	if (boss->monsterinfo.bonus_flags & BF_BERSERKING) {
		boss->s.effects |= EF_GIB | EF_FLAG2;
		health_multiplier *= 1.5f;
		power_armor_multiplier *= 1.5f;
		boss->monsterinfo.quad_time = max(level.time, boss->monsterinfo.quad_time) + 175_sec;
		boss->monsterinfo.attack_state = AS_BLIND;
	}
	if (boss->monsterinfo.bonus_flags & BF_POSSESSED) {
		boss->s.effects |= EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE;
		boss->s.alpha = 0.5f;
		health_multiplier *= 1.4f;
		power_armor_multiplier *= 1.4f;
		boss->monsterinfo.attack_state = AS_BLIND;
	}
	if (boss->monsterinfo.bonus_flags & BF_STYGIAN) {
		boss->s.scale = 1.2f;
		boss->mins *= 1.2f;
		boss->maxs *= 1.2f;
		boss->s.effects |= EF_TRACKER | EF_FLAG1;
		health_multiplier *= 1.5f;
		power_armor_multiplier *= 1.1f;
		boss->monsterinfo.attack_state = AS_BLIND;
	}

	// Calculate minimum health and power armor based on wave number
	int health_min = 5000;
	int power_armor_min = 0;

	// Use the global current_wave_level
	if (current_wave_level >= 25 && current_wave_level <= 200)
	{
		health_min = 18000;
		power_armor_min = std::min(18550, 80000);
	}
	else if (current_wave_level >= 20 && current_wave_level <= 24)
	{
		health_min = 15000;
		power_armor_min = std::min(13000, 65000);
	}
	else if (current_wave_level >= 15 && current_wave_level <= 19)
	{
		health_min = 12000;
		power_armor_min = std::min(9500, 30000);
	}
	else if (current_wave_level >= 10 && current_wave_level <= 14)
	{
		health_min = 10000;
		power_armor_min = std::min(5475, 20000);
	}
	else if (current_wave_level >= 5 && current_wave_level <= 9)
	{
		health_min = 8000;
		power_armor_min = 3600;
	}
	else if (current_wave_level >= 1 && current_wave_level <= 4)
	{
		health_min = 5000;
		power_armor_min = 1500;
	}

	// Apply health multiplier and ensure it's not below the minimum
	boss->health = std::max(static_cast<int>(boss->health * health_multiplier), health_min);
	boss->max_health = boss->health;
	boss->initial_max_health = boss->health;

	// Apply power armor multiplier and ensure it's not below the minimum
	if (boss->monsterinfo.power_armor_power > 0)
	{
		boss->monsterinfo.power_armor_power = std::max(
			static_cast<int>(boss->monsterinfo.base_power_armor * power_armor_multiplier),
			power_armor_min
		);
	}

	// Apply map size adjustments
	if (isSmallMap)
	{
		boss->health = static_cast<int>(boss->health * 0.8f);
		if (boss->monsterinfo.power_armor_power > 0)
			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * 0.9f);
	}
	else if (isBigMap)
	{
		boss->health = static_cast<int>(boss->health * 1.2f);
		if (boss->monsterinfo.power_armor_power > 0)
			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * 1.2f);
	}

	// Ensure health and power armor don't fall below their minimum values after map size adjustments
	boss->health = std::max(boss->health, health_min);
	if (boss->monsterinfo.power_armor_power > 0)
		boss->monsterinfo.power_armor_power = std::max(boss->monsterinfo.power_armor_power, power_armor_min);

	boss->max_health = boss->health;
	boss->initial_max_health = boss->health;
}
void SetBossHealth(edict_t* boss, int base_health, int wave_number)
{
	int health_min = 5000;
	int power_armor = 0;

	if (wave_number >= 25 && wave_number <= 200)
	{
		health_min = 18000;
		power_armor = std::min(18550, 80000);
	}
	else if (wave_number >= 20 && wave_number <= 24)
	{
		health_min = 15000;
		power_armor = std::min(13000, 65000);
	}
	else if (wave_number >= 15 && wave_number <= 19)
	{
		health_min = 12000;
		power_armor = std::min(9500, 30000);
	}
	else if (wave_number >= 10 && wave_number <= 14)
	{
		health_min = 10000;
		power_armor = std::min(5475, 20000);
	}
	else if (wave_number >= 5 && wave_number <= 9)
	{
		health_min = 8000;
		power_armor = 3600;
	}
	else if (wave_number >= 1 && wave_number <= 4)
	{
		health_min = 5000;
		power_armor = 1500;
	}

	boss->health = std::max(base_health, health_min);

	if (st.was_key_specified("power_armor_type"))
	{
		boss->monsterinfo.base_power_armor = power_armor;
		boss->monsterinfo.power_armor_power = power_armor;
	}

	gi.Com_PrintFmt("Boss health set to: {}/{}\n", boss->health, boss->max_health);
}

//getting real name

std::string GetPlayerName(edict_t* player) {
	if (player && player->client) {
		char playerName[MAX_INFO_VALUE] = { 0 };
		gi.Info_ValueForKey(player->client->pers.userinfo, "name", playerName, sizeof(playerName));
		return std::string(playerName);
	}
	return "N/A";
}


//
////spawn ship
//edict_t* CreatePathCornerOnSkySurface(edict_t* reference) {
//	trace_t tr;
//	vec3_t start, end;
//
//	// Definir el punto de inicio (más arriba del jugador de referencia) y el punto final (más abajo)
//	start = reference->s.origin + vec3_t{ 0, 0, 2048 }; // Increase the height
//	end = reference->s.origin - vec3_t{ 0, 0, 4096 };   // Increase the depth
//
//	// Hacer un trazo para encontrar una superficie con SURF_SKY
//	tr = gi.trace(start, vec3_origin, vec3_origin, end, reference, MASK_SOLID);
//
//	// Mensajes de depuración
//	gi.Com_PrintFmt("Trace start: {} {} {}\n", start[0], start[1], start[2]);
//	gi.Com_PrintFmt("Trace end: {} {} {}\n", end[0], end[1], end[2]);
//	gi.Com_PrintFmt("Trace endpos: {} {} {}\n", tr.endpos[0], tr.endpos[1], tr.endpos[2]);
//
//	if (tr.surface && (tr.surface->flags & SURF_SKY)) {
//		vec3_t mins = { -4096, -4096, -4096 }; // Definir límites mínimos del mapa
//		vec3_t maxs = { 4096, 4096, 4096 };   // Definir límites máximos del mapa
//
//		// Verificar que la posición no esté fuera del mapa
//		for (int i = 0; i < 3; i++) {
//			if (tr.endpos[i] < mins[i]) tr.endpos[i] = mins[i];
//			if (tr.endpos[i] > maxs[i]) tr.endpos[i] = maxs[i];
//		}
//
//		gi.Com_PrintFmt("Creating path_corner at: {} {} {}\n", tr.endpos[0], tr.endpos[1], tr.endpos[2]);
//
//		edict_t* path_corner_sky = G_Spawn();
//		path_corner_sky->classname = "path_corner";
//		VectorCopy(tr.endpos, path_corner_sky->s.origin);
//		path_corner_sky->targetname = "path_corner_sky";
//		gi.linkentity(path_corner_sky);
//		return path_corner_sky;
//	}
//	else {
//		gi.Com_PrintFmt("No SKY surface found\n");
//	}
//
//	return nullptr;
//}
//
//
//
//edict_t* CreatePathCornerAbovePlayer(edict_t* player) {
//	edict_t* path_corner_player = G_Spawn();
//	path_corner_player->classname = "path_corner";
//	VectorCopy(player->s.origin, path_corner_player->s.origin);
//	path_corner_player->s.origin[2] += 512; // Elevarlo 512 unidades sobre el jugador
//	path_corner_player->targetname = "path_corner_player";
//	gi.linkentity(path_corner_player);
//	return path_corner_player;
//}
//
//
//
//

extern void SP_target_earthquake(edict_t* self);
constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_SILENT = 1_spawnflag;
constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_TOGGLE = 2_spawnflag;
[[maybe_unused]] constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_UNKNOWN_ROGUE = 4_spawnflag;
constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_ONE_SHOT = 8_spawnflag;


void ImprovedSpawnGrow(const vec3_t& position, float start_size, float end_size, edict_t* spawned_entity) {
	// Create the main SpawnGrow effect
	SpawnGrow_Spawn(position, start_size, end_size);

	// Create additional effects only for boss spawns
	if (spawned_entity && spawned_entity->spawnflags.has(SPAWNFLAG_IS_BOSS)) {
		// Add more dramatic effects for boss spawns
		for (int i = 0; i < 5; i++) {
			vec3_t offset;
			for (int j = 0; j < 3; j++) {
				offset[j] = position[j] + crandom() * 75;  // Random offset within 50 units
			}
			SpawnGrow_Spawn(offset, start_size * 0.5f, end_size * 0.5f);
		}

		// Add a ground shake effect
		auto earthquake = G_Spawn();
		if (earthquake) {
			earthquake->classname = "target_earthquake";
			earthquake->spawnflags = brandom() ? SPAWNFLAGS_EARTHQUAKE_TOGGLE : SPAWNFLAGS_EARTHQUAKE_ONE_SHOT;
			SP_target_earthquake(earthquake);
			earthquake->use(earthquake, spawned_entity, spawned_entity);
		}
	}
}
//constexpr spawnflags_t SPAWNFLAG_LAVABALL_NO_EXPLODE = 1_spawnflag;
void fire_touch(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self);
void PushEntitiesAway(const vec3_t& center, int num_waves, int wave_interval_ms, float push_radius, float push_strength, float horizontal_push_strength, float vertical_push_strength) {

	//gi.WriteByte(svc_temp_entity);
	//gi.WriteByte(TE_BOSSTPORT);
	//gi.WritePosition(center);
	//gi.multicast(center, MULTICAST_PHS, false);

	for (int wave = 0; wave < num_waves; wave++) {
		const float size = push_radius * (1.0f - static_cast<float>(wave) / num_waves);
		const float end_size = size * 0.1f;

		// Use ImprovedSpawnGrow and add fireballs for the first wave
		if (wave == 0) {
			// Create the main SpawnGrow effect
			SpawnGrow_Spawn(center, size, end_size);

			// Add more dramatic effects and fireballs for the first wave
			for (int i = 0; i < 5; i++) {
				vec3_t offset;
				for (int j = 0; j < 3; j++) {
					offset[j] = center[j] + crandom() * 75;  // Random offset within 75 units
				}
				SpawnGrow_Spawn(offset, size * 0.5f, end_size * 0.5f);

				// Spawn a fireball
				edict_t* fireball = G_Spawn();
				if (fireball) {
					fireball->s.effects = EF_FIREBALL;
					fireball->s.renderfx = RF_MINLIGHT;
					fireball->solid = SOLID_BBOX;
					fireball->movetype = MOVETYPE_TOSS;
					fireball->clipmask = MASK_SHOT;
					fireball->velocity[0] = crandom() * 200; // Doubled horizontal speed
					fireball->velocity[1] = crandom() * 200; // Doubled horizontal speed
					fireball->velocity[2] = (200 + (frandom() * 200)); // Kept the same
					fireball->avelocity = { crandom() * 180, crandom() * 180, crandom() * 180 }; // Halved rotation speed
					fireball->classname = "fireball";
					gi.setmodel(fireball, "models/objects/gibs/sm_meat/tris.md2");
					VectorCopy(offset, fireball->s.origin);
					fireball->nextthink = level.time + 15000_ms;
					fireball->think = G_FreeEdict;
					fireball->touch = fire_touch;
					//fireball->spawnflags = SPAWNFLAG_LAVABALL_NO_EXPLODE;
					gi.linkentity(fireball);
				}
			}
		}
		else {
			// Use regular SpawnGrow for subsequent waves
			SpawnGrow_Spawn(center, size, end_size);
		}

		// Find and push entities
		edict_t* ent = nullptr;
		while ((ent = findradius(ent, center, size)) != nullptr) {
			if (!ent->inuse || !ent->takedamage)
				continue;

			vec3_t push_dir{};
			VectorSubtract(ent->s.origin, center, push_dir);
			const float distance = VectorLength(push_dir);

			if (distance > 0.1f) {
				VectorNormalize(push_dir);
			}
			else {
				// If the entity is too close to the center, give it a random direction
				push_dir[0] = crandom();
				push_dir[1] = crandom();
				push_dir[2] = 0;
				VectorNormalize(push_dir);
			}

			// Calculate push strength with a sine wave for smoother effect
			float wave_push_strength = push_strength * (1.0f - distance / size);
			wave_push_strength *= sinf(DEG2RAD(90.0f * (1.0f - distance / size)));

			// Calculate new position
			vec3_t new_pos{};
			VectorMA(ent->s.origin, wave_push_strength / 700, push_dir, new_pos);

			// Trace to ensure we're not pushing through walls
			trace_t tr = gi.trace(ent->s.origin, ent->mins, ent->maxs, new_pos, ent, MASK_SOLID);

			vec3_t final_velocity;
			if (tr.fraction < 1.0) {
				// If we hit something, adjust the push
				VectorScale(push_dir, tr.fraction * wave_push_strength, final_velocity);
			}
			else {
				VectorScale(push_dir, wave_push_strength, final_velocity);
			}

			// Add strong horizontal component
			float horizontal_factor = sinf(DEG2RAD(90.0f * (1.0f - distance / size)));
			final_velocity[0] += push_dir[0] * horizontal_push_strength * horizontal_factor;
			final_velocity[1] += push_dir[1] * horizontal_push_strength * horizontal_factor;

			// Add vertical component
			final_velocity[2] += vertical_push_strength * sinf(DEG2RAD(90.0f * (1.0f - distance / size)));

			VectorCopy(final_velocity, ent->velocity);

			ent->groundentity = nullptr;

			if (ent->client) {
				VectorCopy(ent->velocity, ent->client->oldvelocity);
				ent->client->oldgroundentity = ent->groundentity;
			}

			gi.Com_PrintFmt("Wave {}: Entity {} pushed. New velocity: {}\n", wave + 1, ent->classname ? ent->classname : "unknown", ent->velocity);
		}

		// Wait before the next wave
		if (wave < num_waves - 1) {
			// Note: This delay won't work as is in the function. You might need to implement a different way to handle the delay between waves.
			// For now, we'll just print a message.
			gi.Com_PrintFmt("Waiting {} milliseconds before next wave\n", wave_interval_ms);
		}
	}
}


void Monster_MoveSpawn(edict_t* self)
{
	if (!self || self->health <= 0 || self->deadflag)
		return;

	constexpr int NUM_MONSTERS_MIN = 2;
	constexpr int NUM_MONSTERS_MAX = 3;
	constexpr float SPAWN_RADIUS_MIN = 100.0f;
	constexpr float SPAWN_RADIUS_MAX = 150.0f;
	constexpr int MAX_SPAWN_ATTEMPTS = 10;
	constexpr float SPAWN_HEIGHT_OFFSET = 8.0f;

	const int num_monsters = NUM_MONSTERS_MIN + (rand() % (NUM_MONSTERS_MAX - NUM_MONSTERS_MIN + 1));

	for (int i = 0; i < num_monsters; i++)
	{
		vec3_t spawn_origin;
		const	vec3_t mins = { -16, -16, -24 };
		const	vec3_t maxs = { 16, 16, 32 };

		bool found_spot = false;
		float spawn_angle = 0;

		for (int attempts = 0; attempts < MAX_SPAWN_ATTEMPTS; attempts++)
		{
			VectorCopy(self->s.origin, spawn_origin);
			spawn_angle = frandom() * 2 * PI;
			float radius = SPAWN_RADIUS_MIN + frandom() * (SPAWN_RADIUS_MAX - SPAWN_RADIUS_MIN);

			spawn_origin[0] += cos(spawn_angle) * radius;
			spawn_origin[1] += sin(spawn_angle) * radius;
			spawn_origin[2] += SPAWN_HEIGHT_OFFSET;

			if (CheckSpawnPoint(spawn_origin, mins, maxs))
			{
				found_spot = true;
				break;
			}
		}

		if (!found_spot)
			continue;

		vec3_t spawn_angles = self->s.angles;
		spawn_angles[YAW] = spawn_angle * (180 / PI);

		edict_t* monster = CreateGroundMonster(spawn_origin, spawn_angles, mins, maxs, g_horde->integer ? G_HordePickMonster(self) : brandom() ? "monster_gunner" : "monster_chick", 64);
		if (!monster)
			continue;

		monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
		monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
		monster->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
		monster->monsterinfo.last_sentrygun_target_time = 0_sec;

		const vec3_t spawngrow_pos = monster->s.origin;
		const float magnitude = VectorLength(spawngrow_pos);
		if (magnitude > 0) {
			const	float start_size = magnitude * 0.055f;
			const	float end_size = magnitude * 0.005f;
			SpawnGrow_Spawn(spawngrow_pos, start_size, end_size);
		}

		monster->owner = self;
	}
}

bool string_equals(const char* str1, const std::string_view& str2) {
	return str1 && str2.length() == strlen(str1) &&
		!Q_strncasecmp(str1, str2.data(), str2.length());
}