# ESP PLAYER — CYD Album Player (ESP-IDF)

[English](README.en.md)

Port ESP-IDF do [CYDAlbumPlayer](https://github.com/malaq88/CYDAlbumPlayer) para **ESP32-2432S028 (Cheap Yellow Display)**.

Tema visual: **preto + amarelo** (combinando com o case do CYD).

---

## Visão geral

- Player de álbuns a partir de microSD (`.mp3` / `.wav`)
- Saída local no alto-falante do CYD **ou** fone Bluetooth A2DP
- Indicador **SPK / BT** na UI + boost de volume no amp
- UI touch: Home → Música / Bluetooth → Browser → Player
- LED RGB: azul fixo (BT) / verde fixo (speaker) só durante o play
- Atalho **Now playing** no browser (barra larga sob o header)

---

## Hardware

Placa: **ESP32-2432S028 (CYD)** — TFT ILI9341 240×320, touch resistivo XPT2046, microSD SPI, amp onboard (SPEAK), LED RGB traseiro.

Mapa de pinos (`main/board.h`):

| Função | GPIO |
|--------|------|
| TFT MOSI / MISO / SCLK / CS / DC / BL | 13 / 12 / 14 / 15 / 2 / 21 |
| SD MOSI / MISO / SCLK / CS (SPI3) | 23 / 19 / 18 / 5 |
| Touch CLK / MISO / MOSI / CS / IRQ | 25 / 39 / 32 / 33 / 36 |
| Speaker DAC (amp SPEAK/P4) | **26** |
| BOOT (power backlight) | 0 |
| RGB R / G / B (active LOW) | 4 / 16 / 17 |

**Alto-falante:** conectar no conector **SPEAK** (JST). O áudio local usa o **DAC interno do ESP32** no GPIO 26 — não é I2S.

---

## Funcionamento do app

### Boot

1. Inicializa NVS, LED RGB, power/backlight, touch, decoder e saída local (DAC).
2. Monta o cartão SD (FAT32). Falha → tela de erro.
3. Splash e inicia **A2DP Source** em background (não bloqueia a Home).
4. Escaneia faixas, monta álbuns e abre a **Home**.

A música **não** exige Bluetooth. Sem fone, toca no speaker local.

### Telas

```
Home
 ├─ Music ──────────► Browser (álbuns → faixas) ──► Player
 └─ Bluetooth ──────► BT Devices (lista / conectar)
```

| Tela | Descrição |
|------|-----------|
| **Home** | Cards Music e Bluetooth |
| **Browser** | Pastas = álbuns; lista de faixas; Back; barra Now playing |
| **Player** | Espectro, seek, volume, transport |
| **BT Devices** | Liga/desliga BT; scan/lista Classic; escolher fone |

Navegação com stack de **Back** (volta à tela anterior).

### Áudio: roteamento

```
SD (.mp3/.wav) → decoder → ring PCM
                              ├─ BT conectado  → A2DP Source (stereo)
                              └─ BT desconectado → DAC GPIO26 (mono 8-bit → amp)
```

| Condição | Saída |
|----------|-------|
| Bluetooth **conectado** | Só A2DP — o speaker local **para** |
| Bluetooth **desconectado** + playing | Speaker local (DAC) |
| Pausado / parado | Nenhuma saída ativa |

As duas saídas **nunca** tocam ao mesmo tempo: ao conectar o fone, o DAC entra em idle; ao desconectar, o PCM volta para o speaker.

A UI mostra um badge **SPK** ou **BT** na Home e no Player; o volume exibe `SPK 45%` / `BT 45%` e uma barra. O caminho local aplica boost (~2×) + soft-clip no DAC.

Qualidade local ≈ mono 8-bit (limite do DAC do ESP32). A2DP permanece stereo.

### Player

- **Espectro** — 8 barras (domínio do tempo), atualização leve para não travar o touch
- **Seek** — barra de progresso tocável
- **Volume ±** — aplica no decoder e no volume A2DP
- **Prev / Play-Pause / Next**
- **Shuffle** e **Repeat** (off → all → one)

### Cartão SD

FAT32, estrutura por pastas (cada pasta = álbum):

```
/Album A/faixa01.mp3
/Album A/faixa02.wav
/Album B/musica.mp3
```

Limites (`sd_music.h`):

| Item | Valor |
|------|-------|
| Faixas | 300 |
| Álbuns | 32 |
| Formatos | `.mp3`, `.wav` |

### Energia

- Backlight desliga após **30 s** sem toque (`DISPLAY_IDLE_OFF_MS`); a música continua.
- Botão **BOOT** (GPIO 0): liga/desliga backlight (música continua).
- LED RGB: indica estado BT / reprodução (active LOW).

---

## Arquitetura do código

```
main/
  app_main.cpp      — boot, loop UI
  board.h           — pinos
  theme.h           — cores (preto + amarelo)
  display.*         — ILI9341
  touch.*           — XPT2046 bitbang
  power.*           — backlight, BOOT, idle
  sd_music.*        — FAT + scan álbuns/faixas
  audio_player.*    — decode MP3/WAV, ring, volume, playlist
  audio_local.*     — DAC GPIO26 quando sem BT
  bt_source.*       — A2DP Source (ESP32-A2DP)
  ui.*              — telas e touch
  minimp3.h         — decoder MP3

components/ESP32-A2DP/   — cópia local (PCM callbacks only)
partitions.csv           — app ~3 MB
```

Decode roda em task no **core 1**; saída local (DAC) no **core 0**.

---

## Build & Flash

**Requisitos:** ESP-IDF **v6.0.x**, target `esp32`, flash ≥ 4 MB.

Plugin ESP-IDF (Cursor/VS Code):

1. Abrir esta pasta
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

Partições: `partitions.csv` — app factory **3 MB** (BT + decoder).

---

## Dependência Bluetooth

`components/ESP32-A2DP` é uma cópia local do [pschatzmann/ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP), configurada **apenas com callbacks PCM** (sem `arduino-audio-tools`). O ESP32 atua como **A2DP Source** (envia áudio para fone/caixa Classic).

---

## Troubleshooting

| Sintoma | O que checar |
|---------|--------------|
| Tela branca / cores invertidas (ex.: amarelo vira azul) | `MADCTL` (`0x36`) em `display.cpp` — usar RGB (`0x00`), não BGR (`0x08`) |
| Touch desalinhado | `TS_MINX`…`TS_MAXY` em `board.h` |
| SD Failed | FAT32; pinos CS=5 MOSI=23 MISO=19 SCK=18 |
| Sem som local | Speaker no **SPEAK**; volume > 0; BT **desconectado** |
| Sem som no fone | Parear em **Bluetooth** (Home); Classic A2DP |
| App não cabe na flash | `partitions.csv` (~3 MB app) |

---

## Créditos

- Original Arduino: [malaq88/CYDAlbumPlayer](https://github.com/malaq88/CYDAlbumPlayer)
- A2DP: [pschatzmann/ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP)
- MP3: [lieff/minimp3](https://github.com/lieff/minimp3)
- Framework: [Espressif ESP-IDF](https://github.com/espressif/esp-idf)
