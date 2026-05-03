#include "sensor.h"
#include "i2c.h"
#include <stdint.h>
#include <stdio.h>
#include "i2c_driver.h"
#include "cmsis_os2.h"
#include "benchmark_sys.h"
#include "benchmark_cpu.h"

#define BME680_ADDR (0x76 << 1)

// calibration
static uint16_t dig_T1;
static int16_t  dig_T2;
static int8_t   dig_T3;

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

    // printf("T1=%u T2=%d T3=%d\n", dig_T1, dig_T2, dig_T3);

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
    int ret;

    ret = i2c_write(0xE0, 0xB6);
    if (ret != 0)
    {
        printf("reset failed: %d\r\n", ret);
        return -1;
    }

    osDelay(10);

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

int sensor_read(sensor_data_t *data)
{   
    uint8_t buf[3];
    int ret;

    // ------------------------
    // I2C write #1
    // ------------------------
    ret = i2c_write(0x75, 0x00);
    if (ret != 0)
    {
        printf("write 0x75 failed: %d\r\n", ret);
        return -1;
    }

    // ------------------------
    // I2C write #2
    // ------------------------
    
    ret = i2c_write(0x74, 0x25);
    if (ret != 0)
    {
        printf("write 0x74 failed: %d\r\n", ret);
        return -2;
    }


    // ------------------------
    // I2C read
    // ------------------------
    // read temperature registers
    ret = i2c_read_reg(BME680_ADDR, 0x22, buf, 3);
    if (ret != 0)
    {
        printf("read temp failed: %d\r\n", ret);
        return -3;
    }

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