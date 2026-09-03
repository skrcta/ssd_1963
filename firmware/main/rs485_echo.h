/*
 * RS-485 echo test task.
 *
 * Spawns a FreeRTOS task that receives data on the RS-485 bus and echoes
 * it back to the sender.  Validates wiring, baud rate, and direction
 * control without a Modbus stack — just connect a USB-to-RS-485 adapter
 * on a PC and send bytes.
 *
 * NOTE: This is NOT a self-loopback.  DE and RE! are tied together, so
 * the receiver is muted while transmitting.  An external partner on
 * the bus is required.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the RS-485 echo test task.
 *
 * The task runs at low priority and echoes every received byte sequence
 * back on the bus, logging each exchange.  Call rs485_init() first.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if the task could not be
 *         created.
 */
esp_err_t rs485_echo_start(void);

#ifdef __cplusplus
}
#endif
