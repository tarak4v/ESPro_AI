/**
 * @file display.c
 * @brief Display HAL — QSPI AXS15231B init, LVGL flush callback, backlight.
 */

#include "hal/display.h"
#include "hw_config.h"
#include "core/perf_monitor.h"

#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel = NULL;
static bool s_flipped = false;
static lv_disp_drv_t s_disp_drv;
static lv_disp_draw_buf_t s_draw_buf;

/* ── LVGL flush callback ── */
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    int64_t start = esp_timer_get_time();
    int x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;

    if (s_flipped) {
        x1 = LCD_H_RES - 1 - area->x2;
        y1 = LCD_V_RES - 1 - area->y2;
        x2 = LCD_H_RES - 1 - area->x1;
        y2 = LCD_V_RES - 1 - area->y1;
    }

    /* Panel uses portrait coords internally */
    esp_lcd_panel_draw_bitmap(s_panel, y1, LCD_NOROT_VRES - 1 - x2,
                              y2 + 1, LCD_NOROT_VRES - x1,
                              color_map);

    perf_log_event(PERF_SLOT_LVGL_FLUSH,
                   (uint32_t)((esp_timer_get_time() - start) / 1000));
    lv_disp_flush_ready(drv);
}

/* ── Backlight ── */
static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .gpio_num   = LCD_PIN_BL,
        .duty       = 0,        /* 0 = full brightness (inverted) */
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
}

/* ── Public API ── */

void display_init(void)
{
    backlight_init();

    /* Allocate double frame buffer in PSRAM */
    lv_color_t *buf1 = heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL frame buffers in PSRAM");
        return;
    }

    /* QSPI panel init — see ESP-IDF esp_lcd_axs15231b component */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num       = LCD_PIN_CS,
        .dc_gpio_num       = -1,
        .spi_mode          = 0,
        .pclk_hz           = 40 * 1000 * 1000,
        .trans_queue_depth  = 10,
        .lcd_cmd_bits       = 32,
        .lcd_param_bits     = 8,
    };

    spi_bus_config_t bus_cfg = {
        .sclk_io_num     = LCD_PIN_CLK,
        .data0_io_num    = LCD_PIN_D0,
        .data1_io_num    = LCD_PIN_D1,
        .data2_io_num    = LCD_PIN_D2,
        .data3_io_num    = LCD_PIN_D3,
        .max_transfer_sz = LVGL_SPIRAM_BUFF_LEN,
        .flags           = SPICOMMON_BUSFLAG_QUAD,
    };
    spi_bus_initialize(LCD_QSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_QSPI_HOST,
                             &io_config, &io_handle);

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_new_panel_axs15231b(io_handle, &panel_config, &s_panel);
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_disp_on_off(s_panel, true);

    /* LVGL display driver */
    lv_disp_draw_buf_init(&s_draw_buf, buf1, buf2,
                          LCD_NOROT_HRES * LCD_NOROT_VRES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res      = LCD_H_RES;
    s_disp_drv.ver_res      = LCD_V_RES;
    s_disp_drv.draw_buf     = &s_draw_buf;
    s_disp_drv.flush_cb     = disp_flush_cb;
    s_disp_drv.full_refresh = 1;
    lv_disp_drv_register(&s_disp_drv);

    ESP_LOGI(TAG, "Display init: %dx%d AMOLED, double-buffered PSRAM", LCD_H_RES, LCD_V_RES);
}

void display_set_brightness(uint8_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

void display_set_flip(bool flipped)
{
    s_flipped = flipped;
}

bool display_get_flip(void)
{
    return s_flipped;
}
