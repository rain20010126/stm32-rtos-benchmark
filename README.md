# STM32 RTOS Benchmark

This project implements a performance benchmark framework on STM32 using a producer-consumer architecture under FreeRTOS.

---

## Project Overview

The goal of this project is to analyze system behavior under different workloads and identify performance bottlenecks in an RTOS-based embedded system.

The system is designed to measure:

- Throughput (messages per second)
- End-to-end latency (enqueue → processing completion)
- Queue utilization (max depth)
- Drop rate (queue overflow)
- CPU usage (idle-based estimation)

---

## System Architecture

SensorTask (Producer) → Message Queue → LoggerTask (Consumer)

- **SensorTask**
  - Reads sensor data
  - Generates workload
  - Pushes data into queue with timestamp

- **LoggerTask**
  - Retrieves data from queue
  - Simulates processing workload
  - Measures latency and system performance

---

## Baseline (baseline-v1)

This baseline represents a **CPU-bound saturated system**.

### Configuration

- Producer rate: 10 Hz
- Consumer: heavy workload (busy loop)
- Queue size: 10
- Non-blocking queue access

### Observed Behavior

- CPU usage: ~100% (system fully saturated)
- Throughput: ~3 msg/sec
- Queue: always full (max depth reached)
- Drop rate: continuous (~2–3 msg/sec)
- Latency: ~300–400 ms

### Key Insight

The system bottleneck is located in the **consumer processing stage**, where heavy workload prevents the system from keeping up with incoming data.

---

## Engineering Insights

- CPU saturation leads to queue buildup and increased latency
- Non-blocking queue access can hide actual waiting time
- Latency definition must include processing stage to reflect real system behavior
- Idle-based CPU measurement provides low-overhead estimation for embedded systems

---

## Development Notes

Detailed development logs, experiments, and analysis are documented here:

**[Development Notes](https://hackmd.io/@10530417/ryWL9oLsZx)**

---

## Future Work

- Separate queue latency and processing latency
- Optimize consumer workload
- Explore blocking vs non-blocking queue behavior
- Add latency distribution (histogram / jitter analysis)
- Compare different system configurations

---

## Summary

This project demonstrates how to:

- Design a producer-consumer system in RTOS
- Measure system performance under load
- Identify bottlenecks using quantitative metrics
- Build a reproducible benchmark for optimization
