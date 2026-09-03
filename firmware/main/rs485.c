/*
 * RS-485 half-duplex communication driver — implementation.
 *
 * Call order follows the ESP-IDF uart_echo_rs485 example: driver install
 * first, uart_set_mode() last (it requires the driver object to exist).
 *
 * SPDX-License-Identifier: MIT
 */
#include "rs485.h"

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "rs485";

#define RS485_UART_NUM  CONFIG_HMI_RS485_UART_NUM
#define RS485_TXD_GPIO  CONFIG_HMI_RS485_TXD_GPIO
#define RS485_RXD_GPIO  CONFIG_HMI_RS485_RXD_GPIO
#define RS485_DIR_GPIO  CONFIG_HMI_RS485_DIR_GPIO
#define RS485_BUF_SIZE  512

esp_err_t rs485_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate  = CONFIG_HMI_RS485_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* Install the driver before any configuration calls. */
    ESP_RETURN_ON_ERROR(uart_driver_install(RS485_UART_NUM,
                                            RS485_BUF_SIZE * 2,
                                            0, 0, NULL, 0),
                        TAG, "driver install");

    ESP_RETURN_ON_ERROR(uart_param_config(RS485_UART_NUM, &uart_config),
                        TAG, "param config");

    /*
     * 4th argument is RTS -> drives DE/RE!.  5th is CTS, unused.
     * The UART peripheral asserts RTS during TX and releases it after
     * the final stop bit — no manual GPIO toggling required.
     */
    ESP_RETURN_ON_ERROR(uart_set_pin(RS485_UART_NUM,
                                     RS485_TXD_GPIO,
                                     RS485_RXD_GPIO,
                                     RS485_DIR_GPIO,
                                     UART_PIN_NO_CHANGE),
                        TAG, "set pin");

    ESP_RETURN_ON_ERROR(uart_set_mode(RS485_UART_NUM,
                                      UART_MODE_RS485_HALF_DUPLEX),
                        TAG, "set mode");

    /*
     * Inter-character timeout in UART symbol periods.  3 symbols is the
     * standard Modbus RTU inter-frame gap at most baud rates.
     */
    ESP_RETURN_ON_ERROR(uart_set_rx_timeout(RS485_UART_NUM, 3),
                        TAG, "rx timeout");

    ESP_LOGI(TAG, "initialised on UART%d (TX=%d, RX=%d, DIR=%d) @ %d baud",
             RS485_UART_NUM, RS485_TXD_GPIO, RS485_RXD_GPIO,
             RS485_DIR_GPIO, CONFIG_HMI_RS485_BAUD_RATE);

    return ESP_OK;
}

int rs485_send(const void *data, size_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }
    return uart_write_bytes(RS485_UART_NUM, data, len);
}

int rs485_receive(void *buf, size_t buf_size, uint32_t timeout_ms)
{
    if (buf == NULL || buf_size == 0) {
        return 0;
    }
    const TickType_t ticks = (timeout_ms == 0)
                                 ? 0
                                 : pdMS_TO_TICKS(timeout_ms);
    return uart_read_bytes(RS485_UART_NUM, buf, buf_size, ticks);
}
