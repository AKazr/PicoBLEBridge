#include <stdint.h>
#include <stdbool.h>

#include "hardware/structs/scb.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#ifndef APP_IMAGE_BASE
#define APP_IMAGE_BASE 0x10020000u
#endif

#define APP_SRAM_BASE 0x20000000u
#define APP_SRAM_END  0x20042000u
#define APP_XIP_BASE  0x10000000u
#define APP_XIP_END   0x10200000u

typedef void (*app_entry_t)(void);

static bool bootloader_app_vector_is_valid(uint32_t stack_pointer, uint32_t reset_handler)
{
    if (stack_pointer < APP_SRAM_BASE || stack_pointer > APP_SRAM_END || (stack_pointer & 0x3u) != 0) {
        return false;
    }

    if (reset_handler < APP_XIP_BASE || reset_handler >= APP_XIP_END || (reset_handler & 0x1u) == 0) {
        return false;
    }

    return true;
}

static void __attribute__((noreturn)) bootloader_jump_to_app(uint32_t app_base)
{
    const uint32_t *vectors = (const uint32_t *)app_base;
    uint32_t stack_pointer = vectors[0];
    uint32_t reset_handler = vectors[1];

    if (!bootloader_app_vector_is_valid(stack_pointer, reset_handler)) {
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
    bootloader_jump_to_app(APP_IMAGE_BASE);
}
