#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// ES7210 mic + ES8388 speaker through the Tab5 BSP, one duplex I2S bus at 16 kHz mono PCM16.
bool audio_init(void);
void audio_rec_start(int side);        // non-blocking; called from the LVGL press event
void audio_rec_stop(void);             // non-blocking; the recorder task builds the job
bool audio_recording(void);
float audio_rec_seconds(void);
int audio_level(void);                 // 0..100 peak of the last 20 ms chunk
// Blocking playback (call from the worker). Resamples to 16 kHz if rate differs. Stops early on audio_play_stop().
void audio_play(const int16_t *pcm, size_t samples, int rate);
void audio_play_stop(void);
bool audio_playing(void);
// Helpers
void wav_header(uint8_t *h44, size_t pcm_bytes, int rate);
bool wav_parse(const uint8_t *wav, size_t len, const int16_t **pcm, size_t *samples, int *rate);
