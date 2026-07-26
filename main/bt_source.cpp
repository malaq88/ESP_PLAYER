#include "bt_source.h"
#include "audio_player.h"

#include "BluetoothA2DPSource.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "bt_source";

static BluetoothA2DPSource s_a2dp;
static char s_peer[BT_NAME_BUF];

static bt_scan_entry_t s_scan[BT_SCAN_MAX];
static int s_scan_count;
static portMUX_TYPE s_scan_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_user_choice;
static uint8_t s_chosen[6];

static bool s_enabled = true;
static bool s_started = false;

/* Declared in audio_player.cpp */
extern int32_t audio_a2dp_fill(int16_t (*frames)[2], int32_t count);

static void clear_scan_locked(void)
{
    s_scan_count = 0;
    memset(s_scan, 0, sizeof(s_scan));
}

static void clear_scan(void)
{
    portENTER_CRITICAL(&s_scan_mux);
    clear_scan_locked();
    portEXIT_CRITICAL(&s_scan_mux);
}

static bool addr_eq(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, 6) == 0;
}

static bool ssid_cb(const char *ssid, esp_bd_addr_t address, int rssi)
{
    if (!s_enabled) return false;

    bool accept = false;
    portENTER_CRITICAL(&s_scan_mux);

    bool dup = false;
    for (int i = 0; i < s_scan_count; i++) {
        if (addr_eq(s_scan[i].addr, address)) {
            dup = true;
            break;
        }
    }
    if (!dup && s_scan_count < BT_SCAN_MAX) {
        bt_scan_entry_t *e = &s_scan[s_scan_count++];
        memset(e, 0, sizeof(*e));
        if (ssid && ssid[0]) {
            strncpy(e->name, ssid, sizeof(e->name) - 1);
        } else {
            strncpy(e->name, "(sem nome)", sizeof(e->name) - 1);
        }
        memcpy(e->addr, address, 6);
        e->rssi = rssi;
    }

    if (s_user_choice && memcmp(address, s_chosen, 6) == 0) {
        accept = true;
    }

    portEXIT_CRITICAL(&s_scan_mux);
    return accept;
}

static int32_t data_cb(Frame *frame, int32_t count)
{
    /* Do not drain the PCM ring unless a headset is actually streaming —
     * otherwise local DAC output (GPIO26) would starve. */
    if (!s_enabled || !s_a2dp.is_connected()) {
        for (int32_t i = 0; i < count; i++) {
            frame[i].channel1 = 0;
            frame[i].channel2 = 0;
        }
        return count;
    }

    int16_t buf[256][2];
    int32_t done = 0;
    while (done < count) {
        int32_t chunk = count - done;
        if (chunk > 256) chunk = 256;
        audio_a2dp_fill(buf, chunk);
        for (int32_t i = 0; i < chunk; i++) {
            frame[done + i].channel1 = buf[i][0];
            frame[done + i].channel2 = buf[i][1];
        }
        done += chunk;
    }
    return count;
}

void bt_source_start(void)
{
    s_scan_count = 0;
    s_user_choice = false;
    s_peer[0] = '\0';
    s_enabled = true;

    s_a2dp.set_auto_reconnect(false);
    s_a2dp.clean_last_connection();
    s_a2dp.set_volume(127);
    s_a2dp.set_data_callback_in_frames(data_cb);
    s_a2dp.set_ssid_callback(ssid_cb);
    s_a2dp.start();
    s_started = true;
    ESP_LOGI(TAG, "A2DP source started");
}

bool bt_source_is_enabled(void)
{
    return s_enabled;
}

bool bt_source_is_busy(void)
{
    return false; /* soft toggle is instant */
}

void bt_source_set_enabled(bool enabled)
{
    if (enabled == s_enabled) {
        ESP_LOGI(TAG, "BT already %s", enabled ? "ON" : "OFF");
        return;
    }

    if (!enabled) {
        /* Soft off: disconnect + ignore scan/connect — avoid end() (can hang). */
        s_enabled = false;
        s_user_choice = false;
        s_peer[0] = '\0';
        clear_scan();
        if (s_started && s_a2dp.is_connected()) {
            s_a2dp.disconnect();
            ESP_LOGI(TAG, "BT disconnect requested");
        }
        ESP_LOGI(TAG, "Bluetooth OFF (speaker mode)");
        return;
    }

    s_user_choice = false;
    s_peer[0] = '\0';
    clear_scan();
    if (!s_started) {
        s_a2dp.set_auto_reconnect(false);
        s_a2dp.clean_last_connection();
        s_a2dp.set_volume(127);
        s_a2dp.set_data_callback_in_frames(data_cb);
        s_a2dp.set_ssid_callback(ssid_cb);
        s_a2dp.start();
        s_started = true;
    }
    s_enabled = true;
    ESP_LOGI(TAG, "Bluetooth ON (scanning)");
}

bool bt_source_is_connected(void)
{
    return s_enabled && s_started && s_a2dp.is_connected();
}

const char *bt_source_peer_name(void)
{
    return s_peer;
}

void bt_source_set_volume_percent(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_a2dp.set_volume((pct * 127) / 100);
}

int bt_source_scan_count(void)
{
    portENTER_CRITICAL(&s_scan_mux);
    int n = s_scan_count;
    portEXIT_CRITICAL(&s_scan_mux);
    return n;
}

bool bt_source_scan_get(int idx, bt_scan_entry_t *out)
{
    if (!out) return false;
    portENTER_CRITICAL(&s_scan_mux);
    if (idx < 0 || idx >= s_scan_count) {
        portEXIT_CRITICAL(&s_scan_mux);
        return false;
    }
    *out = s_scan[idx];
    portEXIT_CRITICAL(&s_scan_mux);
    return true;
}

void bt_source_choose(const uint8_t addr[6], const char *name)
{
    if (!s_enabled) return;
    portENTER_CRITICAL(&s_scan_mux);
    memcpy(s_chosen, addr, 6);
    s_user_choice = true;
    portEXIT_CRITICAL(&s_scan_mux);
    if (name) {
        strncpy(s_peer, name, sizeof(s_peer) - 1);
        s_peer[sizeof(s_peer) - 1] = '\0';
    }
}

void bt_source_run_picker(bt_picker_draw_fn draw, bt_picker_tick_fn tick)
{
    const int64_t show_after_ms = 2000;
    int64_t t0 = esp_timer_get_time() / 1000;
    bool showed = false;
    int last_snap = -1;
    bool connecting = false;

    s_user_choice = false;

    while (!s_a2dp.is_connected()) {
        if (tick) tick();

        int64_t now = esp_timer_get_time() / 1000;
        if (!showed && (now - t0 > show_after_ms)) {
            if (draw) draw(false);
            showed = true;
            last_snap = bt_source_scan_count();
        }

        if (showed) {
            int snap = bt_source_scan_count();
            if (s_user_choice && !connecting) {
                if (draw) draw(true);
                connecting = true;
                last_snap = snap;
            } else if (!connecting && snap != last_snap) {
                if (draw) draw(false);
                last_snap = snap;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    if (!s_peer[0]) {
        const char *n = s_a2dp.get_name();
        if (n && n[0]) {
            strncpy(s_peer, n, sizeof(s_peer) - 1);
            s_peer[sizeof(s_peer) - 1] = '\0';
        } else {
            strncpy(s_peer, "BT", sizeof(s_peer) - 1);
        }
    }
    ESP_LOGI(TAG, "Connected to %s", s_peer);
}
