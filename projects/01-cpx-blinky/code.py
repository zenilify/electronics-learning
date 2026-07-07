# ============================================================
# Project 01 — CPX Blinky
# Board: Adafruit Circuit Playground Express
# Language: CircuitPython
#
# What this does:
#   Cycles all 10 onboard NeoPixels through red, green, blue
#   in an infinite loop. This is your "Hello, World!" for
#   physical computing.
# ============================================================

import time          # gives us time.sleep() to pause between colors
import board         # exposes the physical pins on the CPX (like board.NEOPIXEL)
import neopixel      # driver library for the WS2812 LEDs on this board

# --- Set up the NeoPixel strip ---
# board.NEOPIXEL  = the pin that controls the 10 onboard LEDs
# 10              = number of pixels on the CPX
# brightness=0.2  = 20% of max brightness (they are BLINDINGLY bright at 1.0)
# auto_write=True = every write updates the strip immediately (default; simpler)
pixels = neopixel.NeoPixel(board.NEOPIXEL, 10, brightness=0.2, auto_write=True)

# --- Main loop ---
# 'while True:' runs forever. Every CircuitPython program that needs to
# keep doing something uses this pattern.
while True:
    pixels.fill((255, 0, 0))   # (R, G, B) — full red, no green, no blue
    time.sleep(0.5)            # wait half a second

    pixels.fill((0, 255, 0))   # full green
    time.sleep(0.5)

    pixels.fill((0, 0, 255))   # full blue
    time.sleep(0.5)
