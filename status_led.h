#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdbool.h>

void status_led_set(bool on);
bool status_led_suspend_off(void);
void status_led_resume(bool was_on);

#endif
