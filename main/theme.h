#pragma once

#include <stdint.h>

/**
 * Black + yellow — matches the device case.
 * Near-black canvas, charcoal surfaces, yellow accents for actions.
 */
namespace theme {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t BG         = rgb565(10, 10, 10);     /* deep black */
constexpr uint16_t SURFACE    = rgb565(22, 22, 22);     /* elevated */
constexpr uint16_t SURFACE2   = rgb565(36, 36, 36);     /* buttons / tracks */
constexpr uint16_t HIGHLIGHT  = rgb565(255, 230, 140);  /* light yellow */
constexpr uint16_t SHADOW     = rgb565(0, 0, 0);
constexpr uint16_t TEXT       = rgb565(240, 240, 240);
constexpr uint16_t MUTED      = rgb565(150, 150, 150);
constexpr uint16_t ACCENT     = rgb565(240, 190, 40);   /* case yellow */
constexpr uint16_t ACCENT_DIM = rgb565(120, 90, 20);    /* muted yellow edge */
constexpr uint16_t WARN       = rgb565(220, 80, 70);
constexpr uint16_t OK         = rgb565(240, 190, 40);
constexpr uint16_t BAR_FILL   = rgb565(240, 190, 40);
constexpr uint16_t BAR_TRACK  = rgb565(36, 36, 36);
constexpr uint16_t SPEC_BG    = rgb565(14, 14, 14);
constexpr uint16_t SPEC_BAR   = rgb565(200, 155, 30);
constexpr uint16_t SPEC_PEAK  = rgb565(255, 220, 100);
constexpr uint16_t ROW_SEL    = rgb565(240, 190, 40);
constexpr uint16_t DIVIDER    = rgb565(48, 48, 48);
constexpr uint16_t ICON_MUSIC = rgb565(240, 190, 40);   /* yellow */
constexpr uint16_t ICON_BT    = rgb565(210, 170, 50);   /* warm yellow */

} // namespace theme
