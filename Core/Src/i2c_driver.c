/**
 * @file i2c_driver.c
 * @brief Interrupt-driven I2C driver (STM32F4, register-level)
 *
 * Design:
 *   - Interrupt-driven state machine
 *   - Async transaction + blocking wrapper
 *   - FreeRTOS semaphore for synchronization
 *
 * Key idea:
 *   Convert async interrupt I2C into blocking API without busy waiting
 */

#include "i2c_driver.h"
#include "stm32f4xx.h"
#include <stdio.h>
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "semphr.h"

// ======================================================
// RTOS Synchronization
// ======================================================

/**
 * @brief Binary semaphore for transaction completion
 *
 * Used to block task until I2C transaction is finished (IRQ -> give)
 */
static SemaphoreHandle_t i2cSem;

void i2c_os_init(void)
{
    i2cSem = xSemaphoreCreateBinary();
}

// ======================================================
// State Machine Definition
// ======================================================

/**
 * @brief I2C transaction states
 *
 * Represents each stage of I2C communication.
 * Driven by hardware flags (SR1).
 */
typedef enum {
    I2C_IDLE,
    I2C_START,
    I2C_ADDR_W,
    I2C_REG,
    I2C_WRITE_DATA,
    I2C_RESTART,
    I2C_ADDR_R,
    I2C_ADDR_R_WAIT,
    I2C_READ,
    I2C_STOP,
    I2C_DONE,
    I2C_ERROR
} i2c_state_t;

// ======================================================
// Transaction Context
// ======================================================

/**
 * @brief I2C transaction context
 *
 * Stores all runtime information for one I2C transfer.
 */
typedef struct {
    uint8_t dev;       // device address
    uint8_t reg;       // register address

    uint8_t *buf;      // data buffer
    uint8_t tx_buf[4]; // temp buffer for write

    int len;           // total bytes
    int idx;           // current index

    int is_read;       // read or write

    i2c_state_t state; // current state
    int busy;          // transaction in progress flag
} i2c_ctx_t;


// ======================================================
// Global Context
// ======================================================

static i2c_ctx_t i2c;
static i2c_callback_t i2c_cb = 0;

// ======================================================
// Completion Callback (ISR context)
// ======================================================

/**
 * @brief Called when I2C transaction finishes (inside IRQ)
 *
 * - Releases semaphore to wake blocked task
 * - Uses FromISR API (mandatory in interrupt context)
 */
static void i2c_done_cb(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Notify task that transaction is done
    xSemaphoreGiveFromISR(i2cSem, &xHigherPriorityTaskWoken);

    // Trigger context switch if needed
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


// ======================================================
// Interrupt Enable
// ======================================================

/**
 * @brief Enable I2C interrupt (event + error)
 *
 * Must ensure bus is idle before enabling.
 */
void enable_interrupt(void)
{
    // Wait until bus is free
    while (I2C1->SR2 & I2C_SR2_BUSY);

    // Enable I2C interrupt sources
    I2C1->CR2 |= (I2C_CR2_ITEVTEN |
                  I2C_CR2_ITBUFEN |
                  I2C_CR2_ITERREN);

    // Set IRQ priority (lower than kernel)
    NVIC_SetPriority(I2C1_EV_IRQn, 6);
    NVIC_SetPriority(I2C1_ER_IRQn, 6);

    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_EnableIRQ(I2C1_ER_IRQn);
}

// ======================================================
// Blocking API (Wrapper)
// ======================================================

/**
 * @brief Blocking read register
 *
 * Internally uses async API + semaphore synchronization.
 *
 * Flow:
 *   1. Clear old semaphore token
 *   2. Start async transaction
 *   3. Block until ISR signals completion
 */
int i2c_read_reg(uint8_t dev, uint8_t reg, uint8_t *buf, int len)
{
    i2c.is_read = 1;

    // Clear stale semaphore (important!)
    while (uxSemaphoreGetCount(i2cSem) > 0)
    {
        xSemaphoreTake(i2cSem, 0);
    }

    // Start async transaction
    if (i2c_read_reg_async(dev, reg, buf, len, i2c_done_cb) != 0)
        return -1;

    // Block until ISR releases semaphore
    xSemaphoreTake(i2cSem, portMAX_DELAY);

    return 0;
}

/**
 * @brief Blocking write register
 *
 * Same concept as read: async + semaphore
 */
int i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t val)
{
    i2c.is_read = 0;

    // Clear old semaphore token
    while (uxSemaphoreGetCount(i2cSem) > 0)
        xSemaphoreTake(i2cSem, 0);

    // Start async write
    if (i2c_write_reg_async(dev, reg, val, i2c_done_cb) != 0)
        return -1;

    // Wait for completion
    xSemaphoreTake(i2cSem, portMAX_DELAY);

    return 0;
}


// ======================================================
// Utility
// ======================================================

/**
 * @brief Check if I2C is busy
 */
int i2c_is_busy(void)
{
    return i2c.busy;
}

// ======================================================
// Async API
// ======================================================

/**
 * @brief Start async write transaction
 *
 * Initializes context and triggers START condition.
 */
int i2c_write_reg_async(uint8_t dev, uint8_t reg, uint8_t val, i2c_callback_t cb)
{
    if (i2c.busy) return -1;

    // Setup transaction
    i2c.dev = dev;
    i2c.reg = reg;
    i2c.tx_buf[0] = val;
    i2c.buf = i2c.tx_buf;
    i2c.len = 1;
    i2c.idx = 0;
    i2c_cb = cb;
    i2c.state = I2C_START;
    i2c.busy = 1;

    // Trigger START condition (hardware)
    I2C1->CR1 |= I2C_CR1_START;

    return 0;
}


/**
 * @brief Start an asynchronous I2C register read transaction (interrupt-driven)
 *
 * This function initializes the I2C transaction context and triggers a START condition.
 * The actual transfer is handled step-by-step inside the IRQ handler using a state machine.
 *
 * Flow:
 *   START → ADDR_W → REG → RESTART → ADDR_R → READ → DONE
 *
 * @param dev  I2C device address (7-bit address already shifted left by 1)
 * @param reg  Register address to read from
 * @param buf  Output buffer to store received data
 * @param len  Number of bytes to read
 * @param cb   Callback function invoked when transaction completes (can be NULL)
 *
 * @return 0 on success, -1 if I2C bus is busy
 */


int i2c_read_reg_async(uint8_t dev, uint8_t reg, uint8_t *buf, int len, i2c_callback_t cb)
{
    // Reject request if a transaction is already in progress
    if (i2c.busy)
        return -1;

    // Initialize transaction context
    i2c.dev   = dev;     // Device address (write mode initially)
    i2c.reg   = reg;     // Register address to access
    i2c.buf   = buf;     // Buffer to store incoming data
    i2c.len   = len;     // Total number of bytes to read
    i2c.idx   = 0;       // Current index in buffer
    i2c_cb    = cb;      // Completion callback
    i2c.state = I2C_START;
    i2c.busy  = 1;

    // Generate START condition
    // This will trigger SB interrupt and enter IRQ state machine
    I2C1->CR1 |= I2C_CR1_START;

    return 0;
}

/**
 * @brief I2C event IRQ handler (state machine driven)
 *
 * This handler is triggered by I2C hardware events (SB, ADDR, TXE, RXNE, BTF).
 * It advances the I2C transaction step-by-step using a state machine.
 *
 * Design:
 *   - Each state represents a stage of the I2C transaction
 *   - SR1 flags indicate when hardware is ready for the next action
 *   - The handler progresses only when the expected flag is set
 *
 * Key responsibilities:
 *   - Send device address (write/read)
 *   - Send register address
 *   - Handle repeated START for read operations
 *   - Read incoming data bytes
 *   - Control ACK/NACK timing
 *   - Generate STOP condition
 *   - Invoke callback when transaction completes
 *
 * Note:
 *   This function must be called inside I2C1_EV_IRQHandler().
 */

void i2c_ev_irq_handler(void)
{
    uint32_t sr1 = I2C1->SR1;

    // Debug (optional)
    // printf("[IRQ] state=%d SR1=0x%04lX\n\r", i2c.state, sr1);

    switch (i2c.state)
    {
    // ======================================================
    // STATE: START
    // Wait for START condition (SB = 1), then send device addr (write)
    // ======================================================
    case I2C_START:
        if (sr1 & I2C_SR1_SB)
        {
            I2C1->DR = i2c.dev;      // Send device address (write mode)
            i2c.state = I2C_ADDR_W;
        }
        break;

    // ======================================================
    // STATE: ADDR (WRITE)
    // Wait for address acknowledged (ADDR = 1)
    // Must clear ADDR by reading SR1 then SR2
    // ======================================================
    case I2C_ADDR_W:
        if (sr1 & I2C_SR1_ADDR)
        {
            volatile uint32_t tmp;
            tmp = I2C1->SR1;
            tmp = I2C1->SR2;
            (void)tmp;               // Clear ADDR flag

            i2c.state = I2C_REG;
        }
        break;

    // ======================================================
    // STATE: SEND REGISTER
    // Wait for TXE (data register empty), then send register address
    // ======================================================
    case I2C_REG:
        if (sr1 & I2C_SR1_TXE)
        {
            I2C1->DR = i2c.reg;      // Send register address

            if (i2c.is_read)
                i2c.state = I2C_RESTART;     // Read flow
            else
                i2c.state = I2C_WRITE_DATA;  // Write flow
        }
        break;

    // ======================================================
    // STATE: WRITE DATA
    // Wait for BTF (byte transfer finished), then send next byte
    // ======================================================
    case I2C_WRITE_DATA:
        if (sr1 & I2C_SR1_BTF)
        {
            I2C1->DR = i2c.buf[i2c.idx++];

            if (i2c.idx >= i2c.len)
                i2c.state = I2C_STOP;
        }
        break;

    // ======================================================
    // STATE: REPEATED START (for read)
    // Wait for BTF, then generate START again
    // ======================================================
    case I2C_RESTART:
        if (sr1 & I2C_SR1_BTF)
        {
            I2C1->CR1 |= I2C_CR1_START;   // Generate repeated START
            i2c.state = I2C_ADDR_R;
        }
        break;

    // ======================================================
    // STATE: ADDR (READ) - send read address
    // Wait for SB, then send device address (read mode)
    // ======================================================
    case I2C_ADDR_R:
        if (sr1 & I2C_SR1_SB)
        {
            I2C1->DR = i2c.dev | 1;   // Send device address (read mode)
            i2c.state = I2C_ADDR_R_WAIT;
        }
        break;

    // ======================================================
    // STATE: ADDR (READ WAIT)
    // Wait for ADDR flag, then configure ACK/NACK behavior
    // ======================================================
    case I2C_ADDR_R_WAIT:
        if (sr1 & I2C_SR1_ADDR)
        {
            volatile uint32_t tmp;
            tmp = I2C1->SR1;
            tmp = I2C1->SR2;
            (void)tmp;               // Clear ADDR flag

            if (i2c.len == 1)
            {
                // Single byte read: NACK + STOP immediately
                I2C1->CR1 &= ~I2C_CR1_ACK;
                I2C1->CR1 |= I2C_CR1_STOP;
            }
            else
            {
                // Multi-byte read: enable ACK
                I2C1->CR1 |= I2C_CR1_ACK;
            }

            i2c.state = I2C_READ;
        }
        break;

    // ======================================================
    // STATE: READ DATA
    // Wait for RXNE (data received), then read byte
    // ======================================================
    case I2C_READ:
        if (sr1 & I2C_SR1_RXNE)
        {
            i2c.buf[i2c.idx++] = I2C1->DR;

            // Prepare to NACK the last byte
            if (i2c.idx == i2c.len - 1)
            {
                I2C1->CR1 &= ~I2C_CR1_ACK;
                I2C1->CR1 |= I2C_CR1_STOP;
            }

            // All bytes received
            if (i2c.idx >= i2c.len)
            {
                i2c.state = I2C_DONE;
                i2c.busy  = 0;

                I2C1->CR1 |= I2C_CR1_ACK;   // Restore ACK for next transfer

                if (i2c_cb)
                    i2c_cb();              // Notify completion
            }
        }
        break;

    // ======================================================
    // STATE: STOP (write flow)
    // Wait for BTF, then generate STOP condition
    // ======================================================
    case I2C_STOP:
        if (sr1 & I2C_SR1_BTF)
        {
            I2C1->CR1 |= I2C_CR1_STOP;

            // I2C1->CR2 &= ~(I2C_CR2_ITEVTEN |
            //             I2C_CR2_ITBUFEN |
            //             I2C_CR2_ITERREN);

            i2c.state = I2C_IDLE;
            i2c.busy  = 0;

            I2C1->CR1 |= I2C_CR1_ACK;   // Restore ACK

            if (i2c_cb)
            {
                i2c_cb();              // Notify completion
                i2c_cb = 0;
            }
                
            }
        break;

    default:
        break;
    }
}