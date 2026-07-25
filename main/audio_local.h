#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Where decoded audio is (or will be) routed. */
typedef enum {
    AUDIO_ROUTE_NONE = 0,     /* idle / no destination */
    AUDIO_ROUTE_SPEAKER,      /* CYD SPEAK amp via DAC GPIO26 */
    AUDIO_ROUTE_BLUETOOTH     /* A2DP headset */
} audio_route_t;

/** Local speaker via ESP32 DAC on GPIO26 (CYD SPEAK amp). Used when BT is down. */
void audio_local_init(void);

/** Active output path: BT wins whenever connected; otherwise speaker. */
audio_route_t audio_output_route(void);

/** True while the DAC task is actively driving the amp. */
bool audio_local_is_active(void);

#ifdef __cplusplus
}
#endif
