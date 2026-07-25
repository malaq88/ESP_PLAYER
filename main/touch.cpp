#include "touch.h"
#include "board.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

static const char *TAG = "touch";

/* Pressure threshold (XPT2046 Z) — typical CYD values ~300+ when pressed */
#define TOUCH_Z_THRESHOLD 200

static void clk(int v) { gpio_set_level((gpio_num_t)PIN_TOUCH_CLK, v); }
static void mosi(int v) { gpio_set_level((gpio_num_t)PIN_TOUCH_MOSI, v); }
static int  miso(void) { return gpio_get_level((gpio_num_t)PIN_TOUCH_MISO); }
static void cs(int v) { gpio_set_level((gpio_num_t)PIN_TOUCH_CS, v); }

/** SPI Mode 0 bit-bang: MSB first, sample on rising edge. */
static uint8_t xfer8(uint8_t out)
{
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        mosi((out >> i) & 1);
        esp_rom_delay_us(1);
        clk(1);
        esp_rom_delay_us(1);
        in = (uint8_t)((in << 1) | (miso() & 1));
        clk(0);
        esp_rom_delay_us(1);
    }
    return in;
}

static uint16_t xfer16(uint16_t out)
{
    uint16_t in = 0;
    for (int i = 15; i >= 0; i--) {
        mosi((out >> i) & 1);
        esp_rom_delay_us(1);
        clk(1);
        esp_rom_delay_us(1);
        in = (uint16_t)((in << 1) | (miso() & 1));
        clk(0);
        esp_rom_delay_us(1);
    }
    return in;
}

/** Read 12-bit ADC after 8-bit command (same framing as transfer16 >> 3). */
static uint16_t xpt_cmd12(uint8_t cmd)
{
    cs(0);
    esp_rom_delay_us(2);
    xfer8(cmd);
    uint16_t v = xfer16(0) >> 3;
    cs(1);
    esp_rom_delay_us(2);
    return (uint16_t)(v & 0x0FFF);
}

/**
 * Full pressed-sample sequence adapted from Paul Stoffregen XPT2046_Touchscreen
 * (rotation 0 — matches TFT portrait used by this project).
 */
static bool xpt_read_raw(int16_t *x_out, int16_t *y_out, int16_t *z_out)
{
    cs(0);
    esp_rom_delay_us(2);

    xfer8(0xB1); /* Z1 */
    int16_t z1 = (int16_t)(xfer16(0xC1) >> 3); /* Z2 cmd in flight */
    int z = z1 + 4095;
    int16_t z2 = (int16_t)(xfer16(0x91) >> 3); /* start X */
    z -= z2;

    int16_t data[6] = {0};

    if (z >= TOUCH_Z_THRESHOLD) {
        xfer16(0x91); /* discard noisy first X */
        data[0] = (int16_t)(xfer16(0xD1) >> 3); /* Y */
        data[1] = (int16_t)(xfer16(0x91) >> 3); /* X */
        data[2] = (int16_t)(xfer16(0xD1) >> 3); /* Y */
        data[3] = (int16_t)(xfer16(0x91) >> 3); /* X */
        data[4] = (int16_t)(xfer16(0xD0) >> 3); /* Y power-down */
        data[5] = (int16_t)(xfer16(0x00) >> 3);
    } else {
        /* still power down */
        xfer16(0xD0);
        xfer16(0x00);
    }

    cs(1);

    if (z < 0) z = 0;
    if (z_out) *z_out = (int16_t)z;
    if (z < TOUCH_Z_THRESHOLD) return false;

    auto avg2 = [](int16_t a, int16_t b, int16_t c) -> int16_t {
        int16_t da = (a > b) ? (a - b) : (b - a);
        int16_t db = (a > c) ? (a - c) : (c - a);
        int16_t dc = (b > c) ? (b - c) : (c - b);
        if (da <= db && da <= dc) return (int16_t)((a + b) >> 1);
        if (db <= da && db <= dc) return (int16_t)((a + c) >> 1);
        return (int16_t)((b + c) >> 1);
    };

    /* Library names: x = Y-channel samples, y = X-channel samples */
    int16_t x = avg2(data[0], data[2], data[4]);
    int16_t y = avg2(data[1], data[3], data[5]);

    /* rotation 0 (portrait CYD) */
    int16_t xraw = (int16_t)(4095 - y);
    int16_t yraw = x;

    *x_out = xraw;
    *y_out = yraw;
    return true;
}

void touch_init(void)
{
    gpio_config_t out = {};
    out.mode = GPIO_MODE_OUTPUT;
    out.pin_bit_mask = (1ULL << PIN_TOUCH_CLK) | (1ULL << PIN_TOUCH_MOSI) | (1ULL << PIN_TOUCH_CS);
    gpio_config(&out);

    gpio_config_t in = {};
    in.mode = GPIO_MODE_INPUT;
    /* GPIO 34–39 are input-only; no internal pull */
    in.pin_bit_mask = (1ULL << PIN_TOUCH_MISO) | (1ULL << PIN_TOUCH_IRQ);
    gpio_config(&in);

    cs(1);
    clk(0);
    mosi(0);

    /* Wake / dummy read so IRQ line settles */
    (void)xpt_cmd12(0xD0);
    ESP_LOGI(TAG, "XPT2046 bitbang ready (IRQ=%d CS=%d)", PIN_TOUCH_IRQ, PIN_TOUCH_CS);
}

static int map_i(int v, int in_min, int in_max, int out_min, int out_max)
{
    if (in_max == in_min) return out_min;
    long t = (long)(v - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    return (int)t;
}

bool touch_read(int16_t *x, int16_t *y)
{
    /* Average a few samples — resistive touch on CYD is noisy */
    int sum_x = 0, sum_y = 0, n = 0;
    for (int i = 0; i < 3; i++) {
        int16_t rx = 0, ry = 0, z = 0;
        if (!xpt_read_raw(&rx, &ry, &z)) {
            if (n == 0) return false;
            break;
        }
        sum_x += rx;
        sum_y += ry;
        n++;
        esp_rom_delay_us(200);
    }
    if (n == 0) return false;

    int rx = sum_x / n;
    int ry = sum_y / n;

    int tx = map_i(rx, TS_MINX, TS_MAXX, 0, BOARD_SCR_W - 1);
    int ty = map_i(ry, TS_MINY, TS_MAXY, 0, BOARD_SCR_H - 1);

    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx >= BOARD_SCR_W) tx = BOARD_SCR_W - 1;
    if (ty >= BOARD_SCR_H) ty = BOARD_SCR_H - 1;

    *x = (int16_t)tx;
    *y = (int16_t)ty;
    return true;
}
