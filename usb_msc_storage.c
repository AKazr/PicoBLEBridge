#include <string.h>

#include "tusb.h"

#include "status_led.h"
#include "storage_backend.h"

uint8_t tud_msc_get_maxlun_cb(void)
{
    return 0;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    const char vid[] = "BLEBrdge";
    const char pid[] = "Flash Storage";
    const char rev[] = "1.0";

    (void)lun;
    memcpy(vendor_id, vid, sizeof(vid) - 1);
    memcpy(product_id, pid, sizeof(pid) - 1);
    memcpy(product_rev, rev, sizeof(rev) - 1);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    *block_count = storage_backend_block_count();
    *block_size = STORAGE_BLOCK_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    uint32_t byte_offset = lba * STORAGE_BLOCK_SIZE + offset;

    status_led_set(true);
    bool ok = storage_backend_read(byte_offset, buffer, bufsize);
    status_led_set(false);

    if (!ok) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return -1;
    }

    return (int32_t)bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return true;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    uint32_t byte_offset = lba * STORAGE_BLOCK_SIZE + offset;

    status_led_set(true);
    bool ok = storage_backend_write(byte_offset, buffer, bufsize);
    status_led_set(false);

    if (!ok) {
        tud_msc_set_sense(lun, SCSI_SENSE_HARDWARE_ERROR, 0x44, 0x00);
        return -1;
    }

    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)buffer;
    (void)bufsize;

    switch (scsi_cmd[0]) {
    default:
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }
}
