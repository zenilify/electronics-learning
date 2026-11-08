#pragma once
#include "papp.h"
// Background service task for the non-PTT apps: text translate, speak (SD-cached), FX rates, prefetch.
// Callbacks run on the service task — take the display lock (bsp_display_lock) before touching LVGL.
// The result passed to the callback is freed after the callback returns (copy what you keep).
typedef void (*svc_cb_t)(const papp_result_t *r, void *ud);
void svc_start(void);
bool svc_busy(void);
bool svc_text(lang_t src, lang_t tgt, const char *text, bool speak, svc_cb_t cb, void *ud); // /translate-text
bool svc_say(lang_t lang, const char *text, svc_cb_t cb, void *ud);   // SD cache -> /tts -> speaker
bool svc_json(const char *path, svc_cb_t cb, void *ud);               // r->tgt_text = JSON body
// Fill the SD speech cache for n texts in `lang` (no playback); progress(i, n) after each item.
typedef void (*svc_progress_t)(int i, int n, int ok, void *ud);
bool svc_prefetch(lang_t lang, const char *const *texts, int n, svc_progress_t prog, void *ud);
