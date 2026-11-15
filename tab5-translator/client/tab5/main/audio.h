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
int audio_level(void);                 // 0..100 peak of the last 20 ms chunk (live subtitles: dB scale of what is sent)
// Blocking playback (call from the worker). Resamples to 16 kHz if rate differs. Stops early on audio_play_stop().
void audio_play(const int16_t *pcm, size_t samples, int rate);
void audio_play_stop(void);
bool audio_playing(void);
// Helpers
void wav_header(uint8_t *h44, size_t pcm_bytes, int rate);
bool wav_parse(const uint8_t *wav, size_t len, const int16_t **pcm, size_t *samples, int *rate);

// ---- other recording modes (one at a time; PTT is refused while one runs) ----
// Callbacks run on the recorder task and take ownership of `wav` (RIFF PCM16 mono 16 kHz, PSRAM, free()).
typedef void (*audio_wav_cb_t)(uint8_t *wav, size_t len, void *ud);
// Voice note: records until audio_note_stop() or max_sec, then cb(wav).
bool audio_note_start(int max_sec, audio_wav_cb_t cb, void *ud);
void audio_note_stop(void);
// Hands-free conversation: energy VAD with adaptive noise floor and 300 ms pre-roll; each utterance
// (0.5..15 s, ended by 0.8 s of silence) -> cb(wav). Paused while the speaker plays or audio_vad_pause(true).
bool audio_vad_start(audio_wav_cb_t cb, void *ud);
void audio_vad_stop(void);
void audio_vad_pause(bool p);
int  audio_vad_state(void);       // 0 off, 1 listening, 2 hearing speech, 3 paused
// Live subtitles: raw PCM16 mono 16 kHz in chunk_ms pieces (no header) -> cb(pcm, bytes) on the recorder task.
bool audio_stream_start(int chunk_ms, audio_wav_cb_t cb, void *ud);
void audio_stream_stop(void);
bool audio_streaming(void);
// The stream is recorded at its own analog gain and amplified so that the room's noise floor sits just under the
// server's speech detector (audio.c). stats: noise floor (RMS, before the gain) and the gain of the last chunk x10.
void audio_stream_tune(float analog_db, int max_gain);
void audio_stream_stats(int *noise_rms, int *gain_x10);
int audio_stream_mic_db_x10(void);       // analog gain in use right now x10 (steps down when the converter is overdriven)
// Console test: log peak / RMS of the microphone for `seconds` (2 lines per second), at `gain_db` of analog gain when
// that is >= 0 (the normal gain is restored afterwards). Refused while recording.
void audio_mic_probe(int seconds, float gain_db);
// speaker volume 0..100, persisted (NVS vol); the Dial changes it
int audio_get_volume(void);
void audio_set_volume(int v);
