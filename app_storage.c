#include <stddef.h>
#include <stdint.h>

#include "ff.h"

#include "app_storage.h"
#include "status_led.h"

static FATFS app_fs;
static bool app_storage_ready;

bool app_storage_init(void)
{
    FRESULT fr;
    bool led_was_on;

    if (app_storage_ready) {
        return true;
    }

    led_was_on = status_led_suspend_off();
    status_led_set(true);

    fr = f_mount(&app_fs, "0:", 1);
    if (fr == FR_NO_FILESYSTEM) {
        MKFS_PARM mkfs_opt = {
            /* For the current storage size this selects FAT16, not FAT32. */
            .fmt = FM_FAT,
            .n_fat = 1,
            .align = 0,
            .n_root = 0,
            .au_size = 0
        };
        static uint8_t work[4096];

        fr = f_mkfs("0:", &mkfs_opt, work, sizeof(work));
        if (fr != FR_OK) {
            status_led_resume(led_was_on);
            return false;
        }

        fr = f_mount(&app_fs, "0:", 1);
    }

    app_storage_ready = (fr == FR_OK);
    status_led_resume(led_was_on);
    return app_storage_ready;
}

void app_storage_deinit(void)
{
    if (!app_storage_ready) {
        return;
    }

    f_unmount("0:");
    app_storage_ready = false;
}

bool app_storage_is_ready(void)
{
    return app_storage_ready;
}

bool app_storage_get_space_info(uint64_t *total_bytes, uint64_t *free_bytes)
{
    FATFS *fs;
    DWORD free_clusters;
    uint64_t total_clusters;
    uint64_t sectors_per_cluster;

    if (!app_storage_ready) {
        return false;
    }

    if (f_getfree("0:", &free_clusters, &fs) != FR_OK || fs == NULL) {
        return false;
    }

    total_clusters = (uint64_t)(fs->n_fatent - 2u);
    sectors_per_cluster = (uint64_t)fs->csize;

    if (total_bytes != NULL) {
        *total_bytes = total_clusters * sectors_per_cluster * FF_MIN_SS;
    }

    if (free_bytes != NULL) {
        *free_bytes = (uint64_t)free_clusters * sectors_per_cluster * FF_MIN_SS;
    }

    return true;
}
