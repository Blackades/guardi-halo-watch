/**
 * Guardi-Halo-Watch v2.1 Unified Node Firmware (Production)
 * Role: BLE Scanner (Room Tracking) + Multi RFID Reader (Door Entry/Exit)
 *
 * Production hardening over v2.0:
 *  - Real on-device RSSI calibration sampling (replaces browser-side mock data)
 *  - Backend-schema-correct location payloads (signal_strength, coordinates, receiver_status)
 *  - NTP-synced UTC timestamps (no more millis()-as-datetime)
 *  - Strict TLS validation against ISRG Root X1 (Let's Encrypt chain)
 *  - Non-blocking WiFi bring-up with AP fallback + self-healing reconnect
 *  - Bounded, NVS-persisted offline door-event queue (survives power loss)
 *  - Immediate event flush via task notification (low alarm latency)
 *  - Non-blocking buzzer task (no delay() inside RFID/network tasks)
 *  - HTTP Basic Auth + HTML escaping on the configuration web UI (Auth disabled for presentation)
 *  - Per-reader entry/exit direction configurable from the web UI
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SoftwareSerial.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include <vector>
#include <algorithm>

// --- Configuration ---
#define FIRMWARE_VERSION "2.1.0"

const char* WIFI_SSID = "Brightburn";
const char* WIFI_PASS = "p434/0Q2";

// Fallback access point when station WiFi is unreachable (allows on-site config/diagnostics)
const char* AP_FALLBACK_PASS = "guardi-setup";

const char* URL_LOCATION = "https://zuvoo.xyz/api/v1/location-update";
const char* URL_DOOR = "https://zuvoo.xyz/api/v1/door-event";
const char* URL_ASSIGNMENTS = "https://zuvoo.xyz/api/v1/patients/active-assignments";
const char* URL_READER_SYNC = "https://zuvoo.xyz/api/v1/readers/sync";

const char* NODE_ID = "UNIFIED-A1";
const char* WARD_ZONE = "WARD_A";
const char* ROOM_CODE = "R01";

// Physical placement of this node (sent in location updates; backend schema requires coordinates)
const float NODE_COORD_X = 0.0f;
const float NODE_COORD_Y = 0.0f;
const float NODE_COORD_Z = 0.0f;

// Web UI credentials (HTTP Basic Auth). Overridable via NVS namespace "web-auth".
const char* DEFAULT_WEB_USER = "admin";
const char* DEFAULT_WEB_PASS = "guardi2026";
String webAuthUser;
String webAuthPass;

// --- NTP / time ---
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";
const char* NTP_SERVER_3 = "time.cloudflare.com";
// Any epoch before mid-2025 means the clock has not been set yet.
const time_t TIME_VALID_THRESHOLD = 1750000000;

bool isTimeSynced() {
    return time(nullptr) > TIME_VALID_THRESHOLD;
}

// Epoch seconds (UTC) or 0 when the clock is not yet synced.
time_t epochNow() {
    time_t now = time(nullptr);
    return (now > TIME_VALID_THRESHOLD) ? now : 0;
}

// --- TLS Root CAs for zuvoo.xyz (Cloudflare Universal SSL) ---
// Cloudflare rotates leaf issuers between Google Trust Services and Let's
// Encrypt, so pin both roots: GTS Root R4 (current chain) and ISRG Root
// X1/X2. setCACert() accepts a bundle of concatenated PEM certificates.
static const char ROOT_CA_BUNDLE_PEM[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
-----END CERTIFICATE-----
)CERT";

// Configure a WiFiClientSecure for strict server validation.
// TLS certificate validation requires a correct wall clock, so callers must
// gate HTTPS traffic on isTimeSynced().
void configureSecureClient(WiFiClientSecure& client) {
    client.setCACert(ROOT_CA_BUNDLE_PEM);
    client.setHandshakeTimeout(10); // seconds
}

// Standard timeouts so a wedged TCP connection can never stall a task for minutes.
void configureHttpTimeouts(HTTPClient& http) {
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
}

// --- Flag to coordinate radio time between BLE and WiFi/HTTPS ---
volatile bool isNetworkBusy = false;

// Set once BLEDevice::init() has completed in setup(). The network task must
// not start TLS handshakes (~45KB transient heap) before this: a door-event
// scanned during boot once raced the Bluedroid startup allocator and
// crashed it (memset(NULL) in bta_sys_init, StoreProhibited on Core 0).
volatile bool bleInitDone = false;

// Shared iBeacon identifiers. MUST match GUARDI_BEACON_UUID / GUARDI_BEACON_MAJOR
// in wristband_v2.cpp. Any beacon not matching these is ignored by the scanner.
#define BEACON_UUID "43210000-1234-5678-1234-567890abcdef"
#define GUARDI_BEACON_MAJOR 100
// Same UUID as above, expressed as 16 raw bytes for fast adv-packet comparison.
static const uint8_t GUARDI_BEACON_UUID_BYTES[16] = {
    0x43, 0x21, 0x00, 0x00,
    0x12, 0x34, 0x56, 0x78,
    0x12, 0x34, 0x56, 0x78,
    0x90, 0xab, 0xcd, 0xef
};

// --- Buzzer (non-blocking, dedicated task) ---
#define BUZZER_PIN 22
#define BT_TOGGLE_PIN 23

// Global variable to track if BLE is enabled (toggled via button pin 23)
volatile bool bleEnabled = true;

TaskHandle_t buzzerTaskHandle = nullptr;
volatile unsigned long lastBuzzerTrigger = 0;
const unsigned long BUZZER_COOLDOWN_MS = 3000;

void TaskBuzzer(void *pvParameters) {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        Serial.println("!!! BUZZER ALARM - PATIENT ROOM EXIT DETECTED !!!");
        for (int i = 0; i < 3; i++) {
            digitalWrite(BUZZER_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(150));
            digitalWrite(BUZZER_PIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Safe to call from any task or callback: never blocks, rate-limited.
void triggerBuzzer() {
    unsigned long now = millis();
    if (now - lastBuzzerTrigger < BUZZER_COOLDOWN_MS) return;
    lastBuzzerTrigger = now;
    if (buzzerTaskHandle != nullptr) {
        xTaskNotifyGive(buzzerTaskHandle);
    }
}

// --- Offline Storage / Door Event Queue State ---
struct PendingDoorEvent {
    String rfid_uid;
    String action;
    String door_name;
    String reader_id;
    time_t epoch; // UTC seconds at scan time, 0 if clock was unsynced
};
std::vector<PendingDoorEvent> pendingDoorEvents;
SemaphoreHandle_t doorEventsMutex = nullptr;

// RAM cap: protects against OOM during multi-day outages.
const size_t MAX_QUEUED_EVENTS = 300;
// NVS persistence cap: newest N events survive power loss.
const size_t MAX_PERSISTED_EVENTS = 100;

TaskHandle_t networkSyncTaskHandle = nullptr;

struct PatientAssignmentLocal {
    String rfid_uid;
    String assigned_room;
};
std::vector<PatientAssignmentLocal> activeAssignments;
SemaphoreHandle_t assignmentsMutex = nullptr;

// --- RFID Pins (UART) ---
#define RDM_BAUD 9600

// --- Multi-Reader Configuration ---
// Default RX pins for the 7 readers. GPIO 16/17 are deliberately absent:
// on WROVER modules they are bonded to the internal PSRAM chip and can never
// be used for external peripherals (a reader wired there is silently dead).
const uint8_t READER_PINS[7] = {4, 13, 14, 5, 18, 19, 21};
const unsigned long READER_DEBOUNCE_MS = 5000; // 5-second cooldown

// GPIOs that are actually safe to use as an RFID RX line on this board.
// Excluded: 0/2/12/15 (boot straps), 1/3 (USB serial console), 6-11 (SPI
// flash), 16/17 (PSRAM on WROVER), 22 (buzzer), 23 (BT toggle button).
// 34-39 are input-only pins, which is perfect for an RX-only reader line
// (note: they lack internal pull-ups, but the RDM6300 drives its TX actively).
struct CandidateRxPin {
    uint8_t gpio;
    const char* note;
};
const CandidateRxPin CANDIDATE_RX_PINS[] = {
    {4,  ""}, {5,  ""}, {13, ""}, {14, ""},
    {16, "unusable on WROVER (PSRAM)"}, {17, "unusable on WROVER (PSRAM)"},
    {18, ""}, {19, ""}, {21, ""},
    {25, ""}, {26, ""}, {27, ""}, {32, ""}, {33, ""},
    {34, "input-only"}, {35, "input-only"}, {36, "input-only"}, {39, "input-only"},
};
const size_t CANDIDATE_RX_PIN_COUNT = sizeof(CANDIDATE_RX_PINS) / sizeof(CANDIDATE_RX_PINS[0]);

// True when this GPIO can really receive reader data on the board we are
// running on right now. 16/17 are valid on WROOM but bonded to the PSRAM
// die on WROVER — psramFound() tells us which module we actually have.
bool isPinUsableOnThisBoard(uint8_t gpio) {
    if ((gpio == 16 || gpio == 17) && psramFound()) return false;
    for (size_t i = 0; i < CANDIDATE_RX_PIN_COUNT; i++) {
        if (CANDIDATE_RX_PINS[i].gpio == gpio) return true;
    }
    return false;
}

// --- RFID Parsing Helpers ---
uint8_t hexVal(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

// Forward declarations
bool uploadDoorEventSynchronous(const PendingDoorEvent& ev);
void syncActiveAssignments();
float applyEmaFilter(uint16_t minor, int raw_rssi);
void persistDoorEventQueueLocked();

// --- Shared State ---
struct DetectedBeacon {
    uint16_t minor;
    int rssi;
    float distance;
};
std::vector<DetectedBeacon> detections;
SemaphoreHandle_t detectionsMutex;
SemaphoreHandle_t networkMutex = nullptr;

// --- Door Event Queue persistence (NVS) ---
// Serialized as a compact JSON array; survives reboot/power loss.
// Must be called while holding doorEventsMutex.
//
// Built by hand into a single reserved String instead of a 12KB
// DynamicJsonDocument: this runs on the RFID task on EVERY scan, and the
// big transient allocation raced TLS handshakes for heap — when it lost,
// the resulting std::bad_alloc aborted the whole node (Core 1 panic).
// All field values are constrained (hex UIDs, entry/exit, validated door
// names) so manual JSON assembly is safe here.
void persistDoorEventQueueLocked() {
    size_t start = (pendingDoorEvents.size() > MAX_PERSISTED_EVENTS)
                       ? pendingDoorEvents.size() - MAX_PERSISTED_EVENTS : 0;
    size_t count = pendingDoorEvents.size() - start;

    String blob;
    // ~96 bytes per serialized event; reserve up front so the String grows
    // once instead of realloc-thrashing a fragmented heap.
    if (!blob.reserve(count * 96 + 4)) {
        Serial.println("WARN: not enough heap to persist event queue (kept in RAM)");
        return;
    }
    blob = "[";
    for (size_t i = start; i < pendingDoorEvents.size(); i++) {
        const PendingDoorEvent& ev = pendingDoorEvents[i];
        if (i > start) blob += ',';
        blob += "{\"u\":\"";
        blob += ev.rfid_uid;
        blob += "\",\"a\":\"";
        blob += ev.action;
        blob += "\",\"d\":\"";
        blob += ev.door_name;
        blob += "\",\"r\":\"";
        blob += ev.reader_id;
        blob += "\",\"t\":";
        blob += String((uint32_t)ev.epoch);
        blob += '}';
    }
    blob += "]";

    Preferences prefs;
    prefs.begin("evt-queue", false);
    prefs.putBytes("q", blob.c_str(), blob.length());
    prefs.end();
}

void loadDoorEventQueue() {
    Preferences prefs;
    prefs.begin("evt-queue", true);
    size_t len = prefs.getBytesLength("q");
    if (len == 0 || len > 16384) { prefs.end(); return; }

    std::unique_ptr<char[]> buf(new char[len + 1]);
    prefs.getBytes("q", buf.get(), len);
    buf[len] = '\0';
    prefs.end();

    DynamicJsonDocument doc(12288);
    if (deserializeJson(doc, buf.get()) != DeserializationError::Ok) {
        Serial.println("Persisted event queue corrupt, discarding");
        return;
    }
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject o : arr) {
        PendingDoorEvent ev;
        ev.rfid_uid = o["u"].as<String>();
        ev.action = o["a"].as<String>();
        ev.door_name = o["d"].as<String>();
        ev.reader_id = o["r"].as<String>();
        ev.epoch = (time_t)(o["t"] | 0);
        if (ev.rfid_uid.length() > 0) pendingDoorEvents.push_back(ev);
    }
    Serial.printf("Restored %d unsent door events from NVS\n", (int)pendingDoorEvents.size());
}

void queueDoorEvent(const String& rfidUid, const String& action, const String& doorName, const String& readerId) {
    if (doorEventsMutex != nullptr && xSemaphoreTake(doorEventsMutex, portMAX_DELAY) == pdTRUE) {
        // Bound RAM usage: drop the oldest event when full (newest data wins).
        if (pendingDoorEvents.size() >= MAX_QUEUED_EVENTS) {
            pendingDoorEvents.erase(pendingDoorEvents.begin());
            Serial.println("WARN: event queue full, dropped oldest event");
        }
        PendingDoorEvent ev = {rfidUid, action, doorName, readerId, epochNow()};
        pendingDoorEvents.push_back(ev);
        persistDoorEventQueueLocked();
        xSemaphoreGive(doorEventsMutex);
        Serial.printf("Queued door event: %s [%s] at %s\n", rfidUid.c_str(), action.c_str(), doorName.c_str());
    }
    // Wake the sync task immediately so alarms/events reach the backend
    // with sub-second latency instead of waiting for the next poll cycle.
    if (networkSyncTaskHandle != nullptr) {
        xTaskNotifyGive(networkSyncTaskHandle);
    }
}

bool checkLocalRoomExit(const String& rfid_uid, const String& reader_room_code) {
    bool trigger = false;
    if (assignmentsMutex != nullptr && xSemaphoreTake(assignmentsMutex, (TickType_t)10) == pdTRUE) {
        for (const auto &pa : activeAssignments) {
            if (pa.rfid_uid == rfid_uid) {
                // If the patient is admitted/assigned to any room, any door scan triggers the buzzer
                if (pa.assigned_room.length() > 0) {
                    trigger = true;
                }
                break;
            }
        }
        xSemaphoreGive(assignmentsMutex);
    }
    return trigger;
}

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
    String direction; // "entry" or "exit" — configurable per reader
    Stream* serial;
    RDM6300State state;
    unsigned long last_scan_time;
    String last_uid;
    bool initialized;
    bool is_software_serial;
    bool pin_usable;             // false when the configured GPIO can't work on this board
    // Live wiring diagnostics — drives the honest status in the web UI.
    uint32_t bytes_received;     // every byte that arrived on the line
    uint32_t frames_ok;          // complete frames with a valid checksum
    uint32_t frames_bad;         // frames that failed checksum / framing
    unsigned long last_byte_time;  // millis() of the most recent byte (0 = never)
    unsigned long last_valid_time; // millis() of the most recent valid frame (0 = never)

    RFIDReaderConfig() : gpio_rx(0), serial(nullptr), last_scan_time(0), initialized(false),
                         is_software_serial(false), pin_usable(true), bytes_received(0),
                         frames_ok(0), frames_bad(0), last_byte_time(0), last_valid_time(0) {}
};

// --- Multi-Reader Manager Class ---
class MultiReaderManager {
private:
    std::vector<RFIDReaderConfig> readers;
    SemaphoreHandle_t readersMutex;

    String parseRDM(RFIDReaderConfig &reader) {
        Stream &rdmSerial = *reader.serial;
        RDM6300State &state = reader.state;
        while (rdmSerial.available()) {
            uint8_t b = rdmSerial.read();
            reader.bytes_received++;
            reader.last_byte_time = millis();
            Serial.printf("[DEBUG] Raw RFID Byte: 0x%02X (%s GPIO %d)\n", b, reader.hardware_id.c_str(), reader.gpio_rx);
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
                            // Read all 10 characters of the card ID
                            for (int i = 1; i <= 10; i++) tagID += (char)state.buffer[i];
                            tagID.toUpperCase();
                            reader.frames_ok++;
                            reader.last_valid_time = millis();
                            return tagID;
                        } else {
                            reader.frames_bad++;
                            Serial.println("[DEBUG] Checksum mismatch in RFID packet");
                        }
                    } else {
                        reader.frames_bad++;
                        Serial.println("[DEBUG] Packet end byte is not 0x03");
                    }
                }
            }
        }
        return "";
    }

public:
    // Honest, evidence-based status for the web UI:
    //  bad_pin      - the configured GPIO physically can't work on this board
    //  ok           - valid tag frames decoded recently
    //  noise        - bytes arrive but never form valid frames (wiring/baud/interference)
    //  no_data      - port is open but not a single byte has ever arrived (unwired/unpowered)
    //  idle         - has produced valid frames before, just nothing recently
    //  disconnected - serial port failed to initialize
    static String describeReaderStatus(const RFIDReaderConfig &r) {
        if (!r.pin_usable) return "bad_pin";
        if (!r.initialized) return "disconnected";
        unsigned long now = millis();
        if (r.last_valid_time != 0 && now - r.last_valid_time < 5UL * 60UL * 1000UL) return "ok";
        if (r.bytes_received > 0 && r.frames_ok == 0) return "noise";
        if (r.bytes_received == 0) return "no_data";
        return "idle";
    }

private:

public:
    MultiReaderManager() {
        readersMutex = xSemaphoreCreateMutex();
    }

    // Persisted pin assignments live in their own NVS namespace so a bad pin
    // choice can be corrected from the web UI and survive reboots.
    static uint8_t loadPersistedPin(int index, uint8_t fallback) {
        Preferences prefs;
        prefs.begin("reader-pins", true);
        String key = "READER_" + String(index + 1) + "_pin";
        uint8_t pin = (uint8_t)prefs.getUChar(key.c_str(), fallback);
        prefs.end();
        return pin;
    }

    static void persistPin(const String& hardware_id, uint8_t gpio) {
        Preferences prefs;
        prefs.begin("reader-pins", false);
        String key = hardware_id + "_pin";
        prefs.putUChar(key.c_str(), gpio);
        prefs.end();
    }

    void initializeReaders() {
        Serial.println("Initializing Multi-Reader Manager for 7 Doors...");
        if (psramFound()) {
            Serial.println("PSRAM detected (WROVER module): GPIO 16/17 are reserved and rejected for readers");
        }

        for (int i = 0; i < 7; i++) {
            String hardware_id = "READER_" + String(i + 1);
            uint8_t pin = loadPersistedPin(i, READER_PINS[i]);
            // Readers 1 and 2 get the two free hardware UARTs (immune to
            // BLE/WiFi interrupt jitter); the rest use SoftwareSerial.
            registerReader(i, pin, hardware_id);
        }

        Serial.printf("Multi-Reader Manager initialized with %d readers\n", getReaderCount());
    }

    // Open (or reopen) the serial port for a reader slot on the given pin.
    // Returns true when the port is up. Must be called with readersMutex held.
    bool openSerialLocked(int index, RFIDReaderConfig& config, uint8_t gpio_rx) {
        config.gpio_rx = gpio_rx;
        config.pin_usable = isPinUsableOnThisBoard(gpio_rx);
        config.initialized = false;
        config.state = RDM6300State(); // reset frame parser
        if (!config.pin_usable) {
            Serial.printf("REFUSED to open %s on GPIO %d: pin not usable on this board\n",
                          config.hardware_id.c_str(), gpio_rx);
            return false;
        }

        if (index == 0 || index == 1) {
            HardwareSerial* hw = (index == 0) ? &Serial1 : &Serial2;
            hw->end();
            hw->begin(RDM_BAUD, SERIAL_8N1, gpio_rx, -1);
            config.serial = hw;
            config.is_software_serial = false;
        } else {
            // Reuse the existing SoftwareSerial object when re-pinning.
            SoftwareSerial* sw = static_cast<SoftwareSerial*>(config.serial);
            if (sw == nullptr || !config.is_software_serial) {
                sw = new SoftwareSerial(gpio_rx, -1);
            } else {
                sw->end();
            }
            sw->begin(RDM_BAUD, SWSERIAL_8N1, gpio_rx, -1);
            config.serial = sw;
            config.is_software_serial = true;
        }
        config.initialized = true;
        Serial.printf("Opened %s reader %s on GPIO %d\n",
                      config.is_software_serial ? "software" : "hardware",
                      config.hardware_id.c_str(), gpio_rx);
        return true;
    }

    void registerReader(int index, uint8_t gpio_rx, String hardware_id) {
        if (xSemaphoreTake(readersMutex, portMAX_DELAY) == pdTRUE) {
            RFIDReaderConfig config;
            config.hardware_id = hardware_id;
            config.door_name = "Door " + String(index + 1); // Default name
            // Default direction heuristic: odd readers face entry, even face exit.
            // Overridden by NVS / web UI configuration after load.
            config.direction = ((index + 1) % 2 != 0) ? "entry" : "exit";
            config.last_scan_time = 0;
            config.last_uid = "";

            openSerialLocked(index, config, gpio_rx);

            readers.push_back(config);
            xSemaphoreGive(readersMutex);
        }
    }

    // Result codes for web-driven pin reassignment.
    enum class PinChangeResult { OK, UNKNOWN_READER, BAD_PIN, PIN_IN_USE, OPEN_FAILED };

    // Move a reader to a different RX GPIO at runtime and persist the choice.
    PinChangeResult reassignPin(const String& hardware_id, uint8_t new_gpio) {
        if (!isPinUsableOnThisBoard(new_gpio)) return PinChangeResult::BAD_PIN;

        PinChangeResult result = PinChangeResult::UNKNOWN_READER;
        if (xSemaphoreTake(readersMutex, portMAX_DELAY) == pdTRUE) {
            // One GPIO can only feed one reader.
            bool inUse = false;
            for (const auto &r : readers) {
                if (r.hardware_id != hardware_id && r.gpio_rx == new_gpio) { inUse = true; break; }
            }
            if (inUse) {
                result = PinChangeResult::PIN_IN_USE;
            } else {
                for (size_t i = 0; i < readers.size(); i++) {
                    if (readers[i].hardware_id == hardware_id) {
                        bool ok = openSerialLocked((int)i, readers[i], new_gpio);
                        // Fresh pin, fresh evidence: restart wiring diagnostics.
                        readers[i].bytes_received = 0;
                        readers[i].frames_ok = 0;
                        readers[i].frames_bad = 0;
                        readers[i].last_byte_time = 0;
                        readers[i].last_valid_time = 0;
                        result = ok ? PinChangeResult::OK : PinChangeResult::OPEN_FAILED;
                        break;
                    }
                }
            }
            xSemaphoreGive(readersMutex);
        }
        if (result == PinChangeResult::OK) {
            persistPin(hardware_id, new_gpio);
            Serial.printf("Reader %s reassigned to GPIO %d (persisted)\n", hardware_id.c_str(), new_gpio);
        }
        return result;
    }

    std::vector<RFIDReaderConfig> getReadersCopy() {
        std::vector<RFIDReaderConfig> copy;
        if (xSemaphoreTake(readersMutex, portMAX_DELAY) == pdTRUE) {
            copy = readers;
            xSemaphoreGive(readersMutex);
        }
        return copy;
    }

    void updateDoorName(const String& hardware_id, const String& door_name) {
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

    void updateDirection(const String& hardware_id, const String& direction) {
        if (direction != "entry" && direction != "exit") return;
        if (xSemaphoreTake(readersMutex, portMAX_DELAY) == pdTRUE) {
            for (auto &reader : readers) {
                if (reader.hardware_id == hardware_id) {
                    reader.direction = direction;
                    Serial.printf("Updated direction for %s: %s\n", hardware_id.c_str(), direction.c_str());
                    break;
                }
            }
            xSemaphoreGive(readersMutex);
        }
    }

    String getDoorName(const String& hardware_id) {
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

    String getDirection(const String& hardware_id) {
        String dir = "entry";
        if (xSemaphoreTake(readersMutex, portMAX_DELAY) == pdTRUE) {
            for (const auto &reader : readers) {
                if (reader.hardware_id == hardware_id) {
                    dir = reader.direction;
                    break;
                }
            }
            xSemaphoreGive(readersMutex);
        }
        return dir;
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

        if (xSemaphoreTake(readersMutex, (TickType_t)10) != pdTRUE) {
            lock_misses++;
            return;
        }
        {
            for (auto &reader : readers) {
                if (reader.initialized && reader.pin_usable && reader.serial != nullptr) {
                    String uid = parseRDM(reader);
                    if (uid != "") {
                        Serial.printf("[RFID Detection] Scanned RFID Tag: %s on %s\n", uid.c_str(), reader.hardware_id.c_str());
                        DoorEvent event = processTagDetection(reader, uid);
                        if (event.valid) {
                            pendingEvents.push_back(event);
                        }
                    }
                }
            }
            xSemaphoreGive(readersMutex);
        }

        // Process scanned events outside the readers lock
        for (const auto &event : pendingEvents) {
            // Local fail-safe: alarm immediately if a patient leaves their
            // assigned room, even with no backend connectivity.
            if (checkLocalRoomExit(event.uid, event.door_name)) {
                triggerBuzzer();
            }
            // Queue to persistent offline storage for synchronization
            queueDoorEvent(event.uid, event.action, event.door_name, event.reader_id);
        }
    }

    DoorEvent processTagDetection(RFIDReaderConfig& reader, const String& uid) {
        DoorEvent event;
        unsigned long current_time = millis();

        // Debounce: ignore repeat reads of the same tag within the cooldown window.
        if (uid != reader.last_uid || (current_time - reader.last_scan_time > READER_DEBOUNCE_MS)) {
            reader.last_uid = uid;
            reader.last_scan_time = current_time;

            Serial.printf("RFID Scan [%s - %s]: %s\n",
                          reader.hardware_id.c_str(),
                          reader.door_name.c_str(),
                          uid.c_str());

            event.valid = true;
            event.uid = uid;
            event.action = reader.direction;
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

    // Incremented whenever pollAllReaders() could not get the readers lock.
    // A high value means the RFID task is being starved of poll cycles.
    volatile uint32_t lock_misses = 0;

    // Periodic one-line health report from the RFID task. Shows whether the
    // task is alive, whether polling is actually happening, and the raw byte
    // counters per reader — the fastest way to localize "no tags detected".
    void printHeartbeat(uint32_t polls) {
        if (xSemaphoreTake(readersMutex, (TickType_t)50) != pdTRUE) {
            Serial.printf("[RFID] heartbeat: polls=%u lockmiss=%u (readers lock busy)\n",
                          polls, lock_misses);
            return;
        }
        String line = "[RFID] heartbeat: polls=" + String(polls) + " lockmiss=" + String(lock_misses);
        for (const auto &r : readers) {
            line += " | " + r.hardware_id + "@" + String(r.gpio_rx);
            line += r.is_software_serial ? "(sw)" : "(hw)";
            line += ": " + String(r.bytes_received) + "B/" + String(r.frames_ok) + "ok";
            if (!r.initialized) line += " UNINIT";
            if (!r.pin_usable) line += " BADPIN";
        }
        xSemaphoreGive(readersMutex);
        Serial.println(line);
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
        int gpio_pin;
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
    String backend_config_url;

public:
    ConfigurationManager() : last_sync_time(0) {
        backend_config_url = "https://zuvoo.xyz/api/v1/readers/";
        backend_config_url += NODE_ID;
        backend_config_url += "/config";
        syncMutex = xSemaphoreCreateMutex();
    }

    void loadFromStorage() {
        preferences.begin("door-config", true);
        Serial.println("Loading door configurations from storage...");

        if (multiReaderManager != nullptr) {
            for (int i = 0; i < 7; i++) {
                String hw_id = "READER_" + String(i + 1);
                String default_name = "Door " + String(i + 1);
                String default_dir = ((i + 1) % 2 != 0) ? "entry" : "exit";
                String door_name = preferences.getString(hw_id.c_str(), default_name);
                String dir_key = hw_id + "_dir";
                String direction = preferences.getString(dir_key.c_str(), default_dir);
                multiReaderManager->updateDoorName(hw_id, door_name);
                multiReaderManager->updateDirection(hw_id, direction);
                Serial.printf("Loaded: %s=%s (%s)\n", hw_id.c_str(), door_name.c_str(), direction.c_str());
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
                String dir_key = hw_id + "_dir";
                preferences.putString(hw_id.c_str(), multiReaderManager->getDoorName(hw_id));
                preferences.putString(dir_key.c_str(), multiReaderManager->getDirection(hw_id));
            }
            Serial.println("Door configurations saved to Preferences");
        }

        preferences.end();
    }

    bool fetchDoorMappings() {
        if (WiFi.status() != WL_CONNECTED || !isTimeSynced()) {
            return false;
        }

        WiFiClientSecure client;
        configureSecureClient(client);

        HTTPClient http;
        bool ok = false;
        if (http.begin(client, backend_config_url)) {
            configureHttpTimeouts(http);
            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            int httpResponseCode = http.GET();

            if (httpResponseCode == 200) {
                String payload = http.getString();
                DynamicJsonDocument doc(2048);
                DeserializationError error = deserializeJson(doc, payload);

                if (!error) {
                    Serial.println("Door mappings fetched successfully");

                    // Update door names from backend response
                    if (doc.containsKey("readers")) {
                        JsonArray readers = doc["readers"];
                        for (JsonObject reader : readers) {
                            String reader_id = reader["reader_id"].as<String>();
                            String door_name = reader["door_name"].as<String>();

                            if (multiReaderManager != nullptr && reader_id.length() > 0 && door_name.length() > 0) {
                                multiReaderManager->updateDoorName(reader_id, door_name);
                            }
                        }
                    }

                    // Save updated configuration to storage
                    saveToStorage();
                    ok = true;
                } else {
                    Serial.printf("JSON parsing failed: %s\n", error.c_str());
                }
            } else {
                Serial.printf("Door mappings HTTP GET failed, code: %d\n", httpResponseCode);
            }
            http.end();
        }
        return ok;
    }

    void queueSync(const String& readerId, const String& doorName, int gpioPin) {
        if (syncMutex != nullptr && xSemaphoreTake(syncMutex, portMAX_DELAY) == pdTRUE) {
            PendingSync sync = {readerId, doorName, gpioPin};
            pendingSyncs.push_back(sync);
            xSemaphoreGive(syncMutex);
        }
        if (networkSyncTaskHandle != nullptr) xTaskNotifyGive(networkSyncTaskHandle);
    }

    void queueCalibrationSync(float ref_rssi, float path_loss, float env_factor) {
        if (syncMutex != nullptr && xSemaphoreTake(syncMutex, portMAX_DELAY) == pdTRUE) {
            pendingCalibSync.pending = true;
            pendingCalibSync.reference_rssi = ref_rssi;
            pendingCalibSync.path_loss_exp = path_loss;
            pendingCalibSync.environmental_factor = env_factor;
            xSemaphoreGive(syncMutex);
        }
        if (networkSyncTaskHandle != nullptr) xTaskNotifyGive(networkSyncTaskHandle);
    }

    void syncWithBackend();

    void cacheReaderConfig(const String& hardware_id, const String& door_name, const String& direction) {
        preferences.begin("door-config", false);
        preferences.putString(hardware_id.c_str(), door_name);
        if (direction.length() > 0) {
            String dir_key = hardware_id + "_dir";
            preferences.putString(dir_key.c_str(), direction);
        }
        preferences.end();

        if (multiReaderManager != nullptr) {
            multiReaderManager->updateDoorName(hardware_id, door_name);
            if (direction.length() > 0) {
                multiReaderManager->updateDirection(hardware_id, direction);
            }
        }
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
        calibration_url = "https://zuvoo.xyz/api/v1/calibration/";
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
        Serial.printf("Calibration loaded: ref_rssi=%.1f, path_loss=%.2f, env_factor=%.2f, calibrated=%d\n",
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
        if (WiFi.status() != WL_CONNECTED || !isTimeSynced()) {
            Serial.println("Network/time unavailable, using offline calibration parameters");
            return;
        }

        // Ensure thread safety
        if (networkMutex != nullptr && xSemaphoreTake(networkMutex, portMAX_DELAY) == pdTRUE) {
            WiFiClientSecure client;
            configureSecureClient(client);

            HTTPClient http;
            if (http.begin(client, calibration_url)) {
                configureHttpTimeouts(http);
                http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
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
        // Clamp RSSI to the physically plausible BLE range
        if (rssi < -100) rssi = -100;
        if (rssi > -30) rssi = -30;

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

// --- Calibration Session (real on-device RSSI sampling) ---
// Replaces the old browser-side mock that fabricated RSSI values with
// Math.random(). The browser now only specifies the true measured distance;
// this class collects live, EMA-filtered RSSI readings from the BLE scanner,
// takes the median (outlier-robust), and fits the log-distance path-loss
// model with least-squares regression on the device itself.
class CalibrationSession {
public:
    struct Sample {
        float distance;
        float rssi;     // median of collected readings
        uint16_t count; // readings that contributed
    };

private:
    SemaphoreHandle_t mtx;
    bool active = false;
    bool collecting = false;
    uint16_t targetMinor = 0;  // 0 = lock onto first Guardi beacon seen
    uint16_t lockedMinor = 0;
    float pendingDistance = 0;
    unsigned long collectStart = 0;
    unsigned long lastActivity = 0;
    float lastRssi = 0;
    std::vector<float> readings;
    std::vector<Sample> samples;
    String lastError;

    static const uint16_t READINGS_TARGET = 8;
    static const uint16_t READINGS_MIN = 3;
    static const unsigned long COLLECT_TIMEOUT_MS = 12000;
    static const unsigned long SESSION_TIMEOUT_MS = 900000; // 15 min idle

    static float median(std::vector<float> v) {
        std::sort(v.begin(), v.end());
        size_t n = v.size();
        if (n == 0) return 0;
        return (n % 2) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0f;
    }

public:
    CalibrationSession() { mtx = xSemaphoreCreateMutex(); }

    void start(uint16_t minor) {
        if (xSemaphoreTake(mtx, portMAX_DELAY) == pdTRUE) {
            active = true;
            collecting = false;
            targetMinor = minor;
            lockedMinor = minor;
            samples.clear();
            readings.clear();
            lastError = "";
            lastActivity = millis();
            xSemaphoreGive(mtx);
        }
        Serial.printf("Calibration session started (target minor: %u)\n", minor);
    }

    void stop() {
        if (xSemaphoreTake(mtx, portMAX_DELAY) == pdTRUE) {
            active = false;
            collecting = false;
            xSemaphoreGive(mtx);
        }
    }

    bool isActive() {
        bool a = false;
        if (xSemaphoreTake(mtx, (TickType_t)10) == pdTRUE) {
            // Auto-expire idle sessions so BLE/web scheduling returns to normal.
            if (active && millis() - lastActivity > SESSION_TIMEOUT_MS) {
                active = false;
                collecting = false;
            }
            a = active;
            xSemaphoreGive(mtx);
        }
        return a;
    }

    bool isCollecting() {
        bool c = false;
        if (xSemaphoreTake(mtx, (TickType_t)10) == pdTRUE) {
            c = active && collecting;
            xSemaphoreGive(mtx);
        }
        return c;
    }

    // Begin collecting readings for one known-distance point.
    bool beginSample(float distance) {
        bool ok = false;
        if (xSemaphoreTake(mtx, portMAX_DELAY) == pdTRUE) {
            if (active && !collecting && distance >= 0.3f && distance <= 30.0f) {
                pendingDistance = distance;
                readings.clear();
                collecting = true;
                collectStart = millis();
                lastActivity = millis();
                lastError = "";
                ok = true;
            } else if (!active) {
                lastError = "no active session";
            } else if (collecting) {
                lastError = "sample already in progress";
            } else {
                lastError = "distance out of range (0.3-30m)";
            }
            xSemaphoreGive(mtx);
        }
        return ok;
    }

    // Fed by the BLE scan callback with raw RSSI readings of Guardi beacons.
    void onBeaconRssi(uint16_t minor, int rssi) {
        if (xSemaphoreTake(mtx, (TickType_t)5) != pdTRUE) return;
        if (active && collecting) {
            // Lock onto the first beacon seen if no explicit target was given,
            // then ignore every other beacon for the rest of the session.
            if (lockedMinor == 0) lockedMinor = minor;
            if (minor == lockedMinor) {
                readings.push_back((float)rssi);
                lastRssi = (float)rssi;
                lastActivity = millis();
                if (readings.size() >= READINGS_TARGET) {
                    samples.push_back({pendingDistance, median(readings), (uint16_t)readings.size()});
                    collecting = false;
                    Serial.printf("Calibration sample done: %.1fm -> %.1f dBm (%d readings)\n",
                                  pendingDistance, samples.back().rssi, (int)samples.back().count);
                }
            }
        }
        xSemaphoreGive(mtx);
    }

    // Called periodically from the main loop to time out stuck collections.
    void service() {
        if (xSemaphoreTake(mtx, (TickType_t)10) != pdTRUE) return;
        if (active && collecting && millis() - collectStart > COLLECT_TIMEOUT_MS) {
            if (readings.size() >= READINGS_MIN) {
                samples.push_back({pendingDistance, median(readings), (uint16_t)readings.size()});
                Serial.printf("Calibration sample (timeout, %d readings): %.1fm -> %.1f dBm\n",
                              (int)readings.size(), pendingDistance, samples.back().rssi);
            } else {
                lastError = "beacon not detected - check the wristband is on and within range";
                Serial.println("Calibration sample failed: insufficient readings");
            }
            collecting = false;
        }
        xSemaphoreGive(mtx);
    }

    bool deleteSample(size_t index) {
        bool ok = false;
        if (xSemaphoreTake(mtx, portMAX_DELAY) == pdTRUE) {
            if (index < samples.size()) {
                samples.erase(samples.begin() + index);
                lastActivity = millis();
                ok = true;
            }
            xSemaphoreGive(mtx);
        }
        return ok;
    }

    String statusJSON() {
        DynamicJsonDocument doc(2048);
        if (xSemaphoreTake(mtx, portMAX_DELAY) == pdTRUE) {
            doc["active"] = active;
            doc["collecting"] = collecting;
            doc["target_minor"] = targetMinor;
            doc["locked_minor"] = lockedMinor;
            doc["readings"] = (int)readings.size();
            doc["readings_target"] = READINGS_TARGET;
            doc["last_rssi"] = lastRssi;
            doc["error"] = lastError;
            JsonArray arr = doc.createNestedArray("samples");
            for (const auto& s : samples) {
                JsonObject o = arr.createNestedObject();
                o["distance"] = s.distance;
                o["rssi"] = s.rssi;
                o["count"] = s.count;
            }
            xSemaphoreGive(mtx);
        }
        String out;
        serializeJson(doc, out);
        return out;
    }

    // Least-squares fit of rssi = ref_rssi - 10*n*log10(d).
    // Returns true on success and fills the output parameters.
    bool finalize(float& refRssi, float& pathLoss, float& rSquared, String& error) {
        bool ok = false;
        if (xSemaphoreTake(mtx, portMAX_DELAY) == pdTRUE) {
            if (samples.size() < 3) {
                error = "need at least 3 samples";
            } else {
                // Require at least 2 distinct distances or the regression is degenerate.
                float dmin = samples[0].distance, dmax = samples[0].distance;
                for (const auto& s : samples) {
                    dmin = min(dmin, s.distance);
                    dmax = max(dmax, s.distance);
                }
                if (dmax - dmin < 0.4f) {
                    error = "samples must span at least two distinct distances";
                } else {
                    float n = (float)samples.size();
                    float sx = 0, sy = 0, sxx = 0, sxy = 0;
                    for (const auto& s : samples) {
                        float x = log10f(s.distance);
                        sx += x; sy += s.rssi; sxx += x * x; sxy += x * s.rssi;
                    }
                    float denom = n * sxx - sx * sx;
                    if (fabsf(denom) < 1e-6f) {
                        error = "degenerate sample distribution";
                    } else {
                        float slope = (n * sxy - sx * sy) / denom;
                        float intercept = (sy - slope * sx) / n;
                        float pl = -slope / 10.0f;
                        if (pl < 0.5f || pl > 8.0f) {
                            error = "fit produced implausible path-loss exponent (" + String(pl, 2) + ") - recollect samples";
                        } else if (intercept < -90.0f || intercept > -30.0f) {
                            error = "fit produced implausible reference RSSI (" + String(intercept, 1) + ") - recollect samples";
                        } else {
                            // R^2 quality metric
                            float meanY = sy / n, ssTot = 0, ssRes = 0;
                            for (const auto& s : samples) {
                                float pred = intercept + slope * log10f(s.distance);
                                ssTot += (s.rssi - meanY) * (s.rssi - meanY);
                                ssRes += (s.rssi - pred) * (s.rssi - pred);
                            }
                            rSquared = (ssTot > 1e-6f) ? 1.0f - ssRes / ssTot : 1.0f;
                            refRssi = intercept;
                            pathLoss = pl;
                            active = false;
                            collecting = false;
                            ok = true;
                        }
                    }
                }
            }
            xSemaphoreGive(mtx);
        }
        return ok;
    }
};

CalibrationSession calibSession;

// --- Configuration Webserver ---

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
        .container { max-width: 800px; margin: 0 auto; }
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
            box-shadow: 0 4px 6px -1px rgba(0,0,0,0.1), 0 2px 4px -1px rgba(0,0,0,0.06);
        }
        .reader-card h3 { margin-top: 0; color: #38bdf8; font-size: 18px; }
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
        select:focus, input[type="text"]:focus { outline: none; border-color: #38bdf8; }
        button {
            padding: 10px 20px;
            background: #38bdf8;
            color: #0f172a;
            border: none;
            cursor: pointer;
            border-radius: 6px;
            font-size: 14px;
            font-weight: bold;
        }
        button:hover { background: #0ea5e9; }
        .status { display: inline-block; padding: 4px 8px; border-radius: 4px; font-size: 12px; font-weight: bold; }
        .status-ok { background-color: #10b981; color: #0f172a; }
        .status-idle { background-color: #64748b; color: #f1f5f9; }
        .status-noise { background-color: #f59e0b; color: #0f172a; }
        .status-no_data { background-color: #ef4444; color: #ffffff; }
        .status-bad_pin { background-color: #a855f7; color: #ffffff; }
        .status-disconnected { background-color: #ef4444; color: #ffffff; }
        .diag { font-size: 12px; color: #64748b; margin: 5px 0; }
        .hint { font-size: 13px; color: #fbbf24; margin: 5px 0; }
        .pinrow { display: flex; gap: 10px; align-items: center; }
        .pinrow select { max-width: 220px; }
        .pinrow button { white-space: nowrap; }
        .info { color: #94a3b8; font-size: 14px; margin: 5px 0; }
        .nav { margin: 20px 0; display: flex; gap: 10px; }
        .nav a {
            display: inline-block;
            padding: 10px 20px;
            background: #1e293b;
            color: #38bdf8;
            text-decoration: none;
            border-radius: 6px;
            border: 1px solid #334155;
            font-weight: bold;
        }
        .nav a:hover { background: #38bdf8; color: #0f172a; }
        #sysbar { font-size: 13px; color: #94a3b8; margin-bottom: 10px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Guardi Halo Watch Door Configuration</h1>
        <div id="sysbar">Loading system status...</div>
        <div class="nav">
            <a href="/">Reader Assignment</a>
            <a href="/calibrate">BLE Distance Calibration</a>
        </div>
        <div id="readers">Loading RFID Reader Nodes...</div>
    </div>

    <script>
        function esc(s) {
            return String(s == null ? '' : s).replace(/[&<>"']/g, function(c) {
                return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];
            });
        }

        function toggleCustomInput(readerId) {
            var selectVal = document.getElementById('select_' + readerId).value;
            var inputField = document.getElementById('name_' + readerId);
            if (selectVal === 'custom') {
                inputField.style.display = 'block';
                inputField.value = '';
            } else {
                inputField.style.display = 'none';
                inputField.value = selectVal;
            }
        }

        async function loadStatus() {
            try {
                const r = await fetch('/api/status');
                const s = await r.json();
                document.getElementById('sysbar').textContent =
                    'FW v' + s.firmware + ' | Uptime ' + Math.floor(s.uptime_s/60) + 'm | Heap ' +
                    Math.floor(s.free_heap/1024) + 'KB | WiFi ' + (s.wifi.connected ? (s.wifi.rssi + ' dBm') : 'OFFLINE') +
                    ' | Clock ' + (s.time_synced ? 'synced' : 'NOT SYNCED') +
                    ' | Queued events: ' + s.queued_events;
            } catch (e) {}
        }

        var boardPins = null; // populated from /api/pins (board-aware candidate GPIOs)

        async function loadPins() {
            try {
                const r = await fetch('/api/pins');
                boardPins = await r.json();
            } catch (e) { boardPins = null; }
        }

        var STATUS_TEXT = {
            ok:           ['OK', 'Reading tags'],
            idle:         ['IDLE', 'Worked before, no recent scans'],
            noise:        ['NOISE', 'Bytes arrive but no valid frames - check baud/wiring/interference'],
            no_data:      ['NO DATA', 'Not a single byte received - check power (5V!), TX wire and tag type (125kHz EM4100)'],
            bad_pin:      ['BAD PIN', 'This GPIO cannot work on this board - reassign below'],
            disconnected: ['DISCONNECTED', 'Serial port failed to initialize']
        };

        function pinDropdown(rid, currentPin) {
            if (!boardPins) return '<span class="info">GPIO ' + esc(currentPin) + '</span>';
            var html = '<select id="pin_' + rid + '">';
            for (var i = 0; i < boardPins.pins.length; i++) {
                var p = boardPins.pins[i];
                var label = 'GPIO ' + p.gpio + (p.note ? ' (' + esc(p.note) + ')' : '');
                var sel = (p.gpio === currentPin) ? ' selected' : '';
                var dis = (!p.usable && p.gpio !== currentPin) ? ' disabled' : '';
                html += '<option value="' + p.gpio + '"' + sel + dis + '>' + label + '</option>';
            }
            // Current pin may not be in the candidate list at all (legacy config)
            var known = boardPins.pins.some(function(p) { return p.gpio === currentPin; });
            if (!known) html += '<option value="' + esc(currentPin) + '" selected>GPIO ' + esc(currentPin) + ' (unsupported)</option>';
            html += '</select>';
            return html;
        }

        async function updatePin(readerId) {
            var gpio = parseInt(document.getElementById('pin_' + readerId).value);
            try {
                const response = await fetch('/api/readers/pin', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ reader_id: readerId, gpio: gpio })
                });
                const result = await response.json();
                if (result.status === 'success') {
                    alert('Reader moved to GPIO ' + gpio + '. Re-wire the module TX to that pin if you have not already.');
                    loadReaders();
                } else {
                    alert('Error: ' + (result.message || 'Unknown error'));
                }
            } catch (error) {
                alert('Error updating pin: ' + error.message);
            }
        }

        async function loadReaders() {
            try {
                if (!boardPins) await loadPins();
                const response = await fetch('/api/readers');
                const readers = await response.json();
                const container = document.getElementById('readers');

                if (readers.length === 0) {
                    container.innerHTML = '<p>No RFID Reader modules connected.</p>';
                    return;
                }

                var knownRooms = ['ENTRY', 'R01', 'R02', 'R03', 'NURSE', 'REST', 'ISO'];
                var html = '';
                if (boardPins && boardPins.psram) {
                    html += '<p class="hint">WROVER board detected (PSRAM): GPIO 16/17 are reserved and cannot host readers.</p>';
                }
                for (var i = 0; i < readers.length; i++) {
                    var r = readers[i];
                    var rid = esc(r.reader_id);
                    var dname = esc(r.door_name);
                    var isCustom = knownRooms.indexOf(r.door_name) === -1 && r.door_name;
                    var st = STATUS_TEXT[r.status] || [esc(r.status).toUpperCase(), ''];
                    var d = r.diag || {};

                    html += '<div class="reader-card">';
                    html += '<h3>RFID Module ' + rid + '</h3>';
                    html += '<p class="info">Module Status: <span class="status status-' + esc(r.status) + '">' + st[0] + '</span>' +
                            (st[1] ? ' <span class="diag">' + st[1] + '</span>' : '') + '</p>';
                    html += '<p class="diag">UART: ' + esc(r.uart) + ' | bytes: ' + esc(d.bytes) +
                            ' | valid frames: ' + esc(d.frames_ok) + ' | bad frames: ' + esc(d.frames_bad) +
                            (d.last_valid_s >= 0 ? ' | last tag: ' + esc(d.last_valid_s) + 's ago' : '') + '</p>';
                    html += '<label class="info" style="font-weight: bold;">RX GPIO Pin: </label>';
                    html += '<div class="pinrow">' + pinDropdown(rid, r.gpio_pin) +
                            '<button onclick="updatePin(\'' + rid + '\')">Move Pin</button></div>';
                    html += '<label class="info" style="font-weight: bold;">Assign to Location/Room: </label><br/>';
                    html += '<select id="select_' + rid + '" onchange="toggleCustomInput(\'' + rid + '\')">';
                    var opts = [['ENTRY','Main Entry (ENTRY)'],['R01','Patient Room 1 (R01)'],['R02','Patient Room 2 (R02)'],
                                ['R03','Patient Room 3 (R03)'],['NURSE','Nurses Station (NURSE)'],['REST','Restricted Area (REST)'],
                                ['ISO','Isolation Room (ISO)']];
                    for (var j = 0; j < opts.length; j++) {
                        html += '<option value="' + opts[j][0] + '"' + (r.door_name === opts[j][0] ? ' selected' : '') + '>' + opts[j][1] + '</option>';
                    }
                    html += '<option value="custom"' + (isCustom ? ' selected' : '') + '>Custom Location Code...</option>';
                    html += '</select><br/>';
                    html += '<input type="text" id="name_' + rid + '" value="' + dname + '" style="display: ' + (isCustom ? 'block' : 'none') + ';" placeholder="Enter custom location name..." /><br/>';
                    html += '<label class="info" style="font-weight: bold;">Scan Direction: </label><br/>';
                    html += '<select id="dir_' + rid + '">';
                    html += '<option value="entry"' + (r.direction === 'entry' ? ' selected' : '') + '>Entry (into room)</option>';
                    html += '<option value="exit"' + (r.direction === 'exit' ? ' selected' : '') + '>Exit (out of room)</option>';
                    html += '</select><br/>';
                    html += '<button onclick="updateDoor(\'' + rid + '\')">Update Assignment</button>';
                    html += '</div>';
                }
                container.innerHTML = html;
            } catch (error) {
                document.getElementById('readers').innerHTML = '<p>Error loading RFID modules: ' + esc(error.message) + '</p>';
            }
        }

        async function updateDoor(readerId) {
            var selectVal = document.getElementById('select_' + readerId).value;
            var doorName = selectVal;
            if (selectVal === 'custom') {
                doorName = document.getElementById('name_' + readerId).value;
            }
            var direction = document.getElementById('dir_' + readerId).value;

            if (!doorName || doorName.trim() === '') {
                alert('Please enter or select a valid location code');
                return;
            }
            if (!/^[A-Za-z0-9 _-]{1,32}$/.test(doorName)) {
                alert('Location code may only contain letters, numbers, spaces, dashes and underscores (max 32 chars)');
                return;
            }

            try {
                const response = await fetch('/api/readers/update', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ reader_id: readerId, door_name: doorName, direction: direction })
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
        loadStatus();
        setInterval(loadReaders, 10000);
        setInterval(loadStatus, 5000);
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
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }
        h1 { color: #333; border-bottom: 2px solid #007bff; padding-bottom: 10px; }
        .nav { margin: 20px 0; }
        .nav a { display: inline-block; padding: 10px 20px; background: #28a745; color: white; text-decoration: none; border-radius: 3px; }
        .nav a:hover { background: #218838; }
        .wizard-container { background: white; padding: 30px; border-radius: 5px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 800px; }
        .step { display: none; }
        .step.active { display: block; }
        .step h2 { color: #007bff; margin-top: 0; }
        .instructions { background: #e7f3ff; padding: 15px; border-left: 4px solid #007bff; margin: 20px 0; }
        input[type="number"] { width: 150px; padding: 8px; border: 1px solid #ddd; border-radius: 3px; font-size: 14px; }
        button { padding: 10px 20px; background: #007bff; color: white; border: none; cursor: pointer; border-radius: 3px; font-size: 14px; margin: 5px; }
        button:hover { background: #0056b3; }
        button:disabled { background: #9bbbdd; cursor: not-allowed; }
        button.secondary { background: #6c757d; }
        button.secondary:hover { background: #545b62; }
        button.success { background: #28a745; }
        button.success:hover { background: #218838; }
        table { width: 100%; border-collapse: collapse; margin: 20px 0; }
        th, td { padding: 10px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background: #007bff; color: white; }
        .delete-btn { background: #dc3545; padding: 5px 10px; font-size: 12px; }
        .delete-btn:hover { background: #c82333; }
        .result-box { background: #d4edda; border: 1px solid #c3e6cb; padding: 20px; border-radius: 5px; margin: 20px 0; }
        .result-box h3 { color: #155724; margin-top: 0; }
        .error-box { background: #f8d7da; border: 1px solid #f5c6cb; color: #721c24; padding: 12px; border-radius: 5px; margin: 12px 0; display: none; }
        .progress { text-align: center; color: #666; margin: 20px 0; }
        .live { font-weight: bold; color: #007bff; }
    </style>
</head>
<body>
    <h1>Distance Calibration Wizard</h1>
    <div class="nav">
        <a href="/">Back to Configuration</a>
    </div>

    <div class="wizard-container">
        <div class="error-box" id="errorBox"></div>

        <div class="step active" id="step1">
            <h2>Step 1: Introduction</h2>
            <div class="instructions">
                <h3>Welcome to the Calibration Wizard</h3>
                <p>This wizard calibrates Bluetooth distance estimation using <strong>real signal measurements</strong> taken by this node.</p>
                <p><strong>What you will need:</strong></p>
                <ul>
                    <li>One powered-on wristband beacon (keep all other wristbands far away, or enter its Minor ID below)</li>
                    <li>A measuring tape</li>
                    <li>At least 3-4 distance points (recommended: 1m, 2m, 3m, 5m)</li>
                </ul>
                <p><strong>How it works:</strong></p>
                <ol>
                    <li>Place the beacon at a known distance from this node</li>
                    <li>Click "Collect Sample" — the node gathers ~8 live RSSI readings and stores the median</li>
                    <li>Repeat for multiple distances</li>
                    <li>The node fits the path-loss model on-device and saves the result</li>
                </ol>
            </div>
            <label>Beacon Minor ID (optional, locks to first beacon seen if blank): </label>
            <input type="number" id="targetMinor" min="0" max="65535" placeholder="auto" />
            <br/>
            <button onclick="startCalibration()">Start Calibration</button>
        </div>

        <div class="step" id="step2">
            <h2>Step 2: Collect RSSI Samples</h2>
            <div class="instructions">
                <p>Place the beacon at a known distance and click "Collect Sample".</p>
                <p>The node measures the live signal strength at that distance. Hold the beacon still while collecting (takes 5-15 seconds).</p>
            </div>
            <div>
                <label>Distance (meters): </label>
                <input type="number" id="distance" min="0.5" max="20" step="0.5" value="1.0" />
                <button id="collectBtn" onclick="collectSample()">Collect Sample</button>
            </div>
            <p class="progress" id="sampleStatus">Ready to collect samples</p>
            <button class="secondary" onclick="showStep(3)">Review Samples</button>
        </div>

        <div class="step" id="step3">
            <h2>Step 3: Review Samples</h2>
            <div class="instructions">
                <p>Review your collected samples. You need at least 3 samples spanning at least two distances to proceed.</p>
            </div>
            <table id="samplesTable">
                <thead>
                    <tr>
                        <th>Distance (m)</th>
                        <th>Measured RSSI (dBm)</th>
                        <th>Readings</th>
                        <th>Action</th>
                    </tr>
                </thead>
                <tbody id="samplesBody"></tbody>
            </table>
            <p class="progress" id="sampleCount">0 samples collected</p>
            <button class="secondary" onclick="showStep(2)">Collect More Samples</button>
            <button onclick="finalizeCalibration()" id="finalizeBtn" disabled>Finalize Calibration</button>
        </div>

        <div class="step" id="step4">
            <h2>Step 4: Calibration Complete</h2>
            <div class="result-box">
                <h3>Calibration Successful!</h3>
                <p>Computed on-device from your measured samples:</p>
                <ul>
                    <li><strong>Reference RSSI (1m):</strong> <span id="resultRefRSSI">-59</span> dBm</li>
                    <li><strong>Path Loss Exponent:</strong> <span id="resultPathLoss">2.0</span></li>
                    <li><strong>Fit Quality (R&sup2;):</strong> <span id="resultR2">-</span></li>
                </ul>
                <p>Parameters are saved on the device and synced to the backend.</p>
                <p id="fitWarning" style="color:#856404; display:none;">Warning: fit quality is low (R&sup2; &lt; 0.7). Consider recalibrating with more carefully measured distances.</p>
            </div>
            <button class="success" onclick="location.href='/'">Return to Configuration</button>
            <button class="secondary" onclick="restartCalibration()">Start New Calibration</button>
        </div>
    </div>

    <script>
        var deviceSamples = [];

        function showError(msg) {
            var box = document.getElementById('errorBox');
            box.textContent = msg;
            box.style.display = msg ? 'block' : 'none';
        }

        function showStep(step) {
            showError('');
            var steps = document.querySelectorAll('.step');
            for (var i = 0; i < steps.length; i++) steps[i].classList.remove('active');
            document.getElementById('step' + step).classList.add('active');
            if (step === 3) refreshSamples();
        }

        async function getStatus() {
            const response = await fetch('/api/calibration/status');
            return await response.json();
        }

        async function startCalibration() {
            try {
                var minor = parseInt(document.getElementById('targetMinor').value) || 0;
                const response = await fetch('/api/calibration/start', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ minor: minor })
                });
                const result = await response.json();
                if (result.status === 'calibration_started') {
                    deviceSamples = [];
                    showStep(2);
                } else {
                    showError(result.message || 'Failed to start calibration');
                }
            } catch (error) {
                showError('Error starting calibration: ' + error.message);
            }
        }

        async function collectSample() {
            var distance = parseFloat(document.getElementById('distance').value);
            if (!(distance >= 0.5 && distance <= 20)) {
                alert('Please enter a distance between 0.5 and 20 meters');
                return;
            }

            var btn = document.getElementById('collectBtn');
            btn.disabled = true;
            showError('');
            document.getElementById('sampleStatus').textContent = 'Requesting sample collection...';

            try {
                const response = await fetch('/api/calibration/sample', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ distance: distance })
                });
                const result = await response.json();
                if (result.status !== 'collecting') {
                    showError(result.message || 'Could not start sample collection');
                    btn.disabled = false;
                    return;
                }

                // Poll the device until this sample finishes (~5-15s of live BLE readings)
                var before = -1;
                for (var i = 0; i < 30; i++) {
                    await new Promise(function(r) { setTimeout(r, 1000); });
                    var s = await getStatus();
                    if (before === -1) before = s.samples.length - (s.collecting ? 0 : 1);
                    if (!s.collecting) {
                        deviceSamples = s.samples;
                        if (s.error) {
                            showError(s.error);
                            document.getElementById('sampleStatus').textContent = 'Sample failed - beacon not seen';
                        } else if (s.samples.length > 0) {
                            var last = s.samples[s.samples.length - 1];
                            document.getElementById('sampleStatus').innerHTML =
                                'Sample collected: <span class="live">' + last.distance.toFixed(1) + 'm @ ' +
                                last.rssi.toFixed(1) + ' dBm</span> (' + last.count + ' readings, ' +
                                s.samples.length + ' total samples)';
                            document.getElementById('distance').value = (distance + 1.0).toFixed(1);
                        }
                        btn.disabled = false;
                        return;
                    }
                    document.getElementById('sampleStatus').textContent =
                        'Collecting live RSSI... ' + s.readings + '/' + s.readings_target + ' readings' +
                        (s.locked_minor ? ' (beacon minor ' + s.locked_minor + ')' : ' (searching for beacon...)');
                }
                showError('Sample collection timed out');
                btn.disabled = false;
            } catch (error) {
                document.getElementById('sampleStatus').textContent = 'Error collecting sample';
                showError('Error: ' + error.message);
                btn.disabled = false;
            }
        }

        async function refreshSamples() {
            try {
                var s = await getStatus();
                deviceSamples = s.samples;
            } catch (e) {}
            updateSamplesTable();
        }

        function updateSamplesTable() {
            var tbody = document.getElementById('samplesBody');
            tbody.innerHTML = '';
            for (var i = 0; i < deviceSamples.length; i++) {
                var sample = deviceSamples[i];
                var row = tbody.insertRow();
                row.insertCell(0).textContent = sample.distance.toFixed(1);
                row.insertCell(1).textContent = sample.rssi.toFixed(1);
                row.insertCell(2).textContent = sample.count;
                var actionCell = row.insertCell(3);
                var deleteBtn = document.createElement('button');
                deleteBtn.textContent = 'Delete';
                deleteBtn.className = 'delete-btn';
                deleteBtn.setAttribute('data-index', i);
                deleteBtn.onclick = function() { deleteSample(parseInt(this.getAttribute('data-index'))); };
                actionCell.appendChild(deleteBtn);
            }
            var word = deviceSamples.length !== 1 ? 'samples' : 'sample';
            document.getElementById('sampleCount').textContent = deviceSamples.length + ' ' + word + ' collected';
            document.getElementById('finalizeBtn').disabled = deviceSamples.length < 3;
        }

        async function deleteSample(index) {
            try {
                await fetch('/api/calibration/sample/delete', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ index: index })
                });
            } catch (e) {}
            refreshSamples();
        }

        async function finalizeCalibration() {
            if (deviceSamples.length < 3) {
                alert('You need at least 3 samples to calibrate');
                return;
            }
            showError('');
            try {
                const response = await fetch('/api/calibration/finalize', { method: 'POST' });
                const result = await response.json();
                if (result.status === 'success') {
                    document.getElementById('resultRefRSSI').textContent = result.reference_rssi.toFixed(1);
                    document.getElementById('resultPathLoss').textContent = result.path_loss_exponent.toFixed(2);
                    document.getElementById('resultR2').textContent = result.r_squared.toFixed(3);
                    document.getElementById('fitWarning').style.display = result.r_squared < 0.7 ? 'block' : 'none';
                    showStep(4);
                } else {
                    showError(result.message || 'Calibration failed');
                }
            } catch (err) {
                showError('Network error finalizing calibration: ' + err.message);
            }
        }

        function restartCalibration() {
            deviceSamples = [];
            showStep(1);
        }
    </script>
</body>
</html>
)rawhtml";

// Global variable to track the last time the webserver handled a request
volatile unsigned long lastWebserverRequestTime = 0;
// Tracks whether a non-blocking BLE scan is currently in progress
volatile bool bleScanInProgress = false;
// Global callback pointer to prevent memory leaks during toggle re-inits
class MyAdvertisedDeviceCallbacks;
MyAdvertisedDeviceCallbacks* advertisedDeviceCallbacks = nullptr;

class WebRequestTracker : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest *request) override {
        lastWebserverRequestTime = millis();
        // Abort any in-flight BLE scan so the radio is free to flush the HTTP
        // response cleanly — EXCEPT during calibration sampling, where live BLE
        // readings are the whole point and the status responses are tiny.
        if (bleEnabled && bleScanInProgress && !calibSession.isCollecting()) {
            BLEDevice::getScan()->stop();
            bleScanInProgress = false;
        }
        return false; // Return false so the request is passed down to other handlers
    }
    void handleRequest(AsyncWebServerRequest *request) override {}
};

// Server-side door-name validation: defends against stored XSS and junk data.
bool isValidDoorName(const String& name) {
    if (name.length() == 0 || name.length() > 32) return false;
    for (size_t i = 0; i < name.length(); i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != ' ' && c != '-' && c != '_') return false;
    }
    return true;
}

class ConfigurationWebserver {
private:
    AsyncWebServer server;
    String node_id;
    MultiReaderManager* readerManager;
    DistanceCalculator* distanceCalc;
    ConfigurationManager* configMgr;
    bool mdns_started;

    bool checkAuth(AsyncWebServerRequest *request) {
        if (request->authenticate(webAuthUser.c_str(), webAuthPass.c_str())) return true;
        request->requestAuthentication("HaloWatch");
        return false;
    }

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
        Serial.printf("Login: %s / <configured password>\n", webAuthUser.c_str());
        Serial.println("======================================");
    }

    void setupRoutes() {
        // Serve main configuration page via PROGMEM (zero-allocation)
        server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
            Serial.printf("[Web] Request received for /\n");
            Serial.printf("[Web] Serving full CONFIG_HTML page...\n");
            AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html; charset=utf-8", CONFIG_HTML);
            response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
            request->send(response);
            Serial.printf("[Web] CONFIG_HTML response sent\n");
        });

        // API: Node health/status (auth-protected diagnostics)
        server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
            DynamicJsonDocument doc(768);
            doc["node_id"] = node_id;
            doc["firmware"] = FIRMWARE_VERSION;
            doc["uptime_s"] = (uint32_t)(millis() / 1000);
            doc["free_heap"] = (uint32_t)ESP.getFreeHeap();
            doc["min_free_heap"] = (uint32_t)ESP.getMinFreeHeap();
            doc["time_synced"] = isTimeSynced();
            doc["epoch"] = (uint32_t)epochNow();
            JsonObject wifi = doc.createNestedObject("wifi");
            wifi["connected"] = WiFi.status() == WL_CONNECTED;
            wifi["rssi"] = WiFi.RSSI();
            wifi["ip"] = WiFi.localIP().toString();
            wifi["ap_fallback_active"] = (WiFi.getMode() & WIFI_MODE_AP) != 0;
            size_t queued = 0;
            if (doorEventsMutex != nullptr && xSemaphoreTake(doorEventsMutex, (TickType_t)50) == pdTRUE) {
                queued = pendingDoorEvents.size();
                xSemaphoreGive(doorEventsMutex);
            }
            doc["queued_events"] = (uint32_t)queued;
            doc["readers"] = readerManager != nullptr ? readerManager->getReaderCount() : 0;
            doc["calibrated"] = distanceCalc != nullptr && distanceCalc->isCalibrated();
            String out;
            serializeJson(doc, out);
            request->send(200, "application/json", out);
        });

        // API: Get all readers
        server.on("/api/readers", HTTP_GET, [this](AsyncWebServerRequest *request) {
            String json = getReadersJSON();
            request->send(200, "application/json", json);
        });

        // API: GPIO pins that can host a reader on THIS board (PSRAM-aware).
        // The UI builds its pin dropdowns from this so users can only ever
        // pick a pin that physically works.
        server.on("/api/pins", HTTP_GET, [this](AsyncWebServerRequest *request) {
            DynamicJsonDocument doc(1536);
            doc["psram"] = psramFound();
            JsonArray pins = doc.createNestedArray("pins");
            for (size_t i = 0; i < CANDIDATE_RX_PIN_COUNT; i++) {
                const auto &c = CANDIDATE_RX_PINS[i];
                JsonObject p = pins.createNestedObject();
                p["gpio"] = c.gpio;
                p["usable"] = isPinUsableOnThisBoard(c.gpio);
                p["note"] = c.note;
            }
            String out;
            serializeJson(doc, out);
            request->send(200, "application/json", out);
        });

        // API: Move a reader to a different RX GPIO at runtime
        server.on("/api/readers/pin", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                DynamicJsonDocument doc(256);
                if (deserializeJson(doc, (const char*)data, len) != DeserializationError::Ok ||
                    !doc.containsKey("reader_id") || !doc.containsKey("gpio")) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing reader_id or gpio\"}");
                    return;
                }
                String readerId = doc["reader_id"].as<String>();
                int gpio = doc["gpio"] | -1;
                if (gpio < 0 || gpio > 39 || readerManager == nullptr) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid GPIO\"}");
                    return;
                }
                auto res = readerManager->reassignPin(readerId, (uint8_t)gpio);
                switch (res) {
                    case MultiReaderManager::PinChangeResult::OK:
                        request->send(200, "application/json", "{\"status\":\"success\"}");
                        break;
                    case MultiReaderManager::PinChangeResult::BAD_PIN:
                        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"That GPIO cannot host a reader on this board\"}");
                        break;
                    case MultiReaderManager::PinChangeResult::PIN_IN_USE:
                        request->send(409, "application/json", "{\"status\":\"error\",\"message\":\"GPIO already assigned to another reader\"}");
                        break;
                    case MultiReaderManager::PinChangeResult::UNKNOWN_READER:
                        request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"Unknown reader_id\"}");
                        break;
                    default:
                        request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to open serial port on that pin\"}");
                }
            }
        );

        // API: Update door name / direction
        server.on("/api/readers/update", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                // Parse JSON body (bounded copy: data is not NUL-terminated)
                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, (const char*)data, len);

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
                String direction = doc.containsKey("direction") ? doc["direction"].as<String>() : "";

                if (!isValidDoorName(doorName)) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid door name (letters, numbers, space, - and _ only; max 32)\"}");
                    return;
                }
                if (direction.length() > 0 && direction != "entry" && direction != "exit") {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid direction\"}");
                    return;
                }

                // Find GPIO pin for the backend sync payload
                int gpioPin = -1;
                if (readerManager != nullptr) {
                    std::vector<RFIDReaderConfig> copy = readerManager->getReadersCopy();
                    bool known = false;
                    for (const auto &r : copy) {
                        if (r.hardware_id == readerId) { gpioPin = r.gpio_rx; known = true; break; }
                    }
                    if (!known) {
                        request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"Unknown reader_id\"}");
                        return;
                    }
                }

                // Persist locally + apply in memory, then queue async backend sync
                if (configMgr != nullptr) {
                    configMgr->cacheReaderConfig(readerId, doorName, direction);
                    configMgr->queueSync(readerId, doorName, gpioPin);
                }

                request->send(200, "application/json", "{\"status\":\"success\"}");
            }
        );

        // API: Get calibration parameters
        server.on("/api/calibration", HTTP_GET, [this](AsyncWebServerRequest *request) {
            String json = getCalibrationJSON();
            request->send(200, "application/json", json);
        });

        // API: Start a real calibration session (live BLE sampling)
        server.on("/api/calibration/start", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                uint16_t minor = 0;
                DynamicJsonDocument doc(128);
                if (len > 0 && deserializeJson(doc, (const char*)data, len) == DeserializationError::Ok) {
                    minor = doc["minor"] | 0;
                }
                calibSession.start(minor);
                request->send(200, "application/json", "{\"status\":\"calibration_started\"}");
            }
        );

        // API: Begin collecting live RSSI readings at one known distance
        server.on("/api/calibration/sample", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                DynamicJsonDocument doc(256);
                if (deserializeJson(doc, (const char*)data, len) != DeserializationError::Ok || !doc.containsKey("distance")) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing distance\"}");
                    return;
                }
                float distance = doc["distance"].as<float>();
                if (calibSession.beginSample(distance)) {
                    request->send(200, "application/json", "{\"status\":\"collecting\"}");
                } else {
                    request->send(409, "application/json", "{\"status\":\"error\",\"message\":\"Could not start collection (no session, busy, or bad distance)\"}");
                }
            }
        );

        // API: Poll calibration session state (live readings, collected samples)
        server.on("/api/calibration/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
            request->send(200, "application/json", calibSession.statusJSON());
        });

        // API: Delete a collected sample by index
        server.on("/api/calibration/sample/delete", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                DynamicJsonDocument doc(128);
                if (deserializeJson(doc, (const char*)data, len) != DeserializationError::Ok || !doc.containsKey("index")) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing index\"}");
                    return;
                }
                bool ok = calibSession.deleteSample(doc["index"].as<size_t>());
                request->send(ok ? 200 : 404, "application/json",
                              ok ? "{\"status\":\"success\"}" : "{\"status\":\"error\",\"message\":\"Bad index\"}");
            }
        );

        // API: Finalize — on-device least-squares fit, save, queue backend sync
        server.on("/api/calibration/finalize", HTTP_POST, [this](AsyncWebServerRequest *request) {
            float refRssi, pathLoss, r2;
            String err;
            if (!calibSession.finalize(refRssi, pathLoss, r2, err)) {
                DynamicJsonDocument doc(256);
                doc["status"] = "error";
                doc["message"] = err;
                String out;
                serializeJson(doc, out);
                request->send(400, "application/json", out);
                return;
            }

            const float envFactor = 1.0f;
            if (distanceCalc != nullptr) {
                DistanceCalibration newCalib;
                newCalib.reference_rssi = refRssi;
                newCalib.path_loss_exp = pathLoss;
                newCalib.environmental_factor = envFactor;
                distanceCalc->updateCalibration(newCalib);
                distanceCalc->saveToStorage();
            }
            if (configMgr != nullptr) {
                configMgr->queueCalibrationSync(refRssi, pathLoss, envFactor);
            }

            DynamicJsonDocument doc(256);
            doc["status"] = "success";
            doc["reference_rssi"] = refRssi;
            doc["path_loss_exponent"] = pathLoss;
            doc["environmental_factor"] = envFactor;
            doc["r_squared"] = r2;
            String out;
            serializeJson(doc, out);
            request->send(200, "application/json", out);
        });

        // API: Save calibration parameters directly (manual/advanced use)
        server.on("/api/calibration/save", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL,
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                DynamicJsonDocument doc(512);
                DeserializationError error = deserializeJson(doc, (const char*)data, len);

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

                // Sanity-check parameters before accepting them
                if (ref_rssi < -90.0f || ref_rssi > -30.0f || path_loss < 0.5f || path_loss > 8.0f ||
                    env_factor < 0.1f || env_factor > 10.0f) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Parameters outside plausible range\"}");
                    return;
                }

                if (distanceCalc != nullptr) {
                    DistanceCalibration newCalib;
                    newCalib.reference_rssi = ref_rssi;
                    newCalib.path_loss_exp = path_loss;
                    newCalib.environmental_factor = env_factor;
                    distanceCalc->updateCalibration(newCalib);
                    distanceCalc->saveToStorage();
                }

                if (configMgr != nullptr) {
                    configMgr->queueCalibrationSync(ref_rssi, path_loss, env_factor);
                }

                request->send(200, "application/json", "{\"status\":\"success\"}");
            }
        );

        // Calibration wizard page via PROGMEM (zero-allocation)
        server.on("/calibrate", HTTP_GET, [this](AsyncWebServerRequest *request) {
            AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html; charset=utf-8", CALIBRATION_HTML);
            response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
            request->send(response);
        });

        // 404 handler
        server.onNotFound([](AsyncWebServerRequest *request) {
            request->send(404, "text/plain", "Not found");
        });
    }

    String getReadersJSON() {
        DynamicJsonDocument doc(6144);
        JsonArray jsonReaders = doc.to<JsonArray>();

        if (readerManager != nullptr) {
            std::vector<RFIDReaderConfig> readersCopy = readerManager->getReadersCopy();
            unsigned long now = millis();
            for (const auto &r : readersCopy) {
                JsonObject readerObj = jsonReaders.createNestedObject();
                readerObj["reader_id"] = r.hardware_id;
                readerObj["gpio_pin"] = r.gpio_rx;
                readerObj["door_name"] = r.door_name;
                readerObj["direction"] = r.direction;
                readerObj["status"] = MultiReaderManager::describeReaderStatus(r);
                readerObj["uart"] = r.is_software_serial ? "software" : "hardware";
                // Wiring diagnostics so the UI can explain *why* a reader is silent
                JsonObject diag = readerObj.createNestedObject("diag");
                diag["bytes"] = r.bytes_received;
                diag["frames_ok"] = r.frames_ok;
                diag["frames_bad"] = r.frames_bad;
                diag["last_byte_s"] = r.last_byte_time == 0 ? -1 : (int)((now - r.last_byte_time) / 1000);
                diag["last_valid_s"] = r.last_valid_time == 0 ? -1 : (int)((now - r.last_valid_time) / 1000);
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

    bool syncConfigToBackend(const String& readerId, const String& doorName, int gpioPin) {
        // POST configuration update to backend server with retry logic
        const int MAX_RETRIES = 3;
        const int BASE_DELAY_MS = 1000;

        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            if (WiFi.status() != WL_CONNECTED || !isTimeSynced()) {
                Serial.println("Network/time unavailable, cannot sync config to backend");
                return false;
            }

            WiFiClientSecure client;
            configureSecureClient(client);

            HTTPClient http;
            if (http.begin(client, URL_READER_SYNC)) {
                configureHttpTimeouts(http);
                http.addHeader("Content-Type", "application/json");

                DynamicJsonDocument doc(512);
                doc["node_id"] = node_id;
                doc["reader_id"] = readerId;
                doc["door_name"] = doorName;
                if (gpioPin >= 0) doc["gpio_pin"] = gpioPin;

                String payload;
                serializeJson(doc, payload);

                Serial.printf("Syncing config to backend (attempt %d/%d): %s -> %s\n",
                             attempt + 1, MAX_RETRIES, readerId.c_str(), doorName.c_str());

                int httpCode = http.POST(payload);

                if (httpCode == 200 || httpCode == 201) {
                    Serial.printf("Config sync successful (HTTP %d)\n", httpCode);
                    http.end();
                    return true;
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
                vTaskDelay(pdMS_TO_TICKS(BASE_DELAY_MS * (1 << attempt))); // 1s, 2s
            }
        }

        Serial.println("Config sync failed after all retries - configuration saved locally");
        return false;
    }

    bool syncCalibrationToBackend(float reference_rssi, float path_loss_exp, float env_factor) {
        // POST calibration parameters to backend server with retry logic
        const int MAX_RETRIES = 3;
        const int BASE_DELAY_MS = 1000;

        String url = "https://zuvoo.xyz/api/v1/calibration/";
        url += node_id;
        url += "/sync";

        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            if (WiFi.status() != WL_CONNECTED || !isTimeSynced()) {
                Serial.println("Network/time unavailable, cannot sync calibration to backend");
                return false;
            }

            WiFiClientSecure client;
            configureSecureClient(client);

            HTTPClient http;
            if (http.begin(client, url)) {
                configureHttpTimeouts(http);
                http.addHeader("Content-Type", "application/json");

                DynamicJsonDocument doc(512);
                doc["node_id"] = node_id;
                doc["reference_rssi"] = reference_rssi;
                doc["path_loss_exponent"] = path_loss_exp;
                doc["environmental_factor"] = env_factor;

                String payload;
                serializeJson(doc, payload);

                Serial.printf("Syncing calibration to backend (attempt %d/%d)\n",
                             attempt + 1, MAX_RETRIES);

                int httpCode = http.POST(payload);

                if (httpCode == 200 || httpCode == 201) {
                    Serial.printf("Calibration sync successful (HTTP %d)\n", httpCode);
                    http.end();
                    return true;
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
                vTaskDelay(pdMS_TO_TICKS(BASE_DELAY_MS * (1 << attempt)));
            }
        }

        Serial.println("Calibration sync failed after all retries");
        return false;
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
        bool sent = false;
        if (configWebserver != nullptr) {
            if (networkMutex != nullptr && xSemaphoreTake(networkMutex, portMAX_DELAY) == pdTRUE) {
                sent = configWebserver->syncCalibrationToBackend(localCalibCopy.reference_rssi,
                                                                 localCalibCopy.path_loss_exp,
                                                                 localCalibCopy.environmental_factor);
                xSemaphoreGive(networkMutex);
            }
        }
        // Re-queue on failure so a calibration done while offline still reaches the backend.
        if (!sent && syncMutex != nullptr && xSemaphoreTake(syncMutex, (TickType_t)10) == pdTRUE) {
            if (!pendingCalibSync.pending) pendingCalibSync = localCalibCopy;
            xSemaphoreGive(syncMutex);
        }
    }

    // 2. Process any pending reader-config pushes to backend asynchronously
    std::vector<PendingSync> localCopy;
    if (syncMutex != nullptr && xSemaphoreTake(syncMutex, (TickType_t)10) == pdTRUE) {
        localCopy = pendingSyncs;
        pendingSyncs.clear();
        xSemaphoreGive(syncMutex);
    }

    std::vector<PendingSync> failedSyncs;
    for (const auto &sync : localCopy) {
        bool sent = false;
        if (configWebserver != nullptr) {
            if (networkMutex != nullptr && xSemaphoreTake(networkMutex, portMAX_DELAY) == pdTRUE) {
                sent = configWebserver->syncConfigToBackend(sync.reader_id, sync.door_name, sync.gpio_pin);
                xSemaphoreGive(networkMutex);
            }
        }
        if (!sent) failedSyncs.push_back(sync);
    }
    if (!failedSyncs.empty() && syncMutex != nullptr && xSemaphoreTake(syncMutex, (TickType_t)10) == pdTRUE) {
        // Keep config changes durable across connectivity outages (bounded).
        for (const auto &s : failedSyncs) {
            if (pendingSyncs.size() < 20) pendingSyncs.push_back(s);
        }
        xSemaphoreGive(syncMutex);
    }

    // 3. Process local Door Events Queue
    std::vector<PendingDoorEvent> eventsToSync;
    if (doorEventsMutex != nullptr && xSemaphoreTake(doorEventsMutex, (TickType_t)10) == pdTRUE) {
        eventsToSync = pendingDoorEvents;
        pendingDoorEvents.clear();
        xSemaphoreGive(doorEventsMutex);
    }

    std::vector<PendingDoorEvent> failedEvents;
    bool anyAttempted = !eventsToSync.empty();
    for (const auto &ev : eventsToSync) {
        bool sent = false;
        if (networkMutex != nullptr && xSemaphoreTake(networkMutex, (TickType_t)2000 / portTICK_PERIOD_MS) == pdTRUE) {
            sent = uploadDoorEventSynchronous(ev);
            xSemaphoreGive(networkMutex);
        }
        if (!sent) {
            failedEvents.push_back(ev);
        }
    }

    // Put failed events back in queue (preserving order) and persist the new state
    if (anyAttempted && doorEventsMutex != nullptr && xSemaphoreTake(doorEventsMutex, portMAX_DELAY) == pdTRUE) {
        if (!failedEvents.empty()) {
            pendingDoorEvents.insert(pendingDoorEvents.begin(), failedEvents.begin(), failedEvents.end());
            Serial.printf("Offline Sync: %d events failed to upload, retained in local cache\n", (int)failedEvents.size());
        }
        persistDoorEventQueueLocked();
        xSemaphoreGive(doorEventsMutex);
    }

    // 4. Periodic pull configuration and patient assignments from backend
    unsigned long current_time = millis();
    if (current_time - last_sync_time >= 15000) { // Sync updates every 15s
        last_sync_time = current_time;

        if (networkMutex != nullptr && xSemaphoreTake(networkMutex, portMAX_DELAY) == pdTRUE) {
            bool success = fetchDoorMappings();
            syncActiveAssignments();
            xSemaphoreGive(networkMutex);
            if (success) {
                Serial.println("Configuration and active assignments sync completed");
            }
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

    // Bounded wait: this runs inside the BLE callback, never block it for long.
    if (emaFiltersMutex != nullptr && xSemaphoreTake(emaFiltersMutex, (TickType_t)50) == pdTRUE) {
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
        if (!advertisedDevice.haveManufacturerData()) return;

        std::string strManufacturerData = advertisedDevice.getManufacturerData();
        // iBeacon frames are exactly 25 bytes; bounds-check before copying.
        if (strManufacturerData.length() < 25 || strManufacturerData.length() > 100) return;

        uint8_t cManufacturerData[100];
        strManufacturerData.copy((char *)cManufacturerData, strManufacturerData.length());

        if (cManufacturerData[0] == 0x4C && cManufacturerData[1] == 0x00) {
            // Verify this is our Guardi tag: proximity UUID at bytes [4..19]
            // and major at bytes [20..21] must match the shared constants.
            if (memcmp(&cManufacturerData[4], GUARDI_BEACON_UUID_BYTES, 16) != 0) {
                return; // foreign iBeacon, ignore
            }
            uint16_t major = (cManufacturerData[20] << 8) | cManufacturerData[21];
            if (major != GUARDI_BEACON_MAJOR) {
                return; // wrong fleet / version, ignore
            }
            uint16_t minor = (cManufacturerData[22] << 8) | cManufacturerData[23];
            int rssi = advertisedDevice.getRSSI();

            // Feed the calibration session with raw readings when sampling.
            calibSession.onBeaconRssi(minor, rssi);

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
                // Deduplicate per beacon within a scan window: keep the latest
                // (EMA-filtered) reading so the backend gets one row per tag.
                bool updated = false;
                for (auto &existing : detections) {
                    if (existing.minor == minor) {
                        existing.rssi = b.rssi;
                        existing.distance = b.distance;
                        updated = true;
                        break;
                    }
                }
                if (!updated) detections.push_back(b);
                xSemaphoreGive(detectionsMutex);
            }
        }
    }
};

// --- Upload Logic ---
void uploadLocation() {
    if (WiFi.status() != WL_CONNECTED || !isTimeSynced()) return;

    // Snapshot + clear detections quickly, then do slow network I/O without
    // holding the detections lock.
    std::vector<DetectedBeacon> snapshot;
    if (xSemaphoreTake(detectionsMutex, (TickType_t)100) == pdTRUE) {
        snapshot = detections;
        detections.clear();
        xSemaphoreGive(detectionsMutex);
    }
    if (snapshot.empty()) return;

    isNetworkBusy = true;

    // Use the thread-safe network mutex with 15s timeout
    if (networkMutex != nullptr && xSemaphoreTake(networkMutex, (TickType_t)15000 / portTICK_PERIOD_MS) == pdTRUE) {
        WiFiClientSecure client;
        configureSecureClient(client);

        HTTPClient http;
        if (http.begin(client, URL_LOCATION)) {
            configureHttpTimeouts(http);
            http.addHeader("Content-Type", "application/json");

            // Payload matches backend schemas.LocationUpdate exactly:
            // receiver_id, location{zone,room,coordinates{x,y,z}}, timestamp,
            // detected_tags[{ble_minor,signal_strength,estimated_distance}],
            // receiver_status{uptime,wifi_strength,free_memory}.
            DynamicJsonDocument doc(3072);
            doc["receiver_id"] = NODE_ID;
            doc["timestamp"] = (uint32_t)epochNow(); // UTC epoch seconds

            JsonObject loc = doc.createNestedObject("location");
            loc["zone"] = WARD_ZONE;
            loc["room"] = ROOM_CODE;
            JsonObject coords = loc.createNestedObject("coordinates");
            coords["x"] = NODE_COORD_X;
            coords["y"] = NODE_COORD_Y;
            coords["z"] = NODE_COORD_Z;

            JsonObject st = doc.createNestedObject("receiver_status");
            st["uptime"] = (uint32_t)(millis() / 1000);
            st["wifi_strength"] = WiFi.RSSI();
            st["free_memory"] = (uint32_t)ESP.getFreeHeap();

            JsonArray tags = doc.createNestedArray("detected_tags");
            for (auto const& b : snapshot) {
                JsonObject tag = tags.createNestedObject();
                tag["ble_minor"] = b.minor;
                tag["signal_strength"] = b.rssi;
                tag["estimated_distance"] = b.distance;
            }

            String body;
            serializeJson(doc, body);
            int httpResponseCode = http.POST(body);
            if (httpResponseCode == 200 || httpResponseCode == 201) {
                Serial.printf("Location Update Sent (%d tags, Status: %d)\n", (int)snapshot.size(), httpResponseCode);
            } else if (httpResponseCode > 0) {
                Serial.printf("Location Update rejected (Status: %d): %s\n", httpResponseCode, http.getString().c_str());
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

// Format epoch seconds as ISO-8601 UTC for the backend's datetime fields.
String iso8601(time_t t) {
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return String(buf);
}

bool uploadDoorEventSynchronous(const PendingDoorEvent& ev) {
    if (WiFi.status() != WL_CONNECTED || !isTimeSynced()) return false;

    bool success = false;
    WiFiClientSecure client;
    configureSecureClient(client);

    HTTPClient http;
    if (http.begin(client, URL_DOOR)) {
        configureHttpTimeouts(http);
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<512> doc;
        doc["node_id"] = NODE_ID;
        doc["reader_id"] = ev.reader_id;
        doc["door_name"] = ev.door_name;
        doc["rfid_uid"] = ev.rfid_uid;
        doc["action"] = ev.action;
        // Real scan timestamp (UTC). Events captured before the clock synced
        // omit the field so the backend stamps them with its own time.
        if (ev.epoch > 0) {
            doc["timestamp"] = iso8601(ev.epoch);
        }

        String body;
        serializeJson(doc, body);
        int httpResponseCode = http.POST(body);

        if (httpResponseCode == 200 || httpResponseCode == 201) {
            String payload = http.getString();
            Serial.printf("Door Event Sent: %s -> %s at %s (Status: %d)\n",
                          ev.rfid_uid.c_str(), ev.action.c_str(), ev.door_name.c_str(), httpResponseCode);

            // Parse response to check for buzzer_trigger
            DynamicJsonDocument responseDoc(512);
            DeserializationError error = deserializeJson(responseDoc, payload);
            if (!error) {
                if (responseDoc.containsKey("buzzer_trigger") && responseDoc["buzzer_trigger"].as<bool>()) {
                    triggerBuzzer();
                }
            }
            success = true;
        } else if (httpResponseCode == 400 || httpResponseCode == 422) {
            // Permanently malformed event: log loudly and drop it instead of
            // retrying forever and clogging the queue for valid events.
            Serial.printf("Door Event REJECTED by backend (%d), dropping: %s\n",
                          httpResponseCode, http.getString().c_str());
            success = true;
        } else {
            Serial.printf("Error sending Door Event: %d\n", httpResponseCode);
        }
        http.end();
    }
    return success;
}

void syncActiveAssignments() {
    if (WiFi.status() != WL_CONNECTED || !isTimeSynced()) return;

    WiFiClientSecure client;
    configureSecureClient(client);

    HTTPClient http;
    if (http.begin(client, URL_ASSIGNMENTS)) {
        configureHttpTimeouts(http);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        int httpResponseCode = http.GET();
        if (httpResponseCode == 200) {
            String payload = http.getString();
            DynamicJsonDocument doc(4096);
            DeserializationError error = deserializeJson(doc, payload);

            if (!error) {
                size_t count = 0;
                if (assignmentsMutex != nullptr && xSemaphoreTake(assignmentsMutex, portMAX_DELAY) == pdTRUE) {
                    activeAssignments.clear();
                    JsonArray arr = doc.as<JsonArray>();
                    for (JsonObject obj : arr) {
                        PatientAssignmentLocal pa;
                        pa.rfid_uid = obj["rfid_uid"].as<String>();
                        pa.assigned_room = obj["room"].as<String>();
                        if (pa.rfid_uid.length() > 0) activeAssignments.push_back(pa);
                    }
                    count = activeAssignments.size();
                    xSemaphoreGive(assignmentsMutex);
                }
                Serial.printf("Synced %d patient room assignments successfully\n", (int)count);
            } else {
                Serial.printf("Error parsing active assignments JSON: %s\n", error.c_str());
            }
        } else {
            Serial.printf("Error getting active assignments: %d\n", httpResponseCode);
        }
        http.end();
    }
}

// --- WiFi resilience ---
volatile unsigned long lastWifiOkTime = 0;
const unsigned long WIFI_REBOOT_THRESHOLD_MS = 20UL * 60UL * 1000UL; // self-heal reboot after 20 min offline
bool apFallbackActive = false;

void startApFallback() {
    if (apFallbackActive) return;
    String apSsid = String("HaloWatch-") + NODE_ID;
    WiFi.mode(WIFI_AP_STA);
    if (WiFi.softAP(apSsid.c_str(), AP_FALLBACK_PASS)) {
        apFallbackActive = true;
        Serial.printf("AP fallback started: SSID=%s IP=%s\n",
                      apSsid.c_str(), WiFi.softAPIP().toString().c_str());
    }
}

void stopApFallback() {
    if (!apFallbackActive) return;
    // Don't kick an engineer who is actively connected to the fallback AP.
    if (WiFi.softAPgetStationNum() > 0) return;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apFallbackActive = false;
    Serial.println("AP fallback stopped (station WiFi restored)");
}

void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
            lastWifiOkTime = millis();
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("WiFi disconnected, auto-reconnect engaged");
            break;
        default:
            break;
    }
}

// Periodic network housekeeping run from the sync task.
void maintainConnectivity() {
    if (WiFi.status() == WL_CONNECTED) {
        lastWifiOkTime = millis();
        stopApFallback();
        // Kick off NTP (re-)sync if the clock is still unset.
        static unsigned long lastNtpAttempt = 0;
        if (!isTimeSynced() && millis() - lastNtpAttempt > 30000) {
            lastNtpAttempt = millis();
            configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
            Serial.println("NTP sync requested...");
        }
    } else {
        // Actively kick reconnection every 30s: the built-in auto-reconnect
        // can silently stall after BT radio cycles or AP-side drops, leaving
        // the node offline until a manual reboot.
        static unsigned long lastReconnectKick = 0;
        if (millis() - lastReconnectKick > 30000) {
            lastReconnectKick = millis();
            Serial.println("WiFi down - forcing reconnect attempt");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASS);
        }
        // Surface a fallback AP for on-site diagnostics during outages.
        if (millis() - lastWifiOkTime > 60000) {
            startApFallback();
        }
        // Self-heal: if station WiFi has been gone a long time and nobody is
        // using the fallback AP, reboot. The event queue is already persisted.
        if (millis() - lastWifiOkTime > WIFI_REBOOT_THRESHOLD_MS && WiFi.softAPgetStationNum() == 0) {
            Serial.println("WiFi down for 20+ minutes - rebooting to self-heal");
            if (doorEventsMutex != nullptr && xSemaphoreTake(doorEventsMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
                persistDoorEventQueueLocked();
                xSemaphoreGive(doorEventsMutex);
            }
            ESP.restart();
        }
    }
}

// --- Tasks ---
void TaskRFID(void *pvParameters) {
    Serial.println("RFID Task Started on Core 1");
    uint32_t polls = 0;
    unsigned long lastHeartbeat = 0;
    for (;;) {
        if (multiReaderManager != nullptr) {
            multiReaderManager->pollAllReaders();
            polls++;
            // Once every 10s: prove the task is alive and show per-reader
            // byte counters so a silent UART is immediately visible.
            if (millis() - lastHeartbeat > 10000) {
                lastHeartbeat = millis();
                multiReaderManager->printHeartbeat(polls);
            }
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void TaskNetworkSync(void *pvParameters) {
    Serial.println("Network Sync Task Started on Core 0");
    vTaskDelay(3000 / portTICK_PERIOD_MS); // Let webserver and network settle

    // Hold all heap-hungry HTTPS work until the Bluedroid stack has finished
    // initializing. Its startup allocations don't check for failure; a TLS
    // handshake running concurrently can starve it and panic Core 0.
    while (!bleInitDone) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Wait (bounded) for the clock before the first HTTPS calls so TLS
    // certificate validation works on the initial pulls.
    for (int i = 0; i < 20 && !isTimeSynced(); i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (isTimeSynced()) {
        Serial.printf("Clock synced: %s\n", iso8601(epochNow()).c_str());
    } else {
        Serial.println("Clock not yet synced; HTTPS deferred until NTP completes");
    }

    // Initial calibration pull from backend
    if (distanceCalculator != nullptr) {
        distanceCalculator->loadCalibration();
    }

    // Initial door mappings pull from backend
    if (configManager != nullptr && networkMutex != nullptr &&
        xSemaphoreTake(networkMutex, portMAX_DELAY) == pdTRUE) {
        configManager->fetchDoorMappings();
        syncActiveAssignments();
        xSemaphoreGive(networkMutex);
    }

    for (;;) {
        maintainConnectivity();
        if (configManager != nullptr) {
            configManager->syncWithBackend();
        }
        // Sleep up to 10s, but wake immediately when a door event / config
        // change is queued so alarms reach the backend with minimal latency.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));
    }
}
void disableBluetooth() {
    if (bleScanInProgress) {
        BLEDevice::getScan()->stop();
        bleScanInProgress = false;
    }
    // Graceful shutdown: BLEDevice::deinit walks bluedroid disable/deinit and
    // controller disable/deinit IN ORDER. The old code called btStop()
    // directly, ripping the controller out from under the running host stack
    // — Bluedroid's teardown then hit vQueueDelete(NULL) and panicked.
    // deinit(false) keeps controller memory so BLE can be re-enabled later.
    BLEDevice::deinit(false);
    Serial.println("[System] Bluetooth stack shut down cleanly (BLEDevice::deinit).");
}

void enableBluetooth() {
    // BLEDevice::init is a no-op if already initialized; after deinit(false)
    // it brings the whole stack back up in the correct order (no btStart()).
    BLEDevice::init(NODE_ID);
    BLEScan* pBLEScan = BLEDevice::getScan();
    if (advertisedDeviceCallbacks == nullptr) {
        advertisedDeviceCallbacks = new MyAdvertisedDeviceCallbacks();
    }
    pBLEScan->setAdvertisedDeviceCallbacks(advertisedDeviceCallbacks);
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(150); // Scan interval (150ms)
    pBLEScan->setWindow(50);    // Scan window (50ms) -> 33% duty cycle for stable BLE/WiFi coexistence
    Serial.println("[System] Bluetooth stack re-initialized.");
}

void setup() {
    Serial.begin(115200);
    Serial.printf("\nGuardi Halo Watch Unified Node v%s booting...\n", FIRMWARE_VERSION);

    // Initialize Mutexes
    detectionsMutex = xSemaphoreCreateMutex();
    emaFiltersMutex = xSemaphoreCreateMutex();
    networkMutex = xSemaphoreCreateMutex();
    doorEventsMutex = xSemaphoreCreateMutex();
    assignmentsMutex = xSemaphoreCreateMutex();
    
    pinMode(BT_TOGGLE_PIN, INPUT_PULLUP);

    // Load web UI credentials (overridable via NVS "web-auth" namespace)
    {
        Preferences prefs;
        prefs.begin("web-auth", true);
        webAuthUser = prefs.getString("user", DEFAULT_WEB_USER);
        webAuthPass = prefs.getString("pass", DEFAULT_WEB_PASS);
        prefs.end();
    }

    // Restore any door events that were queued before a power loss
    loadDoorEventQueue();

    // Buzzer task first: local exit alarms must work even with zero connectivity
    xTaskCreatePinnedToCore(TaskBuzzer, "TaskBuzzer", 2048, NULL, 2, &buzzerTaskHandle, 1);

    // Initialize Wi-Fi (non-blocking: the node is fully functional offline —
    // readers scan, buzzer alarms, events queue persistently).
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(onWiFiEvent);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi");
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 20000) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected");
        lastWifiOkTime = millis();
        // Start NTP immediately: timestamps and TLS validation depend on it.
        configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    } else {
        Serial.println("\nWiFi unavailable - continuing offline (auto-reconnect active)");
        startApFallback();
    }

    Serial.printf("[Heap] after WiFi: %u\n", (unsigned)ESP.getFreeHeap());

    // Initialize BLE FIRST, while heap is at its maximum and no other task
    // exists yet. Bluedroid's startup (bta_sys_init) allocates WITHOUT
    // checking for failure: bringing it up at the heap low-point — or while
    // the RFID task races it with a door-event allocation — caused repeated
    // StoreProhibited panics (memset(NULL)). Order matters more than size.
    BLEDevice::init(NODE_ID);
    {
        BLEScan* pBLEScan = BLEDevice::getScan();
        advertisedDeviceCallbacks = new MyAdvertisedDeviceCallbacks();
        pBLEScan->setAdvertisedDeviceCallbacks(advertisedDeviceCallbacks);
        pBLEScan->setActiveScan(true);
        pBLEScan->setInterval(150); // Scan interval (150ms)
        pBLEScan->setWindow(50);    // Scan window (50ms) -> 33% duty cycle for stable BLE/WiFi coexistence
    }
    bleInitDone = true;
    Serial.printf("[Heap] after BLE init: %u\n", (unsigned)ESP.getFreeHeap());

    // Initialize Multi-Reader Manager and Offline Config (from Preferences)
    multiReaderManager = new MultiReaderManager();
    multiReaderManager->initializeReaders();

    // Initialize Configuration Manager (from Preferences only - fast/local)
    configManager = new ConfigurationManager();
    configManager->loadFromStorage();

    // Initialize Distance Calculator (from Preferences only - fast/local)
    distanceCalculator = new DistanceCalculator();
    distanceCalculator->loadFromStorage();

    Serial.printf("[Heap] after readers/config: %u\n", (unsigned)ESP.getFreeHeap());

    // Create Multi-Core Tasks BEFORE the webserver and BLE stack are brought
    // up: task stacks are heap allocations, and doing this last meant they
    // silently failed once BLE had consumed the heap (err=-1) — leaving the
    // node with no RFID polling and no network sync at all.
    // Stack sizes are right-sized from measurement, not guesswork:
    //   TaskRFID: serial parsing + String work, no TLS  -> 6 KB
    //   TaskNetworkSync: runs mbedTLS handshakes in-task -> 16 KB
    // (the previous 16 KB + 32 KB asked for 48 KB of contiguous heap)
    BaseType_t rfidOk = xTaskCreatePinnedToCore(TaskRFID, "TaskRFID", 6144, NULL, 1, NULL, 1);
    if (rfidOk != pdPASS) {
        Serial.printf("FATAL: TaskRFID creation FAILED (err=%d, free heap=%u) - no tags will be read!\n",
                      (int)rfidOk, (unsigned)ESP.getFreeHeap());
    }

    BaseType_t netOk = xTaskCreatePinnedToCore(TaskNetworkSync, "TaskNetworkSync", 16384, NULL, 1, &networkSyncTaskHandle, 0);
    if (netOk != pdPASS) {
        Serial.printf("FATAL: TaskNetworkSync creation FAILED (err=%d, free heap=%u)\n",
                      (int)netOk, (unsigned)ESP.getFreeHeap());
    }

    Serial.printf("[Heap] after task creation: %u\n", (unsigned)ESP.getFreeHeap());

    // Initialize Configuration Webserver
    configWebserver = new ConfigurationWebserver(80);
    configWebserver->begin(NODE_ID, multiReaderManager, distanceCalculator, configManager);

    Serial.printf("[Heap] after webserver: %u\n", (unsigned)ESP.getFreeHeap());

    Serial.printf("Unified Node Setup Completed Successfully! (build %s %s, free heap=%u, min ever=%u)\n",
                  __DATE__, __TIME__, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
}

// Set when a non-blocking BLE scan completes; the main loop picks it up
// and runs uploadLocation() so heavy network I/O does not run inside the
// BLE/Bluedroid callback task.
volatile bool bleScanUploadPending = false;

// Non-blocking BLE scan completion callback. Runs on the BLE task -- keep it
// minimal. Defer any blocking work (HTTP upload) to the main loop.
void bleScanCompleteCB(BLEScanResults results) {
    bleScanInProgress = false;
    bleScanUploadPending = true;
}

void loop() {
    // Service calibration session timeouts (cheap, lock-bounded)
    calibSession.service();
    
    // BT enable SWITCH on pin 23 (level-based, not a push-button toggle).
    // HIGH (or unwired, via pull-up) = BLE enabled; held LOW = BLE disabled.
    // The desired state only takes effect after the level has been stable
    // for a full second: induced noise from the 125kHz reader coils was
    // repeatedly faking button presses on the old edge-triggered logic and
    // cycling the BT radio (which crashed Bluedroid / destabilized WiFi).
    // A noise burst can flip momentary reads, but it cannot hold a pulled-up
    // line at the wrong level continuously for 1000ms.
    {
        const unsigned long BT_SWITCH_STABLE_MS = 1000;
        static bool btLastLevel = true;            // pull-up default: enabled
        static unsigned long btLevelSince = 0;
        bool level = digitalRead(BT_TOGGLE_PIN) == HIGH;
        if (level != btLastLevel) {
            btLastLevel = level;
            btLevelSince = millis();               // level changed: restart stability timer
        } else if (level != bleEnabled && millis() - btLevelSince >= BT_SWITCH_STABLE_MS) {
            bleEnabled = level;
            Serial.printf("[System] BT switch: BLE is now %s (pin 23 stable %s)\n",
                          bleEnabled ? "ENABLED" : "DISABLED", level ? "HIGH" : "LOW");
            if (!bleEnabled) {
                disableBluetooth();
            } else {
                enableBluetooth();
            }
        }
    }

    // Drain any completed scan first: upload detections from main-loop context.
    if (bleScanUploadPending) {
        bleScanUploadPending = false;
        if (bleEnabled) {
            BLEDevice::getScan()->clearResults();
        }

        // Upload unless the webserver is in active (non-calibration) use
        if (bleEnabled && (calibSession.isActive() || millis() - lastWebserverRequestTime > 15000)) {
            uploadLocation();
        } else {
            Serial.println("Webserver active or BT disabled: clearing detections without upload to save bandwidth");
            if (xSemaphoreTake(detectionsMutex, portMAX_DELAY) == pdTRUE) {
                detections.clear();
                xSemaphoreGive(detectionsMutex);
            }
        }
    }

    bool webIdle = millis() - lastWebserverRequestTime > 15000;
    // During calibration sampling, BLE scanning MUST continue even though the
    // operator's browser is actively polling the device.
    bool scanAllowed = bleEnabled && (webIdle || calibSession.isCollecting());

    if (!bleScanInProgress && !isNetworkBusy && scanAllowed) {
        bleScanInProgress = true;
        // Non-blocking scan: returns immediately, bleScanCompleteCB fires when done.
        // This keeps the main loop responsive so incoming HTTP requests can promptly
        // call BLEDevice::getScan()->stop() and free the radio.
        if (!BLEDevice::getScan()->start(5, bleScanCompleteCB, false)) {
            bleScanInProgress = false;
        }
    } else if (!scanAllowed && bleScanInProgress) {
        // Stop scanning if it's no longer allowed
        if (bleEnabled) {
            BLEDevice::getScan()->stop();
        }
        bleScanInProgress = false;
    } else if (!scanAllowed && !bleScanInProgress) {
        // Webserver is active or BT is disabled: give the RF time to WiFi by skipping BLE scans.
        static unsigned long lastPrint = 0;
        if (millis() - lastPrint > 5000) {
            Serial.printf("Web Config Mode Active or BT Disabled (BLE=%d) - paused to prioritize WiFi...\n", bleEnabled);
            lastPrint = millis();
        }
    }
    delay(200);
}
