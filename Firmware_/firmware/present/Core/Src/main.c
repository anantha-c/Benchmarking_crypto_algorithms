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
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>

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

/* USER CODE BEGIN 0 */

/* ===== PRESENT cipher code goes here ===== */

#include <stdint.h>
#include <math.h>



#define MESSAGE_SIZE_BYTES   1024

#define PRESENT_ROUNDS    31
#define BLOCK_SIZE_BYTES   8       /* PRESENT block = 64 bits = 8 bytes      */

#define CPU_FREQ_MHZ      80.0
#define CPU_FREQ          (CPU_FREQ_MHZ * 1000000.0)

#define NUM_RUNS          100      /* Iterations per payload size             */

#define SUPPLY_VOLTAGE    3.3      /* V  — STM32L432KC supply                 */
#define ACTIVE_CURRENT    0.0080   /* A  — 8 mA typical @ 80 MHz, from DS    */



static void DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}


/* --------------------------------------------------------------------------
 * Forward S-box  (PRESENT specification, Table 1)
 * S[i] maps a 4-bit nibble to its substituted value.
 * ------------------------------------------------------------------------- */

static const uint8_t S[16] = {
    0xC,0x5,0x6,0xB,
    0x9,0x0,0xA,0xD,
    0x3,0xE,0xF,0x8,
    0x4,0x7,0x1,0x2
};


/* --------------------------------------------------------------------------
 * Inverse S-box  (SI[i] = j  where S[j] = i)
 * Verified against the GitHub reference implementation (invS[]).
 * Values are identical — confirmed correct.
 * ------------------------------------------------------------------------- */

static const uint8_t SI[16] = {
    0x5, 0xE, 0xF, 0x8,
    0xC, 0x1, 0x2, 0xD,
    0xB, 0x4, 0x6, 0x3,
    0x0, 0x7, 0x9, 0xA
};


/* --------------------------------------------------------------------------
 * PRESENT bit-permutation table P[64]  (PRESENT specification, Table 2)
 *
 * P[i] = the position that bit i moves TO after permutation.
 * Equivalent to the formula (16*i) mod 63 for i=0..62, P[63]=63.
 *
 * Using the explicit table here (instead of the modular formula) matches
 * the reference implementation style and is easier to audit.
 * ------------------------------------------------------------------------- */

static const uint8_t P[64] = {
     0, 16, 32, 48,  1, 17, 33, 49,
     2, 18, 34, 50,  3, 19, 35, 51,
     4, 20, 36, 52,  5, 21, 37, 53,
     6, 22, 38, 54,  7, 23, 39, 55,
     8, 24, 40, 56,  9, 25, 41, 57,
    10, 26, 42, 58, 11, 27, 43, 59,
    12, 28, 44, 60, 13, 29, 45, 61,
    14, 30, 46, 62, 15, 31, 47, 63
};


/* --------------------------------------------------------------------------
 * Forward permutation layer
 *
 * Bit i of the input moves to position P[i] in the output.
 * Bit distance from MSB = 63 - i.
 * New position distance from MSB = 63 - P[i].
 * ------------------------------------------------------------------------- */

static uint64_t pLayer(uint64_t x)
{
    uint64_t y = 0;
    for (uint8_t i = 0; i < 64; i++) {
        y |= ((x >> (63 - i)) & 1ULL) << (63 - P[i]);
    }
    return y;
}

/* --------------------------------------------------------------------------
 * Inverse permutation layer
 *
 * Reverses pLayer: bit at position P[i] in input maps back to bit i.
 * For each output bit i, find which input bit belongs there:
 *   source bit position from MSB = 63 - P[i]
 *   place it at output position from MSB = 63 - i  (i.e. shift left by 1)
 *
 * Algorithm (same as GitHub reference inversepermute()):
 *   Build result bit-by-bit; for each i, take the bit at distance (63-P[i])
 *   from the MSB of source, and place it as the next LSB of result.
 * ------------------------------------------------------------------------- */

static uint64_t inv_pLayer(uint64_t x)
{
    uint64_t y = 0;
    for (uint8_t i = 0; i < 64; i++) {
        y = (y << 1) | ((x >> (63 - P[i])) & 1ULL);
    }
    return y;
}

/* --------------------------------------------------------------------------
 * Key schedule — 128-bit variant
 *
 * Generates 32 subkeys from a 128-bit key stored as two 64-bit halves:
 *   keyHigh = bits 127..64
 *   keyLow  = bits  63..0
 *
 * Schedule steps each round:
 *   1. Store keyHigh as subkey[r]
 *   2. Rotate entire 128-bit register left by 61 bits
 *   3. Apply S-box to top TWO nibbles of keyHigh (128-bit variant uses 2)
 *   4. XOR round counter into bits [66:62] of key register
 *      (maps to keyLow bits [2:6] after rotation — implemented as keyLow ^= counter<<62)
 *
 * No malloc — subKeys[] is caller-allocated static array.
 * ------------------------------------------------------------------------- */


void generateSubkeys128(uint64_t keyHigh,
                        uint64_t keyLow,
                        uint64_t subKeys[32])
{
    for (uint8_t r = 0; r <= PRESENT_ROUNDS; r++)
    {
        subKeys[r] = keyHigh;
        if (r == PRESENT_ROUNDS) break;

        /* Step 1: Rotate entire 128-bit key register left by 61 bits */
        uint64_t newHigh = (keyHigh << 61) | (keyLow  >> 3);
        uint64_t newLow  = (keyLow  << 61) | (keyHigh >> 3);
        keyHigh = newHigh;
        keyLow  = newLow;

        /* Step 2: S-box on top two nibbles of keyHigh (128-bit key variant) */
        uint8_t s1 = S[ keyHigh >> 60          ];
        uint8_t s2 = S[(keyHigh >> 56) & 0xF   ];
        keyHigh &= 0x00FFFFFFFFFFFFFFULL;
        keyHigh |= ((uint64_t)s1 << 60);
        keyHigh |= ((uint64_t)s2 << 56);

        /* Step 3: XOR round counter into key register bits [66:62] */
        keyLow ^= ((uint64_t)(r + 1) << 62);
    }
}



/* --------------------------------------------------------------------------
 * Single 64-bit block encryption (in-place)
 *
 * Encryption round structure (31 rounds):
 *   AddRoundKey → SubBytes → pLayer
 * Final step: AddRoundKey with subkey[31]
 * ------------------------------------------------------------------------- */
void present_encrypt(uint64_t *state, const uint64_t subKeys[32])
{
    uint64_t x = *state;

    for (uint8_t r = 0; r < PRESENT_ROUNDS; r++)
    {
        /* AddRoundKey */
        x ^= subKeys[r];

        /* SubBytes — apply S-box to each of the 16 nibbles */
        uint64_t y = 0;
        for (uint8_t i = 0; i < 16; i++) {
            y |= ((uint64_t)S[(x >> (4 * i)) & 0xF]) << (4 * i);
        }
        x = y;

        /* Permutation layer */
        x = pLayer(x);
    }

    /* Final AddRoundKey */
    x ^= subKeys[PRESENT_ROUNDS];
    *state = x;
}

/* --------------------------------------------------------------------------
 * Single 64-bit block decryption (in-place)
 *
 * Decryption is the exact inverse of encryption:
 *   Start : XOR subkey[31]           (undo final AddRoundKey)
 *   Rounds 30 down to 0:
 *     inv_pLayer → inv_SubBytes → XOR subkey[r]
 *
 * Round order verified against GitHub reference decrypt():
 *   state ^= subkeys[31-i] → inversepermute → inverseSbox
 *   which is identical to this implementation.
 * ------------------------------------------------------------------------- */
void present_decrypt(uint64_t *state, const uint64_t subKeys[32])
{
    uint64_t x = *state;

    /* Undo the final key addition */
    x ^= subKeys[PRESENT_ROUNDS];

    for (int8_t r = PRESENT_ROUNDS - 1; r >= 0; r--)
    {
        /* Undo permutation layer */
        x = inv_pLayer(x);

        /* Undo SubBytes — apply inverse S-box to each nibble */
        uint64_t y = 0;
        for (uint8_t i = 0; i < 16; i++) {
            y |= ((uint64_t)SI[(x >> (4 * i)) & 0xF]) << (4 * i);
        }
        x = y;

        /* Undo AddRoundKey */
        x ^= subKeys[r];
    }

    *state = x;
}


/* --------------------------------------------------------------------------
 * ECB encryption — process a full payload as independent 8-byte blocks
 * Buffer modified in-place. msg_len must be a multiple of BLOCK_SIZE_BYTES.
 * ------------------------------------------------------------------------- */
void present_encrypt_ecb(uint8_t *buf,
                         uint32_t msg_len,
                         const uint64_t subKeys[32])
{
    uint32_t num_blocks = msg_len / BLOCK_SIZE_BYTES;

    for (uint32_t b = 0; b < num_blocks; b++)
    {
        /* Load 8 bytes as big-endian 64-bit block */
        uint64_t block =
            ((uint64_t)buf[b*8+0] << 56) | ((uint64_t)buf[b*8+1] << 48) |
            ((uint64_t)buf[b*8+2] << 40) | ((uint64_t)buf[b*8+3] << 32) |
            ((uint64_t)buf[b*8+4] << 24) | ((uint64_t)buf[b*8+5] << 16) |
            ((uint64_t)buf[b*8+6] <<  8) | ((uint64_t)buf[b*8+7]      );

        present_encrypt(&block, subKeys);

        buf[b*8+0] = (uint8_t)(block >> 56);
        buf[b*8+1] = (uint8_t)(block >> 48);
        buf[b*8+2] = (uint8_t)(block >> 40);
        buf[b*8+3] = (uint8_t)(block >> 32);
        buf[b*8+4] = (uint8_t)(block >> 24);
        buf[b*8+5] = (uint8_t)(block >> 16);
        buf[b*8+6] = (uint8_t)(block >>  8);
        buf[b*8+7] = (uint8_t)(block      );
    }
}

/* --------------------------------------------------------------------------
 * ECB decryption — mirrors present_encrypt_ecb() exactly
 * ------------------------------------------------------------------------- */
void present_decrypt_ecb(uint8_t *buf,
                         uint32_t msg_len,
                         const uint64_t subKeys[32])
{
    uint32_t num_blocks = msg_len / BLOCK_SIZE_BYTES;

    for (uint32_t b = 0; b < num_blocks; b++)
    {
        uint64_t block =
            ((uint64_t)buf[b*8+0] << 56) | ((uint64_t)buf[b*8+1] << 48) |
            ((uint64_t)buf[b*8+2] << 40) | ((uint64_t)buf[b*8+3] << 32) |
            ((uint64_t)buf[b*8+4] << 24) | ((uint64_t)buf[b*8+5] << 16) |
            ((uint64_t)buf[b*8+6] <<  8) | ((uint64_t)buf[b*8+7]      );

        present_decrypt(&block, subKeys);

        buf[b*8+0] = (uint8_t)(block >> 56);
        buf[b*8+1] = (uint8_t)(block >> 48);
        buf[b*8+2] = (uint8_t)(block >> 40);
        buf[b*8+3] = (uint8_t)(block >> 32);
        buf[b*8+4] = (uint8_t)(block >> 24);
        buf[b*8+5] = (uint8_t)(block >> 16);
        buf[b*8+6] = (uint8_t)(block >>  8);
        buf[b*8+7] = (uint8_t)(block      );
    }
}


static void print_metrics(uint64_t cycles_total, uint32_t msg_len)
{
    double avg_cycles     = (double)cycles_total / NUM_RUNS;
    double exec_time_sec  = avg_cycles / CPU_FREQ;
    double exec_time_us   = exec_time_sec * 1e6;
    double cycles_per_byte = avg_cycles / (double)msg_len;
    double throughput     = ((double)msg_len / exec_time_sec) / 1024.0;
    double energy_op      = SUPPLY_VOLTAGE * ACTIVE_CURRENT * exec_time_sec * 1e6;
    double energy_per_byte = energy_op / (double)msg_len;

    printf("  Avg Cycles     : %.2f\r\n",         avg_cycles);
    printf("  Exec Time      : %.4f us\r\n",      exec_time_us);
    printf("  Cycles/Byte    : %.3f\r\n",         cycles_per_byte);
    printf("  Throughput     : %.3f KB/s\r\n",    throughput);
    printf("  Energy/Op      : %.6f uJ\r\n",      energy_op);
    printf("  Energy/Byte    : %.6f uJ/byte\r\n", energy_per_byte);
}


/* ===== END PRESENT cipher code ===== */

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
  /* USER CODE BEGIN 2 */
  setvbuf(stdout, NULL, _IONBF, 0);






  	  uint64_t keyHigh = 0xFFFFFFFFFFFFFFFFULL;
      uint64_t keyLow  = 0xFFFFFFFFFFFFFFFFULL;

      uint64_t roundKeys[32];
      generateSubkeys128(keyHigh, keyLow, roundKeys);

      /* Working buffers — sized for largest payload (1024 bytes)              */
      static uint8_t plaintext_master[1024];  /* original plaintext (0xFF)    */
      static uint8_t enc_buf[1024];           /* scratch for encryption run   */
      static uint8_t cipher_buf[1024];        /* stores ciphertext for dec run*/
      static uint8_t dec_buf[1024];           /* scratch for decryption run   */

      memset(plaintext_master, 0xFF, sizeof(plaintext_master));

      	  printf("\r\n");
          printf("============================================================\r\n");
          printf("  Algorithm  : PRESENT-128 (ECB Mode)\r\n");
          printf("  Key Size   : 128 bits (fixed)\r\n");
          printf("Message Size: %d bytes\r\n", MESSAGE_SIZE_BYTES);
          printf("  Clock Freq : %.lu Hz\r\n", HAL_RCC_GetSysClockFreq());
          printf("  Num Runs   : %d (averaged)\r\n", NUM_RUNS);

          printf("============================================================\r\n");


          	  	  uint32_t msg_len    = MESSAGE_SIZE_BYTES;
                  uint32_t num_blocks = msg_len / BLOCK_SIZE_BYTES;

                  uint32_t cyc_start, cyc_end;

                  /* ===== ENCRYPTION BENCHMARK ===================================== */
                  uint64_t enc_cycles_total = 0;

                  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);

                  for (int run = 0; run < NUM_RUNS; run++)
                  {
                      /* Restore fresh plaintext before each run */
                      memcpy(enc_buf, plaintext_master, msg_len);

                      /* --- Timed region --- */
                      DWT->CYCCNT = 0;
                      cyc_start   = DWT->CYCCNT;

                      present_encrypt_ecb(enc_buf, msg_len, roundKeys);

                      cyc_end = DWT->CYCCNT;
                      /* ------------------- */

                      enc_cycles_total += (uint64_t)(cyc_end - cyc_start);
                  }

                  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

                  /* Save ciphertext from last encryption run for decryption benchmark */
                  memcpy(cipher_buf, enc_buf, msg_len);

                  /* ===== DECRYPTION BENCHMARK ===================================== */
                  uint64_t dec_cycles_total = 0;

                  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);

                  for (int run = 0; run < NUM_RUNS; run++)
                  {
                      /* Restore fresh ciphertext before each run */
                      memcpy(dec_buf, cipher_buf, msg_len);

                      /* --- Timed region --- */
                      DWT->CYCCNT = 0;
                      cyc_start   = DWT->CYCCNT;

                      present_decrypt_ecb(dec_buf, msg_len, roundKeys);

                      cyc_end = DWT->CYCCNT;
                      /* ------------------- */

                      dec_cycles_total += (uint64_t)(cyc_end - cyc_start);
                  }

                  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

                  /* ===== PRINT RESULTS ============================================= */
                  printf("\r\n");
                  printf("------------------------------------------------------------\r\n");
                  printf("  Payload : %4" PRIu32 " bytes  |  %2" PRIu32
                         " blocks x 8 bytes\r\n", msg_len, num_blocks);
                  printf("------------------------------------------------------------\r\n");

                  printf("  [ Encryption ]\r\n");
                  print_metrics(enc_cycles_total, msg_len);

                  printf("  [ Decryption ]\r\n");
                  print_metrics(dec_cycles_total, msg_len);



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
/* USER CODE BEGIN 4 */

int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

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
