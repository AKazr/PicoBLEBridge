#include "app_runtime_config.h"

static app_config_t app_runtime_config;

bool app_runtime_config_load(void)
{
    return app_config_load(&app_runtime_config);
}

const app_config_t *app_runtime_config_get(void)
{
    return &app_runtime_config;
}

void app_runtime_config_set(const app_config_t *config)
{
    if (config != NULL) {
        app_runtime_config = *config;
    }
}

const char *app_runtime_config_get_device_name(void)
{
    return app_config_get_device_name(&app_runtime_config);
}

const char *app_runtime_config_get_sensor_name(uint32_t device_id)
{
    return app_config_get_sensor_name(&app_runtime_config, device_id);
}
