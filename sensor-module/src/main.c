/**
 * @file main.c
 * @brief Hayabusa sensor module, hardware revision 3.
 *
 * Reads four exhaust gas thermocouples, eight analog channels and drives the
 * intercooler fan, then broadcasts everything to the Speeduino ECU over
 * classic CAN at 500 kbit/s.
 *
 * The loop is a plain cooperative scheduler. Nothing here needs to be fast:
 * the MAX31855 converters take about 100ms per sample and the ECU only reads
 * the values a few times a second.
 */
#include "analog.h"
#include "board.h"
#include "can_bus.h"
#include "config.h"
#include "egt.h"
#include "fan.h"

/** @brief The MAX31855 needs roughly 100ms per conversion. */
#define EGT_POLL_PERIOD_MS     100U
/** @brief Analog scaling and the fan control law. */
#define ANALOG_PERIOD_MS       20U

int main(void)
{
  board_init();

  /* The fan comes up first so a fault in any later init still leaves the
   * failsafe path in board_panic() able to drive it. */
  fan_init();
  analog_init();
  egt_init();
  can_init();

  uint32_t lastEgt = board_millis();
  uint32_t lastAnalog = board_millis();
  uint32_t lastBroadcast = board_millis();

  /* Give the ADCs a full oversampled scan before the first control decision,
   * otherwise the fan would briefly see zeroed readings as a cold engine. */
  HAL_Delay(50U);
  analog_update();

  for (;;)
  {
    can_poll();

    if (board_elapsed(lastAnalog, ANALOG_PERIOD_MS))
    {
      lastAnalog = board_millis();
      analog_update();
      fan_update();
    }

    if (board_elapsed(lastEgt, EGT_POLL_PERIOD_MS))
    {
      lastEgt = board_millis();
      egt_poll();
    }

    if (board_elapsed(lastBroadcast, CAN_TX_PERIOD_MS))
    {
      lastBroadcast = board_millis();
      can_broadcast();
    }
  }
}
