/**
 * @file rtc.c
 * @brief RTC HAL — PCF85063 BCD time read/write.
 */

#include "hal/rtc.h"
#include "hw_config.h"
#include "i2c_bsp.h"
#include "esp_log.h"

static const char *TAG = "rtc";

static i2c_master_dev_handle_t s_rtc_dev = NULL;

#define RTC_REG_SEC 0x04
#define bcd2dec(v) (((v) >> 4) * 10 + ((v) & 0x0F))
#define dec2bcd(v) ((((v) / 10) << 4) | ((v) % 10))

void pcf85063_init(void)
{
    extern i2c_master_dev_handle_t rtc_dev_handle;
    s_rtc_dev = rtc_dev_handle;
    ESP_LOGI(TAG, "PCF85063 RTC initialised");
}

bool pcf85063_get_time(struct tm *t)
{
    uint8_t buf[7];
    if (i2c_read_buff(s_rtc_dev, RTC_REG_SEC, buf, 7) != ESP_OK)
        return false;

    t->tm_sec = bcd2dec(buf[0] & 0x7F);
    t->tm_min = bcd2dec(buf[1] & 0x7F);
    t->tm_hour = bcd2dec(buf[2] & 0x3F);
    t->tm_mday = bcd2dec(buf[3] & 0x3F);
    t->tm_wday = buf[4] & 0x07;
    t->tm_mon = bcd2dec(buf[5] & 0x1F) - 1;
    t->tm_year = bcd2dec(buf[6]) + 100; /* Years since 1900 */

    return true;
}

bool pcf85063_set_time(const struct tm *t)
{
    uint8_t buf[7] = {
        dec2bcd(t->tm_sec),
        dec2bcd(t->tm_min),
        dec2bcd(t->tm_hour),
        dec2bcd(t->tm_mday),
        (uint8_t)t->tm_wday,
        dec2bcd(t->tm_mon + 1),
        dec2bcd(t->tm_year - 100),
    };
    i2c_writr_buff(s_rtc_dev, RTC_REG_SEC, buf, 7);
    ESP_LOGI(TAG, "RTC set: %04d-%02d-%02d %02d:%02d:%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    return true;
}
