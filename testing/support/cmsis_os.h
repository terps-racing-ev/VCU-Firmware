#ifndef TEST_CMSIS_OS_H
#define TEST_CMSIS_OS_H

/* Host declarations for the CMSIS-RTOS2 API used by application modules. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *osMessageQueueId_t;
typedef enum {
    osOK = 0,
    osError = -1,
    osErrorTimeout = -2,
    osErrorResource = -3,
    osErrorParameter = -4,
    osErrorNoMemory = -5,
    osErrorISR = -6
} osStatus_t;

typedef struct {
    const char *name;
    uint32_t attr_bits;
    void *cb_mem;
    uint32_t cb_size;
    void *mq_mem;
    uint32_t mq_size;
} osMessageQueueAttr_t;

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size,
                                   const osMessageQueueAttr_t *attr);
osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg_ptr,
                             uint8_t msg_prio, uint32_t timeout);
osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id, void *msg_ptr,
                             uint8_t *msg_prio, uint32_t timeout);
osStatus_t osDelay(uint32_t ticks);

#endif
