/* Streaming stereo 16-bit audio output via ASND, with a playback clock for A/V sync. */
#ifndef WIITUBE_AUDIO_H
#define WIITUBE_AUDIO_H

#include <stdint.h>

void audio_init(void);               /* once at startup */
void audio_reset(int sample_rate);   /* new stream; clears the buffer */
int audio_space(void);               /* frames that can be pushed now */
int audio_buffered(void);            /* frames waiting to be played */
/* Queue decoded audio; pts (seconds) is used for the first push only (later audio is
 * assumed contiguous). frames must not exceed audio_space(). */
void audio_push(const int16_t *pcm, int frames, double pts);
void audio_play(void);               /* start output (after some audio is buffered) */
void audio_pause(int paused);
void audio_stop(void);
/* Time (seconds, in stream timestamps) of the sample being heard now; < 0 before play. */
double audio_clock(void);
void audio_set_volume(int volume);   /* 0..255 */
int audio_underruns(void);

#endif
