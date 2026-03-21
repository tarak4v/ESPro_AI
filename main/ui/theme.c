/**
 * @file theme.c
 * @brief Theme palette management.
 */

#include "ui/theme.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "theme";

bool     g_theme_dark = true;
uint32_t th_bg     = 0x0A0A14;
uint32_t th_card   = 0x1A1A2E;
uint32_t th_text   = 0xFFFFFF;
uint32_t th_label  = 0x888888;
uint32_t th_btn    = 0x333333;
uint32_t th_accent = 0x4CAF50;

void theme_init(void)
{
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READONLY, &h) == ESP_OK) {
        uint8_t dark = 1;
        nvs_get_u8(h, "theme_dark", &dark);
        nvs_close(h);
        g_theme_dark = (dark != 0);
    }
    theme_set_dark(g_theme_dark);
}

void theme_set_dark(bool dark)
{
    g_theme_dark = dark;
    if (dark) {
        th_bg     = 0x0A0A14;
        th_card   = 0x1A1A2E;
        th_text   = 0xFFFFFF;
        th_label  = 0x888888;
        th_btn    = 0x333333;
        th_accent = 0x4CAF50;
    } else {
        th_bg     = 0xF0F0F5;
        th_card   = 0xFFFFFF;
        th_text   = 0x1A1A2E;
        th_label  = 0x666666;
        th_btn    = 0xDDDDDD;
        th_accent = 0x2196F3;
    }
    ESP_LOGI(TAG, "Theme: %s", dark ? "dark" : "light");
}

void theme_toggle(void)
{
    theme_set_dark(!g_theme_dark);
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "theme_dark", g_theme_dark ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
}
