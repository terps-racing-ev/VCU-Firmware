#ifndef DATA_CAN_H
#define DATA_CAN_H

#include "cmsis_os.h"
#include <stdint.h>

typedef struct
{
  uint32_t id;
  uint8_t length;
  uint8_t data[8];
} CANMessage;

/* CAN Statistics Structure */
typedef struct {
    uint32_t tx_success_count;      // Successfully transmitted messages
    uint32_t tx_error_count;        // Failed transmissions
    uint32_t tx_queue_full_count;   // Times TX queue was full
    uint32_t rx_message_count;      // Received messages
    uint32_t rx_queue_full_count;   // Times RX queue was full
    uint32_t bus_off_count;         // CAN bus-off events
    uint32_t error_passive_count;   // Times interface entered error-passive state
    uint32_t recovery_count;        // Successful recovery attempts
    uint32_t rx_invalid_count;      // Invalid RX frames dropped
} CAN_Statistics_t;

typedef void (*CANMessageHandler)(const CANMessage *message);

osStatus_t CAN_SendCTRLMsg(const CANMessage *message);
osStatus_t CAN_SendDAQMsg(const CANMessage *message);

#endif /* DATA_CAN_H */
