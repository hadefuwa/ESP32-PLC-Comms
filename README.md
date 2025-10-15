## ESP32 ↔ Siemens S7 PLC over S7 Protocol (Settimino)

Beginner‑friendly guide to connect an ESP32 to a Siemens S7‑1200/1500 PLC using the native S7 protocol (no Modbus) via the Settimino library.

### What you get
- Wi‑Fi connection from ESP32 to PLC
- Reliable S7 connection using Settimino
- Easy tag mapping: read/write DB addresses like `DB1.DBX2.0`, `DB1.DBW80`, `DB1.DBD32`
- Simple Serial commands to read and write tags

---

### 1) Requirements

#### Hardware
- ESP32 dev board (e.g., DevKit V1, WROOM‑32)
- Siemens S7‑1200 or S7‑1500 PLC
- Same IP subnet for both devices (e.g., 192.168.0.x)

#### Software
- Arduino IDE (latest)
- ESP32 board support
  - Arduino IDE → File → Preferences → Additional Boards URLs:
    ```
    https://espressif.github.io/arduino-esp32/package_esp32_index.json
    ```
  - Tools → Board → Boards Manager → install “ESP32 by Espressif Systems”
- Settimino library (Arduino port of Snap7)

---

### 2) Install Settimino

Download from the official SourceForge repository:

`https://sourceforge.net/projects/settimino/`

Steps:
1. Download ZIP
2. Arduino IDE → Sketch → Include Library → Add .ZIP Library… → choose the ZIP

**Note:** You must configure `Platform.h` as shown below.

---

### 3) Configure Platform.h (only if needed)

Open your library file:

`Documents/Arduino/libraries/Settimino/Platform.h`

Ensure the ESP32 Wi‑Fi define is enabled and others are disabled:

```cpp
//#define ARDUINO_LAN
//#define ESP8266_FAMILY
#define ESP32_WIFI
//#define M5STACK_WIFI
//#define M5STACK_LAN
```

If present, comment out any `#include <M5Stack.h>` lines.

---

### 4) Keep Wi‑Fi credentials private

Create a `secrets.h` next to your `.ino`:

```cpp
#pragma once
#define WIFI_SSID "YourWiFiName"
#define WIFI_PASS "YourPassword"
```

Add to `.gitignore` if using Git:

```
secrets.h
```

---

### 5) Project layout

```
ESP32-Snap7-Comms/
├─ main/                // Main Arduino sketch folder
│  └─ main.ino          // Fully commented sketch with tag mapping
└─ secrets.h            // Your Wi‑Fi credentials (not committed)
```

Use `main/main.ino` as the primary sketch. It’s heavily commented and beginner‑friendly.

---

### 6) Configure PLC and network

In TIA Portal (S7‑1200/1500):
1. Device configuration → Protection & Security → enable:
   - “Permit access with PUT/GET communication from remote partner (PLC, HMI, OPC, …)”
2. Set CPU IP (e.g., `192.168.0.99`)
3. Create `DB1` with your variables. If using absolute byte/bit/word addressing, set the DB to Non‑optimized access.

---

### 7) Configure the sketch

Open `main/main.ino` and edit:
- Wi‑Fi: `WIFI_SSID`, `WIFI_PASS` (or `secrets.h`)
- PLC: `plcIP(192,168,0,99)`, `rack = 0`, `slot = 1` (typical for S7‑1200)
- Tag map: edit the `TAG_MAP` array to match your DB addresses

Example tag rows:

```cpp
// name,            address,       unit,   scale, description
{"EStop_State",     "DB1.DBX2.0", "ON/OFF", 1.0, "Emergency Stop Button"},
{"Stats_ForcedCount","DB1.DBW80", "",       1.0, "Number of Forced Tags"},
{"AI0_Scaled",       "DB1.DBD32", "",       1.0, "Analog Input 0"},
```

Supported address forms:
- Bits:  `DBn.DBXbyte.bit` (e.g., `DB1.DBX2.0`)
- Words: `DBn.DBWbyte` (2 bytes, signed INT)
- Reals: `DBn.DBDbyte` (4 bytes, IEEE‑754 float)

`scaleFactor` multiplies what you see when printing numeric values (leave `1.0` if not needed).

---

### 8) Using the Serial command interface

Open Serial Monitor at 115200 baud. Type commands and press Enter:
- `help`          → shows all commands
- `read`          → reads all tags and prints a clean table
- `tags`          → lists the configured map (names, addresses, units)
- `write <tag> <v>` → writes value to a tag
  - For bits, use `0` or `1` (example: `write Green_Status_LED 1`)
  - For words/reals, pass a number (scaling is applied automatically)
- `status`        → Wi‑Fi and PLC status
- `reconnect`     → force PLC reconnect
- `reboot`        → restart ESP32

The sketch auto‑reconnects if the PLC link drops.

---

### 9) Minimal code example

If you prefer a tiny example, `main/main.ino` shows a compact connect + DB read loop:

```cpp
#include <WiFi.h>
#include "Settimino.h"

#define WIFI_SSID "..."
#define WIFI_PASS "..."

IPAddress plcIP(192,168,0,99);
int rack = 0, slot = 1;
S7Client plc;
uint8_t buf[16];

void setup(){
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while(WiFi.status()!=WL_CONNECTED){ delay(500); }
  plc.SetConnectionType(S7_Basic);
  plc.ConnectTo(plcIP, rack, slot);
}

void loop(){
  if(!plc.Connected){ plc.ConnectTo(plcIP, rack, slot); delay(1000); return; }
  if(plc.ReadArea(S7AreaDB, 1, 0, sizeof(buf), buf)==0){
    for(byte b:buf){ if(b<16)Serial.print("0"); Serial.print(b,HEX); Serial.print(" "); }
    Serial.println();
  }
  delay(1000);
}
```

---

### 10) Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Can ping PLC but cannot connect (error 2) | PUT/GET disabled; Wi‑Fi isolation; wrong slot | Enable PUT/GET; ensure not on guest Wi‑Fi; use rack=0, slot=1 for S7‑1200 |
| `M5Stack.h: No such file` | Wrong platform selected | Set `#define ESP32_WIFI` in `Platform.h` and remove M5 includes |
| `rom/miniz.h: No such file` | Old M5Stack dependency | Comment out `#include <M5Stack.h>` |
| IntelliSense shows include errors | IDE pathing only | Build/flash anyway; install ESP32 core and Settimino |
| Reads garbage values | Optimized DB layout | Set DB to Non‑optimized if using absolute addressing |

---

### 11) License

Settimino is LGPL; this project is intended for educational/industrial use. Include and comply with the Settimino license when distributing.

---

### 12) Acknowledgements

- Davide Nardella for Settimino / Snap7
- Espressif for the ESP32 Arduino core
- Community examples and guides that inspired this setup


