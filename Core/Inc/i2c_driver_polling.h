#ifndef I2C_DRIVER_POLLING_H
#define I2C_DRIVER_POLLING_H

#include <stdint.h>

void i2c_init_polling(void);

int i2c_write_reg_polling(uint8_t dev, uint8_t reg, uint8_t data);
int i2c_read_reg_polling(uint8_t dev, uint8_t reg, uint8_t *buf, int len);

#endif