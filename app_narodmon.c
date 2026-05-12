#include <stdio.h>
#include <string.h>

#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/stdlib.h"

#include "app_log.h"
#include "app_config.h"
#include "app_narodmon.h"
#include "app_runtime_config.h"
#include "ble_scanner.h"

#define APP_NARODMON_HOST "narodmon.ru"
#define APP_NARODMON_PORT 8283
#define APP_NARODMON_TIMEOUT_MS 10000u

typedef struct {
    struct tcp_pcb *pcb;
    bool busy;
    bool dns_pending;
    bool connected;
    char payload[APP_NARODMON_PAYLOAD_MAX_LEN];
    uint16_t payload_len;
    uint16_t sent_offset;
    uint32_t deadline_ms;
} app_narodmon_state_t;

static app_narodmon_state_t app_narodmon_state;

static void app_narodmon_reset_state(void)
{
    memset(&app_narodmon_state, 0, sizeof(app_narodmon_state));
}

static void app_narodmon_finish(bool ok, const char *message)
{
    uint16_t payload_len = app_narodmon_state.payload_len;

    if (app_narodmon_state.pcb != NULL) {
        tcp_arg(app_narodmon_state.pcb, NULL);
        tcp_sent(app_narodmon_state.pcb, NULL);
        tcp_recv(app_narodmon_state.pcb, NULL);
        tcp_poll(app_narodmon_state.pcb, NULL, 0);
        tcp_err(app_narodmon_state.pcb, NULL);
        if (tcp_close(app_narodmon_state.pcb) != ERR_OK) {
            tcp_abort(app_narodmon_state.pcb);
        }
    }

    if (message != NULL) {
        if (ok) {
            app_log("%s: %u bytes", message, (unsigned)payload_len);
        } else {
            app_log("%s", message);
        }
    }

    app_narodmon_reset_state();
    (void)ok;
}

static err_t app_narodmon_send_next_chunk(void)
{
    err_t err;
    uint16_t remaining;
    uint16_t chunk_len;

    if (app_narodmon_state.pcb == NULL) {
        return ERR_CLSD;
    }

    remaining = (uint16_t)(app_narodmon_state.payload_len - app_narodmon_state.sent_offset);
    if (remaining == 0) {
        return ERR_OK;
    }

    chunk_len = (uint16_t)LWIP_MIN((u16_t)tcp_sndbuf(app_narodmon_state.pcb), remaining);
    if (chunk_len == 0) {
        return ERR_OK;
    }

    err = tcp_write(app_narodmon_state.pcb,
                    app_narodmon_state.payload + app_narodmon_state.sent_offset,
                    chunk_len,
                    TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        return err;
    }

    app_narodmon_state.sent_offset = (uint16_t)(app_narodmon_state.sent_offset + chunk_len);
    return tcp_output(app_narodmon_state.pcb);
}

static void app_narodmon_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg);
static err_t app_narodmon_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err);
static err_t app_narodmon_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len);
static err_t app_narodmon_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t app_narodmon_poll_cb(void *arg, struct tcp_pcb *tpcb);
static void app_narodmon_err_cb(void *arg, err_t err);

bool app_narodmon_get_device_name(char *buffer, size_t buffer_len)
{
    const char *device_name = app_runtime_config_get_device_name();

    if (buffer == NULL || buffer_len == 0) {
        return false;
    }

    return snprintf(buffer, buffer_len, "%s", device_name) < (int)buffer_len;
}

static bool app_narodmon_write_header(char *payload, size_t payload_size, int *written)
{
    char device_name[APP_CONFIG_HOSTNAME_MAX_LEN];
    int result;

    if (!app_narodmon_get_device_name(device_name, sizeof(device_name))) {
        return false;
    }

    result = snprintf(payload, payload_size, "#%s\n", device_name);
    if (result < 0 || result >= (int)payload_size) {
        return false;
    }

    *written = result;
    return true;
}

int app_narodmon_build_payload(char *buffer, int buffer_len)
{
    int written = 0;
    int metric_length;
    uint32_t uptime_seconds = (uint32_t)((to_ms_since_boot(get_absolute_time()) + 500u) / 1000u);

    if (buffer == NULL || buffer_len <= 0) {
        return -1;
    }

    if (!app_narodmon_write_header(buffer, (size_t)buffer_len, &written)) {
        return -1;
    }

    metric_length = ble_scanner_build_narodmon_payload(buffer + written, buffer_len - written, uptime_seconds);
    if (metric_length <= 0) {
        return metric_length;
    }

    written += metric_length;
    if (written + 3 > buffer_len) {
        return -1;
    }

    buffer[written++] = '#';
    buffer[written++] = '#';
    buffer[written] = '\0';
    return written;
}

static bool app_narodmon_start_connect(const ip_addr_t *ipaddr)
{
    err_t err;

    app_narodmon_state.pcb = tcp_new_ip_type(IP_GET_TYPE(ipaddr));
    if (app_narodmon_state.pcb == NULL) {
        app_log("Narodmon send failed: tcp_new error");
        app_narodmon_reset_state();
        return false;
    }

    tcp_arg(app_narodmon_state.pcb, &app_narodmon_state);
    tcp_recv(app_narodmon_state.pcb, app_narodmon_recv_cb);
    tcp_sent(app_narodmon_state.pcb, app_narodmon_sent_cb);
    tcp_poll(app_narodmon_state.pcb, app_narodmon_poll_cb, 2);
    tcp_err(app_narodmon_state.pcb, app_narodmon_err_cb);

    err = tcp_connect(app_narodmon_state.pcb, ipaddr, APP_NARODMON_PORT, app_narodmon_connected_cb);
    if (err != ERR_OK) {
        app_log("Narodmon send failed: tcp_connect error %d", err);
        tcp_abort(app_narodmon_state.pcb);
        app_narodmon_reset_state();
        return false;
    }

    return true;
}

static void app_narodmon_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    (void)name;
    (void)callback_arg;

    if (!app_narodmon_state.busy) {
        return;
    }

    app_narodmon_state.dns_pending = false;

    if (ipaddr == NULL) {
        app_log("Narodmon send failed: DNS lookup error");
        app_narodmon_reset_state();
        return;
    }

    app_narodmon_start_connect(ipaddr);
}

static err_t app_narodmon_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    (void)arg;
    (void)tpcb;

    if (err != ERR_OK) {
        app_log("Narodmon send failed: connect error %d", err);
        app_narodmon_finish(false, NULL);
        return err;
    }

    app_narodmon_state.connected = true;
    err = app_narodmon_send_next_chunk();
    if (err != ERR_OK) {
        app_log("Narodmon send failed: tcp_write error %d", err);
        app_narodmon_finish(false, NULL);
    }

    return err;
}

static err_t app_narodmon_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    err_t err;

    (void)arg;
    (void)tpcb;
    (void)len;

    if (app_narodmon_state.sent_offset >= app_narodmon_state.payload_len) {
        app_narodmon_finish(true, "Narodmon send OK");
        return ERR_OK;
    }

    err = app_narodmon_send_next_chunk();
    if (err != ERR_OK) {
        app_log("Narodmon send failed: tcp_write error %d", err);
        app_narodmon_finish(false, NULL);
    }

    return err;
}

static err_t app_narodmon_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg;
    (void)tpcb;

    if (p != NULL) {
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
    }

    if (err != ERR_OK) {
        app_log("Narodmon send failed: recv error %d", err);
        app_narodmon_finish(false, NULL);
        return err;
    }

    return ERR_OK;
}

static err_t app_narodmon_poll_cb(void *arg, struct tcp_pcb *tpcb)
{
    (void)arg;
    (void)tpcb;
    return ERR_OK;
}

static void app_narodmon_err_cb(void *arg, err_t err)
{
    (void)arg;
    app_narodmon_state.pcb = NULL;
    app_log("Narodmon send failed: tcp error %d", err);
    app_narodmon_reset_state();
}

bool app_narodmon_start_send(void)
{
    ip_addr_t resolved_ip;
    err_t err;
    int written;

    if (app_narodmon_state.busy) {
        return false;
    }

    written = app_narodmon_build_payload(app_narodmon_state.payload, (int)sizeof(app_narodmon_state.payload));
    if (written < 0) {
        app_log("Narodmon send skipped: unable to get Wi-Fi MAC");
        return false;
    }
    if (written == 0) {
        app_log("Narodmon send skipped: no selected BLE measurements");
        return false;
    }

    app_narodmon_state.busy = true;
    app_narodmon_state.payload_len = (uint16_t)written;
    app_narodmon_state.sent_offset = 0;
    app_narodmon_state.deadline_ms = to_ms_since_boot(get_absolute_time()) + APP_NARODMON_TIMEOUT_MS;
    app_log("Narodmon send started: %u bytes", (unsigned)app_narodmon_state.payload_len);

    err = dns_gethostbyname(APP_NARODMON_HOST, &resolved_ip, app_narodmon_dns_found_cb, &app_narodmon_state);
    if (err == ERR_OK) {
        return app_narodmon_start_connect(&resolved_ip);
    }

    if (err == ERR_INPROGRESS) {
        app_narodmon_state.dns_pending = true;
        return true;
    }

    app_log("Narodmon send failed: DNS lookup error %d", err);
    app_narodmon_reset_state();
    return false;
}

void app_narodmon_poll(uint32_t now_ms)
{
    if (!app_narodmon_state.busy) {
        return;
    }

    if ((int32_t)(now_ms - app_narodmon_state.deadline_ms) >= 0) {
        app_log("Narodmon send failed: timeout");
        if (app_narodmon_state.pcb != NULL) {
            tcp_abort(app_narodmon_state.pcb);
            app_narodmon_state.pcb = NULL;
        }
        app_narodmon_reset_state();
    }
}
