#ifndef APP_MQTT_H
#define APP_MQTT_H

#include "app_config.h"

void app_mqtt_apply_config(const app_mqtt_config_t *config);
void app_mqtt_poll(uint32_t now_ms);
const char *app_mqtt_status(void);
const char *app_mqtt_last_error(void);
uint32_t app_mqtt_last_send_seconds(void);

#endif
