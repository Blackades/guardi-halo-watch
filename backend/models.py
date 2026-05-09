from datetime import datetime
from typing import Optional

from sqlalchemy import (
    Boolean,
    Column,
    DateTime,
    Float,
    ForeignKey,
    Integer,
    String,
    Text,
)
from sqlalchemy.orm import relationship

from .database import Base


class Patient(Base):
    __tablename__ = "patients"

    id = Column(Integer, primary_key=True, index=True)
    # Public identifier exposed to dashboard / devices, e.g. "PT-0266"
    patient_id = Column(String, unique=True, index=True, nullable=False)
    name = Column(String, nullable=False)
    age = Column(Integer, nullable=True)

    # Current status / summary
    status = Column(String, default="normal")  # normal | occupied | anomaly | warning
    room = Column(String, index=True, nullable=True)  # e.g. "R101"
    location = Column(String, nullable=True)  # "Room 101" / "Corridor-1"
    last_activity = Column(DateTime, default=datetime.utcnow)
    movement_steps = Column(Integer, default=0)

    # BLE / RFID Identification
    ble_minor = Column(Integer, unique=True, index=True, nullable=True)
    rfid_uid = Column(String, unique=True, index=True, nullable=True)
    wristband_mac = Column(String, nullable=True)

    # Wristband health / tamper
    battery_level = Column(Integer, nullable=True)
    strap_intact = Column(Boolean, default=True)
    skin_contact = Column(Boolean, default=True)
    fall_detected = Column(Boolean, default=False)
    tamper_alert = Column(Boolean, default=False)
    low_battery = Column(Boolean, default=False)

    # Zone restrictions and isolation
    isolation_level = Column(String(20), default="none")  # none, standard, strict
    authorized_zones = Column(Text, nullable=True)  # JSON array of zone codes
    emergency_contact = Column(String(100), nullable=True)
    medical_notes = Column(Text, nullable=True)

    alerts = relationship("Alert", back_populates="patient", cascade="all, delete-orphan")
    locations = relationship(
        "LocationHistory", back_populates="patient", cascade="all, delete-orphan"
    )
    access_logs = relationship(
        "AccessLog", back_populates="patient", cascade="all, delete-orphan"
    )


class Room(Base):
    """
    Logical room / zone within Ward A used for the floor plan and occupancy.
    """

    __tablename__ = "rooms"

    id = Column(Integer, primary_key=True, index=True)
    room_code = Column(String, unique=True, index=True, nullable=False)  # e.g. "R101"
    name = Column(String, nullable=False)  # "Room 101"
    ward = Column(String, default="Ward A")

    # normal | occupied | anomaly | warning (matches dashboard badge variants)
    status = Column(String, default="normal")

    # Normalised 0-100 coordinates for overlaying on the SVG / PNG floor plan
    pos_x = Column(Float, default=0.0)
    pos_y = Column(Float, default=0.0)

    # Optional link to current patient by public id
    patient_id = Column(String, ForeignKey("patients.patient_id"), nullable=True)
    patient = relationship("Patient", backref="current_room", foreign_keys=[patient_id])


class Alert(Base):
    __tablename__ = "alerts"

    id = Column(Integer, primary_key=True, index=True)
    alert_code = Column(String, unique=True, index=True, nullable=False)

    # alert | warning | info | success  (matches NotificationPanel types)
    type = Column(String, default="alert")
    title = Column(String, nullable=False)
    message = Column(Text, nullable=False)

    patient_id = Column(String, ForeignKey("patients.patient_id"), nullable=True)
    room = Column(String, nullable=True)

    status = Column(String, default="active")  # active | acknowledged | resolved
    is_read = Column(Boolean, default=False)
    
    # Enhanced alert tracking
    escalation_level = Column(Integer, default=1)
    acknowledged_at = Column(DateTime, nullable=True)
    acknowledged_by = Column(String, nullable=True)
    resolved_at = Column(DateTime, nullable=True)
    resolved_by = Column(String, nullable=True)
    alert_metadata = Column(Text, nullable=True)  # JSON metadata for alert details

    created_at = Column(DateTime, default=datetime.utcnow, index=True)

    patient = relationship("Patient", back_populates="alerts")


class AccessLog(Base):
    __tablename__ = "access_logs"

    id = Column(Integer, primary_key=True, index=True)
    log_code = Column(String, unique=True, index=True, nullable=False)

    patient_id = Column(String, ForeignKey("patients.patient_id"))
    patient_name = Column(String, nullable=False)
    room = Column(String, nullable=False)

    # entry | exit | denied
    action = Column(String, nullable=False)

    timestamp = Column(DateTime, default=datetime.utcnow, index=True)
    rfid_id = Column(String, nullable=False)

    # Stored in seconds; converted to human-readable string for the UI
    duration_seconds = Column(Integer, nullable=True)

    patient = relationship("Patient", back_populates="access_logs")


class Receiver(Base):
    """
    Receiver node installed in the ward.
    """

    __tablename__ = "receivers"

    id = Column(Integer, primary_key=True, index=True)
    receiver_id = Column(String, unique=True, index=True, nullable=False)

    zone = Column(String, nullable=False)  # "General Ward A"
    room = Column(String, nullable=False)  # "Corridor-1", "Exit Door"

    zone_type = Column(String, default="normal")  # normal | restricted | exit | isolation

    coord_x = Column(Float, default=0.0)
    coord_y = Column(Float, default=0.0)
    coord_z = Column(Float, default=0.0)

    last_seen = Column(DateTime, default=datetime.utcnow)
    wifi_strength = Column(Integer, nullable=True)
    uptime = Column(Integer, default=0)


class LocationHistory(Base):
    """
    Historical movement log generated from receiver detections.
    """

    __tablename__ = "location_history"

    id = Column(Integer, primary_key=True, index=True)
    patient_id = Column(String, ForeignKey("patients.patient_id"), nullable=False)
    receiver_id = Column(String, ForeignKey("receivers.receiver_id"), nullable=True)

    zone = Column(String, nullable=False)
    room = Column(String, nullable=False)

    coord_x = Column(Float, default=0.0)
    coord_y = Column(Float, default=0.0)
    coord_z = Column(Float, default=0.0)

    estimated_distance = Column(Float, nullable=True)
    confidence_score = Column(Float, nullable=True)
    inferred_room = Column(String(100), nullable=True)
    timestamp = Column(DateTime, default=datetime.utcnow, index=True)

    patient = relationship("Patient", back_populates="locations")
    receiver = relationship(
        "Receiver", primaryjoin="LocationHistory.receiver_id==Receiver.receiver_id"
    )


class Zone(Base):
    """
    Zone configuration table for managing restricted areas and access control.
    """
    __tablename__ = "zones"

    id = Column(Integer, primary_key=True, index=True)
    zone_code = Column(String(50), unique=True, index=True, nullable=False)
    zone_name = Column(String(100), nullable=False)
    zone_type = Column(String(20), nullable=False)  # normal, restricted, exit, isolation
    require_authorization = Column(Boolean, default=False)
    max_occupancy = Column(Integer, nullable=True)
    alert_level = Column(String(20), default="info")  # info, warning, alert, critical
    coordinates_json = Column(Text, nullable=True)  # JSON object with boundary coordinates
    created_at = Column(DateTime, default=datetime.utcnow)
    is_active = Column(Boolean, default=True)


class User(Base):
    """
    User authentication and authorization table.
    """
    __tablename__ = "users"

    id = Column(Integer, primary_key=True, index=True)
    username = Column(String(50), unique=True, index=True, nullable=False)
    password_hash = Column(String(255), nullable=False)
    role = Column(String(20), nullable=False)  # admin, nurse, doctor, viewer
    full_name = Column(String(100), nullable=True)
    email = Column(String(100), nullable=True)
    last_login = Column(DateTime, nullable=True)
    failed_login_attempts = Column(Integer, default=0)
    account_locked_until = Column(DateTime, nullable=True)
    created_at = Column(DateTime, default=datetime.utcnow)
    is_active = Column(Boolean, default=True)


class ServerStatus(Base):
    """
    Single-row table used to track server startup time for uptime calculations.
    """

    __tablename__ = "server_status"

    id = Column(Integer, primary_key=True, index=True)
    started_at = Column(DateTime, default=datetime.utcnow)
    last_heartbeat = Column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)



class PatientAssignmentHistory(Base):
    """
    Tracks the history of patient assignments to tags.
    """
    __tablename__ = "patient_assignment_history"

    id = Column(Integer, primary_key=True, index=True)
    patient_id = Column(String, index=True, nullable=False)
    name = Column(String, nullable=False)
    ward = Column(String, nullable=True)
    ble_minor = Column(Integer, nullable=True)
    rfid_uid = Column(String, nullable=True)
    assigned_at = Column(DateTime, nullable=False)
    unassigned_at = Column(DateTime, default=datetime.utcnow)


class RFIDReader(Base):
    """
    RFID reader configuration for multi-door tracking.
    """
    __tablename__ = "rfid_readers"

    id = Column(Integer, primary_key=True, index=True)
    node_id = Column(String(50), nullable=False, index=True)
    reader_id = Column(String(50), nullable=False, index=True)
    gpio_pin = Column(Integer, nullable=False)
    door_name = Column(String(100), nullable=True)
    status = Column(String(20), default="active")  # active, disconnected, error
    last_seen = Column(DateTime, default=datetime.utcnow)
    created_at = Column(DateTime, default=datetime.utcnow)

    __table_args__ = (
        # Ensure unique combination of node_id and reader_id
        # This allows multiple nodes with same reader_id but not duplicates on same node
    )


class DoorEvent(Base):
    """
    Door access events from RFID readers.
    """
    __tablename__ = "door_events"

    id = Column(Integer, primary_key=True, index=True)
    node_id = Column(String(50), nullable=False)
    reader_id = Column(String(50), nullable=False)
    door_name = Column(String(100), nullable=True, index=True)
    rfid_uid = Column(String(50), nullable=False)
    patient_id = Column(String(50), ForeignKey("patients.patient_id"), nullable=True, index=True)
    action = Column(String(10), nullable=False)  # entry, exit
    timestamp = Column(DateTime, default=datetime.utcnow, index=True)

    # Relationships
    patient = relationship("Patient", foreign_keys=[patient_id])


class DistanceCalibration(Base):
    """
    RSSI-to-distance calibration parameters per node.
    """
    __tablename__ = "distance_calibration"

    id = Column(Integer, primary_key=True, index=True)
    node_id = Column(String(50), unique=True, nullable=False, index=True)
    reference_rssi = Column(Float, default=-59.0)  # RSSI at 1 meter
    path_loss_exponent = Column(Float, default=2.0)  # Path-loss exponent
    environmental_factor = Column(Float, default=1.0)  # Environmental adjustment
    calibration_date = Column(DateTime, default=datetime.utcnow)
    calibrated_by = Column(String(50), nullable=True)
    accuracy_r_squared = Column(Float, nullable=True)  # Goodness of fit
