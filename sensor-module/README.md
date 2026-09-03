# Hayabusa Sensor-Modul

Firmware für die zweite Platine im Hayabusa-Projekt (Schaltplan
`hardware/ECU/sensor-module.kicad_sch` im Hardware-Repository, Revision
3.0-draft). Ein STM32G474RBT6 liest vier Abgastemperaturen, acht
Analogkanäle und regelt den Ladeluftkühler-Lüfter. Alle Werte gehen per
Classic CAN an die Speeduino-ECU.

```bash
pio run -e sensor_module            # Debug, -Og
pio run -e sensor_module_release    # Release, -Os
pio run -e sensor_module -t upload  # über ST-Link an J4
```

Aktuell: 19,5 kB Flash von 128 kB, 912 Byte RAM von 128 kB.

## Hardware

| | |
|---|---|
| MCU | STM32G474RBT6, LQFP64 |
| Takt | 16 MHz Keramikresonator (CSTNE16M0V530000R0) → PLL → 170 MHz, Boost-Mode, 4 Waitstates |
| EGT | 4x MAX31855KASA+ an SPI2, 2,66 MHz, Mode 0 |
| Analog | ADC1 (7 Kanäle) + ADC2 (2 Kanäle), DMA-Ringpuffer, 16-fach Hardware-Oversampling |
| Lüfter | TIM3_CH3 auf PB0 → 100 Ω → SQM120N06 Low-Side |
| CAN | FDCAN1 im Classic-Mode, 500 kBit/s, MCP2562FD |
| Debug | J4: 1=3V3, 2=GND, 3=NRST, 4=SWO, 5=SWCLK, 6=SWDIO |

### Pinbelegung

| Pin | Funktion | Beschaltung |
|---|---|---|
| PA0 | 5V-Rail-Überwachung | 10k/15k Teiler |
| PA1 | Quickshifter Push (J1.8) | 1k Serie, 10k Pullup 3V3 |
| PA2 | Quickshifter Pull (J1.10) | 1k Serie, 10k Pullup 3V3 |
| PA3 | Abgasdruck (J3.11) | 1k Serie, 1k/2k Teiler |
| PA4 | LLK-Wassertemperatur 1 (J2.6) | 1k Serie, 1k Pullup 3V3 |
| PA5 | LLK-Wassertemperatur 2 (J2.11) | 1k Serie, 1k Pullup 3V3 |
| PC0 | Öldruck (J2.10) | 1k Serie, 1k/2k Teiler |
| PC1 | Kraftstoffdruck (J2.3) | 1k Serie, 1k/2k Teiler |
| PC2 | Öltemperatur (J2.4) | 1k Serie, 1k Pullup 3V3 |
| PB0 | Lüfter-PWM | 100 Ω → Gate, 10k Pulldown |
| PB6 / PB10 / PB11 / PB12 | Chipselect EGT 1 / 2 / 3 / 4 | 10k Pullup 3V3 |
| PB13 / PB14 / PB15 | SPI2 SCK / MISO / MOSI | MOSI ohne Empfänger |
| PA11 / PA12 | FDCAN1 RX / TX | MCP2562FD |

> **Die CS-Reihenfolge ist nicht durchlaufend.** IC2 trägt Thermoelement 3,
> IC3 trägt Thermoelement 2. Die Firmware bildet das ab
> (`csLines[]` in `src/egt.c`), das Netlist ist die Referenz.

## CAN-Protokoll

Classic CAN 2.0A, 500 kBit/s, 11-Bit-IDs, **Big Endian**. Basis-ID `0x580`,
über `-DCAN_BASE_ID=0x...` verschiebbar. Sendeintervall 50 ms.

### 0x580 — Abgastemperaturen

| Byte | Inhalt | Einheit |
|---|---|---|
| 0-1 | EGT 1 | °C, int16 |
| 2-3 | EGT 2 | °C, int16 |
| 4-5 | EGT 3 | °C, int16 |
| 6-7 | EGT 4 | °C, int16 |

### 0x581 — Drücke und Öltemperatur

| Byte | Inhalt | Einheit |
|---|---|---|
| 0-1 | Öldruck | kPa, int16 |
| 2-3 | Kraftstoffdruck | kPa, int16 |
| 4-5 | Abgasdruck | kPa, int16 |
| 6-7 | Öltemperatur | 0,1 °C, int16 |

### 0x582 — Ladeluftkühler und Quickshifter

| Byte | Inhalt | Einheit |
|---|---|---|
| 0-1 | LLK-Wassertemperatur 1 | 0,1 °C, int16 |
| 2-3 | LLK-Wassertemperatur 2 | 0,1 °C, int16 |
| 4-5 | Quickshifter Push | mV am Pin, uint16 |
| 6-7 | Quickshifter Pull | mV am Pin, uint16 |

### 0x583 — Status

| Byte | Inhalt |
|---|---|
| 0-1 | 5V-Rail in mV, uint16 |
| 2 | Lüfter-Duty in % |
| 3 | EGT-Fehlerbits, 2 Bit je Kanal: Bit 0 Fehler, Bit 1 keine Antwort |
| 4 | Statusflags |
| 5 | Zähler, +1 je Sendezyklus — steht er still, sendet das Modul nicht mehr |
| 6-7 | Kaltstellentemperatur von IC1, °C int16 |

Statusflags in Byte 4:

| Bit | Bedeutung |
|---|---|
| 0 | Lüfter wird per CAN übersteuert |
| 1 | Lüfter läuft |
| 2 | mindestens ein EGT-Kanal meldet Fehler |
| 3 | 5V-Rail außerhalb 4,5–5,5 V |
| 4 | Bus-Off seit dem letzten Reset aufgetreten |

### 0x587 — Kommando an das Modul

| Byte | Inhalt |
|---|---|
| 0 | `0x01` = Lüfter übersteuern |
| 1 | Duty 0–100, oder `0xFF` um die Übersteuerung freizugeben |

Kommt 2 s lang kein Kommando, fällt das Modul selbsttätig auf die lokale
Regelung zurück. Ein CAN-Ausfall lässt den Lüfter also nicht auf dem zuletzt
kommandierten Wert stehen.

**Ungültige Messwerte werden als `INT16_MIN` (`0x8000`) gesendet**, nicht als 0.
Ein offenes Thermoelement liefert also nicht 0 °C.

## Anbindung an Speeduino

ECU-seitig ist kein Code nötig. Speeduino hat 16 generische CAN-Eingänge
(`canin[0..15]`), die in TunerStudio unter *CAN Inputs* konfiguriert werden:
CAN-Adresse, Startbyte, 1 oder 2 Byte, Endianness.

> **Speeduino addiert `TS_CAN_OFFSET` = 0x100 auf die eingetragene Adresse**
> (`comms_CAN.cpp`, `readAuxCanBus()`). In TunerStudio also `Sende-ID - 0x100`
> eintragen: für 0x580 den Wert **0x480**.

Endianness auf **Big Endian** stellen (Speeduinos Vorgabe,
`caninputEndianess == 0`).

Vollständige Zuordnung aller 16 Kanäle:

| Kanal | TS-Adresse | Frame | Startbyte | Größe | Wert |
|---|---|---|---|---|---|
| canin0  | 0x480 | 0x580 | 0 | 2 | EGT 1 |
| canin1  | 0x480 | 0x580 | 2 | 2 | EGT 2 |
| canin2  | 0x480 | 0x580 | 4 | 2 | EGT 3 |
| canin3  | 0x480 | 0x580 | 6 | 2 | EGT 4 |
| canin4  | 0x481 | 0x581 | 0 | 2 | Öldruck |
| canin5  | 0x481 | 0x581 | 2 | 2 | Kraftstoffdruck |
| canin6  | 0x481 | 0x581 | 4 | 2 | Abgasdruck |
| canin7  | 0x481 | 0x581 | 6 | 2 | Öltemperatur |
| canin8  | 0x482 | 0x582 | 0 | 2 | LLK-Wasser 1 |
| canin9  | 0x482 | 0x582 | 2 | 2 | LLK-Wasser 2 |
| canin10 | 0x482 | 0x582 | 4 | 2 | Quickshifter Push |
| canin11 | 0x482 | 0x582 | 6 | 2 | Quickshifter Pull |
| canin12 | 0x483 | 0x583 | 0 | 2 | 5V-Rail |
| canin13 | 0x483 | 0x583 | 2 | 1 | Lüfter-Duty |
| canin14 | 0x483 | 0x583 | 3 | 1 | EGT-Fehlerbits |
| canin15 | 0x483 | 0x583 | 4 | 1 | Statusflags |

Damit sind alle 16 Kanäle belegt. Zähler und Kaltstellentemperatur bleiben
außen vor — wer die braucht, tauscht sie gegen einen der weniger wichtigen
Kanäle.

Beide Enden hängen am selben Bus mit 500 kBit/s. Terminierung: JP2 auf dem
Sensor-Modul schließt die geteilte Terminierung (2x 60,4 Ω mit 4,7 nF gegen
Masse). Genau zwei Terminierungen am gesamten Bus, nicht mehr.

## Lüfterregelung

Läuft lokal auf dem Modul, damit sie einen CAN-Ausfall überlebt.

- Referenz ist der **wärmere** der beiden LLK-Wassersensoren
- unter `FAN_ON_TEMP_C` (35 °C) aus
- ab `FAN_ON_TEMP_C` linear von `FAN_MIN_DUTY_PCT` (30 %) bis 100 % bei
  `FAN_FULL_TEMP_C` (50 °C)
- Abschalten erst `FAN_OFF_HYSTERESIS_C` (4 K) unter der Einschaltschwelle
- **Fallen beide Wassersensoren aus, läuft der Lüfter mit
  `FAN_FAILSAFE_DUTY_PCT` (100 %)** — unnötig kühlen ist billiger als einen
  Ladeluftkühler zu überhitzen, weil ein Stecker abgefallen ist

Alle Schwellen sind Build-Flags.

## Was noch kalibriert werden muss

Die Schaltung gibt die Beschaltung vor, nicht aber die verbauten Sensoren.
Diese Vorgaben sind Startwerte und in `include/config.h` als solche markiert:

| Flag | Vorgabe | Was daran zu prüfen ist |
|---|---|---|
| `NTC_R25_OHMS` / `NTC_BETA` | 2500 Ω / 3450 | Kennlinie der Öl- und LLK-Temperatursensoren |
| `PRESSURE_SENSOR_MIN_MV` / `_MAX_MV` | 500 / 4500 mV | ratiometrische Standardsensoren angenommen |
| `OIL_PRESSURE_MAX_KPA` | 1000 | Messbereich des verbauten Gebers |
| `FUEL_PRESSURE_MAX_KPA` | 1000 | dito |
| `EXHAUST_PRESSURE_MAX_KPA` | 500 | dito |
| `FAN_ON_TEMP_C` / `FAN_FULL_TEMP_C` | 35 / 50 °C | am Fahrzeug ermitteln |

Zum Prüfen der NTC-Kennlinie: der Widerstand folgt direkt aus dem Teiler
(1 kΩ Pullup gegen 3V3), `ntc_temp_dc()` in `src/analog.c` rechnet ihn ohnehin
aus. Wenn eine Temperatur unplausibel ist, zuerst dort ansetzen.

Der Quickshifter wird bewusst **nicht** ausgewertet, sondern nur als Spannung
am Pin gemeldet. Ob dort ein Schalter, ein Dehnungsmessstreifen oder ein
Hall-Sensor hängt, geht aus dem Schaltplan nicht hervor (1 kΩ Serie, 10 kΩ
Pullup gegen 3V3 passt zu mehreren Varianten). Die Schwellen gehören in die
ECU, sobald die Kennlinie bekannt ist.

## Anmerkung zur Lüfter-Ansteuerung

PB0 treibt das Gate des SQM120N06 direkt über 100 Ω, ohne Gate-Treiber. Bei
3,3 V Ansteuerung und der Gate-Ladung eines 120-A-MOSFETs liegen die
Schaltflanken im Mikrosekundenbereich. Die PWM-Frequenz ist deshalb auf
200 Hz gesetzt (`FAN_PWM_FREQ_HZ`) — hoch genug, dass der Lüfter nicht ruckelt,
niedrig genug, dass der MOSFET nicht nennenswert in der Schaltflanke heizt.

Höhere Frequenzen (20 kHz, außerhalb des Hörbereichs) wären nur mit einem
echten Gate-Treiber sinnvoll. Falls der Lüfter bei 200 Hz hörbar brummt, ist
das der Ansatzpunkt für die nächste Revision.

## Aufbau

```
src/
  main.c            kooperativer Scheduler, sonst nichts
  board.c           Taktbaum, Peripherie-Takte, Panik-Pfad
  egt.c             MAX31855K, vier Kanäle
  analog.c          ADC1/ADC2 mit DMA, Sensor-Skalierung
  fan.c             Regelung und PWM
  can_bus.c         FDCAN1, Senden und Kommandoempfang
  stm32g4xx_it.c    Core-Interrupt-Handler
include/config.h    sämtliche Konstanten aus dem Schaltplan an einer Stelle
```

Zyklen im Hauptloop: CAN-Empfang jede Runde, Analog und Lüfter alle 20 ms,
EGT alle 100 ms (Wandlungszeit des MAX31855), CAN-Senden alle 50 ms.

Bei einem nicht behebbaren Fehler (`board_panic()`) fährt das Modul den
Lüfter auf Failsafe-Duty und bleibt stehen. Ein unabhängiger Watchdog ist
**nicht** aktiviert — das wäre der nächste sinnvolle Schritt, sobald das Modul
am Fahrzeug läuft.
