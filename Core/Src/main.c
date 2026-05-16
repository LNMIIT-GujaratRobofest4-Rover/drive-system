/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RX_BUFFER_SIZE 64

#define CRSF_MIN 172
#define CRSF_VAL_1000 191
#define CRSF_MID 992
#define CRSF_VAL_2000 1792
#define CRSF_MAX 1811

#define CRSF_OUT_MIN -100
#define CRSF_OUT_MAX 100

#define TURNING_FACTOR 0.9
#define DEADBAND 5
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim8;

UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_rx;

/* USER CODE BEGIN PV */
#define TIM_LEFT htim1
#define TIM_LEFT_ADDR &htim1
#define TIM_RIGHT htim8
#define TIM_RIGHT_ADDR &htim8

#define CH_FRONT_LEFT TIM_CHANNEL_2
#define CH_CENTER_LEFT TIM_CHANNEL_3
#define CH_BACK_LEFT TIM_CHANNEL_4
#define CH_FRONT_RIGHT TIM_CHANNEL_1
#define CH_CENTER_RIGHT TIM_CHANNEL_2
#define CH_BACK_RIGHT TIM_CHANNEL_3

#define DIR_GPIO_CH GPIOC
#define DIR_FRONT_LEFT GPIO_PIN_0
#define DIR_CENTER_LEFT GPIO_PIN_1
#define DIR_BACK_LEFT GPIO_PIN_2
#define DIR_FRONT_RIGHT GPIO_PIN_3
#define DIR_CENTER_RIGHT GPIO_PIN_4
#define DIR_BACK_RIGHT GPIO_PIN_5

uint8_t rxBuffer[RX_BUFFER_SIZE]; // DMA buffer
uint8_t packetBuffer[RX_BUFFER_SIZE]; // Temporary buffer for packets
uint8_t packetIndex = 0; // Index for capturing packets
uint8_t expectedPacketLength = 0; // Expected full packet length

typedef struct {
	uint16_t values[16];
	uint32_t lastUpdateTime;
} RCChannels_t;

static RCChannels_t rcChannels = {0};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM8_Init(void);
/* USER CODE BEGIN PFP */
void parseCRSFPacket(uint8_t *packet, uint8_t length);
void parseRCChannels(uint8_t *payload);
int16_t mapCRSF(uint16_t val);

void _modify_gpio_left(GPIO_PinState state);
void _modify_gpio_right(GPIO_PinState state);
void _modify_gpio_all(GPIO_PinState state);
void _modify_pwm_left(uint16_t val);
void _modify_pwm_right(uint16_t val);
void _modify_pwm_all(uint16_t val);

//void rover_control(uint8_t dir, uint16_t speed, uint16_t side_speed);
void manual_mode();
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART1) {
		for (int i = 0; i < RX_BUFFER_SIZE; i++) {
			uint8_t byte = rxBuffer[i];

			if (packetIndex == 0) {
				if (byte == 0xC8) {  // Look for Sync Byte
					packetBuffer[packetIndex++] = byte;
				}
			} else if (packetIndex == 1) {
				expectedPacketLength = byte + 2; // Total packet size
				packetBuffer[packetIndex++] = byte;

				if (expectedPacketLength > RX_BUFFER_SIZE) {
					packetIndex = 0;
				}
			} else {
				packetBuffer[packetIndex++] = byte;

				if (packetIndex == expectedPacketLength) {
					parseCRSFPacket(packetBuffer, packetIndex);
					packetIndex = 0;
				}
			}
		}

    HAL_UART_Receive_DMA(&huart1, rxBuffer, RX_BUFFER_SIZE);
	}
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART1) {
		HAL_UART_Receive_DMA(&huart1, rxBuffer, RX_BUFFER_SIZE); // Restart DMA reception
	}
}

void parseCRSFPacket(uint8_t *packet, uint8_t length) {
	// Ensure packet has valid length
	if (length < 3) return;

	uint8_t type = packet[2];  // Type is at index 2
	uint8_t *payload = &packet[3];  // Payload starts at index 3

	switch (type) {
		case 0x16:  // RC Channels Data
			parseRCChannels(payload);
			break;
		default:

			break;
	}
}

void parseRCChannels(uint8_t *payload) {
	// Correct 11-bit extraction using bit shifts
	rcChannels.values[0]  = (payload[0]  | (payload[1]  << 8))  & 0x07FF;
	rcChannels.values[1]  = (payload[1]  >> 3 | (payload[2]  << 5))  & 0x07FF;
	rcChannels.values[2]  = (payload[2]  >> 6 | (payload[3]  << 2) | (payload[4]  << 10)) & 0x07FF;
	rcChannels.values[3]  = (payload[4]  >> 1 | (payload[5]  << 7))  & 0x07FF;
	rcChannels.values[4]  = (payload[5]  >> 4 | (payload[6]  << 4))  & 0x07FF;
	rcChannels.values[5]  = (payload[6]  >> 7 | (payload[7]  << 1) | (payload[8]  << 9)) & 0x07FF;
	rcChannels.values[6]  = (payload[8]  >> 2 | (payload[9]  << 6))  & 0x07FF;
	rcChannels.values[7]  = (payload[9]  >> 5 | (payload[10] << 3))  & 0x07FF;
	rcChannels.values[8]  = (payload[11] | (payload[12] << 8))  & 0x07FF;
	rcChannels.values[9]  = (payload[12] >> 3 | (payload[13] << 5))  & 0x07FF;
	rcChannels.values[10] = (payload[13] >> 6 | (payload[14] << 2) | (payload[15] << 10)) & 0x07FF;
	rcChannels.values[11] = (payload[15] >> 1 | (payload[16] << 7))  & 0x07FF;
	rcChannels.values[12] = (payload[16] >> 4 | (payload[17] << 4))  & 0x07FF;
	rcChannels.values[13] = (payload[17] >> 7 | (payload[18] << 1) | (payload[19] << 9)) & 0x07FF;
	rcChannels.values[14] = (payload[19] >> 2 | (payload[20] << 6))  & 0x07FF;
	rcChannels.values[15] = (payload[20] >> 5 | (payload[21] << 3))  & 0x07FF;

	rcChannels.lastUpdateTime = HAL_GetTick();
}

int16_t mapCRSF(uint16_t val) {
	if (val < CRSF_MIN) val = CRSF_MIN;
	if (val > CRSF_MAX) val = CRSF_MAX;

	return (int16_t)((val - CRSF_MIN) * (CRSF_OUT_MAX - CRSF_OUT_MIN) / (CRSF_MAX - CRSF_MIN) + CRSF_OUT_MIN);
}

void _modify_gpio_left(GPIO_PinState state) {
	HAL_GPIO_WritePin(DIR_GPIO_CH, DIR_FRONT_LEFT, state);
	HAL_GPIO_WritePin(DIR_GPIO_CH, DIR_CENTER_LEFT, state);
	HAL_GPIO_WritePin(DIR_GPIO_CH, DIR_BACK_LEFT, state);
}

void _modify_gpio_right(GPIO_PinState state) {
	HAL_GPIO_WritePin(DIR_GPIO_CH, DIR_FRONT_RIGHT, state);
	HAL_GPIO_WritePin(DIR_GPIO_CH, DIR_CENTER_RIGHT, state);
	HAL_GPIO_WritePin(DIR_GPIO_CH, DIR_BACK_RIGHT, state);
}

void _modify_gpio_all(GPIO_PinState state) {
	_modify_gpio_left(state);
	_modify_gpio_right(state);

}

void _modify_pwm_left(uint16_t val) {
	__HAL_TIM_SET_COMPARE(TIM_LEFT_ADDR, CH_FRONT_LEFT, val);
	__HAL_TIM_SET_COMPARE(TIM_LEFT_ADDR, CH_CENTER_LEFT, val);
	__HAL_TIM_SET_COMPARE(TIM_LEFT_ADDR, CH_BACK_LEFT, val);
}

void _modify_pwm_right(uint16_t val) {
	__HAL_TIM_SET_COMPARE(TIM_RIGHT_ADDR, CH_FRONT_RIGHT, val);
	__HAL_TIM_SET_COMPARE(TIM_RIGHT_ADDR, CH_CENTER_RIGHT, val);
	__HAL_TIM_SET_COMPARE(TIM_RIGHT_ADDR, CH_BACK_RIGHT, val);
}

void _modify_pwm_all(uint16_t val) {
	_modify_pwm_left(val);
	_modify_pwm_right(val);
}

void manual_mode() {
	int16_t y_map = mapCRSF(rcChannels.values[1]);
	int16_t x_map = mapCRSF(rcChannels.values[0]);
	int16_t s_map = mapCRSF(rcChannels.values[3]);

	if (abs(s_map) - DEADBAND > 0) { // Spin on its axis
		if (s_map > 0) { // right / clockwise spin
			_modify_gpio_left(GPIO_PIN_SET);
			_modify_gpio_right(GPIO_PIN_RESET);
		} else if (s_map < 0) { // left / anti-clockwise spin
			_modify_gpio_left(GPIO_PIN_RESET);
			_modify_gpio_right(GPIO_PIN_SET);
		}

		_modify_pwm_all(abs(s_map));
	} else { // normal movement
		if (abs(y_map) - DEADBAND > 0) {
					if (y_map > 0) _modify_gpio_all(GPIO_PIN_RESET);
					else if (y_map < 0) _modify_gpio_all(GPIO_PIN_SET);
				}

				if (abs(x_map) - DEADBAND > 0) {
					int16_t other = abs(y_map) - (abs(x_map) * TURNING_FACTOR);
					if (other < 0) other = 0;

					if (x_map > 0) {
						_modify_pwm_right(abs(y_map));
						_modify_pwm_left(other);

					} else if (x_map < 0) {
						_modify_pwm_left(abs(y_map));
												_modify_pwm_right(other);
					}
				} else _modify_pwm_all(abs(y_map));
	}
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

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_TIM1_Init();
  MX_TIM8_Init();
  /* USER CODE BEGIN 2 */
  HAL_UART_Receive_DMA(&huart1, rxBuffer, RX_BUFFER_SIZE);

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);  // Motor 1
	HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);  // Motor 2
	HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);  // Motor 3
	HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1);  // Motor 4
	HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_2);  // Motor 5
	HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_3);  // Motor 6

	_modify_gpio_all(GPIO_PIN_RESET);
	_modify_pwm_all(0);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
  	manual_mode();
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 168-1;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 100-1;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_ENABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM8 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM8_Init(void)
{

  /* USER CODE BEGIN TIM8_Init 0 */

  /* USER CODE END TIM8_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM8_Init 1 */

  /* USER CODE END TIM8_Init 1 */
  htim8.Instance = TIM8;
  htim8.Init.Prescaler = 168-1;
  htim8.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim8.Init.Period = 100-1;
  htim8.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim8.Init.RepetitionCounter = 0;
  htim8.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim8) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim8, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim8) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim8, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_ENABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim8, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim8, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim8, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim8, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM8_Init 2 */

  /* USER CODE END TIM8_Init 2 */
  HAL_TIM_MspPostInit(&htim8);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 420000;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

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
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC0 PC1 PC2 PC3
                           PC4 PC5 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

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
