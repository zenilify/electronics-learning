// "Línea · 产线" — bilingual OEE / Andon board for the Traceability Cell (T5-I satellites, N4 server contract
// claude/handoffs/2026-11-08-n4.md §3, AGREEd by T5 11-09). Polls GET /line/kpi?window=shift every 3 s while the
// screen is up. No demo numbers: until the server has /line the board says so.
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

static const char *TAG = "line";
static lv_obj_t *s_scr, *k_val[4], *k_arc[4], *n_cnt, *n_andon, *n_reasons, *n_st;
static TaskHandle_t s_task;
static volatile bool s_up;

static double jnum(const char *j, const char *k)
{
    char pat[32]; snprintf(pat, sizeof pat, "\"%s\"", k);
    const char *p = strstr(j, pat); if (!p) return -1;
    p = strchr(p + strlen(pat), ':'); return p ? atof(p + 1) : -1;
}
static const char *arr(const char *j, const char *k, const char **end)   // "k": [ ... ]  -> start, *end = ']'
{
    char pat[32]; snprintf(pat, sizeof pat, "\"%s\"", k);
    const char *p = strstr(j, pat); if (!p) return NULL;
    p = strchr(p, '['); if (!p) return NULL;
    int d = 0; const char *e = p;
    for (; *e; e++) { if (*e == '[') d++; else if (*e == ']' && --d == 0) break; }
    *end = e; return p + 1;
}
static uint32_t pct_col(double v) { return v >= 0.85 ? C_GREEN : v >= 0.6 ? C_AMBER : C_RED; }

static void show(const char *j, int st)                     // lock held
{
    if (st != 200 || !j) {
        char b[160];
        snprintf(b, sizeof b, st == 404 ? LV_SYMBOL_WARNING " el servidor aún no tiene /line (N4) · 服务器尚未开启产线数据"
                                        : LV_SYMBOL_WARNING " sin datos de la línea (HTTP %d) · 无数据", st);
        lv_label_set_text(n_st, b);
        return;
    }
    const char *K[4] = { "oee", "availability", "performance", "quality" };
    for (int i = 0; i < 4; i++) {
        double v = jnum(j, K[i]); if (v < 0) v = 0; if (v > 1.5) v /= 100;      // 0..1 (accept 0..100)
        char b[16]; snprintf(b, sizeof b, "%.0f%%", v * 100);
        lv_label_set_text(k_val[i], b);
        lv_arc_set_value(k_arc[i], (int)(v * 100));
        lv_obj_set_style_arc_color(k_arc[i], lv_color_hex(pct_col(v)), LV_PART_INDICATOR);
    }
    char b[600];
    snprintf(b, sizeof b, "Total %.0f   ·   " LV_SYMBOL_OK " buenas 良品 %.0f   ·   " LV_SYMBOL_CLOSE " scrap 废品 %.0f",
             jnum(j, "total"), jnum(j, "good"), jnum(j, "scrap"));
    lv_label_set_text(n_cnt, b);
    // andon_open: list of objects {station, reason/text…} or plain strings
    const char *e, *a = arr(j, "andon_open", &e);
    int n = 0; char *w = b; *w = 0;
    for (const char *p = a; a && p && p < e && n < 4; n++) {
        const char *ob = strchr(p, '{'); if (!ob || ob > e) break;
        char *stn = json_str(ob, "station"), *rs = json_str(ob, "es"), *rz = json_str(ob, "zh"), *rr = json_str(ob, "reason");
        w += snprintf(w, b + sizeof b - w, LV_SYMBOL_BELL " %s  %s %s\n", stn ? stn : "?", rs ? rs : rr ? rr : "", rz ? rz : "");
        free(stn); free(rs); free(rz); free(rr);
        p = strchr(ob, '}'); if (p) p++;
    }
    lv_label_set_text(n_andon, n ? b : LV_SYMBOL_OK " sin andon abierto · 无报警");
    lv_obj_set_style_text_color(n_andon, lv_color_hex(n ? C_RED : C_GREEN), 0);
    // top_reasons[{code, es, zh, en, count}]
    a = arr(j, "top_reasons", &e); n = 0; w = b; *w = 0;
    for (const char *p = a; a && p && (p = strstr(p, "\"code\"")) && p < e && n < 5; n++) {
        char *c = json_str(p, "code"), *es = json_str(p, "es"), *zh = json_str(p, "zh");
        const char *ob = strchr(p, '}');
        char cnt[8] = "?"; { const char *q = strstr(p, "\"count\""); if (q && (!ob || q < ob)) snprintf(cnt, sizeof cnt, "%d", atoi(strchr(q, ':') + 1)); }
        w += snprintf(w, b + sizeof b - w, "%s  ×%s   %s · %s\n", c ? c : "", cnt, es ? es : "", zh ? zh : "");
        free(c); free(es); free(zh);
        p = ob ? ob : p + 6;
    }
    lv_label_set_text(n_reasons, n ? b : "—");
    lv_label_set_text(n_st, LV_SYMBOL_REFRESH " en vivo · 实时 · /line/kpi");
}

static void poll_task(void *x)
{
    while (s_up) {
        uint8_t *body = NULL; size_t len = 0;
        int st = papp_get("/line/kpi?window=shift", &body, &len);
        static int last = -999;
        if (st != last) { ESP_LOGI(TAG, "kpi HTTP %d (%u B)", st, (unsigned)len); last = st; }
        lock(); show((const char *)body, st); unlock();
        free(body);
        for (int i = 0; i < 30 && s_up; i++) vTaskDelay(pdMS_TO_TICKS(100));
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

void line_leave(void) { s_up = false; }

static lv_obj_t *card(lv_obj_t *p, const char *es, const char *zh, int i)
{
    lv_obj_t *c = lv_obj_create(p); plain(c, C_PANEL);
    lv_obj_set_size(c, 280, 250); lv_obj_set_style_radius(c, 18, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    k_arc[i] = lv_arc_create(c);
    lv_obj_set_size(k_arc[i], 170, 170); lv_obj_align(k_arc[i], LV_ALIGN_TOP_MID, 0, 6);
    lv_arc_set_range(k_arc[i], 0, 100); lv_arc_set_value(k_arc[i], 0);
    lv_obj_remove_style(k_arc[i], NULL, LV_PART_KNOB); lv_obj_remove_flag(k_arc[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(k_arc[i], 16, LV_PART_MAIN); lv_obj_set_style_arc_width(k_arc[i], 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(k_arc[i], lv_color_hex(C_LINE), LV_PART_MAIN);
    k_val[i] = label(c, f40, C_TEXT, "—"); lv_obj_align(k_val[i], LV_ALIGN_TOP_MID, 0, 66);
    char b[64]; snprintf(b, sizeof b, "%s · %s", es, zh);
    lv_obj_t *l = label(c, f24, i ? C_DIM : C_TEXT, b); lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -4);
    return c;
}

void line_app_open(void)
{
    if (!s_scr) {
        lv_obj_t *body = app_frame(&s_scr, "Línea  ·  产线  ·  OEE / Andon", C_AMBER);
        lv_obj_t *row = lv_obj_create(body); plain(row, C_BG);
        lv_obj_set_size(row, LV_PCT(100), 260);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 18, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        card(row, "OEE", "综合效率", 0); card(row, "Disponib.", "可用率", 1);
        card(row, "Rendim.", "性能", 2); card(row, "Calidad", "质量", 3);
        n_cnt = label(body, f30, C_TEXT, ""); lv_obj_align(n_cnt, LV_ALIGN_TOP_LEFT, 0, 276);
        n_andon = label(body, f30, C_GREEN, ""); lv_obj_align(n_andon, LV_ALIGN_TOP_LEFT, 0, 330);
        lv_obj_set_width(n_andon, 600); lv_label_set_long_mode(n_andon, LV_LABEL_LONG_WRAP);
        lv_obj_t *rt = label(body, f18, C_DIM, "Motivos principales · 主要原因"); lv_obj_align(rt, LV_ALIGN_TOP_LEFT, 640, 330);
        n_reasons = label(body, f24, C_TEXT, "—"); lv_obj_align(n_reasons, LV_ALIGN_TOP_LEFT, 640, 360);
        lv_obj_set_width(n_reasons, 600); lv_label_set_long_mode(n_reasons, LV_LABEL_LONG_WRAP);
        n_st = label(body, f18, C_DIM, "…"); lv_obj_align(n_st, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    open_scr(s_scr);
    if (!s_task) { s_up = true; xTaskCreatePinnedToCore(poll_task, "line", 6144, NULL, 3, &s_task, 0); }
    else s_up = true;
}
