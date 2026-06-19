/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f1xx_it.c
  * @brief   Interrupt Service Routines.
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
#include "stm32f1xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "midi.h"   /* midi_rx_push() — USART1 RX feeds the MIDI ring buffer */
/* ctrl_smooth_tick() is declared in main.h (already included above). */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M3 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32F1xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f1xx.s).                    */
/******************************************************************************/

/* USER CODE BEGIN 1 */

/**
  * @brief USART1 global interrupt — MIDI RX.
  *
  * Hand-written handler: USART1's NVIC is enabled in code (main() USER CODE 2),
  * NOT in CubeMX, so this lives in a USER CODE block and there's no generated
  * handler to collide with on regen. We service the RXNE flag directly and push
  * each byte to the MIDI ring buffer (parsed later in midi_poll) — no
  * HAL_UART_Receive_IT. Reading DR clears RXNE; on overrun (ORE) reading DR also
  * clears it so the UART keeps receiving.
  *
  * FUTURE: to let CubeMX manage the vector instead, tick "USART1 global
  * interrupt" in the NVIC tab, regenerate, then move this RXNE service into the
  * generated USART1_IRQHandler's `USER CODE BEGIN USART1_IRQn 0` block followed
  * by `return;` (so the generated HAL_UART_IRQHandler call is skipped).
  */
void USART1_IRQHandler(void)
{
  uint32_t sr = USART1->SR;
  if (sr & (USART_SR_RXNE | USART_SR_ORE))
  {
    uint8_t b = (uint8_t)(USART1->DR & 0xFFu);   /* read DR -> clears RXNE/ORE */
    if (sr & USART_SR_RXNE)
      midi_rx_push(b);
  }
}

/**
  * @brief TIM7 update interrupt — control-smoothing tick (anti-zipper).
  *
  * Hand-written, like USART1 above: TIM7 is a basic timer brought up in code
  * (ctrl_smooth_start() in main.c USER CODE 2), NOT in CubeMX, so this handler
  * lives in a USER CODE block and there's no generated TIM7_IRQHandler to
  * collide with on regen. Fires at CTRL_SMOOTH_HZ; we clear the update flag and
  * run ctrl_smooth_tick() (main.c), which slews the VCA CVs + glides the bias
  * code. NVIC priority 7 keeps it below USART1 (6) so MIDI RX is never starved.
  *
  * Trade-off (same as USART1): because TIM7 is NOT in the .ioc, CubeMX thinks
  * it's free and could hand it to something else on a future regen. It's
  * documented in CLAUDE.md so that's unlikely, but if you'd rather have CubeMX
  * "reserve" + own the vector:
  *   1. In CubeMX enable TIM7, set the time base for CTRL_SMOOTH_HZ (PSC 71 /
  *      ARR 499 @ 72 MHz = 2 kHz), and tick "TIM7 global interrupt" in NVIC.
  *   2. Regenerate. CubeMX emits MX_TIM7_Init (+ its MspInit clock enable), the
  *      NVIC config, and a TIM7_IRQHandler that calls HAL_TIM_IRQHandler.
  *   3. Move the ctrl_smooth_tick() call into HAL_TIM_PeriodElapsedCallback
  *      (or the generated handler's USER CODE block) and DELETE the manual
  *      __HAL_RCC_TIM7_CLK_ENABLE / NVIC / htim7 setup in ctrl_smooth_start()
  *      — keep only HAL_TIM_Base_Start_IT(&htim7). Then delete this handler.
  */
void TIM7_IRQHandler(void)
{
  if (TIM7->SR & TIM_SR_UIF)
  {
    TIM7->SR = (uint16_t)~TIM_SR_UIF;   /* clear update flag (rc_w0) */
    ctrl_smooth_tick();
  }
}

/* USER CODE END 1 */
