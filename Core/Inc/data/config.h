#ifndef DATA_CONFIG_H
#define DATA_CONFIG_H

#include "cmsis_os.h"
#include "drivers/eeprom.h"

/* Persistent 8-bit virtual addresses. Keep assigned addresses stable across updates. */
#define CONFIG_VADDR_DEFAULT 0U

// Read by config manager
extern uint8_t board_id;

typedef struct {
    uint8_t virtual_address;
    uint16_t default_value;
} Config_Value_t;

/* ############ CONFIG VARIABLES ############## */
/* Add 1 addr + value per variable, then add to Config_Values */

#define CONFIG_DEFAULT_ADDR     0U
#define CONFIG_DEFAULT_VALUE    0U

extern Config_Value_t Config_Values[];

/* Reads call EEPROM directly and return the last completed write's value.
 * Pending writes are not flushed. On failure, *value is unchanged.
 */
EEPROM_StatusTypeDef Config_ReadVariable(uint8_t virtualAddress, uint16_t *value);

#endif /* DATA_CONFIG_H */
