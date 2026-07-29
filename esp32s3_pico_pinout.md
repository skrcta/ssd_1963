# Waveshare ESP32-S3-Pico Pinout Reference

Based on the provided pinout diagram, here is the complete reference for the Waveshare ESP32-S3-Pico board. The board features a 40-pin header layout compatible with the standard Raspberry Pi Pico form factor.

## Right Header (Pins 1 - 20)
*Oriented with the USB port at the top.*

| Physical Pin | Board Label | ESP32-S3 GPIO | Additional Functions / Notes |
| :---: | :--- | :--- | :--- |
| 1 | GP11 | GPIO 11 | |
| 2 | GP12 | GPIO 12 | |
| 3 | GND | GND | Ground |
| 4 | GP13 | GPIO 13 | |
| 5 | GP14 | GPIO 14 | |
| 6 | GP15 | GPIO 15 | |
| 7 | GP16 | GPIO 16 | |
| 8 | GND | GND | Ground |
| 9 | GP17 | GPIO 17 | |
| 10 | GP18 | GPIO 18 | |
| 11 | GP33 | GPIO 33 | |
| 12 | GP34 | GPIO 34 | |
| 13 | GND | GND | Ground |
| 14 | GP35 | GPIO 35 | |
| 15 | GP36 | GPIO 36 | |
| 16 | GP37 | GPIO 37 | |
| 17 | GP38 | GPIO 38 | |
| 18 | GND | GND | Ground |
| 19 | GP39 | GPIO 39 | |
| 20 | GP40 | GPIO 40 | |

---

## Left Header (Pins 40 - 21)
*Oriented with the USB port at the top, reading from top to bottom.*

| Physical Pin | Board Label | ESP32-S3 GPIO | Additional Functions / Notes |
| :---: | :--- | :--- | :--- |
| 40 | VBUS | - | 5V USB Power Input |
| 39 | VSYS | - | System Power Input |
| 38 | GND | GND | Ground |
| 37 | 3V3_EN | - | 3.3V Regulator Enable |
| 36 | 3V3 | - | 3.3V Power Output |
| 35 | GP10 | GPIO 10 | ADC1_CH9 |
| 34 | GP9 | GPIO 9 | ADC1_CH8 |
| 33 | GND | GND | Ground |
| 32 | GP8 | GPIO 8 | ADC1_CH7 |
| 31 | GP7 | GPIO 7 | ADC1_CH6 |
| 30 | RUN | - | System Reset (Active Low) |
| 29 | GP6 | GPIO 6 | ADC1_CH5 |
| 28 | GND | GND | Ground |
| 27 | GP5 | GPIO 5 | ADC1_CH4 |
| 26 | GP4 | GPIO 4 | ADC1_CH3 |
| 25 | GP2 | GPIO 2 | ADC1_CH1 |
| 24 | GP1 | GPIO 1 | ADC1_CH0 |
| 23 | GND | GND | Ground |
| 22 | GP41 | GPIO 41 | |
| 21 | GP42 | GPIO 42 | |

---

## Onboard Test Points (Near USB Port)

There are several test points (TP) on the top/back of the board near the USB port for direct access to specific signals:

* **TP1:** GND (Ground)
* **TP2:** USB D_N (Data Negative)
* **TP3:** USB D_P (Data Positive)
* **TP4:** GP3 (GPIO 3)
* **TP5:** GP21 (GPIO 21)
* **TP6:** GP0 (GPIO 0, often used for BOOT/Flash)

## Summary of Available Pins
* **Total Exposed GPIOs (on headers):** 27 Pins
* **ADC Capable Pins:** GP1, GP2, GP4, GP5, GP6, GP7, GP8, GP9, GP10 (ADC1 channels 0-9 except 2)
* **Power Pins:** VBUS (5V in), VSYS, 3V3 (3.3V out)
