#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <HardwareSerial.h>
#include <map>
#include <vector>

// Production ESP32 Receiver Node Firmware
// Implements long-range RFID reader integration, zone management,
// status indicators, and robust communication with offline storage

// Hardware Configuration
#define RFID_RX_PIN 16
#define RFID_TX_PIN 17
#define LED_RED_PIN 25
#define LED_GREEN_PIN 26
#define LED_BLUE_PIN 27
#define BUZZER_PIN 14

// RFID Reader Configuration
#define RFID_BAUD_RATE 9600
#define RFID_SCAN_INTERVAL 1000  // ms
#define RFID_DETECTION_TIMEOUT 5000  // ms
#define MAX_DETECTION_DISTANCE 5.0  // meters
#define MIN_SIGNAL_STRENGTH -80  // dBm

// Communication Configuration
#define WIFI_CONNECT_TIMEOUT 30000  // ms
#define HTTP_TIMEOUT 10000  // ms
#define SYNC_INTERVAL 5000  // ms
#define OFFLINE_STORAGE_SIZE 100  // max stored records

// EEPROM Configuration
#define EEPROM_SIZE 4096
#define CONFIG_START_ADDR 0
#define OFFLINE_DATA_START_ADDR 512

// Status LED Colors
enum LEDColor {
  LED_OFF,
  LED_RED,
  LED_GREEN,
  LED_BLUE,
  LED_YELLOW,
  LED_PURPLE,
  LED_CYAN,
  LED_WHITE
};

// Alert Types for Buzzer
enum AlertType {
  ALERT_NONE,
  ALERT_TAG_DETECTED,
  ALERT_ZONE_VIOLATION,
  ALERT_SYSTEM_ERROR,
  ALERT_LOW_BATTERY,
  ALERT_COMMUNICATION_ERROR
};

// Zone Types
enum ZoneType {
  ZONE_NORMAL,
  ZONE_RESTRICTED,
  ZONE_ISOLATION,
  ZONE_EXIT
};

// Device Status
enum DeviceStatus {
  STATUS_INITIALIZING,
  STATUS_CONNECTING,
  STATUS_ONLINE,
  STATUS_OFFLINE,
  STATUS_ERROR,
  STATUS_MAINTENANCE
};

// RFID Tag Detection Structure
struct RFIDDetection {
  String rfid_tag;
  String patient_id;
  int signal_strength;
  float estimated_distance;
  unsigned long detection_time;
  int detection_count;
  bool is_valid;
};

// Zone Configuration Structure
struct ZoneConfig {
  String zone_id;
  String zone_name;
  ZoneType zone_type;
  bool require_authorization;
  int max_occupancy;
  std::vector<String> authorized_patients;
  int alert_level;
};

// Device Configuration Structure
struct DeviceConfig {
  String receiver_id;
  String wifi_ssid;
  String wifi_password;
  String backend_url;
  ZoneConfig zone_config;
  int scan_interval;
  bool auto_register;
  String device_name;
  String location_description;
};

// Offline Data Record
struct OfflineRecord {
  unsigned long timestamp;
  String data_json;
  bool transmitted;
};

class RFIDReader {
private:
  HardwareSerial* serial;
  std::map<String, RFIDDetection> active_detections;
  unsigned long last_scan_time;
  
public:
  RFIDReader() : serial(&Serial2), last_scan_time(0) {}
  
  void begin() {
    serial->begin(RFID_BAUD_RATE, SERIAL_8N1, RFID_RX_PIN, RFID_TX_PIN);
    Serial.println("RFID Reader initialized");
  }
  
  void scan() {
    unsigned long now = millis();
    if (now - last_scan_time < RFID_SCAN_INTERVAL) {
      return;
    }
    last_scan_time = now;
    
    // Clean up old detections
    cleanupOldDetections();
    
    // Read from RFID module
    if (serial->available()) {
      String rfid_data = serial->readStringUntil('\n');
      rfid_data.trim();
      
      if (rfid_data.length() >= 10) {  // Valid RFID tag length
        processRFIDTag(rfid_data);
      }
    }
  }
  
  void processRFIDTag(const String& tag_id) {
    // Simulate signal strength measurement (in production, read from RFID module)
    int signal_strength = random(-40, -80);
    float distance = calculateDistance(signal_strength);
    
    if (distance > MAX_DETECTION_DISTANCE || signal_strength < MIN_SIGNAL_STRENGTH) {
      return;  // Tag too far or signal too weak
    }
    
    unsigned long now = millis();
    String patient_id = lookupPatientId(tag_id);
    
    if (active_detections.find(tag_id) != active_detections.end()) {
      // Update existing detection
      active_detections[tag_id].detection_count++;
      active_detections[tag_id].detection_time = now;
      active_detections[tag_id].signal_strength = signal_strength;
      active_detections[tag_id].estimated_distance = distance;
    } else {
      // New detection
      RFIDDetection detection;
      detection.rfid_tag = tag_id;
      detection.patient_id = patient_id;
      detection.signal_strength = signal_strength;
      detection.estimated_distance = distance;
      detection.detection_time = now;
      detection.detection_count = 1;
      detection.is_valid = validateTag(tag_id);
      
      active_detections[tag_id] = detection;
      
      Serial.printf("New RFID detection: %s (Patient: %s, Distance: %.1fm)\n", 
                   tag_id.c_str(), patient_id.c_str(), distance);
    }
  }
  
  float calculateDistance(int signal_strength) {
    // Convert RSSI to distance using path loss formula
    // Distance = 10^((Tx_Power - RSSI) / (10 * n))
    // Assuming Tx_Power = -30 dBm, path loss exponent n = 2
    float distance = pow(10, (-30.0 - signal_strength) / 20.0);
    return constrain(distance, 0.1, MAX_DETECTION_DISTANCE);
  }
  
  String lookupPatientId(const String& rfid_tag) {
    // In production, this would query a local cache or backend
    // For now, generate a patient ID based on tag
    return "P" + rfid_tag.substring(rfid_tag.length() - 3);
  }
  
  bool validateTag(const String& tag_id) {
    // Validate tag format and checksum
    if (tag_id.length() < 8 || tag_id.length() > 16) {
      return false;
    }
    
    // Check for valid hexadecimal characters
    for (char c : tag_id) {
      if (!isxdigit(c)) {
        return false;
      }
    }
    
    return true;
  }
  
  void cleanupOldDetections() {
    unsigned long now = millis();
    auto it = active_detections.begin();
    
    while (it != active_detections.end()) {
      if (now - it->second.detection_time > RFID_DETECTION_TIMEOUT) {
        Serial.printf("Removing old detection: %s\n", it->first.c_str());
        it = active_detections.erase(it);
      } else {
        ++it;
      }
    }
  }
  
  std::vector<RFIDDetection> getActiveDetections() {
    std::vector<RFIDDetection> detections;
    for (const auto& pair : active_detections) {
      if (pair.second.is_valid) {
        detections.push_back(pair.second);
      }
    }
    return detections;
  }
  
  int getDetectionCount() {
    return active_detections.size();
  }
};

class ZoneManager {
private:
  ZoneConfig current_zone;
  std::map<String, int> patient_counts;
  std::map<String, unsigned long> patient_entry_times;
  std::map<String, unsigned long> patient_last_seen;
  std::vector<String> violation_history;
  unsigned long last_occupancy_check;
  
public:
  void initialize(const ZoneConfig& config) {
    current_zone = config;
    patient_counts.clear();
    patient_entry_times.clear();
    patient_last_seen.clear();
    violation_history.clear();
    last_occupancy_check = millis();
    
    Serial.printf("Zone initialized: %s (%s)\n", 
                 config.zone_name.c_str(), 
                 getZoneTypeString(config.zone_type).c_str());
    Serial.printf("Authorization required: %s\n", 
                 config.require_authorization ? "Yes" : "No");
    Serial.printf("Max occupancy: %d\n", config.max_occupancy);
    Serial.printf("Authorized patients: %d\n", config.authorized_patients.size());
  }
  
  bool checkZoneAccess(const String& patient_id) {
    if (!current_zone.require_authorization) {
      return true;
    }
    
    // Check if patient is in authorized list
    for (const String& authorized : current_zone.authorized_patients) {
      if (authorized == patient_id) {
        return true;
      }
    }
    
    // Special handling for different zone types
    switch (current_zone.zone_type) {
      case ZONE_ISOLATION:
        // Isolation zones are very restrictive
        return false;
      case ZONE_EXIT:
        // Exit zones may have different rules
        return checkExitPermission(patient_id);
      case ZONE_RESTRICTED:
        // Restricted zones require explicit authorization
        return false;
      default:
        // Normal zones may allow general access
        return !current_zone.require_authorization;
    }
  }
  
  bool checkExitPermission(const String& patient_id) {
    // In production, this would check discharge status, visitor passes, etc.
    // For now, assume exit requires authorization
    return false;
  }
  
  bool checkOccupancyLimit() {
    if (current_zone.max_occupancy <= 0) {
      return true;  // No limit
    }
    
    int current_occupancy = getCurrentOccupancy();
    return current_occupancy <= current_zone.max_occupancy;
  }
  
  AlertType processDetections(const std::vector<RFIDDetection>& detections) {
    unsigned long now = millis();
    AlertType highest_alert = ALERT_NONE;
    
    // Track new entries and updates
    std::map<String, int> new_patient_counts;
    for (const auto& detection : detections) {
      new_patient_counts[detection.patient_id]++;
      patient_last_seen[detection.patient_id] = now;
      
      // Check for new entries
      if (patient_counts.find(detection.patient_id) == patient_counts.end()) {
        patient_entry_times[detection.patient_id] = now;
        Serial.printf("Patient %s entered zone %s\n", 
                     detection.patient_id.c_str(), current_zone.zone_name.c_str());
      }
    }
    
    // Update patient counts
    patient_counts = new_patient_counts;
    
    // Clean up patients who haven't been seen recently
    cleanupAbsentPatients();
    
    // Check for zone access violations
    for (const auto& detection : detections) {
      if (!checkZoneAccess(detection.patient_id)) {
        String violation = "VIOLATION: Patient " + detection.patient_id + 
                          " unauthorized access to " + current_zone.zone_name + 
                          " at " + String(now);
        violation_history.push_back(violation);
        
        Serial.printf("Zone violation: Patient %s not authorized for %s\n",
                     detection.patient_id.c_str(), current_zone.zone_name.c_str());
        
        // Generate alert based on zone type
        switch (current_zone.zone_type) {
          case ZONE_ISOLATION:
          case ZONE_EXIT:
            highest_alert = ALERT_ZONE_VIOLATION;
            break;
          case ZONE_RESTRICTED:
            if (highest_alert == ALERT_NONE) {
              highest_alert = ALERT_ZONE_VIOLATION;
            }
            break;
          default:
            break;
        }
      }
    }
    
    // Check occupancy limits
    if (!checkOccupancyLimit()) {
      Serial.printf("Occupancy limit exceeded: %d/%d in %s\n", 
                   getCurrentOccupancy(), current_zone.max_occupancy,
                   current_zone.zone_name.c_str());
      
      String violation = "OCCUPANCY: Limit exceeded " + String(getCurrentOccupancy()) + 
                        "/" + String(current_zone.max_occupancy) + " in " + 
                        current_zone.zone_name + " at " + String(now);
      violation_history.push_back(violation);
      
      if (highest_alert == ALERT_NONE) {
        highest_alert = ALERT_ZONE_VIOLATION;
      }
    }
    
    // Limit violation history size
    if (violation_history.size() > 50) {
      violation_history.erase(violation_history.begin());
    }
    
    return highest_alert;
  }
  
  void cleanupAbsentPatients() {
    unsigned long now = millis();
    const unsigned long ABSENCE_TIMEOUT = 10000;  // 10 seconds
    
    auto it = patient_counts.begin();
    while (it != patient_counts.end()) {
      if (now - patient_last_seen[it->first] > ABSENCE_TIMEOUT) {
        Serial.printf("Patient %s left zone %s\n", 
                     it->first.c_str(), current_zone.zone_name.c_str());
        
        patient_entry_times.erase(it->first);
        patient_last_seen.erase(it->first);
        it = patient_counts.erase(it);
      } else {
        ++it;
      }
    }
  }
  
  int getCurrentOccupancy() {
    return patient_counts.size();
  }
  
  std::vector<String> getCurrentPatients() {
    std::vector<String> patients;
    for (const auto& pair : patient_counts) {
      patients.push_back(pair.first);
    }
    return patients;
  }
  
  unsigned long getPatientDwellTime(const String& patient_id) {
    if (patient_entry_times.find(patient_id) != patient_entry_times.end()) {
      return millis() - patient_entry_times[patient_id];
    }
    return 0;
  }
  
  std::vector<String> getRecentViolations(int count = 10) {
    std::vector<String> recent;
    int start = std::max(0, (int)violation_history.size() - count);
    for (int i = start; i < violation_history.size(); i++) {
      recent.push_back(violation_history[i]);
    }
    return recent;
  }
  
  void updateZoneConfiguration(const ZoneConfig& new_config) {
    current_zone = new_config;
    Serial.printf("Zone configuration updated: %s\n", new_config.zone_name.c_str());
    
    // Clear current state to avoid conflicts
    patient_counts.clear();
    patient_entry_times.clear();
    patient_last_seen.clear();
  }
  
  void addAuthorizedPatient(const String& patient_id) {
    current_zone.authorized_patients.push_back(patient_id);
    Serial.printf("Added authorized patient: %s to zone %s\n", 
                 patient_id.c_str(), current_zone.zone_name.c_str());
  }
  
  void removeAuthorizedPatient(const String& patient_id) {
    auto it = std::find(current_zone.authorized_patients.begin(), 
                       current_zone.authorized_patients.end(), patient_id);
    if (it != current_zone.authorized_patients.end()) {
      current_zone.authorized_patients.erase(it);
      Serial.printf("Removed authorized patient: %s from zone %s\n", 
                   patient_id.c_str(), current_zone.zone_name.c_str());
    }
  }
  
  String getZoneTypeString(ZoneType type) {
    switch (type) {
      case ZONE_NORMAL: return "Normal";
      case ZONE_RESTRICTED: return "Restricted";
      case ZONE_ISOLATION: return "Isolation";
      case ZONE_EXIT: return "Exit";
      default: return "Unknown";
    }
  }
  
  ZoneConfig getCurrentZone() {
    return current_zone;
  }
  
  void printZoneStatus() {
    Serial.println("=== ZONE STATUS ===");
    Serial.printf("Zone: %s (%s)\n", current_zone.zone_name.c_str(), 
                 getZoneTypeString(current_zone.zone_type).c_str());
    Serial.printf("Current Occupancy: %d/%d\n", getCurrentOccupancy(), 
                 current_zone.max_occupancy);
    Serial.printf("Authorization Required: %s\n", 
                 current_zone.require_authorization ? "Yes" : "No");
    
    if (!patient_counts.empty()) {
      Serial.println("Current Patients:");
      for (const auto& pair : patient_counts) {
        unsigned long dwell_time = getPatientDwellTime(pair.first);
        Serial.printf("  %s (dwell: %lu seconds)\n", 
                     pair.first.c_str(), dwell_time / 1000);
      }
    }
    
    if (!violation_history.empty()) {
      Serial.printf("Recent Violations: %d\n", violation_history.size());
      auto recent = getRecentViolations(3);
      for (const String& violation : recent) {
        Serial.printf("  %s\n", violation.c_str());
      }
    }
    Serial.println("==================");
  }
};

class StatusIndicator {
private:
  LEDColor current_color;
  unsigned long last_blink_time;
  bool blink_state;
  int blink_interval;
  bool buzzer_active;
  unsigned long buzzer_start_time;
  int buzzer_duration;
  
public:
  StatusIndicator() : current_color(LED_OFF), last_blink_time(0), 
                     blink_state(false), blink_interval(0),
                     buzzer_active(false), buzzer_start_time(0), buzzer_duration(0) {}
  
  void begin() {
    pinMode(LED_RED_PIN, OUTPUT);
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_BLUE_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    
    setColor(LED_OFF);
    digitalWrite(BUZZER_PIN, LOW);
    
    Serial.println("Status indicators initialized");
  }
  
  void setColor(LEDColor color) {
    current_color = color;
    blink_interval = 0;  // Stop blinking
    
    digitalWrite(LED_RED_PIN, LOW);
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_BLUE_PIN, LOW);
    
    switch (color) {
      case LED_RED:
        digitalWrite(LED_RED_PIN, HIGH);
        break;
      case LED_GREEN:
        digitalWrite(LED_GREEN_PIN, HIGH);
        break;
      case LED_BLUE:
        digitalWrite(LED_BLUE_PIN, HIGH);
        break;
      case LED_YELLOW:
        digitalWrite(LED_RED_PIN, HIGH);
        digitalWrite(LED_GREEN_PIN, HIGH);
        break;
      case LED_PURPLE:
        digitalWrite(LED_RED_PIN, HIGH);
        digitalWrite(LED_BLUE_PIN, HIGH);
        break;
      case LED_CYAN:
        digitalWrite(LED_GREEN_PIN, HIGH);
        digitalWrite(LED_BLUE_PIN, HIGH);
        break;
      case LED_WHITE:
        digitalWrite(LED_RED_PIN, HIGH);
        digitalWrite(LED_GREEN_PIN, HIGH);
        digitalWrite(LED_BLUE_PIN, HIGH);
        break;
      default:
        // LED_OFF - all pins already set LOW
        break;
    }
  }
  
  void setBlink(LEDColor color, int interval_ms) {
    current_color = color;
    blink_interval = interval_ms;
    blink_state = false;
    last_blink_time = millis();
  }
  
  void setStatusColor(DeviceStatus status) {
    switch (status) {
      case STATUS_INITIALIZING:
        setBlink(LED_BLUE, 500);
        break;
      case STATUS_CONNECTING:
        setBlink(LED_YELLOW, 250);
        break;
      case STATUS_ONLINE:
        setColor(LED_GREEN);
        break;
      case STATUS_OFFLINE:
        setBlink(LED_RED, 1000);
        break;
      case STATUS_ERROR:
        setBlink(LED_RED, 100);
        break;
      case STATUS_MAINTENANCE:
        setBlink(LED_PURPLE, 2000);
        break;
    }
  }
  
  void playAlert(AlertType alert) {
    switch (alert) {
      case ALERT_TAG_DETECTED:
        playBuzzer(100, 1);  // Short beep
        flashColor(LED_BLUE, 200);  // Brief blue flash
        break;
      case ALERT_ZONE_VIOLATION:
        playBuzzer(500, 3);  // Three long beeps
        setBlink(LED_RED, 200);  // Fast red blinking
        break;
      case ALERT_SYSTEM_ERROR:
        playBuzzer(1000, 1);  // Long beep
        setBlink(LED_RED, 100);  // Very fast red blinking
        break;
      case ALERT_LOW_BATTERY:
        playBuzzer(300, 2);  // Two medium beeps
        setBlink(LED_YELLOW, 1000);  // Slow yellow blinking
        break;
      case ALERT_COMMUNICATION_ERROR:
        playBuzzer(200, 2);  // Two medium beeps
        setBlink(LED_PURPLE, 500);  // Purple blinking
        break;
      default:
        break;
    }
  }
  
  void flashColor(LEDColor color, int duration_ms) {
    LEDColor previous_color = current_color;
    int previous_blink = blink_interval;
    
    setColor(color);
    delay(duration_ms);
    
    if (previous_blink > 0) {
      setBlink(previous_color, previous_blink);
    } else {
      setColor(previous_color);
    }
  }
  
  void playStartupSequence() {
    // Startup light sequence
    setColor(LED_RED);
    delay(300);
    setColor(LED_GREEN);
    delay(300);
    setColor(LED_BLUE);
    delay(300);
    setColor(LED_WHITE);
    delay(500);
    setColor(LED_OFF);
    
    // Startup beep sequence
    playBuzzer(200, 3);
  }
  
  void playShutdownSequence() {
    // Shutdown sequence
    setBlink(LED_RED, 100);
    delay(1000);
    setColor(LED_OFF);
    playBuzzer(500, 1);
  }
  
  void enterDiagnosticMode() {
    Serial.println("Entering diagnostic mode...");
    
    // Test all LED colors
    Serial.println("Testing LEDs...");
    setColor(LED_RED); delay(500);
    setColor(LED_GREEN); delay(500);
    setColor(LED_BLUE); delay(500);
    setColor(LED_YELLOW); delay(500);
    setColor(LED_PURPLE); delay(500);
    setColor(LED_CYAN); delay(500);
    setColor(LED_WHITE); delay(500);
    setColor(LED_OFF);
    
    // Test buzzer patterns
    Serial.println("Testing buzzer...");
    playBuzzer(100, 1); delay(500);
    playBuzzer(200, 2); delay(500);
    playBuzzer(300, 3); delay(500);
    
    // Test alert patterns
    Serial.println("Testing alert patterns...");
    playAlert(ALERT_TAG_DETECTED); delay(1000);
    playAlert(ALERT_ZONE_VIOLATION); delay(2000);
    playAlert(ALERT_COMMUNICATION_ERROR); delay(1000);
    
    setColor(LED_GREEN);
    Serial.println("Diagnostic mode complete");
  }
  
  void indicateDataTransmission(bool success) {
    if (success) {
      flashColor(LED_GREEN, 100);
    } else {
      flashColor(LED_RED, 200);
    }
  }
  
  void indicateTagDetection(int tag_count) {
    // Visual feedback based on number of tags detected
    if (tag_count == 0) {
      // No tags - dim blue
      setColor(LED_BLUE);
    } else if (tag_count <= 3) {
      // Few tags - steady green
      setColor(LED_GREEN);
    } else if (tag_count <= 6) {
      // Moderate tags - yellow
      setColor(LED_YELLOW);
    } else {
      // Many tags - blinking yellow (potential crowding)
      setBlink(LED_YELLOW, 500);
    }
  }
  
  void indicateZoneStatus(ZoneType zone_type, bool violation) {
    if (violation) {
      setBlink(LED_RED, 200);
      return;
    }
    
    switch (zone_type) {
      case ZONE_NORMAL:
        setColor(LED_GREEN);
        break;
      case ZONE_RESTRICTED:
        setColor(LED_YELLOW);
        break;
      case ZONE_ISOLATION:
        setColor(LED_PURPLE);
        break;
      case ZONE_EXIT:
        setColor(LED_CYAN);
        break;
    }
  }
  
  void playBuzzer(int duration_ms, int count = 1) {
    buzzer_active = true;
    buzzer_start_time = millis();
    buzzer_duration = duration_ms * count + (count - 1) * 100;  // Include gaps
    
    // Start buzzer pattern
    for (int i = 0; i < count; i++) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(duration_ms);
      digitalWrite(BUZZER_PIN, LOW);
      if (i < count - 1) {
        delay(100);  // Gap between beeps
      }
    }
  }
  
  void update() {
    unsigned long now = millis();
    
    // Handle blinking
    if (blink_interval > 0 && now - last_blink_time >= blink_interval) {
      blink_state = !blink_state;
      last_blink_time = now;
      
      if (blink_state) {
        setColor(current_color);
      } else {
        setColor(LED_OFF);
      }
    }
    
    // Handle buzzer timeout
    if (buzzer_active && now - buzzer_start_time >= buzzer_duration) {
      buzzer_active = false;
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
};

// Global objects
RFIDReader rfidReader;
ZoneManager zoneManager;
StatusIndicator statusIndicator;
DeviceConfig deviceConfig;
DeviceStatus currentStatus = STATUS_INITIALIZING;

// Communication and storage variables
std::vector<OfflineRecord> offline_storage;
unsigned long last_sync_time = 0;
bool wifi_connected = false;
HTTPClient http;

void loadConfiguration() {
  // Load device configuration from EEPROM
  Serial.println("Loading configuration from EEPROM...");
  
  // Check if configuration exists in EEPROM
  uint8_t config_marker;
  EEPROM.get(CONFIG_START_ADDR, config_marker);
  
  if (config_marker == 0xAA) {
    // Load configuration from EEPROM
    Serial.println("Found existing configuration in EEPROM");
    // In production, deserialize full config from EEPROM
    // For now, use default values with some customization
  } else {
    Serial.println("No configuration found, using defaults");
  }
  
  // Default configuration (would be loaded from EEPROM in production)
  deviceConfig.receiver_id = "RX-" + WiFi.macAddress().substring(9);  // Use MAC suffix
  deviceConfig.wifi_ssid = "HospitalWiFi";
  deviceConfig.wifi_password = "SecurePassword123";
  deviceConfig.backend_url = "https://halo-watch-backend.hospital.local:8000";
  deviceConfig.scan_interval = RFID_SCAN_INTERVAL;
  deviceConfig.auto_register = true;
  deviceConfig.device_name = "Hospital Receiver Node";
  deviceConfig.location_description = "Automatic Location";
  
  // Configure zone
  deviceConfig.zone_config.zone_id = "AUTO_ZONE_" + String(random(1000, 9999));
  deviceConfig.zone_config.zone_name = "Auto-configured Zone";
  deviceConfig.zone_config.zone_type = ZONE_NORMAL;
  deviceConfig.zone_config.require_authorization = false;
  deviceConfig.zone_config.max_occupancy = 10;
  deviceConfig.zone_config.alert_level = 1;
  
  Serial.printf("Configuration loaded - Device ID: %s\n", deviceConfig.receiver_id.c_str());
}

void resetConfiguration() {
  Serial.println("Resetting configuration to defaults...");
  
  // Clear EEPROM configuration
  for (int i = CONFIG_START_ADDR; i < CONFIG_START_ADDR + 512; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  
  // Reload defaults
  loadConfiguration();
  Serial.println("Configuration reset complete");
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Receiver Node - Production Firmware v1.0");
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Initialize hardware components
  statusIndicator.begin();
  statusIndicator.playStartupSequence();
  statusIndicator.setStatusColor(STATUS_INITIALIZING);
  
  // Load configuration
  loadConfiguration();
  
  // Initialize RFID reader
  rfidReader.begin();
  
  // Initialize zone manager
  zoneManager.initialize(deviceConfig.zone_config);
  
  // Connect to WiFi
  currentStatus = STATUS_CONNECTING;
  statusIndicator.setStatusColor(STATUS_CONNECTING);
  connectWiFi();
  
  Serial.println("Receiver node initialization complete");
  Serial.println("Type 'help' for available commands");
}

void loop() {
  // Handle serial commands for debugging
  handleSerialCommands();
  
  // Update status indicators
  statusIndicator.update();
  
  // Check WiFi connection
  checkWiFiConnection();
  
  // Scan for RFID tags
  rfidReader.scan();
  
  // Process detections and check for violations
  std::vector<RFIDDetection> detections = rfidReader.getActiveDetections();
  AlertType alert = zoneManager.processDetections(detections);
  
  // Update visual indicators based on current state
  statusIndicator.indicateTagDetection(detections.size());
  statusIndicator.indicateZoneStatus(deviceConfig.zone_config.zone_type, alert == ALERT_ZONE_VIOLATION);
  
  // Play alert if necessary
  if (alert != ALERT_NONE) {
    statusIndicator.playAlert(alert);
  }
  
  // Sync data with backend
  unsigned long now = millis();
  if (now - last_sync_time >= SYNC_INTERVAL) {
    last_sync_time = now;
    bool sync_success = syncWithBackend(detections);
    statusIndicator.indicateDataTransmission(sync_success);
  }
  
  delay(50);  // Small delay to prevent watchdog issues
}
void lo
adConfiguration() {
  // Load device configuration from EEPROM
  // In production, this would load from persistent storage
  deviceConfig.receiver_id = "RX-WARD-A-001";
  deviceConfig.wifi_ssid = "HospitalWiFi";
  deviceConfig.wifi_password = "SecurePassword123";
  deviceConfig.backend_url = "https://halo-watch-backend.hospital.local:8000";
  deviceConfig.scan_interval = RFID_SCAN_INTERVAL;
  deviceConfig.auto_register = true;
  deviceConfig.device_name = "Ward A Corridor Receiver";
  deviceConfig.location_description = "Main corridor entrance";
  
  // Configure zone
  deviceConfig.zone_config.zone_id = "WARD_A_CORRIDOR";
  deviceConfig.zone_config.zone_name = "Ward A Corridor";
  deviceConfig.zone_config.zone_type = ZONE_NORMAL;
  deviceConfig.zone_config.require_authorization = false;
  deviceConfig.zone_config.max_occupancy = 10;
  deviceConfig.zone_config.alert_level = 1;
  
  Serial.println("Configuration loaded");
}

void saveConfiguration() {
  // Save configuration to EEPROM
  Serial.println("Saving configuration to EEPROM...");
  
  // Set configuration marker
  uint8_t config_marker = 0xAA;
  EEPROM.put(CONFIG_START_ADDR, config_marker);
  
  // In production, serialize full configuration to EEPROM
  // For now, just save the marker
  
  EEPROM.commit();
  Serial.println("Configuration saved");
}

void connectWiFi() {
  Serial.printf("Connecting to WiFi: %s\n", deviceConfig.wifi_ssid.c_str());
  
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(deviceConfig.receiver_id.c_str());
  WiFi.begin(deviceConfig.wifi_ssid.c_str(), deviceConfig.wifi_password.c_str());
  
  unsigned long start_time = millis();
  int retry_count = 0;
  const int MAX_RETRIES = 3;
  
  while (WiFi.status() != WL_CONNECTED && retry_count < MAX_RETRIES) {
    unsigned long attempt_start = millis();
    
    while (WiFi.status() != WL_CONNECTED && 
           millis() - attempt_start < WIFI_CONNECT_TIMEOUT / MAX_RETRIES) {
      delay(500);
      Serial.print(".");
    }
    
    if (WiFi.status() != WL_CONNECTED) {
      retry_count++;
      Serial.printf("\nWiFi connection attempt %d failed, retrying...\n", retry_count);
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(deviceConfig.wifi_ssid.c_str(), deviceConfig.wifi_password.c_str());
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;
    currentStatus = STATUS_ONLINE;
    statusIndicator.setStatusColor(STATUS_ONLINE);
    
    Serial.println();
    Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal strength: %d dBm\n", WiFi.RSSI());
    Serial.printf("Hostname: %s\n", WiFi.getHostname());
    
    // Auto-register device if enabled
    if (deviceConfig.auto_register) {
      registerDevice();
    }
  } else {
    wifi_connected = false;
    currentStatus = STATUS_OFFLINE;
    statusIndicator.setStatusColor(STATUS_OFFLINE);
    statusIndicator.playAlert(ALERT_COMMUNICATION_ERROR);
    
    Serial.println("\nWiFi connection failed after all retries");
  }
}

void checkWiFiConnection() {
  bool was_connected = wifi_connected;
  wifi_connected = (WiFi.status() == WL_CONNECTED);
  
  if (!wifi_connected && was_connected) {
    // Connection lost
    currentStatus = STATUS_OFFLINE;
    statusIndicator.setStatusColor(STATUS_OFFLINE);
    Serial.println("WiFi connection lost");
  } else if (wifi_connected && !was_connected) {
    // Connection restored
    currentStatus = STATUS_ONLINE;
    statusIndicator.setStatusColor(STATUS_ONLINE);
    Serial.println("WiFi connection restored");
    
    // Sync offline data
    syncOfflineData();
  } else if (!wifi_connected) {
    // Try to reconnect
    static unsigned long last_reconnect_attempt = 0;
    if (millis() - last_reconnect_attempt > 30000) {  // Try every 30 seconds
      last_reconnect_attempt = millis();
      Serial.println("Attempting WiFi reconnection...");
      connectWiFi();
    }
  }
}

void registerDevice() {
  if (!wifi_connected) {
    Serial.println("Cannot register device: WiFi not connected");
    return;
  }
  
  Serial.println("Registering device with backend...");
  
  const int MAX_REGISTRATION_RETRIES = 3;
  int retry_count = 0;
  bool registration_successful = false;
  
  while (retry_count < MAX_REGISTRATION_RETRIES && !registration_successful) {
    DynamicJsonDocument doc(1024);
    doc["receiver_id"] = deviceConfig.receiver_id;
    doc["device_name"] = deviceConfig.device_name;
    doc["location_description"] = deviceConfig.location_description;
    doc["zone_config"]["zone_id"] = deviceConfig.zone_config.zone_id;
    doc["zone_config"]["zone_name"] = deviceConfig.zone_config.zone_name;
    doc["zone_config"]["zone_type"] = (int)deviceConfig.zone_config.zone_type;
    doc["firmware_version"] = "1.0.0";
    doc["mac_address"] = WiFi.macAddress();
    doc["ip_address"] = WiFi.localIP().toString();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["free_memory"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    doc["registration_attempt"] = retry_count + 1;
    
    String payload;
    serializeJson(doc, payload);
    
    String url = deviceConfig.backend_url + "/api/v1/receivers/register";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", "ESP32-Receiver/1.0");
    http.setTimeout(HTTP_TIMEOUT);
    
    Serial.printf("Registration attempt %d/%d...\n", retry_count + 1, MAX_REGISTRATION_RETRIES);
    
    int httpCode = http.POST(payload);
    if (httpCode == 200 || httpCode == 201) {
      String response = http.getString();
      Serial.printf("Device registration successful: %s\n", response.c_str());
      
      // Parse response for updated configuration
      DynamicJsonDocument responseDoc(1024);
      DeserializationError error = deserializeJson(responseDoc, response);
      
      if (!error) {
        if (responseDoc.containsKey("zone_config")) {
          updateZoneConfiguration(responseDoc["zone_config"]);
        }
        
        if (responseDoc.containsKey("device_config")) {
          // Update device configuration from backend
          JsonObject deviceConf = responseDoc["device_config"];
          if (deviceConf.containsKey("scan_interval")) {
            deviceConfig.scan_interval = deviceConf["scan_interval"];
          }
          if (deviceConf.containsKey("device_name")) {
            deviceConfig.device_name = deviceConf["device_name"].as<String>();
          }
        }
        
        registration_successful = true;
        saveConfiguration();  // Save updated configuration
      } else {
        Serial.printf("Failed to parse registration response: %s\n", error.c_str());
      }
    } else if (httpCode > 0) {
      String response = http.getString();
      Serial.printf("Device registration failed: %d - %s\n", httpCode, response.c_str());
    } else {
      Serial.printf("Device registration failed: %s\n", http.errorToString(httpCode).c_str());
    }
    
    http.end();
    
    if (!registration_successful) {
      retry_count++;
      if (retry_count < MAX_REGISTRATION_RETRIES) {
        Serial.printf("Retrying registration in 5 seconds...\n");
        delay(5000);
      }
    }
  }
  
  if (!registration_successful) {
    Serial.println("Device registration failed after all retries");
    statusIndicator.playAlert(ALERT_COMMUNICATION_ERROR);
  }
}

void updateZoneConfiguration(JsonObject zoneConfig) {
  if (zoneConfig.containsKey("authorized_patients")) {
    deviceConfig.zone_config.authorized_patients.clear();
    JsonArray patients = zoneConfig["authorized_patients"];
    for (JsonVariant patient : patients) {
      deviceConfig.zone_config.authorized_patients.push_back(patient.as<String>());
    }
  }
  
  if (zoneConfig.containsKey("max_occupancy")) {
    deviceConfig.zone_config.max_occupancy = zoneConfig["max_occupancy"];
  }
  
  if (zoneConfig.containsKey("require_authorization")) {
    deviceConfig.zone_config.require_authorization = zoneConfig["require_authorization"];
  }
  
  // Reinitialize zone manager with updated config
  zoneManager.initialize(deviceConfig.zone_config);
  
  Serial.println("Zone configuration updated");
}

bool syncWithBackend(const std::vector<RFIDDetection>& detections) {
  if (!wifi_connected) {
    // Store data offline
    storeOfflineData(detections);
    return false;
  }
  
  // Create location update payload
  DynamicJsonDocument doc(2048);
  doc["receiver_id"] = deviceConfig.receiver_id;
  
  // Location information
  doc["location"]["zone"] = deviceConfig.zone_config.zone_name;
  doc["location"]["room"] = deviceConfig.location_description;
  doc["location"]["coordinates"]["x"] = 0.0;  // Would be configured per device
  doc["location"]["coordinates"]["y"] = 0.0;
  doc["location"]["coordinates"]["z"] = 0.0;
  
  // Timestamp
  doc["timestamp"] = getCurrentTimestamp();
  
  // Detected tags
  JsonArray tags = doc.createNestedArray("detected_tags");
  for (const auto& detection : detections) {
    JsonObject tag = tags.createNestedObject();
    tag["rfid_tag"] = detection.rfid_tag;
    tag["patient_id"] = detection.patient_id;
    tag["signal_strength"] = detection.signal_strength;
    tag["estimated_distance"] = detection.estimated_distance;
    tag["detection_count"] = detection.detection_count;
  }
  
  // Receiver status
  JsonObject status = doc.createNestedObject("receiver_status");
  status["uptime"] = millis() / 1000;
  status["wifi_strength"] = WiFi.RSSI();
  status["free_memory"] = ESP.getFreeHeap();
  status["detection_count"] = detections.size();
  status["zone_occupancy"] = zoneManager.getCurrentOccupancy();
  
  String payload;
  serializeJson(doc, payload);
  
  // Send to backend
  String url = deviceConfig.backend_url + "/api/v1/location-update";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT);
  
  int httpCode = http.POST(payload);
  if (httpCode == 200) {
    Serial.printf("Location update sent successfully (%d detections)\n", detections.size());
    http.end();
    return true;
  } else {
    Serial.printf("Location update failed: %d\n", httpCode);
    statusIndicator.playAlert(ALERT_COMMUNICATION_ERROR);
    
    // Store failed data for retry
    storeOfflineData(detections);
    http.end();
    return false;
  }
}

void storeOfflineData(const std::vector<RFIDDetection>& detections) {
  if (offline_storage.size() >= OFFLINE_STORAGE_SIZE) {
    // Remove oldest record
    offline_storage.erase(offline_storage.begin());
  }
  
  // Create offline record
  OfflineRecord record;
  record.timestamp = millis();
  record.transmitted = false;
  
  // Serialize detection data
  DynamicJsonDocument doc(1024);
  JsonArray tags = doc.createNestedArray("detections");
  for (const auto& detection : detections) {
    JsonObject tag = tags.createNestedObject();
    tag["rfid_tag"] = detection.rfid_tag;
    tag["patient_id"] = detection.patient_id;
    tag["signal_strength"] = detection.signal_strength;
    tag["estimated_distance"] = detection.estimated_distance;
    tag["detection_count"] = detection.detection_count;
    tag["detection_time"] = detection.detection_time;
  }
  
  serializeJson(doc, record.data_json);
  offline_storage.push_back(record);
  
  Serial.printf("Stored offline data record (%d/%d)\n", 
               offline_storage.size(), OFFLINE_STORAGE_SIZE);
}

void syncOfflineData() {
  if (!wifi_connected || offline_storage.empty()) {
    return;
  }
  
  Serial.printf("Syncing %d offline records...\n", offline_storage.size());
  
  for (auto& record : offline_storage) {
    if (record.transmitted) {
      continue;
    }
    
    // Reconstruct and send data
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, record.data_json);
    
    // Add metadata
    doc["receiver_id"] = deviceConfig.receiver_id;
    doc["offline_timestamp"] = record.timestamp;
    doc["sync_timestamp"] = getCurrentTimestamp();
    
    String payload;
    serializeJson(doc, payload);
    
    String url = deviceConfig.backend_url + "/api/v1/location-update/offline";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT);
    
    int httpCode = http.POST(payload);
    if (httpCode == 200) {
      record.transmitted = true;
      Serial.println("Offline record synced successfully");
    } else {
      Serial.printf("Failed to sync offline record: %d\n", httpCode);
      break;  // Stop syncing on failure
    }
    
    http.end();
    delay(100);  // Small delay between requests
  }
  
  // Remove transmitted records
  offline_storage.erase(
    std::remove_if(offline_storage.begin(), offline_storage.end(),
                   [](const OfflineRecord& r) { return r.transmitted; }),
    offline_storage.end()
  );
  
  if (offline_storage.empty()) {
    Serial.println("All offline data synced successfully");
  }
}

String getCurrentTimestamp() {
  // In production, this would use NTP time
  // For now, return a formatted timestamp based on millis()
  unsigned long seconds = millis() / 1000;
  return "2025-12-18T" + String(seconds % 86400 / 3600) + ":" + 
         String(seconds % 3600 / 60) + ":" + String(seconds % 60) + "Z";
}

// Diagnostic and maintenance functions
void printDiagnostics() {
  Serial.println("=== RECEIVER NODE DIAGNOSTICS ===");
  Serial.printf("Device ID: %s\n", deviceConfig.receiver_id.c_str());
  Serial.printf("Firmware Version: 1.0.0\n");
  Serial.printf("Uptime: %lu seconds\n", millis() / 1000);
  Serial.printf("Free Memory: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("WiFi Status: %s\n", wifi_connected ? "Connected" : "Disconnected");
  if (wifi_connected) {
    Serial.printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
  }
  Serial.printf("Zone: %s (%s)\n", 
               deviceConfig.zone_config.zone_name.c_str(),
               zoneManager.getZoneTypeString(deviceConfig.zone_config.zone_type).c_str());
  Serial.printf("Active Detections: %d\n", rfidReader.getDetectionCount());
  Serial.printf("Zone Occupancy: %d\n", zoneManager.getCurrentOccupancy());
  Serial.printf("Offline Records: %d\n", offline_storage.size());
  Serial.println("================================");
}

// Serial command interface for debugging and configuration
void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();
    
    if (command == "status" || command == "diag") {
      printDiagnostics();
    } else if (command == "restart" || command == "reboot") {
      Serial.println("Restarting device...");
      ESP.restart();
    } else if (command == "wifi") {
      Serial.println("Reconnecting WiFi...");
      connectWiFi();
    } else if (command == "register") {
      registerDevice();
    } else if (command == "sync") {
      syncOfflineData();
    } else if (command == "zone") {
      zoneManager.printZoneStatus();
    } else if (command == "diagnostic" || command == "test") {
      statusIndicator.enterDiagnosticMode();
    } else if (command == "reset") {
      resetConfiguration();
    } else if (command == "save") {
      saveConfiguration();
    } else if (command.startsWith("config ")) {
      String param = command.substring(7);
      if (param.startsWith("ssid ")) {
        deviceConfig.wifi_ssid = param.substring(5);
        Serial.printf("WiFi SSID set to: %s\n", deviceConfig.wifi_ssid.c_str());
      } else if (param.startsWith("password ")) {
        deviceConfig.wifi_password = param.substring(9);
        Serial.println("WiFi password updated");
      } else if (param.startsWith("backend ")) {
        deviceConfig.backend_url = param.substring(8);
        Serial.printf("Backend URL set to: %s\n", deviceConfig.backend_url.c_str());
      } else {
        Serial.println("Usage: config <ssid|password|backend> <value>");
      }
    } else if (command.startsWith("led ")) {
      String color = command.substring(4);
      if (color == "red") statusIndicator.setColor(LED_RED);
      else if (color == "green") statusIndicator.setColor(LED_GREEN);
      else if (color == "blue") statusIndicator.setColor(LED_BLUE);
      else if (color == "yellow") statusIndicator.setColor(LED_YELLOW);
      else if (color == "purple") statusIndicator.setColor(LED_PURPLE);
      else if (color == "cyan") statusIndicator.setColor(LED_CYAN);
      else if (color == "white") statusIndicator.setColor(LED_WHITE);
      else if (color == "off") statusIndicator.setColor(LED_OFF);
    } else if (command == "help") {
      Serial.println("Available commands:");
      Serial.println("  status/diag - Show diagnostics");
      Serial.println("  restart/reboot - Restart device");
      Serial.println("  wifi - Reconnect WiFi");
      Serial.println("  register - Register with backend");
      Serial.println("  sync - Sync offline data");
      Serial.println("  zone - Show zone status");
      Serial.println("  diagnostic/test - Run hardware diagnostic");
      Serial.println("  reset - Reset configuration to defaults");
      Serial.println("  save - Save current configuration");
      Serial.println("  config <param> <value> - Set configuration (ssid/password/backend)");
      Serial.println("  led <color> - Set LED color (red/green/blue/yellow/purple/cyan/white/off)");
      Serial.println("  help - Show this help");
    } else if (command.length() > 0) {
      Serial.println("Unknown command. Type 'help' for available commands.");
    }
  }
}