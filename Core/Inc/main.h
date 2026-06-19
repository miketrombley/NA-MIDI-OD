/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define POT3_Pin GPIO_PIN_0
#define POT3_GPIO_Port GPIOC
#define POT6_Pin GPIO_PIN_1
#define POT6_GPIO_Port GPIOC
#define POT5_Pin GPIO_PIN_2
#define POT5_GPIO_Port GPIOC
#define POT2_Pin GPIO_PIN_3
#define POT2_GPIO_Port GPIOC
#define POT1_Pin GPIO_PIN_0
#define POT1_GPIO_Port GPIOA
#define POT4_Pin GPIO_PIN_1
#define POT4_GPIO_Port GPIOA
#define ADC_EXPRESSION_Pin GPIO_PIN_2
#define ADC_EXPRESSION_GPIO_Port GPIOA
#define SW_2_Pin GPIO_PIN_4
#define SW_2_GPIO_Port GPIOA
#define SW_1_Pin GPIO_PIN_5
#define SW_1_GPIO_Port GPIOA
#define LED2_R_Pin GPIO_PIN_6
#define LED2_R_GPIO_Port GPIOA
#define LED2_G_Pin GPIO_PIN_7
#define LED2_G_GPIO_Port GPIOA
#define LED2_B_Pin GPIO_PIN_0
#define LED2_B_GPIO_Port GPIOB
#define LED1_R_Pin GPIO_PIN_1
#define LED1_R_GPIO_Port GPIOB
#define LED1_G_Pin GPIO_PIN_10
#define LED1_G_GPIO_Port GPIOB
#define LED1_B_Pin GPIO_PIN_11
#define LED1_B_GPIO_Port GPIOB
#define SPI2_CS_Pin GPIO_PIN_12
#define SPI2_CS_GPIO_Port GPIOB
#define EXP_MIDI_CNTRL_Pin GPIO_PIN_8
#define EXP_MIDI_CNTRL_GPIO_Port GPIOA
#define JFET_BYPASS_Pin GPIO_PIN_15
#define JFET_BYPASS_GPIO_Port GPIOA
#define DPOT_SCK_Pin GPIO_PIN_10
#define DPOT_SCK_GPIO_Port GPIOC
#define DPOT_CS_Pin GPIO_PIN_11
#define DPOT_CS_GPIO_Port GPIOC
#define DPOT_MOSI_Pin GPIO_PIN_12
#define DPOT_MOSI_GPIO_Port GPIOC
#define VC_HPF1_Pin GPIO_PIN_3
#define VC_HPF1_GPIO_Port GPIOB
#define VCA_LPF1_Pin GPIO_PIN_6
#define VCA_LPF1_GPIO_Port GPIOB
#define VCA_GAIN_Pin GPIO_PIN_7
#define VCA_GAIN_GPIO_Port GPIOB
#define VCA_VOLUME_Pin GPIO_PIN_8
#define VCA_VOLUME_GPIO_Port GPIOB
#define VCA_LPF2_Pin GPIO_PIN_9
#define VCA_LPF2_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
