/**
 * @file mapselection.cpp
 * @brief Multi-map selection at startup via clutch and TPS
 *
 * Entry: TPS > 90% AND clutch engaged at power-on
 * Selection: Clutch press cycles maps 1->2->3->4->1
 * Display: RPM gauge shows 1000/2000/3000/4000
 * Confirm: Hold clutch 3 seconds
 * Timeout: 10 seconds exits with Map 1
 */

#include "mapselection.h"
#include "globals.h"
#include "sensors.h"

// Configuration
static constexpr uint8_t MAP_SELECTION_TPS_THRESHOLD = 90;  // TPS > 90% to enter
static constexpr uint16_t MAP_CONFIRM_HOLD_MS = 3000;       // 3 seconds to confirm
static constexpr uint16_t MAP_SELECTION_TIMEOUT_MS = 10000; // 10 seconds timeout
static constexpr uint16_t DEBOUNCE_MS = 50;                 // Debounce time

/**
 * @brief Read clutch state respecting polarity setting
 * @return true if clutch is engaged/pressed
 */
static bool readClutch(void)
{
  if(configPage6.launchHiLo > 0) {
    return digitalRead(pinLaunch) == HIGH;
  } else {
    return digitalRead(pinLaunch) == LOW;
  }
}

/**
 * @brief Read TPS as percentage (0-100)
 * @return TPS percentage
 */
static uint8_t readTPSPercent(void)
{
  // Map 10-bit ADC (0-1023) to 0-100%
  uint16_t raw = analogRead(pinTPS);
  return (uint8_t)((raw * 100UL) / 1023UL);
}

/**
 * @brief Update RPM display to show current map selection
 * @param mapNum Map number 0-3 (displays as 1000-4000 RPM)
 */
static void displayMapOnRPM(uint8_t mapNum)
{
  // Show map 1-4 as 1000-4000 RPM on gauge
  currentStatus.RPM = (mapNum + 1) * 1000;
  currentStatus.RPMdiv100 = currentStatus.RPM / 100;
}

void checkMapSelection(void)
{
  // Always start with Map 1
  setMapSet(0);

  // Read initial state
  uint8_t tps = readTPSPercent();
  bool clutchPressed = readClutch();

  // Check entry condition: TPS > 90% AND clutch engaged
  if(tps <= MAP_SELECTION_TPS_THRESHOLD || !clutchPressed) {
    return; // Normal boot, stay on Map 1
  }

  // === Enter Map Selection Mode ===

  uint8_t selectedMap = 0;  // Start with Map 1 (index 0)
  displayMapOnRPM(selectedMap);

  uint32_t selectionStartTime = millis();
  uint32_t clutchPressedTime = millis();  // Track when clutch was pressed
  bool previousClutchState = true;        // Started with clutch pressed
  uint32_t lastActivityTime = millis();

  // Selection loop
  while(true)
  {
    uint32_t now = millis();

    // Check timeout (10 seconds of no activity)
    if((now - lastActivityTime) >= MAP_SELECTION_TIMEOUT_MS)
    {
      // Timeout - exit with Map 1
      setMapSet(0);
      displayMapOnRPM(0);
      return;
    }

    // Read current clutch state
    bool currentClutchState = readClutch();

    // Detect rising edge (clutch newly pressed)
    if(currentClutchState && !previousClutchState)
    {
      // Debounce
      delay(DEBOUNCE_MS);
      if(readClutch())
      {
        // Confirmed press - cycle to next map
        selectedMap = (selectedMap + 1) % 4;
        displayMapOnRPM(selectedMap);
        clutchPressedTime = millis();
        lastActivityTime = millis();
      }
    }

    // Check for 3-second hold to confirm
    if(currentClutchState)
    {
      if((now - clutchPressedTime) >= MAP_CONFIRM_HOLD_MS)
      {
        // Confirmed! Set the selected map and exit
        setMapSet(selectedMap);

        // Flash confirmation: briefly show 0 RPM then back to selected
        currentStatus.RPM = 0;
        delay(200);
        displayMapOnRPM(selectedMap);
        delay(200);
        currentStatus.RPM = 0;
        delay(200);
        displayMapOnRPM(selectedMap);

        return;
      }
    }
    else
    {
      // Clutch released - reset hold timer
      clutchPressedTime = now;
    }

    previousClutchState = currentClutchState;

    // Small delay to prevent tight loop
    delay(10);
  }
}
