#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "pico/stdlib.h"

#include "app_log.h"
#include "app_runtime_config.h"
#include "app_text.h"
#include "ble_scanner.h"
#include "measurement_store.h"

#define BLE_DEVICE_TABLE_SIZE 128
#define BLE_DEVICE_EXPIRY_MS 60000
#define BLE_DEVICE_BTHOME_DATA_MAX_LEN 128

typedef struct {
    bool in_use;
    bool selected;
    bd_addr_t address;
    uint32_t device_id;
    uint8_t address_type;
    int8_t rssi;
    char bthome_data[BLE_DEVICE_BTHOME_DATA_MAX_LEN];
    uint32_t last_seen_ms;
} ble_device_entry_t;

typedef struct {
    bool has_measurements;
    bool parsed_any;
    bool allow_measurements;
} ble_parse_context_t;

static btstack_packet_callback_registration_t ble_hci_event_callback_registration;
static ble_device_entry_t ble_device_table[BLE_DEVICE_TABLE_SIZE];
static char ble_allowed_macs[APP_CONFIG_ALLOWED_MAC_MAX_COUNT][APP_CONFIG_ALLOWED_MAC_LEN];
static uint16_t ble_allowed_mac_count;

static void ble_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static bool ble_address_to_normalized_mac(const bd_addr_t address, char normalized_mac[APP_CONFIG_ALLOWED_MAC_LEN])
{
    return snprintf(normalized_mac,
                    APP_CONFIG_ALLOWED_MAC_LEN,
                    "%02X%02X%02X%02X%02X%02X",
                    address[0],
                    address[1],
                    address[2],
                    address[3],
                    address[4],
                    address[5]) == (APP_CONFIG_ALLOWED_MAC_LEN - 1);
}

static uint32_t ble_address_to_device_id(const bd_addr_t address)
{
    return ((uint32_t)address[2] << 24) |
           ((uint32_t)address[3] << 16) |
           ((uint32_t)address[4] << 8) |
           (uint32_t)address[5];
}

static uint32_t ble_device_id_to_display(uint32_t device_id)
{
    return device_id % 1000000u;
}

static bool ble_normalized_mac_to_address(const char *normalized_mac, bd_addr_t address)
{
    unsigned int bytes[6];

    if (normalized_mac == NULL) {
        return false;
    }

    if (sscanf(normalized_mac, "%2x%2x%2x%2x%2x%2x",
               &bytes[0], &bytes[1], &bytes[2],
               &bytes[3], &bytes[4], &bytes[5]) != 6) {
        return false;
    }

    for (int i = 0; i < 6; ++i) {
        address[i] = (uint8_t)bytes[i];
    }

    return true;
}

static bool ble_mac_is_allowed(const bd_addr_t address)
{
    char normalized_mac[APP_CONFIG_ALLOWED_MAC_LEN];

    if (!ble_address_to_normalized_mac(address, normalized_mac)) {
        return false;
    }

    for (uint16_t i = 0; i < ble_allowed_mac_count; ++i) {
        if (strcmp(ble_allowed_macs[i], normalized_mac) == 0) {
            return true;
        }
    }

    return false;
}

static void format_ble_address(char *buffer, size_t buffer_size, const bd_addr_t address)
{
    snprintf(buffer,
             buffer_size,
             "%02X:%02X:%02X:%02X:%02X:%02X",
             address[0],
             address[1],
             address[2],
             address[3],
             address[4],
             address[5]);
}

static void ble_device_reset_parsed_data(ble_device_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }

    entry->bthome_data[0] = '\0';
}

static ble_device_entry_t *ble_device_table_find(const bd_addr_t address, uint8_t address_type)
{
    for (int i = 0; i < BLE_DEVICE_TABLE_SIZE; ++i) {
        ble_device_entry_t *entry = &ble_device_table[i];

        if (!entry->in_use) {
            continue;
        }

        if ((entry->address_type == address_type) &&
            (memcmp(entry->address, address, sizeof(bd_addr_t)) == 0)) {
            return entry;
        }
    }

    return NULL;
}

static ble_device_entry_t *ble_device_table_find_by_mac_string(const char *normalized_mac)
{
    for (int i = 0; i < BLE_DEVICE_TABLE_SIZE; ++i) {
        char entry_mac[APP_CONFIG_ALLOWED_MAC_LEN];

        if (!ble_device_table[i].in_use) {
            continue;
        }

        if (ble_address_to_normalized_mac(ble_device_table[i].address, entry_mac) &&
            strcmp(entry_mac, normalized_mac) == 0) {
            return &ble_device_table[i];
        }
    }

    return NULL;
}

static void ble_device_table_update(const bd_addr_t address, uint8_t address_type, int8_t rssi, uint32_t last_seen_ms)
{
    int free_index = -1;
    int oldest_index = 0;

    for (int i = 0; i < BLE_DEVICE_TABLE_SIZE; ++i) {
        ble_device_entry_t *entry = &ble_device_table[i];

        if (entry->in_use) {
            if ((entry->address_type == address_type) &&
                (memcmp(entry->address, address, sizeof(bd_addr_t)) == 0)) {
                entry->device_id = ble_address_to_device_id(address);
                entry->rssi = rssi;
                entry->last_seen_ms = last_seen_ms;
                entry->selected = ble_mac_is_allowed(address);
                return;
            }

            if (entry->last_seen_ms < ble_device_table[oldest_index].last_seen_ms) {
                oldest_index = i;
            }
        } else if (free_index < 0) {
            free_index = i;
        }
    }

    ble_device_entry_t *entry = &ble_device_table[free_index >= 0 ? free_index : oldest_index];
    memset(entry, 0, sizeof(*entry));
    entry->in_use = true;
    memcpy(entry->address, address, sizeof(bd_addr_t));
    entry->device_id = ble_address_to_device_id(address);
    entry->address_type = address_type;
    entry->rssi = rssi;
    entry->last_seen_ms = last_seen_ms;
    entry->selected = ble_mac_is_allowed(address);
}

static void ble_device_append_bthome_value(ble_device_entry_t *entry, const char *label, const char *value)
{
    size_t current_len;
    int written;

    if (entry == NULL) {
        return;
    }

    current_len = strlen(entry->bthome_data);
    if (current_len >= (BLE_DEVICE_BTHOME_DATA_MAX_LEN - 1)) {
        return;
    }

    written = snprintf(entry->bthome_data + current_len,
                       BLE_DEVICE_BTHOME_DATA_MAX_LEN - current_len,
                       "%s%s=%s",
                       current_len == 0 ? "" : ", ",
                       label,
                       value);
    if (written < 0 || written >= (int)(BLE_DEVICE_BTHOME_DATA_MAX_LEN - current_len)) {
        entry->bthome_data[BLE_DEVICE_BTHOME_DATA_MAX_LEN - 1] = '\0';
    }
}

static void ble_measurement_append_string(ble_device_entry_t *entry, measurement_field_t field_type, float value)
{
    char value_buffer[24];

    switch (field_type) {
    case MEASUREMENT_FIELD_BATTERY:
        snprintf(value_buffer, sizeof(value_buffer), "%.0f %%", (double)value);
        ble_device_append_bthome_value(entry, "bat", value_buffer);
        break;

    case MEASUREMENT_FIELD_TEMPERATURE:
        snprintf(value_buffer, sizeof(value_buffer), "%.2f C", (double)value);
        ble_device_append_bthome_value(entry, "T", value_buffer);
        break;

    case MEASUREMENT_FIELD_HUMIDITY:
        snprintf(value_buffer, sizeof(value_buffer), "%.2f %%", (double)value);
        ble_device_append_bthome_value(entry, "H", value_buffer);
        break;

    case MEASUREMENT_FIELD_NONE:
    default:
        break;
    }
}

static bool ble_bthome_store_measurement(ble_parse_context_t *context,
                                         ble_device_entry_t *entry,
                                         measurement_field_t field_type,
                                         float value,
                                         uint32_t timestamp_ms)
{
    ble_measurement_append_string(entry, field_type, value);
    context->parsed_any = true;

    if (!context->allow_measurements) {
        return true;
    }

    if (!measurement_store_add(entry->device_id, field_type, value, timestamp_ms)) {
        return false;
    }

    context->has_measurements = true;
    return true;
}

static bool ble_parse_bthome_v2(ble_parse_context_t *context,
                                ble_device_entry_t *entry,
                                const uint8_t *field_data,
                                uint8_t field_data_len,
                                uint32_t timestamp_ms)
{
    uint8_t info;
    uint8_t offset;

    if (entry == NULL || context == NULL || field_data_len < 3) {
        return false;
    }

    info = field_data[2];
    if ((info & 0xE0u) != 0x40u) {
        return false;
    }
    if ((info & 0x01u) != 0) {
        return false;
    }

    entry->bthome_data[0] = '\0';
    offset = 3;

    while (offset < field_data_len) {
        uint8_t object_id = field_data[offset++];

        switch (object_id) {
        case 0x00:
            if ((offset + 1) > field_data_len) {
                return context->parsed_any;
            }
            offset += 1;
            break;

        case 0x01:
            if ((offset + 1) > field_data_len) {
                return context->parsed_any;
            }
            if (!ble_bthome_store_measurement(context,
                                              entry,
                                              MEASUREMENT_FIELD_BATTERY,
                                              (float)field_data[offset],
                                              timestamp_ms)) {
                return context->parsed_any;
            }
            offset += 1;
            break;

        case 0x02:
            if ((offset + 2) > field_data_len) {
                return context->parsed_any;
            }
            if (!ble_bthome_store_measurement(context,
                                              entry,
                                              MEASUREMENT_FIELD_TEMPERATURE,
                                              (float)(int16_t)little_endian_read_16(field_data, offset) / 100.0f,
                                              timestamp_ms)) {
                return context->parsed_any;
            }
            offset += 2;
            break;

        case 0x03:
            if ((offset + 2) > field_data_len) {
                return context->parsed_any;
            }
            if (!ble_bthome_store_measurement(context,
                                              entry,
                                              MEASUREMENT_FIELD_HUMIDITY,
                                              (float)little_endian_read_16(field_data, offset) / 100.0f,
                                              timestamp_ms)) {
                return context->parsed_any;
            }
            offset += 2;
            break;

        default:
            return context->parsed_any;
        }
    }

    return context->parsed_any;
}

static bool ble_parse_bthome_advertising_data(ble_device_entry_t *entry,
                                              const bd_addr_t address,
                                              const uint8_t *data,
                                              uint8_t length,
                                              uint32_t timestamp_ms)
{
    uint8_t offset = 0;
    ble_parse_context_t context;

    if (entry == NULL) {
        return false;
    }

    memset(&context, 0, sizeof(context));
    context.allow_measurements = ble_mac_is_allowed(address);
    ble_device_reset_parsed_data(entry);

    while (offset < length) {
        uint8_t field_len = data[offset];
        uint8_t field_type;
        const uint8_t *field_data;
        uint8_t field_data_len;

        if (field_len == 0) {
            break;
        }
        if ((offset + 1 + field_len) > length) {
            break;
        }

        field_type = data[offset + 1];
        field_data = &data[offset + 2];
        field_data_len = field_len - 1;

        if (field_type == BLUETOOTH_DATA_TYPE_SERVICE_DATA && field_data_len >= 2) {
            uint16_t uuid16 = little_endian_read_16(field_data, 0);
            if (uuid16 == 0xFCD2) {
                entry->device_id = ble_address_to_device_id(address);
                return ble_parse_bthome_v2(&context, entry, field_data, field_data_len, timestamp_ms);
            }
        }

        offset = (uint8_t)(offset + field_len + 1);
    }

    return false;
}

void ble_scanner_periodic(uint32_t now_ms)
{
    int write_index = 0;

    for (int i = 0; i < BLE_DEVICE_TABLE_SIZE; ++i) {
        const ble_device_entry_t *entry = &ble_device_table[i];
        uint32_t age_ms;

        if (!entry->in_use) {
            continue;
        }

        age_ms = (now_ms >= entry->last_seen_ms) ? (now_ms - entry->last_seen_ms) : 0;
        if (entry->selected || age_ms <= BLE_DEVICE_EXPIRY_MS) {
            if (write_index != i) {
                ble_device_table[write_index] = *entry;
            }
            ++write_index;
        }
    }

    for (int i = write_index; i < BLE_DEVICE_TABLE_SIZE; ++i) {
        memset(&ble_device_table[i], 0, sizeof(ble_device_table[i]));
    }
}

int ble_scanner_device_count(void)
{
    int count = 0;

    for (int i = 0; i < BLE_DEVICE_TABLE_SIZE; ++i) {
        if (ble_device_table[i].in_use) {
            ++count;
        }
    }

    return count;
}

void ble_scanner_apply_config(const app_config_t *config)
{
    ble_allowed_mac_count = 0;

    if (config != NULL) {
        for (uint16_t i = 0; i < config->allowed_mac_count && i < APP_CONFIG_ALLOWED_MAC_MAX_COUNT; ++i) {
            snprintf(ble_allowed_macs[ble_allowed_mac_count],
                     sizeof(ble_allowed_macs[ble_allowed_mac_count]),
                     "%s",
                     config->allowed_macs[i]);
            ++ble_allowed_mac_count;
        }
    }

    for (int i = 0; i < BLE_DEVICE_TABLE_SIZE; ++i) {
        if (ble_device_table[i].in_use) {
            ble_device_table[i].selected = ble_mac_is_allowed(ble_device_table[i].address);
        }
    }

    for (uint16_t i = 0; i < ble_allowed_mac_count; ++i) {
        ble_device_entry_t *entry = ble_device_table_find_by_mac_string(ble_allowed_macs[i]);

        if (entry != NULL) {
            entry->selected = true;
            continue;
        }

        for (int slot = 0; slot < BLE_DEVICE_TABLE_SIZE; ++slot) {
            if (!ble_device_table[slot].in_use) {
                memset(&ble_device_table[slot], 0, sizeof(ble_device_table[slot]));
                ble_device_table[slot].in_use = true;
                ble_device_table[slot].selected = true;
                ble_device_table[slot].address_type = 0;
                ble_device_table[slot].rssi = 0;
                ble_device_table[slot].last_seen_ms = 0;
                ble_normalized_mac_to_address(ble_allowed_macs[i], ble_device_table[slot].address);
                ble_device_table[slot].device_id = ble_address_to_device_id(ble_device_table[slot].address);
                break;
            }
        }
    }
}

void ble_scanner_init(void)
{
    memset(ble_device_table, 0, sizeof(ble_device_table));
    memset(ble_allowed_macs, 0, sizeof(ble_allowed_macs));
    ble_allowed_mac_count = 0;
    ble_hci_event_callback_registration.callback = &ble_packet_handler;
    hci_add_event_handler(&ble_hci_event_callback_registration);
    hci_power_control(HCI_POWER_ON);
}

static void ble_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    bd_addr_t address;
    uint8_t address_type;
    int8_t rssi;

    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            app_log("BLE scanner is ready, starting passive scan");
            gap_set_scan_parameters(0, 0x0030, 0x0030);
            gap_start_scan();
        }
        break;

    case GAP_EVENT_ADVERTISING_REPORT:
        gap_event_advertising_report_get_address(packet, address);
        address_type = gap_event_advertising_report_get_address_type(packet);
        rssi = gap_event_advertising_report_get_rssi(packet);
        {
            ble_device_entry_t *entry;
            ble_device_entry_t scratch;
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());

            memset(&scratch, 0, sizeof(scratch));
            if (!ble_parse_bthome_advertising_data(&scratch,
                                                   address,
                                                   gap_event_advertising_report_get_data(packet),
                                                   gap_event_advertising_report_get_data_length(packet),
                                                   now_ms)) {
                break;
            }

            ble_device_table_update(address, address_type, rssi, now_ms);
            entry = ble_device_table_find(address, address_type);
            if (entry != NULL) {
                ble_device_reset_parsed_data(entry);
                snprintf(entry->bthome_data,
                         BLE_DEVICE_BTHOME_DATA_MAX_LEN,
                         "%s",
                         scratch.bthome_data);
            }
        }
        break;

    default:
        break;
    }
}

u16_t ble_scanner_write_devices_json(char *insert, int insert_len)
{
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    int written = 0;
    bool first = true;

    for (int i = 0; i < BLE_DEVICE_TABLE_SIZE; ++i) {
        const ble_device_entry_t *entry = &ble_device_table[i];
        char mac[18];
        const char *name;
        char escaped_name[APP_CONFIG_DEVICE_NAME_LEN * 2];
        char escaped_bthome_data[BLE_DEVICE_BTHOME_DATA_MAX_LEN * 2];
        int result;
        uint32_t age_ms = (now_ms >= entry->last_seen_ms) ? (now_ms - entry->last_seen_ms) : 0;

        if (!entry->in_use) {
            continue;
        }

        format_ble_address(mac, sizeof(mac), entry->address);
        name = app_runtime_config_get_sensor_name(entry->device_id);
        app_text_json_escape(escaped_name, sizeof(escaped_name), name);
        app_text_json_escape(escaped_bthome_data, sizeof(escaped_bthome_data), entry->bthome_data);

        result = snprintf(insert + written,
                          insert_len - written,
                          "%s{\"id\":\"%06lu\",\"device_id\":%lu,\"name\":\"%s\",\"mac\":\"%s\",\"bthome_data\":\"%s\",\"rssi\":%d,\"age_ms\":%lu,\"selected\":%s}",
                          first ? "" : ",",
                          (unsigned long)ble_device_id_to_display(entry->device_id),
                          (unsigned long)entry->device_id,
                          escaped_name,
                          mac,
                          escaped_bthome_data,
                          entry->rssi,
                          (unsigned long)age_ms,
                          entry->selected ? "true" : "false");
        if (result < 0 || result >= (int)(insert_len - written)) {
            break;
        }

        written += result;
        first = false;
    }

    return (u16_t)written;
}

u16_t ble_scanner_write_measurements_json(char *insert, int insert_len)
{
    return measurement_store_write_json(insert, insert_len);
}

int ble_scanner_device_table_used(void)
{
    return ble_scanner_device_count();
}

int ble_scanner_device_table_capacity(void)
{
    return BLE_DEVICE_TABLE_SIZE;
}

int ble_scanner_measurement_table_used(void)
{
    return measurement_store_table_used();
}

int ble_scanner_measurement_table_capacity(void)
{
    return measurement_store_table_capacity();
}

int ble_scanner_measurement_view_used(void)
{
    return measurement_store_view_used();
}

int ble_scanner_measurement_view_capacity(void)
{
    return measurement_store_view_capacity();
}
