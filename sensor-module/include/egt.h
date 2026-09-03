/**
 * @file egt.h
 * @brief MAX31855K thermocouple front end, four channels on SPI2.
 */
#ifndef EGT_H
#define EGT_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"

/** @brief Fault bits as reported by the MAX31855. */
enum egt_fault {
  EGT_FAULT_NONE       = 0x00U,
  EGT_FAULT_OPEN       = 0x01U, /**< Thermocouple not connected            */
  EGT_FAULT_SHORT_GND  = 0x02U, /**< Thermocouple shorted to ground        */
  EGT_FAULT_SHORT_VCC  = 0x04U, /**< Thermocouple shorted to VCC           */
  EGT_FAULT_NO_ANSWER  = 0x08U, /**< SPI read returned all zeroes or ones  */
};

/** @brief One thermocouple channel. */
typedef struct {
  int16_t  temperature_c;    /**< Hot junction temperature, whole degrees  */
  int16_t  cold_junction_c;  /**< Internal reference temperature, degrees  */
  uint8_t  faults;           /**< Bit field of @ref egt_fault              */
  bool     valid;            /**< No faults and the conversion was read    */
} egt_channel_t;

/** @brief Set up SPI2 and the four chip selects. */
void egt_init(void);

/**
 * @brief Read all four converters.
 *
 * The MAX31855 needs about 100ms per conversion, so calling this faster than
 * that just re-reads the same sample.
 */
void egt_poll(void);

/** @brief The most recent reading for @p channel, 0 based. */
const egt_channel_t *egt_get(uint8_t channel);

/** @brief Fault bits of all four channels packed into one byte, 2 bits each. */
uint8_t egt_fault_summary(void);

#endif /* EGT_H */
