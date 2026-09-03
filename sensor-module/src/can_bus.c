#include "can_bus.h"

#include <string.h>
#include "analog.h"
#include "board.h"
#include "config.h"
#include "egt.h"
#include "fan.h"

static FDCAN_HandleTypeDef fdcan;
static uint8_t txCounter = 0U;
static bool busOffSeen = false;

/** @brief Status flag bits in byte 4 of the status frame. */
#define STATUS_FAN_OVERRIDE    0x01U
#define STATUS_FAN_RUNNING     0x02U
#define STATUS_EGT_FAULT       0x04U
#define STATUS_RAIL_5V_BAD     0x08U
#define STATUS_BUS_OFF_SEEN    0x10U

static void put_be16(uint8_t *pBuf, int16_t value)
{
  pBuf[0] = (uint8_t)((uint16_t)value >> 8);
  pBuf[1] = (uint8_t)((uint16_t)value & 0xFFU);
}

void can_init(void)
{
  GPIO_InitTypeDef gpio = {0};
  FDCAN_FilterTypeDef filter = {0};

  __HAL_RCC_FDCAN_CLK_ENABLE();

  gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12; /* PA11 RX, PA12 TX */
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF9_FDCAN1;
  HAL_GPIO_Init(GPIOA, &gpio);

  /* Classic CAN only. The ECU end of the link has a plain MCP2562, not an FD
   * transceiver, and Speeduino runs its bus at 500 kbit/s.
   *
   * Kernel clock is the 16MHz HSE, so one bit is 32 time quanta of 62.5ns:
   * 1 sync + 27 + 4 = 32, which puts the sample point at 87.5%. */
  fdcan.Instance = FDCAN1;
  fdcan.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  fdcan.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  fdcan.Init.Mode = FDCAN_MODE_NORMAL;
  fdcan.Init.AutoRetransmission = ENABLE;
  fdcan.Init.TransmitPause = DISABLE;
  fdcan.Init.ProtocolException = DISABLE;
  fdcan.Init.NominalPrescaler = 1U;
  fdcan.Init.NominalSyncJumpWidth = 4U;
  fdcan.Init.NominalTimeSeg1 = 27U;
  fdcan.Init.NominalTimeSeg2 = 4U;
  fdcan.Init.DataPrescaler = 1U;
  fdcan.Init.DataSyncJumpWidth = 4U;
  fdcan.Init.DataTimeSeg1 = 27U;
  fdcan.Init.DataTimeSeg2 = 4U;
  fdcan.Init.StdFiltersNbr = 1U;
  fdcan.Init.ExtFiltersNbr = 0U;
  fdcan.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&fdcan) != HAL_OK)
  {
    board_panic();
  }

  /* Only the command frame is of interest. Everything else on the bus is
   * rejected in hardware so the receive FIFO cannot fill with ECU traffic. */
  filter.IdType = FDCAN_STANDARD_ID;
  filter.FilterIndex = 0U;
  filter.FilterType = FDCAN_FILTER_MASK;
  filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  filter.FilterID1 = CAN_ID_COMMAND;
  filter.FilterID2 = 0x7FFU;
  if (HAL_FDCAN_ConfigFilter(&fdcan, &filter) != HAL_OK)
  {
    board_panic();
  }

  if (HAL_FDCAN_ConfigGlobalFilter(&fdcan, FDCAN_REJECT, FDCAN_REJECT,
                                   FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK)
  {
    board_panic();
  }

  if (HAL_FDCAN_Start(&fdcan) != HAL_OK)
  {
    board_panic();
  }
}

static void send(uint32_t id, const uint8_t *pData)
{
  FDCAN_TxHeaderTypeDef header = {0};

  header.Identifier = id;
  header.IdType = FDCAN_STANDARD_ID;
  header.TxFrameType = FDCAN_DATA_FRAME;
  header.DataLength = FDCAN_DLC_BYTES_8;
  header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  header.BitRateSwitch = FDCAN_BRS_OFF;
  header.FDFormat = FDCAN_CLASSIC_CAN;
  header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  header.MessageMarker = 0U;

  /* A full queue means nothing is acknowledging us. Dropping the frame is the
   * right answer: the next broadcast carries fresher data anyway. */
  if (HAL_FDCAN_GetTxFifoFreeLevel(&fdcan) > 0U)
  {
    (void)HAL_FDCAN_AddMessageToTxFifoQ(&fdcan, &header, (uint8_t *)pData);
  }
}

void can_broadcast(void)
{
  const analog_values_t *pAnalog = analog_get();
  uint8_t data[8];

  /* Exhaust gas temperature, whole degrees Celsius, one signed value per
   * cylinder. A faulty channel reports INT16_MIN. */
  for (uint8_t i = 0U; i < EGT_COUNT; i++)
  {
    put_be16(&data[i * 2U], egt_get(i)->temperature_c);
  }
  send(CAN_ID_EGT, data);

  put_be16(&data[0], pAnalog->oil_pressure_kpa);
  put_be16(&data[2], pAnalog->fuel_pressure_kpa);
  put_be16(&data[4], pAnalog->exhaust_pressure_kpa);
  put_be16(&data[6], pAnalog->oil_temp_dc);
  send(CAN_ID_PRESSURES, data);

  put_be16(&data[0], pAnalog->ic_water1_dc);
  put_be16(&data[2], pAnalog->ic_water2_dc);
  put_be16(&data[4], (int16_t)pAnalog->qs_push_mv);
  put_be16(&data[6], (int16_t)pAnalog->qs_pull_mv);
  send(CAN_ID_TEMPS_QS, data);

  uint8_t egtFaults = egt_fault_summary();
  uint8_t flags = 0U;
  if (fan_override_active())               { flags |= STATUS_FAN_OVERRIDE; }
  if (fan_duty() > 0U)                     { flags |= STATUS_FAN_RUNNING; }
  if (egtFaults != 0U)                     { flags |= STATUS_EGT_FAULT; }
  if ((pAnalog->rail_5v_mv < 4500U)
   || (pAnalog->rail_5v_mv > 5500U))       { flags |= STATUS_RAIL_5V_BAD; }
  if (can_bus_off())                       { busOffSeen = true; }
  if (busOffSeen)                          { flags |= STATUS_BUS_OFF_SEEN; }

  put_be16(&data[0], (int16_t)pAnalog->rail_5v_mv);
  data[2] = fan_duty();
  data[3] = egtFaults;
  data[4] = flags;
  data[5] = txCounter;
  /* The cold junction of the first converter. If the exhaust readings look
   * wrong this is the first thing to check: it should sit near ambient. */
  put_be16(&data[6], egt_get(0)->cold_junction_c);
  send(CAN_ID_STATUS, data);

  txCounter++;
}

void can_poll(void)
{
  FDCAN_RxHeaderTypeDef header;
  uint8_t data[8];

  while (HAL_FDCAN_GetRxFifoFillLevel(&fdcan, FDCAN_RX_FIFO0) > 0U)
  {
    if (HAL_FDCAN_GetRxMessage(&fdcan, FDCAN_RX_FIFO0, &header, data) != HAL_OK)
    {
      break;
    }

    if ((header.Identifier != CAN_ID_COMMAND) || (header.DataLength < FDCAN_DLC_BYTES_2))
    {
      continue;
    }

    if (data[0] == CAN_CMD_FAN_OVERRIDE)
    {
      fan_set_override(data[1]);
    }
  }
}

bool can_bus_off(void)
{
  FDCAN_ProtocolStatusTypeDef status;

  HAL_FDCAN_GetProtocolStatus(&fdcan, &status);
  return status.BusOff != 0U;
}
