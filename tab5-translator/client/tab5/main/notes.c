// Voice notes: big record button, live timer + level, list of notes on SD (newest first), note viewer.
// Files: /sdcard/papp/notes/<stamp>.wav + <stamp>.md (markdown from the server: title, summary, bullets,
// actions, then the transcript). Server: T5-S /summarize?lang=auto&out=es (API.md).
#include "notes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "ui_priv.h"
#include "papp.h"
#include "papp_http.h"
#include "audio.h"
#include "svc.h"
#include "sdcard.h"

static const char *TAG = "notes";
#define NOTE_MAX_SEC 300
static lv_obj_t *s_scr, *n_btn, *n_btn_l, *n_time, *n_bar, *n_st, *n_list, *s_view, *v_text;
static char s_stamp[24];

static void stamp_now(char *b, size_t n)
{
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    if (tm.tm_year + 1900 >= 2026) strftime(b, n, "%Y%m%d-%H%M%S", &tm);
    else snprintf(b, n, "up-%lld", (long long)(esp_timer_get_time() / 1000000));
}

static void open_note(const char *stamp);
static void on_note(lv_event_t *e) { open_note(lv_obj_get_user_data(lv_event_get_current_target(e))); }
static void free_ud(lv_event_t *e) { free(lv_obj_get_user_data(lv_event_get_current_target(e))); }

static void list_refresh(void)                 // display lock held
{
    lv_obj_clean(n_list);
    DIR *d = opendir(SD_ROOT "/notes");
    if (!d) { label(n_list, f24, C_DIM, sd_ok() ? "sin notas todavía" : LV_SYMBOL_WARNING " sin tarjeta SD"); return; }
    char *names[64]; int nn = 0; struct dirent *de;
    while ((de = readdir(d)) && nn < 64) {
        size_t l = strlen(de->d_name);
        if (l > 3 && !strcmp(de->d_name + l - 3, ".md")) names[nn++] = strndup(de->d_name, l - 3);
    }
    closedir(d);
    for (int i = 0; i < nn; i++) for (int j = i + 1; j < nn; j++) if (strcmp(names[i], names[j]) < 0) { char *t = names[i]; names[i] = names[j]; names[j] = t; }
    for (int i = 0; i < nn; i++) {
        char p[96], title[120] = "";
        snprintf(p, sizeof p, SD_ROOT "/notes/%s.md", names[i]);
        FILE *f = fopen(p, "r");
        if (f) { if (fgets(title, sizeof title, f)) title[strcspn(title, "\n")] = 0; fclose(f); }
        lv_obj_t *b = lv_obj_create(n_list);
        plain(b, C_PANEL);
        lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(b, 12, 0); lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(b, lv_color_hex(C_LINE), LV_STATE_PRESSED);
        label(b, f24, C_TEXT, title[0] == '#' ? title + 2 : (title[0] ? title : "(sin título)"));
        label(b, f18, C_DIM, names[i]);
        lv_obj_set_user_data(b, names[i]);                     // freed with the row
        lv_obj_add_event_cb(b, on_note, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(b, free_ud, LV_EVENT_DELETE, NULL);
    }
    if (!nn) label(n_list, f24, C_DIM, "sin notas todavía");
}

// ---------- viewer ----------
static void open_note(const char *stamp)
{
    char p[96]; snprintf(p, sizeof p, SD_ROOT "/notes/%s.md", stamp);
    FILE *f = fopen(p, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 60000) n = 60000;
    char *t = malloc(n + 1);
    if (!t) { fclose(f); return; }
    n = fread(t, 1, n, f); t[n] = 0; fclose(f);
    if (!s_view) {
        lv_obj_t *body = app_frame(&s_view, "Nota  ·  笔记", C_VIOLET);
        lv_obj_set_scroll_dir(body, LV_DIR_VER);
        v_text = label(body, f24, C_TEXT, "");
        lv_obj_set_width(v_text, 1240);
        lv_label_set_long_mode(v_text, LV_LABEL_LONG_WRAP);
    }
    lv_label_set_text(v_text, t);
    free(t);
    open_scr(s_view);
}

// ---------- record ----------
static void sum_done(const papp_result_t *r, void *ud)
{
    char *md = r->http_status == 200 ? json_str(r->tgt_text, "markdown") : NULL;
    char *title = r->http_status == 200 ? json_str(r->tgt_text, "title") : NULL;
    char p[96]; snprintf(p, sizeof p, SD_ROOT "/notes/%s.md", s_stamp);
    if (md) {
        FILE *f = fopen(p, "w"); if (f) { fputs(md, f); fclose(f); }
    } else {                                                   // keep a stub so the WAV is findable
        FILE *f = fopen(p, "w");
        if (f) { fprintf(f, "# Nota sin resumen (%s)\n\nError %d — audio guardado en %s.wav\n", s_stamp, r->http_status, s_stamp); fclose(f); }
    }
    ESP_LOGI(TAG, "summary %s: HTTP %d in %d ms, %u chars", s_stamp, r->http_status, r->rtt_ms, md ? (unsigned)strlen(md) : 0);
    lock();
    char b[200];
    if (md) snprintf(b, sizeof b, LV_SYMBOL_OK " %s  ·  %.1f s", title ? title : "", r->rtt_ms / 1000.0);
    else snprintf(b, sizeof b, LV_SYMBOL_WARNING " sin resumen (error %d) — el audio quedó en la SD", r->http_status);
    lv_label_set_text(n_st, b);
    list_refresh();
    unlock();
    free(md); free(title);
}

static void got_wav(uint8_t *wav, size_t len, void *ud)        // recorder task
{
    char p[96]; snprintf(p, sizeof p, SD_ROOT "/notes/%s.wav", s_stamp);
    mkdir(SD_ROOT "/notes", 0775);
    int64_t t0 = esp_timer_get_time();
    FILE *f = fopen(p, "wb"); if (f) { fwrite(wav, 1, len, f); fclose(f); }
    ESP_LOGI(TAG, "%s: %u kB to SD in %d ms", p, (unsigned)(len / 1024), (int)((esp_timer_get_time() - t0) / 1000));
    lock(); lv_label_set_text(n_st, LV_SYMBOL_UPLOAD " transcribiendo y resumiendo …"); unlock();
    if (!svc_post("/summarize?lang=auto&out=es", "audio/wav", wav, len, sum_done, NULL)) {   // svc owns wav now
        lock(); lv_label_set_text(n_st, LV_SYMBOL_WARNING " ocupado — el audio quedó en la SD"); unlock();
    }
}

static void rec_toggle(void)
{
    if (audio_recording()) { audio_note_stop(); return; }
    if (!sd_ok()) { lv_label_set_text(n_st, LV_SYMBOL_WARNING " sin tarjeta SD"); return; }
    stamp_now(s_stamp, sizeof s_stamp);
    if (audio_note_start(NOTE_MAX_SEC, got_wav, NULL)) lv_label_set_text(n_st, "grabando · 录音中 — toca para terminar");
}
static void on_rec(lv_event_t *e) { rec_toggle(); }

static void tick(lv_timer_t *t)
{
    if (lv_screen_active() != s_scr) return;
    bool rec = audio_recording();
    lv_obj_set_style_bg_color(n_btn, lv_color_hex(rec ? C_RED : C_PANEL), 0);
    lv_label_set_text(n_btn_l, rec ? LV_SYMBOL_STOP : LV_SYMBOL_AUDIO);
    float s = audio_rec_seconds();
    char b[32]; snprintf(b, sizeof b, "%d:%02d / 5:00", (int)s / 60, (int)s % 60);
    lv_label_set_text(n_time, rec ? b : "0:00 / 5:00");
    lv_bar_set_value(n_bar, rec ? audio_level() : 0, LV_ANIM_OFF);
}

void notes_app_open(void)
{
    if (!s_scr) {
        lv_obj_t *body = app_frame(&s_scr, "Notas de voz  ·  语音笔记", C_VIOLET);
        lv_obj_t *left = lv_obj_create(body); plain(left, C_BG);
        lv_obj_set_size(left, 420, LV_PCT(100));
        lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(left, 18, 0);
        n_btn = lv_obj_create(left); plain(n_btn, C_PANEL);
        lv_obj_set_size(n_btn, 240, 240); lv_obj_set_style_radius(n_btn, 120, 0);
        lv_obj_set_style_border_width(n_btn, 6, 0); lv_obj_set_style_border_color(n_btn, lv_color_hex(C_RED), 0);
        lv_obj_add_flag(n_btn, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(n_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(n_btn, on_rec, LV_EVENT_CLICKED, NULL);
        n_btn_l = label(n_btn, &lv_font_montserrat_40, C_TEXT, LV_SYMBOL_AUDIO); lv_obj_center(n_btn_l);
        n_time = label(left, f40, C_TEXT, "0:00 / 5:00");
        n_bar = lv_bar_create(left); lv_obj_set_size(n_bar, 320, 14); lv_bar_set_range(n_bar, 0, 100);
        lv_obj_set_style_bg_color(n_bar, lv_color_hex(C_VIOLET), LV_PART_INDICATOR);
        n_st = label(left, f18, C_DIM, "toca para grabar · 点击录音 · tap to record");
        lv_obj_set_width(n_st, 400); lv_label_set_long_mode(n_st, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(n_st, LV_TEXT_ALIGN_CENTER, 0);
        n_list = lv_obj_create(body); plain(n_list, C_BG);
        lv_obj_set_size(n_list, 1280 - 32 - 440, LV_PCT(100));
        lv_obj_align(n_list, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_flex_flow(n_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(n_list, 10, 0);
        lv_obj_set_scroll_dir(n_list, LV_DIR_VER);
        lv_timer_create(tick, 200, NULL);
    }
    list_refresh();
    open_scr(s_scr);
}

bool notes_key(int enter)
{
    if (!s_scr || lv_screen_active() != s_scr) return false;
    if (enter) rec_toggle();
    return true;
}
