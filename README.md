# 🏭 IIoT Industrial Gateway — ESP32-S3

> Passerelle IoT Industrielle basée sur ESP32-S3 pour l'acquisition de données capteurs industriels, avec interface de configuration Wi-Fi et architecture IIoT complète (Edge → Fog → Cloud).

![ESP32](https://img.shields.io/badge/ESP32--S3-WROOM-blue)
![PlatformIO](https://img.shields.io/badge/PlatformIO-Framework-orange)
![MQTT](https://img.shields.io/badge/MQTT-Protocol-green)
![License](https://img.shields.io/badge/License-MIT-yellow)

---

## 📋 Table of Contents

- [Project Overview](#project-overview)
- [Architecture](#architecture)
- [Hardware Requirements (BOM)](#hardware-requirements-bom)
- [Wiring Guide](#wiring-guide)
- [Software Setup](#software-setup)
- [Features](#features)
- [Configuration Guide](#configuration-guide)
- [IIoT Stack Setup (Fog + Cloud)](#iiot-stack-setup-fog--cloud)
- [Issues & Solutions Log](#issues--solutions-log)
- [API Reference](#api-reference)
- [Contributors](#contributors)

---

## Project Overview

This project implements an **Industrial IoT Gateway** that bridges industrial sensors to modern IoT platforms. The ESP32-S3 acts as the **Edge layer**, reading data from multiple sensor types, processing and calibrating raw values, then publishing structured JSON data over MQTT to a fog/cloud stack.

### Functional Requirements (Cahier des Charges)

| Code | Requirement | Status |
|------|------------|--------|
| **F-01** | Digital acquisition (Modbus RTU via RS-485) | ⚠️ Blocked — see [Issues](#f-01-modbus-rs485-communication-failure) |
| **F-02** | Analog acquisition (4-20mA / 0-10V) | ✅ Implemented (0-10V via QFM3160 + NTC thermistors) |
| **F-03** | Calibration & scaling (raw ADC → physical units) | ✅ Implemented |
| **F-04** | Local display (Serial Monitor via USB) | ✅ Implemented |
| **F-05** | Wireless config interface (Wi-Fi Access Point) | ✅ Implemented |
| **F-06** | Dynamic parameterization (web-based config) | ✅ Implemented |
| **F-07** | IoT data transfer (MQTT JSON) | ✅ Implemented |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        EDGE LAYER                               │
│                                                                 │
│  ┌────────────┐   ┌────────────┐   ┌──────────────────┐        │
│  │  NTC 10K   │   │  NTC 20K   │   │  Siemens QFM3160 │        │
│  │ (GPIO1)    │   │ (GPIO2)    │   │  U1→GPIO4 (Hum)  │        │
│  │ Temp only  │   │ Temp only  │   │  U2→GPIO3 (Temp) │        │
│  └─────┬──────┘   └─────┬──────┘   └────────┬─────────┘        │
│        │                │                    │                  │
│        └────────────────┼────────────────────┘                  │
│                         │                                       │
│               ┌─────────▼──────────┐                            │
│               │   ESP32-S3 WROOM   │                            │
│               │   - ADC Reading    │                            │
│               │   - Steinhart-Hart │                            │
│               │   - WiFi AP + STA  │                            │
│               │   - Web Dashboard  │                            │
│               │   - MQTT Client    │                            │
│               │   - mDNS           │                            │
│               └─────────┬──────────┘                            │
│                         │ MQTT (JSON)                           │
└─────────────────────────┼───────────────────────────────────────┘
                          │
┌─────────────────────────┼───────────────────────────────────────┐
│                    FOG LAYER                                    │
│                         │                                       │
│               ┌─────────▼──────────┐                            │
│               │  Mosquitto Broker  │                            │
│               └─────────┬──────────┘                            │
│                         │                                       │
│               ┌─────────▼──────────┐                            │
│               │     Node-RED       │                            │
│               │  - MQTT Subscribe  │                            │
│               │  - Data transform  │                            │
│               │  - Write InfluxDB  │                            │
│               └─────────┬──────────┘                            │
│                         │                                       │
└─────────────────────────┼───────────────────────────────────────┘
                          │
┌─────────────────────────┼───────────────────────────────────────┐
│                   CLOUD LAYER                                   │
│                         │                                       │
│               ┌─────────▼──────────┐                            │
│               │     InfluxDB       │                            │
│               │  - Time series DB  │                            │
│               └─────────┬──────────┘                            │
│                         │                                       │
│               ┌─────────▼──────────┐                            │
│               │      Grafana       │                            │
│               │  - Dashboard       │                            │
│               │  - Alerts          │                            │
│               └────────────────────┘                            │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Hardware Requirements (BOM)

### Core Components

| Component | Model | Role | Qty |
|-----------|-------|------|-----|
| Microcontroller | ESP32-S3-WROOM (N16R8) | Edge gateway — WiFi, ADC, processing | 1 |
| Power adapter | Vauxtech 240V→24V 2A DC | Powers sensors + step-down input | 1 |
| Step-down converter | LM2596HV | 24V→5V for ESP32 | 1 |

### Sensors

| Sensor | Model | Type | Output | Measures |
|--------|-------|------|--------|----------|
| NTC 10K | VCP TDC NTC 10K 200 | Passive thermistor | Resistance (10kΩ @ 25°C) | Temperature |
| NTC 20K | Honeywell NTC 20kΩ | Passive thermistor | Resistance (20kΩ @ 25°C) | Temperature |
| QFM3160 | Siemens QFM3160 | Active transmitter | 0-10V (U1: Humidity, U2: Temp) | Temperature + Humidity |
| CWT-XYTH-S-D | CWT | Digital RS485 | Modbus RTU | Temperature + Humidity |

### Passive Components

| Component | Value | Qty | Purpose |
|-----------|-------|-----|---------|
| Resistor | 10kΩ | 2 | NTC 10K voltage divider + QFM R2 divider |
| Resistor | 22kΩ | 3 | NTC 20K divider + QFM R1 dividers (×2) |

### Optional (for Modbus — not yet working)

| Component | Model | Role |
|-----------|-------|------|
| RS485 transceiver | MAX485 module | TTL ↔ RS485 conversion |
| Termination resistor | 120Ω | RS485 bus termination |
| Bias resistors | 560Ω × 2 | RS485 bus biasing |

---

## Wiring Guide

### Power Supply Chain

```
Wall outlet 240V AC
        │
  ┌─────▼──────────┐
  │ Vauxtech 24V 2A │
  └─────┬──────────┘
        │ 24V DC
        ├──────────────────────── QFM3160 G terminal (24V)
        │
  ┌─────▼──────────┐
  │   LM2596HV     │
  │  Set to 5.0V   │
  └─────┬──────────┘
        │ 5V DC
        └──────────────────────── ESP32 VIN
```

> ⚠️ **IMPORTANT**: Before connecting the ESP32, use a multimeter to verify the LM2596HV output is exactly 5.0V. Adjust the blue potentiometer screw slowly until the reading is correct.

### NTC 10K Wiring (GPIO1)

```
ESP32 3.3V ────── NTC 10K ──── Junction ──── 10kΩ ──── GND
                                  │
                               GPIO1
```

### NTC 20K Wiring (GPIO2)

```
ESP32 3.3V ────── NTC 20K ──── Junction ──── 22kΩ ──── GND
                                  │
                               GPIO2
```

### Siemens QFM3160 Wiring (GPIO3 + GPIO4)

The QFM3160 has labeled terminals inside:

| Terminal | Function | Connect to |
|----------|----------|------------|
| G | 24V power | Adapter + (24V) |
| G0 | Ground | Common GND (Wago) |
| U1 | Humidity output (0-10V) | Voltage divider → GPIO4 |
| U2 | Temperature output (0-10V) | Voltage divider → GPIO3 |

Each output needs a voltage divider to scale 0-10V → 0-3.03V (safe for ESP32 ADC):

```
U1 (0-10V) ──── 22kΩ (R1) ──── Junction ──── 10kΩ (R2) ──── GND
                                    │
                                 GPIO4

U2 (0-10V) ──── 22kΩ (R1) ──── Junction ──── 10kΩ (R2) ──── GND
                                    │
                                 GPIO3
```

### Common Ground (Wago Connector)

All grounds must be shared via a single Wago connector:

```
Wago GND rail:
├── ESP32 GND
├── NTC 10K resistor bottom
├── NTC 20K resistor bottom
├── QFM3160 G0 terminal
├── QFM U1 divider R2 bottom
├── QFM U2 divider R2 bottom
└── LM2596HV OUT−
```

> ⚠️ **NEVER** put signal junctions (GPIO1, GPIO2, GPIO3, GPIO4) into the same Wago as GND. Each junction must remain separate and go directly to its own GPIO pin.

### Complete Pin Map

| ESP32-S3 Pin | Function |
|-------------|----------|
| GPIO1 | NTC 10K ADC input |
| GPIO2 | NTC 20K ADC input |
| GPIO3 | QFM3160 U2 (Temperature) via divider |
| GPIO4 | QFM3160 U1 (Humidity) via divider |
| 3.3V | NTC voltage divider power rail (Wago) |
| GND | Common ground rail (Wago) |
| VIN (5V) | From LM2596HV output |

---

## Software Setup

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- [Git](https://git-scm.com/)

### Clone and Build

```bash
# Clone the repository
git clone https://github.com/YOUR-USERNAME/iiot-gateway.git
cd iiot-gateway

# Build the firmware
pio run

# Upload to ESP32-S3
pio run --target upload

# Open Serial Monitor
pio device monitor -b 115200
```

### PlatformIO Configuration

The project uses the following `platformio.ini`:

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

lib_deps =
  bblanchon/ArduinoJson @ ^7.0.0
  me-no-dev/AsyncTCP @ ^1.1.1
  mathieucarbou/ESPAsyncWebServer @ ^3.0.0
  knolleary/PubSubClient @ ^2.8.0

monitor_speed = 115200
```

---

## Features

### 1. Multi-Sensor Acquisition

- **NTC 10K thermistor** — Steinhart-Hart equation with configurable B coefficient
- **NTC 20K thermistor** — Same algorithm, different calibration constants
- **Siemens QFM3160** — Dual output (humidity + temperature) via 0-10V with voltage divider scaling
- All raw ADC values converted to physical units (°C, %RH)
- Error detection for disconnected/unpowered sensors (`< 0.1V` threshold)

### 2. WiFi Access Point + Dashboard

- ESP32 creates its own WiFi network (`IIoT-Sensors` by default)
- Web dashboard accessible at `http://sensor.local` (mDNS) or `http://192.168.4.1`
- Live sensor readings update every 2 seconds via AJAX polling
- Dark-themed industrial UI with real-time status indicators
- Responsive design works on mobile and desktop

### 3. Dynamic Configuration (Persistent Flash Storage)

All parameters are saved to flash (ESP32 Preferences/NVS) and survive reboot:

| Category | Parameters |
|----------|-----------|
| Access Point | SSID, Password, Hostname (mDNS) |
| WiFi Router (STA) | Router SSID, Router Password |
| NTC 10K | Series resistance (Ω), Nominal resistance (Ω) |
| NTC 20K | Series resistance (Ω), Nominal resistance (Ω) |
| NTC Shared | B coefficient, Nominal temperature (°C) |
| QFM3160 | R1 (Ω), R2 (Ω), Humidity max, Temp max, Temp offset |
| General | Read interval (ms) |

### 4. Dual WiFi Mode (AP + STA)

- **AP mode** — Always active for configuration access
- **STA mode** — Connects to a local router to reach the MQTT broker
- Both modes run simultaneously (`WIFI_AP_STA`)
- STA connection status shown on dashboard

### 5. mDNS

- Access the dashboard via hostname instead of IP address
- Default: `http://sensor.local`
- Configurable from the dashboard

### 6. MQTT Publishing

- Publishes JSON-formatted sensor data to configurable MQTT topic
- Configurable broker IP, port, credentials, and topic
- Retained status message (`online`/`offline`)
- Automatic reconnection with 5-second retry interval

### 7. Serial Monitor Output

- Periodic formatted output of all sensor readings
- Modbus error codes displayed for debugging
- WiFi connection status logging

---

## Configuration Guide

### First-Time Setup

1. **Power the system** — Connect 24V adapter, verify 5V on LM2596HV, connect ESP32
2. **Connect to WiFi** — On your phone/laptop, connect to `IIoT-Sensors` (password: `12345678`)
3. **Open dashboard** — Navigate to `http://sensor.local` or `http://192.168.4.1`
4. **Configure router WiFi** — Enter your router SSID and password under "WiFi Router (STA)"
5. **Configure MQTT** — Enter broker IP and topic (if using MQTT)
6. **Save & Apply** — Click the save button, settings persist across reboots

### Calibrating Sensors

#### NTC Sensors
- If NTC reads too HIGH vs a reference thermometer → **decrease B coefficient**
- If NTC reads too LOW vs a reference thermometer → **increase B coefficient**
- Typical range: 3400 to 4200

#### QFM3160
- Check the 3 unlabeled terminals on the left side of the sensor for range jumper:
  - R1 = 0 to +50°C (default, `TEMP_MAX = 50`)
  - R2 = -50 to +50°C (`TEMP_MAX = 100`, add `-50` offset)
  - R3 = 0 to +100°C (`TEMP_MAX = 100`)
- Use the **Temp offset** field to compensate for self-heating (~-3.5°C typical)

---

## IIoT Stack Setup (Fog + Cloud)

### Docker Compose (recommended)

Create `docker-compose.yml`:

```yaml
version: '3.8'

services:
  mosquitto:
    image: eclipse-mosquitto:2
    container_name: mosquitto
    ports:
      - "1883:1883"
      - "9001:9001"
    volumes:
      - ./mosquitto/config:/mosquitto/config
      - ./mosquitto/data:/mosquitto/data
    restart: unless-stopped

  nodered:
    image: nodered/node-red
    container_name: nodered
    ports:
      - "1880:1880"
    volumes:
      - ./nodered-data:/data
    restart: unless-stopped

  influxdb:
    image: influxdb:1.8
    container_name: influxdb
    ports:
      - "8086:8086"
    environment:
      - INFLUXDB_DB=iiot
      - INFLUXDB_ADMIN_USER=admin
      - INFLUXDB_ADMIN_PASSWORD=admin123
    volumes:
      - ./influxdb-data:/var/lib/influxdb
    restart: unless-stopped

  grafana:
    image: grafana/grafana
    container_name: grafana
    ports:
      - "3000:3000"
    environment:
      - GF_SECURITY_ADMIN_PASSWORD=admin123
    volumes:
      - ./grafana-data:/var/lib/grafana
    restart: unless-stopped
```

### Mosquitto Config

Create `mosquitto/config/mosquitto.conf`:

```
listener 1883
allow_anonymous true

listener 9001
protocol websockets
```

### Start the Stack

```bash
docker compose up -d
```

### Node-RED Flow Setup

1. Open `http://localhost:1880`
2. Install palette: `node-red-contrib-influxdb`
3. Import the flow from `node-red/flow.json` in this repo
4. Configure MQTT broker node: `localhost:1883`, topic `iiot/sensor/data`
5. Configure InfluxDB node: `http://influxdb:8086`, database `iiot`
6. Deploy

### Grafana Dashboard Setup

1. Open `http://localhost:3000` (login: admin / admin123)
2. Add data source → InfluxDB → URL: `http://influxdb:8086` → Database: `iiot`
3. Create dashboard → Add panel
4. Example queries:

```sql
-- Temperature over time
SELECT mean("ntc10k") FROM "environment"
WHERE $timeFilter GROUP BY time($__interval)

-- Humidity over time
SELECT mean("qfmHum") FROM "environment"
WHERE $timeFilter GROUP BY time($__interval)
```

### MQTT Topic Structure

| Topic | Payload | Description |
|-------|---------|-------------|
| `iiot/sensor/data` | `{"ntc10k":22.1,"ntc20k":20.0,"qfmHum":55.2,"qfmTemp":22.4}` | Sensor readings |
| `iiot/sensor/status` | `online` / `offline` | Gateway status (retained) |

---

## Issues & Solutions Log

### RESOLVED ✅

#### ESP32-S3 UART pin conflict

**Problem**: Modbus communication returned `0xE2` (no response) on GPIO17/GPIO18. UART loopback test also failed on these pins.

**Root cause**: The ESP32-S3-WROOM board has GPIO17/GPIO18 reserved for internal flash or USB functions. They are NOT available as general-purpose UART pins on this specific board variant.

**Solution**: Performed UART loopback tests on multiple pin pairs. Confirmed **GPIO43 (TX) and GPIO44 (RX)** work correctly for Serial communication on this board.

**Lesson**: Always verify UART pin availability on your specific ESP32-S3 board variant before assuming standard pinouts apply.

---

#### Power supply SUAT EC 24V — 0V output

**Problem**: The SUAT EC 24V 2.5A DIN rail power supply showed 220V on input (Ph to N) but 0V on the DC output terminals. The green LED blinked briefly and then the unit shut off.

**Root cause**: Overcurrent protection was triggering immediately — likely a short circuit somewhere in the output wiring, or output wires not making proper contact in the terminals.

**Solution**: Replaced with a **Vauxtech 240V→24V 2A wall adapter** (barrel jack). Cut the barrel jack, identified + and − wires with multimeter, connected directly. Works perfectly. Cost: 1,950 DZD.

**Lesson**: For prototyping, a simple DC wall adapter is far easier and more reliable than a DIN rail supply. Reserve DIN rail supplies for final panel installations.

---

#### NTC readings wildly unstable (jumping from -40°C to +150°C)

**Problem**: NTC sensor readings were completely random — raw ADC values jumping between 100 and 4095 between reads.

**Root cause**: Poor physical connection at the junction point. The screw terminal on the ESP32 carrier board couldn't clamp both the NTC wire and the resistor leg reliably. They were making intermittent contact.

**Solution**: **Twist the NTC wire and resistor leg together tightly** (4-5 twists) before inserting into the screw terminal. This creates a single solid connection point.

**Lesson**: Screw terminals can only reliably hold one conductor. When two wires must share a terminal, twist them together first.

---

#### NTC resistance formula giving wrong values

**Problem**: First attempt showed resistance of 60-90kΩ (should be ~10kΩ at room temp). Second attempt showed ~1.7kΩ.

**Root cause**: The voltage divider formula must match the physical placement of the NTC vs the fixed resistor. If NTC is on the 3.3V side (top) but the code assumes it's on the GND side (bottom), the formula is inverted.

**Solution**: Two options that must be consistent:
- **NTC on top (3.3V → NTC → junction → resistor → GND)**: use `R = R_series * (3.3/V - 1.0)`
- **NTC on bottom (3.3V → resistor → junction → NTC → GND)**: use `R = R_series / (ADC_MAX/raw - 1.0)`

**Lesson**: Always document which component is on which side of the voltage divider. The formula MUST match the physical circuit.

---

#### QFM3160 U1 and U2 pins swapped

**Problem**: QFM3160 temperature reading showed ~50°C (impossible for room temp) and humidity showed ~25% (suspiciously temperature-like).

**Root cause**: U1 (humidity) and U2 (temperature) were connected to the wrong GPIO pins — the values were being interpreted with the wrong scaling.

**Solution**: Swapped the pin definitions:
```cpp
#define QFM_U1_PIN    4   // humidity (was 3)
#define QFM_U2_PIN    3   // temperature (was 4)
```

**Lesson**: When two outputs from the same sensor give suspicious values, try swapping the pin assignments before debugging the math.

---

#### QFM3160 showing "readings" when unpowered

**Problem**: Before powering the QFM3160, the dashboard still showed temperature and humidity values (not ERROR).

**Root cause**: Floating ADC pins pick up electrical noise from the environment. The code converts this noise into fake readings.

**Solution**: Added a minimum voltage threshold in the QFM reader function:
```cpp
if (vADC < 0.1) return -999.0;  // sensor likely unpowered
```

**Lesson**: Always add a minimum threshold check on ADC inputs connected to active sensors. Passive sensors (NTC) don't have this problem because they're always connected.

---

#### QFM3160 temperature offset (~3-5°C higher than NTCs)

**Problem**: QFM3160 temperature consistently reads 3-5°C higher than both NTC sensors when all sensors are within 4cm of each other.

**Root cause**: Self-heating from the QFM3160's internal electronics. This is documented behavior for active sensors with integrated conditioning circuits.

**Solution**: Added a configurable temperature offset parameter (default: -3.5°C) adjustable from the web dashboard.

---

#### Hager ST312 transformer — wrong output type

**Problem**: Attempted to use a Hager ST312 transformer to power the project. It provides 24V but the sensor and ESP32 need DC.

**Root cause**: The Hager ST312 is an **AC transformer** (24V AC output), not a DC power supply. Feeding AC into DC components would damage them.

**Solution**: Would require a bridge rectifier (W10M) + smoothing capacitor (1000µF) to convert to DC (~33V), then LM2596HV to step down. Ultimately abandoned in favor of the simpler Vauxtech DC adapter.

---

### UNRESOLVED ⚠️

#### F-01: Modbus RS485 communication failure

**Problem**: The CWT-XYTH-S-D RS485 sensor never responds to Modbus requests. All attempts return error `0xE2` (no response / timeout).

**What was tried**:
1. ✅ Swapped A and B wires on MAX485 screw terminals
2. ✅ Tried Modbus addresses 0 and 1
3. ✅ Tried both `readInputRegisters` (FC04) and `readHoldingRegisters` (FC03)
4. ✅ Tried register addresses 0x0000 and 0x0001
5. ✅ Confirmed sensor is powered (display shows correct values)
6. ✅ Confirmed ESP32 UART works (loopback test passes on GPIO43/44)
7. ✅ Ran full address scanner (0-247, FC03 + FC04)
8. ✅ Sent raw Modbus bytes manually — response: NOTHING
9. ✅ Attempted MAX485 loopback (A→B bridged) with Arduino — FAILED
10. ✅ Attempted pure Arduino SoftwareSerial loopback — WORKS
11. ❌ MAX485 module may be faulty — loopback through the module consistently fails

**Current hypothesis**: The MAX485 module itself is defective. The chip is not transmitting or receiving on the A/B lines. This was confirmed by the Arduino loopback test — SoftwareSerial works alone, but adding the MAX485 module in the path breaks communication.

**Possible additional causes**:
- Missing termination resistor (120Ω between A and B)
- Missing bias resistors (560Ω pull-up on A, pull-down on B)
- CWT sensor may use non-standard Modbus settings (different baud rate, parity, etc.)

**Next steps**:
- Replace the MAX485 module with a known-good unit
- Add 120Ω termination and 560Ω bias resistors
- Test with a USB-to-RS485 adapter from a PC to verify sensor communication independently

---

#### F-02: 4-20mA acquisition not implemented

**Problem**: The cahier des charges requires reading a 4-20mA industrial transmitter via the HW-685 module. Currently using 0-10V (QFM3160) and passive NTC thermistors instead.

**Root cause**: No 4-20mA pressure transmitter was available for testing. The HW-685 module was initially confused with a MAX485 RS485 module.

**What the HW-685 actually does**: Converts a 4-20mA current loop signal to 0-3.3V voltage readable by the ESP32 ADC.

**Implementation plan** (when hardware is available):
```cpp
#define HW685_PIN  5  // any free ADC pin
float read420mA(int pin) {
  int raw = analogRead(pin);
  float voltage = (raw / 4095.0) * 3.3;
  // HW-685: 0V = 4mA, 3.3V = 20mA
  float current = 4.0 + (voltage / 3.3) * 16.0;  // mA
  // Example: pressure transmitter 0-10 Bar
  float pressure = ((current - 4.0) / 16.0) * 10.0;  // Bar
  return pressure;
}
```

---

## API Reference

### `GET /`
Returns the HTML dashboard page.

### `GET /api/data`
Returns current sensor readings as JSON.

```json
{
  "ntc10k": 22.13,
  "ntc20k": 20.02,
  "qfmHum": 70.1,
  "qfmTemp": 22.34,
  "staConnected": true,
  "staIP": "192.168.1.105"
}
```

Values of `null` or `<= -900` indicate sensor error.

### `GET /api/config`
Returns all configurable parameters as JSON.

### `POST /api/config`
Saves new configuration. Body: JSON with any/all config fields. Settings are persisted to flash and applied immediately.

---

## Project Structure

```
iiot-gateway/
├── README.md                    # This file
├── platformio.ini               # PlatformIO build config
├── src/
│   └── main.cpp                 # Complete ESP32 firmware
├── node-red/
│   └── flow.json                # Node-RED flow (import ready)
├── docker/
│   ├── docker-compose.yml       # Full IIoT stack
│   └── mosquitto/
│       └── mosquitto.conf       # Broker config
├── docs/
│   ├── wiring-guide.md          # Detailed wiring instructions
│   ├── troubleshooting.md       # Common issues and fixes
│   └── images/                  # Photos and diagrams
└── LICENSE
```

---

## Contributors

| Name | GitHub | Role |
|------|--------|------|
| Soheib miloudi| [@YOUR-USERNAME](https://github.com/soheib_man) | Hardware wiring, sensor integration, firmware development |
| Abderrahim Megrouz | [@megrouz-abderrahim](https://github.com/megrouz-abderrahim) | Hardware wiring, sensor integration, firmware development |
| madina  | [@YOUR-USERNAME](https://github.com/dynatella) | node red config, mqtt integration,   fog development |

---

## License

This project is licensed under the MIT License — see [LICENSE](LICENSE) for details.
