# Requirements Document

## Introduction

The Halo Watch Patient Monitoring System is an automated IoT-based solution designed to enhance patient safety and monitoring in hospital wards. The system utilizes wearable sensor-equipped wristbands that transmit patient identity, movement data, and vital signs in real-time to strategically placed receiver nodes. These nodes communicate with a centralized backend system that provides healthcare staff with a comprehensive monitoring dashboard, automated alerts, and historical data for auditing and analysis.

## Glossary

- **Wristband_Device**: ESP32-based wearable unit with integrated sensors (MPU6050, MAX30102, DHT11) and RFID tag for patient monitoring
- **Receiver_Node**: ESP32-based stationary unit with RFID reader that detects patient locations within hospital zones
- **Backend_System**: FastAPI server with SQLite database that processes sensor data and manages patient information
- **Dashboard_Interface**: React-based web application providing real-time patient monitoring and alert management
- **Zone_Management**: System for defining and monitoring restricted areas, isolation rooms, and exit points
- **Alert_Engine**: Automated system for detecting and notifying staff of patient safety events
- **Tamper_Detection**: Hardware and software mechanisms to detect wristband removal or damage
- **Vital_Signs_Monitor**: Continuous monitoring of heart rate, blood oxygen, and body temperature
- **Location_Tracking**: Real-time patient positioning using RFID technology and receiver nodes

## Requirements

### Requirement 1

**User Story:** As a healthcare worker, I want to monitor patient locations in real-time, so that I can ensure patient safety and respond quickly to emergencies.

#### Acceptance Criteria

1. WHEN a patient wearing a Wristband_Device enters a zone, THE Receiver_Node SHALL detect the RFID signal within 3 meters range
2. WHEN the Receiver_Node detects a patient, THE Backend_System SHALL update the patient location within 5 seconds
3. THE Dashboard_Interface SHALL display patient locations on a visual floor plan with accuracy within 2 meters
4. WHEN a patient moves between zones, THE Backend_System SHALL log the movement with timestamp and duration
5. THE Location_Tracking SHALL maintain 99% uptime during normal hospital operations

### Requirement 2

**User Story:** As a healthcare worker, I want to receive immediate alerts for patient safety events, so that I can intervene before incidents escalate.

#### Acceptance Criteria

1. WHEN the Wristband_Device detects a fall event, THE Alert_Engine SHALL generate a critical alert within 2 seconds
2. WHEN the Tamper_Detection system detects wristband removal, THE Alert_Engine SHALL trigger an immediate tamper alert
3. IF a patient enters a restricted zone without authorization, THEN THE Alert_Engine SHALL generate a zone violation alert
4. WHEN vital signs exceed normal ranges (HR >100 or <60 BPM, temp >37.5°C, SpO2 <95%), THE Alert_Engine SHALL create an abnormal vitals alert
5. THE Dashboard_Interface SHALL display all active alerts with visual and audio notifications

### Requirement 3

**User Story:** As a healthcare worker, I want to monitor patient vital signs continuously, so that I can detect health deterioration early.

#### Acceptance Criteria

1. THE Vital_Signs_Monitor SHALL measure heart rate with accuracy within ±3 BPM
2. THE Vital_Signs_Monitor SHALL measure blood oxygen saturation with accuracy within ±2%
3. THE Vital_Signs_Monitor SHALL measure body temperature with accuracy within ±0.5°C
4. WHEN skin contact is lost for more than 10 seconds, THE Wristband_Device SHALL trigger a contact alert
5. THE Backend_System SHALL store vital signs data with timestamps for historical analysis

### Requirement 4

**User Story:** As a healthcare administrator, I want to configure zone restrictions and access controls, so that I can maintain security and infection control protocols.

#### Acceptance Criteria

1. THE Zone_Management SHALL support configuration of normal, restricted, isolation, and exit zone types
2. WHEN a patient is assigned to isolation, THE Zone_Management SHALL restrict their movement to authorized areas only
3. IF an unauthorized patient attempts to exit the ward, THEN THE Alert_Engine SHALL generate an exit violation alert
4. THE Backend_System SHALL maintain an audit log of all zone access attempts with patient identification
5. WHERE zone restrictions are configured, THE Alert_Engine SHALL enforce access control rules automatically

### Requirement 5

**User Story:** As a healthcare worker, I want the system to operate reliably with minimal maintenance, so that patient monitoring is not interrupted.

#### Acceptance Criteria

1. THE Wristband_Device SHALL operate for minimum 24 hours on a single battery charge during active monitoring
2. WHEN battery level drops below 20%, THE Wristband_Device SHALL send a low battery alert
3. THE Receiver_Node SHALL maintain continuous operation when connected to mains power
4. IF WiFi connectivity is lost, THE Receiver_Node SHALL store data locally and sync when connection is restored
5. THE Backend_System SHALL provide system health monitoring with uptime tracking and error logging

### Requirement 6

**User Story:** As a healthcare worker, I want to authenticate securely to access patient data, so that patient privacy is protected according to healthcare regulations.

#### Acceptance Criteria

1. THE Dashboard_Interface SHALL require user authentication with username and password
2. WHEN authentication fails three times, THE Backend_System SHALL lock the account for 15 minutes
3. THE Backend_System SHALL use HTTPS encryption for all data transmission
4. WHERE patient data is accessed, THE Backend_System SHALL log the access with user identification and timestamp
5. THE Backend_System SHALL automatically log out inactive sessions after 30 minutes

### Requirement 7

**User Story:** As a system administrator, I want to deploy and configure the system easily, so that it can be installed in different hospital environments.

#### Acceptance Criteria

1. THE Backend_System SHALL support configuration through environment variables for different deployment environments
2. THE Wristband_Device SHALL support over-the-air firmware updates via WiFi connection
3. THE Receiver_Node SHALL auto-discover and register with the Backend_System on first startup
4. THE Dashboard_Interface SHALL provide a setup wizard for initial system configuration
5. THE Backend_System SHALL include database migration scripts for schema updates

### Requirement 8

**User Story:** As a healthcare worker, I want to view historical patient data and generate reports, so that I can analyze patterns and improve care quality.

#### Acceptance Criteria

1. THE Backend_System SHALL retain patient movement history for minimum 90 days
2. THE Dashboard_Interface SHALL provide filtering and search capabilities for historical data
3. WHEN generating reports, THE Backend_System SHALL export data in CSV and PDF formats
4. THE Backend_System SHALL calculate and display patient activity metrics including movement frequency and zone dwell times
5. WHERE data analysis is requested, THE Dashboard_Interface SHALL provide graphical charts and trend visualization