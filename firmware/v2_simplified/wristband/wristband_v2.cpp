/**
 * Guardi-Halo-Watch v2.0 Simplified Wristband Firmware
 * Role: iBeacon + Web Config + Event Transmitter
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEBeacon.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "MPU6050.h"

// --- Hardware Configuration ---
#define PIN_TAMPER_STRAP 14
#define PIN_TAMPER_SKIN  15
#define PIN_BATTERY_ADC  34
#define PIN_LED_ALARM    2

// --- Project Configuration ---
const char* WIFI_SSID = "Brightburn";
const char* WIFI_PASS = "p434/0Q2";
const char* BACKEND_URL = "https://api.jewelisbeast.xyz/api/v1";

// --- State & Storage ---
Preferences prefs;
WebServer server(80);
MPU6050 mpu;
BLEAdvertising *pAdvertising;

struct PatientData {
    char id[32];
    char name[64];
    char ward[32];
    char rfid_uid[32];
    int ble_minor;
};

PatientData currentPatient = {"PT-0000", "Unassigned", "Ward 0", "00000000", 1001};
bool fall_detected = false;
bool tamper_detected = false;
float battery_level = 100.0;
unsigned long lastUpdate = 0;
bool mpu_detected = false;

// --- WiFi & Web Server ---
void handleRoot() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>";
    html += "body{font-family:sans-serif; background:#f0f4f8; color:#333; padding:20px;}";
    html += ".card{background:white; padding:20px; border-radius:12px; shadow:0 4px 6px rgba(0,0,0,0.1); margin-bottom:20px;}";
    html += "input, select{width:100%; padding:10px; margin:10px 0; border:1px solid #ccc; border-radius:6px; box-sizing:border-box; background:white;}";
    html += "button{width:100%; padding:10px; background:#4a90e2; color:white; border:none; border-radius:6px; cursor:pointer;}";
    html += "h2{color:#4a90e2;}</style></head><body>";
    html += "<div class='card'><h2>Patient Assignment</h2>";
    html += "<form action='/save' method='POST'>";
    html += "Patient ID:<input type='text' name='id' value='" + String(currentPatient.id) + "'>";
    html += "Full Name:<input type='text' name='name' value='" + String(currentPatient.name) + "'>";
    
    html += "Ward/Room:<br><select name='ward'>";
    html += "<option value='ENTRY'" + String(strcmp(currentPatient.ward, "ENTRY") == 0 ? " selected" : "") + ">Main Entry (ENTRY)</option>";
    html += "<option value='R01'" + String(strcmp(currentPatient.ward, "R01") == 0 ? " selected" : "") + ">Patient Room 1 (R01)</option>";
    html += "<option value='R02'" + String(strcmp(currentPatient.ward, "R02") == 0 ? " selected" : "") + ">Patient Room 2 (R02)</option>";
    html += "<option value='R03'" + String(strcmp(currentPatient.ward, "R03") == 0 ? " selected" : "") + ">Patient Room 3 (R03)</option>";
    html += "<option value='NURSE'" + String(strcmp(currentPatient.ward, "NURSE") == 0 ? " selected" : "") + ">Nurses Station (NURSE)</option>";
    html += "<option value='REST'" + String(strcmp(currentPatient.ward, "REST") == 0 ? " selected" : "") + ">Restricted Area (REST)</option>";
    html += "<option value='ISO'" + String(strcmp(currentPatient.ward, "ISO") == 0 ? " selected" : "") + ">Isolation Room (ISO)</option>";
    html += "</select><br>";

    html += "RFID UID:<input type='text' name='rfid' value='" + String(currentPatient.rfid_uid) + "'>";
    html += "BLE Minor ID:<input type='number' name='minor' value='" + String(currentPatient.ble_minor) + "'>";
    html += "<button type='submit'>Save & Register</button></form></div>";
    
    if (String(currentPatient.id) != "PT-0000" && String(currentPatient.name) != "Unassigned") {
        html += "<div class='card' style='border: 1px solid #d9534f; background: #fff5f5;'>";
        html += "<h2 style='color: #d9534f;'>Active Patient Assigned</h2>";
        html += "<p>Patient <b>" + String(currentPatient.name) + "</b> (" + String(currentPatient.id) + ") is registered to this wristband.</p>";
        html += "<form action='/delete' method='POST'>";
        html += "<button type='submit' style='background:#d9534f; font-weight: bold;'>Delete & Deassign Patient</button>";
        html += "</form></div>";
    }
    
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void postToBackend(const char* endpoint, JsonDocument& doc) {
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        String url = String(BACKEND_URL) + endpoint;
        
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");
        
        String json;
        serializeJson(doc, json);
        int httpCode = http.POST(json);
        Serial.printf("POST %s: %d\n", url.c_str(), httpCode);
        http.end();
    }
}

void handleSave() {
    if (server.hasArg("id")) strncpy(currentPatient.id, server.arg("id").c_str(), 31);
    if (server.hasArg("name")) strncpy(currentPatient.name, server.arg("name").c_str(), 63);
    if (server.hasArg("ward")) strncpy(currentPatient.ward, server.arg("ward").c_str(), 31);
    if (server.hasArg("rfid")) strncpy(currentPatient.rfid_uid, server.arg("rfid").c_str(), 31);
    if (server.hasArg("minor")) currentPatient.ble_minor = server.arg("minor").toInt();

    prefs.begin("patient", false);
    prefs.putBytes("data", &currentPatient, sizeof(PatientData));
    prefs.end();

    // Register with backend
    StaticJsonDocument<256> doc;
    doc["patient_id"] = currentPatient.id;
    doc["name"] = currentPatient.name;
    doc["ward"] = currentPatient.ward;
    doc["ble_minor"] = currentPatient.ble_minor;
    doc["rfid_uid"] = currentPatient.rfid_uid;
    postToBackend("/patients/assign", doc);

    server.send(200, "text/plain", "Saved and Registered! Device will restart to apply BLE changes.");
    delay(2000);
    ESP.restart();
}

void handleDelete() {
    // Notify backend
    StaticJsonDocument<128> doc;
    doc["patient_id"] = currentPatient.id;
    postToBackend("/patients/deassign", doc);

    // Clear local state
    strcpy(currentPatient.id, "PT-0000");
    strcpy(currentPatient.name, "Unassigned");
    strcpy(currentPatient.ward, "Ward 0");
    strcpy(currentPatient.rfid_uid, "00000000");
    currentPatient.ble_minor = 1001;

    // Save empty state to preferences
    prefs.begin("patient", false);
    prefs.putBytes("data", &currentPatient, sizeof(PatientData));
    prefs.end();

    server.send(200, "text/plain", "Patient deleted and deassigned from device! Restarting device to apply changes...");
    delay(2000);
    ESP.restart();
}

// --- BLE iBeacon ---
void setupBeacon() {
    BLEDevice::init("HW-WRISTBAND");
    pAdvertising = BLEDevice::getAdvertising();
    
    BLEBeacon oBeacon = BLEBeacon();
    oBeacon.setManufacturerId(0x4C00); 
    oBeacon.setProximityUUID(BLEUUID("43210000-1234-5678-1234-567890abcdef"));
    oBeacon.setMajor(100);
    oBeacon.setMinor(currentPatient.ble_minor);
    
    BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
    oAdvertisementData.setFlags(0x04); 
    
    std::string strServiceData = "";
    strServiceData += (char)26; 
    strServiceData += (char)0xFF;
    strServiceData += oBeacon.getData(); 
    oAdvertisementData.addData(strServiceData);
    
    pAdvertising->setAdvertisementData(oAdvertisementData);
    pAdvertising->start();
    Serial.println("Beacon started with Minor: " + String(currentPatient.ble_minor));
}

// --- Core Logic ---
void sendStatusUpdate(bool isEmergency) {
    StaticJsonDocument<512> doc;
    doc["device_id"] = WiFi.macAddress();
    doc["patient_id"] = currentPatient.id;
    doc["battery_level"] = (int)battery_level;
    
    JsonObject sensors = doc.createNestedObject("sensors");
    JsonObject mpuObj = sensors.createNestedObject("mpu6050");
    mpuObj["accel_x"] = 0; mpuObj["accel_y"] = 0; mpuObj["accel_z"] = 0;
    mpuObj["gyro_x"] = 0; mpuObj["gyro_y"] = 0; mpuObj["gyro_z"] = 0;
    mpuObj["fall_detected"] = fall_detected;
    
    JsonObject tamperObj = sensors.createNestedObject("tamper");
    tamperObj["strap_intact"] = !tamper_detected;
    tamperObj["skin_contact"] = true; 
    tamperObj["tamper_alert"] = tamper_detected;
    
    JsonObject alerts = doc.createNestedObject("alerts");
    alerts["fall"] = fall_detected;
    alerts["tamper"] = tamper_detected;
    alerts["low_battery"] = battery_level < 20;
    
    postToBackend("/wristband-data", doc);
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_TAMPER_STRAP, INPUT_PULLUP);
    pinMode(PIN_TAMPER_SKIN, INPUT);
    pinMode(PIN_LED_ALARM, OUTPUT);
    
    // Load local data
    prefs.begin("patient", true);
    if (prefs.isKey("data")) {
        prefs.getBytes("data", &currentPatient, sizeof(PatientData));
    }
    prefs.end();

    // Start WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected. IP: " + WiFi.localIP().toString());

    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/delete", HTTP_POST, handleDelete);
    server.begin();

    Wire.begin();
    mpu_detected = mpu.begin();
    if (mpu_detected) {
        Serial.println("MPU6050 sensor successfully detected and initialized!");
    } else {
        Serial.println("WARNING: MPU6050 sensor NOT detected! Skipping accelerometer reads to prevent I2C noise.");
    }
    setupBeacon();
}

void loop() {
    server.handleClient();
    
    // Check Sensors
    bool strapIntact = digitalRead(PIN_TAMPER_STRAP) == LOW;
    bool current_tamper = !strapIntact;
    if (current_tamper != tamper_detected) {
        tamper_detected = current_tamper;
        sendStatusUpdate(true);
    }

    if (mpu_detected) {
        sensors_event_t a, g, t;
        mpu.getEvent(&a, &g, &t);
        float magnitude = sqrt(pow(a.acceleration.x, 2) + pow(a.acceleration.y, 2) + pow(a.acceleration.z, 2));
        if (magnitude > 18.0 && !fall_detected) {
            fall_detected = true;
            sendStatusUpdate(true);
        } else if (magnitude < 11.0 && fall_detected) {
            fall_detected = false;
            sendStatusUpdate(false);
        }
    }

    // Alarm LED lights up continuously when compromised or fall detected
    if (tamper_detected || fall_detected) {
        digitalWrite(PIN_LED_ALARM, HIGH);
    } else {
        digitalWrite(PIN_LED_ALARM, LOW);
    }

    // Periodic heartbeat (every 5 mins)
    if (millis() - lastUpdate > 300000) {
        int raw = analogRead(PIN_BATTERY_ADC);
        battery_level = (raw / 4095.0) * 100.0;
        sendStatusUpdate(false);
        lastUpdate = millis();
    }

    delay(50);
}
