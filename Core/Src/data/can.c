#include "data/can.h"
#include "managers/can_manager.h"

CAN_Statistics_t ctrl_can_statistics;
CAN_Statistics_t daq_can_statistics;

HAL_StatusTypeDef _CAN_InitStatsMutex(CAN_Statistics_t *stats, const char *mutex_name);

HAL_StatusTypeDef CAN_Init(void) {

  if (
    _CAN_InitStatsMutex(&ctrl_can_statistics, "ctrl_can_stats_mutex") != HAL_OK
    || _CAN_InitStatsMutex(&daq_can_statistics, "daq_can_stats_mutex") != HAL_OK
  ) {
    return HAL_ERROR;
  }

  CAN_ResetStatistics(&ctrl_can_statistics);
  CAN_ResetStatistics(&daq_can_statistics);

  return HAL_OK;
}

HAL_StatusTypeDef _CAN_InitStatsMutex(CAN_Statistics_t *stats, const char *mutex_name) {
  const osMutexAttr_t stats_mutex_attr = {
    .name = mutex_name
  };
  stats->mutex = osMutexNew(&stats_mutex_attr);
  return (stats->mutex == NULL) ? HAL_ERROR : HAL_OK;
}

void CAN_ResetStatistics(CAN_Statistics_t *stats) {
  stats->bus_off_count = 0;
  stats->error_passive_count = 0;
  stats->recovery_count = 0;
  stats->rx_invalid_count = 0;
  stats->rx_message_count = 0;
  stats->rx_queue_full_count = 0;
  stats->tx_error_count = 0;
  stats->tx_queue_full_count = 0;
  stats->tx_success_count = 0;
}

static osStatus_t CAN_EnqueueMessage(osMessageQueueId_t queue, const CAN_Message_t *message, CAN_Statistics_t *stats)
{
  if (queue == NULL || message == NULL ||
      message->length > sizeof(message->data) || message->id > 0x1FFFFFFFU)
  {
    return osErrorParameter;
  }

  osStatus_t status = osMessageQueuePut(queue, message, 0U, 0U);
  
  // Assume any error msg means queue is full
  // TODO: is this correct?
  if (status != osOK) {
    // TODO: add timeout
    osMutexAcquire(stats->mutex, osWaitForever);
    stats->tx_queue_full_count++;
    osMutexRelease(stats->mutex);
  }

  return status;
}

osStatus_t CAN_SendCTRLMsg(const CAN_Message_t *message)
{
  return CAN_EnqueueMessage(CTRL_CAN_TxQueueHandle, message, &ctrl_can_statistics);
}

osStatus_t CAN_SendDAQMsg(const CAN_Message_t *message)
{
  return CAN_EnqueueMessage(DAQ_CAN_TxQueueHandle, message, &daq_can_statistics);
}

