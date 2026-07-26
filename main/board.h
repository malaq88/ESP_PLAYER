#pragma once

/** Hardware map for ESP32-2432S028 (Cheap Yellow Display). */

#define BOARD_SCR_W           240
#define BOARD_SCR_H           320

/* TFT (VSPI / HSPI shared bus with SD) */
#define PIN_TFT_MISO          12
#define PIN_TFT_MOSI          13
#define PIN_TFT_SCLK          14
#define PIN_TFT_CS            15
#define PIN_TFT_DC            2
#define PIN_TFT_RST           -1
#define PIN_TFT_BL            21

/* SD card — VSPI (SPI3), NOT the TFT bus */
#define PIN_SD_CS             5
#define PIN_SD_MOSI           23
#define PIN_SD_MISO           19
#define PIN_SD_SCLK           18
#define SD_SPI_HOST           SPI3_HOST

/* Resistive touch (separate HSPI-style pins) */
#define PIN_TOUCH_CLK         25
#define PIN_TOUCH_MISO        39
#define PIN_TOUCH_MOSI        32
#define PIN_TOUCH_CS          33
#define PIN_TOUCH_IRQ         36

/* BOOT button — active LOW; also ESP32 strapping pin */
#define PIN_BOOT_BTN          0

/* Rear RGB LED — active LOW */
#define PIN_RGB_R             4
#define PIN_RGB_G             16
#define PIN_RGB_B             17

/* Onboard amp (SC8002B/PAM8002A) via SPEAK/P4 — ESP32 DAC CH1 */
#define PIN_SPK_DAC           26

/* Touch calibration (raw ADC → screen) — typical CYD / RNT ranges */
#define TS_MINX               200
#define TS_MAXX               3700
#define TS_MINY               240
#define TS_MAXY               3800

#define DISPLAY_IDLE_OFF_MS   30000
#define TOUCH_DEBOUNCE_MS     160
