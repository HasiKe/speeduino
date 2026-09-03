/**
 * @file config.h
 * @brief Board constants for the Hayabusa sensor module, hardware revision 3.
 *
 * Everything here is derived from the rev 3.0 KiCad netlist of
 * hardware/ECU/sensor-module.kicad_sch. Nothing in this file is a guess about
 * the schematic; the values that ARE guesses (sensor curves, fan thresholds)
 * are marked and can be overridden with build flags.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ===========================================================================
 * Clock
 * ------------------------------------------------------------------------ */
/** @brief CSTNE16M0V530000R0 ceramic resonator on PF0/PF1 */
#define HSE_FREQ_HZ            16000000UL
/** @brief System clock. 16MHz / 4 * 85 / 2 */
#define SYSCLK_FREQ_HZ         170000000UL

/* ===========================================================================
 * Thermocouple front end: 4x MAX31855KASA+ on SPI2
 *
 * Note the chip select order. It is NOT sequential on the board: IC2 carries
 * thermocouple 3 and IC3 carries thermocouple 2.
 *   EGT1 = IC1, CS on PB6
 *   EGT2 = IC3, CS on PB10
 *   EGT3 = IC2, CS on PB11
 *   EGT4 = IC4, CS on PB12
 * ------------------------------------------------------------------------ */
#define EGT_COUNT              4U

#define EGT_SPI_PORT           GPIOB
#define EGT_SCK_PIN            GPIO_PIN_13   /* SPI2_SCK  */
#define EGT_MISO_PIN           GPIO_PIN_14   /* SPI2_MISO */
#define EGT_MOSI_PIN           GPIO_PIN_15   /* SPI2_MOSI, routed but unused */

#define EGT1_CS_PORT           GPIOB
#define EGT1_CS_PIN            GPIO_PIN_6
#define EGT2_CS_PORT           GPIOB
#define EGT2_CS_PIN            GPIO_PIN_10
#define EGT3_CS_PORT           GPIOB
#define EGT3_CS_PIN            GPIO_PIN_11
#define EGT4_CS_PORT           GPIOB
#define EGT4_CS_PIN            GPIO_PIN_12

/* ===========================================================================
 * Analog inputs
 *
 * Each input reaches the MCU through a 1k series resistor and an SP720 clamp.
 * ADC1 covers PA0..PA3 and PC0..PC2, ADC2 covers PA4 and PA5.
 * ------------------------------------------------------------------------ */
#define ADC1_CHANNEL_COUNT     7U
#define ADC2_CHANNEL_COUNT     2U

/** @brief Indices into the ADC1 DMA buffer, in conversion order. */
enum adc1_index {
  ADC1_IDX_V5_SENSE = 0,   /**< PA0, 10k/15k divider off the 5V rail   */
  ADC1_IDX_QS_PUSH,        /**< PA1, quickshifter push, 10k pull up    */
  ADC1_IDX_QS_PULL,        /**< PA2, quickshifter pull, 10k pull up    */
  ADC1_IDX_EXH_PRESSURE,   /**< PA3, exhaust pressure, 1k/2k divider   */
  ADC1_IDX_OIL_PRESSURE,   /**< PC0, oil pressure, 1k/2k divider       */
  ADC1_IDX_FUEL_PRESSURE,  /**< PC1, fuel pressure, 1k/2k divider      */
  ADC1_IDX_OIL_TEMP,       /**< PC2, oil temperature NTC, 1k pull up   */
};

/** @brief Indices into the ADC2 DMA buffer, in conversion order. */
enum adc2_index {
  ADC2_IDX_IC_WATER_1 = 0, /**< PA4, intercooler water temp 1 NTC      */
  ADC2_IDX_IC_WATER_2,     /**< PA5, intercooler water temp 2 NTC      */
};

/** @brief ADC reference, supplied through the BLM18AG601 ferrite from 3V3. */
#define ADC_VREF_MV            3300U
#define ADC_FULL_SCALE         4095U

/** @brief Divider on the pressure sensor inputs: 1k series, 2k to ground. */
#define PRESSURE_DIVIDER_NUM   3U
#define PRESSURE_DIVIDER_DEN   2U

/** @brief Divider on the 5V rail sense: 10k over 15k. */
#define V5_DIVIDER_NUM         5U
#define V5_DIVIDER_DEN         3U

/** @brief Pull up resistance on the NTC inputs, to 3V3. */
#define NTC_PULLUP_OHMS        1000U
/** @brief Pull up resistance on the quickshifter inputs, to 3V3. */
#define QS_PULLUP_OHMS         10000U

/* Automotive ratiometric pressure sensors, 0.5V at zero, 4.5V at full scale.
 * GUESS: the actual sensors fitted are not recorded in the schematic. Adjust
 * per channel once they are known. */
#ifndef PRESSURE_SENSOR_MIN_MV
  #define PRESSURE_SENSOR_MIN_MV   500
#endif
#ifndef PRESSURE_SENSOR_MAX_MV
  #define PRESSURE_SENSOR_MAX_MV   4500
#endif
#ifndef OIL_PRESSURE_MAX_KPA
  #define OIL_PRESSURE_MAX_KPA     1000
#endif
#ifndef FUEL_PRESSURE_MAX_KPA
  #define FUEL_PRESSURE_MAX_KPA    1000
#endif
#ifndef EXHAUST_PRESSURE_MAX_KPA
  #define EXHAUST_PRESSURE_MAX_KPA 500
#endif

/* NTC model, resistance at 25C and beta value.
 * GUESS: defaults are a common 2.5k automotive NTC. Verify against the sensors
 * actually fitted; the raw resistance is broadcast so this can be checked. */
#ifndef NTC_R25_OHMS
  #define NTC_R25_OHMS           2500
#endif
#ifndef NTC_BETA
  #define NTC_BETA               3450
#endif

/* ===========================================================================
 * Intercooler fan, PB0 -> 100R -> SQM120N06 low side -> J3.12
 *
 * The gate is driven straight from a 3.3V pin through 100 ohm with no gate
 * driver, so switching a MOSFET this large takes a few microseconds. The PWM
 * frequency is kept low on purpose to keep the switching losses down; see the
 * README.
 * ------------------------------------------------------------------------ */
#define FAN_PWM_PORT           GPIOB
#define FAN_PWM_PIN            GPIO_PIN_0    /* TIM3_CH3, AF2 */
#ifndef FAN_PWM_FREQ_HZ
  #define FAN_PWM_FREQ_HZ      200U
#endif

/* Fan control law, on the hotter of the two intercooler water sensors.
 * GUESS: sensible starting points for a water to air intercooler circuit. */
#ifndef FAN_ON_TEMP_C
  #define FAN_ON_TEMP_C        35
#endif
#ifndef FAN_FULL_TEMP_C
  #define FAN_FULL_TEMP_C      50
#endif
#ifndef FAN_OFF_HYSTERESIS_C
  #define FAN_OFF_HYSTERESIS_C 4
#endif
/** @brief Lowest duty the fan is driven at once it starts, in percent. */
#ifndef FAN_MIN_DUTY_PCT
  #define FAN_MIN_DUTY_PCT     30
#endif
/** @brief Duty used when both water sensors have failed. */
#ifndef FAN_FAILSAFE_DUTY_PCT
  #define FAN_FAILSAFE_DUTY_PCT 100
#endif

/* ===========================================================================
 * CAN. Classic 2.0B at 500 kbit/s: the ECU end has a plain MCP2562, not an FD
 * part, and Speeduino runs its bus at 500 kbit/s.
 * ------------------------------------------------------------------------ */
#ifndef CAN_BASE_ID
  #define CAN_BASE_ID          0x580U
#endif
#define CAN_ID_EGT             (CAN_BASE_ID + 0U)
#define CAN_ID_PRESSURES       (CAN_BASE_ID + 1U)
#define CAN_ID_TEMPS_QS        (CAN_BASE_ID + 2U)
#define CAN_ID_STATUS          (CAN_BASE_ID + 3U)
#define CAN_ID_COMMAND         (CAN_BASE_ID + 7U)

/** @brief Broadcast period for every transmitted frame, in milliseconds. */
#ifndef CAN_TX_PERIOD_MS
  #define CAN_TX_PERIOD_MS     50U
#endif
/** @brief A fan override is dropped if no command arrives within this time. */
#ifndef CAN_OVERRIDE_TIMEOUT_MS
  #define CAN_OVERRIDE_TIMEOUT_MS 2000U
#endif

/** @brief Command byte 0 values on CAN_ID_COMMAND. */
#define CAN_CMD_FAN_OVERRIDE   0x01U
/** @brief Duty value that releases an override and returns to local control. */
#define CAN_FAN_RELEASE        0xFFU

/* ===========================================================================
 * Sentinels
 * ------------------------------------------------------------------------ */
/** @brief Reported for any channel whose reading is not currently valid. */
#define VALUE_INVALID          INT16_MIN

#endif /* CONFIG_H */
