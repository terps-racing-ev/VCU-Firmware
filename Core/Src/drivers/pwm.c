#include "drivers/pwm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool PWM_IsValidChannel(uint32_t channel)
{
    return (channel == TIM_CHANNEL_1) ||
           (channel == TIM_CHANNEL_2) ||
           (channel == TIM_CHANNEL_3) ||
           (channel == TIM_CHANNEL_4);
}

static bool PWM_IsValidOutput(const PWM_Output *output)
{
    return (output != NULL) &&
           (output->timer != NULL) &&
           (output->timer->Instance != NULL) &&
           PWM_IsValidChannel(output->channel);
}

HAL_StatusTypeDef PWM_Init(
    PWM_Output *output,
    TIM_HandleTypeDef *timer,
    uint32_t channel)
{
    if ((output == NULL) ||
        (timer == NULL) ||
        (timer->Instance == NULL) ||
        !PWM_IsValidChannel(channel)) {
        return HAL_ERROR;
    }

    output->timer = timer;
    output->channel = channel;
    output->duty_permille = 0U;

    __HAL_TIM_SET_COMPARE(timer, channel, 0U);

    return HAL_OK;
}

HAL_StatusTypeDef PWM_Start(PWM_Output *output)
{
    if (!PWM_IsValidOutput(output)) {
        return HAL_ERROR;
    }

    return HAL_TIM_PWM_Start(output->timer, output->channel);
}

HAL_StatusTypeDef PWM_Stop(PWM_Output *output)
{
    HAL_StatusTypeDef status;

    if (!PWM_IsValidOutput(output)) {
        return HAL_ERROR;
    }

    __HAL_TIM_SET_COMPARE(output->timer, output->channel, 0U);
    output->duty_permille = 0U;

    status = HAL_TIM_PWM_Stop(output->timer, output->channel);
    return status;
}

uint32_t PWM_DutyToCompare(
    const PWM_Output *output,
    uint16_t duty_permille)
{
    uint32_t auto_reload;
    uint64_t period_counts;
    uint64_t compare;

    if (!PWM_IsValidOutput(output) ||
        (duty_permille > PWM_DUTY_PERMILLE_MAX)) {
        return 0U;
    }

    auto_reload = __HAL_TIM_GET_AUTORELOAD(output->timer);
    period_counts = (uint64_t)auto_reload + 1ULL;
    compare = (period_counts * duty_permille) / PWM_DUTY_PERMILLE_MAX;

    /* ARR + 1 cannot be represented when a 32-bit timer uses UINT32_MAX. */
    if (compare > UINT32_MAX) {
        compare = UINT32_MAX;
    }

    return (uint32_t)compare;
}

HAL_StatusTypeDef PWM_SetDutyPermille(
    PWM_Output *output,
    uint16_t duty_permille)
{
    uint32_t compare;

    if (!PWM_IsValidOutput(output) ||
        (duty_permille > PWM_DUTY_PERMILLE_MAX)) {
        return HAL_ERROR;
    }

    compare = PWM_DutyToCompare(output, duty_permille);
    __HAL_TIM_SET_COMPARE(output->timer, output->channel, compare);
    output->duty_permille = duty_permille;

    return HAL_OK;
}

HAL_StatusTypeDef PWM_SetDutyPercent(
    PWM_Output *output,
    uint8_t duty_percent)
{
    if (duty_percent > 100U) {
        return HAL_ERROR;
    }

    return PWM_SetDutyPermille(output, (uint16_t)duty_percent * 10U);
}

uint16_t PWM_GetDutyPermille(const PWM_Output *output)
{
    if (!PWM_IsValidOutput(output)) {
        return 0U;
    }

    return output->duty_permille;
}

HAL_StatusTypeDef PWM_PrepareDMABuffer(
    const PWM_Output *output,
    const uint16_t *duty_permille,
    uint32_t *compare_buffer,
    uint16_t length)
{
    uint16_t index;

    if (!PWM_IsValidOutput(output) ||
        (duty_permille == NULL) ||
        (compare_buffer == NULL) ||
        (length == 0U)) {
        return HAL_ERROR;
    }

    for (index = 0U; index < length; index++) {
        if (duty_permille[index] > PWM_DUTY_PERMILLE_MAX) {
            return HAL_ERROR;
        }

        compare_buffer[index] =
            PWM_DutyToCompare(output, duty_permille[index]);
    }

    return HAL_OK;
}

HAL_StatusTypeDef PWM_StartDMA(
    PWM_Output *output,
    const uint32_t *compare_buffer,
    uint16_t length)
{
    if (!PWM_IsValidOutput(output) ||
        (compare_buffer == NULL) ||
        (length == 0U)) {
        return HAL_ERROR;
    }

    return HAL_TIM_PWM_Start_DMA(
        output->timer,
        output->channel,
        compare_buffer,
        length);
}

HAL_StatusTypeDef PWM_StopDMA(PWM_Output *output)
{
    HAL_StatusTypeDef status;

    if (!PWM_IsValidOutput(output)) {
        return HAL_ERROR;
    }

    status = HAL_TIM_PWM_Stop_DMA(output->timer, output->channel);
    if (status == HAL_OK) {
        __HAL_TIM_SET_COMPARE(output->timer, output->channel, 0U);
        output->duty_permille = 0U;
    }

    return status;
}