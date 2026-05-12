#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

#define USB_VID 0x2E8A
#define USB_PID 0x0011
#define USB_BCD 0x0200

enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL
};

enum {
    USB_STR_LANGID = 0,
    USB_STR_MANUFACTURER,
    USB_STR_PRODUCT,
    USB_STR_SERIAL,
    USB_STR_MSC
};

#define EPNUM_MSC_OUT 0x01
#define EPNUM_MSC_IN  0x81

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = TUSB_CLASS_MSC,
    .bDeviceSubClass = MSC_SUBCLASS_SCSI,
    .bDeviceProtocol = MSC_PROTOCOL_BOT,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = USB_STR_MANUFACTURER,
    .iProduct = USB_STR_PRODUCT,
    .iSerialNumber = USB_STR_SERIAL,
    .bNumConfigurations = 1
};

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 250),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, USB_STR_MSC, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

static char usb_serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

static const char *const string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },
    "Raspberry Pi",
    "BLEBridge Storage",
    usb_serial_str,
    "Flash Storage",
};

static uint16_t desc_str[40];

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    size_t chr_count;

    (void)langid;

    if (!usb_serial_str[0]) {
        pico_get_unique_board_id_string(usb_serial_str, sizeof(usb_serial_str));
    }

    if (index == USB_STR_LANGID) {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        const char *str;
        size_t max_count;

        if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
            return NULL;
        }

        str = string_desc_arr[index];
        chr_count = strlen(str);
        max_count = (sizeof(desc_str) / sizeof(desc_str[0])) - 1;
        if (chr_count > max_count) {
            chr_count = max_count;
        }

        for (size_t i = 0; i < chr_count; ++i) {
            desc_str[1 + i] = str[i];
        }
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
