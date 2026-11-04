#include "stm32f4xx_hal.h"

CAN_TypeDef Test_CAN1, Test_CAN2;
CAN_HandleTypeDef hcan1 = { .Instance = CAN1 };
CAN_HandleTypeDef hcan2 = { .Instance = CAN2 };
FLASH_TypeDef Test_FLASH;

/* Link-only stand-ins for the STM32 linker script's data-section symbols.
 * Exercising EEPROM_Init/storage requires a flash-memory/linker fixture.
 */
uint8_t _sidata, _sdata, _edata, _siccmram, _sccmram, _eccmram;
