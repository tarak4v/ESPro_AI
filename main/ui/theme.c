/**
 * @file theme.c
 * @brief 5-palette theme engine — Dark, Light, Autumn, Spring, Monsoon.
 *
 * Each theme defines: bg, card, text, label, btn, accent.
 * Stored in config_manager ("theme_id"), applied on boot.
 */

#include "ui/theme.h"
#include "services/config_manager.h"
#include "esp_log.h"

static const char *TAG = "theme";

bool g_theme_dark = true;
uint32_t th_bg = 0x1C1C2E;
uint32_t th_card = 0x2A2A3E;
uint32_t th_text = 0xF0F0F0;
uint32_t th_label = 0xB0B0B0;
uint32_t th_btn = 0x3E3E50;
uint32_t th_accent = 0x4CAF50;

static theme_id_t s_current = THEME_DARK;

/* Palette definitions — 6 colors each */
typedef struct
{
    uint32_t bg;
    uint32_t card;
    uint32_t text;
    uint32_t label;
    uint32_t btn;
    uint32_t accent;
} palette_t;

static const palette_t s_palettes[THEME_COUNT] = {
    /* THEME_DARK — Soft dark with green accent */
    [THEME_DARK] = {
        .bg = 0x1C1C2E,
        .card = 0x2A2A3E,
        .text = 0xF0F0F0,
        .label = 0xB0B0B0,
        .btn = 0x3E3E50,
        .accent = 0x4CAF50,
    },
    /* THEME_LIGHT — Clean white with blue accent */
    [THEME_LIGHT] = {
        .bg = 0xF0F0F5,
        .card = 0xFFFFFF,
        .text = 0x1A1A2E,
        .label = 0x666666,
        .btn = 0xDDDDDD,
        .accent = 0x2196F3,
    },
    /* THEME_AUTUMN — Warm pastel earth tones, burnt orange accent */
    [THEME_AUTUMN] = {
        .bg = 0x2D1B0E,
        .card = 0x3E2723,
        .text = 0xFFE0B2,
        .label = 0xBCAAA4,
        .btn = 0x4E342E,
        .accent = 0xFF8A65,
    },
    /* THEME_SPRING — Soft pastel greens and pinks, fresh feel */
    [THEME_SPRING] = {
        .bg = 0xE8F5E9,
        .card = 0xF1F8E9,
        .text = 0x2E7D32,
        .label = 0x66BB6A,
        .btn = 0xC8E6C9,
        .accent = 0xF06292,
    },
    /* THEME_MONSOON — Cool blue-grey pastels, teal accent, rainy mood */
    [THEME_MONSOON] = {
        .bg = 0x1A2332,
        .card = 0x263238,
        .text = 0xB0BEC5,
        .label = 0x78909C,
        .btn = 0x37474F,
        .accent = 0x4DD0E1,
    },
};

static const char *s_theme_names[THEME_COUNT] = {
    [THEME_DARK] = "Dark",
    [THEME_LIGHT] = "Light",
    [THEME_AUTUMN] = "Autumn",
    [THEME_SPRING] = "Spring",
    [THEME_MONSOON] = "Monsoon",
};

static void apply_palette(const palette_t *p)
{
    th_bg = p->bg;
    th_card = p->card;
    th_text = p->text;
    th_label = p->label;
    th_btn = p->btn;
    th_accent = p->accent;
}

void theme_init(void)
{
    const device_config_t *cfg = config_get();
    s_current = cfg->theme_id;
    if (s_current >= THEME_COUNT)
        s_current = THEME_DARK;
    g_theme_dark = (s_current == THEME_DARK);
    apply_palette(&s_palettes[s_current]);
    ESP_LOGI(TAG, "Theme: %s", s_theme_names[s_current]);
}

void theme_apply(theme_id_t id)
{
    if (id >= THEME_COUNT)
        return;
    s_current = id;
    g_theme_dark = (id == THEME_DARK);
    apply_palette(&s_palettes[id]);
    config_set_u8("theme_id", (uint8_t)id);
    ESP_LOGI(TAG, "Theme changed: %s", s_theme_names[id]);
}

void theme_cycle(void)
{
    theme_id_t next = (s_current + 1) % THEME_COUNT;
    theme_apply(next);
}

theme_id_t theme_get_current(void)
{
    return s_current;
}

const char *theme_get_name(theme_id_t id)
{
    if (id < THEME_COUNT)
        return s_theme_names[id];
    return "Unknown";
}

/* Legacy API */
void theme_set_dark(bool dark)
{
    theme_apply(dark ? THEME_DARK : THEME_LIGHT);
}

void theme_toggle(void)
{
    theme_cycle();
}
