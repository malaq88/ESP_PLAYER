# ESP PLAYER — CYD Album Player (ESP-IDF)

[Português](README.md)

ESP-IDF port of [CYDAlbumPlayer](https://github.com/malaq88/CYDAlbumPlayer) for the **ESP32-2432S028 (Cheap Yellow Display)**.

Visual theme: **black + yellow** (matches the CYD case).

---

## Overview

- Album player from microSD (`.mp3` / `.wav`)
- Local CYD speaker output **or** Bluetooth A2DP headset
- **SPK / BT** UI badge + local amp volume boost
- Touch UI: Home → Music / Bluetooth → Browser → Player
- RGB LED: solid blue (BT) / solid green (speaker) while playing only
- **Now playing** shortcut in the browser (full-width bar under the header)

---

## Hardware

Board: **ESP32-2432S028 (CYD)** — ILI9341 240×320 TFT, resistive XPT2046 touch, SPI microSD, onboard amp (SPEAK), rear RGB LED.

Pin map (`main/board.h`):

| Function | GPIO |
|----------|------|
| TFT MOSI / MISO / SCLK / CS / DC / BL | 13 / 12 / 14 / 15 / 2 / 21 |
| SD MOSI / MISO / SCLK / CS (SPI3) | 23 / 19 / 18 / 5 |
| Touch CLK / MISO / MOSI / CS / IRQ | 25 / 39 / 32 / 33 / 36 |
| Speaker DAC (amp SPEAK/P4) | **26** |
| BOOT (power backlight) | 0 |
| RGB R / G / B (active LOW) | 4 / 16 / 17 |

**Speaker:** plug into the **SPEAK** connector (JST). Local audio uses the ESP32 **internal DAC** on GPIO 26 — not I2S.

---

## App behaviour

### Boot

1. Initializes NVS, RGB LED, power/backlight, touch, decoder and local DAC output.
2. Mounts the SD card (FAT32). Failure → error screen.
3. Splash and starts **A2DP Source** in the background (Home is not blocked).
4. Scans tracks, builds albums and opens **Home**.

Music does **not** require Bluetooth. Without a headset, it plays on the local speaker.

### Screens

```
Home
 ├─ Music ──────────► Browser (albums → tracks) ──► Player
 └─ Bluetooth ──────► BT Devices (list / connect)
```

| Screen | Description |
|--------|-------------|
| **Home** | Music and Bluetooth cards |
| **Browser** | Folders = albums; track list; Back; Now playing bar |
| **Player** | Spectrum, seek, volume, transport |
| **BT Devices** | BT on/off; Classic scan/list; pick a headset |

Navigation uses a **Back** stack (returns to the previous screen).

### Audio routing

```
SD (.mp3/.wav) → decoder → ring PCM
                              ├─ BT connected    → A2DP Source (stereo)
                              └─ BT disconnected → DAC GPIO26 (mono 8-bit → amp)
```

| Condition | Output |
|-----------|--------|
| Bluetooth **connected** | A2DP only — local speaker **stops** |
| Bluetooth **disconnected** + playing | Local speaker (DAC) |
| Paused / stopped | No active output |

Both outputs **never** play at once: connecting a headset idles the DAC; disconnecting sends PCM back to the speaker.

The UI shows an **SPK** or **BT** badge on Home and Player; volume shows `SPK 45%` / `BT 45%` plus a bar. The local path applies ~2× DAC boost with soft-clip.

Local quality ≈ 8-bit mono (ESP32 DAC limit). A2DP stays stereo.

### Player

- **Spectrum** — 8 bars (time-domain), lightweight so touch stays responsive
- **Seek** — tappable progress bar
- **Volume ±** — applied to the decoder and A2DP volume
- **Prev / Play-Pause / Next**
- **Shuffle** and **Repeat** (off → all → one)

### SD card

FAT32, folder-based layout (each folder = album):

```
/Album A/track01.mp3
/Album A/track02.wav
/Album B/song.mp3
```

Limits (`sd_music.h`):

| Item | Value |
|------|-------|
| Tracks | 300 |
| Albums | 32 |
| Formats | `.mp3`, `.wav` |

### Power

- Backlight turns off after **30 s** idle (`DISPLAY_IDLE_OFF_MS`); music keeps playing.
- **BOOT** button (GPIO 0): toggles backlight (music continues).
- RGB LED: reflects BT / playback state (active LOW).

---

## Code layout

```
main/
  app_main.cpp      — boot, UI loop
  board.h           — pins
  theme.h           — colors (black + yellow)
  display.*         — ILI9341
  touch.*           — XPT2046 bitbang
  power.*           — backlight, BOOT, idle
  sd_music.*        — FAT + album/track scan
  audio_player.*    — MP3/WAV decode, ring, volume, playlist
  audio_local.*     — DAC GPIO26 when BT is off
  bt_source.*       — A2DP Source (ESP32-A2DP)
  ui.*              — screens and touch
  minimp3.h         — MP3 decoder

components/ESP32-A2DP/   — local copy (PCM callbacks only)
partitions.csv           — app ~3 MB
```

Decode runs on a task on **core 1**; local DAC output on **core 0**.

---

## Build & Flash

**Requirements:** ESP-IDF **v6.0.x**, target `esp32`, flash ≥ 4 MB.

ESP-IDF plugin (Cursor/VS Code):

1. Open this folder
2. Target: `esp32`
3. If CMake cache is dirty: **Full Clean**, then **Build**
4. Flash / Monitor

CLI (Windows example with EIM / Espressif tools):

```powershell
. "C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1"
cd C:\Users\atrit\Workspace\ESP-IDF\ESP_PLAYER
idf.py build
idf.py -p COMx flash monitor
```

Partitions: `partitions.csv` — factory app **3 MB** (BT + decoder).

---

## Bluetooth dependency

`components/ESP32-A2DP` is a local copy of [pschatzmann/ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP), configured with **PCM callbacks only** (no `arduino-audio-tools`). The ESP32 acts as an **A2DP Source** (sends audio to a Classic headset/speaker).

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| White screen / swapped colors (e.g. yellow looks blue) | `MADCTL` (`0x36`) in `display.cpp` — use RGB (`0x00`), not BGR (`0x08`) |
| Misaligned touch | `TS_MINX`…`TS_MAXY` in `board.h` |
| SD Failed | FAT32; pins CS=5 MOSI=23 MISO=19 SCK=18 |
| No local sound | Speaker on **SPEAK**; volume > 0; BT **disconnected** |
| No headset sound | Pair under **Bluetooth** (Home); Classic A2DP |
| App does not fit flash | `partitions.csv` (~3 MB app) |

---

## Credits

- Original Arduino: [malaq88/CYDAlbumPlayer](https://github.com/malaq88/CYDAlbumPlayer)
- A2DP: [pschatzmann/ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP)
- MP3: [lieff/minimp3](https://github.com/lieff/minimp3)
- Framework: [Espressif ESP-IDF](https://github.com/espressif/esp-idf)
