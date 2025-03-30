// Includes y definiciones relevantes
#include "../g_local.h"
#include "g_horde.h"
#include "../shared.h"
#include <set>
#include "g_horde_benefits.h"
#include "horde_ids.h"
#include <span>
#include "../laser.h" 


static bool need_spawn_cache_reset = false;
static bool need_frame_timer_reset = false;
static bool need_queue_monitor_reset = false;

// Ambush system tracking variables
static gtime_t last_ambush_time = 0_sec;
static int32_t ambush_cooldown_frames = 0;
static int32_t waves_since_ambush = 0;
static bool ambush_system_initialized = false;

// --- Recent Spawn Position Tracking ---
struct RecentSpawnPosition {
	vec3_t position = {};
	gtime_t cooldown_until = 0_sec;
};
static constexpr size_t MAX_RECENT_POSITIONS = 32; // History for TryAlternativeSpawnPosition
static std::array<RecentSpawnPosition, MAX_RECENT_POSITIONS> g_recent_spawn_positions;
static size_t g_recent_position_index = 0;

// --- Recent Teleport Position Tracking ---
struct RecentTeleportPosition {
	vec3_t position = {};
	gtime_t teleport_time = 0_sec;
};
static constexpr int MAX_RECENT_TELEPORT_LOCATIONS = 8; // History for CheckAndTeleportStuckMonster
static std::array<RecentTeleportPosition, MAX_RECENT_TELEPORT_LOCATIONS> g_recent_teleport_positions;
static int g_recent_teleport_index = 0;

// --- Constants for Spawning and Teleporting ---
namespace HordeConstants {

	constexpr gtime_t BASE_SPAWN_TELEPORT_COOLDOWN = 5.0_sec; // Base cooldown applied to spawn point after teleport
	constexpr gtime_t MIN_SPAWN_TELEPORT_COOLDOWN = 2.0_sec; // Absolute minimum cooldown for spawn point after teleport
	// --- Minimum Cooldown Durations ---
	constexpr gtime_t MIN_GLOBAL_SPAWN_COOLDOWN = 1.5_sec; // Minimum for the base SPAWN_POINT_COOLDOWN (matches existing clamp)
	constexpr gtime_t MIN_INDIVIDUAL_SUCCESS_COOLDOWN = 0.5_sec; // Min time after successful spawn
	constexpr gtime_t MIN_INDIVIDUAL_FAILURE_COOLDOWN = 0.5_sec; // Min time after failed spawn attempt
	constexpr gtime_t MIN_ALT_SUCCESS_COOLDOWN = 1.0_sec;      // Min time after successful *alternative* spawn
	constexpr gtime_t MIN_ALT_FAILURE_COOLDOWN = 1.0_sec;      // Min time after failed *alternative* spawn attempt
	constexpr gtime_t MIN_REDUCED_INDIVIDUAL_COOLDOWN = 0.5_sec; // Min duration when reducing cooldowns late wave
	// MIN_TELEPORT_COOLDOWN_MONSTER already exists (12s) and is handled by random_time
	constexpr gtime_t MIN_MONSTER_SPAWN_INTERVAL = 0.8_sec;      // Absolute minimum time between monster spawns

	constexpr float TIME_REDUCTION_MULTIPLIER = 0.95f;
	constexpr vec3_t VALIDATE_CHECK_MINS = { -16, -16, -24 };
	constexpr vec3_t VALIDATE_CHECK_MAXS = { 16,  16,  32 };
	constexpr float MIN_PLAYER_DIST_GENERATE = 200.0f; // Minimum radius for *generating* emergency positions
	constexpr float MIN_PLAYER_DIST_CHECK = 180.0f;    // Minimum final distance *after* validation/drop
	constexpr float MIN_PLAYER_DIST_SQ_CHECK = MIN_PLAYER_DIST_CHECK * MIN_PLAYER_DIST_CHECK;
	constexpr float MIN_PLAYER_DIST_SQ = 150.0f * 150.0f; // Squared distance for efficiency
	constexpr float MIN_RECENT_SPAWN_DIST = 60.0f;      // Min distance between regular/alternative spawns
	constexpr float MIN_RECENT_SPAWN_DIST_SQ = MIN_RECENT_SPAWN_DIST * MIN_RECENT_SPAWN_DIST;
	constexpr float MIN_RECENT_TELEPORT_DIST = 250.0f;  // Min distance between teleport destinations
	constexpr float MIN_RECENT_TELEPORT_DIST_SQ = MIN_RECENT_TELEPORT_DIST * MIN_RECENT_TELEPORT_DIST;
	constexpr gtime_t RECENT_SPAWN_COOLDOWN = 3.0_sec;  // How long a regular spawn pos is considered recent
	constexpr gtime_t RECENT_TELEPORT_COOLDOWN = 5.0_sec; // How long a teleport pos is considered recent

	// ---  CheckAndTeleportStuckMonster coonstants ---
	int g_teleport_rate_count = 0;
	gtime_t g_teleport_rate_reset_time = level.time;

	// Alt position constants
	constexpr gtime_t ALT_SPAWN_COOLDOWN_SHORT = 1.5_sec;
	constexpr gtime_t ALT_SPAWN_COOLDOWN_MEDIUM = 3.0_sec;
	constexpr size_t NUM_HORDE_ALT_POSITIONS = 8;
	constexpr std::array<vec3_t, NUM_HORDE_ALT_POSITIONS> horde_alternative_positions = {
		vec3_t{ 40, 0, 8 },   vec3_t{ -40, 0, 8 },
		vec3_t{ 0, 40, 8 },   vec3_t{ 0, -40, 8 },
		vec3_t{ 30, 30, 0 },  vec3_t{ -30, 30, 0 },
		vec3_t{ 30, -30, 0 }, vec3_t{ -30, -30, 0 }
	};

	// Teleport stuck monster constants
	constexpr gtime_t STUCK_CHECK_TIME = 3_sec;
	constexpr gtime_t MIN_TELEPORT_COOLDOWN_MONSTER = 12_sec;
	constexpr gtime_t MAX_TELEPORT_COOLDOWN_MONSTER = 20_sec;
	constexpr gtime_t GLOBAL_TELEPORT_RESET_INTERVAL = 8_sec;
	constexpr int MAX_TELEPORTS_PER_INTERVAL = 2;

	// Base wave count constants
	constexpr float BASE_DIFFICULTY_MULTIPLIER = 1.1f;
	constexpr float PLAYER_COUNT_SCALE = 0.2f;
	constexpr std::array<std::array<int32_t, 4>, 3> BASE_COUNTS = { {
		{{6, 8, 10, 12}},  // Small maps
		{{8, 12, 14, 16}}, // Medium maps
		{{15, 18, 23, 26}} // Large maps
	} };
	constexpr std::array<int32_t, 3> ADDITIONAL_SPAWNS = { 8, 7, 12 }; // Small, Medium, Large
}

// --- Forward Declarations ---
[[nodiscard]] bool IsValidSpawnLocation(vec3_t& io_position, const vec3_t& monster_mins, const vec3_t& monster_maxs, bool is_flying);
bool CheckAndTeleportStuckMonster(edict_t* self);
bool FindEmergencySpawnPosition(vec3_t& position, vec3_t& angles, bool& used_human_player, horde::MonsterTypeID typeId);
bool TryAlternativeSpawnPosition(edict_t* spawn_point, horde::MonsterTypeID typeId, vec3_t& final_origin, vec3_t& final_angles);
edict_t* SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t& origin, const vec3_t& angles, bool applyHordeFlags);

// --- Helper Functions ---
void MarkPositionAsRecentlyUsed(const vec3_t& position) {
	g_recent_spawn_positions[g_recent_position_index] = {
		position,
		level.time + HordeConstants::RECENT_SPAWN_COOLDOWN
	};
	g_recent_position_index = (g_recent_position_index + 1) % MAX_RECENT_POSITIONS;
}

bool IsPositionTooCloseToRecentSpawn(const vec3_t& position) {
	const gtime_t current_time = level.time;
	for (const auto& recent : g_recent_spawn_positions) {
		if (recent.cooldown_until > current_time) {
			if ((position - recent.position).lengthSquared() < HordeConstants::MIN_RECENT_SPAWN_DIST_SQ) {
				return true;
			}
		}
	}
	return false;
}

void MarkPositionAsRecentlyTeleported(const vec3_t& position) {
	g_recent_teleport_positions[g_recent_teleport_index] = {
		position,
		level.time + HordeConstants::RECENT_TELEPORT_COOLDOWN // Mark when it becomes not recent
	};
	g_recent_teleport_index = (g_recent_teleport_index + 1) % MAX_RECENT_TELEPORT_LOCATIONS;
}

bool IsPositionTooCloseToRecentTeleport(const vec3_t& position) {
	const gtime_t current_time = level.time;
	for (const auto& recent : g_recent_teleport_positions) {
		// Check if the cooldown has expired (teleport_time stores the time *until* it's no longer recent)
		if (recent.teleport_time > current_time) {
			if ((position - recent.position).lengthSquared() < HordeConstants::MIN_RECENT_TELEPORT_DIST_SQ) {
				return true;
			}
		}
	}
	return false;
}

// (Keep ResetSpawnMonsterVars, ResetFrameTimers, ResetQueueMonitorVars as they were)
void ResetSpawnMonsterVars() {
	need_spawn_cache_reset = true;
	horde::g_monsterSpawnTracker.Reset();
}
void ResetFrameTimers() { need_frame_timer_reset = true; }
void ResetQueueMonitorVars() {
	need_queue_monitor_reset = true;
	horde::g_spawnPointTimeTracker.Reset();
}

// --- Global/Static Variables ---
bool champion_spawned_this_wave = false;
int champion_spawn_cooldown = 0;
int consistent_zero_counts = 0;
int counter_mismatch_frames = 0;
constexpr size_t MAX_SPAWN_POINTS = 32;

// (Keep SpawnPointData and SpawnPointDataArray structs as they were)
struct SpawnPointData {
	uint16_t attempts = 0;
	gtime_t teleport_cooldown = 0_sec;  // Added teleport_cooldown
	gtime_t lastSpawnTime = 0_sec;      // Added lastSpawnTime
	bool isTemporarilyDisabled = false;
	gtime_t cooldownEndsAt = 0_sec;
	int32_t successfulSpawns = 0;

	// New fields for alternative position tracking
	uint16_t alternative_attempts = 0;    // Added alternative_attempts
	gtime_t alternative_cooldown = 0_sec; // Added alternative_cooldown
	bool needs_long_alternative_cooldown = false; // Added needs_long_alternative_cooldown

	// Update the success rate calculation method to accept current_time
	float getSuccessRate(gtime_t current_time) const {
		if (attempts == 0) return 1.0f;
		// Use faster approximation - avoid division when possible
		const float time_factor = (current_time - lastSpawnTime).seconds() >= 5.0f ?
			1.0f : (current_time - lastSpawnTime).seconds() * 0.2f;
		// Clamp time_factor to avoid excessive influence
		const float clamped_time_factor = std::min(time_factor, 1.0f);
		// Calculate base success rate, ensure attempts is not zero
		const float base_rate = (attempts > 0) ? (float(successfulSpawns) / float(attempts)) : 1.0f;

		// Combine base rate and time factor, ensuring result is between 0 and 1
		return std::clamp(base_rate + clamped_time_factor, 0.0f, 1.0f);
	}
};
struct SpawnPointDataArray {
	SpawnPointData data[MAX_EDICTS];
	SpawnPointData& operator[](const edict_t* ent) { return data[ent - g_edicts]; }
	const SpawnPointData& operator[](const edict_t* ent) const { return data[ent - g_edicts]; }
	void clear() { for (auto& item : data) item = SpawnPointData{}; }
	bool find_and_access(const edict_t* key, SpawnPointData*& data_ptr) {
		data_ptr = &data[key - g_edicts]; return true;
	}
};
SpawnPointDataArray spawnPointsData;

void ApplyAlternativePositionCooldown(edict_t* spawn_point) {
	if (!spawn_point || !spawn_point->inuse) return;
	auto& data = spawnPointsData[spawn_point];
	data.alternative_attempts++;
	gtime_t cooldown_duration;
	if (data.alternative_attempts <= 2) cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_SHORT;
	else if (data.alternative_attempts <= 5) cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_MEDIUM;
	else {
		cooldown_duration = 5.0_sec + gtime_t::from_sec(0.5f * (data.alternative_attempts - 5));
		cooldown_duration = std::min(cooldown_duration, 10.0_sec);
		if (data.alternative_attempts >= 8) data.needs_long_alternative_cooldown = true;
	}

	// Clamp the calculated alternative failure cooldown duration
	const gtime_t final_alt_duration = std::max(cooldown_duration, HordeConstants::MIN_ALT_FAILURE_COOLDOWN);
	data.alternative_cooldown = level.time + final_alt_duration;

	data.isTemporarilyDisabled = true; // Also disable original point shortly
	// Also clamp the normal point's shorter cooldown based on the *clamped* alternative duration
	const gtime_t final_normal_duration = std::max(final_alt_duration * 0.5f, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
	data.cooldownEndsAt = level.time + final_normal_duration;

	if (developer->integer) gi.Com_PrintFmt("Alternative position cooldown applied to spawn at {}: {:.1f}s (attempts: {})\n", spawn_point->s.origin, final_alt_duration.seconds(), data.alternative_attempts);
}

void ApplySuccessfulAlternativeCooldown(edict_t* spawn_point) {
	if (!spawn_point || !spawn_point->inuse) return;
	auto& data = spawnPointsData[spawn_point];
	data.alternative_attempts = 0;
	data.needs_long_alternative_cooldown = false;
	// Ensure the 3.0s meets the minimum alternative success cooldown
	data.alternative_cooldown = level.time + std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN);
	if (developer->integer > 1) gi.Com_PrintFmt("Success cooldown applied to spawn at {}: {:.1f}s\n", spawn_point->s.origin, std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN).seconds());
}
void IncreaseSpawnAttempts(edict_t* spawn_point) {
	if (!spawn_point || !spawn_point->inuse) return;
	auto& data = spawnPointsData[spawn_point];
	if (level.time - data.lastSpawnTime > 6_sec) { data = {}; return; } // Reset if long time passed

	data.attempts++;
	const float success_rate = data.getSuccessRate(level.time);
	const int max_attempts = 4 + (success_rate >= 0.5f ? 2 : (success_rate >= 0.25f ? 1 : 0));

	gtime_t calculated_duration = 0_sec; // Initialize

	if (data.attempts >= max_attempts) {
		data.isTemporarilyDisabled = true;
		const float cooldown_factor = success_rate < 0.3f ? 1.5f : 0.75f;
		const float attempt_multiplier = data.attempts <= 8 ? data.attempts * 0.25f : 2.0f;
		calculated_duration = gtime_t::from_sec(cooldown_factor * attempt_multiplier);
		if (developer->integer == 1) gi.Com_PrintFmt("SpawnPoint at {} inactivated for adaptive cooldown.\n", spawn_point->s.origin);
	}
	else if ((data.attempts & 1) == 0) { // Every 2 attempts
		calculated_duration = gtime_t::from_sec(0.2f * data.attempts);
	}

	// Apply minimum duration clamp if a duration was calculated
	if (calculated_duration > 0_sec) {
		const gtime_t final_duration = std::max(calculated_duration, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
		data.cooldownEndsAt = level.time + final_duration;
	}
	// No else needed, if no duration was calculated, cooldownEndsAt isn't set here
}

void OnSuccessfulSpawn(edict_t* spawn_point) {
	if (!spawn_point || !spawn_point->inuse) return;
	auto& data = spawnPointsData[spawn_point];
	data.successfulSpawns++;
	data.attempts = 0;
	data.isTemporarilyDisabled = false;
	// Use the minimum success cooldown constant
	data.cooldownEndsAt = level.time + HordeConstants::MIN_INDIVIDUAL_SUCCESS_COOLDOWN;
	horde::g_spawnPointTimeTracker.SetLastSpawnTime(spawn_point, level.time);
}
struct SpawnPointCache {
	gtime_t last_check_time = 0_sec;
	vec3_t last_check_origin = {};
	gtime_t frame_time = 0_sec;         // Frame time of the last check
	bool was_occupied_by_player = false; // True if a player was found in the last check
	bool has_obstacle = false;          // True if a monster/defense was found (excluding player)
};

struct SpawnPointCacheArray {
	SpawnPointCache data[MAX_EDICTS];
	SpawnPointCache& operator[](const edict_t* ent) { return data[ent - g_edicts]; }
	const SpawnPointCache& operator[](const edict_t* ent) const { return data[ent - g_edicts]; }
	void clear() { for (auto& item : data) item = SpawnPointCache{}; }
};
static SpawnPointCacheArray spawn_point_cache;

bool IsSpawnPointOccupied(const edict_t* spawn_point, const edict_t* ignore_ent = nullptr) {
	// --- Basic Validation ---
	if (!spawn_point || !spawn_point->inuse || !is_valid_vector(spawn_point->s.origin)) {
		if (developer->integer) {
			if (!spawn_point) gi.Com_PrintFmt("Warning: Null spawn_point passed to IsSpawnPointOccupied\n");
			else gi.Com_PrintFmt("Warning: Invalid origin vector in spawn point {}\n", spawn_point->s.origin);
		}
		return true; // Safer to assume occupied if invalid
	}

	// --- Cache Check ---
	SpawnPointCache& cache = spawn_point_cache[spawn_point];
	// Increased cache duration slightly for better performance vs accuracy balance
	static constexpr auto CACHE_DURATION = 100_ms; // 0.1 seconds

	// Check time-based cache validity AND frame validity
	if (level.time - cache.last_check_time < CACHE_DURATION &&
		cache.last_check_origin == spawn_point->s.origin &&
		(level.time - cache.frame_time) < FRAME_TIME_MS * 2) // Allow cache reuse within 2 frames
	{
		// Return the cached player occupation status.
		// The caller (SelectRandomSpawnPoint) will use cache.has_obstacle separately
		// if this function returns false.
		return cache.was_occupied_by_player;
	}

	// --- Cache Miss - Perform Live Check ---
	cache.last_check_time = level.time;
	cache.last_check_origin = spawn_point->s.origin;
	cache.frame_time = level.time;
	// Reset flags before checking
	cache.was_occupied_by_player = false;
	cache.has_obstacle = false;

	// Define bounding box for the check
	static const vec3_t mins_scale = vec3_t{ 16, 16, 24 }.scaled(vec3_t{ 1.75f, 1.75f, 1.75f });
	static const vec3_t maxs_scale = vec3_t{ 16, 16, 32 }.scaled(vec3_t{ 1.75f, 1.75f, 1.75f });
	const vec3_t spawn_mins = spawn_point->s.origin - mins_scale;
	const vec3_t spawn_maxs = spawn_point->s.origin + maxs_scale;

	// --- BoxEdicts Checks ---
	// We need to check for players AND obstacles separately to update the cache correctly.
	FilterData check_data = { ignore_ent, 0 }; // Reusable filter data

	// Check 1: Players
	gi.BoxEdicts(spawn_mins, spawn_maxs, nullptr, 0, AREA_SOLID,
		[](edict_t* ent, void* data) -> BoxEdictsResult_t {
			FilterData* fd = static_cast<FilterData*>(data);
			if (ent == fd->ignore_ent) return BoxEdictsResult_t::Skip;
			// Check only for active clients (players or bots)
			if (ent->client && ent->inuse) {
				fd->count++;
				return BoxEdictsResult_t::End; // Found a player/bot
			}
			return BoxEdictsResult_t::Skip;
		}, &check_data);

	cache.was_occupied_by_player = (check_data.count > 0);

	// Check 2: Obstacles (Monsters/Defenses) - ONLY if not already blocked by a player
	if (!cache.was_occupied_by_player) {
		check_data.count = 0; // Reset count for the next check
		gi.BoxEdicts(spawn_mins, spawn_maxs, nullptr, 0, AREA_SOLID,
			[](edict_t* ent, void* data) -> BoxEdictsResult_t {
				FilterData* fd = static_cast<FilterData*>(data);
				if (ent == fd->ignore_ent) return BoxEdictsResult_t::Skip;
				// Check for live monsters OR player defenses
				if ((ent->svflags & SVF_MONSTER && !ent->deadflag) ||
					(ent->inuse && IsPlayerDefense(ent)))
				{
					fd->count++;
					return BoxEdictsResult_t::End; // Found an obstacle
				}
				return BoxEdictsResult_t::Skip;
			}, &check_data);

		cache.has_obstacle = (check_data.count > 0);
	}
	else {
		// If occupied by player, we don't need to check for other obstacles separately for the cache.has_obstacle flag,
		// because the player block takes precedence. We can optionally set has_obstacle=true here too,
		// but it doesn't change the outcome as was_occupied_by_player is already true.
		// Let's keep it simple: if player is there, only was_occupied_by_player matters for the return value.
		// The obstacle check only sets cache.has_obstacle if no player was found.
		cache.has_obstacle = false;
	}


	// This function's primary job is to say if a *player* directly blocks the spawn.
	// The 'has_obstacle' flag in the cache is used by SelectRandomSpawnPoint/SpawnMonsters
	// to decide if alternative positions should be tried.
	return cache.was_occupied_by_player;
}

// --- Corrected SelectRandomSpawnPoint ---
template <typename TFilter>
edict_t* SelectRandomSpawnPoint(TFilter filter) {
	edict_t* availableSpawns[MAX_SPAWN_POINTS]{};
	edict_t* occupiedButUsableSpawns[MAX_SPAWN_POINTS]{};
	int availableCount = 0;
	int occupiedCount = 0;

	for (edict_t* spawnPoint : monster_spawn_points()) {
		if (!spawnPoint || !spawnPoint->inuse || !is_valid_vector(spawnPoint->s.origin)) continue;
		const auto& data = spawnPointsData[spawnPoint]; // Get cooldown data
		if (data.isTemporarilyDisabled && level.time < data.cooldownEndsAt) continue; // Check normal cooldown
		if (level.time < data.alternative_cooldown) continue; // Check alternative cooldown

		// Apply the custom filter first
		if (filter(spawnPoint)) {
			// Check if a player is directly blocking the spawn
			if (IsSpawnPointOccupied(spawnPoint)) {
				// Player is blocking, this point is completely unusable for direct spawn.
				// IsSpawnPointOccupied handles cache update.
				// Do NOT add to any list for this function's purpose.
				if (developer->integer > 2) gi.Com_PrintFmt("SelectRandomSpawnPoint: Point {} skipped (player occupied).\n", (int)(spawnPoint - g_edicts));
				continue; // Skip this point entirely
			}
			else {
				// Not blocked by a player. Now check the cache for non-player obstacles.
				const SpawnPointCache& cache = spawn_point_cache[spawnPoint]; // Get cache entry (already updated by IsSpawnPointOccupied call)
				if (cache.has_obstacle) {
					// Blocked by monster/defense - add to the list for potential alternative spawn.
					if (occupiedCount < MAX_SPAWN_POINTS) {
						occupiedButUsableSpawns[occupiedCount++] = spawnPoint;
					}
					if (developer->integer > 2) gi.Com_PrintFmt("SelectRandomSpawnPoint: Point {} added to occupiedButUsable (obstacle present).\n", (int)(spawnPoint - g_edicts));
				}
				else {
					// Not blocked by player AND no obstacle found -> add to available list.
					if (availableCount < MAX_SPAWN_POINTS) {
						availableSpawns[availableCount++] = spawnPoint;
					}
					if (developer->integer > 2) gi.Com_PrintFmt("SelectRandomSpawnPoint: Point {} added to available.\n", (int)(spawnPoint - g_edicts));
				}
			}
		}
	}

	// Prioritize completely available spawns
	if (availableCount > 0) {
		const size_t idx = irandom(availableCount);
		if (developer->integer > 1) gi.Com_PrintFmt("SelectRandomSpawnPoint: Selected available point {}\n", (int)(availableSpawns[idx] - g_edicts));
		return availableSpawns[idx];
	}

	// If no completely available spawns, try one blocked by an obstacle
	if (occupiedCount > 0) {
		const size_t idx = irandom(occupiedCount);
		if (developer->integer > 1) gi.Com_PrintFmt("SelectRandomSpawnPoint: Selected obstacle-occupied point {} (will try alternative).\n", (int)(occupiedButUsableSpawns[idx] - g_edicts));
		return occupiedButUsableSpawns[idx]; // Let SpawnMonsters handle TryAlternative
	}

	if (developer->integer > 1) gi.Com_PrintFmt("SelectRandomSpawnPoint: No suitable spawn points found.\n");
	return nullptr; // No valid spawn points found
}

static void CleanupSpawnPointCache() noexcept { spawn_point_cache.clear(); }

// Monster wave type flags
enum class MonsterWaveType : uint32_t {
	None = 0,
	Flying = 1 << 0,  // Flying units
	Ground = 1 << 1,  // Basic ground units
	Small = 1 << 2,  // Small units (parasite, stalker)
	Light = 1 << 3,  // Light units (soldiers, basic infantry)
	Heavy = 1 << 4,  // Heavy units (tanks, enforcers)
	Medium = 1 << 5,  // Medium units (gladiators, medics)
	Fast = 1 << 6,  // Fast moving units
	SemiBoss = 1 << 7,  // Mini-boss tier units
	Boss = 1 << 8,  // Full boss units
	Ranged = 1 << 9, // Primarily ranged attackers
	Melee = 1 << 10, // Primarily melee attackers
	Special = 1 << 11, // Special units (medics, commanders)
	Elite = 1 << 12,  // Elite variants of basic units
	Gekk = 1 << 13,  // Gekk initial wave?
	Shambler = 1 << 14,  // Shambler boss wave?
	Mutant = 1 << 15, // Mutant boss wave?
	Arachnophobic = 1 << 16,  // Mutant boss wave?
	Berserk = 1 << 17,  // Berserk  wave
	Bomber = 1 << 18,  // Grenade users wave?
	Spawner = 1 << 19  // Spawning reinforcements users wave
};

MonsterWaveType current_wave_type = MonsterWaveType::None;

enum class BossType {
	CARRIER,
	BOSS2,
	BOSS2KL,
	FIXBOTKL,
	CARRIER_MINI,
	TANK_64,
	SHAMBLERKL,
	GUNCMDRKL,
	GUARDIAN,
	PSX_GUARDIAN,
	BOSS5,
	WIDOW,
	WIDOW2,
	JORG,
	MAKRONKL,
	PSX_ARACHNID,
	REDMUTANT,
	OTHER
};

inline bool HasWaveType(MonsterWaveType entityTypes, MonsterWaveType typeToCheck);

// Spawn point selection filter
struct SpawnMonsterFilter {
	gtime_t currentTime;

	SpawnMonsterFilter(gtime_t time) : currentTime(time) {}

	bool operator()(edict_t* spawnPoint) const {
		// Combined validation to reduce branches
		if (!spawnPoint || !spawnPoint->inuse || !is_valid_vector(spawnPoint->s.origin))
			return false;

		// Direct array access for cooldown check
		const auto& data = spawnPointsData[spawnPoint];

		// Check normal cooldown
		if (data.isTemporarilyDisabled && currentTime < data.cooldownEndsAt)
			return false;

		// NEW: Check alternative position cooldown
		if (data.alternative_cooldown > 0_sec && currentTime < data.alternative_cooldown)
			return false;

		// Check if this is a flying spawn point but we need non-flying monsters
		if (spawnPoint->style == 1 && !HasWaveType(current_wave_type, MonsterWaveType::Flying))
			return false;

		// Cache the origin reference to avoid multiple member accesses
		const vec3_t& origin = spawnPoint->s.origin;

		// Check minimum distance from players using modern vector operations
		for (const auto* const player : active_players()) {
			if (!player || !player->inuse)
				continue;

			// Use squared distance to avoid unnecessary sqrt calculations
			if ((origin - player->s.origin).lengthSquared() < 22500.0f) // 150^2 = 22500
				return false;
		}

		return true;
	}
};
// Definir tamaños máximos para arrays estáticos
constexpr size_t MAX_ELIGIBLE_BOSSES = 16;
constexpr size_t MAX_RECENT_BOSSES = 4;

static constexpr size_t NUM_WAVE_SOUNDS = 12;
static constexpr size_t NUM_START_SOUNDS = 8;

//precache//
// Arrays estáticos de cached_soundindex
static cached_soundindex wave_sounds[NUM_WAVE_SOUNDS];
static cached_soundindex start_sounds[NUM_START_SOUNDS];
static cached_soundindex sound_tele3;      // Para teleport
static cached_soundindex sound_tele_up;     // Para teleport escape
static cached_soundindex sound_spawn1;      // Para spawn de monstruos
static cached_soundindex incoming;      // Para spawn de monstruos

// Arrays de strings con los nombres de los sonidos
static constexpr const char* WAVE_SOUND_PATHS[NUM_WAVE_SOUNDS] = {
	"nav_editor/action_fail.wav",
	"nav_editor/clear_test_node.wav",
	"makron/roar1.wav",
	"zortemp/ack.wav",
	"makron/voice3.wav",
	"world/v_fac3.wav",
	"makron/voice4.wav",
	"world/battle2.wav",
	"world/battle3.wav",
	"world/battle4.wav",
	"world/battle5.wav",
	"misc/alarm.wav"
};

static constexpr const char* START_SOUND_PATHS[NUM_START_SOUNDS] = {
	"misc/r_tele3.wav",
	"world/fish.wav",
	"world/klaxon2.wav",
	"misc/tele_up.wav",
	"world/incoming.wav",
	"world/redforceact.wav",
	"makron/voice2.wav",
	"makron/voice.wav"
};

static const char* GetCurrentMapName() noexcept {
	return static_cast<const char*>(level.mapname);
}

enum class WaveEndReason {
	AllMonstersDead,
	MonstersRemaining,
	TimeLimitReached
};

inline int8_t GetNumActivePlayers();
inline int8_t GetNumSpectPlayers();
inline int8_t GetNumHumanPlayers();

int32_t g_adjusted_monster_cap = 0;
constexpr int8_t MAX_MONSTERS_BIG_MAP = 32;
constexpr int8_t MAX_MONSTERS_MEDIUM_MAP = 16;
constexpr int8_t MAX_MONSTERS_SMALL_MAP = 14;

bool allowWaveAdvance = false; // Global variable to control wave advancement

bool boss_spawned_for_wave = false; // to avoid boss spamming

//bool flying_monsters_mode = false;  // flying wave
bool melee_monsters_mode = false;   // For RedMutant waves
bool small_monsters_mode = false;   // For Widow waves

int16_t last_wave_number = 0;              // Reducido de uint64_t
uint16_t g_totalMonstersInWave = 0;         // Reducido de uint32_t

gtime_t horde_message_end_time = 0_sec;
gtime_t SPAWN_POINT_COOLDOWN = 2.8_sec; //spawns Cooldown 

// Function to check and reduce spawn cooldowns when few monsters remain
void CheckAndReduceSpawnCooldowns() {
	// Only proceed if fewer than 7 stroggs remain and not in a boss wave
	const int32_t remaining_stroggs = GetStroggsNum();
	if (remaining_stroggs > 6 || IsBossWave()) {
		return;
	}

	// Track if we found any valid cooldowns to reset
	bool found_cooldowns_to_reset = false;
	const gtime_t current_time = level.time;

	// Pre-compute the reduction factor once
	constexpr float REDUCTION_FACTOR = 0.4f;  // Changed from 0.15f to 0.4f to make late-wave cooldown reduction less aggressive

	// Process all spawn points in use
	// We need to track the spawn points to process
	std::vector<edict_t*> spawn_points;
	spawn_points.reserve(MAX_SPAWN_POINTS);

	// Find all active spawn points by scanning entities
	for (unsigned int i = 1; i < globals.num_edicts; i++) {
		edict_t* ent = &g_edicts[i];
		if (ent && ent->inuse && ent->classname &&
			!strcmp(ent->classname, "info_player_deathmatch")) {
			spawn_points.push_back(ent);
		}
	}

	// Process spawn points with early termination after collecting
	for (edict_t* spawn_point : spawn_points) {
		auto& data = spawnPointsData[spawn_point];

		// Check if spawn point is disabled and cooldown is still active
		if (data.isTemporarilyDisabled && current_time < data.cooldownEndsAt) {
			found_cooldowns_to_reset = true;

			const gtime_t remaining_time = data.cooldownEndsAt - current_time;
			// Calculate reduced duration and ensure it meets the minimum
			const gtime_t reduced_duration = remaining_time * REDUCTION_FACTOR;
			const gtime_t final_duration = std::max(reduced_duration, HordeConstants::MIN_REDUCED_INDIVIDUAL_COOLDOWN);
			data.cooldownEndsAt = current_time + final_duration; // Apply clamped duration

			data.attempts = 0;
		}
	}

	if (found_cooldowns_to_reset) {
		SPAWN_POINT_COOLDOWN *= REDUCTION_FACTOR;
		// Clamp the global cooldown after reduction
		SPAWN_POINT_COOLDOWN = std::max(SPAWN_POINT_COOLDOWN, HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN);

		if (developer->integer > 1) {
			gi.Com_PrintFmt("Global spawn cooldown reduced and clamped to {:.2f}s\n", SPAWN_POINT_COOLDOWN.seconds());
		}
	}
}

static constexpr gtime_t GetBaseSpawnCooldown(bool isSmallMap, bool isBigMap) {
	if (isSmallMap)
		return 0.5_sec;  // Increased from 0.2 to 0.5
	else if (isBigMap)
		return 2.0_sec;  // Increased from 1.4 to 2.0
	else
		return 1.2_sec;  // Increased from 0.8 to 1.2
}

static float CalculateCooldownScale(int32_t lvl, const MapSize& mapSize) {
	// Early return for low levels - improves branch prediction
	if (lvl <= 10) {
		return 1.0f;
	}

	// Cache player count - only compute once
	const int32_t numHumanPlayers = GetNumHumanPlayers();

	// Compute base scale - more gradual linear scaling with level
	float scale = 1.0f + (lvl * 0.015f);  // Changed from 0.02f to 0.015f for more gradual scaling

	// Compute player reduction factor once
	float playerReduction = 0.0f;
	if (numHumanPlayers > 1) {
		constexpr float PLAYER_REDUCTION = 0.1f;
		constexpr float MAX_REDUCTION = 0.45f;
		playerReduction = std::min((numHumanPlayers - 1) * PLAYER_REDUCTION, MAX_REDUCTION);
		scale *= (1.0f - playerReduction);
	}

	// Determine map multipliers with fewer branches
	float multiplier, maxScale;

	if (mapSize.isSmallMap) {
		multiplier = 0.7f;
		maxScale = 1.3f;
	}
	else if (mapSize.isBigMap) {
		multiplier = 0.85f;
		maxScale = 1.75f;
	}
	else { // Medium map
		multiplier = 0.8f;
		maxScale = 1.5f;
	}

	// Apply map multiplier and clamp to max scale
	return std::min(scale * multiplier, maxScale);
}
cvar_t* g_horde;

enum class horde_state_t {
	warmup,
	spawning,
	active_wave,
	cleanup,
	rest
};

// En HordeState, reemplazar el vector con array estático
struct HordeState {
	gtime_t         warm_time = 4_sec;
	horde_state_t   state = horde_state_t::warmup;
	gtime_t         monster_spawn_time;
	int32_t         num_to_spawn = 0;
	int32_t         level = 0;
	int32_t         queued_monsters = 0;
	gtime_t         lastPrintTime = 0_sec;

	bool            conditionTriggered = false;
	gtime_t         conditionStartTime = 0_sec;
	gtime_t         conditionTimeThreshold = 0_sec;
	bool            timeWarningIssued = false;
	gtime_t         waveEndTime = 0_sec;
	bool            warningIssued[4] = { false, false, false, false };

	// Failsafe timeout to prevent getting stuck in a state
	gtime_t         failsafe_timeout = 0_sec;

	gtime_t         last_successful_hud_update = 0_sec;
	uint32_t        failed_updates_count = 0;

	MapSize current_map_size;
	int32_t max_monsters{};  // Cacheado basado en map_size
	gtime_t base_spawn_cooldown;  // Cacheado basado en map_size

	void update_map_size(const char* mapname) {
		current_map_size = GetMapSize(mapname);
		max_monsters = current_map_size.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
			(current_map_size.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP);
		base_spawn_cooldown = GetBaseSpawnCooldown(
			current_map_size.isSmallMap,
			current_map_size.isBigMap
		);
	}

	void reset_hud_state() {
		last_successful_hud_update = 0_sec;
		failed_updates_count = 0;
	}
} g_horde_local;

// Clase de selección genérica usando templates
template <typename T>
struct WeightedSelection {
	static constexpr size_t MAX_ITEMS = 32;

	struct ItemEntry {
		const T* item;
		float weight;
	};

	// Pre-allocate fixed array instead of using dynamic memory
	ItemEntry items[MAX_ITEMS];
	size_t item_count = 0;
	float total_weight = 0.0f;

	void clear() {
		item_count = 0;
		total_weight = 0.0f;
	}

	bool add(const T* item, float weight) {
		if (!item || item_count >= MAX_ITEMS || weight <= 0.0f)
			return false;

		items[item_count] = { item, weight };
		total_weight += weight;
		item_count++;
		return true;
	}

	const T* select() const {
		if (item_count == 0 || total_weight <= 0.0f)
			return nullptr;

		// Generate uniform random value once
		const float random_weight = frandom() * total_weight;

		// Use linear search - more efficient for small arrays than binary search
		float cumulative = 0.0f;
		for (size_t i = 0; i < item_count; i++) {
			cumulative += items[i].weight;
			if (cumulative >= random_weight)
				return items[i].item;
		}

		// Fallback to last item
		return items[item_count - 1].item;
	}

	// More efficient range selection
	const T* select_range(float min_weight, float max_weight) const {
		if (item_count == 0 || total_weight <= 0.0f)
			return nullptr;

		// Stack-allocated array for eligible items to avoid heap allocation
		ItemEntry eligible_items[MAX_ITEMS]{};
		size_t eligible_count = 0;
		float eligible_total = 0.0f;

		// Filter by weight range
		for (size_t i = 0; i < item_count; i++) {
			if (items[i].weight >= min_weight && items[i].weight <= max_weight) {
				eligible_items[eligible_count++] = items[i];
				eligible_total += items[i].weight;
			}
		}

		if (eligible_count == 0)
			return nullptr;

		// Generate random value once
		const float random_weight = frandom() * eligible_total;

		// Linear search through eligible items
		float cumulative = 0.0f;
		for (size_t i = 0; i < eligible_count; i++) {
			cumulative += eligible_items[i].weight;
			if (cumulative >= random_weight)
				return eligible_items[i].item;
		}

		return eligible_items[eligible_count - 1].item;
	}
};
int16_t current_wave_level = g_horde_local.level;
bool next_wave_message_sent = false;
auto auto_spawned_bosses = std::unordered_set<edict_t*>{};

const std::unordered_set<std::string> smallMaps = {
	"q2dm3", "q2dm7", "q2dm2", "q64/dm10", "test/mals_barrier_test",
	"q64/dm9", "q64/dm7", "q64/dm2", "test/spbox",
	"q64/dm1", "fact3", "q2ctf4", "rdm4", "q64/command","mgu3m4",
	"mgu4trial", "mgu6trial", "ec/base_ec", "mgdm1", "ndctf0", "q64/dm6",
	"q64/dm8", "q64/dm4", "q64/dm3", "industry", "e3/jail_e3"
};

const std::unordered_set<std::string> bigMaps = {
	"q2ctf5", "old/kmdm3", "xdm2", "xdm4", "xdm6", "xdm3", "rdm6", "rdm8", "xdm1", "waste2", "rdm5", "rdm9", "rdm12", "xintell", "sewer64", "base64", "city64"
};

MapSize GetMapSize(const char* mapname) {
	static std::unordered_map<std::string, MapSize> cache;

	const auto it = cache.find(mapname);
	if (it != cache.end())
		return it->second;

	MapSize size;
	size.isSmallMap = smallMaps.count(mapname) > 0;
	size.isBigMap = bigMaps.count(mapname) > 0;
	size.isMediumMap = !size.isSmallMap && !size.isBigMap;

	cache[mapname] = size;
	return size;
}

// Función para calcular el bono de locura y caos
static inline int32_t CalculateChaosInsanityBonus(int32_t lvl) noexcept {
	if (g_chaotic->integer) return (lvl <= 3) ? 6 : 3;
	switch (g_insane->integer) {
	case 2: return 16;
	case 1: return 8;
	default: return 0;
	}
}

// Modify the existing ClampNumToSpawn function
inline static void ClampNumToSpawn(const MapSize& mapSize) { // mapSize parameter might no longer be needed here
	// g_adjusted_monster_cap is now calculated in GetAdjustedMonsterCap and includes player bonus
	const int32_t maxAllowed = g_adjusted_monster_cap;

	// Ensure g_adjusted_monster_cap was initialized (fallback if called too early)
	if (maxAllowed <= 0) {
		// Fallback to basic map defaults if the global cap isn't ready
		const int32_t fallbackCap = mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
			(mapSize.isBigMap ? MAX_MONSTERS_BIG_MAP : MAX_MONSTERS_MEDIUM_MAP);
		g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, fallbackCap);
		if (developer->integer) {
			gi.Com_PrintFmt("ClampNumToSpawn: WARN - g_adjusted_monster_cap not ready, used fallback {}\n", fallbackCap);
		}
	}
	else {
		// Clamp using the globally calculated value
		g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, maxAllowed);
		if (developer->integer > 1) {
			gi.Com_PrintFmt("ClampNumToSpawn: Clamping num_to_spawn to {} (g_adjusted_monster_cap)\n", maxAllowed);
		}
	}
}

static int32_t CalculateQueuedMonsters(const MapSize& mapSize, int32_t lvl, bool isHardMode) noexcept {
	if (lvl <= 3)
		return 0;

	// Base más agresiva con mejor cálculo matemático
	float baseQueued = std::sqrt(static_cast<float>(lvl)) * 3.0f;
	baseQueued *= (1.0f + (lvl) * 0.18f);

	// Multiplicadores optimizados por tamaño de mapa
	const float mapSizeMultiplier = mapSize.isSmallMap ? 1.3f :
		mapSize.isBigMap ? 1.5f : 1.4f;

	const int32_t maxQueued = mapSize.isSmallMap ? 30 :
		mapSize.isBigMap ? 45 : 35;

	baseQueued *= mapSizeMultiplier;

	// Bonus exponencial mejorado para niveles altos
	if (lvl > 20) {
		baseQueued *= std::pow(1.15f, std::min(lvl - 20, 18));
	}

	// Ajuste de dificultad mejorado
	if (isHardMode) {
		float difficultyMultiplier = 1.25f;
		if (lvl > 25) {
			difficultyMultiplier += (lvl - 25) * 0.025f;
			difficultyMultiplier = std::min(difficultyMultiplier, 1.75f);
		}
		baseQueued *= difficultyMultiplier;
	}

	// Factor de reducción final
	baseQueued *= 0.85f;

	return std::min(static_cast<int32_t>(baseQueued), maxQueued);
}

// Cache for common calculations in UnifiedAdjustSpawnRate
struct WaveScalingCache {
	// Lookup tables for frequent calculations
	static constexpr int32_t MAX_WAVE_LEVEL = 250;
	static constexpr int32_t MAX_HUMAN_PLAYERS = 16;

	// Pre-computed cooldown scales to avoid recalculation
	float cooldownScales[3][MAX_WAVE_LEVEL + 1] = {}; // [mapType][level]

	// Cached base counts
	int32_t baseCountsByLevel[3][MAX_WAVE_LEVEL + 1] = {}; // [mapType][level]
	// Removed: int32_t additionalSpawnsByLevel[MAX_WAVE_LEVEL+1] = {}; // Not needed, calculation moved

	// Player multipliers (precomputed)
	float playerMultipliers[MAX_HUMAN_PLAYERS + 1] = {};

	// Initialize all cache tables
	void initialize() {
		using namespace HordeConstants;

		// Initialize player multipliers
		for (int32_t players = 0; players <= MAX_HUMAN_PLAYERS; ++players) {
			playerMultipliers[players] = (players <= 1) ?
				1.0f : BASE_DIFFICULTY_MULTIPLIER + ((players - 1) * PLAYER_COUNT_SCALE);
		}

		// Initialize base counts by map type and level
		for (int mapType = 0; mapType < 3; ++mapType) {
			for (int32_t level = 0; level <= MAX_WAVE_LEVEL; ++level) {
				// Select the appropriate base count based on level ranges
				int32_t countIndex;
				if (level <= 5) countIndex = 0;
				else if (level <= 10) countIndex = 1;
				else if (level <= 15) countIndex = 2;
				else countIndex = 3; // Levels > 15

				// Store pre-computed base count
				baseCountsByLevel[mapType][level] = BASE_COUNTS[mapType][countIndex];
			}
		}

		// Removed initialization for additionalSpawnsByLevel as it's calculated directly
		// in UnifiedAdjustSpawnRate now, eliminating the unused variables.

		// Initialize cooldown scales (can be accessed by mapType and level)
		MapSize smallMap = { true, false, false };
		MapSize mediumMap = { false, false, true }; // Corrected: Medium map should have isMediumMap true
		MapSize bigMap = { false, true, false };

		for (int32_t level = 0; level <= MAX_WAVE_LEVEL; ++level) {
			cooldownScales[0][level] = CalculateCooldownScale(level, smallMap);  // Index 0 = Small
			cooldownScales[1][level] = CalculateCooldownScale(level, mediumMap); // Index 1 = Medium
			cooldownScales[2][level] = CalculateCooldownScale(level, bigMap);    // Index 2 = Big
		}
	}
} g_waveScalingCache;
void UnifiedAdjustSpawnRate(const MapSize& mapSize, int32_t lvl, int32_t humanPlayers) noexcept {
	using namespace HordeConstants;

	// Initialize cache if needed (one-time operation)
	static bool cache_initialized = false;
	if (!cache_initialized) {
		g_waveScalingCache.initialize();
		cache_initialized = true;
	}

	// Clamp input values for safety
	const int32_t safeLevel = std::min(lvl, WaveScalingCache::MAX_WAVE_LEVEL);
	const int32_t safePlayerCount = std::min(humanPlayers, WaveScalingCache::MAX_HUMAN_PLAYERS);

	// Determine map type index for lookups
	const int mapTypeIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);

	// Get base values from cache
	int32_t baseCount = g_waveScalingCache.baseCountsByLevel[mapTypeIndex][safeLevel];

	// Apply player multiplier using cached value
	if (safePlayerCount > 1) {
		const float playerMultiplier = g_waveScalingCache.playerMultipliers[safePlayerCount];
		baseCount = static_cast<int32_t>(baseCount * playerMultiplier);
	}

	// --- CORRECTED SECTION ---
	// Calculate additional spawn count directly, without using the removed cache array
	int32_t additionalSpawn;
	if (safeLevel < 8) {
		additionalSpawn = 6; // Default for early levels
	}
	else {
		// Apply map-specific base value from constants
		additionalSpawn = mapSize.isSmallMap ? ADDITIONAL_SPAWNS[0] :
			mapSize.isBigMap ? ADDITIONAL_SPAWNS[2] : ADDITIONAL_SPAWNS[1];

		// Level-based adjustment for high levels
		if (safeLevel > 25) {
			additionalSpawn = static_cast<int32_t>(additionalSpawn * 1.6f);
		}
	}
	// --- END CORRECTED SECTION ---

	// Apply difficulty adjustments (Chaos/Insanity bonus)
	if (safeLevel >= 3 && (g_chaotic->integer || g_insane->integer)) {
		additionalSpawn += CalculateChaosInsanityBonus(safeLevel);
	}

	// Get cooldown from cache and apply adjustments
	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);
	const float cooldownScale = g_waveScalingCache.cooldownScales[mapTypeIndex][safeLevel];
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	// Apply difficulty-based cooldown adjustments
	if (g_chaotic->integer || g_insane->integer) {
		SPAWN_POINT_COOLDOWN *= HordeConstants::TIME_REDUCTION_MULTIPLIER;
	}
	else {
		// Normal difficulty adjustment
		SPAWN_POINT_COOLDOWN *= 1.2f;
	}

	// Apply periodic difficulty scaling (every 3 levels)
	if (safeLevel > 0 && safeLevel % 3 == 0) { // Added check for safeLevel > 0
		const float difficultyMultiplier = g_waveScalingCache.playerMultipliers[safePlayerCount];
		baseCount = static_cast<int32_t>(baseCount * difficultyMultiplier);

		const float cooldownReduction = (mapSize.isBigMap ? 0.1f : 0.15f) * difficultyMultiplier;
		SPAWN_POINT_COOLDOWN = std::max(
			SPAWN_POINT_COOLDOWN - gtime_t::from_sec(cooldownReduction),
			1.0_sec // Ensure cooldown doesn't go below 1.0 second
		);
	}

	// Final cooldown clamping
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN,
		HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN, // Use the constant
		3.5_sec); // Keep upper bound or define a MAX constant
	// Calculate final spawn count
	g_horde_local.num_to_spawn = baseCount + additionalSpawn;
	ClampNumToSpawn(mapSize); // Handle clamping

	// Calculate queued monsters
	const bool isHardMode = g_insane->integer || g_chaotic->integer;
	g_horde_local.queued_monsters = CalculateQueuedMonsters(mapSize, safeLevel, isHardMode);

	// Debug output
	if (developer->integer == 3) {
		gi.Com_PrintFmt("DEBUG: Wave {} settings:\n", safeLevel);
		gi.Com_PrintFmt("  - Spawn cooldown: {:.2f}s (Scale {:.2f}x)\n",
			SPAWN_POINT_COOLDOWN.seconds(), cooldownScale);
		gi.Com_PrintFmt("  - Base monsters: {}\n", baseCount);
		gi.Com_PrintFmt("  - Additional spawns: {}\n", additionalSpawn);
		gi.Com_PrintFmt("  - Queued monsters: {}\n", g_horde_local.queued_monsters);
		gi.Com_PrintFmt("  - Map type: {}\n",
			mapSize.isBigMap ? "big" : (mapSize.isSmallMap ? "small" : "medium"));
	}
}

void ResetAllSpawnAttempts() noexcept;
void VerifyAndAdjustBots();
void ResetCooldowns() noexcept;

struct ConditionParams {
	int32_t maxMonsters;
	gtime_t timeThreshold;
	gtime_t lowPercentageTimeThreshold;
	gtime_t independentTimeThreshold;
	float lowPercentageThreshold;
	float aggressiveTimeReductionThreshold;

	ConditionParams() noexcept :
		maxMonsters(0),
		timeThreshold(0_sec),
		lowPercentageTimeThreshold(0_sec),
		independentTimeThreshold(0_sec),
		lowPercentageThreshold(0.3f),
		aggressiveTimeReductionThreshold(0.3f) {
	}
};

// Constants and helper functions
// constexpr gtime_t BASE_MAX_WAVE_TIME = 85_sec; // Original
constexpr gtime_t BASE_MAX_WAVE_TIME = 95_sec;    // Increased base time
// constexpr gtime_t TIME_INCREASE_PER_LEVEL = 1.5_sec; // Original
constexpr gtime_t TIME_INCREASE_PER_LEVEL = 1.7_sec; // Slightly faster increase per level
constexpr gtime_t BOSS_TIME_BONUS = 60_sec;          // Keep boss bonus the same
constexpr int MONSTERS_FOR_AGGRESSIVE_REDUCTION = 5; // Keep this threshold constant for now

static constexpr gtime_t calculate_max_wave_time(int32_t wave_level) {
	// Calculate the time base based on level
	gtime_t base_time = BASE_MAX_WAVE_TIME + TIME_INCREASE_PER_LEVEL * wave_level;

	// Limit base time to 220 seconds (Increased from 200)
	base_time = (base_time <= 220_sec) ? base_time : 220_sec;

	// Add extra time if it's a boss wave (levels multiples of 5 after 10)
	if (wave_level >= 10 && wave_level % 5 == 0) {
		base_time += BOSS_TIME_BONUS;
	}

	return base_time;
}

// Variables globales
static gtime_t g_independent_timer_start;
static ConditionParams g_lastParams;
static int32_t g_lastWaveNumber = -1;
static int32_t g_lastNumHumanPlayers = -1;
static bool g_maxMonstersReached = false;
static bool g_lowPercentageTriggered = false;

static ConditionParams GetConditionParams(const MapSize& mapSize, int32_t numHumanPlayers, int32_t lvl) {
	ConditionParams params;

	// Initial validation
	if (g_horde_local.level < 0 || lvl < 0) {
		return params; // Returns default safe parameters
	}

	auto configureMapParams = [&](ConditionParams& params) {
		if (mapSize.isBigMap) {
			params.maxMonsters = (numHumanPlayers >= 3) ? 26 : 22;
			// params.timeThreshold = random_time(17_sec, 22_sec); // Original
			params.timeThreshold = random_time(20_sec, 26_sec); // Increased
		}
		else if (mapSize.isSmallMap) {
			params.maxMonsters = (numHumanPlayers >= 3) ? 12 : 9;
			// params.timeThreshold = random_time(14_sec, 20_sec); // Original
			params.timeThreshold = random_time(18_sec, 25_sec); // Increased more for small maps
		}
		else { // Medium maps
			params.maxMonsters = (numHumanPlayers >= 3) ? 17 : 13;
			// params.timeThreshold = random_time(18_sec, 25_sec); // Original
			params.timeThreshold = random_time(20_sec, 27_sec); // Increased
		}
		};

	configureMapParams(params);

	// Progressive adjustment based on level - more aggressive (Keep this part)
	params.maxMonsters += std::min(lvl / 4, 8);
	params.timeThreshold += gtime_t::from_ms(75ll * std::min(lvl / 3, 4));

	// Early wave time REDUCTION (Keep this logic, multiplier is fine)
	float timeMultiplier = 1.0f;
	if (lvl <= 10) {
		timeMultiplier = 1.0f - (0.25f * (static_cast<float>(lvl - 1) / 9.0f));
		timeMultiplier = std::max(0.1f, timeMultiplier);
	}

	// High level adjustment (Keep this part)
	if (lvl > 10) {
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * 1.2f);
		params.timeThreshold += 0.15_sec;
	}

	// Difficulty adjustment (Keep this part)
	if (g_chaotic->integer || g_insane->integer) {
		if (numHumanPlayers <= 3) {
			// params.timeThreshold += random_time(5_sec, 8_sec); // Original
			params.timeThreshold += random_time(6_sec, 10_sec); // Slightly increased bonus time
		}
		params.maxMonsters = static_cast<int32_t>(params.maxMonsters * 1.1f);
	}

	// Configuration for low percentage of remaining monsters
	// params.lowPercentageTimeThreshold = random_time(8_sec, 17_sec); // Original
	params.lowPercentageTimeThreshold = random_time(10_sec, 20_sec); // Increased
	params.lowPercentageThreshold = 0.3f; // Keep threshold

	// Configuration for independent time based on level (uses the updated calculate_max_wave_time)
	params.independentTimeThreshold = calculate_max_wave_time(lvl);

	// Apply multiplier to time thresholds for early waves (Keep this part)
	if (lvl <= 10) {
		params.timeThreshold = gtime_t::from_sec(params.timeThreshold.seconds() * timeMultiplier);
		params.lowPercentageTimeThreshold = gtime_t::from_sec(params.lowPercentageTimeThreshold.seconds() * timeMultiplier);
		params.independentTimeThreshold = gtime_t::from_sec(params.independentTimeThreshold.seconds() * timeMultiplier);
	}

	// Final parameter validation (Keep this part)
	params.maxMonsters = std::max(1, params.maxMonsters);
	params.timeThreshold = std::max(1_sec, params.timeThreshold);
	params.lowPercentageTimeThreshold = std::max(1_sec, params.lowPercentageTimeThreshold);
	params.independentTimeThreshold = std::max(1_sec, params.independentTimeThreshold);

	return params;
}

// Warning times in seconds
constexpr std::array<float, 3> WARNING_TIMES = { 30.0f, 10.0f, 5.0f };


inline int32_t GetAdjustedMonsterCap(const MapSize& mapSize, int32_t waveLevel);

void ResetChampionMonsterState() {
	champion_spawned_this_wave = false;
	champion_spawn_cooldown = 0;
}

void InitializeWaveType(int32_t waveLevel);

static void Horde_InitLevel(const int32_t lvl) {

	ResetChampionMonsterState();

	// Only initialize wave type for non-boss waves
	if (!(lvl >= 10 && lvl % 5 == 0)) {
		InitializeWaveType(lvl);
	}
	else {
		current_wave_type = MonsterWaveType::None;  // Reset for boss waves
	}

	g_horde_local.update_map_size(GetCurrentMapName());

	// ***** CALL GetAdjustedMonsterCap HERE *****
	// This calculates the cap including the player bonus and stores it globally
	GetAdjustedMonsterCap(g_horde_local.current_map_size, lvl);

	// Reset independent timer for this wave - this is critical for correct wave timing
	g_independent_timer_start = level.time;

	// Configuración de variables iniciales para el nivel
	// Verify that we're not somehow about to overflow
	if (g_horde_local.num_to_spawn > std::numeric_limits<uint16_t>::max())
		g_horde_local.num_to_spawn = std::numeric_limits<uint16_t>::max();

	g_totalMonstersInWave = static_cast<uint16_t>(g_horde_local.num_to_spawn);

	last_wave_number++;
	g_horde_local.level = lvl;
	current_wave_level = lvl;
	boss_spawned_for_wave = false;
	next_wave_message_sent = false;

	CleanupSpawnPointCache();
	VerifyAndAdjustBots();

	// Configurar tiempos iniciales - reset all timing variables
	g_independent_timer_start = level.time;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTriggered = false;
	g_horde_local.waveEndTime = 0_sec;
	g_horde_local.lastPrintTime = 0_sec;

	// Calculate fresh parameters for this wave - this fixes using stale parameters
	g_lastParams = GetConditionParams(g_horde_local.current_map_size, GetNumHumanPlayers(), lvl);

	// Reset warning flags for time warnings
	for (size_t i = 0; i < 4; i++) {
		g_horde_local.warningIssued[i] = false;
	}

	if (developer->integer) {
		gi.Com_PrintFmt("Debug: Wave {} init - Timer threshold: {:.2f}s\n",
			lvl, g_lastParams.timeThreshold.seconds());
	}

	// Ajustar la escala de daño según el nivel
	// ... (damage scale switch remains the same) ...

	// ***** UnifiedAdjustSpawnRate is called AFTER GetAdjustedMonsterCap *****
	// It calculates the *initial* number of monsters and calls ClampNumToSpawn,
	// which now uses the correct player-adjusted cap.
	UnifiedAdjustSpawnRate(g_horde_local.current_map_size, lvl, GetNumHumanPlayers());

	CheckAndApplyBenefit(lvl);
	ResetAllSpawnAttempts();
	ResetCooldowns();
	Horde_CleanBodies();

	//gi.Com_PrintFmt("PRINT: Horde level initialized: {}\n", lvl);
}
bool G_IsDeathmatch() noexcept {
	return deathmatch->integer;
}

bool G_IsCooperative() noexcept {
	return coop->integer && !g_horde->integer;
}

struct weighted_item_t;
using weight_adjust_func_t = void(*)(const weighted_item_t& item, float& weight);


// Define the function pointer type (keep this)
struct weighted_item_t;
using weight_adjust_func_t = void(*)(const weighted_item_t& item, float& weight);


// Structure definition (as you provided)
struct HordeItemInfo {
	item_id_t id;      // Use item_id_t instead of const char*
	float weight;      // Relative probability (higher is more common)
	int minWave;       // Minimum wave level for this item to appear

	constexpr HordeItemInfo(item_id_t item_id, float w, int mw)
		: id(item_id), weight(w), minWave(mw) {
	}
};

// The comprehensive static array
static const HordeItemInfo hordeItemData[] = {
	// --- Armor ---
	{ IT_ARMOR_SHARD,         1.5f,  1 }, // Common, small boost
	{ IT_ARMOR_JACKET,        0.8f,  3 }, // Basic armor
	{ IT_ARMOR_COMBAT,        0.5f,  8 }, // Better armor
	{ IT_ARMOR_BODY,          0.2f, 15 }, // Best standard armor
	{ IT_ITEM_POWER_SCREEN,   0.3f,  7 }, // Power Armor (early)
	{ IT_ITEM_POWER_SHIELD,   0.01f, 19 }, // Power Armor (late)

	// --- Weapons ---
	// Blaster is skipped (always have)
	{ IT_WEAPON_CHAINFIST,    0.1f,  1 }, // Melee, niche drop
	{ IT_WEAPON_SHOTGUN,      1.0f,  1 }, // Basic weapon
	{ IT_WEAPON_SSHOTGUN,     0.7f,  3 }, // Strong early/mid weapon
	{ IT_WEAPON_MACHINEGUN,   1.0f,  1 }, // Basic weapon
	{ IT_WEAPON_ETF_RIFLE,    0.6f,  5 }, // Rogue weapon (mid)
	{ IT_WEAPON_CHAINGUN,     0.5f,  7 }, // Mid-tier DPS
	{ IT_WEAPON_GLAUNCHER,    0.5f,  5 }, // Area denial (mid)
	{ IT_WEAPON_PROXLAUNCHER, 0.3f,  8 }, // Rogue weapon (mid/late trap)
	{ IT_WEAPON_RLAUNCHER,    0.4f,  7 }, // Powerful splash (mid/late)
	{ IT_WEAPON_HYPERBLASTER, 0.4f,  9 }, // Energy weapon (mid/late)
	{ IT_WEAPON_IONRIPPER,    0.4f, 10 }, // Xatrix weapon (mid/late)
	{ IT_WEAPON_PLASMABEAM,   0.2f, 12 }, // Rogue weapon (late, high DPS)
	{ IT_WEAPON_RAILGUN,      0.25f, 15 }, // High skill/damage (late)
	{ IT_WEAPON_PHALANX,      0.25f, 14 }, // Xatrix weapon (late)
	{ IT_WEAPON_BFG,          0.05f, 20 }, // Ultimate weapon (very late, rare)
	{ IT_WEAPON_DISRUPTOR,    0.15f, 18 }, // Rogue weapon (late, piercing)
	// Grenades/Traps/Tesla as 'weapons' (for direct drop maybe?)
	{ IT_AMMO_GRENADES,       0.4f,  2 }, // Hand grenades item
	{ IT_AMMO_TRAP,           0.2f,  6 }, // Xatrix trap item
	{ IT_AMMO_TESLA,          0.15f, 8 }, // Rogue tesla item

	// --- Ammo --- (Generally higher weight than corresponding weapons)
	{ IT_AMMO_SHELLS,         2.0f,  1 }, // Common
	{ IT_AMMO_BULLETS,        2.0f,  1 }, // Common
	{ IT_AMMO_GRENADES,       1.2f,  2 }, // Mid frequency
	{ IT_AMMO_ROCKETS,        1.0f,  6 }, // Needed for RL
	{ IT_AMMO_CELLS,          1.5f,  7 }, // Needed for energy weapons
	{ IT_AMMO_SLUGS,          0.8f, 14 }, // Needed for Railgun
	{ IT_AMMO_MAGSLUG,        0.8f, 13 }, // Needed for Phalanx (Xatrix)
	{ IT_AMMO_FLECHETTES,     1.5f,  4 }, // Needed for ETF Rifle (Rogue)
	{ IT_AMMO_PROX,           0.6f,  7 }, // Needed for Prox Launcher (Rogue)
	{ IT_AMMO_ROUNDS,         0.5f, 17 }, // Needed for Disruptor (Rogue)
	{ IT_AMMO_TRAP,           0.4f,  5 }, // Needed for Trap (Xatrix)
	{ IT_AMMO_TESLA,          0.3f,  7 }, // Needed for Tesla (Rogue)
	{ IT_AMMO_NUKE,           0.02f, 25 }, // Very rare ammo/powerup (Rogue)

	// --- Powerups ---
	{ IT_ITEM_QUAD,           0.1f,  8 }, // Rare, powerful
	{ IT_ITEM_QUADFIRE,       0.1f,  6 }, // Rare, powerful (Xatrix)
	{ IT_ITEM_INVULNERABILITY,0.05f, 12 }, // Very rare, very powerful
	{ IT_ITEM_INVISIBILITY,   0.2f,  5 }, // Situational
	{ IT_ITEM_SILENCER,       0.15f, 4 }, // Situational
	//{ IT_ITEM_REBREATHER,     0.1f,  1 }, // Map dependent, low weight
	//{ IT_ITEM_ENVIROSUIT,     0.1f,  1 }, // Map dependent, low weight
	{ IT_ITEM_ADRENALINE,     0.3f,  3 }, // Permanent health boost, good reward
	{ IT_ITEM_BANDOLIER,      0.5f,  4 }, // Good ammo capacity boost
	{ IT_ITEM_PACK,           0.25f, 10 }, // Excellent ammo capacity boost
	{ IT_ITEM_IR_GOGGLES,     0.15f, 7 }, // Situational (Rogue)
	{ IT_ITEM_DOUBLE,         0.15f, 5 }, // Damage boost (Rogue)
	//{ IT_ITEM_SPHERE_VENGEANCE, 0.1f, 15 }, // Powerful sphere (Rogue)
	{ IT_ITEM_SPHERE_HUNTER,  0.1f, 12 }, // Powerful sphere (Rogue)
	{ IT_ITEM_SPHERE_DEFENDER,0.1f,  8 }, // Powerful sphere (Rogue)
	{ IT_ITEM_DOPPELGANGER,   0.15f, 6 }, // Utility/Distraction (Rogue)
	{ IT_ITEM_TELEPORT,       0.2f,  3 }, // Utility Escape
	{ IT_ITEM_SENTRYGUN,      0.3f,  5 }, // Deployable Defense

	// --- Health ---
	{ IT_HEALTH_SMALL,        1.8f,  1 }, // Very common
	{ IT_HEALTH_MEDIUM,       1.5f,  1 }, // Common
	{ IT_HEALTH_LARGE,        0.8f,  3 }, // Less common
	{ IT_HEALTH_MEGA,         0.1f, 10 }, // Rare, powerful boost

	// --- Legacy Heads (Optional - Minor permanent boost) ---
	//{ IT_ITEM_ANCIENT_HEAD,   0.05f, 10 },
	//{ IT_ITEM_LEGACY_HEAD,    0.05f, 10 },

	// End of list marker (optional, but good practice if needed)
	// { IT_NULL, 0.0f, 0 }
};

// Allow flag operations on MonsterWaveType
template<typename E>
constexpr auto to_underlying(E e) noexcept {
	return static_cast<std::underlying_type_t<E>>(e);
}

constexpr MonsterWaveType operator|(MonsterWaveType a, MonsterWaveType b) {
	return static_cast<MonsterWaveType>(
		to_underlying(a) | to_underlying(b)
		);
}

inline MonsterWaveType operator&(MonsterWaveType a, MonsterWaveType b) noexcept {
	return static_cast<MonsterWaveType>(
		to_underlying(a) & to_underlying(b)
		);
}

inline MonsterWaveType& operator|=(MonsterWaveType& a, MonsterWaveType b) {
	a = a | b;
	return a;
}

// Helper function to check if a monster has a specific wave type
inline bool HasWaveType(MonsterWaveType entityTypes, MonsterWaveType typeToCheck) {
	return (entityTypes & typeToCheck) != MonsterWaveType::None;
}

// First, add these at the top with other global variables
static constexpr size_t WAVE_MEMORY_SIZE = 3;  // Remember last 3 waves
static std::array<MonsterWaveType, WAVE_MEMORY_SIZE> previous_wave_types = {};
static size_t wave_memory_index = 0;


// Helper function to check if a wave type was recently used
static bool WasRecentlyUsed(MonsterWaveType wave_type) noexcept {
	for (const auto& prev_type : previous_wave_types) {
		if (prev_type == wave_type) {
			return true;
		}
	}
	return false;
}

// Helper function to store wave type in memory
static void StoreWaveType(MonsterWaveType wave_type) noexcept {
	previous_wave_types[wave_memory_index] = wave_type;
	wave_memory_index = (wave_memory_index + 1) % WAVE_MEMORY_SIZE;
}

// Helper function to try setting a wave type with validation
static bool TrySetWaveType(MonsterWaveType new_type) {
	// Special case for boss waves - allow flying waves to override the recent wave restriction
	if (HasWaveType(new_type, MonsterWaveType::Flying | MonsterWaveType::Boss)) {
		// If this is a flying boss, we always allow flying waves
		current_wave_type = new_type;
		StoreWaveType(new_type);
		return true;
	}

	// Make flying waves more restrictive for non-boss waves
	if (HasWaveType(new_type, MonsterWaveType::Flying) && !HasWaveType(new_type, MonsterWaveType::Boss)) {
		// Check if any of the recent waves were flying
		for (const auto& prev_type : previous_wave_types) {
			if (HasWaveType(prev_type, MonsterWaveType::Flying)) {
				return false;  // Don't allow flying waves if we had one recently
			}
		}

		// Also check if the previous wave was a boss wave
		if (g_horde_local.level > 0 && (g_horde_local.level - 1) >= 10 &&
			(g_horde_local.level - 1) % 5 == 0) {
			return false;  // Don't allow flying waves right after boss waves
		}
	}

	// Check if the wave type was recently used
	if (!WasRecentlyUsed(new_type)) {
		current_wave_type = new_type;
		StoreWaveType(new_type);
		return true;
	}

	// Fallback for specific wave types
	if (HasWaveType(new_type, MonsterWaveType::Mutant) || HasWaveType(new_type, MonsterWaveType::Shambler)) {
		// Fallback for mutant/shambler types
		current_wave_type = MonsterWaveType::Medium;
		StoreWaveType(MonsterWaveType::Medium);
		return true;
	}
	else if (HasWaveType(new_type, MonsterWaveType::Flying)) {
		// Fallback for flying types
		current_wave_type = MonsterWaveType::Flying | MonsterWaveType::Medium;
		StoreWaveType(current_wave_type);
		return true;
	}
	else if (HasWaveType(new_type, MonsterWaveType::Arachnophobic)) {
		// Fallback for arachnophobic types
		current_wave_type = MonsterWaveType::Small | MonsterWaveType::Arachnophobic;
		StoreWaveType(current_wave_type);
		return true;
	}

	return false;
}

// Helper function to check if a wave type is a special wave
static bool IsSpecialWaveType(MonsterWaveType type) noexcept {
	return HasWaveType(type, MonsterWaveType::Gekk) ||
		HasWaveType(type, MonsterWaveType::Berserk) ||
		HasWaveType(type, MonsterWaveType::Mutant) ||
		HasWaveType(type, MonsterWaveType::Spawner) ||
		HasWaveType(type, MonsterWaveType::Bomber) ||
		HasWaveType(type, MonsterWaveType::Heavy) ||
		HasWaveType(type, MonsterWaveType::Shambler) ||
		HasWaveType(type, MonsterWaveType::Arachnophobic);
}

// check if the previous wave was a special wave
static bool WasLastWaveSpecial() noexcept {
	if (previous_wave_types.empty()) {
		return false;
	}

	// Get the most recent wave type (the one right before the current index)
	size_t last_index = (wave_memory_index == 0) ?
		WAVE_MEMORY_SIZE - 1 :
		wave_memory_index - 1;

	return IsSpecialWaveType(previous_wave_types[last_index]);
}

// Structure for an optional component to add to a wave
struct WaveOptionalComponent {
	MonsterWaveType type = MonsterWaveType::None; // The type to potentially add
	float chance = 0.0f;           // Probability (0.0 to 1.0) of adding this type
	// int min_wave = 1;           // Optional: Could add min wave *within the range*
};

// Structure defining the composition rules up to a certain wave
struct WaveDefinition {
	int max_wave;                       // This definition applies for waves <= max_wave
	MonsterWaveType base_type;          // Guaranteed monster types for this range
	std::vector<WaveOptionalComponent> optionals; // List of potential additions
};

// Define the wave progression using the new structure
// IMPORTANT: Keep this sorted by max_wave ascending!
static const std::vector<WaveDefinition> wave_definitions = {
	// Waves 1-5: Basic ground light units
	{ 5, MonsterWaveType::Light | MonsterWaveType::Ground, {
		// No optionals in the first 5 waves initially
   }},
   // Waves 6-10: Add chance for Small units and a small chance for Flying
   { 10, MonsterWaveType::Light | MonsterWaveType::Ground, {
		{MonsterWaveType::Small, 0.45f}, // Slightly increased chance
		{MonsterWaveType::Flying, 0.20f} // Chance for flyers starts
   }},
	// Waves 11-15: Introduce Medium units, increase Special and Flying chance
	{ 15, MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ground, {
		 {MonsterWaveType::Special, 0.65f}, // Higher chance for Medics etc.
		 {MonsterWaveType::Flying, 0.25f},
		 {MonsterWaveType::Small, 0.2f} // Keep small units occasionally relevant
	}},
	// Waves 16-20: Introduce Heavy units, increase Fast and Flying chance
	{ 20, MonsterWaveType::Medium | MonsterWaveType::Heavy | MonsterWaveType::Ground, {
		 {MonsterWaveType::Fast, 0.55f},
		 {MonsterWaveType::Flying, 0.30f},
		 {MonsterWaveType::Special, 0.3f} // Special units continue
	}},
	// Waves 21-35: Introduce Bomber units, increase Fast and Flying chance
	{ 25, MonsterWaveType::Bomber | MonsterWaveType::Heavy | MonsterWaveType::Ground, {
		 {MonsterWaveType::Bomber, 0.55f},
		 {MonsterWaveType::Flying, 0.30f},
		 {MonsterWaveType::Special, 0.3f} // Special units continue
	}},
	// Waves 36-40: Focus on Heavy/Elite, high chance for Fast/Special, moderate Flying
	{ 35, MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Ground, { // Added Ground base
		 {MonsterWaveType::Special | MonsterWaveType::Fast, 0.75f}, // Combined for variety
		 {MonsterWaveType::Flying, 0.30f},
		 {MonsterWaveType::Medium, 0.28f}
	}},
	// Waves 41+: Add SemiBoss chance, keep others relevant
	{ 40, MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Special | MonsterWaveType::Ground, { // Added Ground base
		 {MonsterWaveType::SemiBoss, 0.45f},
		 {MonsterWaveType::Flying, 0.35f},
		 {MonsterWaveType::Fast | MonsterWaveType::Bomber, 0.35f },
		 { MonsterWaveType::Medium, 0.30f }
	}},
	// Waves 41+: High chance for SemiBoss, Flying, Fast
	// Use a large number for max_wave to act as a catch-all for all subsequent waves
	{ 999, MonsterWaveType::Elite | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Ground, { // Base types
		 {MonsterWaveType::SemiBoss, 0.35f},
		 {MonsterWaveType::Flying, 0.40f},
		 {MonsterWaveType::Bomber | MonsterWaveType::Spawner, 0.6f}
	}}
};

// Updated function to get the wave type
inline MonsterWaveType GetWaveComposition(int waveNumber, bool forceSpecialWave = false) {
	const int32_t numHumanPlayers = GetNumHumanPlayers();
	MonsterWaveType selected_type = MonsterWaveType::None;

	// Special waves check - removed the wave 5-9 restriction
	struct SpecialWave {
		MonsterWaveType type;
		float chance;
		int min_wave;
		int max_wave;
		const char* message;
	};

	const SpecialWave special_waves[] = {
		// Early game special waves (waves 5-15)
		{MonsterWaveType::Gekk, (numHumanPlayers <= 2 ? 0.35f : 0.20f), 5, 7, "*** Gekk invasion incoming! ***\n"},
		{MonsterWaveType::Mutant | MonsterWaveType::Melee, 0.30f, 8, 25, "*** Enraged Horde approaching! ***\n"},
		{MonsterWaveType::Flying | MonsterWaveType::Fast, 0.2f, 9, -1, "*** Aerial assault incoming! ***\n"},

		// Mid game special waves (waves 8+)
		{MonsterWaveType::Berserk, 0.2f, 8, 12, "*** Berserkers incoming! ***\n"},
		{MonsterWaveType::Bomber, 0.35f, 10, -1, "*** Strogg Bomber Units Arrived! ***\n"},

		// Late game special waves
		{MonsterWaveType::Heavy, 0.2f, 12, -1, "*** Heavy Armored Units incoming! ***\n"},
		{MonsterWaveType::Spawner, 0.75f, 25, -1, "*** Spawners Deployed! ***\n"}
	};


	if (!forceSpecialWave && !WasLastWaveSpecial()) {
		for (const auto& wave : special_waves) {
			if (waveNumber >= wave.min_wave &&
				(wave.max_wave == -1 || waveNumber <= wave.max_wave) &&
				!WasRecentlyUsed(wave.type) &&
				frandom() < wave.chance)
			{
				selected_type = wave.type;
				gi.LocBroadcast_Print(PRINT_HIGH, wave.message);
				StoreWaveType(selected_type); // Store special wave type
				return selected_type;          // Return immediately
			}
		}
	}
	// --- End Special Wave Logic ---


	// --- Regular Wave Composition using the new structure ---

	// Find the first definition where waveNumber <= max_wave
	// This works because the vector is sorted by max_wave
	auto it = std::find_if(wave_definitions.begin(), wave_definitions.end(),
		[waveNumber](const WaveDefinition& def) {
			return waveNumber <= def.max_wave;
		});

	// Handle the case where no definition matches (shouldn't happen with a catch-all)
	if (it == wave_definitions.end()) {
		if (developer->integer) {
			gi.Com_PrintFmt("Warning: No wave definition found for wave {}. Using default.\n", waveNumber);
		}
		// Return a safe default (e.g., the first definition's base)
		selected_type = wave_definitions[0].base_type;
	}
	else {
		// Apply the base type from the found definition
		const WaveDefinition& def = *it;
		selected_type = def.base_type;

		// Process optional components for this definition
		for (const auto& optional : def.optionals) {
			// Check if the type is valid, passes the random chance, and wasn't recently used
			if (optional.type != MonsterWaveType::None &&
				frandom() < optional.chance &&
				!WasRecentlyUsed(optional.type))
			{
				// Combine the optional type with the selected type using the overloaded '|' operator
				selected_type |= optional.type;

				// Optional: Play sound only for specific significant additions like Flying
				if (HasWaveType(optional.type, MonsterWaveType::Flying)) {
					// Make sure 'incoming' sound index is valid and loaded
					if (incoming) { // Check if the sound index is valid
						gi.sound(world, CHAN_VOICE, incoming, 1, ATTN_NONE, 0);
					}
				}
			}
		}
	}

	// Store the final selected type for history tracking
	StoreWaveType(selected_type);
	return selected_type;
}
void InitializeWaveType(int32_t waveLevel) {
	current_wave_type = GetWaveComposition(waveLevel);
}

// Wave difficulty multiplier
//inline static float GetWaveDifficultyMultiplier(int waveNumber) noexcept {
//	if (waveNumber <= 5) return 1.0f;
//	if (waveNumber <= 10) return 1.2f;
//	if (waveNumber <= 15) return 1.4f;
//	if (waveNumber <= 20) return 1.6f;
//	if (waveNumber <= 25) return 1.8f;
//	if (waveNumber <= 30) return 2.0f;
//	return 2.0f + ((waveNumber - 30) * 0.1f);
//}
// Update the function to accept MonsterTypeID

// Structure to include wave level information
struct MonsterTypeInfo {
	horde::MonsterTypeID typeId;  // Instead of const char* classname
	MonsterWaveType types;
	int minWave;
	float weight;


	constexpr MonsterTypeInfo(horde::MonsterTypeID id, MonsterWaveType t, int w, float wt)
		: typeId(id), types(t), minWave(w), weight(wt) {
	}
};

static const MonsterTypeInfo monsterTypes[] = {
	// Basic Infantry (Waves 1-5)
	{horde::MonsterTypeID::SOLDIER_LIGHT, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 1.0f},
	{horde::MonsterTypeID::SOLDIER, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 1, 0.9f},
	{horde::MonsterTypeID::SOLDIER_SS, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged, 2, 0.8f},
	{horde::MonsterTypeID::INFANTRY_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 3, 0.85f},

	// Early Flying Units (Waves 1-8)
	{horde::MonsterTypeID::FLYER, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Fast, 1, 0.7f},
	{horde::MonsterTypeID::FIXBOT, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.45f},
	{horde::MonsterTypeID::HOVER_VANILLA, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Ranged, 7, 0.6f},
	{horde::MonsterTypeID::FLOATER, MonsterWaveType::Flying | MonsterWaveType::Light | MonsterWaveType::Ranged, 12, 0.6f},

	// Special Wave Units (Waves 4-9)
	{horde::MonsterTypeID::GEKK, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Small | MonsterWaveType::Mutant | MonsterWaveType::Gekk, 4, 0.7f},
	{horde::MonsterTypeID::PARASITE, MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Melee, 5, 0.6f},
	{horde::MonsterTypeID::STALKER, MonsterWaveType::Ground | MonsterWaveType::Small | MonsterWaveType::Fast | MonsterWaveType::Arachnophobic, 7, 0.6f},
	{horde::MonsterTypeID::BRAIN, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special | MonsterWaveType::Melee | MonsterWaveType::Mutant, 6, 0.7f},

	// Elite Infantry (Waves 4-12)
	{horde::MonsterTypeID::SOLDIER_HYPERGUN, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 4, 0.7f},
	{horde::MonsterTypeID::SOLDIER_RIPPER, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 6, 0.8f},
	{horde::MonsterTypeID::SOLDIER_LASERGUN, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 10, 0.8f},
	{horde::MonsterTypeID::CHICK, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged, 7, 0.6f},

	// Medium Units (Waves 7-12)
	{horde::MonsterTypeID::GUNNER_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Light | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 8, 0.8f},
	{horde::MonsterTypeID::INFANTRY, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Heavy | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 11, 0.85f},
	{horde::MonsterTypeID::MEDIC, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Special, 7, 0.5f},
	{horde::MonsterTypeID::BERSERK, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Melee | MonsterWaveType::Berserk , 6, 0.8f},

	// Arachnophobic Units (Waves 8-18)
	{horde::MonsterTypeID::SPIDER, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 7, 0.35f},
	{horde::MonsterTypeID::GUNCMDR_VANILLA, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 12, 0.4f},
	{horde::MonsterTypeID::ARACHNID2, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite, 18, 0.4f},
	{horde::MonsterTypeID::GM_ARACHNID, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Heavy | MonsterWaveType::Elite, 15, 0.45f},
	{horde::MonsterTypeID::PSX_ARACHNID, MonsterWaveType::Ground | MonsterWaveType::Arachnophobic | MonsterWaveType::Elite | MonsterWaveType::Spawner, 25, 0.35f},

	// Mutant Units (Waves 9-14)
	{horde::MonsterTypeID::MUTANT, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 9, 0.7f},
	{horde::MonsterTypeID::SHAMBLER_SMALL, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Mutant | MonsterWaveType::Shambler, 14, 0.4f},
	{horde::MonsterTypeID::REDMUTANT, MonsterWaveType::Ground | MonsterWaveType::Fast | MonsterWaveType::Elite | MonsterWaveType::Melee | MonsterWaveType::Shambler | MonsterWaveType::Mutant, 14, 0.35f},

	// Heavy Ground Units (Waves 12-18)
	{horde::MonsterTypeID::GLADIATOR, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged, 12, 0.7f},
	{horde::MonsterTypeID::GUNNER, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Ranged | MonsterWaveType::Bomber, 12, 0.8f},
	{horde::MonsterTypeID::TANK_SPAWNER, MonsterWaveType::Ground | MonsterWaveType::Spawner | MonsterWaveType::Heavy | MonsterWaveType::Medium | MonsterWaveType::Elite, 13, 0.6f},
	{horde::MonsterTypeID::TANK, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 14, 0.4f},
	{horde::MonsterTypeID::TANK_COMMANDER, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Bomber, 16, 0.5f},
	{horde::MonsterTypeID::GUNCMDR, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Bomber, 15, 0.7f},
	{horde::MonsterTypeID::RUNNERTANK, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Fast, 21, 0.5f},
	{horde::MonsterTypeID::CHICK_HEAT, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Medium | MonsterWaveType::Elite | MonsterWaveType::Fast, 13, 0.6f},

	// Elite Flying Units (Waves 18-27)
	{horde::MonsterTypeID::HOVER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 18, 0.5f},
	{horde::MonsterTypeID::DAEDALUS, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 21, 0.4f},
	{horde::MonsterTypeID::FLOATER_TRACKER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite, 19, 0.45f},
	{horde::MonsterTypeID::DAEDALUS_BOMBER, MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Elite | MonsterWaveType::Bomber, 19, 0.35f},

	// Elite Ground Units (Waves 18+)
	{horde::MonsterTypeID::GLADIATOR_B, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f},
	{horde::MonsterTypeID::GLADIATOR_C, MonsterWaveType::Ground | MonsterWaveType::Medium | MonsterWaveType::Elite, 18, 0.7f},
	{horde::MonsterTypeID::SHAMBLER, MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 22, 0.4f},
	{horde::MonsterTypeID::TANK_64, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite, 28, 0.3f},

	// Special Heavy Units (Waves 20+)
	{horde::MonsterTypeID::JANITOR, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Bomber, 21, 0.5f},
	{horde::MonsterTypeID::JANITOR2, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Special | MonsterWaveType::Bomber, 26, 0.4f},
	{horde::MonsterTypeID::MEDIC_COMMANDER, MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special | MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.3f},

	// Semi-Boss Units (Waves 16+)
	{horde::MonsterTypeID::MAKRON, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 23, 0.01f},
	{horde::MonsterTypeID::PERRO_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Fast | MonsterWaveType::Small, 20, 0.4f},
	{horde::MonsterTypeID::WIDOW1, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 29, 0.3f},
	{horde::MonsterTypeID::SHAMBLER_KL, MonsterWaveType::Shambler | MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 33, 0.23f},
	{horde::MonsterTypeID::GUNCMDR_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Bomber, 33, 0.2f},
	{horde::MonsterTypeID::MAKRON_KL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Elite, 41, 0.2f},
	{horde::MonsterTypeID::BOSS2_KL, MonsterWaveType::Flying | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy, 46, 0.2f},
	{horde::MonsterTypeID::JORG_SMALL, MonsterWaveType::Ground | MonsterWaveType::SemiBoss | MonsterWaveType::Heavy | MonsterWaveType::Medium, 33, 0.4f},

	// Boss Units
	{horde::MonsterTypeID::BOSS2_64, MonsterWaveType::Flying | MonsterWaveType::Elite, 19, 0.2f},
	{horde::MonsterTypeID::BOSS2_MINI, MonsterWaveType::Flying | MonsterWaveType::Elite, 19, 0.2f},
	{horde::MonsterTypeID::CARRIER_MINI, MonsterWaveType::Flying | MonsterWaveType::Heavy | MonsterWaveType::Elite | MonsterWaveType::Spawner, 27, 0.2f}
};

// Optimized version using a precomputed map
static std::array<MonsterWaveType, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_monsterWaveTypes;
static bool g_monsterWaveTypesInitialized = false;

// Initialize the wave types array once
static void InitializeMonsterWaveTypes() {
	if (g_monsterWaveTypesInitialized) return;

	g_monsterWaveTypes.fill(MonsterWaveType::None);

	// Fill in all the wave types for each monster
	for (const auto& monster : monsterTypes) {
		horde::MonsterTypeID typeId = monster.typeId;
		if (typeId != horde::MonsterTypeID::UNKNOWN) {
			g_monsterWaveTypes[static_cast<size_t>(typeId)] = monster.types;
		}
	}

	g_monsterWaveTypesInitialized = true;
}

// Update GetMonsterWaveTypes to use the precomputed array
// Add a TypeID overload for GetMonsterWaveTypes
inline MonsterWaveType GetMonsterWaveTypes(horde::MonsterTypeID typeId) noexcept {
	if (typeId == horde::MonsterTypeID::UNKNOWN) return MonsterWaveType::None;

	// Make sure the wave types are initialized
	if (!g_monsterWaveTypesInitialized) {
		InitializeMonsterWaveTypes();
	}

	return g_monsterWaveTypes[static_cast<size_t>(typeId)];
}

#include <array>
#include <unordered_set>
#include <random>

// Modified boss_t structure
struct boss_t {
	horde::MonsterTypeID typeId;  // Instead of const char* classname
	int32_t min_level;
	int32_t max_level;
	float weight;
	BossSizeCategory sizeCategory;
	BossType type;
};
static constexpr boss_t BOSS_SMALL[] = {
	{horde::MonsterTypeID::CARRIER_MINI, 24, -1, 0.1f, BossSizeCategory::Small, BossType::CARRIER_MINI},
	{horde::MonsterTypeID::BOSS2_KL, 24, -1, 0.1f, BossSizeCategory::Small, BossType::BOSS2KL},
	{horde::MonsterTypeID::FIXBOT_KL, 9, -1, 0.4f, BossSizeCategory::Small, BossType::FIXBOTKL},
	{horde::MonsterTypeID::WIDOW2, 19, -1, 0.15f, BossSizeCategory::Small, BossType::WIDOW2},
	{horde::MonsterTypeID::TANK_64, -1, -1, 0.25f, BossSizeCategory::Small, BossType::TANK_64},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Small, BossType::SHAMBLERKL},
	{horde::MonsterTypeID::GUNCMDR_KL, -1, 20, 0.3f, BossSizeCategory::Small, BossType::GUNCMDRKL},
	{horde::MonsterTypeID::MAKRON_KL, 36, -1, 0.2f, BossSizeCategory::Small, BossType::MAKRONKL},
	{horde::MonsterTypeID::MAKRON, 16, 26, 0.1f, BossSizeCategory::Small, BossType::OTHER},
	{horde::MonsterTypeID::PSX_ARACHNID, 15, -1, 0.1f, BossSizeCategory::Small, BossType::PSX_ARACHNID},
	{horde::MonsterTypeID::REDMUTANT, -1, 24, 0.1f, BossSizeCategory::Small, BossType::REDMUTANT}
};

static constexpr boss_t BOSS_MEDIUM[] = {
	{horde::MonsterTypeID::CARRIER, 24, -1, 0.1f, BossSizeCategory::Medium, BossType::CARRIER},
	{horde::MonsterTypeID::BOSS2, 19, -1, 0.1f, BossSizeCategory::Medium, BossType::BOSS2},
	{horde::MonsterTypeID::FIXBOT_KL, 9, -1, 0.4f, BossSizeCategory::Small, BossType::FIXBOTKL},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Medium, BossType::SHAMBLERKL},
	{horde::MonsterTypeID::TANK_64, 21, -1, 0.1f, BossSizeCategory::Medium, BossType::TANK_64},
	{horde::MonsterTypeID::SHAMBLER_KL, 21, -1, 0.1f, BossSizeCategory::Medium, BossType::SHAMBLERKL},
	{horde::MonsterTypeID::GUNCMDR_KL, 21, -1, 0.1f, BossSizeCategory::Medium, BossType::GUNCMDRKL},
	{horde::MonsterTypeID::PSX_GUARDIAN, -1, 24, 0.1f, BossSizeCategory::Medium, BossType::PSX_GUARDIAN},
	{horde::MonsterTypeID::WIDOW2, 19, -1, 0.15f, BossSizeCategory::Medium, BossType::WIDOW2},
	{horde::MonsterTypeID::PSX_ARACHNID, -14, -1, 0.1f, BossSizeCategory::Medium, BossType::PSX_ARACHNID},
	{horde::MonsterTypeID::MAKRON_KL, 26, -1, 0.2f, BossSizeCategory::Medium, BossType::MAKRONKL},
	{horde::MonsterTypeID::MAKRON, 16, 25, 0.1f, BossSizeCategory::Medium, BossType::OTHER},
	{horde::MonsterTypeID::REDMUTANT, -1, 24, 0.1f, BossSizeCategory::Small, BossType::REDMUTANT}
};

static constexpr boss_t BOSS_LARGE[] = {
	{horde::MonsterTypeID::CARRIER, 24, -1, 0.1f, BossSizeCategory::Large, BossType::CARRIER},
	{horde::MonsterTypeID::BOSS2, 19, -1, 0.1f, BossSizeCategory::Large, BossType::BOSS2},
	{horde::MonsterTypeID::FIXBOT_KL, 9, -1, 0.4f, BossSizeCategory::Small, BossType::FIXBOTKL},
	{horde::MonsterTypeID::BOSS5, -1, -1, 0.1f, BossSizeCategory::Large, BossType::BOSS5},
	{horde::MonsterTypeID::TANK_64, -1, 20, 0.45f, BossSizeCategory::Large, BossType::TANK_64},
	{horde::MonsterTypeID::SHAMBLER_KL, -1, 20, 0.3f, BossSizeCategory::Large, BossType::SHAMBLERKL},
	{horde::MonsterTypeID::GUNCMDR_KL, -1, 20, 0.3f, BossSizeCategory::Large, BossType::GUNCMDRKL},
	{horde::MonsterTypeID::TANK_64, 21, -1, 0.1f, BossSizeCategory::Large, BossType::TANK_64},
	{horde::MonsterTypeID::SHAMBLER_KL, 21, -1, 0.1f, BossSizeCategory::Large, BossType::SHAMBLERKL},
	{horde::MonsterTypeID::PSX_ARACHNID, 14, -1, 0.1f, BossSizeCategory::Large, BossType::PSX_ARACHNID},
	{horde::MonsterTypeID::WIDOW, -1, -1, 0.1f, BossSizeCategory::Large, BossType::WIDOW},
	{horde::MonsterTypeID::PSX_GUARDIAN, -1, -1, 0.1f, BossSizeCategory::Large, BossType::PSX_GUARDIAN},
	{horde::MonsterTypeID::BOSS5, -1, 24, 0.1f, BossSizeCategory::Large, BossType::BOSS5},
	{horde::MonsterTypeID::JORG, 30, -1, 0.15f, BossSizeCategory::Large, BossType::JORG},
	{horde::MonsterTypeID::MAKRON_KL, 30, -1, 0.2f, BossSizeCategory::Large, BossType::MAKRONKL},
	{horde::MonsterTypeID::REDMUTANT, -1, 24, 0.1f, BossSizeCategory::Small, BossType::REDMUTANT},
	{horde::MonsterTypeID::WIDOW2, -1, 24, 0.19f, BossSizeCategory::Small, BossType::WIDOW2
}
};

// Optimized GetBossList function
static std::span<const boss_t> GetBossList(const MapSize& mapSize, horde::MapID mapId) {
	if (mapSize.isSmallMap ||
		mapId == horde::MapID::Q2DM4 ||
		mapId == horde::MapID::Q64_COMM ||
		mapId == horde::MapID::TEST_TEST_KAISER) {
		return std::span<const boss_t>(BOSS_SMALL);
	}

	if (mapSize.isMediumMap ||
		mapId == horde::MapID::RDM8 ||
		mapId == horde::MapID::XDM1) {
		if (mapId == horde::MapID::MGU6M3 ||
			mapId == horde::MapID::RBOSS) {
			static std::array<boss_t, std::size(BOSS_MEDIUM)> filteredMediumBossList;
			size_t count = 0;

			for (const auto& boss : std::span<const boss_t>(BOSS_MEDIUM)) {
				if (boss.type != BossType::GUARDIAN && boss.type != BossType::PSX_GUARDIAN) {
					filteredMediumBossList[count++] = boss;
				}
			}

			return std::span<const boss_t>(filteredMediumBossList.data(), count);
		}
		return std::span<const boss_t>(BOSS_MEDIUM);
	}

	if (mapSize.isBigMap ||
		mapId == horde::MapID::TEST_SPBOX
		|| mapId == horde::MapID::Q2CTF4) {
		if (mapId == horde::MapID::TEST_SPBOX
			|| mapId == horde::MapID::Q2CTF4) {
			static std::array<boss_t, std::size(BOSS_LARGE)> filteredLargeBossList;
			size_t count = 0;

			for (const auto& boss : std::span<const boss_t>(BOSS_LARGE)) {
				if (boss.type != BossType::BOSS5) {
					filteredLargeBossList[count++] = boss;
				}
			}

			return std::span<const boss_t>(filteredLargeBossList.data(), count);
		}
		return std::span<const boss_t>(BOSS_LARGE);
	}

	return std::span<const boss_t>(); // Empty span for no match
}

// static arrays to replace std::vectors
struct EligibleBosses {
	const boss_t* items[MAX_ELIGIBLE_BOSSES] = {};
	size_t count = 0;

	void clear() noexcept { count = 0; }

	bool add(const boss_t* boss) noexcept {
		if (!boss || count >= MAX_ELIGIBLE_BOSSES)
			return false;

		// Use constant expressions for array access
		switch (count) {
		case 0:  items[0] = boss; break;
		case 1:  items[1] = boss; break;
		case 2:  items[2] = boss; break;
		case 3:  items[3] = boss; break;
		case 4:  items[4] = boss; break;
		case 5:  items[5] = boss; break;
		case 6:  items[6] = boss; break;
		case 7:  items[7] = boss; break;
		case 8:  items[8] = boss; break;
		case 9:  items[9] = boss; break;
		case 10: items[10] = boss; break;
		case 11: items[11] = boss; break;
		case 12: items[12] = boss; break;
		case 13: items[13] = boss; break;
		case 14: items[14] = boss; break;
		case 15: items[15] = boss; break;
		default: return false;
		}
		count++;
		return true;
	}
};

// static array for recent bosses
struct RecentBosses {
	horde::MonsterTypeID items[MAX_RECENT_BOSSES] = { horde::MonsterTypeID::UNKNOWN };
	size_t count = 0;

	void add(horde::MonsterTypeID boss) noexcept {
		// Ignore invalid TypeIDs
		if (boss == horde::MonsterTypeID::UNKNOWN)
			return;

		// Add to array, shift if needed
		if (count < MAX_RECENT_BOSSES) {
			items[count++] = boss;
		}
		else {
			// Shift array entries
			memmove(&items[0], &items[1], sizeof(items[0]) * (MAX_RECENT_BOSSES - 1));
			items[MAX_RECENT_BOSSES - 1] = boss;
		}
	}

	bool contains(horde::MonsterTypeID boss) const noexcept {
		// Check if boss is in recent list
		if (boss == horde::MonsterTypeID::UNKNOWN)
			return false;

		for (size_t i = 0; i < count; ++i) {
			if (items[i] == boss)
				return true;
		}
		return false;
	}

	void clear() noexcept {
		count = 0;
		std::fill_n(items, MAX_RECENT_BOSSES, horde::MonsterTypeID::UNKNOWN);
	}

	// Compatibility method for string interface
	void add(const char* bossClassname) noexcept {
		add(horde::MonsterTypeRegistry::GetTypeID(bossClassname));
	}

	// Compatibility method for string interface
	bool contains(const char* bossClassname) const noexcept {
		return contains(horde::MonsterTypeRegistry::GetTypeID(bossClassname));
	}
};
static RecentBosses recent_bosses;

// Precomputed boss eligibility cache to avoid repeated calculations
struct BossEligibilityCache {
	static constexpr int32_t MAX_PRECOMPUTED_WAVE = 50;

	struct LevelEligibility {
		horde::MonsterTypeID typeIds[MAX_ELIGIBLE_BOSSES] = { horde::MonsterTypeID::UNKNOWN };
		uint8_t count = 0;
	};

	// Cache by map type (0=small, 1=medium, 2=large) and wave level
	LevelEligibility eligibility[3][MAX_PRECOMPUTED_WAVE + 1];
	inline static bool initialized = false;

	void initialize() {
		// For each map type
		for (int mapType = 0; mapType < 3; ++mapType) {
			// Get mapSize for this type
			MapSize mapSize;
			if (mapType == 0) mapSize = { true, false, false };  // Small
			else if (mapType == 2) mapSize = { false, true, false };  // Large
			else mapSize = { false, false, true };  // Medium

			// Get boss list
			const auto bossList = GetBossList(mapSize, horde::MapID::UNKNOWN);

			// For each wave level
			for (int32_t wave = 0; wave <= MAX_PRECOMPUTED_WAVE; ++wave) {
				auto& levelData = eligibility[mapType][wave];
				levelData.count = 0;

				// Filter bosses by level and store TypeIDs directly
				for (const auto& boss : bossList) {
					if ((wave >= boss.min_level || boss.min_level == -1) &&
						(wave <= boss.max_level || boss.max_level == -1)) {

						if (levelData.count < MAX_ELIGIBLE_BOSSES) {
							levelData.typeIds[levelData.count++] = boss.typeId;
						}
					}
				}
			}
		}
		initialized = true;
	}
};
static BossEligibilityCache g_bossEligibilityCache;

static edict_t* CreateBaseHordeMonster(horde::MonsterTypeID typeId, const vec3_t& origin, const vec3_t& angles) {
	// Convert TypeID to classname
	const char* classname = horde::MonsterTypeRegistry::GetClassname(typeId);
	if (!classname) {
		if (developer->integer) {
			gi.Com_PrintFmt("CreateBaseHordeMonster: Invalid TypeID provided.\n");
		}
		return nullptr;
	}

	// Create the entity
	edict_t* monster = G_Spawn();
	if (!monster) {
		if (developer->integer) {
			gi.Com_PrintFmt("CreateBaseHordeMonster: G_Spawn failed for {}\n", classname);
		}
		return nullptr;
	}

	// Set basic properties
	monster->classname = classname;
	monster->s.origin = origin;
	monster->s.angles = angles;

	// Apply common Horde flags automatically
	monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	monster->monsterinfo.last_sentrygun_target_time = 0_ms;
	monster->was_spawned_by_horde = true; // Mark as horde spawned

	// Mark specifically if in spawning state
	if (g_horde_local.state == horde_state_t::spawning) {
		monster->spawned_in_spawn_state = true;
	}

	// Return the initialized (but not yet fully spawned/linked) entity
	return monster;
}

edict_t* SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t& origin, const vec3_t& angles, [[maybe_unused]] bool applyHordeFlags = true) {
	const char* classname = horde::MonsterTypeRegistry::GetClassname(typeId);

	// Create the base entity using the helper
	edict_t* monster = CreateBaseHordeMonster(typeId, origin, angles);
	if (!monster) {
		return nullptr; // Helper function failed (G_Spawn or invalid TypeID)
	}


	// Set basic properties
	monster->classname = classname;
	monster->s.origin = origin;
	monster->s.angles = angles;

	// Apply common Horde flags if requested
	if (applyHordeFlags) {
		monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
		monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
		monster->monsterinfo.last_sentrygun_target_time = 0_ms;
		monster->was_spawned_by_horde = true; // Mark as horde spawned

		// In spawning state, mark specifically
		if (g_horde_local.state == horde_state_t::spawning) {
			monster->spawned_in_spawn_state = true; // Mark as spawned during spawning state
		}
	}

	// Spawn the entity with protection from errors
	monster->solid = SOLID_NOT;  // Start as non-solid to avoid immediate collisions
	ED_CallSpawn(monster);

	// Check if spawn succeeded
	if (!monster->inuse) {
		G_FreeEdict(monster); // Clean up if ED_CallSpawn failed
		return nullptr;
	}

	// Re-set solid state and properly link
	monster->solid = SOLID_BBOX;
	gi.linkentity(monster);

	// *** Final Post-Link Stuck Check using MASK_MONSTERSOLID ***
	trace_t post_spawn_trace = gi.trace(monster->s.origin, monster->mins, monster->maxs,
		monster->s.origin, monster, MASK_MONSTERSOLID);

	if (post_spawn_trace.startsolid) {
		if (developer->integer) {
			edict_t* blocker = post_spawn_trace.ent;
			gi.Com_PrintFmt("SpawnMonsterByTypeID: WARNING - {} spawned stuck (Post-Link Check) at {}. Blocker: {} ({}). Flagging for teleport.\n",
				monster->classname, monster->s.origin,
				blocker ? (blocker->classname ? blocker->classname : "unknown") : "world/unknown",
				blocker ? (blocker - g_edicts) : -1);
		}
		monster->monsterinfo.was_stuck = true;
		monster->monsterinfo.stuck_check_time = level.time;
	}

	return monster;
}
// Optional overload with fewer parameters for common cases
edict_t* SpawnMonsterByTypeID(horde::MonsterTypeID typeId, const vec3_t& origin) {
	return SpawnMonsterByTypeID(typeId, origin, vec3_origin, true);
}


static horde::MonsterTypeID G_HordePickBOSSType(const MapSize& mapSize, std::string_view mapname, int32_t waveNumber, edict_t* bossEntity) {
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(mapname.data());

	// Initialize boss eligibility cache if needed
	static bool cache_initialized = false;
	if (!cache_initialized) {
		g_bossEligibilityCache.initialize();
		cache_initialized = true;
	}

	// Determine map type index for cache lookup
	const int mapTypeIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);

	// Safe level clamping
	const int32_t safeWaveNumber = std::min(waveNumber, BossEligibilityCache::MAX_PRECOMPUTED_WAVE);

	// Get boss list once
	const auto boss_list = GetBossList(mapSize, mapId);
	if (boss_list.empty()) {
		if (developer->integer) {
			gi.Com_PrintFmt("WARNING: Empty boss list for map {} at wave {}\n",
				mapname.data(), waveNumber);
		}
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Get precomputed eligible bosses for this level
	const auto& eligibilityData = g_bossEligibilityCache.eligibility[mapTypeIndex][safeWaveNumber];

	// Validate precomputed data
	if (eligibilityData.count == 0 || eligibilityData.count > MAX_ELIGIBLE_BOSSES) {
		if (developer->integer) {
			gi.Com_PrintFmt("WARNING: Invalid eligible boss count: {}\n",
				eligibilityData.count);
		}
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Use stack-based array for weight calculations
	struct WeightedBoss {
		const boss_t* boss;
		float weight;
		float cumulativeWeight;
	};

	WeightedBoss weightedBosses[MAX_ELIGIBLE_BOSSES]{}; // Ensure MAX_ELIGIBLE_BOSSES is large enough
	size_t weightedCount = 0;
	float totalWeight = 0.0f;

	// First pass - check precomputed eligible bosses against recent history
	for (size_t i = 0; i < eligibilityData.count; ++i) {
		// Get typeId directly from eligibility data
		horde::MonsterTypeID bossTypeId = eligibilityData.typeIds[i];

		// Skip if unknown type
		if (bossTypeId == horde::MonsterTypeID::UNKNOWN) {
			continue;
		}

		// --- CORRECTED CHECK ---
		// Skip if this TypeID is in the recent bosses list using the ID directly
		if (recent_bosses.contains(bossTypeId)) { // <-- Use contains(MonsterTypeID)
			continue;
		}
		// --- END CORRECTION ---

		// Find the boss_t entry with this typeId (only if not recently used)
		const boss_t* boss = nullptr;
		for (const auto& b : boss_list) {
			if (b.typeId == bossTypeId) {
				boss = &b;
				break;
			}
		}

		if (!boss) { // Safety check
			continue;
		}

		// Calculate weight with optimized factors
		float weight = boss->weight;

		// Combined weight adjustment with fewer branches
		if (waveNumber >= boss->min_level && boss->min_level != -1) {
			// Boost if within ideal level range
			if (waveNumber <= boss->min_level + 5) {
				weight *= 1.3f;
			}
			// Reduce if well beyond ideal level
			else if (waveNumber > boss->min_level + 10) {
				weight *= 0.8f;
			}
		}

		// Apply difficulty adjustment in one check
		if (((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) &&
			boss->sizeCategory == BossSizeCategory::Large) {
			weight *= 1.2f;
		}

		// Add to weighted list
		if (weightedCount < MAX_ELIGIBLE_BOSSES) {
			totalWeight += weight;
			weightedBosses[weightedCount].boss = boss;
			weightedBosses[weightedCount].weight = weight;
			weightedBosses[weightedCount].cumulativeWeight = totalWeight;
			weightedCount++;
		}
	} // End of first pass loop

	// If no eligible bosses with history filter, retry without filter
	if (weightedCount == 0 && recent_bosses.count > 0) {
		if (developer->integer > 1) {
			gi.Com_PrintFmt("INFO: No non-recent bosses eligible, resetting history and retrying...\n");
		}
		recent_bosses.clear(); // Reset history

		// Try all bosses from the list based on eligibility data again (since we already filtered by level)
		for (size_t i = 0; i < eligibilityData.count; ++i) {
			horde::MonsterTypeID bossTypeId = eligibilityData.typeIds[i];
			if (bossTypeId == horde::MonsterTypeID::UNKNOWN) continue;

			// Find the boss_t entry again
			const boss_t* boss = nullptr;
			for (const auto& b : boss_list) {
				if (b.typeId == bossTypeId) {
					boss = &b;
					break;
				}
			}
			if (!boss) continue;

			float weight = boss->weight; // Use base weight again

			// Re-apply adjustments if necessary (e.g., difficulty) - copy from above
			if (((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) &&
				boss->sizeCategory == BossSizeCategory::Large) {
				weight *= 1.2f;
			}

			// Add to weighted list
			if (weightedCount < MAX_ELIGIBLE_BOSSES) {
				totalWeight += weight;
				weightedBosses[weightedCount].boss = boss;
				weightedBosses[weightedCount].weight = weight;
				weightedBosses[weightedCount].cumulativeWeight = totalWeight;
				weightedCount++;
			}
		}
	}

	// Still no eligible bosses? Return unknown
	if (weightedCount == 0 || totalWeight <= 0.0f) {
		if (developer->integer) {
			gi.Com_PrintFmt("WARNING: No eligible bosses found even after potential history reset.\n");
		}
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Select boss with binary search for better performance
	const float randomValue = frandom() * totalWeight;

	// Binary search through accumulated weights
	size_t left = 0;
	size_t right = weightedCount - 1;

	// Handle edge case: only one item
	if (left == right) {
		// Directly select the only item
	}
	else {
		// Perform binary search
		while (left < right) {
			const size_t mid = left + (right - left) / 2; // Safer midpoint calculation
			if (weightedBosses[mid].cumulativeWeight < randomValue) {
				left = mid + 1;
			}
			else {
				right = mid;
			}
		}
	}


	// Get selected boss
	const boss_t* chosen_boss = weightedBosses[left].boss;
	if (chosen_boss) {
		// --- CORRECTED ADD ---
		// Add to recent bosses using the MonsterTypeID directly
		recent_bosses.add(chosen_boss->typeId); // <-- Use add(MonsterTypeID)
		// --- END CORRECTION ---

		// Set boss size category for the entity
		bossEntity->bossSizeCategory = chosen_boss->sizeCategory;

		if (developer->integer > 1) {
			const char* chosen_name = horde::MonsterTypeRegistry::GetClassname(chosen_boss->typeId);
			gi.Com_PrintFmt("Selected Boss: {} (Weight: {:.2f})\n",
				chosen_name ? chosen_name : "Unknown", weightedBosses[left].weight);
		}

		// Return the TypeID directly
		return chosen_boss->typeId;
	}

	// Should ideally not happen if weightedCount > 0, but return UNKNOWN as a fallback
	return horde::MonsterTypeID::UNKNOWN;
}

struct picked_item_t {
	const weighted_item_t* item;
	float weight;
};

// Adapted Caching Structure (using std::array for fixed size based on input)
struct HordeItemSelectionCache {
	// Automatically size based on the actual data array
	static constexpr size_t MAX_ENTRIES = std::size(hordeItemData);
	struct Entry {
		const HordeItemInfo* itemInfo; // Pointer to the HordeItemInfo entry
		float weight;
		float cumulative_weight;
	};

	size_t count = 0;
	float total_weight = 0.0f;
	// Use std::array for compile-time sized array based on hordeItemData
	std::array<Entry, MAX_ENTRIES> entries{}; // Value-initialize

	void clear() noexcept {
		count = 0;
		total_weight = 0.0f;
		// No need to clear array elements explicitly when count = 0
	}
};
// Static cache instance specifically for Horde item selection
static HordeItemSelectionCache horde_item_cache;

// Modified Function using HordeItemInfo and item_id_t
gitem_t* G_HordePickItem() {
	// Reset cache for this selection attempt
	horde_item_cache.clear();

	// Use std::span for safe iteration over the hordeItemData array
	std::span<const HordeItemInfo> items_view{ hordeItemData };

	// --- Collect Eligible Items ---
	for (const auto& hordeItemInfo : items_view) {
		// Check if cache is full (safety check, should not happen with std::array)
		if (horde_item_cache.count >= HordeItemSelectionCache::MAX_ENTRIES) {
			if (developer->integer) { // Log error if this happens unexpectedly
				gi.Com_PrintFmt("Warning: HordeItemSelectionCache full! Increase MAX_ENTRIES if not using std::array.\n");
			}
			break;
		}

		// Filter based on minimum wave level required for the item
		if (g_horde_local.level >= hordeItemInfo.minWave) {

			// Use the weight directly from HordeItemInfo
			// Future Enhancements: Add more complex weight adjustments here
			// (e.g., based on player count, current inventory, game state)
			float adjusted_weight = hordeItemInfo.weight;

			// Ensure weight is positive before adding to the cache
			if (adjusted_weight > 0.0f) {
				horde_item_cache.total_weight += adjusted_weight;
				// Get reference to the next entry in the cache array
				auto& entry = horde_item_cache.entries[horde_item_cache.count];
				entry.itemInfo = &hordeItemInfo; // Store pointer to the HordeItemInfo struct
				entry.weight = adjusted_weight;
				entry.cumulative_weight = horde_item_cache.total_weight;
				horde_item_cache.count++; // Increment the count of eligible items
			}
		}
	} // End of item collection loop

	// Check if any eligible items were found
	if (horde_item_cache.count == 0 || horde_item_cache.total_weight <= 0.0f) {
		// Log if no items found (useful for debugging balance/data issues)
		if (developer->integer) {
			gi.Com_PrintFmt("Warning: G_HordePickItem found no eligible items for wave {}.\n", g_horde_local.level);
		}
		return nullptr; // No items eligible or they all have zero/negative weight
	}

	// --- Weighted Random Selection ---
	// Generate a random floating-point value within the total weight range
	const float random_value = frandom() * horde_item_cache.total_weight;

	// Use binary search to efficiently find the selected item based on cumulative weight
	size_t left = 0;
	size_t right = horde_item_cache.count - 1;

	// Handle edge case: only one eligible item
	if (left == right) {
		// 'left' (or 'right') is the index of the only choice
	}
	else {
		// Perform binary search
		while (left < right) {
			// Calculate midpoint safely to avoid potential overflow with large indices
			const size_t mid = left + (right - left) / 2;
			if (horde_item_cache.entries[mid].cumulative_weight < random_value) {
				left = mid + 1; // Random value is in the upper half
			}
			else {
				right = mid; // Random value is in the lower half (including mid)
			}
		}
	}
	// After the loop, 'left' holds the index of the chosen item entry in the cache

	// --- Retrieve and Return the Item ---
	// Get the chosen HordeItemInfo pointer from the cache entry
	const HordeItemInfo* chosen_info = horde_item_cache.entries[left].itemInfo;

	// Safety check: Ensure chosen_info is not null (shouldn't happen if count > 0)
	if (!chosen_info) {
		if (developer->integer) {
			gi.Com_PrintFmt("Error: G_HordePickItem - chosen_info is null despite count > 0.\n");
		}
		return nullptr;
	}

	// Use the item_id_t from the chosen info to get the actual gitem_t pointer
	// This replaces the old FindItemByClassname call
	return GetItemByIndex(chosen_info->id);

}
static int32_t countFlyingSpawns() noexcept {
	return std::count_if(g_edicts + 1, g_edicts + globals.num_edicts,
		[](const edict_t& ent) {
			return ent.inuse &&
				strcmp(ent.classname, "info_player_deathmatch") == 0 &&
				ent.style == 1;
		});
}

static float adjustFlyingSpawnProbability(int32_t flyingSpawns) noexcept {
	switch (flyingSpawns) {
	case 0: return 1.0f;
	case 1: return 0.9f;
	case 2: return 0.8f;
	case 3: return 0.6f;
	default: return 0.5f;
	}
}

static void UpdateCooldowns(edict_t* spawn_point) {
	auto& data = spawnPointsData[spawn_point];
	data.lastSpawnTime = level.time;
	data.isTemporarilyDisabled = true;
	data.cooldownEndsAt = level.time + SPAWN_POINT_COOLDOWN;
}

// Category check functions using TypeIDs
inline bool IsFlying(horde::MonsterTypeID typeId) {
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Flying);
}

inline bool IsGroundUnit(horde::MonsterTypeID typeId) {
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Ground);
}

inline bool IsSmallUnit(horde::MonsterTypeID typeId) {
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Small);
}

inline bool IsBossUnit(horde::MonsterTypeID typeId) {
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Boss);
}

inline bool IsSpecialUnit(horde::MonsterTypeID typeId) {
	return HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Special);
}

inline bool IsValidMonsterForWave(horde::MonsterTypeID typeId, MonsterWaveType waveRequirements) {
	// Fast exit for no requirements
	if (waveRequirements == MonsterWaveType::None) {
		return true;
	}

	// Special wave types should be exclusive - if one of these is set, the monster MUST have it
	const bool isSpecialWaveType =
		HasWaveType(waveRequirements, MonsterWaveType::Gekk) ||
		HasWaveType(waveRequirements, MonsterWaveType::Berserk) ||
		HasWaveType(waveRequirements, MonsterWaveType::Mutant) ||
		HasWaveType(waveRequirements, MonsterWaveType::Spawner) ||
		HasWaveType(waveRequirements, MonsterWaveType::Shambler) ||
		HasWaveType(waveRequirements, MonsterWaveType::Arachnophobic);

	// For special wave types, enforce strict matching
	if (isSpecialWaveType) {
		// If wave is Gekk, only allow Gekk monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Gekk) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Gekk))
			return false;

		// If wave is Berserker, only allow Berserker monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Berserk) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Berserk))
			return false;

		// If wave is Mutant, only allow Mutant monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Mutant) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Mutant))
			return false;

		// If wave is Spawner, only allow Spawner monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Spawner) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Spawner))
			return false;

		// If wave is Shambler, only allow Shambler monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Shambler) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Shambler))
			return false;

		// If wave is Arachnophobic, only allow Arachnophobic monsters
		if (HasWaveType(waveRequirements, MonsterWaveType::Arachnophobic) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Arachnophobic))
			return false;
	}
	else {
		// For regular wave types, check all required flags
		// First check the most important exclusive categories that must match
		if (HasWaveType(waveRequirements, MonsterWaveType::Flying) && !IsFlying(typeId))
			return false;

		if (HasWaveType(waveRequirements, MonsterWaveType::Small) && !IsSmallUnit(typeId))
			return false;

		if (HasWaveType(waveRequirements, MonsterWaveType::Heavy) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Heavy))
			return false;

		if (HasWaveType(waveRequirements, MonsterWaveType::Melee) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Melee))
			return false;

		if (HasWaveType(waveRequirements, MonsterWaveType::Bomber) &&
			!HasWaveType(GetMonsterWaveTypes(typeId), MonsterWaveType::Bomber))
			return false;
	}

	// For mixed waves, check if there's at least one match in other categories
	// This only applies to non-special wave types now
	return isSpecialWaveType || (GetMonsterWaveTypes(typeId) & waveRequirements) != MonsterWaveType::None;
}

static horde::MonsterTypeID G_HordePickMonsterType(edict_t* spawn_point) {
	static constexpr size_t MONSTER_CACHE_SIZE = std::size(monsterTypes);

	static struct {
		struct Entry {
			horde::MonsterTypeID typeId;
			float weight;
			float cumulative_weight;
		};
		std::array<Entry, MONSTER_CACHE_SIZE> entries{};
		size_t count;
		float total_weight;
		void clear() noexcept {
			count = 0;
			total_weight = 0.0;
		}

	} monster_cache;


	// Track consecutive failures for emergency handling
	static int spawn_selection_failures = 0;
	static MonsterWaveType saved_wave_type = MonsterWaveType::None;
	static bool emergency_mode = false;

	// Reset cache counters
	monster_cache.count = 0;
	monster_cache.total_weight = 0.0f;

	// Early validation for quick exit
	if (!spawn_point || !spawn_point->inuse) {
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Check spawn point availability
	auto& data = spawnPointsData[spawn_point];
	if (data.isTemporarilyDisabled) {
		if (level.time < data.cooldownEndsAt) {
			spawn_selection_failures++;
			return horde::MonsterTypeID::UNKNOWN;
		}
		// Reset state if cooldown expired
		data.isTemporarilyDisabled = false;
		data.attempts = 0;
	}

	// Check if spawn point is occupied
	if (IsSpawnPointOccupied(spawn_point)) {
		IncreaseSpawnAttempts(spawn_point);
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Cache commonly used values
	const int32_t currentLevel = g_horde_local.level;
	MonsterWaveType currentWaveTypes = current_wave_type;
	const bool isSpawnPointFlying = spawn_point->style == 1;

	// Enter emergency mode after multiple failures
	if (spawn_selection_failures >= 3 && !emergency_mode) {
		if (developer->integer) {
			gi.Com_PrintFmt("WARNING: Entering emergency monster selection mode after {} failures\n",
				spawn_selection_failures);
		}

		// Save the original wave type if not already saved
		if (saved_wave_type == MonsterWaveType::None && currentWaveTypes != MonsterWaveType::None) {
			saved_wave_type = currentWaveTypes;
		}

		// Simplify wave type requirements
		if (HasWaveType(currentWaveTypes, MonsterWaveType::Flying)) {
			currentWaveTypes = MonsterWaveType::Flying; // Keep only flying requirement
		}
		else {
			currentWaveTypes = MonsterWaveType::Ground; // Default to ground only
		}

		emergency_mode = true;
	}

	// In severe failure cases, disable wave type filtering completely
	if (spawn_selection_failures >= 6) {
		if (developer->integer && currentWaveTypes != MonsterWaveType::None) {
			gi.Com_PrintFmt("CRITICAL: Disabling wave type filtering after {} failures\n",
				spawn_selection_failures);
		}
		currentWaveTypes = MonsterWaveType::None; // Allow any monster type
	}

	// Determine if we should spawn a higher level monster
	bool spawn_higher_level = false;
	if (currentLevel <= 10) {
		spawn_higher_level = frandom() < 0.16f;
	}
	else if (currentLevel <= 15) {
		spawn_higher_level = frandom() < 0.05f;
	}
	else {
		spawn_higher_level = frandom() < 0.02f;
	}

	// Disable higher level monsters in emergency mode
	if (emergency_mode) {
		spawn_higher_level = false;
	}

	// Calculate effective level for monster selection
	int32_t effectiveLevel = currentLevel;

	if (spawn_higher_level) {
		// Calculate level boost
		int32_t levelBoost;
		if (currentLevel < 7) {
			levelBoost = irandom(2, 4);  // Reduced from 4-7 to 2-4
		}
		else if (currentLevel <= 10) {
			levelBoost = irandom(2, 3);  // Reduced from 3-5 to 2-3
		}
		else {
			levelBoost = irandom(1, 2);  // Reduced from 2-3 to 1-2
		}

		// Cap the effective level conservatively
		int32_t maxLevel = currentLevel < 7 ? irandom(5, 7) :
			(currentLevel < 15 ? currentLevel + 5 : currentLevel + 3);
		effectiveLevel = std::min(currentLevel + levelBoost, maxLevel);

		// Absolute cap at 40
		effectiveLevel = std::min(effectiveLevel, 40);

		// Check if any monster is eligible at this level
		bool any_eligible_monsters = false;
		for (const auto& monster : monsterTypes) {
			if (monster.minWave <= effectiveLevel &&
				(currentWaveTypes == MonsterWaveType::None || IsValidMonsterForWave(monster.typeId, currentWaveTypes))) {

				const bool isFlyingMonster = HasWaveType(GetMonsterWaveTypes(monster.typeId), MonsterWaveType::Flying);
				if (!(isSpawnPointFlying && !isFlyingMonster)) {
					any_eligible_monsters = true;
					break;
				}
			}
		}

		// If no eligible monsters, revert to current level
		if (!any_eligible_monsters) {
			if (developer->integer) {
				gi.Com_PrintFmt("WARNING: No eligible monsters at level {}. Reverting to current wave {}\n",
					effectiveLevel, currentLevel);
			}
			effectiveLevel = currentLevel;
		}
	}

	// Skip boss wave checks in emergency mode
	if (!emergency_mode && currentWaveTypes == MonsterWaveType::None &&
		currentLevel >= 10 && currentLevel % 5 == 0 && !boss_spawned_for_wave) {
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Pre-compute flying spawn adjustment
	const int32_t flyingSpawns = countFlyingSpawns();
	const float adjustmentFactor = adjustFlyingSpawnProbability(flyingSpawns);

	// Build monster cache - prioritize compatibility with spawn point type
	for (const auto& monster : monsterTypes) {
		if (monster_cache.count >= 32) break;

		// Basic level check
		if (monster.minWave > effectiveLevel) continue;

		// Skip if not valid for current wave type using typeId directly
		if (currentWaveTypes != MonsterWaveType::None &&
			!IsValidMonsterForWave(monster.typeId, currentWaveTypes)) {
			continue;
		}

		// Flying compatibility check - always maintain this basic requirement
		if (isSpawnPointFlying && !IsFlying(monster.typeId)) continue;

		// Calculate weight
		float weight = monster.weight;

		// Apply level-based adjustments
		if (currentLevel <= 5) {
			if (!HasWaveType(monster.types, MonsterWaveType::Light)) weight *= 0.3f;
		}
		else if (currentLevel <= 10) {
			if (!HasWaveType(monster.types, MonsterWaveType::Light | MonsterWaveType::Small)) weight *= 0.4f;
		}
		else if (currentLevel <= 15) {
			if (!HasWaveType(monster.types, MonsterWaveType::Medium)) weight *= 0.5f;
		}
		else {
			// Special handling for Flying monsters to ensure they remain relevant in higher waves
			if (IsFlying(monster.typeId)) {
				// Less severe penalty for flying monsters, which are more specialized
				weight *= 0.8f;
			}
			else if (!HasWaveType(monster.types, MonsterWaveType::Heavy | MonsterWaveType::Elite)) {
				weight *= 0.6f;
			}
			else {
				weight *= 1.0f + ((currentLevel - 15) * 0.02f);
			}
		}

		// Ensure earlier monsters don't become too rare
		if (monster.minWave < currentLevel) {
			float relevance = 1.0f - std::min(0.4f, (currentLevel - monster.minWave) * 0.01f);
			weight *= relevance;
		}

		// Ensure no monster drops below a reasonable minimum weight
		if (weight < 0.2f && monster.weight >= 0.2f) {
			weight = 0.2f;
		}

		// In emergency mode, make weights more uniform
		if (emergency_mode) {
			weight = std::max(weight, 0.5f);
		}

		// Special handling for boss wave minions
		if (currentLevel >= 10 && currentLevel % 5 == 0 && boss_spawned_for_wave) {
			weight *= HasWaveType(monster.types, currentWaveTypes) ? 2.0f : 0.3f;
		}

		// Apply difficulty adjustments
		if (g_insane->integer || g_chaotic->integer) {
			const float difficultyScale = currentLevel <= 10 ? 1.1f :
				currentLevel <= 20 ? 1.2f : currentLevel <= 30 ? 1.3f : 1.4f;
			weight *= difficultyScale;
			if (HasWaveType(monster.types, MonsterWaveType::Elite)) weight *= 1.2f;
		}

		// Apply flying adjustment
		weight *= adjustmentFactor;

		// Higher level monster adjustment
		if (spawn_higher_level && monster.minWave > currentLevel) {
			if (monster.minWave - currentLevel > 5) weight *= 0.5f;
		}

		// Add to cache if weight is valid
		if (weight > 0.0f) {
			monster_cache.total_weight += weight;
			auto& entry = monster_cache.entries[monster_cache.count];
			entry.typeId = monster.typeId;
			entry.weight = weight;
			entry.cumulative_weight = monster_cache.total_weight;
			monster_cache.count++;
		}
	}

	horde::MonsterTypeID chosen_monster = horde::MonsterTypeID::UNKNOWN;
	if (monster_cache.count == 0) {
		// Create a weighted collection of appropriate fallback monsters based on wave level
		WeightedSelection<MonsterTypeInfo> fallback_monsters;

		// Find monsters appropriate for this wave level
		int best_min_wave = 1;

		// First pass - find the highest minWave that's still <= currentLevel
		for (const auto& monster : monsterTypes) {
			if (monster.minWave <= currentLevel && monster.minWave > best_min_wave) {
				best_min_wave = monster.minWave;
			}
		}

		// Allow some wiggle room (prefer monsters with minWave within 5 levels of best)
		int min_acceptable_wave = std::max(1, best_min_wave - 5);

		// Second pass - collect appropriate monsters
		for (const auto& monster : monsterTypes) {
			if (monster.minWave >= min_acceptable_wave && monster.minWave <= currentLevel) {
				// Check basic flying compatibility
				const bool isFlyingMonster = HasWaveType(GetMonsterWaveTypes(monster.typeId), MonsterWaveType::Flying);

				// Only enforce flying vs ground compatibility
				if (!(isSpawnPointFlying && !isFlyingMonster)) {
					// Higher weight for monsters closer to the current level
					float weight = 1.0f + ((monster.minWave - min_acceptable_wave) * 0.5f);
					fallback_monsters.add(&monster, weight);
				}
			}
		}

		// If we found any appropriate monsters, select one randomly (weighted)
		if (fallback_monsters.item_count > 0) {
			const MonsterTypeInfo* selected = fallback_monsters.select();
			if (selected) {
				chosen_monster = selected->typeId;
			}
		}

		// Last-resort fallback if still nothing found
		if (chosen_monster == horde::MonsterTypeID::UNKNOWN) {
			// Find ANY monster that meets basic requirements
			for (const auto& monster : monsterTypes) {
				if (monster.minWave <= currentLevel) {
					const bool isFlyingMonster = HasWaveType(GetMonsterWaveTypes(monster.typeId), MonsterWaveType::Flying);
					if (!(isSpawnPointFlying && !isFlyingMonster)) {
						chosen_monster = monster.typeId;
						break;
					}
				}
			}
		}

		if (chosen_monster != horde::MonsterTypeID::UNKNOWN && developer->integer) {
			gi.Com_PrintFmt("EMERGENCY FALLBACK: Selected {} after {} failed attempts\n",
				horde::MonsterTypeRegistry::GetClassname(chosen_monster), spawn_selection_failures);
		}

		// If we found a monster via emergency fallback
		if (chosen_monster != horde::MonsterTypeID::UNKNOWN) {
			// Update cooldowns - Need to use GetClassname here as UpdateCooldowns requires a classname
			UpdateCooldowns(spawn_point);

			// Reset failure counters on success
			spawn_selection_failures = 0;
			emergency_mode = false;

			// Restore original wave type if we were in emergency mode
			if (saved_wave_type != MonsterWaveType::None) {
				current_wave_type = saved_wave_type;
				saved_wave_type = MonsterWaveType::None;
			}

			return chosen_monster;
		}

		// No monster available even with emergency fallback
		IncreaseSpawnAttempts(spawn_point);
		spawn_selection_failures++;
		return horde::MonsterTypeID::UNKNOWN;
	}

	// Select monster using binary search for better performance
	const float random_value = frandom() * monster_cache.total_weight;
	size_t left = 0;
	size_t right = monster_cache.count - 1;

	while (left < right) {
		const size_t mid = (left + right) / 2;
		if (monster_cache.entries[mid].cumulative_weight < random_value) {
			left = mid + 1;
		}
		else {
			right = mid;
		}
	}

	// Get the selected monster
	chosen_monster = monster_cache.entries[left].typeId;

	// Update cooldowns if we have a valid selection
	if (chosen_monster != horde::MonsterTypeID::UNKNOWN) {
		UpdateCooldowns(spawn_point);
		// Reset failure counters on success
		spawn_selection_failures = 0;
		emergency_mode = false;

		// Restore original wave type if we were in emergency mode
		if (saved_wave_type != MonsterWaveType::None) {
			current_wave_type = saved_wave_type;
			saved_wave_type = MonsterWaveType::None;
		}
	}

	return chosen_monster;
}

void Horde_PreInit() {
	gi.Com_Print("Horde mode must be DM. Set <deathmatch 1> and <horde 1>, then <map mapname>.\n");
	gi.Com_Print("COOP requires <coop 1> and <horde 0>, optionally <g_hardcoop 1/0>.\n");

	g_horde = gi.cvar("horde", "0", CVAR_LATCH);
	//gi.Com_Print("After starting a normal server type: starthorde to start a game.\n");


	if (!g_horde->integer) {
		//deathmatch->integer == 0;
		gi.cvar_forceset("deathmatch", "0");
		return;
	}

	// Configuración automática cuando horde está activo
	if (g_horde->integer) {
		//deathmatch->integer == 1;
		dm_monsters = gi.cvar("dm_monsters", "0", CVAR_SERVERINFO);

		gi.Com_Print("Initializing Horde mode settings...\n");

		// Configuración de tiempo y límites
		gi.cvar_forceset("deathmatch", "1");
		gi.cvar_forceset("coop", "0");
		gi.cvar_forceset("g_teamplay_force_join", "0");
		gi.cvar_forceset("timelimit", "50");
		gi.cvar_forceset("fraglimit", "0");
		gi.cvar_forceset("capturelimit", "0");

		// Configuración de jugabilidad
		gi.cvar_forceset("sv_target_id", "1");
		gi.cvar_forceset("g_speedstuff", "1.8f");
		gi.cvar_forceset("sv_eyecam", "1");
		gi.cvar_forceset("g_dm_instant_items", "1");
		gi.cvar_forceset("g_disable_player_collision", "1");
		gi.cvar_forceset("g_dm_no_self_damage", "1");
		gi.cvar_forceset("g_allow_techs", "1");

		// Configuración de physics
		gi.cvar_forceset("g_override_physics_flags", "-1");

		// Configuración de armas y daño
		gi.cvar_forceset("g_no_nukes", "0");
		gi.cvar_forceset("g_instant_weapon_switch", "1");
		gi.cvar_forceset("g_dm_no_quad_drop", "0");
		gi.cvar_forceset("g_dm_no_quadfire_drop", "0");

		// Configuración del hook/grapple
		gi.cvar_forceset("g_use_hook", "1");
		gi.cvar_forceset("g_hook_wave", "1");
		gi.cvar_forceset("hook_pullspeed", "1200");
		gi.cvar_forceset("hook_speed", "3000");
		gi.cvar_forceset("hook_sky", "1");
		gi.cvar_forceset("g_allow_grapple", "1");
		gi.cvar_forceset("g_grapple_fly_speed", "3000");
		gi.cvar_forceset("g_grapple_pull_speed", "1200");

		// Configuración de gameplay específica
		gi.cvar_forceset("g_startarmor", "0");
		gi.cvar_forceset("g_vampire", "0");
		gi.cvar_forceset("g_ammoregen", "0");
		gi.cvar_forceset("g_tracedbullets", "0");
		gi.cvar_forceset("g_energyshells", "0");
		gi.cvar_forceset("g_bouncygl", "0");
		gi.cvar_forceset("g_bfgpull", "0");
		gi.cvar_forceset("g_bfgslide", "1");
		gi.cvar_forceset("g_energyshells", "0");
		gi.cvar_forceset("g_autohaste", "0");
		gi.cvar_forceset("g_chaotic", "0");
		gi.cvar_forceset("g_insane", "0");
		gi.cvar_forceset("g_hardcoop", "0");

		// Configuración de IA y bots
		gi.cvar_forceset("g_dm_spawns", "0");
		gi.cvar_forceset("g_damage_scale", "1");
		gi.cvar_forceset("ai_allow_dm_spawn", "1");
		gi.cvar_forceset("ai_damage_scale", "1");
		gi.cvar_forceset("g_loadent", "1");
		gi.cvar_forceset("bot_chat_enable", "0");
		gi.cvar_forceset("bot_skill", "5");
		gi.cvar_forceset("g_coop_squad_respawn", "1");
		gi.cvar_forceset("g_iddmg", "1");

		// Activar monstruos automáticamente
		gi.cvar_forceset("dm_monsters", "0");

		// Resetear el estado del juego
		HandleResetEvent();

		// Mensaje de confirmación
		gi.Com_Print("Horde mode initialized successfully.\n");
	}
}

void VerifyAndAdjustBots() {
	if (developer->integer == 2) {
		gi.cvar_set("bot_minClients", "-1");
	}
	else {
		const MapSize mapSize = GetMapSize(static_cast<const char*>(level.mapname));
		const int32_t spectPlayers = GetNumSpectPlayers();
		const int32_t baseBots = mapSize.isBigMap ? 6 : 4;

		// Agregar bot extra si current_wave_level >= 20
		const int32_t extraBot = (current_wave_level >= 20) ? 1 : 0;
		const int32_t requiredBots = std::max(baseBots + spectPlayers + extraBot, baseBots);

		gi.cvar_set("bot_minClients", std::to_string(requiredBots).c_str());
	}
}

void InitializeWaveSystem() noexcept;

// Guard variable
static bool items_precached = false;

// Renamed function for clarity
static void PrecacheAllGameItems() noexcept {
	// Only precache once
	if (items_precached)
		return;

	if (developer->integer) {
		gi.Com_Print("Precaching all game items...\n");
	}

	// Iterate directly through the global itemlist using item_id_t
	// Start from IT_NULL + 1 because index 0 is unused.
	for (item_id_t i = static_cast<item_id_t>(IT_NULL + 1); i < IT_TOTAL; i = static_cast<item_id_t>(i + 1))
	{
		// Get the gitem_t pointer directly using the index (ID)
		gitem_t* item = &itemlist[i]; // Or use GetItemByIndex(i) if preferred

		// Basic validation: Skip if item pointer is null (shouldn't happen)
		// or if it lacks a classname (often indicates an internal/placeholder item)
		if (!item || !item->classname) {
			continue;
		}

		// Call the existing PrecacheItem function, which handles all necessary assets
		// associated with this gitem_t (models, sounds, icons, nested precaches).
		PrecacheItem(item);
	}

	items_precached = true; // Mark as done

	if (developer->integer) {
		gi.Com_Print("Item precaching complete.\n");
	}
}

// Función para precarga de sonidos
static bool sounds_precached = false;

static void PrecacheWaveSounds() noexcept {
	// Only precache once
	if (sounds_precached)
		return;

	// Individual sounds - using an array for better organization
	static const std::array<std::pair<cached_soundindex*, const char*>, 4> individual_sounds = { {
		{&sound_tele3, "misc/r_tele3.wav"},
		{&sound_tele_up, "misc/tele_up.wav"},
		{&sound_spawn1, "misc/spawn1.wav"},
		{&incoming, "world/incoming.wav"}
	} };

	// Precache individual sounds using span
	std::span individual_view{ individual_sounds };
	for (const auto& [sound_index, path] : individual_view) {
		sound_index->assign(path);
	}

	// Precache wave sounds using span
	std::span wave_view{ WAVE_SOUND_PATHS };
	for (size_t i = 0; i < NUM_WAVE_SOUNDS; ++i) {
		wave_sounds[i].assign(wave_view[i]);
	}

	// Precache start sounds using span
	std::span start_view{ START_SOUND_PATHS };
	for (size_t i = 0; i < NUM_START_SOUNDS; ++i) {
		start_sounds[i].assign(start_view[i]);
	}

	sounds_precached = true;
}

// Agregar un nuevo array para tracking
static std::array<bool, NUM_WAVE_SOUNDS> used_wave_sounds = {};
static size_t remaining_wave_sounds = NUM_WAVE_SOUNDS;

static int GetRandomWaveSound() {
	// Si todos los sonidos han sido usados, resetear
	if (remaining_wave_sounds == 0) {
		std::fill(used_wave_sounds.begin(), used_wave_sounds.end(), false);
		remaining_wave_sounds = NUM_WAVE_SOUNDS;
	}

	// Seleccionar un sonido no usado
	while (true) {
		size_t const index = irandom(NUM_WAVE_SOUNDS);
		if (!used_wave_sounds[index]) {
			used_wave_sounds[index] = true;
			remaining_wave_sounds--;
			return wave_sounds[index];
		}
	}
}

static std::array<bool, NUM_START_SOUNDS> used_start_sounds = {};
static size_t remaining_start_sounds = NUM_START_SOUNDS;

static void PlayWaveStartSound() {
	// Si todos los sonidos han sido usados, resetear
	if (remaining_start_sounds == 0) {
		std::fill(used_start_sounds.begin(), used_start_sounds.end(), false);
		remaining_start_sounds = NUM_START_SOUNDS;
	}

	// Seleccionar un sonido no usado
	while (true) {
		size_t const index = irandom(NUM_START_SOUNDS);
		if (!used_start_sounds[index]) {
			used_start_sounds[index] = true;
			remaining_start_sounds--;
			gi.sound(world, CHAN_VOICE, start_sounds[index], 1, ATTN_NONE, 0);
			break;
		}
	}
}
//Capping resets on map end

static bool hasBeenReset = false;
void AllowReset() noexcept {
	hasBeenReset = false;
}

void Horde_Init() {
	horde::InitializeHordeIDs();

	// Reset precache state
	sounds_precached = false;
	items_precached = false;

	// Clear existing bosses
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end();) {
		edict_t* boss = *it;
		if (boss && boss->inuse) {
			boss->monsterinfo.BOSS_DEATH_HANDLED = true;
			OnEntityRemoved(boss);
		}
		it = auto_spawned_bosses.erase(it);
	}
	auto_spawned_bosses.clear();

	// Do precaching
	PrecacheAllGameItems();
	PrecacheWaveSounds();

	// Initialize wave types
	InitializeMonsterWaveTypes();
	InitializeWaveSystem();
	last_wave_number = 0;

	AllowReset();
	ResetGame();

	gi.Com_Print("PRINT: Horde game state initialized with all necessary resources precached.\n");
}

// Helper function to select a suitable boss weapon drop based on wave level
static item_id_t SelectBossWeaponDrop(int32_t wave_level) {
	// Define potential weapon drops with their minimum required wave level
	static const std::array<std::pair<item_id_t, int32_t>, 8> boss_weapon_drops = { {
		{IT_WEAPON_HYPERBLASTER, 9},
		{IT_WEAPON_RLAUNCHER,    9},
		{IT_WEAPON_PHALANX,      9}, // Xatrix
		{IT_WEAPON_IONRIPPER,    9}, // Xatrix (originally boomer)
		{IT_WEAPON_PLASMABEAM,   9}, // Rogue
		{IT_WEAPON_RAILGUN,      9}, // Moved slightly later
		{IT_WEAPON_DISRUPTOR,    19}, // Rogue (originally disintegrator)
		{IT_WEAPON_BFG,          19}  // Moved slightly later
	} };

	// Collect weapons eligible for the current wave level
	std::vector<item_id_t> eligible_weapons;
	eligible_weapons.reserve(boss_weapon_drops.size()); // Pre-allocate memory

	for (const auto& [weapon_id, min_level] : boss_weapon_drops) {
		if (wave_level >= min_level) {
			eligible_weapons.push_back(weapon_id);
		}
	}

	// If no weapons are eligible, return IT_NULL
	if (eligible_weapons.empty()) {
		return IT_NULL;
	}

	// Select a random weapon from the eligible list
	// Use a more robust random number generator if possible, otherwise fallback
	// std::uniform_int_distribution<size_t> dist(0, eligible_weapons.size() - 1);
	// size_t random_index = dist(mt_rand); // Requires <random> and mt_rand setup
	size_t random_index = irandom(0, eligible_weapons.size() - 1); // Using irandom fallback

	// Safety check (shouldn't be needed with correct random range)
	if (random_index >= eligible_weapons.size()) {
		return IT_NULL;
	}

	return eligible_weapons[random_index];
}


// --- Modified BossDeathHandler using item_id_t ---

// Constants for item dropping physics (if not defined globally)
constexpr int MIN_VELOCITY = -800;
constexpr int MAX_VELOCITY = 800;
constexpr int MIN_VERTICAL_VELOCITY = 400;
constexpr int MAX_VERTICAL_VELOCITY = 950;

void BossDeathHandler(edict_t* boss) {
	// Fast early-out with combined validation (no change needed)
	if (!g_horde || !g_horde->integer || !boss || !boss->inuse || !boss->monsterinfo.IS_BOSS ||
		boss->monsterinfo.BOSS_DEATH_HANDLED || boss->health > 0) {
		return;
	}

	// Mark as handled immediately (no change needed)
	boss->monsterinfo.BOSS_DEATH_HANDLED = true;

	// Clean up entity tracking (no change needed)
	OnEntityDeath(boss);
	OnEntityRemoved(boss);
	auto_spawned_bosses.erase(boss); // Ensure this set is accessible

	// --- Use item_id_t for static drop tables ---
	static const std::array<item_id_t, 8> standardItemIDs = {
		IT_ITEM_ADRENALINE, IT_ITEM_PACK, IT_ITEM_SENTRYGUN,
		IT_ITEM_SPHERE_DEFENDER, IT_ARMOR_COMBAT, IT_ITEM_BANDOLIER,
		IT_ITEM_INVULNERABILITY, IT_AMMO_NUKE
	};
	// Note: Ensure the size `8` matches the number of items listed.

	// --- Drop primary weapon using item_id_t ---
	item_id_t weapon_id = SelectBossWeaponDrop(current_wave_level);
	if (weapon_id != IT_NULL) { // Check against IT_NULL
		if (gitem_t* weapon_item = GetItemByIndex(weapon_id)) { // Use GetItemByIndex
			if (edict_t* weapon = Drop_Item(boss, weapon_item)) {
				// Set up weapon visuals/physics (logic remains the same)
				weapon->s.origin = boss->s.origin;
				weapon->velocity = {
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VERTICAL_VELOCITY, MAX_VERTICAL_VELOCITY))
				};
				weapon->movetype = MOVETYPE_BOUNCE;
				weapon->s.effects = EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER;
				weapon->s.renderfx = RF_GLOW;
				weapon->s.alpha = 0.85f;
				weapon->s.scale = 1.25f;
				weapon->spawnflags = SPAWNFLAG_ITEM_DROPPED_PLAYER;
				weapon->flags &= ~FL_RESPAWN;
				gi.linkentity(weapon);
			}
		}
	}

	// --- Drop special power-up using item_id_t ---
	item_id_t special_id = brandom() ? IT_ITEM_QUADFIRE : IT_ITEM_QUAD; // Select ID
	if (gitem_t* special_item = GetItemByIndex(special_id)) { // Use GetItemByIndex
		if (edict_t* powerup = Drop_Item(boss, special_item)) {
			// Set up powerup visuals/physics (logic remains the same)
			powerup->s.origin = boss->s.origin;
			powerup->velocity = {
				static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
				static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
				static_cast<float>(irandom(300, 400))
			};
			powerup->movetype = MOVETYPE_BOUNCE;
			powerup->s.effects = EF_GRENADE_LIGHT | EF_GIB | EF_BLUEHYPERBLASTER | EF_HOLOGRAM;
			powerup->s.alpha = 0.8f;
			powerup->s.scale = 1.5f;
			powerup->flags &= ~FL_RESPAWN;
			gi.linkentity(powerup);
		}
	}

	// --- Randomize and drop standard items using item_id_t ---
	std::array<item_id_t, standardItemIDs.size()> shuffledIDs = standardItemIDs; // Create a mutable copy

	// Shuffle the IDs (using simple irandom-based shuffle as fallback)
	for (size_t i = shuffledIDs.size() - 1; i > 0; --i) {
		size_t j = irandom(0, i); // irandom needs to be inclusive [0, i]
		if (i != j) {
			std::swap(shuffledIDs[i], shuffledIDs[j]);
		}
	}
	// For a better shuffle, use std::shuffle with a proper random engine if available:
	// std::shuffle(shuffledIDs.begin(), shuffledIDs.end(), mt_rand); // Requires setup

	// Drop the shuffled standard items
	for (item_id_t item_id : shuffledIDs) { // Iterate through shuffled IDs
		if (item_id == IT_NULL) continue; // Safety check

		if (gitem_t* item = GetItemByIndex(item_id)) { // Use GetItemByIndex
			if (edict_t* drop = Drop_Item(boss, item)) {
				// Set up standard drop visuals/physics (logic remains the same)
				drop->s.origin = boss->s.origin;
				drop->velocity = {
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VELOCITY, MAX_VELOCITY)),
					static_cast<float>(irandom(MIN_VERTICAL_VELOCITY, MAX_VERTICAL_VELOCITY))
				};
				drop->movetype = MOVETYPE_BOUNCE;
				drop->flags &= ~FL_RESPAWN;
				drop->s.effects |= EF_GIB;
				drop->spawnflags |= SPAWNFLAG_ITEM_DROPPED_PLAYER;
				gi.linkentity(drop);
			}
		}
	}

	// Clean up boss entity (no change needed)
	// Setting solid/takedamage might interfere with death animations
	gi.linkentity(boss); // Re-link just in case state needs update
}

void boss_die(edict_t* boss) {
	if (!boss || !boss->inuse || !g_horde->integer ||
		!boss->monsterinfo.IS_BOSS || boss->health > 0 ||
		boss->monsterinfo.BOSS_DEATH_HANDLED ||
		auto_spawned_bosses.find(boss) == auto_spawned_bosses.end()) {
		return;
	}

	BossDeathHandler(boss);

	// Limpiar la barra de salud
	for (size_t i = 0; i < MAX_HEALTH_BARS; ++i) {
		if (level.health_bar_entities[i] && level.health_bar_entities[i]->enemy == boss) {
			G_FreeEdict(level.health_bar_entities[i]);
			level.health_bar_entities[i] = nullptr;
			break;
		}
	}

	// Limpiar el configstring del nombre de la barra de salud
	gi.configstring(CONFIG_HEALTH_BAR_NAME, "");
}

static bool Horde_AllMonstersDead() {
	// Fast path using level counters
	if (level.total_monsters == level.killed_monsters) {
		return true;
	}

	// Secondary verification by direct entity check
	// Using active_or_dead_monsters() for more complete coverage
	for (auto ent : active_or_dead_monsters()) {
		// Skip invalid entities and non-counting monsters
		if (!ent || !ent->inuse || (ent->monsterinfo.aiflags & AI_DO_NOT_COUNT))
			continue;

		// If any live monster found, return false immediately
		if (!ent->deadflag && ent->health > 0) {
			return false;
		}

		// Handle dying bosses
		if (ent->monsterinfo.IS_BOSS && ent->health <= 0) {
			if (auto_spawned_bosses.find(ent) != auto_spawned_bosses.end() &&
				!ent->monsterinfo.BOSS_DEATH_HANDLED) {
				boss_die(ent);
			}
		}
	}

	// If we get here, no live monsters found
	return true;
}


// Asegúrate de limpiar entidades muertas
void Horde_CleanBodies() {
	for (edict_t* ent : active_or_dead_monsters()) {
		if (ent->deadflag || ent->health <= 0) {
			if (!ent->is_fading_out) { // Only check once before starting fade
				StartFadeOut(ent);
			}
		}
		else { // If the monster is alive but somehow flagged for removal:
			CheckAndRestoreMonsterAlpha(ent); // Restore alpha for live monsters
		}
	}
}

// attaching healthbar
static void AttachHealthBar(edict_t* boss) {
	auto healthbar = G_Spawn();
	if (!healthbar) return;

	healthbar->classname = "target_healthbar";
	// Usar asignación directa y operador de suma de vec3_t
	healthbar->s.origin = boss->s.origin + vec3_t{ 0, 0, 20 };

	healthbar->delay = 2.0f;
	healthbar->target = boss->targetname;

	// Copiar el nombre del jefe correctamente
	std::string boss_name = GetDisplayName(boss);
	healthbar->message = G_CopyString(boss_name.c_str(), TAG_LEVEL);

	SP_target_healthbar(healthbar);
	healthbar->enemy = boss;

	// Llamar a SetHealthBarName después de configurar el mensaje
	SetHealthBarName(boss);

	for (size_t i = 0; i < MAX_HEALTH_BARS; ++i) {
		if (!level.health_bar_entities[i]) {
			level.health_bar_entities[i] = healthbar;
			break;
		}
	}

	healthbar->think = check_target_healthbar;
	healthbar->nextthink = level.time + 20_sec;
}

void BossSpawnThink(edict_t* self); // Forward declaration of the think function
void SP_target_orb(edict_t* ent);

static void SpawnBossAutomatically() {
	// Clear any existing bosses (existing code remains unchanged)
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end(); ) {
		edict_t* existing_boss = *it;
		if (existing_boss && existing_boss->inuse) {
			existing_boss->monsterinfo.BOSS_DEATH_HANDLED = true;
			OnEntityRemoved(existing_boss);
			G_FreeEdict(existing_boss);
		}
		it = auto_spawned_bosses.erase(it);
	}

	// Reset wave type (existing code remains unchanged)
	current_wave_type = MonsterWaveType::None;

	// Clear health bar (existing code remains unchanged)
	for (size_t i = 0; i < MAX_HEALTH_BARS; ++i) {
		if (level.health_bar_entities[i]) {
			G_FreeEdict(level.health_bar_entities[i]);
			level.health_bar_entities[i] = nullptr;
		}
	}

	// Clear health bar name (existing code remains unchanged)
	gi.configstring(CONFIG_HEALTH_BAR_NAME, "");

	// Early return if not a boss wave (existing code remains unchanged)
	if (g_horde_local.level < 10 || g_horde_local.level % 5 != 0) {
		return;
	}

	// Get map name (existing code remains unchanged)
	const char* map_name = GetCurrentMapName();
	horde::MapID mapId = horde::MapOriginRegistry::GetMapID(map_name);

	// MODIFIED CODE: Use the new MapOriginRegistry instead of mapOrigins map
	vec3_t spawn_origin;
	if (mapId == horde::MapID::UNKNOWN || !horde::MapOriginRegistry::GetOrigin(mapId, spawn_origin)) {
		if (developer->integer) {
			gi.Com_PrintFmt("Error: No spawn origin found for map {}\n", map_name);
		}
		return;
	}

	// Validate origin (slightly modified)
	if (!is_valid_vector(spawn_origin) || spawn_origin == vec3_origin) {
		if (developer->integer) {
			gi.Com_PrintFmt("Error: Invalid spawn origin for map {}\n", map_name);
		}
		return;
	}

	// Create orb effect
	edict_t* orb = G_Spawn();
	if (orb) {
		orb->classname = "target_orb";
		orb->s.origin = spawn_origin;
		SP_target_orb(orb);
	}

	// Spawn boss entity
	edict_t* boss = G_Spawn();
	if (!boss) {
		if (orb) G_FreeEdict(orb);
		if (developer->integer) {
			gi.Com_PrintFmt("Error: Failed to spawn boss entity\n");
		}
		return;
	}

	// Use TypeID version for boss selection
	horde::MonsterTypeID boss_type = G_HordePickBOSSType(
		g_horde_local.current_map_size, map_name, g_horde_local.level, boss);

	if (boss_type == horde::MonsterTypeID::UNKNOWN) {
		if (orb) G_FreeEdict(orb);
		G_FreeEdict(boss);
		if (developer->integer) {
			gi.Com_PrintFmt("Error: Failed to pick a boss type\n");
		}
		return;
	}

	// Get the classname from TypeID for engine functions
	const char* boss_classname = horde::MonsterTypeRegistry::GetClassname(boss_type);

	// Set up boss entity
	boss_spawned_for_wave = true;
	boss->classname = boss_classname;
	boss->s.origin = spawn_origin;

	// Push away nearby entities
	constexpr float push_radius = 500.0f;
	constexpr float push_force = 600.0f;
	PushEntitiesAway(spawn_origin, 3, push_radius, push_force, 750.0f, 75.0f);

	// Store orb entity
	boss->owner = orb;

	// Set up delayed spawn
	boss->nextthink = level.time + 750_ms;
	boss->think = BossSpawnThink;
}

// Define the array
static std::array<std::string_view, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_bossMessagesArray;
static bool g_bossMessagesInitialized = false;

// Initialize function
void InitializeBossMessages() {
	if (g_bossMessagesInitialized)
		return;

	// Fill with default message first
	g_bossMessagesArray.fill("A Strogg Boss has spawned!\nPrepare for battle!\n");

	// Set specific messages individually by index
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2)] = "***** Boss incoming! Hornet is here, ready for some fresh Marine meat! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2_KL)] = "***** Boss incoming! Hornet is about to strike! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::FIXBOT_KL)] = "***** Boss incoming! The Fixer is coming to fix this... *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER_MINI)] = "***** Boss incoming! Carrier Mini is delivering pain right to your face! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER)] = "***** Boss incoming! Carrier's here with a deadly payload! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::TANK_64)] = "***** Boss incoming! Tank Commander is here to take limbs! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::SHAMBLER_KL)] = "***** Boss incoming! The Shambler is emerging watch out! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::GUNCMDR_KL)] = "***** Boss incoming! Gunner Commander has you in his sights! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::MAKRON_KL)] = "***** Boss incoming! Makron is here to personally finish you off! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::PSX_GUARDIAN)] = "***** Boss incoming! The Enhanced Guardian is ready to spam rockets! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::BOSS5)] = "***** Boss incoming! Super-Tank is here to show Strogg's might! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::WIDOW2)] = "***** Boss incoming! The Widow is weaving disruptor beams just for you! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::PSX_ARACHNID)] = "***** Boss incoming! Arachnid is here *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::REDMUTANT)] = "***** Boss incoming! The Bloody Mutant is out for blood—yours! *****\n";
	g_bossMessagesArray[static_cast<size_t>(horde::MonsterTypeID::JORG)] = "***** Boss incoming! Jorg's mech is upgraded and deadly! *****\n";

	g_bossMessagesInitialized = true;
}

// Helper function to get message
inline std::string_view GetBossMessage(horde::MonsterTypeID typeId) {
	if (!g_bossMessagesInitialized)
		InitializeBossMessages();

	size_t index = static_cast<size_t>(typeId);
	if (index < g_bossMessagesArray.size())
		return g_bossMessagesArray[index];
	return "A Strogg Boss has spawned!\nPrepare for battle!\n";
}

// Define a struct for boss wave info
struct BossWaveInfo {
	MonsterWaveType waveType;
	const char* message;
};

// Create an array for boss wave types
static std::array<BossWaveInfo, static_cast<size_t>(horde::MonsterTypeID::MAX_TYPES)> g_bossWaveTypeArray;
static bool g_bossWaveTypesInitialized = false;

// Initialize function
void InitializeBossWaveTypes() {
	if (g_bossWaveTypesInitialized)
		return;

	// Fill with default values first
	for (auto& entry : g_bossWaveTypeArray) {
		entry = { MonsterWaveType::Medium, "\n\n\nDefault wave incoming!\n" };
	}

	// Set specific entries
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::REDMUTANT)] =
	{ MonsterWaveType::Mutant, "\n\n\nMutant's invasion approaches!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::SHAMBLER_KL)] =
	{ MonsterWaveType::Shambler, "\n\n\nMutant's invasion approaches!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::WIDOW)] =
	{ MonsterWaveType::Small, "\n\n\nWidow swarm incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::WIDOW2)] =
	{ MonsterWaveType::Small, "\n\n\nWidow swarm incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::PSX_ARACHNID)] =
	{ MonsterWaveType::Arachnophobic, "\n\n\nArachnophobia wave incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2)] =
	{ MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER)] =
	{ MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::CARRIER_MINI)] =
	{ MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS2_KL)] =
	{ MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::FIXBOT_KL)] =
	{ MonsterWaveType::Flying | MonsterWaveType::Boss, "\n\n\nAerial squadron incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::TANK_64)] =
	{ MonsterWaveType::Medium, "\n\n\nHeavy/Mid armored division incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::PSX_GUARDIAN)] =
	{ MonsterWaveType::Medium, "\n\n\nHeavy/Mid armored division incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::BOSS5)] =
	{ MonsterWaveType::Medium, "\n\n\nHeavy/Mid armored division incoming!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::GUNCMDR_KL)] =
	{ MonsterWaveType::Medium, "\n\n\nPrepare bayonets!The invaders are about to get up close and personal!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::MAKRON)] =
	{ MonsterWaveType::Medium, "\n\n\nPrepare bayonets!The invaders are about to get up close and personal!\n" };
	g_bossWaveTypeArray[static_cast<size_t>(horde::MonsterTypeID::MAKRON_KL)] =
	{ MonsterWaveType::Medium, "\n\n\nPrepare bayonets!The invaders are about to get up close and personal!\n" };

	g_bossWaveTypesInitialized = true;
}

// Helper function
inline std::pair<MonsterWaveType, const char*> GetBossWaveType(horde::MonsterTypeID typeId) {
	if (!g_bossWaveTypesInitialized)
		InitializeBossWaveTypes();

	size_t index = static_cast<size_t>(typeId);
	if (index < g_bossWaveTypeArray.size())
		return { g_bossWaveTypeArray[index].waveType, g_bossWaveTypeArray[index].message };
	return { MonsterWaveType::Medium, "\n\n\nDefault wave incoming!\n" };
}

THINK(BossSpawnThink)(edict_t* self) -> void {
	// Remove the black light effect immediately
	if (self->owner) {
		G_FreeEdict(self->owner);
		self->owner = nullptr;
	}

	// Get the monster TypeID directly from the entity's classname
	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);

	// Set wave type based on boss type - using array lookup
	auto bossWaveInfo = GetBossWaveType(typeId);
	if (TrySetWaveType(bossWaveInfo.first)) {
		gi.LocBroadcast_Print(PRINT_CHAT, "{}", bossWaveInfo.second);
	}
	else if (HasWaveType(bossWaveInfo.first, MonsterWaveType::Mutant) ||
		HasWaveType(bossWaveInfo.first, MonsterWaveType::Shambler)) {
		// Fallback for mutant/shambler types
		current_wave_type = MonsterWaveType::Medium;
		StoreWaveType(MonsterWaveType::Medium);
	}

	// Boss spawn message - using array lookup
	std::string_view bossMessage = GetBossMessage(typeId);
	gi.LocBroadcast_Print(PRINT_CHAT, "\n\n\n{}\n", bossMessage.data());

	// Set boss flags in a single group
	self->monsterinfo.IS_BOSS = true;
	self->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	self->monsterinfo.last_sentrygun_target_time = 0_ms;

	// Spawn entity
	{
		self->solid = SOLID_NOT;
		ED_CallSpawn(self);
		ClearSpawnArea(self->s.origin, self->mins, self->maxs);
		self->solid = SOLID_BBOX;
		gi.linkentity(self);
	}

	// Configure boss properties
	ConfigureBossArmor(self);
	ApplyBossEffects(self);
	self->monsterinfo.attack_state = AS_BLIND;

	// Calculate spawn effect sizes once
	const vec3_t spawn_pos = self->s.origin;
	const float magnitude = spawn_pos.length();
	const float base_size = magnitude * 0.35f;
	const float end_size = base_size * 0.005f;

	// Apply visual effects
	ImprovedSpawnGrow(spawn_pos, base_size, end_size, self);
	SpawnGrow_Spawn(spawn_pos, base_size, end_size);

	// Set up health bar and track boss
	AttachHealthBar(self);
	SetHealthBarName(self);
	auto_spawned_bosses.insert(self);
}

bool CheckAndTeleportBoss(edict_t* self, BossTeleportReason reason = BossTeleportReason::DROWNING) {
	// Early returns - unchanged
	if (!self || !self->inuse || self->deadflag ||
		self->monsterinfo.IS_BOSS || level.intermissiontime || !g_horde->integer)
		return false;

	// Get TypeID once instead of multiple string comparisons
	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
	if (typeId == horde::MonsterTypeID::MISC_INSANE || typeId == horde::MonsterTypeID::TURRET) return false;
	// Cache map name once to reduce string comparisons
	static std::string last_map_name;
	static horde::MapID cached_map_id = horde::MapID::UNKNOWN;

	// Only look up map ID if the map has changed
	const char* current_map = GetCurrentMapName();
	if (last_map_name != current_map) {
		last_map_name = current_map;
		cached_map_id = horde::MapOriginRegistry::GetMapID(current_map);

		// If map not found, cache the UNKNOWN ID for quick rejection
		if (cached_map_id == horde::MapID::UNKNOWN)
			return false;
	}
	else if (cached_map_id == horde::MapID::UNKNOWN) {
		// Use cached failure result
		return false;
	}

	// Get the origin and use it directly
	vec3_t spawn_origin;
	if (!horde::MapOriginRegistry::GetOrigin(cached_map_id, spawn_origin)) {
		return false;
	}

	// Select appropriate cooldown based on reason - use constexpr for compiler optimization
	constexpr gtime_t DROWNING_COOLDOWN = 1_sec;
	constexpr gtime_t TRIGGER_COOLDOWN = 3_sec;

	const gtime_t selected_cooldown = reason == BossTeleportReason::DROWNING ?
		DROWNING_COOLDOWN : TRIGGER_COOLDOWN;

	// Check if recently teleported
	if (self->teleport_time && level.time < self->teleport_time + selected_cooldown)
		return false;

	// Verify spawn point validity
	if (!is_valid_vector(spawn_origin) || spawn_origin == vec3_origin)
		return false;

	// Hide from clients before teleport attempt
	self->svflags |= SVF_NOCLIENT;
	gi.unlinkentity(self);

	// Store current velocity and origin
	const vec3_t old_velocity = self->velocity;
	const vec3_t old_origin = self->s.origin;

	// Attempt teleport
	self->s.origin = spawn_origin;
	self->s.old_origin = spawn_origin;
	self->velocity = vec3_origin;

	// Check if new position is valid - use early returns for performance
	bool teleport_success = true;
	if (!(self->flags & (FL_FLY | FL_SWIM))) {
		teleport_success = M_droptofloor(self);
		if (!teleport_success) {
			// Restore on failure
			self->s.origin = old_origin;
			self->s.old_origin = old_origin;
			self->velocity = old_velocity;
			self->svflags &= ~SVF_NOCLIENT;
			gi.linkentity(self);
			return false;
		}
	}

	// Check collisions at new position
	if (gi.trace(self->s.origin, self->mins, self->maxs,
		self->s.origin, self, MASK_MONSTERSOLID).startsolid) {
		// Restore on failure
		self->s.origin = old_origin;
		self->s.old_origin = old_origin;
		self->velocity = old_velocity;
		self->svflags &= ~SVF_NOCLIENT;
		gi.linkentity(self);
		return false;
	}

	// Make visible again before effects
	self->svflags &= ~SVF_NOCLIENT;
	gi.linkentity(self);

	// Visual and sound effects based on reason - use cached display name
	static std::string cached_name;
	if (cached_name.empty() || self->client) {
		cached_name = GetDisplayName(self);
	}

	// Sound effects and messages based on reason
	switch (reason) {
	case BossTeleportReason::DROWNING:
		gi.sound(self, CHAN_AUTO, sound_tele3, 1, ATTN_NORM, 0);
		gi.LocBroadcast_Print(PRINT_HIGH, "{} emerges from the depths!\n", cached_name.c_str());
		break;
	case BossTeleportReason::TRIGGER_HURT:
		gi.sound(self, CHAN_AUTO, sound_tele_up, 1, ATTN_NORM, 0);
		gi.LocBroadcast_Print(PRINT_HIGH, "{} escapes certain death!\n", cached_name.c_str());
		break;
	}

	SpawnGrow_Spawn(self->s.origin, 80.0f, 10.0f);
	self->teleport_time = level.time;

	// Push away nearby entities
	PushEntitiesAway(spawn_origin, 3, 500.0f, 1000.0f, 3750.0f, 1600.0f);

	// Restore some health if heavily damaged - use multiplication instead of division
	if (self->health < (self->max_health >> 2)) { // Bit shift is faster than division by 4
		self->health = (self->max_health >> 2);
	}

	if (developer->integer) {
		const char* reason_str = reason == BossTeleportReason::DROWNING ? "drowning" : "trigger_hurt";
		gi.Com_PrintFmt("Boss teleported due to {} to: {}\n", reason_str, self->s.origin);
	}

	return true;
}

void SetHealthBarName(const edict_t* boss) {
	// Use a static buffer to avoid allocation
	static char buffer[MAX_STRING_CHARS];

	// Early validation
	if (!boss || !boss->inuse) {
		gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, "");
		return;
	}

	// Get name once
	const std::string display_name = GetDisplayName(boss);
	if (display_name.empty()) {
		gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, "");
		return;
	}

	// Calculate safe buffer size once
	const size_t name_len = std::min(display_name.length(),
		MAX_STRING_CHARS - 1);  // -1 for null terminator

	// Copy directly to buffer
	memcpy(buffer, display_name.c_str(), name_len);
	buffer[name_len] = '\0';  // Ensure null termination

	// Set the configstring once
	gi.game_import_t::configstring(CONFIG_HEALTH_BAR_NAME, buffer);
}

// Implementación de UpdateHordeMessage
void UpdateHordeMessage(std::string_view message, gtime_t duration = 5_sec) {
	// Early validation for empty messages
	if (message.empty() || duration <= 0_ms) {
		ClearHordeMessage();
		return;
	}

	// Get current message once
	const char* current_msg = gi.get_configstring(CONFIG_HORDEMSG);

	// Only update if changed
	if (!current_msg || strcmp(current_msg, message.data()) != 0) {
		gi.configstring(CONFIG_HORDEMSG, message.data());
	}

	// Set duration
	horde_message_end_time = level.time + duration;
}

void ClearHordeMessage() {
	std::string_view const current_msg = gi.get_configstring(CONFIG_HORDEMSG);
	if (!current_msg.empty()) {
		gi.configstring(CONFIG_HORDEMSG, "");
		// Usar active_players() para resetear stats
		for (auto* player : active_players()) {
			player->client->ps.stats[STAT_HORDEMSG] = 0;
		}
	}
	horde_message_end_time = 0_sec;
}

// reset cooldowns, fixed no monster spawning on next map
// En UnifiedAdjustSpawnRate y ResetCooldowns:
void ResetCooldowns() noexcept {
	// Instead of clearing the map, we'll reset every entry to default values
	// This is much simpler with the array approach
	for (size_t i = 0; i < MAX_EDICTS; i++) {
		spawnPointsData.data[i] = SpawnPointData{};
	}

	horde::g_monsterSpawnTracker.Reset();
	horde::g_spawnPointTimeTracker.Reset();

	const MapSize& mapSize = g_horde_local.current_map_size;
	const int32_t currentLevel = g_horde_local.level;
	const int32_t humanPlayers = GetNumHumanPlayers();

	// Get base cooldown based on map size
	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);

	// Apply scale based on level
	const float cooldownScale = CalculateCooldownScale(currentLevel, mapSize);
	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);

	// Additional adjustments (reduced but maintained for balance)
	if (humanPlayers > 1) {
		const float playerAdjustment = 1.0f - (std::min(humanPlayers - 1, 3) * 0.05f);
		SPAWN_POINT_COOLDOWN *= playerAdjustment;
	}

	// Difficulty mode adjustments with safety verification
	if ((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) {
		SPAWN_POINT_COOLDOWN *= 0.95f;
	}

	// Apply absolute limits
	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN, 1.0_sec, 3.0_sec);
}

void ResetAllSpawnAttempts() noexcept {
	// Find all active spawn points and reset them
	for (unsigned int i = 1; i < globals.num_edicts; i++) {
		edict_t* ent = &g_edicts[i];
		if (ent && ent->inuse && ent->classname &&
			!strcmp(ent->classname, "info_player_deathmatch")) {
			auto& data = spawnPointsData[ent];
			data.attempts = 0;
			data.isTemporarilyDisabled = false;
			data.cooldownEndsAt = 0_sec;

			// Reset alternative position tracking too
			data.alternative_attempts = 0;
			data.alternative_cooldown = 0_sec;
			data.needs_long_alternative_cooldown = false;
		}
	}
}
void ResetWaveAdvanceState() noexcept;

//
// game resetting
//


// Reset ambush system state
void ResetAmbushSystem() {
	last_ambush_time = 0_sec;
	ambush_cooldown_frames = 0;
	waves_since_ambush = 0;
	ambush_system_initialized = false;
}


void ResetWaveMemory() {
	previous_wave_types.fill(MonsterWaveType::None);
	wave_memory_index = 0;

}

static void ResetRecentBosses() noexcept {
	recent_bosses.clear();
}

static void ResetTeleportTracking() noexcept {
	// Reset recent teleport position history
	std::fill(g_recent_teleport_positions.begin(), g_recent_teleport_positions.end(), RecentTeleportPosition{});
	g_recent_teleport_index = 0;

	// Reset global teleport rate limiting
	HordeConstants::g_teleport_rate_count = 0;
	HordeConstants::g_teleport_rate_reset_time = level.time; // Reset the timer completely

	if (developer && developer->integer > 1) { // Optional debug print
		gi.Com_PrintFmt("Teleport tracking reset.\n");
	}
}

void ResetGame() {

	// Si ya se ha ejecutado una vez, retornar inmediatamente
	if (hasBeenReset) {
		// Avoid excessive printing if not in developer mode
		if (developer && developer->integer > 1) { // Added null check
			gi.Com_PrintFmt("INFO: Reset already performed, skipping...\n");
		}
		return;
	}

	// Establecer el flag al inicio de la ejecución
	hasBeenReset = true;

	if (developer && developer->integer) { // Added null check
		gi.Com_PrintFmt("INFO: Performing full game state reset...\n");
	}


	// --- **NEW SAFETY CLEANUP** ---
		// Iterate through all possible client slots to catch any missed laser managers
	for (size_t i = 0; i < game.maxclients; ++i) { // Use size_t here
		edict_t* ent = &g_edicts[i + 1]; // Corresponding edict_t for the client slot

		// Check directly in the game.clients array first (most reliable during reset)
		if (game.clients[i].laser_manager) {
			// Cast to the correct type
			auto* holder = static_cast<LaserManagerHolder*>(game.clients[i].laser_manager);
			if (holder) {
				// Delete the holder - This now calls ~LaserManagerHolder() correctly
				delete holder;
				if (developer && developer->integer > 1) {
					gi.Com_PrintFmt("Cleaned up LaserManager for client slot {} during ResetGame (via game.clients)\n", i);
				}
			}
			else {
				// This case should ideally not happen if allocation is managed correctly
				if (developer && developer->integer) {
					gi.Com_PrintFmt("Warning: Found NULL LaserManagerHolder pointer for client slot {} during ResetGame (via game.clients)\n", i);
				}
			}
			game.clients[i].laser_manager = nullptr; // Always clear the pointer in the game state
		}
		// Fallback check via edict_t->client (should be less necessary now)
		else if (ent && ent->inuse && ent->client && ent->client->laser_manager) {
			auto* holder = static_cast<LaserManagerHolder*>(ent->client->laser_manager);
			if (holder) {
				delete holder; // Calls destructor
				if (developer && developer->integer > 1) {
					gi.Com_PrintFmt("Cleaned up LaserManager via edict for client slot {} during ResetGame\n", i);
				}
			}
			// else { // Pointer exists in client but holder is null - log if needed }
			ent->client->laser_manager = nullptr; // Always clear the pointer
		}
	}
	// --- End of NEW SAFETY CLEANUP ---

	std::fill(g_recent_spawn_positions.begin(), g_recent_spawn_positions.end(), RecentSpawnPosition{});
	g_recent_position_index = 0; // Reset the index

	// --- Existing Reset Logic ---
	ResetTeleportTracking();
	ResetRecentBosses();
	ResetAmbushSystem();
	ResetWaveMemory();
	ResetChampionMonsterState(); // Add reset for champion state

	// Clear auto_spawned_bosses safely
	for (auto it = auto_spawned_bosses.begin(); it != auto_spawned_bosses.end(); /* no increment here */) {
		edict_t* boss = *it;
		if (boss && boss->inuse) {
			// Ensure boss death logic is finalized if needed, then free
			if (!boss->monsterinfo.BOSS_DEATH_HANDLED) {
				BossDeathHandler(boss); // Attempt final cleanup if not done
			}
			OnEntityRemoved(boss); // Call removal hook
			G_FreeEdict(boss); // Free the entity
		}
		it = auto_spawned_bosses.erase(it); // Erase and advance iterator
	}
	// auto_spawned_bosses set is now empty
	g_adjusted_monster_cap = 0;

	// Reset global variables that ARE accessible
	spawn_point_cache.clear();

	// Reset static counters (only ones accessible at this scope)
	consistent_zero_counts = 0;
	counter_mismatch_frames = 0;

	// Resetear todas las variables globales
	horde_message_end_time = 0_sec;
	g_totalMonstersInWave = 0;

	// Resetear flags de control
	g_maxMonstersReached = false;
	g_lowPercentageTriggered = false;

	// Limpiar cachés
	CleanupSpawnPointCache();
	spawnPointsData.clear(); // Assuming .clear() zeros out the array entries
	horde::g_monsterSpawnTracker.Reset();
	horde::g_spawnPointTimeTracker.Reset();

	// Reset static function variables by setting flags
	need_spawn_cache_reset = true; // Will trigger cache rebuild on next SpawnMonsters
	need_frame_timer_reset = true; // Will reset frame timers where checked
	need_queue_monitor_reset = true; // Will reset queue monitor where checked


	// Reiniciar variables de estado global
	g_horde_local = HordeState(); // Reset the main state struct
	current_wave_level = 0;       // Explicitly reset wave level
	//last_wave_number = 0;         // Reset last wave number too
	boss_spawned_for_wave = false;
	next_wave_message_sent = false;
	allowWaveAdvance = false;

	// Reiniciar otras variables relevantes
	SPAWN_POINT_COOLDOWN = 2.8_sec; // Reset to default

	// Resetear el estado de las condiciones
	g_horde_local.conditionTriggered = false;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTimeThreshold = 0_sec;
	g_horde_local.waveEndTime = 0_sec;
	g_horde_local.timeWarningIssued = false;
	for (auto& flag : g_horde_local.warningIssued) flag = false; // Reset all warning flags


	// Resetear cualquier otro estado específico de la ola según sea necesario
	current_wave_type = MonsterWaveType::None;

	// Reset core gameplay elements
	ResetAllSpawnAttempts();
	ResetCooldowns(); // This recalculates SPAWN_POINT_COOLDOWN based on map/level 0
	ResetBenefits();

	// Reiniciar la lista de bosses recientes
	ResetRecentBosses();

	// Reiniciar wave advance state
	ResetWaveAdvanceState();

	// Reset wave information (redundant with g_horde_local = HordeState() above, but safe)
	g_horde_local.level = 0; // Ensure level is 0
	g_horde_local.state = horde_state_t::warmup; // Set game state to warmup
	g_horde_local.warm_time = level.time + 4_sec; // Reiniciar el tiempo de warmup
	g_horde_local.monster_spawn_time = level.time; // Reiniciar el tiempo de spawn de monstruos
	g_horde_local.num_to_spawn = 0;
	g_horde_local.queued_monsters = 0;

	// Re-initialize map size dependent variables AFTER resetting state
	g_horde_local.update_map_size(GetCurrentMapName());



	gi.cvar_set("bot_pause", "0");

	// Reset gameplay configuration variables (Use gi.cvar_set for standard vars)
	if (g_chaotic) gi.cvar_set("g_chaotic", "0");
	if (g_insane) gi.cvar_set("g_insane", "0");
	if (g_hardcoop) gi.cvar_set("g_hardcoop", "0");
	if (dm_monsters) gi.cvar_set("dm_monsters", "0");
	if (timelimit) gi.cvar_set("timelimit", "60");
	gi.cvar_set("cheats", "0"); // Use standard 'cheats' cvar name
	if (ai_damage_scale) gi.cvar_set("ai_damage_scale", "1");
	if (ai_allow_dm_spawn) gi.cvar_set("ai_allow_dm_spawn", "1");
	if (g_damage_scale) gi.cvar_set("g_damage_scale", "1");

	// Reset bonuses (Use gi.cvar_set)
	if (g_vampire) gi.cvar_set("g_vampire", "0");
	if (g_startarmor) gi.cvar_set("g_startarmor", "0");
	if (g_ammoregen) gi.cvar_set("g_ammoregen", "0");
	if (g_upgradeproxs) gi.cvar_set("g_upgradeproxs", "0");
	if (g_piercingbeam) gi.cvar_set("g_piercingbeam", "0");
	if (g_tracedbullets) gi.cvar_set("g_tracedbullets", "0");
	if (g_energyshells) gi.cvar_set("g_energyshells", "0");
	if (g_bouncygl) gi.cvar_set("g_bouncygl", "0");
	if (g_bfgpull) gi.cvar_set("g_bfgpull", "0");
	if (g_bfgslide) gi.cvar_set("g_bfgslide", "1"); // Default is often 1
	if (g_autohaste) gi.cvar_set("g_autohaste", "0");

	// Reset sound tracking
	std::fill(used_wave_sounds.begin(), used_wave_sounds.end(), false);
	remaining_wave_sounds = NUM_WAVE_SOUNDS;
	std::fill(used_start_sounds.begin(), used_start_sounds.end(), false);
	remaining_start_sounds = NUM_START_SOUNDS;

	// Clear Horde message
	ClearHordeMessage();
	g_horde_local.reset_hud_state(); // Reset HUD tracking


	// Registrar el reinicio
	gi.Com_PrintFmt("INFO: Horde game state reset complete.\n");
}

// Replace the existing CalculateRemainingMonsters() function
int32_t CalculateRemainingMonsters() noexcept {
	// Simple counter approach - more reliable from old version
	const int32_t remaining = level.total_monsters - level.killed_monsters;
	return std::max(0, remaining);  // Ensure non-negative
}
// Enhanced version of CheckRemainingMonstersCondition
static bool CheckRemainingMonstersCondition(const MapSize& mapSize, WaveEndReason& reason) {
	// Cache frequently used values
	const gtime_t currentTime = level.time;
	const int32_t remainingMonsters = CalculateRemainingMonsters();

	// Transition timer safely when deployment completes
	if (next_wave_message_sent && !g_horde_local.conditionTriggered) {
		// Reset the independent timer when deployment completes
		g_independent_timer_start = currentTime;
		g_horde_local.waveEndTime = currentTime + g_lastParams.timeThreshold;
		g_horde_local.conditionTriggered = true;
		g_horde_local.conditionTimeThreshold = g_lastParams.timeThreshold;

		if (developer->integer) {
			gi.Com_PrintFmt("Debug: Timer reset after wave deployment. New end time: {:.2f}s\n",
				g_lastParams.timeThreshold.seconds());
		}
	}

	// First priority - all monsters are dead or wave advance flag set (checking this first is more efficient)
	if (allowWaveAdvance || Horde_AllMonstersDead()) {
		reason = WaveEndReason::AllMonstersDead;
		ResetWaveAdvanceState();
		return true;
	}

	// Second priority - independent time limit
	if (currentTime >= g_independent_timer_start + g_lastParams.independentTimeThreshold) {
		reason = WaveEndReason::TimeLimitReached;
		return true;
	}

	// Calculate percentage remaining (for conditional timers)
	// Make sure g_totalMonstersInWave is at least 1 to avoid division by zero
	uint16_t safeTotal = g_totalMonstersInWave;
	if (safeTotal < 1) safeTotal = 1;
	const float percentageRemaining = static_cast<float>(remainingMonsters) / safeTotal;

	// Initialize end time if needed
	if (g_horde_local.waveEndTime == 0_sec) {
		g_horde_local.waveEndTime = g_independent_timer_start + g_lastParams.independentTimeThreshold;
	}

	// Check if we should trigger conditional timers
	if (!g_horde_local.conditionTriggered && !next_wave_message_sent) {
		const bool maxMonstersReached = remainingMonsters <= g_lastParams.maxMonsters;
		const bool lowPercentageReached = percentageRemaining <= g_lastParams.lowPercentageThreshold;

		if (maxMonstersReached || lowPercentageReached) {
			g_horde_local.conditionTriggered = true;
			g_horde_local.conditionStartTime = currentTime;

			// Choose appropriate timer threshold based on conditions
			if (maxMonstersReached && lowPercentageReached) {
				// Both conditions met - use the shorter timer
				g_horde_local.conditionTimeThreshold = std::min(
					g_lastParams.timeThreshold, g_lastParams.lowPercentageTimeThreshold);
			}
			else {
				// Only one condition met
				g_horde_local.conditionTimeThreshold = maxMonstersReached ?
					g_lastParams.timeThreshold : g_lastParams.lowPercentageTimeThreshold;
			}

			g_horde_local.waveEndTime = currentTime + g_horde_local.conditionTimeThreshold;

			// Apply special handling for high waves with few monsters
			if (current_wave_level >= 15 && remainingMonsters <= 5) {
				// More aggressive timeout for higher waves with few monsters
				const float reduction_factor = 0.6f;
				g_horde_local.waveEndTime = currentTime +
					(g_horde_local.conditionTimeThreshold * reduction_factor);

				if (developer->integer) {
					gi.Com_PrintFmt("High wave with few monsters: reduced timeout by {}%\n",
						static_cast<int>((1.0f - reduction_factor) * 100));
				}
			}
		}
	}

	// Apply aggressive time reduction for few monsters
	// This happens even if conditions are already triggered
	if (g_horde_local.conditionTriggered && remainingMonsters <= MONSTERS_FOR_AGGRESSIVE_REDUCTION) {

		// --- MODIFIED AGGRESSIVE TIME CALCULATION ---

		// Base calculation - smoother scaling, slightly increased base time
		// float base_time = 3.0f + (remainingMonsters * 0.8f); // Original
		float base_time = 4.0f + (remainingMonsters * 1.0f); // Increased base and per-monster time (e.g., 5s for 1, 7s for 3)

		// Map Size Multiplier: Give more time on smaller maps
		float map_size_multiplier = 1.0f;
		if (mapSize.isSmallMap) {
			map_size_multiplier = 1.3f; // 30% more time for small maps
		}
		else if (mapSize.isMediumMap) {
			map_size_multiplier = 1.15f; // 15% more time for medium maps
		}
		// Big maps use 1.0x multiplier (no change)

		base_time *= map_size_multiplier; // Apply the map size adjustment

		// --- END MODIFIED AGGRESSIVE TIME CALCULATION ---


		// Boss wave consideration - give substantially more time (Keep this logic)
		if (IsBossWave() && boss_spawned_for_wave) {
			base_time *= 2.0f + (0.2f * remainingMonsters);
			base_time = std::max(base_time, 8.0f); // Keep minimum for boss waves
		}
		else {
			// Non-boss wave adjustments

			// Wave level factor - higher waves get less time (Keep this logic)
			if (current_wave_level >= 15) {
				float reduction = std::min((current_wave_level - 15) * 0.02f, 0.3f);
				base_time *= (1.0f - reduction);
			}

			// Player count consideration (Keep this logic)
			int32_t playerCount = GetNumHumanPlayers();
			if (playerCount > 1) {
				float player_reduction = std::min((playerCount - 1) * 0.07f, 0.2f);
				base_time *= (1.0f - player_reduction);
			}
		}

		// Ensure minimum time thresholds (slightly increased for non-boss waves)
		// float min_time = (IsBossWave() && boss_spawned_for_wave) ? 5.0f : 2.0f; // Original
		float min_time = (IsBossWave() && boss_spawned_for_wave) ? 5.0f : 3.0f; // Increased min for non-boss
		// Adjust min_time based on map size as well? Optional, but let's try it:
		if (!IsBossWave() || !boss_spawned_for_wave) {
			min_time *= map_size_multiplier; // Scale the minimum too
		}
		gtime_t aggressive_time = gtime_t::from_sec(std::max(min_time, base_time));

		// Calculate how much original time remains (Keep this logic)
		const gtime_t original_remaining = g_horde_local.waveEndTime - currentTime;

		// Only apply reduction if it's actually faster than current end time (Keep this logic)
		if (aggressive_time < original_remaining) {
			g_horde_local.waveEndTime = currentTime + aggressive_time;

			if (developer->integer) {
				gi.Com_PrintFmt("Aggressive time reduction: {:.1f}s remaining (Map Size Mult: {:.2f}) for {} monsters (wave: {}, boss: {})\n",
					aggressive_time.seconds(),
					map_size_multiplier, // Add multiplier to debug output
					remainingMonsters, current_wave_level,
					(IsBossWave() && boss_spawned_for_wave) ? "yes" : "no");
			}
		}
	}

	// Handle time warnings
	if (g_horde_local.conditionTriggered) {
		const gtime_t remainingTime = g_horde_local.waveEndTime - currentTime;

		for (size_t i = 0; i < WARNING_TIMES.size(); ++i) {
			if (!g_horde_local.warningIssued[i] &&
				remainingTime <= gtime_t::from_sec(WARNING_TIMES[i]) &&
				remainingTime > gtime_t::from_sec(WARNING_TIMES[i]) - 1_sec) {
				gi.LocBroadcast_Print(PRINT_HIGH, "{} seconds remaining!\n",
					static_cast<int>(WARNING_TIMES[i]));
				g_horde_local.warningIssued[i] = true;
			}
		}

		// Check if time has expired
		if (currentTime >= g_horde_local.waveEndTime) {
			reason = WaveEndReason::MonstersRemaining;
			return true;
		}
	}

	// Additional check for wave 15+ with very few monsters and slow progress
	if (current_wave_level >= 15 && remainingMonsters <= 3 && g_horde_local.conditionTriggered) {
		// Get elapsed time since condition triggered
		const gtime_t elapsed = currentTime - g_horde_local.conditionStartTime;

		// If we've spent over 70% of allowed time and few monsters remain, force completion
		if (elapsed >= (g_horde_local.conditionTimeThreshold * 0.7f)) {
			//if (developer->integer) {
			//    gi.Com_PrintFmt("High wave failsafe: Only {} monsters remain after {}% of timer\n",
			//        remainingMonsters,
			//        static_cast<int>((elapsed / g_horde_local.conditionTimeThreshold) * 100));
			//}
			reason = WaveEndReason::MonstersRemaining;
			return true;
		}
	}

	return false;
}
void ResetWaveAdvanceState() noexcept {
	// Reset independent timer
	g_independent_timer_start = level.time;

	// Reset condition variables
	g_horde_local.conditionTriggered = false;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTimeThreshold = 0_sec;
	g_horde_local.timeWarningIssued = false;
	g_horde_local.waveEndTime = 0_sec;

	// Make sure warning flags are reset
	for (size_t i = 0; i < 4; i++) {
		g_horde_local.warningIssued[i] = false;
	}

	allowWaveAdvance = false;
	g_horde_local.lastPrintTime = 0_sec;
	g_totalMonstersInWave = 0;
	boss_spawned_for_wave = false;

	g_lastWaveNumber = -1;
	g_lastNumHumanPlayers = -1;

	// Reset monster detection variables
	consistent_zero_counts = 0;
	counter_mismatch_frames = 0;
}

void AllowNextWaveAdvance() noexcept {
	allowWaveAdvance = true;
}

void fastNextWave() noexcept {
	g_horde_local.monster_spawn_time = level.time;
	g_horde_local.warm_time = level.time;

	// Permitir el avance inmediato
	allowWaveAdvance = true;

	// Resetear variables importantes
	g_horde_local.num_to_spawn = 0;
	g_horde_local.queued_monsters = 0;

	g_horde_local.conditionTriggered = true;
	g_horde_local.waveEndTime = level.time;

	// Limpiar cualquier mensaje pendiente
	ClearHordeMessage();

	g_horde_local.state = horde_state_t::spawning;
	Horde_InitLevel(g_horde_local.level + 1);
}
inline int8_t GetNumActivePlayers() {
	const auto& players = active_players();
	return std::count_if(players.begin(), players.end(),
		[](const edict_t* const player) {
			return player->client && player->client->resp.ctf_team == CTF_TEAM1;
		});
}

inline int8_t GetNumHumanPlayers() {
	const auto& players = active_players_no_spect();
	return std::count_if(players.begin(), players.end(),
		[](const edict_t* const player) {
			return !(player->svflags & SVF_BOT);
		});
}

inline int8_t GetNumSpectPlayers() {
	const auto& players = active_players();
	return std::count_if(players.begin(), players.end(),
		[](const edict_t* const player) noexcept {
			return ClientIsSpectating(player->client);
		});
}

// Implementación de DisplayWaveMessage
static void DisplayWaveMessage(gtime_t duration = 5_sec) {
	static const std::array<const char*, 4> messages = {
		"Horde Menu available upon opening Inventory or using TURTLE on POWERUP WHEEL\n\nMAKE THEM PAY!\n",
		"Welcome to Hell.\n\nUse FlipOff <Key> looking at walls to spawn lasers (cost: 25 cells)\n",
		"New Tactics!\n\nTeslas can now be placed on walls and ceilings!\n\nUse them wisely!",
		"Improved Traps!\n\nTraps are reutilizable after 5secs of eating a strogg!\n\nExploding if strogg is bigger!, up to 60 seconds of life!"
	};

	// Usar distribución uniforme con mt_rand
	std::uniform_int_distribution<size_t> dist(0, messages.size() - 1);
	const size_t choice = dist(mt_rand);
	UpdateHordeMessage(messages[choice], duration);
}

void HandleWaveCleanupMessage(const MapSize& mapSize, const WaveEndReason reason) noexcept {
	// Obtener el número de jugadores humanos
	const int8_t numHumanPlayers = GetNumHumanPlayers();

	// Si la ola terminó con todos los monstruos muertos, aplicar reglas normales
	if (reason == WaveEndReason::AllMonstersDead) {
		if (current_wave_level >= 15 && current_wave_level <= 26) {
			gi.cvar_set("g_insane", "1");
			gi.cvar_set("g_chaotic", "0");
		}
		else if (current_wave_level >= 27) {
			gi.cvar_set("g_insane", "2");
			gi.cvar_set("g_chaotic", "0");
		}
		else if (current_wave_level <= 14) {
			gi.cvar_set("g_insane", "0");
			// Activar chaotic2 si es mapa pequeño Y hay 2+ jugadores, sino chaotic1
			gi.cvar_set("g_chaotic", (mapSize.isSmallMap && numHumanPlayers >= 2) ? "2" : "1");
		}
	}
	else {
		// Si la ola no terminó por victoria completa, pequeña probabilidad de mantener la dificultad
		const float probability = mapSize.isBigMap ? 0.3f :
			mapSize.isSmallMap ? 0.2f : 0.25f;  // 20-30% según tamaño de mapa
		if (frandom() < probability) {
			// Si gana la probabilidad, aplicar la dificultad según el nivel actual
			if (current_wave_level >= 15 && current_wave_level <= 26) {
				gi.cvar_set("g_insane", "1");
				gi.cvar_set("g_chaotic", "0");
			}
			else if (current_wave_level >= 27) {
				gi.cvar_set("g_insane", "2");
				gi.cvar_set("g_chaotic", "0");
			}
			else if (current_wave_level <= 14) {
				gi.cvar_set("g_insane", "0");
				// Activar chaotic2 si es mapa pequeño Y hay 3+ jugadores, sino chaotic1
				gi.cvar_set("g_chaotic", (mapSize.isSmallMap && numHumanPlayers >= 3) ? "2" : "1");
			}
		}
		else {
			// Si no gana la probabilidad, desactivar ambos modos
			gi.cvar_set("g_insane", "0");
			gi.cvar_set("g_chaotic", "0");
		}
	}
	g_horde_local.state = horde_state_t::rest;
}

static void AnnounceIncomingWave(gtime_t duration = 3_sec) {
	const char* message;

	if (g_chaotic->integer > 0 && g_horde_local.level >= 5) {  // Solo después de la ola 5
		if (g_chaotic->integer == 2) {
			message = brandom() ?
				"***RELENTLESS WAVE INCOMING***\n\nSTAND YOUR GROUND!\n" :
				"***OVERWHELMING FORCES APPROACHING***\n\nHOLD THE LINE!\n";
		}
		else {
			message = brandom() ?
				"***CHAOTIC WAVE INCOMING***\n\nSTEEL YOURSELF!\n" :
				"***CHAOS APPROACHES***\n\nREADY FOR BATTLE!\n";
		}
	}
	else if (g_insane->integer > 0) {
		if (g_insane->integer == 2) {
			message = brandom() ?
				"***MERCILESS WAVE INCOMING***\n\nNO RETREAT!\n" :
				"***DEADLY WAVE APPROACHES***\n\nFIGHT TO SURVIVE!\n";
		}
		else {
			message = brandom() ?
				"***INTENSE WAVE INCOMING***\n\nSHOW NO MERCY!\n" :
				"***FIERCE BATTLE AHEAD***\n\nSTAND READY!\n";
		}
	}
	else {
		message = brandom() ?
			"STROGGS STARTING TO PUSH!\n\nSTAY ALERT!\n" :
			"PREPARE FOR INCOMING WAVE!\n\nHOLD POSITION!\n";
	}

	for (auto player : active_players()) {
		if (player->client) {
			player->client->total_damage = 0;
		}
	}

	UpdateHordeMessage(message, duration);

	g_independent_timer_start = level.time;
	g_horde_local.waveEndTime = 0_sec;
	g_horde_local.conditionTriggered = false;
	g_horde_local.conditionStartTime = 0_sec;
	g_horde_local.conditionTimeThreshold = 0_sec;

	// Resetear las advertencias usando un bucle for simple
	for (size_t i = 0; i < 4; i++) {
		g_horde_local.warningIssued[i] = false;
	}

	gi.sound(world, CHAN_VOICE, GetRandomWaveSound(), 1, ATTN_NONE, 0);
}

void InitializeWaveSystem() noexcept {
	PrecacheWaveSounds();
}

static void SetMonsterArmor(edict_t* monster);
static void SetNextMonsterSpawnTime(const MapSize& mapSize);

struct StuckMonsterSpawnFilter {
	bool operator()(edict_t* ent) const {
		if (!ent || !ent->inuse || !ent->classname ||
			strcmp(ent->classname, "info_player_deathmatch") != 0 ||
			ent->style == 1)  // Exclude flying spawns
			return false;

		// Cooldown check - direct array access
		if (level.time < spawnPointsData[ent].teleport_cooldown)
			return false;

		if (IsSpawnPointOccupied(ent))
			return false;

		// Check proximity to players
		for (const auto* const player : active_players_no_spect()) {
			if ((ent->s.origin - player->s.origin).length() < 512.0f) {
				return true;  // Accept spawn points near players
			}
		}
		return false; // No player nearby
	}
};

// Function to attempt dropping a monster to the floor
bool AttemptDropToFloor(vec3_t& position, const vec3_t& mins, const vec3_t& maxs);

edict_t* G_FindEntityInBox(const vec3_t& mins, const vec3_t& maxs, std::function<bool(edict_t*)> predicate) {

	// Static buffer to hold entities found by BoxEdicts.
	// Size should be reasonable, but doesn't need to be MAX_EDICTS.
	static edict_t* entity_list[128]; // Buffer for up to 128 entities in the box
	constexpr size_t max_entities_in_list = std::size(entity_list);

	// Data structure for the BoxEdicts filter
	struct FindData {
		std::function<bool(edict_t*)> predicate;
		edict_t* found_entity = nullptr;
	} find_data;

	find_data.predicate = predicate;

	// Use BoxEdicts with a filter that applies the predicate
	gi.BoxEdicts(mins, maxs, entity_list, max_entities_in_list, AREA_SOLID,
		// Lambda filter for BoxEdicts
		[](edict_t* ent, void* data) -> BoxEdictsResult_t {
			FindData* fd = static_cast<FindData*>(data);

			// Check if the entity is valid and matches the predicate
			if (ent && ent->inuse && fd->predicate(ent)) {
				fd->found_entity = ent;       // Store the found entity
				return BoxEdictsResult_t::End; // Stop searching
			}
			return BoxEdictsResult_t::Skip; // Continue searching
		},
		&find_data // Pass the predicate and result storage
	);

	// Return the entity found by the filter (or nullptr if none matched)
	return find_data.found_entity;
}


// --- FindEmergencySpawnPosition Function ---
bool FindEmergencySpawnPosition(vec3_t& position, vec3_t& angles, bool& used_human_player, horde::MonsterTypeID typeId) {
	if (developer->integer > 1) gi.Com_PrintFmt("DEBUG: Starting FindEmergencySpawnPosition for TypeID {}\n", static_cast<int>(typeId));
	used_human_player = false;

	// Constants
	constexpr int MAX_ATTEMPTS_PER_PLAYER = 8;
	constexpr float FAR_RADIUS_MAX = 1200.0f; // Max generation radius
	const vec3_t monster_mins = HordeConstants::VALIDATE_CHECK_MINS; // Use standard bounds for checks
	const vec3_t monster_maxs = HordeConstants::VALIDATE_CHECK_MAXS;
	bool is_flying = IsFlying(typeId);

	// Player categorization (as before)
	constexpr size_t MAX_PLAYERS = MAX_CLIENTS;
	edict_t* top_damage_humans[MAX_PLAYERS] = { nullptr };
	edict_t* high_spree_humans[MAX_PLAYERS] = { nullptr };
	edict_t* normal_humans[MAX_PLAYERS] = { nullptr };
	edict_t* bots[MAX_PLAYERS] = { nullptr };
	size_t top_damage_count = 0, high_spree_count = 0, normal_count = 0, bot_count = 0;
	int32_t highest_damage = 0; int highest_spree = 0;
	for (auto* player : active_players()) { /* ... (categorization logic unchanged) ... */
		if (!player || !player->inuse || !player->client || player->health <= 0 || ClientIsSpectating(player->client)) continue;
		bool is_human = !(player->svflags & SVF_BOT);
		int32_t player_damage = player->client->total_damage; int player_spree = player->client->resp.spree;
		if (is_human) {
			highest_damage = std::max(highest_damage, player_damage); highest_spree = std::max(highest_spree, player_spree);
			if (player_damage > 0 && player_damage >= highest_damage * 0.7f && top_damage_count < MAX_PLAYERS) top_damage_humans[top_damage_count++] = player;
			else if (player_spree >= 20 && high_spree_count < MAX_PLAYERS) high_spree_humans[high_spree_count++] = player;
			else if (normal_count < MAX_PLAYERS) normal_humans[normal_count++] = player;
		}
		else if (bot_count < MAX_PLAYERS) bots[bot_count++] = player;
	}
	if (top_damage_count == 0 && high_spree_count == 0 && normal_count == 0 && bot_count == 0) { if (developer->integer) gi.Com_PrintFmt("FindEmergencySpawnPosition: No active players/bots found.\n"); return false; }
	struct PlayerGroup { edict_t** players; size_t count; bool is_human; };
	PlayerGroup priority_groups[] = { { top_damage_humans, top_damage_count, true }, { high_spree_humans, high_spree_count, true }, { normal_humans, normal_count, true }, { bots, bot_count, false } };


	// --- Iterate Through Priority Groups ---
	for (const auto& group : priority_groups) {
		if (group.count == 0) continue;
		std::shuffle(group.players, group.players + group.count, mt_rand); // Shuffle within group

		for (size_t player_idx = 0; player_idx < group.count; ++player_idx) {
			edict_t* player = group.players[player_idx];
			if (!player || !player->inuse) continue;

			for (int attempt = 0; attempt < MAX_ATTEMPTS_PER_PLAYER; ++attempt) {
				// Generate candidate position radially
				float radius = HordeConstants::MIN_PLAYER_DIST_GENERATE + frandom() * (FAR_RADIUS_MAX - HordeConstants::MIN_PLAYER_DIST_GENERATE);
				float angle = frandom() * 2.0f * PI;
				vec3_t candidate_pos = {
					player->s.origin[0] + cosf(angle) * radius,
					player->s.origin[1] + sinf(angle) * radius,
					player->s.origin[2] + frandom(8.0f, 48.0f) // Vertical variation
				};

				// --- Step 1: Geometric Validation ---
				if (IsValidSpawnLocation(candidate_pos, monster_mins, monster_maxs, is_flying)) {
					// 'candidate_pos' might now be adjusted (e.g., dropped to floor)

					// --- Step 2: Proximity Checks (After Geometric Validation) ---
					// Check vs Target Player
					if ((candidate_pos - player->s.origin).lengthSquared() < HordeConstants::MIN_PLAYER_DIST_SQ_CHECK) {
						if (developer->integer > 2) gi.Com_PrintFmt("FindEmergencySpawnPosition: Candidate {} too close to target player {}.\n", candidate_pos, player->client->pers.netname);
						continue; // Too close to target, try another attempt
					}
					// Check vs Recent Teleports
					if (IsPositionTooCloseToRecentTeleport(candidate_pos)) {
						if (developer->integer > 2) gi.Com_PrintFmt("FindEmergencySpawnPosition: Candidate {} too close to recent teleport.\n", candidate_pos);
						continue; // Too close to recent teleport
					}
					// Check vs Recent Regular Spawns
					if (IsPositionTooCloseToRecentSpawn(candidate_pos)) {
						if (developer->integer > 2) gi.Com_PrintFmt("FindEmergencySpawnPosition: Candidate {} too close to recent regular spawn.\n", candidate_pos);
						continue; // Too close to recent regular spawn
					}

					// --- SUCCESS! ---
					// All checks passed: geometric validation and all proximity checks
					position = candidate_pos; // Use the final validated & checked position
					vec3_t dir = player->s.origin - position;
					dir.z *= 0.1f; // Aim slightly towards player horizontally
					angles = vectoangles(dir);
					used_human_player = group.is_human;

					if (developer->integer) gi.Com_PrintFmt("FindEmergencySpawnPosition: Success! Found valid pos {} near {} {}.\n", position, group.is_human ? "human" : "bot", player->client->pers.netname);
					return true; // Found a valid position
				}
				// else: IsValidSpawnLocation failed, try next attempt
			} // End attempts loop for this player
		} // End player loop for this group
	} // End group iteration loop

	if (developer->integer) gi.Com_PrintFmt("FindEmergencySpawnPosition: Failed after all attempts.\n");
	return false; // Failed to find a suitable position
}

// String-based overload that delegates to the TypeID version
bool FindEmergencySpawnPosition(vec3_t& position, vec3_t& angles, bool& used_human_player, const char* monster_classname)
{
	// Convert classname to TypeID
	horde::MonsterTypeID typeId = monster_classname ?
		horde::MonsterTypeRegistry::GetTypeID(monster_classname) :
		horde::MonsterTypeID::UNKNOWN;

	// Delegate to the TypeID version
	return FindEmergencySpawnPosition(position, angles, used_human_player, typeId);
}

// Helper function to check if any human (non-bot) players are active and not spectating
bool AreHumanPlayersPresent() {
	for (auto* player : active_players()) {
		if (player && player->inuse && player->client &&
			!(player->svflags & SVF_BOT) &&
			!ClientIsSpectating(player->client) &&
			player->health > 0) {
			return true;
		}
	}
	return false;
}

static BoxEdictsResult_t SpawnPointFilter(edict_t* ent, void* data) {
	FilterData* filter_data = static_cast<FilterData*>(data);

	if (ent == filter_data->ignore_ent) { // Ignore self if specified
		return BoxEdictsResult_t::Skip;
	}
	if (ent->client && ent->inuse) { // Player/Bot check
		filter_data->count++;
		return BoxEdictsResult_t::End;
	}
	if ((ent->svflags & SVF_MONSTER) && ent->inuse && !ent->deadflag) { // Live Monster check
		filter_data->count++;
		return BoxEdictsResult_t::End;
	}
	if (ent->inuse && IsPlayerDefense(ent)) { // Player Defense check
		filter_data->count++;
		return BoxEdictsResult_t::End;
	}
	return BoxEdictsResult_t::Skip;
}

// --- IsValidSpawnLocation Function ---
[[nodiscard]] bool IsValidSpawnLocation(vec3_t& io_position, const vec3_t& monster_mins, const vec3_t& monster_maxs, bool is_flying) {
	// Define the mask to use for world geometry checks
	constexpr contents_t GEOMETRY_MASK = MASK_SOLID; // Use MASK_SOLID (world, doors, plats) - Already contents_t is good

	// 1. Basic Volume Check (Trace at the location against world geometry)
	trace_t trace = gi.trace(io_position, monster_mins, monster_maxs, io_position, nullptr, GEOMETRY_MASK); // OK - GEOMETRY_MASK is contents_t
	if (trace.startsolid || trace.allsolid) {
		if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Failed initial solid check (MASK_SOLID) at {}\n", io_position);
		return false;
	}

	// 2. Ground/Liquid Check (Only for non-flying)
	if (!is_flying) {
		// Check if feet are in liquid (pointcontents check is fine)
		vec3_t foot_pos = io_position;
		foot_pos.z += monster_mins[2] + 1.0f;
		int contents = gi.pointcontents(foot_pos);
		if (contents & MASK_WATER) { // OK - MASK_WATER is contents_t
			if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Failed liquid check at feet {} (Contents: {})\n", foot_pos, contents);
			return false;
		}

		// Check if there's ground below using GEOMETRY_MASK
		vec3_t ground_check_end = io_position;
		ground_check_end.z -= 32;
		trace_t ground_trace = gi.trace(io_position, monster_mins, monster_maxs, ground_check_end, nullptr, GEOMETRY_MASK); // OK
		if (ground_trace.startsolid) { // Added check: If the *trace itself* starts in solid, it's bad
			if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Ground trace started in solid at {}\n", io_position);
			return false;
		}


		// Check immediate ground support using GEOMETRY_MASK
		bool has_immediate_ground = M_CheckBottom_Slow_Generic(io_position, monster_mins, monster_maxs, nullptr, GEOMETRY_MASK, false, false); // OK

		// Attempt M_droptofloor only if potentially needed and safe
		if (ground_trace.fraction < 1.0f && !has_immediate_ground) {
			vec3_t original_pos = io_position;
			// M_droptofloor_generic using GEOMETRY_MASK
			if (!M_droptofloor_generic(io_position, monster_mins, monster_maxs, false, nullptr, GEOMETRY_MASK, false)) { // OK
				// Drop failed, check if stuck in *world* geometry now
				trace = gi.trace(io_position, monster_mins, monster_maxs, io_position, nullptr, GEOMETRY_MASK); // OK
				if (trace.startsolid || trace.allsolid) {
					io_position = original_pos;
					if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Failed M_droptofloor (ended stuck in solid) at {}. Reverted.\n", original_pos);
					return false;
				}
				io_position = original_pos;
				if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: M_droptofloor failed but didn't end stuck in solid at {}. Reverted.\n", original_pos);
				// Need to re-check ground after reverting
				has_immediate_ground = M_CheckBottom_Slow_Generic(io_position, monster_mins, monster_maxs, nullptr, GEOMETRY_MASK, false, false); // OK
				if (!has_immediate_ground) {
					if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: No immediate ground after reverting from failed drop at {}.\n", io_position);
					return false; // Still no ground after reverting
				}

			}
			else {
				if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: M_droptofloor succeeded, new Z: {:.2f}.\n", io_position.z);
			}

			// Final check after potential drop: Is the *new* position clear of *world* geometry?
			trace = gi.trace(io_position, monster_mins, monster_maxs, io_position, nullptr, GEOMETRY_MASK); // OK
			if (trace.startsolid || trace.allsolid) {
				if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Failed solid check *after* potential drop at {}\n", io_position);
				io_position = original_pos; // Revert if final check failed
				return false;
			}
		}
		else if (ground_trace.fraction == 1.0f && !has_immediate_ground) {
			if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: No ground found below or immediately under {} for non-flying unit.\n", io_position);
			return false;
		}
	}

	if (developer->integer > 2) gi.Com_PrintFmt("IsValidSpawnLocation: Success (MASK_SOLID checks) for pos {} (Flying: {})\n", io_position, is_flying ? "Yes" : "No");
	return true;
}

bool CheckAndTeleportStuckMonster(edict_t* self) {
	// Global Rate Limiting
	if (level.time - HordeConstants::g_teleport_rate_reset_time > HordeConstants::GLOBAL_TELEPORT_RESET_INTERVAL) {
		HordeConstants::g_teleport_rate_count = 0;
		HordeConstants::g_teleport_rate_reset_time = level.time;
	}
	int max_teleports = HordeConstants::MAX_TELEPORTS_PER_INTERVAL;
	if ((g_insane && g_insane->integer) || (g_chaotic && g_chaotic->integer)) max_teleports++;
	if (HordeConstants::g_teleport_rate_count >= max_teleports) return false;

	// Initial Validation (as before)
	if (!self || !self->inuse || self->deadflag || level.intermissiontime || !g_horde->integer || self->monsterinfo.IS_BOSS) return false;
	horde::MonsterTypeID typeId = horde::MonsterTypeRegistry::GetTypeID(self->classname);
	if (typeId == horde::MonsterTypeID::MISC_INSANE || typeId == horde::MonsterTypeID::TURRET) return false;
	if (self->teleport_time && level.time < self->teleport_time) return false; // Check monster's cooldown
	if (self->monsterinfo.issummoned || (self->enemy && self->enemy->inuse && visible(self, self->enemy, false))) {
		if (self->monsterinfo.was_stuck) { /* Reset flags if needed */ self->monsterinfo.was_stuck = false; self->monsterinfo.stuck_check_time = 0_sec; }
		return false;
	}

	// Stuck Condition Detection (as before)
	bool should_consider_teleport = false;
	bool currently_stuck = false;
	if (!self->waterlevel) {
		currently_stuck = gi.trace(self->s.origin, self->mins, self->maxs, self->s.origin, self, MASK_MONSTERSOLID).startsolid;
		const bool no_damage_timeout = (level.time - self->monsterinfo.react_to_damage_time) >= 25_sec; // Use constant
		if (!currently_stuck && !no_damage_timeout && !self->monsterinfo.was_stuck) return false;
		if (!self->monsterinfo.was_stuck) { self->monsterinfo.stuck_check_time = level.time; self->monsterinfo.was_stuck = true; return false; }
		if (level.time >= self->monsterinfo.stuck_check_time + HordeConstants::STUCK_CHECK_TIME) { should_consider_teleport = true; }
		else { return false; }
	}
	else { return false; /* No water teleports for now */ }
	if (!should_consider_teleport) return false;


	// Choose Teleport Method (as before)
	bool humans_present = AreHumanPlayersPresent();
	float teleport_chance = 0.01f;
	if (current_wave_level > 5) { /* Calculate chance */
		if (humans_present) teleport_chance = std::min(0.01f + (current_wave_level - 5) * 0.003f, 0.08f);
		else teleport_chance = std::min(0.01f + (current_wave_level - 5) * 0.002f, 0.05f);
	}
	const bool use_player_teleport = (frandom() < teleport_chance);

	// --- Perform Teleport Attempt ---
	bool teleport_succeeded = false;
	vec3_t final_teleport_origin = vec3_origin;
	const edict_t* spawn_point = nullptr;

	// --- Method 1: Teleport Near Player ---
	if (use_player_teleport) {
		vec3_t new_origin, new_angles;
		bool teleported_to_human = false;
		// FindEmergencySpawnPosition now does full validation and proximity checks
		if (FindEmergencySpawnPosition(new_origin, new_angles, teleported_to_human, typeId)) {
			self->svflags |= SVF_NOCLIENT; gi.unlinkentity(self);
			self->s.origin = new_origin; self->s.old_origin = new_origin;
			self->s.angles = new_angles; self->velocity = vec3_origin;
			// No need for extra validation here, FindEmergencySpawnPosition handled it
			teleport_succeeded = true;
			final_teleport_origin = self->s.origin;
			if (developer->integer) gi.Com_PrintFmt("CheckAndTeleportStuckMonster: {} teleported near {} via FindEmergencySpawnPosition to {}.\n", self->classname, teleported_to_human ? "human" : "bot", final_teleport_origin);
		}
		else { if (developer->integer > 1) gi.Com_PrintFmt("CheckAndTeleportStuckMonster: FindEmergencySpawnPosition failed for {}.\n", self->classname); }
	}

	// --- Method 2: Teleport to Random Spawn Point ---
	if (!teleport_succeeded) {
		struct StuckMonsterSpawnFilter { /* Filter as before */
			bool operator()(edict_t* ent) const {
				if (!ent || !ent->inuse || !ent->classname || strcmp(ent->classname, "info_player_deathmatch") != 0 || ent->style == 1) return false;
				if (level.time < spawnPointsData[ent].teleport_cooldown) return false;
				if (IsSpawnPointOccupied(ent, nullptr)) return false; // Check player blocking
				return true; // Basic filter passed
			}
		};
		StuckMonsterSpawnFilter filter;
		const edict_t* const spawn_point = SelectRandomSpawnPoint(filter);

		if (spawn_point) {
			vec3_t candidate_pos = spawn_point->s.origin;
			bool is_flying = (self->flags & FL_FLY);

			// Validate geometrically
			if (IsValidSpawnLocation(candidate_pos, self->mins, self->maxs, is_flying)) {
				// Check proximity to recent teleports
				if (!IsPositionTooCloseToRecentTeleport(candidate_pos)) {
					self->svflags |= SVF_NOCLIENT; gi.unlinkentity(self);
					self->s.origin = candidate_pos; self->s.old_origin = candidate_pos;
					self->velocity = vec3_origin;
					teleport_succeeded = true;
					final_teleport_origin = self->s.origin;
					spawnPointsData[spawn_point].teleport_cooldown = level.time + 5_sec; // Cooldown on the point
					if (developer->integer) gi.Com_PrintFmt("CheckAndTeleportStuckMonster: {} teleported to validated spawn point {} at {}.\n", self->classname, (int)(spawn_point - g_edicts), final_teleport_origin);
				}
				else { if (developer->integer > 1) gi.Com_PrintFmt("CheckAndTeleportStuckMonster: Spawn point {} too close to recent teleport for {}.\n", candidate_pos, self->classname); }
			}
			else { if (developer->integer > 1) gi.Com_PrintFmt("CheckAndTeleportStuckMonster: Spawn point {} failed IsValidSpawnLocation for {}.\n", (int)(spawn_point - g_edicts), self->classname); }
		}
		else { if (developer->integer > 1) gi.Com_PrintFmt("CheckAndTeleportStuckMonster: SelectRandomSpawnPoint failed for {}.\n", self->classname); }
	}

	// --- Post-Teleport Actions ---
	if (teleport_succeeded) {
		// Make visible again and link at the new final position
		self->svflags &= ~SVF_NOCLIENT;
		gi.linkentity(self);

		// ***** BEGIN STALKER FIX *****
		// Explicitly reset orientation for Stalkers after successful teleport.
		// Assumes teleport destinations are always floor-based.
		// Use TypeID comparison which is safer and potentially faster
		if (typeId == horde::MonsterTypeID::STALKER) {
			//if (developer->integer) {
			//	gi.Com_PrintFmt("Stalker teleport success: Resetting orientation for floor.\n");
			//}
			self->gravityVector = { 0, 0, -1 }; // Standard gravity downwards
			self->s.angles[ROLL] = 0;           // Zero roll angle (ensure ROLL is defined or use 2)
			self->gravity = 1.0f;              // Standard gravity multiplier

			// Optionally ensure movetype allows falling if it got stuck in FLY somehow
			// Although teleport should place it correctly.
			// if (self->movetype == MOVETYPE_FLY)
			//     self->movetype = MOVETYPE_STEP;

			// Clear ground entity just in case linking didn't handle it perfectly after teleport
			self->groundentity = nullptr;
		}
		// ***** END STALKER FIX *****

		// Play effects
		gi.sound(self, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
		SpawnGrow_Spawn(final_teleport_origin, 80.0f, 10.0f); // Use the actual final position

		// Reset timers and apply cooldowns
		self->monsterinfo.react_to_damage_time = level.time; // Reset stuck detection timer source
		self->teleport_time = level.time + random_time(HordeConstants::MIN_TELEPORT_COOLDOWN_MONSTER, HordeConstants::MAX_TELEPORT_COOLDOWN_MONSTER);

		// Increment global rate limiter
		HordeConstants::g_teleport_rate_count++;

		// Mark the destination position as recently used for teleporting
		MarkPositionAsRecentlyTeleported(final_teleport_origin);

		// Apply cooldown to the spawn point if one was used
		if (!use_player_teleport && spawn_point) {
			const gtime_t spawn_point_cooldown_duration = std::max(
				HordeConstants::BASE_SPAWN_TELEPORT_COOLDOWN,
				HordeConstants::MIN_SPAWN_TELEPORT_COOLDOWN
			);
			spawnPointsData[spawn_point].teleport_cooldown = level.time + spawn_point_cooldown_duration;
		}
	}
	else {
		// If teleport failed, relink at original position
		self->svflags &= ~SVF_NOCLIENT;
		gi.linkentity(self);
	}

	// Reset Stuck Flags regardless of teleport success/failure
	self->monsterinfo.was_stuck = false;
	self->monsterinfo.stuck_check_time = 0_sec;

	return teleport_succeeded;
}

// Function to track created entities
void OnEntityCreated(edict_t* ent) {
	if (!ent || !ent->inuse)
		return;

	// Add entity tracking logic here if needed
	// For most implementations, this can be a simple stub function
	// that can be expanded later with actual tracking needs

	if (developer->integer > 2) {
		gi.Com_PrintFmt("Entity created: {} ({})\n",
			ent->classname ? ent->classname : "unknown",
			ent - g_edicts);
	}
}

void CheckForMonsterDeathsInSpawningState(edict_t* monster) {
	if (!monster || !monster->inuse)
		return;

	// Check if monster was spawned during spawning state and died in that state
	if (monster->spawned_in_spawn_state && g_horde_local.state == horde_state_t::spawning) {
		// Track monsters killed during spawning
		static int32_t spawn_state_deaths = 0;
		static gtime_t last_death_time = 0_sec;

		// Reset counter if too much time has passed
		if (level.time - last_death_time > 8_sec) {
			spawn_state_deaths = 0;
		}

		spawn_state_deaths++;
		last_death_time = level.time;

		if (developer->integer) {
			gi.Com_PrintFmt("Monster killed during spawning state ({} total)\n", spawn_state_deaths);
		}

		// Calculate spawn progress - how many monsters were spawned vs. total planned
		const int32_t total_planned = g_totalMonstersInWave + g_horde_local.num_to_spawn + g_horde_local.queued_monsters;
		const float spawn_progress = total_planned > 0 ?
			static_cast<float>(g_totalMonstersInWave) / total_planned : 0.0f;

		// Only transition if:
		// 1. Multiple monsters have been killed during spawning AND
		// 2. We've either spawned a significant portion of the wave OR a minimum number of monsters
		// 3. The last condition ensures we don't transition too early in the wave
		if (spawn_state_deaths >= 4 && (spawn_progress >= 0.5f || g_totalMonstersInWave >= 8)) {
			if (developer->integer) {
				gi.Com_PrintFmt("SPAWN TRANSITION: {} monsters killed during spawning. "
					"{} spawned of {} planned ({:.1f}%). Transitioning to active_wave.\n",
					spawn_state_deaths, g_totalMonstersInWave, total_planned,
					spawn_progress * 100.0f);
			}

			// Force transition to active_wave
			if (!next_wave_message_sent) {
				VerifyAndAdjustBots();
				// Use a more immersive message
				//gi.LocBroadcast_Print(PRINT_CHAT,
				//	"\n\n\nStrogg forces engaged! Combat phase activated.\n");
				next_wave_message_sent = true;
			}

			g_horde_local.state = horde_state_t::active_wave;
			spawn_state_deaths = 0; // Reset for next wave
		}
	}

	// Clear flags once monster is dead
	monster->was_spawned_by_horde = false;
	monster->spawned_in_spawn_state = false;
}

// Function to attempt dropping a monster to the floor
bool AttemptDropToFloor(vec3_t& position, const vec3_t& mins, const vec3_t& maxs) {
	// Try a trace straight down to find floor
	vec3_t start = position;
	vec3_t end = position - vec3_t{ 0, 0, 512 };

	trace_t trace = gi.trace(start, mins, maxs, end, nullptr, MASK_MONSTERSOLID);

	// If we hit something that isn't immediately below us
	if (trace.fraction > 0.01f && trace.fraction < 1.0f) {
		// Found floor, set position to just above it
		position = trace.endpos + vec3_t{ 0, 0, 1.0f };

		// Final check to make sure position is valid
		trace = gi.trace(position, mins, maxs, position, nullptr, MASK_MONSTERSOLID);
		if (!trace.startsolid && !trace.allsolid)
			return true;
	}

	return false;
}

bool IsPositionTooCloseToRecent(const vec3_t& position, float min_distance) {
	const gtime_t current_time = level.time;
	for (const auto& recent : g_recent_spawn_positions) {
		if (recent.cooldown_until > current_time) {
			const float distance_squared = (position - recent.position).lengthSquared();
			if (distance_squared < min_distance * min_distance) {
				return true;
			}
		}
	}
	return false;
}

// --- TryAlternativeSpawnPosition Function ---
bool TryAlternativeSpawnPosition(edict_t* spawn_point, horde::MonsterTypeID typeId, vec3_t& final_origin, vec3_t& final_angles) {
	// Initial validation
	if (!spawn_point || !spawn_point->inuse || !is_valid_vector(spawn_point->s.origin)) {
		if (developer->integer > 1) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Invalid spawn_point.\n");
		return false;
	}

	const vec3_t base_origin = spawn_point->s.origin;
	const vec3_t base_angles = spawn_point->s.angles;
	bool is_flying = IsFlying(typeId);
	const vec3_t monster_mins = HordeConstants::VALIDATE_CHECK_MINS;
	const vec3_t monster_maxs = HordeConstants::VALIDATE_CHECK_MAXS;

	// 1. Try Predefined Relative Positions
	for (const auto& offset : HordeConstants::horde_alternative_positions) {
		vec3_t candidate_pos = base_origin + offset;

		// Check LOS (optional, simple check)
		trace_t trace = gi.traceline(base_origin, candidate_pos, spawn_point, MASK_SOLID);
		if (trace.fraction < 0.9f) continue;

		// Validate the location geometrically
		if (IsValidSpawnLocation(candidate_pos, monster_mins, monster_maxs, is_flying)) {
			// Check proximity to recent *regular* spawns
			if (!IsPositionTooCloseToRecentSpawn(candidate_pos)) {
				final_origin = candidate_pos; // Use validated (potentially floor-dropped) position
				final_angles = base_angles;
				if (offset.x != 0.0f || offset.y != 0.0f) final_angles[YAW] = atan2f(offset.y, offset.x) * (180.0f / PI);
				MarkPositionAsRecentlyUsed(final_origin);
				if (developer->integer > 1) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Success using predefined offset (Final Pos: {}).\n", final_origin);
				return true;
			}
			else if (developer->integer > 2) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Predefined offset {} too close to recent spawn.\n", candidate_pos);
		}
	}

	// 2. Try Random Radial Positions
	constexpr int RADIAL_ATTEMPTS = 12;
	constexpr float MIN_RADIUS = 40.0f;
	constexpr float MAX_RADIUS = 180.0f;

	for (int i = 0; i < RADIAL_ATTEMPTS; ++i) {
		float radius = frandom(MIN_RADIUS, MAX_RADIUS);
		float angle = frandom() * 2.0f * PI;
		vec3_t offset = { cosf(angle) * radius, sinf(angle) * radius, frandom(-8.0f, 16.0f) };
		vec3_t candidate_pos = base_origin + offset;

		// Check LOS (optional)
		trace_t trace = gi.traceline(base_origin, candidate_pos, spawn_point, MASK_SOLID);
		if (trace.fraction < 0.8f) continue;

		// Validate geometrically
		if (IsValidSpawnLocation(candidate_pos, monster_mins, monster_maxs, is_flying)) {
			// Check proximity to recent *regular* spawns
			if (!IsPositionTooCloseToRecentSpawn(candidate_pos)) {
				final_origin = candidate_pos;
				final_angles = base_angles;
				final_angles[YAW] = angle * (180.0f / PI);
				MarkPositionAsRecentlyUsed(final_origin);
				if (developer->integer > 1) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Success using radial offset (Final Pos: {}).\n", final_origin);
				return true;
			}
			else if (developer->integer > 2) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Radial offset {} too close to recent spawn.\n", candidate_pos);
		}
	}

	if (developer->integer > 1) gi.Com_PrintFmt("TryAlternativeSpawnPosition: Failed for point {}.\n", (int)(spawn_point - g_edicts));
	return false;
}
// TypeID-based overload for EmergencySpawnMonster
bool EmergencySpawnMonster(const int32_t levelNum, horde::MonsterTypeID typeId) {
	const char* monster_classname = horde::MonsterTypeRegistry::GetClassname(typeId);

	// Find emergency position
	vec3_t emergency_origin, emergency_angles;
	bool used_human_player = false;

	// Use the TypeID version of FindEmergencySpawnPosition
	if (!FindEmergencySpawnPosition(emergency_origin, emergency_angles, used_human_player, typeId)) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Could not find valid position for TypeID {}\n", static_cast<int>(typeId));
		}
		return false;
	}
	// Use improved validation logic - allow fallback since it's emergency
	// *** ADDED 4th ARGUMENT (true) ***
	if (!IsValidSpawnLocation(emergency_origin, HordeConstants::VALIDATE_CHECK_MINS, HordeConstants::VALIDATE_CHECK_MAXS, true)) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Position validation failed\n");
		}
		return false;
	}

	// Create the base entity using the helper
	edict_t* monster = CreateBaseHordeMonster(typeId, emergency_origin, emergency_angles);
	if (!monster) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: CreateBaseHordeMonster failed for TypeID {}\n", static_cast<int>(typeId));
		}
		return false; // Creation failed
	}

	// Use provided classname from TypeID
	monster->classname = monster_classname;
	monster->s.origin = emergency_origin;
	monster->s.angles = emergency_angles;
	monster->spawnflags |= SPAWNFLAG_MONSTER_SUPER_STEP;
	monster->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	monster->monsterinfo.last_sentrygun_target_time = 0_ms;

	// Mark it as spawned by horde
	monster->was_spawned_by_horde = true;

	// In spawning state, mark specifically
	if (g_horde_local.state == horde_state_t::spawning) {
		monster->spawned_in_spawn_state = true;
	}

	// Spawn the entity with protection from errors
	monster->solid = SOLID_NOT;  // Start as non-solid to avoid immediate collisions
	ED_CallSpawn(monster);

	// Check if ED_CallSpawn succeeded
	if (!monster->inuse) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: ED_CallSpawn failed for {}\n", monster->classname);
		}
		return false;
	}

	// Re-set solid state and properly link
	monster->solid = SOLID_BBOX;
	gi.linkentity(monster);

	// --- Apply Modifiers (Same as before) ---
	// ... (Champion chance, armor, item drop, effects) ...
	if (levelNum >= 10 && frandom() < 0.5f) {
		int flag_type = irandom(0, 5);
		switch (flag_type) {
		case 0: monster->monsterinfo.bonus_flags |= BF_CHAMPION; break;
		case 1: monster->monsterinfo.bonus_flags |= BF_CORRUPTED; break;
		case 2: monster->monsterinfo.bonus_flags |= BF_BERSERKING; break;
		case 3: monster->monsterinfo.bonus_flags |= BF_POSSESSED; break;
		case 4: monster->monsterinfo.bonus_flags |= BF_STYGIAN; break;
		}
		ApplyMonsterBonusFlags(monster);
	}
	if (levelNum >= 14 && monster->monsterinfo.power_armor_type == IT_NULL) {
		SetMonsterArmor(monster);
	}
	monster->item = G_HordePickItem();
	monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP; // Ensure drop is allowed
	SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
	gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);
	// --- End Apply Modifiers ---

	// Success!
	if (developer->integer) {
		gi.Com_PrintFmt("EMERGENCY SPAWN SUCCESSFUL: Spawned {} at emergency position\n",
			monster->classname);
	}

	return true;
}

// Modified ShouldTriggerAmbushSpawn function for more frequent ambushes
bool ShouldTriggerAmbushSpawn() {
	// Static variables for tracking

	// Only consider ambush spawning after wave 3 (earlier than before)
	if (current_wave_level < 3) {
		return false;
	}

	// Check if cooldown has expired - REDUCED cooldown
	if (ambush_cooldown_frames > 0) {
		ambush_cooldown_frames--;
		return false;
	}

	// Check if enough time has passed since last ambush - REDUCED from 45 to 25 seconds
	if (level.time - last_ambush_time < 25_sec) {
		return false;
	}

	// Base chance increases with waves since last ambush - INCREASED base chance
	float baseChance = 0.08f + (waves_since_ambush * 0.03f);  // Higher starting value and progression

	// Higher chance in higher waves, capped at 45% (increased from 35%)
	int cappedLevel = (current_wave_level > 25) ? 25 : current_wave_level;
	baseChance += (cappedLevel - 3) * 0.015f;  // Faster scaling with wave level
	baseChance = (baseChance > 0.45f) ? 0.45f : baseChance;

	// Higher chance if players have high health/armor or are on killing sprees
	float playerBonus = 0.0f;
	int playerCount = 0;
	for (auto* player : active_players_no_spect()) {
		if (player && player->inuse && player->health > 0) {
			// Add chance bonus for healthy players or those on sprees
			if (player->health >= 125 || player->client->resp.spree >= 50) {
				playerBonus += 0.04f;  // Increased from 0.03f
			}
			playerCount++;
		}
	}

	// Only apply bonus if there are players, capped at reasonable amount
	if (playerCount > 0) {
		baseChance += std::min(playerBonus, 0.15f);  // Increased cap from 0.12f
	}

	// Roll for chance
	if (frandom() < baseChance) {
		// If successful, set cooldown and timestamp
		last_ambush_time = level.time;
		ambush_cooldown_frames = irandom(100, 220);  // 3-7 seconds cooldown (reduced from 5-10)
		waves_since_ambush = 0;
		return true;
	}

	return false;
}

// Updated SpawnAmbushMonsters to use improved emergency spawning with TypeIDs
int SpawnAmbushMonsters(const MapSize& mapSize, int32_t waveLevel) {
	if (developer->integer) {
		gi.Com_PrintFmt("DEBUG: Starting SpawnAmbushMonsters\n");
	}

	// Determine monster type ID - try to get a valid monster for current wave
	horde::MonsterTypeID monster_typeId = horde::MonsterTypeID::UNKNOWN;

	// Try to get a valid monster type ID from existing spawn points
	for (auto* point : monster_spawn_points()) {
		if (point && point->inuse) {
			// Use TypeID version for more efficient selection
			monster_typeId = G_HordePickMonsterType(point);
			if (monster_typeId != horde::MonsterTypeID::UNKNOWN) {
				// Found a valid type ID, no need to get classname here
				break;
			}
		}
	}

	// Fallback to appropriate monster type ID if none found
	if (monster_typeId == horde::MonsterTypeID::UNKNOWN) {
		if (waveLevel >= 15) {
			// Higher-level monsters for later waves
			const horde::MonsterTypeID high_level_monsters[] = {
				horde::MonsterTypeID::GUNNER,
				horde::MonsterTypeID::GLADIATOR,
				horde::MonsterTypeID::TANK,
				horde::MonsterTypeID::SOLDIER_HYPERGUN,
				horde::MonsterTypeID::SOLDIER_LASERGUN
			};
			monster_typeId = high_level_monsters[irandom(0, 4)];
			// No need to get classname here
		}
		else {
			// Basic monsters for early waves
			const horde::MonsterTypeID basic_monsters[] = {
				horde::MonsterTypeID::SOLDIER_LIGHT,
				horde::MonsterTypeID::SOLDIER,
				horde::MonsterTypeID::INFANTRY,
				horde::MonsterTypeID::SOLDIER_SS,
				horde::MonsterTypeID::FLYER
			};
			monster_typeId = basic_monsters[irandom(0, 4)];
			// No need to get classname here
		}
	}

	// Determine ambush size based on wave and map - INCREASED size
	const int baseCount = mapSize.isSmallMap ? 3 :
		mapSize.isBigMap ? 5 : 4;
	const int ambushSize = baseCount + (waveLevel >= 15 ? 2 : 1);

	// Track successful spawns
	int ambushSuccessCount = 0;

	// Keep track of spawn attempts per position
	constexpr int MAX_FAILURES_PER_POSITION = 3;
	int consecutive_failures = 0;

	// Track timing for spawn attempts
	static gtime_t last_failed_spawn_time = 0_sec;
	constexpr gtime_t SPAWN_RETRY_DELAY = 0.1_sec; // 100ms delay between failed attempts

	// Add safety counter to prevent infinite loops
	int safety_counter = 0;
	const int MAX_TOTAL_ITERATIONS = 100;

	// Spawn ambush monsters
	for (int i = 0; i < ambushSize && consecutive_failures < MAX_FAILURES_PER_POSITION; i++) {
		// Add safety check to prevent infinite loops
		if (safety_counter++ > MAX_TOTAL_ITERATIONS) {
			gi.Com_PrintFmt("WARNING: Emergency spawn safety limit reached in SpawnAmbushMonsters\n");
			break;
		}

		// Check if we need to wait after a failed attempt
		if (consecutive_failures > 0) {
			if (level.time < last_failed_spawn_time + SPAWN_RETRY_DELAY) {
				// Postpone this attempt, don't increment 'i' yet, but continue outer loop check
				// To avoid busy-waiting, we only check periodically
				if (safety_counter % 10 != 0) {
					continue; // Skip this frame's attempt
				}
			}
		}

		// Use the TypeID version for more efficient spawning
		// Check if a valid type ID was determined before calling EmergencySpawnMonster
		if (monster_typeId != horde::MonsterTypeID::UNKNOWN &&
			EmergencySpawnMonster(waveLevel, monster_typeId)) { // Pass the ID directly
			ambushSuccessCount++;
			consecutive_failures = 0; // Reset failures on success

			// Update spawn counters
			if (g_horde_local.num_to_spawn > 0) {
				--g_horde_local.num_to_spawn;
			}
			else if (g_horde_local.queued_monsters > 0) {
				--g_horde_local.queued_monsters;
			}

			// Update total monsters counter
			if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) {
				++g_totalMonstersInWave;
			}
		}
		else {
			// Track failures to avoid getting stuck in endless attempts
			consecutive_failures++;
			last_failed_spawn_time = level.time; // Record time of failure
			// Optional: Log which type ID failed if in dev mode
			if (developer->integer > 1) {
				const char* failed_name = horde::MonsterTypeRegistry::GetClassname(monster_typeId);
				gi.Com_PrintFmt("SpawnAmbushMonsters: EmergencySpawnMonster failed for TypeID {} ({})\n",
					static_cast<int>(monster_typeId), failed_name ? failed_name : "Unknown");
			}
		}
	}

	// Only play sound effect without text message for subtlety
	if (ambushSuccessCount > 0) {
		gi.sound(world, CHAN_AUTO, sound_spawn1, 1, ATTN_NONE, 0);
		if (developer->integer) {
			gi.Com_PrintFmt("AMBUSH: Spawned {}/{} monsters near players\n",
				ambushSuccessCount, ambushSize);
		}
	}
	else if (consecutive_failures >= MAX_FAILURES_PER_POSITION && developer->integer) {
		// Log if the loop terminated due to failures
		gi.Com_PrintFmt("SpawnAmbushMonsters: Loop terminated due to {} consecutive failures.\n", consecutive_failures);
	}


	if (developer->integer) {
		gi.Com_PrintFmt("DEBUG: Finished SpawnAmbushMonsters, successfully spawned: {}\n", ambushSuccessCount);
	}

	return ambushSuccessCount;
}

// Cache for potentially valid spawn points (pointers)
static std::vector<edict_t*> g_potential_spawn_points;
static bool g_spawn_points_cached = false;
static size_t g_spawn_point_shuffle_index = 0; // Index for iterating shuffled list

// State for failure tracking and recovery
static int g_consecutive_spawn_failures = 0;
static constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY = 5;
static constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY = 10;
static bool g_recovery_mode_active = false;
static MonsterWaveType g_original_wave_type_before_recovery = MonsterWaveType::None;

edict_t* SpawnMonsters() {
	// --- Initial Checks & Caching ---
	if (developer->integer == 2) return nullptr; // Debug mode check

	const gtime_t current_time = level.time;
	const MapSize& mapSize = g_horde_local.current_map_size; // Cache map size
	const horde_state_t current_state = g_horde_local.state; // Cache current state
	const int32_t currentLevel = g_horde_local.level;       // Cache current level

	// --- Spawn Point Caching ---
	if (!g_spawn_points_cached || need_spawn_cache_reset) {
		g_potential_spawn_points.clear();
		g_potential_spawn_points.reserve(MAX_SPAWN_POINTS); // Avoid reallocations

		for (auto* point : monster_spawn_points()) { // Uses the helper iterator
			if (point && point->inuse && point->classname &&
				strcmp(point->classname, "info_player_deathmatch") == 0 &&
				is_valid_vector(point->s.origin))
			{
				g_potential_spawn_points.push_back(point);
			}
		}

		if (!g_potential_spawn_points.empty()) {
			// Use a proper random engine if mt_rand is available and setup, else fallback
			// std::shuffle(g_potential_spawn_points.begin(), g_potential_spawn_points.end(), mt_rand);
			// Using a simple shuffle if mt_rand isn't readily available/set up:
			for (size_t i = g_potential_spawn_points.size() - 1; i > 0; --i) {
				size_t j = irandom(0, i); // irandom is inclusive
				if (i != j) {
					std::swap(g_potential_spawn_points[i], g_potential_spawn_points[j]);
				}
			}
		}

		g_spawn_point_shuffle_index = 0;
		g_spawn_points_cached = true;
		need_spawn_cache_reset = false;

		g_consecutive_spawn_failures = 0;
		g_recovery_mode_active = false;
		g_original_wave_type_before_recovery = MonsterWaveType::None;

		if (developer->integer) {
			gi.Com_PrintFmt("Spawn Point Cache Rebuilt: {} potential points found and shuffled.\n",
				g_potential_spawn_points.size());
		}
	}

	if (g_potential_spawn_points.empty()) {
		if (developer->integer) {
			gi.Com_PrintFmt("SpawnMonsters: No potential spawn points found in level. Cannot spawn.\n");
		}
		g_consecutive_spawn_failures++;
		return nullptr;
	}

	// --- Calculate Monster Caps ---
	const int32_t activeMonsters = CalculateRemainingMonsters();
	const int32_t softCap = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap :
		(mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
			(mapSize.isBigMap ? MAX_MONSTERS_BIG_MAP : MAX_MONSTERS_MEDIUM_MAP));
	const int32_t hardCap = static_cast<int32_t>(softCap * 1.2f);

	// --- Hard Cap Check ---
	if (activeMonsters >= hardCap) {
		if (developer->integer > 1) {
			gi.Com_PrintFmt("SpawnMonsters: Hard monster cap reached ({}/{}), SoftCap={}, PlayerAdjustedCap={}\n",
				activeMonsters, hardCap, softCap, g_adjusted_monster_cap);
		}
		if (current_state == horde_state_t::spawning && !next_wave_message_sent) {
			VerifyAndAdjustBots();
			gi.LocBroadcast_Print(PRINT_CENTER, "\n\n\nWave Fully Deployed (Hard Cap Reached).\nWave Level: {}\n", currentLevel);
			next_wave_message_sent = true;
			g_horde_local.state = horde_state_t::active_wave;
		}
		g_horde_local.num_to_spawn = 0;
		return nullptr;
	}

	// --- AMBUSH SYSTEM CHECK ---
	if (current_state == horde_state_t::active_wave && activeMonsters < softCap && ShouldTriggerAmbushSpawn()) {
		int spawnedCount = SpawnAmbushMonsters(mapSize, currentLevel);
		if (spawnedCount > 0) {
			g_consecutive_spawn_failures = 0;
			g_recovery_mode_active = false;
			if (g_original_wave_type_before_recovery != MonsterWaveType::None) { // Restore if needed
				current_wave_type = g_original_wave_type_before_recovery;
				g_original_wave_type_before_recovery = MonsterWaveType::None;
			}
			SetNextMonsterSpawnTime(mapSize);
			return nullptr;
		}
		// If ambush fails, continue to normal spawning
	}

	// --- Check Spawn Conditions & Calculate Batch Size ---
	int32_t availableSpace = softCap - activeMonsters;
	if (availableSpace <= 0) {
		if (developer->integer > 1) {
			gi.Com_PrintFmt("SpawnMonsters: Soft monster cap reached ({}/{}), PlayerAdjustedCap={}\n", activeMonsters, softCap, g_adjusted_monster_cap);
		}
		return nullptr;
	}

	// --- Queue Transfer Logic ---
	if (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters > 0) {
		const int32_t base_transfer = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
		const int32_t transfer_amount = std::min({ g_horde_local.queued_monsters, availableSpace, base_transfer });
		if (transfer_amount > 0) {
			g_horde_local.num_to_spawn += transfer_amount;
			g_horde_local.queued_monsters -= transfer_amount;
			if (developer->integer > 1) {
				gi.Com_PrintFmt("SpawnMonsters: Transferred {} monsters from queue ({} remaining).\n", transfer_amount, g_horde_local.queued_monsters);
			}
			availableSpace -= transfer_amount;
		}
	}

	if (g_horde_local.num_to_spawn <= 0) {
		if (developer->integer > 1) {
			gi.Com_PrintFmt("SpawnMonsters: No monsters available in spawn pool or queue.\n");
		}
		return nullptr;
	}

	// --- Calculate Batch Size ---
	const int32_t base_batch = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
	const int32_t spawnable_this_call = std::min({ g_horde_local.num_to_spawn, base_batch, availableSpace });

	// --- Failure Recovery & Emergency Logic ---
	MonsterWaveType wave_type_override = MonsterWaveType::None;
	if (g_consecutive_spawn_failures >= MAX_CONSECUTIVE_FAILURES_BEFORE_RECOVERY && !g_recovery_mode_active) {
		g_recovery_mode_active = true;
		g_original_wave_type_before_recovery = current_wave_type;
		wave_type_override = HasWaveType(current_wave_type, MonsterWaveType::Flying) ? MonsterWaveType::Flying : MonsterWaveType::Ground;
		ResetAllSpawnAttempts();
		if (developer->integer) {
			gi.Com_PrintFmt("RECOVERY MODE ACTIVATED: Failures={}. Simplified wave type, reset spawn points.\n", g_consecutive_spawn_failures);
		}
	}
	else if (g_consecutive_spawn_failures >= MAX_CONSECUTIVE_FAILURES_BEFORE_EMERGENCY) {
		if (developer->integer) {
			gi.Com_PrintFmt("EMERGENCY SPAWN TRIGGERED: Failures={}. Attempting spawn near players.\n", g_consecutive_spawn_failures);
		}
		int emergency_spawned_count = 0;
		for (int i = 0; i < std::min(spawnable_this_call, 3); ++i) {
			horde::MonsterTypeID emergency_type = (currentLevel < 10) ? horde::MonsterTypeID::SOLDIER : horde::MonsterTypeID::GUNNER;
			if (currentLevel >= 15) emergency_type = horde::MonsterTypeID::TANK;
			if (currentLevel >= 20) emergency_type = horde::MonsterTypeID::GLADIATOR;

			if (EmergencySpawnMonster(currentLevel, emergency_type)) {
				emergency_spawned_count++;
				if (g_horde_local.num_to_spawn > 0) --g_horde_local.num_to_spawn;
				else if (g_horde_local.queued_monsters > 0) --g_horde_local.queued_monsters;
				if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) ++g_totalMonstersInWave;
			}
			else {
				if (developer->integer) gi.Com_PrintFmt("EMERGENCY SPAWN FAILED: Could not spawn type {}.\n", (int)emergency_type);
				break;
			}
		}
		if (emergency_spawned_count > 0) {
			g_consecutive_spawn_failures = 0;
			if (g_recovery_mode_active) {
				g_recovery_mode_active = false;
				if (g_original_wave_type_before_recovery != MonsterWaveType::None) {
					current_wave_type = g_original_wave_type_before_recovery;
					g_original_wave_type_before_recovery = MonsterWaveType::None;
					if (developer->integer) gi.Com_PrintFmt("RECOVERY MODE DEACTIVATED (via Emergency Spawn): Restored original wave type.\n");
				}
				else if (developer->integer) {
					gi.Com_PrintFmt("RECOVERY MODE DEACTIVATED (via Emergency Spawn).\n");
				}
			}
			SetNextMonsterSpawnTime(mapSize);
			return nullptr;
		}
		// Emergency failed, continue to normal loop but failures remain high
	}
	const MonsterWaveType effective_wave_type = g_recovery_mode_active ? wave_type_override : current_wave_type;
	// --- Main Spawn Loop ---
	edict_t* last_spawned_this_call = nullptr;
	int spawned_count_this_call = 0;
	int points_checked_total_this_batch = 0;
	const size_t total_potential_points = g_potential_spawn_points.size();

	for (int i = 0; i < spawnable_this_call; ++i) {
		bool spawn_successful_for_this_monster = false;
		int points_checked_for_this_monster = 0;

		while (points_checked_for_this_monster < total_potential_points) {
			if (g_spawn_point_shuffle_index >= total_potential_points) g_spawn_point_shuffle_index = 0;
			edict_t* spawn_point = g_potential_spawn_points[g_spawn_point_shuffle_index++];
			points_checked_for_this_monster++;
			points_checked_total_this_batch++;

			// --- Initial Spawn Point Validation ---
			const auto& sp_data = spawnPointsData[spawn_point];
			if ((sp_data.isTemporarilyDisabled && current_time < sp_data.cooldownEndsAt) || (current_time < sp_data.alternative_cooldown)) {
				// Point on cooldown, skip silently unless debugging high
				if (developer->integer > 2) gi.Com_PrintFmt("SpawnMonsters: Point {} skipped (cooldown).\n", (int)(spawn_point - g_edicts));
				continue; // Skip this point entirely for now
			}
			const bool is_flying_spawn_point = (spawn_point->style == 1);
			if (effective_wave_type != MonsterWaveType::None && is_flying_spawn_point && !HasWaveType(effective_wave_type, MonsterWaveType::Flying)) {
				if (developer->integer > 2) gi.Com_PrintFmt("SpawnMonsters: Point {} skipped (wave/spawn flying mismatch).\n", (int)(spawn_point - g_edicts));
				IncreaseSpawnAttempts(spawn_point); // Penalize
				g_consecutive_spawn_failures++;
				continue;
			}
			bool too_close_to_player = false;
			for (const auto* const player : active_players_no_spect()) { if ((spawn_point->s.origin - player->s.origin).lengthSquared() < HordeConstants::MIN_PLAYER_DIST_SQ_CHECK) { too_close_to_player = true; break; } }
			if (too_close_to_player) {
				if (developer->integer > 2) gi.Com_PrintFmt("SpawnMonsters: Point {} skipped (too close to player).\n", (int)(spawn_point - g_edicts));
				IncreaseSpawnAttempts(spawn_point); // Penalize
				g_consecutive_spawn_failures++;
				continue;
			}
			SpawnPointCache& cache = spawn_point_cache[spawn_point];
			if (IsSpawnPointOccupied(spawn_point)) { // Checks only player block
				if (developer->integer > 2) gi.Com_PrintFmt("SpawnMonsters: Point {} skipped (player occupied - IsOccupied).\n", (int)(spawn_point - g_edicts));
				IncreaseSpawnAttempts(spawn_point);
				g_consecutive_spawn_failures++;
				continue;
			}

			// --- Monster Selection ---
			horde::MonsterTypeID monster_type = G_HordePickMonsterType(spawn_point);
			if (monster_type == horde::MonsterTypeID::UNKNOWN) {
				// Failure likely logged/handled inside G_HordePickMonsterType
				// G_HordePickMonsterType might have already called IncreaseSpawnAttempts
				g_consecutive_spawn_failures++;
				continue; // Try next spawn point
			}
			const bool monster_is_flying = IsFlying(monster_type);

			// --- Flying Compatibility Check (Post-Selection) ---
			if (is_flying_spawn_point && !monster_is_flying) {
				if (developer->integer > 2) gi.Com_PrintFmt("SpawnMonsters: Point {} skipped (flying point, non-flying monster {}).\n", (int)(spawn_point - g_edicts), (int)monster_type);
				IncreaseSpawnAttempts(spawn_point); g_consecutive_spawn_failures++; continue;
			}
			// Optional: Add check for non-flying point + flying monster if wave isn't specifically flying? (already present before selection, maybe redundant)
			// if (!is_flying_spawn_point && monster_is_flying && effective_wave_type != MonsterWaveType::None && !HasWaveType(effective_wave_type, MonsterWaveType::Flying)) { IncreaseSpawnAttempts(spawn_point); g_consecutive_spawn_failures++; continue; }


			// --- Attempt Spawn: Direct First, then Alternative ---
			vec3_t final_origin;
			vec3_t final_angles;
			bool used_alternative = false;
			bool found_valid_spot = false;

			// ** Attempt 1: Direct Spawn Point **
			vec3_t direct_origin = spawn_point->s.origin;
			if (IsValidSpawnLocation(direct_origin, HordeConstants::VALIDATE_CHECK_MINS, HordeConstants::VALIDATE_CHECK_MAXS, monster_is_flying)) {
				// Check final occupation at the direct (potentially floor-dropped) location
				FilterData final_check_data = { nullptr, 0 };
				const vec3_t check_mins = direct_origin + HordeConstants::VALIDATE_CHECK_MINS;
				const vec3_t check_maxs = direct_origin + HordeConstants::VALIDATE_CHECK_MAXS;
				gi.BoxEdicts(check_mins, check_maxs, nullptr, 0, AREA_SOLID, SpawnPointFilter, &final_check_data);

				if (final_check_data.count == 0) {
					// Direct spot is valid and clear!
					final_origin = direct_origin;
					final_angles = spawn_point->s.angles;
					found_valid_spot = true;
					if (developer->integer > 2) gi.Com_PrintFmt("SpawnMonsters: Point {} direct location {} is valid.\n", (int)(spawn_point - g_edicts), final_origin);
				}
				else {
					if (developer->integer > 2) gi.Com_PrintFmt("SpawnMonsters: Point {} direct location {} failed final occupation check.\n", (int)(spawn_point - g_edicts), direct_origin);
				}
			}
			else {
				if (developer->integer > 2) gi.Com_PrintFmt("SpawnMonsters: Point {} direct location {} failed IsValidSpawnLocation.\n", (int)(spawn_point - g_edicts), spawn_point->s.origin);
			}

			// ** Attempt 2: Alternative Spawn Point (if direct failed OR cache indicated obstacle initially) **
			// Only try alternative if direct failed AND alternative is not on cooldown
			if (!found_valid_spot && current_time >= sp_data.alternative_cooldown)
			{
				if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Direct spawn failed or point congested for point {}, trying alternative...\n", (int)(spawn_point - g_edicts));
				if (TryAlternativeSpawnPosition(spawn_point, monster_type, final_origin, final_angles)) {
					// Alternative position FOUND, now re-validate IT
					used_alternative = true;
					vec3_t alternative_origin_copy = final_origin; // IsValid may modify it

					if (IsValidSpawnLocation(alternative_origin_copy, HordeConstants::VALIDATE_CHECK_MINS, HordeConstants::VALIDATE_CHECK_MAXS, monster_is_flying)) {
						final_origin = alternative_origin_copy; // Use the potentially adjusted pos

						// Check final occupation at the ALTERNATIVE location
						FilterData final_check_data_alt = { nullptr, 0 };
						const vec3_t check_mins_alt = final_origin + HordeConstants::VALIDATE_CHECK_MINS;
						const vec3_t check_maxs_alt = final_origin + HordeConstants::VALIDATE_CHECK_MAXS;
						gi.BoxEdicts(check_mins_alt, check_maxs_alt, nullptr, 0, AREA_SOLID, SpawnPointFilter, &final_check_data_alt);

						if (final_check_data_alt.count == 0) {
							// Alternative spot is valid and clear!
							found_valid_spot = true;
							if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Point {} alternative location {} is valid.\n", (int)(spawn_point - g_edicts), final_origin);
						}
						else {
							if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Point {} alternative location {} failed final occupation check.\n", (int)(spawn_point - g_edicts), final_origin);
							ApplyAlternativePositionCooldown(spawn_point); // Penalize failed alternative attempt
							g_consecutive_spawn_failures++;
							// found_valid_spot remains false
						}
					}
					else {
						if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Point {} alternative location {} failed IsValidSpawnLocation.\n", (int)(spawn_point - g_edicts), final_origin);
						ApplyAlternativePositionCooldown(spawn_point); // Penalize failed alternative attempt
						g_consecutive_spawn_failures++;
						// found_valid_spot remains false
					}
				}
				else {
					// TryAlternativeSpawnPosition itself failed to find anything
					if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Point {} TryAlternativeSpawnPosition returned false.\n", (int)(spawn_point - g_edicts));
					ApplyAlternativePositionCooldown(spawn_point); // Penalize failed alternative attempt
					g_consecutive_spawn_failures++;
					// found_valid_spot remains false
				}
			}
			else if (!found_valid_spot) {
				// Direct failed, but couldn't try alternative (e.g., on cooldown)
				// Apply normal penalty as if direct failed validation
				IncreaseSpawnAttempts(spawn_point);
				g_consecutive_spawn_failures++;
				if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Point {} direct failed, alternative skipped (cooldown?).\n", (int)(spawn_point - g_edicts));
			}


			// --- Spawn Execution (if a valid spot was found) ---
			// --- Spawn Execution (if a valid spot was found) ---
			if (found_valid_spot) {
				edict_t* monster = SpawnMonsterByTypeID(monster_type, final_origin, final_angles);

				if (monster) {
					// --- Success Handling (Initial steps) ---
					spawn_successful_for_this_monster = true;
					last_spawned_this_call = monster;
					spawned_count_this_call++;
					if (g_horde_local.num_to_spawn > 0) --g_horde_local.num_to_spawn;
					else if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: WARN - Spawned monster but num_to_spawn was already zero.\n"); // {} not needed here
					if (g_totalMonstersInWave < std::numeric_limits<uint16_t>::max()) ++g_totalMonstersInWave;
					else if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: WARN - Reached uint16_t max for g_totalMonstersInWave.\n"); // {} not needed here

					// --- Apply Bonuses and Effects ---
					bool is_champion = false;

					// --- Champion Logic ---
					if (currentLevel >= 3 && !champion_spawned_this_wave && champion_spawn_cooldown <= 0 && !monster->monsterinfo.IS_BOSS && frandom() < 0.2f) {
						monster->monsterinfo.bonus_flags |= BF_CHAMPION;

						if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters Debug: Calling ApplyMonsterBonusFlags (Champion) for {}\n", monster - g_edicts); // Corrected format
						ApplyMonsterBonusFlags(monster); // <<< SUSPECTED OF POTENTIALLY FREEING 'monster'

						// ***** DEFENSIVE CHECK #1 *****
						// Verify 'monster' is still valid after calling ApplyMonsterBonusFlags
						if (!monster->inuse) {
							if (developer->integer) {
								// Note: Accessing monster->classname might be unsafe if monster is freed. Use ID carefully.
								gi.Com_PrintFmt("SpawnMonsters: ERROR - Monster edict {} became invalid during or after ApplyMonsterBonusFlags (Champion)!\n", (int)(monster - g_edicts)); // Corrected format
							}
							// Mark overall spawn as failed and stop processing this monster
							spawn_successful_for_this_monster = false;
							// Skip the rest of the bonus/effect logic for this invalid edict
							goto skip_invalid_monster_processing; // Use goto to jump past all remaining logic for this monster
						}
						if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters Debug: Returned from ApplyMonsterBonusFlags (Champion) for {}\n", monster - g_edicts); // Corrected format


						monster->item = G_HordePickItem(); // Champions always drop an item

						if (monster->item) {
							monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP; // Ensure drop flag is correct
						}

						champion_spawned_this_wave = true;
						champion_spawn_cooldown = irandom(15, 25);
						// Use GetDisplayName cautiously IF the monster pointer could be invalid, but check above helps
						gi.LocBroadcast_Print(PRINT_HIGH, "*** A Champion {} has appeared! ***\n", GetDisplayName(monster).c_str()); // LocBroadcast_Print might use C-style
						is_champion = true;
					}

					// --- Non-Champion Item Logic ---
					if (!is_champion) {
						const float drop_chance = currentLevel <= 5 ? 0.8f : (currentLevel <= 8 ? 0.6f : 0.45f);
						if (frandom() < drop_chance) {
							monster->item = G_HordePickItem();
							// Check if monster is still valid after G_HordePickItem (less likely issue)
							if (!monster->inuse) {
								if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: ERROR - Monster edict {} became invalid during G_HordePickItem (Non-Champion)!\n", (int)(monster - g_edicts)); // Corrected format
								spawn_successful_for_this_monster = false;
								goto skip_invalid_monster_processing;
							}
							if (monster->item) {
								monster->spawnflags &= ~SPAWNFLAG_MONSTER_NO_DROP;
							}
							else {
								monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;
							}
						}
						else {
							monster->spawnflags |= SPAWNFLAG_MONSTER_NO_DROP;
						}
					}

					// --- Armor Logic ---
					// Check monster->inuse again before calling SetMonsterArmor, just in case item logic had issues
					if (!monster->inuse) {
						if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: ERROR - Monster edict {} became invalid before SetMonsterArmor!\n", (int)(monster - g_edicts)); // Corrected format
						spawn_successful_for_this_monster = false;
						goto skip_invalid_monster_processing;
					}
					if (currentLevel >= 14 && monster->monsterinfo.power_armor_type == IT_NULL && monster->monsterinfo.armor_type == IT_NULL) {
						if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters Debug: Calling SetMonsterArmor for {}\n", monster - g_edicts); // Corrected format
						SetMonsterArmor(monster);
						// Check if monster is still valid after SetMonsterArmor
						if (!monster->inuse) {
							if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: ERROR - Monster edict {} became invalid during SetMonsterArmor!\n", (int)(monster - g_edicts)); // Corrected format
							spawn_successful_for_this_monster = false;
							goto skip_invalid_monster_processing;
						}
						if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters Debug: Returned from SetMonsterArmor for {}\n", monster - g_edicts); // Corrected format
					}

					// --- Final Effects ---
					// Final check before accessing monster->s.origin
					if (!monster->inuse) {
						if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: ERROR - Monster edict {} became invalid before final effects!\n", (int)(monster - g_edicts)); // Corrected format
						spawn_successful_for_this_monster = false;
						goto skip_invalid_monster_processing;
					}
					SpawnGrow_Spawn(monster->s.origin, 80.0f, 10.0f);
					gi.sound(monster, CHAN_AUTO, sound_spawn1, 1, ATTN_NORM, 0);

					// --- Success Finalization (Only if monster is still valid) ---
					// Update Spawn Point & Trackers
					if (used_alternative) ApplySuccessfulAlternativeCooldown(spawn_point);
					else OnSuccessfulSpawn(spawn_point);
					horde::g_monsterSpawnTracker.SetLastSpawnTime(monster_type, level.time);

					// Reset Failure Counters
					g_consecutive_spawn_failures = 0;
					if (g_recovery_mode_active) { // Deactivate recovery mode on success
						g_recovery_mode_active = false;
						if (g_original_wave_type_before_recovery != MonsterWaveType::None) {
							current_wave_type = g_original_wave_type_before_recovery;
							g_original_wave_type_before_recovery = MonsterWaveType::None;
							if (developer->integer) gi.Com_PrintFmt("RECOVERY MODE DEACTIVATED (Spawn Success): Restored original wave type.\n"); // {} not needed here
						}
						else if (developer->integer) {
							gi.Com_PrintFmt("RECOVERY MODE DEACTIVATED (Spawn Success).\n"); // {} not needed here
						}
					}

					// This label is jumped to if monster becomes invalid during processing
				skip_invalid_monster_processing:;

					// Break inner loop (spawn point search), whether successful or aborted due to invalid monster
					break;

				}
				else { // SpawnMonsterByTypeID failed internally
					if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: SpawnMonsterByTypeID failed internally for type {} at point {} (Pos: {}).\n", (int)monster_type, (int)(spawn_point - g_edicts), final_origin); // Corrected format
					// Penalize the point that was attempted
					if (used_alternative) ApplyAlternativePositionCooldown(spawn_point);
					else IncreaseSpawnAttempts(spawn_point);
					g_consecutive_spawn_failures++;
					// Continue inner loop (try next spawn point)
				}
			} // End if (found_valid_spot)
			// else: No valid spot found, failure handled earlier, continue inner loop
		} // End inner spawn point check loop (`while points_checked_for_this_monster < total_potential_points`)

		// Check if this specific monster failed to spawn (even if others in batch might succeed)
		if (!spawn_successful_for_this_monster) {
			if (developer->integer > 1) gi.Com_PrintFmt("SpawnMonsters: Failed to find suitable spawn point OR monster became invalid for attempt {} in batch.\n", i + 1); // Corrected format
			// Failure counter already incremented inside the loop if needed
		}
	} // End outer batch loop (`for i < spawnable_this_call`)

	// --- Post-Spawn Processing ---
	if (spawned_count_this_call == 0 && spawnable_this_call > 0) {
		if (developer->integer) gi.Com_PrintFmt("SpawnMonsters: Entire batch failed to spawn any monsters. Consecutive failures now: {}\n", g_consecutive_spawn_failures); // Corrected format
	}

	SetNextMonsterSpawnTime(mapSize);
	if (champion_spawn_cooldown > 0) {
		champion_spawn_cooldown--;
	}

	return last_spawned_this_call; // Return last successfully spawned monster this frame (or nullptr)
}

	static void SetMonsterArmor(edict_t * monster) {
		// Cache frequently used constants to avoid recalculating
		static constexpr float HEALTH_RATIO_POW = 1.1f;
		static constexpr float SIZE_FACTOR_POW = 0.7f;
		static constexpr float MASS_FACTOR_POW = 0.6f;
		static constexpr float BASE_ARMOR = 75.0f;
		static constexpr float MAX_HEALTH_ARMOR_FACTOR = 0.2f;

		// Get spawn temp once
		const spawn_temp_t& st = ED_GetSpawnTemp();

		// Assign default armor type if not specified
		if (!st.was_key_specified("power_armor_power")) {
			monster->monsterinfo.armor_type = IT_ARMOR_COMBAT;
		}

		// Calculate base factors once
		const float health_ratio = monster->health / static_cast<float>(monster->max_health);
		const float size_factor = (monster->maxs - monster->mins).length() / 64.0f;
		const float mass_factor = std::min(monster->mass / 200.0f, 1.5f);

		// Pre-compute level scaling factor
		float level_scaling;
		// Use if-else instead of switch for better optimization with constants
		if (current_wave_level <= 15) {
			level_scaling = 1.0f + (current_wave_level * 0.04f);
		}
		else if (current_wave_level <= 25) {
			level_scaling = 1.6f + ((current_wave_level - 15) * 0.06f);
		}
		else {
			level_scaling = 2.2f + ((current_wave_level - 25) * 0.08f);
		}

		// Compute base armor power
		float armor_power = (BASE_ARMOR + monster->max_health * MAX_HEALTH_ARMOR_FACTOR);

		// Use pre-computed powers or fast approximations
		armor_power *= powf(health_ratio, HEALTH_RATIO_POW);
		armor_power *= powf(size_factor, SIZE_FACTOR_POW);
		armor_power *= powf(mass_factor, MASS_FACTOR_POW);
		armor_power *= level_scaling;

		// Apply level-based multiplier
		float armor_multiplier = current_wave_level <= 30 ? 0.4f :
			current_wave_level <= 40 ? 0.5f : 0.6f;
		armor_power *= armor_multiplier;

		// Difficulty adjustment
		if (g_insane->integer) {
			armor_power *= 1.2f;
		}
		else if (g_chaotic->integer) {
			armor_power *= 1.1f;
		}

		// Apply random factor - use faster random generation
		const float random_factor = 0.9f + (0.2f * frandom());
		armor_power *= random_factor;

		// High-level bonus
		if (current_wave_level > 25) {
			armor_power *= 1.0f + ((current_wave_level - 25) * 0.03f);
		}

		// Calculate min/max armor values efficiently
		const int min_armor = std::max(25, static_cast<int>(monster->max_health * 0.08f));
		const int max_armor = static_cast<int>(monster->max_health *
			(current_wave_level > 25 ? 1.5f : 1.2f));

		// Clamp final armor value 
		const int final_armor = std::clamp(static_cast<int>(armor_power), min_armor, max_armor);

		// Assign power directly based on armor type
		if (monster->monsterinfo.power_armor_type == IT_NULL) {
			monster->monsterinfo.power_armor_power = 0;
		}
		else if (monster->monsterinfo.power_armor_type == IT_ITEM_POWER_SHIELD ||
			monster->monsterinfo.power_armor_type == IT_ITEM_POWER_SCREEN) {
			monster->monsterinfo.power_armor_power = final_armor;
		}

		// Assign armor power
		if (monster->monsterinfo.armor_type == IT_NULL) {
			monster->monsterinfo.armor_power = 0;
		}
		else if (monster->monsterinfo.armor_type == IT_ARMOR_COMBAT) {
			monster->monsterinfo.armor_power = final_armor;
		}
	}

	static void SetNextMonsterSpawnTime(const MapSize& mapSize) {
		// Original spawn time ranges (Big maps are faster)
		constexpr std::array<std::pair<gtime_t, gtime_t>, 3> BASE_SPAWN_TIMES = { {
			{0.6_sec, 0.8_sec},  // Small maps
			{0.8_sec, 1.0_sec},  // Medium maps
			{0.4_sec, 0.6_sec}   // Big maps
		} };

		// Apply early wave modifier (waves 1-10) - slows down spawn rate initially
		float earlyWaveMultiplier = 1.0f;
		// Check level is valid before calculation
		if (g_horde_local.level >= 1 && g_horde_local.level <= 10) {
			// Linearly decreases interval multiplier from 2.0x at wave 1 down to 1.1x at wave 10.
			// After wave 10, the multiplier is 1.0x (no modification).
			earlyWaveMultiplier = 2.0f - ((static_cast<float>(g_horde_local.level) - 1.0f) * 0.1f);
			// Clamp just in case level somehow goes outside 1-10 despite the check
			earlyWaveMultiplier = std::clamp(earlyWaveMultiplier, 1.1f, 2.0f);
		}

		// Select base times based on map size
		const size_t mapIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);
		const auto& [base_min_time, base_max_time] = BASE_SPAWN_TIMES[mapIndex];

		// Apply the early wave multiplier to get the target time range
		const gtime_t min_time = gtime_t::from_sec(base_min_time.seconds() * earlyWaveMultiplier);
		const gtime_t max_time = gtime_t::from_sec(base_max_time.seconds() * earlyWaveMultiplier);

		// Calculate the random interval within the adjusted range
		// Ensure min_time <= max_time before calling random_time to avoid potential issues
		const gtime_t calculated_interval = (min_time <= max_time) ?
			random_time(min_time, max_time) :
			min_time; // Fallback to min_time if somehow inverted

		// --- CLAMPING ---
		// Ensure the final interval is never less than the absolute minimum allowed
		const gtime_t final_interval = std::max(calculated_interval, HordeConstants::MIN_MONSTER_SPAWN_INTERVAL);

		// Set the time for the next spawn attempt
		g_horde_local.monster_spawn_time = level.time + final_interval;

		//// Optional Debugging
		//if (developer->integer > 2) {
		//	gi.Com_PrintFmt("SetNextMonsterSpawnTime: Level={}, MapIdx={}, EarlyMult={:.2f}, Base=[{:.2f}-{:.2f}], Adjusted=[{:.2f}-{:.2f}], Calculated={:.2f}, Final={:.2f}\n",
		//		g_horde_local.level,
		//		mapIndex,
		//		earlyWaveMultiplier,
		//		base_min_time.seconds(), base_max_time.seconds(),
		//		min_time.seconds(), max_time.seconds(),
		//		calculated_interval.seconds(),
		//		final_interval.seconds());
		//}
	}
	// Usar enum class para mejorar la seguridad de tipos
	enum class MessageType {
		Standard,
		Chaotic,
		Insane
	};

	static void CalculateTopDamager(PlayerStats & topDamager, float& percentage) {
		constexpr int32_t MAX_DAMAGE = 100000;
		int32_t total_damage = 0;
		topDamager = PlayerStats(); // Reset stats

		// First pass - calculate total damage
		for (const auto& player : active_players()) {
			if (!player || !player->client || !player->inuse)
				continue;

			// Fix 1: Initialize const variable with explicit casting to resolve type ambiguity
			const int32_t player_damage = static_cast<int32_t>(std::min(
				static_cast<int32_t>(player->client->total_damage),
				MAX_DAMAGE));

			if (player_damage > 0) {
				total_damage += player_damage;
			}
		}

		// Second pass - find top damager
		for (const auto& player : active_players()) {
			if (!player || !player->client || !player->inuse)
				continue;

			// Fix 2: Same explicit casting approach for the second instance
			const int32_t player_damage = static_cast<int32_t>(std::min(
				static_cast<int32_t>(player->client->total_damage),
				MAX_DAMAGE));

			if (player_damage > topDamager.total_damage) {
				topDamager.total_damage = player_damage;
				topDamager.player = player;
			}
		}

		// Calculate percentage with extra safety
		percentage = 0.0f;
		if (total_damage > 0 && topDamager.total_damage > 0) {
			percentage = std::min(
				(static_cast<float>(topDamager.total_damage) / total_damage) * 100.0f,
				100.0f);
			percentage = std::round(percentage * 100.0f) / 100.0f;
		}
	}

	struct RewardInfo {
		item_id_t item_id;
		int weight;  // Higher weight = more common

		// Optional: Add constructor for cleaner initialization below
		constexpr RewardInfo(item_id_t id, int w) : item_id(id), weight(w) {}
	};

	// Define the rewards ONLY ONCE in this array
	static const std::array<RewardInfo, 3> TOP_DAMAGER_REWARDS = { {
		{IT_ITEM_BANDOLIER, 60},    // More common
		{IT_ITEM_SENTRYGUN, 30},     // Less common
		{IT_AMMO_NUKE, 2}    // More common
	} };

	// Pre-computed total reward weight, calculated directly from the single array
	static const int TOTAL_REWARD_WEIGHT = [] {
		int total = 0;
		// Calculate sum directly from the array
		for (const auto& reward : TOP_DAMAGER_REWARDS) {
			// Ensure weight is positive to avoid issues
			if (reward.weight > 0) {
				total += reward.weight;
			}
		}
		// Return 1 if total is somehow zero to prevent division by zero later
		return (total > 0) ? total : 1;
		}(); // Immediately invoke the lambda

	static bool GiveTopDamagerReward(const PlayerStats & topDamager, const std::string & playerName) {
		// Quick validation with early return
		if (!topDamager.player || !topDamager.player->inuse || !topDamager.player->client)
			return false;

		const int roll = irandom(1, TOTAL_REWARD_WEIGHT);
		item_id_t selectedItemId = TOP_DAMAGER_REWARDS[0].item_id; // Default fallback to the first item
		int currentWeight = 0;

		for (const auto& reward : TOP_DAMAGER_REWARDS) {
			if (reward.weight <= 0) continue; // Skip items with no weight

			currentWeight += reward.weight;
			if (roll <= currentWeight) {
				selectedItemId = reward.item_id;
				break; // Found the item
			}
		}

		// Get item by ID
		gitem_t* item = GetItemByIndex(selectedItemId);
		if (!item || !item->classname) {
			// Log error if item ID is invalid or item has no classname
			if (developer->integer) {
				gi.Com_PrintFmt("Error: GiveTopDamagerReward - Failed to get valid gitem_t for ID {}\n", static_cast<int>(selectedItemId));
			}
			return false;
		}


		// Spawn and give item directly to player 
		edict_t* entity = G_Spawn();
		if (!entity)
			return false;

		entity->classname = item->classname;
		entity->item = item;
		SpawnItem(entity, item, spawn_temp_t::empty);

		// Check if SpawnItem succeeded (entity might be freed if e.g., item shouldn't spawn in DM)
		if (!entity->inuse) {
			// SpawnItem already freed it, just return false
			return false;
		}

		// Give item to player
		Touch_Item(entity, topDamager.player, null_trace, true);
		// Touch_Item might free the entity if pickup is successful
		if (entity->inuse) {
			G_FreeEdict(entity); // Free if Touch_Item didn't consume it
		}


		// Announce reward
		const char* itemName = item->use_name ? item->use_name :
			(item->classname ? item->classname : "reward");

		gi.LocBroadcast_Print(PRINT_HIGH, "{} receives a {} for top damage!\n",
			playerName.empty() ? "Unknown Player" : playerName.c_str(),
			itemName);

		return true;
	}

	static void SendCleanupMessage(WaveEndReason reason) {
		// Avoid try-catch for performance in normal operation
		// Wave completion message - use switch for better performance
		switch (reason) {
		case WaveEndReason::AllMonstersDead:
			gi.LocBroadcast_Print(PRINT_HIGH, "Wave {} Completely Cleared - Perfect Victory!\n",
				g_horde_local.level);
			break;
		case WaveEndReason::MonstersRemaining:
			gi.LocBroadcast_Print(PRINT_HIGH, "Wave {} Pushed Back - But Still Threatening!\n",
				g_horde_local.level);
			break;
		case WaveEndReason::TimeLimitReached:
			gi.LocBroadcast_Print(PRINT_HIGH, "Wave {} Contained - Time Limit Reached!\n",
				g_horde_local.level);
			break;
		}

		// Calculate top damager once
		PlayerStats topDamager;
		float percentage = 0.0f;
		CalculateTopDamager(topDamager, percentage);

		// Only process if we have a valid player
		if (topDamager.player && topDamager.player->inuse && topDamager.player->client) {
			// Get player name once
			const std::string playerName = GetPlayerName(topDamager.player);

			// Send damage announcement
			gi.LocBroadcast_Print(PRINT_HIGH, "{} dealt the most damage with {}! ({}% of total)\n",
				playerName.c_str(), topDamager.total_damage, static_cast<int>(percentage));

			// Give reward and reset stats if successful
			if (GiveTopDamagerReward(topDamager, playerName)) {
				// Reset all player stats in one pass using iterator
				for (auto* player : active_players()) {
					if (player && player->client) {
						// Group related resets together for better cache coherence
						// Damage counters
						player->client->total_damage = 0;
						player->client->lastdmg = level.time;
						player->client->dmg_counter = 0;
						player->client->ps.stats[STAT_ID_DAMAGE] = 0;

						// Respawn states
						player->client->respawn_time = 0_sec;
						player->client->coop_respawn_state = COOP_RESPAWN_NONE;
						player->client->last_damage_time = level.time;
					}
				}
			}
		}
	}

	// Add this function in the appropriate source file that deals with spawn management.
	void CheckAndResetDisabledSpawnPoints() {
		// Find all active spawn points that are disabled
		for (unsigned int i = 1; i < globals.num_edicts; i++) {
			edict_t* ent = &g_edicts[i];
			if (ent && ent->inuse && ent->classname &&
				strcmp(ent->classname, "info_player_deathmatch") == 0) {

				auto& data = spawnPointsData[ent];
				if (data.isTemporarilyDisabled) {
					// Simply reset the disabled status
					data.isTemporarilyDisabled = false;
					data.attempts = 0;
					data.cooldownEndsAt = 0_sec;
				}
			}
		}
	}

	inline int32_t GetAdjustedMonsterCap(const MapSize & mapSize, int32_t waveLevel) {
		// Original base caps
		const int32_t baseSmallCap = MAX_MONSTERS_SMALL_MAP;  // 14
		const int32_t baseMediumCap = MAX_MONSTERS_MEDIUM_MAP; // 16
		const int32_t baseBigCap = MAX_MONSTERS_BIG_MAP;      // 32

		int32_t baseCap;

		// Determine base cap based on map size first
		if (mapSize.isSmallMap) {
			baseCap = baseSmallCap;
		}
		else if (mapSize.isMediumMap) {
			baseCap = baseMediumCap;
		}
		else { // Big map
			baseCap = baseBigCap;
		}

		// Apply early wave reduction (only for non-big maps)
		if (waveLevel <= 10 && !mapSize.isBigMap) {
			// Scale from 60% at wave 1 up to 100% at wave 10
			float reductionFactor = 0.6f + ((waveLevel - 1) * 0.0444f); // (1.0 - 0.6) / 9 = 0.0444...
			reductionFactor = std::clamp(reductionFactor, 0.6f, 1.0f); // Ensure it stays within bounds
			baseCap = static_cast<int32_t>(baseCap * reductionFactor);
		}

		// --- Player Count Bonus ---
		int32_t finalAdjustedCap = baseCap; // Start with the potentially reduced base cap
		const int32_t numHumanPlayers = GetNumHumanPlayers();

		if (numHumanPlayers > 1) {
			constexpr int32_t MAX_BONUS_PLAYERS = 3; // Cap bonus contribution at 3 extra players (i.e., players 2, 3, 4)
			constexpr int32_t BONUS_PER_PLAYER = 2;  // +2 monsters per extra contributing player
			const int32_t extraPlayers = std::min(numHumanPlayers - 1, MAX_BONUS_PLAYERS);
			const int32_t playerBonus = extraPlayers * BONUS_PER_PLAYER;
			finalAdjustedCap += playerBonus;

			if (developer->integer > 1) {
				gi.Com_PrintFmt("GetAdjustedMonsterCap: Wave={}, BaseCap={}, Humans={}, BonusPlayers={}, Bonus={}, FinalCap={}\n",
					waveLevel, baseCap, numHumanPlayers, extraPlayers, playerBonus, finalAdjustedCap);
			}
		}
		else {
			if (developer->integer > 1) {
				gi.Com_PrintFmt("GetAdjustedMonsterCap: Wave={}, BaseCap={}, Humans={}, No Bonus, FinalCap={}\n",
					waveLevel, baseCap, numHumanPlayers, finalAdjustedCap);
			}
		}
		// --- End Player Count Bonus ---

		// Ensure the cap doesn't go below a minimum reasonable value (e.g., 6)
		finalAdjustedCap = std::max(6, finalAdjustedCap);

		// Store globally
		g_adjusted_monster_cap = finalAdjustedCap;

		return g_adjusted_monster_cap;
	}

	void Horde_RunFrame() {
		// Cache frequently used state variables
		const MapSize& mapSize = g_horde_local.current_map_size;
		const int32_t currentLevel = g_horde_local.level;
		const horde_state_t currentState = g_horde_local.state;
		const gtime_t currentTime = level.time;

		// Regular maintenance
		CleanupSpawnPointCache();
		CheckAndReduceSpawnCooldowns();

		// Safety check for high waves - detect stuck states
		static gtime_t last_wave_change_time = 0_sec;
		static int32_t last_wave_level = 0;

		if (need_frame_timer_reset) {
			last_wave_change_time = 0_sec;
			last_wave_level = 0;
			need_frame_timer_reset = false;
		}

		if (last_wave_level != currentLevel) {
			last_wave_change_time = currentTime;
			last_wave_level = currentLevel;
		}
		else if (currentTime - last_wave_change_time > 3_min &&
			currentState != horde_state_t::warmup &&
			GetStroggsNum() == 0) {
			// Force transition if stuck in same wave for too long
			if (developer->integer) {
				gi.Com_PrintFmt("CRITICAL: Wave {} stuck for over 3 minutes. Forcing progression.\n",
					currentLevel);
			}
			g_horde_local.state = horde_state_t::spawning;
			g_horde_local.monster_spawn_time = currentTime + 1.5_sec;
			Horde_InitLevel(currentLevel + 1);
			return;
		}

		// Handle custom monster settings
		if (dm_monsters->integer >= 1) {
			g_horde_local.num_to_spawn = dm_monsters->integer;
			g_horde_local.queued_monsters = 0;
			ClampNumToSpawn(mapSize);
		}

		bool waveEnded = false;
		WaveEndReason currentWaveEndReason = WaveEndReason::AllMonstersDead;

		// STATE MACHINE
		switch (g_horde_local.state) {
		case horde_state_t::warmup:
			if (g_horde_local.warm_time < currentTime) {
				g_horde_local.state = horde_state_t::spawning;
				Horde_InitLevel(1);
				current_wave_level = 1;
				PlayWaveStartSound();
				DisplayWaveMessage();
			}
			break;

		case horde_state_t::spawning: {
			// Failsafe for stuck in spawning state
			static gtime_t spawning_start_time = 0_sec;
			if (spawning_start_time == 0_sec) {
				spawning_start_time = currentTime;
			}
			else if (currentTime - spawning_start_time > 25_sec) {
				if (!next_wave_message_sent) {
					VerifyAndAdjustBots();
					//gi.LocBroadcast_Print(PRINT_CENTER,
					//	"\n\n\nWave deployment timeout. Moving to active phase.\n");
					next_wave_message_sent = true;
				}
				g_horde_local.state = horde_state_t::active_wave;
				spawning_start_time = 0_sec;
				break;
			}

			// Independent time limit check
			const gtime_t independentTimeLimit = g_independent_timer_start + g_lastParams.independentTimeThreshold;
			if (currentTime >= independentTimeLimit) {
				currentWaveEndReason = WaveEndReason::TimeLimitReached;
				waveEnded = true;
				spawning_start_time = 0_sec;
				break;
			}

			if (g_horde_local.monster_spawn_time <= currentTime) {
				// Boss wave handling
				if (currentLevel >= 10 && currentLevel % 5 == 0 && !boss_spawned_for_wave) {
					SpawnBossAutomatically();
				}

				const int32_t activeMonsters = CalculateRemainingMonsters();
				const int32_t maxMonsters = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap :
					(mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
						(mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP));

				// Queue transfer handling
				if (g_horde_local.num_to_spawn <= 0 && g_horde_local.queued_monsters > 0 &&
					activeMonsters < maxMonsters) {

					const int32_t base_transfer = mapSize.isSmallMap ? 3 : (mapSize.isBigMap ? 5 : 4);
					const int32_t transfer_amount = std::min({
						g_horde_local.queued_monsters,
						maxMonsters - activeMonsters,
						base_transfer
						});

					if (transfer_amount > 0) {
						g_horde_local.num_to_spawn += transfer_amount;
						g_horde_local.queued_monsters -= transfer_amount;
					}
				}

				// Spawn monsters
				if (g_horde_local.num_to_spawn > 0 && activeMonsters < maxMonsters) {
					SpawnMonsters();
				}

				// Check if all monsters have been spawned
				if (g_horde_local.num_to_spawn == 0 && g_horde_local.queued_monsters == 0) {
					if (!next_wave_message_sent) {
						VerifyAndAdjustBots();
						gi.LocBroadcast_Print(PRINT_CENTER,
							"\n\n\nWave Fully Deployed.\nWave Level: {}\n",
							currentLevel);
						next_wave_message_sent = true;
					}
					g_horde_local.state = horde_state_t::active_wave;
					spawning_start_time = 0_sec;
				}

				// Set next spawn attempt time
				SetNextMonsterSpawnTime(mapSize);
			}
			break;
		}

		case horde_state_t::active_wave: {

			// Fast path for all monsters dead
			if (Horde_AllMonstersDead()) {
				currentWaveEndReason = WaveEndReason::AllMonstersDead;
				waveEnded = true;
				break;
			}

			// Check wave completion conditions
			if (CheckRemainingMonstersCondition(mapSize, currentWaveEndReason)) {
				waveEnded = true;
				break;
			}

			// Spawn queued monsters periodically
			if (g_horde_local.monster_spawn_time <= currentTime) {
				const int32_t activeMonsters = CalculateRemainingMonsters();
				const int32_t maxMonsters = g_adjusted_monster_cap > 0 ? g_adjusted_monster_cap :
					(mapSize.isSmallMap ? MAX_MONSTERS_SMALL_MAP :
						(mapSize.isMediumMap ? MAX_MONSTERS_MEDIUM_MAP : MAX_MONSTERS_BIG_MAP));

				// Quick queue transfer if there's room
				if (activeMonsters < maxMonsters && g_horde_local.queued_monsters > 0) {
					const int32_t available_slots = maxMonsters - activeMonsters;
					const int32_t transfer_amount = std::min(g_horde_local.queued_monsters, available_slots);

					if (transfer_amount > 0) {
						g_horde_local.num_to_spawn = transfer_amount;
						g_horde_local.queued_monsters -= transfer_amount;
						SpawnMonsters();
					}
				}

				// Set next spawn time
				SetNextMonsterSpawnTime(mapSize);
			}

			// Check for stuck queued monsters
			static gtime_t last_queue_check = 0_sec;
			static int consecutive_stuck_checks = 0;

			if (currentTime - last_queue_check > 2_sec) {
				last_queue_check = currentTime;

				// Fix negative queue size
				if (g_horde_local.queued_monsters < 0) {
					g_horde_local.queued_monsters = 0;
				}

				// Check for stuck queue
				if (g_horde_local.queued_monsters > 0 && g_horde_local.num_to_spawn == 0) {
					consecutive_stuck_checks++;

					if (consecutive_stuck_checks >= 3) {
						// Unstick queue - move some to spawn pool
						const int32_t to_move = std::min(g_horde_local.queued_monsters, 5);
						g_horde_local.num_to_spawn += to_move;
						g_horde_local.queued_monsters -= to_move;
						consecutive_stuck_checks = 0;
					}
				}
				else {
					consecutive_stuck_checks = 0;
				}
			}
			break;
		}

		case horde_state_t::cleanup:
			if (g_horde_local.monster_spawn_time < currentTime) {
				HandleWaveCleanupMessage(mapSize, currentWaveEndReason);
				g_horde_local.warm_time = currentTime + random_time(0.8_sec, 1.5_sec);
				g_horde_local.state = horde_state_t::rest;

				// Reset spawn points for next wave
				CheckAndResetDisabledSpawnPoints();
			}
			break;

		case horde_state_t::rest:
			if (g_horde_local.warm_time < currentTime) {
				AnnounceIncomingWave(3_sec);
				g_horde_local.state = horde_state_t::spawning;
				Horde_InitLevel(g_horde_local.level + 1);
			}
			break;
		}

		// Cleanup logic - outside the switch for better clarity
		if (waveEnded) {
			SendCleanupMessage(currentWaveEndReason);
			g_horde_local.monster_spawn_time = currentTime + 0.5_sec;
			g_horde_local.state = horde_state_t::cleanup;

			// Reset variables for next wave
			ResetWaveAdvanceState();
		}

		if (horde_message_end_time != 0_sec && currentTime >= horde_message_end_time) {
			ClearHordeMessage();
		}
	}

	// Función para manejar el evento de reinicio
	void HandleResetEvent() {
		ResetGame();
	}

	// Get the remaining time for the current wave
	inline gtime_t GetWaveTimer() {
		const gtime_t currentTime = level.time;
		gtime_t remainingTime = 0_sec;

		// Calcular tiempo de condición si está activa
		if (g_horde_local.conditionTriggered && g_horde_local.waveEndTime > currentTime) {
			remainingTime = g_horde_local.waveEndTime - currentTime;
		}

		// Calcular tiempo independiente
		const gtime_t independentRemaining = g_independent_timer_start + g_lastParams.independentTimeThreshold - currentTime;

		// Siempre retornar el menor tiempo entre ambos si son válidos
		if (independentRemaining > 0_sec) {
			remainingTime = (remainingTime > 0_sec) ?
				std::min(remainingTime, independentRemaining) :
				independentRemaining;
		}

		return remainingTime;
	}

	// Helper functionget stroggs alive on the map
	inline int32_t GetStroggsNum() noexcept {
		return level.total_monsters - level.killed_monsters;
	}

	// Helper function to check if it's a boss wave
	inline bool IsBossWave() noexcept {
		return g_horde_local.level >= 10 && g_horde_local.level % 5 == 0;
	}

