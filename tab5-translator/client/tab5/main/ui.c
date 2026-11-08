// PAPP Tab5 UI — dark split screen (D1, Erick 2026-11-07): left PTT = ES/EN speaker (teal),
// right PTT = 中文 speaker (amber), conversation in the middle, status bar on top.
#include "ui.h"
#include "audio.h"
#include "papp_http.h"
#include "battery.h"
#include "settings.h"
#include "bsp/m5stack_tab5.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "ui_priv.h"

#define MAX_TURNS 24

extern const uint8_t cjk_start[] asm("_binary_papp_cjk_ttf_start");
extern const uint8_t cjk_end[]   asm("_binary_papp_cjk_ttf_end");

static lv_display_t *s_disp;
lv_font_t *f18, *f24, *f30, *f40, *f_logo;
static lv_obj_t *scr, *chat, *lbl_ep, *lbl_wifi, *lbl_batt, *btn_demo, *lbl_demo, *spin, *lbl_status;
static lv_obj_t *pairbtn[PAIR_COUNT];
static lv_obj_t *ptt[2], *ptt_lang[2], *ptt_hint[2], *ptt_bar[2];
static int64_t s_busy_t0;
static bool s_busy;
static char s_busy_msg[96];

static uint32_t side_color(int side) { return side ? C_AMBER : C_TEAL; }

lv_font_t *mkfont(int px)
{
    lv_font_t *f = lv_tiny_ttf_create_data(cjk_start, cjk_end - cjk_start, px);
    if (!f) return (lv_font_t *)&lv_font_montserrat_24;
    f->fallback = px >= 36 ? &lv_font_montserrat_40 : px >= 26 ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
    return f;
}

lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, uint32_t col, const char *t)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_label_set_text(l, t);
    return l;
}

void plain(lv_obj_t *o, uint32_t bg)
{
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

// ---------- pair selector ----------
static void refresh_ptt_labels(void)
{
    lang_t l, r; pair_langs(papp_get_pair(), &l, &r);
    lv_label_set_text(ptt_lang[0], lang_label(l));
    lv_label_set_text(ptt_lang[1], lang_label(r));
    lv_label_set_text(ptt_hint[0], l == LANG_ZH ? "按住说话" : l == LANG_EN ? "Hold to talk" : "Mantén para hablar");
    lv_label_set_text(ptt_hint[1], r == LANG_ZH ? "按住说话" : r == LANG_EN ? "Hold to talk" : "Mantén para hablar");
    for (int i = 0; i < PAIR_COUNT; i++) {
        bool on = i == (int)papp_get_pair();
        lv_obj_set_style_bg_color(pairbtn[i], lv_color_hex(on ? C_LINE : C_PANEL), 0);
        lv_obj_set_style_border_color(pairbtn[i], lv_color_hex(on ? C_TEXT : C_LINE), 0);
    }
}

static void on_pair(lv_event_t *e)
{
    if (audio_recording() || s_busy) return;
    papp_set_pair((pair_t)(intptr_t)lv_event_get_user_data(e));
    refresh_ptt_labels();
}

// ---------- push-to-talk ----------
static void on_ptt(lv_event_t *e)
{
    int side = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED) {
        if (s_busy || papp_demo_running()) { ui_set_hint("Espera · 请稍候"); return; }
        audio_rec_start(side);
        lv_obj_set_style_bg_color(ptt[side], lv_color_hex(side_color(side)), 0);
        lv_obj_set_style_text_color(ptt_lang[side], lv_color_hex(C_BG), 0);
        lv_obj_set_style_text_color(ptt_hint[side], lv_color_hex(C_BG), 0);
    } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
        audio_rec_stop();
        lv_obj_set_style_bg_color(ptt[side], lv_color_hex(C_PANEL), 0);
        lv_obj_set_style_text_color(ptt_lang[side], lv_color_hex(side_color(side)), 0);
        lv_obj_set_style_text_color(ptt_hint[side], lv_color_hex(C_DIM), 0);
        lv_bar_set_value(ptt_bar[side], 0, LV_ANIM_OFF);
        refresh_ptt_labels();
    }
}

static lv_obj_t *mk_ptt(lv_obj_t *p, int side)
{
    lv_obj_t *b = lv_obj_create(p);
    plain(b, C_PANEL);
    lv_obj_set_size(b, 300, 640);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(side_color(side)), 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b, on_ptt, LV_EVENT_PRESSED, (void *)(intptr_t)side);
    lv_obj_add_event_cb(b, on_ptt, LV_EVENT_RELEASED, (void *)(intptr_t)side);
    lv_obj_add_event_cb(b, on_ptt, LV_EVENT_PRESS_LOST, (void *)(intptr_t)side);
    lv_obj_t *mic = label(b, &lv_font_montserrat_40, side_color(side), LV_SYMBOL_AUDIO);
    lv_obj_align(mic, LV_ALIGN_TOP_MID, 0, 70);
    ptt_lang[side] = label(b, f_logo, side_color(side), "");
    lv_obj_align(ptt_lang[side], LV_ALIGN_CENTER, 0, -30);
    ptt_hint[side] = label(b, f30, C_DIM, "");
    lv_obj_set_width(ptt_hint[side], 260);
    lv_obj_set_style_text_align(ptt_hint[side], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ptt_hint[side], LV_ALIGN_CENTER, 0, 90);
    ptt_bar[side] = lv_bar_create(b);
    lv_obj_set_size(ptt_bar[side], 220, 14);
    lv_obj_align(ptt_bar[side], LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_bar_set_range(ptt_bar[side], 0, 60);
    lv_obj_set_style_bg_color(ptt_bar[side], lv_color_hex(C_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ptt_bar[side], lv_color_hex(side_color(side)), LV_PART_INDICATOR);
    return b;
}

// ---------- bubbles ----------
typedef struct { int16_t *pcm; size_t n; int rate; papp_job_t *retry; } bubble_t;

static void on_bubble_del(lv_event_t *e)
{
    bubble_t *b = lv_event_get_user_data(e);
    if (!b) return;
    free(b->pcm);
    if (b->retry) { if (b->retry->wav_owned) free(b->retry->wav); free(b->retry); }
    free(b);
}

static void on_bubble_tap(lv_event_t *e);
static void on_retry(lv_event_t *e);

static lv_obj_t *mk_bubble(int side, uint32_t border)
{
    while (lv_obj_get_child_count(chat) >= MAX_TURNS) lv_obj_delete(lv_obj_get_child(chat, 0));
    lv_obj_t *b = lv_obj_create(chat);
    plain(b, C_PANEL);
    lv_obj_set_width(b, LV_PCT(94));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b, 18, 0);
    lv_obj_set_style_pad_all(b, 16, 0);
    lv_obj_set_style_pad_row(b, 6, 0);
    lv_obj_set_style_border_side(b, side ? LV_BORDER_SIDE_RIGHT : LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(b, 6, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(border), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, side ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, side ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

static lv_obj_t *wrap_label(lv_obj_t *p, const lv_font_t *f, uint32_t col, const char *t, int side)
{
    lv_obj_t *l = label(p, f, col, t && *t ? t : "—");
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, side ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
    return l;
}

void ui_add_turn(const papp_job_t *job, papp_result_t *r, bool offline)
{
    lock();
    lv_obj_t *b = mk_bubble(job->side, side_color(job->side));
    char hdr[160];
    snprintf(hdr, sizeof hdr, "%s → %s", lang_label(job->src), lang_label(job->tgt));
    wrap_label(b, f18, C_DIM, hdr, job->side);
    wrap_label(b, f24, C_DIM, r->src_text, job->side);
    wrap_label(b, f40, C_TEXT, r->tgt_text, job->side);
    char meta[200];
    if (offline) snprintf(meta, sizeof meta, LV_SYMBOL_PLAY "  demo · sin conexión / offline");
    else snprintf(meta, sizeof meta, LV_SYMBOL_PLAY "  %.1f s  ·  ASR %.1f · MT %.1f · TTS %.1f  ·  %s",
                  r->rtt_ms / 1000.f, r->t_asr / 1000.f, r->t_nmt / 1000.f, r->t_tts / 1000.f,
                  papp_endpoint_name(r->endpoint));
    wrap_label(b, f18, side_color(job->side), meta, job->side);
    bubble_t *bd = calloc(1, sizeof *bd);
    bd->pcm = r->pcm; bd->n = r->samples; bd->rate = r->rate;
    r->pcm = NULL;                        // ownership moved to the bubble
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, on_bubble_tap, LV_EVENT_CLICKED, bd);
    lv_obj_add_event_cb(b, on_bubble_del, LV_EVENT_DELETE, bd);
    lv_obj_scroll_to_view(b, LV_ANIM_ON);
    unlock();
}

void ui_add_error(papp_job_t *job, const papp_result_t *r)
{
    lock();
    lv_obj_t *b = mk_bubble(job->side, C_RED);
    const char *msg;
    char tmp[120];
    switch (r->http_status) {
    case 401: msg = papp_have_token() ? "Token rechazado (401) · 令牌无效" : "Sin token — provisionar por USB · 未配置令牌"; break;
    case 400: msg = "Audio o idioma no válido (400) · 音频无效"; break;
    case 413: msg = "Demasiado largo (> 30 s) · 太长了"; break;
    case 422: msg = "No se entendió — habla más cerca · 没听清，请靠近再说"; break;
    case 502: msg = "Servicio no disponible (502) · 服务不可用"; break;
    case -1:  msg = "Sin conexión al servidor · 无法连接服务器"; break;
    default:  snprintf(tmp, sizeof tmp, "Error HTTP %d · 出错了", r->http_status); msg = tmp;
    }
    wrap_label(b, f24, C_RED, msg, job->side);
    bubble_t *bd = calloc(1, sizeof *bd);
    bd->retry = job;
    if (r->http_status != 401 && r->http_status != 400 && r->http_status != 413 && r->http_status != 422) {
        lv_obj_t *btn = lv_button_create(b);
        lv_obj_set_style_bg_color(btn, lv_color_hex(C_LINE), 0);
        lv_obj_set_style_radius(btn, 14, 0);
        lv_obj_t *l = label(btn, f24, C_TEXT, LV_SYMBOL_REFRESH "  Reintentar · 重试");
        lv_obj_center(l);
        lv_obj_add_event_cb(btn, on_retry, LV_EVENT_CLICKED, bd);
    }
    lv_obj_add_event_cb(b, on_bubble_del, LV_EVENT_DELETE, bd);
    lv_obj_scroll_to_view(b, LV_ANIM_ON);
    unlock();
}

void papp_replay(int16_t *pcm, size_t n, int rate);   // main.c
static void on_bubble_tap(lv_event_t *e)
{
    bubble_t *b = lv_event_get_user_data(e);
    if (b && b->pcm && !s_busy && !audio_recording()) papp_replay(b->pcm, b->n, b->rate);
}

static void on_retry(lv_event_t *e)
{
    bubble_t *b = lv_event_get_user_data(e);
    if (!b || !b->retry || s_busy) return;
    papp_job_t *j = b->retry;
    // give the job a fresh copy of the audio: the bubble keeps its own until it is deleted
    papp_job_t *n = malloc(sizeof *n); *n = *j;
    if (j->wav_owned) { n->wav = malloc(j->wav_len); memcpy(n->wav, j->wav, j->wav_len); }
    if (!papp_submit(n)) { if (n->wav_owned) free(n->wav); free(n); }
}

// ---------- status ----------
void ui_busy(const papp_job_t *job, const char *msg)
{
    lock();
    s_busy = job != NULL;
    s_busy_t0 = esp_timer_get_time();
    snprintf(s_busy_msg, sizeof s_busy_msg, "%s", msg ? msg : "");
    if (s_busy) lv_obj_remove_flag(spin, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(spin, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lbl_status, s_busy ? s_busy_msg : "Mantén un botón para hablar · 按住按钮说话");
    unlock();
}

void ui_set_hint(const char *msg)
{
    lock();
    lv_label_set_text(lbl_status, msg);
    unlock();
}

void ui_demo_state(bool running, int idx, int count)
{
    lock();
    char t[32];
    if (running) snprintf(t, sizeof t, LV_SYMBOL_STOP " DEMO %d/%d", idx + 1, count);
    else snprintf(t, sizeof t, LV_SYMBOL_PLAY " DEMO");
    lv_label_set_text(lbl_demo, t);
    lv_obj_set_style_bg_color(btn_demo, lv_color_hex(running ? C_AMBER : C_PANEL), 0);
    lv_obj_set_style_text_color(lbl_demo, lv_color_hex(running ? C_BG : C_TEXT), 0);
    unlock();
}

static void tick(lv_timer_t *t)
{
    static int n;
    for (int s = 0; s < 2; s++) lv_bar_set_value(ptt_bar[s], 0, LV_ANIM_OFF);
    if (audio_recording()) {
        char b[48]; int side = -1;
        for (int s = 0; s < 2; s++) if (lv_obj_has_state(ptt[s], LV_STATE_PRESSED)) side = s;
        if (side >= 0) {
            lv_bar_set_value(ptt_bar[side], audio_level(), LV_ANIM_OFF);
            snprintf(b, sizeof b, LV_SYMBOL_STOP "  %.1f s", audio_rec_seconds());
            lv_label_set_text(ptt_hint[side], b);
        }
    }
    if (s_busy) {
        char b[160]; float el = (esp_timer_get_time() - s_busy_t0) / 1e6f;
        snprintf(b, sizeof b, "%s  %.1f s%s", s_busy_msg, el,
                 el > 6 ? "  ·  primer uso: el servidor despierta (~10 s)" : "");
        lv_label_set_text(lbl_status, b);
    }
    if (++n % 10) return;           // status bar every ~1 s
    char b[96];
    if (!g_status.have_token) snprintf(b, sizeof b, LV_SYMBOL_WARNING " sin token");
    else if (g_status.endpoint < 0) snprintf(b, sizeof b, LV_SYMBOL_CLOSE " offline");
    else snprintf(b, sizeof b, LV_SYMBOL_OK " %s · %d ms", papp_endpoint_name(g_status.endpoint), g_status.health_ms);
    lv_label_set_text(lbl_ep, b);
    lv_obj_set_style_text_color(lbl_ep, lv_color_hex(g_status.endpoint < 0 || !g_status.have_token ? C_RED : C_GREEN), 0);
    if (g_status.wifi_up) snprintf(b, sizeof b, LV_SYMBOL_WIFI " %s %d", g_status.ssid, g_status.rssi);
    else snprintf(b, sizeof b, LV_SYMBOL_WIFI " —");
    lv_label_set_text(lbl_wifi, b);
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(g_status.wifi_up ? C_TEXT : C_RED), 0);
    int pct = battery_percent(g_status.batt_v);
    if (pct < 0) snprintf(b, sizeof b, LV_SYMBOL_USB);
    else snprintf(b, sizeof b, "%s %d%%%s", pct > 75 ? LV_SYMBOL_BATTERY_FULL : pct > 50 ? LV_SYMBOL_BATTERY_3 :
                  pct > 25 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY,
                  pct, g_status.batt_a > 0.05f ? " " LV_SYMBOL_CHARGE : "");
    lv_label_set_text(lbl_batt, b);
}

static void on_demo(lv_event_t *e)
{
    papp_demo_toggle(lv_event_get_code(e) == LV_EVENT_LONG_PRESSED);
}

static void on_logo_home(lv_event_t *e) { home_show(); }
lv_obj_t *ui_translator_screen(void) { return scr; }
lv_display_t *ui_display(void) { return s_disp; }

void on_logo_long(lv_event_t *e)
{
    // flip the screen 180° for the other hand / USB side; remembered in NVS
    lv_display_rotation_t r = lv_display_get_rotation(s_disp) == LV_DISPLAY_ROTATION_90 ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90;
    bsp_display_rotate(s_disp, r);
    settings_set_u8("rot", (uint8_t)r);
}

// ---------- splash ----------
static void bar_anim(void *o, int32_t v) { lv_obj_set_height(o, v); }
static void splash_done(lv_timer_t *t)
{
    lv_obj_t *sp = lv_timer_get_user_data(t);
    lv_screen_load_anim(home_screen(), LV_SCREEN_LOAD_ANIM_FADE_IN, 400, 0, true);
    (void)sp;
    lv_timer_delete(t);
}

static void splash(void)
{
    lv_obj_t *sp = lv_obj_create(NULL);
    plain(sp, C_BG);
    lv_obj_t *box = lv_obj_create(sp);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 360, 160);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, -150);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    static const int h[] = { 50, 110, 150, 90, 130, 70, 40 };
    for (int i = 0; i < 7; i++) {
        lv_obj_t *b = lv_obj_create(box);
        plain(b, i < 3 ? C_TEAL : i > 3 ? C_AMBER : C_TEXT);
        lv_obj_set_size(b, 26, h[i]);
        lv_obj_set_style_radius(b, 13, 0);
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, b); lv_anim_set_exec_cb(&a, bar_anim);
        lv_anim_set_values(&a, h[i] / 3, h[i]);
        lv_anim_set_duration(&a, 380 + i * 60);
        lv_anim_set_playback_duration(&a, 380 + i * 60);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
    lv_obj_t *t = label(sp, f_logo, C_TEXT, "PAPP");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_text_letter_space(t, 18, 0);
    lv_obj_t *s = label(sp, f30, C_DIM, "pocket translator  ·  español · english · 中文");
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 120);
    lv_obj_t *v = label(sp, f18, C_LINE, "v" PAPP_VERSION "  ·  ESP32-P4 + ESP32-C6  ·  push-to-talk");
    lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_screen_load(sp);
    lv_timer_create(splash_done, 2200, sp);
}

// ---------- build ----------
void ui_init(lv_display_t *disp)
{
    s_disp = disp;
    lock();
    f18 = mkfont(20); f24 = mkfont(26); f30 = mkfont(30); f40 = mkfont(40); f_logo = mkfont(96);

    scr = lv_obj_create(NULL);
    plain(scr, C_BG);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // top bar
    lv_obj_t *top = lv_obj_create(scr);
    plain(top, C_BG);
    lv_obj_set_size(top, LV_PCT(100), 64);
    lv_obj_set_style_pad_hor(top, 16, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 14, 0);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *logo = label(top, f30, C_TEXT, LV_SYMBOL_LEFT " PAPP");
    lv_obj_set_style_text_letter_space(logo, 6, 0);
    lv_obj_add_flag(logo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(logo, on_logo_long, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(logo, on_logo_home, LV_EVENT_SHORT_CLICKED, NULL);
    static const char *pair_txt[PAIR_COUNT] = { "ES ⇄ 中文", "EN ⇄ 中文", "ES ⇄ EN" };
    for (int i = 0; i < PAIR_COUNT; i++) {
        pairbtn[i] = lv_obj_create(top);
        plain(pairbtn[i], C_PANEL);
        lv_obj_set_size(pairbtn[i], LV_SIZE_CONTENT, 46);
        lv_obj_set_style_pad_hor(pairbtn[i], 16, 0);
        lv_obj_set_style_radius(pairbtn[i], 23, 0);
        lv_obj_set_style_border_width(pairbtn[i], 2, 0);
        lv_obj_add_flag(pairbtn[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(pairbtn[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *l = label(pairbtn[i], f24, C_TEXT, pair_txt[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(pairbtn[i], on_pair, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    lv_obj_t *sp = lv_obj_create(top); lv_obj_remove_style_all(sp); lv_obj_set_flex_grow(sp, 1);
    lbl_ep = label(top, f18, C_DIM, "…");
    lbl_wifi = label(top, f18, C_DIM, LV_SYMBOL_WIFI);
    lbl_batt = label(top, f18, C_TEXT, "");
    btn_demo = lv_obj_create(top);
    plain(btn_demo, C_PANEL);
    lv_obj_set_size(btn_demo, LV_SIZE_CONTENT, 46);
    lv_obj_set_style_pad_hor(btn_demo, 16, 0);
    lv_obj_set_style_radius(btn_demo, 23, 0);
    lv_obj_add_flag(btn_demo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(btn_demo, LV_OBJ_FLAG_SCROLLABLE);
    lbl_demo = label(btn_demo, f18, C_TEXT, LV_SYMBOL_PLAY " DEMO");
    lv_obj_center(lbl_demo);
    lv_obj_add_event_cb(btn_demo, on_demo, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_demo, on_demo, LV_EVENT_LONG_PRESSED, NULL);

    // body: PTT | chat | PTT
    ptt[0] = mk_ptt(scr, 0);
    lv_obj_align(ptt[0], LV_ALIGN_TOP_LEFT, 16, 68);
    ptt[1] = mk_ptt(scr, 1);
    lv_obj_align(ptt[1], LV_ALIGN_TOP_RIGHT, -16, 68);

    chat = lv_obj_create(scr);
    plain(chat, C_BG);
    lv_obj_set_size(chat, 1280 - 2 * 300 - 4 * 16, 576);
    lv_obj_align(chat, LV_ALIGN_TOP_MID, 0, 68);
    lv_obj_set_flex_flow(chat, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(chat, 12, 0);
    lv_obj_set_style_pad_all(chat, 4, 0);
    lv_obj_set_scroll_dir(chat, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(chat, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *st = lv_obj_create(scr);
    plain(st, C_BG);
    lv_obj_set_size(st, 1280 - 2 * 300 - 4 * 16, 56);
    lv_obj_align(st, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_remove_flag(st, LV_OBJ_FLAG_SCROLLABLE);
    spin = lv_spinner_create(st);
    lv_obj_set_size(spin, 36, 36);
    lv_obj_align(spin, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_arc_color(spin, lv_color_hex(C_TEAL), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spin, lv_color_hex(C_LINE), LV_PART_MAIN);
    lv_obj_add_flag(spin, LV_OBJ_FLAG_HIDDEN);
    lbl_status = label(st, f24, C_DIM, "Mantén un botón para hablar · 按住按钮说话");
    lv_obj_set_width(lbl_status, 560);
    lv_label_set_long_mode(lbl_status, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_status, LV_ALIGN_LEFT_MID, 52, 0);

    refresh_ptt_labels();
    lv_timer_create(tick, 100, NULL);
    home_init();
    splash();
    unlock();
}
