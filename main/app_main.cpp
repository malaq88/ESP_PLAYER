#include "board.h"
#include "display.h"
#include "touch.h"
#include "power.h"
#include "sd_music.h"
#include "audio_player.h"
#include "audio_local.h"
#include "bt_source.h"
#include "ui.h"
#include "theme.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "app";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "ESP PLAYER — CYD Album (ESP-IDF) build-sd-v25");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    rgb_led_init();
    power_init();
    touch_init();
    audio_init();
    audio_local_init();
    ui_init();

    if (!sd_music_init()) {
        display_init();
        ui_draw_error("SD Failed!");
        ESP_LOGE(TAG, "Insert FAT32 microSD. CYD pins: CS=5 MOSI=23 MISO=19 SCK=18");
        while (true) {
            rgb_led_update(false, false, false);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    display_init();
    ui_draw_splash();

    /* BT starts in background — Home does not require a headset */
    bt_source_start();
    bt_source_set_volume_percent(audio_get_volume_percent());

    int tracks = sd_music_scan();
    if (tracks <= 0) {
        ui_draw_error("No music");
        while (true) {
            rgb_led_update(bt_source_is_enabled(), bt_source_is_connected(), false);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    sd_music_build_albums();
    ui_set_screen(UI_SCREEN_HOME);
    ui_draw_home();
    power_note_activity();
    power_set_ui_ready(true);

    ESP_LOGI(TAG, "Ready — %d tracks, %d albums (Home)", tracks, sd_music_album_count());

    while (true) {
        bool playing = (audio_state() == PLAYER_PLAYING);
        rgb_led_update(bt_source_is_enabled(), bt_source_is_connected(), playing);

        ui_handle_touch();
        ui_bt_tick();

        if (power_consume_wake_redraw())
            ui_redraw_current();

        if (power_backlight_on()) {
            ui_tick_progress();
            if (ui_get_screen() == UI_SCREEN_PLAYER)
                ui_update_visualizer();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
