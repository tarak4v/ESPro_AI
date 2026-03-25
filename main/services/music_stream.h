/**
 * @file music_stream.h
 * @brief Internet radio streaming — free MP3 streams decoded and played locally.
 */
#ifndef SERVICES_MUSIC_STREAM_H
#define SERVICES_MUSIC_STREAM_H

#include <stdbool.h>
#include <stdint.h>

/** Start streaming the current radio station. */
void music_stream_play(void);

/** Stop streaming. */
void music_stream_stop(void);

/** Switch to next station in the preset list. */
void music_stream_next(void);

/** Get name of the currently selected station. */
const char *music_stream_station_name(void);

/** Returns true while streaming is active. */
bool music_stream_is_playing(void);

#endif /* SERVICES_MUSIC_STREAM_H */
