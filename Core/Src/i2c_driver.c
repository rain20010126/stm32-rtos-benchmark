/**
 * @file i2c_driver.c
 * @brief Interrupt-driven I2C driver (STM32F4, register-level)
 *
 * Design:
 *   - Interrupt-driven state machine
 *   - Supports async + blocking APIs
 *   - Callback-based completion
 *
 * Flow (read):
 *   START → ADDR_W → REG → RESTART → ADDR_R → READ → DONE
 */

#include "i2c_driver.h"
#include "stm32f4xx.h"
#include <stdio.h>

// ======================================================
// Type definitions
// ======================================================

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

/**
 * @brief I2C transaction context
 */
typedef struct {
    uint8_t dev;
    uint8_t reg;

    uint8_t *buf;
    uint8_t tx_buf[4];

    int len;
    int idx;

    int is_read;

    i2c_state_t state;
    int busy;
} i2c_ctx_t;

// ======================================================
// Global context
// ======================================================

static i2c_ctx_t i2c;
static i2c_callback_t i2c_cb = 0;

// ======================================================
// Initialization
// ======================================================

/**
 * @brief Initialize I2C1 (GPIO + peripheral + interrupt)
 */
void i2c_init(void)
{
    // Enable GPIOB clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    // Configure PB8 (SCL), PB9 (SDA) as AF4
    GPIOB->MODER &= ~((3 << (8*2)) | (3 << (9*2)));
    GPIOB->MODER |=  ((2 << (8*2)) | (2 << (9*2)));

    GPIOB->AFR[1] &= ~((0xF << ((8-8)*4)) | (0xF << ((9-8)*4)));
    GPIOB->AFR[1] |=  ((4 << ((8-8)*4)) | (4 << ((9-8)*4)));

    // Open-drain
    GPIOB->OTYPER |= (1 << 8) | (1 << 9);

    // Pull-up
    GPIOB->PUPDR &= ~((3 << (8*2)) | (3 << (9*2)));
    GPIOB->PUPDR |=  ((1 << (8*2)) | (1 << (9*2)));

    // Enable I2C1 clock
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    // Reset I2C
    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;

    // Configure clock (16 MHz)
    I2C1->CR2 = 16;
    I2C1->CCR = 80;
    I2C1->TRISE = 17;

    // Enable peripheral
    I2C1->CR1 |= I2C_CR1_PE;

    // Wait until bus is free
    while (I2C1->SR2 & I2C_SR2_BUSY);

    // Enable interrupts
    I2C1->CR2 |= (I2C_CR2_ITEVTEN |
                  I2C_CR2_ITBUFEN |
                  I2C_CR2_ITERREN);

    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_EnableIRQ(I2C1_ER_IRQn);
}

// ======================================================
// Blocking API (wrapper around async)
// ======================================================

int i2c_read_reg(uint8_t dev, uint8_t reg, uint8_t *buf, int len)
{
    i2c.is_read = 1;

    if (i2c_read_reg_async(dev, reg, buf, len, 0) != 0)
        return -1;

    while (i2c_is_busy());

    return 0;
}

int i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t val)
{
    i2c.is_read = 0;

    if (i2c_write_reg_async(dev, reg, val, 0) != 0)
        return -1;

    while (i2c_is_busy());

    return 0;
}

// ======================================================
// Utility
// ======================================================

int i2c_is_busy(void)
{
    return i2c.busy;
}

// ======================================================
// Async API
// ======================================================

/**
 * @brief Start async I2C write (register + 1 byte)
 */
int i2c_write_reg_async(uint8_t dev, uint8_t reg, uint8_t val, i2c_callback_t cb)
{   
    if (i2c.busy) return -1;

    i2c.dev = dev;
    i2c.reg = reg;
    i2c.tx_buf[0] = val;   
    i2c.buf = i2c.tx_buf;
    i2c.len = 1;
    i2c.idx = 0;
    i2c_cb = cb;
    i2c.state = I2C_START;
    i2c.busy = 1;

    // trigger START
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

            i2c.state = I2C_DONE;
            i2c.busy  = 0;

            I2C1->CR1 |= I2C_CR1_ACK;   // Restore ACK

            if (i2c_cb)
                i2c_cb();              // Notify completion
        }
        break;

    default:
        break;
    }
}