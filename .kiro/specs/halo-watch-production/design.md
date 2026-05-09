# Design Document

## Overview

The Halo Watch Patient Monitoring System is a comprehensive IoT solution consisting of four main components: wearable sensor devices, receiver nodes, a backend processing system, and a real-time monitoring dashboard. The system is designed to provide continuous patient monitoring, location tracking, and automated alert generation in hospital environments.

## Architecture

### System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    WEARABLE WRISTBAND                       │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ ESP32 + RFID Tag + MPU6050 + MAX30102 + DHT11       │  │
│  │ + Conductive Strap + Skin Contact Sensor            │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────┬───────────────────────────────────────┘
                      │ RFID (125kHz) + WiFi (OTA updates)
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                    RECEIVER NODES                           │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ ESP32 + Long-Range RFID Reader + LED + Buzzer       │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────┬───────────────────────────────────────┘
                      │ WiFi (HTTPS/REST API)
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                    BACKEND SERVER                           │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ FastAPI + SQLite + WebSocket + JWT Auth             │  │
│  │ Alert Engine + Zone Management + Data Processing    │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────┬───────────────────────────────────────┘
                      │ HTTPS + WebSocket
                      ▼
┌─────────────────────────────────────────────────────────────┐
│              MONITORING DASHBOARD                           │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ React + Real-time Floor Plan + Alert Management     │  │
│  │ Authentication + Historical Data + Reports           │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Communication Flow

1. **Wristband → Receiver**: Continuous RFID broadcast (125kHz, 3-5m range)
2. **Wristband → Backend**: Direct WiFi transmission for vital signs and alerts (HTTPS)
3. **Receiver → Backend**: Location updates and zone detections (HTTPS REST API)
4. **Backend → Dashboard**: Real-time updates via WebSocket, historical data via REST API

## Components and Interfaces

### 1. Wristband Device (ESP32-based)

#### Hardware Components
- **Main Controller**: ESP32-WROOM-32 (dual-core, WiFi, Bluetooth)
- **Motion Sensor**: MPU6050 (6-axis IMU for fall detection and activity monitoring)
- **Vital Signs Sensor**: MAX30102 (heart rate and SpO2 monitoring)
- **Temperature Sensor**: DHT11 (body temperature and environmental monitoring)
- **RFID Tag**: 125kHz active tag (3-5m range, unique patient identification)
- **Tamper Detection**: Conductive thread in strap + capacitive skin contact sensor
- **Power System**: 3.7V Li-Po battery (500-1000mAh) with TP4056 charging module
- **Indicators**: Dual-color LED and optional buzzer for status feedback

#### Software Architecture
```cpp
// Operating Modes
enum OperatingMode {
    RFID_ONLY,      // Deep sleep, RFID only (3-5 days battery)
    ACTIVE_MONITOR, // Full sensors, WiFi enabled (8-12 hours)
    HYBRID,         // Periodic sensor checks (1-2 days)
    CHARGING        // Firmware update capable
};

// Main firmware structure
class WristbandController {
    SensorManager sensors;
    AlertEngine alerts;
    CommunicationManager comm;
    PowerManager power;
    
    void setup();
    void loop();
    void handleSensorData();
    void processAlerts();
    void transmitData();
    void managePower();
};
```

#### Key Interfaces
- **I2C Bus**: MPU6050 (0x68), MAX30102 (0x57) on GPIO21/22
- **Digital I/O**: DHT11 (GPIO4), LED (GPIO2), Buzzer (GPIO5)
- **Analog/Touch**: Strap sensor (GPIO15), Skin contact (GPIO13)
- **WiFi**: HTTPS client for backend communication
- **Power**: Battery monitoring and charging status

### 2. Receiver Node (ESP32-based)

#### Hardware Components
- **Main Controller**: ESP32 DevKit V1 (always-on operation)
- **RFID Reader**: Long-range 125kHz reader (3-5m detection range)
- **Visual Indicator**: 10mm RGB LED with status color coding
- **Audio Indicator**: Active buzzer for alert notifications
- **Power Supply**: 5V DC adapter with optional UPS backup
- **Enclosure**: Wall-mounted ABS plastic box (IP40 rating)

#### Software Architecture
```cpp
class ReceiverController {
    RFIDReader rfidReader;
    LocationManager location;
    CommunicationManager comm;
    StatusIndicator indicator;
    
    void setup();
    void scanForTags();
    void processDetections();
    void transmitLocationData();
    void updateStatus();
};

// Zone configuration
struct ZoneConfig {
    String zoneName;
    String zoneType; // normal, restricted, exit, isolation
    bool requireAuthorization;
    String[] authorizedPatients;
    AlertLevel alertLevel;
};
```

#### Key Interfaces
- **UART**: RFID reader communication (GPIO16/17)
- **PWM**: RGB LED control (GPIO25/26/27)
- **Digital I/O**: Buzzer control (GPIO14)
- **WiFi**: HTTPS client for backend API calls
- **Configuration**: Zone settings and patient authorization lists

### 3. Backend System (FastAPI + SQLite)

#### Core Services

**Location Service**
```python
class LocationService:
    def process_rfid_detection(self, receiver_data: LocationUpdate)
    def update_patient_location(self, patient_id: str, location: Location)
    def check_zone_violations(self, patient_id: str, zone: Zone)
    def calculate_movement_patterns(self, patient_id: str, timeframe: TimeRange)
```

**Alert Service**
```python
class AlertService:
    def process_wristband_alerts(self, wristband_data: WristbandData)
    def evaluate_zone_violations(self, location_data: LocationUpdate)
    def check_vital_signs_thresholds(self, vitals: VitalSigns)
    def generate_alert(self, alert_type: AlertType, patient_id: str, details: dict)
    def broadcast_alert(self, alert: Alert)
```

**Zone Management Service**
```python
class ZoneManagementService:
    def configure_zone(self, zone_config: ZoneConfig)
    def assign_patient_restrictions(self, patient_id: str, restrictions: List[ZoneRestriction])
    def validate_zone_access(self, patient_id: str, zone_id: str)
    def update_isolation_protocols(self, patient_id: str, isolation_level: IsolationLevel)
```

**Authentication Service**
```python
class AuthenticationService:
    def authenticate_user(self, credentials: UserCredentials)
    def generate_jwt_token(self, user: User)
    def validate_token(self, token: str)
    def log_access_attempt(self, user_id: str, success: bool)
```

#### Database Schema Extensions

```sql
-- Enhanced patient table
ALTER TABLE patients ADD COLUMN isolation_level VARCHAR(20) DEFAULT 'none';
ALTER TABLE patients ADD COLUMN authorized_zones TEXT; -- JSON array
ALTER TABLE patients ADD COLUMN emergency_contact VARCHAR(100);
ALTER TABLE patients ADD COLUMN medical_notes TEXT;

-- Zone configuration table
CREATE TABLE zones (
    id INTEGER PRIMARY KEY,
    zone_code VARCHAR(50) UNIQUE NOT NULL,
    zone_name VARCHAR(100) NOT NULL,
    zone_type VARCHAR(20) NOT NULL, -- normal, restricted, exit, isolation
    require_authorization BOOLEAN DEFAULT FALSE,
    max_occupancy INTEGER DEFAULT NULL,
    alert_level VARCHAR(20) DEFAULT 'info',
    coordinates_json TEXT -- JSON object with boundary coordinates
);

-- User authentication table
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    role VARCHAR(20) NOT NULL, -- admin, nurse, doctor, viewer
    full_name VARCHAR(100),
    email VARCHAR(100),
    last_login DATETIME,
    failed_login_attempts INTEGER DEFAULT 0,
    account_locked_until DATETIME NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- System configuration table
CREATE TABLE system_config (
    id INTEGER PRIMARY KEY,
    config_key VARCHAR(50) UNIQUE NOT NULL,
    config_value TEXT,
    description TEXT,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

#### API Endpoints Enhancement

```python
# Authentication endpoints
@app.post("/api/v1/auth/login")
@app.post("/api/v1/auth/logout")
@app.post("/api/v1/auth/refresh")

# Zone management endpoints
@app.get("/api/v1/zones")
@app.post("/api/v1/zones")
@app.put("/api/v1/zones/{zone_id}")
@app.delete("/api/v1/zones/{zone_id}")

# Patient management endpoints
@app.put("/api/v1/patients/{patient_id}/restrictions")
@app.get("/api/v1/patients/{patient_id}/history")
@app.get("/api/v1/patients/{patient_id}/vitals/trends")

# Reporting endpoints
@app.get("/api/v1/reports/movement-summary")
@app.get("/api/v1/reports/alert-statistics")
@app.get("/api/v1/reports/zone-occupancy")

# System administration endpoints
@app.get("/api/v1/system/health")
@app.get("/api/v1/system/logs")
@app.post("/api/v1/system/backup")
```

### 4. Dashboard Interface (React-based)

#### Component Architecture

```typescript
// Main application structure
interface DashboardApp {
  AuthenticationProvider: React.FC;
  NavigationLayout: React.FC;
  RealTimeDataProvider: React.FC;
  AlertProvider: React.FC;
}

// Core dashboard components
interface CoreComponents {
  FloorPlanView: React.FC<{
    patients: Patient[];
    zones: Zone[];
    receivers: Receiver[];
    onPatientClick: (patient: Patient) => void;
  }>;
  
  PatientListView: React.FC<{
    patients: Patient[];
    filters: PatientFilters;
    onFilterChange: (filters: PatientFilters) => void;
  }>;
  
  AlertManagementPanel: React.FC<{
    alerts: Alert[];
    onAlertAcknowledge: (alertId: string) => void;
    onAlertResolve: (alertId: string) => void;
  }>;
  
  VitalSignsMonitor: React.FC<{
    patientId: string;
    realTimeData: VitalSigns;
    historicalData: VitalSigns[];
  }>;
  
  ZoneManagementInterface: React.FC<{
    zones: Zone[];
    onZoneUpdate: (zone: Zone) => void;
    onPatientRestrictionUpdate: (patientId: string, restrictions: ZoneRestriction[]) => void;
  }>;
}
```

#### Real-time Features

```typescript
// WebSocket integration for live updates
class RealTimeManager {
  private ws: WebSocket;
  private eventHandlers: Map<string, Function[]>;
  
  connect(url: string): void;
  subscribe(event: string, handler: Function): void;
  handleLocationUpdate(data: LocationUpdateEvent): void;
  handleAlertCreated(data: AlertCreatedEvent): void;
  handleVitalSignsUpdate(data: VitalSignsEvent): void;
}

// State management for real-time data
interface DashboardState {
  patients: Patient[];
  alerts: Alert[];
  zones: Zone[];
  receivers: Receiver[];
  currentUser: User;
  systemStatus: SystemStatus;
}
```

#### User Interface Features

1. **Interactive Floor Plan**
   - SVG-based hospital ward layout
   - Real-time patient position markers
   - Zone boundary visualization
   - Click-to-view patient details
   - Color-coded status indicators

2. **Alert Management Dashboard**
   - Priority-based alert sorting
   - Visual and audio notifications
   - One-click acknowledgment
   - Alert history and trends
   - Escalation procedures

3. **Patient Monitoring Panel**
   - Real-time vital signs display
   - Historical trend charts
   - Movement pattern analysis
   - Zone violation history
   - Emergency contact information

4. **System Administration**
   - User account management
   - Zone configuration interface
   - System health monitoring
   - Data export and reporting
   - Firmware update management

## Data Models

### Enhanced Data Structures

```python
# Patient data model with additional fields
class PatientExtended(BaseModel):
    patient_id: str
    name: str
    age: Optional[int]
    medical_record_number: str
    admission_date: datetime
    discharge_date: Optional[datetime]
    isolation_level: IsolationLevel
    authorized_zones: List[str]
    emergency_contact: Optional[ContactInfo]
    medical_notes: Optional[str]
    current_location: Optional[Location]
    vital_signs: Optional[VitalSigns]
    device_status: DeviceStatus
    alert_preferences: AlertPreferences

# Zone configuration model
class ZoneConfig(BaseModel):
    zone_id: str
    zone_name: str
    zone_type: ZoneType
    coordinates: List[Coordinate]
    max_occupancy: Optional[int]
    require_authorization: bool
    authorized_roles: List[UserRole]
    alert_settings: AlertSettings
    isolation_compatible: bool

# Alert model with enhanced metadata
class AlertExtended(BaseModel):
    alert_id: str
    alert_type: AlertType
    severity: AlertSeverity
    patient_id: str
    zone_id: Optional[str]
    receiver_id: Optional[str]
    title: str
    description: str
    created_at: datetime
    acknowledged_at: Optional[datetime]
    acknowledged_by: Optional[str]
    resolved_at: Optional[datetime]
    escalation_level: int
    related_alerts: List[str]
    metadata: Dict[str, Any]
```

## Error Handling

### Fault Tolerance Strategy

1. **Device Level**
   - Automatic WiFi reconnection with exponential backoff
   - Local data buffering during connectivity loss
   - Battery level monitoring with graceful degradation
   - Sensor failure detection and fallback modes

2. **Network Level**
   - HTTPS with certificate validation
   - Request retry logic with circuit breaker pattern
   - Data compression for bandwidth optimization
   - Offline mode with local storage synchronization

3. **Backend Level**
   - Database connection pooling and failover
   - API rate limiting and DDoS protection
   - Input validation and sanitization
   - Comprehensive error logging and monitoring

4. **Frontend Level**
   - WebSocket reconnection handling
   - Optimistic UI updates with rollback
   - Error boundary components
   - Graceful degradation for offline scenarios

### Error Recovery Procedures

```python
# Example error handling patterns
class ErrorHandler:
    def handle_device_offline(self, device_id: str):
        # Mark device as offline, trigger maintenance alert
        pass
    
    def handle_sensor_failure(self, device_id: str, sensor_type: str):
        # Switch to backup sensors, notify maintenance
        pass
    
    def handle_zone_violation_false_positive(self, alert_id: str):
        # Machine learning-based false positive detection
        pass
    
    def handle_database_connection_loss(self):
        # Failover to backup database, queue operations
        pass
```

## Testing Strategy

### Unit Testing
- **Firmware**: Sensor reading accuracy, alert logic, power management
- **Backend**: API endpoints, data validation, alert generation logic
- **Frontend**: Component rendering, user interactions, data flow

### Integration Testing
- **Device-to-Backend**: Data transmission, authentication, error handling
- **Backend-to-Frontend**: Real-time updates, API responses, WebSocket communication
- **End-to-End**: Complete patient monitoring workflows, alert escalation

### Performance Testing
- **Load Testing**: Multiple concurrent devices, high-frequency data transmission
- **Stress Testing**: Network failures, database overload, memory constraints
- **Battery Life Testing**: Extended operation under various usage patterns

### Security Testing
- **Authentication**: Login security, session management, access control
- **Data Transmission**: HTTPS encryption, certificate validation
- **Input Validation**: SQL injection, XSS prevention, data sanitization

### Hardware Testing
- **Environmental**: Temperature, humidity, electromagnetic interference
- **Durability**: Drop tests, water resistance, strap integrity
- **Range Testing**: RFID detection distance, WiFi connectivity range

## Security Considerations

### Data Protection
- **Encryption**: AES-256 for data at rest, TLS 1.3 for data in transit
- **Access Control**: Role-based permissions, multi-factor authentication
- **Audit Logging**: Comprehensive access logs, tamper detection
- **Data Anonymization**: Patient data pseudonymization for analytics

### Network Security
- **VPN Integration**: Support for hospital VPN infrastructure
- **Certificate Management**: Automated certificate renewal, pinning
- **Firewall Configuration**: Minimal port exposure, intrusion detection
- **Device Authentication**: Unique device certificates, secure provisioning

### Compliance
- **HIPAA Compliance**: Patient data protection, access controls, audit trails
- **Medical Device Regulations**: FDA guidelines for patient monitoring devices
- **Data Retention**: Configurable retention policies, secure data deletion
- **Privacy Controls**: Patient consent management, data portability

This design provides a comprehensive foundation for implementing a production-ready patient monitoring system that meets all the requirements while ensuring scalability, reliability, and security.