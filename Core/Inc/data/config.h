#ifndef DATA_CONFIG_H
#define DATA_CONFIG_H

#include "drivers/eeprom.h"

/* Persistent 8-bit virtual addresses. Keep assigned addresses stable across updates. */
#define CONFIG_VADDR_DEFAULT 0U

// Read by config manager
extern uint8_t board_id;

typedef struct {
    uint8_t virtual_address;
    uint16_t default_value;
    uint16_t min_value;
    uint16_t max_value;
} Config_Value_t;

/* ############ CONFIG VARIABLES ############## */
/* Add an address for each variable, then add to Config_Values (in config.c) */


#define CONFIG_DEFAULT_VALUE    0U

#define CONFIG_DEFAULT_ADDR                 0
#define CONFIG_CTRL_CAN_MANAGER_DELAY_MS    1
#define CONFIG_DAQ_CAN_MANAGER_DELAY_MS     2
#define CONFIG_CONFIG_MANAGER_DELAY_MS      3

extern const Config_Value_t Config_Values[];

/* Reads call EEPROM directly and return the last completed write's value.
 * Pending writes are not flushed. On failure, *value is unchanged.
 */
EEPROM_StatusTypeDef Config_ReadVariable(uint8_t virtualAddress, uint16_t *value);
EEPROM_StatusTypeDef Config_WriteVariable(uint8_t virtualAddress, uint16_t value);

#endif /* DATA_CONFIG_H */
