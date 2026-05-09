# Requirements Document

## Introduction

This feature enhances the Halo Watch unified node (ESP32-based) to support simultaneous monitoring of up to 7 RFID door readers with configurable, human-readable door names. Door names are assigned through an ESP32-hosted configuration web interface accessible on the local network, eliminating NAT traversal issues. Configuration data is synced from the ESP32 to the backend server. The React dashboard provides read-only visibility of door configurations. Additionally, the system implements Bluetooth RSSI-based distance calculation to determine the proximity of wristband tags from the unified node, enabling room-level positioning within the physical demonstration model.

## Glossary

- **Unified_Node**: The ESP32 microcontroller that acts as both a multi-door RFID reader hub and a Bluetooth scanner for wristband proximity detection.
- **RFID_Reader**: A hardware module (RDM6300-compatible) connected to the Unified_Node via UART that reads 125 kHz RFID tags on wristbands.
- **Door_Name**: A human-readable label (e.g., "Main Entrance", "Ward A Exit") assigned to an RFID_Reader to identify the physical door it monitors.
- **Config_Webserver**: An HTTP server hosted on the Unified_Node, accessible on the local network, used to assign Door_Names to RFID_Readers.
- **Backend_Server**: The FastAPI Python server that stores configuration, access logs, and positioning data.
- **React_Dashboard**: The frontend web application that displays system status and door configuration in read-only mode.
- **BLE_Wristband**: An ESP32-based wristband worn by patients that advertises a Bluetooth Low Energy beacon with a unique minor identifier.
- **RSSI**: Received Signal Strength Indicator — the measured power level of a received Bluetooth signal, expressed in dBm.
- **Distance_Calculator**: The firmware component on the Unified_Node that converts RSSI measurements into estimated distances using a path-loss model.
- **Calibration_Parameters**: The set of values (reference RSSI at 1 m, path-loss exponent, environmental factor) used by the Distance_Calculator.
- **Location_Update**: A JSON payload sent by the Unified_Node to the Backend_Server containing detected BLE_Wristband identifiers and their estimated distances.
- **Door_Event**: A JSON payload sent by the Unified_Node to the Backend_Server when an RFID tag is scanned at a door.
- **Preferences_Storage**: The ESP32 non-volatile storage (NVS) used to persist Door_Names across power cycles.
- **mDNS**: Multicast DNS — a protocol that allows the Config_Webserver to be discovered on the local network by a human-readable hostname (e.g., `halowatch-unified-a1.local`).

---

## Requirements

### Requirement 1: Multi-Door RFID Monitoring

**User Story:** As a facility operator, I want a single ESP32 unified node to monitor up to 7 doors simultaneously, so that I can reduce hardware costs and simplify installation.

#### Acceptance Criteria

1. THE Unified_Node SHALL support registration of up to 7 RFID_Readers, each identified by a unique hardware identifier and GPIO RX pin.
2. WHEN an RFID tag is presented to any registered RFID_Reader, THE Unified_Node SHALL detect the tag within 500 ms of presentation.
3. WHEN an RFID tag is detected, THE Unified_Node SHALL record the hardware identifier of the RFID_Reader that detected the tag.
4. WHEN the same RFID tag is detected by the same RFID_Reader within a 5-second debounce window, THE Unified_Node SHALL suppress duplicate Door_Events for that tag.
5. WHEN an RFID tag is detected, THE Unified_Node SHALL transmit a Door_Event payload to the Backend_Server containing the node identifier, reader hardware identifier, Door_Name, RFID UID, action type, and timestamp.
6. IF the Backend_Server is unreachable at the time of detection, THEN THE Unified_Node SHALL store the Door_Event in a local buffer of up to 100 events in Preferences_Storage and retransmit buffered events in FIFO order when connectivity is restored, discarding the oldest event when the buffer is full.
7. THE Unified_Node SHALL parse RDM6300-format RFID frames, validating that each frame is exactly 14 bytes long, begins with start byte 0x02, ends with stop byte 0x03, and passes the XOR checksum of the 10 data bytes before accepting a tag read.
8. IF a buffered Door_Event fails retransmission, THEN THE Unified_Node SHALL retry that event up to 3 times with a 10-second interval before discarding it and proceeding to the next buffered event.

---

### Requirement 2: Configurable Door Names via ESP32-Hosted Web Interface

**User Story:** As a facility operator, I want to assign human-readable names to each door reader through a local web interface on the ESP32, so that door events in the dashboard are easy to identify without requiring access to the backend server.

#### Acceptance Criteria

1. THE Config_Webserver SHALL serve an HTML configuration page on port 80 of the Unified_Node's local IP address.
2. WHEN the Unified_Node connects to the local Wi-Fi network, THE Config_Webserver SHALL register an mDNS hostname in the format `halowatch-{node_id}.local` so that the page is reachable without knowing the IP address.
3. THE Config_Webserver SHALL expose a `GET /api/readers` endpoint that returns HTTP 200 with a JSON array of all registered RFID_Readers; each element SHALL include reader identifier, GPIO pin, current Door_Name, and a status field whose value is one of `"active"`, `"disconnected"`, or `"error"`.
4. THE Config_Webserver SHALL expose a `POST /api/readers/update` endpoint that accepts a JSON body containing a reader identifier and a new Door_Name.
5. WHEN a `POST /api/readers/update` request is received with a valid reader identifier and a valid Door_Name, THE Config_Webserver SHALL update the Door_Name in the MultiReaderManager, persist it to Preferences_Storage, and return HTTP 200 with `{"status": "success"}`.
6. IF a `POST /api/readers/update` request is received with a Door_Name that is empty or fails the validation rules in criterion 7, THEN THE Config_Webserver SHALL return HTTP 400 with an error message indicating which validation rule was violated.
7. THE Config_Webserver SHALL validate that a Door_Name is between 1 and 100 characters in length and contains only alphanumeric characters, spaces, hyphens, and underscores.
8. IF a `POST /api/readers/update` request is received with a reader identifier that does not match any registered RFID_Reader, THEN THE Config_Webserver SHALL return HTTP 404 with an error message indicating the reader identifier was not found.
9. WHEN a Door_Name is successfully updated via the Config_Webserver, THE Unified_Node SHALL sync the updated configuration to the Backend_Server within 60 seconds; IF the sync fails, THEN THE Unified_Node SHALL retain the update in Preferences_Storage and reattempt the sync on the next scheduled sync cycle.

---

### Requirement 3: Door Name Persistence Across Power Cycles

**User Story:** As a facility operator, I want door name assignments to survive ESP32 reboots, so that I do not need to reconfigure the system after a power outage.

#### Acceptance Criteria

1. WHEN a Door_Name is assigned or updated, THE Unified_Node SHALL persist the mapping of reader hardware identifier to Door_Name in Preferences_Storage; IF the write to Preferences_Storage fails, THEN THE Unified_Node SHALL retain the updated Door_Name in memory for the current session and log an error indicating persistence failure.
2. WHEN the Unified_Node boots, THE Unified_Node SHALL load all persisted Door_Name mappings from Preferences_Storage before beginning RFID scanning; IF the read from Preferences_Storage fails, THEN THE Unified_Node SHALL apply the default Door_Name `"Door {N}"` for all readers and log an error indicating the load failure.
3. WHEN no persisted Door_Name exists for a reader, THE Unified_Node SHALL use the default name `"Door {N}"` where N is the 1-based index of that reader in registration order (i.e., the first registered reader is N=1, the second is N=2, and so on).
4. WHEN the Unified_Node successfully fetches updated door mappings from the Backend_Server, THE Unified_Node SHALL overwrite only the Preferences_Storage entries corresponding to the fetched reader identifiers, leaving all other persisted entries unchanged.

---

### Requirement 4: Configuration Sync to Backend Server

**User Story:** As a system administrator, I want the ESP32 to push door name configuration to the backend server, so that the React dashboard can display accurate door labels without requiring direct access to the ESP32.

#### Acceptance Criteria

1. WHEN a Door_Name is updated via the Config_Webserver, THE Unified_Node SHALL send a `POST /api/v1/readers/sync` request to the Backend_Server containing the node identifier, reader identifier, Door_Name, and GPIO pin.
2. THE Backend_Server SHALL expose a `POST /api/v1/readers/sync` endpoint that accepts a ReaderSyncRequest payload and upserts the RFID_Reader record in the database.
3. WHEN the Backend_Server receives a valid sync request, THE Backend_Server SHALL return HTTP 200 with the updated reader record.
4. IF the sync request to the Backend_Server fails, THEN THE Unified_Node SHALL retry the sync up to 3 times with a 10-second interval before logging the failure and continuing operation.
5. THE Unified_Node SHALL periodically fetch the current door mappings from the Backend_Server at an interval of no more than 5 minutes to reconcile any configuration drift.

---

### Requirement 5: React Dashboard Read-Only Door Configuration View

**User Story:** As a dashboard user, I want to see the current door names and reader statuses in the React dashboard, so that I can verify the system configuration without accessing the ESP32 directly.

#### Acceptance Criteria

1. THE React_Dashboard SHALL display a read-only list of all registered RFID_Readers for a selected node, showing reader identifier, Door_Name, GPIO pin, status, and last-seen timestamp formatted as ISO 8601 (e.g., `2024-01-15T10:30:00Z`).
2. WHEN a node is selected, THE React_Dashboard SHALL fetch door configuration data from the Backend_Server via `GET /api/v1/readers/{node_id}` where `{node_id}` is the identifier of the currently selected node.
3. WHILE a node is selected, THE React_Dashboard SHALL re-fetch door configuration data from the Backend_Server via `GET /api/v1/readers/{node_id}` at an interval of 30 seconds and update the display with the latest response.
4. THE React_Dashboard SHALL display the reader status using a visual indicator: green for `"active"`, red for `"disconnected"`, and yellow for `"error"`.
5. THE React_Dashboard SHALL NOT provide any controls to modify Door_Names or reader configuration; all configuration is performed through the Config_Webserver.
6. WHEN the Backend_Server returns HTTP 200 with an empty readers array for the selected node, THE React_Dashboard SHALL display a message indicating that no readers are configured for the selected node.
7. IF the `GET /api/v1/readers/{node_id}` request fails or returns a non-200 HTTP status, THEN THE React_Dashboard SHALL display an error message indicating that door configuration data could not be loaded, and SHALL retain the previously displayed data if any was loaded.

---

### Requirement 6: Bluetooth RSSI-Based Distance Calculation

**User Story:** As a system operator, I want the unified node to calculate the estimated distance to each detected BLE wristband using Bluetooth RSSI, so that the system can determine room-level proximity of patients.

#### Acceptance Criteria

1. WHEN the Unified_Node detects a BLE advertisement from a BLE_Wristband matching the configured beacon UUID, THE Distance_Calculator SHALL compute an estimated distance using the path-loss formula: `distance = 10 ^ ((referenceRSSI - RSSI) / (10 × pathLossExponent)) × environmentalFactor`.
2. THE Distance_Calculator SHALL clamp computed distances to the range [0.1 m, 50.0 m] before reporting.
3. WHEN an RSSI value outside the range [-100 dBm, -30 dBm] is received, THE Distance_Calculator SHALL clamp the RSSI to the nearest boundary value before applying the path-loss formula.
4. THE Unified_Node SHALL include the estimated distance for each detected BLE_Wristband in the Location_Update payload sent to the Backend_Server.
5. WHEN no calibration data has been loaded from the Backend_Server, THE Unified_Node SHALL use default Calibration_Parameters (referenceRSSI = -59 dBm, pathLossExponent = 2.0, environmentalFactor = 1.0); the state "no calibration data loaded" is defined as the condition where no successful `GET /api/v1/calibration/{node_id}` response has been applied since the last boot.
6. FOR ALL valid RSSI inputs in the range [-100 dBm, -30 dBm], the Distance_Calculator SHALL produce a distance value that is monotonically decreasing as RSSI increases (i.e., stronger signal → shorter distance).
7. THE Distance_Calculator SHALL treat a Calibration_Parameters set as valid only when `pathLossExponent` is in the range [1.0, 6.0] and `environmentalFactor` is in the range [0.1, 10.0]; IF loaded parameters fall outside these ranges, THEN THE Unified_Node SHALL discard them and continue using the current active Calibration_Parameters.
8. WHILE a distance value has not been updated for more than 30 seconds for a given BLE_Wristband, THE Unified_Node SHALL treat that wristband's distance as stale and SHALL NOT include it in the Location_Update payload until a fresh RSSI measurement is received.

---

### Requirement 7: Calibration Parameter Management

**User Story:** As a system administrator, I want to store and retrieve per-node Bluetooth calibration parameters on the backend, so that distance calculations can be tuned for the physical environment of each node.

#### Acceptance Criteria

1. THE Backend_Server SHALL expose a `GET /api/v1/calibration/{node_id}` endpoint that returns the Calibration_Parameters for the specified node.
2. THE Backend_Server SHALL expose a `PUT /api/v1/calibration/{node_id}` endpoint that accepts and stores updated Calibration_Parameters for the specified node.
3. WHEN the Unified_Node boots and Wi-Fi is available, THE Unified_Node SHALL fetch Calibration_Parameters from the Backend_Server and apply them to the Distance_Calculator.
4. IF the Backend_Server returns HTTP 404 for a calibration request, THEN THE Unified_Node SHALL continue using the default Calibration_Parameters without raising an error.
5. THE Backend_Server SHALL validate that `path_loss_exponent` is in the range [1.0, 6.0], `reference_rssi` is in the range [-100.0, -20.0], and `environmental_factor` is in the range [0.1, 10.0] before storing Calibration_Parameters; IF any value is outside its valid range, THEN THE Backend_Server SHALL return HTTP 422 with an error message identifying the invalid field and its valid range.
6. WHILE a node is selected in the React_Dashboard, THE React_Dashboard SHALL re-fetch Calibration_Parameters from the Backend_Server via `GET /api/v1/calibration/{node_id}` at an interval of 60 seconds and update the displayed values with the latest response.

---

### Requirement 8: Location Update Reporting with Distance Data

**User Story:** As a system operator, I want the unified node to report detected wristband positions with distance estimates to the backend, so that the dashboard can display room-level patient locations.

#### Acceptance Criteria

1. WHILE at least one BLE_Wristband is detected, THE Unified_Node SHALL transmit a Location_Update payload to the Backend_Server at an interval of no more than 5 seconds.
2. THE Location_Update payload SHALL include the node identifier, zone, room, timestamp, receiver status (uptime, Wi-Fi strength, free memory), and a list of detected tags each containing BLE minor identifier, RSSI, and estimated distance; the estimated distance field SHALL be omitted for any tag whose distance value is stale as defined in Requirement 6 criterion 8.
3. WHEN no BLE_Wristbands are detected during a scan cycle, THE Unified_Node SHALL transmit a Location_Update with an empty detected tags list to indicate the node is active.
4. THE Backend_Server SHALL store the estimated distance from each Location_Update in the `estimated_distance` field of the LocationHistory record; IF the estimated distance field is absent in the payload, THEN THE Backend_Server SHALL store NULL in the `estimated_distance` field.
5. WHEN the Backend_Server receives a Location_Update containing a detected tag with a known BLE minor identifier, THE Backend_Server SHALL update the corresponding patient's location and last-activity timestamp.
6. IF the Backend_Server receives a Location_Update containing a detected tag with a BLE minor identifier that does not match any known patient record, THEN THE Backend_Server SHALL store the Location_Update record as-is and SHALL NOT update any patient location.
7. IF the transmission of a Location_Update to the Backend_Server fails, THEN THE Unified_Node SHALL discard that Location_Update and transmit a fresh Location_Update on the next scheduled interval.

---

### Requirement 9: Config Webserver Calibration Interface

**User Story:** As a technician, I want a calibration wizard accessible from the ESP32 web interface, so that I can collect RSSI samples at known distances and submit them to improve positioning accuracy.

#### Acceptance Criteria

1. THE Config_Webserver SHALL serve a calibration wizard page at `GET /calibrate` that guides the technician through collecting RSSI samples at known distances, returning HTTP 200 with the HTML page content.
2. THE Config_Webserver SHALL expose a `POST /api/calibration/sample` endpoint that accepts a JSON body containing a measured distance (in metres) and an observed RSSI value (in dBm).
3. WHEN a valid calibration sample is received via `POST /api/calibration/sample`, THE Config_Webserver SHALL append the sample to the current session's collection and return HTTP 200 with the total number of samples collected in the current session.
4. IF a `POST /api/calibration/sample` request is received with a distance that is not in the range (0.0 m, 100.0 m] or an RSSI that is not in the range [-100 dBm, -30 dBm], THEN THE Config_Webserver SHALL return HTTP 400 with an error message identifying which field is out of range.
5. THE Config_Webserver SHALL expose a `POST /api/calibration/start` endpoint that initialises a new calibration session, clearing any previously collected samples, and SHALL return HTTP 200 to confirm the session was started.
6. WHEN a `POST /api/calibration/apply` request is received and at least 3 calibration samples have been collected in the current session, THE Config_Webserver SHALL compute updated Calibration_Parameters from the collected samples and push them to the Backend_Server, returning HTTP 200 with the computed Calibration_Parameters on success.
7. IF a `POST /api/calibration/apply` request is received and fewer than 3 calibration samples have been collected in the current session, THEN THE Config_Webserver SHALL return HTTP 409 with an error message indicating that at least 3 samples are required.
8. IF the push of computed Calibration_Parameters to the Backend_Server fails during `POST /api/calibration/apply`, THEN THE Config_Webserver SHALL return HTTP 502 with an error message indicating that the Backend_Server could not be reached, and SHALL retain the computed parameters in memory for a subsequent retry.
