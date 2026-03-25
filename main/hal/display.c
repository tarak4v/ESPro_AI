/**
 * @file display.c
 * @brief Display HAL — QSPI AXS15231B init, LVGL flush callback, backlight.
 *
 * Matches the proven init/flush from SS_Smallstart_VS (the working reference).
 */

#include "hal/display.h"
#include "hw_config.h"
#include "core/perf_monitor.h"

#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel = NULL;
static bool s_flipped = false;
static lv_disp_drv_t s_disp_drv;
static lv_disp_draw_buf_t s_draw_buf;

/* DMA flush synchronisation (same pattern as working project) */
static SemaphoreHandle_t s_flush_sema = NULL;
static uint16_t *s_dma_buf = NULL;
static uint16_t *s_rotat_buf = NULL;

/* Custom init commands — just SLPOUT + DISPON (matching working project) */
static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 100}, /* Sleep Out */
    {0x29, (uint8_t[]){0x00}, 0, 100}, /* Display On */
};

/* ── DMA transfer-done callback ── */
static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_sema, &woken);
    return false;
}

/* ── LVGL flush callback (chunked DMA, matches working project) ── */
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    int64_t start = esp_timer_get_time();

    /* Software 90° rotation — LVGL gives landscape coords (640×172),
       we need to rotate pixel data to portrait (172×640) for the panel. */
    uint16_t *src = (uint16_t *)color_map;
    uint32_t idx = 0;
    for (uint16_t j = 0; j < LCD_H_RES; j++)
    {
        for (uint16_t i = 0; i < LCD_V_RES; i++)
        {
            s_rotat_buf[idx++] = src[LCD_H_RES * (LCD_V_RES - i - 1) + j];
        }
    }

    /* If device is upside-down, reverse the entire buffer (180° flip) */
    if (s_flipped)
    {
        int32_t n = LCD_NOROT_HRES * LCD_NOROT_VRES;
        for (int32_t i = 0; i < n / 2; i++)
        {
            uint16_t tmp = s_rotat_buf[i];
            s_rotat_buf[i] = s_rotat_buf[n - 1 - i];
            s_rotat_buf[n - 1 - i] = tmp;
        }
    }

    uint16_t *map = s_rotat_buf;

    /* Chunked DMA flush: copy to internal DMA buffer, send in strips */
    const int flush_count = LVGL_SPIRAM_BUFF_LEN / LCD_DMA_BUF_LEN;
    const int off_gap = LCD_NOROT_VRES / flush_count;
    const int dma_pixels = LCD_DMA_BUF_LEN / 2;

    int y1 = 0, y2 = off_gap;

    xSemaphoreGive(s_flush_sema);

    for (int i = 0; i < flush_count; i++)
    {
        xSemaphoreTake(s_flush_sema, portMAX_DELAY);
        memcpy(s_dma_buf, map, LCD_DMA_BUF_LEN);
        esp_lcd_panel_draw_bitmap(s_panel, 0, y1, LCD_NOROT_HRES, y2, s_dma_buf);
        y1 += off_gap;
        y2 += off_gap;
        map += dma_pixels;
    }
    xSemaphoreTake(s_flush_sema, portMAX_DELAY);

    perf_log_event(PERF_SLOT_LVGL_FLUSH,
                   (uint32_t)((esp_timer_get_time() - start) / 1000));
    lv_disp_flush_ready(drv);
}

/* ── Backlight ── */
static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BL_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .gpio_num = LCD_PIN_BL,
        .duty = 0, /* 0 = full brightness (inverted) */
        .hpoint = 0,
    };
    ledc_channel_config(&ch);
}

/* ── Public API ── */

void display_init(void)
{
    backlight_init();

    s_flush_sema = xSemaphoreCreateBinary();

    /* Rotation buffer in PSRAM — holds the rotated frame */
    s_rotat_buf = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
                                   MALLOC_CAP_SPIRAM);
    assert(s_rotat_buf);

    /* DMA bounce buffer in internal DMA-capable RAM */
    s_dma_buf = heap_caps_malloc(LCD_DMA_BUF_LEN, MALLOC_CAP_DMA);
    assert(s_dma_buf);

    /* Double frame buffers in PSRAM */
    lv_color_t *buf1 = heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2)
    {
        ESP_LOGE(TAG, "Failed to allocate LVGL frame buffers in PSRAM");
        return;
    }

    /* ── Manual LCD reset (matching working project) ── */
    gpio_config_t rst_cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LCD_PIN_RST),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&rst_cfg);

    /* ── QSPI bus ── */
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_PIN_CLK,
        .data0_io_num = LCD_PIN_D0,
        .data1_io_num = LCD_PIN_D1,
        .data2_io_num = LCD_PIN_D2,
        .data3_io_num = LCD_PIN_D3,
        .max_transfer_sz = LCD_DMA_BUF_LEN,
    };
    spi_bus_initialize(LCD_QSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);

    /* ── Panel IO (SPI) — SPI mode 3, quad_mode, with DMA callback ── */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = -1,
        .spi_mode = 3,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = {.quad_mode = true},
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_QSPI_HOST,
                             &io_config, &io_handle);

    /* ── Panel driver (AXS15231B) ── */
    axs15231b_vendor_config_t vendor_cfg = {
        .flags.use_qspi_interface = 1,
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1, /* We do manual GPIO reset */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    esp_lcd_new_panel_axs15231b(io_handle, &panel_config, &s_panel);

    /* Manual reset pulse (matching working project timing) */
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    esp_lcd_panel_init(s_panel);

    /* ── LVGL display driver — landscape 640×172 with software rotation ── */
    lv_disp_draw_buf_init(&s_draw_buf, buf1, buf2, LCD_H_RES * LCD_V_RES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LCD_H_RES; /* 640 */
    s_disp_drv.ver_res = LCD_V_RES; /* 172 */
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.full_refresh = 1;
    s_disp_drv.user_data = s_panel;
    lv_disp_drv_register(&s_disp_drv);

    ESP_LOGI(TAG, "Display init: %dx%d landscape, double-buffered PSRAM",
             LCD_H_RES, LCD_V_RES);
}

void display_set_brightness(uint8_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

void display_keep_alive(void)
{
    /* no-op: reserved for future use */
}

void display_set_flip(bool flipped)
{
    s_flipped = flipped;
    /* Flip is handled in the software rotation loop in disp_flush_cb */
    ESP_LOGI(TAG, "Display flip: %s", flipped ? "FLIPPED" : "NORMAL");
}

bool display_get_flip(void)
{
    return s_flipped;
}
