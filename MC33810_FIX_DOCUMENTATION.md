# MC33810 Treiber Fix - Hayabusa ECU

**Datum:** 2026-04-28
**Board:** Dropbear v2 basierend, Teensy 4.1
**Problem:** MC33810 kommunizierte nicht richtig mit Speeduino

---

## Symptome

- SPI-Kommunikation schien zu funktionieren (Responses kamen zurück)
- LEDs/Outputs schalteten nicht
- Fault-Flags zeigten 0xFF (alle Faults gesetzt)

---

## Ursachen

### 1. Hardware: VPWR nur 5V statt 12V

Der MC33810 braucht zwei Spannungsversorgungen:
- **VDD:** 5V für Logik
- **VPWR:** 8-40V (typisch 12V) für die Treiberstufen

Mit nur 5V auf VPWR zeigt der Chip falsche Fault-Flags und die Outputs funktionieren nicht.

**Lösung:** 12V auf VPWR-Pin legen.

---

### 2. Software: Falsches SPI-Befehlsformat im Speeduino MC33810-Treiber

#### MC33810 On/Off Command Format (laut Datasheet)

```
16-bit Befehl:
┌─────────┬─────────┬─────────┬─────────┐
│ 15...12 │ 11...8  │  7...4  │  3...0  │
├─────────┼─────────┼─────────┼─────────┤
│ Command │ GD[3:0] │OUT[3:0] │Reserved │
│  = 0011 │ Zündung │Einspritz│  = 0000 │
└─────────┴─────────┴─────────┴─────────┘
```

- **Bits 15-12:** Command = 0011 (3)
- **Bits 11-8:** GD[3:0] - Gate Driver Outputs (Zündung)
- **Bits 7-4:** OUT[3:0] - Low-Side Driver Outputs (Einspritzung)
- **Bits 3-0:** Reserved, muss 0 sein

#### Beispiele

| Zustand | Befehl | Binär |
|---------|--------|-------|
| Alles AUS | 0x3000 | 0011 0000 0000 0000 |
| Alles AN | 0x3FF0 | 0011 1111 1111 0000 |
| Nur INJ1 | 0x3010 | 0011 0000 0001 0000 |
| Nur IGN1 | 0x3100 | 0011 0001 0000 0000 |

---

## Fehler im Original-Code

### acc_mc33810.cpp (Original)

```cpp
// FALSCH - Bit-Positionen stimmen nicht mit MC33810 Format überein
uint8_t MC33810_BIT_INJ1 = 1;  // Sollte 4 sein
uint8_t MC33810_BIT_INJ2 = 2;  // Sollte 5 sein
uint8_t MC33810_BIT_INJ3 = 3;  // Sollte 6 sein
uint8_t MC33810_BIT_INJ4 = 4;  // Sollte 7 sein

uint8_t MC33810_BIT_IGN1 = 1;  // Sollte 8 sein
uint8_t MC33810_BIT_IGN2 = 2;  // Sollte 9 sein
uint8_t MC33810_BIT_IGN3 = 3;  // Sollte 10 sein
uint8_t MC33810_BIT_IGN4 = 4;  // Sollte 11 sein
```

### acc_mc33810.h (Original)

```cpp
// FALSCH - 8-bit State kann Bits 8-11 nicht adressieren!
static const uint8_t MC33810_ONOFF_CMD = 0x30;
static volatile uint8_t mc33810_1_requestedState;

// FALSCH - word(0x30, state) ergibt 0x30XX, GD-Bits sind immer 0!
#define openInjector1_MC33810() ... SPI.transfer16(word(MC33810_ONOFF_CMD, mc33810_1_requestedState)) ...
```

**Konsequenz:** Zündungs-Outputs (GD0-GD3) konnten NIE angesteuert werden!

---

## Fix

### acc_mc33810.cpp (Korrigiert)

```cpp
// Korrekte Bit-Positionen für MC33810 On/Off Command
uint8_t MC33810_BIT_INJ1 = 4;   // OUT0 = Bit 4
uint8_t MC33810_BIT_INJ2 = 5;   // OUT1 = Bit 5
uint8_t MC33810_BIT_INJ3 = 6;   // OUT2 = Bit 6
uint8_t MC33810_BIT_INJ4 = 7;   // OUT3 = Bit 7

uint8_t MC33810_BIT_IGN1 = 8;   // GD0 = Bit 8
uint8_t MC33810_BIT_IGN2 = 9;   // GD1 = Bit 9
uint8_t MC33810_BIT_IGN3 = 10;  // GD2 = Bit 10
uint8_t MC33810_BIT_IGN4 = 11;  // GD3 = Bit 11

// Für zweiten MC33810 (falls vorhanden):
uint8_t MC33810_BIT_INJ5 = 4;   // OUT0 am zweiten IC
uint8_t MC33810_BIT_INJ6 = 5;
uint8_t MC33810_BIT_INJ7 = 6;
uint8_t MC33810_BIT_INJ8 = 7;

uint8_t MC33810_BIT_IGN5 = 8;   // GD0 am zweiten IC
uint8_t MC33810_BIT_IGN6 = 9;
uint8_t MC33810_BIT_IGN7 = 10;
uint8_t MC33810_BIT_IGN8 = 11;
```

### acc_mc33810.h (Korrigiert)

```cpp
// 16-bit Command und State
static const uint16_t MC33810_ONOFF_CMD = 0x3000;
static volatile uint16_t mc33810_1_requestedState;
static volatile uint16_t mc33810_2_requestedState;
static volatile uint16_t mc33810_1_returnState;
static volatile uint16_t mc33810_2_returnState;

// Korrigierte Macros - OR statt word()
#define openInjector1_MC33810() MC33810_1_ACTIVE(); \
    BIT_SET(mc33810_1_requestedState, MC33810_BIT_INJ1); \
    mc33810_1_returnState = SPI.transfer16(MC33810_ONOFF_CMD | mc33810_1_requestedState); \
    MC33810_1_INACTIVE()

#define coil1High_MC33810() MC33810_1_ACTIVE(); \
    BIT_SET(mc33810_1_requestedState, MC33810_BIT_IGN1); \
    mc33810_1_returnState = SPI.transfer16(MC33810_ONOFF_CMD | mc33810_1_requestedState); \
    MC33810_1_INACTIVE()
```

---

## Test-Sketch

Ein Test-Sketch wurde erstellt unter:
```
/home/kevin/projects/hayabusa/speeduino/test_mc33810/
```

### Kompilieren und Hochladen

```bash
# PlatformIO in venv installieren (falls nicht vorhanden)
python3 -m venv /tmp/pio_venv
/tmp/pio_venv/bin/pip install platformio

# Kompilieren
cd /home/kevin/projects/hayabusa/speeduino/test_mc33810
/tmp/pio_venv/bin/platformio run -e teensy41

# Hochladen (mit sudo wegen USB-Rechte)
sudo ~/.platformio/packages/tool-teensy/teensy_loader_cli \
    --mcu=TEENSY41 -v -s \
    .pio/build/teensy41/firmware.hex

# Serial Monitor
sudo chmod 666 /dev/ttyACM1
python3 -c "import serial; s=serial.Serial('/dev/ttyACM1',115200); print(s.read(4096).decode())"
```

### Test-Befehle (über Serial)

| Taste | Funktion |
|-------|----------|
| r | Diagnose neu starten |
| t | Alle Outputs toggeln |
| 1-4 | OUT0-OUT3 einzeln toggeln |
| 5-8 | GD0-GD3 einzeln toggeln |
| s | Status auslesen |

---

## Pin-Belegung (Case 57)

### Teensy 4.1 SPI

| Signal | Pin |
|--------|-----|
| MOSI | 11 |
| MISO | 12 |
| SCK | 13 |
| CS (MC33810) | 10 |

### Isolation Pins

Laut User müssen diese Pins HIGH sein um nur mit MC33810 zu reden:
- D7 = HIGH
- D6 = HIGH
- D2 = HIGH

### MC33810 Outputs

| Speeduino | MC33810 | Funktion |
|-----------|---------|----------|
| pinInjector1 | OUT0 | Einspritzung 1 |
| pinInjector2 | OUT1 | Einspritzung 2 |
| pinInjector3 | OUT2 | Einspritzung 3 |
| pinInjector4 | OUT3 | Einspritzung 4 |
| pinCoil1 | GD0 | Zündung 1 |
| pinCoil2 | GD1 | Zündung 2 |
| pinCoil3 | GD2 | Zündung 3 |
| pinCoil4 | GD3 | Zündung 4 |

---

## MC33810 Initialisierung

Der MC33810 muss vor Benutzung konfiguriert werden:

```cpp
void initMC33810(void) {
    // 1. GD Outputs auf GPGD-Modus setzen
    // Command 0x1F00 = Mode Select, alle GD auf GPGD
    SPI.transfer16(0x1F00);

    // 2. Open Load Detection beim Ausschalten deaktivieren
    // (sonst ständige Fault-Flags ohne Last)
    // Command 0x28F0 = LSD Fault Config
    SPI.transfer16(0x28F0);
}
```

---

## Fault-Flags (Normal ohne Last)

Beim Testen ohne angeschlossene Injektoren/Spulen sind diese Faults **normal**:

- **LSD Short to Ground:** Normal, weil Output offen
- **GD Open Load:** Normal, keine Spule angeschlossen
- **GD Low VDS:** Normal, kein Strom fließt

Mit angeschlossener Last sollten diese verschwinden.

---

## Checkliste für zukünftige Probleme

1. [ ] VPWR auf 12V prüfen (nicht nur 5V!)
2. [ ] VDD auf 5V prüfen
3. [ ] SPI-Verbindungen prüfen (MOSI, MISO, SCK, CS)
4. [ ] CS-Pin muss während Transfer LOW sein
5. [ ] Isolation-Pins richtig setzen (D7, D6, D2 HIGH)
6. [ ] Test-Sketch hochladen und Diagnose laufen lassen
7. [ ] Bei 0xFF Faults: Meist Hardware-Problem (Spannung, Verbindung)
8. [ ] Bei 0x0F Faults: Normal ohne Last

---

## Dateien die geändert wurden

```
speeduino/acc_mc33810.cpp  - Bit-Positionen korrigiert
speeduino/acc_mc33810.h    - 16-bit State, korrigierte Macros
```

---

## Referenzen

- MC33810 Datasheet: NXP/Freescale
- Speeduino Wiki: https://wiki.speeduino.com
- Dropbear v2 Schematic: [dein Schematic hier]
