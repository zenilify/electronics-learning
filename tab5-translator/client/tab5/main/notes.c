// Voice notes (Erick 11-08 UI notes): record -> pick the output language (ES / EN / 中文) -> transcript + summary.
// The WAV stays on SD, so any note can be re-done in another language from the viewer. "Historial" lists past notes.
// Files: /sdcard/papp/notes/<stamp>.wav, <stamp>.md (latest), <stamp>.<lang>.md (one per language asked).
// Server: /summarize?lang=auto&out=es|en|zh (server/API.md).
#include "notes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "ui_priv.h"
#include "papp.h"
#include "papp_http.h"
#include "audio.h"
#include "svc.h"
#include "sdcard.h"
#include "settings.h"

static const char *TAG = "notes";
#define NOTE_MAX_SEC 300
static const char *OUT_Q[3] = { "es", "en", "zh" };
static const char *OUT_T[3] = { "Español", "English", "中文" };
static lv_obj_t *s_scr, *n_btn, *n_btn_l, *n_time, *n_bar, *n_st, *n_ask, *n_lang[3];
static lv_obj_t *s_hist, *h_list, *s_view, *v_text, *v_lang[3], *v_st;
static char s_stamp[24];          // note being recorded / summarised
static char s_view_stamp[24];     // note open in the viewer
static volatile bool s_have_wav;  // recorded, waiting for a language choice

static void stamp_now(char *b, size_t n)
{
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    if (tm.tm_year + 1900 >= 2026) strftime(b, n, "%Y%m%d-%H%M%S", &tm);
    else snprintf(b, n, "up-%lld", (long long)(esp_timer_get_time() / 1000000));
}

// ---------- generic text viewer (also used by Diálogo's history) ----------
static lv_obj_t *s_txt, *t_lbl, *t_title;
void text_screen_show(const char *title, const char *text)
{
    if (!s_txt) {
        lv_obj_t *body = app_frame(&s_txt, "", C_VIOLET);
        lv_obj_set_scroll_dir(body, LV_DIR_VER);
        t_title = label(body, f30, C_TEXT, "");
        t_lbl = label(body, f24, C_TEXT, "");
        lv_obj_set_width(t_lbl, 1240); lv_label_set_long_mode(t_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_align(t_lbl, LV_ALIGN_TOP_LEFT, 0, 50);
    }
    lv_label_set_text(t_title, title);
    lv_label_set_text(t_lbl, text);
    open_scr(s_txt);
}

static char *read_file(const char *p, long max)
{
    FILE *f = fopen(p, "r"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); long off = n > max ? n - max : 0; fseek(f, off, SEEK_SET);
    char *t = malloc(n - off + 1);
    if (t) { n = fread(t, 1, n - off, f); t[n] = 0; }
    fclose(f);
    return t;
}

// ---------- viewer ----------
static void show_note(const char *stamp)            // lock held
{
    char p[96]; snprintf(p, sizeof p, SD_ROOT "/notes/%s.md", stamp);
    char *t = read_file(p, 60000);
    if (!t) return;
    strlcpy(s_view_stamp, stamp, sizeof s_view_stamp);
    lv_label_set_text(v_text, t);
    lv_label_set_text(v_st, "");
    free(t);
    open_scr(s_view);
}

static void sum_done(const papp_result_t *r, void *ud)
{
    int li = (int)(intptr_t)ud;
    char *md = r->http_status == 200 ? json_str(r->tgt_text, "markdown") : NULL;
    char *title = r->http_status == 200 ? json_str(r->tgt_text, "title") : NULL;
    char p[112];
    if (md) {
        snprintf(p, sizeof p, SD_ROOT "/notes/%s.%s.md", s_stamp, OUT_Q[li]);
        FILE *f = fopen(p, "w"); if (f) { fputs(md, f); fclose(f); }
        snprintf(p, sizeof p, SD_ROOT "/notes/%s.md", s_stamp);
        f = fopen(p, "w"); if (f) { fputs(md, f); fclose(f); }
    } else {
        snprintf(p, sizeof p, SD_ROOT "/notes/%s.md", s_stamp);
        struct stat st;
        if (stat(p, &st) != 0) {                              // keep a stub so the WAV is findable in History
            FILE *f = fopen(p, "w");
            if (f) { fprintf(f, "# Nota sin resumen (%s)\n\nError %d — el audio quedó en %s.wav\n", s_stamp, r->http_status, s_stamp); fclose(f); }
        }
    }
    ESP_LOGI(TAG, "summary %s out=%s: HTTP %d in %d ms, %u chars", s_stamp, OUT_Q[li], r->http_status, r->rtt_ms, md ? (unsigned)strlen(md) : 0);
    lock();
    char b[200];
    if (md) snprintf(b, sizeof b, LV_SYMBOL_OK " %s  ·  %.1f s", title ? title : "", r->rtt_ms / 1000.0);
    else snprintf(b, sizeof b, LV_SYMBOL_WARNING " sin resumen (error %d) — el audio quedó en la SD", r->http_status);
    lv_label_set_text(n_st, b);
    if (v_st) lv_label_set_text(v_st, md ? "" : b);
    if (md) show_note(s_stamp);                               // straight to the result
    unlock();
    free(md); free(title);
}

static bool summarize(const char *stamp, int li)             // reads the WAV from SD; lock held (UI calls)
{
    char p[96]; snprintf(p, sizeof p, SD_ROOT "/notes/%s.wav", stamp);
    FILE *f = fopen(p, "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *w = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!w || fread(w, 1, n, f) != (size_t)n) { fclose(f); free(w); return false; }
    fclose(f);
    strlcpy(s_stamp, stamp, sizeof s_stamp);
    char path[64]; snprintf(path, sizeof path, "/summarize?lang=auto&out=%s", OUT_Q[li]);
    return svc_post(path, "audio/wav", w, n, sum_done, (void *)(intptr_t)li);   // svc owns w
}

static void on_view_lang(lv_event_t *e)                     // re-do the open note in another language
{
    int li = (int)(intptr_t)lv_event_get_user_data(e);
    char p[112]; snprintf(p, sizeof p, SD_ROOT "/notes/%s.%s.md", s_view_stamp, OUT_Q[li]);
    char *t = read_file(p, 60000);                            // already done in that language: just show it
    if (t) { lv_label_set_text(v_text, t); free(t); return; }
    if (summarize(s_view_stamp, li)) lv_label_set_text(v_st, LV_SYMBOL_REFRESH " traduciendo y resumiendo …");
    else lv_label_set_text(v_st, LV_SYMBOL_WARNING " ocupado o sin audio");
}

// ---------- history ----------
static void on_note(lv_event_t *e) { show_note(lv_obj_get_user_data(lv_event_get_current_target(e))); }
static void free_ud(lv_event_t *e) { free(lv_obj_get_user_data(lv_event_get_current_target(e))); }

static void hist_refresh(void)                 // display lock held
{
    lv_obj_clean(h_list);
    DIR *d = opendir(SD_ROOT "/notes");
    if (!d) { label(h_list, f24, C_DIM, sd_ok() ? "sin notas todavía" : LV_SYMBOL_WARNING " sin tarjeta SD"); return; }
    char *names[100]; int nn = 0; struct dirent *de;
    while ((de = readdir(d)) && nn < 100) {
        size_t l = strlen(de->d_name);
        if (l > 3 && !strcmp(de->d_name + l - 3, ".md") && !memchr(de->d_name, '.', l - 3))   // <stamp>.md only
            names[nn++] = strndup(de->d_name, l - 3);
    }
    closedir(d);
    for (int i = 0; i < nn; i++) for (int j = i + 1; j < nn; j++) if (strcmp(names[i], names[j]) < 0) { char *t = names[i]; names[i] = names[j]; names[j] = t; }
    for (int i = 0; i < nn; i++) {
        char p[96], title[120] = "";
        snprintf(p, sizeof p, SD_ROOT "/notes/%s.md", names[i]);
        FILE *f = fopen(p, "r");
        if (f) { if (fgets(title, sizeof title, f)) title[strcspn(title, "\n")] = 0; fclose(f); }
        char when[40] = "";                                    // 20261108-113512 -> 2026-11-08 11:35
        if (strlen(names[i]) == 15 && names[i][8] == '-')
            snprintf(when, sizeof when, "%.4s-%.2s-%.2s %.2s:%.2s", names[i], names[i] + 4, names[i] + 6, names[i] + 9, names[i] + 11);
        lv_obj_t *b = lv_obj_create(h_list);
        plain(b, C_PANEL);
        lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(b, 14, 0); lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(b, lv_color_hex(C_LINE), LV_STATE_PRESSED);
        label(b, f30, C_TEXT, title[0] == '#' ? title + 2 : (title[0] ? title : "(sin título)"));
        label(b, f18, C_DIM, when[0] ? when : names[i]);
        lv_obj_set_user_data(b, names[i]);
        lv_obj_add_event_cb(b, on_note, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(b, free_ud, LV_EVENT_DELETE, NULL);
    }
    if (!nn) label(h_list, f24, C_DIM, "sin notas todavía");
}
static void on_hist(lv_event_t *e) { hist_refresh(); open_scr(s_hist); }

// ---------- record ----------
static void got_wav(uint8_t *wav, size_t len, void *ud)        // recorder task
{
    char p[96]; snprintf(p, sizeof p, SD_ROOT "/notes/%s.wav", s_stamp);
    mkdir(SD_ROOT "/notes", 0775);
    FILE *f = fopen(p, "wb"); if (f) { fwrite(wav, 1, len, f); fclose(f); }
    free(wav);
    ESP_LOGI(TAG, "%s: %u kB to SD", p, (unsigned)(len / 1024));
    s_have_wav = true;
    lock();
    lv_obj_remove_flag(n_ask, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(n_st, "¿En qué idioma quieres la transcripción y el resumen?");
    unlock();
}

static void on_lang(lv_event_t *e)
{
    int li = (int)(intptr_t)lv_event_get_user_data(e);
    settings_set_u8("nlang", (uint8_t)li);
    for (int i = 0; i < 3; i++) lv_obj_set_style_bg_color(n_lang[i], lv_color_hex(i == li ? C_VIOLET : C_PANEL), 0);
    if (!s_have_wav) return;                                   // just changing the default
    if (summarize(s_stamp, li)) {
        s_have_wav = false;
        lv_obj_add_flag(n_ask, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(n_st, LV_SYMBOL_UPLOAD " transcribiendo y resumiendo …");
    } else lv_label_set_text(n_st, LV_SYMBOL_WARNING " ocupado — intenta de nuevo");
}

static void rec_toggle(void)
{
    if (audio_recording()) { audio_note_stop(); return; }
    if (!sd_ok()) { lv_label_set_text(n_st, LV_SYMBOL_WARNING " sin tarjeta SD"); return; }
    s_have_wav = false;
    lv_obj_add_flag(n_ask, LV_OBJ_FLAG_HIDDEN);
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

static lv_obj_t *lang_row(lv_obj_t *p, lv_obj_t **btns, lv_event_cb_t cb, const char *caption)
{
    lv_obj_t *row = lv_obj_create(p); plain(row, C_BG);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 14, 0);
    if (caption) label(row, f24, C_DIM, caption);
    for (int i = 0; i < 3; i++) {
        btns[i] = pill(row, OUT_T[i], C_PANEL, f30);
        lv_obj_set_height(btns[i], 72);
        lv_obj_add_event_cb(btns[i], cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    return row;
}

void notes_app_open(void)
{
    if (!s_scr) {
        lv_obj_t *body = app_frame(&s_scr, "Notas de voz  ·  语音笔记", C_VIOLET);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(body, 16, 0);
        lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
        n_btn = lv_obj_create(body); plain(n_btn, C_PANEL);
        lv_obj_set_size(n_btn, 220, 220); lv_obj_set_style_radius(n_btn, 110, 0);
        lv_obj_set_style_border_width(n_btn, 6, 0); lv_obj_set_style_border_color(n_btn, lv_color_hex(C_RED), 0);
        lv_obj_add_flag(n_btn, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(n_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(n_btn, on_rec, LV_EVENT_CLICKED, NULL);
        n_btn_l = label(n_btn, &lv_font_montserrat_40, C_TEXT, LV_SYMBOL_AUDIO); lv_obj_center(n_btn_l);
        n_time = label(body, f40, C_TEXT, "0:00 / 5:00");
        n_bar = lv_bar_create(body); lv_obj_set_size(n_bar, 420, 14); lv_bar_set_range(n_bar, 0, 100);
        lv_obj_set_style_bg_color(n_bar, lv_color_hex(C_VIOLET), LV_PART_INDICATOR);
        n_st = label(body, f24, C_DIM, "toca para grabar · 点击录音 · tap to record");
        lv_obj_set_width(n_st, 1100); lv_label_set_long_mode(n_st, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(n_st, LV_TEXT_ALIGN_CENTER, 0);
        n_ask = lang_row(body, n_lang, on_lang, "Resumen en:");
        lv_obj_add_flag(n_ask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *h = pill(body, LV_SYMBOL_LIST "  Historial · 历史", C_PANEL, f24);
        lv_obj_add_event_cb(h, on_hist, LV_EVENT_CLICKED, NULL);
        int li = settings_get_u8("nlang", 0); if (li > 2) li = 0;
        for (int i = 0; i < 3; i++) lv_obj_set_style_bg_color(n_lang[i], lv_color_hex(i == li ? C_VIOLET : C_PANEL), 0);
        lv_timer_create(tick, 200, NULL);
        // history screen
        lv_obj_t *hb = app_frame(&s_hist, "Historial de notas  ·  笔记历史", C_VIOLET);
        h_list = lv_obj_create(hb); plain(h_list, C_BG);
        lv_obj_set_size(h_list, LV_PCT(100), LV_PCT(100));
        lv_obj_set_flex_flow(h_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(h_list, 10, 0);
        lv_obj_set_scroll_dir(h_list, LV_DIR_VER);
        // viewer
        lv_obj_t *vb = app_frame(&s_view, "Nota  ·  笔记", C_VIOLET);
        lv_obj_t *vr = lang_row(vb, v_lang, on_view_lang, "Ver en:");
        lv_obj_align(vr, LV_ALIGN_TOP_RIGHT, 0, -4);
        v_st = label(vb, f18, C_DIM, "");
        lv_obj_align(v_st, LV_ALIGN_TOP_LEFT, 0, 20);
        lv_obj_t *sc = lv_obj_create(vb); plain(sc, C_BG);
        lv_obj_set_size(sc, LV_PCT(100), 520); lv_obj_align(sc, LV_ALIGN_TOP_LEFT, 0, 84);
        lv_obj_set_scroll_dir(sc, LV_DIR_VER);
        v_text = label(sc, f24, C_TEXT, "");
        lv_obj_set_width(v_text, 1230); lv_label_set_long_mode(v_text, LV_LABEL_LONG_WRAP);
    }
    open_scr(s_scr);
}

bool notes_key(int enter, const char *text)   // Enter/space = record/stop; 1/2/3 = Español/English/中文 after recording
{
    if (!s_scr || lv_screen_active() != s_scr) return false;
    if (text && text[0] >= '1' && text[0] <= '3' && s_have_wav) {
        int li = text[0] - '1';
        settings_set_u8("nlang", (uint8_t)li);
        if (summarize(s_stamp, li)) { s_have_wav = false; lv_obj_add_flag(n_ask, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(n_st, LV_SYMBOL_UPLOAD " transcribiendo y resumiendo …"); }
        return true;
    }
    if (enter) rec_toggle();
    return true;
}
