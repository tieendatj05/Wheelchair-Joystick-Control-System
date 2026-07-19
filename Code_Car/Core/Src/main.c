/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32 NRF24 RX + L298 + ULTRASONIC
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"
#include <string.h>
#include <stdlib.h>

/* ================= NRF24 PIN ================= */
#define NRF_CE_PORT   GPIOB
#define NRF_CE_PIN    GPIO_PIN_0
#define NRF_CSN_PORT  GPIOB
#define NRF_CSN_PIN   GPIO_PIN_1

#define CE_HIGH()  HAL_GPIO_WritePin(NRF_CE_PORT, NRF_CE_PIN, GPIO_PIN_SET)
#define CE_LOW()   HAL_GPIO_WritePin(NRF_CE_PORT, NRF_CE_PIN, GPIO_PIN_RESET)
#define CSN_HIGH() HAL_GPIO_WritePin(NRF_CSN_PORT, NRF_CSN_PIN, GPIO_PIN_SET)
#define CSN_LOW()  HAL_GPIO_WritePin(NRF_CSN_PORT, NRF_CSN_PIN, GPIO_PIN_RESET)

/* ================= NRF24 CMD ================= */
#define R_REGISTER     0x00
#define R_RX_PAYLOAD   0x61
#define W_REGISTER     0x20
#define STATUS         0x07
#define RX_ADDR_P0     0x0A
#define RX_PW_P0       0x11
#define CONFIG         0x00
#define RF_CH          0x05
#define RF_SETUP       0x06
#define EN_RXADDR      0x02
#define EN_AA          0x01

/* ================= MOTOR ================= */
#define IN1_PORT GPIOB
#define IN1_PIN  GPIO_PIN_8
#define IN2_PORT GPIOA
#define IN2_PIN  GPIO_PIN_4
#define IN3_PORT GPIOA
#define IN3_PIN  GPIO_PIN_3
#define IN4_PORT GPIOA
#define IN4_PIN  GPIO_PIN_2

/* ================= ULTRASONIC ================= */
#define TRIG_PORT GPIOB
#define TRIG_PIN  GPIO_PIN_11
#define ECHO_PORT GPIOB
#define ECHO_PIN  GPIO_PIN_10

#define CENTER    500
#define DEADZONE  100

/* ================= DATA ================= */
typedef struct {
  int16_t x;
  int16_t y;
  uint8_t sw;
} DataPacket_t;

DataPacket_t rxData;

/* ================= SPI ================= */
uint8_t NRF_SPI_RW(uint8_t data)
{
  uint8_t rx;
  HAL_SPI_TransmitReceive(&hspi1, &data, &rx, 1, HAL_MAX_DELAY);
  return rx;
}

/* ================= NRF ================= */
void NRF_WriteReg(uint8_t reg, uint8_t val)
{
  CSN_LOW();
  NRF_SPI_RW(W_REGISTER | reg);
  NRF_SPI_RW(val);
  CSN_HIGH();
}

void NRF_WriteAddr(uint8_t reg, uint8_t *addr)
{
  CSN_LOW();
  NRF_SPI_RW(W_REGISTER | reg);
  for (int i = 0; i < 5; i++) NRF_SPI_RW(addr[i]);
  CSN_HIGH();
}

void NRF_RX_Init(void)
{
  uint8_t addr[5] = {0x57,0x48,0x4C,0x30,0x31}; // WHL01

  CE_LOW();
  CSN_HIGH();
  HAL_Delay(5);

  NRF_WriteReg(CONFIG, 0x0F); // RX | PWR_UP | CRC
  NRF_WriteReg(EN_AA, 0x01);
  NRF_WriteReg(EN_RXADDR, 0x01);
  NRF_WriteReg(RF_CH, 80);
  NRF_WriteReg(RF_SETUP, 0x26);
  NRF_WriteReg(RX_PW_P0, sizeof(DataPacket_t));
  NRF_WriteAddr(RX_ADDR_P0, addr);

  CE_HIGH();
}

uint8_t NRF_DataAvailable(void)
{
  CSN_LOW();
  uint8_t status = NRF_SPI_RW(R_REGISTER | STATUS);
  CSN_HIGH();
  return (status & 0x40);
}

void NRF_ReadPayload(uint8_t *buf, uint8_t len)
{
  CSN_LOW();
  NRF_SPI_RW(R_RX_PAYLOAD);
  for (int i = 0; i < len; i++) buf[i] = NRF_SPI_RW(0xFF);
  CSN_HIGH();
}

/* ================= MOTOR ================= */
void Motor_Stop(void)
{
  HAL_GPIO_WritePin(IN1_PORT, IN1_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(IN2_PORT, IN2_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(IN3_PORT, IN3_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(IN4_PORT, IN4_PIN, GPIO_PIN_RESET);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0);
}

/* ================= ULTRASONIC ================= */
uint32_t Read_Ultrasonic_CM(void)
{
  HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_SET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET);

  uint32_t t = 0;
  while (!HAL_GPIO_ReadPin(ECHO_PORT, ECHO_PIN));
  while (HAL_GPIO_ReadPin(ECHO_PORT, ECHO_PIN))
  {
    t++;
    HAL_Delay(1);
    if (t > 300) return 999;
  }
  return t * 1.7; // ~cm
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

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
/* ================= MAIN ================= */
int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_TIM2_Init();

  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);

  NRF_RX_Init();
  Motor_Stop();

 uint32_t lastRxTick = HAL_GetTick();

	while (1)
	{
			/* ================= FAILSAFE ================= */
			if (HAL_GetTick() - lastRxTick > 500)
			{
					Motor_Stop();   // mot song > 500ms
					continue;
			}

			/* ================= NRF RX ================= */
			if (!NRF_DataAvailable())
					continue;

			NRF_ReadPayload((uint8_t*)&rxData, sizeof(rxData));
			lastRxTick = HAL_GetTick();

			int x = rxData.y;   // tien / lui
			int y = rxData.x;   // re

			int speed = abs(x - CENTER);
			speed = (speed * 1000) / 2048;
			if (speed > 1000) speed = 1000;

			/* ================= QUAY TAI CHO ================= */
			if (abs(x - CENTER) <= DEADZONE && abs(y - CENTER) > DEADZONE)
			{
					int spin = abs(y - CENTER);
					spin = (spin * 800) / 2048;
					if (spin > 800) spin = 800;

					if (y > CENTER)
					{
							// quay phai
							HAL_GPIO_WritePin(IN1_PORT, IN1_PIN, GPIO_PIN_SET);
							HAL_GPIO_WritePin(IN2_PORT, IN2_PIN, GPIO_PIN_RESET);
							HAL_GPIO_WritePin(IN3_PORT, IN3_PIN, GPIO_PIN_RESET);
							HAL_GPIO_WritePin(IN4_PORT, IN4_PIN, GPIO_PIN_SET);
					}
					else
					{
							// quay trai
							HAL_GPIO_WritePin(IN1_PORT, IN1_PIN, GPIO_PIN_RESET);
							HAL_GPIO_WritePin(IN2_PORT, IN2_PIN, GPIO_PIN_SET);
							HAL_GPIO_WritePin(IN3_PORT, IN3_PIN, GPIO_PIN_SET);
							HAL_GPIO_WritePin(IN4_PORT, IN4_PIN, GPIO_PIN_RESET);
					}

					__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, spin);
					__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, spin);
					continue;
			}

			/* ================= DUNG ================= */
			if (abs(x - CENTER) <= DEADZONE)
			{
					Motor_Stop();
					continue;
			}

			/* ================= TIEN / LUI ================= */
			if (x > CENTER + DEADZONE)
			{
					/* TIEN (CHAN) */
					if (Read_Ultrasonic_CM() < 20)
					{
							Motor_Stop();
							continue;
					}

					// tien
					HAL_GPIO_WritePin(IN1_PORT, IN1_PIN, GPIO_PIN_SET);
					HAL_GPIO_WritePin(IN2_PORT, IN2_PIN, GPIO_PIN_RESET);
					HAL_GPIO_WritePin(IN3_PORT, IN3_PIN, GPIO_PIN_SET);
					HAL_GPIO_WritePin(IN4_PORT, IN4_PIN, GPIO_PIN_RESET);
			}
			else if (x < CENTER - DEADZONE)
			{
					// LUI (KHONG CHAN)
					HAL_GPIO_WritePin(IN1_PORT, IN1_PIN, GPIO_PIN_RESET);
					HAL_GPIO_WritePin(IN2_PORT, IN2_PIN, GPIO_PIN_SET);
					HAL_GPIO_WritePin(IN3_PORT, IN3_PIN, GPIO_PIN_RESET);
					HAL_GPIO_WritePin(IN4_PORT, IN4_PIN, GPIO_PIN_SET);
			}

			/* ================= RE ================= */
			int leftSpeed = speed;
			int rightSpeed = speed;

			if (y > CENTER + DEADZONE)       // re phai
					rightSpeed -= 200;
			else if (y < CENTER - DEADZONE)  // re trai
					leftSpeed -= 200;

			if (leftSpeed < 0) leftSpeed = 0;
			if (rightSpeed < 0) rightSpeed = 0;

			__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, leftSpeed);
			__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, rightSpeed);
	}

}
