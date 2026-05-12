#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "app_config.h"
#include "app_log.h"
#include "app_narodmon.h"
#include "app_runtime_config.h"
#include "app_storage.h"
#include "app_text.h"
#include "app_web.h"
#include "ble_scanner.h"

#define APP_WEB_SAVE_STATUS_MAX_LEN 96
#define APP_WEB_REBOOT_DELAY_MS 1000u

static app_config_t app_web_next_config;
static uint8_t app_web_cpu_load_percent;
static uint32_t app_web_narodmon_seconds_remaining;
static bool app_web_save_status_valid;
static char app_web_save_status[APP_WEB_SAVE_STATUS_MAX_LEN];
static bool app_web_reboot_pending;
static uint32_t app_web_reboot_deadline_ms;

const char *app_web_ssi_tags[] = {
    "devices",
    "meas",
    "stat",
    "cfg",
    "nmon",
};

const size_t app_web_ssi_tag_count = sizeof(app_web_ssi_tags) / sizeof(app_web_ssi_tags[0]);

static const char *app_web_cgi_settings_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]);
static const char *app_web_cgi_select_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]);
static const char *app_web_cgi_name_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]);

static const tCGI app_web_cgis[] = {
    {"/apply.cgi", app_web_cgi_settings_handler},
    {"/select.cgi", app_web_cgi_select_handler},
    {"/name.cgi", app_web_cgi_name_handler},
};

static void app_web_set_save_status(const char *message)
{
    app_web_save_status_valid = true;
    snprintf(app_web_save_status, sizeof(app_web_save_status), "%s", message != NULL ? message : "");
}

static void app_web_apply_config(const app_config_t *config)
{
    app_runtime_config_set(config);
    ble_scanner_apply_config(app_runtime_config_get());
}

static const char *app_web_find_param_value(int count, char *params[], char *values[], const char *name)
{
    for (int i = 0; i < count; ++i) {
        if (strcmp(params[i], name) == 0) {
            return values[i] != NULL ? values[i] : "";
        }
    }

    return NULL;
}

static u16_t app_web_write_status_json(char *insert, int insert_len)
{
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    char device_name[APP_CONFIG_HOSTNAME_MAX_LEN * 2];
    int written;

    app_storage_get_space_info(&total_bytes, &free_bytes);
    app_text_json_escape(device_name, sizeof(device_name), app_runtime_config_get_device_name());

    written = snprintf(insert,
                       insert_len,
                       "{\"flash_total_bytes\":%llu,"
                       "\"flash_free_bytes\":%llu,"
                       "\"cpu_load_percent\":%u,"
                       "\"device_name\":\"%s\","
                       "\"narodmon_device_name\":\"%s\","
                       "\"narodmon_seconds_remaining\":%lu,"
                       "\"device_table_used\":%d,"
                       "\"device_table_capacity\":%d,"
                       "\"measurement_table_used\":%d,"
                       "\"measurement_table_capacity\":%d,"
                       "\"measurement_view_used\":%d,"
                       "\"measurement_view_capacity\":%d}",
                       (unsigned long long)total_bytes,
                       (unsigned long long)free_bytes,
                       app_web_cpu_load_percent,
                       device_name,
                       device_name,
                       (unsigned long)app_web_narodmon_seconds_remaining,
                       ble_scanner_device_table_used(),
                       ble_scanner_device_table_capacity(),
                       ble_scanner_measurement_table_used(),
                       ble_scanner_measurement_table_capacity(),
                       ble_scanner_measurement_view_used(),
                       ble_scanner_measurement_view_capacity());

    if (written < 0) {
        return 0;
    }

    if (written >= insert_len) {
        written = insert_len - 1;
        if (written < 0) {
            written = 0;
        }
        insert[written] = '\0';
    }

    return (u16_t)written;
}

static u16_t app_web_write_config_json(char *insert, int insert_len)
{
    const app_config_t *config = app_runtime_config_get();
    char ssid[APP_CONFIG_SSID_MAX_LEN * 2];
    char hostname[APP_CONFIG_HOSTNAME_MAX_LEN * 2];
    char password[APP_CONFIG_PASSWORD_MAX_LEN * 2];
    char save_status[APP_WEB_SAVE_STATUS_MAX_LEN * 2];
    int written;

    app_text_json_escape(ssid, sizeof(ssid), config->ssid);
    app_text_json_escape(hostname, sizeof(hostname), config->hostname);
    app_text_json_escape(password, sizeof(password), config->password);
    app_text_json_escape(save_status, sizeof(save_status), app_web_save_status_valid ? app_web_save_status : "");

    written = snprintf(insert,
                       insert_len,
                       "{\"mode\":\"%s\","
                       "\"ssid\":\"%s\","
                       "\"hostname\":\"%s\","
                       "\"channel\":%lu,"
                       "\"security\":\"%s\","
                       "\"password\":\"%s\","
                       "\"send_narodmon\":%s,"
                       "\"measurement_retention_seconds\":%lu,"
                       "\"measurement_max_count\":%u,"
                       "\"save_status_valid\":%s,"
                       "\"save_status\":\"%s\"}",
                       app_config_mode_name(config->mode),
                       ssid,
                       hostname,
                       (unsigned long)config->channel,
                       app_config_security_name(config->security),
                       password,
                       config->send_narodmon ? "true" : "false",
                       (unsigned long)config->measurement_retention_seconds,
                       (unsigned)config->measurement_max_count,
                       app_web_save_status_valid ? "true" : "false",
                       save_status);

    if (written < 0) {
        return 0;
    }

    if (written >= insert_len) {
        written = insert_len - 1;
        if (written < 0) {
            written = 0;
        }
        insert[written] = '\0';
    }

    return (u16_t)written;
}

static u16_t app_web_write_narodmon_text(char *insert, int insert_len)
{
    int written = app_narodmon_build_payload(insert, insert_len);

    if (written < 0) {
        return 0;
    }

    return (u16_t)written;
}

static const char *app_web_cgi_settings_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    const char *value;
    bool reboot_requested;

    (void)iIndex;

    app_web_next_config = *app_runtime_config_get();
    value = app_web_find_param_value(iNumParams, pcParam, pcValue, "reboot");
    reboot_requested = (value != NULL && strcmp(value, "1") == 0);

    value = app_web_find_param_value(iNumParams, pcParam, pcValue, "mode");
    if (value != NULL) {
        char mode_value[16];
        snprintf(mode_value, sizeof(mode_value), "%s", value);
        app_text_url_decode(mode_value);
        app_web_next_config.mode = strcmp(mode_value, "client") == 0 ? APP_WIFI_MODE_CLIENT : APP_WIFI_MODE_AP;
    }

    value = app_web_find_param_value(iNumParams, pcParam, pcValue, "ssid");
    if (value != NULL) {
        snprintf(app_web_next_config.ssid, sizeof(app_web_next_config.ssid), "%s", value);
        app_text_url_decode(app_web_next_config.ssid);
    }

    value = app_web_find_param_value(iNumParams, pcParam, pcValue, "hostname");
    if (value != NULL) {
        snprintf(app_web_next_config.hostname, sizeof(app_web_next_config.hostname), "%s", value);
        app_text_url_decode(app_web_next_config.hostname);
    }

    value = app_web_find_param_value(iNumParams, pcParam, pcValue, "channel");
    if (value != NULL) {
        char channel_value[16];
        snprintf(channel_value, sizeof(channel_value), "%s", value);
        app_text_url_decode(channel_value);
        app_web_next_config.channel = (uint32_t)strtoul(channel_value, NULL, 10);
    }

    value = app_web_find_param_value(iNumParams, pcParam, pcValue, "security");
    if (value != NULL) {
        char security_value[16];
        snprintf(security_value, sizeof(security_value), "%s", value);
        app_text_url_decode(security_value);
        app_web_next_config.security = strcmp(security_value, "wpa2") == 0 ? APP_WIFI_SECURITY_WPA2 : APP_WIFI_SECURITY_OPEN;
    }

    value = app_web_find_param_value(iNumParams, pcParam, pcValue, "password");
    if (value != NULL) {
        snprintf(app_web_next_config.password, sizeof(app_web_next_config.password), "%s", value);
        app_text_url_decode(app_web_next_config.password);
    }

    value = app_web_find_param_value(iNumParams, pcParam, pcValue, "send_narodmon");
    app_web_next_config.send_narodmon = (value != NULL);

    value = app_web_find_param_value(iNumParams, pcParam, pcValue, "measurement_retention_seconds");
    if (value != NULL) {
        char retention_value[16];
        unsigned long retention_seconds;
        snprintf(retention_value, sizeof(retention_value), "%s", value);
        app_text_url_decode(retention_value);
        retention_seconds = strtoul(retention_value, NULL, 10);
        if (retention_seconds > APP_CONFIG_MEASUREMENT_RETENTION_MAX_SECONDS) {
            retention_seconds = APP_CONFIG_MEASUREMENT_RETENTION_MAX_SECONDS;
        }
        app_web_next_config.measurement_retention_seconds = (uint32_t)retention_seconds;
    }

    value = app_web_find_param_value(iNumParams, pcParam, pcValue, "measurement_max_count");
    if (value != NULL) {
        char max_count_value[16];
        unsigned long max_count;
        snprintf(max_count_value, sizeof(max_count_value), "%s", value);
        app_text_url_decode(max_count_value);
        max_count = strtoul(max_count_value, NULL, 10);
        if (max_count > APP_CONFIG_MEASUREMENT_MAX_COUNT_MAX) {
            max_count = APP_CONFIG_MEASUREMENT_MAX_COUNT_MAX;
        }
        app_web_next_config.measurement_max_count = (uint16_t)max_count;
    }

    app_config_normalize(&app_web_next_config);

    if (app_web_next_config.security == APP_WIFI_SECURITY_WPA2 && strlen(app_web_next_config.password) < 8) {
        app_web_set_save_status("Save failed: WPA2 password must be at least 8 characters");
        app_log("User settings save failed: WPA2 password too short");
        return "/settings.html";
    }

    if (!app_config_save(&app_web_next_config)) {
        app_web_set_save_status("Save failed: unable to write config.ini");
        app_log("User settings save failed: unable to write config.ini");
        return "/settings.html";
    }

    app_web_apply_config(&app_web_next_config);
    app_log("User updated settings: mode=%s ssid='%s' hostname='%s' channel=%lu security=%s send_narodmon=%s",
            app_config_mode_name(app_web_next_config.mode),
            app_web_next_config.ssid,
            app_web_next_config.hostname,
            (unsigned long)app_web_next_config.channel,
            app_config_security_name(app_web_next_config.security),
            app_web_next_config.send_narodmon ? "on" : "off");
    if (reboot_requested) {
        app_web_set_save_status("Saved to config.ini. Rebooting...");
        app_log("User requested reboot after settings save");
        app_web_reboot_pending = true;
        app_web_reboot_deadline_ms = to_ms_since_boot(get_absolute_time()) + APP_WEB_REBOOT_DELAY_MS;
    } else {
        app_web_set_save_status("Saved to config.ini. Reboot required to apply changes.");
    }
    return "/settings.html";
}

static const char *app_web_cgi_select_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    const char *mac_value;
    const char *enabled_value;
    char normalized_mac[APP_CONFIG_ALLOWED_MAC_LEN];
    bool enabled;

    (void)iIndex;

    mac_value = app_web_find_param_value(iNumParams, pcParam, pcValue, "mac");
    enabled_value = app_web_find_param_value(iNumParams, pcParam, pcValue, "enabled");
    if (mac_value == NULL || enabled_value == NULL) {
        app_web_set_save_status("Selection update failed: missing parameters");
        app_log("User device selection failed: missing parameters");
        return "/index.html";
    }

    snprintf(normalized_mac, sizeof(normalized_mac), "%s", mac_value);
    app_text_url_decode(normalized_mac);
    if (!app_config_normalize_mac(normalized_mac, sizeof(normalized_mac), normalized_mac)) {
        app_web_set_save_status("Selection update failed: invalid MAC");
        app_log("User device selection failed: invalid MAC");
        return "/index.html";
    }

    enabled = strcmp(enabled_value, "1") == 0;
    app_web_next_config = *app_runtime_config_get();
    if (!app_config_set_allowed_mac(&app_web_next_config, normalized_mac, enabled)) {
        app_web_set_save_status("Selection update failed: unable to store MAC");
        app_log("User device selection failed: unable to store MAC %s", normalized_mac);
        return "/index.html";
    }

    if (!app_config_save(&app_web_next_config)) {
        app_web_set_save_status("Selection update failed: unable to write config.ini");
        app_log("User device selection failed: unable to write config.ini");
        return "/index.html";
    }

    app_web_apply_config(&app_web_next_config);
    app_log("User %s tracking for BLE device %s", enabled ? "enabled" : "disabled", normalized_mac);
    app_web_set_save_status("Device selection saved to config.ini");
    return "/index.html";
}

static const char *app_web_cgi_name_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    const char *device_id_value;
    const char *name_value;
    char encoded_name[(APP_CONFIG_DEVICE_NAME_MAX_BYTES * 3) + 1];
    uint32_t device_id;

    (void)iIndex;

    device_id_value = app_web_find_param_value(iNumParams, pcParam, pcValue, "device_id");
    name_value = app_web_find_param_value(iNumParams, pcParam, pcValue, "name");
    if (device_id_value == NULL || name_value == NULL) {
        app_web_set_save_status("Name update failed: missing parameters");
        app_log("User device name update failed: missing parameters");
        return "/ble.html";
    }

    device_id = (uint32_t)strtoul(device_id_value, NULL, 10);
    snprintf(encoded_name, sizeof(encoded_name), "%s", name_value);
    app_text_url_decode(encoded_name);

    app_web_next_config = *app_runtime_config_get();
    if (!app_config_set_device_name(&app_web_next_config, device_id, encoded_name)) {
        app_web_set_save_status("Name update failed: unable to store name");
        app_log("User device name update failed: unable to store name for %lu", (unsigned long)device_id);
        return "/ble.html";
    }

    if (!app_config_save(&app_web_next_config)) {
        app_web_set_save_status("Name update failed: unable to write config.ini");
        app_log("User device name update failed: unable to write config.ini");
        return "/ble.html";
    }

    app_web_apply_config(&app_web_next_config);
    app_log("User updated name for device %lu", (unsigned long)device_id);
    app_web_set_save_status("Device name saved to config.ini");
    return "/ble.html";
}

void app_web_init(void)
{
    app_web_cpu_load_percent = 0;
    app_web_narodmon_seconds_remaining = 0;
    app_web_save_status_valid = false;
    app_web_save_status[0] = '\0';
    app_web_reboot_pending = false;
    app_web_reboot_deadline_ms = 0;
}

void app_web_set_cpu_load(uint8_t load_percent)
{
    app_web_cpu_load_percent = load_percent;
}

void app_web_set_narodmon_seconds_remaining(uint32_t seconds_remaining)
{
    app_web_narodmon_seconds_remaining = seconds_remaining;
}

void app_web_poll(uint32_t now_ms)
{
    if (!app_web_reboot_pending) {
        return;
    }

    if ((int32_t)(now_ms - app_web_reboot_deadline_ms) >= 0) {
        watchdog_reboot(0, 0, 0);
        while (true) {
            tight_loop_contents();
        }
    }
}

u16_t app_web_ssi_handler(int index, char *insert, int insert_len)
{
    switch (index) {
    case 0:
        return ble_scanner_write_devices_json(insert, insert_len);
    case 1:
        return ble_scanner_write_measurements_json(insert, insert_len);
    case 2:
        return app_web_write_status_json(insert, insert_len);
    case 3:
        return app_web_write_config_json(insert, insert_len);
    case 4:
        return app_web_write_narodmon_text(insert, insert_len);
    default:
        return 0;
    }
}

void app_web_register_http_handlers(void)
{
    http_set_ssi_handler(app_web_ssi_handler, app_web_ssi_tags, (int)app_web_ssi_tag_count);
    http_set_cgi_handlers(app_web_cgis, (int)(sizeof(app_web_cgis) / sizeof(app_web_cgis[0])));
}
