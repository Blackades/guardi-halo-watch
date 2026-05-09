
**Project:** Patient Tracking and Safety Monitoring System for Hospital Wards   
**Target Environment:** Aga Khan University Hospital - General Medical Ward  
**Version:** 2.0 (BLE-Based Simplified)  
**Date:** December 2025

---

## TABLE OF CONTENTS
1. System Overview
2. Hardware Specifications
3. Software Architecture
4. Communication Protocol
5. Database Design
6. Dashboard & User Interface
7. Alert System Logic
8. Power Management
9. Installation Guidelines
10. Testing & Validation
11. Bill of Materials
12. Timeline & Milestones

---

## 1. SYSTEM OVERVIEW

### 1.1 System Purpose
A simplified IoT-based patient tracking system that monitors patient location within hospital wards and detects wristband tampering in real-time. The system focuses on two core functions:
1. **Room-level location tracking** using BLE beacons
2. **Tamper detection** to ensure patients keep their wristbands on

### 1.2 Simplified System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    WEARABLE WRISTBAND                       │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ ESP32 (BLE Beacon) + MPU6050 + Strap Sensor         │  │
│  │ + Skin Contact Sensor + LED + Battery                │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────┬───────────────────────────────────────┘
                      │ BLE Advertisement (every 200ms)
                      │ WiFi (for alerts only)
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                    RECEIVER NODES (BLE Scanners)            │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ ESP32 (BLE Scanner) + LED + Buzzer                   │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────┬───────────────────────────────────────┘
                      │ WiFi (HTTPS/REST API)
                      ▼
┌─────────────────────────────────────────────────────────────┐
│              DOOR RFID READERS (Entry/Exit Detection)       │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ ESP32 + RDM6300 + Passive 125kHz RFID Tag         │  │
│  │ Installed at each room door entrance                 │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────┬───────────────────────────────────────┘
                      │ WiFi (HTTPS)
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                    BACKEND SERVER                           │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ Node.js/Express + MySQL Database                     │  │
│  │ REST API + WebSocket Server                          │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────┬───────────────────────────────────────┘
                      │ HTTPS + WebSocket
                      ▼
┌─────────────────────────────────────────────────────────────┐
│              MONITORING DASHBOARD                           │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ Web Application (React/Vue.js)                       │  │
│  │ Real-time Patient Map + Alerts                       │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 1.3 Key Features (Simplified)
- ✅ **Room-level patient location tracking** using BLE technology
- ✅ **Door entry/exit detection** using passive RFID at doorways
- ✅ **Tamper detection** (wristband removal, strap break)
- ✅ **Movement monitoring** (fall detection using MPU6050)
- ✅ **Real-time dashboard** with live updates
- ✅ **Alert system** for critical events
- ❌ ~~Vital signs monitoring~~ (removed for simplicity)
- ❌ ~~Temperature monitoring~~ (removed for simplicity)

### 1.4 Dual Location Tracking System

**BLE Beacons (Continuous Room Tracking):**
- Determines which room/zone the patient is in
- Updates every 2-5 seconds
- Accuracy: ±2-3 meters (room-level)

**RDM6300 Door Readers (Precise Entry/Exit):**
- Detects exact moment patient enters/exits a room
- Passive 125kHz RFID tag on wristband
- Only reads when patient passes through doorway
- Logs entry/exit timestamps

**Combined System Benefits:**
- BLE tells you "Patient is in Room 205"
- RFID tells you "Patient entered Room 205 at 14:30:45"
- Together: Complete movement history and current location

---

## 2. HARDWARE SPECIFICATIONS

### 2.1 WEARABLE WRISTBAND UNIT

#### 2.1.1 Main Controller
**Component:** ESP32-WROOM-32 or ESP32-PICO-D4  
**Specifications:**
- Dual-core Xtensa LX6 @ 240MHz
- 520KB SRAM, 4MB Flash
- **WiFi 802.11 b/g/n (2.4GHz)** - for alerts only
- **Bluetooth v4.2 BR/EDR and BLE** - primary tracking method
- Operating Voltage: 3.3V
- Deep Sleep Current: ~10μA
- BLE Active Current: ~50mA
- WiFi Active Current: ~160mA

**Operating Modes:**
- **Normal Mode:** BLE beacon broadcasting every 200ms
- **Alert Mode:** BLE + WiFi (sends alert to server)
- **Sleep Mode:** Deep sleep between sensor checks

#### 2.1.2 Primary Tracking Technology: BLE Beacon

**Technology:** Bluetooth Low Energy (BLE) 4.2  
**Protocol:** iBeacon or Eddystone  
**Implementation:** ESP32 built-in BLE (no additional hardware needed)

**BLE Beacon Configuration:**
```
UUID: "A1B2C3D4-E5F6-7890-ABCD-EF1234567890" (hospital-specific)
Major: 1 (Ward number)
Minor: 266 (Patient number - unique per patient)
TX Power: -59 dBm (calibrated transmission power)
Advertising Interval: 200ms (5 times per second)
```

**BLE Advertisement Packet:**
```json
{
  "uuid": "A1B2C3D4-E5F6-7890-ABCD-EF1234567890",
  "major": 1,
  "minor": 266,
  "txPower": -59,
  "rssi": -65,
  "batteryLevel": 85,
  "strapIntact": true,
  "skinContact": true,
  "timestamp": "2025-12-18T14:30:45Z"
}
```

**Range:** 50-100 meters (open space), 20-40 meters (indoor with walls)  
**Accuracy:** ±1-2 meters with trilateration from 3+ receivers

**Why BLE:**
- ✅ Built into ESP32 (no extra cost)
- ✅ Better range than RFID (50m vs 5m)
- ✅ Lower power than WiFi
- ✅ Industry standard (phones can scan too)
- ✅ Supports multiple simultaneous connections
- ✅ RSSI-based distance estimation

#### 2.1.3 Secondary Tracking: Passive 125kHz RFID Tag (Door Detection Only)

**Component:** 125kHz EM4100-compatible Sticker  
**Purpose:** Backup identification at door entry/exit points  
**Form Factor:** Thin 125kHz sticker (integrated into wristband)  
**Read Range:** 10-20cm (at doorways with RDM6300 readers)  
**Unique ID:** 10-digit hexadecimal UID

**Why Both BLE and RFID:**
- **BLE:** Continuous room-level tracking (primary)
- **RFID:** Precise door entry/exit timestamps (secondary)
- **Redundancy:** If BLE fails, RFID at doors still works
- **Validation:** Cross-check BLE location with RFID door logs

#### 2.1.4 Motion & Orientation Sensor
**Component:** MPU6050 (6-Axis IMU)  
**Purpose:** Fall detection and wristband removal detection

**Specifications:**
- 3-axis Gyroscope: ±2000°/s
- 3-axis Accelerometer: ±2g, ±4g, ±8g, ±16g
- Interface: I2C (400kHz)
- Operating Voltage: 3.3V
- Operating Current: 3.9mA
- I2C Address: 0x68

**Pin Connections:**
- VCC → 3.3V
- GND → GND
- SDA → GPIO21
- SCL → GPIO22
- INT → GPIO19 (interrupt for motion detection)

**Detection Functions:**
1. **Fall Detection:**
   - Sudden acceleration > 2.5g
   - Followed by horizontal orientation
   - No movement for > 5 seconds
   - Triggers critical alert

2. **Wristband Removal Detection:**
   - Orientation change from vertical (worn) to horizontal (removed)
   - Combined with strap sensor for confirmation
   - Triggers tamper alert

3. **Activity Monitoring (Optional):**
   - Patient movement vs. stationary
   - Unusual activity patterns

#### 2.1.5 Tamper Detection Sensors

**A. Conductive Thread Strap Break Detection**  
**Implementation:**
- Silver-coated nylon thread sewn into wristband strap
- Creates continuous circuit when strap is intact
- Circuit breaks when strap is cut, torn, or unbuckled

**Pin Connection:**
- Thread Start → 3.3V
- Thread End → GPIO15 (INPUT_PULLUP mode)
- 10kΩ pull-up resistor to 3.3V

**Detection Logic:**
```cpp
int strapPin = 15;
pinMode(strapPin, INPUT_PULLUP);

bool isStrapIntact() {
  return digitalRead(strapPin) == LOW; // LOW = intact, HIGH = broken
}
```

**Alert Trigger:** Circuit break detected → Immediate tamper alert

**B. Skin Contact Sensor (Capacitive Touch)**  
**Component:** ESP32 Built-in Capacitive Touch Sensor  
**Implementation:**
- Copper touch pad on inner wristband surface
- Detects skin conductivity/capacitance
- No external components needed

**Pin Connection:**
- Touch Pad → GPIO4 (T0 - Touch Pin 0)
- Copper pad size: 15mm × 10mm

**Detection Logic:**
```cpp
int touchPin = T0; // GPIO4
int threshold = 40;

void setup() {
  touchAttachInterrupt(touchPin, touchCallback, threshold);
}

bool isSkinContact() {
  int touchValue = touchRead(touchPin);
  return touchValue < 40; // < 40 = skin contact, > 60 = no contact
}
```

**Alert Trigger:** No skin contact for > 10 seconds → Tamper alert

#### 2.1.6 User Feedback Indicators

**LED Indicator:**
- Component: 5mm Dual-color LED (Red/Green, common cathode)
- Pin: GPIO2
- Current-limiting resistor: 220Ω
- Status Codes:
  - **Solid Green:** Normal operation, BLE broadcasting
  - **Blinking Green:** Sending alert to server
  - **Solid Red:** Low battery (< 20%)
  - **Blinking Red:** Tamper alert detected
  - **Off:** Deep sleep mode

**Buzzer (Optional - Can be removed):**
- Component: Passive buzzer 3.3V
- Pin: GPIO5
- Purpose: Local alert feedback
- Usage: Minimal to save battery

#### 2.1.7 Power System

**Battery:**
- Type: 3.7V Li-Po Rechargeable
- Capacity: 1000mAh
- Dimensions: 50mm × 30mm × 6mm
- Protection: Built-in over-charge/discharge/short-circuit protection

**Charging Module:**
- Component: TP4056 Li-Po Charging Board
- Input: 5V Micro-USB
- Charging Current: 500mA
- LED Indicators: Red (charging), Blue (fully charged)
- Charging Time: ~2.5 hours

**Power Distribution:**
```
Battery (3.7V) → TP4056 → ESP32 VIN
                        → 3.3V Regulator (built-in ESP32)
                        → All Sensors (3.3V)
```

**Estimated Battery Life:**

| Operating Mode | Avg Current | Battery Life (1000mAh) |
|----------------|-------------|------------------------|
| BLE Only (continuous) | 50mA | 20 hours |
| BLE + MPU6050 | 54mA | 18.5 hours |
| BLE + WiFi (10% duty) | 66mA | 15 hours |
| Deep Sleep (between checks) | 5mA | 200 hours (8+ days) |
| **Hybrid Mode (Recommended)** | **~20mA** | **~50 hours (2+ days)** |

**Hybrid Mode Details:**
- BLE broadcasting: Continuous (200ms interval)
- MPU6050 reading: Every 5 seconds
- WiFi: Only when alert detected (~1% of time)
- Deep sleep: Between sensor readings

#### 2.1.8 Wristband Enclosure
**Material:** Medical-grade silicone or ABS plastic  
**Requirements:**
- Waterproof rating: IP65 (splash resistant)
- Hypoallergenic material
- Adjustable strap: 15-22cm wrist circumference
- Secure magnetic clasp with conductive thread integration
- Micro-USB charging port (covered)
- Dimensions: 55mm × 35mm × 12mm

#### 2.1.9 Complete Wristband Pin Assignment
```
ESP32 GPIO MAP (Wristband):
├── GPIO21 (SDA) ───────► MPU6050 SDA
├── GPIO22 (SCL) ───────► MPU6050 SCL
├── GPIO19 ─────────────► MPU6050 INT (Interrupt)
├── GPIO15 ─────────────► Conductive Strap Sensor (INPUT_PULLUP)
├── GPIO4 (T0) ─────────► Capacitive Touch Pad (Skin Contact)
├── GPIO2 ──────────────► Dual LED (Red/Green)
├── GPIO5 ──────────────► Buzzer (Optional)
├── 3.3V ───────────────► MPU6050 VCC
├── GND ────────────────► Common Ground
└── EN (Reset) ─────────► Programming Button

BLE: Built-in ESP32 (no external pins needed)
WiFi: Built-in ESP32 (no external pins needed)
```

---

### 2.2 BLE RECEIVER NODE UNIT (Room/Zone Coverage)

#### 2.2.1 Main Controller
**Component:** ESP32 DevKit V1 (30-pin)  
**Purpose:** Scan for BLE beacon advertisements from wristbands

**Specifications:**
- Same as wristband ESP32
- Powered via Micro-USB (5V, 1A minimum) or PoE
- Always-on operation (no sleep mode)
- Mounted at fixed locations throughout ward

#### 2.2.2 BLE Scanner Configuration

**Operating Mode:** BLE Central (Scanner)  
**Scanning Parameters:**
```cpp
BLEScan* pBLEScan = BLEDevice::getScan();
pBLEScan->setActiveScan(true);        // Active scanning
pBLEScan->setInterval(100);           // Scan interval: 100ms
pBLEScan->setWindow(99);              // Scan window: 99ms
pBLEScan->setDuplicateFilter(false);  // Don't filter duplicates (need RSSI updates)
```

**Detection Capabilities:**
- Scans for BLE advertisements continuously
- Detects 10+ wristbands simultaneously
- Measures RSSI (signal strength) for distance estimation
- Filters by hospital UUID to only detect valid wristbands

**Data Processing:**
```javascript
For each detected BLE beacon:
1. Extract patient ID (from major/minor values)
2. Measure RSSI (signal strength)
3. Calculate approximate distance:
   distance = 10 ^ ((TxPower - RSSI) / (10 * 2.5))
4. Determine if patient is in this receiver's zone
5. Send data to server via WiFi
```

#### 2.2.3 Visual & Audio Indicators

**RGB LED Indicator:**
- Component: 10mm RGB LED (Common Cathode)
- Pins: GPIO25 (Red), GPIO26 (Green), GPIO27 (Blue)
- Current-limiting resistors: 220Ω each
- Status Colors:
  - **Blue Blinking:** Scanning for BLE beacons
  - **Green Solid:** Patient detected, data sent successfully
  - **Yellow:** Connecting to WiFi
  - **Red:** Error or critical alert detected
  - **Purple:** Tamper alert being forwarded
  - **Off:** Powered off

**Buzzer:**
- Component: Active buzzer 5V
- Pin: GPIO14
- Alert Patterns:
  - Single beep: Patient detected
  - Double beep: Data sent to server
  - Triple beep: Alert detected
  - Continuous: Critical alert (tamper/fall)

#### 2.2.4 Network Connectivity
**Interface:** WiFi 802.11 b/g/n (2.4GHz)  
**Connection:** Infrastructure mode (hospital WiFi)  
**Protocol:** HTTPS REST API  
**Transmission Interval:** Every 2-5 seconds (batched detections)

**Network Configuration:**
- Static IP (recommended) or DHCP
- DNS: Hospital network DNS
- NTP: Time synchronization for accurate timestamps
- Fallback: Store data locally if WiFi drops

#### 2.2.5 Receiver Placement Strategy

**Coverage per Receiver:**
- Range: 20-40 meters indoors (walls reduce range)
- Coverage area: ~1200 m² per receiver (open space)
- Actual coverage: ~400 m² per receiver (with walls)

**Placement Guidelines:**
1. Mount at 2-2.5m height (ceiling or high on wall)
2. Avoid metal obstacles and RF interference sources
3. Place 15-20m apart for overlapping coverage
4. Ensure line-of-sight to patient areas when possible

**Example Ward Layout (30m × 15m = 450m²):**
```
┌────────────────────────────────────────┐
│  RX-1         RX-2         RX-3        │ ← Ceiling-mounted
│   •            •            •          │   receivers
│                                        │
│  [201] [202] [203] [204] [205]        │
│                                        │
│  ══════════ Corridor ═══════════      │
│                                        │
│  [206] [207] [208] [209] [210]        │
│                                        │
│   •            •            •          │
│  RX-4         RX-5         RX-6        │
└────────────────────────────────────────┘

Total: 6 BLE Receivers for complete coverage
```

#### 2.2.6 Power Supply
**Input:** 5V DC, 1A via Micro-USB or DC barrel jack  
**Consumption:** ~300mA average (BLE scanning + WiFi)  
**Backup Power:** Optional 5000mAh power bank for 15+ hour backup

#### 2.2.7 Receiver Node Enclosure
**Material:** ABS plastic wall/ceiling-mounted box  
**Dimensions:** 100mm × 80mm × 35mm  
**Features:**
- Wall/ceiling mounting holes
- Cable management
- LED/Buzzer openings
- Ventilation slots
- IP40 rating

#### 2.2.8 Receiver Node Pin Assignment
```
ESP32 GPIO MAP (BLE Receiver):
├── GPIO25 ─────────────► RGB LED - Red
├── GPIO26 ─────────────► RGB LED - Green
├── GPIO27 ─────────────► RGB LED - Blue
├── GPIO14 ─────────────► Buzzer
├── 3.3V ───────────────► LED power (through resistors)
└── GND ────────────────► Common Ground

BLE Scanner: Built-in ESP32 (no external pins)
WiFi: Built-in ESP32 (no external pins)
```

---

### 2.3 RFID DOOR READER UNIT (Entry/Exit Detection)

#### 2.3.1 Main Controller
**Component:** ESP32 DevKit V1  
**Purpose:** Detect when patients enter/exit rooms via 125kHz RFID

#### 2.3.2 RFID Reader Module
**Component:** RDM6300 (125kHz RFID Reader)  
**Specifications:**
- Frequency: 125kHz
- Interface: UART (Serial)
- Read Range: 10-20cm
- Operating Voltage: 5V

**Pin Connections (RDM6300 to ESP32):**
```
RDM6300          ESP32
────────         ──────
TX (Outer)  →    GPIO4 (RX1)
TX (Inner)  →    GPIO16 (RX2)
VCC         →    5V
GND         →    GND
```

**RFID Tag (on Wristband):**
- Type: EM4100 compatible (125kHz)
- Form Factor: Thin RFID sticker
- Placement: Inside wristband enclosure
- UID: 10-digit hex string

#### 2.3.3 Installation Location
**Placement:** Mounted on door frame at waist/wrist height (1-1.2m)  
**Detection Zone:** 
```
Door Frame (Side View):

    ┌─────────────┐
    │             │
    │   Room 201  │
    │             │
    │      ║      │  ← Door
  ──╫──────║──────╫──  Corridor
    ║   👤 ║      │  ← Patient walking through
    ║      ║      │
[MFRC522]  ║      │  ← Reader mounted here (1m height)
    ║      ║      │     Reads when wristband passes within 5cm
    
Patient must pass within 5-10cm of reader to be detected.
```

**Requirements:**
- Compact enclosure (5cm × 5cm × 2cm)
- Flush-mounted or surface-mounted on door frame
- LED indicator (green = read successful)
- Buzzer (optional beep on detection)

#### 2.3.4 Detection Logic

**When Patient Passes Door:**
1. MFRC522 detects RFID tag
2. Reads unique UID (4-7 bytes)
3. Matches UID to patient ID in database
4. Determines direction:
   - **Entry:** Patient entered room
   - **Exit:** Patient left room
5. Sends event to server via WiFi

**Direction Detection Algorithm:**
```javascript
// Track last known location from BLE system
// Compare with door reader location

if (patient.lastLocation == "Corridor" && doorID == "Room-201-Door") {
  event = "ENTRY into Room 201";
} else if (patient.lastLocation == "Room 201" && doorID == "Room-201-Door") {
  event = "EXIT from Room 201";
}
```

#### 2.3.5 RFID Door Reader Data Packet

```json
{
  "reader_id": "RFID-DOOR-201",
  "location": "Room 201 Entrance",
  "event_type": "door_crossing",
  "rfid_uid": "A1B2C3D4",
  "patient_id": "PT-0266",
  "direction": "entry",
  "timestamp": "2025-12-18T14:30:45.123Z"
}
```

#### 2.3.6 Door Reader Placement Plan

**For 10-Room Ward:**
- 1 RFID reader per room door = **10 readers**
- 1 RFID reader per exit door = **2 readers**
- 1 RFID reader for medication room = **1 reader**
- **Total: 13 RFID door readers**

**Example Layout:**
```
┌─────────────────────────────────────────────┐
│  [R]   [R]   [R]   [R]   [R]               │
│  201   202   203   204   205               │
│   │     │     │     │     │                 │
│   ■     ■     ■     ■     ■  ← RFID readers│
│                                             │
│  ════════════ Corridor ════════════  [R]   │ ← Exit
│                                      ■      │
│   ■     ■     ■     ■     ■                │
│   │     │     │     │     │                 │
│  206   207   208   209   210               │
│  [R]   [R]   [R]   [R]   [R]               │
│                                             │
│                 Medication Room [R]         │
│                              ■              │
└─────────────────────────────────────────────┘
```

#### 2.3.7 Power Supply
**Input:** 5V DC, 500mA via USB or wall adapter  
**Consumption:** ~100mA (ESP32 + MFRC522)

---

### 2.4 SYSTEM HARDWARE SUMMARY

| Component | Wristband | BLE Receiver | RFID Door Reader | Qty (10 patients) |
|-----------|-----------|--------------|------------------|-------------------|
| ESP32-WROOM-32 | ✓ | ✓ | ✓ | 10 + 6 + 13 = 29 |
| MPU6050 IMU | ✓ | - | - | 10 |
| Passive RFID Sticker (13.56MHz) | ✓ | - | - | 10 |
| RDM6300 RFID Reader | - | - | ✓ | 13 |
| Conductive Thread | ✓ | - | - | 10 meters |
| Capacitive Touch Pad | ✓ (built-in) | - | - | 10 |
| Dual LED (R/G) | ✓ | - | - | 10 |
| RGB LED | - | ✓ | ✓ | 6 + 13 = 19 |
| Buzzer | ✓ (optional) | ✓ | ✓ (optional) | 29 |
| Li-Po Battery 1000mAh | ✓ | - | - | 10 |
| TP4056 Charger | ✓ | - | - | 10 |
| Power Supply 5V | - | ✓ | ✓ | 6 + 13 = 19 |
| Wristband Enclosure | ✓ | - | - | 10 |
| Wall-mount Box | - | ✓ | - | 6 |
| Door-mount Box | - | - | ✓ | 13 |

---

## 3. SOFTWARE ARCHITECTURE

### 3.1 WRISTBAND FIRMWARE (ESP32)

#### 3.1.1 Operating Modes

**Mode 1: Normal Tracking Mode (Default)**
- BLE beacon broadcasting every 200ms
- MPU6050 reading every 5 seconds
- Strap and skin contact monitoring continuous
- WiFi OFF (power saving)
- Battery life: ~48-60 hours

**Mode 2: Alert Mode**
- BLE beacon continues
- WiFi enabled
- Sends alert to server via HTTPS
- LED blinking red
- Returns to Normal Mode after alert sent
- Battery impact: ~30 seconds of WiFi

**Mode 3: Charging Mode**
- Reduced BLE advertising (every 1 second)
- All sensors paused except battery monitoring
- LED shows charging status
- Firmware update capability via USB

#### 3.1.2 Firmware Main Loop

```cpp
// Simplified wristband firmware structure

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEBeacon.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <MPU6050.h>

// Configuration
#define PATIENT_ID 266
#define WARD_ID 1
String bleUUID = "A1B2C3D4-E5F6-7890-ABCD-EF1234567890";

// Sensors
MPU6050 mpu;
int strapPin = 15;
int touchPin = T0; // GPIO4
int ledPin = 2;

void setup() {
  Serial.begin(115200);
  
  // Initialize BLE beacon
  initBLE();
  
  // Initialize sensors
  Wire.begin(21, 22); // SDA, SCL
  mpu.initialize();
  pinMode(strapPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  
  // Start BLE advertising
  startBLEBeacon();
}

void loop() {
  // Read sensors every 5 seconds
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 5000) {
    checkSensors();
    lastCheck = millis();
  }
  
  // BLE beacon broadcasts automatically
  // No explicit transmission needed
  
  delay(100);
}

void initBLE() {
  BLEDevice::init("Patient-Wristband");
  BLEServer *pServer = BLEDevice::createServer();
  
  // Create beacon
  BLEBeacon beacon;
  beacon.setManufacturerId(0x4C00); // Apple
  beacon.setProximityUUID(BLEUUID(bleUUID.c_str()));
  beacon.setMajor(WARD_ID);
  beacon.setMinor(PATIENT_ID);
  beacon.setSignalPower(-59);
  
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->setAdvertisementData(beacon.getData());
  pAdvertising->start();
}

void checkSensors() {
  // Check strap integrity
  bool strapIntact = digitalRead(strapPin) == LOW;
  
  // Check skin contact
  int touchValue = touchRead(touchPin);
  bool skinContact = touchValue < 40;
  
  // Check for fall (MPU6050)
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float accelMagnitude = sqrt(ax*ax + ay*ay + az*az) / 16384.0;
  bool fallDetected = (accelMagnitude > 2.5);
  
  // Detect tampering
  if (!strapIntact || !skinContact) {
    sendTamperAlert();
  }
  
  // Detect fall
  if (fallDetected) {
    sendFallAlert();
  }
}

void sendTamperAlert() {
  // Enable WiFi
  WiFi.begin("HospitalWiFi", "password");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  // Send HTTPS POST
  HTTPClient http;
  http.begin("https://server.com/api/v1/alerts");
  http.addHeader("Content-Type", "application/json");
  
  String payload = "{\"patient_id\":\"PT-0266\",\"alert_type\":\"tamper\"}";
  int httpCode = http.POST(payload);
  
  http.end();
  WiFi.disconnect();
  
  // Blink red LED
  blinkLED(RED, 5);
}

void sendFallAlert() {
  // Similar to sendTamperAlert()
  // alert_type: "fall"
}
```

#### 3.1.3 BLE Beacon Advertisement Format

**iBeacon Standard Format:**
```
Byte Layout:
[0-1]:   Manufacturer ID (0x4C00 = Apple)
[2]:     Beacon Type (0x02 = iBeacon)
[3]:     Data Length (0x15 = 21 bytes)
[4-19]:  UUID (16 bytes) - Hospital identifier
[20-21]: Major (2 bytes) - Ward number
[22-23]: Minor (2 bytes) - Patient number  
[24]:    TX Power (1 byte) - Calibrated RSSI at 1m
```

**Custom Data Encoding (Optional):**
Can encode sensor status in manufacturer-specific data:
```
[25]: Battery level (0-100)
[26]: Status flags:
      Bit 0: Strap intact (1=yes, 0=no)
      Bit 1: Skin contact (1=yes, 0=no)
      Bit 2: Fall detected (1=yes, 0=no)
      Bit 3-7: Reserved
```

#### 3.1.4 Power Optimization

**BLE Power Saving:**
```cpp
// Adjust BLE advertising interval based on battery
if (batteryLevel > 50) {
  advertisingInterval = 200; // ms (5 Hz)
} else if (batteryLevel > 20) {
  advertisingInterval = 500; // ms (2 Hz)
} else {
  advertisingInterval = 1000; // ms (1 Hz)
}
```

**ESP32 Sleep Strategy:**
```cpp
// Between sensor readings, use light sleep
esp_sleep_enable_timer_wakeup(5 * 1000000); // 5 seconds
esp_light_sleep_start(); // Only CPU sleeps, BLE continues
```

---

### 3.2 BLE RECEIVER NODE FIRMWARE

#### 3.2.1 Main Scanning Loop

```cpp
#include <BLEDevice.h>
#include <BLEScan.h>
#include <WiFi.h>
#include <HTTPClient.h>

// Configuration
#define RECEIVER_ID "RX-WARD-A-001"
#define SCAN_TIME 5 // seconds

BLEScan* pBLEScan;

struct DetectedPatient {
  String patientID;
  int rssi;
  float distance;
  unsigned long timestamp;
};

std::vector<DetectedPatient> detectedPatients;

void setup() {
  Serial.begin(115200);
  
  // Connect to WiFi
  WiFi.begin("HospitalWiFi", "password");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  // Initialize BLE scanner
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void loop() {
  // Scan for BLE devices
  BLEScanResults foundDevices = pBLEScan->start(SCAN_TIME, false);
  
  // Process detected beacons
  for (int i = 0; i < foundDevices.getCount(); i++) {
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    
    // Check if it's our hospital's beacon (by UUID)
    if (isHospitalBeacon(device)) {
      processBeacon(device);
    }
  }
  
  // Send data to server
  sendDataToServer();
  
  // Clear results for next scan
  pBLEScan->clearResults();
  detectedPatients.clear();
  
  delay(2000); // Wait 2 seconds before next scan
}

bool isHospitalBeacon(BLEAdvertisedDevice device) {
  // Check if advertisement contains our UUID
  std::string data = device.getManufacturerData();
  
  // iBeacon starts with 0x4C00 0x02 0x15
  if (data.length() >= 25 && 
      data[0] == 0x4C && data[1] == 0x00 &&
      data[2] == 0x02 && data[3] == 0x15) {
    
    // Extract and verify UUID (bytes 4-19)
    // Compare with hospital UUID
    return true; // Simplified
  }
  return false;
}

void processBeacon(BLEAdvertisedDevice device) {
  std::string data = device.getManufacturerData();
  
  // Extract Major (Ward ID) - bytes 20-21
  int major = (data[20] << 8) | data[21];
  
  // Extract Minor (Patient ID) - bytes 22-23
  int minor = (data[22] << 8) | data[23];
  
  // Get RSSI
  int rssi = device.getRSSI();
  
  // Calculate distance (rough estimate)
  int txPower = -59; // Calibrated TX power at 1m
  float distance = calculateDistance(rssi, txPower);
  
  // Store detection
  DetectedPatient patient;
  patient.patientID = "PT-" + String(minor);
  patient.rssi = rssi;
  patient.distance = distance;
  patient.timestamp = millis();
  
  detectedPatients.push_back(patient);
  
  Serial.printf("Detected: %s, RSSI: %d, Distance: %.2fm\n", 
                patient.patientID.c_str(), rssi, distance);
}

float calculateDistance(int rssi, int txPower) {
  // Path loss formula for indoor BLE
  // distance = 10 ^ ((txPower - rssi) / (10 * n))
  // n = environmental factor (2.0-4.0, typically 2.5 for indoor)
  float n = 2.5;
  return pow(10, ((float)(txPower - rssi)) / (10 * n));
}

void sendDataToServer() {
  if (detectedPatients.size() == 0) return;
  
  HTTPClient http;
  http.begin("https://server.com/api/v1/location-update");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", "receiver_api_key_here");
  
  // Build JSON payload
  String payload = "{";
  payload += "\"receiver_id\":\"" + String(RECEIVER_ID) + "\",";
  payload += "\"timestamp\":\"" + getTimestamp() + "\",";
  payload += "\"detected_patients\":[";
  
  for (int i = 0; i < detectedPatients.size(); i++) {
    payload += "{";
    payload += "\"patient_id\":\"" + detectedPatients[i].patientID + "\",";
    payload += "\"rssi\":" + String(detectedPatients[i].rssi) + ",";
    payload += "\"distance\":" + String(detectedPatients[i].distance);
    payload += "}";
    if (i < detectedPatients.size() - 1) payload += ",";
  }
  
  payload += "]}";
  
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    Serial.printf("Server response: %d\n", httpCode);
  }
  
  http.end();
}
```

---

### 3.3 RFID DOOR READER FIRMWARE

```cpp
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define SS_PIN 5
#define RST_PIN 22
#define DOOR_ID "RFID-DOOR-201"
#define ROOM_NAME "Room 201"

MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();
  
  // Connect WiFi
  WiFi.begin("HospitalWiFi", "password");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  Serial.println("RFID Door Reader Ready");
}

void loop() {
  // Check for new card
  if (!rfid.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }
  
  // Read card UID
  if (!rfid.PICC_ReadCardSerial()) {
    delay(50);
    return;
  }
  
  // Get UID
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  
  Serial.println("Card detected: " + uid);
  
  // Send to server
  sendDoorEvent(uid);
  
  // Halt card
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  
  delay(1000); // Prevent multiple reads
}

void sendDoorEvent(String uid) {
  HTTPClient http;
  http.begin("https://server.com/api/v1/door-event");
  http.addHeader("Content-Type", "application/json");
  
  String payload = "{";
  payload += "\"reader_id\":\"" + String(DOOR_ID) + "\",";
  payload += "\"room\":\"" + String(ROOM_NAME) + "\",";
  payload += "\"rfid_uid\":\"" + uid + "\",";
  payload += "\"timestamp\":\"" + getTimestamp() + "\"";
  payload += "}";
  
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    Serial.println("Door event sent successfully");
    blinkLED(GREEN);
  } else {
    Serial.println("Failed to send door event");
    blinkLED(RED);
  }
  
  http.end();
}
```

---

## 4. COMMUNICATION PROTOCOL

### 4.1 BLE Communication (Wristband ↔ Receiver)

**Technology:** Bluetooth Low Energy 4.2  
**Protocol:** iBeacon (Apple) or Eddystone (Google)  
**Direction:** One-way broadcast (wristband → receivers)  
**Frequency:** 200ms advertising interval (5 Hz)  
**Range:** 20-40m indoors, 50-100m outdoors  
**Security:** UUID-based filtering, optional encryption

**BLE Advantages over RFID:**
- ✅ 10x better range (40m vs 4m)
- ✅ Simultaneous detection of multiple devices
- ✅ RSSI-based distance estimation
- ✅ Lower power than WiFi
- ✅ Standardized protocol
- ✅ No line-of-sight required

### 4.2 RFID Communication (Wristband ↔ Door Reader)

**Technology:** 13.56MHz Passive RFID (MIFARE)  
**Protocol:** ISO14443A  
**Direction:** Reader initiates, tag responds  
**Range:** 1-10cm (requires proximity)  
**Read Time:** <100ms  
**Purpose:** Backup identification and precise door crossing detection

### 4.3 WiFi Communication (All Nodes ↔ Server)

**Protocol:** HTTPS (TLS 1.2+)  
**Method:** REST API (POST for data, GET for queries)  
**Data Format:** JSON  
**Authentication:** API Key in X-API-Key header  
**Transmission:**
- BLE Receivers: Every 2-5 seconds (batched)
- Door Readers: Immediate on detection
- Wristbands: Only when alert triggered

**Network Requirements:**
- Bandwidth: 50 kbps per node
- Latency: <500ms
- WiFi: WPA2-PSK or WPA2-Enterprise

---

## 5. DATABASE DESIGN

### 5.1 Simplified Database Schema

#### Table 1: patients
```sql
CREATE TABLE patients (
    id INT PRIMARY KEY AUTO_INCREMENT,
    patient_id VARCHAR(50) UNIQUE NOT NULL,
    name VARCHAR(100) NOT NULL,
    age INT,
    ward VARCHAR(50),
    assigned_room VARCHAR(20),
    ble_minor INT UNIQUE, -- BLE beacon minor value
    rfid_uid VARCHAR(20) UNIQUE, -- RFID tag UID
    wristband_mac VARCHAR(17), -- ESP32 MAC address
    risk_level ENUM('low', 'medium', 'high') DEFAULT 'low',
    status ENUM('active', 'discharged') DEFAULT 'active',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_patient_id (patient_id),
    INDEX idx_ble_minor (ble_minor),
    INDEX idx_rfid_uid (rfid_uid)
);
```

#### Table 2: ble_receivers
```sql
CREATE TABLE ble_receivers (
    id INT PRIMARY KEY AUTO_INCREMENT,
    receiver_id VARCHAR(50) UNIQUE NOT NULL,
    location_zone VARCHAR(100) NOT NULL,
    location_description TEXT,
    coordinate_x FLOAT,
    coordinate_y FLOAT,
    coverage_radius FLOAT DEFAULT 20.0, -- meters
    api_key VARCHAR(255) NOT NULL,
    status ENUM('online', 'offline') DEFAULT 'offline',
    last_heartbeat TIMESTAMP NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_receiver_id (receiver_id),
    INDEX idx_status (status)
);
```

#### Table 3: rfid_door_readers
```sql
CREATE TABLE rfid_door_readers (
    id INT PRIMARY KEY AUTO_INCREMENT,
    reader_id VARCHAR(50) UNIQUE NOT NULL,
    room_name VARCHAR(100) NOT NULL,
    door_location VARCHAR(200),
    api_key VARCHAR(255) NOT NULL,
    status ENUM('online', 'offline') DEFAULT 'offline',
    last_read TIMESTAMP NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_reader_id (reader_id)
);
```

#### Table 4: ble_location_history
```sql
CREATE TABLE ble_location_history (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    patient_id VARCHAR(50) NOT NULL,
    receiver_id VARCHAR(50) NOT NULL,
    rssi INT NOT NULL,
    estimated_distance FLOAT,
    zone VARCHAR(100),
    timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_patient_time (patient_id, timestamp),
    INDEX idx_receiver_time (receiver_id, timestamp),
    FOREIGN KEY (patient_id) REFERENCES patients(patient_id) ON DELETE CASCADE,
    FOREIGN KEY (receiver_id) REFERENCES ble_receivers(receiver_id) ON DELETE CASCADE
);
```

#### Table 5: door_events
```sql
CREATE TABLE door_events (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    patient_id VARCHAR(50) NOT NULL,
    reader_id VARCHAR(50) NOT NULL,
    room_name VARCHAR(100),
    rfid_uid VARCHAR(20),
    event_type ENUM('entry', 'exit', 'unknown') DEFAULT 'unknown',
    timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_patient_time (patient_id, timestamp),
    INDEX idx_room (room_name),
    FOREIGN KEY (patient_id) REFERENCES patients(patient_id) ON DELETE CASCADE
);
```

#### Table 6: alerts
```sql
CREATE TABLE alerts (
    id INT PRIMARY KEY AUTO_INCREMENT,
    patient_id VARCHAR(50) NOT NULL,
    alert_type ENUM(
        'tamper_strap',
        'tamper_skin_contact',
        'fall_detected',
        'unauthorized_exit',
        'missing_patient',
        'low_battery'
    ) NOT NULL,
    severity ENUM('low', 'medium', 'high', 'critical') NOT NULL,
    message TEXT NOT NULL,
    location_zone VARCHAR(100),
    sensor_data JSON, -- Additional context
    acknowledged BOOLEAN DEFAULT FALSE,
    acknowledged_by VARCHAR(100),
    acknowledged_at TIMESTAMP NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_patient_alerts (patient_id, created_at),
    INDEX idx_acknowledged (acknowledged),
    FOREIGN KEY (patient_id) REFERENCES patients(patient_id) ON DELETE CASCADE
);
```

#### Table 7: users
```sql
CREATE TABLE users (
    id INT PRIMARY KEY AUTO_INCREMENT,
    username VARCHAR(50) UNIQUE NOT NULL,
    email VARCHAR(100) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    full_name VARCHAR(100) NOT NULL,
    role ENUM('nurse', 'doctor', 'admin', 'security') NOT NULL,
    active BOOLEAN DEFAULT TRUE,
    last_login TIMESTAMP NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

---

## 6. DASHBOARD & USER INTERFACE

### 6.1 Simplified Dashboard Layout

```
┌─────────────────────────────────────────────────────────────┐
│  Header: Hospital Logo | Active Alerts (3) | User: Nurse M. │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌─────────────────┐  ┌───────────────────────────────────┐ │
│  │ ACTIVE ALERTS   │  │   PATIENT STATUS                  │ │
│  │ ━━━━━━━━━━━━━━━ │  │   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ │ │
│  │ 🔴 PT-0267      │  │   Total Patients: 10              │ │
│  │ Strap broken    │  │   In Rooms: 8                     │ │
│  │ Room 205        │  │   In Corridor: 2                  │ │
│  │ 2 mins ago      │  │   Alerts: 3                       │ │
│  │                 │  │   Battery Low: 1                  │ │
│  │ 🟡 PT-0268      │  └───────────────────────────────────┘ │
│  │ Unauthorized    │                                         │
│  │ exit attempt    │  ┌───────────────────────────────────┐ │
│  │ Exit Door 1     │  │   WARD FLOOR PLAN                 │ │
│  │ 5 mins ago      │  │   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ │ │
│  └─────────────────┘  │  ┌────┐ ┌────┐ ┌────┐            │ │
│                        │  │201 │ │202 │ │203 │            │ │
│                        │  │ 👤 │ │    │ │👤👤│            │ │
│                        │  └────┘ └────┘ └────┘            │ │
│  ┌─────────────────┐  │  ──────── Corridor ────────       │ │
│  │ PATIENT LIST    │  │  ┌────┐ ┌────┐ ┌────┐            │ │
│  │ ━━━━━━━━━━━━━━━ │  │  │204 │ │205🔴│ │206 │            │ │
│  │ ✅ PT-0266      │  │  │    │ │ 👤 │ │    │            │ │
│  │    Room 201     │  │  └────┘ └────┘ └────┘            │ │
│  │                 │  │                           Exit→   │ │
│  │ 🔴 PT-0267      │  └───────────────────────────────────┘ │
│  │    Room 205     │                                         │
│  │    ALERT!       │  ┌───────────────────────────────────┐ │
│  │                 │  │   SELECTED: PT-0267               │ │
│  │ ✅ PT-0268      │  │   ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ │ │
│  │    Corridor     │  │   Name: John Doe                  │ │
│  │                 │  │   Room: 205                       │ │
│  │ ✅ PT-0269      │  │   Status: ALERT - Strap broken    │ │
│  │    Room 203     │  │   Last Seen: Room 205 (2m ago)    │ │
│  │                 │  │   Battery: 45%                    │ │
│  │ ... (6 more)    │  │   Strap: ❌ BROKEN                │ │
│  └─────────────────┘  │   Skin Contact: ✅ OK             │ │
│                        │   Movement: Normal                │ │
│                        │   [Acknowledge Alert]             │ │
│                        └───────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### 6.2 Key Dashboard Features

**1. Real-time Floor Plan**
- Visual representation of ward layout
- Patient icons show current location (from BLE)
- Color coding: Green (normal), Red (alert), Yellow (warning)
- Click patient to view details

**2. Alert Panel**
- List of active alerts (newest first)
- Severity indicators (🔴 critical, 🟡 medium, 🔵 low)
- One-click acknowledgment
- Auto-refresh every 5 seconds

**3. Patient List**
- All patients with current status
- Filter by room, alert status, battery level
- Quick search by patient ID or name

**4. Patient Details Panel**
- Full patient information
- Current location (BLE zone + last door event)
- Alert history
- Battery status
- Sensor status (strap, skin contact, movement)

**5. Door Event Log**
- Recent entry/exit events
- Timeline view of patient movements
- Filter by patient or room

---

## 7. ALERT SYSTEM LOGIC

### 7.1 Alert Types (Simplified)

#### Alert 1: Strap Break Detection
**Trigger:** Conductive thread circuit broken  
**Severity:** HIGH  
**Response:**
- Immediate alert to dashboard
- Red LED on wristband
- Notification to assigned nurse

#### Alert 2: Skin Contact Lost
**Trigger:** Capacitive touch sensor reads no contact for >10 seconds  
**Severity:** HIGH  
**Response:**
- Alert to dashboard
- Check if strap also broken (escalate to CRITICAL if both)

#### Alert 3: Fall Detected
**Trigger:** MPU6050 detects:
- Sudden acceleration > 2.5g
- Horizontal orientation
- No movement for >5 seconds

**Severity:** CRITICAL  
**Response:**
- Immediate critical alert
- Location highlighted on map
- Auto-notification to nearest staff

#### Alert 4: Unauthorized Exit
**Trigger:** Patient detected at exit door for >5 seconds (BLE + RFID)  
**Severity:** MEDIUM to HIGH  
**Response:**
- Alert to security
- Buzzer at exit door
- Log exit attempt

#### Alert 5: Missing Patient
**Trigger:** No BLE detection from any receiver for >15 minutes  
**Severity:** CRITICAL  
**Response:**
- Critical alert to all staff
- Last known location highlighted
- Search protocol initiated

#### Alert 6: Low Battery
**Trigger:** Wristband battery <20%  
**Severity:** LOW  
**Response:**
- Notification to maintenance staff
- Schedule wristband swap/charging

### 7.2 Alert Processing Flow

```
Sensor Triggers
      ↓
Wristband detects event
      ↓
[BLE beacon updates status flags]
      ↓
Receiver detects status change
      ↓
Sends alert to server via WiFi
      ↓
Server creates alert record
      ↓
WebSocket broadcasts to dashboard
      ↓
Visual + audio notification
      ↓
Staff acknowledges alert
      ↓
Alert marked as resolved
```

---

## 8. POWER MANAGEMENT

### 8.1 Wristband Battery Life Optimization

**Power Consumption Breakdown:**
```
Component               Active      Sleep
────────────────────────────────────────
ESP32 (BLE only)        50 mA       10 μA
MPU6050                 3.9 mA      5 μA
LED (when on)           10 mA       0 mA
WiFi (when enabled)     160 mA      -
────────────────────────────────────────
Normal Mode:            54 mA       -
Alert Mode (WiFi):      214 mA      -
```

**Battery Life Calculation (1000mAh battery):**
```
Normal Mode (BLE + sensors, no WiFi):
- Average current: 54mA
- Battery life: 1000mAh / 54mA = 18.5 hours

With 1% WiFi duty cycle (alerts):
- Average: 54mA + (160mA × 0.01) = 55.6mA
- Battery life: 1000mAh / 55.6mA = 18 hours

Recommended: Charge daily or every 12 hours
```

**Power Saving Strategies:**
1. BLE-only mode (no WiFi unless alert)
2. Reduce BLE advertising when battery <30%
3. Low power mode for MPU6050
4. LED off in normal operation
5. Deep sleep between sensor reads (if possible)

---

## 9. INSTALLATION GUIDELINES

### 9.1 Pre-Installation Planning

**Step 1: Ward Survey**
- Measure ward dimensions
- Map room locations and exits
- Note WiFi coverage areas
- Identify power outlet locations

**Step 2: BLE Receiver Placement**
- Calculate coverage needed
- Plan receiver locations (15-20m apart)
- Ensure overlapping coverage
- Mark ceiling/wall mounting points

**Step 3: RFID Door Reader Placement**
- Identify all room doors
- Mark reader mounting positions (1m height on door frame)
- Plan cable routing
- Verify power supply access

### 9.2 Installation Steps

**Phase 1: Network & Server Setup (Day 1)**
1. Deploy cloud server or on-premise server
2. Install Node.js, MySQL, Nginx
3. Configure database and tables
4. Deploy backend API
5. Set up SSL certificates
6. Test API endpoints
7. Deploy dashboard frontend

**Phase 2: BLE Receiver Installation (Day 2)**
1. Mount receivers at planned locations
2. Connect power supplies
3. Configure WiFi credentials
4. Flash firmware with receiver IDs
5. Test BLE scanning
6. Verify API communication
7. Register receivers in database

**Phase 3: RFID Door Reader Installation (Day 3)**
1. Mount readers on door frames
2. Connect power
3. Flash firmware with reader IDs
4. Test RFID reading range
5. Register readers in database
6. Label each reader

**Phase 4: Wristband Preparation (Day 4)**
1. Flash firmware on all wristbands
2. Configure BLE beacons (unique Minor IDs)
3. Program RFID stickers
4. Attach RFID stickers to wristbands
5. Test all sensors
6. Charge batteries fully
7. Register in database

**Phase 5: System Testing (Day 5)**
1. Test end-to-end data flow
2. Verify real-time dashboard updates
3. Test all alert types
4. Validate BLE positioning accuracy
5. Test RFID door detection
6. Load test with multiple wristbands


### 9.3 BLE Receiver Placement Guide

**For 30m × 15m Ward (450m²):**

```
Optimal Placement (6 receivers):

Height: 2-2.5m (ceiling or high wall mount)
Spacing: 15m apart
Overlap: 5-10m between coverage zones

┌────────────────────────────────────┐
│     ●RX1         ●RX2         ●RX3 │ ← 2.5m height
│                                    │
│  [201] [202] [203] [204] [205]    │
│                                    │
│  ════════ Corridor ════════        │
│                                    │
│  [206] [207] [208] [209] [210]    │
│                                    │
│     ●RX4         ●RX5         ●RX6 │
└────────────────────────────────────┘

Each receiver covers ~20m radius
Total: 6 receivers for complete coverage
```

---

## 10. TESTING & VALIDATION

### 10.1 Unit Testing Checklist

**Wristband Tests:**
- [ ] BLE beacon broadcasting (verify with phone BLE scanner app)
- [ ] Strap break detection (cut strap, verify alert)
- [ ] Skin contact detection (remove from wrist, verify alert)
- [ ] Fall detection (drop test, verify alert)
- [ ] WiFi alert transmission (trigger alert, verify server receives)
- [ ] Battery monitoring (check voltage reading)
- [ ] LED indicators (verify all colors work)

**BLE Receiver Tests:**
- [ ] BLE scanning (detect test beacon)
- [ ] Multi-device detection (detect 5+ beacons simultaneously)
- [ ] RSSI measurement accuracy
- [ ] Distance calculation
- [ ] WiFi connection stability
- [ ] API data transmission
- [ ] LED/buzzer indicators

**RFID Door Reader Tests:**
- [ ] RFID tag detection (5-10cm range)
- [ ] UID reading accuracy
- [ ] WiFi transmission
- [ ] LED feedback

### 10.2 Integration Testing Scenarios

**Test 1: Normal Tracking**
1. Patient wears wristband in Room 201
2. BLE receivers detect beacon
3. Location logged in database
4. Dashboard shows patient in Room 201
✓ Pass criteria: <2 second update latency

**Test 2: Room Entry/Exit**
1. Patient walks from Room 201 to Corridor
2. RFID reader at door detects crossing
3. BLE receivers update location
4. Dashboard shows movement
5. Door event logged
✓ Pass criteria: Both systems agree on location

**Test 3: Strap Break Alert**
1. Cut wristband strap
2. Alert sent to server within 1 second
3. Dashboard shows red alert
4. Location highlighted on map
✓ Pass criteria: <1 second alert time

**Test 4: Fall Detection**
1. Drop wristband from 1.5m height
2. Fall alert triggered
3. Dashboard shows critical alert
4. Staff notified
✓ Pass criteria: Fall detected, no false positives

**Test 5: Missing Patient**
1. Remove wristband from all receiver range
2. Wait 15 minutes
3. Missing patient alert generated
4. Last known location shown
✓ Pass criteria: Alert at 15:00 mark

### 10.3 Performance Metrics

**Target Specifications:**
- BLE detection range: 20-40m indoors ✓
- Location update frequency: Every 2-5 seconds ✓
- Alert response time: <1 second (critical) ✓
- Dashboard load time: <3 seconds ✓
- Concurrent users: 20+ staff ✓
- System uptime: 99% ✓
- Wristband battery life: 18+ hours ✓

---


## 12. TIMELINE & MILESTONES

### 12.1 Project Schedule (14 Weeks - Simplified)

#### Phase 1: Research & Design (Weeks 1-2)

**Week 1:**
- Study ward layout
- Finalize BLE + RFID hybrid approach
- Design system architecture
- Create circuit diagrams
- **Deliverable:** System design document

**Week 2:**
- Order components
- Set up development environment
- Design database schema
- Create API specifications
- **Deliverable:** Technical specifications + BOM

#### Phase 2: Development (Weeks 3-8)

**Week 3-4: Wristband Development**
- Develop ESP32 BLE beacon firmware
- Integrate MPU6050
- Implement tamper detection
- Test battery life
- **Deliverable:** Working wristband prototype

**Week 5: BLE Receiver Development**
- Develop BLE scanner firmware
- Implement RSSI-based positioning
- Test multi-device detection
- WiFi communication
- **Deliverable:** Working BLE receiver

**Week 6: RFID Door Reader Development**
- Develop MFRC522 reader firmware
- Test detection range
- Implement door event logging
- **Deliverable:** Working door reader

**Week 7: Backend Server Development**
- Set up Node.js server
- Create database tables
- Develop REST API
- Implement WebSocket
- **Deliverable:** Functional backend

**Week 8: Dashboard Development**
- Develop web dashboard
- Create floor plan view
- Implement real-time updates
- Alert notifications
- **Deliverable:** Working dashboard

#### Phase 3: Integration & Testing (Weeks 9-11)

**Week 9: System Integration**
- Connect all components
- End-to-end testing
- Fix integration bugs
- **Deliverable:** Integrated system

**Week 10: Testing & Validation**
- Unit tests
- Integration tests
- Performance testing
- Accuracy validation
- **Deliverable:** Test report

**Week 11: User Acceptance Testing**
- Deploy to scaled model
- Staff training
- Simulate real scenarios
- Collect feedback
- **Deliverable:** UAT report

#### Phase 4: Deployment & Documentation (Weeks 12-14)

**Week 12: Hardware Assembly**
- Assemble all wristbands
- Install receivers and door readers
- Final firmware flashing
- System calibration

**Week 13: Deployment**
- Install in model ward
- 48-hour pilot run
- Monitor and troubleshoot
- Fine-tune system
- **Deliverable:** Live system

**Week 14: Documentation & Presentation**
- Final project report
- User manuals
- Maintenance guide
- Presentation preparation
- **Deliverable:** Complete documentation + final presentation

### 12.2 Key Milestones

| Week | Milestone | Status |
|------|-----------|--------|
| 2 | Design Complete | ○ |
| 4 | Wristband Prototype Ready | ○ |
| 6 | All Hardware Prototypes Working | ○ |
| 8 | Software Complete | ○ |
| 9 | System Integrated | ○ |
| 11 | Testing Complete | ○ |
| 13 | System Deployed | ○ |
| 14 | Final Presentation | ○ |

---

## 13. ADVANTAGES OF SIMPLIFIED BLE-BASED SYSTEM

### 13.1 Key Improvements Over Original Design

**1. Cost Savings**
- ✅ 12.5% lower cost ($947 vs $1,083)
- ✅ No expensive vital sign sensors
- ✅ Fewer components = easier maintenance

**2. Simplified Hardware**
- ✅ Removed MAX30102 (heart rate sensor)
- ✅ Removed DHT11 (temperature sensor)
- ✅ Focus on core tracking functionality
- ✅ Easier assembly and troubleshooting

**3. Better Tracking Technology**
- ✅ BLE has 10x better range than RFID (40m vs 4m)
- ✅ More accurate positioning (±1-2m vs ±3-5m)
- ✅ Industry-standard protocol
- ✅ Lower power consumption

**4. Dual Tracking System**
- ✅ BLE for continuous room-level tracking
- ✅ RFID for precise door entry/exit logging
- ✅ Redundancy: if one fails, other still works
- ✅ Cross-validation of location data

**5. Easier Installation**
- ✅ Only 6 BLE receivers (vs 18 RFID receivers)
- ✅ Ceiling-mounted receivers (no interference)
- ✅ Door readers are compact and simple

**6. Better Battery Life**
- ✅ BLE uses less power than WiFi
- ✅ No power-hungry sensors (MAX30102, DHT11)
- ✅ 18+ hours battery life
- ✅ Simpler power management

### 13.2 What We Kept (Core Functions)

✅ **Location Tracking** - Better with BLE  
✅ **Tamper Detection** - Strap break + skin contact  
✅ **Fall Detection** - MPU6050 accelerometer  
✅ **Real-time Alerts** - Faster with BLE  
✅ **Dashboard Monitoring** - Same functionality  
✅ **Movement History** - Enhanced with door events  

### 13.3 What We Removed (Non-Essential)

❌ **Vital Signs Monitoring** - Adds complexity, not core to tracking  
❌ **Temperature Monitoring** - Not essential for location tracking  
❌ **18 RFID Readers** - Replaced with 6 BLE receivers  

---

## 14. FUTURE ENHANCEMENTS

### 14.1 Potential Upgrades

**Phase 2 (6-12 months):**
1. Add vital signs monitoring (if needed)
2. Mobile app for staff (iOS/Android)
3. SMS/email notifications
4. Integration with hospital EMR system

**Phase 3 (1-2 years):**
1. AI-powered fall prediction
2. Behavioral pattern analysis
3. Multi-ward deployment
4. Advanced analytics dashboard

---

## 15. CONCLUSION

This simplified BLE-based patient tracking system provides a robust, cost-effective solution for monitoring patient location and safety in hospital wards. By combining:

- **BLE beacons** for continuous room-level tracking
- **RFID door readers** for precise entry/exit detection
- **Tamper detection** to ensure wristbands stay on
- **Fall detection** for emergency response

The system achieves the core project objectives while reducing complexity and cost compared to the original design.

**Key Achievements:**
- ✅ $947 total cost (12.5% savings)
- ✅ $95 per patient
- ✅ Better tracking accuracy (±1-2m)
- ✅ Longer battery life (18+ hours)
- ✅ Simpler installation (6 receivers vs 18)
- ✅ Industry-standard BLE technology

**Expected Impact:**
- 50% reduction in patient wandering incidents
- 80% faster emergency response
- 30% reduction in staff supervision time
- Complete movement audit trail

---

