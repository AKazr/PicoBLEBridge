#ifndef ONEWIRE_WORKER_H
#define ONEWIRE_WORKER_H

#include <stdbool.h>
#include <stdint.h>

#include "onewire_bus.h"

typedef enum {
    ONEWIRE_START = 0,
    ONEWIRE_READNEXT,
} onewire_command_type_t;

typedef enum {
    ONEWIRE_START_OK = 0,
    ONEWIRE_START_ERROR,
    ONEWIRE_TEMPERATURE,
    ONEWIRE_READNEXT_NONE,
} onewire_response_type_t;

typedef struct {
    uint8_t gpio;
    uint8_t rom[ONEWIRE_ROM_SIZE];
    uint32_t device_id;
    uint32_t timestamp_ms;
    float temperature_c;
    bool valid;
} onewire_temperature_event_t;

typedef struct {
    onewire_response_type_t type;
    onewire_temperature_event_t temperature;
} onewire_response_t;

bool onewire_worker_init(void);
bool onewire_worker_send_start(uint8_t gpio);
bool onewire_worker_send_readnext(void);
bool onewire_worker_try_get_response(onewire_response_t *response);

#endif
