// 08-m5stack-hello.ino
// First sketch on the M5Stack Core (M5GO kit) — board onboarding.
//
// What this teaches, compared to what you already know:
//   - On the Wio Terminal (projects 03-04) you used TFT_eSPI for the screen
//     and raw digitalRead() + INPUT_PULLUP for the buttons.
//   - M5Unified is ONE library that wraps the screen, buttons, speaker,
//     battery and IMU behind a single object called `M5`.
//     Less wiring-level control, much less boilerplate.
//
// Board:   Tools > Board > esp32 > M5Core
// Port:    /dev/cu.usbserial-XXXXXXXX (CH9102 USB-serial bridge)
// Library: M5Unified (installed via Library Manager)

#include <M5Unified.h>   // like #include <TFT_eSPI.h>, but for the whole board

// One counter per button, like the counter idea from project 04.
int countA = 0;
int countB = 0;
int countC = 0;

void drawScreen() {
  // M5.Display is the equivalent of your `tft` object on the Wio.
  M5.Display.fillScreen(TFT_BLACK);

  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_GREEN);
  M5.Display.setCursor(20, 20);
  M5.Display.println("Hola M5Stack!");

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setCursor(20, 70);
  M5.Display.printf("A: %d\n", countA);   // printf works like Serial.printf
  M5.Display.setCursor(20, 100);
  M5.Display.printf("B: %d\n", countB);
  M5.Display.setCursor(20, 130);
  M5.Display.printf("C: %d\n", countC);

  // Battery: something the Wio Terminal couldn't tell you —
  // the M5GO base has a battery and M5Unified can read its level.
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.setCursor(20, 180);
  M5.Display.printf("Bateria: %d%%", M5.Power.getBatteryLevel());
}

void setup() {
  // On the Wio you called tft.begin() + pinMode() for every button.
  // Here ONE call configures screen, buttons, power chip, speaker...
  auto cfg = M5.config();   // default configuration (auto-detects the model)
  M5.begin(cfg);

  drawScreen();
}

void loop() {
  // IMPORTANT: M5.update() polls the buttons once per loop pass.
  // Forget it and Btn*.wasPressed() never fires. (On the Wio you read
  // pins directly, so there was nothing to "update".)
  M5.update();

  // wasPressed() = true exactly ONCE per physical press.
  // That's edge detection — with digitalRead() you'd have to debounce
  // and track previous state yourself.
  bool changed = false;
  if (M5.BtnA.wasPressed()) { countA++; changed = true; }
  if (M5.BtnB.wasPressed()) { countB++; changed = true; }
  if (M5.BtnC.wasPressed()) { countC++; changed = true; }

  // Only redraw when something changed — avoids screen flicker.
  if (changed) { drawScreen(); }

  delay(10);   // small pause; keeps the loop polite, like in project 04
}
