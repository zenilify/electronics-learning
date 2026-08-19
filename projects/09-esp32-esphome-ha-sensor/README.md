# Project 09 — ESP32 → Home Assistant sensor (ESPHome)

First step toward the big goal: **build sensors that report to Home
Assistant.** This project turns a generic ESP32 DevKit into a Home Assistant
device using [ESPHome](https://esphome.io) — no external components required,
so it's a safe "Hello, World!" you can validate and compile before wiring
anything up.

## What it reports to Home Assistant

- Wi-Fi signal strength (RSSI, dBm)
- Uptime (seconds)
- ESP32 internal die temperature
- IP address (text sensor)

## New concepts vs. earlier Arduino/CircuitPython projects

- **Declarative YAML instead of imperative code.** You describe *what* the
  device has (sensors, Wi-Fi, API); ESPHome generates the C++ firmware for you.
- **`!secret` references.** Credentials live in `secrets.yaml` (git-ignored),
  keeping Wi-Fi passwords and keys out of the repo.
- **Native HA API.** The device auto-discovers in Home Assistant over an
  encrypted connection — no MQTT broker or cloud account needed.

## Steps

1. Create your secrets file (never commit it):
   ```bash
   cp secrets.yaml.example secrets.yaml
   # then edit secrets.yaml with your real Wi-Fi + keys
   ```
2. Validate the config (works with no board attached):
   ```bash
   esphome config 09-esp32-esphome-ha-sensor.yaml
   ```
3. Compile the firmware (downloads the ESP32 toolchain the first time):
   ```bash
   esphome compile 09-esp32-esphome-ha-sensor.yaml
   ```
4. Flash over USB and watch the logs (needs the board plugged in):
   ```bash
   esphome run 09-esp32-esphome-ha-sensor.yaml
   ```
5. In Home Assistant, go to **Settings → Devices & Services**; the ESP32 is
   auto-discovered. Add it (paste the API encryption key if prompted).

## Next ideas

- Swap in a real environmental sensor (BME280 / SHT31 over I2C).
- Add a PIR motion `binary_sensor`.
- Add a capacitive soil-moisture `sensor` for the plant-monitor project.
