#ifndef ONEWIRE_SOURCE_H
#define ONEWIRE_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "lwip/apps/httpd.h"

bool onewire_source_init(void);
void onewire_source_apply_config(const app_config_t *config);
void onewire_source_periodic(uint32_t now_ms);
u16_t onewire_source_write_devices_json(char *insert, int insert_len);
int onewire_source_device_table_used(void);
int onewire_source_device_table_capacity(void);

#endif
