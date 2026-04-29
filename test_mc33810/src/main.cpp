/*
 * MC33810 SPI Communication Test v2 for Hayabusa ECU
 * Detailed fault diagnostics
 *
 * Pin isolation setup:
 * - D7, D6, D2 = HIGH (disable other chips)
 * - D10 = CS for MC33810
 */

#include <SPI.h>

// Pin definitions
const int PIN_CS_MC33810 = 10;
const int PIN_ISO_D7 = 7;
const int PIN_ISO_D6 = 6;
const int PIN_ISO_D2 = 2;

// MC33810 Commands per datasheet
const uint16_t CMD_MODE_SELECT = 0x1000;  // Mode Select
const uint16_t CMD_LSD_FAULT   = 0x2000;  // LSD Fault Config
const uint16_t CMD_ON_OFF      = 0x3000;  // On/Off Control
const uint16_t CMD_SPARK       = 0x4000;  // Spark Command
const uint16_t CMD_END_SPARK   = 0x5000;  // End Spark Time
const uint16_t CMD_DAC         = 0x6000;  // DAC
const uint16_t CMD_LSD_STATUS  = 0xA000;  // LSD Status Request
const uint16_t CMD_GD_STATUS   = 0xB000;  // GD Status Request
const uint16_t CMD_ALL_STATUS  = 0xD000;  // All Status
const uint16_t CMD_ID          = 0xE000;  // Device ID
const uint16_t CMD_NOP         = 0x0000;  // NOP

// Forward declarations
uint16_t spiTransfer(uint16_t cmd);
void printBinary16(uint16_t val);
void runDiagnostics();
void decodeLSDStatus(uint16_t status);
void decodeGDStatus(uint16_t status);
void decodeAllStatus(uint16_t status);
void initMC33810();
void testOutputs();

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println("\n======================================");
  Serial.println("MC33810 Detailed Diagnostic Test");
  Serial.println("Hayabusa ECU - Dropbear v2");
  Serial.println("======================================\n");

  // Setup isolation pins
  Serial.println("Setting isolation pins:");
  pinMode(PIN_ISO_D7, OUTPUT);
  pinMode(PIN_ISO_D6, OUTPUT);
  pinMode(PIN_ISO_D2, OUTPUT);

  digitalWrite(PIN_ISO_D7, HIGH);
  digitalWrite(PIN_ISO_D6, HIGH);
  digitalWrite(PIN_ISO_D2, HIGH);
  Serial.println("  D7=HIGH, D6=HIGH, D2=HIGH");

  // Setup CS
  pinMode(PIN_CS_MC33810, OUTPUT);
  digitalWrite(PIN_CS_MC33810, HIGH);
  Serial.println("  D10(CS)=HIGH (inactive)\n");

  // Init SPI
  SPI.begin();
  Serial.println("SPI initialized (Mode 0, 6MHz, MSB first)\n");

  delay(100);

  // Initialize MC33810
  initMC33810();

  delay(100);

  // Run diagnostics
  runDiagnostics();
}

uint16_t spiTransfer(uint16_t cmd) {
  uint16_t response;
  SPI.beginTransaction(SPISettings(6000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS_MC33810, LOW);
  delayMicroseconds(1);
  response = SPI.transfer16(cmd);
  delayMicroseconds(1);
  digitalWrite(PIN_CS_MC33810, HIGH);
  SPI.endTransaction();
  return response;
}

void printBinary16(uint16_t val) {
  for (int i = 15; i >= 0; i--) {
    Serial.print((val >> i) & 1);
    if (i == 12 || i == 8 || i == 4) Serial.print(" ");
  }
}

void initMC33810() {
  Serial.println("======================================");
  Serial.println("INITIALIZING MC33810");
  Serial.println("======================================\n");

  uint16_t response;

  // 1. Set all GD outputs to GPGD mode
  // Command: 0001 [GD3:GPGD][GD2:GPGD][GD1:GPGD][GD0:GPGD] xxxx xxxx
  // 0x1F00 = 0001 1111 0000 0000 = All GDs in GPGD mode
  Serial.println("1. Setting GD outputs to GPGD mode...");
  response = spiTransfer(0x1F00);
  Serial.print("   TX: 0x1F00  RX: 0x"); Serial.println(response, HEX);

  // 2. Configure LSD fault settings
  // Disable open load detection when output is OFF (common for testing)
  // 0010 [shutdown mode=1000] [OL when ON=1111] [OL when OFF=0000]
  // 0x28F0 = Shutdown on fault, OL detect when ON only
  Serial.println("2. Configuring LSD fault settings...");
  response = spiTransfer(0x28F0);
  Serial.print("   TX: 0x28F0  RX: 0x"); Serial.println(response, HEX);

  // 3. Read back to confirm
  response = spiTransfer(CMD_NOP);
  Serial.print("   Confirm RX: 0x"); Serial.println(response, HEX);

  Serial.println("\nInitialization complete.\n");
}

void runDiagnostics() {
  uint16_t response1, response2;

  Serial.println("======================================");
  Serial.println("DIAGNOSTIC RESULTS");
  Serial.println("======================================\n");

  // Device ID
  Serial.println("--- Device ID ---");
  response1 = spiTransfer(CMD_ID);
  response2 = spiTransfer(CMD_NOP);
  Serial.print("Response: 0x"); Serial.print(response2, HEX);
  Serial.print(" ("); printBinary16(response2); Serial.println(")");

  // MC33810 Device ID format:
  // [15:12] = 1110 (echo of ID command)
  // [11:8]  = Device family code (should be 0x4 for MC33810)
  // [7:4]   = Silicon revision
  // [3:0]   = Metal mask revision
  uint8_t family = (response2 >> 8) & 0x0F;
  uint8_t silicon = (response2 >> 4) & 0x0F;
  uint8_t metal = response2 & 0x0F;
  Serial.print("  Family: 0x"); Serial.print(family, HEX);
  Serial.print("  Silicon Rev: 0x"); Serial.print(silicon, HEX);
  Serial.print("  Metal Rev: 0x"); Serial.println(metal, HEX);

  if (family != 0x4 && response2 != 0x20FF) {
    Serial.println("  WARNING: Unexpected device family!");
  }

  // LSD Status
  Serial.println("\n--- LSD (Low-Side Driver) Status ---");
  response1 = spiTransfer(CMD_LSD_STATUS);
  response2 = spiTransfer(CMD_NOP);
  Serial.print("Response: 0x"); Serial.print(response2, HEX);
  Serial.print(" ("); printBinary16(response2); Serial.println(")");
  decodeLSDStatus(response2);

  // GD Status
  Serial.println("\n--- GD (Gate Driver) Status ---");
  response1 = spiTransfer(CMD_GD_STATUS);
  response2 = spiTransfer(CMD_NOP);
  Serial.print("Response: 0x"); Serial.print(response2, HEX);
  Serial.print(" ("); printBinary16(response2); Serial.println(")");
  decodeGDStatus(response2);

  // All Status
  Serial.println("\n--- All Status ---");
  response1 = spiTransfer(CMD_ALL_STATUS);
  response2 = spiTransfer(CMD_NOP);
  Serial.print("Response: 0x"); Serial.print(response2, HEX);
  Serial.print(" ("); printBinary16(response2); Serial.println(")");
  decodeAllStatus(response2);

  Serial.println("\n======================================");
  Serial.println("COMMANDS:");
  Serial.println("  'r' - Rerun diagnostics");
  Serial.println("  't' - Toggle all outputs");
  Serial.println("  '1'-'4' - Toggle LSD output 1-4");
  Serial.println("  '5'-'8' - Toggle GD output 1-4");
  Serial.println("  's' - Read status");
  Serial.println("======================================");
}

void decodeLSDStatus(uint16_t status) {
  // LSD Status format (per MC33810 datasheet):
  // Bits [11:8]: Open Load fault when ON (OUT3-OUT0)
  // Bits [7:4]:  Short to battery fault (OUT3-OUT0)
  // Bits [3:0]:  Short to ground fault (OUT3-OUT0)

  uint8_t openLoad = (status >> 8) & 0x0F;
  uint8_t shortBat = (status >> 4) & 0x0F;
  uint8_t shortGnd = status & 0x0F;

  Serial.println("  Open Load faults (when ON):");
  for(int i=0; i<4; i++) {
    Serial.print("    OUT"); Serial.print(i);
    Serial.println((openLoad >> i) & 1 ? ": FAULT" : ": OK");
  }

  Serial.println("  Short to Battery faults:");
  for(int i=0; i<4; i++) {
    Serial.print("    OUT"); Serial.print(i);
    Serial.println((shortBat >> i) & 1 ? ": FAULT" : ": OK");
  }

  Serial.println("  Short to Ground faults:");
  for(int i=0; i<4; i++) {
    Serial.print("    OUT"); Serial.print(i);
    Serial.println((shortGnd >> i) & 1 ? ": FAULT" : ": OK");
  }
}

void decodeGDStatus(uint16_t status) {
  // GD Status format:
  // Bits [11:8]: MAXI flag (max current exceeded, GD3-GD0)
  // Bits [7:4]:  Open Load fault (GD3-GD0)
  // Bits [3:0]:  Low VDS fault (GD3-GD0)

  uint8_t maxi = (status >> 8) & 0x0F;
  uint8_t openLoad = (status >> 4) & 0x0F;
  uint8_t lowVDS = status & 0x0F;

  Serial.println("  MAXI (Max Current) flags:");
  for(int i=0; i<4; i++) {
    Serial.print("    GD"); Serial.print(i);
    Serial.println((maxi >> i) & 1 ? ": FAULT" : ": OK");
  }

  Serial.println("  Open Load faults:");
  for(int i=0; i<4; i++) {
    Serial.print("    GD"); Serial.print(i);
    Serial.println((openLoad >> i) & 1 ? ": FAULT" : ": OK");
  }

  Serial.println("  Low VDS faults:");
  for(int i=0; i<4; i++) {
    Serial.print("    GD"); Serial.print(i);
    Serial.println((lowVDS >> i) & 1 ? ": FAULT" : ": OK");
  }
}

void decodeAllStatus(uint16_t status) {
  // All Status format:
  // Bit 11: TSD (Thermal Shutdown)
  // Bit 10: VBatUV (Battery Undervoltage)
  // Bit 9:  VpwrOV (Power Overvoltage)
  // Bit 8:  VpwrUV (Power Undervoltage)
  // Bits 7:4: LSD summary fault (OUT3-OUT0)
  // Bits 3:0: GD summary fault (GD3-GD0)

  Serial.println("  Global faults:");
  Serial.print("    TSD (Thermal Shutdown): ");
  Serial.println((status >> 11) & 1 ? "FAULT!" : "OK");

  Serial.print("    VBat Undervoltage: ");
  Serial.println((status >> 10) & 1 ? "FAULT!" : "OK");

  Serial.print("    VPWR Overvoltage: ");
  Serial.println((status >> 9) & 1 ? "FAULT!" : "OK");

  Serial.print("    VPWR Undervoltage: ");
  Serial.println((status >> 8) & 1 ? "FAULT!" : "OK");

  Serial.println("  LSD summary (per output):");
  for(int i=0; i<4; i++) {
    Serial.print("    OUT"); Serial.print(i);
    Serial.println(((status >> 4) >> i) & 1 ? ": FAULT" : ": OK");
  }

  Serial.println("  GD summary (per output):");
  for(int i=0; i<4; i++) {
    Serial.print("    GD"); Serial.print(i);
    Serial.println((status >> i) & 1 ? ": FAULT" : ": OK");
  }
}

void testOutputs() {
  static bool allOn = false;
  uint16_t response;

  allOn = !allOn;

  // On/Off command format per MC33810 datasheet:
  // Bits [15:12] = 0011 (command)
  // Bits [11:8]  = GD3-GD0 (gate drivers)
  // Bits [7:4]   = OUT3-OUT0 (low-side drivers)
  // Bits [3:0]   = Reserved (must be 0)
  //
  // All ON  = 0x3FF0 = 0011 1111 1111 0000
  // All OFF = 0x3000 = 0011 0000 0000 0000
  uint16_t cmd = allOn ? 0x3FF0 : 0x3000;
  response = spiTransfer(cmd);

  Serial.print("Outputs ");
  Serial.print(allOn ? "ON" : "OFF");
  Serial.print(": TX=0x"); Serial.print(cmd, HEX);
  Serial.print(" RX=0x"); Serial.println(response, HEX);

  // Read back status
  delay(50);
  spiTransfer(CMD_ALL_STATUS);
  response = spiTransfer(CMD_NOP);
  Serial.print("Status: 0x"); Serial.println(response, HEX);
}

void loop() {
  static uint8_t lsdState = 0;
  static uint8_t gdState = 0;

  if (Serial.available()) {
    char c = Serial.read();
    uint16_t response;

    switch(c) {
      case 'r':
      case 'R':
        Serial.println("\n=== RERUNNING DIAGNOSTICS ===\n");
        initMC33810();
        runDiagnostics();
        break;

      case 't':
      case 'T':
        testOutputs();
        break;

      // Command format: 0x3[GD3-0][OUT3-0]0
      // GD in bits 11-8, OUT in bits 7-4, reserved bits 3-0 = 0
      case '1':
        lsdState ^= 0x01; // Toggle OUT0
        response = spiTransfer(CMD_ON_OFF | (gdState << 8) | (lsdState << 4));
        Serial.print("OUT0 "); Serial.print(lsdState & 0x01 ? "ON" : "OFF");
        Serial.print(" CMD=0x"); Serial.print(CMD_ON_OFF | (gdState << 8) | (lsdState << 4), HEX);
        Serial.print(" RX=0x"); Serial.println(response, HEX);
        break;
      case '2':
        lsdState ^= 0x02; // Toggle OUT1
        response = spiTransfer(CMD_ON_OFF | (gdState << 8) | (lsdState << 4));
        Serial.print("OUT1 "); Serial.print(lsdState & 0x02 ? "ON" : "OFF");
        Serial.print(" CMD=0x"); Serial.print(CMD_ON_OFF | (gdState << 8) | (lsdState << 4), HEX);
        Serial.print(" RX=0x"); Serial.println(response, HEX);
        break;
      case '3':
        lsdState ^= 0x04; // Toggle OUT2
        response = spiTransfer(CMD_ON_OFF | (gdState << 8) | (lsdState << 4));
        Serial.print("OUT2 "); Serial.print(lsdState & 0x04 ? "ON" : "OFF");
        Serial.print(" CMD=0x"); Serial.print(CMD_ON_OFF | (gdState << 8) | (lsdState << 4), HEX);
        Serial.print(" RX=0x"); Serial.println(response, HEX);
        break;
      case '4':
        lsdState ^= 0x08; // Toggle OUT3
        response = spiTransfer(CMD_ON_OFF | (gdState << 8) | (lsdState << 4));
        Serial.print("OUT3 "); Serial.print(lsdState & 0x08 ? "ON" : "OFF");
        Serial.print(" CMD=0x"); Serial.print(CMD_ON_OFF | (gdState << 8) | (lsdState << 4), HEX);
        Serial.print(" RX=0x"); Serial.println(response, HEX);
        break;
      case '5':
        gdState ^= 0x01; // Toggle GD0
        response = spiTransfer(CMD_ON_OFF | (gdState << 8) | (lsdState << 4));
        Serial.print("GD0 "); Serial.print(gdState & 0x01 ? "ON" : "OFF");
        Serial.print(" CMD=0x"); Serial.print(CMD_ON_OFF | (gdState << 8) | (lsdState << 4), HEX);
        Serial.print(" RX=0x"); Serial.println(response, HEX);
        break;
      case '6':
        gdState ^= 0x02; // Toggle GD1
        response = spiTransfer(CMD_ON_OFF | (gdState << 8) | (lsdState << 4));
        Serial.print("GD1 "); Serial.print(gdState & 0x02 ? "ON" : "OFF");
        Serial.print(" CMD=0x"); Serial.print(CMD_ON_OFF | (gdState << 8) | (lsdState << 4), HEX);
        Serial.print(" RX=0x"); Serial.println(response, HEX);
        break;
      case '7':
        gdState ^= 0x04; // Toggle GD2
        response = spiTransfer(CMD_ON_OFF | (gdState << 8) | (lsdState << 4));
        Serial.print("GD2 "); Serial.print(gdState & 0x04 ? "ON" : "OFF");
        Serial.print(" CMD=0x"); Serial.print(CMD_ON_OFF | (gdState << 8) | (lsdState << 4), HEX);
        Serial.print(" RX=0x"); Serial.println(response, HEX);
        break;
      case '8':
        gdState ^= 0x08; // Toggle GD3
        response = spiTransfer(CMD_ON_OFF | (gdState << 8) | (lsdState << 4));
        Serial.print("GD3 "); Serial.print(gdState & 0x08 ? "ON" : "OFF");
        Serial.print(" CMD=0x"); Serial.print(CMD_ON_OFF | (gdState << 8) | (lsdState << 4), HEX);
        Serial.print(" RX=0x"); Serial.println(response, HEX);
        break;

      case 's':
      case 'S':
        Serial.println("\n--- Current Status ---");
        spiTransfer(CMD_LSD_STATUS);
        response = spiTransfer(CMD_NOP);
        Serial.print("LSD Status: 0x"); Serial.println(response, HEX);
        decodeLSDStatus(response);

        spiTransfer(CMD_GD_STATUS);
        response = spiTransfer(CMD_NOP);
        Serial.print("GD Status: 0x"); Serial.println(response, HEX);
        decodeGDStatus(response);

        spiTransfer(CMD_ALL_STATUS);
        response = spiTransfer(CMD_NOP);
        Serial.print("All Status: 0x"); Serial.println(response, HEX);
        decodeAllStatus(response);
        break;
    }
  }
}
