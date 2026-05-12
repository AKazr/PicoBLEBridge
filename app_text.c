#include "app_text.h"

static int app_text_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

void app_text_json_escape(char *dst, size_t dst_size, const char *src)
{
    size_t written = 0;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (*src != '\0' && written + 1 < dst_size) {
        char ch = *src++;

        if ((ch == '\\' || ch == '"') && written + 2 < dst_size) {
            dst[written++] = '\\';
            dst[written++] = ch;
        } else if ((unsigned char)ch >= 0x20u) {
            dst[written++] = ch;
        }
    }

    dst[written] = '\0';
}

void app_text_url_decode(char *text)
{
    char *src = text;
    char *dst = text;

    if (text == NULL) {
        return;
    }

    while (*src != '\0') {
        if (*src == '+') {
            *dst++ = ' ';
            ++src;
            continue;
        }

        if (*src == '%' && src[1] != '\0' && src[2] != '\0') {
            int hi = app_text_hex_value(src[1]);
            int lo = app_text_hex_value(src[2]);

            if (hi >= 0 && lo >= 0) {
                *dst++ = (char)((hi << 4) | lo);
                src += 3;
                continue;
            }
        }

        *dst++ = *src++;
    }

    *dst = '\0';
}
