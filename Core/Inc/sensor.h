#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <stdint.h>

typedef struct {
    int16_t temperature;   // °C * 100
    int16_t humidity;      // %RH * 100
    int32_t pressure;      // Pa
    int32_t gas;           // ohm (or raw)
} sensor_data_t;

typedef struct {
    uint32_t timestamp;
    sensor_data_t sensor;
} log_data_t;

int sensor_init(void);
int sensor_read_temperature(sensor_data_t *data);
int sensor_read_pressure(sensor_data_t *data);
#endif