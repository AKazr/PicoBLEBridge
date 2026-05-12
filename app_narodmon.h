#ifndef APP_NARODMON_H
#define APP_NARODMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define APP_NARODMON_PAYLOAD_MAX_LEN 4096

bool app_narodmon_get_device_name(char *buffer, size_t buffer_len);
int app_narodmon_build_payload(char *buffer, int buffer_len);
bool app_narodmon_start_send(void);
void app_narodmon_poll(uint32_t now_ms);

#endif
