#include "audio_player.h"
#include "sd_music.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <atomic>

static int path_ieq_ext(const char *fn, const char *ext4)
{
    size_t l = strlen(fn);
    if (l < 4) return 0;
    for (int i = 0; i < 4; i++) {
        char a = (char)tolower((unsigned char)fn[l - 4 + i]);
        char b = (char)tolower((unsigned char)ext4[i]);
        if (a != b) return 0;
    }
    return 1;
}

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

static const char *TAG = "audio";

static int16_t s_ring[AUDIO_RING_FRAMES * 2];
static std::atomic<size_t> s_rb_head{0};
static std::atomic<size_t> s_rb_tail{0};
static SemaphoreHandle_t s_rb_mux;

static int16_t s_vis_ring[VIS_BUF_LEN];
static std::atomic<uint32_t> s_vis_pos{0};
static float s_vis_val[NUM_VIS_BARS];
static float s_vis_env[NUM_VIS_BARS];

static float s_gain = 0.3f;
static int s_volume_pct = 30;
static SemaphoreHandle_t s_audio_lock;

/* Residual PCM from an MP3 frame that did not fully fit in the ring */
static int s_pcm_remain;
static int s_pcm_off;
static int s_pcm_ch;

static player_state_t s_state = PLAYER_STOPPED;
static audio_type_t s_type = AUDIO_NONE;
static int s_track = 0;
static char s_album[MAX_ALBUM_NAME_LEN];
static bool s_shuffle;
static int s_repeat; /* 0 off, 1 all, 2 one */

static FILE *s_file;
static mp3dec_t s_mp3;
static uint8_t s_mp3_buf[2048];
static size_t s_mp3_buf_len;
static bool s_mp3_eof;
/** Off-stack PCM — MINIMP3_MAX_SAMPLES_PER_FRAME is ~4.5 KiB; must not live on task stack. */
static mp3d_sample_t s_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
/** Off-stack WAV read chunk — avoids ~2 KiB on the decode task stack. */
static uint8_t s_wav_rd[512 * 4];

/* WAV state */
static uint32_t s_wav_data_left;
static uint16_t s_wav_channels;
static uint16_t s_wav_bits;
static uint32_t s_wav_rate;

static int64_t s_wall_start_ms;
static int64_t s_pause_accum_ms;
static int64_t s_pause_began_ms;
static uint32_t s_duration_sec;
static uint32_t s_cached_wav_rate;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static size_t rb_avail(void)
{
    size_t h = s_rb_head.load(std::memory_order_relaxed);
    size_t t = s_rb_tail.load(std::memory_order_relaxed);
    return (h >= t) ? (h - t) : (AUDIO_RING_FRAMES - t + h);
}

static size_t rb_free(void)
{
    size_t a = rb_avail();
    if (a >= AUDIO_RING_FRAMES - 1) return 0;
    return AUDIO_RING_FRAMES - 1 - a;
}

size_t audio_ring_avail(void) { return rb_avail(); }

static bool rb_push_sample(int16_t l, int16_t r)
{
    size_t head = s_rb_head.load(std::memory_order_relaxed);
    size_t next = (head + 1) % AUDIO_RING_FRAMES;
    if (next == s_rb_tail.load(std::memory_order_acquire)) return false;
    int32_t gl = (int32_t)(l * s_gain);
    int32_t gr = (int32_t)(r * s_gain);
    if (gl > 32767) gl = 32767;
    if (gl < -32768) gl = -32768;
    if (gr > 32767) gr = 32767;
    if (gr < -32768) gr = -32768;
    s_ring[head * 2] = (int16_t)gl;
    s_ring[head * 2 + 1] = (int16_t)gr;
    s_rb_head.store(next, std::memory_order_release);
    uint32_t vp = s_vis_pos.load(std::memory_order_relaxed);
    s_vis_ring[vp & (VIS_BUF_LEN - 1)] = (int16_t)((gl + gr) >> 1);
    s_vis_pos.store(vp + 1, std::memory_order_relaxed);
    return true;
}

static void audio_decode_task(void *arg)
{
    (void)arg;
    for (;;) {
        audio_service();
        /* 1 ms when playing keeps the A2DP ring topped up */
        vTaskDelay(pdMS_TO_TICKS(s_state == PLAYER_PLAYING ? 1 : 10));
    }
}

void audio_init(void)
{
    s_rb_mux = xSemaphoreCreateMutex();
    s_audio_lock = xSemaphoreCreateRecursiveMutex();
    s_rb_head.store(0, std::memory_order_relaxed);
    s_rb_tail.store(0, std::memory_order_relaxed);
    memset(s_vis_val, 0, sizeof(s_vis_val));
    memset(s_vis_env, 0, sizeof(s_vis_env));
    audio_set_volume_percent(45);

    /* Decode on core 1; scratch is static in minimp3, stack mainly for FAT/stdio */
    xTaskCreatePinnedToCore(audio_decode_task, "aud_dec", 12288, NULL, 6, NULL, 1);
}

void audio_set_volume_percent(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_volume_pct = pct;
    /* Mild curve — mid levels louder on the weak SPEAK amp */
    float t = (float)pct / 100.0f;
    s_gain = t * t * 0.35f + t * 0.65f;
}

int audio_get_volume_percent(void) { return s_volume_pct; }

player_state_t audio_state(void) { return s_state; }
audio_type_t audio_type(void) { return s_type; }
int audio_current_track(void) { return s_track; }
uint32_t audio_duration_sec(void) { return s_duration_sec; }
uint32_t audio_wav_rate_hz(void) { return s_cached_wav_rate; }
const char *audio_current_album(void) { return s_album; }

uint32_t audio_elapsed_sec(void)
{
    if (s_state == PLAYER_STOPPED || s_wall_start_ms == 0) return 0;
    uint32_t el;
    if (s_state == PLAYER_PAUSED && s_pause_began_ms >= s_wall_start_ms) {
        el = (uint32_t)((s_pause_began_ms - s_wall_start_ms - s_pause_accum_ms) / 1000);
    } else if (s_state == PLAYER_PLAYING) {
        el = (uint32_t)((now_ms() - s_wall_start_ms - s_pause_accum_ms) / 1000);
    } else {
        return 0;
    }
    if (s_duration_sec > 0 && el > s_duration_sec) el = s_duration_sec;
    return el;
}

void audio_stop(bool flush_ring)
{
    if (s_audio_lock) xSemaphoreTakeRecursive(s_audio_lock, portMAX_DELAY);
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
    s_type = AUDIO_NONE;
    s_state = PLAYER_STOPPED;
    s_mp3_buf_len = 0;
    s_mp3_eof = true;
    s_wav_data_left = 0;
    s_pcm_remain = 0;
    s_pcm_off = 0;
    if (flush_ring) {
        s_rb_head.store(0, std::memory_order_relaxed);
        s_rb_tail.store(0, std::memory_order_relaxed);
    }
    s_wall_start_ms = 0;
    s_pause_accum_ms = 0;
    s_pause_began_ms = 0;
    s_duration_sec = 0;
    s_cached_wav_rate = 0;
    if (s_audio_lock) xSemaphoreGiveRecursive(s_audio_lock);
}

static bool open_wav(const char *path)
{
    s_file = fopen(path, "rb");
    if (!s_file) return false;
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, s_file) < 12) return false;
    if (memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) return false;

    s_wav_channels = 0;
    s_wav_bits = 0;
    s_wav_rate = 0;
    s_wav_data_left = 0;

    while (!feof(s_file)) {
        uint8_t cid[4], szb[4];
        if (fread(cid, 1, 4, s_file) < 4 || fread(szb, 1, 4, s_file) < 4) break;
        uint32_t chunk = (uint32_t)szb[0] | ((uint32_t)szb[1] << 8) |
                         ((uint32_t)szb[2] << 16) | ((uint32_t)szb[3] << 24);
        if (memcmp(cid, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (chunk < 16 || fread(fmt, 1, 16, s_file) < 16) return false;
            s_wav_channels = (uint16_t)(fmt[2] | (fmt[3] << 8));
            s_wav_rate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
                         ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            s_wav_bits = (uint16_t)(fmt[14] | (fmt[15] << 8));
            if (chunk > 16) fseek(s_file, (long)(chunk - 16), SEEK_CUR);
            if (chunk & 1) fseek(s_file, 1, SEEK_CUR);
        } else if (memcmp(cid, "data", 4) == 0) {
            s_wav_data_left = chunk;
            break;
        } else {
            fseek(s_file, (long)(chunk + (chunk & 1u)), SEEK_CUR);
        }
    }
    if (!s_wav_data_left || s_wav_bits != 16) {
        ESP_LOGW(TAG, "WAV unsupported (need 16-bit PCM)");
        return false;
    }
    s_type = AUDIO_WAV;
    s_cached_wav_rate = s_wav_rate;
    s_duration_sec = sd_music_wav_duration_sec(path, &s_cached_wav_rate);
    return true;
}

static bool open_mp3(const char *path)
{
    s_file = fopen(path, "rb");
    if (!s_file) return false;
    mp3dec_init(&s_mp3);
    s_mp3_buf_len = 0;
    s_mp3_eof = false;
    s_type = AUDIO_MP3;
    fseek(s_file, 0, SEEK_END);
    long sz = ftell(s_file);
    fseek(s_file, 0, SEEK_SET);
    s_duration_sec = (sz > 0) ? (uint32_t)(sz / 16000) : 0;
    if (s_duration_sec == 0 && sz > 8000) s_duration_sec = 1;
    s_cached_wav_rate = 0;
    return true;
}

bool audio_start_track(int playlist_index, bool gapless)
{
    if (s_audio_lock) xSemaphoreTakeRecursive(s_audio_lock, portMAX_DELAY);

    int count = sd_music_track_count();
    if (count <= 0) {
        if (s_audio_lock) xSemaphoreGiveRecursive(s_audio_lock);
        return false;
    }
    if (playlist_index < 0) playlist_index = count - 1;
    if (playlist_index >= count) playlist_index = 0;
    s_track = playlist_index;

    audio_stop(!gapless);

    const char *path = sd_music_track_path(s_track);
    if (!path) {
        if (s_audio_lock) xSemaphoreGiveRecursive(s_audio_lock);
        return false;
    }
    sd_music_album_from_path(path, s_album, sizeof(s_album));

    bool ok = false;
    if (path_ieq_ext(path, ".wav"))
        ok = open_wav(path);
    else
        ok = open_mp3(path);

    if (!ok) {
        audio_stop(true);
        ESP_LOGE(TAG, "Failed to open %s", path);
        if (s_audio_lock) xSemaphoreGiveRecursive(s_audio_lock);
        return false;
    }

    s_pcm_remain = 0;
    s_pcm_off = 0;
    s_state = PLAYER_PLAYING;
    s_wall_start_ms = now_ms();
    s_pause_accum_ms = 0;
    s_pause_began_ms = 0;

    /* Do not decode on the UI task — minimp3 needs a large scratch; decode task fills the ring */
    ESP_LOGI(TAG, "Playing [%d] %s (free heap %u)", s_track, path,
             (unsigned)esp_get_free_heap_size());
    if (s_audio_lock) xSemaphoreGiveRecursive(s_audio_lock);
    return true;
}

void audio_next(bool gapless)
{
    int count = sd_music_track_count();
    if (count <= 0) return;

    if (s_repeat == 2 && gapless) {
        audio_start_track(s_track, true);
        return;
    }

    int next = s_track + 1;
    if (s_shuffle && count > 1) {
        int r = (int)(esp_random() % (uint32_t)count);
        if (r == s_track) r = (r + 1) % count;
        next = r;
    } else if (next >= count) {
        if (s_repeat == 1)
            next = 0;
        else {
            audio_stop(true);
            return;
        }
    }
    audio_start_track(next, gapless);
}

void audio_prev(void)
{
    int count = sd_music_track_count();
    if (count <= 0) return;
    if (s_shuffle && count > 1) {
        int r = (int)(esp_random() % (uint32_t)count);
        if (r == s_track) r = (r + 1) % count;
        audio_start_track(r, false);
        return;
    }
    audio_start_track(s_track > 0 ? s_track - 1 : count - 1, false);
}

void audio_toggle_shuffle(void)
{
    s_shuffle = !s_shuffle;
}

void audio_cycle_repeat(void)
{
    s_repeat = (s_repeat + 1) % 3;
}

bool audio_shuffle_on(void) { return s_shuffle; }
int audio_repeat_mode(void) { return s_repeat; }

void audio_toggle_pause(void)
{
    if (s_audio_lock) xSemaphoreTakeRecursive(s_audio_lock, portMAX_DELAY);
    if (s_state == PLAYER_PLAYING) {
        s_state = PLAYER_PAUSED;
        s_pause_began_ms = now_ms();
    } else if (s_state == PLAYER_PAUSED) {
        if (s_pause_began_ms) {
            s_pause_accum_ms += (now_ms() - s_pause_began_ms);
            s_pause_began_ms = 0;
        }
        s_state = PLAYER_PLAYING;
    }
    if (s_audio_lock) xSemaphoreGiveRecursive(s_audio_lock);
}

static bool decode_wav_once(void)
{
    if (!s_file || s_wav_data_left == 0) return false;
    size_t free_fr = rb_free();
    if (free_fr < 32) return true;

    size_t bytes_per = (s_wav_channels == 1) ? 2u : 4u;
    size_t max_fr = free_fr;
    if (max_fr > 512) max_fr = 512;
    size_t max_by_data = s_wav_data_left / bytes_per;
    if (max_fr > max_by_data) max_fr = max_by_data;
    if (max_fr == 0) {
        s_wav_data_left = 0;
        return false;
    }

    uint8_t *buf = s_wav_rd;
    size_t want = max_fr * bytes_per;
    size_t got = fread(buf, 1, want, s_file);
    if (got < bytes_per) {
        s_wav_data_left = 0;
        return false;
    }
    size_t frames = got / bytes_per;
    s_wav_data_left -= (uint32_t)(frames * bytes_per);

    for (size_t i = 0; i < frames; i++) {
        const uint8_t *p = buf + i * bytes_per;
        int16_t l = (int16_t)(p[0] | (p[1] << 8));
        int16_t r = l;
        if (s_wav_channels >= 2)
            r = (int16_t)(p[2] | (p[3] << 8));
        if (!rb_push_sample(l, r)) break;
    }
    return true;
}

static bool flush_pcm_pending(void)
{
    while (s_pcm_remain > 0) {
        int16_t l, r;
        if (s_pcm_ch == 1) {
            l = r = s_pcm[s_pcm_off];
        } else {
            l = s_pcm[s_pcm_off * 2];
            r = s_pcm[s_pcm_off * 2 + 1];
        }
        if (!rb_push_sample(l, r)) return true;
        s_pcm_off++;
        s_pcm_remain--;
    }
    return true;
}

static bool decode_mp3_once(void)
{
    if (!s_file) return false;
    if (s_pcm_remain > 0) return flush_pcm_pending();
    if (rb_free() < 1152) return true;

    if (s_mp3_buf_len < 2048 && !s_mp3_eof) {
        size_t n = fread(s_mp3_buf + s_mp3_buf_len, 1, sizeof(s_mp3_buf) - s_mp3_buf_len, s_file);
        if (n == 0) s_mp3_eof = true;
        else s_mp3_buf_len += n;
    }

    if (s_mp3_buf_len == 0) return false;

    mp3dec_frame_info_t info;
    int samples = mp3dec_decode_frame(&s_mp3, s_mp3_buf, (int)s_mp3_buf_len, s_pcm, &info);
    if (info.frame_bytes > 0) {
        size_t used = (size_t)info.frame_bytes;
        if (used < s_mp3_buf_len) {
            memmove(s_mp3_buf, s_mp3_buf + used, s_mp3_buf_len - used);
            s_mp3_buf_len -= used;
        } else {
            s_mp3_buf_len = 0;
        }
    } else if (samples == 0) {
        if (s_mp3_eof) return false;
        if (s_mp3_buf_len >= sizeof(s_mp3_buf)) {
            memmove(s_mp3_buf, s_mp3_buf + 1, s_mp3_buf_len - 1);
            s_mp3_buf_len--;
        }
        return true;
    }

    if (samples <= 0) return true;

    s_pcm_ch = info.channels;
    s_pcm_off = 0;
    s_pcm_remain = samples;
    return flush_pcm_pending();
}

void audio_pump(int max_loops)
{
    if (s_state != PLAYER_PLAYING) return;
    for (int i = 0; i < max_loops; i++) {
        bool ok = false;
        if (s_type == AUDIO_MP3) ok = decode_mp3_once();
        else if (s_type == AUDIO_WAV) ok = decode_wav_once();
        if (!ok) break;
    }
}

void audio_service(void)
{
    if (!s_audio_lock || xSemaphoreTakeRecursive(s_audio_lock, 0) != pdTRUE) return;

    if (s_state != PLAYER_PLAYING) {
        xSemaphoreGiveRecursive(s_audio_lock);
        return;
    }

    bool alive = false;
    if (s_type == AUDIO_MP3) {
        alive = s_file && (!s_mp3_eof || s_mp3_buf_len > 0 || s_pcm_remain > 0);
    } else if (s_type == AUDIO_WAV) {
        alive = s_file && s_wav_data_left > 0;
    }

    if (alive) {
        int target = (AUDIO_RING_FRAMES * 3) / 4;
        int loops = 0;
        while ((int)rb_avail() < target && loops < 200) {
            bool ok = false;
            if (s_type == AUDIO_MP3) ok = decode_mp3_once();
            else if (s_type == AUDIO_WAV) ok = decode_wav_once();
            if (!ok) {
                alive = false;
                break;
            }
            loops++;
        }
    }

    if (!alive) {
        if (rb_avail() < 200) {
            audio_next(true);
        }
    }
    xSemaphoreGiveRecursive(s_audio_lock);
}

/* Frame layout matches ESP32-A2DP Frame { int16 channel1; int16 channel2; } */
int32_t audio_a2dp_fill(int16_t (*frames)[2], int32_t count)
{
    int32_t avail = (int32_t)rb_avail();
    int32_t to_send = count < avail ? count : avail;
    size_t tail = s_rb_tail.load(std::memory_order_relaxed);
    for (int32_t i = 0; i < to_send; i++) {
        frames[i][0] = s_ring[tail * 2];
        frames[i][1] = s_ring[tail * 2 + 1];
        tail = (tail + 1) % AUDIO_RING_FRAMES;
    }
    s_rb_tail.store(tail, std::memory_order_release);
    for (int32_t i = to_send; i < count; i++) {
        frames[i][0] = 0;
        frames[i][1] = 0;
    }
    return count;
}

float audio_vis_sample_rate(void)
{
    if (s_type == AUDIO_WAV && s_cached_wav_rate > 0) return (float)s_cached_wav_rate;
    return 44100.0f;
}

void audio_vis_compute(void)
{
    /* Cheap time-domain bands — Goertzel was starving the UI/touch loop */
    if (s_vis_pos.load(std::memory_order_relaxed) < (uint32_t)VIS_N) return;
    uint32_t end = s_vis_pos.load(std::memory_order_relaxed);
    const int per = VIS_N / NUM_VIS_BARS;
    for (int b = 0; b < NUM_VIS_BARS; b++) {
        float acc = 0.f;
        int base = b * per;
        for (int i = 0; i < per; i++) {
            uint32_t idx = (end - VIS_N + base + i) & (VIS_BUF_LEN - 1);
            float s = (float)s_vis_ring[idx] * (1.0f / 32768.0f);
            acc += s * s;
        }
        float rms = sqrtf(acc / (float)per);
        float treble = 0.7f + (float)b * 0.18f;
        float scaled = rms * treble * 4.5f;
        s_vis_env[b] = s_vis_env[b] * 0.85f + scaled * 0.15f;
        if (s_vis_env[b] < 1e-6f) s_vis_env[b] = 1e-6f;
        float t = scaled / (s_vis_env[b] * 1.2f + 1e-6f);
        if (t > 1.0f) t = 1.0f;
        if (t > s_vis_val[b]) s_vis_val[b] = s_vis_val[b] * 0.35f + t * 0.65f;
        else s_vis_val[b] = s_vis_val[b] * 0.75f + t * 0.25f;
    }
}

void audio_vis_decay(void)
{
    for (int i = 0; i < NUM_VIS_BARS; i++) {
        s_vis_val[i] *= 0.82f;
        s_vis_env[i] *= 0.88f;
    }
}

const float *audio_vis_bands(void) { return s_vis_val; }
