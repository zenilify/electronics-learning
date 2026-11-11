# switch-xiaoc6 — RainMaker Neo switch on the Seeed Studio XIAO ESP32C6

Copy of `../switch-stamps3` (itself the vendor `examples/switch`) retargeted to `esp32c6`.

| Item | Value |
|---|---|
| Board | Seeed XIAO ESP32C6 (ESP32-C6, 4 MB flash), on the Grove Shield or the Expansion Board |
| User LED | GPIO15, driven directly (`CONFIG_SWITCH_USER_LED_GPIO`), active-low (hypothesis until the LED follows the state) |
| Button | BOOT = GPIO9 (`app_button` default for C6); short or long press toggles |
| Optional RGB | Grove WS2812 stick on the D0 Grove port = GPIO0 (`app_led`, WS2812) |
| Provisioning | BLE, assisted claiming, public Neo (same as the StampS3) |

Build: `~/bin/idf6 bash -c 'idf.py set-target esp32c6 && idf.py build'` from this directory
(`tools/fetch-vendor.sh` first). Flash: see `switch-stamps3/README` flow; back up the 4 MB flash and
check the image sha256 against the build log before writing (landmine 340).
