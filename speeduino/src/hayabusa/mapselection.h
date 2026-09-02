/**
 * @file
 * @brief Map set selection at power up, for the Gen 1 Hayabusa ECU rev 3.
 *
 * Held at power up with the throttle wide open and the clutch pulled in, the
 * ECU enters selection mode:
 *  - the FI lamp blinks the number of the selected map set
 *  - the tacho output is driven at (map set number) x 1000 rpm
 *  - each clutch pull steps to the next map set, wrapping after the last
 *  - holding the clutch in for three seconds confirms
 *  - ten seconds without input leaves selection mode on map set 1
 *
 * This runs from initHayabusaR3(), before the main loop starts, so the
 * watchdog is still being kicked unconditionally.
 */
#pragma once

#include <stdint.h>

#if defined(HAYABUSA_ECU_R3)
void checkMapSelection(void);
#else
static inline void checkMapSelection(void) {}
#endif
