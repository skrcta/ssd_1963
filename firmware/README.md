# ER-TFT050-6-5654 ESP32-S3 display test

Minimal ESP-IDF firmware to validate the 8-bit 8080 wiring in
[`wiring_guide_esp32s3.md`](../wiring_guide_esp32s3.md). It uses ESP-IDF's native
`esp_lcd` Intel 8080 driver and a small SSD1963 initialization sequence - no
external display library is required.

## What it tests

The firmware resets the SSD1963 with the backlight off, initializes its
800x480 8-bit pixel interface, sends each pixel as ordered red, green, and blue
bytes, enables the backlight, then continuously shows:

- eight vertical colour bars (data-bit order and primary colours);
- a grayscale ramp (all RGB byte values);
- an 80x60 checkerboard (address window / rectangle updates).

Serial logs print the effective GPIO map and write clock. ESP-IDF's LCD_CAM
i80 driver is TX-only, so RD is driven and held high; this first test does not
read the controller ID.

## Pin-map configuration

The defaults match the wiring guide: DB0..DB7 = GPIO 2, 4..10; CS/DC/WR/RD =
GPIO 11/12/13/14; RESET/BL = GPIO 15/16. Change them without editing C:

```sh
. ~/espressif/esp-idf-v5.5/export.sh
cd ssd_1963/firmware
idf.py set-target esp32s3
idf.py menuconfig
```

Open `ER-TFT050 display test` -> `Display GPIO map`.

Use the menu entries to match your actual wiring:

| Menu entry | SSD1963/display signal | Default ESP32-S3-Pico GPIO |
| --- | --- | --- |
| `DB0 GPIO` | DB0 | GPIO 2 |
| `DB1 GPIO` | DB1 | GPIO 4 |
| `DB2 GPIO` | DB2 | GPIO 5 |
| `DB3 GPIO` | DB3 | GPIO 6 |
| `DB4 GPIO` | DB4 | GPIO 7 |
| `DB5 GPIO` | DB5 | GPIO 8 |
| `DB6 GPIO` | DB6 | GPIO 9 |
| `DB7 GPIO` | DB7 | GPIO 10 |
| `CS GPIO` | `/CS` | GPIO 11 |
| `D/C GPIO` | D/C or RS | GPIO 12 |
| `WR GPIO` | `/WR` | GPIO 13 |
| `RD GPIO` | `/RD`, held high by firmware | GPIO 14 |
| `RESET GPIO` | `/RESET` | GPIO 15 |
| `BL_ON/OFF GPIO` | backlight enable | GPIO 16 |

The parent `ER-TFT050 display test` menu also exposes:

- `8080 write clock (Hz)`: default `10000000`. Keep this at 10 MHz for first
  bring-up; raise it only after the display is stable.
- `Backlight enable is active high`: default enabled. Change only if the
  backlight control transistor or jumper wiring inverts the signal.

After editing the menu, choose `Save`, accept the default `sdkconfig` path, then
exit. Rebuild and flash after a change:

```sh
idf.py build flash monitor
```

At startup, the firmware prints the effective GPIO map. It also rejects
unavailable, input-only, or duplicate GPIO assignments before touching the LCD
bus, so a bad menuconfig edit should fail loudly in the serial log instead of
silently driving the wrong pins.

Hardware remains as specified in the guide: R3 fitted/R4 open (8080), J3
fitted/J4 open (external backlight), and J8 open for a 5 V display supply.
