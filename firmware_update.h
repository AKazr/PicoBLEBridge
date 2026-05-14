#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H

#include <stdint.h>

#define FIRMWARE_UPDATE_MAGIC 0x55424250u
#define FIRMWARE_UPDATE_MANIFEST_VERSION 1u
#define FIRMWARE_UPDATE_FLAG_PENDING 0x00000001u

typedef struct firmware_update_manifest {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t flags;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t sequence;
    uint32_t reserved[8];
    uint32_t header_crc32;
} firmware_update_manifest_t;

#endif
