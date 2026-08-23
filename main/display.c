#include "display.h"

#include "config.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LCD_HOST SPI2_HOST
#define LCD_CS GPIO_NUM_12
#define LCD_PCLK GPIO_NUM_11
#define LCD_DATA0 GPIO_NUM_4
#define LCD_DATA1 GPIO_NUM_5
#define LCD_DATA2 GPIO_NUM_6
#define LCD_DATA3 GPIO_NUM_7

#define TOUCH_ADDR_CST820 0x15
#define V2_PANEL_X_GAP 0x10

#define LCD_CMD_BRIGHTNESS 0x51

// LCD reset, DSI power enable, and touch reset are not ESP32 GPIOs at all -
// they're output pins on the TCA9554 IO expander (address 0x20, the same
// chip button.c reads the PWR button from). The expander is a separate chip
// with its own power domain, so an ESP32-only reset (a reflash, or the RTS
// reset pulse) does NOT reset it: these lines simply keep whatever level
// they were last driven to, potentially by completely different firmware.
// Neither FluidBox nor earlier TiltMaze ever drove them, relying entirely on
// whatever state happened to be left behind - normally released (panel out
// of reset) after a real power-on, but not guaranteed, and not something a
// reflash can be trusted to inherit correctly. That produced exactly what
// was seen on hardware: perfectly healthy firmware - correct rendering,
// stable fps, no crashes - driving a panel that was electrically held in
// reset and simply never receiving any of it. The exact register layout and
// timing below are copied from the board's own official
// examples/esp-idf/90_axp2101_pmu component (board_variant.c,
// release_touch_reset()), which is the only first-party code that actually
// drives this reset.
#define IO_EXPANDER_ADDR 0x20
#define IO_EXPANDER_REG_OUTPUT 0x01
#define IO_EXPANDER_REG_CONFIG 0x03
#define IO_EXPANDER_LCD_RST (1 << 0)
#define IO_EXPANDER_DSI_PWR_EN (1 << 1)
#define IO_EXPANDER_TOUCH_RST (1 << 2)
#define IO_EXPANDER_SD_CS (1 << 7)
#define IO_EXPANDER_RESET_MASK \
    (IO_EXPANDER_LCD_RST | IO_EXPANDER_DSI_PWR_EN | IO_EXPANDER_TOUCH_RST | IO_EXPANDER_SD_CS)

// Briefly dropped to 40 MHz while chasing a black-screen bug that turned
// out to be reset_display_lines() being missing entirely (see the comment
// above it) - the panel was electrically held in reset, which no clock
// speed was ever going to fix. Restored to match FluidBox: 80 MHz is the
// ESP32-S3 SPI ceiling and halves the transfer time versus the driver's
// 40 MHz default. Drop back to 40 MHz if the panel ever actually shows
// tearing or corrupted pixels; these signals go through the GPIO matrix
// rather than dedicated IOMUX pins.
#define LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)

#define BAND_PIXELS (LCD_H_RES * BAND_ROWS)

static const char *TAG = "display";

// Two buffers, alternating by band index, so one can be filled while the other
// is being transmitted. DMA_ATTR keeps them in DMA-capable internal SRAM.
static DMA_ATTR uint16_t s_band_buf[2][BAND_PIXELS];

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_buffers_free;
static bool s_is_v2;

// Same power-on sequence Waveshare uses: pixel format RGB565, brightness and
// HBM registers open, full-window address set, sleep out, display on.
static const co5300_lcd_init_cmd_t s_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x11, NULL, 0, 100},
    {0x29, NULL, 0, 0},
};

static bool IRAM_ATTR on_trans_done(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *event,
                                    void *user_ctx)
{
    (void)io;
    (void)event;
    (void)user_ctx;

    BaseType_t higher_priority_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_buffers_free, &higher_priority_woken);
    return higher_priority_woken == pdTRUE;
}

static esp_err_t io_expander_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    const uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(dev, payload, sizeof(payload), 100);
}

// Pulses LCD_RST, DSI_PWR_EN, and TOUCH_RST low then high through the IO
// expander, and leaves SD_CS deasserted throughout. Must run before any
// touch probing or panel command - both are meaningless while their reset
// lines are asserted. Timing (20 ms low, 150 ms settle after release)
// matches the official reference exactly; this isn't a value worth
// improvising on.
static void reset_display_lines(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return;
    }

    i2c_master_dev_handle_t expander = NULL;
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = IO_EXPANDER_ADDR,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &expander) != ESP_OK) {
        ESP_LOGW(TAG, "IO expander not reachable, cannot reset display lines");
        return;
    }

    // Config register: 0 bit = output. Only these four pins are driven here;
    // every other expander pin (including the PWR button button.c reads)
    // keeps whatever direction it already has.
    esp_err_t ret = io_expander_write(expander, IO_EXPANDER_REG_CONFIG,
                                       (uint8_t)~IO_EXPANDER_RESET_MASK);
    ret |= io_expander_write(expander, IO_EXPANDER_REG_OUTPUT, IO_EXPANDER_SD_CS);
    vTaskDelay(pdMS_TO_TICKS(20));
    ret |= io_expander_write(expander, IO_EXPANDER_REG_OUTPUT, IO_EXPANDER_RESET_MASK);
    if (ret != ESP_OK) {
        // Not fatal by itself - display_init() still proceeds and the panel
        // may already be correctly reset from a prior run - but this is
        // exactly the kind of failure that previously showed up as a
        // perfectly healthy-looking boot log driving a dead screen, so it
        // needs to be loud rather than silently swallowed.
        ESP_LOGE(TAG, "display line reset had at least one failed I2C write (0x%x) - "
                      "panel may still be held in reset",
                 ret);
    }
    vTaskDelay(pdMS_TO_TICKS(150));

    i2c_master_bus_rm_device(expander);
}

// The two board revisions differ only in the touch controller address and a
// 16 pixel horizontal offset on the panel.
static bool detect_v2(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return false;
    }
    const bool is_v2 = i2c_master_probe(bus, TOUCH_ADDR_CST820, 50) == ESP_OK;
    ESP_LOGI(TAG, "detected %s board revision",
             is_v2 ? "V2 (CO5300/CST820)" : "original (SH8601/FT3168)");
    return is_v2;
}

esp_err_t display_init(i2c_master_bus_handle_t i2c_bus)
{
    reset_display_lines(i2c_bus);
    s_is_v2 = detect_v2(i2c_bus);

    s_buffers_free = xSemaphoreCreateCounting(2, 2);
    if (s_buffers_free == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const spi_bus_config_t bus_config = CO5300_PANEL_BUS_QSPI_CONFIG(
        LCD_PCLK, LCD_DATA0, LCD_DATA1, LCD_DATA2, LCD_DATA3,
        BAND_PIXELS * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init failed");

    esp_lcd_panel_io_spi_config_t io_config =
        CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS, on_trans_done, NULL);
    // The driver's default is 40 MHz, which caps the panel at about 52 fps
    // since a full frame is 322 KB. 80 MHz is the ESP32-S3 SPI ceiling and
    // halves the transfer time.
    io_config.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_io),
        TAG, "panel io init failed");

    const co5300_vendor_config_t vendor_config = {
        .init_cmds = s_init_cmds,
        .init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_io, &panel_config, &s_panel),
                        TAG, "panel init failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel setup failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, s_is_v2 ? V2_PANEL_X_GAP : 0, 0),
                        TAG, "panel gap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "display on failed");

    return ESP_OK;
}

uint16_t *display_acquire_band(void)
{
    // Transfers complete in the order they were queued, so once a slot frees
    // up the next buffer in rotation is guaranteed idle.
    xSemaphoreTake(s_buffers_free, portMAX_DELAY);

    static unsigned next;
    return s_band_buf[next++ & 1];
}

esp_err_t display_flush_band(int band_index, const uint16_t *buffer)
{
    const int y0 = band_index * BAND_ROWS;
    return esp_lcd_panel_draw_bitmap(s_panel, 0, y0, LCD_H_RES, y0 + BAND_ROWS, buffer);
}

esp_err_t display_set_brightness(uint8_t level)
{
    return esp_lcd_panel_io_tx_param(s_io, LCD_CMD_BRIGHTNESS, &level, 1);
}
