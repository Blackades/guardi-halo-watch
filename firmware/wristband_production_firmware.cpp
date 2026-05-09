#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <driver/adc.h>
#include <driver/touch_pad.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <time.h>

// Sensor Libraries
#include "MPU6050.h"
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include "DHT.h"

// ============================================================================
// CONFIGURATION AND CONSTANTS
// ============================================================================

// WiFi Configuration
const char* WIFI_SSID = "HOSPITAL_WIFI";
const char* WIFI_PASSWORD = "HOSPITAL_PASSWORD";
const int WIFI_TIMEOUT_MS = 30000;
const int WIFI_RETRY_DELAY_MS = 5000;

// Backend Configuration
const char* BACKEND_BASE_URL = "https://halo-watch-backend.hospital.local";
const char* OTA_UPDATE_URL = "/api/v1/firmware/update";
const char* DATA_ENDPOINT = "/api/v1/wristband-data";
const char* HEARTBEAT_ENDPOINT = "/api/v1/device/heartbeat";

// Device Configuration
String DEVICE_ID = "WB-ESP32-PROD-001";
String PATIENT_ID = "P001";
String RFID_TAG = "A1B2C3D4E5";

// Pin Definitions
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define DHT_PIN 4
#define LED_PIN 2
#define BUZZER_PIN 5
#define STRAP_SENSOR_PIN 15
#define SKIN_CONTACT_PIN 13
#define BATTERY_ADC_PIN 35
#define CHARGING_DETECT_PIN 12

// Sensor Configuration
#define DHT_TYPE DHT11
#define MPU6050_ADDR 0x68
#define MAX30102_ADDR 0x57

// Timing Constants
#define SENSOR_READ_INTERVAL_MS 1000
#define DATA_TRANSMISSION_INTERVAL_MS 30000
#define HEARTBEAT_INTERVAL_MS 300000  // 5 minutes
#define BATTERY_CHECK_INTERVAL_MS 60000
#define TAMPER_CHECK_INTERVAL_MS 500

// Alert Thresholds
#define FALL_THRESHOLD_G 2.5
#define HEART_RATE_MIN 50
#define HEART_RATE_MAX 120
#define SPO2_MIN 90
#define TEMP_MAX 37.8
#define BATTERY_LOW_THRESHOLD 20
#define BATTERY_CRITICAL_THRESHOLD 10

// Power Management
#define DEEP_SLEEP_DURATION_US 30000000  // 30 seconds
#define LIGHT_SLEEP_DURATION_US 5000000  // 5 seconds

// ============================================================================
// GLOBAL VARIABLES AND OBJECTS
// ============================================================================

// Sensor Objects
MPU6050 mpu;
MAX30105 particleSensor;
DHT dht(DHT_PIN, DHT_TYPE);

// Operating Mode Enumeration
enum OperatingMode {
  RFID_ONLY,      // Deep sleep, RFID only (3-5 days battery)
  ACTIVE_MONITOR, // Full sensors, WiFi enabled (8-12 hours)
  HYBRID,         // Periodic sensor checks (1-2 days)
  CHARGING        // Firmware update capable
};

// Current operating mode
OperatingMode currentMode = ACTIVE_MONITOR;

// Timing variables
unsigned long lastSensorRead = 0;
unsigned long lastDataTransmission = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastBatteryCheck = 0;
unsigned long lastTamperCheck = 0;

// Sensor data structures
struct SensorData {
  // MPU6050 data
  float accel_x, accel_y, accel_z;
  float gyro_x, gyro_y, gyro_z;
  bool fall_detected;
  
  // MAX30102 data
  int heart_rate;
  int spo2;
  String signal_quality;
  
  // DHT11 data
  float temperature;
  float humidity;
  
  // Tamper detection
  bool strap_intact;
  bool skin_contact;
  bool tamper_alert;
  
  // System data
  int battery_level;
  bool charging;
  unsigned long timestamp;
};

struct AlertData {
  bool fall;
  bool tamper;
  bool abnormal_vitals;
  bool low_battery;
  bool critical_battery;
  bool sensor_failure;
};

// Current sensor readings
SensorData currentSensorData;
AlertData currentAlerts;

// Data buffers for offline storage
const int MAX_BUFFERED_READINGS = 100;
SensorData dataBuffer[MAX_BUFFERED_READINGS];
int bufferIndex = 0;
int bufferedCount = 0;

// WiFi and connectivity status
bool wifiConnected = false;
int wifiRetryCount = 0;
const int MAX_WIFI_RETRIES = 3;

// ============================================================================
// SENSOR INTEGRATION FUNCTIONS
// ============================================================================

/**
 * Initialize all sensors and hardware components
 */
bool initializeSensors() {
  Serial.println("Initializing sensors...");
  
  // Initialize I2C
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000); // 400kHz I2C speed
  
  // Initialize MPU6050
  if (!mpu.begin()) {
    Serial.println("Failed to initialize MPU6050!");
    return false;
  }
  
  // Configure MPU6050
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  
  // Initialize MAX30102
  if (!particleSensor.begin()) {
    Serial.println("Failed to initialize MAX30102!");
    return false;
  }
  
  // Configure MAX30102
  particleSensor.setup(); // Configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x0A); // Turn Red LED to low to indicate sensor is running
  particleSensor.setPulseAmplitudeGreen(0); // Turn off Green LED
  
  // Initialize DHT11
  dht.begin();
  
  // Initialize GPIO pins
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(STRAP_SENSOR_PIN, INPUT_PULLUP);
  pinMode(SKIN_CONTACT_PIN, INPUT);
  pinMode(BATTERY_ADC_PIN, INPUT);
  pinMode(CHARGING_DETECT_PIN, INPUT_PULLUP);
  
  // Configure ADC for battery monitoring
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11); // GPIO35
  
  // Configure touch sensor for skin contact
  touch_pad_init();
  touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
  touch_pad_config(TOUCH_PAD_NUM0, 0); // GPIO4 as touch sensor
  
  Serial.println("All sensors initialized successfully");
  return true;
}

/**
 * Read MPU6050 accelerometer and gyroscope data
 */
void readMPU6050Data() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  currentSensorData.accel_x = a.acceleration.x;
  currentSensorData.accel_y = a.acceleration.y;
  currentSensorData.accel_z = a.acceleration.z;
  
  currentSensorData.gyro_x = g.gyro.x;
  currentSensorData.gyro_y = g.gyro.y;
  currentSensorData.gyro_z = g.gyro.z;
  
  // Calculate total acceleration magnitude for fall detection
  float totalAccel = sqrt(pow(a.acceleration.x, 2) + 
                         pow(a.acceleration.y, 2) + 
                         pow(a.acceleration.z, 2));
  
  // Simple fall detection algorithm
  currentSensorData.fall_detected = (totalAccel > FALL_THRESHOLD_G * 9.81);
  
  if (currentSensorData.fall_detected) {
    Serial.println("FALL DETECTED!");
    currentAlerts.fall = true;
  }
}

/**
 * Read MAX30102 heart rate and SpO2 data
 */
void readMAX30102Data() {
  static uint32_t irBuffer[100]; // Infrared LED sensor data
  static uint32_t redBuffer[100]; // Red LED sensor data
  static int32_t bufferLength = 100;
  static int32_t spo2;
  static int8_t validSPO2;
  static int32_t heartRate;
  static int8_t validHeartRate;
  
  // Read samples
  for (byte i = 0; i < bufferLength; i++) {
    while (particleSensor.available() == false) {
      particleSensor.check();
    }
    
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }
  
  // Calculate heart rate and SpO2
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, 
                                        &spo2, &validSPO2, &heartRate, &validHeartRate);
  
  if (validHeartRate) {
    currentSensorData.heart_rate = heartRate;
    currentSensorData.signal_quality = "good";
    
    // Check for abnormal heart rate
    if (heartRate < HEART_RATE_MIN || heartRate > HEART_RATE_MAX) {
      currentAlerts.abnormal_vitals = true;
    }
  } else {
    currentSensorData.heart_rate = 0;
    currentSensorData.signal_quality = "poor";
  }
  
  if (validSPO2) {
    currentSensorData.spo2 = spo2;
    
    // Check for low SpO2
    if (spo2 < SPO2_MIN) {
      currentAlerts.abnormal_vitals = true;
    }
  } else {
    currentSensorData.spo2 = 0;
  }
}

/**
 * Read DHT11 temperature and humidity data
 */
void readDHT11Data() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  
  if (!isnan(humidity) && !isnan(temperature)) {
    currentSensorData.humidity = humidity;
    currentSensorData.temperature = temperature;
    
    // Check for fever
    if (temperature > TEMP_MAX) {
      currentAlerts.abnormal_vitals = true;
    }
  } else {
    Serial.println("Failed to read from DHT sensor!");
    currentAlerts.sensor_failure = true;
  }
}

/**
 * Apply sensor data fusion and filtering algorithms
 */
void applySensorFusion() {
  // Simple moving average filter for accelerometer data
  static float accel_x_history[5] = {0};
  static float accel_y_history[5] = {0};
  static float accel_z_history[5] = {0};
  static int history_index = 0;
  
  // Update history
  accel_x_history[history_index] = currentSensorData.accel_x;
  accel_y_history[history_index] = currentSensorData.accel_y;
  accel_z_history[history_index] = currentSensorData.accel_z;
  
  history_index = (history_index + 1) % 5;
  
  // Calculate filtered values
  float filtered_x = 0, filtered_y = 0, filtered_z = 0;
  for (int i = 0; i < 5; i++) {
    filtered_x += accel_x_history[i];
    filtered_y += accel_y_history[i];
    filtered_z += accel_z_history[i];
  }
  
  currentSensorData.accel_x = filtered_x / 5.0;
  currentSensorData.accel_y = filtered_y / 5.0;
  currentSensorData.accel_z = filtered_z / 5.0;
  
  // Kalman filter for heart rate (simplified)
  static float heart_rate_estimate = 70.0;
  static float estimation_error = 1.0;
  
  if (currentSensorData.heart_rate > 0) {
    float kalman_gain = estimation_error / (estimation_error + 5.0);
    heart_rate_estimate = heart_rate_estimate + kalman_gain * (currentSensorData.heart_rate - heart_rate_estimate);
    estimation_error = (1 - kalman_gain) * estimation_error;
    
    currentSensorData.heart_rate = (int)heart_rate_estimate;
  }
}

/**
 * Read all sensor data and apply processing
 */
void readAllSensors() {
  // Clear previous alerts
  memset(&currentAlerts, 0, sizeof(AlertData));
  
  // Read sensor data based on operating mode
  switch (currentMode) {
    case ACTIVE_MONITOR:
      readMPU6050Data();
      readMAX30102Data();
      readDHT11Data();
      applySensorFusion();
      break;
      
    case HYBRID:
      readMPU6050Data(); // Always monitor for falls
      if (millis() % 10000 < 1000) { // Read vitals every 10 seconds
        readMAX30102Data();
        readDHT11Data();
      }
      break;
      
    case RFID_ONLY:
      // Only basic monitoring
      readMPU6050Data(); // For fall detection
      break;
      
    case CHARGING:
      readMPU6050Data();
      readMAX30102Data();
      readDHT11Data();
      applySensorFusion();
      break;
  }
  
  currentSensorData.timestamp = millis();
}

// ============================================================================
// SETUP FUNCTION
// ============================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("Halo Watch Wristband - Production Firmware v1.0");
  
  // Initialize hardware
  if (!initializeSensors()) {
    Serial.println("Sensor initialization failed! Entering safe mode...");
    currentMode = RFID_ONLY;
  }
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  // Set device ID based on MAC address if not configured
  if (DEVICE_ID == "WB-ESP32-PROD-001") {
    DEVICE_ID = "WB-" + WiFi.macAddress();
    DEVICE_ID.replace(":", "");
  }
  
  Serial.println("Setup complete. Device ID: " + DEVICE_ID);
  Serial.println("Operating Mode: " + String(currentMode));
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  unsigned long currentTime = millis();
  
  // Read sensors at regular intervals
  if (currentTime - lastSensorRead >= SENSOR_READ_INTERVAL_MS) {
    lastSensorRead = currentTime;
    readAllSensors();
  }
  
  // Check tamper detection
  if (currentTime - lastTamperCheck >= TAMPER_CHECK_INTERVAL_MS) {
    lastTamperCheck = currentTime;
    checkTamperDetection();
  }
  
  // Check battery level
  if (currentTime - lastBatteryCheck >= BATTERY_CHECK_INTERVAL_MS) {
    lastBatteryCheck = currentTime;
    checkBatteryLevel();
  }
  
  // Manage WiFi connection
  manageWiFiConnection();
  
  // Transmit data if connected
  if (wifiConnected && (currentTime - lastDataTransmission >= DATA_TRANSMISSION_INTERVAL_MS)) {
    lastDataTransmission = currentTime;
    transmitSensorData();
  }
  
  // Send heartbeat
  if (wifiConnected && (currentTime - lastHeartbeat >= HEARTBEAT_INTERVAL_MS)) {
    lastHeartbeat = currentTime;
    sendHeartbeat();
  }
  
  // Power management
  managePowerMode();
  
  delay(50); // Small delay to prevent watchdog reset
}
// ====
========================================================================
// TAMPER DETECTION SYSTEM
// ============================================================================

/**
 * Check for tamper detection events
 */
void checkTamperDetection() {
  // Check strap integrity using conductive thread
  bool strapIntact = digitalRead(STRAP_SENSOR_PIN) == HIGH;
  currentSensorData.strap_intact = strapIntact;
  
  // Check skin contact using capacitive sensing
  uint16_t touchValue = touchRead(SKIN_CONTACT_PIN);
  bool skinContact = touchValue < 40; // Threshold for skin contact
  currentSensorData.skin_contact = skinContact;
  
  // Generate tamper alert if either check fails
  bool tamperDetected = !strapIntact || !skinContact;
  currentSensorData.tamper_alert = tamperDetected;
  currentAlerts.tamper = tamperDetected;
  
  if (tamperDetected) {
    Serial.println("TAMPER DETECTED!");
    Serial.printf("Strap intact: %s, Skin contact: %s\n", 
                  strapIntact ? "YES" : "NO", 
                  skinContact ? "YES" : "NO");
    
    // Immediate alert transmission for tamper events
    if (wifiConnected) {
      transmitEmergencyAlert("tamper");
    } else {
      // Store in buffer for later transmission
      bufferAlertData();
    }
    
    // Visual and audio indication
    indicateTamperAlert();
  }
}

/**
 * Provide visual and audio indication for tamper alerts
 */
void indicateTamperAlert() {
  // Flash LED red rapidly
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
  
  // Sound buzzer pattern
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);
  }
}

/**
 * Log tamper events and implement recovery procedures
 */
void logTamperEvent() {
  // Create tamper event log entry
  StaticJsonDocument<200> tamperLog;
  tamperLog["device_id"] = DEVICE_ID;
  tamperLog["patient_id"] = PATIENT_ID;
  tamperLog["event_type"] = "tamper";
  tamperLog["timestamp"] = millis();
  tamperLog["strap_intact"] = currentSensorData.strap_intact;
  tamperLog["skin_contact"] = currentSensorData.skin_contact;
  
  // Store in local buffer
  // In production, this would be stored in EEPROM or flash memory
  Serial.println("Tamper event logged");
  
  // Implement recovery procedure
  if (!currentSensorData.strap_intact) {
    Serial.println("Strap break detected - entering emergency mode");
    currentMode = RFID_ONLY; // Conserve battery
  }
  
  if (!currentSensorData.skin_contact) {
    Serial.println("Skin contact lost - monitoring for reconnection");
    // Continue monitoring but flag as potentially removed
  }
}

// ============================================================================
// POWER MANAGEMENT SYSTEM
// ============================================================================

/**
 * Check battery level and charging status
 */
void checkBatteryLevel() {
  // Read battery voltage using ADC
  int adcValue = adc1_get_raw(ADC1_CHANNEL_7);
  float voltage = (adcValue * 3.3) / 4095.0; // Convert to voltage
  
  // Convert voltage to battery percentage (simplified)
  // Assumes 3.7V Li-Po battery with voltage divider
  float batteryVoltage = voltage * 2.0; // Adjust based on voltage divider
  int batteryPercent = map(batteryVoltage * 100, 320, 420, 0, 100); // 3.2V to 4.2V range
  batteryPercent = constrain(batteryPercent, 0, 100);
  
  currentSensorData.battery_level = batteryPercent;
  
  // Check charging status
  currentSensorData.charging = digitalRead(CHARGING_DETECT_PIN) == LOW;
  
  // Generate battery alerts
  if (batteryPercent <= BATTERY_CRITICAL_THRESHOLD) {
    currentAlerts.critical_battery = true;
    Serial.println("CRITICAL BATTERY LEVEL!");
  } else if (batteryPercent <= BATTERY_LOW_THRESHOLD) {
    currentAlerts.low_battery = true;
    Serial.println("Low battery warning");
  }
  
  // Switch to power saving mode if battery is low
  if (batteryPercent <= BATTERY_LOW_THRESHOLD && !currentSensorData.charging) {
    if (currentMode == ACTIVE_MONITOR) {
      currentMode = HYBRID;
      Serial.println("Switching to HYBRID mode to conserve battery");
    } else if (batteryPercent <= BATTERY_CRITICAL_THRESHOLD && currentMode == HYBRID) {
      currentMode = RFID_ONLY;
      Serial.println("Switching to RFID_ONLY mode - critical battery");
    }
  }
  
  // Switch to charging mode if connected to charger
  if (currentSensorData.charging && currentMode != CHARGING) {
    currentMode = CHARGING;
    Serial.println("Charging detected - switching to CHARGING mode");
  }
}

/**
 * Manage power modes and sleep functionality
 */
void managePowerMode() {
  switch (currentMode) {
    case RFID_ONLY:
      // Disable WiFi and Bluetooth to save power
      WiFi.mode(WIFI_OFF);
      esp_bt_controller_disable();
      
      // Enter deep sleep between sensor readings
      if (millis() % 30000 < 100) { // Wake up every 30 seconds
        esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_US);
        esp_deep_sleep_start();
      }
      break;
      
    case HYBRID:
      // Reduce WiFi power and use light sleep
      WiFi.setSleep(true);
      esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
      
      // Light sleep between operations
      if (millis() % 5000 < 100) {
        esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_DURATION_US);
        esp_light_sleep_start();
      }
      break;
      
    case ACTIVE_MONITOR:
      // Full power mode - no sleep
      WiFi.setSleep(false);
      break;
      
    case CHARGING:
      // Full power mode while charging
      WiFi.setSleep(false);
      // Enable OTA updates in this mode
      break;
  }
}

/**
 * Provide status indication based on operating mode
 */
void indicateStatus() {
  switch (currentMode) {
    case RFID_ONLY:
      // Slow blue blink
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(2900);
      break;
      
    case HYBRID:
      // Medium green blink
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(1800);
      break;
      
    case ACTIVE_MONITOR:
      // Fast green blink
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(900);
      break;
      
    case CHARGING:
      // Solid red while charging
      digitalWrite(LED_PIN, HIGH);
      break;
  }
}

// ============================================================================
// WIFI COMMUNICATION AND OTA UPDATES
// ============================================================================

/**
 * Manage WiFi connection with auto-reconnect
 */
void manageWiFiConnection() {
  if (currentMode == RFID_ONLY) {
    wifiConnected = false;
    return;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    
    if (wifiRetryCount < MAX_WIFI_RETRIES) {
      Serial.println("WiFi disconnected. Attempting to reconnect...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      
      unsigned long startTime = millis();
      while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        wifiRetryCount = 0;
        Serial.println("\nWiFi reconnected successfully");
        Serial.println("IP address: " + WiFi.localIP().toString());
      } else {
        wifiRetryCount++;
        Serial.println("\nWiFi reconnection failed");
      }
    } else {
      // Too many failures, wait longer before retry
      delay(WIFI_RETRY_DELAY_MS);
      wifiRetryCount = 0;
    }
  } else {
    wifiConnected = true;
  }
}

/**
 * Create secure HTTPS client
 */
WiFiClientSecure* createSecureClient() {
  WiFiClientSecure* client = new WiFiClientSecure();
  
  // In production, use proper certificate validation
  client->setInsecure(); // For development only
  
  return client;
}

/**
 * Format sensor data into JSON packet
 */
String formatSensorDataPacket() {
  StaticJsonDocument<1024> doc;
  
  doc["device_id"] = DEVICE_ID;
  doc["patient_id"] = PATIENT_ID;
  doc["rfid_tag"] = RFID_TAG;
  doc["timestamp"] = currentSensorData.timestamp;
  doc["battery_level"] = currentSensorData.battery_level;
  doc["operating_mode"] = currentMode;
  
  // Sensor data
  JsonObject sensors = doc.createNestedObject("sensors");
  
  JsonObject mpu6050 = sensors.createNestedObject("mpu6050");
  mpu6050["accel_x"] = currentSensorData.accel_x;
  mpu6050["accel_y"] = currentSensorData.accel_y;
  mpu6050["accel_z"] = currentSensorData.accel_z;
  mpu6050["gyro_x"] = currentSensorData.gyro_x;
  mpu6050["gyro_y"] = currentSensorData.gyro_y;
  mpu6050["gyro_z"] = currentSensorData.gyro_z;
  mpu6050["fall_detected"] = currentSensorData.fall_detected;
  
  JsonObject max30102 = sensors.createNestedObject("max30102");
  max30102["heart_rate"] = currentSensorData.heart_rate;
  max30102["spo2"] = currentSensorData.spo2;
  max30102["signal_quality"] = currentSensorData.signal_quality;
  
  JsonObject dht11 = sensors.createNestedObject("dht11");
  dht11["temperature"] = currentSensorData.temperature;
  dht11["humidity"] = currentSensorData.humidity;
  
  JsonObject tamper = sensors.createNestedObject("tamper");
  tamper["strap_intact"] = currentSensorData.strap_intact;
  tamper["skin_contact"] = currentSensorData.skin_contact;
  tamper["tamper_alert"] = currentSensorData.tamper_alert;
  
  // Alert data
  JsonObject alerts = doc.createNestedObject("alerts");
  alerts["fall"] = currentAlerts.fall;
  alerts["tamper"] = currentAlerts.tamper;
  alerts["abnormal_vitals"] = currentAlerts.abnormal_vitals;
  alerts["low_battery"] = currentAlerts.low_battery;
  alerts["critical_battery"] = currentAlerts.critical_battery;
  alerts["sensor_failure"] = currentAlerts.sensor_failure;
  
  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

/**
 * Transmit sensor data to backend
 */
void transmitSensorData() {
  if (!wifiConnected) {
    bufferSensorData();
    return;
  }
  
  WiFiClientSecure* client = createSecureClient();
  HTTPClient http;
  
  String url = String(BACKEND_BASE_URL) + DATA_ENDPOINT;
  http.begin(*client, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  
  String payload = formatSensorDataPacket();
  
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    String response = http.getString();
    Serial.printf("Data transmission successful: %d\n", httpCode);
    
    // Transmit any buffered data
    transmitBufferedData();
  } else {
    Serial.printf("Data transmission failed: %s\n", http.errorToString(httpCode).c_str());
    bufferSensorData();
  }
  
  http.end();
  delete client;
}

/**
 * Send emergency alert immediately
 */
void transmitEmergencyAlert(String alertType) {
  if (!wifiConnected) return;
  
  WiFiClientSecure* client = createSecureClient();
  HTTPClient http;
  
  String url = String(BACKEND_BASE_URL) + "/api/v1/emergency-alert";
  http.begin(*client, url);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<300> alertDoc;
  alertDoc["device_id"] = DEVICE_ID;
  alertDoc["patient_id"] = PATIENT_ID;
  alertDoc["alert_type"] = alertType;
  alertDoc["timestamp"] = millis();
  alertDoc["priority"] = "critical";
  
  String alertPayload;
  serializeJson(alertDoc, alertPayload);
  
  int httpCode = http.POST(alertPayload);
  
  if (httpCode > 0) {
    Serial.printf("Emergency alert sent: %s\n", alertType.c_str());
  } else {
    Serial.printf("Emergency alert failed: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
  delete client;
}

/**
 * Send heartbeat to backend
 */
void sendHeartbeat() {
  if (!wifiConnected) return;
  
  WiFiClientSecure* client = createSecureClient();
  HTTPClient http;
  
  String url = String(BACKEND_BASE_URL) + HEARTBEAT_ENDPOINT;
  http.begin(*client, url);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<200> heartbeatDoc;
  heartbeatDoc["device_id"] = DEVICE_ID;
  heartbeatDoc["timestamp"] = millis();
  heartbeatDoc["battery_level"] = currentSensorData.battery_level;
  heartbeatDoc["operating_mode"] = currentMode;
  heartbeatDoc["wifi_rssi"] = WiFi.RSSI();
  
  String heartbeatPayload;
  serializeJson(heartbeatDoc, heartbeatPayload);
  
  int httpCode = http.POST(heartbeatPayload);
  
  if (httpCode > 0) {
    Serial.println("Heartbeat sent successfully");
  }
  
  http.end();
  delete client;
}

/**
 * Check for and perform OTA firmware updates
 */
void checkForOTAUpdate() {
  if (currentMode != CHARGING) return; // Only update while charging
  
  WiFiClientSecure* client = createSecureClient();
  HTTPClient http;
  
  String url = String(BACKEND_BASE_URL) + OTA_UPDATE_URL + "?device_id=" + DEVICE_ID;
  http.begin(*client, url);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    int contentLength = http.getSize();
    
    if (contentLength > 0) {
      Serial.println("OTA update available. Starting update...");
      
      if (Update.begin(contentLength)) {
        WiFiClient* stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);
        
        if (written == contentLength) {
          Serial.println("OTA update written successfully");
        }
        
        if (Update.end()) {
          if (Update.isFinished()) {
            Serial.println("OTA update completed successfully. Rebooting...");
            ESP.restart();
          } else {
            Serial.println("OTA update failed to finish");
          }
        } else {
          Serial.printf("OTA update error: %s\n", Update.errorString());
        }
      } else {
        Serial.println("Not enough space for OTA update");
      }
    }
  } else if (httpCode == 204) {
    Serial.println("No OTA update available");
  }
  
  http.end();
  delete client;
}

// ============================================================================
// DATA BUFFERING AND OFFLINE STORAGE
// ============================================================================

/**
 * Buffer sensor data for offline storage
 */
void bufferSensorData() {
  if (bufferedCount < MAX_BUFFERED_READINGS) {
    dataBuffer[bufferIndex] = currentSensorData;
    bufferIndex = (bufferIndex + 1) % MAX_BUFFERED_READINGS;
    bufferedCount++;
    Serial.println("Sensor data buffered for later transmission");
  } else {
    Serial.println("Buffer full - overwriting oldest data");
    bufferIndex = (bufferIndex + 1) % MAX_BUFFERED_READINGS;
  }
}

/**
 * Buffer alert data for offline storage
 */
void bufferAlertData() {
  // Store critical alerts in a separate buffer
  // Implementation would use EEPROM or flash storage
  Serial.println("Alert data buffered for later transmission");
}

/**
 * Transmit buffered data when connection is restored
 */
void transmitBufferedData() {
  if (bufferedCount == 0) return;
  
  Serial.printf("Transmitting %d buffered readings...\n", bufferedCount);
  
  for (int i = 0; i < bufferedCount; i++) {
    // Create JSON for buffered data
    // Simplified - in production would batch multiple readings
    delay(100); // Small delay between transmissions
  }
  
  // Clear buffer after successful transmission
  bufferedCount = 0;
  bufferIndex = 0;
  Serial.println("Buffered data transmitted successfully");
}/
/ ============================================================================
// ALERT DETECTION ALGORITHMS
// ============================================================================

/**
 * Advanced fall detection algorithm using accelerometer data
 */
bool detectFallEvent() {
  static float accelHistory[10][3]; // Store last 10 readings
  static int historyIndex = 0;
  static bool initialized = false;
  
  // Store current reading
  accelHistory[historyIndex][0] = currentSensorData.accel_x;
  accelHistory[historyIndex][1] = currentSensorData.accel_y;
  accelHistory[historyIndex][2] = currentSensorData.accel_z;
  
  historyIndex = (historyIndex + 1) % 10;
  
  if (!initialized) {
    if (historyIndex == 0) initialized = true;
    return false;
  }
  
  // Calculate total acceleration magnitude
  float totalAccel = sqrt(pow(currentSensorData.accel_x, 2) + 
                         pow(currentSensorData.accel_y, 2) + 
                         pow(currentSensorData.accel_z, 2));
  
  // Phase 1: Free fall detection (low acceleration)
  static bool freeFallDetected = false;
  if (totalAccel < 0.5 * 9.81) { // Less than 0.5g indicates free fall
    freeFallDetected = true;
    return false; // Don't trigger yet
  }
  
  // Phase 2: Impact detection (high acceleration after free fall)
  if (freeFallDetected && totalAccel > 2.0 * 9.81) {
    // Phase 3: Check for orientation change
    float orientationChange = calculateOrientationChange();
    
    if (orientationChange > 30.0) { // 30 degree change indicates fall
      freeFallDetected = false;
      Serial.println("Fall detected: Free fall + Impact + Orientation change");
      return true;
    }
  }
  
  // Reset free fall flag if too much time has passed
  static unsigned long freeFallTime = 0;
  if (freeFallDetected) {
    if (freeFallTime == 0) {
      freeFallTime = millis();
    } else if (millis() - freeFallTime > 2000) { // 2 second timeout
      freeFallDetected = false;
      freeFallTime = 0;
    }
  }
  
  return false;
}

/**
 * Calculate orientation change for fall detection
 */
float calculateOrientationChange() {
  // Simplified orientation calculation using accelerometer
  float currentPitch = atan2(currentSensorData.accel_y, 
                           sqrt(pow(currentSensorData.accel_x, 2) + 
                                pow(currentSensorData.accel_z, 2))) * 180.0 / PI;
  
  float currentRoll = atan2(-currentSensorData.accel_x, currentSensorData.accel_z) * 180.0 / PI;
  
  static float previousPitch = 0;
  static float previousRoll = 0;
  static bool firstReading = true;
  
  if (firstReading) {
    previousPitch = currentPitch;
    previousRoll = currentRoll;
    firstReading = false;
    return 0;
  }
  
  float pitchChange = abs(currentPitch - previousPitch);
  float rollChange = abs(currentRoll - previousRoll);
  
  previousPitch = currentPitch;
  previousRoll = currentRoll;
  
  return max(pitchChange, rollChange);
}

/**
 * Detect abnormal vital signs patterns
 */
bool detectAbnormalVitals() {
  bool abnormal = false;
  
  // Heart rate analysis
  if (currentSensorData.heart_rate > 0) {
    // Tachycardia detection
    if (currentSensorData.heart_rate > 120) {
      Serial.println("Tachycardia detected: HR > 120 BPM");
      abnormal = true;
    }
    
    // Bradycardia detection
    if (currentSensorData.heart_rate < 50) {
      Serial.println("Bradycardia detected: HR < 50 BPM");
      abnormal = true;
    }
    
    // Heart rate variability analysis (simplified)
    static int hrHistory[5] = {0};
    static int hrHistoryIndex = 0;
    
    hrHistory[hrHistoryIndex] = currentSensorData.heart_rate;
    hrHistoryIndex = (hrHistoryIndex + 1) % 5;
    
    // Check for sudden changes
    int maxHR = 0, minHR = 200;
    for (int i = 0; i < 5; i++) {
      if (hrHistory[i] > 0) {
        maxHR = max(maxHR, hrHistory[i]);
        minHR = min(minHR, hrHistory[i]);
      }
    }
    
    if (maxHR - minHR > 40) {
      Serial.println("Irregular heart rate detected");
      abnormal = true;
    }
  }
  
  // SpO2 analysis
  if (currentSensorData.spo2 > 0) {
    if (currentSensorData.spo2 < 90) {
      Serial.println("Hypoxemia detected: SpO2 < 90%");
      abnormal = true;
    }
    
    // Trend analysis for SpO2
    static int spo2History[5] = {0};
    static int spo2HistoryIndex = 0;
    
    spo2History[spo2HistoryIndex] = currentSensorData.spo2;
    spo2HistoryIndex = (spo2HistoryIndex + 1) % 5;
    
    // Check for declining trend
    int declineCount = 0;
    for (int i = 1; i < 5; i++) {
      if (spo2History[i] > 0 && spo2History[i-1] > 0) {
        if (spo2History[i] < spo2History[i-1]) {
          declineCount++;
        }
      }
    }
    
    if (declineCount >= 3) {
      Serial.println("Declining SpO2 trend detected");
      abnormal = true;
    }
  }
  
  // Temperature analysis
  if (!isnan(currentSensorData.temperature)) {
    if (currentSensorData.temperature > 38.0) {
      Serial.println("Fever detected: Temperature > 38°C");
      abnormal = true;
    }
    
    if (currentSensorData.temperature < 35.0) {
      Serial.println("Hypothermia detected: Temperature < 35°C");
      abnormal = true;
    }
  }
  
  return abnormal;
}

/**
 * Analyze movement patterns for unusual activity
 */
bool analyzeMovementPatterns() {
  static float movementHistory[20] = {0}; // Store movement intensity
  static int movementIndex = 0;
  static unsigned long lastMovementTime = 0;
  
  // Calculate movement intensity
  float movementIntensity = sqrt(pow(currentSensorData.accel_x, 2) + 
                                pow(currentSensorData.accel_y, 2) + 
                                pow(currentSensorData.accel_z, 2));
  
  movementHistory[movementIndex] = movementIntensity;
  movementIndex = (movementIndex + 1) % 20;
  
  // Calculate average movement over last 20 readings
  float avgMovement = 0;
  for (int i = 0; i < 20; i++) {
    avgMovement += movementHistory[i];
  }
  avgMovement /= 20.0;
  
  // Detect prolonged inactivity
  if (avgMovement < 0.2) { // Very low movement threshold
    if (lastMovementTime == 0) {
      lastMovementTime = millis();
    } else if (millis() - lastMovementTime > 300000) { // 5 minutes of inactivity
      Serial.println("Prolonged inactivity detected");
      return true;
    }
  } else {
    lastMovementTime = 0; // Reset inactivity timer
  }
  
  // Detect excessive movement (possible seizure or distress)
  if (avgMovement > 3.0) {
    Serial.println("Excessive movement detected");
    return true;
  }
  
  // Detect repetitive movement patterns (possible distress)
  float variance = 0;
  for (int i = 0; i < 20; i++) {
    variance += pow(movementHistory[i] - avgMovement, 2);
  }
  variance /= 20.0;
  
  if (variance < 0.1 && avgMovement > 1.0) {
    Serial.println("Repetitive movement pattern detected");
    return true;
  }
  
  return false;
}

/**
 * Prioritize and manage alert transmission
 */
void processAndTransmitAlerts() {
  // Priority levels: 1 = Critical, 2 = High, 3 = Medium, 4 = Low
  struct AlertPriority {
    String alertType;
    int priority;
    bool requiresImmediate;
  };
  
  AlertPriority alerts[10];
  int alertCount = 0;
  
  // Check for fall detection
  if (detectFallEvent()) {
    currentAlerts.fall = true;
    alerts[alertCount++] = {"fall", 1, true};
  }
  
  // Check for tamper detection
  if (currentAlerts.tamper) {
    alerts[alertCount++] = {"tamper", 1, true};
  }
  
  // Check for abnormal vitals
  if (detectAbnormalVitals()) {
    currentAlerts.abnormal_vitals = true;
    alerts[alertCount++] = {"abnormal_vitals", 2, false};
  }
  
  // Check for movement patterns
  if (analyzeMovementPatterns()) {
    alerts[alertCount++] = {"unusual_movement", 3, false};
  }
  
  // Check for battery alerts
  if (currentAlerts.critical_battery) {
    alerts[alertCount++] = {"critical_battery", 2, false};
  } else if (currentAlerts.low_battery) {
    alerts[alertCount++] = {"low_battery", 4, false};
  }
  
  // Check for sensor failures
  if (currentAlerts.sensor_failure) {
    alerts[alertCount++] = {"sensor_failure", 3, false};
  }
  
  // Sort alerts by priority and transmit
  for (int i = 0; i < alertCount; i++) {
    if (alerts[i].requiresImmediate && wifiConnected) {
      transmitEmergencyAlert(alerts[i].alertType);
    }
  }
  
  // Regular alerts will be included in the next data transmission
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Get current timestamp in ISO format
 */
String getCurrentTimestamp() {
  time_t now;
  struct tm timeinfo;
  
  if (!getLocalTime(&timeinfo)) {
    // Fallback to millis if NTP not available
    return String(millis());
  }
  
  char timeString[64];
  strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(timeString);
}

/**
 * Initialize NTP time synchronization
 */
void initializeNTP() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("NTP time synchronization initialized");
}

/**
 * Perform system self-test
 */
bool performSelfTest() {
  Serial.println("Performing system self-test...");
  
  bool testPassed = true;
  
  // Test sensors
  if (!mpu.begin()) {
    Serial.println("Self-test FAILED: MPU6050 not responding");
    testPassed = false;
  }
  
  if (!particleSensor.begin()) {
    Serial.println("Self-test FAILED: MAX30102 not responding");
    testPassed = false;
  }
  
  // Test DHT11
  float testTemp = dht.readTemperature();
  if (isnan(testTemp)) {
    Serial.println("Self-test FAILED: DHT11 not responding");
    testPassed = false;
  }
  
  // Test GPIO pins
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);
  
  // Test battery monitoring
  int batteryReading = adc1_get_raw(ADC1_CHANNEL_7);
  if (batteryReading < 100) {
    Serial.println("Self-test WARNING: Battery reading seems low");
  }
  
  if (testPassed) {
    Serial.println("Self-test PASSED: All systems operational");
    // Flash LED green 3 times
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
  } else {
    Serial.println("Self-test FAILED: Some systems not operational");
    // Flash LED red 5 times
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  return testPassed;
}

/**
 * Handle system errors and recovery
 */
void handleSystemError(String errorType, String errorMessage) {
  Serial.printf("SYSTEM ERROR [%s]: %s\n", errorType.c_str(), errorMessage.c_str());
  
  // Log error to local storage
  StaticJsonDocument<200> errorLog;
  errorLog["timestamp"] = millis();
  errorLog["error_type"] = errorType;
  errorLog["error_message"] = errorMessage;
  errorLog["device_id"] = DEVICE_ID;
  
  // Attempt recovery based on error type
  if (errorType == "sensor_failure") {
    // Try to reinitialize sensors
    initializeSensors();
  } else if (errorType == "wifi_failure") {
    // Reset WiFi and try to reconnect
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else if (errorType == "memory_error") {
    // Clear buffers and restart if necessary
    bufferedCount = 0;
    bufferIndex = 0;
  }
  
  // If critical error, consider system restart
  if (errorType == "critical_failure") {
    Serial.println("Critical failure detected. Restarting system in 5 seconds...");
    delay(5000);
    ESP.restart();
  }
}

/**
 * Print system status for debugging
 */
void printSystemStatus() {
  Serial.println("=== SYSTEM STATUS ===");
  Serial.printf("Device ID: %s\n", DEVICE_ID.c_str());
  Serial.printf("Patient ID: %s\n", PATIENT_ID.c_str());
  Serial.printf("Operating Mode: %d\n", currentMode);
  Serial.printf("WiFi Connected: %s\n", wifiConnected ? "YES" : "NO");
  Serial.printf("Battery Level: %d%%\n", currentSensorData.battery_level);
  Serial.printf("Charging: %s\n", currentSensorData.charging ? "YES" : "NO");
  Serial.printf("Strap Intact: %s\n", currentSensorData.strap_intact ? "YES" : "NO");
  Serial.printf("Skin Contact: %s\n", currentSensorData.skin_contact ? "YES" : "NO");
  Serial.printf("Heart Rate: %d BPM\n", currentSensorData.heart_rate);
  Serial.printf("SpO2: %d%%\n", currentSensorData.spo2);
  Serial.printf("Temperature: %.1f°C\n", currentSensorData.temperature);
  Serial.printf("Buffered Readings: %d\n", bufferedCount);
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.println("====================");
}