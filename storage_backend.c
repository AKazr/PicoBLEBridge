#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include "storage_backend.h"

extern const uint8_t __storage_start__;
extern const uint8_t __storage_end__;

static uint8_t sector_buffer[FLASH_SECTOR_SIZE];

typedef struct {
    uint32_t flash_offset;
} storage_backend_flash_operation_t;

static void storage_backend_program_sector(void *context)
{
    const storage_backend_flash_operation_t *operation = context;

    flash_range_erase(operation->flash_offset, FLASH_SECTOR_SIZE);
    for (uint32_t page_offset = 0; page_offset < FLASH_SECTOR_SIZE; page_offset += FLASH_PAGE_SIZE) {
        flash_range_program(operation->flash_offset + page_offset,
                            sector_buffer + page_offset,
                            FLASH_PAGE_SIZE);
    }
}

static uint32_t storage_backend_offset_bytes(void)
{
    return (uint32_t)((uintptr_t)&__storage_start__ - XIP_BASE);
}

uint32_t storage_backend_size_bytes(void)
{
    return (uint32_t)(&__storage_end__ - &__storage_start__);
}

uint32_t storage_backend_block_count(void)
{
    return storage_backend_size_bytes() / STORAGE_BLOCK_SIZE;
}

const uint8_t *storage_backend_xip_base(void)
{
    return (const uint8_t *)(XIP_BASE + storage_backend_offset_bytes());
}

static bool storage_backend_bounds_check(uint32_t byte_offset, uint32_t size)
{
    uint32_t total_size = storage_backend_size_bytes();

    if (byte_offset > total_size) {
        return false;
    }

    if (size > (total_size - byte_offset)) {
        return false;
    }

    return true;
}

bool storage_backend_read(uint32_t byte_offset, void *buffer, uint32_t size)
{
    if (!storage_backend_bounds_check(byte_offset, size)) {
        return false;
    }

    memcpy(buffer, storage_backend_xip_base() + byte_offset, size);
    return true;
}

bool storage_backend_write(uint32_t byte_offset, const void *buffer, uint32_t size)
{
    const uint8_t *data = (const uint8_t *)buffer;
    uint32_t flash_offset = storage_backend_offset_bytes();

    if (!storage_backend_bounds_check(byte_offset, size)) {
        return false;
    }

    while (size > 0) {
        uint32_t sector_index = byte_offset / FLASH_SECTOR_SIZE;
        uint32_t sector_offset = byte_offset % FLASH_SECTOR_SIZE;
        uint32_t sector_flash_offset = flash_offset + sector_index * FLASH_SECTOR_SIZE;
        uint32_t chunk_size = FLASH_SECTOR_SIZE - sector_offset;
        const uint8_t *sector_xip = (const uint8_t *)(XIP_BASE + sector_flash_offset);

        if (chunk_size > size) {
            chunk_size = size;
        }

        memcpy(sector_buffer, sector_xip, FLASH_SECTOR_SIZE);
        memcpy(sector_buffer + sector_offset, data, chunk_size);

        storage_backend_flash_operation_t operation = {
            .flash_offset = sector_flash_offset,
        };

        if (flash_safe_execute(storage_backend_program_sector, &operation, UINT32_MAX) != PICO_OK) {
            return false;
        }

        byte_offset += chunk_size;
        data += chunk_size;
        size -= chunk_size;
    }

    return true;
}
