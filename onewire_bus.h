#ifndef ONEWIRE_BUS_H
#define ONEWIRE_BUS_H

#include <stdbool.h>
#include <stdint.h>

#define ONEWIRE_ROM_SIZE 8

void onewire_bus_init(uint8_t gpio);
uint8_t onewire_bus_search(uint8_t gpio, uint8_t (*roms)[ONEWIRE_ROM_SIZE], uint8_t capacity);
bool onewire_bus_start_conversion(uint8_t gpio);
bool onewire_bus_read_temperature(uint8_t gpio, const uint8_t rom[ONEWIRE_ROM_SIZE], float *temperature_c);
uint32_t onewire_bus_device_id(const uint8_t rom[ONEWIRE_ROM_SIZE]);

#endif
