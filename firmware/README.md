# Halo Watch Wristband Firmware

This directory contains the production-ready ESP32 firmware for the Halo Watch patient monitoring wristband device.

## Overview

The wristband firmware implements comprehensive patient monitoring capabilities including:

- **Sensor Integration**: MPU6050 (accelerometer/gyroscope), MAX30102 (heart rate/SpO2), DHT11 (temperature/humidity)
- **Tamper Detection**: Conductive strap monitoring and capacitive skin contact detection
- **Power Management**: Multiple operating modes for optimal battery life
- **WiFi Communication**: Secure HTTPS data transmission and OTA firmware updates
- **Alert Detection**: Advanced algorithms for fall detection, abnormal vitals, and emergency situations

## File Structure

### Main Firmware
- `wristband_production_firmware.cpp` - Main production firmware with all features
- `wristband_firmware_example.cpp` - Simple example firmware (legacy)

### Sensor Libraries
- `MPU6050.h/cpp` - MPU6050 accelerometer/gyroscope driver
- `MAX30105.h/cpp` - MAX30102 heart rate and SpO2 sensor driver
- `DHT.h/cpp` - DHT11 temperature and humidity sensor driver

### Algorithm Libraries
- `heartRate.h/cpp` - Heart rate detection algorithms
- `spo2_algorithm.h/cpp` - SpO2 calculation algorithms

### Testing Framework
- `test_wristband_firmware.cpp` - Comprehensive test suite
- `unity.h/cpp` - Simple Unity testing framework for Arduino

## Hardware Requirements

### ESP32 Development Board
- ESP32-WROOM-32 or compatible
- Minimum 4MB flash memory
- WiFi capability

### Sensors
- **MPU6050**: 6-axis IMU (I2C address 0x68)
- **MAX30102**: Heart rate and SpO2 sensor (I2C address 0x57)
- **DHT11**: Temperature and humidity sensor (digital pin)

### Additional Components
- 3.7V Li-Po battery (500-1000mAh recommended)
- TP4056 charging module
- Conductive thread for strap tamper detection
- Touch sensor for skin contact detection
- RGB LED for status indication
- Buzzer for audio alerts

## Pin Configuration

```cpp
// I2C Bus
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// Sensors
#define DHT_PIN 4
#define STRAP_SENSOR_PIN 15
#define SKIN_CONTACT_PIN 13
#define BATTERY_ADC_PIN 35
#define CHARGING_DETECT_PIN 12

// Indicators
#define LED_PIN 2
#define BUZZER_PIN 5
```

## Operating Modes

The firmware supports four operating modes for optimal power management:

1. **RFID_ONLY**: Deep sleep mode, RFID broadcast only (3-5 days battery life)
2. **ACTIVE_MONITOR**: Full sensor monitoring with WiFi (8-12 hours battery life)
3. **HYBRID**: Periodic sensor checks with reduced WiFi (1-2 days battery life)
4. **CHARGING**: Full functionality while charging, OTA updates enabled

## Configuration

### WiFi Settings
```cpp
const char* WIFI_SSID = "HOSPITAL_WIFI";
const char* WIFI_PASSWORD = "HOSPITAL_PASSWORD";
```

### Backend Configuration
```cpp
const char* BACKEND_BASE_URL = "https://halo-watch-backend.hospital.local";
```

### Device Identification
```cpp
String DEVICE_ID = "WB-ESP32-PROD-001";  // Auto-generated from MAC if default
String PATIENT_ID = "P001";              // Assigned during patient registration
String RFID_TAG = "A1B2C3D4E5";         // Unique RFID identifier
```

## Alert Thresholds

The firmware includes configurable thresholds for various alerts:

```cpp
// Fall Detection
#define FALL_THRESHOLD_G 2.5

// Vital Signs
#define HEART_RATE_MIN 50
#define HEART_RATE_MAX 120
#define SPO2_MIN 90
#define TEMP_MAX 37.8

// Battery
#define BATTERY_LOW_THRESHOLD 20
#define BATTERY_CRITICAL_THRESHOLD 10
```

## Data Transmission

The firmware transmits data to the backend in JSON format:

```json
{
  "device_id": "WB-ESP32-PROD-001",
  "patient_id": "P001",
  "rfid_tag": "A1B2C3D4E5",
  "timestamp": 1234567890,
  "battery_level": 85,
  "operating_mode": 1,
  "sensors": {
    "mpu6050": {
      "accel_x": 0.1, "accel_y": 0.2, "accel_z": 9.8,
      "gyro_x": 0.0, "gyro_y": 0.0, "gyro_z": 0.0,
      "fall_detected": false
    },
    "max30102": {
      "heart_rate": 75,
      "spo2": 98,
      "signal_quality": "good"
    },
    "dht11": {
      "temperature": 36.5,
      "humidity": 45.0
    },
    "tamper": {
      "strap_intact": true,
      "skin_contact": true,
      "tamper_alert": false
    }
  },
  "alerts": {
    "fall": false,
    "tamper": false,
    "abnormal_vitals": false,
    "low_battery": false,
    "critical_battery": false,
    "sensor_failure": false
  }
}
```

## Installation and Setup

### 1. Hardware Assembly
1. Connect sensors according to pin configuration
2. Install battery and charging circuit
3. Assemble in waterproof enclosure
4. Attach conductive strap with tamper detection

### 2. Firmware Upload
1. Install Arduino IDE with ESP32 board support
2. Install required libraries:
   - ArduinoJson
   - WiFiClientSecure
   - Wire (built-in)
3. Configure WiFi and backend settings
4. Upload firmware to ESP32

### 3. Device Registration
1. Power on device
2. Device will auto-generate ID from MAC address
3. Register device with backend system
4. Assign to patient and configure restrictions

## Testing

The firmware includes a comprehensive test suite (`test_wristband_firmware.cpp`) that covers:

- Sensor initialization and data reading
- Alert detection algorithms
- Power management functions
- WiFi communication simulation
- Integration testing
- System recovery procedures

### Running Tests
1. Upload test firmware to ESP32
2. Open Serial Monitor (115200 baud)
3. Tests will run automatically on startup
4. Review test results and debug any failures

### Mock Data Testing
The test suite uses mock sensor data to validate algorithms without requiring actual sensors:

```cpp
MockSensorData normalData = {
  0.1, 0.2, 9.8,  // Normal acceleration
  0.0, 0.0, 0.0,  // No rotation
  75,             // Normal heart rate
  98,             // Normal SpO2
  36.5,           // Normal temperature
  45.0,           // Normal humidity
  true,           // Strap intact
  true,           // Skin contact
  85              // Good battery level
};
```

## Security Features

- **HTTPS Communication**: All data transmission uses TLS encryption
- **Device Authentication**: Unique device certificates for backend communication
- **OTA Security**: Firmware updates only accepted from authenticated backend
- **Data Validation**: Input sanitization and bounds checking

## Power Optimization

The firmware implements several power-saving techniques:

- **Dynamic Mode Switching**: Automatically adjusts operating mode based on battery level
- **Deep Sleep**: ESP32 enters deep sleep between sensor readings in RFID_ONLY mode
- **WiFi Power Management**: Reduces WiFi power consumption in HYBRID mode
- **Sensor Gating**: Disables unused sensors in low-power modes

## Troubleshooting

### Common Issues

1. **Sensor Initialization Failure**
   - Check I2C connections
   - Verify sensor addresses
   - Ensure adequate power supply

2. **WiFi Connection Problems**
   - Verify SSID and password
   - Check signal strength
   - Review network security settings

3. **Battery Drain**
   - Check for sensor failures
   - Verify sleep mode operation
   - Monitor for excessive WiFi activity

4. **False Alerts**
   - Adjust alert thresholds
   - Improve sensor mounting
   - Check for interference

### Debug Output

Enable debug output by setting:
```cpp
#define DEBUG_MODE 1
```

This will provide detailed logging of:
- Sensor readings
- Alert triggers
- WiFi status
- Power management decisions
- Error conditions

## Maintenance

### Regular Maintenance
- Monitor battery health and replacement cycles
- Update firmware when new versions are available
- Clean sensors and contacts regularly
- Check strap integrity and tamper detection

### Firmware Updates
The device supports over-the-air (OTA) firmware updates when in CHARGING mode:
1. Place device on charger
2. Backend will automatically push updates
3. Device will download and install new firmware
4. Automatic reboot with new version

## Compliance and Safety

This firmware is designed to meet:
- Medical device safety standards
- Hospital electromagnetic compatibility requirements
- Patient data privacy regulations
- Battery safety and charging standards

## Support

For technical support or questions about the firmware:
1. Review this documentation
2. Check the test suite for examples
3. Examine debug output for error details
4. Contact the development team with specific issues

## Version History

- **v1.0**: Initial production release with all core features
- **v1.1**: Enhanced power management and alert algorithms (planned)
- **v1.2**: Additional sensor support and improved OTA (planned)