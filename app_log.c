#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "pico/stdlib.h"

#include "app_storage.h"
#include "app_log.h"
#include "status_led.h"

#define APP_LOG_MAX_SIZE_BYTES (48u * 1024u)

static FIL log_file;
static bool log_ready;

static bool app_log_open_current_file(void)
{
    FRESULT fr;

    fr = f_open(&log_file, "0:/log.txt", FA_OPEN_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        return false;
    }

    fr = f_lseek(&log_file, f_size(&log_file));
    if (fr != FR_OK) {
        f_close(&log_file);
        return false;
    }

    return true;
}

static bool app_log_rotate_if_needed(size_t incoming_len)
{
    FRESULT fr;
    FSIZE_t current_size;

    current_size = f_size(&log_file);
    if ((current_size + (FSIZE_t)incoming_len) <= APP_LOG_MAX_SIZE_BYTES) {
        return true;
    }

    f_sync(&log_file);
    f_close(&log_file);

    fr = f_unlink("0:/log1.txt");
    if (fr != FR_OK && fr != FR_NO_FILE) {
        return app_log_open_current_file();
    }

    fr = f_rename("0:/log.txt", "0:/log1.txt");
    if (fr != FR_OK) {
        return app_log_open_current_file();
    }

    return app_log_open_current_file();
}

bool app_log_init(void)
{
    if (!app_storage_init()) {
        return false;
    }

    if (!app_log_open_current_file()) {
        return false;
    }

    log_ready = true;
    return true;
}

void app_log_close(void)
{
    if (!log_ready) {
        return;
    }

    f_sync(&log_file);
    f_close(&log_file);
    log_ready = false;
}

void app_log(const char *fmt, ...)
{
    char line[256];
    UINT written;
    va_list args;
    bool led_was_on;
    uint32_t uptime_ms;
    int prefix_len;
    size_t message_offset;

    if (!log_ready) {
        return;
    }

    uptime_ms = to_ms_since_boot(get_absolute_time());
    prefix_len = snprintf(line,
                          sizeof(line),
                          "[%lu.%03lu] ",
                          (unsigned long)(uptime_ms / 1000u),
                          (unsigned long)(uptime_ms % 1000u));
    message_offset = (prefix_len > 0 && (size_t)prefix_len < sizeof(line))
                         ? (size_t)prefix_len
                         : 0u;

    va_start(args, fmt);
    vsnprintf(line + message_offset, sizeof(line) - message_offset, fmt, args);
    va_end(args);

    size_t len = strlen(line);
    if (len >= sizeof(line) - 2) {
        len = sizeof(line) - 2;
        line[len] = '\0';
    }

    line[len++] = '\n';
    line[len] = '\0';

    //led_was_on = status_led_suspend_off();
    if (app_log_rotate_if_needed(len) &&
        f_write(&log_file, line, len, &written) == FR_OK &&
        written == len) {
        f_sync(&log_file);
    }
   // status_led_resume(led_was_on);
}
