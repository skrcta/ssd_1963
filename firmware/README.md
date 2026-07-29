# ER-TFT050-6-5654 ESP32-S3 display test

Minimal ESP-IDF firmware to validate the 8-bit 8080 wiring in
[`wiring_guide_esp32s3.md`](../wiring_guide_esp32s3.md). It uses ESP-IDF's native
`esp_lcd` Intel 8080 driver and a small SSD1963 initialization sequence—no
external display library is required.

## What it tests

The firmware resets the SSD1963 with the backlight off, initializes its
800x480 RGB565 interface, enables the backlight, then continuously shows:

- eight vertical colour bars (data-bit order and primary colours);
- a grayscale ramp (all RGB565 bits);
- an 80x60 checkerboard (address window / rectangle updates).

Serial logs print the effective GPIO map and write clock. RD remains in the
configuration for traceability, but ESP-IDF's LCD_CAM i80 driver is TX-only;
this first test does not read the controller ID.

## Pin-map configuration

The defaults match the wiring guide: DB0..DB7 = GPIO 2, 4..10; CS/DC/WR/RD =
GPIO 11/12/13/14; RESET/BL = GPIO 15/16. Change them without editing C:

```sh
. ~/espressif/esp-idf-v5.5/export.sh
cd firmware
idf.py set-target esp32s3
idf.py menuconfig
```

Open `ER-TFT050 display test` → `Display GPIO map`. The same menu also exposes
the 8080 write clock (10 MHz conservative default), RGB565 byte order, and
backlight polarity. Rebuild and flash after a change:

```sh
idf.py build flash monitor
```

Hardware remains as specified in the guide: R3 fitted/R4 open (8080), J3
fitted/J4 open (external backlight), and J8 open for a 5 V display supply.
