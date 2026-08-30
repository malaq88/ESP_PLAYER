#pragma once

#include <stdint.h>

/**
 * Cyberpunk night city — deep navy canvas, cyan + magenta neon accents.
 */
namespace theme {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t BG         = rgb565(6, 8, 18);       /* night sky */
constexpr uint16_t SURFACE    = rgb565(12, 16, 32);     /* panel */
constexpr uint16_t SURFACE2   = rgb565(20, 28, 48);     /* elevated */
constexpr uint16_t HIGHLIGHT  = rgb565(200, 255, 255);  /* ice glow */
constexpr uint16_t SHADOW     = rgb565(2, 4, 10);
constexpr uint16_t TEXT       = rgb565(220, 230, 248);
constexpr uint16_t MUTED      = rgb565(88, 102, 138);
constexpr uint16_t ACCENT     = rgb565(0, 220, 235);    /* neon cyan */
constexpr uint16_t ACCENT_DIM = rgb565(0, 90, 110);
constexpr uint16_t ACCENT2    = rgb565(255, 45, 160);   /* hot magenta */
constexpr uint16_t WARN       = rgb565(255, 70, 90);
constexpr uint16_t OK         = rgb565(0, 235, 170);
constexpr uint16_t BAR_FILL   = rgb565(0, 220, 235);
constexpr uint16_t BAR_TRACK  = rgb565(18, 26, 44);
constexpr uint16_t SPEC_BG    = rgb565(8, 10, 22);
constexpr uint16_t SPEC_BAR   = rgb565(0, 190, 210);
constexpr uint16_t SPEC_PEAK  = rgb565(255, 50, 150);
constexpr uint16_t ROW_SEL    = rgb565(0, 160, 180);
constexpr uint16_t DIVIDER    = rgb565(28, 40, 68);
constexpr uint16_t GRID       = rgb565(14, 20, 36);
constexpr uint16_t ICON_MUSIC = rgb565(255, 50, 150);
constexpr uint16_t ICON_BT    = rgb565(70, 140, 255);

} // namespace theme
