#include <string.h>

#include "ble/le_device_db_tlv.h"
#include "btstack_memory.h"
#include "btstack_tlv.h"
#include "hci.h"
#include "pico/btstack_cyw43.h"
#include "pico/btstack_hci_transport_cyw43.h"
#include "pico/btstack_run_loop_async_context.h"

#define APP_BTSTACK_TLV_ENTRY_COUNT 16u
#define APP_BTSTACK_TLV_VALUE_SIZE 128u

typedef struct {
    bool used;
    uint32_t tag;
    uint16_t size;
    uint8_t value[APP_BTSTACK_TLV_VALUE_SIZE];
} app_btstack_tlv_entry_t;

static app_btstack_tlv_entry_t app_btstack_tlv_entries[APP_BTSTACK_TLV_ENTRY_COUNT];

static int app_btstack_tlv_get(void *context, uint32_t tag, uint8_t *buffer, uint32_t buffer_size)
{
    app_btstack_tlv_entry_t *entries = context;

    for (size_t i = 0; i < APP_BTSTACK_TLV_ENTRY_COUNT; ++i) {
        if (!entries[i].used || entries[i].tag != tag) {
            continue;
        }
        if (buffer != NULL && buffer_size > 0) {
            uint32_t copy_size = entries[i].size < buffer_size ? entries[i].size : buffer_size;
            memcpy(buffer, entries[i].value, copy_size);
        }
        return entries[i].size;
    }
    return 0;
}

static int app_btstack_tlv_store(void *context, uint32_t tag, const uint8_t *data, uint32_t data_size)
{
    app_btstack_tlv_entry_t *entries = context;
    app_btstack_tlv_entry_t *free_entry = NULL;

    if (data_size > APP_BTSTACK_TLV_VALUE_SIZE || (data_size > 0 && data == NULL)) {
        return 1;
    }
    for (size_t i = 0; i < APP_BTSTACK_TLV_ENTRY_COUNT; ++i) {
        if (entries[i].used && entries[i].tag == tag) {
            free_entry = &entries[i];
            break;
        }
        if (!entries[i].used && free_entry == NULL) {
            free_entry = &entries[i];
        }
    }
    if (free_entry == NULL) {
        return 1;
    }
    free_entry->used = true;
    free_entry->tag = tag;
    free_entry->size = (uint16_t)data_size;
    if (data_size > 0) {
        memcpy(free_entry->value, data, data_size);
    }
    return 0;
}

static void app_btstack_tlv_delete(void *context, uint32_t tag)
{
    app_btstack_tlv_entry_t *entries = context;

    for (size_t i = 0; i < APP_BTSTACK_TLV_ENTRY_COUNT; ++i) {
        if (entries[i].used && entries[i].tag == tag) {
            memset(&entries[i], 0, sizeof(entries[i]));
            return;
        }
    }
}

static const btstack_tlv_t app_btstack_tlv = {
    .get_tag = app_btstack_tlv_get,
    .store_tag = app_btstack_tlv_store,
    .delete_tag = app_btstack_tlv_delete,
};

bool btstack_cyw43_init(async_context_t *context)
{
    memset(app_btstack_tlv_entries, 0, sizeof(app_btstack_tlv_entries));
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_async_context_get_instance(context));
    hci_init(hci_transport_cyw43_instance(), NULL);
    btstack_tlv_set_instance(&app_btstack_tlv, app_btstack_tlv_entries);
#ifdef ENABLE_BLE
    le_device_db_tlv_configure(&app_btstack_tlv, app_btstack_tlv_entries);
#endif
    return true;
}

void btstack_cyw43_deinit(async_context_t *context)
{
    (void)context;
    hci_power_control(HCI_POWER_OFF);
    hci_close();
    btstack_run_loop_async_context_deinit();
    btstack_run_loop_deinit();
    btstack_memory_deinit();
}
