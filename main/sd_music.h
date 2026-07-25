#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_TRACKS          300
#define MAX_ALBUMS          32
#define MAX_ALBUM_NAME_LEN  48
#define MAX_PATH_LEN        256

#ifdef __cplusplus
extern "C" {
#endif

bool sd_music_init(void);
int  sd_music_scan(void);
int  sd_music_track_count(void);
const char *sd_music_track_path(int idx);

void sd_music_build_albums(void);
int  sd_music_album_count(void);
const char *sd_music_album_name(int idx);

void sd_music_load_album_tracks(const char *album_name);
int  sd_music_browse_track_count(void);
int  sd_music_browse_track_playlist_index(int browse_idx);

void sd_music_get_display_name(const char *path, char *out, size_t max_len);
void sd_music_album_from_path(const char *path, char *out, size_t max_len);

uint32_t sd_music_wav_duration_sec(const char *path, uint32_t *out_rate_hz);
bool sd_music_read_raw(const char *path, void *buf, size_t expect_bytes);

#ifdef __cplusplus
}
#endif
