#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void rgb_led_init(void);
void rgb_led_update(bool bt_connected, bool playing);

void power_init(void);
void power_note_activity(void);
void power_poll(void);
bool power_backlight_on(void);
bool power_consume_wake_redraw(void);
void power_set_ui_ready(bool ready);

#ifdef __cplusplus
}
#endif
