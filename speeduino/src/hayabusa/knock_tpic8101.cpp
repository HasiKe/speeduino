#include "knock_tpic8101.h"

#if defined(HAYABUSA_ECU_R3)

#include <Arduino.h>
#include <SPI.h>
#include "hayabusa_r3.h"
#include "../../globals.h"

/* ---------------------------------------------------------------------------
 * TPIC8101 SPI command set (datasheet SLIS110C, table 1)
 *
 *   010 D[4:0]     prescaler, D[4:1] = oscillator frequency, D[0] = SDO high Z
 *   1110 000 D[0]  channel select, 0 = channel 1
 *   00 D[5:0]      band pass centre frequency, index into table 3
 *   10 D[5:0]      gain, index into table 3
 *   110 D[4:0]     integrator time constant, index into table 3
 *
 * In the power up (default) mode the chip echoes each command byte back on the
 * following transfer, which is what initHayabusaKnock() checks.
 * ------------------------------------------------------------------------- */
static constexpr uint8_t TPIC_CMD_PRESCALER  = 0x40U; // 4MHz resonator (Y1), SDO active
static constexpr uint8_t TPIC_CMD_CHANNEL_1  = 0xE0U;
static constexpr uint8_t TPIC_CMD_CHANNEL_2  = 0xE1U;
static constexpr uint8_t TPIC_CMD_BANDPASS   = 0x00U; // ORed with the frequency index
static constexpr uint8_t TPIC_CMD_GAIN       = 0x80U; // ORed with the gain index
static constexpr uint8_t TPIC_CMD_INTEGRATOR = 0xC0U; // ORed with the time constant index

/* Defaults. The band pass index is the first circumferential mode of an 81mm
 * bore: 1.841 * 900m/s / (pi * 0.081m) is about 6.5kHz, table index 39 is
 * 6.37kHz. Gain index 14 is unity, integrator index 18 is 200us. All three are
 * starting points and want checking against a knock trace on the engine. */
#if !defined(HAYABUSA_KNOCK_BANDPASS_INDEX)
  #define HAYABUSA_KNOCK_BANDPASS_INDEX 39U
#endif
#if !defined(HAYABUSA_KNOCK_GAIN_INDEX)
  #define HAYABUSA_KNOCK_GAIN_INDEX 14U
#endif
#if !defined(HAYABUSA_KNOCK_INTEGRATOR_INDEX)
  #define HAYABUSA_KNOCK_INTEGRATOR_INDEX 18U
#endif
/** @brief Nominal length of the integration window after each spark, in uS. */
#if !defined(HAYABUSA_KNOCK_WINDOW_US)
  #define HAYABUSA_KNOCK_WINDOW_US 2000U
#endif

static constexpr uint8_t PIN_KNOCK_CS       = 37U;
static constexpr uint8_t PIN_KNOCK_INT_HOLD = 36U;

/* The TPIC8101 clocks SDI in on the falling edge of SCLK with SCLK idling low,
 * which is SPI mode 1. 4MHz is comfortably inside the timing specification. */
static const SPISettings knockSpiSettings(4000000, MSBFIRST, SPI_MODE1);

static bool knockAvailable = false;
static volatile bool windowOpen = false;
static volatile uint32_t windowStartTime = 0U;
static volatile uint8_t lastKnockValue = 0U;

/**
 * @brief Send one command byte and return the response of the previous one.
 *
 * The chip latches the byte when chip select goes back high, so each byte gets
 * its own chip select pulse. Interrupts are held off for the transfer: the
 * MC33810 shares this bus and is driven from the scheduler interrupts.
 */
static uint8_t knockTransfer(uint8_t command)
{
  uint8_t response;

  SPI.beginTransaction(knockSpiSettings);
  {
    uint32_t savedPrimask;
    __asm__ __volatile__ ("MRS %0, primask" : "=r" (savedPrimask));
    __asm__ __volatile__ ("cpsid i" ::);

    digitalWriteFast(PIN_KNOCK_CS, LOW);
    response = SPI.transfer(command);
    digitalWriteFast(PIN_KNOCK_CS, HIGH);

    __asm__ __volatile__ ("MSR primask, %0" : : "r" (savedPrimask));
  }
  SPI.endTransaction();

  return response;
}

bool initHayabusaKnock(void)
{
  pinMode(PIN_KNOCK_CS, OUTPUT);
  digitalWriteFast(PIN_KNOCK_CS, HIGH);
  pinMode(PIN_KNOCK_INT_HOLD, OUTPUT);
  digitalWriteFast(PIN_KNOCK_INT_HOLD, LOW); //Hold mode. SPI frames are ignored while integrating.

  const uint8_t commands[] = {
    TPIC_CMD_PRESCALER,
    TPIC_CMD_CHANNEL_1,
    (uint8_t)(TPIC_CMD_BANDPASS   | (uint8_t)HAYABUSA_KNOCK_BANDPASS_INDEX),
    (uint8_t)(TPIC_CMD_GAIN       | (uint8_t)HAYABUSA_KNOCK_GAIN_INDEX),
    (uint8_t)(TPIC_CMD_INTEGRATOR | (uint8_t)HAYABUSA_KNOCK_INTEGRATOR_INDEX),
  };

  //In the default mode the response to a command is the command itself, so the
  //response to transfer n is the echo of command n-1. Send the sequence twice
  //and check the echoes of the second pass.
  bool echoOk = true;
  for (uint8_t pass = 0U; pass < 2U; pass++)
  {
    for (uint8_t i = 0U; i < (uint8_t)sizeof(commands); i++)
    {
      uint8_t previous = (i == 0U) ? commands[sizeof(commands) - 1U] : commands[i - 1U];
      uint8_t response = knockTransfer(commands[i]);
      if ((pass == 1U) && (response != previous))
      {
        echoOk = false;
      }
    }
  }

  knockAvailable = echoOk;
  return knockAvailable;
}

void hayabusaKnockOnSpark(uint8_t channel)
{
  //Only one knock sensor input is used, so every cylinder shares the same
  //window. Restarting an open window would shorten it, so ignore overlaps.
  (void)channel;
  if (knockAvailable && !windowOpen)
  {
    windowStartTime = micros();
    windowOpen = true;
    digitalWriteFast(PIN_KNOCK_INT_HOLD, HIGH); //Integrate
  }
}

void hayabusaKnockService(void)
{
  if (!windowOpen)
  {
    return;
  }

  uint32_t elapsed = micros() - windowStartTime;
  if (elapsed < (uint32_t)HAYABUSA_KNOCK_WINDOW_US)
  {
    return;
  }

  digitalWriteFast(PIN_KNOCK_INT_HOLD, LOW); //Hold, the output now stays put
  windowOpen = false;

  //This service runs at 1kHz, so the window overshoots the nominal length by up
  //to 1ms. The integrator output grows with the window length, so scale the
  //reading back to the nominal window to keep the value comparable between
  //samples. Without this the threshold in the tune would see the jitter as
  //knock.
  uint32_t reading = (uint32_t)analogRead(A17);
  reading = (reading * (uint32_t)HAYABUSA_KNOCK_WINDOW_US) / elapsed;

  //Speeduino's analog knock handling works on an 8 bit value
  reading = (reading * 255UL) / 1023UL;
  lastKnockValue = (uint8_t)((reading > 255UL) ? 255UL : reading);
}

uint8_t getHayabusaKnockValue(void)
{
  return lastKnockValue;
}

bool hayabusaKnockIsAvailable(void)
{
  return knockAvailable;
}

#endif
