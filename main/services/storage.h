/**
 * @file storage.h
 * @brief Unified storage — LittleFS + NVS helpers.
 */
#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Mount LittleFS partition. */
void storage_init(void);

/** Read file contents into heap-allocated buffer (caller must free). */
char *storage_read_file(const char *path, size_t *out_len);

/** Write data to a file. */
bool storage_write_file(const char *path, const void *data, size_t len);

/** Append data to a file. */
bool storage_append_file(const char *path, const void *data, size_t len);

/** Check if file exists. */
bool storage_file_exists(const char *path);

/** NVS read/write helpers. */
bool storage_nvs_get_u8(const char *ns, const char *key, uint8_t *val);
bool storage_nvs_set_u8(const char *ns, const char *key, uint8_t val);
bool storage_nvs_get_str(const char *ns, const char *key, char *buf, size_t len);
bool storage_nvs_set_str(const char *ns, const char *key, const char *val);

#endif /* STORAGE_H */
