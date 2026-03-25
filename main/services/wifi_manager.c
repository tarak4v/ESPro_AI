/**
 * @file wifi_manager.c
 * @brief WiFi APSTA mode — STA connects to configured AP, AP mode always on
 *        for config portal access. Supports scan, hidden SSID, NTP.
 */

#include "services/wifi_manager.h"
#include "services/config_manager.h"
#include "core/event_bus.h"
#include "hal/rtc.h"
#include "hal/audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void beep_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500)); /* let system settle */
    audio_beep(1000, 1000);
    vTaskDelete(NULL);
}

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_mac.h"
#include <string.h>
#include <time.h>

static const char *TAG = "wifi_mgr";
static volatile bool s_connected = false;
static char s_sta_ip[20] = "";

/* Scan cache */
static wifi_scan_entry_t s_scan_results[WIFI_SCAN_MAX_AP];
static uint16_t s_scan_count = 0;

static void ntp_sync_cb(struct timeval *tv)
{
    struct tm ti;
    time_t now = tv->tv_sec;
    localtime_r(&now, &ti);
    pcf85063_set_time(&ti);
    event_post(EVT_NTP_SYNCED, 0);
    ESP_LOGI(TAG, "NTP synced → RTC: %04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);
}

static void start_ntp(void)
{
    if (esp_sntp_enabled())
        return;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(ntp_sync_cb);
    esp_sntp_init();
    setenv("TZ", "IST-5:30", 1);
    tzset();
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT)
    {
        if (id == WIFI_EVENT_STA_START)
        {
            const device_config_t *cfg = config_get();
            if (strlen(cfg->wifi_ssid) > 0)
            {
                esp_wifi_connect();
            }
        }
        else if (id == WIFI_EVENT_STA_DISCONNECTED)
        {
            s_connected = false;
            s_sta_ip[0] = '\0';
            event_post(EVT_WIFI_DISCONNECTED, 0);
            ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
            const device_config_t *cfg = config_get();
            if (strlen(cfg->wifi_ssid) > 0)
            {
                esp_wifi_connect();
            }
        }
        else if (id == WIFI_EVENT_SCAN_DONE)
        {
            wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * WIFI_SCAN_MAX_AP);
            if (ap_list)
            {
                uint16_t count = WIFI_SCAN_MAX_AP;
                esp_wifi_scan_get_ap_records(&count, ap_list);
                s_scan_count = (count > WIFI_SCAN_MAX_AP) ? WIFI_SCAN_MAX_AP : count;
                for (uint16_t i = 0; i < s_scan_count; i++)
                {
                    strncpy(s_scan_results[i].ssid, (const char *)ap_list[i].ssid,
                            sizeof(s_scan_results[i].ssid) - 1);
                    s_scan_results[i].ssid[sizeof(s_scan_results[i].ssid) - 1] = '\0';
                    s_scan_results[i].rssi = ap_list[i].rssi;
                    s_scan_results[i].authmode = (uint8_t)ap_list[i].authmode;
                }
                free(ap_list);
                ESP_LOGI(TAG, "Scan done: %u APs found", s_scan_count);
            }
            else
            {
                ESP_LOGE(TAG, "Scan: alloc failed");
            }
        }
        else if (id == WIFI_EVENT_AP_STACONNECTED)
        {
            ESP_LOGI(TAG, "AP: client connected");
        }
        else if (id == WIFI_EVENT_AP_STADISCONNECTED)
        {
            ESP_LOGI(TAG, "AP: client disconnected");
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *evt = data;
        s_connected = true;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, "WiFi connected: %s", s_sta_ip);
        event_post(EVT_WIFI_CONNECTED, 0);
        start_ntp();
        /* Beep disabled — audio playback disrupts display backlight */
        // xTaskCreate(beep_task, "beep", 4096, NULL, 2, NULL);
    }
}

void wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create both STA and AP netifs */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    esp_event_handler_instance_t wifi_inst, ip_inst;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, &wifi_inst);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, &ip_inst);

    /* APSTA mode — AP always on, STA connects if configured */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    /* Configure AP */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap_config = {0};
    snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid),
             "%s%02X%02X", WIFI_AP_SSID_PREFIX, mac[4], mac[5]);
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    strncpy((char *)ap_config.ap.password, WIFI_AP_PASS, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.channel = WIFI_AP_CHANNEL;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = WIFI_AP_MAX_CONN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    /* Configure STA from config_manager */
    const device_config_t *cfg = config_get();
    wifi_config_t sta_config = {0};
    if (strlen(cfg->wifi_ssid) > 0)
    {
        strncpy((char *)sta_config.sta.ssid, cfg->wifi_ssid, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char *)sta_config.sta.password, cfg->wifi_pass, sizeof(sta_config.sta.password) - 1);
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        if (cfg->wifi_hidden)
        {
            sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        }
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi APSTA started (AP: %s, STA: %s)",
             (char *)ap_config.ap.ssid,
             strlen(cfg->wifi_ssid) > 0 ? cfg->wifi_ssid : "(none)");
}

bool wifi_is_connected(void)
{
    return s_connected;
}

bool wifi_scan_start(void)
{
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 100, .max = 300},
    };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

uint16_t wifi_scan_get_results(wifi_scan_entry_t *out, uint16_t max_count)
{
    uint16_t n = (s_scan_count < max_count) ? s_scan_count : max_count;
    if (n > 0 && out)
    {
        memcpy(out, s_scan_results, n * sizeof(wifi_scan_entry_t));
    }
    return n;
}

uint16_t wifi_scan_get_count(void)
{
    return s_scan_count;
}

void wifi_connect_to(const char *ssid, const char *pass, bool hidden)
{
    if (!ssid)
        return;

    /* Persist to config */
    config_set_str("wifi_ssid", ssid);
    config_set_str("wifi_pass", pass ? pass : "");
    config_set_u8("wifi_hidden", hidden ? 1 : 0);

    /* Apply new STA config */
    esp_wifi_disconnect();

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    if (pass)
    {
        strncpy((char *)sta_config.sta.password, pass, sizeof(sta_config.sta.password) - 1);
    }
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    if (hidden)
    {
        sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    }

    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_connect();
    ESP_LOGI(TAG, "Connecting to: %s (hidden=%d)", ssid, hidden);
}

const char *wifi_get_ap_ip(void)
{
    return "192.168.4.1";
}

const char *wifi_get_sta_ip(void)
{
    return s_sta_ip;
}
