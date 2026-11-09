#pragma once
#include <stdbool.h>
// Voice notes (D-T5-5 + Erick): record up to 5 min -> SD (WAV) -> POST /summarize -> markdown note on SD.
void notes_app_open(void);
bool notes_key(int enter, const char *text);   // display lock held; Enter = start/stop, 1/2/3 = output language
// Conversation (hands-free, VAD) — conv.c
void conv_app_open(void);
void conv_leave(void);         // stop listening (any context)
bool conv_key(int enter);
#include <stdint.h>
#include <stddef.h>
void conv_inject(const uint8_t *wav, size_t len);
void text_screen_show(const char *title, const char *text);   // full-screen scrollable text (notes.c)
// Live subtitles (ROADMAP row 10) — subs.c. Server: POST /stream/start|chunk|stop (T5-S API.md v0.2.0).
void subs_app_open(void);
void subs_leave(void);
bool subs_key(int enter, const char *text);    // display lock held: Enter = start/stop, 1/2/3 = ES/EN/中文
void subs_inject(const uint8_t *wav, size_t len);   // console test: a WAV paced in 1 s chunks
