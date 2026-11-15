# Tab5: plugging in USB-C power while running on battery restarts the device

One-page note for M5Stack (visit of 26 Nov 2026). Unit: Tab5, ESP32-P4 rev 1.3 + ESP32-C6, battery pack fitted
(third-party NP-F550 type). Firmware: our own ESP-IDF 6.0.3 application, esp-hosted 2.12.13 on the C6.
All measurements are from 11 Nov 2026; the full log is in `PDCA-2026-11-11.md` sections 5 and 6.

## What happens

The Tab5 runs from its battery. USB-C power is plugged in. The device restarts at once. Unplugging never restarts it.

## What we measured

| Test | Result |
|---|---|
| USB-C cable through a **USB-C-to-A adapter** into a PC's USB-A port | restart **7 of 7** plug-ins |
| **USB-C to USB-C** cable into a USB-C port (a Thunderbolt hub ×4, a PC's own USB-C port ×1) | restart **0 of 5** |
| One plug-in on a USB-PD wall charger | restarted once (cable not recorded, so not counted in either row) |
| Reset reason read at boot (`esp_reset_reason()`) | **BROWNOUT**, 2 of 2 instrumented plug-ins (detector level 7, the default) |
| Same plug-in with the brownout detector switched off | the P4 keeps running, but 4 s later esp-hosted reports `H_SDIO_DRV: Unrecoverable host sdio state` and restarts the host |
| Battery charger enable pin off, then USB-A 5 V output also off | still restarts — neither is the cause |

## How we read it

The supply dips for a moment when USB power arrives on a port that has 5 V live at the socket (USB-A). The dip trips the
P4's brownout detector, and it also knocks out the C6 link, so switching the detector off does not help. A USB-C source
only switches its 5 V on after it detects the plug, and with that we saw no restart.

Not measured: the dip itself (no oscilloscope), and whether M5Stack's own firmware restarts in the same test.

## What we do today

USB-C to USB-C cable into a USB-C port, and plug power in before a conversation, not during one. Our firmware shows
why it last started (Ajustes: "último arranque"), which is how we tell a plug-in restart from a crash.

## Questions for M5Stack

1. Is this known for the Tab5 power path, and is it expected with a USB-A source?
2. Is there a recommended way to ride through it — a brownout level, or a hardware revision that changes the switching?
3. For the C6: is in-place recovery of the SDIO link (esp-hosted `TRANSPORT_RESTART_ON_FAILURE=n` plus handling the
   failure event) the route you would take, or is a host restart the intended behaviour?
