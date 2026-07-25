#include "audio_local.h"
#include "audio_player.h"
#include "bt_source.h"
#include "board.h"

#include "driver/dac_continuous.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <string.h>

static const char *TAG = "audio_local";

#define LOCAL_CHUNK_FRAMES  256
#define LOCAL_DEFAULT_HZ    44100
/* DAC is only 8-bit; boost before soft-clip so the SPEAK amp is usable. */
#define LOCAL_AMP_BOOST     2.0f

static dac_continuous_handle_t s_dac;
static uint32_t s_dac_hz;
static bool s_dac_on;
static volatile bool s_local_active;

static int32_t soft_clip16(int32_t x)
{
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return x;
}

static esp_err_t dac_open(uint32_t hz)
{
    if (hz < 16000) hz = 16000;
    if (hz > 48000) hz = 48000;

    if (s_dac) {
        if (s_dac_on) {
            dac_continuous_disable(s_dac);
            s_dac_on = false;
        }
        dac_continuous_del_channels(s_dac);
        s_dac = nullptr;
    }

    /* CH1 = GPIO26 — CYD amp. Do not enable CH0 (GPIO25 = touch CLK). */
    dac_continuous_config_t cfg = {};
    cfg.chan_mask = DAC_CHANNEL_MASK_CH1;
    cfg.desc_num = 4;
    cfg.buf_size = 2048;
    cfg.freq_hz = hz;
    cfg.offset = 0;
    cfg.clk_src = DAC_DIGI_CLK_SRC_APLL;
    cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;

    esp_err_t err = dac_continuous_new_channels(&cfg, &s_dac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dac_continuous_new_channels: %s", esp_err_to_name(err));
        s_dac = nullptr;
        return err;
    }
    err = dac_continuous_enable(s_dac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dac_continuous_enable: %s", esp_err_to_name(err));
        dac_continuous_del_channels(s_dac);
        s_dac = nullptr;
        return err;
    }

    s_dac_hz = hz;
    s_dac_on = true;
    ESP_LOGI(TAG, "DAC CH1 GPIO%d @ %" PRIu32 " Hz", PIN_SPK_DAC, hz);
    return ESP_OK;
}

static void dac_silence_and_idle(void)
{
    if (!s_dac || !s_dac_on) return;
    uint8_t mid[64];
    memset(mid, 128, sizeof(mid));
    dac_continuous_write(s_dac, mid, sizeof(mid), nullptr, 50);
    dac_continuous_disable(s_dac);
    s_dac_on = false;
}

static void local_out_task(void *arg)
{
    (void)arg;
    int16_t frames[LOCAL_CHUNK_FRAMES][2];
    uint8_t pcm8[LOCAL_CHUNK_FRAMES];
    bool was_local = false;

    for (;;) {
        const bool want_local =
            !bt_source_is_connected() &&
            audio_state() == PLAYER_PLAYING;

        if (!want_local) {
            if (was_local) {
                dac_silence_and_idle();
                was_local = false;
                s_local_active = false;
                ESP_LOGI(TAG, "Speaker idle (BT=%d state=%d)",
                         (int)bt_source_is_connected(), (int)audio_state());
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        uint32_t hz = (uint32_t)(audio_vis_sample_rate() + 0.5f);
        if (hz < 8000) hz = LOCAL_DEFAULT_HZ;

        if (!s_dac || !s_dac_on || s_dac_hz != hz) {
            if (dac_open(hz) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            was_local = true;
        }

        audio_a2dp_fill(frames, LOCAL_CHUNK_FRAMES);
        for (int i = 0; i < LOCAL_CHUNK_FRAMES; i++) {
            int32_t mono = ((int32_t)frames[i][0] + (int32_t)frames[i][1]) >> 1;
            mono = soft_clip16((int32_t)((float)mono * LOCAL_AMP_BOOST));
            pcm8[i] = (uint8_t)((mono + 32768) >> 8);
        }

        esp_err_t err = dac_continuous_write(s_dac, pcm8, LOCAL_CHUNK_FRAMES, nullptr, 200);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "dac write: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        was_local = true;
        s_local_active = true;
    }
}

audio_route_t audio_output_route(void)
{
    if (bt_source_is_connected())
        return AUDIO_ROUTE_BLUETOOTH;
    return AUDIO_ROUTE_SPEAKER;
}

bool audio_local_is_active(void)
{
    return s_local_active;
}

void audio_local_init(void)
{
    s_local_active = false;
    if (dac_open(LOCAL_DEFAULT_HZ) != ESP_OK) {
        ESP_LOGE(TAG, "Local speaker unavailable");
        return;
    }
    /* Idle until playback without BT — avoid DC on the amp */
    dac_silence_and_idle();

    xTaskCreatePinnedToCore(local_out_task, "aud_spk", 4096, nullptr, 5, nullptr, 0);
    ESP_LOGI(TAG, "Local speaker task ready (GPIO%d DAC, boost=%.1fx)",
             PIN_SPK_DAC, (double)LOCAL_AMP_BOOST);
}
