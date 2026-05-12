#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

bool app_storage_init(void);
void app_storage_deinit(void);
bool app_storage_is_ready(void);
bool app_storage_get_space_info(uint64_t *total_bytes, uint64_t *free_bytes);

#endif
