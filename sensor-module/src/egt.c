#include "egt.h"

#include <string.h>
#include "board.h"
#include "config.h"

/* MAX31855 32 bit output, MSB first:
 *   D31..D18  14 bit signed hot junction temperature, 0.25 C per LSB
 *   D17       reserved
 *   D16       fault, set if any of D2..D0 are set
 *   D15..D4   12 bit signed cold junction temperature, 0.0625 C per LSB
 *   D3        reserved
 *   D2        short to VCC
 *   D1        short to ground
 *   D0        open circuit
 */
#define MAX31855_FAULT_BIT     (1UL << 16)
#define MAX31855_SCV_BIT       (1UL << 2)
#define MAX31855_SCG_BIT       (1UL << 1)
#define MAX31855_OC_BIT        (1UL << 0)

typedef struct {
  GPIO_TypeDef *port;
  uint16_t pin;
} cs_line_t;

/* Chip select order follows the thermocouple numbering, not the IC reference
 * designators: IC2 carries thermocouple 3 and IC3 carries thermocouple 2. */
static const cs_line_t csLines[EGT_COUNT] = {
  { EGT1_CS_PORT, EGT1_CS_PIN },
  { EGT2_CS_PORT, EGT2_CS_PIN },
  { EGT3_CS_PORT, EGT3_CS_PIN },
  { EGT4_CS_PORT, EGT4_CS_PIN },
};

static SPI_HandleTypeDef spi;
static egt_channel_t channels[EGT_COUNT];

void egt_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_SPI2_CLK_ENABLE();

  /* Chip selects idle high. Set the level before switching to an output so no
   * converter sees a glitch low. */
  for (uint8_t i = 0U; i < EGT_COUNT; i++)
  {
    HAL_GPIO_WritePin(csLines[i].port, csLines[i].pin, GPIO_PIN_SET);
  }
  gpio.Pin = EGT1_CS_PIN | EGT2_CS_PIN | EGT3_CS_PIN | EGT4_CS_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin = EGT_SCK_PIN | EGT_MISO_PIN | EGT_MOSI_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(EGT_SPI_PORT, &gpio);

  /* Mode 0, MSB first. The MAX31855 tops out at 5MHz; 170MHz / 64 is 2.66MHz.
   * The converters have no data input, so this is a read only bus and MOSI
   * goes nowhere. Full duplex is used anyway so the HAL clocks out a known
   * number of bits. */
  spi.Instance = SPI2;
  spi.Init.Mode = SPI_MODE_MASTER;
  spi.Init.Direction = SPI_DIRECTION_2LINES;
  spi.Init.DataSize = SPI_DATASIZE_8BIT;
  spi.Init.CLKPolarity = SPI_POLARITY_LOW;
  spi.Init.CLKPhase = SPI_PHASE_1EDGE;
  spi.Init.NSS = SPI_NSS_SOFT;
  spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
  spi.Init.TIMode = SPI_TIMODE_DISABLE;
  spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  spi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&spi) != HAL_OK)
  {
    board_panic();
  }

  for (uint8_t i = 0U; i < EGT_COUNT; i++)
  {
    channels[i].temperature_c = VALUE_INVALID;
    channels[i].cold_junction_c = VALUE_INVALID;
    channels[i].faults = (uint8_t)EGT_FAULT_NO_ANSWER;
    channels[i].valid = false;
  }
}

/** @brief Clock 32 bits out of one converter. Returns false on a bus error. */
static bool read_raw(uint8_t channel, uint32_t *pRaw)
{
  static const uint8_t dummy[4] = { 0U, 0U, 0U, 0U };
  uint8_t rx[4] = { 0U, 0U, 0U, 0U };

  HAL_GPIO_WritePin(csLines[channel].port, csLines[channel].pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(&spi, (uint8_t *)dummy, rx, sizeof(rx), 10U);
  HAL_GPIO_WritePin(csLines[channel].port, csLines[channel].pin, GPIO_PIN_SET);

  if (status != HAL_OK)
  {
    return false;
  }

  *pRaw = ((uint32_t)rx[0] << 24) | ((uint32_t)rx[1] << 16)
        | ((uint32_t)rx[2] << 8)  | (uint32_t)rx[3];
  return true;
}

static void decode(uint32_t raw, egt_channel_t *pChannel)
{
  /* All zeroes means nothing drove MISO, all ones means the converter is
   * missing or held in reset. Neither is a valid frame: bit 17 and bit 3 are
   * reserved and always read back as zero. */
  if ((raw == 0UL) || (raw == 0xFFFFFFFFUL))
  {
    pChannel->temperature_c = VALUE_INVALID;
    pChannel->cold_junction_c = VALUE_INVALID;
    pChannel->faults = (uint8_t)EGT_FAULT_NO_ANSWER;
    pChannel->valid = false;
    return;
  }

  uint8_t faults = 0U;
  if ((raw & MAX31855_OC_BIT) != 0UL)  { faults |= (uint8_t)EGT_FAULT_OPEN; }
  if ((raw & MAX31855_SCG_BIT) != 0UL) { faults |= (uint8_t)EGT_FAULT_SHORT_GND; }
  if ((raw & MAX31855_SCV_BIT) != 0UL) { faults |= (uint8_t)EGT_FAULT_SHORT_VCC; }

  /* Sign extend the 14 bit hot junction field, then drop the quarter degrees. */
  int32_t hot = (int32_t)(raw >> 18) & 0x3FFF;
  if ((hot & 0x2000) != 0)
  {
    hot -= 0x4000;
  }

  /* Sign extend the 12 bit cold junction field, 1/16 degree per LSB. */
  int32_t cold = (int32_t)(raw >> 4) & 0x0FFF;
  if ((cold & 0x0800) != 0)
  {
    cold -= 0x1000;
  }

  pChannel->cold_junction_c = (int16_t)(cold / 16);
  pChannel->faults = faults;

  if (((raw & MAX31855_FAULT_BIT) != 0UL) || (faults != 0U))
  {
    /* The hot junction field is meaningless while a fault is asserted. */
    pChannel->temperature_c = VALUE_INVALID;
    pChannel->valid = false;
  }
  else
  {
    pChannel->temperature_c = (int16_t)(hot / 4);
    pChannel->valid = true;
  }
}

void egt_poll(void)
{
  for (uint8_t i = 0U; i < EGT_COUNT; i++)
  {
    uint32_t raw = 0UL;
    if (read_raw(i, &raw))
    {
      decode(raw, &channels[i]);
    }
    else
    {
      channels[i].temperature_c = VALUE_INVALID;
      channels[i].faults = (uint8_t)EGT_FAULT_NO_ANSWER;
      channels[i].valid = false;
    }
  }
}

const egt_channel_t *egt_get(uint8_t channel)
{
  return (channel < EGT_COUNT) ? &channels[channel] : &channels[0];
}

uint8_t egt_fault_summary(void)
{
  uint8_t summary = 0U;

  for (uint8_t i = 0U; i < EGT_COUNT; i++)
  {
    /* Two bits per channel: bit 0 any fault, bit 1 no answer at all. */
    if (channels[i].faults != 0U)
    {
      summary |= (uint8_t)(1U << (i * 2U));
    }
    if ((channels[i].faults & (uint8_t)EGT_FAULT_NO_ANSWER) != 0U)
    {
      summary |= (uint8_t)(2U << (i * 2U));
    }
  }

  return summary;
}
