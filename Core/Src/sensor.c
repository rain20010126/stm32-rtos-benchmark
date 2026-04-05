#include "sensor.h"
#include "i2c.h"
#include <stdint.h>
#include <stdio.h>
#include "i2c_driver.h"

#define BME680_ADDR (0x76 << 1)

// calibration
static uint16_t dig_T1;
static int16_t  dig_T2;
static int8_t   dig_T3;

static uint16_t dig_P1;
static int16_t  dig_P2;
static int8_t   dig_P3;
static int16_t  dig_P4;
static int16_t  dig_P5;
static int8_t   dig_P6;
static int8_t   dig_P7;
static int16_t  dig_P8;
static int16_t  dig_P9;
static uint8_t  dig_P10;

static int32_t t_fine;

// ========================
// I2C write
// ========================

static int i2c_write(uint8_t reg, uint8_t val)
{
    return i2c_write_reg(BME680_ADDR, reg, val);
}

// ========================
// Read calibration (FINAL)
// ========================

static int read_calibration(void)
{
    uint8_t calib1[25];
    uint8_t calib2[16];

    if (i2c_read_reg(BME680_ADDR, 0x89, calib1, 25) != 0)
    {
        printf("read1 fail\n");
        return -1;
    }

    if (i2c_read_reg(BME680_ADDR, 0xE1, calib2, 16) != 0)
    {
        printf("read2 fail\n");
        return -1;
    }

    // correct mapping + little endian
    dig_T1 = (uint16_t)(calib2[8] | (calib2[9] << 8));
    dig_T2 = (int16_t)(((int16_t)calib1[2] << 8) | calib1[1]);
    dig_T3 = (int8_t)calib1[2];

    // ===== humidity calibration =====
    
    dig_P1 = (uint16_t)(((uint16_t)calib1[6] << 8) | calib1[5]);
    dig_P2 = (int16_t)(((int16_t)calib1[8] << 8) | calib1[7]);
    dig_P3 = (int8_t)calib1[9];
    dig_P4 = (int16_t)(((int16_t)calib1[12] << 8) | calib1[11]);
    dig_P5 = (int16_t)(((int16_t)calib1[14] << 8) | calib1[13]);
    dig_P6 = (int8_t)calib1[16];
    dig_P7 = (int8_t)calib1[15];
    dig_P8 = (int16_t)(((int16_t)calib1[20] << 8) | calib1[19]);
    dig_P9 = (int16_t)(((int16_t)calib1[22] << 8) | calib1[21]);
    dig_P10 = (uint8_t)calib1[23];

    return 0;
}

// ========================
// Init
// ========================

int sensor_init(void)
{
    uint8_t id;

    // printf("sensor_init start\n");

    // soft reset
    if (i2c_write(0xE0, 0xB6) != HAL_OK) {
        printf("reset failed\n");
        return -1;
    }

    HAL_Delay(10);

    // read chip id
    if (i2c_read_reg(BME680_ADDR, 0xD0, &id, 1) != 0) {
        printf("read chip id failed\n");
        return -1;
    }

    // printf("chip id = 0x%02X\n", id);

    if (id != 0x61) {
        printf("wrong chip id\n");
        return -1;
    }

    if (read_calibration() != 0) {
        printf("calibration failed\n");
        return -1;
    }

    // printf("sensor init OK\n");
    return 0;
}

// ========================
// Read temperature
// ========================

int sensor_read_temperature(sensor_data_t *data)
{
    uint8_t buf[3];

    // config register (recommended)
    if (i2c_write(0x75, 0x00) != HAL_OK)
        return -1;

    // ctrl_meas:
    // osrs_t = x1 (001)
    // mode = forced (01)
    // => 001 00 01 = 0x25
    if (i2c_write(0x74, 0x25) != HAL_OK)
        return -1;

    // read temperature registers
    if (i2c_read_reg(BME680_ADDR, 0x22, buf, 3) != 0)
        return -1;

    int32_t adc_T =
        ((int32_t)buf[0] << 12) |
        ((int32_t)buf[1] << 4) |
        ((int32_t)buf[2] >> 4);

    // compensation (official formula)
    int32_t var1, var2, var3;

    var1 = (((int32_t)adc_T >> 3) - ((int32_t)dig_T1 << 1));
    var2 = (var1 * (int32_t)dig_T2) >> 11;
    var3 = (((var1 >> 1) * (var1 >> 1)) >> 12);
    var3 = (var3 * ((int32_t)dig_T3 << 4)) >> 14;

    t_fine = var2 + var3;

    int32_t temp_comp = ((t_fine * 5) + 128) >> 8;

    data->temperature = temp_comp;

    return 0;
}

int32_t BME680_compensate_P(uint32_t adc_P)
{
  int32_t var1 = 0, var2 = 0, var3 = 0, var4 = 0, P = 0;
  var1 = (((int32_t) t_fine) >> 1) - 64000;
  var2 = ((((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t) dig_P6) >> 2;
  var2 = var2 + ((var1 * (int32_t)dig_P5) << 1);
  var2 = (var2 >> 2) + ((int32_t) dig_P4 << 16);
  var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) * ((int32_t) dig_P3 << 5)) >> 3) + (((int32_t) dig_P2 * var1) >> 1);
  var1 = var1 >> 18;
  var1 = ((32768 + var1) * (int32_t) dig_P1) >> 15;
  P = 1048576 - adc_P;
  P = (int32_t)((P - (var2 >> 12)) * ((uint32_t) 3125));
  var4 = (1 << 31);
  
  if(P >= var4)
    P = (( P / (uint32_t) var1) << 1);
  else
    P = ((P << 1) / (uint32_t) var1);
    
  var1 = ((int32_t) dig_P9 * (int32_t) (((P >> 3) * (P >> 3)) >> 13)) >> 12;
  var2 = ((int32_t)(P >> 2) * (int32_t) dig_P8) >> 13;
  var3 = ((int32_t)(P >> 8) * (int32_t)(P >> 8) * (int32_t)(P >> 8) * (int32_t)dig_P10) >> 17;
  P = (int32_t)(P) + ((var1 + var2 + var3 + ((int32_t)dig_P7 << 7)) >> 4);
  
  return P;
}

int sensor_read_pressure(sensor_data_t *data)
{
    uint8_t buf[3];

    if (i2c_write(0x75, 0x00) != 0)
        return -1;

    if (i2c_write(0x74, 0x25) != 0)
        return -1;

    if (i2c_read_reg(BME680_ADDR, 0x1F, buf, 3) != 0)
        return -1;

    int32_t adc_P =
        ((int32_t)buf[0] << 12) |
        ((int32_t)buf[1] << 4) |
        ((int32_t)buf[2] >> 4);

    // printf("adc_P = %ld\n", adc_P);

    int32_t pressure = BME680_compensate_P(adc_P);

    data->pressure = pressure;

    // printf("P = %ld Pa (%.2f hPa)\n", pressure, pressure / 100.0f);

    return 0;
}