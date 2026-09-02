/**
 * @file
 * @brief TPIC8101 knock signal conditioner for the Gen 1 Hayabusa ECU rev 3.
 *
 * The TPIC8101 (IC7) amplifies the knock sensor signal, passes it through a
 * programmable band pass filter, rectifies it and integrates it over a window
 * that the ECU opens with the INT/HOLD line. The integrator output is held
 * once the window closes and is read back on A17.
 *
 * This driver configures the chip over SPI and runs the integration window.
 * The resulting value is fed to Speeduino's existing analog knock handling
 * (KNOCK_MODE_ANALOG), which does the timing retard.
 *
 * The chip shares the SPI bus with the MC33810 and the tune flash, and needs
 * SPI mode 1 while the others use mode 0, so every transfer opens its own
 * transaction. Chip select doubles as the output enable of the 74LVC1G125
 * that gates the TPIC8101 SDO onto the shared MISO line.
 */
#pragma once

#include <stdint.h>

#if defined(HAYABUSA_ECU_R3)

/** @brief Configure the TPIC8101. Returns true if it echoed every command. */
bool initHayabusaKnock(void);

/** @brief Open the integration window. Called from the spark event. */
void hayabusaKnockOnSpark(uint8_t channel);

/** @brief 1kHz service. Closes the integration window and samples the output. */
void hayabusaKnockService(void);

/**
 * @brief Last integrator reading, 0..255.
 *
 * Normalised to the nominal window length, see hayabusaKnockService().
 */
uint8_t getHayabusaKnockValue(void);

/** @brief True if the TPIC8101 answered during initialisation. */
bool hayabusaKnockIsAvailable(void);

#else

static inline bool initHayabusaKnock(void) { return false; }
static inline void hayabusaKnockOnSpark(uint8_t) {}
static inline void hayabusaKnockService(void) {}
static inline uint8_t getHayabusaKnockValue(void) { return 0U; }
static inline bool hayabusaKnockIsAvailable(void) { return false; }

#endif
