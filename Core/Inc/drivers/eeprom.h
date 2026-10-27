#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>

#define EEPROM_VARIABLE_COUNT 256U

typedef enum
{
  EEPROM_OK = 0,
  EEPROM_NOT_FOUND,
  EEPROM_INVALID_ARGUMENT,
  EEPROM_NOT_INITIALIZED,
  EEPROM_FLASH_ERROR
} EEPROM_StatusTypeDef;

/*
 * Flash-backed virtual variables, addresses 0..EEPROM_VARIABLE_COUNT-1,
 * with 32-bit values. Sectors 9 and 10 (0x080A0000..0x080DFFFF) must be kept
 * exclusively for this driver. Linker scripts are unchanged; init checks the
 * firmware load addresses for overlap. Do not place other storage in these
 * sectors. Programming assumes a 2.7-3.6 V supply.
 *
 * Call EEPROM_Init() after HAL_Init(), before reading or writing. It restores
 * committed values and initializes empty storage. Unknown nonempty storage
 * returns EEPROM_FLASH_ERROR instead of being automatically formatted.
 *
 * Calls are blocking and must be serialized by the caller, including against
 * other flash users. Use from task/startup context, never from an ISR. Flash
 * erase/program operations stall flash execution on the STM32F405.
 * After EEPROM_FLASH_ERROR, call EEPROM_Init() before retrying an operation.
 * An interrupted/failed write may have committed; read back after reinit.
 */
EEPROM_StatusTypeDef EEPROM_Init(void);

/* On failure, *value is unchanged. Unwritten variables return NOT_FOUND. */
EEPROM_StatusTypeDef EEPROM_ReadVariable(uint16_t virtualAddress, uint32_t *value);

/* Unchanged values do not consume flash writes. All 32-bit values are valid. */
EEPROM_StatusTypeDef EEPROM_WriteVariable(uint16_t virtualAddress, uint32_t value);

#endif /* EEPROM_H */
