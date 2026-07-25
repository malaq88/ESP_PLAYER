# ESP PLAYER — CYD Album Player (ESP-IDF)

Port ESP-IDF do [CYDAlbumPlayer](https://github.com/malaq88/CYDAlbumPlayer) para **ESP32-2432S028 (Cheap Yellow Display)**.  
ESP-IDF port of [CYDAlbumPlayer](https://github.com/malaq88/CYDAlbumPlayer) for the **ESP32-2432S028 (Cheap Yellow Display)**.

Tema visual: **neon cyberpunk / CRT** (ciano, magenta, azul elétrico).  
UI theme: **neon cyberpunk / CRT** (cyan, magenta, electric blue).

---

## Visão geral / Overview

| PT | EN |
|----|-----|
| Player de álbuns a partir de microSD (`.mp3` / `.wav`) | Album player from microSD (`.mp3` / `.wav`) |
| Saída local no alto-falante do CYD **ou** fone Bluetooth A2DP | Local CYD speaker output **or** Bluetooth A2DP headset |
| Indicador **SPK / BT** na UI + boost de volume no amp | **SPK / BT** UI badge + local amp volume boost |
| UI touch: Home → Música / Bluetooth → Browser → Player | Touch UI: Home → Music / Bluetooth → Browser → Player |
| Backlight com timeout e botão BOOT | Backlight idle timeout + BOOT button |

---

## Hardware

Placa: **ESP32-2432S028 (CYD)** — TFT ILI9341 240×320, touch resistivo XPT2046, microSD SPI, amp onboard (SPEAK), LED RGB traseiro.  
Board: **ESP32-2432S028 (CYD)** — ILI9341 240×320 TFT, resistive XPT2046 touch, SPI microSD, onboard amp (SPEAK), rear RGB LED.

Mapa de pinos / Pin map (`main/board.h`):

| Função / Function | GPIO |
|-------------------|------|
| TFT MOSI / MISO / SCLK / CS / DC / BL | 13 / 12 / 14 / 15 / 2 / 21 |
| SD MOSI / MISO / SCLK / CS (SPI3) | 23 / 19 / 18 / 5 |
| Touch CLK / MISO / MOSI / CS / IRQ | 25 / 39 / 32 / 33 / 36 |
| Speaker DAC (amp SPEAK/P4) | **26** |
| BOOT (power backlight) | 0 |
| RGB R / G / B (active LOW) | 4 / 16 / 17 |

**Alto-falante / Speaker:** conectar no conector **SPEAK** (JST). O áudio local usa o **DAC interno do ESP32** no GPIO 26 — não é I2S.  
Plug a speaker into the **SPEAK** connector. Local audio uses the ESP32 **internal DAC** on GPIO 26 — not I2S.

---

## Funcionamento do app / App behaviour

### Boot

1. Inicializa NVS, LED RGB, power/backlight, touch, decoder e saída local (DAC).  
   Initializes NVS, RGB LED, power/backlight, touch, decoder and local DAC output.
2. Monta o cartão SD (FAT32). Falha → tela de erro.  
   Mounts the SD card (FAT32). Failure → error screen.
3. Splash (“NEON RETRO”) e inicia **A2DP Source** em background (não bloqueia a Home).  
   Splash (“NEON RETRO”) and starts **A2DP Source** in the background (Home is not blocked).
4. Escaneia faixas, monta álbuns e abre a **Home**.  
   Scans tracks, builds albums and opens **Home**.

A música **não** exige Bluetooth. Sem fone, toca no speaker local.  
Music does **not** require Bluetooth. Without a headset, it plays on the local speaker.

### Telas / Screens

```
Home
 ├─ Music ──────────► Browser (álbuns → faixas) ──► Player
 └─ Bluetooth ──────► BT Devices (lista / conectar)
```

| Tela / Screen | PT | EN |
|---------------|----|----|
| **Home** | Cards Music (magenta) e Bluetooth (azul) | Music (magenta) and Bluetooth (blue) cards |
| **Browser** | Pastas = álbuns; lista de faixas; Back | Folders = albums; track list; Back |
| **Player** | Espectro, seek, volume, transport, shuffle/repeat | Spectrum, seek, volume, transport, shuffle/repeat |
| **BT Devices** | Scan/lista de dispositivos Classic; escolher fone | Classic device scan/list; pick a headset |

Navegação com stack de **Back** (volta à tela anterior).  
Navigation uses a **Back** stack (returns to the previous screen).

### Áudio: roteamento / Audio routing

```
SD (.mp3/.wav) → decoder → ring PCM
                              ├─ BT conectado  → A2DP Source (stereo)
                              └─ BT desconectado → DAC GPIO26 (mono 8-bit → amp)
```

| Condição / Condition | Saída / Output |
|----------------------|----------------|
| Bluetooth **conectado** | Só A2DP — o speaker local **para** |
| Bluetooth **desconectado** + playing | Speaker local (DAC) |
| Pausado / parado | Nenhuma saída ativa |

As duas saídas **nunca** tocam ao mesmo tempo: ao conectar o fone, o DAC entra em idle; ao desconectar, o PCM volta para o speaker.  
Both outputs **never** play at once: connecting a headset idles the DAC; disconnecting sends PCM back to the speaker.

A UI mostra um badge **SPK** (magenta) ou **BT** (azul) na Home e no Player; o volume exibe `SPK 45%` / `BT 45%` e uma barra. O caminho local aplica boost (~2×) + soft-clip no DAC.  
The UI shows an **SPK** (magenta) or **BT** (blue) badge on Home and Player; volume shows `SPK 45%` / `BT 45%` plus a bar. The local path applies ~2× DAC boost with soft-clip.

Qualidade local ≈ mono 8-bit (limite do DAC do ESP32). A2DP permanece stereo.  
Local quality ≈ 8-bit mono (ESP32 DAC limit). A2DP stays stereo.

### Player

- **Espectro** — 8 barras (domínio do tempo), atualização leve para não travar o touch  
  **Spectrum** — 8 bars (time-domain), lightweight so touch stays responsive  
- **Seek** — barra de progresso tocável  
  **Seek** — tappable progress bar  
- **Volume ±** — aplica no decoder e no volume A2DP  
  **Volume ±** — applied to the decoder and A2DP volume  
- **Prev / Play-Pause / Next**  
- **Shuffle** e **Repeat** (off → all → one)  
  **Shuffle** and **Repeat** (off → all → one)

### Cartão SD / SD card

FAT32, estrutura por pastas (cada pasta = álbum):  
FAT32, folder-based layout (each folder = album):

```
/Album A/faixa01.mp3
/Album A/faixa02.wav
/Album B/musica.mp3
```

Limites / Limits (`sd_music.h`):

| Item | Valor / Value |
|------|---------------|
| Faixas / Tracks | 300 |
| Álbuns / Albums | 32 |
| Formatos / Formats | `.mp3`, `.wav` |

### Energia / Power

- Backlight desliga após **30 s** sem toque (`DISPLAY_IDLE_OFF_MS`); a música continua.  
  Backlight turns off after **30 s** idle; music keeps playing.
- Botão **BOOT** (GPIO 0): liga/desliga backlight (música continua).  
  **BOOT** button (GPIO 0): toggles backlight (music continues).
- LED RGB: indica estado BT / reprodução (active LOW).  
  RGB LED: reflects BT / playback state (active LOW).

---

## Arquitetura do código / Code layout

```
main/
  app_main.cpp      — boot, loop UI
  board.h           — pinos / pins
  theme.h           — cores neon / neon colors
  display.*         — ILI9341
  touch.*           — XPT2046 bitbang
  power.*           — backlight, BOOT, idle
  sd_music.*        — FAT + scan álbuns/faixas
  audio_player.*    — decode MP3/WAV, ring, volume, playlist
  audio_local.*     — DAC GPIO26 quando sem BT
  bt_source.*       — A2DP Source (ESP32-A2DP)
  ui.*              — telas e touch
  minimp3.h         — decoder MP3

components/ESP32-A2DP/   — cópia local / local copy (PCM callbacks only)
partitions.csv           — app ~3 MB
```

Decode roda em task no **core 1**; saída local (DAC) no **core 0**.  
Decode runs on a task on **core 1**; local DAC output on **core 0**.

---

## Build & Flash

**Requisitos / Requirements:** ESP-IDF **v6.0.x**, target `esp32`, flash ≥ 4 MB.

Plugin ESP-IDF (Cursor/VS Code):

1. Abrir esta pasta / Open this folder  
2. Target: `esp32`  
3. Se o cache CMake estiver sujo: **Full Clean**, depois **Build**  
4. Flash / Monitor  

CLI (exemplo Windows com EIM / Espressif tools):

```powershell
. "C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1"
cd C:\Users\atrit\Workspace\ESP-IDF\ESP_PLAYER
idf.py build
idf.py -p COMx flash monitor
```

Partições / Partitions: `partitions.csv` — app factory **3 MB** (BT + decoder).

---

## Dependência Bluetooth / Bluetooth dependency

`components/ESP32-A2DP` é uma cópia local do [pschatzmann/ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP), configurada **apenas com callbacks PCM** (sem `arduino-audio-tools`). O ESP32 atua como **A2DP Source** (envia áudio para fone/caixa Classic).  

Local copy of ESP32-A2DP with **PCM callbacks only** (no `arduino-audio-tools`). The ESP32 acts as an **A2DP Source** (sends audio to a Classic headset/speaker).

---

## Troubleshooting

| Sintoma / Symptom | O que checar / Check |
|-------------------|----------------------|
| Tela branca / cores invertidas | `MADCTL` (`0x36`) em `display.cpp` |
| Touch desalinhado | `TS_MINX`…`TS_MAXY` em `board.h` |
| SD Failed | FAT32; pinos CS=5 MOSI=23 MISO=19 SCK=18 |
| Sem som local | Speaker no **SPEAK**; volume > 0; BT **desconectado** |
| Sem som no fone | Parear em **Bluetooth** (Home); Classic A2DP |
| App não cabe na flash | `partitions.csv` (~3 MB app) |

---

## Créditos / Credits

- Original Arduino: [malaq88/CYDAlbumPlayer](https://github.com/malaq88/CYDAlbumPlayer)  
- A2DP: [pschatzmann/ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP)  
- MP3: [lieff/minimp3](https://github.com/lieff/minimp3)  
- Framework: [Espressif ESP-IDF](https://github.com/espressif/esp-idf)
