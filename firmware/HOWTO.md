# Flashing prebuilt firmware on Waveshare ESP32-S3-Pico

This guide is for flashing the ER-TFT050-6-5654 SSD1963 display-test firmware
onto a Waveshare ESP32-S3-Pico without installing ESP-IDF.

The Waveshare ESP32-S3-Pico uses an ESP32-S3R2, 2 MB PSRAM, 16 MB flash, USB-C,
a CH343 USB-UART bridge, and a CH334 USB hub. The commands below are therefore
written for `--chip esp32s3` and the board's CH343 UART serial port.

> [!WARNING]
> This firmware is still a display bring-up diagnostic. Display timing, colour
> order, latch edge, and backlight polarity still need bench validation on real
> hardware.

## Download firmware

Download the latest `ssd1963-er-tft050-esp32s3-*.zip` asset from:

https://github.com/skrcta/ssd_1963/releases

Extract it to a local folder. A release archive should contain:

- `bootloader.bin`
- `partition-table.bin`
- `er_tft050_display_test.bin`
- `flash_args.txt`
- `flasher_args.json`
- `SHA256SUMS.txt`
- `flash_windows.cmd`
- `flash_linux.sh`

If `SHA256SUMS.txt` is present, verify the files before flashing:

Windows PowerShell:

```powershell
Get-FileHash .\bootloader.bin -Algorithm SHA256
Get-FileHash .\partition-table.bin -Algorithm SHA256
Get-FileHash .\er_tft050_display_test.bin -Algorithm SHA256
```

Linux/macOS:

```sh
sha256sum -c SHA256SUMS.txt
```

## Install flashing tools

Install Python 3.10 or newer, then install `esptool`.

Windows:

```cmd
py -m pip install --upgrade esptool pyserial
```

Linux/macOS:

```sh
python3 -m pip install --upgrade esptool pyserial
```

Check that `esptool` works:

```sh
esptool.py version
```

## Find the serial port

Connect the ESP32-S3-Pico to USB-C. Use the CH343 UART serial port for these
commands.

Windows:

```cmd
py -m serial.tools.list_ports
```

Typical result:

```text
COM5 - USB-SERIAL CH343
```

You can also open Device Manager, expand "Ports (COM & LPT)", and use the COM
port shown for the CH343 device.

Linux:

```sh
python3 -m serial.tools.list_ports
```

Typical result:

```text
/dev/ttyACM0 - USB-Enhanced-SERIAL CH343
```

or:

```text
/dev/ttyUSB0 - USB-SERIAL CH343
```

## Erase flash

Erasing is recommended before switching between unrelated firmware images.

Windows example:

```cmd
esptool.py --chip esp32s3 --port COM5 erase_flash
```

Linux/macOS example:

```sh
esptool.py --chip esp32s3 --port /dev/ttyACM0 erase_flash
```

Replace the port with the one found on your machine.

## Flash firmware

The ESP-IDF default flash offsets are:

| Offset | File |
| --- | --- |
| `0x0` | `bootloader.bin` |
| `0x8000` | `partition-table.bin` |
| `0x10000` | `er_tft050_display_test.bin` |

Windows `cmd.exe`:

```cmd
esptool.py ^
  --chip esp32s3 ^
  --port COM5 ^
  --baud 921600 ^
  write_flash ^
  0x0 bootloader.bin ^
  0x8000 partition-table.bin ^
  0x10000 er_tft050_display_test.bin
```

Linux/macOS:

```sh
esptool.py \
  --chip esp32s3 \
  --port /dev/ttyACM0 \
  --baud 921600 \
  write_flash \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 er_tft050_display_test.bin
```

The ESP32-S3-Pico has an automatic download circuit for UART flashing through
the CH343 port. In normal use, `esptool` will reset the board into download
mode by itself. If connection fails, hold `BOOT`, tap `RESET`, release `BOOT`,
and run the command again.

Waveshare also documents a native USB download path where `BOOT` is held before
connecting the Type-C cable. This HOWTO uses the CH343 UART path instead,
because it matches the board's documented automatic download circuit and the
usual ESP-IDF/esptool workflow.

## Monitor logs

After flashing, reset the board and open the serial monitor at 115200 baud.

Windows:

```cmd
py -m serial.tools.miniterm COM5 115200
```

Linux/macOS:

```sh
python3 -m serial.tools.miniterm /dev/ttyACM0 115200
```

Exit miniterm with `Ctrl+]`.

Expected firmware behaviour:

- serial logs print the configured GPIO map and LCD write clock;
- the backlight turns on after initialization;
- the display cycles vertical colour bars, a grayscale ramp, and an 80x60
  checkerboard.

If the display remains blank, verify the wiring guide first, especially the
8080-mode jumpers, 5 V display power, common ground, `/RD` pull-up, reset, and
backlight wiring.

## Build locally instead

If you have ESP-IDF v5.5 installed, build and flash from this directory:

```sh
source ~/espressif/esp-idf-v5.5/export.sh
idf.py set-target esp32s3
idf.py build flash monitor
```

The local build outputs equivalent images at:

- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`
- `build/er_tft050_display_test.bin`

## Create a release ZIP

Maintainers can build and package the firmware with:

```sh
source ~/espressif/esp-idf-v5.5/export.sh
firmware/tools/release_firmware.sh --tag v0.1.0
```

The script validates that the generated ESP-IDF flash metadata targets
`esp32s3` and 16 MB flash. It builds in `firmware/build-release/` so stale
local `sdkconfig` files do not affect the published firmware. The output ZIP
is written to:

```text
firmware/dist/ssd1963-er-tft050-esp32s3-pico-v0.1.0.zip
```

To upload the ZIP to GitHub as a draft release asset:

```sh
firmware/tools/release_firmware.sh --tag v0.1.0 --upload
```

To create a published release instead of a draft:

```sh
firmware/tools/release_firmware.sh --tag v0.1.0 --upload --publish
```

## Board reference

- Waveshare ESP32-S3-Pico wiki:
  https://www.waveshare.com/wiki/ESP32-S3-Pico
- Waveshare ESP32-S3-Pico documentation:
  https://docs.waveshare.com/ESP32-S3-Pico
