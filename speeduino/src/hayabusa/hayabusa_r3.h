/**
 * @file
 * @brief Board specific I/O for the Gen 1 Hayabusa ECU, hardware revision 3.
 *
 * Everything in here is inert unless HAYABUSA_ECU_R3 is defined and the tune
 * selects board ID 57.
 *
 * The board carries a few devices that Speeduino has no generic support for:
 *  - a TPS3823 watchdog that resets the Teensy, and disables the MC33810
 *    outputs, unless it is kicked regularly
 *  - an output enable line for the MC33810, gated with the watchdog through a
 *    NAND gate
 *  - the motorcycle switches: neutral, clutch, tip over sensor, gear position
 *  - an FI (fault) lamp driver and an O2 heater high side switch
 *  - a fault feedback line shared by the stepper driver and the high side
 *    switches
 */
#pragma once

#include <stdint.h>

#if defined(HAYABUSA_ECU_R3)

/** @brief The pin map ID that this board is selected with in TunerStudio */
constexpr uint8_t HAYABUSA_R3_BOARD_ID = 57U;

/** @brief Live state of the Hayabusa specific inputs */
struct hayabusaStatus_t
{
  bool inNeutral;       ///< Neutral switch closed
  bool clutchPulled;    ///< Clutch lever pulled in
  bool tippedOver;      ///< Tip over sensor reports the bike is down
  bool driverFault;     ///< Stepper driver or a high side switch reports a fault
  bool outputsEnabled;  ///< MC33810 output enable line is asserted
  uint8_t gear;         ///< Selected gear, 1..6. 0 means neutral or unknown
  uint8_t gearRaw;      ///< Raw gear position sensor reading, 0..255
};

extern hayabusaStatus_t hayabusaStatus;

/** @brief Set up the Hayabusa specific I/O. Call once from initialiseAll(). */
void initHayabusaR3(void);

/**
 * @brief Enable or disable the MC33810 injector and ignition outputs.
 *
 * The enable line runs through a NAND gate together with the watchdog reset,
 * so the outputs are also killed by a watchdog timeout regardless of this.
 */
void hayabusaSetOutputsEnabled(bool enabled);

/** @brief Tell the watchdog service that the main loop is still running. */
void hayabusaMainLoopAlive(void);

/** @brief 1kHz service: watchdog and knock integration window. */
void hayabusaServiceFast(void);

/** @brief 30Hz service: switch inputs, gear position, lamps and heater. */
void hayabusaService(void);

#else

static inline void initHayabusaR3(void) {}
static inline void hayabusaSetOutputsEnabled(bool) {}
static inline void hayabusaMainLoopAlive(void) {}
static inline void hayabusaServiceFast(void) {}
static inline void hayabusaService(void) {}

#endif
