#ifndef IO_MANAGER_H
#define IO_MANAGER_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

HAL_StatusTypeDef IO_Manager_Init(void);
void IO_Manager_Task(void *argument);

/**
 * @brief Enable or disable DRS PWM.
 *
 * @param enabled        true to assert DRS_EN, false to disable DRS
 * @param duty_permille  Duty cycle from 0 through 1000
 *
 * Examples:
 * @code
 * IO_SetDRSPWM(true, 500U);   // Enable DRS at 50.0%
 * IO_SetDRSPWM(false, 0U);    // Disable DRS safely
 * @endcode
 */
HAL_StatusTypeDef IO_SetDRSPWM(
    bool enabled,
    uint16_t duty_permille);

#endif /* IO_MANAGER_H */