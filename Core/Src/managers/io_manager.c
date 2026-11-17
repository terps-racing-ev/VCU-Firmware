#include "managers/io_manager.h"

#include "cmsis_os.h"
#include "drivers/pwm.h"
#include "main.h"

extern TIM_HandleTypeDef htim3;

static PWM_Output drs_pwm;

HAL_StatusTypeDef IO_Manager_Init(void)
{
    HAL_GPIO_WritePin(
        DRS_EN_GPIO_Port,
        DRS_EN_Pin,
        GPIO_PIN_RESET);

    if (PWM_Init(&drs_pwm, &htim3, TIM_CHANNEL_1) != HAL_OK) {
        return HAL_ERROR;
    }

    if (PWM_SetDutyPermille(&drs_pwm, 0U) != HAL_OK) {
        return HAL_ERROR;
    }

    if (PWM_Start(&drs_pwm) != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

void IO_Manager_Task(void *argument)
{
    (void)argument;

    for (;;) {
        osDelay(10U);
    }
}

HAL_StatusTypeDef IO_SetDRSPWM(
    bool enabled,
    uint16_t duty_permille)
{
    HAL_StatusTypeDef status;

    if (duty_permille > PWM_DUTY_PERMILLE_MAX) {
        return HAL_ERROR;
    }

    if (!enabled) {
        HAL_GPIO_WritePin(
            DRS_EN_GPIO_Port,
            DRS_EN_Pin,
            GPIO_PIN_RESET);

        return PWM_SetDutyPermille(&drs_pwm, 0U);
    }

    status = PWM_SetDutyPermille(&drs_pwm, duty_permille);
    if (status != HAL_OK) {
        return status;
    }

    HAL_GPIO_WritePin(
        DRS_EN_GPIO_Port,
        DRS_EN_Pin,
        GPIO_PIN_SET);

    return HAL_OK;
}
