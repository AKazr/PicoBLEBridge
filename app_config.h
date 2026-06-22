#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_CONFIG_SSID_MAX_LEN 33
#define APP_CONFIG_PASSWORD_MAX_LEN 65
#define APP_CONFIG_HOSTNAME_MAX_LEN 64
#define APP_CONFIG_ALLOWED_MAC_MAX_COUNT 128
#define APP_CONFIG_ALLOWED_MAC_LEN 13
#define APP_CONFIG_DEVICE_NAME_MAX_COUNT 128
#define APP_CONFIG_DEVICE_NAME_MAX_BYTES 64
#define APP_CONFIG_DEVICE_NAME_LEN (APP_CONFIG_DEVICE_NAME_MAX_BYTES + 1)
#define APP_CONFIG_MEASUREMENT_RETENTION_MIN_SECONDS 5u
#define APP_CONFIG_MEASUREMENT_RETENTION_MAX_SECONDS 600u
#define APP_CONFIG_MEASUREMENT_RETENTION_DEFAULT_SECONDS 300u
#define APP_CONFIG_MEASUREMENT_MAX_COUNT_MAX 1000u
#define APP_CONFIG_MEASUREMENT_MAX_COUNT_DEFAULT 0u
#define APP_CONFIG_ONEWIRE_POLL_INTERVAL_MIN_SECONDS 5u
#define APP_CONFIG_ONEWIRE_POLL_INTERVAL_MAX_SECONDS 300u
#define APP_CONFIG_ONEWIRE_POLL_INTERVAL_DEFAULT_SECONDS 30u

typedef struct {
    uint32_t device_id;
    char name[APP_CONFIG_DEVICE_NAME_LEN];
} app_config_device_name_t;

typedef enum {
    APP_WIFI_MODE_AP = 0,
    APP_WIFI_MODE_CLIENT
} app_wifi_mode_t;

typedef enum {
    APP_WIFI_SECURITY_OPEN = 0,
    APP_WIFI_SECURITY_WPA2
} app_wifi_security_t;

typedef struct {
    app_wifi_mode_t mode;
    char ssid[APP_CONFIG_SSID_MAX_LEN];
    char hostname[APP_CONFIG_HOSTNAME_MAX_LEN];
    uint32_t channel;
    app_wifi_security_t security;
    char password[APP_CONFIG_PASSWORD_MAX_LEN];
    bool send_narodmon;
    uint32_t measurement_retention_seconds;
    uint16_t measurement_max_count;
    uint32_t onewire_poll_interval_seconds;
    bool onewire_gpio2_enabled;
    bool onewire_gpio3_enabled;
    char allowed_macs[APP_CONFIG_ALLOWED_MAC_MAX_COUNT][APP_CONFIG_ALLOWED_MAC_LEN];
    uint16_t allowed_mac_count;
    app_config_device_name_t device_names[APP_CONFIG_DEVICE_NAME_MAX_COUNT];
    uint16_t device_name_count;
} app_config_t;

bool app_config_load(app_config_t *config);
bool app_config_save(const app_config_t *config);
void app_config_normalize(app_config_t *config);
bool app_config_normalize_mac(char *normalized_mac, size_t normalized_mac_size, const char *input);
bool app_config_set_allowed_mac(app_config_t *config, const char *normalized_mac, bool enabled);
const char *app_config_get_device_name(const app_config_t *config);
const char *app_config_get_sensor_name(const app_config_t *config, uint32_t device_id);
bool app_config_set_device_name(app_config_t *config, uint32_t device_id, const char *name);
const char *app_config_mode_name(app_wifi_mode_t mode);
const char *app_config_security_name(app_wifi_security_t security);

#endif
