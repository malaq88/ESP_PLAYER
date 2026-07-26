#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BT_SCAN_MAX   24
#define BT_NAME_BUF   40

typedef struct {
    char    name[BT_NAME_BUF];
    uint8_t addr[6];
    int     rssi;
} bt_scan_entry_t;

#ifdef __cplusplus
extern "C" {
#endif

void bt_source_start(void);
bool bt_source_is_connected(void);
bool bt_source_is_enabled(void);
/** Enable/disable Classic BT + A2DP (async; may take a moment). */
void bt_source_set_enabled(bool enabled);
bool bt_source_is_busy(void);
const char *bt_source_peer_name(void);
void bt_source_set_volume_percent(int pct);

int bt_source_scan_count(void);
bool bt_source_scan_get(int idx, bt_scan_entry_t *out);
void bt_source_choose(const uint8_t addr[6], const char *name);

/** Block until connected; calls ui redraw callbacks. */
typedef void (*bt_picker_draw_fn)(bool connecting);
typedef void (*bt_picker_tick_fn)(void);
void bt_source_run_picker(bt_picker_draw_fn draw, bt_picker_tick_fn tick);

#ifdef __cplusplus
}
#endif
