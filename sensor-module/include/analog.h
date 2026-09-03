/**
 * @file analog.h
 * @brief ADC1 and ADC2 in continuous scan mode with DMA, plus sensor scaling.
 */
#ifndef ANALOG_H
#define ANALOG_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Scaled analog readings. */
typedef struct {
  int16_t oil_pressure_kpa;
  int16_t fuel_pressure_kpa;
  int16_t exhaust_pressure_kpa;
  int16_t oil_temp_dc;        /**< Tenths of a degree Celsius              */
  int16_t ic_water1_dc;       /**< Tenths of a degree Celsius              */
  int16_t ic_water2_dc;       /**< Tenths of a degree Celsius              */
  uint16_t qs_push_mv;        /**< Quickshifter push sensor, millivolts    */
  uint16_t qs_pull_mv;        /**< Quickshifter pull sensor, millivolts    */
  uint16_t rail_5v_mv;        /**< Measured 5V rail, millivolts            */
} analog_values_t;

/** @brief Start both ADCs. Conversions run continuously into DMA buffers. */
void analog_init(void);

/** @brief Convert the latest raw samples into @ref analog_values_t. */
void analog_update(void);

/** @brief The most recent scaled readings. */
const analog_values_t *analog_get(void);

/**
 * @brief The hotter of the two intercooler water sensors, in whole degrees.
 *
 * Returns false if neither sensor produced a plausible reading, which is what
 * puts the fan into its failsafe.
 */
bool analog_intercooler_temp_c(int16_t *pTemp);

#endif /* ANALOG_H */
