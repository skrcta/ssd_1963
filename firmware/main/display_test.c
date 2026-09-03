/*
 * SSD1963 / ER-TFT050-6-5654 production-wiring display test.
 *
 * The GPIO map is deliberately configured through menuconfig instead of being
 * hard-coded here. Defaults implement wiring_guide_esp32s3.md exactly.
 */
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rs485.h"
#include "rs485_echo.h"

#define LCD_WIDTH  800
#define LCD_HEIGHT 480
#define DRAW_LINES 20
#define LCD_BYTES_PER_PIXEL 3
#define DRAW_BUFFER_BYTES (LCD_WIDTH * DRAW_LINES * LCD_BYTES_PER_PIXEL)

#define LCD_HT_REG  0x041F
#define LCD_HPS     0x00D2
#define LCD_HPW_REG 0x00
#define LCD_LPS     0x0000
#define LCD_VT_REG  0x020C
#define LCD_VPS     0x0022
#define LCD_VPW_REG 0x00
#define LCD_FPS     0x0000

#define LCD_HT  (LCD_HT_REG + 1)
#define LCD_HPW (LCD_HPW_REG + 1)
#define LCD_VT  (LCD_VT_REG + 1)
#define LCD_VPW (LCD_VPW_REG + 1)

_Static_assert(LCD_HT > LCD_WIDTH, "horizontal total must exceed active width");
_Static_assert(LCD_VT > LCD_HEIGHT, "vertical total must exceed active height");
_Static_assert(LCD_HPS >= LCD_HPW, "horizontal sync must fit before active data");
_Static_assert(LCD_VPS >= LCD_VPW, "vertical sync must fit before active data");
_Static_assert(LCD_HT > LCD_HPS + LCD_WIDTH,
               "horizontal timing must leave a front porch");
_Static_assert(LCD_VT > LCD_VPS + LCD_HEIGHT,
               "vertical timing must leave a front porch");

static const char *TAG = "er_tft050_test";

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} rgb888_t;

_Static_assert(sizeof(rgb888_t) == LCD_BYTES_PER_PIXEL,
               "RGB888 pixels must be exactly three bytes");

typedef struct {
    int data[8];
    int cs;
    int dc;
    int wr;
    int rd;       // Held inactive; ESP-IDF i80 is TX-only.
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

/*
 * Compile-time duplicate detection.  Every pin value comes from CONFIG_*
 * constants, so a collision is knowable at build time.  The runtime loop in
 * validate_pin_map() remains as a defence-in-depth backstop.
 */
#define PINS_DIFFER(a, b) \
    _Static_assert((a) != (b), "GPIO conflict: " #a " == " #b)

/* D0–D7 vs each other */
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_D1);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_D2);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_D3);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_D4);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_D5);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_D6);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_D7);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_D2);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_D3);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_D4);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_D5);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_D6);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_D7);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_D3);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_D4);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_D5);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_D6);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_D7);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D3, CONFIG_HMI_LCD_PIN_D4);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D3, CONFIG_HMI_LCD_PIN_D5);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D3, CONFIG_HMI_LCD_PIN_D6);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D3, CONFIG_HMI_LCD_PIN_D7);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D4, CONFIG_HMI_LCD_PIN_D5);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D4, CONFIG_HMI_LCD_PIN_D6);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D4, CONFIG_HMI_LCD_PIN_D7);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D5, CONFIG_HMI_LCD_PIN_D6);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D5, CONFIG_HMI_LCD_PIN_D7);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D6, CONFIG_HMI_LCD_PIN_D7);

/* Control pins vs each other */
PINS_DIFFER(CONFIG_HMI_LCD_PIN_CS,        CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_CS,        CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_CS,        CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_CS,        CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_CS,        CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_DC,        CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_DC,        CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_DC,        CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_DC,        CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_WR,        CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_WR,        CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_WR,        CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_RD,        CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_RD,        CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_RESET,     CONFIG_HMI_LCD_PIN_BACKLIGHT);

/* Data pins vs control pins */
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_CS);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D0, CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_CS);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D1, CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_CS);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D2, CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D3, CONFIG_HMI_LCD_PIN_CS);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D3, CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D3, CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D3, CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D3, CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D3, CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D4, CONFIG_HMI_LCD_PIN_CS);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D4, CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D4, CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D4, CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D4, CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D4, CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D5, CONFIG_HMI_LCD_PIN_CS);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D5, CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D5, CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D5, CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D5, CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D5, CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D6, CONFIG_HMI_LCD_PIN_CS);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D6, CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D6, CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D6, CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D6, CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D6, CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D7, CONFIG_HMI_LCD_PIN_CS);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D7, CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D7, CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D7, CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D7, CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_LCD_PIN_D7, CONFIG_HMI_LCD_PIN_BACKLIGHT);

/* RS-485 pins vs each other */
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_RS485_RXD_GPIO);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_RS485_DIR_GPIO);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_RS485_DIR_GPIO);

/* RS-485 pins vs all display pins */
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_D0);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_D1);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_D2);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_D3);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_D4);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_D5);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_D6);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_D7);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_CS);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_RS485_TXD_GPIO, CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_D0);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_D1);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_D2);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_D3);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_D4);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_D5);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_D6);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_D7);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_CS);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_RS485_RXD_GPIO, CONFIG_HMI_LCD_PIN_BACKLIGHT);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_D0);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_D1);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_D2);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_D3);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_D4);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_D5);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_D6);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_D7);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_CS);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_DC);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_WR);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_RD);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_RESET);
PINS_DIFFER(CONFIG_HMI_RS485_DIR_GPIO, CONFIG_HMI_LCD_PIN_BACKLIGHT);

static esp_lcd_panel_io_handle_t lcd_io;
static SemaphoreHandle_t color_done;
static uint8_t *draw_buffer;

static void fail_startup(const char *reason)
{
    ESP_LOGE(TAG, "%s", reason);
    abort();
}

static void validate_output_gpio(const char *signal, int gpio)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        ESP_LOGE(TAG, "%s uses invalid or non-output GPIO %d", signal, gpio);
        abort();
    }
}

static void validate_input_gpio(const char *signal, int gpio)
{
    if (!GPIO_IS_VALID_GPIO(gpio)) {
        ESP_LOGE(TAG, "%s uses invalid GPIO %d", signal, gpio);
        abort();
    }
}

typedef struct {
    const char *name;
    int gpio;
    bool needs_output;   /* false for RXD, which is only ever read */
} pin_check_t;

static void validate_pin_map(void)
{
    /*
     * Single table of every assigned GPIO.  Both the capability check and
     * the duplicate sweep are derived from this one array, so adding a pin
     * cannot silently fall out of either check.
     *
     * needs_output is false for RS485_RXD: it is only ever read, so it
     * needs a valid GPIO rather than an output-capable one.
     */
    const pin_check_t checks[] = {
        { "D0",        pin_map.data[0],            true },
        { "D1",        pin_map.data[1],            true },
        { "D2",        pin_map.data[2],            true },
        { "D3",        pin_map.data[3],            true },
        { "D4",        pin_map.data[4],            true },
        { "D5",        pin_map.data[5],            true },
        { "D6",        pin_map.data[6],            true },
        { "D7",        pin_map.data[7],            true },
        { "CS",        pin_map.cs,                 true },
        { "D/C",       pin_map.dc,                 true },
        { "WR",        pin_map.wr,                 true },
        { "RD",        pin_map.rd,                 true },
        { "RESET",     pin_map.reset,              true },
        { "BACKLIGHT", pin_map.backlight,           true },
        { "RS485_TXD", CONFIG_HMI_RS485_TXD_GPIO,  true },
        { "RS485_RXD", CONFIG_HMI_RS485_RXD_GPIO,  false },
        { "RS485_DIR", CONFIG_HMI_RS485_DIR_GPIO,  true },
    };
    const size_t count = sizeof(checks) / sizeof(checks[0]);

    for (size_t i = 0; i < count; ++i) {
        if (checks[i].needs_output) {
            validate_output_gpio(checks[i].name, checks[i].gpio);
        } else {
            validate_input_gpio(checks[i].name, checks[i].gpio);
        }
        for (size_t j = 0; j < i; ++j) {
            if (checks[i].gpio == checks[j].gpio) {
                ESP_LOGE(TAG, "%s and %s both use GPIO %d",
                         checks[j].name, checks[i].name, checks[i].gpio);
                abort();
            }
        }
    }
}

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
    validate_output_gpio("output", gpio);
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
    /*
     * Provisional panel timing from TFT_eSPI's SSD1963_800BD_DRIVER sequence,
     * which is marked as copied from BuyDisplay code. The exact
     * ER-TFT050-6-5654 module datasheet does not publish this tuple, so the
     * PLL, pixel clock, sync polarity, and porch values still require bench
     * and logic-analyzer confirmation.
     *
     * Decoded SSD1963 timing (register values are zero-based where noted):
     *   PLL: 10 MHz reference, M=35, N=2 -> 120 MHz system clock
     *   PCLK: LCDC_FPR=0x33333 -> about 24 MHz
     *   active: 800 x 480; total: 1056 x 525
     *   horizontal: HPS=210, HPW=1, LPS=0; derived back/front porches 209/46
     *   vertical:   VPS=34,  VPW=1, FPS=0; derived back/front porches 33/11
     */
    const uint8_t pll_mn[] = {0x23, 0x02, 0x54};
    const uint8_t pll_enable[] = {0x01};
    const uint8_t pll_use[] = {0x03};
    const uint8_t lshift_freq[] = {0x03, 0x33, 0x33};
    const uint8_t lcd_mode[] = {0x20, 0x00, 0x03, 0x1F, 0x01, 0xDF, 0x00};
    const uint8_t h_period[] = {
        (uint8_t)(LCD_HT_REG >> 8), (uint8_t)LCD_HT_REG,
        (uint8_t)(LCD_HPS >> 8), (uint8_t)LCD_HPS,
        LCD_HPW_REG, (uint8_t)(LCD_LPS >> 8), (uint8_t)LCD_LPS, 0x00,
    };
    const uint8_t v_period[] = {
        (uint8_t)(LCD_VT_REG >> 8), (uint8_t)LCD_VT_REG,
        (uint8_t)(LCD_VPS >> 8), (uint8_t)LCD_VPS,
        LCD_VPW_REG, (uint8_t)(LCD_FPS >> 8), (uint8_t)LCD_FPS,
    };
    const uint8_t gpio_conf[] = {0x0F, 0x01};
    const uint8_t gpio_value[] = {0x01};
    const uint8_t pixel_format[] = {0x00};
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
    // 8-bit interface, 3 bytes/pixel; SSD1963 rev 1.6 §7.1.4,
    // Table 7-1 and §9.74.
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
                             rgb888_t color)
{
    set_window(x, y, width, height);
    for (uint16_t row = 0; row < height;) {
        const uint16_t lines = (height - row) > DRAW_LINES ? DRAW_LINES : height - row;
        const size_t pixel_count = width * lines;
        for (size_t i = 0; i < pixel_count; ++i) {
            const size_t offset = i * LCD_BYTES_PER_PIXEL;
            draw_buffer[offset] = color.red;
            draw_buffer[offset + 1] = color.green;
            draw_buffer[offset + 2] = color.blue;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(lcd_io, row == 0 ? 0x2C : 0x3C,
                                                   draw_buffer,
                                                   pixel_count * LCD_BYTES_PER_PIXEL));
        xSemaphoreTake(color_done, portMAX_DELAY);
        row += lines;
    }
}

static void show_color_bars(void)
{
    static const rgb888_t colors[] = {
        {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
        {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0},
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
                             light ? (rgb888_t){190, 190, 190} : (rgb888_t){20, 20, 20});
        }
    }
}

static void show_gray_ramp(void)
{
    for (uint16_t x = 0; x < LCD_WIDTH; x += 20) {
        const uint8_t value = (uint32_t)x * 255 / (LCD_WIDTH - 20);
        write_solid_rect(x, 0, 20, LCD_HEIGHT, (rgb888_t){value, value, value});
    }
}

static void init_lcd_bus(void)
{
    configure_output(pin_map.backlight, !pin_map.backlight_active_high);
    configure_output(pin_map.cs, 1); // Keep the panel deselected through boot.
    configure_output(pin_map.rd, 1); // TX-only bus: hold the active-low read strobe inactive.

    color_done = xSemaphoreCreateBinary();
    if (color_done == NULL) {
        fail_startup("failed to allocate color-transfer semaphore");
    }

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
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &lcd_io));
    draw_buffer = esp_lcd_i80_alloc_draw_buffer(lcd_io, DRAW_BUFFER_BYTES, MALLOC_CAP_DMA);
    if (draw_buffer == NULL) {
        fail_startup("failed to allocate DMA draw buffer");
    }
}

void app_main(void)
{
    validate_pin_map();
    ESP_ERROR_CHECK(rs485_init());
    ESP_ERROR_CHECK(rs485_echo_start());
    ESP_LOGI(TAG, "ER-TFT050 SSD1963 display test starting");
    ESP_LOGI(TAG, "D0..D7=%d,%d,%d,%d,%d,%d,%d,%d CS=%d DC=%d WR=%d RD(held high)=%d RST=%d BL=%d @ %" PRIu32 " Hz",
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
