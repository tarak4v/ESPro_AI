/**
 * @file weather.h
 * @brief Periodic weather fetch from OpenWeatherMap.
 */
#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>

typedef struct
{
    float temp;
    int humidity;
    float wind_speed;
    int uv_index;
    char description[32];
    bool valid;
} weather_data_t;

void weather_init(void);
weather_data_t weather_get(void);

#endif /* WEATHER_H */
