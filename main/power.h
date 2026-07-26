#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void rgb_led_init(void);
/**
 * Rear RGB (active LOW):
 *  - BT on + not connected + idle → blink red/blue (searching)
 *  - playing + BT connected       → solid blue
 *  - playing + speaker            → solid green
 *  - otherwise                    → off
 */
void rgb_led_update(bool bt_enabled, bool bt_connected, bool playing);

void power_init(void);
void power_note_activity(void);
void power_poll(void);
bool power_backlight_on(void);
bool power_consume_wake_redraw(void);
void power_set_ui_ready(bool ready);

#ifdef __cplusplus
}
#endif
