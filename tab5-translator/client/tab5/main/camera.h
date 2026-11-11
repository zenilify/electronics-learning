#pragma once
// Point & Translate: SC202CS (MIPI-CSI, /dev/video0 via esp_video) -> live preview -> HW JPEG -> POST /ocr-translate.
void cam_app_open(void);   // LVGL context: builds the screen once, starts streaming
void cam_stop(void);       // any context: stop streaming (called when leaving the screen)
#include <stdbool.h>
bool cam_key(int enter);   // true if the camera screen is active (display lock held)
void cam_set_back_url(const char *ip, int port);   // net task: mDNS _pappcam._tcp result ("" = gone)
void cam_toggle_source(void);
int  cam_bad_frames(void);   // back-camera frames dropped because the JPEG was incomplete
void cam_set_back_usb(const char *ip);   // camera address learned over the USB cable or typed on the console ("" = forget)
const char *cam_back_url(void);          // "http://ip:port" in use, or ""
