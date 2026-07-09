// ============================================================
// Project 03 — Wio Terminal Hello LCD
// Board: Seeed Wio Terminal (SAMD51)
// Language: C++ / Arduino
//
// What this does:
//   Displays a colorful welcome message on the 320x240 LCD.
//   Introduces the TFT_eSPI library, screen coordinates,
//   text colors, and font sizes.
// ============================================================

#include "TFT_eSPI.h"   // Screen driver, bundled with Seeed's Wio Terminal board package

TFT_eSPI tft;           // Create a single "tft" object representing the screen

void setup() {
  tft.begin();                    // Wake the screen
  tft.setRotation(3);             // 3 = landscape orientation (USB port on the left)
  tft.fillScreen(TFT_BLACK);      // Wipe the whole screen to black

  // ---- Title in big yellow letters ----
  tft.setTextColor(TFT_YELLOW);   // Foreground color
  tft.setTextSize(3);             // 3x scale (default font is 6x8 px)
  tft.setCursor(30, 40);          // (x, y) starting position in pixels
  tft.print("Hello, Erick!");

  // ---- Subtitle in medium white letters ----
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(30, 100);
  tft.print("Wio Terminal is alive.");

  // ---- Small cyan footer ----
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(1);
  tft.setCursor(30, 160);
  tft.print("Your first C++ LCD program.");
}

void loop() {
  // Nothing to do. The screen holds whatever was drawn in setup()
  // until we tell it otherwise.
}
