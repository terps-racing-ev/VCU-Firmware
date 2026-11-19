#ifndef DATA_STATE_H
#define DATA_STATE_H

#include <stdint.h>
#include "cmsis_os.h"
#include "main.h"                 /* HAL_StatusTypeDef */

/* ------------------------------------------------------------------------
 * (a) The states.
 *
 * LOADING is gone: config/EEPROM is already loaded in main.c before the
 * scheduler starts, so there is nothing left to wait for. The car now boots
 * straight into LAMP_CHECK.
 *
 * Numbers are explicit because this byte will go out on CAN and the DBC has
 * to match. Never reorder; only add at the end.
 *
 * 0 is deliberately UNUSED (it was LOADING). A zeroed / corrupted state
 * variable therefore matches no case and falls into the "unknown -> HARD_FAULT"
 * default in the transition function instead of looking like a valid state.
 * ---------------------------------------------------------------------- */
typedef enum {
    VCU_STATE_LAMP_CHECK        = 1,   /* [?] test plan PDF numbers this "7" */
    VCU_STATE_NOT_READY         = 2,
    VCU_STATE_PLAYING_RTD_SOUND = 3,
    VCU_STATE_DRIVING           = 4,
    VCU_STATE_BAP_FAULT         = 5,   /* Brake/Accel Plausibility */
    VCU_STATE_HARD_FAULT        = 6
} VCU_State_t;

/* ------------------------------------------------------------------------
 * (b) Error bits (HVC style). TREV4 had two local bools (hard_fault,
 * bap_fault). Here every reason gets its own bit so the VCU can report WHY.
 * ---------------------------------------------------------------------- */
typedef uint32_t VCU_ErrorMask_t;

typedef enum {
    VCU_ERR_APPS_INVALID = 0,
    VCU_ERR_BSE_INVALID  = 1,
    VCU_ERR_SDC_OPEN     = 2,
    VCU_ERR_RED_CAR      = 3,
    VCU_ERR_HVC_TIMEOUT  = 4,
    VCU_ERR_BAP          = 5          /* the only "soft" fault */
} VCU_ErrorBit_t;

#define VCU_ERR_BIT(bit)      ((VCU_ErrorMask_t)1U << (uint32_t)(bit))
#define SET_ERROR(mask, bit)  ((mask) |= VCU_ERR_BIT(bit))

#define VCU_BAP_MASK          VCU_ERR_BIT(VCU_ERR_BAP)
/* hard_fault == any error bit EXCEPT BAP. */
#define VCU_HARD_FAULT_MASK   (~VCU_BAP_MASK)

/* ------------------------------------------------------------------------
 * (c) Accessors. Other managers (torque, IO, CAN) call ONLY these.
 *     Writer: the state manager. Readers: anyone. Never call from an ISR.
 * ---------------------------------------------------------------------- */
HAL_StatusTypeDef State_InitState(void);
void State_GetState(VCU_State_t *state);
void State_SetState(VCU_State_t new_state);
void State_GetErrorMask(VCU_ErrorMask_t *mask);
void State_SetErrorMask(VCU_ErrorMask_t mask);

#endif /* DATA_STATE_H */
