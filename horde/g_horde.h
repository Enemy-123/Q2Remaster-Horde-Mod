
extern cvar_t* g_horde;

void Horde_PreInit();
void Horde_Init();
void Horde_RunFrame();
gitem_t* G_HordePickItem();
bool G_IsDeathmatch();
bool G_IsCooperative();

void ResetGame();
void HandleResetEvent();
extern int current_wave_number;


