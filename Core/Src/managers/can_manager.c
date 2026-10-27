#include "cmsis_os.h"
#include "data/can.h"
#include "managers/can_manager.h"
#include "main.h"

extern CAN_HandleTypeDef hcan1;
extern CAN_HandleTypeDef hcan2;

#define CAN_TX_RETRY_DELAY_MS 1U

osMessageQueueId_t CTRL_CAN_RxQueueHandle;
osMessageQueueId_t DAQ_CAN_RxQueueHandle;
osMessageQueueId_t CTRL_CAN_TxQueueHandle;
osMessageQueueId_t DAQ_CAN_TxQueueHandle;

HAL_StatusTypeDef CTRL_CAN_ManagerInit(void)
{
  CTRL_CAN_RxQueueHandle = osMessageQueueNew(CAN_RX_QUEUE_LENGTH, sizeof(CANMessage), NULL);
  CTRL_CAN_TxQueueHandle = osMessageQueueNew(CAN_RX_QUEUE_LENGTH, sizeof(CANMessage), NULL);
  if (CTRL_CAN_RxQueueHandle == NULL || CTRL_CAN_TxQueueHandle == NULL)
  {
    return HAL_ERROR;
  }

  if (HAL_CAN_Start(&hcan1) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

HAL_StatusTypeDef DAQ_CAN_ManagerInit(void)
{
  DAQ_CAN_RxQueueHandle = osMessageQueueNew(CAN_RX_QUEUE_LENGTH, sizeof(CANMessage), NULL);
  DAQ_CAN_TxQueueHandle = osMessageQueueNew(CAN_RX_QUEUE_LENGTH, sizeof(CANMessage), NULL);
  if (DAQ_CAN_RxQueueHandle == NULL || DAQ_CAN_TxQueueHandle == NULL)
  {
    return HAL_ERROR;
  }

  if (HAL_CAN_Start(&hcan2) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

void CTRL_CAN_Manager_Task(void *argument)
{
  (void)argument;

  CANMessage message;
  CAN_TxHeaderTypeDef header;
  uint32_t mailbox;
  HAL_StatusTypeDef status;

  for (;;)
  {
    if (osMessageQueueGet(CTRL_CAN_TxQueueHandle, &message, NULL, osWaitForever) != osOK)
    {
      continue;
    }

    header.IDE = (message.id > 0x7FFU) ? CAN_ID_EXT : CAN_ID_STD;
    header.StdId = message.id;
    header.ExtId = message.id;
    header.RTR = CAN_RTR_DATA;
    header.DLC = message.length;
    header.TransmitGlobalTime = DISABLE;

    for (uint32_t retries = 0U; ; ++retries)
    {
      status = HAL_CAN_AddTxMessage(&hcan1, &header, message.data, &mailbox);
      if (status != HAL_BUSY || retries >= MAX_TX_RETRIES)
      {
        break;
      }

      osDelay(CAN_TX_RETRY_DELAY_MS);
    }
  }
}

void DAQ_CAN_Manager_Task(void *argument)
{
  (void)argument;

  CANMessage message;
  CAN_TxHeaderTypeDef header;
  uint32_t mailbox;
  HAL_StatusTypeDef status;

  for (;;)
  {
    if (osMessageQueueGet(DAQ_CAN_TxQueueHandle, &message, NULL, osWaitForever) != osOK)
    {
      continue;
    }

    header.IDE = (message.id > 0x7FFU) ? CAN_ID_EXT : CAN_ID_STD;
    header.StdId = message.id;
    header.ExtId = message.id;
    header.RTR = CAN_RTR_DATA;
    header.DLC = message.length;
    header.TransmitGlobalTime = DISABLE;

    for (uint32_t retries = 0U; ; ++retries)
    {
      status = HAL_CAN_AddTxMessage(&hcan2, &header, message.data, &mailbox);
      if (status != HAL_BUSY || retries >= MAX_TX_RETRIES)
      {
        break;
      }

      osDelay(CAN_TX_RETRY_DELAY_MS);
    }
  }
}

/* Receive Messages ----------------------------------------------------------*/
static void CAN_ManagerReceiveMessages(CAN_HandleTypeDef *hcan, uint32_t fifo)
{
  osMessageQueueId_t queue;
  CANMessage message;
  CAN_RxHeaderTypeDef header;

  if (hcan->Instance == CAN1)
  {
    queue = CTRL_CAN_RxQueueHandle;
  }
  else if (hcan->Instance == CAN2)
  {
    queue = DAQ_CAN_RxQueueHandle;
  }
  else
  {
    return;
  }

  while (HAL_CAN_GetRxFifoFillLevel(hcan, fifo) > 0U)
  {
    if (HAL_CAN_GetRxMessage(hcan, fifo, &header, message.data) != HAL_OK)
    {
      return;
    }

    message.id = (header.IDE == CAN_ID_EXT) ? header.ExtId : header.StdId;
    message.length = (uint8_t)header.DLC;

    if (queue != NULL)
    {
      (void)osMessageQueuePut(queue, &message, 0U, 0U);
    }
  }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  CAN_ManagerReceiveMessages(hcan, CAN_RX_FIFO0);
}

void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  CAN_ManagerReceiveMessages(hcan, CAN_RX_FIFO1);
}
