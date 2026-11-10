
#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include "stm32f4xx_hal.h"
#include "cmsis_os2.h"

HAL_StatusTypeDef State_Manager_Init(void);
void State_Manager_Task(void *argument);

#endif /* IO_MANAGER_H */
