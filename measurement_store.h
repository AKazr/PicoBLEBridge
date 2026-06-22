#ifndef MEASUREMENT_STORE_H
#define MEASUREMENT_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "lwip/apps/httpd.h"

typedef enum {
    MEASUREMENT_FIELD_NONE = 0,
    MEASUREMENT_FIELD_BATTERY = 1,
    MEASUREMENT_FIELD_TEMPERATURE = 2,
    MEASUREMENT_FIELD_HUMIDITY = 3,
    MEASUREMENT_FIELD_1WIRE_TEMPERATURE = 4,
} measurement_field_t;

void measurement_store_init(uint32_t now_ms);
void measurement_store_configure(uint32_t retention_seconds, uint16_t max_measurements_per_series);
void measurement_store_periodic(uint32_t now_ms);
bool measurement_store_add(uint32_t device_id, measurement_field_t field_type, float value, uint32_t timestamp_ms);
u16_t measurement_store_write_json(char *insert, int insert_len);
int measurement_store_build_narodmon_payload(char *insert, int insert_len, uint32_t uptime_seconds);
int measurement_store_table_used(void);
int measurement_store_table_capacity(void);
int measurement_store_view_used(void);
int measurement_store_view_capacity(void);

#endif
