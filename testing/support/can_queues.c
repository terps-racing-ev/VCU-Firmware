#include "cmsis_os.h"

/* data/can tests mock the manager instead of linking its implementation. */
osMessageQueueId_t CTRL_CAN_RxQueueHandle;
osMessageQueueId_t DAQ_CAN_RxQueueHandle;
osMessageQueueId_t CTRL_CAN_TxQueueHandle;
osMessageQueueId_t DAQ_CAN_TxQueueHandle;
