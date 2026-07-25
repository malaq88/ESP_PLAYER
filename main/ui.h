#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SCREEN_HOME = 0,
    UI_SCREEN_BT_DEVICES, /* lista de fones Bluetooth — NÃO é música */
    UI_SCREEN_BROWSER,
    UI_SCREEN_PLAYER
} ui_screen_t;

void ui_init(void);
void ui_draw_splash(void);
void ui_draw_error(const char *msg);

void ui_draw_home(void);
void ui_draw_bt_devices(void);
void ui_draw_browser(void);
void ui_draw_player(void);
void ui_draw_player_progress(void);
void ui_draw_volume(void);
void ui_update_visualizer(void);
void ui_redraw_current(void);

void ui_handle_touch(void);
void ui_bt_tick(void);

void ui_set_screen(ui_screen_t s);
ui_screen_t ui_get_screen(void);

void ui_on_bt_connected_flash(void);
void ui_tick_progress(void);

#ifdef __cplusplus
}
#endif
