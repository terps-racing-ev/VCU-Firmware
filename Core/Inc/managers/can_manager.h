#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include "cmsis_os.h"
#include "data/can.h"
#include "main.h"

#define MAX_TX_RETRIES 3U
#define CAN_RX_QUEUE_LENGTH 16U

extern osMessageQueueId_t CTRL_CAN_RxQueueHandle;
extern osMessageQueueId_t DAQ_CAN_RxQueueHandle;
extern osMessageQueueId_t CTRL_CAN_TxQueueHandle;
extern osMessageQueueId_t DAQ_CAN_TxQueueHandle;

static const CANMessageHandler CAN_RxHandlers = {};
#define CAN_RxHandlers_Size (sizeof(CAN_RxHandlers)/sizeof(CANMessageHandler))

HAL_StatusTypeDef CTRL_CAN_ManagerInit(void);
HAL_StatusTypeDef DAQ_CAN_ManagerInit(void);
void CTRL_CAN_Manager_Task(void *argument);
void DAQ_CAN_Manager_Task(void *argument);

#endif /* CAN_MANAGER_H */
