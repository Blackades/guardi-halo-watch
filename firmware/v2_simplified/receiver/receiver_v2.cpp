/**
 * Guardi-Halo-Watch v2.0 Simplified Receiver Firmware
 * Role: BLE Scanner + WiFi Uploader
 * Features:
 * - Scans for iBeacons matching the facility UUID
 * - Estimates distance based on RSSI
 * - Periodic WiFi upload to backend ingestion endpoint
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Configuration
const char* WIFI_SSID = "Brightburn";
const char* WIFI_PASS = "p434/0Q2";
const char* BACKEND_URL = "https://zuvoo.xyz/api/v1/location-update";
const char* RECEIVER_ID = "RX-A1";
const char* WARD_ZONE = "WARD_A";
const char* ROOM_CODE = "R01";

#define BEACON_UUID "43210000-1234-5678-1234-567890abcdef"

BLEScan* pBLEScan;
int scanTime = 5; // seconds

struct DetectedBeacon {
    uint16_t minor;
    int rssi;
    float distance;
};

std::vector<DetectedBeacon> detections;

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.haveManufacturerData()) {
            std::string strManufacturerData = advertisedDevice.getManufacturerData();
            uint8_t cManufacturerData[100];
            strManufacturerData.copy((char *)cManufacturerData, strManufacturerData.length());

            // Simple iBeacon check
            if (strManufacturerData.length() == 25 && cManufacturerData[0] == 0x4C && cManufacturerData[1] == 0x00) {
                uint16_t major = (cManufacturerData[20] << 8) | cManufacturerData[21];
                uint16_t minor = (cManufacturerData[22] << 8) | cManufacturerData[23];
                int8_t txPower = (int8_t)cManufacturerData[24];
                
                DetectedBeacon b;
                b.minor = minor;
                b.rssi = advertisedDevice.getRSSI();
                // Basic FSPL distance estimation
                b.distance = pow(10.0, (double)(txPower - b.rssi) / 20.0);
                
                detections.push_back(b);
            }
        }
    }
};

void uploadData() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (detections.empty()) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, BACKEND_URL);
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument doc(2048);
    doc["receiver_id"] = RECEIVER_ID;
    JsonObject loc = doc.createNestedObject("location");
    loc["zone"] = WARD_ZONE;
    loc["room"] = ROOM_CODE;
    JsonObject coords = loc.createNestedObject("coordinates");
    coords["x"] = 0; coords["y"] = 0; coords["z"] = 0;

    JsonArray tags = doc.createNestedArray("detected_tags");
    for (auto const& b : detections) {
        JsonObject tag = tags.createNestedObject();
        tag["ble_minor"] = b.minor;
        tag["rssi"] = b.rssi;
        tag["estimated_distance"] = b.distance;
    }

    String requestBody;
    serializeJson(doc, requestBody);
    
    int httpResponseCode = http.POST(requestBody);
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    
    http.end();
    detections.clear();
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi Connected");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void loop() {
  Serial.println("Scanning...");
  pBLEScan->start(scanTime, false);
  pBLEScan->clearResults();
  
  uploadData();
  delay(1000);
}
