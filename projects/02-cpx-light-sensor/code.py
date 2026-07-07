# ============================================================
# Project 02 — Light-Reactive NeoPixels
# Board: Adafruit Circuit Playground Express
# Language: CircuitPython
#
# What this does:
#   Reads the onboard light sensor (a tiny phototransistor
#   near the "eye" symbol on the board). Prints the reading
#   to the serial console, then colors the NeoPixels based
#   on how bright or dark the environment is.
#
#     Cover the board with your hand -> blue   (dark)
#     Normal room light              -> green  (medium)
#     Shine a phone flashlight       -> red    (bright)
#
# What you learn:
#   The input -> decide -> output loop that runs every
#   automation, sensor, and control system in existence.
# ============================================================

import time
import board
import analogio       # driver for reading analog voltages (light sensor is analog)
import neopixel

# --- Set up OUTPUTS and INPUTS ---
pixels = neopixel.NeoPixel(board.NEOPIXEL, 10, brightness=0.3, auto_write=True)
light = analogio.AnalogIn(board.LIGHT)   # 'board.LIGHT' = the pin the sensor sits on

while True:
    # ---------- INPUT ----------
    reading = light.value            # integer 0 (pitch black) to ~65535 (very bright)
    print("Light reading:", reading) # shows in Mu's Serial console

    # ---------- DECIDE ----------
    if reading < 8000:
        color = (0, 0, 100)      # dim blue -> "it's dark"
    elif reading < 25000:
        color = (0, 100, 0)      # green    -> "normal room light"
    else:
        color = (100, 0, 0)      # red      -> "very bright"

    # ---------- OUTPUT ----------
    pixels.fill(color)

    time.sleep(0.1)              # ~10 readings per second (smooth but not busy)
