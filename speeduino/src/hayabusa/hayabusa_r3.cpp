#include "hayabusa_r3.h"

#if defined(HAYABUSA_ECU_R3)

#include <Arduino.h>
#include "knock_tpic8101.h"
#include "mapselection.h"
#include "../../globals.h"
#include "../../statuses.h"

/* Pin numbers, from the rev 3.0 schematic. These are fixed by the hardware and
 * deliberately not part of the tune. */
static constexpr uint8_t PIN_MC33810_OUTEN = 2U;  // NAND with the watchdog reset -> MC33810 OUTEN
static constexpr uint8_t PIN_WATCHDOG_WDI  = 3U;  // TPS3823 watchdog input
static constexpr uint8_t PIN_NEUTRAL       = 4U;  // Neutral switch, J2.20
static constexpr uint8_t PIN_ADXL343_CS    = 7U;  // Accelerometer, unused, parked high
static constexpr uint8_t PIN_DRIVER_FAULT  = 9U;  // DRV8434A nFAULT + BTF3050 current sense, active low
static constexpr uint8_t PIN_O2_HEATER     = 25U; // BTF3050 high side, J2.46
static constexpr uint8_t PIN_FI_LAMP       = 27U; // ZXMS6006 low side, J2.42
static constexpr uint8_t PIN_CLUTCH        = 33U; // Clutch switch, J2.19
static constexpr uint8_t PIN_TIPOVER       = 34U; // Tip over sensor comparator output
static constexpr uint8_t PIN_GEAR_POSITION = A16; // Gear position sensor, J2.57

/* The MC33810 OUTEN pin is active low and is driven by a NAND of this pin and
 * the (active low) watchdog reset. With the watchdog happy, driving this pin
 * high pulls OUTEN low and enables the injector and ignition outputs. A
 * watchdog timeout forces OUTEN high and kills them. */
static constexpr uint8_t OUTPUTS_ENABLED_LEVEL = HIGH;

/* Levels of the motorcycle switches. The switches close to ground, so a closed
 * switch reads low.
 *
 * NOTE: on rev 3.0 these three inputs (neutral, clutch, spare digital) sit on a
 * 10k/10k divider between the connector pin and 3V3, which puts the "switch
 * closed" level at 1.65V. That is above the Teensy 4.1 VIL of 0.9V, so the low
 * state is not guaranteed to be read as a logic low. The lower leg wants to go
 * to ground instead of 3V3, or the ratio wants changing (e.g. 10k series with a
 * 100k pull up gives 0.3V). */
static constexpr uint8_t SWITCH_CLOSED_LEVEL = LOW;

/* Tip over sensor. The sensor feeds a comparator (U11A) that trips when the
 * sensor output rises above about 2.4V. Suzuki tip over sensors sit low when
 * upright and go high when the bike is down, so a high here means down.
 *
 * VERIFY THIS ON THE BIKE before relying on the cut off: an inverted sensor
 * would cut the engine while riding. HAYABUSA_TIPOVER_INVERTED flips it. */
#if defined(HAYABUSA_TIPOVER_INVERTED)
static constexpr uint8_t TIPPED_OVER_LEVEL = LOW;
#else
static constexpr uint8_t TIPPED_OVER_LEVEL = HIGH;
#endif

/** @brief How long the tip over sensor must stay tripped before the cut off. */
static constexpr uint16_t TIPOVER_DEBOUNCE_MS = 500U;
/** @brief FI lamp bulb check duration after power up. */
static constexpr uint16_t FI_LAMP_BULB_CHECK_MS = 2000U;
/** @brief Engine must run this long before the O2 heater is switched on. */
static constexpr uint16_t O2_HEATER_DELAY_MS = 3000U;

/* Gear position thresholds, as raw 8 bit readings of A16. The Gen 1 gear
 * position sensor steps its output voltage per gear; these defaults are evenly
 * spaced and WANT CALIBRATING against hayabusaStatus.gearRaw on the bike. */
#if !defined(HAYABUSA_GEAR_THRESHOLDS)
  #define HAYABUSA_GEAR_THRESHOLDS { 46U, 77U, 108U, 139U, 170U, 255U }
#endif
static const uint8_t gearThresholds[6] = HAYABUSA_GEAR_THRESHOLDS;

hayabusaStatus_t hayabusaStatus;

static volatile bool mainLoopAlive = false;
static volatile bool watchdogPinState = false;
static uint32_t tipoverStartTime = 0U;
static uint32_t engineRunningSince = 0U;
static uint32_t initTime = 0U;

static bool readSwitch(uint8_t pin)
{
  return digitalReadFast(pin) == SWITCH_CLOSED_LEVEL;
}

void hayabusaSetOutputsEnabled(bool enabled)
{
  hayabusaStatus.outputsEnabled = enabled;
  digitalWriteFast(PIN_MC33810_OUTEN, enabled ? OUTPUTS_ENABLED_LEVEL : (uint8_t)!OUTPUTS_ENABLED_LEVEL);
}

void initHayabusaR3(void)
{
  if (configPage2.pinMapping != HAYABUSA_R3_BOARD_ID)
  {
    return;
  }

  hayabusaStatus = hayabusaStatus_t{};

  //Park the accelerometer chip select. It is not used, but it shares the SPI
  //bus and would otherwise float and could answer transfers meant for others.
  pinMode(PIN_ADXL343_CS, OUTPUT);
  digitalWriteFast(PIN_ADXL343_CS, HIGH);

  pinMode(PIN_WATCHDOG_WDI, OUTPUT);
  digitalWriteFast(PIN_WATCHDOG_WDI, LOW);

  pinMode(PIN_MC33810_OUTEN, OUTPUT);
  hayabusaSetOutputsEnabled(true);

  pinMode(PIN_NEUTRAL, INPUT);
  pinMode(PIN_CLUTCH, INPUT);
  pinMode(PIN_TIPOVER, INPUT);
  pinMode(PIN_DRIVER_FAULT, INPUT);

  pinMode(PIN_FI_LAMP, OUTPUT);
  digitalWriteFast(PIN_FI_LAMP, HIGH); //Bulb check

  pinMode(PIN_O2_HEATER, OUTPUT);
  digitalWriteFast(PIN_O2_HEATER, LOW);

  (void)initHayabusaKnock();

  //Map set selection, if the rider is holding the entry combination. Runs
  //before the main loop starts, while the watchdog is still kicked freely.
  checkMapSelection();

  initTime = millis();
}

void hayabusaMainLoopAlive(void)
{
  mainLoopAlive = true;
}

void hayabusaServiceFast(void)
{
  if (configPage2.pinMapping != HAYABUSA_R3_BOARD_ID)
  {
    return;
  }

  /* The TPS3823 is edge triggered and resets the board, and disables the
   * MC33810 outputs, if it is not kicked. Only kick it when the main loop has
   * run since the last kick, so that a hung main loop actually trips it. Until
   * initialisation has completed there is no main loop yet, so kick freely. */
  if (mainLoopAlive || !currentStatus.initialisationComplete)
  {
    mainLoopAlive = false;
    watchdogPinState = !watchdogPinState;
    digitalWriteFast(PIN_WATCHDOG_WDI, watchdogPinState ? HIGH : LOW);
  }

  hayabusaKnockService();
}

void hayabusaService(void)
{
  if (configPage2.pinMapping != HAYABUSA_R3_BOARD_ID)
  {
    return;
  }

  uint32_t now = millis();

  hayabusaStatus.inNeutral = readSwitch(PIN_NEUTRAL);
  //The clutch is also Speeduino's launch input, so follow that polarity setting
  hayabusaStatus.clutchPulled = (configPage6.launchHiLo > 0U)
                              ? (digitalReadFast(PIN_CLUTCH) == HIGH)
                              : (digitalReadFast(PIN_CLUTCH) == LOW);
  hayabusaStatus.driverFault = (digitalReadFast(PIN_DRIVER_FAULT) == LOW);

  //Gear position
  hayabusaStatus.gearRaw = (uint8_t)(((uint32_t)analogRead(PIN_GEAR_POSITION) * 255UL) / 1023UL);
  if (hayabusaStatus.inNeutral)
  {
    hayabusaStatus.gear = 0U;
  }
  else
  {
    uint8_t gear = 6U;
    for (uint8_t i = 0U; i < 6U; i++)
    {
      if (hayabusaStatus.gearRaw <= gearThresholds[i])
      {
        gear = i + 1U;
        break;
      }
    }
    hayabusaStatus.gear = gear;
  }

  //Publish the gear to the rest of the firmware (boost by gear, logging, CAN).
  //Speeduino's own gear detection derives the gear from the VSS to RPM ratio;
  //leave that alone if the tune uses it, as this board's sensor needs
  //calibrating before it can be trusted.
  if (configPage2.vssMode == 0U)
  {
    currentStatus.gear = hayabusaStatus.gear;
  }

  //Tip over sensor, debounced
  if (digitalReadFast(PIN_TIPOVER) == TIPPED_OVER_LEVEL)
  {
    if (tipoverStartTime == 0U)
    {
      tipoverStartTime = now;
    }
    hayabusaStatus.tippedOver = ((now - tipoverStartTime) >= TIPOVER_DEBOUNCE_MS);
  }
  else
  {
    tipoverStartTime = 0U;
    hayabusaStatus.tippedOver = false;
  }

  //The MC33810 outputs are the kill path: dropping them stops both fuel and
  //spark without fighting the schedulers.
  hayabusaSetOutputsEnabled(!hayabusaStatus.tippedOver);

  //FI lamp: bulb check on power up, then any engine protection or driver fault
  bool lampOn = ((now - initTime) < FI_LAMP_BULB_CHECK_MS)
             || currentStatus.engineProtect.isActive()
             || hayabusaStatus.driverFault
             || hayabusaStatus.tippedOver;
  digitalWriteFast(PIN_FI_LAMP, lampOn ? HIGH : LOW);

  //O2 heater, once the engine has been running for a moment
  if (currentStatus.RPM > 0U)
  {
    if (engineRunningSince == 0U)
    {
      engineRunningSince = now;
    }
    digitalWriteFast(PIN_O2_HEATER, ((now - engineRunningSince) >= O2_HEATER_DELAY_MS) ? HIGH : LOW);
  }
  else
  {
    engineRunningSince = 0U;
    digitalWriteFast(PIN_O2_HEATER, LOW);
  }
}

#endif
