#include "data/config.h"
#include "managers/config_manager.h"

EEPROM_StatusTypeDef Config_ReadVariable(uint16_t virtualAddress, uint32_t *value)
{
  return EEPROM_ReadVariable(virtualAddress, value);
}

osStatus_t Config_WriteVariable(uint16_t virtualAddress, uint32_t value)
{
  if (virtualAddress >= EEPROM_VARIABLE_COUNT)
  {
    return osErrorParameter;
  }
  if (Config_WriteQueueHandle == NULL)
  {
    return osErrorResource;
  }

  const ConfigWriteRequest request = {virtualAddress, value};
  return osMessageQueuePut(Config_WriteQueueHandle, &request, 0U, 0U);
}
