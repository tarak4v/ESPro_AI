/**
 * @file rtc.h
 * @brief RTC HAL — PCF85063 real-time clock (BCD registers).
 */
#ifndef HAL_RTC_H
#define HAL_RTC_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

void pcf85063_init(void);
bool pcf85063_get_time(struct tm *t);
bool pcf85063_set_time(const struct tm *t);

/* Convenience aliases */
#define rtc_ext_init pcf85063_init
#define rtc_ext_get_time pcf85063_get_time
#define rtc_ext_set_time pcf85063_set_time

#endif /* HAL_RTC_H */
