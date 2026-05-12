#ifndef APP_TEXT_H
#define APP_TEXT_H

#include <stddef.h>

void app_text_json_escape(char *dst, size_t dst_size, const char *src);
void app_text_url_decode(char *text);

#endif
