#pragma once

#include <stdint.h>

/** Retro CRT + cyberpunk neon — dark night city, cyan / magenta accents. */
namespace theme {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t BG         = rgb565(8, 10, 16);      /* near-black CRT */
constexpr uint16_t SURFACE    = rgb565(16, 20, 32);     /* panel */
constexpr uint16_t SURFACE2   = rgb565(28, 34, 52);
constexpr uint16_t HIGHLIGHT  = rgb565(180, 240, 255);  /* ice / glow */
constexpr uint16_t SHADOW     = rgb565(4, 6, 10);
constexpr uint16_t TEXT       = rgb565(230, 236, 248);  /* off-white */
constexpr uint16_t MUTED      = rgb565(100, 112, 140);
constexpr uint16_t ACCENT     = rgb565(0, 230, 240);    /* neon cyan */
constexpr uint16_t ACCENT_DIM = rgb565(0, 120, 140);
constexpr uint16_t WARN       = rgb565(255, 60, 100);
constexpr uint16_t OK         = rgb565(60, 230, 140);
constexpr uint16_t BAR_FILL   = rgb565(0, 230, 240);
constexpr uint16_t BAR_TRACK  = rgb565(28, 34, 52);
constexpr uint16_t SPEC_BG    = rgb565(10, 12, 20);
constexpr uint16_t SPEC_BAR   = rgb565(0, 220, 230);
constexpr uint16_t SPEC_PEAK  = rgb565(255, 45, 160);
constexpr uint16_t ROW_SEL    = rgb565(0, 160, 180);
constexpr uint16_t DIVIDER    = rgb565(40, 50, 72);
constexpr uint16_t ICON_MUSIC = rgb565(255, 40, 150);   /* magenta */
constexpr uint16_t ICON_BT    = rgb565(80, 120, 255);   /* electric blue */

} // namespace theme
