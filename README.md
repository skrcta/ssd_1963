# SSD1963 / ESP32-S3 display bring-up

> [!WARNING]
> **The firmware has not yet been tested on physical hardware.**
> It builds against ESP-IDF v5.5, but display initialization, colour order,
> timing, backlight polarity, and the supplied pin map still require bench
> validation with the wired ESP32-S3 Pico and ER-TFT050-6-5654 display.

This repository contains a diagnostic firmware project for an
ER-TFT050-6-5654 (800x480, SSD1963) display connected to a Waveshare
ESP32-S3 Pico through an 8-bit 8080 parallel interface.

## Repository contents

- [`firmware/`](firmware/) — ESP-IDF v5.5 test application. Its pin map,
  backlight polarity, and write clock are configurable in `idf.py menuconfig`.
- [`wiring_guide_esp32s3.md`](wiring_guide_esp32s3.md) — required wiring,
  jumper, power, and logic-level guidance.
- [`esp32s3_pico_pinout.md`](esp32s3_pico_pinout.md) — board-header reference.
- [`docs/`](docs/) — locally retained SSD1963 and ER-TFT050-6-5654 reference
  datasheets used for the firmware review and corrections.

## First bench test

Read the wiring guide before applying power. Then:

```sh
source ~/espressif/esp-idf-v5.5/export.sh
cd firmware
idf.py menuconfig
idf.py build flash monitor
```

The firmware should cycle through vertical colour bars, a grayscale ramp, and
an 80x60 checkerboard every three seconds. Treat the first result as a wiring
and timing diagnostic, not proof of a production-ready display driver.
