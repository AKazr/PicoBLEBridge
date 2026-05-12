#include "pico/cyw43_arch.h"

#include "status_led.h"

static bool status_led_state;

void status_led_set(bool on)
{
    status_led_state = on;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}

bool status_led_suspend_off(void)
{
    bool was_on = status_led_state;

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
    return was_on;
}

void status_led_resume(bool was_on)
{
    status_led_set(was_on);
}
