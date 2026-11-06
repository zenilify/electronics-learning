# Foundry results

## 2026-11-06 — first full run (Cloud Build `1d5e72d9`, 21/22 OK)

Artifacts + `manifest.json` / `manifest.md`: `gs://papps-c-foundry/2026-11-06/`.
Sources: home-lab `b8159215`, wio-mission-control `f6a25a54`, electronics-learning
`a2b4273e`, Zenilify-Consulting `64188928` (all clean in scope) + untracked `screens.h`
(sha256 `35b7b7fa64d52aea`).

| Target | Board | Result | Size of main image |
|---|---|---|---|
| atom-echo-voice2 | Atom Echo (ESP32) | ✅ | 862 KB factory |
| sensor-recamara | Atom (ESP32) | ✅ | 917 KB |
| clima-recamara | CoreInk (ESP32) | ✅ | 1.03 MB |
| m5core-qr / m5core-qr-probe | Core (ESP32) | ✅ / ✅ | 1.01 MB / — |
| el-09-ha-sensor | ESP32 DevKit | ✅ | 943 KB |
| voz-satelite-2 | AtomS3R + Echo base (ESP32-S3) | ✅ | 2.52 MB |
| voz-piramide-estudio | ESP32-S3 | ✅ | 2.68 MB |
| **stackchan-estudio** | Stack-chan (ESP32-S3) | ❌ | `speaker.i2s_audio_duplex` (stackchan-base.yaml:589): *Invalid configuration for the mixer component. The bits_per_sample Must be string, got NoneType* |
| tab5-banco | Tab5 (ESP32-P4 + C6) | ✅ | 2.57 MB |
| xiao-line-node / -b / -b-diag | XIAO ESP32-C6 | ✅ ✅ ✅ | 1.23 / 1.20 / 1.03 MB |
| sticks3-bridge | StickS3 (ESP-IDF 6.0.3 + microlink) | ✅ | 1.49 MB app |
| wio-mission-control | Wio Terminal (SAMD51) | ✅ | 189 KB |
| el-03 / el-04 | Wio Terminal | ✅ ✅ | 57 KB each |
| el-08-m5stack-hello | Core (esp32 core 3.3.12) | ✅ | 484 KB |
| paper-badge | PaperS3 (PlatformIO) | ✅ | 1.26 MB |
| nuttx-tab5-nsh | Tab5 (NuttX) | ✅ | 266 KB |
| nuttx-tab5-lvgl_demo | Tab5 (NuttX + LVGL) | ✅ | 620 KB |
| qemu-sticks3-bridge | QEMU esp32s3 | ✅ | boots to `app_main()`, then `M5PM1 id read FAILED` (no PMIC in QEMU — expected) |

### Findings

- **stackchan-estudio does not validate on ESPHome 2026.10.2.** The
  `n-IA-hane/esphome-intercom@v2026.5.0` duplex speaker hands the 2026.10 `mixer`
  no `bits_per_sample`. Cachy runs ESPHome 2026.9.1, so the node may still build
  there; it will break on the next ESPHome upgrade unless the intercom component
  is bumped or `bits_per_sample` is set explicitly.
- ESPHome 2026.10 builds with **native ESP-IDF 5.5.6**, no PlatformIO.
- `paper-badge` has `platform = espressif32` unpinned; it resolved to
  **espressif32 8.0.0 / Arduino-ESP32 3.3.12**. Pin it in `platformio.ini` if you want
  the badge to keep building the same way.
- `github://m5stack/esphome-yaml@main` (unpinned) resolved to `f76024e1527b` on this run.
- QEMU: the StickS3's octal PSRAM is not emulated, so the smoke image is a second
  build with quad PSRAM (`merged-qemu.bin`); the real image is untouched.

### Cost (list prices, estimate)

Cloud Build, all runs on 2026-11-06 including debugging: ~2.6 USD (~48 MXN).
Images: 2 builds on e2-highcpu-8 (~12 min). Firmware: 5 runs on e2-highcpu-32
(~11 min for a full run, ~0.70 USD each).
Storage: Artifact Registry ~7.5 GB (idf 5.5 GB, arduino 0.59, nuttx 0.56 ×2 tags — the plain `xpack14.2.0-3` tag is superseded by `-kf`, esphome 0.26) ≈ 0.75 USD/month;
GCS ~90 MB.
