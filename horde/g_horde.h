
extern cvar_t* g_horde;

void Horde_PreInit() noexcept;
void Horde_Init() noexcept;
void Horde_RunFrame() noexcept;
gitem_t* G_HordePickItem() noexcept;
bool G_IsDeathmatch() noexcept;
bool G_IsCooperative() noexcept;

void ResetGame() noexcept;
void HandleResetEvent() noexcept;
extern int current_wave_number;
extern int last_wave_number;
extern int remainingMonsters;


//hook.cpp
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

