#include "managers/config_manager.h"
#include "main.h"

osMessageQueueId_t Config_WriteQueueHandle;

int Config_Manager_Init(void)
{
  if (Config_WriteQueueHandle != NULL)
  {
    return HAL_OK;
  }

  if (EEPROM_Init() != EEPROM_OK)
  {
    return HAL_ERROR;
  }

  Config_WriteQueueHandle = osMessageQueueNew(CONFIG_WRITE_QUEUE_LENGTH,
                                             sizeof(ConfigWriteRequest), NULL);
  if (Config_WriteQueueHandle == NULL)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

void Config_Manager_Task(void *argument)
{
  (void)argument;

  ConfigWriteRequest request;

  for (;;)
  {
    if (osMessageQueueGet(Config_WriteQueueHandle, &request, NULL, osWaitForever) != osOK)
    {
      continue;
    }

    EEPROM_StatusTypeDef status = EEPROM_WriteVariable(request.virtualAddress, request.value);

    // TODO: send ack
  }
}