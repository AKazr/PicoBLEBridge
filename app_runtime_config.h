#ifndef APP_RUNTIME_CONFIG_H
#define APP_RUNTIME_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

bool app_runtime_config_load(void);
const app_config_t *app_runtime_config_get(void);
void app_runtime_config_set(const app_config_t *config);
const char *app_runtime_config_get_device_name(void);
const char *app_runtime_config_get_sensor_name(uint32_t device_id);

#endif
