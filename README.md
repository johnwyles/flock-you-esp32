# flock-you-esp32

ESP32 firmware for M5Stack Basic Development Kit with hardware detection for GPS, LoRa, CC1101 sub-GHz, BLE, and WiFi promiscuous scanning. Built for detecting Flock Safety cameras and other wireless signals.

## Hardware

**Target:** M5Stack Basic (ESP32-PICO-D4)

**Optional modules (auto-detected at boot):**
| Module | Interface | Port | Pins |
|--------|-----------|------|------|
| GPS Unit v1.1 (AT6668) | I2C | Port A | SDA=21, SCL=22, addr 0x10 |
| LoRa Module 433MHz (SX1278/RA-02) | SPI | Port B | CS=5, RST=26, DIO0=2, SCK=18, MISO=19, MOSI=23 |
| CC1101 Module (315/433/868/915 MHz) | SPI | Port B | CS=4, GDO0=0, SCK=18, MISO=19, MOSI=23 |

**Module conflict:** LoRa and CC1101 share SPI bus (SCK/MISO/MOSI) with different CS pins (5 vs 4). Both can be present simultaneously.

## Features

### WiFi 2.4 GHz Promiscuous Scanning
**What it looks for:**
- All 802.11 frames in monitor mode: **probe requests**, **beacon frames**, **deauth frames**
- Specifically hunting for **Flock Safety camera** signatures:
  - SSIDs starting with `Flock Camera net.`
  - SSIDs matching `Flock-XXXXXX`
  - MAC OUI prefixes known to belong to Flock hardware
- Any device sending probe requests for known Flock SSIDs
- Channel range: 1-13 (2412-2484 MHz)

**What gets recorded:**
- Source MAC address
- Signal strength (RSSI)
- Channel number
- SSID (from beacons/probe requests)
- Encryption type (open/WEP/WPA/WPA2/WPA3)
- Confidence score based on SSID match, OUI match, and sequential MAC patterns
- Timestamps: first seen, last seen, hit count

### BLE Scanning
**What it looks for:**
- BLE advertisement packets (non-connectable undirected advertisements)
- Specifically hunting for **Flock Safety BLE signatures**:
  - Manufacturer ID `0x0499` (Flock Safety / Will Greenberg)
  - Raven / Flock service UUID patterns (128-bit UUIDs)
  - Device names containing `Flock`, `Camera`, or `Raven`
- Uses NimBLE-Arduino scanner with time-multiplexing around WiFi promiscuous mode
- Frequency: 2400-2483.5 MHz

**What gets recorded:**
- BLE MAC address
- Signal strength (RSSI)
- Match type: manufacturer ID, service UUID, or device name
- Confidence score
- GPS coordinates if fix available at time of detection

### CC1101 Sub-GHz Detection
Scans all 4 bands every 5 seconds.

#### TPMS Detection (433 MHz)
**What it looks for:**
- **Tire Pressure Monitoring System** signals at 433.92 MHz
- Protocols: Schrader eXpanders, Continental TPMS, Huf TPMS
- Signal characteristics:
  - FSK or OOK modulation
  - Rolling code with ~30-60 second transmission intervals
  - Packet length: typically 8-16 bytes
  - Payload includes: sensor ID, pressure, temperature, battery status, rolling code counter
- While driving: stationary or slow-moving vehicles with TPMS sensors

**What gets recorded:**
- Frequency: 433.92 MHz
- RSSI
- Band: `433 MHz`
- Type: `tpms`
- Packet length
- MAC: `cc1101-01d0` (derived from frequency)

#### Car Remote / Key Fob Detection (315 MHz / 433 MHz)
**What it looks for:**
- **Car key fob** signals at 315 MHz (North America) or 433 MHz (Europe/Asia)
- Protocols: KeeLoq (HCS301), HCS200, AVR410
- Signal characteristics:
  - OOK/ASK modulation
  - Rolling code with sync counter
  - Button press events: lock, unlock, trunk release, panic
  - Packet length: typically 10-12 bytes afterManchester decoding
  - Transmission duration: ~30-100ms per button press
- While driving: other vehicles locking/unlocking, nearby parking lots

**What gets recorded:**
- Frequency: 315000 or 433920 Hz
- RSSI
- Band: `315 MHz` or `433 MHz`
- Type: `remote`
- Packet length
- MAC: `cc1101-004b` or `cc1101-01d0`

#### Weather Station Detection (433 MHz)
**What it looks for:**
- **Personal weather station** signals at 433 MHz
- Protocols: Acurite 00592TX, Oregon Scientific V1/V2/V3, Ambient Weather, Fine Offset
- Signal characteristics:
  - OOK or FSK modulation
  - Periodic transmission: every 20-40 seconds
  - Packet length: typically 6-12 bytes afterManchester decoding
  - Payload includes: temperature, humidity, wind speed/direction, rainfall, barometric pressure
  - Sensor ID for multi-sensor stations
- While driving: residential areas with mounted weather stations

**What gets recorded:**
- Frequency: 433.92 MHz
- RSSI
- Band: `433 MHz`
- Type: `weather`
- Packet length
- MAC: `cc1101-01d0`

#### Garage Door / Gate Opener Detection (315 MHz / 433 MHz)
**What it looks for:**
- **Garage door** and **gate opener** signals
- Protocols: KeeLoq, Linear DTMF, MultiCode
- Signal characteristics:
  - OOK/ASK modulation
  - Rolling or fixed code
  - Button press events
  - Short transmission: ~30-100ms
- While driving: residential garages, commercial gates

**What gets recorded:**
- Frequency: 315000 or 433920 Hz
- RSSI
- Band: `315 MHz` or `433 MHz`
- Type: `garage`
- Packet length
- MAC: `cc1101-004b` or `cc1101-01d0`

### LoRa Presence Detection
**What it looks for:**
- **LoRa-modulated signals** at 433, 868, or 915 MHz
- Current implementation: **signal presence only**
  - Frequency detected
  - Signal strength (RSSI)
  - Packet length
- **Why not decoded payloads?**
  - Requires exact radio parameters: spreading factor (SF7-SF12), bandwidth (125/250/500 kHz), coding rate (4/5-4/8), preamble length
  - Many proprietary LoRa devices use custom parameters
  - Without exact params: can detect chirps exist but cannot decode bits
- **Worth noting while driving:**
  - **Meshtastic nodes** if on your channel/frequency (open protocol, can decode if configured)
  - **LoRaWAN gateways** always present in cities
  - **Unknown sensors**: agriculture, metering, asset trackers

**What gets recorded:**
- Frequency: 433000, 868000, or 915000 Hz
- RSSI
- Method: `lora`
- SSID: `LoRa 433 MHz`, `LoRa 868 MHz`, or `LoRa 915 MHz`
- Packet length if available

### GPS Waypoints
**What it does:**
- Reads NMEA sentences from GPS Unit v1.1 (AT6668) over I2C
- Extracts: latitude, longitude, altitude, speed, satellite count, HDOP, timestamp
- **Btn B**: manual waypoint marker with label `"manual"`
- Auto-save: appends to `waypoints-YYYY-MM-DD.json` every 60 seconds when GPS fix valid
- File rolls over when date changes

**What gets recorded:**
- Unix timestamp
- Label (`manual` or `auto`)
- Latitude, longitude, altitude (meters)
- Speed (km/h)
- Satellite count
- HDOP (horizontal dilution of precision)
- GPS coordinates are also appended to **every WiFi/BLE/CC1101/LoRa detection** when fix is valid

### Webserver Mode
- **Btn C long press (800ms)** toggles AP mode
- SSID: `flock-you` / Password: `flockyou` (from `.env` at compile time)
- IP: `192.168.4.1`
- Display shows: WiFi status (connecting/connected/disconnected) + activity log
- Endpoints:
  - `GET /` - status page
  - `GET /files` - lists all detection/waypoint files sorted by date
  - `GET /file?name=flock_you-2026-05-01.json` - serves specific file

### Data Persistence
- **SPIFFS** (internal flash) or **SD card** (if M5Launcher)
- Detections: `flock_you-YYYY-MM-DD.json` (e.g., `flock_you-2026-05-01.json`)
- Waypoints: `waypoints-YYYY-MM-DD.json`
- Auto-save every 60s when new detections
- Manual save: **Btn A**

## Button Mapping

| Button | Action |
|--------|--------|
| **A** | Save session (all detections to JSON) |
| **B** | Record GPS waypoint (manual) |
| **C** (short) | Show recent detections list (8s auto-hide) |
| **C** (long, 800ms) | Toggle webserver on/off |

## Serial Debug Commands

All commands case-insensitive, end with newline:

| Command | Description |
|---------|-------------|
| `CMD:HELP` | List all commands |
| `CMD:FAKE` | Inject one fake detection per active module |
| `CMD:FAKE_WIFI` | Inject fake WiFi detection |
| `CMD:FAKE_BLE` | Inject fake BLE detection |
| `CMD:FAKE_TPMS` | Inject fake TPMS (CC1101) |
| `CMD:FAKE_REMOTE` | Inject fake car remote (CC1101) |
| `CMD:FAKE_WEATHER` | Inject fake weather station (CC1101) |
| `CMD:FAKE_GPS` | Inject fake GPS waypoint |
| `CMD:FAKE_LORA` | Inject fake LoRa detection |
| `CMD:CLEAR` | Clear all detections buffer |
| `CMD:STATUS` | Print module status, detection count, free heap |

**CMD:FAKE behavior:**
- 1 WiFi detection
- 1 BLE detection (if enabled)
- 3 CC1101 detections (TPMS + remote + weather)
- 1 LoRa detection (if present)
- 1 GPS waypoint (if present)

## Build & Upload

### Prerequisites
- PlatformIO
- `.env` file with WiFi credentials (never committed):
```bash
FLOCKYOU_WIFI_SSID=your_ssid
FLOCKYOU_WIFI_PASS=your_password
```

### Compile-time credential injection
`generate_build_flags.py` reads `.env` and injects `FY_WS_SSID`/`FY_WS_PASS` into firmware binary. No secrets in source or runtime config.

### Build environments
```bash
pio run -e m5stack-basic-launcher    # Standard (BLE + launcher)
pio run -e m5stack-basic-launcher-ble # BLE + launcher (deprecated, merged)
pio run -e m5stack-basic             # No launcher
pio run -e m5stack-basic-ble         # BLE, no launcher
```

### Upload
```bash
pio run -e m5stack-basic-launcher -t upload
```

### Monitor
```bash
screen /dev/ttyUSB0 115200
# or
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200
```

## M5Launcher Integration
1. Copy `firmware.bin` to SD card: `SD:/firmwares/flock-you/firmware.bin`
2. Boot M5Launcher - select flock-you
3. Data written to `SD:/firmwares/flock-you/data/`

## Flask Web App (Host-side)

See `api/flockyou.py` for the Flask backend that:
- Receives detections via serial/websocket
- Matches GPS coordinates temporally
- Serves data via `/api/files`, `/api/file/<name>`
- Exports CSV/KML

Run:
```bash
cd api
python3 -m venv venv && source venv/bin/activate
pip install -r requirements.txt
cp .env.example .env  # add WiFi creds if needed
python3 flockyou.py
```

## WiFi Credentials Security

- **`.env`** - gitignored, single source of truth for host Flask app
- **Compile-time injection** - `generate_build_flags.py` reads `.env`, injects into firmware
- **No secrets** in source code, no runtime config files on device
- **`.env.example`** - template committed to repo

## File Formats

### Detection JSON (`flock_you-YYYY-MM-DD.json`)
```json
{
  "mac": "aa:bb:cc:dd:ee:ff",
  "method": "wifi|ble|subghz|lora",
  "rssi": -45,
  "channel": 6,
  "first": 1704067200,
  "last": 1704067260,
  "count": 5,
  "ssid": "Flock Camera net.",
  "confidence": 85,
  "gps": {"lat": 40.7128, "lon": -74.0060, "alt": 10.5, "speed": 0.0, "satellites": 8, "hdop": 1.2},
  "lora": {"available": true},
  "subghz": {"freq_mhz": 433, "rssi": -50, "band": 1, "type": "tpms", "len": 10}
}
```

### Waypoint JSON (`waypoints-YYYY-MM-DD.json`)
```json
{"ts":1704067200,"label":"manual","lat":40.7128,"lon":-74.0060,"alt":10.5,"sat":8,"hdop":1.2}
```

## Links

- **M5Stack Basic:** https://shop.m5stack.com/products/m5stack-basic-v2-6
- **GPS Unit v1.1:** https://shop.m5stack.com/products/gps-unit-v1-1
- **LoRa Module 433MHz:** https://shop.m5stack.com/products/lora-module-433mhz
- **CC1101 Module:** https://shop.m5stack.com/products/m5stack-cc1101-module-855-925mhz
- **M5Launcher:** https://github.com/bmorcelli/M5Launcher
- **Meshtastic:** https://meshtastic.org/
- **Reticulum:** https://github.com/markqvist/Reticulum