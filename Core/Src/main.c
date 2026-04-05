#include "main.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <stdlib.h> 

#include "sensor.h"
#include "i2c.h"
#include "i2c_driver.h"
#include "benchmark_cpu.h"
#include "benchmark_sys.h"
#include "control.h"

#define BENCHMARK_MODE 1   // 1: benchmark, 0: production

UART_HandleTypeDef huart2;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Definitions for SensorTask */
osThreadId_t SensorTaskHandle;
const osThreadAttr_t SensorTask_attributes = {
  .name = "SensorTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for LoggerTask */
osThreadId_t LoggerTaskHandle;
const osThreadAttr_t LoggerTask_attributes = {
  .name = "LoggerTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

/* Definitions for BenchmarkTask */
osThreadId_t BenchmarkTaskHandle;
const osThreadAttr_t BenchmarkTask_attributes = {
  .name = "BenchmarkTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Definitions for logQueue */
osMessageQueueId_t logQueueHandle;
const osMessageQueueAttr_t logQueue_attributes = {
  .name = "logQueue"
};
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
void StartDefaultTask(void *argument);
void StartTask02(void *argument);
void StartTask03(void *argument);
void BenchmarkTask(void *argument);

int main(void)
{
  HAL_Init(); 
  SystemClock_Config();

  MX_GPIO_Init();
  // MX_I2C1_Init();
  i2c_init();
  MX_USART2_UART_Init();

  benchmark_cpu_init();
  benchmark_sys_init();

  osKernelInitialize();

  logQueueHandle = osMessageQueueNew (10, sizeof(log_data_t), &logQueue_attributes);

  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
  SensorTaskHandle = osThreadNew(StartTask02, NULL, &SensorTask_attributes);
  LoggerTaskHandle = osThreadNew(StartTask03, NULL, &LoggerTask_attributes);
  BenchmarkTaskHandle = osThreadNew(BenchmarkTask, NULL, &BenchmarkTask_attributes);

  /* Start scheduler */
  osKernelStart();

  while (1)
  {

  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}


/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    return len;
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{ 
  osDelay(2000);

  vTaskSuspend(SensorTaskHandle);
  vTaskSuspend(LoggerTaskHandle);
  vTaskSuspend(BenchmarkTaskHandle); 

  benchmark_calibrate_idle(); 
  
  vTaskResume(SensorTaskHandle);
  vTaskResume(LoggerTaskHandle);
  vTaskResume(BenchmarkTaskHandle);

  vTaskDelete(NULL);
}

void StartTask02(void *argument)
{
  log_data_t data;

  sensor_init();

  // --- PI controller state ---
  // static int integral = 0;

  for(;;)
  {   
      if (sensor_read_temperature(&data.sensor) == 0)
      {   
          // simulate workload
          // for (volatile int i = 0; i < 50000; i++);

          data.timestamp = benchmark_start();

          if (osMessageQueuePut(logQueueHandle, &data, 0, 0) == osOK)
          {
              uint32_t depth = osMessageQueueGetCount(logQueueHandle);
              benchmark_queue_update(depth);

              // // --- PI control ---
              // int error = (int)depth - TARGET_DEPTH;

              // // integral accumulation
              // integral += error;

              // // anti-windup
              // if (integral > 100) integral = 100;
              // if (integral < -100) integral = -100;

              // int delay = BASE_DELAY + KP * error + KI * integral;

              // // clamp delay
              // if (delay < MIN_DELAY) delay = MIN_DELAY;
              // if (delay > MAX_DELAY) delay = MAX_DELAY;


              // osDelay(delay);
              osDelay(1);
          }
          else
          {
              benchmark_queue_drop();

              // queue full -> reduce the producer rate
              osDelay(MAX_DELAY);
          }
      }
    
      // osDelay(10);
  }
}

void StartTask03(void *argument)
{
  log_data_t data = {0}; 
  static int counter = 0;

  for(;;)
  {
#if BENCHMARK_MODE
    // ================================
    // [Benchmark Mode - Non-blocking]
    // ================================
    if (osMessageQueueGet(logQueueHandle, &data, NULL, osWaitForever) != osOK)
    {
        continue; 
    }
#else
    // ================================
    // [Production Mode - Blocking]
    // ================================
    if (osMessageQueueGet(logQueueHandle, &data, NULL, osWaitForever) != osOK)
    {
        continue;
    }
#endif

    // ================================
    // [Benchmark Section]
    // ================================

#if BENCHMARK_MODE
    // Count throughput
    benchmark_throughput_inc();

    // T1: queue latency
    // uint32_t queue_cycles = benchmark_end(data.timestamp);

    // // Simulate processing workload
    // for (volatile int i = 0; i < 200000; i++);

    // T2: total latency
    uint32_t total_cycles = benchmark_end(data.timestamp);

    // uint32_t processing_cycles = total_cycles - queue_cycles;

    benchmark_latency_record(total_cycles);

    // printf("[BENCH] queue=%lu us, proc=%lu us, total=%lu us\r\n",
    //         queue_cycles / (CPU_FREQ / 1000000),
    //         processing_cycles / (CPU_FREQ / 1000000),
    //         total_cycles / (CPU_FREQ / 1000000));
    
#endif

    // ================================
    // [Temperature Print Section]
    // ================================

    if (++counter >= 10)
    {
        counter = 0;

        int temp = data.sensor.temperature;

        printf("T: %d.%02d\r\n",
                temp / 100,
                abs(temp % 100));
    }
  }
}

void BenchmarkTask(void *argument)
{
    for (;;)
    {
        benchmark_sys_log();
        benchmark_cpu_log();

        osDelay(1000);   // 每秒印一次
    }
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
