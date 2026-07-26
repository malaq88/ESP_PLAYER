#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void touch_init(void);

/** Instant sample (may be noisy on first contact). */
bool touch_read(int16_t *x, int16_t *y);

/**
 * Wait for a complete tap: sample while pressed, then return a
 * filtered coordinate (much more accurate on resistive CYD panels).
 */
bool touch_read_tap(int16_t *x, int16_t *y);

#ifdef __cplusplus
}
#endif
