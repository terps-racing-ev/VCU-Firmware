#include <stdint.h>
#include "stm32f4xx_hal.h"

typedef enum {
    VCU_STATE_LAMP_CHECK = 0,
    VCU_STATE_NOT_READY = 1,
    VCU_STATE_RTD_SFX_PLAYING = 2,
    VCU_STATE_DRIVING = 3,
    VCU_STATE_BAP_FAULT = 4,
    VCU_STATE_HARD_FAULT = 5
} VCU_state;

//TODO: error bit mask, function types

typedef uint32_t StateErrMask;

typedef enum {
    VCU_ERR_APPS = 0,
    VCU_ERR_BSE = 1,
    VCU_ERR_OPEN_SDC = 2,
    VCU_ERR_RED_CAR = 3,
    VCU_ERR_HVC_FAULT = 4,
    VCU_ERR_BAP = 5, 
} ErrorBits;

extern struct {
    VCU_state state;
    ErrorBits error_mask;
} VCU_Info;

#define SET_ERROR(mask, bit) (mask) |= ((ErrorMask)1U << (uint32_t)(bit))    

HAL_StatusTypeDef State_InitState(void);
void State_GetState(VCU_state *state);
void State_SetState(VCU_state new_state);
void State_SetErrorMask(ErrorBits mask);
void State_GetErrorMask(ErrorBits *mask);