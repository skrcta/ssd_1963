/*
 * RS-485 echo test task — implementation.
 *
 * Runs a FreeRTOS task that blocks on rs485_receive(), logs what it
 * got, and echoes the buffer back with rs485_send().  Useful for
 * validating the physical layer with a USB-to-RS-485 adapter on a PC.
 *
 * SPDX-License-Identifier: MIT
 */
#include "rs485_echo.h"

#include "rs485.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "rs485_echo";

/*
 * Receive buffer size in bytes.  uart_read_bytes() never writes past
 * this limit; any surplus stays in the driver's 1024-byte RX ring for
 * the next loop iteration.  A burst larger than 512 bytes is therefore
 * echoed as two chunks, which is acceptable for a diagnostic echo test.
 */
#define ECHO_BUF_SIZE  512

/*
 * End-of-frame idle timeout in milliseconds.  uart_read_bytes() loops
 * until it fills the entire buffer OR the timeout expires, so this value
 * is effectively the echo latency for any frame shorter than ECHO_BUF_SIZE.
 *
 * Lower bound: must exceed the largest inter-byte gap *within* a frame.
 * rs485_init() sets a 3-symbol rx timeout via uart_set_rx_timeout().
 * At the slowest Kconfig baud (1200), one character ≈ 8.33 ms, so
 * 3 characters ≈ 25 ms.
 *
 * 50 ms clears that worst case with margin across the full 1200–460800
 * baud range.  Do not go below 50 ms without re-deriving the lower bound.
 */
#define ECHO_RX_TIMEOUT_MS  50

/* Task stack size in bytes — generous for ESP_LOG formatting.  */
#define ECHO_TASK_STACK  4096

/* Task priority — just above idle so display work is never starved. */
#define ECHO_TASK_PRIO  (tskIDLE_PRIORITY + 1)

/**
 * Dump up to @p len bytes as space-separated hex to the log.
 */
static void log_hex(const char *prefix, const uint8_t *data, size_t len)
{
    /* Format into a stack buffer.  Each byte becomes "XX " (3 chars).
     * Cap at ~64 bytes (192 chars + NUL) to avoid overflowing the
     * log line or the task stack. */
    const size_t max_bytes = 64;
    const size_t show = (len <= max_bytes) ? len : max_bytes;
    char hex[max_bytes * 3 + 4];   /* +4 for possible "..." + NUL */
    size_t pos = 0;

    for (size_t i = 0; i < show; ++i) {
        hex[pos++] = "0123456789ABCDEF"[data[i] >> 4];
        hex[pos++] = "0123456789ABCDEF"[data[i] & 0x0F];
        hex[pos++] = ' ';
    }
    if (len > max_bytes) {
        hex[pos++] = '.';
        hex[pos++] = '.';
        hex[pos++] = '.';
    }
    hex[pos] = '\0';

    ESP_LOGI(TAG, "%s (%u bytes): %s", prefix, (unsigned)len, hex);
}

static void rs485_echo_task(void *arg)
{
    (void)arg;
    uint8_t buf[ECHO_BUF_SIZE];

    ESP_LOGI(TAG, "echo task started — waiting for data");

    while (true) {
        const int n = rs485_receive(buf, sizeof(buf), ECHO_RX_TIMEOUT_MS);
        if (n <= 0) {
            /* Timeout or error — just loop. */
            continue;
        }

        log_hex("RX", buf, (size_t)n);

        const int sent = rs485_send(buf, (size_t)n);
        if (sent < 0) {
            ESP_LOGE(TAG, "send failed");
        } else if ((size_t)sent != (size_t)n) {
            ESP_LOGW(TAG, "short write: sent %d of %d bytes", sent, n);
        } else {
            ESP_LOGI(TAG, "TX echoed %d bytes", sent);
        }
    }
}

esp_err_t rs485_echo_start(void)
{
    const BaseType_t ret = xTaskCreate(rs485_echo_task,
                                       "rs485_echo",
                                       ECHO_TASK_STACK,
                                       NULL,
                                       ECHO_TASK_PRIO,
                                       NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create echo task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
