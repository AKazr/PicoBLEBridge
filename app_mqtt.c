#include <stdio.h>
#include <string.h>

#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "pico/cyw43_arch.h"

#include "app_log.h"
#include "app_mqtt.h"
#include "measurement_store.h"

#define MQTT_CONNECT_TIMEOUT_MS 15000u
#define MQTT_PUBLISH_TIMEOUT_MS 35000u
#define MQTT_RETRY_MIN_MS 5000u
#define MQTT_RETRY_MAX_MS 60000u

typedef enum {
    MQTT_DISABLED,
    MQTT_WAITING,
    MQTT_RESOLVING,
    MQTT_CONNECTING,
    MQTT_CONNECTED,
} app_mqtt_state_t;

static app_mqtt_config_t mqtt_config;
static bool config_changed;
static mqtt_client_t *client;
static app_mqtt_state_t state;
static char client_id[24];
static char bridge_mac[13];
static char last_error[64];
static uint32_t last_send_seconds;
static uint32_t deadline_ms;
static uint32_t next_connect_ms;
static uint32_t next_send_ms;
static uint32_t retry_ms = MQTT_RETRY_MIN_MS;
static uintptr_t dns_generation;
static ip_addr_t broker_address;
static bool dns_done;
static bool dns_valid;
static bool connection_changed;
static mqtt_connection_status_t connection_result;
static bool publish_pending;
static bool publish_done;
static err_t publish_result;
static measurement_sample_t samples[MEASUREMENT_STORE_VIEW_SIZE];
static int sample_count;
static int sample_index;

static bool time_due(uint32_t now_ms, uint32_t target_ms)
{
    return (int32_t)(now_ms - target_ms) >= 0;
}

static void disconnect_client(void)
{
    if (client != NULL) {
        mqtt_disconnect(client);
    }
    /* DNS cannot be cancelled. Ignore callbacks from previous attempts. */
    ++dns_generation;
    dns_done = false;
    connection_changed = false;
    publish_pending = false;
    publish_done = false;
    sample_count = 0;
    sample_index = 0;
}

static void retry_later(uint32_t now_ms, const char *message, int code)
{
    disconnect_client();
    snprintf(last_error, sizeof(last_error), "%s (%d)", message, code);
    app_log("MQTT: %s; retry in %lu s", last_error, (unsigned long)(retry_ms / 1000u));
    state = MQTT_WAITING;
    next_connect_ms = now_ms + retry_ms;
    if (retry_ms < MQTT_RETRY_MAX_MS) {
        retry_ms *= 2;
        if (retry_ms > MQTT_RETRY_MAX_MS) {
            retry_ms = MQTT_RETRY_MAX_MS;
        }
    }
}

/* Callbacks only record results; publishing and flash logging run in poll(). */
static void dns_found(const char *name, const ip_addr_t *address, void *arg)
{
    (void)name;
    if (state != MQTT_RESOLVING || (uintptr_t)arg != dns_generation) {
        return;
    }
    dns_valid = address != NULL;
    if (dns_valid) {
        broker_address = *address;
    }
    dns_done = true;
}

static void connection_callback(mqtt_client_t *mqtt, void *arg, mqtt_connection_status_t result)
{
    (void)mqtt;
    (void)arg;
    connection_result = result;
    connection_changed = true;
}

static void publish_callback(void *arg, err_t result)
{
    (void)arg;
    publish_result = result;
    publish_done = true;
}

void app_mqtt_apply_config(const app_mqtt_config_t *config)
{
    if (mqtt_config.enabled != config->enabled ||
        mqtt_config.port != config->port ||
        mqtt_config.interval_seconds != config->interval_seconds ||
        strcmp(mqtt_config.host, config->host) != 0 ||
        strcmp(mqtt_config.username, config->username) != 0 ||
        strcmp(mqtt_config.password, config->password) != 0) {
        mqtt_config = *config;
        config_changed = true;
    }
}

void app_mqtt_poll(uint32_t now_ms)
{
    err_t err;

    if (config_changed) {
        disconnect_client();
        config_changed = false;
        retry_ms = MQTT_RETRY_MIN_MS;
        next_connect_ms = now_ms;
        last_send_seconds = 0;
        last_error[0] = '\0';
        state = mqtt_config.enabled ? MQTT_WAITING : MQTT_DISABLED;
        if (mqtt_config.enabled && (mqtt_config.host[0] == '\0' ||
            (mqtt_config.password[0] != '\0' && mqtt_config.username[0] == '\0'))) {
            snprintf(last_error, sizeof(last_error), "Invalid MQTT settings");
            state = MQTT_DISABLED;
        }
        app_log("MQTT: %s", last_error[0] != '\0' ? last_error : app_mqtt_status());
    }

    if (state == MQTT_DISABLED) {
        return;
    }

    if (connection_changed) {
        connection_changed = false;
        if (connection_result != MQTT_CONNECT_ACCEPTED) {
            retry_later(now_ms, "Connection failed", connection_result);
            return;
        }
        state = MQTT_CONNECTED;
        next_send_ms = now_ms;
        retry_ms = MQTT_RETRY_MIN_MS;
        app_log("MQTT connected");
    }

    if (state == MQTT_WAITING) {
        if (!time_due(now_ms, next_connect_ms)) {
            return;
        }
        if (client == NULL) {
            client = mqtt_client_new();
            if (client == NULL) {
                retry_later(now_ms, "Client allocation failed", ERR_MEM);
                return;
            }
        }
        uint8_t mac[6];
        if (cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac) != 0) {
            retry_later(now_ms, "MAC unavailable", ERR_IF);
            return;
        }
        snprintf(bridge_mac, sizeof(bridge_mac), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        snprintf(client_id, sizeof(client_id), "BLEB%s", bridge_mac);
        state = MQTT_RESOLVING;
        deadline_ms = now_ms + MQTT_CONNECT_TIMEOUT_MS;
        dns_done = false;
        ++dns_generation;
        err = dns_gethostbyname(mqtt_config.host, &broker_address, dns_found, (void *)dns_generation);
        if (err == ERR_OK) {
            dns_valid = true;
            dns_done = true;
        } else if (err != ERR_INPROGRESS) {
            retry_later(now_ms, "DNS lookup failed", err);
            return;
        }
    }

    if (state == MQTT_RESOLVING && dns_done) {
        dns_done = false;
        if (!dns_valid) {
            retry_later(now_ms, "DNS lookup failed", ERR_VAL);
            return;
        }
        struct mqtt_connect_client_info_t info = {0};
        info.client_id = client_id;
        info.client_user = mqtt_config.username[0] != '\0' ? mqtt_config.username : NULL;
        info.client_pass = mqtt_config.password[0] != '\0' ? mqtt_config.password : NULL;
        info.keep_alive = 60;
        state = MQTT_CONNECTING;
        deadline_ms = now_ms + MQTT_CONNECT_TIMEOUT_MS;
        err = mqtt_client_connect(client, &broker_address, mqtt_config.port, connection_callback, NULL, &info);
        if (err != ERR_OK) {
            retry_later(now_ms, "Connect failed", err);
            return;
        }
    }

    if (state == MQTT_RESOLVING || state == MQTT_CONNECTING) {
        if (time_due(now_ms, deadline_ms)) {
            retry_later(now_ms, "Connect timeout", ERR_TIMEOUT);
        }
        return;
    }
    if (state != MQTT_CONNECTED) {
        return;
    }

    if (publish_done) {
        publish_done = false;
        publish_pending = false;
        if (publish_result != ERR_OK) {
            retry_later(now_ms, "Publish failed", publish_result);
            return;
        }
        ++sample_index;
        deadline_ms = now_ms + MQTT_PUBLISH_TIMEOUT_MS;
        if (sample_index == sample_count) {
            last_send_seconds = now_ms / 1000u;
            last_error[0] = '\0';
            app_log("MQTT sent %u values", (unsigned)sample_count);
            sample_count = 0;
        }
    }

    if (sample_count == 0) {
        if (!time_due(now_ms, next_send_ms)) {
            return;
        }
        sample_count = measurement_store_snapshot(samples, MEASUREMENT_STORE_VIEW_SIZE, now_ms);
        sample_index = 0;
        next_send_ms = now_ms + mqtt_config.interval_seconds * 1000u;
        deadline_ms = now_ms + MQTT_PUBLISH_TIMEOUT_MS;
        if (sample_count == 0) {
            return;
        }
    }

    if (time_due(now_ms, deadline_ms)) {
        retry_later(now_ms, "Publish timeout", ERR_TIMEOUT);
        return;
    }
    if (!publish_pending) {
        char topic[80];
        char value[32];
        const measurement_sample_t *sample = &samples[sample_index];
        snprintf(topic, sizeof(topic), "blebridge/%s/%lu/%s", bridge_mac,
                 (unsigned long)sample->device_id, measurement_field_name(sample->field_type));
        int length = snprintf(value, sizeof(value), "%.3f", (double)sample->value);
        if (length < 0 || length >= (int)sizeof(value)) {
            retry_later(now_ms, "Value too long", ERR_VAL);
            return;
        }
        err = mqtt_publish(client, topic, value, (u16_t)length, 1, 0, publish_callback, NULL);
        if (err == ERR_OK) {
            publish_pending = true;
        } else if (err != ERR_MEM) {
            retry_later(now_ms, "Publish failed", err);
        }
    }
}

const char *app_mqtt_status(void)
{
    switch (state) {
    case MQTT_WAITING: return "Waiting to connect";
    case MQTT_RESOLVING: return "Resolving host";
    case MQTT_CONNECTING: return "Connecting";
    case MQTT_CONNECTED: return "Connected";
    default: return "Disabled";
    }
}

const char *app_mqtt_last_error(void)
{
    return last_error;
}

uint32_t app_mqtt_last_send_seconds(void)
{
    return last_send_seconds;
}
