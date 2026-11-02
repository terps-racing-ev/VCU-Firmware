#include "stm32f4xx_hal.h"
#include "drivers/bootloader.h"

/* STM32F405RG flash layout: bootloader sectors 0-1, metadata sectors 2-3,
 * unused sector 4, slot A sectors 5-6, and slot B sectors 7-8.
 */
#define BOOTLOADER_FLASH_BASE                 0x08000000UL
#define BOOTLOADER_FLASH_END                  0x08100000UL
#define BOOTLOADER_CODE_BASE                  0x08000000UL
#define BOOTLOADER_CODE_END                   0x08008000UL
#define BOOTLOADER_METADATA_A_BASE            0x08008000UL
#define BOOTLOADER_METADATA_B_BASE            0x0800C000UL
#define BOOTLOADER_METADATA_SECTOR_SIZE       0x00004000UL
#define BOOTLOADER_METADATA_A_SECTOR          2U
#define BOOTLOADER_METADATA_B_SECTOR          3U

/* Image slot metadata remains in the persistent record for format compatibility. */
/* Applications placed in a slot must be linked to that slot's base address. */
#define BOOTLOADER_SLOT_A_BASE                0x08020000UL
#define BOOTLOADER_SLOT_A_END                 0x08060000UL
#define BOOTLOADER_SLOT_A_FIRST_SECTOR        5U
#define BOOTLOADER_SLOT_A_LAST_SECTOR         6U
#define BOOTLOADER_SLOT_A_SIZE                (BOOTLOADER_SLOT_A_END - BOOTLOADER_SLOT_A_BASE)
#define BOOTLOADER_SLOT_B_BASE                0x08060000UL
#define BOOTLOADER_SLOT_B_END                 0x080A0000UL
#define BOOTLOADER_SLOT_B_FIRST_SECTOR        7U
#define BOOTLOADER_SLOT_B_LAST_SECTOR         8U
#define BOOTLOADER_SLOT_B_SIZE                (BOOTLOADER_SLOT_B_END - BOOTLOADER_SLOT_B_BASE)
#define BOOTLOADER_MAX_IMAGE_SIZE             BOOTLOADER_SLOT_A_SIZE

#define BOOTLOADER_SLOT_COUNT                 2U
#define BOOTLOADER_SLOT_A                     0U
#define BOOTLOADER_SLOT_B                     1U
#define BOOTLOADER_SLOT_NONE                  UINT32_MAX

#define BOOTLOADER_METADATA_RECORDS_PER_SECTOR \
    (BOOTLOADER_METADATA_SECTOR_SIZE / BOOTLOADER_METADATA_RECORD_SIZE)

#define BOOTLOADER_METADATA_MAGIC             0x444D4C42UL
#define BOOTLOADER_METADATA_FORMAT_VERSION    1U
#define BOOTLOADER_METADATA_RECORD_SIZE       76U
#define BOOTLOADER_METADATA_WORD_COUNT        (BOOTLOADER_METADATA_RECORD_SIZE / 4U)
#define BOOTLOADER_METADATA_COMMITTED         0x00000000UL
#define BOOTLOADER_METADATA_ERASED            UINT32_MAX

#define BOOTLOADER_TRANSACTION_NOT_FLASHED    0U
#define BOOTLOADER_TRANSACTION_IN_PROGRESS    1U
#define BOOTLOADER_TRANSACTION_FLASHED        2U
#define BOOTLOADER_VALID_SLOT_A               (1UL << BOOTLOADER_SLOT_A)
#define BOOTLOADER_VALID_SLOT_B               (1UL << BOOTLOADER_SLOT_B)

#define BOOTLOADER_CRC32_POLYNOMIAL_REFLECTED 0xEDB88320UL
#define BOOTLOADER_CRC32_INITIAL              0xFFFFFFFFUL
#define BOOTLOADER_CRC32_FINAL_XOR            0xFFFFFFFFUL

#define BOOTLOADER_METADATA_CRC_LENGTH \
    offsetof(bootloader_metadata_t, record_crc32)

typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t record_size;
    uint32_t sequence;
    uint32_t board_id;
    uint32_t active_slot;
    uint32_t update_slot;
    uint32_t transaction_state;
    uint32_t valid_slots;
    uint32_t image_size[BOOTLOADER_SLOT_COUNT];
    uint32_t image_crc32[BOOTLOADER_SLOT_COUNT];
    uint32_t image_version[BOOTLOADER_SLOT_COUNT];
    uint32_t reserved[2];
    uint32_t record_crc32;
    uint32_t commit_word;
} bootloader_metadata_t;

static bool Bootloader_RecordValid(const bootloader_metadata_t*);
static uint32_t Bootloader_Crc32Update(uint32_t, const uint8_t*, size_t);
static uint32_t Bootloader_Crc32(const uint8_t*, size_t);

uint8_t Bootloader_ReadBoardId(void)
{
    const uintptr_t sector_bases[2] = {
        BOOTLOADER_METADATA_A_BASE,
        BOOTLOADER_METADATA_B_BASE
    };
    bool found = false;
    bootloader_metadata_t metadata;

    for (size_t sector = 0; sector < 2U; ++sector)
    {
        for (size_t index = 0; index < BOOTLOADER_METADATA_RECORDS_PER_SECTOR; ++index)
        {
            const bootloader_metadata_t *record =
                (const bootloader_metadata_t *)(sector_bases[sector] +
                                                 index * sizeof(bootloader_metadata_t));

            if (!Bootloader_RecordValid(record))
            {
                continue;
            }

            if (!found)
            {
                metadata = *record;
                found = true;
            }
            else
            {
                const uint32_t sequence_gap =
                    record->sequence - metadata.sequence;
                if (sequence_gap != 0U && sequence_gap < 0x80000000UL)
                {
                    metadata = *record;
                }
            }
        }
    }

    return found ? (uint8_t) metadata.board_id : BOOTLOADER_DEFAULT_BOARD_ID;
}

static bool Bootloader_RecordValid(const bootloader_metadata_t *record)
{
    if (record->magic != BOOTLOADER_METADATA_MAGIC ||
        record->format_version != BOOTLOADER_METADATA_FORMAT_VERSION ||
        record->record_size != sizeof(bootloader_metadata_t) ||
        record->commit_word != BOOTLOADER_METADATA_COMMITTED ||
        (record->valid_slots & ~(BOOTLOADER_VALID_SLOT_A |
                                 BOOTLOADER_VALID_SLOT_B)) != 0U ||
        (record->active_slot != BOOTLOADER_SLOT_NONE &&
         record->active_slot >= BOOTLOADER_SLOT_COUNT) ||
        (record->update_slot != BOOTLOADER_SLOT_NONE &&
         record->update_slot >= BOOTLOADER_SLOT_COUNT) ||
        record->transaction_state > BOOTLOADER_TRANSACTION_FLASHED)
    {
        return false;
    }

    if (record->active_slot != BOOTLOADER_SLOT_NONE &&
        (record->valid_slots & (1UL << record->active_slot)) == 0U)
    {
        return false;
    }

    return Bootloader_Crc32((const uint8_t *)record,
                            BOOTLOADER_METADATA_CRC_LENGTH) == record->record_crc32;
}

static uint32_t Bootloader_Crc32(const uint8_t *data, size_t length)
{
    return Bootloader_Crc32Update(BOOTLOADER_CRC32_INITIAL, data, length) ^
           BOOTLOADER_CRC32_FINAL_XOR;
}

static uint32_t Bootloader_Crc32Update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8U; ++bit)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U
                                    ? BOOTLOADER_CRC32_POLYNOMIAL_REFLECTED
                                    : 0U);
        }
    }

    return crc;
}

