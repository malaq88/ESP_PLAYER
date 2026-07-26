#include "ui.h"
#include "board.h"
#include "theme.h"
#include "display.h"
#include "touch.h"
#include "power.h"
#include "sd_music.h"
#include "audio_player.h"
#include "audio_local.h"
#include "bt_source.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ui";

static ui_screen_t s_screen = UI_SCREEN_HOME;
static ui_screen_t s_stack[6];
static int s_stack_n;

enum BrowseLevel { BROWSE_ALBUMS, BROWSE_TRACKS };
static BrowseLevel s_browse_level = BROWSE_ALBUMS;
static int s_album_scroll;
static int s_track_scroll;
static int64_t s_last_touch_ms;
static int64_t s_last_progress_ms;
static int64_t s_last_vis_ms;
static int s_bt_pick_scroll;
static int s_last_bt_count = -1;
static bool s_bt_connecting;
static bool s_last_bt_enabled = true;
static bool s_last_bt_busy = false;
static int s_last_bar_h[NUM_VIS_BARS];
static bool s_vis_valid;
static audio_route_t s_last_route = AUDIO_ROUTE_NONE;
static int64_t s_last_route_ms;

/* Layout */
static const int HEADER_H = 28;
static const int LIST_Y_BASE = 40;
static const int FOOTER_H = 44;
static const int ITEM_H = 28;

static const int BACK_X = 4, BACK_Y = 4, BACK_W = 52, BACK_H = 22;

/* Full-width NOW bar under header — Y-only hit (reliable on CYD) */
static const int NOW_BAR_Y0 = 28;
static const int NOW_BAR_H = 34;

/* Player layout */
static const int PL_TITLE_Y = 34;
static const int PL_SPEC_Y = 66;
static const int PL_SPEC_H = 52;
static const int PL_SEEK_Y = 132;
static const int PL_VOL_Y = 280;

/* Transport hit zones (keep Y-based play — most reliable on CYD) */
static const int TR_PLAY_Y0 = 168;
static const int TR_PLAY_Y1 = 228;
static const int TR_ROW2_Y0 = 232;
static const int TR_ROW2_Y1 = 274;
static const int TR_MID_X = 120;

static const int HIT_PAD = 4;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static int footer_y(void) { return BOARD_SCR_H - FOOTER_H; }

static bool music_is_active(void)
{
    return audio_state() == PLAYER_PLAYING || audio_state() == PLAYER_PAUSED;
}

/** List starts lower when the NOW bar is visible. */
static int browser_list_y(void)
{
    return music_is_active() ? (NOW_BAR_Y0 + NOW_BAR_H + 2) : LIST_Y_BASE;
}

/** Compact SPK / BT badge (header or volume row). */
static void draw_route_badge(int x, int y, uint16_t bg)
{
    audio_route_t r = audio_output_route();
    const bool bt = (r == AUDIO_ROUTE_BLUETOOTH);
    const char *label = bt ? "BT" : "SPK";
    uint16_t fg = bt ? theme::ICON_BT : theme::ICON_MUSIC;
    int tw = display_text_width(label, 1);
    int bw = tw + 10;
    int bh = 14;
    display_fill_rect(x, y, bw, bh, theme::SURFACE2);
    display_fill_rect(x, y, 3, bh, fg);
    display_draw_text(x + 6, y + 3, label, fg, theme::SURFACE2, 1);
    (void)bg;
}

static bool hit_rect(int16_t tx, int16_t ty, int x, int y, int w, int h, int pad)
{
    return tx >= (x - pad) && tx < (x + w + pad) &&
           ty >= (y - pad) && ty < (y + h + pad);
}

static int visible_slots(void)
{
    int vis = (footer_y() - browser_list_y()) / ITEM_H;
    return vis < 1 ? 1 : vis;
}

static void format_ms(char *buf, size_t n, uint32_t sec)
{
    uint32_t m = sec / 60u;
    uint32_t s = sec % 60u;
    snprintf(buf, n, "%lu:%02lu", (unsigned long)m, (unsigned long)s);
}

static void truncate_str(char *dst, size_t dst_n, const char *src, int max_chars)
{
    if (!src) src = "-";
    strncpy(dst, src, dst_n - 1);
    dst[dst_n - 1] = '\0';
    if ((int)strlen(dst) > max_chars && max_chars > 2) {
        dst[max_chars - 2] = '\0';
        strcat(dst, "..");
    }
}

static void nav_push(ui_screen_t from)
{
    if (s_stack_n < (int)(sizeof(s_stack) / sizeof(s_stack[0])))
        s_stack[s_stack_n++] = from;
}

static void nav_goto(ui_screen_t next)
{
    if (next == s_screen) return;
    nav_push(s_screen);
    s_screen = next;
}

static bool nav_back(void)
{
    if (s_stack_n <= 0) {
        s_screen = UI_SCREEN_HOME;
        return true;
    }
    s_screen = s_stack[--s_stack_n];
    return true;
}

static void draw_chrome_panel(int x, int y, int w, int h)
{
    /* Flat elevated card — no neon frame */
    display_fill_round_rect(x, y, w, h, 10, theme::SURFACE);
}

static void draw_back_btn(void)
{
    display_fill_round_rect(BACK_X, BACK_Y, BACK_W, BACK_H, 6, theme::SURFACE2);
    display_draw_text(BACK_X + 8, BACK_Y + 7, "< Back", theme::TEXT, theme::SURFACE2, 1);
}

static void draw_now_btn(void)
{
    if (!music_is_active()) return;

    display_fill_rect(0, NOW_BAR_Y0, BOARD_SCR_W, NOW_BAR_H, theme::SURFACE);
    display_fill_rect(0, NOW_BAR_Y0, 3, NOW_BAR_H, theme::ACCENT);

    char line[40];
    const char *name = "Now Playing";
    if (sd_music_track_count() > 0) {
        char raw[40];
        sd_music_get_display_name(sd_music_track_path(audio_current_track()), raw, sizeof(raw));
        truncate_str(line, sizeof(line), raw, 22);
        name = line;
    }
    display_draw_text(12, NOW_BAR_Y0 + 6, "Now playing", theme::MUTED, theme::SURFACE, 1);
    display_draw_text(12, NOW_BAR_Y0 + 18, name, theme::TEXT, theme::SURFACE, 1);
    display_draw_text(210, NOW_BAR_Y0 + 12, ">", theme::MUTED, theme::SURFACE, 2);
}

/** Full-width bar under header — only needs Y (same idea as the PLAY button). */
static bool hit_now_btn(int16_t tx, int16_t ty)
{
    (void)tx;
    if (!music_is_active()) return false;
    return ty >= NOW_BAR_Y0 && ty < (NOW_BAR_Y0 + NOW_BAR_H);
}

static void draw_nav_footer(int page, int total_pages)
{
    int fy = footer_y();
    display_fill_rect(0, fy, BOARD_SCR_W, FOOTER_H, theme::BG);
    display_draw_hline(0, fy, BOARD_SCR_W, theme::DIVIDER);

    const int gap = 6;
    const int btn_y = fy + 4;
    const int btn_h = FOOTER_H - 8;
    const int btn_w = (BOARD_SCR_W - 16 - gap) / 2;

    display_fill_round_rect(8, btn_y, btn_w, btn_h, 8, theme::SURFACE);
    display_draw_text(8 + 10, btn_y + btn_h / 2 - 4, "< PREV", theme::TEXT, theme::SURFACE, 1);

    const int next_x = 8 + btn_w + gap;
    display_fill_round_rect(next_x, btn_y, btn_w, btn_h, 8, theme::SURFACE);
    {
        const char *next_lbl = "NEXT >";
        int ntw = display_text_width(next_lbl, 1);
        display_draw_text(next_x + btn_w - ntw - 10, btn_y + btn_h / 2 - 4,
                          next_lbl, theme::TEXT, theme::SURFACE, 1);
    }

    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", page, total_pages < 1 ? 1 : total_pages);
    int tw = display_text_width(pg, 1);
    display_fill_rect((BOARD_SCR_W - tw) / 2 - 2, fy - 1, tw + 4, 10, theme::BG);
    display_draw_text((BOARD_SCR_W - tw) / 2, fy, pg, theme::MUTED, theme::BG, 1);
}

/** Footer: left half = previous page, right half = next page. */
static bool footer_hit_prev(int16_t tx, int16_t ty)
{
    return ty >= footer_y() && tx < (BOARD_SCR_W / 2);
}

static bool footer_hit_next(int16_t tx, int16_t ty)
{
    return ty >= footer_y() && tx >= (BOARD_SCR_W / 2);
}

static void draw_list_row(int y, const char *label, bool playing)
{
    display_fill_rect(0, y, BOARD_SCR_W, ITEM_H, theme::BG);
    display_draw_text(12, y + 10, label, playing ? theme::ACCENT : theme::TEXT, theme::BG, 1);
    display_draw_hline(12, y + ITEM_H - 1, BOARD_SCR_W - 24, theme::DIVIDER);
}

void ui_init(void)
{
    s_screen = UI_SCREEN_HOME;
    s_stack_n = 0;
    s_browse_level = BROWSE_ALBUMS;
    s_album_scroll = 0;
    s_track_scroll = 0;
    s_bt_pick_scroll = 0;
    s_last_touch_ms = 0;
    s_last_progress_ms = 0;
    s_last_vis_ms = 0;
    s_vis_valid = false;
    s_last_bt_count = -1;
    s_bt_connecting = false;
    memset(s_last_bar_h, 0, sizeof(s_last_bar_h));
}

void ui_set_screen(ui_screen_t s)
{
    s_screen = s;
    s_stack_n = 0;
}

ui_screen_t ui_get_screen(void) { return s_screen; }

void ui_draw_error(const char *msg)
{
    display_fill(theme::BG);
    display_draw_text_centered(140, 220, msg ? msg : "Error", theme::WARN, theme::BG, 2);
}

void ui_draw_splash(void)
{
    display_fill(theme::BG);
    display_draw_text_centered(100, 220, "ESP PLAYER", theme::TEXT, theme::BG, 2);
    display_draw_text_centered(128, 220, "Music", theme::MUTED, theme::BG, 1);
    int cx = BOARD_SCR_W / 2;
    display_fill_circle(cx, 190, 22, theme::SURFACE2);
    display_fill_triangle(cx - 6, 178, cx - 6, 202, cx + 14, 190, theme::ACCENT);

    const size_t need = 200 * 218 * 2;
    uint16_t *raw = (uint16_t *)malloc(need);
    if (raw && sd_music_read_raw("/sd/guara565.raw", raw, need)) {
        display_fill(theme::BG);
        display_push_image(20, 40, 200, 218, raw);
    }
    free(raw);
}

void ui_on_bt_connected_flash(void)
{
    display_fill(theme::BG);
    display_draw_text_centered(150, 220, "Bluetooth OK", theme::OK, theme::BG, 2);
    vTaskDelay(pdMS_TO_TICKS(350));
}

/* ── Home — two stacked cards (hit by Y, not X) ───────────── */

static const int HOME_MUSIC_Y = 56;
static const int HOME_BT_Y = 150;
static const int HOME_CARD_H = 78;
static const int HOME_CARD_X = 16;
static const int HOME_CARD_W = (BOARD_SCR_W - 32);

static void draw_home_card(int y, uint16_t icon_col, const char *title, const char *sub)
{
    draw_chrome_panel(HOME_CARD_X, y, HOME_CARD_W, HOME_CARD_H);
    display_fill_circle(HOME_CARD_X + 36, y + HOME_CARD_H / 2, 18, theme::SURFACE2);
    display_fill_circle(HOME_CARD_X + 36, y + HOME_CARD_H / 2, 10, icon_col);
    display_draw_text(HOME_CARD_X + 70, y + 24, title, theme::TEXT, theme::SURFACE, 2);
    display_draw_text(HOME_CARD_X + 70, y + 50, sub, theme::MUTED, theme::SURFACE, 1);
}

void ui_draw_home(void)
{
    display_fill(theme::BG);
    display_fill_rect(0, 0, BOARD_SCR_W, HEADER_H, theme::SURFACE);
    display_draw_hline(0, HEADER_H - 1, BOARD_SCR_W, theme::DIVIDER);
    display_draw_text(12, 8, "Home", theme::TEXT, theme::SURFACE, 1);
    draw_route_badge(168, 7, theme::SURFACE);
    s_last_route = audio_output_route();

    bool bt = bt_source_is_connected();
    bool bt_on = bt_source_is_enabled();
    const char *music_sub = bt ? "Output: Bluetooth" : "Output: Speaker";
    const char *bt_sub = !bt_on ? "Powered off"
                         : bt   ? "Connected"
                                : "Connect headset";

    draw_home_card(HOME_MUSIC_Y, theme::ICON_MUSIC, "Music", music_sub);
    draw_home_card(HOME_BT_Y, theme::ICON_BT, "Bluetooth", bt_sub);
    display_draw_text(HOME_CARD_X + 30, HOME_BT_Y + HOME_CARD_H / 2 - 4, "BT",
                      theme::TEXT, theme::ICON_BT, 1);

    if (audio_state() == PLAYER_PLAYING || audio_state() == PLAYER_PAUSED) {
        display_draw_text_centered(250, 220, "Now Playing >", theme::MUTED, theme::BG, 1);
    }
}

/* ── Bluetooth Devices screen (NOT music browser) ─────────── */

/* Full-width power row — easy to hit on resistive touch */
static const int BT_PWR_Y = 50;
static const int BT_PWR_H = 40;
static const int BT_TOGGLE_X = 162;
static const int BT_TOGGLE_W = 66;
static const int BT_TOGGLE_H = 30;
static const int BT_LIST_Y = 100;
static const int BT_ITEM_H = 44;

static void draw_bt_toggle(bool on)
{
    display_fill_round_rect(8, BT_PWR_Y, BOARD_SCR_W - 16, BT_PWR_H, 8, theme::SURFACE);
    display_draw_text(16, BT_PWR_Y + 14, "Bluetooth", theme::TEXT, theme::SURFACE, 1);
    display_draw_text(110, BT_PWR_Y + 14, on ? "ON" : "OFF",
                      on ? theme::OK : theme::MUTED, theme::SURFACE, 1);

    int ty = BT_PWR_Y + (BT_PWR_H - BT_TOGGLE_H) / 2;
    display_fill_round_rect(BT_TOGGLE_X, ty, BT_TOGGLE_W, BT_TOGGLE_H, 14,
                            on ? theme::ACCENT : theme::SURFACE2);
    int knob_x = on ? (BT_TOGGLE_X + BT_TOGGLE_W - 15) : (BT_TOGGLE_X + 15);
    display_fill_circle(knob_x, ty + BT_TOGGLE_H / 2, 11, theme::TEXT);
}

void ui_draw_bt_devices(void)
{
    display_fill(theme::BG);
    display_fill_rect(0, 0, BOARD_SCR_W, 48, theme::SURFACE);
    display_draw_hline(0, 47, BOARD_SCR_W, theme::DIVIDER);
    draw_back_btn();
    display_draw_text(64, 8, "Bluetooth", theme::TEXT, theme::SURFACE, 1);
    display_draw_text(64, 26, "Pair a headset", theme::MUTED, theme::SURFACE, 1);

    const bool enabled = bt_source_is_enabled();
    s_last_bt_enabled = enabled;
    s_last_bt_busy = false;

    draw_bt_toggle(enabled);

    if (!enabled) {
        display_fill_rect(12, BT_LIST_Y, BOARD_SCR_W - 24, 90, theme::SURFACE);
        display_draw_text(20, BT_LIST_Y + 20, "Bluetooth is OFF", theme::MUTED, theme::SURFACE, 1);
        display_draw_text(20, BT_LIST_Y + 44, "Music uses SPEAK amp", theme::TEXT, theme::SURFACE, 1);
        display_draw_text(20, BT_LIST_Y + 64, "LED: green while playing", theme::MUTED, theme::SURFACE, 1);
        s_last_bt_count = -1;
        return;
    }

    if (bt_source_is_connected()) {
        s_bt_connecting = false;
        display_fill_rect(12, BT_LIST_Y, BOARD_SCR_W - 24, 100, theme::SURFACE);
        display_draw_text(20, BT_LIST_Y + 14, "CONNECTED", theme::OK, theme::SURFACE, 1);
        const char *peer = bt_source_peer_name();
        char line[40];
        truncate_str(line, sizeof(line), peer && peer[0] ? peer : "Headset", 28);
        display_draw_text(20, BT_LIST_Y + 40, line, theme::TEXT, theme::SURFACE, 1);
        display_draw_text(20, BT_LIST_Y + 64, "Audio route: Bluetooth", theme::MUTED, theme::SURFACE, 1);
        display_draw_text(20, BT_LIST_Y + 82, "LED: blue while playing", theme::MUTED, theme::SURFACE, 1);
        s_last_bt_count = bt_source_scan_count();
        return;
    }

    if (s_bt_connecting) {
        display_draw_text(12, BT_LIST_Y - 16, "Connecting...", theme::ACCENT, theme::BG, 1);
    } else {
        int n = bt_source_scan_count();
        char status[40];
        if (n <= 0)
            strncpy(status, "Scanning... LED blinks R/B", sizeof(status));
        else
            snprintf(status, sizeof(status), "%d device(s) found", n);
        display_draw_text(12, BT_LIST_Y - 16, status, theme::MUTED, theme::BG, 1);
    }

    int vis = (footer_y() - BT_LIST_Y) / BT_ITEM_H;
    if (vis < 1) vis = 1;
    int n = bt_source_scan_count();
    int total_pages = (n + vis - 1) / vis;
    if (total_pages < 1) total_pages = 1;
    int page = n == 0 ? 1 : (s_bt_pick_scroll / vis) + 1;

    for (int row = 0; row < vis; row++) {
        int idx = s_bt_pick_scroll + row;
        if (idx >= n) break;
        bt_scan_entry_t e;
        if (!bt_source_scan_get(idx, &e)) break;
        int y = BT_LIST_Y + row * BT_ITEM_H;
        display_fill_round_rect(8, y + 2, BOARD_SCR_W - 16, BT_ITEM_H - 4, 8, theme::SURFACE);
        display_fill_circle(28, y + BT_ITEM_H / 2, 8, theme::SURFACE2);
        display_fill_circle(28, y + BT_ITEM_H / 2, 4, theme::ICON_BT);
        char line[40];
        truncate_str(line, sizeof(line), e.name, 24);
        display_draw_text(48, y + 10, line, theme::TEXT, theme::SURFACE, 1);
        display_draw_text(48, y + 26, "Tap to connect", theme::MUTED, theme::SURFACE, 1);
    }
    draw_nav_footer(page, total_pages);
    s_last_bt_count = n;
}

void ui_bt_tick(void)
{
    if (s_screen != UI_SCREEN_BT_DEVICES) return;

    bool enabled = bt_source_is_enabled();
    if (enabled != s_last_bt_enabled) {
        ui_draw_bt_devices();
        return;
    }

    if (!enabled) return;

    if (bt_source_is_connected()) {
        if (s_bt_connecting) {
            s_bt_connecting = false;
            ui_draw_bt_devices();
        }
        return;
    }
    int n = bt_source_scan_count();
    if (n != s_last_bt_count && !s_bt_connecting)
        ui_draw_bt_devices();
}

/* ── Browser ─────────────────────────────────────────────── */

void ui_draw_browser(void)
{
    display_fill(theme::BG);
    display_fill_rect(0, 0, BOARD_SCR_W, HEADER_H, theme::SURFACE);
    draw_back_btn();

    const char *title = (s_browse_level == BROWSE_ALBUMS) ? "Albums" : "< Albums";
    display_draw_text(64, 8, title, theme::TEXT, theme::SURFACE, 1);
    display_draw_hline(0, HEADER_H - 1, BOARD_SCR_W, theme::DIVIDER);
    draw_now_btn();

    const int list_y = browser_list_y();
    int item_count = (s_browse_level == BROWSE_ALBUMS)
                         ? sd_music_album_count()
                         : sd_music_browse_track_count();
    int scroll = (s_browse_level == BROWSE_ALBUMS) ? s_album_scroll : s_track_scroll;
    int vis = visible_slots();
    int total_pages = (item_count + vis - 1) / vis;
    if (total_pages < 1) total_pages = 1;
    int page = (scroll / vis) + 1;

    for (int i = 0; i < vis; i++) {
        int idx = scroll + i;
        if (idx >= item_count) break;
        int y = list_y + i * ITEM_H;
        char name[40];
        bool playing = false;
        if (s_browse_level == BROWSE_ALBUMS) {
            truncate_str(name, sizeof(name), sd_music_album_name(idx), 28);
        } else {
            int pi = sd_music_browse_track_playlist_index(idx);
            sd_music_get_display_name(sd_music_track_path(pi), name, sizeof(name));
            truncate_str(name, sizeof(name), name, 28);
            playing = (pi == audio_current_track() && music_is_active());
        }
        draw_list_row(y, name, playing);
    }
    draw_nav_footer(page, total_pages);
}

/* ── Player ──────────────────────────────────────────────── */

static void vis_geometry(int *ix, int *iy, int *iw, int *ih, int *barW, int *gap, int *maxH)
{
    *ix = 24;
    *iy = PL_SPEC_Y;
    *iw = BOARD_SCR_W - 48;
    *ih = PL_SPEC_H;
    *gap = 3;
    *barW = (*iw - *gap * (NUM_VIS_BARS - 1)) / NUM_VIS_BARS;
    if (*barW < 2) *barW = 2;
    *maxH = *ih - 4;
}

static void draw_vis_full(void)
{
    int ix, iy, iw, ih, barW, gap, maxH;
    vis_geometry(&ix, &iy, &iw, &ih, &barW, &gap, &maxH);
    display_fill_rect(ix, iy, iw, ih, theme::SPEC_BG);
    const float *bands = audio_vis_bands();
    int baseY = iy + ih;
    for (int b = 0; b < NUM_VIS_BARS; b++) {
        int x = ix + b * (barW + gap);
        int hh = (int)(bands[b] * (float)maxH);
        if (hh < 0) hh = 0;
        if (hh > maxH) hh = maxH;
        s_last_bar_h[b] = hh;
        if (hh > 0) display_fill_rect(x, baseY - hh, barW, hh, theme::SPEC_BAR);
    }
    s_vis_valid = true;
}

static void draw_vis_dirty(void)
{
    int ix, iy, iw, ih, barW, gap, maxH;
    vis_geometry(&ix, &iy, &iw, &ih, &barW, &gap, &maxH);
    const float *bands = audio_vis_bands();
    int baseY = iy + ih;
    for (int b = 0; b < NUM_VIS_BARS; b++) {
        int x = ix + b * (barW + gap);
        int hh = (int)(bands[b] * (float)maxH);
        if (hh < 0) hh = 0;
        if (hh > maxH) hh = maxH;
        int prev = s_last_bar_h[b];
        if (hh == prev) continue;
        if (hh > prev)
            display_fill_rect(x, baseY - hh, barW, hh - prev, theme::SPEC_BAR);
        else
            display_fill_rect(x, baseY - prev, barW, prev - hh, theme::SPEC_BG);
        s_last_bar_h[b] = hh;
    }
}

void ui_update_visualizer(void)
{
    if (s_screen != UI_SCREEN_PLAYER) return;
    if (now_ms() - s_last_vis_ms < 85) return;
    s_last_vis_ms = now_ms();
    if (audio_state() != PLAYER_PLAYING) audio_vis_decay();
    else audio_vis_compute();
    if (!s_vis_valid) draw_vis_full();
    else draw_vis_dirty();
}

void ui_draw_player_progress(void)
{
    if (s_screen != UI_SCREEN_PLAYER) return;
    uint32_t el = audio_elapsed_sec();
    uint32_t dur = audio_duration_sec();

    const int bx = 20, by = PL_SEEK_Y, bw = BOARD_SCR_W - 40, bh = 6;
    display_fill_rect(0, by - 10, BOARD_SCR_W, 36, theme::BG);

    /* Soft track capsule */
    display_fill_round_rect(bx, by - 1, bw, bh + 2, 4, theme::SURFACE2);
    display_fill_round_rect(bx + 1, by, bw - 2, bh, 3, theme::BAR_TRACK);

    int knob_x = bx + 3;
    if (dur > 0) {
        uint32_t fw = (uint32_t)(((uint64_t)el * (uint64_t)(bw - 6)) / (uint64_t)dur);
        if (fw > (uint32_t)(bw - 6)) fw = (uint32_t)(bw - 6);
        if (fw > 0)
            display_fill_round_rect(bx + 1, by, (int)fw + 2, bh, 3, theme::BAR_FILL);
        knob_x = bx + 3 + (int)fw;
    }
    display_fill_circle(knob_x, by + bh / 2, 6, theme::TEXT);

    char tEl[12], tTot[12];
    format_ms(tEl, sizeof(tEl), el);
    if (dur > 0) format_ms(tTot, sizeof(tTot), dur);
    else strncpy(tTot, "--:--", sizeof(tTot));
    display_draw_text(bx, by + 14, tEl, theme::MUTED, theme::BG, 1);
    int tw = display_text_width(tTot, 1);
    display_draw_text(bx + bw - tw, by + 14, tTot, theme::MUTED, theme::BG, 1);
    s_last_progress_ms = now_ms();
}

void ui_draw_volume(void)
{
    int y = PL_VOL_Y;
    display_fill_rect(0, y, BOARD_SCR_W, 40, theme::BG);

    display_fill_round_rect(10, y + 2, BOARD_SCR_W - 20, 34, 10, theme::SURFACE);

    display_draw_text(22, y + 12, "-", theme::MUTED, theme::SURFACE, 2);

    audio_route_t r = audio_output_route();
    const char *tag = (r == AUDIO_ROUTE_BLUETOOTH) ? "BT" : "SPK";
    char vbuf[20];
    snprintf(vbuf, sizeof(vbuf), "%s %d%%", tag, audio_get_volume_percent());
    int tw = display_text_width(vbuf, 1);
    display_draw_text((BOARD_SCR_W - tw) / 2, y + 8, vbuf, theme::MUTED, theme::SURFACE, 1);

    const int bx = 70, by = y + 24, bw = BOARD_SCR_W - 140, bh = 4;
    display_fill_round_rect(bx, by, bw, bh, 2, theme::BAR_TRACK);
    int fill = (audio_get_volume_percent() * bw) / 100;
    if (fill > 0) display_fill_round_rect(bx, by, fill, bh, 2, theme::ACCENT);

    display_draw_text(204, y + 12, "+", theme::MUTED, theme::SURFACE, 2);
}

static void draw_capsule_btn(int x, int y, int w, int h, uint16_t fill, uint16_t border)
{
    (void)border;
    display_fill_round_rect(x, y, w, h, 10, fill);
}

static void draw_transport(void)
{
    display_fill_rect(0, TR_PLAY_Y0 - 4, BOARD_SCR_W, TR_ROW2_Y1 - TR_PLAY_Y0 + 8, theme::BG);

    /* Center PLAY — solid disc, Spotify-style hero */
    {
        const int cx = BOARD_SCR_W / 2;
        const int cy = (TR_PLAY_Y0 + TR_PLAY_Y1) / 2;
        display_fill_circle(cx, cy, 26, theme::ACCENT);
        if (audio_state() == PLAYER_PLAYING) {
            display_fill_rect(cx - 9, cy - 11, 6, 22, theme::BG);
            display_fill_rect(cx + 3, cy - 11, 6, 22, theme::BG);
        } else {
            display_fill_triangle(cx - 7, cy - 12, cx - 7, cy + 12, cx + 13, cy, theme::BG);
        }
    }

    /* PREV | NEXT */
    {
        const int y = TR_ROW2_Y0 + 2;
        const int h = TR_ROW2_Y1 - TR_ROW2_Y0 - 4;
        const int gap = 8;
        const int w = (BOARD_SCR_W - 24 - gap) / 2;

        draw_capsule_btn(12, y, w, h, theme::SURFACE, theme::DIVIDER);
        int pcx = 12 + w / 2;
        int pcy = y + h / 2;
        display_fill_triangle(pcx - 8, pcy, pcx + 6, pcy - 10, pcx + 6, pcy + 10, theme::TEXT);
        display_fill_rect(pcx - 12, pcy - 10, 3, 20, theme::TEXT);

        draw_capsule_btn(12 + w + gap, y, w, h, theme::SURFACE, theme::DIVIDER);
        int ncx = 12 + w + gap + w / 2;
        int ncy = y + h / 2;
        display_fill_triangle(ncx - 6, ncy - 10, ncx - 6, ncy + 10, ncx + 8, ncy, theme::TEXT);
        display_fill_rect(ncx + 9, ncy - 10, 3, 20, theme::TEXT);
    }
}

void ui_draw_player(void)
{
    s_vis_valid = false;
    memset(s_last_bar_h, 0, sizeof(s_last_bar_h));

    display_fill(theme::BG);
    display_fill_rect(0, 0, BOARD_SCR_W, HEADER_H, theme::SURFACE);
    draw_back_btn();

    int tc = sd_music_track_count();
    char idx[16];
    if (tc > 0) snprintf(idx, sizeof(idx), "%d/%d", audio_current_track() + 1, tc);
    else strncpy(idx, "-/-", sizeof(idx));
    display_draw_text(120, 8, idx, theme::MUTED, theme::SURFACE, 1);
    draw_route_badge(188, 7, theme::SURFACE);
    s_last_route = audio_output_route();
    display_draw_hline(0, HEADER_H - 1, BOARD_SCR_W, theme::DIVIDER);

    char title[48];
    if (tc > 0)
        sd_music_get_display_name(sd_music_track_path(audio_current_track()), title, sizeof(title));
    else
        strncpy(title, "No track", sizeof(title));
    truncate_str(title, sizeof(title), title, 28);
    display_draw_text_centered(PL_TITLE_Y, 220, title, theme::TEXT, theme::BG, 1);

    const char *alb = audio_current_album();
    char abuf[28];
    truncate_str(abuf, sizeof(abuf), alb && alb[0] ? alb : "-", 26);
    display_draw_text_centered(PL_TITLE_Y + 16, 220, abuf, theme::MUTED, theme::BG, 1);

    /* Spectrum card — flat, no neon */
    display_fill_round_rect(12, PL_SPEC_Y - 4, BOARD_SCR_W - 24, PL_SPEC_H + 8, 8, theme::SURFACE);
    draw_vis_full();
    ui_draw_player_progress();
    draw_transport();
    ui_draw_volume();
}

void ui_redraw_current(void)
{
    switch (s_screen) {
    case UI_SCREEN_HOME: ui_draw_home(); break;
    case UI_SCREEN_BT_DEVICES: ui_draw_bt_devices(); break;
    case UI_SCREEN_BROWSER: ui_draw_browser(); break;
    case UI_SCREEN_PLAYER: ui_draw_player(); break;
    }
}

void ui_tick_progress(void)
{
    if (!power_backlight_on()) return;

    /* Refresh SPK/BT badge when the route changes (any screen that shows it) */
    audio_route_t route = audio_output_route();
    if (route != s_last_route && now_ms() - s_last_route_ms >= 300) {
        s_last_route = route;
        s_last_route_ms = now_ms();
        if (s_screen == UI_SCREEN_HOME)
            ui_draw_home();
        else if (s_screen == UI_SCREEN_PLAYER) {
            draw_route_badge(188, 7, theme::SURFACE);
            ui_draw_volume();
        } else if (s_screen == UI_SCREEN_BT_DEVICES)
            ui_draw_bt_devices();
    }

    if (s_screen != UI_SCREEN_PLAYER) return;
    if (audio_state() != PLAYER_PLAYING && audio_state() != PLAYER_PAUSED) return;
    if (now_ms() - s_last_progress_ms >= 500)
        ui_draw_player_progress();
}

/* ── Touch ───────────────────────────────────────────────── */

void ui_handle_touch(void)
{
    if (!power_backlight_on()) return;
    int16_t tx, ty;
    /* Stable tap: average/median while pressed — not the noisy first contact */
    if (!touch_read_tap(&tx, &ty)) return;
    power_note_activity();

    if (now_ms() - s_last_touch_ms < TOUCH_DEBOUNCE_MS) return;
    s_last_touch_ms = now_ms();

    ESP_LOGI(TAG, "tap %d,%d screen=%d", (int)tx, (int)ty, (int)s_screen);

    /*
     * Browser NOW must be checked BEFORE Back. On this CYD the X axis can
     * drift; a tap on the visual NOW button was often classified as Back.
     */
    if (s_screen == UI_SCREEN_BROWSER && hit_now_btn(tx, ty)) {
        ESP_LOGI(TAG, "Browser -> NOW player (ty=%d)", (int)ty);
        /* Force player screen even if stack thinks we're already there */
        if (s_screen != UI_SCREEN_PLAYER) {
            if (s_stack_n < (int)(sizeof(s_stack) / sizeof(s_stack[0])))
                s_stack[s_stack_n++] = s_screen;
        }
        s_screen = UI_SCREEN_PLAYER;
        ui_draw_player();
        return;
    }

    /* Global back — left side of header only (avoid stealing right-side taps) */
    if (s_screen != UI_SCREEN_HOME &&
        tx < (BOARD_SCR_W / 2) &&
        hit_rect(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H, 8)) {
        nav_back();
        ui_redraw_current();
        return;
    }

    if (s_screen == UI_SCREEN_HOME) {
        /* TOP card = Music, BOTTOM card = Bluetooth devices */
        if (ty >= HOME_MUSIC_Y && ty < HOME_MUSIC_Y + HOME_CARD_H) {
            ESP_LOGI(TAG, "Home -> Music browser (ty=%d)", (int)ty);
            nav_goto(UI_SCREEN_BROWSER);
            s_browse_level = BROWSE_ALBUMS;
            ui_draw_browser();
            return;
        }
        if (ty >= HOME_BT_Y && ty < HOME_BT_Y + HOME_CARD_H) {
            ESP_LOGI(TAG, "Home -> BT DEVICES (ty=%d)", (int)ty);
            nav_goto(UI_SCREEN_BT_DEVICES);
            s_bt_pick_scroll = 0;
            s_bt_connecting = false;
            s_last_bt_count = -1;
            ui_draw_bt_devices();
            return;
        }
        if ((audio_state() == PLAYER_PLAYING || audio_state() == PLAYER_PAUSED) &&
            ty >= 245 && ty < 290) {
            nav_goto(UI_SCREEN_PLAYER);
            ui_draw_player();
        }
        return;
    }

    if (s_screen == UI_SCREEN_BT_DEVICES) {
        /* Full power row — works even while connected */
        if (ty >= BT_PWR_Y && ty < BT_PWR_Y + BT_PWR_H && tx >= 8 && tx < BOARD_SCR_W - 8) {
            bool next = !bt_source_is_enabled();
            ESP_LOGI(TAG, "BT power tap -> %s (tx=%d ty=%d)", next ? "ON" : "OFF", (int)tx, (int)ty);
            s_bt_connecting = false;
            s_bt_pick_scroll = 0;
            bt_source_set_enabled(next);
            ui_draw_bt_devices();
            return;
        }

        if (!bt_source_is_enabled() || bt_source_is_connected())
            return;

        int vis = (footer_y() - BT_LIST_Y) / BT_ITEM_H;
        if (vis < 1) vis = 1;
        int n = bt_source_scan_count();

        if (footer_hit_prev(tx, ty)) {
            s_bt_pick_scroll -= vis;
            if (s_bt_pick_scroll < 0) s_bt_pick_scroll = 0;
            ESP_LOGI(TAG, "BT footer PREV");
            ui_draw_bt_devices();
            return;
        }
        if (footer_hit_next(tx, ty)) {
            if (s_bt_pick_scroll + vis < n) s_bt_pick_scroll += vis;
            ESP_LOGI(TAG, "BT footer NEXT");
            ui_draw_bt_devices();
            return;
        }
        int list_bottom = BT_LIST_Y + vis * BT_ITEM_H;
        if (ty < BT_LIST_Y || ty >= list_bottom) return;
        int row = (ty - BT_LIST_Y) / BT_ITEM_H;
        int idx = s_bt_pick_scroll + row;
        bt_scan_entry_t e;
        if (!bt_source_scan_get(idx, &e)) return;
        ESP_LOGI(TAG, "BT choose %s", e.name);
        bt_source_choose(e.addr, e.name);
        s_bt_connecting = true;
        ui_draw_bt_devices();
        return;
    }

    if (s_screen == UI_SCREEN_BROWSER) {
        /* NOW already handled above (before Back) */

        if (s_browse_level == BROWSE_TRACKS &&
            ty < HEADER_H && tx < (BOARD_SCR_W / 2) && tx >= 56) {
            s_browse_level = BROWSE_ALBUMS;
            s_track_scroll = 0;
            ui_draw_browser();
            return;
        }

        int vis = visible_slots();
        int item_count = (s_browse_level == BROWSE_ALBUMS)
                             ? sd_music_album_count()
                             : sd_music_browse_track_count();

        if (footer_hit_prev(tx, ty)) {
            if (s_browse_level == BROWSE_ALBUMS) {
                s_album_scroll -= vis;
                if (s_album_scroll < 0) s_album_scroll = 0;
            } else {
                s_track_scroll -= vis;
                if (s_track_scroll < 0) s_track_scroll = 0;
            }
            ESP_LOGI(TAG, "Browser footer PREV scroll=%d",
                     s_browse_level == BROWSE_ALBUMS ? s_album_scroll : s_track_scroll);
            ui_draw_browser();
            return;
        }
        if (footer_hit_next(tx, ty)) {
            if (s_browse_level == BROWSE_ALBUMS) {
                if (s_album_scroll + vis < item_count) s_album_scroll += vis;
            } else {
                if (s_track_scroll + vis < item_count) s_track_scroll += vis;
            }
            ESP_LOGI(TAG, "Browser footer NEXT scroll=%d",
                     s_browse_level == BROWSE_ALBUMS ? s_album_scroll : s_track_scroll);
            ui_draw_browser();
            return;
        }

        const int list_y = browser_list_y();
        int list_bottom = list_y + vis * ITEM_H;
        if (ty < list_y || ty >= list_bottom) return;
        int row = (ty - list_y) / ITEM_H;
        int scroll = (s_browse_level == BROWSE_ALBUMS) ? s_album_scroll : s_track_scroll;
        int idx = scroll + row;
        if (idx < 0 || idx >= item_count) return;

        if (s_browse_level == BROWSE_ALBUMS) {
            sd_music_load_album_tracks(sd_music_album_name(idx));
            s_browse_level = BROWSE_TRACKS;
            s_track_scroll = 0;
            ui_draw_browser();
            return;
        }

        if (audio_state() != PLAYER_STOPPED) audio_stop(true);
        int pi = sd_music_browse_track_playlist_index(idx);
        ESP_LOGI(TAG, "play playlist=%d", pi);
        if (!audio_start_track(pi, false)) return;
        nav_goto(UI_SCREEN_PLAYER);
        ui_draw_player();
        return;
    }

    /* Player */
    if (ty >= PL_VOL_Y) {
        int v = audio_get_volume_percent();
        if (tx < 80) v -= 10;
        else if (tx > 160) v += 10;
        else return;
        audio_set_volume_percent(v);
        bt_source_set_volume_percent(audio_get_volume_percent());
        ui_draw_volume();
        return;
    }

    if (ty >= TR_PLAY_Y0 && ty < TR_PLAY_Y1) {
        ESP_LOGI(TAG, "PLAY tap (%d,%d)", (int)tx, (int)ty);
        audio_toggle_pause();
        draw_transport();
        ui_draw_player_progress();
        return;
    }

    if (ty >= TR_ROW2_Y0 && ty < TR_ROW2_Y1) {
        if (tx < TR_MID_X) {
            ESP_LOGI(TAG, "PREV tap (%d,%d)", (int)tx, (int)ty);
            audio_prev();
            ui_draw_player();
        } else {
            ESP_LOGI(TAG, "NEXT tap (%d,%d)", (int)tx, (int)ty);
            audio_next(false);
            ui_draw_player();
        }
        return;
    }
}
