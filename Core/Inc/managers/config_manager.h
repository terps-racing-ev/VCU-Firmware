#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "data/config.h"
#include "data/can.h"

#define CONFIG_WRITE_QUEUE_LENGTH 16U
#define CONFIG_READ_QUEUE_LENGTH 16U

/* Call after osKernelInitialize(), before starting the scheduler. Returns
 * HAL_OK/HAL_ERROR. Access EEPROM through data/config.h after init.
 */
int Config_Manager_Init(void);
void Config_Manager_Task(void *argument);

bool Config_RxHandler(const CAN_Message_t *msg);

#endif /* CONFIG_MANAGER_H */
