# Wiring Guide: ER-TFT050-6-5654 to Waveshare ESP32-S3 Pico

This document provides a recommended wiring scheme to connect the ER-TFT050-6-5654 (5-inch TFT LCD with SSD1963) to a Waveshare ESP32-S3 Pico.

## Interface Strategy: 8-Bit 8080 Parallel
The ESP32-S3 Pico exposes 27 GPIOs. A 16-bit interface would need 22 of them (16 data + 6 control), leaving only 5 — not enough for the touch panel and a Modbus UART. **8-bit 8080 parallel mode** is therefore the only practical choice.

With the 8-bit wiring below plus a capacitive touch panel, **9 GPIOs remain free** (GP1, GP35–GP42) — enough for an RS-485 Modbus port and spares.

* **Hardware configuration (datasheet §4.4):** 8080 mode requires **R3=0Ω, R4 No Connection**. This is the factory default, so no rework is normally needed. (6800 mode is the inverse: R4=0Ω, R3 NC.)
* **Backlight jumper (datasheet §4.4):** `J3 short, J4 open` routes backlight control to the **external** BL_ON/OFF signal on JP2 pin 39 — this is what the wiring below assumes, and it is the factory default. If a board arrives with `J4 short, J3 open`, the SSD1963 drives the backlight internally and pin 39 will do nothing.

### Throughput expectation
A full 800x480 frame at 16bpp is 768 KB, which in 8-bit mode is ~1.54 M bus write cycles. At a 20 MHz PCLK that is roughly **13 fps full-screen**. This is fine for an HMI built on partial/dirty-rectangle updates, but full-screen animation is not on the table with this interface.

---

## 1. Power Connections

| Display Pin (JP2) | Symbol | ESP32-S3 Pico Pin | Notes |
| :--- | :--- | :--- | :--- |
| 1 | VSS | GND | Common Ground |
| 2 | VDD | 5V (see below) | Ensure J8 on the display is OPEN to accept 5V power (datasheet §4.4). J8 open is the factory default; short J8 only if feeding 3.3V instead. |
| 40 | VSS | GND | Common Ground |

### Power budget — do not run the product from USB
The module draws up to **280 mA at 5V** (datasheet §4.6, IDD(5.0V)). Combined with an ESP32-S3 transmitting on Wi-Fi, total draw approaches the 500 mA budget of a USB 2.0 port.

* **Bench bring-up only:** VBUS (pin 40, left header) is acceptable. Note that VBUS is only live while USB is plugged in.
* **Product wiring:** feed an external regulated 5V rail to **VSYS** (pin 39, left header) and take display VDD from the same rail. Do not depend on VBUS.

---

## 2. Display Control & Data Bus (8-Bit Mode)

The ESP32-S3 has a flexible IO matrix, meaning you can assign these functions to almost any free GPIO. To make the physical wiring as clean as possible, the GPIOs below are grouped physically: the **entire 8-bit data bus is kept on the Left Header**, and the **control pins are grouped at the top of the Right Header**.

*Note: GPIO 3 is skipped in the data bus mapping because it is not exposed on the standard headers of the Waveshare Pico.*

| Display Pin (JP2) | Symbol | ESP32-S3 Pico GPIO | Physical Header Position | Description |
| :--- | :--- | :--- | :--- | :--- |
| 9 | DB0 | GPIO 2 | Left Side (Pin 25) | Data Bit 0 |
| 10 | DB1 | GPIO 4 | Left Side (Pin 26) | Data Bit 1 |
| 11 | DB2 | GPIO 5 | Left Side (Pin 27) | Data Bit 2 |
| 12 | DB3 | GPIO 6 | Left Side (Pin 29) | Data Bit 3 |
| 13 | DB4 | GPIO 7 | Left Side (Pin 31) | Data Bit 4 |
| 14 | DB5 | GPIO 8 | Left Side (Pin 32) | Data Bit 5 |
| 15 | DB6 | GPIO 9 | Left Side (Pin 34) | Data Bit 6 |
| 16 | DB7 | GPIO 10 | Left Side (Pin 35) | Data Bit 7 |
| 17 ~ 32 | DB8-DB23| NC | - | Leave floating in 8-bit mode |
| 8 | TE | NC | - | Not Connected (Reserved for future tearing synchronization) |
| 3 | /CS | GPIO 11 | Right Side (Pin 1) | Chip Select (Active Low) |
| 4 | D/C | GPIO 12 | Right Side (Pin 2) | Data / Command Select |
| 5 | E_ /RD | GPIO 14 | Right Side (Pin 5) | Read Strobe Signal (8080 mode: RD#) |
| 6 | R/W_ /WR | GPIO 13 | Right Side (Pin 4) | Write Strobe Signal (8080 mode: WR#) |
| 7 | /RESET_NC | GPIO 15 | Right Side (Pin 6) | Reset Signal (Active Low) |
| 39 | BL_ON/OFF | GPIO 16 | Right Side (Pin 7) | Backlight Enable / PWM Brightness Control (requires J3 short, J4 open) |

### Required passive components

**Boot-state pull resistors.** Every ESP32-S3 GPIO is high-impedance from power-on until firmware configures it. Two of these floats have visible consequences, so add external resistors:

| Signal | Resistor | Reason |
| :--- | :--- | :--- |
| /CS (GPIO 11) | 10 kΩ pull-**up** to 3.3V | Holds the display deselected during boot; prevents spurious bus cycles while the data lines float. |
| BL_ON/OFF (GPIO 16) | 10 kΩ pull-**down** to GND | Prevents an indeterminate backlight state at power-on — typically a full-brightness flash before firmware takes over. Invert if your backlight driver is active-low. |

**Series damping.** Put **22–33 Ω** in series on WR and on each of DB0–DB7, close to the ESP32-S3. See the logic-level note below — the abs-max rating leaves no headroom for ringing.

---

## 3. Touch Panel Connections

Choose the wiring based on the type of touch panel your display has (Capacitive or Resistive).

### Option A: Capacitive Touch Panel (CTP) - Uses I2C
| Display Pin (JP2) | Symbol | ESP32-S3 Pico GPIO | Physical Header Position | Description |
| :--- | :--- | :--- | :--- | :--- |
| 35 | CTP_SDA | GPIO 17 | Right Side (Pin 9) | I2C Data |
| 34 | CTP_SCL | GPIO 18 | Right Side (Pin 10) | I2C Clock |
| 36 | CTP_INT | GPIO 33 | Right Side (Pin 11) | Interrupt |
| 33 | CTP_ /RST | GPIO 34 *(optional)* | Right Side (Pin 12) | Touch Reset — **can be left unconnected.** The module has an onboard RC reset circuit (datasheet §4.1). Omit to free a GPIO unless you need firmware-commanded touch recovery. |

*Confirm I2C pull-ups: the CTP module normally carries its own on SDA/SCL. If a bus scan finds nothing, add 4.7 kΩ to 3.3V on both lines.*

### Option B: Resistive Touch Panel (RTP) - Uses SPI
| Display Pin (JP2) | Symbol | ESP32-S3 Pico GPIO | Physical Header Position | Description |
| :--- | :--- | :--- | :--- | :--- |
| 33 | RTP CS | GPIO 17 | Right Side (Pin 9) | SPI Chip Select |
| 34 | RTP CLK | GPIO 18 | Right Side (Pin 10) | SPI Clock |
| 35 | RTP DIN | GPIO 33 | Right Side (Pin 11) | SPI MOSI (Data In) |
| 36 | RTP DOUT | GPIO 34 | Right Side (Pin 12) | SPI MISO (Data Out) |
| 38 | RTP PEN | GPIO 35 | Right Side (Pin 14) | Pen Interrupt (IRQ). **Open-drain output** (datasheet §4.1) — it can only pull low, so a pull-up is mandatory. Enable the ESP32-S3 internal pull-up on GPIO 35, or fit 10 kΩ to 3.3V. Without it the pin never reads high and touch detection will not work. |
| 37 | RTP BUSY | NC | - | Usually left unconnected |

---

## Notes for Software Setup & Bring-up
When configuring your display library and initializing the hardware:
1. **Driver:** SSD1963
2. **Interface:** 8-bit 8080 parallel
3. **Library choice:** **LovyanGFX** is the recommended starting point — its i80 bus backend targets the ESP32-S3 LCD_CAM peripheral directly. TFT_eSPI also lists SSD1963 drivers, but confirm its ESP32-S3 parallel support before committing. *(Not verified against a checked-out copy of either library — confirm before relying on this.)*
4. **Pin Mapping:** Map the exact GPIO numbers you physically wired to the corresponding data and control pin definitions in the library's setup file (e.g. `User_Setup.h` for TFT_eSPI).
5. **Touch pull-up:** if using RTP, enable the internal pull-up on the PEN GPIO (see §3 Option B).
6. **Initialization Checklist:**
   * Verify controller ID during initialization
   * Verify PLL lock
   * Read status register after reset (ensure RD is wired as recommended above)
   * Test both write and read cycles

### UART availability
GPIO 43/44 — the ESP32-S3 default UART0 pins — are **not** brought out to the Pico headers. Any Modbus RTU / RS-485 port must be routed through the GPIO matrix to spare pins (see the free-pin row in the Quick Reference below).

---

## Logic Levels — Resolved

**No level shifters are required.** Datasheet §4.6 specifies VDDIO as 3.0V min / 3.3V typ / 3.6V max, so the ESP32-S3's 3.3V I/O drives the SSD1963 interface directly.

**However:** datasheet §4.5 lists the absolute maximum for VDDIO as **+3.3V** — contradicting the 3.6V max in §4.6, and leaving zero headroom above the nominal rail. Treat 3.3V as a hard ceiling:
* Keep the 3.3V rail tightly regulated, with no overshoot at power-on.
* Fit the 22–33 Ω series resistors specified in §2 so that transmission-line ringing on WR and the data bus cannot push above the abs-max rating.

---

## Quick Reference Summary

| Function | GPIO |
|-----------|------|
| DB0 | GPIO2 |
| DB1 | GPIO4 |
| DB2 | GPIO5 |
| DB3 | GPIO6 |
| DB4 | GPIO7 |
| DB5 | GPIO8 |
| DB6 | GPIO9 |
| DB7 | GPIO10 |
| CS | GPIO11 |
| DC | GPIO12 |
| WR | GPIO13 |
| RD | GPIO14 |
| RESET | GPIO15 |
| Backlight | GPIO16 |
| Touch (CTP: SDA / SCL / INT / RST) | GPIO17 / 18 / 33 / 34 |
| Touch (RTP: CS / CLK / DIN / DOUT / PEN) | GPIO17 / 18 / 33 / 34 / 35 |
| **Free for application use** | GP1, GP35*–GP42 |

\* GP35 is free only with a capacitive panel; RTP uses it for PEN.

### Sources
Verified against `Datasheets/ER-TFT050-6-5654_Datasheet.pdf` (§4.1 pin configuration, §4.4 jump points, §4.5–4.6 electrical characteristics) and the Waveshare board pinout in `Assets/ESP32-S3-Pico-details-inter-1.jpg`.
