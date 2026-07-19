/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   NRF24 TX STM32 (NO LIB)
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "spi.h"
#include "adc.h"
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
#define W_REGISTER     0x20
#define W_TX_PAYLOAD   0xA0
#define FLUSH_TX       0xE1
#define NOP            0xFF

/* ================= NRF24 REG ================= */
#define CONFIG         0x00
#define EN_AA          0x01
#define EN_RXADDR      0x02
#define SETUP_RETR     0x04
#define RF_CH          0x05
#define RF_SETUP       0x06
#define STATUS         0x07
#define TX_ADDR        0x10
#define RX_ADDR_P0     0x0A

/* ================= DATA ================= */
typedef struct {
  int16_t x;
  int16_t y;
  uint8_t sw;
} DataPacket_t;

DataPacket_t txData;
int16_t lastX = -1;
int16_t lastY = -1;
uint8_t lastSW = 1;

#define ADC_THRESHOLD 10

/* ================= SPI RW ================= */
uint8_t NRF_SPI_RW(uint8_t data)
{
  uint8_t rx;
  HAL_SPI_TransmitReceive(&hspi1, &data, &rx, 1, HAL_MAX_DELAY);
  return rx;
}

/* ================= NRF BASIC ================= */
void NRF_WriteReg(uint8_t reg, uint8_t value)
{
  CSN_LOW();
  NRF_SPI_RW(W_REGISTER | reg);
  NRF_SPI_RW(value);
  CSN_HIGH();
}

void NRF_WriteAddr(uint8_t reg, uint8_t *addr)
{
  CSN_LOW();
  NRF_SPI_RW(W_REGISTER | reg);
  for (int i = 0; i < 5; i++)
    NRF_SPI_RW(addr[i]);
  CSN_HIGH();
}

void NRF_FlushTX(void)
{
  CSN_LOW();
  NRF_SPI_RW(FLUSH_TX);
  CSN_HIGH();
}

/* ================= NRF TX INIT ================= */
void NRF24_TX_Init(void)
{
  uint8_t addr[5] = {0x57,0x48,0x4C,0x30,0x31}; // WHL01

  CE_LOW();
  CSN_HIGH();
  HAL_Delay(5);

  NRF_WriteReg(CONFIG, 0x0E);     // PWR_UP | CRC | TX
  NRF_WriteReg(EN_AA, 0x01);
  NRF_WriteReg(EN_RXADDR, 0x01);
  NRF_WriteReg(SETUP_RETR, 0x2F);
  NRF_WriteReg(RF_CH, 80);
  NRF_WriteReg(RF_SETUP, 0x26);   // 250kbps

  NRF_WriteAddr(TX_ADDR, addr);
  NRF_WriteAddr(RX_ADDR_P0, addr);

  NRF_FlushTX();

  CE_HIGH();
  HAL_Delay(2);
}

/* ================= SEND ================= */
void NRF_Send(uint8_t *data, uint8_t len)
{
  CE_LOW();
  CSN_LOW();
  NRF_SPI_RW(W_TX_PAYLOAD);
  for (int i = 0; i < len; i++)
    NRF_SPI_RW(data[i]);
  CSN_HIGH();

  CE_HIGH();
  HAL_Delay(1);
  CE_LOW();
}

/* ================= ADC READ ================= */
uint16_t ReadADC(uint32_t channel)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Channel = channel;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);

  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
  uint16_t val = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);

  return val;
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the CPU, AHB and APB buses clocks */
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

  RCC_ClkInitStruct.ClockType =
      RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;

  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
} 

/* ================= MAIN ================= */
int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_ADC1_Init();

  NRF24_TX_Init();

  while (1)
  {
    int16_t curX = ReadADC(ADC_CHANNEL_3); // PA3
    int16_t curY = ReadADC(ADC_CHANNEL_4); // PA4
    uint8_t curSW = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2);

    uint8_t needSend = 0;
    if (abs(curX - lastX) >= ADC_THRESHOLD) needSend = 1;
    if (abs(curY - lastY) >= ADC_THRESHOLD) needSend = 1;
    if (curSW != lastSW) needSend = 1;

    if (needSend)
    {
      txData.x = curX;
      txData.y = curY;
      txData.sw = curSW;

      NRF_Send((uint8_t*)&txData, sizeof(txData));

      lastX = curX;
      lastY = curY;
      lastSW = curSW;
    }

    HAL_Delay(20); // Cho 20ms
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
    //
  }
}
