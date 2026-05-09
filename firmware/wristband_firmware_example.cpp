#include <WiFi.h>
#include <HTTPClient.h>

// This sketch is a simplified reference implementation that shows how an ESP32
// wristband device could talk to the FastAPI backend in this project.
//
// It does NOT include real sensor drivers; instead it sends synthetic data
// shaped exactly like the "WristbandData" JSON schema in backend/schemas.py.

// --- WiFi configuration (replace with hospital / lab WiFi) ---
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// --- Backend configuration ---
const char* BACKEND_BASE_URL = "http://192.168.0.10:8000";  // Adjust to your backend IP
const char* PATIENT_ID       = "P003";
const char* DEVICE_ID        = "WB-ESP32-DEMO-003";
const char* RFID_TAG         = "A1B2C3D4E5";

unsigned long lastPostMs = 0;
const unsigned long POST_INTERVAL_MS = 10 * 1000;  // 10 seconds

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void setup() {
  Serial.begin(115200);
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  const unsigned long now = millis();
  if (now - lastPostMs >= POST_INTERVAL_MS) {
    lastPostMs = now;

    HTTPClient http;
    String url = String(BACKEND_BASE_URL) + "/api/v1/wristband-data";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    // Fake sensor values
    int heartRate    = random(65, 100);
    int spo2         = random(95, 100);
    float temp       = 36.5 + (random(-5, 6) / 10.0f);
    bool tamper      = false;
    bool fall        = false;
    bool abnormal    = temp > 37.5;
    bool lowBattery  = false;
    int batteryLevel = random(40, 100);

    String payload = "{";
    payload += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
    payload += "\"patient_id\":\"" + String(PATIENT_ID) + "\",";
    payload += "\"rfid_tag\":\"" + String(RFID_TAG) + "\",";
    payload += "\"timestamp\":\"" + String("2025-12-18T14:30:45Z") + "\",";  // replace with real RTC time
    payload += "\"battery_level\":" + String(batteryLevel) + ",";
    payload += "\"sensors\":{";
    payload +=   "\"mpu6050\":{";
    payload +=     "\"accel_x\":0.0,\"accel_y\":0.0,\"accel_z\":1.0,";
    payload +=     "\"gyro_x\":0.0,\"gyro_y\":0.0,\"gyro_z\":0.0,";
    payload +=     "\"fall_detected\":" + String(fall ? "true" : "false");
    payload +=   "},";
    payload +=   "\"max30102\":{";
    payload +=     "\"heart_rate\":" + String(heartRate) + ",";
    payload +=     "\"spo2\":" + String(spo2) + ",";
    payload +=     "\"signal_quality\":\"good\"";
    payload +=   "},";
    payload +=   "\"dht11\":{";
    payload +=     "\"temperature\":" + String(temp, 1) + ",";
    payload +=     "\"humidity\":45";
    payload +=   "},";
    payload +=   "\"tamper\":{";
    payload +=     "\"strap_intact\":true,";
    payload +=     "\"skin_contact\":true,";
    payload +=     "\"tamper_alert\":" + String(tamper ? "true" : "false");
    payload +=   "}";
    payload += "},";
    payload += "\"alerts\":{";
    payload +=   "\"fall\":" + String(fall ? "true" : "false") + ",";
    payload +=   "\"tamper\":" + String(tamper ? "true" : "false") + ",";
    payload +=   "\"abnormal_vitals\":" + String(abnormal ? "true" : "false") + ",";
    payload +=   "\"low_battery\":" + String(lowBattery ? "true" : "false");
    payload += "}";
    payload += "}";

    int httpCode = http.POST(payload);
    if (httpCode > 0) {
      String resp = http.getString();
      Serial.printf("Wristband POST %d: %s\n", httpCode, resp.c_str());
    } else {
      Serial.printf("Wristband POST failed: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }

  delay(50);
}


