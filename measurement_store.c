#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_narodmon.h"
#include "app_runtime_config.h"
#include "app_text.h"
#include "measurement_store.h"

#define MEASUREMENT_STORE_TABLE_SIZE 4096
#define MEASUREMENT_STORE_VIEW_SIZE 150
#define MEASUREMENT_STORE_RETENTION_DEFAULT_MS (APP_CONFIG_MEASUREMENT_RETENTION_DEFAULT_SECONDS * 1000u)
#define MEASUREMENT_STORE_REVISION_INTERVAL_MS 1000u

#define NARODMON_LINE_TEMP_MAX_LEN 20
#define NARODMON_LINE_RH_MAX_LEN 17
#define NARODMON_LINE_BATTERY_MAX_LEN 21
#define NARODMON_LINE_SIGNAL_MAX_LEN 20
#define NARODMON_LINE_UPTIME_MAX_LEN 19
#define NARODMON_HEADER_AND_TRAILER_MAX_LEN 20
#define NARODMON_MAX_DEVICES (((APP_NARODMON_PAYLOAD_MAX_LEN - NARODMON_HEADER_AND_TRAILER_MAX_LEN - NARODMON_LINE_UPTIME_MAX_LEN) / \
                               (NARODMON_LINE_TEMP_MAX_LEN + NARODMON_LINE_RH_MAX_LEN + NARODMON_LINE_BATTERY_MAX_LEN + NARODMON_LINE_SIGNAL_MAX_LEN)) - 1)

typedef struct {
    uint32_t device_id;
    uint8_t field_type;
    float value;
    uint32_t timestamp_ms;
} measurement_entry_t;

typedef struct {
    bool in_use;
    uint32_t device_id;
    uint8_t field_type;
    float average_value;
    uint16_t sample_count;
} measurement_view_entry_t;

typedef struct {
    float sum_value;
    uint32_t sample_count;
} measurement_accumulator_t;

typedef struct {
    bool in_use;
    uint32_t device_id;
    uint16_t max_samples;
} narodmon_signal_entry_t;

static measurement_entry_t measurement_table[MEASUREMENT_STORE_TABLE_SIZE];
static measurement_view_entry_t measurement_view[MEASUREMENT_STORE_VIEW_SIZE];
static uint32_t measurement_retention_ms = MEASUREMENT_STORE_RETENTION_DEFAULT_MS;
static uint16_t measurement_max_count;
static uint32_t measurement_last_revision_ms;

static uint32_t measurement_device_id_to_display(uint32_t device_id)
{
    return device_id % 1000000u;
}

static const char *measurement_field_name(uint8_t field_type)
{
    switch ((measurement_field_t)field_type) {
    case MEASUREMENT_FIELD_BATTERY:
        return "battery";
    case MEASUREMENT_FIELD_TEMPERATURE:
        return "temperature";
    case MEASUREMENT_FIELD_HUMIDITY:
        return "humidity";
    case MEASUREMENT_FIELD_NONE:
    default:
        return "unknown";
    }
}

static int measurement_table_prune(uint32_t now_ms, uint32_t retention_ms)
{
    int free_count = 0;

    for (int i = 0; i < MEASUREMENT_STORE_TABLE_SIZE; ++i) {
        measurement_entry_t *entry = &measurement_table[i];
        uint32_t age_ms;

        if (entry->field_type == MEASUREMENT_FIELD_NONE) {
            ++free_count;
            continue;
        }

        age_ms = (now_ms >= entry->timestamp_ms) ? (now_ms - entry->timestamp_ms) : 0;
        if (age_ms > retention_ms) {
            entry->field_type = MEASUREMENT_FIELD_NONE;
            ++free_count;
        }
    }

    return free_count;
}

static int measurement_find_oldest(uint32_t now_ms, uint32_t device_id, uint8_t field_type, bool filter_series)
{
    int oldest_index = -1;
    uint32_t oldest_age_ms = 0;

    for (int i = 0; i < MEASUREMENT_STORE_TABLE_SIZE; ++i) {
        const measurement_entry_t *entry = &measurement_table[i];
        uint32_t age_ms;

        if (entry->field_type == MEASUREMENT_FIELD_NONE) {
            continue;
        }

        if (filter_series && (entry->device_id != device_id || entry->field_type != field_type)) {
            continue;
        }

        age_ms = (now_ms >= entry->timestamp_ms) ? (now_ms - entry->timestamp_ms) : 0;
        if (oldest_index < 0 || age_ms > oldest_age_ms) {
            oldest_index = i;
            oldest_age_ms = age_ms;
        }
    }

    return oldest_index;
}

static int measurement_find_free_slot(void)
{
    for (int i = 0; i < MEASUREMENT_STORE_TABLE_SIZE; ++i) {
        if (measurement_table[i].field_type == MEASUREMENT_FIELD_NONE) {
            return i;
        }
    }

    return -1;
}

static void measurement_enforce_series_limit(uint32_t now_ms, uint32_t device_id, uint8_t field_type)
{
    if (measurement_max_count == 0) {
        return;
    }

    while (true) {
        uint16_t count = 0;

        for (int i = 0; i < MEASUREMENT_STORE_TABLE_SIZE; ++i) {
            const measurement_entry_t *entry = &measurement_table[i];

            if (entry->field_type == field_type && entry->device_id == device_id) {
                ++count;
            }
        }

        if (count < measurement_max_count) {
            return;
        }

        int oldest_index = measurement_find_oldest(now_ms, device_id, field_type, true);
        if (oldest_index < 0) {
            return;
        }
        measurement_table[oldest_index].field_type = MEASUREMENT_FIELD_NONE;
    }
}

static void measurement_view_rebuild(void)
{
    measurement_accumulator_t accumulators[MEASUREMENT_STORE_VIEW_SIZE];

    memset(measurement_view, 0, sizeof(measurement_view));
    memset(accumulators, 0, sizeof(accumulators));

    for (int i = 0; i < MEASUREMENT_STORE_TABLE_SIZE; ++i) {
        const measurement_entry_t *measurement = &measurement_table[i];
        int slot = -1;

        if (measurement->field_type == MEASUREMENT_FIELD_NONE) {
            continue;
        }

        for (int j = 0; j < MEASUREMENT_STORE_VIEW_SIZE; ++j) {
            measurement_view_entry_t *view = &measurement_view[j];

            if (!view->in_use) {
                if (slot < 0) {
                    slot = j;
                }
                continue;
            }

            if ((view->field_type == measurement->field_type) &&
                (view->device_id == measurement->device_id)) {
                slot = j;
                break;
            }
        }

        if (slot < 0) {
            continue;
        }

        if (!measurement_view[slot].in_use) {
            measurement_view[slot].in_use = true;
            measurement_view[slot].device_id = measurement->device_id;
            measurement_view[slot].field_type = measurement->field_type;
        }

        accumulators[slot].sum_value += measurement->value;
        ++accumulators[slot].sample_count;
    }

    for (int i = 0; i < MEASUREMENT_STORE_VIEW_SIZE; ++i) {
        measurement_view_entry_t *view = &measurement_view[i];

        if (!view->in_use || accumulators[i].sample_count == 0) {
            view->in_use = false;
            continue;
        }

        view->sample_count = (uint16_t)accumulators[i].sample_count;
        view->average_value = accumulators[i].sum_value / (float)accumulators[i].sample_count;
    }
}

static void measurement_revision(uint32_t now_ms)
{
    measurement_table_prune(now_ms, measurement_retention_ms);
    measurement_view_rebuild();
    measurement_last_revision_ms = now_ms;
}

static int narodmon_find_or_add_signal_entry(narodmon_signal_entry_t *entries,
                                             int entry_count,
                                             int current_device_count,
                                             uint32_t device_id)
{
    int first_free = -1;

    for (int i = 0; i < entry_count; ++i) {
        if (!entries[i].in_use) {
            if (first_free < 0) {
                first_free = i;
            }
            continue;
        }

        if (entries[i].device_id == device_id) {
            return i;
        }
    }

    if (first_free < 0 || current_device_count >= NARODMON_MAX_DEVICES) {
        return -1;
    }

    entries[first_free].in_use = true;
    entries[first_free].device_id = device_id;
    entries[first_free].max_samples = 0;
    return first_free;
}

void measurement_store_init(uint32_t now_ms)
{
    memset(measurement_table, 0, sizeof(measurement_table));
    memset(measurement_view, 0, sizeof(measurement_view));
    measurement_retention_ms = MEASUREMENT_STORE_RETENTION_DEFAULT_MS;
    measurement_max_count = APP_CONFIG_MEASUREMENT_MAX_COUNT_DEFAULT;
    measurement_last_revision_ms = now_ms;
}

void measurement_store_configure(uint32_t retention_seconds, uint16_t max_measurements_per_series)
{
    if (retention_seconds < APP_CONFIG_MEASUREMENT_RETENTION_MIN_SECONDS) {
        retention_seconds = APP_CONFIG_MEASUREMENT_RETENTION_MIN_SECONDS;
    }
    if (retention_seconds > APP_CONFIG_MEASUREMENT_RETENTION_MAX_SECONDS) {
        retention_seconds = APP_CONFIG_MEASUREMENT_RETENTION_MAX_SECONDS;
    }
    if (max_measurements_per_series > APP_CONFIG_MEASUREMENT_MAX_COUNT_MAX) {
        max_measurements_per_series = APP_CONFIG_MEASUREMENT_MAX_COUNT_MAX;
    }

    measurement_retention_ms = retention_seconds * 1000u;
    measurement_max_count = max_measurements_per_series;
}

void measurement_store_periodic(uint32_t now_ms)
{
    if ((now_ms - measurement_last_revision_ms) >= MEASUREMENT_STORE_REVISION_INTERVAL_MS) {
        measurement_revision(now_ms);
    }
}

bool measurement_store_add(uint32_t device_id, measurement_field_t field_type, float value, uint32_t timestamp_ms)
{
    int free_index;

    if (field_type == MEASUREMENT_FIELD_NONE) {
        return false;
    }

    measurement_table_prune(timestamp_ms, measurement_retention_ms);
    measurement_enforce_series_limit(timestamp_ms, device_id, (uint8_t)field_type);

    free_index = measurement_find_free_slot();
    if (free_index < 0) {
        int oldest_index = measurement_find_oldest(timestamp_ms, 0, 0, false);
        if (oldest_index < 0) {
            return false;
        }
        measurement_table[oldest_index].field_type = MEASUREMENT_FIELD_NONE;
        free_index = oldest_index;
    }

    measurement_table[free_index].device_id = device_id;
    measurement_table[free_index].field_type = (uint8_t)field_type;
    measurement_table[free_index].value = value;
    measurement_table[free_index].timestamp_ms = timestamp_ms;
    return true;
}

void measurement_store_filter(measurement_store_device_allowed_fn is_allowed, void *context)
{
    if (is_allowed == NULL) {
        return;
    }

    for (int i = 0; i < MEASUREMENT_STORE_TABLE_SIZE; ++i) {
        if (measurement_table[i].field_type == MEASUREMENT_FIELD_NONE) {
            continue;
        }

        if (!is_allowed(measurement_table[i].device_id, context)) {
            measurement_table[i].field_type = MEASUREMENT_FIELD_NONE;
        }
    }

    measurement_view_rebuild();
}

u16_t measurement_store_write_json(char *insert, int insert_len)
{
    int written = 0;
    bool first = true;

    for (int i = 0; i < MEASUREMENT_STORE_VIEW_SIZE; ++i) {
        const measurement_view_entry_t *entry = &measurement_view[i];
        const char *name = "";
        char escaped_name[APP_CONFIG_DEVICE_NAME_LEN * 2];
        int result;

        if (!entry->in_use) {
            continue;
        }

        name = app_runtime_config_get_sensor_name(entry->device_id);
        if (name == NULL) {
            name = "";
        }
        app_text_json_escape(escaped_name, sizeof(escaped_name), name);

        result = snprintf(insert + written,
                          insert_len - written,
                          "%s{\"id\":\"%06lu\",\"name\":\"%s\",\"field_type\":\"%s\",\"average_value\":%.3f,\"sample_count\":%u}",
                          first ? "" : ",",
                          (unsigned long)measurement_device_id_to_display(entry->device_id),
                          escaped_name,
                          measurement_field_name(entry->field_type),
                          (double)entry->average_value,
                          entry->sample_count);
        if (result < 0 || result >= (int)(insert_len - written)) {
            break;
        }

        written += result;
        first = false;
    }

    return (u16_t)written;
}

int measurement_store_build_narodmon_payload(char *insert, int insert_len, uint32_t uptime_seconds)
{
    narodmon_signal_entry_t signal_entries[MEASUREMENT_STORE_VIEW_SIZE];
    int written = 0;
    int exported_signal_devices = 0;
    bool has_sensor_metrics = false;

    memset(signal_entries, 0, sizeof(signal_entries));

    {
        int result = snprintf(insert + written,
                              insert_len - written,
                              "#UPTIME#%lu\n",
                              (unsigned long)uptime_seconds);
        if (result < 0 || result >= (int)(insert_len - written)) {
            return written;
        }
        written += result;
    }

    for (int i = 0; i < MEASUREMENT_STORE_VIEW_SIZE; ++i) {
        const measurement_view_entry_t *entry = &measurement_view[i];
        const char *metric_prefix;
        int result;
        int signal_index;

        if (!entry->in_use) {
            continue;
        }

        signal_index = narodmon_find_or_add_signal_entry(signal_entries,
                                                         MEASUREMENT_STORE_VIEW_SIZE,
                                                         exported_signal_devices,
                                                         entry->device_id);
        if (signal_index < 0) {
            continue;
        }
        if (signal_entries[signal_index].max_samples == 0) {
            ++exported_signal_devices;
        }

        switch ((measurement_field_t)entry->field_type) {
        case MEASUREMENT_FIELD_TEMPERATURE:
            metric_prefix = "TEMP";
            break;
        case MEASUREMENT_FIELD_HUMIDITY:
            metric_prefix = "RH";
            break;
        case MEASUREMENT_FIELD_BATTERY:
            metric_prefix = "MB2BAT";
            break;
        default:
            continue;
        }

        result = snprintf(insert + written,
                          insert_len - written,
                          "#%s%06lu#%.2f\n",
                          metric_prefix,
                          (unsigned long)measurement_device_id_to_display(entry->device_id),
                          (double)entry->average_value);
        if (result < 0 || result >= (int)(insert_len - written)) {
            break;
        }

        written += result;
        has_sensor_metrics = true;

        if (entry->sample_count > signal_entries[signal_index].max_samples) {
            signal_entries[signal_index].max_samples = entry->sample_count;
        }
    }

    if (!has_sensor_metrics) {
        return 0;
    }

    for (int i = 0; i < MEASUREMENT_STORE_VIEW_SIZE; ++i) {
        int result;

        if (!signal_entries[i].in_use) {
            continue;
        }

        result = snprintf(insert + written,
                          insert_len - written,
                          "#SIGNAL%06lu#%u\n",
                          (unsigned long)measurement_device_id_to_display(signal_entries[i].device_id),
                          (unsigned)signal_entries[i].max_samples);
        if (result < 0 || result >= (int)(insert_len - written)) {
            break;
        }

        written += result;
    }

    return written;
}

int measurement_store_table_used(void)
{
    int count = 0;

    for (int i = 0; i < MEASUREMENT_STORE_TABLE_SIZE; ++i) {
        if (measurement_table[i].field_type != MEASUREMENT_FIELD_NONE) {
            ++count;
        }
    }

    return count;
}

int measurement_store_table_capacity(void)
{
    return MEASUREMENT_STORE_TABLE_SIZE;
}

int measurement_store_view_used(void)
{
    int count = 0;

    for (int i = 0; i < MEASUREMENT_STORE_VIEW_SIZE; ++i) {
        if (measurement_view[i].in_use) {
            ++count;
        }
    }

    return count;
}

int measurement_store_view_capacity(void)
{
    return MEASUREMENT_STORE_VIEW_SIZE;
}
