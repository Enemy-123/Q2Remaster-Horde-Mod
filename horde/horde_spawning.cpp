//// Implementations for spawning-related functions will go here.
//
//#include "../g_local.h" // Include necessary base headers
//#include "g_horde.h"    // Include main horde header
//#include "horde_spawning.h" // Include its own header
//#include "horde_boss.h"   // May need boss declarations
//#include "../shared.h"
//#include <set> // Include necessary standard library headers
//#include <vector>
//#include <array>
//#include <string>
//#include <string_view>
//#include <numeric>
//#include <algorithm>
//#include <cmath>
//#include <random> // For std::shuffle if used later
//#include <unordered_set>
//#include <span>
//
//// Include other necessary headers from g_horde.cpp if needed
//#include "g_horde_benefits.h"
//#include "horde_ids.h"
//#include "../laser.h"
//
//// --- Moved from g_horde.cpp ---
//
//MonsterWaveType current_wave_type = MonsterWaveType::None;
//
//// Cache for potentially valid spawn points (pointers)
//static std::vector<edict_t*> g_potential_spawn_points;
//static bool g_spawn_points_cached = false;
//static size_t g_spawn_point_shuffle_index = 0; // Index for iterating shuffled list
//
//// State for failure tracking and recovery
//static int g_consecutive_spawn_failures = 0;
//static bool g_recovery_mode_active = false;
//static MonsterWaveType g_original_wave_type_before_recovery = MonsterWaveType::None;
//
//static bool need_spawn_cache_reset = false;
//static bool need_frame_timer_reset = false;
//static bool need_queue_monitor_reset = false;
//
////retaliation horde ( for when players are killing way too ez new/current wave in spawning state)
//
//static bool g_horde_retaliation_active = false;
//static gtime_t g_horde_retaliation_end_time = 0_sec;
//// Optional: Store the targeted player edict for focus (can be nullptr)
//static edict_t* g_horde_retaliation_target_player = nullptr;
//
//// Ambush system tracking variables
//static gtime_t last_ambush_time = 0_sec;
//static gtime_t ambush_cooldown_end = 0_sec;
//static int32_t waves_since_ambush = 0;
//static bool ambush_system_initialized = false;
//
//// --- Recent Spawn Position Tracking ---
//struct RecentSpawnPosition {
//	vec3_t position = {};
//	gtime_t cooldown_until = 0_sec;
//};
//static constexpr size_t MAX_RECENT_POSITIONS = 32; // History for TryAlternativeSpawnPosition
//static std::array<RecentSpawnPosition, MAX_RECENT_POSITIONS> g_recent_spawn_positions;
//static size_t g_recent_position_index = 0;
//
//// --- Recent Teleport Position Tracking ---
//struct RecentTeleportPosition {
//	vec3_t position = {};
//	gtime_t teleport_time = 0_sec;
//};
//static constexpr int MAX_RECENT_TELEPORT_LOCATIONS = 8; // History for CheckAndTeleportStuckMonster
//static std::array<RecentTeleportPosition, MAX_RECENT_TELEPORT_LOCATIONS> g_recent_teleport_positions;
//static int g_recent_teleport_index = 0;
//
//// --- Helper Functions ---
//void MarkPositionAsRecentlyUsed(const vec3_t& position) {
//	g_recent_spawn_positions[g_recent_position_index] = {
//		position,
//		level.time + HordeConstants::RECENT_SPAWN_COOLDOWN
//	};
//	g_recent_position_index = (g_recent_position_index + 1) % MAX_RECENT_POSITIONS;
//}
//
//bool IsPositionTooCloseToRecentSpawn(const vec3_t& position) {
//	const gtime_t current_time = level.time;
//	for (const auto& recent : g_recent_spawn_positions) {
//		if (recent.cooldown_until > current_time) {
//			if ((position - recent.position).lengthSquared() < HordeConstants::MIN_RECENT_SPAWN_DIST_SQ) {
//				return true;
//			}
//		}
//	}
//	return false;
//}
//
//void MarkPositionAsRecentlyTeleported(const vec3_t& position) {
//	g_recent_teleport_positions[g_recent_teleport_index] = {
//		position,
//		level.time + HordeConstants::RECENT_TELEPORT_COOLDOWN // Mark when it becomes not recent
//	};
//	g_recent_teleport_index = (g_recent_teleport_index + 1) % MAX_RECENT_TELEPORT_LOCATIONS;
//}
//
//bool IsPositionTooCloseToRecentTeleport(const vec3_t& position) {
//	const gtime_t current_time = level.time;
//	for (const auto& recent : g_recent_teleport_positions) {
//		// Check if the cooldown has expired (teleport_time stores the time *until* it's no longer recent)
//		if (recent.teleport_time > current_time) {
//			if ((position - recent.position).lengthSquared() < HordeConstants::MIN_RECENT_TELEPORT_DIST_SQ) {
//				return true;
//			}
//		}
//	}
//	return false;
//}
//
//// --- Global/Static Variables ---
//bool champion_spawned_this_wave = false; // Keep shared state here for now
//int champion_spawn_cooldown = 0;       // Keep shared state here for now
//int consistent_zero_counts = 0;        // Keep shared state here for now
//int counter_mismatch_frames = 0;       // Keep shared state here for now
//constexpr size_t MAX_SPAWN_POINTS = 32;
//int32_t g_adjusted_monster_cap = 0;
//uint16_t g_totalMonstersInWave = 0;
//gtime_t SPAWN_POINT_COOLDOWN = 2.8_sec; //spawns Cooldown
//
//// (Keep SpawnPointData and SpawnPointDataArray structs as they were)
//struct SpawnPointData {
//	uint16_t attempts = 0;
//	gtime_t teleport_cooldown = 0_sec;  // Added teleport_cooldown
//	gtime_t lastSpawnTime = 0_sec;      // Added lastSpawnTime
//	bool isTemporarilyDisabled = false;
//	gtime_t cooldownEndsAt = 0_sec;
//	int32_t successfulSpawns = 0;
//
//	// New fields for alternative position tracking
//	uint16_t alternative_attempts = 0;    // Added alternative_attempts
//	gtime_t alternative_cooldown = 0_sec; // Added alternative_cooldown
//	bool needs_long_alternative_cooldown = false; // Added needs_long_alternative_cooldown
//
//	// Update the success rate calculation method to accept current_time
//	float getSuccessRate(gtime_t current_time) const {
//		if (attempts == 0) return 1.0f;
//		// Use faster approximation - avoid division when possible
//		const float time_factor = (current_time - lastSpawnTime).seconds() >= 5.0f ?
//			1.0f : (current_time - lastSpawnTime).seconds() * 0.2f;
//		// Clamp time_factor to avoid excessive influence
//		const float clamped_time_factor = std::min(time_factor, 1.0f);
//		// Calculate base success rate, ensure attempts is not zero
//		const float base_rate = (attempts > 0) ? (float(successfulSpawns) / float(attempts)) : 1.0f;
//
//		// Combine base rate and time factor, ensuring result is between 0 and 1
//		return std::clamp(base_rate + clamped_time_factor, 0.0f, 1.0f);
//	}
//};
//struct SpawnPointDataArray {
//	SpawnPointData data[MAX_EDICTS];
//	SpawnPointData& operator[](const edict_t* ent) { return data[ent - g_edicts]; }
//	const SpawnPointData& operator[](const edict_t* ent) const { return data[ent - g_edicts]; }
//	void clear() { for (auto& item : data) item = SpawnPointData{}; }
//	bool find_and_access(const edict_t* key, SpawnPointData*& data_ptr) {
//		data_ptr = &data[key - g_edicts]; return true;
//	}
//};
//SpawnPointDataArray spawnPointsData;
//
//void ApplyAlternativePositionCooldown(edict_t* spawn_point) {
//	if (!spawn_point || !spawn_point->inuse) return;
//	auto& data = spawnPointsData[spawn_point];
//	data.alternative_attempts++;
//	gtime_t cooldown_duration;
//	if (data.alternative_attempts <= 2) cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_SHORT;
//	else if (data.alternative_attempts <= 5) cooldown_duration = HordeConstants::ALT_SPAWN_COOLDOWN_MEDIUM;
//	else {
//		cooldown_duration = 5.0_sec + gtime_t::from_sec(0.5f * (data.alternative_attempts - 5));
//		cooldown_duration = std::min(cooldown_duration, 10.0_sec);
//		if (data.alternative_attempts >= 8) data.needs_long_alternative_cooldown = true;
//	}
//
//	// Clamp the calculated alternative failure cooldown duration
//	const gtime_t final_alt_duration = std::max(cooldown_duration, HordeConstants::MIN_ALT_FAILURE_COOLDOWN);
//	data.alternative_cooldown = level.time + final_alt_duration;
//
//	data.isTemporarilyDisabled = true; // Also disable original point shortly
//	// Also clamp the normal point's shorter cooldown based on the *clamped* alternative duration
//	const gtime_t final_normal_duration = std::max(final_alt_duration * 0.5f, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
//	data.cooldownEndsAt = level.time + final_normal_duration;
//
//	if (developer->integer) gi.Com_PrintFmt("Alternative position cooldown applied to spawn at {}: {:.1f}s (attempts: {})\n", spawn_point->s.origin, final_alt_duration.seconds(), data.alternative_attempts);
//}
//
//void ApplySuccessfulAlternativeCooldown(edict_t* spawn_point) {
//	if (!spawn_point || !spawn_point->inuse) return;
//	auto& data = spawnPointsData[spawn_point];
//	data.alternative_attempts = 0;
//	data.needs_long_alternative_cooldown = false;
//	// Ensure the 3.0s meets the minimum alternative success cooldown
//	data.alternative_cooldown = level.time + std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN);
//	if (developer->integer > 1) gi.Com_PrintFmt("Success cooldown applied to spawn at {}: {:.1f}s\n", spawn_point->s.origin, std::max(3.0_sec, HordeConstants::MIN_ALT_SUCCESS_COOLDOWN).seconds());
//}
//void IncreaseSpawnAttempts(edict_t* spawn_point) {
//	if (!spawn_point || !spawn_point->inuse) return;
//	auto& data = spawnPointsData[spawn_point];
//	if (level.time - data.lastSpawnTime > 6_sec) { data = {}; return; } // Reset if long time passed
//
//	data.attempts++;
//	const float success_rate = data.getSuccessRate(level.time);
//	const int max_attempts = 4 + (success_rate >= 0.5f ? 2 : (success_rate >= 0.25f ? 1 : 0));
//
//	gtime_t calculated_duration = 0_sec; // Initialize
//
//	if (data.attempts >= max_attempts) {
//		data.isTemporarilyDisabled = true;
//		const float cooldown_factor = success_rate < 0.3f ? 1.5f : 0.75f;
//		const float attempt_multiplier = data.attempts <= 8 ? data.attempts * 0.25f : 2.0f;
//		calculated_duration = gtime_t::from_sec(cooldown_factor * attempt_multiplier);
//		if (developer->integer == 1) gi.Com_PrintFmt("SpawnPoint at {} inactivated for adaptive cooldown.\n", spawn_point->s.origin);
//	}
//	else if ((data.attempts & 1) == 0) { // Every 2 attempts
//		calculated_duration = gtime_t::from_sec(0.2f * data.attempts);
//	}
//
//	// Apply minimum duration clamp if a duration was calculated
//	if (calculated_duration > 0_sec) {
//		const gtime_t final_duration = std::max(calculated_duration, HordeConstants::MIN_INDIVIDUAL_FAILURE_COOLDOWN);
//		data.cooldownEndsAt = level.time + final_duration;
//	}
//	// No else needed, if no duration was calculated, cooldownEndsAt isn't set here
//}
//
//void OnSuccessfulSpawn(edict_t* spawn_point) {
//	if (!spawn_point || !spawn_point->inuse) return;
//	auto& data = spawnPointsData[spawn_point];
//	data.successfulSpawns++;
//	data.attempts = 0;
//	data.isTemporarilyDisabled = false;
//	// Use the minimum success cooldown constant
//	data.cooldownEndsAt = level.time + HordeConstants::MIN_INDIVIDUAL_SUCCESS_COOLDOWN;
//	horde::g_spawnPointTimeTracker.SetLastSpawnTime(spawn_point, level.time);
//}
//struct SpawnPointCache {
//	gtime_t last_check_time = 0_sec;
//	vec3_t last_check_origin = {};
//	gtime_t frame_time = 0_sec;         // Frame time of the last check
//	bool was_occupied_by_player = false; // True if a player was found in the last check
//	bool has_obstacle = false;          // True if a monster/defense was found (excluding player)
//};
//
//struct SpawnPointCacheArray {
//	SpawnPointCache data[MAX_EDICTS];
//	SpawnPointCache& operator[](const edict_t* ent) { return data[ent - g_edicts]; }
//	const SpawnPointCache& operator[](const edict_t* ent) const { return data[ent - g_edicts]; }
//	void clear() { for (auto& item : data) item = SpawnPointCache{}; }
//};
//static SpawnPointCacheArray spawn_point_cache;
//
//bool IsSpawnPointOccupied(const edict_t* spawn_point, const edict_t* ignore_ent = nullptr) {
//	// --- Basic Validation ---
//	if (!spawn_point || !spawn_point->inuse || !is_valid_vector(spawn_point->s.origin)) {
//		if (developer->integer) {
//			if (!spawn_point) gi.Com_PrintFmt("Warning: Null spawn_point passed to IsSpawnPointOccupied\n");
//			else gi.Com_PrintFmt("Warning: Invalid origin vector in spawn point {}\n", spawn_point->s.origin);
//		}
//		return true; // Safer to assume occupied if invalid
//	}
//
//	// --- Cache Check ---
//	SpawnPointCache& cache = spawn_point_cache[spawn_point];
//	// Increased cache duration slightly for better performance vs accuracy balance
//	static constexpr auto CACHE_DURATION = 100_ms; // 0.1 seconds
//
//	// Check time-based cache validity AND frame validity
//	if (level.time - cache.last_check_time < CACHE_DURATION &&
//		cache.last_check_origin == spawn_point->s.origin &&
//		(level.time - cache.frame_time) < FRAME_TIME_MS * 2) // Allow cache reuse within 2 frames
//	{
//		// Return the cached player occupation status.
//		// The caller (SelectRandomSpawnPoint) will use cache.has_obstacle separately
//		// if this function returns false.
//		return cache.was_occupied_by_player;
//	}
//
//	// --- Cache Miss - Perform Live Check ---
//	cache.last_check_time = level.time;
//	cache.last_check_origin = spawn_point->s.origin;
//	cache.frame_time = level.time;
//	// Reset flags before checking
//	cache.was_occupied_by_player = false;
//	cache.has_obstacle = false;
//
//	// Define bounding box for the check - use static const for efficiency
//	static const vec3_t mins_scale = vec3_t{ 16, 16, 24 }.scaled(vec3_t{ 1.75f, 1.75f, 1.75f });
//	static const vec3_t maxs_scale = vec3_t{ 16, 16, 32 }.scaled(vec3_t{ 1.75f, 1.75f, 1.75f });
//	const vec3_t spawn_mins = spawn_point->s.origin - mins_scale;
//	const vec3_t spawn_maxs = spawn_point->s.origin + maxs_scale;
//
//	// --- BoxEdicts Checks ---
//	// We need to check for players AND obstacles separately to update the cache correctly.
//	FilterData check_data = { ignore_ent, 0 }; // Reusable filter data
//
//	// Check 1: Players
//	gi.BoxEdicts(spawn_mins, spawn_maxs, nullptr, 0, AREA_SOLID,
//		[](edict_t* ent, void* data) -> BoxEdictsResult_t {
//			FilterData* fd = static_cast<FilterData*>(data);
//			if (ent == fd->ignore_ent) return BoxEdictsResult_t::Skip;
//			// Check only for active clients (players or bots)
//			if (ent->client && ent->inuse) {
//				fd->count++;
//				return BoxEdictsResult_t::End; // Found a player/bot
//			}
//			return BoxEdictsResult_t::Skip;
//		}, &check_data);
//
//	cache.was_occupied_by_player = (check_data.count > 0);
//
//	// Check 2: Obstacles (Monsters/Defenses) - ONLY if not already blocked by a player
//	if (!cache.was_occupied_by_player) {
//		check_data.count = 0; // Reset count for the next check
//		gi.BoxEdicts(spawn_mins, spawn_maxs, nullptr, 0, AREA_SOLID,
//			[](edict_t* ent, void* data) -> BoxEdictsResult_t {
//				FilterData* fd = static_cast<FilterData*>(data);
//				if (ent == fd->ignore_ent) return BoxEdictsResult_t::Skip;
//				// Check for live monsters OR player defenses
//				if ((ent->svflags & SVF_MONSTER && !ent->deadflag) ||
//					(ent->inuse && IsPlayerDefense(ent)))
//				{
//					fd->count++;
//					return BoxEdictsResult_t::End; // Found an obstacle
//				}
//				return BoxEdictsResult_t::Skip;
//			}, &check_data);
//
//		cache.has_obstacle = (check_data.count > 0);
//	}
//	else {
//		// If occupied by player, we don't need to check for other obstacles separately for the cache.has_obstacle flag,
//		// because the player block takes precedence. We can optionally set has_obstacle=true here too,
//		// but it doesn't change the outcome as was_occupied_by_player is already true.
//		// Let's keep it simple: if player is there, only was_occupied_by_player matters for the return value.
//		// The obstacle check only sets cache.has_obstacle if no player was found.
//		cache.has_obstacle = false;
//	}
//
//
//	// This function's primary job is to say if a *player* directly blocks the spawn.
//	// The 'has_obstacle' flag in the cache is used by SelectRandomSpawnPoint/SpawnMonsters
//	// to decide if alternative positions should be tried.
//	return cache.was_occupied_by_player;
//}
//// --- Corrected SelectRandomSpawnPoint ---
//template <typename TFilter>
//edict_t* SelectRandomSpawnPoint(TFilter filter) {
//	edict_t* availableSpawns[MAX_SPAWN_POINTS]{};
//	edict_t* occupiedButUsableSpawns[MAX_SPAWN_POINTS]{};
//	int availableCount = 0;
//	int occupiedCount = 0;
//
//	for (edict_t* spawnPoint : monster_spawn_points()) {
//		if (!spawnPoint || !spawnPoint->inuse || !is_valid_vector(spawnPoint->s.origin)) continue;
//		const auto& data = spawnPointsData[spawnPoint]; // Get cooldown data
//		if (data.isTemporarilyDisabled && level.time < data.cooldownEndsAt) continue; // Check normal cooldown
//		if (level.time < data.alternative_cooldown) continue; // Check alternative cooldown
//
//		// Apply the custom filter first
//		if (filter(spawnPoint)) {
//			// Check if a player is directly blocking the spawn
//			if (IsSpawnPointOccupied(spawnPoint)) {
//				// Player is blocking, this point is completely unusable for direct spawn.
//				// IsSpawnPointOccupied handles cache update.
//				// Do NOT add to any list for this function's purpose.
//				if (developer->integer > 2) gi.Com_PrintFmt("SelectRandomSpawnPoint: Point {} skipped (player occupied).\n", (int)(spawnPoint - g_edicts));
//				continue; // Skip this point entirely
//			}
//			else {
//				// Not blocked by a player. Now check the cache for non-player obstacles.
//				const SpawnPointCache& cache = spawn_point_cache[spawnPoint]; // Get cache entry (already updated by IsSpawnPointOccupied call)
//				if (cache.has_obstacle) {
//					// Blocked by monster/defense - add to the list for potential alternative spawn.
//					if (occupiedCount < MAX_SPAWN_POINTS) {
//						occupiedButUsableSpawns[occupiedCount++] = spawnPoint;
//					}
//					if (developer->integer > 2) gi.Com_PrintFmt("SelectRandomSpawnPoint: Point {} added to occupiedButUsable (obstacle present).\n", (int)(spawnPoint - g_edicts));
//				}
//				else {
//					// Not blocked by player AND no obstacle found -> add to available list.
//					if (availableCount < MAX_SPAWN_POINTS) {
//						availableSpawns[availableCount++] = spawnPoint;
//					}
//					if (developer->integer > 2) gi.Com_PrintFmt("SelectRandomSpawnPoint: Point {} added to available.\n", (int)(spawnPoint - g_edicts));
//				}
//			}
//		}
//	}
//
//	// Prioritize completely available spawns
//	if (availableCount > 0) {
//		const size_t idx = irandom(availableCount);
//		if (developer->integer > 1) gi.Com_PrintFmt("SelectRandomSpawnPoint: Selected available point {}\n", (int)(availableSpawns[idx] - g_edicts));
//		return availableSpawns[idx];
//	}
//
//	// If no completely available spawns, try one blocked by an obstacle
//	if (occupiedCount > 0) {
//		const size_t idx = irandom(occupiedCount);
//		if (developer->integer > 1) gi.Com_PrintFmt("SelectRandomSpawnPoint: Selected obstacle-occupied point {} (will try alternative).\n", (int)(occupiedButUsableSpawns[idx] - g_edicts));
//		return occupiedButUsableSpawns[idx]; // Let SpawnMonsters handle TryAlternative
//	}
//
//	if (developer->integer > 1) gi.Com_PrintFmt("SelectRandomSpawnPoint: No suitable spawn points found.\n");
//	return nullptr; // No valid spawn points found
//}
//
//static void CleanupSpawnPointCache() noexcept { spawn_point_cache.clear(); }
//
//// --- Moved Implementations ---
//
//// Function to check and reduce spawn cooldowns when few monsters remain
//void CheckAndReduceSpawnCooldowns() {
//	// Only proceed if fewer than 7 stroggs remain and not in a boss wave
//	const int32_t remaining_stroggs = GetStroggsNum();
//	if (remaining_stroggs > 6 || IsBossWave()) {
//		return;
//	}
//
//	// Track if we found any valid cooldowns to reset
//	bool found_cooldowns_to_reset = false;
//	const gtime_t current_time = level.time;
//
//	// Pre-compute the reduction factor once
//	constexpr float REDUCTION_FACTOR = 0.4f;  // Changed from 0.15f to 0.4f to make late-wave cooldown reduction less aggressive
//
//	// Process all spawn points in use
//	// We need to track the spawn points to process
//	std::vector<edict_t*> spawn_points;
//	spawn_points.reserve(MAX_SPAWN_POINTS);
//
//	// Find all active spawn points by scanning entities
//	for (uint32_t i = 1; i < globals.num_edicts; i++) {
//		edict_t* ent = &g_edicts[i];
//		if (ent && ent->inuse && ent->classname &&
//			!strcmp(ent->classname, "info_player_deathmatch")) {
//			spawn_points.push_back(ent);
//		}
//	}
//
//	// Process spawn points with early termination after collecting
//	for (edict_t* spawn_point : spawn_points) {
//		auto& data = spawnPointsData[spawn_point];
//
//		// Check if spawn point is disabled and cooldown is still active
//		if (data.isTemporarilyDisabled && current_time < data.cooldownEndsAt) {
//			found_cooldowns_to_reset = true;
//
//			const gtime_t remaining_time = data.cooldownEndsAt - current_time;
//			// Calculate reduced duration and ensure it meets the minimum
//			const gtime_t reduced_duration = remaining_time * REDUCTION_FACTOR;
//			const gtime_t final_duration = std::max(reduced_duration, HordeConstants::MIN_REDUCED_INDIVIDUAL_COOLDOWN);
//			data.cooldownEndsAt = current_time + final_duration; // Apply clamped duration
//
//			data.attempts = 0;
//		}
//	}
//
//	if (found_cooldowns_to_reset) {
//		SPAWN_POINT_COOLDOWN *= REDUCTION_FACTOR;
//		// Clamp the global cooldown after reduction
//		SPAWN_POINT_COOLDOWN = std::max(SPAWN_POINT_COOLDOWN, HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN);
//
//		if (developer->integer > 1) {
//			gi.Com_PrintFmt("Global spawn cooldown reduced and clamped to {:.2f}s\n", SPAWN_POINT_COOLDOWN.seconds());
//		}
//	}
//}
//
//static constexpr gtime_t GetBaseSpawnCooldown(bool isSmallMap, bool isBigMap) {
//	if (isSmallMap)
//		return 0.5_sec;  // Increased from 0.2 to 0.5
//	else if (isBigMap)
//		return 2.0_sec;  // Increased from 1.4 to 2.0
//	else
//		return 1.2_sec;  // Increased from 0.8 to 1.2
//}
//
//static float CalculateCooldownScale(int32_t lvl, const horde::MapSize& mapSize) {
//	// Early return for low levels - improves branch prediction
//	if (lvl <= 10) {
//		return 1.0f;
//	}
//
//	// Cache player count - only compute once
//	const int32_t numHumanPlayers = GetNumHumanPlayers();
//
//	// Compute base scale - more gradual linear scaling with level
//	float scale = 1.0f + (lvl * 0.015f);  // Changed from 0.02f to 0.015f for more gradual scaling
//
//	// Compute player reduction factor once
//	float playerReduction = 0.0f;
//	if (numHumanPlayers > 1) {
//		constexpr float PLAYER_REDUCTION = 0.1f;
//		constexpr float MAX_REDUCTION = 0.45f;
//		playerReduction = std::min((numHumanPlayers - 1) * PLAYER_REDUCTION, MAX_REDUCTION);
//		scale *= (1.0f - playerReduction);
//	}
//
//	// Determine map multipliers with fewer branches
//	float multiplier, maxScale;
//
//	if (mapSize.isSmallMap) {
//		multiplier = 0.7f;
//		maxScale = 1.3f;
//	}
//	else if (mapSize.isBigMap) {
//		multiplier = 0.85f;
//		maxScale = 1.75f;
//	}
//	else { // Medium map
//		multiplier = 0.8f;
//		maxScale = 1.5f;
//	}
//
//	// Apply map multiplier and clamp to max scale
//	return std::min(scale * multiplier, maxScale);
//}
//
//inline int32_t GetAdjustedMonsterCap(const horde::MapSize& mapSize, int32_t waveLevel) {
//	// Original base caps
//	const int32_t baseSmallCap = HordeConstants::MAX_MONSTERS_SMALL_MAP;  // 14
//	const int32_t baseMediumCap = HordeConstants::MAX_MONSTERS_MEDIUM_MAP; // 16
//	const int32_t baseBigCap = HordeConstants::MAX_MONSTERS_BIG_MAP;      // 32
//
//	int32_t baseCap;
//
//	// Determine base cap based on map size first
//	if (mapSize.isSmallMap) {
//		baseCap = baseSmallCap;
//	}
//	else if (mapSize.isMediumMap) {
//		baseCap = baseMediumCap;
//	}
//	else { // Big map
//		baseCap = baseBigCap;
//	}
//
//	// Apply early wave reduction (only for non-big maps)
//	if (waveLevel <= 10 && !mapSize.isBigMap) {
//		// Scale from 60% at wave 1 up to 100% at wave 10
//		float reductionFactor = 0.6f + ((waveLevel - 1) * 0.0444f); // (1.0 - 0.6) / 9 = 0.0444...
//		reductionFactor = std::clamp(reductionFactor, 0.6f, 1.0f); // Ensure it stays within bounds
//		baseCap = static_cast<int32_t>(baseCap * reductionFactor);
//	}
//
//	// --- Player Count Bonus ---
//	int32_t finalAdjustedCap = baseCap; // Start with the potentially reduced base cap
//	const int32_t numHumanPlayers = GetNumHumanPlayers();
//
//	if (numHumanPlayers > 1) {
//		constexpr int32_t MAX_BONUS_PLAYERS = 3; // Cap bonus contribution at 3 extra players (i.e., players 2, 3, 4)
//		constexpr int32_t BONUS_PER_PLAYER = 2;  // +2 monsters per extra contributing player
//		const int32_t extraPlayers = std::min(numHumanPlayers - 1, MAX_BONUS_PLAYERS);
//		const int32_t playerBonus = extraPlayers * BONUS_PER_PLAYER;
//		finalAdjustedCap += playerBonus;
//
//		if (developer->integer > 1) {
//			gi.Com_PrintFmt("GetAdjustedMonsterCap: Wave={}, BaseCap={}, Humans={}, BonusPlayers={}, Bonus={}, FinalCap={}\n",
//				waveLevel, baseCap, numHumanPlayers, extraPlayers, playerBonus, finalAdjustedCap);
//		}
//	}
//	else {
//		if (developer->integer > 1) {
//			gi.Com_PrintFmt("GetAdjustedMonsterCap: Wave={}, BaseCap={}, Humans={}, No Bonus, FinalCap={}\n",
//				waveLevel, baseCap, numHumanPlayers, finalAdjustedCap);
//		}
//	}
//	// --- End Player Count Bonus ---
//
//	// Ensure the cap doesn't go below a minimum reasonable value (e.g., 6)
//	finalAdjustedCap = std::max(6, finalAdjustedCap);
//
//	// Store globally
//	g_adjusted_monster_cap = finalAdjustedCap;
//
//	return g_adjusted_monster_cap;
//}
//
//// Modify the existing ClampNumToSpawn function
//inline static void ClampNumToSpawn(const horde::MapSize& mapSize) { // mapSize parameter might no longer be needed here
//	// g_adjusted_monster_cap is now calculated in GetAdjustedMonsterCap and includes player bonus
//	const int32_t maxAllowed = g_adjusted_monster_cap;
//
//	// Ensure g_adjusted_monster_cap was initialized (fallback if called too early)
//	if (maxAllowed <= 0) {
//		// Fallback to basic map defaults if the global cap isn't ready
//		const int32_t fallbackCap = mapSize.isSmallMap ? HordeConstants::MAX_MONSTERS_SMALL_MAP :
//			(mapSize.isBigMap ? HordeConstants::MAX_MONSTERS_BIG_MAP : HordeConstants::MAX_MONSTERS_MEDIUM_MAP);
//		g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, fallbackCap);
//		if (developer->integer) {
//			gi.Com_PrintFmt("ClampNumToSpawn: WARN - g_adjusted_monster_cap not ready, used fallback {}\n", fallbackCap);
//		}
//	}
//	else {
//		// Clamp using the globally calculated value
//		g_horde_local.num_to_spawn = std::clamp(g_horde_local.num_to_spawn, 0, maxAllowed);
//		if (developer->integer > 1) {
//			gi.Com_PrintFmt("ClampNumToSpawn: Clamping num_to_spawn to {} (g_adjusted_monster_cap)\n", maxAllowed);
//		}
//	}
//}
//
//static int32_t CalculateQueuedMonsters(const horde::MapSize& mapSize, int32_t lvl, bool isHardMode) noexcept {
//	if (lvl <= 3)
//		return 0;
//
//	// Base más agresiva con mejor cálculo matemático
//	float baseQueued = std::sqrt(static_cast<float>(lvl)) * 3.0f;
//	baseQueued *= (1.0f + (lvl) * 0.18f);
//
//	// Multiplicadores optimizados por tamaño de mapa
//	const float mapSizeMultiplier = mapSize.isSmallMap ? 1.3f :
//		mapSize.isBigMap ? 1.5f : 1.4f;
//
//	const int32_t maxQueued = mapSize.isSmallMap ? 30 :
//		mapSize.isBigMap ? 45 : 35;
//
//	baseQueued *= mapSizeMultiplier;
//
//	// Bonus exponencial mejorado para niveles altos
//	if (lvl > 20) {
//		baseQueued *= std::pow(1.15f, std::min(lvl - 20, 18));
//	}
//
//	// Ajuste de dificultad mejorado
//	if (isHardMode) {
//		float difficultyMultiplier = 1.25f;
//		if (lvl > 25) {
//			difficultyMultiplier += (lvl - 25) * 0.025f;
//			difficultyMultiplier = std::min(difficultyMultiplier, 1.75f);
//		}
//		baseQueued *= difficultyMultiplier;
//	}
//
//	// Factor de reducción final
//	baseQueued *= 0.85f;
//
//	return std::min(static_cast<int32_t>(baseQueued), maxQueued);
//}
//
//// Cache for common calculations in UnifiedAdjustSpawnRate
//struct WaveScalingCache {
//	// Lookup tables for frequent calculations
//	static constexpr int32_t MAX_WAVE_LEVEL = 250;
//	static constexpr int32_t MAX_HUMAN_PLAYERS = 16;
//
//	// Pre-computed cooldown scales to avoid recalculation
//	float cooldownScales[3][MAX_WAVE_LEVEL + 1] = {}; // [mapType][level]
//
//	// Cached base counts
//	int32_t baseCountsByLevel[3][MAX_WAVE_LEVEL + 1] = {}; // [mapType][level]
//	// Removed: int32_t additionalSpawnsByLevel[MAX_WAVE_LEVEL+1] = {}; // Not needed, calculation moved
//
//	// Player multipliers (precomputed)
//	float playerMultipliers[MAX_HUMAN_PLAYERS + 1] = {};
//
//	// Initialize all cache tables
//	void initialize() {
//		using namespace HordeConstants;
//
//		// Initialize player multipliers
//		for (int32_t players = 0; players <= MAX_HUMAN_PLAYERS; ++players) {
//			playerMultipliers[players] = (players <= 1) ?
//				1.0f : BASE_DIFFICULTY_MULTIPLIER + ((players - 1) * PLAYER_COUNT_SCALE);
//		}
//
//		// Initialize base counts by map type and level
//		for (int mapType = 0; mapType < 3; ++mapType) {
//			for (int32_t level = 0; level <= MAX_WAVE_LEVEL; ++level) {
//				// Select the appropriate base count based on level ranges
//				int32_t countIndex;
//				if (level <= 5) countIndex = 0;
//				else if (level <= 10) countIndex = 1;
//				else if (level <= 15) countIndex = 2;
//				else countIndex = 3; // Levels > 15
//
//				// Store pre-computed base count
//				baseCountsByLevel[mapType][level] = BASE_COUNTS[mapType][countIndex];
//			}
//		}
//
//		// Removed initialization for additionalSpawnsByLevel as it's calculated directly
//		// in UnifiedAdjustSpawnRate now, eliminating the unused variables.
//
//		// Initialize cooldown scales (can be accessed by mapType and level)
//		horde::MapSize smallMap = { true, false, false };
//		horde::MapSize mediumMap = { false, false, true }; // Corrected: Medium map should have isMediumMap true
//		horde::MapSize bigMap = { false, true, false };
//
//		for (int32_t level = 0; level <= MAX_WAVE_LEVEL; ++level) {
//			cooldownScales[0][level] = CalculateCooldownScale(level, smallMap);  // Index 0 = Small
//			cooldownScales[1][level] = CalculateCooldownScale(level, mediumMap); // Index 1 = Medium
//			cooldownScales[2][level] = CalculateCooldownScale(level, bigMap);    // Index 2 = Big
//		}
//	}
//} g_waveScalingCache;
//
//// Función para calcular el bono de locura y caos
//static inline int32_t CalculateChaosInsanityBonus(int32_t lvl) noexcept {
//	if (g_chaotic->integer) return (lvl <= 3) ? 6 : 3;
//	switch (g_insane->integer) {
//	case 2: return 16;
//	case 1: return 8;
//	default: return 0;
//	}
//}
//
//void UnifiedAdjustSpawnRate(const horde::MapSize& mapSize, int32_t lvl, int32_t humanPlayers) noexcept {
//	using namespace HordeConstants;
//
//	// Initialize cache if needed (one-time operation)
//	static bool cache_initialized = false;
//	if (!cache_initialized) {
//		g_waveScalingCache.initialize();
//		cache_initialized = true;
//	}
//
//	// Clamp input values for safety
//	const int32_t safeLevel = std::min(lvl, WaveScalingCache::MAX_WAVE_LEVEL);
//	const int32_t safePlayerCount = std::min(humanPlayers, WaveScalingCache::MAX_HUMAN_PLAYERS);
//
//	// Determine map type index for lookups
//	const int mapTypeIndex = mapSize.isSmallMap ? 0 : (mapSize.isBigMap ? 2 : 1);
//
//	// Get base values from cache
//	int32_t baseCount = g_waveScalingCache.baseCountsByLevel[mapTypeIndex][safeLevel];
//
//	// Apply player multiplier using cached value
//	if (safePlayerCount > 1) {
//		const float playerMultiplier = g_waveScalingCache.playerMultipliers[safePlayerCount];
//		baseCount = static_cast<int32_t>(baseCount * playerMultiplier);
//	}
//
//	// --- CORRECTED SECTION ---
//	// Calculate additional spawn count directly, without using the removed cache array
//	int32_t additionalSpawn;
//	if (safeLevel < 8) {
//		additionalSpawn = 6; // Default for early levels
//	}
//	else {
//		// Apply map-specific base value from constants
//		additionalSpawn = mapSize.isSmallMap ? ADDITIONAL_SPAWNS[0] :
//			mapSize.isBigMap ? ADDITIONAL_SPAWNS[2] : ADDITIONAL_SPAWNS[1];
//
//		// Level-based adjustment for high levels
//		if (safeLevel > 25) {
//			additionalSpawn = static_cast<int32_t>(additionalSpawn * 1.6f);
//		}
//	}
//	// --- END CORRECTED SECTION ---
//
//	// Apply difficulty adjustments (Chaos/Insanity bonus)
//	if (safeLevel >= 3 && (g_chaotic->integer || g_insane->integer)) {
//		additionalSpawn += CalculateChaosInsanityBonus(safeLevel);
//	}
//
//	// Get cooldown from cache and apply adjustments
//	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);
//	const float cooldownScale = g_waveScalingCache.cooldownScales[mapTypeIndex][safeLevel];
//	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);
//
//	// Apply difficulty-based cooldown adjustments
//	if (g_chaotic->integer || g_insane->integer) {
//		SPAWN_POINT_COOLDOWN *= HordeConstants::TIME_REDUCTION_MULTIPLIER;
//	}
//	else {
//		// Normal difficulty adjustment
//		SPAWN_POINT_COOLDOWN *= 1.2f;
//	}
//
//	// Apply periodic difficulty scaling (every 3 levels)
//	if (safeLevel > 0 && safeLevel % 3 == 0) { // Added check for safeLevel > 0
//		const float difficultyMultiplier = g_waveScalingCache.playerMultipliers[safePlayerCount];
//		baseCount = static_cast<int32_t>(baseCount * difficultyMultiplier);
//
//		const float cooldownReduction = (mapSize.isBigMap ? 0.1f : 0.15f) * difficultyMultiplier;
//		SPAWN_POINT_COOLDOWN = std::max(
//			SPAWN_POINT_COOLDOWN - gtime_t::from_sec(cooldownReduction),
//			1.0_sec // Ensure cooldown doesn't go below 1.0 second
//		);
//	}
//
//	// Final cooldown clamping
//	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN,
//		HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN, // Use the constant
//		3.5_sec); // Keep upper bound or define a MAX constant
//	// Calculate final spawn count
//	g_horde_local.num_to_spawn = baseCount + additionalSpawn;
//	ClampNumToSpawn(mapSize); // Handle clamping
//
//	// Calculate queued monsters
//	const bool isHardMode = g_insane->integer || g_chaotic->integer;
//	g_horde_local.queued_monsters = CalculateQueuedMonsters(mapSize, safeLevel, isHardMode);
//
//	// Debug output
//	if (developer->integer == 3) {
//		gi.Com_PrintFmt("DEBUG: Wave {} settings:\n", safeLevel);
//		gi.Com_PrintFmt("  - Spawn cooldown: {:.2f}s (Scale {:.2f}x)\n",
//			SPAWN_POINT_COOLDOWN.seconds(), cooldownScale);
//		gi.Com_PrintFmt("  - Base monsters: {}\n", baseCount);
//		gi.Com_PrintFmt("  - Additional spawns: {}\n", additionalSpawn);
//		gi.Com_PrintFmt("  - Queued monsters: {}\n", g_horde_local.queued_monsters);
//		gi.Com_PrintFmt("  - Map type: {}\n",
//			mapSize.isBigMap ? "big" : (mapSize.isSmallMap ? "small" : "medium"));
//	}
//}
//
//// --- End Moved from g_horde.cpp ---
//
//// Reset spawn attempts for all points
//void ResetAllSpawnAttempts() noexcept {
//	spawnPointsData.clear(); // Clears all data including attempts
//	if (developer->integer) {
//		gi.Com_PrintFmt("All spawn point attempts and cooldowns reset.\n");
//	}
//}
//
//// Reset cooldowns for all spawn points and the global cooldown
//void ResetCooldowns() noexcept {
//	// Reset individual spawn point cooldowns
//	for (uint32_t i = 1; i < globals.num_edicts; i++) {
//		edict_t* ent = &g_edicts[i];
//		if (ent && ent->inuse && ent->classname &&
//			!strcmp(ent->classname, "info_player_deathmatch")) {
//			auto& data = spawnPointsData[ent];
//			data.isTemporarilyDisabled = false;
//			data.cooldownEndsAt = 0_sec;
//			data.alternative_cooldown = 0_sec; // Also reset alternative cooldown
//			data.alternative_attempts = 0;
//			data.needs_long_alternative_cooldown = false;
//		}
//	}
//
//	// Reset global spawn cooldown based on map size and level
//	const horde::MapSize& mapSize = g_horde_local.current_map_size;
//	const int32_t lvl = g_horde_local.level;
//	SPAWN_POINT_COOLDOWN = GetBaseSpawnCooldown(mapSize.isSmallMap, mapSize.isBigMap);
//	const float cooldownScale = CalculateCooldownScale(lvl, mapSize);
//	SPAWN_POINT_COOLDOWN = gtime_t::from_sec(SPAWN_POINT_COOLDOWN.seconds() * cooldownScale);
//	SPAWN_POINT_COOLDOWN = std::clamp(SPAWN_POINT_COOLDOWN,
//		HordeConstants::MIN_GLOBAL_SPAWN_COOLDOWN,
//		3.5_sec); // Keep upper bound
//
//	if (developer->integer > 1) {
//		gi.Com_PrintFmt("Global and individual spawn cooldowns reset. Global: {:.2f}s\n", SPAWN_POINT_COOLDOWN.seconds());
//	}
//}
//
//
//// Calculate the maximum time allowed for a wave
//static constexpr gtime_t calculate_max_wave_time(int32_t wave_level) {
//	using namespace HordeConstants;
//	// Corrected: Multiply int with seconds value, then add gtime_t objects
//	gtime_t max_time = BASE_MAX_WAVE_TIME + gtime_t::from_sec(wave_level * TIME_INCREASE_PER_LEVEL.seconds());
//	if (wave_level >= 10 && wave_level % 5 == 0) { // Boss wave bonus
//		max_time += BOSS_TIME_BONUS;
//	}
//	return max_time;
//}
//
//// Get parameters for wave completion conditions
//static ConditionParams GetConditionParams(const horde::MapSize& mapSize, int32_t numHumanPlayers, int32_t lvl) {
//	ConditionParams params;
//
//	// Calculate max monsters for the wave (uses the globally adjusted cap)
//	params.maxMonsters = GetAdjustedMonsterCap(mapSize, lvl);
//
//	// Calculate base time threshold
//	params.timeThreshold = calculate_max_wave_time(lvl);
//
//	// Adjust time threshold based on player count
//	if (numHumanPlayers > 1) {
//		// Ensure multiplication happens on float/double before creating gtime_t
//		params.timeThreshold = gtime_t::from_sec(params.timeThreshold.seconds() * (1.0f + (numHumanPlayers - 1) * 0.1f));
//	}
//
//	// Set low percentage time threshold (e.g., 60% of base time)
//	params.lowPercentageTimeThreshold = gtime_t::from_sec(params.timeThreshold.seconds() * 0.6f);
//
//	// Set independent time threshold (e.g., 80% of base time, minimum 90s)
//	// Corrected: Perform multiplication on seconds, then compare gtime_t objects
//	params.independentTimeThreshold = std::max(gtime_t::from_sec(params.timeThreshold.seconds() * 0.8f), 90_sec);
//
//	// Adjust thresholds for difficulty
//	if (g_chaotic->integer || g_insane->integer) {
//		// Corrected: Perform multiplication on seconds before creating new gtime_t
//		params.timeThreshold = gtime_t::from_sec(params.timeThreshold.seconds() * 0.85f);
//		params.lowPercentageTimeThreshold = gtime_t::from_sec(params.lowPercentageTimeThreshold.seconds() * 0.9f);
//		params.independentTimeThreshold = gtime_t::from_sec(params.independentTimeThreshold.seconds() * 0.9f);
//	}
//
//	// Set percentage thresholds (can be constants or adjusted)
//	params.lowPercentageThreshold = 0.3f; // 30% remaining monsters
//	params.aggressiveTimeReductionThreshold = 0.15f; // 15% remaining monsters
//
//	// Clamp minimum times
//	params.timeThreshold = std::max(params.timeThreshold, 60_sec); // Corrected: Use 60_sec
//	params.lowPercentageTimeThreshold = std::max(params.lowPercentageTimeThreshold, 45_sec); // Corrected: Use 45_sec
//	params.independentTimeThreshold = std::max(params.independentTimeThreshold, 75_sec); // Corrected: Use 75_sec
//
//
//	if (developer->integer > 1) {
//		gi.Com_PrintFmt("GetConditionParams (Wave {}): MaxM={}, TimeT={:.1f}s, LowPercT={:.1f}s, IndepT={:.1f}s\n",
//			lvl, params.maxMonsters, params.timeThreshold.seconds(),
//			params.lowPercentageTimeThreshold.seconds(), params.independentTimeThreshold.seconds());
//	}
//
//	return params;
//}
//
//
//// Reset champion monster state for the wave
//void ResetChampionMonsterState() {
//	champion_spawned_this_wave = false;
//	champion_spawn_cooldown = 0; // Reset cooldown at the start of a new wave/level
//}
//
//// Wave type memory and selection logic
//static constexpr size_t WAVE_MEMORY_SIZE = 3;
//static std::array<MonsterWaveType, WAVE_MEMORY_SIZE> previous_wave_types = {};
//static size_t wave_memory_index = 0;
//
//static bool WasRecentlyUsed(MonsterWaveType wave_type) noexcept {
//	for (const auto& prev_type : previous_wave_types) {
//		if (prev_type == wave_type) {
//			return true;
//		}
//	}
//	return false;
//}
//
//static void StoreWaveType(MonsterWaveType wave_type) noexcept {
//	previous_wave_types[wave_memory_index] = wave_type;
//	wave_memory_index = (wave_memory_index + 1) % WAVE_MEMORY_SIZE;
//}
//
//static bool TrySetWaveType(MonsterWaveType new_type) {
//	if (!WasRecentlyUsed(new_type)) {
//		current_wave_type = new_type;
//		StoreWaveType(new_type);
//		return true;
//	}
//	return false;
//}
//
//static bool IsSpecialWaveType(MonsterWaveType type) noexcept {
//	// Define which types are considered "special"
//	return HasWaveType(type, MonsterWaveType::Boss) ||
//		HasWaveType(type, MonsterWaveType::SemiBoss) ||
//		HasWaveType(type, MonsterWaveType::Elite) ||
//		HasWaveType(type, MonsterWaveType::Special); // Add other types if needed
//}
//
//static bool WasLastWaveSpecial() noexcept {
//	// Check the most recently stored wave type
//	const size_t last_index = (wave_memory_index == 0) ? (WAVE_MEMORY_SIZE - 1) : (wave_memory_index - 1);
//	return IsSpecialWaveType(previous_wave_types[last_index]);
//}
//
//struct WaveOptionalComponent {
//	MonsterWaveType type;
//	float probability; // 0.0 to 1.0
//};
//
//struct WaveDefinition {
//	MonsterWaveType baseType;
//	std::array<WaveOptionalComponent, 4> optionalComponents; // Fixed size for potential additions
//};
//
//// Define wave compositions
//const std::array<WaveDefinition, 8> wave_definitions = { {
//	// Wave 0 (Example: Early game - Ground focus)
//	{ MonsterWaveType::Ground | MonsterWaveType::Light, { { {MonsterWaveType::Flying, 0.1f}, {MonsterWaveType::Ranged, 0.2f} } } },
//	// Wave 1 (Example: Introduce flyers more)
//	{ MonsterWaveType::Ground | MonsterWaveType::Flying | MonsterWaveType::Light, { { {MonsterWaveType::Medium, 0.1f}, {MonsterWaveType::Fast, 0.15f} } } },
//	// Wave 2 (Example: Mix ground and air, add medium chance)
//	{ MonsterWaveType::Ground | MonsterWaveType::Flying | MonsterWaveType::Medium, { { {MonsterWaveType::Heavy, 0.05f}, {MonsterWaveType::Special, 0.1f} } } },
//	// Wave 3 (Example: Heavier ground focus)
//	{ MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Medium, { { {MonsterWaveType::Ranged, 0.3f}, {MonsterWaveType::Fast, 0.1f} } } },
//	// Wave 4 (Example: Boss wave placeholder - handled separately)
//	{ MonsterWaveType::Boss, {} }, // Optional components empty for boss waves
//	// Wave 5 (Example: Fast and flying focus)
//	{ MonsterWaveType::Flying | MonsterWaveType::Fast | MonsterWaveType::Light, { { {MonsterWaveType::Special, 0.2f}, {MonsterWaveType::Elite, 0.05f} } } },
//	// Wave 6 (Example: Heavy and special units)
//	{ MonsterWaveType::Ground | MonsterWaveType::Heavy | MonsterWaveType::Special, { { {MonsterWaveType::Flying, 0.15f}, {MonsterWaveType::Elite, 0.1f} } } },
//	// Wave 7 (Example: Mixed bag, higher elite chance)
//	{ MonsterWaveType::Ground | MonsterWaveType::Flying | MonsterWaveType::Medium | MonsterWaveType::Fast, { { {MonsterWaveType::Heavy, 0.15f}, {MonsterWaveType::Elite, 0.15f} } } }
//} };
//
//inline MonsterWaveType GetWaveComposition(int waveNumber, bool forceSpecialWave = false) {
//	// Use modulo to cycle through definitions, ensuring it wraps around correctly
//	const size_t definition_index = (waveNumber - 1) % wave_definitions.size(); // -1 because waves are 1-based
//
//	// Handle boss waves explicitly (every 5 waves starting from 10)
//	if (waveNumber >= 10 && waveNumber % 5 == 0) {
//		return MonsterWaveType::Boss; // Explicitly return Boss type
//	}
//
//	// Get the base definition
//	MonsterWaveType final_composition = wave_definitions[definition_index].baseType;
//
//	// Add optional components based on probability
//	for (const auto& component : wave_definitions[definition_index].optionalComponents) {
//		if (component.type != MonsterWaveType::None && frandom() < component.probability) {
//			final_composition |= component.type;
//		}
//	}
//
//	// Force a special wave type if requested and not already special/boss
//	if (forceSpecialWave && !IsSpecialWaveType(final_composition) && !HasWaveType(final_composition, MonsterWaveType::Boss)) {
//		// Add a random special type (e.g., Elite or Special)
//		if (frandom() < 0.5f) {
//			final_composition |= MonsterWaveType::Elite;
//		}
//		else {
//			final_composition |= MonsterWaveType::Special;
//		}
//	}
//
//
//	return final_composition;
//}
//
//
//void InitializeWaveType(int32_t waveLevel) {
//	// Determine if we should force a special wave (e.g., every 7 waves, but not if last was special)
//	const bool forceSpecial = (waveLevel > 5 && waveLevel % 7 == 0 && !WasLastWaveSpecial());
//
//	MonsterWaveType potential_type = GetWaveComposition(waveLevel, forceSpecial);
//
//	// Attempt to set the wave type, avoiding recent repeats
//	if (!TrySetWaveType(potential_type)) {
//		// Fallback: If the chosen type was recently used, try the *next* definition
//		// or simply default to a basic ground/light mix if all else fails.
//		const size_t next_definition_index = waveLevel % wave_definitions.size(); // Simple wrap around
//		potential_type = wave_definitions[next_definition_index].baseType;
//
//		if (!TrySetWaveType(potential_type)) {
//			// Ultimate fallback if even the next definition was recent
//			current_wave_type = MonsterWaveType::Ground | MonsterWaveType::Light;
//			StoreWaveType(current_wave_type); // Store the fallback
//			if (developer->integer) {
//				gi.Com_PrintFmt("Wave Type Fallback: Used basic Ground/Light for wave %d\n", waveLevel);
//			}
//		}
//		else {
//			if (developer->integer) {
//				gi.Com_PrintFmt("Wave Type Fallback: Used next definition base type for wave %d\n", waveLevel);
//			}
//		}
//	}
//
//	if (developer->integer) {
//		// Optional: Convert wave type flags to string for better debug output
//		// std::string waveTypeStr = WaveTypeToString(current_wave_type);
//		gi.Com_PrintFmt("Initialized Wave %d Type: %u\n", waveLevel, static_cast<uint32_t>(current_wave_type));
//	}
//}
//
//// --- End Moved from g_horde.cpp ---
