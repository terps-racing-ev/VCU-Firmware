#ifndef TEST_STM32F4XX_HAL_H
#define TEST_STM32F4XX_HAL_H

/* Host-only HAL surface. CMock supplies the function implementations. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2, HAL_TIMEOUT = 3 } HAL_StatusTypeDef;
typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;

typedef struct { uint32_t reserved; } CAN_TypeDef;
typedef struct { CAN_TypeDef *Instance; } CAN_HandleTypeDef;
extern CAN_TypeDef Test_CAN1, Test_CAN2;
extern CAN_HandleTypeDef hcan1, hcan2;
#define CAN1 (&Test_CAN1)
#define CAN2 (&Test_CAN2)

typedef struct {
    uint32_t FilterIdHigh, FilterIdLow, FilterMaskIdHigh, FilterMaskIdLow;
    uint32_t FilterFIFOAssignment, FilterBank, FilterMode, FilterScale;
    uint32_t FilterActivation, SlaveStartFilterBank;
} CAN_FilterTypeDef;
typedef struct {
    uint32_t StdId, ExtId, IDE, RTR, DLC;
    FunctionalState TransmitGlobalTime;
} CAN_TxHeaderTypeDef;
typedef struct {
    uint32_t StdId, ExtId, IDE, RTR, DLC, Timestamp, FilterMatchIndex;
} CAN_RxHeaderTypeDef;

#define CAN_ID_STD 0U
#define CAN_ID_EXT 4U
#define CAN_RTR_DATA 0U
#define CAN_FILTERMODE_IDMASK 0U
#define CAN_FILTERSCALE_32BIT 1U
#define CAN_RX_FIFO0 0U
#define CAN_RX_FIFO1 1U
#define CAN_IT_RX_FIFO0_MSG_PENDING 2U
#define CAN_IT_RX_FIFO1_MSG_PENDING 16U

HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *hcan, CAN_FilterTypeDef *filter);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *hcan, uint32_t interrupts);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *hcan, CAN_TxHeaderTypeDef *header,
                                     const uint8_t data[], uint32_t *mailbox);
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *hcan, uint32_t fifo);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *hcan, uint32_t fifo,
                                     CAN_RxHeaderTypeDef *header, uint8_t data[]);
/* Implemented by can_manager.c; extern excludes them from CMock generation. */
extern void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan);
extern void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcan);

typedef struct { volatile uint32_t ACR; } FLASH_TypeDef;
extern FLASH_TypeDef Test_FLASH;
#define FLASH (&Test_FLASH)
#define FLASH_ACR_DCEN (1U << 10)
#define FLASH_TYPEPROGRAM_WORD 2U
#define FLASH_TYPEERASE_SECTORS 0U
#define FLASH_VOLTAGE_RANGE_3 2U
#define FLASH_SECTOR_9 9U
#define FLASH_SECTOR_10 10U
#define FLASH_FLAG_EOP 1U
#define FLASH_FLAG_OPERR 2U
#define FLASH_FLAG_WRPERR 16U
#define FLASH_FLAG_PGAERR 32U
#define FLASH_FLAG_PGPERR 64U
#define FLASH_FLAG_PGSERR 128U

typedef struct {
    uint32_t TypeErase, Banks, Sector, NbSectors, VoltageRange;
} FLASH_EraseInitTypeDef;

HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t data);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *erase, uint32_t *sector_error);

/* Turn register macros/intrinsics into mockable calls on the host. */
void Test_FLASH_ClearFlag(uint32_t flags);
void Test_FLASH_DataCacheDisable(void);
void Test_FLASH_DataCacheReset(void);
void Test_FLASH_DataCacheEnable(void);
void Test_DataSyncBarrier(void);
#define __HAL_FLASH_CLEAR_FLAG(flags) Test_FLASH_ClearFlag(flags)
#define __HAL_FLASH_DATA_CACHE_DISABLE() Test_FLASH_DataCacheDisable()
#define __HAL_FLASH_DATA_CACHE_RESET() Test_FLASH_DataCacheReset()
#define __HAL_FLASH_DATA_CACHE_ENABLE() Test_FLASH_DataCacheEnable()
#define __DSB() Test_DataSyncBarrier()

#endif
