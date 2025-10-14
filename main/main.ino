/*
 * ESP32 - Siemens S7-1200 Communication System
 * Author: Hamed Adefuwa
 *
 * WHAT THIS DOES (Beginner-friendly):
 * - Connects ESP32 to Wi‑Fi
 * - Connects to a Siemens S7 PLC (tested with S7‑1200) using Settimino
 * - Reads a group of PLC tags from DB1 and prints them nicely over Serial
 * - Lets you write to PLC tags from the Serial Monitor (simple commands)
 * - Automatically reconnects if Wi‑Fi/PLC connection drops
 *
 * HOW TO ADAPT FOR YOUR PROJECT (2 steps):
 * 1) Change Wi‑Fi and PLC details in the CONFIGURATION section.
 * 2) Edit the TAG_MAP table to match your DB number, addresses and tag names.
 *    - Use addresses like:  DB1.DBX2.0 (bit), DB1.DBW80 (word), DB1.DBD32 (real)
 *    - Description and unit are only for display. scaleFactor multiplies what
 *      you SEE for numeric types (leave 1.0 if you do not need scaling).
 *
 * TIP: Start by getting READS working. Only then try WRITES for safe tags.
 *
 * Hardware: ESP32 DevKit + Siemens S7-1214C
 * Library: Settimino (install via Library Manager)
 */

 #include <WiFi.h>
 #include "Settimino.h"
 
 // ============================================================================
 // CONFIGURATION
 // ============================================================================
 
// Wi‑Fi credentials (change these to your network)
 #define WIFI_SSID "VM0091128"
 #define WIFI_PASS "p3gRmSw5xcwb"
 
// PLC connection (change to your PLC IP/rack/slot)
 IPAddress plcIP(192, 168, 0, 99);
 int rack = 0;
 int slot = 1;
 
 // Timing
 #define READ_INTERVAL 1000      // Read PLC every 1000ms
 #define RECONNECT_DELAY 5000    // Wait 5s between reconnect attempts
 #define MAX_RECONNECTS 10       // Reboot ESP32 after 10 failed reconnects
 
 // ============================================================================
 // EASY TAG CONFIGURATION
 // ============================================================================
 /*
  * HOW TO USE:
  * 1. In TIA Portal, open your DB1 and note down the addresses
  * 2. Fill in the table below with your tag names and addresses
  * 3. Upload to ESP32 - done!
  * 
  * ADDRESS FORMAT GUIDE:
  * - BOOL:  "DBX6.0"  means byte 6, bit 0
  * - INT:   "DBW0"    means word starting at byte 0 (2 bytes)
  * - REAL:  "DBD2"    means double-word starting at byte 2 (4 bytes)
  * 
  * EXAMPLE DB1 LAYOUT IN TIA PORTAL:
  * ┌─────────────────────────────────┐
  * │ DB1 (Data Block)                │
  * ├──────────┬──────────┬───────────┤
  * │ Name     │ Type     │ Address   │
  * ├──────────┼──────────┼───────────┤
  * │ TempSP   │ INT      │ DBW0      │  ← starts at byte 0
  * │ FlowRate │ REAL     │ DBD2      │  ← starts at byte 2
  * │ Pump     │ BOOL     │ DBX6.0    │  ← byte 6, bit 0
  * │ Valve    │ BOOL     │ DBX6.1    │  ← byte 6, bit 1
  * │ Level    │ INT      │ DBW8      │  ← starts at byte 8
  * └──────────┴──────────┴───────────┘
  */
 
// Define your tags here - add/remove/modify as needed
// Each row describes ONE tag you want to read (and optionally write).
// name: a simple key to reference the tag (used in Serial commands)
// address: DBn.DBX/DBW/DBD format. Examples:
//   - Bit : DB1.DBX2.0  (byte 2, bit 0)
//   - Word: DB1.DBW80   (2 bytes, signed INT)
//   - Real: DB1.DBD32   (4 bytes, IEEE‑754 float)
// unit: text shown after numeric values (e.g. "°C", "%", "ON/OFF")
// scaleFactor: numeric display multiplier (1.0 means "no scaling")
// description: human‑friendly label shown in the table
struct PLCTag {
  const char* name;        // Tag key (e.g., emergency_stop)
  const char* address;     // PLC address (e.g., "DB1.DBX0.0", "DB1.DBW2")
  const char* unit;        // Unit (e.g., "ON/OFF", "L/min")
  float scaleFactor;       // Multiply raw numeric value by this (use 1.0 if none)
  const char* description; // Human-readable description
};
 
 // ┌────────────────────────────────────────────────────────────┐
 // │  EDIT THIS TABLE TO MATCH YOUR PLC DB1                     │
 // └────────────────────────────────────────────────────────────┘
PLCTag TAG_MAP[] = {
  // Name                      Address         Unit     Scale  Description
  {"EStop_State",              "DB1.DBX2.0",  "ON/OFF", 1.0,  "Emergency Stop Button"},
  {"GreenButton_State",        "DB1.DBX4.0",  "ON/OFF", 1.0,  "Green Start Button"},
  {"RedButton_State",          "DB1.DBX6.0",  "ON/OFF", 1.0,  "Red Stop Button"},
  {"LeftSwitch_State",         "DB1.DBX8.0",  "ON/OFF", 1.0,  "Left Selector Switch"},
  {"RightSwitch_State",        "DB1.DBX10.0", "ON/OFF", 1.0,  "Right Selector Switch"},
  {"SpareInput_A5_State",      "DB1.DBX12.0", "ON/OFF", 1.0,  "Spare DI A5"},
  {"SpareInput_A6_State",      "DB1.DBX14.0", "ON/OFF", 1.0,  "Spare DI A6"},
  {"SpareInput_A7_State",      "DB1.DBX16.0", "ON/OFF", 1.0,  "Spare DI A7"},
  {"Input_B0_State",           "DB1.DBX18.0", "ON/OFF", 1.0,  "DI B0"},
  {"Input_B1_State",           "DB1.DBX20.0", "ON/OFF", 1.0,  "DI B1"},
  {"Input_B2_State",           "DB1.DBX22.0", "ON/OFF", 1.0,  "DI B2"},
  {"Input_B3_State",           "DB1.DBX24.0", "ON/OFF", 1.0,  "DI B3"},
  {"Input_B4_State",           "DB1.DBX26.0", "ON/OFF", 1.0,  "DI B4"},
  {"AI0_Scaled",               "DB1.DBD32",   "",       1.0,  "Analog Input 0"},
  {"AI1_Scaled",               "DB1.DBD44",   "",       1.0,  "Analog Input 1"},
  {"Green_Status_LED",         "DB1.DBX58.0", "ON/OFF", 1.0,  "Green Status LED"},
  {"Yellow_Status_LED",        "DB1.DBX60.0", "ON/OFF", 1.0,  "Yellow Status LED"},
  {"Red_Status_LED",           "DB1.DBX62.0", "ON/OFF", 1.0,  "Red Status LED"},
  {"SpareOutput_A3_State",     "DB1.DBX64.0", "ON/OFF", 1.0,  "Spare DQ A3"},
  {"SpareOutput_A4_State",     "DB1.DBX66.0", "ON/OFF", 1.0,  "Spare DQ A4"},
  {"SpareOutput_A5_State",     "DB1.DBX68.0", "ON/OFF", 1.0,  "Spare DQ A5"},
  {"SpareOutput_A6_State",     "DB1.DBX70.0", "ON/OFF", 1.0,  "Spare DQ A6"},
  {"SpareOutput_A7_State",     "DB1.DBX72.0", "ON/OFF", 1.0,  "Spare DQ A7"},
  {"Output_B0_State",          "DB1.DBX74.0", "ON/OFF", 1.0,  "DQ B0"},
  {"Output_B1_State",          "DB1.DBX76.0", "ON/OFF", 1.0,  "DQ K1 Relay"},
  {"HMI_ClearForcing",         "DB1.DBX0.0",  "ON/OFF", 1.0,  "HMI Clear Forcing Button"},
  {"HMI_FaultReset",           "DB1.DBX0.1",  "ON/OFF", 1.0,  "HMI Fault Reset Button"},
  {"Stats_ForcingActive",      "DB1.DBX78.0", "ON/OFF", 1.0,  "System Forcing Active"},
  {"Stats_ForcedCount",        "DB1.DBW80",   "",       1.0,  "Number of Forced Tags"},
};
 
// How many tags are in the TAG_MAP (used everywhere below)
const int NUM_TAGS = sizeof(TAG_MAP) / sizeof(TAG_MAP[0]);
 
 // ============================================================================
 // INTERNAL VARIABLES (don't edit below unless you know what you're doing)
 // ============================================================================
 
 S7Client plc;
 uint8_t dbBuffer[256];
 float tagValues[50];  // Store current values (increase if you have >50 tags)
 
 unsigned long lastReadTime = 0;
 unsigned long lastReconnectAttempt = 0;
 int reconnectCount = 0;
 
// ============================================================================
// ADDRESS PARSER (accepts DBn.DBX/DBW/DBD and legacy DBX/DBW/DBD)
// ----------------------------------------------------------------------------
// Converts a text address (e.g. "DB1.DBX2.0" or "DBW8") to numeric fields:
// - dbNumber: the DB number (defaults to 1 if not present)
// - type:     'X' (bit), 'W' (word/INT), 'D' (dword/REAL)
// - byteOffset and bitOffset: where to read inside the DB
// ============================================================================
 
struct ParsedAddress {
  int dbNumber;   // e.g., 1 for DB1
  char type;      // 'W'=Word, 'D'=DWord, 'X'=Bit
  int byteOffset;
  int bitOffset;  // Only used for type 'X'
};
 
ParsedAddress parseAddress(const char* addr) {
  ParsedAddress result = {1, 'E', 0, 0};  // default DB1, 'E' = error

  // Accept both "DBW0"/"DBX6.0" and "DB1.DBW2"/"DB1.DBX0.0"
  if (strncmp(addr, "DB", 2) != 0) return result;

  const char* p = addr;
  // If starts with DB{n}.
  if (p[2] >= '0' && p[2] <= '9') {
    // parse number after DB
    int i = 2;
    int dbNum = 0;
    while (p[i] >= '0' && p[i] <= '9') { dbNum = dbNum * 10 + (p[i]-'0'); i++; }
    result.dbNumber = dbNum > 0 ? dbNum : 1;
    if (p[i] == '.' && p[i+1] == 'D' && p[i+2] == 'B') {
      p = &p[i+1]; // point to "DB..."
    } else {
      // Unexpected format like DB1X..., fallback
      p = &p[i];
    }
  }

  // Now p should start with "DBX", "DBW", or "DBD"
  if (strncmp(p, "DB", 2) != 0) return result;
  result.type = p[2]; // 'X','W','D'

  if (result.type == 'X') {
    // Format: DBX0.0
    int dotPos = -1;
    for (int i = 3; i < (int)strlen(p); i++) {
      if (p[i] == '.') { dotPos = i; break; }
    }
    if (dotPos == -1) return result;
    result.byteOffset = atoi(&p[3]);
    result.bitOffset = atoi(&p[dotPos + 1]);
  } else {
    // Format: DBW2 or DBD4
    result.byteOffset = atoi(&p[3]);
    result.bitOffset = 0;
  }

  return result;
}
 
 // ============================================================================
 // DATA EXTRACTION HELPERS
 // ============================================================================
 
// Read a 16‑bit signed integer (big‑endian) from a byte buffer
int16_t getInt(uint8_t* buf, int offset) {
   return (buf[offset] << 8) | buf[offset + 1];
 }
 
// Read a 32‑bit IEEE‑754 float (big‑endian) from a byte buffer
float getReal(uint8_t* buf, int offset) {
   uint32_t temp = ((uint32_t)buf[offset] << 24) |
                   ((uint32_t)buf[offset + 1] << 16) |
                   ((uint32_t)buf[offset + 2] << 8) |
                   (uint32_t)buf[offset + 3];
   float result;
   memcpy(&result, &temp, 4);
   return result;
 }
 
// Read a single bit from a given byte/bit position
bool getBool(uint8_t* buf, int byteOffset, int bitOffset) {
   return (buf[byteOffset] >> bitOffset) & 0x01;
 }
 
// Write a 16‑bit signed integer (big‑endian) into a byte buffer
void setInt(uint8_t* buf, int offset, int16_t value) {
   buf[offset] = (value >> 8) & 0xFF;
   buf[offset + 1] = value & 0xFF;
 }
 
// Write a 32‑bit IEEE‑754 float (big‑endian) into a byte buffer
void setReal(uint8_t* buf, int offset, float value) {
   uint32_t temp;
   memcpy(&temp, &value, 4);
   buf[offset] = (temp >> 24) & 0xFF;
   buf[offset + 1] = (temp >> 16) & 0xFF;
   buf[offset + 2] = (temp >> 8) & 0xFF;
   buf[offset + 3] = temp & 0xFF;
 }
 
// Set/clear a single bit inside a byte buffer
void setBool(uint8_t* buf, int byteOffset, int bitOffset, bool value) {
   if (value) {
     buf[byteOffset] |= (1 << bitOffset);
   } else {
     buf[byteOffset] &= ~(1 << bitOffset);
   }
 }
 
 // ============================================================================
 // PLC CONNECTION
 // ============================================================================
 
// Quick network check: are we allowed to open TCP port 102 to the PLC?
// If this fails, the Wi‑Fi/AP/router is blocking the S7 port.
bool probePort102() {
   WiFiClient client;
   Serial.print("Probing TCP port 102...");
   if (client.connect(plcIP, 102)) {
     Serial.println(" OK");
     client.stop();
     return true;
   } else {
     Serial.println(" FAILED");
     return false;
   }
 }
 
// Connect to the PLC. We try slot 1, 2, then 0 using Settimino's ConnectTo.
// If all fail, we try the legacy Connect method too.
bool connectPLC() {
   int slotsToTry[] = {1, 2, 0};
   const int numSlots = sizeof(slotsToTry) / sizeof(slotsToTry[0]);
   char errText[128];
   
   if (!probePort102()) {
     Serial.println("Port 102 not accessible. Check network/firewall.");
     return false;
   }
   
   plc.SetConnectionType(S7_Basic);
   
   for (int i = 0; i < numSlots; i++) {
     slot = slotsToTry[i];
     Serial.printf("Connecting PLC (rack=%d, slot=%d)...", rack, slot);
     
     int r = plc.ConnectTo(plcIP, rack, slot);
     
     if (r == 0) {
       Serial.println(" SUCCESS");
       reconnectCount = 0;
       return true;
     } else {
       plc.ErrorText(r, errText, sizeof(errText));
       Serial.printf(" FAILED (code %d: %s)\n", r, errText);
     }
     delay(250);
   }
   
   for (int i = 0; i < numSlots; i++) {
     slot = slotsToTry[i];
     plc.SetConnectionParams(plcIP, rack, slot);
     Serial.printf("Connecting PLC (legacy, rack=%d, slot=%d)...", rack, slot);
     
     int r = plc.Connect();
     
     if (r == 0) {
       Serial.println(" SUCCESS");
       reconnectCount = 0;
       return true;
     } else {
       plc.ErrorText(r, errText, sizeof(errText));
       Serial.printf(" FAILED (code %d: %s)\n", r, errText);
     }
     delay(250);
   }
   
   Serial.println("All connection attempts failed.");
   return false;
 }
 
// Called from loop() whenever the PLC is disconnected.
// Tries to reconnect every RECONNECT_DELAY milliseconds.
void handleReconnection() {
   if (millis() - lastReconnectAttempt < RECONNECT_DELAY) {
     return;
   }
   
   lastReconnectAttempt = millis();
   reconnectCount++;
   
   Serial.printf("\n=== Connection Lost (attempt %d/%d) ===\n", 
                 reconnectCount, MAX_RECONNECTS);
   
   if (reconnectCount >= MAX_RECONNECTS) {
     Serial.println("Max reconnect attempts reached. Rebooting ESP32...");
     delay(1000);
     ESP.restart();
   }
   
   if (connectPLC()) {
     Serial.println("Reconnection successful!");
   }
 }
 
 // ============================================================================
 // AUTOMATIC READ/WRITE (uses TAG_MAP)
 // ============================================================================
 
// Read the minimum continuous span of bytes that covers ALL the tags in TAG_MAP
// (we assume all tags live in the same DB). Then decode each tag into
// tagValues[] using its type and scaleFactor.
bool readAllTags() {
  if (NUM_TAGS <= 0) return true;

  // Assume all tags are in the same DB; verify and compute required span
  ParsedAddress first = parseAddress(TAG_MAP[0].address);
  int dbNumber = first.dbNumber > 0 ? first.dbNumber : 1;

  int maxOffset = 0;
  for (int i = 0; i < NUM_TAGS; i++) {
    ParsedAddress addr = parseAddress(TAG_MAP[i].address);
    if (addr.dbNumber != dbNumber) {
      Serial.println("Warning: Multiple DBs detected. This simple reader expects all tags in one DB.");
      // Continue but still read only the first DB
    }
    int endOffset = addr.byteOffset;
    if (addr.type == 'W') endOffset += 2;
    if (addr.type == 'D') endOffset += 4;
    if (addr.type == 'X') endOffset += 1;
    if (endOffset > maxOffset) maxOffset = endOffset;
  }

  // Fetch bytes [0..maxOffset) from the PLC only once
  int r = plc.ReadArea(S7AreaDB, dbNumber, 0, maxOffset, dbBuffer);
  if (r != 0) {
    char errText[128];
    plc.ErrorText(r, errText, sizeof(errText));
    Serial.printf("Read error (code %d: %s)\n", r, errText);
    return false;
  }

  // Parse values and apply scaling for numeric types
  for (int i = 0; i < NUM_TAGS; i++) {
    ParsedAddress addr = parseAddress(TAG_MAP[i].address);
    if (addr.type == 'W') {
      float raw = (float)getInt(dbBuffer, addr.byteOffset);
      float scale = TAG_MAP[i].scaleFactor > 0 ? TAG_MAP[i].scaleFactor : 1.0f;
      tagValues[i] = raw * scale;
    } else if (addr.type == 'D') {
      float rawReal = getReal(dbBuffer, addr.byteOffset);
      float scale = TAG_MAP[i].scaleFactor > 0 ? TAG_MAP[i].scaleFactor : 1.0f;
      tagValues[i] = rawReal * scale;
    } else if (addr.type == 'X') {
      tagValues[i] = getBool(dbBuffer, addr.byteOffset, addr.bitOffset) ? 1.0f : 0.0f;
    }
  }

  return true;
}
 
// Write ONE tag by name from the Serial command interface.
// - For bits we read the owner byte, change the bit, and write the byte back.
// - For words we write a 16‑bit value (value/scale)
// - For reals we write a 32‑bit float (value/scale)
bool writeTag(const char* tagName, float value) {
   // Find tag in map
   int tagIndex = -1;
   for (int i = 0; i < NUM_TAGS; i++) {
     if (strcasecmp(TAG_MAP[i].name, tagName) == 0) {
       tagIndex = i;
       break;
     }
   }
   
   if (tagIndex == -1) {
     Serial.printf("Tag '%s' not found in TAG_MAP\n", tagName);
     return false;
   }
   
  ParsedAddress addr = parseAddress(TAG_MAP[tagIndex].address);
  int r = 0;

  int dbNumber = addr.dbNumber > 0 ? addr.dbNumber : 1;
  float scale = TAG_MAP[tagIndex].scaleFactor > 0 ? TAG_MAP[tagIndex].scaleFactor : 1.0f;

  if (addr.type == 'W') {
    // Convert engineering value to raw by dividing scale
    int16_t raw = (int16_t)(value / scale);
    uint8_t buf[2];
    setInt(buf, 0, raw);
    r = plc.WriteArea(S7AreaDB, dbNumber, addr.byteOffset, 2, buf);
  } else if (addr.type == 'D') {
    // Assume REAL in PLC; invert scale if provided
    float raw = value / scale;
    uint8_t buf[4];
    setReal(buf, 0, raw);
    r = plc.WriteArea(S7AreaDB, dbNumber, addr.byteOffset, 4, buf);
  } else if (addr.type == 'X') {
    uint8_t byteBuf;
    r = plc.ReadArea(S7AreaDB, dbNumber, addr.byteOffset, 1, &byteBuf);
    if (r == 0) {
      setBool(&byteBuf, 0, addr.bitOffset, value != 0.0f);
      r = plc.WriteArea(S7AreaDB, dbNumber, addr.byteOffset, 1, &byteBuf);
    }
  }
   
   if (r != 0) {
     Serial.printf("Write failed (error %d)\n", r);
     return false;
   }
   
   return true;
 }
 
 // ============================================================================
 // DISPLAY
 // ============================================================================
 
// Pretty print all tag values in aligned columns (no borders)
void printAllTags() {
  Serial.println("\nPLC TAG VALUES (DB1)");
  Serial.println("Name               Description                 Value");
  Serial.println("------------------------------------------------------");
  
   for (int i = 0; i < NUM_TAGS; i++) {
     ParsedAddress addr = parseAddress(TAG_MAP[i].address);
     
     char line[80];
     if (addr.type == 'X') {
       // Boolean display
     snprintf(line, sizeof(line), "%-18s %-26s %6s",
               TAG_MAP[i].name,
               TAG_MAP[i].description,
               tagValues[i] > 0.5 ? "TRUE" : "FALSE");
     } else {
       // Numeric display
       char valueStr[20];
       if (strlen(TAG_MAP[i].unit) > 0) {
         snprintf(valueStr, sizeof(valueStr), "%.2f %s", tagValues[i], TAG_MAP[i].unit);
       } else {
         snprintf(valueStr, sizeof(valueStr), "%.2f", tagValues[i]);
       }
     snprintf(line, sizeof(line), "%-18s %-26s %15s",
               TAG_MAP[i].name,
               TAG_MAP[i].description,
               valueStr);
     }
     Serial.println(line);
   }
  Serial.println("");
 }
 
 void printRawHex(int numBytes) {
   Serial.print("Raw DB1 (hex): ");
   for (int i = 0; i < numBytes; i++) {
     if (dbBuffer[i] < 16) Serial.print("0");
     Serial.print(dbBuffer[i], HEX);
     Serial.print(" ");
   }
   Serial.println();
 }
 
 // ============================================================================
 // SERIAL COMMAND INTERFACE
 // ============================================================================
 
// Simple Serial command interface (type in Serial Monitor + enter):
//   help         -> show available commands
//   read         -> read and print all tags
//   raw          -> dump the first N bytes from DB1 as hex
//   tags         -> list configured TAG_MAP entries
//   write X V    -> write value V to tag named X
//   status       -> show Wi‑Fi and PLC status
//   reconnect    -> force a PLC reconnect now
//   reboot       -> restart the ESP32
void processSerialCommand() {
   if (!Serial.available()) return;
   
   String cmd = Serial.readStringUntil('\n');
   cmd.trim();
   
   String cmdLower = cmd;
   cmdLower.toLowerCase();
   
   if (cmdLower == "help" || cmdLower == "?") {
     Serial.println("\n╔════════════════════════════════════════════════╗");
     Serial.println("║                  COMMANDS                      ║");
     Serial.println("╠════════════════════════════════════════════════╣");
     Serial.println("║ read              - Read all PLC tags          ║");
     Serial.println("║ raw               - Show raw hex dump          ║");
     Serial.println("║ tags              - List all configured tags   ║");
     Serial.println("║ write <tag> <val> - Write to tag               ║");
     Serial.println("║ status            - System status              ║");
     Serial.println("║ reconnect         - Force reconnection         ║");
     Serial.println("║ reboot            - Restart ESP32              ║");
     Serial.println("╠════════════════════════════════════════════════╣");
     Serial.println("║ Examples:                                      ║");
     Serial.println("║   write TempSetpoint 25                        ║");
     Serial.println("║   write PumpRunning 1    (turns pump on)       ║");
     Serial.println("║   write ValveOpen 0      (closes valve)        ║");
     Serial.println("╚════════════════════════════════════════════════╝\n");
   }
   else if (cmdLower == "read") {
     if (readAllTags()) {
       printAllTags();
     }
   }
   else if (cmdLower == "raw") {
     if (readAllTags()) {
       printRawHex(50);
     }
   }
   else if (cmdLower == "tags") {
     Serial.println("\n╔════════════════════════════════════════════════╗");
     Serial.println("║          CONFIGURED TAGS (TAG_MAP)             ║");
     Serial.println("╠════════════════════════════════════════════════╣");
     for (int i = 0; i < NUM_TAGS; i++) {
       char line[80];
       snprintf(line, sizeof(line), "║ [%2d] %-20s %-10s %-8s ║",
                i, TAG_MAP[i].name, TAG_MAP[i].address, TAG_MAP[i].unit);
       Serial.println(line);
     }
     Serial.println("╚════════════════════════════════════════════════╝\n");
   }
   else if (cmdLower.startsWith("write ")) {
     int spaceIdx = cmd.indexOf(' ', 6);
     if (spaceIdx > 0) {
       String tagName = cmd.substring(6, spaceIdx);
       float value = cmd.substring(spaceIdx + 1).toFloat();
       
       if (writeTag(tagName.c_str(), value)) {
         Serial.printf("✓ Wrote %.2f to '%s'\n", value, tagName.c_str());
       }
     } else {
       Serial.println("Usage: write <tagname> <value>");
     }
   }
   else if (cmdLower == "status") {
     Serial.println("\n╔════════════════════════════════════════════════╗");
     Serial.println("║              SYSTEM STATUS                     ║");
     Serial.println("╠════════════════════════════════════════════════╣");
     Serial.printf("║ Wi-Fi:      %-35s ║\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
     Serial.printf("║ IP:         %-35s ║\n", WiFi.localIP().toString().c_str());
     Serial.printf("║ PLC:        %-35s ║\n", plc.Connected ? "Connected" : "Disconnected");
     Serial.printf("║ PLC IP:     %-35s ║\n", plcIP.toString().c_str());
     Serial.printf("║ Reconnects: %d/%-32d ║\n", reconnectCount, MAX_RECONNECTS);
     Serial.printf("║ Uptime:     %-35lu ║\n", millis() / 1000);
     Serial.println("╚════════════════════════════════════════════════╝\n");
   }
   else if (cmdLower == "reconnect") {
     Serial.println("Forcing reconnection...");
     plc.Disconnect();
     connectPLC();
   }
   else if (cmdLower == "reboot") {
     Serial.println("Rebooting ESP32...");
     delay(1000);
     ESP.restart();
   }
   else {
     Serial.println("Unknown command. Type 'help' for available commands.");
   }
 }
 
 // ============================================================================
 // SETUP
 // ============================================================================
 
 void setup() {
   Serial.begin(115200);
   delay(100);
   
   Serial.println("\n\n");
   Serial.println("╔════════════════════════════════════════════════╗");
   Serial.println("║   ESP32 - Siemens S7 Communication System      ║");
   Serial.println("║               Hamed Adefuwa                    ║");
   Serial.println("╚════════════════════════════════════════════════╝\n");
   
   // Connect to Wi-Fi
   WiFi.begin(WIFI_SSID, WIFI_PASS);
   Serial.print("Connecting to Wi-Fi");
   
   while (WiFi.status() != WL_CONNECTED) {
     delay(500);
     Serial.print(".");
   }
   
   Serial.println("\n\n╔════════════════════════════════════════════════╗");
   Serial.println("║            Wi-Fi Connected                     ║");
   Serial.println("╠════════════════════════════════════════════════╣");
   Serial.printf("║ SSID:    %-38s║\n", WIFI_SSID);
   Serial.printf("║ IP:      %-38s║\n", WiFi.localIP().toString().c_str());
   Serial.printf("║ Gateway: %-38s║\n", WiFi.gatewayIP().toString().c_str());
   Serial.printf("║ Subnet:  %-38s║\n", WiFi.subnetMask().toString().c_str());
   Serial.println("╚════════════════════════════════════════════════╝\n");
   
   // Connect to PLC
   connectPLC();
   
   Serial.println("\nType 'help' for available commands.\n");
 }
 
 // ============================================================================
 // MAIN LOOP
 // ============================================================================
 
 void loop() {
   if (!plc.Connected) {
     handleReconnection();
     return;
   }
   
   if (millis() - lastReadTime >= READ_INTERVAL) {
     lastReadTime = millis();
     
     if (readAllTags()) {
       printAllTags();
     }
   }
   
   processSerialCommand();
   delay(10);
 }