#ifndef PWM_H
#define PWM_H

#include "stm32f4xx_hal.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PWM_DUTY_PERMILLE_MAX 1000U

/**
 * @brief Hardware PWM output controlled by an STM32 timer channel.
 *
 * The timer, channel, output pin, period, and optional DMA request must be
 * configured by CubeMX before this driver is used.
 */
typedef struct {
    TIM_HandleTypeDef *timer;
    uint32_t channel;
    uint16_t duty_permille;
} PWM_Output;

HAL_StatusTypeDef PWM_Init(
    PWM_Output *output,
    TIM_HandleTypeDef *timer,
    uint32_t channel);

HAL_StatusTypeDef PWM_Start(PWM_Output *output);
HAL_StatusTypeDef PWM_Stop(PWM_Output *output);

/** @brief Set duty cycle in per-mille (0 = 0%, 1000 = 100%). */
HAL_StatusTypeDef PWM_SetDutyPermille(
    PWM_Output *output,
    uint16_t duty_permille);

HAL_StatusTypeDef PWM_SetDutyPercent(
    PWM_Output *output,
    uint8_t duty_percent);

uint16_t PWM_GetDutyPermille(const PWM_Output *output);

/** @brief Convert a per-mille duty value into a timer CCR value. */
uint32_t PWM_DutyToCompare(
    const PWM_Output *output,
    uint16_t duty_permille);

/**
 * @brief Convert a duty-cycle sequence into CCR values for DMA.
 *
 * compare_buffer must remain allocated while DMA is active.
 */
HAL_StatusTypeDef PWM_PrepareDMABuffer(
    const PWM_Output *output,
    const uint16_t *duty_permille,
    uint32_t *compare_buffer,
    uint16_t length);

/**
 * @brief Start a CubeMX-configured PWM DMA transfer.
 *
 * DMA Normal versus Circular behavior is selected in CubeMX. Each buffer
 * element is a timer compare value, normally produced by
 * PWM_PrepareDMABuffer().
 */
HAL_StatusTypeDef PWM_StartDMA(
    PWM_Output *output,
    const uint32_t *compare_buffer,
    uint16_t length);

HAL_StatusTypeDef PWM_StopDMA(PWM_Output *output);

#ifdef __cplusplus
}
#endif

#endif /* PWM_H */