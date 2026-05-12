#ifndef STORAGE_BACKEND_H
#define STORAGE_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#define STORAGE_BLOCK_SIZE 512u

uint32_t storage_backend_size_bytes(void);
uint32_t storage_backend_block_count(void);
const uint8_t *storage_backend_xip_base(void);
bool storage_backend_read(uint32_t byte_offset, void *buffer, uint32_t size);
bool storage_backend_write(uint32_t byte_offset, const void *buffer, uint32_t size);

#endif
