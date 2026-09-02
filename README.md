<div align="center">

<img src="https://github.com/speeduino/wiki.js/raw/master/img/Speeduino%20logo_med.png" alt="Speeduino" width="600" />

##### Speeduino für die Gen 1 Suzuki Hayabusa ECU, Hardware-Revision 3

</div>

## Was dieser Branch ist

`Hayabusa/ECU-R3` ist ein Fork von [Speeduino](https://github.com/speeduino/speeduino)
für die selbstgebaute Motorsteuerung im Projekt [hayabusa](https://github.com/HasiKe/hayabusa)
(Verzeichnis `hardware/ECU`, Schaltplan-Revision 3.0).

Basis: `speeduino/master` bei `95dd0e66` (31.08.2026).

Alle Hayabusa-Funktionen hängen an `HAYABUSA_ECU_R3` bzw. an Board-ID 57 und
werden für andere Boards weder kompiliert noch zur Laufzeit betreten. Vier
Änderungen betreffen trotzdem gemeinsamen Code — sie sind unter
[Fehlerbehebungen](#4-fehlerbehebungen-die-auch-andere-boards-betreffen) und
[SPI-Bus](#2-spi-bus-wird-von-vier-geräten-geteilt) einzeln aufgeführt.

Ausführliche Beschreibung von Pinbelegung, Kalibrierung und Hardware-Befunden:
**[HAYABUSA_ECU_R3.md](HAYABUSA_ECU_R3.md)**

```bash
pio run -e teensy41_hayabusa
```

In TunerStudio Board **57 "Gen1 Hayabusa ECU R3"** wählen.

---

## Die Hardware

| | |
|---|---|
| MCU | Teensy 4.1 |
| Einspritzung + Zündung | MC33810 über SPI, GD0..GD3 im GPGD-Mode auf ISL9V5036 IGBTs |
| Kurbel / Nocke | MAX9926 VR-Aufbereitung |
| Klopfsensor | TPIC8101 mit 4 MHz Keramikresonator |
| Leerlauf | DRV8434A Schrittmotortreiber |
| Tune-Speicher | W25Q32JVSS SPI-Flash |
| CAN | MCP2562 auf CAN1 |
| Watchdog | TPS3823, extern, verriegelt die MC33810-Ausgänge |
| Lastausgänge | 3x ZXMS6006 Low-Side, 2x BTF3050TE High-Side |
| Sonstiges | ADXL343 (unbenutzt), USB-Schutz, Verpolschutz LM74700 + TPS54360B |

---

## Unterschiede zu speeduino/master

### 1. Neues Board: ID 57

`speeduino/src/pins/pinMapping.cpp` — `getHayabusaR3Mapping()`. Die Pinbelegung
stammt aus dem KiCad-Netlist der Revision 3.0, nicht aus einer Dropbear-Ableitung.
ID 57 war upstream frei, seit der Dropbear auf 55 umgezogen ist.

MC33810-Bitzuordnung passend zur R3-Verdrahtung: INJ1..4 auf OUT0..OUT3,
IGN1..4 auf GD0..GD3.

### 2. SPI-Bus wird von vier Geräten geteilt

Am primären SPI hängen MC33810 (6 MHz, Mode 0, **aus den Scheduler-Interrupts
bedient**), W25Q32-Flash, ADXL343 und TPIC8101 (Mode 1). Eine gemeinsame
Buskonfiguration gibt es damit nicht.

- `acc_mc33810.cpp`: SPI-Settings pro Transfer statt einmalig in `initMC33810()`,
  jedes `beginTransaction()` mit `endTransaction()` gepaart.
- `SPIAsEEPROM`: jeder Flash-Zugriff öffnet eine eigene Transaktion,
  Taktrate über `SPI_EEPROM_CLOCK_HZ` überschreibbar.
- `winbondflash.h`: mit `SPI_FLASH_ATOMIC_TRANSFERS` bleiben Interrupts gesperrt,
  solange das Flash-Chipselect aktiv ist. Fenster sind wenige Bytes lang,
  höchstens ein paar Dutzend Mikrosekunden.
- Der Knock-Treiber macht dasselbe für seine Mode-1-Transfers.

### 3. Tune im SPI-Flash statt im internen EEPROM

Die EEPROM-Emulation des Teensy 4.1 hat 4284 Byte — genug für das
Standard-Layout mit 4096 Byte, nicht für die zusätzlichen Kennfeldsätze.
Der Build nutzt daher `USE_SPI_EEPROM`: 264 Sektoren à 31 emulierte Byte,
8184 Byte gesamt, im ersten Megabyte des Chips.

Das Standard-Layout bleibt Byte für Byte unverändert, die neuen Tabellen
beginnen bei Adresse 4096.

> **Beim Umstieg von internem EEPROM auf SPI-Flash muss der Tune neu geschrieben werden.**

### 4. Fehlerbehebungen, die auch andere Boards betreffen

| Datei | Problem |
|---|---|
| `board_teensy41.cpp` | `pSecondarySerial` wurde in `boardInitPins()` gesetzt, das läuft aber **nach** `secondarySerial.begin()`. Die Zuweisung war wirkungslos. Betraf auch den DropBear (Board 55). Jetzt in `initBoard()`. |
| `board_teensy41.h` | `ANALOG_PINS` endete bei A16. Der Teensy 4.1 hat A0..A17; A17 war für `pinTranslateAnalog()` unerreichbar. |
| `SPIAsEEPROM.cpp` | Die SPI-Flash-Implementierung war auf `ARDUINO_ARCH_STM32` eingeschränkt, obwohl `board_teensy35.cpp` sie bereits einbindet. |
| `SPIAsEEPROM.h` | `Flash_SPI_Config` hielt `SPIClass` **per Wert**. Auf Teensy 4 ist die Klasse weder default-konstruierbar noch sinnvoll kopierbar. Jetzt per Referenz. |

---

## Neue Funktionen

### Externer Watchdog

Der TPS3823 setzt den Teensy zurück und zieht gleichzeitig MC33810 `OUTEN` auf
high, wenn sein Eingang nicht getoggelt wird. Die Firmware bedient ihn aus dem
1-kHz-Timer, aber **nur wenn die Hauptschleife seit dem letzten Toggle gelaufen
ist** — eine hängende Schleife löst den Reset also tatsächlich aus. Während
`initialiseAll()` gibt es noch keine Schleife, dort wird bedingungslos getoggelt.

Der `OUTEN`-Pfad läuft über ein NAND mit dem Watchdog-Reset und dient zugleich
als Abschaltweg für Kraftstoff und Zündung.

### Klopferkennung mit TPIC8101

Konfiguration beim Start über SPI: Prescaler 4 MHz, Kanal 1, Bandpass-Index 39
(6,37 kHz), Gain-Index 14 (1,0), Integrator-Index 18 (200 µs).
Der Bandpass-Vorgabewert kommt aus der ersten Umfangsmode bei 81 mm Bohrung,
rund 6,5 kHz.

Das Integrationsfenster öffnet bei jedem Zündfunken und schließt aus dem
1-kHz-Dienst. Der kann nur auf die Millisekunde genau schließen, und der
Integratorausgang wächst mit der Fensterlänge — der Messwert wird deshalb auf
das nominelle 2-ms-Fenster zurückgerechnet, bevor Speeduinos analoge
Zündrücknahme ihn bekommt.

Build-Flags: `HAYABUSA_KNOCK_BANDPASS_INDEX`, `HAYABUSA_KNOCK_GAIN_INDEX`,
`HAYABUSA_KNOCK_INTEGRATOR_INDEX`, `HAYABUSA_KNOCK_WINDOW_US`.

In TunerStudio Knock-Mode auf *analog* stellen. Die Pin-Einstellung wird auf
diesem Board ignoriert, der Treiber tastet A17 selbst am Fensterende ab.

### Motorrad-Ein- und Ausgänge

Im 30-Hz-Takt bedient:

- **Neutralschalter** (D4)
- **Kupplungsschalter** (D33), Polarität folgt der Launch-Control-Einstellung
- **Gangsensor** (A16), Schwellenwerte über `HAYABUSA_GEAR_THRESHOLDS`.
  Wird nach `currentStatus.gear` geschrieben, solange die VSS-Ganglogik aus ist,
  damit Boost-by-Gear, Logging und CAN den echten Gang sehen.
- **Kippschalter** (D34) mit 500 ms Entprellung, schaltet die MC33810-Ausgänge ab
- **FI-Lampe** (D27): Lampentest beim Einschalten, danach bei Motorschutz,
  Treiberfehler oder Kippschalter
- **Lambdaheizung** (D25), freigegeben wenn der Motor 3 s läuft
- **Fehlereingang** (D9) von DRV8434A `nFAULT` und den BTF3050-Stromfühlern

Starterausgang (Pin 26) und Spare-Ausgang (Pin 28) bleiben frei für Speeduinos
programmierbare I/O.

### Vier umschaltbare Kennfeldsätze

| Satz | Fuel | Zündung | Boost |
|---|---|---|---|
| 1 | `fuelTable` | `ignitionTable` | `boostTable` |
| 2 | `fuelTable2` | `ignitionTable2` | `boostTable` |
| 3 | `fuelTable3` (Seite 16) | `ignitionTable3` (Seite 17) | `boostTable3` (Seite 20) |
| 4 | `fuelTable4` (Seite 18) | `ignitionTable4` (Seite 19) | `boostTable4` (Seite 21) |

Satz 2 teilt sich die Sekundärtabellen mit Speeduino, deshalb wird das
Sekundär-Blending übersprungen solange er aktiv ist. **Zweite Fuel-/Spark-Tabelle
und Multi-Map nicht gleichzeitig konfigurieren.**

In TunerStudio die Setting-Group *"Hayabusa multi map switching"* einschalten,
dann erscheinen die Seiten 16-21. Nicht mit dem seriellen Kompatibilitätsmodus
kombinierbar.

### Kennfeldauswahl beim Einschalten

Vollgas halten, Kupplung gezogen, Zündung an. Dann:

- FI-Lampe blinkt die Satznummer, Drehzahlmesser zeigt Satz × 1000 min⁻¹
- jeder Kupplungszug schaltet weiter, nach 4 zurück auf 1
- drei Sekunden Kupplung halten bestätigt — erst nachdem die Kupplung
  einmal losgelassen wurde
- nach zehn Sekunden ohne Eingabe Abbruch auf Satz 1

Die Auswahl wird nicht gespeichert. Ohne Auswahlmodus startet die ECU immer auf
Satz 1.

---

## Vor der ersten Fahrt

> **Kippschalter-Polarität prüfen.** Der Cutoff nimmt an, dass der Sensor bei
> liegendem Motorrad HIGH liefert (Komparator U11A kippt über ca. 2,4 V).
> Ist die Polarität umgekehrt, **schaltet der Motor während der Fahrt ab**.
> Am Fahrzeug messen, bei Bedarf mit `-DHAYABUSA_TIPOVER_INVERTED` bauen.

Ebenfalls noch zu kalibrieren:

- Gangsensor-Schwellen — Rohwert steht in `hayabusaStatus.gearRaw`
- Klopf-Bandpass, Gain, Integrator und Fensterlänge gegen eine Klopfaufnahme

## Bekannte Hardware-Einschränkungen der Revision 3.0

Drei Punkte, die die Firmware nicht umgehen kann. Details in
[HAYABUSA_ECU_R3.md](HAYABUSA_ECU_R3.md).

1. **Schaltereingänge erreichen keinen gültigen Low-Pegel.** Neutral, Kupplung
   und Spare hängen je an 10k/10k gegen 3V3. Geschlossener Schalter ergibt
   1,65 V, das VIL des Teensy 4.1 liegt bei 0,9 V. Kupplungsauswahl und Launch
   Control hängen daran.
2. **Spare-Analogeingang nicht angeschlossen.** J2.53 endet über den Teiler auf
   einem Netz, das keinen Teensy-Pin erreicht.
3. **Kein Start-Anforderungseingang.** Der Starterausgang ist verdrahtet,
   `DIAG-IN` (J2.22) aber nicht — die Neutral-/Kupplungsverriegelung lässt sich
   nicht umsetzen.

## Korrektur zur früheren MC33810-Dokumentation

`MC33810_FIX_DOCUMENTATION.md` aus dem alten Branch behauptete, die Gate-Treiber
lägen in den Bits 11..8 und die Low-Side-Treiber in 7..4 des ON/OFF-Befehls.
Laut Datenblatt (Table 20) ist es umgekehrt:

```
15..12  0011   Kommando
11..8   XXXX   don't care
 7..4   GPGD   Gate Driver GD0..GD3   (Zündung)
 3..0   OUTx   Low-Side OUT0..OUT3    (Einspritzung)
```

Mit der alten Änderung hätten Einspritzbefehle die Gate-Treiber geschaltet und
die Zündbits wären im don't-care-Nibble gelandet — **die Zündung hätte nie
gefeuert**. Der zweite dort genannte Punkt, VPWR braucht 12 V statt 5 V, war
korrekt und der eigentliche Fehler.

---

## Upstream

Alles Übrige stammt unverändert aus Speeduino und steht unter GPLv3.

- Handbuch: https://wiki.speeduino.com
- Projekt: https://github.com/speeduino/speeduino
- Discord: https://discord.gg/YWCEexaNDe
