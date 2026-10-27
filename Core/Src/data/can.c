#include "data/can.h"
#include "managers/can_manager.h"

static osStatus_t CAN_EnqueueMessage(osMessageQueueId_t queue, const CANMessage *message)
{
  if (queue == NULL || message == NULL ||
      message->length > sizeof(message->data) || message->id > 0x1FFFFFFFU)
  {
    return osErrorParameter;
  }

  return osMessageQueuePut(queue, message, 0U, 0U);
}

osStatus_t CAN_SendCTRLMsg(const CANMessage *message)
{
  return CAN_EnqueueMessage(CTRL_CAN_TxQueueHandle, message);
}

osStatus_t CAN_SendDAQMsg(const CANMessage *message)
{
  return CAN_EnqueueMessage(DAQ_CAN_TxQueueHandle, message);
}
