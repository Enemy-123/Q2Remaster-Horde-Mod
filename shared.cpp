#include "shared.h"
#include "horde/g_horde.h"
#include <unordered_map>
#include <algorithm>  // For std::max
#include <span>
#include "horde/horde_ids.h"
#include "horde/g_entity_properties.h"


horde::MapSize GetMapSize(const char* mapname) {
	// Use the namespace qualifier for the cache type
	static std::unordered_map<std::string, horde::MapSize> cache;

	// Check cache first
	const auto it = cache.find(mapname);
	if (it != cache.end()) {
		return it->second;
	}

	// Cache miss: Determine size using MapID
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(mapname);

	// Get size from the registry based on MapID
	// Use the namespace qualifier for the local variable type
	horde::MapSize size = horde::MapOriginRegistry::GetMapSize(mapId);
	// Note: GetMapSize(MapID) handles UNKNOWN and defaults to Medium

	// Store in cache and return
	cache[mapname] = size;
	return size;
}

bool IsRemovableEntity(const edict_t* ent);
void RemoveEntity(edict_t* ent);


void RemovePlayerOwnedEntities(edict_t* player) {
    if (!player || !player->client) { // Added player->client check for safety before accessing num_lasers
        return;
    }

    // Iterate through entities (starting from 1, as g_edicts[0] is world)
    size_t num_edicts = static_cast<size_t>(globals.num_edicts);
    for (size_t i = 1; i < num_edicts; ++i) {
        edict_t* ent = &g_edicts[i];

        if (!ent->inuse) {
            continue;
        }

        // Check ownership (direct owner or teammaster, and their owners/teammasters for two levels)
        bool is_owned_by_player = (ent->owner == player) ||
                                  (ent->owner && ent->owner->owner == player) ||
                                  (ent->teammaster == player) ||
                                  (ent->teammaster && ent->teammaster->teammaster == player);

        if (is_owned_by_player) {
            // Step 1: Perform generic "death" processing (scoring, events, etc.)
            // This function is assumed NOT to free the entity or run its specific die sequence.
            OnEntityDeath(ent);

            // Step 2: If the entity is of a type that has a specific removal sequence
            // (like turrets dying, mines exploding), then trigger that.
            if (IsRemovableEntity(ent)) { // Use the refactored IsRemovableEntity
                RemoveEntity(ent); // Use the refactored RemoveEntity
            }
            // If an entity is owned and OnEntityDeath is called, but it's not "IsRemovableEntity",
            // it implies its removal is handled elsewhere or it's not supposed to be removed
            // in this specific "player owned entity removal" pass beyond OnEntityDeath.
            // This matches the original logic flow.
        }
    }

    // Reset player-specific counts if any
    if (player->client) { // Ensure client exists
        player->client->num_lasers = 0;
        // Potentially reset other counts here if needed
    }
}

// --- Refactored Functions ---

bool IsPlayerDefense(const edict_t* ent) {
    if (!ent || !ent->classname) {
        return false;
    }
    // Get the ID from the classname
    horde::SpecialEntityTypeID id = horde::SpecialTypeRegistry::GetTypeID(ent->classname);
    if (id == horde::SpecialEntityTypeID::UNKNOWN) {
        return false;
    }
    // Use the ID for a fast array lookup
    return g_entityProperties.is_defense[static_cast<size_t>(id)];
}

bool IsRemovableEntity(const edict_t* ent) {
    if (!ent || !ent->classname) {
        return false;
    }
    horde::SpecialEntityTypeID id = horde::SpecialTypeRegistry::GetTypeID(ent->classname);
    if (id == horde::SpecialEntityTypeID::UNKNOWN) {
        return false;
    }
    return g_entityProperties.is_removable[static_cast<size_t>(id)];
}

void RemoveEntity(edict_t* ent) {
    if (!ent || !ent->inuse || !ent->classname) {
        return;
    }

    horde::SpecialEntityTypeID id = horde::SpecialTypeRegistry::GetTypeID(ent->classname);
    if (id != horde::SpecialEntityTypeID::UNKNOWN) {
        // Use the ID to look up the specific die handler
        EntityDieHandler handler = g_entityProperties.die_handler[static_cast<size_t>(id)];
        if (handler) {
            // Call the specific handler if it exists
            handler(ent, nullptr, nullptr, 0, ent->s.origin, mod_t{});
            return;
        }
    }

    // If no specific handler was found, use the generic explosion.
    // This also correctly handles any entity that is "removable" but doesn't
    // have a specific die function assigned in our source data.
    BecomeExplosion1(ent);
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

float M_DamageModifier(edict_t* monster) noexcept {
	if (!monster)
		return 1.0f;

	// Special case for sentry guns - use a reduced multiplier
	if (monster->classname && strcmp(monster->classname, "monster_sentrygun") == 0) {
		float modifier = 1.0f;

		// Apply reduced power-up multipliers for sentries
		if (monster->monsterinfo.quad_time > level.time)
			modifier = 2.0f;  // Reduced from 4.0 to 2.0
		else if (monster->monsterinfo.double_time > level.time)
			modifier = 1.5f;  // Reduced from 2.0 to 1.5

		// *** FIX: Resetting the flag here was incorrect. It should be reset *after* use. ***
		// monster->monsterinfo.damage_modifier_applied = false; // REMOVED
		return modifier;
	}

	// Standard logic for other monsters/entities
	float modifier = 1.0f;

	// Check power-ups in priority order
	if (monster->monsterinfo.quad_time > level.time)
		modifier = 4.0f;
	else if (monster->monsterinfo.double_time > level.time)
		modifier = 2.0f;

	// *** FIX: Resetting the flag here was incorrect. It should be reset *after* use. ***
	// monster->monsterinfo.damage_modifier_applied = false; // REMOVED

	return modifier;
}

std::string GetTitleFromFlags(int bonus_flags) {
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

// Static array to cache display names by TypeID for fast lookup
static std::array<std::string, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_displayNamesByID;
static bool g_displayNamesInitialized = false;

// Initialize the display names array once
void InitializeDisplayNames() {
	if (g_displayNamesInitialized) return;

	// First, fill with classnames as fallback
	for (size_t i = 0; i < static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES); i++) {
		const char* classname = horde::MonsterTypeRegistry::GetClassname(static_cast<horde::MonsterTypeID>(i));
		g_displayNamesByID[i] = classname ? classname : "Unknown";
	}

	// Then override with display names from the replacements map
	for (const auto& [classname_view, display_name] : name_replacements) {
		horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(classname_view.data());
		if (typeId != horde::MonsterTypeID::UNKNOWN) {
			g_displayNamesByID[static_cast<size_t>(typeId)] = std::string(display_name);
		}
	}

	g_displayNamesInitialized = true;
}

// Keep the same signature but use the ID system internally
std::string GetDisplayName(const char* classname) {
	if (!classname) return "Unknown";

	// Make sure names are initialized
	if (!g_displayNamesInitialized) {
		InitializeDisplayNames();
	}

	// Convert to TypeID and use fast array lookup
	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(classname);
	if (typeId != horde::MonsterTypeID::UNKNOWN) {
		// Use the cached name from the array
		return g_displayNamesByID[static_cast<size_t>(typeId)];
	}

	// Fallback for non-monster entities or unknown monsters
	auto it = name_replacements.find(classname);
	return it != name_replacements.end() ? std::string(it->second) : classname;
}

// Entity version remains simple but now uses the optimized classname version
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
	if (!monster || !monster->inuse)
		return;

	// Check IS_BOSS using the member within monsterinfo
	if (monster->monsterinfo.IS_BOSS)
		return;

	const spawn_temp_t& st = ED_GetSpawnTemp();
	
		if (monster->monsterinfo.bonus_flags != BF_NONE && (!(monster->monsterinfo.bonus_flags & BF_FRIENDLY))) {
			if (!st.was_key_specified("power_armor_power"))
				monster->monsterinfo.power_armor_power = monster->max_health * 0.4f;
		if (!st.was_key_specified("power_armor_type"))
			monster->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
	}

	// Handle summoned monster logic first (this flag can coexist with others)
	if (monster->monsterinfo.issummoned) {
		monster->monsterinfo.bonus_flags |= BF_FRIENDLY;
		FindMTarget(monster);
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

	// *** FIX: Set the NO_DROP flag correctly ***
	// This ensures the flag is set regardless of whether a bonus is applied later
	monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;

	monster->gib_health *= 2.8f;
	if (monster->gib_health <= -200)
		monster->gib_health = -200;

	// Using if-else to ensure only one bonus flag applies (in order of priority)
	if (monster->monsterinfo.bonus_flags & BF_CHAMPION) {
		monster->s.effects |= EF_ROCKET | EF_FIREBALL;
		monster->s.renderfx |= RF_SHELL_RED;
		monster->health *= 2.0f;
		monster->monsterinfo.power_armor_power *= 1.25f;
		monster->monsterinfo.armor_power *= 1.25f;
		monster->monsterinfo.double_time = std::max(level.time, monster->monsterinfo.double_time) + 475_sec;
		// *** FIX: Ensure NO_DROP flag is cleared if champion gets an item ***
		// (This logic is now handled later after item assignment)
	}
	else if (monster->monsterinfo.bonus_flags & BF_CORRUPTED) {
		monster->s.effects |= EF_PLASMA | EF_TAGTRAIL;
		monster->health *= 2.2f;
		monster->monsterinfo.power_armor_power *= 1.4f;
		monster->monsterinfo.armor_power *= 1.4f;
	}
	else if (monster->monsterinfo.bonus_flags & BF_RAGEQUITTER) {
		monster->s.effects |= EF_BLUEHYPERBLASTER;
		monster->s.alpha = 0.6f;
		monster->health *= 1.4f;
		monster->monsterinfo.power_armor_power *= 4.0f;
		monster->monsterinfo.armor_power *= 4.0f;
		monster->monsterinfo.invincible_time = max(level.time, monster->monsterinfo.invincible_time) + 7_sec;
	}
	else if (monster->monsterinfo.bonus_flags & BF_BERSERKING) {
		monster->s.effects |= EF_GIB | EF_FLAG2;
		monster->health *= 1.6f;
		monster->monsterinfo.power_armor_power *= 1.3f;
		monster->monsterinfo.armor_power *= 1.3f;
		monster->monsterinfo.quad_time = max(level.time, monster->monsterinfo.quad_time) + 475_sec;
		monster->monsterinfo.attack_state = AS_BLIND;
	}
	else if (monster->monsterinfo.bonus_flags & BF_POSSESSED) {
		monster->s.effects |= EF_BLASTER | EF_GREENGIB | EF_HALF_DAMAGE;
		monster->s.alpha = 0.5f;
		monster->health *= 2.4f;
		monster->monsterinfo.power_armor_power *= 1.7f;
		monster->monsterinfo.armor_power *= 1.7f;
		monster->monsterinfo.attack_state = AS_BLIND;
	}
	else if (monster->monsterinfo.bonus_flags & BF_STYGIAN) {
		monster->s.effects |= EF_TRACKER | EF_FLAG1;
		monster->health *= 2.5f;
		monster->monsterinfo.power_armor_power *= 1.1f;
		monster->monsterinfo.armor_power *= 1.1f;
		monster->monsterinfo.attack_state = AS_BLIND;
	}

	monster->max_health = monster->health;
	monster->s.renderfx |= RF_IR_VISIBLE;

	//// *** FIX: Adjust NO_DROP flag based on item presence AFTER potential item assignment ***
	//// This logic should ideally be called *after* the item drop chance is determined
	//// in the calling function (e.g., SpawnMonsters, EmergencySpawnMonster).
	//// Assuming monster->item has been potentially set by the caller:
	//if (monster->item) {
	//	monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP; // Clear flag if item exists
	//}
	//else {
	//	monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP; // Ensure flag is set if no item
	//}

	// Link the entity *after* all changes to ensure visuals are sent
	gi.linkentity(monster);
}
// Función auxiliar para calcular los valores mínimos de salud y armadura
static constexpr void CalculateBossMinimums(int wave_number, int& health_min, int& power_armor_min) noexcept
{
	// Definimos los límites absolutos
	constexpr int HEALTH_MAX_LIMIT = 21500;
	constexpr int POWER_ARMOR_MAX_LIMIT = 18000;

	if (wave_number >= 25) {
		health_min = HEALTH_MAX_LIMIT;  // Ya estamos al límite máximo
		power_armor_min = std::min(19550, POWER_ARMOR_MAX_LIMIT);
	}
	else if (wave_number >= 20) {
		health_min = std::min(15000, HEALTH_MAX_LIMIT);
		power_armor_min = std::min(9000, 12000);
	}
	else if (wave_number >= 15) {
		health_min = std::min(12400, 15500);
		power_armor_min = std::min(4500, 10000);
	}
	else if (wave_number >= 10) {
		health_min = std::min(8000, 13500);
		power_armor_min = std::min(5475, 8000);
	}
	else if (wave_number >= 5) {
		health_min = std::min(7500, 12000);  // Invertidos los números para que tenga más sentido
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
	if (!boss->monsterinfo.IS_BOSS || boss->monsterinfo.effects_applied)
		return;

	// Obtener la categoría de tamaño del jefe
	const BossSizeCategory sizeCategory = boss->bossSizeCategory;

	// Generar un flag aleatorio
	// Generar un flag aleatorio
	const int32_t random_flag_int = 1 << irandom(6);
	boss->monsterinfo.bonus_flags = static_cast<bonus_flags_t>(random_flag_int);

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
	boss->monsterinfo.effects_applied = true;

	//	gi.Com_PrintFmt("PRINT: Boss health set to: {}/{}\n", boss->health, boss->max_health);
}

//getting real name
std::string GetPlayerName(const edict_t* player) {
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
extern constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_SILENT = 1_spawnflag;
extern constexpr spawnflags_t SPAWNFLAGS_EARTHQUAKE_ONE_SHOT = 8_spawnflag;

void ImprovedSpawnGrow(const vec3_t& position, float start_size, float end_size, edict_t* spawned_entity)
{
	// Constantes para mejor legibilidad y mantenimiento
	constexpr int   NUM_SECONDARY_EFFECTS = 12;
	constexpr float RANDOM_OFFSET_RANGE = 255.0f;
	constexpr float EFFECT_SCALE = 0.55f;
	constexpr float EARTHQUAKE_SPEED = 500.0f;
	constexpr int   EARTHQUAKE_DURATION = 12;

	// Crear el efecto principal de spawn
	SpawnGrow_Spawn(position, start_size, end_size);

	// Si no es una entidad jefe, terminamos aquí
	if (!spawned_entity || !spawned_entity->monsterinfo.IS_BOSS) {
		return;
	}

	// Efectos adicionales para spawn de jefes
	for (int i = 0; i < NUM_SECONDARY_EFFECTS; i++)
	{
		vec3_t offset{};

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

// Optimized clear path check using modern vector operations
bool G_IsClearPath(const edict_t* ignore, contents_t mask, const vec3_t& spot1, const vec3_t& spot2) {
	// Early out if either vector is invalid
	if (!is_valid_vector(spot1) || !is_valid_vector(spot2))
		return false;
		
	// Use direct traceline call
	const trace_t tr = gi.traceline(spot1, spot2, ignore, mask);
	return (tr.fraction == 1.0f);
}

void TeleportEntity(edict_t* ent, edict_t* dest) {
	if (!ent || !ent->inuse || !dest || !dest->inuse)
		return;
	
	// Early-out if vectors are invalid
	if (!is_valid_vector(dest->s.origin))
		return;

	// Store original position for effect
	//const vec3_t old_origin = ent->s.origin;

	// Teleport effect at source
	// gi.WriteByte(svc_temp_entity);
	// gi.WriteByte(TE_TELEPORT_EFFECT);
	// gi.WritePosition(old_origin);
	// gi.multicast(old_origin, MULTICAST_PVS, false);

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
	// gi.WriteByte(svc_temp_entity);
	// gi.WriteByte(TE_TELEPORT_EFFECT);
	// gi.WritePosition(ent->s.origin);
	// gi.multicast(ent->s.origin, MULTICAST_PVS, false);
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

void ClearSpawnArea(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs) {
	if (!is_valid_vector(origin) || !is_valid_vector(mins) || !is_valid_vector(maxs))
		return;

	const vec3_t safe_offset{ 26.0f, 26.0f, 26.0f };
	const vec3_t area_mins = origin + mins - safe_offset;
	const vec3_t area_maxs = origin + maxs + safe_offset;

	const float max_dim = std::max({
		maxs[0] - mins[0],
		maxs[1] - mins[1],
		maxs[2] - mins[2]
		});
	const float safe_radius = std::min(max_dim + 42.0f, 2048.0f);

	std::vector<edict_t*> entities_in_area;
	// Optional: Reserve some space if you expect a certain number of entities often
	// entities_in_area.reserve(32);

	edict_t* ent = nullptr;
	while ((ent = findradius(ent, origin, safe_radius)) != nullptr) {
		if (!ent || !ent->inuse)
			continue;

		// Skip monsters and non-solid/trigger entities
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

		entities_in_area.push_back(ent);
	}

	// Process collected entities
	for (edict_t* current_ent : entities_in_area) {
		// Re-check inuse as processing might invalidate entities (though unlikely here)
		if (!current_ent || !current_ent->inuse)
			continue;

		if (current_ent->client) {
			edict_t* spawn_point = SelectSingleSpawnPoint(current_ent);
			if (spawn_point && spawn_point->inuse) {
				TeleportEntity(current_ent, spawn_point);
			}
		}
		else {
			RemoveEntity(current_ent);
		}
	}
}


void PushEntitiesAway(const vec3_t& center, int num_waves, float push_radius, float push_strength, float horizontal_push_strength, float vertical_push_strength)
{
	push_radius = std::max(push_radius, 1.0f);
	const float search_radius = push_radius * 1.5f;

	std::vector<edict_t*> entities_to_process;
	std::vector<edict_t*> entities_to_remove;
	// Optional: Reserve some space
	// entities_to_process.reserve(64);
	// entities_to_remove.reserve(16);

	// Collect entities
	for (edict_t* ent = nullptr; (ent = findradius(ent, center, search_radius)) != nullptr;) {
		if (!ent || !ent->inuse)
			continue;

		if (gi.traceline(center, ent->s.origin, nullptr, MASK_SOLID).fraction < 1.0f)
			continue;

		if (IsRemovableEntity(ent)) {
			entities_to_remove.push_back(ent);
		}
		else if (ent->takedamage) {
			entities_to_process.push_back(ent);
		}
	}

	// Remove designated entities first
	for (edict_t* ent_to_remove : entities_to_remove) {
		// Check inuse again in case it was removed by another process
		if (ent_to_remove && ent_to_remove->inuse)
			RemoveEntity(ent_to_remove);
	}

	// Process waves
	for (int wave = 0; wave < num_waves; wave++) {
		const float wave_progress = static_cast<float>(wave) / num_waves;
		const float size = std::max(push_radius * (1.0f - wave_progress * 0.5f), 0.030f);

		SpawnGrow_Spawn(center, size, size * 0.3f);

		if (wave > 0) {
			vec3_t effect_pos = center;
			for (int i = 0; i < 4; i++) {
				effect_pos[0] = center[0] + crandom() * size * 0.5f;
				effect_pos[1] = center[1] + crandom() * size * 0.5f;
				effect_pos[2] = center[2] + crandom() * size * 0.25f;
				SpawnGrow_Spawn(effect_pos, size * 0.5f, size * 0.15f);
			}
		}

		// Process entities to push/affect
		for (edict_t* ent : entities_to_process) {
			// Check inuse status as entities might be removed/killed between waves or by removals
			if (!ent || !ent->inuse)
				continue;

			if (!gi.inPVS(center, ent->s.origin, false))
				continue;

			vec3_t push_dir = ent->s.origin - center;
			const float dist = push_dir.length();

			if (dist < 0.01f) {
				push_dir = vec3_t{ crandom(), crandom(), 0.1f };
			}
			else {
				push_dir.normalize(); // Normalize only if dist is not near zeroF
			}


			const float dist_factor = std::max(0.0f, 1.0f - (dist / search_radius));

			int base_push = (ent->svflags & SVF_MONSTER) ? 800 : 80;
			if (ent->groundentity)
				base_push *= 2;

			base_push = static_cast<int>(base_push * dist_factor);

			if (ent->client) {
				ent->client->landmark_free_fall = true;
			}

			T_Damage(ent, ent, ent, push_dir, ent->s.origin, vec3_origin,
				0, base_push, DAMAGE_RADIUS, MOD_UNKNOWN);

			if (vertical_push_strength > 0 && wave <= 1) {
				const float boost = (wave == 0) ? 100.0f : 75.0f;
				ent->velocity.z += boost;
			}

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
		{ "monster_fixbotkl", "Fixer" },
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
		{ "monster_spider", "Plasma Spider" },
		{ "monster_arachnid", "Arachnid" },
		{ "monster_psxarachnid", "Arachnid" },
		{ "monster_guncmdr", "Gunner Grenadier" },
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
		{ "monster_guncmdrkl", "Grenadier Commander" },
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
		{ "monster_turret", "Turret" },
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

bool TeleportSelf(edict_t* ent) {
    if (!ent || !ent->inuse || !ent->client || !ent->solid || ent->deadflag) {
        return false;
    }

    // Use direct client members
    if (ent->client->teleport_cooldown > level.time) {
        // Assuming teleport_cooldown and level.time are gtime_t or compatible
        float remaining_seconds = (ent->client->teleport_cooldown - level.time).seconds();
        float remaining_display = std::floor(remaining_seconds * 10.0f) / 10.0f;
        gi.LocClient_Print(ent, PRINT_HIGH, "Teleport on cooldown for {} seconds\n", remaining_display);
        return false;
    }

    ent->client->teleport_cooldown = level.time + 3_sec; // Apply cooldown
    std::string playerName = GetPlayerName(ent);

    struct spawn_point_info_t {
        edict_t* point;
        float dist; // Distance from players or some other metric
    };

    std::vector<spawn_point_info_t> spawn_points;
    spawn_points.reserve(16);

	for (edict_t* spot : monster_spawn_points()) {
		if (spot->style == 0) {
			spawn_points.push_back({spot, PlayersRangeFromSpot(spot)});
		}
	}

    if (spawn_points.empty()) {
        if (developer->integer) {
            gi.Com_PrintFmt("PRINT TeleportSelf WARNING: No valid spawn points found for teleport.\n");
        }
        return false;
    }

    // Helper lambda to perform the teleport and associated actions
    auto perform_teleport_actions = [&](edict_t* destination_spot) {
        TeleportEntity(ent, destination_spot);

        if (ent->client->owned_sphere) {
            edict_t* sphere = ent->client->owned_sphere;
            sphere->s.origin = ent->s.origin;
            // Assuming vec3_t has .z or you use [2]
            // Using absmax.z (or absmax[2]) to place it on top of the player's new bounding box
            sphere->s.origin.z = ent->absmax.z; 
            sphere->s.angles[YAW] = ent->s.angles[YAW];
            gi.linkentity(sphere);
        }

        if (!ent->client->emergency_teleport) {
            gi.LocBroadcast_Print(PRINT_HIGH, "{} Teleported Away!\n", playerName.c_str());
        }
        
        // Use std::max for gtime_t or ensure types are compatible
        ent->client->invincible_time = std::max(level.time, ent->client->invincible_time) + 2_sec;
    };

    if (spawn_points.size() == 1) {
        if (SpawnPointClear(spawn_points[0].point)) {
            perform_teleport_actions(spawn_points[0].point);
            // If this was an emergency teleport, and it succeeded, reset the flag.
            // This depends on desired logic: reset on any success, or only on fallback?
            // Original reset only on fallback. Let's assume reset on any successful emergency TP.
            if (ent->client->emergency_teleport) {
                ent->client->emergency_teleport = false;
            }
            return true;
        }
        if (developer->integer) {
            gi.Com_PrintFmt("PRINT TeleportSelf WARNING: Only spawn point is blocked.\n");
        }
        return false; 
    }

    // Sort spawn points by distance (farthest first for iteration)
    std::sort(spawn_points.begin(), spawn_points.end(),
              [](const spawn_point_info_t& a, const spawn_point_info_t& b) {
                  return a.dist > b.dist; // Sort descending by distance (farthest first)
              });

    for (const auto& sp_info : spawn_points) {
        if (SpawnPointClear(sp_info.point)) {
            perform_teleport_actions(sp_info.point);
            if (ent->client->emergency_teleport) { // Reset if it was an emergency TP
                ent->client->emergency_teleport = false;
            }
            return true;
        }
    }

    // Fallback: No clear spot found, teleport to a random one from the available (but likely blocked) spots.
    // Use your provided random_index template function.
    const int32_t random_idx = random_index(spawn_points); 
    edict_t* random_destination = spawn_points[random_idx].point;

    if (developer->integer) {
        gi.Com_PrintFmt("PRINT TeleportSelf WARNING: No clear spawn points. Using random point (index %i, potentially blocked).\n", random_idx);
    }
    
    perform_teleport_actions(random_destination);
    
    // Unconditionally reset emergency_teleport if we hit this fallback path,
    // consistent with original code's structure where it was reset here.
    ent->client->emergency_teleport = false; 
    
    return true;
}

// --- Extern Declarations for Monster Jump Moves ---

// Berserk
extern const mmove_t berserk_move_jump;
extern const mmove_t berserk_move_jump2;

// Brain
extern const mmove_t brain_move_jumpattack;
extern const mmove_t brain_move_jump;
extern const mmove_t brain_move_jump2;

// Chick
extern const mmove_t chick_move_jump;
extern const mmove_t chick_move_jump2;

// Gun Commander
extern const mmove_t guncmdr_move_jump;
extern const mmove_t guncmdr_move_jump2;

// Gunner
extern const mmove_t gunner_move_jump;
extern const mmove_t gunner_move_jump2;

// Gunner Vanilla
extern const mmove_t gunner_vanilla_move_jump;
extern const mmove_t gunner_vanilla_move_jump2;

// Infantry
extern const mmove_t infantry_move_jump;
extern const mmove_t infantry_move_jump2;

// Mutant
extern const mmove_t mutant_move_jump;
extern const mmove_t mutant_move_jump_up;
extern const mmove_t mutant_move_jump_down;

// Parasite
extern const mmove_t parasite_move_jump_up;
extern const mmove_t parasite_move_jump_down;

// Red Mutant
extern const mmove_t redmutant_move_jump;
extern const mmove_t redmutant_move_jump_up;
extern const mmove_t redmutant_move_jump_down;

// Runner Tank
extern const mmove_t runnertank_move_jump;
extern const mmove_t runnertank_move_jump2;

// Shocker
extern const mmove_t shocker_move_jump;
extern const mmove_t shocker_move_jump2;

// Soldier
extern const mmove_t soldier_move_jump;
extern const mmove_t soldier_move_jump2;

// Stalker
extern const mmove_t stalker_move_jump_straightup;
extern const mmove_t stalker_move_jump_up;
extern const mmove_t stalker_move_jump_down;

// Gekk
extern const mmove_t gekk_move_jump_up;
extern const mmove_t gekk_move_jump_down;

// --- End Extern Declarations ---

// --- Global Data Structure for Fast Lookups ---
// Using a sorted vector for lookups. For a relatively small, static set of pointers,
// this offers superior cache locality and can outperform hash-based containers like
// std::unordered_set due to lower overhead from hashing and pointer chasing.
static std::vector<const mmove_t*> g_jump_moves;


// --- Initialization Function ---
// Call this function ONCE during your game/mod initialization (e.g., in InitGame).
void InitializeMonsterMoveSets() {
    // Prevent re-initialization
    if (!g_jump_moves.empty()) {
        return;
    }

    // Reserve memory to prevent reallocations while populating the vector.
    // There are 34 jump moves defined.
    g_jump_moves.reserve(34);

    // Populate the vector with all known jump moves.
    // Berserk
    g_jump_moves.push_back(&berserk_move_jump);
    g_jump_moves.push_back(&berserk_move_jump2);
    
    // Brain
    g_jump_moves.push_back(&brain_move_jumpattack);
    g_jump_moves.push_back(&brain_move_jump);
    g_jump_moves.push_back(&brain_move_jump2);
    
    // Chick
    g_jump_moves.push_back(&chick_move_jump);
    g_jump_moves.push_back(&chick_move_jump2);
    
    // Gun Commander
    g_jump_moves.push_back(&guncmdr_move_jump);
    g_jump_moves.push_back(&guncmdr_move_jump2);
    
    // Gunner
    g_jump_moves.push_back(&gunner_move_jump);
    g_jump_moves.push_back(&gunner_move_jump2);
    
    // Gunner Vanilla
    g_jump_moves.push_back(&gunner_vanilla_move_jump);
    g_jump_moves.push_back(&gunner_vanilla_move_jump2);
    
    // Infantry
    g_jump_moves.push_back(&infantry_move_jump);
    g_jump_moves.push_back(&infantry_move_jump2);
    
    // Mutant
    g_jump_moves.push_back(&mutant_move_jump);
    g_jump_moves.push_back(&mutant_move_jump_up);
    g_jump_moves.push_back(&mutant_move_jump_down);
    
    // Parasite
    g_jump_moves.push_back(&parasite_move_jump_up);
    g_jump_moves.push_back(&parasite_move_jump_down);
    
    // Red Mutant
    g_jump_moves.push_back(&redmutant_move_jump);
    g_jump_moves.push_back(&redmutant_move_jump_up);
    g_jump_moves.push_back(&redmutant_move_jump_down);
    
    // Runner Tank
    g_jump_moves.push_back(&runnertank_move_jump);
    g_jump_moves.push_back(&runnertank_move_jump2);
    
    // Soldier
    g_jump_moves.push_back(&soldier_move_jump);
    g_jump_moves.push_back(&soldier_move_jump2);
    
    // Stalker
    g_jump_moves.push_back(&stalker_move_jump_straightup);
    g_jump_moves.push_back(&stalker_move_jump_up);
    g_jump_moves.push_back(&stalker_move_jump_down);
    
    // Gekk
    g_jump_moves.push_back(&gekk_move_jump_up);
    g_jump_moves.push_back(&gekk_move_jump_down);

    // Sort the vector to allow for fast binary searching.
    std::sort(g_jump_moves.begin(), g_jump_moves.end());
}


// --- Optimized IsMonsterJumping Function ---
// This version uses a pre-sorted vector and binary search for a fast, cache-friendly lookup.
bool IsMonsterJumping(const edict_t* self) {
    // Early exit for invalid entity or if no move is active.
    if (!self || !self->monsterinfo.active_move) {
        return false;
    }

    // Get the raw pointer to the current move from the save_data_t wrapper.
    const mmove_t* current_move = self->monsterinfo.active_move.pointer();

    // Perform a binary search on the sorted vector of jump moves.
    // This is very efficient for this type of check.
    return std::binary_search(g_jump_moves.cbegin(), g_jump_moves.cend(), current_move);
}