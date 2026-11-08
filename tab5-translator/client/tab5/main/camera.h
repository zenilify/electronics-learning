#pragma once
// Point & Translate: SC202CS (MIPI-CSI, /dev/video0 via esp_video) -> live preview -> HW JPEG -> POST /ocr-translate.
void cam_app_open(void);   // LVGL context: builds the screen once, starts streaming
void cam_stop(void);       // any context: stop streaming (called when leaving the screen)
#include <stdbool.h>
bool cam_key(int enter);   // true if the camera screen is active (display lock held)
