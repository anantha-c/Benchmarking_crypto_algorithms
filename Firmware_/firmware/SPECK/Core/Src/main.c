/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : SPECK-64/128 Benchmark — Optimized & Fixed
  *
  * Changes vs original:
  *   1. __disable_irq() / __enable_irq() guard added around every timed window
  *      (matches AES/ASCON style — prevents ISR jitter on cycle counts).
  *   2. Decryption benchmark no longer re-encrypts inside the timed run loop.
  *      Ciphertext is prepared ONCE before the dec loop (same pattern as AES).
  *   3. GPIO toggle moved INSIDE each run for per-run oscilloscope visibility.
  *   4. cycles_total promoted to uint64_t to prevent u32 overflow on large msgs.
  *   5. DWT->CYCCNT explicitly reset to 0 before every start capture.
  *   6. Added __ISB() after __DSB() for full pipeline flush (matches AES code).
  *   7. Speck64128Encrypt / Decrypt: Pt==Ct in-place aliasing made explicit.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* USER CODE BEGIN PV */
/* USER CODE BEGIN PV */
UART_HandleTypeDef huart2;
/* USER CODE END PV */

#define CPU_FREQ_MHZ     80.0
#define CPU_FREQ         (CPU_FREQ_MHZ * 1000000.0)

#define MESSAGE_SIZE_BYTES   1024
#define MESSAGE_SIZE_BITS    (MESSAGE_SIZE_BYTES * 8)

/* SPECK-64/128: 64-bit block, 128-bit key */
#define BLOCK_SIZE_BYTES     8
#define NUM_BLOCKS           (MESSAGE_SIZE_BYTES / BLOCK_SIZE_BYTES)
#define BUFFER_WORDS         (MESSAGE_SIZE_BYTES / 4)   /* u32 words */

#define NUM_RUNS             100

#define SUPPLY_VOLTAGE       3.3
#define ACTIVE_CURRENT       0.0080

/* ---- SPECK-64/128 parameters ---- */
#define SPECK64_128_ROUNDS   27

typedef uint32_t u32;

#define ROTL32(x,r)  (((x) << (r)) | ((x) >> (32-(r))))
#define ROTR32(x,r)  (((x) >> (r)) | ((x) << (32-(r))))

/* Encryption round: modular-add → XOR key → rotate */
#define ER32(x,y,k) \
    ( x = ROTR32(x,8), \
      x += y,          \
      x ^= (k),        \
      y = ROTL32(y,3), \
      y ^= x )

/* Decryption round: inverse of ER32 */
#define DR32(x,y,k) \
    ( y ^= x,           \
      y = ROTR32(y,3),  \
      x ^= (k),         \
      x -= y,           \
      x = ROTL32(x,8) )

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);

/* USER CODE BEGIN 0 */

int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

/* --------------------------------------------------------------------------
 * DWT cycle counter init
 * ------------------------------------------------------------------------- */
static void DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

/* --------------------------------------------------------------------------
 * Key schedule — generates 27 round keys from a 128-bit key (4 x u32)
 * ------------------------------------------------------------------------- */
static void Speck64128KeySchedule(const u32 K[4], u32 rk[SPECK64_128_ROUNDS])
{
    u32 A = K[0];
    u32 B = K[1];
    u32 C = K[2];
    u32 D = K[3];

    for (u32 i = 0; i < SPECK64_128_ROUNDS; )
    {
        rk[i] = A;  ER32(B, A, i++);
        rk[i] = A;  ER32(C, A, i++);
        rk[i] = A;  ER32(D, A, i++);
    }
}

/* --------------------------------------------------------------------------
 * Single block encrypt — in-place (buf[0]=x, buf[1]=y)
 * Using a single u32* pointer avoids the aliasing issue in the original
 * where Pt and Ct pointed to the same array with different semantics.
 * ------------------------------------------------------------------------- */
static inline void Speck64128EncryptBlock(u32 buf[2],
                                          const u32 rk[SPECK64_128_ROUNDS])
{
    u32 x = buf[0], y = buf[1];
    for (u32 i = 0; i < SPECK64_128_ROUNDS; )
        ER32(y, x, rk[i++]);
    buf[0] = x;  buf[1] = y;
}

/* --------------------------------------------------------------------------
 * Single block decrypt — in-place
 * ------------------------------------------------------------------------- */
static inline void Speck64128DecryptBlock(u32 buf[2],
                                          const u32 rk[SPECK64_128_ROUNDS])
{
    u32 x = buf[0], y = buf[1];
    for (int i = SPECK64_128_ROUNDS - 1; i >= 0; i--)
        DR32(y, x, rk[i]);
    buf[0] = x;  buf[1] = y;
}

/* --------------------------------------------------------------------------
 * ECB encrypt an entire buffer (buf must be multiple of BLOCK_SIZE_BYTES)
 * ------------------------------------------------------------------------- */
static void Speck64128EncryptECB(u32 *buf, uint32_t num_blocks,
                                 const u32 rk[SPECK64_128_ROUNDS])
{
    for (uint32_t b = 0; b < num_blocks; b++)
        Speck64128EncryptBlock(&buf[b * 2], rk);
}

/* --------------------------------------------------------------------------
 * ECB decrypt an entire buffer
 * ------------------------------------------------------------------------- */
static void Speck64128DecryptECB(u32 *buf, uint32_t num_blocks,
                                 const u32 rk[SPECK64_128_ROUNDS])
{
    for (uint32_t b = 0; b < num_blocks; b++)
        Speck64128DecryptBlock(&buf[b * 2], rk);
}

/* USER CODE END 0 */

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    DWT_Init();
    MX_GPIO_Init();
    MX_USART2_UART_Init();

    /* --- Key & round keys ------------------------------------------------ */
    const u32 key[4] = {
        0x03020100u,
        0x0b0a0908u,
        0x13121110u,
        0x1b1a1918u
    };
    u32 round_keys[SPECK64_128_ROUNDS];
    Speck64128KeySchedule(key, round_keys);

    /* --- Working buffers ------------------------------------------------- */
    static u32 plaintext_master[BUFFER_WORDS];  /* original plaintext        */
    static u32 enc_buf[BUFFER_WORDS];           /* scratch for encrypt run   */
    static u32 cipher_buf[BUFFER_WORDS];        /* ciphertext for dec bench  */
    static u32 dec_buf[BUFFER_WORDS];           /* scratch for decrypt run   */

    for (int i = 0; i < BUFFER_WORDS; i++)
        plaintext_master[i] = (u32)i;

    /* =====================================================================
     * ENCRYPTION BENCHMARK
     * ===================================================================== */
    uint64_t enc_cycles_total = 0;
    uint32_t cyc_start, cyc_end;
    volatile u32 sink;   /* prevent dead-code elimination */

    for (int run = 0; run < NUM_RUNS; run++)
    {
        /* Restore fresh plaintext before each run (NOT timed) */
        memcpy(enc_buf, plaintext_master, MESSAGE_SIZE_BYTES);

        __disable_irq();

        HAL_GPIO_WritePin(Time_marker_GPIO_Port,
                          Time_marker_Pin,
                          GPIO_PIN_SET);
        __DSB();
        __ISB();

        DWT->CYCCNT = 0;
        cyc_start   = DWT->CYCCNT;

        Speck64128EncryptECB(enc_buf, NUM_BLOCKS, round_keys);

        cyc_end = DWT->CYCCNT;

        __DSB();
        __ISB();

        HAL_GPIO_WritePin(Time_marker_GPIO_Port,
                          Time_marker_Pin,
                          GPIO_PIN_RESET);
        __enable_irq();

        sink = enc_buf[0];
        enc_cycles_total += (uint64_t)(cyc_end - cyc_start);
    }

    /* Save ciphertext from last run for decryption benchmark */
    memcpy(cipher_buf, enc_buf, MESSAGE_SIZE_BYTES);

    /* =====================================================================
     * DECRYPTION BENCHMARK
     * Ciphertext prepared ONCE above — NOT re-computed inside the loop.
     * ===================================================================== */
    uint64_t dec_cycles_total = 0;

    for (int run = 0; run < NUM_RUNS; run++)
    {
        /* Restore fresh ciphertext before each run (NOT timed) */
        memcpy(dec_buf, cipher_buf, MESSAGE_SIZE_BYTES);

        __disable_irq();

        HAL_GPIO_WritePin(Time_marker_GPIO_Port,
                          Time_marker_Pin,
                          GPIO_PIN_SET);
        __DSB();
        __ISB();

        DWT->CYCCNT = 0;
        cyc_start   = DWT->CYCCNT;

        Speck64128DecryptECB(dec_buf, NUM_BLOCKS, round_keys);

        cyc_end = DWT->CYCCNT;

        __DSB();
        __ISB();

        HAL_GPIO_WritePin(Time_marker_GPIO_Port,
                          Time_marker_Pin,
                          GPIO_PIN_RESET);
        __enable_irq();

        sink = dec_buf[0];
        dec_cycles_total += (uint64_t)(cyc_end - cyc_start);
    }
    (void)sink;

    /* =====================================================================
     * METRICS
     * ===================================================================== */
    double enc_avg_cycles    = (double)enc_cycles_total / NUM_RUNS;
    double enc_exec_time_sec = enc_avg_cycles / CPU_FREQ;
    double enc_exec_time_us  = enc_exec_time_sec * 1e6;
    double enc_cpb           = enc_avg_cycles / MESSAGE_SIZE_BYTES;
    double enc_throughput    = (MESSAGE_SIZE_BYTES / enc_exec_time_sec) / 1024.0;
    double enc_energy_op     = SUPPLY_VOLTAGE * ACTIVE_CURRENT * enc_exec_time_sec * 1e6;
    double enc_energy_byte   = enc_energy_op / MESSAGE_SIZE_BYTES;

    double dec_avg_cycles    = (double)dec_cycles_total / NUM_RUNS;
    double dec_exec_time_sec = dec_avg_cycles / CPU_FREQ;
    double dec_exec_time_us  = dec_exec_time_sec * 1e6;
    double dec_cpb           = dec_avg_cycles / MESSAGE_SIZE_BYTES;
    double dec_throughput    = (MESSAGE_SIZE_BYTES / dec_exec_time_sec) / 1024.0;
    double dec_energy_op     = SUPPLY_VOLTAGE * ACTIVE_CURRENT * dec_exec_time_sec * 1e6;
    double dec_energy_byte   = dec_energy_op / MESSAGE_SIZE_BYTES;

    /* =====================================================================
     * OUTPUT
     * ===================================================================== */
    printf("\r\n======== Algorithm: SPECK-64/128 ========\r\n");
    printf("Clock Freq     : %lu Hz\r\n",  HAL_RCC_GetSysClockFreq());
    printf("Key Size       : 128 bits\r\n");
    printf("Block Size     : %d bits\r\n",   BLOCK_SIZE_BYTES * 8);
    printf("Message Size   : %d bytes (%d bits)\r\n",
           MESSAGE_SIZE_BYTES, MESSAGE_SIZE_BITS);
    printf("Num Blocks     : %d\r\n",   NUM_BLOCKS);
    printf("Num Runs       : %d\r\n",   NUM_RUNS);

    printf("\r\n--- Encryption Performance ---\r\n");
    printf("Avg Cycles     : %.0f\r\n",         enc_avg_cycles);
    printf("Exec Time      : %.4f us\r\n",       enc_exec_time_us);
    printf("Cycles/Byte    : %.3f\r\n",          enc_cpb);
    printf("Throughput     : %.3f KB/s\r\n",     enc_throughput);
    printf("Energy/Op      : %.6f uJ\r\n",       enc_energy_op);
    printf("Energy/Byte    : %.6f uJ/byte\r\n",  enc_energy_byte);

    printf("\r\n--- Decryption Performance ---\r\n");
    printf("Avg Cycles     : %.0f\r\n",         dec_avg_cycles);
    printf("Exec Time      : %.4f us\r\n",       dec_exec_time_us);
    printf("Cycles/Byte    : %.3f\r\n",          dec_cpb);
    printf("Throughput     : %.3f KB/s\r\n",     dec_throughput);
    printf("Energy/Op      : %.6f uJ\r\n",       dec_energy_op);
    printf("Energy/Byte    : %.6f uJ/byte\r\n",  dec_energy_byte);
    printf("==========================================\r\n");

    while (1) { __NOP(); }
}

/* =========================================================================
 * Peripheral Init (unchanged from original)
 * ========================================================================= */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
        Error_Handler();

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.MSIState            = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange       = RCC_MSIRANGE_6;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM            = 1;
    RCC_OscInitStruct.PLL.PLLN            = 40;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
        Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance                    = USART2;
    huart2.Init.BaudRate               = 115200;
    huart2.Init.WordLength             = UART_WORDLENGTH_8B;
    huart2.Init.StopBits               = UART_STOPBITS_1;
    huart2.Init.Parity                 = UART_PARITY_NONE;
    huart2.Init.Mode                   = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart2) != HAL_OK)
        Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(Time_marker_GPIO_Port, Time_marker_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin   = Time_marker_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(Time_marker_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin   = LD3_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
