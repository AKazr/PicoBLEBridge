#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"

#include "minIni.h"

static char *trim_left(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }

    return text;
}

static void trim_right(char *text)
{
    size_t len = strlen(text);

    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[--len] = '\0';
    }
}

static void copy_default(char *buffer, int buffer_size, const char *def_value)
{
    if (buffer_size <= 0) {
        return;
    }

    if (def_value == NULL) {
        buffer[0] = '\0';
        return;
    }

    snprintf(buffer, (size_t)buffer_size, "%s", def_value);
}

static bool read_line(FIL *file, char *buffer, size_t buffer_size)
{
    size_t pos = 0;

    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    while (pos < (buffer_size - 1)) {
        UINT read_count = 0;
        char ch;

        if (f_read(file, &ch, 1, &read_count) != FR_OK || read_count == 0) {
            break;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            break;
        }

        buffer[pos++] = ch;
    }

    buffer[pos] = '\0';
    return pos > 0 || f_eof(file) == 0;
}

int ini_gets(const char *section, const char *key, const char *def_value, char *buffer, int buffer_size, const char *filename)
{
    FIL file;
    FRESULT fr;
    char line[192];
    char current_section[64];
    bool section_match;

    if (buffer == NULL || buffer_size <= 0 || key == NULL || filename == NULL) {
        return 0;
    }

    copy_default(buffer, buffer_size, def_value);
    current_section[0] = '\0';
    section_match = (section == NULL || section[0] == '\0');

    fr = f_open(&file, filename, FA_READ);
    if (fr != FR_OK) {
        return (int)strlen(buffer);
    }

    while (read_line(&file, line, sizeof(line))) {
        char *text = trim_left(line);
        char *equals;

        trim_right(text);
        if (*text == '\0' || *text == ';' || *text == '#') {
            continue;
        }

        if (*text == '[') {
            char *end = strchr(text + 1, ']');

            if (end == NULL) {
                continue;
            }

            *end = '\0';
            snprintf(current_section, sizeof(current_section), "%s", trim_left(text + 1));
            trim_right(current_section);
            section_match = (section != NULL) && strcmp(current_section, section) == 0;
            continue;
        }

        if (!section_match) {
            continue;
        }

        equals = strchr(text, '=');
        if (equals == NULL) {
            continue;
        }

        *equals = '\0';
        trim_right(text);
        if (strcmp(text, key) != 0) {
            continue;
        }

        text = trim_left(equals + 1);
        trim_right(text);
        snprintf(buffer, (size_t)buffer_size, "%s", text);
        f_close(&file);
        return (int)strlen(buffer);
    }

    f_close(&file);
    return (int)strlen(buffer);
}

long ini_getl(const char *section, const char *key, long def_value, const char *filename)
{
    char value[32];
    char *end;
    long parsed;

    ini_gets(section, key, "", value, sizeof(value), filename);
    if (value[0] == '\0') {
        return def_value;
    }

    parsed = strtol(value, &end, 10);
    if (end == value) {
        return def_value;
    }

    return parsed;
}
