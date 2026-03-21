/**
 * @file storage.c
 * @brief Unified storage — LittleFS mount + file ops + NVS helpers.
 */

#include "services/storage.h"
#include "esp_littlefs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "storage";

void storage_init(void)
{
    esp_vfs_littlefs_conf_t cfg = {
        .base_path       = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&cfg);
    if (ret == ESP_OK) {
        size_t total, used;
        esp_littlefs_info("storage", &total, &used);
        ESP_LOGI(TAG, "LittleFS: %u KB used / %u KB total",
                 (unsigned)(used / 1024), (unsigned)(total / 1024));
    } else {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
    }
}

char *storage_read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
    if (buf) {
        fread(buf, 1, len, f);
        buf[len] = '\0';
        if (out_len) *out_len = len;
    }
    fclose(f);
    return buf;
}

bool storage_write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fwrite(data, 1, len, f);
    fclose(f);
    return true;
}

bool storage_append_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "a");
    if (!f) return false;
    fwrite(data, 1, len, f);
    fclose(f);
    return true;
}

bool storage_file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0);
}

bool storage_nvs_get_u8(const char *ns, const char *key, uint8_t *val)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t ret = nvs_get_u8(h, key, val);
    nvs_close(h);
    return (ret == ESP_OK);
}

bool storage_nvs_set_u8(const char *ns, const char *key, uint8_t val)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_set_u8(h, key, val);
    nvs_commit(h);
    nvs_close(h);
    return true;
}

bool storage_nvs_get_str(const char *ns, const char *key, char *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t ret = nvs_get_str(h, key, buf, &len);
    nvs_close(h);
    return (ret == ESP_OK);
}

bool storage_nvs_set_str(const char *ns, const char *key, const char *val)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_set_str(h, key, val);
    nvs_commit(h);
    nvs_close(h);
    return true;
}
