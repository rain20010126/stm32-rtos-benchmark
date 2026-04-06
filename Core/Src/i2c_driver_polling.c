#include "i2c_driver.h"
#include "stm32f4xx.h"
#include <stdio.h>

static void i2c_start(void);
static int  i2c_send_address(uint8_t addr);
static int  i2c_write_byte(uint8_t data);
static uint8_t i2c_read_byte(int ack);
static void i2c_stop(void);


void i2c_init_polling(void)
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

int i2c_read_reg_polling(uint8_t dev, uint8_t reg, uint8_t *buf, int len)
{
    if (len <= 0) return -1;

    i2c_start();

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
    i2c_start();

    if (i2c_send_address(dev | 1) != 0)
    {
        i2c_stop();
        return -3;
    }

    for (int i = 0; i < len; i++)
    {
        if (i == len - 1)
        {
            I2C1->CR1 &= ~I2C_CR1_ACK;  // NACK before last byte
        }

        buf[i] = i2c_read_byte(i != (len - 1));
    }

    i2c_stop();
    I2C1->CR1 |= I2C_CR1_ACK;

    return 0;
}

int i2c_write_reg_polling(uint8_t dev, uint8_t reg, uint8_t val)
{
    int ret;

    i2c_start();

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

static void i2c_start(void)
{   
    // printf("before START: SR1=0x%04lX SR2=0x%04lX\n", I2C1->SR1, I2C1->SR2);
    uint32_t timeout = 100000;

    I2C1->CR1 |= I2C_CR1_START;

    // printf("after START: SR1=0x%04lX\n", I2C1->SR1);

    while (!(I2C1->SR1 & I2C_SR1_SB))
    {
        if (--timeout == 0) return;
    }

    volatile uint32_t temp = I2C1->SR1;
    (void)temp;
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

static uint8_t i2c_read_byte(int ack)
{
    uint8_t data;

    if (ack)
        I2C1->CR1 |= I2C_CR1_ACK;
    else
        I2C1->CR1 &= ~I2C_CR1_ACK;

    // Wait RXNE (Receive buffer not empty)
    while (!(I2C1->SR1 & I2C_SR1_RXNE));

    // Read data and clean buffer
    data = I2C1->DR;

    return data;
}

// --------------------------------

static void i2c_stop(void)
{
    I2C1->CR1 |= I2C_CR1_STOP;
}