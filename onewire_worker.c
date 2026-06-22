#include <string.h>

#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "onewire_worker.h"

#define ONEWIRE_GPIO2 2u
#define ONEWIRE_GPIO3 3u
#define ONEWIRE_WORKER_DEVICE_LIMIT 16
#define ONEWIRE_QUEUE_SLOT_COUNT 4
#define ONEWIRE_CONVERT_DELAY_MS 750u
#define ONEWIRE_WORKER_INIT_TIMEOUT_MS 1000u

typedef struct {
    onewire_command_type_t type;
    uint8_t gpio;
} onewire_command_t;

typedef struct {
    onewire_command_t entries[ONEWIRE_QUEUE_SLOT_COUNT];
    volatile uint8_t read_index;
    volatile uint8_t write_index;
} onewire_command_queue_t;

typedef struct {
    onewire_response_t entries[ONEWIRE_QUEUE_SLOT_COUNT];
    volatile uint8_t read_index;
    volatile uint8_t write_index;
} onewire_response_queue_t;

static onewire_command_queue_t onewire_command_queue;
static onewire_response_queue_t onewire_response_queue;
static volatile bool onewire_worker_initialized;
static volatile bool onewire_worker_init_ok;
static uint8_t onewire_worker_gpio;
static uint8_t onewire_worker_roms[ONEWIRE_WORKER_DEVICE_LIMIT][ONEWIRE_ROM_SIZE];
static uint8_t onewire_worker_device_count;
static uint8_t onewire_worker_device_index;

static uint8_t onewire_queue_next_index(uint8_t index)
{
    return (uint8_t)((index + 1u) % ONEWIRE_QUEUE_SLOT_COUNT);
}

static bool onewire_command_queue_try_add(const onewire_command_t *command)
{
    uint8_t write_index = onewire_command_queue.write_index;
    uint8_t next_index = onewire_queue_next_index(write_index);

    if (next_index == onewire_command_queue.read_index) {
        return false;
    }
    onewire_command_queue.entries[write_index] = *command;
    __dmb();
    onewire_command_queue.write_index = next_index;
    __sev();
    return true;
}

static bool onewire_command_queue_try_remove(onewire_command_t *command)
{
    uint8_t read_index = onewire_command_queue.read_index;

    if (read_index == onewire_command_queue.write_index) {
        return false;
    }
    __dmb();
    *command = onewire_command_queue.entries[read_index];
    onewire_command_queue.read_index = onewire_queue_next_index(read_index);
    return true;
}

static bool onewire_response_queue_try_add(const onewire_response_t *response)
{
    uint8_t write_index = onewire_response_queue.write_index;
    uint8_t next_index = onewire_queue_next_index(write_index);

    if (next_index == onewire_response_queue.read_index) {
        return false;
    }
    onewire_response_queue.entries[write_index] = *response;
    __dmb();
    onewire_response_queue.write_index = next_index;
    __sev();
    return true;
}

bool onewire_worker_try_get_response(onewire_response_t *response)
{
    uint8_t read_index = onewire_response_queue.read_index;

    if (read_index == onewire_response_queue.write_index) {
        return false;
    }
    __dmb();
    *response = onewire_response_queue.entries[read_index];
    onewire_response_queue.read_index = onewire_queue_next_index(read_index);
    return true;
}

static void onewire_worker_handle_start(const onewire_command_t *command, onewire_response_t *response)
{
    if (command->gpio != ONEWIRE_GPIO2 && command->gpio != ONEWIRE_GPIO3) {
        response->type = ONEWIRE_START_ERROR;
        return;
    }

    onewire_worker_gpio = command->gpio;
    onewire_worker_device_index = 0;
    onewire_worker_device_count = onewire_bus_search(command->gpio,
                                                      onewire_worker_roms,
                                                      ONEWIRE_WORKER_DEVICE_LIMIT);
    if (onewire_worker_device_count > 0) {
        if (!onewire_bus_start_conversion(command->gpio)) {
            onewire_worker_device_count = 0;
            response->type = ONEWIRE_START_ERROR;
            return;
        }
        sleep_ms(ONEWIRE_CONVERT_DELAY_MS);
    }
    response->type = ONEWIRE_START_OK;
}

static void onewire_worker_handle_readnext(onewire_response_t *response)
{
    onewire_temperature_event_t *event = &response->temperature;
    const uint8_t *rom;

    if (onewire_worker_device_index >= onewire_worker_device_count) {
        response->type = ONEWIRE_READNEXT_NONE;
        return;
    }

    rom = onewire_worker_roms[onewire_worker_device_index++];
    memset(event, 0, sizeof(*event));
    event->gpio = onewire_worker_gpio;
    memcpy(event->rom, rom, sizeof(event->rom));
    event->device_id = onewire_bus_device_id(rom);
    event->valid = onewire_bus_read_temperature(onewire_worker_gpio, rom, &event->temperature_c);
    event->timestamp_ms = to_ms_since_boot(get_absolute_time());
    response->type = ONEWIRE_TEMPERATURE;
}

static void onewire_worker_main(void)
{
    onewire_command_t command;
    onewire_response_t response;

    onewire_bus_init(ONEWIRE_GPIO2);
    onewire_bus_init(ONEWIRE_GPIO3);
    onewire_worker_init_ok = flash_safe_execute_core_init();
    onewire_worker_initialized = true;

    while (true) {
        while (!onewire_command_queue_try_remove(&command)) {
            __wfe();
        }
        memset(&response, 0, sizeof(response));

        switch (command.type) {
        case ONEWIRE_START:
            onewire_worker_handle_start(&command, &response);
            break;
        case ONEWIRE_READNEXT:
            onewire_worker_handle_readnext(&response);
            break;
        default:
            response.type = ONEWIRE_START_ERROR;
            break;
        }

        while (!onewire_response_queue_try_add(&response)) {
            __wfe();
        }
    }
}

static bool onewire_worker_send(onewire_command_type_t type, uint8_t gpio)
{
    onewire_command_t command = {
        .type = type,
        .gpio = gpio,
    };
    return onewire_command_queue_try_add(&command);
}

bool onewire_worker_init(void)
{
    absolute_time_t deadline;

    memset(&onewire_command_queue, 0, sizeof(onewire_command_queue));
    memset(&onewire_response_queue, 0, sizeof(onewire_response_queue));
    onewire_worker_initialized = false;
    onewire_worker_init_ok = false;

    multicore_launch_core1(onewire_worker_main);
    deadline = make_timeout_time_ms(ONEWIRE_WORKER_INIT_TIMEOUT_MS);
    while (!onewire_worker_initialized && !time_reached(deadline)) {
        sleep_us(10);
    }
    return onewire_worker_initialized && onewire_worker_init_ok;
}

bool onewire_worker_send_start(uint8_t gpio)
{
    return onewire_worker_send(ONEWIRE_START, gpio);
}

bool onewire_worker_send_readnext(void)
{
    return onewire_worker_send(ONEWIRE_READNEXT, 0);
}
