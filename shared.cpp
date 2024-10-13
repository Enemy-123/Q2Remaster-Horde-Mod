#include "shared.h"
#include "horde/g_horde.h"
#include <unordered_map>
#include <algorithm>  // For std::max

#include "g_local.h"

bool IsRemovableEntity(const edict_t* ent);
void RemoveEntity(edict_t* ent);

void turret_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
void prox_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
void tesla_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
void trap_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
void laser_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
bool hasEntities = false;

void RemovePlayerOwnedEntities(edict_t* player)
{
	if (!player)
		return;

	for (unsigned int i = 0; i < globals.num_edicts; i++)
	{
		edict_t* ent = &g_edicts[i];
		if (!ent->inuse || !ent->owner)
			continue;

		bool shouldRemove = (ent->owner == player ||
			ent->owner->owner == player ||
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
bool IsRemovableEntity(const edict_t* ent)
{
	static const std::unordered_set<std::string_view> removableEntities = {
		"tesla_mine", "food_cube_trap", "prox_mine", "monster_sentrygun",
		"emitter", "laser"
	};

	return ent && removableEntities.find(ent->classname) != removableEntities.end();
}

void RemoveEntity(edict_t* ent)
{
	static const std::unordered_map<std::string_view, void(*)(edict_t*, edict_t*, edict_t*, int, const vec3_t&, const mod_t&)> deathFunctions = {
		{"monster_sentrygun", turret_die},
		{"tesla_mine", tesla_die},
		{"prox_mine", prox_die},
		{"food_cube_trap", trap_die},
		{"emitter", laser_die},
		{"laser", laser_die}
	};

	if (ent && ent->inuse)
	{
		auto it = deathFunctions.find(ent->classname);
		if (it != deathFunctions.end())
		{
			if (!strcmp(ent->classname, "monster_sentrygun") && ent->health > 0)
			{
				ent->health = -1;
			}
			it->second(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
		}
		else
		{
			BecomeExplosion1(ent);
		}
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
		{ "monster_infantry_vanilla", "Infantry" },
		{ "monster_infantry", "Enforcer" },
		{ "monster_flyer", "Flyer" },
		{ "monster_kamikaze", "Kamikaze Flyer" },
		{ "monster_hover_vanilla", "Blaster Icarus" },
		{ "monster_fixbot", "Fixbot" },
		{ "monster_gekk", "Gekk" },
		{ "monster_flipper", "Flipper" },
		{ "monster_gunner_vanilla", "Gunner" },
		{ "monster_gunner", "Heavy Gunner" },
		{ "monster_medic", "Medic" },
		{ "monster_brain", "Brain" },
		{ "monster_stalker", "Stalker" },
		{ "monster_parasite", "Parasite" },
		{ "monster_tank", "Tank" },
		{ "monster_tank_vanilla", "Vanilla Tank" },
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
		{ "monster_daedalus_bomber", "Bombardier Hover" },
		{ "monster_medic_commander", "Medic Commander" },
		{ "monster_tank_commander", "Tank Commander" },
		{ "monster_spider", "Arachnid" },
		{ "monster_arachnid", "Arachnid" },
		{ "monster_psxarachnid", "Arachnid" },
		{ "monster_guncmdr", "Grenadier Commander" },
		{ "monster_gladc", "Plasma Gladiator" },
		{ "monster_gladiator", "Gladiator" },
		{ "monster_shambler", "Shambler" },
		{ "monster_floater_tracker", "DarkMatter Technician" },
		{ "monster_carrier_mini", "Mini Carrier" },
		{ "monster_carrier", "Carrier" },
		{ "monster_tank_64", "N64 Tank" },
		{ "monster_janitor", "Janitor" },
		{ "monster_janitor2", "Mini Guardian" },
		{ "monster_guardian", "Guardian" },
		{ "monster_psxguardian", "Enhanced Guardian" },
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
		return;

	if (monster->monsterinfo.bonus_flags & BF_CHAMPION)
	{
		monster->s.effects |= EF_ROCKET | EF_FIREBALL;
		monster->s.renderfx |= RF_SHELL_RED;
		monster->health *= 2.0f;
		monster->monsterinfo.power_armor_power *= 1.25f;
		monster->monsterinfo.double_time = std::max(level.time, monster->monsterinfo.double_time) + 475_sec;
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
		monster->monsterinfo.invincible_time = max(level.time, monster->monsterinfo.invincible_time) + 15_sec;
	}
	if (monster->monsterinfo.bonus_flags & BF_BERSERKING) {
		monster->s.effects |= EF_GIB | EF_FLAG2;
		monster->health *= 1.5f;
		monster->monsterinfo.power_armor_power *= 1.3f;
		monster->monsterinfo.quad_time = max(level.time, monster->monsterinfo.quad_time) + 475_sec;
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

// Función auxiliar para calcular los valores mínimos de salud y armadura
static void CalculateBossMinimums(int wave_number, int& health_min, int& power_armor_min) noexcept
{
	if (wave_number >= 25 && wave_number <= 200)
	{
		health_min = 18000;
		power_armor_min = std::min(18550, 80000);
	}
	else if (wave_number >= 20 && wave_number <= 24)
	{
		health_min = 15000;
		power_armor_min = std::min(13000, 65000);
	}
	else if (wave_number >= 15 && wave_number <= 19)
	{
		health_min = 12000;
		power_armor_min = std::min(9500, 30000);
	}
	else if (wave_number >= 10 && wave_number <= 14)
	{
		health_min = 10000;
		power_armor_min = std::min(5475, 20000);
	}
	else if (wave_number >= 5 && wave_number <= 9)
	{
		health_min = 8000;
		power_armor_min = 3600;
	}
	else
	{
		health_min = 5000;
		power_armor_min = 1500;
	}
}
void ApplyBossEffects(edict_t* boss)
{

	if (!boss->spawnflags.has(SPAWNFLAG_IS_BOSS))
		return;

	const auto mapSize = GetMapSize(level.mapname);

	const int32_t random_flag = 1 << (rand() % 6);
	boss->monsterinfo.bonus_flags = random_flag;

	float health_multiplier = 1.0f;
	float power_armor_multiplier = 1.0f;

	// Aplicar efectos de bonus flags
	if (boss->monsterinfo.bonus_flags & BF_CHAMPION) {
		if (!(mapSize.isSmallMap)) {
			boss->s.scale *= 1.3f;
			boss->mins *= 1.3f;
			boss->maxs *= 1.3f;
		}
		boss->s.effects |= EF_ROCKET | EF_FIREBALL;
		boss->s.renderfx |= RF_SHELL_RED;
		health_multiplier *= 1.5f;
		power_armor_multiplier *= 1.25f;
		boss->monsterinfo.double_time = std::max(level.time, boss->monsterinfo.double_time) + 475_sec;
	}
	if (boss->monsterinfo.bonus_flags & BF_CORRUPTED) {
		if (!(mapSize.isSmallMap)) {
			boss->s.scale *= 1.5f;
			boss->mins *= 1.5f;
			boss->maxs *= 1.5f;
		}
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
		boss->monsterinfo.quad_time = max(level.time, boss->monsterinfo.quad_time) + 475_sec;
		boss->monsterinfo.attack_state = AS_BLIND;
	}
	if (boss->monsterinfo.bonus_flags & BF_POSSESSED) {
		boss->s.effects |= EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE;
		boss->s.alpha = 0.6f;
		health_multiplier *= 1.4f;
		power_armor_multiplier *= 1.4f;
		boss->monsterinfo.attack_state = AS_BLIND;
	}
	if (boss->monsterinfo.bonus_flags & BF_STYGIAN) {

		if (!(mapSize.isSmallMap)) {
			boss->s.scale *= 1.2f;
			boss->mins *= 1.2f;
			boss->maxs *= 1.2f;
		}
		boss->s.effects |= EF_TRACKER | EF_FLAG1;
		health_multiplier *= 1.5f;
		power_armor_multiplier *= 1.1f;
		boss->monsterinfo.attack_state = AS_BLIND;
	}

	// Calcular valores mínimos basados en el número de ola
	int health_min, power_armor_min;
	CalculateBossMinimums(current_wave_level, health_min, power_armor_min);

	// Aplicar multiplicadores y asegurar que no estén por debajo del mínimo
	boss->health = std::max(static_cast<int>(boss->health * health_multiplier), health_min);
	boss->max_health = boss->health;
	boss->initial_max_health = boss->health;

	if (boss->monsterinfo.power_armor_power > 0)
	{
		boss->monsterinfo.power_armor_power = std::max(
			static_cast<int>(boss->monsterinfo.base_power_armor * power_armor_multiplier),
			power_armor_min
		);
	}

	// Aplicar ajustes de tamaño de mapa
	if (mapSize.isSmallMap)
	{
		boss->health = static_cast<int>(boss->health * 0.8f);
		if (boss->monsterinfo.power_armor_power > 0)
			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * 0.9f);
	}
	else if (mapSize.isBigMap)
	{
		boss->health = static_cast<int>(boss->health * 1.2f);
		if (boss->monsterinfo.power_armor_power > 0)
			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * 1.2f);
	}

	// Asegurar que la salud y la armadura no caigan por debajo de los valores mínimos después de los ajustes
	boss->health = std::max(boss->health, health_min);
	if (boss->monsterinfo.power_armor_power > 0)
		boss->monsterinfo.power_armor_power = std::max(boss->monsterinfo.power_armor_power, power_armor_min);

	boss->max_health = boss->health;
	boss->initial_max_health = boss->health;

	gi.Com_PrintFmt("PRINT: Boss health set to: {}/{}\n", boss->health, boss->max_health);
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
				offset[j] = position[j] + crandom() * 125;  // Random offset within 50 units
			}
			SpawnGrow_Spawn(offset, start_size * 0.5f, end_size * 0.5f);
		}

		// Crear el efecto de terremoto
		auto earthquake = G_Spawn();
		earthquake->classname = "target_earthquake";
		earthquake->spawnflags = brandom() ? SPAWNFLAGS_EARTHQUAKE_SILENT : SPAWNFLAGS_EARTHQUAKE_ONE_SHOT; // Usar flag de un solo uso para activarlo una vez
		earthquake->speed = 500; // Severidad del terremoto
		earthquake->count = 4; // Duración del terremoto en segundos
		SP_target_earthquake(earthquake);
		earthquake->use(earthquake, spawned_entity, spawned_entity); // Activar el terremoto
	}
}

inline float AngleNormalize360(float angle)
{
	angle = fmodf(angle, 360.0f);
	if (angle < 0.0f)
		angle += 360.0f;
	return angle;
}


void TeleportEntity(edict_t* ent, edict_t* dest)
{
	if (!ent || !ent->inuse || !dest || !dest->inuse)
		return;

	// Teleport effect at source
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_TELEPORT_EFFECT);
	gi.WritePosition(ent->s.origin);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);

	// Move entity
	VectorCopy(dest->s.origin, ent->s.origin);
	ent->s.origin[2] += 10; // Slightly above the ground

	// Reset velocity
	VectorClear(ent->velocity);

	// Handle client-specific updates
	if (ent->client)
	{
		// Hold time to prevent immediate movement
		ent->client->ps.pmove.pm_time = 160; // Hold time (in milliseconds)
		ent->client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;

		// Teleport event for visual effect
		ent->s.event = EV_PLAYER_TELEPORT;

		// Set view angles
		VectorCopy(dest->s.angles, ent->s.angles);
		VectorCopy(dest->s.angles, ent->client->ps.viewangles);
		VectorCopy(dest->s.angles, ent->client->v_angle);

		// Update delta_angles to prevent view snapping
		for (int i = 0; i < 3; i++)
		{
			float angle_diff = AngleNormalize360(dest->s.angles[i]) - ent->client->resp.cmd_angles[i];
			ent->client->ps.pmove.delta_angles[i] = angle_diff;
		}

		// Update client-side position
		for (int i = 0; i < 3; i++)
		{
			ent->client->ps.pmove.origin[i] = static_cast<int16_t>(ent->s.origin[i] * 8.0f);
		}

		// Clear pmove velocity
		VectorClear(ent->client->ps.pmove.velocity);
	}
	else
	{
		// For non-player entities, set angles
		VectorCopy(dest->s.angles, ent->s.angles);
	}

	// Unlink and relink entity to update position
	gi.unlinkentity(ent);
	gi.linkentity(ent);

	// Kill anything at the destination to prevent telefragging
	KillBox(ent, false, MOD_TELEFRAG, true);

	// Teleport effect at destination
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_TELEPORT_EFFECT);
	gi.WritePosition(ent->s.origin);
	gi.multicast(ent->s.origin, MULTICAST_PVS, false);
}


//constexpr spawnflags_t SPAWNFLAG_LAVABALL_NO_EXPLODE = 1_spawnflag;
void fire_touch(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self);
edict_t* SelectSingleSpawnPoint(edict_t* ent);

void PushEntitiesAway(const vec3_t& center, int num_waves, int wave_interval_ms, float push_radius, float push_strength, float horizontal_push_strength, float vertical_push_strength) {
	gi.Com_PrintFmt("PRINT: Starting PushEntitiesAway at position: {}\n", center);

	const int max_attempts = 5; // Maximum number of attempts to push entities
	std::vector<edict_t*> stubborn_entities; // Entities that couldn't be moved after all attempts
	std::vector<edict_t*> entities_to_remove; // Entities to remove after iteration

	for (int wave = 0; wave < num_waves; wave++) {
		const float size = std::max(push_radius * (1.0f - static_cast<float>(wave) / num_waves), 0.030f);
		const float end_size = size * 0.3f;

		// Use regular SpawnGrow for all waves
		SpawnGrow_Spawn(center, size, end_size);

		// Find and collect entities
		std::vector<edict_t*> entities_in_radius;
		edict_t* ent = nullptr;

		while ((ent = findradius(ent, center, size)) != nullptr) {
			if (!ent || !ent->inuse || !ent->takedamage || !ent->s.origin)
				continue;

			entities_in_radius.push_back(ent);
		}

		// Process entities
		for (auto* entity : entities_in_radius) {
			if (!entity || !entity->inuse)
				continue;

			// Check if the entity is a special case that should be removed
			if (IsRemovableEntity(entity)) {
				entities_to_remove.push_back(entity);
				continue;
			}

			// Attempt to push the entity multiple times
			bool pushed = false;
			for (int attempt = 0; attempt < max_attempts; attempt++) {
				vec3_t push_dir{};
				VectorSubtract(entity->s.origin, center, push_dir);
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
				wave_push_strength = std::min(wave_push_strength, 1000.0f);  // Limit maximum push strength

				// Increase push strength for subsequent attempts
				wave_push_strength *= (1.0f + attempt * 0.5f);

				// Calculate new position
				vec3_t new_pos{};
				VectorMA(entity->s.origin, wave_push_strength / 700, push_dir, new_pos);

				// Trace to ensure we're not pushing through walls
				const trace_t tr = gi.trace(entity->s.origin, entity->mins, entity->maxs, new_pos, entity, MASK_SOLID);

				if (!tr.allsolid && !tr.startsolid) {
					// Code to push entities
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

					VectorCopy(final_velocity, entity->velocity);

					entity->groundentity = nullptr;

					if (entity->client) {
						VectorCopy(entity->velocity, entity->client->oldvelocity);
						entity->client->oldgroundentity = entity->groundentity;
					}

					gi.Com_PrintFmt("PRINT: Wave {}: Entity {} pushed. New velocity: {}\n",
						wave + 1, entity->classname ? entity->classname : "unknown", entity->velocity);

					pushed = true;
					break;
				}
			}

			if (!pushed) {
				// The entity couldn't be moved after multiple attempts
				stubborn_entities.push_back(entity);
				gi.Com_PrintFmt("PRINT: Entity {} at {} could not be moved after {} attempts.\n",
					entity->classname ? entity->classname : "unknown", entity->s.origin, max_attempts);
			}
		}

		// Wait for the specified interval before the next wave
		if (wave < num_waves - 1) {
			gi.Com_PrintFmt("PRINT: Waiting {} milliseconds before next wave\n", wave_interval_ms);
			// Implement your delay mechanism here
		}
	}

	// Remove entities after iteration
	for (auto* ent : entities_to_remove) {
		if (ent && ent->inuse) {
			RemoveEntity(ent);
			gi.Com_PrintFmt("PRINT: Entity {} removed.\n", ent->classname ? ent->classname : "unknown");
		}
	}

	// Handle stubborn entities
	for (auto* stubborn_ent : stubborn_entities) {
		if (!stubborn_ent || !stubborn_ent->inuse)
			continue;

		if (stubborn_ent->client) {
			// For players, teleport to a safe spawn point
			edict_t* spawn_point = SelectSingleSpawnPoint(stubborn_ent);
			if (spawn_point) {
				TeleportEntity(stubborn_ent, spawn_point);

				gi.Com_PrintFmt("PRINT: Player {} teleported to spawn point.\n", stubborn_ent->client->pers.netname);
			}
			else {
				gi.Com_PrintFmt("PRINT: WARNING: Could not find a safe spawn point for player {}.\n", stubborn_ent->client->pers.netname);
			}
		}
		else {
			// For non-player entities, remove them
			if (stubborn_ent && stubborn_ent->inuse) {
				RemoveEntity(stubborn_ent);
				gi.Com_PrintFmt("PRINT: Non-player entity {} removed.\n", stubborn_ent->classname ? stubborn_ent->classname : "unknown");
			}
		}
	}

	gi.Com_PrintFmt("PRINT: PushEntitiesAway completed\n");
}

bool string_equals(const char* str1, const std::string_view& str2) {
	return str1 && str2.length() == strlen(str1) &&
		!Q_strncasecmp(str1, str2.data(), str2.length());
}

bool EntitiesOverlap(edict_t* ent, const vec3_t& area_mins, const vec3_t& area_maxs)
{
	vec3_t ent_mins, ent_maxs;

	VectorAdd(ent->s.origin, ent->mins, ent_mins);
	VectorAdd(ent->s.origin, ent->maxs, ent_maxs);

	for (int i = 0; i < 3; i++)
	{
		if (ent_maxs[i] < area_mins[i] || ent_mins[i] > area_maxs[i])
			return false;
	}
	return true;
}


void ClearSpawnArea(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs)
{
	edict_t* ent = nullptr;
	vec3_t area_mins, area_maxs;

	// Calculate the absolute bounds of the area to check
	VectorAdd(origin, mins, area_mins);
	VectorAdd(origin, maxs, area_maxs);

	// Expand the area slightly to account for movement
	for (int i = 0; i < 3; i++)
	{
		area_mins[i] -= 26.0f;
		area_maxs[i] += 26.0f;
	}

	// Find entities within the area
	while ((ent = findradius(ent, origin, VectorLength(maxs) + 16.0f)) != nullptr)
	{
		if (!ent->inuse)
			continue;

		if (ent->svflags & SVF_MONSTER)
			continue;

		if (ent->solid == SOLID_NOT || ent->solid == SOLID_TRIGGER)
			continue;

		// Check if the entity is actually overlapping with the area
		if (!EntitiesOverlap(ent, area_mins, area_maxs))
			continue;

		if (ent->client)
		{
			// For players, teleport them to a safe spawn point
			edict_t* spawn_point = SelectSingleSpawnPoint(ent);
			if (spawn_point)
			{
				TeleportEntity(ent, spawn_point);
				gi.Com_PrintFmt("PRINT: Player {} teleported to spawn point to make room for boss.\n", ent->client->pers.netname);
			}
			else
			{
				gi.Com_PrintFmt("PRINT: WARNING: Could not find a spawn point for player {}.\n", ent->client->pers.netname);
			}
		}
		else
		{
			// For non-player entities, remove them
			RemoveEntity(ent);
			gi.Com_PrintFmt("PRINT: Entity {} removed from boss spawn area.\n", ent->classname ? ent->classname : "unknown");
		}
	}
}
