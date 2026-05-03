#include "benchmark_sys.h"
#include <stdio.h>
#include "cmsis_os2.h"

static uint32_t max_queue_depth = 0;
static uint32_t queue_drop_count = 0;
static uint32_t queue_drop_last = 0;

static uint32_t msg_count = 0;
static uint32_t last_tick = 0;

// sensor_read latency only
static uint32_t sensor_latency_min = 0xFFFFFFFF;
static uint32_t sensor_latency_max = 0;
static uint64_t sensor_latency_sum = 0;
static uint32_t sensor_latency_count = 0;

static uint32_t cycles_to_us(uint32_t cycles)
{
    return (uint32_t)(((uint64_t)cycles * 1000000ULL) / CPU_FREQ);
}

void benchmark_sys_init(void)
{
    max_queue_depth = 0;
    queue_drop_count = 0;
    queue_drop_last = 0;
    msg_count = 0;
    last_tick = osKernelGetTickCount();

    sensor_latency_min = 0xFFFFFFFF;
    sensor_latency_max = 0;
    sensor_latency_sum = 0;
    sensor_latency_count = 0;
}

void benchmark_queue_update(uint32_t depth)
{
    if (depth > max_queue_depth)
        max_queue_depth = depth;
}

void benchmark_queue_drop(void)
{
    queue_drop_count++;
}

void benchmark_throughput_inc(void)
{
    msg_count++;
}

void benchmark_sensor_latency_record(uint32_t cycles)
{
    if (cycles < sensor_latency_min)
        sensor_latency_min = cycles;

    if (cycles > sensor_latency_max)
        sensor_latency_max = cycles;

    sensor_latency_sum += cycles;
    sensor_latency_count++;
}

void benchmark_sys_log(void)
{
    uint32_t now = osKernelGetTickCount();

    if (now - last_tick >= 1000)
    {
        uint32_t drop_per_sec = queue_drop_count - queue_drop_last;
        queue_drop_last = queue_drop_count;

        uint32_t sensor_avg = 0;

        if (sensor_latency_count > 0)
        {
            sensor_avg = sensor_latency_sum / sensor_latency_count;
        }

        printf("[SYS] tp=%lu | maxQ=%lu | drop=%lu (%lu/s) | sensor_avg=%lu us | sensor_min=%lu us | sensor_max=%lu us\r\n",
        msg_count,
        max_queue_depth,
        queue_drop_count,
        drop_per_sec,
        cycles_to_us(sensor_avg),
        cycles_to_us(sensor_latency_min),
        cycles_to_us(sensor_latency_max));

        msg_count = 0;

        sensor_latency_min = 0xFFFFFFFF;
        sensor_latency_max = 0;
        sensor_latency_sum = 0;
        sensor_latency_count = 0;

        last_tick = now;
    }
}