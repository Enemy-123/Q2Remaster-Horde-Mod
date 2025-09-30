//shared.h 
#pragma once

#include "g_local.h"
#include <string>
#include <unordered_map> // Make sure this is included
#include "horde/horde_ids.h"

extern std::vector<edict_t*> g_targetable_special_entities;


//TRAP

// State for a single trap target
struct trap_target_state_t {
    int      entity_num; // Use entity number for safety
    float    distance;
};

constexpr int TRAP_MAX_TARGETS = 3;            // Maximum number of targets

// The main state for a trap entity. Only Plain Old Data.
struct trap_state_t {
    trap_target_state_t targets[TRAP_MAX_TARGETS];
    int                 num_targets;
    bool                in_cooldown;
    gtime_t             cooldown_end;
    std::vector<edict_t*> owned_gibs;  // Track gibs owned by this trap for fast cleanup

    // Helper to reset the state to its default values
    void clear() {
        for (auto& target : targets) {
            target.entity_num = 0;
            target.distance = 0.0f;
        }
        num_targets = 0;
        in_cooldown = false;
        cooldown_end = 0_sec;
        owned_gibs.clear();
    }
};


// --- CHANGE #3: Add helper functions to safely manage the state map.
trap_state_t* GetTrapState(const edict_t* ent);
trap_state_t* CreateTrapState(edict_t* ent);
void RemoveTrapState(const edict_t* ent);

extern std::unordered_map<int, trap_state_t> g_trap_states;

// LASERS
struct EmitterState
{
    bool is_warning_phase = false;
    bool is_blink_on = false;
    gtime_t last_blink_time = 0_ms;

    // Track previous visual state to avoid redundant updates
    uint32_t last_beam_skinnum = 0;
    uint32_t last_flare_skinnum = 0;
    int last_beam_frame = -1;
    int last_emitter_renderfx = -1;

    // Add a clear() method for convenience
    void clear() {
        is_warning_phase = false;
        is_blink_on = false;
        last_blink_time = 0_ms;
        last_beam_skinnum = 0;
        last_flare_skinnum = 0;
        last_beam_frame = -1;
        last_emitter_renderfx = -1;
    }
};

// --- CHANGE #1 & #2: Same changes as above for safety and correctness.
extern std::unordered_map<int, EmitterState> g_emitter_states;

// --- CHANGE #3: Add helper functions for laser state management.
EmitterState* GetEmitterState(const edict_t* ent);
EmitterState* CreateEmitterState(edict_t* ent);
void RemoveEmitterState(const edict_t* ent);

constexpr int ADRENALINE_HEALTH_BONUS = 5;
constexpr float VECTOR_LENGTH_SQ_EPSILON = 0.0001f * 0.0001f;

// Adrenaline bonus calculation functions
int CalculateSentryHealth(int base_health, gclient_t* owner_client);
gtime_t CalculateDeployableLifetime(gtime_t base_lifetime, gclient_t* owner_client);

[[nodiscard]] constexpr bool IsFirstThreeWaves(int32_t wave_level) noexcept {
    return wave_level <= 3;
}

// boss stuff

enum class BossTeleportReason {
    DROWNING,
    TRIGGER_HURT,
//    STUCK
};
void InitializeMonsterMoveSets();
bool M_AdjustBlindfireTarget(edict_t* self, const vec3_t& start, const vec3_t& target, const vec3_t& right, vec3_t& out_dir);
bool CheckAndTeleportBoss(edict_t* boss, const BossTeleportReason reason);
void ImprovedSpawnGrow(const vec3_t& position, float start_size, float end_size, edict_t* spawned_entity);
void ClearSpawnArea(const vec3_t& origin, const vec3_t& mins, const vec3_t& maxs);
extern void SetHealthBarName(const edict_t* boss);

// Mantén el mapa de nombres como una variable externa
extern const std::unordered_map<horde::MonsterTypeID, std::string_view> monster_name_replacements;
//DMG & POWERUP
void ApplyMonsterBonusFlags(edict_t* monster);
void ApplyBossEffects(edict_t* boss);
//extern [[nodiscard]] constexpr float M_DamageModifier(edict_t* monster) noexcept;
void UpdatePowerUpTimes(edict_t* monster) noexcept;

// healthbar
extern void SP_target_healthbar(edict_t* self);
extern void use_target_healthbar(edict_t* self, edict_t* other, edict_t* activator);
extern void check_target_healthbar(edict_t* self);
extern void HandleSpawnPhaseAggression(edict_t* monster);
extern void Monster_MoveSpawn(edict_t* self); 
extern void ConfigureBossArmor(edict_t* self);
extern void PushEntitiesAway(const vec3_t& center, int num_waves, float push_radius, float push_strength, float horizontal_push_strength, float vertical_push_strength);
extern horde::MapSize GetMapSize(const char* mapname) noexcept;
extern gtime_t GetWaveTimer();
extern int32_t GetStroggsNum() noexcept;
extern inline bool IsBossWave() noexcept;

bool TeleportSelf(edict_t* ent);
//extern int8_t GetNumHumanPlayers();

// Estructura para almacenar las estad√≠sticas de los jugadores
struct PlayerStats {
    edict_t* player = nullptr;
    int32_t total_damage = 0;
};
void ApplyGradualHealing(edict_t* ent);

void AllowReset() noexcept;

bool CheckAndTeleportStuckMonster(edict_t* self);
bool G_IsClearPath(const edict_t* ignore, contents_t mask, const vec3_t& spot1, const vec3_t& spot2);

extern gtime_t horde_message_end_time;
extern void CheckAndUpdateMenus();
extern void CheckAndResetDisabledSpawnPoints();
extern void CheckAndRestoreMonsterAlpha(edict_t* const ent);
extern void G_UpdateAdrenalineBasedDeployables(int current_wave_level);
// Estructura para pasar datos adicionales a la función de filtro
struct FilterData {
    const edict_t* ignore_ent;
    int count;
};
//weapon
void SpawnClusterGrenades(edict_t* owner, const vec3_t& origin, int base_damage);

extern bool CTFCheckTimeExtensionVote();
extern void ClearHordeMessage();
extern void StartFadeOut(edict_t* ent);
extern bool IsMonsterJumping(const edict_t* self);

extern bool Horde_TeleportMonster(edict_t* self, const vec3_t& destination_origin, const vec3_t& destination_angles, bool play_effects, bool force_despite_visibility);

const char* GetPlayerName(const edict_t* player);
extern const char* GetDisplayName(const edict_t* ent);
extern const char* GetTitleFromFlags(bonus_flags_t bonus_flags) noexcept;
extern bool IsPlayerDefense(const edict_t* ent);
extern void VerifyAndAdjustBots();
extern bool GetPredictedScaledBounds(horde::MonsterTypeID typeId, vec3_t& out_mins, vec3_t& out_maxs);

extern void remove_sentries(edict_t* ent) noexcept;


// Forward declarations for strogg summoner functions
extern void strogg_summoner_die(edict_t* self, edict_t* inflictor, edict_t* attacker, int damage, const vec3_t& point, const mod_t& mod);
extern void strogg_base_think(edict_t* self);
// Forward declaration for strogg summoner touch function
extern void strogg_summoned_touch(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self);
extern void InheritSummonedProperties(edict_t* child, edict_t* parent, bool full_setup);
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
