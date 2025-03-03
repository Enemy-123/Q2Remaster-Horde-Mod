#include "shared.h"
#include "horde/g_horde.h"
#include <unordered_map>
#include <algorithm>  // For std::max
#include <span>

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

	const std::span<edict_t> edicts(g_edicts, static_cast<size_t>(globals.num_edicts));

	for (size_t i = 0; i < edicts.size(); i++)
	{
		edict_t* ent = &edicts[i];
		if (!ent->inuse)
			continue;

		const bool shouldRemove = (ent->owner == player ||
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

	if (player->client)
	{
		player->client->num_lasers = 0;
	}
}

bool IsRemovableEntity(const edict_t* ent)
{
	if (!ent || !ent->classname)
		return false;

	const char* classname = ent->classname;
	return !strcmp(classname, "tesla_mine") ||
		!strcmp(classname, "food_cube_trap") ||
		!strcmp(classname, "prox_mine") ||
		!strcmp(classname, "monster_sentrygun") ||
		!strcmp(classname, "emitter") ||
		!strcmp(classname, "laser");
}

void RemoveEntity(edict_t* ent)
{
	if (!ent || !ent->inuse || !ent->classname)
		return;

	const char* classname = ent->classname;

	if (!strcmp(classname, "monster_sentrygun") && ent->health > 0) {
		ent->health = -1;
		turret2_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
	}
	else if (!strcmp(classname, "tesla_mine")) {
		tesla_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
	}
	else if (!strcmp(classname, "prox_mine")) {
		prox_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
	}
	else if (!strcmp(classname, "food_cube_trap")) {
		trap_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
	}
	else if (!strcmp(classname, "emitter") || !strcmp(classname, "laser")) {
		laser_die(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
	}
	else {
		BecomeExplosion1(ent);
	}
}

void UpdatePowerUpTimes(edict_t* monster) {
	if (!monster)
		return;

	const bool quad_expired = monster->monsterinfo.quad_time <= level.time;
	const bool double_expired = monster->monsterinfo.double_time <= level.time;

	if (quad_expired && double_expired) {
		monster->monsterinfo.damage_modifier_applied = false;
	}
}

constexpr float M_DamageModifier(edict_t* monster) noexcept {
	if (!monster)
		return 1.0f;

	if (monster->monsterinfo.damage_modifier_applied)
		return 1.0f;

	monster->monsterinfo.damage_modifier_applied = true;

	// Check quad first (higher priority)
	if (monster->monsterinfo.quad_time > level.time)
		return 4.0f;

	if (monster->monsterinfo.double_time > level.time)
		return 2.0f;

	return 1.0f;
}

[[nodiscard]] inline std::string GetTitleFromFlags(int bonus_flags) {
    if (bonus_flags == 0)
        return "";
        
    std::string title;
    title.reserve(64); // More reasonable reservation size
    
    // Direct flag checks for a small set of flags is likely faster than iterating
    if (bonus_flags & BF_CHAMPION)   title += "Champion ";
    if (bonus_flags & BF_CORRUPTED)  title += "Corrupted ";
    if (bonus_flags & BF_RAGEQUITTER) title += "Ragequitter ";
    if (bonus_flags & BF_BERSERKING) title += "Berserking ";
    if (bonus_flags & BF_POSSESSED)  title += "Possessed ";
    if (bonus_flags & BF_STYGIAN)    title += "Stygian ";
    if (bonus_flags & BF_FRIENDLY)   title += "Friendly ";
    
    return title;
}

// Sobrecarga para edict_t*
[[nodiscard]] inline std::string GetDisplayName(const edict_t* ent) {
	if (!ent) return "Unknown";

	std::string base_name = GetDisplayName(ent->classname);
	if (ent->monsterinfo.bonus_flags) {
		return GetTitleFromFlags(ent->monsterinfo.bonus_flags) + base_name;
	}
	return base_name;
}


void ApplyMonsterBonusFlags(edict_t* monster)
{
	if (monster->monsterinfo.IS_BOSS)
		return;

	if (monster->monsterinfo.issummoned) {

		monster->monsterinfo.bonus_flags |= BF_FRIENDLY;

		FindMTarget(monster);
	//	if (monster->svflags & SVF_MONSTER) // this needs &= ~ to work properly, 
	//		monster->svflags & ~SVF_MONSTER; // but got the desired effect on Monster_UpdateState so bots see these as friends! ( in case of summoned monster)

		monster->svflags |= SVF_PLAYER;		
		monster->monsterinfo.team = CTF_TEAM1;
		monster->s.renderfx &= ~RF_DOT_SHADOW;

		// Configurar equipo
		if (monster->owner->client->resp.ctf_team == CTF_TEAM1)
			monster->monsterinfo.team = CTF_TEAM1;
		else if (monster->owner->client->resp.ctf_team == CTF_TEAM2)
			monster->monsterinfo.team = CTF_TEAM2;

		// Establecer equipo basado en CTF
		if (monster->owner->client->resp.ctf_team == CTF_TEAM1) {
			monster->team = TEAM1;
		}
		else if (monster->owner->client->resp.ctf_team == CTF_TEAM2) {
			monster->team = TEAM2;
		}
		else {
			monster->team = "neutral";
		}
		gi.linkentity(monster);

	}

	monster->spawnflags.has(SPAWNFLAG_MONSTER_NO_DROP);

	monster->gib_health *= 2.8f;

	if (monster->gib_health <= -200)
		monster->gib_health = -200;

	if (monster->monsterinfo.bonus_flags & BF_CHAMPION)
	{
		monster->s.effects |= EF_ROCKET | EF_FIREBALL;
		monster->s.renderfx |= RF_SHELL_RED;
		monster->health *= 2.0f;
		monster->monsterinfo.power_armor_power *= 1.25f;
		monster->monsterinfo.armor_power *= 1.25f;
		monster->monsterinfo.double_time = std::max(level.time, monster->monsterinfo.double_time) + 475_sec;
	}
	if (monster->monsterinfo.bonus_flags & BF_CORRUPTED)
	{
		monster->s.effects |= EF_PLASMA | EF_TAGTRAIL;
		monster->health *= 1.5f;
		monster->monsterinfo.power_armor_power *= 1.4f;
		monster->monsterinfo.armor_power *= 1.4f;
	}
	if (monster->monsterinfo.bonus_flags & BF_RAGEQUITTER) {
		monster->s.effects |= EF_BLUEHYPERBLASTER;
		monster->s.alpha = 0.6f;
		monster->monsterinfo.power_armor_power *= 4.0f;
		monster->monsterinfo.armor_power *= 4.0f;
		monster->monsterinfo.invincible_time = max(level.time, monster->monsterinfo.invincible_time) + 7_sec;
	}
	if (monster->monsterinfo.bonus_flags & BF_BERSERKING) {
		monster->s.effects |= EF_GIB | EF_FLAG2;
		monster->health *= 1.5f;
		monster->monsterinfo.power_armor_power *= 1.3f;
		monster->monsterinfo.armor_power *= 1.3f;
		monster->monsterinfo.quad_time = max(level.time, monster->monsterinfo.quad_time) + 475_sec;
		monster->monsterinfo.attack_state = AS_BLIND;
	}
	if (monster->monsterinfo.bonus_flags & BF_POSSESSED) {
		monster->s.effects |= EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE;
		monster->s.alpha = 0.5f;
		monster->health *= 1.7f;
		monster->monsterinfo.power_armor_power *= 1.7f;
		monster->monsterinfo.armor_power *= 1.7f;
		monster->monsterinfo.attack_state = AS_BLIND;
	}
	if (monster->monsterinfo.bonus_flags & BF_STYGIAN) {
		monster->s.effects |= EF_TRACKER | EF_FLAG1;
		monster->health *= 1.5f;
		monster->monsterinfo.power_armor_power *= 1.1f;
		monster->monsterinfo.armor_power *= 1.1f;
		monster->monsterinfo.attack_state = AS_BLIND;
	}

	monster->max_health = monster->health;
	monster->s.renderfx |= RF_IR_VISIBLE;
}

// Función auxiliar para calcular los valores mínimos de salud y armadura
static constexpr void CalculateBossMinimums(int wave_number, int& health_min, int& power_armor_min) noexcept
{
	// Definimos los límites absolutos
	constexpr int HEALTH_MAX_LIMIT = 16500;
	constexpr int POWER_ARMOR_MAX_LIMIT = 15000;

	if (wave_number >= 25) {
		health_min = HEALTH_MAX_LIMIT;  // Ya estamos al límite máximo
		power_armor_min = std::min(13550, POWER_ARMOR_MAX_LIMIT);
	}
	else if (wave_number >= 20) {
		health_min = std::min(13000, HEALTH_MAX_LIMIT);
		power_armor_min = std::min(9000, 12000);
	}
	else if (wave_number >= 15) {
		health_min = std::min(10000, 13500);
		power_armor_min = std::min(4500, 10000);
	}
	else if (wave_number >= 10) {
		health_min = std::min(7000, 9500);
		power_armor_min = std::min(5475, 8000);
	}
	else if (wave_number >= 5) {
		health_min = std::min(2500, 10000);  // Invertidos los números para que tenga más sentido
		power_armor_min = std::min(5475, 8000);
	}
	else {
		// Valores base para las primeras oleadas
		health_min = 5000;
		power_armor_min = 1500;
	}

	// Aseguramos que los valores nunca excedan los límites absolutos
	health_min = std::min(health_min, HEALTH_MAX_LIMIT);
	power_armor_min = std::min(power_armor_min, POWER_ARMOR_MAX_LIMIT);
}

constexpr float REGULAR_ARMOR_FACTOR = 0.75f;  // 75% del power armor mínimo
// Función auxiliar para manejar la armadura del boss
void ConfigureBossArmor(edict_t* self) {
	if (!self || !self->inuse || !self->monsterinfo.IS_BOSS)
		return;

	// Calcular valores mínimos basados en el número de ola
	int health_min, power_armor_min;
	CalculateBossMinimums(current_wave_level, health_min, power_armor_min);

	// Manejar Power Armor (Shield/Screen)
	if (self->monsterinfo.power_armor_power > 0) {
		self->monsterinfo.power_armor_power = std::max(
			static_cast<int>(self->monsterinfo.power_armor_power),
			power_armor_min
		);
	}

	// Manejar Armor regular
	// Verificar si tiene armor sin depender del tipo
	if (self->monsterinfo.armor_power > 0) {
		const int min_regular_armor = static_cast<int>(power_armor_min * REGULAR_ARMOR_FACTOR);

		self->monsterinfo.armor_power = std::max(
			static_cast<int>(self->monsterinfo.armor_power),
			min_regular_armor
		);
	}
}
void ApplyBossEffects(edict_t* boss)
{
	// Verificar si es un jefe y si ya se han aplicado los efectos
	if (!boss->monsterinfo.IS_BOSS || boss->effects_applied)
		return;

	// Obtener la categoría de tamaño del jefe
	const BossSizeCategory sizeCategory = boss->bossSizeCategory;

	// Generar un flag aleatorio
	const int32_t random_flag = 1 << (rand() % 6);
	boss->monsterinfo.bonus_flags = random_flag;

	float health_multiplier = 1.0f;
	float power_armor_multiplier = 1.0f;

	// Función de utilidad para escalar el jefe
	auto ScaleEntity = [&](float scale_factor) {
		boss->s.scale *= scale_factor;
		//boss->mins *= scale_factor;
		//boss->maxs *= scale_factor;
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
			health_multiplier *= 1.13f;
			power_armor_multiplier *= 1.1f;
			break;
		case BossSizeCategory::Medium:
			ScaleEntity(1.25f);
			health_multiplier *= 1.24f;
			power_armor_multiplier *= 1.08f;
			break;
		case BossSizeCategory::Large:
			ScaleEntity(1.5f);
			health_multiplier *= 1.35f;
			power_armor_multiplier *= 1.05f;
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
			health_multiplier *= 1.24f;
			power_armor_multiplier *= 1.04f;
			break;
		case BossSizeCategory::Medium:
			ScaleEntity(1.2f);
			health_multiplier *= 1.12f;
			power_armor_multiplier *= 1.15f;
			break;
		case BossSizeCategory::Large:
			ScaleEntity(1.2f);
			health_multiplier *= 1.4f;
			power_armor_multiplier *= 1.2f;
			break;
		}

		// Aplicar efectos visuales adicionales
		boss->s.effects |= EF_PLASMA | EF_TAGTRAIL;
	}

	if (boss->monsterinfo.bonus_flags & BF_RAGEQUITTER) {
		boss->s.effects |= EF_BLUEHYPERBLASTER;
		boss->s.alpha = 0.6f;
		power_armor_multiplier *= 1.2f;
		boss->monsterinfo.invincible_time = std::max(level.time, boss->monsterinfo.invincible_time) + 12_sec;
	}

	if (boss->monsterinfo.bonus_flags & BF_BERSERKING) {
		boss->s.effects |= EF_GIB | EF_FLAG2;
		health_multiplier *= 1.22f;
		power_armor_multiplier *= 1.32f;
		boss->monsterinfo.quad_time = std::max(level.time, boss->monsterinfo.quad_time) + 475_sec;
		boss->monsterinfo.attack_state = AS_BLIND;
	}

	if (boss->monsterinfo.bonus_flags & BF_POSSESSED) {
		boss->s.effects |= EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE;
		boss->s.alpha = 0.6f;
		health_multiplier *= 1.2f;
		power_armor_multiplier *= 1.25f;
		boss->monsterinfo.attack_state = AS_BLIND;
	}

	if (boss->monsterinfo.bonus_flags & BF_STYGIAN) {
		// Aplicar escalado basado en la categoría de tamaño
		switch (sizeCategory) {
		case BossSizeCategory::Small:
			ScaleEntity(1.1f);
			health_multiplier *= 1.2f;
			power_armor_multiplier *= 1.1f;
			break;
		case BossSizeCategory::Medium:
			ScaleEntity(1.2f);
			health_multiplier *= 1.5f;
			power_armor_multiplier *= 1.1f;
			break;
		case BossSizeCategory::Large:
			ScaleEntity(1.2f);
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

	// Manejar Power Armor
	if (boss->monsterinfo.power_armor_power > 0)
	{
		boss->monsterinfo.power_armor_power = std::max(
			static_cast<int>(boss->monsterinfo.power_armor_power * power_armor_multiplier),
			power_armor_min
		);
	}

	// Manejar Armor regular
	if (boss->monsterinfo.armor_power > 0)
	{
		const int min_regular_armor = static_cast<int>(power_armor_min * REGULAR_ARMOR_FACTOR);

		boss->monsterinfo.armor_power = std::max(
			static_cast<int>(boss->monsterinfo.armor_power * power_armor_multiplier),
			min_regular_armor
		);
	}

	// En la parte del escalado por tamaño:
	switch (sizeCategory) {
	case BossSizeCategory::Small:
		boss->health = static_cast<int>(boss->health * 0.8f);
		if (boss->monsterinfo.power_armor_power > 0)
			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * 0.9f);
		if (boss->monsterinfo.armor_power > 0)
			boss->monsterinfo.armor_power = static_cast<int>(boss->monsterinfo.armor_power * 0.9f);
		break;
	case BossSizeCategory::Large:
		boss->health = static_cast<int>(boss->health * 1.2f);
		if (boss->monsterinfo.power_armor_power > 0)
			boss->monsterinfo.power_armor_power = static_cast<int>(boss->monsterinfo.power_armor_power * 1.2f);
		if (boss->monsterinfo.armor_power > 0)
			boss->monsterinfo.armor_power = static_cast<int>(boss->monsterinfo.armor_power * 1.2f);
		break;
	case BossSizeCategory::Medium:
		break;
	}

	// Asegurar mínimos después de ajustes
	boss->health = std::max(boss->health, health_min);
	if (boss->monsterinfo.power_armor_power > 0)
		boss->monsterinfo.power_armor_power = std::max(boss->monsterinfo.power_armor_power, power_armor_min);
	if (boss->monsterinfo.armor_power > 0) {
		const int min_regular_armor = static_cast<int>(power_armor_min * REGULAR_ARMOR_FACTOR);
		boss->monsterinfo.armor_power = std::max(boss->monsterinfo.armor_power, min_regular_armor);
	}

	boss->max_health = boss->health;

	// Marcar que los efectos ya fueron aplicados para evitar escalados acumulativos
	boss->effects_applied = true;

	//	gi.Com_PrintFmt("PRINT: Boss health set to: {}/{}\n", boss->health, boss->max_health);
}

//getting real name
[[nodiscard]] inline std::string GetPlayerName(const edict_t* player) {
	if (!player || !player->client) {
		return "N/A";
	}

	char buffer[MAX_INFO_VALUE] = { 0 };
	size_t written = gi.Info_ValueForKey(player->client->pers.userinfo, "name",
		buffer, sizeof(buffer) - 1);

	if (written == 0 || written >= sizeof(buffer)) {
		return "N/A";
	}

	buffer[written] = '\0'; // Ensure null termination
	return std::string(buffer);
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
	if (!spawned_entity || !spawned_entity->monsterinfo.IS_BOSS) {
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

bool G_IsClearPath(const edict_t* ignore, contents_t mask, const vec3_t& spot1, const vec3_t& spot2) {
	const trace_t tr = gi.traceline(spot1, spot2, ignore, mask);
	return (tr.fraction == 1.0f);
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

	// Hide entity during teleport
	ent->svflags |= SVF_NOCLIENT;
	gi.unlinkentity(ent);

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

	// Make entity visible again
	ent->svflags &= ~SVF_NOCLIENT;
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

bool EntitiesOverlap(const edict_t* ent, const vec3_t& area_mins, const vec3_t& area_maxs) {
	if (!ent)
		return false;

	// Calculate entity bounds
	const vec3_t ent_mins = ent->s.origin + ent->mins;
	const vec3_t ent_maxs = ent->s.origin + ent->maxs;

	// Use the existing boxes_intersect function directly
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

void ClearSpawnArea(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs) {
	// Validate all input vectors
	if (!is_valid_vector(origin) || !is_valid_vector(mins) || !is_valid_vector(maxs))
		return;

	// Calculate bounds with safe limits
	vec3_t area_mins, area_maxs;
	for (int i = 0; i < 3; i++) {
		area_mins[i] = origin[i] + mins[i] - 26.0f;
		area_maxs[i] = origin[i] + maxs[i] + 26.0f;
	}

	// Safe radius for search
	const float max_dim = std::max({
		maxs[0] - mins[0],
		maxs[1] - mins[1],
		maxs[2] - mins[2]
		});
	const float safe_radius = std::min(max_dim + 42.0f, 2048.0f);

	// Safely collect entities
	int entity_count = 0;
	edict_t* ent = nullptr;

	while ((ent = findradius(ent, origin, safe_radius)) != nullptr) {
		if (!ent || !ent->inuse || entity_count >= MAX_ENTITIES)
			continue;

		if ((ent->svflags & SVF_MONSTER) ||
			ent->solid == SOLID_NOT ||
			ent->solid == SOLID_TRIGGER)
			continue;

		// Additional validation
		if (!is_valid_vector(ent->s.origin) ||
			!is_valid_vector(ent->mins) ||
			!is_valid_vector(ent->maxs))
			continue;

		if (!EntitiesOverlap(ent, area_mins, area_maxs))
			continue;

		g_spawn_area_entities[entity_count++] = ent;
	}

	// Process collected entities
	for (int i = 0; i < entity_count; i++) {
		ent = g_spawn_area_entities[i];
		if (!ent || !ent->inuse)
			continue;

		if (ent->client) {
			edict_t* spawn_point = SelectSingleSpawnPoint(ent);
			if (spawn_point && spawn_point->inuse) {
				TeleportEntity(ent, spawn_point);
			}
		}
		else {
			RemoveEntity(ent);
		}
	}
}

void PushEntitiesAway(const vec3_t& center, int num_waves, float push_radius, float push_strength, float horizontal_push_strength, float vertical_push_strength)
{
	// Radio mínimo y búsqueda
	push_radius = std::max(push_radius, 1.0f);
	const float search_radius = push_radius * 1.5f;

	// Arrays estáticos para entidades
	static edict_t* entities_to_process[MAX_ENTITIES];
	static edict_t* entities_to_remove[MAX_ENTITIES];
	size_t process_count = 0;
	size_t remove_count = 0;



	// Recolectar entidades
	for (edict_t* ent = nullptr; (ent = findradius(ent, center, search_radius)) != nullptr;) {
		if (!ent || !ent->inuse)
			continue;

		// Verificar PVS y línea de visión
		if (gi.traceline(center, ent->s.origin, nullptr, MASK_SOLID).fraction < 1.0f)
			continue;

		// Clasificar entidades
		if (IsRemovableEntity(ent)) {
			if (remove_count < MAX_ENTITIES)
				entities_to_remove[remove_count++] = ent;
		}
		else if (ent->takedamage) {
			if (process_count < MAX_ENTITIES)
				entities_to_process[process_count++] = ent;
		}
	}

	// Remover entidades primero
	for (size_t i = 0; i < remove_count; i++) {
		if (entities_to_remove[i] && entities_to_remove[i]->inuse)
			RemoveEntity(entities_to_remove[i]);
	}

	// Procesar olas
	for (int wave = 0; wave < num_waves; wave++) {
		const float wave_progress = static_cast<float>(wave) / num_waves;
		const float size = std::max(push_radius * (1.0f - wave_progress * 0.5f), 0.030f);

		// Efecto visual mejorado
		SpawnGrow_Spawn(center, size, size * 0.3f);

		// Efectos adicionales para olas posteriores
		if (wave > 0) {
			vec3_t effect_pos = center;
			for (int i = 0; i < 4; i++) {
				effect_pos[0] = center[0] + crandom() * size * 0.5f;
				effect_pos[1] = center[1] + crandom() * size * 0.5f;
				effect_pos[2] = center[2] + crandom() * size * 0.25f;
				SpawnGrow_Spawn(effect_pos, size * 0.5f, size * 0.15f);
			}
		}

		// Procesar entidades
		for (size_t entity_idx = 0; entity_idx < process_count; entity_idx++) {
			edict_t* ent = entities_to_process[entity_idx];

			if (!ent || !ent->inuse)
				continue;

			if (!gi.inPVS(center, ent->s.origin, false))
				continue;

			// Calcular dirección y distancia
			vec3_t push_dir = ent->s.origin - center;
			const float dist = push_dir.length(); // Usar length() directamente en vez de sqrt(lengthSquared())

			if (dist < 0.01f) {
				push_dir = vec3_t{ crandom(), crandom(), 0.1f };
			}

			push_dir.normalize();

			// Calcular factor de distancia (1.0 cerca, 0.0 lejos)
			const float dist_factor = std::max(0.0f, 1.0f - (dist / search_radius));

			// Calcular fuerza base del knockback
			int base_push = (ent->svflags & SVF_MONSTER) ? 800 : 80;
			if (ent->groundentity)
				base_push *= 2;

			// Aplicar el factor de distancia al knockback
			base_push = static_cast<int>(base_push * dist_factor);

			// Marcar la protección contra daño de caída
			if (ent->client) {
				ent->client->landmark_free_fall = true;
			}

			// Usar T_Damage con knockback positivo 
			T_Damage(ent, ent, ent, push_dir, ent->s.origin, vec3_origin,
				0,  // sin daño 
				base_push,  // knockback escalado por distancia
				DAMAGE_RADIUS,  // flags 
				MOD_UNKNOWN);

			// Añadir componente vertical si es necesario
			if (vertical_push_strength > 0 && wave <= 1) {
				const float boost = (wave == 0) ? 100.0f : 75.0f;
				ent->velocity.z += boost;
			}

			// Actualizar cliente para efectos visuales
			if (ent->client && wave == 0) {
				ent->client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;
				ent->client->ps.pmove.pm_time = 100;
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
		{ "monster_shambler_small", "Tiny Shambler!" },
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
		{ "monster_jorg_small", "Mini Jorg" },
		{ "monster_gladb", "DarkMatter Gladiator" },
		{ "monster_boss2_64", "N64 Mini Hornet" },
		{ "monster_boss2_mini", "Mini Hornet" },
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
		{ "monster_sentrygun", "Sentry-Gun" },
		{ "monster_turret", "TurretGun" },
		{ "monster_turretkl", "TurretGun" },
		{ "monster_gnorta", "Gnorta" },
		{ "monster_shocker", "Shocker" },
		{ "monster_arachnid2", "Arachnid" },
		{ "monster_gm_arachnid", "Guided-Missile Arachnid" },
		{ "misc_insane", "Insane Grunt" },
		{ "food_cube_trap", "Stroggonoff Maker" },
		{ "tesla_mine", " Tesla Mine" },
		{ "emitter", "Laser Emitter" },
		{ "doppleganger", "Doppelganger" }
};

bool SpawnPointClear(edict_t* spot);
float PlayersRangeFromSpot(edict_t* spot);

bool TeleportSelf(edict_t* ent)
{
	// Basic validation
	if (!ent || !ent->inuse || !ent->client || !ent->solid || ent->deadflag)
		return false;

	// Check cooldown
	if (ent->client->teleport_cooldown > level.time)
	{
		// Inform player of remaining cooldown
		float remaining = std::floor((ent->client->teleport_cooldown - level.time).seconds() * 10.0f) / 10.0f;
		gi.LocClient_Print(ent, PRINT_HIGH, "Teleport on cooldown for {} seconds\n", remaining);
		return false;
	}

	// Set cooldown
	ent->client->teleport_cooldown = level.time + 3_sec;
	std::string playerName = GetPlayerName(ent);

	// Define spawn point structure
	struct spawn_point_t
	{
		edict_t* point;
		float dist;
	};

	// Pre-allocate a reasonable capacity
	std::vector<spawn_point_t> spawn_points;
	spawn_points.reserve(16);

	// Find valid spawn points
	edict_t* spot = nullptr;
	while ((spot = G_FindByString<&edict_t::classname>(spot, "info_player_deathmatch")) != nullptr) {
		if (spot->style == 0) {
			spawn_points.push_back({ spot, PlayersRangeFromSpot(spot) });
		}
	}

	// No valid spawn points found
	if (spawn_points.empty()) {
		if (developer->integer) {
			gi.Com_PrintFmt("PRINT TeleportSelf WARNING: No valid spawn points found for teleport.\n");
		}
		return false;
	}

	// Handle single spawn point case
	if (spawn_points.size() == 1) {
		if (SpawnPointClear(spawn_points[0].point)) {
			// Perform teleport
			TeleportEntity(ent, spawn_points[0].point);

			// Move sphere if owned
			if (ent->client->owned_sphere) {
				edict_t* sphere = ent->client->owned_sphere;
				sphere->s.origin = ent->s.origin;
				sphere->s.origin[2] = ent->absmax[2];
				sphere->s.angles[YAW] = ent->s.angles[YAW];
				gi.linkentity(sphere);
			}

			// Announce teleport
			if (!ent->client->emergency_teleport) {
				gi.LocBroadcast_Print(PRINT_HIGH, "{} Teleported Away!\n", playerName.c_str());
			}

			// Grant invincibility
			ent->client->invincible_time = max(level.time, ent->client->invincible_time) + 2_sec;
			return true;
		}

		if (developer->integer) {
			gi.Com_PrintFmt("PRINT TeleportSelf WARNING: Only spawn point is blocked.\n");
		}
		return false;
	}

	// Sort spawn points by distance (ascending)
	std::sort(spawn_points.begin(), spawn_points.end(),
		[](const spawn_point_t& a, const spawn_point_t& b) {
			return a.dist < b.dist;
		});

	// Try to find the farthest clear spawn point
	for (int32_t i = spawn_points.size() - 1; i >= 0; --i) {
		if (SpawnPointClear(spawn_points[i].point)) {
			// Perform teleport
			TeleportEntity(ent, spawn_points[i].point);

			// Move sphere if owned
			if (ent->client->owned_sphere) {
				edict_t* sphere = ent->client->owned_sphere;
				sphere->s.origin = ent->s.origin;
				sphere->s.origin[2] = ent->absmax[2];
				sphere->s.angles[YAW] = ent->s.angles[YAW];
				gi.linkentity(sphere);
			}

			// Announce teleport
			if (!ent->client->emergency_teleport) {
				gi.LocBroadcast_Print(PRINT_HIGH, "{} Teleported Away!\n", playerName.c_str());
			}

			// Grant invincibility
			ent->client->invincible_time = max(level.time, ent->client->invincible_time) + 2_sec;
			return true;
		}
	}

	// If no clear points found, use a random one with telefrags allowed
	const size_t random_index = rand() % spawn_points.size();

	// Perform teleport
	TeleportEntity(ent, spawn_points[random_index].point);

	// Move sphere if owned
	if (ent->client->owned_sphere) {
		edict_t* sphere = ent->client->owned_sphere;
		sphere->s.origin = ent->s.origin;
		sphere->s.origin[2] = ent->absmax[2];
		sphere->s.angles[YAW] = ent->s.angles[YAW];
		gi.linkentity(sphere);
	}

	// Announce teleport
	if (!ent->client->emergency_teleport) {
		gi.LocBroadcast_Print(PRINT_HIGH, "{} Teleported Away!\n", playerName.c_str());
	}

	// Grant invincibility and reset emergency flag
	ent->client->invincible_time = max(level.time, ent->client->invincible_time) + 2_sec;
	ent->client->emergency_teleport = false;

	if (developer->integer) {
		gi.Com_PrintFmt("PRINT WARNING TeleportSelf: No clear spawn points found, using random location.\n");
	}

	return true;
}