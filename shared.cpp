#include "shared.h"
#include "horde/g_horde.h"
#include <unordered_map>
#include <algorithm>  // For std::max

bool IsRemovableEntity(const edict_t* ent);
void RemoveEntity(edict_t* ent);

void turret2_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
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
		turret2_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
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

// Sobrecarga para edict_t*
std::string GetDisplayName(const edict_t* ent) {
	if (!ent) return "Unknown";

	std::string base_name = GetDisplayName(ent->classname);
	if (ent->monsterinfo.bonus_flags) {
		return GetTitleFromFlags(ent->monsterinfo.bonus_flags) + base_name;
	}
	return base_name;
}


void ApplyMonsterBonusFlags(edict_t* monster)
{
	monster->spawnflags.has(SPAWNFLAG_MONSTER_NO_DROP);

	if (monster->gib_health <= -900)
		monster->gib_health = -900;

	monster->gib_health *= 2.8f;

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
	monster->s.renderfx |= RF_IR_VISIBLE;
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


void TeleportEntity(edict_t* ent, edict_t* dest) {
	if (!ent || !ent->inuse || !dest || !dest->inuse)
		return;

	// Store original position for effect
	const vec3_t old_origin = ent->s.origin;

	// Teleport effect at source
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_TELEPORT_EFFECT);
	gi.WritePosition(old_origin);
	gi.multicast(old_origin, MULTICAST_PVS, false);

	// Move entity using vec3_t operations
	ent->s.origin = dest->s.origin;
	ent->s.origin.z += 10; // Slight elevation to prevent clipping

	// Reset velocities using vec3_t assignments
	ent->velocity = vec3_origin;

	if (ent->client) {
		ent->client->ps.pmove.pm_time = 160;
		ent->client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;
		ent->s.event = EV_PLAYER_TELEPORT;

		// Direct vec3_t angle assignments
		ent->client->ps.viewangles = dest->s.angles;
		ent->client->v_angle = dest->s.angles;
		ent->s.angles = dest->s.angles;

		// Convert to pmove origin format using vec3_t
		for (int i = 0; i < 3; i++) {
			ent->client->ps.pmove.origin[i] = static_cast<int16_t>(ent->s.origin[i] * 8.0f);
			float angle_diff = anglemod(dest->s.angles[i] - ent->client->resp.cmd_angles[i]);
			ent->client->ps.pmove.delta_angles[i] = angle_diff;
		}

		ent->client->ps.pmove.velocity = vec3_origin;
	}
	else {
		ent->s.angles = dest->s.angles;
	}

	gi.unlinkentity(ent);
	gi.linkentity(ent);

	// Prevent telefrag
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

bool EntitiesOverlap(edict_t* ent, const vec3_t& area_mins, const vec3_t& area_maxs) {
	// Calculate bounds using vec3_t operations
	const vec3_t ent_mins = ent->s.origin + ent->mins;
	const vec3_t ent_maxs = ent->s.origin + ent->maxs;

	// Use boxes_intersect from g_local
	return boxes_intersect(ent_mins, ent_maxs, area_mins, area_maxs);
}

// Arrays estáticos a nivel de archivo (fuera de las funciones)
namespace {
	constexpr int MAX_ENTITIES = 300;
	edict_t* g_stubborn_entities[MAX_ENTITIES];
	edict_t* g_entities_to_process[MAX_ENTITIES];
	edict_t* g_entities_to_remove[MAX_ENTITIES];
	edict_t* g_spawn_area_entities[MAX_ENTITIES];  // Para ClearSpawnArea
}

void ClearSpawnArea(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs)
{
	int entity_count = 0;

	// Calculate the absolute bounds of the area to check
	vec3_t area_mins = origin + mins;
	vec3_t area_maxs = origin + maxs;

	// Expand the area slightly to account for movement
	vec3_t expansion{ 26.0f, 26.0f, 26.0f };
	area_mins -= expansion;
	area_maxs += expansion;

	// Calculate search radius using vec3_t's length() method
	float search_radius = maxs.length() + 16.0f;

	// First pass: collect entities
	for (edict_t* ent = nullptr; (ent = findradius(ent, origin, search_radius)) != nullptr;) {
		if (!ent->inuse)
			continue;
		if (ent->svflags & SVF_MONSTER)
			continue;
		if (ent->solid == SOLID_NOT || ent->solid == SOLID_TRIGGER)
			continue;

		if (!EntitiesOverlap(ent, area_mins, area_maxs))
			continue;

		if (entity_count < MAX_ENTITIES) {
			g_spawn_area_entities[entity_count++] = ent;  // Usar el array global
		}
	}

	// Second pass: process entities
	for (int i = 0; i < entity_count; i++) {
		edict_t* ent = g_spawn_area_entities[i];  // Usar el array global

		if (ent->client) {
			// For players, teleport them to a safe spawn point
			edict_t* spawn_point = SelectSingleSpawnPoint(ent);
			if (spawn_point) {
				TeleportEntity(ent, spawn_point);
				gi.Com_PrintFmt("PRINT: Player {} teleported to spawn point to make room for boss.\n",
					ent->client->pers.netname);
			}
			else {
				gi.Com_PrintFmt("PRINT: WARNING: Could not find a spawn point for player {}.\n",
					ent->client->pers.netname);
			}
		}
		else {
			// For non-player entities, remove them
			RemoveEntity(ent);
			gi.Com_PrintFmt("PRINT: Entity {} removed from boss spawn area.\n",
				ent->classname ? ent->classname : "unknown");
		}
	}
}

void PushEntitiesAway(const vec3_t& center, int num_waves, int wave_interval_ms,
	float push_radius, float push_strength,
	float horizontal_push_strength, float vertical_push_strength)
{
	// Constantes optimizadas
	static constexpr float MIN_VELOCITY = 400.0f;
	static constexpr float MAX_FORCE = 4000.0f;
	static constexpr float BASE_FORCE = 500.0f;
	static constexpr int MAX_ATTEMPTS = 5;
	static constexpr float VERTICAL_BOOST_FIRST = 800.0f;
	static constexpr float VERTICAL_BOOST_SECOND = 600.0f;
	static constexpr float FORCE_MULTIPLIER = 1.25f;
	static constexpr float ATTEMPT_SCALE = 0.75f;
	static constexpr float MIN_DISTANCE_CHECK = 0.01f;

	// Arrays estáticos para entidades
	static edict_t* entities_to_process[MAX_ENTITIES];
	static edict_t* entities_to_remove[MAX_ENTITIES];
	size_t process_count = 0;
	size_t remove_count = 0;

	// Asegurar radio mínimo y calcular radio de búsqueda
	push_radius = std::max(push_radius, 1.0f);
	const float search_radius = push_radius * 1.5f;

	// Recolectar entidades con verificación de visibilidad
	for (edict_t* ent = nullptr; (ent = findradius(ent, center, search_radius)) != nullptr;) {
		if (!ent || !ent->inuse)
			continue;

		// Verificar PVS y línea de visión
		if (!gi.inPVS(center, ent->s.origin, false))
			continue;

		// Verificar línea de visión directa
		vec3_t check_point = ent->s.origin;
		check_point.z += (ent->maxs.z + ent->mins.z) * 0.5f;

		const trace_t tr = gi.traceline(center, check_point, nullptr, MASK_SOLID);
		if (tr.fraction < 1.0f && tr.ent != ent)
			continue;

		if (IsRemovableEntity(ent)) {
			if (remove_count < MAX_ENTITIES)
				entities_to_remove[remove_count++] = ent;
		}
		else if (ent->takedamage) {
			if (process_count < MAX_ENTITIES)
				entities_to_process[process_count++] = ent;
		}
	}

	// Procesar entidades removibles primero
	for (size_t i = 0; i < remove_count; i++) {
		if (entities_to_remove[i] && entities_to_remove[i]->inuse)
			RemoveEntity(entities_to_remove[i]);
	}

	// Procesar olas
	for (int wave = 0; wave < num_waves; wave++) {
		const float wave_progress = static_cast<float>(wave) / num_waves;
		const float size = std::max(push_radius * (1.0f - wave_progress * 0.5f), 0.030f);

		// Efecto visual de spawn
		SpawnGrow_Spawn(center, size, size * 0.3f);

		// Procesar cada entidad
		for (size_t entity_idx = 0; entity_idx < process_count; entity_idx++) {
			edict_t* entity = entities_to_process[entity_idx];

			if (!entity || !entity->inuse)
				continue;

			// Re-verificar PVS para esta entidad
			if (!gi.inPVS(center, entity->s.origin, false))
				continue;

			bool pushed = false;
			int attempts = 0;

			while (!pushed && attempts < MAX_ATTEMPTS) {
				// Verificar línea de visión nuevamente
				const trace_t vis_tr = gi.traceline(center,
					entity->s.origin, nullptr, MASK_SOLID);
				if (vis_tr.fraction < 1.0f && vis_tr.ent != entity) {
					attempts++;
					continue;
				}

				// Calcular dirección de empuje usando vec3_t
				vec3_t push_dir = entity->s.origin - center;
				const float distance_squared = push_dir.lengthSquared();

				// Normalizar dirección
				if (distance_squared > MIN_DISTANCE_CHECK) {
					push_dir = push_dir.normalized();
				}
				else {
					// Dirección aleatoria si está muy cerca
					push_dir = vec3_t{ crandom(), crandom(), 0.0f }.normalized();
				}

				const float distance = std::sqrt(distance_squared);
				const float distance_factor = std::max(0.3f, 1.0f - (distance / size));

				// Calcular fuerza de empuje
				float wave_push_strength = push_strength * distance_factor *
					std::sin(DEG2RAD(90.0f * distance_factor)) * FORCE_MULTIPLIER;

				// Ajustar por intentos
				const float attempt_multiplier = 1.0f + (attempts * ATTEMPT_SCALE);
				wave_push_strength = std::min(wave_push_strength * attempt_multiplier, MAX_FORCE);

				// Calcular nueva posición
				const vec3_t new_pos = entity->s.origin + (push_dir * (wave_push_strength / BASE_FORCE));

				// Verificar colisión
				const trace_t tr = gi.trace(entity->s.origin, entity->mins, entity->maxs,
					new_pos, entity, MASK_SOLID);

				if (!tr.allsolid && !tr.startsolid) {
					// Calcular velocidad final
					const float tr_scale = tr.fraction < 1.0f ? tr.fraction : 1.0f;
					vec3_t final_velocity = push_dir * (wave_push_strength * tr_scale);

					// Añadir componente horizontal
					const float horizontal_factor = std::sin(DEG2RAD(90.0f * distance_factor));
					final_velocity += push_dir * (horizontal_push_strength * horizontal_factor);

					// Añadir componente vertical
					final_velocity.z += vertical_push_strength *
						std::sin(DEG2RAD(90.0f * distance_factor));

					// Boost vertical para primeras olas
					if (wave <= 1) {
						final_velocity.z += (wave == 0) ? VERTICAL_BOOST_FIRST : VERTICAL_BOOST_SECOND;
					}

					// Asegurar velocidad mínima
					for (int axis = 0; axis < 3; axis++) {
						if (std::abs(final_velocity[axis]) < MIN_VELOCITY) {
							final_velocity[axis] = (final_velocity[axis] >= 0 ? MIN_VELOCITY : -MIN_VELOCITY);
						}
					}

					// Aplicar velocidad final
					final_velocity *= FORCE_MULTIPLIER;

					// Actualizar entidad
					entity->velocity = final_velocity;
					entity->groundentity = nullptr;

					// Actualizar cliente si es necesario
					if (entity->client) {
						entity->client->oldvelocity = final_velocity;
						entity->client->oldgroundentity = nullptr;
						entity->client->ps.pmove.velocity = final_velocity;
					}

					pushed = true;

					gi.Com_PrintFmt("PRINT: Wave {}: Entity {} pushed with velocity {}\n",
						wave + 1, entity->classname ? entity->classname : "unknown", final_velocity);
				}

				attempts++;
			}
		}
	}
}


[[nodiscard]] constexpr bool string_equals(const char* str1, const std::string_view& str2) noexcept {
	return str1 && str2.length() == strlen(str1) && !Q_strncasecmp(str1, str2.data(), str2.length());
}

// Define el mapa de nombres aquí
const std::unordered_map<std::string_view, std::string_view> name_replacements = {
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
		{ "monster_tank_spawner", "Spawner Tank" },
		{ "monster_runnertank", "BETA Runner Tank" },
		{ "monster_guncmdr_vanilla", "Gunner Commander" },
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
