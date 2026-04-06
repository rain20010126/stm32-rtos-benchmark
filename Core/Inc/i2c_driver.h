#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include <stdint.h>

/**
 * @file i2c_driver.h
 * @brief Interrupt-driven I2C driver (STM32F4)
 *
 * Features:
 *   - Register-level implementation (no HAL)
 *   - Interrupt-driven state machine
 *   - Blocking and non-blocking APIs
 *   - Callback support for async operations
 */

// ======================================================
// Callback type
// ======================================================

/**
 * @brief I2C transfer completion callback
 */
typedef void (*i2c_callback_t)(void);

// ======================================================
// Initialization
// ======================================================

/**
 * @brief Initialize I2C peripheral and interrupts
 */
void enable_interrupt(void);

// ======================================================
// Blocking API
// ======================================================

/**
 * @brief Write one byte to a device register (blocking)
 *
 * @param dev  Device address (7-bit << 1)
 * @param reg  Register address
 * @param val  Data to write
 * @return 0 on success, -1 on failure
 */
int i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t val);

/**
 * @brief Read bytes from a device register (blocking)
 *
 * @param dev  Device address (7-bit << 1)
 * @param reg  Register address
 * @param buf  Output buffer
 * @param len  Number of bytes to read
 * @return 0 on success, -1 on failure
 */
int i2c_read_reg(uint8_t dev, uint8_t reg, uint8_t *buf, int len);

// ======================================================
// Non-blocking API
// ======================================================

/**
 * @brief Write one byte to a device register (non-blocking)
 *
 * @param dev  Device address (7-bit << 1)
 * @param reg  Register address
 * @param val  Data to write
 * @param cb   Callback on completion (can be NULL)
 * @return 0 on success, -1 if busy
 */
int i2c_write_reg_async(uint8_t dev, uint8_t reg, uint8_t val, i2c_callback_t cb);

/**
 * @brief Read bytes from a device register (non-blocking)
 *
 * @param dev  Device address (7-bit << 1)
 * @param reg  Register address
 * @param buf  Output buffer
 * @param len  Number of bytes to read
 * @param cb   Callback on completion (can be NULL)
 * @return 0 on success, -1 if busy
 */
int i2c_read_reg_async(uint8_t dev, uint8_t reg, uint8_t *buf, int len, i2c_callback_t cb);

// ======================================================
// Status
// ======================================================

/**
 * @brief Check if I2C bus is busy
 *
 * @return 1 if busy, 0 if idle
 */
int i2c_is_busy(void);

#endif