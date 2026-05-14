#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"

#include "app_firmware_update.h"
#include "app_log.h"
#include "firmware_update.h"

#define APP_FIRMWARE_UPDATE_HEADER_SIZE FLASH_SECTOR_SIZE
#define APP_FIRMWARE_UPDATE_CRC32_POLY 0xedb88320u

extern const uint8_t __update_metadata_start__;
extern const uint8_t __firmware_copy_start__;
extern const uint8_t __firmware_copy_end__;

typedef struct {
    bool active;
    bool header_valid;
    app_firmware_update_result_t result;
    uint32_t content_len;
    uint32_t received;
    uint32_t image_received;
    uint32_t image_size;
    uint32_t page_fill;
    uint32_t erased_up_to;
    uint8_t header[APP_FIRMWARE_UPDATE_HEADER_SIZE];
    uint8_t page[FLASH_PAGE_SIZE];
} app_firmware_update_state_t;

static app_firmware_update_state_t app_firmware_update;

static uint32_t app_firmware_update_flash_offset(const uint8_t *xip_address)
{
    return (uint32_t)((uintptr_t)xip_address - XIP_BASE);
}

static uint32_t app_firmware_update_image_max_size(void)
{
    return (uint32_t)((uintptr_t)&__firmware_copy_end__ - (uintptr_t)&__firmware_copy_start__);
}

static uint32_t app_firmware_update_crc32_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (APP_FIRMWARE_UPDATE_CRC32_POLY & mask);
        }
    }

    return crc;
}

static uint32_t app_firmware_update_crc32(const uint8_t *data, uint32_t size)
{
    return app_firmware_update_crc32_update(0xffffffffu, data, size) ^ 0xffffffffu;
}

static bool app_firmware_update_manifest_is_valid(const uint8_t *header, uint32_t content_len,
                                                  firmware_update_manifest_t *manifest)
{
    uint32_t header_crc;

    if (content_len < APP_FIRMWARE_UPDATE_HEADER_SIZE) {
        return false;
    }

    memcpy(manifest, header, sizeof(*manifest));

    if (manifest->magic != FIRMWARE_UPDATE_MAGIC ||
        manifest->version != FIRMWARE_UPDATE_MANIFEST_VERSION ||
        manifest->header_size != sizeof(firmware_update_manifest_t) ||
        (manifest->flags & FIRMWARE_UPDATE_FLAG_PENDING) == 0u ||
        manifest->image_size == 0u ||
        manifest->image_size > app_firmware_update_image_max_size() ||
        content_len != (APP_FIRMWARE_UPDATE_HEADER_SIZE + manifest->image_size)) {
        return false;
    }

    header_crc = app_firmware_update_crc32(header, offsetof(firmware_update_manifest_t, header_crc32));
    return header_crc == manifest->header_crc32;
}

static void app_firmware_update_fail(app_firmware_update_result_t result)
{
    if (app_firmware_update.result == result) {
        return;
    }

    app_firmware_update.result = result;
    app_log("OTA update failed: %s", app_firmware_update_result_text(result));
}

static void app_firmware_update_flash_program_page(uint32_t flash_offset, const uint8_t *page)
{
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(flash_offset, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

static void app_firmware_update_flash_erase_sector(uint32_t flash_offset)
{
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}

static bool app_firmware_update_write_image_data(const uint8_t *data, uint32_t size)
{
    uint32_t copy_start = app_firmware_update_flash_offset(&__firmware_copy_start__);

    while (size > 0) {
        uint32_t space = FLASH_PAGE_SIZE - app_firmware_update.page_fill;
        uint32_t chunk = size < space ? size : space;

        memcpy(app_firmware_update.page + app_firmware_update.page_fill, data, chunk);
        app_firmware_update.page_fill += chunk;
        app_firmware_update.image_received += chunk;
        data += chunk;
        size -= chunk;

        if (app_firmware_update.image_received > app_firmware_update.image_size) {
            app_firmware_update_fail(APP_FIRMWARE_UPDATE_IMAGE_TOO_LARGE);
            return false;
        }

        if (app_firmware_update.page_fill == FLASH_PAGE_SIZE) {
            uint32_t page_offset = app_firmware_update.image_received - FLASH_PAGE_SIZE;
            while (app_firmware_update.erased_up_to <= page_offset) {
                app_firmware_update_flash_erase_sector(copy_start + app_firmware_update.erased_up_to);
                app_firmware_update.erased_up_to += FLASH_SECTOR_SIZE;
            }
            app_firmware_update_flash_program_page(copy_start + page_offset, app_firmware_update.page);
            app_firmware_update.page_fill = 0;
            memset(app_firmware_update.page, 0xff, sizeof(app_firmware_update.page));
        }
    }

    return true;
}

static void app_firmware_update_finish_write(void)
{
    uint32_t metadata_start = app_firmware_update_flash_offset(&__update_metadata_start__);

    if (app_firmware_update.page_fill > 0) {
        uint32_t copy_start = app_firmware_update_flash_offset(&__firmware_copy_start__);
        uint32_t page_offset = app_firmware_update.image_received - app_firmware_update.page_fill;
        memset(app_firmware_update.page + app_firmware_update.page_fill, 0xff,
               sizeof(app_firmware_update.page) - app_firmware_update.page_fill);
        while (app_firmware_update.erased_up_to <= page_offset) {
            app_firmware_update_flash_erase_sector(copy_start + app_firmware_update.erased_up_to);
            app_firmware_update.erased_up_to += FLASH_SECTOR_SIZE;
        }
        app_firmware_update_flash_program_page(copy_start + page_offset, app_firmware_update.page);
        app_firmware_update.page_fill = 0;
    }

    app_firmware_update_flash_erase_sector(metadata_start);
    for (uint32_t offset = 0; offset < APP_FIRMWARE_UPDATE_HEADER_SIZE; offset += FLASH_PAGE_SIZE) {
        app_firmware_update_flash_program_page(metadata_start + offset, app_firmware_update.header + offset);
    }
}

app_firmware_update_result_t app_firmware_update_begin(uint32_t content_len)
{
    memset(&app_firmware_update, 0, sizeof(app_firmware_update));
    app_firmware_update.result = APP_FIRMWARE_UPDATE_OK;

    if (content_len < APP_FIRMWARE_UPDATE_HEADER_SIZE ||
        content_len > (APP_FIRMWARE_UPDATE_HEADER_SIZE + app_firmware_update_image_max_size())) {
        app_firmware_update_fail(APP_FIRMWARE_UPDATE_INVALID_SIZE);
        return app_firmware_update.result;
    }

    app_firmware_update.active = true;
    app_firmware_update.content_len = content_len;
    app_log("OTA update upload started: %lu bytes", (unsigned long)content_len);
    return APP_FIRMWARE_UPDATE_OK;
}

app_firmware_update_result_t app_firmware_update_write(const uint8_t *data, uint32_t size)
{
    if (!app_firmware_update.active) {
        return APP_FIRMWARE_UPDATE_NOT_ACTIVE;
    }

    while (size > 0 && app_firmware_update.result == APP_FIRMWARE_UPDATE_OK) {
        if (app_firmware_update.received < APP_FIRMWARE_UPDATE_HEADER_SIZE) {
            uint32_t header_remaining = APP_FIRMWARE_UPDATE_HEADER_SIZE - app_firmware_update.received;
            uint32_t chunk = size < header_remaining ? size : header_remaining;

            memcpy(app_firmware_update.header + app_firmware_update.received, data, chunk);
            app_firmware_update.received += chunk;
            data += chunk;
            size -= chunk;

            if (app_firmware_update.received == APP_FIRMWARE_UPDATE_HEADER_SIZE) {
                firmware_update_manifest_t manifest;
                if (!app_firmware_update_manifest_is_valid(app_firmware_update.header,
                                                           app_firmware_update.content_len, &manifest)) {
                    app_firmware_update_fail(APP_FIRMWARE_UPDATE_INVALID_HEADER);
                    return app_firmware_update.result;
                }
                app_firmware_update.header_valid = true;
                app_firmware_update.image_size = manifest.image_size;
                memset(app_firmware_update.page, 0xff, sizeof(app_firmware_update.page));
            }

            continue;
        }

        app_firmware_update.received += size;
        app_firmware_update_write_image_data(data, size);
        return app_firmware_update.result;
    }

    return app_firmware_update.result;
}

app_firmware_update_result_t app_firmware_update_finish(void)
{
    if (!app_firmware_update.active) {
        return APP_FIRMWARE_UPDATE_NOT_ACTIVE;
    }

    if (app_firmware_update.result == APP_FIRMWARE_UPDATE_OK) {
        if (!app_firmware_update.header_valid ||
            app_firmware_update.received != app_firmware_update.content_len ||
            app_firmware_update.image_received != app_firmware_update.image_size) {
            app_firmware_update_fail(APP_FIRMWARE_UPDATE_INCOMPLETE);
        } else {
            app_firmware_update_finish_write();
            app_log("OTA update uploaded: image=%lu bytes", (unsigned long)app_firmware_update.image_size);
        }
    }

    app_firmware_update.active = false;
    return app_firmware_update.result;
}

void app_firmware_update_abort(app_firmware_update_result_t result)
{
    if (result == APP_FIRMWARE_UPDATE_OK) {
        result = APP_FIRMWARE_UPDATE_INCOMPLETE;
    }

    app_firmware_update_fail(result);
    app_firmware_update.active = false;
}

bool app_firmware_update_is_active(void)
{
    return app_firmware_update.active;
}

uint32_t app_firmware_update_received(void)
{
    return app_firmware_update.received;
}

uint32_t app_firmware_update_total_size(void)
{
    return app_firmware_update.content_len;
}

const char *app_firmware_update_result_text(app_firmware_update_result_t result)
{
    switch (result) {
    case APP_FIRMWARE_UPDATE_OK:
        return "Update uploaded. Reboot to apply firmware.";
    case APP_FIRMWARE_UPDATE_INVALID_SIZE:
        return "Update failed: invalid OTA file size";
    case APP_FIRMWARE_UPDATE_INVALID_HEADER:
        return "Update failed: invalid OTA header";
    case APP_FIRMWARE_UPDATE_IMAGE_TOO_LARGE:
        return "Update failed: OTA image is larger than manifest size";
    case APP_FIRMWARE_UPDATE_INCOMPLETE:
        return "Update failed: incomplete OTA upload";
    case APP_FIRMWARE_UPDATE_INVALID_OFFSET:
        return "Update failed: unexpected OTA chunk offset";
    case APP_FIRMWARE_UPDATE_NOT_ACTIVE:
    default:
        return "Update failed: OTA upload is not active";
    }
}
