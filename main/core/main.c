/**
 * @file main.c
 * @brief ESPro AI — app_main entry point.
 *
 * Initialises hardware, subsystems, and starts the OS kernel tasks.
 * Boot sequence:
 *   1. NVS + LittleFS storage + config manager
 *   2. Display + backlight
 *   3. Touch controller
 *   4. I2C buses + sensors (IMU, RTC)
 *   5. Audio subsystem (I2S + codecs)
 *   6. WiFi (APSTA mode) + webserver
 *   7. LVGL init + theme (config-driven)
 *   8. Event bus + perf monitor
 *   9. AI subsystems
 *  10. OTA manager + OS kernel → start all tasks
 */

#include "hw_config.h"
#include "os_kernel.h"
#include "event_bus.h"
#include "perf_monitor.h"
#include "lvgl.h"

/* HAL */
#include "hal/display.h"
#include "hal/touch.h"
#include "hal/audio.h"
#include "hal/imu.h"
#include "hal/rtc.h"

/* UI */
#include "ui/ui_manager.h"
#include "ui/theme.h"

/* AI */
#include "ai/agent_orchestrator.h"
#include "ai/voice_pipeline.h"
#include "ai/proactive_agent.h"

/* Services */
#include "services/wifi_manager.h"
#include "services/storage.h"
#include "services/config_manager.h"
#include "services/webserver.h"
#include "services/ota_manager.h"
#include "services/weather.h"
#include "services/power_manager.h"
#include "services/mqtt_service.h"
#include "services/meeting_service.h"

#include "i2c_bsp.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"

static const char *TAG = "main";

void app_main(void)
{
    int64_t boot_start = esp_timer_get_time();

    ESP_LOGI(TAG, "╔══════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ESPro AI — Multi-Modal Watch   ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════╝");

    /* 1. Storage + Config */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }
    storage_init();
    config_manager_init();
    const device_config_t *cfg = config_get();
    ESP_LOGI(TAG, "[1/10] Storage + config ready (device: %s)", cfg->device_name);

    /* 2. Display (lv_init must precede LVGL driver registration) */
    lv_init();
    display_init();
    ESP_LOGI(TAG, "[2/10] Display ready");

    /* 3. I2C buses + Touch */
    i2c_master_Init();
    touch_init();
    ESP_LOGI(TAG, "[3/10] I2C + Touch ready");

    /* 4. Sensors */
    imu_init();
    pcf85063_init();
    ESP_LOGI(TAG, "[4/10] Sensors ready (IMU + RTC)");

    /* 5. Audio */
    audio_init();
    ESP_LOGI(TAG, "[5/10] Audio ready");

    /* 6. WiFi (APSTA) + services */
    wifi_manager_init();
    weather_init();
    mqtt_service_init();
    meeting_service_init();
    ESP_LOGI(TAG, "[6/10] WiFi APSTA + weather + MQTT + meeting ready (portal on-demand)");

    /* 7. LVGL + Theme (config-driven) */
    ui_manager_init();
    theme_init();
    ESP_LOGI(TAG, "[7/10] LVGL + theme ready (%s)", theme_get_name(cfg->theme_id));

    /* 8. Core OS services */
    event_bus_init();
    perf_monitor_init();
    ESP_LOGI(TAG, "[8/10] Event bus + perf monitor ready");

    /* 9. AI */
    voice_pipeline_init();
    agent_orchestrator_init();
    proactive_agent_register();
    power_manager_init();
    ESP_LOGI(TAG, "[9/10] AI + agents + power manager ready (STT: %s, LLM: %s)",
             config_get_provider_name(cfg->stt_provider),
             config_get_provider_name(cfg->llm_provider));

    /* 10. OTA + Start tasks */
    ota_manager_init();
    os_kernel_start_tasks();

    int64_t boot_ms = (esp_timer_get_time() - boot_start) / 1000;
    ESP_LOGI(TAG, "[10/10] Boot complete in %lld ms", boot_ms);
    perf_log_event("boot_time", (uint32_t)boot_ms);
}
