from __future__ import annotations

from datetime import datetime
from typing import Any, Dict, List, Literal, Optional

from pydantic import BaseModel, Field


# ---------- Dashboard-facing DTOs ----------


class RoomPosition(BaseModel):
    x: float
    y: float


class RoomSummary(BaseModel):
    id: str
    name: str
    status: Literal["normal", "occupied", "anomaly", "warning"]
    position: RoomPosition
    # High-level zone type used by the dashboard to style markers.
    kind: Literal[
        "patient_room",
        "isolation",
        "restricted",
        "nurses_station",
        "control_room",
        "waiting_room",
        "corridor",
        "entry",
        "washroom",
    ] = "patient_room"
    patientId: Optional[str] = None


# Vitals removed in v2.0


class PatientSummary(BaseModel):
    id: str
    name: str
    age: Optional[int] = None
    room: Optional[str] = None
    status: Literal["normal", "occupied", "anomaly", "warning"]
    location: Optional[str] = None
    lastActivity: Optional[str] = None
    movementSteps: Optional[int] = None
    strap_intact: Optional[bool] = True


class PatientDetails(PatientSummary):
    medicalRecordNumber: Optional[str] = None
    admissionDate: Optional[datetime] = None
    emergencyContact: Optional[Dict[str, str]] = None
    medicalNotes: Optional[str] = None
    allergies: List[str] = Field(default_factory=list)
    medications: List[str] = Field(default_factory=list)
    movementPattern: List[Dict[str, Any]] = Field(default_factory=list)


class NotificationOut(BaseModel):
    id: str
    type: Literal["alert", "warning", "info", "success", "critical"]
    title: str
    message: str
    timestamp: datetime
    patientId: Optional[str] = None
    room: Optional[str] = None
    isRead: bool = False
    status: str = "active"
    escalation_level: int = 1
    acknowledged_at: Optional[datetime] = None
    acknowledged_by: Optional[str] = None
    resolved_at: Optional[datetime] = None
    resolved_by: Optional[str] = None


class AccessLogOut(BaseModel):
    id: str
    patientId: str
    patientName: str
    room: str
    action: Literal["entry", "exit", "denied"]
    timestamp: datetime
    rfidId: str
    duration: Optional[str] = None


class OverviewStats(BaseModel):
    activePatients: int
    availableRooms: int
    activeAlerts: int
    systemUptimeSeconds: int


# ---------- Ingestion DTOs (wristband / receiver) ----------


class MPU6050Data(BaseModel):
    accel_x: float
    accel_y: float
    accel_z: float
    gyro_x: float
    gyro_y: float
    gyro_z: float
    fall_detected: bool = False


# Vitals sensors removed in v2.0


class TamperData(BaseModel):
    strap_intact: bool
    skin_contact: bool
    tamper_alert: bool = False


class WristbandSensors(BaseModel):
    mpu6050: MPU6050Data
    tamper: TamperData


class WristbandAlerts(BaseModel):
    fall: bool = False
    tamper: bool = False
    low_battery: bool = False


class WristbandData(BaseModel):
    device_id: str
    patient_id: str
    timestamp: Optional[datetime] = None
    battery_level: int
    sensors: WristbandSensors
    alerts: WristbandAlerts


class Coordinates(BaseModel):
    x: float
    y: float
    z: float = 0.0


class ReceiverLocation(BaseModel):
    zone: str
    room: str
    coordinates: Coordinates


class ReceiverStatus(BaseModel):
    uptime: int
    wifi_strength: int
    free_memory: int


class DetectedTag(BaseModel):
    ble_minor: int
    signal_strength: int
    estimated_distance: Optional[float] = None


class LocationUpdate(BaseModel):
    receiver_id: str
    location: ReceiverLocation
    timestamp: datetime
    detected_tags: List[DetectedTag] = Field(default_factory=list)
    receiver_status: Optional[ReceiverStatus] = None


# ---------- Authentication DTOs ----------


class UserLogin(BaseModel):
    username: str
    password: str


class UserCreate(BaseModel):
    username: str
    password: str
    role: Literal["admin", "nurse", "doctor", "viewer"]
    full_name: Optional[str] = None
    email: Optional[str] = None


class UserUpdate(BaseModel):
    full_name: Optional[str] = None
    email: Optional[str] = None
    role: Optional[Literal["admin", "nurse", "doctor", "viewer"]] = None
    is_active: Optional[bool] = None


class UserOut(BaseModel):
    id: int
    username: str
    role: str
    full_name: Optional[str] = None
    email: Optional[str] = None
    last_login: Optional[datetime] = None
    created_at: datetime
    is_active: bool

    class Config:
        from_attributes = True


class TokenResponse(BaseModel):
    access_token: str
    refresh_token: str
    token_type: str = "bearer"
    expires_in: int
    user: UserOut


class RefreshTokenRequest(BaseModel):
    refresh_token: str


# ---------- Zone Management DTOs ----------


class ZoneCoordinates(BaseModel):
    boundaries: List[Dict[str, float]]  # Array of {x, y} coordinate points
    center: Optional[Dict[str, float]] = None  # {x, y} center point


class ZoneCreate(BaseModel):
    zone_code: str
    zone_name: str
    zone_type: Literal["normal", "restricted", "exit", "isolation"]
    require_authorization: bool = False
    max_occupancy: Optional[int] = None
    alert_level: Literal["info", "warning", "alert", "critical"] = "info"
    coordinates: Optional[ZoneCoordinates] = None


class ZoneUpdate(BaseModel):
    zone_name: Optional[str] = None
    zone_type: Optional[Literal["normal", "restricted", "exit", "isolation"]] = None
    require_authorization: Optional[bool] = None
    max_occupancy: Optional[int] = None
    alert_level: Optional[Literal["info", "warning", "alert", "critical"]] = None
    coordinates: Optional[ZoneCoordinates] = None
    is_active: Optional[bool] = None


class ZoneOut(BaseModel):
    id: int
    zone_code: str
    zone_name: str
    zone_type: str
    require_authorization: bool
    max_occupancy: Optional[int]
    alert_level: str
    coordinates: Optional[ZoneCoordinates] = None
    created_at: datetime
    is_active: bool

    class Config:
        from_attributes = True

    @classmethod
    def from_orm(cls, obj):
        coordinates = None
        if obj.coordinates_json:
            try:
                import json
                coordinates = ZoneCoordinates(**json.loads(obj.coordinates_json))
            except (json.JSONDecodeError, TypeError, ValueError):
                pass
        
        return cls(
            id=obj.id,
            zone_code=obj.zone_code,
            zone_name=obj.zone_name,
            zone_type=obj.zone_type,
            require_authorization=obj.require_authorization,
            max_occupancy=obj.max_occupancy,
            alert_level=obj.alert_level,
            coordinates=coordinates,
            created_at=obj.created_at,
            is_active=obj.is_active
        )


class PatientZoneRestrictions(BaseModel):
    isolation_level: Literal["none", "standard", "strict"] = "none"
    authorized_zones: Optional[List[str]] = None
    emergency_contact: Optional[str] = None
    medical_notes: Optional[str] = None


class ZoneOccupancyOut(BaseModel):
    zone_code: str
    zone_name: str
    current_occupancy: int
    max_occupancy: Optional[int]
    occupancy_exceeded: bool
    occupancy_percentage: Optional[float]


class ZoneAccessCheck(BaseModel):
    authorized: bool
    reason: str


# ---------- Zone Monitoring DTOs (dashboard) ----------


class ZoneDashboardOut(BaseModel):
    """
    Dashboard-facing representation of a zone used by the React
    ZoneManagement page.
    """

    id: str  # zone_code
    name: str
    type: Literal["normal", "restricted", "exit", "isolation"]
    description: Optional[str] = None
    maxOccupancy: Optional[int] = None
    requireAuthorization: bool
    authorizedPatients: List[str] = Field(default_factory=list)
    authorizedRoles: List[str] = Field(default_factory=list)
    alertLevel: Literal["info", "warning", "critical"]
    isActive: bool
    coordinates: Optional[ZoneCoordinates] = None
    createdAt: datetime
    updatedAt: datetime


class ZoneOccupancyPatient(BaseModel):
    id: str
    name: str
    entryTime: datetime
    authorized: bool


class ZoneOccupancyDashboard(BaseModel):
    zoneId: str
    zoneName: str
    currentOccupancy: int
    maxOccupancy: Optional[int] = None
    patients: List[ZoneOccupancyPatient] = Field(default_factory=list)


class ZoneViolationRecord(BaseModel):
    id: str
    zoneId: str
    zoneName: str
    patientId: str
    patientName: str
    violationType: str
    timestamp: datetime
    resolved: bool
    resolvedAt: Optional[datetime] = None
    resolvedBy: Optional[str] = None


# ---------- Alert Management DTOs ----------


class AlertThresholdConfig(BaseModel):
    battery_low_threshold: int = 20


class AlertResolveRequest(BaseModel):
    resolution_notes: Optional[str] = None


class AlertStatistics(BaseModel):
    total_alerts: int
    active_alerts: int
    acknowledged_alerts: int
    resolved_alerts: int
    alert_types: Dict[str, int]
    time_period_hours: int



# ---------- Patient Assignment DTOs ----------


class PatientAssignment(BaseModel):
    patient_id: str
    name: str
    ward: Optional[str] = None
    ble_minor: int
    rfid_uid: str


class PatientAssignmentHistoryOut(BaseModel):
    id: int
    patient_id: str
    name: str
    ward: Optional[str] = None
    ble_minor: Optional[int] = None
    rfid_uid: Optional[str] = None
    assigned_at: datetime
    unassigned_at: datetime

    class Config:
        from_attributes = True


# ---------- Door Configuration DTOs ----------


class ReaderRegisterRequest(BaseModel):
    node_id: str
    reader_id: str
    gpio_pin: int


class DoorNameAssignRequest(BaseModel):
    door_name: str


class RFIDReaderOut(BaseModel):
    reader_id: str
    gpio_pin: int
    door_name: Optional[str] = None
    status: str
    last_seen: datetime
    created_at: datetime


class DoorMappingsOut(BaseModel):
    node_id: str
    mappings: Dict[str, str]  # reader_id -> door_name


class DoorEventPayload(BaseModel):
    node_id: str
    reader_id: str
    door_name: Optional[str] = None
    rfid_uid: str
    action: Literal["entry", "exit"]
    timestamp: Optional[datetime] = None


class ReaderSyncRequest(BaseModel):
    """Payload sent by ESP32 to sync a reader's configuration to the backend."""
    node_id: str
    reader_id: str
    door_name: str
    gpio_pin: Optional[int] = None


class CalibrationSyncRequest(BaseModel):
    node_id: str
    reference_rssi: float
    path_loss_exponent: float
    environmental_factor: float


class CalibrationOut(BaseModel):
    node_id: str
    reference_rssi: float
    path_loss_exponent: float
    environmental_factor: float
    calibration_date: datetime

    class Config:
        from_attributes = True


class AuditDoorEventOut(BaseModel):
    id: int
    node_id: str
    reader_id: str
    door_name: Optional[str] = None
    rfid_uid: str
    patient_id: Optional[str] = None
    patient_name: Optional[str] = None
    action: str
    timestamp: datetime

    class Config:
        from_attributes = True


class AuditAdmissionOut(BaseModel):
    id: Optional[int] = None
    patient_id: str
    name: str
    ward: Optional[str] = None
    ble_minor: Optional[int] = None
    rfid_uid: Optional[str] = None
    assigned_at: datetime
    unassigned_at: Optional[datetime] = None

    class Config:
        from_attributes = True



