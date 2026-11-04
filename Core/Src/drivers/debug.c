#include "debug.h"
#include "config.h"
#include "stm32f4xx_hal.h"

bool Reset_RxHandler(const CAN_Message_t *msg){
    // 0x   A0    XX            00
    //      ^cmd  ^ board id    ^ filler
    // no data included
    
    // Check for this board
    if (
            msg->length != 0
        ||  (uint8_t) msg->id != 0
        ||  (uint8_t) (msg->id >> 8) != board_id
        ||  (uint8_t) (msg->id >> 16) != CAN_RESET_CMD
    ) {
        return false;
    }
    
    // TODO: send debug msg
    // hard because this function will be called in CAN manager context
    // so we can't just send a message and then delay
    NVIC_SystemReset();
}