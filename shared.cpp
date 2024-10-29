#include "shared.h"
#include "horde/g_horde.h"
#include <unordered_map>
#include <algorithm>  // For std::max

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

bool IsRemovableEntity(const edict_t* ent)
{
	static const std::unordered_set<std::string> removableEntities = {
		"tesla_mine", "food_cube_trap", "prox_mine", "monster_sentrygun",
		"emitter", "laser"
	};

	return removableEntities.find(ent->classname) != removableEntities.end();
}

void RemoveEntity(edict_t* ent)
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
	title.reserve(100); // Reservar espacio anticipadamente

	static const std::pair<int, std::string_view> flagTitles[] = {
		{BF_CHAMPION, "Champion "},
		{BF_CORRUPTED, "Corrupted "},
		{BF_RAGEQUITTER, "Ragequitter "},
		{BF_BERSERKING, "Berserking "},
		{BF_POSSESSED, "Possessed "},
		{BF_STYGIAN, "Stygian "}
	};

	for (const auto& [flag, name] : flagTitles)
		if (bonus_flags & flag)
			title += name;

	return title;
}

std::string GetDisplayName(const edict_t* ent)
{
	if (!ent) {
		return "Unknown";
	}

	static const std::unordered_map<std::string, std::string> name_replacements = {
		{ "monster_soldier_light", "Blaster Guard" },
		{ "monster_soldier_ss", "SS Guard" },
		{ "monster_soldier", "SG Guard" },
		{ "monster_soldier_hypergun", "Hyper Guard" },
		{ "monster_soldier_lasergun", "Laser Guard" },
		{ "monster_soldier_ripper", "Ripper Guard" },
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
		{ "monster_tank_vanilla", "Spawner Tank" },
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
	std::string display_name = (it != name_replacements.end()) ?
		std::string(it->second) : ent->classname;

	return GetTitleFromFlags(ent->monsterinfo.bonus_flags) + display_name;
}
void ApplyMonsterBonusFlags(edict_t* monster)
{
	if (monster->gib_health <= -900)
		monster->gib_health = -900;

	monster->gib_health *= 4.4f;

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
		monster->s.alpha = 0.6f;
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
static constexpr void CalculateBossMinimums(int wave_number, int& health_min, int& power_armor_min) noexcept
{
	if (wave_number >= 25) {
		health_min = 18000;
		power_armor_min = std::min(18550, 20000);
	}
	else if (wave_number >= 20) {
		health_min = std::min(15000, 16500);
		power_armor_min = std::min(13000, 16000);
	}
	else if (wave_number >= 15) {
		health_min = std::min(12000, 15500);
		power_armor_min = std::min(9500, 12000);
	}
	else if (wave_number >= 10) {
		health_min = std::min(10000, 12500);
		power_armor_min = std::min(5475, 8000);
	}
	else if (wave_number >= 5) {
		health_min = std::min(10000, 12500);
		power_armor_min = std::min(5475, 8000);
	}
	else {
		health_min = 5000;
		power_armor_min = 1500;
	}
}

void ApplyBossEffects(edict_t* boss)
{
	// Verificar si es un jefe y si ya se han aplicado los efectos
	if (!boss->spawnflags.has(SPAWNFLAG_IS_BOSS) || boss->effects_applied)
		return;

	// Obtener la categoría de tamaño del jefe
	BossSizeCategory sizeCategory = boss->bossSizeCategory;

	// Generar un flag aleatorio
	const int32_t random_flag = 1 << (rand() % 6);
	boss->monsterinfo.bonus_flags = random_flag;

	float health_multiplier = 1.0f;
	float power_armor_multiplier = 1.0f;

	// Función de utilidad para escalar el jefe
	auto ScaleEntity = [&](float scale_factor) {
		boss->s.scale *= scale_factor;
		boss->mins *= scale_factor;
		boss->maxs *= scale_factor;
		boss->mass *= scale_factor;

		// Ajustar la posición para alinear con el suelo
		float height_offset = -(boss->mins[2]);
		boss->s.origin[2] += height_offset;
		gi.linkentity(boss);
		};

	// Aplicar efectos de bonus flags
	if (boss->monsterinfo.bonus_flags & BF_CHAMPION) {
		// Aplicar escalado basado en la categoría de tamaño
		switch (sizeCategory) {
		case BossSizeCategory::Small:
			ScaleEntity(1.1f);
			health_multiplier *= 1.5f;
			power_armor_multiplier *= 1.25f;
			break;
		case BossSizeCategory::Medium:
			ScaleEntity(1.25f);
			health_multiplier *= 1.5f;
			power_armor_multiplier *= 1.25f;
			break;
		case BossSizeCategory::Large:
			ScaleEntity(1.5f);
			health_multiplier *= 1.5f;
			power_armor_multiplier *= 1.25f;
			break;
		}

		// Aplicar efectos visuales adicionales
		boss->s.effects |= EF_ROCKET | EF_FIREBALL;
		boss->s.renderfx |= RF_SHELL_RED;
		boss->monsterinfo.double_time = std::max(level.time, boss->monsterinfo.double_time) + 475_sec;
	}

	if (boss->monsterinfo.bonus_flags & BF_CORRUPTED) {
		// Aplicar escalado basado en la categoría de tamaño
		switch (sizeCategory) {
		case BossSizeCategory::Small:
			ScaleEntity(1.1f);
			health_multiplier *= 1.4f;
			power_armor_multiplier *= 1.4f;
			break;
		case BossSizeCategory::Medium:
			ScaleEntity(1.3f);
			health_multiplier *= 1.2f;
			power_armor_multiplier *= 1.4f;
			break;
		case BossSizeCategory::Large:
			ScaleEntity(1.4f);
			health_multiplier *= 1.4f;
			power_armor_multiplier *= 1.4f;
			break;
		}

		// Aplicar efectos visuales adicionales
		boss->s.effects |= EF_PLASMA | EF_TAGTRAIL;
	}

	if (boss->monsterinfo.bonus_flags & BF_RAGEQUITTER) {
		boss->s.effects |= EF_BLUEHYPERBLASTER;
		boss->s.alpha = 0.6f;
		power_armor_multiplier *= 1.4f;
		boss->monsterinfo.invincible_time = std::max(level.time, boss->monsterinfo.invincible_time) + 12_sec;
	}

	if (boss->monsterinfo.bonus_flags & BF_BERSERKING) {
		boss->s.effects |= EF_GIB | EF_FLAG2;
		health_multiplier *= 1.5f;
		power_armor_multiplier *= 1.5f;
		boss->monsterinfo.quad_time = std::max(level.time, boss->monsterinfo.quad_time) + 475_sec;
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
		// Aplicar escalado basado en la categoría de tamaño
		switch (sizeCategory) {
		case BossSizeCategory::Small:
			ScaleEntity(1.1f);
			health_multiplier *= 1.5f;
			power_armor_multiplier *= 1.1f;
			break;
		case BossSizeCategory::Medium:
			ScaleEntity(1.2f);
			health_multiplier *= 1.5f;
			power_armor_multiplier *= 1.1f;
			break;
		case BossSizeCategory::Large:
			ScaleEntity(1.4f);
			health_multiplier *= 1.5f;
			power_armor_multiplier *= 1.1f;
			break;
		}

		// Aplicar efectos visuales adicionales
		boss->s.effects |= EF_TRACKER | EF_FLAG1;
		boss->monsterinfo.attack_state = AS_BLIND;
	}

	// Calcular valores mínimos basados en el número de ola
	int health_min, power_armor_min;
	CalculateBossMinimums(current_wave_level, health_min, power_armor_min);

	// Aplicar multiplicadores de salud y armadura
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

	// Aplicar escalado de salud y armadura basado en la categoría de tamaño
	switch (sizeCategory) {
	case BossSizeCategory::Small:
		boss->health = static_cast<int>(boss->health * 0.8f);
		if (boss->monsterinfo.power_armor_power > 0)
			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * 0.9f);
		break;
	case BossSizeCategory::Large:
		boss->health = static_cast<int>(boss->health * 1.2f);
		if (boss->monsterinfo.power_armor_power > 0)
			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * 1.2f);
		break;
	case BossSizeCategory::Medium:
		// Opcional: Puedes aplicar un escalado diferente o ninguno para categorías medias
		break;
	}

	// Asegurar que la salud y la armadura no caigan por debajo de los valores mínimos después de los ajustes
	boss->health = std::max(boss->health, health_min);
	if (boss->monsterinfo.power_armor_power > 0)
		boss->monsterinfo.power_armor_power = std::max(boss->monsterinfo.power_armor_power, power_armor_min);

	boss->max_health = boss->health;
	boss->initial_max_health = boss->health;

	// Marcar que los efectos ya fueron aplicados para evitar escalados acumulativos
	boss->effects_applied = true;

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

void ImprovedSpawnGrow(const vec3_t& position, float start_size, float end_size, edict_t* spawned_entity)
{
	// Constantes para mejor legibilidad y mantenimiento
	constexpr int   NUM_SECONDARY_EFFECTS = 9;
	constexpr float RANDOM_OFFSET_RANGE = 255.0f;
	constexpr float EFFECT_SCALE = 0.55f;
	constexpr float EARTHQUAKE_SPEED = 500.0f;
	constexpr int   EARTHQUAKE_DURATION = 7;

	// Crear el efecto principal de spawn
	SpawnGrow_Spawn(position, start_size, end_size);

	// Si no es una entidad jefe, terminamos aquí
	if (!spawned_entity || !spawned_entity->spawnflags.has(SPAWNFLAG_IS_BOSS)) {
		return;
	}

	// Efectos adicionales para spawn de jefes
	for (int i = 0; i < NUM_SECONDARY_EFFECTS; i++)
	{
		vec3_t offset;

		// Calcular posición aleatoria para cada efecto secundario
		for (int j = 0; j < 3; j++) {
			offset[j] = position[j] + crandom() * RANDOM_OFFSET_RANGE;
		}

		// Crear efecto secundario escalado
		SpawnGrow_Spawn(offset,
			start_size * EFFECT_SCALE,
			end_size * EFFECT_SCALE);
	}

	// Crear y configurar el efecto de terremoto
	edict_t* earthquake = G_Spawn();
	if (earthquake)
	{
		// Configurar parámetros del terremoto
		earthquake->classname = "target_earthquake";
		earthquake->spawnflags = brandom()
			? SPAWNFLAGS_EARTHQUAKE_SILENT
			: SPAWNFLAGS_EARTHQUAKE_ONE_SHOT;
		earthquake->speed = EARTHQUAKE_SPEED;
		earthquake->count = EARTHQUAKE_DURATION;

		// Inicializar y activar el terremoto
		SP_target_earthquake(earthquake);
		earthquake->use(earthquake, spawned_entity, spawned_entity);
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

void PushEntitiesAway(const vec3_t& center, int num_waves, int wave_interval_ms,
	float push_radius, float push_strength,
	float horizontal_push_strength, float vertical_push_strength)
{
	gi.Com_PrintFmt("PRINT: Starting PushEntitiesAway at position: {}\n", center);

	constexpr int MAX_ATTEMPTS = 5;
	std::vector<edict_t*> stubborn_entities;
	std::vector<edict_t*> entities_to_process;
	std::vector<edict_t*> entities_to_remove;

	// Pre-reserve vectors to avoid reallocations
	stubborn_entities.reserve(MAX_EDICTS);
	entities_to_process.reserve(MAX_EDICTS);
	entities_to_remove.reserve(MAX_EDICTS);

	// Collect entities in radius
	for (edict_t* ent = nullptr; (ent = findradius(ent, center, push_radius)) != nullptr;) {
		if (!ent || !ent->inuse)
			continue;

		if (IsRemovableEntity(ent))
			entities_to_remove.push_back(ent);
		else if (ent->takedamage && ent->s.origin)
			entities_to_process.push_back(ent);
	}

	// Handle removable entities first
	for (auto* remove_ent : entities_to_remove) {
		if (remove_ent && remove_ent->inuse) {
			RemoveEntity(remove_ent);
			gi.Com_PrintFmt("PRINT: Removable entity {} eliminated.\n",
				remove_ent->classname ? remove_ent->classname : "unknown");
		}
	}
	entities_to_remove.clear();

	// Process waves
	for (int wave = 0; wave < num_waves; wave++) {
		const float wave_progress = static_cast<float>(wave) / num_waves;
		const float size = std::max(push_radius * (1.0f - wave_progress), 0.030f);
		SpawnGrow_Spawn(center, size, size * 0.3f);

		// Process normal entities
		for (auto it = entities_to_process.begin(); it != entities_to_process.end();) {
			auto* entity = *it;

			if (!entity || !entity->inuse) {
				it = entities_to_process.erase(it);
				continue;
			}

			if (IsRemovableEntity(entity)) {
				RemoveEntity(entity);
				gi.Com_PrintFmt("PRINT: Entity {} became removable and was eliminated.\n",
					entity->classname ? entity->classname : "unknown");
				it = entities_to_process.erase(it);
				continue;
			}

			bool pushed = false;
			for (int attempt = 0; attempt < MAX_ATTEMPTS && !pushed; attempt++) {
				// Calculate push direction
				vec3_t push_dir = entity->s.origin - center;
				const float distance = push_dir.length();

				if (distance > 0.1f) {
					push_dir.normalize();
				}
				else {
					push_dir = vec3_t{ crandom(), crandom(), 0 }.normalized();
				}

				// Calculate push strength
				const float distance_factor = 1.0f - distance / size;
				float wave_push_strength = push_strength * distance_factor;
				wave_push_strength *= sinf(DEG2RAD(90.0f * distance_factor));
				wave_push_strength = std::min(wave_push_strength * (1.0f + attempt * 0.5f), 1000.0f);

				// Calculate new position
				const vec3_t new_pos = entity->s.origin + (push_dir * (wave_push_strength / 700));
				const trace_t tr = gi.trace(entity->s.origin, entity->mins, entity->maxs,
					new_pos, entity, MASK_SOLID);

				if (!tr.allsolid && !tr.startsolid) {
					// Calculate final velocity
					const float tr_scale = tr.fraction < 1.0f ? tr.fraction : 1.0f;
					vec3_t final_velocity = push_dir * (wave_push_strength * tr_scale);

					// Add horizontal and vertical components
					const float horizontal_factor = sinf(DEG2RAD(90.0f * distance_factor));
					final_velocity += push_dir * (horizontal_push_strength * horizontal_factor);
					final_velocity.z += vertical_push_strength * sinf(DEG2RAD(90.0f * distance_factor));

					// Apply velocity
					entity->velocity = final_velocity;
					entity->groundentity = nullptr;

					if (entity->client) {
						entity->client->oldvelocity = entity->velocity;
						entity->client->oldgroundentity = entity->groundentity;
					}

					pushed = true;
					gi.Com_PrintFmt("PRINT: Wave {}: Entity {} pushed. Velocity: {}\n",
						wave + 1, entity->classname ? entity->classname : "unknown", entity->velocity);
				}
			}

			if (!pushed) {
				stubborn_entities.push_back(entity);
				gi.Com_PrintFmt("PRINT: Entity {} at {} could not be moved after {} attempts.\n",
					entity->classname ? entity->classname : "unknown", entity->s.origin, MAX_ATTEMPTS);
			}

			++it;
		}

		if (wave < num_waves - 1) {
			gi.Com_PrintFmt("PRINT: Waiting {} milliseconds before next wave\n", wave_interval_ms);
		}
	}

	// Handle stubborn entities
	for (auto* stubborn_ent : stubborn_entities) {
		if (!stubborn_ent || !stubborn_ent->inuse)
			continue;

		if (stubborn_ent->client) {
			if (edict_t* spawn_point = SelectSingleSpawnPoint(stubborn_ent)) {
				TeleportEntity(stubborn_ent, spawn_point);
				gi.Com_PrintFmt("PRINT: Player {} teleported to spawn point.\n",
					stubborn_ent->client->pers.netname);
			}
			else {
				gi.Com_PrintFmt("PRINT: WARNING: Could not find spawn point for player {}.\n",
					stubborn_ent->client->pers.netname);
			}
		}
		else if (IsRemovableEntity(stubborn_ent)) {
			RemoveEntity(stubborn_ent);
			gi.Com_PrintFmt("PRINT: Stubborn removable entity {} eliminated.\n",
				stubborn_ent->classname ? stubborn_ent->classname : "unknown");
		}
		else {
			RemoveEntity(stubborn_ent);
			gi.Com_PrintFmt("PRINT: Stubborn non-player entity {} removed.\n",
				stubborn_ent->classname ? stubborn_ent->classname : "unknown");
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
	vec3_t ent_mins = ent->s.origin + ent->mins;
	vec3_t ent_maxs = ent->s.origin + ent->maxs;

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
	area_mins = origin + mins;
	area_maxs = origin + maxs;
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

// Define the vector functions if they are not already defined
// Copy a vector
inline void InlineVectorCopy(const vec3_t& src, vec3_t& dest) {
	dest[0] = src[0];
	dest[1] = src[1];
	dest[2] = src[2];
}

// Returns the length of a vector
float VectorLength(const vec3_t v) {
	return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// Normalizes a vector
void VectorNormalize(vec3_t v) {
	float length = VectorLength(v);
	if (length) {
		float ilength = 1.0f / length;
		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}
}
void VectorClear(vec3_t v) {
	v[0] = 0;
	v[1] = 0;
	v[2] = 0;
}

#include <cmath>

#ifdef __SSE__
#include <xmmintrin.h>  // SSE
#endif
#ifdef __SSE2__
#include <emmintrin.h>  // SSE2
#endif

// Versión estándar optimizada sin SIMD
inline float VectorDistance(const vec3_t& v1, const vec3_t& v2) {
	const float dx = v1[0] - v2[0];
	const float dy = v1[1] - v2[1];
	const float dz = v1[2] - v2[2];
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}