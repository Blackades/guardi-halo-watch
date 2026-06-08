/**
 * Guardi-Halo-Watch v2.0 Simplified Door Reader Firmware
 * Role: RFID Entry/Exit Station (125kHz RDM6300)
 * Features:
 * - Scans for 125kHz tags on two RDM6300 readers via UART.
 * - Detects direction:
 *   - Outer Reader (Serial1) -> Entry Event
 *   - Inner Reader (Serial2) -> Exit Event
 * - WiFi upload to door-event endpoint
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Configuration
const char* WIFI_SSID = "Brightburn";
const char* WIFI_PASS = "p434/0Q2";
const char* BACKEND_URL = "https://zuvoo.xyz/api/v1/door-event";
const char* READER_ID = "DR-WARD-A";

// RDM6300 Configuration
#define RDM_BAUD 9600
#define RX_OUTER 4   // Serial1 RX
#define TX_OUTER 2   // Dummy
#define RX_INNER 16  // Serial2 RX (Default)

// Cooldown state to prevent double reporting
struct LastRead {
    String uid;
    unsigned long timestamp;
};
#define COOLDOWN_MS 5000
LastRead last_read = {"", 0};

/**
 * Parses the 14-byte frame from RDM6300
 * Format: 0x02 [10 ASCII Data] [2 ASCII Checksum] 0x03
 */
String parseRDM(HardwareSerial &rdmSerial) {
    if (rdmSerial.available() >= 14) {
        // Find start byte
        while (rdmSerial.available() && rdmSerial.peek() != 0x02) {
            rdmSerial.read();
        }
        
        if (rdmSerial.available() >= 14 && rdmSerial.read() == 0x02) {
            char buffer[11];
            for (int i = 0; i < 10; i++) {
                buffer[i] = rdmSerial.read();
            }
            buffer[10] = '\0';
            
            // Skip checksum and consume end byte
            rdmSerial.read(); // Checksum 1
            rdmSerial.read(); // Checksum 2
            if (rdmSerial.peek() == 0x03) rdmSerial.read();
            
            return String(buffer);
        }
    }
    return "";
}

void uploadDoorEvent(String rfidUid, String action) {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, BACKEND_URL);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> doc;
    doc["reader_id"] = READER_ID;
    doc["rfid_uid"] = rfidUid;
    doc["action"] = action;
    doc["timestamp"] = "2026-04-14T00:00:00Z"; 

    String requestBody;
    serializeJson(doc, requestBody);
    
    int httpResponseCode = http.POST(requestBody);
    Serial.printf("[%s] Event sent, UID: %s, Response: %d\n", action.c_str(), rfidUid.c_str(), httpResponseCode);
    
    http.end();
}

void setup() {
    Serial.begin(115200);
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected");

    // Initialize RDM Readers via HardwareSerial
    Serial1.begin(RDM_BAUD, SERIAL_8N1, RX_OUTER, TX_OUTER);
    Serial2.begin(RDM_BAUD, SERIAL_8N1, RX_INNER, -1);

    Serial.println("Dual RDM6300 Readers Initialized");
    Serial.println("Outer Reader (Entry) on GPIO:" + String(RX_OUTER));
    Serial.println("Inner Reader (Exit) on GPIO:" + String(RX_INNER));
}

void processReader(HardwareSerial &rdmSerial, String action) {
    String uid = parseRDM(rdmSerial);
    if (uid != "") {
        // Cooldown check
        if (uid == last_read.uid && (millis() - last_read.timestamp) < COOLDOWN_MS) {
            return;
        }
        
        Serial.printf("Reader %s detected tag: %s\n", action.c_str(), uid.c_str());
        uploadDoorEvent(uid, action);
        
        last_read.uid = uid;
        last_read.timestamp = millis();
    }
}

void loop() {
    processReader(Serial1, "entry"); // Outer reader
    processReader(Serial2, "exit");  // Inner reader
    delay(10);
}
