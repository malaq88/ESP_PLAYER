#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void display_init(void);
void display_set_backlight(bool on);
bool display_backlight_on(void);

void display_fill(uint16_t color);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color);
void display_draw_rect(int x, int y, int w, int h, uint16_t color);
void display_draw_hline(int x, int y, int w, uint16_t color);
void display_draw_vline(int x, int y, int h, uint16_t color);
void display_fill_circle(int x0, int y0, int r, uint16_t color);
void display_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);

/** 6x8 bitmap font; scale 1 or 2. Returns width drawn in pixels. */
int display_draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
int display_text_width(const char *s, int scale);
void display_draw_text_centered(int y, int max_w, const char *s, uint16_t fg, uint16_t bg, int scale);

void display_push_image(int x, int y, int w, int h, const uint16_t *data);

/* Shared SPI mutex for TFT bus only (SD uses a separate SPI host). */
void display_bus_lock(void);
void display_bus_unlock(void);

#ifdef __cplusplus
}
#endif
