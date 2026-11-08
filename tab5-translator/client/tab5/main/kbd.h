#pragma once
#include <stdint.h>
// M5Stack "Keyboard for Tab5" (STM32F030, I2C 0x6D) on Ext.Port1: SDA G0 / SCL G1 / INT G50 (active low).
// Register map from m5stack/M5Unit-KEYBOARD unit_Tab5Keyboard.hpp (same as home-lab esphome/tab5-banco.yaml):
//   0x10 mode (1 = HID) · 0x00 INT_CFG · 0x02 EVENT_NUM (write 0 = clear) · 0x30 event {modifier, keycode}, empty = FF FF
void kbd_start(void);
// Key codes delivered to the UI: printable UTF-8 as text, specials below.
enum { KBD_ENTER = 1, KBD_BKSP, KBD_ESC, KBD_TAB, KBD_UP, KBD_DOWN, KBD_LEFT, KBD_RIGHT };
// Implemented in home.c (called with the display lock held): either `text` (UTF-8) or `special` != 0.
void home_kbd(const char *text, int special);
