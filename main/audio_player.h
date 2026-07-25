#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define AUDIO_RING_FRAMES   4096
#define VIS_BUF_LEN         512
#define VIS_N               128
#define NUM_VIS_BARS        8

typedef enum {
    PLAYER_STOPPED = 0,
    PLAYER_PLAYING,
    PLAYER_PAUSED
} player_state_t;

typedef enum {
    AUDIO_NONE = 0,
    AUDIO_MP3,
    AUDIO_WAV
} audio_type_t;

#ifdef __cplusplus
extern "C" {
#endif

void audio_init(void);
void audio_set_volume_percent(int pct);
int  audio_get_volume_percent(void);

void audio_stop(bool flush_ring);
bool audio_start_track(int playlist_index, bool gapless);
void audio_next(bool gapless);
void audio_prev(void);
void audio_toggle_pause(void);

void audio_toggle_shuffle(void);
void audio_cycle_repeat(void);
bool audio_shuffle_on(void);
int  audio_repeat_mode(void); /* 0 off, 1 all, 2 one */

void audio_pump(int max_loops); /* lightweight; decode task does the real work */
void audio_service(void);

player_state_t audio_state(void);
audio_type_t   audio_type(void);
int            audio_current_track(void);
uint32_t       audio_elapsed_sec(void);
uint32_t       audio_duration_sec(void);
uint32_t       audio_wav_rate_hz(void);
const char    *audio_current_album(void);

/** A2DP pull: fills stereo int16 frames[n][2], pads with silence, returns n. */
int32_t audio_a2dp_fill(int16_t (*frames)[2], int32_t n);

/** Visualizer */
void audio_vis_compute(void);
void audio_vis_decay(void);
const float *audio_vis_bands(void);
float audio_vis_sample_rate(void);

size_t audio_ring_avail(void);

#ifdef __cplusplus
}
#endif
