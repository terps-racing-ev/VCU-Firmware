#include "drivers/eeprom.h"
#include "stm32f4xx_hal.h"

#define EEPROM_PAGE_SIZE 0x20000U
#define EEPROM_PAGE_0_ADDRESS 0x080A0000U
#define EEPROM_PAGE_1_ADDRESS 0x080C0000U
#define EEPROM_HEADER_SIZE 16U
#define EEPROM_RECORD_SIZE 12U
#define EEPROM_MAGIC 0x45455031U
#define EEPROM_ERASED 0xFFFFFFFFU
#define EEPROM_COMMITTED 0U

/* Existing STM32 linker symbols; no EEPROM linker section is required. */
extern uint8_t _sidata;
extern uint8_t _sdata;
extern uint8_t _edata;
extern uint8_t _siccmram __attribute__((weak));
extern uint8_t _sccmram __attribute__((weak));
extern uint8_t _eccmram __attribute__((weak));

static const uint32_t EEPROM_PageAddresses[2] = {
  EEPROM_PAGE_0_ADDRESS, EEPROM_PAGE_1_ADDRESS
};
static const uint32_t EEPROM_PageSectors[2] = {
  FLASH_SECTOR_9, FLASH_SECTOR_10
};

static uint32_t EEPROM_Values[EEPROM_VARIABLE_COUNT];
static uint8_t EEPROM_Present[EEPROM_VARIABLE_COUNT];
static uint32_t EEPROM_ActivePage;
static uint32_t EEPROM_NextAddress;
static uint8_t EEPROM_Initialized;

static uint32_t EEPROM_ReadWord(uint32_t address)
{
  return *(volatile const uint32_t *)(uintptr_t)address;
}

static uint8_t EEPROM_IsPageValid(uint32_t page)
{
  uint32_t address = EEPROM_PageAddresses[page];

  return EEPROM_ReadWord(address) == EEPROM_MAGIC &&
         (EEPROM_ReadWord(address + 4U) ^ EEPROM_ReadWord(address + 8U)) == EEPROM_ERASED &&
         EEPROM_ReadWord(address + 12U) == EEPROM_COMMITTED;
}

static uint8_t EEPROM_IsErased(uint32_t address, uint32_t end)
{
  for (; address < end; address += 4U)
  {
    if (EEPROM_ReadWord(address) != EEPROM_ERASED)
    {
      return 0U;
    }
  }

  return 1U;
}

static HAL_StatusTypeDef EEPROM_ProgramWord(uint32_t address, uint32_t value)
{
  HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, value);

  /* Scanning erased slots can cache them before they are programmed. */
  if ((FLASH->ACR & FLASH_ACR_DCEN) != 0U)
  {
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
  }
  __DSB();

  if (status == HAL_OK && EEPROM_ReadWord(address) != value)
  {
    status = HAL_ERROR;
  }

  return status;
}

static HAL_StatusTypeDef EEPROM_ErasePage(uint32_t page)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sectorError;

  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Sector = EEPROM_PageSectors[page];
  erase.NbSectors = 1U;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  if (HAL_FLASHEx_Erase(&erase, &sectorError) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return EEPROM_IsErased(EEPROM_PageAddresses[page],
                         EEPROM_PageAddresses[page] + EEPROM_PAGE_SIZE) ? HAL_OK : HAL_ERROR;
}

static HAL_StatusTypeDef EEPROM_StartPage(uint32_t page, uint32_t generation)
{
  uint32_t address = EEPROM_PageAddresses[page];

  if (EEPROM_ProgramWord(address, EEPROM_MAGIC) != HAL_OK ||
      EEPROM_ProgramWord(address + 4U, generation) != HAL_OK ||
      EEPROM_ProgramWord(address + 8U, ~generation) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

static HAL_StatusTypeDef EEPROM_AppendRecord(uint32_t address, uint16_t virtualAddress,
                                           uint32_t value)
{
  uint32_t tag = ((uint32_t)(uint16_t)~virtualAddress << 16U) | virtualAddress;

  /* Data and its inverse are written before the address commits the record. */
  if (EEPROM_ProgramWord(address, value) != HAL_OK ||
      EEPROM_ProgramWord(address + 4U, ~value) != HAL_OK ||
      EEPROM_ProgramWord(address + 8U, tag) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

static void EEPROM_LoadPage(void)
{
  uint32_t start = EEPROM_PageAddresses[EEPROM_ActivePage] + EEPROM_HEADER_SIZE;
  uint32_t end = EEPROM_PageAddresses[EEPROM_ActivePage] + EEPROM_PAGE_SIZE;

  EEPROM_NextAddress = start;
  for (uint32_t index = 0U; index < EEPROM_VARIABLE_COUNT; ++index)
  {
    EEPROM_Present[index] = 0U;
  }

  for (uint32_t address = start; address + EEPROM_RECORD_SIZE <= end;
       address += EEPROM_RECORD_SIZE)
  {
    uint32_t value = EEPROM_ReadWord(address);
    uint32_t inverse = EEPROM_ReadWord(address + 4U);
    uint32_t tag = EEPROM_ReadWord(address + 8U);
    uint16_t virtualAddress = (uint16_t)tag;

    /* Consume torn records too; flash slots cannot be overwritten. */
    if (value != EEPROM_ERASED || inverse != EEPROM_ERASED || tag != EEPROM_ERASED)
    {
      EEPROM_NextAddress = address + EEPROM_RECORD_SIZE;
    }

    if (virtualAddress < EEPROM_VARIABLE_COUNT &&
        (uint16_t)(tag >> 16U) == (uint16_t)~virtualAddress &&
        (value ^ inverse) == EEPROM_ERASED)
    {
      EEPROM_Values[virtualAddress] = value;
      EEPROM_Present[virtualAddress] = 1U;
    }
  }
}

static HAL_StatusTypeDef EEPROM_TransferPage(uint16_t virtualAddress, uint32_t value)
{
  uint32_t page = EEPROM_ActivePage ^ 1U;
  uint32_t base = EEPROM_PageAddresses[page];
  uint32_t address = base + EEPROM_HEADER_SIZE;
  uint32_t generation = EEPROM_ReadWord(EEPROM_PageAddresses[EEPROM_ActivePage] + 4U) + 1U;

  /* The old page stays valid until every live variable has been copied. */
  if (EEPROM_ErasePage(page) != HAL_OK || EEPROM_StartPage(page, generation) != HAL_OK)
  {
    return HAL_ERROR;
  }

  for (uint32_t index = 0U; index < EEPROM_VARIABLE_COUNT; ++index)
  {
    if (EEPROM_Present[index] != 0U && index != virtualAddress)
    {
      if (EEPROM_AppendRecord(address, (uint16_t)index, EEPROM_Values[index]) != HAL_OK)
      {
        return HAL_ERROR;
      }
      address += EEPROM_RECORD_SIZE;
    }
  }

  if (EEPROM_AppendRecord(address, virtualAddress, value) != HAL_OK ||
      EEPROM_ProgramWord(base + 12U, EEPROM_COMMITTED) != HAL_OK)
  {
    return HAL_ERROR;
  }

  EEPROM_ActivePage = page;
  EEPROM_NextAddress = address + EEPROM_RECORD_SIZE;
  return HAL_OK;
}

static HAL_StatusTypeDef EEPROM_UnlockFlash(void)
{
  HAL_StatusTypeDef status = HAL_FLASH_Unlock();

  if (status == HAL_OK)
  {
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                          FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
  }

  return status;
}

EEPROM_StatusTypeDef EEPROM_Init(void)
{
  EEPROM_Initialized = 0U;

  // Check firmware/RAM data don't overlap
  uintptr_t imageEnd = (uintptr_t)&_sidata + ((uintptr_t)&_edata - (uintptr_t)&_sdata);
  uintptr_t ccmEnd = (uintptr_t)&_siccmram + ((uintptr_t)&_eccmram - (uintptr_t)&_sccmram);
  if (imageEnd > EEPROM_PAGE_0_ADDRESS || ccmEnd > EEPROM_PAGE_0_ADDRESS)
  {
    return EEPROM_FLASH_ERROR;
  }

  uint8_t valid0 = EEPROM_IsPageValid(0U);
  uint8_t valid1 = EEPROM_IsPageValid(1U);

  if (valid0 != 0U && valid1 != 0U)
  {
    uint32_t generation0 = EEPROM_ReadWord(EEPROM_PAGE_0_ADDRESS + 4U);
    uint32_t generation1 = EEPROM_ReadWord(EEPROM_PAGE_1_ADDRESS + 4U);
    uint32_t difference = generation1 - generation0;

    if (difference == 0U || difference == 0x80000000U)
    {
      return EEPROM_FLASH_ERROR;
    }
    EEPROM_ActivePage = (difference < 0x80000000U) ? 1U : 0U;
  }
  else if (valid0 != 0U || valid1 != 0U)
  {
    EEPROM_ActivePage = (valid0 != 0U) ? 0U : 1U;
  }
  else
  {
    /* Also recover a reset during the very first page-header write. */
    for (uint32_t page = 0U; page < 2U; ++page)
    {
      uint32_t base = EEPROM_PageAddresses[page];
      uint32_t magic = EEPROM_ReadWord(base);

      // If we accidentally write into magic program is dead
      // TODO: have fallback
      if ((magic & EEPROM_MAGIC) != EEPROM_MAGIC ||
          EEPROM_ReadWord(base + 12U) == EEPROM_COMMITTED ||
          EEPROM_IsErased(base + EEPROM_HEADER_SIZE, base + EEPROM_PAGE_SIZE) == 0U)
      {
        return EEPROM_FLASH_ERROR;
      }
    }

    if (EEPROM_UnlockFlash() != HAL_OK)
    {
      return EEPROM_FLASH_ERROR;
    }

    HAL_StatusTypeDef status = HAL_OK;
    if (EEPROM_IsErased(EEPROM_PAGE_0_ADDRESS,
                        EEPROM_PAGE_0_ADDRESS + EEPROM_PAGE_SIZE) == 0U)
    {
      status = EEPROM_ErasePage(0U);
    }
    if (status == HAL_OK)
    {
      status = EEPROM_StartPage(0U, 0U);
    }
    if (status == HAL_OK)
    {
      status = EEPROM_ProgramWord(EEPROM_PAGE_0_ADDRESS + 12U, EEPROM_COMMITTED);
    }
    HAL_StatusTypeDef lockStatus = HAL_FLASH_Lock();
    if (status != HAL_OK || lockStatus != HAL_OK)
    {
      return EEPROM_FLASH_ERROR;
    }
    EEPROM_ActivePage = 0U;
  }

  EEPROM_LoadPage();
  EEPROM_Initialized = 1U;
  return EEPROM_OK;
}

EEPROM_StatusTypeDef EEPROM_ReadVariable(uint16_t virtualAddress, uint32_t *value)
{
  if (virtualAddress >= EEPROM_VARIABLE_COUNT || value == NULL)
  {
    return EEPROM_INVALID_ARGUMENT;
  }
  if (EEPROM_Initialized == 0U)
  {
    return EEPROM_NOT_INITIALIZED;
  }
  if (EEPROM_Present[virtualAddress] == 0U)
  {
    return EEPROM_NOT_FOUND;
  }

  *value = EEPROM_Values[virtualAddress];
  return EEPROM_OK;
}

EEPROM_StatusTypeDef EEPROM_WriteVariable(uint16_t virtualAddress, uint32_t value)
{
  if (virtualAddress >= EEPROM_VARIABLE_COUNT)
  {
    return EEPROM_INVALID_ARGUMENT;
  }
  if (EEPROM_Initialized == 0U)
  {
    return EEPROM_NOT_INITIALIZED;
  }
  if (EEPROM_Present[virtualAddress] != 0U && EEPROM_Values[virtualAddress] == value)
  {
    return EEPROM_OK;
  }
  if (EEPROM_UnlockFlash() != HAL_OK)
  {
    EEPROM_Initialized = 0U;
    return EEPROM_FLASH_ERROR;
  }

  HAL_StatusTypeDef status;
  uint32_t end = EEPROM_PageAddresses[EEPROM_ActivePage] + EEPROM_PAGE_SIZE;
  if (EEPROM_NextAddress + EEPROM_RECORD_SIZE <= end)
  {
    status = EEPROM_AppendRecord(EEPROM_NextAddress, virtualAddress, value);
    EEPROM_NextAddress += EEPROM_RECORD_SIZE;
  }
  else
  {
    status = EEPROM_TransferPage(virtualAddress, value);
  }

  HAL_StatusTypeDef lockStatus = HAL_FLASH_Lock();
  if (status != HAL_OK || lockStatus != HAL_OK)
  {
    EEPROM_Initialized = 0U;
    return EEPROM_FLASH_ERROR;
  }

  EEPROM_Values[virtualAddress] = value;
  EEPROM_Present[virtualAddress] = 1U;
  return EEPROM_OK;
}
