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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "crypto_aead.h"
#include "api.h"

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
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#define DEBUG_UART  1  // 1 = PuTTY debug, 0 = energy measurement
#define NUM_RUNS    100

#define PAYLOAD_SIZE   1024


uint8_t key[CRYPTO_KEYBYTES] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
	    0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F};
uint8_t nonce[CRYPTO_NPUBBYTES] = {0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
	    0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F};
uint8_t ad[1] = {0};

uint8_t plaintext[1024] = {0};
uint8_t ciphertext[1024+CRYPTO_ABYTES] = {0};
uint8_t decrypted[1024] = {0};

unsigned long long clen, mlen;





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
  /* USER CODE BEGIN 2 */
	//memset(key, 0x00, CRYPTO_KEYBYTES);
	//memset(nonce, 0x00, CRYPTO_NPUBBYTES);
	//memset(ad, 0x00, sizeof(ad));



	size_t pt_len = PAYLOAD_SIZE;

	const float voltage    = 3.3f;
	const float current_mA = 8.0f;

	for (size_t i = 0; i < pt_len; i++)
	        plaintext[i] = (uint8_t)(i & 0xFF);

	uint32_t start_cycles;
	uint32_t end_cycles;
	uint32_t total_cycles = 0;
	uint32_t avg_cycles = 0;



    for(int run = 0; run < NUM_RUNS; run++)
        {
            __disable_irq();

            HAL_GPIO_WritePin(Time_marker_GPIO_Port,
                              Time_marker_Pin,
                              GPIO_PIN_SET);

            __DSB();
            __ISB();

            DWT->CYCCNT  = 0;
            start_cycles = DWT->CYCCNT;

            crypto_aead_encrypt(
                ciphertext, &clen,
                plaintext, pt_len,
                ad, 0,
                NULL,
                nonce, key
            );

            end_cycles = DWT->CYCCNT;

            __DSB();
            __ISB();

            HAL_GPIO_WritePin(Time_marker_GPIO_Port,
                              Time_marker_Pin,
                              GPIO_PIN_RESET);

            __enable_irq();

            total_cycles += (end_cycles - start_cycles);
        }

        avg_cycles = total_cycles / NUM_RUNS;

        volatile float execution_time_us = ((float)avg_cycles / 80000000.0f) * 1000000.0f;

        float cycles_per_byte        = (float)avg_cycles / (float)pt_len;
        float throughput_kbps        = ((float)pt_len / execution_time_us) * 1000.0f;
        float execution_time_s       = execution_time_us / 1000000.0f;
        float energy_per_operation_uJ = voltage * (current_mA / 1000.0f) * execution_time_s * 1000000.0f;
        float energy_per_byte_uJ     = energy_per_operation_uJ / (float)pt_len;

        printf("\r\n===== ASCON-128 Encryption  ======\r\n");
        printf("Message Size: %d bytes\r\n", (unsigned)pt_len);
        printf("SYSCLK = %lu Hz\r\n", HAL_RCC_GetSysClockFreq());
        printf("Execution Cycles: %lu\r\n",           avg_cycles);
        printf("Execution Time: %.6f us\r\n",          execution_time_us);
        printf("Cycles per Byte: %.3f\r\n",            cycles_per_byte);
        printf("Throughput: %.3f KB/s\r\n",            throughput_kbps);
        printf("Energy per Operation: %.6f uJ\r\n",    energy_per_operation_uJ);
        printf("Energy per Byte: %.6f uJ/byte\r\n",    energy_per_byte_uJ);

/* ----------------------------------------------------------- */
/* PREPARE VALID CIPHERTEXT FOR DECRYPT BENCHMARK               */
/* (outside timing window – NOT measured)                       */
/* ----------------------------------------------------------- */

crypto_aead_encrypt(
    ciphertext, &clen,
    plaintext, pt_len,
    ad, 0,
    NULL,
    nonce, key
);


/* ----------------------------------------------------------- */
/* ASCON-128 DECRYPTION BENCHMARK                              */
/* ----------------------------------------------------------- */

	uint32_t start_cycles_dec;
	uint32_t end_cycles_dec;
	uint32_t total_cycles_dec = 0;
	uint32_t avg_cycles_dec = 0;

/* ciphertext already produced by encryption stage */

for(int run = 0; run < NUM_RUNS; run++)
    {
        __disable_irq();

        HAL_GPIO_WritePin(Time_marker_GPIO_Port,
                          Time_marker_Pin,
                          GPIO_PIN_SET);

        __DSB();
        __ISB();

        DWT->CYCCNT      = 0;
        start_cycles_dec = DWT->CYCCNT;

        crypto_aead_decrypt(
            decrypted, &mlen,
            NULL,
            ciphertext, clen,
            ad, 0,
            nonce, key
        );

        end_cycles_dec = DWT->CYCCNT;

        __DSB();
        __ISB();

        HAL_GPIO_WritePin(Time_marker_GPIO_Port,
                          Time_marker_Pin,
                          GPIO_PIN_RESET);

        __enable_irq();

        total_cycles_dec += (end_cycles_dec - start_cycles_dec);
    }

    avg_cycles_dec = total_cycles_dec / NUM_RUNS;

    float execution_time_dec_us  = ((float)avg_cycles_dec / 80000000.0f) * 1000000.0f;

    float cycles_per_byte_dec   = (float)avg_cycles_dec / (float)pt_len;
    float throughput_dec_kbps   = ((float)pt_len / execution_time_dec_us) * 1000.0f;
    float execution_time_dec_s  = execution_time_dec_us / 1000000.0f;
    float energy_dec_uJ         = voltage * (current_mA / 1000.0f) * execution_time_dec_s * 1000000.0f;
    float energy_per_byte_dec   = energy_dec_uJ / (float)pt_len;

    printf("\r\n===== ASCON-128 Decryption======\r\n ");
    printf("Execution Cycles: %lu\r\n",           avg_cycles_dec);
    printf("Execution Time: %.6f us\r\n",          execution_time_dec_us);
    printf("Cycles per Byte: %.3f\r\n",            cycles_per_byte_dec);
    printf("Throughput: %.3f KB/s\r\n",            throughput_dec_kbps);
    printf("Energy per Operation: %.6f uJ\r\n",    energy_dec_uJ);
    printf("Energy per Byte: %.6f uJ/byte\r\n",    energy_per_byte_dec);


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
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
  HAL_GPIO_WritePin(Time_marker_GPIO_Port, Time_marker_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : Time_marker_Pin */
  GPIO_InitStruct.Pin = Time_marker_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(Time_marker_GPIO_Port, &GPIO_InitStruct);

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
  __disable_irq();
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
