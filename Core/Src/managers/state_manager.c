#include "state_manager.h"
#include "can.h"

HAL_StatusTypeDef State_Manager_Init(void) {
    return HAL_OK;
}

void State_Manager_Task(void *argument) {
    CAN_Message_t msg = {
        .data = {},
        .length = 0,
        .id = 0xD
    };
    
    for (;;) {
        CAN_SendCTRLMsg(&msg);

        osDelay(1U);
    }
}