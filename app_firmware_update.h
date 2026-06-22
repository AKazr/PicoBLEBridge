#ifndef APP_FIRMWARE_UPDATE_H
#define APP_FIRMWARE_UPDATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_FIRMWARE_UPDATE_OK = 0,
    APP_FIRMWARE_UPDATE_INVALID_SIZE,
    APP_FIRMWARE_UPDATE_INVALID_HEADER,
    APP_FIRMWARE_UPDATE_IMAGE_TOO_LARGE,
    APP_FIRMWARE_UPDATE_INCOMPLETE,
    APP_FIRMWARE_UPDATE_NOT_ACTIVE,
    APP_FIRMWARE_UPDATE_INVALID_OFFSET,
    APP_FIRMWARE_UPDATE_FLASH_ERROR,
} app_firmware_update_result_t;

app_firmware_update_result_t app_firmware_update_begin(uint32_t content_len);
app_firmware_update_result_t app_firmware_update_write(const uint8_t *data, uint32_t size);
app_firmware_update_result_t app_firmware_update_finish(void);
void app_firmware_update_abort(app_firmware_update_result_t result);
bool app_firmware_update_is_active(void);
uint32_t app_firmware_update_received(void);
uint32_t app_firmware_update_total_size(void);
const char *app_firmware_update_result_text(app_firmware_update_result_t result);

#endif
