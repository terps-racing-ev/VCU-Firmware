#include "managers/io_manager.h"

#include "cmsis_os.h"
#include "data/io.h"
#include "drivers/pwm.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim3;

static PWM_Output drs_pwm;

HAL_StatusTypeDef IO_Manager_Init(void) {
  HAL_GPIO_WritePin(DRS_EN_GPIO_Port, DRS_EN_Pin, GPIO_PIN_RESET);

  /* BTG eFuse initialization: IN/EN, optional DEN, and IS ADC channel. */
  if (IO_InitEFuse(&batt_fans_L_efuse, "Batt_Fans_L_EFuse", Batt_Fans_L_IN_GPIO_Port, Batt_Fans_L_IN_Pin, NULL, 0U, &hadc1, ADC_CHANNEL_4) != HAL_OK) return HAL_ERROR;
  if (IO_InitEFuse(&batt_fans_R_efuse, "Batt_Fans_R_EFuse", Batt_Fans_R_IN_GPIO_Port, Batt_Fans_R_IN_Pin, NULL, 0U, &hadc1, ADC_CHANNEL_5) != HAL_OK) return HAL_ERROR;
  if (IO_InitEFuse(&rad_fans_efuse, "Rad_Fans_EFuse", Rad_Fans_IN_GPIO_Port, Rad_Fans_IN_Pin, BTG_DEN_GPIO_Port, BTG_DEN_Pin, &hadc1, ADC_CHANNEL_6) != HAL_OK) return HAL_ERROR;
  if (IO_InitEFuse(&pump_efuse, "Pump_EFuse", Pump_IN_GPIO_Port, Pump_IN_Pin, Misc_GPIO_3v3_GPIO_Port, Misc_GPIO_3v3_Pin, &hadc1, ADC_CHANNEL_7) != HAL_OK) return HAL_ERROR;
  if (IO_InitEFuse(&drs_efuse, "DRS_EFuse", DRS_EN_GPIO_Port, DRS_EN_Pin, NULL, 0U, &hadc1, ADC_CHANNEL_14) != HAL_OK) return HAL_ERROR;
  /* DRS PWM on PB4 is separate from the PB3 EN and PC4 IS eFuse interface. */
    
  if (IO_InitAnalogIO(&lv_current_raw, "lv_current_raw") != HAL_OK) return HAL_ERROR;
  if (IO_InitCurrent(&lv_current, "lv_current") != HAL_OK) return HAL_ERROR;
  if (IO_InitAnalogIO(&hv_current_raw, "hv_current_raw") != HAL_OK) return HAL_ERROR;
  if (IO_InitCurrent(&hv_current, "hv_current") != HAL_OK) return HAL_ERROR;

  //PWM Init seperate
  if (PWM_Init(&drs_pwm, &htim3, TIM_CHANNEL_1) != HAL_OK) return HAL_ERROR;
  if (PWM_SetDutyPermille(&drs_pwm, 0U) != HAL_OK) return HAL_ERROR;
  if (PWM_Start(&drs_pwm) != HAL_OK)  return HAL_ERROR;
  
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
        status = IO_SetEFuseCommand(&drs_efuse, false);
        if (status != HAL_OK) {
            return status;
        }

        return PWM_SetDutyPermille(&drs_pwm, 0U);
    }

    status = PWM_SetDutyPermille(&drs_pwm, duty_permille);
    if (status != HAL_OK) {
        return status;
    }

    return IO_SetEFuseCommand(&drs_efuse, true);
}
