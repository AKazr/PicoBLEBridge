#include <stdbool.h>
#include <string.h>

#include "ff.h"
#include "lwip/apps/fs.h"

#define APP_WEB_LOGS_URI "/logs.txt"
#define APP_WEB_LOG_READ_CHUNK 1024u

typedef struct {
    FIL previous_file;
    FIL current_file;
    FSIZE_t previous_size;
    FSIZE_t previous_read;
    bool previous_open;
    bool current_open;
    bool in_use;
} app_web_log_stream_t;

static app_web_log_stream_t app_web_log_stream;

static int app_web_log_read_file(FIL *source, char *buffer, UINT count)
{
    UINT bytes_read = 0;

    if (f_read(source, buffer, count, &bytes_read) != FR_OK) {
        return FS_READ_EOF;
    }
    return (int)bytes_read;
}

int fs_open_custom(struct fs_file *file, const char *name)
{
    app_web_log_stream_t *stream = &app_web_log_stream;
    uint64_t total_size;

    if (strcmp(name, APP_WEB_LOGS_URI) != 0 || stream->in_use) {
        return 0;
    }

    memset(stream, 0, sizeof(*stream));
    stream->in_use = true;

    if (f_open(&stream->previous_file, "0:/log1.txt", FA_READ) == FR_OK) {
        stream->previous_open = true;
        stream->previous_size = f_size(&stream->previous_file);
    }

    if (f_open(&stream->current_file, "0:/log.txt", FA_READ) == FR_OK) {
        stream->current_open = true;
    }

    total_size = (uint64_t)stream->previous_size;
    if (stream->current_open) {
        total_size += (uint64_t)f_size(&stream->current_file);
    }

    memset(file, 0, sizeof(*file));
    file->len = (int)total_size;
    file->flags = FS_FILE_FLAGS_HEADER_PERSISTENT;
    file->pextension = stream;
    return 1;
}

void fs_close_custom(struct fs_file *file)
{
    app_web_log_stream_t *stream;

    if (file == NULL || file->pextension == NULL) {
        return;
    }

    stream = (app_web_log_stream_t *)file->pextension;
    if (stream->previous_open) {
        f_close(&stream->previous_file);
    }
    if (stream->current_open) {
        f_close(&stream->current_file);
    }

    memset(stream, 0, sizeof(*stream));
    file->pextension = NULL;
}

int fs_read_custom(struct fs_file *file, char *buffer, int count)
{
    app_web_log_stream_t *stream;
    UINT read_count;
    int result;

    if (file == NULL || buffer == NULL || count <= 0 || file->pextension == NULL) {
        return FS_READ_EOF;
    }

    stream = (app_web_log_stream_t *)file->pextension;
    read_count = (UINT)count;
    if (read_count > APP_WEB_LOG_READ_CHUNK) {
        read_count = APP_WEB_LOG_READ_CHUNK;
    }

    if (stream->previous_open && stream->previous_read < stream->previous_size) {
        FSIZE_t remaining = stream->previous_size - stream->previous_read;

        if ((FSIZE_t)read_count > remaining) {
            read_count = (UINT)remaining;
        }
        result = app_web_log_read_file(&stream->previous_file, buffer, read_count);
        if (result > 0) {
            stream->previous_read += (FSIZE_t)result;
            file->index += result;
            return result;
        }
        stream->previous_read = stream->previous_size;
    }

    if (!stream->current_open) {
        return FS_READ_EOF;
    }

    result = app_web_log_read_file(&stream->current_file, buffer, read_count);
    if (result > 0) {
        file->index += result;
    }
    return result > 0 ? result : FS_READ_EOF;
}
