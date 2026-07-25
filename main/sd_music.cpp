#include "sd_music.h"
#include "board.h"
#include "display.h"

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

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

static bool is_mp3(const char *fn) { return path_ieq_ext(fn, ".mp3") != 0; }
static bool is_wav(const char *fn) { return path_ieq_ext(fn, ".wav") != 0; }

static const char *TAG = "sd_music";
static const char *MOUNT = "/sd";

static char *s_playlist[MAX_TRACKS];
static int s_track_count;

static char s_albums[MAX_ALBUMS][MAX_ALBUM_NAME_LEN];
static int s_album_count;

static int s_browse_idx[MAX_TRACKS];
static int s_browse_count;

static void add_file(const char *path)
{
    if (s_track_count >= MAX_TRACKS) return;
    char *c = strdup(path);
    if (!c) return;
    s_playlist[s_track_count++] = c;
}

static void scan_dir(const char *dirpath, int depth)
{
    if (depth > 3) return;
    DIR *d = opendir(dirpath);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (strcmp(e->d_name, "System Volume Information") == 0) continue;

        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", dirpath, e->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_dir(path, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            if (is_mp3(e->d_name) || is_wav(e->d_name)) {
                add_file(path);
            }
        }
    }
    closedir(d);
}

bool sd_music_init(void)
{
    /*
     * CYD microSD on VSPI / SPI3 (NOT the TFT SPI2 bus):
     *   SCLK=18, MISO=19, MOSI=23, CS=5
     * Matches Arduino SD.begin(5) on default VSPI.
     */
    ESP_LOGI(TAG, "SD init v3 — host=SPI3 CS=%d MOSI=%d MISO=%d SCK=%d",
             PIN_SD_CS, PIN_SD_MOSI, PIN_SD_MISO, PIN_SD_SCLK);

    for (int attempt = 1; attempt <= 4; attempt++) {
        gpio_reset_pin((gpio_num_t)PIN_SD_CS);
        gpio_reset_pin((gpio_num_t)PIN_SD_MOSI);
        gpio_reset_pin((gpio_num_t)PIN_SD_MISO);
        gpio_reset_pin((gpio_num_t)PIN_SD_SCLK);

        gpio_set_pull_mode((gpio_num_t)PIN_SD_MISO, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode((gpio_num_t)PIN_SD_CS, GPIO_PULLUP_ONLY);
        gpio_set_direction((gpio_num_t)PIN_SD_CS, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)PIN_SD_CS, 1);

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
        mount_config.format_if_mount_failed = false;
        mount_config.max_files = 8;
        mount_config.allocation_unit_size = 16 * 1024;

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = SPI3_HOST;
        host.max_freq_khz = (attempt <= 2) ? SDMMC_FREQ_PROBING : 10000;
        host.unaligned_multi_block_rw_max_chunk_size = 8;

        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = PIN_SD_MOSI;
        buscfg.miso_io_num = PIN_SD_MISO;
        buscfg.sclk_io_num = PIN_SD_SCLK;
        buscfg.quadwp_io_num = -1;
        buscfg.quadhd_io_num = -1;
        buscfg.max_transfer_sz = 4000;

        /* Free leftover bus from a previous failed attempt / crash reboot */
        spi_bus_free(SPI3_HOST);

        esp_err_t ret = spi_bus_initialize(host.slot, &buscfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SD SPI init attempt %d: %s", attempt, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.gpio_cs = (gpio_num_t)PIN_SD_CS;
        slot_config.host_id = (spi_host_device_t)host.slot;

        sdmmc_card_t *card = NULL;
        vTaskDelay(pdMS_TO_TICKS(80 * attempt));
        ret = esp_vfs_fat_sdspi_mount(MOUNT, &host, &slot_config, &mount_config, &card);
        if (ret == ESP_OK) {
            sdmmc_card_print_info(stdout, card);
            ESP_LOGI(TAG, "SD mounted at %s (attempt %d)", MOUNT, attempt);
            return true;
        }

        ESP_LOGW(TAG, "SD mount attempt %d failed: %s", attempt, esp_err_to_name(ret));
        spi_bus_free(SPI3_HOST);
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    ESP_LOGE(TAG, "SD mount fail — check FAT32 card, seating, pins 18/19/23/5");
    return false;
}

int sd_music_scan(void)
{
    for (int i = 0; i < s_track_count; i++) {
        free(s_playlist[i]);
        s_playlist[i] = NULL;
    }
    s_track_count = 0;
    scan_dir(MOUNT, 0);
    ESP_LOGI(TAG, "Found %d tracks", s_track_count);
    return s_track_count;
}

int sd_music_track_count(void) { return s_track_count; }

const char *sd_music_track_path(int idx)
{
    if (idx < 0 || idx >= s_track_count) return NULL;
    return s_playlist[idx];
}

void sd_music_get_display_name(const char *path, char *out, size_t max_len)
{
    if (!out || max_len == 0) return;
    out[0] = '\0';
    if (!path) return;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    strncpy(out, name, max_len - 1);
    out[max_len - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

void sd_music_album_from_path(const char *path, char *out, size_t max_len)
{
    out[0] = '\0';
    if (!path || max_len == 0) return;
    const char *last = strrchr(path, '/');
    if (!last || last == path) return;
    const char *start = path;
    int folder_len = (int)(last - path);
    for (int j = folder_len - 1; j >= 0; j--) {
        if (path[j] == '/') {
            start = path + j + 1;
            break;
        }
    }
    size_t n = (size_t)(last - start);
    if (n >= max_len) n = max_len - 1;
    memcpy(out, start, n);
    out[n] = '\0';
    if (strcmp(out, "sd") == 0) out[0] = '\0';
}

void sd_music_build_albums(void)
{
    s_album_count = 0;
    for (int i = 0; i < s_track_count && s_album_count < MAX_ALBUMS; i++) {
        char candidate[MAX_ALBUM_NAME_LEN];
        sd_music_album_from_path(s_playlist[i], candidate, sizeof(candidate));
        if (!candidate[0]) continue;
        if (strcmp(candidate, "System Volume Information") == 0) continue;
        bool exists = false;
        for (int a = 0; a < s_album_count; a++) {
            if (strcmp(s_albums[a], candidate) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            strncpy(s_albums[s_album_count], candidate, MAX_ALBUM_NAME_LEN - 1);
            s_albums[s_album_count][MAX_ALBUM_NAME_LEN - 1] = '\0';
            s_album_count++;
        }
    }

    if (s_album_count < MAX_ALBUMS - 1) {
        for (int i = s_album_count; i > 0; i--) {
            strncpy(s_albums[i], s_albums[i - 1], MAX_ALBUM_NAME_LEN - 1);
            s_albums[i][MAX_ALBUM_NAME_LEN - 1] = '\0';
        }
        strncpy(s_albums[0], "[ All Tracks ]", MAX_ALBUM_NAME_LEN - 1);
        s_albums[0][MAX_ALBUM_NAME_LEN - 1] = '\0';
        s_album_count++;
    }
}

int sd_music_album_count(void) { return s_album_count; }

const char *sd_music_album_name(int idx)
{
    if (idx < 0 || idx >= s_album_count) return "";
    return s_albums[idx];
}

static void sort_browse(void)
{
    for (int i = 0; i < s_browse_count - 1; i++) {
        for (int j = 0; j < s_browse_count - 1 - i; j++) {
            int a = s_browse_idx[j];
            int b = s_browse_idx[j + 1];
            if (strcmp(s_playlist[a], s_playlist[b]) > 0) {
                int t = s_browse_idx[j];
                s_browse_idx[j] = s_browse_idx[j + 1];
                s_browse_idx[j + 1] = t;
            }
        }
    }
}

void sd_music_load_album_tracks(const char *album_name)
{
    s_browse_count = 0;
    if (!album_name) return;
    if (strcmp(album_name, "[ All Tracks ]") == 0) {
        for (int i = 0; i < s_track_count; i++)
            s_browse_idx[s_browse_count++] = i;
        sort_browse();
        return;
    }
    for (int i = 0; i < s_track_count; i++) {
        char alb[MAX_ALBUM_NAME_LEN];
        sd_music_album_from_path(s_playlist[i], alb, sizeof(alb));
        if (strcmp(alb, album_name) == 0)
            s_browse_idx[s_browse_count++] = i;
    }
    sort_browse();
}

int sd_music_browse_track_count(void) { return s_browse_count; }

int sd_music_browse_track_playlist_index(int browse_idx)
{
    if (browse_idx < 0 || browse_idx >= s_browse_count) return -1;
    return s_browse_idx[browse_idx];
}

uint32_t sd_music_wav_duration_sec(const char *path, uint32_t *out_rate_hz)
{
    if (out_rate_hz) *out_rate_hz = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t riff[12];
    if (fread(riff, 1, 12, f) < 12 || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) {
        fclose(f);
        return 0;
    }
    uint16_t num_ch = 0, bps = 0;
    uint32_t rate = 0, data_size = 0;
    bool have_data = false;
    while (!feof(f)) {
        uint8_t cid[4], szb[4];
        if (fread(cid, 1, 4, f) < 4 || fread(szb, 1, 4, f) < 4) break;
        uint32_t chunk = (uint32_t)szb[0] | ((uint32_t)szb[1] << 8) |
                         ((uint32_t)szb[2] << 16) | ((uint32_t)szb[3] << 24);
        if (memcmp(cid, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (chunk < 16 || fread(fmt, 1, 16, f) < 16) break;
            num_ch = (uint16_t)(fmt[2] | (fmt[3] << 8));
            rate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
                   ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            bps = (uint16_t)(fmt[14] | (fmt[15] << 8));
            uint32_t skip = chunk - 16;
            if (skip) fseek(f, (long)skip, SEEK_CUR);
            if (chunk & 1) fseek(f, 1, SEEK_CUR);
        } else if (memcmp(cid, "data", 4) == 0) {
            data_size = chunk;
            have_data = true;
            break;
        } else {
            fseek(f, (long)(chunk + (chunk & 1u)), SEEK_CUR);
        }
    }
    fclose(f);
    if (out_rate_hz) *out_rate_hz = rate;
    uint32_t bytes_per_sec = rate * (uint32_t)num_ch * ((uint32_t)bps / 8u);
    if (!bytes_per_sec || !have_data) return 0;
    return data_size / bytes_per_sec;
}

bool sd_music_read_raw(const char *path, void *buf, size_t expect_bytes)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, expect_bytes, f);
    fclose(f);
    return n == expect_bytes;
}
