// ============================================================
// Project 04 — Wio Terminal Input Dashboard
// Board: Seeed Wio Terminal (SAMD51)
// Language: C++ / Arduino
//
// What this does:
//   Live "dashboard" that shows the state of every button and
//   joystick direction on the Wio Terminal. Each row turns
//   green when its input is pressed, gray otherwise.
//
// What you learn:
//   - pinMode() with INPUT_PULLUP (built-in resistor + logic inversion)
//   - digitalRead() — sampling a digital input pin
//   - Writing your own C++ functions with parameters (showButton)
//   - Redrawing text without flicker via setTextColor(fg, bg)
// ============================================================

#include "TFT_eSPI.h"

TFT_eSPI tft;

void setup() {
  // ---- Configure every input pin ----
  // Buttons on the Wio Terminal are wired between the pin and GROUND.
  // INPUT_PULLUP enables an internal resistor that gently pulls the
  // pin UP to 3.3V when nothing is pressed. Pressing the button
  // connects the pin to ground, dragging it DOWN to 0V.
  //
  // So: released = HIGH (3.3V), pressed = LOW (0V). Yes, inverted.
  pinMode(WIO_5S_UP,    INPUT_PULLUP);
  pinMode(WIO_5S_DOWN,  INPUT_PULLUP);
  pinMode(WIO_5S_LEFT,  INPUT_PULLUP);
  pinMode(WIO_5S_RIGHT, INPUT_PULLUP);
  pinMode(WIO_5S_PRESS, INPUT_PULLUP);
  pinMode(WIO_KEY_A,    INPUT_PULLUP);
  pinMode(WIO_KEY_B,    INPUT_PULLUP);
  pinMode(WIO_KEY_C,    INPUT_PULLUP);

  // ---- Set up the screen ----
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(20, 10);
  tft.print("Input Dashboard");
}

// Reusable helper function.
// Draws one row: "LABEL : PRESSED" (green) or "LABEL : ---" (gray).
// The second argument to setTextColor is a background color that
// auto-erases old pixels, so redrawing every loop doesn't flicker.
void showButton(const char* label, int pin, int y) {
  tft.setTextSize(2);
  tft.setCursor(20, y);

  if (digitalRead(pin) == LOW) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.print(label);
    tft.print(": PRESSED  ");
  } else {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.print(label);
    tft.print(": ---      ");
  }
}

void loop() {
  // Poll each input ~30 times per second and redraw the dashboard.
  showButton("UP   ", WIO_5S_UP,    45);
  showButton("DOWN ", WIO_5S_DOWN,  69);
  showButton("LEFT ", WIO_5S_LEFT,  93);
  showButton("RIGHT", WIO_5S_RIGHT, 117);
  showButton("PRESS", WIO_5S_PRESS, 141);
  showButton("KEY A", WIO_KEY_A,    165);
  showButton("KEY B", WIO_KEY_B,    189);
  showButton("KEY C", WIO_KEY_C,    213);

  delay(30);   // ~33 refreshes per second
}
