/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

#include "aes.h"

#include <string.h>
#include <stdio.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
RNG_HandleTypeDef hrng;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_RNG_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint8_t aes_key[AES_KEYLEN];
struct AES_ctx aes_ctx;



#define DEBUG_UART 0  // set to 0 for measurements


#define MSG_SIZE 16   // change later to 16,64,128,512,1024

uint8_t plaintext[MSG_SIZE];
uint8_t buffer[MSG_SIZE];




void generate_aes_key_rng(uint8_t* key , uint32_t key_len)
{
    uint32_t rnd;
    uint32_t words = key_len / 4;


    for (int i = 0; i < words; i++)
    {
        if (HAL_RNG_GenerateRandomNumber(&hrng, &rnd) != HAL_OK)
        {
            Error_Handler();
        }

        key[i*4 + 0] = (rnd >> 24) & 0xFF;
        key[i*4 + 1] = (rnd >> 16) & 0xFF;
        key[i*4 + 2] = (rnd >> 8)  & 0xFF;
        key[i*4 + 3] = (rnd)       & 0xFF;
    }
}


static inline void DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    return len;
}




/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();
  DWT_Init();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_RNG_Init();
  /* USER CODE BEGIN 2 */
     // 128-bit key

  /* USER CODE END 2 */
  /* --- RNG key generation marker --- */

    for(int i = 0; i < MSG_SIZE; i++)
    {
        plaintext[i] = i;
    }

    	  	  //--------encryption -----------
  #define NUM_RUNS 100
  volatile float execution_time_us;
  uint32_t start_cycles;
  uint32_t end_cycles;
  uint32_t total_cycles = 0;
  uint32_t avg_cycles = 0;

  uint8_t iv[AES_BLOCKLEN] = {0};
  generate_aes_key_rng((uint8_t*)aes_key, AES_KEYLEN);
  AES_init_ctx_iv(&aes_ctx, aes_key, iv);

  for(int run = 0; run < NUM_RUNS; run++)
  {
  	memcpy(buffer, plaintext, MSG_SIZE);
  	AES_ctx_set_iv(&aes_ctx, iv);

      __disable_irq();
      HAL_GPIO_WritePin(Time_mark_GPIO_Port, Time_mark_Pin, GPIO_PIN_SET);
      __DSB();
      __ISB();

      DWT->CYCCNT = 0;
      start_cycles = DWT->CYCCNT;



      AES_CBC_encrypt_buffer(&aes_ctx, buffer, MSG_SIZE);



      end_cycles = DWT->CYCCNT;

      __DSB();
      __ISB();

      HAL_GPIO_WritePin(Time_mark_GPIO_Port, Time_mark_Pin, GPIO_PIN_RESET);
      __enable_irq();

      total_cycles += (end_cycles - start_cycles);

  }

  avg_cycles = total_cycles / NUM_RUNS;

  printf("\r\n=========AES-128 Encryption============\r\n");

  printf("SYSCLK = %lu Hz\r\n", HAL_RCC_GetSysClockFreq());
  printf("Message Size: %d bytes\r\n", MSG_SIZE);
  printf("Number of Runs: %d\r\n", NUM_RUNS);

  printf("\r\n---- Performance Metrics ----\r\n");

  printf("Execution Cycles: %lu cycles\r\n", avg_cycles);

  execution_time_us =
  ((float)avg_cycles / 80000000.0f) * 1000000.0f;

  printf("Execution Time: %.6f us\r\n", execution_time_us);

  float cycles_per_byte =
  (avg_cycles / (float)MSG_SIZE);

  printf("Cycles per Byte: %.3f cycles/byte\r\n", cycles_per_byte);

  float throughput_kbps =
  ((float)MSG_SIZE / execution_time_us) * 1000.0f;

  printf("Throughput: %.3f KB/s\r\n", throughput_kbps);




  printf("\r\n---- Energy Estimation ----\r\n");

  /* Replace with datasheet active current later */
  float voltage = 3.3f;
  float current_mA = 8.0f;

  float execution_time_s =
  execution_time_us / 1000000.0f;

  float energy_per_operation_uJ =
  voltage * (current_mA / 1000.0f) * execution_time_s * 1000000.0f;

  printf("Estimated Energy per Operation: %.6f uJ\r\n",
  energy_per_operation_uJ);

  float energy_per_byte_uJ =
  energy_per_operation_uJ / (float)MSG_SIZE;

  printf("Estimated Energy per Byte: %.6f uJ/byte\r\n",
  energy_per_byte_uJ);

  printf("=====================================\r\n");




  	      //--------- decryption -----------

  	      /* Prepare ciphertext (NOT measured) */


  #define NUM_RUNS_dec 100

  uint32_t start_cycles_dec;
  uint32_t end_cycles_dec;
  uint32_t total_cycles_dec = 0;
  uint32_t avg_cycles_dec = 0;

  /* Prepare ciphertext once (NOT measured) */
  memcpy(buffer, plaintext, MSG_SIZE);
  AES_ctx_set_iv(&aes_ctx, iv);
  AES_CBC_encrypt_buffer(&aes_ctx, buffer, MSG_SIZE);

  for(int run = 0; run < NUM_RUNS_dec; run++)
  {
  	AES_ctx_set_iv(&aes_ctx, iv);
      __disable_irq();

      HAL_GPIO_WritePin(Time_mark_GPIO_Port,
                        Time_mark_Pin,
                        GPIO_PIN_SET);

      __DSB();
      __ISB();

      DWT->CYCCNT = 0;
      start_cycles_dec = DWT->CYCCNT;

      AES_CBC_decrypt_buffer(&aes_ctx, buffer, MSG_SIZE);

      end_cycles_dec = DWT->CYCCNT;

      __DSB();
      __ISB();

      HAL_GPIO_WritePin(Time_mark_GPIO_Port,
                        Time_mark_Pin,
                        GPIO_PIN_RESET);

      __enable_irq();

      total_cycles_dec +=
      (end_cycles_dec - start_cycles_dec);
  }

  avg_cycles_dec = total_cycles_dec / NUM_RUNS_dec;

  float execution_time_dec_us =
  ((float)avg_cycles_dec / 80000000.0f) * 1000000.0f;

  float cycles_per_byte_dec =
  avg_cycles_dec / (float)MSG_SIZE;

  float throughput_dec_kbps =
  ((float)MSG_SIZE / execution_time_dec_us) * 1000.0f;

  voltage = 3.3f;
  current_mA = 8.0f;

  float execution_time_dec_s =
  execution_time_dec_us / 1000000.0f;

  float energy_dec_uJ =
  voltage * (current_mA / 1000.0f)
  * execution_time_dec_s * 1000000.0f;

  float energy_per_byte_dec =
  energy_dec_uJ / (float)MSG_SIZE;


  printf("\r\n======= AES-128 Decryption  ========\r\n");

  printf("Execution Cycles: %lu\r\n",
  avg_cycles_dec);

  printf("Execution Time: %.6f us\r\n",
  execution_time_dec_us);

  printf("Cycles per Byte: %.3f\r\n",
  cycles_per_byte_dec);

  printf("Throughput: %.3f KB/s\r\n",
  throughput_dec_kbps);

  printf("Energy per Operation: %.6f uJ\r\n",
  energy_dec_uJ);

  printf("Energy per Byte: %.6f uJ/byte\r\n",
  energy_per_byte_dec);








  	      while (1)
  	          {
  	              __NOP();   // CPU idle, stable baseline
  	          }

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief RNG Initialization Function
  * @param None
  * @retval None
  */
static void MX_RNG_Init(void)
{

  /* USER CODE BEGIN RNG_Init 0 */

  /* USER CODE END RNG_Init 0 */

  /* USER CODE BEGIN RNG_Init 1 */

  /* USER CODE END RNG_Init 1 */
  hrng.Instance = RNG;
  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RNG_Init 2 */

  /* USER CODE END RNG_Init 2 */

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
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
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
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(Time_mark_GPIO_Port, Time_mark_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : Time_mark_Pin */
  GPIO_InitStruct.Pin = Time_mark_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(Time_mark_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD3_Pin */
  GPIO_InitStruct.Pin = LD3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __enable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
