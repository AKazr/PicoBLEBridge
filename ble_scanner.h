#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "lwip/apps/httpd.h"

void ble_scanner_init(void);
void ble_scanner_apply_config(const app_config_t *config);
void ble_scanner_periodic(uint32_t now_ms);
int ble_scanner_device_count(void);
u16_t ble_scanner_write_devices_json(char *insert, int insert_len);
u16_t ble_scanner_write_measurements_json(char *insert, int insert_len);
int ble_scanner_device_table_used(void);
int ble_scanner_device_table_capacity(void);
int ble_scanner_measurement_table_used(void);
int ble_scanner_measurement_table_capacity(void);
int ble_scanner_measurement_view_used(void);
int ble_scanner_measurement_view_capacity(void);

#endif
