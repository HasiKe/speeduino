#include "analog.h"

#include <math.h>
#include "board.h"
#include "config.h"

static ADC_HandleTypeDef adc1;
static ADC_HandleTypeDef adc2;
static DMA_HandleTypeDef dmaAdc1;
static DMA_HandleTypeDef dmaAdc2;

static volatile uint16_t adc1Raw[ADC1_CHANNEL_COUNT];
static volatile uint16_t adc2Raw[ADC2_CHANNEL_COUNT];

static analog_values_t values;

/* Conversion order. The index order here must match enum adc1_index and
 * enum adc2_index in config.h.
 *
 * STM32G474 LQFP64 analog inputs:
 *   PA0 ADC1_IN1   PA1 ADC1_IN2   PA2 ADC1_IN3   PA3 ADC1_IN4
 *   PC0 ADC1_IN6   PC1 ADC1_IN7   PC2 ADC1_IN8
 *   PA4 ADC2_IN17  PA5 ADC2_IN13
 * PA4 and PA5 are the reason the second ADC is needed at all: neither is
 * reachable from ADC1. */
static const uint32_t adc1Channels[ADC1_CHANNEL_COUNT] = {
  ADC_CHANNEL_1,  /* PA0 5V sense          */
  ADC_CHANNEL_2,  /* PA1 quickshifter push */
  ADC_CHANNEL_3,  /* PA2 quickshifter pull */
  ADC_CHANNEL_4,  /* PA3 exhaust pressure  */
  ADC_CHANNEL_6,  /* PC0 oil pressure      */
  ADC_CHANNEL_7,  /* PC1 fuel pressure     */
  ADC_CHANNEL_8,  /* PC2 oil temperature   */
};

static const uint32_t adc2Channels[ADC2_CHANNEL_COUNT] = {
  ADC_CHANNEL_17, /* PA4 intercooler water 1 */
  ADC_CHANNEL_13, /* PA5 intercooler water 2 */
};

static const uint32_t adc1Ranks[ADC1_CHANNEL_COUNT] = {
  ADC_REGULAR_RANK_1, ADC_REGULAR_RANK_2, ADC_REGULAR_RANK_3,
  ADC_REGULAR_RANK_4, ADC_REGULAR_RANK_5, ADC_REGULAR_RANK_6,
  ADC_REGULAR_RANK_7,
};

static const uint32_t adc2Ranks[ADC2_CHANNEL_COUNT] = {
  ADC_REGULAR_RANK_1, ADC_REGULAR_RANK_2,
};

static void gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;

  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3
           | GPIO_PIN_4 | GPIO_PIN_5;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
  HAL_GPIO_Init(GPIOC, &gpio);
}

static void common_adc_config(ADC_HandleTypeDef *pAdc, ADC_TypeDef *pInstance, uint32_t rankCount)
{
  pAdc->Instance = pInstance;
  /* Asynchronous clock, so the divider applies to the ADC kernel clock that
   * board.c selects (the system clock), not to PCLK. 170MHz / 4 = 42.5MHz. */
  pAdc->Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
  pAdc->Init.Resolution = ADC_RESOLUTION_12B;
  pAdc->Init.DataAlign = ADC_DATAALIGN_RIGHT;
  pAdc->Init.GainCompensation = 0U;
  pAdc->Init.ScanConvMode = ADC_SCAN_ENABLE;
  pAdc->Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  pAdc->Init.LowPowerAutoWait = DISABLE;
  pAdc->Init.ContinuousConvMode = ENABLE;
  pAdc->Init.NbrOfConversion = rankCount;
  pAdc->Init.DiscontinuousConvMode = DISABLE;
  pAdc->Init.ExternalTrigConv = ADC_SOFTWARE_START;
  pAdc->Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  pAdc->Init.DMAContinuousRequests = ENABLE;
  pAdc->Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  /* Hardware oversampling by 16 with a shift of 4 keeps the result 12 bit
   * while averaging away most of the noise from a 12V automotive harness. */
  pAdc->Init.OversamplingMode = ENABLE;
  pAdc->Init.Oversampling.Ratio = ADC_OVERSAMPLING_RATIO_16;
  pAdc->Init.Oversampling.RightBitShift = ADC_RIGHTBITSHIFT_4;
  pAdc->Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
  pAdc->Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;
}

static void dma_init(DMA_HandleTypeDef *pDma, DMA_Channel_TypeDef *pChannel,
                     uint32_t request, ADC_HandleTypeDef *pAdc)
{
  pDma->Instance = pChannel;
  pDma->Init.Request = request;
  pDma->Init.Direction = DMA_PERIPH_TO_MEMORY;
  pDma->Init.PeriphInc = DMA_PINC_DISABLE;
  pDma->Init.MemInc = DMA_MINC_ENABLE;
  pDma->Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  pDma->Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  pDma->Init.Mode = DMA_CIRCULAR;
  pDma->Init.Priority = DMA_PRIORITY_LOW;
  if (HAL_DMA_Init(pDma) != HAL_OK)
  {
    board_panic();
  }
  __HAL_LINKDMA(pAdc, DMA_Handle, *pDma);
}

void analog_init(void)
{
  ADC_ChannelConfTypeDef channel = {0};

  __HAL_RCC_ADC12_CLK_ENABLE();
  gpio_init();

  common_adc_config(&adc1, ADC1, ADC1_CHANNEL_COUNT);
  common_adc_config(&adc2, ADC2, ADC2_CHANNEL_COUNT);

  dma_init(&dmaAdc1, DMA1_Channel1, DMA_REQUEST_ADC1, &adc1);
  dma_init(&dmaAdc2, DMA1_Channel2, DMA_REQUEST_ADC2, &adc2);

  if ((HAL_ADC_Init(&adc1) != HAL_OK) || (HAL_ADC_Init(&adc2) != HAL_OK))
  {
    board_panic();
  }

  /* The 1k series resistors plus the clamp capacitance need a long sampling
   * time to charge the sample and hold. 247.5 cycles at 42.5MHz is about 6us. */
  channel.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
  channel.SingleDiff = ADC_SINGLE_ENDED;
  channel.OffsetNumber = ADC_OFFSET_NONE;
  channel.Offset = 0U;

  for (uint8_t i = 0U; i < ADC1_CHANNEL_COUNT; i++)
  {
    channel.Channel = adc1Channels[i];
    channel.Rank = adc1Ranks[i];
    if (HAL_ADC_ConfigChannel(&adc1, &channel) != HAL_OK)
    {
      board_panic();
    }
  }
  for (uint8_t i = 0U; i < ADC2_CHANNEL_COUNT; i++)
  {
    channel.Channel = adc2Channels[i];
    channel.Rank = adc2Ranks[i];
    if (HAL_ADC_ConfigChannel(&adc2, &channel) != HAL_OK)
    {
      board_panic();
    }
  }

  if ((HAL_ADCEx_Calibration_Start(&adc1, ADC_SINGLE_ENDED) != HAL_OK)
   || (HAL_ADCEx_Calibration_Start(&adc2, ADC_SINGLE_ENDED) != HAL_OK))
  {
    board_panic();
  }

  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);

  if ((HAL_ADC_Start_DMA(&adc1, (uint32_t *)adc1Raw, ADC1_CHANNEL_COUNT) != HAL_OK)
   || (HAL_ADC_Start_DMA(&adc2, (uint32_t *)adc2Raw, ADC2_CHANNEL_COUNT) != HAL_OK))
  {
    board_panic();
  }
}

/* The transfers are circular and the buffers are read straight out of memory,
 * so there is nothing to do on completion. The handlers exist because the HAL
 * enables the transfer complete interrupt and an unhandled one would leave the
 * DMA flagging forever. */
void DMA1_Channel1_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&dmaAdc1);
}

void DMA1_Channel2_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&dmaAdc2);
}

/** @brief Raw count to millivolts at the pin. */
static uint16_t counts_to_mv(uint16_t counts)
{
  return (uint16_t)(((uint32_t)counts * ADC_VREF_MV) / ADC_FULL_SCALE);
}

/**
 * @brief Scale a pressure sensor channel.
 *
 * The sensor output passes a 1k over 2k divider, so the pin sees two thirds of
 * it. Ratiometric sensors sit at 0.5V with no pressure and 4.5V at full scale,
 * so anything below the low limit means an open or shorted sensor.
 */
static int16_t pressure_kpa(uint16_t counts, int32_t fullScaleKpa)
{
  int32_t signalMv = ((int32_t)counts_to_mv(counts) * PRESSURE_DIVIDER_NUM) / PRESSURE_DIVIDER_DEN;

  /* 100mV of slack below the sensor's zero point, anything under that is a
   * wiring fault rather than a low reading. */
  if (signalMv < (PRESSURE_SENSOR_MIN_MV - 100))
  {
    return VALUE_INVALID;
  }

  int32_t span = PRESSURE_SENSOR_MAX_MV - PRESSURE_SENSOR_MIN_MV;
  int32_t kpa = ((signalMv - PRESSURE_SENSOR_MIN_MV) * fullScaleKpa) / span;

  if (kpa < 0)             { kpa = 0; }
  if (kpa > fullScaleKpa)  { kpa = fullScaleKpa; }

  return (int16_t)kpa;
}

/**
 * @brief Scale an NTC channel to tenths of a degree Celsius.
 *
 * The sensor sits between the input and ground with a pull up to 3V3, so the
 * resistance follows from the divider. A beta model then gives the temperature.
 * The resistance itself is what to check first if a reading looks wrong.
 */
static int16_t ntc_temp_dc(uint16_t counts, uint32_t pullupOhms)
{
  uint32_t mv = counts_to_mv(counts);

  /* At the rails the divider tells us nothing: 0 means the sensor is shorted,
   * full scale means it is open. */
  if ((mv < 30U) || (mv > (ADC_VREF_MV - 30U)))
  {
    return VALUE_INVALID;
  }

  float resistance = (float)pullupOhms * (float)mv / (float)(ADC_VREF_MV - mv);

  /* 1/T = 1/T0 + ln(R/R0) / B, with T0 at 25C in kelvin. */
  const float t0 = 298.15F;
  float invT = (1.0F / t0) + (logf(resistance / (float)NTC_R25_OHMS) / (float)NTC_BETA);
  float tempC = (1.0F / invT) - 273.15F;

  if ((tempC < -50.0F) || (tempC > 200.0F))
  {
    return VALUE_INVALID;
  }

  return (int16_t)(tempC * 10.0F);
}

void analog_update(void)
{
  values.rail_5v_mv = (uint16_t)(((uint32_t)counts_to_mv(adc1Raw[ADC1_IDX_V5_SENSE])
                                  * V5_DIVIDER_NUM) / V5_DIVIDER_DEN);

  values.qs_push_mv = counts_to_mv(adc1Raw[ADC1_IDX_QS_PUSH]);
  values.qs_pull_mv = counts_to_mv(adc1Raw[ADC1_IDX_QS_PULL]);

  values.oil_pressure_kpa     = pressure_kpa(adc1Raw[ADC1_IDX_OIL_PRESSURE], OIL_PRESSURE_MAX_KPA);
  values.fuel_pressure_kpa    = pressure_kpa(adc1Raw[ADC1_IDX_FUEL_PRESSURE], FUEL_PRESSURE_MAX_KPA);
  values.exhaust_pressure_kpa = pressure_kpa(adc1Raw[ADC1_IDX_EXH_PRESSURE], EXHAUST_PRESSURE_MAX_KPA);

  values.oil_temp_dc  = ntc_temp_dc(adc1Raw[ADC1_IDX_OIL_TEMP], NTC_PULLUP_OHMS);
  values.ic_water1_dc = ntc_temp_dc(adc2Raw[ADC2_IDX_IC_WATER_1], NTC_PULLUP_OHMS);
  values.ic_water2_dc = ntc_temp_dc(adc2Raw[ADC2_IDX_IC_WATER_2], NTC_PULLUP_OHMS);
}

const analog_values_t *analog_get(void)
{
  return &values;
}

bool analog_intercooler_temp_c(int16_t *pTemp)
{
  int16_t hottest = VALUE_INVALID;

  if (values.ic_water1_dc != VALUE_INVALID)
  {
    hottest = values.ic_water1_dc;
  }
  if ((values.ic_water2_dc != VALUE_INVALID) && (values.ic_water2_dc > hottest))
  {
    hottest = values.ic_water2_dc;
  }

  if (hottest == VALUE_INVALID)
  {
    return false;
  }

  *pTemp = (int16_t)(hottest / 10);
  return true;
}
