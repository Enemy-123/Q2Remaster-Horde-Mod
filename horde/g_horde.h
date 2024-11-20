// Horde mode game initialization and management functions
extern cvar_t* g_horde;
void Horde_PreInit()  ;
void Horde_Init()  ;
void Horde_RunFrame()  ;
void ResetGame()  ;
void HandleResetEvent()  ;

// Item selection in Horde mode
gitem_t* G_HordePickItem();

// Game mode checks
bool G_IsDeathmatch() noexcept;
bool G_IsCooperative() noexcept;

// Hook functionality for player interactions
void Hook_InitGame(void);
void Hook_PlayerDie(edict_t* attacker, edict_t* self);
void Hook_Think(edict_t* self);
void Hook_Reset(edict_t* rhook);
bool Hook_Check(edict_t* self);
void Hook_Service(edict_t* self);
void Hook_Track(edict_t* self);
void Hook_Touch(edict_t* self, edict_t* other, const trace_t& tr, bool other_touching_self);
void Hook_Fire(edict_t* owner, vec3_t start, vec3_t forward);
void Weapon_Hook_Fire(edict_t* ent);
void Weapon_Hook(edict_t* ent);

// HORDE CS
extern void ClearHordeMessage();
extern void UpdateHordeMessage(std::string_view message, gtime_t duration);
extern void UpdateHordeHUD();

extern void CleanupInvalidEntities();

extern uint16_t g_totalMonstersInWave;
extern inline int8_t CalculateRemainingMonsters();


// Forzar limpieza de cuerpos
extern void Horde_CleanBodies();
extern void ResetWaveAdvanceState() noexcept;