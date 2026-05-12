#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "tusb.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/apps/httpd.h"

#include "app_config.h"
#include "app_log.h"
#include "app_narodmon.h"
#include "app_runtime_config.h"
#include "app_storage.h"
#include "app_web.h"
#include "ble_scanner.h"
#include "dhcpserver/dhcpserver.h"
#include "dnsserver/dnsserver.h"
#include "status_led.h"

#define WIFI_CONNECT_TIMEOUT_MS 30000
#define IP_ACQUIRE_TIMEOUT_MS 15000
#define NARODMON_SEND_INTERVAL_MS (5u * 60u * 1000u)
#define POLL_INTERVAL_MS 50
#define USB_HOST_DETECT_TIMEOUT_MS 1500
#define WIFI_CONNECT_LED_BLINK_MS 100
#define REBOOT_DELAY_MS 1000

static const char *firmware_build_version = __DATE__ " " __TIME__;

static dhcp_server_t dhcp_server;
static dns_server_t dns_server;

static void reboot_after_log(const char *reason)
{
    if (reason != NULL) {
        app_log("%s", reason);
    }
    sleep_ms(REBOOT_DELAY_MS);
    watchdog_reboot(0, 0, 0);
    while (true) {
        tight_loop_contents();
    }
}

static bool usb_host_present_at_boot(void)
{
    tusb_rhport_init_t usb_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    absolute_time_t deadline = make_timeout_time_ms(USB_HOST_DETECT_TIMEOUT_MS);

    status_led_set(false);
    if (!tusb_init(0, &usb_init)) {
        return false;
    }

    while (!time_reached(deadline)) {
        tud_task();
        cyw43_arch_poll();

        if (tud_mounted() || tud_connected()) {
            return true;
        }

        sleep_ms(1);
    }

    tud_deinit(0);
    return false;
}

static void run_usb_mass_storage_mode(void)
{
    status_led_set(false);

    while (true) {
        tud_task();
        cyw43_arch_poll();
        sleep_ms(1);
    }
}

static uint32_t app_config_to_cyw43_auth(app_wifi_security_t security)
{
    return security == APP_WIFI_SECURITY_WPA2 ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
}

static bool wait_for_ipv4_address(struct netif *netif, uint32_t timeout_ms)
{
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    while (ip4_addr_isany_val(*netif_ip4_addr(netif))) {
        if (time_reached(deadline)) {
            return false;
        }

        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(POLL_INTERVAL_MS));
    }

    return true;
}

static bool connect_wifi_with_blink(const char *ssid, const char *password, uint32_t auth, uint32_t timeout_ms)
{
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    absolute_time_t next_blink = get_absolute_time();
    bool led_on = false;
    int err;

    err = cyw43_arch_wifi_connect_async(ssid, password, auth);
    if (err != 0) {
        status_led_set(false);
        return false;
    }

    status_led_set(false);

    while (!time_reached(deadline)) {
        int link_status;

        cyw43_arch_poll();

        if (absolute_time_diff_us(next_blink, get_absolute_time()) <= 0) {
            led_on = !led_on;
            status_led_set(led_on);
            next_blink = make_timeout_time_ms(WIFI_CONNECT_LED_BLINK_MS);
        }

        link_status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
        if (link_status == CYW43_LINK_UP || link_status == CYW43_LINK_NOIP || link_status == CYW43_LINK_JOIN) {
            status_led_set(false);
            return true;
        }

        if (link_status == CYW43_LINK_BADAUTH || link_status == CYW43_LINK_FAIL || link_status == CYW43_LINK_NONET) {
            status_led_set(false);
            return false;
        }

        cyw43_arch_wait_for_work_until(make_timeout_time_ms(POLL_INTERVAL_MS));
    }

    status_led_set(false);
    return false;
}

static bool init_network_services(const app_config_t *config, struct netif **active_netif)
{
    uint32_t auth = app_config_to_cyw43_auth(config->security);

    if (config->security == APP_WIFI_SECURITY_WPA2 && strlen(config->password) < 8) {
        app_log("Invalid configuration: WPA2 requires password length of at least 8 characters");
        return false;
    }

    if (config->mode == APP_WIFI_MODE_CLIENT) {
        *active_netif = &cyw43_state.netif[CYW43_ITF_STA];
        netif_set_hostname(*active_netif, config->hostname);
        cyw43_arch_enable_sta_mode();
        app_log("Connecting to Wi-Fi network '%s' (%s)", config->ssid, app_config_security_name(config->security));

        if (!connect_wifi_with_blink(config->ssid,
                                     config->password[0] ? config->password : NULL,
                                     auth,
                                     WIFI_CONNECT_TIMEOUT_MS)) {
            app_log("Wi-Fi connection failed");
            return false;
        }

        if (!wait_for_ipv4_address(*active_netif, IP_ACQUIRE_TIMEOUT_MS)) {
            app_log("Timed out waiting for DHCP address");
            return false;
        }

        app_log("Wi-Fi connected, IP address is %s", ip4addr_ntoa(netif_ip4_addr(*active_netif)));
        return true;
    }

    cyw43_wifi_ap_set_channel(&cyw43_state, (int)config->channel);
    app_log("Enabling Wi-Fi access point '%s' on channel %lu (%s)",
            config->ssid,
            (unsigned long)config->channel,
            app_config_security_name(config->security));
    cyw43_arch_enable_ap_mode(config->ssid,
                              config->password[0] ? config->password : NULL,
                              auth);

    *active_netif = &cyw43_state.netif[CYW43_ITF_AP];
    netif_set_hostname(*active_netif, config->hostname);
    dhcp_server_init(&dhcp_server,
                     (ip_addr_t *)netif_ip_addr4(*active_netif),
                     (ip_addr_t *)netif_ip_netmask4(*active_netif));
    dns_server_init(&dns_server, (ip_addr_t *)netif_ip_addr4(*active_netif));
    app_log("Wi-Fi access point active, IP address is %s", ip4addr_ntoa(netif_ip4_addr(*active_netif)));
    return true;
}

static bool reconnect_client_network(const app_config_t *config, struct netif *active_netif)
{
    uint32_t auth = app_config_to_cyw43_auth(config->security);

    app_log("Wi-Fi link lost, attempting reconnect to '%s'", config->ssid);
    cyw43_arch_enable_sta_mode();

    if (!connect_wifi_with_blink(config->ssid,
                                 config->password[0] ? config->password : NULL,
                                 auth,
                                 WIFI_CONNECT_TIMEOUT_MS)) {
        app_log("Wi-Fi reconnect failed");
        return false;
    }

    if (!wait_for_ipv4_address(active_netif, IP_ACQUIRE_TIMEOUT_MS)) {
        app_log("Wi-Fi reconnect failed: no DHCP address");
        return false;
    }

    app_log("Wi-Fi reconnected, IP address is %s", ip4addr_ntoa(netif_ip4_addr(active_netif)));
    return true;
}

static void run_main_application_mode(void)
{
    struct netif *active_netif;
    uint64_t cpu_busy_acc_us = 0;
    uint64_t cpu_total_acc_us = 0;
    absolute_time_t next_cpu_update;
    absolute_time_t next_narodmon_send;
    const app_config_t *config;

    if (!app_runtime_config_load()) {
        while (true) {
            sleep_ms(1000);
        }
    }
    config = app_runtime_config_get();

    if (!app_log_init()) {
        while (true) {
            sleep_ms(1000);
        }
    }

    app_web_init();
    app_log("Booting BLEBridge");
    app_log("Firmware build: %s", firmware_build_version);

    if (watchdog_caused_reboot()) {
        app_log("Rebooted by Watchdog");
    }

    app_log("Loaded config: mode=%s ssid='%s' hostname='%s' channel=%lu security=%s",
            app_config_mode_name(config->mode),
            config->ssid,
            config->hostname,
            (unsigned long)config->channel,
            app_config_security_name(config->security));
    if (!init_network_services(config, &active_netif)) {
        reboot_after_log("Rebooting after network initialization failure");
    }

    cyw43_arch_lwip_begin();
    httpd_init();
    app_web_register_http_handlers();
    cyw43_arch_lwip_end();

    app_log("HTTP server is listening on http://%s/", ip4addr_ntoa(netif_ip4_addr(active_netif)));
    ble_scanner_init();
    ble_scanner_apply_config(config);
    next_cpu_update = make_timeout_time_ms(1000);
    next_narodmon_send = make_timeout_time_ms(NARODMON_SEND_INTERVAL_MS);
    app_web_set_narodmon_seconds_remaining(NARODMON_SEND_INTERVAL_MS / 1000u);

    status_led_set(true);

    while (true) {
        absolute_time_t loop_start = get_absolute_time();
        uint32_t now_ms;
        absolute_time_t next_poll = make_timeout_time_ms(POLL_INTERVAL_MS);

        cyw43_arch_poll();
        now_ms = to_ms_since_boot(get_absolute_time());
        app_narodmon_poll(now_ms);
        app_web_poll(now_ms);
        ble_scanner_periodic(now_ms);

        config = app_runtime_config_get();
        if (config->mode == APP_WIFI_MODE_CLIENT && cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
            if (!reconnect_client_network(config, active_netif)) {
                reboot_after_log("Rebooting after Wi-Fi reconnect failure");
            }
        }

        {
            int64_t remaining_us = absolute_time_diff_us(get_absolute_time(), next_narodmon_send);
            uint32_t remaining_seconds = 0;

            if (config->send_narodmon && remaining_us > 0) {
                remaining_seconds = (uint32_t)((remaining_us + 999999) / 1000000);
            }

            app_web_set_narodmon_seconds_remaining(remaining_seconds);
        }

        {
            absolute_time_t before_wait = get_absolute_time();
            int64_t busy_us = absolute_time_diff_us(loop_start, before_wait);

            if (busy_us > 0) {
                cpu_busy_acc_us += (uint64_t)busy_us;
            }

            cyw43_arch_wait_for_work_until(next_poll);

            {
                absolute_time_t after_wait = get_absolute_time();
                int64_t total_us = absolute_time_diff_us(loop_start, after_wait);

                if (total_us > 0) {
                    cpu_total_acc_us += (uint64_t)total_us;
                }
            }
        }

        if (time_reached(next_cpu_update)) {
            uint8_t cpu_load = 0;

            if (cpu_total_acc_us > 0) {
                uint64_t percent = (cpu_busy_acc_us * 100u) / cpu_total_acc_us;
                if (percent > 100u) {
                    percent = 100u;
                }
                cpu_load = (uint8_t)percent;
            }

            app_web_set_cpu_load(cpu_load);
            cpu_busy_acc_us = 0;
            cpu_total_acc_us = 0;
            next_cpu_update = make_timeout_time_ms(1000);
        }

        if (config->send_narodmon && time_reached(next_narodmon_send)) {
            app_narodmon_start_send();
            next_narodmon_send = make_timeout_time_ms(NARODMON_SEND_INTERVAL_MS);
            app_web_set_narodmon_seconds_remaining(NARODMON_SEND_INTERVAL_MS / 1000u);
        }
    }
}

int main(void)
{
    if (cyw43_arch_init()) {
        while (true) {
            sleep_ms(1000);
        }
    }

    status_led_set(true);

    if (usb_host_present_at_boot()) {
        run_usb_mass_storage_mode();
    } else {
        run_main_application_mode();
    }

    return 0;
}
