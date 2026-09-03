/*
 * RS-485 half-duplex communication driver.
 *
 * Thin wrapper around the ESP-IDF UART driver configured for
 * UART_MODE_RS485_HALF_DUPLEX.  The DIR (DE/RE!) pin is driven
 * automatically by the hardware RTS signal — no manual toggling.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the RS-485 UART peripheral.
 *
 * Configures the UART port, pins, and half-duplex mode using values
 * from Kconfig (HMI_RS485_*).  Must be called once before rs485_send()
 * or rs485_receive().
 *
 * @return ESP_OK on success, or an error from the UART driver.
 */
esp_err_t rs485_init(void);

/**
 * Transmit data on the RS-485 bus.
 *
 * Hardware asserts DIR before the first start bit and releases it
 * after the last stop bit.
 *
 * @param data   Pointer to the bytes to send.
 * @param len    Number of bytes to send.
 * @return       Number of bytes written, or -1 on error.
 */
int rs485_send(const void *data, size_t len);

/**
 * Receive data from the RS-485 bus.
 *
 * Blocks up to @p timeout_ms milliseconds waiting for data.
 *
 * @param buf         Buffer to receive into.
 * @param buf_size    Maximum number of bytes to read.
 * @param timeout_ms  Receive timeout in milliseconds.  Use 0 for a
 *                    non-blocking check.
 * @return            Number of bytes read (may be 0 on timeout), or
 *                    -1 on error.
 */
int rs485_receive(void *buf, size_t buf_size, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
