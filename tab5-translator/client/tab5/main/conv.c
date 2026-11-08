// Hands-free conversation (D-T5-5 row 3): the Tab5 listens continuously (audio.c VAD), each utterance goes to
// POST /translate?pair=<a>,<b>&format=wav — the server's language ID decides who spoke and answers in the other
// language (X-Papp-Src / X-Papp-Tgt). Half duplex: the microphone is ignored while the reply plays.
#include "notes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ui_priv.h"
#include "papp.h"
#include "papp_http.h"
#include "audio.h"
#include "svc.h"
#include "sdcard.h"

static const char *TAG = "conv";
static lv_obj_t *s_scr, *c_go, *c_go_l, *c_st, *c_bar, *c_list, *c_pair[3];
static int s_pair;                               // 0 es,zh · 1 en,zh · 2 es,en
static const char *PAIR_Q[3] = { "es,zh", "en,zh", "es,en" };
static const char *PAIR_T[3] = { "ES ⇄ 中文", "EN ⇄ 中文", "ES ⇄ EN" };
static volatile int s_inflight;

static uint32_t lang_col(const char *l) { return !strcmp(l, "zh") ? C_AMBER : !strcmp(l, "en") ? C_BLUE : C_TEAL; }
static const char *lang_lbl(const char *l) { return !strcmp(l, "zh") ? "中文" : !strcmp(l, "en") ? "EN" : "ES"; }

static void add_turn(const char *sl, const char *src, const char *tl, const char *tgt, int ms)   // lock held
{
    if (lv_obj_get_child_count(c_list) >= 30) lv_obj_delete(lv_obj_get_child(c_list, 0));
    lv_obj_t *b = lv_obj_create(c_list);
    plain(b, C_PANEL);
    lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(b, 14, 0); lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, 0); lv_obj_set_style_border_width(b, 8, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(lang_col(sl)), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(b, 6, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    char h[300]; snprintf(h, sizeof h, "%s  %s", lang_lbl(sl), src);
    lv_obj_t *a = label(b, f24, C_DIM, h);
    snprintf(h, sizeof h, "%s  %s", lang_lbl(tl), tgt);
    lv_obj_t *t = label(b, f40, lang_col(tl), h);
    char m[32]; snprintf(m, sizeof m, "%.1f s", ms / 1000.0);
    lv_obj_t *z = label(b, f18, C_LINE, m);
    lv_obj_t *ls[] = { a, t, z };
    for (int i = 0; i < 3; i++) { lv_obj_set_width(ls[i], LV_PCT(100)); lv_label_set_long_mode(ls[i], LV_LABEL_LONG_WRAP); }
    lv_obj_scroll_to_view(b, LV_ANIM_ON);
}

static void conv_done(const papp_result_t *r, void *ud)          // svc task
{
    s_inflight = 0;
    bool ok = r->http_status == 200 && r->tgt_text && r->tgt_text[0] && r->samples;
    ESP_LOGI(TAG, "turn %s->%s HTTP %d %d ms via %s", r->src_lang, r->tgt_lang, r->http_status, r->rtt_ms,
             papp_endpoint_name(r->endpoint));
    lock();
    if (ok) add_turn(r->src_lang[0] ? r->src_lang : "?", r->src_text ? r->src_text : "", r->tgt_lang, r->tgt_text, r->rtt_ms);
    else if (r->http_status != 200) { char b[64]; snprintf(b, sizeof b, LV_SYMBOL_WARNING " error %d", r->http_status); lv_label_set_text(c_st, b); }
    unlock();
    if (ok) {
        char *ln = malloc(strlen(r->src_text ? r->src_text : "") + strlen(r->tgt_text) + 32);
        if (ln) { sprintf(ln, "%s>%s\t%s\t%s", r->src_lang, r->tgt_lang, r->src_text ? r->src_text : "", r->tgt_text); sd_log("conversation.tsv", ln); free(ln); }
        audio_vad_pause(true);
        audio_play(r->pcm, r->samples, r->rate);
        vTaskDelay(pdMS_TO_TICKS(300));                           // let the room echo die down
        audio_vad_pause(false);
    }
}

static void on_utt(uint8_t *wav, size_t len, void *ud)            // recorder task
{
    if (s_inflight) { free(wav); ESP_LOGW(TAG, "still translating the previous turn — utterance dropped"); return; }
    char path[96];
    snprintf(path, sizeof path, "/translate?pair=%s&format=wav&rate=%d", PAIR_Q[s_pair], PAPP_RATE);
    s_inflight = 1;
    if (!svc_post(path, "audio/wav", wav, len, conv_done, NULL)) s_inflight = 0;   // svc owns/free()s wav
}

static void set_pair(int p)
{
    s_pair = p;
    for (int i = 0; i < 3; i++) lv_obj_set_style_bg_color(c_pair[i], lv_color_hex(i == p ? C_TEAL : C_PANEL), 0);
}
static void on_pair(lv_event_t *e) { set_pair((int)(intptr_t)lv_event_get_user_data(e)); }

static void toggle(void)
{
    if (audio_vad_state()) { audio_vad_stop(); return; }
    audio_vad_start(on_utt, NULL);
}
static void on_go(lv_event_t *e) { toggle(); }

static void tick(lv_timer_t *t)
{
    if (lv_screen_active() != s_scr) return;
    int st = audio_vad_state();
    static const char *TXT[] = { "toca para empezar · 点击开始", "escuchando · 正在听 …", "oyendo · 听到了 …", "hablando · 播放中 …" };
    if (s_inflight && st != 3) lv_label_set_text(c_st, LV_SYMBOL_REFRESH " traduciendo · 翻译中 …");
    else lv_label_set_text(c_st, TXT[st]);
    lv_obj_set_style_bg_color(c_go, lv_color_hex(st ? (st == 2 ? C_GREEN : C_TEAL) : C_PANEL), 0);
    lv_label_set_text(c_go_l, st ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_bar_set_value(c_bar, st ? audio_level() : 0, LV_ANIM_OFF);
}

void conv_leave(void) { audio_vad_stop(); }

void conv_app_open(void)
{
    if (!s_scr) {
        lv_obj_t *body = app_frame(&s_scr, "Conversación  ·  对话", C_TEAL);
        lv_obj_t *left = lv_obj_create(body); plain(left, C_BG);
        lv_obj_set_size(left, 360, LV_PCT(100));
        lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(left, 14, 0);
        for (int i = 0; i < 3; i++) {
            c_pair[i] = pill(left, PAIR_T[i], C_PANEL, f24);
            lv_obj_set_width(c_pair[i], 300);
            lv_obj_add_event_cb(c_pair[i], on_pair, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
        c_go = lv_obj_create(left); plain(c_go, C_PANEL);
        lv_obj_set_size(c_go, 200, 200); lv_obj_set_style_radius(c_go, 100, 0);
        lv_obj_add_flag(c_go, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(c_go, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(c_go, on_go, LV_EVENT_CLICKED, NULL);
        c_go_l = label(c_go, &lv_font_montserrat_40, C_TEXT, LV_SYMBOL_PLAY); lv_obj_center(c_go_l);
        c_bar = lv_bar_create(left); lv_obj_set_size(c_bar, 280, 14); lv_bar_set_range(c_bar, 0, 100);
        lv_obj_set_style_bg_color(c_bar, lv_color_hex(C_TEAL), LV_PART_INDICATOR);
        c_st = label(left, f24, C_DIM, "");
        lv_obj_set_width(c_st, 340); lv_label_set_long_mode(c_st, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(c_st, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *hint = label(left, f18, C_LINE, "pon el Tab5 entre los dos · 放在两人中间");
        lv_obj_set_width(hint, 340); lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        c_list = lv_obj_create(body); plain(c_list, C_BG);
        lv_obj_set_size(c_list, 1280 - 32 - 380, LV_PCT(100));
        lv_obj_align(c_list, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_flex_flow(c_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(c_list, 10, 0);
        lv_obj_set_scroll_dir(c_list, LV_DIR_VER);
        set_pair(0);
        lv_timer_create(tick, 150, NULL);
    }
    open_scr(s_scr);
}

bool conv_key(int enter)
{
    if (!s_scr || lv_screen_active() != s_scr) return false;
    if (enter) toggle();
    return true;
}

void conv_inject(const uint8_t *wav, size_t len)   // console test: feed a WAV as if the VAD had heard it
{
    if (!s_scr) return;
    uint8_t *w = malloc(len);
    if (w) { memcpy(w, wav, len); on_utt(w, len, NULL); }
}
