#ifndef DATA_CONFIG_H
#define DATA_CONFIG_H

#include "cmsis_os.h"
#include "drivers/eeprom.h"

/* Persistent virtual addresses. Keep assigned addresses stable across updates. */
#define CONFIG_VADDR_DEFAULT 0U

typedef struct
{
  uint16_t virtualAddress;
  uint32_t value;
} ConfigWriteRequest;

/* Reads call EEPROM directly and return the last completed write's value.
 * Pending writes are not flushed. On failure, *value is unchanged.
 */
EEPROM_StatusTypeDef Config_ReadVariable(uint16_t virtualAddress, uint32_t *value);

/* Nonblocking FIFO write submission. osOK means queued, not committed.
 * The virtual address and value are copied into the manager's write queue.
 * Invalid addresses return osErrorParameter; a full queue or uninitialized
 * manager returns osErrorResource.
 */
osStatus_t Config_WriteVariable(uint16_t virtualAddress, uint32_t value);

#endif /* DATA_CONFIG_H */
