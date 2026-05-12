#include "hardware/flash.h"
#include "ff.h"
#include "diskio.h"

#include "storage_backend.h"

DSTATUS disk_initialize(BYTE pdrv)
{
    return (pdrv == 0) ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    uint32_t offset = (uint32_t)sector * STORAGE_BLOCK_SIZE;
    uint32_t size = (uint32_t)count * STORAGE_BLOCK_SIZE;

    if (pdrv != 0) {
        return RES_PARERR;
    }

    return storage_backend_read(offset, buff, size) ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    uint32_t offset = (uint32_t)sector * STORAGE_BLOCK_SIZE;
    uint32_t size = (uint32_t)count * STORAGE_BLOCK_SIZE;

    if (pdrv != 0) {
        return RES_PARERR;
    }

    return storage_backend_write(offset, buff, size) ? RES_OK : RES_ERROR;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0) {
        return RES_PARERR;
    }

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;

    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = storage_backend_block_count();
        return RES_OK;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = STORAGE_BLOCK_SIZE;
        return RES_OK;

    case GET_BLOCK_SIZE:
        *(DWORD *)buff = FLASH_SECTOR_SIZE / STORAGE_BLOCK_SIZE;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
