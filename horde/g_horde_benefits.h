#pragma once
#include "../g_local.h"

// Forward declarations
struct benefit_t {
    const char* name;
    const char* center_msg;
    const char* chat_msg;
    const char* cvar_name;
    const char* cvar_value;
    int32_t min_level;
    int32_t max_level;
    float weight;
};

// Constantes globales
constexpr size_t MAX_BENEFITS = 10;  // Número de beneficios en el array
constexpr size_t MAX_RECENT_BENEFITS = 3;

// Declaraciones externas
extern const benefit_t BENEFITS[MAX_BENEFITS];
extern uint32_t obtained_benefits_mask;
extern uint8_t recent_benefits[MAX_RECENT_BENEFITS];
extern size_t recent_index;
extern int32_t vampire_level;

// Funciones públicas
inline bool has_benefit(size_t index);
void mark_benefit_obtained(size_t index);
void ResetBenefits() noexcept;
void CheckAndApplyBenefit(int32_t wave);
std::string GetActiveBonusesString();