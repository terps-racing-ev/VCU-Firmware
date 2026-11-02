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
  CTRL_CAN_RxQueueHandle = osMessageQueueNew(CAN_RX_QUEUE_LENGTH, sizeof(CAN_Message_t), NULL);
  CTRL_CAN_TxQueueHandle = osMessageQueueNew(CAN_RX_QUEUE_LENGTH, sizeof(CAN_Message_t), NULL);
  if (CTRL_CAN_RxQueueHandle == NULL || CTRL_CAN_TxQueueHandle == NULL)
  {
    return HAL_ERROR;
  }

  // Configure CAN filter BEFORE starting CAN
  // Accept all extended CAN IDs (for debugging)
  CAN_FilterTypeDef filterConfig;
  filterConfig.FilterBank = 0;
  filterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  filterConfig.FilterScale = CAN_FILTERSCALE_32BIT;

  // Accept all extended IDs: ID=0, Mask=0 means "don't care about any bits"
  filterConfig.FilterIdHigh = 0x0000;
  filterConfig.FilterIdLow = 0x0004;   // Only IDE bit set (extended ID)
  filterConfig.FilterMaskIdHigh = 0x0000;  // Don't care about any ID bits
  filterConfig.FilterMaskIdLow = 0x0004;   // But we DO care about IDE bit (only extended)
  
  filterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
  filterConfig.FilterActivation = ENABLE;
  filterConfig.SlaveStartFilterBank = 14;
  
  if (HAL_CAN_ConfigFilter(&hcan1, &filterConfig) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (HAL_CAN_Start(&hcan1) != HAL_OK) 
  {
    return HAL_ERROR;
  }

  HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);

  return HAL_OK;
}

HAL_StatusTypeDef DAQ_CAN_ManagerInit(void)
{
  DAQ_CAN_RxQueueHandle = osMessageQueueNew(CAN_RX_QUEUE_LENGTH, sizeof(CAN_Message_t), NULL);
  DAQ_CAN_TxQueueHandle = osMessageQueueNew(CAN_RX_QUEUE_LENGTH, sizeof(CAN_Message_t), NULL);
  if (DAQ_CAN_RxQueueHandle == NULL || DAQ_CAN_TxQueueHandle == NULL)
  {
    return HAL_ERROR;
  }

  // Configure CAN filter BEFORE starting CAN
  // Accept all extended CAN IDs (for debugging)
  CAN_FilterTypeDef filterConfig;
  filterConfig.FilterBank = 14;
  filterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  filterConfig.FilterScale = CAN_FILTERSCALE_32BIT;

  // Accept all extended IDs: ID=0, Mask=0 means "don't care about any bits"
  filterConfig.FilterIdHigh = 0x0000;
  filterConfig.FilterIdLow = 0x0004;   // Only IDE bit set (extended ID)
  filterConfig.FilterMaskIdHigh = 0x0000;  // Don't care about any ID bits
  filterConfig.FilterMaskIdLow = 0x0004;   // But we DO care about IDE bit (only extended)
  
  filterConfig.FilterFIFOAssignment = CAN_RX_FIFO1;
  filterConfig.FilterActivation = ENABLE;
  filterConfig.SlaveStartFilterBank = 14;
  
  if (HAL_CAN_ConfigFilter(&hcan2, &filterConfig) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (HAL_CAN_Start(&hcan2) != HAL_OK)
  {
    return HAL_ERROR;
  }

  HAL_CAN_ActivateNotification(&hcan2, CAN_IT_RX_FIFO1_MSG_PENDING);

  return HAL_OK;
}

void CTRL_CAN_Manager_Task(void *argument)
{
  (void)argument;

  CAN_Message_t message;
  CAN_TxHeaderTypeDef header = {
    .IDE = CAN_ID_EXT,
    .RTR = CAN_RTR_DATA,
    .TransmitGlobalTime = DISABLE
  };
  uint32_t mailbox;
  HAL_StatusTypeDef status;

  for (;;)
  {
    // TODO: add max sends per cycle
    // TODO: get CAN statistics
    while (osMessageQueueGet(CTRL_CAN_TxQueueHandle, &message, NULL, 0U) == osOK)
    {
      header.ExtId = message.id;
      header.DLC = message.length;

      for (uint32_t retries = 0U; ; ++retries)
      {
        // TODO: this drops msgs that take too many tries
        status = HAL_CAN_AddTxMessage(&hcan1, &header, message.data, &mailbox);
        if (status == HAL_OK || retries >= MAX_TX_RETRIES)
        {
          break;
        }

        osDelay(CAN_TX_RETRY_DELAY_MS);
      }
    }

    while (osMessageQueueGet(CTRL_CAN_RxQueueHandle, &message, NULL, 0U) == osOK)
    { 
      // Send to each of the msg handlers
      for (int i = 0; i < CAN_RxHandlers_Size; i++) {
        CAN_RxHandlers[i](&message);
      }
    }

    osDelay(1U);
  }
}

void DAQ_CAN_Manager_Task(void *argument)
{
  (void)argument;

  CAN_Message_t message;
  CAN_TxHeaderTypeDef header = {
    .IDE = CAN_ID_EXT,
    .RTR = CAN_RTR_DATA,
    .TransmitGlobalTime = DISABLE
  };
  uint32_t mailbox;
  HAL_StatusTypeDef status;

  for (;;)
  {

    // TODO: add max sends per cycle
    // TODO: get CAN statistics
    while (osMessageQueueGet(DAQ_CAN_TxQueueHandle, &message, NULL, 0U) == osOK)
    {
      header.ExtId = message.id;
      header.DLC = message.length;

      // TODO: this drops msgs that take too many tries
      for (uint32_t retries = 0U; ; ++retries)
      {
        status = HAL_CAN_AddTxMessage(&hcan2, &header, message.data, &mailbox);
        if (status == HAL_OK || retries >= MAX_TX_RETRIES)
        {
          break;
        }

        osDelay(CAN_TX_RETRY_DELAY_MS);
      }
    }

    while (osMessageQueueGet(DAQ_CAN_RxQueueHandle, &message, NULL, 0U) == osOK)
    { 
      // Send to each of the msg handlers
      for (int i = 0; i < CAN_RxHandlers_Size; i++) {
        CAN_RxHandlers[i](&message);
      }
    }
    osDelay(1U);
  }
}

/* Receive Messages ----------------------------------------------------------*/
static void CAN_ManagerReceiveMessages(CAN_HandleTypeDef *hcan, uint32_t fifo)
{
  osMessageQueueId_t queue;
  CAN_Message_t message;
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

    // TODO: this drops when queue is full, get CAN statistics pls
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
