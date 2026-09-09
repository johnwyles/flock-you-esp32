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
- Channels 1-13 (2412-2484 MHz)
- Probe requests, beacons, deauth frames
- Flock camera SSID matching (`Flock Camera net.`, `Flock-XXXXXX`)
- MAC OUI tracking for manufacturer identification
- Confidence scoring system

### BLE Scanning (enabled by default)
- 2400-2483.5 MHz
- Flock BLE manufacturer ID detection
- Raven service UUID detection
- Device name matching

### GPS Waypoints
- Records lat/lon/alt/satellites/HDOP
- Manual waypoint: **Btn B** press
- Auto-saved every 60s when fix valid
- Date-rolling file: `waypoints-YYYY-MM-DD.json` (e.g., `waypoints-2026-05-01.json`)

### CC1101 Sub-GHz Detection
Scans all 4 bands every 5 seconds:
| Band | Frequency | Signals detected |
|------|-----------|------------------|
| 315 MHz | Car key fobs, garage doors | OOK/ASK modulation |
| 433 MHz | TPMS (tire pressure), weather stations, car remotes | FSK/OOK |
| 868 MHz | European ISM sensors | FSK/OOK |
| 915 MHz | US ISM sensors | FSK/OOK |

Each detection logged with: MAC (`cc1101-XXXX`), freq MHz, RSSI dBm, band, type (TPMS/remote/weather/garage/LoRa), packet length

### LoRa Presence Detection
- Detects LoRa-modulated signals at 433/868/915 MHz
- Logs frequency, RSSI, packet length
- Without Meshtastic parameters: signal presence only

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