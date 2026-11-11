# PAPP ESP-NOW bridge

A small ESP32-S3 (StampS3, AtomS3 Lite, XIAO ESP32S3 …) that speaks ESP-NOW to the remotes and plain text lines to the
Tab5. Protocol and bench results: `../PROTOCOL.md` §6. Why a bridge at all: `../../docs/PDCA-2026-11-11.md` §3.1.

```sh
pio run                                                   # build
pio run -t upload --upload-port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00
python3 ../../client/tab5/tools/console_nr.py <port> 4 0.5:"T5B info"
python3 ../tools/bridge_relay.py --bridge <port> --seconds 120      # bench: cachy is the cable to the Tab5
```

Back up the board first if it already has a job (`esptool read-flash`), as done on 2026-11-11 for the line sensor.
