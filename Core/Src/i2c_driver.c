#include "i2c_driver.h"
#include "stm32f4xx.h"
#include <stdio.h>

static int i2c_start_wait_idle(void);
static int i2c_repeated_start(void);
static int i2c_send_address(uint8_t addr);
static int i2c_write_byte(uint8_t data);
static int i2c_read_byte(uint8_t *data, int ack);
static void i2c_stop(void);

void i2c_init(void)
{
    // Enable cloc, RCC = Reset and Clock Control
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    // PB8 = SCL, PB9 = SDA (AF4 : 2C)
    GPIOB->MODER &= ~((3 << (8*2)) | (3 << (9*2)));
    GPIOB->MODER |=  ((2 << (8*2)) | (2 << (9*2)));

    GPIOB->AFR[1] &= ~((0xF << ((8-8)*4)) | (0xF << ((9-8)*4)));
    GPIOB->AFR[1] |=  ((4 << ((8-8)*4)) | (4 << ((9-8)*4)));


    // open-drain, OTYPER = Output Type Register
    GPIOB->OTYPER |= (1 << 8) | (1 << 9);

    // pull-up, PUPDR = Pull-Up / Pull-Down Register
    GPIOB->PUPDR &= ~((3 << (8*2)) | (3 << (9*2)));
    GPIOB->PUPDR |=  ((1 << (8*2)) | (1 << (9*2)));

    // reset I2C, SWRST = Software Reset
    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;

    // clock config
    I2C1->CR2 = 16;        // 16 MHz
    I2C1->CCR = 80;        // 100kHz (Fscl = Fpclk / (2 * CCR))
    I2C1->TRISE = 17;

    // enable
    I2C1->CR1 |= I2C_CR1_PE;
}

int i2c_read_reg(uint8_t dev, uint8_t reg, uint8_t *buf, int len)
{
    if (len <= 0) return -1;

    if (i2c_start_wait_idle() != 0)
    {
        i2c_stop();
        return -4;
    }

    if (i2c_send_address(dev) != 0)
    {
        i2c_stop();
        return -1;
    }
        

    if (i2c_write_byte(reg) != 0)
    {
        i2c_stop();
        return -2;
    }

    // repeated start
    if (i2c_repeated_start() != 0)
    {
        i2c_stop();
        return -5;
    }

    if (i2c_send_address(dev | 1) != 0)
    {
        i2c_stop();
        return -3;
    }

    for (int i = 0; i < len; i++)
    {
        if (i == len - 1)
        {
            I2C1->CR1 &= ~I2C_CR1_ACK;
        }

        if (i2c_read_byte(&buf[i], i != (len - 1)) != 0)
        {
            i2c_stop();
            I2C1->CR1 |= I2C_CR1_ACK;
            return -6;
        }
    }

    i2c_stop();
    I2C1->CR1 |= I2C_CR1_ACK;

    return 0;
}

int i2c_write_reg(uint8_t dev, uint8_t reg, uint8_t val)
{
    int ret;

    if (i2c_start_wait_idle() != 0)
    {
        i2c_stop();
        return -4;
    }

    // send device address (write mode)
    ret = i2c_send_address(dev);
    if (ret != 0)
    {
        i2c_stop();
        return -1;
    }

    // send register address
    ret = i2c_write_byte(reg);
    if (ret != 0)
    {
        i2c_stop();
        return -2;
    }

    // send data
    ret = i2c_write_byte(val);
    if (ret != 0)
    {
        i2c_stop();
        return -3;
    }

    // stop condition
    i2c_stop();

    return 0;
}

// static int i2c_start(void)
// {
//     uint32_t timeout = 100000;

//     I2C1->CR1 |= I2C_CR1_START;

//     while (!(I2C1->SR1 & I2C_SR1_SB))
//     {
//         if (--timeout == 0)
//             return -1;
//     }

//     volatile uint32_t temp = I2C1->SR1;
//     (void)temp;

//     return 0;
// }

static int i2c_start_wait_idle(void)
{
    uint32_t timeout = 100000;

    while (I2C1->SR2 & I2C_SR2_BUSY)
    {
        if (--timeout == 0)
            return -2;
    }

    I2C1->CR1 |= I2C_CR1_START;

    timeout = 100000;
    while (!(I2C1->SR1 & I2C_SR1_SB))
    {
        if (--timeout == 0)
            return -1;
    }

    volatile uint32_t temp = I2C1->SR1;
    (void)temp;

    return 0;
}

static int i2c_repeated_start(void)
{
    uint32_t timeout = 100000;

    I2C1->CR1 |= I2C_CR1_START;

    while (!(I2C1->SR1 & I2C_SR1_SB))
    {
        if (--timeout == 0)
            return -1;
    }

    volatile uint32_t temp = I2C1->SR1;
    (void)temp;

    return 0;
}


static int i2c_send_address(uint8_t addr)
{
    uint32_t timeout = 100000;

    // Send address 
    // DR: Data register 7-bit address + 1-bit R/W
    I2C1->DR = addr;

    // Wait for ADDR (address matched) or AF (Acknowledge Failure)
    while (!(I2C1->SR1 & I2C_SR1_ADDR))
    {
        // ACK failure
        if (I2C1->SR1 & I2C_SR1_AF)
        {
            I2C1->SR1 &= ~I2C_SR1_AF; //reset SR1
            return -1;
        }

        if (--timeout == 0)
            return -2;
    }

    // Clear ADDR (read SR1 then SR2)
    volatile uint32_t temp;
    temp = I2C1->SR1;
    temp = I2C1->SR2;
    (void)temp;

    return 0;
}

// --------------------------------

static int i2c_write_byte(uint8_t data)
{
    uint32_t timeout = 100000;

    // Write data
    I2C1->DR = data;

    // Wait TXE (Transmit Data Register Empty)
    while (!(I2C1->SR1 & I2C_SR1_TXE))
    {
        if (--timeout == 0)
            return -1;
    }

    // Wait BTF (Byte Transfer Finished)
    while (!(I2C1->SR1 & I2C_SR1_BTF))
    {
        if (--timeout == 0)
            return -2;
    }

    return 0;
}

// --------------------------------

static int i2c_read_byte(uint8_t *data, int ack)
{
    uint32_t timeout = 100000;

    if (ack)
        I2C1->CR1 |= I2C_CR1_ACK;
    else
        I2C1->CR1 &= ~I2C_CR1_ACK;

    while (!(I2C1->SR1 & I2C_SR1_RXNE))
    {
        if (--timeout == 0)
            return -1;
    }

    *data = I2C1->DR;
    return 0;
}

// --------------------------------

static int i2c_stop_wait(void)
{
    uint32_t timeout = 100000;

    I2C1->CR1 |= I2C_CR1_STOP;

    while (I2C1->CR1 & I2C_CR1_STOP)
    {
        if (--timeout == 0)
            return -1;
    }

    return 0;
}

static void i2c_stop(void)
{
    if (i2c_stop_wait() != 0)
    {
        printf("STOP wait failed: SR1=0x%04lX SR2=0x%04lX CR1=0x%04lX\r\n",
            I2C1->SR1, I2C1->SR2, I2C1->CR1);
    }
}