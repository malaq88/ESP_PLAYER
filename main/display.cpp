#include "display.h"
#include "board.h"
#include "theme.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "display";

static spi_device_handle_t s_tft;
static SemaphoreHandle_t s_spi_mutex;
static bool s_bl_on = true;

/* Built-in 6x8 ASCII (offset 32). Compact public-domain style glyphs. */
static const uint8_t FONT6X8[][6] = {
    {0x00,0x00,0x00,0x00,0x00,0x00}, // space
    {0x00,0x00,0x5F,0x00,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00,0x00},
    {0x14,0x7F,0x14,0x7F,0x14,0x00},
    {0x24,0x2A,0x7F,0x2A,0x12,0x00},
    {0x23,0x13,0x08,0x64,0x62,0x00},
    {0x36,0x49,0x55,0x22,0x50,0x00},
    {0x00,0x05,0x03,0x00,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00,0x00},
    {0x00,0x41,0x22,0x1C,0x00,0x00},
    {0x08,0x2A,0x1C,0x2A,0x08,0x00},
    {0x08,0x08,0x3E,0x08,0x08,0x00},
    {0x00,0x50,0x30,0x00,0x00,0x00},
    {0x08,0x08,0x08,0x08,0x08,0x00},
    {0x00,0x60,0x60,0x00,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02,0x00},
    {0x3E,0x51,0x49,0x45,0x3E,0x00}, // 0
    {0x00,0x42,0x7F,0x40,0x00,0x00},
    {0x42,0x61,0x51,0x49,0x46,0x00},
    {0x21,0x41,0x45,0x4B,0x31,0x00},
    {0x18,0x14,0x12,0x7F,0x10,0x00},
    {0x27,0x45,0x45,0x45,0x39,0x00},
    {0x3C,0x4A,0x49,0x49,0x30,0x00},
    {0x01,0x71,0x09,0x05,0x03,0x00},
    {0x36,0x49,0x49,0x49,0x36,0x00},
    {0x06,0x49,0x49,0x29,0x1E,0x00},
    {0x00,0x36,0x36,0x00,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41,0x00},
    {0x14,0x14,0x14,0x14,0x14,0x00},
    {0x41,0x22,0x14,0x08,0x00,0x00},
    {0x02,0x01,0x51,0x09,0x06,0x00},
    {0x32,0x49,0x79,0x41,0x3E,0x00},
    {0x7E,0x11,0x11,0x11,0x7E,0x00}, // A
    {0x7F,0x49,0x49,0x49,0x36,0x00},
    {0x3E,0x41,0x41,0x41,0x22,0x00},
    {0x7F,0x41,0x41,0x22,0x1C,0x00},
    {0x7F,0x49,0x49,0x49,0x41,0x00},
    {0x7F,0x09,0x09,0x01,0x01,0x00},
    {0x3E,0x41,0x41,0x51,0x32,0x00},
    {0x7F,0x08,0x08,0x08,0x7F,0x00},
    {0x00,0x41,0x7F,0x41,0x00,0x00},
    {0x20,0x40,0x41,0x3F,0x01,0x00},
    {0x7F,0x08,0x14,0x22,0x41,0x00},
    {0x7F,0x40,0x40,0x40,0x40,0x00},
    {0x7F,0x02,0x04,0x02,0x7F,0x00},
    {0x7F,0x04,0x08,0x10,0x7F,0x00},
    {0x3E,0x41,0x41,0x41,0x3E,0x00},
    {0x7F,0x09,0x09,0x09,0x06,0x00},
    {0x3E,0x41,0x51,0x21,0x5E,0x00},
    {0x7F,0x09,0x19,0x29,0x46,0x00},
    {0x46,0x49,0x49,0x49,0x31,0x00},
    {0x01,0x01,0x7F,0x01,0x01,0x00},
    {0x3F,0x40,0x40,0x40,0x3F,0x00},
    {0x1F,0x20,0x40,0x20,0x1F,0x00},
    {0x7F,0x20,0x18,0x20,0x7F,0x00},
    {0x63,0x14,0x08,0x14,0x63,0x00},
    {0x03,0x04,0x78,0x04,0x03,0x00},
    {0x61,0x51,0x49,0x45,0x43,0x00},
    {0x00,0x7F,0x41,0x41,0x00,0x00},
    {0x02,0x04,0x08,0x10,0x20,0x00},
    {0x00,0x41,0x41,0x7F,0x00,0x00},
    {0x04,0x02,0x01,0x02,0x04,0x00},
    {0x40,0x40,0x40,0x40,0x40,0x00},
    {0x00,0x01,0x02,0x04,0x00,0x00},
    {0x20,0x54,0x54,0x54,0x78,0x00}, // a
    {0x7F,0x48,0x44,0x44,0x38,0x00},
    {0x38,0x44,0x44,0x44,0x20,0x00},
    {0x38,0x44,0x44,0x48,0x7F,0x00},
    {0x38,0x54,0x54,0x54,0x18,0x00},
    {0x08,0x7E,0x09,0x01,0x02,0x00},
    {0x08,0x14,0x54,0x54,0x3C,0x00},
    {0x7F,0x08,0x04,0x04,0x78,0x00},
    {0x00,0x44,0x7D,0x40,0x00,0x00},
    {0x20,0x40,0x44,0x3D,0x00,0x00},
    {0x00,0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00,0x00},
    {0x7C,0x04,0x18,0x04,0x78,0x00},
    {0x7C,0x08,0x04,0x04,0x78,0x00},
    {0x38,0x44,0x44,0x44,0x38,0x00},
    {0x7C,0x14,0x14,0x14,0x08,0x00},
    {0x08,0x14,0x14,0x18,0x7C,0x00},
    {0x7C,0x08,0x04,0x04,0x08,0x00},
    {0x48,0x54,0x54,0x54,0x20,0x00},
    {0x04,0x3F,0x44,0x40,0x20,0x00},
    {0x3C,0x40,0x40,0x20,0x7C,0x00},
    {0x1C,0x20,0x40,0x20,0x1C,0x00},
    {0x3C,0x40,0x30,0x40,0x3C,0x00},
    {0x44,0x28,0x10,0x28,0x44,0x00},
    {0x0C,0x50,0x50,0x50,0x3C,0x00},
    {0x44,0x64,0x54,0x4C,0x44,0x00},
    {0x00,0x08,0x36,0x41,0x00,0x00},
    {0x00,0x00,0x7F,0x00,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00,0x00},
    {0x08,0x04,0x08,0x10,0x08,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00},
};

static inline void lock_spi(void)
{
    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
}

static inline void unlock_spi(void)
{
    xSemaphoreGive(s_spi_mutex);
}

static void tft_cmd(uint8_t cmd)
{
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    gpio_set_level((gpio_num_t)PIN_TFT_DC, 0);
    spi_device_polling_transmit(s_tft, &t);
}

static void tft_data(const void *data, size_t len)
{
    if (!data || !len) return;
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    gpio_set_level((gpio_num_t)PIN_TFT_DC, 1);
    spi_device_polling_transmit(s_tft, &t);
}

static void tft_data8(uint8_t d)
{
    tft_data(&d, 1);
}

static void set_addr_window(int x0, int y0, int x1, int y1)
{
    uint8_t data[4];
    tft_cmd(0x2A);
    data[0] = (x0 >> 8) & 0xFF; data[1] = x0 & 0xFF;
    data[2] = (x1 >> 8) & 0xFF; data[3] = x1 & 0xFF;
    tft_data(data, 4);
    tft_cmd(0x2B);
    data[0] = (y0 >> 8) & 0xFF; data[1] = y0 & 0xFF;
    data[2] = (y1 >> 8) & 0xFF; data[3] = y1 & 0xFF;
    tft_data(data, 4);
    tft_cmd(0x2C);
}

static void write_pixels(const uint16_t *px, size_t count)
{
    /* ILI9341 expects big-endian RGB565 on wire */
    static uint8_t buf[512];
    size_t i = 0;
    while (i < count) {
        size_t n = count - i;
        if (n > 256) n = 256;
        for (size_t k = 0; k < n; k++) {
            uint16_t c = px[i + k];
            buf[k * 2]     = (c >> 8) & 0xFF;
            buf[k * 2 + 1] = c & 0xFF;
        }
        tft_data(buf, n * 2);
        i += n;
    }
}

static void fill_solid(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > BOARD_SCR_W) w = BOARD_SCR_W - x;
    if (y + h > BOARD_SCR_H) h = BOARD_SCR_H - y;
    if (w <= 0 || h <= 0) return;

    lock_spi();
    set_addr_window(x, y, x + w - 1, y + h - 1);
    uint8_t hi = (color >> 8) & 0xFF;
    uint8_t lo = color & 0xFF;
    uint8_t line[BOARD_SCR_W * 2];
    for (int i = 0; i < w; i++) {
        line[i * 2] = hi;
        line[i * 2 + 1] = lo;
    }
    for (int row = 0; row < h; row++) {
        tft_data(line, (size_t)w * 2);
    }
    unlock_spi();
}

void display_set_backlight(bool on)
{
    gpio_set_direction((gpio_num_t)PIN_TFT_BL, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_TFT_BL, on ? 1 : 0);
    s_bl_on = on;
}

bool display_backlight_on(void)
{
    return s_bl_on;
}

void display_fill(uint16_t color)
{
    fill_solid(0, 0, BOARD_SCR_W, BOARD_SCR_H, color);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    fill_solid(x, y, w, h, color);
}

void display_draw_hline(int x, int y, int w, uint16_t color)
{
    fill_solid(x, y, w, 1, color);
}

void display_draw_vline(int x, int y, int h, uint16_t color)
{
    fill_solid(x, y, 1, h, color);
}

void display_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    display_draw_hline(x, y, w, color);
    display_draw_hline(x, y + h - 1, w, color);
    display_draw_vline(x, y, h, color);
    display_draw_vline(x + w - 1, y, h, color);
}

void display_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color)
{
    /* Soft corners without per-pixel SPI writes (keeps UI responsive). */
    (void)r;
    fill_solid(x, y, w, h, color);
}

void display_fill_circle(int x0, int y0, int r, uint16_t color)
{
    for (int y = -r; y <= r; y++) {
        int dx = (int)sqrtf((float)(r * r - y * y));
        fill_solid(x0 - dx, y0 + y, 2 * dx + 1, 1, color);
    }
}

void display_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    /* Sort by Y */
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

    auto edge = [](int xa, int ya, int xb, int yb, int y) -> float {
        if (ya == yb) return (float)xa;
        return (float)xa + (float)(xb - xa) * (float)(y - ya) / (float)(yb - ya);
    };

    for (int y = y0; y <= y2; y++) {
        float xa, xb;
        if (y < y1) {
            xa = edge(x0, y0, x2, y2, y);
            xb = edge(x0, y0, x1, y1, y);
        } else {
            xa = edge(x0, y0, x2, y2, y);
            xb = edge(x1, y1, x2, y2, y);
        }
        if (xa > xb) { float t = xa; xa = xb; xb = t; }
        fill_solid((int)xa, y, (int)(xb - xa) + 1, 1, color);
    }
}

int display_text_width(const char *s, int scale)
{
    if (!s) return 0;
    if (scale < 1) scale = 1;
    return (int)strlen(s) * 6 * scale;
}

int display_draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    if (!s) return 0;
    if (scale < 1) scale = 1;
    int ox = x;
    uint16_t glyph[6 * 8]; /* scale==1 path */
    while (*s) {
        char c = *s++;
        if (c < 32 || c > 126) c = '?';
        const uint8_t *g = FONT6X8[c - 32];
        if (scale == 1) {
            for (int row = 0; row < 8; row++) {
                for (int col = 0; col < 6; col++) {
                    glyph[row * 6 + col] = (g[col] & (1 << row)) ? fg : bg;
                }
            }
            display_push_image(x, y, 6, 8, glyph);
        } else {
            for (int col = 0; col < 6; col++) {
                uint8_t bits = g[col];
                for (int row = 0; row < 8; row++) {
                    uint16_t color = (bits & (1 << row)) ? fg : bg;
                    fill_solid(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
    return x - ox;
}

void display_draw_text_centered(int y, int max_w, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    char buf[64];
    strncpy(buf, s ? s : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int max_chars = max_w / (6 * scale) - 2;
    if (max_chars < 4) max_chars = 4;
    int len = (int)strlen(buf);
    if (len > max_chars) {
        buf[max_chars - 2] = '\0';
        strcat(buf, "..");
    }
    int tw = display_text_width(buf, scale);
    display_draw_text((BOARD_SCR_W - tw) / 2, y, buf, fg, bg, scale);
}

void display_push_image(int x, int y, int w, int h, const uint16_t *data)
{
    if (!data || w <= 0 || h <= 0) return;
    lock_spi();
    set_addr_window(x, y, x + w - 1, y + h - 1);
    write_pixels(data, (size_t)w * (size_t)h);
    unlock_spi();
}

void display_bus_lock(void) { lock_spi(); }
void display_bus_unlock(void) { unlock_spi(); }

static void ili9341_init_seq(void)
{
    tft_cmd(0x01); // SWRESET
    vTaskDelay(pdMS_TO_TICKS(150));
    tft_cmd(0x11); // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(120));

    tft_cmd(0x3A); tft_data8(0x55); // 16-bit
    tft_cmd(0x36); tft_data8(0x48); // MX + BGR — portrait-ish for many CYDs
    /* MADCTL: try portrait 240x320. Common CYD rotation 0 uses 0x08/0x48. */
    tft_cmd(0x36); tft_data8(0x08);

    tft_cmd(0xB1); tft_data8(0x00); tft_data8(0x18);
    tft_cmd(0xB6); tft_data8(0x08); tft_data8(0x82); tft_data8(0x27);

    tft_cmd(0x26); tft_data8(0x02); // gamma set (ILI9341_2 tweak)
    vTaskDelay(pdMS_TO_TICKS(50));
    tft_cmd(0x26); tft_data8(0x01);

    tft_cmd(0x29); // DISPON
    vTaskDelay(pdMS_TO_TICKS(20));
}

void display_init(void)
{
    s_spi_mutex = xSemaphoreCreateMutex();

    gpio_config_t io = {};
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (1ULL << PIN_TFT_DC) | (1ULL << PIN_TFT_BL) | (1ULL << PIN_TFT_CS);
    gpio_config(&io);
    gpio_set_level((gpio_num_t)PIN_TFT_BL, 1);
    gpio_set_level((gpio_num_t)PIN_TFT_CS, 1);
    s_bl_on = true;

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = PIN_TFT_MOSI;
    buscfg.miso_io_num = PIN_TFT_MISO;
    buscfg.sclk_io_num = PIN_TFT_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = BOARD_SCR_W * 2 * 16;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPI bus already init");
    } else {
        ESP_ERROR_CHECK(err);
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 40000000;
    devcfg.mode = 0;
    devcfg.spics_io_num = PIN_TFT_CS;
    devcfg.queue_size = 2;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &s_tft));

    lock_spi();
    ili9341_init_seq();
    unlock_spi();

    display_fill(theme::BG);
    ESP_LOGI(TAG, "ILI9341 ready %dx%d", BOARD_SCR_W, BOARD_SCR_H);
}
