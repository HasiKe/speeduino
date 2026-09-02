#include "mapselection.h"

#if defined(HAYABUSA_ECU_R3)

#include <Arduino.h>
#include "hayabusa_r3.h"
#include "../../globals.h"

static constexpr uint8_t  TPS_ENTRY_THRESHOLD_PERCENT = 90U;
static constexpr uint16_t CONFIRM_HOLD_MS = 3000U;
static constexpr uint16_t SELECTION_TIMEOUT_MS = 10000U;
static constexpr uint16_t DEBOUNCE_MS = 50U;

static constexpr uint8_t PIN_CLUTCH  = 33U;
static constexpr uint8_t PIN_FI_LAMP = 27U;
static constexpr uint8_t PIN_TACHO   = 8U;

static bool clutchPulled(void)
{
  //Same polarity as launch control, so that one setting covers both uses
  if (configPage6.launchHiLo > 0U)
  {
    return digitalReadFast(PIN_CLUTCH) == HIGH;
  }
  return digitalReadFast(PIN_CLUTCH) == LOW;
}

static uint8_t readTpsPercent(void)
{
  uint16_t raw = (uint16_t)(((uint32_t)analogRead(pinNumbers.pinTPS) * 255UL) / 1023UL);
  uint8_t tpsMin = configPage2.tpsMin;
  uint8_t tpsMax = configPage2.tpsMax;

  if (tpsMax > tpsMin)
  {
    if (raw <= tpsMin) { return 0U; }
    if (raw >= tpsMax) { return 100U; }
    return (uint8_t)(((uint32_t)(raw - tpsMin) * 100UL) / (uint32_t)(tpsMax - tpsMin));
  }
  //Reversed or uncalibrated TPS: fall back to the raw reading
  return (uint8_t)(((uint32_t)raw * 100UL) / 255UL);
}

/**
 * @brief Half period of the tacho square wave for a given engine speed.
 *
 * Speeduino pulses the tacho output once per ignition event, optionally
 * divided down. A four stroke fires nCylinders/2 times per crank revolution.
 */
static uint32_t tachoHalfPeriodUs(uint16_t rpm)
{
  uint8_t sparksPerRev = configPage2.nCylinders / 2U;
  if (sparksPerRev == 0U) { sparksPerRev = 1U; }

  uint16_t divider = (uint16_t)configPage2.tachoDiv + 1U;
  uint32_t pulsesPerMinute = ((uint32_t)rpm * (uint32_t)sparksPerRev) / (uint32_t)divider;
  if (pulsesPerMinute == 0U) { pulsesPerMinute = 1U; }

  return 30000000UL / pulsesPerMinute; //Half of 60e6 / pulses per minute
}

/** @brief Drive the tacho output and poll the clutch for the given time. */
static bool waitAndDriveTacho(uint16_t rpm, uint32_t durationMs, bool *pClutchState)
{
  uint32_t halfPeriod = tachoHalfPeriodUs(rpm);
  uint32_t start = millis();
  uint32_t lastEdge = micros();
  bool level = false;
  bool clutchSeen = false;

  while ((millis() - start) < durationMs)
  {
    if ((micros() - lastEdge) >= halfPeriod)
    {
      lastEdge += halfPeriod;
      level = !level;
      digitalWriteFast(PIN_TACHO, level ? HIGH : LOW);
    }
    if (clutchPulled()) { clutchSeen = true; }
  }

  if (pClutchState != NULL) { *pClutchState = clutchPulled(); }
  return clutchSeen;
}

/** @brief Blink the FI lamp @p count times to show the selected map set. */
static void blinkMapNumber(uint8_t count)
{
  for (uint8_t i = 0U; i < count; i++)
  {
    digitalWriteFast(PIN_FI_LAMP, HIGH);
    (void)waitAndDriveTacho(0U, 200U, NULL);
    digitalWriteFast(PIN_FI_LAMP, LOW);
    (void)waitAndDriveTacho(0U, 200U, NULL);
  }
}

void checkMapSelection(void)
{
  (void)setMapSet(0U);

  //Entry condition: throttle wide open and clutch in, both at power up
  if ((readTpsPercent() <= TPS_ENTRY_THRESHOLD_PERCENT) || !clutchPulled())
  {
    return;
  }

  pinMode(PIN_TACHO, OUTPUT);

  uint8_t selected = 0U;
  bool previousClutch = true; //Entry condition means the clutch is already in
  //The clutch has to be released once before a hold counts as a confirmation,
  //otherwise simply holding the entry combination for three seconds confirms
  //map set 1 before the rider can select anything.
  bool armed = false;
  uint32_t lastActivity = millis();
  uint32_t clutchHeldSince = millis();

  blinkMapNumber(selected + 1U);

  while ((millis() - lastActivity) < SELECTION_TIMEOUT_MS)
  {
    bool clutchNow = false;
    (void)waitAndDriveTacho((uint16_t)((selected + 1U) * 1000U), 20U, &clutchNow);

    if (clutchNow && !previousClutch)
    {
      //Rising edge: step to the next map set
      (void)waitAndDriveTacho((uint16_t)((selected + 1U) * 1000U), DEBOUNCE_MS, &clutchNow);
      if (clutchNow)
      {
        selected = (uint8_t)((selected + 1U) % MAX_MAP_SETS);
        blinkMapNumber(selected + 1U);
        clutchHeldSince = millis();
        lastActivity = millis();
      }
    }
    else if (clutchNow)
    {
      if (armed && ((millis() - clutchHeldSince) >= CONFIRM_HOLD_MS))
      {
        //Confirmed
        (void)setMapSet(selected);
        blinkMapNumber(selected + 1U);
        digitalWriteFast(PIN_TACHO, LOW);
        return;
      }
    }
    else
    {
      clutchHeldSince = millis();
      armed = true;
    }

    previousClutch = clutchNow;
  }

  //Timed out without a confirmation: stay on map set 1
  (void)setMapSet(0U);
  digitalWriteFast(PIN_TACHO, LOW);
}

#endif
