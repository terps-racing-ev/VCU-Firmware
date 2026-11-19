#include "data/state.h"

/* Private: nobody else can touch these except through the accessors. */
static osMutexId_t     state_mutex = NULL;
static VCU_State_t     vcu_state   = VCU_STATE_LAMP_CHECK;
static VCU_ErrorMask_t vcu_errors  = 0U;

/* Called once from State_Manager_Init(), before the scheduler starts.
 * Safe to call again (re-init): the mutex is only created the first time,
 * so we never leak a second one. */
HAL_StatusTypeDef State_InitState(void)
{
    if (state_mutex == NULL) {
        const osMutexAttr_t attr = { .name = "State_Mutex" };
        state_mutex = osMutexNew(&attr);
        if (state_mutex == NULL) {
            return HAL_ERROR;
        }
    }

    vcu_state  = VCU_STATE_LAMP_CHECK;   /* always boot into LAMP_CHECK */
    vcu_errors = 0U;
    return HAL_OK;
}

/* Lock, copy, unlock. */
void State_GetState(VCU_State_t *state)
{
    if (state == NULL) { return; }
    if (state_mutex != NULL) { (void)osMutexAcquire(state_mutex, osWaitForever); }
    *state = vcu_state;
    if (state_mutex != NULL) { (void)osMutexRelease(state_mutex); }
}

void State_SetState(VCU_State_t new_state)
{
    if (state_mutex != NULL) { (void)osMutexAcquire(state_mutex, osWaitForever); }
    vcu_state = new_state;
    if (state_mutex != NULL) { (void)osMutexRelease(state_mutex); }
}

void State_GetErrorMask(VCU_ErrorMask_t *mask)
{
    if (mask == NULL) { return; }
    if (state_mutex != NULL) { (void)osMutexAcquire(state_mutex, osWaitForever); }
    *mask = vcu_errors;
    if (state_mutex != NULL) { (void)osMutexRelease(state_mutex); }
}

void State_SetErrorMask(VCU_ErrorMask_t mask)
{
    if (state_mutex != NULL) { (void)osMutexAcquire(state_mutex, osWaitForever); }
    vcu_errors = mask;
    if (state_mutex != NULL) { (void)osMutexRelease(state_mutex); }
}
