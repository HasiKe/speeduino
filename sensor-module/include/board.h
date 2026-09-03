/**
 * @file board.h
 * @brief Clock tree, fault handling and the shared millisecond timebase.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32g4xx_hal.h"

/** @brief Bring up the clock tree, the peripheral clocks and HAL. */
void board_init(void);

/** @brief Milliseconds since reset. */
static inline uint32_t board_millis(void) { return HAL_GetTick(); }

/** @brief True once @p since is at least @p interval milliseconds old. */
static inline bool board_elapsed(uint32_t since, uint32_t interval)
{
  return (uint32_t)(HAL_GetTick() - since) >= interval;
}

/**
 * @brief Unrecoverable error.
 *
 * Turns the fan on at the failsafe duty, since losing the intercooler fan is
 * worse than running it needlessly, then resets through the watchdog.
 */
void board_panic(void);

#endif /* BOARD_H */
