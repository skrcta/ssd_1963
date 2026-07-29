/*
 * SSD1963 / ER-TFT050-6-5654 production-wiring display test.
 *
 * The GPIO map is deliberately configured through menuconfig instead of being
 * hard-coded here. Defaults implement wiring_guide_esp32s3.md exactly.
 */
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LCD_WIDTH  800
#define LCD_HEIGHT 480
#define DRAW_LINES 20
#define DRAW_BUFFER_BYTES (LCD_WIDTH * DRAW_LINES * sizeof(uint16_t))

#define RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const char *TAG = "er_tft050_test";

typedef struct {
    int data[8];
    int cs;
    int dc;
    int wr;
    int rd;       // Kept in the map for wiring traceability; ESP-IDF i80 is TX-only.
    int reset;
    int backlight;
    bool backlight_active_high;
    uint32_t pclk_hz;
} display_pins_t;

static const display_pins_t pin_map = {
    .data = {
        CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_D1,
        CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_D3,
        CONFIG_HMI_LCD_PIN_D4, CONFIG_HMI_LCD_PIN_D5,
        CONFIG_HMI_LCD_PIN_D6, CONFIG_HMI_LCD_PIN_D7,
    },
    .cs = CONFIG_HMI_LCD_PIN_CS,
    .dc = CONFIG_HMI_LCD_PIN_DC,
    .wr = CONFIG_HMI_LCD_PIN_WR,
    .rd = CONFIG_HMI_LCD_PIN_RD,
    .reset = CONFIG_HMI_LCD_PIN_RESET,
    .backlight = CONFIG_HMI_LCD_PIN_BACKLIGHT,
    .backlight_active_high = CONFIG_HMI_LCD_BACKLIGHT_ACTIVE_HIGH,
    .pclk_hz = CONFIG_HMI_LCD_PCLK_HZ,
};

static esp_lcd_panel_io_handle_t lcd_io;
static SemaphoreHandle_t color_done;
static uint16_t *draw_buffer;

static bool color_transfer_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    (void)io;
    (void)edata;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

static void lcd_command(uint8_t command, const uint8_t *data, size_t data_len)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, command, data, data_len));
}

static void configure_output(int gpio, int level)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(gpio, level));
}

static void set_backlight(bool enabled)
{
    const int level = enabled == pin_map.backlight_active_high;
    ESP_ERROR_CHECK(gpio_set_level(pin_map.backlight, level));
}

static void reset_panel(void)
{
    configure_output(pin_map.reset, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(gpio_set_level(pin_map.reset, 1));
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void init_ssd1963(void)
{
    // Values are for the 800x480 panel; the input pixel format is RGB565.
    const uint8_t pll_mn[] = {0x23, 0x02, 0x04};
    const uint8_t pll_enable[] = {0x01};
    const uint8_t pll_use[] = {0x03};
    const uint8_t lshift_freq[] = {0x03, 0xFF, 0xFF};
    const uint8_t lcd_mode[] = {0x24, 0x00, 0x03, 0x1F, 0x01, 0xDF, 0x00};
    const uint8_t h_period[] = {0x03, 0x5F, 0x00, 0x2E, 0x00, 0x46, 0x00, 0x00};
    const uint8_t v_period[] = {0x01, 0xDF, 0x00, 0x16, 0x00, 0x0C, 0x00, 0x00};
    const uint8_t gpio_conf[] = {0x0F, 0x01};
    const uint8_t gpio_value[] = {0x01};
    const uint8_t pixel_format[] = {0x03}; // 16-bit 5:6:5 MCU input
    const uint8_t address_mode[] = {0x00};

    lcd_command(0xE2, pll_mn, sizeof(pll_mn));
    lcd_command(0xE0, pll_enable, sizeof(pll_enable));
    vTaskDelay(pdMS_TO_TICKS(10));
    lcd_command(0xE0, pll_use, sizeof(pll_use));
    vTaskDelay(pdMS_TO_TICKS(10));
    lcd_command(0x01, NULL, 0);             // Software reset
    vTaskDelay(pdMS_TO_TICKS(10));
    lcd_command(0xE6, lshift_freq, sizeof(lshift_freq));
    lcd_command(0xB0, lcd_mode, sizeof(lcd_mode));
    lcd_command(0xB4, h_period, sizeof(h_period));
    lcd_command(0xB6, v_period, sizeof(v_period));
    lcd_command(0xB8, gpio_conf, sizeof(gpio_conf));
    lcd_command(0xBA, gpio_value, sizeof(gpio_value));
    lcd_command(0xF0, pixel_format, sizeof(pixel_format));
    lcd_command(0x36, address_mode, sizeof(address_mode));
    lcd_command(0x29, NULL, 0);             // Display on
}

static void set_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    const uint16_t x_end = x + width - 1;
    const uint16_t y_end = y + height - 1;
    const uint8_t column[] = {x >> 8, x, x_end >> 8, x_end};
    const uint8_t page[] = {y >> 8, y, y_end >> 8, y_end};
    lcd_command(0x2A, column, sizeof(column));
    lcd_command(0x2B, page, sizeof(page));
}

static void write_solid_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                             uint16_t color)
{
    set_window(x, y, width, height);
    for (uint16_t row = 0; row < height;) {
        const uint16_t lines = (height - row) > DRAW_LINES ? DRAW_LINES : height - row;
        const size_t pixel_count = width * lines;
        for (size_t i = 0; i < pixel_count; ++i) {
            draw_buffer[i] = color;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(lcd_io, row == 0 ? 0x2C : 0x3C,
                                                   draw_buffer, pixel_count * sizeof(uint16_t)));
        xSemaphoreTake(color_done, portMAX_DELAY);
        row += lines;
    }
}

static void show_color_bars(void)
{
    static const uint16_t colors[] = {
        RGB565(255, 255, 255), RGB565(255, 255, 0), RGB565(0, 255, 255),
        RGB565(0, 255, 0), RGB565(255, 0, 255), RGB565(255, 0, 0),
        RGB565(0, 0, 255), RGB565(0, 0, 0),
    };
    const uint16_t bar_width = LCD_WIDTH / (sizeof(colors) / sizeof(colors[0]));
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        write_solid_rect(i * bar_width, 0, bar_width, LCD_HEIGHT, colors[i]);
    }
}

static void show_checkerboard(void)
{
    const uint16_t square_width = 80;
    const uint16_t square_height = 60;
    for (uint16_t y = 0; y < LCD_HEIGHT; y += square_height) {
        for (uint16_t x = 0; x < LCD_WIDTH; x += square_width) {
            const bool light = ((x / square_width) + (y / square_height)) & 1;
            write_solid_rect(x, y, square_width, square_height,
                             light ? RGB565(190, 190, 190) : RGB565(20, 20, 20));
        }
    }
}

static void show_gray_ramp(void)
{
    for (uint16_t x = 0; x < LCD_WIDTH; x += 20) {
        const uint8_t value = (uint32_t)x * 255 / (LCD_WIDTH - 20);
        write_solid_rect(x, 0, 20, LCD_HEIGHT, RGB565(value, value, value));
    }
}

static void init_lcd_bus(void)
{
    configure_output(pin_map.backlight, !pin_map.backlight_active_high);
    configure_output(pin_map.cs, 1); // Keep the panel deselected through boot.

    color_done = xSemaphoreCreateBinary();
    assert(color_done != NULL);

    esp_lcd_i80_bus_handle_t i80_bus;
    const esp_lcd_i80_bus_config_t bus_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = pin_map.dc,
        .wr_gpio_num = pin_map.wr,
        .data_gpio_nums = {
            pin_map.data[0], pin_map.data[1], pin_map.data[2], pin_map.data[3],
            pin_map.data[4], pin_map.data[5], pin_map.data[6], pin_map.data[7],
        },
        .bus_width = 8,
        .max_transfer_bytes = DRAW_BUFFER_BYTES,
        .dma_burst_size = 32,
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

    const esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = pin_map.cs,
        .pclk_hz = pin_map.pclk_hz,
        .trans_queue_depth = 1,
        .on_color_trans_done = color_transfer_done,
        .user_ctx = color_done,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = {
            .swap_color_bytes = CONFIG_HMI_LCD_SWAP_COLOR_BYTES,
            .pclk_idle_low = 1,
            .pclk_active_neg = 1,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &lcd_io));
    draw_buffer = esp_lcd_i80_alloc_draw_buffer(lcd_io, DRAW_BUFFER_BYTES, MALLOC_CAP_DMA);
    assert(draw_buffer != NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ER-TFT050 SSD1963 display test starting");
    ESP_LOGI(TAG, "D0..D7=%d,%d,%d,%d,%d,%d,%d,%d CS=%d DC=%d WR=%d RD(reserved)=%d RST=%d BL=%d @ %" PRIu32 " Hz",
             pin_map.data[0], pin_map.data[1], pin_map.data[2], pin_map.data[3],
             pin_map.data[4], pin_map.data[5], pin_map.data[6], pin_map.data[7],
             pin_map.cs, pin_map.dc, pin_map.wr, pin_map.rd, pin_map.reset,
             pin_map.backlight, pin_map.pclk_hz);

    init_lcd_bus();
    reset_panel();
    init_ssd1963();
    set_backlight(true);

    while (true) {
        show_color_bars();
        vTaskDelay(pdMS_TO_TICKS(3000));
        show_gray_ramp();
        vTaskDelay(pdMS_TO_TICKS(3000));
        show_checkerboard();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
