#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "firmware_update.h"
#include "hardware/flash.h"
#include "hardware/structs/scb.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#ifndef APP_IMAGE_BASE
#define APP_IMAGE_BASE 0x10020000u
#endif

#define APP_SRAM_BASE 0x20000000u
#define APP_SRAM_END  0x20042000u
#define APP_XIP_BASE  0x10000000u
#define APP_IMAGE_SIZE (896u * 1024u)
#define APP_COPY_BASE  0x10100000u
#define UPDATE_METADATA_BASE 0x1001f000u
#define UPDATE_METADATA_SIZE FLASH_SECTOR_SIZE

#define CRC32_POLY 0xedb88320u

typedef void (*app_entry_t)(void);

static uint8_t sector_buffer[FLASH_SECTOR_SIZE];

static uint32_t bootloader_flash_offset(uint32_t xip_address)
{
    return xip_address - APP_XIP_BASE;
}

static uint32_t bootloader_round_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t bootloader_crc32_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (CRC32_POLY & mask);
        }
    }

    return crc;
}

static uint32_t bootloader_crc32(const uint8_t *data, uint32_t size)
{
    return bootloader_crc32_update(0xffffffffu, data, size) ^ 0xffffffffu;
}

static bool bootloader_app_vector_is_valid(uint32_t stack_pointer,
                                           uint32_t reset_handler,
                                           uint32_t expected_image_base,
                                           uint32_t max_image_size)
{
    if (stack_pointer < APP_SRAM_BASE || stack_pointer > APP_SRAM_END || (stack_pointer & 0x3u) != 0) {
        return false;
    }

    if ((reset_handler & 0x1u) == 0) {
        return false;
    }

    uint32_t reset_address = reset_handler & ~0x1u;
    if (reset_address < expected_image_base || reset_address >= (expected_image_base + max_image_size)) {
        return false;
    }

    return true;
}

static bool bootloader_image_vectors_are_valid(uint32_t image_base,
                                               uint32_t expected_image_base,
                                               uint32_t image_size)
{
    if (image_size < 8u || image_size > APP_IMAGE_SIZE) {
        return false;
    }

    const uint32_t *vectors = (const uint32_t *)image_base;
    return bootloader_app_vector_is_valid(vectors[0], vectors[1], expected_image_base, image_size);
}

static bool bootloader_manifest_is_blank(const firmware_update_manifest_t *manifest)
{
    const uint8_t *data = (const uint8_t *)manifest;

    for (uint32_t i = 0; i < UPDATE_METADATA_SIZE; ++i) {
        if (data[i] != 0xffu && data[i] != 0x00u) {
            return false;
        }
    }

    return true;
}

static bool bootloader_manifest_is_valid(const firmware_update_manifest_t *manifest)
{
    if (bootloader_manifest_is_blank(manifest)) {
        return false;
    }

    if (manifest->magic != FIRMWARE_UPDATE_MAGIC ||
        manifest->version != FIRMWARE_UPDATE_MANIFEST_VERSION ||
        manifest->header_size != sizeof(firmware_update_manifest_t) ||
        (manifest->flags & FIRMWARE_UPDATE_FLAG_PENDING) == 0u ||
        manifest->image_size == 0u ||
        manifest->image_size > APP_IMAGE_SIZE) {
        return false;
    }

    uint32_t header_crc = bootloader_crc32((const uint8_t *)manifest,
                                           offsetof(firmware_update_manifest_t, header_crc32));
    return header_crc == manifest->header_crc32;
}

static bool bootloader_staged_image_is_valid(const firmware_update_manifest_t *manifest)
{
    if (!bootloader_image_vectors_are_valid(APP_COPY_BASE, APP_IMAGE_BASE, manifest->image_size)) {
        return false;
    }

    uint32_t image_crc = bootloader_crc32((const uint8_t *)APP_COPY_BASE, manifest->image_size);
    return image_crc == manifest->image_crc32;
}

static void bootloader_copy_staged_image(uint32_t image_size)
{
    uint32_t copy_size = bootloader_round_up(image_size, FLASH_SECTOR_SIZE);

    for (uint32_t offset = 0; offset < copy_size; offset += FLASH_SECTOR_SIZE) {
        memcpy(sector_buffer, (const void *)(APP_COPY_BASE + offset), FLASH_SECTOR_SIZE);
        flash_range_erase(bootloader_flash_offset(APP_IMAGE_BASE + offset), FLASH_SECTOR_SIZE);

        for (uint32_t page_offset = 0; page_offset < FLASH_SECTOR_SIZE; page_offset += FLASH_PAGE_SIZE) {
            flash_range_program(bootloader_flash_offset(APP_IMAGE_BASE + offset + page_offset),
                                sector_buffer + page_offset,
                                FLASH_PAGE_SIZE);
        }
    }
}

static void bootloader_clear_update_metadata(void)
{
    flash_range_erase(bootloader_flash_offset(UPDATE_METADATA_BASE), UPDATE_METADATA_SIZE);
}

static void __attribute__((noreturn)) bootloader_reboot(void)
{
    watchdog_reboot(0, 0, 0);

    while (true) {
        tight_loop_contents();
    }
}

static void bootloader_try_apply_update(void)
{
    const firmware_update_manifest_t *manifest = (const firmware_update_manifest_t *)UPDATE_METADATA_BASE;

    if (!bootloader_manifest_is_valid(manifest)) {
        return;
    }

    if (!bootloader_staged_image_is_valid(manifest)) {
        return;
    }

    uint32_t image_size = manifest->image_size;
    uint32_t image_crc32 = manifest->image_crc32;

    __asm volatile("cpsid i");
    bootloader_copy_staged_image(image_size);

    if (bootloader_crc32((const uint8_t *)APP_IMAGE_BASE, image_size) != image_crc32) {
        bootloader_reboot();
    }

    bootloader_clear_update_metadata();
    bootloader_reboot();
}

static void __attribute__((noreturn)) bootloader_jump_to_app(uint32_t app_base)
{
    const uint32_t *vectors = (const uint32_t *)app_base;
    uint32_t stack_pointer = vectors[0];
    uint32_t reset_handler = vectors[1];

    if (!bootloader_app_vector_is_valid(stack_pointer, reset_handler, app_base, APP_IMAGE_SIZE)) {
        reset_usb_boot(0, 0);
    }

    __asm volatile("cpsid i");
    scb_hw->vtor = app_base;
    __asm volatile("dsb");
    __asm volatile("isb");
    __asm volatile("msr msp, %0" : : "r"(stack_pointer) : );
    __asm volatile("cpsie i");
    ((app_entry_t)reset_handler)();

    while (true) {
        tight_loop_contents();
    }
}

int main(void)
{
    bootloader_try_apply_update();
    bootloader_jump_to_app(APP_IMAGE_BASE);
}
