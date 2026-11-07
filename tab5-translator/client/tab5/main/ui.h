#pragma once
#include "papp.h"
#include "lvgl.h"
// All ui_* calls from non-LVGL tasks take the BSP display lock internally.
void ui_init(lv_display_t *disp);
void ui_busy(const papp_job_t *job, const char *msg);  // spinner + message; NULL job = idle
void ui_set_hint(const char *msg);
// Adds a conversation bubble. Takes ownership of r->pcm (kept for tap-to-replay) and of the texts.
void ui_add_turn(const papp_job_t *job, papp_result_t *r, bool offline_demo);
// Error bubble with a retry button; takes ownership of job (re-submitted on tap).
void ui_add_error(papp_job_t *job, const papp_result_t *r);
void ui_demo_state(bool running, int idx, int count);
