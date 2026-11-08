#pragma once
// Shared look & feel for every PAPP OS screen (translator in ui.c, the rest in home.c).
#include "lvgl.h"
#include "bsp/m5stack_tab5.h"
#define C_BG     0x0B0F14
#define C_PANEL  0x151C25
#define C_LINE   0x243041
#define C_TEXT   0xE6EDF3
#define C_DIM    0x8B98A5
#define C_TEAL   0x14B8A6
#define C_AMBER  0xF59E0B
#define C_RED    0xEF4444
#define C_GREEN  0x22C55E
#define C_BLUE   0x3B82F6
#define C_VIOLET 0x8B5CF6
#define C_PINK   0xEC4899
extern lv_font_t *f18, *f24, *f30, *f40, *f_logo;   // tiny_ttf CJK subset, built in ui_init
lv_font_t *mkfont(int px);
lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, uint32_t col, const char *t);
void plain(lv_obj_t *o, uint32_t bg);
static inline bool lock(void) { return bsp_display_lock(0); }
static inline void unlock(void) { bsp_display_unlock(); }
lv_obj_t *ui_translator_screen(void);
lv_display_t *ui_display(void);
void on_logo_long(lv_event_t *e);         // rotate 180° + remember
// home.c
void home_init(void);                      // called inside ui_init (display lock held)
lv_obj_t *home_screen(void);
void home_show(void);                      // LVGL context only
lv_obj_t *pill(lv_obj_t *p, const char *txt, uint32_t bg, const lv_font_t *f);
lv_obj_t *app_frame(lv_obj_t **scr_out, const char *title, uint32_t accent);   // returns the body
void open_scr(lv_obj_t *s);
void show_card(const char *zh, const char *py, const char *sub, const char *addr);
