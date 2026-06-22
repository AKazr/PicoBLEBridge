#include <stddef.h>
#include <string.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "onewire_bus.h"

#define DS18B20_FAMILY_CODE 0x28u
#define DS18B20_CMD_SEARCH_ROM 0xF0u
#define DS18B20_CMD_MATCH_ROM 0x55u
#define DS18B20_CMD_SKIP_ROM 0xCCu
#define DS18B20_CMD_CONVERT_T 0x44u
#define DS18B20_CMD_READ_SCRATCHPAD 0xBEu

static void onewire_drive_low(uint8_t gpio)
{
    gpio_put(gpio, 0);
    gpio_set_dir(gpio, GPIO_OUT);
}

static void onewire_release(uint8_t gpio)
{
    gpio_set_dir(gpio, GPIO_IN);
}

static bool onewire_reset(uint8_t gpio)
{
    bool present;

    onewire_drive_low(gpio);
    sleep_us(480);
    onewire_release(gpio);
    sleep_us(70);
    present = !gpio_get(gpio);
    sleep_us(410);
    return present;
}

static void onewire_write_bit(uint8_t gpio, bool bit)
{
    onewire_drive_low(gpio);
    if (bit) {
        sleep_us(6);
        onewire_release(gpio);
        sleep_us(64);
    } else {
        sleep_us(60);
        onewire_release(gpio);
        sleep_us(10);
    }
}

static bool onewire_read_bit(uint8_t gpio)
{
    bool bit;

    onewire_drive_low(gpio);
    sleep_us(6);
    onewire_release(gpio);
    sleep_us(9);
    bit = gpio_get(gpio);
    sleep_us(55);
    return bit;
}

static void onewire_write_byte(uint8_t gpio, uint8_t value)
{
    for (int i = 0; i < 8; ++i) {
        onewire_write_bit(gpio, (value & (1u << i)) != 0);
    }
}

static uint8_t onewire_read_byte(uint8_t gpio)
{
    uint8_t value = 0;

    for (int i = 0; i < 8; ++i) {
        if (onewire_read_bit(gpio)) {
            value |= (uint8_t)(1u << i);
        }
    }
    return value;
}

static uint8_t onewire_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;

    for (size_t i = 0; i < len; ++i) {
        uint8_t inbyte = data[i];

        for (int bit = 0; bit < 8; ++bit) {
            uint8_t mix = (crc ^ inbyte) & 0x01u;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8Cu;
            }
            inbyte >>= 1;
        }
    }
    return crc;
}

static bool onewire_search_next(uint8_t gpio, uint8_t rom[ONEWIRE_ROM_SIZE],
                                uint8_t *last_discrepancy, bool *last_device)
{
    uint8_t rom_byte_number = 0;
    uint8_t rom_byte_mask = 1;
    uint8_t id_bit_number = 1;
    uint8_t last_zero = 0;

    if (*last_device || !onewire_reset(gpio)) {
        return false;
    }

    onewire_write_byte(gpio, DS18B20_CMD_SEARCH_ROM);
    do {
        bool id_bit = onewire_read_bit(gpio);
        bool cmp_id_bit = onewire_read_bit(gpio);
        bool search_direction;

        if (id_bit && cmp_id_bit) {
            return false;
        }

        if (id_bit != cmp_id_bit) {
            search_direction = id_bit;
        } else {
            search_direction = id_bit_number < *last_discrepancy
                                   ? (rom[rom_byte_number] & rom_byte_mask) != 0
                                   : id_bit_number == *last_discrepancy;
            if (!search_direction) {
                last_zero = id_bit_number;
            }
        }

        if (search_direction) {
            rom[rom_byte_number] |= rom_byte_mask;
        } else {
            rom[rom_byte_number] &= (uint8_t)~rom_byte_mask;
        }
        onewire_write_bit(gpio, search_direction);

        ++id_bit_number;
        rom_byte_mask <<= 1;
        if (rom_byte_mask == 0) {
            ++rom_byte_number;
            rom_byte_mask = 1;
        }
    } while (rom_byte_number < ONEWIRE_ROM_SIZE);

    *last_discrepancy = last_zero;
    *last_device = last_zero == 0;
    return onewire_crc8(rom, ONEWIRE_ROM_SIZE) == 0 && rom[0] == DS18B20_FAMILY_CODE;
}

void onewire_bus_init(uint8_t gpio)
{
    gpio_init(gpio);
    gpio_put(gpio, 0);
    gpio_set_dir(gpio, GPIO_IN);
    gpio_pull_up(gpio);
}

uint8_t onewire_bus_search(uint8_t gpio, uint8_t (*roms)[ONEWIRE_ROM_SIZE], uint8_t capacity)
{
    uint8_t rom[ONEWIRE_ROM_SIZE] = {0};
    uint8_t last_discrepancy = 0;
    bool last_device = false;
    uint8_t count = 0;

    while (count < capacity && onewire_search_next(gpio, rom, &last_discrepancy, &last_device)) {
        memcpy(roms[count], rom, ONEWIRE_ROM_SIZE);
        ++count;
    }
    return count;
}

bool onewire_bus_start_conversion(uint8_t gpio)
{
    if (!onewire_reset(gpio)) {
        return false;
    }
    onewire_write_byte(gpio, DS18B20_CMD_SKIP_ROM);
    onewire_write_byte(gpio, DS18B20_CMD_CONVERT_T);
    return true;
}

bool onewire_bus_read_temperature(uint8_t gpio, const uint8_t rom[ONEWIRE_ROM_SIZE], float *temperature_c)
{
    uint8_t scratchpad[9];
    int16_t raw_temperature;

    if (!onewire_reset(gpio)) {
        return false;
    }

    onewire_write_byte(gpio, DS18B20_CMD_MATCH_ROM);
    for (int i = 0; i < ONEWIRE_ROM_SIZE; ++i) {
        onewire_write_byte(gpio, rom[i]);
    }
    onewire_write_byte(gpio, DS18B20_CMD_READ_SCRATCHPAD);
    for (int i = 0; i < 9; ++i) {
        scratchpad[i] = onewire_read_byte(gpio);
    }

    if (onewire_crc8(scratchpad, sizeof(scratchpad)) != 0) {
        return false;
    }
    raw_temperature = (int16_t)((uint16_t)scratchpad[0] | ((uint16_t)scratchpad[1] << 8));
    *temperature_c = (float)raw_temperature / 16.0f;
    return true;
}

uint32_t onewire_bus_device_id(const uint8_t rom[ONEWIRE_ROM_SIZE])
{
    return ((uint32_t)rom[3] << 24) |
           ((uint32_t)rom[4] << 16) |
           ((uint32_t)rom[5] << 8) |
           (uint32_t)rom[6];
}
