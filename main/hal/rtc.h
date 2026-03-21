/**
 * @file rtc.h
 * @brief RTC HAL — PCF85063 real-time clock (BCD registers).
 */
#ifndef HAL_RTC_H
#define HAL_RTC_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

void rtc_init(void);
bool rtc_get_time(struct tm *t);
bool rtc_set_time(const struct tm *t);

#endif /* HAL_RTC_H */
