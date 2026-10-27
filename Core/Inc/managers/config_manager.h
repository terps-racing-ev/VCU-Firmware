#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "data/config.h"

#define CONFIG_WRITE_QUEUE_LENGTH 16U

extern osMessageQueueId_t Config_WriteQueueHandle;

/* Call after osKernelInitialize(), before starting the scheduler. Returns
 * HAL_OK/HAL_ERROR. Access EEPROM through data/config.h after init.
 */
int Config_Manager_Init(void);
void Config_Manager_Task(void *argument);

#endif /* CONFIG_MANAGER_H */
