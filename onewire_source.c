#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "app_config.h"
#include "app_runtime_config.h"
#include "app_text.h"
#include "measurement_store.h"
#include "onewire_source.h"
#include "onewire_worker.h"

#define ONEWIRE_BUS_COUNT 2
#define ONEWIRE_DEVICE_TABLE_SIZE 32

typedef struct {
    bool in_use;
    bool present;
    bool valid_temperature;
    uint8_t gpio;
    uint8_t rom[ONEWIRE_ROM_SIZE];
    uint32_t device_id;
    float temperature_c;
    uint32_t last_seen_ms;
    uint32_t error_count;
} onewire_device_entry_t;

typedef enum {
    ONEWIRE_SOURCE_IDLE = 0,
    ONEWIRE_SOURCE_WAIT_START,
    ONEWIRE_SOURCE_WAIT_READNEXT,
} onewire_source_state_t;

static const uint8_t onewire_gpios[ONEWIRE_BUS_COUNT] = {2, 3};
static onewire_device_entry_t onewire_devices[ONEWIRE_DEVICE_TABLE_SIZE];
static bool onewire_bus_enabled[ONEWIRE_BUS_COUNT] = {true, true};
static uint32_t onewire_poll_interval_ms = APP_CONFIG_ONEWIRE_POLL_INTERVAL_DEFAULT_SECONDS * 1000u;
static uint32_t onewire_next_poll_ms;
static uint8_t onewire_current_bus;
static onewire_source_state_t onewire_source_state;

static void onewire_format_rom(char *buffer, size_t buffer_size, const uint8_t rom[ONEWIRE_ROM_SIZE])
{
    snprintf(buffer,
             buffer_size,
             "%02X%02X%02X%02X%02X%02X%02X%02X",
             rom[7], rom[6], rom[5], rom[4], rom[3], rom[2], rom[1], rom[0]);
}

static onewire_device_entry_t *onewire_find_or_alloc_device(const onewire_temperature_event_t *event)
{
    int free_index = -1;
    int oldest_index = 0;

    for (int i = 0; i < ONEWIRE_DEVICE_TABLE_SIZE; ++i) {
        if (onewire_devices[i].in_use &&
            onewire_devices[i].gpio == event->gpio &&
            memcmp(onewire_devices[i].rom, event->rom, sizeof(event->rom)) == 0) {
            return &onewire_devices[i];
        }
        if (!onewire_devices[i].in_use && free_index < 0) {
            free_index = i;
        } else if (onewire_devices[i].in_use &&
                   onewire_devices[i].last_seen_ms < onewire_devices[oldest_index].last_seen_ms) {
            oldest_index = i;
        }
    }

    onewire_device_entry_t *entry = &onewire_devices[free_index >= 0 ? free_index : oldest_index];
    memset(entry, 0, sizeof(*entry));
    entry->in_use = true;
    entry->gpio = event->gpio;
    memcpy(entry->rom, event->rom, sizeof(entry->rom));
    entry->device_id = event->device_id;
    return entry;
}

static void onewire_finish_cycle(uint32_t now_ms)
{
    onewire_source_state = ONEWIRE_SOURCE_IDLE;
    onewire_next_poll_ms = now_ms + onewire_poll_interval_ms;
}

static void onewire_start_next_bus(uint32_t now_ms)
{
    while (onewire_current_bus < ONEWIRE_BUS_COUNT && !onewire_bus_enabled[onewire_current_bus]) {
        ++onewire_current_bus;
    }

    if (onewire_current_bus >= ONEWIRE_BUS_COUNT) {
        onewire_finish_cycle(now_ms);
    } else if (onewire_worker_send_start(onewire_gpios[onewire_current_bus])) {
        onewire_source_state = ONEWIRE_SOURCE_WAIT_START;
    } else {
        onewire_source_state = ONEWIRE_SOURCE_IDLE;
        onewire_next_poll_ms = now_ms + 100u;
    }
}

static void onewire_finish_bus(uint32_t now_ms)
{
    ++onewire_current_bus;
    onewire_start_next_bus(now_ms);
}

static void onewire_handle_temperature(const onewire_temperature_event_t *event)
{
    onewire_device_entry_t *entry = onewire_find_or_alloc_device(event);

    entry->present = true;
    entry->last_seen_ms = event->timestamp_ms;
    entry->valid_temperature = event->valid;
    if (event->valid) {
        entry->temperature_c = event->temperature_c;
        measurement_store_add(event->device_id,
                              MEASUREMENT_FIELD_1WIRE_TEMPERATURE,
                              event->temperature_c,
                              event->timestamp_ms);
    } else {
        ++entry->error_count;
    }
}

static void onewire_handle_response(const onewire_response_t *response, uint32_t now_ms)
{
    if (onewire_source_state == ONEWIRE_SOURCE_WAIT_START) {
        if (response->type == ONEWIRE_START_OK && onewire_worker_send_readnext()) {
            onewire_source_state = ONEWIRE_SOURCE_WAIT_READNEXT;
        } else {
            onewire_finish_bus(now_ms);
        }
        return;
    }

    if (onewire_source_state != ONEWIRE_SOURCE_WAIT_READNEXT) {
        return;
    }

    if (response->type == ONEWIRE_TEMPERATURE) {
        onewire_handle_temperature(&response->temperature);
        if (!onewire_worker_send_readnext()) {
            onewire_finish_bus(now_ms);
        }
    } else {
        onewire_finish_bus(now_ms);
    }
}

bool onewire_source_init(void)
{
    memset(onewire_devices, 0, sizeof(onewire_devices));
    onewire_source_state = ONEWIRE_SOURCE_IDLE;
    onewire_next_poll_ms = UINT32_MAX;
    return onewire_worker_init();
}

void onewire_source_apply_config(const app_config_t *config)
{
    uint32_t interval_seconds = APP_CONFIG_ONEWIRE_POLL_INTERVAL_DEFAULT_SECONDS;

    if (config != NULL) {
        interval_seconds = config->onewire_poll_interval_seconds;
        onewire_bus_enabled[0] = config->onewire_gpio2_enabled;
        onewire_bus_enabled[1] = config->onewire_gpio3_enabled;
    }
    if (interval_seconds < APP_CONFIG_ONEWIRE_POLL_INTERVAL_MIN_SECONDS) {
        interval_seconds = APP_CONFIG_ONEWIRE_POLL_INTERVAL_MIN_SECONDS;
    }
    if (interval_seconds > APP_CONFIG_ONEWIRE_POLL_INTERVAL_MAX_SECONDS) {
        interval_seconds = APP_CONFIG_ONEWIRE_POLL_INTERVAL_MAX_SECONDS;
    }

    onewire_poll_interval_ms = interval_seconds * 1000u;
    if (onewire_source_state == ONEWIRE_SOURCE_IDLE) {
        onewire_next_poll_ms = to_ms_since_boot(get_absolute_time());
    }

    for (int i = 0; i < ONEWIRE_DEVICE_TABLE_SIZE; ++i) {
        if (onewire_devices[i].in_use &&
            ((onewire_devices[i].gpio == onewire_gpios[0] && !onewire_bus_enabled[0]) ||
             (onewire_devices[i].gpio == onewire_gpios[1] && !onewire_bus_enabled[1]))) {
            onewire_devices[i].present = false;
        }
    }
}

void onewire_source_periodic(uint32_t now_ms)
{
    onewire_response_t response;

    while (onewire_worker_try_get_response(&response)) {
        onewire_handle_response(&response, now_ms);
    }

    if (onewire_source_state != ONEWIRE_SOURCE_IDLE || (int32_t)(now_ms - onewire_next_poll_ms) < 0) {
        return;
    }

    for (int i = 0; i < ONEWIRE_DEVICE_TABLE_SIZE; ++i) {
        if (onewire_devices[i].in_use) {
            onewire_devices[i].present = false;
        }
    }

    onewire_current_bus = 0;
    onewire_start_next_bus(now_ms);
}

u16_t onewire_source_write_devices_json(char *insert, int insert_len)
{
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    int written = 0;
    bool first = true;

    for (int i = 0; i < ONEWIRE_DEVICE_TABLE_SIZE; ++i) {
        const onewire_device_entry_t *entry = &onewire_devices[i];
        char rom[17];
        const char *name;
        char escaped_name[APP_CONFIG_DEVICE_NAME_LEN * 2];
        uint32_t age_ms;
        int result;

        if (!entry->in_use) {
            continue;
        }

        onewire_format_rom(rom, sizeof(rom), entry->rom);
        name = app_runtime_config_get_sensor_name(entry->device_id);
        app_text_json_escape(escaped_name, sizeof(escaped_name), name);
        age_ms = (now_ms >= entry->last_seen_ms) ? (now_ms - entry->last_seen_ms) : 0;

        result = snprintf(insert + written,
                          insert_len - written,
                          "%s{\"id\":\"%06lu\",\"device_id\":%lu,\"name\":\"%s\",\"gpio\":%u,\"rom\":\"%s\",\"temperature\":%.3f,\"temperature_valid\":%s,\"present\":%s,\"age_ms\":%lu,\"error_count\":%lu}",
                          first ? "" : ",",
                          (unsigned long)(entry->device_id % 1000000u),
                          (unsigned long)entry->device_id,
                          escaped_name,
                          (unsigned)entry->gpio,
                          rom,
                          (double)entry->temperature_c,
                          entry->valid_temperature ? "true" : "false",
                          entry->present ? "true" : "false",
                          (unsigned long)age_ms,
                          (unsigned long)entry->error_count);
        if (result < 0 || result >= (int)(insert_len - written)) {
            break;
        }

        written += result;
        first = false;
    }

    return (u16_t)written;
}

int onewire_source_device_table_used(void)
{
    int count = 0;

    for (int i = 0; i < ONEWIRE_DEVICE_TABLE_SIZE; ++i) {
        if (onewire_devices[i].in_use) {
            ++count;
        }
    }
    return count;
}

int onewire_source_device_table_capacity(void)
{
    return ONEWIRE_DEVICE_TABLE_SIZE;
}
