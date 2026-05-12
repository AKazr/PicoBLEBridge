#ifndef APP_WEB_H
#define APP_WEB_H

#include <stddef.h>
#include <stdint.h>

#include "lwip/apps/httpd.h"

extern const char *app_web_ssi_tags[];
extern const size_t app_web_ssi_tag_count;

void app_web_init(void);
void app_web_set_cpu_load(uint8_t load_percent);
void app_web_set_narodmon_seconds_remaining(uint32_t seconds_remaining);
void app_web_poll(uint32_t now_ms);
u16_t app_web_ssi_handler(int index, char *insert, int insert_len);
void app_web_register_http_handlers(void);

#endif
