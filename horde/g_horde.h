// Horde mode game initialization and management functions

extern cvar_t* g_horde;
extern void Horde_PreInit()  ;
extern void Horde_Init()  ;
extern void Horde_RunFrame()  ;
extern void ResetGame()  ;
extern void HandleResetEvent()  ;
extern uint64_t last_wave_number;  // Tracks the last completed wave number, used for intermission

// Item selection in Horde mode
gitem_t* G_HordePickItem(std::mt19937& rng)  ;
extern const char* G_HordePickMonster(edict_t* spawn_point, std::mt19937& rng)  ;

// Game mode checks
extern bool G_IsDeathmatch() noexcept;
extern bool G_IsCooperative() noexcept;



// HORDE CS
extern gtime_t horde_message_end_time;  // Add this line
extern void ClearHordeMessage();
extern void UpdateHordeMessage(std::string_view message, gtime_t duration);
extern void UpdateHordeHUD();
extern uint32_t g_horde_local.total_monsters_in_wave;

extern void CleanupInvalidEntities();

//extern HordeState g_horde_local;
extern gtime_t g_independent_timer_start;
extern bool g_allowWaveAdvance;

// Hook functionality for player interactions
void Hook_InitGame(void);
void Hook_PlayerDie(edict_t* attacker, edict_t* self);
void Hook_Think(edict_t* self);
edict_t* Hook_Start(edict_t* ent);
void Hook_Reset(edict_t* rhook);
bool Hook_Check(edict_t* self);
void Hook_Service(edict_t* self);
void Hook_Track(edict_t* self);
void Hook_Touch(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self);
void Hook_Fire(edict_t* owner, vec3_t start, vec3_t forward);
void Weapon_Hook_Fire(edict_t* ent);
void Weapon_Hook(edict_t* ent);