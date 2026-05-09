#include <WiFi.h>
#include <HTTPClient.h>

// Reference sketch for an ESP32 receiver node that reports detected RFID tags
// to the FastAPI backend using the "LocationUpdate" JSON schema.
//
// For simplicity this sends a fixed set of tags and a static location every
// 15 seconds. In a real deployment you would replace the fake tag list with
// reads from an RFID module such as an RDM6300 or long‑range 125kHz reader.

const char* WIFI_SSID     = "Brightburn";
const char* WIFI_PASSWORD = "p434/0Q2";

const char* BACKEND_BASE_URL = "http://192.168.254.102:8000";  // Adjust to backend IP
const char* RECEIVER_ID      = "RX-WARD-A-001";

unsigned long lastPostMs = 0;
const unsigned long POST_INTERVAL_MS = 15 * 1000;

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

  unsigned long now = millis();
  if (now - lastPostMs >= POST_INTERVAL_MS) {
    lastPostMs = now;

    HTTPClient http;
    String url = String(BACKEND_BASE_URL) + "/api/v1/location-update";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    // Example: we "see" P001 and P003 within the corridor
    String payload = "{";
    payload += "\"receiver_id\":\"" + String(RECEIVER_ID) + "\",";
    payload += "\"location\":{";
    payload +=   "\"zone\":\"General Ward A\",";
    payload +=   "\"room\":\"Corridor-1\",";
    payload +=   "\"coordinates\":{\"x\":10.5,\"y\":5.2,\"z\":0}";
    payload += "},";
    payload += "\"timestamp\":\"2025-12-18T14:30:50Z\",";
    payload += "\"detected_tags\":[";
    payload +=   "{";
    payload +=     "\"rfid_tag\":\"A1B2C3D4E5\",";
    payload +=     "\"patient_id\":\"P001\",";
    payload +=     "\"signal_strength\":-45,";
    payload +=     "\"estimated_distance\":2.3,";
    payload +=     "\"detection_count\":5";
    payload +=   "},";
    payload +=   "{";
    payload +=     "\"rfid_tag\":\"F6G7H8I9J0\",";
    payload +=     "\"patient_id\":\"P003\",";
    payload +=     "\"signal_strength\":-68,";
    payload +=     "\"estimated_distance\":5.1,";
    payload +=     "\"detection_count\":3";
    payload +=   "}";
    payload += "],";
    payload += "\"receiver_status\":{";
    payload +=   "\"uptime\":86400,";
    payload +=   "\"wifi_strength\":-52,";
    payload +=   "\"free_memory\":230000";
    payload += "}";
    payload += "}";

    int httpCode = http.POST(payload);
    if (httpCode > 0) {
      String resp = http.getString();
      Serial.printf("Receiver POST %d: %s\n", httpCode, resp.c_str());
    } else {
      Serial.printf("Receiver POST failed: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }

  delay(50);
}



