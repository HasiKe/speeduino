# Gen 1 Hayabusa ECU, hardware revision 3

Firmware notes for the Teensy 4.1 based ECU in `hardware/ECU` of the
[hayabusa](..) project. Everything described here is selected by building the
`teensy41_hayabusa` PlatformIO environment and choosing board **57 "Gen1
Hayabusa ECU R3"** in TunerStudio.

```
pio run -e teensy41_hayabusa
```

## Pin map

Derived from the rev 3.0 KiCad netlist (`kicad-cli sch export netlist`), not
from the older `docs/HARDWARE.md` table, which describes the Dropbear and no
longer matches this board.

| Teensy | Net / device | Firmware use |
|---|---|---|
| D0 / D1 | J1 header | Serial1, secondary serial |
| D2 | 74LVC1G00 NAND with watchdog reset -> MC33810 OUTEN | Output enable, fuel and spark kill |
| D3 | TPS3823 WDI | Watchdog kick |
| D4 | Neutral switch, J2.20 | Neutral input |
| D5 | ZXMS6006 IC3.1 -> J2.9 | Fuel pump |
| D6 | W25Q32JVSS CS | Tune store (SPI flash) |
| D7 | ADXL343 CS | Unused, parked high |
| D8 | ZXMS6006 IC3.2 -> J2.55 | Tacho output |
| D9 | DRV8434A nFAULT + BTF3050 sense | Driver fault input, active low |
| D10 | MC33810 CS | Injector and ignition driver |
| D11 / D12 / D13 | MOSI / MISO / SCLK | Shared SPI bus |
| D20 | MAX9926 COUT1 | Crank trigger |
| D21 | MAX9926 COUT2 | Cam trigger |
| D22 / D23 | MCP2562 TXD / RXD | CAN1 |
| D24 | BTF3050 U13 -> J2.30 | Boost solenoid |
| D25 | BTF3050 U14 -> J2.46 | O2 heater |
| D26 | ZXMS6006 IC2.2 -> J2.28 | Starter output, free for programmable I/O |
| D27 | ZXMS6006 IC2.1 -> J2.42 | FI lamp |
| D28 | ZXMS6006 IC4.2 -> J2.47 | Spare output, free for programmable I/O |
| D29 | ZXMS6006 IC4.1 -> J2.29 | Cooling fan |
| D30 / D31 / D32 | DRV8434A STEP / DIR / ENABLE+nSLEEP | Stepper idle valve |
| D33 | Clutch switch, J2.19 | Launch control input, map set selection |
| D34 | Tip over sensor via LMV324 comparator | Tip over cut off |
| D35 | J2.60 | Spare digital input, mapped to VSS |
| D36 / D37 | TPIC8101 INT/HOLD / CS | Knock conditioner |
| A0 / A1 / A2 | J2.58 / J2.52 / J2.49 | MAP / Baro / TPS |
| A3 | J2.38 via LMV324 buffer | O2 |
| A4 / A5 | J2.50 / J2.51 | IAT / CLT |
| A14 / A15 | J2.39 / 12V-PROT divider | Flex / battery voltage |
| A16 | J2.57 | Gear position sensor |
| A17 | TPIC8101 OUT | Knock integrator output |

Injectors and coils are not GPIO. The MC33810 low side drivers OUT0..OUT3 feed
the injectors and the gate drivers GD0..GD3, in GPGD mode, drive the
ISL9V5036 IGBTs for the coils.

### Channel order and cylinder order

Speeduino numbers its channels in firing sequence, not by cylinder: channel 1
fires at 0 degrees, channel 2 at 180, channel 3 at 360 and channel 4 at 540
(sequential), and Wasted COP pairs channels 1+3 and 2+4. J2 keeps the stock
ECU connector layout, where the coil and injector pins are numbered by
cylinder. The Hayabusa fires 1-2-4-3, so the board mapping routes channel 3 to
cylinder 4 and channel 4 to cylinder 3:

| Speeduino channel | Fires at | MC33810 | J2 | Cylinder |
|---|---|---|---|---|
| INJ1 / IGN1 | 0 | OUT0 / GD0 | J2.7 / J2.1 | 1 |
| INJ2 / IGN2 | 180 | OUT1 / GD1 | J2.6 / J2.2 | 2 |
| INJ3 / IGN3 | 360 | OUT3 / GD3 | J2.4 / J2.10 | 4 |
| INJ4 / IGN4 | 540 | OUT2 / GD2 | J2.5 / J2.3 | 3 |

Cylinder 1 is on the left (generator) side. With this mapping the TunerStudio
settings are the plain ones: Sequential or Wasted COP spark, semi-sequential
pairing "1+3 & 2+4". Earlier revisions of this branch mapped the channels by
cylinder number, which put the spark for cylinders 3 and 4 180 degrees off.

## Battery voltage

`readBat()` in Speeduino assumes the Arduino boards' divider, 24.5 V at ADC
full scale. This board divides 12V-PROT by 47k/10k into the 3.3 V ADC, so full
scale is 18.8 V. `sensors.cpp` uses `HAYABUSA_R3_BATTERY_FULL_SCALE_10` when
board 57 is selected; without that the display and the dwell and injector
voltage corrections would see 14.0 V as 18.2 V.

## Shared SPI bus

Four devices sit on the primary SPI bus: the MC33810 (6MHz, mode 0, driven from
the scheduler interrupts), the W25Q32 tune flash, the ADXL343 (unused) and the
TPIC8101 knock conditioner (mode 1). They cannot share one bus configuration,
so each driver opens its own SPI transaction per transfer, and the flash and
knock drivers hold interrupts off while their chip select is asserted, so an
MC33810 transfer from an interrupt cannot land in the middle of theirs. Those
windows are a handful of bytes long, tens of microseconds at most.

## Tune storage

The internal Teensy 4.1 EEPROM emulation is 4284 bytes, which fits the stock
4096 byte Speeduino layout but not the extra map sets. The build therefore uses
the W25Q32 as the tune store (`USE_SPI_EEPROM`): 264 sectors of 31 emulated
bytes, 8184 bytes total, in the first ~1MB of the chip.

The stock layout is unchanged, so existing tunes stay readable. The extra
tables start at address 4096.

**Switching a board from internal EEPROM to SPI flash means the tune has to be
written again.**

## Watchdog

The TPS3823 resets the Teensy, and forces MC33810 OUTEN high, unless its input
is toggled. The firmware kicks it from the 1kHz timer, but only when the main
loop has run since the last kick, so a hung main loop still trips it. While
`initialiseAll()` is running there is no main loop yet, so it is kicked
unconditionally until initialisation completes.

## Knock

The TPIC8101 is configured over SPI at startup: 4MHz prescaler (the CSTCR4M00G53
resonator), channel 1, band pass index 39 (6.37kHz), gain index 14 (unity) and
integrator index 18 (200us). The band pass default comes from the first
circumferential mode of an 81mm bore, roughly 6.5kHz.

The integration window opens on each spark and closes from the 1kHz service,
which can only close it to a millisecond. The integrator output grows with the
window length, so the reading is scaled back to the nominal 2ms window before
it is handed to Speeduino's analog knock retard.

Overridable with build flags: `HAYABUSA_KNOCK_BANDPASS_INDEX`,
`HAYABUSA_KNOCK_GAIN_INDEX`, `HAYABUSA_KNOCK_INTEGRATOR_INDEX`,
`HAYABUSA_KNOCK_WINDOW_US`.

In TunerStudio set the knock mode to analog. The knock pin setting is ignored
on this board: the driver samples A17 itself, at the end of each window.

**All four values are starting points and need checking against a knock trace
on the engine before the retard is trusted.**

## TunerStudio ini corrections

Three definitions in `reference/speeduino.ini` were wrong for this build and
are corrected on this branch:

- `ignTrim1..8` (from upstream PR #1504) were placed on page 6 at offsets
  41..48, on top of `airDenRates`, `boostFreq`, `vvtFreq`, `idleFreq` and the
  launch byte. The firmware keeps them in `config13` at offset 42..49, which
  is where the ini now puts them. Loading a tune with the old ini zeroed the
  IAT density correction above 40 C.
- The load axes of the map set 3 and 4 tables (`fuelLoad3Bins`, `ignLoad3Bins`,
  `fuelLoad4Bins`, `ignLoad4Bins`, `boostLoad3Bins`, `boostLoad4Bins`) were
  fixed kPa definitions. They now follow the fuel and ignition load algorithm
  like the main tables, so an Alpha-N tune gets a 0..100 % TPS axis with 0.5 %
  resolution instead of a fourfold compressed one.
- `boostTable3` and `boostTable4` use the same 2.0 scale as `boostTable`.

## Crank and cam trigger

The 1999 and 2000 engines carry the early crank rotor: 8 evenly spaced teeth,
no missing tooth, VR sensor. The cam sensor is a VR pickup with one pulse per
cam revolution. In TunerStudio that is the "Dual Wheel" pattern with 8 primary
teeth and "Single tooth cam"; the trigger angle has to be found with a timing
light on the engine (the values quoted for the 24-1 rotor of the 2002-2007
bikes do not apply). Both edges RISING for the MAX9926.

## Multi map switching

Four selectable map sets:

| Map set | Fuel table | Ignition table | Boost table |
|---|---|---|---|
| 1 | `fuelTable` | `ignitionTable` | `boostTable` |
| 2 | `fuelTable2` | `ignitionTable2` | `boostTable` |
| 3 | `fuelTable3` (page 16) | `ignitionTable3` (page 17) | `boostTable3` (page 20) |
| 4 | `fuelTable4` (page 18) | `ignitionTable4` (page 19) | `boostTable4` (page 21) |

Map set 2 shares Speeduino's secondary tables, so the secondary fuel and spark
blending is skipped while it is active. Do not configure the second fuel or
spark table switching at the same time as multi map switching.

Enable the "Hayabusa multi map switching" setting group in TunerStudio to see
pages 16 to 21. It cannot be combined with serial compatibility mode.

### Selecting a map set

Hold the throttle wide open with the clutch pulled in while switching the
ignition on. The ECU then:

- blinks the FI lamp once per map set number and drives the tacho at
  (map set) x 1000 rpm
- steps to the next set on each clutch pull, wrapping from 4 back to 1
- confirms on a three second clutch hold, once the clutch has been released
  at least once
- leaves selection mode on map set 1 after ten seconds without input

To stay on map set 1, either wait for the timeout or cycle all the way round
to it and hold.

The selection is not stored: the ECU always comes up on map set 1 unless the
rider enters selection mode.

## Findings from the rev 3.0 hardware

Three things worth acting on, none of which the firmware can work around:

### 1. Switch inputs cannot reach a valid logic low

Neutral (D4), clutch (D33) and the spare digital input (D35) each sit on a
10k/10k divider between the connector pin and 3V3 (R78/R80, R71/R73, R79/R81).
With the switch closed to ground the pin sits at 1.65V. The Teensy 4.1 VIL is
0.9V, so that level is not guaranteed to read as a logic low, and the clutch
based map selection and launch control depend on it.

Fix on the board: take the lower leg to ground instead of 3V3, or change the
ratio, e.g. 10k in series with a 100k pull up gives 0.3V closed and 3.3V open.

### 2. Spare analog input is not connected

`SPARE2_Sensor` (J2.53) goes through R77 and the R83/R85 divider to the net
`/Inputs 2/SPARE2_Sensor-ADC`, which reaches no Teensy pin. The signal
conditioning is fitted but the input is dead.

### 3. No start request input

The starter output (J2.28) is wired, but nothing on the connector provides a
start button signal (`DIAG-IN`, J2.22, is unconnected), so the neutral and
clutch interlock cannot be implemented. The output is left free for use as a
programmable output instead.

Also unused, and harmless: the MC33810 FB0..FB3 feedback dividers and the RSP
current sense resistor only apply to the chip's ignition mode. This board has
no connection to the MC33810 GIN pins, so the coils must be driven over SPI,
which requires GPGD mode.

## Correction to the earlier MC33810 notes

`MC33810_FIX_DOCUMENTATION.md` on the previous branch stated that the Driver
ON/OFF command places the gate drivers in bits 11..8 and the low side drivers
in bits 7..4, and changed `acc_mc33810` accordingly.

That is not what the MC33810 datasheet says. Table 20 gives the frame as:

```
bits 15..12  0011   command
bits 11..8   XXXX   don't care
bits  7..4   GPGD   gate drivers GD0..GD3
bits  3..0   OUTx   low side drivers OUT0..OUT3
```

and the text is explicit: "The Driver ON/OFF Command, bits 4 through 7 control
gate drivers that have been Mode Select Command programmed as GPGD" and "Bit 0
through bit 3 of the ON/OFF Control Command turn ON or OFF the specific output
driver."

With the previous change the injector bits addressed the gate drivers and the
ignition bits landed in the don't care nibble, so **ignition could never fire**.
The other root cause in that document, VPWR needing 12V rather than 5V, is
correct and was the real fault.

This branch uses the upstream driver unchanged, with
`mc33810InjBits = {0,1,2,3}` and `mc33810IgnBits = {4,5,6,7}`, which matches the
rev 3 wiring: INJ1..4 on OUT0..OUT3, IGN1..4 on GD0..GD3.

## Things still to calibrate on the bike

- gear position sensor thresholds (`HAYABUSA_GEAR_THRESHOLDS`, watch
  `hayabusaStatus.gearRaw`)
- tip over sensor polarity. The default assumes the sensor output goes high
  when the bike is down; `HAYABUSA_TIPOVER_INVERTED` flips it. **Check this
  before riding: an inverted sensor would cut the engine at speed.**
- knock band pass, gain, integrator and window
