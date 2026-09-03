/**
 * @file can_bus.h
 * @brief FDCAN1 in classic mode, 500 kbit/s, towards the Speeduino ECU.
 *
 * All transmitted values are big endian, which is what Speeduino's generic CAN
 * input channels default to (config9.caninputEndianess == 0).
 */
#ifndef CAN_BUS_H
#define CAN_BUS_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Set up FDCAN1 and its receive filter. */
void can_init(void);

/** @brief Send the four broadcast frames. */
void can_broadcast(void);

/** @brief Drain the receive FIFO and act on any command frames. */
void can_poll(void);

/** @brief True if the peripheral reported a bus off condition. */
bool can_bus_off(void);

#endif /* CAN_BUS_H */
