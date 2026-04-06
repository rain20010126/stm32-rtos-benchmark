#include "benchmark_cpu.h"
#include "main.h"
#include <stdio.h>
#include <stdint.h>
#include "stm32f4xx.h"
#include "cmsis_os2.h"

/* ================================
 * Global variables
 * ================================ */

// idle counter (incremented in Idle Hook)
volatile uint32_t idle_counter = 0;

// internal use
static uint32_t last_idle_counter = 0;
static uint32_t max_idle_per_sec = 0;

/* ================================
 * DWT Cycle Counter Init
 * ================================ */

static void dwt_init(void)
{
    // Enable DWT (Data Watchpoint and Trace)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    // Reset cycle counter
    DWT->CYCCNT = 0;

    // Enable cycle counter
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* ================================
 * Public API
 * ================================ */

void benchmark_cpu_init(void)
{
    dwt_init();

    // reset counters
    idle_counter = 0;
    last_idle_counter = 0;
    max_idle_per_sec = 0;
}

/**
 * @brief Call this once after system stabilizes (calibration)
 *        Run this when system is mostly idle to get max_idle_per_sec
 */
void benchmark_calibrate_idle(void)
{
    // printf("Calibrating idle...\r\n");

    idle_counter = 0;
    osDelay(1000);  // 1 second window

    max_idle_per_sec = idle_counter;

    // printf("Idle baseline: %lu\r\n", max_idle_per_sec);
}

/**
 * @brief Get CPU usage (%)
 */
uint32_t benchmark_get_cpu_usage(void)
{
    uint32_t current = idle_counter;
    uint32_t diff = current - last_idle_counter;
    last_idle_counter = current;

    if (max_idle_per_sec == 0)
        return 0;

    uint32_t idle_percent = (diff * 100) / max_idle_per_sec;

    if (idle_percent > 100)
        idle_percent = 100;  

    uint32_t cpu = 100 - idle_percent;

    return cpu;
}

/**
 * @brief Print benchmark info (call every 1 sec)
 */
void benchmark_cpu_log(void)
{
    uint32_t cpu = benchmark_get_cpu_usage();

    printf("[BENCH] CPU: %lu%%\r\n", cpu);
}

/**
 * @brief Start cycle measurement
 */
uint32_t benchmark_start(void)
{
    return DWT->CYCCNT;
}

/**
 * @brief End cycle measurement
 */
uint32_t benchmark_end(uint32_t start)
{
    return DWT->CYCCNT - start;
}