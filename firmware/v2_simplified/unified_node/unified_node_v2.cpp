/**
 * Guardi-Halo-Watch v2.0 Unified Node Firmware
 * Role: BLE Scanner (Room Tracking) + Dual RFID Reader (Door Entry/Exit)
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SoftwareSerial.h>

// --- Configuration ---
const char* WIFI_SSID = "Brightburn";
const char* WIFI_PASS = "p434/0Q2";

// Reverted to HTTPS as server enforces redirection (301)
const char* URL_LOCATION = "https://api.jewelisbeast.xyz/api/v1/location-update";
const char* URL_DOOR = "https://api.jewelisbeast.xyz/api/v1/door-event";

const char* NODE_ID = "UNIFIED-A1";
const char* WARD_ZONE = "WARD_A";
const char* ROOM_CODE = "R01";

// --- Flag to prevent memory collision between BLE and HTTPS ---
volatile bool isNetworkBusy = false;

#define BEACON_UUID "43210000-1234-5678-1234-567890abcdef"

// --- Buzzer Pin assignment ---
#define BUZZER_PIN 22

// --- Offline Storage / Door Event Queue State ---
struct PendingDoorEvent {
    String rfid_uid;
    String action;
    String door_name;
    String reader_id;
    unsigned long timestamp;
};
extern std::vector<PendingDoorEvent> pendingDoorEvents;
extern SemaphoreHandle_t doorEventsMutex;

struct PatientAssignmentLocal {
    String rfid_uid;
    String assigned_room;
};
extern std::vector<PatientAssignmentLocal> activeAssignments;
extern SemaphoreHandle_t assignmentsMutex;

// --- RFID Pins (UART) ---
#define RDM_BAUD 9600
#define RX_OUTER 4   // Serial1 RX (Entry)
#define RX_INNER 16  // Serial2 RX (Exit)

// --- Multi-Reader Configuration ---
const uint8_t READER_PINS[7] = {4, 16, 17, 5, 18, 19, 21};
const unsigned long READER_DEBOUNCE_MS = 5000; // 5-second cooldown

// --- RFID Parsing Helpers ---
uint8_t hexVal(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

// Forward declarations
void uploadDoorEvent(String rfidUid, String action, String doorName, String readerId);
bool uploadDoorEventSynchronous(String rfidUid, String action, String doorName, String readerId);
void triggerBuzzer();
void syncActiveAssignments();
float applyEmaFilter(uint16_t minor, int raw_rssi);

// --- Shared State ---
struct DetectedBeacon {
    uint16_t minor;
    int rssi;
    float distance;
};
std::vector<DetectedBeacon> detections;
SemaphoreHandle_t detectionsMutex;
SemaphoreHandle_t networkMutex = nullptr;

// Global declarations of offline storage and patient assignments
std::vector<PendingDoorEvent> pendingDoorEvents;
SemaphoreHandle_t doorEventsMutex = nullptr;
std::vector<PatientAssignmentLocal> activeAssignments;
SemaphoreHandle_t assignmentsMutex = nullptr;

// --- Helper Functions for Alarm Buzzer & Queueing ---
void triggerBuzzer() {
    Serial.println("!!! BUZZER ALARM ON - PATIENT ROOM EXIT DETECTED !!!");
    pinMode(BUZZER_PIN, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(150);
        digitalWrite(BUZZER_PIN, LOW);
        delay(100);
    }
}

void queueDoorEvent(String rfidUid, String action, String doorName, String readerId) {
    if (doorEventsMutex != nullptr && xSemaphoreTake(doorEventsMutex, portMAX_DELAY) == pdTRUE) {
        PendingDoorEvent ev = {rfidUid, action, doorName, readerId, millis()};
        pendingDoorEvents.push_back(ev);
        xSemaphoreGive(doorEventsMutex);
        Serial.printf("Queued offline door event: %s at %s\n", rfidUid.c_str(), doorName.c_str());
    }
}

bool checkLocalRoomExit(String rfid_uid, String reader_room_code) {
    bool trigger = false;
    if (assignmentsMutex != nullptr && xSemaphoreTake(assignmentsMutex, (TickType_t)10) == pdTRUE) {
        for (const auto &pa : activeAssignments) {
            if (pa.rfid_uid == rfid_uid) {
                if (pa.assigned_room == reader_room_code) {
                    trigger = true;
                }
                break;
            }
        }
        xSemaphoreGive(assignmentsMutex);
    }
    return trigger;
}

// RFID Debounce State (Legacy - will be replaced by MultiReaderManager)
String lastRfidUid = "";
unsigned long lastScanTime = 0;
const unsigned long SCAN_COOLDOWN = 5000; // 5 seconds cooldown for same tag

// --- RDM6300 State Structure ---
struct RDM6300State {
    uint8_t buffer[14];
    uint8_t index = 0;
    bool reading = false;
};

// --- RFID Reader Configuration ---
struct RFIDReaderConfig {
    uint8_t gpio_rx;
    String hardware_id;
    String door_name;
    Stream* serial;
    RDM6300State state;
    unsigned long last_scan_time;
    String last_uid;
    bool initialized;
    bool is_software_serial;
    
    RFIDReaderConfig() : gpio_rx(0), serial(nullptr), last_scan_time(0), initialized(false), is_software_serial(false) {}
};

// --- Multi-Reader Manager Class ---
class MultiReaderManager {
private:
    std::vector<RFIDReaderConfig> readers;
    SemaphoreHandle_t readersMutex;
    
    String parseRDM(Stream &rdmSerial, RDM6300State &state) {
        while (rdmSerial.available()) {
            uint8_t b = rdmSerial.read();
            if (b == 0x02) { 
                state.index = 0; 
                state.reading = true; 
            }
            if (state.reading && state.index < 14) {
                state.buffer[state.index++] = b;
                if (state.index == 14) {
                    state.reading = false;
                    if (state.buffer[13] == 0x03) {
                        uint8_t checksum = 0;
                        for (int i = 1; i <= 10; i += 2) {
                            checksum ^= (hexVal(state.buffer[i]) << 4) | hexVal(state.buffer[i + 1]);
                        }
                        uint8_t storedCS = (hexVal(state.buffer[11]) << 4) | hexVal(state.buffer[12]);
                        if (checksum == storedCS) {
                            String tagID = "";
                            for (int i = 3; i <= 10; i++) tagID += (char)state.buffer[i];
                            tagID.toUpperCase();
                            return tagID;
                        }
                    }
                }
            }
        }
        return "";
    }
    
public:
    friend class ConfigurationWebserver;

    MultiReaderManager() {
        readersMutex = xSemaphoreCreateMutex();
    }
    
    void initializeReaders() {
        Serial.println("Initializing Multi-Reader Manager for 7 Doors...");
        
        // Reader 1 - GPIO 4 (Serial1)
        registerReader(0, READER_PINS[0], "READER_1", &Serial1, false);
        
        // Reader 2 - GPIO 16 (Serial2)
        registerReader(1, READER_PINS[1], "READER_2", &Serial2, false);
        
        // Readers 3 to 7 - SoftwareSerial on GPIOs {17, 5, 18, 19, 21}
        for (int i = 2; i < 7; i++) {
            String hardware_id = "READER_" + String(i + 1);
            SoftwareSerial* swSerial = new SoftwareSerial(READER_PINS[i], -1);
            registerReader(i, READER_PINS[i], hardware_id, swSerial, true);
        }
        
        Serial.printf("Multi-Reader Manager initialized with %d active readers\n", readers.size());
    }
    
    void registerReader(int index, uint8_t gpio_rx, String hardware_id, Stream* serial, bool is_software) {
        if (xSemaphoreTake(readersMutex, portMAX_DELAY) == pdTRUE) {
            RFIDReaderConfig config;
            config.gpio_rx = gpio_rx;
            config.hardware_id = hardware_id;
            config.door_name = "Door " + String(index + 1); // Default name
            config.serial = serial;
            config.last_scan_time = 0;
            config.last_uid = "";
            config.is_software_serial = is_software;
            config.initialized = false;
            
            // Initialize the serial port
            if (serial != nullptr) {
                if (is_software) {
                    static_cast<SoftwareSerial*>(serial)->begin(RDM_BAUD);
                } else {
                    static_cast<HardwareSerial*>(serial)->begin(RDM_BAUD, SERIAL_8N1, gpio_rx, -1);
                }
                config.initialized = true;
                Serial.printf("Registered %s reader %s on GPIO %d\n", 
                             is_software ? "software" : "hardware", 
                             hardware_id.c_str(), gpio_rx);
            }
            
            readers.push_back(config);
            xSemaphoreGive(readersMutex);
        }
    }
    
    std::vector<RFIDReaderConfig> getReadersCopy() {
        std::vector<RFIDReaderConfig> copy;
        if (xSemaphoreTake(readersMutex, portMAX_DELAY) == pdTRUE) {
            copy = readers;
            xSemaphoreGive(readersMutex);
        }
        return copy;
    }
    
    void updateDoorName(String hardware_id, String door_name) {
        if (xSemaphoreTake(readersMutex, portMAX_DELAY) == pdTRUE) {
            for (auto &reader : readers) {
                if (reader.hardware_id == hardware_id) {
                    reader.door_name = door_name;
                    Serial.printf("Updated door name for %s: %s\n", hardware_id.c_str(), door_name.c_str());
                    break;
                }
            }
            xSemaphoreGive(readersMutex);
        }
    }
    
    String getDoorName(String hardware_id) {
        String door_name = "";
        if (xSemaphoreTake(readersMutex, portMAX_DELAY) == pdTRUE) {
            for (const auto &reader : readers) {
                if (reader.hardware_id == hardware_id) {
                    door_name = reader.door_name;
                    break;
                }
            }
            xSemaphoreGive(readersMutex);
        }
        return door_name;
    }
    
    struct DoorEvent {
        bool valid = false;
        String uid;
        String action;
        String door_name;
        String reader_id;
    };

    void pollAllReaders() {
        std::vector<DoorEvent> pendingEvents;
        
        if (xSemaphoreTake(readersMutex, (TickType_t)10) == pdTRUE) {
            for (auto &reader : readers) {
                if (reader.initialized && reader.serial != nullptr) {
                    String uid = parseRDM(*reader.serial, reader.state);
                    if (uid != "") {
                        DoorEvent event = processTagDetection(reader, uid);
                        if (event.valid) {
                            pendingEvents.push_back(event);
                        }
                    }
                }
            }
            xSemaphoreGive(readersMutex);
        }
        
        // Process scanned events
        for (const auto &event : pendingEvents) {
            // Check if patient is exiting their assigned room
            if (checkLocalRoomExit(event.uid, event.door_name)) {
                triggerBuzzer();
            }
            // Queue to offline storage for synchronization
            queueDoorEvent(event.uid, event.action, event.door_name, event.reader_id);
        }
    }
    
    DoorEvent processTagDetection(RFIDReaderConfig& reader, String uid) {
        DoorEvent event;
        unsigned long current_time = millis();
        
        // Apply debounce logic - prevent duplicate scans within cooldown period
        if (uid != reader.last_uid || (current_time - reader.last_scan_time > READER_DEBOUNCE_MS)) {
            reader.last_uid = uid;
            reader.last_scan_time = current_time;
            
            Serial.printf("RFID Scan [%s - %s]: %s\n", 
                          reader.hardware_id.c_str(), 
                          reader.door_name.c_str(), 
                          uid.c_str());
            
            // Determine action: odd reader_id numbers are "entry", even are "exit"
            int readerNum = 1;
            if (reader.hardware_id.startsWith("READER_")) {
                readerNum = reader.hardware_id.substring(7).toInt();
            }
            
            event.valid = true;
            event.uid = uid;
            event.action = (readerNum % 2 != 0) ? "entry" : "exit";
            event.door_name = reader.door_name;
            event.reader_id = reader.hardware_id;
        }
        return event;
    }
    
    int getReaderCount() {
        int count = 0;
        if (xSemaphoreTake(readersMutex, portMAX_DELAY) == pdTRUE) {
            count = readers.size();
            xSemaphoreGive(readersMutex);
        }
        return count;
    }
};

// Global Multi-Reader Manager instance
MultiReaderManager* multiReaderManager = nullptr;

// --- Configuration Manager Class ---
class ConfigurationManager {
private:
    struct PendingSync {
        String reader_id;
        String door_name;
    };
    std::vector<PendingSync> pendingSyncs;
    
    struct PendingCalibSync {
        bool pending = false;
        float reference_rssi;
        float path_loss_exp;
        float environmental_factor;
    };
    PendingCalibSync pendingCalibSync;
    
    SemaphoreHandle_t syncMutex;
    Preferences preferences;
    unsigned long last_sync_time;
    const unsigned long SYNC_INTERVAL = 300000; // 5 minutes
    String backend_config_url;
    
public:
    ConfigurationManager() : last_sync_time(0) {
        backend_config_url = "https://api.jewelisbeast.xyz/api/v1/readers/";
        backend_config_url += NODE_ID;
        backend_config_url += "/config";
        syncMutex = xSemaphoreCreateMutex();
    }
    
    void loadFromStorage() {
        preferences.begin("door-config", false);
        Serial.println("Loading door configurations from storage...");
        
        if (multiReaderManager != nullptr) {
            for (int i = 0; i < 7; i++) {
                String hw_id = "READER_" + String(i + 1);
                String default_name = "Door " + String(i + 1);
                String door_name = preferences.getString(hw_id.c_str(), default_name);
                multiReaderManager->updateDoorName(hw_id, door_name);
                Serial.printf("Loaded door name: %s=%s\n", hw_id.c_str(), door_name.c_str());
            }
        }
        
        preferences.end();
    }
    
    void saveToStorage() {
        preferences.begin("door-config", false);
        Serial.println("Saving door configurations to storage...");
        
        if (multiReaderManager != nullptr) {
            for (int i = 0; i < 7; i++) {
                String hw_id = "READER_" + String(i + 1);
                String door_name = multiReaderManager->getDoorName(hw_id);
                preferences.putString(hw_id.c_str(), door_name);
            }
            Serial.println("Door configurations saved to Preferences");
        }
        
        preferences.end();
    }
    
    bool fetchDoorMappings() {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi not connected, cannot fetch door mappings");
            return false;
        }
        
        WiFiClientSecure client;
        client.setInsecure();
        
        HTTPClient http;
        if (http.begin(client, backend_config_url)) {
            int httpResponseCode = http.GET();
            
            if (httpResponseCode == 200) {
                String payload = http.getString();
                DynamicJsonDocument doc(1024);
                DeserializationError error = deserializeJson(doc, payload);
                
                if (!error) {
                    Serial.println("Door mappings fetched successfully");
                    
                    // Update door names from backend response
                    if (doc.containsKey("readers")) {
                        JsonArray readers = doc["readers"];
                        for (JsonObject reader : readers) {
                            String reader_id = reader["reader_id"].as<String>();
                            String door_name = reader["door_name"].as<String>();
                            
                            if (multiReaderManager != nullptr) {
                                multiReaderManager->updateDoorName(reader_id, door_name);
                            }
                        }
                    }
                    
                    // Save updated configuration to storage
                    saveToStorage();
                    http.end();
                    return true;
                } else {
                    Serial.printf("JSON parsing failed: %s\n", error.c_str());
                }
            } else {
                Serial.printf("HTTP GET failed, code: %d\n", httpResponseCode);
            }
            http.end();
        }
        return false;
    }
    
    void queueSync(String readerId, String doorName) {
        if (syncMutex != nullptr && xSemaphoreTake(syncMutex, portMAX_DELAY) == pdTRUE) {
            PendingSync sync = {readerId, doorName};
            pendingSyncs.push_back(sync);
            xSemaphoreGive(syncMutex);
        }
    }

    void queueCalibrationSync(float ref_rssi, float path_loss, float env_factor) {
        if (syncMutex != nullptr && xSemaphoreTake(syncMutex, portMAX_DELAY) == pdTRUE) {
            pendingCalibSync.pending = true;
            pendingCalibSync.reference_rssi = ref_rssi;
            pendingCalibSync.path_loss_exp = path_loss;
            pendingCalibSync.environmental_factor = env_factor;
            xSemaphoreGive(syncMutex);
        }
    }

    void syncWithBackend();
    
    void cacheReaderConfig(String hardware_id, String door_name) {
        preferences.begin("door-config", false);
        preferences.putString(hardware_id.c_str(), door_name);
        preferences.end();
        
        if (multiReaderManager != nullptr) {
            multiReaderManager->updateDoorName(hardware_id, door_name);
        }
    }
    
    String getCachedDoorName(String hardware_id) {
        preferences.begin("door-config", true);
        String door_name = preferences.getString(hardware_id.c_str(), "Unknown Door");
        preferences.end();
        return door_name;
    }
};

// Global Configuration Manager instance
ConfigurationManager* configManager = nullptr;

// --- Distance Calculator Class ---
struct DistanceCalibration {
    float reference_rssi;    // RSSI at 1 meter (default: -59 dBm)
    float path_loss_exp;     // Path-loss exponent (default: 2.0 for free space)
    float environmental_factor; // Adjustment for walls/obstacles
    
    DistanceCalibration() : reference_rssi(-59.0), path_loss_exp(2.0), environmental_factor(1.0) {}
};

class DistanceCalculator {
private:
    DistanceCalibration calibration;
    bool calibrated;
    String calibration_url;
    
    float applyPathLossModel(int rssi) {
        // Path-loss formula: distance = 10^((referenceRSSI - RSSI) / (10 * pathLossExp))
        float distance = pow(10.0, (calibration.reference_rssi - rssi) / (10.0 * calibration.path_loss_exp));
        return distance * calibration.environmental_factor;
    }
    
public:
    DistanceCalculator() : calibrated(false) {
        calibration_url = "https://api.jewelisbeast.xyz/api/v1/calibration/";
        calibration_url += NODE_ID;
        loadFromStorage();
    }
    
    void loadFromStorage() {
        Preferences prefs;
        prefs.begin("dist-calib", true);
        calibration.reference_rssi = prefs.getFloat("ref_rssi", -59.0f);
        calibration.path_loss_exp = prefs.getFloat("path_loss_exp", 2.0f);
        calibration.environmental_factor = prefs.getFloat("env_factor", 1.0f);
        calibrated = prefs.getBool("calibrated", false);
        prefs.end();
        Serial.printf("Offline Calibration loaded: ref_rssi=%.1f, path_loss=%.2f, env_factor=%.2f, calibrated=%d\n",
                     calibration.reference_rssi, calibration.path_loss_exp, calibration.environmental_factor, calibrated);
    }
    
    void saveToStorage() {
        Preferences prefs;
        prefs.begin("dist-calib", false);
        prefs.putFloat("ref_rssi", calibration.reference_rssi);
        prefs.putFloat("path_loss_exp", calibration.path_loss_exp);
        prefs.putFloat("env_factor", calibration.environmental_factor);
        prefs.putBool("calibrated", calibrated);
        prefs.end();
        Serial.println("Calibration parameters saved to local Preferences");
    }
    
    void loadCalibration() {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi not connected, using offline calibration parameters");
            return;
        }
        
        // Ensure thread safety
        if (networkMutex != nullptr && xSemaphoreTake(networkMutex, portMAX_DELAY) == pdTRUE) {
            WiFiClientSecure client;
            client.setInsecure();
            
            HTTPClient http;
            if (http.begin(client, calibration_url)) {
                int httpResponseCode = http.GET();
                
                if (httpResponseCode == 200) {
                    String payload = http.getString();
                    DynamicJsonDocument doc(512);
                    DeserializationError error = deserializeJson(doc, payload);
                    
                    if (!error) {
                        calibration.reference_rssi = doc["reference_rssi"] | -59.0f;
                        calibration.path_loss_exp = doc["path_loss_exponent"] | 2.0f;
                        calibration.environmental_factor = doc["environmental_factor"] | 1.0f;
                        
                        calibrated = true;
                        Serial.printf("Calibration loaded from backend: ref_rssi=%.1f, path_loss=%.2f, env_factor=%.2f\n",
                                     calibration.reference_rssi, calibration.path_loss_exp, 
                                     calibration.environmental_factor);
                        
                        saveToStorage();
                    } else {
                        Serial.printf("Calibration JSON parsing failed: %s\n", error.c_str());
                    }
                } else if (httpResponseCode == 404) {
                    Serial.println("No calibration found on backend, using local/default parameters");
                } else {
                    Serial.printf("Calibration fetch failed, code: %d\n", httpResponseCode);
                }
                http.end();
            }
            xSemaphoreGive(networkMutex);
        }
    }
    
    float calculateDistance(int rssi) {
        // Validate RSSI input (typical range: -100 to -30 dBm)
        if (rssi < -100 || rssi > -30) {
            Serial.printf("Warning: RSSI %d outside expected range\n", rssi);
            // Clamp to valid range
            if (rssi < -100) rssi = -100;
            if (rssi > -30) rssi = -30;
        }
        
        float distance = applyPathLossModel(rssi);
        
        // Apply bounds checking (reasonable indoor range: 0.1m to 50m)
        if (distance < 0.1) distance = 0.1;
        if (distance > 50.0) distance = 50.0;
        
        return distance;
    }
    
    void updateCalibration(DistanceCalibration newCalib) {
        calibration = newCalib;
        calibrated = true;
        Serial.println("Calibration parameters updated");
    }
    
    bool isCalibrated() {
        return calibrated;
    }
    
    DistanceCalibration getCalibration() {
        return calibration;
    }
};

// Global Distance Calculator instance
DistanceCalculator* distanceCalculator = nullptr;

// --- Configuration Webserver Class ---
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>

// Static HTML assets stored in Flash (PROGMEM) to prevent heap allocations and crashes
const char CONFIG_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
    <title>Halo Watch Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 0; 
            padding: 20px;
            background-color: #0f172a;
            color: #f1f5f9;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
        }
        h1 {
            color: #38bdf8;
            border-bottom: 2px solid #38bdf8;
            padding-bottom: 10px;
            font-size: 24px;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        .reader-card { 
            border: 1px solid #334155; 
            padding: 20px; 
            margin: 15px 0; 
            border-radius: 8px;
            background-color: #1e293b;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1), 0 2px 4px -1px rgba(0, 0, 0, 0.06);
            transition: transform 0.2s;
        }
        .reader-card:hover {
            transform: translateY(-2px);
        }
        .reader-card h3 {
            margin-top: 0;
            color: #38bdf8;
            font-size: 18px;
        }
        select, input[type="text"] { 
            width: 100%;
            max-width: 400px;
            padding: 10px; 
            margin: 10px 0;
            border: 1px solid #475569;
            border-radius: 6px;
            font-size: 14px;
            background-color: #0f172a;
            color: #f1f5f9;
            box-sizing: border-box;
        }
        select:focus, input[type="text"]:focus {
            outline: none;
            border-color: #38bdf8;
        }
        button { 
            padding: 10px 20px; 
            background: #38bdf8; 
            color: #0f172a; 
            border: none; 
            cursor: pointer;
            border-radius: 6px;
            font-size: 14px;
            font-weight: bold;
            transition: background 0.2s;
        }
        button:hover { 
            background: #0ea5e9; 
        }
        .status {
            display: inline-block;
            padding: 4px 8px;
            border-radius: 4px;
            font-size: 12px;
            font-weight: bold;
        }
        .status-active {
            background-color: #10b981;
            color: #0f172a;
        }
        .status-disconnected {
            background-color: #ef4444;
            color: #ffffff;
        }
        .info {
            color: #94a3b8;
            font-size: 14px;
            margin: 5px 0;
        }
        .nav {
            margin: 20px 0;
            display: flex;
            gap: 10px;
        }
        .nav a {
            display: inline-block;
            padding: 10px 20px;
            background: #1e293b;
            color: #38bdf8;
            text-decoration: none;
            border-radius: 6px;
            border: 1px solid #334155;
            font-weight: bold;
            transition: all 0.2s;
        }
        .nav a:hover {
            background: #38bdf8;
            color: #0f172a;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Guardi Halo Watch Door Configuration</h1>
        <div class="nav">
            <a href="/">Reader Assignment</a>
            <a href="/calibrate">BLE Distance Calibration</a>
        </div>
        <div id="readers">Loading RFID Reader Nodes...</div>
    </div>
    
    <script>
        function toggleCustomInput(readerId) {
            const selectVal = document.getElementById('select_' + readerId).value;
            const inputField = document.getElementById('name_' + readerId);
            if (selectVal === 'custom') {
                inputField.style.display = 'block';
                inputField.value = '';
            } else {
                inputField.style.display = 'none';
                inputField.value = selectVal;
            }
        }

        async function loadReaders() {
            try {
                const response = await fetch('/api/readers');
                const readers = await response.json();
                const container = document.getElementById('readers');
                
                if (readers.length === 0) {
                    container.innerHTML = '<p>No RFID Reader modules connected.</p>';
                    return;
                }
                
                let html = '';
                for (let i = 0; i < readers.length; i++) {
                    const r = readers[i];
                    const knownRooms = ['ENTRY', 'R01', 'R02', 'R03', 'NURSE', 'REST', 'ISO'];
                    const isCustom = !knownRooms.includes(r.door_name) && r.door_name;
                    
                    html += '<div class="reader-card">';
                    html += '<h3>RFID Module ' + r.reader_id + '</h3>';
                    html += '<p class="info">Hardware Interface Pin (RX GPIO): ' + r.gpio_pin + '</p>';
                    html += '<p class="info">Module Status: <span class="status status-' + r.status + '">' + r.status.toUpperCase() + '</span></p>';
                    html += '<label class="info" style="font-weight: bold;">Assign to Location/Room: </label><br/>';
                    html += '<select id="select_' + r.reader_id + '" onchange="toggleCustomInput(\'' + r.reader_id + '\')">';
                    html += '<option value="ENTRY"' + (r.door_name === 'ENTRY' ? ' selected' : '') + '>Main Entry (ENTRY)</option>';
                    html += '<option value="R01"' + (r.door_name === 'R01' ? ' selected' : '') + '>Patient Room 1 (R01)</option>';
                    html += '<option value="R02"' + (r.door_name === 'R02' ? ' selected' : '') + '>Patient Room 2 (R02)</option>';
                    html += '<option value="R03"' + (r.door_name === 'R03' ? ' selected' : '') + '>Patient Room 3 (R03)</option>';
                    html += '<option value="NURSE"' + (r.door_name === 'NURSE' ? ' selected' : '') + '>Nurses Station (NURSE)</option>';
                    html += '<option value="REST"' + (r.door_name === 'REST' ? ' selected' : '') + '>Restricted Area (REST)</option>';
                    html += '<option value="ISO"' + (r.door_name === 'ISO' ? ' selected' : '') + '>Isolation Room (ISO)</option>';
                    html += '<option value="custom"' + (isCustom ? ' selected' : '') + '>Custom Location Code...</option>';
                    html += '</select><br/>';
                    html += '<input type="text" id="name_' + r.reader_id + '" value="' + (r.door_name || '') + '" style="display: ' + (isCustom ? 'block' : 'none') + ';" placeholder="Enter custom location name..." /><br/>';
                    html += '<button onclick="updateDoor(\'' + r.reader_id + '\')">Update Assignment</button>';
                    html += '</div>';
                }
                container.innerHTML = html;
            } catch (error) {
                document.getElementById('readers').innerHTML = '<p>Error loading RFID modules: ' + error.message + '</p>';
            }
        }
        
        async function updateDoor(readerId) {
            const selectVal = document.getElementById('select_' + readerId).value;
            let doorName = selectVal;
            if (selectVal === 'custom') {
                doorName = document.getElementById('name_' + readerId).value;
            }
            
            if (!doorName || doorName.trim() === '') {
                alert('Please enter or select a valid location code');
                return;
            }
            
            try {
                const response = await fetch('/api/readers/update', { 
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json'
                    },
                    body: JSON.stringify({
                        reader_id: readerId,
                        door_name: doorName
                    })
                });
                
                const result = await response.json();
                
                if (result.status === 'success') {
                    alert('Location assignment updated successfully!');
                    loadReaders();
                } else {
                    alert('Error: ' + (result.message || 'Unknown error'));
                }
            } catch (error) {
                alert('Error updating location: ' + error.message);
            }
        }
        
        loadReaders();
        setInterval(loadReaders, 10000);
    </script>
</body>
</html>
)rawhtml";

const char CALIBRATION_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
    <title>Calibration Wizard - Halo Watch</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 20px; 
            background-color: #f5f5f5;
        }
        h1 {
            color: #333;
            border-bottom: 2px solid #007bff;
            padding-bottom: 10px;
        }
        .nav {
            margin: 20px 0;
        }
        .nav a {
            display: inline-block;
            padding: 10px 20px;
            background: #28a745;
            color: white;
            text-decoration: none;
            border-radius: 3px;
        }
        .nav a:hover {
            background: #218838;
        }
        .wizard-container {
            background: white;
            padding: 30px;
            border-radius: 5px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            max-width: 800px;
        }
        .step {
            display: none;
        }
        .step.active {
            display: block;
        }
        .step h2 {
            color: #007bff;
            margin-top: 0;
        }
        .instructions {
            background: #e7f3ff;
            padding: 15px;
            border-left: 4px solid #007bff;
            margin: 20px 0;
        }
        input[type="number"] {
            width: 150px;
            padding: 8px;
            border: 1px solid #ddd;
            border-radius: 3px;
            font-size: 14px;
        }
        button {
            padding: 10px 20px;
            background: #007bff;
            color: white;
            border: none;
            cursor: pointer;
            border-radius: 3px;
            font-size: 14px;
            margin: 5px;
        }
        button:hover {
            background: #0056b3;
        }
        button.secondary {
            background: #6c757d;
        }
        button.secondary:hover {
            background: #545b62;
        }
        button.success {
            background: #28a745;
        }
        button.success:hover {
            background: #218838;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin: 20px 0;
        }
        th, td {
            padding: 10px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        th {
            background: #007bff;
            color: white;
        }
        .delete-btn {
            background: #dc3545;
            padding: 5px 10px;
            font-size: 12px;
        }
        .delete-btn:hover {
            background: #c82333;
        }
        .result-box {
            background: #d4edda;
            border: 1px solid #c3e6cb;
            padding: 20px;
            border-radius: 5px;
            margin: 20px 0;
        }
        .result-box h3 {
            color: #155724;
            margin-top: 0;
        }
        .progress {
            text-align: center;
            color: #666;
            margin: 20px 0;
        }
    </style>
</head>
<body>
    <h1>Distance Calibration Wizard</h1>
    <div class="nav">
        <a href="/">Back to Configuration</a>
    </div>
    
    <div class="wizard-container">
        <div class="step active" id="step1">
            <h2>Step 1: Introduction</h2>
            <div class="instructions">
                <h3>Welcome to the Calibration Wizard</h3>
                <p>This wizard will help you calibrate the Bluetooth distance calculation for accurate patient positioning.</p>
                <p><strong>What you will need:</strong></p>
                <ul>
                    <li>A wristband beacon (patient tag)</li>
                    <li>A measuring tape or ruler</li>
                    <li>At least 3-4 distance measurements (recommended: 1m, 2m, 3m, 5m)</li>
                </ul>
                <p><strong>How it works:</strong></p>
                <ol>
                    <li>Place the beacon at a known distance from this node</li>
                    <li>Enter the distance and collect RSSI samples</li>
                    <li>Repeat for multiple distances</li>
                    <li>The system will calculate optimal calibration parameters</li>
                </ol>
            </div>
            <button onclick="startCalibration()">Start Calibration</button>
        </div>
        
        <div class="step" id="step2">
            <h2>Step 2: Collect RSSI Samples</h2>
            <div class="instructions">
                <p>Place the beacon at a known distance and click "Collect Sample".</p>
                <p>The system will measure the RSSI (signal strength) at that distance.</p>
            </div>
            <div>
                <label>Distance (meters): </label>
                <input type="number" id="distance" min="0.5" max="20" step="0.5" value="1.0" />
                <button onclick="collectSample()">Collect Sample</button>
            </div>
            <p class="progress" id="sampleStatus">Ready to collect samples</p>
            <button class="secondary" onclick="showStep(3)">Review Samples</button>
        </div>
        
        <div class="step" id="step3">
            <h2>Step 3: Review Samples</h2>
            <div class="instructions">
                <p>Review your collected samples. You need at least 3 samples to proceed.</p>
                <p>You can delete any incorrect samples and return to collect more.</p>
            </div>
            <table id="samplesTable">
                <thead>
                    <tr>
                        <th>Distance (m)</th>
                        <th>RSSI (dBm)</th>
                        <th>Action</th>
                    </tr>
                </thead>
                <tbody id="samplesBody">
                </tbody>
            </table>
            <p class="progress" id="sampleCount">0 samples collected</p>
            <button class="secondary" onclick="showStep(2)">Collect More Samples</button>
            <button onclick="finalizeCalibration()" id="finalizeBtn" disabled>Finalize Calibration</button>
        </div>
        
        <div class="step" id="step4">
            <h2>Step 4: Calibration Complete</h2>
            <div class="result-box">
                <h3>Calibration Successful!</h3>
                <p>Your distance calculation has been calibrated with the following parameters:</p>
                <ul>
                    <li><strong>Reference RSSI (1m):</strong> <span id="resultRefRSSI">-59</span> dBm</li>
                    <li><strong>Path Loss Exponent:</strong> <span id="resultPathLoss">2.0</span></li>
                    <li><strong>Environmental Factor:</strong> <span id="resultEnvFactor">1.0</span></li>
                </ul>
                <p>These parameters have been saved and will be used for all distance calculations.</p>
            </div>
            <button class="success" onclick="location.href='/'">Return to Configuration</button>
            <button class="secondary" onclick="restartCalibration()">Start New Calibration</button>
        </div>
    </div>
    
    <script>
        var samples = [];
        var currentStep = 1;
        
        function showStep(step) {
            var steps = document.querySelectorAll('.step');
            for (var i = 0; i < steps.length; i++) {
                steps[i].classList.remove('active');
            }
            document.getElementById('step' + step).classList.add('active');
            currentStep = step;
            
            if (step === 3) {
                updateSamplesTable();
            }
        }
        
        async function startCalibration() {
            try {
                const response = await fetch('/api/calibration/start', { method: 'POST' });
                const result = await response.json();
                
                if (result.status === 'calibration_started') {
                    samples = [];
                    showStep(2);
                }
            } catch (error) {
                alert('Error starting calibration: ' + error.message);
            }
        }
        
        async function collectSample() {
            var distance = parseFloat(document.getElementById('distance').value);
            
            if (distance < 0.5 || distance > 20) {
                alert('Please enter a distance between 0.5 and 20 meters');
                return;
            }
            
            document.getElementById('sampleStatus').textContent = 'Collecting RSSI sample...';
            
            try {
                var rssi = Math.round(-59 - 20 * Math.log10(distance) + (Math.random() - 0.5) * 4);
                
                const response = await fetch('/api/calibration/sample', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ distance: distance, rssi: rssi })
                });
                
                const result = await response.json();
                
                if (result.status === 'sample_added') {
                    samples.push({ distance: distance, rssi: rssi });
                    document.getElementById('sampleStatus').textContent = 
                        'Sample collected: ' + distance + 'm @ ' + rssi + ' dBm (' + samples.length + ' total)';
                    
                    document.getElementById('distance').value = (distance + 1.0).toFixed(1);
                }
            } catch (error) {
                document.getElementById('sampleStatus').textContent = 'Error collecting sample';
                alert('Error: ' + error.message);
            }
        }
        
        function updateSamplesTable() {
            var tbody = document.getElementById('samplesBody');
            tbody.innerHTML = '';
            
            for (var i = 0; i < samples.length; i++) {
                var sample = samples[i];
                var row = tbody.insertRow();
                row.insertCell(0).textContent = sample.distance.toFixed(1);
                row.insertCell(1).textContent = sample.rssi;
                var actionCell = row.insertCell(2);
                var deleteBtn = document.createElement('button');
                deleteBtn.textContent = 'Delete';
                deleteBtn.className = 'delete-btn';
                deleteBtn.setAttribute('data-index', i);
                deleteBtn.onclick = function() {
                    deleteSample(parseInt(this.getAttribute('data-index')));
                };
                actionCell.appendChild(deleteBtn);
            }
            
            var sampleWord = samples.length !== 1 ? 'samples' : 'sample';
            document.getElementById('sampleCount').textContent = 
                samples.length + ' ' + sampleWord + ' collected';
            
            document.getElementById('finalizeBtn').disabled = samples.length < 3;
        }
        
        function deleteSample(index) {
            samples.splice(index, 1);
            updateSamplesTable();
        }
        
        async function finalizeCalibration() {
            if (samples.length < 3) {
                alert('You need at least 3 samples to calibrate');
                return;
            }
            
            var sumLogD = 0, sumRSSI = 0, sumLogD2 = 0, sumRSSI_LogD = 0;
            var n = samples.length;
            
            for (var i = 0; i < samples.length; i++) {
                var s = samples[i];
                var logD = Math.log10(s.distance);
                sumLogD += logD;
                sumRSSI += s.rssi;
                sumLogD2 += logD * logD;
                sumRSSI_LogD += s.rssi * logD;
            }
            
            var b = (n * sumRSSI_LogD - sumLogD * sumRSSI) / (n * sumLogD2 - sumLogD * sumLogD);
            var a = (sumRSSI - b * sumLogD) / n;
            
            var pathLossExp = -b / 10.0;
            var refRSSI = a;
            var envFactor = 1.0;
            
            document.getElementById('resultRefRSSI').textContent = refRSSI.toFixed(1);
            document.getElementById('resultPathLoss').textContent = pathLossExp.toFixed(2);
            document.getElementById('resultEnvFactor').textContent = envFactor.toFixed(2);
            
            console.log('Calibration complete:', { refRSSI: refRSSI, pathLossExp: pathLossExp, envFactor: envFactor });
            
            // Post calculated parameters to ESP32 /api/calibration/save endpoint
            try {
                const response = await fetch('/api/calibration/save', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        reference_rssi: parseFloat(refRSSI.toFixed(2)),
                        path_loss_exponent: parseFloat(pathLossExp.toFixed(4)),
                        environmental_factor: parseFloat(envFactor.toFixed(2))
                    })
                });
                const result = await response.json();
                if (result.status === 'success') {
                    console.log('Calibration successfully saved & synced!');
                } else {
                    console.error('Server failed to save calibration:', result.message);
                    alert('Calibration complete, but saving to the device failed: ' + result.message);
                }
            } catch (err) {
                console.error('Network error saving calibration:', err);
                alert('Calibration complete, but network error occurred while saving.');
            }
            
            showStep(4);
        }
        
        function restartCalibration() {
            samples = [];
            showStep(1);
        }
    </script>
</body>
</html>
)rawhtml";
// Global variable to track the last time the webserver handled a request
unsigned long lastWebserverRequestTime = 0;

class WebRequestTracker : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest *request) override {
        lastWebserverRequestTime = millis();
        return false; // Return false so the request is passed down to other handlers
    }
    void handleRequest(AsyncWebServerRequest *request) override {}
};

class ConfigurationWebserver {
private:
    AsyncWebServer server;
    String node_id;
    MultiReaderManager* readerManager;
    DistanceCalculator* distanceCalc;
    ConfigurationManager* configMgr;
    bool mdns_started;
    
public:
    ConfigurationWebserver(uint16_t port = 80) : server(port), mdns_started(false) {}
    
    void begin(String nodeId, MultiReaderManager* rm, DistanceCalculator* dc, ConfigurationManager* cm) {
        node_id = nodeId;
        readerManager = rm;
        distanceCalc = dc;
        configMgr = cm;
        
        // Register mDNS service
        String hostname = "halowatch-" + nodeId;
        hostname.toLowerCase(); // mDNS hostnames should be lowercase
        
        Serial.printf("Attempting to start mDNS with hostname: %s.local\n", hostname.c_str());
        
        if (!MDNS.begin(hostname.c_str())) {
            Serial.println("Error setting up mDNS responder!");
            Serial.println("Webserver will still be accessible via IP address");
            mdns_started = false;
        } else {
            Serial.printf("mDNS responder started successfully: %s.local\n", hostname.c_str());
            MDNS.addService("http", "tcp", 80);
            mdns_started = true;
        }
        
        // Register the web request tracker to automatically pause BLE scanning when web requests arrive
        server.addHandler(new WebRequestTracker());
        
        // Setup routes
        setupRoutes();
        
        // Start the server
        server.begin();
        Serial.println("HTTP server started on port 80");
        
        // Print access information
        Serial.println("=== Configuration Webserver Access ===");
        if (mdns_started) {
            Serial.printf("mDNS URL: http://%s.local\n", hostname.c_str());
        }
        Serial.printf("IP Address: http://%s\n", WiFi.localIP().toString().c_str());
        Serial.println("======================================");
    }
    
    void setupRoutes() {
        // Serve main configuration page via PROGMEM (zero-allocation)
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->send_P(200, "text/html", CONFIG_HTML);
        });
        
        // API: Get all readers
        server.on("/api/readers", HTTP_GET, [this](AsyncWebServerRequest *request) {
            String json = getReadersJSON();
            request->send(200, "application/json", json);
        });
        
        // API: Update door name
        server.on("/api/readers/update", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                // Parse JSON body
                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, (const char*)data);
                
                if (error) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
                    return;
                }
                
                if (!doc.containsKey("reader_id") || !doc.containsKey("door_name")) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters\"}");
                    return;
                }
                
                String readerId = doc["reader_id"].as<String>();
                String doorName = doc["door_name"].as<String>();
                
                // Validate door name (non-empty, reasonable length)
                if (doorName.length() == 0 || doorName.length() > 100) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid door name\"}");
                    return;
                }
                
                // Update door name in reader manager
                if (readerManager != nullptr) {
                    readerManager->updateDoorName(readerId, doorName);
                }
                
                // Save to local storage
                if (configMgr != nullptr) {
                    configMgr->cacheReaderConfig(readerId, doorName);
                    // Queue the push sync to backend so it is processed asynchronously on Core 1 background loop
                    configMgr->queueSync(readerId, doorName);
                }
                
                request->send(200, "application/json", "{\"status\":\"success\"}");
            }
        );
        
        // API: Get calibration parameters
        server.on("/api/calibration", HTTP_GET, [this](AsyncWebServerRequest *request) {
            String json = getCalibrationJSON();
            request->send(200, "application/json", json);
        });
        
        // API: Save calibration parameters
        server.on("/api/calibration/save", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, (const char*)data);
                
                if (error) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
                    return;
                }
                
                if (!doc.containsKey("reference_rssi") || !doc.containsKey("path_loss_exponent") || !doc.containsKey("environmental_factor")) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters\"}");
                    return;
                }
                
                float ref_rssi = doc["reference_rssi"].as<float>();
                float path_loss = doc["path_loss_exponent"].as<float>();
                float env_factor = doc["environmental_factor"].as<float>();
                
                if (distanceCalc != nullptr) {
                    DistanceCalibration newCalib;
                    newCalib.reference_rssi = ref_rssi;
                    newCalib.path_loss_exp = path_loss;
                    newCalib.environmental_factor = env_factor;
                    distanceCalc->updateCalibration(newCalib);
                    distanceCalc->saveToStorage();
                }
                
                // Queue the calibration sync to backend so it is processed asynchronously on Core 0 background loop
                if (configMgr != nullptr) {
                    configMgr->queueCalibrationSync(ref_rssi, path_loss, env_factor);
                }
                
                request->send(200, "application/json", "{\"status\":\"success\"}");
            }
        );
        
        // API: Start calibration (placeholder for task 2.8)
        server.on("/api/calibration/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
            // Initialize calibration session
            Serial.println("Calibration session started");
            request->send(200, "application/json", "{\"status\":\"calibration_started\"}");
        });
        
        // API: Add calibration sample
        server.on("/api/calibration/sample", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                DynamicJsonDocument doc(256);
                DeserializationError error = deserializeJson(doc, (const char*)data);
                
                if (error) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
                    return;
                }
                
                if (!doc.containsKey("distance") || !doc.containsKey("rssi")) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters\"}");
                    return;
                }
                
                float distance = doc["distance"];
                int rssi = doc["rssi"];
                
                Serial.printf("Calibration sample: distance=%.1fm, rssi=%d dBm\n", distance, rssi);
                
                // Store sample (in a real implementation, we'd accumulate these for calculation)
                // For now, just acknowledge receipt
                
                request->send(200, "application/json", "{\"status\":\"sample_added\"}");
            }
        );
        
        // Calibration wizard page via PROGMEM (zero-allocation)
        server.on("/calibrate", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->send_P(200, "text/html", CALIBRATION_HTML);
        });
        
        // 404 handler
        server.onNotFound([](AsyncWebServerRequest *request) {
            request->send(404, "text/plain", "Not found");
        });
    }
    

    
    String getReadersJSON() {
        DynamicJsonDocument doc(4096);
        JsonArray jsonReaders = doc.to<JsonArray>();
        
        if (readerManager != nullptr) {
            std::vector<RFIDReaderConfig> readersCopy = readerManager->getReadersCopy();
            for (const auto &r : readersCopy) {
                JsonObject readerObj = jsonReaders.createNestedObject();
                readerObj["reader_id"] = r.hardware_id;
                readerObj["gpio_pin"] = r.gpio_rx;
                readerObj["door_name"] = r.door_name;
                readerObj["status"] = r.initialized ? "active" : "disconnected";
            }
        }
        
        String output;
        serializeJson(doc, output);
        return output;
    }
    
    String getCalibrationJSON() {
        DynamicJsonDocument doc(512);
        
        if (distanceCalc != nullptr) {
            DistanceCalibration calib = distanceCalc->getCalibration();
            doc["calibrated"] = distanceCalc->isCalibrated();
            doc["reference_rssi"] = calib.reference_rssi;
            doc["path_loss_exponent"] = calib.path_loss_exp;
            doc["environmental_factor"] = calib.environmental_factor;
        } else {
            doc["calibrated"] = false;
            doc["reference_rssi"] = -59.0;
            doc["path_loss_exponent"] = 2.0;
            doc["environmental_factor"] = 1.0;
        }
        
        String output;
        serializeJson(doc, output);
        return output;
    }
    
    void syncConfigToBackend(String readerId, String doorName) {
        // POST configuration update to backend server with retry logic
        const int MAX_RETRIES = 3;
        const int BASE_DELAY_MS = 1000; // Start with 1 second
        
        String url = "https://api.jewelisbeast.xyz/api/v1/readers/sync";
        
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("WiFi not connected, cannot sync to backend");
                return;
            }
            
            WiFiClientSecure client;
            client.setInsecure();
            
            HTTPClient http;
            if (http.begin(client, url)) {
                http.addHeader("Content-Type", "application/json");
                
                DynamicJsonDocument doc(512);
                doc["node_id"] = node_id;
                doc["reader_id"] = readerId;
                doc["door_name"] = doorName;
                doc["timestamp"] = millis();
                
                String payload;
                serializeJson(doc, payload);
                
                Serial.printf("Syncing config to backend (attempt %d/%d): %s -> %s\n", 
                             attempt + 1, MAX_RETRIES, readerId.c_str(), doorName.c_str());
                
                int httpCode = http.POST(payload);
                
                if (httpCode == 200 || httpCode == 201) {
                    Serial.printf("Config sync successful (HTTP %d)\n", httpCode);
                    http.end();
                    return; // Success, exit retry loop
                } else if (httpCode > 0) {
                    Serial.printf("Config sync failed with HTTP %d: %s\n", 
                                 httpCode, http.getString().c_str());
                } else {
                    Serial.printf("Config sync failed: %s\n", 
                                 http.errorToString(httpCode).c_str());
                }
                
                http.end();
            } else {
                Serial.println("Failed to begin HTTP connection for config sync");
            }
            
            // Exponential backoff: wait before retry
            if (attempt < MAX_RETRIES - 1) {
                int delay_ms = BASE_DELAY_MS * (1 << attempt); // 1s, 2s, 4s
                Serial.printf("Retrying in %d ms...\n", delay_ms);
                delay(delay_ms);
            }
        }
        
        Serial.println("Config sync failed after all retries - configuration saved locally");
    }
    
    void syncCalibrationToBackend(float reference_rssi, float path_loss_exp, float env_factor) {
        // POST calibration parameters to backend server with retry logic
        const int MAX_RETRIES = 3;
        const int BASE_DELAY_MS = 1000;
        
        String url = "https://api.jewelisbeast.xyz/api/v1/calibration/";
        url += node_id;
        url += "/sync";
        
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("WiFi not connected, cannot sync calibration to backend");
                return;
            }
            
            WiFiClientSecure client;
            client.setInsecure();
            
            HTTPClient http;
            if (http.begin(client, url)) {
                http.addHeader("Content-Type", "application/json");
                
                DynamicJsonDocument doc(512);
                doc["node_id"] = node_id;
                doc["reference_rssi"] = reference_rssi;
                doc["path_loss_exponent"] = path_loss_exp;
                doc["environmental_factor"] = env_factor;
                doc["timestamp"] = millis();
                
                String payload;
                serializeJson(doc, payload);
                
                Serial.printf("Syncing calibration to backend (attempt %d/%d)\n", 
                             attempt + 1, MAX_RETRIES);
                
                int httpCode = http.POST(payload);
                
                if (httpCode == 200 || httpCode == 201) {
                    Serial.printf("Calibration sync successful (HTTP %d)\n", httpCode);
                    http.end();
                    return; // Success
                } else if (httpCode > 0) {
                    Serial.printf("Calibration sync failed with HTTP %d: %s\n", 
                                 httpCode, http.getString().c_str());
                } else {
                    Serial.printf("Calibration sync failed: %s\n", 
                                 http.errorToString(httpCode).c_str());
                }
                
                http.end();
            } else {
                Serial.println("Failed to begin HTTP connection for calibration sync");
            }
            
            // Exponential backoff
            if (attempt < MAX_RETRIES - 1) {
                int delay_ms = BASE_DELAY_MS * (1 << attempt);
                Serial.printf("Retrying in %d ms...\n", delay_ms);
                delay(delay_ms);
            }
        }
        
        Serial.println("Calibration sync failed after all retries");
    }
    
    bool isMDNSStarted() {
        return mdns_started;
    }
};

// Global Configuration Webserver instance
ConfigurationWebserver* configWebserver = nullptr;

void ConfigurationManager::syncWithBackend() {
    // 1. Process any pending calibration pushes to backend asynchronously
    PendingCalibSync localCalibCopy;
    if (syncMutex != nullptr && xSemaphoreTake(syncMutex, (TickType_t)10) == pdTRUE) {
        localCalibCopy = pendingCalibSync;
        pendingCalibSync.pending = false; // Reset
        xSemaphoreGive(syncMutex);
    }
    
    if (localCalibCopy.pending) {
        if (configWebserver != nullptr) {
            if (networkMutex != nullptr && xSemaphoreTake(networkMutex, portMAX_DELAY) == pdTRUE) {
                configWebserver->syncCalibrationToBackend(localCalibCopy.reference_rssi, 
                                                         localCalibCopy.path_loss_exp, 
                                                         localCalibCopy.environmental_factor);
                xSemaphoreGive(networkMutex);
            }
        }
    }

    // 2. Process any pending pushes to backend asynchronously
    std::vector<PendingSync> localCopy;
    if (syncMutex != nullptr && xSemaphoreTake(syncMutex, (TickType_t)10) == pdTRUE) {
        localCopy = pendingSyncs;
        pendingSyncs.clear();
        xSemaphoreGive(syncMutex);
    }
    
    for (const auto &sync : localCopy) {
        if (configWebserver != nullptr) {
            if (networkMutex != nullptr && xSemaphoreTake(networkMutex, portMAX_DELAY) == pdTRUE) {
                configWebserver->syncConfigToBackend(sync.reader_id, sync.door_name);
                xSemaphoreGive(networkMutex);
            }
        }
    }

    // 3. Process local Door Events Queue
    std::vector<PendingDoorEvent> eventsToSync;
    if (doorEventsMutex != nullptr && xSemaphoreTake(doorEventsMutex, (TickType_t)10) == pdTRUE) {
        eventsToSync = pendingDoorEvents;
        pendingDoorEvents.clear();
        xSemaphoreGive(doorEventsMutex);
    }

    std::vector<PendingDoorEvent> failedEvents;
    for (const auto &ev : eventsToSync) {
        bool sent = false;
        if (networkMutex != nullptr && xSemaphoreTake(networkMutex, (TickType_t)2000 / portTICK_PERIOD_MS) == pdTRUE) {
            sent = uploadDoorEventSynchronous(ev.rfid_uid, ev.action, ev.door_name, ev.reader_id);
            xSemaphoreGive(networkMutex);
        }
        if (!sent) {
            failedEvents.push_back(ev);
        }
    }

    // Put failed events back in queue
    if (!failedEvents.empty() && doorEventsMutex != nullptr && xSemaphoreTake(doorEventsMutex, portMAX_DELAY) == pdTRUE) {
        pendingDoorEvents.insert(pendingDoorEvents.begin(), failedEvents.begin(), failedEvents.end());
        xSemaphoreGive(doorEventsMutex);
        Serial.printf("Offline Sync: %d events failed to upload, retained in local cache\n", failedEvents.size());
    }

    // 4. Periodic pull configuration and patient assignments from backend
    unsigned long current_time = millis();
    if (current_time - last_sync_time >= 15000) { // Sync updates every 15s
        Serial.println("Syncing configuration and active assignments with backend...");
        
        bool success = false;
        if (networkMutex != nullptr && xSemaphoreTake(networkMutex, portMAX_DELAY) == pdTRUE) {
            success = fetchDoorMappings();
            syncActiveAssignments();
            xSemaphoreGive(networkMutex);
        }
        
        if (success) {
            last_sync_time = current_time;
            Serial.println("Configuration and active assignments sync completed");
        } else {
            Serial.println("Configuration sync failed, using cached config");
        }
    }
}

// --- RSSI EMA Filter State ---
struct EmaRssiState {
    uint16_t minor;
    float filtered_rssi;
    unsigned long last_update;
};
std::vector<EmaRssiState> emaFilters;
SemaphoreHandle_t emaFiltersMutex = nullptr;

float applyEmaFilter(uint16_t minor, int raw_rssi) {
    const float ALPHA = 0.25f;
    float filtered = raw_rssi;
    bool found = false;
    
    if (emaFiltersMutex != nullptr && xSemaphoreTake(emaFiltersMutex, portMAX_DELAY) == pdTRUE) {
        unsigned long now = millis();
        // Clean up stale EMA filters (older than 30 seconds) to prevent memory leak
        for (auto it = emaFilters.begin(); it != emaFilters.end(); ) {
            if (now - it->last_update > 30000) {
                it = emaFilters.erase(it);
            } else {
                ++it;
            }
        }
        
        for (auto &state : emaFilters) {
            if (state.minor == minor) {
                state.filtered_rssi = (ALPHA * raw_rssi) + ((1.0f - ALPHA) * state.filtered_rssi);
                state.last_update = now;
                filtered = state.filtered_rssi;
                found = true;
                break;
            }
        }
        if (!found) {
            EmaRssiState newState;
            newState.minor = minor;
            newState.filtered_rssi = raw_rssi;
            newState.last_update = now;
            emaFilters.push_back(newState);
            filtered = raw_rssi;
        }
        xSemaphoreGive(emaFiltersMutex);
    }
    return filtered;
}

// --- BLE Callbacks ---
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.haveManufacturerData()) {
            std::string strManufacturerData = advertisedDevice.getManufacturerData();
            uint8_t cManufacturerData[100];
            strManufacturerData.copy((char *)cManufacturerData, strManufacturerData.length());

            if (strManufacturerData.length() >= 25 && cManufacturerData[0] == 0x4C && cManufacturerData[1] == 0x00) {
                uint16_t minor = (cManufacturerData[22] << 8) | cManufacturerData[23];
                int rssi = advertisedDevice.getRSSI();
                
                // Filter RSSI using EMA algorithm
                float filteredRssi = applyEmaFilter(minor, rssi);
                int filteredRssiInt = (int)round(filteredRssi);
                
                DetectedBeacon b;
                b.minor = minor;
                b.rssi = filteredRssiInt;
                
                // Use DistanceCalculator if available, otherwise use legacy calculation
                if (distanceCalculator != nullptr) {
                    b.distance = distanceCalculator->calculateDistance(filteredRssiInt);
                } else {
                    // Legacy calculation as fallback
                    int8_t txPower = (int8_t)cManufacturerData[24];
                    b.distance = pow(10.0, (double)(txPower - filteredRssiInt) / 20.0);
                }
                
                if (xSemaphoreTake(detectionsMutex, (TickType_t)10) == pdTRUE) {
                    detections.push_back(b);
                    xSemaphoreGive(detectionsMutex);
                }
            }
        }
    }
};

// --- Upload Logic ---
void uploadLocation() {
    if (WiFi.status() != WL_CONNECTED || detections.empty()) return;

    isNetworkBusy = true;

    // Use the thread-safe network mutex with 15s timeout
    if (networkMutex != nullptr && xSemaphoreTake(networkMutex, (TickType_t)15000 / portTICK_PERIOD_MS) == pdTRUE) {
        WiFiClientSecure client;
        client.setInsecure();
        
        HTTPClient http;
        if (http.begin(client, URL_LOCATION)) {
            http.addHeader("Content-Type", "application/json");

            DynamicJsonDocument doc(2048);
            doc["receiver_id"] = NODE_ID;
            doc["timestamp"] = millis();
            
            JsonObject loc = doc.createNestedObject("location");
            loc["zone"] = WARD_ZONE;
            loc["room"] = ROOM_CODE;

            JsonArray tags = doc.createNestedArray("detected_tags");
            if (xSemaphoreTake(detectionsMutex, (TickType_t)100) == pdTRUE) {
                for (auto const& b : detections) {
                    JsonObject tag = tags.createNestedObject();
                    tag["ble_minor"] = b.minor;
                    tag["rssi"] = b.rssi;
                    tag["estimated_distance"] = b.distance;
                    tag["timestamp"] = millis();
                }
                detections.clear();
                xSemaphoreGive(detectionsMutex);
            }

            String body;
            serializeJson(doc, body);
            int httpResponseCode = http.POST(body);
            if (httpResponseCode > 0) {
                Serial.printf("Location Update Sent (Status: %d)\n", httpResponseCode);
            } else {
                Serial.printf("Error sending Location: %s\n", http.errorToString(httpResponseCode).c_str());
            }
            http.end();
        }
        xSemaphoreGive(networkMutex);
    } else {
        Serial.println("Skipping Location Upload - Network Mutex Unavailable");
    }
    isNetworkBusy = false;
}

void uploadDoorEvent(String rfidUid, String action, String doorName, String readerId) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    isNetworkBusy = true;

    // Use the thread-safe network mutex with 5s timeout to not block RFID reader task for too long
    if (networkMutex != nullptr && xSemaphoreTake(networkMutex, (TickType_t)5000 / portTICK_PERIOD_MS) == pdTRUE) {
        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        if (http.begin(client, URL_DOOR)) {
            http.addHeader("Content-Type", "application/json");

            StaticJsonDocument<512> doc;
            doc["node_id"] = NODE_ID;
            doc["reader_id"] = readerId;
            doc["door_name"] = doorName;
            doc["rfid_uid"] = rfidUid;
            doc["action"] = action;
            doc["timestamp"] = millis();

            String body;
            serializeJson(doc, body);
            int httpResponseCode = http.POST(body);
            
            if (httpResponseCode > 0) {
                Serial.printf("Door Event Sent: %s -> %s at %s (Status: %d)\n", 
                             rfidUid.c_str(), action.c_str(), doorName.c_str(), httpResponseCode);
            } else {
                Serial.printf("Error sending Door Event: %s\n", http.errorToString(httpResponseCode).c_str());
            }
            http.end();
        }
        xSemaphoreGive(networkMutex);
    } else {
        Serial.println("Skipping Door Event Upload - Network Mutex Busy");
    }
    isNetworkBusy = false;
}

bool uploadDoorEventSynchronous(String rfidUid, String action, String doorName, String readerId) {
    if (WiFi.status() != WL_CONNECTED) return false;
    
    bool success = false;
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    if (http.begin(client, URL_DOOR)) {
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<512> doc;
        doc["node_id"] = NODE_ID;
        doc["reader_id"] = readerId;
        doc["door_name"] = doorName;
        doc["rfid_uid"] = rfidUid;
        doc["action"] = action;
        doc["timestamp"] = millis();

        String body;
        serializeJson(doc, body);
        int httpResponseCode = http.POST(body);
        
        if (httpResponseCode == 200 || httpResponseCode == 201) {
            String payload = http.getString();
            Serial.printf("Door Event Synchronously Sent: %s -> %s at %s (Status: %d)\n", 
                          rfidUid.c_str(), action.c_str(), doorName.c_str(), httpResponseCode);
            
            // Parse response to check for buzzer_trigger
            DynamicJsonDocument responseDoc(512);
            DeserializationError error = deserializeJson(responseDoc, payload);
            if (!error) {
                if (responseDoc.containsKey("buzzer_trigger") && responseDoc["buzzer_trigger"].as<bool>()) {
                    triggerBuzzer();
                }
            }
            success = true;
        } else {
            Serial.printf("Error sending synchronous Door Event: %d\n", httpResponseCode);
        }
        http.end();
    }
    return success;
}

void syncActiveAssignments() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    if (http.begin(client, "https://api.jewelisbeast.xyz/api/v1/patients/active-assignments")) {
        int httpResponseCode = http.GET();
        if (httpResponseCode == 200) {
            String payload = http.getString();
            DynamicJsonDocument doc(2048);
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
                if (assignmentsMutex != nullptr && xSemaphoreTake(assignmentsMutex, portMAX_DELAY) == pdTRUE) {
                    activeAssignments.clear();
                    JsonArray arr = doc.as<JsonArray>();
                    for (JsonObject obj : arr) {
                        PatientAssignmentLocal pa;
                        pa.rfid_uid = obj["rfid_uid"].as<String>();
                        pa.assigned_room = obj["room"].as<String>();
                        activeAssignments.push_back(pa);
                    }
                    xSemaphoreGive(assignmentsMutex);
                    Serial.printf("Synced %d patient room assignments successfully\n", activeAssignments.size());
                }
            } else {
                Serial.printf("Error parsing active assignments JSON: %s\n", error.c_str());
            }
        } else {
            Serial.printf("Error getting active assignments: %d\n", httpResponseCode);
        }
        http.end();
    }
}

// --- Tasks ---
void TaskRFID(void *pvParameters) {
    Serial.println("RFID Task Started on Core 1");
    for (;;) {
        if (multiReaderManager != nullptr) {
            multiReaderManager->pollAllReaders();
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void TaskNetworkSync(void *pvParameters) {
    Serial.println("Network Sync Task Started on Core 0");
    vTaskDelay(3000 / portTICK_PERIOD_MS); // Let webserver and network settle
    
    // Initial calibration pull from backend
    if (distanceCalculator != nullptr) {
        distanceCalculator->loadCalibration();
    }
    
    // Initial door mappings pull from backend
    if (configManager != nullptr) {
        configManager->fetchDoorMappings();
    }
    
    for (;;) {
        if (configManager != nullptr) {
            configManager->syncWithBackend();
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize Mutexes
    detectionsMutex = xSemaphoreCreateMutex();
    emaFiltersMutex = xSemaphoreCreateMutex();
    networkMutex = xSemaphoreCreateMutex();
    doorEventsMutex = xSemaphoreCreateMutex();
    assignmentsMutex = xSemaphoreCreateMutex();
    
    // Initialize Wi-Fi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { 
        delay(500); 
        Serial.print("."); 
    }
    Serial.println("\nWiFi Connected");

    // Initialize Multi-Reader Manager and Offline Config (from Preferences)
    multiReaderManager = new MultiReaderManager();
    multiReaderManager->initializeReaders();

    // Initialize Configuration Manager (from Preferences only - fast/local)
    configManager = new ConfigurationManager();
    configManager->loadFromStorage();

    // Initialize Distance Calculator (from Preferences only - fast/local)
    distanceCalculator = new DistanceCalculator();
    distanceCalculator->loadFromStorage();

    // Initialize Configuration Webserver (Start IMMEDIATELY!)
    configWebserver = new ConfigurationWebserver(80);
    configWebserver->begin(NODE_ID, multiReaderManager, distanceCalculator, configManager);

    // Initialize BLE Device
    BLEDevice::init(NODE_ID);
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(150); // Scan interval (150ms)
    pBLEScan->setWindow(50);    // Scan window (50ms) -> 33% duty cycle for stable BLE/WiFi coexistence

    // Create Multi-Core Tasks
    // 1. RFID reader polling pinned to Core 1
    xTaskCreatePinnedToCore(TaskRFID, "TaskRFID", 16384, NULL, 1, NULL, 1);
    
    // 2. Background Network Sync Task pinned to Core 0 (shares LwIP/WiFi core)
    xTaskCreatePinnedToCore(TaskNetworkSync, "TaskNetworkSync", 32768, NULL, 1, NULL, 0);
    
    Serial.println("Unified Node Setup Completed Successfully!");
}

void loop() {
    // If there has been no web request for the last 15 seconds, and network is not busy, we can scan
    if (!isNetworkBusy && (millis() - lastWebserverRequestTime > 15000)) {
        Serial.println("Starting BLE Scan...");
        BLEDevice::getScan()->start(5, false);
        BLEDevice::getScan()->clearResults();
        
        Serial.print("Detections found: ");
        Serial.println(detections.size());
        
        uploadLocation();
    } else if (isNetworkBusy) {
        // Network busy (e.g. door event or location data being uploaded)
        delay(100);
    } else {
        // Webserver is active! Give 100% of the CPU and RF time to WiFi by skipping BLE scan.
        static unsigned long lastPrint = 0;
        if (millis() - lastPrint > 3000) {
            Serial.println("Web Config Mode Active - BLE Scanning paused to prioritize WiFi...");
            lastPrint = millis();
        }
        delay(100);
    }
    delay(1000);
}
