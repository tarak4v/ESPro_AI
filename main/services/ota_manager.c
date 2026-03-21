/**
 * @file ota_manager.c
 * @brief OTA firmware update — HTTPS download to OTA partition, then reboot.
 */

#include "services/ota_manager.h"
#include "services/config_manager.h"
#include "core/event_bus.h"

#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ota_mgr";

#define OTA_BUF_SIZE 4096

static volatile ota_state_t s_state = OTA_STATE_IDLE;
static volatile int s_progress = 0;
static char s_ota_url[256] = {0};

void ota_manager_init(void)
{
    /* Mark current firmware as valid (rollback protection) */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK)
    {
        if (state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            ESP_LOGI(TAG, "Confirming new firmware as valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    ESP_LOGI(TAG, "OTA manager init (running: %s)", running->label);
}

static void ota_task(void *arg)
{
    ESP_LOGI(TAG, "OTA update starting: %s", s_ota_url);
    s_state = OTA_STATE_DOWNLOADING;
    s_progress = 0;

    esp_http_client_config_t http_cfg = {
        .url = s_ota_url,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client)
    {
        ESP_LOGE(TAG, "HTTP client init failed");
        s_state = OTA_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        s_state = OTA_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0)
    {
        ESP_LOGE(TAG, "Invalid content length: %d", content_length);
        esp_http_client_cleanup(client);
        s_state = OTA_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition)
    {
        ESP_LOGE(TAG, "No OTA partition found");
        esp_http_client_cleanup(client);
        s_state = OTA_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Writing to partition: %s (size: %d bytes)",
             update_partition->label, content_length);

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        s_state = OTA_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    char *buf = malloc(OTA_BUF_SIZE);
    if (!buf)
    {
        esp_ota_abort(ota_handle);
        esp_http_client_cleanup(client);
        s_state = OTA_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    int total_read = 0;
    while (1)
    {
        int read_len = esp_http_client_read(client, buf, OTA_BUF_SIZE);
        if (read_len < 0)
        {
            ESP_LOGE(TAG, "HTTP read error");
            break;
        }
        if (read_len == 0)
        {
            /* Check if we've finished */
            if (esp_http_client_is_complete_data_received(client))
                break;
            continue;
        }

        err = esp_ota_write(ota_handle, buf, read_len);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            break;
        }

        total_read += read_len;
        s_progress = (int)((int64_t)total_read * 100 / content_length);

        if (s_progress % 10 == 0)
        {
            ESP_LOGI(TAG, "OTA progress: %d%%", s_progress);
        }
    }

    free(buf);
    esp_http_client_cleanup(client);

    if (total_read < content_length)
    {
        ESP_LOGE(TAG, "Download incomplete: %d / %d", total_read, content_length);
        esp_ota_abort(ota_handle);
        s_state = OTA_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
        s_state = OTA_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(err));
        s_state = OTA_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    s_progress = 100;
    s_state = OTA_STATE_DONE;
    ESP_LOGI(TAG, "OTA success! Rebooting in 2s...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    vTaskDelete(NULL); /* unreachable */
}

bool ota_start_update(const char *url)
{
    if (!url || strlen(url) == 0)
    {
        ESP_LOGE(TAG, "No OTA URL provided");
        return false;
    }

    if (s_state == OTA_STATE_DOWNLOADING)
    {
        ESP_LOGW(TAG, "OTA already in progress");
        return false;
    }

    strncpy(s_ota_url, url, sizeof(s_ota_url) - 1);
    s_ota_url[sizeof(s_ota_url) - 1] = '\0';

    /* Save URL to config */
    config_set_str("ota_url", url);

    BaseType_t ret = xTaskCreate(ota_task, "ota_task", 8192, NULL, 3, NULL);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create OTA task");
        return false;
    }

    return true;
}

ota_state_t ota_get_state(void)
{
    return s_state;
}

int ota_get_progress(void)
{
    return s_progress;
}
