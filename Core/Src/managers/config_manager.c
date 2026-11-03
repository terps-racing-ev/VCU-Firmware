#include "managers/config_manager.h"
#include "main.h"
#include "drivers/bootloader.h"

typedef struct
{
  uint8_t virtualAddress;
  uint16_t value;
} Config_WriteRequest_t;

typedef struct
{
  uint8_t virtualAddress;
} Config_ReadRequest_t;

osMessageQueueId_t Config_WriteQueueHandle;
osMessageQueueId_t Config_ReadQueueHandle;

uint8_t board_id;

void Config_PackWriteReply(CAN_Message_t*, uint8_t, EEPROM_StatusTypeDef);
void Config_PackReadReply(CAN_Message_t*, uint8_t, EEPROM_StatusTypeDef, uint16_t);

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

  // Read bootloader memory
  // Only time this every happens
  board_id = Bootloader_ReadBoardId();

  Config_WriteQueueHandle = osMessageQueueNew(CONFIG_WRITE_QUEUE_LENGTH,
                                             sizeof(Config_WriteRequest_t), NULL);
  if (Config_WriteQueueHandle == NULL)
  {
    return HAL_ERROR;
  }

  Config_ReadQueueHandle = osMessageQueueNew(CONFIG_READ_QUEUE_LENGTH, sizeof(Config_ReadRequest_t), NULL);
  if (Config_ReadQueueHandle == NULL) {
    return HAL_ERROR;
  }

  return HAL_OK;
}

void Config_Manager_Task(void *argument)
{
  (void)argument;

  CAN_Message_t msg = {
    .id = 0,
    .length = 0
  };
  Config_WriteRequest_t w_req;
  Config_ReadRequest_t r_req;

  for (;;)
  {
    while (osMessageQueueGet(Config_WriteQueueHandle, &w_req, NULL, 1U) == osOK)
    {
      
      EEPROM_StatusTypeDef status = Config_WriteVariable(w_req.virtualAddress, w_req.value);
      Config_PackWriteReply(&msg, w_req.virtualAddress, status);

      // Send on both CAN busses since we don't know which it came from
      CAN_SendCTRLMsg(&msg);
      CAN_SendDAQMsg(&msg);
    }

    while (osMessageQueueGet(Config_ReadQueueHandle, &r_req, NULL, 1U) == osOK)
    {
      uint16_t value;
      EEPROM_StatusTypeDef status = Config_ReadVariable(r_req.virtualAddress, &value);
      Config_PackReadReply(&msg, r_req.virtualAddress, status, value);

      // Send on both CAN busses since we don't know which it came from
      CAN_SendCTRLMsg(&msg);
      CAN_SendDAQMsg(&msg);
    }

    uint16_t delay;
    Config_ReadVariable(CONFIG_CONFIG_MANAGER_DELAY_MS, &delay);
    osDelay(delay);
  }
}

bool Config_RxHandler(const CAN_Message_t *msg) {
  // C0             XX          XX
  // ^ config cmd   ^ board ID  ^ parameter address

  uint8_t cmd = (uint8_t) (msg->id >> 16);
  uint8_t msg_board_id = (uint8_t) (msg->id >> 8);

  if (cmd != CAN_CONFIG_CMD || msg_board_id != board_id)
  {
    return false;
  }

  // Read request
  if (msg->length == 0) {
    Config_ReadRequest_t rr = {
      .virtualAddress = (uint8_t) msg->id
    };

    osMessageQueuePut(Config_ReadQueueHandle, &rr, 0U, 0U);
    return true;

  // Write request
  } else if (msg->length == 2) {
    Config_WriteRequest_t wr = {
      .virtualAddress = (uint8_t) msg->id,
      .value = (uint16_t)(((uint16_t) msg->data[1] << 8 | msg->data[0] << 0))
    };

    osMessageQueuePut(Config_WriteQueueHandle, &wr, 0U, 0U);
    return true;
  }

  return false;
}

void Config_PackWriteReply(CAN_Message_t *msg, uint8_t vaddr, EEPROM_StatusTypeDef status) {
  
  msg->id = ((uint32_t) CAN_CONFIG_REPLY << 16)
            | ((uint32_t) board_id << 8)
            | ((uint32_t) vaddr);

  msg->data[0] = (uint8_t) status;

  msg->length = 1;
}

void Config_PackReadReply(CAN_Message_t *msg, uint8_t vaddr, EEPROM_StatusTypeDef status, uint16_t value) {
  
  msg->id = ((uint32_t) CAN_CONFIG_REPLY << 16)
            | ((uint32_t) board_id << 8)
            | ((uint32_t) vaddr);

  msg->data[0] = (uint8_t) status;
  msg->data[1] = (uint8_t) value;
  msg->data[2] = (uint8_t) value >> 8;

  msg->length = 3;
}