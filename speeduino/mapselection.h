#pragma once
#include <Arduino.h>

/**
 * @brief Check for map selection mode at startup
 *
 * Entry condition: TPS > 90% AND clutch engaged at power-on
 *
 * In selection mode:
 * - RPM gauge shows current map (1000=Map1, 2000=Map2, etc.)
 * - Each clutch press cycles to next map (wraps 4->1)
 * - Hold clutch 3 seconds to confirm selection
 * - Timeout after 10 seconds exits with Map 1
 *
 * Called at end of initialiseAll() after sensors are ready.
 */
void checkMapSelection(void);
