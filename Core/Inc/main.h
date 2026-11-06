/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LD1_Pin GPIO_PIN_13
#define LD1_GPIO_Port GPIOC
#define LD2_Pin GPIO_PIN_14
#define LD2_GPIO_Port GPIOC
#define LD2C15_Pin GPIO_PIN_15
#define LD2C15_GPIO_Port GPIOC
#define LV_Current_Sense_Pin GPIO_PIN_0
#define LV_Current_Sense_GPIO_Port GPIOC
#define RTD_Pin GPIO_PIN_0
#define RTD_GPIO_Port GPIOB
#define BSPD_Latched_Pin GPIO_PIN_1
#define BSPD_Latched_GPIO_Port GPIOB
#define BSPD_Fault_Pin GPIO_PIN_2
#define BSPD_Fault_GPIO_Port GPIOB
#define IMD_Latched_Pin GPIO_PIN_10
#define IMD_Latched_GPIO_Port GPIOB
#define BMS_Latched_Pin GPIO_PIN_11
#define BMS_Latched_GPIO_Port GPIOB
#define SDC_Post_Buttons_Pin GPIO_PIN_14
#define SDC_Post_Buttons_GPIO_Port GPIOB
#define Batt_Fans_L_IN_Pin GPIO_PIN_15
#define Batt_Fans_L_IN_GPIO_Port GPIOB
#define Batt_Fans_R_IN_Pin GPIO_PIN_7
#define Batt_Fans_R_IN_GPIO_Port GPIOC
#define Batt_Fans_R_DEN_Pin GPIO_PIN_8
#define Batt_Fans_R_DEN_GPIO_Port GPIOC
#define Rad_Fans_IN_Pin GPIO_PIN_9
#define Rad_Fans_IN_GPIO_Port GPIOC
#define BTG_DEN_Pin GPIO_PIN_8
#define BTG_DEN_GPIO_Port GPIOA
#define Pump_IN_Pin GPIO_PIN_9
#define Pump_IN_GPIO_Port GPIOA
#define Pump_DEN_Pin GPIO_PIN_10
#define Pump_DEN_GPIO_Port GPIOA
#define RTM_Lamp_Check_Pin GPIO_PIN_15
#define RTM_Lamp_Check_GPIO_Port GPIOA
#define TSSI_Green_EN_Pin GPIO_PIN_10
#define TSSI_Green_EN_GPIO_Port GPIOC
#define TSSI_Red_En_Pin GPIO_PIN_11
#define TSSI_Red_En_GPIO_Port GPIOC
#define Brake_Light_EN_Pin GPIO_PIN_12
#define Brake_Light_EN_GPIO_Port GPIOC
#define DRS_IN_Pin GPIO_PIN_3
#define DRS_IN_GPIO_Port GPIOB
#define DRS_PWM_Pin GPIO_PIN_4
#define DRS_PWM_GPIO_Port GPIOB
#define Buzzer_EN_Pin GPIO_PIN_5
#define Buzzer_EN_GPIO_Port GPIOB
#define Data_Rail_EN_Pin GPIO_PIN_6
#define Data_Rail_EN_GPIO_Port GPIOB
#define Data_Rail_Diag_En_Pin GPIO_PIN_7
#define Data_Rail_Diag_En_GPIO_Port GPIOB
#define SDC_Post_Raw_Pin GPIO_PIN_8
#define SDC_Post_Raw_GPIO_Port GPIOB
#define SDC_Post_ILs_Pin GPIO_PIN_9
#define SDC_Post_ILs_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
