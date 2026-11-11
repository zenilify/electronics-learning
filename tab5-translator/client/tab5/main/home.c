// PAPP OS — home screen + the non-PTT apps (D-T5-5/D-T5-8, 2026-11-07).
// Every screen is built once (lazily) and kept; network work goes through svc.c, never the LVGL task.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "ui_priv.h"
#include "usbport.h"
#include "remotes.h"
#include "papp.h"
#include "papp_http.h"
#include "battery.h"
#include "sdcard.h"
#include "svc.h"
#include "net.h"
#include "c6ota.h"
#include "settings.h"
#include "content.h"
#include "kbd.h"
#include "audio.h"
#include "camera.h"
#include "notes.h"

static const char *TAG = "home";
static lv_obj_t *s_home, *h_wifi, *h_batt, *h_ep, *h_sd;
static lv_font_t *f64;

// ---------- common frame ----------
static void go_home(lv_event_t *e) { home_show(); }

lv_obj_t *home_screen(void) { return s_home; }
void home_show(void) { cam_stop(); conv_leave(); subs_leave(); line_leave(); audio_note_stop(); lv_screen_load_anim(s_home, LV_SCREEN_LOAD_ANIM_FADE_IN, 200, 0, false); }

lv_obj_t *pill(lv_obj_t *p, const char *txt, uint32_t bg, const lv_font_t *f)
{
    lv_obj_t *b = lv_obj_create(p);
    plain(b, bg);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 52);
    lv_obj_set_style_pad_hor(b, 20, 0);
    lv_obj_set_style_radius(b, 26, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_LINE), LV_STATE_PRESSED);
    lv_obj_t *l = label(b, f, C_TEXT, txt);
    lv_obj_center(l);
    return b;
}

// Screen with a top bar ("‹ Home", title); returns the body container (below the bar).
lv_obj_t *app_frame(lv_obj_t **scr_out, const char *title, uint32_t accent)
{
    lv_obj_t *s = lv_obj_create(NULL);
    plain(s, C_BG);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *top = lv_obj_create(s);
    plain(top, C_BG);
    lv_obj_set_size(top, LV_PCT(100), 72);
    lv_obj_set_style_pad_hor(top, 16, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 18, 0);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *back = pill(top, LV_SYMBOL_LEFT " Home", C_PANEL, f24);
    lv_obj_add_event_cb(back, go_home, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bar = lv_obj_create(top); plain(bar, accent); lv_obj_set_size(bar, 8, 40); lv_obj_set_style_radius(bar, 4, 0);
    label(top, f30, C_TEXT, title);
    lv_obj_t *body = lv_obj_create(s);
    plain(body, C_BG);
    lv_obj_set_size(body, 1280, 720 - 72);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 72);
    lv_obj_set_style_pad_all(body, 16, 0);
    *scr_out = s;
    return body;
}

void open_scr(lv_obj_t *s) { lv_screen_load_anim(s, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 220, 0, false); }

// ---------- full-screen "show this to the person" card ----------
static lv_obj_t *s_show, *show_zh, *show_py, *show_sub, *show_addr;
static void show_close(lv_event_t *e) { lv_obj_add_flag(s_show, LV_OBJ_FLAG_HIDDEN); }
void show_card(const char *zh, const char *py, const char *sub, const char *addr)
{
    if (!s_show) {
        s_show = lv_obj_create(lv_layer_top());
        plain(s_show, 0xFFFFFF);
        lv_obj_set_size(s_show, 1280, 720);
        lv_obj_add_flag(s_show, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_show, show_close, LV_EVENT_CLICKED, NULL);
        lv_obj_set_flex_flow(s_show, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_show, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(s_show, 24, 0);
        lv_obj_set_style_pad_hor(s_show, 48, 0);
        show_zh = label(s_show, f_logo, 0x000000, "");
        show_addr = label(s_show, f40, 0x111111, "");
        show_py = label(s_show, f30, 0x555555, "");
        show_sub = label(s_show, f24, 0x777777, "");
        lv_obj_t *hint = label(s_show, f18, 0xAAAAAA, "toca para cerrar · 点击关闭");
        (void)hint;
        lv_obj_t *arr[] = { show_zh, show_addr, show_py, show_sub };
        for (int i = 0; i < 4; i++) {
            lv_obj_set_width(arr[i], 1180);
            lv_label_set_long_mode(arr[i], LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_align(arr[i], LV_TEXT_ALIGN_CENTER, 0);
        }
    }
    lv_obj_set_style_text_font(show_zh, strlen(zh) > 30 ? f64 : f_logo, 0);
    lv_label_set_text(show_zh, zh);
    lv_label_set_text(show_py, py ? py : "");
    lv_label_set_text(show_sub, sub ? sub : "");
    lv_label_set_text(show_addr, addr ? addr : "");
    lv_obj_remove_flag(s_show, LV_OBJ_FLAG_HIDDEN);
}

// ---------- Phrasebook ----------
// Two sources. Built in: 40 phrases in 7 groups (content.h), always there. SD card: the server's pack
// (papp/phrasebook/index.json — 340 phrases in 16 groups, zh + es audio already on the card), used when present.
#define PHR_MAXCAT 24
#define PHR_SD 10000                      // event user data >= PHR_SD: index into the SD list
typedef struct { const char *zh, *py, *es, *en; uint8_t cat; } sdphr_t;
static struct { const char *id, *es, *en; } s_sdcat[PHR_MAXCAT];
static sdphr_t *s_sdp;
static int s_sdn, s_sdncat;
static char *s_sdjson;                    // index.json in PSRAM; the strings above point into it

// Value of "key" inside the object [o, oe): start and closing quote. The file has no escapes (checked 11-11).
static bool jfield(char *o, char *oe, const char *key, char **v, char **ve)
{
    char pat[20]; snprintf(pat, sizeof pat, "\"%s\"", key);
    for (char *p = o; (p = strstr(p, pat)) && p < oe; p += strlen(pat)) {
        char *q = p + strlen(pat);
        while (*q == ' ') q++;
        if (*q != ':') continue;                               // that was a value, not a key
        q++; while (*q == ' ') q++;
        if (*q != '"') return false;
        char *e = strchr(q + 1, '"');
        if (!e || e > oe) return false;
        *v = q + 1; *ve = e; return true;
    }
    return false;
}

static void phr_sd_load(void)
{
    if (s_sdjson || !sd_ok()) return;
    FILE *f = fopen(SD_ROOT "/phrasebook/index.json", "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *j = n > 100 && n < 2 * 1024 * 1024 ? heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM) : NULL;
    if (j) { n = fread(j, 1, n, f); j[n] = 0; }
    fclose(f);
    sdphr_t *arr = j ? heap_caps_calloc(1000, sizeof *arr, MALLOC_CAP_SPIRAM) : NULL;
    int nc = 0, ne = 0;
    char *p = arr ? strstr(j, "\"categories\"") : NULL, *v[5], *ve[5];
    if (p && (p = strchr(p, '['))) {
        for (char *o = strchr(p, '{'); o && nc < PHR_MAXCAT; ) {
            char *oe = strchr(o, '}'); if (!oe) break;
            char *nx = strchr(oe + 1, '{'), *cl = strchr(oe + 1, ']');      // look ahead before we cut strings
            if (jfield(o, oe, "id", &v[0], &ve[0]) && jfield(o, oe, "es", &v[1], &ve[1]) && jfield(o, oe, "en", &v[2], &ve[2])) {
                *ve[0] = 0; *ve[1] = 0; *ve[2] = 0;
                s_sdcat[nc].id = v[0]; s_sdcat[nc].es = v[1]; s_sdcat[nc].en = v[2]; nc++;
            }
            p = cl ? cl : oe + 1;
            if (!nx || (cl && cl < nx)) break;
            o = nx;
        }
        p = strstr(p, "\"entries\"");
        if (p && (p = strchr(p, '['))) for (char *o = strchr(p, '{'); o && ne < 1000; ) {
            char *oe = strchr(o, '}'); if (!oe) break;
            char *nx = strchr(oe + 1, '{'), *cl = strchr(oe + 1, ']');
            if (jfield(o, oe, "cat", &v[0], &ve[0]) && jfield(o, oe, "zh", &v[1], &ve[1]) && jfield(o, oe, "py", &v[2], &ve[2]) &&
                jfield(o, oe, "es", &v[3], &ve[3]) && jfield(o, oe, "en", &v[4], &ve[4])) {
                for (int k = 0; k < 5; k++) *ve[k] = 0;
                int c = -1; for (int k = 0; k < nc; k++) if (!strcmp(s_sdcat[k].id, v[0])) c = k;
                if (c >= 0) { arr[ne].cat = (uint8_t)c; arr[ne].zh = v[1]; arr[ne].py = v[2]; arr[ne].es = v[3]; arr[ne].en = v[4]; ne++; }
            }
            if (!nx || (cl && cl < nx)) break;
            o = nx;
        }
    }
    if (nc && ne) { s_sdjson = j; s_sdp = arr; s_sdn = ne; s_sdncat = nc; ESP_LOGI(TAG, "phrasebook: %d phrases in %d groups from the SD card", ne, nc); }
    else { free(j); free(arr); ESP_LOGW(TAG, "phrasebook: SD index not usable, built-in list"); }
}

static lv_obj_t *s_phr, *phr_list, *phr_cats[PHR_MAXCAT], *phr_status;
static int phr_ncat(void) { return s_sdn ? s_sdncat : PHRASE_CAT_COUNT; }
static void phr_fill(int cat);
static void on_cat(lv_event_t *e) { phr_fill((int)(intptr_t)lv_event_get_user_data(e)); }
static void on_phrase(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    const char *zh, *py, *es, *en;
    if (i >= PHR_SD) { const sdphr_t *q = &s_sdp[i - PHR_SD]; zh = q->zh; py = q->py; es = q->es; en = q->en; }
    else { const phrase_t *q = &PHRASES[i]; zh = q->zh; py = q->py; es = q->es; en = q->en; }
    char sub[200]; snprintf(sub, sizeof sub, "%s  ·  %s", es, en);
    show_card(zh, py, sub, NULL);
    if (!svc_say(LANG_ZH, zh, NULL, NULL)) ESP_LOGW(TAG, "speaker busy");
}
static void phr_card(const char *zh, const char *py, const char *es, const char *en, int ud)
{
    lv_obj_t *b = lv_obj_create(phr_list);
    plain(b, C_PANEL);
    lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(b, 16, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_LINE), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(b, 4, 0);
    lv_obj_t *z = label(b, f40, C_TEXT, zh);
    lv_obj_set_width(z, LV_PCT(100)); lv_label_set_long_mode(z, LV_LABEL_LONG_WRAP);
    char t[320]; snprintf(t, sizeof t, "%s   ·   %s / %s", py, es, en);
    lv_obj_t *l = label(b, f18, C_DIM, t);
    lv_obj_set_width(l, LV_PCT(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_add_event_cb(b, on_phrase, LV_EVENT_CLICKED, (void *)(intptr_t)ud);
}
static int phr_cur, phr_scroll;           // group on show; scroll position to come back to
static void phr_fill(int cat)
{
    phr_cur = cat;
    for (int i = 0; i < phr_ncat(); i++)
        lv_obj_set_style_bg_color(phr_cats[i], lv_color_hex(i == cat ? C_AMBER : C_PANEL), 0);
    lv_obj_clean(phr_list);
    lv_obj_scroll_to_y(phr_list, 0, LV_ANIM_OFF);
    if (s_sdn) {
        for (int i = 0; i < s_sdn; i++) if (s_sdp[i].cat == cat) phr_card(s_sdp[i].zh, s_sdp[i].py, s_sdp[i].es, s_sdp[i].en, PHR_SD + i);
        return;
    }
    const phrase_cat_t *c = &PHRASE_CATS[cat];
    for (int i = c->first; i < c->first + c->count; i++) phr_card(PHRASES[i].zh, PHRASES[i].py, PHRASES[i].es, PHRASES[i].en, i);
}
// The phrase cards are many tiny LVGL allocations, and malloc() of 64 bytes or less stays in INTERNAL RAM on this build
// (sdkconfig SPIRAM_MALLOC_ALWAYSINTERNAL=64). Measured 11-11 with tools/ram_walk.py: this screen kept 55 kB of the
// ~300 kB that are free after boot - the most of any app, and the screen object lives on for the next visit.
// So the list is emptied once the screen is really off the display (LVGL's "screen unloaded" event, after the fade)
// and filled again when the app is opened: same group, same scroll position. The group buttons on the left stay.
static void on_phr_unloaded(lv_event_t *e) { phr_scroll = lv_obj_get_scroll_y(phr_list); lv_obj_clean(phr_list); }
static void phr_reopen(void)
{
    if (!phr_list || lv_obj_get_child_count(phr_list)) return;   // still filled (e.g. left and re-opened within the fade)
    phr_fill(phr_cur);
    lv_obj_update_layout(phr_list);                              // the scroll range is only known after a layout pass
    lv_obj_scroll_to_y(phr_list, phr_scroll, LV_ANIM_OFF);
}
static const char *s_prefetch_list[PHRASE_COUNT + PLACE_COUNT];
static void prefetch_prog(int i, int n, int ok, void *ud)
{
    char b[96]; snprintf(b, sizeof b, "%s %d/%d%s", LV_SYMBOL_DOWNLOAD, i, n, i == n ? "  ·  listo offline " LV_SYMBOL_OK : "");
    lock(); lv_label_set_text(phr_status, b); unlock();
}
static void on_prefetch(lv_event_t *e)
{
    int n = 0;
    for (int i = 0; i < PHRASE_COUNT; i++) s_prefetch_list[n++] = PHRASES[i].zh;
    for (int i = 0; i < PLACE_COUNT; i++) s_prefetch_list[n++] = PLACES[i].zh;
    if (!sd_ok()) { lv_label_set_text(phr_status, LV_SYMBOL_WARNING " sin tarjeta SD"); return; }
    if (svc_prefetch(LANG_ZH, s_prefetch_list, n, prefetch_prog, NULL)) lv_label_set_text(phr_status, LV_SYMBOL_DOWNLOAD " …");
}
static void build_phrasebook(void)
{
    phr_sd_load();
    lv_obj_t *body = app_frame(&s_phr, "Frases  ·  常用语", C_AMBER);
    lv_obj_t *cats = lv_obj_create(body);
    plain(cats, C_BG);
    lv_obj_set_size(cats, 280, LV_PCT(100));
    lv_obj_set_flex_flow(cats, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cats, 10, 0);
    lv_obj_set_scroll_dir(cats, LV_DIR_VER);                    // 16 groups from the card do not fit: the column scrolls
    for (int i = 0; i < phr_ncat(); i++) {
        char t[64];
        if (s_sdn) snprintf(t, sizeof t, "%s · %s", s_sdcat[i].es, s_sdcat[i].en);
        else snprintf(t, sizeof t, "%s · %s", PHRASE_CATS[i].cat_es, PHRASE_CATS[i].cat_en);
        phr_cats[i] = pill(cats, t, C_PANEL, f18);         // f24 clipped "Transporte · Getting around" on both sides
        lv_obj_set_size(phr_cats[i], LV_PCT(100), 52);
        lv_obj_t *cl = lv_obj_get_child(phr_cats[i], 0);
        lv_obj_set_width(cl, 256); lv_label_set_long_mode(cl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(cl, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(cl);
        lv_obj_add_event_cb(phr_cats[i], on_cat, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (s_sdn) {                                                // everything is on the card already
        char t[96]; snprintf(t, sizeof t, LV_SYMBOL_SD_CARD " %d frases · audio en la SD, sin internet", s_sdn);
        phr_status = label(cats, f18, C_DIM, t);
    } else {
        lv_obj_t *pf = pill(cats, LV_SYMBOL_DOWNLOAD " Offline", C_LINE, f24);
        lv_obj_set_width(pf, LV_PCT(100));
        lv_obj_add_event_cb(pf, on_prefetch, LV_EVENT_CLICKED, NULL);
        phr_status = label(cats, f18, C_DIM, "audio → SD para usar sin internet");
    }
    lv_obj_set_width(phr_status, 270);
    lv_label_set_long_mode(phr_status, LV_LABEL_LONG_WRAP);
    phr_list = lv_obj_create(body);
    plain(phr_list, C_BG);
    lv_obj_set_size(phr_list, 1280 - 32 - 300, LV_PCT(100));
    lv_obj_align(phr_list, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_flex_flow(phr_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(phr_list, 10, 0);
    lv_obj_set_scroll_dir(phr_list, LV_DIR_VER);
    phr_fill(0);
    lv_obj_add_event_cb(s_phr, on_phr_unloaded, LV_EVENT_SCREEN_UNLOADED, NULL);
}

// ---------- Take me to ----------
static lv_obj_t *s_places;
static void on_place(lv_event_t *e)
{
    const place_t *p = &PLACES[(int)(intptr_t)lv_event_get_user_data(e)];
    char sub[200]; snprintf(sub, sizeof sub, "请带我去这里 · Please take me here%s",
                            p->ok ? "" : "\n⚠ confirmar dirección · address not verified");
    show_card(p->zh, p->py, sub, p->addr_zh[0] ? p->addr_zh : NULL);
    char say[160]; snprintf(say, sizeof say, "请带我去%s", p->zh);
    svc_say(LANG_ZH, say, NULL, NULL);
}
static void build_places(void)
{
    lv_obj_t *body = app_frame(&s_places, "Llévame a  ·  请带我去", C_PINK);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(body, 16, 0);
    lv_obj_set_style_pad_column(body, 16, 0);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    for (int i = 0; i < PLACE_COUNT; i++) {
        lv_obj_t *c = lv_obj_create(body);
        plain(c, C_PANEL);
        lv_obj_set_size(c, (1280 - 32 - 16) / 2, 150);
        lv_obj_set_style_radius(c, 18, 0);
        lv_obj_set_style_pad_all(c, 18, 0);
        lv_obj_set_style_border_side(c, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(c, 8, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(PLACES[i].ok ? C_PINK : C_AMBER), 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(C_LINE), LV_STATE_PRESSED);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(c, 6, 0);
        label(c, f40, C_TEXT, PLACES[i].zh);
        label(c, f24, C_DIM, PLACES[i].en);
        lv_obj_add_event_cb(c, on_place, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

// ---------- Type & Translate ----------
static lv_obj_t *s_type, *ty_ta, *ty_out, *ty_src, *ty_st, *ty_dir[2], *ty_kb;
static int ty_lang;                // 0 = ES→中文, 1 = EN→中文
static char *ty_last;
static void ty_dir_set(int d)
{
    ty_lang = d;
    for (int i = 0; i < 2; i++) lv_obj_set_style_bg_color(ty_dir[i], lv_color_hex(i == d ? C_BLUE : C_PANEL), 0);
}
static void on_ty_dir(lv_event_t *e) { ty_dir_set((int)(intptr_t)lv_event_get_user_data(e)); }
static void ty_done(const papp_result_t *r, void *ud)
{
    lock();
    if (r->http_status == 200 && r->tgt_text) {
        free(ty_last); ty_last = strdup(r->tgt_text);
        lv_label_set_text(ty_out, r->tgt_text);
        char b[96]; snprintf(b, sizeof b, LV_SYMBOL_OK " %d ms · %s · toca para mostrar", r->rtt_ms, papp_endpoint_name(r->endpoint));
        lv_label_set_text(ty_st, b);
    } else {
        char b[96]; snprintf(b, sizeof b, LV_SYMBOL_WARNING " error %d · ¿hay red?", r->http_status);
        lv_label_set_text(ty_st, b);
    }
    unlock();
}
static void ty_go(void)
{
    const char *t = lv_textarea_get_text(ty_ta);
    if (!t || !t[0]) return;
    lv_label_set_text(ty_src, t);
    if (svc_text(ty_lang ? LANG_EN : LANG_ES, LANG_ZH, t, true, ty_done, NULL))
        lv_label_set_text(ty_st, LV_SYMBOL_REFRESH " traduciendo · 翻译中 …");
}
static void on_ty_ready(lv_event_t *e) { ty_go(); }
static void on_ty_show(lv_event_t *e) { if (ty_last) show_card(ty_last, NULL, lv_label_get_text(ty_src), NULL); }
static void build_type(void)
{
    lv_obj_t *body = app_frame(&s_type, "Escribe y traduce  ·  打字翻译", C_BLUE);
    lv_obj_t *row = lv_obj_create(body);
    plain(row, C_BG);
    lv_obj_set_size(row, LV_PCT(100), 64);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);
    ty_dir[0] = pill(row, "ES → 中文", C_PANEL, f24);
    ty_dir[1] = pill(row, "EN → 中文", C_PANEL, f24);
    for (int i = 0; i < 2; i++) lv_obj_add_event_cb(ty_dir[i], on_ty_dir, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *go = pill(row, LV_SYMBOL_PLAY " Traducir", C_TEAL, f24);
    lv_obj_add_event_cb(go, on_ty_ready, LV_EVENT_CLICKED, NULL);
    ty_st = label(row, f18, C_DIM, "teclado en pantalla o teclado USB");
    ty_ta = lv_textarea_create(body);
    lv_obj_set_size(ty_ta, 600, 150);
    lv_obj_align(ty_ta, LV_ALIGN_TOP_LEFT, 0, 74);
    lv_obj_set_style_text_font(ty_ta, f30, 0);
    lv_obj_set_style_bg_color(ty_ta, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_text_color(ty_ta, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_border_color(ty_ta, lv_color_hex(C_BLUE), 0);
    lv_textarea_set_placeholder_text(ty_ta, "¿Dónde está la estación del metro?");
    lv_obj_t *out = lv_obj_create(body);
    plain(out, C_PANEL);
    lv_obj_set_size(out, 1280 - 32 - 616, 150);
    lv_obj_align(out, LV_ALIGN_TOP_RIGHT, 0, 74);
    lv_obj_set_style_radius(out, 14, 0);
    lv_obj_set_style_pad_all(out, 12, 0);
    lv_obj_add_flag(out, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(out, on_ty_show, LV_EVENT_CLICKED, NULL);
    ty_out = label(out, f40, C_AMBER, "…");
    lv_obj_set_width(ty_out, LV_PCT(100));
    lv_label_set_long_mode(ty_out, LV_LABEL_LONG_WRAP);
    ty_src = label(out, f18, C_DIM, "");
    lv_obj_align(ty_src, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *kb = ty_kb = lv_keyboard_create(body);
    lv_obj_set_size(kb, LV_PCT(100), 380);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_28, 0);
    lv_keyboard_set_textarea(kb, ty_ta);
    lv_obj_add_event_cb(kb, on_ty_ready, LV_EVENT_READY, NULL);
    ty_dir_set(0);
}

// ---------- Converter ----------
#define FX_N 6
static const char *FX_CODE[FX_N] = { "CNY", "MXN", "USD", "EUR", "CHF", "HKD" };
static double fx_rate[FX_N];      // units per 1 USD; 0 = unknown
static char fx_date[48];
static lv_obj_t *s_conv, *cv_ta, *cv_dd, *cv_out[FX_N], *cv_units, *cv_st;
static void cv_update(void)
{
    double v = atof(lv_textarea_get_text(cv_ta));
    int from = lv_dropdown_get_selected(cv_dd);
    char b[200];                                  // the units line is ~110 bytes ("·" and "°" are 2 bytes each): 96 cut it
    for (int i = 0; i < FX_N; i++) {
        if (fx_rate[from] > 0 && fx_rate[i] > 0) snprintf(b, sizeof b, "%s  %.2f", FX_CODE[i], v / fx_rate[from] * fx_rate[i]);
        else snprintf(b, sizeof b, "%s  —", FX_CODE[i]);
        lv_label_set_text(cv_out[i], b);
    }
    snprintf(b, sizeof b, "%.1f km = %.1f mi   ·   %.1f kg = %.1f lb   ·   %.1f °C = %.1f °F   ·   %.1f cm = %.1f in",
             v, v * 0.621371, v, v * 2.20462, v, v * 9 / 5 + 32, v, v / 2.54);
    lv_label_set_text(cv_units, b);
}
static void on_cv(lv_event_t *e) { cv_update(); }
static double jnum(const char *j, const char *k)
{
    char pat[16]; snprintf(pat, sizeof pat, "\"%s\"", k);
    const char *p = strstr(j, pat); if (!p) return 0;
    p = strchr(p + strlen(pat), ':'); return p ? atof(p + 1) : 0;
}
static void fx_done(const papp_result_t *r, void *ud)
{
    if (r->http_status != 200 || !r->tgt_text) {
        lock(); lv_label_set_text(cv_st, LV_SYMBOL_WARNING " sin tipos de cambio (¿red?) · unidades sí funcionan"); unlock();
        return;
    }
    for (int i = 0; i < FX_N; i++) fx_rate[i] = jnum(r->tgt_text, FX_CODE[i]);
    // rates are "units per 1 USD": the base itself has no usable entry in the reply, so USD showed "—" (seen 11-11)
    bool any = false; for (int i = 0; i < FX_N; i++) any |= fx_rate[i] > 0;
    for (int i = 0; i < FX_N; i++) if (any && !strcmp(FX_CODE[i], "USD") && fx_rate[i] <= 0) fx_rate[i] = 1.0;
    const char *d = strstr(r->tgt_text, "\"date\"");
    fx_date[0] = 0;
    if (d && (d = strchr(d + 6, '"'))) { snprintf(fx_date, sizeof fx_date, "%.*s", (int)strcspn(d + 1, "\""), d + 1); }
    sd_log("fx.json", r->tgt_text);
    lock();
    char b[96]; snprintf(b, sizeof b, LV_SYMBOL_OK " tipos de cambio: %s", fx_date);
    lv_label_set_text(cv_st, b);
    cv_update();
    unlock();
}
static void build_conv(void)
{
    lv_obj_t *body = app_frame(&s_conv, "Convertidor  ·  换算", C_GREEN);
    cv_ta = lv_textarea_create(body);
    lv_textarea_set_one_line(cv_ta, true);
    lv_textarea_set_accepted_chars(cv_ta, "0123456789.");
    lv_textarea_set_text(cv_ta, "100");
    lv_obj_set_size(cv_ta, 300, 70);
    lv_obj_set_style_text_font(cv_ta, f40, 0);
    lv_obj_set_style_bg_color(cv_ta, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_text_color(cv_ta, lv_color_hex(C_TEXT), 0);
    lv_obj_add_event_cb(cv_ta, on_cv, LV_EVENT_VALUE_CHANGED, NULL);
    cv_dd = lv_dropdown_create(body);
    lv_dropdown_set_options(cv_dd, "CNY\nMXN\nUSD\nEUR\nCHF\nHKD");
    lv_obj_set_size(cv_dd, 160, 70);
    lv_obj_align(cv_dd, LV_ALIGN_TOP_LEFT, 316, 0);
    lv_obj_set_style_text_font(cv_dd, &lv_font_montserrat_28, 0);
    lv_obj_add_event_cb(cv_dd, on_cv, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *grid = lv_obj_create(body);
    plain(grid, C_BG);
    lv_obj_set_size(grid, 700, 300);
    lv_obj_align(grid, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 10, 0); lv_obj_set_style_pad_column(grid, 10, 0);
    for (int i = 0; i < FX_N; i++) {
        lv_obj_t *c = lv_obj_create(grid);
        plain(c, C_PANEL);
        lv_obj_set_size(c, 340, 86);
        lv_obj_set_style_radius(c, 14, 0);
        cv_out[i] = label(c, f40, i < 2 ? C_GREEN : C_TEXT, "—");
        lv_obj_center(cv_out[i]);
    }
    cv_st = label(body, f18, C_DIM, LV_SYMBOL_REFRESH " pidiendo tipos de cambio …");
    lv_obj_align(cv_st, LV_ALIGN_TOP_LEFT, 0, 90);
    cv_units = label(body, f24, C_DIM, "");
    lv_obj_set_width(cv_units, 1240);
    lv_obj_align(cv_units, LV_ALIGN_TOP_LEFT, 0, 316);
    lv_obj_t *kb = lv_keyboard_create(body);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(kb, 600, 260);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_28, 0);
    lv_keyboard_set_textarea(kb, cv_ta);
    cv_update();
}

// ---------- Settings / about ----------
static lv_obj_t *s_set, *set_info, *set_nets, *set_sync;
static unsigned set_gen = ~0u;
static char set_cur[33];
static int set_prefv = -2;
static void on_net_pick(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot == -2) { net_request_scan(); return; }
    net_join(slot);
    set_gen = ~0u;            // force a redraw (pref changed)
}
static void nets_redraw(void)
{
    net_known_t k[NET_SLOTS]; unsigned gen;
    int n = net_known(k, NET_SLOTS, &gen);
    int pref = net_pref();
    if (gen == set_gen && pref == set_prefv && !strcmp(set_cur, (char *)g_status.ssid)) return;
    set_gen = gen; set_prefv = pref; strlcpy(set_cur, (char *)g_status.ssid, sizeof set_cur);
    lv_obj_clean(set_nets);
    lv_obj_t *t = label(set_nets, f24, C_DIM, LV_SYMBOL_WIFI " Red:");
    (void)t;
    lv_obj_t *au = pill(set_nets, "Auto", pref < 0 ? C_LINE : C_PANEL, f24);
    if (pref < 0) { lv_obj_set_style_border_width(au, 3, 0); lv_obj_set_style_border_color(au, lv_color_hex(C_AMBER), 0); }
    lv_obj_add_event_cb(au, on_net_pick, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    for (int i = 0; i < n; i++) {
        char b[64];
        if (k[i].rssi > -999) snprintf(b, sizeof b, "%s%s  %d", k[i].current ? LV_SYMBOL_OK " " : "", k[i].ssid, k[i].rssi);
        else snprintf(b, sizeof b, "%s  —", k[i].ssid);
        lv_obj_t *p = pill(set_nets, b, k[i].current ? 0x0F5F57 : C_PANEL, f24);
        if (k[i].rssi <= -999) lv_obj_set_style_text_color(lv_obj_get_child(p, 0), lv_color_hex(C_DIM), 0);
        if (k[i].slot == pref) { lv_obj_set_style_border_width(p, 3, 0); lv_obj_set_style_border_color(p, lv_color_hex(C_AMBER), 0); }
        lv_obj_add_event_cb(p, on_net_pick, LV_EVENT_CLICKED, (void *)(intptr_t)k[i].slot);
    }
    lv_obj_t *sc = pill(set_nets, LV_SYMBOL_REFRESH " Buscar redes", C_PANEL, f24);
    lv_obj_add_event_cb(sc, on_net_pick, LV_EVENT_CLICKED, (void *)(intptr_t)-2);
}
static void set_refresh(lv_timer_t *t)
{
    if (lv_screen_active() != s_set) return;
    uint32_t tot, fr; sd_info(&tot, &fr);
    uint32_t a = g_status.c6v[0], b = g_status.c6v[1], c = g_status.c6v[2]; bool c6 = g_status.c6v_ok;
    char s[800], ub[200];
    usbport_describe(ub, sizeof ub, true);
    float amps = g_status.batt_a > -0.005f && g_status.batt_a < 0.005f ? 0.0f : g_status.batt_a;   // no "-0.00"
    // six lines: the Wi-Fi row starts right below (a seventh line is cut off — seen on the 11-11 screenshot)
    snprintf(s, sizeof s,
             "PAPP OS v%s  ·  ESP32-P4 rev1.3 + ESP32-C6 (esp-hosted %s%lu.%lu.%lu)\n"
             "Wi-Fi: %s  %s  %d dBm  ·  IP %s\n"
             "Servidor: %s  ·  %d ms\n"
             "Batería: %.2f V  %.2f A  ·  %s\n"
             "Tarjeta SD: %s  %s  ·  %lu / %lu GB libres  ·  RAM %u kB  ·  PSRAM %u MB\n"
             "USB-A: %s",
             PAPP_VERSION, c6 ? "" : "?", (unsigned long)a, (unsigned long)b, (unsigned long)c,
             g_status.wifi_up ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE, g_status.ssid, g_status.rssi, g_status.ip,
             papp_endpoint_name(g_status.endpoint), g_status.health_ms,
             g_status.batt_v, amps,
             amps > 0.05f ? LV_SYMBOL_CHARGE " cargando" : amps < -0.05f ? "usando la batería" : usbport_charge_is_on() ? "con corriente externa, sin cargar" : "cargador apagado",
             sd_ok() ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE, sd_card_name(), (unsigned long)(fr / 1024), (unsigned long)(tot / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / (1024 * 1024)),
             ub);
    lv_label_set_text(set_info, s);
    nets_redraw();
}
static void on_bright(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    bsp_display_brightness_set(v);
    settings_set_u8("bri", (uint8_t)v);
}
static void on_reprobe(lv_event_t *e) { net_reprobe(); }
static void tz_label(lv_obj_t *p) { char b[64]; snprintf(b, sizeof b, LV_SYMBOL_GPS " Hora: %s", TZ_NAME[settings_get_u8("tz", 0) ? 1 : 0]);
                                    lv_label_set_text(lv_obj_get_child(p, 0), b); }
static void voice_label(lv_obj_t *p) { lv_label_set_text(lv_obj_get_child(p, 0), settings_get_u8("voice", 0) ? LV_SYMBOL_AUDIO " Voz: Erick" : LV_SYMBOL_AUDIO " Voz: PAPP"); }
static void on_voice(lv_event_t *e)       // es/en phrases in Erick's cloned voice (SD papp/voices/erick/, N4 pack)
{
    settings_set_u8("voice", settings_get_u8("voice", 0) ? 0 : 1);
    voice_label(lv_event_get_current_target(e));
}
static void on_tz(lv_event_t *e)          // México <-> China: clocks, file names and logs follow at once
{
    settings_set_u8("tz", settings_get_u8("tz", 0) ? 0 : 1);
    papp_tz_apply();
    tz_label(lv_event_get_current_target(e));
}
static void sync_prog(int i, int n, int ok, void *ud)
{
    char b[96];
    if (n == 0) snprintf(b, sizeof b, LV_SYMBOL_WARNING " sin servidor / sin paquete");
    else if (i < n) snprintf(b, sizeof b, LV_SYMBOL_DOWNLOAD " %d / %d", i, n);
    else snprintf(b, sizeof b, "%s %d archivos en la SD", ok ? LV_SYMBOL_OK : LV_SYMBOL_WARNING, n);
    lock(); lv_label_set_text(set_sync, b); unlock();
}
static void on_sync(lv_event_t *e)
{
    if (!sd_ok()) { lv_label_set_text(set_sync, LV_SYMBOL_WARNING " sin tarjeta SD"); return; }
    if (svc_sd_sync(sync_prog, NULL)) lv_label_set_text(set_sync, LV_SYMBOL_DOWNLOAD " …");
    else lv_label_set_text(set_sync, "ocupado, intenta de nuevo");
}
static void build_settings(void)
{
    lv_obj_t *body = app_frame(&s_set, "Ajustes  ·  设置", C_DIM);
    set_info = label(body, f24, C_TEXT, "…");
    lv_obj_set_style_text_line_space(set_info, 4, 0);
    lv_obj_set_width(set_info, 1240); lv_label_set_long_mode(set_info, LV_LABEL_LONG_DOT);
    set_nets = lv_obj_create(body);
    plain(set_nets, C_BG);
    lv_obj_set_size(set_nets, LV_PCT(100), 130);
    lv_obj_align(set_nets, LV_ALIGN_TOP_LEFT, 0, 262);
    lv_obj_set_flex_flow(set_nets, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(set_nets, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(set_nets, 10, 0); lv_obj_set_style_pad_column(set_nets, 10, 0);
    lv_obj_t *row = lv_obj_create(body);
    plain(row, C_BG);
    lv_obj_set_size(row, LV_PCT(100), 70);
    lv_obj_align(row, LV_ALIGN_BOTTOM_LEFT, 0, -120);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_t *r = pill(row, LV_SYMBOL_LOOP " Girar pantalla 180°", C_PANEL, f24);
    lv_obj_add_event_cb(r, on_logo_long, LV_EVENT_CLICKED, NULL);
    lv_obj_t *p = pill(row, LV_SYMBOL_REFRESH " Buscar servidor", C_PANEL, f24);
    lv_obj_add_event_cb(p, on_reprobe, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sy = pill(row, LV_SYMBOL_DOWNLOAD " Paquete de frases → SD", C_PANEL, f24);
    lv_obj_add_event_cb(sy, on_sync, LV_EVENT_CLICKED, NULL);
    set_sync = label(row, f18, C_DIM, "");
    label(body, f24, C_DIM, LV_SYMBOL_IMAGE " Brillo");
    lv_obj_t *lb = lv_obj_get_child(body, -1);
    lv_obj_align(lb, LV_ALIGN_BOTTOM_LEFT, 0, -40);
    lv_obj_t *sl = lv_slider_create(body);
    lv_slider_set_range(sl, 10, 100);
    lv_slider_set_value(sl, settings_get_u8("bri", 85), LV_ANIM_OFF);
    lv_obj_set_width(sl, 520);
    lv_obj_align(sl, LV_ALIGN_BOTTOM_LEFT, 160, -46);
    lv_obj_add_event_cb(sl, on_bright, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *tz = pill(body, "", C_PANEL, f24);
    lv_obj_align(tz, LV_ALIGN_BOTTOM_RIGHT, 0, -30);
    tz_label(tz);
    lv_obj_add_event_cb(tz, on_tz, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vo = pill(body, "", C_PANEL, f24);
    lv_obj_align(vo, LV_ALIGN_BOTTOM_RIGHT, -350, -30);
    voice_label(vo);
    lv_obj_add_event_cb(vo, on_voice, LV_EVENT_CLICKED, NULL);
    lv_timer_create(set_refresh, 1000, NULL);
}

// ---------- Controles: remotes status (remotes.c) ----------
static lv_obj_t *s_rem, *rem_l;
static void rem_tick(lv_timer_t *t)
{
    if (lv_screen_active() != s_rem) return;
    char b[400]; int n = remotes_list(b, sizeof b);
    lv_label_set_text(rem_l, n ? b : "Ningún control visto todavía.\nEnciende el StickS3 o el Dial en la misma red Wi-Fi.\n"
                                      "No remote seen yet — switch on the StickS3 or the Dial on the same Wi-Fi.");
}
static void build_remotes(void)
{
    lv_obj_t *body = app_frame(&s_rem, "Controles  ·  遥控", C_BLUE);
    lv_obj_t *h = label(body, f24, C_DIM, "StickS3: KEY1 = hablar (tú) · KEY2 = hablar (la otra persona) / par\n"
                                           "Dial: girar = idioma · presionar = hablar · toque largo + girar = volumen");
    lv_obj_set_width(h, 1180); lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    rem_l = label(body, f30, C_TEXT, "");
    lv_obj_align(rem_l, LV_ALIGN_TOP_LEFT, 0, 110);
    lv_obj_set_width(rem_l, 1180); lv_label_set_long_mode(rem_l, LV_LABEL_LONG_WRAP);
    lv_timer_create(rem_tick, 1000, NULL);
}

// ---------- "next" placeholder apps ----------
typedef struct { const char *title, *what; uint32_t col; lv_obj_t *scr; } soon_t;
static soon_t SOON[] = {
    { "Señala y traduce  ·  拍照翻译", "Cámara MIPI-CSI → OCR → traducción sobre la imagen.\nCamera → OCR → translated overlay.", C_VIOLET, NULL },
    { "Notas de voz  ·  语音笔记", "Graba → transcripción + resumen → tarjeta SD y Obsidian.\nRecord → transcript + summary → SD card.", C_VIOLET, NULL },
    { "Conversación  ·  对话", "Sin botones: detecta quién habla y en qué idioma.\nHands-free: voice activity + language ID.", C_TEAL, NULL },
    { "Subtítulos  ·  实时字幕", "Subtítulos en vivo, letra grande, para charlas y reuniones.\nLive captions for talks and meetings.", C_TEAL, NULL },
    { "Controles  ·  遥控", "M5Dial (perilla = idioma/volumen) y StickS3 (botón para hablar) por ESP-NOW.", C_BLUE, NULL },
    { "RS-485 / Modbus", "Puerto Ext 2: Modbus RTU con un StampS3 como esclavo — el Tab5 como HMI industrial.", C_AMBER, NULL },
    { "Casa  ·  智能家居", "Panel de Home Assistant: luces, plano de la casa, seguridad.", C_GREEN, NULL },
};
#define SOON_N (int)(sizeof SOON / sizeof SOON[0])
static void open_soon(int i)
{
    if (!SOON[i].scr) {
        lv_obj_t *body = app_frame(&SOON[i].scr, SOON[i].title, SOON[i].col);
        lv_obj_t *l = label(body, f30, C_TEXT, SOON[i].what);
        lv_obj_set_width(l, 1100);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_center(l);
        lv_obj_t *b = label(body, f24, SOON[i].col, "próximamente · coming next · 即将推出");
        lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -40);
    }
    open_scr(SOON[i].scr);
}

// ---------- home ----------
typedef enum { A_TRANSLATE, A_TYPE, A_PHRASE, A_PLACES, A_CONV, A_SETTINGS, A_CAM, A_NOTES, A_DIALOG, A_SUBS, A_LINE, A_REMOTES, A_SOON0 } app_id_t;
typedef struct { const char *icon, *name, *sub; uint32_t col; int id; } tile_t;
static const tile_t TILES[] = {
    { LV_SYMBOL_AUDIO,     "Traductor",   "按住说话 · hold to talk",  C_TEAL,   A_TRANSLATE },
    { LV_SYMBOL_KEYBOARD,  "Escribe",     "打字翻译 · type",          C_BLUE,   A_TYPE },
    { LV_SYMBOL_LIST,      "Frases",      "常用语 · phrasebook",      C_AMBER,  A_PHRASE },
    { LV_SYMBOL_GPS,       "Llévame a",   "请带我去 · take me to",    C_PINK,   A_PLACES },
    { LV_SYMBOL_SHUFFLE,   "Convertir",   "换算 · ¥ $ km °C",        C_GREEN,  A_CONV },
    { LV_SYMBOL_IMAGE,     "Cámara",      "拍照翻译 · point",          C_VIOLET, A_CAM },
    { LV_SYMBOL_EDIT,      "Notas",       "语音笔记 · voice notes",   C_VIOLET, A_NOTES },
    { LV_SYMBOL_LOOP,      "Diálogo",     "对话 · hands-free",        C_TEAL,   A_DIALOG },
    { LV_SYMBOL_EYE_OPEN,  "Subtítulos",  "实时字幕 · captions",       C_TEAL,   A_SUBS },
    { LV_SYMBOL_BLUETOOTH, "Controles",   "Dial · StickS3",          C_BLUE,   A_REMOTES },
    { LV_SYMBOL_CHARGE,    "Línea",       "产线 · OEE / Andon",       C_AMBER,  A_LINE },
    { LV_SYMBOL_HOME,      "Casa",        "Home Assistant",          C_GREEN,  A_SOON0 + 6 },
    { LV_SYMBOL_SETTINGS,  "Ajustes",     "设置 · settings",          C_DIM,    A_SETTINGS },
};
#define TILE_N (int)(sizeof TILES / sizeof TILES[0])

static void open_app(int id)
{
    switch (id) {
    case A_TRANSLATE: open_scr(ui_translator_screen()); break;
    case A_TYPE:      if (!s_type) build_type(); open_scr(s_type); break;
    case A_PHRASE:    if (!s_phr) build_phrasebook(); else phr_reopen(); open_scr(s_phr); break;
    case A_PLACES:    if (!s_places) build_places(); open_scr(s_places); break;
    case A_CONV:
        if (!s_conv) build_conv();
        open_scr(s_conv);
        if (fx_rate[0] == 0) svc_json("/fx", fx_done, NULL);
        break;
    case A_CAM:       cam_app_open(); break;
    case A_NOTES:     notes_app_open(); break;
    case A_DIALOG:    conv_app_open(); break;
    case A_SUBS:      subs_app_open(); break;
    case A_LINE:      line_app_open(); break;
    case A_REMOTES:   if (!s_rem) build_remotes(); open_scr(s_rem); rem_tick(NULL); break;
    case A_SETTINGS:  if (!s_set) build_settings(); open_scr(s_set); net_request_scan(); set_refresh(NULL); break;
    default:          open_soon(id - A_SOON0); break;
    }
}
static void on_tile(lv_event_t *e) { open_app((int)(intptr_t)lv_event_get_user_data(e)); }

static void home_tick(lv_timer_t *t)
{
    if (lv_screen_active() != s_home) return;
    char b[96];
    if (g_status.wifi_up) snprintf(b, sizeof b, LV_SYMBOL_WIFI " %s %d", g_status.ssid, g_status.rssi);
    else snprintf(b, sizeof b, LV_SYMBOL_WIFI " —");
    lv_label_set_text(h_wifi, b);
    if (g_status.endpoint >= 0) snprintf(b, sizeof b, LV_SYMBOL_OK " %s", papp_endpoint_name(g_status.endpoint));
    else snprintf(b, sizeof b, LV_SYMBOL_CLOSE " offline");
    lv_label_set_text(h_ep, b);
    lv_obj_set_style_text_color(h_ep, lv_color_hex(g_status.endpoint >= 0 ? C_GREEN : C_RED), 0);
    lv_label_set_text(h_sd, sd_ok() ? LV_SYMBOL_SD_CARD " SD" : LV_SYMBOL_SD_CARD " —");
    int pct = battery_percent(g_status.batt_v);
    if (pct < 0) snprintf(b, sizeof b, LV_SYMBOL_USB);
    else snprintf(b, sizeof b, LV_SYMBOL_BATTERY_FULL " %d%%%s", pct, g_status.batt_a > 0.05f ? " " LV_SYMBOL_CHARGE : "");
    lv_label_set_text(h_batt, b);
}

void home_init(void)
{
    f64 = mkfont(64);
    s_home = lv_obj_create(NULL);
    plain(s_home, C_BG);
    lv_obj_remove_flag(s_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *top = lv_obj_create(s_home);
    plain(top, C_BG);
    lv_obj_set_size(top, LV_PCT(100), 72);
    lv_obj_set_style_pad_hor(top, 24, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 22, 0);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *logo = label(top, f40, C_TEXT, "PAPP");
    lv_obj_set_style_text_letter_space(logo, 8, 0);
    label(top, f24, C_DIM, "OS · M5Stack Tab5");
    lv_obj_t *sp = lv_obj_create(top); lv_obj_remove_style_all(sp); lv_obj_set_flex_grow(sp, 1);
    h_ep = label(top, f18, C_DIM, "…");
    h_wifi = label(top, f18, C_TEXT, LV_SYMBOL_WIFI);
    h_sd = label(top, f18, C_TEXT, LV_SYMBOL_SD_CARD);
    h_batt = label(top, f18, C_TEXT, "");

    lv_obj_t *grid = lv_obj_create(s_home);
    plain(grid, C_BG);
    lv_obj_set_size(grid, 1280, 720 - 72);
    lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0, 72);
    lv_obj_set_style_pad_all(grid, 20, 0);
    lv_obj_set_style_pad_row(grid, 18, 0);
    lv_obj_set_style_pad_column(grid, 18, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    const int w = (1280 - 40 - 6 * 18) / 7, h = (720 - 72 - 40 - 18) / 2;   // 7 × 2 grid
    for (int i = 0; i < TILE_N; i++) {
        lv_obj_t *t = lv_obj_create(grid);
        plain(t, C_PANEL);
        lv_obj_set_size(t, w, h);
        lv_obj_set_style_radius(t, 22, 0);
        lv_obj_set_style_pad_all(t, 16, 0);
        lv_obj_set_style_bg_color(t, lv_color_hex(C_LINE), LV_STATE_PRESSED);
        lv_obj_set_style_transform_scale(t, 240, LV_STATE_PRESSED);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        // flex column: icon / spacer / name / sub — nothing absolute, so wrapped text can't overlap
        lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(t, 4, 0);
        lv_obj_t *ic = lv_obj_create(t);
        plain(ic, TILES[i].col);
        lv_obj_set_size(ic, 84, 84);
        lv_obj_set_style_radius(ic, 24, 0);
        lv_obj_remove_flag(ic, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *il = label(ic, &lv_font_montserrat_40, 0x0B0F14, TILES[i].icon);
        lv_obj_center(il);
        lv_obj_t *gap = lv_obj_create(t); lv_obj_remove_style_all(gap); lv_obj_set_flex_grow(gap, 1);
        lv_obj_t *n = label(t, f24, C_TEXT, TILES[i].name);
        lv_obj_set_size(n, LV_PCT(100), lv_font_get_line_height(f24));
        lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
        lv_obj_t *s = label(t, f18, C_DIM, TILES[i].sub);
        lv_obj_set_width(s, LV_PCT(100));
        lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
        if (TILES[i].id >= A_SOON0) {
            lv_obj_t *bd = label(t, f18, C_DIM, "next");
            lv_obj_add_flag(bd, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_align(bd, LV_ALIGN_TOP_RIGHT, 0, 0);
        }
        lv_obj_add_event_cb(t, on_tile, LV_EVENT_CLICKED, (void *)(intptr_t)TILES[i].id);
    }
    lv_timer_create(home_tick, 1000, NULL);
}

// ---------- physical keyboard (Keyboard for Tab5, kbd.c) — display lock held ----------
void home_kbd(const char *text, int sp)
{
    lv_obj_t *a = lv_screen_active();
    if (s_show && !lv_obj_has_flag(s_show, LV_OBJ_FLAG_HIDDEN)) { lv_obj_add_flag(s_show, LV_OBJ_FLAG_HIDDEN); return; }
    if (a == s_type) {
        if (ty_kb && !lv_obj_has_flag(ty_kb, LV_OBJ_FLAG_HIDDEN)) {      // real keys → hide the on-screen one
            lv_obj_add_flag(ty_kb, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_height(ty_ta, 420); lv_obj_set_width(ty_ta, 600);
            lv_obj_set_height(lv_obj_get_parent(ty_out), 420);
            lv_obj_set_style_text_font(ty_out, f64, 0);
        }
        if (text) lv_textarea_add_text(ty_ta, text);
        else if (sp == KBD_ENTER) ty_go();
        else if (sp == KBD_BKSP) lv_textarea_delete_char(ty_ta);
        else if (sp == KBD_TAB) ty_dir_set(!ty_lang);
        else if (sp == KBD_ESC) { if (lv_textarea_get_text(ty_ta)[0]) lv_textarea_set_text(ty_ta, ""); else home_show(); }
        return;
    }
    if (a == s_conv) {
        if (text && (strchr("0123456789.", text[0]) && text[0])) lv_textarea_add_text(cv_ta, text);
        else if (sp == KBD_BKSP) lv_textarea_delete_char(cv_ta);
        else if (sp == KBD_ESC) home_show();
        else if (sp == KBD_TAB) { lv_dropdown_set_selected(cv_dd, (lv_dropdown_get_selected(cv_dd) + 1) % FX_N); cv_update(); }
        return;
    }
    bool ent = sp == KBD_ENTER || (text && text[0] == ' ');
    if (text && text[0] == 'b' && cam_key(0)) { cam_toggle_source(); return; }   // camera screen: b = front/back
    if (cam_key(ent) || notes_key(ent, text) || conv_key(ent) || subs_key(ent, text)) { if (sp == KBD_ESC) home_show(); return; }
    if (a == s_home && text) {          // shortcuts: t e f l c a p
        static const struct { char k; int id; } K[] = { {'t', A_TRANSLATE}, {'e', A_TYPE}, {'f', A_PHRASE},
                                                        {'l', A_PLACES}, {'c', A_CONV}, {'a', A_SETTINGS}, {'p', A_CAM}, {'n', A_NOTES}, {'d', A_DIALOG}, {'s', A_SUBS}, {'o', A_LINE}, {'r', A_REMOTES} };
        for (int i = 0; i < (int)(sizeof K / sizeof K[0]); i++)
            if (text[0] == K[i].k) open_app(K[i].id);
        return;
    }
    if (sp == KBD_ESC && a != s_home) home_show();
}
