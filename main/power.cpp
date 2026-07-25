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
static bool s_rgb_prev_bt;
static bool s_rgb_prev_play;

static void rgb_all_off(void)
{
    gpio_set_level((gpio_num_t)PIN_RGB_R, 1);
    gpio_set_level((gpio_num_t)PIN_RGB_G, 1);
    gpio_set_level((gpio_num_t)PIN_RGB_B, 1);
}

void rgb_led_init(void)
{
    gpio_config_t io = {};
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (1ULL << PIN_RGB_R) | (1ULL << PIN_RGB_G) | (1ULL << PIN_RGB_B);
    gpio_config(&io);
    rgb_all_off();
    s_rgb_last_ms = ms_now();
}

void rgb_led_update(bool bt_connected, bool playing)
{
    int64_t now = ms_now();
    if (bt_connected != s_rgb_prev_bt || playing != s_rgb_prev_play) {
        s_rgb_prev_bt = bt_connected;
        s_rgb_prev_play = playing;
        s_rgb_last_ms = now;
        s_rgb_phase = false;
        rgb_all_off();
    }

    if (bt_connected && playing) {
        if (now - s_rgb_last_ms >= 450) {
            s_rgb_last_ms = now;
            s_rgb_phase = !s_rgb_phase;
        }
        gpio_set_level((gpio_num_t)PIN_RGB_R, 1);
        if (s_rgb_phase) {
            gpio_set_level((gpio_num_t)PIN_RGB_G, 0);
            gpio_set_level((gpio_num_t)PIN_RGB_B, 1);
        } else {
            gpio_set_level((gpio_num_t)PIN_RGB_G, 1);
            gpio_set_level((gpio_num_t)PIN_RGB_B, 0);
        }
    } else if (!bt_connected) {
        if (now - s_rgb_last_ms >= 280) {
            s_rgb_last_ms = now;
            s_rgb_phase = !s_rgb_phase;
        }
        gpio_set_level((gpio_num_t)PIN_RGB_G, 1);
        if (s_rgb_phase) {
            gpio_set_level((gpio_num_t)PIN_RGB_R, 0);
            gpio_set_level((gpio_num_t)PIN_RGB_B, 1);
        } else {
            gpio_set_level((gpio_num_t)PIN_RGB_R, 1);
            gpio_set_level((gpio_num_t)PIN_RGB_B, 0);
        }
    } else {
        rgb_all_off();
    }
}

/* ── Backlight + BOOT (power button) ─────────────────────── */

static int64_t s_last_activity_ms;
static uint8_t s_boot_phase;
static int64_t s_boot_ms;
static bool s_ui_ready;
static bool s_wake_redraw;
static TaskHandle_t s_power_task;

static void boot_gpio_init(void)
{
    /* GPIO0 is a strapping/RTC pin — reset before use as BOOT input */
    gpio_reset_pin((gpio_num_t)PIN_BOOT_BTN);
    gpio_config_t io = {};
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    io.pin_bit_mask = (1ULL << PIN_BOOT_BTN);
    gpio_config(&io);
}

static void bl_apply(bool on)
{
    display_set_backlight(on);
}

static void toggle_bl(void)
{
    if (display_backlight_on()) {
        bl_apply(false);
        ESP_LOGI(TAG, "BOOT: display OFF");
    } else {
        bl_apply(true);
        s_last_activity_ms = ms_now();
        if (s_ui_ready) s_wake_redraw = true;
        ESP_LOGI(TAG, "BOOT: display ON");
    }
}

static void power_button_task(void *arg)
{
    (void)arg;
    for (;;) {
        power_poll();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void power_init(void)
{
    boot_gpio_init();
    s_last_activity_ms = ms_now();
    s_boot_phase = 0;
    s_ui_ready = false;
    s_wake_redraw = false;

    /* Dedicated poll — UI draw must never starve the power button */
    xTaskCreatePinnedToCore(
        power_button_task,
        "pwr_btn",
        3072,
        NULL,
        10,
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
    bool down = (gpio_get_level((gpio_num_t)PIN_BOOT_BTN) == 0);

    /* Debounced press → toggle once; wait for release (phone power button) */
    if (s_boot_phase == 0) {
        if (down) {
            s_boot_phase = 1;
            s_boot_ms = now;
        }
    } else if (s_boot_phase == 1) {
        if (!down) {
            s_boot_phase = 0;
        } else if (now - s_boot_ms >= 40) {
            toggle_bl();
            s_boot_phase = 2;
        }
    } else { /* 2: latched until release */
        if (!down) s_boot_phase = 0;
    }

    if (display_backlight_on() && (now - s_last_activity_ms >= DISPLAY_IDLE_OFF_MS)) {
        bl_apply(false);
        ESP_LOGI(TAG, "Idle timeout: display OFF");
    }
}
