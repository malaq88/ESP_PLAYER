#include "power.h"
#include "board.h"
#include "display.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power";

static int64_t ms_now(void)
{
    return esp_timer_get_time() / 1000;
}

/* ── RGB LED ─────────────────────────────────────────────── */

static int64_t s_rgb_last_ms;
static bool s_rgb_phase;

static void rgb_all_off(void)
{
    gpio_set_level((gpio_num_t)PIN_RGB_R, 1);
    gpio_set_level((gpio_num_t)PIN_RGB_G, 1);
    gpio_set_level((gpio_num_t)PIN_RGB_B, 1);
}

static void rgb_solid(bool r_on, bool g_on, bool b_on)
{
    /* Active LOW */
    gpio_set_level((gpio_num_t)PIN_RGB_R, r_on ? 0 : 1);
    gpio_set_level((gpio_num_t)PIN_RGB_G, g_on ? 0 : 1);
    gpio_set_level((gpio_num_t)PIN_RGB_B, b_on ? 0 : 1);
}

void rgb_led_init(void)
{
    gpio_config_t io = {};
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (1ULL << PIN_RGB_R) | (1ULL << PIN_RGB_G) | (1ULL << PIN_RGB_B);
    gpio_config(&io);
    rgb_all_off();
    s_rgb_last_ms = ms_now();
    s_rgb_phase = false;
}

void rgb_led_update(bool bt_enabled, bool bt_connected, bool playing)
{
    int64_t now = ms_now();

    if (playing) {
        if (bt_connected)
            rgb_solid(false, false, true);  /* blue = A2DP */
        else
            rgb_solid(false, true, false);  /* green = speaker */
        return;
    }

    /* Idle: blink red/blue while BT is on and waiting for a headset */
    if (bt_enabled && !bt_connected) {
        if (now - s_rgb_last_ms >= 280) {
            s_rgb_last_ms = now;
            s_rgb_phase = !s_rgb_phase;
        }
        if (s_rgb_phase)
            rgb_solid(true, false, false);  /* red */
        else
            rgb_solid(false, false, true);  /* blue */
        return;
    }

    rgb_all_off();
}

/* ── Backlight idle lock / touch wake ────────────────────── */

static int64_t s_last_activity_ms;
static bool s_ui_ready;
static bool s_wake_redraw;
static TaskHandle_t s_power_task;

static void bl_apply(bool on)
{
    display_set_backlight(on);
}

static void power_idle_task(void *arg)
{
    (void)arg;
    for (;;) {
        power_poll();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void power_init(void)
{
    s_last_activity_ms = ms_now();
    s_ui_ready = false;
    s_wake_redraw = false;

    xTaskCreatePinnedToCore(
        power_idle_task,
        "pwr_idle",
        2048,
        NULL,
        5,
        &s_power_task,
        0);
}

void power_note_activity(void)
{
    if (!display_backlight_on()) return;
    s_last_activity_ms = ms_now();
}

bool power_backlight_on(void)
{
    return display_backlight_on();
}

void power_wake_display(void)
{
    if (display_backlight_on()) return;
    bl_apply(true);
    s_last_activity_ms = ms_now();
    if (s_ui_ready) s_wake_redraw = true;
    ESP_LOGI(TAG, "Touch wake: display ON");
}

void power_set_ui_ready(bool ready)
{
    s_ui_ready = ready;
}

bool power_consume_wake_redraw(void)
{
    if (!s_wake_redraw) return false;
    s_wake_redraw = false;
    return true;
}

void power_poll(void)
{
    int64_t now = ms_now();

    if (display_backlight_on() && (now - s_last_activity_ms >= DISPLAY_IDLE_OFF_MS)) {
        bl_apply(false);
        ESP_LOGI(TAG, "Idle timeout: display locked");
    }
}
