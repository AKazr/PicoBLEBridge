#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdbool.h>

bool app_log_init(void);
void app_log_close(void);
void app_log(const char *fmt, ...);

#endif
