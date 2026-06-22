#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"

#include "app_config.h"
#include "app_storage.h"
#include "minIni.h"

#define APP_CONFIG_PATH "0:/config.ini"

static app_config_t app_config_defaults_buffer;

static void app_config_defaults(app_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->mode = APP_WIFI_MODE_AP;
    snprintf(config->ssid, sizeof(config->ssid), "%s", "BLEBridge");
    snprintf(config->hostname, sizeof(config->hostname), "%s", "blebridge");
    config->channel = 3;
    config->security = APP_WIFI_SECURITY_OPEN;
    config->measurement_retention_seconds = APP_CONFIG_MEASUREMENT_RETENTION_DEFAULT_SECONDS;
    config->measurement_max_count = APP_CONFIG_MEASUREMENT_MAX_COUNT_DEFAULT;
    config->onewire_poll_interval_seconds = APP_CONFIG_ONEWIRE_POLL_INTERVAL_DEFAULT_SECONDS;
    config->onewire_gpio2_enabled = true;
    config->onewire_gpio3_enabled = true;
}

bool app_config_normalize_mac(char *normalized_mac, size_t normalized_mac_size, const char *input)
{
    size_t written = 0;

    if (normalized_mac == NULL || normalized_mac_size < APP_CONFIG_ALLOWED_MAC_LEN || input == NULL) {
        return false;
    }

    while (*input != '\0') {
        unsigned char ch = (unsigned char)*input++;

        if (ch == ':' || ch == '-' || ch == ' ') {
            continue;
        }

        if (!isxdigit(ch) || written >= (APP_CONFIG_ALLOWED_MAC_LEN - 1)) {
            return false;
        }

        normalized_mac[written++] = (char)toupper(ch);
    }

    if (written != (APP_CONFIG_ALLOWED_MAC_LEN - 1)) {
        return false;
    }

    normalized_mac[written] = '\0';
    return true;
}

static void app_config_trim(char *text)
{
    char *start = text;
    size_t len;

    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[--len] = '\0';
    }
}

static void app_config_copy_device_name(char *dst, size_t dst_size, const char *src)
{
    const unsigned char *cursor = (const unsigned char *)src;
    size_t max_bytes;
    size_t written = 0;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    max_bytes = dst_size - 1;
    if (max_bytes > APP_CONFIG_DEVICE_NAME_MAX_BYTES) {
        max_bytes = APP_CONFIG_DEVICE_NAME_MAX_BYTES;
    }

    while (*cursor != '\0' && written < max_bytes) {
        unsigned char ch = *cursor;
        size_t char_len = 0;

        if (ch < 0x20u || ch == 0x7Fu) {
            ++cursor;
            continue;
        }

        if (ch < 0x80u) {
            char_len = 1;
        } else if (ch >= 0xC2u && ch <= 0xDFu) {
            char_len = 2;
        } else if (ch >= 0xE0u && ch <= 0xEFu) {
            char_len = 3;
        } else if (ch >= 0xF0u && ch <= 0xF4u) {
            char_len = 4;
        } else {
            ++cursor;
            continue;
        }

        if (written + char_len > max_bytes) {
            break;
        }

        bool valid = true;
        for (size_t i = 1; i < char_len; ++i) {
            if ((cursor[i] & 0xC0u) != 0x80u) {
                valid = false;
                break;
            }
        }

        if (!valid) {
            ++cursor;
            continue;
        }

        for (size_t i = 0; i < char_len; ++i) {
            dst[written++] = (char)cursor[i];
        }
        cursor += char_len;
    }

    dst[written] = '\0';
}

static bool app_config_exists(void)
{
    FILINFO info;
    return f_stat(APP_CONFIG_PATH, &info) == FR_OK;
}

static bool app_config_write_default_file(void)
{
    app_config_defaults(&app_config_defaults_buffer);
    return app_config_save(&app_config_defaults_buffer);
}

static bool app_config_ensure_file(void)
{
    if (app_config_exists()) {
        return true;
    }

    return app_config_write_default_file();
}

static app_wifi_mode_t app_config_parse_mode(const char *value)
{
    if (strcmp(value, "client") == 0) {
        return APP_WIFI_MODE_CLIENT;
    }

    return APP_WIFI_MODE_AP;
}

static app_wifi_security_t app_config_parse_security(const char *value)
{
    if (strcmp(value, "wpa2") == 0) {
        return APP_WIFI_SECURITY_WPA2;
    }

    return APP_WIFI_SECURITY_OPEN;
}

bool app_config_set_allowed_mac(app_config_t *config, const char *normalized_mac, bool enabled)
{
    char canonical[APP_CONFIG_ALLOWED_MAC_LEN];
    uint16_t index;

    if (config == NULL || !app_config_normalize_mac(canonical, sizeof(canonical), normalized_mac)) {
        return false;
    }

    for (index = 0; index < config->allowed_mac_count; ++index) {
        if (strcmp(config->allowed_macs[index], canonical) == 0) {
            break;
        }
    }

    if (enabled) {
        if (index < config->allowed_mac_count) {
            return true;
        }

        if (config->allowed_mac_count >= APP_CONFIG_ALLOWED_MAC_MAX_COUNT) {
            return false;
        }

        snprintf(config->allowed_macs[config->allowed_mac_count],
                 sizeof(config->allowed_macs[config->allowed_mac_count]),
                 "%s",
                 canonical);
        ++config->allowed_mac_count;
        return true;
    }

    if (index >= config->allowed_mac_count) {
        return true;
    }

    for (uint16_t move = index + 1; move < config->allowed_mac_count; ++move) {
        memcpy(config->allowed_macs[move - 1], config->allowed_macs[move], APP_CONFIG_ALLOWED_MAC_LEN);
    }

    if (config->allowed_mac_count > 0) {
        --config->allowed_mac_count;
        config->allowed_macs[config->allowed_mac_count][0] = '\0';
    }

    return true;
}

const char *app_config_get_device_name(const app_config_t *config)
{
    if (config == NULL || config->hostname[0] == '\0') {
        return "blebridge";
    }

    return config->hostname;
}

const char *app_config_get_sensor_name(const app_config_t *config, uint32_t device_id)
{
    if (config == NULL) {
        return "";
    }

    for (uint16_t i = 0; i < config->device_name_count; ++i) {
        if (config->device_names[i].device_id == device_id) {
            return config->device_names[i].name;
        }
    }

    return "";
}

bool app_config_set_device_name(app_config_t *config, uint32_t device_id, const char *name)
{
    char normalized_name[APP_CONFIG_DEVICE_NAME_LEN];
    uint16_t index;

    if (config == NULL) {
        return false;
    }

    app_config_copy_device_name(normalized_name, sizeof(normalized_name), name);
    app_config_trim(normalized_name);

    for (index = 0; index < config->device_name_count; ++index) {
        if (config->device_names[index].device_id == device_id) {
            break;
        }
    }

    if (normalized_name[0] == '\0') {
        if (index >= config->device_name_count) {
            return true;
        }

        for (uint16_t move = index + 1; move < config->device_name_count; ++move) {
            config->device_names[move - 1] = config->device_names[move];
        }

        --config->device_name_count;
        memset(&config->device_names[config->device_name_count], 0, sizeof(config->device_names[config->device_name_count]));
        return true;
    }

    if (index >= config->device_name_count) {
        if (config->device_name_count >= APP_CONFIG_DEVICE_NAME_MAX_COUNT) {
            return false;
        }

        index = config->device_name_count++;
        config->device_names[index].device_id = device_id;
    }

    app_config_copy_device_name(config->device_names[index].name, sizeof(config->device_names[index].name), normalized_name);
    return true;
}

void app_config_normalize(app_config_t *config)
{
    if (config == NULL) {
        return;
    }

    app_config_trim(config->ssid);
    if (config->ssid[0] == '\0') {
        snprintf(config->ssid, sizeof(config->ssid), "%s", "BLEBridge");
    }

    app_config_trim(config->hostname);
    if (config->hostname[0] == '\0') {
        snprintf(config->hostname, sizeof(config->hostname), "%s", "blebridge");
    }

    if (config->channel < 1 || config->channel > 13) {
        config->channel = 3;
    }

    app_config_trim(config->password);
    if (config->security == APP_WIFI_SECURITY_OPEN) {
        config->password[0] = '\0';
    }

    if (config->measurement_retention_seconds < APP_CONFIG_MEASUREMENT_RETENTION_MIN_SECONDS) {
        config->measurement_retention_seconds = APP_CONFIG_MEASUREMENT_RETENTION_MIN_SECONDS;
    }
    if (config->measurement_retention_seconds > APP_CONFIG_MEASUREMENT_RETENTION_MAX_SECONDS) {
        config->measurement_retention_seconds = APP_CONFIG_MEASUREMENT_RETENTION_MAX_SECONDS;
    }
    if (config->measurement_max_count > APP_CONFIG_MEASUREMENT_MAX_COUNT_MAX) {
        config->measurement_max_count = APP_CONFIG_MEASUREMENT_MAX_COUNT_MAX;
    }
    if (config->onewire_poll_interval_seconds < APP_CONFIG_ONEWIRE_POLL_INTERVAL_MIN_SECONDS) {
        config->onewire_poll_interval_seconds = APP_CONFIG_ONEWIRE_POLL_INTERVAL_MIN_SECONDS;
    }
    if (config->onewire_poll_interval_seconds > APP_CONFIG_ONEWIRE_POLL_INTERVAL_MAX_SECONDS) {
        config->onewire_poll_interval_seconds = APP_CONFIG_ONEWIRE_POLL_INTERVAL_MAX_SECONDS;
    }

    if (config->allowed_mac_count > APP_CONFIG_ALLOWED_MAC_MAX_COUNT) {
        config->allowed_mac_count = APP_CONFIG_ALLOWED_MAC_MAX_COUNT;
    }

    if (config->device_name_count > APP_CONFIG_DEVICE_NAME_MAX_COUNT) {
        config->device_name_count = APP_CONFIG_DEVICE_NAME_MAX_COUNT;
    }

    for (uint16_t i = 0; i < config->device_name_count; ++i) {
        app_config_copy_device_name(config->device_names[i].name, sizeof(config->device_names[i].name), config->device_names[i].name);
        app_config_trim(config->device_names[i].name);
    }
}

bool app_config_load(app_config_t *config)
{
    char value[APP_CONFIG_PASSWORD_MAX_LEN];

    if (config == NULL) {
        return false;
    }

    if (!app_storage_init()) {
        return false;
    }

    app_config_defaults(config);
    if (!app_config_ensure_file()) {
        return false;
    }

    ini_gets("network", "mode", "ap", value, sizeof(value), APP_CONFIG_PATH);
    app_config_trim(value);
    config->mode = app_config_parse_mode(value);

    ini_gets("network", "ssid", config->ssid, config->ssid, sizeof(config->ssid), APP_CONFIG_PATH);
    ini_gets("network", "hostname", config->hostname, config->hostname, sizeof(config->hostname), APP_CONFIG_PATH);

    config->channel = (uint32_t)ini_getl("network", "channel", (long)config->channel, APP_CONFIG_PATH);

    ini_gets("network", "security", "open", value, sizeof(value), APP_CONFIG_PATH);
    app_config_trim(value);
    config->security = app_config_parse_security(value);

    ini_gets("network", "password", "", config->password, sizeof(config->password), APP_CONFIG_PATH);
    config->send_narodmon = ini_getl("network", "send_narodmon", 0, APP_CONFIG_PATH) != 0;
    {
        long retention_seconds = ini_getl("measurements",
                                          "retention_seconds",
                                          (long)config->measurement_retention_seconds,
                                          APP_CONFIG_PATH);
        long max_count = ini_getl("measurements",
                                  "max_count",
                                  (long)config->measurement_max_count,
                                  APP_CONFIG_PATH);

        config->measurement_retention_seconds = retention_seconds > 0 ? (uint32_t)retention_seconds : 0;
        if (max_count > (long)APP_CONFIG_MEASUREMENT_MAX_COUNT_MAX) {
            config->measurement_max_count = APP_CONFIG_MEASUREMENT_MAX_COUNT_MAX;
        } else {
            config->measurement_max_count = max_count > 0 ? (uint16_t)max_count : 0;
        }
    }
    {
        long poll_interval_seconds = ini_getl("onewire",
                                              "poll_interval_seconds",
                                              (long)config->onewire_poll_interval_seconds,
                                              APP_CONFIG_PATH);

        config->onewire_poll_interval_seconds = poll_interval_seconds > 0 ? (uint32_t)poll_interval_seconds : 0;
        config->onewire_gpio2_enabled = ini_getl("onewire", "gpio2_enabled", 1, APP_CONFIG_PATH) != 0;
        config->onewire_gpio3_enabled = ini_getl("onewire", "gpio3_enabled", 1, APP_CONFIG_PATH) != 0;
    }

    config->allowed_mac_count = 0;
    for (uint16_t i = 0; i < APP_CONFIG_ALLOWED_MAC_MAX_COUNT; ++i) {
        char key[16];
        char value_mac[APP_CONFIG_ALLOWED_MAC_LEN + 8];
        char canonical[APP_CONFIG_ALLOWED_MAC_LEN];

        snprintf(key, sizeof(key), "mac%u", (unsigned)(i + 1));
        ini_gets("network", key, "", value_mac, sizeof(value_mac), APP_CONFIG_PATH);
        app_config_trim(value_mac);

        if (value_mac[0] == '\0') {
            continue;
        }

        if (app_config_normalize_mac(canonical, sizeof(canonical), value_mac)) {
            app_config_set_allowed_mac(config, canonical, true);
        }
    }

    config->device_name_count = 0;
    for (uint16_t i = 0; i < APP_CONFIG_DEVICE_NAME_MAX_COUNT; ++i) {
        char key[16];
        char value_id[16];
        char value_name[APP_CONFIG_DEVICE_NAME_LEN];
        uint32_t device_id;

        snprintf(key, sizeof(key), "id%u", (unsigned)(i + 1));
        ini_gets("names", key, "", value_id, sizeof(value_id), APP_CONFIG_PATH);
        app_config_trim(value_id);
        if (value_id[0] == '\0') {
            continue;
        }
        device_id = (uint32_t)strtoul(value_id, NULL, 0);

        snprintf(key, sizeof(key), "name%u", (unsigned)(i + 1));
        ini_gets("names", key, "", value_name, sizeof(value_name), APP_CONFIG_PATH);
        app_config_trim(value_name);
        app_config_set_device_name(config, device_id, value_name);
    }
    app_config_normalize(config);

    return true;
}

static bool app_config_write_line(FIL *file, const char *fmt, ...)
{
    char line[1024];
    UINT written;
    int result;
    va_list args;

    va_start(args, fmt);
    result = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (result < 0 || result >= (int)sizeof(line)) {
        return false;
    }

    return f_write(file, line, (UINT)result, &written) == FR_OK && written == (UINT)result;
}

bool app_config_save(const app_config_t *config)
{
    FIL file;

    if (config == NULL) {
        return false;
    }

    if (f_open(&file, APP_CONFIG_PATH, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        return false;
    }

    if (!app_config_write_line(&file,
                               "; BLEBridge configuration file\n"
                               "; mode: ap | client\n"
                               "; ssid: Wi-Fi network name\n"
                               "; hostname: DHCP/NetBIOS style host name reported by the interface\n"
                               "; channel: 1..13, used only in ap mode\n"
                               "; security: open | wpa2\n"
                               "; password: empty for open, at least 8 characters for wpa2\n"
                               "; send_narodmon: 0 | 1\n"
                               "; retention_seconds: measurement storage time, 5..600\n"
                               "; max_count: max measurements per device/field pair, 0..1000; 0 means unlimited\n"
                               "; poll_interval_seconds: 1-Wire DS18B20 polling interval, 5..300\n"
                               "; gpio2_enabled/gpio3_enabled: 0 | 1, enable the corresponding 1-Wire bus\n"
                               "; macN: BLE MAC address without separators, enables device processing\n"
                               "; [names] stores idN/nameN pairs for user-visible sensor names.\n"
                               "; In client mode, the device connects to an existing Wi-Fi network.\n"
                               "; In ap mode, the device creates its own Wi-Fi access point.\n"
                               "; Changes are applied after reboot.\n"
                               "\n"
                               "[network]\n"
                               "mode=%s\n"
                               "ssid=%s\n"
                               "hostname=%s\n"
                               "channel=%lu\n"
                               "security=%s\n"
                               "password=%s\n"
                               "send_narodmon=%d\n",
                               app_config_mode_name(config->mode),
                               config->ssid,
                               config->hostname,
                               (unsigned long)config->channel,
                               app_config_security_name(config->security),
                               config->password,
                               config->send_narodmon ? 1 : 0)) {
        f_close(&file);
        return false;
    }

    for (uint16_t i = 0; i < config->allowed_mac_count; ++i) {
        if (!app_config_write_line(&file,
                                   "mac%u=%s\n",
                                   (unsigned)(i + 1),
                                   config->allowed_macs[i])) {
            f_close(&file);
            return false;
        }
    }

    if (!app_config_write_line(&file,
                               "\n"
                               "[measurements]\n"
                               "retention_seconds=%lu\n"
                               "max_count=%u\n",
                               (unsigned long)config->measurement_retention_seconds,
                               (unsigned)config->measurement_max_count)) {
        f_close(&file);
        return false;
    }

    if (!app_config_write_line(&file,
                               "\n"
                               "[onewire]\n"
                               "poll_interval_seconds=%lu\n"
                               "gpio2_enabled=%d\n"
                               "gpio3_enabled=%d\n",
                               (unsigned long)config->onewire_poll_interval_seconds,
                               config->onewire_gpio2_enabled ? 1 : 0,
                               config->onewire_gpio3_enabled ? 1 : 0)) {
        f_close(&file);
        return false;
    }

    if (!app_config_write_line(&file, "\n[names]\n")) {
        f_close(&file);
        return false;
    }

    for (uint16_t i = 0; i < config->device_name_count; ++i) {
        if (!app_config_write_line(&file,
                                   "id%u=%lu\n"
                                   "name%u=%s\n",
                                   (unsigned)(i + 1),
                                   (unsigned long)config->device_names[i].device_id,
                                   (unsigned)(i + 1),
                                   config->device_names[i].name)) {
            f_close(&file);
            return false;
        }
    }

    f_sync(&file);
    f_close(&file);
    return true;
}

const char *app_config_mode_name(app_wifi_mode_t mode)
{
    return mode == APP_WIFI_MODE_CLIENT ? "client" : "ap";
}

const char *app_config_security_name(app_wifi_security_t security)
{
    return security == APP_WIFI_SECURITY_WPA2 ? "wpa2" : "open";
}
